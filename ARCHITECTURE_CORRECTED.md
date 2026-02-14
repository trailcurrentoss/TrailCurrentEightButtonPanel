# Button/LED Architecture - Corrected

## Understanding the Multi-Panel System

The Eight Button Panel operates in a **coordinated multi-panel environment** where multiple panels share the same CAN bus. Each panel can control lights on other panels, so LED state must be **synchronized via CAN** rather than controlled locally.

---

## Complete Message Flow

### 1️⃣ Button Press Event
```
Physical Button 1 Pressed
        ↓
while button held, every 200ms:
        ↓
send CAN ID 0x18, data[0]=0 (button index 0)
        ↓
Serial output: "[BTN] Button 1 pressed - CAN message sent"
```

### 2️⃣ External Controller Processing
```
Some device on CAN bus receives CAN ID 0x18
        ↓
Decides: "Button 1 pressed, toggle light 1"
        ↓
Updates its light state database
        ↓
Broadcasts new light state via CAN ID 0x1B
```

### 3️⃣ All Panels Update Lights
```
All panels (including this one) receive:
CAN ID 0x1B, data[0-7] = [light1_state, light2_state, ..., light8_state]
        ↓
Each panel updates its LED backlights
        ↓
Serial output: "[LED] Backlight states: 255,0,255,0,255,0,255,0"
```

### 4️⃣ Visual Feedback
```
LED backlights now show the CURRENT state from CAN bus
        ↓
If another panel turned light 1 on, this panel sees it too
        ↓
All panels always agree on the current state
```

---

## CAN Message Protocol

### Button Press Message (ID 0x18)
```
Identifier: 0x18 (24 decimal)
Data Length: 1 byte
Data Format: [button_index]
  - button_index = 0-7 (for buttons 1-8)

Example: Button 3 pressed
  ID: 0x18
  Data: [2]  (index 2 = button 3)
```

### LED State Broadcast (ID 0x1B)
```
Identifier: 0x1B (27 decimal)
Data Length: 8 bytes
Data Format: [led1, led2, led3, led4, led5, led6, led7, led8]
  - led_X = 0x00 (OFF) or 0xFF (ON) or any value > 0

Example: Lights 1, 3, 5, 7 are ON; 2, 4, 6, 8 are OFF
  ID: 0x1B
  Data: [0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00]
```

---

## Code Implementation

### Button Handling (main.cpp loop)
```cpp
// HANDLE BTN01
if (digitalRead(BTN1_PIN) == LOW) {
  if ((millis() - btn01DebounceTime) > debounceDelay) {
    btn01DebounceTime = millis();
    send_message(0);  // Send button 1 (index 0)
  }
}
```
- Checks if button is pressed (LOW)
- Respects 200ms debounce delay
- Sends message every 200ms while button held
- No local LED toggle - just sends button press

### LED Receive (onCanRx callback)
```cpp
else if (msg.identifier == 0x1B) {
  // Expected: 8 bytes of LED data
  if (msg.data_length_code >= 8) {
    // Update LED backlights based on received state
    digitalWrite(LED1_PIN, msg.data[0] > 0 ? HIGH : LOW);
    digitalWrite(LED2_PIN, msg.data[1] > 0 ? HIGH : LOW);
    // ... (all 8 LEDs)
  }
}
```
- Only processes messages with ID 0x1B
- Updates all 8 LED GPIO pins based on received data
- No local state tracking - LEDs mirror CAN bus state

---

## Why This Architecture?

### Problem: Multiple Panels on Same Bus
- Panel A might control light 1
- Panel B might also control light 1
- What's the actual state of light 1?

### Solution: Single Source of Truth
- **The CAN bus is the source of truth**
- One device (or system) decides light states
- All devices receive the same state broadcast
- All devices display the same backlight status
- No conflicts, no confusion

### Benefits
✅ **Synchronized**: All panels always show correct state
✅ **Coordinated**: Multiple panels don't conflict
✅ **Simple**: Buttons just send press events
✅ **Scalable**: Works with any number of panels
✅ **Reliable**: State is explicit, not inferred

---

## Testing the System

### What You Should See

**When Button 1 is Pressed:**
```
[BTN] Button 1 pressed - CAN message sent
```
(Repeats every 200ms while button is held)

**When LED State is Broadcast:**
```
[LED] Backlight states: 255,0,255,0,255,0,255,0
```
(Shows all 8 LED values received from CAN bus)

**Expected Behavior:**
1. Press button 1 → see button message
2. External system (controller) receives it and broadcasts new state
3. This panel receives new LED state and updates backlights
4. All panels update backlights

### If It's Not Working

**Symptoms: Button press but LED doesn't update**
- Issue: Button message being sent but controller not responding
- Check: Is there a device on CAN bus listening to ID 0x18?
- Check: Is that device sending back LED state on ID 0x1B?

**Symptoms: LED updates but wrong lights**
- Issue: CAN message data[X] mapping is wrong
- Check: Verify LED data byte order matches your hardware
- Check: Verify LED GPIO pins are correct

**Symptoms: Multiple lights turning on unexpectedly**
- Issue: Button 1 press was causing multiple buttons to trigger
- Check: GPIO pin definitions (BTN1_PIN, etc.)
- Check: Button INPUT_PULLUP configuration
- Check: Debounce timing

---

## Original vs New Architecture

### Original (Polling-Based)
- Raw TWAI driver in canHelper.h
- CAN polling in main loop (every iteration)
- Synchronous message reception
- Filter: Only accept ID 0x1B messages
- No OTA support

### New (Callback-Based with OTA)
- TwaiTaskBased library (FreeRTOS tasks)
- Asynchronous callbacks for RX/TX
- Tasks handle CAN in background
- Accept-all filter (process all IDs in callbacks)
- OTA support via CAN ID 0x0 trigger
- Variadic debug macros

**The Button/LED logic remains the same**, just using new library:
- Buttons still send ID 0x18
- LEDs still respond to ID 0x1B
- Same message format, same behavior

---

## Multi-Device Example

### Scenario: 2 Panels on Same Bus

```
Panel A (this device):         Panel B (other device):
├─ Button 1                    ├─ Button 1
├─ Button 2                    ├─ Button 2
├─ LED 1-8 backlights          ├─ LED 1-8 backlights
└─ CAN bus                     └─ CAN bus
         │                            │
         └────────────────────────────┘
                (shared CAN)

Scenario: User presses Button 1 on Panel B
1. Panel B sends: CAN ID 0x18, data[0]=0
2. Central controller receives and toggles light 1
3. Controller broadcasts: CAN ID 0x1B, data=[255,0,0,0,0,0,0,0]
4. Panel A receives and updates: LED1=ON, LED2-8=OFF
5. Panel B receives and updates: LED1=ON, LED2-8=OFF
6. Both panels show same state ✅
```

---

## Configuration Reference

### GPIO Pins (globals.h)
```cpp
LED1_PIN = 32,  LED2_PIN = 33,  LED3_PIN = 26,  LED4_PIN = 14
LED5_PIN = 4,   LED6_PIN = 23,  LED7_PIN = 19,  LED8_PIN = 17

BTN1_PIN = 34,  BTN2_PIN = 25,  BTN3_PIN = 27,  BTN4_PIN = 12
BTN5_PIN = 16,  BTN6_PIN = 22,  BTN7_PIN = 21,  BTN8_PIN = 18
```

### CAN Settings (main.cpp)
```cpp
CAN_TX = GPIO 15
CAN_RX = GPIO 13
Baudrate = 500 kbps
Filter = Accept all messages
```

### Debounce Timing (main.cpp)
```cpp
debounceDelay = 200ms  // Minimum time between button messages
```

---

## Summary

**The system works as a coordinated CAN bus network:**
- Buttons send requests (button press events)
- A controller processes requests (toggles lights, makes decisions)
- Controller broadcasts state (all lights current status)
- All panels display state (LED backlights show current state)

**This ensures all panels always agree on the current light state**, regardless of which panel's button was pressed.

**Files Modified:**
- `src/main.cpp` - Simplified button/LED logic to match original architecture
- `src/globals.h` - Cleaned up and organized LED/button pins

**Date Corrected:** 2026-02-10
