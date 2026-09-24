#include "ql_ble.h"
#include "ble.h"
#include "rtos_pub.h"
#include <string.h>
#include "LightingRemoteBridge.h"

#ifndef REMOTE_BLE_DEBUG    // 1: 打开 BLE 调试日志，0: 关闭
#define REMOTE_BLE_DEBUG 0
#endif

#ifndef REMOTE_ACTION_LOG // 1: 打开遥控动作日志，0: 关闭
#define REMOTE_ACTION_LOG 1
#endif

#if REMOTE_BLE_DEBUG
#define REMOTE_DBG(...) bk_printf(__VA_ARGS__)
#else
#define REMOTE_DBG(...) do {} while (0)
#endif

#if REMOTE_ACTION_LOG
#define REMOTE_LOG(...) os_printf(__VA_ARGS__)
#else
#define REMOTE_LOG(...) do {} while (0)
#endif

// 由 BkLightingManager.cpp 提供的 C 接口，用于控制灯光
extern void remote_light_on(void);
extern void remote_light_off(void);
extern void remote_light_level_up(void);
extern void remote_light_level_down(void);
extern void remote_light_toggle(void);
extern bool remote_light_is_on(void);
extern void remote_light_red(void);
extern void remote_light_green(void);
extern void remote_light_blue(void);
extern void remote_light_yellow(void);
extern void remote_light_lightblue(void);
extern void remote_light_purple(void);
extern void remote_light_white(void);
extern void remote_light_music_gentle(void);
extern void remote_light_music_aggressive(void);
extern void remote_light_music_stop(void);
extern void remote_light_music_set_sensitivity(uint8_t level);
extern void remote_light_scene_mode_next(void);
extern void remote_light_scene_mode_prev(void);
extern void remote_light_scene_auto_toggle(void);
extern void remote_light_cold_white(void);

/* 情景模式接口，由 BkLightingManager.cpp 提供 */
extern void remote_light_scene1(void);
extern void remote_light_scene2(void);
extern void remote_light_scene3(void);

/* 定时关机接口，由 BkLightingManager.cpp 提供 */
extern void remote_light_shutdown_timer_start(uint32_t ms);
extern void remote_light_shutdown_timer_cancel(void);


/***************ble test******************/

int ble_central_state = 0;
static ql_task_t ble_central_hdl = NULL;
uint8_t scan_actv_idx = -1;
uint8_t conn_actv_idx = -1;

struct scan_param g_scan_info = {0};

uint8_t conn_addr[6] = {0};
uint8_t adv_addr_type = 0; //0:public ,1:random
uint8_t scan_success_flag = 0;
uint8_t data_handle = 0;

// 扫描启停开关：
// - 1 表示允许扫描；
// - 0 表示禁止扫描（用于上电配网期间关闭）。
volatile uint8_t s_scan_enable = 0;
static uint8_t s_scanning = 0;
static volatile uint8_t s_central_suspend = 0;
static bool s_ble_entry_done = false;
static uint8_t s_music_sensitivity = 4; // 音乐律动初始灵敏度，范围 1~5，数值越大越灵敏


static void ble_central_cmd_cb(ble_cmd_t cmd, ble_cmd_param_t *param);
void ql_ble_remote_force_stop_scan(void);
void ql_ble_remote_notice_cb(ble_notice_t notice, void *param);
void ql_ble_remote_reset_ble_state(void);
void ql_ble_remote_install_notice_cb(void);

// 供其他模块控制扫描开关（例如配网/遥控切换）
void ql_ble_remote_set_scan_enable(int enable)
{
    s_scan_enable = (enable ? 1 : 0);
}

// 暂停/恢复 BLE central 线程，用于与 CHIPoBLE 配网切换
void ql_ble_remote_set_suspend(int enable)
{
    s_central_suspend = (enable ? 1 : 0);
    if (s_central_suspend)
    {
        ql_ble_remote_force_stop_scan();
        if (ble_central_hdl != NULL)
        {
            ql_rtos_task_delete(ble_central_hdl);
            ble_central_hdl = NULL;
        }
    }
    else
    {
        ble_central_state = 0;
        s_scanning = 0;
        scan_actv_idx = BLE_ACTIVITY_MAX;
    }
}

// 供应用在进入配网前强制停止扫描并删除活动，释放 BLE 资源
void ql_ble_remote_force_stop_scan(void)
{
    int ret;

    s_scan_enable = 0;

    if (scan_actv_idx < BLE_ACTIVITY_MAX)
    {
        if (s_scanning)
        {
            ret = ql_ble_stop_scaning(scan_actv_idx, ble_central_cmd_cb);
            REMOTE_DBG("[alan] ql_ble_remote_force_stop_scan: stop_scan ret = %d\r\n", ret);
            (void) ret;
        }

        s_scanning = 0;

        // 等待扫描活动进入空闲态，再删除 activity
        for (int i = 0; i < 10; i++)
        {
            if (app_ble_actv_state_get(scan_actv_idx) == ACTV_IDLE)
            {
                break;
            }
            ql_rtos_task_sleep_ms(20);
        }

        ret = ql_ble_delete_scaning(scan_actv_idx, ble_central_cmd_cb);
        REMOTE_DBG("[alan] ql_ble_remote_force_stop_scan: delete_scan ret = %d\r\n", ret);
        (void) ret;

        scan_actv_idx = BLE_ACTIVITY_MAX;
        ble_central_state = 0;
    }
}

/**
 * 遥控器数据结构
 */
typedef struct {
    uint8_t valid;           /* 是否为有效的遥控器数据 */
    uint8_t key;             /* 按键编号（这里存放 cmd 值） */
    uint8_t status;          /* 按键状态: 0=按下/长按, 1=抬起 */
    uint8_t press_count;     /* 这里复用为帧序 num */
    uint16_t press_time_ms;  /* 按键按下时间(ms) */
    uint16_t remote_id;      /* 遥控ID（rolling code 拼成 16bit） */
    uint8_t device_type;     /* 保留 */
    uint16_t manufactor_code; /* 保留 */
} remote_control_data_t;

/**
 * 检查是否为目标遥控器设备
 */
static int app_check_remote_uuid(const uint8_t adv_addr[6])
{
    (void) adv_addr;
    /* 不固定 MAC，改由协议头 + 对码表过滤 */
    return 1;
}

/**
 * 解析遥控器数据（按新遥控协议）
 *
 * Tx_Payload（说明书）：
 *   [0-7]   = 报头 + MAC
 *   [8]    = 0x02
 *   [9]    = 0x01
 *   [10]   = 0x02
 *   [11]   = 0x1B
 *   [12]   = 0x16
 *   [13]   = 0x55
 *   [14]   = 0x56
 *   [15-16]= rolling_code[0..1]
 *   [17]   = num（帧序）
 *   [18]   = cmd（按键值：0x01~0x05 等）
 *   [19-22]= 0x00
 *   [23]   = key_time（按键时间：按下发 1，每 100ms++，松手发 0）
 *
 * BLE 上报的 adv data 对应从 Tx_Payload[8] 开始，所以在 data[] 中
 * 查找固定头 {02,01,02,1B,16,55,56}，然后解析后续字段。
 */
static remote_control_data_t app_parse_remote_data(const uint8_t *data, uint16_t data_len)
{
    remote_control_data_t result;
    memset(&result, 0, sizeof(result));

    /* 广播数据至少要能容纳固定头 + 滚码 + 帧序 + cmd + key_time */
    if (data_len < 16) {
        return result;
    }

    /* 在 adv data 中查找固定头 {02 01 02 1B 16 55 56} */
    const uint8_t kHeader[] = { 0x02, 0x01, 0x02, 0x1B, 0x16, 0x55, 0x56 };
    int start = -1;
    for (uint16_t i = 0; i + (uint16_t) sizeof(kHeader) <= data_len; i++) {
        if (memcmp(&data[i], kHeader, sizeof(kHeader)) == 0) {
            start = i;
            break;
        }
    }

    if (start < 0) {
        return result;
    }

    /* 确保后面还有 rolling_code[0..1] + num + cmd + 4*0 + key_time */
    if (data_len < (uint16_t) (start + 16)) {
        return result;
    }

    const uint8_t *p = &data[start];

    uint8_t rolling0   = p[7];
    uint8_t rolling1   = p[8];
    uint8_t num        = p[9];
    uint8_t cmd        = p[10];
    uint8_t key_time   = p[15];   /* 1 起步，每 100ms++，松手发 0 */

    (void)rolling0;
    (void)rolling1;

    /* 一级消抖：过滤完全相同的一帧重复上报 */
    static uint8_t s_lastRolling0 = 0x00;
    static uint8_t s_lastRolling1 = 0x00;
    static uint8_t s_lastFrameNum = 0x00;
    static uint8_t s_lastCmd      = 0x00;
    static uint8_t s_lastKeyTime  = 0x00;
    static uint8_t s_lastValid    = 0; // 0: 未初始化, 1: 有有效历史

    if (s_lastValid &&
        s_lastRolling0 == rolling0 &&
        s_lastRolling1 == rolling1 &&
        s_lastFrameNum == num &&
        s_lastCmd      == cmd &&
        s_lastKeyTime  == key_time)
    {
        return result;
    }

    s_lastRolling0 = rolling0;
    s_lastRolling1 = rolling1;
    s_lastFrameNum = num;
    s_lastCmd      = cmd;
    s_lastKeyTime  = key_time;
    s_lastValid    = 1;

    /* 填充结果结构体 */
    result.key           = cmd;           /* 按键编号：即说明书里的 cmd */
    result.press_count   = num;           /* 复用为帧序计数 */
    result.press_time_ms = (uint16_t) key_time * 100;/* 100ms 单位 */
    result.remote_id     = (uint16_t) ((((uint16_t) rolling1) << 8) | rolling0);

    /* 按键状态：key_time == 0 视为抬起，其它视为按下中 */
    if (key_time == 0) {
        result.status = 1; /* 抬起 */
    } else {
        result.status = 0; /* 按下/长按，业务层只关心按下事件 */
    }

    result.valid = 1;

    return result;
}

/**
 * 处理遥控器按键事件(尽量用队列事件发送出去执行，快进快出)
 */
static void app_handle_remote_key(const remote_control_data_t *remote_data)
{
    if (!remote_data->valid) {
        return;
    }
    
    // 只在按下事件时处理（status: 0=按下/长按,1=抬起）
    if (remote_data->status != 0)
    {
        return;
    }

    /*
     * 二级消抖：时间窗口消抖
     * - 为每个 cmd 维护最近一次触发的时间戳（ms）
     * - 在固定窗口内同一 cmd 只允许触发一次
     */
    static uint32_t s_lastCmdTime[0x20] = { 0 };
    const uint32_t now = rtos_get_time();
    const uint32_t debounceMs = 300;
    if (remote_data->key < (uint8_t) (sizeof(s_lastCmdTime) / sizeof(s_lastCmdTime[0])))
    {
        uint32_t last = s_lastCmdTime[remote_data->key];
        if ((now - last) < debounceMs)
        {
            return;
        }
        s_lastCmdTime[remote_data->key] = now;
    }

    /*
     * 新版遥控器功能码：
     *   0x01 -> 关闭灯带
     *   0x02 -> auto（5个情景模式每10秒轮换）
     *   0x03 -> 打开灯带
     *   0x04 -> mode+
     *   0x05 -> mode-
     *   0x06 -> 亮度+
     *   0x07 -> 音乐律动麦克风灵敏度+
     *   0x08 -> 音乐律动麦克风灵敏度-
     *   0x09 -> 亮度-
     *   0x0A -> 红色
     *   0x0B -> 绿色
     *   0x0C -> 蓝色
     *   0x0D -> 黄色
     *   0x0E -> 浅蓝
     *   0x0F -> 紫色
     *   0x10 -> 定时1h
     *   0x11 -> 定时2h
     *   0x12 -> W（偏暖白）
     *   0x13 -> 定时3h
     *   0x14 -> 定时4h
     *   0x15 -> C（偏冷白，原 COLOR 键）
     *   0x16 -> 音乐律动1
     *   0x18 -> 音乐律动2
     */
    if (!remote_light_is_on() && remote_data->key != 0x03)
    {
        return;
    }

    switch (remote_data->key) {
        case 0x01: // 关闭灯带
            REMOTE_LOG("[REMOTE] Power OFF (cmd=0x01)\r\n");
            remote_light_off();
            break;
        case 0x02: // auto
            REMOTE_LOG("[REMOTE] Auto scene toggle (cmd=0x02)\r\n");
            remote_light_scene_auto_toggle();
            break;
        case 0x03: // 打开灯带
            REMOTE_LOG("[REMOTE] Power ON (cmd=0x03)\r\n");
            remote_light_shutdown_timer_cancel();
            remote_light_on();
            break;
        case 0x04: // mode+
            REMOTE_LOG("[REMOTE] Scene mode next (cmd=0x04)\r\n");
            remote_light_scene_mode_next();
            break;
        case 0x05: // mode-
            REMOTE_LOG("[REMOTE] Scene mode prev (cmd=0x05)\r\n");
            remote_light_scene_mode_prev();
            break;
        case 0x06: // 亮度增加
            REMOTE_LOG("[REMOTE] Light level up (cmd=0x06)\r\n");
            remote_light_level_up();
            break;
        case 0x07: // 音乐律动灵敏度提高
            if (s_music_sensitivity < 5)
            {
                s_music_sensitivity++;
            }
            REMOTE_LOG("[REMOTE] Music sensitivity up=%u (cmd=0x07)\r\n", s_music_sensitivity);
            remote_light_music_set_sensitivity(s_music_sensitivity);
            break;
        case 0x08: // 音乐律动灵敏度降低
            if (s_music_sensitivity > 1)
            {
                s_music_sensitivity--;
            }
            REMOTE_LOG("[REMOTE] Music sensitivity down=%u (cmd=0x08)\r\n", s_music_sensitivity);
            remote_light_music_set_sensitivity(s_music_sensitivity);
            break;
        case 0x09: // 亮度减小
            REMOTE_LOG("[REMOTE] Light level down (cmd=0x09)\r\n");
            remote_light_level_down();
            break;
        case 0x0A: // 显示红色
            REMOTE_LOG("[REMOTE] Show RED color (cmd=0x0A)\r\n");
            remote_light_red();
            break;
        case 0x0B: // 显示绿色
            REMOTE_LOG("[REMOTE] Show GREEN color (cmd=0x0B)\r\n");
            remote_light_green();
            break;
        case 0x0C: // 显示蓝色
            REMOTE_LOG("[REMOTE] Show BLUE color (cmd=0x0C)\r\n");
            remote_light_blue();
            break;
        case 0x0D: // 显示黄色
            REMOTE_LOG("[REMOTE] Show YELLOW color (cmd=0x0D)\r\n");
            remote_light_yellow();
            break;
        case 0x0E: // 显示浅蓝色
            REMOTE_LOG("[REMOTE] Show LIGHTBLUE color (cmd=0x0E)\r\n");
            remote_light_lightblue();
            break;
        case 0x0F: // 显示紫色
            REMOTE_LOG("[REMOTE] Show PURPLE color (cmd=0x0F)\r\n");
            remote_light_purple();
            break;
        case 0x10: // 定时关机 1小时
            REMOTE_LOG("[REMOTE] Timer OFF 1 hour (cmd=0x10)\r\n");
            remote_light_shutdown_timer_start(3600UL * 1000UL);
            break;
        case 0x11: // 定时关机 2小时
            REMOTE_LOG("[REMOTE] Timer OFF 2 hours (cmd=0x11)\r\n");
            remote_light_shutdown_timer_start(7200UL * 1000UL);
            break;
        case 0x12: // W：偏暖白
            REMOTE_LOG("[REMOTE] Show WARM WHITE (cmd=0x12)\r\n");
            remote_light_white();
            break;
        case 0x13: // 定时关机 3小时
            REMOTE_LOG("[REMOTE] Timer OFF 3 hours (cmd=0x13)\r\n");
            remote_light_shutdown_timer_start(10800UL * 1000UL);
            break;
        case 0x14: // 定时关机 4小时
            REMOTE_LOG("[REMOTE] Timer OFF 4 hours (cmd=0x14)\r\n");
            remote_light_shutdown_timer_start(14400UL * 1000UL);
            break;
        case 0x15: // C：偏冷白（原 COLOR 键）
            REMOTE_LOG("[REMOTE] Show COLD WHITE (cmd=0x15)\r\n");
            remote_light_cold_white();
            break;
        case 0x16: // 柔和音乐律动
            REMOTE_LOG("[REMOTE] Music gentle (cmd=0x16)\r\n");
            remote_light_music_gentle();
            break;
        case 0x18: // 躁动音乐律动
            REMOTE_LOG("[REMOTE] Music aggressive (cmd=0x18)\r\n");
            remote_light_music_aggressive();
            break;
        default:
            REMOTE_LOG("[REMOTE] other cmd:0x%02X \r\n", remote_data->key);
            break;
    }
}


/**
 * 过滤广播数据包，找到遥控器的广播包并解析处理
 */
void app_demo_ble_adv_report_deal(const recv_adv_t * p_adv_report)
{
    /* 检查是否为遥控器设备 (根据adv_addr判断) */
    if (app_check_remote_uuid(p_adv_report->adv_addr)) {
        /* 解析遥控器数据 */
        remote_control_data_t remote_data = app_parse_remote_data(
            p_adv_report->data,
            p_adv_report->data_len);
        
        if (!remote_data.valid)
        {
            return;
        }

        /* 对码/锁定指令：0x17 — 始终生效，作为锁定/解锁切换。
         * 逻辑：如果当前遥控已被授权，则再次按 0x17 清除授权（解锁）；
         * 否则将该遥控写入配对表（锁定到该遥控）。
         */
        if (remote_data.key == 0x17)
        {
            if (remote_data.status != 0)
            {
                return;
            }

            // 如果当前配对表为空（未锁定），按 0x17 应该把当前遥控器写入配对表（锁定）。
            // 仅当已有配对时才以已授权/非授权来决定清码或忽略。
            if (LightingRemoteHasPairs())
            {
                if (LightingRemoteIsPaired(remote_data.remote_id))
                {
                    if (LightingRemoteClearPair(remote_data.remote_id))
                    {
                        REMOTE_LOG("[REMOTE_PAIR] cleared id 0x%04x (cmd=0x17)\r\n", remote_data.remote_id);
                    }
                }
                else
                {
                    // 已有配对且当前遥控非配对，按 0x17 则覆盖（因为 kMaxRemotePairs==1）
                    LightingRemotePairRequest(remote_data.remote_id);
                    REMOTE_LOG("[REMOTE_PAIR] paired id 0x%04x (replaced, cmd=0x17)\r\n", remote_data.remote_id);
                }
            }
            else
            {
                // 没有任何配对，按 0x17 时将当前遥控写入（锁定）
                LightingRemotePairRequest(remote_data.remote_id);
                REMOTE_LOG("[REMOTE_PAIR] paired id 0x%04x (first, cmd=0x17)\r\n", remote_data.remote_id);
            }

            return;
        }

        /* 非对码指令：必须已对码 */
        if (!LightingRemoteIsAuthorized(remote_data.remote_id))
        {
            // os_printf("[REMOTE_WARN] unpaired remote ignored\r\n");
            return;
        }

        /* 首次成功遥控时，通知关闭对码窗口 */
        static uint8_t s_notified_first_use = 0;
        if (!s_notified_first_use)
        {
            s_notified_first_use = 1;
            LightingRemoteFirstUsedNotify();
        }

        /* 处理按键事件 */
        app_handle_remote_key(&remote_data);
        
        return;
    }
}


static void ql_demo_central_notice_cb(ble_notice_t notice, void *param)
{
    switch (notice)
    {
    case BLE_5_REPORT_ADV://扫描事件
    {
        recv_adv_t *r_ind = (recv_adv_t *)param;

        app_demo_ble_adv_report_deal(r_ind);//协议处理
        break;
    }
    case BLE_5_STACK_OK:
        os_printf("ble stack ok");
        break;
    case BLE_5_ATT_INFO_REQ:
        // BLE ATT 由 CHIPoBLE 处理，遥控 central 不提供本地 GATT
        break;
    case BLE_5_CREATE_DB:
    {
        create_db_t *cd_ind = (create_db_t *)param;
        os_printf("cd_ind:prf_id:%d, status:%d\r\n", cd_ind->prf_id, cd_ind->status);
        break;
    }
    case BLE_5_SDP_REGISTER_FAILED:
        os_printf("BLE_5_SDP_REGISTER_FAILED\r\n");
        break;
    default:
        break;
    }
}

void ql_ble_remote_notice_cb(ble_notice_t notice, void *param)
{
    ql_demo_central_notice_cb(notice, param);
}

void ql_ble_remote_install_notice_cb(void)
{
    ql_ble_set_notice_cb(ql_demo_central_notice_cb);
}

void ql_ble_remote_reset_ble_state(void)
{
    s_ble_entry_done = false;
    ble_central_state = 0;
    s_scanning = 0;
    scan_actv_idx = BLE_ACTIVITY_MAX;
}


static void ble_central_cmd_cb(ble_cmd_t cmd, ble_cmd_param_t *param)
{
    os_printf("cmd:%d idx:%d status:%d\r\n", cmd, param->cmd_idx, param->status);
}
   
/**
 * 蓝牙扫描任务
 */
static void ql_ble_central_demo_entry(void *arg)
{
    int ret = 0;
    REMOTE_DBG("[alan] ql_ble_central_demo_entry\r\n");
    struct scan_param *scan_info = &g_scan_info;
    
    while (1)
    {
        if (s_central_suspend)
        {
            ql_rtos_task_sleep_ms(200);
            continue;
        }

        /* 等待协议栈初始化完成 */
        while (kernel_state_get(TASK_BLE_APP) != APPM_READY)
        {
            ql_rtos_task_sleep_ms(200);
        }

        if(ble_central_state == 0)
        {
            // BLE notice callback is owned by CHIPoBLE; adv reports are forwarded via ql_ble_remote_notice_cb.
            // 不再调用 ql_ble_init()，避免与 CHIPoBLE 的 GATT 数据库冲突

            /* 设置扫描参数 */
            scan_info->channel_map = 0x07;
            scan_info->interval = 100;//扫描间隔
            scan_info->window = 30;//扫描窗口

            /* 获取空闲索引 actv_idx */
            scan_actv_idx = ql_ble_get_idle_actv_idx_handle();
            REMOTE_DBG("[alan] ql_ble_get_idle_actv_idx_handle ret:%d\r\n", scan_actv_idx);
            if (scan_actv_idx >= BLE_ACTIVITY_MAX)
            {
                REMOTE_DBG("[alan] scan_actv_idx error\r\n");
                ble_central_state = 0;
                ql_rtos_task_sleep_ms(200);
                continue;
            }

            ble_central_state = 1;
        }

        if (ble_central_state == 1)
        {
            if (s_scan_enable)
            {
                if (!s_scanning)
                {
                    if (app_ble_actv_state_get(scan_actv_idx) == ACTV_IDLE)
                    {
                        ret = ql_ble_scan_start(scan_actv_idx , scan_info , ble_central_cmd_cb);
                        REMOTE_DBG("[alan] ql_ble_scan_start ret = %d\r\n", ret);
                    }
                    else
                    {
                        ret = ql_ble_start_scaning(scan_actv_idx , scan_info->interval , scan_info->window , ble_central_cmd_cb);
                        REMOTE_DBG("[alan] ql_ble_start_scaning ret = %d\r\n", ret);
                    }

                    if (ret == QL_BT_SUCCESS)
                    {
                        s_scanning = 1;
                    }
                }
            }
            else
            {
                if (s_scanning)
                {
                    ret = ql_ble_stop_scaning(scan_actv_idx, ble_central_cmd_cb);
                    REMOTE_DBG("[alan] ql_ble_stop_scaning ret = %d\r\n", ret);
                    s_scanning = 0;
                }
            }
        }

        ql_rtos_task_sleep_ms(200);
    }

    ble_central_hdl = NULL;
    ql_rtos_task_delete(NULL);
}

void ql_ble_demo_central_thread_creat(void)
{
    OSStatus err = kNoErr;
    REMOTE_DBG("[alan] ql_ble_demo_central_thread_creat\r\n");

    if (ble_central_hdl != NULL)
    {
        REMOTE_DBG("[alan] ql_ble_demo_central_thread_creat: already running\r\n");
        return;
    }

    if (!s_ble_entry_done)
    {
        ble_entry();
        s_ble_entry_done = true;
    }

    // todo  create new thread;
    err = ql_rtos_task_create(&ble_central_hdl,
                                4*1024,
                                THD_EXTENDED_APP_PRIORITY,
                                "ble_central_demo",
                                ql_ble_central_demo_entry,
                                0);

    if (err != kNoErr)
    {
        return ;
    }

    return;
}
