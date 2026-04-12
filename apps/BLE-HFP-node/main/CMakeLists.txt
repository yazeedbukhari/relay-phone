idf_component_register(SRCS "audio.c" "buttons.c" "phone.c" "gap.c" "config.c" "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES bt nvs_flash esp_driver_dac esp_driver_gpio esp_ringbuf)

target_compile_definitions(__idf_main PRIVATE
    BT_PIN="$ENV{BT_PIN}"
    REMOTE_DEVICE_NAME="$ENV{REMOTE_DEVICE_NAME}")
