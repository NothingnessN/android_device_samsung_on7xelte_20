// Exercise the real wrapper with a fake vendor HAL and allocation failures.
#include <assert.h>
#include <errno.h>
#include <initializer_list>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <cutils/log.h>
#include <hardware/camera3.h>
#include <utils/threads.h>

static bool failAllocation;
static void* test_calloc(size_t count, size_t size) {
    return failAllocation ? nullptr : calloc(count, size);
}
#define calloc test_calloc
#include "../CameraWrapper.cpp"
#undef calloc

static camera_module_t vendorModule;
static hw_module_methods_t vendorMethods;
static camera3_device_t vendorDevice;
static camera3_device_ops_t vendorOps;
static camera_metadata_t metadata;
static int loadResult, openResult, closeResult, moduleResult, forwardResult;
static int loadCalls, openCalls, closeCalls, dumpCalls, deprecatedCalls;
static bool nullModule, nullDevice;

extern "C" int hw_get_module_by_class(const char* id, const char* instance,
        const hw_module_t** module) {
    assert(strcmp(id, "camera") == 0 && strcmp(instance, "vendor") == 0);
    ++loadCalls;
    *module = nullModule ? nullptr : &vendorModule.common;
    return loadResult;
}

static int fake_number() { return 2; }
static int fake_info(int id, camera_info* info) {
    assert(id >= 0 && id < 2);
    info->device_version = vendorDevice.common.version;
    return moduleResult;
}
static int fake_close(hw_device_t* device) {
    assert(device == &vendorDevice.common);
    ++closeCalls;
    return closeResult;
}
static int fake_open(const hw_module_t* module, const char* id, hw_device_t** device) {
    assert(module == &vendorModule.common && (strcmp(id, "0") == 0 || strcmp(id, "1") == 0));
    ++openCalls;
    if (!openResult)
        *device = nullDevice ? nullptr : &vendorDevice.common;
    return openResult;
}
static int fake_legacy(const hw_module_t* module, const char* id,
        uint32_t version, hw_device_t** device) {
    assert(version == CAMERA_DEVICE_API_VERSION_1_0);
    return fake_open(module, id, device);
}
static int fake_callbacks(const camera_module_callbacks_t* callbacks) {
    assert(callbacks);
    return moduleResult;
}
static int fake_torch(const char* id, bool enabled) {
    assert(strcmp(id, "0") == 0 && enabled);
    return moduleResult;
}
static int fake_init() { return moduleResult; }
static void fake_tags(vendor_tag_ops_t* ops) { ops->marker = 42; }
static int fake_initialize(const camera3_device* device, const camera3_callback_ops_t* callbacks) {
    assert(device == &vendorDevice && callbacks);
    return forwardResult;
}
static int fake_configure(const camera3_device* device, camera3_stream_configuration_t* streams) {
    assert(device == &vendorDevice && streams);
    return forwardResult;
}
static int fake_register(const camera3_device* device, const camera3_stream_buffer_set_t* buffers) {
    assert(device == &vendorDevice && buffers);
    ++deprecatedCalls;
    return forwardResult;
}
static const camera_metadata_t* fake_settings(const camera3_device* device, int type) {
    assert(device == &vendorDevice && type == 1);
    return &metadata;
}
static int fake_request(const camera3_device* device, camera3_capture_request_t* request) {
    assert(device == &vendorDevice && request);
    return forwardResult;
}
static void fake_metadata_tags(const camera3_device* device, vendor_tag_query_ops_t* ops) {
    assert(device == &vendorDevice && ops);
    ++deprecatedCalls;
}
static void fake_dump(const camera3_device* device, int fd) {
    assert(device == &vendorDevice && fd == 42);
    ++dumpCalls;
}
static int fake_flush(const camera3_device* device) {
    assert(device == &vendorDevice);
    return forwardResult;
}

static void reset() {
    gVendorModule = nullptr;
    memset(&vendorModule, 0, sizeof(vendorModule));
    memset(&vendorDevice, 0, sizeof(vendorDevice));
    memset(&vendorOps, 0, sizeof(vendorOps));
    loadResult = openResult = closeResult = moduleResult = forwardResult = 0;
    loadCalls = openCalls = closeCalls = dumpCalls = deprecatedCalls = 0;
    failAllocation = nullModule = nullDevice = false;
    vendorMethods.open = fake_open;
    vendorModule.common.module_api_version = CAMERA_MODULE_API_VERSION_2_4;
    vendorModule.common.methods = &vendorMethods;
    vendorModule.get_number_of_cameras = fake_number;
    vendorModule.get_camera_info = fake_info;
    vendorModule.set_callbacks = fake_callbacks;
    vendorModule.get_vendor_tag_ops = fake_tags;
    vendorModule.open_legacy = fake_legacy;
    vendorModule.set_torch_mode = fake_torch;
    vendorModule.init = fake_init;
    vendorDevice.common.tag = HARDWARE_DEVICE_TAG;
    vendorDevice.common.version = CAMERA_DEVICE_API_VERSION_3_3;
    vendorDevice.common.close = fake_close;
    vendorDevice.ops = &vendorOps;
    vendorOps.initialize = fake_initialize;
    vendorOps.configure_streams = fake_configure;
    vendorOps.register_stream_buffers = fake_register;
    vendorOps.construct_default_request_settings = fake_settings;
    vendorOps.process_capture_request = fake_request;
    vendorOps.get_metadata_vendor_tag_ops = fake_metadata_tags;
    vendorOps.dump = fake_dump;
    vendorOps.flush = fake_flush;
}

int main() {
    const hw_module_t* module = &HAL_MODULE_INFO_SYM.common;
    hw_device_t* device = nullptr;
    camera_info info;
    camera_module_callbacks_t callbacks = {};
    reset();
    const char* invalidIds[] = {nullptr, "", "2", "-1", "abc", "0x0", " 0", "+0",
                               "1junk", "2147483648", "99999999999999999999999999"};
    for (const char* id : invalidIds) {
        device = reinterpret_cast<hw_device_t*>(1);
        assert(camera_device_open(module, id, &device) == -EINVAL && device == nullptr);
    }
    assert(camera_device_open(module, "0", nullptr) == -EINVAL);
    assert(camera_device_open(nullptr, "0", &device) == -EINVAL);
    assert(openCalls == 0);

    reset();
    loadResult = -ENOENT;
    assert(camera_device_open(module, "0", &device) == -ENOENT && device == nullptr);
    assert(camera_get_number_of_cameras() == 0);
    assert(camera_get_camera_info(0, &info) == -ENOENT);
    assert(camera_set_callbacks(&callbacks) == -ENOENT);
    assert(camera_set_torch_mode("0", true) == -ENOENT);
    assert(camera_init() == -ENOENT);
    assert(camera_open_legacy(module, "0", CAMERA_DEVICE_API_VERSION_1_0, &device) == -ENOENT);
    reset();
    nullModule = true;
    assert(camera_init() == -ENODEV);

    reset();
    assert(camera_get_camera_info(0, &info) == 0);
    assert(camera_device_open(module, "0", &device) == 0);
    assert(device && device != &vendorDevice.common && device->version == info.device_version);
    assert(device->version == CAMERA_DEVICE_API_VERSION_3_3);
    auto* camera = reinterpret_cast<camera3_device_t*>(device);
    assert(camera->ops->register_stream_buffers == nullptr);
    assert(camera->ops->get_metadata_vendor_tag_ops == nullptr);
    assert(camera->ops->signal_stream_flush == nullptr);
    assert(camera->ops->is_reconfiguration_required == nullptr);
    camera3_callback_ops_t deviceCallbacks = {};
    camera3_stream_configuration_t streams = {};
    camera3_capture_request_t request = {};
    assert(camera->ops->initialize(camera, &deviceCallbacks) == 0);
    forwardResult = -EREMOTEIO;
    assert(camera->ops->configure_streams(camera, &streams) == -EREMOTEIO);
    assert(camera->ops->construct_default_request_settings(camera, 1) == &metadata);
    assert(camera->ops->process_capture_request(camera, &request) == -EREMOTEIO);
    assert(camera->ops->flush(camera) == -EREMOTEIO);
    camera->ops->dump(camera, 42);
    assert(dumpCalls == 1);
    closeResult = -EIO;
    assert(device->close(device) == -EIO && closeCalls == 1);

    reset();
    failAllocation = true;
    assert(camera_device_open(module, "0", &device) == -ENOMEM && device == nullptr);
    assert(openCalls == 1 && closeCalls == 1);
    reset();
    openResult = -EBUSY;
    assert(camera_device_open(module, "0", &device) == -EBUSY && device == nullptr);
    assert(closeCalls == 0);
    reset();
    vendorDevice.ops = nullptr;
    assert(camera_device_open(module, "0", &device) == -ENODEV && device == nullptr);
    assert(closeCalls == 1);
    reset();
    vendorOps.initialize = nullptr;
    assert(camera_device_open(module, "0", &device) == -ENODEV && closeCalls == 1);
    reset();
    nullDevice = true;
    assert(camera_device_open(module, "0", &device) == -ENODEV && closeCalls == 0);

    for (uint32_t version : {CAMERA_DEVICE_API_VERSION_1_0, CAMERA_DEVICE_API_VERSION_3_5}) {
        reset();
        vendorDevice.common.version = version;
        vendorDevice.ops = reinterpret_cast<camera3_device_ops_t*>(1);
        assert(camera_device_open(module, "1", &device) == 0 && device == &vendorDevice.common);
        assert(device->close(device) == 0 && closeCalls == 1);
    }
    reset();
    assert(camera_open_legacy(module, "0", CAMERA_DEVICE_API_VERSION_1_0, &device) == 0);
    assert(device == &vendorDevice.common && openCalls == 1);
    device->close(device);
    reset();
    vendorDevice.common.version = CAMERA_DEVICE_API_VERSION_3_1;
    assert(camera_device_open(module, "0", &device) == 0);
    camera = reinterpret_cast<camera3_device_t*>(device);
    camera3_stream_buffer_set_t buffers = {};
    vendor_tag_query_ops_t oldTags = {};
    assert(camera->ops->register_stream_buffers(camera, &buffers) == 0);
    camera->ops->get_metadata_vendor_tag_ops(camera, &oldTags);
    assert(deprecatedCalls == 2);
    device->close(device);

    reset();
    vendorModule.init = nullptr;
    vendorModule.set_callbacks = nullptr;
    vendorModule.set_torch_mode = nullptr;
    vendorModule.open_legacy = nullptr;
    vendorModule.get_vendor_tag_ops = nullptr;
    assert(camera_init() == 0);
    assert(camera_set_callbacks(&callbacks) == -ENOSYS);
    assert(camera_set_torch_mode("0", true) == -ENOSYS);
    assert(camera_open_legacy(module, "0", CAMERA_DEVICE_API_VERSION_1_0, &device) == -ENOSYS);
    vendor_tag_ops_t tags = {};
    camera_get_vendor_tag_ops(&tags);
    reset();
    moduleResult = -EIO;
    assert(camera_get_camera_info(0, &info) == -EIO);
    assert(camera_set_callbacks(&callbacks) == -EIO);
    assert(camera_set_torch_mode("0", true) == -EIO);
    assert(camera_init() == -EIO);
    assert(camera_get_camera_info(-1, &info) == -EINVAL);
    assert(camera_get_camera_info(2, &info) == -EINVAL);
    assert(camera_get_camera_info(0, nullptr) == -EINVAL);
    assert(camera_device_close(nullptr) == -EINVAL);

    reset();
    std::thread threads[8];
    for (auto& thread : threads)
        thread = std::thread([] { assert(camera_get_number_of_cameras() == 2); });
    for (auto& thread : threads)
        thread.join();
    assert(loadCalls == 1);
    puts("Camera wrapper versions, forwarding, errors, cleanup and module loading: PASS");
}
