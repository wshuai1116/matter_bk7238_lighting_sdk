

#include "matter_pal.h"
#include "LightingManager.h"

#include <lib/support/logging/CHIPLogging.h>

LightingManager LightingManager::sLight;

static void lighting_gpio_init()
{
#ifdef BEKEN_SOC_BK7231
    BkGpioInitialize(GPIO6, OUTPUT_NORMAL);
    BkGpioInitialize(GPIO7, OUTPUT_NORMAL);
    BkGpioInitialize(GPIO8, OUTPUT_NORMAL);
    BkGpioOutputHigh(GPIO6);
    BkGpioOutputHigh(GPIO7);
    BkGpioOutputHigh(GPIO8);
#endif
}
static void lighting_gpio_set(LightingManager::Action_t aAction)
{
#ifdef BEKEN_SOC_BK7231
    if(aAction == LightingManager::ON_ACTION)
        BkGpioOutputLow(GPIO6);
    if(aAction == LightingManager::OFF_ACTION)
        BkGpioOutputHigh(GPIO6);
#endif
}

CHIP_ERROR LightingManager::Init()
{
    mState = kState_Off;
    lighting_gpio_init();
    mActionInitiated_CB = lighting_gpio_set;
    return CHIP_NO_ERROR;
}

bool LightingManager::IsTurnedOn()
{
    return mState == kState_On;
}

void LightingManager::SetCallbacks(LightingCallback_fn aActionInitiated_CB, LightingCallback_fn aActionCompleted_CB)
{
    mActionInitiated_CB = aActionInitiated_CB;
    mActionCompleted_CB = aActionCompleted_CB;
}

bool LightingManager::InitiateAction(Action_t aAction)
{
    // TODO: this function is called InitiateAction because we want to implement some features such as ramping up here.
    bool action_initiated = false;
    State_t new_state;

    switch (aAction)
    {
    case ON_ACTION:
        ChipLogProgress(AppServer, "LightingManager::InitiateAction(ON_ACTION)");
        break;
    case OFF_ACTION:
        ChipLogProgress(AppServer, "LightingManager::InitiateAction(OFF_ACTION)");
        break;
    default:
        ChipLogProgress(AppServer, "LightingManager::InitiateAction(unknown)");
        break;
    }

    // Initiate On/Off Action only when the previous one is complete.
    if (mState == kState_Off && aAction == ON_ACTION)
    {
        action_initiated = true;
        new_state        = kState_On;
    }
    else if (mState == kState_On && aAction == OFF_ACTION)
    {
        action_initiated = true;
        new_state        = kState_Off;
    }

    if (action_initiated)
    {
        if (mActionInitiated_CB)
        {
            mActionInitiated_CB(aAction);
        }

        Set(new_state == kState_On);

        if (mActionCompleted_CB)
        {
            mActionCompleted_CB(aAction);
        }
    }

    return action_initiated;
}

void LightingManager::Set(bool aOn)
{
    if (aOn)
    {
        mState = kState_On;
    }
    else
    {
        mState = kState_Off;
    }
}
