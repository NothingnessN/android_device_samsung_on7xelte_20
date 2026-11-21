// Host tests for the real shim, with sync_wait mocked and real fd ownership.
#include "../../shims/libexynoscamera/LegacyCamera.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <initializer_list>
#include <stdio.h>
#include <unistd.h>

static int calls, lastFd, lastTimeout, waitResult, waitErrno;

extern "C" int sync_wait(int fd, int timeout) {
    ++calls;
    lastFd = fd;
    lastTimeout = timeout;
    errno = waitErrno;
    return waitResult;
}

int main() {
    struct Guarded { uint32_t before; ExynosLegacyFence fence; uint32_t after; } object;
    object.before = 0x12345678;
    object.after = 0xabcdef01;
    assert(exynos_fence_ctor(&object.fence, -1) == &object.fence);
    assert(object.fence.refCount == 0 && object.fence.fd == -1);
    assert(exynos_fence_wait(&object.fence, 1000) == 0 && calls == 0);
    assert(exynos_fence_dtor(&object.fence) == &object.fence);
    assert(object.before == 0x12345678 && object.after == 0xabcdef01);
    assert(exynos_fence_wait(nullptr, 0) == -EINVAL);

    int fds[2];
    assert(pipe(fds) == 0);
    assert(exynos_fence_ctor(&object.fence, fds[0]) == &object.fence);
    ++object.fence.refCount;  // same offset as the vendor's inlined incStrong
    waitResult = 0;
    assert(exynos_fence_wait(&object.fence, 123) == 0);
    assert(calls == 1 && lastFd == fds[0] && lastTimeout == 123);
    for (int error : {ETIME, ETIMEDOUT, EBADF, EINVAL, ENOMEM}) {
        waitResult = -1;
        waitErrno = error;
        assert(exynos_fence_wait(&object.fence, -1) == -error);
        assert(lastFd == fds[0] && lastTimeout == -1);
    }
    assert(object.fence.refCount == 1);
    assert(exynos_fence_dtor(&object.fence) == &object.fence);
    assert(object.fence.fd == -1);
    assert(fcntl(fds[0], F_GETFD) == -1 && errno == EBADF);
    assert(fcntl(fds[1], F_GETFD) >= 0);
    // A repeated explicit destructor must not close a recycled descriptor.
    int recycled = dup(fds[1]);
    assert(recycled >= 0);
    assert(exynos_fence_dtor(&object.fence) == &object.fence);
    assert(fcntl(recycled, F_GETFD) >= 0);
    close(recycled);
    close(fds[1]);
    assert(object.before == 0x12345678 && object.after == 0xabcdef01);

    object.fence.fd = -2;
    waitResult = -1;
    waitErrno = EINVAL;
    assert(exynos_fence_wait(&object.fence, 0) == -EINVAL);
    puts("Fence layout, waits, errors and ownership: PASS");
}
