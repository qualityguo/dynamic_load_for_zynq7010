/***************************** Include Files *********************************/
#include "key_driver.h"
#include <string.h>
#include <stdbool.h>
#include "ssbl.h"

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/
struct KEY_Device{
	// 名称
	char *name;
	// KEY对象指针
	int (*init)(struct KEY_Device *pDev);
	// 读取按键状态
	int (*read)(struct KEY_Device *pDev, uint8_t *state);
	// 私有数据
	void *priv_data;
	// 注册回调函数
	int (*registernotify)(struct KEY_Device *pDev, key_device_notify_t notify);
};
struct KEY_Drv_Data{
	int init_flag;					// init-flag
	uint32_t pin;					// MIO引脚号
	key_device_notify_t notify;		// 中断用户回调函数
	XGpioPs * gpio_handle;			// GPIO控制器
	XScuGic * gic_handle;			// GIC控制器
};

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/
void GpioPs_Intr_CallBack_Handler(void *CallBackRef, u32 Bank, u32 Status);
static void key_hw_read(struct KEY_Drv_Data *data, uint8_t *state);
static void key_intr_setup(void);
int zynq_key_init(struct KEY_Device *pDev);
int zynq_key_read(struct KEY_Device *pDev, uint8_t *state);
int zynq_key_registernotify(struct KEY_Device *pDev, key_device_notify_t notify);


/************************** Variable Definitions *****************************/
// bsp.c中初始化的GPIO控制器和GIC控制器，LED和KEY共用同一个GPIO控制器
extern XScuGic xInterruptController;
extern XGpioPs xGpioPs;
static int g_gpio_intr_connected = 0;		// GPIO中断是否已连接到GIC(整个GPIO控制器只需连接一次)



static void key_hw_read(struct KEY_Drv_Data *data, uint8_t *state)
{
	// 低电平有效: 读到0表示按下返回1, 读到1表示松开返回0
	*state = XGpioPs_ReadPin(data->gpio_handle, data->pin) ? 0U : 1U;
}

int zynq_key_init(struct KEY_Device *pDev)
{
	struct KEY_Drv_Data *data = pDev->priv_data;

	if(data->init_flag == 1)
	{
		return 0;
	}

	XGpioPs_SetDirectionPin(data->gpio_handle, data->pin, 0U);		// 方向为输入
	XGpioPs_SetIntrTypePin(data->gpio_handle, data->pin, XGPIOPS_IRQ_TYPE_EDGE_FALLING);	// 下降沿触发(按下)
	XGpioPs_IntrClearPin(data->gpio_handle, data->pin);				// 清空中断标志
	XGpioPs_IntrEnablePin(data->gpio_handle, data->pin);			// 使能中断

	key_intr_setup();												// 连接GPIO中断到GIC(仅执行一次)

	data->init_flag = 1;
	return 0;
}

int zynq_key_read(struct KEY_Device *pDev, uint8_t *state)
{
	struct KEY_Drv_Data *data = pDev->priv_data;
	key_hw_read(data, state);
	return 0;
}

int zynq_key_registernotify(struct KEY_Device *pDev, key_device_notify_t notify)
{
	struct KEY_Drv_Data *data = pDev->priv_data;
	if(data->notify == NULL)
	{
		data->notify = notify;
		return 0;
	}
	else
	{
		data->notify = notify;
		ssbl_printf(LOG_INFO, "zynq_key_registernotify function changed.\n");
		return 0;
	}
}


struct KEY_Drv_Data zynq_key0_data = {
	.init_flag = 0,
	.pin = 50,
	.notify = NULL,
	.gpio_handle = &xGpioPs,
	.gic_handle = &xInterruptController,
};

struct KEY_Device g_zynq_key0 = {
	.name = "zynq_key0",
	.init = zynq_key_init,
	.read = zynq_key_read,
	.priv_data = &zynq_key0_data,
	.registernotify = zynq_key_registernotify,
};

struct KEY_Drv_Data zynq_key1_data = {
	.init_flag = 0,
	.pin = 51,
	.notify = NULL,
	.gpio_handle = &xGpioPs,
	.gic_handle = &xInterruptController,
};

struct KEY_Device g_zynq_key1 = {
	.name = "zynq_key1",
	.init = zynq_key_init,
	.read = zynq_key_read,
	.priv_data = &zynq_key1_data,
	.registernotify = zynq_key_registernotify,
};


/******************************************************************/
/*
 * GPIO Bank中断处理函数
 *
 * XGpioPs_IntrHandler -> GpioPs_Intr_CallBack_Handler(CallBackRef, Bank, Status)
 * Bank1覆盖MIO32-53, 引脚N对应Status中的bit(N-32)
 */
void GpioPs_Intr_CallBack_Handler(void *CallBackRef, u32 Bank, u32 Status)
{
	(void)CallBackRef;

	if(Bank != XGPIOPS_BANK1)
		return;

	// KEY0 - MIO50 (Bank1 bit 18)
	if(Status & (1U << (50 - 32)))
	{
		XGpioPs_IntrClearPin(&xGpioPs, 50);				// 先清除中断标志, 否则会立即重新触发
		if(zynq_key0_data.notify != NULL)
		{
			zynq_key0_data.notify(&g_zynq_key0, 50);	// 通知应用层
		}
	}
	// KEY1 - MIO51 (Bank1 bit 19)
	if(Status & (1U << (51 - 32)))
	{
		XGpioPs_IntrClearPin(&xGpioPs, 51);
		if(zynq_key1_data.notify != NULL)
		{
			zynq_key1_data.notify(&g_zynq_key1, 51);
		}
	}
}

/*
 * 连接GPIO中断到GIC(整个GPIO控制器只需连接一次)
 */
static void key_intr_setup(void)
{
	if(g_gpio_intr_connected)
		return;

	XGpioPs_SetCallbackHandler(&xGpioPs, &xGpioPs, GpioPs_Intr_CallBack_Handler);
	XScuGic_Connect(&xInterruptController, XPS_GPIO_INT_ID,
					(Xil_InterruptHandler)XGpioPs_IntrHandler, &xGpioPs);
	XScuGic_Enable(&xInterruptController, XPS_GPIO_INT_ID);

	g_gpio_intr_connected = 1;
}

/******************************************************************/
/*
 * 全局数组中添加Device到KEY_Device
 */
// 全局的KEY_Device结构体数组
struct KEY_Device * g_key_devs[] = {&g_zynq_key0, &g_zynq_key1};

struct KEY_Device * Get_KEY_Device(char *name)
{
	int i = 0;
	for(i = 0; i < sizeof(g_key_devs)/sizeof(g_key_devs[0]); i++)
	{
		if(0 == strcmp(name, g_key_devs[i]->name))
		{
			return g_key_devs[i];
		}
	}
	ssbl_printf(LOG_INFO, "Can not find device name!\n");
	return NULL;
}

/*****************************************************************************/
/*
 * API实现
 */

int key_Init(struct KEY_Device *pDev)
{
	if(!pDev || !(pDev->init))
		return -1;
	return pDev->init(pDev);
}

int key_Read(struct KEY_Device *pDev, uint8_t *state)
{
	if(!pDev || !(pDev->read))
		return -1;
	return pDev->read(pDev, state);
}

int key_Register_Notify(struct KEY_Device *pDev, key_device_notify_t notify)
{
	if(!pDev || !(pDev->registernotify))
		return -1;
	return pDev->registernotify(pDev, notify);
}
