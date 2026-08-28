#pragma once

#include "dogtag_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_PORTAL_SSID_PREFIX "DogTag_"
#define WIFI_PORTAL_PASS        "12345678"
#define WIFI_PORTAL_IP          "192.168.4.1"
#define WIFI_PORTAL_NETMASK     "255.255.255.0"

esp_err_t wifi_portal_init(void);
void wifi_portal_deinit(void);
esp_err_t wifi_portal_start(void);
void wifi_portal_stop(void);
bool wifi_portal_is_running(void);
bool wifi_portal_is_station_connected(void);

typedef void (*wifi_portal_config_cb_t)(const char *name, const char *phone);
void wifi_portal_set_config_callback(wifi_portal_config_cb_t cb);

#ifdef __cplusplus
}
#endif