/**
 * @file debug.h
 * @brief Comprehensive debug macro system with compile-time removal
 *
 * This header provides debug macros that:
 * - Support all Serial.print/println variants
 * - Support printf-style formatting with any number of arguments
 * - Compile away completely when DEBUG=0 (zero code/memory overhead)
 * - Work consistently across all TrailCurrent projects
 *
 * Usage:
 *   debug("Hello");                           // Serial.print()
 *   debugln("Hello");                         // Serial.println()
 *   debugf("Value: %d", 42);                  // Serial.printf()
 *   debugf("X=%d, Y=%d, Z=%d", x, y, z);     // Multiple arguments
 *   debug_hex(0xFF);                          // Print as hex
 *   debug_array(data, 8);                     // Print 8-byte array
 */

#ifndef DEBUG_H
#define DEBUG_H

#pragma once
#include <Arduino.h>

// ============================================================================
// DEBUG FLAG - Set to 1 to enable debug output, 0 to disable
// ============================================================================
#ifndef DEBUG
#define DEBUG 1  // Can be overridden via compiler flags or platformio.ini
#endif

// ============================================================================
// CORE DEBUG MACROS
// ============================================================================

#if DEBUG == 1

/**
 * Print single value (no newline)
 * Supports: char, int, float, String, const char*, etc.
 */
#define debug(x) Serial.print(x)

/**
 * Print single value with newline
 */
#define debugln(x) Serial.println(x)

/**
 * Printf-style formatted output with variadic arguments
 * Supports any number of format arguments
 * Example: debugf("X=%d, Y=%d", x, y)
 */
#define debugf(...) Serial.printf(__VA_ARGS__)

/**
 * Printf-style with newline
 */
#define debugfln(fmt, ...) do { Serial.printf(fmt, ##__VA_ARGS__); Serial.println(); } while(0)

/**
 * Print hex value with optional prefix
 * Example: debug_hex(0xFF) outputs "FF"
 */
#define debug_hex(val) Serial.printf("%02X", (uint32_t)(val))

/**
 * Print binary value
 * Example: debug_bin(0b1010) outputs "1010"
 */
#define debug_bin(val) Serial.printf("%b", (uint32_t)(val))

/**
 * Print memory dump of byte array
 * Example: debug_array(buffer, 16) - prints 16 bytes in hex
 */
#define debug_array(data, len) do { \
  for (size_t _i = 0; _i < (len); _i++) { \
    Serial.printf("%02X ", ((uint8_t*)(data))[_i]); \
    if ((_i + 1) % 16 == 0) Serial.println(); \
  } \
  Serial.println(); \
} while(0)

/**
 * Print labeled value for debugging
 * Example: debug_val("count", count) outputs "count=42"
 */
#define debug_val(name, val) Serial.printf("%s=%d\n", name, (int)(val))

/**
 * Print with category prefix
 * Example: debug_tag("[CAN]", "Message received")
 */
#define debug_tag(tag, msg) Serial.printf("%s %s\n", tag, msg)

/**
 * Conditional debug output
 * Example: debug_if(error, "Error occurred: %d", error_code)
 */
#define debug_if(condition, ...) do { \
  if (condition) { Serial.printf(__VA_ARGS__); Serial.println(); } \
} while(0)

/**
 * Assert with debug output
 * Example: debug_assert(value != NULL, "Value is NULL!")
 */
#define debug_assert(condition, msg) do { \
  if (!(condition)) { \
    Serial.printf("[ASSERT] %s\n", msg); \
    while(1);  /* Halt for debugging */ \
  } \
} while(0)

/**
 * Performance profiling - mark start point
 * Usage: unsigned long start = debug_micros(); ... debug_elapsed(start, "Operation");
 */
#define debug_micros() micros()

/**
 * Print elapsed time since start
 */
#define debug_elapsed(start_time, label) do { \
  unsigned long elapsed = micros() - (start_time); \
  Serial.printf("[PERF] %s: %lu µs\n", label, elapsed); \
} while(0)

/**
 * Stack usage estimation
 */
#define debug_stack() do { \
  extern int __bss_end, __data_start; \
  int stack_ptr; \
  Serial.printf("[STACK] ~%d bytes free\n", (int)&stack_ptr - __bss_end); \
} while(0)

#else  // DEBUG == 0 - All debug output compiled away

// Completely remove all debug code with zero overhead
#define debug(x) (void)0
#define debugln(x) (void)0
#define debugf(...) (void)0
#define debugfln(...) (void)0
#define debug_hex(val) (void)0
#define debug_bin(val) (void)0
#define debug_array(data, len) (void)0
#define debug_val(name, val) (void)0
#define debug_tag(tag, msg) (void)0
#define debug_if(condition, ...) (void)0
#define debug_assert(condition, msg) (void)0
#define debug_micros() 0
#define debug_elapsed(start_time, label) (void)0
#define debug_stack() (void)0

#endif  // DEBUG

// ============================================================================
// LEGACY SUPPORT - For backwards compatibility with existing code
// ============================================================================

#if DEBUG == 1
  #define debugg(x, y, z) Serial.printf(x, y, z)
#else
  #define debugg(x, y, z) (void)0
#endif

#endif  // DEBUG_H
