/*
  Pengy v1.0 - Air Quality Parameters acquisition

  This firmware reads from PM2.5 and PM10 concetration from SDS021 sensor, reads
  temperature and humidity from AM2302 sensor; accumulates, agregates data; 
  sends it every hour using LoRaWAN via The Things Network to Thingy.IO system.

  Copyright (c) 2019 Dusan Stojkovic
*/

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <Streaming.h>

#include <SdsDustSensor.h>
#include <DHT.h>

#define DEBUG true
#define LOG if(DEBUG)Serial

SdsDustSensor sds(Serial1);
DHT dht(A5, DHT22);

#define _NAME_ "Pengy-???????"

static const uint8_t PROGMEM DEVEUI[8]= { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }; // LSB
static const uint8_t PROGMEM APPEUI[8]= { 0x12, 0x5E, 0x01, 0xD0, 0x7E, 0xD5, 0xB3, 0x70 }; // LSB
static const uint8_t PROGMEM APPKEY[16] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }; // MSB

void os_getArtEui (u1_t* buf) { memcpy_P(buf, APPEUI, 8);}
void os_getDevEui (u1_t* buf) { memcpy_P(buf, DEVEUI, 8);}
void os_getDevKey (u1_t* buf) {  memcpy_P(buf, APPKEY, 16);}

const lmic_pinmap lmic_pins = 
{
    .nss = LORA_SPI_SS,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = LORA_RST,
    .dio = {LORA_DIO0, LORA_DIO1, LMIC_UNUSED_PIN},
};

bool joined = false;
unsigned int loops = 0;
uint8_t data[8];

void onEvent (ev_t ev) 
{ 
    LOG << F("[") << millis() << F("] LoRa - ");
    switch(ev) {
        case EV_JOINING:
            LOG << F("JOINING") << endl;
            break;
        case EV_JOINED:
            LOG << F("JOINED") << endl;
            joined = true;
            LMIC_setLinkCheckMode(0);
            break;
        case EV_TXCOMPLETE:
            LOG << F("TXCOMPLETE") << endl;
            break;            
         default:
            LOG << ev << endl;
            break;
    }
}

void setup() 
{
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
  
    while (!Serial && millis() < 10000);
    Serial.begin(115200);

    delay(10);
    LOG << endl << endl  << F(_NAME_) << " - " << F(__DATE__) << " " << F(__TIME__) << endl << "#" << endl << endl;

    // init air quiality sensor
    sds.begin();
    sds.setQueryReportingMode();
    
    // init temperature and humidity sensor
    dht.begin();

    // Init LoRa
    os_init();
    LMIC_reset();
    // relax timing - required for ATmega328/ATmega32U4 (8MHz) otherwise OTAA joins on SF7 and SF8 will likely fail.    
    LMIC_setClockError(MAX_CLOCK_ERROR * 1 / 100);
    LMIC_startJoining();

    digitalWrite(LED_PIN, LOW);
}

double humidityCummulative=0;
double temperatureCummulative=0;
int countHTCummulative=0;

double fpmCummulative=0;
double rpmCummulative=0;
int countAQCummulative=0;

double humidity;
double temperature;
double fpm;
double rpm;

void wait(unsigned long d)
{
    unsigned long b_ = micros(); 
    unsigned long e_ = d; 

    while (e_ > 0) {
        yield();
        os_runloop_once();
        while ( e_ > 0 && (micros() - b_) >= 1000) {
            e_--;
            b_ += 1000;
        }
    }
}

void loop()
{
    if (!joined)
    {
        digitalWrite(LED_PIN,((millis()/500) % 2));
        wait(250);
        return;
    }

    unsigned long loop_ts = millis();

    LOG << F("[") << loop_ts << F("] ") << loops << endl;

    if (loops % 5 == 0) // every 5 minutes
    {
        LOG << F("HT ");
        // LED on
        digitalWrite(LED_PIN, HIGH);

        for (int l=0; l<5; l++)
        {
            bool ok = dht.read(true);
            if (ok)
            {
                //LOG << F("RH=") << dht.readHumidity() << F(" t=") << dht.readTemperature() << endl;

                humidityCummulative += dht.readHumidity();;
                temperatureCummulative += dht.readTemperature();;

                countHTCummulative++;
                humidity = 1.0 * humidityCummulative / countHTCummulative; // 1.5 
                temperature = 1.0 * temperatureCummulative / countHTCummulative;
            }
            else
            {
                //LOG << F("ERROR") << endl;
            }
            wait(2000);
        }
        // LED off
        digitalWrite(LED_PIN, LOW);        
    }
    
    if (loops % 10 == 0) // every 10 minutes
    {
        LOG << F("AQ ");
        
        // LED on
        digitalWrite(LED_PIN, HIGH);

        sds.wakeup();
        wait(30000);

        for (int l=0; l<5; l++)
        {
            PmResult pm = sds.queryPm();
            if (pm.isOk()) 
            {
                //LOG << F("fPM=") << pm.pm25 << F(" rPM=") << pm.pm10 << endl;

                fpmCummulative += pm.pm25;
                rpmCummulative += pm.pm10;

                countAQCummulative++;
                fpm = 1.0 * fpmCummulative / countAQCummulative; // 0.7
                rpm = 1.0 * rpmCummulative / countAQCummulative; // 0.7
            } else 
            {
                //LOG << F("ERROR=") << pm.statusToString() << endl;
            }
            wait(1000);
        }            
        WorkingStateResult state = sds.sleep();
        /*
        if (state.isWorking()) 
        {
            LOG << F("ERROR = Problem with sleeping the sensor.") << endl;
        }
        */
        // LED off
        digitalWrite(LED_PIN, LOW);
    }
    
    LOG << endl << F("RH=") << humidity << F(" t=") << temperature << F(" fPM=") << fpm << F(" rPM=") << rpm << endl;

    if (loops % 20 == 1) // every 20 minutes
    {
        countHTCummulative=0;
        humidityCummulative=0;
        temperatureCummulative=0;

        countAQCummulative=0;
        fpmCummulative=0;
        rpmCummulative=0;

        if (true)
        {
            // LED on
            digitalWrite(LED_PIN, HIGH);

            uint16_t hum = humidity * 10;      
            data[0] = ( hum >> 8 ) & 0xFF;
            data[1] = hum & 0xFF;

            uint16_t tem = temperature * 10;
            data[2] = ( tem >> 8 ) & 0xFF;
            data[3] = tem & 0xFF;

            // PM 10
            uint16_t pm10 = rpm * 10;
            data[4] = ( pm10 >> 8 ) & 0xFF;
            data[5] = pm10 & 0xFF;
            
            // PM 2.4
            uint16_t pm25 = fpm * 10;
            data[6] = ( pm25 >> 8 ) & 0xFF;
            data[7] = pm25 & 0xFF;
            
            // transmit
            LOG << F("Sending [") << millis() << F("] - ");
            int res =  LMIC_setTxData2(1, data, sizeof(data), 0);
            LOG << ((res == 0) ? F("OK") : F("ERR")) << F(" @ ") << LMIC.txChnl << endl;;

            while ( (LMIC.opmode & OP_JOINING) or (LMIC.opmode & OP_TXRXPEND) ) { os_runloop_once();  }

            // LED off
            digitalWrite(LED_PIN, LOW);
        }
        else
        {
            /*
            //Serial << F("[") << millis() << F("] - X");

            uint16_t now = millis()/1000;
            data[0] = ( now >> 8 ) & 0xFF;
            data[1] = now & 0xFF;

            // transmit
            //Serial << F(" - ");
            int res =  LMIC_setTxData2(2, data, 2, 0);
            //Serial << ((res == 0) ? F("OK") : F("ERR")) << F(" ") << LMIC.txChnl;

            while ( (LMIC.opmode & OP_JOINING) or (LMIC.opmode & OP_TXRXPEND) ) { os_runloop_once();  }
            //Serial << endl;
            */
        }
    }
    LOG << F("(") << _FLOAT(0.001*(millis()-loop_ts),2) << F(")") << endl << endl;  

    while (millis() - loop_ts < 60000L) { wait(1000);} // sleep until full minute
    loops++;
}
