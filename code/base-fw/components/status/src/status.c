#include "led_strip.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "status_interface.h"

#define LED_COUNT 1
#define LED_RMT_RES_HZ  (10 * 1000 * 1000)

static const char *TAG = "status_light";

esp_err_t status_init(const status_config_t* config, status_handle_t** out_handle)
{
    status_handle_t* handle = (status_handle_t*)malloc(sizeof(status_handle_t));
    if (!handle)
        return ESP_ERR_NO_MEM;
    
    // copy config into handle
    *handle = (status_handle_t) {
        .config = *config,
    };

    // LED strip general initialization
    led_strip_config_t strip_config = {
        .strip_gpio_num = config->led_pin,        // The GPIO that connected to the LED strip's data line
        .max_leds = 1,                            // There is only one LED on the board
        .led_pixel_format = LED_PIXEL_FORMAT_GRB, // Pixel format of your LED strip
        .led_model = LED_MODEL_WS2812,            // LED strip model
        .flags.invert_out = false,                // whether to invert the output signal
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
        .resolution_hz = LED_RMT_RES_HZ,       // RMT counter clock frequency
        .flags.with_dma = false,               // DMA feature is available on ESP target like ESP32-S3
    };

    // LED Strip object handle
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &(handle->led)));
    ESP_LOGI(TAG, "Created LED strip object with RMT backend");

    *out_handle = handle;
    return ESP_OK;
}

esp_err_t status_red(status_handle_t* handle)
{
    ESP_ERROR_CHECK(led_strip_set_pixel(handle->led, 0, 20, 0, 0));
    ESP_ERROR_CHECK(led_strip_refresh(handle->led));
    return ESP_OK;
}

esp_err_t status_yellow(status_handle_t* handle)
{
    ESP_ERROR_CHECK(led_strip_set_pixel(handle->led, 0, 10, 10, 0));
    ESP_ERROR_CHECK(led_strip_refresh(handle->led));
    return ESP_OK;
}

esp_err_t status_green(status_handle_t* handle)
{
    ESP_ERROR_CHECK(led_strip_set_pixel(handle->led, 0, 0, 20, 0));
    ESP_ERROR_CHECK(led_strip_refresh(handle->led));
    return ESP_OK;
}



