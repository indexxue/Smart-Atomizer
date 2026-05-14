# STM32 ESP8266 链路（TySerialFrame / ty_link）使用说明

本文说明本工程中 **USART1** 上 **厂测（proto）** 与 **ESP8266 业务协议（ty_link）** 的用法与扩展点；**两套固件通过 Keil 目标与是否编译 `Core/Src/freertos.c`、是否调用 `serial_cmd_init` 区分，不依赖 `Common` 内预处理器宏。** 帧格式与 CMD 定义见 [esp8266_stm32_link_proto.md](./esp8266_stm32_link_proto.md)；实施背景见 [esp8266_stm32_link_implementation_plan.md](./esp8266_stm32_link_implementation_plan.md)。

---

## 1. 模块与文件

| 模块 | 路径 | 作用 |
|------|------|------|
| `proto` | `Common/proto.c`、`Common/proto.h` | USART1 字节路由：厂测环形缓冲 **或** ESP 模式下的 ISR 回调；阻塞发送 `proto_uart1_send` |
| `ty_serial_frame` | `Common/ty_serial_frame.c`、`Common/ty_serial_frame.h` | SOF `A5 5A`、CMD、小端 LEN、CRC8 编解码 |
| `ty_link` | `Common/ty_link.c`、`Common/ty_link.h` | 0x20 周期上报、0x21 解析与 0x22 应答；RTOS 队列 + `ty_link_task` |
| FreeRTOS 入口 | `Core/Src/freertos.c` | 应用固件：`MX_FREERTOS_Init()` 中创建 `ty_link_task` 并调用 `ty_link_init()`（**Factory 目标不编译本文件**） |

---

## 2. 两套固件如何区分（不靠宏）

| 固件 | Keil 目标 / 入口 | `Core/Src/freertos.c`（`ty_link`） | `serial_cmd`（厂测 CLI + `proto`/`u1*`） |
|------|------------------|-------------------------------------|------------------------------------------|
| **应用** | `Smart Atomizer` + `Core/Src/main.c` | **参与编译**，`MX_FREERTOS_Init()` 启动 `ty_link` | **不要**调用 `serial_cmd_init` / `serial_cmd_register_defaults`（应用代码不引用 `Factory/start.c`） |
| **厂测** | `Factory` + `Factory/factory_main.c` | **不参与编译**（`freertos.c` 对该目标 `IncludeInBuild=0`） | **参与**：`Factory/start.c` 里 `serial_cmd_init` + `serial_cmd_register_defaults` |

USART1 上要么是 **厂测环缓**（厂测镜像里 `proto_init` + `FACTORY` 模式），要么是 **ESP 协议**（应用镜像里 `ty_link_init` + `ESP8266` 模式），由 **哪套镜像在跑** 决定，**无需**在 `Common/serial_cmd.c` 里再用预处理器宏切两套行为。

`Common/serial_cmd.c` 保持完整厂测命令实现即可；应用固件通过 **不包含厂测入口、不调用 `serial_cmd_init`** 避免与 `ty_link` 抢 USART1。

---

## 3. 应用固件注意点

- **`ty_link_init()`** 内部会 `proto_init`、`UART1_Start_Receive_IT`，并 `proto_uart_set_mode(PROTO_UART_MODE_ESP8266)`。
- 若误在应用里调用 **`serial_cmd_init()`**，其中的 **`proto_init` 会清掉 ESP 回调并把 USART1 拉回厂测模式**，与 `ty_link` 冲突。应用层应 **不链接或不调用** 厂测初始化路径（当前 `Core/Src/main.c` 不调用即可）。
- `proto.c` 仍可能被应用链接（`ty_link` 依赖），其中对 `serial_cmd_*` 的调用在 **未调用 `serial_cmd_init`** 时仅影响「若有人走 `proto_cmd_tx` 等回复路径」；正常应用只走 `ty_link`，不触发厂测命令即可。

---

## 4. 厂测固件注意点

- `Factory/start.c`：`serial_cmd_init(&huart3)` → `proto_init(&huart1)` → `serial_cmd_register_defaults()`（含 `proto`、`u1tx` 等）→ `UART1_Start_Receive_IT()`。
- 与 **应用** 不同：厂测工程 **不编译** `Core/Src/freertos.c`，因此 **不会** 启动 `ty_link_task`。

---

## 5. `proto` 模式说明

| 模式 | 枚举 | USART1 RX 行为 |
|------|------|------------------|
| 厂测 | `PROTO_UART_MODE_FACTORY` | 字节写入环形缓冲，供 `u1rx` / `u1xfer` 等读取 |
| ESP 链路 | `PROTO_UART_MODE_ESP8266` | 每字节调用已注册的 `proto_esp_rx_byte_handler`（`ty_link` 内写入队列） |

切换模式会 **清空** 环形缓冲，避免厂测数据与二进制帧混杂。

透传监视 `proto_enter()` / `proto_poll_monitor()`：仅在厂测相关逻辑下使用；**ESP8266 模式**下 `proto_poll_monitor` **不会**把 UART1 二进制转发到调试口。

---

## 6. `ty_link` API 摘要

以下声明见 `Common/ty_link.h`。

### 6.1 初始化与线程

- **`bool ty_link_init(void)`**  
  创建 RX 队列、互斥锁，`proto_init` + `UART1_Start_Receive_IT`，注册 ISR 字节回调并进入 ESP 模式。失败返回 `false`（RTOS 对象创建失败等），此时 **不应** 再 `osThreadNew(ty_link_task, …)`。  
  当前工程在 `freertos.c` 中仅在 `ty_link_init()` 成功后才创建线程。

- **`void ty_link_task(void *argument)`**  
  协议线程入口：从队列取字节 → `ty_serial_frame` 解码 → 处理 **0x21** 并组 **0x22**；按周期发送 **0x20**。

### 6.2 下发设置（0x21）扩展

- **`void ty_link_register_apply(ty_link_apply_settings_fn fn, void *user)`**  

  收到合法 **0x21** 时：

  - 若 `fn != NULL` 且 **`fn(in, out, user)` 返回 `true`**：认为 `out` 已填好，用于 **0x22** 的 `status` 及生效字段（字节 1～4，不含 `req_id`，`req_id` 仍来自请求帧）。
  - 否则：使用内置默认逻辑（仅更新内部 **shadow**：目标湿度、开关、挡位），`status = 0`。

  回调里请 **尽快** 完成逻辑（协议文档建议 ACK 在约 100 ms 量级内，除非写 Flash 等长事务）。

### 6.3 遥测 0x20

- 周期约 **500 ms** 自动发送一帧 **0x20**（6 字节载荷，含目标湿度）。
- **`void ty_link_set_measured_humidity_pct(uint8_t pct)`**  
  更新 0x20 第 0 字节（当前相对湿度 0～100）。未接 SHT20 时内部默认约 50%；可在传感器任务中周期性调用本接口。

- 第 1、2 字节来自 **`adc_voltage`**（声压 / 水位原始值映射），需在 `main` 中已调用 **`adc_voltage_init()`**。

### 6.4 原始帧发送

- **`bool ty_link_send_raw_frame(uint8_t cmd, const uint8_t *payload, uint16_t len)`**  
  在互斥保护下组 TySerialFrame 并 `proto_uart1_send`。用于调试或将来扩展 CMD；正常业务一般无需直接调用（0x20/0x22 已由 `ty_link` 内部发送）。

### 6.5 错误码（0x22 `status`）

| 宏 | 值 | 含义 |
|----|----|------|
| `TY_ACK_ERR_NONE` | 0 | 成功 |
| `TY_ACK_ERR_BAD_REQ_ID` | 1 | `req_id == 0` |
| `TY_ACK_ERR_BAD_PARAM` | 2 | 湿度 >100 或 `humidifier_on` >1 等 |

可在 `ty_link_register_apply` 回调中返回其它非 0 自定义码（需与 ESP 网页侧约定）。

---

## 7. `proto` 与底层发送（进阶）

- **`proto_uart1_send` / `proto_uart1_send_timeout`**：对已组好的 **完整线路上字节** 做阻塞发送。
- **`proto_uart_set_mode` / `proto_uart_get_mode`**：手动切换 USART1 消费方（一般由 `ty_link_init` 与 `proto_init` 管理，除非你做动态厂测/量产切换）。
- **`proto_esp_register_rx_byte_handler`**：自定义 ISR 投递方式时可用；`ty_link` 已注册为写入 CMSIS 消息队列。

---

## 8. 厂测命令与 USART1（厂测镜像 / `PROTO_UART_MODE_FACTORY`）

在 **Factory** 目标下 `serial_cmd_register_defaults()` 会注册：

| 命令 | 作用 |
|------|------|
| `u1tx` | 按十六进制字节发送 UART1 |
| `u1rx` | 从环形缓冲读出若干字节并以 hex 回复 |
| `u1clr` | 清空 RX 环 |
| `u1xfer` | 发一段 hex 并延时后读回 |

进入 **`proto_enter()`** 后为透传监视模式（与产测流程相关，详见 `serial_cmd` 注册处）。

---

## 9. FreeRTOS 与堆

启用 `ty_link` 后会增加 **消息队列、互斥锁、`ty_link_task` 栈**。工程已将 `configTOTAL_HEAP_SIZE` 提高（见 `Core/Inc/FreeRTOSConfig.h`）。若仍出现创建失败，可适当再增大堆或减小 `ty_link` 线程栈 / 队列深度（在 `ty_link.c` 中 `TY_LINK_RX_QUEUE_DEPTH` 等宏）。

---

## 10. 与协议文档的对照

| 文档 CMD | `ty_link` 行为 |
|----------|----------------|
| 0x20 SensorReport | 周期发送；湿度可被 `ty_link_set_measured_humidity_pct` 覆盖 |
| 0x21 SetRequest | 解析后组 0x22；可挂 `ty_link_register_apply` |
| 0x22 SetAck | 自动回复，载荷 5 字节 |

详细字节布局见 [esp8266_stm32_link_proto.md](./esp8266_stm32_link_proto.md)。

---

## 11. 常见问题

**Q：厂测能同时用 u1rx 和 ESP 协议吗？**  
A：不能在同一固件、同一时刻混用。厂测刷 **Factory** 镜像（不编 `Core/Src/freertos.c` / 无 `ty_link`）；应用刷 **Smart Atomizer** 镜像（不跑 `serial_cmd_init`）。

**Q：收到 0x21 后多久必须回 0x22？**  
A：ESP 侧默认约 4 s 超时；文档建议 STM32 侧尽快（约 100 ms 量级）。当前实现在 **`ty_link_task`** 中处理，避免在 UART ISR 里做重逻辑。

**Q：如何接 SHT20？**  
A：在独立任务中读湿度成功后调用 **`ty_link_set_measured_humidity_pct`**；加湿/挡位控制在 **`ty_link_register_apply`** 里对接硬件或 `strip` 等模块。

---

## 12. 文档修订

- 与当前代码目录：`Common/proto.*`、`Common/ty_serial_frame.*`、`Common/ty_link.*`、`Core/Src/freertos.c` 一致。
- 若修改默认周期或队列深度，请同步更新 `esp8266_stm32_link_implementation_plan.md`。
