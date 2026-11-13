#include "app_storage.hpp"
#include "app_ui.hpp"
#include "demo_controller.hpp"
#include "ocr_benchmark.hpp"

#include "esp_log.h"
#include "sdkconfig.h"

extern "C" void app_main(void)
{
    constexpr char TAG[] = "ocr";
    constexpr int LVGL_CORE_ID = 0;
    constexpr int OCR_CORE_ID = 1;
    static app::UiContext ui;

    // Initialize storage and the touchscreen UI.
    if (!app::initialize_storage()) {
        ESP_LOGW(TAG, "Storage initialization was incomplete; UI will use fallback resources where possible");
    }
    if (!app::initialize_ui(ui, LVGL_CORE_ID)) {
        return;
    }

    // Start the selected mode; the demo controller handles user interaction.
#if CONFIG_APP_OCR_BENCHMARK
    if (!app::start_ocr_benchmark(ui)) {
        app::show_error(ui, "Benchmark task failed");
    }
#else
    if (!app::start_demo(ui, OCR_CORE_ID)) {
        return;
    }
    ESP_LOGI(TAG, "LVGL pinned to core %d; OCR pinned to core %d", LVGL_CORE_ID, OCR_CORE_ID);
#endif
}
