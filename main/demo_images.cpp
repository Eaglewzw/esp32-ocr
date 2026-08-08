#include "demo_images.hpp"

namespace {

extern const uint8_t pp_ocr_v6_jpg_start[]
    asm("_binary_pp_ocr_v6_jpg_start");
extern const uint8_t pp_ocr_v6_jpg_end[]
    asm("_binary_pp_ocr_v6_jpg_end");
extern const uint8_t ocr_example_1_jpg_start[]
    asm("_binary_ocr_example_1_jpg_start");
extern const uint8_t ocr_example_1_jpg_end[]
    asm("_binary_ocr_example_1_jpg_end");
extern const uint8_t ocr_example_2_jpg_start[]
    asm("_binary_ocr_example_2_jpg_start");
extern const uint8_t ocr_example_2_jpg_end[]
    asm("_binary_ocr_example_2_jpg_end");

} // namespace

namespace app {

const DemoImage *get_demo_images(size_t &count)
{
    static const DemoImage images[] = {
        {
            .display_name = "快递面单",
            .jpeg_data = pp_ocr_v6_jpg_start,
            .jpeg_size = static_cast<size_t>(pp_ocr_v6_jpg_end - pp_ocr_v6_jpg_start),
            .width = 750,
            .height = 1170,
        },
        {
            .display_name = "安全号码样例 1",
            .jpeg_data = ocr_example_1_jpg_start,
            .jpeg_size = static_cast<size_t>(ocr_example_1_jpg_end - ocr_example_1_jpg_start),
            .width = 711,
            .height = 132,
        },
        {
            .display_name = "安全号码样例 2",
            .jpeg_data = ocr_example_2_jpg_start,
            .jpeg_size = static_cast<size_t>(ocr_example_2_jpg_end - ocr_example_2_jpg_start),
            .width = 995,
            .height = 200,
        },
    };

    count = sizeof(images) / sizeof(images[0]);
    return images;
}

} // namespace app
