
#pragma once
#include "driver/spi_master.h"
#include "driver/gpio.h"

/// Configuration of ADS1299 interface
typedef struct {
    spi_host_device_t spi_host; ///< SPI host to use
    int spi_clock_speed_hz;     ///< SPI clock speed in Hz
    gpio_num_t miso_pin;        ///< SPI MISO pin
    gpio_num_t mosi_pin;        ///< SPI MOSI pin
    gpio_num_t sclk_pin;        ///< SPI SCLK pin
    gpio_num_t cs_pin;          ///< SPI CS pin
    gpio_num_t drdy_pin;        ///< ADS1299 DRDY pin
    gpio_num_t reset_pin;       ///< ADS1299 reset pin
} ads1299_config_t;

typedef struct {
    ads1299_config_t config;  ///< User passed configuration of ADS1299 interface
    spi_device_handle_t spi;  ///< SPI device handle
    uint8_t id;
} ads1299_handle_t;

typedef enum {GAIN_1, GAIN_2, GAIN_4, GAIN_6, GAIN_8, GAIN_12, GAIN_24} ads1299_gain_t;
typedef enum { NORMAL, SHORTED, BIAS_MEAS, MVDD, TEMP, TESTSIG, BIAS_DRP, BIAS_DRN } ads1299_ch_input_t;
typedef enum { DR_16KSPS, DR_8KSPS, DR_4KSPS, DR_2KSPS, DR_1KSPS, DR_500SPS, DR_250SPS } ads1299_data_rate_t;
typedef enum { LOFF_P, LOFF_N } ads1299_loff_polarity_t;
typedef enum { BIAS_P, BIAS_N } ads1299_bias_polarity_t;

/******* PUBLIC FUNCTIONS *********/
esp_err_t ads1299_init(const ads1299_config_t* config, ads1299_handle_t** out_handle);
esp_err_t ads1299_deinit(ads1299_handle_t* handle);

// Check DRDY
int ads1299_ready(ads1299_handle_t* handle);
esp_err_t ads1299_read(ads1299_handle_t* handle, uint32_t* status, int32_t res[]);
esp_err_t ads1299_acquire_bus(ads1299_handle_t* handle);
esp_err_t ads1299_release_bus(ads1299_handle_t* handle);

esp_err_t ads1299_cmd(ads1299_handle_t* handle, uint8_t cmd);
esp_err_t ads1299_wakeup(ads1299_handle_t* handle);
esp_err_t ads1299_standby(ads1299_handle_t* handle);
esp_err_t ads1299_start(ads1299_handle_t* handle);
esp_err_t ads1299_stop(ads1299_handle_t* handle);
esp_err_t ads1299_rdatac(ads1299_handle_t* handle);
esp_err_t ads1299_sdatac(ads1299_handle_t* handle);
esp_err_t ads1299_reset(ads1299_handle_t* handle);

esp_err_t ads1299_set_ch_all(ads1299_handle_t* handle, bool en);
esp_err_t ads1299_set_ch(ads1299_handle_t* handle, uint8_t ch, bool en);
esp_err_t ads1299_get_ch(ads1299_handle_t* handle, uint8_t ch, uint8_t* ret_val);
esp_err_t ads1299_set_ch_input(ads1299_handle_t* handle, uint8_t ch, ads1299_ch_input_t data);
esp_err_t ads1299_set_ch_gain(ads1299_handle_t* handle, uint8_t ch, ads1299_gain_t gain);
esp_err_t ads1299_set_datarate(ads1299_handle_t* handle, ads1299_data_rate_t dr);
esp_err_t ads1299_get_datarate(ads1299_handle_t* handle, ads1299_data_rate_t* ret_val);
esp_err_t ads1299_set_impedence_mode(ads1299_handle_t* handle, ads1299_loff_polarity_t loff);
esp_err_t ads1299_reset_impedence_mode(ads1299_handle_t* handle, ads1299_loff_polarity_t loff);
esp_err_t ads1299_get_impedence(ads1299_handle_t* handle, ads1299_loff_polarity_t loff, uint8_t* ret_val);
esp_err_t ads1299_set_bias_all(ads1299_handle_t* handle, ads1299_bias_polarity_t bias, bool en);
esp_err_t ads1299_set_bias_ch(ads1299_handle_t* handle, uint8_t ch, ads1299_bias_polarity_t bias, bool en);
esp_err_t ads1299_set_srb2_ch(ads1299_handle_t* handle, uint8_t ch, bool en);