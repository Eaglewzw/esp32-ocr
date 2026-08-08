idf_component_register(
    SRCS
        main.cpp
        app_storage.cpp
        app_ui.cpp
        demo_images.cpp
        ocr_pipeline.cpp
    INCLUDE_DIRS .
    REQUIRES pp_ocr_v6 esp-dl esp32_p4_wifi6_touch_lcd_35
             espressif__esp_mmap_assets nvs_flash spi_flash
    EMBED_FILES
        test_images/pp_ocr_v6.jpg
        test_images/ocr_example_1.jpg
        test_images/ocr_example_2.jpg
)

# Package fonts in the dedicated 7 MB "storage" partition. The local packer
# emits the esp_mmap_assets binary format without requiring Pillow/QOI.
set(FONT_ASSET_DIR "${CMAKE_CURRENT_LIST_DIR}/font_assets")
file(GLOB FONT_ASSET_FILES CONFIGURE_DEPENDS "${FONT_ASSET_DIR}/*.ttf")
set(FONT_ASSET_BUILD_DIR "${CMAKE_BINARY_DIR}/font_assets")
set(FONT_ASSET_BIN "${FONT_ASSET_BUILD_DIR}/storage.bin")
set(FONT_ASSET_HEADER "${CMAKE_CURRENT_BINARY_DIR}/mmap_generate_storage.h")

idf_build_get_property(python PYTHON)
add_custom_command(
    OUTPUT "${FONT_ASSET_BIN}" "${FONT_ASSET_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FONT_ASSET_BUILD_DIR}"
    COMMAND ${python}
            "${PROJECT_DIR}/tools/pack_font_assets.py"
            --input-dir "${FONT_ASSET_DIR}"
            --output-bin "${FONT_ASSET_BIN}"
            --output-header "${FONT_ASSET_HEADER}"
            --partition storage
            --max-size 0x700000
    DEPENDS ${FONT_ASSET_FILES} "${PROJECT_DIR}/tools/pack_font_assets.py"
    COMMENT "Packing memory-mapped TTF fonts into storage.bin"
    VERBATIM
)
add_custom_target(font_assets_bin ALL DEPENDS "${FONT_ASSET_BIN}" "${FONT_ASSET_HEADER}")

target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
add_dependencies(${COMPONENT_LIB} font_assets_bin)
esptool_py_flash_to_partition(flash storage "${FONT_ASSET_BIN}")
add_dependencies(flash font_assets_bin)
