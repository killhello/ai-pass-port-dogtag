idf_component_register(
    SRCS "main.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "demo_radio.c"
         "demo_dogtag.c"
         "ble_dogtag.c"
         "dogtag_state.c"
         "dogtag_ui.c"
         "dogtag_audio.c"
         "font_cn_16.c"
         "font_cn_20.c"
    INCLUDE_DIRS "."
    EMBED_FILES "dogtag_help.wav"
    REQUIRES bsp bt esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash
)
