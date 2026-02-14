#pragma once
#include <Arduino.h>
#include "debug.h"

// ============================================================================
// LED Pin Definitions
// ============================================================================
#define LED1_PIN 32
#define LED2_PIN 33
#define LED3_PIN 26
#define LED4_PIN 14
#define LED5_PIN 4
#define LED6_PIN 23
#define LED7_PIN 19
#define LED8_PIN 17

// ============================================================================
// Debug Configuration
// ============================================================================
// Set DEBUG flag here or via platformio.ini build flags:
//   build_flags = -DDEBUG=1
//
// Debug features are completely compiled away when DEBUG=0 (zero overhead)

namespace globals {
  // Global constants and utilities can go here
}
