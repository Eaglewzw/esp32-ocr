# ESP32-P4 快递面单 OCR

基于 ESP32-P4 + LVGL 的端侧 OCR 识别系统，摄像头拍摄快递面单，触摸屏实时显示识别结果。

## 硬件

- 主控：[ESP32-P4]（双核 400MHz）
- 开发板：[Waveshare ESP32-P4-WIFI6-Touch-LCD-XC]（3.5 寸 800×800 触摸屏）
- 存储：16MB Flash / 7MB SPIFFS

## 核心技术栈

[ESP-IDF] 5.5 · [LVGL] 9.4 · Wi-Fi 6 · PSRAM · SD 卡 · 音频播放

## 开发进度

- [x] LVGL 图形界面 · 触摸驱动 · SD 卡 · 音频
- [ ] 摄像头采集 · 图像预处理 · 文字检测 · OCR 识别 · 语音播报

## 快速开始

```bash
source ~/.espressif/v5.5.4/esp-idf/export.sh
idf.py set-target esp32p4
idf.py menuconfig   # 选择 LCD 型号、芯片版本
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

> **芯片版本**：sdkconfig 默认适配 v1.x，v3.x 芯片需在 menuconfig → `Component config → ESP32P4-specific` 切换。
> **LCD**：默认 800×800 3.4 寸，4 寸屏在 menuconfig 切换。

## 许可

Apache-2.0

[ESP32-P4]: https://www.espressif.com/en/products/socs/esp32-p4
[Waveshare ESP32-P4-WIFI6-Touch-LCD-XC]: https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-XC
[ESP-IDF]: https://github.com/espressif/esp-idf
[LVGL]: https://github.com/lvgl/lvgl
