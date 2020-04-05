#define I2C_MASTER_TX_BUF_DISABLE                  0                       
#define I2C_MASTER_RX_BUF_DISABLE                  0                       
#define WRITE_BIT                   I2C_MASTER_WRITE          
#define READ_BIT                     I2C_MASTER_READ          
#define ACK_CHECK_EN                             0x1                 
#define ACK_CHECK_DIS                            0x0               
#define ACK_VAL                                  0x0                    
#define NACK_VAL                                 0x1                  
#define LAST_NACK_VAL                            0x2  

static esp_err_t i2c_init()
{   i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = i2c_gpio_sda,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = i2c_gpio_scl,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = i2c_frequency
    };
    return i2c_param_config(i2c_port, &conf);
}

static int i2cdetect()
{   i2c_init();
    i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, 
                       I2C_MASTER_TX_BUF_DISABLE, 0);
    uint8_t address;
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            address = i + j;
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (address << 1) | WRITE_BIT, ACK_CHECK_EN);
            i2c_master_stop(cmd);
            esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 50 / portTICK_RATE_MS);
            i2c_cmd_link_delete(cmd);
            if (ret == ESP_OK) { printf("%02x ", address);
            } else if (ret == ESP_ERR_TIMEOUT) { printf("UU ");
            } else { printf("-- "); }
        }
        printf("\r\n");
    }
    printf("\r\n\n");
    i2c_driver_delete(i2c_port);
    return 0;
}

static int i2c_read( uint8_t chip_addr, uint8_t data_addr, uint8_t *data_rd, size_t len)
{ 
    i2c_init();
    vTaskDelay(1);
    i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, 
                       I2C_MASTER_TX_BUF_DISABLE, 0);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, chip_addr << 1 | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, data_addr, ACK_CHECK_EN);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, chip_addr << 1 | READ_BIT, ACK_CHECK_EN);
    if (len > 1) {
        i2c_master_read(cmd, data_rd, len - 1, ACK_VAL);
    }
    i2c_master_read_byte(cmd, data_rd +len - 1, NACK_VAL);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 100 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK) { 
         //for(int i=0; i<len; i++){ printf ("%d %x\n", i, data_rd[i]); }
    }
    else if (ret == ESP_ERR_TIMEOUT) { ESP_LOGW("", "Bus is busy");}
    else { ESP_LOGW("", "Read failed"); }
    i2c_driver_delete(i2c_port);
    vTaskDelay(2);
    return 0;
}
 
static int i2c_write_block(int chip_addr, int data_addr, uint8_t *wr_data, int len)
{
    i2c_init();
    i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, 
                       I2C_MASTER_TX_BUF_DISABLE, 0);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, chip_addr << 1 | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, data_addr, ACK_CHECK_EN);
    for (int i = 0; i < len; i++){
       i2c_master_write_byte(cmd, wr_data[i], ACK_CHECK_EN);
    }
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 100 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK) { //ESP_LOGI("", "Write OK addr %x  datablock\n", chip_addr);
    } else if (ret == ESP_ERR_TIMEOUT) { ESP_LOGW("", "Bus is busy");
    } else { ESP_LOGW("", "Write Failed"); }
    i2c_driver_delete(i2c_port);
    return 0;
}
 
static int i2c_write(int chip_addr, int data_addr, int wr_data)
{
    i2c_init();
    i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, 
                       I2C_MASTER_TX_BUF_DISABLE, 0);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, chip_addr << 1 | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, data_addr, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, wr_data, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 100 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK) { //ESP_LOGI("", "Write OK addr %x  data %x\n", chip_addr, wr_data);
    } else if (ret == ESP_ERR_TIMEOUT) { ESP_LOGW("", "Bus is busy");
    } else { ESP_LOGW("", "Write Failed"); }
    i2c_driver_delete(i2c_port);
    return 0;
}
