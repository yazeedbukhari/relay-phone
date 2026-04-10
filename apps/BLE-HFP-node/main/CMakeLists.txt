idf_component_register(SRCS "gap.c" "config.c" "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES bt nvs_flash)

target_compile_definitions(__idf_main PRIVATE BT_PIN="$ENV{BT_PIN}")
