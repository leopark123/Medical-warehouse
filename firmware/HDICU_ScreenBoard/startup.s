/**
 * Minimal startup for GD32F303RCT6 / STM32F303RCT6
 * Sets up stack, zeroes BSS, copies data, calls main.
 */
    .syntax unified
    .cpu cortex-m4
    .thumb

    .section .isr_vector, "a"
    .global g_pfnVectors
g_pfnVectors:
    .word _estack               /* 0: Initial SP */
    .word Reset_Handler         /* 1: Reset */
    .word Default_Handler       /* 2: NMI */
    .word Default_Handler       /* 3: HardFault */
    .word Default_Handler       /* 4: MemManage */
    .word Default_Handler       /* 5: BusFault */
    .word Default_Handler       /* 6: UsageFault */
    .word 0, 0, 0, 0            /* 7-10: Reserved */
    .word Default_Handler       /* 11: SVCall */
    .word Default_Handler       /* 12: DebugMon */
    .word 0                     /* 13: Reserved */
    .word Default_Handler       /* 14: PendSV */
    .word SysTick_Handler       /* 15: SysTick */

    /* IRQ 0-36: Default */
    .rept 37
    .word Default_Handler
    .endr
    /* IRQ 37: USART1 (GD32: USART0) */
    .word USART1_IRQHandler
    /* IRQ 38: USART2 (GD32: USART1) */
    .word USART2_IRQHandler
    /* IRQ 39-67: Default (fill remaining) */
    .rept 29
    .word Default_Handler
    .endr

    .text
    .thumb_func
    .global Reset_Handler
Reset_Handler:
    /* Copy .data from Flash to RAM */
    ldr r0, =_sdata
    ldr r1, =_edata
    ldr r2, =_sidata
copy_data:
    cmp r0, r1
    bge zero_bss
    ldr r3, [r2], #4
    str r3, [r0], #4
    b copy_data

    /* Zero .bss */
zero_bss:
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
zero_loop:
    cmp r0, r1
    bge call_main
    str r2, [r0], #4
    b zero_loop

call_main:
    bl main
    b .

    .thumb_func
    .weak Default_Handler
Default_Handler:
    b .

    /* Weak aliases so main.c can override */
    .weak SysTick_Handler
    .set SysTick_Handler, Default_Handler
    .weak USART1_IRQHandler
    .set USART1_IRQHandler, Default_Handler
    .weak USART2_IRQHandler
    .set USART2_IRQHandler, Default_Handler
