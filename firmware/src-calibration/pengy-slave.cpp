#include <LoRa.h>
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

#include "pengy-acqusition.hpp"

//
#define DEBUG true
#define LOG if(DEBUG)Serial

unsigned int loops = 0;
uint8_t data[20];

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

void handlePM()
{
    LOG << F("PM") << endl;
    
    // LED on
    digitalWrite(LED_PIN, HIGH);

    WorkingStateResult sdsStarting = sds.wakeup();
    LOG << F("  waking up - ") << sdsStarting.statusToString() << endl;

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
        else 
        {
            LOG << F(" (") << (l+1) << F(") X   ") << pm.statusToString() << endl;
        }
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
    
    WorkingStateResult sdsSleeping = sds.sleep();
    LOG << F("   sleeping - ") << (sdsSleeping.statusToString()) << endl;
    
    // LED off
    digitalWrite(LED_PIN, LOW);
}

    

void normalizeNoise()
{
    //noise = 11.3894 * log(noise) + 7.3404;
    noise = 1.0 * log(noise) + 0.0;
}

void normalizeTHP()
{
    temperature = 1.0 * temperature + 0.0;
    humidity    = 1.0 * humidity + 0.0;
    pressure    = 1.0 * pressure + 0.0;
}
    
void normalizeGas()
{
    concetrationCO  = 1.0 * concetrationCO + 0.0;
    concetrationNH3 = 1.0 * concetrationNH3 + 0.0;
    concetrationNO2 = 1.0 * concetrationNO2 + 0.0;
}

void normalizePM()
{
    rpm = 1.0 * rpm + 0.0;
    fpm    = 1.0 * fpm + 0.0;
    pressure    = 1.0 * pressure + 0.0;
}

bool readData= false, handleData = false;

void onReceived(int packetSize) 
{
    if (packetSize == 3)
    {
        uint8_t data[3];  
        LoRa.readBytes(data, 3);

        if (data[0] == 0 && data[1] == NODE_ID)
        {
            if (data[2] == 'R')
                readData = true;
            if (data[2] == 'H')
                handleData = true;
        }
        return;
   }

    LOG << "Received (" << packetSize << " bytes) - Ignored" << endl;
    for (int i=0; i<packetSize; i++) LoRa.read();
}


void setup()
{
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
  
    while (!Serial && millis() < 10000);
    Serial.begin(115200);

    delay(10);
    LOG << endl << endl  << F("SLAVE ") << NODE_ID << " - " << F(__DATE__) << " " << F(__TIME__) << endl << "#" << endl << endl;

    // init air quiality sensor
    LOG << F("Setup Air Quality sensor") << endl;
    sds.begin();
    ReportingModeResult modeResult = sds.setQueryReportingMode();
    LOG << F("   Querying : ") << modeResult.statusToString() << endl;

    /*
    LOG << F("   mode     : ") << sds.queryReportingMode().statusToString() << endl;
    LOG << F("   firmware : ") << sds.queryFirmwareVersion().statusToString() << endl;
    LOG << F("   state    : ") << sds.queryWorkingState().statusToString() << endl;
    LOG << F("   querying : ") << sds.setQueryReportingMode().statusToString() << endl;
    */
        
    // init wire
    wireSetup();

    // Init LoRa
    Serial << F("Setting LoRa         : ");
    LoRa.setPins(8, 4, 7);
    LoRa.setSyncWord(0x83);
    bool loring = LoRa.begin(866E6);
    Serial << (loring ? F("✔") : F("✖")) << endl;

    LoRa.onReceive(onReceived);
    LoRa.receive();
    LOG << F("Listening [") << millis() << F("] ...") << endl;

    digitalWrite(LED_PIN, LOW);
}



void loop()
{
    if (handleData)
    {
        handleData = false;
        LOG << F("Data handling [") << millis() << F("] ...") << endl;

        handleNoise();
        handleTHP();
        handleGas();
        handlePM();
    }

    if (readData)
    {
        readData = false;
        LOG << F("Data reading [") << millis() << F("] ...") << endl;

        readNoise();
        normalizeNoise();
        LOG << F("NOISE - ") << F("v=") << noise << F("dBA") << endl;

        readTHP();
        normalizeTHP();
        LOG << F("THP - ") << F("t=") << temperature << F("°C - H=") << humidity<< F("%RH - p=") << pressure << F(" hPa") << endl;
        
        readGas();
        normalizeGas();
        LOG << F("Gas - ") << F("CO=") << concetrationCO << F(" - NH3=") << concetrationNH3<< F(" - NO2=") << concetrationNO2 << endl;
        
        normalizePM();
        LOG << F("PM - ") << F(" fPM=") << fpm << F(" rPM=") << rpm << endl;

        uint16_t datum;
		// port 1 - Humidity (0,1), Temperature (2,3), PM10 (4,5), PM25 (6,7)
		// port 2 - Humidity (0,1), Temperature (2,3), PM10 (4,5), PM25 (6,7), 
        //          Pressure (8,9), 
        //          CO (10,11), NH3 (12,13), N02 (14,15), 
        //          Noise (16,17)

        // port 3 - 
        countAQCummulative=0;
        fpmCummulative=0;
        rpmCummulative=0;

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

        data[18] = NODE_ID;

        // transmit
        unsigned long send_ts = millis();
        LOG << F("Sending [") << send_ts << F("] - ");

        LoRa.beginPacket();
        LoRa.write((uint8_t*) data, 19);
        LoRa.endPacket(false);

        LOG << F(" ~") << _FLOAT(0.001*(millis()-send_ts),2) << F("s") << endl << endl;

        LoRa.receive();
        LOG << F("Listening [") << millis() << F("] ...") << endl;

    }
}