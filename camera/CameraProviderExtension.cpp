/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CameraProviderExtension.h"

#include <android-base/logging.h>

#include <linux/videodev2.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <string>

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

/*
 * MediaTek flash LED drivers (drivers/misc/mediatek/flashlight/v4l2, AW36518
 * or LM3644 on MT6879) store the torch brightness in a 7 bit hardware
 * register, so the usable range is 0x00 - 0x7F. Bit 7 of the AW36518 register
 * is a hardware override bit and has to stay cleared, hence 0x7F is the
 * maximum strength we expose.
 *
 * Two backends are supported:
 *
 *  - V4L2_CID_FLASH_TORCH_INTENSITY of the flash LED subdevice. This is what
 *    the camera HAL uses itself and it needs no kernel change, so it is the
 *    default. The value is in uA, the range is queried from the driver.
 *  - the torch_brightness sysfs node added by camera/patches/kernel, used when
 *    a patched kernel is running (it keeps the level across torch off/on).
 */
static constexpr int32_t kTorchMaxStrengthLevel = 0x7F;
static constexpr int32_t kTorchDefaultStrengthLevel = 64;

static constexpr const char* kI2cDevicesPath = "/sys/bus/i2c/devices";
static constexpr const char* kTorchBrightnessNode = "torch_brightness";
static constexpr const char* kVideo4LinuxPath = "/sys/class/video4linux";
static constexpr const char* kDevPath = "/dev/";

/*
 * The DT of this platform can bind either the AW36518 or the LM3644 driver to
 * the flash LED, the subdevice name is "<driver name> <bus>-<address>":
 * "aw36518 6-0063" / "lm3644 6-0063".
 */
static constexpr const char* kTorchSubdevNames[] = {"aw36518", "lm3644"};

struct TorchInfo {
    bool valid = false;
    /* true: torch_brightness sysfs node, false: V4L2 subdevice devnode */
    bool useSysfs = false;
    std::string path;
    /* V4L2 control range, unused for the sysfs backend */
    int32_t minimum = 0;
    int32_t step = 1;
    int32_t maximum = 0;
    int32_t maxLevel = kTorchMaxStrengthLevel;
};

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

static bool readFirstLine(const std::string& path, std::string* out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    if (!std::getline(file, line)) {
        return false;
    }

    *out = line;
    return true;
}

static int32_t clampStrength(int32_t strength, int32_t maxLevel) {
    if (strength < 0) {
        return 0;
    }
    if (strength > maxLevel) {
        return maxLevel;
    }
    return strength;
}

/* ---------------------------------------------------------------- sysfs --- */

static bool discoverSysfs(TorchInfo* info, const std::string& root) {
    DIR* dir = opendir(root.c_str());
    if (dir == nullptr) {
        return false;
    }

    bool found = false;
    while (const dirent* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        std::string candidate = root + "/" + entry->d_name + "/" + kTorchBrightnessNode;
        if (access(candidate.c_str(), R_OK | W_OK) == 0) {
            info->path = candidate;
            info->useSysfs = true;
            info->valid = true;
            info->maxLevel = kTorchMaxStrengthLevel;
            found = true;
            break;
        }
    }

    closedir(dir);
    return found;
}

/* ----------------------------------------------------------------- v4l2 --- */

static bool isTorchSubdev(const std::string& name) {
    for (const char* prefix : kTorchSubdevNames) {
        if (name.compare(0, strlen(prefix), prefix) == 0) {
            return true;
        }
    }
    return false;
}

static bool queryTorchControl(const std::string& node, TorchInfo* info) {
    int fd = open(node.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        LOG(ERROR) << "Failed to open " << node << ": " << strerror(errno);
        return false;
    }

    struct v4l2_queryctrl query = {};
    query.id = V4L2_CID_FLASH_TORCH_INTENSITY;

    bool ok = ioctl(fd, VIDIOC_QUERYCTRL, &query) == 0;
    int error = errno;
    close(fd);

    if (!ok) {
        LOG(ERROR) << node << " has no torch intensity control: " << strerror(error);
        return false;
    }

    if (query.flags & V4L2_CTRL_FLAG_DISABLED) {
        LOG(ERROR) << node << " torch intensity control is disabled";
        return false;
    }

    info->minimum = query.minimum;
    info->maximum = query.maximum;
    info->step = query.step > 0 ? query.step : 1;

    int64_t levels = (info->maximum - info->minimum) / info->step;
    info->maxLevel = levels > kTorchMaxStrengthLevel ? kTorchMaxStrengthLevel
                                                    : static_cast<int32_t>(levels);

    /* A single level means there is nothing to control. */
    return info->maxLevel > 1;
}

static bool discoverV4l2(TorchInfo* info, const std::string& root) {
    DIR* dir = opendir(root.c_str());
    if (dir == nullptr) {
        return false;
    }

    bool found = false;
    while (const dirent* entry = readdir(dir)) {
        if (strncmp(entry->d_name, "v4l-subdev", strlen("v4l-subdev")) != 0) {
            continue;
        }

        std::string name;
        if (!readFirstLine(root + "/" + entry->d_name + "/name", &name)) {
            continue;
        }

        if (!isTorchSubdev(name)) {
            continue;
        }

        std::string node = std::string(kDevPath) + entry->d_name;
        if (queryTorchControl(node, info)) {
            LOG(INFO) << "Using " << node << " (" << name << ") for torch strength";
            info->path = node;
            info->useSysfs = false;
            info->valid = true;
            found = true;
            break;
        }
    }

    closedir(dir);
    return found;
}

static const TorchInfo& torchInfo() {
    static TorchInfo info = []() {
        TorchInfo discovered;

        if (discoverSysfs(&discovered, kI2cDevicesPath)) {
            LOG(INFO) << "Using " << discovered.path << " for torch strength";
        } else if (!discoverV4l2(&discovered, kVideo4LinuxPath)) {
            LOG(ERROR) << "No torch strength interface found";
            discovered = TorchInfo();
        }

        return discovered;
    }();

    return info;
}

/* -------------------------------------------------------------- backend --- */

static bool applyLevel(const TorchInfo& info, int32_t level) {
    if (info.useSysfs) {
        return writeValue(info.path, level);
    }

    int fd = open(info.path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        LOG(ERROR) << "Failed to open " << info.path << ": " << strerror(errno);
        return false;
    }

    struct v4l2_control control = {};
    control.id = V4L2_CID_FLASH_TORCH_INTENSITY;
    control.value = info.minimum + level * info.step;
    if (control.value > info.maximum) {
        control.value = info.maximum;
    }

    bool ok = ioctl(fd, VIDIOC_S_CTRL, &control) == 0;
    int error = errno;
    close(fd);

    if (!ok) {
        LOG(ERROR) << "Failed to set torch intensity on " << info.path << ": "
                   << strerror(error);
        return false;
    }

    return true;
}

/* ------------------------------------------------------------------ api --- */

bool supportsTorchStrengthControlExt() {
    const TorchInfo& info = torchInfo();

    if (!info.valid) {
        return false;
    }

    /* The sysfs node can disappear when the module is reloaded. */
    if (info.useSysfs) {
        return access(info.path.c_str(), R_OK | W_OK) == 0;
    }

    return true;
}

int32_t getTorchDefaultStrengthLevelExt() {
    const TorchInfo& info = torchInfo();

    return clampStrength(kTorchDefaultStrengthLevel, info.maxLevel);
}

int32_t getTorchMaxStrengthLevelExt() {
    return torchInfo().maxLevel;
}

int32_t getTorchStrengthLevelExt() {
    const TorchInfo& info = torchInfo();
    int32_t strength = clampStrength(gTorchStrengthLevel.load(), info.maxLevel);

    if (!info.useSysfs || !gTorchEnabled.load()) {
        return strength;
    }

    /* While the torch is on, the driver knows the brightness in use. */
    int32_t hardwareStrength = 0;
    if (readValue(info.path, &hardwareStrength)) {
        hardwareStrength = clampStrength(hardwareStrength, info.maxLevel);
        if (hardwareStrength > 0) {
            strength = hardwareStrength;
            gTorchStrengthLevel.store(hardwareStrength);
        }
    }

    return strength;
}

void setTorchStrengthLevelExt(int32_t torchStrength, bool enabled) {
    const TorchInfo& info = torchInfo();

    if (!info.valid) {
        LOG(ERROR) << "Torch brightness interface is not available";
        return;
    }

    const int32_t strength = enabled ? clampStrength(torchStrength, info.maxLevel) : 0;

    /*
     * A strength of 0 means "no strength requested": the sysfs node has to be
     * cleared so the kernel drops its override, while for the V4L2 backend the
     * HAL keeps full control as long as we do not touch the control.
     */
    if ((info.useSysfs || strength > 0) && !applyLevel(info, strength)) {
        LOG(ERROR) << "Failed to set torch strength level to " << strength;
        return;
    }

    gTorchEnabled.store(enabled);
    if (enabled && strength > 0) {
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
