function Decoder(bytes, port) 
{
  var decoded = {};
      
  if (port == 1)
  {
    var hum = bytes[0]<<8 | bytes[1]; hum = 0.1 * hum; hum = Number(hum.toFixed(1));
    var tem = bytes[2]<<24>>16 | bytes[3]; tem = 0.1 * tem; tem = Number(tem.toFixed(1));
    var rpm = bytes[4]<<8 | bytes[5]; rpm = 0.1 * rpm; rpm = Number(rpm.toFixed(0));
    var fpm = bytes[6]<<8 | bytes[7]; fpm = 0.1 * fpm; fpm = Number(fpm.toFixed(0));
    
    if (hum < 0.0 || hum > 100.0) hum = null;
    if (tem < -50.0 || tem > 100.0 ) tem = null;

    if (rpm < 0 || rpm > 5000) rpm = null;
    if (fpm < 0 || fpm > 5000) fpm = null;
    
    var eaqi="NaN";
    if (fpm >= 50 && fpm < 800 || rpm >= 100 && rpm < 1200) { eaqi = "Very poor" } else
    if (fpm >= 25 && fpm <  50 || rpm >=  50 && rpm <  100) { eaqi = "Poor" } else
    if (fpm >= 20 && fpm <  25 || rpm >=  35 && rpm <   50) { eaqi = "Moderate" } else
    if (fpm >= 10 && fpm <  20 || rpm >=  20 && rpm <   35) { eaqi = "Fair" } else
    if (fpm >   0 && fpm <  10 || rpm >    0 && rpm <   20) { eaqi = "Good" } else { eaqi = "Unknown" };
  
    decoded.Humidity = hum;
    decoded.Temperature = tem;
    decoded.FPM = fpm;  
    decoded.RPM = rpm;  

    decoded.EAQI = eaqi;
    
    return decoded;
  }

}