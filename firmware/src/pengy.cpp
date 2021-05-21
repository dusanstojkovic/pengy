/*
  Pengy v2.0 - Air Quality Parameters acquisition

  This firmware acquires:
     - PM1.0, PM2.5 and PM10 concetration from Sensirion SPS30 sensor
     - temperature, humidity and preassure from BME680 sensor
     - sound level from MAX9814 electret microphone

  then accumulates, agregates data and sends it every hour using LoRaWAN 
  via The Things Network to Thingy.IO system.

  Copyright (c) 2021 Dusan Stojkovic
*/

#include "Arduino.h"
#include "Wire.h"

//
#include "LoRaWanMinimal_APP.h"
//#include "LoRaWan_APP.h"
//#include "lorawan.hpp"

#include <Streaming.h>

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

float noise = 0;


// Eyes
#include "CubeCell_NeoPixel.h"
CubeCell_NeoPixel eyes(1, RGB, NEO_GRB + NEO_KHZ800);

#define DEBUG true
#define LOG if(DEBUG)Serial

//
static const uint8_t NWKSKEY[16] = NWK_S_KEY;
static const uint8_t APPSKEY[16] = APP_S_KEY;
static const uint32_t DEVADDR = DEV_ADDR;
uint16_t userChannelsMask[6]={ 0x00FF,0x0000,0x0000,0x0000,0x0000,0x0000 };
//
//uint8_t nwkSKey[] = NWK_S_KEY;
//uint8_t appSKey[] = APP_S_KEY;
//uint32_t devAddr = DEV_ADDR;

unsigned int loops = 0;

bool send = false;
uint8_t data[14];

// LED lights
void lightInit()
{
    pinMode(Vext,OUTPUT);
    digitalWrite(Vext,LOW);
    eyes.begin(); eyes.clear(); eyes.show();
}

// Light effects

static TimerEvent_t lightEvent;
volatile uint32_t lightTicks;
enum lightEffect_t { RANDOM, RAINBOW, PULSE_RGB} ;

static lightEffect_t lightEffect;

void lightTick()
{
    switch (lightEffect)
    {
        case RANDOM:
            eyes.setPixelColor(0, eyes.Color(random(255), random(255), random(255)));
            break;
        case RAINBOW:
            eyes.setPixelColor(0, eyes.Color(eyes.sine8((lightTicks-85) % 256), eyes.sine8(lightTicks % 256), eyes.sine8((lightTicks+85) % 256)));
            break;
        case PULSE_RGB:
            break;
    }
    eyes.show();
    
    lightTicks = (lightTicks + 1) ;

    TimerSetValue(&lightEvent, 10);
    TimerStart(&lightEvent);
}

void lightEffectStart(lightEffect_t effect)
{
    eyes.begin();
    lightTicks = 0;
    lightEffect = effect;
    lightTick();
}

void lightEffectStop()
{
    TimerStop(&lightEvent);
    eyes.clear(); eyes.show();
}

void lightSetupBegin()
{
    eyes.begin(); 
    for(int c = 0; c < 3; c++ ) {
        for(int k = 0; k < 256; k++) {
            switch(c) {
                case 0: eyes.setPixelColor(0, eyes.Color(k,0,0));  break;
                case 1: eyes.setPixelColor(0, eyes.Color(0,k,0));  break;
                case 2: eyes.setPixelColor(0, eyes.Color(0,0,k));  break;
            }
            eyes.show(); 
            delay(1);
        }
        for(int k = 255; k >= 0; k--) {
            switch(c) {
                case 0: eyes.setPixelColor(0, eyes.Color(k,0,0));  break;
                case 1: eyes.setPixelColor(0, eyes.Color(0,k,0));  break;
                case 2: eyes.setPixelColor(0, eyes.Color(0,0,k));  break;
            }
            eyes.show(); 
            delay(1);
        }
    }
}

void lightSetupStop()
{
    eyes.clear(); eyes.show();    
}

// Acquisition

void acquireNoise()
{
    LOG << endl << F("Noise >>>") << endl;

    lightEffectStart(RAINBOW);

    unsigned long start_ts = millis();
    while (millis() - start_ts < 5000)
    {
        double soundValMin=3.4028235E+38, soundValSubMin=3.4028235E+38, soundValMax=-3.4028235E+38, soundValSubMax= -3.4028235E+38;

        unsigned long sound_ts= millis();
        while (millis() - sound_ts < 125) // 125ms
        {
            int soundVal = 0; for (int i=0; i<3; i++) soundVal += analogRead(ADC); soundVal/=4;

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
    lightEffectStop();

    LOG << F("<<<") << endl << endl;;    

    noise = soundValueCummulative / countSoundValueCummulative;
    noise = CAL_NOISE_1 * log(noise) + CAL_NOISE_0;    
}

void acquireTHP()
{
    LOG << endl << F("THP >>>") << endl;

    lightEffectStart(RAINBOW);

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
            LOG << F(" (") << (l+1) << F(")   t=") << temperature << F("°C   H=") << humidity << F("%RH   p=") << pressure << F(" hPa   MASL=") << altitude << F(" m") << endl;

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
        delay(1000);
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
    
    lightEffectStop();

    LOG << F("<<<") << endl << endl;
    
    temperature = 1.0 * temperatureCummulative / countTHPCummulative;
    humidity = 1.0 * humidityCummulative / countTHPCummulative;
    pressure = 1.0 * pressureCummulative / countTHPCummulative;

    temperature = CAL_TEMPERATURE_1 * temperature + CAL_TEMPERATURE_0;
    humidity    = CAL_HUMIDITY_1    * humidity    + CAL_HUMIDITY_0;
    pressure    = CAL_PRESSURE_1    * pressure    + CAL_PRESSURE_0;    
}

void acquirePM()
{
    LOG << endl << F("PM >>>") << endl;
    
    lightEffectStart(RAINBOW);

    bool began = sps30.beginMeasuring();
    LOG << (began ? F("+"):F("-")) << endl;

    delay(30000);

    float pm1Min=3.4028235E+38, pm1Max=-3.4028235E+38;
    float pm25Min=3.4028235E+38, pm25Max=-3.4028235E+38;
    float pm10Min=3.4028235E+38, pm10Max=-3.4028235E+38;
    int pmCount = 0;

    for (int l=0; l<7; l++)
    {
        if (sps30.readMeasurement())
        {
            LOG <<F(" (") << (l+1) << F(")   PM1.0=") << sps30.massPM1 << F("µg/m³  PM2.5=") << sps30.massPM25 << F("µg/m³  PM4.0=") << sps30.massPM4 << F("µg/m³  PM10=") << sps30.massPM10 << F("µg/m³  size=") << sps30.typPartSize << F("µm") << endl;

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
        delay(1000);
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
    
    bool stopped = sps30.stopMeasuring();
    LOG << (stopped ? F("+"):F("-")) << endl;
    
    lightEffectStop();

    LOG << F("<<<") << endl << endl;
    
    pm1 = 1.0 * pm1Cummulative / countAQCummulative;
    pm25 = 1.0 * pm25Cummulative / countAQCummulative;
    pm10 = 1.0 * pm10Cummulative / countAQCummulative;

    pm10 = CAL_PM10_1 * pm10 + CAL_PM10_0;
    pm1 = CAL_PM1_1 * pm1 + CAL_PM1_0;
    pm25 = CAL_PM25_1 * pm25 + CAL_PM25_0;    
}

void acquireReset()
{
    countSoundValueCummulative = 0;
    soundValueCummulative = 0;

    countTHPCummulative = 0;
    temperatureCummulative = 0;
    humidityCummulative = 0;
    pressureCummulative = 0;

    countAQCummulative=0;
    pm1Cummulative=0;
    pm25Cummulative=0;
    pm10Cummulative=0;
}

/*
void downLinkDataHandle(McpsIndication_t *mcpsIndication)
{
  Serial.printf("Received downlink: %s, RXSIZE %d, PORT %d, DATA: ",mcpsIndication->RxSlot?"RXWIN2":"RXWIN1",mcpsIndication->BufferSize,mcpsIndication->Port);
  for(uint8_t i=0;i<mcpsIndication->BufferSize;i++) {
    Serial.printf("%02X",mcpsIndication->Buffer[i]);
  }
  Serial.println();
}
*/

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
    TimerInit(&lightEvent, lightTick);
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
    Serial << F("Initializing LoRaWAN ... ");
    boardInitMcu();
    LoRaWAN.begin(LORAWAN_CLASS, ACTIVE_REGION);
    LoRaWAN.setAdaptiveDR(true);
    bool joined = LoRaWAN.joinABP((uint8_t*)NWKSKEY, (uint8_t*)APPSKEY, DEVADDR);
    Serial << (joined ? F("OK") : F("NOK")) << endl;
    //
    //boardInitMcu();
    //LoRaWAN.ifskipjoin();
    //
    //LoRaWAN.init(loraWanClass,loraWanRegion);
    //LoRaWAN.join();

    // LED out
    lightInit();
    lightSetupStop();

    Serial << "#" << endl << endl;
}

void loop()
{
    unsigned long loop_ts = millis();

    Serial << endl << F("[") << loop_ts << F(" m / ") << loops <<  F(" l] ") << endl;

    if (loops % 1 == 0) // every minute - sample noise
    {
        acquireNoise();
        Serial << F("NOISE ") << F("v=") << noise << F("dBA") << endl;
    }

    if (loops % 5 == 0) // every 5 minutes - sample THP
    {
        acquireTHP();
        Serial << F("THP ") << F("t=") << temperature << F("°C - H=") << humidity<< F("%RH - p=") << pressure << F(" hPa") << endl;        
    }

    if (loops % 10 == 0) // every 10 minutes - sample particulates
    {
        acquirePM();
        Serial << F("PM ") << F(" 1.0=") << pm1 << F("µg/m³  2.5=") << pm25 << F("µg/m³  4.0=") << 0 << F("µg/m³  10=") << pm10 << F("µg/m³") << endl;        
    }

    if (loops % 20 == 2) // every 20 minutes - send
    {
        uint16_t datum;
        //appDataSize = 14;
        //appData *******

        // Humidity
        datum = humidity * 10;
        data[0] = ( datum >> 8 ) & 0xFF; data[1] = datum & 0xFF;
        // Temperature
        datum = temperature * 10;
        data[2] = ( datum >> 8 ) & 0xFF; data[3] = datum & 0xFF;
        // Pressure
        datum = pressure;
        data[4] = ( datum >> 8 ) & 0xFF; data[5] = datum & 0xFF;

        // PM 1.0
        datum = pm1 * 10;
        data[6] = ( datum >> 8 ) & 0xFF; data[7] = datum & 0xFF;
        // PM 2.5
        datum = pm25 * 10;
        data[8] = ( datum >> 8 ) & 0xFF; data[9] = datum & 0xFF;
        // PM 10
        datum = pm10 * 10;
        data[10] = ( datum >> 8 ) & 0xFF; data[11] = datum & 0xFF;

        // Noise
        datum = noise * 100;
        data[12] = ( datum >> 8 ) & 0xFF; data[13] = datum & 0xFF;
        
        send = true;
    }

    if (send)
    {
        // LED on
        lightEffectStart(RANDOM);

        // transmit
        Serial << F("Sending[") << millis() << F("]-");
        bool sent = LoRaWAN.send(sizeof(data), data, 3, false);
        Serial << (sent ? F("OK") : F("NOK")) << endl;
        //
        //LoRaWAN.send();
        //        
        
        if (sent)
        {
            acquireReset();
            send=false;
        }

        // LED off
        delay(500);
        lightEffectStop();
    }

    LOG << F("[") << (0.001*(millis()-loop_ts)) << F(" s]") << endl << endl << endl << endl;  
    
    // Color to match EAQI
    eyes.clear();
    if (pm25 >= 50 && pm25 < 800 || pm10 >= 100 && pm10 < 1200) { eyes.setPixelColor(0, eyes.Color(150,0,50)); } else // Very poor
    if (pm25 >= 25 && pm25 <  50 || pm10 >=  50 && pm10 <  100) { eyes.setPixelColor(0, eyes.Color(255,80,80)); } else // Poor
    if (pm25 >= 20 && pm25 <  25 || pm10 >=  35 && pm10 <   50) { eyes.setPixelColor(0, eyes.Color(240,230,35)); }else // Moderate
    if (pm25 >= 10 && pm25 <  20 || pm10 >=  20 && pm10 <   35) { eyes.setPixelColor(0, eyes.Color(80,204,170)); } else // Fair
    if (pm25 >   0 && pm25 <  10 || pm10 >    0 && pm10 <   20) { eyes.setPixelColor(0, eyes.Color(80,240,230)); }// Good
    eyes.show();
    //

    while (millis() - loop_ts < 60000L) { delay(1000); } // sleep until full minute
    loops++;    

    ////////////////////////////////////
    //if (loops == 3) loops = 0; 
    ////////////////////////////////////

    eyes.setPixelColor(0, eyes.Color(255,255,255)); eyes.show(); delay(250); eyes.clear(); eyes.show();
}