#include "app_storage.hpp"

#include "esp_log.h"
#include "esp_mmap_assets.h"
#include "mmap_generate_storage.h"
#include "nvs_flash.h"

namespace {

constexpr char TAG[] = "app_storage";
constexpr char FONT_PARTITION_LABEL[] = "storage";

mmap_assets_handle_t font_assets_handle = nullptr;

bool initialize_nvs()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ret = nvs_flash_erase();
        if (ret == ESP_OK) {
            ret = nvs_flash_init();
        }
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return false;
    }
    return true;
}

bool initialize_font_assets()
{
    if (font_assets_handle) {
        return true;
    }

    mmap_assets_config_t config = {};
    config.partition_label = FONT_PARTITION_LABEL;
    config.max_files = MMAP_STORAGE_FILES;
    config.checksum = MMAP_STORAGE_CHECKSUM;
    config.flags.mmap_enable = true;
    config.flags.use_fs = false;
    config.flags.app_bin_check = true;
    config.flags.full_check = false;
    config.flags.metadata_check = true;

    const esp_err_t ret = mmap_assets_new(&config, &font_assets_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to map font assets from '%s': %s",
                 FONT_PARTITION_LABEL, esp_err_to_name(ret));
        font_assets_handle = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "Mapped %d font assets directly from flash",
             mmap_assets_get_stored_files(font_assets_handle));
    return true;
}

int font_asset_index(app::FontAsset asset)
{
    switch (asset) {
    case app::FontAsset::CjkFull:
        return MMAP_STORAGE_CJK_FULL_TTF;
    case app::FontAsset::DemoFallback:
        return MMAP_STORAGE_DEMO_FALLBACK_TTF;
    }
    return -1;
}

} // namespace

namespace app {

bool initialize_storage()
{
    const bool nvs_ready = initialize_nvs();
    const bool fonts_ready = initialize_font_assets();
    return nvs_ready && fonts_ready;
}

bool get_font_asset(FontAsset asset, MappedAssetView &view)
{
    view = {};
    if (!font_assets_handle) {
        return false;
    }

    const int index = font_asset_index(asset);
    if (index < 0) {
        return false;
    }

    const uint8_t *data = mmap_assets_get_mem(font_assets_handle, index);
    const int size = mmap_assets_get_size(font_assets_handle, index);
    const char *name = mmap_assets_get_name(font_assets_handle, index);
    if (!data || size <= 0 || !name) {
        ESP_LOGE(TAG, "Invalid mapped font asset at index %d", index);
        return false;
    }

    view.data = data;
    view.size = static_cast<size_t>(size);
    view.name = name;
    return true;
}

} // namespace app
