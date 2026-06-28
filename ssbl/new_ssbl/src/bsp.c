/***************************** Include Files *********************************/
#include "ssbl.h"

/************************** Constant Definitions *****************************/
#define GTIMER_COUNTER_LO   (*(volatile u32 *)0xF8F00200)
#define GTIMER_COUNTER_HI   (*(volatile u32 *)0xF8F00204)
#define GTIMER_CONTROL      (*(volatile u32 *)0xF8F00208)
#define SCU_RELOAD_CNT		(XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ/2/100)			//10ms
#define UART1_CB_BUF_SIZE	2048


/************************** Function Prototypes ******************************/
static 	void 	GIC_Init(void);
static 	void 	Global_Timer_Init(void);
static 	void 	PrivateTimer_Init(void);
static 	void	Gpio_Ps_Init(void);
static  void	Uart_Ps_Init(void);
		void 	tx_irq_dispatch(unsigned int int_id);



/************************** Variable Definitions *****************************/
		XScuTimer 	xScuTimer;
		XScuGic 	xInterruptController;
		XGpioPs 	xGpioPs;
static 	uint8_t		Uart1_Cb_Buf[UART1_CB_BUF_SIZE];
static	CirCularBuffer_t cb0;

void bsp_init()
{
	GIC_Init();										// Xilinx接口的GIC初始化-关键用于ThreadX和BSP的API适配
	Global_Timer_Init();							// 用作时间获取-只读-用途:性能测量
	PrivateTimer_Init();							// tick初始化-不需要通过XScuGic_Connect注册中断服务函数-因为29号中断走快速路径
	Gpio_Ps_Init();									// LED和KEY的初始化
	Uart_Ps_Init();									// UART的初始化
}


static void Global_Timer_Init(void)
{
	GTIMER_CONTROL    = 0x00;						// 停
	GTIMER_COUNTER_LO = 0;
	GTIMER_COUNTER_HI = 0;
	GTIMER_CONTROL    = 0x01;						// 启
}

static void GIC_Init(void)
{
	XScuGic_Config *intc_cfg;
	intc_cfg = XScuGic_LookupConfig(INTC_DEVICE_ID);
	XScuGic_CfgInitialize(&xInterruptController, intc_cfg, intc_cfg->CpuBaseAddress);
}

void tx_irq_dispatch(unsigned int int_id)					// 被汇编调用
{
	extern XScuGic xInterruptController;

	if(int_id < XSCUGIC_MAX_NUM_INTR_INPUTS)
	{
		XScuGic_VectorTableEntry * entry =
			&xInterruptController.Config->HandlerTable[int_id];
		entry->Handler(entry->CallBackRef);
	}
}

static void PrivateTimer_Init(void)
{
	XScuTimer_Config *cfg = XScuTimer_LookupConfig(XPAR_XSCUTIMER_0_DEVICE_ID);
	XScuTimer_CfgInitialize(&xScuTimer, cfg, cfg->BaseAddr);

	XScuTimer_LoadTimer(&xScuTimer, SCU_RELOAD_CNT);
	XScuTimer_EnableAutoReload(&xScuTimer);
	XScuTimer_EnableInterrupt(&xScuTimer);

	XScuGic_SetPriorityTriggerType(&xInterruptController, 29, 0, 0);
	// XScuGic_Connect不必调用-因为中断号29单走一条分支
	XScuGic_Enable(&xInterruptController, 29);		// 使能SCU定时器中断

	XScuTimer_Start(&xScuTimer);
}

static 	void	Gpio_Ps_Init(void)
{
	struct LED_Device *pLEDDev = NULL;
	struct KEY_Device *pKEYDev = NULL;

	// 1. GPIO控制器初始化(LookupConfig + CfgInitialize)---LED和KEY共用同一个控制器
	XGpioPs_Config *cfg = XGpioPs_LookupConfig(XPAR_PS7_GPIO_0_DEVICE_ID);
	XGpioPs_CfgInitialize(&xGpioPs, cfg, cfg->BaseAddr);

	// 2. LED驱动初始化-设备级配置(方向/输出使能)由驱动层完成
	pLEDDev = Get_LED_Device("zynq_led0");
	if(pLEDDev == NULL)
	{
		ssbl_printf(LOG_ERR, "led0 Get_LED_Device error.\r\n");
		return;
	}
	led_Init(pLEDDev);

	pLEDDev = Get_LED_Device("zynq_led1");
	if(pLEDDev == NULL)
	{
		ssbl_printf(LOG_ERR, "led1 Get_LED_Device error.\r\n");
		return;
	}
	led_Init(pLEDDev);

	// 3. KEY驱动初始化-设备级配置(方向/中断类型/中断连接)由驱动层完成
	pKEYDev = Get_KEY_Device("zynq_key0");
	if(pKEYDev == NULL)
	{
		ssbl_printf(LOG_ERR, "key0 Get_KEY_Device error.\r\n");
		return;
	}
	key_Init(pKEYDev);

	pKEYDev = Get_KEY_Device("zynq_key1");
	if(pKEYDev == NULL)
	{
		ssbl_printf(LOG_ERR, "key1 Get_KEY_Device error.\r\n");
		return;
	}
	key_Init(pKEYDev);
}


static  void	Uart_Ps_Init(void)
{
	uint8_t ret = 0;
	int ret1 = 0;
	struct UART_Device *pUARTDev1 = NULL;

	ret = cb_init(&cb0, Uart1_Cb_Buf, UART1_CB_BUF_SIZE);
	if(ret != 1)
	{
		ssbl_printf(LOG_ERR, "uart1 cb_init error.\r\n");
		return;
	}

	pUARTDev1 = Get_UART_Device("zynq_uart1");
	if(pUARTDev1 == NULL)
	{
		ssbl_printf(LOG_ERR, "uart1 Get_UART_Device error.\r\n");
		return;
	}
	ret1 = uart_Register_Cb(pUARTDev1, &cb0);
	if(ret1 != 0)
	{
		ssbl_printf(LOG_ERR, "uart1 uart_Register_Cb error.\r\n");
		return;
	}
	ret1 = uart_Init(pUARTDev1, 115200, 8, 0, 1);
	if(ret1 != 0)
	{
		ssbl_printf(LOG_ERR, "uart1 uart_Init error.\r\n");
		return;
	}

}
