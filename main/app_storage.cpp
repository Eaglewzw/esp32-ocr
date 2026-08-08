#include "app_storage.hpp"

#include "esp_log.h"
#include "nvs_flash.h"

namespace {

constexpr char TAG[] = "app_storage";

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

} // namespace

namespace app {

bool initialize_storage()
{
    return initialize_nvs();
}

} // namespace app
