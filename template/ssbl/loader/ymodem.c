/***************************** Include Files *********************************/
#include "ymodem.h"
#include "ssbl.h"
#include <stdlib.h>
#include <string.h>

/************************** Constant Definitions *****************************/
#define	YM_CHD_INTV_TICK		(3u * TX_TIMER_TICKS_PER_SECOND)				// 握手包超时时间
#define YM_WAIT_CHR_TICK		(3u * TX_TIMER_TICKS_PER_SECOND)				// 包内超时时间
#define YM_MAX_RETRY			5

/************************** Function Prototypes ******************************/
static void 		uart1_cb(struct UART_Device *dev, uint32_t event);
static uint16_t		ymodem_crc16(const uint8_t *buf, uint32_t len);
static void 		ymodem_putc(struct ym_ctx *ctx, uint8_t c);
static enum ym_code	ymodem_read_code(struct ym_ctx *ctx, ULONG timeout);
static uint32_t		ymodem_read_data(struct ym_ctx *ctx, uint32_t len);
static int 			ymodem_recv_handshake(struct ym_ctx *ctx, int timeout_sec);
static int			ymodem_recv_trans(struct ym_ctx *ctx);
static int			ymodem_recv_fin(struct ym_ctx *ctx);

/************************** Variable Definitions *****************************/
static	TX_SEMAPHORE		ymodem_semaphore;
static	uint8_t				ymodem_sem_created = 0;
static	uint8_t				tm_rx_buf[YM_STX_PKG_SZ];
static	struct ym_ctx		*g_ym_ctx_activate = NULL;

int ymodem_recv_on_uart(struct ym_ctx *ctx, struct UART_Device *dev,
						ym_callback on_begin, ym_callback on_data, ym_callback on_end,
						int handshake_timeout)
{
	int err;

	// 入口检查
	if(ctx == NULL || dev == NULL)
		return -YM_ERR_FILE;

	if(handshake_timeout <= 0)
		handshake_timeout = 20;				// 缺省20s

	// 创建内部信号量
	if(ymodem_sem_created == 0)
	{
		tx_semaphore_create(&ymodem_semaphore, "ym_sem", 0);
		ymodem_sem_created = 1;
	}

	// 初始化YModem对象
	ctx->dev = dev;
	ctx->on_begin = on_begin;
	ctx->on_data = on_data;
	ctx->on_end = on_end;
	ctx->stage = YM_STAGE_NONE;
	ctx->buf = tm_rx_buf;
	g_ym_ctx_activate = ctx;

	// 注册串口回调函数
	uart_Register_Notify(dev, uart1_cb);

	// step1: 握手
	err = ymodem_recv_handshake(ctx, handshake_timeout);
	// setp2: 中间包
	if(err == 0)
		err = ymodem_recv_trans(ctx);
	// setp3: 结束包
	if(err == 0)
		err = ymodem_recv_fin(ctx);

	// 重注册串口回调函数
	uart_Register_Notify(dev, NULL);
	g_ym_ctx_activate = NULL;

	return err;
}


static void 		uart1_cb(struct UART_Device *dev, uint32_t event)
{
	if(g_ym_ctx_activate && ymodem_sem_created==1)
		tx_semaphore_put(&ymodem_semaphore);
}

static uint16_t		ymodem_crc16(const uint8_t *buf, uint32_t len)
{
	uint16_t crc = 0;
	uint32_t i = 0, j = 0;

	for(i = 0; i < len; i++)
	{
		crc ^= (uint16_t) buf[i] << 8;
		for(j = 0; j < 8; j++)
		{
			crc = (crc & 0x8000) ? (uint16_t)((crc << 1)^0x1021) : (uint16_t)(crc << 1);
		}
	}

	return crc;
}

static void 		ymodem_putc(struct ym_ctx *ctx, uint8_t c)
{
	uart_Send(ctx->dev, &c, 1);
}

static enum ym_code	ymodem_read_code(struct ym_ctx *ctx, ULONG timeout)
{
	uint8_t c;

	// 假设我的循环缓冲区中已经有数据了
	if(uart_Recv(ctx->dev, &c, 1) == 1)					// 取到数据直接返回
		return (enum ym_code)c;

	do{
		// 等待超时时间再拿数
		if(tx_semaphore_get(&ymodem_semaphore, timeout) != TX_SUCCESS)
			return YM_CODE_NONE;
		if(uart_Recv(ctx->dev, &c, 1) == 1)					// 取到数据直接返回
			return (enum ym_code)c;
	}while(1);
}

static uint32_t		ymodem_read_data(struct ym_ctx *ctx, uint32_t len)
{
	uint8_t *p = ctx->buf + 1;
	uint32_t got = 0;

	// 读取直到超时或者读取了指定长度
	do{
		got += (uint32_t)uart_Recv(ctx->dev, p+got, len-got);
		if(got >= len)
			return got;
	}while(tx_semaphore_get(&ymodem_semaphore, YM_WAIT_CHR_TICK) == TX_SUCCESS);

	return got;
}



static int ymodem_recv_handshake(struct ym_ctx *ctx, int timeout_sec)
{
	ULONG deadline = tx_time_get() + (ULONG)timeout_sec * TX_TIMER_TICKS_PER_SECOND;
	enum ym_code code;
	uint32_t plen = 0;
	uint16_t crc = 0, crc_rx = 0;

	// 切换状态
	ctx->stage = YM_STAGE_ESTABLISHING;

	for(;;)
	{
		if(tx_time_get() >= deadline)
			return -YM_ERR_TMO;					// 握手超时

		ymodem_putc(ctx, YM_CODE_C);			// 发送字符C
		code = ymodem_read_code(ctx, YM_CHD_INTV_TICK);			// 3s等待
		if(code == YM_CODE_NONE)
			continue;							// 空则继续尝试握手
		if(code != YM_CODE_SOH && code != YM_CODE_STX)
			continue;							// 乱码也继续尝试

		// 收到了数据包的头
		plen = (code == YM_CODE_SOH) ? 128u : 1024u;

		// 根据头读一包
		if(ymodem_read_data(ctx, 2 + plen + 2) != (2 + plen + 2))
			return -YM_ERR_DSZ;

		// 校验序号, 必须是0
		if(ctx->buf[1] != 0x00 || ctx->buf[2] != 0xFF)
			return -YM_ERR_SEQ;

		// 做CRC校验
		crc = ymodem_crc16(ctx->buf+3, plen);
		crc_rx = ((uint16_t)ctx->buf[3+plen] << 8) | ctx->buf[3+plen+1];
		if(crc != crc_rx)
			return -YM_ERR_CRC;

		// 发ACK+C
		ymodem_putc(ctx, YM_CODE_ACK);
		ymodem_putc(ctx, YM_CODE_C);

		// 调用回调函数
		if(ctx->on_begin != NULL)
		{
			if(ctx->on_begin(ctx, ctx->buf+3, plen) == YM_CODE_CAN)
				return -YM_ERR_FILE;
		}

		ctx->stage = YM_STAGE_ESTABLISHED;
		return 0;
	}
}

static int			ymodem_recv_trans(struct ym_ctx *ctx)
{
	enum ym_code code;
	int retry = 0;
	uint32_t plen = 0, expect_seq = 1;
	uint16_t crc = 0, crc_rx = 0;

	for(;;)
	{
		code = ymodem_read_code(ctx, YM_WAIT_CHR_TICK);

		if(code == YM_CODE_EOT)						// 结束标志
		{
			ctx->stage = YM_STAGE_FINISHING;
			return 0;
		}
		if(code == YM_CODE_CAN)						// 两次CANs, 结束传输
		{
			if(ymodem_read_code(ctx, YM_WAIT_CHR_TICK) == YM_CODE_CAN)
				return -YM_ERR_CAN;
			continue;								// 单个CAN, 可能是噪声, 继续等待
		}
		if(code != YM_CODE_SOH && code != YM_CODE_STX)		// 回NAK并重传, 重传次数限制
		{
			if(++retry > YM_MAX_RETRY)
				return -YM_ERR_CODE;
			ymodem_putc(ctx, YM_CODE_NAK);
			continue;
		}

		// 收到了数据包的头
		plen = (code == YM_CODE_SOH) ? 128u : 1024u;

		// 根据头读一包
		if(ymodem_read_data(ctx, 2 + plen + 2) != (2 + plen + 2))
			return -YM_ERR_DSZ;

		// 校验序号-必须连续
		if(ctx->buf[1] != (uint8_t)(expect_seq) || ctx->buf[2] != (uint8_t)(~expect_seq))
			return -YM_ERR_SEQ;

		// 做CRC校验
		crc = ymodem_crc16(ctx->buf+3, plen);
		crc_rx = ((uint16_t)ctx->buf[3+plen] << 8) | ctx->buf[3+plen+1];
		if(crc != crc_rx)
		{
			if(++retry > YM_MAX_RETRY)
				return -YM_ERR_CRC;
			ymodem_putc(ctx, YM_CODE_NAK);
			continue;
		}
		retry = 0;

		// 首个有效数据包到达, 切换到传输态
		if(expect_seq == 1)
			ctx->stage = YM_STAGE_TRANSMITTING;

		// 发ACK
		ymodem_putc(ctx, YM_CODE_ACK);
		expect_seq++;

		// 调用回调函数
		if(ctx->on_data != NULL)
		{
			if(ctx->on_data(ctx, ctx->buf+3, plen) == YM_CODE_CAN)
				return -YM_ERR_FILE;
		}
	}

}

static int			ymodem_recv_fin(struct ym_ctx *ctx)
{
	enum ym_code code;
	uint32_t plen = 0;
	uint16_t crc = 0, crc_rx = 0;

	// 发送NAK
	ymodem_putc(ctx, YM_CODE_NAK);

	// 读取应答
	code = ymodem_read_code(ctx, YM_WAIT_CHR_TICK);
	if (code != YM_CODE_EOT)
		return -YM_ERR_CODE;

	// 发送ACK+C
	ymodem_putc(ctx, YM_CODE_ACK);
	ymodem_putc(ctx, YM_CODE_C);

	// 收全0包
	code = ymodem_read_code(ctx, YM_WAIT_CHR_TICK);
	if(code != YM_CODE_SOH && code != YM_CODE_STX)
		return -YM_ERR_CODE;

	// 收到了数据包的头
	plen = (code == YM_CODE_SOH) ? 128u : 1024u;

	// 根据头读一包
	if(ymodem_read_data(ctx, 2 + plen + 2) != (2 + plen + 2))
		return -YM_ERR_DSZ;

	// CRC校验
	crc = ymodem_crc16(ctx->buf+3, plen);
	crc_rx = ((uint16_t)ctx->buf[3+plen] << 8) | ctx->buf[3+plen+1];
	if(crc != crc_rx)
		return -YM_ERR_CRC;

	// 发ACK
	ymodem_putc(ctx, YM_CODE_ACK);

	// 调用回调函数
	if(ctx->on_end != NULL)
	{
		if(ctx->on_end(ctx, ctx->buf+3, plen) == YM_CODE_CAN)
			return -YM_ERR_FILE;
	}

	ctx->stage = YM_STAGE_FINISHED;
	return 0;
}

