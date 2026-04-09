#!/bin/bash
# Build all Tapper firmware variants (2 target devices x 3 addresses = 6 binaries).
# Produces merged binaries (bootloader + partition table + OTA data + app)
# that can be flashed at offset 0x0:
#   build/tapper_torrent_addr0.bin .. tapper_switchback_addr2.bin
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

        # Create merged binary (flashable at 0x0, includes all partitions)
        esptool.py --chip esp32 merge_bin -o "$OUTPUT_DIR/tapper_${target}_addr${addr}.bin" \
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
ls -lh "$OUTPUT_DIR"/tapper_*_addr*.bin
