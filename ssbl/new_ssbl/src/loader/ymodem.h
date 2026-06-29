#ifndef __YMODEM_H_
#define __YMODEM_H_

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#include <stdint.h>
#include "uart_driver.h"
#include "tx_api.h"

/************************** Constant Definitions *****************************/
#define	YM_SOH_PKG_SZ		(1 + 1 + 1 + 128 + 2)
#define	YM_STX_PKG_SZ		(1 + 1 + 1 + 1024 + 2)

/**************************** Type Definitions *******************************/
// 字节编码+错误码
enum ym_code {
	YM_CODE_NONE	=	0x00,		// 无数据
	YM_CODE_SOH		=	0x01,		// 128B头
	YM_CODE_STX		=	0x02,		// 1024B头
	YM_CODE_EOT		=	0x04,		// 结束
	YM_CODE_ACK		=	0x06,		// 应答
	YM_CODE_NAK		=	0x15,		// 非应答
	YM_CODE_CAN		=	0x18,		// 终止
	YM_CODE_C		=	0x43,		// 请求
	/* error code */
	YM_ERR_TMO		=	0x70,		// 握手超时
	YM_ERR_CODE		=	0x71,		// 字节错误
	YM_ERR_SEQ		=	0x72,		// 序号不连续
	YM_ERR_CRC		=	0x73,		// CRC错误
	YM_ERR_DSZ		=	0x74,		// 数据不足
	YM_ERR_CAN		=	0x75,		// 用户终止
	YM_ERR_ACK		=	0x76,		// 错误应答
	YM_ERR_FILE		=	0x77,		// 传输文件无效
};
// 状态机
enum ym_stage {
	YM_STAGE_NONE	=	0,
	YM_STAGE_ESTABLISHING,
	YM_STAGE_ESTABLISHED,
	YM_STAGE_TRANSMITTING,
	YM_STAGE_FINISHING,
	YM_STAGE_FINISHED,
};
// 对象声明
struct ym_ctx;
// 回调函数
typedef enum ym_code (*ym_callback)(struct ym_ctx *ctx, uint8_t *buf, uint32_t len);
// YM对象
struct ym_ctx {
	ym_callback			on_begin;			// 包0到达
	ym_callback			on_data;			// 数据包到达
	ym_callback			on_end;				// 结束
	enum ym_stage		stage;				// 此时状态
	uint8_t				*buf;				// 缓冲区
	struct UART_Device 	*dev;				// 关联串口对象
};
/************************** Function Prototypes ******************************/
int ymodem_recv_on_uart(struct ym_ctx *ctx, struct UART_Device *dev,
						ym_callback on_begin, ym_callback on_data, ym_callback on_end,
						int handshake_timeout);

#ifdef __cplusplus
}
#endif

#endif /* __YMODEM_H_ */
