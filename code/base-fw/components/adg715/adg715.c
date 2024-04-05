#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "freertos/FreeRTOS.h"

#include "adg715_interface.h"
#include "adg715.h"

static const char *TAG = "esp_adg715";

esp_err_t adg715_init(const adg715_config_t* config, adg715_handle_t** out_handle)
{
    esp_err_t err;

    err = i2c_master_probe(config->i2c_bus, config->i2c_addr, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to find device with address %x", config->i2c_addr);
        return err;
    }

    adg715_handle_t* handle = (adg715_handle_t*)malloc(sizeof(adg715_handle_t));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate memory for ADG715 device");
        return ESP_ERR_NO_MEM;
    }

    // copy config into handle
    *handle = (adg715_handle_t) {
        .config = *config,
    };

    // Setup reset pin
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << config->reset_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Hold reset high
    gpio_set_level(config->reset_pin, 1);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_addr,
        .scl_speed_hz = 100000,
    };

    err = i2c_master_bus_add_device(config->i2c_bus, &dev_cfg, &(handle->i2c_dev));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C device");
        adg715_deinit(handle); // Release resources
    }

    *out_handle = handle;
    return ESP_OK;
}

esp_err_t adg715_deinit(adg715_handle_t* handle)
{
    // Release all the resources
    gpio_reset_pin(handle->config.reset_pin);
    if(handle->i2c_dev) {
        i2c_master_bus_rm_device(handle->i2c_dev);
        handle->i2c_dev = NULL;
    }

    free(handle);
    return ESP_OK;
}

esp_err_t adg715_set(adg715_handle_t* handle, uint8_t state)
{
    // Set the state of the ADG715 switches
    return i2c_master_transmit(handle->i2c_dev, &state, 1, 10);
}

esp_err_t adg715_get(adg715_handle_t* handle, uint8_t* ret_val)
{
    // Get the state of the ADG715 switches
    return i2c_master_receive(handle->i2c_dev, ret_val, 1, 10);
}

esp_err_t adg715_reset(adg715_handle_t* handle)
{
    // Reset the ADG715
    gpio_set_level(handle->config.reset_pin, 0);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    gpio_set_level(handle->config.reset_pin, 1);
    return ESP_OK;
}

