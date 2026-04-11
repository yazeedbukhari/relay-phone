idf_component_register(SRCS "phone.c" "gap.c" "config.c" "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES bt nvs_flash esp_driver_dac)

target_compile_definitions(__idf_main PRIVATE
    BT_PIN="$ENV{BT_PIN}"
    REMOTE_DEVICE_NAME="$ENV{REMOTE_DEVICE_NAME}")
