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

#include <errno.h>
#include <new>
#include <sync/sync.h>
#include <unistd.h>

extern "C" ExynosLegacyFence* exynos_fence_ctor(ExynosLegacyFence* fence, int fd) {
    // ARM C++ constructors return this in r0, even though C++ spells them void.
    return new (fence) ExynosLegacyFence{0, fd};
}

extern "C" int exynos_fence_wait(const ExynosLegacyFence* fence, int timeout) {
    if (fence == nullptr) {
        return -EINVAL;
    }
    if (fence->fd == -1) {
        // HAL3: no acquire/release fence means the buffer is already ready.
        return 0;
    }
    int result = sync_wait(fence->fd, timeout);
    return result < 0 ? -errno : 0;
}

extern "C" ExynosLegacyFence* exynos_fence_dtor(ExynosLegacyFence* fence) {
    if (fence != nullptr) {
        int fd = fence->fd;
        fence->fd = -1;
        if (fd >= 0) {
            // The constructor takes ownership. Do not retry close after EINTR.
            close(fd);
        }
    }
    // The vendor's decStrong calls operator delete with r0 immediately after
    // this destructor. Returning void would let close() clobber that pointer.
    return fence;
}
