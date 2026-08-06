#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "sys/stat.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "dl_image_jpeg.hpp"
#include "pp_ocr_v6.hpp"

static const char *TAG = "ocr";

extern const uint8_t pp_ocr_v6_jpg_start[] asm("_binary_pp_ocr_v6_jpg_start");
extern const uint8_t pp_ocr_v6_jpg_end[] asm("_binary_pp_ocr_v6_jpg_end");
extern const uint8_t noto_sc_ocr_ttf_start[] asm("_binary_noto_sc_ocr_ttf_start");
extern const uint8_t noto_sc_ocr_ttf_end[] asm("_binary_noto_sc_ocr_ttf_end");

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

static void init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted at /spiffs");
    } else {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(ret));
    }
}

static void write_font_to_spiffs(void)
{
    struct stat st;
    if (stat("/spiffs/noto_sc_ocr.ttf", &st) == 0) {
        ESP_LOGI(TAG, "Font already on SPIFFS (%ld bytes)", st.st_size);
        return;
    }
    size_t font_size = noto_sc_ocr_ttf_end - noto_sc_ocr_ttf_start;
    FILE *f = fopen("/spiffs/noto_sc_ocr.ttf", "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to create font file on SPIFFS");
        return;
    }
    fwrite(noto_sc_ocr_ttf_start, 1, font_size, f);
    fclose(f);
    ESP_LOGI(TAG, "Font written to SPIFFS (%u bytes)", (unsigned)font_size);
}

extern "C" void app_main(void)
{
    // Init NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Mount SPIFFS and write font file
    init_spiffs();
    write_font_to_spiffs();

    // Init LVGL display
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
    };
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    int scr_w = lv_display_get_horizontal_resolution(disp);
    int scr_h = lv_display_get_vertical_resolution(disp);

    // Create Chinese font from SPIFFS
    lv_font_t *cjk_font_20 = lv_freetype_font_create("/spiffs/noto_sc_ocr.ttf",
        LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 20, LV_FREETYPE_FONT_STYLE_NORMAL);
    lv_font_t *cjk_font_16 = lv_freetype_font_create("/spiffs/noto_sc_ocr.ttf",
        LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 16, LV_FREETYPE_FONT_STYLE_NORMAL);

    if (!cjk_font_16) {
        ESP_LOGE(TAG, "Failed to create CJK font!");
    }

    // ---- Decode JPEG first (no LVGL lock) ----
    ESP_LOGI(TAG, "Decoding JPEG...");
    dl::image::jpeg_img_t jpeg_img = {
        .data = (void *)pp_ocr_v6_jpg_start,
        .data_len = (size_t)(pp_ocr_v6_jpg_end - pp_ocr_v6_jpg_start)
    };

    auto img = dl::image::sw_decode_jpeg(jpeg_img, dl::image::DL_IMAGE_PIX_TYPE_RGB888);
    if (!img.data) {
        ESP_LOGE(TAG, "Failed to decode JPEG");
        bsp_display_lock(-1);
        lv_obj_t *err = lv_label_create(lv_scr_act());
        lv_label_set_text(err, "解码失败!");
        lv_obj_set_style_text_color(err, lv_color_white(), 0);
        lv_obj_align(err, LV_ALIGN_CENTER, 0, 0);
        bsp_display_unlock();
        return;
    }

    // ---- Show JPEG image while OCR runs ----
    bsp_display_lock(-1);

    // Title bar
    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "OCR 识别中...");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, cjk_font_20 ? cjk_font_20 : &lv_font_montserrat_20, 0);
    lv_obj_set_style_bg_color(title, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(title, 8, 0);
    lv_obj_set_width(title, scr_w);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    // Show the JPEG as an LVGL image
    lv_image_dsc_t img_dsc = {};
    img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    img_dsc.header.cf = LV_COLOR_FORMAT_RGB888;
    img_dsc.header.w = img.width;
    img_dsc.header.h = img.height;
    img_dsc.data = (const uint8_t *)img.data;
    img_dsc.data_size = img.width * img.height * 3;

    lv_obj_t *jpeg_view = lv_image_create(lv_scr_act());
    lv_image_set_src(jpeg_view, &img_dsc);

    // Scale to fit screen while keeping aspect ratio
    float scale_x = (float)(scr_w - 16) / img.width;
    float scale_y = (float)(scr_h - 60) / img.height;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;
    int scaled_w = (int)(img.width * scale);
    int scaled_h = (int)(img.height * scale);
    lv_obj_set_size(jpeg_view, scaled_w, scaled_h);
    lv_obj_align(jpeg_view, LV_ALIGN_BOTTOM_MID, 0, -4);

    lv_obj_set_style_image_recolor_opa(jpeg_view, LV_OPA_80, 0);

    // Progress overlay
    lv_obj_t *overlay = lv_label_create(lv_scr_act());
    lv_label_set_text(overlay, "OCR 识别中...");
    lv_obj_set_style_text_color(overlay, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(overlay, cjk_font_20 ? cjk_font_20 : &lv_font_montserrat_20, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(overlay, 12, 0);
    lv_obj_set_style_radius(overlay, 8, 0);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);

    bsp_display_unlock();

    // ---- Run OCR (no lock) ----
    ESP_LOGI(TAG, "Running OCR...");
    auto *ocr = new pp_ocr_v6::PPOCRV6();
    auto results = ocr->run(img);

    // ---- Build result text ----
    char buf[8192] = {0};
    int offset = 0;
    for (const auto &res : results) {
        log_ocr_result(res);
        int pct = (int)(res.score * 100);
        offset += snprintf(buf + offset, sizeof(buf) - offset,
                           "%s  (%d%%)\n",
                           res.text.c_str(), pct);
    }

    // ---- Switch to results view ----
    bsp_display_lock(-1);

    // Remove image and overlay, keep title
    lv_obj_del(jpeg_view);
    lv_obj_del(overlay);

    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), "OCR 结果: %u 项", (unsigned)results.size());
    lv_label_set_text(title, title_buf);

    // Scrollable result container
    lv_obj_t *cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cont, scr_w - 8, scr_h - 50);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_ON);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);

    lv_obj_t *result_label = lv_label_create(cont);
    lv_label_set_text(result_label, buf);
    lv_obj_set_style_text_color(result_label, lv_color_white(), 0);
    if (cjk_font_16) {
        lv_obj_set_style_text_font(result_label, cjk_font_16, 0);
    }
    lv_obj_set_width(result_label, scr_w - 32);

    ESP_LOGI(TAG, "Done! %u results", (unsigned)results.size());

    bsp_display_unlock();

    delete ocr;
    heap_caps_free(img.data);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
