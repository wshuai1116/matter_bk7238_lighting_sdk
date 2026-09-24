/*================================================================
  Copyright (c) 2021, Quectel Wireless Solutions Co., Ltd. All rights reserved.
  Quectel Wireless Solutions Proprietary and Confidential.
=================================================================*/
//#include "include.h"
#include "ql_type.h"
#include "ql_debug.h"
#include "ql_gpio.h"
#include "ql_i2c.h"
#include "ql_pwm.h"
#include "ql_timer.h"
#include "ql_watchdog.h"
#include "ql_uart.h"

#define CFG_ENABLE_QUECTEL_OSI 0
#define CFG_ENABLE_QUECTEL_ADC 0
#define CFG_ENABLE_QUECTEL_TIMER 0
#define CFG_ENABLE_QUECTEL_GPIO 0
#define CFG_ENABLE_QUECTEL_UART 0
#define CFG_ENABLE_QUECTEL_PWM 0
#define CFG_ENABLE_QUECTEL_SPI 0
#define CFG_ENABLE_QUECTEL_FLASH 0
#define CFG_ENABLE_QUECTEL_WDT 0
#define CFG_ENABLE_QUECTEL_I2C 0
#define CFG_ENABLE_QUECTEL_OTA 0
#define CFG_ENABLE_QUECTEL_LOW_POWER 0

#if CFG_ENABLE_QUECTEL_OSI
extern  void ql_osi_demo_thread_creat(void);
#endif

#if CFG_ENABLE_QUECTEL_ADC
extern void ql_adc_demo_thread_creat(void);
#endif

#if CFG_ENABLE_QUECTEL_TIMER
extern void	ql_timer_demo_thread_creat(void);
#endif

#if CFG_ENABLE_QUECTEL_GPIO
extern void	ql_gpio_demo_thread_creat(void);
#endif

#if CFG_ENABLE_QUECTEL_UART
extern void	ql_uart_demo_thread_creat(void);
#endif

#if CFG_ENABLE_QUECTEL_PWM
extern void	ql_pwm_demo_thread_creat(void);
#endif

#if CFG_ENABLE_QUECTEL_SPI
extern void ql_spi_demo_thread_creat(void);
#endif

#if CFG_ENABLE_QUECTEL_FLASH
extern void ql_flash_demo_thread_creat(void);
#endif

#if CFG_ENABLE_QUECTEL_WDT
extern void	ql_wdg_demo_thread_create(void);
#endif

#if CFG_ENABLE_QUECTEL_I2C
extern void	ql_i2c_demo_thread_creat(void);
#endif

#if CFG_ENABLE_QUECTEL_LOW_POWER
extern void ql_lowpower_demo_thread_creat(void);
#endif

#if CFG_ENABLE_QUECTEL_OTA
extern void ql_ota_demo_thread_creat(void);
#endif

void quec_app_main(void * argv)
{
    ql_dbg_log("\nEnter %s\n", __func__);

#if CFG_ENABLE_QUECTEL_ADC
	ql_adc_demo_thread_creat();
#endif

#if CFG_ENABLE_QUECTEL_FLASH
	ql_flash_demo_thread_creat();
#endif

#if CFG_ENABLE_QUECTEL_GPIO
	ql_gpio_demo_thread_creat();
#endif

#if CFG_ENABLE_QUECTEL_I2C
	ql_i2c_demo_thread_creat();
#endif

#if CFG_ENABLE_QUECTEL_PWM
	ql_pwm_demo_thread_creat();
#endif

#if CFG_ENABLE_QUECTEL_SPI
	ql_spi_demo_thread_creat();
#endif

#if CFG_ENABLE_QUECTEL_TIMER
	ql_timer_demo_thread_creat();
#endif

#if CFG_ENABLE_QUECTEL_WDT
	ql_wdg_demo_thread_create();
#endif

#if CFG_ENABLE_QUECTEL_UART
	ql_uart_demo_thread_creat();
#endif

#if CFG_ENABLE_QUECTEL_OSI
	ql_osi_demo_thread_creat();
#endif

#if CFG_ENABLE_QUECTEL_LOW_POWER
	ql_lowpower_demo_thread_creat();
#endif

#if CFG_ENABLE_QUECTEL_OTA
	ql_ota_demo_thread_creat();
#endif
}
