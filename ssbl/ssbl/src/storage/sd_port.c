/* boot/ssbl/storage/sd_port.c — SD 卡 storage 后端
 * 绑定 vendor fx_zynq_sd_driver，只挂载 P2 数据分区（spec §5.5.3）。
 * P1（BOOT.BIN）与 SSBL 完全隔离——SSBL 不读不写 P1。
 *
 * 分区定位：sd_port_init() 裸读 MBR 解析出 P2 起始 LBA，通过 fx_media_open()
 * 的 driver_info 透传给 fx_zynq_sd_driver；driver 的 BOOT_READ 直接按该绝对
 * LBA 读 P2 的 boot sector，并把它写入 fx_media_hidden_sectors，后续 READ/WRITE
 * 在逻辑扇区号上自动叠加该偏移。不用 FileX 自带的 _fx_partition_offset_calculate
 * ——后者会因 MBR 首 3 字节 "EB xx 90" 误判 MBR 为 boot sector 而返回 0。
 */

#include "storage.h"
#include "fx_api.h"
#include "fx_zynq_sdio_driver.h"
#include "device_core.h"
#include "ioctl_cmd.h"
#include "xil_printf.h"

extern FX_MEDIA  g_fx_media;
extern FX_FILE   g_fx_file;

extern int storage_fx_media_open(void);
extern int storage_fx_media_close(void);
extern int storage_fx_file_open(const char *path, int mode);
extern int storage_fx_file_read(void *buf, uint32_t len);
extern int storage_fx_file_write(const void *buf, uint32_t len);
extern int storage_fx_file_close(void);
extern int storage_fx_file_size(const char *path, uint32_t *size);
extern int storage_fx_file_create(const char *path);
extern int storage_fx_file_delete(const char *path);
extern int storage_fx_file_rename(const char *oldpath, const char *newpath);
extern int storage_fx_dir_list(const char *path);
extern void storage_fx_set_driver(VOID (*driver)(FX_MEDIA *), const char *name,
                                  VOID *driver_info);

/* 要挂载的分区的绝对起始 LBA：由 sd_port_init() 解析 MBR 得到（P2）。
 * 直接传给 FileX driver，绕开 FileX 自带的 MBR 自动检测。0 = 第一个分区。 */
static ULONG g_sd_part_start = 0;

const storage_ops_t g_sd_storage = {
    .media_open  = storage_fx_media_open,
    .media_close = storage_fx_media_close,
    .file_open   = storage_fx_file_open,
    .file_read   = storage_fx_file_read,
    .file_write  = storage_fx_file_write,
    .file_close  = storage_fx_file_close,
    .file_size   = storage_fx_file_size,
    .file_create = storage_fx_file_create,
    .file_delete = storage_fx_file_delete,
    .file_rename = storage_fx_file_rename,
    .dir_list    = storage_fx_dir_list,
};

/* 裸读单个 SD 扇区（挂载前解析 MBR 用，绕过 FileX）。
 * 复用 BSP 的 sd0 块设备 ioctl。返回 0=OK。 */
static int sd_raw_read_sector(ULONG sector, void *buf)
{
    struct device *sd = device_find("sd0");
    struct sd_rw_args args = { .sector = sector, .count = 1, .buf = buf };
    if (sd == NULL) return -1;
    return device_ioctl(sd, SD_IOCTL_READ_SECTORS, &args);
}

/* 读 MBR（LBA 0），解析第 2 个分区表项得 P2 起始 LBA，写入 g_sd_part_start。
 * MBR 分区表项布局（每项 16 字节，偏移 0x1BE 起）：
 *   byte[8..11] = 该分区起始 LBA（little-endian）
 *   第 2 项 = 偏移 0x1CE + 8 = 0x1D6
 * 返回：>0 = P2 起始 LBA；<=0 = 失败。
 */
static int sd_port_read_partition_table(void)
{
    uint8_t mbr[512];
    uint32_t lba;

    if (sd_raw_read_sector(0, mbr) != 0) {
        return -1;
    }
    /* 简单校验 MBR 签名 0x55AA */
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        return -2;
    }
    /* P2 起始 LBA（第 2 个分区表项，偏移 0x1D6，4 字节 little-endian） */
    lba =   (uint32_t)mbr[0x1D6]
         | ((uint32_t)mbr[0x1D7] << 8)
         | ((uint32_t)mbr[0x1D8] << 16)
         | ((uint32_t)mbr[0x1D9] << 24);

    g_sd_part_start = (ULONG)lba;
    return (lba != 0) ? (int)lba : -3;
}

/* SSBL 初始化时调用：解析 P2 起始 LBA + 设置 g_storage + 注册 driver。
 * 注意：本函数不调用任何 FileX/ThreadX API，可在 tx_kernel_enter 前调用。 */
void sd_port_init(void)
{
    int p2_lba = sd_port_read_partition_table();
    if (p2_lba <= 0) {
        xil_printf("[sd_port] WARN: P2 partition not found (rc=%d), "
                   "ensure SD is dual-formatted per spec §5.5\r\n", p2_lba);
        g_sd_part_start = 0;
    } else {
        xil_printf("[sd_port] P2 start LBA = %d\r\n", p2_lba);
    }

    /* 注册 FileX driver + P2 绝对起始 LBA（经 driver_info 透传给 driver 的
     * BOOT_READ，直接定位到 P2 的 boot sector）。 */
    storage_fx_set_driver(fx_zynq_sd_driver, "SD_P2", &g_sd_part_start);
    g_storage = &g_sd_storage;
}
