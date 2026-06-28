/***************************** Include Files *********************************/
#include "handoff.h"
#include "ssbl.h"

/************************** Constant Definitions *****************************/
#define GTIMER_CONTROL      (*(volatile u32 *)0xF8F00208)

/************************** Function Prototypes ******************************/
/* xil_cache.c 中有实现但 xil_cache.h 未声明（BSP 版本差异） */
extern void Xil_L2CacheFlush(void);
/* 汇编：cache/MMU 清理 + bx 跳转，不返回 */
extern void handoff_exit(uint32_t entry_addr) __attribute__((noreturn));

/************************** Variable Definitions *****************************/
extern	XScuGic 	xInterruptController;
extern	XScuTimer 	xScuTimer;


void jump_to_app(uint32_t app_load_addr)
{
	// 打印跳转信息
	ssbl_printf(LOG_INFO, "cleanup done, jumping to 0x%08X\r\n", (unsigned)app_load_addr);

	// 用户添加的清理信息
	// 关键-中断屏蔽-此后就不会再调度了
	__asm__ volatile ("cpsid ifa" ::: "memory");

	// bsp_init逆初始化
	// 关GIC
	XScuGic_Stop(&xInterruptController);
	// 关SCU
	XScuTimer_DisableInterrupt(&xScuTimer);     /* 关 timer IRQ 输出 */
	XScuTimer_Stop(&xScuTimer);                 /* 停计数 */
	// 关GTC
	GTIMER_CONTROL    = 0x00;					// 停
	// 关UART
	// 关GPIO

	// 关键-Cache一致性
	Xil_DCacheDisable();        /* flush L1 D-cache → L2，再关 */
	Xil_L2CacheFlush();         /* flush L2 → DDR */
	Xil_ICacheDisable();        /* invalidate + 关 I-cache */

	// 跳转
	handoff_exit(app_load_addr);
}
