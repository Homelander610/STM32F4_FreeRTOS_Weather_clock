#ifndef _ST7789_H_
#define _ST7789_H_

#define ST7789_WIDTH 240
#define ST7789_HEIGHT 320

#include "font.h"
#include "image.h"

void st7789_init(void);
void st7789_fill_color(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
void st7789_write_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color ,const font_t *font);
void st7789_draw_image(uint16_t x, uint16_t y, const image_t *image);


#endif
