#include "bk7238_rdi_native.h"
#include <crypto/CHIPCryptoPAL.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/CHIPDeviceConfig.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <lib/support/CodeUtils.h>
#include <string.h>

namespace chip {
namespace DeviceLayer {
CHIP_ERROR PrepareBekenRdiPayload()
{
    uint8_t uid[BK_RDI_UID_MAX] = {};
    MutableByteSpan span(uid);
    CHIP_ERROR err = CHIP_NO_ERROR;
    int need = bk_rdi_native_needs_uid();
    ChipLogProgress(DeviceLayer, "RDI storage preflight result=%d", need);
    VerifyOrReturnError(need >= 0, CHIP_ERROR_PERSISTED_STORAGE_FAILED);
    if (need == 1 || CONFIG_ENABLE_BEKEN_DEVICE_INFO) {
        auto *provider = GetDeviceInstanceInfoProvider();
        err = provider != nullptr ? provider->GetRotatingDeviceIdUniqueId(span) : CHIP_ERROR_INCORRECT_STATE;
        bool valid = err == CHIP_NO_ERROR && span.size() >= BK_RDI_UID_MIN && span.size() <= BK_RDI_UID_MAX;
        uint8_t any = 0, notFF = 0;
        if (valid) {
            for (size_t i = 0; i < span.size(); ++i) { any |= uid[i]; notFF |= static_cast<uint8_t>(~uid[i]); }
            valid = any != 0 && notFF != 0;
        }
        if (!valid) {
#if CONFIG_ENABLE_BEKEN_DEVICE_INFO
            ChipLogError(DeviceLayer, "Factory RDI UID is missing or invalid");
            return err != CHIP_NO_ERROR ? err : CHIP_ERROR_INVALID_ARGUMENT;
#else
            span = MutableByteSpan(uid, BK_RDI_UID_MIN);
            ReturnErrorOnFailure(Crypto::DRBG_get_bytes(uid, span.size()));
#endif
        }
#if CONFIG_ENABLE_BEKEN_DEVICE_INFO
        VerifyOrReturnError(span.size() == 16, CHIP_ERROR_INVALID_ARGUMENT);
#endif
    }
    const bool supplyUid = need == 1 || CONFIG_ENABLE_BEKEN_DEVICE_INFO;
    int result = bk_rdi_native_begin(supplyUid ? uid : nullptr, supplyUid ? span.size() : 0);
    { volatile uint8_t *p = uid; size_t n = sizeof(uid); while (n--) *p++ = 0; }
    ChipLogProgress(DeviceLayer, "RDI payload begin result=%d", result);
    VerifyOrReturnError(result == BK_RDI_OK, CHIP_ERROR_PERSISTED_STORAGE_FAILED);
    return CHIP_NO_ERROR;
}
} // namespace DeviceLayer
} // namespace chip
