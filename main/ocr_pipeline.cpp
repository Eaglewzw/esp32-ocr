#include "ocr_pipeline.hpp"

#include "app_ui.hpp"
#include "demo_images.hpp"
#include "dl_image_jpeg.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pp_ocr_v6.hpp"
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr char TAG[] = "ocr_pipeline";
constexpr size_t MAX_RESULT_TEXT_SIZE = 8192;

void log_ocr_result(const pp_ocr_v6::OCRResult &result)
{
    ESP_LOGI(TAG,
             "text=\"%s\", score=%.4f, box=[%d,%d %d,%d %d,%d %d,%d]",
             result.text.c_str(), result.score,
             result.box.points[0], result.box.points[1],
             result.box.points[2], result.box.points[3],
             result.box.points[4], result.box.points[5],
             result.box.points[6], result.box.points[7]);
}

std::vector<std::string> format_results(const std::vector<pp_ocr_v6::OCRResult> &results)
{
    std::vector<std::string> lines;
    lines.reserve(results.size());
    size_t total_size = 0;
    for (const auto &result : results) {
        log_ocr_result(result);
        const int percentage = static_cast<int>(result.score * 100);
        std::string line = result.text + "  (" + std::to_string(percentage) + "%)";
        if (total_size + line.size() > MAX_RESULT_TEXT_SIZE) {
            lines.emplace_back("...");
            break;
        }
        total_size += line.size();
        lines.emplace_back(std::move(line));
    }
    return lines;
}

} // namespace

namespace app {

void run_ocr_pipeline(UiContext &ui, const DemoImage &selected_image)
{
    ESP_LOGI(TAG, "Decoding '%s' on core %d...", selected_image.display_name, xPortGetCoreID());
    dl::image::jpeg_img_t jpeg_image = {
        .data = const_cast<uint8_t *>(selected_image.jpeg_data),
        .data_len = selected_image.jpeg_size,
    };

    auto image = dl::image::sw_decode_jpeg(jpeg_image, dl::image::DL_IMAGE_PIX_TYPE_RGB888);
    if (!image.data) {
        ESP_LOGE(TAG, "Failed to decode JPEG");
        show_error(ui, "解码失败!");
        return;
    }

    if (!show_ocr_preview(ui, image, selected_image.display_name)) {
        heap_caps_free(image.data);
        return;
    }

    // Model::run() defaults to single-core mode, keeping inference on the OCR task's core.
    ESP_LOGI(TAG, "Running OCR on core %d...", xPortGetCoreID());
    // Keep the models loaded between demo runs so subsequent selections start faster.
    static pp_ocr_v6::PPOCRV6 ocr;
    auto results = ocr.run(image);
    auto result_lines = format_results(results);

    if (queue_ocr_results(ui, std::move(result_lines), results.size(), image.data)) {
        ESP_LOGI(TAG, "Queued %u results for rendering on the LVGL core", static_cast<unsigned>(results.size()));
    } else {
        // show_error() removes the preview before the referenced pixels are released.
        ESP_LOGE(TAG, "Failed to queue OCR result rendering");
        if (show_error(ui, "OCR 结果显示失败!")) {
            heap_caps_free(image.data);
        } else {
            ESP_LOGE(TAG, "Keeping decoded image allocated because the active preview still references it");
        }
    }
}

} // namespace app
