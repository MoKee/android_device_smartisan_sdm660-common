/*
 * Copyright (C) 2018 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "LightService"

#include "Light.h"

#include <android-base/logging.h>
#include <fstream>

namespace {
using android::hardware::light::V2_0::LightState;

const static std::string kLcdBacklightPath = "/sys/class/leds/lcd-backlight/brightness";
const static std::string kLcdMaxBacklightPath = "/sys/class/leds/lcd-backlight/max_brightness";
const static std::string kRedLedPath = "/sys/class/leds/red/brightness";
const static std::string kGreenLedPath = "/sys/class/leds/green/brightness";
const static std::string kBlueLedPath = "/sys/class/leds/blue/brightness";
const static std::string kRedDutyPctsPath = "/sys/class/leds/red/duty_pcts";
const static std::string kGreenDutyPctsPath = "/sys/class/leds/green/duty_pcts";
const static std::string kBlueDutyPctsPath = "/sys/class/leds/blue/duty_pcts";
const static std::string kRedStartIdxPath = "/sys/class/leds/red/start_idx";
const static std::string kGreenStartIdxPath = "/sys/class/leds/green/start_idx";
const static std::string kBlueStartIdxPath = "/sys/class/leds/blue/start_idx";
const static std::string kRedPauseLoPath = "/sys/class/leds/red/pause_lo";
const static std::string kGreenPauseLoPath = "/sys/class/leds/green/pause_lo";
const static std::string kBluePauseLoPath = "/sys/class/leds/blue/pause_lo";
const static std::string kRedPauseHiPath = "/sys/class/leds/red/pause_hi";
const static std::string kGreenPauseHiPath = "/sys/class/leds/green/pause_hi";
const static std::string kBluePauseHiPath = "/sys/class/leds/blue/pause_hi";
const static std::string kRedRampStepMsPath = "/sys/class/leds/red/ramp_step_ms";
const static std::string kGreenRampStepMsPath = "/sys/class/leds/green/ramp_step_ms";
const static std::string kBlueRampStepMsPath = "/sys/class/leds/blue/ramp_step_ms";
const static std::string kRedBlinkPath = "/sys/class/leds/red/blink";
const static std::string kGreenBlinkPath = "/sys/class/leds/green/blink";
const static std::string kBlueBlinkPath = "/sys/class/leds/blue/blink";
const static std::string kRgbBlinkPath = "/sys/class/leds/rgb/rgb_blink";

static constexpr int RAMP_SIZE = 8;
static constexpr int RAMP_STEP_DURATION = 50;

static constexpr int BRIGHTNESS_RAMP[RAMP_SIZE] = {0, 12, 25, 37, 50, 72, 85, 100};
static constexpr int DEFAULT_MAX_BRIGHTNESS = 255;

static uint32_t rgbToBrightness(const LightState& state) {
    uint32_t color = state.color & 0x00ffffff;
    return ((77 * ((color >> 16) & 0xff)) + (150 * ((color >> 8) & 0xff)) +
            (29 * (color & 0xff))) >> 8;
}

static bool isLit(const LightState& state) {
    return (state.color & 0x00ffffff);
}

static std::string getScaledDutyPcts(int brightness) {
    std::string buf, pad;

    for (auto i : BRIGHTNESS_RAMP) {
        buf += pad;
        buf += std::to_string(i * brightness / 255);
        pad = ",";
    }

    return buf;
}
}  // anonymous namespace

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

/*
 * Write value to path and close file.
 */
template <typename T>
static void set(const std::string& path, const T& value) {
    std::ofstream file(path);
    file << value;
}

template <typename T>
static T get(const std::string& path, const T& def) {
    std::ifstream file(path);
    T result;

    file >> result;
    return file.fail() ? def : result;
}

Light::Light() {
    mPanelMaxBrightness = get(kLcdMaxBacklightPath, DEFAULT_MAX_BRIGHTNESS);

    auto attnFn(std::bind(&Light::setAttentionLight, this, std::placeholders::_1));
    auto backlightFn(std::bind(&Light::setLcdBacklight, this, std::placeholders::_1));
    auto batteryFn(std::bind(&Light::setBatteryLight, this, std::placeholders::_1));
    auto notifFn(std::bind(&Light::setNotificationLight, this, std::placeholders::_1));
    mLights.emplace(std::make_pair(Type::ATTENTION, attnFn));
    mLights.emplace(std::make_pair(Type::BACKLIGHT, backlightFn));
    mLights.emplace(std::make_pair(Type::BATTERY, batteryFn));
    mLights.emplace(std::make_pair(Type::NOTIFICATIONS, notifFn));
}

// Methods from ::android::hardware::light::V2_0::ILight follow.
Return<Status> Light::setLight(Type type, const LightState& state) {
    auto it = mLights.find(type);

    if (it == mLights.end()) {
        return Status::LIGHT_NOT_SUPPORTED;
    }

    it->second(state);

    return Status::SUCCESS;
}

Return<void> Light::getSupportedTypes(getSupportedTypes_cb _hidl_cb) {
    std::vector<Type> types;

    for (auto const& light : mLights) {
        types.push_back(light.first);
    }

    _hidl_cb(types);

    return Void();
}

void Light::setAttentionLight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    mAttentionState = state;
    setSpeakerBatteryLightLocked();
}

void Light::setLcdBacklight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);

    uint32_t brightness = rgbToBrightness(state);

    // If max panel brightness is not the default (255),
    // apply linear scaling across the accepted range.
    if (mPanelMaxBrightness != DEFAULT_MAX_BRIGHTNESS) {
        int old_brightness = brightness;
        brightness = brightness * mPanelMaxBrightness / DEFAULT_MAX_BRIGHTNESS;
        LOG(VERBOSE) << "scaling brightness " << old_brightness << " => " << brightness;
    }

    set(kLcdBacklightPath, brightness);
}

void Light::setBatteryLight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    mBatteryState = state;
    setSpeakerBatteryLightLocked();
}

void Light::setNotificationLight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);

    uint32_t brightness, color, rgb[3];
    LightState localState = state;

    // If a brightness has been applied by the user
    brightness = (localState.color & 0xff000000) >> 24;
    if (brightness > 0 && brightness < 255) {
        // Retrieve each of the RGB colors
        color = localState.color & 0x00ffffff;
        rgb[0] = (color >> 16) & 0xff;
        rgb[1] = (color >> 8) & 0xff;
        rgb[2] = color & 0xff;

        // Apply the brightness level
        if (rgb[0] > 0) {
            rgb[0] = (rgb[0] * brightness) / 0xff;
        }
        if (rgb[1] > 0) {
            rgb[1] = (rgb[1] * brightness) / 0xff;
        }
        if (rgb[2] > 0) {
            rgb[2] = (rgb[2] * brightness) / 0xff;
        }

        // Update with the new color
        localState.color = (rgb[0] << 16) + (rgb[1] << 8) + rgb[2];
    }

    mNotificationState = localState;
    setSpeakerBatteryLightLocked();
}

void Light::setSpeakerBatteryLightLocked() {
    if (isLit(mNotificationState)) {
        setSpeakerLightLocked(mNotificationState);
    } else if (isLit(mAttentionState)) {
        setSpeakerLightLocked(mAttentionState);
    } else if (isLit(mBatteryState)) {
        setSpeakerLightLocked(mBatteryState);
    } else {
        // Lights off
        set(kRedLedPath, 0);
        set(kGreenLedPath, 0);
        set(kBlueLedPath, 0);
        set(kRedBlinkPath, 0);
        set(kGreenBlinkPath, 0);
        set(kBlueBlinkPath, 0);
    }
}

void Light::setSpeakerLightLocked(const LightState& state) {
    int red, green, blue, blink;
    int onMs, offMs, stepDuration, pauseHi;
    uint32_t colorRGB = state.color;

    switch (state.flashMode) {
        case Flash::TIMED:
            onMs = state.flashOnMs;
            offMs = state.flashOffMs;
            break;
        case Flash::NONE:
        default:
            onMs = 0;
            offMs = 0;
            break;
    }

    red = (colorRGB >> 16) & 0xff;
    green = (colorRGB >> 8) & 0xff;
    blue = colorRGB & 0xff;
    blink = onMs > 0 && offMs > 0;

    // Disable all blinking to start
    set(kRgbBlinkPath, 0);

    if (blink) {
        stepDuration = RAMP_STEP_DURATION;
        pauseHi = onMs - (stepDuration * RAMP_SIZE * 2);

        if (stepDuration * RAMP_SIZE * 2 > onMs) {
            stepDuration = onMs / (RAMP_SIZE * 2);
            pauseHi = 0;
        }

        // Red
        set(kRedStartIdxPath, 0);
        set(kRedDutyPctsPath, getScaledDutyPcts(red));
        set(kRedPauseLoPath, offMs);
        set(kRedPauseHiPath, pauseHi);
        set(kRedRampStepMsPath, stepDuration);

        // Green
        set(kGreenStartIdxPath, RAMP_SIZE);
        set(kGreenDutyPctsPath, getScaledDutyPcts(green));
        set(kGreenPauseLoPath, offMs);
        set(kGreenPauseHiPath, pauseHi);
        set(kGreenRampStepMsPath, stepDuration);

        // Blue
        set(kBlueStartIdxPath, RAMP_SIZE * 2);
        set(kBlueDutyPctsPath, getScaledDutyPcts(blue));
        set(kBluePauseLoPath, offMs);
        set(kBluePauseHiPath, pauseHi);
        set(kBlueRampStepMsPath, stepDuration);

        // Start the party
        set(kRgbBlinkPath, 1);
    } else {
        set(kRedLedPath, red);
        set(kGreenLedPath, green);
        set(kBlueLedPath, blue);
    }
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
