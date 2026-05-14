# STM32 ↔ ESP8266 链路协议改造实施计划

本文档基于 [esp8266_stm32_link_proto.md](./esp8266_stm32_link_proto.md) 与当前工程中的 `Common/proto.c`、`Common/proto.h`，描述将固件从「UART 调试/透传」升级为 **TySerialFrame + 业务 CMD（0x20/0x21/0x22）** 的实施计划。

---

## 0. 硬件与串口约定

- **协议物理链路**：STM32 **USART1** 与 ESP8266 串口对接（波特率等与 ESP 固件一致，常见为 115200，以双方配置为准）。
- **工程现状**：`proto_init()`、`proto_on_rx_from_isr()` 等已绑定 UART1（`huart1`），产测命令 `u1tx` / `u1rx` 等亦针对该口；改造后 **业务帧仍走 USART1**，仅在「产测 CLI / 透传」与「ESP 链路模式」之间切换消费者，避免同一字节流被既当文本又当二进制解析。

---

## 1. 背景与目标

**协议摘要**（详见协议文档）：

| CMD | 方向 | 名称 | 要点 |
|-----|------|------|------|
| 0x20 | STM32 → ESP | SensorReport | 周期遥测；载荷 5 字节最小、6 字节推荐（含目标湿度） |
| 0x21 | ESP → STM32 | SetRequest | 固定 4 字节；`req_id` 为 1–255 |
| 0x22 | STM32 → ESP | SetAck | 固定 5 字节；`req_id` 与 0x21 一致；ESP 默认等待约 4000 ms |

**ESP 侧**：`POST /api/settings` 后发 0x21 并进入 pending；仅当 0x22 的 `req_id` 与 pending 一致时结束；超时记 `last_error = timeout`。

**当前固件**：`proto.c` 提供 USART1 环形缓冲、十六进制发送/接收、透传监视；**尚未实现** TySerialFrame 与 0x20/0x21/0x22。

**目标**：在 **USART1** 上可靠收发帧、处理 0x21 并尽快回复 0x22、周期发送 0x20，并与应用层湿度、挡位、运行开关、目标湿度等数据一致。

---

## 2. 差距分析

| 能力 | 文档要求 | 当前 `proto.c` |
|------|----------|----------------|
| 帧同步与 CRC | SOF `A5 5A` + CMD + LEN + payload + CRC8 | 仅原始字节流 |
| 收 0x21 | 解析后取 4 字节载荷 | 无 |
| 回 0x22 | 尽快（建议 &lt;100 ms，除非写 Flash 等长事务） | 无 |
| 发 0x20 | 固定周期（如 200 ms～1 s） | 无 |
| `req_id` | 禁止 0；与 ACK 配对 | 无 |

---

## 3. 软件分层（建议）

1. **TySerialFrame 层**（新建，如 `Common/ty_serial_frame.c` + `.h`）  
   - 与 ESP 侧 `TySerialFrame` **字段与 CRC 规则一致**（文档附录示例可作为 golden case）。  
   - 提供编码、解码状态机（处理粘包/半包）。

2. **链路业务层**（如 `Common/ty_link.c` / `.h` 或并入 `proto`）  
   - 常量：`0x20` / `0x21` / `0x22`。  
   - RX：从 **USART1** 字节流喂入解析器；完整帧且 CMD=0x21、LEN=4 时提交设置请求。  
   - TX：`send_sensor_report(...)`、`send_set_ack(...)`，内部组帧后经 **USART1** 发送。

3. **应用适配层**  
   - 映射传感器/ADC/挡位/开关/目标湿度到 0x20 各字节。  
   - 应用 0x21 后，0x22 回填 **MCU 实际生效** 字段。  
   - 本地按键等改参后仍通过 **0x20** 上报当前生效状态。

4. **调度**  
   - FreeRTOS 周期任务：200 ms～1 s 发送 0x20。  
   - 0x21 处理：解析完成后短路径内更新应用并发送 0x22，满足 ESP 4 s 窗口。

---

## 4. 分阶段实施

**阶段 A — 帧层与自测**  
- 实现编解码；用协议文档附录整帧 `A5 5A 20 06 00 ...` 做校验。  
- 定义最大 payload/帧长，避免栈与缓冲溢出。

**阶段 B — USART1 RX 接入**  
- 在现有 `proto_on_rx_from_isr`（或 USART1 IRQ）中，按模式将字节送入帧解析器 **或** 保留产测环形缓冲。  
- 定义 `PROTO_MODE_FACTORY` / `PROTO_MODE_ESP_LINK`，进入 ESP 链路时不在 `proto_poll_monitor` 中把二进制数据当文本 echo。

**阶段 C — 0x21 → 应用 → 0x22**  
- 校验 `req_id != 0`、湿度 0–100、`humidifier_on` 0/1；非法返回 `status != 0`（错误码集中定义）。  
- 合法则调用现有控制接口，用生效值组 0x22。

**阶段 D — 0x20 周期上报**  
- 从传感器/控制任务读取最新状态；优先 6 字节载荷。  
- 与 0x22 发送互斥，避免 USART1 上半帧交错。

**阶段 E — 与 `serial_cmd` 整合**  
- `serial_cmd` 与 `proto_enter`/`proto_exit`：模式切换时明确谁消费 USART1 RX。  
- 产品策略：STA 已连接、CLI 关闭后由 ESP 独占 USART1（可用标志位或首帧 SOF 自动切入链路模式）。

**阶段 F — 与 ESP 联调**  
- `/api/status`、`POST /api/settings`、pending/timeout、`sensor.valid` 等验收项与协议文档第 5、6 节对齐。

---

## 5. 待决策事项

1. TySerialFrame 实现：从 ESP 工程移植并改为 C，或手写并对照 ESP `tySerialFrameSend` 做 CRC 对齐。  
2. USART1 发送：阻塞 `HAL_UART_Transmit` 与 DMA 的选择（周期 0x20 较密时倾向 DMA + 简单队列）。  
3. 若设置需写 Flash：是否在 0x22 前完成；若延迟过大，需产品层约定 `status` 或异步持久化策略。  
4. 0x22 `status` 非 0 时的错误码表，建议补入 `esp8266_stm32_link_proto.md`。

---

## 6. 文件改动清单（预估）

| 动作 | 路径/模块 |
|------|-----------|
| 新增 | `Common/ty_serial_frame.c`、`.h`（或等价命名） |
| 新增/大改 | `Common/ty_link.c`、`.h` 或扩展 `proto.c` |
| 修改 | `proto.c` / `proto.h`：模式、RX 分发、与帧层衔接 |
| 修改 | `serial_cmd.c`：与 USART1 模式切换相关逻辑 |
| 修改 | `Core/Src/main.c` 或应用任务：周期 0x20、初始化 |
| 修改 | `Core/Src/stm32f1xx_it.c`（或 USART1 回调）：字节投递 |
| 修改 | `MDK-ARM/*.uvprojx`：加入新源文件 |

---

## 7. 验收标准

1. 合法 0x20（5 或 6 字节载荷）经 TySerialFrame 从 **USART1** 发出后，ESP 侧 `sensor` 缓存有效。  
2. 合法 0x21 在约 4000 ms 内收到 `req_id` 匹配的 0x22。  
3. 坏帧/CRC 错误可丢弃并重新同步到下一 SOF。  
4. 本地改参后，后续 0x20 反映设备侧已生效状态。

---

## 8. 风险与缓解

- **半包与缓冲溢出**：状态机解析 + 明确的缓冲满策略（与产测需求权衡）。  
- **CRC 不一致**：与 ESP 用附录示例及互发单帧第一时间对齐。  
- **CLI 与二进制冲突**：ESP 链路模式下禁止对非文本 RX 做透传式 `serial_cmd_send`。

---

## 9. 版本

- 与 `esp8266_stm32_link_proto.md` 中 `kTyCmdSensorReport` / `SetRequest` / `SetAck` 约定一致。  
- 本文档：**USART1 为协议串口**；扩展载荷建议新 CMD，避免破坏已有布局。
