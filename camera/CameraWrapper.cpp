/*
 * Copyright (C) 2017, The LineageOS Project
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

/**
* @file CameraWrapper.cpp
*
* This file wraps a vendor camera module.
*
*/

#define LOG_NDEBUG 0

#define LOG_TAG "CameraWrapper"
#include <cutils/log.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <hardware/hardware.h>
#include <hardware/camera3.h>
#include <utils/threads.h>

static android::Mutex gCameraWrapperLock;
static android::Mutex gVendorModuleLock;
static camera_module_t *gVendorModule = 0;

static int camera_device_open(const hw_module_t *module, const char *name,
        hw_device_t **device);
static int camera_get_number_of_cameras(void);
static int camera_get_camera_info(int camera_id, struct camera_info *info);
static int camera_set_callbacks(const camera_module_callbacks_t *callbacks);
static void camera_get_vendor_tag_ops(vendor_tag_ops_t *ops);
static int camera_open_legacy(const struct hw_module_t *module,
        const char *id, uint32_t halVersion, struct hw_device_t **device);
static int camera_set_torch_mode(const char *camera_id, bool enabled);
static int camera_init();

static struct hw_module_methods_t camera_module_methods = {
    .open = camera_device_open
};

camera_module_t HAL_MODULE_INFO_SYM = {
    .common = {
         .tag = HARDWARE_MODULE_TAG,
         .module_api_version = CAMERA_MODULE_API_VERSION_2_4,
         .hal_api_version = HARDWARE_HAL_API_VERSION,
         .id = CAMERA_HARDWARE_MODULE_ID,
         .name = "Samsung Camera Wrapper",
         .author = "The LineageOS Project",
         .methods = &camera_module_methods,
         .dso = NULL, /* remove compilation warnings */
         .reserved = {0}, /* remove compilation warnings */
    },
    .get_number_of_cameras = camera_get_number_of_cameras,
    .get_camera_info = camera_get_camera_info,
    .set_callbacks = camera_set_callbacks,
    .get_vendor_tag_ops = camera_get_vendor_tag_ops,
    .open_legacy = camera_open_legacy,
    .set_torch_mode = camera_set_torch_mode,
    .init = camera_init,
    .reserved = {0}, /* remove compilation warnings */
};

typedef struct wrapper_camera_device {
    camera3_device_t base;
    camera3_device_ops_t ops;
    int id;
    camera3_device_t *vendor;
} wrapper_camera_device_t;

#define VENDOR_CALL(device, func, ...) ({ \
    wrapper_camera_device_t *__wrapper_dev = (wrapper_camera_device_t*) device; \
    __wrapper_dev->vendor->ops->func(__wrapper_dev->vendor, ##__VA_ARGS__); \
})

/*******************************************************************
 * common functions
 *******************************************************************/

static int check_vendor_module()
{
    int rv;
    camera_module_t *vendor_module = NULL;
    ALOGV("%s", __FUNCTION__);

    android::Mutex::Autolock lock(gVendorModuleLock);

    if (gVendorModule)
        return 0;

    rv = hw_get_module_by_class("camera", "vendor",
            (const hw_module_t**)&vendor_module);

    if (rv) {
        ALOGE("failed to open vendor camera module: %d", rv);
        return rv;
    }
    if (!vendor_module || !vendor_module->common.methods ||
            !vendor_module->common.methods->open ||
            !vendor_module->get_number_of_cameras || !vendor_module->get_camera_info) {
        ALOGE("invalid vendor camera module");
        return -ENODEV;
    }
    gVendorModule = vendor_module;
    return 0;
}

/*******************************************************************
 * implementation of camera3_device_ops functions
 *******************************************************************/

int camera_initialize(const struct camera3_device *device,
    const camera3_callback_ops_t *callback_ops)
{
    if(!device)
        return -ENODEV;

    ALOGV("%s->%p->%p", __FUNCTION__, device, ((wrapper_camera_device_t*)device)->vendor);

    return VENDOR_CALL(device, initialize, callback_ops);
}

int camera_configure_streams(const struct camera3_device *device,
    camera3_stream_configuration_t *stream_list)
{
    if(!device)
        return -ENODEV;

    ALOGV("%s->%p->%p", __FUNCTION__, device, ((wrapper_camera_device_t*)device)->vendor);

    return VENDOR_CALL(device, configure_streams, stream_list);
}

int camera_register_stream_buffers(const struct camera3_device *device,
    const camera3_stream_buffer_set_t *buffer_set)
{
    if (!device)
        return -ENODEV;

    return VENDOR_CALL(device, register_stream_buffers, buffer_set);
}

const camera_metadata_t * camera_construct_default_request_settings(
    const struct camera3_device *device , int type)
{
    if(!device)
        return NULL;

    ALOGV("%s->%p->%p", __FUNCTION__, device, ((wrapper_camera_device_t*)device)->vendor);

    return VENDOR_CALL(device, construct_default_request_settings, type);
}

int camera_process_capture_request(const struct camera3_device *device,
    camera3_capture_request_t *request)
{
    if(!device)
        return -ENODEV;

    ALOGV("%s->%p->%p", __FUNCTION__, device, ((wrapper_camera_device_t*)device)->vendor);

    return VENDOR_CALL(device, process_capture_request, request);
}

void camera_get_metadata_vendor_tag_ops(const struct camera3_device *device,
    vendor_tag_query_ops_t *ops)
{
    if(!device)
        return;

    ALOGV("%s->%p->%p", __FUNCTION__, device, ((wrapper_camera_device_t*)device)->vendor);

    VENDOR_CALL(device, get_metadata_vendor_tag_ops, ops);
}

void camera_dump(const struct camera3_device *device, int fd)
{
    if(!device)
        return;

    ALOGV("%s->%p->%p", __FUNCTION__, device, ((wrapper_camera_device_t*)device)->vendor);

    return VENDOR_CALL(device, dump, fd);
}

int camera_flush(const struct camera3_device *device)
{
    if(!device)
        return -ENODEV;

    ALOGV("%s->%p->%p", __FUNCTION__, device, ((wrapper_camera_device_t*)device)->vendor);

    return VENDOR_CALL(device, flush);
}

extern "C" void heaptracker_free_leaked_memory(void);

int camera_device_close(hw_device_t* device)
{
    int ret = 0;
    wrapper_camera_device_t *wrapper_dev = NULL;

    ALOGI("%s", __FUNCTION__);

    android::Mutex::Autolock lock(gCameraWrapperLock);

    if (!device) {
        ret = -EINVAL;
        goto done;
    }

    wrapper_dev = (wrapper_camera_device_t*) device;

    ret = wrapper_dev->vendor->common.close((hw_device_t*)wrapper_dev->vendor);
    free(wrapper_dev);
done:
#ifdef HEAPTRACKER
    heaptracker_free_leaked_memory();
#endif
    return ret;
}

/*******************************************************************
 * implementation of camera_module functions
 *******************************************************************/

static int camera_device_open(const hw_module_t *module, const char *name,
        hw_device_t **device)
{
    android::Mutex::Autolock lock(gCameraWrapperLock);
    ALOGI("camera_device open");

    if (!device)
        return -EINVAL;
    *device = NULL;
    if (!module || !name || name[0] < '0' || name[0] > '9')
        return -EINVAL;

    char *end = NULL;
    errno = 0;
    long cameraid = strtol(name, &end, 10);
    if (errno == ERANGE || *end != '\0' || cameraid < 0 || cameraid > INT_MAX)
        return -EINVAL;

    int rv = check_vendor_module();
    if (rv)
        return rv;
    int num_cameras = gVendorModule->get_number_of_cameras();
    if (cameraid >= num_cameras) {
        ALOGE("cameraid out of bounds: %ld, num supported = %d", cameraid, num_cameras);
        return -EINVAL;
    }

    hw_device_t *vendor_device = NULL;
    rv = gVendorModule->common.methods->open(&gVendorModule->common, name, &vendor_device);
    if (rv) {
        ALOGE("vendor camera open failed: %d", rv);
        return rv;
    }
    if (!vendor_device || vendor_device->tag != HARDWARE_DEVICE_TAG || !vendor_device->close) {
        ALOGE("vendor returned an invalid camera device");
        if (vendor_device && vendor_device->close)
            vendor_device->close(vendor_device);
        return -ENODEV;
    }

    // Never reinterpret HAL1 as HAL3, or truncate a newer HAL3 ops table.
    if (vendor_device->version < CAMERA_DEVICE_API_VERSION_3_0 ||
            vendor_device->version > CAMERA_DEVICE_API_VERSION_3_4) {
        *device = vendor_device;
        return 0;
    }

    camera3_device_t *vendor_camera = (camera3_device_t*)vendor_device;
    const camera3_device_ops_t *vendor_ops = vendor_camera->ops;
    if (!vendor_ops || !vendor_ops->initialize || !vendor_ops->configure_streams ||
            !vendor_ops->construct_default_request_settings ||
            !vendor_ops->process_capture_request || !vendor_ops->dump ||
            (vendor_device->version >= CAMERA_DEVICE_API_VERSION_3_1 && !vendor_ops->flush)) {
        ALOGE("vendor returned an incomplete HAL3 ops table");
        vendor_device->close(vendor_device);
        return -ENODEV;
    }

    wrapper_camera_device_t *camera_device =
            (wrapper_camera_device_t*)calloc(1, sizeof(*camera_device));
    if (!camera_device) {
        ALOGE("camera_device allocation failed");
        vendor_device->close(vendor_device);
        return -ENOMEM;
    }
    camera_device->id = (int)cameraid;
    camera_device->vendor = vendor_camera;
    camera_device->base.common.tag = HARDWARE_DEVICE_TAG;
    // get_camera_info and open must report the same, actual vendor version.
    camera_device->base.common.version = vendor_device->version;
    camera_device->base.common.module = (hw_module_t*)module;
    camera_device->base.common.close = camera_device_close;
    camera_device->base.ops = &camera_device->ops;

    camera3_device_ops_t *camera_ops = &camera_device->ops;
    camera_ops->initialize = camera_initialize;
    camera_ops->configure_streams = camera_configure_streams;
    if (vendor_device->version < CAMERA_DEVICE_API_VERSION_3_2) {
        camera_ops->register_stream_buffers = vendor_ops->register_stream_buffers ?
                camera_register_stream_buffers : NULL;
        camera_ops->get_metadata_vendor_tag_ops = vendor_ops->get_metadata_vendor_tag_ops ?
                camera_get_metadata_vendor_tag_ops : NULL;
    }
    camera_ops->construct_default_request_settings = camera_construct_default_request_settings;
    camera_ops->process_capture_request = camera_process_capture_request;
    camera_ops->dump = camera_dump;
    camera_ops->flush = vendor_ops->flush ? camera_flush : NULL;

    *device = &camera_device->base.common;
    return 0;
}

static int camera_get_number_of_cameras(void)
{
    ALOGV("%s", __FUNCTION__);
    if (check_vendor_module())
        return 0;

    return gVendorModule->get_number_of_cameras();
}

static int camera_get_camera_info(int camera_id, struct camera_info *info)
{
    ALOGV("%s", __FUNCTION__);
    if (!info)
        return -EINVAL;
    memset(info, 0, sizeof(*info));
    int rv = check_vendor_module();
    if (rv)
        return rv;
    if (camera_id < 0 || camera_id >= gVendorModule->get_number_of_cameras())
        return -EINVAL;

    return gVendorModule->get_camera_info(camera_id, info);
}

static int camera_set_callbacks(const camera_module_callbacks_t *callbacks)
{
    ALOGV("%s", __FUNCTION__);
    if (!callbacks)
        return -EINVAL;
    int rv = check_vendor_module();
    if (rv)
        return rv;
    if (gVendorModule->common.module_api_version < CAMERA_MODULE_API_VERSION_2_1 ||
            !gVendorModule->set_callbacks)
        return -ENOSYS;

    return gVendorModule->set_callbacks(callbacks);
}

static void camera_get_vendor_tag_ops(vendor_tag_ops_t *ops)
{
    ALOGV("%s", __FUNCTION__);
    if (!ops || check_vendor_module())
        return;
    if (gVendorModule->common.module_api_version < CAMERA_MODULE_API_VERSION_2_2 ||
            !gVendorModule->get_vendor_tag_ops)
        return;

    gVendorModule->get_vendor_tag_ops(ops);
}

static int camera_open_legacy(const struct hw_module_t *module,
        const char *id, uint32_t halVersion, struct hw_device_t **device)
{
    ALOGV("%s", __FUNCTION__);
    if (!device)
        return -EINVAL;
    *device = NULL;
    if (!module || !id)
        return -EINVAL;
    android::Mutex::Autolock lock(gCameraWrapperLock);
    int rv = check_vendor_module();
    if (rv)
        return rv;
    if (gVendorModule->common.module_api_version < CAMERA_MODULE_API_VERSION_2_3 ||
            !gVendorModule->open_legacy)
        return -ENOSYS;

    return gVendorModule->open_legacy(&gVendorModule->common, id, halVersion, device);
}

static int camera_set_torch_mode(const char *camera_id, bool enabled)
{
    ALOGV("%s", __FUNCTION__);
    if (!camera_id)
        return -EINVAL;
    int rv = check_vendor_module();
    if (rv)
        return rv;
    if (gVendorModule->common.module_api_version < CAMERA_MODULE_API_VERSION_2_4 ||
            !gVendorModule->set_torch_mode)
        return -ENOSYS;

    return gVendorModule->set_torch_mode(camera_id, enabled);
}

static int camera_init()
{
    ALOGV("%s", __FUNCTION__);
    int rv = check_vendor_module();
    if (rv)
        return rv;
    if (gVendorModule->common.module_api_version < CAMERA_MODULE_API_VERSION_2_4 ||
            !gVendorModule->init)
        return 0;

    return gVendorModule->init();
}
