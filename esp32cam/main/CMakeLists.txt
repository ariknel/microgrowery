idf_component_register(
    SRCS
        "main.c"
        "camera.c"
        "uart_link.c"
        "flash_led.c"
    INCLUDE_DIRS "."
    REQUIRES
        freertos
        driver
        json
        esp_timer
        esp32-camera
)
