#ifndef TASK_METER_H
#define TASK_METER_H

#include "FreeRTOS.h"
#include "task.h"

/*
 * Meter task -- owns TCS meter scanning/polling, the YC1021 BLE Lx
 * command dispatch, and cmd.c's shared state (see the approved
 * FreeRTOS-port plan, Phase E). Structurally near-identical to the old
 * bare-metal main() loop body: tcs_init()/yc1021_init()/cmd_init() once,
 * then tcs_poll()/yc1021_poll()/cmd_reset_pending() every iteration --
 * none of those functions block internally, so they slot into a task
 * loop unchanged.
 */
void vTaskMeter(void *pv);

#endif
