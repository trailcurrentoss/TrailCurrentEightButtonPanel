#include "ota.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"

static const char *TAG = "ota";

// ---------------------------------------------------------------------------
// Device hostname (cached at init)
// ---------------------------------------------------------------------------

static char s_hostname[16];  // "esp32-XXYYZZ"

// ---------------------------------------------------------------------------
// WiFi credentials (loaded from NVS, updatable via CAN)
// ---------------------------------------------------------------------------

#define NVS_NAMESPACE       "wifi_config"
#define NVS_KEY_SSID        "ssid"
#define NVS_KEY_PASSWORD    "password"

static char s_ssid[33];
static char s_password[64];
static bool s_has_credentials = false;

// ---------------------------------------------------------------------------
// WiFi config CAN provisioning state
// ---------------------------------------------------------------------------

static struct {
    bool receiving;
    uint8_t ssid_len;
    uint8_t password_len;
    uint8_t ssid_chunks;
    uint8_t password_chunks;
    uint8_t received_ssid_chunks;
    uint8_t received_password_chunks;
    char ssid_buf[33];
    char password_buf[64];
    int64_t last_message_time;
} s_wifi_cfg;

#define WIFI_CONFIG_TIMEOUT_MS 5000

// ---------------------------------------------------------------------------
// OTA state
// ---------------------------------------------------------------------------

static volatile bool s_ota_in_progress = false;
static volatile bool s_ota_complete = false;

// ---------------------------------------------------------------------------
// NVS helpers
// ---------------------------------------------------------------------------

static bool load_credentials(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    size_t ssid_len = sizeof(s_ssid);
    size_t pass_len = sizeof(s_password);
    esp_err_t e1 = nvs_get_str(handle, NVS_KEY_SSID, s_ssid, &ssid_len);
    esp_err_t e2 = nvs_get_str(handle, NVS_KEY_PASSWORD, s_password, &pass_len);
    nvs_close(handle);

    if (e1 == ESP_OK && e2 == ESP_OK && strlen(s_ssid) > 0) {
        return true;
    }
    return false;
}

static void save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing");
        return;
    }
    nvs_set_str(handle, NVS_KEY_SSID, ssid);
    nvs_set_str(handle, NVS_KEY_PASSWORD, password);
    nvs_commit(handle);
    nvs_close(handle);

    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
    strncpy(s_password, password, sizeof(s_password) - 1);
    s_password[sizeof(s_password) - 1] = '\0';
    s_has_credentials = true;

    ESP_LOGI(TAG, "WiFi credentials saved (SSID: %s)", s_ssid);
}

// ---------------------------------------------------------------------------
// WiFi connect / disconnect
// ---------------------------------------------------------------------------

static esp_netif_t *s_netif = NULL;
static volatile bool s_wifi_connecting = false;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_connecting) {
            ESP_LOGW(TAG, "WiFi disconnected, retrying...");
            esp_wifi_connect();
        }
    }
}

bool wifi_connect(void)
{
    if (!s_has_credentials) {
        ESP_LOGE(TAG, "No WiFi credentials — cannot connect");
        return false;
    }

    ESP_LOGI(TAG, "Connecting to WiFi: %s", s_ssid);

    if (s_netif == NULL) {
        esp_netif_init();
        esp_event_loop_create_default();
        s_netif = esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                   &wifi_event_handler, NULL);
    }

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, s_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, s_password, sizeof(wifi_config.sta.password) - 1);

    s_wifi_connecting = true;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();

    // Wait for AP association AND DHCP IP assignment (up to 15 seconds)
    for (int i = 0; i < 30; i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(s_netif, &ip_info);
        if (ip_info.ip.addr != 0) {
            ESP_LOGI(TAG, "WiFi connected, IP: " IPSTR, IP2STR(&ip_info.ip));
            return true;
        }
    }
    s_wifi_connecting = false;
    ESP_LOGE(TAG, "WiFi connection failed (no IP assigned)");
    return false;
}

void wifi_disconnect(void)
{
    s_wifi_connecting = false;
    esp_wifi_disconnect();
    esp_wifi_stop();
    ESP_LOGI(TAG, "WiFi disconnected");
}

// ---------------------------------------------------------------------------
// mDNS
// ---------------------------------------------------------------------------

static void mdns_start(void)
{
    mdns_init();
    mdns_hostname_set(s_hostname);
    mdns_instance_name_set("TrailCurrent Tapper OTA");
    ESP_LOGI(TAG, "mDNS hostname: %s.local", s_hostname);
}

static void mdns_stop(void)
{
    mdns_free();
}

// ---------------------------------------------------------------------------
// HTTP OTA server — accepts firmware upload at POST /ota
// ---------------------------------------------------------------------------

static esp_ota_handle_t s_ota_handle;
static const esp_partition_t *s_update_partition;

static esp_err_t ota_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA upload started (%d bytes)", req->content_len);
    s_ota_in_progress = true;

    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (s_update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition available");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        s_ota_in_progress = false;
        return ESP_FAIL;
    }

    esp_err_t err = esp_ota_begin(s_update_partition, OTA_WITH_SEQUENTIAL_WRITES, &s_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        s_ota_in_progress = false;
        return ESP_FAIL;
    }

    char buf[1024];
    int received = 0;
    int total = 0;

    while (total < req->content_len) {
        received = httpd_req_recv(req, buf, sizeof(buf));
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "OTA receive error");
            esp_ota_abort(s_ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            s_ota_in_progress = false;
            return ESP_FAIL;
        }

        err = esp_ota_write(s_ota_handle, buf, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(s_ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            s_ota_in_progress = false;
            return ESP_FAIL;
        }

        total += received;
        if ((total % (64 * 1024)) == 0 || total == req->content_len) {
            ESP_LOGI(TAG, "OTA progress: %d / %d bytes", total, req->content_len);
        }
    }

    err = esp_ota_end(s_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        s_ota_in_progress = false;
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(s_update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        s_ota_in_progress = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA upload complete, rebooting...");
    httpd_resp_sendstr(req, "OTA OK, rebooting...\n");

    s_ota_complete = true;
    s_ota_in_progress = false;
    return ESP_OK;
}

static httpd_handle_t start_ota_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    httpd_uri_t ota_uri = {
        .uri       = "/ota",
        .method    = HTTP_POST,
        .handler   = ota_post_handler,
    };
    httpd_register_uri_handler(server, &ota_uri);

    ESP_LOGI(TAG, "OTA HTTP server started on port %d", config.server_port);
    ESP_LOGI(TAG, "Upload firmware: curl -X POST http://%s.local/ota --data-binary @build/tapper.bin",
             s_hostname);

    return server;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ota_init(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Build hostname from MAC
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_hostname, sizeof(s_hostname), "esp32-%02X%02X%02X",
             mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Device hostname: %s", s_hostname);

    // Load WiFi credentials
    s_has_credentials = load_credentials();
    if (s_has_credentials) {
        ESP_LOGI(TAG, "WiFi credentials loaded (SSID: %s)", s_ssid);
    } else {
        ESP_LOGW(TAG, "No WiFi credentials — provision via CAN ID 0x01");
    }

    memset(&s_wifi_cfg, 0, sizeof(s_wifi_cfg));
}

const char *ota_get_hostname(void)
{
    return s_hostname;
}

bool ota_has_credentials(void)
{
    return s_has_credentials;
}

void ota_handle_trigger(const uint8_t *data, uint8_t len)
{
    if (len < 3) return;

    char target[16];
    snprintf(target, sizeof(target), "esp32-%02X%02X%02X",
             data[0], data[1], data[2]);

    ESP_LOGI(TAG, "OTA trigger target: %s, this device: %s", target, s_hostname);

    if (strcmp(target, s_hostname) != 0) {
        return;
    }

    if (!s_has_credentials) {
        ESP_LOGE(TAG, "OTA triggered but no WiFi credentials available");
        return;
    }

    ESP_LOGI(TAG, "=== Entering OTA mode ===");

    if (!wifi_connect()) {
        ESP_LOGE(TAG, "WiFi connection failed — aborting OTA");
        return;
    }
    mdns_start();
    httpd_handle_t server = start_ota_server();

    // Wait for upload or timeout
    int64_t start = esp_timer_get_time();
    s_ota_complete = false;

    while (!s_ota_complete) {
        vTaskDelay(pdMS_TO_TICKS(100));
        int64_t elapsed = (esp_timer_get_time() - start) / 1000;
        if (elapsed >= OTA_TIMEOUT_MS) {
            ESP_LOGW(TAG, "OTA timeout — no upload received");
            break;
        }
    }

    // Cleanup
    if (server) {
        httpd_stop(server);
    }
    mdns_stop();
    wifi_disconnect();

    if (s_ota_complete) {
        ESP_LOGI(TAG, "Restarting with new firmware...");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }

    ESP_LOGI(TAG, "=== OTA mode exited, resuming normal operation ===");
}

// ---------------------------------------------------------------------------
// WiFi config CAN provisioning (chunked protocol)
// ---------------------------------------------------------------------------

void ota_handle_wifi_config(const uint8_t *data, uint8_t len)
{
    if (len < 1) return;

    int64_t now = esp_timer_get_time() / 1000;

    // Check for timeout on in-progress reception
    if (s_wifi_cfg.receiving &&
        (now - s_wifi_cfg.last_message_time > WIFI_CONFIG_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "WiFi config timeout — resetting");
        memset(&s_wifi_cfg, 0, sizeof(s_wifi_cfg));
    }

    switch (data[0]) {
    case 0x01: {
        // Start message: [0x01, ssid_len, password_len, ssid_chunks, password_chunks]
        if (len < 5) return;
        memset(&s_wifi_cfg, 0, sizeof(s_wifi_cfg));
        s_wifi_cfg.receiving = true;
        s_wifi_cfg.ssid_len = data[1];
        s_wifi_cfg.password_len = data[2];
        s_wifi_cfg.ssid_chunks = data[3];
        s_wifi_cfg.password_chunks = data[4];
        s_wifi_cfg.last_message_time = now;
        ESP_LOGI(TAG, "WiFi config start: SSID %d bytes/%d chunks, password %d bytes/%d chunks",
                 s_wifi_cfg.ssid_len, s_wifi_cfg.ssid_chunks,
                 s_wifi_cfg.password_len, s_wifi_cfg.password_chunks);
        break;
    }
    case 0x02: {
        // SSID chunk: [0x02, chunk_index, byte0..byte5]
        if (!s_wifi_cfg.receiving || len < 3) return;
        uint8_t idx = data[1];
        uint8_t offset = idx * 6;
        if (idx >= s_wifi_cfg.ssid_chunks) return;
        for (int i = 0; i < 6 && (offset + i) < s_wifi_cfg.ssid_len; i++) {
            s_wifi_cfg.ssid_buf[offset + i] = data[2 + i];
        }
        s_wifi_cfg.received_ssid_chunks++;
        s_wifi_cfg.last_message_time = now;
        break;
    }
    case 0x03: {
        // Password chunk: [0x03, chunk_index, byte0..byte5]
        if (!s_wifi_cfg.receiving || len < 3) return;
        uint8_t idx = data[1];
        uint8_t offset = idx * 6;
        if (idx >= s_wifi_cfg.password_chunks) return;
        for (int i = 0; i < 6 && (offset + i) < s_wifi_cfg.password_len; i++) {
            s_wifi_cfg.password_buf[offset + i] = data[2 + i];
        }
        s_wifi_cfg.received_password_chunks++;
        s_wifi_cfg.last_message_time = now;
        break;
    }
    case 0x04: {
        // End message: [0x04, checksum]
        if (!s_wifi_cfg.receiving || len < 2) return;
        uint8_t expected = data[1];

        if (s_wifi_cfg.received_ssid_chunks != s_wifi_cfg.ssid_chunks ||
            s_wifi_cfg.received_password_chunks != s_wifi_cfg.password_chunks) {
            ESP_LOGE(TAG, "WiFi config: missing chunks (SSID %d/%d, pass %d/%d)",
                     s_wifi_cfg.received_ssid_chunks, s_wifi_cfg.ssid_chunks,
                     s_wifi_cfg.received_password_chunks, s_wifi_cfg.password_chunks);
            s_wifi_cfg.receiving = false;
            return;
        }

        uint8_t checksum = 0;
        for (int i = 0; i < s_wifi_cfg.ssid_len; i++) {
            checksum ^= s_wifi_cfg.ssid_buf[i];
        }
        for (int i = 0; i < s_wifi_cfg.password_len; i++) {
            checksum ^= s_wifi_cfg.password_buf[i];
        }

        if (checksum != expected) {
            ESP_LOGE(TAG, "WiFi config checksum mismatch (got 0x%02X, expected 0x%02X)",
                     checksum, expected);
            s_wifi_cfg.receiving = false;
            return;
        }

        s_wifi_cfg.ssid_buf[s_wifi_cfg.ssid_len] = '\0';
        s_wifi_cfg.password_buf[s_wifi_cfg.password_len] = '\0';

        save_credentials(s_wifi_cfg.ssid_buf, s_wifi_cfg.password_buf);
        s_wifi_cfg.receiving = false;
        break;
    }
    default:
        ESP_LOGW(TAG, "WiFi config: unknown type 0x%02X", data[0]);
        break;
    }
}
