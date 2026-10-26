#include "LightingRemoteBridge.h"

#include "BkLightingManager.h"
#include "BkDriverGpio.h"
#include "gpio_pub.h"

#include <lib/support/logging/CHIPLogging.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/AppDelegate.h>
#include <app/server/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <platform/Beken/BLEManagerImpl.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/KeyValueStoreManager.h>
#include <platform/PlatformManager.h>
#include <credentials/FabricTable.h>
#include <system/SystemClock.h>
#include <string.h>

extern "C" {
#include "../../../../quectel/quec_adapt/api/ql_wlan.h"
}

using namespace chip;

namespace {
constexpr uint8_t kMaxRemotePairs = 1; // 限制为只支持 1 个遥控器（锁定行为）
constexpr const char * kRemotePairKey = "lighting-remote-pairs-id";
constexpr const char * kBootCycleCountKey = "lighting-phone-cfg-boot-count";
constexpr uint32_t kPairWindowMsDefault = 0;        // 默认对码窗口：0 表示无超时（按键 0x02 始终可切换锁定）
constexpr uint32_t kIndicatorOnlyMs = 30000;        // 蓝灯闪烁最长持续时长（ms），超时后仅关灯，对码继续后台运行
constexpr uint32_t kCommissioningWindowMs = 180000;     // 配网窗口默认时长（ms），超时配网窗口自动关闭，进入正常工作状态

constexpr bk_gpio_t kPairKeyGpio = GPIO17;
constexpr uint32_t kKeyPollIntervalMs = 20;
constexpr uint8_t kKeyDebounceCount = 2;
constexpr uint32_t kKeyPollStartDelayMs = 1000;
constexpr uint32_t kKeyLongPressMs = 5000;
constexpr uint32_t kBootCycleWindowMs = 30000;      //30秒内上电kBootCycleCountForCommissioning次视为连续重启，触发进入手机配网模式
constexpr uint8_t kBootCycleCountForCommissioning = 3;

struct RemotePairTable
{
    uint8_t count;
    uint8_t next;
    uint16_t ids[kMaxRemotePairs];
};

struct KeyState
{
    bool pressed;
    uint8_t debounce;
    uint32_t holdMs;
    bool longHandled;
};

RemotePairTable sRemotePairs;
bool sRemotePairsLoaded = false;
bool sPairingWindowActive = false;
bool sInitDone = false;
bool sDeferPowerOnDefaults = false;
bool sPhoneCommissioningActive = false;
bool sPhoneCommissioningSoftApActive = false;
bool sSuppressCommissioningClosedCallback = false;
const char * sPhoneCommissioningTrigger = "none";

KeyState sKeyState = { false, 0, 0, false };
uint8_t sBootCycleCount = 0;

extern "C" void ql_ble_remote_set_scan_enable(int enable);
extern "C" void ql_ble_remote_force_stop_scan(void);
extern "C" void ql_ble_remote_set_suspend(int enable);
extern "C" void ql_ble_demo_central_thread_creat(void);
extern "C" void LightingIndicatorStart(uint8_t mode);
extern "C" void LightingIndicatorStop(void);
extern "C" void LightingRemoteOpenPairingWindow(uint32_t durationMs);
extern "C" void remote_light_toggle(void);

void PairingWindowTimeout(chip::System::Layer *, void *);
void StartCommissioningWindowHandler(chip::System::Layer *, void *);
void IndicatorOnlyTimeout(chip::System::Layer *, void *);
void ReopenCommissioningAfterLastFabricRemoved(intptr_t);
void ExitPhoneCommissioningMode(const char * reason);
void ResumeRemoteReceive(const char * reason);
void StartPairingWindowSoftAP();
void StopPairingWindowSoftAP();
void StopRemoteScan();

void CloseRemotePairingWindowState()
{
    sPairingWindowActive = false;
    DeviceLayer::SystemLayer().CancelTimer(PairingWindowTimeout, nullptr);
    DeviceLayer::SystemLayer().CancelTimer(IndicatorOnlyTimeout, nullptr);
}

void EnterPhoneCommissioningMode(const char * triggerSource)
{
    auto & server = Server::GetInstance();

    if (sPhoneCommissioningActive)
    {
        ChipLogProgress(DeviceLayer, "Phone commissioning mode already active, ignore trigger=%s", triggerSource);
        return;
    }

    sPhoneCommissioningActive = true;
    sPhoneCommissioningTrigger = triggerSource;
    ChipLogProgress(DeviceLayer,
                    "Entering phone commissioning mode: trigger=%s fabricCount=%u duration=%lu ms",
                    sPhoneCommissioningTrigger,
                    static_cast<unsigned>(server.GetFabricTable().FabricCount()),
                    static_cast<unsigned long>(kCommissioningWindowMs));

    CloseRemotePairingWindowState();
    LightingIndicatorStop();
    StopPairingWindowSoftAP();
    StopRemoteScan();
    ql_ble_remote_set_suspend(1);
    LightingIndicatorStart(1);
    StartPairingWindowSoftAP();

    // 已经配网过时，先清掉旧的 Matter 配网信息，再重新打开配网窗口。
    if (server.GetFabricTable().FabricCount() > 0)
    {
        if (server.GetCommissioningWindowManager().IsCommissioningWindowOpen())
        {
            sSuppressCommissioningClosedCallback = true;
            server.GetCommissioningWindowManager().CloseCommissioningWindow();
        }
        server.GetFabricTable().DeleteAllFabrics();
    }

    DeviceLayer::ConnectivityMgr().ClearWiFiStationProvision();

    DeviceLayer::SystemLayer().CancelTimer(StartCommissioningWindowHandler, nullptr);
    DeviceLayer::SystemLayer().StartTimer(System::Clock::Milliseconds32(0), StartCommissioningWindowHandler, nullptr);
}

void StartPairingWindowSoftAP()
{
    if (sPhoneCommissioningSoftApActive)
    {
        ChipLogProgress(DeviceLayer, "SoftAP already active for phone commissioning");
        return;
    }

    // 生成带手动配对码后缀的 SoftAP SSID，便于现场识别设备。
    char manualBuf[64];
    chip::MutableCharSpan manualSpan(manualBuf);
    CHIP_ERROR err = GetManualPairingCode(manualSpan,
        chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kSoftAP));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "StartPairingWindowSoftAP: GetManualPairingCode failed");
        return;
    }

    const char * prefix    = "XH-";
    char ssid[33];
    size_t prefix_len  = strlen(prefix);
    size_t manual_len  = strlen(manualSpan.data());
    size_t copy_manual = (sizeof(ssid) - 1 > prefix_len) ? (sizeof(ssid) - 1 - prefix_len) : 0;
    if (manual_len > copy_manual)
        manual_len = copy_manual;
    memcpy(ssid, prefix, prefix_len);
    memcpy(ssid + prefix_len, manualSpan.data(), manual_len);
    ssid[prefix_len + manual_len] = '\0';

    char apkey[] = "12345678";

    ql_network_init_s netCfg;
    memset(&netCfg, 0, sizeof(netCfg));
    netCfg.wifi_mode = QL_SOFT_AP;
    strncpy(netCfg.wifi_ssid, ssid, sizeof(netCfg.wifi_ssid) - 1);
    netCfg.wifi_ssid[sizeof(netCfg.wifi_ssid) - 1] = '\0';
    strncpy(netCfg.wifi_key, apkey, sizeof(netCfg.wifi_key) - 1);
    netCfg.wifi_key[sizeof(netCfg.wifi_key) - 1] = '\0';
    strncpy(netCfg.local_ip_addr, "192.168.10.1", sizeof(netCfg.local_ip_addr) - 1);
    netCfg.local_ip_addr[sizeof(netCfg.local_ip_addr) - 1] = '\0';
    strncpy(netCfg.net_mask, "255.255.255.0", sizeof(netCfg.net_mask) - 1);
    netCfg.net_mask[sizeof(netCfg.net_mask) - 1] = '\0';
    strncpy(netCfg.dns_server_ip_addr, "192.168.10.1", sizeof(netCfg.dns_server_ip_addr) - 1);
    netCfg.dns_server_ip_addr[sizeof(netCfg.dns_server_ip_addr) - 1] = '\0';
    netCfg.dhcp_mode = 2; // DHCP server

    int ret = ql_wlan_start(&netCfg);
    if (ret != 0)
    {
        ChipLogError(DeviceLayer, "SoftAP start failed: %d", ret);
    }
    else
    {
        sPhoneCommissioningSoftApActive = true;
        ChipLogProgress(DeviceLayer, "SoftAP started, SSID=%s", ssid);
    }
}

void StopPairingWindowSoftAP()
{
    // 关闭手机配网模式使用的 SoftAP。
    if (!sPhoneCommissioningSoftApActive)
    {
        return;
    }

    int ret = ql_wlan_stop(QL_SOFT_AP);
    if (ret != 0)
    {
        ChipLogError(DeviceLayer, "SoftAP stop failed: %d", ret);
    }
    else
    {
        sPhoneCommissioningSoftApActive = false;
        ChipLogProgress(DeviceLayer, "SoftAP stopped");
    }
}

class LightingCommissioningDelegate : public AppDelegate
{
public:
    void OnCommissioningWindowOpened() override
    {
        ChipLogProgress(DeviceLayer, "Commissioning window opened: trigger=%s active=%d", sPhoneCommissioningTrigger,
                        sPhoneCommissioningActive ? 1 : 0);
    }

    void OnCommissioningWindowClosed() override
    {
        if (sSuppressCommissioningClosedCallback)
        {
            sSuppressCommissioningClosedCallback = false;
            ChipLogProgress(DeviceLayer, "Commissioning window close callback suppressed during mode switch");
            return;
        }

        ChipLogProgress(DeviceLayer, "Commissioning window closed: trigger=%s active=%d", sPhoneCommissioningTrigger,
                        sPhoneCommissioningActive ? 1 : 0);
        LightingRemoteEnableAfterCommissioning();
    }
};

LightingCommissioningDelegate sCommissioningDelegate;

class LightingFabricDelegate : public chip::FabricTable::Delegate
{
public:
    void OnFabricCommitted(const FabricTable &, FabricIndex) override
    {
        ChipLogProgress(DeviceLayer, "Fabric committed while trigger=%s active=%d", sPhoneCommissioningTrigger,
                        sPhoneCommissioningActive ? 1 : 0);
    }

    void OnFabricRemoved(const FabricTable & fabricTable, FabricIndex fabricIndex) override
    {
        // 长按/上电进入配网时会自己删 Fabric，此时窗口已经在打开，不能再嵌套进来。
        if (sPhoneCommissioningActive || fabricTable.FabricCount() != 0)
        {
            return;
        }

        ChipLogProgress(DeviceLayer, "Last fabric 0x%x removed, scheduling commissioning window",
                        static_cast<unsigned>(fabricIndex));
        // RemoveFabric 的响应还要靠当前会话发出，等这条命令返回后再开广播。
        DeviceLayer::PlatformMgr().ScheduleWork(ReopenCommissioningAfterLastFabricRemoved, 0);
    }
};

LightingFabricDelegate sFabricDelegate;

void StartRemoteScan()
{
    // 确保 central 线程存在后再开启遥控扫描。
    ql_ble_demo_central_thread_creat();

    ql_ble_remote_set_scan_enable(1);
}

void StopRemoteScan()
{
    // 立即停扫并强制结束当前扫描过程。
    ql_ble_remote_set_scan_enable(0);
    ql_ble_remote_force_stop_scan();
}

void ReopenCommissioningAfterLastFabricRemoved(intptr_t)
{
    if (sPhoneCommissioningActive)
    {
        return;
    }
    if (Server::GetInstance().GetFabricTable().FabricCount() != 0)
    {
        return;
    }

    ChipLogProgress(DeviceLayer, "Last fabric removed, reopening commissioning window");
    // 先停遥控器主机，再把控制器交回 CHIPoBLE，最后走现有的手机配网窗口。
    StopRemoteScan();
    ql_ble_remote_set_suspend(1);
    DeviceLayer::Internal::BLEMgrImpl().RestartForCommissioning();
    EnterPhoneCommissioningMode("last-fabric-removed");
}

void LoadRemotePairs()
{
    // 只在首次使用时从 KV 加载，后续走内存缓存。
    if (sRemotePairsLoaded)
    {
        return;
    }

    memset(&sRemotePairs, 0, sizeof(sRemotePairs));
    size_t len = sizeof(sRemotePairs);
    CHIP_ERROR err = DeviceLayer::PersistedStorage::KeyValueStoreMgr().Get(kRemotePairKey, &sRemotePairs, len);
    if (err != CHIP_NO_ERROR)
    {
        memset(&sRemotePairs, 0, sizeof(sRemotePairs));
    }

    if (sRemotePairs.count > kMaxRemotePairs)
    {
        sRemotePairs.count = 0;
        sRemotePairs.next = 0;
        memset(sRemotePairs.ids, 0, sizeof(sRemotePairs.ids));
    }

    sRemotePairsLoaded = true;
}

void SaveRemotePairs()
{
    // 配对表每次变更后立即持久化。
    (void) DeviceLayer::PersistedStorage::KeyValueStoreMgr().Put(kRemotePairKey, &sRemotePairs, sizeof(sRemotePairs));
}

void ClearRemotePairsPersisted()
{
    memset(&sRemotePairs, 0, sizeof(sRemotePairs));
    sRemotePairsLoaded = true;

    CHIP_ERROR err = DeviceLayer::PersistedStorage::KeyValueStoreMgr().Delete(kRemotePairKey);
    if (err != CHIP_NO_ERROR && err != CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND)
    {
        ChipLogProgress(DeviceLayer, "Clear remote pairs failed: 0x%lx", static_cast<unsigned long>(err.AsInteger()));
    }
}

void ClearRemotePairsAndOpenPairingWindow()
{
    auto & server = Server::GetInstance();

    if (server.GetCommissioningWindowManager().IsCommissioningWindowOpen())
    {
        server.GetCommissioningWindowManager().CloseCommissioningWindow();
    }

    // 清除手机配网（Fabric）信息
    server.GetFabricTable().DeleteAllFabrics();

    ClearRemotePairsPersisted();

    LightingIndicatorStop();
    LightingRemoteOpenPairingWindow(kPairWindowMsDefault);
}

int FindRemotePairIndex(uint16_t remoteId)
{
    for (uint8_t i = 0; i < sRemotePairs.count && i < kMaxRemotePairs; ++i)
    {
        if (sRemotePairs.ids[i] == remoteId)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void ClosePairingWindowWork(intptr_t)
{
    // 统一的“关闭遥控对码窗口”收尾动作。
    CloseRemotePairingWindowState();
}

void PairingWindowTimeout(chip::System::Layer *, void *)
{
    CloseRemotePairingWindowState();
}

void IndicatorOnlyTimeout(chip::System::Layer *, void *)
{
    // 遥控器对码窗口不再驱动闪烁指示灯，这里只做兼容保护，不应影响当前灯态。
    LightingIndicatorStop();
}

void BootCycleWindowTimeout(chip::System::Layer *, void *)
{
    // 1 分钟窗口超时后，清空上电次数统计。
    ChipLogProgress(DeviceLayer,
                    "Boot-cycle window expired after %lu ms, clearing counter",
                    static_cast<unsigned long>(kBootCycleWindowMs));
    sBootCycleCount = 0;
    (void) DeviceLayer::PersistedStorage::KeyValueStoreMgr().Delete(kBootCycleCountKey);
}

void StartCommissioningWindowHandler(chip::System::Layer *, void *)
{
    // 打开 Matter 手机配网窗口。
    auto & server = Server::GetInstance();
    CHIP_ERROR err = server.GetCommissioningWindowManager().OpenBasicCommissioningWindow(
        chip::System::Clock::Seconds32(kCommissioningWindowMs / 1000));
    ChipLogProgress(DeviceLayer,
                    "Opening basic commissioning window: trigger=%s duration=%lu ms",
                    sPhoneCommissioningTrigger,
                    static_cast<unsigned long>(kCommissioningWindowMs));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogProgress(DeviceLayer, "OpenBasicCommissioningWindow failed: 0x%lx", static_cast<unsigned long>(err.AsInteger()));
        ExitPhoneCommissioningMode("open-window-failed");
    }
}

void StartKeyPollingHandler(chip::System::Layer * systemLayer, void * ctx);

void KeyPollHandler(chip::System::Layer * systemLayer, void * ctx)
{
    (void) systemLayer;
    (void) ctx;

    bool isLow = !BkGpioInputGet(kPairKeyGpio); // 上拉输入：低为按下

    if (isLow)
    {
        if (!sKeyState.pressed)
        {
            if (++sKeyState.debounce >= kKeyDebounceCount)
            {
                sKeyState.pressed = true;
                sKeyState.holdMs = 0;
                sKeyState.longHandled = false;
            }
        }
        else
        {
            sKeyState.holdMs += kKeyPollIntervalMs;
            if (!sKeyState.longHandled && sKeyState.holdMs >= kKeyLongPressMs)
            {
                // 长按 5 秒：进入手机配网模式。
                sKeyState.longHandled = true;
                EnterPhoneCommissioningMode("key-long-press");
            }
        }
    }
    else
    {
        if (sKeyState.pressed)
        {
            if (sKeyState.longHandled)
            {
                // 长按已处理，抬手只做状态复位，不触发单击动作。
                sKeyState.pressed = false;
                sKeyState.debounce = 0;
                sKeyState.holdMs = 0;
                sKeyState.longHandled = false;
                DeviceLayer::SystemLayer().StartTimer(System::Clock::Milliseconds32(kKeyPollIntervalMs), KeyPollHandler, nullptr);
                return;
            }

            remote_light_toggle();
        }

        sKeyState.pressed = false;
        sKeyState.debounce = 0;
        sKeyState.holdMs = 0;
        sKeyState.longHandled = false;
    }

    DeviceLayer::SystemLayer().StartTimer(System::Clock::Milliseconds32(kKeyPollIntervalMs), KeyPollHandler, nullptr);
}

void StartKeyPollingHandler(chip::System::Layer * systemLayer, void * ctx)
{
    (void) systemLayer;
    (void) ctx;
    KeyPollHandler(systemLayer, ctx);
}

} // namespace

extern "C" void LightingRemoteInit(void)
{
    if (sInitDone)
    {
        return;
    }

    sInitDone = true;

    LoadRemotePairs();
    BkGpioInitialize(kPairKeyGpio, INPUT_PULL_UP);
    sKeyState = { false, 0, 0, false };
    sDeferPowerOnDefaults = false;
    bool enterPhoneCommissioningByBootCycle = false;

    size_t bootLen = sizeof(sBootCycleCount);
    // 读取并累计上电次数，用于“1 分钟内上电 3 次”入口。
    CHIP_ERROR bootErr = DeviceLayer::PersistedStorage::KeyValueStoreMgr().Get(kBootCycleCountKey, &sBootCycleCount, bootLen);
    if (bootErr != CHIP_NO_ERROR || bootLen != sizeof(sBootCycleCount))
    {
        ChipLogProgress(DeviceLayer,
                        "Boot-cycle counter read miss/reset: err=0x%lx len=%u expected=%u",
                        static_cast<unsigned long>(bootErr.AsInteger()), static_cast<unsigned>(bootLen),
                        static_cast<unsigned>(sizeof(sBootCycleCount)));
        sBootCycleCount = 0;
    }
    else
    {
        ChipLogProgress(DeviceLayer,
                        "Boot-cycle counter read: count=%u window=%lu ms threshold=%u",
                        static_cast<unsigned>(sBootCycleCount), static_cast<unsigned long>(kBootCycleWindowMs),
                        static_cast<unsigned>(kBootCycleCountForCommissioning));
    }
    if (sBootCycleCount < 0xFF)
    {
        sBootCycleCount++;
    }
    (void) DeviceLayer::PersistedStorage::KeyValueStoreMgr().Put(kBootCycleCountKey, &sBootCycleCount, sizeof(sBootCycleCount));
    ChipLogProgress(DeviceLayer,
                    "Boot-cycle counter stored: count=%u window=%lu ms threshold=%u",
                    static_cast<unsigned>(sBootCycleCount), static_cast<unsigned long>(kBootCycleWindowMs),
                    static_cast<unsigned>(kBootCycleCountForCommissioning));

    DeviceLayer::SystemLayer().CancelTimer(BootCycleWindowTimeout, nullptr);
    DeviceLayer::SystemLayer().StartTimer(System::Clock::Milliseconds32(kBootCycleWindowMs), BootCycleWindowTimeout, nullptr);

    if (sBootCycleCount >= kBootCycleCountForCommissioning)
    {
        // 命中上电次数阈值后立即清零，避免重复触发。
        ChipLogProgress(DeviceLayer,
                        "Boot-cycle threshold reached: count=%u, entering phone commissioning mode",
                        static_cast<unsigned>(sBootCycleCount));
        sBootCycleCount = 0;
        (void) DeviceLayer::PersistedStorage::KeyValueStoreMgr().Delete(kBootCycleCountKey);
        sDeferPowerOnDefaults = true;
        enterPhoneCommissioningByBootCycle = true;
    }

    // 默认进入遥控对码窗口；若检测到上电 3 次触发，则改为进入手机配网模式。
    ql_ble_remote_set_scan_enable(0);

    auto & server = Server::GetInstance();
    server.GetCommissioningWindowManager().SetAppDelegate(&sCommissioningDelegate);
    server.GetFabricTable().AddFabricDelegate(&sFabricDelegate);
    if (server.GetFabricTable().FabricCount() == 0)
    {
        // 解除配网不会清 wifi-ssid。不先清已配网标记的话，下面关窗会把 BLE 线程退掉。
        DeviceLayer::Internal::BLEMgrImpl().RestartForCommissioning();
    }
    if (server.GetCommissioningWindowManager().IsCommissioningWindowOpen())
    {
        server.GetCommissioningWindowManager().CloseCommissioningWindow();
    }

    if (enterPhoneCommissioningByBootCycle)
    {
        EnterPhoneCommissioningMode("boot-cycle");
    }
    else if (server.GetFabricTable().FabricCount() == 0)
    {
        // 未配网（新烧录或 matter_factory_reset 之后）直接打开配网窗口。
        // 云端写入的工厂 uniqueId 在这次广播里通过 C3 读出。已配网设备仍不广播。
        ChipLogProgress(DeviceLayer, "No fabric on boot, opening commissioning window for BLE scan");
        EnterPhoneCommissioningMode("uncommissioned-boot");
    }
    else
    {
        ChipLogProgress(DeviceLayer, "Boot-cycle threshold not reached, opening remote pairing window");
        LightingRemoteOpenPairingWindow(kPairWindowMsDefault);
    }

    DeviceLayer::SystemLayer().CancelTimer(StartKeyPollingHandler, nullptr);
    DeviceLayer::SystemLayer().StartTimer(System::Clock::Milliseconds32(kKeyPollStartDelayMs), StartKeyPollingHandler, nullptr);

}

extern "C" void LightingRemoteOpenPairingWindow(uint32_t durationMs)
{
    // 打开遥控器对码窗口（与手机配网窗口是两条独立流程）。
    sPairingWindowActive = true;

    ql_ble_remote_set_suspend(0);

    DeviceLayer::SystemLayer().CancelTimer(PairingWindowTimeout, nullptr);
    if (durationMs > 0)
    {
        DeviceLayer::SystemLayer().StartTimer(System::Clock::Milliseconds32(durationMs), PairingWindowTimeout, nullptr);
    }

    // 遥控器对码窗口不再启动闪烁指示灯，清掉遗留超时定时器，避免误关当前灯态。
    DeviceLayer::SystemLayer().CancelTimer(IndicatorOnlyTimeout, nullptr);

    StartRemoteScan();
}

extern "C" bool LightingRemoteIsPairingWindowOpen(void)
{
    return sPairingWindowActive;
}

extern "C" int LightingRemoteIsAuthorized(uint16_t remoteId)
{
    LoadRemotePairs();

    // 如果当前没有任何已配对遥控器，则认为设备未被锁定，允许任意遥控器控制。
    if (sRemotePairs.count == 0)
    {
        return 1;
    }

    int idx = FindRemotePairIndex(remoteId);
    return (idx >= 0) ? 1 : 0;
}

extern "C" int LightingRemoteHasPairs(void)
{
    LoadRemotePairs();
    return (sRemotePairs.count > 0) ? 1 : 0;
}

extern "C" int LightingRemoteIsPaired(uint16_t remoteId)
{
    LoadRemotePairs();
    return (FindRemotePairIndex(remoteId) >= 0) ? 1 : 0;
}

extern "C" void LightingRemotePairRequest(uint16_t remoteId)
{
    LoadRemotePairs();

    int idx = FindRemotePairIndex(remoteId);
    if (idx < 0)
    {
        if (sRemotePairs.count < kMaxRemotePairs)
        {
            sRemotePairs.ids[sRemotePairs.count] = remoteId;
            sRemotePairs.count++;
        }
        else
        {
            sRemotePairs.ids[sRemotePairs.next] = remoteId;
            sRemotePairs.next = (sRemotePairs.next + 1) % kMaxRemotePairs;
        }

        SaveRemotePairs();
    }

    DeviceLayer::PlatformMgr().ScheduleWork(ClosePairingWindowWork, 0);
}

extern "C" int LightingRemoteClearPair(uint16_t remoteId)
{
    LoadRemotePairs();

    int idx = FindRemotePairIndex(remoteId);
    if (idx < 0)
    {
        return 0;
    }

    for (uint8_t i = static_cast<uint8_t>(idx + 1); i < sRemotePairs.count; ++i)
    {
        sRemotePairs.ids[i - 1] = sRemotePairs.ids[i];
    }

    if (sRemotePairs.count > 0)
    {
        sRemotePairs.count--;
    }

    if (sRemotePairs.count < kMaxRemotePairs)
    {
        sRemotePairs.next = 0;
    }
    else if (sRemotePairs.next >= sRemotePairs.count)
    {
        sRemotePairs.next = 0;
    }

    SaveRemotePairs();

    return 1;
}

extern "C" void LightingRemoteFirstUsedNotify(void)
{
    if (sPairingWindowActive)
    {
        DeviceLayer::PlatformMgr().ScheduleWork(ClosePairingWindowWork, 0);
    }
}

extern "C" void LightingRemoteEnableAfterCommissioning(void)
{
    ExitPhoneCommissioningMode("commissioning-finished");
}

extern "C" bool LightingRemoteShouldDeferPowerOnDefaults(void)
{
    return sDeferPowerOnDefaults;
}

namespace {

void ResumeRemoteReceive(const char * reason)
{
    ChipLogProgress(DeviceLayer, "Resume remote receive: reason=%s", reason);
    ql_ble_remote_set_suspend(0);
    StartRemoteScan();
}

void ExitPhoneCommissioningMode(const char * reason)
{
    if (!sPhoneCommissioningActive)
    {
        ChipLogProgress(DeviceLayer, "Ignore phone commissioning exit: already inactive, reason=%s", reason);
        return;
    }

    ChipLogProgress(DeviceLayer, "Exit phone commissioning mode: reason=%s trigger=%s", reason, sPhoneCommissioningTrigger);

    sPhoneCommissioningActive = false;
    sPhoneCommissioningTrigger = "none";
    sDeferPowerOnDefaults = false;

    LightingIndicatorStop();
    StopPairingWindowSoftAP();
    BkLightingMgr().SetOnOff(false);
    ResumeRemoteReceive(reason);
}

} // namespace
