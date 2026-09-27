/*
 * SPDX-FileCopyrightText: PenguinOS
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "GlyphLights"

#include <aidl/android/hardware/light/BnLights.h>
#include <android-base/file.h>
#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <mutex>

using ::aidl::android::hardware::light::BnLights;
using ::aidl::android::hardware::light::HwLight;
using ::aidl::android::hardware::light::HwLightState;
using ::aidl::android::hardware::light::LightType;
using ::android::base::WriteStringToFile;

namespace {

constexpr int kLightCameraIndicator = 105;
constexpr int kLightCameraFill = 106;
constexpr int kLightCameraToy = 120;

// NTCamera sends these commands as the light's colour.
constexpr int kFillOn = 11;
constexpr int kFillOff = 10;
constexpr int kCountdownOn = 7;
constexpr int kCountdownOff = 3;

constexpr const char* kModePath = "/sys/devices/platform/soc/994000.i2c/i2c-0/0-003a/leds/led_strips/operating_mode";
constexpr const char* kAllWhitePath = "/sys/devices/platform/soc/994000.i2c/i2c-0/0-003a/leds/led_strips/all_white_brightness";
constexpr int kMaxBrightness = 254;

class Lights : public BnLights {
  public:
    ndk::ScopedAStatus setLightState(int id, const HwLightState& state) override {
        switch (id) {
            case kLightCameraIndicator:
                setAll(state.color != 0);
                break;
            case kLightCameraFill:
                switch (state.color) {
                    case kFillOn:
                    case kCountdownOn:
                        setAll(true);
                        break;
                    // LightsService clears a light to 0 when the camera's session goes away.
                    case 0:
                    case kFillOff:
                    case kCountdownOff:
                        setAll(false);
                        break;
                    default:
                        LOG(DEBUG) << "Ignoring camera command " << state.color;
                }
                break;
            case kLightCameraToy:
                break;
            default:
                return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        }
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus getLights(std::vector<HwLight>* lights) override {
        for (int id : {kLightCameraIndicator, kLightCameraFill, kLightCameraToy}) {
            lights->push_back({.id = id, .ordinal = 0, .type = LightType::CAMERA});
        }
        return ndk::ScopedAStatus::ok();
    }

  private:
    void setAll(bool on) {
        std::lock_guard<std::mutex> lock(mLock);
        // The strip ignores brightness writes unless the mode is set first.
        WriteStringToFile("1", kModePath);
        if (!WriteStringToFile(std::to_string(on ? kMaxBrightness : 0), kAllWhitePath)) {
            PLOG(ERROR) << "Failed to write " << kAllWhitePath;
        }
    }

    std::mutex mLock;
};

}  // namespace

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    std::shared_ptr<Lights> lights = ndk::SharedRefBase::make<Lights>();
    const std::string instance = std::string(Lights::descriptor) + "/default";
    CHECK_EQ(AServiceManager_addService(lights->asBinder().get(), instance.c_str()), STATUS_OK);
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
