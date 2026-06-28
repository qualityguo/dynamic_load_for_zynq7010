#ifndef __KEY_DRIVER_H_
#define __KEY_DRIVER_H_

#include "stdint.h"

struct KEY_Device;					// 前向声明
typedef void (*key_device_notify_t)(struct KEY_Device *dev, uint32_t event);

struct KEY_Device * Get_KEY_Device(char *name);
int key_Init(struct KEY_Device *pDev);
int key_Read(struct KEY_Device *pDev, uint8_t *state);
int key_Register_Notify(struct KEY_Device *pDev, key_device_notify_t notify);

/*
 * 使用方式: 实现对应的KEY_Device结构体，并添加到全局数组g_key_devs中
 * 通过Get_KEY_Device和name得到KEY_Device结构体，并使用对应函数
 * 使用registernotify函数注册按键中断回调函数
 * 使用init函数初始化
 * 使用read函数读取按键状态(任务上下文中调用)
 */

#endif /* __KEY_DRIVER_H_ */
