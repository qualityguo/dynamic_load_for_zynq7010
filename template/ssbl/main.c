#include "ssbl.h"
/*
*********************************************************************************************************
*                                 任务优先级，数值越小优先级越高
*********************************************************************************************************
*/
#define  APP_CFG_TASK_START_PRIO                     	2u
#define	 APP_CFG_TASK_TRIG_PRIO							10u
#define  APP_CFG_TASK_SHELL_PRIO						20u

/*
*********************************************************************************************************
*                                    任务栈大小，单位字节
*********************************************************************************************************
*/
#define  APP_CFG_TASK_START_STK_SIZE                   4096u
#define  APP_CFG_TASK_TRIG_STK_SIZE					   4096u
#define  APP_CFG_TASK_SHELL_STK_SIZE				   4096u

/*
*********************************************************************************************************
*                                       静态全局变量
*********************************************************************************************************
*/
static  TX_THREAD   AppTaskStartTCB;
static  uint64_t    AppTaskStartStk[APP_CFG_TASK_START_STK_SIZE/8];
static	TX_THREAD	AppTaskTrigTCB;
static	uint64_t	AppTaskTrigStk[APP_CFG_TASK_TRIG_STK_SIZE/8];
static	TX_THREAD	AppTaskShellTCB;
static  uint64_t	AppTaskShellStk[APP_CFG_TASK_SHELL_STK_SIZE/8];

/*
*********************************************************************************************************
*                                      函数声明
*********************************************************************************************************
*/
static  void  AppTaskStart          (ULONG thread_input);
static  void  AppFileXInit			(void);
extern	void  AppTaskTrig			(ULONG thread_input);
extern	void  AppTaskShell			(ULONG thread_input);
static  void  AppTaskCreate 		(void);
static  void  AppObjCreate 			(void);
/*
*******************************************************************************************************
*                               		变量
*******************************************************************************************************
*/
		uint32_t	media_memory[64 * 1024];							// 给FileX开的动态内存
extern  FX_MEDIA  	g_fx_media;											// 根据不同启动方式确定存储介质
		u32			g_boot_mode;										// 当前启动模式（SD/QSPI）
		boot_cfg_t 				g_runtime_cfg;							// boot.cfg文件
		TX_EVENT_FLAGS_GROUP	trigger_event_group;					// Trigger任务的检测触发
		TX_SEMAPHORE			shell_active_semaphore;					// Shell任务激活信号量

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
	bsp_init();

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
*	功能说明: 启动任务。负责全部初始化编排 + 倒计时 + 自动启动决策。
*	           是 shell_active_semaphore 的唯一 put 方（trigger 只设 flag）。
*	形    参: thread_input 是在创建该任务时传递的形参
*	返 回 值: 无
	优 先 级: 2
*********************************************************************************************************
*/
static  void  AppTaskStart (ULONG thread_input)
{
	(void)thread_input;
	ULONG flags = 0;

	/* 文件系统相关初始化 */
	AppFileXInit();

	/* bit文件加载初始化 */
	bitstream_init();

	/* 创建任务 */
	AppTaskCreate();

	/* 创建任务间通信机制 */
	AppObjCreate();

	/* 倒计时 + 自动启动决策 */
	if(g_runtime_cfg.auto_boot_flag == 1)
	{
		/* 用 event_flags_get 带超时等待 trigger 的 PROCESSED flag。
		 * trigger 验证通过 → 本函数立刻被唤醒（不等满延时）。
		 * 超时 → 自动启动。 */
		UINT rc = tx_event_flags_get(&trigger_event_group,
									 TRIGGER_FLAG_PROCESSED,
									 TX_OR_CLEAR,
									 &flags,
									 g_runtime_cfg.boot_delay_seconds * 100);

		if(rc == TX_SUCCESS)
		{
			/* trigger 验证通过 → 进 CLI */
			ssbl_printf(LOG_INFO, "Trigger fired, entering CLI.\r\n");
			tx_semaphore_put(&shell_active_semaphore);
		}
		else
		{
			/* 超时 → 自动启动 */
			int br = boot_run(g_runtime_cfg.app_name, g_runtime_cfg.bit_name);
			if(br != 0)
			{
				/* 启动失败 → 回退 CLI */
				ssbl_printf(LOG_ERR, "Auto-boot failed (%d), falling back to CLI.\r\n", br);
				tx_semaphore_put(&shell_active_semaphore);
			}
		}
	}
	else
	{
		/* 不自动启动 → 直接进 CLI */
		ssbl_printf(LOG_INFO, "auto_boot=0, entering CLI.\r\n");
		tx_semaphore_put(&shell_active_semaphore);
	}

	/* 编排完成，idle */
	while(1)
	{
		tx_thread_sleep(100);
	}
}

/*
*********************************************************************************************************
*	函 数 名: AppFileXInit
*	功能说明: FileX相关初始化
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
static  void  AppFileXInit			(void)
{
	UINT status;
	/* 判断当前启动模式 */
	g_boot_mode = Xil_In32(BOOT_MODE_REG) & BOOT_MODES_MASK;

#ifdef XPAR_PS7_QSPI_LINEAR_0_S_AXI_BASEADDR
	/* NOR-FLash初始化 */
	lx_nor_flash_initialize();
#endif
	/* FileX初始化 */
	fx_system_initialize();

	/* 根据BOOT模式挂载存储介质 */
	switch (g_boot_mode) {
		case SD_MODE:
			status =  fx_media_open(&g_fx_media, "SDIO", fx_zynq_sd_driver, 0, media_memory, sizeof(media_memory));
			if (status != FX_SUCCESS)
			{
				ssbl_printf(LOG_ERR, "fx_media_open error.\r\n");
			}
			else
			{
				ssbl_printf(LOG_INFO, "fx_media_open sdio.\r\n");
			}
			break;
#ifdef XPAR_PS7_QSPI_LINEAR_0_S_AXI_BASEADDR
		case QSPI_MODE:
			status =  fx_media_open(&g_fx_media, "QSPI", fx_zynq_qspi_driver, 0, media_memory, sizeof(media_memory));
			if (status != FX_SUCCESS)
			{
				ssbl_printf(LOG_INFO, "fx_media_open failed(0x%lx), formatting...\r\n", (unsigned long)status);
				/* 第一次open失败可能是文件系统并未没有格式化 */
				status =  fx_media_format(&g_fx_media,
										fx_zynq_qspi_driver,
										0,
										(UCHAR *)media_memory,
										sizeof(media_memory),
										"QSPI_DISK",
										1,
										32,
										FX_QSPI_META_SECTORS,
										lx_qspi_get_total_sectors() - FX_QSPI_META_SECTORS,
										512,
										1,
										1,
										1);
				if (status != FX_SUCCESS)
				{
					ssbl_printf(LOG_ERR, "fx_media_format error(0x%lx).\r\n", (unsigned long)status);
					break;
				}
				status =  fx_media_open(&g_fx_media, "QSPI", fx_zynq_qspi_driver, 0, media_memory, sizeof(media_memory));
				if (status != FX_SUCCESS)
				{
					ssbl_printf(LOG_ERR, "fx_media_open error after format.\r\n");
					break;
				}
			}
			else
			{
				ssbl_printf(LOG_INFO, "fx_media_open qspi flash.\r\n");
			}
			break;
#endif
		default:
			// 回退到SD卡版本
//			status =  fx_media_open(&g_fx_media, "SDIO", fx_zynq_sd_driver, 0, media_memory, sizeof(media_memory));
//			if (status != FX_SUCCESS)
//			{
//				ssbl_printf(LOG_ERR, "fx_media_open error.\r\n");
//			}
//			ssbl_printf(LOG_INFO, "fx_media_open sdio.\r\n");
#ifdef XPAR_PS7_QSPI_LINEAR_0_S_AXI_BASEADDR
			// 回退到Flash版本
//			status =  fx_media_open(&g_fx_media, "QSPI", fx_zynq_qspi_driver, 0, media_memory, sizeof(media_memory));
//			if (status != FX_SUCCESS)
//			{
//				ssbl_printf(LOG_INFO, "fx_media_open failed(0x%lx), formatting...\r\n", (unsigned long)status);
//				/* 第一次open失败可能是文件系统并未没有格式化 */
//				status =  fx_media_format(&g_fx_media,
//										fx_zynq_qspi_driver,
//										0,
//										(UCHAR *)media_memory,
//										sizeof(media_memory),
//										"QSPI_DISK",
//										1,
//										32,
//										FX_QSPI_META_SECTORS,
//										lx_qspi_get_total_sectors() - FX_QSPI_META_SECTORS,
//										512,
//										1,
//										1,
//										1);
//				if (status != FX_SUCCESS)
//				{
//					ssbl_printf(LOG_ERR, "fx_media_format error(0x%lx).\r\n", (unsigned long)status);
//					break;
//				}
//				status =  fx_media_open(&g_fx_media, "QSPI", fx_zynq_qspi_driver, 0, media_memory, sizeof(media_memory));
//				if (status != FX_SUCCESS)
//				{
//					ssbl_printf(LOG_ERR, "fx_media_open error after format.\r\n");
//					break;
//				}
//			}
//			ssbl_printf(LOG_INFO, "fx_media_open qspi flash.\r\n");
#endif
			ssbl_printf(LOG_ERR, "Unsupported boot mode: 0x%.4lx\r\n", g_boot_mode);
			break;
	}

	/* 读取boot.cfg */
	if(boot_config_load(&g_runtime_cfg) == 0)
		ssbl_printf(LOG_INFO, "read boot.cfg success\r\n");
}


/*
*********************************************************************************************************
*	函 数 名: AppTaskCreate
*	功能说明: 创建应用任务
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
static  void  AppTaskCreate (void)
{
	/**************创建检测触发任务*********************/
	tx_thread_create(&AppTaskTrigTCB,	              	/* 任务控制块地址 */
					  "App Task Trigger",		        /* 任务名 */
					  AppTaskTrig,                 		/* 启动任务函数地址 */
					  0,                             	/* 传递给任务的参数 */
					  &AppTaskTrigStk[0],		        /* 堆栈基地址 */
					  APP_CFG_TASK_TRIG_STK_SIZE, 		/* 堆栈空间大小 */
					  APP_CFG_TASK_TRIG_PRIO,     		/* 任务优先级*/
					  APP_CFG_TASK_TRIG_PRIO,    	 	/* 任务抢占阀值 */
					  TX_NO_TIME_SLICE,               	/* 不开启时间片 */
					  TX_AUTO_START);                 	/* 创建后立即启动 */
	/**************创建Shell任务*********************/
	tx_thread_create(&AppTaskShellTCB,	              	/* 任务控制块地址 */
					  "App Task Shell",			        /* 任务名 */
					  AppTaskShell,               		/* 启动任务函数地址 */
					  0,                             	/* 传递给任务的参数 */
					  &AppTaskShellStk[0],		        /* 堆栈基地址 */
					  APP_CFG_TASK_SHELL_STK_SIZE, 		/* 堆栈空间大小 */
					  APP_CFG_TASK_SHELL_PRIO,     		/* 任务优先级*/
					  APP_CFG_TASK_SHELL_PRIO,    	 	/* 任务抢占阀值 */
					  TX_NO_TIME_SLICE,               	/* 不开启时间片 */
					  TX_AUTO_START);                 	/* 创建后立即启动 */
}


/*
*********************************************************************************************************
*	函 数 名: AppObjCreate
*	功能说明: 创建任务通讯
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
static  void  AppObjCreate (void)
{
	/* 创建事件标志组 */
	tx_event_flags_create(&trigger_event_group, "trigger_event_group");
	/* 创建信号量 */
	tx_semaphore_create(&shell_active_semaphore, "shell_sem", 0);		// 初始0
}




