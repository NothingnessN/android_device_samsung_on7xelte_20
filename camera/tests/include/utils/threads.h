// Host-only implementation of the mutex interface used by CameraWrapper.cpp.
#pragma once
#include <mutex>

namespace android {
class Mutex {
public:
    class Autolock {
    public:
        explicit Autolock(Mutex& mutex) : mMutex(mutex) { mMutex.mMutex.lock(); }
        ~Autolock() { mMutex.mMutex.unlock(); }
    private:
        Mutex& mMutex;
    };
private:
    std::mutex mMutex;
};
}
