// Test the real shim's this/argument forwarding against a typed constructor.
#include "../../shims/libexynoscamera/LegacyCamera.h"
#include <ui/GraphicBuffer.h>

#include <assert.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>

static void* capturedThis;
static const native_handle_t* capturedHandle;
static android::GraphicBuffer::HandleWrapMethod capturedMethod;
static uint32_t capturedWidth, capturedHeight, capturedLayers, capturedStride;
static int capturedFormat;
static uint64_t capturedUsage;

android::GraphicBuffer::GraphicBuffer(const native_handle_t* handle, HandleWrapMethod method,
        uint32_t width, uint32_t height, int format, uint32_t layers, uint64_t usage,
        uint32_t stride) {
    capturedThis = this;
    capturedHandle = handle;
    capturedMethod = method;
    capturedWidth = width;
    capturedHeight = height;
    capturedFormat = format;
    capturedLayers = layers;
    capturedUsage = usage;
    capturedStride = stride;
    memset(storage, 0xa5, sizeof(storage));
}

int main() {
    static_assert(sizeof(void*) == 4, "Run this forwarding test with -m32");
    alignas(8) unsigned char allocation[8 + kExynosGraphicBufferAllocationSize + 8];
    native_handle_t handle = {12, 1, 36};
    for (bool own : {false, true}) {
        memset(allocation, 0x5a, sizeof(allocation));
        void* buffer = allocation + 8;
        assert(exynos_graphicbuffer_ctor(buffer, 1920, 1080, 0x23,
                0x80000203, 2048, &handle, own) == buffer);
        assert(capturedThis == buffer && capturedHandle == &handle);
        assert(capturedMethod == (own ? android::GraphicBuffer::TAKE_HANDLE :
                                      android::GraphicBuffer::WRAP_HANDLE));
        assert(capturedWidth == 1920 && capturedHeight == 1080);
        assert(capturedFormat == 0x23 && capturedLayers == 1);
        assert(capturedUsage == 0x80000203ULL && capturedStride == 2048);
        for (int i = 0; i < 8; ++i) {
            assert(allocation[i] == 0x5a);
            assert(allocation[8 + kExynosGraphicBufferAllocationSize + i] == 0x5a);
        }
    }
    puts("GraphicBuffer this, arguments and ownership mode: PASS");
}
