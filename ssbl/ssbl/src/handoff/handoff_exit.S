/*
*********************************************************************************************************
*
*	模块名称 : SSBL → app 跳转汇编（spec §13.4，Phase 5 / Task 5.6）
*	文件名称 : handoff_exit.S
*	版    本 : V1.0
*	说    明 : 纯破坏性清理后 bx 到 app 入口。
*	           入口：r0 = app entry address（= load_addr，payload[0] 即 reset 向量，
*	           header 已剥离、不在 DDR，故不加 0x20）。
*	           GIC / Private Timer 的停机由 C 侧 handoff.c（jump_to_app）提前完成，
*	           本汇编只做 cache/MMU 相关清理 + 跳转。
*
*	注意：r0 = entry_addr 是入口参数，下面 bl 的 C 函数按 AAPCS 会破坏 r0-r3
*	      （caller-saved），必须先把 entry_addr 存入 callee-saved 的 r4，最后 bx r4，
*	      否则 bx r0 会跳到被破坏的垃圾地址立即 fault。
*
*********************************************************************************************************
*/

    .section .text.handoff_exit
    .arm
    .global handoff_exit
    .type   handoff_exit, %function

handoff_exit:

    /* 1. 禁所有中断（IRQ/FIQ/Async fault）*/
    cpsid   ifa
    mov     r4, r0              /* 保存 app 入口到 callee-saved r4 */

    /* 2. Flush D-cache（clean + invalidate，写回 DDR）：
     *    app 代码经 memcpy 写入走 D-cache，必须写回 DDR 才能被取指拿到。*/
    bl      Xil_DCacheFlush

    /* 3. Flush L2 (PL310)：clean + invalidate 一次 */
    bl      Xil_L2CacheFlush

    /* 4. Invalidate I-cache + BTB：强制从 DDR 重取最新 app 指令 */
    bl      Xil_ICacheInvalidate

    /* 5. Disable I-cache / D-cache：清 SCTLR 的 I(C12) 和 C(C2) 位。
     *    0x1004 不是合法的 ARM modified-immediate（两位相距 10，无法编码成单个
     *    rotated byte），故拆成两条单 bit 的 bic（每条都可编码）。*/
    mrc     p15, 0, r1, c1, c0, 0
    bic     r1, r1, #(1<<12)        /* I-cache */
    bic     r1, r1, #(1<<2)         /* D-cache */
    mcr     p15, 0, r1, c1, c0, 0
    isb

    /* 6. Disable MMU（必须在 cache 操作之后）：清 SCTLR 的 M 位 */
    mrc     p15, 0, r1, c1, c0, 0
    bic     r1, r1, #1
    mcr     p15, 0, r1, c1, c0, 0
    isb

    /* 7. invalidate TLB 再一次 */
    mov     r2, #0
    mcr     p15, 0, r2, c8, c7, 0       /* TLBIALL */
    dsb
    isb

    /* 8. 跳转到 app 入口（app 的 _vector_table[0] = reset = load_addr 本身）*/
    bx      r4

#if 0   /* ===== 相干跳转（仅 cache 维护，保留 MMU/cache 使能）——备用回退方案 =====
     * 若完整 teardown 在某些 app 上导致环境破坏跑不起来，可启用此版：
     * 它只做必要的 cache 维护（flush D+L2、invalidate I），不关 cache / 不关 MMU，
     * 让 app 在 SSBL 现有映射下直接跑。对 init_platform 为空操作的 PS app 同样可用。
     *    cpsid   ifa
     *    mov     r4, r0
     *    bl      Xil_DCacheFlush
     *    bl      Xil_L2CacheFlush
     *    bl      Xil_ICacheInvalidate
     *    dsb
     *    isb
     *    bx      r4                                                            */
#endif

    .size   handoff_exit, .-handoff_exit
