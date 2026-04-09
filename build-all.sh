#!/bin/bash
# Build all Tapper firmware variants (2 target devices x 3 addresses = 6 binaries).
# Produces: build/tapper_torrent_addr0.bin .. tapper_switchback_addr2.bin
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
        cp "$OUTPUT_DIR/tapper.bin" "$OUTPUT_DIR/tapper_${target}_addr${addr}.bin"
        echo ""
    done
done

echo "========================================"
echo "Build complete"
echo "========================================"
ls -lh "$OUTPUT_DIR"/tapper_*_addr*.bin
