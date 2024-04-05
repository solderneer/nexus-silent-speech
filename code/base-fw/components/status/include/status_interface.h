#pragma once

#include "led_strip.h"
#include "driver/gpio.h"

/// Configuration of status interface
typedef struct {
    gpio_num_t led_pin; ///< GPIO pin number of LED
} status_config_t;

typedef struct {
    status_config_t config; ///< User passed configuration of status interface
    led_strip_handle_t led;  ///< User passed configuration of status interface
} status_handle_t;


esp_err_t status_init(const status_config_t* config, status_handle_t** out_handle);
esp_err_t status_red(status_handle_t* handle);
esp_err_t status_yellow(status_handle_t* handle);
esp_err_t status_green(status_handle_t* handle);