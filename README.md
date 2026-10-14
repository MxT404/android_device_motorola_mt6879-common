# Motorola MT6879 Common Device Tree

This repository contains the common device tree and platform definitions for Motorola devices based on the **MediaTek Dimensity 7020 (MT6879)** SoC.

## Supported Devices

* Motorola Edge 40 Neo (`manaus`)
* Motorola Moto G54 / G64 / G84 5G variants (`cancun`, `tesla`)

## Features & Subsystems

* **Audio:** Hi-Res Audio with hardware-matched offload policy, native support for LHDC v3/v5, LDAC, and aptX encoded formats.
* **Camera:** Camera provider HAL configs and tuning libraries for 50MP OIS sensor pipeline.
* **Biometrics:** Under-display fingerprint sensor (UDFPS optical handler).
* **Display:** 144Hz refresh rate framework configs and Dimensity display color enhancement.
* **Connectivity:** WiFi 6 (802.11ax), Bluetooth 5.4, NXP NFC (SN220) with eSE.
* **SEPolicy:** SELinux policies tailored for MediaTek Android 15/17 VNDK and GKI compatibility.

## Directory Path in Source Tree

```bash
device/motorola/mt6879-common
```

## Dependencies

* [Common Vendor Blobs](https://github.com/motorola-manaus/proprietary_vendor_motorola_mt6879-common): `vendor/motorola/mt6879-common`
* [Kernel Source](https://github.com/motorola-manaus/android_kernel_motorola_mt6879): `kernel/motorola/mt6879`
* [MediaTek SEPolicy](https://github.com/motorola-manaus/device_mediatek_sepolicy_vndr): `device/mediatek/sepolicy_vndr`
* [MediaTek Hardware](https://github.com/motorola-manaus/hardware_mediatek): `hardware/mediatek`

## License

* SPDX-License-Identifier: Apache-2.0
