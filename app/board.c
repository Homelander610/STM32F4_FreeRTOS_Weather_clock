#include <stdio.h>
#include <stdint.h>
#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"
#include "board.h"
#include "console.h"
#include "esp_at.h"
#include "weather.h"
#include "aht20.h"
#include "rtc.h"
#include "FreeRTOS.h"
#include "task.h"

//开启时钟
void board_lowlevel_init(void)
{
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA,ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB,ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD,ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1,ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3,ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2,ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1,ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2,ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
	
	PWR_BackupAccessCmd(ENABLE);	//开启对备份域的访问 包含RTC实时时钟 LSE低速外部时钟 备份寄存器 (这里主要是前两个)
	RCC_LSEConfig(RCC_LSE_ON);	//开启LSE LSE通常连接一个32.768kHz的晶振（2的15次方），易分频得到1Hz的RTC时钟
	while(RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET);
	RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
}

//板子设备初始化
void board_init(void)
{
	tim_delay_init();
	console_init();
	printf("[BOARD] Console init OK\r\n");
	rtc_init();
	printf("[BOARD] RTC init OK\r\n");
	if(aht20_init()){
		printf("[BOARD] AHT20 init OK\r\n");
	}
	else{
		printf("[BOARD] AHT20 init FAILED!\r\n");
	}
	
	printf("[BOARD] Board low_level init finished\r\n");
}

//重定向 printf 到 USART1
int fputc(int ch, FILE *f)
{
	while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
	USART_SendData(USART1, (uint8_t)ch);
	return ch;
}
void vAssertCalled(const char *file, int line)
{
	portDISABLE_INTERRUPTS();
    printf("Assert Called: %s(%d)\r\n", file, line);
}

void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName)
{
    printf("Stack Overflowed: %s\r\n", pcTaskName);
    configASSERT(0);
}

void vApplicationMallocFailedHook( void )
{
    printf("Malloc Failed\r\n");
    configASSERT(0);
}

