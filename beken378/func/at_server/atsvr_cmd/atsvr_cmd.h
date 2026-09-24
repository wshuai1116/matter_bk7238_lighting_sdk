#ifndef _ATSVR_CMD_H_
#define _ATSVR_CMD_H_

#include "atsvr_cmd_cfg.h"

extern void atsvr_cmd_init(void);
extern void atsvr_extern_cmd_init(void);
#ifndef _SEND_HEX_
#define _SEND_HEX_

enum NET_TYPE
{
	UDP_CLIENT = 1,
	UDP_SERVER,
	TCP_CLIENT,
	TCP_SERVER,
	BLE_GATT,
};




extern  void atsvr_wncv_type_cmd(int argc, char **argv);//Type

extern  void atsvr_wncv_eptype_cmd(int argc, char **argv);//EndProductType

extern  void atsvr_wncv_cfgstatus_cmd(int argc, char **argv);//ConfigStatus

extern  void atsvr_wncv_opstatus_cmd(int argc, char **argv);//OperationalStatus

extern  void atsvr_wncv_liolimit_cmd(int argc, char **argv);//Lift InstalledOpenLimit

extern  void atsvr_wncv_liclimit_cmd(int argc, char **argv);//Lift InstalledClosedLimit

extern  void atsvr_wncv_tiolimit_cmd(int argc, char **argv);//Tilt InstalledOpenLimit
 
extern  void atsvr_wncv_ticlimit_cmd(int argc, char **argv);//Tilt InstalledClosedLimit

extern  void atsvr_wncv_ltgposition_cmd(int argc, char **argv);//Lift TargetPosition

extern  void atsvr_wncv_lcrposition_cmd(int argc, char **argv);//Lift CurrentPosition

extern  void atsvr_wncv_ttgposition_cmd(int argc, char **argv);//Tilt TargetPosition

extern  void atsvr_wncv_tcrposition_cmd(int argc, char **argv);//Tilt CurrentPosition

extern  void atsvr_wncv_mode_cmd(int argc, char **argv);//Mode

extern  void atsvr_wncv_safstatus_cmd(int argc, char **argv);//SafetyStatus

extern  void atsvr_factory_reset_cmd(int argc, char **argv);//factory reset

extern  void atsvr_re_commission_cmd(int argc, char **argv);//recommissioning

extern  void atsvr_ps_vol_bat_cmd(int argc, char **argv);//BatVoltage

extern  void atsvr_ps_pctr_bat_cmd(int argc, char **argv);//BatPercentRemaining

extern  void atsvr_ps_chglvl_bat_cmd(int argc, char **argv);//BatChargeLevel

extern  void atsvr_ps_repneed_bat_cmd(int argc, char **argv);//BatReplacementNeeded

extern  void atsvr_ps_repabil_bat_cmd(int argc, char **argv);//BatReplaceability

extern  void atsvr_ps_chgstate_bat_cmd(int argc, char **argv);//BatChargeState

extern  void atsvr_ps_funcwhlchg_bat_cmd(int argc, char **argv);//BatFunctionalWhileCharging

extern  void atsvr_get_qrcode_url_cmd(int argc, char **argv);

extern  void atsvr_get_manual_code_cmd(int argc, char **argv);

extern  void atsvr_dyn_ep_cmd(int argc, char **argv); //DynamicEndpoint
extern  void atsvr_dyn_del_cmd(int argc, char **argv); //DynamicEndpointDelete
extern  void atsvr_dyn_list_cmd(int argc, char **argv); //DynamicEndpointList


extern  void atsvr_dyn_ep_clr_cmd(int argc, char **argv); //DynamicEndpointClear


#endif

#endif

