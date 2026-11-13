#include "demo_controller.hpp"
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
constexpr UBaseType_t OCR_TASK_PRIORITY = 5;
constexpr uint32_t OCR_TASK_STACK_SIZE = 32 * 1024;

struct DemoController {
    app::UiContext *ui = nullptr;
    const app::DemoImage *images = nullptr;
    size_t image_count = 0;
    QueueHandle_t request_queue = nullptr;
    TaskHandle_t task = nullptr;
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
            app::run_ocr_pipeline(*controller.ui, *selected_image);
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

    if (!app::show_image_selector(*controller.ui,
                                  controller.images,
                                  controller.image_count,
                                  select_image,
                                  return_to_selector,
                                  &controller)) {
        ESP_LOGE(TAG, "Failed to return to image selector");
    }
}

} // namespace

namespace app {

bool start_demo(UiContext &ui, int ocr_core_id)
{
    auto &controller = demo_controller;
    if (controller.task) {
        ESP_LOGW(TAG, "Demo is already running");
        return false;
    }

    controller.ui = &ui;
    controller.images = get_demo_images(controller.image_count);
    controller.request_queue = xQueueCreate(1, sizeof(const DemoImage *));
    if (!controller.request_queue) {
        ESP_LOGE(TAG, "Failed to create OCR request queue");
        show_error(ui, "OCR 请求队列创建失败!");
        return false;
    }

    BaseType_t task_created = xTaskCreatePinnedToCore(
        ocr_task,
        "ocr_task",
        OCR_TASK_STACK_SIZE,
        &controller,
        OCR_TASK_PRIORITY,
        &controller.task,
        ocr_core_id);
    if (task_created != pdPASS) {
        vQueueDelete(controller.request_queue);
        controller.request_queue = nullptr;
        controller.task = nullptr;
        ESP_LOGE(TAG, "Failed to create OCR task on core %d", ocr_core_id);
        show_error(ui, "OCR 任务创建失败!");
        return false;
    }

    if (!show_image_selector(ui,
                             controller.images,
                             controller.image_count,
                             select_image,
                             return_to_selector,
                             &controller)) {
        vTaskDelete(controller.task);
        controller.task = nullptr;
        vQueueDelete(controller.request_queue);
        controller.request_queue = nullptr;
        ESP_LOGE(TAG, "Failed to show image selector");
        return false;
    }

    return true;
}

} // namespace app
