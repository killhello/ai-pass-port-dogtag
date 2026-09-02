#include "dogtag_ui.h"
#include "dogtag_state.h"
#include "ui_pixel.h"
#include "bsp_display.h"

#include "esp_log.h"
#include "lvgl.h"
#include "driver/soc.h"
#include "esp_timer.h"

extern const lv_font_t font_cn_16;
extern const lv_font_t font_cn_20;

static lv_obj_t *s_scr;
static lv_obj_t *s_panel;
static lv_obj_t *s_lbl_time;
static lv_obj_t *s_lbl_sub;
static lv_obj_t *s_lbl_soc;
static lv_obj_t *s_lbl_rssi;
static lv_obj_t *s_lbl_name;
static lv_obj_t *s_lbl_phone;
static lv_obj_t *s_lbl_sos;
static lv_obj_t *s_lbl_cd;
static lv_timer_t *s_ui_tmr;

static void destroy_screen(void) {
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    s_panel = s_lbl_time = s_lbl_sub = s_lbl_soc = s_lbl_rssi = s_lbl_name = NULL;
    s_lbl_phone = s_lbl_sos = s_lbl_cd = NULL;
}

void dogtag_ui_init(void) {
    s_scr = s_panel = s_lbl_time = s_lbl_sub = s_lbl_soc = s_lbl_rssi = NULL;
    s_lbl_name = s_lbl_phone = s_lbl_sos = s_lbl_cd = NULL;
    s_ui_tmr = NULL;
}

void dogtag_ui_deinit(void) {
    if (!bsp_lvgl_lock(500)) return;
    destroy_screen();
    if (s_ui_tmr) { lv_timer_delete(s_ui_tmr); s_ui_tmr = NULL; }
    bsp_lvgl_unlock();
}

static void update_time_label(void) {
    if (!s_lbl_time) return;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *t = localtime(&tv.tv_sec);
    lv_label_set_text_fmt(s_lbl_time, "%02d:%02d", t->tm_hour, t->tm_min);
}

static void time_timer_cb(lv_timer_t *timer) {
    (void)timer;
    update_time_label();
}

void dogtag_ui_enter_pairing(void) {
    if (!bsp_lvgl_lock(500)) return;
    destroy_screen();
    s_scr = ui_pixel_screen_create("");

    lv_obj_set_style_bg_color(s_scr, lv_color_hex(UI_TEAL), 0);

    s_lbl_time = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_lbl_time, &font_cn_20, 0);
    lv_obj_set_style_text_color(s_lbl_time, lv_color_hex(UI_WHITE), 0);
    lv_obj_align(s_lbl_time, LV_ALIGN_TOP_MID, 0, 20);
    update_time_label();

    s_lbl_sub = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_lbl_sub, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_lbl_sub, lv_color_hex(UI_WHITE), 0);
    lv_obj_set_style_text_opa(s_lbl_sub, LV_OPA_70, 0);
    lv_obj_align(s_lbl_sub, LV_ALIGN_TOP_MID, 0, 50);
    lv_label_set_text(s_lbl_sub, "配对模式");

    s_panel = ui_pixel_panel_create(s_scr, 20, 80, 200, 160, UI_WHITE);

    s_lbl_sos = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_sos, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_lbl_sos, lv_color_hex(UI_TEAL), 0);
    lv_obj_set_width(s_lbl_sos, 180);
    lv_obj_set_style_text_align(s_lbl_sos, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_lbl_sos);
    lv_label_set_text(s_lbl_sos, "请扫描蓝牙\n设置主人信息");

    if (!s_ui_tmr) {
        s_ui_tmr = lv_timer_create(time_timer_cb, 1000, NULL);
    }
    lv_screen_load(s_scr);
    bsp_lvgl_unlock();
}

void dogtag_ui_enter_silent(void) {
    if (!bsp_lvgl_lock(500)) return;
    destroy_screen();
    s_scr = ui_pixel_screen_create("");

    lv_obj_set_style_bg_color(s_scr, lv_color_hex(UI_TEAL), 0);

    s_lbl_time = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_lbl_time, &font_cn_20, 0);
    lv_obj_set_style_text_color(s_lbl_time, lv_color_hex(UI_WHITE), 0);
    lv_obj_align(s_lbl_time, LV_ALIGN_TOP_MID, 0, 20);
    update_time_label();

    s_lbl_sub = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_lbl_sub, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_lbl_sub, lv_color_hex(UI_WHITE), 0);
    lv_obj_set_style_text_opa(s_lbl_sub, LV_OPA_70, 0);
    lv_obj_align(s_lbl_sub, LV_ALIGN_TOP_MID, 0, 50);
    lv_label_set_text(s_lbl_sub, "已连接");

    s_panel = ui_pixel_panel_create(s_scr, 20, 70, 200, 180, UI_WHITE);

    s_lbl_rssi = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_rssi, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_lbl_rssi, lv_color_hex(UI_RED), 0);
    lv_obj_align(s_lbl_rssi, LV_ALIGN_TOP_MID, 0, 8);
    lv_label_set_text(s_lbl_rssi, "0");

    s_lbl_name = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_name, &font_cn_20, 0);
    lv_obj_set_style_text_color(s_lbl_name, lv_color_hex(UI_INK), 0);
    lv_obj_set_width(s_lbl_name, 180);
    lv_obj_set_style_text_align(s_lbl_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_lbl_name, LV_ALIGN_TOP_MID, 0, 40);
    lv_label_set_text_fmt(s_lbl_name, "%s", dogtag_state_get_owner_name());

    s_lbl_phone = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_phone, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_lbl_phone, lv_color_hex(UI_TEAL), 0);
    lv_obj_set_width(s_lbl_phone, 180);
    lv_obj_set_style_text_align(s_lbl_phone, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_lbl_phone, LV_ALIGN_TOP_MID, 0, 80);
    lv_label_set_text_fmt(s_lbl_phone, "%s", dogtag_state_get_owner_phone());

    if (!s_ui_tmr) {
        s_ui_tmr = lv_timer_create(time_timer_cb, 1000, NULL);
    }
    lv_screen_load(s_scr);
    bsp_lvgl_unlock();
}

void dogtag_ui_enter_lost(void) {
    if (!bsp_lvgl_lock(500)) return;
    destroy_screen();
    s_scr = ui_pixel_screen_create("");

    lv_obj_set_style_bg_color(s_scr, lv_color_hex(UI_RED), 0);

    s_lbl_time = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_lbl_time, &font_cn_20, 0);
    lv_obj_set_style_text_color(s_lbl_time, lv_color_hex(UI_WHITE), 0);
    lv_obj_align(s_lbl_time, LV_ALIGN_TOP_MID, 0, 20);
    update_time_label();

    s_lbl_cd = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_lbl_cd, &font_cn_20, 0);
    lv_obj_set_style_text_color(s_lbl_cd, lv_color_hex(UI_WHITE), 0);
    lv_obj_align(s_lbl_cd, LV_ALIGN_TOP_MID, 0, 50);
    lv_label_set_text(s_lbl_cd, "0:00");

    s_panel = ui_pixel_panel_create(s_scr, 20, 80, 200, 160, UI_WHITE);

    s_lbl_soc = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_soc, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_lbl_soc, lv_color_hex(UI_INK), 0);
    lv_obj_align(s_lbl_soc, LV_ALIGN_TOP_MID, 0, 8);

    s_lbl_name = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_name, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_lbl_name, lv_color_hex(UI_INK), 0);
    lv_obj_set_width(s_lbl_name, 180);
    lv_obj_set_style_text_align(s_lbl_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_lbl_name, LV_ALIGN_TOP_MID, 0, 40);
    lv_label_set_text_fmt(s_lbl_name, "%s", dogtag_state_get_owner_name());

    s_lbl_phone = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_phone, &font_cn_20, 0);
    lv_obj_set_style_text_color(s_lbl_phone, lv_color_hex(UI_RED), 0);
    lv_obj_set_width(s_lbl_phone, 180);
    lv_obj_set_style_text_align(s_lbl_phone, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_lbl_phone, LV_ALIGN_TOP_MID, 0, 70);
    lv_label_set_text_fmt(s_lbl_phone, "%s", dogtag_state_get_owner_phone());

    s_lbl_sos = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_sos, &font_cn_20, 0);
    lv_obj_set_style_text_color(s_lbl_sos, lv_color_hex(UI_RED), 0);
    lv_obj_align(s_lbl_sos, LV_ALIGN_TOP_MID, 0, 110);
    lv_label_set_text(s_lbl_sos, "SOS");

    if (!s_ui_tmr) {
        s_ui_tmr = lv_timer_create(time_timer_cb, 1000, NULL);
    }
    lv_screen_load(s_scr);
    bsp_lvgl_unlock();
}

void dogtag_ui_update_flash(bool flash_on) {
    if (!s_panel) return;
    if (!bsp_lvgl_lock(100)) return;
    lv_color_t bg = flash_on ? lv_color_hex(0xFF4444) : lv_color_hex(UI_WHITE);
    lv_obj_set_style_bg_color(s_panel, bg, 0);
    bsp_lvgl_unlock();
}

void dogtag_ui_update_battery(int soc) {
    if (!s_lbl_soc) return;
    if (!bsp_lvgl_lock(100)) return;
    lv_label_set_text_fmt(s_lbl_soc, "%d%%", soc);
    bsp_lvgl_unlock();
}

void dogtag_ui_update_rssi(int8_t rssi) {
    if (!s_lbl_rssi) return;
    if (!bsp_lvgl_lock(100)) return;
    if (rssi <= -127) {
        lv_label_set_text(s_lbl_rssi, "0");
        lv_obj_set_style_text_color(s_lbl_rssi, lv_color_hex(UI_RED), 0);
    } else {
        lv_label_set_text_fmt(s_lbl_rssi, "%d", rssi);
        lv_obj_set_style_text_color(s_lbl_rssi, lv_color_hex(UI_GREEN), 0);
    }
    bsp_lvgl_unlock();
}

void dogtag_ui_update_countdown(int seconds) {
    if (!s_lbl_cd) return;
    if (!bsp_lvgl_lock(100)) return;
    lv_label_set_text_fmt(s_lbl_cd, "%d:%02d", seconds / 60, seconds % 60);
    bsp_lvgl_unlock();
}

void dogtag_ui_update_time(const char *time_str) {
    if (!s_lbl_time) return;
    if (!bsp_lvgl_lock(100)) return;
    lv_label_set_text(s_lbl_time, time_str);
    bsp_lvgl_unlock();
}

bool dogtag_ui_lock(void) {
    return bsp_lvgl_lock(500);
}

void dogtag_ui_unlock(void) {
    bsp_lvgl_unlock();
}
