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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <functional>

#include <lib/core/CHIPError.h>

class BkLightingManager
{
public:
    CHIP_ERROR Init();
    void ApplyPowerOnDefaults();
    void RestoreCommissionedState();
    void SetColor(uint8_t hue, uint8_t saturation);
    void SetLevel(uint8_t level);
    void StepLevel(int8_t step);
    void SetRgbColor(uint8_t r, uint8_t g, uint8_t b);
    void SetTemperature(uint16_t temperature);
    void SetCurrentX(uint16_t currentX);
    void SetCurrentY(uint16_t currentY);
    void SetOnOff(bool on);
    bool StartProductionRgbOutput();
    void SetProductionRgbOutput(uint8_t r, uint8_t g, uint8_t b);
    void StopProductionRgbOutput();
    void CompleteInitAfterCommissioning();
    bool IsOn() const;
    uint8_t GetLevel() const;

private:
    enum class ColorMode
    {
        Hsv,
        Temperature,
        Xy,
    };

    friend BkLightingManager & BkLightingMgr(void);
    bool isOn;
    void ApplyCurrentColor();

    uint16_t mHue;
    uint8_t mSaturation;
    uint8_t mLevel;
    uint16_t mTemperature;
    uint16_t mCurrentX;
    uint16_t mCurrentY;
    ColorMode mColorMode;
    bool mColorStateValid;

    static BkLightingManager sLight;
};

inline BkLightingManager & BkLightingMgr(void)
{
    return BkLightingManager::sLight;
}
