idf_component_register(
    SRCS
        main.cpp
        app_storage.cpp
        app_ui.cpp
        demo_images.cpp
        ocr_pipeline.cpp
    INCLUDE_DIRS .
    REQUIRES pp_ocr_v6 esp-dl esp32_p4_wifi6_touch_lcd_35 nvs_flash spi_flash
    EMBED_FILES
        test_images/pp_ocr_v6.jpg
        test_images/ocr_example_1.jpg
        test_images/ocr_example_2.jpg
        noto_sc_ocr.ttf
)
