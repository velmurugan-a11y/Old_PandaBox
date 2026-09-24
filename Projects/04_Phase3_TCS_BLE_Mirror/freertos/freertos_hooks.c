/*
 * freertos_hooks.c -- application hooks FreeRTOS calls into. Stack
 * overflow and malloc failure are made loud (log + spin) rather than
 * silently corrupting state or hanging invisibly, matching this
 * project's approach after this session's real-hardware incidents
 * (see yc1021.c's header comment and cmd.c's confidence-flagging style).
 */
#include "FreeRTOS.h"
#include "task.h"
#include "log.h"
#include <stdio.h>

void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
    char msg[64];
    ( void ) xTask;
    snprintf( msg, sizeof( msg ), "FreeRTOS: STACK OVERFLOW in task '%s'\r\n", pcTaskName );
    log_line( msg );
    for( ; ; ) { }
}

void vApplicationMallocFailedHook( void )
{
    log_line( "FreeRTOS: MALLOC FAILED (heap_4 exhausted)\r\n" );
    for( ; ; ) { }
}

void freertos_assert_fail( const char *file, int line )
{
    char msg[100];
    snprintf( msg, sizeof( msg ), "FreeRTOS: ASSERT FAILED at %s:%d\r\n", file, line );
    log_line( msg );
    for( ; ; ) { }
}

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
/* Mandatory once static allocation is enabled -- provides the idle
 * task's TCB + stack memory. */
static StaticTask_t xIdleTaskTCB;
static StackType_t  xIdleTaskStack[ configMINIMAL_STACK_SIZE ];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                     StackType_t **ppxIdleTaskStackBuffer,
                                     uint32_t *pulIdleTaskStackSize )
{
    *ppxIdleTaskTCBBuffer   = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = xIdleTaskStack;
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}
#endif
