#pragma once

#include "ble_dogtag.h"

#ifdef __cplusplus
extern "C" {
#endif

void dogtag_state_init(void);
void dogtag_state_deinit(void);

dogtag_state_t dogtag_state_get_state(void);
void dogtag_state_set_state(dogtag_state_t state);

const char *dogtag_state_get_owner_name(void);
const char *dogtag_state_get_owner_phone(void);
bool dogtag_state_is_owner_valid(void);
void dogtag_state_set_owner(const char *name, const char *phone);
void dogtag_state_set_owner_name(const char *name);
void dogtag_state_set_owner_phone(const char *phone);
void dogtag_state_set_owner_valid(bool valid);
void dogtag_state_clear_owner(void);
void dogtag_state_save_nvs(void);
void dogtag_state_load_nvs(void);

void dogtag_state_start_debounce_timer(void);
void dogtag_state_stop_all_timers(void);

int dogtag_state_get_countdown(void);

#ifdef __cplusplus
}
#endif