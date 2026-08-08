#pragma once

#include "demo_images.hpp"
#include "dl_image_define.hpp"
#include "lvgl.h"
#include <cstddef>
#include <string>
#include <vector>

namespace app {

using ImageSelectedCallback = bool (*)(size_t image_index, void *user_data);
using ReturnToSelectorCallback = void (*)(void *user_data);

struct OcrPreview {
    lv_obj_t *title = nullptr;
    lv_obj_t *image = nullptr;
    lv_obj_t *overlay = nullptr;
    lv_image_dsc_t image_descriptor = {};
    bool active = false;
};

struct UiContext {
    lv_display_t *display = nullptr;
    lv_font_t *cjk_font_20 = nullptr;
    lv_font_t *cjk_font_16 = nullptr;
    lv_font_t *demo_fallback_font_20 = nullptr;
    lv_font_t *demo_fallback_font_16 = nullptr;
    int screen_width = 0;
    int screen_height = 0;
    lv_obj_t *page = nullptr;
    lv_timer_t *result_timer = nullptr;
    std::vector<lv_image_dsc_t> selector_image_descriptors;
    ImageSelectedCallback image_selected_callback = nullptr;
    ReturnToSelectorCallback return_callback = nullptr;
    void *callback_user_data = nullptr;
    bool navigation_pending = false;
    OcrPreview preview;
};

bool initialize_ui(UiContext &ui, int lvgl_core_id);
bool show_image_selector(UiContext &ui,
                         const DemoImage *images,
                         size_t image_count,
                         ImageSelectedCallback image_selected_callback,
                         ReturnToSelectorCallback return_callback,
                         void *callback_user_data);
bool show_error(UiContext &ui, const char *message);
bool show_ocr_preview(UiContext &ui, const dl::image::img_t &image, const char *image_name);

// Queue result rendering on the LVGL core. Ownership of decoded_image_data is
// transferred only when this function returns true.
bool queue_ocr_results(UiContext &ui,
                       std::vector<std::string> result_lines,
                       size_t result_count,
                       void *decoded_image_data);

} // namespace app
