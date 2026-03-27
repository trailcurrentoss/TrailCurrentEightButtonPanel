#pragma once

#include <stdint.h>

// Discovery window duration (3 minutes)
#define DISCOVERY_TIMEOUT_MS 180000

/**
 * Initialize discovery subsystem.
 * Must be called after ota_init().
 */
void discovery_init(void);

/**
 * Handle a CAN discovery trigger (ID 0x02, broadcast).
 * If this module has WiFi credentials, joins WiFi,
 * advertises via mDNS with module metadata, and waits
 * for Headwaters to confirm registration.
 */
void discovery_handle_trigger(void);
