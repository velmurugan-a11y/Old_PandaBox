/*
 * startup_gd32f305vct6.s -- minimal Cortex-M startup for this native
 * bring-up project. All IRQs are weak-aliased to Default_Handler since
 * Phase 1 is pure polling (no interrupts used yet); add real handlers
 * later the same way startup/startup_stm32f103.s in the sibling
 * pandabox-firmware project does.
 *
 * Vector table length (76 words: 16 core + 60 IRQ) matches the confirmed
 * STM32F103-high-density-compatible layout already established for this
 * MCU family in the sibling project.
 */
    .syntax unified
    .cpu cortex-m4
    .thumb

    .global g_pfnVectors
    .global Reset_Handler
    .global Default_Handler

    .word _estack

    .section .isr_vector,"a",%progbits
    .type g_pfnVectors, %object
    .size g_pfnVectors, .-g_pfnVectors
g_pfnVectors:
    .word _estack
    .word Reset_Handler
    .word Default_Handler /* NMI */
    .word HardFault_Handler
    .word Default_Handler /* MemManage */
    .word Default_Handler /* BusFault */
    .word Default_Handler /* UsageFault */
    .word 0
    .word 0
    .word 0
    .word 0
    .word SVC_Handler
    .word Default_Handler /* DebugMon */
    .word 0
    .word PendSV_Handler
    .word SysTick_Handler
    .rept 52
    .word Default_Handler /* IRQ0..51 -- unused */
    .endr
    .word UART3_IRQHandler /* IRQ52 -- YC1021 UART3 RX, interrupt-driven ring buffer (yc1021.c) */
    .word UART4_IRQHandler /* IRQ53 -- EC25 UART4 RX, interrupt-driven ring buffer (ec25.c) */
    .rept 6
    .word Default_Handler /* IRQ54..59 -- unused */
    .endr

    .section .text.Reset_Handler
    .thumb_func
    .type Reset_Handler, %function
Reset_Handler:
    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata
copy_data_loop:
    cmp r1, r2
    bcs copy_data_done
    ldr r3, [r0], #4
    str r3, [r1], #4
    b copy_data_loop
copy_data_done:

    ldr r1, =_sbss
    ldr r2, =_ebss
    movs r3, #0
zero_bss_loop:
    cmp r1, r2
    bcs zero_bss_done
    str r3, [r1], #4
    b zero_bss_loop
zero_bss_done:

    ldr r0, =main
    blx r0
hang:
    b hang
    .size Reset_Handler, .-Reset_Handler

    .thumb_func
    .type Default_Handler, %function
Default_Handler:
Infinite_Loop:
    b Infinite_Loop
    .size Default_Handler, .-Default_Handler

    /* SysTick/SVC/PendSV are aliased DIRECTLY to FreeRTOS's port
     * functions (Phase C of the FreeRTOS port) -- NOT weak-to-
     * Default_Handler like the still-genuinely-unused IRQ0..59 slots.
     *
     * BUG FIX, found via real-hardware diagnosis: an earlier version of
     * this file kept these three weak-aliased to Default_Handler,
     * expecting port.c's vPortSVCHandler/xPortPendSVHandler/
     * xPortSysTickHandler to "override" them the same way HardFault below
     * does. That only works when the override uses the EXACT SAME symbol
     * name (which HardFault_Handler does, hence it correctly picks up
     * fault.c's real definition) -- vPortSVCHandler is a DIFFERENT name
     * from SVC_Handler, so the weak alias was never overridden, and
     * `svc 0` in xPortStartScheduler() silently vectored into
     * Default_Handler's infinite loop with zero diagnostic output. Fixed
     * by aliasing directly to the port's real (differently-named)
     * functions instead of to Default_Handler. */
    .thumb_set SysTick_Handler, xPortSysTickHandler
    .thumb_set SVC_Handler, vPortSVCHandler
    .thumb_set PendSV_Handler, xPortPendSVHandler

    .weak HardFault_Handler
    .thumb_set HardFault_Handler, Default_Handler

    .weak UART4_IRQHandler
    .thumb_set UART4_IRQHandler, Default_Handler

    .weak UART3_IRQHandler
    .thumb_set UART3_IRQHandler, Default_Handler
