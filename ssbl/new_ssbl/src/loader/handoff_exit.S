.section .text.handoff_exit
    .arm
    .global handoff_exit
    .type   handoff_exit, %function

handoff_exit:
    /* 关 MMU（cache 已在 C 侧关掉，这里只清 M 位）*/
    mrc     p15, 0, r1, c1, c0, 0       @SCTLR
    bic     r1, r1, #1                  @M = MMU enable
    mcr     p15, 0, r1, c1, c0, 0
    isb

    /* invalidate TLB */
    mov     r2, #0
    mcr     p15, 0, r2, c8, c7, 0       @TLBIALL
    dsb
    isb

    bx      r0                          @app reset
.Lhang:
    b       .Lhang

    .size   handoff_exit, .-handoff_exit
