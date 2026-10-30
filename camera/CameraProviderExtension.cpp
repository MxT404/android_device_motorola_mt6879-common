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

#include <dirent.h>
#include <unistd.h>

/*
 * MediaTek V4L2 flash LED drivers (drivers/misc/mediatek/flashlight/v4l2,
 * AW36518 and LM3644 on MT6879).
 *
 * They store the torch brightness in a 7 bit hardware register, so the usable
 * range is 0x00 - 0x7F. Bit 7 of the AW36518 register is a hardware override
 * bit and must stay cleared, hence 0x7F is the maximum strength we expose.
 *
 * torch_brightness is an attribute of the I2C client of the flash LED, i.e.
 * /sys/bus/i2c/devices/<bus>-<address>/torch_brightness. Which of the two
 * drivers ends up bound depends on the device tree (the manaus overlay uses
 * aw36518@63 while the base mt6879 dtsi has lm3644@63), so look the node up
 * instead of hardcoding bus and address.
 */
static constexpr const char* kI2cDevicesPath = "/sys/bus/i2c/devices";
static constexpr const char* kTorchBrightnessNode = "torch_brightness";

static constexpr int32_t kTorchMaxStrengthLevel = 0x7F;

/*
 * 64 translates to 97390 uA, roughly half of the 7 bit range. It is a safe
 * default for the slider which still leaves room in both directions, the
 * driver falls back to its own 25000 uA when no strength is requested.
 */
static constexpr int32_t kTorchDefaultStrengthLevel = 64;

static std::atomic<bool> gTorchEnabled{false};
static std::atomic<int32_t> gTorchStrengthLevel{kTorchDefaultStrengthLevel};

static const std::string& torchBrightnessPath() {
    static std::string path;

    if (!path.empty()) {
        return path;
    }

    DIR* dir = opendir(kI2cDevicesPath);
    if (dir == nullptr) {
        return path;
    }

    while (const dirent* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        std::string candidate = std::string(kI2cDevicesPath) + "/" + entry->d_name + "/" +
                                kTorchBrightnessNode;
        if (access(candidate.c_str(), R_OK | W_OK) == 0) {
            path = candidate;
            break;
        }
    }

    closedir(dir);
    return path;
}

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
    const std::string& path = torchBrightnessPath();

    return !path.empty() && access(path.c_str(), R_OK | W_OK) == 0;
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

        if (readValue(torchBrightnessPath(), &hardwareStrength)) {
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
    const std::string& path = torchBrightnessPath();

    if (path.empty()) {
        LOG(ERROR) << "Torch brightness node is not available";
        return;
    }

    /* Writing 0 drops the override and lets the HAL drive the torch again. */
    const int32_t strength = enabled ? clampStrength(torchStrength) : 0;

    if (!writeValue(path, strength)) {
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
