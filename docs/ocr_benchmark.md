# OCR 检测尺寸与文字长度实验

该模式对比同一份 PP-OCRv6 INT8 检测模型在 736×736 和 320×320 下的耗时。
通过 `dl::Model` 的 `input_shapes` 覆盖输入；不修改模型文件或量化参数。
检测后从原图裁剪，用现有 48×320 INT16 识别模型做辅助质量检查。

## 条件与计时范围

| 图片 | 原图 | 文字 | 检测尺寸 |
|---|---|---|---|
| label | 原有 750×1170 面单 | 多行 | 736、320 |
| line15 | 736×736 | 上海市徐汇区田林路智能硬件中心 | 736、320 |
| line2 | 736×736 | 上海 | 736、320 |
| line3 | 736×736 | 上海市 | 736、320 |

三张受控样本采用同一字体、40 px 字号、起点 (48,320)、白底黑字。
生成器使用仓库内的 `cjk_full.ttf`，保存 JPEG 质量为95、无色度子采样；
manifest 记录字符数、文字边界、生成库版本和 SHA-256。

每张图先进行第一批736→320，再进行第二批320→736。
每个条件每批预热3次、正式10次；共160次正式检测和48次预热。
切换条件时销毁旧检测实例，然后创建新实例，逐个校验实际输入/输出形状。
736使用模型默认形状，与原有调用方式一致；320指定 `[1,320,320,3]`。

计时字段单位均为微秒：

- `pre_us`：缩放、补边、归一化及量化。
- `model_us`：检测网络执行。
- `post_us`：检测框提取、过滤、坐标变换及排序。
- `total_us`：外部测量整个 `Det::run()` 调用。

图片每张仅解码一次。解码和模型初始化另行记录，不纳入检测总时间。
Rec只在该图片全部正式计时结束后运行。置信度≥0.5才保留，保持原有业务规则。
串口输出在计时结束后进行；每次检测前等待100 ms，让日志排空并让出CPU。
屏幕为相同静态页面，LVGL在Core 0运行；检测任务Core 1、优先级5、栈32 KB。
不更改现有CPU/PSRAM频率、优化级别、检测阈值或模型推理的单核方式。

## 构建与运行

先加载本机 ESP-IDF 环境。生成器和分析器需要 Pillow，采集器需要 pyserial，
绘图需要 matplotlib。已生成的样本也保存在仓库中。

```bash
python3 tools/generate_ocr_benchmark_images.py
idf.py menuconfig
```

在 `OCR application` 中启用 `Run the controlled 736/320 detection experiment`，
对应 `CONFIG_APP_OCR_BENCHMARK=y`。该模式要求当前使用Flash rodata和INT16 Rec。

```bash
idf.py build
idf.py -p /dev/ttyACM0 flash
python3 tools/capture_ocr_benchmark.py --port /dev/ttyACM0 \
    --output experiments/ocr_detect_YYYY-MM-DD/run01
python3 tools/analyze_ocr_benchmark.py experiments/ocr_detect_YYYY-MM-DD/run01
```

固件启动后显示等待页面，收到串口字符 `b` 才运行；采集器会自动发送。
一次启动只运行一轮，重新测试时复位板卡并选择新的输出目录。
采集器不会自动复位设备，不允许覆盖已有原始日志。
如遇崩溃、看门狗错误、尺寸检查失败或超时，保存部分日志并报错，不生成完整结论。

关闭该配置并重新构建/烧录即可回到原有三张图片的交互演示。

## 数据与报告

- `raw_serial.log`：原始串口输出。
- `events.jsonl`：逐次计时、模型形状、框坐标和识别文本。
- `runs.csv`：208次原始检测数据，以 `warmup` 区分预热。
- `summary.csv`：8个条件的均值、中位数、P95、标准差、极值、分批均值及框数。
- `report.md`、`latency_comparison.png`、`*_boxes.png`：汇总和视觉对照。
- `provenance.json`、`sdkconfig.test`、`tracked_changes.patch`：固件、配置、模型、图片校验信息。

分析器检查全部160次正式检测及48次预热齐全、运行编号无重复、形状正确、
分阶段时间不超过总时间，并要求收到设备 `done` 事件。
P95采用nearest-rank定义，即20次排序后的第19个值。

速度提升用736与320的检测总耗时均值之比计算，另列纯模型耗时比。
框数变化必须结合对照图检查，不能直接等同于准确率变化。
受控图片只覆盖一种字体、字号及背景，测得的检测耗时不含完整OCR的解码、
模型首次加载、Rec及显示开销。
