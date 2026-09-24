# 生产打码 TCP 功能需求文档

## 背景

Beken Matter 灯设备需要一个仅用于生产环节的“打码”流程：设备在 Matter 配网之前，把自身的 Matter 配网二维码上报给上位机；生产人员通过上位机控制设备亮灭，确认当前二维码对应哪一台实物设备，然后打印二维码并粘贴到设备机身。

这个功能只在生产阶段使用。设备出厂后，正常用户使用过程中不需要这个 TCP 打码能力。代码可以长期保留在固件中，但功能必须只在明确的生产条件下启动，避免影响正常 Matter 功能或泄露配网二维码。

## 目标

- 未加入 Matter fabric 的设备上电后，只扫描一次固定产测 Wi-Fi。
- 如果扫描到产测 Wi-Fi，设备使用固件中的产测 Wi-Fi 密码自动连接。
- 如果扫描不到产测 Wi-Fi，设备不进入产测打码流程，继续正常 Matter APP 流程。
- 设备在产测 Wi-Fi 连接成功后启动生产打码 TCP client 线程。
- 设备连接指定的上位机 TCP server 地址和端口。
- 设备向上位机发送 Matter 配网二维码和稳定的设备标识。
- 上位机显示收到的设备和二维码。
- 上位机发送简单识别/控制指令，第一版只支持开、关。
- 设备执行识别/控制指令时，不破坏现有 Matter 灯控状态处理逻辑。
- 生产人员确认设备后，通过上位机打印二维码并粘贴到对应设备。

## 非目标

- 该功能不是 Matter 配网流程的替代方案。
- 该功能不是面向终端用户的远程控制通道。
- 该功能不应在用户家庭 Wi-Fi 或普通使用环境下运行。
- 如果设备端已配置上位机 IP 和端口，则不需要 UDP 发现机制。
- 该功能不要求设备已经加入 Matter fabric。

## 启动条件

当前选定方案把“上电是否扫描到产测 Wi-Fi”作为是否进入产测流程的入口判断，同时要求设备未加入 Matter fabric。

推荐规则：

```text
如果 设备未加入 Matter fabric:
    上电后扫描一次产测 Wi-Fi SSID
    如果扫描到产测 Wi-Fi:
        使用固件中的产测 Wi-Fi 密码自动连接
        如果连接成功:
            启动生产打码 TCP client
        如果连接失败:
            退出产测流程，继续正常 Matter APP 流程
    如果没有扫描到产测 Wi-Fi:
        继续正常 Matter APP 流程
否则:
    不扫描产测 Wi-Fi，不启动生产打码 TCP client
```

也就是说，产测 Wi-Fi 的存在本身作为功能开关。设备离开生产环境后，只要上电时扫描不到产测 SSID，生产打码 TCP 线程就不应该运行。

建议增加双保险：

- 只有 server IP 和端口有效时才允许启动。
- Wi-Fi 断开或切换到非产测 SSID 后，停止或挂起 TCP 线程。
- 设备已经完成 Matter 配网后，不自动进入产测打码流程；返修设备应先重置，清除旧 fabric 和旧 Wi-Fi 后再进入产测流程。

## 退货返修/重打码场景

客户退货回来的设备可能已经被用户操作过，也可能仍保留客户的 Matter fabric 和客户 Wi-Fi。为了降低产测逻辑和客户网络状态的耦合，返修设备需要先重置，清除旧 Matter fabric 和旧 Wi-Fi 后，再重新上电进入产测流程。

推荐返修流程：

```text
退货设备上电
  |
  v
进入重置/返修模式
  |
  v
清除旧 Matter fabric 和旧 Wi-Fi
  |
  v
重新上电
  |
  v
设备未加入 Matter fabric，执行一次产测 Wi-Fi 扫描
  |
  +-- 扫描到产测 Wi-Fi并连接成功
          |
          v
      启动生产打码 TCP client
  |
  v
上报二维码，上位机重新打印
```

因此，返修场景不建议在设备仍保留客户 fabric 的情况下直接抢连产测 Wi-Fi。先重置再进入产测流程，可以避免产测逻辑和客户网络、Matter 正常运行状态互相影响。

成立条件：

- 固件中保留了生产打码 TCP 功能。
- 设备未加入 Matter fabric。
- 上电扫描到产测 SSID，并且自动连接成功。
- TCP server IP 和端口仍然有效。
- Matter 二维码生成所需的工厂数据仍然存在，例如 passcode、discriminator、vendor id、product id、序列号等。

不成立的情况：

- 如果返修时执行的擦除动作把 Matter 工厂数据也擦掉了，设备就无法生成原始二维码。
- 如果只是清除 Matter fabric、网络凭据、用户配网信息，通常不影响二维码生成。
- 如果上电扫描不到产测 Wi-Fi，TCP 打码线程不应启动。

## 当前代码擦除行为评估

当前 lighting app 里的手机配网/重新配网逻辑主要清除两类信息：

- Matter fabric 信息。
- Wi-Fi station 凭据。

相关代码：

- `main/LightingRemoteBridge.cpp`
  - 进入手机配网模式时，如果已经配网过，会调用 `server.GetFabricTable().DeleteAllFabrics()` 清除旧 Matter fabric。
  - 随后调用 `DeviceLayer::ConnectivityMgr().ClearWiFiStationProvision()` 清除旧 Wi-Fi station 配置。
- `src/platform/Beken/ConnectivityManagerImpl.cpp`
  - `_ClearWiFiStationProvision()` 调用 `BekenWiFiDriver::GetInstance().ClearNetworkConfig()`。
- `src/platform/Beken/NetworkCommissioningWiFiDriver.cpp`
  - `ClearNetworkConfig()` 只删除 KVS 中的 `wifi-ssid` 和 `wifi-pass`，并清空内存中的 saved/staging network。
- `src/platform/Beken/FactoryDataProvider.cpp`
  - 二维码相关工厂数据从 `BK_PARTITION_MATTER_FACTORY_FLASH` 读取。
- `src/platform/Beken/ConfigurationManagerImpl.cpp`
  - factory reset 擦除的是 `BK_PARTITION_MATTER_FLASH`，不是 `BK_PARTITION_MATTER_FACTORY_FLASH`。

基于当前代码，清除 Matter fabric 和 Wi-Fi station 凭据不会擦除二维码生成所需的 Matter 工厂数据。因此，返修设备重置后重新上电，只要扫描到产测 Wi-Fi 并自动连接成功，仍然可以进入 TCP 打码流程并重新上报二维码。

需要避免的操作：

- 不要在返修流程中擦除 `BK_PARTITION_MATTER_FACTORY_FLASH`。
- 不要把产测 TCP server 配置只放在会被 factory reset 清掉的普通 Matter 存储区，除非每次返修都会重新写入。
- 如果将来修改 factory reset 逻辑，需要继续确认它不会擦除 Matter factory data 分区。

需要注意：

- 如果设备仍保留客户的 Matter fabric，普通产测上位机通常没有权限直接修改它的 Wi-Fi 网络。
- 返修时应先让设备进入工厂/返修模式，清除旧 fabric 和旧 Wi-Fi 凭据，再重新进入产测流程。
- 如果设备已经存在 Matter fabric，即使扫描到产测 Wi-Fi，也不自动连接产测 Wi-Fi，不启动 TCP 打码流程。
- 这样可以避免产测逻辑和客户网络、Matter 正常运行状态互相抢占。

## 选定方案：上电扫描产测 Wi-Fi 自动进入打码

选定方案为：把产测 Wi-Fi SSID、密码、TCP server IP 和端口写在代码或固定配置中。设备每次上电后只扫描一次周围 Wi-Fi，如果扫描到目标产测 Wi-Fi，则自动连接该 Wi-Fi；连接成功后启动 TCP 打码流程。如果没有扫描到目标产测 Wi-Fi，则不启动该功能，不继续扫描或重试，直接继续正常 Matter APP 流程。

流程：

```text
设备上电
  |
  v
扫描周围 Wi-Fi，优先扫描配置的产测 SSID
  |
  +-- 没扫描到产测 SSID
  |       |
  |       v
  |   不连接产测 Wi-Fi，不启动 TCP 打码，不再重试扫描
  |       |
  |       v
  |   继续正常 Matter APP 流程
  |
  +-- 扫描到产测 SSID
          |
          v
      使用固件/固定配置中的产测 Wi-Fi 密码连接
          |
          v
      连接成功后启动 TCP 打码 client
          |
          v
      上报二维码和设备标识
```

核心行为约束：

- 只有设备未加入 Matter fabric 时，才执行上电产测 Wi-Fi 扫描。
- 上电只做一次产测 Wi-Fi 扫描。
- 扫描不到产测 SSID 时，不进入生产打码流程。
- 扫描不到产测 SSID 时，不阻塞 Matter APP，不持续后台扫描。
- 扫描到产测 SSID 后，设备优先进入产测打码流程。
- 产测 Wi-Fi 连接成功后，才启动 TCP client 并上报二维码。
- 产测 Wi-Fi 连接只尝试一次；连接失败时退出产测打码流程，继续正常 Matter APP 流程。

当前工程具备实现基础：

- `src/platform/Beken/NetworkCommissioningWiFiDriver.cpp` 已有 Wi-Fi 扫描能力。
- `StartScanWiFiNetworks()` 支持 directed scanning，即按指定 SSID 扫描。
- `ConnectWiFiNetwork()` 可以使用指定 SSID 和密码发起 station 连接。

该方案与清除 Matter fabric、清除已保存 Wi-Fi 凭据没有直接冲突。因为产测 Wi-Fi 凭据来自固件/固定配置，不依赖 `wifi-ssid` 和 `wifi-pass` 这组可被清除的 Matter 网络凭据。

需要注意：

- 扫描和连接必须在独立任务或异步流程中执行，不能阻塞 Matter 平台线程。
- 只扫描一次，不要长时间频繁扫描，避免影响 Matter BLE/SoftAP 配网体验。
- 如果扫描到产测 Wi-Fi 后自动连接，会占用设备 Wi-Fi station 状态，可能改变原本“等待用户配网”的启动行为。
- 产测 Wi-Fi SSID/密码写入固件后，所有设备都长期带着这组凭据，需要评估泄露风险。
- 如果客户环境附近恰好存在同名 SSID，设备可能误进入打码流程。

## 方案取舍记录

当前选择上电扫描产测 Wi-Fi，扫描到后自动连接并进入 TCP 打码。

选择原因：

- 生产线更自动化，上电后只要周围有产测 Wi-Fi，设备可以自动进入打码流程。
- 不依赖之前保存的 `wifi-ssid`/`wifi-pass`，清 Wi-Fi 后也能再次自动连接产测 Wi-Fi。
- 对返修设备更方便：只要设备已重置、固件还保留产测 Wi-Fi 固定配置，并且现场有产测 Wi-Fi，就可以自动进入打码流程。

需要接受的代价：

- 固件长期携带产测 Wi-Fi 密码，安全边界更宽。
- 上电时会额外扫描 Wi-Fi，可能拉长启动时间或影响配网体验。
- 如果扫描到同名 SSID，可能误连并触发二维码上报。
- 自动连接产测 Wi-Fi 可能与 Matter 正常配网流程竞争，因此限定为未加入 Matter fabric 的设备，并且上电只扫描一次。
- 如果客户环境也能模拟产测 SSID 和 TCP server，存在二维码被采集的风险。

最终约束：

- 该自动扫描/连接只对未加入 Matter fabric 的设备生效。
- 为避免影响正常用户场景，扫描只执行一次。
- 如果扫描不到产测 Wi-Fi，立即回到正常 Matter APP 流程。
- 不使用编译宏控制该功能；所有固件都保留产测流程，等价于该功能始终编译启用。

## TCP 数据格式

第一版使用纯文本行协议。每条消息以换行符 `\n` 结束，设备侧实现和串口/抓包调试都更简单，上位机按行读取即可。

设备连接成功后上报一行设备信息：

```text
QR|mac=AA:BB:CC:DD:EE:FF|qr=MT:XXXXXXXXXXXX|mp=03785760671|commissioned=0
```

字段说明：

- `QR`：二维码上报消息类型。
- `mac`：设备 MAC 地址。
- `qr`：Matter 二维码字符串。
- `mp`：Matter 手动配网码。
- `commissioned`：是否已有 Matter fabric，`0` 表示未配网，`1` 表示已配网。

上位机下发控制指令：

```text
ON
OFF
```

设备回复：

```text
OK|cmd=ON
OK|cmd=OFF
ERR|reason=unsupported_cmd
```

第一版只支持 `ON` 和 `OFF`，暂不支持 `toggle` 和 `blink`。后续如果需要扩展更多字段，可以继续沿用 `key=value` 的文本行格式。

## 工程接入点

当前相关文件：

- `main/chipinterface.cpp`
  - 初始化 Matter server、灯控管理器、遥控桥、OTA，并打印 onboarding code。
  - 当前已在 app 初始化完成后调用非阻塞的 `ProductionQrTcpStart()`。
- `main/ProductionQrTcp.cpp`
  - 产测打码功能实现文件。
  - 固定产测参数：
    - SSID：`sparkleiot_test`
    - 密码：`88888888`
    - TCP server：`192.168.4.1:8899`
  - 启动后先判断 Matter fabric 数量，已有 fabric 时直接退出。
  - 未加入 fabric 时，只执行一次 directed scan。
  - 扫描到产测 SSID 后只发起一次 Wi-Fi station 连接。
  - 连接成功并拿到 IP 后，启动 TCP client 长连接流程。
  - TCP 连接断开或上位机未启动时，保持低频重连；Wi-Fi 不再是产测 SSID 后退出线程。
  - TCP 收到 `ON`、`OFF` 后，通过 Matter 平台线程调度 `BkLightingMgr().SetOnOff()`，不直接操作 GPIO/PWM。
- `main/include/ProductionQrTcp.h`
  - 对外只暴露 `ProductionQrTcpStart()`。
- `main/BkLightingManager.cpp`
  - 管理 RGB、灯光状态和硬件输出。
  - TCP 指令必须复用现有灯控接口，不要直接操作 GPIO/PWM。
- `main/DeviceCallbacks.cpp`
  - Matter OnOff 属性变化已经会调用 `BkLightingMgr().SetOnOff()`。
- `args.gni`
  - 当前 `chip_inet_config_enable_tcp_endpoint = false`，所以生产打码 TCP client 建议使用平台/lwIP socket API，不使用 CHIP Inet TCP endpoint。
- `BUILD.gn`
  - 当前已把 `main/ProductionQrTcp.cpp` 加入 `MatterApp` 源文件。

## 影响评估

如果该功能以“条件启动的独立任务”方式实现，预计对现有功能影响较低。

关键约束：

- 不要在 Matter 平台线程中执行阻塞式 `connect`、`recv` 或重连循环。
- 只有未加入 Matter fabric、扫描到产测 SSID 并连接成功时，才启动 TCP client。
- TCP 指令处理不要直接操作 GPIO/PWM。
- 使用 `BkLightingMgr()` 或现有本地控制封装，保证灯光状态处理一致。
- TCP server 不可用时，连接重试需要限频，避免消耗过多 lwIP socket、内存或 CPU。
- Wi-Fi 断开或切换 SSID 后，停止或挂起 TCP client。

潜在风险：

- 如果该线程在用户 Wi-Fi 下运行，可能泄露 Matter 配网二维码，或持续尝试连接产测服务器。
- 如果 TCP 线程阻塞共享资源，可能影响 Matter 配网、mDNS、OTA 或正常控制。
- 如果 TCP 控制绕过 Matter/灯控管理器，可能导致物理灯状态和 Matter 属性状态不一致。
- 如果产测 Wi-Fi 凭据在工厂外仍可用，存在未授权采集二维码的风险。

## 待确认问题

当前待确认问题已关闭，可以开始进入代码实现设计。

## 第一版建议

- 不使用编译宏控制该功能；所有固件都包含产测打码流程。
- 设备已有 Matter fabric 时，不执行产测 Wi-Fi 自动扫描，不启动 TCP 打码流程。
- 返修设备需要先重置，清除旧 fabric 和旧 Wi-Fi 后，再重新上电进入产测流程。
- 产测 Wi-Fi SSID：`sparkleiot_test`。
- 产测 Wi-Fi 密码：`88888888`。
- TCP server IP：`192.168.4.1`。
- TCP server 端口：`8899`。
- 以上产测配置直接使用编译期常量写在代码里。
- 上电后只扫描一次 `sparkleiot_test`。
- 扫描不到产测 SSID 时，不启动 TCP 打码，继续正常 Matter APP 流程。
- 扫描到产测 SSID 后，使用密码 `88888888` 自动连接。
- 产测 Wi-Fi 连接只尝试一次；如果连接失败，退出产测打码流程，生产人员可重新上电后再试。
- 产测 Wi-Fi 连接成功后，启动 TCP client。
- 使用 `192.168.4.1:8899` 连接上位机。
- 上报二维码、MAC、可选序列号、Matter 是否已配网状态。
- 保持一个 TCP 长连接。
- 上位机不可用时，TCP 连接可以低频重连，但不能阻塞正常 Matter 任务。
- TCP 协议第一版使用纯文本行协议。
- 第一版只实现 `ON`、`OFF` 两类识别指令。

## 当前实现状态

当前已按第一版需求完成代码接入：

- 已新增 `main/ProductionQrTcp.cpp` 和 `main/include/ProductionQrTcp.h`。
- 已在 `main/chipinterface.cpp` 的 `InitServer()` 末尾启动产测线程。
- 已在 `BUILD.gn` 中加入新源文件。
- 已使用独立 RTOS task 执行扫描、连接和 TCP socket 收发，避免阻塞 Matter 平台线程。
- 已实现无 fabric 才进入产测流程。
- 已实现上电只扫描一次产测 SSID。
- 已实现产测 Wi-Fi 只连接一次，失败后退出产测流程。
- 已实现 TCP 上报 MAC、二维码、手动配网码和 commissioned 状态。
- 已实现 `ON`、`OFF` 两个上位机控制指令。
- 已实现 TCP 控制通过 `BkLightingMgr().SetOnOff()` 进入现有灯控逻辑。
- 产测 Wi-Fi 连接等待时间当前为 30 秒；如果 30 秒内仍未拿到有效 IPv4，则停止 station 连接并退出本次产测流程，生产人员可重新上电后再试。
- 当前产测 Wi-Fi 扫描、连接和 IP 获取已按实测情况调整为 Beken 原生 Wi-Fi API：
  - 扫描：`bk_wlan_scan_ap_reg_cb()`、`bk_wlan_start_assign_scan()`、`wlan_sta_scan_result()`。
  - 连接：`bk_wlan_start()`。
  - IP 判断：以 `bk_wlan_get_ip_status()` 读到有效 IPv4 地址作为连接成功条件。
- 扫描完成等待已从 RTOS 信号量改为回调置标志位、产测线程轮询标志位，避免 Wi-Fi 回调上下文中信号量未唤醒导致误判超时。
- MAC 获取已使用 station MAC：`bk_wifi_get_station_mac_address()`。

已执行的工程验证：

```text
ninja -C out/matter MatterApp
ninja -C out/matter
```

验证结果：`MatterApp` 静态库和默认目标均编译通过。

上位机对接注意：

- 上位机监听 `192.168.4.1:8899`。
- 设备连接后会主动发送 `QR|mac=...|qr=...|mp=...|commissioned=...` 文本，行尾为 `\n`。
- 为降低上位机应用层读取/显示时序导致的偶发漏显示，设备在同一个 TCP 连接内连续发送 3 次相同的 `QR|...` 上报行，每次间隔约 200ms。
- 上位机可以按 `mac`、`qr` 或整行内容去重。
- 上位机按行发送 `ON\n` 或 `OFF\n`。
- 设备按行回复 `OK|cmd=ON`、`OK|cmd=OFF` 或 `ERR|reason=unsupported_cmd`。
- 第一版不支持 `toggle`、`blink`。

注意：如果 8266 bridge 同时维护多个 TCP client，裸 `ON/OFF` 广播会同时控制多台设备，不适合最终产测识别流程。后续应改为按 TCP client id 或 MAC 定向控制。

## 当前实测结果

截至 2026-07-06，当前版本已完成一次完整产测链路验证。

实测环境：

- 产测 Wi-Fi：`sparkleiot_test`。
- 产测 Wi-Fi 密码：`88888888`。
- 设备获取到的 IP：`192.168.4.3`。
- 上位机 TCP server：`192.168.4.1:8899`。

设备侧关键日志：

```text
Production QR TCP: task started
Production QR TCP: scan result ssid=sparkleiot_test rssi=234 channel=1
Production QR TCP: production SSID found
WLAN_EVENT_CONNECTED
IP up: 192.168.4.3
Production QR TCP: connected, ssid=sparkleiot_test ip=192.168.4.3
Production QR TCP: connected to server 192.168.4.1:8899
```

上位机关键日志：

```text
0,CONNECT
QR|mac=C8:47:8C:42:88:48|qr=MT:S6SA2NC-03ISDA5V320|mp=03785760671|commissioned=0
```

说明：序列号仍可通过本地 `matter_show` 查看，但第一版 TCP 上报字段不再包含 `serial`。

控制指令实测：

- 上位机发送 `ON` 后，设备串口出现 `SetOnOff 0 -> 1`，灯打开。
- 上位机发送 `OFF` 后，设备串口出现 `SetOnOff 1 -> 0`，灯关闭。

因此当前第一版主流程已跑通：

```text
设备上电
  -> 扫描到产测 Wi-Fi
  -> 自动连接产测 Wi-Fi
  -> 获取 IP
  -> TCP 连接上位机
  -> 上报 Matter 二维码
  -> 接收上位机 ON/OFF 控制
  -> 控制灯开关用于识别设备
```

调试过程中已修复的问题：

- 初版使用 Quectel `ql_wlan_*` 扫描包装层，底层能扫到 AP，但产测线程收不到预期回调，导致误判扫描超时。已改为 Beken 原生扫描接口。
- Beken 原生扫描回调已经执行后，RTOS 信号量没有稳定唤醒产测线程，导致已经扫到 `sparkleiot_test` 但仍误判超时。已改为回调置标志位、线程轮询。
- 设备实际已连接并打印 `IP up: 192.168.4.3`，但旧逻辑依赖 `bk_wlan_get_link_status()` 同时满足 SSID 和连接状态，导致误判 Wi-Fi 连接超时。已改为以 `bk_wlan_get_ip_status()` 获取有效 IPv4 为准。

仍建议补充验证的场景：

- 关闭产测 Wi-Fi 后上电，确认设备只扫描一次，随后不启动 TCP 打码流程。
- 设备已有 Matter fabric 时上电，即使现场存在 `sparkleiot_test`，也不进入产测打码流程。
- 上位机 TCP server 未启动时，确认设备低频重连，不影响 Matter 主流程。

## 2026-07-07 同步：多设备测试和 8266 后续工作

最新设备侧固定配置：

```text
产测 Wi-Fi SSID: sparkleiot_test
产测 Wi-Fi 密码: 88888888
TCP server IP: 192.168.4.1
TCP server 端口: 8899
Wi-Fi 连接等待: 30s
QR 上报次数: 3 次
QR 上报间隔: 约 200ms
```

设备侧当前策略：

- 上电后只扫描一次产测 Wi-Fi。
- 扫描不到 `sparkleiot_test` 时，不进入产测打码流程。
- 扫描到后发起一次产测 Wi-Fi 连接。
- 30 秒内拿到有效 IPv4 才继续 TCP 打码。
- 30 秒内未拿到 IP 时，调用 `bk_wlan_stop(BK_STATION)` 停止本次 station 连接，退出产测流程。
- TCP 连接成功后发送 3 次相同的 `QR|...` 上报行。

多设备实测结果：

- 使用 8266 SoftAP + TCP server bridge，三台不同 MAC 设备连续上电接入。
- 三台设备均能接入 AP、建立 TCP client、上报二维码。
- 典型上位机日志：

```text
0,CONNECT
[TCP_ACCEPT] id=0
[TCP_RX] id=0 len=81
QR|mac=C8:47:8C:42:88:48|qr=MT:S6SA2NC-03ISDA5V320|mp=03785760671|commissioned=0

1,CONNECT
[TCP_ACCEPT] id=1
[TCP_RX] id=1 len=81
QR|mac=BC:A7:04:D2:DB:22|qr=MT:U9BK2F0S02X0DE4R900|mp=10144307390|commissioned=0

2,CONNECT
[TCP_ACCEPT] id=2
[TCP_RX] id=2 len=81
QR|mac=BC:A7:04:D2:DB:26|qr=MT:U9BK2NC-03NT4.4S600|mp=35303605136|commissioned=0
```

多设备测试结论：

- 多台不同设备、不同 MAC 的实际产测场景比同一台设备反复断电重启更稳定。
- 此前反复 `DISCONNECTED/CONNECTED` 的日志主要出现在同一台设备短时间反复重启时。
- 设备侧日志出现过 `ASSOC_REJECT`、`status=17`、`blacklist add ...`，判断为 AP 或协议栈短时间未完全释放同一 STA MAC 的旧关联状态。
- 实际产测中每台设备 MAC 不同，并且通常一台设备只上电一次，预期会明显好于反复重启同一台设备的调试场景。
- 仍不建议多台设备同时大批量抢连同一个 8266 AP；产测 SOP 应尽量控制为一台或少量设备顺序打码。

8266 bridge 当前测试行为：

- 8266 作为 SoftAP，SSID 为 `sparkleiot_test`，IP 为 `192.168.4.1`。
- 8266 监听 TCP `8899`。
- 设备 TCP client 接入后，8266 输出 `id,CONNECT` 和 `[TCP_ACCEPT] id=...`。
- 8266 收到设备 TCP 数据后，输出 `[TCP_RX] id=... len=...`，随后把 `QR|...` 原样转发到串口。
- 8266 串口收到裸 `ON` 或 `OFF` 时，当前测试代码会广播给所有在线 TCP client，并输出 `[TCP_TX] broadcast clients=N len=...`。
- 设备收到 `ON/OFF` 后回复 `OK|cmd=ON` 或 `OK|cmd=OFF`，8266 再作为 TCP RX 转发到串口。

8266 bridge 当前问题：

- 裸 `ON/OFF` 广播会同时控制所有在线设备，无法用于“区分当前二维码对应哪台设备”。
- 当多台设备同时在线时，上位机必须能把 `QR|...` 与 TCP client id 绑定，否则无法定向控制某一台设备。
- 当前调试日志 `[TCP_RX]`、`[UART_RX]`、`[TCP_TX]` 对排查有帮助，导入产测初期建议保留；后续量产稳定后可减少日志。

推荐的后续协议调整：

```text
8266 -> 上位机:
id,CONNECT
id,QR|mac=AA:BB:CC:DD:EE:FF|qr=MT:...|mp=...|commissioned=0
id,OK|cmd=ON
id,OK|cmd=OFF

上位机 -> 8266:
id,ON
id,OFF
```

示例：

```text
1,QR|mac=BC:A7:04:D2:DB:22|qr=MT:U9BK2F0S02X0DE4R900|mp=10144307390|commissioned=0
1,ON
1,OFF
1,OK|cmd=OFF
```

后续 8266 修改建议：

- TCP 收到设备数据后，转发给串口时统一加上 client id 前缀，例如 `1,QR|...`。
- 设备回复 `OK|cmd=...` 时也加上 client id，例如 `1,OK|cmd=ON`。
- 串口命令支持 `id,ON` 和 `id,OFF`，只转发给指定 TCP client。
- 裸 `ON/OFF` 不再广播；可以改为报错，或仅控制最近上报 QR 的 client。为了避免误控，建议最终版必须指定 id。
- 上位机记录 `client id -> mac -> qr -> mp -> commissioned` 的映射，生产人员点击某条二维码记录时，只控制该记录对应的 client id。
- 如果 client 断开，8266 输出 `id,CLOSE`，上位机应把该 client 标记为离线。

建议新线程继续处理的工作：

- 完成 8266 bridge 的定向控制协议修改。
- 明确 8266 Arduino 草稿文件是否纳入仓库；如纳入，建议保存为 `connectedhomeip/examples/lighting-app/beken/tools/tcpserver.ino` 或单独的 `production-tools/esp8266-tcp-bridge/tcpserver.ino`，避免放在 Matter app `main` 源码目录中造成误解。
- 更新上位机同事对接说明：二维码行、控制命令、ACK 行均带 client id。
- 多设备验证：至少 3 台不同 MAC，顺序上电，确认每台都能独立 QR 上报和定向 ON/OFF。
- 异常验证：指定离线 id 控制、client 断开后重连、8266 重启后设备重新上电。
