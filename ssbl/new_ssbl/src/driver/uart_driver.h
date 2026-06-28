#ifndef __UART_DRIVER_H_
#define __UART_DRIVER_H_

#include "stdint.h"
#include "cb.h"

struct UART_Device;					// 前向声明
typedef void (*uart_device_notify_t)(struct UART_Device *dev, uint32_t event);


struct UART_Device * Get_UART_Device(char *name);
int uart_Init(struct UART_Device *pDev, int baud, int datas, int parity, char stop);
int uart_Send(struct UART_Device *pDev, uint8_t *data, uint32_t len);
int uart_Recv(struct UART_Device *pDev, uint8_t *data, uint32_t len);
int uart_Register_Cb(struct UART_Device *pDev, CirCularBuffer_t* cb);
int uart_Register_Notify(struct UART_Device *pDev, uart_device_notify_t notify);

/*
 * 使用方式: 实现对应的UART_Device结构体，并添加到全局数组g_uart_devs中
 * 通过Get_UART_Device和name得到UART_Device结构体，并使用对应函数
 * 使用register函数注册接收用循环缓冲区
 * 使用init函数初始化
 * 使用send函数发送数据
 * 使用recv函数接收数据
 */

#endif /* __UART_DRIVER_H_ */
