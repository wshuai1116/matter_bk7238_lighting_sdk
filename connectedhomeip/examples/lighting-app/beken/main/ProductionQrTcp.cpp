/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
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

#include "ProductionQrTcp.h"

#include "BkLightingManager.h"

#include <app/server/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <lib/support/CHIPMemString.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <lwip/sockets.h>

#include "mem_pub.h"

extern "C" {
#include "../../../../quectel/quec_adapt/api/ql_osi.h"
#include "wlan_ui_pub.h"
}

using namespace chip;
using namespace chip::DeviceLayer;

namespace {

constexpr const char * kProductionSsid     = "yourwifi";
constexpr const char * kProductionPassword = "yourwifipassword";
constexpr const char * kServerIp           = "your.server.ip.address";
constexpr uint16_t kServerPort             = 0;  // your server port

constexpr uint32_t kScanTimeoutMs        = 8000;
constexpr uint32_t kConnectTimeoutMs     = 30000;
constexpr uint32_t kTcpReconnectDelayMs  = 5000;
constexpr uint32_t kWifiPollIntervalMs   = 250;
constexpr uint32_t kSocketRecvTimeoutSec = 2;
constexpr uint32_t kSocketSendTimeoutSec = 2;
constexpr uint32_t kReportSendCount      = 3;
constexpr uint32_t kReportSendIntervalMs = 200;
constexpr uint32_t kRgbStepIntervalMs    = 500;
constexpr uint32_t kRgbStopTimeoutMs     = 2500;

ql_task_t sProductionTask = nullptr;
bool sStarted             = false;
volatile bool sScanDone            = false;
volatile bool sScanFoundProduction = false;
ql_task_t sProductionRgbTask = nullptr;
ql_mutex_t sProductionRgbMutex = nullptr;
ql_event_group_t sProductionRgbEvents = nullptr;
ql_sem_t sProductionRgbStopped = nullptr;
bool sProductionRgbRunning = false;
bool sProductionRgbExitRequested = false;
uint8_t sProductionRgbChannel = 0;
constexpr uint32_t kProductionRgbWakeBit = 1U << 0;

bool IsProductionSsid(const char * ssid)
{
    return ssid != nullptr && strcmp(ssid, kProductionSsid) == 0;
}

void ScanDoneCallback(void * arg, unsigned char vifIdx)
{
    ScanResult_adv result;
    memset(&result, 0, sizeof(result));

    sScanFoundProduction = false;
    if (wlan_sta_scan_result(&result) == 0 && result.ApList != nullptr)
    {
        for (int i = 0; i < result.ApNum; ++i)
        {
            result.ApList[i].ssid[sizeof(result.ApList[i].ssid) - 1] = '\0';
            ChipLogProgress(DeviceLayer, "Production QR TCP: scan result ssid=%s rssi=%d channel=%d", result.ApList[i].ssid,
                            result.ApList[i].ApPower, result.ApList[i].channel);
            if (IsProductionSsid(result.ApList[i].ssid))
            {
                sScanFoundProduction = true;
                break;
            }
        }
    }

    sScanDone = true;

    if (result.ApList != nullptr)
    {
        os_free(result.ApList);
    }
}

bool ScanProductionWifiOnce()
{
    sScanDone            = false;
    sScanFoundProduction = false;
    bk_wlan_scan_ap_reg_cb(ScanDoneCallback);

    UINT8 * ssidArray[1];
    ssidArray[0] = reinterpret_cast<UINT8 *>(const_cast<char *>(kProductionSsid));
    bk_wlan_start_assign_scan(ssidArray, 1);

    const uint32_t pollCount = kScanTimeoutMs / kWifiPollIntervalMs;
    for (uint32_t i = 0; i < pollCount && !sScanDone; ++i)
    {
        ql_rtos_task_sleep_ms(kWifiPollIntervalMs);
    }

    bk_wlan_scan_ap_reg_cb(nullptr);

    if (!sScanDone)
    {
        ChipLogError(DeviceLayer, "Production QR TCP: scan timeout");
        return false;
    }

    ChipLogProgress(DeviceLayer, "Production QR TCP: production SSID %s", sScanFoundProduction ? "found" : "not found");
    return sScanFoundProduction;
}

bool WaitProductionWifiGotIp()
{
    const uint32_t pollCount = kConnectTimeoutMs / kWifiPollIntervalMs;
    for (uint32_t i = 0; i < pollCount; ++i)
    {
        IPStatusTypedef ipStatus;
        memset(&ipStatus, 0, sizeof(ipStatus));
        if (bk_wlan_get_ip_status(&ipStatus, BK_STATION) == 0 && ipStatus.ip[0] != '\0' &&
            strcmp(ipStatus.ip, "0.0.0.0") != 0)
        {
            LinkStatusTypeDef linkStatus;
            memset(&linkStatus, 0, sizeof(linkStatus));
            if (bk_wlan_get_link_status(&linkStatus) == 0)
            {
                ChipLogProgress(DeviceLayer, "Production QR TCP: connected, ssid=%s ip=%s",
                                reinterpret_cast<const char *>(linkStatus.ssid), ipStatus.ip);
            }
            else
            {
                ChipLogProgress(DeviceLayer, "Production QR TCP: connected, ip=%s", ipStatus.ip);
            }
            return true;
        }

        ql_rtos_task_sleep_ms(kWifiPollIntervalMs);
    }

    ChipLogError(DeviceLayer, "Production QR TCP: connect production Wi-Fi timeout");
    return false;
}

bool ConnectProductionWifiOnce()
{
    network_InitTypeDef_st netCfg;
    memset(&netCfg, 0, sizeof(netCfg));

    netCfg.wifi_mode = BK_STATION;
    chip::Platform::CopyString(netCfg.wifi_ssid, kProductionSsid);
    chip::Platform::CopyString(netCfg.wifi_key, kProductionPassword);
    netCfg.dhcp_mode           = DHCP_CLIENT;
    netCfg.wifi_retry_interval = 0;

    int ret = bk_wlan_start(&netCfg);
    if (ret != 0)
    {
        ChipLogError(DeviceLayer, "Production QR TCP: ql_wlan_start failed: %d", ret);
        return false;
    }

    if (WaitProductionWifiGotIp())
    {
        return true;
    }

    bk_wlan_stop(BK_STATION);
    return false;
}

bool IsStillOnProductionWifi()
{
    IPStatusTypedef ipStatus;
    memset(&ipStatus, 0, sizeof(ipStatus));

    if (bk_wlan_get_ip_status(&ipStatus, BK_STATION) != 0 || ipStatus.ip[0] == '\0' ||
        strcmp(ipStatus.ip, "0.0.0.0") == 0)
    {
        return false;
    }

    LinkStatusTypeDef linkStatus;
    memset(&linkStatus, 0, sizeof(linkStatus));
    if (bk_wlan_get_link_status(&linkStatus) != 0 || linkStatus.conn_state == 0)
    {
        return false;
    }
    linkStatus.ssid[sizeof(linkStatus.ssid) - 1] = '\0';
    return IsProductionSsid(reinterpret_cast<const char *>(linkStatus.ssid));
}

void SetProductionRgbChannel(uint8_t channel)
{
    switch (channel)
    {
    case 0:
        BkLightingMgr().SetProductionRgbOutput(255, 0, 0);
        break;
    case 1:
        BkLightingMgr().SetProductionRgbOutput(0, 255, 0);
        break;
    default:
        BkLightingMgr().SetProductionRgbOutput(0, 0, 255);
        break;
    }
}

void ProductionRgbTaskMain(void * arg)
{
    (void) arg;
    ChipLogProgress(DeviceLayer, "Production RGB test: task started");
    ChipLogProgress(DeviceLayer, "Production RGB test: running from R");

    bool wifiDisconnected = false;
    while (true)
    {
        if (!IsStillOnProductionWifi())
        {
            wifiDisconnected = true;
            ChipLogProgress(DeviceLayer, "Production RGB test: production Wi-Fi disconnected, task exiting");
            break;
        }

        ql_rtos_mutex_lock(sProductionRgbMutex);
        bool exitRequested = sProductionRgbExitRequested;
        if (!exitRequested && sProductionRgbRunning)
        {
            SetProductionRgbChannel(sProductionRgbChannel);
            sProductionRgbChannel = static_cast<uint8_t>((sProductionRgbChannel + 1) % 3);
        }
        ql_rtos_mutex_unlock(sProductionRgbMutex);

        if (exitRequested)
        {
            break;
        }

        ql_rtos_event_group_wait_bits(sProductionRgbEvents, kProductionRgbWakeBit, true, false, kRgbStepIntervalMs);
    }

    ql_rtos_mutex_lock(sProductionRgbMutex);
    sProductionRgbRunning = false;
    sProductionRgbExitRequested = true;
    sProductionRgbTask = nullptr;
    ql_rtos_mutex_unlock(sProductionRgbMutex);

    BkLightingMgr().StopProductionRgbOutput();
    if (wifiDisconnected)
    {
        ChipLogProgress(DeviceLayer, "Production RGB test: stopped after production Wi-Fi disconnect");
    }
    ChipLogProgress(DeviceLayer, "Production RGB test: task stopped");
    ql_rtos_semaphore_release(sProductionRgbStopped);
    ql_rtos_task_delete(nullptr);
}

void CleanupProductionRgbResources()
{
    if (sProductionRgbEvents != nullptr)
    {
        ql_rtos_event_group_delete(sProductionRgbEvents);
        sProductionRgbEvents = nullptr;
    }
    if (sProductionRgbStopped != nullptr)
    {
        ql_rtos_semaphore_delete(sProductionRgbStopped);
        sProductionRgbStopped = nullptr;
    }
    if (sProductionRgbMutex != nullptr)
    {
        ql_rtos_mutex_delete(sProductionRgbMutex);
        sProductionRgbMutex = nullptr;
    }
}

bool StartProductionRgbTest()
{
    if (sProductionRgbTask != nullptr)
    {
        return true;
    }

    if (ql_rtos_mutex_create(&sProductionRgbMutex) != QL_OSI_SUCCESS)
    {
        ChipLogError(DeviceLayer, "Production RGB test: mutex creation failed");
        return false;
    }
    if (ql_rtos_event_group_create(&sProductionRgbEvents) != QL_OSI_SUCCESS ||
        ql_rtos_semaphore_create(&sProductionRgbStopped, 0) != QL_OSI_SUCCESS)
    {
        ChipLogError(DeviceLayer, "Production RGB test: event/semaphore creation failed");
        CleanupProductionRgbResources();
        return false;
    }
    if (!BkLightingMgr().StartProductionRgbOutput())
    {
        CleanupProductionRgbResources();
        return false;
    }

    sProductionRgbRunning = true;
    sProductionRgbExitRequested = false;
    sProductionRgbChannel = 0;
    ql_rtos_status err = ql_rtos_task_create(&sProductionRgbTask, 3 * 1024, QL_RTOS_DEFAULT_THREAD_PRIORITY,
                                             const_cast<char *>("prod_rgb"), ProductionRgbTaskMain, nullptr);
    if (err != QL_OSI_SUCCESS)
    {
        ChipLogError(DeviceLayer, "Production RGB test: task creation failed: %d", err);
        sProductionRgbTask = nullptr;
        BkLightingMgr().StopProductionRgbOutput();
        CleanupProductionRgbResources();
        return false;
    }
    return true;
}

void StopProductionRgbTest()
{
    if (sProductionRgbMutex == nullptr)
    {
        BkLightingMgr().StopProductionRgbOutput();
        return;
    }

    ql_rtos_mutex_lock(sProductionRgbMutex);
    sProductionRgbRunning = false;
    sProductionRgbExitRequested = true;
    ql_rtos_mutex_unlock(sProductionRgbMutex);

    BkLightingMgr().SetProductionRgbOutput(0, 0, 0);
    ql_rtos_event_group_set_bits(sProductionRgbEvents, kProductionRgbWakeBit, false);

    if (ql_rtos_semaphore_wait(sProductionRgbStopped, kRgbStopTimeoutMs) != QL_OSI_SUCCESS)
    {
        ChipLogError(DeviceLayer, "Production RGB test: task stop timeout");
        BkLightingMgr().StopProductionRgbOutput();
        return;
    }

    BkLightingMgr().StopProductionRgbOutput();
    CleanupProductionRgbResources();
}

bool PauseProductionRgbTest()
{
    if (sProductionRgbMutex == nullptr)
    {
        return false;
    }

    ql_rtos_mutex_lock(sProductionRgbMutex);
    if (sProductionRgbTask == nullptr || sProductionRgbExitRequested)
    {
        ql_rtos_mutex_unlock(sProductionRgbMutex);
        return false;
    }
    sProductionRgbRunning = false;
    sProductionRgbChannel = 0;
    BkLightingMgr().SetProductionRgbOutput(0, 0, 0);
    ql_rtos_mutex_unlock(sProductionRgbMutex);
    ql_rtos_event_group_set_bits(sProductionRgbEvents, kProductionRgbWakeBit, false);
    ChipLogProgress(DeviceLayer, "Production RGB test: paused and off");
    return true;
}

bool RunProductionRgbTestFromRed()
{
    if (sProductionRgbMutex == nullptr)
    {
        return false;
    }

    ql_rtos_mutex_lock(sProductionRgbMutex);
    if (sProductionRgbTask == nullptr || sProductionRgbExitRequested)
    {
        ql_rtos_mutex_unlock(sProductionRgbMutex);
        return false;
    }
    sProductionRgbChannel = 0;
    sProductionRgbRunning = true;
    ql_rtos_mutex_unlock(sProductionRgbMutex);
    ql_rtos_event_group_set_bits(sProductionRgbEvents, kProductionRgbWakeBit, false);
    ChipLogProgress(DeviceLayer, "Production RGB test: running from R");
    return true;
}

void FormatMac(char * out, size_t outSize)
{
    char mac[16];
    memset(mac, 0, sizeof(mac));

    bk_wifi_get_station_mac_address(mac);

    snprintf(out, outSize, "%02X:%02X:%02X:%02X:%02X:%02X",
             static_cast<unsigned char>(mac[0]), static_cast<unsigned char>(mac[1]), static_cast<unsigned char>(mac[2]),
             static_cast<unsigned char>(mac[3]), static_cast<unsigned char>(mac[4]), static_cast<unsigned char>(mac[5]));
}

bool BuildReportLine(char * out, size_t outSize)
{
    char qrCode[256];
    MutableCharSpan qrCodeSpan(qrCode);
    CHIP_ERROR qrErr = GetQRCode(qrCodeSpan, RendezvousInformationFlags(RendezvousInformationFlag::kBLE));
    if (qrErr != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Production QR TCP: GetQRCode failed: %" CHIP_ERROR_FORMAT, qrErr.Format());
        snprintf(out, outSize, "ERR|reason=qrcode_failed\n");
        return false;
    }

    char manualPairingCode[64];
    MutableCharSpan manualPairingCodeSpan(manualPairingCode);
    CHIP_ERROR mpErr = GetManualPairingCode(manualPairingCodeSpan, RendezvousInformationFlags(RendezvousInformationFlag::kBLE));
    if (mpErr != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Production QR TCP: GetManualPairingCode failed: %" CHIP_ERROR_FORMAT, mpErr.Format());
        snprintf(out, outSize, "ERR|reason=manual_pairing_code_failed\n");
        return false;
    }

    char mac[32];
    FormatMac(mac, sizeof(mac));

    const bool commissioned = Server::GetInstance().GetFabricTable().FabricCount() > 0;
    int written = snprintf(out, outSize, "QR|mac=%s|qr=%s|mp=%s|commissioned=%u\n", mac, qrCode, manualPairingCode,
                           commissioned ? 1 : 0);
    if (written < 0 || static_cast<size_t>(written) >= outSize)
    {
        chip::Platform::CopyString(out, outSize, "ERR|reason=report_too_long\n");
        return false;
    }

    return true;
}

bool SendAll(int fd, const char * data)
{
    size_t remaining = strlen(data);
    const char * ptr = data;

    while (remaining > 0)
    {
        int sent = send(fd, ptr, remaining, 0);
        if (sent <= 0)
        {
            ChipLogError(DeviceLayer, "Production QR TCP: send failed errno=%d", errno);
            return false;
        }
        ptr += sent;
        remaining -= sent;
    }

    return true;
}

void HandleCommandLine(int fd, char * line)
{
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\r')
    {
        line[--len] = '\0';
    }

    if (strcmp(line, "ON") == 0)
    {
        SendAll(fd, RunProductionRgbTestFromRed() ? "OK|cmd=ON\n" : "ERR|reason=rgb_test_unavailable\n");
    }
    else if (strcmp(line, "OFF") == 0)
    {
        SendAll(fd, PauseProductionRgbTest() ? "OK|cmd=OFF\n" : "ERR|reason=rgb_test_unavailable\n");
    }
    else if (line[0] != '\0')
    {
        SendAll(fd, "ERR|reason=unsupported_cmd\n");
    }
}

void ProcessCommandIfComplete(int fd, char * line, size_t & lineLen, bool force)
{
    if (lineLen == 0)
    {
        return;
    }

    if (!force && lineLen < sizeof("OFF") - 1)
    {
        return;
    }

    line[lineLen] = '\0';

    if (force || strcmp(line, "ON") == 0 || strcmp(line, "OFF") == 0)
    {
        HandleCommandLine(fd, line);
        lineLen = 0;
    }
}

void ReceiveCommandLoop(int fd)
{
    char recvBuf[96];
    char line[96];
    size_t lineLen = 0;

    while (IsStillOnProductionWifi())
    {
        int ret = recv(fd, recvBuf, sizeof(recvBuf), 0);
        if (ret == 0)
        {
            ChipLogProgress(DeviceLayer, "Production QR TCP: server closed connection");
            break;
        }
        if (ret < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                ProcessCommandIfComplete(fd, line, lineLen, true);
                continue;
            }
            ChipLogError(DeviceLayer, "Production QR TCP: recv failed errno=%d", errno);
            break;
        }

        for (int i = 0; i < ret; ++i)
        {
            if (recvBuf[i] == '\n')
            {
                line[lineLen] = '\0';
                HandleCommandLine(fd, line);
                lineLen = 0;
            }
            else if (lineLen < sizeof(line) - 1)
            {
                line[lineLen++] = recvBuf[i];
            }
        }
        ProcessCommandIfComplete(fd, line, lineLen, false);
    }
}

int ConnectTcpServer()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        ChipLogError(DeviceLayer, "Production QR TCP: socket failed errno=%d", errno);
        return -1;
    }

    timeval timeout;
    timeout.tv_sec  = kSocketRecvTimeoutSec;
    timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    timeval sendTimeout;
    sendTimeout.tv_sec  = kSocketSendTimeoutSec;
    sendTimeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof(sendTimeout));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(kServerPort);
    addr.sin_addr.s_addr = inet_addr(kServerIp);

    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
    {
        ChipLogError(DeviceLayer, "Production QR TCP: connect %s:%u failed errno=%d", kServerIp, kServerPort, errno);
        close(fd);
        return -1;
    }

    ChipLogProgress(DeviceLayer, "Production QR TCP: connected to server %s:%u", kServerIp, kServerPort);
    return fd;
}

void TcpClientLoop()
{
    while (IsStillOnProductionWifi())
    {
        int fd = ConnectTcpServer();
        if (fd >= 0)
        {
            char report[512];
            bool reportOk = BuildReportLine(report, sizeof(report));
            ChipLogProgress(DeviceLayer, "Production QR TCP: report line: %s", report);

            bool allReportsSent = true;
            for (uint32_t i = 0; i < kReportSendCount; ++i)
            {
                if (!SendAll(fd, report))
                {
                    ChipLogError(DeviceLayer, "Production QR TCP: report send failed, index=%lu",
                                 static_cast<unsigned long>(i + 1));
                    allReportsSent = false;
                    break;
                }

                ChipLogProgress(DeviceLayer, "Production QR TCP: report sent %lu/%lu%s",
                                static_cast<unsigned long>(i + 1), static_cast<unsigned long>(kReportSendCount),
                                reportOk ? "" : " with error payload");

                if (i + 1 < kReportSendCount)
                {
                    ql_rtos_task_sleep_ms(kReportSendIntervalMs);
                }
            }

            if (allReportsSent)
            {
                ReceiveCommandLoop(fd);
            }
            close(fd);
        }

        if (IsStillOnProductionWifi())
        {
            ql_rtos_task_sleep_ms(kTcpReconnectDelayMs);
        }
    }
}

void ProductionTaskMain(void * arg)
{
    ChipLogProgress(DeviceLayer, "Production QR TCP: task started");

    if (Server::GetInstance().GetFabricTable().FabricCount() > 0)
    {
        ChipLogProgress(DeviceLayer, "Production QR TCP: fabric exists, skip production flow");
        sProductionTask = nullptr;
        ql_rtos_task_delete(nullptr);
        return;
    }

    if (!ScanProductionWifiOnce())
    {
        ChipLogProgress(DeviceLayer, "Production QR TCP: production Wi-Fi not found, skip");
        sProductionTask = nullptr;
        ql_rtos_task_delete(nullptr);
        return;
    }

    if (!ConnectProductionWifiOnce())
    {
        ChipLogProgress(DeviceLayer, "Production QR TCP: production Wi-Fi connect failed, skip");
        sProductionTask = nullptr;
        ql_rtos_task_delete(nullptr);
        return;
    }

    if (!StartProductionRgbTest())
    {
        ChipLogError(DeviceLayer, "Production RGB test: failed to start; QR TCP reporting will continue");
    }

    TcpClientLoop();
    StopProductionRgbTest();

    ChipLogProgress(DeviceLayer, "Production QR TCP: task exit");
    sProductionTask = nullptr;
    ql_rtos_task_delete(nullptr);
}

} // namespace

void ProductionQrTcpStart(void)
{
    if (sStarted)
    {
        return;
    }

    sStarted = true;
    ql_rtos_status err =
        ql_rtos_task_create(&sProductionTask, 6 * 1024, QL_RTOS_DEFAULT_THREAD_PRIORITY, const_cast<char *>("prod_qr_tcp"),
                            ProductionTaskMain, nullptr);
    if (err != QL_OSI_SUCCESS)
    {
        ChipLogError(DeviceLayer, "Production QR TCP: create task failed: %d", err);
        sProductionTask = nullptr;
        sStarted        = false;
    }
}
