import time
import datetime
import sched
import yaml
import json
import logging
import paho.mqtt.client
import requests

import pandas as pd
import numpy as np

# logging config
logging.basicConfig(
	level=logging.WARNING,
	format='%(asctime)s [%(levelname)-8s] #%(lineno)03d / %(funcName)s / (%(threadName)-10s) %(message)s',
)

# reading configuration
with open("pengy-pulse.eco.yml", 'r') as ymlfile:
	config = yaml.load(ymlfile, Loader=yaml.FullLoader)

# ttn config
ttn_mqtt_broker = config['ttn_mqtt']['server']
ttn_mqtt_broker_port = config['ttn_mqtt']['port']
ttn_mqtt_username = config['ttn_mqtt']['username']
ttn_mqtt_password = config['ttn_mqtt']['password']
ttn_mqtt_client_id = 'pengy-pulse.eco'

# pulse.eco config
pulse_eco_software_version = config['pulse_eco']['software_version']

# sensor mapping
device_keys = config["device_keys"]

sch = sched.scheduler(time.time, time.sleep)

aq = {}
al = {}

def sendPulseEco():
	sch.enter(900, 1, sendPulseEco)

	try:
		for uid in aq:
			aq[uid] = aq[uid].append(pd.DataFrame(
				data = {"rpm": [np.nan],
						"fpm": [np.nan], 
						"tem": [np.nan],
						"hum": [np.nan],
						"pre": [np.nan],
						"no2": [np.nan],
						"co": [np.nan],
						"nh3": [np.nan],
						"noi": [np.nan]},
				index = [datetime.datetime.now()]))
			
			aq_ = aq[uid].rolling('30min').median()
			rpm = round(aq_.rpm.iat[-1],0)
			fpm = round(aq_.fpm.iat[-1],0)
			tem = round(aq_.tem.iat[-1],1)
			hum = round(aq_.hum.iat[-1],0)
			pre = round(aq_.pre.iat[-1],0)
			no2 = round(aq_.no2.iat[-1],0)
			co = round(aq_.co.iat[-1],0)
			nh3 = round(aq_.nh3.iat[-1],0)
			noi = round(aq_.noi.iat[-1],0)
			alt = al[uid]

			logging.debug('Pulse.Eco * Send - Sensor %s : RPM = %s μg/m³   FPM = %s μg/m³   t = %s °C   RH = %s %%   p = %s hPa   MASL = %s m   NO2 = %s ppm   CO = %s ppm   NH3 = %s ppm   Noise = %s dBa' % (uid, rpm, fpm, tem, hum, pre, alt, no2, co, nh3, noi))

			if uid in device_keys:
				device_key = device_keys[uid]
			else:
				device_key = uid # fallback mapping

			try:
				params = { "devAddr": device_key, "version": 20001 }
			
				# temperature, humitity, pressure, altidute
				if not np.isnan(tem): params["temperature"] = tem
				if not np.isnan(hum): params["humidity"] = hum
				if not np.isnan(pre): params["pressure"] = pre
				if not np.isnan(alt): params["altitude"] = alt

				# add PM
				if not np.isnan(rpm): params["pm10"] = rpm
				if not np.isnan(fpm): params["pm25"] = fpm

				# noise
				if not np.isnan(noi): params["noise_dba"] = noi

				# gas
				if not np.isnan(no2): params["no2_ppb"] = 1000.0 * no2 
				if not np.isnan(co): params["co_ppb"]  = 1000.0 * co
				if not np.isnan(nh3): params["nh3_ppb"] = 1000.0 * nh3

				r = requests.get('https://pulse.eco/wifipoint/store', params)

				logging.debug('Pulse.Eco * GET : %s' % r.url)

			except requests.ConnectionError as e:
				logging.error('Pulse.Eco * Send - Connection error: %s' % str(e))

	except Exception as e:
		logging.error('Pulse.Eco * Send - Error: %s' % str(e))
	
	return


def ttn_on_connect(client, userdata, flags, rc):
	logging.info('MQTT (TTN) * On Connect - Result: ' + paho.mqtt.client.connack_string(rc))
	ttn_mqtt_client.subscribe("pengy/devices/+/up", 0)


def ttn_on_disconnect(client, userdata, rc):
	if rc != 0:
		logging.warning('MQTT (TTN) * On Disconnect - unexpected disconnection')

def ttn_on_message(client, userdata, message):
	try:
		logging.debug('MQTT (TTN) * On Message - Topic "' + message.topic + '"')

		try:
			payload = message.payload.decode('utf8')
			data = json.loads(payload)							 
			
			uid = data["dev_id"]

			alt  = data["metadata"]["altitude"]
			
			tem  = data["payload_fields"].get("Temperature") # v1  v1.5
			hum  = data["payload_fields"].get("Humidity")    # v1  v1.5
			pre  = data["payload_fields"].get("Pressure")    #     v1.5
			
			rpm  = data["payload_fields"].get("RPM")         # v1  v1.5
			fpm  = data["payload_fields"].get("FPM")         # v1  v1.5

			no2  = data["payload_fields"].get("NO2")         # v1  v1.5
			co  = data["payload_fields"].get("CO")           # v1  v1.5
			nh3  = data["payload_fields"].get("NH3")         # v1  v1.5

			noi  = data["payload_fields"].get("Noise")       #     v1.5

			aqi  = data["payload_fields"].get("EAQI", "Unknown")

			is_retry = data.get("is_retry", False)
		except Exception as ex:
			logging.error('MQTT (TTN) * On Message - Error parsing payload: %s - %s' % (payload, str(ex)))
			return

		if not uid in aq:
			aq[uid] = pd.DataFrame(data = {"tem": [], "hum": [], "pre": [], "fpm": [], "rpm": [], "no2": [], "co": [], "nh3": [], "noi": []}, index = [])
			al[uid] = alt

		aq[uid] = aq[uid].append(pd.DataFrame(
				data = {"tem" : [tem or np.nan], "hum" : [hum or np.nan], "pre" : [pre or np.nan],
						"rpm" : [rpm or np.nan], "fpm" : [fpm or np.nan],
						"no2" : [no2 or np.nan], "co"  : [co or np.nan], "nh3" : [nh3 or np.nan],
						"noi" : [noi or np.nan]},
				index = [datetime.datetime.now()]))

		aq[uid] = aq[uid].last('24h')

		if is_retry:
			return

	except Exception as ex:
		logging.error('MQTT (TTN) * On Message - Error : %s' % str(ex))

	return


def ttn_on_subscribe(client, userdata, mid, granted_qos):
	logging.info('MQTT (TTN) * On Subscribe - MId=' + str(mid) + ' QoS=' + str(granted_qos))


def ttn_on_log(client, userdata, level, string):
	if level >= logging.INFO:
		logging.log(level, 'MQTT (TTN) * Log - %s' % str(string))


logging.info('Pengy-Pulse.Eco started')

try:
	ttn_mqtt_client = paho.mqtt.client.Client(client_id=ttn_mqtt_client_id, clean_session=False, userdata=None)

	ttn_mqtt_client.on_connect = ttn_on_connect
	ttn_mqtt_client.on_disconnect = ttn_on_disconnect

	ttn_mqtt_client.on_message = ttn_on_message

	ttn_mqtt_client.on_subscribe = ttn_on_subscribe
	ttn_mqtt_client.on_log = ttn_on_log

	ttn_mqtt_client.username_pw_set(username=ttn_mqtt_username, password=ttn_mqtt_password)

	ttn_mqtt_client.tls_set('thethings-network.pem')
	ttn_mqtt_client.tls_insecure_set(True)
	ttn_mqtt_client.connect(ttn_mqtt_broker, ttn_mqtt_broker_port, 60)

	ttn_mqtt_client.loop_start()

	sch.enter(15, 1, sendPulseEco)
	
	while True:
		sch.run(False)
		time.sleep(1)
		
	ttn_mqtt_client.loop_stop()
	ttn_mqtt_client.disconnect()

except Exception as ex:
	logging.error('Pengy-Pulse.Eco error: %s' % str(ex))

logging.error('Pengy-Pulse.Eco finished')