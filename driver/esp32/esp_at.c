#include <stdbool.h>
#include <stdio.h>
#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"
#include "string.h"
#include "esp_at.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "console.h"
#include "weather.h"

// TX: PD8
// RX: PD9

typedef enum
{
	AT_ACK_NONE,
	AT_ACK_OK,
	AT_ACK_ERROR,
	AT_ACK_BUSY,
	AT_ACK_READY,
} at_ack_t;

typedef struct
{
	at_ack_t ack;
	const char *string;
} at_ack_match_t;

static const at_ack_match_t at_ack_matches[] =
	{
		{AT_ACK_OK, "OK\r\n"},
		{AT_ACK_ERROR, "ERROR\r\n"},
		{AT_ACK_BUSY, "busy p…\r\n"},
		{AT_ACK_READY, "ready\r\n"},
};

static char *rxline;
static char rxbuf[1024];
static char txbuf[1024];
static uint32_t rxlen;
static at_ack_t rxack;
static SemaphoreHandle_t at_ack_semaphore;

static bool esp_at_wait_ready(uint32_t timeout);
static bool esp_at_write_command(const char *command, uint32_t timeout);
static void esp_at_usart_write(const char *data);
static bool esp_at_wait_boot(uint32_t timeout);

static void esp_at_io_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_StructInit(&GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	// PD8 PD9引脚复用
	GPIO_PinAFConfig(GPIOD, GPIO_PinSource8, GPIO_AF_USART3);
	GPIO_PinAFConfig(GPIOD, GPIO_PinSource9, GPIO_AF_USART3);
}
static void esp_at_usart_init(void)
{
	USART_InitTypeDef USART_InitStructure;
	USART_StructInit(&USART_InitStructure);

	USART_InitStructure.USART_BaudRate = 115200u;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART3, &USART_InitStructure);

	USART_DMACmd(USART3, USART_DMAReq_Tx, ENABLE);
	USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
	USART_Cmd(USART3, ENABLE);
}
static void esp_at_dma_init(void)
{
	DMA_InitTypeDef DMA_Structure;
	DMA_StructInit(&DMA_Structure);
	DMA_Structure.DMA_Channel = DMA_Channel_4;							// USART3使用通道4
	DMA_Structure.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DR;		// DR寄存器地址
	DMA_Structure.DMA_DIR = DMA_DIR_MemoryToPeripheral;					// 传输方向 内存->外设
	DMA_Structure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;		// 外设地址是否自增	false
	DMA_Structure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; // 外设数据单元大小
	DMA_Structure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;			// 内存读取字节大小
	DMA_Structure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_Structure.DMA_Mode = DMA_Mode_Normal;		  // 循环/单次模式		单次
	DMA_Structure.DMA_Priority = DMA_Priority_Medium; // 优先级
	DMA_Structure.DMA_FIFOMode = DMA_FIFOMode_Enable; // 启用
	DMA_Structure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
	DMA_Structure.DMA_MemoryBurst = DMA_MemoryBurst_INC8;
	DMA_Structure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
	DMA_Init(DMA1_Stream3, &DMA_Structure); // USART3_TX->DMA1_Stream3
}
static void esp_at_int_init(void)
{
	// 初始化串口的RX中断
	NVIC_InitTypeDef NVIC_InitStruct;
	NVIC_InitStruct.NVIC_IRQChannel = USART3_IRQn;
	NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 5;
	NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStruct);
	NVIC_SetPriority(USART3_IRQn, 5);
}

static void esp_at_lowlevel_init(void)
{
	esp_at_usart_init();
	esp_at_dma_init();
	esp_at_int_init();
	esp_at_io_init();
}

bool esp_at_init(void)
{
	at_ack_semaphore = xSemaphoreCreateBinary();
	configASSERT(at_ack_semaphore);

	esp_at_lowlevel_init();

	if (!esp_at_wait_boot(5000))
	{
		printf("AT Failed\r\n");
		return false;
	}

	if (!esp_at_write_command("AT+RESTORE\r\n", 5000))
	{
		printf("AT+RESTORE  Failed\r\n");
		return false;
	}

	if (!esp_at_wait_ready(7000))
	{
		printf("get ready Failed\r\n");
		return false;
	}

	printf("ready yes\r\n");
	return true;
}

// 发送命令内容函数（独立成一个小函数）
static void esp_at_usart_write(const char *data)
{
	uint32_t len = strlen(data);
	DMA1_Stream3->M0AR = (uint32_t)data;
	DMA1_Stream3->NDTR = len;

	DMA_ClearFlag(DMA1_Stream3, DMA_FLAG_TCIF3);
	DMA_Cmd(DMA1_Stream3, ENABLE);
}

// 匹配接收的字符串
static at_ack_t match_internal_ack(const char *str)
{
	for (uint32_t i = 0; i < ARRAY_SIZE(at_ack_matches); i++)
	{
		if (strcmp(str, at_ack_matches[i].string) == 0)
		{
			return at_ack_matches[i].ack;
		}
	}
	return AT_ACK_NONE;
}

// 接收完毕
static at_ack_t esp_at_usart_wait_receive(uint32_t timeout)
{
	rxlen = 0;
	rxline = rxbuf;
	bool acked = xSemaphoreTake(at_ack_semaphore, pdMS_TO_TICKS(timeout)) == pdPASS;
	return acked ? rxack : AT_ACK_NONE;
}

// 等待一个特定的ack，如ready
static bool esp_at_wait_ready(uint32_t timeout)
{
	return esp_at_usart_wait_receive(timeout) == AT_ACK_READY;
}

// 写命令，检查返回值是否OK
static bool esp_at_write_command(const char *command, uint32_t timeout)
{
// 加一点调试信息
#if ESP_AT_DEBUG
	printf("[DEBUG] Send: %s\r\n", command);
#endif
	esp_at_usart_write(command);
	at_ack_t ack = esp_at_usart_wait_receive(timeout);
// 调试信息
#if ESP_AT_DEBUG
	printf("[DEBUG] Response: %s\r\n", rxbuf);
#endif
	return ack == AT_ACK_OK;
}

// 获取外部的一个响应值
static const char *esp_at_get_response(void)
{
	return rxbuf;
}

// 增加一个等待esp32启动完成的函数
static bool esp_at_wait_boot(uint32_t timeout)
{
	for (int t = 0; t < timeout; t += 100)
	{
		if (esp_at_write_command("AT\r\n", 100))
			return true;
	}
	return false;
}

// 连接wifi初始化
bool esp_at_wifi_init(void)
{
	return esp_at_write_command("AT+CWMODE=1\r\n", 2000);
}
// 连接wifi函数
bool esp_at_connect_wifi(const char *ssid, const char *pwd, const char *mac)
{
	if (ssid == NULL || pwd == NULL)
		return false;

	char cmd[128];
	int len = snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd); // ssid为wifi或热点名称，pwd为wifi或热点密码，需替换
	if (mac)
		snprintf(cmd + len, sizeof(cmd) - len, ",\"%s\"", mac);

	return esp_at_write_command(cmd, 15000);
}

// 解析wifi数据
static bool parse_cwstate_response(const char *response, esp_wifi_info_t *info)
{
	// AT+CWSTATE?
	//+CWSTATE:2,"iPhone18promax"

	// OK
	response = strstr(response, "+CWSTATE:");
	if (response == NULL)
		return false;

	int wifi_state;
	if (sscanf(response, "+CWSTATE:%d,\"%31[^\"]\"", &wifi_state, info->ssid) != 2)
		return false;

	info->connected = (wifi_state == 2);

	return true;
}

static bool parse_cwjap_response(const char *response, esp_wifi_info_t *info)
{
	// AT+CWJAP?
	//+CWJAP:"iPhone18promax","c2:09:f2:22:7d:2b",6,-41,0,1,3,0,1

	// OK
	response = strstr(response, "+CWJAP:");
	if (response == NULL)
		return false;

	if (sscanf(response, "+CWJAP:\"%31[^\"]\",\"%17[^\"]\",%d,%d", info->ssid, info->bssid, &info->channel, &info->rssi) != 4)
		return false;

	return true;
}
// 检查wifi是否连接成功
bool esp_at_get_wifi_info(esp_wifi_info_t *info)
{
	if (!esp_at_write_command("AT+CWSTATE?\r\n", 2000)) // 查询esp设备wifi状态和wifi信息
		return false;
	// 解析获取的内容
	if (!parse_cwstate_response(esp_at_get_response(), info))
		return false;
	if (info->connected == true)
	{
		if (!esp_at_write_command("AT+CWJAP?\r\n", 2000)) // 查询与esp32station连接的AP信息
			return false;
		// 解析
		if (!parse_cwjap_response(esp_at_get_response(), info))
			return false;
	}
	return true;
}
// wifi是否连接
bool wifi_is_connected(void)
{
	esp_wifi_info_t info;
	if (esp_at_get_wifi_info(&info))
		return info.connected;
	return false;
}

// 时间获取
bool esp_at_sntp_init(void)
{
	if (!esp_at_write_command("AT+CIPSNTPCFG=1,8\r\n", 2000))
		return false;
	return true;
}
// 月份字符串转数字（Jan=1, Feb=2, ..., Dec=12）
static uint8_t month_str_to_num(const char *month_str)
{
	const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	for (uint8_t i = 0; i < 12; i++)
	{
		if (strcmp(month_str, months[i]) == 0)
			return i + 1;
	}
	return 0; // 无效月份
}

// 星期字符串转数字（Sun=0, Mon=1, ..., Sat=6）
static uint8_t weekday_str_to_num(const char *weekday_str)
{
	const char *weekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
	for (uint8_t i = 0; i < 7; i++)
	{
		if (strcmp(weekday_str, weekdays[i]) == 0)
			return i + 1;
	}

	return 0; // 无效星期
}
static bool parse_cipsntptime_response(const char *response, esp_date_time_t *date)
{
	response = strstr(response, "+CIPSNTPTIME:");
	if (response == NULL)
		return false;
	char weekday_str[4];
	char month_str[4];

	if (sscanf(response, "+CIPSNTPTIME:%3s %3s %hhu %hhu:%hhu:%hhu %hu", weekday_str, month_str, &date->day, &date->hour, &date->minute, &date->second, &date->year) != 7)
		return false;

	date->weekday = weekday_str_to_num(weekday_str);
	date->month = month_str_to_num(month_str);

	return true;
}
// 解析时间
bool esp_at_sntp_get_time(esp_date_time_t *date)
{
	if (!esp_at_write_command("AT+CIPSNTPTIME?\r\n", 2000))
		return false;

	// 解析
	if (!parse_cipsntptime_response(esp_at_get_response(), date))
		return false;

	return true;
}

// 请求网络链接
const char *esp_at_http_get(const char *url)
{
	snprintf(txbuf, sizeof(txbuf), "AT+HTTPCLIENT=2,1,\"%s\",,,2\r\n", url);
	bool ret = esp_at_write_command(txbuf, 5000); // 将请求网络连接的命令发送出去，并延时5s
	return ret ? esp_at_get_response() : NULL;
}

// USART的中断函数
void USART3_IRQHandler(void)
{
	if (USART_GetITStatus(USART3, USART_IT_RXNE) == SET)
	{
		if (rxlen < (sizeof(rxbuf) - 1))
		{
			rxbuf[rxlen++] = USART_ReceiveData(USART3);

			if (rxbuf[rxlen - 1] == '\n')
			{
				rxbuf[rxlen] = '\0';
				at_ack_t ack = match_internal_ack(rxline);

				if (ack != AT_ACK_NONE)
				{
					rxack = ack;
					BaseType_t pxHigherPriorityTaskWoken;
					xSemaphoreGiveFromISR(at_ack_semaphore, &pxHigherPriorityTaskWoken);
					portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
				}
				rxline = rxbuf + rxlen; // 若与OK或ERROR匹配不上，则找下一句
			}
		}
	}
	USART_ClearITPendingBit(USART3, USART_IT_RXNE);
}
