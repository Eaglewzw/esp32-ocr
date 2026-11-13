#include "ocr_benchmark.hpp"
#include "sdkconfig.h"

#if CONFIG_APP_OCR_BENCHMARK

#include "app_ui.hpp"
#include "demo_images.hpp"
#include "dl_define.hpp"
#include "dl_image_jpeg.hpp"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pp_ocr_v6.hpp"
#include <cstdio>
#include <string>
#include <vector>

// Latency macros print inside Det::run(); the experiment emits only after timing.
static_assert(DL_LOG_INFER_LATENCY == 0, "Disable ESP-DL latency logging for the benchmark");

namespace {
extern const uint8_t line15_start[] asm("_binary_line15_jpg_start");
extern const uint8_t line15_end[] asm("_binary_line15_jpg_end");
extern const uint8_t line2_start[] asm("_binary_line2_jpg_start");
extern const uint8_t line2_end[] asm("_binary_line2_jpg_end");
extern const uint8_t line3_start[] asm("_binary_line3_jpg_start");
extern const uint8_t line3_end[] asm("_binary_line3_jpg_end");

constexpr int WARMUPS = 3;
constexpr int RUNS_PER_BATCH = 10;
constexpr int CORE = 1;
#if CONFIG_COMPILER_OPTIMIZATION_DEBUG
constexpr char OPTIMIZATION[] = "debug";
#elif CONFIG_COMPILER_OPTIMIZATION_PERF
constexpr char OPTIMIZATION[] = "performance";
#elif CONFIG_COMPILER_OPTIMIZATION_SIZE
constexpr char OPTIMIZATION[] = "size";
#else
constexpr char OPTIMIZATION[] = "none";
#endif

struct Sample {
    const char *id;
    const uint8_t *data;
    size_t size;
};

// UTF-8 bytes are preserved; only JSON control characters need escaping.
std::string json_string(const std::string &input)
{
    std::string result = "\"";
    for (unsigned char ch : input) {
        if (ch == '\\' || ch == '"') {
            result += '\\';
            result += ch;
        } else if (ch < 0x20) {
            char escaped[7];
            snprintf(escaped, sizeof(escaped), "\\u%04x", ch);
            result += escaped;
        } else {
            result += ch;
        }
    }
    return result + '"';
}

bool check_shapes(const pp_ocr_v6::Det &det, const Sample &sample, int size, int batch, int64_t init_us)
{
    const auto input = det.input_shape();
    const auto output = det.output_shape();
    const bool valid = det.ready() && input == std::vector<int>({1, size, size, 3}) &&
                       output == std::vector<int>({1, size, size, 1});
    printf("BENCH_JSON {\"type\":\"model\",\"sample\":\"%s\",\"size\":%d,\"batch\":%d,"
           "\"init_us\":%lld,\"input\":%s,\"output\":%s,\"valid\":%s,"
           "\"free_internal\":%u,\"free_psram\":%u}\n",
           sample.id, size, batch, init_us, dl::vector_to_string(input).c_str(),
           dl::vector_to_string(output).c_str(), valid ? "true" : "false",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    return valid;
}

void print_box(const pp_ocr_v6::TextBox &box)
{
    printf("[%d,%d,%d,%d,%d,%d,%d,%d]", box.points[0], box.points[1], box.points[2], box.points[3],
           box.points[4], box.points[5], box.points[6], box.points[7]);
}

bool measure(const Sample &sample, const dl::image::img_t &image, int size, int batch)
{
    const int64_t init_start = esp_timer_get_time();
    // 736 uses the untouched model default to preserve the original baseline.
    pp_ocr_v6::Det det("pp_ocr_v6_det_s8.espdl", size == 736 ? 0 : size);
    if (!check_shapes(det, sample, size, batch, esp_timer_get_time() - init_start)) {
        return false;
    }
    for (int iteration = 0; iteration < WARMUPS + RUNS_PER_BATCH; ++iteration) {
        // Let UART output drain and give the idle task time outside the measurement.
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(100));
        pp_ocr_v6::DetTiming timing;
        const int64_t start = esp_timer_get_time();
        const auto boxes = det.run(image, &timing);
        const int64_t total_us = esp_timer_get_time() - start;
        const bool warmup = iteration < WARMUPS;
        const int run = warmup ? iteration + 1 : (batch - 1) * RUNS_PER_BATCH + iteration - WARMUPS + 1;
        printf("BENCH_JSON {\"type\":\"run\",\"sample\":\"%s\",\"size\":%d,\"batch\":%d,"
               "\"warmup\":%s,\"run\":%d,\"pre_us\":%lld,\"model_us\":%lld,"
               "\"post_us\":%lld,\"total_us\":%lld,\"boxes\":%u,\"core\":%d}\n",
               sample.id, size, batch, warmup ? "true" : "false", run,
               timing.preprocess_us, timing.model_us, timing.postprocess_us, total_us,
               static_cast<unsigned>(boxes.size()), xPortGetCoreID());
        if (batch == 1 && iteration == WARMUPS + RUNS_PER_BATCH - 1) {
            for (size_t i = 0; i < boxes.size(); ++i) {
                printf("BENCH_JSON {\"type\":\"box\",\"sample\":\"%s\",\"size\":%d,"
                       "\"index\":%u,\"score\":%.6f,\"points\":", sample.id, size,
                       static_cast<unsigned>(i), boxes[i].score);
                print_box(boxes[i]);
                printf("}\n");
            }
        }
    }
    return true;
}

bool quality_check(const Sample &sample, const dl::image::img_t &image, int size)
{
    pp_ocr_v6::Det det("pp_ocr_v6_det_s8.espdl", size == 736 ? 0 : size);
    if (!check_shapes(det, sample, size, 0, 0)) {
        return false;
    }
    pp_ocr_v6::Rec rec("pp_ocr_v6_rec_s16.espdl");
    const auto boxes = det.run(image);
    for (size_t i = 0; i < boxes.size(); ++i) {
        float score = 0;
        const auto text = rec.run(image, boxes[i], &score);
        const auto escaped = json_string(text);
        printf("BENCH_JSON {\"type\":\"ocr\",\"sample\":\"%s\",\"size\":%d,"
               "\"index\":%u,\"score\":%.6f,\"accepted\":%s,\"text\":%s,\"points\":",
               sample.id, size, static_cast<unsigned>(i), score,
               score >= pp_ocr_v6::PPOCRV6::default_rec_score_threshold ? "true" : "false", escaped.c_str());
        print_box(boxes[i]);
        printf("}\n");
        vTaskDelay(1);
    }
    printf("BENCH_JSON {\"type\":\"quality_done\",\"sample\":\"%s\",\"size\":%d,\"boxes\":%u}\n",
           sample.id, size, static_cast<unsigned>(boxes.size()));
    return true;
}

bool run_experiment()
{
    const auto *desc = esp_app_get_description();
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    char elf_hash[65];
    for (int i = 0; i < 32; ++i) {
        snprintf(elf_hash + i * 2, 3, "%02x", desc->app_elf_sha256[i]);
    }
    printf("BENCH_JSON {\"type\":\"start\",\"protocol\":1,\"cpu_mhz\":%d,\"psram_mhz\":%d,"
           "\"core\":%d,\"priority\":%u,\"warmups_per_batch\":%d,\"runs_per_batch\":%d,"
           "\"chip_revision\":%d,\"idf\":%s,\"elf_sha256\":\"%s\",\"optimization\":\"%s\"}\n",
           CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, CONFIG_SPIRAM_SPEED, xPortGetCoreID(),
           static_cast<unsigned>(uxTaskPriorityGet(nullptr)), WARMUPS, RUNS_PER_BATCH,
           chip.revision, json_string(esp_get_idf_version()).c_str(), elf_hash, OPTIMIZATION);
    size_t count;
    const auto *demos = app::get_demo_images(count);
    if (!count) {
        return false;
    }
    const Sample samples[] = {
        {"label", demos[0].jpeg_data, demos[0].jpeg_size},
        {"line15", line15_start, static_cast<size_t>(line15_end - line15_start)},
        {"line2", line2_start, static_cast<size_t>(line2_end - line2_start)},
        {"line3", line3_start, static_cast<size_t>(line3_end - line3_start)},
    };
    for (const auto &sample : samples) {
        dl::image::jpeg_img_t jpeg = {.data = const_cast<uint8_t *>(sample.data), .data_len = sample.size};
        const int64_t decode_start = esp_timer_get_time();
        auto image = dl::image::sw_decode_jpeg(jpeg, dl::image::DL_IMAGE_PIX_TYPE_RGB888);
        printf("BENCH_JSON {\"type\":\"image\",\"sample\":\"%s\",\"width\":%u,\"height\":%u,\"decode_us\":%lld}\n",
               sample.id, image.width, image.height, esp_timer_get_time() - decode_start);
        if (!image.data) {
            return false;
        }
        bool ok = true;
        for (int batch = 1; batch <= 2 && ok; ++batch) {
            const int sizes[] = {batch == 1 ? 736 : 320, batch == 1 ? 320 : 736};
            for (int size : sizes) {
                if (!(ok = measure(sample, image, size, batch))) {
                    break;
                }
            }
        }
        // Recognition is instantiated only after all timing runs for this image.
        if (ok) {
            ok = quality_check(sample, image, 736) && quality_check(sample, image, 320);
        }
        heap_caps_free(image.data);
        if (!ok) {
            return false;
        }
    }
    return true;
}

void benchmark_task(void *arg)
{
    auto &ui = *static_cast<app::UiContext *>(arg);
    app::show_error(ui, "OCR benchmark ready\nSend b over serial");
    printf("BENCH_READY send b to start\n");
    fflush(stdout);
    while (true) {
        const int ch = getchar();
        if (ch == 'b') {
            break;
        }
        clearerr(stdin);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    app::show_error(ui, "OCR benchmark running\n736 / 320\nPlease wait");
    vTaskDelay(pdMS_TO_TICKS(1000));
    const bool ok = run_experiment();
    printf("BENCH_JSON {\"type\":\"%s\"}\n", ok ? "done" : "failed");
    fflush(stdout);
    app::show_error(ui, ok ? "OCR benchmark complete\nResults saved over serial" : "OCR benchmark failed\nCheck serial log");
    vTaskDelete(nullptr);
}
} // namespace

namespace app {
bool start_ocr_benchmark(UiContext &ui)
{
    return xTaskCreatePinnedToCore(benchmark_task, "ocr_benchmark", 32 * 1024, &ui, 5, nullptr, CORE) == pdPASS;
}
}
#endif
