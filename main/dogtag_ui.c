#include "dogtag_ui.h"
#include "dogtag_state.h"
#include "ui_pixel.h"
#include "bsp_display.h"

#include "esp_log.h"
#include "lvgl.h"

extern const lv_font_t font_cn_16;
extern const lv_font_t font_cn_20;

static const char *TAG = "dogtag_ui";

static lv_obj_t *s_scr;
static lv_obj_t *s_panel;
static lv_obj_t *s_lbl_title;
static lv_obj_t *s_lbl_soc;
static lv_obj_t *s_lbl_name;
static lv_obj_t *s_lbl_phone;
static lv_obj_t *s_lbl_sos;
static lv_obj_t *s_lbl_cd;
static lv_obj_t *s_mascot;
static lv_timer_t *s_ui_tmr;

static void destroy_screen(void) {
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    s_panel = s_lbl_title = s_lbl_soc = s_lbl_name = NULL;
    s_lbl_phone = s_lbl_sos = s_lbl_cd = s_mascot = NULL;
}

void dogtag_ui_init(void) {
    s_scr = s_panel = s_lbl_title = s_lbl_soc = NULL;
    s_lbl_name = s_lbl_phone = s_lbl_sos = s_lbl_cd = s_mascot = NULL;
    s_ui_tmr = NULL;
}

void dogtag_ui_deinit(void) {
    if (!bsp_lvgl_lock(500)) return;
    destroy_screen();
    if (s_ui_tmr) { lv_timer_delete(s_ui_tmr); s_ui_tmr = NULL; }
    bsp_lvgl_unlock();
}

void dogtag_ui_enter_pairing(void) {
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

void dogtag_ui_enter_silent(void) {
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

void dogtag_ui_enter_lost(void) {
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
    lv_label_set_text_fmt(s_lbl_name, "姓名: %s", dogtag_state_get_owner_name());

    s_lbl_phone = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_phone, &font_cn_20, 0);
    lv_obj_set_style_text_color(s_lbl_phone, lv_color_hex(UI_RED), 0);
    lv_obj_align(s_lbl_phone, LV_ALIGN_TOP_LEFT, 6, 94);
    lv_label_set_text_fmt(s_lbl_phone, "电话: %s", dogtag_state_get_owner_phone());

    s_lbl_sos = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_sos, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_lbl_sos, lv_color_hex(UI_RED), 0);
    lv_obj_align(s_lbl_sos, LV_ALIGN_TOP_MID, 0, 128);
    lv_label_set_text(s_lbl_sos, "SOS");

    s_mascot = ui_pixel_mascot_create(s_scr, 101, 244);
    lv_screen_load(s_scr);
    bsp_lvgl_unlock();
}

void dogtag_ui_update_flash(bool flash_on) {
    if (!s_panel) return;
    if (!bsp_lvgl_lock(100)) return;
    lv_color_t bg = flash_on ? lv_color_hex(0xFF4444) : lv_color_hex(UI_PAPER);
    lv_obj_set_style_bg_color(s_panel, bg, 0);
    bsp_lvgl_unlock();
}

void dogtag_ui_update_battery(int soc) {
    if (!s_lbl_soc) return;
    if (!bsp_lvgl_lock(100)) return;
    lv_label_set_text_fmt(s_lbl_soc, "%d%%", soc);
    bsp_lvgl_unlock();
}

void dogtag_ui_update_countdown(int seconds) {
    if (!s_lbl_cd) return;
    if (!bsp_lvgl_lock(100)) return;
    lv_label_set_text_fmt(s_lbl_cd, "%d:%02d", seconds / 60, seconds % 60);
    bsp_lvgl_unlock();
}

bool dogtag_ui_lock(void) {
    return bsp_lvgl_lock(500);
}

void dogtag_ui_unlock(void) {
    bsp_lvgl_unlock();
}

lv_obj_t *dogtag_ui_get_mascot(void) {
    return s_mascot;
}