#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "ads1299.h"
#include "ads1299_interface.h"

static const char *TAG = "esp_ads1299";

esp_err_t ads1299_init(const ads1299_config_t* config, ads1299_handle_t** out_handle)
{
    esp_err_t err = ESP_OK;

    ads1299_handle_t* handle = (ads1299_handle_t*)malloc(sizeof(ads1299_handle_t));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate memory for ADS1299 device");
        return ESP_ERR_NO_MEM;
    }
    
    // copy config into handle
    *handle = (ads1299_handle_t) {
        .config = *config,
    };

    // Setup DRDY pin
    gpio_config_t gpio_conf = {
        .pin_bit_mask = (1ULL << config->drdy_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&gpio_conf);

    // Setup reset pin
    gpio_conf.pin_bit_mask = (1ULL << config->reset_pin);
    gpio_conf.mode = GPIO_MODE_OUTPUT;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&gpio_conf);

    // Hold reset high
    gpio_set_level(config->reset_pin, 1);

    // Setup SPI bus
    spi_device_interface_config_t spi_cfg = {
        .command_bits = 8, // Standard commands are 1 byte, except for RREG and WREG
        .address_bits = 0, // Not using this
        .dummy_bits = 0, // Not using this, either
        .mode = 1, // SPI mode 1
        .clock_speed_hz = config->spi_clock_speed_hz,
        .spics_io_num = config->cs_pin,
        .cs_ena_posttrans = 5, // Always hold CS high for 4 t_clk cycles after communication
        .queue_size = 2,
        .input_delay_ns = 0, // TODO: update this for faster, better timing performance
        .flags = 0
    };

    // Attach ads1299 to spi bus
    err = spi_bus_add_device(config->spi_host, &spi_cfg, &(handle->spi));
    if (err == ESP_OK) {
        // Success!

        // Initialization steps
        ads1299_reset(handle);
        ads1299_sdatac(handle);
        _ads1299_rreg(handle, ADS_ID, &(handle->id));
        ESP_LOGI(TAG, "ADS1299 ID: %d", handle->id);
        _ads1299_wreg(handle, ADS_CONFIG2, 0xD5);
        _ads1299_wreg(handle, ADS_CONFIG3, 0xFC);
        // _ads1299_wreg(handle, ADS_MISC1, 0x20);

        int ch = 0;
        for (ch = 0; ch < 8; ch++)
            _ads1299_wreg(handle, ADS_CH1SET + ch, 0x60); // configure to default 24x gain and normal input

        ads1299_start(handle);
        ads1299_rdatac(handle);

        *out_handle = handle;
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to initialize SPI device");
        ads1299_deinit(handle); // Release resources
        return err;
    }
}

esp_err_t ads1299_deinit(ads1299_handle_t* handle)
{
    esp_err_t err = ESP_OK;

    // Deregister gpio pin
    gpio_reset_pin(handle->config.drdy_pin);

    if (handle->spi) {
        err = spi_bus_remove_device(handle->spi);
        handle->spi = NULL;
    }

    free(handle); // Release the allocated heap memory
    return err;
}

int ads1299_ready(ads1299_handle_t* handle)
{
    return !gpio_get_level(handle->config.drdy_pin);
}

esp_err_t ads1299_read(ads1299_handle_t* handle, uint32_t* status, int32_t res[])
{
    const uint8_t data_len = 8;

    uint8_t transmit_buf[27] = {0x00};
    uint8_t receive_buf[27] = {0x00};

    spi_transaction_ext_t t = {
        .base = (spi_transaction_t) {
            .flags = SPI_TRANS_VARIABLE_CMD,
            .length = (27 * 8),
            .tx_buffer = transmit_buf,
            .rx_buffer = receive_buf
        },
        .command_bits = 0
    };

    esp_err_t err = spi_device_transmit(handle->spi, &t);

    if (err != ESP_OK) return err;

    // Parse the received data
    for (int i = 0; i < data_len; i++) {
        // Off by 4 because first three is status
        // Sign conversion from 24 bit to 32 bit
        res[i] = (int32_t) (((receive_buf[i*3+3] & 0x80) ? (0xFF) : (0x00)) << 24 |
                ((receive_buf[i*3+3] & 0xFF) << 16) |
                ((receive_buf[i*3+4] & 0xFF) << 8)  |
                ((receive_buf[i*3+5] & 0xFF) << 0));
    }

    *status = (receive_buf[0] << 16) | (receive_buf[1] << 8) | receive_buf[2];
    return ESP_OK;
}

esp_err_t ads1299_acquire_bus(ads1299_handle_t* handle)
{
    return spi_device_acquire_bus(handle->spi, portMAX_DELAY);
}

esp_err_t ads1299_release_bus(ads1299_handle_t* handle)
{
    spi_device_release_bus(handle->spi);
    return ESP_OK;
}

esp_err_t ads1299_cmd(ads1299_handle_t* handle, uint8_t cmd)
{
    spi_transaction_t t = {
        .cmd = cmd,
        .length = 0,
    };

    esp_err_t err = spi_device_transmit(handle->spi, &t);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

esp_err_t ads1299_wakeup(ads1299_handle_t* handle) { return ads1299_cmd(handle, ADS_WAKEUP); }
esp_err_t ads1299_standby(ads1299_handle_t* handle) { return ads1299_cmd(handle, ADS_STANDBY); }
esp_err_t ads1299_start(ads1299_handle_t* handle) { return ads1299_cmd(handle, ADS_START); }
esp_err_t ads1299_stop(ads1299_handle_t* handle) { return ads1299_cmd(handle, ADS_STOP); }
esp_err_t ads1299_rdatac(ads1299_handle_t* handle) { return ads1299_cmd(handle, ADS_RDATAC); }
esp_err_t ads1299_sdatac(ads1299_handle_t* handle) { return ads1299_cmd(handle, ADS_SDATAC); }
esp_err_t ads1299_reset(ads1299_handle_t* handle) { return ads1299_cmd(handle, ADS_RESET); }

esp_err_t ads1299_set_ch_all(ads1299_handle_t* handle, bool en)
{
    uint8_t reg = 0;
    esp_err_t ret = ESP_OK;

    ads1299_sdatac(handle);
    for (int ch = 0; ch < 8; ch++) {
        ret = _ads1299_rreg(handle, ADS_CH1SET + ch, &reg);
        if (ret != ESP_OK) return ret;
        ret = _ads1299_wreg(handle, ADS_CH1SET + ch, en ? (reg & 0x7F) : (reg | 0x80));
        if (ret != ESP_OK) return ret;
    }
    ads1299_rdatac(handle);
    return ret;
}

esp_err_t ads1299_set_ch(ads1299_handle_t* handle, uint8_t ch, bool en)
{
    if (0 > ch || ch > 7) return ESP_ERR_INVALID_ARG;

    uint8_t reg = 0;
    esp_err_t ret = ESP_OK;

    ads1299_sdatac(handle);
    ret = _ads1299_rreg(handle, ADS_CH1SET + ch, &reg);
    if (ret != ESP_OK) return ret;
    ret = _ads1299_wreg(handle, ADS_CH1SET + ch, en ? (reg & 0x7F) : (reg | 0x80));
    if (ret != ESP_OK) return ret;
    ads1299_rdatac(handle);
    return ret;
}

esp_err_t ads1299_get_ch(ads1299_handle_t* handle, uint8_t ch, uint8_t* ret_val)
{
    if (ch < 0 || ch > 7) return ESP_ERR_INVALID_ARG;
        
    esp_err_t ret = ESP_OK;

    ads1299_sdatac(handle);
    ret = _ads1299_rreg(handle, ADS_CH1SET + ch, ret_val);
    ads1299_rdatac(handle);
    return ret;
}

esp_err_t ads1299_set_ch_input(ads1299_handle_t* handle, uint8_t ch, ads1299_ch_input_t data)
{
    if (ch < 0 || ch > 7) return ESP_ERR_INVALID_ARG;

    uint8_t reg = 0;
    esp_err_t ret = ESP_OK;

    ads1299_sdatac(handle);
    ret = _ads1299_rreg(handle, ADS_CH1SET + ch, &reg);
    ret = _ads1299_wreg(handle, ADS_CH1SET + ch, (reg & 0xF8) | data);
    ads1299_rdatac(handle);
    return ret;
}

esp_err_t ads1299_set_ch_gain(ads1299_handle_t* handle, uint8_t ch, ads1299_gain_t gain)
{
    if (ch < 0 || ch > 7) 
        return ESP_ERR_INVALID_ARG;

    uint8_t reg = 0;
    esp_err_t ret = ESP_OK;

    ads1299_sdatac(handle);
    ret = _ads1299_rreg(handle, ADS_CH1SET + ch, &reg);
    ret = _ads1299_wreg(handle, ADS_CH1SET + ch, (reg & 0x8F) | gain);
    ads1299_rdatac(handle);
    return ret;
}

esp_err_t ads1299_set_srb2_ch(ads1299_handle_t* handle, uint8_t ch, bool en) 
{
    if (ch < 0 || ch > 7) return ESP_ERR_INVALID_ARG;

    uint8_t reg = 0;
    esp_err_t ret = ESP_OK;

    ads1299_sdatac(handle);
    ret = _ads1299_rreg(handle, ADS_CH1SET + ch, &reg);
    ret = _ads1299_wreg(handle, ADS_CH1SET + ch, en ? (reg | 0x08) : (reg & (~0x08)));
    ads1299_rdatac(handle);
    return ret;

}

esp_err_t ads1299_set_datarate(ads1299_handle_t* handle, ads1299_data_rate_t dr)
{
    uint8_t reg = 0;
    esp_err_t ret = ESP_OK;

    ads1299_sdatac(handle);
    ret = _ads1299_rreg(handle, ADS_CONFIG1, &reg);
    ret = _ads1299_wreg(handle, ADS_CONFIG1, (reg & 0xF8) | dr);
    ads1299_rdatac(handle);
    return ret;
}

esp_err_t ads1299_get_datarate(ads1299_handle_t* handle, ads1299_data_rate_t* ret_val)
{
    uint8_t reg = 0;
    esp_err_t ret = ESP_OK;

    ads1299_sdatac(handle);
    ret = _ads1299_rreg(handle, ADS_CONFIG1, &reg);
    ads1299_rdatac(handle);

    // ESP_LOGI(TAG, "reg in hexadecimal: 0x%02X", reg);
    *ret_val = reg & 0x07;
    return ret;
}

esp_err_t ads1299_set_impedence_mode(ads1299_handle_t* handle, ads1299_loff_polarity_t loff)
{
    uint8_t reg = 0;
    esp_err_t ret = ESP_OK;

    ads1299_sdatac(handle);
    for (int ch = 0; ch < 8; ch++) {
        ret = _ads1299_rreg(handle, ADS_CH1SET + ch, &reg);
        // Set gain to 1 and ch input to normal
        ret = _ads1299_wreg(handle, ADS_CH1SET + ch, (reg & 0x88));

        if (ret != ESP_OK)
            break;
    }
    ret = _ads1299_wreg(handle, (loff ? ADS_LOFF_SENSN : ADS_LOFF_SENSP), 0xFF); // Enable all channels
    ads1299_rdatac(handle);
    return ret;
}

esp_err_t ads1299_reset_impedence_mode(ads1299_handle_t* handle, ads1299_loff_polarity_t loff)
{
    esp_err_t ret = ESP_OK;

    ads1299_sdatac(handle);
    _ads1299_wreg(handle, (loff ? ADS_LOFF_SENSN : ADS_LOFF_SENSP), 0x00); // Enable all channels
    ads1299_rdatac(handle);
    return ret;
}

esp_err_t ads1299_get_impedence(ads1299_handle_t* handle, ads1299_loff_polarity_t loff, uint8_t* ret_val)
{
    esp_err_t ret = ESP_OK;

    ads1299_sdatac(handle);
    ret = _ads1299_rreg(handle, (loff ? ADS_LOFF_STATN : ADS_LOFF_STATP), ret_val);
    ads1299_rdatac(handle);
    return ret;
}

esp_err_t ads1299_set_bias_all(ads1299_handle_t* handle, ads1299_bias_polarity_t bias, bool en)
{
    esp_err_t ret = ESP_OK;

    ads1299_sdatac(handle);
    ret = _ads1299_wreg(handle, bias ? ADS_BIAS_SENSN : ADS_BIAS_SENSP, en ? 0xFF : 0x00);
    if (ret != ESP_OK) return ret;
    ret = _ads1299_wreg(handle, ADS_CONFIG3, en ? 0xEC : 0xE0);
    if (ret != ESP_OK) return ret;
    ads1299_rdatac(handle);
    return ret;
}

esp_err_t ads1299_set_bias_ch(ads1299_handle_t* handle, uint8_t ch, ads1299_bias_polarity_t bias, bool en)
{
    if (ch < 0 || ch > 7) return ESP_ERR_INVALID_ARG;

    uint8_t reg = 0;
    esp_err_t ret = ESP_OK;

    ads1299_sdatac(handle);
    ret = _ads1299_wreg(handle, bias ? ADS_BIAS_SENSN : ADS_BIAS_SENSP, &reg);
    if (ret != ESP_OK) return ret; // TODO: remove this so rdatac is run properly
    ret = _ads1299_wreg(handle, bias ? ADS_BIAS_SENSN : ADS_BIAS_SENSP, en ? (reg | (1 << ch)) : (reg & ~(1 << ch)));
    if (ret != ESP_OK) return ret;
    ret = _ads1299_wreg(handle, ADS_CONFIG3, en ? 0xEC : 0xE0);
    if (ret != ESP_OK) return ret;
    ads1299_rdatac(handle);
    return ret;
}


esp_err_t _ads1299_rreg(ads1299_handle_t* handle, uint8_t addr, uint8_t* ret_val)
{
    uint16_t command = 0x0000;
    command = (ADS_RREG | addr) << 8;
    spi_transaction_ext_t t = {
        .base = (spi_transaction_t) {
            .flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
            .cmd = command,
            .length = 8,
            .tx_data = {0x00},
            .rx_data = {0x00}
        },
        .command_bits = 16,
        .address_bits = 0,
        .dummy_bits = 0
    };

    esp_err_t err = spi_device_transmit(handle->spi, &t);
    if (err != ESP_OK) {
        return err;
    }

    *ret_val = t.base.rx_data[0];
    return ESP_OK;
}

esp_err_t _ads1299_wreg(ads1299_handle_t* handle, uint8_t addr, uint8_t val)
{
    uint16_t command = 0x0000;
    command = (ADS_WREG | addr) << 8;
    spi_transaction_ext_t t = {
        .base = (spi_transaction_t) {
            .flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
            .cmd = command,
            .length = 8,
            .tx_data = {val},
            .rx_data = {0x00}
        },
        .command_bits = 16,
        .address_bits = 0,
        .dummy_bits = 0
    };

    esp_err_t err = spi_device_transmit(handle->spi, &t);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}