#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "board.h"
#include "wifi_config.h"
#include "ota.h"
#include "discovery.h"

static const char *TAG = "tapper";

// =============================================================================
// Pin Definitions — ESP32-WROOM-32 8-Button Control Panel
// =============================================================================

// CAN bus pins
#define CAN_TX_PIN   15
#define CAN_RX_PIN   13

// 8 button input pins (active low, internal pull-up)
static const gpio_num_t BUTTON_PINS[8] = {
    GPIO_NUM_34,  // BTN1
    GPIO_NUM_25,  // BTN2
    GPIO_NUM_27,  // BTN3
    GPIO_NUM_12,  // BTN4
    GPIO_NUM_16,  // BTN5
    GPIO_NUM_22,  // BTN6
    GPIO_NUM_21,  // BTN7
    GPIO_NUM_18,  // BTN8
};

// 8 LED backlight output pins
static const gpio_num_t LED_PINS[8] = {
    GPIO_NUM_32,  // LED1
    GPIO_NUM_33,  // LED2
    GPIO_NUM_26,  // LED3
    GPIO_NUM_14,  // LED4
    GPIO_NUM_4,   // LED5
    GPIO_NUM_23,  // LED6
    GPIO_NUM_19,  // LED7
    GPIO_NUM_17,  // LED8
};

#define NUM_BUTTONS  8

// =============================================================================
// CAN Bus Configuration
// =============================================================================

#define CAN_BAUDRATE           500000
#define TX_PROBE_INTERVAL_MS   2000

// CAN protocol IDs
#define CAN_ID_OTA              0x00
#define CAN_ID_WIFI_CONFIG      0x01
#define CAN_ID_DISCOVERY_TRIGGER 0x02

// CAN IDs computed from build flags (TARGET_DEVICE + DEVICE_INSTANCE)
#ifndef DEVICE_INSTANCE
#define DEVICE_INSTANCE 0
#endif
#if DEVICE_INSTANCE < 0 || DEVICE_INSTANCE > 2
#error "DEVICE_INSTANCE must be 0, 1, or 2"
#endif

#define CAN_ID_TOGGLE  (CAN_ID_TOGGLE_BASE + DEVICE_INSTANCE)
#define CAN_ID_STATUS  (CAN_ID_STATUS_BASE + DEVICE_INSTANCE)

// =============================================================================
// Button Timing Constants
// =============================================================================

#define DEBOUNCE_DELAY_US      (50 * 1000LL)    // 50ms debounce

// =============================================================================
// Button State
// =============================================================================

typedef struct {
    bool     pressed;
    bool     toggle_sent;
    int64_t  press_start_us;
} button_state_t;

static button_state_t s_buttons[NUM_BUTTONS] = {0};

// =============================================================================
// GPIO Initialization
// =============================================================================

static void gpio_init_all(void)
{
    // Configure LED pins as outputs, initially off
    for (int i = 0; i < NUM_BUTTONS; i++) {
        gpio_config_t led_cfg = {
            .pin_bit_mask = (1ULL << LED_PINS[i]),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&led_cfg);
        gpio_set_level(LED_PINS[i], 0);
    }

    // Configure button pins as inputs with pull-up
    // Note: GPIOs 34-39 are input-only and have no internal pull-up;
    // they require an external pull-up resistor on the PCB.
    for (int i = 0; i < NUM_BUTTONS; i++) {
        bool has_internal_pullup = (BUTTON_PINS[i] < GPIO_NUM_34);
        gpio_config_t btn_cfg = {
            .pin_bit_mask = (1ULL << BUTTON_PINS[i]),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = has_internal_pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&btn_cfg);
    }

    ESP_LOGI(TAG, "GPIO initialized: 8 buttons (INPUT_PULLUP), 8 LEDs (OUTPUT)");
}

// =============================================================================
// CAN TX Helpers
// =============================================================================

static void send_toggle(int button_index)
{
    twai_message_t msg = {
        .identifier = CAN_ID_TOGGLE,
        .data_length_code = 1,
        .data = { button_index },
    };
    twai_transmit(&msg, 0);
    ESP_LOGI(TAG, "Button %d toggle sent (ID 0x%02X)", button_index + 1,
             CAN_ID_TOGGLE);
}

// =============================================================================
// LED State Handler (RX from target device status message)
// =============================================================================

static void handle_led_state(const uint8_t *data, uint8_t len)
{
#ifdef TARGET_DEVICE_TORRENT
    // Torrent status: 8 bytes, one per channel (PWM value 0-255)
    if (len < 8) return;
    for (int i = 0; i < NUM_BUTTONS; i++) {
        gpio_set_level(LED_PINS[i], data[i] > 0 ? 1 : 0);
    }
#elif defined(TARGET_DEVICE_SWITCHBACK)
    // Switchback status: 1 byte bitmask, one bit per relay
    if (len < 1) return;
    uint8_t bitmask = data[0];
    for (int i = 0; i < NUM_BUTTONS; i++) {
        gpio_set_level(LED_PINS[i], (bitmask >> i) & 1);
    }
#endif
}

// =============================================================================
// Button Scanning — runs in main task at ~1ms resolution
// =============================================================================

static void button_scan(void)
{
    int64_t now_us = esp_timer_get_time();

    for (int i = 0; i < NUM_BUTTONS; i++) {
        bool pin_low = (gpio_get_level(BUTTON_PINS[i]) == 0);
        button_state_t *btn = &s_buttons[i];

        if (pin_low) {
            if (!btn->pressed) {
                btn->pressed = true;
                btn->press_start_us = now_us;
                btn->toggle_sent = false;
            } else if (!btn->toggle_sent &&
                       (now_us - btn->press_start_us) >= DEBOUNCE_DELAY_US) {
                btn->toggle_sent = true;
                send_toggle(i);
            }
        } else {
            if (btn->pressed) {
                btn->pressed = false;
                btn->toggle_sent = false;
            }
        }
    }
}

// =============================================================================
// TWAI (CAN) task — runs independently
// =============================================================================

static void twai_task(void *arg)
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TWAI driver");
        vTaskDelete(NULL);
        return;
    }
    if (twai_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TWAI driver");
        vTaskDelete(NULL);
        return;
    }

    // Broadcast firmware version on CAN 0x04 at startup
    {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        const esp_app_desc_t *app = esp_app_get_description();
        unsigned maj = 0, min = 0, pat = 0;
        sscanf(app->version, "%u.%u.%u", &maj, &min, &pat);
        twai_message_t ver_msg = {
            .identifier = 0x04,
            .data_length_code = 6,
            .data = { mac[3], mac[4], mac[5], maj, min, pat }
        };
        twai_transmit(&ver_msg, pdMS_TO_TICKS(50));
        ESP_LOGI(TAG, "Version broadcast: %s (CAN 0x04)", app->version);
    }

    uint32_t alerts = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS |
                      TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL |
                      TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED |
                      TWAI_ALERT_ERR_ACTIVE | TWAI_ALERT_TX_FAILED |
                      TWAI_ALERT_TX_SUCCESS;
    twai_reconfigure_alerts(alerts, NULL);
    ESP_LOGI(TAG, "TWAI driver started (NORMAL mode, 500 kbps)");

    typedef enum { TX_ACTIVE, TX_PROBING } tx_state_t;
    bool bus_off = false;
    tx_state_t tx_state = TX_ACTIVE;
    int tx_fail_count = 0;
    const int TX_FAIL_THRESHOLD = 3;

    while (1) {
        uint32_t triggered;
        twai_read_alerts(&triggered, pdMS_TO_TICKS(10));

        // --- Bus error handling ---
        if (triggered & TWAI_ALERT_BUS_OFF) {
            ESP_LOGE(TAG, "TWAI bus-off, initiating recovery");
            bus_off = true;
            twai_initiate_recovery();
            continue;
        }
        if (triggered & TWAI_ALERT_BUS_RECOVERED) {
            ESP_LOGI(TAG, "TWAI bus recovered, restarting");
            twai_start();
            bus_off = false;
            tx_fail_count = 0;
            tx_state = TX_PROBING;
        }
        if (triggered & TWAI_ALERT_ERR_PASS) {
            ESP_LOGW(TAG, "TWAI error passive (no peers ACKing?)");
        }
        if (triggered & TWAI_ALERT_TX_FAILED) {
            if (tx_state == TX_ACTIVE) {
                tx_fail_count++;
                if (tx_fail_count >= TX_FAIL_THRESHOLD) {
                    tx_state = TX_PROBING;
                    ESP_LOGW(TAG, "TWAI no peers detected, entering slow probe");
                }
            }
        }
        if (triggered & TWAI_ALERT_TX_SUCCESS) {
            if (tx_state == TX_PROBING) {
                tx_state = TX_ACTIVE;
                tx_fail_count = 0;
                ESP_LOGI(TAG, "TWAI probe ACK'd, peer detected, resuming normal TX");
            }
            tx_fail_count = 0;
        }

        // --- Drain received messages ---
        if (triggered & TWAI_ALERT_RX_DATA) {
            if (tx_state == TX_PROBING) {
                tx_state = TX_ACTIVE;
                tx_fail_count = 0;
                ESP_LOGI(TAG, "TWAI peer detected via RX, resuming normal TX");
            }
            twai_message_t msg;
            while (twai_receive(&msg, 0) == ESP_OK) {
                if (msg.rtr) continue;

                if (msg.identifier == CAN_ID_OTA) {
                    ota_handle_trigger(msg.data, msg.data_length_code);
                } else if (msg.identifier == CAN_ID_WIFI_CONFIG) {
                    wifi_config_handle_can(msg.data, msg.data_length_code);
                } else if (msg.identifier == CAN_ID_DISCOVERY_TRIGGER) {
                    discovery_handle_trigger();
                } else if (msg.identifier == CAN_ID_STATUS) {
                    handle_led_state(msg.data, msg.data_length_code);
                }
            }
        }

        // Tapper is input-only — no periodic status TX.
        // Button presses are sent from the main task via send_toggle.
        (void)bus_off;
    }
}

// =============================================================================
// Device Configuration Accessors (compile-time from build flags)
// =============================================================================

uint8_t tapper_get_device_instance(void) { return DEVICE_INSTANCE; }
const char *tapper_get_target_device(void) { return TARGET_DEVICE_NAME; }
uint32_t tapper_get_toggle_id(void) { return CAN_ID_TOGGLE; }
uint32_t tapper_get_status_id(void) { return CAN_ID_STATUS; }

// =============================================================================
// Main application
// =============================================================================

void app_main(void)
{
    ESP_LOGI(TAG, "=== TrailCurrent Tapper ===");
    ESP_LOGI(TAG, "8-Button Control Panel with CAN Bus");

    // Initialize NVS and load WiFi credentials
    ESP_ERROR_CHECK(wifi_config_init());

    char ssid[33] = {0};
    char password[64] = {0};
    if (wifi_config_load(ssid, sizeof(ssid), password, sizeof(password))) {
        ESP_LOGI(TAG, "WiFi credentials loaded from NVS");
    } else {
        ESP_LOGI(TAG, "No WiFi credentials — OTA disabled until provisioned via CAN");
    }

    // Initialize discovery and OTA (must be after wifi_config_init)
    discovery_init();
    ota_init();

    ESP_LOGI(TAG, "Hostname: %s", wifi_config_get_hostname());
    ESP_LOGI(TAG, "Target: %s instance %d (toggle=0x%02X, status=0x%02X)",
             TARGET_DEVICE_NAME, DEVICE_INSTANCE, CAN_ID_TOGGLE, CAN_ID_STATUS);

    // Initialize GPIO for buttons and LEDs
    gpio_init_all();

    // CAN runs in its own task so bus errors never block button scanning
    xTaskCreatePinnedToCore(twai_task, "twai", 4096, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "Setup complete");

    // Main task scans buttons at ~10ms resolution
    while (1) {
        button_scan();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
