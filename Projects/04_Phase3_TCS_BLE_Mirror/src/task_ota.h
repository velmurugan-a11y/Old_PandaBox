#ifndef TASK_OTA_H
#define TASK_OTA_H

/*
 * OTA task -- Phase G of the FreeRTOS port (see the approved plan).
 * Deliberately a TRUE placeholder: this board has no confirmed external
 * SPI flash for OTA image staging (user-confirmed this session), so
 * there is no real OTA logic to run yet. Exists only so the task
 * structure matches the reference architecture's 4-task shape and so
 * the priority slot is reserved; grows real logic only once external
 * flash (or an internal-flash-based staging scheme) is decided.
 */
void vTaskOta(void *pv);

#endif
