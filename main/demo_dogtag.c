// main/demo_dogtag.c — 电子狗牌：防抖 + 轻度走失 + 重度走失
#include "demo.h"
#include "demo_radio.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "ui_pixel.h"

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

// 中文字体（lv_font_conv 生成）
extern const lv_font_t font_cn_16;
extern const lv_font_t font_cn_20;

static const char *TAG = "demo_dogtag";

// ---------------------------------------------------------------------------
#define DOGTAG_NAME_MAX   32
#define DOGTAG_PHONE_MAX  20
#define DOGTAG_ADV_PREFIX "DogTag"
#define DEBOUNCE_SEC_DEFAULT       2
#define MILD_TIMEOUT_SEC_DEFAULT   600

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

#define CMD_PAIR_COMPLETE  0x01
#define CMD_FIND_ME        0x02

typedef enum {
    STATE_INIT = 0,
    STATE_PAIRING,
    STATE_SILENT,
    STATE_DEBOUNCE,
    STATE_MILD_LOST,
    STATE_SEVERE_LOST,
} dogtag_state_t;

static dogtag_state_t s_state = STATE_INIT;
static char s_owner_name[DOGTAG_NAME_MAX];
static char s_owner_phone[DOGTAG_PHONE_MAX];
static bool s_owner_valid;

static SemaphoreHandle_t s_host_stopped;
static bool s_initialized;
static bool s_start_requested;
static uint8_t s_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_advertising;

static TimerHandle_t s_debounce_tmr;
static TimerHandle_t s_countdown_tmr;
static TimerHandle_t s_flash_tmr;
static int s_countdown;
static volatile bool s_flash_on;

static lv_obj_t *s_scr, *s_panel, *s_lbl_title, *s_lbl_soc;
static lv_obj_t *s_lbl_name, *s_lbl_phone, *s_lbl_sos, *s_lbl_cd;
static lv_obj_t *s_mascot;
static lv_timer_t *s_ui_tmr;

static TaskHandle_t s_audio_task;
static volatile bool s_audio_stop;

// ---------------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------------
static void nvs_load(void) {
    nvs_handle_t h;
    if (nvs_open("dogtag", NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s_owner_name);
    if (nvs_get_str(h, "name", s_owner_name, &len) != ESP_OK) goto out;
    len = sizeof(s_owner_phone);
    if (nvs_get_str(h, "phone", s_owner_phone, &len) != ESP_OK) goto out;
    s_owner_valid = true;
out:
    nvs_close(h);
}
static void nvs_save(void) {
    nvs_handle_t h;
    if (nvs_open("dogtag", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "name", s_owner_name);
    nvs_set_str(h, "phone", s_owner_phone);
    nvs_commit(h);
    nvs_close(h);
}

// ---------------------------------------------------------------------------
// 音频
// ---------------------------------------------------------------------------
extern const uint8_t dogtag_help_wav_start[] asm("_binary_dogtag_help_wav_start");
extern const uint8_t dogtag_help_wav_end[]   asm("_binary_dogtag_help_wav_end");

static void play_wav(int volume) {
    const uint8_t *data = dogtag_help_wav_start;
    size_t len = (size_t)(dogtag_help_wav_end - dogtag_help_wav_start);
    if (len < 44 || memcmp(data, "RIFF", 4) != 0) return;
    size_t pos = 12;
    const uint8_t *pcm = NULL;
    uint32_t pcm_len = 0;
    while (pos + 8 <= len) {
        uint32_t cs = *(const uint32_t *)(data + pos + 4);
        if (memcmp(data + pos, "data", 4) == 0) { pcm = data + pos + 8; pcm_len = cs; break; }
        pos += 8 + cs;
    }
    if (!pcm) return;
    bsp_audio_set_format(16000, 16, 1);
    bsp_audio_set_volume(volume);
    size_t written = 0;
    while (written < pcm_len && !s_audio_stop) {
        size_t n = (pcm_len - written) > 4096 ? 4096 : (pcm_len - written);
        bsp_audio_write((void *)(pcm + written), n);
        written += n;
    }
}

static void play_beep(void) {
    if (s_audio_stop) return;
    bsp_audio_set_format(8000, 16, 1);
    bsp_audio_set_volume(30);
    int16_t buf[800];
    for (int i = 0; i < 800; i++)
        buf[i] = ((i % 8) < 4) ? 3000 : -3000;
    bsp_audio_write(buf, sizeof(buf));
}

static void audio_task(void *arg) {
    (void)arg;
    for (;;) {
        if (s_state == STATE_MILD_LOST && !s_audio_stop) {
            play_beep();
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        if (s_state == STATE_SEVERE_LOST && !s_audio_stop) {
            play_wav(90);
            if (s_audio_stop) continue;
            for (int i = 0; i < 3 && !s_audio_stop; i++) {
                play_beep();
                vTaskDelay(pdMS_TO_TICKS(300));
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ---------------------------------------------------------------------------
// 定时器回调
// ---------------------------------------------------------------------------
static void debounce_cb(TimerHandle_t t) {
    (void)t;
    ESP_LOGI(TAG, "debounce -> MILD_LOST");
    s_state = STATE_MILD_LOST;
    s_countdown = MILD_TIMEOUT_SEC_DEFAULT;
    s_audio_stop = false;
    if (s_countdown_tmr) xTimerStart(s_countdown_tmr, 0);
    if (s_flash_tmr) xTimerStart(s_flash_tmr, 0);
}

static void countdown_cb(TimerHandle_t t) {
    (void)t;
    if (s_countdown > 0) s_countdown--;
    if (s_countdown <= 0 && s_state == STATE_MILD_LOST) {
        ESP_LOGI(TAG, "countdown -> SEVERE_LOST");
        s_state = STATE_SEVERE_LOST;
        if (s_flash_tmr) xTimerStop(s_flash_tmr, 0);
        if (s_countdown_tmr) xTimerStop(s_countdown_tmr, 0);
    }
}

static void flash_cb(TimerHandle_t t) {
    (void)t;
    s_flash_on = !s_flash_on;
}

// ---------------------------------------------------------------------------
// 屏幕
// ---------------------------------------------------------------------------
static void destroy_screen(void) {
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
    s_panel = s_lbl_title = s_lbl_soc = s_lbl_name = NULL;
    s_lbl_phone = s_lbl_sos = s_lbl_cd = s_mascot = NULL;
}

static void ui_tick(lv_timer_t *t) {
    (void)t;
    if ((s_state == STATE_MILD_LOST || s_state == STATE_SEVERE_LOST) && s_panel) {
        lv_color_t bg = s_flash_on ? lv_color_hex(0xFF4444) : lv_color_hex(UI_PAPER);
        lv_obj_set_style_bg_color(s_panel, bg, 0);
    }
    if (s_state == STATE_MILD_LOST && s_lbl_cd) {
        lv_label_set_text_fmt(s_lbl_cd, "%d:%02d", s_countdown / 60, s_countdown % 60);
    }
    if ((s_state == STATE_MILD_LOST || s_state == STATE_SEVERE_LOST) && s_lbl_soc) {
        int soc = bsp_battery_soc();
        lv_label_set_text_fmt(s_lbl_soc, "%d%%", soc);
    }
}

static void enter_pairing_screen(void) {
    if (!bsp_lvgl_lock(500)) return;
    destroy_screen();
    s_scr = ui_pixel_screen_create("DOG TAG");
    lv_obj_t *panel = ui_pixel_panel_create(s_scr, 22, 58, 196, 180, UI_PAPER);
    s_lbl_sos = lv_label_create(panel);
    lv_obj_set_style_text_font(s_lbl_sos, &font_cn_16, 0);
    lv_obj_set_width(s_lbl_sos, 168);
    lv_obj_set_style_text_align(s_lbl_sos, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_lbl_sos, lv_color_hex(UI_INK), 0);
    lv_obj_center(s_lbl_sos);
    lv_label_set_text(s_lbl_sos, "\n\n配对模式\n\n请扫描蓝牙\n设置主人信息");
    s_mascot = ui_pixel_mascot_create(s_scr, 101, 244);
    lv_screen_load(s_scr);
    bsp_lvgl_unlock();
}

static void enter_lost_screen(void) {
    if (!bsp_lvgl_lock(500)) return;
    destroy_screen();
    s_scr = ui_pixel_screen_create("DOG TAG");
    s_panel = ui_pixel_panel_create(s_scr, 18, 54, 204, 190, UI_PAPER);

    s_lbl_title = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_title, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_hex(UI_INK), 0);
    lv_obj_align(s_lbl_title, LV_ALIGN_TOP_MID, 0, 4);
    lv_label_set_text(s_lbl_title, "DOG TAG");

    s_lbl_soc = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_soc, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_lbl_soc, lv_color_hex(UI_INK), 0);
    lv_obj_align(s_lbl_soc, LV_ALIGN_TOP_LEFT, 6, 26);

    s_lbl_cd = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_cd, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_lbl_cd, lv_color_hex(UI_RED), 0);
    lv_obj_align(s_lbl_cd, LV_ALIGN_TOP_LEFT, 6, 46);

    s_lbl_name = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_name, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_lbl_name, lv_color_hex(UI_INK), 0);
    lv_obj_align(s_lbl_name, LV_ALIGN_TOP_LEFT, 6, 70);

    s_lbl_phone = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_phone, &font_cn_20, 0);
    lv_obj_set_style_text_color(s_lbl_phone, lv_color_hex(UI_RED), 0);
    lv_obj_align(s_lbl_phone, LV_ALIGN_TOP_LEFT, 6, 94);

    s_lbl_sos = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_sos, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_lbl_sos, lv_color_hex(UI_RED), 0);
    lv_obj_align(s_lbl_sos, LV_ALIGN_TOP_MID, 0, 128);
    lv_label_set_text(s_lbl_sos, "SOS");

    s_mascot = ui_pixel_mascot_create(s_scr, 101, 244);
    lv_screen_load(s_scr);
    bsp_lvgl_unlock();
}

// BLE screens for silent state
static void enter_silent_screen(void) {
    if (!bsp_lvgl_lock(500)) return;
    destroy_screen();
    s_scr = ui_pixel_screen_create("DOG TAG");
    s_panel = ui_pixel_panel_create(s_scr, 18, 54, 204, 190, UI_PAPER);

    s_lbl_title = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_title, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_hex(UI_INK), 0);
    lv_obj_align(s_lbl_title, LV_ALIGN_TOP_MID, 0, 4);
    lv_label_set_text(s_lbl_title, "DOG TAG");

    s_lbl_soc = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_soc, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_lbl_soc, lv_color_hex(UI_INK), 0);
    lv_obj_align(s_lbl_soc, LV_ALIGN_TOP_LEFT, 6, 26);
    lv_label_set_text(s_lbl_soc, "BLE");

    s_mascot = ui_pixel_mascot_create(s_scr, 101, 244);
    lv_screen_load(s_scr);
    bsp_lvgl_unlock();
}

// ---------------------------------------------------------------------------
// BLE
// ---------------------------------------------------------------------------
static int gap_event(struct ble_gap_event *ev, void *arg);

static int advertise_conn(void) {
    if (s_advertising) return 0;
    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.name = (const uint8_t *)ble_svc_gap_device_name();
    f.name_len = strlen((const char *)f.name);
    f.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc != 0) return rc;
    struct ble_gap_adv_params p = {0};
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc == 0) s_advertising = true;
    return rc;
}

static int advertise_nonconn(void) {
    if (s_advertising) return 0;
    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    char name[24];
    snprintf(name, sizeof(name), "%s", DOGTAG_ADV_PREFIX);
    f.name = (const uint8_t *)name;
    f.name_len = strlen(name);
    f.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc != 0) return rc;
    struct ble_gap_adv_params p = {0};
    p.conn_mode = BLE_GAP_CONN_MODE_NON;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc == 0) s_advertising = true;
    return rc;
}

static void adv_stop(void) {
    if (s_advertising) { ble_gap_adv_stop(); s_advertising = false; }
}

static int gatt_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg) {
    (void)arg; (void)conn; (void)attr;
    const ble_uuid_t *uuid = ctx->chr->uuid;

    if (ctx->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (ble_uuid_cmp(uuid, &chr_name_uuid.u) == 0) {
            os_mbuf_append(ctx->om, s_owner_name, strlen(s_owner_name)); return 0;
        }
        if (ble_uuid_cmp(uuid, &chr_phone_uuid.u) == 0) {
            os_mbuf_append(ctx->om, s_owner_phone, strlen(s_owner_phone)); return 0;
        }
        if (ble_uuid_cmp(uuid, &chr_batt_uuid.u) == 0) {
            int soc = bsp_battery_soc();
            uint8_t v = (soc >= 0 && soc <= 100) ? (uint8_t)soc : 0;
            os_mbuf_append(ctx->om, &v, 1); return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ctx->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (ble_uuid_cmp(uuid, &chr_name_uuid.u) == 0) {
            size_t n = ctx->om->om_len < DOGTAG_NAME_MAX-1 ? ctx->om->om_len : DOGTAG_NAME_MAX-1;
            memcpy(s_owner_name, ctx->om->om_data, n); s_owner_name[n] = 0;
            ESP_LOGI(TAG, "name: %s", s_owner_name); return 0;
        }
        if (ble_uuid_cmp(uuid, &chr_phone_uuid.u) == 0) {
            size_t n = ctx->om->om_len < DOGTAG_PHONE_MAX-1 ? ctx->om->om_len : DOGTAG_PHONE_MAX-1;
            memcpy(s_owner_phone, ctx->om->om_data, n); s_owner_phone[n] = 0;
            ESP_LOGI(TAG, "phone: %s", s_owner_phone); return 0;
        }
        if (ble_uuid_cmp(uuid, &chr_cmd_uuid.u) == 0) {
            uint8_t cmd = ctx->om->om_data[0];
            if (cmd == CMD_PAIR_COMPLETE) {
                s_owner_valid = true; nvs_save();
                ESP_LOGI(TAG, "pair complete");
                ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
            } else if (cmd == CMD_FIND_ME && s_state == STATE_SEVERE_LOST) {
                play_wav(90);
            }
            return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

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

static int gap_event(struct ble_gap_event *ev, void *arg) {
    (void)arg;
    if (ev->type == BLE_GAP_EVENT_CONNECT) {
        if (ev->connect.status == 0) {
            ESP_LOGI(TAG, "BLE connected");
            s_conn_handle = ev->connect.conn_handle;
            adv_stop();
            if (s_state == STATE_DEBOUNCE || s_state == STATE_MILD_LOST || s_state == STATE_SEVERE_LOST) {
                ESP_LOGI(TAG, "reconnected -> SILENT");
                s_state = STATE_SILENT;
                s_audio_stop = true;
                if (s_debounce_tmr) xTimerStop(s_debounce_tmr, 0);
                if (s_countdown_tmr) xTimerStop(s_countdown_tmr, 0);
                if (s_flash_tmr) xTimerStop(s_flash_tmr, 0);
                enter_silent_screen();
            }
            struct ble_gap_upd_params u = { .itvl_min=0x1000, .itvl_max=0x1000, .latency=4, .supervision_timeout=600 };
            ble_gap_update_params(s_conn_handle, &u);
        }
    } else if (ev->type == BLE_GAP_EVENT_DISCONNECT) {
        ESP_LOGI(TAG, "BLE disconnected");
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        if (s_state == STATE_SILENT && s_owner_valid) {
            ESP_LOGI(TAG, "-> DEBOUNCE");
            s_state = STATE_DEBOUNCE;
            enter_lost_screen();
            advertise_nonconn();
            if (s_debounce_tmr) xTimerReset(s_debounce_tmr, 0);
        }
    } else if (ev->type == BLE_GAP_EVENT_ADV_COMPLETE) {
        if (s_state == STATE_MILD_LOST || s_state == STATE_SEVERE_LOST) advertise_nonconn();
        else if (s_advertising) advertise_conn();
    }
    return 0;
}

static void on_reset(int reason) { ESP_LOGE(TAG, "nimble reset %d", reason); }

static void on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc != 0) { ESP_LOGE(TAG, "addr infer failed"); return; }
    if (s_state == STATE_PAIRING) advertise_conn();
    else if (s_state == STATE_MILD_LOST || s_state == STATE_SEVERE_LOST) advertise_nonconn();
}

static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    if (s_host_stopped) xSemaphoreGive(s_host_stopped);
    nimble_port_freertos_deinit();
}

static esp_err_t ble_start(void) {
    if (s_initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;
    err = nimble_port_init();
    if (err != ESP_OK) return err;
    s_initialized = true;
    s_host_stopped = xSemaphoreCreateBinary();
    if (!s_host_stopped) { nimble_port_deinit(); s_initialized = false; return ESP_ERR_NO_MEM; }
    ble_svc_gap_init(); ble_svc_gatt_init();
    char name[24]; snprintf(name, sizeof(name), "%s%04X", DOGTAG_ADV_PREFIX, (unsigned)(esp_random() & 0xFFFF));
    ble_svc_gap_device_name_set(name);
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    s_start_requested = true;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

static void ble_stop(void) {
    s_start_requested = false;
    if (!s_initialized) return;
    adv_stop();
    int rc = nimble_port_stop();
    if (rc == 0 && s_host_stopped) xSemaphoreTake(s_host_stopped, portMAX_DELAY);
    if (rc == 0) { nimble_port_deinit(); s_initialized = false; }
    if (s_host_stopped) { vSemaphoreDelete(s_host_stopped); s_host_stopped = NULL; }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void demo_dogtag_enter(void) {
    ESP_LOGI(TAG, "enter");
    nvs_load();

    if (s_owner_valid) {
        s_state = STATE_SILENT;
        ESP_LOGI(TAG, "owner: %s/%s", s_owner_name, s_owner_phone);
    } else {
        s_state = STATE_PAIRING;
        ESP_LOGI(TAG, "no owner -> pairing");
    }

    if (!s_debounce_tmr)
        s_debounce_tmr = xTimerCreate("deb", pdMS_TO_TICKS(DEBOUNCE_SEC_DEFAULT*1000), pdFALSE, NULL, debounce_cb);
    if (!s_countdown_tmr)
        s_countdown_tmr = xTimerCreate("cd", pdMS_TO_TICKS(1000), pdTRUE, NULL, countdown_cb);
    if (!s_flash_tmr)
        s_flash_tmr = xTimerCreate("flash", pdMS_TO_TICKS(500), pdTRUE, NULL, flash_cb);

    ble_start();
    s_audio_stop = (s_state == STATE_SILENT);
    if (!s_audio_task) xTaskCreate(audio_task, "dogtag_audio", 4096, NULL, 3, &s_audio_task);
    if (!s_ui_tmr) s_ui_tmr = lv_timer_create(ui_tick, 1000, NULL);

    if (s_state == STATE_PAIRING) enter_pairing_screen();
    else enter_silent_screen();
}

void demo_dogtag_exit(void) {
    ESP_LOGI(TAG, "exit");
    s_audio_stop = true;
    if (s_audio_task) { vTaskDelete(s_audio_task); s_audio_task = NULL; }
    if (s_ui_tmr) { lv_timer_delete(s_ui_tmr); s_ui_tmr = NULL; }
    if (s_debounce_tmr) { xTimerDelete(s_debounce_tmr, 0); s_debounce_tmr = NULL; }
    if (s_countdown_tmr) { xTimerDelete(s_countdown_tmr, 0); s_countdown_tmr = NULL; }
    if (s_flash_tmr) { xTimerDelete(s_flash_tmr, 0); s_flash_tmr = NULL; }
    if (!bsp_lvgl_lock(500)) return;
    destroy_screen();
    bsp_lvgl_unlock();
    ble_stop();
    s_state = STATE_INIT;
}

void demo_dogtag_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_OK && s_state == STATE_PAIRING && s_owner_valid) {
        nvs_save();
        s_state = STATE_SILENT;
        s_audio_stop = true;
        enter_silent_screen();
    }
    if (btn == BSP_BTN_UP && (s_state == STATE_MILD_LOST || s_state == STATE_SEVERE_LOST)) {
        if (bsp_lvgl_lock(500)) { ui_pixel_mascot_jump(s_mascot); bsp_lvgl_unlock(); }
        play_wav(90);
    }
}
