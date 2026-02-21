#include <Arduino.h>
#include <stdint.h>
#include <Preferences.h>
#include <TwaiTaskBased.h>
#include <OtaUpdate.h>
#include "globals.h"

// Button debounce timing
unsigned long btn01DebounceTime = 0;
unsigned long btn02DebounceTime = 0;
unsigned long btn03DebounceTime = 0;
unsigned long btn04DebounceTime = 0;
unsigned long btn05DebounceTime = 0;
unsigned long btn06DebounceTime = 0;
unsigned long btn07DebounceTime = 0;
unsigned long btn08DebounceTime = 0;
unsigned long debounceDelay = 200;

// Button state tracking for press-and-hold brightness control
unsigned long btn01PressStartTime = 0;
unsigned long btn02PressStartTime = 0;
unsigned long btn03PressStartTime = 0;
unsigned long btn04PressStartTime = 0;
unsigned long btn05PressStartTime = 0;
unsigned long btn06PressStartTime = 0;
unsigned long btn07PressStartTime = 0;
unsigned long btn08PressStartTime = 0;

bool btn01WasPressed = false;
bool btn02WasPressed = false;
bool btn03WasPressed = false;
bool btn04WasPressed = false;
bool btn05WasPressed = false;
bool btn06WasPressed = false;
bool btn07WasPressed = false;
bool btn08WasPressed = false;

bool btn01ToggleSent = false;
bool btn02ToggleSent = false;
bool btn03ToggleSent = false;
bool btn04ToggleSent = false;
bool btn05ToggleSent = false;
bool btn06ToggleSent = false;
bool btn07ToggleSent = false;
bool btn08ToggleSent = false;

bool btn01InBrightnessMode = false;
bool btn02InBrightnessMode = false;
bool btn03InBrightnessMode = false;
bool btn04InBrightnessMode = false;
bool btn05InBrightnessMode = false;
bool btn06InBrightnessMode = false;
bool btn07InBrightnessMode = false;
bool btn08InBrightnessMode = false;

uint8_t btn01Brightness = 0;
uint8_t btn02Brightness = 0;
uint8_t btn03Brightness = 0;
uint8_t btn04Brightness = 0;
uint8_t btn05Brightness = 0;
uint8_t btn06Brightness = 0;
uint8_t btn07Brightness = 0;
uint8_t btn08Brightness = 0;

unsigned long btn01LastBrightnessUpdate = 0;
unsigned long btn02LastBrightnessUpdate = 0;
unsigned long btn03LastBrightnessUpdate = 0;
unsigned long btn04LastBrightnessUpdate = 0;
unsigned long btn05LastBrightnessUpdate = 0;
unsigned long btn06LastBrightnessUpdate = 0;
unsigned long btn07LastBrightnessUpdate = 0;
unsigned long btn08LastBrightnessUpdate = 0;

// Timing constants for brightness control
const unsigned long HOLD_THRESHOLD = 700;        // 700ms to enter brightness mode
const unsigned long BRIGHTNESS_INCREMENT = 100;  // Update brightness every 100ms

// Button GPIO pins
#define BTN1_PIN 34
#define BTN2_PIN 25
#define BTN3_PIN 27
#define BTN4_PIN 12
#define BTN5_PIN 16
#define BTN6_PIN 22
#define BTN7_PIN 21
#define BTN8_PIN 18

// WiFi credential reception state (CAN ID 0x01 protocol)
bool wifiConfigInProgress = false;
uint8_t wifiSsidBuffer[33];       // Max 32 chars + null
uint8_t wifiPasswordBuffer[64];   // Max 63 chars + null
uint8_t wifiSsidLen = 0;          // Expected total SSID length
uint8_t wifiPasswordLen = 0;      // Expected total password length
uint8_t wifiSsidReceived = 0;     // Bytes received so far
uint8_t wifiPasswordReceived = 0;

// Create OTA update handler (3-minute timeout, 180000 ms)
// Credentials are loaded from NVS when OTA is triggered; empty here for getHostName() only
OtaUpdate otaUpdate(180000, "", "");

/**
 * Save WiFi credentials to NVS (Non-Volatile Storage)
 */
void saveWifiCredentials(const char* ssid, const char* password) {
  Preferences prefs;
  prefs.begin("wifi", false);  // read-write
  prefs.putString("ssid", ssid);
  prefs.putString("password", password);
  prefs.end();
  debugf("[WiFi] Credentials saved to NVS (SSID: %s)\n", ssid);
}

/**
 * Handle WiFi credential CAN messages (CAN ID 0x01)
 * Protocol uses data[0] as message type:
 *   0x01: Start - contains SSID/password lengths and chunk counts
 *   0x02: SSID chunk - chunk_index + up to 6 data bytes
 *   0x03: Password chunk - chunk_index + up to 6 data bytes
 *   0x04: End - XOR checksum for validation
 */
void handleWifiConfigMessage(const twai_message_t &msg) {
  uint8_t msgType = msg.data[0];

  switch (msgType) {
    case 0x01: {  // Start message
      wifiSsidLen = msg.data[1];
      wifiPasswordLen = msg.data[2];
      wifiSsidReceived = 0;
      wifiPasswordReceived = 0;
      memset(wifiSsidBuffer, 0, sizeof(wifiSsidBuffer));
      memset(wifiPasswordBuffer, 0, sizeof(wifiPasswordBuffer));
      wifiConfigInProgress = true;
      debugf("[WiFi] Config start: SSID len=%d, Password len=%d\n", wifiSsidLen, wifiPasswordLen);
      break;
    }

    case 0x02: {  // SSID chunk
      if (!wifiConfigInProgress) break;
      uint8_t dataBytes = msg.data_length_code - 2;
      uint8_t remaining = wifiSsidLen - wifiSsidReceived;
      if (dataBytes > remaining) dataBytes = remaining;
      if (wifiSsidReceived + dataBytes <= 32) {
        memcpy(wifiSsidBuffer + wifiSsidReceived, &msg.data[2], dataBytes);
        wifiSsidReceived += dataBytes;
      }
      break;
    }

    case 0x03: {  // Password chunk
      if (!wifiConfigInProgress) break;
      uint8_t dataBytes = msg.data_length_code - 2;
      uint8_t remaining = wifiPasswordLen - wifiPasswordReceived;
      if (dataBytes > remaining) dataBytes = remaining;
      if (wifiPasswordReceived + dataBytes <= 63) {
        memcpy(wifiPasswordBuffer + wifiPasswordReceived, &msg.data[2], dataBytes);
        wifiPasswordReceived += dataBytes;
      }
      break;
    }

    case 0x04: {  // End message with checksum
      if (!wifiConfigInProgress) break;
      wifiConfigInProgress = false;

      // Verify XOR checksum
      uint8_t checksum = 0;
      for (uint8_t i = 0; i < wifiSsidReceived; i++) checksum ^= wifiSsidBuffer[i];
      for (uint8_t i = 0; i < wifiPasswordReceived; i++) checksum ^= wifiPasswordBuffer[i];

      if (checksum == msg.data[1] && wifiSsidReceived == wifiSsidLen && wifiPasswordReceived == wifiPasswordLen) {
        wifiSsidBuffer[wifiSsidReceived] = '\0';
        wifiPasswordBuffer[wifiPasswordReceived] = '\0';
        saveWifiCredentials((const char*)wifiSsidBuffer, (const char*)wifiPasswordBuffer);
      } else {
        debugf("[WiFi] Config failed: checksum %s, SSID %d/%d bytes, Password %d/%d bytes\n",
               (checksum == msg.data[1]) ? "OK" : "MISMATCH",
               wifiSsidReceived, wifiSsidLen, wifiPasswordReceived, wifiPasswordLen);
      }
      break;
    }
  }
}

/**
 * CAN RX Callback - called when a CAN message is received
 * Handles three types of messages:
 *   - ID 0x0: OTA trigger (MAC-based targeting)
 *   - ID 0x01: WiFi credential configuration
 *   - ID 0x1B: LED control commands
 */
void onCanRx(const twai_message_t &msg) {
  // OTA trigger message (ID 0x0)
  if (msg.identifier == 0x0) {
    debugln("[OTA] CAN trigger received");

    // Extract target hostname from CAN data
    char updateForHostName[14];
    String currentHostName = otaUpdate.getHostName();

    // Format: esp32-XXXXXX where X is MAC address in hex
    sprintf(updateForHostName, "esp32-%02X%02X%02X",
            msg.data[0], msg.data[1], msg.data[2]);

    debugf("[OTA] Target hostname: %s\n", updateForHostName);
    debugf("[OTA] Current hostname: %s\n", currentHostName.c_str());

    // Check if this OTA trigger is for this device
    if (currentHostName.equals(updateForHostName)) {
      debugln("[OTA] Hostname matched - reading WiFi credentials from NVS");
      Preferences prefs;
      prefs.begin("wifi", true);  // read-only
      String ssid = prefs.getString("ssid", "");
      String password = prefs.getString("password", "");
      prefs.end();

      if (ssid.length() > 0 && password.length() > 0) {
        debugf("[OTA] Using stored WiFi credentials (SSID: %s)\n", ssid.c_str());
        OtaUpdate ota(180000, ssid.c_str(), password.c_str());
        ota.waitForOta();
        debugln("[OTA] OTA mode exited - resuming normal operation");
      } else {
        debugln("[OTA] ERROR: No WiFi credentials in NVS - cannot start OTA");
      }
    }
  }

  // WiFi credential configuration message (ID 0x01)
  else if (msg.identifier == 0x01) {
    handleWifiConfigMessage(msg);
  }

  // LED control message (ID 0x1B) - updates LED backlights to show current state
  else if (msg.identifier == 0x1B) {
    // Expected: 8 bytes of LED data (0 = OFF, non-zero = ON)
    if (msg.data_length_code >= 8) {
      debugln("[LED] LED state update received");

      // Update LED backlights based on received state
      digitalWrite(LED1_PIN, msg.data[0] > 0 ? HIGH : LOW);
      digitalWrite(LED2_PIN, msg.data[1] > 0 ? HIGH : LOW);
      digitalWrite(LED3_PIN, msg.data[2] > 0 ? HIGH : LOW);
      digitalWrite(LED4_PIN, msg.data[3] > 0 ? HIGH : LOW);
      digitalWrite(LED5_PIN, msg.data[4] > 0 ? HIGH : LOW);
      digitalWrite(LED6_PIN, msg.data[5] > 0 ? HIGH : LOW);
      digitalWrite(LED7_PIN, msg.data[6] > 0 ? HIGH : LOW);
      digitalWrite(LED8_PIN, msg.data[7] > 0 ? HIGH : LOW);

      debugf("[LED] Backlight states: %d,%d,%d,%d,%d,%d,%d,%d\n",
             msg.data[0], msg.data[1], msg.data[2], msg.data[3],
             msg.data[4], msg.data[5], msg.data[6], msg.data[7]);
    }
  }
}

/**
 * CAN TX Callback - called when a CAN message transmission completes
 * Logs transmission result
 */
void onCanTx(bool success) {
  if (!success) {
    debugln("[CAN] Transmission failed");
  }
}

/**
 * Send a CAN button message
 * Message format: ID=0x18, 1 byte containing button index (0-7)
 * The external controller receives this and toggles the light state,
 * then broadcasts the new state via CAN ID 0x1B for all panels to display
 */
void send_message(int buttonIndex) {
  twai_message_t message;
  message.identifier = 0x18;           // Button press message ID
  message.extd = false;                // Standard CAN format
  message.rtr = false;
  message.data_length_code = 1;
  message.data[0] = buttonIndex;       // Button index (0-7)

  if (TwaiTaskBased::send(message)) {
    debugf("[BTN] Button %d pressed - CAN message sent\n", buttonIndex + 1);
  } else {
    debugf("[BTN] Button %d pressed - CAN TX failed\n", buttonIndex + 1);
  }
}

/**
 * Send a CAN brightness control message
 * Message format: ID=0x015, 2 bytes [device_index, brightness]
 * Button N controls device N brightness on the CAN bus
 */
void send_brightness_message(int deviceIndex, uint8_t brightness) {
  twai_message_t message;
  message.identifier = 0x015;          // Brightness control message ID
  message.extd = false;                // Standard CAN format
  message.rtr = false;
  message.data_length_code = 2;
  message.data[0] = deviceIndex;       // Device index (0-7)
  message.data[1] = brightness;        // Brightness (0-255)

  if (TwaiTaskBased::send(message)) {
    debugf("[BTN] Device %d brightness set to %d\n", deviceIndex + 1, brightness);
  } else {
    debugf("[BTN] Device %d brightness message failed\n", deviceIndex + 1);
  }
}

// Jingle Bells startup light show - plays once on first-ever boot
// Uses all 8 LEDs blinking to the rhythm of the chorus
void playJingleBells() {
  const int ledPins[] = {LED1_PIN, LED2_PIN, LED3_PIN, LED4_PIN,
                         LED5_PIN, LED6_PIN, LED7_PIN, LED8_PIN};

  // Set LEDs from bitmask: bit 0 = LED1, bit 7 = LED8
  auto setLeds = [&](uint8_t mask) {
    for (int i = 0; i < 8; i++) {
      digitalWrite(ledPins[i], (mask & (1 << i)) ? HIGH : LOW);
    }
  };

  // Jingle Bells chorus at ~150 BPM (quarter = 400ms)
  // q=350+50gap, h=700+50gap, e=175+25gap, q.=525+50gap, w=1400+100gap
  struct Note { uint8_t leds; uint16_t on; uint16_t gap; };
  const Note melody[] = {
    // "Jin-gle bells," (E E E-)
    {0b00010001, 350, 50},   // q - LEDs 1,5
    {0b00100010, 350, 50},   // q - LEDs 2,6
    {0b11111111, 700, 50},   // h - ALL

    // "jin-gle bells," (E E E-)
    {0b01000100, 350, 50},   // q - LEDs 3,7
    {0b10001000, 350, 50},   // q - LEDs 4,8
    {0b11111111, 700, 50},   // h - ALL

    // "jin-gle all the way" (E G C D E----)
    {0b10000001, 350, 50},   // q - LEDs 1,8 (outside)
    {0b01000010, 350, 50},   // q - LEDs 2,7
    {0b00100100, 525, 50},   // q. - LEDs 3,6
    {0b00011000, 175, 25},   // e - LEDs 4,5 (center)
    {0b11111111, 1400, 100}, // w - ALL

    // "Oh what fun it" (F F F. F)
    {0b10000001, 350, 50},   // q - LEDs 1,8
    {0b11000011, 350, 50},   // q - LEDs 1,2,7,8
    {0b11100111, 525, 50},   // q. - LEDs 1,2,3,6,7,8
    {0b11111111, 175, 25},   // e - ALL

    // "is to ride in a" (F E E E)
    {0b00011000, 350, 50},   // q - LEDs 4,5 (center)
    {0b00111100, 350, 50},   // q - LEDs 3,4,5,6
    {0b01111110, 175, 25},   // e - LEDs 2,3,4,5,6,7
    {0b11111111, 175, 25},   // e - ALL

    // "one horse o-pen sleigh!" (D D E D G----)
    {0b01000010, 350, 50},   // q - LEDs 2,7
    {0b10000001, 350, 50},   // q - LEDs 1,8
    {0b11000011, 350, 50},   // q - LEDs 1,2,7,8
    {0b01100110, 350, 50},   // q - LEDs 2,3,6,7
    {0b11111111, 1400, 100}, // w - ALL (big finish!)
  };

  debugln("[LED] Playing Jingle Bells startup sequence");
  for (int i = 0; i < (int)(sizeof(melody) / sizeof(melody[0])); i++) {
    setLeds(melody[i].leds);
    delay(melody[i].on);
    setLeds(0);
    delay(melody[i].gap);
  }
  debugln("[LED] Jingle Bells complete");
}

void setup() {
  Serial.begin(115200);
  delay(100);

  debugln("=== TrailCurrent Eight Button Panel ===");
  debugln("CAN Bus Control with OTA Updates");

  // Initialize LED pins (outputs)
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  pinMode(LED4_PIN, OUTPUT);
  pinMode(LED5_PIN, OUTPUT);
  pinMode(LED6_PIN, OUTPUT);
  pinMode(LED7_PIN, OUTPUT);
  pinMode(LED8_PIN, OUTPUT);

  // Turn off all LEDs initially
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);
  digitalWrite(LED4_PIN, LOW);
  digitalWrite(LED5_PIN, LOW);
  digitalWrite(LED6_PIN, LOW);
  digitalWrite(LED7_PIN, LOW);
  digitalWrite(LED8_PIN, LOW);

  debugln("[LED] All LEDs initialized to OFF");

  // Play Jingle Bells light show on first-ever startup only
  Preferences prefs;
  prefs.begin("panel", false);
  bool hasPlayedStartup = prefs.getBool("jbPlayed", false);
  if (!hasPlayedStartup) {
    playJingleBells();
    prefs.putBool("jbPlayed", true);
    debugln("[LED] First startup sequence flag saved to NVS");
  }
  prefs.end();

  // Initialize button pins (inputs with pullup)
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(BTN3_PIN, INPUT_PULLUP);
  pinMode(BTN4_PIN, INPUT_PULLUP);
  pinMode(BTN5_PIN, INPUT_PULLUP);
  pinMode(BTN6_PIN, INPUT_PULLUP);
  pinMode(BTN7_PIN, INPUT_PULLUP);
  pinMode(BTN8_PIN, INPUT_PULLUP);

  debugln("[BTN] All buttons initialized");

  // Register CAN callbacks
  TwaiTaskBased::onReceive(onCanRx);
  TwaiTaskBased::onTransmit(onCanTx);

  // Initialize CAN bus
  // GPIO 15 = TX, GPIO 13 = RX, 500 kbps
  if (!TwaiTaskBased::begin(GPIO_NUM_15, GPIO_NUM_13, 500000)) {
    debugln("[CAN] ERROR: Failed to initialize CAN bus!");
    while (1) {  // Halt on CAN initialization failure
      delay(1000);
    }
  }

  debugln("[CAN] CAN bus initialized successfully");
  debugf("[OTA] Device hostname: %s\n", otaUpdate.getHostName().c_str());
  debugln("[OTA] Ready to receive OTA trigger (CAN ID 0x0)");
  debugln("======================================");
  debugln("Normal operation started");
}

void loop() {
  // HANDLE BTN01 - Short press = toggle, Long hold = brightness
  if (digitalRead(BTN1_PIN) == LOW) {
    // Button is pressed
    if (!btn01WasPressed) {
      // Button just pressed (first detection)
      btn01WasPressed = true;
      btn01PressStartTime = millis();
      btn01ToggleSent = false;
      btn01InBrightnessMode = false;
      debugln("[BTN] Button 1 pressed");
    } else {
      // Button is being held
      unsigned long holdDuration = millis() - btn01PressStartTime;

      // Send toggle message on first press (after debounce), before brightness mode
      if (!btn01ToggleSent && !btn01InBrightnessMode && holdDuration >= debounceDelay && holdDuration < HOLD_THRESHOLD) {
        btn01ToggleSent = true;
        debugln("[BTN] Button 1 short press - sending toggle");
        send_message(0);
      }

      // Enter brightness adjustment mode after 700ms
      if (holdDuration >= HOLD_THRESHOLD && !btn01InBrightnessMode) {
        btn01InBrightnessMode = true;
        btn01Brightness = 0;
        btn01LastBrightnessUpdate = millis();
        btn01ToggleSent = false;  // Reset toggle flag when entering brightness
        debugln("[BTN] Button 1 entering brightness mode");
      }

      if (btn01InBrightnessMode) {
        // Brightness adjustment mode - increment every 100ms
        if ((millis() - btn01LastBrightnessUpdate) >= BRIGHTNESS_INCREMENT) {
          btn01LastBrightnessUpdate = millis();
          // Increment brightness and loop: 0 → 1 → ... → 255 → 0
          if (btn01Brightness >= 255) {
            btn01Brightness = 0;
          } else {
            btn01Brightness++;
          }
          send_brightness_message(0, btn01Brightness);
        }
      }
    }
  } else {
    // Button is not pressed (released or never pressed)
    if (btn01WasPressed) {
      // Button was just released
      if (btn01InBrightnessMode) {
        // Long press released - brightness mode ended
        debugf("[BTN] Button 1 brightness mode ended at %d\n", btn01Brightness);
      }

      // Reset state
      btn01WasPressed = false;
      btn01ToggleSent = false;
      btn01InBrightnessMode = false;
    }
  }

  // HANDLE BTN02 - Short press = toggle, Long hold = brightness
  if (digitalRead(BTN2_PIN) == LOW) {
    if (!btn02WasPressed) {
      btn02WasPressed = true;
      btn02PressStartTime = millis();
      btn02ToggleSent = false;
      btn02InBrightnessMode = false;
      debugln("[BTN] Button 2 pressed");
    } else {
      unsigned long holdDuration = millis() - btn02PressStartTime;

      if (!btn02ToggleSent && !btn02InBrightnessMode && holdDuration >= debounceDelay && holdDuration < HOLD_THRESHOLD) {
        btn02ToggleSent = true;
        debugln("[BTN] Button 2 short press - sending toggle");
        send_message(1);
      }

      if (holdDuration >= HOLD_THRESHOLD && !btn02InBrightnessMode) {
        btn02InBrightnessMode = true;
        btn02Brightness = 0;
        btn02LastBrightnessUpdate = millis();
        btn02ToggleSent = false;
        debugln("[BTN] Button 2 entering brightness mode");
      }

      if (btn02InBrightnessMode) {
        if ((millis() - btn02LastBrightnessUpdate) >= BRIGHTNESS_INCREMENT) {
          btn02LastBrightnessUpdate = millis();
          // Increment brightness and loop: 0 → 1 → ... → 255 → 0
          if (btn02Brightness >= 255) {
            btn02Brightness = 0;
          } else {
            btn02Brightness++;
          }
          send_brightness_message(1, btn02Brightness);
        }
      }
    }
  } else {
    if (btn02WasPressed) {
      if (btn02InBrightnessMode) {
        debugf("[BTN] Button 2 brightness mode ended at %d\n", btn02Brightness);
      }

      btn02WasPressed = false;
      btn02ToggleSent = false;
      btn02InBrightnessMode = false;
    }
  }

  // HANDLE BTN03 - Short press = toggle, Long hold = brightness
  if (digitalRead(BTN3_PIN) == LOW) {
    if (!btn03WasPressed) {
      btn03WasPressed = true;
      btn03PressStartTime = millis();
      btn03ToggleSent = false;
      btn03InBrightnessMode = false;
      debugln("[BTN] Button 3 pressed");
    } else {
      unsigned long holdDuration = millis() - btn03PressStartTime;

      if (!btn03ToggleSent && !btn03InBrightnessMode && holdDuration >= debounceDelay && holdDuration < HOLD_THRESHOLD) {
        btn03ToggleSent = true;
        debugln("[BTN] Button 3 short press - sending toggle");
        send_message(2);
      }

      if (holdDuration >= HOLD_THRESHOLD && !btn03InBrightnessMode) {
        btn03InBrightnessMode = true;
        btn03Brightness = 0;
        btn03LastBrightnessUpdate = millis();
        btn03ToggleSent = false;
        debugln("[BTN] Button 3 entering brightness mode");
      }

      if (btn03InBrightnessMode) {
        if ((millis() - btn03LastBrightnessUpdate) >= BRIGHTNESS_INCREMENT) {
          btn03LastBrightnessUpdate = millis();
          // Increment brightness and loop: 0 → 1 → ... → 255 → 0
          if (btn03Brightness >= 255) {
            btn03Brightness = 0;
          } else {
            btn03Brightness++;
          }
          send_brightness_message(2, btn03Brightness);
        }
      }
    }
  } else {
    if (btn03WasPressed) {
      if (btn03InBrightnessMode) {
        debugf("[BTN] Button 3 brightness mode ended at %d\n", btn03Brightness);
      }

      btn03WasPressed = false;
      btn03ToggleSent = false;
      btn03InBrightnessMode = false;
    }
  }

  // HANDLE BTN04 - Short press = toggle, Long hold = brightness
  if (digitalRead(BTN4_PIN) == LOW) {
    if (!btn04WasPressed) {
      btn04WasPressed = true;
      btn04PressStartTime = millis();
      btn04ToggleSent = false;
      btn04InBrightnessMode = false;
      debugln("[BTN] Button 4 pressed");
    } else {
      unsigned long holdDuration = millis() - btn04PressStartTime;

      if (!btn04ToggleSent && !btn04InBrightnessMode && holdDuration >= debounceDelay && holdDuration < HOLD_THRESHOLD) {
        btn04ToggleSent = true;
        debugln("[BTN] Button 4 short press - sending toggle");
        send_message(3);
      }

      if (holdDuration >= HOLD_THRESHOLD && !btn04InBrightnessMode) {
        btn04InBrightnessMode = true;
        btn04Brightness = 0;
        btn04LastBrightnessUpdate = millis();
        btn04ToggleSent = false;
        debugln("[BTN] Button 4 entering brightness mode");
      }

      if (btn04InBrightnessMode) {
        if ((millis() - btn04LastBrightnessUpdate) >= BRIGHTNESS_INCREMENT) {
          btn04LastBrightnessUpdate = millis();
          // Increment brightness and loop: 0 → 1 → ... → 255 → 0
          if (btn04Brightness >= 255) {
            btn04Brightness = 0;
          } else {
            btn04Brightness++;
          }
          send_brightness_message(3, btn04Brightness);
        }
      }
    }
  } else {
    if (btn04WasPressed) {
      if (btn04InBrightnessMode) {
        debugf("[BTN] Button 4 brightness mode ended at %d\n", btn04Brightness);
      }

      btn04WasPressed = false;
      btn04ToggleSent = false;
      btn04InBrightnessMode = false;
    }
  }

  // HANDLE BTN05 - Short press = toggle, Long hold = brightness
  if (digitalRead(BTN5_PIN) == LOW) {
    if (!btn05WasPressed) {
      btn05WasPressed = true;
      btn05PressStartTime = millis();
      btn05ToggleSent = false;
      btn05InBrightnessMode = false;
      debugln("[BTN] Button 5 pressed");
    } else {
      unsigned long holdDuration = millis() - btn05PressStartTime;

      if (!btn05ToggleSent && !btn05InBrightnessMode && holdDuration >= debounceDelay && holdDuration < HOLD_THRESHOLD) {
        btn05ToggleSent = true;
        debugln("[BTN] Button 5 short press - sending toggle");
        send_message(4);
      }

      if (holdDuration >= HOLD_THRESHOLD && !btn05InBrightnessMode) {
        btn05InBrightnessMode = true;
        btn05Brightness = 0;
        btn05LastBrightnessUpdate = millis();
        btn05ToggleSent = false;
        debugln("[BTN] Button 5 entering brightness mode");
      }

      if (btn05InBrightnessMode) {
        if ((millis() - btn05LastBrightnessUpdate) >= BRIGHTNESS_INCREMENT) {
          btn05LastBrightnessUpdate = millis();
          // Increment brightness and loop: 0 → 1 → ... → 255 → 0
          if (btn05Brightness >= 255) {
            btn05Brightness = 0;
          } else {
            btn05Brightness++;
          }
          send_brightness_message(4, btn05Brightness);
        }
      }
    }
  } else {
    if (btn05WasPressed) {
      if (btn05InBrightnessMode) {
        debugf("[BTN] Button 5 brightness mode ended at %d\n", btn05Brightness);
      }

      btn05WasPressed = false;
      btn05ToggleSent = false;
      btn05InBrightnessMode = false;
    }
  }

  // HANDLE BTN06 - Short press = toggle, Long hold = brightness
  if (digitalRead(BTN6_PIN) == LOW) {
    if (!btn06WasPressed) {
      btn06WasPressed = true;
      btn06PressStartTime = millis();
      btn06ToggleSent = false;
      btn06InBrightnessMode = false;
      debugln("[BTN] Button 6 pressed");
    } else {
      unsigned long holdDuration = millis() - btn06PressStartTime;

      if (!btn06ToggleSent && !btn06InBrightnessMode && holdDuration >= debounceDelay && holdDuration < HOLD_THRESHOLD) {
        btn06ToggleSent = true;
        debugln("[BTN] Button 6 short press - sending toggle");
        send_message(5);
      }

      if (holdDuration >= HOLD_THRESHOLD && !btn06InBrightnessMode) {
        btn06InBrightnessMode = true;
        btn06Brightness = 0;
        btn06LastBrightnessUpdate = millis();
        btn06ToggleSent = false;
        debugln("[BTN] Button 6 entering brightness mode");
      }

      if (btn06InBrightnessMode) {
        if ((millis() - btn06LastBrightnessUpdate) >= BRIGHTNESS_INCREMENT) {
          btn06LastBrightnessUpdate = millis();
          // Increment brightness and loop: 0 → 1 → ... → 255 → 0
          if (btn06Brightness >= 255) {
            btn06Brightness = 0;
          } else {
            btn06Brightness++;
          }
          send_brightness_message(5, btn06Brightness);
        }
      }
    }
  } else {
    if (btn06WasPressed) {
      if (btn06InBrightnessMode) {
        debugf("[BTN] Button 6 brightness mode ended at %d\n", btn06Brightness);
      }

      btn06WasPressed = false;
      btn06ToggleSent = false;
      btn06InBrightnessMode = false;
    }
  }

  // HANDLE BTN07 - Short press = toggle, Long hold = brightness
  if (digitalRead(BTN7_PIN) == LOW) {
    if (!btn07WasPressed) {
      btn07WasPressed = true;
      btn07PressStartTime = millis();
      btn07ToggleSent = false;
      btn07InBrightnessMode = false;
      debugln("[BTN] Button 7 pressed");
    } else {
      unsigned long holdDuration = millis() - btn07PressStartTime;

      if (!btn07ToggleSent && !btn07InBrightnessMode && holdDuration >= debounceDelay && holdDuration < HOLD_THRESHOLD) {
        btn07ToggleSent = true;
        debugln("[BTN] Button 7 short press - sending toggle");
        send_message(6);
      }

      if (holdDuration >= HOLD_THRESHOLD && !btn07InBrightnessMode) {
        btn07InBrightnessMode = true;
        btn07Brightness = 0;
        btn07LastBrightnessUpdate = millis();
        btn07ToggleSent = false;
        debugln("[BTN] Button 7 entering brightness mode");
      }

      if (btn07InBrightnessMode) {
        if ((millis() - btn07LastBrightnessUpdate) >= BRIGHTNESS_INCREMENT) {
          btn07LastBrightnessUpdate = millis();
          // Increment brightness and loop: 0 → 1 → ... → 255 → 0
          if (btn07Brightness >= 255) {
            btn07Brightness = 0;
          } else {
            btn07Brightness++;
          }
          send_brightness_message(6, btn07Brightness);
        }
      }
    }
  } else {
    if (btn07WasPressed) {
      if (btn07InBrightnessMode) {
        debugf("[BTN] Button 7 brightness mode ended at %d\n", btn07Brightness);
      }

      btn07WasPressed = false;
      btn07ToggleSent = false;
      btn07InBrightnessMode = false;
    }
  }

  // HANDLE BTN08 - Short press = toggle, Long hold = brightness
  if (digitalRead(BTN8_PIN) == LOW) {
    if (!btn08WasPressed) {
      btn08WasPressed = true;
      btn08PressStartTime = millis();
      btn08ToggleSent = false;
      btn08InBrightnessMode = false;
      debugln("[BTN] Button 8 pressed");
    } else {
      unsigned long holdDuration = millis() - btn08PressStartTime;

      if (!btn08ToggleSent && !btn08InBrightnessMode && holdDuration >= debounceDelay && holdDuration < HOLD_THRESHOLD) {
        btn08ToggleSent = true;
        debugln("[BTN] Button 8 short press - sending toggle");
        send_message(7);
      }

      if (holdDuration >= HOLD_THRESHOLD && !btn08InBrightnessMode) {
        btn08InBrightnessMode = true;
        btn08Brightness = 0;
        btn08LastBrightnessUpdate = millis();
        btn08ToggleSent = false;
        debugln("[BTN] Button 8 entering brightness mode");
      }

      if (btn08InBrightnessMode) {
        if ((millis() - btn08LastBrightnessUpdate) >= BRIGHTNESS_INCREMENT) {
          btn08LastBrightnessUpdate = millis();
          // Increment brightness and loop: 0 → 1 → ... → 255 → 0
          if (btn08Brightness >= 255) {
            btn08Brightness = 0;
          } else {
            btn08Brightness++;
          }
          send_brightness_message(7, btn08Brightness);
        }
      }
    }
  } else {
    if (btn08WasPressed) {
      if (btn08InBrightnessMode) {
        debugf("[BTN] Button 8 brightness mode ended at %d\n", btn08Brightness);
      }

      btn08WasPressed = false;
      btn08ToggleSent = false;
      btn08InBrightnessMode = false;
    }
  }

  // CAN I/O is handled by FreeRTOS tasks in TwaiTaskBased
  // No polling required - just yield to let other tasks run
  yield();
}
