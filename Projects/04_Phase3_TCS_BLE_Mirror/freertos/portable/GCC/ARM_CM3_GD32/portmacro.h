/*
 * portmacro.h -- fresh, minimal FreeRTOS port for GD32F305VCT6 (Cortex-M4,
 * no FPU use -- see the approved plan's CPU-target decision). Modeled on
 * FreeRTOS's well-established GCC/ARM_CM3-family port pattern. No CMSIS
 * used -- raw register addresses, consistent with src/regs.h's style.
 * See port.c for the full rationale (not adapted from the Renesas
 * rm_freertos_port -- that's FSP/MPU/TrustZone-specific and doesn't
 * apply to this bare target).
 */
#ifndef PORTMACRO_H
#define PORTMACRO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Type definitions. */
#define portCHAR        char
#define portFLOAT       float
#define portDOUBLE      double
#define portLONG        long
#define portSHORT       short
#define portSTACK_TYPE  uint32_t
#define portBASE_TYPE   long

typedef portSTACK_TYPE StackType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;

#if ( configUSE_16_BIT_TICKS == 1 )
    typedef uint16_t TickType_t;
    #define portMAX_DELAY ( TickType_t ) 0xffff
#else
    typedef uint32_t TickType_t;
    #define portMAX_DELAY ( TickType_t ) 0xffffffffUL
    #define portTICK_TYPE_IS_ATOMIC 1
#endif

/* Architecture specifics. */
#define portSTACK_GROWTH            ( -1 )
#define portTICK_PERIOD_MS          ( ( TickType_t ) 1000 / configTICK_RATE_HZ )
#define portBYTE_ALIGNMENT          8
#define portDONT_DISCARD            __attribute__( ( used ) )

/* Scheduler utilities -- PendSV is used to request a context switch, the
 * same mechanism every Cortex-M FreeRTOS port uses (lowest-priority
 * exception, so it always runs after any real interrupt has finished). */
#define portNVIC_INT_CTRL_REG     ( * ( ( volatile uint32_t * ) 0xe000ed04 ) )
#define portNVIC_PENDSVSET_BIT    ( 1UL << 28UL )

#define portEND_SWITCHING_ISR( xSwitchRequired ) \
    do { if( ( xSwitchRequired ) != 0 ) { portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT; } } while( 0 )
#define portYIELD_FROM_ISR( x )   portEND_SWITCHING_ISR( x )
#define portYIELD() \
    do { \
        portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT; \
        __asm volatile( "dsb" ::: "memory" ); \
        __asm volatile( "isb" ); \
    } while( 0 )

/* Critical section management -- BASEPRI based (masks interrupts at or
 * below configMAX_SYSCALL_INTERRUPT_PRIORITY only, matching
 * FreeRTOSConfig.h), not global cpsid/cpsie -- lets any interrupt above
 * that priority still run during a FreeRTOS critical section. */
extern void vPortEnterCritical( void );
extern void vPortExitCritical( void );
extern uint32_t ulPortRaiseBASEPRI( void );
extern void vPortSetBASEPRI( uint32_t ulNewMaskValue );

#define portSET_INTERRUPT_MASK_FROM_ISR()        ulPortRaiseBASEPRI()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR( x )   vPortSetBASEPRI( x )
#define portDISABLE_INTERRUPTS()                 ulPortRaiseBASEPRI()
#define portENABLE_INTERRUPTS()                  vPortSetBASEPRI( 0 )
#define portENTER_CRITICAL()                     vPortEnterCritical()
#define portEXIT_CRITICAL()                      vPortExitCritical()

#define portNOP()             __asm volatile ( "nop" )
#define portINLINE             inline

#ifndef portFORCE_INLINE
    #define portFORCE_INLINE inline __attribute__( ( always_inline ) )
#endif

#define portTASK_FUNCTION_PROTO( vFunction, pvParameters ) void vFunction( void *pvParameters )
#define portTASK_FUNCTION( vFunction, pvParameters )        void vFunction( void *pvParameters )

#ifdef __cplusplus
}
#endif

#endif /* PORTMACRO_H */
