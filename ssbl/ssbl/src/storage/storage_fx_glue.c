#include "storage.h"
#include "fx_api.h"
#include <string.h>
#include "xil_printf.h"

#define STORAGE_MEDIA_NAME_LEN  32

/* FileX media 缓冲，至少 1 个扇区；SD 卡扇区固定 512B，给 4 扇区留余量。
 * （FileX 无 FX_SECTOR_SIZE 宏，按 BPB 实际扇区大小，SD=512。） */
#define SD_SECTOR_SIZE          512u

/* 全局 FileX 对象（一份，不并发访问）*/
FX_MEDIA  g_fx_media;
FX_FILE   g_fx_file;

/* 全局 storage 后端指针：storage.h 里 extern 声明，此处定义。
 * sd_port_init() 运行前为 NULL；sd_port_init 把它指向 &g_sd_storage。 */
const storage_ops_t *g_storage = NULL;

/* media 内存缓冲（FileX 要求，扇区对齐）*/
static UCHAR media_memory[SD_SECTOR_SIZE * 4] __attribute__((aligned(32)));

/* 当前 media 的 driver 函数指针（由 sd_port/qspi_port 设置）*/
static VOID (*g_fx_driver)(FX_MEDIA *) = NULL;

/* 传给 fx_media_open 的 driver_info（如 SD 分区索引），透传给 driver */
static VOID *g_fx_driver_info = NULL;

/* 当前 media 名称（"SD_CARD" / "QSPI_NOR"）*/
static char g_media_name[STORAGE_MEDIA_NAME_LEN] = "(none)";

void storage_fx_set_driver(VOID (*driver)(FX_MEDIA *), const char *name,
                           VOID *driver_info)
{
    g_fx_driver = driver;
    g_fx_driver_info = driver_info;
    strncpy(g_media_name, name, STORAGE_MEDIA_NAME_LEN - 1);
    g_media_name[STORAGE_MEDIA_NAME_LEN - 1] = '\0';
}

/* === FileX 通用回调 === */
int storage_fx_media_open(void)
{
    UINT status;
    if (g_fx_driver == NULL) return STORAGE_ERR_INVAL;
    status = fx_media_open(&g_fx_media, g_media_name, g_fx_driver,
                           g_fx_driver_info, media_memory, sizeof(media_memory));
    if (status != FX_SUCCESS) {
        xil_printf("[fx_glue] fx_media_open failed, FX status = 0x%x\r\n", status);
        return STORAGE_ERR_OPEN;
    }
    return STORAGE_OK;
}

int storage_fx_media_close(void)
{
    UINT status = fx_media_close(&g_fx_media);
    return (status == FX_SUCCESS) ? STORAGE_OK : STORAGE_ERR_IO;
}

int storage_fx_file_open(const char *path, int mode)
{
    UINT fx_mode;
    if (mode & STORAGE_OPEN_WRITE) {
        fx_mode = FX_OPEN_FOR_WRITE;
    } else {
        fx_mode = FX_OPEN_FOR_READ;
    }
    UINT status = fx_file_open(&g_fx_media, &g_fx_file, (CHAR *)path, fx_mode);
    return (status == FX_SUCCESS) ? STORAGE_OK : STORAGE_ERR_NOT_FOUND;
}

int storage_fx_file_read(void *buf, uint32_t len)
{
    ULONG actual = 0;
    UINT status = fx_file_read(&g_fx_file, buf, len, &actual);
    if (status != FX_SUCCESS) return STORAGE_ERR_IO;
    return (int)actual;
}

int storage_fx_file_write(const void *buf, uint32_t len)
{
    UINT status = fx_file_write(&g_fx_file, (void *)buf, len);
    return (status == FX_SUCCESS) ? (int)len : STORAGE_ERR_IO;
}

int storage_fx_file_close(void)
{
    UINT status = fx_file_close(&g_fx_file);
    return (status == FX_SUCCESS) ? STORAGE_OK : STORAGE_ERR_IO;
}

int storage_fx_file_size(const char *path, uint32_t *size)
{
    FX_FILE tmp;
    UINT status = fx_file_open(&g_fx_media, &tmp, (CHAR *)path, FX_OPEN_FOR_READ);
    if (status != FX_SUCCESS) return STORAGE_ERR_NOT_FOUND;
    *size = (uint32_t)tmp.fx_file_current_file_size;
    fx_file_close(&tmp);
    return STORAGE_OK;
}

int storage_fx_file_delete(const char *path)
{
    UINT status = fx_file_delete(&g_fx_media, (CHAR *)path);
    if (status == FX_NOT_FOUND) return STORAGE_ERR_NOT_FOUND;
    return (status == FX_SUCCESS) ? STORAGE_OK : STORAGE_ERR_IO;
}

int storage_fx_file_create(const char *path)
{
    UINT status = fx_file_create(&g_fx_media, (CHAR *)path);
    if (status == FX_ALREADY_CREATED) return STORAGE_ERR_EXISTS;
    return (status == FX_SUCCESS) ? STORAGE_OK : STORAGE_ERR_IO;
}

int storage_fx_file_rename(const char *oldpath, const char *newpath)
{
    UINT status = fx_file_rename(&g_fx_media, (CHAR *)oldpath, (CHAR *)newpath);
    return (status == FX_SUCCESS) ? STORAGE_OK : STORAGE_ERR_IO;
}

int storage_fx_dir_list(const char *path)
{
    FX_LOCAL_PATH local;
    CHAR  name[FX_MAX_LONG_NAME_LEN + 1];
    UINT  status;

    /* 切到目标目录（path == "/" 即根目录）*/
    status = fx_directory_local_path_set(&g_fx_media, &local, (CHAR *)path);
    if (status != FX_SUCCESS) return STORAGE_ERR_NOT_FOUND;

    status = fx_directory_first_entry_find(&g_fx_media, name);
    while (status == FX_SUCCESS) {
        xil_printf("  %s\r\n", name);
        status = fx_directory_next_entry_find(&g_fx_media, name);
    }

    fx_directory_local_path_clear(&g_fx_media);
    return STORAGE_OK;
}











