#include <Wire.h>
#include <SPI.h>
#include <Streaming.h>

#define DEBUG true
#define LOG if(DEBUG)Serial

uint8_t wireData[7];

void wireSetup()
{
    Wire.begin();
}

void wireReceived(int bytes)
{
    //LOG << F("wireReceived [") << bytes << F("]  - < ");
    if (bytes == 7) 
    {
        Wire.readBytes(wireData, 7);
        //for (int l=0;l<bytes;l++) LOG << _DEC(wireData[l]) << " ";
        //LOG << ">" << endl;

        uint16_t val;

        if (wireData[0] == 'N')
        {
            val = wireData[1] << 8 | wireData[2];
            noise = 0.01 * val;
            //LOG << F("NOISE - ") << F("N=") << noise << F("dBA") << endl;            
        }
        if (wireData[0] == 'A')
        {
            val = wireData[1] << 8 | wireData[2];            
            humidity = 0.1 * val;
            val = wireData[3] << 8 | wireData[4];            
            temperature = 0.1 * val;
            val = wireData[5] << 8 | wireData[6];            
            pressure = 1.0 * val;
            //LOG << F("THP - ") << F("t=") << temperature << F("°C - H=") << humidity<< F("%RH - p=") << pressure << F(" hPa") << endl;            
        }
        if (wireData[0] == 'G')
        {
            val = wireData[1] << 8 | wireData[2];            
            concetrationCO = 1.0 * val;
            val = wireData[3] << 8 | wireData[4];            
            concetrationNH3 = 1.0 * val;
            val = wireData[5] << 8 | wireData[6];            
            concetrationNO2 = 0.01 * val;   
            //LOG << F("Gas - ") << F("CO=") << concetrationCO << F(" - NH3=") << concetrationNH3<< F(" - NO2=") << concetrationNO2 << endl;
        }
        //LOG << endl;
    }
}

void wireSend(char c)
{
    Wire.beginTransmission(13);
    Wire.write(c);
    Wire.endTransmission();    
}

void handleNoise()
{
    wireSend('n');
}

void handleTHP()
{
    wireSend('a');
}

void handleGas()
{
    wireSend('g');
}

void readNoise()
{
    wireSend('N');
    wireReceived(Wire.requestFrom(13,7));
}

void readTHP()
{
    wireSend('A');
    wireReceived(Wire.requestFrom(13,7));
}

void readGas()
{
    wireSend('G');
    wireReceived(Wire.requestFrom(13,7));
}