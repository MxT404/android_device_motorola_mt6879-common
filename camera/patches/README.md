# Torch brightness control for Motorola MT6879 (manaus)

This directory contains the patches that are needed outside of this device tree
to get the flashlight strength slider working. The device side (the
`CameraProviderExtension` implementation in `camera/`) is already part of this
repository, `soong_config_set,libcameraservice,ext_lib` in `mt6879.mk` links it
into `libcameraservice`.

The flashlight of the Edge 40 Neo is an LED driver on `i2c-6` at address `0x63`.
Two nodes exist in the DT for that address - `aw36518@63` (`mediatek,aw36518`,
added by `cust_mt6879_camera_v4l2_manaus.dtsi`) and `lm3644@63`
(`mediatek,lm3644`, from the base `cust_mt6879_camera_v4l2.dtsi`) - and both
drivers are in `modules.load`, so the patch covers both. The camera HAL
(`libcam.hal3a.v3.flash.v4l2.so`) drives whichever one bound through
`V4L2_CID_FLASH_LED_MODE` / `V4L2_CID_FLASH_TORCH_INTENSITY`.

## 1. Kernel: `kernel/motorola/mt6879`

```
cd kernel/motorola/mt6879
git am /path/to/device/motorola/mt6879-common/camera/patches/kernel/0001-*.patch
```

`drivers/misc/mediatek/flashlight/v4l2/aw36518.c` and
`drivers/misc/mediatek/flashlight/v4l2/lm3644.c` gain a `torch_brightness` sysfs
node on the I2C client of the flash LED:

```
/sys/bus/i2c/devices/<bus>-0063/torch_brightness      # 6-0063 on manaus
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

Windows `cmd` does not understand single quotes and has no `grep`. Quote the
shell command with double quotes so that the pipe is executed on the device
(Android's toybox provides `grep`):

```bat
adb root
adb shell ls /sys/bus/i2c/devices/
adb shell ls /sys/bus/i2c/devices/*/torch_brightness
adb shell "ls -l /sys/bus/i2c/devices/6-0063/driver"     # which driver bound
rem value is the last applied level, 0 when the camera HAL has full control
adb shell "cat /sys/bus/i2c/devices/6-0063/torch_brightness"
rem turn the torch on from the quick settings tile, then dim it live
adb shell "echo 16 > /sys/bus/i2c/devices/6-0063/torch_brightness"
adb shell "echo 127 > /sys/bus/i2c/devices/6-0063/torch_brightness"
```

Long press the flashlight tile afterwards, the brightness slider should show up
and dim the torch while it is on.

## Troubleshooting

`ls /sys/bus/i2c/devices/*/torch_brightness` finds nothing:

1. The running kernel does not contain the patch. Verify with
   `adb shell "dmesg | grep -i torch"` - a patched kernel prints
   `torch brightness control enabled` while the flash driver probes. If the
   line is missing, apply `camera/patches/kernel/0001-*.patch` to
   `kernel/motorola/mt6879` and rebuild (the drivers are modules, so both the
   boot image and `vendor_dlkm` need to be flashed).
2. Check which driver bound: `adb shell ls -l /sys/bus/i2c/devices/6-0063/driver`.
   Both `aw36518` and `lm3644` are covered, any other driver is not.

Node exists but there is no slider in the UI:

* `frameworks/av` / `vendor/lineage` patches missing (see section 2) -
  `adb logcat -d -s cameraserver | findstr /i torch` reports
  `Torch brightness node is not available` only when the node itself cannot be
  opened, otherwise no torch message appears at all.
  (`adb logcat` runs on the host, so `findstr` is correct there.)
* SELinux/DAC: check `adb shell "ls -lZ /sys/bus/i2c/devices/6-0063/torch_brightness"`,
  it must be `u:object_r:vendor_sysfs_torch:s0` and `cameraserver cameraserver`.
  A denial shows up as `avc: denied` in `adb shell dmesg`.

Node permissions are handled by this device tree:

* `init/init.mt6879.rc` - `chown cameraserver cameraserver` + `chmod 0660` on
  `/sys/bus/i2c/devices/6-0063/torch_brightness`
* `sepolicy/vendor/genfs_contexts` - `vendor_sysfs_torch` label
* `sepolicy/vendor/cameraserver.te` - `allow cameraserver vendor_sysfs_torch:file ...`
