/***************************************************************************
 * Copyright (c) 2024 Microsoft Corporation 
 * 
 * This program and the accompanying materials are made available under the
 * terms of the MIT License which is available at
 * https://opensource.org/licenses/MIT.
 * 
 * SPDX-License-Identifier: MIT
 **************************************************************************/


/**************************************************************************/
/**************************************************************************/
/**                                                                       */
/** ThreadX Component                                                     */
/**                                                                       */
/**   Initialize                                                          */
/**                                                                       */
/**************************************************************************/
/**************************************************************************/
#ifdef TX_INCLUDE_USER_DEFINE_FILE
#include "tx_user.h"
#endif

    .arm

SVC_MODE        =       0xD3
IRQ_MODE        =       0xD2
FIQ_MODE        =       0xD1
SYS_MODE        =       0xDF


    .global     _tx_thread_system_stack_ptr
    .global     _tx_initialize_unused_memory
    .global     _tx_thread_context_save
    .global     _tx_thread_context_restore
    .global     _tx_timer_interrupt
    .global     _end

    .global     __vectors
    .global     disableHighVecs
    .global		tx_irq_dispatch

    .text
    .align 2
    .thumb
    .global $_tx_initialize_low_level
    .type   $_tx_initialize_low_level,function
$_tx_initialize_low_level:
     BX        pc
     NOP
    .arm
     STMFD     sp!, {lr}
     BL        _tx_initialize_low_level
     LDMFD     sp!, {lr}
     BX        lr

    .text
    .align 2
    .global _tx_initialize_low_level
    .type   _tx_initialize_low_level,function
_tx_initialize_low_level:

	MOV		r12, lr                     // save lr before mode switch

    // Switch SYS -> SVC (SYS has no SPSR)
    MOV     r0, #SVC_MODE
    MSR     CPSR_c, r0

    // Save SVC sp as ThreadX system stack
    LDR     r2, =_tx_thread_system_stack_ptr
    STR     sp, [r2]

    // Save unused memory pointer (_end + 8)
    LDR     r1, =_end
    LDR     r2, =_tx_initialize_unused_memory
    ADD     r1, r1, #8
    STR     r1, [r2]

    PUSH	{r12}                       // push saved lr

    // Set VBAR to ThreadX vector table
    LDR     r0, =__vectors
    MCR     p15, 0, r0, c12, c0, 0
    BL      disableHighVecs

    // GIC / Timer init has been moved to bsp_init()

    POP    {lr}
#ifdef __THUMB_INTERWORK
    BX      lr
#else
    MOV     pc, lr
#endif

    .global __tx_undefined
__tx_undefined:
    B       __tx_undefined

    .global __tx_swi_interrupt
__tx_swi_interrupt:
    B       __tx_swi_interrupt

    .global __tx_prefetch_handler
__tx_prefetch_handler:
    B       __tx_prefetch_handler

    .global __tx_abort_handler
__tx_abort_handler:
    B       __tx_abort_handler

    .global __tx_reserved_handler
__tx_reserved_handler:
    B       __tx_reserved_handler

// ============================================================
// IRQ Handler
// ============================================================
    .global __tx_irq_handler
    .global __tx_irq_processing_return
__tx_irq_handler:
    B       _tx_thread_context_save
__tx_irq_processing_return:

    PUSH    {r4, r5}                    // r5 for 8-byte alignment
    BL      readIntAck
    MOV     r4, r0

    CMP     r0, #29                     // Private Timer?
    BNE     by_pass_timer_interrupt

    BL      clear_private_timer_irq
    DSB
    BL     _tx_timer_interrupt
    B	   after_irq_dispatch

by_pass_timer_interrupt:
    MOV     r0, r4
    BL		tx_irq_dispatch

after_irq_dispatch:
    MOV		r0, r4
    BL      writeEOI
    POP     {r4, r5}
    B       _tx_thread_context_restore


#ifdef TX_ENABLE_FIQ_SUPPORT
    .global  __tx_fiq_handler
    .global  __tx_fiq_processing_return
__tx_fiq_handler:
    B       _tx_thread_fiq_context_save
__tx_fiq_processing_return:
#ifdef TX_ENABLE_FIQ_NESTING
    BL      _tx_thread_fiq_nesting_start
#endif
#ifdef TX_ENABLE_FIQ_NESTING
    BL      _tx_thread_fiq_nesting_end
#endif
    B       _tx_thread_fiq_context_restore

#else
    .global  __tx_fiq_handler
__tx_fiq_handler:
    B       __tx_fiq_handler
#endif


BUILD_OPTIONS:
    .word  _tx_build_options
VERSION_ID:
    .word  _tx_version_id
