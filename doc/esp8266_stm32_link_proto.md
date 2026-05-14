# ESP8266 ↔ STM32 串口链路协议（业务层）

本文描述 **在 `TySerialFrame` 帧封装之上** 的业务命令、载荷与 **请求–应答** 约定。物理层为 UART（本工程默认与 USB `Serial` 复用，仅在 STA 已连接、文本配网 CLI 关闭后由 ESP 独占）。

帧格式（SOF、LEN、CRC8）以 **`include/TySerialFrame/TySerialFrame.h`** 为准，此处只定义 **CMD** 与 **PAYLOAD** 字节布局。

---

## 1. 命令一览

| CMD  | 方向        | 名称           | 说明 |
|------|-------------|----------------|------|
| 0x20 | STM32 → ESP | `SensorReport` | 周期性遥测；**缓存**为网页「传感器」卡片数据源 |
| 0x21 | ESP → STM32 | `SetRequest`   | 网页/上位机下发目标湿度、是否运行、挡位；**必须**用 0x22 应答同一 `req_id` |
| 0x22 | STM32 → ESP | `SetAck`       | 对最近一次匹配的 0x21 的确认或拒绝 |

STM32 本地改参数（按键等）时，仍应通过 **0x20** 上报当前「设备侧已生效」的状态，以便网页与下发结果一致。

---

## 2. 0x20 `SensorReport`（遥测）

**最小长度**：5 字节；**推荐长度**：6 字节（含目标湿度）。

| 偏移 | 长度 | 含义 |
|------|------|------|
| 0    | 1    | 当前相对湿度测量值 0–100（%） |
| 1    | 1    | 音频/音量强度 0–255（麦克风或 ADC；网页与 JSON 中亦作 **`volume_level`**，与 `audio_level` 同值） |
| 2    | 1    | 水位 0–100（液位百分比，语义由 MCU 定义） |
| 3    | 1    | 加湿/运行相关开关：0 关，非 0 开 |
| 4    | 1    | 工作挡位 0–255（挡位语义由 MCU 定义） |
| 5    | 1    | **可选**：目标湿度设定 0–100（%），与 MCU 内已生效的设定一致 |

- ESP 侧 **`sensor`** JSON 来自 **最近一次合法 0x20 的缓存**（不是「任意最后一帧」），字段含 `humidity_pct`、`water_level_pct`、`audio_level` 与 **`volume_level`**（与 `audio_level` 同值，便于「音量」语义对接）。
- 若 `len == 5`，ESP 不解析字节 5；网页仍可通过 **0x22** 或后续带字节 6 的遥测获得目标湿度。

---

## 3. 0x21 `SetRequest`（写设置）

**长度**：固定 4 字节。

| 偏移 | 含义 |
|------|------|
| 0    | `req_id`：1–255，每次请求递增；用于与 0x22 配对 |
| 1    | `target_humidity_pct`：0–100 |
| 2    | `humidifier_on`：0 关，1 开 |
| 3    | `gear`：0–255 |

**流程约定**

1. ESP 在 HTTP `POST /api/settings` 成功后发出 0x21，并进入 **pending** 状态（默认等待 **4000 ms**）。
2. STM32 应用新参数（或拒绝），回 **0x22**，且 **`req_id` 与 0x21 一致**。
3. 若 `status == 0`，ESP 将 **网页展示的设定值** 更新为 ACK 中的字段，并清除 pending。
4. 若 `status != 0`，pending 结束，`last_error` 记为 `mcu_rejected:<code>`（具体错误码由 MCU 定义）。
5. 超时无 0x22：`last_error = timeout`；**网页仍保持下发前的设定显示**（冻结），直到再次成功同步。

**并发**：pending 期间 ESP 拒绝新的 `POST /api/settings`（返回 `pending`）。

---

## 4. 0x22 `SetAck`（应答）

**长度**：固定 5 字节。

| 偏移 | 含义 |
|------|------|
| 0    | `req_id`：与待确认的 0x21 相同 |
| 1    | `status`：0 成功；非 0 为错误码（项目自定义） |
| 2    | MCU 实际生效的 `target_humidity_pct` |
| 3    | 实际生效的 `humidifier_on`（0/1） |
| 4    | 实际生效的 `gear` |

仅当 `req_id` 与 ESP 当前 pending 一致时，ESP 才消费该帧；其它 0x22 可忽略或记录日志。

---

## 5. HTTP 与 JSON（ESP 侧）

| 方法 | 路径 | 说明 |
|------|------|------|
| GET  | `/api/status` | 含 `last_frame`（调试用）、`sensor`（0x20 缓存）、`settings`（已确认/冻结显示）、`sync`（pending、req_id、deadline_ms、last_error、requested） |
| POST | `/api/settings` | Body：`{"target_humidity_pct":55,"humidifier_on":true,"gear":2}`；可只发部分字段，未出现的字段按 **当前已展示设定** 合并后再发 0x21 |

实现见：

- `include/TyDeviceComm/`：状态机、合并、超时、`tySerialFrameSend` 调用  
- `include/Web/Web.cpp`：路由与页面  
- `include/TySerialFrame/TySerialFrame.cpp`：`tySerialFrameSend` 组帧  

---

## 6. STM32 实现检查清单

1. 收到 0x21 后 **尽快** 回 0x22（建议在主循环或协议任务中 < 100 ms 量级，除非写 Flash 等长事务）。
2. 0x20 建议 **固定周期**（如 200 ms–1 s），保证 ESP 缓存新鲜。
3. 若暂时不支持字节 5，可先发 5 字节 0x20，并保证关键设定仅从 0x22 反映到上位机。
4. `req_id` 禁止用 0；ESP 侧从 1 递增。

---

## 7. 版本与兼容

- 本文与当前固件中的 `kTyCmdSensorReport` / `kTyCmdSetRequest` / `kTyCmdSetAck` 一致。
- 若需扩展载荷，建议 **新增 CMD**，保留旧 CMD 布局，避免已烧录设备解析失败。

---

## 8. 附录：一帧有效 0x20 遥测（STM32 → ESP8266）示例

**约定**：`LEN = 6`（小端 `06 00`），`CRC8 = (0x20 + 0x06 + 0x00 + payload[0..5]) mod 256`。

| 字段 | 十六进制 | 十进制含义（示例） |
|------|----------|---------------------|
| SOF | `A5 5A` | 帧头 |
| CMD | `20` | `SensorReport` |
| LEN | `06 00` | 载荷 6 字节 |
| payload[0] | `37` | 湿度 55% |
| payload[1] | `C8` | 音量/ADC 200 |
| payload[2] | `58` | 水位 88% |
| payload[3] | `01` | 加湿器开 |
| payload[4] | `03` | 挡位 3 |
| payload[5] | `3C` | 目标湿度 60% |
| CRC8 | `BD` | 校验（上式求和末字节） |

**完整线路上字节序列（从先到后）**：

```text
A5 5A 20 06 00 37 C8 58 01 03 3C BD
```

ESP8266 收齐后应解析成功，`/api/status` 里 `sensor.valid` 为 `true`，`last_frame.hex` 为上述载荷的十六进制串（不含 SOF/CMD/LEN/CRC 的展示方式以固件为准；当前 JSON 中 `last_frame.hex` 为 **整段载荷** 的 hex，即 `37C85801033C`）。
