// Host-only fixtures, not generated HIDL headers or an Android ABI test.
#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace android {
template <typename T> using sp = std::shared_ptr<T>;

namespace hardware {
template <typename T, size_t N> using hidl_array = std::array<T, N>;
template <typename T> using hidl_vec = std::vector<T>;
using hidl_string = std::string;
struct hidl_memory {};
struct Void {};

struct ReturnState {
    bool ok;
    bool checked = false;
};

// Like HIDL, an unchecked failed transport return must not be discarded.
template <typename T> class Return {
public:
    Return(T value, bool ok = true)
        : mValue(std::move(value)), mState(std::make_shared<ReturnState>(ReturnState{ok})) {}
    ~Return() { assert(mState->ok || mState->checked); }
    bool isOk() const { mState->checked = true; return mState->ok; }
    std::string description() const { return "test transport failure"; }
    operator T() const { assert(isOk()); return mValue; }

private:
    T mValue;
    std::shared_ptr<ReturnState> mState;
};

template <> class Return<void> {
public:
    Return(Void = {}) : Return(true) {}
    explicit Return(bool ok) : mState(std::make_shared<ReturnState>(ReturnState{ok})) {}
    ~Return() { assert(mState->ok || mState->checked); }
    bool isOk() const { mState->checked = true; return mState->ok; }
    std::string description() const { return "test transport failure"; }

private:
    std::shared_ptr<ReturnState> mState;
};

namespace radio {
namespace V1_0 {
enum class RadioResponseType : int32_t { SOLICITED = 0, SOLICITED_ACK = 1 };
enum class RadioError : int32_t { NONE = 0, REQUEST_NOT_SUPPORTED = 6 };
struct RadioResponseInfo {
    RadioResponseType type;
    int32_t serial;
    RadioError error;
};
}  // namespace V1_0

namespace config {
namespace V1_0 {
struct SimSlotStatus { hidl_string iccid; };
struct IRadioConfigIndication { virtual ~IRadioConfigIndication() = default; };
struct IRadioConfigResponse {
    virtual ~IRadioConfigResponse() = default;
    virtual Return<void> getSimSlotsStatusResponse(
        const radio::V1_0::RadioResponseInfo&, const hidl_vec<SimSlotStatus>&) = 0;
    virtual Return<void> setSimSlotsMappingResponse(const radio::V1_0::RadioResponseInfo&) = 0;
};
struct IRadioConfig {
    virtual ~IRadioConfig() = default;
    virtual Return<void> setResponseFunctions(
        const sp<IRadioConfigResponse>&, const sp<IRadioConfigIndication>&) = 0;
    virtual Return<void> getSimSlotsStatus(int32_t) = 0;
    virtual Return<void> setSimSlotsMapping(int32_t, const hidl_vec<uint32_t>&) = 0;
};
}  // namespace V1_0

namespace V1_1 {
struct ModemInfo { uint8_t modemId; };
struct PhoneCapability {
    uint8_t maxActiveData;
    uint8_t maxActiveInternetData;
    bool isInternetLingeringSupported;
    hidl_vec<ModemInfo> logicalModemList;
};
struct ModemsConfig { uint8_t numOfLiveModems; };
struct IRadioConfigResponse : V1_0::IRadioConfigResponse {
    inline static bool failCast = false;
    static Return<sp<IRadioConfigResponse>> castFrom(const sp<V1_0::IRadioConfigResponse>& base) {
        if (failCast) return {nullptr, false};
        return std::dynamic_pointer_cast<IRadioConfigResponse>(base);
    }
    virtual Return<void> getPhoneCapabilityResponse(
        const radio::V1_0::RadioResponseInfo&, const PhoneCapability&) = 0;
    virtual Return<void> setPreferredDataModemResponse(const radio::V1_0::RadioResponseInfo&) = 0;
    virtual Return<void> setModemsConfigResponse(const radio::V1_0::RadioResponseInfo&) = 0;
    virtual Return<void> getModemsConfigResponse(
        const radio::V1_0::RadioResponseInfo&, const ModemsConfig&) = 0;
};
struct IRadioConfig : V1_0::IRadioConfig {
    virtual Return<void> getPhoneCapability(int32_t) = 0;
    virtual Return<void> setPreferredDataModem(int32_t, uint8_t) = 0;
    virtual Return<void> setModemsConfig(int32_t, const ModemsConfig&) = 0;
    virtual Return<void> getModemsConfig(int32_t) = 0;
};
}  // namespace V1_1
}  // namespace config
}  // namespace radio
}  // namespace hardware
}  // namespace android

inline unsigned int fakeLogCount = 0;
inline void fakeLog(const char*, ...) __attribute__((format(printf, 1, 2)));
inline void fakeLog(const char*, ...) { ++fakeLogCount; }
#define ALOGE(...) fakeLog(__VA_ARGS__)
