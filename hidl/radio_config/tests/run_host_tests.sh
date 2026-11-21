#!/bin/bash
# Host fixtures test callback logic; the remote Android build validates real HIDL headers.
set -euo pipefail

TEST_DIR=$(cd "${BASH_SOURCE%/*}" && pwd)
CXX=${CXX:-c++}
TEMP_ROOT=${TMPDIR:-/tmp/opencode}
mkdir -p "$TEMP_ROOT"
WORK_DIR=$(mktemp -d "$TEMP_ROOT/on7xelte-radio-config-tests.XXXXXX")
trap 'rm -rf "${WORK_DIR:?}"' EXIT

mkdir -p "$WORK_DIR/include/android/hardware/radio/config/1.0" \
    "$WORK_DIR/include/android/hardware/radio/config/1.1" \
    "$WORK_DIR/include/hidl" "$WORK_DIR/include/log"
for HEADER in android/hardware/radio/config/1.1/IRadioConfig.h \
    android/hardware/radio/config/1.1/IRadioConfigResponse.h \
    android/hardware/radio/config/1.0/IRadioConfigIndication.h \
    hidl/MQDescriptor.h hidl/Status.h log/log.h; do
    ln -s "$TEST_DIR/FakeHidl.h" "$WORK_DIR/include/$HEADER"
done

FLAGS=(-std=gnu++17 -Wall -Wextra -Werror -fsanitize=address,undefined
    -fno-omit-frame-pointer -I"$WORK_DIR/include")
for BITS in 64 32; do
    "$CXX" "${FLAGS[@]}" -m"$BITS" "$TEST_DIR/../RadioConfig.cpp" \
        "$TEST_DIR/RadioConfigTest.cpp" -o "$WORK_DIR/radio_config$BITS"
    "$WORK_DIR/radio_config$BITS"
done
