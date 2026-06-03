#ifndef __UI_H__
#define __UI_H__

#include <stdint.h>
#include "font.h"
#include "image.h"

#define UI_WIDTH 240
#define UI_HEIGHT 320

/*红色的8位只需要5位，于是按位与0xF8，舍弃低三位，保留高五位（剩余7-3位），排在16位数的15-11位上，即左移8位*/
/*绿色的8位只需要6位，于是按位与0xFC，舍弃低两位，保留高六位（剩余7-2位），排在16位数的10-5位上，即左移3位*/
/*蓝色的8位只需要5位，于是直接舍弃低三位，即直接右移三位，自然落在了16位数上的4-0位*/
#define mkcolor(r,g,b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >>3))	//将输入8位RGB颜色值转换为16位RGB565格式


void ui_init(void);
void ui_fill_color(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
void ui_write_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color ,const font_t *font);
void ui_draw_image(uint16_t x, uint16_t y, const image_t *image);


#endif /*__UI_H__*/
