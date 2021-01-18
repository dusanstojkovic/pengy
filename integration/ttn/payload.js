function Decoder(bytes, port) 
{
  var decoded = {};
      
  if (port == 1 || port == 2)
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
    
    decoded.Version = "1.0";
  }

  if (port == 2)
  {
    var pre = bytes[8]<<8 | bytes[9]; pre = Number(pre.toFixed(0));
    var co = bytes[10]<<8 | bytes[11]; co = 1.0 * co; co = Number(co.toFixed(0));
    var nh3 = bytes[12]<<8 | bytes[13]; nh3 = 1.0 * nh3; nh3 = Number(nh3.toFixed(0));
    var no2 = bytes[14]<<8 | bytes[15]; no2 = 0.01 * no2; no2 = Number(no2.toFixed(2));
	var noise = bytes[16]<<8 | bytes[17]; noise = 0.01 * noise; noise = Number(noise.toFixed(2));

    if (pre < 0.0 || pre > 1000000.0) pre = null;
    if (co < 0.0 || co > 10000) co = null;
    if (nh3 < 0.0 || nh3 > 10000) nh3 = null;
    if (no2 < 0.0 || no2 > 10000) no2 = null;

    decoded.Pressure = pre;
    decoded.CO = co;
    decoded.NH3 = nh3;
    decoded.NO2 = no2;
	  decoded.Noise = noise;
    
    decoded.Version = "1.5";
  }

  return decoded;
}