# Button/LED Behavior Fix - Summary

## Issues Found & Fixed

### Issue 1: BTN1 Missing INPUT_PULLUP (CRITICAL)

**Problem:**
- Button 1 was configured as `INPUT` (no pull-up resistor)
- Buttons 2-8 were configured as `INPUT_PULLUP`
- This inconsistency caused unreliable button reading for BTN1

**Symptoms:**
- Button 1 behaves erratically
- Multiple LEDs may turn on unexpectedly
- Button presses not detected reliably

**Fix Applied:**
```cpp
// BEFORE:
pinMode(BTN1_PIN, INPUT);        // No pull-up!
pinMode(BTN2_PIN, INPUT_PULLUP); // Pull-up enabled

// AFTER:
pinMode(BTN1_PIN, INPUT_PULLUP); // Now consistent with others
pinMode(BTN2_PIN, INPUT_PULLUP);
// ... all buttons now use INPUT_PULLUP
```

**Why This Matters:**
- Without pull-up, BTN1 pin floats between HIGH and LOW unpredictably
- This causes false HIGH readings when button is not pressed
- Led to the "button turns light on but not off" behavior
- Now all buttons have consistent 3.3V pull-up via internal resistor

---

### Issue 2: LED Pin Definition Order (Clarification)

**Problem:**
- LED pin definitions were out of order:
  ```cpp
  #define LED1_PIN 32
  #define LED2_PIN 33
  #define LED3_PIN 26
  #define LED4_PIN 14
  #define LED6_PIN 23   // LED6 defined before LED5!
  #define LED7_PIN 19
  #define LED8_PIN 17
  #define LED5_PIN 4    // LED5 defined last!
  ```

**Impact:**
- Confusing when reading code (LED6 appears before LED5)
- Not functionally broken (code uses LED5_PIN by name, not by position)
- But creates maintenance confusion

**Fix Applied:**
```cpp
#define LED1_PIN 32
#define LED2_PIN 33
#define LED3_PIN 26
#define LED4_PIN 14
#define LED5_PIN 4    // Now in correct sequential order
#define LED6_PIN 23
#define LED7_PIN 19
#define LED8_PIN 17
```

---

## Testing the Fix

### How to Verify Buttons Work Now

1. **Rebuild firmware:**
   - VSCode: Command Palette → "PlatformIO: Build"
   - OR: `pio run`

2. **Flash to device:**
   - VSCode: Command Palette → "PlatformIO: Upload"
   - OR: `pio run --target upload`

3. **Test each button:**
   ```bash
   pio device monitor --speed 115200
   ```
   - Press Button 1: Should see `[BTN] Button 1 pressed - CAN message sent`
   - Press Button 2: Should see `[BTN] Button 2 pressed - CAN message sent`
   - Repeat for all 8 buttons
   - Each button should only appear once per press (no repeats)

4. **Expected Behavior:**
   - Press button → CAN message sent immediately
   - Release button → No more messages
   - Press again after 200ms → Another CAN message
   - Each button sends its unique number (1-8)

---

## Understanding Button Message Format

**CAN Button Message (ID 0x18):**
- ID: 0x18 (button press)
- Data: 1 byte containing button number (1-8)
- Example: Button 1 press sends [0x01]

**Current Debounce Behavior:**
```cpp
if (digitalRead(BTN1_PIN) == LOW) {
    if ((millis() - btn01DebounceTime) > debounceDelay) {
        btn01DebounceTime = millis();
        sendButtonMessage(1);  // Send only once per 200ms
    }
}
```

- While button held: sends message every 200ms (200ms debounce delay)
- When button released: stops sending
- Next press after 200ms: sends new message

This is designed for **continuous repeat detection**, not toggle buttons.

---

## If Buttons Still Don't Work Properly

### Check These Settings:

1. **GPIO Pin Definitions** (globals.h):
   ```cpp
   #define BTN1_PIN 34
   #define BTN2_PIN 25
   #define BTN3_PIN 27
   #define BTN4_PIN 12
   #define BTN5_PIN 16
   #define BTN6_PIN 22
   #define BTN7_PIN 21
   #define BTN8_PIN 18
   ```
   Verify these match your physical wiring!

2. **Button Polarity** - All buttons configured for ACTIVE LOW:
   ```cpp
   if (digitalRead(BTN1_PIN) == LOW) {  // LOW when pressed
   ```
   - Button pressed (connected to GND): GPIO reads LOW
   - Button not pressed (connected to 3.3V via pull-up): GPIO reads HIGH

3. **CAN Bus Status:**
   ```
   [CAN] CAN bus initialized successfully
   ```
   Must appear in serial output. If it says ERROR, CAN is not working.

4. **Debounce Delay:**
   ```cpp
   unsigned long debounceDelay = 200;  // milliseconds
   ```
   If buttons are triggering too frequently, increase this value.

---

## Related Serial Output Indicators

**Good Signs** ✅:
- `[BTN] Button X pressed - CAN message sent` for each press
- One message per physical press (respecting 200ms debounce)
- All 8 buttons send unique numbers (1-8)

**Problem Signs** ❌:
- No button messages appear
- Same button number appearing for different physical buttons
- Button message appearing without pressing (floating pin)
- Message repeating rapidly without holding button

---

## Files Modified

1. **src/main.cpp:144** - Changed BTN1_PIN from INPUT to INPUT_PULLUP
2. **src/globals.h** - Reordered LED pin definitions to sequential order

---

## Next Steps

1. Rebuild and reflash firmware with these fixes
2. Test button behavior using QUICK_TEST_REFERENCE.md (Section 2-3: Button Test)
3. Verify each button sends correct message
4. If issues persist, enable DEBUG output for detailed logging:
   - platformio.ini: `build_flags = -DDEBUG=1`
   - This will show exactly what the GPIO reads and when messages are sent

---

**Date Fixed:** 2026-02-10
**Related Issue:** Button toggle behavior not working correctly
**Status:** Fixes applied, ready for rebuild and testing
