# Torch brightness control for Motorola MT6879 (manaus / AW36518)

This directory contains the patches that are needed outside of this device tree
to get the flashlight strength slider working. The device side (the
`CameraProviderExtension` implementation in `camera/`) is already part of this
repository, `soong_config_set,libcameraservice,ext_lib` in `mt6879.mk` links it
into `libcameraservice`.

The flashlight of the Edge 40 Neo is an **AW36518** LED driver
(`mediatek,aw36518` in `cust_mt6879_camera_v4l2_manaus.dtsi`) handled by the
V4L2 flash driver, `libcam.hal3a.v3.flash.v4l2.so` in the camera HAL talks to
it through `V4L2_CID_FLASH_LED_MODE` / `V4L2_CID_FLASH_TORCH_INTENSITY`.

## 1. Kernel: `kernel/motorola/mt6879`

```
cd kernel/motorola/mt6879
git am /path/to/device/motorola/mt6879-common/camera/patches/kernel/0001-*.patch
```

`drivers/misc/mediatek/flashlight/v4l2/aw36518.c` gains a `torch_brightness`
sysfs node on the I2C client of the flash LED:

```
/sys/bus/i2c/devices/6-0063/torch_brightness
```

It accepts the raw 7 bit (0x00 - 0x7F) torch brightness register value, applies
it live while the torch is on and keeps it for later torch on/off cycles.
`0` drops the override and hands the torch back to the camera HAL.

## 2. ROM: `frameworks/av` and `vendor/lineage`

The slider is driven by `CameraManager.turnOnTorchWithStrengthLevel()`, which
only works when the camera provider advertises
`ANDROID_FLASH_INFO_STRENGTH_MAXIMUM_LEVEL > 1`. The MediaTek camera provider is
closed source and does not, so `libcameraservice` has to be extended, exactly
like on other devices using this feature (crDroid, YAAP, LMODroid, AOSPA...).

**Check first** whether your ROM already ships the extension:

```
ls frameworks/av/services/camera/libcameraservice/common/CameraProviderExtension.h
```

If the file exists and `grep -r soong_config_set,libcameraservice` in your
device trees/vendor shows `ext_lib`, you are done, skip this step.

Otherwise apply both patches (they were generated against `lineage-24.0`,
`git apply --check` passes on that tree):

```
cd frameworks/av
git am /path/to/device/motorola/mt6879-common/camera/patches/frameworks_av/0001-*.patch

cd vendor/lineage
git am /path/to/device/motorola/mt6879-common/camera/patches/vendor_lineage/0001-*.patch
```

* `frameworks_av` adds `CameraProviderExtension.{h,cpp}` (weak default
  implementations), compiles the default implementation into
  `libcameraservice`, and lets `CameraProviderManager` use the extension for
  `turnOnTorchWithStrengthLevel`, `setTorchMode`, `getTorchStrengthLevel`,
  `getTorchDefaultStrengthLevel` and for advertising
  `FLASH_INFO_STRENGTH_{MAXIMUM,DEFAULT}_LEVEL` in the camera characteristics.
* `vendor_lineage` adds the `libcameraservice` `ext_lib` soong config type that
  whole-archive links the device library (`libcameraservice_extension.mt6879`)
  into `libcameraservice`, overriding the weak defaults.

## Verification

```sh
adb root
# node exists, value is the last applied level (0 when the HAL has full control)
adb shell cat /sys/bus/i2c/devices/6-0063/torch_brightness

# turn the torch on from the quick settings tile, then dim it live
adb shell 'echo 16  > /sys/bus/i2c/devices/6-0063/torch_brightness'
adb shell 'echo 127 > /sys/bus/i2c/devices/6-0063/torch_brightness'
```

Long press the flashlight tile afterwards, the brightness slider should show up
and dim the torch while it is on. `logcat -s cameraserver` reports
`Torch brightness node is not available` when the node is missing (kernel
without the patch, or the permissions below were not applied).

Node permissions are handled by this device tree:

* `init/init.mt6879.rc` - `chown cameraserver cameraserver` + `chmod 0660`
* `sepolicy/vendor/genfs_contexts` - `vendor_sysfs_torch` label
* `sepolicy/vendor/cameraserver.te` - `allow cameraserver vendor_sysfs_torch:file ...`
