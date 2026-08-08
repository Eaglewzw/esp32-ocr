# ESP32-P4 OCR Demo 字体 Flash 存储方案

## 1. 目标与结论

本工程把中文字体从应用程序的 `EMBED_FILES` 中移出，放入 16 MB Flash
里的独立 `storage` 分区。启动时使用 `esp_mmap_assets` 把字体映射为只读
地址，再把地址直接交给 LVGL TinyTTF。

这样有三个直接收益：

1. 完整中文字体不再挤占 8 MB `factory` 应用分区。
2. 不需要把约 4 MB 字体复制到 PSRAM。
3. 字形解析直接读取映射内存，避免 SPIFFS 逐段读文件带来的显示延迟。

## 2. Flash 与文件布局

分区表 `partitions.csv` 当前分配如下：

| 分区 | 大小 | 用途 |
| --- | ---: | --- |
| `factory` | 8 MB | 程序、OCR 模型和三张演示图片 |
| `storage` | 7 MB | mmap 字体资源 |

字体文件位于 `main/font_assets/`：

| 文件 | 约大小 | 用途 |
| --- | ---: | --- |
| `cjk_full.ttf` | 3.9 MB | Droid Sans Fallback Full，主要中文字体 |
| `demo_fallback.ttf` | 108 KB | Noto Sans CJK SC 演示字符子集和特殊符号兜底 |

工程内的 `tools/pack_font_assets.py` 会生成 `storage.bin` 和构建目录中的
`mmap_generate_storage.h`。脚本只使用 Python 标准库，不依赖 Pillow、NumPy
或联网安装。执行完整的 `idf.py flash` 时，`storage.bin` 会自动写入
`storage` 分区。

## 3. 运行流程

启动顺序如下：

```text
app_main
  -> initialize_storage
       -> 初始化 NVS
       -> 校验 storage 资源数量和 checksum
       -> mmap storage 字体分区
  -> initialize_ui
       -> 创建 20 px 和 16 px TinyTTF 字体
       -> 建立字体 fallback 链
  -> LVGL(Core 0) 显示；OCR(Core 1) 推理
```

字体查找链为：

```text
cjk_full.ttf -> demo_fallback.ttf -> LVGL Montserrat
```

`cjk_full.ttf` 覆盖大范围 CJK，但不包含普通半角英文、数字、`¥` 和 `·`。
小型兜底字体补齐 OCR demo 已知字符，最后由 LVGL Montserrat 处理普通
ASCII。16 px 和 20 px 字体分别维护字形缓存；OCR 结果仍采用分批渲染，
不会长时间阻塞 LVGL 任务。

## 4. 本次 OCR Demo 验收字符

下面的文本来自当前三张演示图片的识别日志，用于确认字体组合完整：

```text
李四100****0000
隐私保护
可接电话拨丁
上海市徐汇区田林路XX号智能
1286480450转263
硬件实验室
乐鑫科技Epessifystems 01-6227-4269
西南转运站
代收货款：¥319.7
LOGO
标准时效
2026/07/2719:17:00第1次打印
客付邮费：¥11.1
TEST114832269481
522 X32-6 901
集
示例分区B
可直接电话拨打
Y49
DEM0407020728093
ESP32-S3-Korvo-2 X1
OV2640摄像头模组 x1
已验视
隐私保护服务生效中
1650572325 转721
快递员可用该安全号码联系收件人
SYNTHETIC·图中信息均为虚构·NOREAL I
```

完整 Droid 字体负责包括“丁”在内的中文；小型兜底字体负责半角英文、
数字、人民币符号 `¥` 和间隔号 `·`。两者的字符并集覆盖以上内容。

## 5. 构建、烧录和日志检查

```bash
idf.py build
idf.py flash monitor
```

字体内容发生变化后必须执行完整 `flash`，不要只执行 `app-flash`，否则
设备里的 `storage` 仍是旧版本。正常启动时应看到类似日志：

```text
mmap_assets: MMAP Assets [storage] mounted successfully
app_storage: Mapped 2 font assets directly from flash
app_ui: fonts mapped from flash: CJK=..., fallback=...
```

`app_bin_check` 会比较固件编译时的文件数量和 checksum。如果只更新了
应用或只更新了字体分区，启动会明确报告数量或 checksum 不匹配，而不会
静默使用错误资源。

## 6. 更换或扩充字体

### 只替换完整中文字体

用新的 TTF 覆盖 `main/font_assets/cjk_full.ttf`，保持文件名不变，然后
执行 `idf.py build` 和完整 `idf.py flash`。无需修改 C++ 代码。

新字体需满足以下条件：

- TinyTTF/stb_truetype 能解析；优先使用普通 TTF，避免 TTC 字体集合。
- 两个 TTF 加资源表后的总大小小于 7 MB。
- 发布产品前确认字体授权允许随固件分发。

### 增加新的特殊字符

如果完整字体没有某个符号，可以重新生成并覆盖
`main/font_assets/demo_fallback.ttf`。建议只加入界面和 OCR demo 实际需要
的字符，以保持兜底字体较小。文件名不变时无需修改代码。

如果新增第三个 TTF 文件，构建系统会自动把它打包，但程序不会自动使用
它；还需要在 `app_storage.cpp` 中增加资源枚举映射，并在 `app_ui.cpp` 中
把它接入 fallback 链。

## 7. 关键实现位置

- `main/CMakeLists.txt`：调用打包器并注册 `storage.bin` 烧录项。
- `tools/pack_font_assets.py`：生成兼容 `esp_mmap_assets` 的字体资源包。
- `main/app_storage.cpp`：校验、映射字体分区并提供只读资源视图。
- `main/app_ui.cpp`：创建 TinyTTF 字体、缓存并连接 fallback 链。
- `main/font_assets/`：字体和授权文件。
- `partitions.csv`：8 MB 应用分区和 7 MB字体资源分区。

字体文件的授权文本位于 `main/font_assets/LICENSE-DroidSansFallbackFull.txt`
和 `main/font_assets/LICENSE-demo-fallback.txt`。
