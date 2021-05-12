/*
  Pengy v2.0 - Air Quality Parameters acquisition

  This firmware acquires:
     - PM2.5 and PM10 concetration from Sensirion SPS30 sensor
     - temperature, humidity and preassure from BME680 sensor
     - sound level from LM386 enabled electret microphone (https://www.waveshare.com/sound-sensor.htm)

  then accumulates, agregates data and sends it every hour using LoRaWAN 
  via The Things Network to Thingy.IO system.

  Copyright (c) 2021 Dusan Stojkovic
*/

#include "Arduino.h"
#include "Wire.h"

#include "LoRaWan_APP.h"
#include "lorawan.hpp"

#include <Streaming.h>

//
#include <SPS30.h>

double pm1Cummulative=0, pm25Cummulative=0, pm10Cummulative=0;
int countAQCummulative=0;

float pm1, pm25, pm10;
SPS30 sps30;

#include <Adafruit_Sensor.h>
#include "Adafruit_BME280.h"
Adafruit_BME280 bme;

long temperatureCummulative=0;
long humidityCummulative=0;
long pressureCummulative = 0;
int countTHPCummulative=0;

float temperature;
float humidity;
float pressure;

double soundValueCummulative=0;
int countSoundValueCummulative = 0;
double soundValue=0;

//

float noise;

// Eyes
#include "CubeCell_NeoPixel.h"
CubeCell_NeoPixel eyes(1, RGB, NEO_GRB + NEO_KHZ800);

#define DEBUG true
#define LOG if(DEBUG)Serial

uint8_t nwkSKey[] = NWK_S_KEY;
uint8_t appSKey[] = APP_S_KEY;
uint32_t devAddr = DEV_ADDR;

unsigned int loops = 0;

bool send = false;
uint8_t data[6];


void wait(unsigned long d)
{
    delay(d);
    return;

    unsigned long b_ = micros();
    unsigned long e_ = d;

    while (e_ > 0)
    {
        //os_runloop_once();
        //yield();
        while ( e_ > 0 && (micros() - b_) >= 1000)
        {
            e_--;
            b_ += 1000;
        }
    }
}

// LED lights
void lightInit()
{
    pinMode(Vext,OUTPUT);
    digitalWrite(Vext,LOW);
    eyes.begin(); eyes.clear(); eyes.show();
}

void lightMeasuringStart()
{
    // make rainbow effect
}

void lightMeasuringStop()
{
    eyes.begin(); eyes.clear(); eyes.show();
}

void lightSetupBegin()
{
    eyes.setPixelColor(0, eyes.Color(255,0,0)); eyes.show(); delay(200);
    eyes.setPixelColor(0, eyes.Color(0,255,0)); eyes.show(); delay(200);
    eyes.setPixelColor(0, eyes.Color(0,0,255)); eyes.show(); delay(200);
}

void lightSetupEnd()
{
    eyes.clear(); eyes.show();    
}



// Acquisitionß
void handleNoise()
{
    lightMeasuringStart();

    unsigned long start_ts = millis();
    while (millis() - start_ts < 7500)
    {
        double soundValMin=3.4028235E+38, soundValSubMin=3.4028235E+38, soundValMax=-3.4028235E+38, soundValSubMax= -3.4028235E+38;

        unsigned long sound_ts= millis();
        while (millis() - sound_ts < 125) // 125ms
        {
            int soundVal = 0; for (int i=0; i<3; i++) soundVal += analogRead(GPIO0); soundVal/=4;

            if (soundVal < soundValMin) 
            {
                soundValSubMin = soundValMin;
                soundValMin = soundVal;
            }
            else if (soundVal < soundValSubMin)
                soundValSubMin = soundVal;
            
            if (soundVal > soundValMax) 
            {
                soundValSubMax = soundValMax;
                soundValMax = soundVal;
            }
            else if (soundVal > soundValSubMax)
                soundValSubMax = soundVal;
        }
        
        soundValMax = soundValSubMax;
        soundValMin = soundValSubMin;

        soundValueCummulative += soundValMax - soundValMin; // accumulate envelope
        countSoundValueCummulative++;
    }

    LOG << " span Cum = " << soundValueCummulative << "   span Cnt " << countSoundValueCummulative << endl;

    soundValue = soundValueCummulative / countSoundValueCummulative;

    lightMeasuringStop();

    LOG << F("NOISE - ") << F("N=") << soundValue << endl;
}

void handleTHP()
{
    lightMeasuringStart();

    float temperatureMin=3.4028235E+38, temperatureMax=-3.4028235E+38;
    float humidityMin=3.4028235E+38, humidityMax=-3.4028235E+38;
    float pressureMin=3.4028235E+38, pressureMax=-3.4028235E+38;
    int thpCount = 0;
    
    for (int l=0; l<7; l++)
    {
        float temperature = bme.readTemperature();         // °C
        float humidity    = bme.readHumidity();            // %RH
        float pressure    = bme.readPressure() * 0.01f;    // hPa
        float altitude    = bme.readAltitude(1013.25);     // m

        if (!isnanf(temperature) && !isnanf(humidity) && !isnanf(pressure))
        {
            Serial << F(" (") << (l+1) << F(")   t=") << temperature << F("°C   H=") << humidity << F("%RH   p=") << pressure << F(" hPa   MASL=") << altitude << F(" m") << endl;

            temperatureCummulative += temperature;
            if (temperature < temperatureMin) temperatureMin = temperature;
            if (temperature > temperatureMax) temperatureMax = temperature;                

            humidityCummulative += humidity;
            if (humidity < humidityMin) humidityMin = humidity;
            if (humidity > humidityMax) humidityMax = humidity;                

            pressureCummulative += pressure;
            if (pressure < pressureMin) pressureMin = pressure;
            if (pressure > pressureMax) pressureMax = pressure;   

            thpCount++;
            countTHPCummulative++;
        }
        else
        {
            LOG << F(" (") << (l+1) << F(")  X") << endl;
        }
        wait(1000);
    }
    LOG << "t m " << temperatureMin << " M " << temperatureMax << endl;
    LOG << "H m " << humidityMin << " M " << humidityMax << endl;
    LOG << "p m " << pressureMin << " M " << pressureMax << endl;

    if (thpCount > 2)
    {
        temperatureCummulative = temperatureCummulative - temperatureMax - temperatureMin;
        humidityCummulative = humidityCummulative - humidityMax - humidityMin;
        pressureCummulative = pressureCummulative - pressureMax - pressureMin;
        countTHPCummulative = countTHPCummulative - 2;
    }
    
    temperature = 1.0 * temperatureCummulative / countTHPCummulative;
    humidity = 1.0 * humidityCummulative / countTHPCummulative;
    pressure = 0.01 * pressureCummulative / countTHPCummulative;

    lightMeasuringStop();

    //LOG << F("THP - ") << F("t=") << temperature << F("°C - H=") << humidity<< F("%RH - p=") << pressure << F(" hPa") << endl;
}

void handlePM()
{
    LOG << F("PM") << endl;
    
    lightMeasuringStart();

    bool began = sps30.beginMeasuring();
    LOG << (began ? F("+"):F("-")) << endl;

    wait(30000);

    float pm1Min=3.4028235E+38, pm1Max=-3.4028235E+38;
    float pm25Min=3.4028235E+38, pm25Max=-3.4028235E+38;
    float pm10Min=3.4028235E+38, pm10Max=-3.4028235E+38;
    int pmCount = 0;

    for (int l=0; l<7; l++)
    {
        if (sps30.readMeasurement())
        {
            LOG <<F(" (") << (l+1) << F(")   PM1.0=") << sps30.massPM1 << F("µg/m³  PM2.5=") << sps30.massPM25 << F("µg/m³  PM4.0=") << sps30.massPM4 << F("µg/m³  PM10=") << sps30.massPM10 << F("µg/m³  size=") << endl;

            pm1Cummulative += sps30.massPM1;
            if (sps30.massPM1 < pm1Min) pm1Min = sps30.massPM1;
            if (sps30.massPM1 > pm1Max) pm1Max = sps30.massPM1;

            pm25Cummulative += sps30.massPM25;
            if (sps30.massPM25 < pm25Min) pm25Min = sps30.massPM25;
            if (sps30.massPM25 > pm25Max) pm25Max = sps30.massPM25;

            pm10Cummulative += sps30.massPM10;
            if (sps30.massPM10 < pm10Min) pm10Min = sps30.massPM10;
            if (sps30.massPM10 > pm10Max) pm10Max = sps30.massPM10;                

            pmCount++;
            countAQCummulative++;
        }
        /*
        else 
        {
          LOG << F(" (") << (l+1) << F(") X   ") << pm.statusToString() << endl;
        }
        */
        wait(1000);
    }            
    
    //LOG << "cnt " << pmCount << endl;
    //LOG << "fpm m " << fpmMin << " M " << fpmMax << endl;
    //LOG << "pm10 m " << pm10Min << " M " << pm10Max << endl;

    if (pmCount > 2)
    {
        pm1Cummulative = pm1Cummulative - pm1Max - pm1Min;
        pm25Cummulative = pm25Cummulative - pm25Max - pm25Min;
        pm10Cummulative = pm10Cummulative - pm10Max - pm10Min;
        countAQCummulative = countAQCummulative - 2;
    }
    
    pm1 = 1.0 * pm1Cummulative / countAQCummulative;
    pm25 = 1.0 * pm25Cummulative / countAQCummulative;
    pm10 = 1.0 * pm10Cummulative / countAQCummulative;
    
    bool stopped = sps30.stopMeasuring();
    LOG << (stopped ? F("+"):F("-")) << endl;
    
    lightMeasuringStop();
}

// Normalization

void normalizeNoise()
{
    noise = CAL_NOISE_1 * log(noise) + CAL_NOISE_0;
}

void normalizeTHP()
{
    temperature = CAL_TEMPERATURE_1 * temperature + CAL_TEMPERATURE_0;
    humidity    = CAL_HUMIDITY_1    * humidity    + CAL_HUMIDITY_0;
    pressure    = CAL_PRESSURE_1    * pressure    + CAL_PRESSURE_0;
}

void normalizePM()
{
    pm10 = CAL_RPM_1 * pm10 + CAL_RPM_0;
    pm1 = CAL_FPM_1 * pm1 + CAL_FPM_0;
    pm25 = CAL_FPM_1 * pm25 + CAL_FPM_0;
}


// Setup & Loop
void setup()
{

    while (!Serial && millis() < 10000);
    Serial.begin(115200);
    Wire.begin();

    delay(10);
    Serial << endl << endl  << F(_NAME_) << " - " << F(__DATE__) << " " << F(__TIME__) << endl << "#" << endl << endl;

    // LED init
    lightInit();

    // LED in
    lightSetupBegin();

    // air quiality sensor
    Serial << F("Initializing SPS30 sensor ... ");
    bool initAQ = sps30.begin(Wire); 
    Serial << (initAQ ? F("OK") : F("NOK")) << endl;;

    // temperature, humidity, preassure
    Serial << F("Initializing BME280 sensor ... ");
    bool initTHP = bme.begin(0x76, &Wire);
    Serial << (initTHP ? F("OK") : F("NOK")) << endl;;

    // LoRaWAN
    boardInitMcu();
    LoRaWAN.ifskipjoin();

    LoRaWAN.init(loraWanClass,loraWanRegion);
    LoRaWAN.join();

    // LED out
    lightSetupEnd();

    Serial << "#" << endl << endl;
}

void loop()
{
    unsigned long loop_ts = millis();

    LOG << F("[") << loop_ts << F(" m / ") << loops <<  F(" l] ") << endl;

    if (loops % 20 == 1) // every 20 minutes - request readout
    {
        // Ambiental noise
        handleNoise();
        normalizeNoise();
        LOG << F("NOISE ") << F("v=") << noise << F("dBA") << endl;

        // Temperature, Humidity Pressure
        handleTHP();
        normalizeTHP();
        LOG << F("THP ") << F("t=") << temperature << F("°C - H=") << humidity<< F("%RH - p=") << pressure << F(" hPa") << endl;

        // Particulate metter
        countAQCummulative=0;
        pm1Cummulative=0;
        pm25Cummulative=0;
        pm10Cummulative=0;

        handlePM();
        normalizePM();
        LOG << F("PM ") << F(" 1.0=") << pm1 << F("µg/m³  2.5=") << pm25 << F("µg/m³  4.0=") << 0 << F("µg/m³  10=") << pm10 << F("µg/m³") << endl;
    }

    if (loops % 5 == 0) // every 5 minutes - sample THP
        handleTHP();

    if (loops % 5 == 0) // every 10 minutes - sample particulates
        handlePM();

    if (loops % 1 == 0) // every minute - sample noise
        handleNoise();

    if (loops % 20 == 2) // every 20 minutes - send
    {
        uint16_t datum;
		// port 1 - Humidity (0,1), Temperature (2,3), PM10 (4,5), PM25 (6,7)
		// port 2 - Humidity (0,1), Temperature (2,3), PM10 (4,5), PM25 (6,7), 
        //          Pressure (8,9), 
        //          CO (10,11), NH3 (12,13), N02 (14,15), 
        //          Noise (16,17)
        appDataSize = 14;

        // Humidity
        datum = humidity * 10;
        appData[0] = ( datum >> 8 ) & 0xFF; appData[1] = datum & 0xFF;
        // Temperature
        datum = temperature * 10;
        appData[2] = ( datum >> 8 ) & 0xFF; appData[3] = datum & 0xFF;
        // Pressure
        datum = pressure;
        appData[8] = ( datum >> 8 ) & 0xFF; appData[9] = datum & 0xFF;

        // PM 1.0
        datum = pm1 * 10;
        appData[0] = ( datum >> 8 ) & 0xFF; appData[1] = datum & 0xFF;
        // PM 2.5
        datum = pm25 * 10;
        appData[2] = ( datum >> 8 ) & 0xFF; appData[3] = datum & 0xFF;
        // PM 10
        datum = pm10 * 10;
        appData[4] = ( datum >> 8 ) & 0xFF; appData[5] = datum & 0xFF;

        // Noise
        datum = noise * 100;
        appData[16] = ( datum >> 8 ) & 0xFF; appData[17] = datum & 0xFF;
        
        send = true;
    }

    if (send)
    {
        // LED on
        eyes.clear(); eyes.show();

        // transmit
        LOG << F("Sending[") << millis() << F("]-");
        LoRaWAN.send();
        LOG << F(" ?") << endl;;

        send=false;

        // LED off
        eyes.setPixelColor(0, eyes.Color(255,255,255)); eyes.show(); 
        delay(200);
        eyes.clear(); eyes.show();
    }

    LOG << F("[") << (0.001*(millis()-loop_ts)) << F(" s]") << endl << endl << endl << endl;  

    while (millis() - loop_ts < 60000L) { wait(1000);} // sleep until full minute
    loops++;    

    ////////////////////////////////////
    //if (loops == 3) loops = 0; 
    ////////////////////////////////////

    uint16_t voltage=analogRead(ADC); //return the voltage in mV, max value can be read is 2400mV 

    eyes.setPixelColor(0, eyes.Color(10*loops % 256,0,0)); eyes.show(); delay(1000);
    eyes.setPixelColor(0, eyes.Color(0,10*loops % 256,0)); eyes.show(); delay(1000);
    eyes.setPixelColor(0, eyes.Color(0,0,10*loops % 256)); eyes.show(); delay(1000);
    eyes.clear(); eyes.show();
}