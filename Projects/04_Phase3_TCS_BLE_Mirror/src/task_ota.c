#include "task_ota.h"
#include "FreeRTOS.h"
#include "task.h"

void vTaskOta(void *pv)
{
    (void)pv;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
