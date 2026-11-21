// Minimal host HAL fixture. Production builds use libhardware's real headers.
#pragma once
#include <stdint.h>

#define HARDWARE_MODULE_TAG 0xfacade00U
#define HARDWARE_DEVICE_TAG 0xfacade01U
#define HARDWARE_HAL_API_VERSION 0x100
#define HAL_MODULE_INFO_SYM HMI

struct hw_module_t;
struct hw_device_t;
struct hw_module_methods_t {
    int (*open)(const hw_module_t*, const char*, hw_device_t**);
};
struct hw_module_t {
    uint32_t tag;
    uint16_t module_api_version, hal_api_version;
    const char *id, *name, *author;
    hw_module_methods_t* methods;
    void* dso;
    uintptr_t reserved[25];
};
struct hw_device_t {
    uint32_t tag, version;
    hw_module_t* module;
    uintptr_t reserved[12];
    int (*close)(hw_device_t*);
};
extern "C" int hw_get_module_by_class(const char*, const char*, const hw_module_t**);
