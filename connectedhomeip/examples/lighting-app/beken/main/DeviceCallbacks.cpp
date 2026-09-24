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
 * @file DeviceCallbacks.cpp
 *
 * Implements all the callbacks to the application from the CHIP Stack
 *
 **/

#include "DeviceCallbacks.h"
#include "BkLightingManager.h"
#include "LightingRemoteBridge.h"

#include <common/CHIPDeviceManager.h>

#include <app/CommandHandler.h>
#include <app/server/Dnssd.h>
#include <app/util/attribute-table.h>
#include <app/util/basic-types.h>
#include <app/util/util.h>
#include <lib/dnssd/Advertiser.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <lib/support/logging/Constants.h>

static const char * TAG = "app-devicecallbacks";

using namespace ::chip;
using namespace ::chip::Inet;
using namespace ::chip::System;
using namespace ::chip::DeviceLayer;
using namespace ::chip::DeviceManager;
using namespace ::chip::Logging;
using namespace ::chip::app;

void AppDeviceCallbacks::DeviceEventCallback(const ChipDeviceEvent * event, intptr_t arg)
{
    CommonDeviceCallbacks::DeviceEventCallback(event, arg);

    if (event->Type == DeviceEventType::kCommissioningComplete)
    {
        LightingRemoteEnableAfterCommissioning();
    }
}

void AppDeviceCallbacks::PostAttributeChangeCallback(EndpointId endpointId, ClusterId clusterId, AttributeId attributeId,
                                                     uint8_t type, uint16_t size, uint8_t * value)
{
    ChipLogProgress(DeviceLayer, "[%s] cluster ID: " ChipLogFormatMEI, TAG, ChipLogValueMEI(clusterId));
    switch (clusterId)
    {
    case Clusters::OnOff::Id:
        OnOnOffPostAttributeChangeCallback(endpointId, attributeId, value);
        break;

    case Clusters::LevelControl::Id:
        OnLevelControlAttributeChangeCallback(endpointId, attributeId, value);
        break;

    case Clusters::ColorControl::Id:
        OnColorControlAttributeChangeCallback(endpointId, attributeId, value);
        break;

    default:
        ChipLogProgress(Zcl, "Unknown cluster ID: " ChipLogFormatMEI, ChipLogValueMEI(clusterId));
        break;
    }
}

void AppDeviceCallbacks::OnOnOffPostAttributeChangeCallback(EndpointId endpointId, AttributeId attributeId, uint8_t * value)
{
    VerifyOrExit(attributeId == Clusters::OnOff::Attributes::OnOff::Id,
                 ChipLogError(DeviceLayer, "[%s] Unhandled Attribute ID: '0x%04lx", TAG, attributeId));
    VerifyOrExit(endpointId == 1 || endpointId == 2,
                 ChipLogError(DeviceLayer, "[%s] Unexpected EndPoint ID: `0x%02x'", TAG, endpointId));

    // At this point we can assume that value points to a bool value.
    BkLightingMgr().SetOnOff(*value);

exit:
    return;
}

void AppDeviceCallbacks::OnLevelControlAttributeChangeCallback(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value)
{
    VerifyOrExit(attributeId == Clusters::LevelControl::Attributes::CurrentLevel::Id,
                 ChipLogError(DeviceLayer, "[%s] Unhandled Attribute ID: '0x%04lx", TAG, attributeId));
    VerifyOrExit(endpointId == 1, ChipLogError(DeviceLayer, "[%s] Unexpected EndPoint ID: `0x%02x'", TAG, endpointId));

    BkLightingMgr().SetLevel(*value);

exit:
    return;
}

void AppDeviceCallbacks::OnColorControlAttributeChangeCallback(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value)
{
    uint8_t colormode;
    uint8_t hue, saturation;
    uint16_t currentX, currentY,colortemp;

    VerifyOrExit(attributeId == Clusters::ColorControl::Attributes::CurrentHue::Id ||
                 attributeId == Clusters::ColorControl::Attributes::CurrentSaturation::Id ||
                 attributeId == Clusters::ColorControl::Attributes::CurrentX::Id||
                 attributeId == Clusters::ColorControl::Attributes::CurrentY::Id||
                 attributeId == Clusters::ColorControl::Attributes::ColorTemperatureMireds::Id||
                 attributeId == Clusters::ColorControl::Attributes::ColorMode::Id,
                 ChipLogError(DeviceLayer, "[%s] Unhandled AttributeId ID: '0x%04lx", TAG, attributeId));
    VerifyOrExit(endpointId == 1, ChipLogError(DeviceLayer, "[%s] Unexpected EndPoint ID: `0x%02x'", TAG, endpointId));

    if(attributeId == Clusters::ColorControl::Attributes::ColorMode::Id)
    {
        colormode = *value;

        switch (colormode)
        {
        case 0:
            emberAfReadAttribute(endpointId, Clusters::ColorControl::Id, Clusters::ColorControl::Attributes::CurrentSaturation::Id,
                                &saturation, sizeof(uint8_t));
            emberAfReadAttribute(endpointId, Clusters::ColorControl::Id, Clusters::ColorControl::Attributes::CurrentHue::Id,
                                &hue, sizeof(uint8_t));
            BkLightingMgr().SetColor(hue, saturation);
            break;
        case 1:
            emberAfReadAttribute(endpointId, Clusters::ColorControl::Id, Clusters::ColorControl::Attributes::CurrentX::Id,
                                (uint8_t *)&currentX, sizeof(uint16_t));
            emberAfReadAttribute(endpointId, Clusters::ColorControl::Id, Clusters::ColorControl::Attributes::CurrentY::Id,
                                (uint8_t *)&currentY, sizeof(uint16_t));

            BkLightingMgr().SetCurrentX(currentX);
            BkLightingMgr().SetCurrentY(currentY);
            break;
        case 2:
            emberAfReadAttribute(endpointId, Clusters::ColorControl::Id, Clusters::ColorControl::Attributes::ColorTemperatureMireds::Id,
                                (uint8_t *)&colortemp, sizeof(uint16_t));

            BkLightingMgr().SetTemperature(colortemp);
            break;
        default:
            break;
        }
    }
    else
    {
        emberAfReadAttribute(endpointId, Clusters::ColorControl::Id, Clusters::ColorControl::Attributes::ColorMode::Id, &colormode, sizeof(uint8_t));

        switch (colormode)
        {
        case 0:
            if (attributeId == Clusters::ColorControl::Attributes::CurrentHue::Id)
            {
                hue = *value;
                emberAfReadAttribute(endpointId, Clusters::ColorControl::Id, Clusters::ColorControl::Attributes::CurrentSaturation::Id,
                                     &saturation, sizeof(uint8_t));
                BkLightingMgr().SetColor(hue, saturation);
            }
            if (attributeId == Clusters::ColorControl::Attributes::CurrentSaturation::Id)
            {
                saturation = *value;
                emberAfReadAttribute(endpointId, Clusters::ColorControl::Id, Clusters::ColorControl::Attributes::CurrentHue::Id,
                                     &hue, sizeof(uint8_t));
                BkLightingMgr().SetColor(hue, saturation);
            }
            break;
        case 1:
            if (attributeId == Clusters::ColorControl::Attributes::CurrentX::Id)
            {
                BkLightingMgr().SetCurrentX(*(uint16_t *) value);
            }
            if (attributeId == Clusters::ColorControl::Attributes::CurrentY::Id)
            {
                BkLightingMgr().SetCurrentY(*(uint16_t *) value);
            }
            break;
        case 2:
            if (attributeId == Clusters::ColorControl::Attributes::ColorTemperatureMireds::Id)
            {
                BkLightingMgr().SetTemperature(*(uint16_t *) value);
            }
            break;
        default:
            break;
        }
    }
exit:
    return;
}
#if 0
void emberAfScenesClusterEnhancedAddSceneCallback(chip::app::CommandHandler *, chip::app::ConcreteCommandPath const &,
                                                  chip::app::Clusters::Scenes::Commands::EnhancedAddScene::DecodableType const &)
{}
void emberAfScenesClusterEnhancedViewSceneCallback(chip::app::CommandHandler *, chip::app::ConcreteCommandPath const &,
                                                  chip::app::Clusters::Scenes::Commands::EnhancedViewScene::DecodableType const &)
{}
void emberAfScenesClusterCopySceneCallback(chip::app::CommandHandler *, chip::app::ConcreteCommandPath const &,
                                                  chip::app::Clusters::Scenes::Commands::CopyScene::DecodableType const &)
{}
#endif
