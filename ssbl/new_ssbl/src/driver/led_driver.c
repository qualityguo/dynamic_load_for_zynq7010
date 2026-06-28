/***************************** Include Files *********************************/
#include "led_driver.h"
#include <string.h>
#include <stdbool.h>
#include "ssbl.h"

/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/
struct LED_Device{
	// 名称
	char *name;
	// LED对象指针
	int (*init)(struct LED_Device *pDev);
	// 点亮LED
	int (*on)(struct LED_Device *pDev);
	// 熄灭LED
	int (*off)(struct LED_Device *pDev);
	// 翻转LED
	int (*toggle)(struct LED_Device *pDev);
	// 读取LED状态
	int (*read)(struct LED_Device *pDev, uint8_t *state);
	// 私有数据
	void *priv_data;
};
struct LED_Drv_Data{
	int init_flag;					// init-flag
	uint32_t pin;					// MIO引脚号
	uint8_t state;					// 当前状态: 0=熄灭, 1=点亮
	XGpioPs * gpio_handle;			// GPIO控制器
};

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/
static void led_hw_set(struct LED_Drv_Data *data, uint8_t on);
int zynq_led_init(struct LED_Device *pDev);
int zynq_led_on(struct LED_Device *pDev);
int zynq_led_off(struct LED_Device *pDev);
int zynq_led_toggle(struct LED_Device *pDev);
int zynq_led_read(struct LED_Device *pDev, uint8_t *state);


/************************** Variable Definitions *****************************/
// bsp.c中初始化的GPIO控制器，LED和KEY共用同一个控制器
extern XGpioPs xGpioPs;



static void led_hw_set(struct LED_Drv_Data *data, uint8_t on)
{
	// 低电平有效: on=1 -> 写0点亮, on=0 -> 写1熄灭
	XGpioPs_WritePin(data->gpio_handle, data->pin, on ? 0U : 1U);
	data->state = on;
}

int zynq_led_init(struct LED_Device *pDev)
{
	struct LED_Drv_Data *data = pDev->priv_data;

	if(data->init_flag == 1)
	{
		return 0;
	}

	XGpioPs_SetDirectionPin(data->gpio_handle, data->pin, 1U);		// 方向为输出
	XGpioPs_SetOutputEnablePin(data->gpio_handle, data->pin, 1U);	// 输出使能
	led_hw_set(data, 0);											// 默认熄灭

	data->init_flag = 1;
	return 0;
}

int zynq_led_on(struct LED_Device *pDev)
{
	struct LED_Drv_Data *data = pDev->priv_data;
	led_hw_set(data, 1);
	return 0;
}

int zynq_led_off(struct LED_Device *pDev)
{
	struct LED_Drv_Data *data = pDev->priv_data;
	led_hw_set(data, 0);
	return 0;
}

int zynq_led_toggle(struct LED_Device *pDev)
{
	struct LED_Drv_Data *data = pDev->priv_data;
	led_hw_set(data, !data->state);
	return 0;
}

int zynq_led_read(struct LED_Device *pDev, uint8_t *state)
{
	struct LED_Drv_Data *data = pDev->priv_data;
	*state = data->state;
	return 0;
}


struct LED_Drv_Data zynq_led0_data = {
	.init_flag = 0,
	.pin = 0,
	.state = 0,
	.gpio_handle = &xGpioPs,
};

struct LED_Device g_zynq_led0 = {
	.name = "zynq_led0",
	.init = zynq_led_init,
	.on = zynq_led_on,
	.off = zynq_led_off,
	.toggle = zynq_led_toggle,
	.read = zynq_led_read,
	.priv_data = &zynq_led0_data,
};

struct LED_Drv_Data zynq_led1_data = {
	.init_flag = 0,
	.pin = 13,
	.state = 0,
	.gpio_handle = &xGpioPs,
};

struct LED_Device g_zynq_led1 = {
	.name = "zynq_led1",
	.init = zynq_led_init,
	.on = zynq_led_on,
	.off = zynq_led_off,
	.toggle = zynq_led_toggle,
	.read = zynq_led_read,
	.priv_data = &zynq_led1_data,
};

/******************************************************************/
/*
 * 全局数组中添加Device到LED_Device
 */
// 全局的LED_Device结构体数组
struct LED_Device * g_led_devs[] = {&g_zynq_led0, &g_zynq_led1};

struct LED_Device * Get_LED_Device(char *name)
{
	int i = 0;
	for(i = 0; i < sizeof(g_led_devs)/sizeof(g_led_devs[0]); i++)
	{
		if(0 == strcmp(name, g_led_devs[i]->name))
		{
			return g_led_devs[i];
		}
	}
	ssbl_printf(LOG_INFO, "Can not find device name!\n");
	return NULL;
}

/*****************************************************************************/
/*
 * API实现
 */

int led_Init(struct LED_Device *pDev)
{
	if(!pDev || !(pDev->init))
		return -1;
	return pDev->init(pDev);
}

int led_On(struct LED_Device *pDev)
{
	if(!pDev || !(pDev->on))
		return -1;
	return pDev->on(pDev);
}

int led_Off(struct LED_Device *pDev)
{
	if(!pDev || !(pDev->off))
		return -1;
	return pDev->off(pDev);
}

int led_Toggle(struct LED_Device *pDev)
{
	if(!pDev || !(pDev->toggle))
		return -1;
	return pDev->toggle(pDev);
}

int led_Read(struct LED_Device *pDev, uint8_t *state)
{
	if(!pDev || !(pDev->read))
		return -1;
	return pDev->read(pDev, state);
}
