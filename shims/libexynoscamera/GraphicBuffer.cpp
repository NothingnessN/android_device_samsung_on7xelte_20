/*
 * Copyright (C) 2018 The LineageOS Project
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

#include "LegacyCamera.h"

#include <new>
#include <ui/GraphicBuffer.h>

static_assert(sizeof(void*) == 4, "These camera blobs require a 32-bit shim");
static_assert(sizeof(android::GraphicBuffer) <= kExynosGraphicBufferAllocationSize,
        "Increase the verified blob allocation fixups before using a larger GraphicBuffer");
static_assert(alignof(android::GraphicBuffer) <= 8,
        "Legacy operator new cannot provide this GraphicBuffer alignment");

extern "C" void* exynos_graphicbuffer_ctor(
        void* buffer,
        uint32_t inWidth,
        uint32_t inHeight,
        int inFormat,
        uint32_t inUsage,
        uint32_t inStride,
        native_handle_t* inHandle,
        bool keepOwnership)
{
    android::GraphicBuffer::HandleWrapMethod inMethod =
        (keepOwnership ? android::GraphicBuffer::TAKE_HANDLE : android::GraphicBuffer::WRAP_HANDLE);
    // Use the real C++ declaration so this and the 64-bit usage argument are
    // passed with the correct ARM calling convention. No guessed mangled name.
    new (buffer) android::GraphicBuffer(inHandle, inMethod, inWidth, inHeight,
        inFormat, 1, static_cast<uint64_t>(inUsage), inStride);
    return buffer;
}
