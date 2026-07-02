#ifndef __QSPI_DRIVER_H_
#define __QSPI_DRIVER_H_

/*
 * =============================================================================
 *                       第三层：QSPI NOR Flash 硬件驱动
 * =============================================================================
 * 对接 Zynq-7000 PS QSPI 控制器(XQspiPs) + Winbond W25Q 系列 NOR Flash。
 * 接口以【绝对 Flash 物理地址】为单位，不含任何文件系统/磨损均衡语义。
 *
 * 实现严格参考官方 FSBL：ssbl/new_fsbl/src/qspi.c
 *   - InitQspi()  -> qspi_drv_init()
 *   - FlashReadID()-> qspi_drv_read_id()
 *   - FlashRead() -> qspi_drv_read()
 *   (FSBL 为只读 BootLoader，无 write/erase；本层补齐 page program / 4K 擦除)
 *
 * 上层(Layer2 LevelX)负责地址翻译：物理地址 = QSPI_BOOT_OFFSET + 逻辑偏移。
 * =============================================================================
 */

#include <stdint.h>

/*
 * 初始化 QSPI 控制器（IO 模式、手动片选），读取并校验 JEDEC ID，解除 Flash
 * 块保护位。幂等：重复调用直接返回成功（由内部 init_flag 保证）。
 *
 * @return  0 成功；-1 失败（控制器初始化失败或 JEDEC ID 不符）
 */
int  qspi_drv_init(void);

/*
 * 读取 Flash JEDEC ID（命令 0x9F）。
 * @param id  输出 3 字节：id[0]=厂商 id[1]=类型 id[2]=容量
 * @return    0 成功；-1 传输失败
 */
int  qspi_drv_read_id(uint8_t id[3]);

/*
 * 从 Flash 读取数据（FAST_READ 0x0B，3 字节寻址）。
 * dummy 字节由 XQspiPs 驱动自动注入（见 xqspips.c），本函数只在发送缓冲放
 * cmd+3addr。可任意长度，内部按 4KB 分段。
 *
 * @param addr  绝对 Flash 地址（3 字节寻址，< 16MB）
 * @param buf   接收缓冲（调用方分配）
 * @param len   读取字节数
 * @return      0 成功；-1 传输失败
 */
int  qspi_drv_read(uint32_t addr, uint8_t *buf, uint32_t len);

/*
 * 向 Flash 写入数据（页编程 0x02，3 字节寻址）。
 * NOR 页编程不得跨 256B 页边界，本函数内部按页边界分片，调用方可一次写入
 * 任意长度（>256B 会自动多次下发 PP 命令）。
 *
 * @param addr  绝对 Flash 地址
 * @param data  待写入数据
 * @param len   字节数
 * @return      0 成功；-1 传输失败
 */
int  qspi_drv_write(uint32_t addr, const uint8_t *data, uint32_t len);

/*
 * 4KB 块擦除（命令 0x20，3 字节寻址）。
 * @param addr  待擦除块的绝对 Flash 地址（块内任意地址均可，按 4KB 对齐生效）
 * @return      0 成功；-1 传输失败
 */
int  qspi_drv_erase_4k(uint32_t addr);

/*
 * 读取状态寄存器（W25Q256：which=1/2/3 对应 SR1/SR2/SR3，命令 0x05/0x35/0x15）。
 * @return  SR 字节；<0 传输失败
 */
int  qspi_drv_read_sr(int which);

#endif /* __QSPI_DRIVER_H_ */
