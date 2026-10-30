/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CameraProviderExtension.h"

#include <android-base/logging.h>

#include <atomic>
#include <fstream>
#include <string>

#include <unistd.h>

/*
 * MediaTek AW36518 flash LED driver (drivers/misc/mediatek/flashlight/v4l2).
 *
 * The driver stores the torch brightness in a 7 bit hardware register, so the
 * usable range is 0x00 - 0x7F. Bit 7 of that register is a hardware override
 * bit and must stay cleared, hence 0x7F is the maximum strength we expose.
 *
 * The node is an attribute of the I2C client, which is instantiated from
 * &i2c6 in cust_mt6879_camera_v4l2_manaus.dtsi, hence the stable
 * <bus>-<address> path below.
 */
static constexpr const char* kTorchBrightnessPath =
        "/sys/bus/i2c/devices/6-0063/torch_brightness";

static constexpr int32_t kTorchMaxStrengthLevel = 0x7F;

/*
 * 64 translates to 97390 uA, roughly half of the 7 bit range. It is a safe
 * default for the slider which still leaves room in both directions, the
 * driver falls back to its own 25000 uA when no strength is requested.
 */
static constexpr int32_t kTorchDefaultStrengthLevel = 64;

static std::atomic<bool> gTorchEnabled{false};
static std::atomic<int32_t> gTorchStrengthLevel{kTorchDefaultStrengthLevel};

template <typename T>
static bool writeValue(const std::string& path, const T& value) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    file << value;
    return file.good();
}

template <typename T>
static bool readValue(const std::string& path, T* out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    T value;
    file >> value;
    if (file.fail()) {
        return false;
    }

    *out = value;
    return true;
}

static int32_t clampStrength(int32_t strength) {
    if (strength < 0) {
        return 0;
    }
    if (strength > kTorchMaxStrengthLevel) {
        return kTorchMaxStrengthLevel;
    }
    return strength;
}

bool supportsTorchStrengthControlExt() {
    return access(kTorchBrightnessPath, R_OK | W_OK) == 0;
}

int32_t getTorchDefaultStrengthLevelExt() {
    return kTorchDefaultStrengthLevel;
}

int32_t getTorchMaxStrengthLevelExt() {
    return kTorchMaxStrengthLevel;
}

int32_t getTorchStrengthLevelExt() {
    int32_t strength = gTorchStrengthLevel.load();

    /* While the torch is on, the driver knows the brightness in use. */
    if (gTorchEnabled.load()) {
        int32_t hardwareStrength = 0;

        if (readValue(kTorchBrightnessPath, &hardwareStrength)) {
            hardwareStrength = clampStrength(hardwareStrength);
            if (hardwareStrength > 0) {
                strength = hardwareStrength;
                gTorchStrengthLevel.store(hardwareStrength);
            }
        }
    }

    return strength;
}

void setTorchStrengthLevelExt(int32_t torchStrength, bool enabled) {
    if (!supportsTorchStrengthControlExt()) {
        LOG(ERROR) << "Torch brightness node is not available";
        return;
    }

    /* Writing 0 drops the override and lets the HAL drive the torch again. */
    const int32_t strength = enabled ? clampStrength(torchStrength) : 0;

    if (!writeValue(kTorchBrightnessPath, strength)) {
        LOG(ERROR) << "Failed to set torch strength level to " << strength;
        return;
    }

    gTorchEnabled.store(enabled);
    if (enabled) {
        gTorchStrengthLevel.store(strength);
    }
}

/* Variant of the extension used by ROMs without the 'enabled' argument. */
void setTorchStrengthLevelExt(int32_t torchStrength) {
    setTorchStrengthLevelExt(torchStrength, torchStrength > 0);
}

bool supportsSetTorchModeExt() {
    return false;
}

void setTorchModeExt(bool enabled) {
    setTorchStrengthLevelExt(kTorchDefaultStrengthLevel, enabled);
}
