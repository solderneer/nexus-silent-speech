#include <stdio.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/time.h>

// ESP includes
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"
#include "esp_sntp.h"
#include "driver/i2c_master.h"

// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

// Custom components
#include "ads1299_interface.h"
#include "adg715_interface.h"
#include "status_interface.h"

// #define BASE_WIFI_SSID "BT-RSC2QS"
// #define BASE_WIFI_PASS "tVDHXba7t9GeK4"

#define BASE_WIFI_SSID "Shan's Room"
#define BASE_WIFI_PASS "iamironman"

#define BASE_WIFI_MAXIMUM_RETRY 5

static const char *TAG = "base-board-fw";

/******** WIFI CONFIG **********/
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

// Retry number for WiFi connection
static int s_retry_num = 0;

/********* TCP/IP CONFIG ********/
#define HOST_IP_ADDR "192.168.1.143"
#define HOST_IP_PORT 8080

int sock = -1;
int addr_family = 0;
int ip_protocol = 0;

/********* ADS1299 INTERFACE PINS *******/

#define ADS1299_SPI_HOST             SPI2_HOST
#define ADS1299_SPI_CLOCK_SPEED_HZ   (2*1000*1000)
#define ADS1299_MISO_PIN             GPIO_NUM_8
#define ADS1299_MOSI_PIN             GPIO_NUM_12
#define ADS1299_SCLK_PIN             GPIO_NUM_9
#define ADS1299_CS_PIN               GPIO_NUM_10
#define ADS1299_DRDY_PIN             GPIO_NUM_18
#define ADS1299_RESET_PIN            GPIO_NUM_11

/********* MASTER I2C and ADG715 **********/

#define BOARD_I2C_PORT -1
#define BOARD_I2C_SDA_PIN GPIO_NUM_21
#define BOARD_I2C_SCL_PIN GPIO_NUM_47
#define ADG715_RESET_PIN GPIO_NUM_16

const uint8_t adg715_addr[4] = {0x48, 0x49, 0x4A, 0x4B};

/********* STATUS LED CONFIG *********/

#define STATUS_LED_GPIO GPIO_NUM_48

/********* COMM BUFFER ***********/

#define BUFFER_SIZE (256 * 16) // TODO: Optimize this
#define BUFFER_FLUSH_SIZE (256 * 15)
static char buffer[BUFFER_SIZE];
static int buffer_index = 0;

// System state machine
enum base_state_t
{
    WIFI_DISCONNECTED,
    WIFI_CONNECTING,
    SERVER_CONNECTING,
    STREAMING
};
static enum base_state_t base_state = WIFI_CONNECTING;

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < BASE_WIFI_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = BASE_WIFI_SSID,
            .password = BASE_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK},
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}

void app_main(void)
{
    /********* SETUP CODE **********/

    // Setup NVS Flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);   

    // Setup time
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // Setup status
    status_config_t status_config = {
        .led_pin = STATUS_LED_GPIO
    };
    status_handle_t* status_handle;
    status_init(&status_config, &status_handle);
    status_red(status_handle);

    // Setup I2C master bus
    ESP_LOGI(TAG, "Initializing I2C Bus...");
    i2c_master_bus_config_t i2c_master_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA_PIN,
        .scl_io_num = BOARD_I2C_SCL_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false
    };

    i2c_master_bus_handle_t i2c_bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_master_config, &i2c_bus_handle));

    /* Setup 4 ADG715s */
    adg715_handle_t* adg715_handle[4];
    for(int i = 0; i < sizeof(adg715_addr); i++) {
        adg715_config_t adg715_config = {
            .i2c_bus = i2c_bus_handle,
            .i2c_addr = adg715_addr[i],
            .reset_pin = ADG715_RESET_PIN
        };
        adg715_init(&adg715_config, &adg715_handle[i]);
        adg715_set(adg715_handle[i],0x00);
    }

    //adg715_set(adg715_handle[0], 0x81); // U1 (MSB S8, LSB S1) (CH3, CH2)
    //adg715_set(adg715_handle[1], 0x28); // U2 (CH7, CH6)
    //adg715_set(adg715_handle[2], 0x88); // U3 (CH4, CH1)
    //adg715_set(adg715_handle[3], 0x88); // U4 (CH8, CH5)

    // Setup SPI2 master bus
    ESP_LOGI(TAG, "Initializing SPI%d bus...", ADS1299_SPI_HOST + 1);
    spi_bus_config_t buscfg = {
        .miso_io_num = ADS1299_MISO_PIN,
        .mosi_io_num = ADS1299_MOSI_PIN,
        .sclk_io_num = ADS1299_SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(ADS1299_SPI_HOST, &buscfg, SPI_DMA_DISABLED));

    // Setup ADS1299
    ads1299_config_t ads1299_config = {
        .spi_host = ADS1299_SPI_HOST,
        .spi_clock_speed_hz = ADS1299_SPI_CLOCK_SPEED_HZ,
        .miso_pin = ADS1299_MISO_PIN,
        .mosi_pin = ADS1299_MOSI_PIN,
        .sclk_pin = ADS1299_SCLK_PIN,
        .cs_pin = ADS1299_CS_PIN,
        .drdy_pin = ADS1299_DRDY_PIN,
        .reset_pin = ADS1299_RESET_PIN
    };

    ads1299_handle_t* ads1299_handle;
    ads1299_init(&ads1299_config, &ads1299_handle);
    ads1299_set_datarate(ads1299_handle, DR_250SPS);
    
    //ads1299_set_ch_all(ads1299_handle, false);
    //ads1299_set_ch(ads1299_handle, 1, true);
    //ads1299_set_srb2_ch(ads1299_handle, 1, true);
    //ads1299_set_bias_ch(ads1299_handle, 1, BIAS_N, true);
    //ads1299_set_bias_ch(ads1299_handle, 1, BIAS_P, true);

    ads1299_data_rate_t dr = 0;
    ads1299_get_datarate(ads1299_handle, &dr);
    ESP_LOGI(TAG, "Data rate: %d", dr);

    // print all channels state
    for(int i = 0; i < 8; i++) {
        uint8_t reg;

        // ads1299_set_ch_input(ads1299_handle, i, TESTSIG);
        ads1299_set_srb2_ch(ads1299_handle, i, true);
        ads1299_set_bias_ch(ads1299_handle, i, BIAS_N, true);

        ads1299_get_ch(ads1299_handle, i, &reg);
        ESP_LOGI(TAG, "CH%d Setting: %x", i, reg);
    }
    
    // TODO: Move this into the ADS1299 component library to be gain dependent
    const double LSB = 2.5 / 24 / (16777216 - 1); // LSB = VREF / Gain / (2^24 - 1)

    /********* STATE MACHINE *******/
    while (1)
    {
        switch (base_state)
        {
        case WIFI_DISCONNECTED:
            // ESP_LOGI(TAG, "Disconnected from Wifi");
            status_red(status_handle);
            break;
        case WIFI_CONNECTING:
            ESP_LOGI(TAG, "Connecting to Wifi");
            status_red(status_handle);

            wifi_init_sta();

            /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection 
             * failed for the maximum number of re-tries (WIFI_FAIL_BIT). The bits are set by 
             * event_handler() (see above) */
            EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

            if (bits & WIFI_CONNECTED_BIT)
                base_state = SERVER_CONNECTING;
            else if (bits & WIFI_FAIL_BIT)
                base_state = WIFI_DISCONNECTED;
            else
            {
                ESP_LOGE(TAG, "UNEXPECTED EVENT");
                base_state = WIFI_DISCONNECTED;
            }
            break;
        case SERVER_CONNECTING:
            ESP_LOGI(TAG, "[SERVER_DISCONNECTED] Connected to ap SSID:%s password:%s", 
                BASE_WIFI_SSID, BASE_WIFI_PASS);
            ESP_LOGI(TAG, "[SERVER_DISCONNECTED] Looking for server.");
            status_yellow(status_handle);

            // Setup time
            ESP_LOGI(TAG, "[SERVER_DISCONNECTED] Initializing SNTP");
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
            esp_sntp_init();

            // wait for time to be set
            int retry = 0;
            const int retry_count = 10;
            while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
                ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
                vTaskDelay(2000 / portTICK_PERIOD_MS);
            }
            
            // Setting up UDP socket
            struct sockaddr_in dest_addr;
            dest_addr.sin_addr.s_addr = inet_addr(HOST_IP_ADDR);
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(HOST_IP_PORT);
            addr_family = AF_INET;
            ip_protocol = IPPROTO_IP;

            sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
            if (sock < 0) {
                ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                break;
            }

            // Set timeout
            struct timeval timeout;
            timeout.tv_sec = 10;
            timeout.tv_usec = 0;
            setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

            ESP_LOGI(TAG, "Socket created, sending to %s:%d", HOST_IP_ADDR, HOST_IP_PORT);
            base_state = STREAMING;
            break;
        case STREAMING:
            ESP_LOGI(TAG, "[STREAMING] Streaming data.");
            status_green(status_handle);
            ads1299_acquire_bus(ads1299_handle);

            while (1) {
                if (ads1299_ready(ads1299_handle)) {
                    uint32_t status;
                    int32_t res[8];
                    double res_V[8];

                    ads1299_read(ads1299_handle, &status, res);
                    time(&now);

                    // Convert to uV
                    for(int i = 0; i < 8; i++) {
                        res_V[i] = res[i] * LSB;
                    }
                    
                    char temp_buffer[256] = {0};
                    sprintf(temp_buffer, "%lld,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf\n", now, res_V[0], res_V[1], res_V[2], res_V[3], res_V[4], res_V[5], res_V[6], res_V[7]);
                    memcpy(&buffer[buffer_index], temp_buffer, 256);
        
                    if (buffer_index + 256 <= BUFFER_FLUSH_SIZE) {
                        buffer_index += 256;
                    } else {
                        // Flush to network
                        int err = sendto(sock, buffer, buffer_index, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
                        buffer_index = 0;

                        if (err < 0) {
                            ESP_LOGE(TAG, "Connection lost: errno %d", errno);
                            break;
                        }               
                    }
                } else {
                    // Reset watchdog
                }
            }

            // Error with UDP, go back to SERVER_CONNECTING
            shutdown(sock, 0);
            close(sock);
            ads1299_release_bus(ads1299_handle);
            base_state = SERVER_CONNECTING;
            break;
        }
    }
}
