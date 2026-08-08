#include "app_storage.hpp"
#include "app_ui.hpp"
#include "demo_images.hpp"
#include "ocr_pipeline.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <atomic>

namespace {

constexpr char TAG[] = "ocr";
constexpr BaseType_t LVGL_CORE_ID = 0;
constexpr BaseType_t OCR_CORE_ID = 1;
constexpr UBaseType_t OCR_TASK_PRIORITY = 5;
constexpr uint32_t OCR_TASK_STACK_SIZE = 32 * 1024;

app::UiContext ui_context;

struct DemoController {
    const app::DemoImage *images = nullptr;
    size_t image_count = 0;
    QueueHandle_t request_queue = nullptr;
    std::atomic<bool> ocr_busy{false};
};

DemoController demo_controller;

void ocr_task(void *arg)
{
    auto &controller = *static_cast<DemoController *>(arg);
    ESP_LOGI(TAG, "OCR task running on core %d", xPortGetCoreID());

    const app::DemoImage *selected_image = nullptr;
    while (xQueueReceive(controller.request_queue, &selected_image, portMAX_DELAY) == pdTRUE) {
        if (selected_image) {
            app::run_ocr_pipeline(ui_context, *selected_image);
            ESP_LOGI(TAG, "OCR run finished on core %d", xPortGetCoreID());
        }
        controller.ocr_busy.store(false, std::memory_order_release);
    }
}

bool select_image(size_t image_index, void *user_data)
{
    auto &controller = *static_cast<DemoController *>(user_data);
    if (image_index >= controller.image_count) {
        return false;
    }

    bool expected = false;
    if (!controller.ocr_busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        ESP_LOGW(TAG, "Ignoring image selection while OCR is busy");
        return false;
    }

    const app::DemoImage *selected_image = &controller.images[image_index];
    if (xQueueSend(controller.request_queue, &selected_image, 0) != pdTRUE) {
        controller.ocr_busy.store(false, std::memory_order_release);
        ESP_LOGE(TAG, "Failed to queue selected image");
        return false;
    }

    ESP_LOGI(TAG, "Selected image: %s", selected_image->display_name);
    return true;
}

void return_to_selector(void *user_data)
{
    auto &controller = *static_cast<DemoController *>(user_data);
    if (controller.ocr_busy.load(std::memory_order_acquire)) {
        return;
    }

    if (!app::show_image_selector(ui_context,
                                  controller.images,
                                  controller.image_count,
                                  select_image,
                                  return_to_selector,
                                  &controller)) {
        ESP_LOGE(TAG, "Failed to return to image selector");
    }
}

} // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "app_main running on core %d", xPortGetCoreID());

    if (!app::initialize_storage()) {
        ESP_LOGW(TAG, "Storage initialization was incomplete; UI will use fallback resources where possible");
    }

    if (!app::initialize_ui(ui_context, LVGL_CORE_ID)) {
        return;
    }

    demo_controller.images = app::get_demo_images(demo_controller.image_count);
    demo_controller.request_queue = xQueueCreate(1, sizeof(const app::DemoImage *));
    if (!demo_controller.request_queue) {
        ESP_LOGE(TAG, "Failed to create OCR request queue");
        app::show_error(ui_context, "OCR 请求队列创建失败!");
        return;
    }

    BaseType_t task_created = xTaskCreatePinnedToCore(
        ocr_task,
        "ocr_task",
        OCR_TASK_STACK_SIZE,
        &demo_controller,
        OCR_TASK_PRIORITY,
        nullptr,
        OCR_CORE_ID);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OCR task on core %d", OCR_CORE_ID);
        app::show_error(ui_context, "OCR 任务创建失败!");
        return;
    }

    if (!app::show_image_selector(ui_context,
                                  demo_controller.images,
                                  demo_controller.image_count,
                                  select_image,
                                  return_to_selector,
                                  &demo_controller)) {
        ESP_LOGE(TAG, "Failed to show image selector");
        return;
    }

    ESP_LOGI(TAG, "LVGL pinned to core %d; OCR pinned to core %d", LVGL_CORE_ID, OCR_CORE_ID);
}
