#include <string.h>
#include <stdio.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "driver/i2c.h"
#include "sdkconfig.h"

#define I2C_ADDR_MAX30102      0x57 //max30102 i2c address
#define i2c_port                  0
#define i2c_frequency       800000
#define i2c_gpio_sda             18
#define i2c_gpio_scl             19

#include "./interfaces/i2c.c"

#define I2C_ADDR_SSD1306       0x3c
#include "./devices/ssd1306.c"

#define EXAMPLE_WIFI_SSID "troutstream"
#define EXAMPLE_WIFI_PASS "password"
#define PORT 80
#include "./interfaces/wifisetup.c"

//globals
char outStr[1500];
float meastime;
int countedsamples = 0;
int irpower = 0, rpower = 0, lirpower = 0, lrpower = 0;
int startstop = 0, raworbp = 0;
float heartrate=99.2, pctspo2=99.2;   
//long avgR, avgIR;
//int adc_read_ptr = 0;
//long adc_read[600];
//int hrstart, hrinterval;
//float redsumsq, redtemp=0, irsumsq, irtemp=0;
//float lastred = 0;

static void tcp_server_task(void *pvParameters)
{
    char rx_buffer[1500];
    char addr_str[16];
    //static float rxv[5], ryv[5], irxv[5], iryv[5];
    int addr_family;
    int ip_protocol;
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
            if (sock < 0) { ESP_LOGE("", "Unable to accept connection: errno %d", errno); break; }
            //ESP_LOGI("", "Socket accepted");
            int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
            // Error occured during receiving
            if (len <= 0) { ESP_LOGE("", "recv failed: errno %d", errno); break; }
            else {
                //prints client source ip and http request packet to esp monitor
                inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, 
                              addr_str, sizeof(addr_str) - 1);
                //ESP_LOGI("---  ", "Received %d bytes from %s:", len, addr_str);
                //ESP_LOGI("", "HTTP REQUEST Packet\n%s", rx_buffer);
                //parse request
                char http_type[8];
                char temp_str[16];
                sscanf(rx_buffer, "%s/", http_type);
                //ESP_LOGI("", "request type %s  ", http_type);
                temp = strstr(rx_buffer, "?");    
                if(temp){
                   memcpy(temp_str, rx_buffer+strlen(http_type)+2, 
                                    strlen(rx_buffer)-strlen(temp));
                   temp_str[strlen(temp_str)] = '\0';
                } else {
                   sscanf(rx_buffer+strlen(http_type)+2, "%s", temp_str);
                }
                //return html page to socket
                if (strcmp("index.html", temp_str) ==0 || strcmp("HTTP/1.1",temp_str) ==0 ){
                    extern const char index_html_start[] asm("_binary_index_html_start");
                    extern const char index_html_end[] asm("_binary_index_html_end");
                    int pkt_buf_size = 1500;
                    int pkt_end = pkt_buf_size;
                    int html_len =  strlen(index_html_start) - strlen(index_html_end);
                    for( int pkt_ptr = 0; pkt_ptr < html_len; pkt_ptr = pkt_ptr + pkt_buf_size){
                        if ((html_len - pkt_ptr) < pkt_buf_size) pkt_end = html_len - pkt_ptr;
                            //ESP_LOGI("", "pkt_ptr %d pkt_end %d", pkt_ptr,pkt_end );
                            send(sock, index_html_start + pkt_ptr, pkt_end, 0);
                        }
                } else 
                //parse datagram, rcv data from webpage and return collected data
                if (strcmp("GET", http_type) ==0 || strcmp("getData",temp_str) ==0 ){
                    temp = strstr(rx_buffer, "irpower=");  if(temp)sscanf(temp,"irpower=%d", &irpower); 
                    temp = strstr(rx_buffer, "xrpower=");  if(temp)sscanf(temp,"xrpower=%d", &rpower); 
                    temp = strstr(rx_buffer, "raworbp=");  if(temp)sscanf(temp,"raworbp=%d", &raworbp); 
                    temp = strstr(rx_buffer, "startstop=");if(temp)sscanf(temp,"startstop=%d", &startstop); 
                    //printf ("ir=%d r=%d ss=%d braw=%d\n", irpower,rpower,startstop,raworbp);
            
                    //int adc_read_ptr_shadow = adc_read_ptr;
                    //adc_read_ptr = 0;
                    
                    char outstr[1500];
          /*
                    snprintf( outstr, sizeof outstr, "%d,",adc_read_ptr_shadow / 3);
                    snprintf(tmp, sizeof tmp, "%ld,", adc_read[0]); strcat (outstr, tmp);  
                    snprintf(tmp, sizeof tmp, "%ld,", avgIR); strcat (outstr, tmp);  
                    snprintf(tmp, sizeof tmp, "%ld,", avgR); strcat (outstr, tmp);  
                    avgR = 0; avgIR = 0;
                    //filter is 2nd order butterworth 0.5/25 Hz
                    for( x = 0; x < adc_read_ptr_shadow; x++){
                       if(x%3 == 1){
                           avgR = avgR + adc_read[x];
                           rxv[0] = rxv[1]; rxv[1] = rxv[2]; rxv[2] = rxv[3]; rxv[3] = rxv[4]; 
                           rxv[4] = ((float) adc_read[x]) / 3.48311;
                           ryv[0] = ryv[1]; ryv[1] = ryv[2]; ryv[2] = ryv[3]; ryv[3] = ryv[4]; 
                           ryv[4] = (rxv[0] + rxv[4]) - 2 * rxv[2]
                                  + ( -0.1718123813 * ryv[0]) + (  0.3686645260 * ryv[1])
                                  + ( -1.1718123813 * ryv[2]) + (  1.9738037992 * ryv[3]);
                           if(raworbp==1)snprintf(tmp, sizeof tmp, "%5.1f,", rxv[4]);
                              else snprintf(tmp, sizeof tmp, "%5.1f,", -1.0 * ryv[4]);
                           strcat (outstr, tmp);

             } 
                       if(x%3 == 2){
                           avgIR = avgIR + adc_read[x];
                           irxv[0] = irxv[1]; irxv[1] = irxv[2]; irxv[2] = irxv[3]; irxv[3] = irxv[4]; 
                           irxv[4] = ((float) adc_read[x]) / 3.48311;
                           iryv[0] = iryv[1]; iryv[1] = iryv[2]; iryv[2] = iryv[3]; iryv[3] = iryv[4]; 
                           iryv[4] = (irxv[0] + irxv[4]) - 2 * irxv[2]
                                   + ( -0.1718123813 * iryv[0]) + (  0.3686645260 * iryv[1])
                                   + ( -1.1718123813 * iryv[2]) + (  1.9738037992 * iryv[3]);
                           if(raworbp==1)snprintf(tmp, sizeof tmp, "%5.4f,", irxv[4]);
                              else   snprintf(tmp, sizeof tmp, "%5.4f,", -1.0 * iryv[4]);
                           strcat (outstr, tmp); } 
                    }
                    strcat (outstr, "0\0");  
                    //ESP_LOGI("","sizeof=%d  %s",strlen(outstr), outstr);
                    //printf("%s\n",outstr);
                    send(sock, outstr, sizeof outstr, 0);
                    */

                    snprintf(outstr, sizeof outstr, "%2d,%2d,%4.1f,%4.1f,", countedsamples, (int)(100*meastime), heartrate, pctspo2); 
                    strcat (outstr, outStr);  //header and data for outstr

                    send(sock, outstr, sizeof outstr, 0);  //send outstr for tcp responce 

                    //printf("%s\n", outstr);
                    strcpy (outStr, "\0");  //reset outStr  
                    countedsamples = 0;


                //resource not found 
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

void max30102_init() {
    uint8_t data;
    data = ( 0x2 << 5);  //sample averaging 0=1,1=2,2=4,3=8,4=16,5+=32
    i2c_write(I2C_ADDR_MAX30102, 0x08, data);
    data = 0x03;                //mode = red and ir samples
    i2c_write(I2C_ADDR_MAX30102, 0x09, data);
    data = ( 0x3 << 5) + ( 0x3 << 2 ) + 0x3; //first and last 0x3, middle smap rate 0=50,1=100,etc 
    i2c_write(I2C_ADDR_MAX30102, 0x0a, data);
    data = 0xd0;                //ir pulse power
    i2c_write(I2C_ADDR_MAX30102, 0x0c, data);
    data = 0xa0;                //red pulse power
    i2c_write(I2C_ADDR_MAX30102, 0x0d, data);
}

void max30102_task () {
    int cnt, samp, tcnt = 0, cntr = 0;
    uint8_t rptr, wptr;
    uint8_t data;
    uint8_t regdata[256];
    //int irmeas, redmeas;
    float firxv[5], firyv[5], fredxv[5], fredyv[5];
    float lastmeastime = 0;
    float hrarray[10],spo2array[10];
    int hrarraycnt = 0;
    while(1){
        if(lirpower!=irpower){
            data = (uint8_t) irpower;
            i2c_write(I2C_ADDR_MAX30102, 0x0d,  data); 
            lirpower=irpower;
        }
        if(lrpower!=rpower){
            data = (uint8_t) rpower;
            i2c_write(I2C_ADDR_MAX30102, 0x0c,  data); 
            lrpower=rpower;
        }
        i2c_read(I2C_ADDR_MAX30102, 0x04, &wptr, 1);
        i2c_read(I2C_ADDR_MAX30102, 0x06, &rptr, 1);
        
        samp = ((32+wptr)-rptr)%32;
        i2c_read(I2C_ADDR_MAX30102, 0x07, regdata, 6*samp);
        //ESP_LOGI("samp","----  %d %d %d %d",  adc_read_ptr,samp,wptr,rptr);

        for(cnt = 0; cnt < samp; cnt++){
            meastime =  0.01 * tcnt++;
            firxv[0] = firxv[1]; firxv[1] = firxv[2]; firxv[2] = firxv[3]; firxv[3] = firxv[4];
            firxv[4] = (1/3.48311) * (256*256*(regdata[6*cnt+3]%4)+ 256*regdata[6*cnt+4]+regdata[6*cnt+5]);
            firyv[0] = firyv[1]; firyv[1] = firyv[2]; firyv[2] = firyv[3]; firyv[3] = firyv[4];
            firyv[4] = (firxv[0] + firxv[4]) - 2 * firxv[2]
                    + ( -0.1718123813 * firyv[0]) + (  0.3686645260 * firyv[1])
                    + ( -1.1718123813 * firyv[2]) + (  1.9738037992 * firyv[3]);

            fredxv[0] = fredxv[1]; fredxv[1] = fredxv[2]; fredxv[2] = fredxv[3]; fredxv[3] = fredxv[4];
            fredxv[4] = (1/3.48311) * (256*256*(regdata[6*cnt+0]%4)+ 256*regdata[6*cnt+1]+regdata[6*cnt+2]);
            fredyv[0] = fredyv[1]; fredyv[1] = fredyv[2]; fredyv[2] = fredyv[3]; fredyv[3] = fredyv[4];
            fredyv[4] = (fredxv[0] + fredxv[4]) - 2 * fredxv[2]
                    + ( -0.1718123813 * fredyv[0]) + (  0.3686645260 * fredyv[1])
                    + ( -1.1718123813 * fredyv[2]) + (  1.9738037992 * fredyv[3]);

            //if (-1.0 * firyv[4] >= 100 && -1.0 * firyv[3] < 100){
            //   heartrate = 60 / (meastime - lastmeastime);
            //   pctspo2 = 110 - 25 * ((fredyv[4]/fredxv[4]) / (firyv[4]/firxv[4]));
            //   printf ("%6.2f  %4.2f     hr= %5.1f     spo2= %5.1f\n", meastime, meastime - lastmeastime, heartrate, pctspo2);
            //   lastmeastime = meastime;
            //}

            //printf("%8.1f %8.1f %8.1f %8.3f\n", -1.0 * firyv[0], -1.0 * firyv[2], -1.0 * firyv[4], meastime-lastmeastime); 
            if (-1.0 * firyv[4] >= 100 && -1.0 * firyv[2] > -1*firyv[0] && -1.0 * firyv[2] > -1*firyv[4] && meastime-lastmeastime > 0.5){
               hrarray[hrarraycnt%5] = 60 / (meastime - lastmeastime);
               spo2array[hrarraycnt%5] = 110 - 25 * ((fredyv[4]/fredxv[4]) / (firyv[4]/firxv[4]));
               if(spo2array[hrarraycnt%5]>100)spo2array[hrarraycnt%5]=99.9;
               printf ("%6.2f  %4.2f     hr= %5.1f     spo2= %5.1f\n", meastime, meastime - lastmeastime, heartrate, pctspo2);
               lastmeastime = meastime;
	       hrarraycnt++;
	       heartrate = (hrarray[0]+hrarray[1]+hrarray[2]+hrarray[3]+hrarray[4]) / 5;
	       if (heartrate < 40 || heartrate > 150) heartrate = 0;
	       pctspo2 = (spo2array[0]+spo2array[1]+spo2array[2]+spo2array[3]+spo2array[4]) / 5;
	       if (pctspo2 < 50 || pctspo2 > 101) pctspo2 = 0;
            }
            
       char tmp[32];
       countedsamples++;
       if(countedsamples < 100){
            if(raworbp == 0){snprintf(tmp, sizeof tmp, "%5.1f,%5.1f,", -1* fredyv[4], -1* firyv[4] ); strcat (outStr, tmp); } 
                       else {snprintf(tmp, sizeof tmp, "%5.1f,%5.1f,", fredxv[4], firxv[4] ); strcat (outStr, tmp);}  
            }

       //if((tcnt%10)==0) printf("%8.3f %2d samp=%3d red= %8.3f %8.2f  ir= %8.3f %8.2f rate= %4.1f o2= %4.1f\n", 
            //   meastime, cntr, samp, fredxv[4], -1.0*fredyv[4], firxv[4], -1.0*firyv[4], heartrate, pctspo2);

            //if(adc_read_ptr>200)adc_read_ptr=0;
        }

   //print to ssd1306
   cntr++;
    char textstr[64];
   if((cntr % 10) == 0){
           sprintf( textstr, "4Heart Rate|||4 %4.1f bpm|| %3.1f SpO2", heartrate, pctspo2);   
           ssd1306_text ( textstr );   
           vTaskDelay(2);
   }
    }
}

void app_main()
{
    //configure esp32 memory, wifi and i2c 
    ESP_ERROR_CHECK( nvs_flash_init() );
    initialise_wifi(); 
    wait_for_ip();

    i2c_init();
    i2cdetect();

    ssd1306_init();
    ssd1306_blank ( 0xcc );
 
    //configure max30102 with i2c instructions
    max30102_init();

    //start tcp server and data collection tasks
    xTaskCreate(tcp_server_task, "tcp_server", 8192, NULL, 4, NULL);
    xTaskCreate(max30102_task, "max30102_task", 4096, NULL, 5, NULL);

}

