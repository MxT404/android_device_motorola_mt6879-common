# Torch brightness control for Motorola MT6879 (manaus)

> The kernel patch below is **optional** - torch strength works on the stock
> kernel through the V4L2 flash subdevice. Only the two ROM patches are needed.

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

## 1. Kernel: `kernel/motorola/mt6879` (OPTIONAL)

Not required. The stock kernel already exposes `V4L2_CID_FLASH_TORCH_INTENSITY`
on the flash LED subdevice and the device side uses that (see
`camera/CameraProviderExtension.cpp`), so torch strength works without touching
the kernel.

If you do rebuild the kernel, this patch adds a `torch_brightness` sysfs node to
both V4L2 flash drivers (`aw36518.c` and `lm3644.c`) which is then preferred by
the extension because it keeps the level across torch off/on cycles:

```
cd kernel/motorola/mt6879
git am /path/to/device/motorola/mt6879-common/camera/patches/kernel/0001-*.patch
```

```
/sys/bus/i2c/devices/<bus>-0063/torch_brightness      # 6-0063 on manaus
```

It accepts the raw 7 bit (0x00 - 0x7F) register value, applies it live while the
torch is on and keeps it for later torch off/on cycles. `0` drops the override
and hands the torch back to the camera HAL.

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

Windows `cmd` does not understand single quotes. Quote the `adb shell` argument
with double quotes so that pipes run on the phone (Android's toybox has `grep`),
and only use `findstr` for host side output such as `adb logcat`:

```bat
adb root
rem which driver bound and how the devnode is named/labelled
adb shell "ls -l /sys/bus/i2c/devices/6-0063/driver"
adb shell "cat /sys/class/video4linux/v4l-subdev*/name"
adb shell "ls -lZ /dev/v4l-subdev*"
rem if the optional kernel patch is applied, the node is also there
adb shell ls /sys/bus/i2c/devices/*/torch_brightness
rem turn the torch on from the quick settings tile, then check the extension
adb logcat -d -s cameraserver | findstr /i torch
```

The cameraserver log line `Using /dev/v4l-subdevN (aw36518 6-0063) for torch
strength` proves the extension found the flash LED. Long press the flashlight
tile afterwards, the brightness slider should show up and dim the torch.

## Troubleshooting

Nothing in `adb logcat -d -s cameraserver | findstr /i torch`:

1. The extension was not linked in, i.e. the `frameworks/av` +
   `vendor/lineage` patches from section 2 are missing.
2. The subdevice was not found. Check the name and the permissions:
   `adb shell "cat /sys/class/video4linux/v4l-subdev*/name"` must contain
   `aw36518 6-0063` (or `lm3644 6-0063`), and
   `adb shell "ls -lZ /dev/v4l-subdev*"` must show mode `0666` with the
   `mtk_v4l2_media_device` label. Fix the label with the
   `sepolicy/vendor/cameraserver.te` rules and the mode with
   `init/ueventd.mt6879.rc` from this device tree.

Messages like `Failed to open /dev/v4l-subdevN: Permission denied` mean DAC or
SELinux is still blocking, an `avc: denied` for `mtk_v4l2_media_device` shows up
in `adb shell "dmesg | grep avc"`.

Node permissions are handled by this device tree:

* `init/init.mt6879.rc` - `chown cameraserver cameraserver` + `chmod 0660` on
  `/sys/bus/i2c/devices/6-0063/torch_brightness`
* `sepolicy/vendor/genfs_contexts` - `vendor_sysfs_torch` label
* `sepolicy/vendor/cameraserver.te` - `allow cameraserver vendor_sysfs_torch:file ...`
