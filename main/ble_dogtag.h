#pragma once

#include "host/ble_gap.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DOGTAG_NAME_MAX   32
#define DOGTAG_PHONE_MAX  20
#define DOGTAG_ADV_PREFIX "DogTag"

#define CMD_PAIR_COMPLETE  0x01
#define CMD_FIND_ME        0x02

typedef enum {
    DOGTAG_STATE_INIT = 0,
    DOGTAG_STATE_PAIRING,
    DOGTAG_STATE_SILENT,
    DOGTAG_STATE_DEBOUNCE,
    DOGTAG_STATE_MILD_LOST,
    DOGTAG_STATE_SEVERE_LOST,
} dogtag_state_t;

esp_err_t ble_dogtag_init(void);
void ble_dogtag_deinit(void);
void ble_dogtag_start_advertising(void);
void ble_dogtag_stop_advertising(void);
bool ble_dogtag_is_connected(void);
uint16_t ble_dogtag_get_conn_handle(void);
dogtag_state_t ble_dogtag_get_state(void);
void ble_dogtag_set_state(dogtag_state_t state);
const char *ble_dogtag_get_owner_name(void);
const char *ble_dogtag_get_owner_phone(void);
bool ble_dogtag_is_owner_valid(void);
void ble_dogtag_set_owner(const char *name, const char *phone);
void ble_dogtag_clear_owner(void);

#ifdef __cplusplus
}
#endif