#ifndef __LED_DRIVER_H_
#define __LED_DRIVER_H_

#include "stdint.h"

struct LED_Device;					// 前向声明

struct LED_Device * Get_LED_Device(char *name);
int led_Init(struct LED_Device *pDev);
int led_On(struct LED_Device *pDev);
int led_Off(struct LED_Device *pDev);
int led_Toggle(struct LED_Device *pDev);
int led_Read(struct LED_Device *pDev, uint8_t *state);

/*
 * 使用方式: 实现对应的LED_Device结构体，并添加到全局数组g_led_devs中
 * 通过Get_LED_Device和name得到LED_Device结构体，并使用对应函数
 * 使用init函数初始化
 * 使用On/Off/Toggle函数控制LED
 * 使用Read函数读取LED状态
 */

#endif /* __LED_DRIVER_H_ */
