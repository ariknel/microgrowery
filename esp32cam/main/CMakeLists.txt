idf_component_register(
    SRCS
        "main.c"
        "camera.c"
        "uart_link.c"
        "wifi_link.c"
    INCLUDE_DIRS "."
    REQUIRES
        freertos
        driver
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        json
        esp32-camera
)
