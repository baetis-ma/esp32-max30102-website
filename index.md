## Heart Rate Monitor and Oximator
<img align="right" width="45%" height="350" src="hr.png"></img>
##### This project uses an ESP32S module hooked up to two modules on the i2c bus, a MAX30102 Pulse Oximeter and Heart-Rate Sensor and a SSD1306 OLED display. The oled display shows heart rate and sp02 concentration. The esp host a webpage that can be used to adjust the red and ir led power output, as well as display a graph of the two channels of reflected light returns with and without filtering applied.
##### The MAX30102 Pulse Oximeter and Heart-Rate Sensor has a red (660nm) and an infrared (880nm) leds providing pulsed light output at anout 100Hz. A photodetector and 18 bit analog to digital converter read the reflected output light during the peak light ooutput of each of the pulses. When a finger is placed on the chips optical window readings are made of the reflected light at both colors separately. 
##### As the heart beats the capularies near the surface of the skin expand with the pressure increase, the extra blood causes a larger light reflection which shows up as a ~1% ripple on the top of the total reflected light at the heart beat rate. A forth order butterworth bandpass (0.25/25Hz) filter is used to eliminate dc as well as 60Hz noise. It turns out that fully oxegenated blood relects light diffently at 660nm than it does 880nm, the ratio of the percent ripple of the two colors for some reason can be used to calculate the oxygen concentration of the blood.
##### It makes sense for the filtering to be done on the esp module so that the system can calulate pulse rate and spo2 concentration without being attached to a browser running javascript dsp filter calculations. An ESP32S was chosen for this project and the code will not work well with an esp8266.



This project collects data from a max30102 heart rate monitor device and presents a webpage to a connected browser displaying the real time data as well as the dsp derived Pulse Rate and Oxygen concentration results. Hardware Required :

ESP32-S module (~$5 ebay)
MAX30102 module (~$2 ebay)
Four wires - interdevice connections
3.3v
gnd
I2C sda on gpio19
I2C scl on gpio18
A computer running a browser and esp-idf tools
USB cable
This project uses Espressif IoT Developemnt Framework (esp-idf) to compile user application code, build the ESP code image and flash it into the ESP device. The setup is pretty easy and well documented on several websites (see esp-idf-webpage project for a walk through).

The ESP device establishes a wi-fi connection, then configures the max30102 to collect 100 samples per second of red and IR data. It then initiates two tasks:

adc_task() - a loop that

adjusts ir and red led power if changed on web page
i2c read the max30102 measurement data buffer pointers
determine if data is present
construct a read of available sample data with I2C bus
configure data into words and load into local buffer
tcp_server_task() - tcp socket listening for http request from wifi

receive tcp server socket http request
parse http request into following three types
file download (ex. index.html, image, .css or a .js file)
if the beginning of the payload contains either "index.html" or "" then the contents of the file index.html are forwarded to browser from esp read only memory space
this program could be easily modified to handle other files
files included in main/components.mk are tranferred to esp rom at build time, start and stop pointer are available to C program
request for a data string dump
the JavaScript in index.html sends an HTTP request header of
GET /getData?irpower=100&xrpower=180&raworbp=0&startstop=0
every 0.5 seconds
The parser updates the esp code global values of irpower, rpower, raworbp and startstop from the webpage
raw data is filtered by a two pole butterworth dsp operation
a comma delimited outstr[] is conctructed with the number of samples available, the average IR value, the average red value followed by alternating red and IR instantaneous values and sent to the JavaScript for display update. The data sent depends on the state of the raworbp flag.
the data pointer for the adc_task() data logging is reset
none of the above sends a 404 page
This nets C source code with no non-esp-idf dependencies of a couple hundred lines, most of which were cut and pasted from the sdk examples folder. The other coding portion of this program is an .html file including a javascript portion (about 200 lines) that is embedded in the esp read only memory at compile time (if referenced in component.mk). The .html file is transfered via tcp to your browser when the esp's url is addressed.

The JavaScript in the .html has a couple of functions:

control of plot modes, ie run/pause, display data type raw/filtered.
send GET query variable data from browser tags to esp (for things like controlling led brightness).
downloads plotly.js for handeling graph real time output display.
formats data returned from esp into plotly format
calculates and displays heartrate and oxygen concentration measurements.
The video starts out with no finger on the max30102 so it just shows noise. When a finger is placed on the module there is a big spike in the graph due to the reflected light going to almost fullscale, the IIR filter takes a little while to filter out the dc, the dc autoranges out, and the heart beats show up pretty clearly. The intensity of red and ir diodes can be changed, the updating can be halted and the unfiltered data can be displayed. The heart rate and SpO2 concentrations are calculated and displayed.


##### The tcp_server_task in the esp takes the incoming IP packets combines, windows, verifies/requests retransmission, and notifies us that an http request is ready. When I filled 192.168.0.122/index.html into my browser address bar window the contents of the tcp server (running over in my esp running a wifi connection at IP adrress 192.168.0.122) receive buffer were:

##### The esp programs tcp_sever_task examines the incoming packet and extracts parameters url_type, url_name and url_resource. In the above packet the parameters extracted would be url_type = ‘GET’ and url_name = ‘index.html’ and url_resource is empty, this packet was sent from browser request 192.168.0.122/index.html in the serch bar. The program figures out that this is a request of the contents of the file ‘index.html’ (stored in esp eprom) and forwards it to the tcp server socket send command. The browser executes the data returning from this request will be executed as html.
<img align="right" width="45%" height="350" src="helloworld0.png"></img>
##### The browser request 192.168.0.122/getData?hello+world tcp_server_task extracts url_name = 'getData' and url_resource = 'hello+world'. The top level subroutine getData() is called - which generated a simple web page output to the socket back to the browser. The url_resource string can be parsed to transfer state from the brower to the esp. The esp can respond in the webpage trnsmitting esp state back to the browser. The browser command above can also be generated by the html code with action commands.
##### If neither of the above url_type/url_name conditions are not matched the esp returns a simple 404 page.
##### The entire top level `website.c` program is shown below, the main portion of the program starts the `nvs_flash_init()` (where the index.html file is stored) and then `initialize_wifi()` and `wait_for_ip()` (initialize and wait for ip connection). The program then starts `xTaskCreate(tcp_server_task, "tcp_server", 8192, NULL, 4, NULL)` which sets up socket to listen for tcp connection, then reading and parsing that packet. The `tcp_server_task` will take action based on these results, either serving up index.html page, dumping a 404 page or running the subroutine GetData in website.c. In this case all the GetData subroutine does is make a simple html responce to the client browser.
```C
#include <string.h>
#include <stdio.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"

#define EXAMPLE_WIFI_SSID "troutstream"
#define EXAMPLE_WIFI_PASS "password"
#define PORT 80
char glob_ipadr[25];
char glob_ipadrc[25];
void wait_for_ip();
void initialize_wifi(void);
void tcp_server_task(void *pvParameters);

void GetData( int sock, char *url_resource, int length ) {
  char temp[1000];
  sprintf(temp,"\
     <!DOCTYPE HTML>\                  
     <html>\                  
     <head>\                  
     <title> Hello World!</title>\                  
     <style>\                  
       h1 { text-align:center; color:blue; }\                  
       h2 { text-align:center; color:red; }\                  
       h5 { text-align:center; color:green; }\                  
     </style>\                  
     </head>\                  
     <body>\                  
     <h1>getData() subroutine<h1>\                  
     <h2>Thanks for sending me --> %s<h2>\
     <h5>my ip is %s<h5>\
     <h5>your ip is %s<h5></body></html>\
     </body>\                  
     </html>\n", 
          url_resource, glob_ipadr, glob_ipadrc);
  temp[strlen(temp)] = '\0';
  send(sock, temp, strlen(temp), 0);
}

void app_main()
{
    nvs_flash_init();
    initialize_wifi();
    wait_for_ip();
 
    xTaskCreate(tcp_server_task, "tcp_server", 8192, NULL, 4, NULL);

}
```
##### For simple projects where you just want to control a few parameters on a remote esp controlled system having the esp the esp generate the retuen webpage isn't that bad. In some cases it makes sense to add a little javascript to your index.html file; with javascript you needn't send an html file from the esp to the browser, a data packet is sent from the esp which the javascript running on the browser uses to overwrite dom fields anywhere on the bowser display. Another very useful feature is that it is easy to use something link curl commands to send and receive data from the esp, including collecting and storing data. 
## Setup machine for build
##### There are plenty of sites on the web to get you up end running with esp-idf, this is an official https://docs.espressif.com/projects/esp-idf/en/latest/get-started/ that steered me through. A few lines down in the Setup Toolchain area you can pick options for windows, linux or mac os.  Next go to what you want to be your working directory and download repository and setup directory environment depenancies.
##### for the esp32 (if you installed idf directories into into ~/esp like I did)
```
cd <directory in which project will be installed>
git clone https://github.com/baetis-ma/esp32-idf-website
cd esp32-idf-website
. ~/esp/esp-idf/export.sh  (read carefully - .sh is sourced)
```
##### or for the esp8266, this link is pretty good for esp8266 idf setup https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/index.html (if you installed idf directorys int ~/esp8266 like I did)
```
cd <directory in which project will be installed>
git clone https://github.com/baetis-ma/esp32-idf-website
cd esp32-idf-website
export IDF_PATH=~/esp8266/ESP8266_RTOS_SDK
export PATH=$PATH:~/esp8266/xtensa-lx106-elf/bin
```
##### Stay in the esp32-idf-website directory on your compter and type `make menuconfig` (make sure you have exported environment variables above into that terminal's shell).  You can set the tty port and change the download speed here (Serial flaser config) to 921600 baud on the esp32 (esp8266 worked at 115200 baud max). `make flash monitor` will build, download, exectute and monitor the esp. If all went well that's it, if you change some of the code run `make flash monitor' again, the changes made to the code will be compiled, linked and downloaded. 
##### First time through it takes a few minutes to build everything, because the rtos operating is being built. When the monitor comes up and the esp boots look the monitor output for the station ip address and type that into a browser address box.  Test out adding /index.html or /getData or /getData?a=hellow+world to the address url (check monitor output in the terminal to see what the contents of the tcp server socket's receive buffer). A path not identified will 404, as does the favicon.ico that the browser likes to add in as an additional http request after the browser address bar request (if you want a favicon.ico add to tcp_server_task and update component.mk with your fav favicom.ico, I suppose).
## Next Step -
##### Another repository, baetis-ma/motortest, has documentation for a project that uses a webpage to control a test of a motor with a propellor. The esp hosts a webpage allowing :
```
- power relay to the motor on/off
- frequency of esc pulses
- pwm pulse width to esc control
- rotation speed of motor
- thrust measured by strain guage
- current being used by esc
- voltage into esc
```
##### Using curl commands in a shell (or perl) script allows programmatic control of motor testing, where the pwm can be adjusted upward slowly and the motor speed, motor power and generated thrust can be saved to a file and studied with gnuplot afterward. 
## Notes
##### Make sure to either add your user to dialout (permanent) or chmod 666 /dev/ttyUSB0 (for example) if you run into a device permission problem (linux). Also the file main/website.c has to be editted to reflect yor own access point credentials (#define EXAMPLE_WIFI_SSID "troutstream" and #define EXAMPLE_WIFI_PASS "password").
##### Tested on on half dozen from ESP01 to ESP32S modules, you can get the ESP01 for as little as a dollar and the ESP32S for about six dollars.
