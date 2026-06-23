#include "xil_printf.h"
#include "includes.h"
#include "boot_selector.h"
#include "boot_config.h"
#include "cli.h"
#include "cli_uart.h"
#include "cli_trigger.h"
#include "countdown.h"

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
*	全局运行期配置：AppTaskStart 在线程上下文 boot_config_load 写入一次，
*	countdown（auto_boot/boot_delay）与 CLI（status/cfg/boot）共享。
*/
boot_cfg_t g_runtime_cfg;


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
*	功能说明: 启动任务 = SSBL 初始化编排线程（Phase 7 起取代 Phase 6 的直接 boot）。
*	           在线程上下文依次：
*	             1) fx_system_initialize + 挂载 SD P2（FileX 依赖 tx_mutex，必须线程上下文）
*	             2) boot_config_load → g_runtime_cfg（先于创建 countdown，它要读 auto_boot）
*	             3) 创建 CLI 子系统：cli_uart_init / cli_trigger_init / cli_create（DONT_START）
*	             4) countdown_create：auto_boot=0 直接进 CLI（不启动 trigger）；
*	                auto_boot=1 启动 trigger 监控 + 倒计时线程
*	           之后本任务自杀，调度器接管：countdown 决定进 CLI 还是自动 boot。
*	           注意：boot_config_load 不能放 tx_application_define（那是 pre-kernel，
*	           介质还没挂载）。故 Phase 7 的"启动时序"落在 AppTaskStart 而非 plan 原文。
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
		xil_printf("[SSBL] storage_media_open failed (%d), entering CLI\r\n", rc);
		cli_uart_init();
		cli_create();
		cli_activate();          /* 介质不可用也进 CLI（spec §5.3 容错） */
		tx_thread_terminate(tx_thread_identify());
	}
	xil_printf("[SSBL] storage media opened (P2)\r\n");

	/* 1. 先加载 cfg 到全局，countdown 要读 auto_boot / boot_delay */
	boot_config_load(&g_runtime_cfg);

	/* 2. 初始化 CLI 子系统（cli 线程 DONT_START，等被 cli_activate 唤醒） */
	cli_uart_init();
	cli_trigger_init();
	cli_create();

	/* 3. countdown（内部按 auto_boot 决定：auto_boot=0 直接进 CLI，不启动 trigger；
	 *    auto_boot=1 启动 trigger 监控 + 倒计时线程） */
	countdown_create();

	/* 编排完成，本线程自杀 */
	tx_thread_terminate(tx_thread_identify());
}

/***************************** (END OF FILE) *********************************/
