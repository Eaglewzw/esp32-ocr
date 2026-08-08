#include "app_ui.hpp"
#include "app_storage.hpp"

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <new>
#include <utility>

namespace {

constexpr char TAG[] = "app_ui";
constexpr size_t FONT_GLYPH_CACHE_SIZE = 256;
constexpr size_t FALLBACK_GLYPH_CACHE_SIZE = 96;
constexpr uint32_t RESULT_BATCH_PERIOD_MS = 1;

struct ResultRenderJob {
    app::UiContext *ui = nullptr;
    std::vector<std::string> lines;
    size_t result_count = 0;
    size_t next_line = 0;
    void *decoded_image_data = nullptr;
    lv_obj_t *container = nullptr;
    int64_t start_time_us = 0;
};

const lv_font_t *font_or_default(const lv_font_t *font, const lv_font_t *fallback)
{
    return font ? font : fallback;
}

lv_font_t *create_mapped_font(const app::MappedAssetView &asset,
                              int32_t size,
                              size_t cache_size)
{
    if (!asset.data || asset.size == 0) {
        return nullptr;
    }
    return lv_tiny_ttf_create_data_ex(
        asset.data, asset.size, size, LV_FONT_KERNING_NONE, cache_size);
}

void link_font_fallback(lv_font_t *font, const lv_font_t *fallback)
{
    if (font) {
        font->fallback = fallback;
    }
}

void reset_preview(app::UiContext &ui)
{
    ui.preview = {};
}

void cancel_result_render(app::UiContext &ui)
{
    if (!ui.result_timer) {
        return;
    }

    auto *job = static_cast<ResultRenderJob *>(lv_timer_get_user_data(ui.result_timer));
    lv_timer_delete(ui.result_timer);
    ui.result_timer = nullptr;
    if (job) {
        if (job->decoded_image_data) {
            heap_caps_free(job->decoded_image_data);
        }
        delete job;
    }
}

void clear_page(app::UiContext &ui)
{
    cancel_result_render(ui);
    if (ui.page) {
        lv_obj_delete(ui.page);
        ui.page = nullptr;
    }
    ui.selector_image_descriptors.clear();
    reset_preview(ui);
}

lv_obj_t *create_page(app::UiContext &ui)
{
    clear_page(ui);
    lv_obj_t *screen = lv_display_get_screen_active(ui.display);
    ui.page = lv_obj_create(screen);
    lv_obj_remove_style_all(ui.page);
    lv_obj_set_size(ui.page, ui.screen_width, ui.screen_height);
    lv_obj_align(ui.page, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(ui.page, lv_color_hex(0x10131a), 0);
    lv_obj_set_style_bg_opa(ui.page, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui.page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui.page, LV_OBJ_FLAG_CLICKABLE);
    return ui.page;
}

void add_gesture_bubble(lv_obj_t *object)
{
    lv_obj_add_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

void invoke_return_callback(void *user_data)
{
    auto &ui = *static_cast<app::UiContext *>(user_data);
    ui.navigation_pending = false;
    if (ui.return_callback) {
        ui.return_callback(ui.callback_user_data);
    }
}

void result_gesture_callback(lv_event_t *event)
{
    auto &ui = *static_cast<app::UiContext *>(lv_event_get_user_data(event));
    lv_indev_t *indev = lv_indev_active();
    if (!indev || lv_indev_get_gesture_dir(indev) != LV_DIR_LEFT || ui.navigation_pending) {
        return;
    }

    lv_indev_wait_release(indev);
    ui.navigation_pending = true;
    if (lv_async_call(invoke_return_callback, &ui) != LV_RESULT_OK) {
        ui.navigation_pending = false;
        ESP_LOGE(TAG, "Failed to queue return to image selector");
    }
}

void attach_return_gesture(app::UiContext &ui, lv_obj_t *page)
{
    lv_obj_clear_flag(page, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(page, result_gesture_callback, LV_EVENT_GESTURE, &ui);
}

void show_loading_overlay(app::UiContext &ui)
{
    lv_obj_t *overlay = lv_obj_create(ui.page);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, ui.screen_width, ui.screen_height);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *label = lv_label_create(overlay);
    lv_label_set_text(label, "正在加载图片...");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, font_or_default(ui.cjk_font_20, &lv_font_montserrat_20), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

void selector_click_callback(lv_event_t *event)
{
    auto *card = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
    auto *ui = static_cast<app::UiContext *>(lv_obj_get_user_data(card));
    const size_t image_index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (!ui || !ui->image_selected_callback) {
        return;
    }

    if (ui->image_selected_callback(image_index, ui->callback_user_data)) {
        show_loading_overlay(*ui);
    }
}

void render_next_result(lv_timer_t *timer)
{
    auto *job = static_cast<ResultRenderJob *>(lv_timer_get_user_data(timer));
    app::UiContext &ui = *job->ui;

    if (job->next_line >= job->lines.size()) {
        const int64_t elapsed_ms = (esp_timer_get_time() - job->start_time_us) / 1000;
        ESP_LOGI(TAG, "Rendered %u OCR result lines in %lld ms on core %d",
                 static_cast<unsigned>(job->lines.size()), elapsed_ms, xPortGetCoreID());
        ui.result_timer = nullptr;
        lv_timer_delete(timer);
        delete job;
        return;
    }

    lv_obj_t *label = lv_label_create(job->container);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(label, ui.screen_width - 40);
    lv_label_set_text(label, job->lines[job->next_line].c_str());
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, font_or_default(ui.cjk_font_16, &lv_font_montserrat_16), 0);
    add_gesture_bubble(label);
    ++job->next_line;
}

void begin_result_render(void *user_data)
{
    auto *job = static_cast<ResultRenderJob *>(user_data);
    app::UiContext &ui = *job->ui;
    job->start_time_us = esp_timer_get_time();

    ESP_LOGI(TAG, "Starting batched result rendering on core %d", xPortGetCoreID());

    // Delete the preview before releasing the pixel buffer referenced by it.
    create_page(ui);
    heap_caps_free(job->decoded_image_data);
    job->decoded_image_data = nullptr;

    char title_buffer[64];
    snprintf(title_buffer, sizeof(title_buffer), "OCR 结果: %u 项", static_cast<unsigned>(job->result_count));
    lv_obj_t *title = lv_label_create(ui.page);
    lv_label_set_text(title, title_buffer);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, font_or_default(ui.cjk_font_20, &lv_font_montserrat_20), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    add_gesture_bubble(title);

    job->container = lv_obj_create(ui.page);
    lv_obj_set_size(job->container, ui.screen_width - 8, ui.screen_height - 86);
    lv_obj_align(job->container, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_bg_color(job->container, lv_color_black(), 0);
    lv_obj_set_style_border_width(job->container, 0, 0);
    lv_obj_set_style_pad_all(job->container, 8, 0);
    lv_obj_set_style_pad_row(job->container, 4, 0);
    lv_obj_set_scrollbar_mode(job->container, LV_SCROLLBAR_MODE_ON);
    lv_obj_set_scroll_dir(job->container, LV_DIR_VER);
    lv_obj_set_flex_flow(job->container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(job->container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    add_gesture_bubble(job->container);

    lv_obj_t *footer = lv_label_create(ui.page);
    lv_label_set_text(footer, "← 左滑返回图片选择");
    lv_obj_set_style_text_color(footer, lv_color_hex(0xaab4c8), 0);
    lv_obj_set_style_text_font(footer, font_or_default(ui.cjk_font_16, &lv_font_montserrat_16), 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -5);
    add_gesture_bubble(footer);

    attach_return_gesture(ui, ui.page);

    if (job->lines.empty()) {
        job->lines.emplace_back("未识别到文字");
    }

    ui.result_timer = lv_timer_create(render_next_result, RESULT_BATCH_PERIOD_MS, job);
    if (!ui.result_timer) {
        ESP_LOGE(TAG, "Failed to create result rendering timer");
        delete job;
        return;
    }
    lv_timer_ready(ui.result_timer);
}

} // namespace

namespace app {

bool initialize_ui(UiContext &ui, int lvgl_core_id)
{
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = {
            .task_stack_size = ESP_LV_ADAPTER_DEFAULT_STACK_SIZE,
            .task_priority = ESP_LV_ADAPTER_DEFAULT_TASK_PRIORITY,
            .task_core_id = lvgl_core_id,
            .tick_period_ms = ESP_LV_ADAPTER_DEFAULT_TICK_PERIOD_MS,
            .task_min_delay_ms = ESP_LV_ADAPTER_DEFAULT_TASK_MIN_DELAY_MS,
            .task_max_delay_ms = ESP_LV_ADAPTER_DEFAULT_TASK_MAX_DELAY_MS,
            .stack_in_psram = false,
            .auto_sleep = {
                .enable = false,
                .mode = ESP_LV_ADAPTER_AUTO_SLEEP_MODE_DISABLED,
                .idle_timeout_ms = ESP_LV_ADAPTER_DEFAULT_AUTO_SLEEP_TIMEOUT_MS,
                .callbacks = {},
            },
        },
    };

    ui.display = bsp_display_start_with_config(&cfg);
    if (!ui.display) {
        ESP_LOGE(TAG, "Failed to start LVGL display");
        return false;
    }

    esp_err_t ret = bsp_display_backlight_on();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable display backlight: %s", esp_err_to_name(ret));
    }

    ui.screen_width = lv_display_get_horizontal_resolution(ui.display);
    ui.screen_height = lv_display_get_vertical_resolution(ui.display);

    if (!bsp_display_lock(-1)) {
        ESP_LOGE(TAG, "Failed to lock LVGL while loading fonts");
        return false;
    }

    MappedAssetView cjk_asset;
    MappedAssetView demo_fallback_asset;
    const bool has_cjk_asset = get_font_asset(FontAsset::CjkFull, cjk_asset);
    const bool has_demo_fallback_asset = get_font_asset(FontAsset::DemoFallback, demo_fallback_asset);

    if (has_demo_fallback_asset) {
        ui.demo_fallback_font_20 = create_mapped_font(
            demo_fallback_asset, 20, FALLBACK_GLYPH_CACHE_SIZE);
        ui.demo_fallback_font_16 = create_mapped_font(
            demo_fallback_asset, 16, FALLBACK_GLYPH_CACHE_SIZE);
        link_font_fallback(ui.demo_fallback_font_20, &lv_font_montserrat_20);
        link_font_fallback(ui.demo_fallback_font_16, &lv_font_montserrat_16);
    }

    if (has_cjk_asset) {
        ui.cjk_font_20 = create_mapped_font(cjk_asset, 20, FONT_GLYPH_CACHE_SIZE);
        ui.cjk_font_16 = create_mapped_font(cjk_asset, 16, FONT_GLYPH_CACHE_SIZE);
    }

    const lv_font_t *fallback_20 = font_or_default(
        ui.demo_fallback_font_20, &lv_font_montserrat_20);
    const lv_font_t *fallback_16 = font_or_default(
        ui.demo_fallback_font_16, &lv_font_montserrat_16);
    link_font_fallback(ui.cjk_font_20, fallback_20);
    link_font_fallback(ui.cjk_font_16, fallback_16);

    // If the full font could not be created, the demo subset still provides a
    // useful Chinese UI instead of dropping immediately to an ASCII-only font.
    if (!ui.cjk_font_20) {
        ui.cjk_font_20 = ui.demo_fallback_font_20;
    }
    if (!ui.cjk_font_16) {
        ui.cjk_font_16 = ui.demo_fallback_font_16;
    }
    bsp_display_unlock();

    if (!ui.cjk_font_16 || !ui.cjk_font_20) {
        ESP_LOGW(TAG, "Failed to create one or more mapped CJK fonts; using fallback fonts where possible");
    }

    ESP_LOGI(TAG,
             "LVGL initialized on core %d (%dx%d), fonts mapped from flash: CJK=%u bytes, fallback=%u bytes",
             lvgl_core_id,
             ui.screen_width,
             ui.screen_height,
             static_cast<unsigned>(cjk_asset.size),
             static_cast<unsigned>(demo_fallback_asset.size));
    return true;
}

bool show_image_selector(UiContext &ui,
                         const DemoImage *images,
                         size_t image_count,
                         ImageSelectedCallback image_selected_callback,
                         ReturnToSelectorCallback return_callback,
                         void *callback_user_data)
{
    if (!ui.display || !images || image_count == 0 || !image_selected_callback || !bsp_display_lock(-1)) {
        ESP_LOGE(TAG, "Failed to lock LVGL while showing image selector");
        return false;
    }

    ui.image_selected_callback = image_selected_callback;
    ui.return_callback = return_callback;
    ui.callback_user_data = callback_user_data;
    ui.navigation_pending = false;
    create_page(ui);

    lv_obj_t *title = lv_label_create(ui.page);
    lv_label_set_text(title, "ESP32-P4 OCR 演示");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, font_or_default(ui.cjk_font_20, &lv_font_montserrat_20), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *hint = lv_label_create(ui.page);
    lv_label_set_text(hint, "请选择一张图片开始识别");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xaab4c8), 0);
    lv_obj_set_style_text_font(hint, font_or_default(ui.cjk_font_16, &lv_font_montserrat_16), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 38);

    lv_obj_t *list = lv_obj_create(ui.page);
    lv_obj_set_size(list, ui.screen_width - 12, ui.screen_height - 68);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 5, 0);
    lv_obj_set_style_pad_row(list, 7, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ui.selector_image_descriptors.resize(image_count);
    // Keep all three samples visible on the board's 320x480 display. The list
    // reserves 10 px for outer padding and 14 px for the two row gaps.
    const int card_height = std::max(96, (ui.screen_height - 92) / 3);
    for (size_t index = 0; index < image_count; ++index) {
        lv_image_dsc_t &descriptor = ui.selector_image_descriptors[index];
        descriptor = {};
        descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
        descriptor.header.cf = LV_COLOR_FORMAT_RAW;
        descriptor.header.w = images[index].width;
        descriptor.header.h = images[index].height;
        descriptor.data = images[index].jpeg_data;
        descriptor.data_size = images[index].jpeg_size;

        lv_obj_t *card = lv_button_create(list);
        lv_obj_set_size(card, ui.screen_width - 32, card_height);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x222938), 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_pad_all(card, 6, 0);
        lv_obj_set_style_pad_row(card, 4, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_user_data(card, &ui);
        lv_obj_add_event_cb(card,
                            selector_click_callback,
                            LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(index)));

        lv_obj_t *thumbnail = lv_image_create(card);
        lv_image_set_src(thumbnail, &descriptor);
        lv_obj_set_size(thumbnail, ui.screen_width - 48, card_height - 38);
        lv_image_set_inner_align(thumbnail, LV_IMAGE_ALIGN_CONTAIN);
        lv_obj_add_flag(thumbnail, LV_OBJ_FLAG_EVENT_BUBBLE);

        lv_obj_t *name = lv_label_create(card);
        lv_label_set_text(name, images[index].display_name);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_set_style_text_font(name, font_or_default(ui.cjk_font_16, &lv_font_montserrat_16), 0);
        lv_obj_add_flag(name, LV_OBJ_FLAG_EVENT_BUBBLE);
    }

    bsp_display_unlock();
    ESP_LOGI(TAG, "Image selector ready with %u samples", static_cast<unsigned>(image_count));
    return true;
}

bool show_error(UiContext &ui, const char *message)
{
    if (!ui.display || !bsp_display_lock(-1)) {
        ESP_LOGE(TAG, "Failed to lock LVGL while showing an error");
        return false;
    }

    create_page(ui);
    lv_obj_t *error_label = lv_label_create(ui.page);
    lv_label_set_text(error_label, message);
    lv_obj_set_style_text_color(error_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(error_label, font_or_default(ui.cjk_font_20, &lv_font_montserrat_20), 0);
    lv_obj_align(error_label, LV_ALIGN_CENTER, 0, -20);
    add_gesture_bubble(error_label);

    if (ui.return_callback) {
        lv_obj_t *hint = lv_label_create(ui.page);
        lv_label_set_text(hint, "← 左滑返回图片选择");
        lv_obj_set_style_text_color(hint, lv_color_hex(0xaab4c8), 0);
        lv_obj_set_style_text_font(hint, font_or_default(ui.cjk_font_16, &lv_font_montserrat_16), 0);
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, 22);
        add_gesture_bubble(hint);
        attach_return_gesture(ui, ui.page);
    }

    bsp_display_unlock();
    return true;
}

bool show_ocr_preview(UiContext &ui, const dl::image::img_t &source_image, const char *image_name)
{
    if (!ui.display || !source_image.data || source_image.width == 0 || source_image.height == 0) {
        return false;
    }
    if (!bsp_display_lock(-1)) {
        ESP_LOGE(TAG, "Failed to lock LVGL for preview");
        return false;
    }

    create_page(ui);
    OcrPreview &preview = ui.preview;

    preview.title = lv_label_create(ui.page);
    char title_buffer[96];
    snprintf(title_buffer, sizeof(title_buffer), "%s · OCR 识别中...", image_name ? image_name : "图片");
    lv_label_set_text(preview.title, title_buffer);
    lv_obj_set_style_text_color(preview.title, lv_color_white(), 0);
    lv_obj_set_style_text_font(preview.title, font_or_default(ui.cjk_font_20, &lv_font_montserrat_20), 0);
    lv_obj_set_style_bg_color(preview.title, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(preview.title, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(preview.title, 8, 0);
    lv_obj_set_width(preview.title, ui.screen_width);
    lv_obj_align(preview.title, LV_ALIGN_TOP_MID, 0, 0);

    preview.image_descriptor = {};
    preview.image_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    preview.image_descriptor.header.cf = LV_COLOR_FORMAT_RGB888;
    preview.image_descriptor.header.w = source_image.width;
    preview.image_descriptor.header.h = source_image.height;
    preview.image_descriptor.data = static_cast<const uint8_t *>(source_image.data);
    preview.image_descriptor.data_size = source_image.width * source_image.height * 3;

    preview.image = lv_image_create(ui.page);
    lv_image_set_src(preview.image, &preview.image_descriptor);

    const float scale_x = static_cast<float>(ui.screen_width - 16) / source_image.width;
    const float scale_y = static_cast<float>(ui.screen_height - 60) / source_image.height;
    const float scale = scale_x < scale_y ? scale_x : scale_y;
    lv_obj_set_size(preview.image,
                    static_cast<int>(source_image.width * scale),
                    static_cast<int>(source_image.height * scale));
    lv_image_set_inner_align(preview.image, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_align(preview.image, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_image_recolor_opa(preview.image, LV_OPA_80, 0);

    preview.overlay = lv_label_create(ui.page);
    lv_label_set_text(preview.overlay, "OCR 识别中...");
    lv_obj_set_style_text_color(preview.overlay, lv_color_white(), 0);
    lv_obj_set_style_text_font(preview.overlay, font_or_default(ui.cjk_font_20, &lv_font_montserrat_20), 0);
    lv_obj_set_style_bg_color(preview.overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(preview.overlay, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(preview.overlay, 12, 0);
    lv_obj_set_style_radius(preview.overlay, 8, 0);
    lv_obj_align(preview.overlay, LV_ALIGN_CENTER, 0, 0);

    preview.active = true;
    bsp_display_unlock();
    return true;
}

bool queue_ocr_results(UiContext &ui,
                       std::vector<std::string> result_lines,
                       size_t result_count,
                       void *decoded_image_data)
{
    auto *job = new (std::nothrow) ResultRenderJob;
    if (!job) {
        ESP_LOGE(TAG, "Failed to allocate result rendering job");
        return false;
    }

    job->ui = &ui;
    job->lines = std::move(result_lines);
    job->result_count = result_count;
    job->decoded_image_data = decoded_image_data;

    if (!bsp_display_lock(-1)) {
        ESP_LOGE(TAG, "Failed to lock LVGL while queuing OCR results");
        delete job;
        return false;
    }
    const lv_result_t result = lv_async_call(begin_result_render, job);
    bsp_display_unlock();

    if (result != LV_RESULT_OK) {
        ESP_LOGE(TAG, "Failed to queue OCR result rendering");
        delete job;
        return false;
    }
    return true;
}

} // namespace app
