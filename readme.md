# STM32F103RCT6：Flash 分区与烧录说明

面向固件开发与生产烧录：说明 256KB Flash 上的 Bootloader / 双 APP / NVS 布局、Keil 分散加载与烧录注意点。分区示意图见仓库内 `flash_partition`。

---

## 目录

1. [概述](#1-概述)
2. [Flash 分区](#2-flash-分区)
3. [芯片扇区与分区的关系（必读）](#3-芯片扇区与分区的关系必读)
4. [开发环境](#4-开发环境)
5. [Bootloader 工程](#5-bootloader-工程)
6. [APP-A 工程](#6-app-a-工程)
7. [APP-B 工程](#7-app-b-工程)
8. [NVS 区域](#8-nvs-区域)
9. [生产整片烧录](#9-生产整片烧录)
10. [验证与调试](#10-验证与调试)
11. [中断向量表与其它注意点](#11-中断向量表与其它注意点)

---

## 1. 概述

将 Flash 划分为 Bootloader、APP-A、APP-B、NVS，可用于双备份升级、回滚与参数持久化。本文说明各段起始地址、Keil 中 IROM / `.sct` 的配置方式，以及按扇区擦除时的实际行为（与数据手册一致）。

---

## 2. Flash 分区

| 区域       | 起始地址     | 结束地址     | 大小   | 用途                         |
| ---------- | ------------ | ------------ | ------ | ---------------------------- |
| Bootloader | `0x08000000` | `0x08007FFF` | 32KB   | 引导与升级管理               |
| APP-A      | `0x08008000` | `0x0801FFFF` | 96KB   | 主应用（正常启动）           |
| APP-B      | `0x08020000` | `0x08037FFF` | 96KB   | 备份 / OTA 目标 / 回滚       |
| NVS        | `0x08038000` | `0x0803FFFF` | 32KB   | 非易失参数（如磨损均衡设计） |

> 链接脚本中的 ROM 区间不可重叠或越界；与 Bootloader 约定启动地址时需保持一致。

---

## 3. 芯片扇区与分区的关系（必读）

STM32F103xE（256KB）主 Flash 扇区划分（RM0008）为：

| 扇区 | 大小  | 地址范围（含）              |
| ---- | ----- | ----------------------------- |
| 0–3  | 各16KB | `0x08000000` – `0x0800FFFF` |
| 4    | 64KB  | `0x08010000` – `0x0801FFFF` |
| 5    | 128KB | `0x08020000` – `0x0803FFFF` |

与本文分区对应关系：

- **Bootloader（32KB）** 恰好占用扇区 0、1；**APP-A（96KB）** 从 `0x08008000` 起占用扇区 2、3、4。二者边界对齐到扇区边界，**仅擦除 APP-A 所占扇区时不会擦到 Bootloader**（与此前误写“可能擦到 Bootloader 第二扇区”相反，已更正）。
- **APP-B（96KB）与 NVS（32KB）** 合计 128KB，**同处于扇区 5**。对扇区 5 做**整扇区擦除**会同时擦掉 APP-B 与 NVS。OTA 写 APP-B 时若工具/算法按“扇区”擦除，需明确是擦整个扇区 5 还是仅按页编程；若整扇区擦除，应先备份 NVS 或在固件中恢复默认配置。

---

## 4. 开发环境

- Keil MDK 5.x+
- STM32F1 器件支持包（DFP）
- ST-Link / J-Link
- 对应芯片的 Flash 编程算法（Keil 自带）

---

## 5. Bootloader 工程

### 5.1 目标 / 链接

1. `Options for Target` → `Target`：IROM1 Start `0x08000000`，Size `0x8000`（32KB）；IRAM1 通常 `0x20000000`，`0xC000`。
2. 若用分散加载：在 `Linker` 取消 `Use Memory Layout from Target Dialog`，编辑 `.sct`，使 ROM 与上表一致。

### 5.2 输出

`Output` 中勾选 `Create HEX File`（如 `Bootloader.hex`）。

### 5.3 烧录

`Utilities` → `Settings`：编程范围建议覆盖整片 Flash（Start `0x08000000`，Size `0x40000`），擦除方式按工具说明选择；仅烧 Bootloader 时，`Erase Sectors` 一般只涉及扇区 0–1。

---

## 6. APP-A 工程

### 6.1 分散加载示例

`Linker` 取消 `Use Memory Layout from Target Dialog`，`.sct` 示例：

```text
; APP-A: 0x08008000, 96KB (0x18000)

LR_IROM1 0x08008000 0x00018000  {
  ER_IROM1 0x08008000 0x00018000  {
    *.o (RESET, +First)
    *(InRoot$$Sections)
    .ANY (+RO)
  }
  RW_IRAM1 0x20000000 0x0000C000  {
    .ANY (+RW +ZI)
  }
}
```

保存后重新编译。

### 6.2 输出与开发烧录

生成 `APP-A.hex`。`Download` 时若使用 `Erase Sectors`，通常擦除的是 APP-A 涉及的扇区 2–4，**不应**擦除 Bootloader 所在扇区 0–1（仍以所用工具实际行为为准）。

---

## 7. APP-B 工程

与 APP-A 相同思路，仅 ROM 基址改为 `0x08020000`，长度仍为 `0x18000`：

```text
LR_IROM1 0x08020000 0x00018000  {
  ER_IROM1 0x08020000 0x00018000  {
    *.o (RESET, +First)
    *(InRoot$$Sections)
    .ANY (+RO)
  }
  RW_IRAM1 0x20000000 0x0000C000  {
    .ANY (+RW +ZI)
  }
}
```

烧录 APP-B 时务必结合第 3 节：**与 NVS 同属扇区 5**，避免无意整扇区擦除导致 NVS 丢失。

---

## 8. NVS 区域

运行时参数区，烧录阶段常不写初值。应用可在首次启动检测是否为擦除态（全 `0xFF`），再写入默认配置。

生产预置数据可选：

- 在 APP 或 Bootloader 中用 `__attribute__((at(0x08038000)))` 等将常量放到 NVS 地址（需与链接布局一致）；
- 或单独生成 NVS 数据 HEX，再与主 HEX 合并烧录。

---

## 9. 生产整片烧录

### 9.1 J-Flash 多文件

1. 准备 `Bootloader.hex`、`APP-A.hex`、`APP-B.hex`（及可选 NVS HEX）。
2. 工程选择 `STM32F103RCT6`，依次 `Open data file` 加载；地址重叠时工具会提示，需消除重叠。
3. Flash 选项中设置擦除策略（如按扇区或整片，与产线规范一致）。
4. `Program & Verify` 一次写入。

### 9.2 合并 HEX

示例（需本机安装对应工具）：

```bash
srec_cat Bootloader.hex -intel APP-A.hex -intel APP-B.hex -intel -o All.hex -intel
```

再用 Keil 或其它烧录器烧录 `All.hex`；算法需覆盖 `0x08000000` 起整片 256KB。

---

## 10. 验证与调试

- **`.map`**：查 `Image$$ER_IROM1$$Base` 等确认链接起始地址。
- **Memory 窗口**：查看 `0x08008000`、`0x08020000`、`0x08038000` 是否符合预期。
- **NVS**：未预置时应为 `0xFF`。

---

## 11. 中断向量表与其它注意点

1. **VTOR**：APP-A / APP-B 不在 `0x08000000` 运行时，须在 `SystemInit()` 或 `main()` 早期设置，例如 `SCB->VTOR = FLASH_BASE | offset`：APP-A 偏移 `0x8000`，APP-B 偏移 `0x20000`（与具体启动代码一致即可）。
2. **扇区 5**：再次强调 APP-B 与 NVS 同扇区；OTA 与产线流程需统一擦写策略。
3. **稳妥产线策略**：若希望行为最简单，可采用整片擦除后一次性写入合并 HEX，避免多次部分擦除带来的边界问题。

---

**文档版本**：1.1  
**最后更新**：2026-03-28
