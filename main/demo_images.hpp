#pragma once

#include <cstddef>
#include <cstdint>

namespace app {

struct DemoImage {
    const char *display_name;
    const uint8_t *jpeg_data;
    size_t jpeg_size;
    uint16_t width;
    uint16_t height;
};

const DemoImage *get_demo_images(size_t &count);

} // namespace app
