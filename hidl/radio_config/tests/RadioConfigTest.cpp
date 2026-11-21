// Exercise the production RadioConfig.cpp with host-only callback fixtures.
#include "../RadioConfig.h"

#include <cstdio>
#include <limits>

using namespace android::hardware;
using namespace android::hardware::radio::config;
using android::hardware::radio::V1_0::RadioError;
using android::hardware::radio::V1_0::RadioResponseInfo;
using android::hardware::radio::V1_0::RadioResponseType;

enum class Method { SLOTS, MAPPING, CAPABILITY, DATA_MODEM, SET_MODEMS, GET_MODEMS };
struct Reply { Method method; RadioResponseInfo info; };
struct Recorder {
    std::vector<Reply> replies;
    bool fail = false;
    Return<void> record(Method method, const RadioResponseInfo& info) {
        replies.push_back({method, info});
        return Return<void>(!fail);
    }
};

template <typename Interface> struct BaseResponse : Interface {
    Recorder recorder;
    Return<void> getSimSlotsStatusResponse(
        const RadioResponseInfo& info, const hidl_vec<V1_0::SimSlotStatus>& slots) override {
        assert(slots.empty());  // No invented SIM/ICCID data.
        return recorder.record(Method::SLOTS, info);
    }
    Return<void> setSimSlotsMappingResponse(const RadioResponseInfo& info) override {
        return recorder.record(Method::MAPPING, info);
    }
};

using Response10 = BaseResponse<V1_0::IRadioConfigResponse>;
struct Response11 : BaseResponse<V1_1::IRadioConfigResponse> {
    Return<void> getPhoneCapabilityResponse(
        const RadioResponseInfo& info, const V1_1::PhoneCapability& capability) override {
        assert(capability.maxActiveData == 0 && capability.maxActiveInternetData == 0);
        assert(!capability.isInternetLingeringSupported && capability.logicalModemList.empty());
        return recorder.record(Method::CAPABILITY, info);
    }
    Return<void> setPreferredDataModemResponse(const RadioResponseInfo& info) override {
        return recorder.record(Method::DATA_MODEM, info);
    }
    Return<void> setModemsConfigResponse(const RadioResponseInfo& info) override {
        return recorder.record(Method::SET_MODEMS, info);
    }
    Return<void> getModemsConfigResponse(
        const RadioResponseInfo& info, const V1_1::ModemsConfig& config) override {
        assert(config.numOfLiveModems == 0);
        return recorder.record(Method::GET_MODEMS, info);
    }
};

void requestAll(RadioConfig& config, int32_t serial) {
    assert(config.getSimSlotsStatus(serial).isOk());
    assert(config.setSimSlotsMapping(serial, {1, 0}).isOk());
    assert(config.getPhoneCapability(serial).isOk());
    assert(config.setPreferredDataModem(serial, 1).isOk());
    assert(config.setModemsConfig(serial, {2}).isOk());
    assert(config.getModemsConfig(serial).isOk());
}

void checkReply(const Reply& reply, Method method, int32_t serial) {
    assert(reply.method == method);
    assert(reply.info.serial == serial);
    assert(reply.info.type == RadioResponseType::SOLICITED);
    assert(reply.info.error == RadioError::REQUEST_NOT_SUPPORTED);
}

int main() {
    RadioConfig config;
    requestAll(config, 17);  // Before callbacks are registered: no null dereference.

    auto response = std::make_shared<Response11>();
    assert(config.setResponseFunctions(response, nullptr).isOk());
    for (const int32_t serial : {0, 1, -1, 12345, std::numeric_limits<int32_t>::min(),
                                std::numeric_limits<int32_t>::max(), 12345}) {
        response->recorder.replies.clear();
        requestAll(config, serial);  // Colliding/repeated serials must each get a response.
        assert(response->recorder.replies.size() == 6);
        for (size_t i = 0; i < 6; ++i) {
            checkReply(response->recorder.replies[i], static_cast<Method>(i), serial);
        }
    }
    std::puts("Six solicited responses preserve boundary and colliding serials: PASS");

    auto legacy = std::make_shared<Response10>();
    assert(config.setResponseFunctions(legacy, nullptr).isOk());
    const size_t oldCount = response->recorder.replies.size();
    requestAll(config, 81);
    assert(legacy->recorder.replies.size() == 2);
    checkReply(legacy->recorder.replies[0], Method::SLOTS, 81);
    checkReply(legacy->recorder.replies[1], Method::MAPPING, 81);
    assert(response->recorder.replies.size() == oldCount);  // No stale 1.1 callback.

    assert(config.setResponseFunctions(nullptr, nullptr).isOk());
    requestAll(config, 82);
    assert(legacy->recorder.replies.size() == 2);
    std::puts("1.0-only clients and null/replaced callbacks: PASS");

    V1_1::IRadioConfigResponse::failCast = true;
    const unsigned int beforeCast = fakeLogCount;
    assert(config.setResponseFunctions(response, nullptr).isOk());
    assert(fakeLogCount == beforeCast + 1);
    response->recorder.replies.clear();
    requestAll(config, 83);
    assert(response->recorder.replies.size() == 2);  // Keep 1.0 even if casting fails.
    V1_1::IRadioConfigResponse::failCast = false;

    assert(config.setResponseFunctions(response, nullptr).isOk());
    response->recorder.fail = true;
    response->recorder.replies.clear();
    const unsigned int beforeResponses = fakeLogCount;
    requestAll(config, 84);
    assert(response->recorder.replies.size() == 6);
    assert(fakeLogCount == beforeResponses + 6);  // All failed returns were checked/logged.
    for (size_t i = 0; i < 6; ++i) {
        checkReply(response->recorder.replies[i], static_cast<Method>(i), 84);
    }
    std::puts("Cast/callback transport errors are handled without aborting: PASS");
}
