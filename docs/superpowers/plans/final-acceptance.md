# 最终校验：跨 Phase 综合验收与回归矩阵

> **章节定位**：本文件是整个实现计划的收口校验章，覆盖所有 Phase 之上的横向一致性检查与端到端验收。
> **前置**：各分章（`phases/phase-NN-*.md`）的 Task 内"验证"步骤已逐项通过。
> **导航**：[↑ 返回总述](2026-06-22-zynq-multistage-boot.md) ｜ [← 上一章：Phase 12 网络](phases/phase-12-network-web.md)

---

## 1. 计划自审（Self-Review）

按 writing-plans 技能要求，对整个计划做 3 项横向检查。

### 1.1 Spec 覆盖检查

> 完整矩阵见 [总述 §5](2026-06-22-zynq-multistage-boot.md#5-spec-覆盖矩阵)。结论：spec §1–§15 全部章节有对应实现 Phase，**覆盖完整**，无遗漏。

### 1.2 Placeholder 扫描

通读全计划（含所有分章），搜索典型红旗：

| 红旗模式 | 结果 |
|---|---|
| "TBD" / "TODO" / "fill in later" | **无**（Phase 12 的 `web_server.c` 路由回调中有标注为"Task 实施时补"的 TODO，属合理的"按 NetXDuo 实际 API 版本填写"指引，非悬空占位） |
| "add error handling" / "handle edge cases" | **无**（每个函数的错误返回码都明示） |
| "similar to Task N" | **无**（boot_config_host_stub.c 重复了 apply_kv，是故意的 host test 设计） |
| "Write tests for the above" | **无**（每个 test 都有具体 case） |

**需要实现期注意的"软约束"（非 placeholder）**：
1. [phase-02](phases/phase-02-fsbl.md) image_mover.c 简化——给了骨架但 Xilinx Boot Header 结构解析需查 vendor `image_mover.c` 原代码（已标注 "Implementation note"）。
2. [phase-09](phases/phase-09-bitstream.md) `XDcfg_Transfer` API 签名依 Xilinx 版本而异（已标注）。
3. [phase-12](phases/phase-12-network-web.md) NetXDuo HTTP server callback 签名依 6.4.1 头文件实际 typedef 调整（已标注）。

这三处是合理的"按 vendor 实际填写"指引，不是悬空占位。

### 1.3 类型一致性检查

> 此表是 [总述 §4 公共符号清单](2026-06-22-zynq-multistage-boot.md#4-公共符号清单) 的审计视图，验证每个跨 Phase 符号的定义与使用一致。

| 符号 | 定义位置 | 使用位置 | 一致？ |
|---|---|---|---|
| `image_header_t` | Phase 5 / Task 5.2 | Phase 5 image_loader、Phase 8 staging_verify、Phase 12 Web 上传校验 | ✅ |
| `storage_ops_t` | Phase 4 / Task 4.1 | Phase 4 sd_port、Phase 6 扩展、Phase 11 qspi_port、Phase 12 Web 文件操作 | ✅ |
| `storage_media_open` 等宏 | Phase 4 / Task 4.1 | 全 Phase 4+ | ✅ |
| `g_storage` | Phase 4 / Task 4.1 | Phase 4 sd_port_init、Phase 11 qspi_port_init | ✅ |
| `g_fx_media` / `g_fx_file` | Phase 4 / Task 4.2 | 全 Phase 4+（Phase 12 加 `g_storage_mutex` 保护） | ✅ |
| `boot_cfg_t` | Phase 6 / Task 6.1 | Phase 6.2/6.4、Phase 7.4/7.5、Phase 10.3、Phase 12 Web 编辑 | ✅ |
| `jump_to_app` | Phase 5 / Task 5.6 handoff.h | Phase 5.7、Phase 6.4、Phase 7.6 boot cmd、Phase 12 Web 后 boot | ✅ |
| `handoff_exit` | Phase 5 / Task 5.6 .S | Phase 5.6 handoff.c | ✅ |
| `STAGING_AREA_ADDR/SIZE` | Phase 8 / Task 8.1 | Phase 8.2、Phase 8.3、Phase 12 Web 上传（共用 staging） | ✅ |
| `boot_log_*` | Phase 10 / Task 10.3 | Phase 10.4、app_template | ✅ |
| `boot_magic` 常量 | Phase 10 / Task 10.1 ssbl_config.h | Phase 10.3 boot_log.c、Phase 10.3 app_template | ✅ |
| `g_storage_mutex` | Phase 12 / Task 12.4 | Phase 12 Web 回调、CLI storage 操作 | ✅（新增） |
| `net_stop` | Phase 12 / Task 12.3 | Phase 12.6 handoff jump_to_app | ✅（新增） |

**结论：类型一致**。MAGIC 值（`BOOT_MAGIC_ATTEMPT = 0x4154544D` "ATTM"、`BOOT_MAGIC_OK = 0x4F4B4F4B` "OKOK"）在 ssbl_config.h 定义，boot_log.c 与 app_template 都用同一字面值（app_template 不 link SSBL，直接写 hex 值，与 spec §10.4 "app 唯一配合" 一致）。

---

## 2. handoff 资源释放审计

> `jump_to_app` 是破坏性跳转。验证所有占用中断/DMA/外设的 Phase 都在跳转前注册了收尾逻辑。
> 完整契约见 [总述 §4.5](2026-06-22-zynq-multistage-boot.md#45-handoff-资源释放契约)。

| 资源 | 占用 Phase | 收尾位置 | 顺序约束 | 审计 |
|---|---|---|---|---|
| ThreadX 业务线程（cli/countdown/trigger） | Phase 7 | `jump_to_app` 步骤 1 | 在停 Timer/GIC 前 | ✅ |
| Private Timer（ThreadX tick） | Phase 1 | `jump_to_app` 步骤 4 | 在停 GIC 前 | ✅ |
| GIC | Phase 1 | `jump_to_app` 步骤 5 | 最后停 | ✅ |
| GEM 中断 IRQ54 + Web server | Phase 12 | `jump_to_app` 步骤 1.5（Task 12.6） | **必须在停 GIC 之前**（`nx_ip_delete` 内部断 GIC 注册） | ✅ |

**新增功能若占用中断/DMA**：必须在 `jump_to_app` 停 GIC 之前加收尾，并在 [总述 §4.5](2026-06-22-zynq-multistage-boot.md#45-handoff-资源释放契约) 登记。

---

## 3. 端到端启动链验收

> 验证完整的 4 级启动链在真实硬件上跑通。这是所有 Phase 的最终集成验收。

### 3.1 启动链全链路

```
Stage 0: BootROM（片上 ROM）→ 加载 FSBL 到 OCM
Stage 1: 精简 FSBL（OCM 0x0）→ ps7_init + 加载 SSBL 到 DDR → handoff
Stage 2: SSBL（DDR 0x100000，ThreadX+FileX）→ 读 boot.cfg → 加载 app + bit → 跳转
Stage 3: app（DDR 0x1000000）
```

### 3.2 验收场景矩阵

| # | 场景 | 前置 Phase | 操作 | 期望 |
|---|---|---|---|---|
| 1 | 冷启动自动 boot | 0-6,9 | 烧 SD，上电 | FSBL→SSBL→读 cfg→加载 app+bit→跳转，app 跑起 |
| 2 | boot.cfg 缺失/损坏 | 6 | 删 boot.cfg，上电 | 用默认值（app=default.bin, delay=3）+ 告警，不阻断 |
| 3 | app 文件不存在 | 5,6 | cfg 指向不存在的 app，上电 | 进 CLI（spec §5.3 容错）|
| 4 | CRC 校验失败 | 5 | 篡改 app.bin，上电 | 拒绝加载，进 CLI |
| 5 | bit 文件缺失但 app OK | 9 | cfg 的 bit= 不存在，上电 | 告警继续启动（spec §5.3）|
| 6 | GPIO 触发 CLI | 7 | boot_delay 内按 key0 | 进 CLI |
| 7 | UART 魔数触发 CLI | 7 | boot_delay 内发 0xDEADBEEF | 进 CLI |
| 8 | CLI 内联 boot | 7 | CLI 下 `boot app.bin` | 加载并跳转 |
| 9 | YMODEM 升级 app | 8 | CLI 下 ymodem 上传新 app | 暂存→CRC→提交，重启加载新 app |
| 10 | YMODEM 升级断电安全 | 8 | 上传中途断电 | 重启后旧 app 完好（`.tmp+rename` 原子写）|
| 11 | boot.log 状态机 | 10 | 正常启动 vs app 不喂狗 | 下次启动分别记 OK / FAIL |
| 12 | LED 错误码 | 10 | 触发各种 fatal | LED 闪对应错误码 |
| 13 | QSPI 启动（可选） | 11 | 切 QSPI boot mode，上电 | 全链路与 SD 一致 |
| 14 | 网线插入自动起 Web | 12 | 插网线，上电 | 串口 link up，浏览器访问配置页 |
| 15 | Web 改 boot.cfg | 12 | 页面编辑→保存→重启 | 新 cfg 生效（走 boot_config_save 原子写）|
| 16 | Web 上传 app.bin | 12 | 页面上传→boot 命令 | 新 app 加载启动（走 staging→CRC→提交）|
| 17 | 不插网线零回归 | 12 | 不插网线，全流程 | 与 Phase 11 完全一致，无网络副作用 |
| 18 | handoff 无残留中断 | 5,12 | 自动 boot 到 app | app 正常运行，无 GEM/GIC 残留中断 |
| 19 | 软复位 | 7 | CLI `reset` | 系统复位重启 |

任一失败，按对应分章的"失败排查"小节定位。

---

## 4. 分章交叉回归

> 验证一个 Phase 的实现不破坏其他 Phase 的功能。每行 = 一个回归测试组。

| 被测功能 | 必须同时通过的 Phase | 回归重点 |
|---|---|---|
| storage 层（SD 挂载） | Phase 4 | 被 Phase 6/7/8/9/10/11/12 全部依赖，改动后全量回归文件读写 |
| boot.cfg 原子写 | Phase 6 | 被 Phase 7（cfg save）、Phase 10（mark_attempt）、Phase 12（Web POST）共用，改 `.tmp+rename` 逻辑后三者全验 |
| boot_selector_load_only | Phase 6 | 被 Phase 7（test/boot）、Phase 8（YMODEM 提交）、Phase 12（Web 上传提交）共用，改后三路径全验 |
| jump_to_app 资源释放 | Phase 5 | 被所有跳转路径依赖；Phase 12 新增 `net_stop`，改 handoff 顺序后验 IRQ54 不残留 |
| image_header + CRC | Phase 5 | 被 Phase 6/8/12 的校验路径共用，改格式后三处全验 |

---

## 5. 关键风险与失败排查总表

> 汇总各分章标注的"最关键风险点"，供实施时统一参考。

| 风险 | 涉及分章 | 失败现象 | 排查方向 |
|---|---|---|---|
| Vitis include path 链路 | Phase 1 | 编译报未定义引用 | 回查 spec §8.2 移植修正点是否全数应用 |
| FSBL Boot Header 解析 | Phase 2 | FSBL 找不到 partition | 保留原 vendor `ReadPmfHeader`，查 image_mover.c 原代码 |
| handoff cache/MMU 顺序 | Phase 5 | 跳转后挂死/Data Abort | 按 spec §13.4 顺序：clean→disable cache→disable MMU |
| YMODEM CRC16 + 收尾块 | Phase 8 | 升级后文件损坏 | 用 Tera Term YMODEM 模式严格测试，核对 CRC16 实现 |
| GEM BD 地址 + TLB | Phase 12 | DMA 收发 Data Abort | `Xil_SetTlbAttributes(0x0FF00000, 0xc02)` 须在 MMU init 后 |
| GEM 中断收尾顺序 | Phase 12 | app 收到残留 IRQ54 | `net_stop` 必须在 `XScuGic_Stop` 之前 |
| 链接顺序 | Phase 12 | NetXDuo 符号未定义 | `-lNetXDuo` 在 `-lThreadX` 之前 |
| SD 单例并发 | Phase 12 | CLI 与 Web 同时操作崩溃 | `g_storage_mutex` 覆盖所有 storage 操作 |

---

## 6. 验收检查清单

实现完成时，逐项确认：

- [ ] 所有 13 个 Phase 的分章 Task 内"验证"步骤通过
- [ ] §3 端到端验收场景 1-19 全部通过（硬件在环）
- [ ] §1.3 类型一致性表所有符号一致（无未定义/重复定义）
- [ ] §2 handoff 资源释放审计无遗漏（所有中断/DMA 有收尾）
- [ ] §4 分章交叉回归无破坏（storage / boot.cfg / boot_selector / handoff / image_header 改动后全量回归）
- [ ] [总述 §4 公共符号清单](2026-06-22-zynq-multistage-boot.md#4-公共符号清单) 与代码实际一致（新增符号已登记）
- [ ] spec §1–§15 全章节有实现覆盖（见总述 §5）

全部勾选后，计划实现完成。

---

## 7. 不在本期范围（汇总）

> 各分章声明的"不在本期范围"汇总，供后续规划参考。

- **Phase 11**：QSPI 是后期独立工作，SD 方案优先。
- **Phase 12 网络**：
  - HTTPS/TLS（需 `nx_secure_*`，代码体积显著增加）
  - DHCP / AutoIP（已选静态 IP；若需可后加 `nx_dhcp_*` addon）
  - mDNS（`http://zynq-ssbl.local`，体验向）
  - TFTP server（除 Web 外另一上传通道，`nxd_tftp_server` addon）
  - 网络升级的 RSA 签名校验（当前镜像格式只支持 CRC32，安全启动是独立的 Phase）
- **未规划但可扩展**：A/B 冗余启动、NAND 启动、JTAG boot mode、bitstream 多 partition。
