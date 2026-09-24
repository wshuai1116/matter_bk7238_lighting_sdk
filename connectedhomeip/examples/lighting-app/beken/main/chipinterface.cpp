/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
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

#include "DeviceCallbacks.h"
#include "BkLightingManager.h"
#include "LightingRemoteBridge.h"
#include "ProductionQrTcp.h"

#include <common/BekenAppServer.h>
#include <common/CHIPDeviceManager.h>

#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <DeviceInfoProviderImpl.h>
#include <platform/CHIPDeviceLayer.h>
#include <lib/support/CHIPMem.h>

#include <app/clusters/identify-server/identify-server.h>
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <app/server/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <lib/core/ErrorStr.h>
#include <platform/Beken/BekenConfig.h>
#include <platform/Beken/NetworkCommissioningDriver.h>
#include <setup_payload/ManualSetupPayloadGenerator.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <platform/Beken/FactoryDataProvider.h>
#include <inet/BasicPacketFilters.h>

#include <ota/OTAHelper.h>

using chip::ByteSpan;
using chip::EndpointId;
using chip::FabricIndex;
using chip::NodeId;
using chip::OnDeviceConnected;
using chip::OnDeviceConnectionFailure;
using chip::PeerId;
using chip::Server;
using chip::VendorId;
using chip::Callback::Callback;
using chip::System::Layer;
using chip::Transport::PeerAddress;
using namespace chip::Messaging;

using namespace ::chip;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceManager;
using namespace ::chip::DeviceLayer;

// 是否启用旧的“快速上电 3 次即擦除 Matter 分区并重启”逻辑。
// 0: 关闭。避免与当前灯工程使用的“上电计数触发配网窗口”机制互相干扰。
// 1: 开启。保留历史 fast_factory_reset 行为。
#ifndef LIGHTING_ENABLE_FAST_FACTORY_RESET
#define LIGHTING_ENABLE_FAST_FACTORY_RESET 0
#endif

static AppDeviceCallbacks EchoCallbacks;

void OnIdentifyStart(Identify *)
{
    ChipLogProgress(Zcl, "OnIdentifyStart");
}

void OnIdentifyStop(Identify *)
{
    ChipLogProgress(Zcl, "OnIdentifyStop");
}

void OnTriggerEffect(Identify * identify)
{
    switch (identify->mCurrentEffectIdentifier)
    {
    case app::Clusters::Identify::EffectIdentifierEnum::kBlink:
        ChipLogProgress(Zcl, "EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_BLINK");
        break;
    case app::Clusters::Identify::EffectIdentifierEnum::kBreathe:
        ChipLogProgress(Zcl, "EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_BREATHE");
        break;
    case app::Clusters::Identify::EffectIdentifierEnum::kOkay:
        ChipLogProgress(Zcl, "EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_OKAY");
        break;
    case app::Clusters::Identify::EffectIdentifierEnum::kChannelChange:
        ChipLogProgress(Zcl, "EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_CHANNEL_CHANGE");
        break;
    default:
        ChipLogProgress(Zcl, "No identifier effect");
        return;
    }
}

static Identify gIdentify1 = {
    chip::EndpointId{ 1 }, OnIdentifyStart, OnIdentifyStop, app::Clusters::Identify::IdentifyTypeEnum::kNone, OnTriggerEffect,
};

extern "C" void matter_factory_reset(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv ){
    Server::GetInstance().ScheduleFactoryReset();
}

extern "C" void matter_show(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv ){
    uint16_t discriminator = 0;
    uint32_t passcode = 0;
    uint16_t product_id = 0;
    uint16_t vendor_id = 0;
    char serial_number[21] = {0};
    GetCommissionableDataProvider()->GetSetupDiscriminator(discriminator);
    GetCommissionableDataProvider()->GetSetupPasscode(passcode);
    GetDeviceInstanceInfoProvider()->GetProductId(product_id);
    GetDeviceInstanceInfoProvider()->GetVendorId(vendor_id);
    GetDeviceInstanceInfoProvider()->GetSerialNumber(serial_number, sizeof(serial_number));
    bk_printf("Device Configuration:\r\n");
    bk_printf("\tSerial Number: %s\r\n", serial_number);
    bk_printf("\tVendor ID: %d (0x%04x)\r\n", vendor_id, vendor_id);
    bk_printf("\tProduct ID: %d (0x%04x)\r\n", product_id, product_id);
    bk_printf("\tSetup Passcode: %08ld\r\n", passcode);
    bk_printf("\tSetup Discriminator: %d (0x%04x)\r\n", discriminator, discriminator);
    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));
}

extern "C" void matter_software_reset(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t c) {
            using chip::DeviceLayer::Internal::BekenConfig;
            BekenConfig::WriteConfigValue(BekenConfig::kConfigKey_SoftwareVersion, (uint16_t) 0);
            BekenConfig::WriteConfigValueStr(BekenConfig::kConfigKey_SoftwareVersionString, "0.0");
            bk_reboot();
        },
        0);
}

void fast_factory_reset() {
#if LIGHTING_ENABLE_FAST_FACTORY_RESET
    uint16_t fast_count = 1;
    using chip::DeviceLayer::Internal::BekenConfig;
    if (CHIP_NO_ERROR == BekenConfig::ReadConfigValue(BekenConfig::kConfigKey_FastReboot, fast_count))
    {
        fast_count++;
        if (fast_count >= 3)
        {
            bk_printf("[FAST_REBOOT] count=%d trigger fast_factory_reset\r\n", fast_count);
            bk_erase_all(BK_PARTITION_MATTER_FLASH);
            bk_reboot();
        } else {
            bk_printf("[FAST_REBOOT] count=%d\r\n", fast_count);
            BekenConfig::WriteConfigValue(BekenConfig::kConfigKey_FastReboot, fast_count);
        }
    }
    else
    {
        bk_printf("[FAST_REBOOT] read error, init count=%d\r\n", fast_count);
        BekenConfig::WriteConfigValue(BekenConfig::kConfigKey_FastReboot, fast_count);
    }
#else
    bk_printf("[FAST_REBOOT] disabled by LIGHTING_ENABLE_FAST_FACTORY_RESET=0\r\n");
#endif
}

#if CHIP_DEVICE_CONFIG_ENABLE_TEST_SETUP_PARAMS
chip::DeviceLayer::FactoryDataProviderInRAM mFactoryDataProvider;
#else
chip::DeviceLayer::FactoryDataProvider mFactoryDataProvider;
#endif
chip::DeviceLayer::DeviceInfoProviderImpl gExampleDeviceInfoProvider;

static void InitServer(intptr_t context)
{
    chip::BekenAppServer::Init();
#if LIGHTING_ENABLE_FAST_FACTORY_RESET
    DeviceLayer::SystemLayer().StartTimer(
        chip::System::Clock::Milliseconds32(5000),
        [](Layer * systemLayer, void * appState) {
            using chip::DeviceLayer::Internal::BekenConfig;
            BekenConfig::WriteConfigValue(BekenConfig::kConfigKey_FastReboot, (uint16_t) 0);
            bk_printf("[FAST_REBOOT] reset count=%d after 5000ms uptime\r\n", 0);
        },
        nullptr);
#endif

    BkLightingMgr().Init();
    LightingRemoteInit();
    if (Server::GetInstance().GetFabricTable().FabricCount() > 0)
    {
        BkLightingMgr().RestoreCommissionedState();
    }
    else
    {
        BkLightingMgr().ApplyPowerOnDefaults();
    }


    OTAHelpers::Instance().InitOTARequestor();

    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));
    ProductionQrTcpStart();
}
extern "C" void ChipTest(void)
{
    ChipLogProgress(DeviceLayer, "Lighting App Demo!");
    fast_factory_reset();
    CHIP_ERROR err = CHIP_NO_ERROR;

    SetCommissionableDataProvider(&mFactoryDataProvider);
    SetDeviceInstanceInfoProvider(&mFactoryDataProvider);
#if CONFIG_ENABLE_BEKEN_DEVICE_INFO
    SetDeviceAttestationCredentialsProvider(&mFactoryDataProvider);
#else
    SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());
#endif

    chip::DeviceLayer::SetDeviceInfoProvider(&gExampleDeviceInfoProvider);

    static chip::Inet::DropIfTooManyQueuedPacketsFilter filter(10);
    filter.SetPredicate(
        [](void *c, const void *e, const chip::Inet::IPPacketInfo &pkInfo,
            const chip::System::PacketBufferHandle &pktPayload) {
            if (pkInfo.DestPort == 5353) {
                return true;
            }
            return false;
        },
        NULL);
    chip::Inet::UDPEndPointImplLwIP::SetQueueFilter((chip::Inet::EndpointQueueFilter *)&filter);

    CHIPDeviceManager & deviceMgr = CHIPDeviceManager::GetInstance();
    err                           = deviceMgr.Init(&EchoCallbacks); // start the CHIP task
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "DeviceManagerInit() - ERROR!");
    }
    chip::DeviceLayer::PlatformMgr().ScheduleWork(InitServer, 0);
}

bool lowPowerClusterSleep()
{
    return true;
}
