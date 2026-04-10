#!/bin/bash
# Build all Tapper firmware variants (2 target devices x 3 addresses = 6 binaries).
# Produces two binaries per variant:
#   build/tapper_{target}_addr{N}.bin         — app-only (for OTA via Headwaters)
#   build/tapper_{target}_addr{N}_merged.bin  — merged  (for web flasher, full flash at 0x0)
#
# The app-only binary contains just the application image. Headwaters OTA
# writes it to a single app partition via esp_ota_write, which validates
# the image as an app. A merged binary would fail that validation because
# it starts with the bootloader, not an app header.
#
# The merged binary combines bootloader + partition table + OTA data + app
# into one file flashable at offset 0x0. The web flasher requires this
# because it writes the entire flash from a single binary.
set -e

TARGETS=("torrent" "switchback")
MAX_ADDR=2
OUTPUT_DIR="build"

for target in "${TARGETS[@]}"; do
    for addr in $(seq 0 $MAX_ADDR); do
        echo "========================================"
        echo "Building Tapper: $target address $addr ..."
        echo "========================================"
        idf.py build -DTARGET_DEVICE=$target -DDEVICE_INSTANCE=$addr

        # Copy app-only binary with variant-specific name (for OTA)
        cp "$OUTPUT_DIR/tapper.bin" "$OUTPUT_DIR/tapper_${target}_addr${addr}.bin"

        # Create merged binary (for web flasher — flashable at 0x0)
        esptool.py --chip esp32 merge_bin -o "$OUTPUT_DIR/tapper_${target}_addr${addr}_merged.bin" \
            --flash_mode dio --flash_size 4MB \
            0x1000 "$OUTPUT_DIR/bootloader/bootloader.bin" \
            0x8000 "$OUTPUT_DIR/partition_table/partition-table.bin" \
            0xe000 "$OUTPUT_DIR/ota_data_initial.bin" \
            0x10000 "$OUTPUT_DIR/tapper.bin"
        echo ""
    done
done

echo "========================================"
echo "Build complete"
echo "========================================"
echo ""
echo "App-only binaries (for OTA):"
ls -lh "$OUTPUT_DIR"/tapper_*_addr[0-9].bin
echo ""
echo "Merged binaries (for web flasher):"
ls -lh "$OUTPUT_DIR"/tapper_*_addr*_merged.bin
echo ""
echo "Attach ALL of the above to the GitHub release."
