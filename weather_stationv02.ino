/*
Written by Alexander Hörman & Clarence Robild
Documentation:
This software used for a IoT-based weather system which uses LoRa to communicate established LoRaWAN gateways. Our examples
is connected to the netmore platform. Changing thhe parameters found in arduino_secrets.h the application can easily be connected to other platforms
such as The Things Network.
Library are explained with a short description and purpose which is available at : INSERT LINK TO GITHUB
BME280 SENSOR
  Description:
    BME280 sensor module. Reads Temperature, Humidty and barometric pressure. Using pressure readings estimation about altitude can also be made.
  Communication protocol: I2C or SPI
  Reads: Temperature, Humidity and barometric pressure
  Connection:
    I2C:
        -SCK(SCL Pin) --> A5
        -SDI(SDA Pin) --> A4  
        -VCC --> VCC (3.3V - 5V)
        -GND --> GND
RAIN SENSOR
  Description:
    Rain gauge sensor which sends a pulse whenever the bucket tips. Precision is limited by what hardware is implemented.
    For calibration check documentation.
  Communication protocol: RJ11 connector to digital I/O
  Reads: Temperature, Humidity and barometric pressure
  Connection:
    RJ11:
        -Green -> D4 (15k pullup resistor also connected to GND)  
        -RED --> VCC
*/

#include "arduino_secrets.h"
String appEui = SECRET_APP_EUI;
String appKey = SECRET_APP_KEY;

#include <MKRWAN.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "ArduinoLowPower.h"
#include <RTCZero.h>

RTCZero rtc;

int loopStart;
int timeLeftOfLoop;

//Define BME280 Sensor, if other model than adafruit the I2C address will either be 0x76 or 0x77
Adafruit_BME280 bme280;

//Define modem for mkr1300 WAN.
LoRaModem modem;

//Input pin for rain gauge sensor. Must support interrupt.
//MKR 1300 WAN has 8 external interrupt pins: D0, D1, D4, D5, D6, D7, D8, A1 -or D16-, A2 - or D17
#define RainPin 4  

//variables for hourly rain and a value calcualted for our specific raing gauge sensor.
//Calibration: Rainfall in cm = Volume of rain/ Catch area  ==> bucketAmount = Rainfall in cm / total amount of bucket tips.
//Example: 10ml / 55cm^2 = 0.182cm = 1.82mm ==> 1.82mm / 5 tips = 0.364 mm/bucket tip
const float bucketAmount = 0.364;
float hourlyRain = 0.0;  

//sleep time in seconds
int secondsToSleep = 3600;



void setup() {

  Serial.begin(115200);
 
  pinMode(RainPin, INPUT_PULLUP);  //Set rainPin to input mode to read signal when a bucket tip occur
  LowPower.attachInterruptWakeup(digitalPinToInterrupt(RainPin), addRainCounter,RISING); //Attach an interrupt to the rainPin which executes the method named "addRainCounter"
   

  //Checks connection to the bme280, and gives relevant error description
  if (!bme280.begin()) {
    Serial.println("Invalid BME280, control sensor connection!");
    while (1);
  }

  //Checks the connection to modem. Starts and defines the frequency range used.
  if (!modem.begin(EU868)) {
    Serial.println("ERROR. Failed to start LoRa module, check connection and frequency used in your country!");
    while (1) {}
  };

  //Prints module version and DevEUI. DevEUI is used for connecting to platform of choice. DevEUI is a unique ID for the LoRa module.
  Serial.print("Module version: ");
  Serial.println(modem.version());
  Serial.print("DevEUI: ");
  Serial.println(modem.deviceEUI());

  //Join using appEUI and appkey, if connection fails throw error and start infinite loop
  int connected = modem.joinOTAA(appEui, appKey);
  if (!connected) {
    Serial.println("ERROR. Failed to connect to gateway. Control that you are within range of a gateway!");
    Serial.println("For the The Things Network the map can be found within the manual!");
    while (1) {}
  }
  rtc.begin();
  rtc.setEpoch(0);
  loopStart = rtc.getEpoch();
}

/*
  Name: sendSensorValuesToGateway
  Return: void
  Called from: void loop();
  Function: Reads values through the I2C conenction and appropiate address 0x76 or 0x77 depending on hardware. These are formated to a payload string
*/
void sendSensorValuesToGateway() {

    //Read BME280 values and assign to their respective variable.
    float temp = bme280.readTemperature();
    float humidity = bme280.readHumidity();
    float pressure = bme280.readPressure() / 100.0F;

    String payload = String(temp) + "," +String(humidity) + "," + String(pressure) + "," + String(hourlyRain);//Construct the payload format

    int packetEndCheck;
    modem.beginPacket();//start LoRa packet construction
    modem.print(payload);//Insert payload, for hex: modem.print(), for bytes: modem.write().
    packetEndCheck = modem.endPacket(true);//set end indcator for LoRa packet

    //Check for that the packet has reached its end, if so packet has been sent succesfully. Otherwise throw error message.
    if (packetEndCheck > 0) {
      Serial.println("Message sent successfully!");
    } else {
      Serial.println("ERROR sending message");
    }

    hourlyRain = 0;//Reset rain value after every sleep period.
}

void loop(){

  //Epoch gives time since program started, check if last go to sleep the remaining time until seconds to sleep has been achived.
  //This is needed as the prgram exits sleep whenever rain sensor interrupts the program adn can thus not remember how much time was
  //left to sleep. Epochs solve this as rtc is on in sleep mode.
  if(rtc.getEpoch()-loopStart >= secondsToSleep){
    sendSensorValuesToGateway();
    loopStart = rtc.getEpoch();

  }else{
    timeLeftOfLoop = secondsToSleep-(rtc.getEpoch()-loopStart);
    //delay(timeLeftOfLoop*1000);
    //LowPower.idle(timeLeftOfLoop*1000);
    //LowPower.sleep(timeLeftOfLoop*1000);
    LowPower.deepSleep(timeLeftOfLoop*1000);
  }
}

/*
  Name: addRainCounter 
  Return: void
  Called from: Interupt connected to pin D4.
  Function: Add counter for how many times the bucket has tipped. Is surronded by a debouncer section to avoid faulty readings during heavy rainfall
*/
void addRainCounter(){

  static unsigned long last_interrupt_time = 0;
  unsigned long interrupt_time = millis();
  
  //if new interrupt within 200ms, it is most likely a faulty activation due to debounce.
  if (interrupt_time - last_interrupt_time > 65) 
    {
      hourlyRain+=bucketAmount; //add one rain counter
    }
  last_interrupt_time = interrupt_time;   
}
