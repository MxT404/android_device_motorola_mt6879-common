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
MediaTek V4L2 flash driver that the camera HAL talks to through
`V4L2_CID_FLASH_TORCH_INTENSITY`. Torch strength is implemented the same way as
on other devices:

* `camera/CameraProviderExtension.cpp` implements `supportsTorchStrengthControlExt()`
  and friends and writes the 7 bit torch brightness register through the
  `torch_brightness` node of the flash LED I2C client. The node is looked up
  under `/sys/bus/i2c/devices/`, so it works with either flash driver that can
  be bound on this platform (`aw36518@63` from the manaus overlay or
  `lm3644@63` from the base dtsi).
* `camera/Android.bp` builds it as `libcameraservice_extension.mt6879`, which
  `mt6879.mk` links into `libcameraservice` with
  `soong_config_set,libcameraservice,ext_lib`.
* `init/init.mt6879.rc` and `sepolicy/vendor` give `cameraserver` access to the
  sysfs node.

Two things live outside of this repository and are provided as patches in
`camera/patches/` (see the README there):

| Repository | Patch | Needed for |
| --- | --- | --- |
| `kernel/motorola/mt6879` | `camera/patches/kernel/0001-*.patch` | the `torch_brightness` sysfs node itself |
| `frameworks/av` | `camera/patches/frameworks_av/0001-*.patch` | the libcameraservice extension hook (ROMs that do not already have it) |
| `vendor/lineage` | `camera/patches/vendor_lineage/0001-*.patch` | the `libcameraservice` `ext_lib` soong config type |

Without the kernel patch the sysfs node is missing, `supportsTorchStrengthControlExt()`
returns `false` and the flashlight simply stays single level, i.e. nothing else
breaks.

Verification on a booted device:

```sh
# Windows cmd: use double quotes so the pipe runs on the device (toybox grep)
adb root
adb shell ls /sys/bus/i2c/devices/                 # the flash LED is one of these
adb shell ls /sys/bus/i2c/devices/*/torch_brightness
adb shell "cat /sys/bus/i2c/devices/6-0063/torch_brightness"   # 0x00-0x7F, 0 = HAL default
adb shell "echo 32 > /sys/bus/i2c/devices/6-0063/torch_brightness"   # while torch is on
adb shell "dmesg | grep -i torch"                # "torch brightness control enabled"
```

## Dependencies

* [Common Vendor Blobs](https://github.com/motorola-manaus/proprietary_vendor_motorola_mt6879-common): `vendor/motorola/mt6879-common`
* [Kernel Source](https://github.com/motorola-manaus/android_kernel_motorola_mt6879): `kernel/motorola/mt6879`
* [MediaTek SEPolicy](https://github.com/motorola-manaus/device_mediatek_sepolicy_vndr): `device/mediatek/sepolicy_vndr`
* [MediaTek Hardware](https://github.com/motorola-manaus/hardware_mediatek): `hardware/mediatek`

## License

* SPDX-License-Identifier: Apache-2.0
