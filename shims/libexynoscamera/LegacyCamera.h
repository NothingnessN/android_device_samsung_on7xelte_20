/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

struct native_handle;

// The blobs inline LightRefBase operations at offset 0 and getFd() at offset 4.
// This is NOT an Android 13 Fence: it has no vtable and is only eight bytes.
struct ExynosLegacyFence {
    int32_t refCount;
    int32_t fd;
};

static_assert(sizeof(ExynosLegacyFence) == 8, "Legacy Fence allocation size changed");
static_assert(offsetof(ExynosLegacyFence, refCount) == 0, "Legacy Fence refcount moved");
static_assert(offsetof(ExynosLegacyFence, fd) == 4, "Legacy Fence fd moved");

// Must match the two movs r0, #0xa0 fixups in camera/fixup_camera_blobs.py.
constexpr size_t kExynosGraphicBufferAllocationSize = 0xa0;

// Only the patched camera imports use these names. Never interpose the modern
// framework's android::Fence symbols with an incompatible object layout.
extern "C" {
__attribute__((visibility("default")))
ExynosLegacyFence* exynos_fence_ctor(ExynosLegacyFence* fence, int fd);

__attribute__((visibility("default")))
int exynos_fence_wait(const ExynosLegacyFence* fence, int timeout);

__attribute__((visibility("default")))
ExynosLegacyFence* exynos_fence_dtor(ExynosLegacyFence* fence);

__attribute__((visibility("default")))
void* exynos_graphicbuffer_ctor(void* buffer, uint32_t width, uint32_t height,
        int format, uint32_t usage, uint32_t stride, native_handle* handle,
        bool keepOwnership);
}
