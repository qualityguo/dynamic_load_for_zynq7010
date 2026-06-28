/***************************** Include Files *********************************/
#include "ssbl.h"

/************************** Constant Definitions *****************************/
#define MAGIC_WORD  0xDEADBEEFu											// 串口触发Shell魔数
#define	UART_BUF_LEN	128


/************************** Function Prototypes ******************************/
static void key0_cb(struct KEY_Device *dev, uint32_t event);
static void uart1_cb(struct UART_Device *dev, uint32_t event);

/************************** Variable Definitions *****************************/
extern	TX_EVENT_FLAGS_GROUP	trigger_event_group;					// Trigger任务的检测触发
static	uint8_t uart_buf[UART_BUF_LEN];


/*
*********************************************************************************************************
*	函 数 名: AppTaskTrig
*	功能说明: 监听串口和按键-触发后发TRIGGER_FLAG_PROCESSED
*	形    参: thread_input 是在创建该任务时传递的形参
*	返 回 值: 无
	优 先 级: 10
*********************************************************************************************************
*/
void  AppTaskTrig (ULONG thread_input)
{
	(void)thread_input;
	UINT status;
	int act_len = 0;
	uint8_t	key_value;										// 按键值
	uint32_t magic_window = 0;								// UART魔术窗
	static  ULONG 					event_flags_value;		/* 事件标志暂存 */
	struct KEY_Device *pKEYDev0 = NULL;
	struct UART_Device *pUARTDev1 = NULL;

	// 1. 注册 KEY 和 UART 回调
	pKEYDev0 = Get_KEY_Device("zynq_key0");
	if(pKEYDev0 == NULL)
	{
		ssbl_printf(LOG_ERR, "key0 Get_KEY_Device error.\r\n");
	}
	key_Register_Notify(pKEYDev0, key0_cb);

	pUARTDev1 = Get_UART_Device("zynq_uart1");
	if(pUARTDev1 == NULL)
	{
		ssbl_printf(LOG_ERR, "uart1 Get_UART_Device error.\r\n");
	}
	uart_Register_Notify(pUARTDev1, uart1_cb);

	// 2. 死等原始事件
	while(1)
	{
		status = tx_event_flags_get(&trigger_event_group,
									TRIGGER_FLAG_KEY0|TRIGGER_FLAG_UART1,
									TX_OR_CLEAR, 					// OR+CLEAR
									&event_flags_value,
									TX_WAIT_FOREVER);				// 无限等待

		if(status != TX_SUCCESS)
			continue;

		// 3. 验证
		if(event_flags_value & TRIGGER_FLAG_KEY0)					// KEY0 消抖
		{
			tx_thread_sleep(2);									// 延时20ms-消抖
			key_Read(pKEYDev0, &key_value);
			if(key_value == 1)
			{
				ssbl_printf(LOG_INFO, "Trigger: KEY0 verified.\r\n");
				tx_event_flags_set(&trigger_event_group, TRIGGER_FLAG_PROCESSED, TX_OR);
				tx_thread_terminate(tx_thread_identify());
			}
		}
		else if(event_flags_value & TRIGGER_FLAG_UART1)				// UART 魔数匹配
		{
			act_len = uart_Recv(pUARTDev1, uart_buf, UART_BUF_LEN);
			for(int i = 0; i < act_len; i++)
			{
				magic_window = (magic_window << 8) | (uint32_t)uart_buf[i];
				if(magic_window == MAGIC_WORD)
				{
					magic_window = 0;								/* 复位，下次重新匹配 */
					ssbl_printf(LOG_INFO, "Trigger: UART magic verified.\r\n");
					tx_event_flags_set(&trigger_event_group, TRIGGER_FLAG_PROCESSED, TX_OR);
					tx_thread_terminate(tx_thread_identify());
				}
			}
		}
	}
}


static void key0_cb(struct KEY_Device *dev, uint32_t event)
{
	tx_event_flags_set(&trigger_event_group, TRIGGER_FLAG_KEY0, TX_OR);
}

static void uart1_cb(struct UART_Device *dev, uint32_t event)
{
	tx_event_flags_set(&trigger_event_group, TRIGGER_FLAG_UART1, TX_OR);
}
