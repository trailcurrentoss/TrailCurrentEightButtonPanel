#pragma once

#include <stdint.h>
#include <stdbool.h>

// OTA update window duration (3 minutes)
#define OTA_TIMEOUT_MS 180000

// CAN message IDs
#define CAN_ID_OTA_TRIGGER        0x00
#define CAN_ID_WIFI_CONFIG        0x01
#define CAN_ID_DISCOVERY_TRIGGER  0x02

/**
 * Initialize NVS and load WiFi credentials if available.
 * Must be called once at startup.
 */
void ota_init(void);

/**
 * Get the device hostname (esp32-XXYYZZ from last 3 MAC bytes).
 * Valid after ota_init(). Returns pointer to static buffer.
 */
const char *ota_get_hostname(void);

/**
 * Check whether WiFi credentials have been provisioned.
 */
bool ota_has_credentials(void);

/**
 * Connect to WiFi using stored credentials.
 * Blocks up to 15 seconds waiting for connection.
 * Returns true if connected, false on failure.
 * Shared by OTA and discovery.
 */
bool wifi_connect(void);

/**
 * Disconnect from WiFi and stop the radio.
 */
void wifi_disconnect(void);

/**
 * Handle a CAN OTA trigger message (ID 0x00).
 * Compares MAC bytes in data[0..2] against this device's hostname.
 * If matched, enters OTA mode: connects WiFi, starts HTTP server,
 * waits for firmware upload or timeout, then disconnects and returns.
 */
void ota_handle_trigger(const uint8_t *data, uint8_t len);

/**
 * Handle a CAN WiFi config message (ID 0x01).
 * Implements the chunked credential provisioning protocol.
 */
void ota_handle_wifi_config(const uint8_t *data, uint8_t len);
