#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ui.h"
#include "wifi.h"

void wifi_page_display(void)
{
	const char *ssid = WIFI_SSID;
	int ssid_len = strlen(ssid) * font20_maple_bold.size / 2;
	uint16_t ssid_startx = 0;
	if (ssid_len < UI_WIDTH)
	{
		ssid_startx = (UI_WIDTH - ssid_len) / 2;
	}

	const uint16_t color_bg = mkcolor(0, 0, 0);
	ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, color_bg);
	ui_draw_image(30, 15, &image_wifi);
	ui_write_string(88, 191, "WIFI", mkcolor(0, 255, 234), color_bg, &font32_maple_bold);
	ui_write_string(ssid_startx, 233, ssid, mkcolor(255, 255, 255), color_bg, &font20_maple_bold);
	ui_write_string(84, 263, "Á¬½ÓÖÐ", mkcolor(148, 198, 255), color_bg, &font24_maple_bold);
}
