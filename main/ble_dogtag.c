#include "ble_dogtag.h"
#include "dogtag_state.h"
#include "dogtag_ui.h"
#include "dogtag_audio.h"

#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "ble_dogtag";

static const ble_uuid128_t svc_uuid =
    BLE_UUID128_INIT(0x78,0x56,0x34,0x12, 0x78,0x56,0x34,0x12,
                      0x12,0x34,0x56,0x78, 0x9a,0xbc,0xde,0xf0);
static const ble_uuid128_t chr_name_uuid =
    BLE_UUID128_INIT(0x78,0x56,0x34,0x12, 0x78,0x56,0x34,0x12,
                      0x12,0x34,0x56,0x78, 0x9a,0xbc,0xde,0xf1);
static const ble_uuid128_t chr_phone_uuid =
    BLE_UUID128_INIT(0x78,0x56,0x34,0x12, 0x78,0x56,0x34,0x12,
                      0x12,0x34,0x56,0x78, 0x9a,0xbc,0xde,0xf2);
static const ble_uuid128_t chr_batt_uuid =
    BLE_UUID128_INIT(0x78,0x56,0x34,0x12, 0x78,0x56,0x34,0x12,
                      0x12,0x34,0x56,0x78, 0x9a,0xbc,0xde,0xf3);
static const ble_uuid128_t chr_cmd_uuid =
    BLE_UUID128_INIT(0x78,0x56,0x34,0x12, 0x78,0x56,0x34,0x12,
                      0x12,0x34,0x56,0x78, 0x9a,0xbc,0xde,0xf4);

static SemaphoreHandle_t s_host_stopped;
static bool s_initialized;
static bool s_start_requested;
static uint8_t s_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_advertising;
static int8_t s_rssi = -127;
static TimerHandle_t s_rssi_tmr;

static void rssi_poll_cb(TimerHandle_t t) {
    (void)t;
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        s_rssi = -127;
        return;
    }
    int8_t rssi;
    int rc = ble_gap_conn_rssi(s_conn_handle, &rssi);
    if (rc == 0) {
        s_rssi = rssi;
        dogtag_ui_update_rssi(s_rssi);
    }
}

static int gatt_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg);
static int gap_event(struct ble_gap_event *ev, void *arg);
static int advertise(void);
static void adv_stop(void);
static void on_reset(int reason);
static void on_sync(void);
static void host_task(void *arg);

static const struct ble_gatt_svc_def gatt_svcs[] = {{
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &svc_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]){
        { .uuid = &chr_name_uuid.u,  .access_cb = gatt_access, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE },
        { .uuid = &chr_phone_uuid.u, .access_cb = gatt_access, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE },
        { .uuid = &chr_batt_uuid.u,  .access_cb = gatt_access, .flags = BLE_GATT_CHR_F_READ },
        { .uuid = &chr_cmd_uuid.u,   .access_cb = gatt_access, .flags = BLE_GATT_CHR_F_WRITE },
        { 0 },
    },
}, { 0 }};

static int gatt_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg) {
    (void)arg; (void)conn; (void)attr;
    const ble_uuid_t *uuid = ctx->chr->uuid;

    ble_uuid128_t *uuid128 = BLE_UUID128(uuid);
    ESP_LOGD(TAG, "gatt_access op=%d uuid=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
             ctx->op,
             uuid128->value[15], uuid128->value[14], uuid128->value[13], uuid128->value[12],
             uuid128->value[11], uuid128->value[10], uuid128->value[9], uuid128->value[8],
             uuid128->value[7], uuid128->value[6], uuid128->value[5], uuid128->value[4],
             uuid128->value[3], uuid128->value[2], uuid128->value[1], uuid128->value[0]);

    if (ctx->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (ble_uuid_cmp(uuid, &chr_name_uuid.u) == 0) {
            const char *name = dogtag_state_get_owner_name();
            os_mbuf_append(ctx->om, name, strlen(name));
            return 0;
        }
        if (ble_uuid_cmp(uuid, &chr_phone_uuid.u) == 0) {
            const char *phone = dogtag_state_get_owner_phone();
            os_mbuf_append(ctx->om, phone, strlen(phone));
            return 0;
        }
        if (ble_uuid_cmp(uuid, &chr_batt_uuid.u) == 0) {
            extern int bsp_battery_soc(void);
            int soc = bsp_battery_soc();
            uint8_t v = (soc >= 0 && soc <= 100) ? (uint8_t)soc : 0;
            os_mbuf_append(ctx->om, &v, 1);
            return 0;
        }
        ESP_LOGW(TAG, "read unknown uuid");
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ctx->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (ble_uuid_cmp(uuid, &chr_name_uuid.u) == 0) {
            size_t n = ctx->om->om_len < DOGTAG_NAME_MAX-1 ? ctx->om->om_len : DOGTAG_NAME_MAX-1;
            char name[DOGTAG_NAME_MAX];
            memcpy(name, ctx->om->om_data, n);
            name[n] = 0;
            ESP_LOGI(TAG, "name write: %s", name);
            dogtag_state_set_owner_name(name);
            return 0;
        }
        if (ble_uuid_cmp(uuid, &chr_phone_uuid.u) == 0) {
            size_t n = ctx->om->om_len < DOGTAG_PHONE_MAX-1 ? ctx->om->om_len : DOGTAG_PHONE_MAX-1;
            char phone[DOGTAG_PHONE_MAX];
            memcpy(phone, ctx->om->om_data, n);
            phone[n] = 0;
            ESP_LOGI(TAG, "phone write: %s", phone);
            dogtag_state_set_owner_phone(phone);
            return 0;
        }
        if (ble_uuid_cmp(uuid, &chr_cmd_uuid.u) == 0) {
            uint8_t cmd = ctx->om->om_data[0];
            if (cmd == CMD_PAIR_COMPLETE) {
                ESP_LOGI(TAG, "pair complete");
                dogtag_state_set_owner_valid(true);
                dogtag_state_save_nvs();
            } else if (cmd == CMD_FIND_ME && dogtag_state_get_state() == DOGTAG_STATE_SEVERE_LOST) {
                dogtag_audio_find_me_request();
            }
            return 0;
        }
        ESP_LOGW(TAG, "write unknown uuid");
        return BLE_ATT_ERR_UNLIKELY;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int advertise(void) {
    if (s_advertising) return 0;
    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.name = (const uint8_t *)ble_svc_gap_device_name();
    f.name_len = strlen((const char *)f.name);
    f.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv set fields failed: %d", rc);
        return rc;
    }
    struct ble_gap_adv_params p = {0};
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = 0x20;
    p.itvl_max = 0x40;
    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc == 0) {
        s_advertising = true;
        ESP_LOGI(TAG, "advertising started");
    } else {
        ESP_LOGE(TAG, "adv start failed: %d", rc);
    }
    return rc;
}

static void adv_stop(void) {
    if (s_advertising) {
        ble_gap_adv_stop();
        s_advertising = false;
        ESP_LOGI(TAG, "advertising stopped");
    }
}

static int gap_event(struct ble_gap_event *ev, void *arg) {
    (void)arg;
    if (ev->type == BLE_GAP_EVENT_CONNECT) {
        if (ev->connect.status == 0) {
            ESP_LOGI(TAG, "BLE connected");
            s_conn_handle = ev->connect.conn_handle;
            adv_stop();
            xTimerStart(s_rssi_tmr, 0);
            if (dogtag_state_get_state() == DOGTAG_STATE_DEBOUNCE ||
                dogtag_state_get_state() == DOGTAG_STATE_MILD_LOST ||
                dogtag_state_get_state() == DOGTAG_STATE_SEVERE_LOST) {
                ESP_LOGI(TAG, "reconnected -> SILENT");
                dogtag_state_set_state(DOGTAG_STATE_SILENT);
                dogtag_audio_stop();
                dogtag_ui_enter_silent();
            }
        } else {
            ESP_LOGW(TAG, "connect failed: %d", ev->connect.status);
        }
    } else if (ev->type == BLE_GAP_EVENT_DISCONNECT) {
        ESP_LOGI(TAG, "BLE disconnected, reason=%d", ev->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_rssi = -127;
        xTimerStop(s_rssi_tmr, 0);
        dogtag_ui_update_rssi(-127);
        if (dogtag_state_get_state() == DOGTAG_STATE_PAIRING && dogtag_state_is_owner_valid()) {
            ESP_LOGI(TAG, "pair done -> SILENT");
            dogtag_state_set_state(DOGTAG_STATE_SILENT);
            dogtag_ui_enter_silent();
            advertise();
        } else if (dogtag_state_get_state() == DOGTAG_STATE_SILENT && dogtag_state_is_owner_valid()) {
            ESP_LOGI(TAG, "-> DEBOUNCE");
            dogtag_state_set_state(DOGTAG_STATE_DEBOUNCE);
            dogtag_audio_stop();
            dogtag_ui_enter_lost();
            advertise();
            dogtag_state_start_debounce_timer();
        } else {
            advertise();
        }
    } else if (ev->type == BLE_GAP_EVENT_ADV_COMPLETE) {
        advertise();
    }
    return 0;
}

static void on_reset(int reason) {
    ESP_LOGE(TAG, "nimble reset %d", reason);
}

static void on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "addr infer failed: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "on_sync addr_type=%d", s_addr_type);
    if (s_start_requested) {
        advertise();
    }
}

static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    if (s_host_stopped) xSemaphoreGive(s_host_stopped);
    nimble_port_freertos_deinit();
}

esp_err_t ble_dogtag_init(void) {
    if (s_initialized) return ESP_ERR_INVALID_STATE;

    extern esp_err_t demo_radio_nvs_prepare(void);
    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS prepare failed: %d", err);
        return err;
    }

    err = nimble_port_init();
    if (err != ESP_OK) return err;
    s_initialized = true;

    s_host_stopped = xSemaphoreCreateBinary();
    if (!s_host_stopped) {
        nimble_port_deinit();
        s_initialized = false;
        return ESP_ERR_NO_MEM;
    }

    if (!s_rssi_tmr) {
        s_rssi_tmr = xTimerCreate("rssi", pdMS_TO_TICKS(1000), pdTRUE, NULL, rssi_poll_cb);
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();

    char name[24];
    snprintf(name, sizeof(name), "%s%04X", DOGTAG_ADV_PREFIX, (unsigned)(esp_random() & 0xFFFF));
    int rc = ble_svc_gap_device_name_set(name);
    if (rc != 0) {
        ESP_LOGE(TAG, "device name set failed: %d", rc);
        goto fail;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;

    rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatts count cfg failed: %d", rc);
        goto fail;
    }

    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatts add svcs failed: %d", rc);
        goto fail;
    }

    s_start_requested = true;
    nimble_port_freertos_init(host_task);
    return ESP_OK;

fail:
    vSemaphoreDelete(s_host_stopped);
    s_host_stopped = NULL;
    nimble_port_deinit();
    s_initialized = false;
    return ESP_FAIL;
}

void ble_dogtag_deinit(void) {
    s_start_requested = false;
    if (!s_initialized) return;
    adv_stop();
    int rc = nimble_port_stop();
    if (rc == 0 && s_host_stopped) {
        xSemaphoreTake(s_host_stopped, portMAX_DELAY);
    }
    if (rc == 0) {
        nimble_port_deinit();
        s_initialized = false;
    }
    if (s_host_stopped) {
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
    }
}

void ble_dogtag_start_advertising(void) {
    if (s_start_requested) {
        advertise();
    }
}

void ble_dogtag_stop_advertising(void) {
    adv_stop();
}

bool ble_dogtag_is_connected(void) {
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

uint16_t ble_dogtag_get_conn_handle(void) {
    return s_conn_handle;
}

dogtag_state_t ble_dogtag_get_state(void) {
    return dogtag_state_get_state();
}

void ble_dogtag_set_state(dogtag_state_t state) {
    dogtag_state_set_state(state);
}

const char *ble_dogtag_get_owner_name(void) {
    return dogtag_state_get_owner_name();
}

const char *ble_dogtag_get_owner_phone(void) {
    return dogtag_state_get_owner_phone();
}

bool ble_dogtag_is_owner_valid(void) {
    return dogtag_state_is_owner_valid();
}

void ble_dogtag_set_owner(const char *name, const char *phone) {
    dogtag_state_set_owner(name, phone);
}

void ble_dogtag_clear_owner(void) {
    dogtag_state_clear_owner();
}

int8_t ble_dogtag_get_rssi(void) {
    return s_rssi;
}