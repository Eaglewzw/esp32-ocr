#pragma once

#include <cstddef>
#include <cstdint>

namespace app {

enum class FontAsset {
    CjkFull,
    DemoFallback,
};

struct MappedAssetView {
    const uint8_t *data = nullptr;
    size_t size = 0;
    const char *name = nullptr;
};

// Initialize NVS and memory-map the font asset partition.
bool initialize_storage();

// The returned memory remains valid for the lifetime of the application.
bool get_font_asset(FontAsset asset, MappedAssetView &view);

} // namespace app
