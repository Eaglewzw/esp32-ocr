#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "dl_image_jpeg.hpp"
#include "pp_ocr_v6.hpp"

static const char *TAG = "ocr";

extern const uint8_t pp_ocr_v6_jpg_start[] asm("_binary_pp_ocr_v6_jpg_start");
extern const uint8_t pp_ocr_v6_jpg_end[] asm("_binary_pp_ocr_v6_jpg_end");

static lv_obj_t *result_label;

static void log_ocr_result(const pp_ocr_v6::OCRResult &res)
{
    ESP_LOGI(TAG,
             "text=\"%s\", score=%.4f, box=[%d,%d %d,%d %d,%d %d,%d]",
             res.text.c_str(), res.score,
             res.box.points[0], res.box.points[1],
             res.box.points[2], res.box.points[3],
             res.box.points[4], res.box.points[5],
             res.box.points[6], res.box.points[7]);
}

extern "C" void app_main(void)
{
    // Init NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Init LVGL display
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
    };
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    bsp_display_lock(-1);

    // Create result label on screen
    result_label = lv_label_create(lv_scr_act());
    lv_label_set_text(result_label, "OCR initializing...");
    lv_obj_set_style_text_color(result_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(result_label, &lv_font_montserrat_16, 0);
    lv_obj_align(result_label, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_label_set_long_mode(result_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(result_label, lv_display_get_horizontal_resolution(disp) - 20);

    ESP_LOGI(TAG, "Decoding JPEG...");
    dl::image::jpeg_img_t jpeg_img = {
        .data = (void *)pp_ocr_v6_jpg_start,
        .data_len = (size_t)(pp_ocr_v6_jpg_end - pp_ocr_v6_jpg_start)
    };

    auto img = dl::image::sw_decode_jpeg(jpeg_img, dl::image::DL_IMAGE_PIX_TYPE_RGB888);
    if (!img.data) {
        ESP_LOGE(TAG, "Failed to decode JPEG");
        lv_label_set_text(result_label, "Failed to decode JPEG image!");
        bsp_display_unlock();
        return;
    }

    lv_label_set_text(result_label, "Running OCR...");
    ESP_LOGI(TAG, "Running OCR...");

    auto *ocr = new pp_ocr_v6::PPOCRV6();
    auto results = ocr->run(img);

    // Build result string
    char buf[4096] = {0};
    int offset = snprintf(buf, sizeof(buf), "OCR Results (%u items):\n", (unsigned)results.size());
    for (const auto &res : results) {
        log_ocr_result(res);
        offset += snprintf(buf + offset, sizeof(buf) - offset,
                           "  %s (%.2f)\n",
                           res.text.c_str(), res.score);
    }

    lv_label_set_text(result_label, buf);
    ESP_LOGI(TAG, "Done! %u results", (unsigned)results.size());

    delete ocr;
    heap_caps_free(img.data);
    bsp_display_unlock();
}
