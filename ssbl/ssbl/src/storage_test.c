/*
*********************************************************************************************************
*
*	模块名称 : FileX / SD 存储层自测（Phase 4，Task 4.4）
*	文件名称 : storage_test.c
*	版    本 : V1.0
*	说    明 : 临时验证线程：挂载 P2 后做 ls + cat boot.cfg，再做一遍
*	           写/读/校验/删除的 round-trip 自测，确认 FileX + SD 链路可用。
*	           Phase 7（CLI）上线后由 cli_commands.c 的 ls/cat 命令替代，
*	           届时本文件可移除。
*
*	运行前提：SD 卡已按 spec §5.5 双分区格式化（P1=BOOT.BIN，P2=数据），
*	          P2 上可放一个 boot.cfg 用于读取验证（没有也只告警，不判失败）。
*
*********************************************************************************************************
*/

#include "includes.h"
#include "storage.h"
#include "image_header.h"
#include "handoff.h"

#define STORAGE_TEST_PRIO       15u
#define STORAGE_TEST_STK_SIZE   16384u

/* Phase 5（Task 5.7）：自测后加载并跳转的 app 镜像名（位于 P2 数据分区根目录）。
 * 由 pack_app.py 打包后拷到 SD 卡 P2。Phase 6 起 app 名由 boot.cfg 决定。 */
#define SSBL_APP_PATH           "app_current.bin"

/* round-trip 自测用的临时文件名（带前后缀，避免与真实文件冲突） */
#define STORAGE_TEST_FILE       "__stor_test__.bin"
/* 写入测试 payload 的固定 pattern 与长度 */
#define STORAGE_TEST_PATTERN    0x5Au
#define STORAGE_TEST_LEN        512u

/* 计数测试通过/失败项，便于串口一眼看出结论 */
static int g_pass = 0;
static int g_fail = 0;

#define TEST_CHECK(cond, name)                                          \
    do {                                                                \
        if (cond) { g_pass++; xil_printf("  [PASS] %s\r\n", name); }    \
        else      { g_fail++; xil_printf("  [FAIL] %s\r\n", name); }    \
    } while (0)

/*
*********************************************************************************************************
*                                ls：列出根目录，验证只挂了 P2
*********************************************************************************************************
*/
static void storage_test_list_root(void)
{
    int rc;

    xil_printf("[stor_test] ---- ls / ----\r\n");
    rc = storage_dir_list("/");
    TEST_CHECK(rc == STORAGE_OK, "dir_list(/)");

    /* 隔离自检提示：若这里能看到 BOOT.BIN，说明误挂了 P1（spec §5.5 失效） */
    xil_printf("[stor_test] (隔离自检：上方列表不应出现 BOOT.BIN)\r\n");
}

/*
*********************************************************************************************************
*                                cat：读 boot.cfg（若存在）
*********************************************************************************************************
*/
static void storage_test_cat(const char *path)
{
    char buf[256];
    int  rc;
    int  n;

    rc = storage_file_open(path, STORAGE_OPEN_READ);
    if (rc != STORAGE_OK) {
        xil_printf("[stor_test] %s not found (OK for Phase 4)\r\n", path);
        return;
    }

    n = storage_file_read(buf, sizeof(buf) - 1);
    storage_file_close();

    if (n > 0) {
        buf[n] = '\0';
        xil_printf("[stor_test] ---- cat %s (%d bytes) ----\r\n", path, n);
        xil_printf("%s\r\n", buf);
        TEST_CHECK(1, "read boot.cfg");
    } else {
        xil_printf("[stor_test] read %s returned %d\r\n", path, n);
        TEST_CHECK(0, "read boot.cfg");
    }
}

/*
*********************************************************************************************************
*                round-trip：create → write → size → read → verify → delete
*********************************************************************************************************
*/
static void storage_test_roundtrip(void)
{
    static uint8_t wr[STORAGE_TEST_LEN];
    static uint8_t rd[STORAGE_TEST_LEN];
    uint32_t size = 0;
    int rc, n, i, mismatch = 0;

    xil_printf("[stor_test] ---- round-trip %s ----\r\n", STORAGE_TEST_FILE);

    /* 干净起见：先删可能残留的测试文件 */
    storage_file_delete(STORAGE_TEST_FILE);

    /* FileX 区分 create 与 open：写新文件必须先 create 再 open(write)。 */
    rc = storage_file_create(STORAGE_TEST_FILE);
    TEST_CHECK(rc == STORAGE_OK, "create");
    if (rc != STORAGE_OK) return;

    /* 写：打开(写) → 写固定 pattern → 关闭 */
    rc = storage_file_open(STORAGE_TEST_FILE, STORAGE_OPEN_WRITE);
    TEST_CHECK(rc == STORAGE_OK, "open(write)");
    if (rc != STORAGE_OK) return;

    for (i = 0; i < STORAGE_TEST_LEN; i++) {
        wr[i] = (uint8_t)(STORAGE_TEST_PATTERN ^ (i & 0xFF));
    }
    rc = storage_file_write(wr, STORAGE_TEST_LEN);
    TEST_CHECK(rc == (int)STORAGE_TEST_LEN, "write");
    storage_file_close();

    /* size：file_size 应等于写入长度 */
    rc = storage_file_size(STORAGE_TEST_FILE, &size);
    TEST_CHECK(rc == STORAGE_OK && size == STORAGE_TEST_LEN, "file_size");

    /* 读：打开(读) → 全量读 → 关闭 */
    rc = storage_file_open(STORAGE_TEST_FILE, STORAGE_OPEN_READ);
    TEST_CHECK(rc == STORAGE_OK, "open(read)");
    if (rc != STORAGE_OK) goto cleanup;

    n = storage_file_read(rd, STORAGE_TEST_LEN);
    TEST_CHECK(n == (int)STORAGE_TEST_LEN, "read len");
    storage_file_close();

    /* 校验内容一致 */
    for (i = 0; i < STORAGE_TEST_LEN; i++) {
        if (rd[i] != (uint8_t)(STORAGE_TEST_PATTERN ^ (i & 0xFF))) {
            mismatch++;
        }
    }
    TEST_CHECK(n == (int)STORAGE_TEST_LEN && mismatch == 0, "verify content");

cleanup:
    /* 删除：清理测试文件 */
    rc = storage_file_delete(STORAGE_TEST_FILE);
    TEST_CHECK(rc == STORAGE_OK, "delete");
}

/*
*********************************************************************************************************
*                                测试线程主体
*********************************************************************************************************
*/
static void storage_test_thread(ULONG thread_input)
{
    (void)thread_input;
    int rc;

    xil_printf("[stor_test] ===== Phase 4 FileX/SD self-test start =====\r\n");

    /* fx_system_initialize 内部创建系统互斥锁（tx_mutex_create），必须在
     * 线程上下文调用——pre-kernel 调会使该锁半初始化，后续 fx_media_open
     * 使用时触发 Data Abort。故从 main 移到此处。 */
    fx_system_initialize();

    /* fx_media_open 必须在线程上下文调用（见 main.c 注释）。此处挂载 P2。 */
    rc = storage_media_open();
    if (rc != STORAGE_OK) {
        xil_printf("[stor_test] storage_media_open failed (%d), abort\r\n", rc);
        tx_thread_suspend(tx_thread_identify());
    }
    xil_printf("[stor_test] storage media opened (P2)\r\n");

    storage_test_list_root();
    storage_test_cat("boot.cfg");
    storage_test_roundtrip();

    xil_printf("[stor_test] ===== done: %d passed, %d failed =====\r\n",
               g_pass, g_fail);

    /* Phase 5（Task 5.7）：自测通过后加载 app.bin 并跳转到 app，端到端验证
     * SSBL → app handoff。Phase 6 会用真正的 boot_selector（读 boot.cfg 选 app）
     * 替代这段临时 load+jump。app 落在 SD 卡 P2 数据分区（spec §5.5）。 */
    {
        extern int image_loader_load(const char *path, uint32_t *out_load_addr);
        extern void jump_to_app(uint32_t app_load_addr);
        uint32_t load_addr = 0;
        int rc2 = image_loader_load(SSBL_APP_PATH, &load_addr);
        if (rc2 == STORAGE_OK) {
            xil_printf("[SSBL] handoff to app @0x%08X\r\n", (unsigned)load_addr);
            jump_to_app(load_addr);    /* 不返回 */
        } else {
            xil_printf("[SSBL] load %s failed (%d), staying in SSBL\r\n",
                       SSBL_APP_PATH, rc2);
        }
    }

    /* load 失败才走到这里：挂起自身，不占调度 */
    tx_thread_suspend(tx_thread_identify());
}

/*
*********************************************************************************************************
*                                创建测试线程
*********************************************************************************************************
*/
void storage_test_create(void)
{
    static TX_THREAD tcb;
    static uint64_t  stk[STORAGE_TEST_STK_SIZE / 8];

    tx_thread_create(&tcb, "storage_test", storage_test_thread, 0,
                     &stk[0], STORAGE_TEST_STK_SIZE,
                     STORAGE_TEST_PRIO, STORAGE_TEST_PRIO,
                     TX_NO_TIME_SLICE, TX_AUTO_START);
}

/***************************** (END OF FILE) *********************************/
