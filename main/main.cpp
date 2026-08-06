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

    int scr_w = lv_display_get_horizontal_resolution(disp);
    int scr_h = lv_display_get_vertical_resolution(disp);

    bsp_display_lock(-1);

    // --- Title bar ---
    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "OCR - ");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_bg_color(title, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(title, 8, 0);
    lv_obj_set_width(title, scr_w);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    // --- Scrollable result area ---
    lv_obj_t *cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, scr_w, scr_h - 40);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_ON);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);

    // --- Status item ---
    lv_obj_t *status_label = lv_label_create(cont);
    lv_label_set_text(status_label, "Decoding JPEG...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);

    lv_label_set_text(title, "OCR - Decoding...");

    ESP_LOGI(TAG, "Decoding JPEG...");
    dl::image::jpeg_img_t jpeg_img = {
        .data = (void *)pp_ocr_v6_jpg_start,
        .data_len = (size_t)(pp_ocr_v6_jpg_end - pp_ocr_v6_jpg_start)
    };

    auto img = dl::image::sw_decode_jpeg(jpeg_img, dl::image::DL_IMAGE_PIX_TYPE_RGB888);
    if (!img.data) {
        ESP_LOGE(TAG, "Failed to decode JPEG");
        lv_label_set_text(status_label, "Failed to decode JPEG!");
        lv_label_set_text(title, "OCR - Error");
        bsp_display_unlock();
        return;
    }

    lv_label_set_text(status_label, "Running OCR...");
    lv_label_set_text(title, "OCR - Running...");

    ESP_LOGI(TAG, "Running OCR...");
    auto *ocr = new pp_ocr_v6::PPOCRV6();
    auto results = ocr->run(img);

    // Display results
    char buf[128];
    snprintf(buf, sizeof(buf), "OCR Results: %u items", (unsigned)results.size());
    lv_label_set_text(status_label, buf);
    lv_label_set_text(title, buf);

    for (const auto &res : results) {
        log_ocr_result(res);

        // Result item container
        lv_obj_t *item = lv_obj_create(cont);
        lv_obj_set_width(item, scr_w - 24);
        lv_obj_set_height(item, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x16213e), 0);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_pad_all(item, 6, 0);
        lv_obj_set_style_radius(item, 6, 0);

        // Text
        lv_obj_t *text_label = lv_label_create(item);
        lv_label_set_text(text_label, res.text.c_str());
        lv_obj_set_style_text_color(text_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(text_label, &lv_font_montserrat_16, 0);
        lv_label_set_long_mode(text_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(text_label, scr_w - 100);

        // Score badge
        char score_str[32];
        snprintf(score_str, sizeof(score_str), "%.0f%%", res.score * 100);
        lv_obj_t *score_label = lv_label_create(item);
        lv_label_set_text(score_label, score_str);
        lv_obj_set_style_text_color(score_label,
            res.score > 0.95 ? lv_color_hex(0x4ecca3) :
            res.score > 0.80 ? lv_color_hex(0xf0a500) : lv_color_hex(0xe74c3c), 0);
        lv_obj_set_style_text_font(score_label, &lv_font_montserrat_12, 0);
        lv_obj_align(score_label, LV_ALIGN_RIGHT_MID, -4, 0);
    }

    ESP_LOGI(TAG, "Done! %u results", (unsigned)results.size());

    delete ocr;
    heap_caps_free(img.data);
    bsp_display_unlock();

    // Keep running - LVGL task handles display refresh
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
