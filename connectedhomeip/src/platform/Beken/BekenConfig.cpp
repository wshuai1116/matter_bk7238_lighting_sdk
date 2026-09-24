/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/* this file behaves like a config.h, comes first */
#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <lib/core/CHIPEncoding.h>
#include <platform/Beken/BekenConfig.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CHIPMemString.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

#include "matter_pal.h"
#include "easyflash.h"

enum
{
    kPrefsTypeBoolean = 1,
    kPrefsTypeInteger = 2,
    kPrefsTypeString  = 3,
    kPrefsTypeBuffer  = 4,
    kPrefsTypeBinary  = 5
};

namespace chip {
namespace DeviceLayer {
namespace Internal {

// *** CAUTION ***: Changing the names or namespaces of these values will *break* existing devices.

// NVS namespaces used to store device configuration information.
const char BekenConfig::kConfigNamespace_ChipFactory[]  = "chip-factory";
const char BekenConfig::kConfigNamespace_ChipConfig[]   = "chip-config";
const char BekenConfig::kConfigNamespace_ChipCounters[] = "chip-counters";

// Keys stored in the chip-factory namespace
const BekenConfig::Key BekenConfig::kConfigKey_SerialNum             = { kConfigNamespace_ChipFactory, "serial-num" };
const BekenConfig::Key BekenConfig::kConfigKey_MfrDeviceId           = { kConfigNamespace_ChipFactory, "device-id" };
const BekenConfig::Key BekenConfig::kConfigKey_MfrDeviceCert         = { kConfigNamespace_ChipFactory, "device-cert" };
const BekenConfig::Key BekenConfig::kConfigKey_MfrDeviceICACerts     = { kConfigNamespace_ChipFactory, "device-ca-certs" };
const BekenConfig::Key BekenConfig::kConfigKey_MfrDevicePrivateKey   = { kConfigNamespace_ChipFactory, "device-key" };
const BekenConfig::Key BekenConfig::kConfigKey_HardwareVersion       = { kConfigNamespace_ChipFactory, "hardware-ver" };
const BekenConfig::Key BekenConfig::kConfigKey_HardwareVersionString = { kConfigNamespace_ChipFactory, "hw-ver-str" };
const BekenConfig::Key BekenConfig::kConfigKey_ManufacturingDate     = { kConfigNamespace_ChipFactory, "mfg-date" };
const BekenConfig::Key BekenConfig::kConfigKey_SetupPinCode          = { kConfigNamespace_ChipFactory, "pin-code" };
const BekenConfig::Key BekenConfig::kConfigKey_SetupDiscriminator    = { kConfigNamespace_ChipFactory, "discriminator" };
const BekenConfig::Key BekenConfig::kConfigKey_Spake2pIterationCount = { kConfigNamespace_ChipFactory, "iteration-count" };
const BekenConfig::Key BekenConfig::kConfigKey_Spake2pSalt           = { kConfigNamespace_ChipFactory, "salt" };
const BekenConfig::Key BekenConfig::kConfigKey_Spake2pVerifier       = { kConfigNamespace_ChipFactory, "verifier" };
const BekenConfig::Key BekenConfig::kConfigKey_UniqueId              = { kConfigNamespace_ChipFactory, "uniqueId" };
const BekenConfig::Key BekenConfig::kConfigKey_DACCert               = { kConfigNamespace_ChipFactory, "dac-cert" };
const BekenConfig::Key BekenConfig::kConfigKey_DACPrivateKey         = { kConfigNamespace_ChipFactory, "dac-key" };
const BekenConfig::Key BekenConfig::kConfigKey_DACPublicKey          = { kConfigNamespace_ChipFactory, "dac-pub-key" };
const BekenConfig::Key BekenConfig::kConfigKey_PAICert               = { kConfigNamespace_ChipFactory, "pai-cert" };
const BekenConfig::Key BekenConfig::kConfigKey_CertDeclaration       = { kConfigNamespace_ChipFactory, "cert-dclrn" };
const BekenConfig::Key BekenConfig::kConfigKey_VendorId              = { kConfigNamespace_ChipFactory, "vendor-id" };
const BekenConfig::Key BekenConfig::kConfigKey_VendorName            = { kConfigNamespace_ChipFactory, "vendor-name" };
const BekenConfig::Key BekenConfig::kConfigKey_ProductId             = { kConfigNamespace_ChipFactory, "product-id" };
const BekenConfig::Key BekenConfig::kConfigKey_ProductName           = { kConfigNamespace_ChipFactory, "product-name" };
const BekenConfig::Key BekenConfig::kConfigKey_ProductLabel          = { kConfigNamespace_ChipFactory, "product-label" };
const BekenConfig::Key BekenConfig::kConfigKey_ProductURL            = { kConfigNamespace_ChipFactory, "product-url" };
const BekenConfig::Key BekenConfig::kConfigKey_SupportedCalTypes     = { kConfigNamespace_ChipFactory, "cal-types" };
const BekenConfig::Key BekenConfig::kConfigKey_SupportedLocaleSize   = { kConfigNamespace_ChipFactory, "locale-sz" };
const BekenConfig::Key BekenConfig::kConfigKey_RotatingDevIdUniqueId = { kConfigNamespace_ChipFactory, "rd-id-uid" };

// Keys stored in the chip-config namespace
const BekenConfig::Key BekenConfig::kConfigKey_FabricId                    = { kConfigNamespace_ChipConfig, "fabric-id" };
const BekenConfig::Key BekenConfig::kConfigKey_ServiceConfig               = { kConfigNamespace_ChipConfig, "service-config" };
const BekenConfig::Key BekenConfig::kConfigKey_PairedAccountId             = { kConfigNamespace_ChipConfig, "account-id" };
const BekenConfig::Key BekenConfig::kConfigKey_ServiceId                   = { kConfigNamespace_ChipConfig, "service-id" };
const BekenConfig::Key BekenConfig::kConfigKey_GroupKeyIndex               = { kConfigNamespace_ChipConfig, "group-key-index" };
const BekenConfig::Key BekenConfig::kConfigKey_LastUsedEpochKeyId          = { kConfigNamespace_ChipConfig, "last-ek-id" };
const BekenConfig::Key BekenConfig::kConfigKey_FailSafeArmed               = { kConfigNamespace_ChipConfig, "fail-safe-armed" };
const BekenConfig::Key BekenConfig::kConfigKey_WiFiStationSecType          = { kConfigNamespace_ChipConfig, "sta-sec-type" };
const BekenConfig::Key BekenConfig::kConfigKey_OperationalDeviceId         = { kConfigNamespace_ChipConfig, "op-device-id" };
const BekenConfig::Key BekenConfig::kConfigKey_OperationalDeviceCert       = { kConfigNamespace_ChipConfig, "op-device-cert" };
const BekenConfig::Key BekenConfig::kConfigKey_OperationalDeviceICACerts   = { kConfigNamespace_ChipConfig, "op-device-ca-certs" };
const BekenConfig::Key BekenConfig::kConfigKey_OperationalDevicePrivateKey = { kConfigNamespace_ChipConfig, "op-device-key" };
const BekenConfig::Key BekenConfig::kConfigKey_RegulatoryLocation          = { kConfigNamespace_ChipConfig, "regulatory-location" };
const BekenConfig::Key BekenConfig::kConfigKey_CountryCode                 = { kConfigNamespace_ChipConfig, "country-code" };
const BekenConfig::Key BekenConfig::kConfigKey_Breadcrumb                  = { kConfigNamespace_ChipConfig, "breadcrumb" };
const BekenConfig::Key BekenConfig::kConfigKey_SoftwareVersion             = { kConfigNamespace_ChipConfig, "software-version" };
const BekenConfig::Key BekenConfig::kConfigKey_SoftwareVersionString       = { kConfigNamespace_ChipConfig, "software-version-str" };
const BekenConfig::Key BekenConfig::kConfigKey_FastReboot                  = { kConfigNamespace_ChipConfig, "fast-reboot" };
const BekenConfig::Key BekenConfig::kConfigKey_OldSoftwareVersion          = { kConfigNamespace_ChipConfig, "old-sw-version" };
const BekenConfig::Key BekenConfig::kConfigKey_OldSoftwareVersionString    = { kConfigNamespace_ChipConfig, "old-sw-version-str" };
const BekenConfig::Key BekenConfig::kConfigKey_RBLSoftwareVersion          = { kConfigNamespace_ChipConfig, "rbl-sw-version" };


// Keys stored in the Chip-counters namespace
const BekenConfig::Key BekenConfig::kCounterKey_RebootCount           = { kConfigNamespace_ChipCounters, "reboot-count" };
const BekenConfig::Key BekenConfig::kCounterKey_UpTime                = { kConfigNamespace_ChipCounters, "up-time" };
const BekenConfig::Key BekenConfig::kCounterKey_TotalOperationalHours = { kConfigNamespace_ChipCounters, "total-hours" };
const BekenConfig::Key BekenConfig::kCounterKey_BootReason            = { kConfigNamespace_ChipCounters, "boot-reason" };

CHIP_ERROR BekenConfig::ReadConfigValue(Key key, bool & val)
{
    uint8_t intval      = 0;
    char sKey[64];
    ReturnErrorOnFailure(key.toString(sKey, 64));
    size_t outLen = ef_get_env_blob(sKey, (void *)&intval, sizeof(intval), NULL);
    if(0 == outLen)
    {
         ChipLogError(DeviceLayer, "read %s %s failed", key.Namespace, key.Name);
         return CHIP_ERROR_READ_FAILED;
    }
    val = (intval != 0);
    return CHIP_NO_ERROR;
}

CHIP_ERROR BekenConfig::ReadConfigValue(Key key, uint16_t & val)
{
    char sKey[64];
    ReturnErrorOnFailure(key.toString(sKey, 64));
    size_t outLen = ef_get_env_blob(sKey, (void *)&val, sizeof(val), NULL);
    if(0 == outLen)
    {
         ChipLogError(DeviceLayer, "read %s %s failed", key.Namespace, key.Name);
         return CHIP_ERROR_READ_FAILED;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR BekenConfig::ReadConfigValue(Key key, uint32_t & val)
{
    char sKey[64];
    ReturnErrorOnFailure(key.toString(sKey, 64));
    size_t outLen = ef_get_env_blob(sKey, (void *)&val, sizeof(val), NULL);
    if(0 == outLen)
    {
         ChipLogError(DeviceLayer, "read %s %s failed", key.Namespace, key.Name);
         return CHIP_ERROR_READ_FAILED;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR BekenConfig::ReadConfigValue(Key key, uint64_t & val)
{
    char sKey[64];
    ReturnErrorOnFailure(key.toString(sKey, 64));
    size_t outLen = ef_get_env_blob(sKey, (void *)&val, sizeof(val), NULL);
    if(0 == outLen)
    {
         ChipLogError(DeviceLayer, "read %s %s failed", key.Namespace, key.Name);
         return CHIP_ERROR_READ_FAILED;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR BekenConfig::ReadConfigValueStr(Key key, char * buf, size_t bufSize, size_t & outLen)
{
    char sKey[64];
    ReturnErrorOnFailure(key.toString(sKey, 64));
    outLen = ef_get_env_blob(sKey, buf, bufSize, NULL);
    if(0 == outLen)
    {
         ChipLogError(DeviceLayer, "read %s %s failed", key.Namespace, key.Name);
         return CHIP_ERROR_READ_FAILED;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR BekenConfig::ReadConfigValueBin(Key key, uint8_t * buf, size_t bufSize, size_t & outLen)
{
    char sKey[64];
    ReturnErrorOnFailure(key.toString(sKey, 64));
    outLen = ef_get_env_blob(sKey, buf, bufSize, NULL);
    if(0 == outLen)
    {
         ChipLogError(DeviceLayer, "read %s %s failed", key.Namespace, key.Name);
         return CHIP_ERROR_READ_FAILED;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR BekenConfig::WriteConfigValue(Key key, bool val)
{
    uint8_t value    = 0;

    if (val == 1)
    {
        value = 1;
    }
    else
    {
        value = 0;
    }
    char sKey[64];
    ReturnErrorOnFailure(key.toString(sKey, 64));
    EfErrCode r = ef_set_env_blob(sKey, (void *)&value, sizeof(value));
    if(EF_NO_ERR != r)
    {
         ChipLogError(DeviceLayer, "write %s %s %d failed", key.Namespace, key.Name, r);
         return CHIP_ERROR_WRITE_FAILED;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR BekenConfig::WriteConfigValue(Key key, uint16_t val)
{
    char sKey[64];
    ReturnErrorOnFailure(key.toString(sKey, 64));
    EfErrCode r = ef_set_env_blob(sKey, (void *)&val, sizeof(val));
    if(EF_NO_ERR != r)
    {
         ChipLogError(DeviceLayer, "write %s %s %d failed", key.Namespace, key.Name, r);
         return CHIP_ERROR_WRITE_FAILED;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR BekenConfig::WriteConfigValue(Key key, uint32_t val)
{
    char sKey[64];
    ReturnErrorOnFailure(key.toString(sKey, 64));
    EfErrCode r = ef_set_env_blob(sKey, (void *)&val, sizeof(val));
    if(EF_NO_ERR != r)
    {
         ChipLogError(DeviceLayer, "write %s %s %d failed", key.Namespace, key.Name, r);
         return CHIP_ERROR_WRITE_FAILED;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR BekenConfig::WriteConfigValue(Key key, uint64_t val)
{
    char sKey[64];
    ReturnErrorOnFailure(key.toString(sKey, 64));
    EfErrCode r = ef_set_env_blob(sKey, (void *)&val, sizeof(val));
    if(EF_NO_ERR != r)
    {
         ChipLogError(DeviceLayer, "write %s %s %d failed", key.Namespace, key.Name, r);
         return CHIP_ERROR_WRITE_FAILED;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR BekenConfig::WriteConfigValueStr(Key key, const char * str)
{
    char sKey[64];
    ReturnErrorOnFailure(key.toString(sKey, 64));
    EfErrCode r = ef_set_env_blob(sKey, str, strlen(str));
    if(EF_NO_ERR != r)
    {
         ChipLogError(DeviceLayer, "write %s %s %d failed", key.Namespace, key.Name, r);
         return CHIP_ERROR_WRITE_FAILED;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR BekenConfig::WriteConfigValueStr(Key key, const char * str, size_t strLen)
{
    strLen = (strLen > strlen(str)) ? strlen(str) : strLen;
    char sKey[64];
    ReturnErrorOnFailure(key.toString(sKey, 64));
    EfErrCode r = ef_set_env_blob(sKey, str, strLen);
    if(EF_NO_ERR != r)
    {
         ChipLogError(DeviceLayer, "write %s %s %d failed", key.Namespace, key.Name, r);
         return CHIP_ERROR_WRITE_FAILED;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR BekenConfig::WriteConfigValueBin(Key key, const uint8_t * data, size_t dataLen)
{
    char sKey[64];
    ReturnErrorOnFailure(key.toString(sKey, 64));
    EfErrCode r = ef_set_env_blob(sKey, data, dataLen);
    if(EF_NO_ERR != r)
    {
         ChipLogError(DeviceLayer, "write %s %s %d failed", key.Namespace, key.Name, r);
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR BekenConfig::ClearConfigValue(Key key)
{
    char sKey[64];
    ReturnErrorOnFailure(key.toString(sKey, 64));
    EfErrCode r = ef_set_env_blob(sKey, NULL, 0);
    if(EF_NO_ERR != r)
    {
         ChipLogError(DeviceLayer, "clear %s %s %d failed", key.Namespace, key.Name, r);
    }
    return CHIP_NO_ERROR;
}

bool BekenConfig::ConfigValueExists(Key key)
{
    uint8_t data[1];
    char sKey[64];
    if(CHIP_NO_ERROR != key.toString(sKey, 64))
    {
        return false;
    }
    return 0 < ef_get_env_blob(sKey, data, 1, NULL);
}

void BekenConfig::RunConfigUnitTest() {}

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
