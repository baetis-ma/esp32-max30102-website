//sets up system and i2c interface
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "driver/i2c.h"
#include "sdkconfig.h"

#define CPU_FREQ                   240

#define I2C_MASTER_TX_BUF_DISABLE    0                       
#define I2C_MASTER_RX_BUF_DISABLE    0                       
#define WRITE_BIT     I2C_MASTER_WRITE          
#define READ_BIT       I2C_MASTER_READ          
#define ACK_CHECK_EN               0x1                 
#define ACK_CHECK_DIS              0x0               
#define ACK_VAL                    0x0                    
#define NACK_VAL                   0x1                  
#define LAST_NACK_VAL              0x2  

uint32_t IRAM_ATTR cycles(int reset)
{
    uint32_t ccount;
    uint32_t ccount_stamp;
    __asm__ __volatile__ ( "rsr     %0, ccount" : "=a" (ccount) );

    if (reset == 1){ ccount_stamp = ccount; ccount = 0; } 
    else { ccount = ccount - ccount_stamp; } 
    ccount = ccount / CPU_FREQ;
    printf(" timer =  %10.3f ms\n", (float) ccount/1000);

    return ccount; 
}

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;
const int IPV4_GOTIP_BIT = BIT0;
static const char *TAG = "webpage ";
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
        break;
    case SYSTEM_EVENT_STA_CONNECTED:
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, IPV4_GOTIP_BIT);
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, IPV4_GOTIP_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static esp_err_t i2c_read(i2c_port_t i2c_num, uint8_t addr_rd, uint8_t *data_rd, size_t size)
{
    esp_err_t ret;
    if (size == 0) { return ESP_OK; }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (I2C_ADDR << 1) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, addr_rd, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) { return ret; }
    vTaskDelay(1);
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, I2C_ADDR << 1 | READ_BIT, ACK_CHECK_EN);
    i2c_master_read(cmd, data_rd, size, LAST_NACK_VAL);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_master_init()
{
    int i2c_master_port = I2C_MASTER_NUM;
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    i2c_param_config(i2c_master_port, &conf);
    return i2c_driver_install(i2c_master_port, conf.mode,
               I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}

static esp_err_t i2c_write(i2c_port_t i2c_num, uint8_t addr_wr, uint8_t *data_wr, size_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (I2C_ADDR << 1) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, addr_wr,  ACK_CHECK_EN);
    i2c_master_write(cmd, data_wr, size, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

