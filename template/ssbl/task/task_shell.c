/***************************** Include Files *********************************/
#include "ssbl.h"

/************************** Constant Definitions *****************************/


/************************** Function Prototypes ******************************/
static signed char myShellRead(char *c);
static void myShellWrite(const char c);

/************************** Variable Definitions *****************************/
SHELL_TypeDef	shell;
static struct 	UART_Device *pUARTDev1 = NULL;
extern	TX_SEMAPHORE			shell_active_semaphore;					// Shell任务激活信号量

/*
*********************************************************************************************************
*	函 数 名: AppTaskShell
*	功能说明: shell循环
*	形    参: thread_input 是在创建该任务时传递的形参
*	返 回 值: 无
	优 先 级: 20
*********************************************************************************************************
*/
void  AppTaskShell			(ULONG thread_input)
{
	(void)thread_input;
	UINT status;

	// 1.  串口重注册回调
	pUARTDev1 = Get_UART_Device("zynq_uart1");
	if(pUARTDev1 == NULL)
	{
		ssbl_printf(LOG_ERR, "uart1 Get_UART_Device error.\r\n");
	}

	// 2. 初始化shell组件
	shell.read = myShellRead;
	shell.write = myShellWrite;
	shellInit(&shell);

	// 3. 等待任务激活
	status = tx_semaphore_get(&shell_active_semaphore, TX_WAIT_FOREVER);
	if(status == TX_SUCCESS)
	{
		ssbl_printf(LOG_INFO, "Shell task activated!\r\n");
	}

	uart_Register_Notify(pUARTDev1, NULL);			// 回调必须放在这，否则会影响trigger任务

	// 4. 启动shell轮询
	while(1)
	{
		shellTask(&shell);							// TODO： 改轮询为信号驱动
	}

}

static signed char myShellRead(char *c)			// 轮询地取
{
	if(uart_Recv(pUARTDev1, (uint8_t*)c, 1) == 1)			// 取到一个数
	{
		return 0;
	}
	else
	{
		return -1;
	}

}

static void myShellWrite(const char c)
{
	uart_Send(pUARTDev1, (uint8_t*)&c, 1);
}
