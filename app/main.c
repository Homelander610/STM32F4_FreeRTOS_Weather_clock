#include <stdio.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"
#include "board.h"
#include "page.h"
#include "wifi.h"
#include "app.h"
#include "ui.h"
#include "workqueue.h"

static void main_init(void *pararm)
{
	board_init();
	printf("[SYSTEM] Hardware ready, OS taking over...\r\n");

	ui_init();
	printf("[UI] UI task & queue created\r\n");

	welcome_page_display();
	printf("[UI] Welcome page rendered\r\n");

	wifi_init();
	wifi_page_display();
	wifi_wait_connect();

	main_page_display();
	printf("[UI] Main page rendered\r\n");
	app_init();
	printf("[APP] All timers & workqueues started\r\n");

	vTaskDelete(NULL);
}

int main(void)
{
	board_lowlevel_init();
	workqueue_init();

	xTaskCreate(main_init, "init", 1024, NULL, 9, NULL);

	vTaskStartScheduler();

	while (1)
	{
		; // code should not run here
	}
}
