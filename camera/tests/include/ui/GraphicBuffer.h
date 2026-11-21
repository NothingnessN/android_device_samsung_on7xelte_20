// Host constructor-forwarding fixture, NOT an Android GraphicBuffer layout.
#pragma once
#include <stdint.h>

struct native_handle { int version, numFds, numInts; };
typedef struct native_handle native_handle_t;

#ifndef TEST_GRAPHICBUFFER_SIZE
#define TEST_GRAPHICBUFFER_SIZE 0xa0
#endif

namespace android {
class alignas(8) GraphicBuffer {
public:
    enum HandleWrapMethod : uint8_t { WRAP_HANDLE, TAKE_HANDLE };
    GraphicBuffer(const native_handle_t*, HandleWrapMethod, uint32_t width,
            uint32_t height, int format, uint32_t layers, uint64_t usage, uint32_t stride);
private:
    ~GraphicBuffer();
    unsigned char storage[TEST_GRAPHICBUFFER_SIZE];
};
}
