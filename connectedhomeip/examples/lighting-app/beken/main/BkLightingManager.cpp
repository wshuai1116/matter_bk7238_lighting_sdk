//2026-03-05

#include <platform/Beken/matter_pal.h>
#include <cstring>
#include "BkLightingManager.h"

// 某些版本的 SDK 未暴露 bk_adc_t 定义，这里做一个最小兜底。
#ifndef bk_adc_t
typedef enum {
    BK_ADC_CH0 = 0,
    BK_ADC_CH1,
    BK_ADC_CH2,
    BK_ADC_CH3,
    BK_ADC_CH4,
    BK_ADC_CH5,
    BK_ADC_CH6,
    BK_ADC_CH7,
} bk_adc_t;
#endif

#include "BkDriverAdc.h"
extern "C" {
#include "../../../../../../quectel/quec_adapt/api/ql_adc.h"
}

namespace {
bool sQlAdcInited = false;
}

static bool MapBkAdcToQlAdc(bk_adc_t adc, ql_adc_channel_e *out)
{
    if (out == nullptr)
    {
        return false;
    }

    switch (adc)
    {
    case BK_ADC_CH1:
        *out = QL_ADC_CHANNEL_1;
        return true;
    case BK_ADC_CH2:
        *out = QL_ADC_CHANNEL_2;
        return true;
    case BK_ADC_CH3:
        *out = QL_ADC_CHANNEL_3;
        return true;
    case BK_ADC_CH4:
        *out = QL_ADC_CHANNEL_4;
        return true;
    default:
        return false;
    }
}

OSStatus BkAdcInitialize(bk_adc_t adc, uint32_t sampling_cycle)
{
    (void) adc;
    (void) sampling_cycle;
    ql_adc_channel_e qlChan;
    if (MapBkAdcToQlAdc(adc, &qlChan))
    {
        if (!sQlAdcInited)
        {
            if (ql_adc_init() == QL_ADC_SUCCESS)
            {
                sQlAdcInited = true;
            }
        }

        if (sQlAdcInited && ql_adc_start(qlChan) == QL_ADC_SUCCESS)
        {
            return kNoErr;
        }
    }
    return kGeneralErr;
}

OSStatus BkAdcTakeSample(bk_adc_t adc, uint16_t *output)
{
    if (output == nullptr)
    {
        return kGeneralErr;
    }

    ql_adc_channel_e qlChan;
    if (MapBkAdcToQlAdc(adc, &qlChan))
    {
        uint16_t data = 0;
        if (ql_adc_read(qlChan, &data, 1000) == QL_ADC_SUCCESS)
        {
            *output = data;
            return kNoErr;
        }
    }
    return kGeneralErr;
}

OSStatus BkAdcFinalize(bk_adc_t adc)
{
    ql_adc_channel_e qlChan;
    if (MapBkAdcToQlAdc(adc, &qlChan))
    {
        ql_adc_stop(qlChan);
    }
    if (sQlAdcInited)
    {
        ql_adc_deinit();
        sQlAdcInited = false;
    }
    return kNoErr;
}

extern "C" {
// 使用相对路径引用 Quectel OSI 适配层头文件，避免依赖全局包含路径配置
#include "../../../../../../quectel/quec_adapt/api/ql_osi.h"
}

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/server/Dnssd.h>
#include <app/server/Server.h>
#include <app/util/attribute-table.h>
#include <app/util/af-types.h>
#include <app/util/attribute-storage.h>

#include <lib/support/logging/CHIPLogging.h>
#include <platform/PlatformManager.h>
#include <ColorFormat.h>
#include "LightingRemoteBridge.h"

BkLightingManager BkLightingManager::sLight;

namespace {

constexpr chip::EndpointId kLightingEndpointId = 1;
constexpr uint8_t kDefaultPowerOnLevel = 254;
constexpr uint8_t kDefaultPowerOnHue = 0;
constexpr uint8_t kDefaultPowerOnSaturation = 0;
constexpr uint16_t kDefaultPowerOnTemperatureMireds = 326;

void lighting_sync_power_on_defaults_to_matter()
{
    using namespace ::chip::app;

    auto status = Clusters::OnOff::Attributes::OnOff::Set(kLightingEndpointId, true);
    if (status != chip::Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(DeviceLayer, "sync OnOff failed: %d", static_cast<int>(status));
    }

    status = Clusters::LevelControl::Attributes::CurrentLevel::Set(kLightingEndpointId, kDefaultPowerOnLevel);
    if (status != chip::Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(DeviceLayer, "sync CurrentLevel failed: %d", static_cast<int>(status));
    }

    status = Clusters::ColorControl::Attributes::CurrentHue::Set(kLightingEndpointId, kDefaultPowerOnHue);
    if (status != chip::Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(DeviceLayer, "sync CurrentHue failed: %d", static_cast<int>(status));
    }

    status = Clusters::ColorControl::Attributes::CurrentSaturation::Set(kLightingEndpointId, kDefaultPowerOnSaturation);
    if (status != chip::Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(DeviceLayer, "sync CurrentSaturation failed: %d", static_cast<int>(status));
    }

    status = Clusters::ColorControl::Attributes::ColorMode::Set(
        kLightingEndpointId, Clusters::ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation);
    if (status != chip::Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(DeviceLayer, "sync ColorMode failed: %d", static_cast<int>(status));
    }
}

} // namespace

extern "C" void LightingCompleteInitAfterCommissioning(void)
{
    BkLightingMgr().CompleteInitAfterCommissioning();
}

#ifdef BEKEN_SOC_BK7231
#define LED_GPIOS    { GPIO6, GPIO26, GPIO24 }
#define LED_PWMS     { BK_PWM_0, BK_PWM_5, BK_PWM_4 }
#define LED_PWM_UNIT 1000
// RGB 灯条电平极性控制：
// 1 表示高电平点亮    0 表示低电平点亮（默认）
#ifndef RGB_LIGHT_HIGH_LEVEL_ON
#define RGB_LIGHT_HIGH_LEVEL_ON 1
#endif
// 上电后 GPIO 初始电平控制：
// 1 表示上电后拉高，0 表示上电后拉低
#ifndef RGB_LIGHT_INIT_LEVEL_HIGH
#define RGB_LIGHT_INIT_LEVEL_HIGH 0
#endif
#endif // BEKEN_SOC_BK7231

enum LightingIndicatorMode
{
    kIndicatorOff = 0,
    kIndicatorCommissioning = 1,
    kIndicatorRemotePairing = 2,
};

static ql_timer_t s_indicator_timer;
static bool s_indicator_timer_inited = false;
static bool s_indicator_active = false;
static bool s_indicator_on = false;
static uint8_t s_indicator_r = 0;
static uint8_t s_indicator_g = 0;
static uint8_t s_indicator_b = 0;
static uint8_t s_last_r = 0;
static uint8_t s_last_g = 0;
static uint8_t s_last_b = 0;
static bool s_last_on = false;
static bool s_saved_valid = false;
static uint8_t s_saved_r = 0;
static uint8_t s_saved_g = 0;
static uint8_t s_saved_b = 0;
static bool s_saved_on = false;
static ql_mutex_t s_rgb_output_mutex = nullptr;
static bool s_rgb_output_mutex_inited = false;
static bool s_production_rgb_output_active = false;
static ql_timer_t s_timer_feedback_timer;
static bool s_timer_feedback_timer_inited = false;
static bool s_timer_feedback_active = false;
static uint8_t s_timer_feedback_phase = 0;
static uint8_t s_timer_feedback_prev_r = 0;
static uint8_t s_timer_feedback_prev_g = 0;
static uint8_t s_timer_feedback_prev_b = 0;
static bool s_timer_feedback_prev_on = false;
static const uint32_t kIndicatorBlinkMs = 1000; // 配网/对码期间指示灯闪烁周期（ms）

static bool production_rgb_output_is_active()
{
    if (!s_rgb_output_mutex_inited)
    {
        return false;
    }

    ql_rtos_mutex_lock(s_rgb_output_mutex);
    bool active = s_production_rgb_output_active;
    ql_rtos_mutex_unlock(s_rgb_output_mutex);
    return active;
}

static void lighting_color_set_raw(uint8_t r, uint8_t g, uint8_t b)
{
#ifdef BEKEN_SOC_BK7231
    uint8_t rgb[] = { r, g, b };
    bk_pwm_t chans[] = LED_PWMS;
    for (int i = 0; i < 3; i++)
    {
        uint32_t duty = RGB_LIGHT_HIGH_LEVEL_ON ? rgb[i] : (255 - rgb[i]);
        bk_pwm_update_param(chans[i], 255 * LED_PWM_UNIT, duty * LED_PWM_UNIT);
    }
#else
    (void) r;
    (void) g;
    (void) b;
#endif
}

static void lighting_gpio_init()
{
#ifdef BEKEN_SOC_BK7231
    if (ql_rtos_mutex_create(&s_rgb_output_mutex) == QL_OSI_SUCCESS)
    {
        s_rgb_output_mutex_inited = true;
    }
    else
    {
        ChipLogError(DeviceLayer, "Production RGB test: output mutex creation failed");
    }

    GPIO_INDEX gpios[] = LED_GPIOS;
    bk_pwm_t chans[] = LED_PWMS;
    uint32_t initDuty = RGB_LIGHT_INIT_LEVEL_HIGH ? (255 * LED_PWM_UNIT) : 0;
    for(int i=0;i<3;i++){
        BkGpioInitialize(gpios[i], OUTPUT_NORMAL);
        if (RGB_LIGHT_INIT_LEVEL_HIGH)
        {
            BkGpioOutputHigh(gpios[i]);
        }
        else
        {
            BkGpioOutputLow(gpios[i]);
        }
        bk_pwm_initialize(chans[i], 255 * LED_PWM_UNIT, initDuty);
        bk_pwm_start(chans[i]);
    }
    s_last_on = false;
#endif // BEKEN_SOC_BK7231
}
static void lighting_gpio_set(bool on)
{
#ifdef BEKEN_SOC_BK7231
    if (s_rgb_output_mutex_inited)
    {
        ql_rtos_mutex_lock(s_rgb_output_mutex);
    }

    if (!s_production_rgb_output_active)
    {
        lighting_color_set_raw(on ? 255 : 0, on ? 255 : 0, on ? 255 : 0);
        if (!s_indicator_active && !s_timer_feedback_active)
        {
            s_last_on = on;
        }
    }

    if (s_rgb_output_mutex_inited)
    {
        ql_rtos_mutex_unlock(s_rgb_output_mutex);
    }
#endif // BEKEN_SOC_BK7231
}
void lighting_color_set(uint8_t r, uint8_t g, uint8_t b)
{
    // ChipLogProgress(DeviceLayer, "change color set %d,%d,%d", r, g, b);
#ifdef BEKEN_SOC_BK7231
    if (s_rgb_output_mutex_inited)
    {
        ql_rtos_mutex_lock(s_rgb_output_mutex);
    }

    if (!s_production_rgb_output_active)
    {
        lighting_color_set_raw(r, g, b);
        if (!s_indicator_active && !s_timer_feedback_active)
        {
            s_last_r = r;
            s_last_g = g;
            s_last_b = b;
        }
    }

    if (s_rgb_output_mutex_inited)
    {
        ql_rtos_mutex_unlock(s_rgb_output_mutex);
    }
#endif // BEKEN_SOC_BK7231
}

void BkLightingManager::ApplyCurrentColor()
{
    if (!isOn)
    {
        return;
    }

    if (!mColorStateValid)
    {
        lighting_color_set(255, 255, 255);
        return;
    }

    switch (mColorMode)
    {
    case ColorMode::Temperature:
    {
        CtColor_t ctMireds = { mTemperature };
        RgbColor_t rgb = CTToRgb(ctMireds);
        lighting_color_set(rgb.r, rgb.g, rgb.b);
        break;
    }
    case ColorMode::Xy:
    {
        RgbColor_t rgb = XYToRgb(mLevel, mCurrentX, mCurrentY);
        lighting_color_set(rgb.r, rgb.g, rgb.b);
        break;
    }
    case ColorMode::Hsv:
    default:
    {
        HsvColor_t hsv = { mHue, mSaturation, mLevel };
        RgbColor_t rgb = HsvToRgb(hsv);
        lighting_color_set(rgb.r, rgb.g, rgb.b);
        break;
    }
    }
}

static void indicator_apply_color(bool on)
{
    if (on)
    {
        lighting_color_set(s_indicator_r, s_indicator_g, s_indicator_b);
    }
    else
    {
        lighting_color_set(0, 0, 0);
    }
}

static void indicator_timer_cb(void *arg)
{
    (void) arg;
    if (!s_indicator_active)
    {
        return;
    }

    s_indicator_on = !s_indicator_on;
    indicator_apply_color(s_indicator_on);
}

extern "C" void LightingIndicatorStart(uint8_t mode)
{
    if (production_rgb_output_is_active())
    {
        return;
    }

    if (!s_indicator_active)
    {
        s_saved_on = s_last_on;
        s_saved_r = s_last_r;
        s_saved_g = s_last_g;
        s_saved_b = s_last_b;
        s_saved_valid = true;
    }

    s_indicator_active = true;
    s_indicator_on = true;

    switch (mode)
    {
    case kIndicatorCommissioning:
        s_indicator_r = 0;
        s_indicator_g = 255;
        s_indicator_b = 0;
        break;
    case kIndicatorRemotePairing:
        s_indicator_r = 0;
        s_indicator_g = 0;
        s_indicator_b = 255;
        break;
    default:
        s_indicator_r = 0;
        s_indicator_g = 0;
        s_indicator_b = 0;
        break;
    }

    if (!s_indicator_timer_inited)
    {
        if (ql_rtos_timer_create(&s_indicator_timer, kIndicatorBlinkMs, indicator_timer_cb, NULL) == kNoErr)
        {
            s_indicator_timer_inited = true;
        }
    }

    indicator_apply_color(true);

    if (s_indicator_timer_inited)
    {
        ql_rtos_timer_start(s_indicator_timer);
    }
}

extern "C" void LightingIndicatorStop(void)
{
    if (!s_indicator_active)
    {
        return;
    }

    if (s_indicator_timer_inited)
    {
        ql_rtos_timer_stop(s_indicator_timer);
    }

    s_indicator_active = false;
    s_indicator_on = false;

    if (s_saved_valid && s_saved_on)
    {
        lighting_color_set(s_saved_r, s_saved_g, s_saved_b);
    }
    else
    {
        lighting_color_set(0, 0, 0);
    }
}

static void timer_feedback_timer_cb(void *arg)
{
    (void) arg;

    if (!s_timer_feedback_active)
    {
        return;
    }

    if (s_timer_feedback_phase == 0)
    {
        lighting_color_set(0, 255, 0);
        s_timer_feedback_phase++;
        return;
    }

    if (s_timer_feedback_phase == 1)
    {
        lighting_color_set(0, 0, 0);
        s_timer_feedback_phase++;
        return;
    }

    if (s_timer_feedback_prev_on)
    {
        lighting_color_set(s_timer_feedback_prev_r, s_timer_feedback_prev_g, s_timer_feedback_prev_b);
    }
    else
    {
        lighting_color_set(0, 0, 0);
    }

    s_timer_feedback_active = false;
    if (s_timer_feedback_timer_inited)
    {
        ql_rtos_timer_stop(s_timer_feedback_timer);
    }
}

static void timer_feedback_blink_green_once(void)
{
    if (s_indicator_active || production_rgb_output_is_active())
    {
        return;
    }

    if (!s_timer_feedback_timer_inited)
    {
        if (ql_rtos_timer_create(&s_timer_feedback_timer, 120, timer_feedback_timer_cb, NULL) == kNoErr)
        {
            s_timer_feedback_timer_inited = true;
        }
    }

    if (!s_timer_feedback_timer_inited)
    {
        return;
    }

    s_timer_feedback_prev_on = s_last_on;
    s_timer_feedback_prev_r = s_last_r;
    s_timer_feedback_prev_g = s_last_g;
    s_timer_feedback_prev_b = s_last_b;
    s_timer_feedback_phase = 0;
    s_timer_feedback_active = true;

    ql_rtos_timer_stop(s_timer_feedback_timer);
    timer_feedback_timer_cb(NULL);
    ql_rtos_timer_start(s_timer_feedback_timer);
}

// ===================== 遥控情景本地动画调度 =====================
// ==== 麦克风静音中心值模式宏 ====
// MIC_CENTER_MODE: 0 = 静态（使用 kAdcCenter），1 = 动态（进入律动时采样 10 次取平均以自适应环境）
#define MIC_CENTER_MODE_STATIC  0
#define MIC_CENTER_MODE_DYNAMIC 1
#ifndef MIC_CENTER_MODE
#define MIC_CENTER_MODE MIC_CENTER_MODE_DYNAMIC
#endif
// ===================== 麦克风音乐律动（本地采样） =====================
// 遥控器 MODE 的 5 种本地场景动画：
//  scene 1: 三色跳变
//  scene 2: 三色渐变
//  scene 3: 七色跳变
//  scene 4: 七色渐变
//  scene 5: 阶梯式白光亮灭

static ql_timer_t s_remote_scene_timer;
static int s_remote_scene_timer_inited = 0;
static uint8_t s_remote_scene_id = 0;    // 0:无场景, 1~5: MODE 场景
static uint16_t s_remote_scene_step = 0; // 当前动画帧
static uint8_t s_remote_scene_selected_id = 1; // mode+/mode-/auto 的当前场景选择
static ql_timer_t s_remote_scene_auto_timer;
static int s_remote_scene_auto_timer_inited = 0;
static bool s_remote_scene_auto_active = false;
static const uint32_t kRemoteSceneAutoSwitchMs = 10000;
static const uint32_t kRemoteSceneTickMs = 100;

struct RemoteSceneRgb
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

static const RemoteSceneRgb kRemoteSceneRgbColors[] = {
    { 255, 0, 0 },   // 红
    { 0, 255, 0 },   // 绿
    { 0, 0, 255 },   // 蓝
};

static const RemoteSceneRgb kRemoteSceneSevenColors[] = {
    { 255, 0, 0 },   // 红
    { 255, 165, 0 }, // 橙
    { 255, 255, 0 }, // 黄
    { 0, 255, 0 },   // 绿
    { 0, 255, 255 }, // 青
    { 0, 0, 255 },   // 蓝
    { 255, 0, 255 }, // 紫
};

static uint8_t remote_scene_lerp(uint8_t from, uint8_t to, uint16_t step, uint16_t stepCount)
{
    int32_t delta = static_cast<int32_t>(to) - static_cast<int32_t>(from);
    return static_cast<uint8_t>(static_cast<int32_t>(from) + delta * step / stepCount);
}

static RemoteSceneRgb remote_scene_fade_color(const RemoteSceneRgb * colors, uint8_t colorCount, uint16_t step)
{
    const uint16_t kFadeSteps = 20; // 每种颜色之间渐变 2 秒
    uint16_t segment = (step / kFadeSteps) % colorCount;
    uint16_t progress = step % kFadeSteps;
    const RemoteSceneRgb & from = colors[segment];
    const RemoteSceneRgb & to = colors[(segment + 1) % colorCount];

    return {
        remote_scene_lerp(from.r, to.r, progress, kFadeSteps),
        remote_scene_lerp(from.g, to.g, progress, kFadeSteps),
        remote_scene_lerp(from.b, to.b, progress, kFadeSteps),
    };
}

static void stop_music_reactive_effect();
static void remote_scene_auto_stop(void);
static void remote_scene_apply_step(void)
{
    if (s_remote_scene_id == 0)
    {
        return;
    }

    uint8_t r = 0, g = 0, b = 0;

    switch (s_remote_scene_id)
    {
    case 1:
    {
        // 三色跳变：每种颜色保持 1 秒
        uint8_t idx = static_cast<uint8_t>((s_remote_scene_step / 10) % 3);
        r = kRemoteSceneRgbColors[idx].r;
        g = kRemoteSceneRgbColors[idx].g;
        b = kRemoteSceneRgbColors[idx].b;
        break;
    }
    case 2:
    {
        // 三色渐变：颜色始终保持纯 R/G/B，仅亮度由暗到亮再到暗。
        static const uint8_t kBrightnessSteps[] = {
            0, 25, 51, 76, 102, 127, 153, 178, 204, 229, 254,
            229, 204, 178, 153, 127, 102, 76, 51, 25,
        };
        const uint16_t brightnessStepCount =
            static_cast<uint16_t>(sizeof(kBrightnessSteps) / sizeof(kBrightnessSteps[0]));
        uint8_t colorIndex = static_cast<uint8_t>((s_remote_scene_step / brightnessStepCount) % 3);
        uint8_t brightness = kBrightnessSteps[s_remote_scene_step % brightnessStepCount];
        const RemoteSceneRgb & color = kRemoteSceneRgbColors[colorIndex];

        r = static_cast<uint8_t>((static_cast<uint16_t>(color.r) * brightness) / 254);
        g = static_cast<uint8_t>((static_cast<uint16_t>(color.g) * brightness) / 254);
        b = static_cast<uint8_t>((static_cast<uint16_t>(color.b) * brightness) / 254);
        break;
    }
    case 3:
    {
        // 七色跳变：每种颜色保持 1 秒
        uint8_t idx = static_cast<uint8_t>((s_remote_scene_step / 10) % 7);
        r = kRemoteSceneSevenColors[idx].r;
        g = kRemoteSceneSevenColors[idx].g;
        b = kRemoteSceneSevenColors[idx].b;
        break;
    }
    case 4:
    {
        // 七色渐变
        RemoteSceneRgb color = remote_scene_fade_color(kRemoteSceneSevenColors, 7, s_remote_scene_step);
        r = color.r;
        g = color.g;
        b = color.b;
        break;
    }
    case 5:
    {
        // 阶梯式白光亮灭，每阶保持 200ms
        static const uint8_t kWhiteSteps[] = {
            0, 42, 85, 127, 170, 212, 254, 212, 170, 127, 85, 42,
        };
        uint8_t idx = static_cast<uint8_t>((s_remote_scene_step / 2) %
                                           (sizeof(kWhiteSteps) / sizeof(kWhiteSteps[0])));
        r = g = b = kWhiteSteps[idx];
        break;
    }
    default:
        return;
    }

    // 使用统一接口驱动灯带（内部会根据当前模式转换成 HSV 等）
    BkLightingMgr().SetRgbColor(r, g, b);
}

static void remote_scene_timer_cb(void *arg)
{
    (void) arg;
    if (s_remote_scene_id == 0)
    {
        return;
    }

    s_remote_scene_step++;
    remote_scene_apply_step();
}

static void remote_scene_stop(void)
{
    if (s_remote_scene_timer_inited)
    {
        ql_rtos_timer_stop(s_remote_scene_timer);
    }
    s_remote_scene_id   = 0;
    s_remote_scene_step = 0;
}

static void remote_scene_start(uint8_t sceneId)
{
    if (production_rgb_output_is_active())
    {
        return;
    }

    // 停止上一个场景
    remote_scene_stop();

    // 停止麦克风律动，确保不会与情景动画叠加
    stop_music_reactive_effect();

    if (!s_remote_scene_timer_inited)
    {
        if (ql_rtos_timer_create(&s_remote_scene_timer, kRemoteSceneTickMs, remote_scene_timer_cb, NULL) == kNoErr)
        {
            s_remote_scene_timer_inited = 1;
        }
    }

    if (!s_remote_scene_timer_inited)
    {
        // 创建失败则退化为单色场景
        return;
    }

    if (sceneId < 1 || sceneId > 5)
    {
        sceneId = 1;
    }

    s_remote_scene_id   = sceneId;
    s_remote_scene_step = 0;
    s_remote_scene_selected_id = sceneId;

    // 先立即显示第一帧
    remote_scene_apply_step();

    // 启动定时器，循环播放
    ql_rtos_timer_start(s_remote_scene_timer);
}

static void remote_scene_auto_timer_cb(void *arg)
{
    (void) arg;

    if (!s_remote_scene_auto_active)
    {
        return;
    }

    uint8_t nextScene = static_cast<uint8_t>((s_remote_scene_selected_id % 5) + 1);
    remote_scene_start(nextScene);
}

static void remote_scene_auto_start(void)
{
    if (production_rgb_output_is_active())
    {
        return;
    }

    stop_music_reactive_effect();

    if (!s_remote_scene_auto_timer_inited)
    {
        if (ql_rtos_timer_create(&s_remote_scene_auto_timer, kRemoteSceneAutoSwitchMs, remote_scene_auto_timer_cb, NULL) == kNoErr)
        {
            s_remote_scene_auto_timer_inited = 1;
        }
    }

    if (!s_remote_scene_auto_timer_inited)
    {
        return;
    }

    if (s_remote_scene_selected_id < 1 || s_remote_scene_selected_id > 5)
    {
        s_remote_scene_selected_id = 1;
    }

    s_remote_scene_auto_active = true;
    remote_scene_start(s_remote_scene_selected_id);
    ql_rtos_timer_start(s_remote_scene_auto_timer);
}

static void remote_scene_auto_stop(void)
{
    if (s_remote_scene_auto_timer_inited)
    {
        ql_rtos_timer_stop(s_remote_scene_auto_timer);
    }
    s_remote_scene_auto_active = false;
}

// ===================== 麦克风音乐律动（本地采样） =====================

static uint8_t clamp_u8(uint16_t v)
{
    return v > 254 ? 254 : static_cast<uint8_t>(v);
}

class MicMusicReactive
{
public:
    enum class Mode
    {
        Gentle,
        Aggressive,
    };

    void SetSensitivity(uint8_t level)
    {
        if (level < kSensitivityMin)
        {
            level = kSensitivityMin;
        }
        else if (level > kSensitivityMax)
        {
            level = kSensitivityMax;
        }
        mSensitivity = level;
    }

    void Start(Mode mode)
    {
        if (production_rgb_output_is_active())
        {
            return;
        }

        // 已在运行时仅切换模式，避免反复启停 ADC
        if (mRunning)
        {
            mMode     = mode;
            mEma      = 0.0f;
            mHue      = 0;
            mColorTick = 0;
            return;
        }

        mMode       = mode;
        mEma        = 0.0f;
        mHue        = 0;
        mColorTick  = 0;
        mRunning    = (BkAdcInitialize(static_cast<bk_adc_t>(kAdcChannel), 0) == kNoErr);

#if MIC_CENTER_MODE == MIC_CENTER_MODE_DYNAMIC
        mAdcCenter = kAdcCenter;
        uint32_t sum = 0;
        uint16_t tmp = 0;
        int cnt = 0;
        for (int i = 0; i < 10; ++i) {
            if (BkAdcTakeSample(static_cast<bk_adc_t>(kAdcChannel), &tmp) == kNoErr) {
                sum += tmp;
                ++cnt;
            }
        }
        if (cnt > 0) {
            mAdcCenter = static_cast<uint16_t>(sum / cnt);
        }
#else
        mAdcCenter = kAdcCenter;
#endif

        // 采样（或使用静态值）完成后打印一次中心值，便于调试
        ChipLogProgress(AppServer, "Mic ADC center=%u", static_cast<unsigned>(mAdcCenter));

        if (!mTimerCreated)
        {
            if (ql_rtos_timer_create(&mTimer, kTickMs, &MicMusicReactive::TimerThunk, this) == kNoErr)
            {
                mTimerCreated = true;
            }
        }

        if (mRunning && mTimerCreated)
        {
            ql_rtos_timer_start(mTimer);
        }
        else
        {
            Stop();
        }
    }

    void Stop()
    {
        if (mTimerCreated)
        {
            ql_rtos_timer_stop(mTimer);
        }

        if (mRunning)
        {
            ql_adc_channel_e qlChan;
            if (MapBkAdcToQlAdc(static_cast<bk_adc_t>(kAdcChannel), &qlChan))
            {
                ql_adc_stop(qlChan);
            }
        }

        mRunning = false;
    }

    bool IsRunning() const { return mRunning; }

private:
    static constexpr uint8_t kAdcChannel = 3;   // LIGHT_ADC_NUM (GPIO20/ADC3)
    static constexpr uint16_t kAdcMax    = 4096; // LIGHT_ADC_MAX
    static constexpr uint32_t kTickMs    = 40;  // 25Hz 更新
    static constexpr uint16_t kAdcCenter = 1495; // 默认静音中心值（静态模式使用）；动态模式会在 Start() 中采样覆盖
    uint16_t mAdcCenter = kAdcCenter; // 当前使用的中心值（支持动态/静态）
    static constexpr float kNoSoundEmaThreshold = 0.02f; // EMA 低于此值视为无声音

    static constexpr uint8_t kSensitivityMin = 1;
    static constexpr uint8_t kSensitivityMax = 5;
    // 灵敏度档位说明：保留 1~5 档（对应更灵敏的区间），数值越大越灵敏。
    // 阈值 T 随灵敏度从 70（最低）线性降至 10（最高）；放缩区间 R 从 120（最低）线性缩至 30（最高）。
    static constexpr uint16_t kThresholdMax  = 70;  // 最低灵敏度阈值
    static constexpr uint16_t kThresholdMin  = 10;  // 最高灵敏度阈值
    static constexpr uint16_t kRangeMax      = 120; // 最低灵敏度放缩区间
    static constexpr uint16_t kRangeMin      = 30;  // 最高灵敏度放缩区间

    static void TimerThunk(void *arg)
    {
        static_cast<MicMusicReactive *>(arg)->OnTick();
    }

    void OnTick()
    {
        if (!mRunning)
        {
            return;
        }

        uint16_t sample = 0;
        if (BkAdcTakeSample(static_cast<bk_adc_t>(kAdcChannel), &sample) != kNoErr)
        {
            return;
        }

        // 打印 ADC 原始值（默认每 10 次输出 1 次，避免刷屏）
        // static uint32_t s_logCounter = 0;
        // if ((++s_logCounter % 10) == 0)
        // {
        //     ChipLogProgress(AppServer, "Mic ADC raw=%u", static_cast<unsigned>(sample));
        // }

        if (sample > kAdcMax)
        {
            sample = kAdcMax;
        }

        // 基于固定中心值的幅度估计：X = |sample - center|
        int32_t diff = static_cast<int32_t>(sample) - static_cast<int32_t>(mAdcCenter);
        if (diff < 0)
        {
            diff = -diff;
        }
        uint16_t x = static_cast<uint16_t>(diff);

        // 将灵敏度映射到阈值 T 和放缩区间 R
        // 档位1~5对应原6~10，6~10更灵敏
        // 只保留1~5档，线性插值
        float s = static_cast<float>(mSensitivity - 1) / 4.0f;
        float t = static_cast<float>(kThresholdMax) - s * static_cast<float>(kThresholdMax - kThresholdMin);
        float r = static_cast<float>(kRangeMax) - s * static_cast<float>(kRangeMax - kRangeMin);

        float norm = 0.0f;
        if (x > static_cast<uint16_t>(t))
        {
            norm = (static_cast<float>(x) - t) / r;
            if (norm > 1.0f)
            {
                norm = 1.0f;
            }
        }

        // 两种音乐模式使用相同的声音响应，仅颜色策略不同。
        const float alpha = 0.35f;
        mEma             = (1.0f - alpha) * mEma + alpha * norm;

        // 无声音时直接熄灭灯带（但保留指示灯模式）
        if (mEma < kNoSoundEmaThreshold)
        {
            if (!s_indicator_active)
            {
                lighting_color_set(0, 0, 0);
            }
            return;
        }

        uint16_t hue = mHue;
        uint8_t sat  = 100;
        uint8_t val  = 180;

        if (mMode == Mode::Gentle)
        {
            // 音乐模式1：沿用模式2的明暗响应，颜色只使用 R/G/B。
            static const uint16_t kRgbHues[] = { 0, 120, 240 };
            hue = kRgbHues[(mColorTick / 25) % 3]; // 每秒切换一种 RGB 颜色
            sat = 100;
            val = clamp_u8(static_cast<uint16_t>(40 + mEma * 210.0f));
            mColorTick++;
        }
        else
        {
            // 音乐模式2：七色等时循环。声音只控制亮度，避免音量分布导致某些颜色长期缺失。
            // 每个纯色保持约 520ms，再用约 200ms 过渡到下一种颜色。
            static const uint16_t kColorHoldTicks = 13;
            static const uint16_t kColorFadeTicks = 5;
            static const uint16_t kColorPhaseTicks = kColorHoldTicks + kColorFadeTicks;
            const uint8_t colorCount =
                static_cast<uint8_t>(sizeof(kRemoteSceneSevenColors) / sizeof(kRemoteSceneSevenColors[0]));
            uint8_t colorIndex = static_cast<uint8_t>((mColorTick / kColorPhaseTicks) % colorCount);
            uint16_t phaseTick = mColorTick % kColorPhaseTicks;
            RemoteSceneRgb color = kRemoteSceneSevenColors[colorIndex];

            if (phaseTick >= kColorHoldTicks)
            {
                const RemoteSceneRgb & nextColor = kRemoteSceneSevenColors[(colorIndex + 1) % colorCount];
                uint16_t fadeStep = phaseTick - kColorHoldTicks + 1;
                color.r = remote_scene_lerp(color.r, nextColor.r, fadeStep, kColorFadeTicks);
                color.g = remote_scene_lerp(color.g, nextColor.g, fadeStep, kColorFadeTicks);
                color.b = remote_scene_lerp(color.b, nextColor.b, fadeStep, kColorFadeTicks);
            }

            val = clamp_u8(static_cast<uint16_t>(40 + mEma * 210.0f));
            uint8_t outputR =
                static_cast<uint8_t>((static_cast<uint16_t>(color.r) * val) / 254);
            uint8_t outputG =
                static_cast<uint8_t>((static_cast<uint16_t>(color.g) * val) / 254);
            uint8_t outputB =
                static_cast<uint8_t>((static_cast<uint16_t>(color.b) * val) / 254);

            mColorTick++;
            BkLightingMgr().SetRgbColor(outputR, outputG, outputB);
            return;
        }

        mHue = hue;

        HsvColor_t hsv = { hue, sat, val };
        RgbColor_t rgb = HsvToRgb(hsv);
        // ChipLogProgress(AppServer, "Music ADC=%u", static_cast<unsigned>(sample));
        BkLightingMgr().SetRgbColor(rgb.r, rgb.g, rgb.b);
    }

    Mode mMode           = Mode::Gentle;
    ql_timer_t mTimer    = {0};
    bool mTimerCreated   = false;
    bool mRunning        = false;
    float mEma           = 0.0f;
    uint16_t mHue        = 0;
    uint16_t mColorTick  = 0;
    uint8_t mSensitivity = 5;
};

static MicMusicReactive s_musicReactive;

static void stop_music_reactive_effect()
{
    s_musicReactive.Stop();
}

// 将 RGB 颜色转换为 HSV，范围：
//   h: 0-360, s: 0-100, v: 0-254
static HsvColor_t RgbToHsvInternal(uint8_t r, uint8_t g, uint8_t b)
{
    float rf = r / 255.0f;
    float gf = g / 255.0f;
    float bf = b / 255.0f;

    float maxc = rf;
    if (gf > maxc)
        maxc = gf;
    if (bf > maxc)
        maxc = bf;

    float minc = rf;
    if (gf < minc)
        minc = gf;
    if (bf < minc)
        minc = bf;

    float delta = maxc - minc;

    float h = 0.0f;
    if (delta > 0.00001f)
    {
        if (maxc == rf)
        {
            float tmp = (gf - bf) / delta;
            if (tmp < 0.0f)
            {
                tmp += 6.0f;
            }
            h = 60.0f * tmp;
        }
        else if (maxc == gf)
        {
            h = 60.0f * (((bf - rf) / delta) + 2.0f);
        }
        else
        {
            h = 60.0f * (((rf - gf) / delta) + 4.0f);
        }
    }

    float s = 0.0f;
    if (maxc > 0.00001f)
    {
        s = delta / maxc;
    }

    float v = maxc;

    HsvColor_t hsv;
    if (h < 0.0f)
    {
        h = 0.0f;
    }
    if (h > 360.0f)
    {
        h = 360.0f;
    }
    hsv.h = (uint16_t) (h + 0.5f);          // 0-360
    hsv.s = (uint8_t) (s * 100.0f + 0.5f);  // 0-100
    if (hsv.s > 100)
    {
        hsv.s = 100;
    }
    hsv.v = (uint8_t) (v * 254.0f + 0.5f);  // 0-254
    if (hsv.v > 254)
    {
        hsv.v = 254;
    }

    return hsv;
}

CHIP_ERROR BkLightingManager::Init()
{
    isOn = false;
    mColorMode = ColorMode::Temperature;
    mColorStateValid = false;
    mHue = 0;
    mSaturation = 0;
    mLevel = kDefaultPowerOnLevel;
    mTemperature = kDefaultPowerOnTemperatureMireds;
    mCurrentX = 0;
    mCurrentY = 0;
    lighting_gpio_init();

    return CHIP_NO_ERROR;
}

void BkLightingManager::ApplyPowerOnDefaults()
{
    if (LightingRemoteShouldDeferPowerOnDefaults())
    {
        ChipLogProgress(DeviceLayer, "Skip power-on defaults while commissioning mode has priority");
        return;
    }

    mHue = 0;
    mSaturation = 0;
    mLevel = kDefaultPowerOnLevel;
    mTemperature = kDefaultPowerOnTemperatureMireds;
    mCurrentX = 0;
    mCurrentY = 0;
    mColorMode = ColorMode::Hsv;
    mColorStateValid = true;

    SetOnOff(true);
    SetRgbColor(255, 255, 255);
    lighting_sync_power_on_defaults_to_matter();
}

void BkLightingManager::RestoreCommissionedState()
{
    using namespace ::chip::app;

    if (LightingRemoteShouldDeferPowerOnDefaults())
    {
        ChipLogProgress(DeviceLayer, "Skip commissioned state restore while commissioning mode has priority");
        return;
    }

    uint8_t on = 0;
    uint8_t level = kDefaultPowerOnLevel;
    uint8_t colorMode = static_cast<uint8_t>(Clusters::ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation);
    uint8_t hue = 0;
    uint8_t saturation = 0;
    uint16_t currentX = 0;
    uint16_t currentY = 0;
    uint16_t temperature = kDefaultPowerOnTemperatureMireds;

    (void) emberAfReadAttribute(kLightingEndpointId, Clusters::OnOff::Id, Clusters::OnOff::Attributes::OnOff::Id, &on, sizeof(on));
    (void) emberAfReadAttribute(kLightingEndpointId, Clusters::LevelControl::Id, Clusters::LevelControl::Attributes::CurrentLevel::Id,
                                &level, sizeof(level));
    (void) emberAfReadAttribute(kLightingEndpointId, Clusters::ColorControl::Id, Clusters::ColorControl::Attributes::ColorMode::Id,
                                &colorMode, sizeof(colorMode));

    switch (colorMode)
    {
    case static_cast<uint8_t>(Clusters::ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation):
        (void) emberAfReadAttribute(kLightingEndpointId, Clusters::ColorControl::Id,
                                    Clusters::ColorControl::Attributes::CurrentHue::Id, &hue, sizeof(hue));
        (void) emberAfReadAttribute(kLightingEndpointId, Clusters::ColorControl::Id,
                                    Clusters::ColorControl::Attributes::CurrentSaturation::Id, &saturation,
                                    sizeof(saturation));
        SetLevel(level);
        SetColor(hue, saturation);
        break;
    case static_cast<uint8_t>(Clusters::ColorControl::ColorModeEnum::kCurrentXAndCurrentY):
        (void) emberAfReadAttribute(kLightingEndpointId, Clusters::ColorControl::Id,
                                    Clusters::ColorControl::Attributes::CurrentX::Id, reinterpret_cast<uint8_t *>(&currentX),
                                    sizeof(currentX));
        (void) emberAfReadAttribute(kLightingEndpointId, Clusters::ColorControl::Id,
                                    Clusters::ColorControl::Attributes::CurrentY::Id, reinterpret_cast<uint8_t *>(&currentY),
                                    sizeof(currentY));
        SetLevel(level);
        SetCurrentX(currentX);
        SetCurrentY(currentY);
        break;
    case static_cast<uint8_t>(Clusters::ColorControl::ColorModeEnum::kColorTemperatureMireds):
        (void) emberAfReadAttribute(kLightingEndpointId, Clusters::ColorControl::Id,
                                    Clusters::ColorControl::Attributes::ColorTemperatureMireds::Id,
                                    reinterpret_cast<uint8_t *>(&temperature), sizeof(temperature));
        SetLevel(level);
        SetTemperature(temperature);
        break;
    default:
        SetLevel(level);
        SetColor(kDefaultPowerOnHue, kDefaultPowerOnSaturation);
        break;
    }

    SetOnOff(on != 0);
}

void BkLightingManager::SetColor(uint8_t hue, uint8_t saturation)
{
    ChipLogProgress(DeviceLayer, "color hs update %d/254,%d/254", hue, saturation);
    mColorMode     = ColorMode::Hsv;
    mColorStateValid = true;
    mHue           = ((uint32_t) hue * 360) / 254;
    mSaturation    = ((uint32_t) saturation * 100) / 254;
    HsvColor_t hsv = { mHue, mSaturation, mLevel };
    RgbColor_t rgb = HsvToRgb(hsv);

    // ChipLogProgress(DeviceLayer, "color hsv update %d/360,%d/100,%d/254", hsv.h, hsv.s, hsv.v);
    if (isOn)
    {
        lighting_color_set(rgb.r, rgb.g, rgb.b);
    }
}

void BkLightingManager::SetLevel(uint8_t level)
{
    mLevel         = level;
    mColorStateValid = true;
    HsvColor_t hsv = { mHue, mSaturation, mLevel };
    RgbColor_t rgb = HsvToRgb(hsv);

    // ChipLogProgress(DeviceLayer, "color hsv update %d/360,%d/100,%d/254", hsv.h, hsv.s, hsv.v);
    if (isOn)
    {
        lighting_color_set(rgb.r, rgb.g, rgb.b);
    }
}

void BkLightingManager::StepLevel(int8_t step)
{
    int newLevel = (int) mLevel + (int) step;

    if (newLevel < 1)
    {
        newLevel = 1;
    }
    if (newLevel > 254)
    {
        newLevel = 254;
    }

    SetLevel((uint8_t) newLevel);
}

void BkLightingManager::SetRgbColor(uint8_t r, uint8_t g, uint8_t b)
{
    // 如果当前是关灯，则先打开
    if (!isOn)
    {
        SetOnOff(true);
    }

    // 将指定 RGB 转换为 HSV，更新内部状态，
    // 这样后续的亮度调节仍然沿着同一色相/饱和度变化
    HsvColor_t hsv = RgbToHsvInternal(r, g, b);
    mColorMode  = ColorMode::Hsv;
    mColorStateValid = true;
    mHue        = hsv.h;
    mSaturation = hsv.s;

    // 使用 SetLevel 来统一更新 mLevel 并输出颜色
    SetLevel(hsv.v);
}

void BkLightingManager::SetTemperature(uint16_t temperature)
{
    ChipLogProgress(DeviceLayer, "temperature update %x", temperature);
    mColorMode = ColorMode::Temperature;
    mColorStateValid = true;
    mTemperature = temperature;
    CtColor_t ctMireds = { mTemperature };
    RgbColor_t rgb = CTToRgb(ctMireds);
    if (isOn)
    {
        lighting_color_set(rgb.r, rgb.g, rgb.b);
    }
}
void BkLightingManager::SetCurrentX(uint16_t currentX){
    ChipLogProgress(DeviceLayer, "currentX update %x", currentX);
    mColorMode = ColorMode::Xy;
    mColorStateValid = true;
    mCurrentX = currentX;
    RgbColor_t rgb = XYToRgb(mLevel, mCurrentX, mCurrentY);
    if (isOn)
    {
        lighting_color_set(rgb.r, rgb.g, rgb.b);
    }
}
void BkLightingManager::SetCurrentY(uint16_t currentY){
    ChipLogProgress(DeviceLayer, "currentY update %x", currentY);
    mColorMode = ColorMode::Xy;
    mColorStateValid = true;
    mCurrentY = currentY;
    RgbColor_t rgb = XYToRgb(mLevel, mCurrentX, mCurrentY);
    if (isOn)
    {
        lighting_color_set(rgb.r, rgb.g, rgb.b);
    }
}

void BkLightingManager::SetOnOff(bool on)
{
    ChipLogProgress(DeviceLayer, "SetOnOff %d -> %d", isOn, on);
    if(isOn != on){
        isOn = on;
        lighting_gpio_set(on);
        if (isOn)
        {
            if (!mColorStateValid)
            {
                mColorMode = ColorMode::Hsv;
                mHue = 0;
                mSaturation = 0;
                mLevel = 254;
                mColorStateValid = true;
            }
            ApplyCurrentColor();
        }
    }
    else if (on)
    {
        // A production test can force the physical outputs off without changing
        // the normal light state. Re-applying ON restores the current user color.
        ApplyCurrentColor();
    }
    else
    {
        lighting_gpio_set(false);
    }
}

bool BkLightingManager::IsOn() const
{
    return isOn;
}

uint8_t BkLightingManager::GetLevel() const
{
    return mLevel;
}

void BkLightingManager::CompleteInitAfterCommissioning()
{
    if (chip::Server::GetInstance().GetFabricTable().FabricCount() > 0)
    {
        RestoreCommissionedState();
    }
    else
    {
        ApplyPowerOnDefaults();
    }
}

static uint8_t remote_scale_100_to_254(uint8_t value100)
{
    return static_cast<uint8_t>((static_cast<uint16_t>(value100) * 254U + 50U) / 100U);
}

static uint8_t remote_scale_360_to_254(uint16_t value360)
{
    if (value360 > 360)
    {
        value360 = 360;
    }
    return static_cast<uint8_t>((static_cast<uint32_t>(value360) * 254U + 180U) / 360U);
}

static void remote_light_sync_onoff_attribute(bool on)
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t arg) {
            uint8_t value = (arg != 0) ? 1 : 0;
            auto status = emberAfWriteAttribute(1, chip::app::Clusters::OnOff::Id,
                                                chip::app::Clusters::OnOff::Attributes::OnOff::Id, &value,
                                                sizeof(value));
            if (status != chip::Protocols::InteractionModel::Status::Success)
            {
                ChipLogProgress(DeviceLayer, "Remote on/off sync failed: %u", static_cast<unsigned>(status));
            }
        },
        on ? 1 : 0);
}

static void remote_light_sync_level_attribute(uint8_t level)
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t arg) {
            uint8_t value = static_cast<uint8_t>(arg);
            auto status   = emberAfWriteAttribute(1, chip::app::Clusters::LevelControl::Id,
                                                chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Id,
                                                &value, sizeof(value));
            if (status != chip::Protocols::InteractionModel::Status::Success)
            {
                ChipLogProgress(DeviceLayer, "Remote level sync failed: %u", static_cast<unsigned>(status));
            }
        },
        level);
}

static void remote_light_sync_hs_attribute(uint8_t hue, uint8_t saturation, uint8_t level)
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t arg) {
            uint32_t packed = static_cast<uint32_t>(arg);
            uint8_t hueValue = static_cast<uint8_t>((packed >> 16) & 0xFF);
            uint8_t satValue = static_cast<uint8_t>((packed >> 8) & 0xFF);
            uint8_t levelValue = static_cast<uint8_t>(packed & 0xFF);

            auto hueStatus = emberAfWriteAttribute(1, chip::app::Clusters::ColorControl::Id,
                                                   chip::app::Clusters::ColorControl::Attributes::CurrentHue::Id,
                                                   &hueValue, sizeof(hueValue));
            auto satStatus = emberAfWriteAttribute(1, chip::app::Clusters::ColorControl::Id,
                                                   chip::app::Clusters::ColorControl::Attributes::CurrentSaturation::Id,
                                                   &satValue, sizeof(satValue));
            auto levelStatus = emberAfWriteAttribute(1, chip::app::Clusters::LevelControl::Id,
                                                     chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Id,
                                                     &levelValue, sizeof(levelValue));

            if (hueStatus != chip::Protocols::InteractionModel::Status::Success ||
                satStatus != chip::Protocols::InteractionModel::Status::Success ||
                levelStatus != chip::Protocols::InteractionModel::Status::Success)
            {
                ChipLogProgress(DeviceLayer, "Remote color sync failed: hue=%u sat=%u level=%u",
                                static_cast<unsigned>(hueStatus),
                                static_cast<unsigned>(satStatus),
                                static_cast<unsigned>(levelStatus));
            }
        },
        static_cast<intptr_t>((static_cast<uint32_t>(hue) << 16) |
                              (static_cast<uint32_t>(saturation) << 8) |
                              static_cast<uint32_t>(level)));
}

// 以下接口供 C 代码（遥控 demo）调用，通过写入 Matter 属性来驱动灯光，保证属性和硬件状态一致。
extern "C" void remote_light_on(void)
{
    remote_scene_auto_stop();
    remote_scene_stop();
    stop_music_reactive_effect();
    BkLightingMgr().SetOnOff(true);
    remote_light_sync_onoff_attribute(true);
}

extern "C" void remote_light_off(void)
{
    remote_scene_auto_stop();
    remote_scene_stop();
    stop_music_reactive_effect();
    BkLightingMgr().SetOnOff(false);
    remote_light_sync_onoff_attribute(false);
}

extern "C" bool remote_light_is_on(void)
{
    return BkLightingMgr().IsOn();
}

static void remote_light_level_step(int8_t step)
{
    // 遥控器本地控制亮度：直接通过灯光管理器调整电平
    remote_scene_auto_stop();
    remote_scene_stop();
    stop_music_reactive_effect();
    BkLightingMgr().StepLevel(step);
    remote_light_sync_level_attribute(BkLightingMgr().GetLevel());
}

extern "C" void remote_light_level_up(void)
{
    remote_light_level_step(25);
}

extern "C" void remote_light_level_down(void)
{
    remote_light_level_step(-25);
}

extern "C" void remote_light_toggle(void)
{
    remote_scene_auto_stop();
    remote_scene_stop();
    stop_music_reactive_effect();
    bool on = BkLightingMgr().IsOn();
    bool newOn = !on;
    BkLightingMgr().SetOnOff(newOn);
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t arg) {
            uint8_t value = (arg != 0) ? 1 : 0;
            auto status = emberAfWriteAttribute(1, chip::app::Clusters::OnOff::Id,
                                               chip::app::Clusters::OnOff::Attributes::OnOff::Id, &value,
                                               sizeof(value));
            if (status != chip::Protocols::InteractionModel::Status::Success)
            {
                ChipLogProgress(DeviceLayer, "Remote toggle sync failed: %u", static_cast<unsigned>(status));
            }
        },
        newOn ? 1 : 0);
}

extern "C" void remote_light_red(void)
{
    // 显示固定红色光
    remote_scene_auto_stop();
    remote_scene_stop();
    stop_music_reactive_effect();
    BkLightingMgr().SetRgbColor(255, 0, 0);
    remote_light_sync_hs_attribute(remote_scale_360_to_254(0), remote_scale_100_to_254(100), BkLightingMgr().GetLevel());
}

extern "C" void remote_light_green(void)
{
    // 显示固定绿色光
    remote_scene_auto_stop();
    remote_scene_stop();
    stop_music_reactive_effect();
    BkLightingMgr().SetRgbColor(0, 255, 0);
    remote_light_sync_hs_attribute(remote_scale_360_to_254(120), remote_scale_100_to_254(100), BkLightingMgr().GetLevel());
}

extern "C" void remote_light_blue(void)
{
    // 显示固定蓝色光
    remote_scene_auto_stop();
    remote_scene_stop();
    stop_music_reactive_effect();
    BkLightingMgr().SetRgbColor(0, 0, 255);
    remote_light_sync_hs_attribute(remote_scale_360_to_254(240), remote_scale_100_to_254(100), BkLightingMgr().GetLevel());
}

extern "C" void remote_light_yellow(void)
{
    // 显示指定黄色 (255,191,0)
    remote_scene_auto_stop();
    remote_scene_stop();
    stop_music_reactive_effect();
    BkLightingMgr().SetRgbColor(255, 191, 0);
    HsvColor_t hsv = RgbToHsvInternal(255, 191, 0);
    remote_light_sync_hs_attribute(remote_scale_360_to_254(hsv.h), remote_scale_100_to_254(hsv.s), hsv.v);
}

extern "C" void remote_light_lightblue(void)
{
    // 显示指定浅蓝色 (0,106,215)
    remote_scene_auto_stop();
    remote_scene_stop();
    stop_music_reactive_effect();
    BkLightingMgr().SetRgbColor(0, 106, 215);
    HsvColor_t hsv = RgbToHsvInternal(0, 106, 215);
    remote_light_sync_hs_attribute(remote_scale_360_to_254(hsv.h), remote_scale_100_to_254(hsv.s), hsv.v);
}

extern "C" void remote_light_purple(void)
{
    // 显示指定紫色 (177, 0, 254)
    remote_scene_auto_stop();
    remote_scene_stop();
    stop_music_reactive_effect();
    BkLightingMgr().SetRgbColor(177, 0, 254);
    HsvColor_t hsv = RgbToHsvInternal(177, 0, 254);
    remote_light_sync_hs_attribute(remote_scale_360_to_254(hsv.h), remote_scale_100_to_254(hsv.s), hsv.v);
}

extern "C" void remote_light_white(void)
{
    // W 键：偏暖白
    remote_scene_auto_stop();
    remote_scene_stop();
    stop_music_reactive_effect();
    BkLightingMgr().SetRgbColor(255, 170, 90);
    HsvColor_t hsv = RgbToHsvInternal(255, 170, 90);
    remote_light_sync_hs_attribute(remote_scale_360_to_254(hsv.h), remote_scale_100_to_254(hsv.s), hsv.v);
}

extern "C" void remote_light_cold_white(void)
{
    // C 键（原 COLOR 键）：偏冷白
    remote_scene_auto_stop();
    remote_scene_stop();
    stop_music_reactive_effect();
    BkLightingMgr().SetRgbColor(170, 215, 255);
    HsvColor_t hsv = RgbToHsvInternal(170, 215, 255);
    remote_light_sync_hs_attribute(remote_scale_360_to_254(hsv.h), remote_scale_100_to_254(hsv.s), hsv.v);
}

extern "C" void remote_light_scene1(void)
{
    remote_scene_auto_stop();
    remote_scene_start(1);
}

extern "C" void remote_light_scene2(void)
{
    remote_scene_auto_stop();
    remote_scene_start(2);
}

extern "C" void remote_light_scene3(void)
{
    remote_scene_auto_stop();
    remote_scene_start(3);
}

extern "C" void remote_light_scene_mode_next(void)
{
    remote_scene_auto_stop();
    uint8_t nextScene = static_cast<uint8_t>((s_remote_scene_selected_id % 5) + 1);
    remote_scene_start(nextScene);
}

extern "C" void remote_light_scene_mode_prev(void)
{
    remote_scene_auto_stop();
    uint8_t prevScene = static_cast<uint8_t>((s_remote_scene_selected_id == 1) ? 5 : (s_remote_scene_selected_id - 1));
    remote_scene_start(prevScene);
}

extern "C" void remote_light_scene_auto_toggle(void)
{
    if (s_remote_scene_auto_active)
    {
        remote_scene_auto_stop();
        remote_scene_stop();
        return;
    }

    remote_scene_auto_start();
}

extern "C" void remote_light_music_gentle(void)
{
    remote_scene_auto_stop();
    remote_scene_stop();
    s_musicReactive.Start(MicMusicReactive::Mode::Gentle);
}

extern "C" void remote_light_music_aggressive(void)
{
    remote_scene_auto_stop();
    remote_scene_stop();
    s_musicReactive.Start(MicMusicReactive::Mode::Aggressive);
}

extern "C" void remote_light_music_set_sensitivity(uint8_t level)
{
    // 灵敏度范围 1~10，超出自动夹紧
    s_musicReactive.SetSensitivity(level);
}

extern "C" void remote_light_music_stop(void)
{
    remote_scene_auto_stop();
    stop_music_reactive_effect();
}

// ===================== 定时关机 =====================

static bool s_shutdown_timer_active = false;

static void ShutdownTimerHandler(chip::System::Layer * aLayer, void * aAppState)
{
    (void) aLayer;
    (void) aAppState;
    s_shutdown_timer_active = false;
    ChipLogProgress(DeviceLayer, "[REMOTE_TIMER] shutdown timer fired, turning off light");
    // SystemLayer 回调已在 CHIP 任务上下文中，可直接操作属性和硬件
    // 先停止情景动画和音乐律动，防止其定时器持续调用 SetRgbColor 重新开灯
    remote_scene_auto_stop();
    remote_scene_stop();
    stop_music_reactive_effect();
    BkLightingMgr().SetOnOff(false);
    uint8_t value = 0;
    emberAfWriteAttribute(1, chip::app::Clusters::OnOff::Id,
                          chip::app::Clusters::OnOff::Attributes::OnOff::Id,
                          &value, sizeof(value));
}

extern "C" void remote_light_shutdown_timer_start(uint32_t ms)
{
    // BLE 回调在非 CHIP 任务上下文，需通过 ScheduleWork 跳转
    chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t arg) {
        uint32_t delayMs = static_cast<uint32_t>(arg);
        timer_feedback_blink_green_once();
        chip::DeviceLayer::SystemLayer().CancelTimer(ShutdownTimerHandler, nullptr);
        chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Milliseconds32(delayMs), ShutdownTimerHandler, nullptr);
        s_shutdown_timer_active = true;
        ChipLogProgress(DeviceLayer, "[REMOTE_TIMER] shutdown scheduled in %lu ms", static_cast<unsigned long>(delayMs));
    }, static_cast<intptr_t>(ms));
}

extern "C" void remote_light_shutdown_timer_cancel(void)
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
        if (s_shutdown_timer_active)
        {
            chip::DeviceLayer::SystemLayer().CancelTimer(ShutdownTimerHandler, nullptr);
            s_shutdown_timer_active = false;
            ChipLogProgress(DeviceLayer, "[REMOTE_TIMER] shutdown timer cancelled");
        }
    }, 0);
}

static void stop_competing_effects_for_production_rgb()
{
    remote_scene_auto_stop();
    remote_scene_stop();
    stop_music_reactive_effect();

    if (s_indicator_timer_inited)
    {
        ql_rtos_timer_stop(s_indicator_timer);
    }
    s_indicator_active = false;
    s_indicator_on = false;
    s_saved_valid = false;

    if (s_timer_feedback_timer_inited)
    {
        ql_rtos_timer_stop(s_timer_feedback_timer);
    }
    s_timer_feedback_active = false;
    s_timer_feedback_phase = 0;
}

bool BkLightingManager::StartProductionRgbOutput()
{
#ifdef BEKEN_SOC_BK7231
    if (!s_rgb_output_mutex_inited)
    {
        ChipLogError(DeviceLayer, "Production RGB test: output mutex unavailable");
        return false;
    }

    ql_rtos_mutex_lock(s_rgb_output_mutex);
    s_production_rgb_output_active = true;
    lighting_color_set_raw(0, 0, 0);
    ql_rtos_mutex_unlock(s_rgb_output_mutex);

    stop_competing_effects_for_production_rgb();
    return true;
#else
    ChipLogError(DeviceLayer, "Production RGB test: physical PWM output is not compiled");
    return false;
#endif
}

void BkLightingManager::SetProductionRgbOutput(uint8_t r, uint8_t g, uint8_t b)
{
#ifdef BEKEN_SOC_BK7231
    if (!s_rgb_output_mutex_inited)
    {
        return;
    }

    ql_rtos_mutex_lock(s_rgb_output_mutex);
    if (s_production_rgb_output_active)
    {
        lighting_color_set_raw(r, g, b);
    }
    ql_rtos_mutex_unlock(s_rgb_output_mutex);
#else
    (void) r;
    (void) g;
    (void) b;
#endif
}

void BkLightingManager::StopProductionRgbOutput()
{
#ifdef BEKEN_SOC_BK7231
    if (!s_rgb_output_mutex_inited)
    {
        return;
    }

    // Keep production ownership while stopping effects so a pending timer
    // callback cannot overwrite the final all-off output.
    stop_competing_effects_for_production_rgb();

    ql_rtos_mutex_lock(s_rgb_output_mutex);
    lighting_color_set_raw(0, 0, 0);
    s_production_rgb_output_active = false;
    ql_rtos_mutex_unlock(s_rgb_output_mutex);
#endif
}
