/*
  Pengy v1.5 - Air Quality Parameters acquisition

  This firmware acquires:
     - PM2.5 and PM10 concetration from SDS011 sensor
	 - [external pengy-acqusition.cpp] temperature, humidity and preassure from BME680 sensor
     - [external pengy-acqusition.cpp] CO, NH3 and NO2 from MiCS-6814 sensor
     - [external pengy-acqusition.cpp] sound level from LM386 enabled electret microphone (https://www.waveshare.com/sound-sensor.htm)
     
  then accumulates, agregates data and sends it every hour using LoRaWAN 
  via The Things Network to Thingy.IO system.

  Copyright (c) 2021 Dusan Stojkovic
*/

#include <Wire.h>
#include <SPI.h>
#include <lmic.h>
#include <hal/hal.h>
#include <Streaming.h>

#include <SdsDustSensor.h>
SoftwareSerial sdss(0,1);
SdsDustSensor sds(Serial1);

double fpmCummulative=0;
double rpmCummulative=0;
int countAQCummulative=0;

float fpm;
float rpm;

float noise;

float concetrationCO;
float concetrationNH3;
float concetrationNO2;

float temperature;
float humidity;
float pressure;

#include <pengy-acqusition.hpp>

//
#define DEBUG true
#define LOG if(DEBUG)Serial

#ifdef _NAME_
static const uint8_t NWKSKEY[16] = NWK_S_KEY;
static const uint8_t APPSKEY[16] = APP_S_KEY;
static const uint32_t DEVADDR = DEV_ADDR;
#elif
#define _NAME_ "Pengy-???"
static const uint8_t NWKSKEY[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t APPSKEY[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint32_t DEVADDR = 0x00000000;
#endif

void os_getArtEui (u1_t* buf) { }
void os_getDevEui (u1_t* buf) { }
void os_getDevKey (u1_t* buf) { }

const lmic_pinmap lmic_pins = 
{
    .nss = LORA_SPI_SS,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = LORA_RST,
    .dio = {LORA_DIO0, LORA_DIO1, LMIC_UNUSED_PIN},
};

unsigned int loops = 0;
uint8_t data[18];

void wait(unsigned long d)
{
    unsigned long b_ = micros();
    unsigned long e_ = d;

    while (e_ > 0)
    {
        yield();
        while ( e_ > 0 && (micros() - b_) >= 1000)
        {
            e_--;
            b_ += 1000;
        }
    }
}

void onEvent (ev_t ev)
{ 
    LOG << F("[") << millis() << F("] LoRa - ") << ev << endl; 
}

void handlePM()
{
    LOG << F("PM") << endl;
    
    // LED on
    digitalWrite(LED_PIN, HIGH);

    //WorkingStateResult sdsStarting = 
    sds.wakeup();
    //LOG << F("  waking up - ") << sdsStarting.statusToString() << endl;

    wait(30000);

    float fpmMin=3.4028235E+38, fpmMax=-3.4028235E+38;
    float rpmMin=3.4028235E+38, rpmMax=-3.4028235E+38;
    int pmCount = 0;

    for (int l=0; l<7; l++)
    {
        PmResult pm = sds.queryPm();
        if (pm.isOk()) 
        {
            //LOG << F(" (") << (l+1) << F(")   fPM=") << pm.pm25 << F("   rPM=") << pm.pm10 << endl;

            fpmCummulative += pm.pm25;
            if (pm.pm25 < fpmMin) fpmMin = pm.pm25;
            if (pm.pm25 > fpmMax) fpmMax = pm.pm25;
            
            rpmCummulative += pm.pm10;
            if (pm.pm10 < rpmMin) rpmMin = pm.pm10;
            if (pm.pm10 > rpmMax) rpmMax = pm.pm10;                

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
    //LOG << "rpm m " << rpmMin << " M " << rpmMax << endl;

    if (pmCount > 2)
    {
        fpmCummulative = fpmCummulative - fpmMax - fpmMin;
        rpmCummulative = rpmCummulative - rpmMax - rpmMin;
        countAQCummulative = countAQCummulative - 2;
    }
    
    fpm = 1.0 * fpmCummulative / countAQCummulative; // 0.7
    rpm = 1.0 * rpmCummulative / countAQCummulative; // 0.7
    
    //WorkingStateResult sdsSleeping = 
    sds.sleep();
    //LOG << F("   sleeping - ") << (sdsSleeping.statusToString()) << endl;
    
    // LED off
    digitalWrite(LED_PIN, LOW);
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

void normalizeGas()
{
    concetrationCO  = CAL_GAS_CO_1  * pow(concetrationCO,  CAL_GAS_CO_0);
    concetrationNH3 = CAL_GAS_NH3_1 * pow(concetrationNH3, CAL_GAS_NH3_0);
    concetrationNO2 = CAL_GAS_NO2_1 * pow(concetrationNO2, CAL_GAS_NO2_0);
}

void normalizePM()
{
    rpm = (CAL_RPM_2 * rpm + CAL_RPM_1) * rpm + CAL_RPM_0;
    fpm = (CAL_FPM_2 * fpm + CAL_FPM_1) * fpm + CAL_FPM_0;
}


// Setup & Loop
void setup()
{
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
  
    while (!Serial && millis() < 10000);
    Serial.begin(115200);

    delay(10);
    LOG << endl << endl  << F(_NAME_) << " - " << F(__DATE__) << " " << F(__TIME__) << endl << "#" << endl << endl;

    // init air quiality sensor
    //LOG << F("Setup Air Quality sensor") << endl;
    sds.begin();
    //ReportingModeResult modeResult = 
    sds.setQueryReportingMode();
    //LOG << F("   Querying : ") << modeResult.statusToString() << endl;

    /*
    LOG << F("   mode     : ") << sds.queryReportingMode().statusToString() << endl;
    LOG << F("   firmware : ") << sds.queryFirmwareVersion().statusToString() << endl;
    LOG << F("   state    : ") << sds.queryWorkingState().statusToString() << endl;
    LOG << F("   querying : ") << sds.setQueryReportingMode().statusToString() << endl;
    */
        
    // init wire
    wireSetup();

    // Init LoRa
    os_init();
    LMIC_reset();

    LMIC_setSession (0x1, DEVADDR, (uint8_t*)NWKSKEY, (uint8_t*)APPSKEY);

    LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF7B), BAND_CENTI);      // g-band
    LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(DR_FSK,  DR_FSK),  BAND_MILLI);      // g2-band

    LMIC_setLinkCheckMode(0);

    LMIC.dn2Dr = DR_SF9;
    LMIC_setDrTxpow(DR_SF7,14);

    digitalWrite(LED_PIN, LOW);
}

void loop()
{
    os_runloop_once();

    unsigned long loop_ts = millis();

    LOG << F("[") << loop_ts << F(" millis / ") << loops <<  F(" loops] ") << endl;

    if (loops % 20 == 1) // every 20 minutes - request readout
    //if (loops % 10 == 1) // Daniel
    {
        // Ambiental noise
        readNoise();
        normalizeNoise();
        LOG << F("NOISE ") << F("v=") << noise << F("dBA") << endl;

        // Temperature, Humidity Pressure
        readTHP();
        normalizeTHP();
        LOG << F("THP ") << F("t=") << temperature << F("°C - H=") << humidity<< F("%RH - p=") << pressure << F(" hPa") << endl;
        
        // Gasses
        readGas();
        normalizeGas();
        LOG << F("Gas ") << F("CO=") << concetrationCO << F(" - NH3=") << concetrationNH3<< F(" - NO2=") << concetrationNO2 << endl;
        
        // Particulate metter
        countAQCummulative=0;
        fpmCummulative=0;
        rpmCummulative=0;

        normalizePM();
        LOG << F("PM ") << F(" fPM=") << fpm << F(" rPM=") << rpm << endl;     
    }

    if (loops % 5 == 0) // every 5 minutes - sample THP
        handleTHP();

    if (loops % 6 == 0) // every 6 minutes - sample gass
        handleGas();

    if (loops % 5 == 0) // every 10 minutes - sample particulates
        handlePM();

    if (loops % 1 == 0) // every minute - sample noise
        handleNoise();

    if (loops % 20 == 2) // every 20 minutes - send
    //if (loops % 10 == 2) // Daniel
    {
        uint16_t datum;
		// port 1 - Humidity (0,1), Temperature (2,3), PM10 (4,5), PM25 (6,7)
		// port 2 - Humidity (0,1), Temperature (2,3), PM10 (4,5), PM25 (6,7), 
        //          Pressure (8,9), 
        //          CO (10,11), NH3 (12,13), N02 (14,15), 
        //          Noise (16,17)
        
        // Humidity
        datum = humidity * 10;
        data[0] = ( datum >> 8 ) & 0xFF; data[1] = datum & 0xFF;
        // Temperature
        datum = temperature * 10;
        data[2] = ( datum >> 8 ) & 0xFF; data[3] = datum & 0xFF;
        // Pressure
        datum = pressure;
        data[8] = ( datum >> 8 ) & 0xFF; data[9] = datum & 0xFF;

        // PM 10
        datum = rpm * 10;
        data[4] = ( datum >> 8 ) & 0xFF; data[5] = datum & 0xFF;
        // PM 2.5
        datum = fpm * 10;
        data[6] = ( datum >> 8 ) & 0xFF; data[7] = datum & 0xFF;

        // CO
        datum = concetrationCO * 1;
        data[10] = ( datum >> 8 ) & 0xFF; data[11] = datum & 0xFF;
        // NH3
        datum = concetrationNH3 * 1;
        data[12] = ( datum >> 8 ) & 0xFF; data[13] = datum & 0xFF;
        // NO2
        datum = concetrationNO2 * 100;
        data[14] = ( datum >> 8 ) & 0xFF; data[15] = datum & 0xFF;

        // Noise
        datum = noise * 100;
        data[16] = ( datum >> 8 ) & 0xFF; data[17] = datum & 0xFF;

        // LED on
        digitalWrite(LED_PIN, HIGH);

        // transmit
        LOG << F("Sending[") << millis() << F("]-");
        int res =  LMIC_setTxData2(2, data, sizeof(data), 0);
        LOG << ((res == 0) ? F("OK") : F("NOK")) << F(" @") << LMIC.txChnl << endl;;

        while ( LMIC.opmode & OP_TXRXPEND ) { os_runloop_once();  }
        
        // LED off
        digitalWrite(LED_PIN, LOW);
    }

    LOG << F("[") << (0.001*(millis()-loop_ts)) << F(" s]") << endl << endl << endl << endl;  

    while (millis() - loop_ts < 60000L) { wait(1000);} // sleep until full minute
    loops++;    

    ////////////////////////////////////
    //if (loops == 3) loops = 0; 
    ////////////////////////////////////
}