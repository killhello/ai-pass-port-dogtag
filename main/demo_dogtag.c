// main/demo_dogtag.c — 电子狗牌主入口
#include "demo.h"
#include "ble_dogtag.h"
#include "dogtag_state.h"
#include "dogtag_ui.h"
#include "dogtag_audio.h"
#include "wifi_portal.h"
#include "demo_radio.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "demo_dogtag";

static void wifi_config_cb(const char *name, const char *phone) {
    ESP_LOGI(TAG, "WiFi config received: name=%s, phone=%s", name, phone);
    dogtag_state_set_owner(name, phone);
    dogtag_state_set_owner_valid(true);
    dogtag_state_save_nvs();
}

void demo_dogtag_enter(void) {
    ESP_LOGI(TAG, "enter");

    dogtag_state_init();
    dogtag_state_load_nvs();

    if (dogtag_state_is_owner_valid()) {
        dogtag_state_set_state(DOGTAG_STATE_SILENT);
        ESP_LOGI(TAG, "owner: %s/%s",
                 dogtag_state_get_owner_name(),
                 dogtag_state_get_owner_phone());
    } else {
        dogtag_state_set_state(DOGTAG_STATE_PAIRING);
        ESP_LOGI(TAG, "no owner -> pairing");
    }

    dogtag_ui_init();
    dogtag_audio_init();

    esp_err_t err = ble_dogtag_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ble init failed: %d", err);
        return;
    }

    wifi_portal_set_config_callback(wifi_config_cb);
    err = wifi_portal_init();
    if (err == ESP_OK) {
        wifi_portal_start();
    } else {
        ESP_LOGW(TAG, "wifi portal init failed: %d", err);
    }

    dogtag_audio_stop_clear();

    if (dogtag_state_get_state() == DOGTAG_STATE_PAIRING) {
        dogtag_ui_enter_pairing();
    } else {
        dogtag_ui_enter_silent();
    }
}

void demo_dogtag_exit(void) {
    ESP_LOGI(TAG, "exit");

    wifi_portal_deinit();
    dogtag_audio_deinit();
    dogtag_ui_deinit();
    dogtag_state_deinit();
    ble_dogtag_deinit();

    dogtag_state_set_state(DOGTAG_STATE_INIT);
}

void demo_dogtag_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK) return;

    dogtag_state_t state = dogtag_state_get_state();

    if (btn == BSP_BTN_OK && state == DOGTAG_STATE_PAIRING && dogtag_state_is_owner_valid()) {
        dogtag_state_save_nvs();
        dogtag_state_set_state(DOGTAG_STATE_SILENT);
        dogtag_audio_stop();
        dogtag_ui_enter_silent();
    }
    if (btn == BSP_BTN_OK && (state == DOGTAG_STATE_MILD_LOST || state == DOGTAG_STATE_SEVERE_LOST)) {
        dogtag_state_set_state(DOGTAG_STATE_SILENT);
        dogtag_audio_stop();
        dogtag_state_stop_all_timers();
        ble_dogtag_stop_advertising();
        dogtag_ui_enter_silent();
    }
    if (btn == BSP_BTN_UP && (state == DOGTAG_STATE_MILD_LOST || state == DOGTAG_STATE_SEVERE_LOST)) {
        if (dogtag_ui_lock()) {
            lv_obj_t *mascot = dogtag_ui_get_mascot();
            extern void ui_pixel_mascot_jump(lv_obj_t *);
            ui_pixel_mascot_jump(mascot);
            dogtag_ui_unlock();
        }
        dogtag_audio_play_help(90);
    }
}