#pragma once
#include "driver/gpio.h"
#include "driver/i2c_master.h"

// Configuration of ADG715 interface
typedef struct {
    i2c_master_bus_handle_t i2c_bus; ///< I2C bus to use
    uint8_t i2c_addr;                ///< I2C address of ADG715 
    gpio_num_t reset_pin;            ///< ADG715 reset pin 
} adg715_config_t;

typedef struct {
    adg715_config_t config;           ///< User passed configuration of ADG715 interface
    i2c_master_dev_handle_t i2c_dev;  ///< I2C device handle
} adg715_handle_t;

/******* PUBLIC FUNCTIONS *********/
esp_err_t adg715_init(const adg715_config_t* config, adg715_handle_t** out_handle);
esp_err_t adg715_deinit(adg715_handle_t* handle);

esp_err_t adg715_set(adg715_handle_t* handle, uint8_t state);
esp_err_t adg715_get(adg715_handle_t* handle, uint8_t* ret_val);
esp_err_t adg715_reset(adg715_handle_t* handle);
