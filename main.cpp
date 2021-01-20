/* MCProject Version 2 
 * Autor: Eren Erdogan

 * Copyright (c) 2016 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mbed.h"
#include "MLX90614.h"
#include "HCSR04/HCSR04.h"

// Blinking rate in milliseconds
#define BLINKING_RATE_MS  800

I2C i2c(I2C_SDA, I2C_SCL);
MLX90614 IR_thermometer(&i2c);
HCSR04  usensor(D8,D9);
DigitalOut led(LED1);
DigitalOut laser(D11);
DigitalOut signal(D13);
InterruptIn *bewegungssensorInter;
DigitalIn bewegungssensor(PA_1);

/**
    Initialization of variables
*/
WiFiInterface *wifi;
TCPSocket socket;
SocketAddress a;
char buf[50];

bool connectWifi();
void interruptWorker();
void waitingForEventInterrupt();
void socketworkerWorker();
void laserWaitingBlink();
const char *sec2str(nsapi_security_t sec)
{
    switch (sec) {
        case NSAPI_SECURITY_NONE:
            return "None";
        case NSAPI_SECURITY_WEP:
            return "WEP";
        case NSAPI_SECURITY_WPA:
            return "WPA";
        case NSAPI_SECURITY_WPA2:
            return "WPA2";
        case NSAPI_SECURITY_WPA_WPA2:
            return "WPA/WPA2";
        case NSAPI_SECURITY_UNKNOWN:
        default:
            return "Unknown";
    }
}
/*
    Scanning WiFi in the area.
*/
int scan_demo(WiFiInterface *wifi)
{
    WiFiAccessPoint *ap;
    printf("Scan:\n");
    int count = wifi->scan(NULL,0);
    if (count <= 0) {
        printf("scan() failed with return value: %d\n", count);
        return 0;
    }
    /* Limit number of network arbitrary to 15 */
    count = count < 15 ? count : 15;
    ap = new WiFiAccessPoint[count];
    count = wifi->scan(ap, count);
    if (count <= 0) {
        printf("scan() failed with return value: %d\n", count);
        return 0;
    }
    for (int i = 0; i < count; i++) {
        printf("Network: %s secured: %s BSSID: %hhX:%hhX:%hhX:%hhx:%hhx:%hhx RSSI: %hhd Ch: %hhd\n", ap[i].get_ssid(),
               sec2str(ap[i].get_security()), ap[i].get_bssid()[0], ap[i].get_bssid()[1], ap[i].get_bssid()[2],
               ap[i].get_bssid()[3], ap[i].get_bssid()[4], ap[i].get_bssid()[5], ap[i].get_rssi(), ap[i].get_channel());
    }
    printf("%d networks available.\n", count);
    delete[] ap;
    return count;
}
/*
    Reading values from temperature sensor. Several measurements are carried out for 
    better measurement results.
*/
float readTemp(){
        float  tmpValue = IR_thermometer.read_temp(1);
        int loopCount = 21;
        int loopCountCorrect=0;
        float tempValueSum = 0;    
        for(int i = 0; i < loopCount;i++){
            tmpValue = IR_thermometer.read_temp(1);
            printf("Temperature outer loop is %5.1F degrees C\r\n",tmpValue);
            if (tmpValue >= 31.5 && tmpValue <= 43){
                tempValueSum += tmpValue;
                loopCountCorrect++;
            }
        }
        if (tempValueSum == 0){
            printf("Messfehler: %5.1F degrees C\r\n",tmpValue);
            return 0;
        }    
        tempValueSum = tempValueSum / loopCountCorrect;
        printf("Temperature is %5.1F degrees C\r\n",tempValueSum);
        return tempValueSum;
}

int getDistance_cm(){
        int dist;
        for(dist = -1; dist == -1;){
            usensor.setRanges(5,15);
            usensor.startMeasurement();
            dist = usensor.getDistance_cm();
            printf("%i \n\r",dist);
        }
        return dist;
}

bool leserTest(){
    printf(" -- Start Laser sensor test\n");
    for (int i=0; i< 2; i++){
        laser.write(1);  
        thread_sleep_for(BLINKING_RATE_MS);
        laser.write(0); 
        thread_sleep_for(BLINKING_RATE_MS);
    }
    printf(" -- End Laser sensor test\n");
    return false;
}
bool signalTest(){
    printf(" -- Start Signal sensor test\n");
    for (int i=0; i< 2; i++){
        signal.write(1);  
        thread_sleep_for(200);
        signal.write(0); 
        thread_sleep_for(200);
    }
    printf(" -- End Signal sensor test\n");
    return false;
}
void startSignal(){
    signal.write(1);  
    thread_sleep_for(200);
     signal.write(0); 
}
/*
    The connected sensors are tested here to see whether they work. 
    The sensors are tested sequentially.
*/
bool sensorTest(){
    printf("Begin sensor test\n");
    leserTest();
    signalTest();
    printf("End sensor test\n");
    return false;
}

int main(){
    sensorTest();
    printf("Start Wifi\n");
    #ifdef MBED_MAJOR_VERSION
        printf("Mbed OS version %d.%d.%d\n\n", MBED_MAJOR_VERSION, MBED_MINOR_VERSION, MBED_PATCH_VERSION);
    #endif
    // Test if Wifi connectin successful 
    if (!connectWifi())
        return -1;
   
    nsapi_size_or_error_t result =  socket.open(wifi); // open test socket connection
    if (result != 0) {
        printf("Error! socket.open() returned: %d\n", result);
        wifi->disconnect();
        return -1;
    }
    socket.close(); // open test socket connection
    /*
        Setup application connection
    */
    // Port 
    a.set_port(8002); 
    // IP Address 
    a.set_ip_address("172.20.10.7");
    startSignal();
    for (int i = 0;i < 500;i++){
        if(bewegungssensor.read() && getDistance_cm() > 4 &&  getDistance_cm() < 7){   //Die Messung zwischen 5 und 6 cm
            socketworkerWorker();
        } else{
            laserWaitingBlink();
        }
    }

    char rbuffer[64];
    int rcount = socket.recv(rbuffer, sizeof rbuffer);
    printf("recv %d [%.*s]\n", rcount, strstr(rbuffer, "\r\n") - rbuffer, rbuffer);
    wifi->disconnect();
    printf("\nDone\n");
}

void laserWaitingBlink(){
    laser.write(1);  
    thread_sleep_for(100);
    laser.write(0);  
    thread_sleep_for(100);
    laser.write(1);
    thread_sleep_for(100);
    laser.write(0);
    thread_sleep_for(800);           
}

void socketworkerWorker(){
     laser.write(1);
     signal.write(1);
     thread_sleep_for(600);  
     float temp = readTemp();
     if (temp != 0){
            socket.open(wifi);
            socket.connect(a);
            sprintf(buf, "%5.1F;%i\n",temp ,getDistance_cm());
            int scount = socket.send(buf, sizeof buf);
            printf("sent %d [%.*s]\n", scount, strstr(buf, "\r\n") - buf, buf);
            socket.close();
    }
    laser.write(0); 
    thread_sleep_for(600);
    signal.write(0);
     
}

bool connectWifi(){
    wifi = WiFiInterface::get_default_instance();
    if (!wifi) {
        printf("ERROR: No WiFiInterface found.\n");
        return false;
    }
    
    printf("\nConnecting to %s...\n", "iPhone von Eren Er");
    // Set Wifi/SSID name and password 
    int ret = wifi->connect("Wifi SSID", "Wifi pwd", NSAPI_SECURITY_WPA2);
    if (ret != 0) {
        printf("\nConnection error: %d\n", ret);
        return false;
    }

    printf("Success\n\n");
    printf("MAC: %s\n", wifi->get_mac_address());
    printf("IP: %s\n", wifi->get_ip_address());
    printf("Netmask: %s\n", wifi->get_netmask());
    printf("Gateway: %s\n", wifi->get_gateway());
    printf("RSSI: %d\n\n", wifi->get_rssi());
    return true;
}

