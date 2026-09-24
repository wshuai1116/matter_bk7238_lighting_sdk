#ifndef __QL_API_BLE_H__
#define __QL_API_BLE_H__

#include "app_ble.h"
#include "common_bt_defines.h"
#include "app_sdp.h"
#include "app_task.h"
// 使用相对路径引用 Quectel OSI 适配层头文件，避免依赖全局包含路径配置
#include "../../../../../../quectel/quec_adapt/api/ql_osi.h"
#include "rtos_pub.h"
#include "uart_pub.h"
#include "error.h"
#include "ble_ui.h"
#include "ble_api.h"
#include "ble_api_5_x.h"

#if (CFG_BLE_SCAN_NUM)
#define BLE_REMOTE_ENABLE 1
#endif

#define QL_BT_SCAN_INTERVAL_MIN 18   //UNIT:0.625MS
#define QL_BT_SCAN_INTERVAL_MAX 4096  //UNIT:0.625MS

#define QL_BT_SCAN_WINDOW_MIN 17   //UNIT:0.625MS
#define QL_BT_SCAN_WINDOW_MAX 4096  //UNIT:0.625MS

#if (CFG_BLE_VERSION == BLE_VERSION_5_1) || (CFG_BLE_VERSION == BLE_VERSION_4_2)
typedef struct
{
	/// 16 bits UUID LSB First
	uint8_t uuid[16];
	/// Attribute Permissions (@see enum attm_perm_mask)
	uint16_t perm;
	/// Attribute Extended Permissions (@see enum attm_value_perm_mask)
	uint16_t ext_perm;
	/// Attribute Max Size
	/// note: for characteristic declaration contains handle offset
	/// note: for included service, contains target service handle
	uint16_t max_size;
}ql_attm_desc_t;
#elif (CFG_BLE_VERSION == BLE_VERSION_5_2)
typedef struct
{
	/// Attribute UUID (LSB First)
	uint8_t  uuid[16];
	/// Attribute information bit field (@see enum gatt_att_info_bf)
	uint16_t info;
	/// Attribute extended information bit field (@see enum gatt_att_ext_info_bf)
	/// Note:
	///   - For Included Services and Characteristic Declarations, this field contains targeted handle.
	///   - For Characteristic Extended Properties, this field contains 2 byte value
	///   - For Client Characteristic Configuration and Server Characteristic Configuration, this field is not used.
	uint16_t ext_info;
}ql_attm_desc_t;
#endif 

struct ql_ble_db_cfg
{
	uint16_t prf_task_id;
	///Service uuid
	uint8_t uuid[16];
	///Number of db
	uint8_t att_db_nb;
	///Start handler, 0 means autoalloc
	uint16_t start_hdl;
	///Attribute database
	ql_attm_desc_t* att_db;
	///Service config
	uint8_t svc_perm;
};

typedef enum {
	QL_BT_SUCCESS = 0,
	QL_BT_ERR_PROFILE,
	QL_BT_ERR_CREATE_DB,
	QL_BT_ERR_CMD_NOT_SUPPORT,
	QL_BT_ERR_UNKNOW_IDX,
	QL_BT_ERR_BLE_STATUS,
	QL_BT_ERR_BLE_PARAM,
	QL_BT_ERR_ADV_DATA,
	QL_BT_ERR_CMD_RUN,
	QL_BT_ERR_NO_MEM,
	QL_BT_ERR_INIT_CREATE,
	QL_BT_ERR_INIT_STATE,

	QL_BT_ERR_ATTC_WRITE,
	QL_BT_ERR_ATTC_WRITE_UNREGISTER,
} ql_errcode_bt_e;

ql_errcode_bt_e ql_ble_set_notice_cb(ble_notice_cb_t func);
ql_errcode_bt_e ql_ble_init(void);
uint8_t ql_ble_get_idle_actv_idx_handle(void);
ql_errcode_bt_e ql_ble_create_scaning(uint8_t actv_idx, ble_cmd_cb_t callback);
ql_errcode_bt_e ql_ble_stop_scaning(uint8_t actv_idx, ble_cmd_cb_t callback);
ql_errcode_bt_e ql_ble_delete_scaning(uint8_t actv_idx, ble_cmd_cb_t callback);
ql_errcode_bt_e ql_ble_scan_start(uint8_t actv_idx, struct scan_param *scan, ble_cmd_cb_t callback);
ql_errcode_bt_e ql_ble_start_scaning(uint8_t actv_idx, uint16_t scan_intv, uint16_t scan_wd, ble_cmd_cb_t callback);
ql_errcode_bt_e ql_ble_set_scan_rsp_data(uint8_t actv_idx, unsigned char* scan_buff, unsigned char scan_len, ble_cmd_cb_t callback);


#ifdef __cplusplus
extern "C" {
#endif

void ql_ble_demo_central_thread_creat(void);
void app_demo_ble_adv_report_deal(const recv_adv_t * p_adv_report);

#ifdef __cplusplus
}
#endif

#endif


