#pragma once

#include <stdint.h>

/**
 * Get the target device instance (0, 1, or 2).  Set at build time.
 */
uint8_t tapper_get_device_instance(void);

/**
 * Get the target device type name ("torrent" or "switchback").
 */
const char *tapper_get_target_device(void);

/**
 * Get the CAN toggle TX ID for the configured target.
 */
uint32_t tapper_get_toggle_id(void);

/**
 * Get the CAN status RX ID for the configured target.
 */
uint32_t tapper_get_status_id(void);
