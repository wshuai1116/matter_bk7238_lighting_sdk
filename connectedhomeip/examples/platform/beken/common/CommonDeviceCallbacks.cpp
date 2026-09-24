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
#include <platform/Beken/matter_pal.h>
#include "CommonDeviceCallbacks.h"

#include <app/server/Dnssd.h>
#include <app/util/util.h>
#include <lib/support/CodeUtils.h>
#include <app/server/Server.h>

using namespace chip;
using namespace chip::DeviceLayer;
using namespace chip::System;

static void tryEstablishSession(Layer * systemLayer, void *ctx){
    auto onSuccessFn = [](void *c,Messaging::ExchangeManager &e, const SessionHandle &s){
        ChipLogProgress(DeviceLayer, "establish session success");
    };
    auto onFailedFn = [](void *c,const ScopedNodeId &p, CHIP_ERROR e){
        ChipLogProgress(DeviceLayer, "FindOrEstablishSession: PeerId = [%d:" ChipLogFormatX64 "], failed: %" CHIP_ERROR_FORMAT, p.GetFabricIndex(),
                  ChipLogValueX64(p.GetNodeId()), e.Format());
        static uint8_t count = 2;
        if(count > 0){
            count--;
            DeviceLayer::SystemLayer().CancelTimer(tryEstablishSession, NULL);
            DeviceLayer::SystemLayer().StartTimer(Clock::Milliseconds32(1000), tryEstablishSession, NULL);
        } else {
            count = 2;
        }
    };

    static chip::Callback::Callback<OnDeviceConnected> onSuccessFnCallback(onSuccessFn, nullptr);
    static chip::Callback::Callback<OnDeviceConnectionFailure> onFailedFnCallback(onFailedFn, nullptr);

    SimpleSessionResumptionStorage *simpleSessionResumptionStorage =
        static_cast<SimpleSessionResumptionStorage *>(Server::GetInstance().GetSessionResumptionStorage());

    chip::SimpleSessionResumptionStorage::SessionIndex index;
    LogErrorOnFailure(simpleSessionResumptionStorage->LoadIndex(index));
    if (index.mSize > 0)
    {
        int i = index.mSize - 1;
        ChipLogProgress(
            DeviceLayer,
            "try FindOrEstablishSession: PeerId = [%d:" ChipLogFormatX64 "]",
            index.mNodes[i].GetFabricIndex(),
            ChipLogValueX64(index.mNodes[i].GetNodeId()));
        Server::GetInstance().GetCASESessionManager()->FindOrEstablishSession(
            index.mNodes[i], &onSuccessFnCallback, &onFailedFnCallback);
    }
}

void CommonDeviceCallbacks::DeviceEventCallback(const ChipDeviceEvent * event, intptr_t arg)
{
    ChipLogProgress(Zcl, "DeviceEventCallback, event->Type:%d \r\n", event->Type);
    switch (event->Type)
    {
    case DeviceEventType::kInternetConnectivityChange:
        OnInternetConnectivityChange(event);
        break;

    case DeviceEventType::kInterfaceIpAddressChanged:
        ChipLogProgress(Zcl, "IP(%s) changed event",
                        (event->InterfaceIpAddressChanged.Type == InterfaceIpChangeType::kIpV4_Assigned) ? "IPv4" : "IPv6");
        if ((event->InterfaceIpAddressChanged.Type == InterfaceIpChangeType::kIpV4_Assigned) ||
            (event->InterfaceIpAddressChanged.Type == InterfaceIpChangeType::kIpV6_Assigned))
        {
            // MDNS server restart on any ip assignment: if link local ipv6 is configured, that
            // will not trigger a 'internet connectivity change' as there is no internet
            // connectivity. MDNS still wants to refresh its listening interfaces to include the
            // newly selected address.
            chip::app::DnssdServer::Instance().StartServer();
#if defined(CONFIG_BEKEN_ACTIVATE_SESSION_RESUMPTION) && CONFIG_BEKEN_ACTIVATE_SESSION_RESUMPTION
            if (Server::GetInstance().GetFabricTable().FabricCount() > 0) {
                DeviceLayer::SystemLayer().CancelTimer(tryEstablishSession, NULL);
                DeviceLayer::SystemLayer().StartTimer(Clock::Milliseconds32(1000), tryEstablishSession, NULL);
            }
#endif
        }
        break;
    case DeviceEventType::kCHIPoBLEConnectionEstablished:
#ifdef BEKEN_SOC_BK7231
        sctrl_overclock(1);
#endif
        ChipLogProgress(Zcl, "CHIPoBLE connection established");
#ifdef BEKEN_SOC_BK7231
        {
            DeviceLayer::Internal::BLEManagerImpl sInstance;
            sInstance.vBleUpdateParam();
        }
#endif
        break;
    case DeviceEventType::kCHIPoBLEConnectionClosed:
        ChipLogProgress(Zcl, "CHIPoBLE disconnected");
        break;
    case DeviceEventType::kCommissioningComplete:
#ifdef BEKEN_SOC_BK7231
        {
            //alexa:0x1217 home:0x1349 0x1384
            static FabricIndex index=1;
            const FabricInfo *fabricInfo = Server::GetInstance().GetFabricTable().FindFabricWithIndex(index);
            if(fabricInfo->GetVendorId()!=0x1349)
            {
                sctrl_overclock(0);
            }
            index++;
        }
#endif
        ChipLogProgress(Zcl, "Commissioning complete");
        break;
    }
}

void CommonDeviceCallbacks::OnInternetConnectivityChange(const ChipDeviceEvent * event)
{
    if (event->InternetConnectivityChange.IPv4 == kConnectivity_Established)
    {
        ChipLogProgress(Zcl, "Server ready at:%d", CHIP_PORT);
        // we start dnssd when ipv6 is ready.
    }
    else if (event->InternetConnectivityChange.IPv4 == kConnectivity_Lost)
    {
        ChipLogProgress(Zcl, "Lost IPv4 connectivity...");
    }
    if (event->InternetConnectivityChange.IPv6 == kConnectivity_Established)
    {
        ChipLogProgress(Zcl, "IPv6 Server ready...");
    }
    else if (event->InternetConnectivityChange.IPv6 == kConnectivity_Lost)
    {
        ChipLogProgress(Zcl, "Lost IPv6 connectivity...");
    }
}
