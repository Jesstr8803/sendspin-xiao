idf_component_register(
    SRCS
        "main.cpp"
        "i2s_audio_sink.cpp"
        "wifi_init.cpp"
        "mdns_init.cpp"
        "status_led.cpp"
        "nvs_persistence.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        driver
        mdns
        pthread
)
