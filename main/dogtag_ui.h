#pragma once

#include "lvgl.h"
#include "dogtag_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void dogtag_ui_init(void);
void dogtag_ui_deinit(void);

void dogtag_ui_enter_pairing(void);
void dogtag_ui_enter_silent(void);
void dogtag_ui_enter_lost(void);
void dogtag_ui_update_flash(bool flash_on);
void dogtag_ui_update_battery(int soc);
void dogtag_ui_update_rssi(int8_t rssi);
void dogtag_ui_update_countdown(int seconds);

bool dogtag_ui_lock(void);
void dogtag_ui_unlock(void);

lv_obj_t *dogtag_ui_get_mascot(void);

#ifdef __cplusplus
}
#endif