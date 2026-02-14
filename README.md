# TrailCurrent Eight Button Panel

Eight-button control panel that sends device commands and brightness control over a CAN bus interface with OTA firmware update capability.

## Hardware Overview

- **Microcontroller:** ESP32 (WROOM32)
- **Function:** Physical button panel for CAN bus device control
- **Key Features:**
  - 8 momentary buttons with LED backlights
  - Short press: toggle device on/off
  - Long press (hold 700ms+): brightness adjustment (0-255)
  - CAN bus communication at 500 kbps
  - Over-the-air (OTA) firmware updates via WiFi
  - LED state feedback from CAN bus
  - FreeCAD enclosure design

## Hardware Requirements

### Components

- **Microcontroller:** ESP32 development board
- **CAN Transceiver:** Vehicle CAN bus interface (TX: GPIO 15, RX: GPIO 13)

### Pin Connections

**Buttons (INPUT_PULLUP):**

| GPIO | Function |
|------|----------|
| 34 | Button 1 |
| 25 | Button 2 |
| 27 | Button 3 |
| 12 | Button 4 |
| 16 | Button 5 |
| 22 | Button 6 |
| 21 | Button 7 |
| 18 | Button 8 |

**LED Backlights (OUTPUT):**

| GPIO | Function |
|------|----------|
| 32 | LED 1 |
| 33 | LED 2 |
| 26 | LED 3 |
| 14 | LED 4 |
| 4 | LED 5 |
| 23 | LED 6 |
| 19 | LED 7 |
| 17 | LED 8 |

### KiCAD Library Dependencies

This project uses the consolidated [TrailCurrentKiCADLibraries](https://github.com/trailcurrentoss/TrailCurrentKiCADLibraries).

**Setup:**

```bash
# Clone the library
git clone https://github.com/trailcurrentoss/TrailCurrentKiCADLibraries.git

# Set environment variables (add to ~/.bashrc or ~/.zshrc)
export TRAILCURRENT_SYMBOL_DIR="/path/to/TrailCurrentKiCADLibraries/symbols"
export TRAILCURRENT_FOOTPRINT_DIR="/path/to/TrailCurrentKiCADLibraries/footprints"
export TRAILCURRENT_3DMODEL_DIR="/path/to/TrailCurrentKiCADLibraries/3d_models"
```

See [KICAD_ENVIRONMENT_SETUP.md](https://github.com/trailcurrentoss/TrailCurrentKiCADLibraries/blob/main/KICAD_ENVIRONMENT_SETUP.md) in the library repository for detailed setup instructions.

## Opening the Project

1. **Set up environment variables** (see Library Dependencies above)
2. **Open KiCAD:**
   ```bash
   kicad EDA/trailer-switch-panel-eight-buttons.kicad_pro
   ```
3. **Verify libraries load** - All symbol and footprint libraries should resolve without errors
4. **View 3D models** - Open PCB and press `Alt+3` to view the 3D visualization

## Firmware

See `src/` directory for PlatformIO-based firmware.

**Setup:**
```bash
# Install PlatformIO (if not already installed)
pip install platformio

# Build firmware
pio run

# Upload to board (serial)
pio run -t upload

# Upload via OTA (after initial flash)
pio run -t upload --upload-port esp32-DEVICE_ID
```

### Firmware Dependencies

This firmware depends on the following public libraries:

- **[OtaUpdateLibraryWROOM32](https://github.com/trailcurrentoss/OtaUpdateLibraryWROOM32)** (v0.0.1) - Over-the-air firmware update functionality
- **[TwaiTaskBasedLibraryWROOM32](https://github.com/trailcurrentoss/TwaiTaskBasedLibraryWROOM32)** (v0.0.1) - CAN bus communication interface

All dependencies are automatically resolved by PlatformIO during the build process.

**WiFi Credentials (for OTA updates):**
- Copy `src/Secrets.h.template` to `src/Secrets.h` and fill in your WiFi credentials
- Never commit `Secrets.h` to version control (it's in `.gitignore`)

### CAN Bus Protocol

**Transmit (Panel to Bus):**

| CAN ID | Bytes | Description |
|--------|-------|-------------|
| 0x18 | 1 | Button toggle (byte 0 = button index 0-7) |
| 0x15 | 2 | Brightness control (byte 0 = device index, byte 1 = brightness 0-255) |

**Receive (Bus to Panel):**

| CAN ID | Bytes | Description |
|--------|-------|-------------|
| 0x00 | 3 | OTA update trigger (MAC-based device targeting) |
| 0x1B | 8 | LED backlight state (1 byte per LED, 0=off, non-zero=on) |

### Button Behavior

- **Short press** (< 700ms): Sends toggle command on CAN ID 0x18
- **Long hold** (>= 700ms): Enters brightness mode, incrementing brightness every 100ms and sending on CAN ID 0x15
- **Release after hold**: Locks brightness at current value

## Manufacturing

- **PCB Files:** Ready for fabrication via standard PCB services (JLCPCB, OSH Park, etc.)
- **BOM Generation:** Export BOM from KiCAD schematic (Tools > Generate BOM)
- **Enclosure:** FreeCAD design included in `CAD/` directory
- **JLCPCB Assembly:** See [BOM_ASSEMBLY_WORKFLOW.md](https://github.com/trailcurrentoss/TrailCurrentKiCADLibraries/blob/main/BOM_ASSEMBLY_WORKFLOW.md) for detailed assembly workflow

## Project Structure

```
├── CAD/                          # FreeCAD enclosure design
├── EDA/                          # KiCAD hardware design files
│   ├── trailer-switch-panel-eight-buttons.kicad_pro
│   ├── trailer-switch-panel-eight-buttons.kicad_sch
│   └── trailer-switch-panel-eight-buttons.kicad_pcb
├── src/                          # Firmware source
│   ├── main.cpp                  # Button handling and CAN communication
│   ├── globals.h                 # LED pin definitions
│   ├── debug.h                   # Comprehensive debug macro system
│   ├── canHelper.h               # CAN bus configuration
│   └── Secrets.h.template        # WiFi credentials template
├── ARCHITECTURE_CORRECTED.md     # Architecture documentation
├── BUTTON_LED_FIX_SUMMARY.md     # Button/LED fix notes
├── platformio.ini                # Build configuration
└── partitions.csv                # ESP32 flash partition layout
```

## License

MIT License - See LICENSE file for details.

## Contributing

Improvements and contributions are welcome! Please submit issues or pull requests.

## Support

For questions about:
- **KiCAD setup:** See [KICAD_ENVIRONMENT_SETUP.md](https://github.com/trailcurrentoss/TrailCurrentKiCADLibraries/blob/main/KICAD_ENVIRONMENT_SETUP.md)
- **Assembly workflow:** See [BOM_ASSEMBLY_WORKFLOW.md](https://github.com/trailcurrentoss/TrailCurrentKiCADLibraries/blob/main/BOM_ASSEMBLY_WORKFLOW.md)
