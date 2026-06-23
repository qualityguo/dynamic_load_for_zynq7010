#ifndef __STOARGE_H_
#define __STOARGE_H_

#include <stdint.h>

/* storage 模式（open/file_open 用）*/
#define STORAGE_OPEN_READ   0x01
#define STORAGE_OPEN_WRITE  0x02
#define STORAGE_OPEN_RDWR   (STORAGE_OPEN_READ | STORAGE_OPEN_WRITE)


/* storage 错误码（负数；0 = OK）*/
#define STORAGE_OK              0
#define STORAGE_ERR_OPEN        (-1)
#define STORAGE_ERR_NO_MEDIA    (-2)
#define STORAGE_ERR_NOT_FOUND   (-3)
#define STORAGE_ERR_IO          (-4)
#define STORAGE_ERR_EXISTS      (-5)
#define STORAGE_ERR_INVAL       (-6)


typedef struct {
    int  (*media_open)  (void);
    int  (*media_close) (void);
    int  (*file_open)   (const char *path, int mode);
    int  (*file_read)   (void *buf, uint32_t len);
    int  (*file_write)  (const void *buf, uint32_t len);
    int  (*file_close)  (void);
    int  (*file_size)   (const char *path, uint32_t *size);
    int  (*file_create) (const char *path);
    int  (*file_delete) (const char *path);
    int  (*file_rename) (const char *oldpath, const char *newpath);
    int  (*dir_list)    (const char *path);
} storage_ops_t;

/* 由 sd_port.c 或 qspi_port.c 提供实例 */
extern const storage_ops_t *g_storage;

/* 便捷包装宏（上层只调这些）*/
#define storage_media_open()        (g_storage->media_open())
#define storage_media_close()       (g_storage->media_close())
#define storage_file_open(p,m)      (g_storage->file_open(p,m))
#define storage_file_read(b,l)      (g_storage->file_read(b,l))
#define storage_file_write(b,l)     (g_storage->file_write(b,l))
#define storage_file_close()        (g_storage->file_close())
#define storage_file_size(p,s)      (g_storage->file_size(p,s))
#define storage_file_create(p)      (g_storage->file_create(p))
#define storage_file_delete(p)      (g_storage->file_delete(p))
#define storage_file_rename(o,n)    (g_storage->file_rename(o,n))
#define storage_dir_list(p)         (g_storage->dir_list(p))


#endif /* __STOARGE_H_ */
