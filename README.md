# Motorola MT6879 Common Device Tree

This repository contains the common device tree and platform definitions for Motorola devices based on the **MediaTek Dimensity 7020 (MT6879)** SoC.

## Supported Devices

* Motorola Edge 40 Neo (`manaus`)
* Motorola Moto G54 / G64 / G84 5G variants (`cancun`, `tesla`)

## Features & Subsystems

* **Audio:** Hi-Res Audio with hardware-matched offload policy, native support for LHDC v3/v5, LDAC, and aptX encoded formats.
* **Camera:** Camera provider HAL configs and tuning libraries for 50MP OIS sensor pipeline.
* **Flashlight:** Torch brightness (strength) control for the AW36518 flash LED.
* **Biometrics:** Under-display fingerprint sensor (UDFPS optical handler).
* **Display:** 144Hz refresh rate framework configs and Dimensity display color enhancement.
* **Connectivity:** WiFi 6 (802.11ax), Bluetooth 5.4, NXP NFC (SN220) with eSE.
* **SEPolicy:** SELinux policies tailored for MediaTek Android 15/17 VNDK and GKI compatibility.

## Directory Path in Source Tree

```bash
device/motorola/mt6879-common
```

## Torch brightness control

The Edge 40 Neo uses an **AW36518** flash LED on `i2c-6` (`0x63`), driven by the
MediaTek V4L2 flash driver; the camera HAL talks to it through
`V4L2_CID_FLASH_TORCH_INTENSITY` (7 bit register, `0x00` - `0x7F`).

**No kernel change is needed**, the stock kernel already exposes the control:

* `camera/CameraProviderExtension.cpp` implements `supportsTorchStrengthControlExt()`
  and friends. It finds the flash LED subdevice under `/sys/class/video4linux`
  (`aw36518 6-0063` or `lm3644 6-0063`, depending on which driver bound), asks
  it for the `V4L2_CID_FLASH_TORCH_INTENSITY` range and writes the level with
  `VIDIOC_S_CTRL` - the same interface the camera HAL uses itself. If a patched
  kernel with the `torch_brightness` sysfs node is running, that node is used
  instead (it keeps the level across torch off/on).
* `camera/Android.bp` builds it as `libcameraservice_extension.mt6879`, which
  `mt6879.mk` links into `libcameraservice` with
  `soong_config_set,libcameraservice,ext_lib`.
* `init/ueventd.mt6879.rc` makes `/dev/v4l-subdev*` reachable (0666, SELinux
  still restricts it) and `sepolicy/vendor/cameraserver.te` allows `cameraserver`
  to use `mtk_v4l2_media_device`.

The UI (the strength slider behind the flashlight tile) needs the
`libcameraservice` extension hook on the ROM side. LineageOS does not ship it,
so apply the two patches from `camera/patches/` (details in its README):

| Repository | Patch | Needed for |
| --- | --- | --- |
| `frameworks/av` | `camera/patches/frameworks_av/0001-*.patch` | the libcameraservice extension hook (skip if the ROM already has `CameraProviderExtension.h`) |
| `vendor/lineage` | `camera/patches/vendor_lineage/0001-*.patch` | the `libcameraservice` `ext_lib` soong config type (same) |
| `kernel/motorola/mt6879` | `camera/patches/kernel/0001-*.patch` | **optional**, only adds the `torch_brightness` sysfs node |

Verification on a booted device (Windows cmd: quote the `adb shell` argument so
pipes run on the phone):

```bat
adb root
adb shell "ls /sys/class/video4linux/"
adb shell "cat /sys/class/video4linux/v4l-subdev*/name"     # "aw36518 6-0063"
adb shell "ls -lZ /dev/v4l-subdev*"                        # mtk_v4l2_media_device, 0666
adb logcat -d -s cameraserver | findstr /i torch          # "Using /dev/v4l-subdevN (aw36518 6-0063) for torch strength"
```

Long press the flashlight tile with the torch on, the slider should dim it.

## Dependencies

* [Common Vendor Blobs](https://github.com/motorola-manaus/proprietary_vendor_motorola_mt6879-common): `vendor/motorola/mt6879-common`
* [Kernel Source](https://github.com/motorola-manaus/android_kernel_motorola_mt6879): `kernel/motorola/mt6879`
* [MediaTek SEPolicy](https://github.com/motorola-manaus/device_mediatek_sepolicy_vndr): `device/mediatek/sepolicy_vndr`
* [MediaTek Hardware](https://github.com/motorola-manaus/hardware_mediatek): `hardware/mediatek`

## License

* SPDX-License-Identifier: Apache-2.0
