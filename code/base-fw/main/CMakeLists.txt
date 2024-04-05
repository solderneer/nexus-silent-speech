idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_wifi nvs_flash adg715 ads1299 status)
