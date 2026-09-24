 #ifndef _AT_SVR_OPTS_H_
#define _AT_SVR_OPTS_H_


#define ATSVR_VERSION_NUM            "FC41DAAR03A08_beta.bin"

#ifdef QUEC_VERSION
#undef ATSVR_VERSION_NUM
#define ATSVR_VERSION_NUM QUEC_VERSION
#endif

#define ATSVR_MAX_COMMANDS           128//64
#define ATSVR_MAX_ARG                16

#define ATSVR_ADD_ESCAPE_CFG         1
#define ATSVR_INPUT_BUFF_MAX_SIZE           1024

#define ATSVR_POWER_UP_READY_DELAY          400

#define ATSVR_READY_MSG                     "\r\nready\r\n"
#define ATSVR_CMD_RSP_SUCCEED               "\r\nOK\r\n"
#define ATSVR_CMD_RSP_ERROR                 "\r\nERROR\r\n"
#define ATSVR_CMDMSG_ERROR_RSP              "\r\nERROR\r\n"
#define ATSVR_RET_CHAR                      '\r'
#define ATSVR_END_CHAR		                '\n'
#define ATSVR_INPUT_CHAR		            ">"

#define ATSVR_EVENT_HEAD                    "+QSTASTAT:"
#define ATSVR_EVT_WLAN_DISCONNECTED         "+QSTASTAT:WLAN_DISCONNECTED\r\n"
#define ATSVR_EVT_WLAN_CONNECTED            "+QSTASTAT:WLAN_CONNECTED\r\n"
#define ATSVR_EVT_GOT_IP                    "+QSTASTAT:GOT_IP\r\n"
#define ATSVR_EVT_NO_AP						"+QSTASTAT:SCAN_NO_AP\r\n"
#define ATSVR_EVT_AP_CONNECT				"+QSTASTAT:AP_CONNECT\r\n"
#define ATSVR_EVT_AP_DISCONNECT				"+QSTASTAT:AP_DISCONNECT\r\n"
#define ATSVR_EVT_AP_CONNECT_FAILED			"+QSTASTAT:AP_CONNECT_FAILED\r\n"



#ifndef __weak__
#define __weak__              __attribute__((weak))
#endif

extern void bk_printf(const char *fmt, ...);
#define ATSVRLOG                     bk_printf


#endif
