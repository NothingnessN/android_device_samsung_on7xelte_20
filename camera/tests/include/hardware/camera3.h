// Minimal host interfaces for testing forwarding/cleanup, not binary HAL ABI validation.
#pragma once
#include <stddef.h>
#include <hardware/hardware.h>

#define CAMERA_HARDWARE_MODULE_ID "camera"
#define CAMERA_MODULE_API_VERSION_2_1 0x201
#define CAMERA_MODULE_API_VERSION_2_2 0x202
#define CAMERA_MODULE_API_VERSION_2_3 0x203
#define CAMERA_MODULE_API_VERSION_2_4 0x204
#define CAMERA_DEVICE_API_VERSION_1_0 0x100
#define CAMERA_DEVICE_API_VERSION_3_0 0x300
#define CAMERA_DEVICE_API_VERSION_3_1 0x301
#define CAMERA_DEVICE_API_VERSION_3_2 0x302
#define CAMERA_DEVICE_API_VERSION_3_3 0x303
#define CAMERA_DEVICE_API_VERSION_3_4 0x304
#define CAMERA_DEVICE_API_VERSION_3_5 0x305

struct camera_metadata_t { int marker; };
struct camera_module_callbacks_t { int marker; };
struct vendor_tag_ops_t { int marker; };
struct vendor_tag_query_ops_t { int marker; };
struct camera3_callback_ops_t { int marker; };
struct camera3_stream_configuration_t { int marker; };
struct camera3_stream_buffer_set_t { int marker; };
struct camera3_capture_request_t { int marker; };
struct camera_info {
    int facing, orientation;
    uint32_t device_version;
    const camera_metadata_t* static_camera_characteristics;
    int resource_cost;
    char** conflicting_devices;
    size_t conflicting_devices_length;
};
struct camera_module_t {
    hw_module_t common;
    int (*get_number_of_cameras)();
    int (*get_camera_info)(int, camera_info*);
    int (*set_callbacks)(const camera_module_callbacks_t*);
    void (*get_vendor_tag_ops)(vendor_tag_ops_t*);
    int (*open_legacy)(const hw_module_t*, const char*, uint32_t, hw_device_t**);
    int (*set_torch_mode)(const char*, bool);
    int (*init)();
    void *get_physical_camera_info, *is_stream_combination_supported, *notify_device_state_change;
    void* reserved[2];
};
struct camera3_device;
struct camera3_device_ops_t {
    int (*initialize)(const camera3_device*, const camera3_callback_ops_t*);
    int (*configure_streams)(const camera3_device*, camera3_stream_configuration_t*);
    int (*register_stream_buffers)(const camera3_device*, const camera3_stream_buffer_set_t*);
    const camera_metadata_t* (*construct_default_request_settings)(const camera3_device*, int);
    int (*process_capture_request)(const camera3_device*, camera3_capture_request_t*);
    void (*get_metadata_vendor_tag_ops)(const camera3_device*, vendor_tag_query_ops_t*);
    void (*dump)(const camera3_device*, int);
    int (*flush)(const camera3_device*);
    void *signal_stream_flush, *is_reconfiguration_required;
    void* reserved[6];
};
typedef struct camera3_device {
    hw_device_t common;
    camera3_device_ops_t* ops;
    void* priv;
} camera3_device_t;
