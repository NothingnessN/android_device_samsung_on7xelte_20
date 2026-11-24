# ARM32 rild SDK-27 compatibility shim

## Purpose and scope

This constructor calls `android_set_application_target_sdk_version(27)` before
the installed `rild` loads Samsung's RIL blob. It logs an in-process getter
readback under `RildSdk27Shim`. It selects existing SDK-dependent compatibility
behavior; it does **not** repair the blob's uninitialized mutex attributes.

The suspected initializer self-deadlock remains **UNCONFIRMED**. The inspected
Bootleggers Android 13 libc already has UL's SDK >=28 PI-mutex gate. Its missing
compiled `rild=27` override, not a missing libc gate, motivates this experiment.
No Bionic, platform-ROM, Bluetooth, audio, or vendor-blob source is changed.

## Integration

`BoardConfig.mk` injects the shim into the executable only:

```make
TARGET_LD_SHIM_LIBS += \
    /vendor/bin/hw/rild|/system/lib/librild_sdk27_shim.so
```

`device.mk` packages `librild_sdk27_shim`. `Android.bp` builds a system-side
ARM32 shared library, with `libdl`, `libdl_android`, and `liblog`, and no STL.
Do not mark it vendor/proprietary: the inspected Android 13 `libdl_android`
module has no vendor/LL-NDK variant. Existing camera/RIL shims and UL SDK
override entries are unchanged. The SDK shim is **not** mapped to `libsec-ril.so`.

The inspected loader canonicalizes executable mappings. Thus `/vendor/bin/hw/rild`
matches Bootleggers' `/system/vendor/bin/hw/rild`. Every launch of that same
executable can receive the shim, including a secondary rild; this is not an
argument- or primary-instance-only filter. Other executables are not matched.

## Required ROM preflight

- The ROM must consume `TARGET_LD_SHIM_LIBS` through its existing Soong defaults
  and support executable shim injection in the target ARM32 linker.
- Its **actual ARM32 libc** must already gate PI initialization on process SDK
  >=28, as verified for the inspected Bootleggers runtime. Lowering SDK alone
  cannot provide that behavior on ROMs without the gate.
- `libdl_android.so` must export the setter; `libdl.so` must export the getter.
  The public getter is declared in `<android/api-level.h>`. The private setter
  is explicitly declared with its verified C ABI; it is not a libc export or
  a declaration in the public `<android/dlext.h>`.
- The rild namespace must permit `/system/lib/librild_sdk27_shim.so` and its
  dependencies. The inspected Bootleggers legacy namespace is not isolated;
  that is not a guarantee for a different ROM or build.

If a prerequisite is absent, stop and report it. Do not add platform patches,
weaken namespaces/permissions, or use a guessed linker symbol. Missing library
or symbol resolution can prevent rild startup before the constructor can log.

## UL comparison and constructor timing

[UL Bionic d51393f](https://github.com/LineageOS-UL/android_bionic/commit/d51393f91522cd9d20ff36794d332fd3146870ba)
selects an executable-path override inside `init_default_namespaces`, before
ordinary library loading. It consumes the `SDK_VERSION_OVERRIDES` definition.
[UL vendor/lineage 8a81ccc](https://github.com/LineageOS-UL/android_vendor_lineage/commit/8a81cccc5c1b7e5afab5766e448c0ef82f3ab673)
restores `TARGET_PROCESS_SDK_VERSION_OVERRIDE` -> Soong -> that definition.

This device-only alternative uses the verified libdl_android wrapper to reach
the same process SDK setter, but **later**: initial linking, executable
`.preinit`, and the shim's dependency constructors precede its constructor.
Executable shims are inserted after preloads and before ordinary DT_NEEDED
entries in the inspected loader. The installed rild dlopens its blob in `main`,
after executable constructors, so this shim precedes that blob's constructors
and RIL initialization.

Earlier SDK-dependent namespace/loading decisions are not retroactively redone.
SDK 27 affects the entire rild process, including SDK-dependent libc/fdsan
behavior, not just Samsung's library. This is not fully equivalent to UL's
early linker override and is not a platform-wide SDK change.

## Build and host tests — user-run, not executed by the assistant

From the existing ROM source root, retaining its working product definition:

```sh
source build/envsetup.sh
read -r -p 'Existing on7xelte ROM lunch target: ' ROM_LUNCH
lunch "$ROM_LUNCH"
m librild_sdk27_shim android.hardware.radio.config@1.1-service.on7xelte linker
m -j"$(nproc)"
```

This checkout registers `lineage_on7xelte-userdebug`; it does not establish the
remote Bootleggers lunch target. Use the ROM's normal distribution target too
if needed. The shim map is **compiled into the linker**: a library-only build
or copy is insufficient. The normal product output must include the updated
runtime linker/APEX packaging. Do not replace libc/linker/APEX files ad hoc.

The separate single-SIM radio-config change returns `RadioError::NONE` for
modem 0 and `REQUEST_NOT_SUPPORTED` for IDs 1–255. Its host fixtures can be run:

```sh
CXX=c++ bash device/samsung/on7xelte/hidl/radio_config/tests/run_host_tests.sh
```

This requires 64- and 32-bit C++/ASan/UBSan support. Fixtures cover all 256 IDs,
serial preservation, null/1.0/replaced callbacks, and transport failures. They
do not validate Android HIDL ABI, shim linkage, or modem behavior.

## Device acceptance — read-only after the user's normal deployment/boot

1. Identify the **current primary** `rild -c 1 -l /system/lib/libsec-ril.so` PID.
   Do not use a secondary sleep-loop PID or reuse a previous boot's PID.
2. Find its `RildSdk27Shim` log: `target SDK: 10000 -> 27 (requested 27)`, or
   `27 -> 27` when an early override already applied. This is a getter readback
   inside rild at initialization, unlike `ro.build.version.sdk`, an ELF Android
   note, or a getter run in a separate helper process.
3. Verify progression beyond the old initializer endpoint, then successful
   `Open: opened [fd,/dev/umts_ipc0].` and RFS-open messages. Kernel
   `umts_ipc0.opened=1` **and** `umts_rfs0.opened=1`, when retained, corroborate
   channel opening. These are expected checks, not already captured results.
4. Check the modem-0 framework response has no error 6/retry loop. Other stub
   operations intentionally remain unsupported. Passing the initializer stall
   does not establish SIM detection or network registration.

Do not clear logs, restart/interrupt services, elevate privileges, change
permissions, read EFS/NV/identity/calibration data, or open modem device nodes.
If SDK 27 is verified but the same stall remains, stop and report instead of
forcing a wider fix. Successful startup still does not identify the mutex.
