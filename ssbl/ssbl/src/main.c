#include "xil_printf.h"
#include "includes.h"
#include "boot_selector.h"

/*
*********************************************************************************************************
*                                 任务优先级，数值越小优先级越高
*********************************************************************************************************
*/
#define  APP_CFG_TASK_START_PRIO                     	2u

/*
*********************************************************************************************************
*                                    任务栈大小，单位字节
*********************************************************************************************************
*/
#define  APP_CFG_TASK_START_STK_SIZE                   8192u

/*
*********************************************************************************************************
*                                       静态全局变量
*********************************************************************************************************
*/
static  TX_THREAD   AppTaskStartTCB;
static  uint64_t    AppTaskStartStk[APP_CFG_TASK_START_STK_SIZE/8];

/*
*********************************************************************************************************
*                                      函数声明
*********************************************************************************************************
*/
static  void  AppTaskStart          (ULONG thread_input);

/* sd_port_init：pre-kernel 安全（只读 MBR + 注册 driver），main 里调 */
extern  void  sd_port_init          (void);


/*
*********************************************************************************************************
*	函 数 名: main
*	功能说明: 标准c程序入口。
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
int main()
{
	xil_printf("\r\n[SSBL] Hello from SSBL @0x100000\r\n");

	bsp_init();
	board_init();

	/* sd_port_init 只读 MBR + 存指针 + 注册 driver，不碰 FileX/ThreadX API，
	 * 可在 tx_kernel_enter 前调用。fx_system_initialize() 与 fx_media_open()
	 * 都依赖 ThreadX（内部用 tx_mutex），必须在线程上下文里调——放在
	 * AppTaskStart 里（见下），pre-kernel 调会触发 Data Abort。 */
	sd_port_init();

	tx_kernel_enter();

	while(1);
}

/*
*********************************************************************************************************
*	函 数 名: tx_application_define
*	功能说明: ThreadX专用的任务创建，通信组件创建函数
*	形    参: first_unused_memory  未使用的地址空间
*	返 回 值: 无
*********************************************************************************************************
*/
void tx_application_define(void *first_unused_memory)
{
	(void)first_unused_memory;

	/**************创建启动任务*********************/
	tx_thread_create(&AppTaskStartTCB,              /* 任务控制块地址 */
					   "App Task Start",              /* 任务名 */
					   AppTaskStart,                  /* 启动任务函数地址 */
					   0,                             /* 传递给任务的参数 */
					   &AppTaskStartStk[0],            /* 堆栈基地址 */
					   APP_CFG_TASK_START_STK_SIZE,    /* 堆栈空间大小 */
					   APP_CFG_TASK_START_PRIO,        /* 任务优先级*/
					   APP_CFG_TASK_START_PRIO,        /* 任务抢占阀值 */
					   TX_NO_TIME_SLICE,               /* 不开启时间片 */
					   TX_AUTO_START);                 /* 创建后立即启动 */
}

/*
*********************************************************************************************************
*	函 数 名: AppTaskStart
*	功能说明: 启动任务 = boot_selector 驱动线程。
*	           在线程上下文初始化 FileX + 挂载 P2，然后读 boot.cfg → 加载 app/bit
*	           → 跳转。成功路径不返回（已 jump_to_app）。
*	形    参: thread_input 是在创建该任务时传递的形参
*	优 先 级: 2
*********************************************************************************************************
*/
static  void  AppTaskStart (ULONG thread_input)
{
	(void)thread_input;
	int rc;

	/* fx_system_initialize / fx_media_open 必须在线程上下文调用（内部用 tx_mutex），
	 * pre-kernel 调用会让该锁半初始化，后续 fx_media_open 触发 Data Abort。
	 * Phase 4 起从 main 移到此处。 */
	fx_system_initialize();

	rc = storage_media_open();
	if (rc != STORAGE_OK) {
		xil_printf("[SSBL] storage_media_open failed (%d), entering idle\r\n", rc);
		tx_thread_suspend(tx_thread_identify());
	}
	xil_printf("[SSBL] storage media opened (P2)\r\n");

	/* 读 boot.cfg → 加载 app (+bit) → 跳转。成功不返回。 */
	rc = boot_selector_run();

	/* 走到这里说明 boot 失败（cfg 严重错误 / app 不存在 / CRC 错等，spec §5.3）。
	 * Phase 6 先 idle；Phase 7 起此处改为进 CLI。 */
	xil_printf("[SSBL] boot_selector failed (%d), entering idle\r\n", rc);
	tx_thread_suspend(tx_thread_identify());
}

/***************************** (END OF FILE) *********************************/
