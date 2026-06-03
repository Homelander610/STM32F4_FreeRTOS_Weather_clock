#ifndef __BOARD_H__
#define __BOARD_H__

#include <stdio.h>

void board_lowlevel_init(void);
void board_init(void);
int fputc(int ch, FILE *f);

#endif /*__BOARD_H__*/
