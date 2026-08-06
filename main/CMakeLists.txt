idf_component_register(
    SRCS main.cpp
    INCLUDE_DIRS .
    REQUIRES pp_ocr_v6 esp-dl esp32_p4_wifi6_touch_lcd_35 nvs_flash
    EMBED_FILES pp_ocr_v6.jpg
)
