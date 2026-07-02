/***************************** Include Files *********************************/
#include "uart_driver.h"
#include <string.h>
#include <stdbool.h>
#include "ssbl.h"

/************************** Constant Definitions *****************************/
#define UART1_RECV_BUFFER_SIZE	2048						// 对每个外设的驱动层的缓冲区大小进行独立设置

/**************************** Type Definitions *******************************/
struct UART_Device{
	// 名称
	char *name;
	// UART对象指针、波特率、数据位数、有无校验位、停止位数
	int (*init)(struct UART_Device *pDev, int baud, int datas, int parity, char stop);
	// UART对象指针、发送数据、发送长度、等待时间
	int (*send)(struct UART_Device *pDev, uint8_t *data, uint32_t len);
	// UART对象指针、接收数据、接收长度、等待时间
	int (*recv)(struct UART_Device *pDev, uint8_t *data, uint32_t len);
	// 私有数据
	void *priv_data;
	// 注册循环缓冲区
	int (*registercb)(struct UART_Device *pDev, CirCularBuffer_t* cb);
	// 注册回调函数
	int (*registernotify)(struct UART_Device *pDev, uart_device_notify_t notify);
};
struct UART_Drv_Data{
	int init_flag;						// init-flag
	int uart_device_id;					// UART-ID
	int uart_int_irq_id;				// UART-IRQ-ID
	char uart_priority;					// priority
	char uart_trigger;					// trigger
	int rx_buffer_size;					// recv-buffer-size
	int uart_send_err_cnt;				// send err cnt
	int uart_send_success_cnt;			// send success cnt
	uint64_t uart_send_data_cnt;		// send success data cnt
	int uart_recv_err_cnt;				// recv err cnt
	int uart_recv_success_cnt;			// recv success cnt
	uint64_t uart_recv_data_cnt;		// recv success data cnt
	XUartPs_Handler callback;			// Callack
	uart_device_notify_t notify;				// Interrupt user callback
	XScuGic * gic_handle;				// GIC
	XUartPs * uart_handle;				// UART
	uint8_t *rx_buffer_ptr;				// recv-buffer			中断中的缓冲区
	CirCularBuffer_t* rx_cirbuffer_ptr;	// recv-cb				中断和应用的缓冲区
};

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/
void UartPs_Intr_CallBack_Handler(void *CallBackRef, u32 Event, u32 EventData);
int zynq_uart_init(struct UART_Device *pDev, int baud, int datas, int parity, char stop);
int zynq_uart_send(struct UART_Device *pDev, uint8_t *out, uint32_t len);
int zynq_uart_recv(struct UART_Device *pDev, uint8_t *out, uint32_t len);
int zynq_uart_registercb(struct UART_Device *pDev, CirCularBuffer_t* cb);
int zynq_uart_registernotify(struct UART_Device *pDev, uart_device_notify_t notify);


/************************** Variable Definitions *****************************/
// 内核中初始化Timer使用的GIC对象，如果采用另外一个对象初始化UART/DMA，将会导致软件定时器失效
extern XScuGic xInterruptController;
XUartPs Uart_Ps1;
static uint8_t uart1_rxdata_buffer[UART1_RECV_BUFFER_SIZE];




void UartPs_Intr_CallBack_Handler(void *CallBackRef, u32 Event, u32 EventData)
{
	struct UART_Device *pDev = (struct UART_Device *)CallBackRef;
	struct UART_Drv_Data *data = pDev->priv_data;
	XUartPs *uart_ps = data->uart_handle;
	CirCularBuffer_t* cb = data->rx_cirbuffer_ptr;
	uint8_t *rx_buffer_ptr = data->rx_buffer_ptr;
	int rx_buffer_size = data->rx_buffer_size;

	/* All of the data has been sent */
	if (Event == XUARTPS_EVENT_SENT_DATA) {
		data->uart_send_success_cnt++;
		data->uart_send_data_cnt += EventData;
	};

	/* All of the data has been received */
	if (Event == XUARTPS_EVENT_RECV_DATA) {
		while(XUartPs_IsSending(uart_ps));
		// 更新状态
		data->uart_recv_success_cnt++;
		data->uart_recv_data_cnt += EventData;
		// 写入循环缓冲区
		if(cb != NULL)
		{
			cb_write(cb, rx_buffer_ptr, EventData);
			if(data->notify != NULL)
			{
				data->notify(pDev, EventData);
			}
		}
		// 再次接收
		XUartPs_Recv(uart_ps, rx_buffer_ptr, rx_buffer_size);
	}

	/*
	 * Data was received, but not the expected number of bytes, a
	 * timeout just indicates the data stopped for 8 character times
	 */
	if (Event == XUARTPS_EVENT_RECV_TOUT) {
		if(0 != EventData)								// 避免空闲中断没有数据的情况
		{
			while(XUartPs_IsSending(uart_ps));
			// 更新状态
			data->uart_recv_success_cnt++;
			data->uart_recv_data_cnt += EventData;
			// 写入循环缓冲区
			if(cb != NULL)
			{
				cb_write(cb, rx_buffer_ptr, EventData);
				if(data->notify != NULL)
				{
					data->notify(pDev, EventData);
				}
			}
		}
		// 再次接收
		XUartPs_Recv(uart_ps, rx_buffer_ptr, rx_buffer_size);
	}

	/*
	 * Data was received with an error, keep the data but determine
	 * what kind of errors occurred
	 */
	if (Event == XUARTPS_EVENT_RECV_ERROR) {
		// 更新状态
		data->uart_recv_err_cnt++;
	}

	/*
	 * Data was received with an parity or frame or break error, keep the data
	 * but determine what kind of errors occurred. Specific to Zynq Ultrascale+
	 * MP.
	 */
	if (Event == XUARTPS_EVENT_PARE_FRAME_BRKE) {
		// 更新状态
		data->uart_recv_err_cnt++;
	}

	/*
	 * Data was received with an overrun error, keep the data but determine
	 * what kind of errors occurred. Specific to Zynq Ultrascale+ MP.
	 */
	if (Event == XUARTPS_EVENT_RECV_ORERR) {
		// 更新状态
		data->uart_recv_err_cnt++;
	}
}

int zynq_uart_init(struct UART_Device *pDev, int baud, int datas, int parity, char stop)
{
	int status;
	u32 IntrMask;
	XUartPs_Config *uart_cfg;

	struct UART_Drv_Data *data = pDev->priv_data;
	int uart_init_flag = data->init_flag;
	int device_id = data->uart_device_id;
	int uart_int_irq_id = data->uart_int_irq_id;
	XUartPs *uart_ps = data->uart_handle;
	XScuGic *intc = data->gic_handle;
	char priority = data->uart_priority;
	char trigger = data->uart_trigger;
	XUartPs_Handler callback_func = data->callback;
	uint8_t *rx_buffer_ptr = data->rx_buffer_ptr;
	int rx_buffer_size = data->rx_buffer_size;
	XUartPsFormat UARTFormat;
	UARTFormat.BaudRate = baud;
	UARTFormat.DataBits = datas;
	UARTFormat.Parity = parity;
	UARTFormat.StopBits = stop;

	if(uart_init_flag == 1)
	{
		return XST_SUCCESS;
	}

	// 1. 实现GIC的初始化和注册异常---应该在所有驱动的最外层实现

	// 2. UART的配置
	uart_cfg = XUartPs_LookupConfig(device_id);
	if (NULL == uart_cfg)
	{
		ssbl_printf(LOG_ERR, "XUartPs_LookupConfig failed!\n");
		return XST_FAILURE;
	}

	status = XUartPs_CfgInitialize(uart_ps, uart_cfg, uart_cfg->BaseAddress);
	if (status != XST_SUCCESS)
	{
		ssbl_printf(LOG_ERR, "XUartPs_CfgInitialize failed!\n");
		return XST_FAILURE;
	}

		//设置工作模式:正常模式
	XUartPs_SetOperMode(uart_ps, XUARTPS_OPER_MODE_NORMAL);
		//设置波特率:115200
//	XUartPs_SetBaudRate(uart_ps, baud);
	XUartPs_SetDataFormat(uart_ps, &UARTFormat);
		// 设置中断优先级
	XScuGic_SetPriorityTriggerType(intc, uart_int_irq_id, priority, trigger);
		//设置RxFIFO的中断触发等级
	XUartPs_SetFifoThreshold(uart_ps, 32);			// 32<64
		//设置空闲中断
	XUartPs_SetRecvTimeout(uart_ps, 8);				// 8个时钟没有数据
		//设置中断处理函数的回调函数
	XUartPs_SetHandler(uart_ps, (XUartPs_Handler)callback_func, pDev);
		//设置UART的中断触发方式
	IntrMask =
				XUARTPS_IXR_TOUT | XUARTPS_IXR_PARITY | XUARTPS_IXR_FRAMING |
				XUARTPS_IXR_OVER | XUARTPS_IXR_TXEMPTY | XUARTPS_IXR_RXFULL |
				XUARTPS_IXR_RXOVR;
	XUartPs_SetInterruptMask(uart_ps, IntrMask);

		//注册UART_INT_IRQ_ID中断处理函数到GIC
	status = XScuGic_Connect(intc, uart_int_irq_id,
				(Xil_ExceptionHandler) XUartPs_InterruptHandler,(void *) uart_ps);
	if (status != XST_SUCCESS)
	{
		ssbl_printf(LOG_ERR, "XScuGic_Connect failed!\n");
		return XST_FAILURE;
	}
		//使能GIC中的串口中断
	XScuGic_Enable(intc, uart_int_irq_id);

	// 3. 启动一次接收
	XUartPs_Recv(uart_ps, rx_buffer_ptr, rx_buffer_size);

	// 标记初始化完成
	data->init_flag = 1;

	return XST_SUCCESS;
}

int zynq_uart_send(struct UART_Device *pDev, uint8_t *out, uint32_t len)
{
	struct UART_Drv_Data *data = (struct UART_Drv_Data *)pDev->priv_data;
	XUartPs *uart_ps = data->uart_handle;

	while(XUartPs_IsSending(uart_ps));			// 阻塞等待UART不处于发送状态
	XUartPs_Send(uart_ps, out, len);			// 完成一次突发发送或发送完成(<64字节)

	/*
	 * 异步发送，不用等待发送完成，但是要求发送的缓冲区out存在，如果释放掉会造成不可知后果
	 * 同步发送，等待发送完成数据计数=len，之后再释放发送缓冲区out
	 * 此处异步实现
	 */
	return 0;
}

int zynq_uart_recv(struct UART_Device *pDev, uint8_t *out, uint32_t len)
{
	struct UART_Drv_Data *data = (struct UART_Drv_Data *)pDev->priv_data;
	CirCularBuffer_t* cb = data->rx_cirbuffer_ptr;
	int datalen = 0;
	datalen = cb_read(cb, out, len);

	return datalen;			// 实际大小
}

int zynq_uart_registercb(struct UART_Device *pDev, CirCularBuffer_t* cb)
{
	struct UART_Drv_Data *data = (struct UART_Drv_Data *)pDev->priv_data;
	if(data->rx_cirbuffer_ptr == NULL)
	{
		data->rx_cirbuffer_ptr = cb;
		return 0;
	}
	else
	{
		ssbl_printf(LOG_ERR, "CirCularBuffer has been registered, this register action Failed!\n");
		return -1;
	}
}

int zynq_uart_registernotify(struct UART_Device *pDev, uart_device_notify_t notify)
{
	struct UART_Drv_Data *data = (struct UART_Drv_Data *)pDev->priv_data;
	if(data->notify == NULL)
	{
		data->notify = notify;
		return 0;
	}
	else
	{
		data->notify = notify;
		ssbl_printf(LOG_INFO, "zynq_uart_registernotify function changed!\r\n");
		return 0;
	}
}


struct UART_Drv_Data zynq_uart1_data = {
	.init_flag = 0,
	.uart_device_id = XPAR_PS7_UART_1_DEVICE_ID,
	.uart_int_irq_id = XPAR_XUARTPS_1_INTR,
	.uart_priority = 0xA0,
	.uart_trigger = 0x03,
	.rx_buffer_size = UART1_RECV_BUFFER_SIZE,
	.uart_send_err_cnt = 0,
	.uart_send_success_cnt = 0,
	.uart_send_data_cnt = 0,
	.uart_recv_err_cnt = 0,
	.uart_recv_success_cnt = 0,
	.uart_recv_data_cnt = 0,
	.callback = UartPs_Intr_CallBack_Handler,
	.notify = NULL,
	.gic_handle = &xInterruptController,
	.uart_handle = &Uart_Ps1,
	.rx_buffer_ptr = uart1_rxdata_buffer,
	.rx_cirbuffer_ptr = NULL,
};

struct UART_Device g_zynq_uart1 = {
	.name = "zynq_uart1",
	.init = zynq_uart_init,
	.send = zynq_uart_send,
	.recv = zynq_uart_recv,
	.priv_data = &zynq_uart1_data,
	.registercb = zynq_uart_registercb,
	.registernotify = zynq_uart_registernotify,
};

/******************************************************************/
/*
 * 全局数组中添加Device到UART_Device
 */
// 全局的UART_Device结构体数组
struct UART_Device *g_uart_devs[] = {&g_zynq_uart1};

struct UART_Device * Get_UART_Device(char *name)
{
	int i = 0;
	for(i = 0; i < sizeof(g_uart_devs)/sizeof(g_uart_devs[0]); i++)
	{
		if(0 == strcmp(name, g_uart_devs[i]->name))
		{
			return g_uart_devs[i];
		}
	}
	ssbl_printf(LOG_INFO, "Can not find device name!\n");
	return NULL;
}

/*****************************************************************************/
/*
 * API实现
 */

int uart_Init(struct UART_Device *pDev, int baud, int datas, int parity, char stop)
{
	if(!pDev || !(pDev->init))
		return -1;
	return pDev->init(pDev, baud, datas, parity, stop);
}

int uart_Send(struct UART_Device *pDev, uint8_t *data, uint32_t len)
{
	if(!pDev || !(pDev->send))
		return -1;
	return pDev->send(pDev, data, len);
}

int uart_Recv(struct UART_Device *pDev, uint8_t *data, uint32_t len)
{
	if(!pDev || !(pDev->recv))
		return -1;
	return pDev->recv(pDev, data, len);
}

int uart_Register_Cb(struct UART_Device *pDev, CirCularBuffer_t* cb)
{
	if(!pDev || !(pDev->registercb))
		return -1;
	return pDev->registercb(pDev, cb);
}

int uart_Register_Notify(struct UART_Device *pDev, uart_device_notify_t notify)
{
	if(!pDev || !(pDev->registernotify))
		return -1;
	return pDev->registernotify(pDev, notify);
}

