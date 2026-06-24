# Zynq-7000 多级启动代码 — 实现计划（总述）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **文档结构说明**：本计划已按 Phase 拆分为独立分章，便于单独阅读、维护和并行实现。
> - **本文件（总述）**：项目目标、章节索引、模块依赖矩阵、公共符号清单、扩展指南
> - **分章**：`phases/phase-NN-*.md`，每章一个 Phase，含导航/元信息/接口定位 + 完整 Task
> - **最终校验**：`final-acceptance.md`，跨 Phase 综合验收与回归矩阵
> - **归档**：`2026-06-22-zynq-multistage-boot_archive.md`，拆分前的单文件完整版（只读，供全文检索）

---

## 1. 项目目标

实现一个 4 级启动链（BootROM → 精简 FSBL → SSBL/ThreadX+FileX → 独立 app），SSBL 根据 SD 卡上的 `boot.cfg` 动态选择 PL bitstream 与 app，并通过串口 CLI + YMODEM 提供现场升级能力；后期扩展网络（Web 配置页 + 网络升级）。

**Architecture:** FSBL 裁剪自用户现有 `threadx_for_zynq7010/zynq7010/zynq_fsbl`，只加载一个 CPU partition（SSBL）；SSBL 基于 `hello_threadx` vendor 拷贝改造，零新 port 代码，通过 `storage_ops_t` 抽象层对接 FileX/SD（后期换 QSPI+LevelX）；app 自带 Xilinx BSP `boot.S`，SSBL 跳转前做纯破坏性清理（clean cache → disable cache → disable MMU），落到"接近 reset"状态。

**Tech Stack:** Zynq-7010（Cortex-A9 单核 + Artix-7 PL）、ThreadX 6.4.1 + FileX 6.4.1（vendor 拷贝）、Xilinx standalone BSP、arm-none-eabi-gcc、bootgen、Python 3（pack_app.py）。Phase 12 复用 NetXDuo 6.4.1（`hello_netxduo` 现有移植）。

**Source Spec:** `docs/superpowers/specs/2026-06-22-zynq-multistage-boot-design.md`

---

## 2. 章节索引

本计划分 12 个 Phase（对应 spec §14 的 M1–M11 里程碑 + Phase 12 为 spec 之外的扩展）。每个 Phase 完成后是一块可独立验证的软件：

| Phase | 标题 | Spec | 里程碑 | 主要交付物 | 分章 |
|---|---|---|---|---|---|
| 0 | 顶层目录骨架与 vendor 拷贝 | §3 | — | 顶层目录骨架 + vendor 拷贝脚本 | [phase-00](phases/phase-00-scaffold.md) |
| 1 | SSBL 工程骨架 | §8 | M1 | SSBL 工程骨架（基于 hello_threadx，ORIGIN=0x100000） | [phase-01](phases/phase-01-ssbl-skeleton.md) |
| 2 | 精简 FSBL | §7 | M2 | 精简 FSBL（裁剪现有 zynq_fsbl） | [phase-02](phases/phase-02-fsbl.md) |
| 3 | FSBL → SSBL 链通 | §13.2 | M3 | FSBL → SSBL 链通（boot.bif + BOOT.BIN） | [phase-03](phases/phase-03-fsbl-ssbl-chain.md) |
| 4 | storage 抽象层 + FileX SD 挂载 | §11 | M4 | storage 抽象层 + FileX SD 卡挂载 | [phase-04](phases/phase-04-storage-filex.md) |
| 5 | app_template + pack_app.py + handoff | §9.5, §13.3 | M5 | app_template + pack_app.py + handoff | [phase-05](phases/phase-05-app-template-handoff.md) |
| 6 | boot.cfg 解析 + boot_selector 自动选 app | §5 | M6 | boot.cfg 解析 + boot_selector 自动选 app | [phase-06](phases/phase-06-boot-cfg.md) |
| 7 | CLI 子系统 | §6 | M7 | CLI 子系统（触发 + 命令集） | [phase-07](phases/phase-07-cli.md) |
| 8 | YMODEM 两阶段升级 | §6.3 | M8 | YMODEM 两阶段升级（DDR 暂存 + 原子提交） | [phase-08](phases/phase-08-ymodem-upgrade.md) |
| 9 | bitstream 动态加载 | §4.2 | M9 | bitstream 动态加载（FileX + PCAP） | [phase-09](phases/phase-09-bitstream.md) |
| 10 | 错误处理 + LED 错误码 + OCM 标记 | §10 | M10 | 错误处理 + LED 错误码 + boot.log/OCM 标记 | [phase-10](phases/phase-10-error-handling.md) |
| 11 | QSPI 迁移 | §11.2 | M11 | QSPI 迁移（FileX + LevelX NOR） | [phase-11](phases/phase-11-qspi.md) |
| 12 | NetXDuo 集成 + Web 配置页 + 网络升级 | 网络（新增） | M12 | NetXDuo 集成 + Web 配置页 + 网络升级（复用现有 hello_netxduo 移植） | [phase-12](phases/phase-12-network-web.md) |
| — | **最终校验** | §12 | — | 跨 Phase 综合验收与回归矩阵 | [final-acceptance](final-acceptance.md) |

**实施次序约束**：
- **Phase 0 → 1 → 2 → 3**：强制顺序（每级依赖上一级链通）。
- **Phase 4–10**：依赖 Phase 3 链通后，多数任务可并行（host test、PC 脚本、SSBL 各模块）。
- **Phase 11**：独立后期工作。
- **Phase 12（网络）**：依赖 Phase 4（storage 层）、Phase 5（image_loader/CRC）、Phase 6（boot_config）已就绪；与 Phase 11 互不依赖，可并行或后做。

**跨 Phase 约定**（所有任务遵守）：
- **环境变量**：`VENDOR=E:/桌面/docs/threadx_for_zynq7010`（源 spec 资产根）；`PROJ=E:/桌面/docs/Code`（本项目根）。
- **路径约定**：分章文件路径沿用 spec §3.1 的约定路径 `boot/ssbl/src/`；实际 Vitis 工程落地为 `ssbl/ssbl/src/`（与 ThreadX_A9 / FileX_A9 同级）。两者一一对应。
- **构建工具**：使用 Vitis 自带的 `arm-none-eabi-gcc`，所有命令在 Vitis/Vivado PowerShell（Windows）或 XSCT Console 下执行。
- **平台限制**：硬件相关任务（FSBL 跑板、SD 卡测试、YMODEM 升级）需要物理开发板，单元测试与脚本类任务可在 PC 上完成。
- **风格**：复用 vendor 文件不重写注释风格；新文件用本项目风格（snake_case 文件/函数，camelCase 不引入）。

---

## 3. 模块依赖矩阵

> 此矩阵是各分章相互引用的"接口契约"。**新增功能时先查本矩阵**，确认要接入哪个模块、依赖什么，再回对应分章扩展。
>
> 行 = 依赖方（谁用），列 = 被依赖方（提供什么）。`✓` = 编译期/链接期依赖；`△` = 运行期/可选依赖；空 = 无依赖。

| 依赖方 ＼ 被依赖方 | Phase 0 目录 | Phase 1 SSBL 骨架 | Phase 2 FSBL | Phase 3 链通 | Phase 4 storage | Phase 5 image/header/handoff | Phase 6 boot.cfg | Phase 7 CLI | Phase 8 YMODEM | Phase 9 bit | Phase 10 err/log | Phase 11 QSPI | Phase 12 net |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **Phase 1 SSBL 骨架** | ✓ | | | | | | | | | | | | |
| **Phase 2 FSBL** | ✓ | | | | | | | | | | | | |
| **Phase 3 链通** | | ✓ | ✓ | | | | | | | | | | |
| **Phase 4 storage** | | ✓ | | | | | | | | | | | |
| **Phase 5 image/handoff** | | ✓ | | | △ | | | | | | | | |
| **Phase 6 boot.cfg** | | | | | ✓ | △ | | | | | | | |
| **Phase 7 CLI** | | ✓ | | | ✓ | | ✓ | | | | △ | | |
| **Phase 8 YMODEM** | | ✓ | | | ✓ | ✓ | ✓ | ✓ | | | | | |
| **Phase 9 bitstream** | | ✓ | ✓ | | ✓ | | ✓ | | | | | | |
| **Phase 10 err/log** | | ✓ | | | ✓ | | ✓ | △ | | | | | |
| **Phase 11 QSPI** | | ✓ | | | ✓ | | | | | | | | |
| **Phase 12 net** | | ✓ | | | ✓ | ✓ | ✓ | | | | | | ✓ |

**读法示例**：
- Phase 7（CLI）行有 `✓` 在 Phase 1（SSBL 骨架，提供 ThreadX/main）、Phase 4（storage，`ls/cat` 用）、Phase 6（boot.cfg，`cfg` 命令用）、`△` 在 Phase 10（boot.log，`status` 命令可选读）。
- Phase 12（net）依赖 Phase 1（SSBL 骨架）、Phase 4（storage，Web 文件操作）、Phase 5（image_loader/CRC，上传校验）、Phase 6（boot_config，Web 编辑 cfg）。

**关键复用点**（新增功能的高频接入位置）：
- **所有文件读写** → 走 Phase 4 的 `storage_ops_t` 宏，不直接调 FileX。
- **镜像校验** → 走 Phase 5 的 `image_header_t` + CRC32。
- **升级提交** → 走 Phase 5 的 `boot_selector_load_only`（staging→校验→提交），Phase 8 的 YMODEM 与 Phase 12 的 Web 上传共用此路径。
- **配置读写** → 走 Phase 6 的 `boot_config_load/save`（原子写）。
- **跳转 app** → 走 Phase 5 的 `jump_to_app`，跳转前资源释放见各 Phase 的 handoff 注意。

---

## 4. 公共符号清单

> 此清单列出跨 Phase 共享的类型、全局变量、常量与函数的**定义位置**和**使用位置**。新增功能时，引用现有符号而非重定义；如需新增公共符号，在此登记并注明定义 Phase。
>
> 完整的类型一致性审计见 [最终校验章](final-acceptance.md)。

### 4.1 公共数据类型

| 符号 | 定义位置 | 使用位置 | 说明 |
|---|---|---|---|
| `image_header_t` | Phase 5 / Task 5.2 | Phase 5（image_loader）、Phase 8（staging_verify）、Phase 12（Web 上传校验） | 镜像头：magic + len + CRC + payload |
| `storage_ops_t` | Phase 4 / Task 4.1 | Phase 4（sd_port）、Phase 6（扩展 file_create/truncate）、Phase 11（qspi_port）、Phase 12（Web 文件操作） | 介质抽象虚表 |
| `boot_cfg_t` | Phase 6 / Task 6.1 | Phase 6（解析/选择）、Phase 7（cfg 命令）、Phase 10（mark_attempt）、Phase 12（Web 编辑） | boot.cfg 解析结果 |

### 4.2 公共全局变量

| 符号 | 定义位置 | 使用位置 | 说明 |
|---|---|---|---|
| `g_storage` | Phase 4 / Task 4.1 | Phase 4（sd_port_init）、Phase 11（qspi_port_init） | `const storage_ops_t *`，介质后端指针 |
| `g_fx_media` / `g_fx_file` | Phase 4 / Task 4.2 | Phase 4+ 全模块 | FileX 全局单例（单文件访问，Phase 12 加 `g_storage_mutex` 保护） |
| `g_runtime_cfg` | Phase 6 起（main.c） | countdown / CLI / Phase 12 | 运行期 `boot_cfg_t`，AppTaskStart 写、各处读 |
| `g_storage_mutex` | Phase 12 / Task 12.4 | Phase 12 Web 回调、CLI storage 操作 | SD 访问串行化锁（CLI 与 HTTP 不能同时操作 `g_fx_file`） |

### 4.3 公共常量（DDR 内存布局）

> 这些地址是各 Phase 内存划分的"硬约定"，改动前必须核对所有引用方。完整 DDR 布局见 spec §2。

| 符号 | 值 | 定义位置 | 用途 |
|---|---|---|---|
| SSBL ORIGIN | `0x00100000` | Phase 1（ssbl.ld） | SSBL 代码/数据区起点 |
| app ORIGIN | `0x01000000` | Phase 5（app.ld） | app 加载区（≤8MB） |
| `STAGING_AREA_ADDR` | `0x02000000` | Phase 8 / Task 8.1 | YMODEM/Web 上传暂存区（10MB） |
| `BIT_DDR_TEMP` | spec §2 | Phase 9 / Task 9.1 | bitstream 下载到 DDR 的临时区 |
| GEM BD 区 | `0x0FF00000`/`0x0FF10000` | Phase 12 / Task 12.2（driver 硬编码） | NetXDuo DMA 描述符环（non-cacheable） |
| `BOOT_OCM_MARKER_ADDR` | `0xFFFF0000` | Phase 10 / Task 10.1 | OCM 高地址，boot.log 状态标记 |

### 4.4 公共函数 / 入口

| 符号 | 定义位置 | 使用位置 | 说明 |
|---|---|---|---|
| `storage_media_open` 等宏 | Phase 4 / Task 4.1 | 全 Phase 4+ | storage 包装宏（上层只调这些） |
| `boot_config_load/save/defaults` | Phase 6 / Task 6.2 | Phase 6/7/10/12 | boot.cfg 读写（save 是 `.tmp+rename` 原子写） |
| `boot_selector_load_only` | Phase 6 / Task 6.4 | Phase 6（自动 boot）、Phase 7（test/boot cmd）、Phase 8（YMODEM 提交）、Phase 12（Web 上传提交） | staging→校验→提交共用入口 |
| `jump_to_app` | Phase 5 / Task 5.6 | Phase 5.7、Phase 6.4、Phase 7.6、Phase 12（Web 后 boot） | noreturn；跳转前需做资源释放（见下） |
| `image_loader_load` | Phase 5 / Task 5.5 | Phase 6.4、Phase 8.1、Phase 12 | header 校验 + CRC + payload 拷贝 |
| `ssbl_fatal` | Phase 10 / Task 10.2 | 全 Phase（错误终态） | LED 错误码 + 串口 + 死循环 |

### 4.5 handoff 资源释放契约

> `jump_to_app` 是破坏性跳转。**任何在跳转前占用中断/DMA/外设的 Phase，都必须在 `handoff.c` 的 `jump_to_app` 里注册收尾逻辑**，否则 app 会收到残留中断。当前已注册：

| 资源 | 占用 Phase | 收尾位置 | 收尾动作 |
|---|---|---|---|
| ThreadX 业务线程 | Phase 7（cli/countdown/trigger） | `handoff.c:jump_to_app` | `tx_thread_terminate` |
| Private Timer | Phase 1（ThreadX tick） | `handoff.c:jump_to_app` | `PTIMER_CONTROL = 0` |
| GIC | Phase 1 | `handoff.c:jump_to_app` | `XScuGic_Stop` |
| GEM 中断 IRQ54 | **Phase 12（net）** | `handoff.c:jump_to_app`（Task 12.6） | `net_stop()` → `nx_ip_delete` 断 IRQ54 |

**新增功能若占用中断/DMA**：在 `jump_to_app` 的"停 GIC 之前"加收尾调用，并在本表登记。

---

## 5. Spec 覆盖矩阵

> 确认 spec 每一章都有对应的实现 Phase。

| Spec 章节 | 覆盖分章 |
|---|---|
| §1 项目目标与启动链 | 全计划 + [phase-06](phases/phase-06-boot-cfg.md) boot_selector |
| §2 DDR 内存布局 | [phase-01](phases/phase-01-ssbl-skeleton.md) ssbl.ld、[phase-05](phases/phase-05-app-template-handoff.md) app.ld、[phase-08](phases/phase-08-ymodem-upgrade.md) staging、[phase-09](phases/phase-09-bitstream.md) BIT_DDR_TEMP、[phase-12](phases/phase-12-network-web.md) GEM BD 区 |
| §3 目录结构 | [phase-00](phases/phase-00-scaffold.md) |
| §4.1 image header | [phase-05](phases/phase-05-app-template-handoff.md) image_header.h、pack_app.py |
| §4.2 bitstream | [phase-09](phases/phase-09-bitstream.md) |
| §5 boot.cfg | [phase-06](phases/phase-06-boot-cfg.md) |
| §6 CLI | [phase-07](phases/phase-07-cli.md)、[phase-08](phases/phase-08-ymodem-upgrade.md) |
| §7 精简 FSBL | [phase-02](phases/phase-02-fsbl.md) |
| §8 ThreadX 集成 | [phase-01](phases/phase-01-ssbl-skeleton.md) |
| §9 handoff | [phase-05](phases/phase-05-app-template-handoff.md) |
| §10 错误处理 | [phase-10](phases/phase-10-error-handling.md) |
| §11 介质抽象 | [phase-04](phases/phase-04-storage-filex.md)、[phase-11](phases/phase-11-qspi.md) |
| §12 测试 | 各分章"验证"步骤 + [final-acceptance](final-acceptance.md) |
| §13 构建流程 | [phase-03](phases/phase-03-fsbl-ssbl-chain.md) boot.bif、[phase-05](phases/phase-05-app-template-handoff.md) pack_app.py / handoff_exit.S |
| §14 milestones | 全 Phase ↔ M1-M12 |
| §15 关键决策 | 嵌入各分章的设计说明 |

---

## 6. 扩展指南（如何新增功能）

> 本计划设计为可扩展。新增一个功能（如新的升级通道、新的配置项、新的外设）时，按以下步骤接入，避免破坏现有接口。

### 6.1 新增功能的三步接入法

1. **查依赖矩阵（§3）**：确认新功能依赖哪些现有模块（storage / image_loader / boot_config / cli / handoff）。
2. **查符号清单（§4）**：优先复用现有符号（`storage_*` 宏、`boot_selector_load_only`、`boot_config_save`），不要重定义。
3. **新建分章或扩展现有分章**：
   - 若是全新子系统（如 Phase 12 网络）→ 新建 `phases/phase-13-xxx.md`，头部用与现有分章一致的模板（导航 + 元信息 + 接口定位）。
   - 若是现有 Phase 的增强 → 在对应分章末尾加 Task（如 Task X.N），并在本总述的依赖矩阵/符号清单登记新符号。

### 6.2 新增公共符号时的登记义务

每当一个分章定义了被其他 Phase 引用的类型/全局/常量/函数，**必须**：
1. 在本总述 §4（公共符号清单）的对应小节加一行（定义位置、使用位置）。
2. 若占用中断/DMA/外设，在 §4.5（handoff 资源释放契约）登记收尾逻辑。
3. 在 [final-acceptance](final-acceptance.md) 的类型一致性检查表更新该符号。

### 6.3 新增 Phase 的编号约定

- Phase 0–11 对应 spec §14 的 M1–M11，是主启动链，编号固定。
- Phase 12+ 是 spec 之外的扩展功能（网络、安全启动、A/B 冗余等），顺次递增。
- 每个扩展 Phase 在总述 §2（章节索引）和 §5（Spec 覆盖矩阵）登记。

---

## 7. 执行方式

**两种执行选项：**

1. **Subagent-Driven（推荐）**：用 superpowers:subagent-driven-development，每个 Task 派一个 fresh subagent，Task 间二阶段评审。
2. **Inline Execution**：用 superpowers:executing-plans，在当前会话里按 checkpoint 批量执行。

**最关键风险点**（跨 Phase）：
1. [phase-01](phases/phase-01-ssbl-skeleton.md) Vitis 工程配置（include path 链路最易出错）——失败时回查 spec §8.2 移植修正点是否全数应用。
2. [phase-02](phases/phase-02-fsbl.md) image_mover 简化（Boot Header 解析依赖 vendor 实现）——保留原 vendor `ReadPmfHeader` 辅助函数即可。
3. [phase-05](phases/phase-05-app-template-handoff.md) handoff（cache/MMU 顺序错会崩）——按 spec §13.4 + 分章失败排查表逐项查。
4. [phase-08](phases/phase-08-ymodem-upgrade.md) YMODEM 协议（CRC16 + 序号 + 收尾块）——用 Tera Term 的 YMODEM 模式严格测试。
5. [phase-12](phases/phase-12-network-web.md) GEM BD 地址 + TLB 属性（DMA non-cacheable 约束）——见分章"风险与注意事项"。

---

## 8. 归档与版本

- **分章版（当前）**：本文件 + `phases/` + `final-acceptance.md`
- **归档版**：`2026-06-22-zynq-multistage-boot_archive.md`（拆分前的单文件完整版，7007 行，只读，供全文检索）
- **Spec**：`docs/superpowers/specs/2026-06-22-zynq-multistage-boot-design.md`
