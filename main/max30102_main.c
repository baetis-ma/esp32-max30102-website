#include <string.h>
#include <stdio.h>
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

#define EXAMPLE_WIFI_SSID "troutstream"
#define EXAMPLE_WIFI_PASS "password"
#define PORT 80

#define I2C_MASTER_NUM               0
#define I2C_MASTER_SCL_IO           19
#define I2C_MASTER_SDA_IO           18
#define I2C_MASTER_FREQ_HZ      100000
#define I2C_MASTER_TX_BUF_DISABLE    0                       
#define I2C_MASTER_RX_BUF_DISABLE    0                       

#define WRITE_BIT     I2C_MASTER_WRITE          
#define READ_BIT       I2C_MASTER_READ          
#define I2C_ADDR                  0x57
#define ACK_CHECK_EN               0x1                 
#define ACK_CHECK_DIS              0x0               
#define ACK_VAL                    0x0                    
#define NACK_VAL                   0x1                  
#define LAST_NACK_VAL              0x2  
#define CPU_FREQ                   240

//globals
long avgR, avgIR;
long adc_read[600];
int adc_read_ptr = 0;
int irpower = 0, rpower = 0;
int lirpower = 0, lrpower = 0;
static float rxv[5], ryv[5], irxv[5], iryv[5];

uint32_t ccount_stamp;
uint32_t IRAM_ATTR cycles(int reset)
{
    uint32_t ccount;
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

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

static void wait_for_ip()
{
    uint32_t bits = IPV4_GOTIP_BIT;
    //ESP_LOGI(TAG, "Waiting for AP connection...");
    xEventGroupWaitBits(wifi_event_group, bits, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Connected to AP");
}

static void tcp_server_task(void *pvParameters)
{
    char rx_buffer[1500];
    char addr_str[16];
    int addr_family;
    int ip_protocol;
    int startstop = 0, raworbp = 0;
    char *temp;

    while (1) {

        struct sockaddr_in destAddr;
        destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);

        int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        bind(listen_sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
        listen(listen_sock, 1);

        struct sockaddr_in sourceAddr;
        uint addrLen = sizeof(sourceAddr);
        while (1) {
            int sock = accept(listen_sock, (struct sockaddr *)&sourceAddr, &addrLen);
            if (sock < 0) { ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno); break; }
            ESP_LOGI(TAG, "Socket accepted");

            int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string

            // Error occured during receiving
            if (len <= 0) { ESP_LOGE(TAG, "recv failed: errno %d", errno); break; }
            // Data received
            else {
                //prints client source ip and http request packet to esp monitor
                inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                //ESP_LOGI("---  ", "Received %d bytes from %s:", len, addr_str);
                //ESP_LOGI("", "HTTP REQUEST Packet\n%s", rx_buffer);

                //parse request
                char http_type[8];
                char temp_str[16];
                //for(int a=0; a<= strlen(rx_buffer); a++)if(rx_buffer[a]=='?'||rx_buffer[a]=='/')rx_buffer[a]=',';
                sscanf(rx_buffer, "%s/", http_type);
                //ESP_LOGI("", "request type %s  ", http_type);
                temp = strstr(rx_buffer, "?");    
                if(temp){
                   //ESP_LOGI("", "  %s %d %d ",  temp , strlen(temp), strlen(rx_buffer));
                   memcpy(temp_str, rx_buffer+strlen(http_type)+2, 
                                    strlen(rx_buffer)-strlen(temp));
                   temp_str[strlen(temp_str)] = '\0';
                   //ESP_LOGI("", " temp_str %s",  temp_str);
                } else {
                   sscanf(rx_buffer+strlen(http_type)+2, "%s", temp_str);
                }

                //ESP_LOGI("", "request type %s  %s  ", http_type, temp_str);

                //reads file directly from read only data space (saving ram buffer space)
                //and breaks up file to fit ip buffer size //i hate having an if case for each file name - fix
                if (strcmp("index.html", temp_str) ==0 || strcmp("HTTP/1.1",temp_str) ==0 ){
                    extern const char index_html_start[] asm("_binary_index_html_start");
                    extern const char index_html_end[] asm("_binary_index_html_end");
                    int pkt_buf_size = 1500;
                    int pkt_end = pkt_buf_size;
                    int html_len =  strlen(index_html_start) - strlen(index_html_end);
                    for( int pkt_ptr = 0; pkt_ptr < html_len; pkt_ptr = pkt_ptr + pkt_buf_size){
                        if ((html_len - pkt_ptr) < pkt_buf_size) pkt_end = html_len - pkt_ptr;
                            //ESP_LOGI(TAG, "pkt_ptr %d pkt_end %d", pkt_ptr,pkt_end );
                            send(sock, index_html_start + pkt_ptr, pkt_end, 0);
                        }
                } else 
                if (strcmp("GET", http_type) ==0 || strcmp("getData",temp_str) ==0 ){
                    //read values sent with POST
                    int x;
                    char tmp[40];
                    char outstr[1500];
                    temp = strstr(rx_buffer, "irpower=");    if(temp)sscanf(temp,"irpower=%d", &irpower); 
                    temp = strstr(rx_buffer, "xrpower=");    if(temp)sscanf(temp,"xrpower=%d", &rpower); 
                    temp = strstr(rx_buffer, "raworbp=");    if(temp)sscanf(temp,"raworbp=%d", &raworbp); 
                    temp = strstr(rx_buffer, "startstop=");    if(temp)sscanf(temp,"startstop=%d", &startstop); 
                    //printf ("ir=%d r=%d ss=%d braw=%d\n", irpower,rpower,startstop,raworbp);


            
                    int adc_read_ptr_shadow = adc_read_ptr;
                    adc_read_ptr = 0;
                    
                    snprintf( outstr, sizeof outstr, "%d,",adc_read_ptr_shadow / 3);
                    snprintf(tmp, sizeof tmp, "%ld,", adc_read[0]); strcat (outstr, tmp);  
                    snprintf(tmp, sizeof tmp, "%ld,", avgIR); strcat (outstr, tmp);  
                    snprintf(tmp, sizeof tmp, "%ld,", avgR); strcat (outstr, tmp);  
                    avgR = 0; avgIR = 0;
                    //filter is 2nf order butterworth 0.5/25 Hz
                    for( x = 0; x < adc_read_ptr_shadow; x++){
                        if(x%3 == 1){
                              rxv[0] = rxv[1]; rxv[1] = rxv[2]; rxv[2] = rxv[3]; rxv[3] = rxv[4]; 
                              rxv[4] = ((float) adc_read[x]) / 3.48311;
                              ryv[0] = ryv[1]; ryv[1] = ryv[2]; ryv[2] = ryv[3]; ryv[3] = ryv[4]; 
                              ryv[4] =   (rxv[0] + rxv[4]) - 2 * rxv[2]
                                           + ( -0.1718123813 * ryv[0]) + (  0.3686645260 * ryv[1])
                                           + ( -1.1718123813 * ryv[2]) + (  1.9738037992 * ryv[3]);
                              avgR = avgR + adc_read[x];
                              snprintf(tmp, sizeof tmp, "%5.1f,", -1.0 * ryv[4]);
                              strcat (outstr, tmp); } 
                        if(x%3 == 2){
                              irxv[0] = irxv[1]; irxv[1] = irxv[2]; irxv[2] = irxv[3]; irxv[3] = irxv[4]; 
                              irxv[4] = ((float) adc_read[x]) / 3.48311;
                              iryv[0] = iryv[1]; iryv[1] = iryv[2]; iryv[2] = iryv[3]; iryv[3] = iryv[4]; 
                              iryv[4] =   (irxv[0] + irxv[4]) - 2 * irxv[2]
                                           + ( -0.1718123813 * iryv[0]) + (  0.3686645260 * iryv[1])
                                           + ( -1.1718123813 * iryv[2]) + (  1.9738037992 * iryv[3]);
                              avgIR = avgIR + adc_read[x];
                              snprintf(tmp, sizeof tmp, "%5.4f,", -1.0 * iryv[4]);
                              strcat (outstr, tmp); } 
                    }
                    strcat (outstr, "0\0");  
                    //ESP_LOGI("","sizeof=%d  %s",strlen(outstr), outstr);
                    send(sock, outstr, sizeof outstr, 0);
                } else {
                    char temp404[] = "<html><h1>Not Found 404</html>";
                    send(sock, temp404, sizeof(temp404), 0);
                    //ESP_LOGI("", "404");
                }
                vTaskDelay(10/ portTICK_RATE_MS); //waits for buffer to purge
                shutdown(sock, 0);
                close(sock);
            }
        }
    }
    vTaskDelete(NULL);
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

void adc_task () {
    int cnt, samp, tcnt = 0;
    uint8_t rptr, wptr;
    uint8_t data[1];
    uint8_t regdata[256];
    while(1){
        if(lirpower!=irpower){
            data[0] = (uint8_t) irpower;
            i2c_write(I2C_MASTER_NUM, 0x0c,  data, 1); 
            lirpower=irpower;
        }
        if(lrpower!=rpower){
            data[0] = (uint8_t) rpower;
            i2c_write(I2C_MASTER_NUM, 0x0d,  data, 1); 
            lrpower=rpower;
        }

        i2c_read(I2C_MASTER_NUM, 0x04, &wptr, 1);
        i2c_read(I2C_MASTER_NUM, 0x06, &rptr, 1);
        samp = ((32+wptr)-rptr)%32;
        i2c_read(I2C_MASTER_NUM, 0x07, regdata, 6*samp);
        //ESP_LOGI("samp","----  %d %d ",  adc_read_ptr,samp);
        for(cnt = 0; cnt < samp; cnt++){
            adc_read[adc_read_ptr++] =  tcnt++;
            adc_read[adc_read_ptr++] = (256*256*(regdata[6*cnt+3]%4)+256*regdata[6*cnt+4]+regdata[6*cnt+5]);
            adc_read[adc_read_ptr++] = (256*256*(regdata[6*cnt+0]%4)+256*regdata[6*cnt+1]+regdata[6*cnt+2]);
            if(adc_read_ptr>200)adc_read_ptr=0;
            //printf(" 0x%06x  0x%06x\n", 256*256*(regdata[6*cnt+0]%4)+256*regdata[6*cnt+1]+regdata[6*cnt+2],
            //                            256*256*(regdata[6*cnt+3]%4)+256*regdata[6*cnt+4]+regdata[6*cnt+5]);
        }
        vTaskDelay(10);
    }
}

void app_main()
{
    uint8_t data[1];

    ESP_ERROR_CHECK( nvs_flash_init() );
    initialise_wifi();
    cycles(1);
    wait_for_ip();
    cycles(0);
    i2c_master_init();

    data[0] = ( 0x2 << 5);  //sample averaging 0=1,1=2,2=4,3=8,4=16,5+=32
    i2c_write(I2C_MASTER_NUM, 0x08, data, 1);
    cycles(1);
    data[0] = 0x03;                //mode = red and ir samples
    i2c_write(I2C_MASTER_NUM, 0x09, data, 1);
    cycles(0);
    data[0] = ( 0x3 << 5) + ( 0x3 << 2 ) + 0x3; //first and last 0x3, middle smap rate 0=50,1=100,etc 
    i2c_write(I2C_MASTER_NUM, 0x0a, data, 1);
    cycles(0);
    data[0] = 0xd0;                //ir pulse power
    i2c_write(I2C_MASTER_NUM, 0x0c, data, 1);
    cycles(0);
    data[0] = 0xa0;                //red pulse power
    i2c_write(I2C_MASTER_NUM, 0x0d, data, 1);
    cycles(0);

    xTaskCreate(tcp_server_task, "tcp_server", 8192, NULL, 4, NULL);
    xTaskCreate(adc_task, "adc_task", 4096, NULL, 5, NULL);
}
