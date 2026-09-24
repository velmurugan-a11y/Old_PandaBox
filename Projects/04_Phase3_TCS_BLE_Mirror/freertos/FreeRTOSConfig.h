/*
 * FreeRTOSConfig.h -- hand-written for this project (NOT adapted from the
 * Renesas RA reference project's ra_cfg/aws/FreeRTOSConfig.h, which is
 * full of bsp_api.h/FSP-specific macros that don't apply here). Kept
 * deliberately minimal: only what Phase C's two-task smoke test and the
 * planned Meter/Telit/OTA tasks need. Grow this file only when a real
 * feature needs a specific option, not preemptively.
 *
 * configPRIO_BITS=4 is a hardware fact, not a guess: every STM32F1/
 * GD32F1-3-family Cortex-M3/M4 part implements exactly 4 NVIC priority
 * bits (16 levels) -- this is universal across the whole family this
 * MCU belongs to (see src/regs.h's own header note on STM32F103
 * register/peripheral compatibility).
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                     1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0
#define configUSE_TICKLESS_IDLE                  0
#define configCPU_CLOCK_HZ                       ( 120000000UL ) /* BOARD_SYSCLK_HZ, board_config.h -- Phase 2: HXTAL(12MHz)+PLL x10. MUST be kept in sync with board_config.h's BOARD_SYSCLK_HZ by hand (this file doesn't include board_config.h). */
#define configTICK_RATE_HZ                       ( 1000 )
#define configMAX_PRIORITIES                     ( 5 )
#define configMINIMAL_STACK_SIZE                 ( 128 )  /* words, not bytes -- idle task */
#define configMAX_TASK_NAME_LEN                  ( 16 )
#define configUSE_16_BIT_TICKS                   0
#define configIDLE_SHOULD_YIELD                  1
#define configUSE_TASK_NOTIFICATIONS             1
#define configUSE_MUTEXES                        1
#define configUSE_RECURSIVE_MUTEXES              0
#define configUSE_COUNTING_SEMAPHORES            1
#define configQUEUE_REGISTRY_SIZE                0
#define configUSE_QUEUE_SETS                     0
#define configUSE_TIME_SLICING                   1
#define configUSE_NEWLIB_REENTRANT               0
#define configENABLE_BACKWARD_COMPATIBILITY      0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS  0
#define configUSE_APPLICATION_TASK_TAG           0
#define configUSE_CO_ROUTINES                    0
#define configMAX_CO_ROUTINE_PRIORITIES          ( 2 )

/* Software timers: not needed by anything yet (see the approved plan) --
 * cut to reduce surface area. Revisit only when a real feature needs
 * xTimer*. */
#define configUSE_TIMERS                         0

/* Memory allocation: static for the 3 real tasks (Meter/Telit/OTA) per
 * the approved plan; heap_4 still included for the idle task's
 * kernel-mandated allocation and any future queues/mutexes. */
#define configSUPPORT_STATIC_ALLOCATION           1
#define configSUPPORT_DYNAMIC_ALLOCATION          1
#define configTOTAL_HEAP_SIZE                     ( 4 * 1024 )
#define configAPPLICATION_ALLOCATED_HEAP           0

/* Hooks -- stack overflow / malloc failure must be loud, not silent,
 * given this project's real-hardware-incident history this session
 * (see freertos_hooks.c). */
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configUSE_MALLOC_FAILED_HOOK             1
#define configUSE_DAEMON_TASK_STARTUP_HOOK       0

#define configGENERATE_RUN_TIME_STATS            0
#define configUSE_TRACE_FACILITY                 1  /* needed for uxTaskGetStackHighWaterMark's supporting types */
#define configUSE_STATS_FORMATTING_FUNCTIONS     0

/* Cortex-M interrupt priority setup -- see header comment on
 * configPRIO_BITS above. configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5
 * (out of 15) matches the value used throughout FreeRTOS's own official
 * Cortex-M3/M4 demo projects for this same priority-bit width. */
#define configPRIO_BITS                          4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY  15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* configASSERT: log over RTT/debug UART and spin, so a failed assertion
 * is visible instead of silently hanging -- see freertos_hooks.c. */
extern void freertos_assert_fail(const char *file, int line);
#define configASSERT( x ) if( ( x ) == 0 ) freertos_assert_fail( __FILE__, __LINE__ )

/* Optional API inclusions actually used by this project. */
#define INCLUDE_vTaskPrioritySet             1
#define INCLUDE_uxTaskPriorityGet            1
#define INCLUDE_vTaskDelete                  1
#define INCLUDE_vTaskSuspend                 1
#define INCLUDE_vTaskDelayUntil              1
#define INCLUDE_vTaskDelay                   1
#define INCLUDE_xTaskGetSchedulerState       1
#define INCLUDE_xTaskGetCurrentTaskHandle    1
#define INCLUDE_uxTaskGetStackHighWaterMark  1  /* Phase D's stats task */
#define INCLUDE_eTaskGetState                1
#define INCLUDE_xTaskGetHandle               0
#define INCLUDE_xSemaphoreGetMutexHolder     0
#define INCLUDE_xTaskGetIdleTaskHandle       0
#define INCLUDE_xTaskAbortDelay              0

#endif /* FREERTOS_CONFIG_H */
