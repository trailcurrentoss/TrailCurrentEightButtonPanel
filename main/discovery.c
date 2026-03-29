#include "discovery.h"
#include "wifi_config.h"
#include "ota.h"
#include "board.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "mdns.h"

static const char *TAG = "discovery";

// ---------------------------------------------------------------------------
// Compile-time module identity
// ---------------------------------------------------------------------------

#ifndef MODULE_TYPE
#error "MODULE_TYPE must be defined (e.g., \"tapper\")"
#endif

// ---------------------------------------------------------------------------
// Discovery state
// ---------------------------------------------------------------------------

static volatile bool s_confirmed = false;
static volatile bool s_discovery_running = false;

// ---------------------------------------------------------------------------
// mDNS service advertisement with TXT records
// ---------------------------------------------------------------------------

static void discovery_mdns_start(void)
{
    const char *hostname = wifi_config_get_hostname();

    char instance_str[4];
    snprintf(instance_str, sizeof(instance_str), "%d",
             tapper_get_device_instance());

    const esp_app_desc_t *app = esp_app_get_description();

    mdns_init();
    mdns_hostname_set(hostname);
    mdns_instance_name_set("TrailCurrent Module");

    mdns_txt_item_t txt[] = {
        { "type",     MODULE_TYPE },
        { "target",   (char *)tapper_get_target_device() },
        { "instance", instance_str },
        { "fw",       app->version },
    };

    mdns_service_add("TrailCurrent Discovery", "_trailcurrent", "_tcp",
                     80, txt, sizeof(txt) / sizeof(txt[0]));

    ESP_LOGI(TAG, "mDNS discovery: %s.local type=%s fw=%s",
             hostname, MODULE_TYPE, app->version);
}

// ---------------------------------------------------------------------------
// HTTP confirmation endpoint — Headwaters calls this to acknowledge
// ---------------------------------------------------------------------------

static esp_err_t confirm_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Discovery confirmed by Headwaters");
    httpd_resp_sendstr(req, "confirmed\n");
    s_confirmed = true;
    return ESP_OK;
}

static httpd_handle_t discovery_start_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    httpd_uri_t confirm_uri = {
        .uri     = "/discovery/confirm",
        .method  = HTTP_GET,
        .handler = confirm_handler,
    };
    httpd_register_uri_handler(server, &confirm_uri);

    return server;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void discovery_init(void)
{
    ESP_LOGI(TAG, "Discovery ready — will respond to CAN 0x02 trigger");
}

static void discovery_task_fn(void *arg)
{
    if (!wifi_config_has_credentials()) {
        ESP_LOGE(TAG, "Discovery triggered but no WiFi credentials — cannot respond");
        s_discovery_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "=== Entering discovery mode ===");

    if (!wifi_connect()) {
        ESP_LOGE(TAG, "WiFi connection failed — aborting discovery");
        s_discovery_running = false;
        vTaskDelete(NULL);
        return;
    }
    discovery_mdns_start();
    httpd_handle_t server = discovery_start_server();

    // Wait for confirmation or timeout
    s_confirmed = false;
    int64_t start = esp_timer_get_time();

    while (!s_confirmed) {
        vTaskDelay(pdMS_TO_TICKS(100));
        int64_t elapsed_ms = (esp_timer_get_time() - start) / 1000;
        if (elapsed_ms >= DISCOVERY_TIMEOUT_MS) {
            ESP_LOGW(TAG, "Discovery timeout — no confirmation received");
            break;
        }
    }

    // Cleanup
    if (server) {
        httpd_stop(server);
    }
    mdns_free();
    wifi_disconnect();

    if (s_confirmed) {
        ESP_LOGI(TAG, "=== Discovery complete — module registered ===");
    } else {
        ESP_LOGI(TAG, "=== Discovery timed out — will respond to next trigger ===");
    }

    s_discovery_running = false;
    vTaskDelete(NULL);
}

bool discovery_is_running(void)
{
    return s_discovery_running;
}

void discovery_handle_trigger(void)
{
    if (s_discovery_running) {
        ESP_LOGW(TAG, "Discovery already in progress — ignoring trigger");
        return;
    }
    if (ota_is_running()) {
        ESP_LOGW(TAG, "OTA in progress — ignoring discovery trigger");
        return;
    }
    s_discovery_running = true;
    xTaskCreate(discovery_task_fn, "discovery", 8192, NULL, 3, NULL);
}
