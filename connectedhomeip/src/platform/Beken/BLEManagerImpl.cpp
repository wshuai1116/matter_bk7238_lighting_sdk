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

/**
 *    @file
 *          Provides an implementation of the BLEManager singleton object
 *          for the Beken platforms.
 */

/* this file behaves like a config.h, comes first */
#include <ble/Ble.h>
#include <crypto/CHIPCryptoPAL.h>
#include <lib/core/TLV.h>
#include <platform/CommissionableDataProvider.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <platform/internal/CHIPDeviceLayerInternal.h>
#include <setup_payload/AdditionalDataPayload.h>
#include <setup_payload/AdditionalDataPayloadGenerator.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <mbedtls/gcm.h>

#if CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
#include <ble/CHIPBleServiceData.h>
#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
#include <setup_payload/AdditionalDataPayloadGenerator.h>
#endif
#include "stdio.h"
#include "timers.h"
#include <string.h>

// BK7231n BLE related header files
extern "C" {
#include "matter_pal.h"
#include "ble_api_5_x.h"
#include "ble_api.h"
#include "app_ble.h"
#include "app_sdp.h"
#include "app_ble_init.h"
#include "ble_ui.h"
#include "rtos_pub.h"
#include "timers.h"
}

extern "C" void ql_ble_remote_notice_cb(ble_notice_t notice, void * param) __attribute__((weak));

#if CFG_BLE_USE_DYN_RAM
extern "C" void ble_thread_exit(void);
#endif

/*******************************************************************************
 * Local data types
 *******************************************************************************/
using namespace ::chip;
using namespace ::chip::Ble;

namespace chip {
namespace DeviceLayer {
namespace Internal {

namespace {

#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
constexpr uint8_t kAdditionalDataPrivateTag = 0x80;
constexpr size_t kAdditionalDataPrivateMaxLen = 160;
constexpr size_t kC3PrivateNonceLen = 12;
constexpr size_t kC3PrivateTagLen   = 16;
constexpr uint8_t kC3PrivateFormatVersion = 1;
// Shared AES-128 key for both ends; replace with your production key source.
constexpr uint8_t kC3PrivateAesKey[16] = {
    0x73, 0x21, 0x14, 0x57, 0x64, 0x34, 0xac, 0x6d,
    0xf7, 0xe3, 0xdf, 0x25, 0x9f, 0xab, 0xe6, 0x38,
};

// Avoid BLE task stack overflow: use static scratch buffers for C3 private payload path.
static mbedtls_gcm_context sC3GcmCtx;
static char sC3QrCodeBuffer[160];
static char sC3MacString[ConfigurationManager::kEthernetMACAddressLength * 2 + 1];
static char sC3PrivatePayload[kAdditionalDataPrivateMaxLen];
static uint8_t sC3EncryptedPrivatePayload[kAdditionalDataPrivateMaxLen];
static uint8_t sC3Mac[ConfigurationManager::kEthernetMACAddressLength];
static size_t sC3EncryptedPrivatePayloadLen = 0;
static bool sC3PrivatePayloadReady          = false;
#endif

CHIP_ERROR GenerateBleOnboardingQRCode(chip::MutableCharSpan & outQrCode)
{
    chip::PayloadContents payload;
    payload.version = 0;
    payload.rendezvousInformation.SetValue(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));

    ReturnErrorOnFailure(DeviceLayer::GetCommissionableDataProvider()->GetSetupPasscode(payload.setUpPINCode));

    uint16_t discriminator = 0;
    ReturnErrorOnFailure(DeviceLayer::GetCommissionableDataProvider()->GetSetupDiscriminator(discriminator));
    payload.discriminator.SetLongValue(discriminator);

    ReturnErrorOnFailure(chip::DeviceLayer::GetDeviceInstanceInfoProvider()->GetVendorId(payload.vendorID));
    ReturnErrorOnFailure(chip::DeviceLayer::GetDeviceInstanceInfoProvider()->GetProductId(payload.productID));

    return chip::QRCodeBasicSetupPayloadGenerator(payload).payloadBase38Representation(outQrCode);
}

#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
CHIP_ERROR EncryptC3PrivatePayloadAesGcm(const uint8_t * plaintext, size_t plaintextLen, uint8_t * output, size_t outputCapacity,
                                         size_t & outputLen)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    int mbedtlsErr = 0;

    VerifyOrReturnError(plaintext != nullptr && output != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(outputCapacity >= (1 + kC3PrivateNonceLen + plaintextLen + kC3PrivateTagLen), CHIP_ERROR_BUFFER_TOO_SMALL);

    output[0] = kC3PrivateFormatVersion;
    uint8_t * nonce = &output[1];
    uint8_t * cipherText = &output[1 + kC3PrivateNonceLen];
    uint8_t * authTag = &output[1 + kC3PrivateNonceLen + plaintextLen];

    ReturnErrorOnFailure(chip::Crypto::DRBG_get_bytes(nonce, kC3PrivateNonceLen));

    mbedtls_gcm_init(&sC3GcmCtx);
    mbedtlsErr = mbedtls_gcm_setkey(&sC3GcmCtx, MBEDTLS_CIPHER_ID_AES, kC3PrivateAesKey, sizeof(kC3PrivateAesKey) * 8);
    VerifyOrExit(mbedtlsErr == 0, err = CHIP_ERROR_INTERNAL);

    mbedtlsErr = mbedtls_gcm_crypt_and_tag(&sC3GcmCtx, MBEDTLS_GCM_ENCRYPT, plaintextLen, nonce, kC3PrivateNonceLen, nullptr, 0,
                                           plaintext, cipherText, kC3PrivateTagLen, authTag);
    VerifyOrExit(mbedtlsErr == 0, err = CHIP_ERROR_INTERNAL);

    outputLen = 1 + kC3PrivateNonceLen + plaintextLen + kC3PrivateTagLen;
exit:
    mbedtls_gcm_free(&sC3GcmCtx);
    return err;
}

CHIP_ERROR PrepareC3PrivatePayloadCache()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    CHIP_ERROR qrErr = CHIP_NO_ERROR;
    CHIP_ERROR macErr = CHIP_NO_ERROR;
    chip::MutableCharSpan qrCode(sC3QrCodeBuffer);
    int privatePayloadLen = 0;

    memset(sC3QrCodeBuffer, 0, sizeof(sC3QrCodeBuffer));
    memset(sC3MacString, 0, sizeof(sC3MacString));
    memset(sC3PrivatePayload, 0, sizeof(sC3PrivatePayload));
    memset(sC3EncryptedPrivatePayload, 0, sizeof(sC3EncryptedPrivatePayload));
    memset(sC3Mac, 0, sizeof(sC3Mac));
    sC3EncryptedPrivatePayloadLen = 0;
    sC3PrivatePayloadReady        = false;

    qrErr = GenerateBleOnboardingQRCode(qrCode);
    if (qrErr != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "GetQRCode failed: %" CHIP_ERROR_FORMAT, qrErr.Format());
        qrCode.reduce_size(0);
    }

    macErr = ConfigurationMgr().GetPrimaryWiFiMACAddress(sC3Mac);
    if (macErr != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "GetPrimaryWiFiMACAddress failed: %" CHIP_ERROR_FORMAT, macErr.Format());
        sC3MacString[0] = '\0';
    }
    else
    {
        snprintf(sC3MacString, sizeof(sC3MacString), "%02X%02X%02X%02X%02X%02X", sC3Mac[0], sC3Mac[1], sC3Mac[2], sC3Mac[3],
                 sC3Mac[4], sC3Mac[5]);
    }

    privatePayloadLen = snprintf(sC3PrivatePayload, sizeof(sC3PrivatePayload), "%.*s|MAC:%s", static_cast<int>(qrCode.size()),
                                 qrCode.data(), sC3MacString);
    VerifyOrReturnError(privatePayloadLen >= 0, CHIP_ERROR_INTERNAL);
    VerifyOrReturnError(static_cast<size_t>(privatePayloadLen) < sizeof(sC3PrivatePayload), CHIP_ERROR_BUFFER_TOO_SMALL);

    err = EncryptC3PrivatePayloadAesGcm(reinterpret_cast<const uint8_t *>(sC3PrivatePayload), static_cast<size_t>(privatePayloadLen),
                                        sC3EncryptedPrivatePayload, sizeof(sC3EncryptedPrivatePayload),
                                        sC3EncryptedPrivatePayloadLen);
    ReturnErrorOnFailure(err);

    sC3PrivatePayloadReady = true;
    return CHIP_NO_ERROR;
}
#endif

/*******************************************************************************
 * Macros & Constants definitions
 *******************************************************************************/
#ifndef MAX_ADV_DATA_LEN
#define MAX_ADV_DATA_LEN 31
#endif
#define CHIP_ADV_DATA_TYPE_FLAGS 0x01
#define CHIP_ADV_DATA_FLAGS 0x06

/* advertising configuration */
#define CHIP_ADV_SHORT_UUID_LEN (2)

/* FreeRTOS sw timer */
TimerHandle_t bleFastAdvTimer;
TimerHandle_t bleParaUpdateTimer;

enum
{
    DriveBLEExtPerfEvt_DISCONNECT = 0,
};

const ChipBleUUID ChipUUID_CHIPoBLEChar_RX = { { 0x18, 0xEE, 0x2E, 0xF5, 0x26, 0x3D, 0x45, 0x59, 0x95, 0x9F, 0x4F, 0x9C, 0x42, 0x9F,
                                                 0x9D, 0x11 } };
const ChipBleUUID ChipUUID_CHIPoBLEChar_TX = { { 0x18, 0xEE, 0x2E, 0xF5, 0x26, 0x3D, 0x45, 0x59, 0x95, 0x9F, 0x4F, 0x9C, 0x42, 0x9F,
                                                 0x9D, 0x12 } };
static const uint8_t _svc_uuid[16]         = { 0xF6, 0xFF, 0, 0, 0x0, 0x0, 0, 0, 0, 0, 0x0, 0x0, 0, 0, 0, 0 };

#define UUID_CHIPoBLECharact_RX                                                                                                    \
    {                                                                                                                              \
        0x11, 0x9D, 0x9F, 0x42, 0x9C, 0x4F, 0x9F, 0x95, 0x59, 0x45, 0x3D, 0x26, 0xF5, 0x2E, 0xEE, 0x18                             \
    }
#define ChipUUID_CHIPoBLECharact_TX                                                                                                \
    {                                                                                                                              \
        0x12, 0x9D, 0x9F, 0x42, 0x9C, 0x4F, 0x9F, 0x95, 0x59, 0x45, 0x3D, 0x26, 0xF5, 0x2E, 0xEE, 0x18                             \
    }
#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
#define UUID_CHIPoBLEChar_C3                                                                                                       \
    {                                                                                                                              \
        0x04, 0x8F, 0x21, 0x83, 0x8A, 0x74, 0x7D, 0xB8, 0xF2, 0x45, 0x72, 0x87, 0x38, 0x02, 0x63, 0x64                             \
    }
#endif
#define BEKEN_ATT_DECL_PRIMARY_SERVICE_128                                                                                         \
    {                                                                                                                              \
        0x00, 0x28, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0                                                                       \
    }
#define BEKEN_ATT_DECL_CHARACTERISTIC_128                                                                                          \
    {                                                                                                                              \
        0x03, 0x28, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0                                                                       \
    }
#define BEKEN_ATT_DESC_CLIENT_CHAR_CFG_128                                                                                         \
    {                                                                                                                              \
        0x02, 0x29, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0                                                                       \
    }

enum
{
    SVR_FFF6_IDX_SVC,
    SVR_FFF6_RX_DECL,
    SVR_FFF6_RX_VALUE,
    SVR_FFF6_TX_DECL,
    SVR_FFF6_TX_VALUE,
    SVR_FFF6_TX_CFG,
#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
    SVR_FFF6_C3_DECL,
    SVR_FFF6_C3_VALUE,
#endif
    SVR_FFF6_MAX,
};

#if (CFG_BLE_VERSION == BLE_VERSION_5_1)
bk_attm_desc_t svr_fff6_att_db[SVR_FFF6_MAX] = {
    //  Service Declaration
    [SVR_FFF6_IDX_SVC] = { BEKEN_ATT_DECL_PRIMARY_SERVICE_128, BK_PERM_SET(RD, ENABLE), 0, 2 },

    //  Level Characteristic Declaration
    [SVR_FFF6_RX_DECL] = { BEKEN_ATT_DECL_CHARACTERISTIC_128, BK_PERM_SET(RD, ENABLE), 0, 0 },
    //  Level Characteristic Value  BK_PERM_RIGHT_ENABLE WRITE_REQ_POS  RI_POS  UUID_LEN_POS  BK_PERM_RIGHT_UUID_128
    [SVR_FFF6_RX_VALUE] = { UUID_CHIPoBLECharact_RX, BK_PERM_SET(WRITE_REQ, ENABLE),
                            BK_PERM_SET(RI, ENABLE) | BK_PERM_SET(UUID_LEN, UUID_128), 512 },

    /// RD_POS  NTF_POS
    [SVR_FFF6_TX_DECL] = { BEKEN_ATT_DECL_CHARACTERISTIC_128, BK_PERM_SET(RD, ENABLE) | BK_PERM_SET(NTF, ENABLE), 0, 0 },
    ////  UUID_LEN_POS   BK_PERM_RIGHT_UUID_128 RD_POS
    [SVR_FFF6_TX_VALUE] = { ChipUUID_CHIPoBLECharact_TX, BK_PERM_SET(RD, ENABLE) | BK_PERM_SET(IND, ENABLE),
                            BK_PERM_SET(RI, ENABLE) | BK_PERM_SET(UUID_LEN, UUID_128), 512 },
    [SVR_FFF6_TX_CFG] = { BEKEN_ATT_DESC_CLIENT_CHAR_CFG_128, BK_PERM_SET(RD, ENABLE) | BK_PERM_SET(WRITE_REQ, ENABLE), 0,
                          2 },
#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
    [SVR_FFF6_C3_DECL] = { BEKEN_ATT_DECL_CHARACTERISTIC_128, BK_PERM_SET(RD, ENABLE) | BK_PERM_SET(NTF, ENABLE), 0, 0 },
    [SVR_FFF6_C3_VALUE] = { UUID_CHIPoBLEChar_C3, BK_PERM_SET(RD, ENABLE),
                            BK_PERM_SET(RI, ENABLE) | BK_PERM_SET(UUID_LEN, UUID_128), 512 },
#endif
};
#elif (CFG_BLE_VERSION == BLE_VERSION_5_2)
bk_attm_desc_t svr_fff6_att_db[SVR_FFF6_MAX] = {
    //  Service Declaration
    [SVR_FFF6_IDX_SVC] = { BEKEN_ATT_DECL_PRIMARY_SERVICE_128, PROP(RD), 2 },

    //  Level Characteristic Declaration
    [SVR_FFF6_RX_DECL] = { BEKEN_ATT_DECL_CHARACTERISTIC_128, PROP(RD), 0 },
    //  Level Characteristic Value  BK_PERM_RIGHT_ENABLE WRITE_REQ_POS  RI_POS  UUID_LEN_POS  BK_PERM_RIGHT_UUID_128
    [SVR_FFF6_RX_VALUE] = { UUID_CHIPoBLECharact_RX, PROP(WR)|ATT_UUID(128), 512|OPT(NO_OFFSET) },
    /// RD_POS  NTF_POS
    [SVR_FFF6_TX_DECL] = { BEKEN_ATT_DECL_CHARACTERISTIC_128, PROP(RD)| PROP(N), 0 },
    ////  UUID_LEN_POS   BK_PERM_RIGHT_UUID_128 RD_POS
    [SVR_FFF6_TX_VALUE] = { ChipUUID_CHIPoBLECharact_TX, PROP(RD)| PROP(I)|ATT_UUID(128), 512|OPT(NO_OFFSET) },
    [SVR_FFF6_TX_CFG] = { BEKEN_ATT_DESC_CLIENT_CHAR_CFG_128, PROP(RD)| PROP(WR), 2|OPT(NO_OFFSET) },
#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
    [SVR_FFF6_C3_DECL] = { BEKEN_ATT_DECL_CHARACTERISTIC_128, PROP(RD)| PROP(N), 0 },
    [SVR_FFF6_C3_VALUE] = { UUID_CHIPoBLEChar_C3, PROP(RD)|ATT_UUID(128), 512 },
#endif
};
#endif // CFG_BLE_VERSION

} // namespace

BLEManagerImpl BLEManagerImpl::sInstance;

int BLEManagerImpl::beken_ble_init(void)
{
    int status;

    struct bk_ble_db_cfg ble_db_cfg;

    ble_db_cfg.att_db      = svr_fff6_att_db;
    ble_db_cfg.att_db_nb   = SVR_FFF6_MAX;
    ble_db_cfg.prf_task_id = 0;
    ble_db_cfg.start_hdl   = 0;
#if (CFG_BLE_VERSION == BLE_VERSION_5_1)
    ble_db_cfg.svc_perm    = BK_PERM_SET(SVC_UUID_LEN, UUID_16);
#elif (CFG_BLE_VERSION == BLE_VERSION_5_2)
    ble_db_cfg.svc_perm    = SVC_UUID(16);
#endif
    memcpy(&(ble_db_cfg.uuid[0]), &_svc_uuid[0], 16);
    status = bk_ble_create_db(&ble_db_cfg);

    if (status != ERR_SUCCESS)
    {
        return -1;
    }
    return 0;
}

CHIP_ERROR BLEManagerImpl::_Init()
{
    CHIP_ERROR err;
    param_conn_idx = 0;

    // Initialize the CHIP BleLayer.
    ReturnErrorOnFailure(BleLayer::Init(this, this, &DeviceLayer::SystemLayer()));

    mServiceMode = ConnectivityManager::kCHIPoBLEServiceMode_Enabled;
    memset(mBleConnections, 0, sizeof(mBleConnections));
    ble_set_notice_cb(ble_event_notice);

    // Set related flags
    mFlags.ClearAll();
    mFlags.Set(Flags::kAdvertisingEnabled, CHIP_DEVICE_CONFIG_CHIPOBLE_ENABLE_ADVERTISING_AUTOSTART);
    mAdvertisingMode = ConnectivityManager::kFastAdvertising;

    char ssid[DeviceLayer::Internal::kMaxWiFiSSIDLength];
    if (CHIP_NO_ERROR == PersistedStorage::KeyValueStoreMgr().Get( "wifi-ssid", ssid, DeviceLayer::Internal::kMaxWiFiSSIDLength)) {
        mFlags.Set(Flags::kDeviceCommissioned);
    }

    if (!mFlags.Has(Flags::kBEKENBLEAdvTimer))
    {
        uint32_t bleAdvTimeoutMs = CHIP_DEVICE_CONFIG_BLE_ADVERTISING_INTERVAL_CHANGE_TIME;
        bleFastAdvTimer          = xTimerCreate("", (bleAdvTimeoutMs / 2), pdFALSE, NULL, ble_adv_timer_timeout_handle);
        if (bleAdvTimeoutMs) {
            mFlags.Set(Flags::kBEKENBLEAdvTimer);
        } else {
            ChipLogError(DeviceLayer, "Failed create bleFastAdvTimer timer");
        }
    }
    PlatformMgr().ScheduleWork(DriveBLEState, 0);

#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
    // Prepare C3 private payload once to keep BLE read callback stack usage low.
    err = PrepareC3PrivatePayloadCache();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "PrepareC3PrivatePayloadCache failed: %" CHIP_ERROR_FORMAT, err.Format());
    }
#endif

    return CHIP_NO_ERROR;
}

void BLEManagerImpl::HandleTXCharRead(void * param)
{
    CHIPoBLEConState * conState;
    read_req_t * r_req = (read_req_t *) param;
    if (param == NULL)
    {
        ChipLogError(DeviceLayer, "HandleTXCharRead failed: %p", param);
    }
    r_req->length = 0;
}

void BLEManagerImpl::HandleTXCharCCCDRead(void * param)
{
    CHIPoBLEConState * conState;
    read_req_t * r_req = (read_req_t *) param;
    conState               = GetConnectionState(r_req->conn_idx);

    if (param == NULL)
    {
        ChipLogError(DeviceLayer, "HandleTXCharCCCDRead failed");
        return;
    }

    if (conState != NULL)
    {
        r_req->value[0] = conState->subscribed ? 1 : 0;
        r_req->value[1] = 0;
        r_req->length   = 2;
    }
    else
    {
        ChipLogError(DeviceLayer, "conState failed: %p", conState);
        r_req->length = 0;
    }
}

void BLEManagerImpl::HandleTXCharCCCDWrite(int conn_id, int notificationsEnabled, int indicationsEnabled)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    // If the client has requested to enabled indications/notifications
    if (indicationsEnabled || notificationsEnabled)
    {
        // If indications are not already enabled for the connection...
        if (!IsSubscribed(conn_id))
        {
            // Record that indications have been enabled for this connection.
            err = SetSubscribed(conn_id);
            VerifyOrExit(err != CHIP_ERROR_NO_MEMORY, err = CHIP_NO_ERROR);
            SuccessOrExit(err);
        }
    }
    else
    {
        // If indications had previously been enabled for this connection, record that they are no longer enabled.
        UnsetSubscribed(conn_id);
    }

    // Post an event to the Chip queue to process either a CHIPoBLE Subscribe or Unsubscribe based on
    // whether the client is enabling or disabling indications.
    {
        ChipDeviceEvent event;
        event.Type = (indicationsEnabled || notificationsEnabled) ? DeviceEventType::kCHIPoBLESubscribe
                                                                  : DeviceEventType::kCHIPoBLEUnsubscribe;
        event.CHIPoBLESubscribe.ConId = conn_id;
        PlatformMgr().PostEventOrDie(&event);
    }

    ChipLogProgress(DeviceLayer, "CHIPoBLE %s received",
                    (indicationsEnabled || notificationsEnabled) ? "subscribe" : "unsubscribe");

exit:
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HandleTXCharCCCDWrite() failed: %s", ErrorStr(err));
    }

    return;
}

uint16_t BLEManagerImpl::_NumConnections(void)
{
    uint16_t numCons = 0;

    for (uint8_t i = 0; i < kMaxConnections; i++)
    {
        if (mBleConnections[i].allocated == 1)
        {
            numCons += 1;
        }
    }

    return numCons;
}

CHIP_ERROR BLEManagerImpl::HandleGAPDisconnect(uint16_t conn_id, uint16_t disc_cause)
{
    if (RemoveConnection(conn_id))
    {
        CHIP_ERROR disconReason;
        switch (disc_cause)
        {
        case 0x13:
        case 0x08:
            disconReason = BLE_ERROR_REMOTE_DEVICE_DISCONNECTED;
            break;
        case 0x16:
            disconReason = BLE_ERROR_APP_CLOSED_CONNECTION;
            break;
        default:
            disconReason = BLE_ERROR_CHIPOBLE_PROTOCOL_ABORT;
            break;
        }
        ChipDeviceEvent event;
        event.Type                           = DeviceEventType::kCHIPoBLEConnectionError;
        event.CHIPoBLEConnectionError.ConId  = conn_id;
        event.CHIPoBLEConnectionError.Reason = disconReason;
        PlatformMgr().PostEventOrDie(&event);
    }

    // Force a reconfiguration of advertising in case we switched to non-connectable mode when
    // the BLE connection was established.
    //mFlags.Set(Flags::kAdvertisingRefreshNeeded);
    return CHIP_NO_ERROR;
}

bool BLEManagerImpl::RemoveConnection(uint8_t connectionHandle)
{
    CHIPoBLEConState * bleConnState = GetConnectionState(connectionHandle);
    bool status                     = false;

    if (bleConnState != NULL)
    {
        bleConnState->Reset();
        memset(bleConnState, 0, sizeof(CHIPoBLEConState));
        status = true;
    }
    return status;
}

BLEManagerImpl::CHIPoBLEConState * BLEManagerImpl::GetConnectionState(uint8_t connectionHandle, bool allocate)
{
    uint8_t freeIndex = kMaxConnections;

    for (uint8_t i = 0; i < kMaxConnections; i++)
    {
        if (mBleConnections[i].allocated == 1)
        {
            if (mBleConnections[i].conn_idx == connectionHandle)
            {
                return &mBleConnections[i];
            }
        }

        else if (i < freeIndex)
        {
            freeIndex = i;
        }
    }

    if (allocate)
    {
        if (freeIndex < kMaxConnections)
        {
            mBleConnections[freeIndex].Set(connectionHandle);
            return &mBleConnections[freeIndex];
        }

        ChipLogError(DeviceLayer, "Failed to allocate CHIPoBLEConState");
    }

    return NULL;
}

CHIP_ERROR BLEManagerImpl::_SetAdvertisingEnabled(bool val)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    VerifyOrExit(mServiceMode != ConnectivityManager::kCHIPoBLEServiceMode_NotSupported, err = CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);

    if (mFlags.Has(Flags::kAdvertisingEnabled) != val)
    {
        mFlags.Set(Flags::kAdvertisingEnabled, val);
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
    }

exit:
    return err;
}

CHIP_ERROR BLEManagerImpl::_SetAdvertisingMode(BLEAdvertisingMode mode)
{
    if (mAdvertisingMode != mode)
    {
        // fast -> slow
        ChipLogProgress(DeviceLayer, "mAdvertisingMode: fast -> slow");
        mAdvertisingMode = mode;
    }
    else
    {
        // slow -> stop
        ChipLogProgress(DeviceLayer, "mAdvertisingMode: slow -> stop");
        mFlags.Clear(Flags::kAdvertisingEnabled);
    }
    mFlags.Set(Flags::kAdvertisingRefreshNeeded);
    PlatformMgr().ScheduleWork(DriveBLEState, 0);
    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::_GetDeviceName(char * buf, size_t bufSize)
{
    if (strlen(mDeviceName) >= bufSize)
    {
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }
    strcpy(buf, mDeviceName);
    return CHIP_NO_ERROR;
}

CHIP_ERROR BLEManagerImpl::_SetDeviceName(const char * deviceName)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    VerifyOrExit(mServiceMode != ConnectivityManager::kCHIPoBLEServiceMode_NotSupported, err = CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);

    if (deviceName != NULL && deviceName[0] != 0)
    {
        VerifyOrExit(strlen(deviceName) >= kMaxDeviceNameLength, err = CHIP_ERROR_INVALID_ARGUMENT);
        strcpy(mDeviceName, deviceName);
        mFlags.Set(Flags::kDeviceNameSet);
        ChipLogProgress(DeviceLayer, "Setting device name to : \"%s\"", deviceName);
    }
    else
    {
        mDeviceName[0] = 0;
        mFlags.Clear(Flags::kDeviceNameSet);
    }

exit:
    return err;
}

void BLEManagerImpl::_OnPlatformEvent(const ChipDeviceEvent * event)
{
    switch (event->Type)
    {
    // Platform specific events
    case DeviceEventType::kCHIPoBLESubscribe:
        HandleSubscribeReceived(event->CHIPoBLESubscribe.ConId, &CHIP_BLE_SVC_ID, &ChipUUID_CHIPoBLEChar_TX);
        {
            ChipDeviceEvent connEstEvent;
            connEstEvent.Type = DeviceEventType::kCHIPoBLEConnectionEstablished;
            PlatformMgr().PostEventOrDie(&connEstEvent);
        }
        break;

    case DeviceEventType::kCHIPoBLEUnsubscribe: {
        ChipLogProgress(DeviceLayer, "_OnPlatformEvent kCHIPoBLEUnsubscribe");
        HandleUnsubscribeReceived(event->CHIPoBLEUnsubscribe.ConId, &CHIP_BLE_SVC_ID, &ChipUUID_CHIPoBLEChar_TX);
    }
    break;

    case DeviceEventType::kCHIPoBLEWriteReceived: {
        ChipLogProgress(DeviceLayer, "_OnPlatformEvent kCHIPoBLEWriteReceived");
        HandleWriteReceived(event->CHIPoBLEWriteReceived.ConId, &CHIP_BLE_SVC_ID, &ChipUUID_CHIPoBLEChar_RX,
                            PacketBufferHandle::Adopt(event->CHIPoBLEWriteReceived.Data));
    }
    break;

    case DeviceEventType::kCHIPoBLEConnectionError: {
        ChipLogProgress(DeviceLayer, "_OnPlatformEvent kCHIPoBLEConnectionError");
        HandleConnectionError(event->CHIPoBLEConnectionError.ConId, event->CHIPoBLEConnectionError.Reason);
    }
    break;

    case DeviceEventType::kCHIPoBLEIndicateConfirm: {
        ChipLogProgress(DeviceLayer, "_OnPlatformEvent kCHIPoBLEIndicateConfirm");
        HandleIndicationConfirmation(event->CHIPoBLEIndicateConfirm.ConId, &CHIP_BLE_SVC_ID, &ChipUUID_CHIPoBLEChar_TX);
    }
    break;

    case DeviceEventType::kServiceProvisioningChange:
    case DeviceEventType::kWiFiConnectivityChange:
        ChipLogProgress(DeviceLayer, "Updating advertising data");
        break;
    case DeviceEventType::kCommissioningComplete:
        mFlags.Set(Flags::kDeviceCommissioned);
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
        break;

    default:
        ChipLogProgress(DeviceLayer, "_OnPlatformEvent default:  event->Type = %d", event->Type);
        break;
    }
}

CHIP_ERROR BLEManagerImpl::SubscribeCharacteristic(BLE_CONNECTION_OBJECT conId, const ChipBleUUID * svcId, const ChipBleUUID * charId)
{
    ChipLogProgress(DeviceLayer, "BLEManagerImpl::SubscribeCharacteristic() not supported");
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR BLEManagerImpl::UnsubscribeCharacteristic(BLE_CONNECTION_OBJECT conId, const ChipBleUUID * svcId, const ChipBleUUID * charId)
{
    ChipLogProgress(DeviceLayer, "BLEManagerImpl::UnsubscribeCharacteristic() not supported");
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR BLEManagerImpl::CloseConnection(BLE_CONNECTION_OBJECT conId)
{
    CHIP_ERROR err;
    ChipLogProgress(DeviceLayer, "Closing BLE GATT connection (con %u)", conId);
    uint32_t delay_time = 20;
    int ret;

    do
    {
        ret = bk_ble_disconnect(conId, beken_ble_cmd_cb);
        if (ERR_SUCCESS == ret)
        {
            break;
        }

        rtos_delay_milliseconds(10);
        delay_time--;
    } while (delay_time);
    // Beken Ble close function
    err = MapBLEError(ret);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "bk_ble_disconnect() failed: %s", ErrorStr(err));
    }

    return CHIP_NO_ERROR;
}

uint16_t BLEManagerImpl::GetMTU(BLE_CONNECTION_OBJECT conId) const
{
#if (CFG_BLE_VERSION == BLE_VERSION_5_1)
    CHIPoBLEConState * conState = const_cast<BLEManagerImpl *>(this)->GetConnectionState(conId);
    return (conState != NULL) ? conState->mtu : 0;
#elif (CFG_BLE_VERSION == BLE_VERSION_5_2)
    uint16_t mtu = 0;
    app_ble_mtu_get(0, &mtu);
    return mtu;
#endif
}

CHIP_ERROR BLEManagerImpl::SendWriteRequest(BLE_CONNECTION_OBJECT conId, const ChipBleUUID * svcId, const ChipBleUUID * charId,
                                      PacketBufferHandle pBuf)
{
    ChipLogError(DeviceLayer, "BLEManagerImpl::SendWriteRequest() not supported");
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

void BLEManagerImpl::NotifyChipConnectionClosed(BLE_CONNECTION_OBJECT conId)
{
    // Nothing to do
}

CHIP_ERROR BLEManagerImpl::SendIndication(BLE_CONNECTION_OBJECT conId, const ChipBleUUID * svcId, const ChipBleUUID * charId,
                                    PacketBufferHandle data)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    int ret;

    VerifyOrExit(IsSubscribed(conId), err = CHIP_ERROR_INVALID_ARGUMENT);
    ret = bk_ble_send_ind_value(data->DataLength(), data->Start(), 0, SVR_FFF6_TX_VALUE);
    err = MapBLEError(ret);
exit:
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "BLEManagerImpl::SendIndication() failed: %s", ErrorStr(err));
        return err;
    }

    return err;
}

/*******************************************************************************
 * Private functions
 *******************************************************************************/

CHIP_ERROR BLEManagerImpl::ConfigureAdvertisingData(void)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    // If the device name is not specified, generate a CHIP-standard name based on the bottom digits of the Chip device id.
    uint16_t discriminator;
    SuccessOrExit(err = GetCommissionableDataProvider()->GetSetupDiscriminator(discriminator));

    if (!mFlags.Has(Flags::kDeviceNameSet))
    {
        snprintf(mDeviceName, sizeof(mDeviceName), "%s%04u", CHIP_DEVICE_CONFIG_BLE_DEVICE_NAME_PREFIX, discriminator);
        mDeviceName[kMaxDeviceNameLength] = 0;
    }

    // Configure the BLE device name.
    mFlags.Set(Flags::kDeviceNameDefSet);
    ble_appm_set_dev_name(kMaxDeviceNameLength, (uint8_t *) mDeviceName);
exit:
    return err;
}

CHIP_ERROR BLEManagerImpl::MapBLEError(int bleErr)
{
    switch (bleErr)
    {
    case ERR_SUCCESS:
        return CHIP_NO_ERROR;
    default:
        return CHIP_ERROR_INCORRECT_STATE;
    }
}

// 遥控 BLE 主机 demo 入口（在 lighting 应用里实现）
extern "C" void ql_ble_demo_central_thread_creat(void);
extern "C" void ql_ble_remote_reset_ble_state(void);
extern "C" void ql_ble_remote_set_suspend(int enable);
extern "C" void ql_ble_remote_set_scan_enable(int enable);
extern "C" void ql_ble_remote_install_notice_cb(void);
void BLEManagerImpl::DriveBLEState(void)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    if (mServiceMode != ConnectivityManager::kCHIPoBLEServiceMode_Enabled)
    {
        goto exit;
    }

    if (!mFlags.Has(Flags::kBekenBLESGATTSReady))
    {
        err = MapBLEError(beken_ble_init()); // BLE_5_CREATE_DB
        goto exit;
    }

    if ((!mFlags.Has(Flags::kDeviceNameSet)) && (!mFlags.Has(Flags::kDeviceNameDefSet)))
    {
        ChipLogProgress(DeviceLayer, "appm set dev name...");
        err = ConfigureAdvertisingData();
        if (err == CHIP_NO_ERROR) {
            ChipLogDetail(DeviceLayer, "appm set dev name success");
        } else {
            ChipLogError(DeviceLayer, "appm set dev name failed");
        }
    }

    if (mFlags.Has(Flags::kDeviceConnected))
    {
        if (mFlags.Has(Flags::kBEKENBLEAdvTimerRun))
        {
            ChipLogProgress(DeviceLayer, "cancel bleFastAdvTimer timer...");
            CancelBleAdvTimeoutTimer();
            goto exit;
        }
    } // kDeviceConnected

    if (mFlags.Has(Flags::kAdvertisingRefreshNeeded))
    {
        if (mFlags.Has(Flags::kBEKENBLEADVStarted))
        {
            if (adv_actv_idx >= BLE_ACTIVITY_MAX || app_ble_actv_state_get(adv_actv_idx) != ACTV_ADV_STARTED)
            {
                ChipLogDetail(DeviceLayer, "skip stop advertising: adv not started, actv_idx=%d", adv_actv_idx);
                mFlags.Clear(Flags::kBEKENBLEADVStarted);
            }
            else
            {
                ChipLogProgress(DeviceLayer, "stop advertising...");
                ChipLogDetail(DeviceLayer, "adv_actv_idx=%d", adv_actv_idx);
                err = MapBLEError(bk_ble_stop_advertising(adv_actv_idx, beken_ble_cmd_cb)); // BLE_STOP_ADV
                goto exit;
            }
        }
        if (mFlags.Has(Flags::kBEKENBLEAdvTimerRun))
        {
            ChipLogProgress(DeviceLayer, "cancel bleFastAdvTimer timer...");
            CancelBleAdvTimeoutTimer();
            goto exit;
        }
        if (mFlags.Has(Flags::kBEKENBLEADVCreate))
        {
            ChipLogProgress(DeviceLayer, "delete advertising...");
            ChipLogDetail(DeviceLayer, "adv_actv_idx=%d", adv_actv_idx);
            err = MapBLEError(bk_ble_delete_advertising(adv_actv_idx, beken_ble_cmd_cb)); // BLE_DELETE_ADV
            goto exit;
        }
    } // kAdvertisingRefreshNeeded

    if (mFlags.Has(Flags::kAdvertisingEnabled) && !mFlags.Has(Flags::kDeviceConnected))
    {
        if (!mFlags.Has(Flags::kBEKENBLEADVCreate))
        {
            uint16_t adv_int_min;
            uint16_t adv_int_max;
            if(mAdvertisingMode == ConnectivityManager::kFastAdvertising)
            {
                adv_int_min = 40;
                adv_int_max = 40;
                ChipLogProgress(DeviceLayer, "create fast advertising...");
            } else {
                adv_int_min = 800;
                adv_int_max = 800;
                ChipLogProgress(DeviceLayer, "create slow advertising...");
            }
            adv_actv_idx = app_ble_get_idle_actv_idx_handle();
            if(BLE_ACTIVITY_MAX <= adv_actv_idx)
            {
                ChipLogError(DeviceLayer, "app_ble_get_idle_actv_idx_handle error");
                err = MapBLEError(-1);
                goto exit;
            }
            ChipLogDetail(DeviceLayer, "adv_actv_idx=%d, adv_int_min=%d, adv_int_max=%d", adv_actv_idx, adv_int_min, adv_int_max);
            err = MapBLEError(bk_ble_create_advertising(adv_actv_idx, 7, adv_int_min, adv_int_max, beken_ble_cmd_cb)); // BLE_CREATE_ADV
            goto exit;
        } // kBEKENBLEADVCreate

        if (!mFlags.Has(Flags::kBEKENBLEADVSetData))
        {
            ChipBLEDeviceIdentificationInfo deviceIdInfo;
            uint8_t advData[MAX_ADV_DATA_LEN] = { 0 };
            uint8_t index                     = 0;
            /**************** Prepare advertising data *******************************************/
            memset(advData, 0, sizeof(advData));
#if (CFG_BLE_VERSION == BLE_VERSION_5_1)
            advData[index++] = 0x02;                     // length
            advData[index++] = CHIP_ADV_DATA_TYPE_FLAGS; // AD type : flags
            advData[index++] = CHIP_ADV_DATA_FLAGS;      // AD value
#endif
            advData[index++] = static_cast<uint8_t>(sizeof(deviceIdInfo) + CHIP_ADV_SHORT_UUID_LEN + 1); // length
            advData[index++] = 0x16;                                      // AD type: (Service Data - 16-bit UUID)
            advData[index++] = static_cast<uint8_t>(_svc_uuid[0] & 0xFF); // AD value
            advData[index++] = static_cast<uint8_t>(_svc_uuid[1] & 0xFF); // AD value
            err              = ConfigurationMgr().GetBLEDeviceIdentificationInfo(deviceIdInfo);
            if (err != CHIP_NO_ERROR)
            {
                ChipLogError(DeviceLayer, "GetBLEDeviceIdentificationInfo(): %s", ErrorStr(err));
                goto exit;
            }
#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
            deviceIdInfo.SetAdditionalDataFlag(true);
#endif
            VerifyOrExit(index + sizeof(deviceIdInfo) <= sizeof(advData), err = CHIP_ERROR_OUTBOUND_MESSAGE_TOO_BIG);

            memcpy(&advData[index], &deviceIdInfo, sizeof(deviceIdInfo));
            index = static_cast<uint8_t>(index + sizeof(deviceIdInfo));
            ChipLogProgress(DeviceLayer, "set adv data...");
            bk_ble_set_adv_data(adv_actv_idx, advData, index, beken_ble_cmd_cb);
            goto exit;
        } // kBEKENBLEADVSetData

        if (!mFlags.Has(Flags::kBEKENBLEADVSetRsp))
        {
            uint8_t advData[MAX_ADV_DATA_LEN] = { 0 };
            uint8_t index = 0;
            memset(advData, 0, sizeof(advData));
            index++;
            advData[index++] = 0x08;
            memcpy(&advData[2], "BK_MATTER", strlen("BK_MATTER"));
            index += strlen("BK_MATTER");
            advData[0] = index;
            ChipLogProgress(DeviceLayer, "set rsp data...");
            bk_ble_set_scan_rsp_data(adv_actv_idx, advData, index + 1, beken_ble_cmd_cb);
            goto exit;
        } // kBEKENBLEADVSetRsp

        // must disconnect before start
        if (!mFlags.Has(Flags::kBEKENBLEADVStarted))
        {
            ChipLogProgress(DeviceLayer, "start advertising...");
            ChipLogDetail(DeviceLayer, "adv_actv_idx=%d, duration=%d", adv_actv_idx, 0);
            err = MapBLEError(bk_ble_start_advertising(adv_actv_idx, 0, beken_ble_cmd_cb)); // BLE_START_ADV
            goto exit;
        }

        if (!mFlags.Has(Flags::kBEKENBLEAdvTimerRun))
        {
            ChipLogProgress(DeviceLayer, "start bleFastAdvTimer timer...");
            StartBleAdvTimeoutTimer();
            goto exit;
        }
    } // kAdvertisingEnabled


#if CFG_BLE_USE_DYN_RAM
    // 设备已配网、当前无连接且已停止 BLE 广播后，退出 CHIPoBLE 线程，
    // 然后启动 BLE 主机模式扫描遥控器，避免主从同时开启。
    if (!mFlags.Has(Flags::kDeviceConnected) && mFlags.Has(Flags::kDeviceCommissioned) && !mFlags.Has(Flags::kAdvertisingEnabled))
    {
        ChipLogProgress(DeviceLayer, "ble thread exit");
        ble_thread_exit();
        mServiceMode = ConnectivityManager::kCHIPoBLEServiceMode_Disabled;
        // Stop any existing central task and re-init BLE for remote scanning.
        ql_ble_remote_set_suspend(1);
        ql_ble_remote_reset_ble_state();
        ql_ble_remote_set_suspend(0);
        ql_ble_remote_install_notice_cb();
        ql_ble_demo_central_thread_creat();
        ql_ble_remote_set_scan_enable(1);
    }
#endif

exit:
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Disabling CHIPoBLE service due to error: %s", ErrorStr(err));
        mServiceMode = ConnectivityManager::kCHIPoBLEServiceMode_Disabled;
    }
}

void BLEManagerImpl::DriveBLEState(intptr_t arg)
{
    sInstance.DriveBLEState();
}

void BLEManagerImpl::DriveBLEExtPerf(intptr_t arg)
{
    int arg_int = static_cast<int>(arg);
    int event   = arg_int & 0xFFU;
    switch (event)
    {
    case DriveBLEExtPerfEvt_DISCONNECT: {
        uint8_t conn_indx = (arg_int >> 8) & 0xFFU;
        sInstance.CloseConnection(conn_indx);
    }
    break;
    default:
        break;
    }
}

/*******************************************************************************
 * FreeRTOS Task Management Functions
 *******************************************************************************/
void BLEManagerImpl::ble_adv_timer_timeout_handle(TimerHandle_t xTimer)
{
    ChipLogProgress(DeviceLayer, "enter %s", __FUNCTION__);
    sInstance.SetAdvertisingMode(ConnectivityManager::kSlowAdvertising);
}

void BLEManagerImpl::ble_updatepara_timer_timeout_handle(TimerHandle_t xTimer)
{
    PlatformMgr().ScheduleWork(ble_change_para, 0);
}

void BLEManagerImpl::CancelBleAdvTimeoutTimer(void)
{
    if (xTimerIsTimerActive(bleFastAdvTimer) && xTimerStop(bleFastAdvTimer, 10) != pdPASS)
    {
        ChipLogError(DeviceLayer, "Failed to stop bleFastAdvTimer timer");
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
    }
    else
    {
        ChipLogDetail(DeviceLayer, "cancel bleFastAdvTimer timer success");
        mFlags.Clear(Flags::kBEKENBLEAdvTimerRun);
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
    }
}

void BLEManagerImpl::StartBleAdvTimeoutTimer()
{
    uint32_t bleAdvTimeoutMs = CHIP_DEVICE_CONFIG_BLE_ADVERTISING_INTERVAL_CHANGE_TIME;
    if (mAdvertisingMode == ConnectivityManager::kSlowAdvertising)
    {
        bleAdvTimeoutMs = 15 * 60 * 1000;
    }
    xTimerStart(bleFastAdvTimer, BEKEN_WAIT_FOREVER);
    if (xTimerChangePeriod(bleFastAdvTimer, bleAdvTimeoutMs / portTICK_PERIOD_MS, 100) != pdPASS)
    {
        ChipLogError(DeviceLayer, "Failed to start bleFastAdvTimer timer");
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
    }
    else
    {
        ChipLogDetail(DeviceLayer, "start bleFastAdvTimer timer success");
        mFlags.Set(Flags::kBEKENBLEAdvTimerRun);
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
    }
}

CHIP_ERROR BLEManagerImpl::SetSubscribed(uint16_t conId)
{
    CHIPoBLEConState * bleConnState = GetConnectionState(conId, false);

    if (bleConnState != NULL)
    {
        bleConnState->subscribed = 1;
        return CHIP_NO_ERROR;
    }

    return CHIP_ERROR_NO_MEMORY;
}

bool BLEManagerImpl::UnsetSubscribed(uint16_t conId)
{
    CHIPoBLEConState * bleConnState = GetConnectionState(conId, false);

    if (bleConnState != NULL)
    {
        bleConnState->subscribed = 0;
        return true;
    }

    return false;
}

bool BLEManagerImpl::IsSubscribed(uint16_t conId)
{
    CHIPoBLEConState * bleConnState = GetConnectionState(conId, false);

    if (bleConnState != NULL)
    {
        if (bleConnState->subscribed)
        {
            return true;
        }
    }
    return false;
}

void BLEManagerImpl::HandleRXCharWrite(uint8_t * p_value, uint16_t len, uint8_t conn_id)
{
    CHIP_ERROR err         = CHIP_NO_ERROR;
    PacketBufferHandle buf = System::PacketBufferHandle::New(len, 0);
    memcpy(buf->Start(), p_value, len);
    buf->SetDataLength(len);

    // Post an event to the Chip queue to deliver the data into the Chip stack.
    {
        ChipDeviceEvent event;
        event.Type                        = DeviceEventType::kCHIPoBLEWriteReceived;
        event.CHIPoBLEWriteReceived.ConId = (uint16_t) conn_id;
        event.CHIPoBLEWriteReceived.Data  = std::move(buf).UnsafeRelease();
        PlatformMgr().PostEventOrDie(&event);
    }

    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "HandleRXCharWrite() failed: %s", ErrorStr(err));
    }
}

void BLEManagerImpl::HandleTXCharConfirm(CHIPoBLEConState * conState, int status)
{
    // If the confirmation was successful...
    if (status == 0)
    {
        // Post an event to the Chip queue to process the indicate confirmation.
        ChipDeviceEvent event;
        event.Type                          = DeviceEventType::kCHIPoBLEIndicateConfirm;
        event.CHIPoBLEIndicateConfirm.ConId = conState->conn_idx;
        PlatformMgr().PostEventOrDie(&event);
    }
    else
    {
        ChipDeviceEvent event;
        event.Type                           = DeviceEventType::kCHIPoBLEConnectionError;
        event.CHIPoBLEConnectionError.ConId  = conState->conn_idx;
        event.CHIPoBLEConnectionError.Reason = BLE_ERROR_CHIPOBLE_PROTOCOL_ABORT;
        PlatformMgr().PostEventOrDie(&event);
    }
}

void BLEManagerImpl::beken_ble_cmd_cb(ble_cmd_t cmd, ble_cmd_param_t * param)
{
    if(param->status != ERR_SUCCESS) {
        ChipLogError(DeviceLayer, "error:cmd=%d, param->cmd_idx=%d, param->status=%d", cmd, param->cmd_idx, param->status);
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
        return;
    }
    switch (cmd)
    {
    case BLE_CREATE_ADV:
        ChipLogDetail(DeviceLayer, "create advertising success");
        sInstance.mFlags.Set(Flags::kBEKENBLEADVCreate);
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
        break;
    case BLE_SET_ADV_DATA:
        ChipLogDetail(DeviceLayer, "set adv data success");
        sInstance.mFlags.Set(Flags::kBEKENBLEADVSetData);
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
        break;
    case BLE_SET_RSP_DATA:
        ChipLogDetail(DeviceLayer, "set rsp data success");
        sInstance.mFlags.Set(Flags::kBEKENBLEADVSetRsp);
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
        break;
    case BLE_START_ADV:
        ChipLogDetail(DeviceLayer, "start advertising success");
        sInstance.mFlags.Set(Flags::kBEKENBLEADVStarted);
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
        break;
    case BLE_STOP_ADV:
        ChipLogDetail(DeviceLayer, "stop advertising success");
        sInstance.mFlags.Clear(Flags::kBEKENBLEADVStarted);
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
        break;
    case BLE_CONN_UPDATE_PARAM:
        ChipLogDetail(DeviceLayer, "update conn para success");
        break;
    case BLE_DELETE_ADV:
        ChipLogDetail(DeviceLayer, "delete advertising success");
        sInstance.mFlags.Clear(Flags::kBEKENBLEADVCreate);
        sInstance.mFlags.Clear(Flags::kBEKENBLEADVStarted);
        sInstance.mFlags.Clear(Flags::kBEKENBLEADVSetData);
        sInstance.mFlags.Clear(Flags::kBEKENBLEADVSetRsp);
        sInstance.mFlags.Clear(Flags::kAdvertisingRefreshNeeded);
        sInstance.adv_actv_idx = kUnusedIndex;
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
        break;
    case BLE_CONN_DIS_CONN:
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
        break;
    default:
        break;
    }
}

#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
void BLEManagerImpl::HandleC3CharRead(void * param)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    chip::System::PacketBufferHandle bufferHandle;
    chip::System::PacketBufferTLVWriter writer;
    chip::TLV::TLVWriter innerWriter;
    read_req_t * r_req = (read_req_t *) param;
    chip::System::PacketBufferHandle tempBuffer = chip::System::PacketBufferHandle::New(chip::System::PacketBuffer::kMaxSize);

#if CHIP_ENABLE_ROTATING_DEVICE_ID && defined(CHIP_DEVICE_CONFIG_ROTATING_DEVICE_ID_UNIQUE_ID)
    AdditionalDataPayloadGeneratorParams additionalDataPayloadParams;
    uint8_t rotatingDeviceIdUniqueId[ConfigurationManager::kRotatingDeviceIDUniqueIDLength] = {};
    MutableByteSpan rotatingDeviceIdUniqueIdSpan(rotatingDeviceIdUniqueId);
    uint8_t rotatingDeviceIdInternalBuffer[RotatingDeviceId::kMaxLength];
    MutableByteSpan rotatingDeviceIdBuffer(rotatingDeviceIdInternalBuffer);
#endif

    VerifyOrExit(!tempBuffer.IsNull(), err = CHIP_ERROR_NO_MEMORY);
    writer.Init(std::move(tempBuffer));

    if (r_req->value == NULL)
    {
        ChipLogError(DeviceLayer, "param->value == NULL");
        return;
    }

    err = writer.OpenContainer(chip::TLV::AnonymousTag(), chip::TLV::kTLVType_Structure, innerWriter);
    SuccessOrExit(err);

#if CHIP_ENABLE_ROTATING_DEVICE_ID && defined(CHIP_DEVICE_CONFIG_ROTATING_DEVICE_ID_UNIQUE_ID)
    err = DeviceLayer::GetDeviceInstanceInfoProvider()->GetRotatingDeviceIdUniqueId(rotatingDeviceIdUniqueIdSpan);
    SuccessOrExit(err);
    err = ConfigurationMgr().GetLifetimeCounter(additionalDataPayloadParams.rotatingDeviceIdLifetimeCounter);
    SuccessOrExit(err);
    additionalDataPayloadParams.rotatingDeviceIdUniqueId = rotatingDeviceIdUniqueIdSpan;
    err = AdditionalDataPayloadGenerator().generateRotatingDeviceIdAsBinary(additionalDataPayloadParams, rotatingDeviceIdBuffer);
    SuccessOrExit(err);

    err = innerWriter.Put(chip::TLV::ContextTag(chip::SetupPayloadData::kRotatingDeviceIdTag), rotatingDeviceIdBuffer);
    SuccessOrExit(err);
#endif /* CHIP_ENABLE_ROTATING_DEVICE_ID */

    if (sC3PrivatePayloadReady)
    {
        err = innerWriter.Put(chip::TLV::ContextTag(kAdditionalDataPrivateTag),
                              chip::ByteSpan(sC3EncryptedPrivatePayload, sC3EncryptedPrivatePayloadLen));
        SuccessOrExit(err);
    }
    else
    {
        ChipLogError(DeviceLayer, "C3 private payload cache not ready, skip private tag this read");
    }

    err = writer.CloseContainer(innerWriter);
    SuccessOrExit(err);
    err = writer.Finalize(&bufferHandle);
    SuccessOrExit(err);

    memcpy(r_req->value, bufferHandle->Start(), bufferHandle->DataLength());
    r_req->length = bufferHandle->DataLength();
exit:
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Failed to generate TLV encoded Additional Data");
    }
    return;
}
#endif

void BLEManagerImpl::ble_change_para(intptr_t arg)
{
    uint16_t min_interval = 6;
    uint16_t max_interval = 12;//modify for ios Home 6;
    uint16_t latency      = 0;
    uint16_t timeout      = 500;

    bk_ble_update_param(sInstance.param_conn_idx, min_interval, max_interval, latency, timeout, beken_ble_cmd_cb);
}

void BLEManagerImpl::ble_para_cfm(void * arg)
{
    conn_param_t *d_ind = (conn_param_t *)arg;

    // Always accept connection parameter updates to avoid premature disconnects during commissioning.
    app_ble_send_conn_param_update_cfm(d_ind->conn_idx, true);
}

#ifdef BEKEN_SOC_BK7231
void BLEManagerImpl::vBleUpdateParam(void)
{
    ChipLogProgress(DeviceLayer, "[%s]",__func__);
    PlatformMgr().ScheduleWork(ble_change_para, 0);
}
#endif

void BLEManagerImpl::ble_event_notice(ble_notice_t notice, void * param)
{
    static int conn_idx = 0;
    static bool sCccdWritten = false;

    if (ql_ble_remote_notice_cb != nullptr)
    {
        ql_ble_remote_notice_cb(notice, param);
    }
    switch (notice)
    {
    case BLE_5_REPORT_ADV:
        // Adv reports are consumed by the remote scan bridge; no CHIPoBLE handling needed.
        break;
    case BLE_5_WRITE_EVENT: {
        write_req_t * w_req = (write_req_t *) param;
        ChipLogProgress(DeviceLayer, "BLE_5_WRITE_EVENT att_idx=%u len=%u", w_req->att_idx,
                        static_cast<unsigned>(w_req->len));
        if (w_req->att_idx == SVR_FFF6_TX_CFG)
        {
            sCccdWritten = true;
            int notificationsEnabled = w_req->value[0] | (w_req->value[0] << 8);
            sInstance.HandleTXCharCCCDWrite(w_req->conn_idx, notificationsEnabled);
        }
        else if (w_req->att_idx == SVR_FFF6_RX_VALUE)
        {
            sInstance.HandleRXCharWrite((uint8_t *) &w_req->value[0], w_req->len, w_req->conn_idx);
        }
        break;
    }
    case BLE_5_READ_EVENT: {
        read_req_t * r_req = (read_req_t *) param;
        ChipLogProgress(DeviceLayer, "BLE_5_READ_EVENT att_idx=%u", r_req->att_idx);
        if (r_req->att_idx == SVR_FFF6_RX_VALUE)
        {
            sInstance.HandleTXCharRead(param);
        }
        if (r_req->att_idx == SVR_FFF6_TX_CFG)
        {
            sInstance.HandleTXCharCCCDRead(param);
        }
#if CHIP_ENABLE_ADDITIONAL_DATA_ADVERTISING
        if (r_req->att_idx == SVR_FFF6_C3_VALUE)
        {
            sInstance.HandleC3CharRead((void *) param);
        }
#endif
        break;
    }
    case BLE_5_TX_DONE: {
        ChipLogProgress(DeviceLayer, "BLE_5_TX_DONE");
        CHIPoBLEConState * conState = sInstance.GetConnectionState(conn_idx, false);
        if (conState != NULL)
        {
            sInstance.HandleTXCharConfirm(conState, 0);
        }
    }
    break;
    case BLE_5_MTU_CHANGE: {
        mtu_change_t * m_ind = (mtu_change_t *) param;
        CHIPoBLEConState * conState = sInstance.GetConnectionState(m_ind->conn_idx);
        if (conState != NULL)
        {
            conState->mtu = (m_ind->mtu_size >= (1 << 10)) ? ((1 << 10) - 1) : m_ind->mtu_size;
        }
        break;
    }
    case BLE_5_CONNECT_EVENT: {
        conn_ind_t * c_ind = (conn_ind_t *) param;
        sInstance.param_conn_idx = c_ind->conn_idx;
        sCccdWritten = false;
        
        ChipLogProgress(DeviceLayer, "BLE GATT connection established");
        ChipLogProgress(DeviceLayer, "conn_idx:%d, peer_addr_type:%d, peer_addr:%02x:%02x:%02x:%02x:%02x:%02x",
                        c_ind->conn_idx, c_ind->peer_addr_type, c_ind->peer_addr[0], c_ind->peer_addr[1], c_ind->peer_addr[2],
                        c_ind->peer_addr[3], c_ind->peer_addr[4], c_ind->peer_addr[5]);
        sInstance.mFlags.Set(Flags::kDeviceConnected);
        sInstance.mFlags.Clear(Flags::kBEKENBLEADVStarted);
        CHIPoBLEConState * bleConnState = sInstance.GetConnectionState(c_ind->conn_idx, true);
        if (bleConnState == NULL)
        {
            ChipLogError(DeviceLayer, "BLE_5_CONNECT_EVENT failed");
            int ext_evt = DriveBLEExtPerfEvt_DISCONNECT | (c_ind->conn_idx << 8);
            PlatformMgr().ScheduleWork(DriveBLEExtPerf, ext_evt);
        }
        conn_idx = c_ind->conn_idx;
        PlatformMgr().ScheduleWork(DriveBLEState, 0);

        // bleParaUpdateTimer = xTimerCreate("", 500, pdFALSE, NULL, ble_updatepara_timer_timeout_handle);
        // xTimerStart(bleParaUpdateTimer, BEKEN_WAIT_FOREVER);

        break;
    }
    case BLE_5_DISCONNECT_EVENT: {
        discon_ind_t * c_ind = (discon_ind_t *) param;
        ChipLogProgress(DeviceLayer, "BLE GATT disconnection reason:%d, cccdWritten=%d", c_ind->reason,
                        sCccdWritten ? 1 : 0);
        sInstance.HandleGAPDisconnect(c_ind->conn_idx, c_ind->reason);
        sInstance.mFlags.Clear(Flags::kDeviceConnected);
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
        break;
    }
    case BLE_5_ATT_INFO_REQ: {
        att_info_req_t * a_ind = (att_info_req_t *) param;
        if (SVR_FFF6_RX_VALUE == a_ind->att_idx)
        {
            a_ind->length = 512;
            a_ind->status = ERR_SUCCESS;
        }
        else if (SVR_FFF6_TX_CFG == a_ind->att_idx)
        {
            a_ind->length = 2;
            a_ind->status = ERR_SUCCESS;
        }
        break;
    }
    case BLE_5_CREATE_DB: {
        sInstance.mFlags.Set(Flags::kBekenBLESGATTSReady);
        PlatformMgr().ScheduleWork(DriveBLEState, 0);
        break;
    }
    case BLE_5_INIT_CONN_PARAM_UPDATE_REQ_EVENT:
    {
        conn_param_t *d_ind_t = (conn_param_t *)param;       
        ble_para_cfm(d_ind_t);
        break;
    }
    case BLE_5_INIT_CONN_PARAM_UPDATE_IND_EVENT:
    {
        conn_update_ind_t *d_ind = (conn_update_ind_t *)param;
        // Do not force a shorter interval during commissioning to avoid connection drops.
        ChipLogProgress(DeviceLayer, "conn param interval:%d time_out:%d", d_ind->interval, d_ind->time_out);
        break;
    }
    default: {
        ChipLogProgress(DeviceLayer, "Unhandled event:%x", notice);
    }
    break;
    }
}

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
#endif // CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
