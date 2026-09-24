#include "ql_ble.h"

extern ble_err_t bk_ble_init(void);

ql_errcode_bt_e ql_ble_set_notice_cb(ble_notice_cb_t func)
{
    ble_set_notice_cb(func);

    return QL_BT_SUCCESS;
}

ql_errcode_bt_e ql_ble_init(void)
{
    return (ql_errcode_bt_e)bk_ble_init();
}
uint8_t ql_ble_get_idle_actv_idx_handle(void)
{
    return app_ble_get_idle_actv_idx_handle();
}

ql_errcode_bt_e ql_ble_set_scan_rsp_data(uint8_t actv_idx, unsigned char* scan_buff, unsigned char scan_len, ble_cmd_cb_t callback)
{
    return (ql_errcode_bt_e)bk_ble_set_scan_rsp_data(actv_idx,scan_buff, scan_len,callback);
}

ql_errcode_bt_e ql_ble_create_scaning(uint8_t actv_idx, ble_cmd_cb_t callback)
{
    return (ql_errcode_bt_e)bk_ble_create_scaning(actv_idx,callback);
}

ql_errcode_bt_e ql_ble_start_scaning(uint8_t actv_idx, uint16_t scan_intv, uint16_t scan_wd, ble_cmd_cb_t callback)
{
    if((scan_intv < QL_BT_SCAN_INTERVAL_MIN) || (scan_intv > QL_BT_SCAN_INTERVAL_MAX) || 
        (scan_wd < QL_BT_SCAN_WINDOW_MIN) || ((scan_wd > QL_BT_SCAN_WINDOW_MAX)) || 
        (scan_wd > scan_intv))
    {
        return QL_BT_ERR_BLE_PARAM;
    }        

    return (ql_errcode_bt_e)bk_ble_start_scaning(actv_idx,scan_intv,scan_wd,callback);
}

ql_errcode_bt_e ql_ble_scan_start(uint8_t actv_idx, struct scan_param *scan, ble_cmd_cb_t callback)
{
    if((scan->interval < QL_BT_SCAN_INTERVAL_MIN) || (scan->interval  > QL_BT_SCAN_INTERVAL_MAX) || 
        (scan->window < QL_BT_SCAN_WINDOW_MIN) || ((scan->window > QL_BT_SCAN_WINDOW_MAX)) || 
        (scan->window > scan->interval))
    {
        return QL_BT_ERR_BLE_PARAM;
    }      

    return (ql_errcode_bt_e)bk_ble_scan_start(actv_idx , scan , callback);
}


ql_errcode_bt_e ql_ble_stop_scaning(uint8_t actv_idx, ble_cmd_cb_t callback)
{
    return (ql_errcode_bt_e)bk_ble_stop_scaning(actv_idx,callback);
}

ql_errcode_bt_e ql_ble_delete_scaning(uint8_t actv_idx, ble_cmd_cb_t callback)
{
    return (ql_errcode_bt_e)bk_ble_delete_scaning(actv_idx,callback);
}

#if (BLE_APP_PRESENT && (BLE_CENTRAL) && (BLE_SDP_CLIENT))
ql_errcode_bt_e ql_ble_gattc_read_service_data_by_handle_req(uint8_t conidx,uint16_t handle,ble_cmd_cb_t callback)
{
    return (ql_errcode_bt_e)bk_ble_read_service_data_by_handle_req(conidx,handle,callback);
}

ql_errcode_bt_e ql_ble_gattc_write_service_data_req(uint8_t conidx,uint16_t handle,uint16_t data_len,uint8_t *data,ble_cmd_cb_t callback)
{
    return (ql_errcode_bt_e)bk_ble_write_service_data_req(conidx,handle,data_len,data,callback);
}
#endif
