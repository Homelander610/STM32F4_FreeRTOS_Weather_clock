#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"
#include "board.h"
#include "page.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "workqueue.h"
#include "timers.h"
#include "esp_at.h"
#include "rtc.h"
#include "weather.h"
#include "aht20.h"
#include <math.h>

#define MILLISECONDS(x) (x)
#define SECONDS(x) MILLISECONDS((x) * 1000UL)
#define MINUTES(x) SECONDS((x) * 60)
#define HOURS(x) MINUTES((x) * 60)
#define DAYS(x) HOURS((x) * 24)

#define TIME_SYNC_INTERVAL HOURS(1)
#define WIFI_UPDATE_INTERVAL SECONDS(5)
#define TIME_UPDATE_INTERVAL SECONDS(1)
#define INNER_UPDATE_INTERVAL SECONDS(3)
#define OUTDOOR_UPDATE_INTERVAL MINUTES(1)

static TimerHandle_t time_sync_timer;
static TimerHandle_t wifi_update_timer;
static TimerHandle_t time_update_timer;
static TimerHandle_t inner_update_timer;
static TimerHandle_t outdoor_update_timer;

static void time_sync(void)
{
	uint32_t restart_sync_delay = TIME_SYNC_INTERVAL;
	rtc_date_time_t rtc_date = {0};
	esp_date_time_t esp_date = {0};
	if (!esp_at_sntp_get_time(&esp_date))
	{
		printf("[SNTP] get time failed\r\n");
		restart_sync_delay = SECONDS(1);
		goto err;
	}
	if (esp_date.year < 2000)
	{
		printf("[SNTP] invalid date format\r\n");
		restart_sync_delay = SECONDS(1);
		goto err;
	}
	printf("[SNTP] %04u-%02u-%02u %02u:%02u:%02u (%d)\r\n", esp_date.year, esp_date.month, esp_date.day, esp_date.hour, esp_date.minute, esp_date.second, esp_date.weekday);

	rtc_date.year = esp_date.year;
	rtc_date.month = esp_date.month;
	rtc_date.day = esp_date.day;
	rtc_date.hour = esp_date.hour;
	rtc_date.minute = esp_date.minute;
	rtc_date.second = esp_date.second;
	rtc_date.weekday = esp_date.weekday;
	rtc_set_time(&rtc_date);

err:
	xTimerChangePeriod(time_sync_timer, pdMS_TO_TICKS(restart_sync_delay), 0);
}
static void wifi_update(void)
{
	static esp_wifi_info_t last_info = {0};

	esp_wifi_info_t info = {0};
	if (!esp_at_get_wifi_info(&info))
	{
		printf("[AT] wifi get info failed\r\n");
		return;
	}
	if (memcmp(&info, &last_info, sizeof(esp_wifi_info_t)) == 0) // 与上一次info比较，若一致则不更新wifi，不一致则更新
		return;

	// 只有连接状态不一致时 才触发UI和串口打印
	if (last_info.connected != info.connected)
	{
		if (info.connected)
		{
			printf("[WIFI] connected to %s\r\n", info.ssid);
			printf("[WIFI] SSID: %s, BSSID: %s, Channel: %d, RSSI: %d\r\n", info.ssid, info.bssid, info.channel, info.rssi);
			main_page_redraw_wifi_ssid(info.ssid);
		}
		else
		{
			printf("[WIFI] disconnected from %s\r\n", last_info.ssid);
			main_page_redraw_wifi_ssid("wifi lost");
		}
	}
	// 更新历史信息
	memcpy(&last_info, &info, sizeof(esp_wifi_info_t));
}
static void time_update(void)
{
	static rtc_date_time_t last_date = {0};

	rtc_date_time_t date = {0};
	rtc_get_time(&date);

	if (date.year < 2020)
		return;

	if (memcmp(&date, &last_date, sizeof(rtc_date_time_t)) == 0)
		return;

	memcpy(&last_date, &date, sizeof(rtc_date_time_t));
	printf("[TIME] Updating display: %02d:%02d:%02d\r\n", date.hour, date.minute, date.second);
	main_page_redraw_time(&date);
	main_page_redraw_date(&date);
}
static void inner_update(void)
{
	static float last_temperature, last_humidity;

	if (!aht20_start_measurement())
	{
		printf("[AHT20] start measurement failed\r\n");
		return;
	}
	if (!aht20_wait_for_measurement())
	{
		printf("[AHT20] wait for measurement failed\r\n");
		return;
	}

	float temperature = 0.0f, humidity = 0.0f;

	if (!aht20_read_measurement(&temperature, &humidity))
	{
		printf("[AHT20] read measurement failed\r\n");
		return;
	}

	if ((fabs(last_temperature - temperature) < 0.1f) && (fabs(last_humidity - humidity) < 0.1f))
		return;

	last_temperature = temperature;
	last_humidity = humidity;

	printf("[AHT20] Temperature: %.1f, Humidity: %.1f\r\n", temperature, humidity);
	main_page_redraw_inner_temperature(temperature);
	main_page_redraw_inner_humidity(humidity);
}
static void outdoor_update(void)
{
	static weather_info_t last_weather = {0};
	weather_info_t weather = {0};
	uint32_t next_check_interval = MINUTES(10);

	// 原有的天气请求逻辑保持不变
	const char *weather_url = "https://api.seniverse.com/v3/weather/now.json?key=SEc2YmJCa39zrYSV4&location=WQJ6YY8MHZP0&language=en&unit=c";
	const char *weather_http_response = esp_at_http_get(weather_url);

	if (weather_http_response == NULL)
	{
		printf("[WEATHER] http error\r\n");
		goto end;
	}

	if (!parse_seniverse_response(weather_http_response, &weather))
	{
		printf("[WEATHER] parse failed\r\n");
		goto end;
	}

	if (memcmp(&last_weather, &weather, sizeof(weather_info_t)) == 0)
	{
		goto end;
	}

	memcpy(&last_weather, &weather, sizeof(weather_info_t));
	printf("[WEATHER] %s, %s, %.1f\r\n", weather.city, weather.weather, weather.temperature);

	main_page_redraw_outdoor_temperature(weather.temperature);
	main_page_redraw_outdoor_weather_icon(weather.weather_code);

end:
	xTimerChangePeriod(outdoor_update_timer, pdMS_TO_TICKS(next_check_interval), 0);
}

typedef void (*app_job_t)(void);

static void app_work(void *param)
{
	app_job_t job = (app_job_t)param;
	job();
}

static void work_timer_cb(TimerHandle_t timer)
{
	app_job_t job = (app_job_t)pvTimerGetTimerID(timer);
	workqueue_run(app_work, job);
}
static void app_timer_cb(TimerHandle_t timer)
{
	app_job_t job = (app_job_t)pvTimerGetTimerID(timer);
	job();
}

void app_init(void)
{
	/**
	 * 在 FreeRTOS 中，所有的软件定时器回调，都是在一个叫做 TmrSvc (Timer Service) 的系统守护任务中运行的。这个任务有一个铁律：绝对不能在里面执行任何会引起阻塞（Block）或耗时过长的代码，否则整个系统的所有软件定时器都会瘫痪。
	 */
	// 时间更新只需要去RTC寄存器取值 不需要耗时操作 因此可以直接在系统定时器任务中执行
	time_update_timer = xTimerCreate("time update", pdMS_TO_TICKS(TIME_UPDATE_INTERVAL), pdTRUE, (void *)time_update, app_timer_cb);

	// 其余四个包含AT 串口指令交互、等待 HTTP 响应、等待 I2C 温湿度转换，耗时动辄几百毫秒甚至几秒，且充满了 xSemaphoreTake 或死循环等待 因此交给工作队列
	wifi_update_timer = xTimerCreate("wifi update", pdMS_TO_TICKS(WIFI_UPDATE_INTERVAL), pdTRUE, (void *)wifi_update, work_timer_cb);
	inner_update_timer = xTimerCreate("inner update", pdMS_TO_TICKS(INNER_UPDATE_INTERVAL), pdTRUE, (void *)inner_update, work_timer_cb);

	// 这两个与上面三个也不同。上面三个都是与本地硬件交互，操作稳定可控，耗时固定；而以下二者均需要依赖外部网络和第三方服务器。所以下一次执行的间隔必须是根据本次执行结果动态变化的
	// 所以定时器周期模式设置为->单次定时器/不自动重载
	// 而上面三者的定时器模式设置为->周期定时器/自动重载
	time_sync_timer = xTimerCreate("time sync", pdMS_TO_TICKS(TIME_SYNC_INTERVAL), pdFALSE, (void *)time_sync, work_timer_cb);
	outdoor_update_timer = xTimerCreate("outdoor update", pdMS_TO_TICKS(OUTDOOR_UPDATE_INTERVAL), pdFALSE, (void *)outdoor_update, work_timer_cb);

	xTimerStart(time_update_timer, 0);
	xTimerStart(wifi_update_timer, 0);
	xTimerStart(inner_update_timer, 0);

	workqueue_run(app_work, (void *)time_sync);
	workqueue_run(app_work, (void *)wifi_update);
	workqueue_run(app_work, (void *)inner_update);
	workqueue_run(app_work, (void *)outdoor_update);
}
