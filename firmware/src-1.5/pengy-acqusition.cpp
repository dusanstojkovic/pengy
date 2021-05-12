/*
  Pengy v1.5 - External Air Quality Parameters acquisition

  This firmware acquires:
	 - temperature, humidity and preassure from BME680 sensor
     - CO, NH3 and NO2 from MiCS-6814 sensor
     - sound level from LM386 enabled electret microphone (https://www.waveshare.com/sound-sensor.htm)
     
  then accumulates, agregates data and sends it via Wire to main module.

  Copyright (c) 2021 Dusan Stojkovic
*/

#include <Arduino.h>
#include <Streaming.h>

// 
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"
Adafruit_BME680 bme;

long temperatureCummulative=0;
long humidityCummulative=0;
long pressureCummulative = 0;
int countTHPCummulative=0;

float temperature;
float humidity;
float pressure;

//
unsigned long concetrationCOCummulative=0;
unsigned long concetrationNH3Cummulative=0;
unsigned long concetrationNO2Cummulative = 0;
unsigned int countConcetrationCummulative=0;

float concetrationCO;
float concetrationNH3;
float concetrationNO2;

//
double soundValueCummulative=0;
int countSoundValueCummulative = 0;
double soundValue=0;

//
#define DEBUG true
#define LOG if(DEBUG)Serial

unsigned int loops = 0;
uint8_t data[16];

//
bool hNoise = false, hTHP = false, hGas = false;
bool rNoise = false, rTHP = false, rGas = false;

void wait(unsigned long d)
{
    unsigned long b_ = micros(); 
    unsigned long e_ = d; 

    while (e_ > 0) {
        yield();
        while ( e_ > 0 && (micros() - b_) >= 1000) {
            e_--;
            b_ += 1000;
        }
    }
}

void handleNoise()
{
    // LED on
    digitalWrite(LED_PIN, HIGH);

    unsigned long start_ts = millis();
    while (millis() - start_ts < 7500)
    {
        double soundValMin=3.4028235E+38, soundValSubMin=3.4028235E+38, soundValMax=-3.4028235E+38, soundValSubMax= -3.4028235E+38;

        unsigned long sound_ts= millis();
        while (millis() - sound_ts < 125) // 125ms
        {
            int soundVal = 0; for (int i=0; i<3; i++) soundVal += analogRead(A0); soundVal/=4;

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

    // LED off
    digitalWrite(LED_PIN, LOW);      

    LOG << F("NOISE - ") << F("N=") << soundValue << endl;
  
}

void handleTHP()
{
    // LED on
    digitalWrite(LED_PIN, HIGH);

    float temperatureMin=3.4028235E+38, temperatureMax=-3.4028235E+38;
    float humidityMin=3.4028235E+38, humidityMax=-3.4028235E+38;
    float pressureMin=3.4028235E+38, pressureMax=-3.4028235E+38;
    int thpCount = 0;
    
    for (int l=0; l<7; l++)
    {
        bool ok = bme.performReading();
        if (ok)
        {
            LOG << F(" (") << (l+1) << F(")   t=") << bme.temperature << F("°C   H=") << bme.humidity << F("%RH   p=") << bme.pressure << F(" hPa") << endl;

            temperatureCummulative += bme.temperature;
            if (bme.temperature < temperatureMin) temperatureMin = bme.temperature;
            if (bme.temperature > temperatureMax) temperatureMax = bme.temperature;                

            humidityCummulative += bme.humidity;
            if (bme.humidity < humidityMin) humidityMin = bme.humidity;
            if (bme.humidity > humidityMax) humidityMax = bme.humidity;                

            pressureCummulative += bme.pressure;
            if (bme.pressure < pressureMin) pressureMin = bme.pressure;
            if (bme.pressure > pressureMax) pressureMax = bme.pressure;   

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

    // LED off
    digitalWrite(LED_PIN, LOW);

    LOG << F("THP - ") << F("t=") << temperature << F("°C - H=") << humidity<< F("%RH - p=") << pressure << F(" hPa") << endl;
}

void handleGas()        
{
    // LED on
    digitalWrite(LED_PIN, HIGH);

    int concetrationCOMin=32767, concetrationCOMax=-32768;
    int concetrationNH3Min=32767, concetrationNH3Max=-32768;
    int concetrationNO2Min=32767, concetrationNO2Max=-32768;
    int concetrationCount = 0;

    for (int l=0; l<7; l++)
    {
        int valNO2 = 0; for (int i=0; i<3; i++) valNO2 += analogRead(A1); valNO2/=4; // valNO2 = aNO2 * valNO2 + bN02
        int valNH3 = 0; for (int i=0; i<3; i++) valNH3 += analogRead(A2); valNH3/=4;
        int valCO = 0; for (int i=0; i<3; i++) valCO += analogRead(A3); valCO/=4;

        bool ok = true;
        if (ok)
        {
            LOG << F(" (") << (l+1) << F(")   CO=") << valCO << F("  NH3=") << valNH3<< F("   NO2=") << valNO2 << endl;            

            concetrationCOCummulative += valCO;
            if (valCO < concetrationCOMin) concetrationCOMin = valCO;
            if (valCO > concetrationCOMax) concetrationCOMax = valCO;                

            concetrationNH3Cummulative += valNH3;
            if (valNH3 < concetrationNH3Min) concetrationNH3Min = valNH3;
            if (valNH3 > concetrationNH3Max) concetrationNH3Max = valNH3;                

            concetrationNO2Cummulative += valNO2;
            if (valNO2 < concetrationNO2Min) concetrationNO2Min = valNO2;
            if (valNO2 > concetrationNO2Max) concetrationNO2Max = valNO2;   

            concetrationCount++;
            countConcetrationCummulative++;
        }
        else
        {
            LOG << F(" (") << (l+1) << F(") X") << endl;
        }
        wait(1000);
    }

    LOG << F(" CO min=") << concetrationCOMin << F(" max=") << concetrationCOMax << endl;
    LOG << F(" NH3 min=") << concetrationNH3Min << F(" max=") << concetrationNH3Max << endl;
    LOG << F(" NO2 min=") << concetrationNO2Min << F(" max=") << concetrationNO2Max << endl;
    
    if (concetrationCount > 2)
    {
        concetrationCOCummulative = concetrationCOCummulative - concetrationCOMax - concetrationCOMin;
        concetrationNH3Cummulative = concetrationNH3Cummulative - concetrationNH3Max - concetrationNH3Min;
        concetrationNO2Cummulative = concetrationNO2Cummulative - concetrationNO2Max - concetrationNO2Min;
        countConcetrationCummulative = countConcetrationCummulative - 2;
    }
    
    concetrationCO = 1.0 * concetrationCOCummulative / countConcetrationCummulative;
    concetrationNH3 = 1.0 * concetrationNH3Cummulative / countConcetrationCummulative;
    concetrationNO2 = 1.0 * concetrationNO2Cummulative / countConcetrationCummulative;

    // LED off
    digitalWrite(LED_PIN, LOW);

    LOG << F("Gas - ") << F("CO=") << concetrationCO << F(" - NH3=") << concetrationNH3<< F(" - NO2=") << concetrationNO2 << endl;
}

void wireReceived(int bytes) 
{
    LOG << F("Wire Rec [") << millis()<< F("] / ") << bytes << F(" bytes");
    if (bytes == 1) 
    {
        char action = Wire.read();
        LOG << " - " << action;
        if (action == 'n') hNoise = true;
        if (action == 'a') hTHP = true;
        if (action == 'g') hGas = true;
        if (action == 'N') rNoise = true;
        if (action == 'A') rTHP = true;
        if (action == 'G') rGas = true;
    }
    LOG << endl << endl;
}

void wireRequested(void)
{
    if (rNoise) 
    { 
        rNoise=false;

        unsigned long loop_ts = millis();        
        LOG << F("[") << loop_ts << F("] Request Noise ");
        
        LOG << F("<") << soundValue << F(">");

        countSoundValueCummulative = 0;
        soundValueCummulative = 0;
        
        uint16_t datum;
        Wire.write('N');
        datum = soundValue * 100.0;
        Wire.write(( datum >> 8 ) & 0xFF);
        Wire.write(datum & 0xFF);        
        Wire.write("    ");
        
        LOG << F(" ~") << _FLOAT(0.001*(millis()-loop_ts),2) << F("s") << endl << endl;
        return;
    }

    if (rTHP) 
    { 
        rTHP=false; 

        unsigned long loop_ts = millis();        
        LOG << F("[") << loop_ts << F("] Request THP ");

        LOG << F("<") << temperature << F(",") << humidity << F(",") << pressure << F(">");
        
        countTHPCummulative=0;
        temperatureCummulative=0;
        humidityCummulative=0;
        pressureCummulative=0;

        uint16_t datum;
        Wire.write('A');
        datum = humidity * 10;
        Wire.write(( datum >> 8 ) & 0xFF);
        Wire.write(datum & 0xFF);
        datum = temperature * 10;
        Wire.write(( datum >> 8 ) & 0xFF);
        Wire.write(datum & 0xFF);
        datum = pressure;
        Wire.write(( datum >> 8 ) & 0xFF);
        Wire.write(datum & 0xFF);
        
        LOG << F(" ~") << _FLOAT(0.001*(millis()-loop_ts),2) << F("s") << endl << endl;
        return;
    }

    if (rGas)
    {
        rGas=false;

        unsigned long loop_ts = millis();
        LOG << F("[") << loop_ts << F("] Request Gas ");

        LOG << F("<") << concetrationCO << F(",") << concetrationNH3 << F(",") << concetrationNO2 << F(">");

        countConcetrationCummulative=0;
        concetrationCOCummulative=0;
        concetrationNH3Cummulative=0;
        concetrationNO2Cummulative=0;

        uint16_t datum;
        Wire.write('G');
        datum = concetrationCO * 1;
        Wire.write(( datum >> 8 ) & 0xFF);
        Wire.write(datum & 0xFF);
        datum = concetrationNH3 * 1;
        Wire.write(( datum >> 8 ) & 0xFF);
        Wire.write(datum & 0xFF);
        datum = concetrationNO2 * 100;
        Wire.write(( datum >> 8 ) & 0xFF);
        Wire.write(datum & 0xFF);

        LOG << F(" ~") << _FLOAT(0.001*(millis()-loop_ts),2) << F("s") << endl << endl;
        return;
    }
}



void setup() 
{
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    
    while (!Serial && millis() < 10000);
    Serial.begin(115200);
    delay(100);

    LOG << endl << endl  << F("ACQ") << " - " << F(__DATE__) << " " << F(__TIME__) << endl << "#" << endl << endl;

    // init THP sensor
    LOG << F("Setup THP sensor") << endl;
    bme.begin();
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    // don't measure gas VOC
    bme.setGasHeater(0, 0);
    // do measure gas VOC 
    //bme.setGasHeater(320, 150); // 320*C for 150 ms    
   

    // init gas sensor
    LOG << F("Setup Gas sensor") << endl;
    pinMode(A1, INPUT_PULLUP);
    pinMode(A2, INPUT_PULLUP);
    pinMode(A3, INPUT_PULLUP);

    // init sound sensor
    LOG << F("Setup Sound sensor") << endl;    
    pinMode(A0, INPUT);

    // init Wire
    Wire.begin(13);
    Wire.onReceive(wireReceived);
    Wire.onRequest(wireRequested);    
    
    digitalWrite(LED_PIN, LOW);

    LOG << endl << "#" << endl << endl;
}

void loop()
{
    if (hNoise) 
    { 
        hNoise=false;

        unsigned long loop_ts = millis();
        LOG << F("[") << loop_ts << F("] Handle Noise") << endl;        

        handleNoise();
        
        LOG << F(" ~ ") << _FLOAT(0.001*(millis()-loop_ts),2) << F("s") << endl << endl;        
    }

    if (hTHP) 
    { 
        hTHP=false;

        unsigned long loop_ts = millis();
        LOG << F("[") << loop_ts << F("] Handle THP") << endl;

        handleTHP();

        LOG << F(" ~ ") << _FLOAT(0.001*(millis()-loop_ts),2) << F("s") << endl << endl;        
    }

    if (hGas) 
    {
        hGas=false; 

        unsigned long loop_ts = millis();
        LOG << F("[") << loop_ts << F("] Handle Gas") << endl;

        handleGas();

        LOG << F(" ~ ") << _FLOAT(0.001*(millis()-loop_ts),2) << F("s") << endl << endl;
    }
}