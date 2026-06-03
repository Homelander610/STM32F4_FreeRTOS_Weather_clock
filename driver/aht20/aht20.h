#ifndef __AHT20_H__
#define __AHT20_H__

#include <stdbool.h>
#include "stm32f4xx.h"                  // Device header
#include "stm32f4xx_conf.h"	
#include "tim_delay.h"

//PB8 I2C_SCL
//PB9 I2C_SDA

bool aht20_init(void);
bool aht20_start_measurement(void);
bool aht20_wait_for_measurement(void);
bool aht20_read_measurement(float *temperature, float *humidity);


#endif /*__AHT20_H__*/
