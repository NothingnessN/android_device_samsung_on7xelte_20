# on7xelte Android 13 camera compatibility

These changes address confirmed camera-service and vendor ABI defects in the
Android 13 device tree. Camera operation still needs a complete ROM rebuild and
testing on the phone; host tests are not a substitute for that.

## Changes that must ship together

- `BoardConfig.mk` disables `TARGET_HAS_LEGACY_CAMERA_HAL1`. In Bootleggers this
  option controls whether CameraService is embedded in mediaserver, not whether
  a vendor HAL1 device can be exposed. Android 13's CameraServiceProxy expects
  CameraService to run as UID 1047; mediaserver runs as UID 1013 and fails to
  initialize the allowed camera users.
- `device.mk` packages standalone `cameraserver`. Both camera blobs receive the
  `libexynoscamera_shim` linker mapping; the Android 13 tree's existing
  `libstagefright_shim` mappings and `Camera2` package are retained.
- `shims/libexynoscamera/` provides camera-private Fence functions compatible
  with the blobs' eight-byte object layout. FD `-1` is a successful no-fence
  wait; other FDs use `sync_wait`, retain errors, and are closed on destruction.
  Modern framework Fence symbols are not interposed.
- The GraphicBuffer shim uses a typed placement constructor with an explicit
  object pointer, one layer, 64-bit usage conversion, and unchanged handle
  ownership semantics. Both blob allocations grow from `0x88` to `0xa0` bytes.
  Production compilation rejects a larger GraphicBuffer or a 64-bit shim.
- HAL3's software-fence validity check accepts FD `-1` without changing the
  hardware-fence checks, actual waits, or error paths.
- `CameraWrapper.cpp` reports the actual vendor device version, validates IDs
  and outputs, propagates errors, avoids null optional callbacks, closes the
  vendor device on allocation failure, and synchronizes module loading. HAL1
  and newer HAL3 devices pass through without being reinterpreted.

No JPEG/CSC/current-shot pointer patches or teardown bypasses are included.
Earlier crashes in those paths must be retested after these confirmed fixes.

## Guarded blob fixups

`fixup_camera_blobs.py` accepts only the pinned originals or their exact patched
forms. It preserves file sizes, symbol indices, relocations, and exported hash
tables. Unknown or partially patched revisions fail before any input is written.
Re-running it on patched blobs does not write them again.

From the local `~/on7xelte` directory:

```sh
python3 device/camera/fixup_camera_blobs.py --check \
    vendor/proprietary/lib/libexynoscamera.so \
    vendor/proprietary/lib/libexynoscamera3.so
bash device/camera/tests/run_host_tests.sh
```

Remove `--check` only when intentionally applying the fixups to verified stock
blobs. `extract-files.sh` applies them after extraction and stops on an unknown
revision. Python 3.7+ is required. Host C++ tests require 32-bit multilib and
AddressSanitizer/UndefinedBehaviorSanitizer; `CXX=clang++` is also supported.
The host Android headers and constructor are mocks, used for behavior tests,
not proof of the actual Android class/HAL ABI.

Patched SHA-256 values:

| Library | SHA-256 |
| --- | --- |
| `libexynoscamera.so` | `36744663706cb3f96cb090bf8484fad3d12dc2a25968b8764edeea10b4f0c1de` |
| `libexynoscamera3.so` | `09ed013b203f65bae4d7065731f11b8f6872136fe3b35ae0e0b0155ed5dfae52` |

## Remote build checklist

1. Upload **both** local trees to `device/samsung/on7xelte` and
   `vendor/samsung/on7xelte` in the Android 13 build checkout. Include the new
   shim header, fixup script, and patched vendor binaries. Do not mix the new
   private-import blobs with an old shim.
2. Check that no other configuration overrides the legacy camera flag. After
   your normal environment setup and lunch selection, run:

   ```sh
   get_build_var TARGET_HAS_LEGACY_CAMERA_HAL1
   python3 device/samsung/on7xelte/camera/fixup_camera_blobs.py --check \
       vendor/samsung/on7xelte/proprietary/lib/libexynoscamera.so \
       vendor/samsung/on7xelte/proprietary/lib/libexynoscamera3.so
   m camera.universal7870 libexynoscamera_shim cameraserver
   ```

   The flag must print `false`; the shim's size/alignment assertions must pass
   against your real Android 13 headers. Do not remove those assertions to
   force a build through.
3. Run your normal **full-ROM build**. The changes affect framework/media
   service placement, mediaserver, init files, and the linker, not just the
   camera wrapper. Verify the resulting images include `cameraserver`, its init
   RC, both patched camera blobs, and the new 32-bit vendor shim. Inspect the
   shim's dynamic symbols with `readelf -Ws` or `llvm-readelf --dyn-syms`:
   `exynos_fence_ctor`, `exynos_fence_wait`, `exynos_fence_dtor`, and
   `exynos_graphicbuffer_ctor` must be exported. Both blobs must receive their
   camera-shim mappings as well as the existing OMX mappings.

## Device test checklist

After installing the rebuilt ROM through your normal process:

```sh
adb shell ps -A -o USER,UID,PID,NAME
adb shell cat /system/etc/init/cameraserver.rc
adb shell dumpsys media.camera
adb shell ls -l /vendor/lib/libexynoscamera_shim.so
adb shell sha256sum /system/lib/libexynoscamera.so /system/lib/libexynoscamera3.so
```

Check that `cameraserver` runs as UID 1047, allowed user IDs include the active
user (normally `0`), and new logs no longer show the UID 1013 CameraServiceProxy
rejection. Verify the two installed blob hashes match the table above.

Capture logs without clearing the existing buffers:

```sh
adb logcat -b all -v threadtime > camera-rebuild.log
```

While capturing, test native Camera2 and Open Camera with rear and front cameras:
preview, photo capture, video recording/playback, camera switching, and repeated
close/reopen. Stop logcat with Ctrl-C afterward. If a failure remains, save
`adb logcat -b crash -d` and `adb shell dumpsys media.camera` as separate files,
and note the app, selected camera/API, and failing operation. Do not treat the
host test results as confirmation that photo/video or the earlier vendor
crashes are resolved.
