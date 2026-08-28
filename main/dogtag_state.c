#include "dogtag_state.h"
#include "dogtag_ui.h"
#include "dogtag_audio.h"

#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "dogtag_state";

#define DOGTAG_NAME_MAX   32
#define DOGTAG_PHONE_MAX  20
#define DEBOUNCE_SEC_DEFAULT       2
#define MILD_TIMEOUT_SEC_DEFAULT   600

static dogtag_state_t s_state = DOGTAG_STATE_INIT;
static char s_owner_name[DOGTAG_NAME_MAX] = {0};
static char s_owner_phone[DOGTAG_PHONE_MAX] = {0};
static bool s_owner_valid = false;

static TimerHandle_t s_debounce_tmr;
static TimerHandle_t s_countdown_tmr;
static TimerHandle_t s_flash_tmr;
static int s_countdown;
static volatile bool s_flash_on;

static void debounce_cb(TimerHandle_t t) {
    (void)t;
    ESP_LOGI(TAG, "debounce -> MILD_LOST");
    s_state = DOGTAG_STATE_MILD_LOST;
    s_countdown = MILD_TIMEOUT_SEC_DEFAULT;
    dogtag_audio_stop_clear();
    if (s_countdown_tmr) xTimerStart(s_countdown_tmr, 0);
    if (s_flash_tmr) xTimerStart(s_flash_tmr, 0);
    dogtag_ui_enter_lost();
}

static void countdown_cb(TimerHandle_t t) {
    (void)t;
    if (s_countdown > 0) s_countdown--;
    if (s_countdown <= 0 && s_state == DOGTAG_STATE_MILD_LOST) {
        ESP_LOGI(TAG, "countdown -> SEVERE_LOST");
        s_state = DOGTAG_STATE_SEVERE_LOST;
        if (s_flash_tmr) xTimerStop(s_flash_tmr, 0);
        if (s_countdown_tmr) xTimerStop(s_countdown_tmr, 0);
        dogtag_ui_enter_lost();
    }
}

static void flash_cb(TimerHandle_t t) {
    (void)t;
    s_flash_on = !s_flash_on;
    dogtag_ui_update_flash(s_flash_on);
}

void dogtag_state_init(void) {
    s_state = DOGTAG_STATE_INIT;
    s_owner_name[0] = 0;
    s_owner_phone[0] = 0;
    s_owner_valid = false;
    s_countdown = 0;
    s_flash_on = false;

    if (!s_debounce_tmr) {
        s_debounce_tmr = xTimerCreate("deb", pdMS_TO_TICKS(DEBOUNCE_SEC_DEFAULT*1000), pdFALSE, NULL, debounce_cb);
    }
    if (!s_countdown_tmr) {
        s_countdown_tmr = xTimerCreate("cd", pdMS_TO_TICKS(1000), pdTRUE, NULL, countdown_cb);
    }
    if (!s_flash_tmr) {
        s_flash_tmr = xTimerCreate("flash", pdMS_TO_TICKS(500), pdTRUE, NULL, flash_cb);
    }
}

void dogtag_state_deinit(void) {
    if (s_debounce_tmr) { xTimerDelete(s_debounce_tmr, 0); s_debounce_tmr = NULL; }
    if (s_countdown_tmr) { xTimerDelete(s_countdown_tmr, 0); s_countdown_tmr = NULL; }
    if (s_flash_tmr) { xTimerDelete(s_flash_tmr, 0); s_flash_tmr = NULL; }
}

dogtag_state_t dogtag_state_get_state(void) {
    return s_state;
}

void dogtag_state_set_state(dogtag_state_t state) {
    s_state = state;
}

const char *dogtag_state_get_owner_name(void) {
    return s_owner_name;
}

const char *dogtag_state_get_owner_phone(void) {
    return s_owner_phone;
}

bool dogtag_state_is_owner_valid(void) {
    return s_owner_valid;
}

void dogtag_state_set_owner(const char *name, const char *phone) {
    if (name) {
        strncpy(s_owner_name, name, DOGTAG_NAME_MAX-1);
        s_owner_name[DOGTAG_NAME_MAX-1] = 0;
    }
    if (phone) {
        strncpy(s_owner_phone, phone, DOGTAG_PHONE_MAX-1);
        s_owner_phone[DOGTAG_PHONE_MAX-1] = 0;
    }
    s_owner_valid = true;
    dogtag_state_save_nvs();
}

void dogtag_state_set_owner_name(const char *name) {
    if (name) {
        strncpy(s_owner_name, name, DOGTAG_NAME_MAX-1);
        s_owner_name[DOGTAG_NAME_MAX-1] = 0;
    }
}

void dogtag_state_set_owner_phone(const char *phone) {
    if (phone) {
        strncpy(s_owner_phone, phone, DOGTAG_PHONE_MAX-1);
        s_owner_phone[DOGTAG_PHONE_MAX-1] = 0;
    }
}

void dogtag_state_set_owner_valid(bool valid) {
    s_owner_valid = valid;
    if (valid) {
        dogtag_state_save_nvs();
    }
}

void dogtag_state_clear_owner(void) {
    s_owner_name[0] = 0;
    s_owner_phone[0] = 0;
    s_owner_valid = false;
    dogtag_state_save_nvs();
}

void dogtag_state_load_nvs(void) {
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

void dogtag_state_save_nvs(void) {
    nvs_handle_t h;
    if (nvs_open("dogtag", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "name", s_owner_name);
    nvs_set_str(h, "phone", s_owner_phone);
    nvs_commit(h);
    nvs_close(h);
}

void dogtag_state_start_debounce_timer(void) {
    if (s_debounce_tmr) xTimerStart(s_debounce_tmr, 0);
}

void dogtag_state_stop_all_timers(void) {
    if (s_debounce_tmr) xTimerStop(s_debounce_tmr, 0);
    if (s_countdown_tmr) xTimerStop(s_countdown_tmr, 0);
    if (s_flash_tmr) xTimerStop(s_flash_tmr, 0);
}

int dogtag_state_get_countdown(void) {
    return s_countdown;
}