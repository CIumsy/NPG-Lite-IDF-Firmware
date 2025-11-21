file(GLOB_RECURSE srcs "main.c" "src/*.c")

idf_component_register(SRCS "${srcs}"
                       PRIV_REQUIRES bt nvs_flash esp_driver_gpio esp_adc esp_pm
                       INCLUDE_DIRS "./include")

