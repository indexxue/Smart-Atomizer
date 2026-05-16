# 节奏识别（FFT 低频能量）与灯带同步 — 开发计划

本文档说明如何在当前 **Smart Atomizer** 工程上，实现「DMA 采集麦克风 ADC → FFT 提取 60~200 Hz 鼓点频段能量 → 实时节奏强度系数（0~1）→ 驱动 `strip` 灯效」的整体方案与分阶段任务。实现时以现有代码为锚点：`Common/adc_voltage.c`（ADC1 声路 DMA）、`Common/strip.c`（`STRIP_SCENE_ID_MIC_REACTIVE` / `strip_scene_mic_apply`）、`Factory/start.c`（`factory_poll_services` 周期调用 `strip_scene_update`）。

---

## 1. 目标与验收标准

| 项目 | 说明 |
|------|------|
| 输入 | ADC1（PA0，MAX9814 等）经 DMA 的连续采样序列（需可配置的采样率 `Fs` 与缓冲长度 `N`）。 |
| 频段 | 聚合 **60 Hz ~ 200 Hz** 频带内的能量（可理解为 kick / 低频节奏相关能量，而非全带 VU）。 |
| 输出 | 浮点或定点 **`rhythm ∈ [0, 1]`**，平滑、单调随低频能量增大，静音时趋近 0。 |
| 灯带 | 在 `strip_scene_update` → `strip_scene_mic_apply`（或并列的新 action）中，用 `rhythm` 调制亮度/填充/色相，并保持与现有 `strip_show_spi` 刷新方式一致。 |
| 非目标（首版可不做） | BPM 估计、节拍相位对齐、多麦克风波束成形。 |

**验收建议**：固定音量播放含明显 kick 的测试音频，示波或日志观察 `rhythm` 在鼓点处上冲、句间回落；灯带视觉与鼓点主观同步；CPU 占用与栈深度在目标 MCU 上可接受且无音频 DMA 欠采样。

---

## 2. 现状摘要（与方案的衔接点）

- **DMA 与读数**：`adc_voltage_init()` 对 ADC1/ADC3 启动 DMA；`adc_voltage_avg_raw(ADC_VOLTAGE_SOUND)` 对 `ADC_VOLTAGE_DMA_DEPTH`（默认 32）点做算术平均，用于当前 **时域包络 VU**，频率信息不足。
- **灯带 MIC 场景**：`strip_mic_frame_prepare()` 每帧取平均 raw → 慢基线 → `|AC|` 包络 → 噪声门 → `strip_scene_mic_apply()` 映射为彩虹 VU。若要「鼓点频段」，需在并行路径上增加 **基于原始采样块** 的频域特征，而不是仅替换平均值为 FFT（平均会抹掉波形细节）。
- **刷新周期**：Factory 固件中 `StartThread` 每 `start_task_poll_ms`（20 ms）调用 `strip_scene_update()`；MIC 流 action 每轮都会 `strip_scene_mic_apply()`，即灯效更新可与 **20 ms 级** 帧率对齐（FFT 可每帧或每两帧跑一次，按 CPU 预算定）。

---

## 3. 信号链与算法设计

### 3.1 采样率与 FFT 长度

- 设 ADC 规则采样周期为 `Ts = 1/Fs`，一次 FFT 使用 `N` 个样本（**N 须为 2 的幂**，便于用 radix-2 FFT / CMSIS-DSP）。
- 频率分辨率：`Δf = Fs / N`。  
  - 例：`Fs = 10 kHz`，`N = 512` → `Δf ≈ 19.5 Hz`，可对 60~200 Hz 取多个 bin 求和或求 RMS。  
  - 若 `Fs` 较低（如 2 kHz），则 200 Hz 以下 bin 数过少，鼓带区分度差，需提高 `Fs` 或增大 `N`。
- **硬件侧**：在 CubeMX / `adc.c` 中确认 ADC 时钟、采样时间、规则通道转换顺序，使 **有效 Fs** 与软件假设一致；必要时为「声路」单独使用定时器 TRGO 触发 ADC 或更高采样率的连续模式（若当前为软件轮询式触发，需改为 **连续 + DMA 环形缓冲** 以满足 FFT 窗口）。

### 3.2 预处理（进入 FFT 前）

1. **去直流**：对窗口内样本减去均值（或与 `strip_mic_frame_prepare` 类似的慢基线，但 FFT 窗口内用块均值即可）。
2. **窗函数**（推荐）：对 `N` 点乘 Hamming/Hann，减轻频谱泄漏；鼓点瞬态会略被抹平，可与无窗 A/B 对比。
3. **数据类型**：CMSIS-DSP 常用 `float32_t` 输入；若 RAM/CPU 紧，可评估 Q15 路径，但开发与调参成本更高。

### 3.3 FFT 与 60~200 Hz 能量

1. 调用 **实数 FFT**（如 `arm_rfft_fast_init_f32` + `arm_rfft_fast_f32`），得到复频谱或幅度谱。
2. 计算索引范围：  
   `k_low = ceil(60 / Δf)`，`k_high = floor(200 / Δf)`，在 `[k_low, k_high]` 上对 **功率** `|X[k]|²` 或幅度 `|X[k]|` 求和（或 RMS：`sqrt(sum/Nband)`）。
3. **仅使用正频率半轴**；若使用 rFFT 打包格式，需按 CMSIS 文档解包 bin。

### 3.4 归一化为 `rhythm ∈ [0, 1]`（实时强度系数）

推荐组合（可按效果微调）：

1. **慢跟踪噪声底**：对频段能量 `E` 做低通得 `E_floor`（如 IIR：`floor = α*floor + (1-α)*E`），用 **`E_rel = max(0, E - E_floor)`** 抑制环境噪声。
2. **动态范围压缩**：`E_rel` 经 `log(1 + k*E_rel)` 或幂律 `pow(E_rel, γ)`（γ<1）避免偶尔极大值长期占满 1.0。
3. **分位或峰值归一**：维护短时（如 1~3 s）`E_max` 的衰减峰值：`peak = max(E_rel, λ*peak)`，再 `rhythm = saturate(E_rel / peak)`。可与标定流程类比现有 `strip_scene_mic_cal_*`，增加「低频能量标定」可选。
4. **时间平滑**：attack 快（跟上鼓点）、release 慢（避免灯带闪烁），例如分别对上升/下降用不同一阶系数。
5. **输出**：`uint16_t rhythm_q15 = (uint16_t)(rhythm * 32767.f)` 等定点接口，便于 `strip` 内无浮点绘制。

### 3.5 与灯带映射

- **方案 A（侵入小）**：在 `strip_scene_mic_apply` 中，用 `rhythm` 替代或缩放当前 `s_mic_fill_smooth` / `val` 通道，使 VU 长度与亮度主要反映 **低频节奏** 而非全带语音。
- **方案 B（结构清晰）**：新增 `STRIP_ACTION_MIC_RHYTHM_STREAM` 或 `STRIP_SCENE_ID_MIC_RHYTHM`，与现有 VU 场景并存，由 `strip_scene_run` 选择。
- **视觉**：鼓点强时提高亮度或扩展「填充」段；弱节奏时回到现有 idle 四色或降低饱和度，避免 DC 漂移导致常亮。

---

## 4. 软件架构与模块划分

| 模块 | 建议位置 | 职责 |
|------|-----------|------|
| DMA 环形缓冲与快照 | `adc_voltage.c` / 新 `mic_fft_capture.c` | 暴露「最近连续 `N` 点」只读快照（双缓冲或半缓冲完成中断拷贝），避免 FFT 与 DMA 写同一半区冲突。 |
| FFT + 频段能量 + `rhythm` | 新 `rhythm_fft.c` / `rhythm_fft.h` | 初始化 CMSIS 实例、窗表、每帧 `rhythm_fft_step()`。 |
| 灯带绑定 | `strip.c` | `strip_mic_frame_prepare` 或 `strip_scene_update` 开头调用 `rhythm_fft_step`；`strip_scene_mic_apply` 读取 `rhythm_fft_get()`。 |
| 可选标定 | `serial_cmd.c` 或现有 MIC 标定扩展 | 打印/写入 `E_floor`、压缩系数、满量程等 NVS 参数。 |
| Factory 入口 | `Factory/start.c` | 无需大改，保持 `adc_voltage_init` 与 `strip_scene_update` 调用顺序；若 FFT 在独立任务中跑，再评估优先级与栈。 |

**线程模型**：首版建议在 **`strip_scene_update` 同上下文** 同步跑 FFT（实现简单、无锁），若 20 ms 内算不完再考虑低优先级 FreeRTOS 任务 + 双缓冲信号量。

---

## 5. 依赖与资源评估（STM32F103 类）

- **CMSIS-DSP**：在 MDK 工程中启用 ARM::CMSIS-DSP，链接数学库；使用 `arm_rfft_fast_f32` 等符号。
- **RAM**：窗系数 `N` + 输入缓冲 `N` + FFT 工作区（参考 CMSIS 实例结构体大小）；`N=512` 量级通常可接受，需在 `.map` 中核对。
- **CPU**：每帧一次 `N` 点 rFFT + 带内求和；若超标，可降为 `N=256`、降低 `Fs`、或每 2 帧计算一次并用 ZOH 保持灯效更新率。

---

## 6. 分阶段开发计划

### 阶段 0：基线与测量（0.5~1 天）

- 用示波器或 UART 日志确认当前 ADC1 **实际 Fs**、DMA 是否连续、是否存在与 `adc_voltage_avg_raw` 一致的「仅平均」路径。
- 决定目标 `Fs`、`N`，并列出 bin 与 60/200 Hz 的对应表（写入注释或调试日志）。

### 阶段 1：连续采样与快照 API（1~2 天）

- 扩展声路 DMA 为 **≥ N 深度的环形缓冲**（或双 `N/2` 半缓冲 + `HAL_ADC_ConvHalfCpltCallback` / `CpltCallback`）。
- 实现 `mic_capture_snapshot(int16_t *out, size_t n)`：从中断安全区拷贝最近 `N` 点（或就绪半帧），保证 FFT 不读到半旧半新数据。

### 阶段 2：FFT 与频段能量（2~3 天）

- 集成 CMSIS-DSP rFFT，完成去直流、可选窗、60~200 Hz 带内能量 `E`。
- 单元级：用已知单频正弦（信号发生器）验证 bin 峰值位置与幅度趋势。

### 阶段 3：`rhythm` 归一与平滑（1~2 天）

- 实现 `E_floor`、`peak`、attack/release，输出 `float rhythm` 或 Q15。
- 与音乐实测调参；对比「仅全带 VU」与「低频 rhythm」的主观差异。

### 阶段 4：灯带联调（1~2 天）

- 修改 `strip_scene_mic_apply`（或新 scene），将 `rhythm` 映射到像素；确认 `strip_show_spi` 仍在每帧调用且 SPI DMA 与 ADC DMA 通道无资源冲突。
- 回归：按键触发 `STRIP_SCENE_ID_TRIGGER` 等仍优先于 MIC 场景的逻辑不变。

### 阶段 5：健壮性与可选配置（1~2 天）

- 静音/插拔麦克风边界；`rhythm` 限幅与 NaN 防护（若用 float）。
- 可选：串口命令查询 `rhythm`、导出标定；文档化推荐 `Fs`/`N`。

---

## 7. 风险与对策

| 风险 | 对策 |
|------|------|
| 实际 `Fs` 与假设不符 | 以定时器或 `DWT_CYCCNT` / MCO 标定；Cube 中固定 ADC 时钟树。 |
| FFT 耗时超出 20 ms 帧 | 减小 `N`、降 `Fs`、降采样（CIC/均值抽取）后再 FFT。 |
| 语音/环境低频干扰 | 提高 `E_floor`、略抬高带下限（如 80~200 Hz）或加高通（IIR）在时域预处理。 |
| RAM 不足 | 更小 `N`、Q15 FFT、或将窗与 twiddle 放 Flash。 |

---

## 8. 文档与代码追踪

- 算法参数（`Fs`、`N`、频段、attack/release 系数）建议集中在 `rhythm_fft.h` 或 NVS 配置区，并在本文档「附录」中维护一版默认值表。
- 实现完成后可在本文件末尾追加「修订记录」表（日期、变更摘要）。

---

## 附录：与仓库文件的对应关系

| 文件 | 与节奏 FFT 的关系 |
|------|-------------------|
| `Common/adc_voltage.c` / `adc_voltage.h` | 扩展 DMA 深度/双缓冲；或新增并行 capture 模块避免破坏水位 ADC3 的简洁接口。 |
| `Common/strip.c` / `strip.h` | 消费 `rhythm`；可选新 `strip_scene_id_e` / action。 |
| `Factory/start.c` | 保持 `strip_scene_update()` 调用链；必要时调整 `start_task_poll_ms` 与 FFT 预算匹配。 |
| `Core/Src/adc.c`（及 Cube 配置） | 采样时间与触发源决定 `Fs`。 |

---

*文档版本：初稿 — 与当前仓库 `strip` / `adc_voltage` / Factory 轮询结构对齐。*

---

## 9. 固件现状（与 §1~§8 计划的关系）

当前产品固件（含厂测 **Factory**）采用 **「仅电压 → 灯」**：`ADC1` DMA 缓冲上 `adc_voltage_avg_raw()` 做块平均，`strip_scene_mic_apply()` 用慢基线与包络驱动 **VU 灯效**；**未启用** FFT 节奏分支，`rhythm_fft` 模块与 4096 点声路缓冲已移除，两路 ADC 仍共用 `ADC_VOLTAGE_DMA_DEPTH`（默认 32）。

若日后按本文档 §3~§6 重新引入频域节奏，可恢复更长声路 DMA、抽取与 FFT 路径，并与 `STRIP_SCENE_ID_MIC_REACTIVE` 并行或拆分 scene。
