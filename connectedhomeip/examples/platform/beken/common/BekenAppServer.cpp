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

#include "BekenAppServer.h"
#include "CHIPDeviceManager.h"
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <app/server/Dnssd.h>
#include <app/server/Server.h>
#include <platform/Beken/NetworkCommissioningDriver.h>

using namespace chip;
using namespace chip::Credentials;
using namespace chip::DeviceLayer;

namespace {
app::Clusters::NetworkCommissioning::Instance
    sWiFiNetworkCommissioningInstance(0 /* Endpoint Id */, &(NetworkCommissioning::BekenWiFiDriver::GetInstance()));

chip::FabricTableDelegateImpl gFabricTableDelegate;

} // namespace

namespace chip {

void FabricTableDelegateImpl::OnFabricRemoved(const FabricTable & fabricTable, FabricIndex fabricIndex){
    if(fabricTable.FabricCount() == 0){
        // 保持设备在线，是否重新进入配网由应用层决定。
        ChipLogProgress(Zcl,"Delete the last Fabric, skip ScheduleFactoryReset");
    }
}

namespace BekenAppServer {

void Init(AppDelegate * sAppDelegate)
{
    // Init ZCL Data Model and CHIP App Server
    static chip::CommonCaseDeviceServerInitParams initParams;
    (void) initParams.InitializeStaticResourcesBeforeServerInit();
    if (sAppDelegate != nullptr)
    {
        initParams.appDelegate = sAppDelegate;
    }
    chip::Server::GetInstance().Init(initParams);
    if (Server::GetInstance().GetFabricTable().AddFabricDelegate(
            &gFabricTableDelegate) != CHIP_NO_ERROR) {
      ChipLogError(Zcl, "Failed to initialize Matter server");
    }
    sWiFiNetworkCommissioningInstance.Init();
}

} // namespace BekenAppServer

} // namespace chip

