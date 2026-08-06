# ESP32-P4 快递面单 OCR 识别

基于 ESP32-P4 的高性能端侧 OCR 识别系统，使用摄像头拍摄快递面单，通过 LVGL 图形界面在触摸屏上实时显示识别结果。

## 硬件平台

| 组件 | 型号 |
|------|------|
| 主控 | ESP32-P4 (双核 + LP 核, 400MHz) |
| 开发板 | [Waveshare ESP32-P4-WIFI6-Touch-LCD-XC](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-XC) |
| 显示屏 | 3.5 寸 800×800 (或 4 寸 720×720) 触摸屏 |
| 存储 | 16MB Flash + 7MB SPIFFS 分区 |
| 摄像头 | TBD |
| 网络 | Wi-Fi 6 (802.11ax) |

## 软件架构

```
esp32_ocr/
├── main/                       # 主程序
│   ├── main.c                  # 应用入口
│   ├── CMakeLists.txt          # 编译配置
│   ├── component.mk            # 组件配置
│   └── idf_component.yml       # 组件依赖
├── components/
│   ├── esp32_p4_wifi6_touch_lcd_35/  # Waveshare BSP 驱动
│   │   ├── include/bsp/        # 显示/触摸/音频 驱动头文件
│   │   └── ...                 # 硬件初始化 & 配置
│   └── bsp_extra/              # 扩展驱动（音频播放器/SD卡）
├── partitions.csv              # 分区表
├── sdkconfig.defaults          # SDK 默认配置
├── CMakeLists.txt              # 顶层编译配置
└── .gitignore
```

## 核心依赖

| 依赖 | 版本 | 用途 |
|------|------|------|
| [ESP-IDF](https://github.com/espressif/esp-idf) | 5.5.4 | 开发框架 |
| [LVGL](https://github.com/lvgl/lvgl) | 9.4.x | 图形用户界面 |
| [esp_lvgl_port](https://components.espressif.com/components/espressif/esp_lvgl_port) | ^2 | LVGL 硬件接口 |
| [esp_lcd_jd9365](https://components.espressif.com/components/espressif/esp_lcd_jd9365) | 1.0.2 | LCD 驱动 (JD9365) |
| [esp_lcd_touch_gt911](https://components.espressif.com/components/espressif/esp_lcd_touch_gt911) | ^1 | 触摸驱动 (GT911) |
| [esp_codec_dev](https://components.espressif.com/components/espressif/esp_codec_dev) | 1.2.* | 音频编解码 |

## 功能规划

- [x] LVGL 图形界面框架搭建
- [x] 触摸屏驱动适配
- [x] SD 卡文件系统
- [x] 音频播放支持
- [ ] 摄像头驱动集成
- [ ] 快递面单图像采集
- [ ] 图像预处理（畸变矫正、二值化、噪点过滤）
- [ ] 文字区域检测
- [ ] OCR 字符识别
- [ ] 识别结果提取（收件人、电话、地址、运单号）
- [ ] Wi-Fi 数据上传
- [ ] 语音播报识别结果
- [ ] 离线识别模型部署

## 快速开始

### 1. 环境准备

```bash
# 安装 ESP-IDF v5.5.4
# 参考: https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/get-started/index.html

# 初始化环境
source ~/.espressif/v5.5.4/esp-idf/export.sh
```

### 2. 编译

```bash
cd esp32_ocr

# 设置为 ESP32-P4 芯片（已完成，必要时重新运行）
idf.py set-target esp32p4

# 配置项目（选择 LCD 尺寸、芯片版本等）
idf.py menuconfig

# 编译
idf.py build
```

### 3. 烧录

```bash
# 连接 USB，确认串口设备
idf.py -p /dev/ttyACM0 flash

# 查看串口日志
idf.py -p /dev/ttyACM0 monitor
```

### 4. 重要配置说明

- **芯片版本**：sdkconfig 当前配置为 ESP32-P4 v1.x 版本。如果使用 v3.x 芯片，请通过 `idf.py menuconfig` → `Component config → ESP32P4-specific` 切换。
- **LCD 类型**：通过 menuconfig 选择 "Waveshare board with 800*800 3.4-inch Display" 或 "4-inch Display"
- **PSRAM**：默认启用 200MHz Octal PSRAM，用于图像缓冲处理
- **Flash 大小**：16MB，分区表中 factory 分区为 8MB，存储分区为 7MB

## 工作原理

```
┌──────────┐    ┌──────────┐    ┌──────────────┐    ┌──────────┐
│ 摄像头   │───▶│ 图像采集  │───▶│ 图像预处理    │───▶│  OCR    │
│ Camera   │    │ Capture   │    │ Pre-processing│    │ Engine │
└──────────┘    └──────────┘    └──────────────┘    └────┬─────┘
                                                        │
                     ┌──────────────────────────────────┘
                     ▼
               ┌──────────┐    ┌──────────────┐
               │ 结果提取  │───▶│  LVGL 界面   │
               │ Parse     │    │  Display     │
               └──────────┘    └──────────────┘
```

## 许可

本项目基于 Apache-2.0 许可证开源。部分组件遵循其各自的许可证。

## 致谢

- [Espressif](https://www.espressif.com/) - ESP32-P4 芯片 & ESP-IDF 框架
- [LVGL](https://lvgl.io/) - 轻量级嵌入式图形库
- [Waveshare](https://www.waveshare.com/) - 开发板硬件
