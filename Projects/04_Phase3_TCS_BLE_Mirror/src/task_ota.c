#include "task_ota.h"
#include "led_mgr.h"
#include "FreeRTOS.h"
#include "task.h"

/* Placeholder: no real OTA transport yet. When OTA is wired up, call
 * led_mgr_event(LED_EVT_OTA_START) before the transfer and
 * led_mgr_event(LED_EVT_OTA_DONE) on completion. */
void vTaskOta(void *pv)
{
    (void)pv;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
