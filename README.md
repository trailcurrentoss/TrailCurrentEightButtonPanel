# TrailCurrent Tapper

![TrailCurrent Tapper](DOCS/images/tapper_assembled.png)

Eight-button control panel that sends device commands and brightness control over a CAN bus interface with OTA firmware update capability. Part of the [TrailCurrent](https://trailcurrent.com) open-source vehicle platform.

## Hardware Overview

- **Microcontroller:** ESP32 (WROOM32)
- **Function:** Physical button panel for CAN bus device control
- **Key Features:**
  - 8 momentary buttons with LED backlights
  - Short press: toggle device on/off
  - Long press (hold 700ms+): brightness adjustment (0-255)
  - CAN bus communication at 500 kbps
  - Over-the-air (OTA) firmware updates via WiFi
  - mDNS-based network discovery
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
git clone git@github.com:trailcurrentoss/TrailCurrentKiCADLibraries.git

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
   kicad EDA/trailcurrent-tapper.kicad_pro
   ```
3. **Verify libraries load** - All symbol and footprint libraries should resolve without errors
4. **View 3D models** - Open PCB and press `Alt+3` to view the 3D visualization

## Firmware

ESP-IDF based firmware in the `main/` directory.

**Prerequisites:**
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/) v4.1 or later

**Build and flash:**
```bash
# Set up ESP-IDF environment
. $IDF_PATH/export.sh

# Build
idf.py build

# Flash via serial
idf.py -p /dev/ttyUSB0 flash monitor

# OTA update (after initial flash, with WiFi credentials provisioned)
curl -X POST http://esp32-XXYYZZ.local/ota --data-binary @build/tapper.bin
```

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
| 0x01 | var | WiFi credential provisioning (chunked protocol) |
| 0x02 | 0 | Discovery trigger (broadcast) |
| 0x1B | 8 | LED backlight state (1 byte per LED, 0=off, non-zero=on) |

### Button Behavior

- **Short press** (< 700ms): Sends toggle command on CAN ID 0x18
- **Long hold** (>= 700ms): Enters brightness mode, incrementing brightness every 100ms and sending on CAN ID 0x15
- **Release after hold**: Locks brightness at current value

### OTA Updates

WiFi credentials are provisioned over CAN (ID 0x01) and stored in NVS. When an OTA trigger (ID 0x00) matches this device's MAC-derived hostname, the module connects to WiFi, advertises via mDNS, and accepts firmware uploads at `POST /ota`.

### Network Discovery

On CAN trigger (ID 0x02), the module joins WiFi and advertises itself via mDNS service `_trailcurrent._tcp` with TXT records for module type, CAN ID, and firmware version.

## Manufacturing

- **PCB Files:** Ready for fabrication via standard PCB services (JLCPCB, OSH Park, etc.)
- **BOM Generation:** Export BOM from KiCAD schematic (Tools > Generate BOM)
- **Enclosure:** FreeCAD design included in `CAD/` directory
- **JLCPCB Assembly:** See [BOM_ASSEMBLY_WORKFLOW.md](https://github.com/trailcurrentoss/TrailCurrentKiCADLibraries/blob/main/BOM_ASSEMBLY_WORKFLOW.md) for detailed assembly workflow

## Project Structure

```
├── CAD/                          # FreeCAD enclosure design
├── EDA/                          # KiCAD hardware design files
│   ├── trailcurrent-tapper.kicad_pro
│   ├── trailcurrent-tapper.kicad_sch
│   └── trailcurrent-tapper.kicad_pcb
├── main/                         # ESP-IDF firmware source
│   ├── main.c                    # Button handling, LED control, CAN communication
│   ├── ota.c / ota.h             # OTA updates and WiFi provisioning
│   ├── discovery.c / discovery.h # mDNS network discovery
│   ├── CMakeLists.txt            # Component build configuration
│   └── idf_component.yml         # Managed component dependencies
├── CMakeLists.txt                # ESP-IDF project root
├── sdkconfig.defaults            # Default SDK configuration
└── partitions.csv                # ESP32 flash partition layout (dual OTA)
```

## License

MIT License - See LICENSE file for details.

## Contributing

Improvements and contributions are welcome! Please submit issues or pull requests.

## Support

For questions about:
- **KiCAD setup:** See [KICAD_ENVIRONMENT_SETUP.md](https://github.com/trailcurrentoss/TrailCurrentKiCADLibraries/blob/main/KICAD_ENVIRONMENT_SETUP.md)
- **Assembly workflow:** See [BOM_ASSEMBLY_WORKFLOW.md](https://github.com/trailcurrentoss/TrailCurrentKiCADLibraries/blob/main/BOM_ASSEMBLY_WORKFLOW.md)
