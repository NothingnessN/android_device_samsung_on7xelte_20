// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "RildSdk27Shim"

#include <android/api-level.h>
#include <log/log.h>

// Private platform ABI in libdl_android.so, forwarded to the linker.
// The public android/dlext.h does not declare it; libc.so does not export it.
extern "C" void android_set_application_target_sdk_version(int target);

namespace {

// Injected into the rild executable before it dlopens Samsung's RIL blob.
// This selects an existing libc compatibility path; it does not fix mutex
// attributes or provide the pre-P PI gate on ROMs that lack that gate.
__attribute__((constructor)) void setRildTargetSdkVersion() {
    const int previous = android_get_application_target_sdk_version();
    android_set_application_target_sdk_version(27);
    const int current = android_get_application_target_sdk_version();

    if (current != 27) {
        ALOGE("target SDK: %d -> %d (requested 27; override failed)", previous, current);
        return;
    }
    ALOGI("target SDK: %d -> %d (requested 27)", previous, current);
}

}  // namespace
