#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "wifi.h"
#include "esp_at.h"
#include "page.h"
#include "rtc.h"

static const uint16_t color_bg_time = mkcolor(248, 248, 248);	// 模块1的背景色
static const uint16_t color_bg_inner = mkcolor(136, 217, 234);	// 模块2的背景色
static const uint16_t color_bg_outdoor = mkcolor(254, 135, 75); // 模块3的背景色

void main_page_display(void)
{
	ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, mkcolor(0, 0, 0));
	// 第一个模块
	do
	{
		ui_fill_color(15, 15, 224, 154, color_bg_time);
		// wifi
		ui_draw_image(23, 20, &image_icon_wifi);
		// ssid
		main_page_redraw_wifi_ssid(WIFI_SSID);
		// 时间
		ui_write_string(25, 42, "--:--", mkcolor(0, 0, 0), color_bg_time, &font76_maple_bold);
		// 日期
		ui_write_string(35, 121, "----/--/-- 星期二", mkcolor(143, 143, 143), color_bg_time, &font20_maple_bold);
	} while (0);

	// 第二个模块
	do
	{
		ui_fill_color(15, 165, 114, 304, color_bg_inner);
		ui_write_string(19, 170, "室内环境", mkcolor(0, 0, 0), color_bg_inner, &font24_maple_bold);
		ui_write_string(86, 190, "C", mkcolor(0, 0, 0), color_bg_inner, &font32_maple_bold);
		ui_write_string(91, 262, "%", mkcolor(0, 0, 0), color_bg_inner, &font32_maple_bold);
		main_page_redraw_inner_temperature(999.9f);
		main_page_redraw_inner_humidity(999.9f);
	} while (0);

	// 第三个模块
	do
	{
		ui_fill_color(125, 165, 224, 304, color_bg_outdoor);
		ui_draw_image(139, 248, &image_wenduji);
		ui_write_string(192, 190, "C", mkcolor(0, 0, 0), color_bg_outdoor, &font32_maple_bold);
		main_page_redraw_outdoor_weather_icon(-1);
		main_page_redraw_outdoor_city("西安");
		main_page_redraw_outdoor_temperature(999.9f);

	} while (0);
}
// 重新渲染ssid的函数
void main_page_redraw_wifi_ssid(const char *ssid)
{
	char str[21];
	snprintf(str, 21, "%20s", ssid);
	ui_write_string(50, 23, str, mkcolor(143, 143, 143), color_bg_time, &font16_maple_bold);
}

// 重新渲染时间的函数
void main_page_redraw_time(rtc_date_time_t *time)
{
	char str[6];
	char comma = (time->second % 2 == 0) ? ':' : ' ';
	snprintf(str, sizeof(str), "%02u%c%02u", time->hour, comma, time->minute);
	ui_write_string(25, 42, str, mkcolor(0, 0, 0), color_bg_time, &font76_maple_bold);
}
// 重新渲染日期的函数
void main_page_redraw_date(rtc_date_time_t *date)
{
	char str[18];
	snprintf(str, sizeof(str), "%04u/%02u/%02u 星期%s", date->year, date->month, date->day,
			 date->weekday == 1 ? "一" : date->weekday == 2 ? "二"
									 : date->weekday == 3	? "三"
									 : date->weekday == 4	? "四"
									 : date->weekday == 5	? "五"
									 : date->weekday == 6	? "六"
									 : date->weekday == 7	? "日"
															: "X");
	ui_write_string(35, 121, str, mkcolor(143, 143, 143), color_bg_time, &font20_maple_bold);
}

// 重新渲染温湿度
void main_page_redraw_inner_temperature(const float temperature)
{
	char str[3] = {'-', '-'};
	if (temperature > -10.0f && temperature <= 100.0f)
		snprintf(str, sizeof(str), "%2.0f", temperature);
	ui_write_string(30, 190, str, mkcolor(0, 0, 0), color_bg_inner, &font54_maple_bold);
}
void main_page_redraw_inner_humidity(const float humidity)
{
	char str[3] = {'-', '-'};
	if (humidity > 0.0f && humidity <= 99.99f)
		snprintf(str, sizeof(str), "%2.0f", humidity);
	ui_write_string(28, 239, str, mkcolor(0, 0, 0), color_bg_inner, &font64_maple_extrabold);
}
// 重新渲染城市
void main_page_redraw_outdoor_city(const char *city)
{
	char str[9];
	snprintf(str, sizeof(str), "%s", city);
	ui_write_string(127, 170, str, mkcolor(0, 0, 0), color_bg_outdoor, &font24_maple_bold);
}
// 重新渲染室外温度
void main_page_redraw_outdoor_temperature(const float temperature)
{
	char str[3] = {'-', '-'};
	if (temperature > -10.0f && temperature <= 100.0f)
		snprintf(str, sizeof(str), "%2.0f", temperature);
	ui_write_string(135, 190, str, mkcolor(0, 0, 0), color_bg_outdoor, &font54_maple_bold);
}
// 更新天气图标
void main_page_redraw_outdoor_weather_icon(const int code)
{
	const image_t *icon;
	if (code == 0 || code == 2 || code == 38)
		icon = &image_qing;
	else if (code == 1 || code == 3)
		icon = &image_yueliang;
	else if (code == 4 || code == 9)
		icon = &image_yintian;
	else if (code == 5 || code == 6 || code == 7 || code == 8)
		icon = &image_duoyun;
	else if (code == 10 || code == 13 || code == 14 || code == 15 || code == 16 || code == 17 || code == 18 || code == 19)
		icon = &image_zhongyu;
	else if (code == 11 || code == 12)
		icon = &image_leizhenyu;
	else if (code == 20 || code == 21 || code == 22 || code == 23 || code == 24 || code == 25)
		icon = &image_zhongxue;
	else
		icon = &image_icon_na;
	ui_draw_image(166, 248, icon);
}
