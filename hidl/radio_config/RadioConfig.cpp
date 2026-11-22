/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.1 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.1
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "android.hardware.radio.config@1.1-service.on7xelte"

#include "RadioConfig.h"

#include <log/log.h>

namespace android {
namespace hardware {
namespace radio {
namespace config {

using namespace ::android::hardware::radio::config::V1_1;

namespace {

using ::android::hardware::radio::V1_0::RadioError;
using ::android::hardware::radio::V1_0::RadioResponseInfo;
using ::android::hardware::radio::V1_0::RadioResponseType;

RadioResponseInfo unsupportedResponse(int32_t serial) {
    RadioResponseInfo info{};
    info.type = RadioResponseType::SOLICITED;
    info.serial = serial;
    info.error = RadioError::REQUEST_NOT_SUPPORTED;
    return info;
}

void checkResponse(const Return<void>& result) {
    if (!result.isOk()) {
        ALOGE("Radio-config response failed: %s", result.description().c_str());
    }
}

}  // namespace

// Methods from ::android::hardware::radio::config::V1_1::IRadioConfig follow.
Return<void> RadioConfig::setResponseFunctions(
    const sp<V1_0::IRadioConfigResponse>& radioConfigResponse,
    const sp<V1_0::IRadioConfigIndication>& radioConfigIndication) {
    mRadioConfigResponse = radioConfigResponse;
    mRadioConfigResponseV1_1 = nullptr;
    if (radioConfigResponse != nullptr) {
        auto responseV1_1 = V1_1::IRadioConfigResponse::castFrom(radioConfigResponse);
        if (responseV1_1.isOk()) {
            mRadioConfigResponseV1_1 = responseV1_1;
        } else {
            ALOGE("Could not cast radio-config response: %s", responseV1_1.description().c_str());
        }
    }
    mRadioConfigIndication = radioConfigIndication;
    return Void();
}

// This legacy stub has no modem-backed radio-config implementation. Reject
// unimplemented queries/reconfiguration so Android retains its legacy RIL
// configuration. Selecting the single existing data modem below is satisfied.
Return<void> RadioConfig::getSimSlotsStatus(int32_t serial) {
    if (mRadioConfigResponse != nullptr) {
        const hidl_vec<V1_0::SimSlotStatus> slotStatus{};
        checkResponse(mRadioConfigResponse->getSimSlotsStatusResponse(
            unsupportedResponse(serial), slotStatus));
    }
    return Void();
}

Return<void> RadioConfig::setSimSlotsMapping(int32_t serial,
                                            const hidl_vec<uint32_t>& /* slotMap */) {
    if (mRadioConfigResponse != nullptr) {
        checkResponse(mRadioConfigResponse->setSimSlotsMappingResponse(unsupportedResponse(serial)));
    }
    return Void();
}

Return<void> RadioConfig::getPhoneCapability(int32_t serial) {
    if (mRadioConfigResponseV1_1 != nullptr) {
        const V1_1::PhoneCapability capability{};
        checkResponse(mRadioConfigResponseV1_1->getPhoneCapabilityResponse(
            unsupportedResponse(serial), capability));
    }
    return Void();
}

Return<void> RadioConfig::setPreferredDataModem(int32_t serial, uint8_t modemId) {
    if (mRadioConfigResponseV1_1 != nullptr) {
        auto info = unsupportedResponse(serial);
        // Single-SIM: modem 0 is already the preferred modem; no switch is needed.
        if (modemId == 0) {
            info.error = RadioError::NONE;  // SUCCESS in the framework.
        }
        checkResponse(mRadioConfigResponseV1_1->setPreferredDataModemResponse(info));
    }
    return Void();
}

Return<void> RadioConfig::setModemsConfig(int32_t serial,
                                         const V1_1::ModemsConfig& /* modemsConfig */) {
    if (mRadioConfigResponseV1_1 != nullptr) {
        checkResponse(mRadioConfigResponseV1_1->setModemsConfigResponse(unsupportedResponse(serial)));
    }
    return Void();
}

Return<void> RadioConfig::getModemsConfig(int32_t serial) {
    if (mRadioConfigResponseV1_1 != nullptr) {
        const V1_1::ModemsConfig config{};
        checkResponse(mRadioConfigResponseV1_1->getModemsConfigResponse(
            unsupportedResponse(serial), config));
    }
    return Void();
}

}  // namespace config
}  // namespace radio
}  // namespace hardware
}  // namespace android
