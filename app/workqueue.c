#include <stdint.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "workqueue.h"

typedef struct
{
    work_t work;
    void *param;
} work_message_t;

static QueueHandle_t work_msg_queue;

static void work_func(void *param)
{
    work_message_t msg;
    while (1)
    {
        xQueueReceive(work_msg_queue, &msg, portMAX_DELAY);
        msg.work(msg.param);
    }
}

void workqueue_init(void)
{
    work_msg_queue = xQueueCreate(16, sizeof(work_message_t));
    configASSERT(work_msg_queue);
    xTaskCreate(work_func, "workqueue", 1024, NULL, 5, NULL);
}

void workqueue_run(work_t work, void *param)
{
    configASSERT(work_msg_queue); // 防止workqueue_run在workqueue_init之前被调用并往空队列里面发数据
    work_message_t msg = {work, param};
    if (xQueueSend(work_msg_queue, &msg, 0) != pdPASS)
    {
        // 队列满 任务被丢弃 通过串口打印警告
        printf("[WARNING] Workqueue full! Task dropped.\r\n");
    }
}
