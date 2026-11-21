#!/bin/bash
# Requires a C++ compiler with 32-bit host C++ libraries (multilib).
set -euo pipefail

TEST_DIR=$(cd "${BASH_SOURCE%/*}" && pwd)
DEVICE_DIR=$(cd "$TEST_DIR/../.." && pwd)
CXX=${CXX:-c++}
TEMP_ROOT=${TMPDIR:-/tmp/opencode}
mkdir -p "$TEMP_ROOT"
WORK_DIR=$(mktemp -d "$TEMP_ROOT/on7xelte-camera-tests.XXXXXX")
trap 'rm -rf "${WORK_DIR:?}"' EXIT

FLAGS=(-std=gnu++17 -Wall -Wextra -Werror -Wno-missing-field-initializers
       -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -I"$TEST_DIR/include")

for BITS in 64 32; do
    "$CXX" "${FLAGS[@]}" -m"$BITS" "$TEST_DIR/FenceTest.cpp" \
        "$DEVICE_DIR/shims/libexynoscamera/Fence.cpp" -o "$WORK_DIR/fence$BITS"
    "$WORK_DIR/fence$BITS"
    "$CXX" "${FLAGS[@]}" -m"$BITS" "$TEST_DIR/CameraWrapperTest.cpp" \
        -o "$WORK_DIR/wrapper$BITS"
    "$WORK_DIR/wrapper$BITS"
done

"$CXX" "${FLAGS[@]}" -m32 "$TEST_DIR/GraphicBufferTest.cpp" \
    "$DEVICE_DIR/shims/libexynoscamera/GraphicBuffer.cpp" -o "$WORK_DIR/graphicbuffer32"
"$WORK_DIR/graphicbuffer32"

# The production compile-time guards must reject a larger class and a 64-bit shim.
if "$CXX" "${FLAGS[@]}" -m32 -DTEST_GRAPHICBUFFER_SIZE=0xa8 -c \
        "$DEVICE_DIR/shims/libexynoscamera/GraphicBuffer.cpp" -o "$WORK_DIR/oversized.o" \
        >"$WORK_DIR/oversized.log" 2>&1; then
    echo "ERROR: oversized GraphicBuffer was accepted" >&2
    exit 1
fi
grep -Fq 'Increase the verified blob allocation fixups' "$WORK_DIR/oversized.log"
if "$CXX" "${FLAGS[@]}" -m64 -c "$DEVICE_DIR/shims/libexynoscamera/GraphicBuffer.cpp" \
        -o "$WORK_DIR/64bit.o" >"$WORK_DIR/64bit.log" 2>&1; then
    echo "ERROR: 64-bit camera shim was accepted" >&2
    exit 1
fi
grep -Fq 'require a 32-bit shim' "$WORK_DIR/64bit.log"
echo "GraphicBuffer size and architecture guards: PASS"

PYTHONDONTWRITEBYTECODE=1 python3 "$TEST_DIR/test_blob_fixups.py"
