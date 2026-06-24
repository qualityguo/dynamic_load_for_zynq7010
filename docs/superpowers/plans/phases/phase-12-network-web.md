> **章节导航**：
  [← 上一章：QSPI 迁移](phase-11-qspi.md) ｜ [↑ 返回总述](../2026-06-22-zynq-multistage-boot.md)

> **Phase 元信息**
> - 对应 Spec：`网络（新增）`
> - 里程碑：`M12`
> - 交付物：NetXDuo 集成 + Web 配置页 + 网络升级（复用现有 hello_netxduo 移植）
> - 分章文件：`phases/phase-12-network-web.md`

> **接口定位**：本章对其他 Phase 的依赖与对外暴露的符号，见
> [总述章 · 模块依赖矩阵](../2026-06-22-zynq-multistage-boot.md#3-模块依赖矩阵)。
> 新增功能时，先查该清单确认接口落点，再回本章扩展。

---
## Phase 12：NetXDuo 集成 + Web 配置页 + 网络升级（M12，扩展）

**目标**：在 SSBL 阶段增加网络能力——网线插入后自动启动一个 Web 页面，浏览器内可：
1. 查看 SD 卡 P2 的启动文件列表与内容
2. 上传 `app.bin` / `.bit` 文件（复用现有 staging→CRC→提交升级路径）
3. 在线编辑并保存 `boot.cfg`

**关键背景（已有成熟移植，大幅简化本 Phase）**：用户前期在 `E:/桌面/docs/threadx_for_zynq7010/hello_netxduo` 已完成 NetXDuo 移植且版本完全匹配（ThreadX/FileX/NetXDuo 均 6.4.1）。以下资产已调通，直接复用：
- **NetXDuo 静态库工程** `threadx_for_zynq7010/NetXDuo/`（511 个 .c，v6.4.1）
- **GEM 网卡驱动** `hello_netxduo/src/NetXDuo/Port/nx_driver_zynq.c`（1622 行，DMA/BD/中断/收发全实现，IRQ54）
- **RTL8211E PHY 驱动** `hello_netxduo/src/NetXDuo/Port/rtl8211e_phy.c`（自动协商 + SLCR 时钟，针对实际芯片）
- **协议栈初始化序列** `hello_netxduo/src/netxduo_tcp_server.c` 的 `NetXTest2()`（pool/ip/arp/tcp/udp/icmp 全实测能跑）

**已确认的技术决策**：
- IP 获取：**静态固定 IP**（沿用 hello_netxduo 模式，不依赖 DHCP）
- 传输安全：**HTTP 明文**（省去 TLS）
- Web 实现：**NetXDuo 自带 `nx_web_http_server`**
- 启动时机：**检测到网线链路 up 才启动 Web 服务**（PHY 轮询，符合"网线插上之后开启网页"）

**缺口（本 Phase 真正的新工作量）**：
1. HTTP server addon 源码（现有库只编了 `common` 核心，无 http addon，需从上游 `eclipse-threadx/netxduo` 仓库 `addons/web/` 取）
2. FileX 桥接（HTTP server 读写 SD 卡，开 `NX_WEB_HTTP_SERVER_ENABLE_FILEX`）
3. 运行期 PHY 链路轮询（`nx_driver_zynq.c:1317` 的 `_nx_driver_hardware_get_status` 是空实现，不感知热插拔）
4. HTML 网页资源（从零写）
5. 文件上传处理（HTTP PUT 收文件 → 复用 staging→CRC→提交）
6. handoff 收尾（跳转前停 HTTP server + 释放 GEM 中断）
7. SD 并发互斥（`g_fx_file` 单例需加 `tx_mutex`）

### Task 12.1：拷贝 NetXDuo 库工程 + 补 HTTP server addon 源码

**Files:**
- Create: `ssbl/NetXDuo_A9/`（整份拷贝自 `threadx_for_zynq7010/NetXDuo/`）
- Create: `ssbl/NetXDuo_A9/src/NetXDuo/addons/web/src/nx_web_http_server*.c`
- Create: `ssbl/NetXDuo_A9/src/NetXDuo/addons/web/inc/nx_web_http_server.h`
- Create: `ssbl/NetXDuo_A9/NetXDuo_A9.prj`（仿 `FileX_A9.prj`）

- [ ] **Step 1：整份拷贝 NetXDuo 库工程**

```powershell
# 源：用户前期移植成果
$src = "E:/桌面/docs/threadx_for_zynq7010/NetXDuo"
# 目标：SSBL 工程（与 ThreadX_A9 / FileX_A9 同级）
$dst = "E:/桌面/docs/dynamic_load_for_zynq7010/ssbl/NetXDuo_A9"

# 整份拷贝（common 核心源码，511 个 .c，已调通）
Copy-Item -Path $src - -Destination $dst -Recurse
```

- [ ] **Step 2：从上游仓库补 HTTP server addon 源码**

现有 `NetXDuo/` 库只编了 `common` 核心（无 addon）。HTTP server 在 `addons/web/`。从 `https://github.com/eclipse-threadx/netxduo` 取：

```powershell
# addons/web 目录下需拷的文件（共 ~6 个 .c + 1 个 .h）：
#   nx_web_http_server.c           HTTP server 主实现
#   nx_web_http_server_common.c    GET/PUT/POST 通用处理
#   nx_web_http_server_get.c       GET 处理
#   nx_web_http_server_put.c       PUT 处理（上传用）
#   nx_web_http_server_post.c      POST 处理（表单/cfg 编辑用）
#   nx_web_http_server_request_process.c
# 头文件：
#   nx_web_http_server.h
#   nx_web_http_server_common.h

$addon_src = "<上游仓库>/addons/web/src"
$addon_inc = "<上游仓库>/addons/web/inc"
$dst_src = "ssbl/NetXDuo_A9/src/NetXDuo/addons/web/src"
$dst_inc = "ssbl/NetXDuo_A9/src/NetXDuo/addons/web/inc"

New-Item -ItemType Directory -Force -Path $dst_src, $dst_inc | Out-Null
Copy-Item "$addon_src/*.c" $dst_src
Copy-Item "$addon_inc/*.h" $dst_inc
```

- [ ] **Step 3：配库工程编译宏与 include**

库工程 `.cproject`（仿 `FileX_A9`）关键配置：
- **Include 路径**加：`src/NetXDuo/addons/web/inc`
- **编译宏**（Preprocessor defines）加：
  - `NX_WEB_HTTP_SERVER_ENABLE_FILEX`  ← 让 HTTP server 用 FileX 做文件后端
  - `NX_INCLUDE_USER_DEFINE_FILE`（若用 nx_user.h）
- 确认 `TX_INCLUDE_USER_DEFINE_FILE` 已开（与 ThreadX 库一致）

- [ ] **Step 4：编出 `libNetXDuo.a`，验证 HTTP server 符号**

```bash
# Vitis 编译 NetXDuo_A9 库工程后
arm-none-eabi-nm ssbl/NetXDuo_A9/Debug/libNetXDuo.a | grep nx_web_http_server_create
# 期望：能看到 _nx_web_http_server_create 符号（T 段 = text）
```

- [ ] **Step 5：提交**

```bash
git add ssbl/NetXDuo_A9/
git commit -m "Phase 12.1: vendor-copy NetXDuo lib + HTTP server addon source"
```

---

### Task 12.2：拷贝 GEM/PHY 驱动到 SSBL

**Files:**
- Create: `ssbl/ssbl/src/net/Port/nx_driver_zynq.c`（拷贝）
- Create: `ssbl/ssbl/src/net/Port/nx_driver_zynq.h`（拷贝）
- Create: `ssbl/ssbl/src/net/Port/rtl8211e_phy.c`（拷贝）
- Modify: `ssbl/ssbl/.cproject`（加 `-lNetXDuo` + include 路径）

- [ ] **Step 1：拷贝 GEM 驱动 + PHY 驱动**

```powershell
$src = "E:/桌面/docs/threadx_for_zynq7010/hello_netxduo/src/NetXDuo/Port"
$dst = "ssbl/ssbl/src/net/Port"
New-Item -ItemType Directory -Force -Path $dst | Out-Null
Copy-Item "$src/nx_driver_zynq.c", "$src/nx_driver_zynq.h", "$src/rtl8211e_phy.c" $dst
```

- [ ] **Step 2：SSBL 工程链接配置（`.cproject`）**

仿 `hello_netxduo/.cproject:126-140`：
- **Linker libraries** 加：`-lNetXDuo`（注意顺序：NetXDuo 依赖 ThreadX，故 NetXDuo 在 ThreadX **之前**）
- **Linker library paths** 加：`${workspace_loc:/NetXDuo_A9/Debug}`
- **Include paths** 加：`src/net/Port`、`src/NetXDuo`（nx_api.h 路径）

> **Note（链接顺序）**：NetXDuo 符号依赖 ThreadX（内部用 tx_mutex/tx_thread），故 `-lNetXDuo -lThreadX -lFileX`，NetXDuo 必须在最前。顺序错误会报未定义引用。

- [ ] **Step 3：验证 BD 硬编码地址不与 SSBL 内存冲突（★ 关键陷阱）**

驱动把 DMA BD 环钉死在（`nx_driver_zynq.c:101-102`）：
```c
#define RX_BD_LIST_START_ADDRESS    0x0FF00000
#define TX_BD_LIST_START_ADDRESS    0x0FF10000
```

并 `Xil_SetTlbAttributes(0x0FF00000, 0xc02)` 标记 non-cacheable（DMA 必须）。需确认：
- `0x0FF00000`（255MB）落在 SSBL DDR 区间 `[0x100000, 0x20000000]` 内 ✅
- 不与 SSBL 镜像（<1MB，从 0x100000 起）、staging 区（`0x02000000`=32MB，Phase 8.1 定义）、app 区（`0x01000000`=16MB，Phase 5.1）重叠 ✅
- `Xil_SetTlbAttributes` 调用需在 SSBL MMU 初始化（bsp_init）**之后**执行——驱动内部已在该位置调用，确认 SSBL 的 MMU 映射覆盖 `0x0FF00000` 且允许改属性

```bash
# 跑起来后验证：在驱动初始化后打印 BD 区可读写
# 若 Xil_SetTlbAttributes 失败会触发 Data Abort，串口会挂死
```

- [ ] **Step 4：验证 GEM 中断 IRQ54 与现有中断共存**

GEM0 中断号 = 54（`XPAR_XEMACPS_0_INTR = XPS_GEM0_INT_ID`），通过驱动的 `XScuGic_Connect` 注册。确认：
- SSBL 的 `bsp_init()` 已初始化 `XScuGic xInterruptController`（`handoff.c:27` 引用，同源 hello_netxduo）
- 现有 UART/SD 中断号与 54 不冲突（UART1=53，SDIO 通常 79/82）

- [ ] **Step 5：占位测试 —— 仅链接，不调业务**

在 `main.c` 临时加一行确认能编过：
```c
#include "nx_api.h"
extern VOID nx_driver_zynq(NX_IP_DRIVER *);
/* AppTaskStart 里临时加：*/
xil_printf("[net] driver symbol linked: %p\r\n", (void*)nx_driver_zynq);
```

- [ ] **Step 6：提交**

```bash
git add ssbl/ssbl/src/net/Port/ ssbl/ssbl/.cproject
git commit -m "Phase 12.2: vendor-copy GEM/PHY driver, link NetXDuo into SSBL"
```

---

### Task 12.3：写 net_init.c + net_config.h（协议栈初始化 + 静态 IP + 链路轮询）

**Files:**
- Create: `boot/ssbl/src/net/net_config.h`
- Create: `boot/ssbl/src/net/net_init.c`
- Create: `boot/ssbl/src/net/net.h`
- Modify: `boot/ssbl/src/main.c`（`AppTaskStart` 里插入 `net_init()`）

- [ ] **Step 1：写 net_config.h（网络参数宏）**

```c
/* boot/ssbl/src/net/net_config.h — Phase 12 网络参数
 *
 * 静态 IP（不依赖 DHCP），IP/掩码/网关按现场网段填。
 * hello_netxduo 原值 192.168.28.245/24，SSBL 沿用同一套可改。
 */
#ifndef SSBL_NET_CONFIG_H
#define SSBL_NET_CONFIG_H

#include "nx_api.h"

/* 静态 IPv4 */
#define NET_STATIC_IP       IP_ADDRESS(192,168,1,100)   /* ★ 改成现场网段 */
#define NET_NETMASK         IP_ADDRESS(255,255,255,0)
#define NET_GATEWAY         IP_ADDRESS(192,168,1,1)

/* MAC 地址（与 hello_netxduo nx_driver_zynq.c:40 一致，可改）*/
#define NET_MAC_ADDR0       0x00
#define NET_MAC_ADDR1       0x11
#define NET_MAC_ADDR2       0x22
#define NET_MAC_ADDR3       0x33
#define NET_MAC_ADDR4       0x44
#define NET_MAC_ADDR5       0x56

/* HTTP server */
#define NET_HTTP_PORT       80

/* packet pool：40 包 × 1600 字节 ≈ 64KB */
#define NET_PACKET_COUNT    40
#define NET_PACKET_SIZE     1600

/* PHY 链路轮询周期（ms），用于"网线插上才启动 Web" */
#define NET_PHY_POLL_MS     500

/* GEM 中断号（Zynq GEM0 = 54）*/
#define NET_GEM0_INTR       54

#endif /* SSBL_NET_CONFIG_H */
```

- [ ] **Step 2：写 net.h（对外接口）**

```c
/* boot/ssbl/src/net/net.h — Phase 12 网络模块对外接口
 *
 * 初始化时序：在 AppTaskStart 的 storage_media_open() 之后、countdown_create()
 * 之前调用 net_init()（必须线程上下文，NetX 依赖 ThreadX，同 FileX）。
 *
 * net_init 创建：packet pool + IP 实例（静态 IP）+ 协议栈使能 + 链路轮询线程。
 * 链路轮询线程：周期读 PHY，up 时启动 Web server，down 时停 Web server。
 */
#ifndef SSBL_NET_H
#define SSBL_NET_H

#include <stdint.h>

/* 协议栈 + 链路轮询初始化（线程上下文调）。返回 0=OK，负数=失败。*/
int  net_init(void);

/* 查询链路状态：1=up，0=down。*/
int  net_link_is_up(void);

/* 停止并释放网络资源（handoff 跳 app 前调）。
 * 停 Web server + 删 IP 实例 + 断 GEM 中断。*/
void net_stop(void);

/* 获取当前 IP（点分十进制写入 buf，len>=16）。返回 0=OK。*/
int  net_get_ip(char *buf, int len);

#endif /* SSBL_NET_H */
```

- [ ] **Step 3：写 net_init.c（提炼自 hello_netxduo NetXTest2）**

```c
/* boot/ssbl/src/net/net_init.c — Phase 12 协议栈初始化 + 链路轮询
 *
 * 初始化序列（提炼自 hello_netxduo/src/netxduo_tcp_server.c:98 NetXTest2，
 * 已实测能跑）：
 *   nx_system_initialize
 *     → nx_packet_pool_create
 *     → nx_ip_create（静态 IP + nx_driver_zynq）
 *     → nx_arp_enable / nx_ip_fragment_enable / nx_tcp_enable / nx_icmp_enable
 *     → 创建链路轮询线程
 *
 * 链路轮询：每 NET_PHY_POLL_MS 读 PHY 寄存器 0x11（PHYSR）link bit。
 *   up → 启动 Web server（web_server_start）
 *   down → 停 Web server（web_server_stop）
 *
 * ★ 与 hello_netxduo 的差异：
 *   - 删 UDP（HTTP 不需要，省代码）；保留 TCP/ARP/ICMP（ping 调试 + HTTP 基础）
 *   - 删 socket echo 业务，改为 Web server
 *   - 新增链路热插拔轮询（hello_netxduo 无此逻辑）
 */
#include "net.h"
#include "net_config.h"
#include "nx_api.h"
#include "tx_api.h"
#include "xil_printf.h"
#include "xemacps.h"            /* XEmacPs_PhyRead 用于链路轮询 */

/* 由 Task 12.4 提供 */
extern int  web_server_start(void);
extern void web_server_stop(void);

/* driver 提供的入口（nx_driver_zynq.c）*/
extern VOID nx_driver_zynq(NX_IP_DRIVER *driver_req_ptr);

/* 全局对象（部分需被 web_server.c 引用）*/
NX_PACKET_POOL  g_net_pool;
NX_IP           g_net_ip;

/* 静态存储区（对齐：packet pool 按 NX_PACKET 对齐）*/
static ULONG    s_pool_area[(NET_PACKET_COUNT * (NET_PACKET_SIZE + NX_PACKET_SIZE_ADJUSTED)) / sizeof(ULONG)]
                __attribute__((aligned(32)));
static ULONG    s_arp_area[512 / sizeof(ULONG)];

/* IP 协议栈线程栈（IP 内部用，非应用线程）*/
static uint64_t s_ip_thread_stk[8192 / 8];
static TX_THREAD s_ip_thread;     /* nx_ip_create 内部使用的 IP helper 线程 */
/* ★ 注：nx_ip_create 的第 7~9 参数是它自己用的 IP helper 线程 TCB/栈/优先级，
 *   不是应用线程。hello_netxduo 原样传 AppTaskNetXStk。*/

/* 链路轮询线程 */
static TX_THREAD s_link_thread;
static uint64_t  s_link_stk[4096 / 8];
#define NET_LINK_PRIO    12      /* 低于 cli(10)/trigger(11)，后台轮询 */

/* 链路状态：0=down，1=up */
static volatile int s_link_up = 0;
/* Web server 当前是否启动（避免重复 start/stop）*/
static volatile int s_web_running = 0;

/* PHY 物理地址（RTL8211E 默认 0，见 rtl8211e_phy.c）*/
#define PHY_ADDR          0
#define PHY_REG_PHYSR     0x11    /* RTL8211E PHY Status Register */
#define PHY_PHYSR_LINK    (1<<10) /* bit10 = Link Status */

/* GEM 实例：driver 内部已静态持有，这里只为 PhyRead 提供句柄。
 * hello_netxduo 的 nx_driver_zynq.c 内有全局 XEmacPs，需 extern 拿到。*/
extern XEmacPs *nx_driver_get_emac(void);   /* 由 nx_driver_zynq.c 暴露 */

/* === 读 PHY link 状态 === */
static int phy_link_up(void)
{
    XEmacPs *emac = nx_driver_get_emac();
    if (emac == NULL) return 0;
    UINT data = 0;
    UINT rc = XEmacPs_PhyRead(emac, PHY_ADDR, PHY_REG_PHYSR, &data);
    if (rc != XST_SUCCESS) return 0;
    return (data & PHY_PHYSR_LINK) ? 1 : 0;
}

/* === 链路轮询线程主循环 === */
static void link_thread_entry(ULONG arg)
{
    (void)arg;
    for (;;) {
        int up = phy_link_up();

        if (up && !s_web_running) {
            xil_printf("[net] link up, starting web server\r\n");
            if (web_server_start() == 0) {
                s_web_running = 1;
                s_link_up = 1;
            }
        } else if (!up && s_web_running) {
            xil_printf("[net] link down, stopping web server\r\n");
            web_server_stop();
            s_web_running = 0;
            s_link_up = 0;
        }
        tx_thread_sleep(NET_PHY_POLL_MS / 10);   /* tick=10ms */
    }
}

/* === net_init === */
int net_init(void)
{
    UINT status;

    nx_system_initialize();

    /* packet pool */
    status = nx_packet_pool_create(&g_net_pool, "SSBL Net Pool",
                                   NET_PACKET_SIZE,
                                   s_pool_area,
                                   sizeof(s_pool_area));
    if (status != NX_SUCCESS) {
        xil_printf("[net] pool_create failed: 0x%X\r\n", status);
        return -1;
    }

    /* IP 实例（静态 IP）*/
    status = nx_ip_create(&g_net_ip, "SSBL IP",
                          NET_STATIC_IP, NET_NETMASK,
                          &g_net_pool, nx_driver_zynq,
                          (UCHAR *)s_ip_thread_stk, sizeof(s_ip_thread_stk),
                          3);    /* IP helper 线程优先级 */
    if (status != NX_SUCCESS) {
        xil_printf("[net] ip_create failed: 0x%X\r\n", status);
        return -2;
    }

    /* 协议栈使能（ARP/TCP/ICMP 必需；UDP 不开，HTTP 用不到）*/
    nx_arp_enable(&g_net_ip, s_arp_area, sizeof(s_arp_area));
    nx_ip_fragment_enable(&g_net_ip);
    nx_tcp_enable(&g_net_ip);
    nx_icmp_enable(&g_net_ip);

    /* 链路轮询线程 */
    status = tx_thread_create(&s_link_thread, "net link",
                              link_thread_entry, 0,
                              s_link_stk, sizeof(s_link_stk),
                              NET_LINK_PRIO, NET_LINK_PRIO,
                              TX_NO_TIME_SLICE, TX_AUTO_START);
    if (status != TX_SUCCESS) {
        xil_printf("[net] link thread create failed: 0x%X\r\n", status);
        return -3;
    }

    {
        ULONG ip = 0;
        nx_ip_address_get(&g_net_ip, &ip, &ip);
        xil_printf("[net] init done, static IP configured\r\n");
    }
    return 0;
}

int net_link_is_up(void)   { return s_link_up; }

int net_get_ip(char *buf, int len)
{
    ULONG ip, mask;
    if (nx_ip_address_get(&g_net_ip, &ip, &mask) != NX_SUCCESS) return -1;
    /* 点分十进制 */
    snprintf(buf, len, "%lu.%lu.%lu.%lu",
             (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
             (ip >> 8) & 0xFF, ip & 0xFF);
    return 0;
}

/* === handoff 前调 === */
void net_stop(void)
{
    if (s_web_running) { web_server_stop(); s_web_running = 0; }
    tx_thread_terminate(&s_link_thread);
    nx_ip_delete(&g_net_ip);   /* 内部释放 GEM 中断资源 */
    xil_printf("[net] stopped (GEM IRQ freed)\r\n");
}
```

- [ ] **Step 4：在 driver 暴露 XEmacPs 句柄（nx_driver_zynq.c 小改）**

`nx_driver_zynq.c` 内有静态 `XEmacPs` 实例。加一个 getter 供链路轮询读 PHY：

```c
/* 在 nx_driver_zynq.c 末尾加：*/
XEmacPs *nx_driver_get_emac(void)
{
    return &nx_driver_information.nx_driver_information_interface_XEmacPs;
    /* 实际变量名按驱动内部结构调整，参考 hello_netxduo 原文件 */
}
```

- [ ] **Step 5：接入 main.c 的 AppTaskStart**

修改 `boot/ssbl/src/main.c` 的 `AppTaskStart`（Phase 7 起的初始化编排线程），在 `storage_media_open` 之后、`countdown_create` 之前插入：

```c
#include "net.h"

static void AppTaskStart(ULONG thread_input)
{
    (void)thread_input;
    int rc;

    fx_system_initialize();
    rc = storage_media_open();
    if (rc != STORAGE_OK) { /* … 原 CLI 容错 … */ }

    boot_config_load(&g_runtime_cfg);

    /* ★ Phase 12：网络初始化（静态 IP + 链路轮询）。
     *    放这里：① 依赖 ThreadX（必须线程上下文，同 FileX）
     *           ② 在 boot_config 之后，因 Web 的 bootcfg 编辑要用 cfg 模块
     *           ③ 在 countdown 之前，确保 boot 窗口期 Web 可用 */
    net_init();

    cli_uart_init();
    cli_trigger_init();
    cli_create();
    countdown_create();

    tx_thread_terminate(tx_thread_identify());
}
```

> **Note（初始化失败处理）**：`net_init()` 失败（如 GEM 没起来）只告警继续，**不阻断启动**——与 spec §5.3 "容错退回可用通道"一致：网不通还有 UART CLI。`net_init` 内部已用 xil_printf 告警，AppTaskStart 不必额外处理返回值。

- [ ] **Step 6：验证（spec §12 风格）**

| 场景 | 操作 | 期望 |
|---|---|---|
| 静态 IP 生效 | 网线连 PC（同网段），上电 | 串口 `[net] init done, static IP configured` |
| ping 通 | PC `ping 192.168.1.100` | 收到 reply（ICMP 已使能）|
| 链路热插拔 | 启动后拔网线→插回 | 串口交替打 `link down` / `link up, starting web server`（本步 web_server 是 stub，下个 Task 实现）|
| 启动失败容错 | 故意改错 IP/掩码 | 串口告警，但 CLI/boot 正常 |

- [ ] **Step 7：提交**

```bash
git add boot/ssbl/src/net/net_config.h boot/ssbl/src/net/net.h \
        boot/ssbl/src/net/net_init.c boot/ssbl/src/main.c \
        boot/ssbl/src/net/Port/nx_driver_zynq.c
git commit -m "Phase 12.3: net_init (static IP + protocol stack + PHY link polling)"
```

---

### Task 12.4：写 web_server.c（HTTP server + 路由回调 + SD 互斥）

**Files:**
- Create: `boot/ssbl/src/net/web_server.c`
- Create: `boot/ssbl/src/net/web_server.h`
- Modify: `boot/ssbl/storage/storage_fx_glue.c`（加 `TX_MUTEX g_storage_mutex`）

- [ ] **Step 1：先给 storage 层加并发互斥（★ 必需前置）**

现有 `g_fx_file` 是单例（`storage_fx_glue.c:14`），CLI 与 HTTP Server 若同时操作 SD 会破坏文件状态。加一把互斥锁：

```c
/* storage_fx_glue.c 顶部加：*/
#include "tx_api.h"
TX_MUTEX g_storage_mutex;   /* CLI 与 HTTP 访问 SD 的串行化锁 */

/* storage_media_open 里创建一次：*/
tx_mutex_create(&g_storage_mutex, "storage", TX_NO_INHERIT);
```

```c
/* storage_fx_glue.c 的每个 file 操作（open/read/write/close/list）前后加锁。
 * 以 file_read 为例：*/
int storage_fx_file_read(void *buf, uint32_t len)
{
    tx_mutex_get(&g_storage_mutex, TX_WAIT_FOREVER);
    /* … 原 fx_file_read 逻辑 … */
    tx_mutex_put(&g_storage_mutex);
    return rc;
}
```

> **Note**：若不想侵入每个函数，可在上层（web_server 回调 + cli_commands）按"事务"粒度加锁（open 到 close 之间持锁）。后者更省锁开销但易遗漏，本计划推荐前者（侵入小、安全）。

- [ ] **Step 2：写 web_server.h**

```c
/* boot/ssbl/src/net/web_server.h — Phase 12 HTTP server 接口
 *
 * 由 net_init 的链路轮询线程在 link up 时调 web_server_start，
 * link down 时调 web_server_stop。不直接被 main 调。
 */
#ifndef SSBL_WEB_SERVER_H
#define SSBL_WEB_SERVER_H

int  web_server_start(void);   /* 创建+启动 HTTP server，监听 NET_HTTP_PORT */
void web_server_stop(void);    /* 停止+删除 HTTP server，释放 socket */

#endif
```

- [ ] **Step 3：写 web_server.c（HTTP server 骨架 + 路由）**

```c
/* boot/ssbl/src/net/web_server.c — Phase 12 HTTP server
 *
 * 用 NetXDuo nx_web_http_server 组件，监听 80。注册 user-defined request
 * callback，把 URL 路由到现有 storage / boot_config / boot_selector 逻辑：
 *
 *   GET  /                  → 内嵌 index.html（web_pages.h）
 *   GET  /api/files         → storage_dir_list（P2 文件 JSON）
 *   GET  /api/bootcfg       → storage_file_read("boot.cfg")
 *   PUT  /upload/<name>     → 收数据流写 staging → CRC 校验 → .tmp+rename 提交
 *   POST /api/bootcfg       → 解析 body → boot_config_save
 *
 * ★ 复用原则：所有文件操作走 storage_ops_t 宏，不直接调 FileX；
 *            上传走 boot_selector_load_only 的 staging→校验→提交流程。
 */
#include "web_server.h"
#include "net_config.h"
#include "nx_api.h"
#include "nx_web_http_server.h"
#include "storage.h"
#include "boot_config.h"
#include "tx_api.h"
#include "xil_printf.h"
#include <string.h>
#include <stdio.h>

/* 前端页面（Task 12.5 提供）*/
extern const char  index_html[];
extern const UINT  index_html_len;

/* === HTTP server 实例与栈 === */
static NX_WEB_HTTP_SERVER  s_http_server;
static uint8_t             s_http_stack[16384];

/* === 路由回调：user-defined request 处理入口 === */
static UINT http_request_callback(NX_WEB_HTTP_SERVER *server,
                                  UINT request_type,
                                  CHAR *resource, CHAR *query,
                                  NX_PACKET *packet_ptr)
{
    /* GET / */
    if (request_type == NX_WEB_HTTP_SERVER_GET_REQUEST &&
        strcmp(resource, "/") == 0)
    {
        nx_web_http_server_callback_data_send(server,
            (void *)index_html, index_html_len);
        return NX_WEB_HTTP_SERVER_CALLBACK_COMPLETED;
    }

    /* GET /api/files → 列 P2 文件（JSON）
     * 复用 storage_dir_list，但需把输出转成 JSON。简化：storage_dir_list
     * 改为填一个 buffer，或 web 层自己遍历 fx_directory_first/next。
     * 这里给 JSON 拼接骨架，实际 dir 迭代 API 见 FileX fx_directory_*。*/
    if (request_type == NX_WEB_HTTP_SERVER_GET_REQUEST &&
        strcmp(resource, "/api/files") == 0)
    {
        char json[1024];
        int n = 0;
        n += snprintf(json + n, sizeof(json) - n, "{\"files\":[");
        /* TODO: fx_directory_first/next 遍历 P2 填文件名（Task 实施时补）
         *   复用 storage 层；若 storage_ops_t 无 dir 迭代，扩展一个
         *   storage_dir_iter_begin/next。*/
        n += snprintf(json + n, sizeof(json) - n, "]}");
        nx_web_http_server_callback_data_send(server, json, n);
        return NX_WEB_HTTP_SERVER_CALLBACK_COMPLETED;
    }

    /* GET /api/bootcfg → 返回 boot.cfg 原文 */
    if (request_type == NX_WEB_HTTP_SERVER_GET_REQUEST &&
        strcmp(resource, "/api/bootcfg") == 0)
    {
        char buf[2048];
        int rc = storage_file_open("boot.cfg", STORAGE_OPEN_READ);
        if (rc != STORAGE_OK) return NX_NOT_FOUND;
        int n = storage_file_read(buf, sizeof(buf) - 1);
        storage_file_close();
        if (n < 0) return NX_NOT_FOUND;
        buf[n] = '\0';
        nx_web_http_server_callback_data_send(server, buf, n);
        return NX_WEB_HTTP_SERVER_CALLBACK_COMPLETED;
    }

    /* POST /api/bootcfg → 解析 body → boot_config_save（原子写）
     * ★ 复用 boot_config 模块的 apply_kv + save 逻辑。*/
    if (request_type == NX_WEB_HTTP_SERVER_POST_REQUEST &&
        strcmp(resource, "/api/bootcfg") == 0)
    {
        /* 1. 从 packet_ptr 读 POST body（URL-encoded 或纯文本 cfg）
         * 2. 写到 boot.cfg.tmp + rename（调 boot_config_save）
         * 详细 packet 数据提取见 nx_packet_data_retrieve。*/
        /* TODO: Task 实施时补 packet 读取 + apply_kv 解析。
         *   复用 config/boot_config.c 的 parse_text 逻辑。*/
        return NX_WEB_HTTP_SERVER_CALLBACK_COMPLETED;
    }

    /* PUT /upload/<name> → 收文件流 → staging → CRC → 提交
     * ★ 复用 boot_selector_load_only 的 staging→校验→提交流程。
     *   与 YMODEM 走同一个 staging area（0x02000000）。*/
    if (request_type == NX_WEB_HTTP_SERVER_PUT_REQUEST &&
        strncmp(resource, "/upload/", 8) == 0)
    {
        const char *filename = resource + 8;
        /* 1. PUT 数据流分块写到 staging（STAGING_AREA_ADDR）
         * 2. image_header 校验 + CRC32（复用 image_loader_load）
         * 3. .tmp + rename 原子提交到 P2（复用 boot_selector_load_only）
         * TODO: Task 实施时补。*/
        xil_printf("[web] upload %s received (processing)\r\n", filename);
        return NX_WEB_HTTP_SERVER_CALLBACK_COMPLETED;
    }

    /* 未匹配 → 交给 server 默认处理（404）*/
    return NX_SUCCESS;
}

/* === web_server_start === */
int web_server_start(void)
{
    UINT status;
    status = nx_web_http_server_create(&s_http_server,
        "SSBL HTTP Server", &g_net_ip, NET_HTTP_PORT,
        &g_net_pool,   /* 由 net_init.c 提供的全局 pool */
        http_request_callback,   /* ★ 路由回调 */
        s_http_stack, sizeof(s_http_stack));
    if (status != NX_SUCCESS) {
        xil_printf("[web] server_create failed: 0x%X\r\n", status);
        return -1;
    }
    status = nx_web_http_server_start(&s_http_server);
    if (status != NX_SUCCESS) {
        xil_printf("[web] server_start failed: 0x%X\r\n", status);
        return -2;
    }
    xil_printf("[web] server listening on :%d\r\n", NET_HTTP_PORT);
    return 0;
}

/* === web_server_stop === */
void web_server_stop(void)
{
    nx_web_http_server_stop(&s_http_server);
    nx_web_http_server_delete(&s_http_server);
    xil_printf("[web] server stopped\r\n");
}
```

> **Note（callback 机制）**：NetXDuo HTTP server 的 user-defined callback 在 GET/PUT/POST 各阶段触发（`get_start`/`get_done`/`put`/`put_done` 等）。上面用单一 `http_request_callback` 做路由分发是简化骨架；实际 API 版本可能要求多个回调（`NX_WEB_HTTP_SERVER_MIME_CALLBACK`、`NX_WEB_HTTP_SERVER_GET_CALLBACK` 等），按 `nx_web_http_server.h` 的 typedef 实际签名调整。Task 实施时以 `hello_netxduo` 已验证的 NetXDuo 6.4.1 头文件为准。

- [ ] **Step 4：验证（spec §12 风格）**

| 场景 | 操作 | 期望 |
|---|---|---|
| 页面访问 | 浏览器开 `http://192.168.1.100/` | 看到 index.html（下个 Task 填充前是占位文本）|
| 文件列表 | 浏览器 `http://192.168.1.100/api/files` | 返回 `{"files":[...]}` JSON |
| 读 cfg | `http://192.168.1.100/api/bootcfg` | 返回 boot.cfg 原文 |
| SD 互斥 | 上传同时敲 CLI `ls` | 无崩溃（g_storage_mutex 串行化）|

- [ ] **Step 5：提交**

```bash
git add boot/ssbl/src/net/web_server.c boot/ssbl/src/net/web_server.h \
        boot/ssbl/storage/storage_fx_glue.c
git commit -m "Phase 12.4: HTTP server with routing + storage mutex"
```

---

### Task 12.5：写 web_pages.h（前端页面 + JS）

**Files:**
- Create: `boot/ssbl/src/net/web_pages.h`

- [ ] **Step 1：写 web_pages.h（内嵌单文件 HTML）**

```c
/* boot/ssbl/src/net/web_pages.h — Phase 12 前端页面（内嵌）
 *
 * 单文件 HTML（含内联 CSS + JS），ROM 化为 C 字符串数组。
 * HTTP server 的 GET / 直接下发。无外部资源依赖（无 CDN、无外链）。
 *
 * 三个功能区：
 *   1. 文件列表（fetch /api/files，AJAX 刷新）
 *   2. boot.cfg 编辑器（textarea + fetch POST /api/bootcfg 保存）
 *   3. 文件上传（input file + fetch PUT /upload/<name>）
 */
#ifndef SSBL_WEB_PAGES_H
#define SSBL_WEB_PAGES_H

#include "nx_api.h"   /* UINT */

static const char index_html[] =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<title>SSBL Boot Config</title>"
"<style>"
"body{font-family:monospace;margin:20px;max-width:800px}"
"h2{border-bottom:2px solid #333;padding-bottom:4px}"
"textarea{width:100%;height:180px;font-family:monospace}"
"button{padding:6px 14px;margin:4px 0}"
".ok{color:green}.err{color:red}"
"</style></head><body>"
"<h2>SSBL 启动配置</h2>"

/* 1. 文件列表 */
"<h3>SD 卡 P2 文件</h3>"
"<button onclick='loadFiles()'>刷新</button>"
"<div id='files'></div>"

/* 2. boot.cfg 编辑 */
"<h3>boot.cfg</h3>"
"<textarea id='cfg'></textarea><br>"
"<button onclick='saveCfg()'>保存</button>"
"<span id='cfgStat'></span>"

/* 3. 文件上传 */
"<h3>上传文件</h3>"
"<input type='file' id='upFile'>"
"<button onclick='upload()'>上传</button>"
"<span id='upStat'></span>"

"<script>"
"function loadFiles(){"
"fetch('/api/files').then(r=>r.json()).then(d=>{"
"document.getElementById('files').innerHTML="
"d.files.map(f=>'<div>'+f+'</div>').join('');"
"});}"
"function loadCfg(){"
"fetch('/api/bootcfg').then(r=>r.text()).then(t=>{"
"document.getElementById('cfg').value=t;});}"
"function saveCfg(){"
"var t=document.getElementById('cfg').value;"
"fetch('/api/bootcfg',{method:'POST',body:t}).then(()=>{"
"document.getElementById('cfgStat').innerHTML="
"'<span class=ok>已保存</span>';});}"
"function upload(){"
"var f=document.getElementById('upFile').files[0];"
"if(!f)return;fetch('/upload/'+f.name,{method:'PUT',body:f}).then(()=>{"
"document.getElementById('upStat').innerHTML="
"'<span class=ok>上传完成</span>';loadFiles();});}"
"loadFiles();loadCfg();"
"</script></body></html>";

static const UINT index_html_len = sizeof(index_html) - 1;

#endif /* SSBL_WEB_PAGES_H */
```

> **Note（体积）**：上面约 1.5KB，ROM 化进 `.rodata`，对 SSBL（DDR 共 ~511MB）无压力。若页面复杂化，考虑 gzip 预压缩 + `Content-Encoding: gzip`，但本期不需要。

- [ ] **Step 2：验证（spec §12 风格）**

| 场景 | 操作 | 期望 |
|---|---|---|
| 页面完整 | 浏览器访问 `/` | 看到三个功能区，样式正常 |
| 列表刷新 | 点"刷新" | 文件列表出现（boot.cfg/app_*.bin 等）|
| cfg 回显 | 页面加载 | textarea 自动填入当前 boot.cfg |
| 改 cfg 保存 | 编辑 textarea → 点"保存" → 断电重启 | 重启后 `cat boot.cfg` 是新内容（证明走 boot_config_save 原子写）|
| 上传 app.bin | 选文件 → 点"上传" | 上传完成后文件列表出现新文件，`boot` 命令能加载 |

- [ ] **Step 3：提交**

```bash
git add boot/ssbl/src/net/web_pages.h
git commit -m "Phase 12.5: embedded HTML config page with AJAX file/upload/cfg"
```

---

### Task 12.6：handoff 收尾（跳 app 前释放网络资源）

**Files:**
- Modify: `boot/ssbl/src/handoff/handoff.c`

- [ ] **Step 1：在 jump_to_app 停 GIC 前加 net_stop**

修改 `boot/ssbl/src/handoff/handoff.c` 的 `jump_to_app`（Phase 5.6 / 7）。在现有"停 Private Timer"（`PTIMER_CONTROL = 0`）之前、停 ThreadX 业务线程之后插入网络收尾：

```c
#include "net.h"   /* net_stop */

void jump_to_app(uint32_t app_load_addr)
{
    /* 1. 停 ThreadX 业务线程（原逻辑不变）*/
    TX_THREAD *self = tx_thread_identify();
    if (cli_thread && cli_thread != self) tx_thread_terminate(cli_thread);
    if (countdown_thread && countdown_thread != self) tx_thread_terminate(countdown_thread);
    if (trigger_thread && trigger_thread != self) tx_thread_terminate(trigger_thread);

    /* ★ Phase 12：停网络（Web server + IP 实例 + GEM 中断）。
     *    必须在停 GIC 之前，否则 nx_ip_delete 内部断 GIC 中断会失败。
     *    若 app 不接管网络，GEM 中断残留会导致 app 收到意外中断而异常。*/
    net_stop();

    /* 2. 让调度器跑一轮 */
    tx_thread_relinquish();

    /* 4. 停 Private Timer */
    PTIMER_CONTROL = 0;
    PTIMER_ISR     = 1;

    /* 5. 停 GIC */
    XScuGic_Stop(&xInterruptController);

    /* … handoff_exit 原逻辑不变 …*/
}
```

> **Note（顺序约束）**：`net_stop` 必须在 `XScuGic_Stop` **之前**——`nx_ip_delete` 内部会调 `XScuGic_Disconnect` 断开 GEM 的 IRQ54 注册，而 GIC 已 stop 后该调用无效。顺序颠倒会导致 IRQ54 仍在 GIC vector table，app 启用 GIC 后收到残留中断。

- [ ] **Step 2：验证（spec §12 风格）**

| 场景 | 操作 | 期望 |
|---|---|---|
| Web 触发 boot | 页面未提供 boot 按钮时，靠 boot_delay 超时自动 boot | app 正常启动，无 GEM 残留中断 |
| 串口收尾日志 | 自动 boot 时观察串口 | `[net] stopped (GEM IRQ freed)` → `[handoff] cleanup done` → `[APP] Hello` |
| app 独立运行 | app 不初始化网络也能跑 | app 不收到任何网络中断（IRQ54 已断）|

- [ ] **Step 3：提交**

```bash
git add boot/ssbl/src/handoff/handoff.c
git commit -m "Phase 12.6: handoff releases network resources before jump"
```

---

### Task 12.7：端到端回归（spec §12 风格整合）

- [ ] **Step 1：完整流程验证**

| # | 场景 | 操作 | 期望 |
|---|---|---|---|
| 1 | 不插网线启动 | 上电不插网线 | 启动流程与 Phase 11 完全一致，零回归；串口无 `[net] link up` |
| 2 | 插网线启动 | 上电前插好网线 | `[net] init done` → `link up, starting web server` → `[web] listening` |
| 3 | 浏览器访问 | PC 开 `http://192.168.1.100/` | 看到配置页（三功能区）|
| 4 | 列文件 | 点刷新 | 显示 boot.cfg / app_*.bin / pl_*.bit |
| 5 | 读 cfg | 页面加载 | textarea 显示当前 boot.cfg |
| 6 | 改 cfg 保存 | 编辑→保存→断电→重启 | 新 boot.cfg 生效（证明 boot_config_save 原子写）|
| 7 | 上传 app.bin | 选文件→上传→`boot` 命令 | 新 app 能被加载启动（复用 staging→CRC→提交）|
| 8 | 链路热插拔 | 运行中拔/插网线 | 串口交替 `link down`/`link up`；Web 自动停/启 |
| 9 | handoff 无残留 | 自动 boot 到 app | app 正常运行，无 GEM 中断异常 |

- [ ] **Step 2：回归 spec §12.2 原有场景**

确认 Phase 12 不破坏 Phase 0-11 的任何功能：
- CLI 子系统（串口触发、命令集）不受网线影响
- YMODEM 升级与 Web 上传不冲突（g_storage_mutex 串行化）
- boot.cfg 的 `.tmp + rename` 原子性未被 Web 路径破坏

任一失败，回查 `g_storage_mutex` 是否覆盖了所有 storage 操作。

- [ ] **Step 3：提交 + 记录**

```bash
git add docs/superpowers/plans/2026-06-22-zynq-multistage-boot.md
git commit -m "Phase 12: NetXDuo + Web config page verified end-to-end (M12)"
```

---

## Phase 12 风险与注意事项（★ 实施时必读）

1. **BD 地址 `0x0FF00000` + TLB 属性**：DMA 描述符区必须 non-cacheable，`Xil_SetTlbAttributes(0x0FF00000, 0xc02)` 需在 SSBL MMU 初始化后执行（Task 12.2 Step 3 重点验证）。
2. **GEM 中断 IRQ54 与 GIC 收尾顺序**：`net_stop` 必须在 `XScuGic_Stop` 之前，否则 IRQ54 残留（Task 12.6 Step 1）。
3. **链接顺序**：`-lNetXDuo` 必须在 `-lThreadX` **之前**（NetXDuo 符号依赖 ThreadX），否则未定义引用（Task 12.2 Step 2）。
4. **`.prj` 历史绝对路径**：拷贝来的 NetXDuo_A9.prj / hello_netxduo driver 文件若带 `D:/RTOS_Study/...` 旧路径，导入 Vitis 后需重新解析 platform 引用。
5. **SD 并发**：`g_fx_file` 单例，Web 与 CLI 不能同时操作 → `g_storage_mutex`（Task 12.4 Step 1）。
6. **HTTP addon 宏**：`NX_WEB_HTTP_SERVER_ENABLE_FILEX` 必须在重编库时打开，否则 server 无文件后端（Task 12.1 Step 3）。
7. **boot.cfg 原子性**：Web POST 走 `boot_config_save`（已有 `.tmp + rename`），断电安全，无需额外处理。
8. **PHY 寄存器位**：`PHY_REG_PHYSR`/`PHY_PHYSR_LINK`（0x11/bit10）针对 RTL8211E。若板子换 PHY（如 TI/Marvell），寄存器地址和 link 位需相应改（Task 12.3 Step 3 的 `phy_link_up`）。

---

## Phase 12 不在本期范围

- HTTPS/TLS（需 `nx_secure_*`，代码体积显著增加）
- DHCP / AutoIP（已选静态 IP；若需可后加 `nx_dhcp_*` addon）
- mDNS（`http://zynq-ssbl.local`，体验向）
- TFTP server（除 Web 外另一上传通道，`nxd_tftp_server` addon）
- 网络升级的 RSA 签名校验（当前镜像格式只支持 CRC32，安全启动是独立的 Phase）

---
