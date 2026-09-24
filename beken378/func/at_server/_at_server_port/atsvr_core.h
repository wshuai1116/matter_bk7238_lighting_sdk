#ifndef _ATSVR_CORE_H_
#define _ATSVR_CORE_H_

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum{
	ATSVR_MSG_INIT = 0,
	ATSVR_MSG_STREAM,
	ATSVR_MSG_EVENT,
}atsvr_msgtype_e;

typedef enum{
	ATSVR_SUBMSG_NONE = 0x00,
	ATSVR_SUBMSG_ATFREE,
}atsvr_subtype_e;

typedef enum{
	ATSVR_MATTER_SESSION_DISCONNECT,
    ATSVR_MATTER_COMMISSSION_FAILED,
	ATSVR_MATTER_SESSION_CONNECTTED,
    ATSVR_MATTER_OPEN_COMISSION_WINDOW,
	ATSVR_MATTER_CLOSE_COMISSION_WINDOW,
    ATSVR_MATTER_NEED_COMISSION,
    ATSVR_MATTER_NO_NEED_COMISSION,
    ATSVR_NETWORK_CONNECT,
    ATSVR_NETWORK_DISCONNECT,
	ATSVR_THREAD_NO_FOUND,
	ATSVR_THREAD_CONNECT,
	ATSVR_THREAD_DISCONNECT,
	ATSVR_THREAD_CONNECT_FAILED,
}ATSVR_EVT_T;

typedef struct{
	atsvr_msgtype_e type;
	atsvr_subtype_e sub_type;
	unsigned int addition_infor;
	unsigned int len;
	void *msg_param;
}atsvr_msg_t;

extern int atsvr_msg_get_input(unsigned char *inbuf, unsigned int *bp);
extern void atsvr_msg_handler(atsvr_msg_t *msg);

extern int atsvr_send_msg_queue(atsvr_msg_t *sd_atsvrmsg,unsigned int timeout);
extern int atsvr_send_msg_queue_block(atsvr_msg_t *sd_atsvrmsg);
extern int atsvr_port_send_msg_queue(atsvr_msg_t *sd_atsvrmsg,unsigned int timeout);

extern void atsvr_event_handler(ATSVR_EVT_T event);
extern void atsvr_event_sender(ATSVR_EVT_T event);

extern void ql_notify_matter_event(ATSVR_EVT_T event);

extern void quec_report_commission_state(void);
extern unsigned short generate_discriminator_by_mac();

int ql_report_enter_low_mode(void);

#ifdef __cplusplus
}
#endif


#endif

