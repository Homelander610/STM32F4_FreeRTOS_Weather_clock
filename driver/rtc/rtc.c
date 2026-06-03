#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"
#include "FreeRTOS.h"
#include "task.h"
#include "rtc.h"

void rtc_init(void)
{
	RTC_InitTypeDef RTC_InitStructure;
	RTC_StructInit(&RTC_InitStructure);
	RTC_Init(&RTC_InitStructure);

	RCC_RTCCLKCmd(ENABLE);
	RTC_WaitForSynchro();
}
// 确保读出来的时间和写进去的时间一致
static void rtc_set_time_once(const rtc_date_time_t *date_time)
{
	RTC_DateTypeDef date;
	RTC_TimeTypeDef time;
	RTC_DateStructInit(&date);
	RTC_TimeStructInit(&time);

	date.RTC_Year = date_time->year - 2000;
	date.RTC_Month = date_time->month;
	date.RTC_Date = date_time->day;
	date.RTC_WeekDay = date_time->weekday;
	time.RTC_Hours = date_time->hour;
	time.RTC_Minutes = date_time->minute;
	time.RTC_Seconds = date_time->second;

	RTC_SetDate(RTC_Format_BIN, &date);
	RTC_SetTime(RTC_Format_BIN, &time);
}
// 确保取出的时间不会由于跨日、跨月等影响准确值
static void rtc_get_time_once(rtc_date_time_t *date_time)
{
	RTC_DateTypeDef date;
	RTC_TimeTypeDef time;
	RTC_DateStructInit(&date);
	RTC_TimeStructInit(&time);

	RTC_GetTime(RTC_Format_BIN, &time);
	RTC_GetDate(RTC_Format_BIN, &date);

	date_time->year = 2000 + date.RTC_Year;
	date_time->month = date.RTC_Month;
	date_time->day = date.RTC_Date;
	date_time->weekday = date.RTC_WeekDay;
	date_time->hour = time.RTC_Hours;
	date_time->minute = time.RTC_Minutes;
	date_time->second = time.RTC_Seconds;
}
void rtc_set_time(const rtc_date_time_t *date_time)
{
	rtc_date_time_t rtime;
	uint8_t retry_count = 0;
	const uint8_t MAX_RETRY = 3;
	do
	{
		// 进入临界区，禁止任务调度和中断抢占
		taskENTER_CRITICAL();
		rtc_set_time_once(date_time);
		rtc_get_time_once(&rtime);
		// 退出临界区，恢复系统正常调度
		taskEXIT_CRITICAL();

		if (date_time->second == rtime.second)
		{
			break;
		}
		retry_count++;
	} while (retry_count < MAX_RETRY);
}

void rtc_get_time(rtc_date_time_t *date_time)
{
	rtc_date_time_t time1, time2;
	uint8_t retry_count = 0;
	const uint8_t MAX_RETRY = 3;
	do
	{
		// 加入临界区保护，防止获取时间的时候被高优先级任务抢占
		// 进入临界区，禁止任务调度和中断抢占
		taskENTER_CRITICAL();

		rtc_get_time_once(&time1);
		rtc_get_time_once(&time2);

		// 退出临界区，恢复系统正常调度
		taskEXIT_CRITICAL();

		if (memcmp(&time1, &time2, sizeof(rtc_date_time_t)) == 0)
		{
			memcpy(date_time, &time1, sizeof(rtc_date_time_t));
			return;
		}

		retry_count++;
	} while (retry_count < MAX_RETRY);

	// 如果超过最大重试次数依然失败（极小概率硬件异常），直接将最后一次读取的值返回，防止系统卡死
	memcpy(date_time, &time2, sizeof(rtc_date_time_t));
}
