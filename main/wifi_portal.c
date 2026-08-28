#include "wifi_portal.h"
#include "dogtag_state.h"
#include "dogtag_ui.h"
#include "dogtag_audio.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/ip_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "wifi_portal";

static httpd_handle_t s_server = NULL;
static bool s_running = false;
static bool s_station_connected = false;
static wifi_portal_config_cb_t s_config_cb = NULL;
static char s_ap_ssid[32];
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *PORTAL_HTML = 
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>DogTag 配置</title>"
"<style>"
"body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:20px;background:#f5f5f5}"
".card{background:#fff;padding:24px;border-radius:12px;box-shadow:0 2px 8px rgba(0,0,0,.1)}"
"h2{color:#333;text-align:center;margin-bottom:24px}"
".field{margin-bottom:16px}"
"label{display:block;margin-bottom:6px;color:#555;font-weight:500}"
"input{width:100%;padding:12px;border:1px solid #ddd;border-radius:8px;box-sizing:border-box;font-size:16px}"
"button{width:100%;padding:14px;background:#e74c3c;color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:600;cursor:pointer}"
"button:active{background:#c0392b}"
".hint{font-size:12px;color:#999;margin-top:4px;text-align:center}"
"</style></head><body>"
"<div class='card'>"
"<h2>电子狗牌配置</h2>"
"<div class='field'><label>主人姓名</label><input id='name' placeholder='请输入姓名'></div>"
"<div class='field'><label>联系电话</label><input id='phone' type='tel' placeholder='请输入电话'></div>"
"<button onclick='save()'>保存并配对</button>"
"<p class='hint'>保存后请断开热点，设备将进入防走失模式</p>"
"</div>"
"<script>"
"function save(){"
"  const n=document.getElementById('name').value.trim();"
"  const p=document.getElementById('phone').value.trim();"
"  if(!n||!p){alert('请填写完整信息');return}"
"  fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:n,phone:p})})"
"    .then(r=>r.json()).then(d=>{if(d.ok){alert('配置成功，请断开热点');location.href='/done'}else{alert('失败:'+d.msg)}})"
"    .catch(e=>alert('网络错误:'+e))"
"}"
"</script></body></html>";

static const char *DONE_HTML = 
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>配置完成</title>"
"<style>"
"body{font-family:sans-serif;max-width:400px;margin:80px auto;padding:20px;text-align:center;background:#f5f5f5}"
".card{background:#fff;padding:32px;border-radius:12px;box-shadow:0 2px 8px rgba(0,0,0,.1)}"
"h2{color:#27ae60;margin-bottom:16px}"
"p{color:#666;line-height:1.6}"
"</style></head><body>"
"<div class='card'><h2>配置成功</h2><p>已保存主人信息<br>请断开热点连接<br>设备将进入防走失模式</p></div>"
"</body></html>";

static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t done_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, DONE_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t config_post_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"msg\":\"JSON解析失败\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    cJSON *name_item = cJSON_GetObjectItem(json, "name");
    cJSON *phone_item = cJSON_GetObjectItem(json, "phone");
    if (!cJSON_IsString(name_item) || !cJSON_IsString(phone_item)) {
        cJSON_Delete(json);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"msg\":\"字段格式错误\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    const char *name = name_item->valuestring;
    const char *phone = phone_item->valuestring;

    if (s_config_cb) {
        s_config_cb(name, phone);
    }

    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t captive_portal_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
static const httpd_uri_t uri_done = { .uri = "/done", .method = HTTP_GET, .handler = done_get_handler };
static const httpd_uri_t uri_config = { .uri = "/config", .method = HTTP_POST, .handler = config_post_handler };
static const httpd_uri_t uri_captive = { .uri = "/*", .method = HTTP_GET, .handler = captive_portal_handler };

static void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&s_server, &config) == ESP_OK) {
        httpd_register_uri_handler(s_server, &uri_root);
        httpd_register_uri_handler(s_server, &uri_done);
        httpd_register_uri_handler(s_server, &uri_config);
        httpd_register_uri_handler(s_server, &uri_captive);
        ESP_LOGI(TAG, "HTTP server started");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }
}

static void stop_webserver(void) {
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            ESP_LOGI(TAG, "Station connected");
            s_station_connected = true;
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            ESP_LOGI(TAG, "Station disconnected");
            s_station_connected = false;
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            if (dogtag_state_get_state() == DOGTAG_STATE_PAIRING && dogtag_state_is_owner_valid()) {
                ESP_LOGI(TAG, "Station disconnected during pairing -> trigger alarm");
                dogtag_state_set_state(DOGTAG_STATE_SILENT);
                dogtag_state_stop_all_timers();
                dogtag_audio_stop();
                dogtag_ui_enter_silent();
            }
        }
    }
}

esp_err_t wifi_portal_init(void) {
    if (s_running) return ESP_OK;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return ret;

    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s%04X", WIFI_PORTAL_SSID_PREFIX, (unsigned)(esp_random() & 0xFFFF));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(s_ap_ssid),
            .channel = 1,
            .password = WIFI_PORTAL_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    memcpy(wifi_config.ap.ssid, s_ap_ssid, strlen(s_ap_ssid));
    if (strlen(WIFI_PORTAL_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF")));

    ESP_LOGI(TAG, "WiFi Portal init done, SSID: %s", s_ap_ssid);
    return ESP_OK;
}

esp_err_t wifi_portal_start(void) {
    if (s_running) return ESP_OK;
    ESP_ERROR_CHECK(esp_wifi_start());
    start_webserver();
    s_running = true;
    ESP_LOGI(TAG, "WiFi Portal started, SSID: %s, IP: " WIFI_PORTAL_IP, s_ap_ssid);
    return ESP_OK;
}

void wifi_portal_stop(void) {
    if (!s_running) return;
    stop_webserver();
    esp_wifi_stop();
    s_running = false;
    s_station_connected = false;
    ESP_LOGI(TAG, "WiFi Portal stopped");
}

void wifi_portal_deinit(void) {
    wifi_portal_stop();
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_wifi_deinit();
    esp_netif_deinit();
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    ESP_LOGI(TAG, "WiFi Portal deinit done");
}

bool wifi_portal_is_running(void) {
    return s_running;
}

bool wifi_portal_is_station_connected(void) {
    return s_station_connected;
}

void wifi_portal_set_config_callback(wifi_portal_config_cb_t cb) {
    s_config_cb = cb;
}