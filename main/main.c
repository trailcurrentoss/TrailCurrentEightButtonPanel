#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
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

// CAN IDs
#define CAN_ID_TOGGLE       0x18    // Button toggle command (TX)
#define CAN_ID_BRIGHTNESS   0x15    // Brightness control (TX)
#define CAN_ID_LED_STATE    0x1B    // LED state feedback (RX from Torrent)

// =============================================================================
// Button Timing Constants
// =============================================================================

#define DEBOUNCE_DELAY_US      (50 * 1000LL)    // 50ms debounce
#define HOLD_THRESHOLD_US      (500 * 1000LL)   // 500ms to enter brightness mode
#define BRIGHTNESS_INTERVAL_US (30 * 1000LL)    // Update brightness every 30ms (~7.5s full sweep)

// =============================================================================
// Button State
// =============================================================================

typedef struct {
    bool     pressed;
    bool     toggle_sent;
    bool     in_brightness_mode;
    bool     brightness_rising;    // true = ramping up, false = ramping down
    uint8_t  brightness;
    int64_t  press_start_us;
    int64_t  last_brightness_us;
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
    ESP_LOGI(TAG, "Button %d toggle sent", button_index + 1);
}

static void send_brightness(int device_index, uint8_t brightness)
{
    twai_message_t msg = {
        .identifier = CAN_ID_BRIGHTNESS,
        .data_length_code = 2,
        .data = { device_index, brightness },
    };
    twai_transmit(&msg, 0);
}

// =============================================================================
// LED State Handler (RX from Torrent on CAN ID 0x1B)
// =============================================================================

static void handle_led_state(const uint8_t *data, uint8_t len)
{
    if (len < 8) return;

    for (int i = 0; i < NUM_BUTTONS; i++) {
        gpio_set_level(LED_PINS[i], data[i] > 0 ? 1 : 0);
    }

    ESP_LOGD(TAG, "LED state: %d,%d,%d,%d,%d,%d,%d,%d",
             data[0], data[1], data[2], data[3],
             data[4], data[5], data[6], data[7]);
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
                // Button just pressed
                btn->pressed = true;
                btn->press_start_us = now_us;
                btn->toggle_sent = false;
                btn->in_brightness_mode = false;
            } else {
                // Button held
                int64_t hold_us = now_us - btn->press_start_us;

                // Send toggle after debounce, before brightness threshold
                if (!btn->toggle_sent && !btn->in_brightness_mode &&
                    hold_us >= DEBOUNCE_DELAY_US && hold_us < HOLD_THRESHOLD_US) {
                    btn->toggle_sent = true;
                    send_toggle(i);
                }

                // Enter brightness mode after hold threshold
                if (hold_us >= HOLD_THRESHOLD_US && !btn->in_brightness_mode) {
                    btn->in_brightness_mode = true;
                    btn->brightness = 0;
                    btn->brightness_rising = true;
                    btn->last_brightness_us = now_us;
                    btn->toggle_sent = false;
                    ESP_LOGI(TAG, "Button %d entering brightness mode", i + 1);
                }

                // Oscillate brightness while held: ramp up to 255, then back down to 0, repeat
                if (btn->in_brightness_mode &&
                    (now_us - btn->last_brightness_us) >= BRIGHTNESS_INTERVAL_US) {
                    btn->last_brightness_us = now_us;
                    if (btn->brightness_rising) {
                        if (btn->brightness >= 255) {
                            btn->brightness_rising = false;
                            btn->brightness = 254;
                        } else {
                            btn->brightness++;
                        }
                    } else {
                        if (btn->brightness == 0) {
                            btn->brightness_rising = true;
                            btn->brightness = 1;
                        } else {
                            btn->brightness--;
                        }
                    }
                    send_brightness(i, btn->brightness);
                }
            }
        } else {
            if (btn->pressed) {
                // Button released
                if (btn->in_brightness_mode) {
                    ESP_LOGI(TAG, "Button %d brightness locked at %d", i + 1, btn->brightness);
                }
                btn->pressed = false;
                btn->toggle_sent = false;
                btn->in_brightness_mode = false;
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

                if (msg.identifier == CAN_ID_OTA_TRIGGER) {
                    ota_handle_trigger(msg.data, msg.data_length_code);
                } else if (msg.identifier == CAN_ID_WIFI_CONFIG) {
                    ota_handle_wifi_config(msg.data, msg.data_length_code);
                } else if (msg.identifier == CAN_ID_DISCOVERY_TRIGGER) {
                    discovery_handle_trigger();
                } else if (msg.identifier == CAN_ID_LED_STATE) {
                    handle_led_state(msg.data, msg.data_length_code);
                }
            }
        }

        // Tapper is input-only — no periodic status TX.
        // Button presses are sent from the main task via send_toggle/send_brightness.
        (void)bus_off;
    }
}

// =============================================================================
// Main application
// =============================================================================

void app_main(void)
{
    ota_init();
    discovery_init();

    ESP_LOGI(TAG, "=== TrailCurrent Tapper ===");
    ESP_LOGI(TAG, "8-Button Control Panel with CAN Bus");
    ESP_LOGI(TAG, "Hostname: %s", ota_get_hostname());

    // Initialize GPIO for buttons and LEDs
    gpio_init_all();

    ESP_LOGI(TAG, "CAN toggle TX ID: 0x%02X, brightness TX ID: 0x%02X, LED state RX ID: 0x%02X",
             CAN_ID_TOGGLE, CAN_ID_BRIGHTNESS, CAN_ID_LED_STATE);

    // CAN runs in its own task so bus errors never block button scanning
    xTaskCreate(twai_task, "twai", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Setup complete");

    // Main task scans buttons at ~10ms resolution
    while (1) {
        button_scan();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
