#ifndef _ATSVR_PORT_H_
#define _ATSVR_PORT_H_
#ifdef __cplusplus
extern "C"
{
#endif
#define ATSVR_QUEUE_MAX_NB              100

#define AT_UART_PORT_CFG                UART1_PORT
#define AT_CTS_RTS_SOFTWARE_CFG         1
#define AT_UART_SEND_DATA_INTTRRUPT_PROTECT   0

#include "stdint.h"
#define IN
#define OUT
#define CONST const
#define STRING

extern void set_at_uart_overflow(int enable);
extern int get_at_uart_overflow(void);

extern void atsvr_overflow_handler(void);

extern int IN atsvr_input_char(unsigned char *buf);
extern void timer_getinput_handler(void);

enum deliver_data_mode
{
	NORMAL_AT = 1,
	NORAML_AT_TO_RAWDATA,
	RAWDATA_TO_NORMAL_AT,
};


typedef struct{
	void (*escape_func)();
	void (*readable_func)();
	void (*enter_rawdata_func)();
	void(*enter_rawdata_complete_func)();
}rawdata_mode_func_tbl;

typedef enum{
	ATSVR_AT_CMD_MODE = 0,
	ATSVR_AT_DATA_ONLINE_MODE = 1,
	ATSVR_AT_DATA_CMD_MODE = 2,
}port_mode_e;

extern void atsvr_register_rawdata_mode_func(rawdata_mode_func_tbl *func_tbl);

extern void atsvr_deregister_rawdata_mode_func(rawdata_mode_func_tbl *func_tbl);

extern rawdata_mode_func_tbl *atsvr_get_rawdata_mode_func();

extern void atsvr_change_port_mode(int mode);

extern void atsvr_enter_rawdata_complete();

extern int  atsvr_get_port_mode();

extern void atsvr_uart_set_rx_sema(void);

int atsvr_app_init(void);

void atsvr_msg_main( void * data );

void atsvr_handler_main( void* data );



#ifdef __cplusplus
}
#endif

#endif

