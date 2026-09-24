/*
 * port.c -- fresh FreeRTOS port for GD32F305VCT6 (Cortex-M4, no FPU use).
 * Written from scratch, modeled on FreeRTOS's well-established, publicly
 * documented GCC/ARM_CM3-family port pattern (naked SVC/PendSV/SysTick
 * handlers, BASEPRI-based critical sections) -- NOT adapted from the
 * Renesas rm_freertos_port present elsewhere in this repo, which is
 * FSP/MPU/TrustZone-specific scaffolding around a small real core; that
 * would need surgical stripping and be riskier to verify than writing
 * this well-known minimal version directly (see the approved plan).
 *
 * No CMSIS used anywhere -- raw register addresses only, consistent with
 * this project's src/regs.h style. This project uses zero floating
 * point, so this is the plain (non-"F") port: no FPU context save/
 * restore, no lazy stacking.
 */
#include "FreeRTOS.h"
#include "task.h"

/* Constants required to manipulate the NVIC/SysTick -- standard Cortex-M
 * core peripheral addresses, same on every Cortex-M3/M4 part. */
#define portNVIC_SYSTICK_CTRL_REG           ( *( ( volatile uint32_t * ) 0xe000e010 ) )
#define portNVIC_SYSTICK_LOAD_REG           ( *( ( volatile uint32_t * ) 0xe000e014 ) )
#define portNVIC_SYSTICK_CURRENT_VALUE_REG  ( *( ( volatile uint32_t * ) 0xe000e018 ) )
#define portNVIC_SYSPRI2_REG                ( *( ( volatile uint32_t * ) 0xe000ed20 ) )

#define portNVIC_SYSTICK_CLK_BIT    ( 1UL << 2UL )
#define portNVIC_SYSTICK_INT_BIT    ( 1UL << 1UL )
#define portNVIC_SYSTICK_ENABLE_BIT ( 1UL << 0UL )

#define portNVIC_PENDSV_PRI  ( ( ( uint32_t ) configKERNEL_INTERRUPT_PRIORITY ) << 16UL )
#define portNVIC_SYSTICK_PRI ( ( ( uint32_t ) configKERNEL_INTERRUPT_PRIORITY ) << 24UL )

#define portINITIAL_XPSR ( 0x01000000 )

/* Each task's own interrupt-nesting depth for critical sections. */
static UBaseType_t uxCriticalNesting = 0xaaaaaaaaUL;

static void prvSetupTimerInterrupt( void );

/*
 * A task must never return -- if one somehow does (a bug, not a supported
 * use), land here loudly rather than run off into undefined memory.
 * Matches this project's "fail loud, not silent" approach established
 * elsewhere (configASSERT, the stack-overflow hook).
 */
static void prvTaskExitError( void )
{
    volatile uint32_t ulDummyBreakpointTarget = 0;
    for( ; ; )
    {
        ( void ) ulDummyBreakpointTarget;
    }
}

/*
 * Initialise a task's stack exactly as it would look immediately after a
 * context-switch interrupt has stacked it -- so the first "restore"
 * (vPortSVCHandler, for the very first task, or xPortPendSVHandler
 * afterwards) can pop it uniformly like any other task's saved context.
 */
StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters )
{
    pxTopOfStack--;                                        /* Offset added to account for the way the MCU uses the stack on entry/exit of interrupts. */
    *pxTopOfStack = portINITIAL_XPSR;                       /* xPSR -- Thumb bit must be set. */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) pxCode;                 /* PC -- where the task starts. */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) prvTaskExitError;       /* LR -- if the task function ever returns. */
    pxTopOfStack -= 5;                                      /* R12, R3, R2, R1. */
    *pxTopOfStack = ( StackType_t ) pvParameters;           /* R0 -- the task's parameter. */
    pxTopOfStack -= 8;                                      /* R11, R10, R9, R8, R7, R6, R5, R4. */

    return pxTopOfStack;
}

void vPortEnterCritical( void )
{
    portDISABLE_INTERRUPTS();
    uxCriticalNesting++;
    __asm volatile ( "dsb" ::: "memory" );
    __asm volatile ( "isb" );
}

void vPortExitCritical( void )
{
    configASSERT( uxCriticalNesting );
    uxCriticalNesting--;
    if( uxCriticalNesting == 0 )
    {
        portENABLE_INTERRUPTS();
    }
}

__attribute__( ( naked ) ) uint32_t ulPortRaiseBASEPRI( void )
{
    __asm volatile
    (
        "   mrs r0, basepri                             \n"
        "   mov r1, %0                                  \n"
        "   msr basepri, r1                              \n"
        "   dsb                                          \n"
        "   isb                                          \n"
        "   bx lr                                        \n"
        ::"i" ( configMAX_SYSCALL_INTERRUPT_PRIORITY ) : "r0", "r1", "memory"
    );
}

__attribute__( ( naked ) ) void vPortSetBASEPRI( uint32_t ulNewMaskValue )
{
    ( void ) ulNewMaskValue;
    __asm volatile
    (
        "   msr basepri, r0    \n"
        "   bx lr              \n"
        ::: "memory"
    );
}

/*
 * vPortSVCHandler -- used exactly once, to start the very first task.
 * Loads pxCurrentTCB (set by the scheduler before xPortStartScheduler's
 * final `svc 0`) and restores its initial stack frame, same shape
 * pxPortInitialiseStack built above.
 */
__attribute__( ( naked ) ) void vPortSVCHandler( void )
{
    __asm volatile
    (
        "   ldr r3, pxCurrentTCBConst2  \n" /* pxCurrentTCBConst2 holds the ADDRESS of pxCurrentTCB. */
        "   ldr r1, [r3]                \n" /* r1 = pxCurrentTCB. */
        "   ldr r0, [r1]                \n" /* r0 = *pxCurrentTCB = task's saved stack pointer (first TCB member). */
        "   ldmia r0!, {r4-r11}         \n" /* Pop r4-r11 (not auto-saved/restored by hardware). */
        "   msr psp, r0                 \n" /* Task's stack pointer -> PSP. */
        "   isb                         \n"
        "   mov r0, #0                  \n"
        "   msr basepri, r0             \n" /* Unmask everything -- fresh start. */
        "   orr r14, #0xd               \n" /* Return using PSP, thread mode. */
        "   bx r14                      \n"
        "                               \n"
        "   .align 4                    \n"
        "pxCurrentTCBConst2: .word pxCurrentTCB \n"
    );
}

/*
 * xPortPendSVHandler -- the actual context switch. Runs at the lowest
 * priority (configKERNEL_INTERRUPT_PRIORITY), so it only executes once
 * every real interrupt has finished -- standard Cortex-M FreeRTOS design.
 */
__attribute__( ( naked ) ) void xPortPendSVHandler( void )
{
    __asm volatile
    (
        "   mrs r0, psp                     \n"
        "   isb                             \n"
        "                                   \n"
        "   ldr r3, pxCurrentTCBConst       \n" /* r3 = &pxCurrentTCB. */
        "   ldr r2, [r3]                    \n" /* r2 = pxCurrentTCB. */
        "                                   \n"
        "   stmdb r0!, {r4-r11}             \n" /* Save r4-r11 onto the task's own (PSP) stack. */
        "   str r0, [r2]                    \n" /* Save new top-of-stack into *pxCurrentTCB. */
        "                                   \n"
        "   stmdb sp!, {r3, r14}            \n" /* Save r3, LR on the MSP stack across the C call below. */
        "   mov r0, %0                      \n"
        "   msr basepri, r0                 \n" /* Mask below configMAX_SYSCALL_INTERRUPT_PRIORITY while switching. */
        "   dsb                             \n"
        "   isb                             \n"
        "   bl vTaskSwitchContext           \n" /* Picks the new pxCurrentTCB. */
        "   mov r0, #0                      \n"
        "   msr basepri, r0                 \n"
        "   ldmia sp!, {r3, r14}            \n"
        "                                   \n"
        "   ldr r1, [r3]                    \n" /* r1 = pxCurrentTCB (now the NEW task). */
        "   ldr r0, [r1]                    \n" /* r0 = new task's saved stack pointer. */
        "   ldmia r0!, {r4-r11}             \n" /* Restore r4-r11 from the new task's stack. */
        "   msr psp, r0                     \n"
        "   isb                             \n"
        "   bx r14                          \n"
        "                                   \n"
        "   .align 4                        \n"
        "pxCurrentTCBConst: .word pxCurrentTCB \n"
        ::"i" ( configMAX_SYSCALL_INTERRUPT_PRIORITY )
    );
}

void xPortSysTickHandler( void )
{
    /* SysTick runs at the lowest priority -- when this executes, no
     * higher-priority interrupt can be masked that isn't already, so a
     * plain disable/enable (not save/restore) is correct here, matching
     * the standard Cortex-M FreeRTOS SysTick handler pattern. */
    uint32_t ulPreviousMask = portSET_INTERRUPT_MASK_FROM_ISR();
    {
        if( xTaskIncrementTick() != 0 )
        {
            portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;
        }
    }
    portCLEAR_INTERRUPT_MASK_FROM_ISR( ulPreviousMask );
}

static void prvSetupTimerInterrupt( void )
{
    portNVIC_SYSTICK_CTRL_REG = 0;
    portNVIC_SYSTICK_CURRENT_VALUE_REG = 0;
    portNVIC_SYSTICK_LOAD_REG = ( configCPU_CLOCK_HZ / configTICK_RATE_HZ ) - 1UL;
    portNVIC_SYSTICK_CTRL_REG = ( portNVIC_SYSTICK_CLK_BIT | portNVIC_SYSTICK_INT_BIT | portNVIC_SYSTICK_ENABLE_BIT );
}

/* Top-of-stack symbol from linker/gd32f305vct6.ld (also the vector
 * table's own first word, per startup_gd32f305vct6.s). Referenced
 * directly rather than read back via VTOR at 0xE000ED08 -- VTOR is
 * OPTIONAL in the Cortex-M3/M4 architecture and not every part in the
 * STM32F1/GD32F1-compatible family that this MCU belongs to is confirmed
 * to implement it; guessing wrong there means dereferencing garbage as a
 * pointer, an immediate silent HardFault (Default_Handler is just an
 * infinite loop -- no log output). Using the linker symbol we already
 * know at compile time avoids that uncertainty entirely. */
extern uint32_t _estack;

BaseType_t xPortStartScheduler( void )
{
    /* PendSV and SysTick must be the lowest-priority exceptions -- if a
     * real interrupt could preempt a context switch mid-way, the saved
     * register state would be inconsistent. */
    portNVIC_SYSPRI2_REG |= portNVIC_PENDSV_PRI;
    portNVIC_SYSPRI2_REG |= portNVIC_SYSTICK_PRI;

    prvSetupTimerInterrupt();

    uxCriticalNesting = 0;

    /* Reset MSP to the top of RAM (reclaiming stack used by init code
     * before the scheduler took over) then trigger vPortSVCHandler via
     * `svc 0` to load and jump into the first task. */
    __asm volatile
    (
        " ldr r0, =_estack      \n"
        " msr msp, r0           \n"
        " cpsie i               \n"
        " cpsie f               \n"
        " dsb                   \n"
        " isb                   \n"
        " svc 0                 \n"
        " nop                   \n"
    );

    /* Should never reach here. */
    return 0;
}

void vPortEndScheduler( void )
{
    /* Not supported/needed on this bare-metal target -- tasks never
     * "end" the scheduler and return to main(). */
    configASSERT( 0 );
}
