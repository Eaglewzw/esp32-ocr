#include "pp_ocr_v6.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "pp_ocr_v6_det_postprocessor.hpp"
#include "pp_ocr_v6_image_preprocessor.hpp"
#include "pp_ocr_v6_rec_postprocessor.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_PP_OCR_V6_MODEL_IN_FLASH_RODATA
extern const uint8_t pp_ocr_v6_espdl[] asm("_binary_pp_ocr_v6_espdl_start");
static const char *model_path = (const char *)pp_ocr_v6_espdl;
#elif CONFIG_PP_OCR_V6_MODEL_IN_FLASH_PARTITION
static const char *model_path = "pp_ocr_v6";
#else
#if !defined(CONFIG_BSP_SD_MOUNT_POINT)
#define CONFIG_BSP_SD_MOUNT_POINT "/sdcard"
#endif
#endif

static const char *TAG = "pp_ocr_v6";

namespace pp_ocr_v6 {
Det::Det(const char *model_name, int input_size) :
    m_model(nullptr), m_image_preprocessor(nullptr), m_postprocessor(nullptr), m_letterbox_img({}), m_resize_scale(1.0f)
{
    std::map<std::string, std::vector<int>> shapes;
    if (input_size != 0) {
        assert(input_size > 0 && input_size % 32 == 0);
        shapes.emplace("x", std::vector<int>{1, input_size, input_size, 3});
    }
#if CONFIG_PP_OCR_V6_MODEL_IN_SDCARD
    auto sd_path = std::filesystem::path(CONFIG_BSP_SD_MOUNT_POINT) / CONFIG_PP_OCR_V6_MODEL_SDCARD_DIR / model_name;
    m_model = new dl::Model(sd_path.c_str(), fbs::MODEL_LOCATION_IN_SDCARD,
                            0, dl::MEMORY_MANAGER_GREEDY, nullptr, true, shapes);
#else
    m_model =
        new dl::Model(model_path, model_name, static_cast<fbs::model_location_type_t>(CONFIG_PP_OCR_V6_MODEL_LOCATION),
                       0, dl::MEMORY_MANAGER_GREEDY, nullptr, true, shapes);
#endif
    m_model->minimize();
    m_image_preprocessor =
        new dl::image::ImagePreprocessor(m_model, {123.675f, 116.28f, 103.53f}, {58.395f, 57.12f, 57.375f}, true);
    m_postprocessor = new DetPostprocessor(m_model);

    auto *model_input = m_image_preprocessor->get_model_input();
    int model_h = model_input->shape[1];
    int model_w = model_input->shape[2];
    void *buf = heap_caps_aligned_alloc(16, model_w * model_h * 3, MALLOC_CAP_SPIRAM);
    if (!buf) {
        buf = heap_caps_aligned_alloc(16, model_w * model_h * 3, MALLOC_CAP_DEFAULT);
    }
    m_letterbox_img = {.data = buf,
                       .width = static_cast<uint16_t>(model_w),
                       .height = static_cast<uint16_t>(model_h),
                       .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888};
}

Det::~Det()
{
    if (m_letterbox_img.data) {
        heap_caps_free(m_letterbox_img.data);
    }
    delete m_postprocessor;
    delete m_image_preprocessor;
    delete m_model;
}

std::vector<int> Det::input_shape() const
{
    const auto inputs = m_model->get_inputs();
    return inputs.empty() ? std::vector<int>{} : inputs.begin()->second->shape;
}

std::vector<int> Det::output_shape() const
{
    const auto outputs = m_model->get_outputs();
    return outputs.empty() ? std::vector<int>{} : outputs.begin()->second->shape;
}

std::vector<TextBox> Det::run(const dl::image::img_t &img, DetTiming *timing)
{
    if (timing) {
        *timing = {};
    }
    if (!img.data || !img.width || !img.height || !ready()) {
        ESP_LOGE(TAG, "Invalid detection image or unavailable letterbox buffer");
        return {};
    }
    const int64_t start_us = timing ? esp_timer_get_time() : 0;
    DL_LOG_INFER_LATENCY_INIT();
    DL_LOG_INFER_LATENCY_START();
    int model_w = m_letterbox_img.width;
    int model_h = m_letterbox_img.height;
    float scale_x = static_cast<float>(model_w) / img.width;
    float scale_y = static_cast<float>(model_h) / img.height;
    m_resize_scale = std::min(scale_x, scale_y);
    int resize_w = std::max(1, static_cast<int>(std::round(img.width * m_resize_scale)));
    int resize_h = std::max(1, static_cast<int>(std::round(img.height * m_resize_scale)));
    resize_w = std::min(resize_w, model_w);
    resize_h = std::min(resize_h, model_h);
    bilinear_letterbox_top_left(img, m_letterbox_img, resize_w, resize_h);
    m_image_preprocessor->preprocess(m_letterbox_img);
    const int64_t pre_end_us = timing ? esp_timer_get_time() : 0;
    DL_LOG_INFER_LATENCY_END_PRINT("det", "pre");

    DL_LOG_INFER_LATENCY_START();
    m_model->run();
    const int64_t model_end_us = timing ? esp_timer_get_time() : 0;
    DL_LOG_INFER_LATENCY_END_PRINT("det", "model");

    DL_LOG_INFER_LATENCY_START();
    auto boxes = m_postprocessor->postprocess(img, m_resize_scale, model_w, model_h);
    if (timing) {
        const int64_t end_us = esp_timer_get_time();
        timing->preprocess_us = pre_end_us - start_us;
        timing->model_us = model_end_us - pre_end_us;
        timing->postprocess_us = end_us - model_end_us;
    }
    DL_LOG_INFER_LATENCY_END_PRINT("det", "post");

    return boxes;
}

Rec::Rec(const char *model_name) : m_model(nullptr), m_image_preprocessor(nullptr), m_postprocessor(nullptr)
{
#if CONFIG_PP_OCR_V6_MODEL_IN_SDCARD
    auto sd_path = std::filesystem::path(CONFIG_BSP_SD_MOUNT_POINT) / CONFIG_PP_OCR_V6_MODEL_SDCARD_DIR / model_name;
    m_model = new dl::Model(sd_path.c_str(), fbs::MODEL_LOCATION_IN_SDCARD);
#else
    m_model =
        new dl::Model(model_path, model_name, static_cast<fbs::model_location_type_t>(CONFIG_PP_OCR_V6_MODEL_LOCATION));
#endif
    m_model->minimize();
    m_image_preprocessor =
        new dl::image::ImagePreprocessor(m_model, {127.5f, 127.5f, 127.5f}, {127.5f, 127.5f, 127.5f}, true);
    m_postprocessor = new RecCTCPostprocessor(m_model);
}

Rec::~Rec()
{
    delete m_postprocessor;
    delete m_image_preprocessor;
    delete m_model;
}

float Rec::input_aspect_ratio() const
{
    if (!m_image_preprocessor) {
        return 0.0f;
    }
    auto *input = m_image_preprocessor->get_model_input();
    if (!input || input->shape.size() < 4) {
        return 0.0f;
    }
    int h = input->shape[1];
    int w = input->shape[2];
    if (h <= 0 || w <= 0) {
        return 0.0f;
    }
    return static_cast<float>(w) / static_cast<float>(h);
}

std::string Rec::run(const dl::image::img_t &img, const TextBox &box, float *score)
{
    DL_LOG_INFER_LATENCY_INIT();
    DL_LOG_INFER_LATENCY_START();
    dl::image::img_t crop = get_rotate_crop_image(img, box);
    if (!crop.data) {
        if (score) {
            *score = 0.0f;
        }
        return {};
    }

    dl::TensorBase *model_input = m_image_preprocessor->get_model_input();
    bool input_ready = resize_norm_img(crop, model_input);
    heap_caps_free(crop.data);
    if (!input_ready) {
        if (score) {
            *score = 0.0f;
        }
        return {};
    }
    DL_LOG_INFER_LATENCY_END_PRINT("rec", "pre");

    DL_LOG_INFER_LATENCY_START();
    m_model->run();
    DL_LOG_INFER_LATENCY_END_PRINT("rec", "model");

    DL_LOG_INFER_LATENCY_START();
    std::string text = m_postprocessor->postprocess(score);
    DL_LOG_INFER_LATENCY_END_PRINT("rec", "post");

    return text;
}

static const char *rec_model_name_for(RecModel type)
{
    switch (type) {
    case RecModel::PP_OCR_V6_REC_S16:
#if CONFIG_FLASH_PP_OCR_V6_REC_S16 || CONFIG_PP_OCR_V6_MODEL_IN_SDCARD
        return "pp_ocr_v6_rec_s16.espdl";
#else
        ESP_LOGE(TAG, "pp_ocr_v6_rec_s16 not flashed; falling back to pp_ocr_v6_rec_s8.");
        return "pp_ocr_v6_rec_s8.espdl";
#endif
    case RecModel::PP_OCR_V6_REC_S8:
    default:
        return "pp_ocr_v6_rec_s8.espdl";
    }
}

static const char *rec_wide_model_name_for()
{
#if CONFIG_FLASH_PP_OCR_V6_REC_S16_W640 || CONFIG_PP_OCR_V6_MODEL_IN_SDCARD
    return "pp_ocr_v6_rec_s16_w640.espdl";
#else
    return nullptr;
#endif
}

PPOCRV6::PPOCRV6(RecMode rec_mode, RecModel rec_type, int det_input_size) :
    m_det("pp_ocr_v6_det_s8.espdl", det_input_size),
    m_rec(rec_model_name_for(rec_type)),
    m_rec_long(nullptr),
    m_dual_aspect_ratio_threshold(0.0),
    m_rec_score_threshold(default_rec_score_threshold)
{
    const auto shape = m_det.input_shape();
    ESP_LOGI(TAG, "Detect input: %d x %d", shape[2], shape[1]);
    if (rec_mode == RecMode::Dual) {
        const char *wide = rec_wide_model_name_for();
        if (wide) {
            m_rec_long = new Rec(wide);
            // Both models are S16, so route at the short model's native W/H.
            m_dual_aspect_ratio_threshold = m_rec.input_aspect_ratio();
            ESP_LOGI(TAG,
                     "rec mode: DUAL (short='%s' W/H=%.2f, long='%s' W/H=%.2f); "
                     "route long when crop W/H > %.2f",
                     rec_model_name_for(rec_type),
                     m_rec.input_aspect_ratio(),
                     wide,
                     m_rec_long->input_aspect_ratio(),
                     m_dual_aspect_ratio_threshold);
        } else {
            ESP_LOGW(TAG,
                     "rec mode DUAL requested but pp_ocr_v6_rec_s16_w640.espdl is not flashed; "
                     "falling back to SHORT.");
        }
    } else {
        ESP_LOGI(TAG, "rec mode: SHORT ('%s', W/H=%.2f)", rec_model_name_for(rec_type), m_rec.input_aspect_ratio());
    }
}

PPOCRV6::~PPOCRV6()
{
    delete m_rec_long;
}

std::vector<OCRResult> PPOCRV6::run(const dl::image::img_t &img)
{
    auto boxes = m_det.run(img);
    std::vector<OCRResult> results;
    results.reserve(boxes.size());
    for (const auto &box : boxes) {
        float rec_score = 0.0f;

        Rec *rec = &m_rec;
        if (m_rec_long && m_dual_aspect_ratio_threshold > 0.0f) {
            float aspect_ratio = estimate_crop_aspect_ratio(box);
            if (aspect_ratio > m_dual_aspect_ratio_threshold) {
                rec = m_rec_long;
            }
        }
        auto text = rec->run(img, box, &rec_score);
        if (rec_score >= m_rec_score_threshold) {
            results.push_back({box, text, rec_score});
        }
        vTaskDelay(1);
    }
    return results;
}

} // namespace pp_ocr_v6
