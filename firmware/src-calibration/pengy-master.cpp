#include <LoRa.h>

#include <SPS30.h>
#include "MutichannelGasSensor.h"

#include <Streaming.h>
#define DEBUG false
#define LOG if(DEBUG)Serial

double fpmCummulative=0;
double rpmCummulative=0;
int countAQCummulative=0;

float fpm;
float rpm;

SPS30 sps30;

//
unsigned long concetrationCOCummulative=0;
unsigned long concetrationNH3Cummulative=0;
unsigned long concetrationNO2Cummulative = 0;
unsigned int countConcetrationCummulative=0;

float concetrationCO;
float concetrationNH3;
float concetrationNO2;

unsigned int loops = 0;
uint8_t data[16];

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
        int valNO2 = 100 * gas.measure_NO2();
        int valNH3 = 100 * gas.measure_NH3();
        int valCO = 100 * gas.measure_CO();

        bool ok = true;
        if (ok)
        {
            LOG << F(" (") << (l+1) << F(")   CO=") << valCO << F("  NH3=") << valNH3<< F("   NO2=") << valNO2 
                    << F(" :   C2H5OH=") << gas.measure_C2H5OH() << F("   C3H8=") << gas.measure_C3H8() << F("   C4H10=") << gas.measure_C4H10() << F("   CH4=") << gas.measure_CH4() << F("   CO=") << gas.measure_CO() << F("   H2=") << gas.measure_H2() << F("   NH3=") << gas.measure_NH3() << F("   NO2=") << gas.measure_NO2() << endl;            

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
            //LOG << F(" (") << (l+1) << F(") X") << endl;
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
    
    concetrationCO = 0.01 * concetrationCOCummulative / countConcetrationCummulative;
    concetrationNH3 = 0.01 * concetrationNH3Cummulative / countConcetrationCummulative;
    concetrationNO2 = 0.01 * concetrationNO2Cummulative / countConcetrationCummulative;

    // LED off
    digitalWrite(LED_PIN, LOW);
}

void handlePM()
{
    LOG <<F("PM") << endl;
    
    // LED on
    digitalWrite(LED_PIN, HIGH);

    bool began = sps30.beginMeasuring();
    LOG << F("   began - ") << (began ? "Y":"N") << endl;
    wait(30000);

    float fpmMin=3.4028235E+38, fpmMax=-3.4028235E+38;
    float rpmMin=3.4028235E+38, rpmMax=-3.4028235E+38;
    int pmCount = 0;

    for (int l=0; l<7; l++)
    {
        if (sps30.readMeasurement())
        {
            LOG <<F(" (") << (l+1) << F(")   PM1.0=") << sps30.massPM1 << F("µg/m³  PM2.5=") << sps30.massPM25 << F("µg/m³  PM4.0=") << sps30.massPM4 << F("µg/m³  PM10=") << sps30.massPM10 << F("µg/m³  size=") << sps30.typPartSize << F("µm") << endl;
    
            fpmCummulative += sps30.massPM25;
            if (sps30.massPM25 < fpmMin) fpmMin = sps30.massPM25;
            if (sps30.massPM25 > fpmMax) fpmMax = sps30.massPM25;
            
            rpmCummulative += sps30.massPM10;
            if (sps30.massPM10 < rpmMin) rpmMin = sps30.massPM10;
            if (sps30.massPM10 > rpmMax) rpmMax = sps30.massPM10;                

            pmCount++;
            countAQCummulative++;
        } 
        else 
        {
           //LOG <<F(" (") << (l+1) << F(") X   ") << pm.statusToString() << endl;
        }
        wait(1000);
    }
    
    LOG << "fpm m " << fpmMin << " M " << fpmMax << endl;
    LOG << "rpm m " << rpmMin << " M " << rpmMax << endl;

    if (pmCount > 2)
    {
        fpmCummulative = fpmCummulative - fpmMax - fpmMin;
        rpmCummulative = rpmCummulative - rpmMax - rpmMin;
        countAQCummulative = countAQCummulative - 2;
    }
    
    fpm = 1.0 * fpmCummulative / countAQCummulative; // 0.7
    rpm = 1.0 * rpmCummulative / countAQCummulative; // 0.7
    
    bool stopped = sps30.stopMeasuring();
    LOG << F("   stopped - ") << (stopped ? "Y":"N") << endl;    
    
    // LED off
    digitalWrite(LED_PIN, LOW);
}

void onReceive(int packetSize) 
{
//    Serial << F("Received [") << millis()<< F("] / ") << packetSize << F(" bytes - ");

    if (packetSize != 19)
    {
        LOG << F("Received (") << packetSize << F(" bytes) - Ignored") << endl;
        for (int i=0; i<packetSize; i++) LoRa.read();
        return;
    }
    uint8_t data[19];  
    LoRa.readBytes(data, 19);
    
    //for (int i=0; i<17; i++) Serial  << " " << data[i];    Serial << endl;

    uint32_t node_id = data[18];
    
    float humidity = 0.1 * (data[0] << 8 | data[1]);
    float temperature = 0.1 * (data[2] << 8 | data[3]);
    float pressure = 1.0 * (data[8] << 8 | data[9]);
    float rpm = 0.1 * (data[4] << 8 | data[5]);
    float fpm = 0.1 * (data[6] << 8 | data[7]);
    float concetrationCO = 1.0 * (data[10] << 8 | data[11]);
    float concetrationNH3 = 1.0 * (data[12] << 8 | data[13]);
    float concetrationNO2 = 0.01 * (data[14] << 8 | data[15]);   
    float noise = 0.01 * (data[16] << 8 | data[17]);

    Serial << F("   ") << node_id << F(" ; ") 
            << temperature << F(";") << humidity << F(";") << pressure << F(";") 
            << fpm << F(";") << rpm << F(";")
            << concetrationCO << F(";") << concetrationNH3 << F(";") << concetrationNO2 << F(";")
            << noise << F("; ");
    
    LOG << F("Sensor ") << node_id << F(" :: ")
            << F("t=") << temperature << F("°C - H=") << humidity<< F("%RH - p=") << pressure << F(" hPa") << F(" : ")
            << F("fPM=") << fpm << F(" - rPM=") << rpm << F(" : ")
            << F("CO=") << concetrationCO << F(" - NH3=") << concetrationNH3<< F(" - NO2=") << concetrationNO2 << F(" : ")
            << F("N=") << noise << F("dBA") << endl;
}

void setup()
{
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
  
    while (!Serial && millis() < 10000);
    Serial.begin(115200);

    delay(10);
    Serial << endl << endl  << F("Pengy.MASTER") << " - " << F(__DATE__) << " " << F(__TIME__) << endl << "#" << endl << endl;

    // init air quiality sensor
    Serial << F("Setup Air Quality sensor") << endl;
    sps30.begin();

    // init gas sensor
    Serial << F("Setup Multichannel Gas sensor") << endl;
    gas.begin(0x04);

    gas.powerOn(); //#####

    float R0_NH3, R0_CO, R0_NO2;
    
    R0_NH3 = gas.getR0(0);
    R0_CO  = gas.getR0(1);
    R0_NO2 = gas.getR0(2);
    
    Serial << F("R0_NH3 = ") << R0_NH3 << endl << F("R0_CO = ") << R0_CO << endl << F("R0_NO2 = ") << R0_NO2 << endl;

    gas.display_eeprom();

    // Init LoRa
    Serial << F("  Setting LoRa         : ");
    LoRa.setPins(8, 4, 7);
    LoRa.setSyncWord(0x83);
    bool loring = LoRa.begin(866E6);
    Serial << (loring ? F("✔") : F("✖")) << endl;

    LoRa.onReceive(onReceive);
    LoRa.receive();
    Serial << F("Listening [") << millis() << F("] ...") << endl;
    digitalWrite(LED_PIN, LOW);
}

void loop()
{
    unsigned long loop_ts = millis();

    ////Serial <<F("[") << loop_ts << F(" millis / ") << loops <<  F(" loops] ") << endl;

    if (loops % 2 == 1)
    {
        
        Serial << endl << loop_ts << F("; ") << fpm << F(";") << rpm << F(";") 
            << concetrationCO << F(";") << concetrationNH3 << F(";") << concetrationNO2 << F("; ");
        
        LOG << F("Master   :: ")
            << F("fPM=") << fpm << F(" - rPM=") << rpm << F(" : ")
            << F("CO=") << concetrationCO << F("ppm - NH3=") << concetrationNH3<< F("ppm - NO2=") << concetrationNO2 << F("ppm")  << endl;

        //
        countAQCummulative=0;
        fpmCummulative=0;
        rpmCummulative=0;

        countConcetrationCummulative = 0;
        concetrationCOCummulative = 0;
        concetrationNH3Cummulative = 0;
        concetrationNO2Cummulative = 0;

        for (int i=1; i<=4; i++)
        {
            LoRa.beginPacket();
            LoRa.write(0); LoRa.write(i); LoRa.write('R');
            LoRa.endPacket(false);
            LoRa.receive();
            wait(1000);
        }

        ///Serial << endl;
    }

    if (loops % 2 == 0)
    {
        LOG << F("Acquisition ...") << endl;
        for (int i=1; i<=4; i++)
        {
            LoRa.beginPacket();
            LoRa.write(0); LoRa.write(i); LoRa.write('H');
            LoRa.endPacket(false);
        }
        LoRa.receive();

        handleGas();
        handlePM();
    }

    ////Serial <<F("[") << _FLOAT(0.001*(millis()-loop_ts),2) << F(" s]") << endl << endl << endl << endl;  

    while (millis() - loop_ts < 60000L) { wait(1000);} // sleep until full minute
    loops++;    

}