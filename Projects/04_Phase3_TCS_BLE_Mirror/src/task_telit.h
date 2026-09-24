#ifndef TASK_TELIT_H
#define TASK_TELIT_H

/*
 * Telit task -- owns the EC25 4G modem's AT handshake (Phase F of the
 * FreeRTOS port, see the approved plan). GPS will be added here later
 * via the modem's own AT+QGPS* commands (per the user's decision that
 * this board has no separate GPS module) -- deliberately NOT part of
 * this first migration step, to isolate "did moving EC25 into a task
 * break anything" from "does GPS AT handling work".
 */
void vTaskTelit(void *pv);

#endif
