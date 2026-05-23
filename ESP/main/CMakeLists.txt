idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES my_serial my_wifi my_mqtt my_screen my_ui my_ota my_nvs)
