#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"
#include "string.h"
#include "console.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

// TX: PA9
// RX: PA10

static SemaphoreHandle_t write_async_semaphore;
static console_received_func_t received_func;

// GPIO
static void console_io_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	// PA9、PA10引脚复用
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);
}
// usart
static void console_usart_init(void)
{
	USART_InitTypeDef USART_InitStructure;
	USART_StructInit(&USART_InitStructure);

	USART_InitStructure.USART_BaudRate = 115200u;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART1, &USART_InitStructure);

	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
	USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE);
	USART_Cmd(USART1, ENABLE);
}
// DMA
static void console_dma_init(void)
{
	DMA_InitTypeDef DMA_Structure;
	DMA_StructInit(&DMA_Structure);
	DMA_Structure.DMA_Channel = DMA_Channel_4;							// USART1使用通道4
	DMA_Structure.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR;		// DR寄存器地址
	DMA_Structure.DMA_DIR = DMA_DIR_MemoryToPeripheral;					// 传输方向 内存->外设
	DMA_Structure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;		// 外设地址是否自增	false
	DMA_Structure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; // 外设数据单元大小
	DMA_Structure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;			// 内存读取字节大小
	DMA_Structure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_Structure.DMA_Mode = DMA_Mode_Normal;		  // 循环/单次模式		单次
	DMA_Structure.DMA_Priority = DMA_Priority_Low;	  // 优先级
	DMA_Structure.DMA_FIFOMode = DMA_FIFOMode_Enable; // 启用
	DMA_Structure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
	DMA_Structure.DMA_MemoryBurst = DMA_MemoryBurst_INC8;
	DMA_Structure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
	DMA_ITConfig(DMA2_Stream7, DMA_IT_TC, ENABLE);
	DMA_Init(DMA2_Stream7, &DMA_Structure); // USART1_TX->DMA1_Stream4
}
// 中断
static void console_int_init(void)
{
	// 初始化串口的RX中断
	NVIC_InitTypeDef NVIC_InitStruct;
	NVIC_InitStruct.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 5;
	NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStruct);
	NVIC_SetPriority(USART1_IRQn, 5);

	// 初始化dma的TC中断
	NVIC_InitStruct.NVIC_IRQChannel = DMA2_Stream7_IRQn;
	NVIC_Init(&NVIC_InitStruct);
	NVIC_SetPriority(DMA2_Stream7_IRQn, 5);
}

void console_init(void)
{
	write_async_semaphore = xSemaphoreCreateBinary(); // 创建一个二值信号量
	configASSERT(write_async_semaphore);			  // 做一个安全检查，检查信号量是否创建成功

	console_usart_init();
	console_dma_init();
	console_int_init();
	console_io_init();
}
void console_write(const char str[])
{
	uint32_t length = strlen(str);
	do
	{
		uint32_t chunk_size = length < 65535 ? length : 65535;

		DMA2_Stream7->M0AR = (uint32_t)str;
		DMA2_Stream7->NDTR = chunk_size;

		DMA_Cmd(DMA2_Stream7, ENABLE);
		xSemaphoreTake(write_async_semaphore, portMAX_DELAY);

		length -= chunk_size;
		str += chunk_size;
	} while (length > 0);

	while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
		;
	USART_ClearFlag(USART1, USART_FLAG_TC);
}
// USART的回调函数
void console_received_register(console_received_func_t func)
{
	received_func = func;
}
// USART的中断函数
void USART1_IRQHandler(void)
{
	if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
	{
		if (received_func != NULL)
		{
			uint8_t data = USART_ReceiveData(USART1);
			received_func(data);
		}
		USART_ClearITPendingBit(USART1, USART_IT_RXNE);
	}
}
void DMA2_Stream7_IRQHandler(void)
{
	if (DMA_GetITStatus(DMA2_Stream7, DMA_IT_TCIF7) == SET)
	{
		BaseType_t pxHigherPriorityTaskWoken;
		xSemaphoreGiveFromISR(write_async_semaphore, &pxHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);

		DMA_ClearITPendingBit(DMA2_Stream7, DMA_IT_TCIF7);
	}
}
