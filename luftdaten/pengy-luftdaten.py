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
	format='%(asctime)s [%(levelname)-7s] #%(lineno)03d (%(threadName)-10s) %(message)s',
)

# reading configuration
with open("pengy-luftdaten.yml", 'r') as ymlfile:
	config = yaml.load(ymlfile)

# ttn config
ttn_mqtt_broker = config['ttn_mqtt']['server']
ttn_mqtt_broker_port = config['ttn_mqtt']['port']
ttn_mqtt_username = config['ttn_mqtt']['username']
ttn_mqtt_password = config['ttn_mqtt']['password']
ttn_mqtt_client_id = 'pengy-lufdaten'

# luftdaten config
luftdaten_software_version = config['luftdaten']['software_version']

sch = sched.scheduler(time.time, time.sleep)

aq = {}

def postLuftdaten(sensor_id, sensor_pin, values):
	try:
		r = requests.post('https://api.luftdaten.info/v1/push-sensor-data/',
			json = {
				"software_version": luftdaten_software_version,
				#"sampling_rate":10000,
				"sensordatavalues": [{"value_type": key, "value": val} for key, val in values.items()],
			},
			headers = {
				"X-PIN":    str(sensor_pin),
				"X-Sensor": sensor_id,
			})		
	except requests.ConnectionError as e:
		logging.error('Luftdaten * Post - Connection error: %s' % str(e))
	except Exception as e:
		logging.error('Luftdaten * Post - Error: %s' % str(e))

	return


def sendLuftdaten():
	try:
		for uid in aq:
			aq[uid] = aq[uid].append(pd.DataFrame(
				data = {"fpm": [np.nan], 
						"rpm": [np.nan],
						"tem": [np.nan],
						"hum": [np.nan]},
				index = [datetime.datetime.now()]))
			
			aq_ = aq[uid].rolling('30min').median()
			fpm = round(aq_.fpm.iat[-1],0)
			rpm = round(aq_.rpm.iat[-1],0) 
			tem = round(aq_.tem.iat[-1],1)
			hum = round(aq_.hum.iat[-1],0)
			
			logging.debug('Luftdaten * Send - Sensor %s : RPM = %s   FPM = %s   t = %s   RH = %s' % (uid, rpm, fpm, tem, hum))

			postLuftdaten('ttn-pengy-'+uid, 1, { "P1": rpm, "P2": fpm } )
			postLuftdaten('ttn-pengy-'+uid, 7, { "temperature": tem, "humidity": hum } )

	except Exception as e:
		logging.error('Luftdaten * Send - Error: %s' % str(e))
	
	sch.enter(275, 1, sendLuftdaten)

	return


def ttn_on_connect(client, userdata, flags, rc):
	logging.info("MQTT (TTN) * On Connect - Result: " + paho.mqtt.client.connack_string(rc))
	ttn_mqtt_client.subscribe("pengy/devices/+/up", 0)


def ttn_on_disconnect(client, userdata, rc):
	if rc != 0:
		logging.warning("MQTT (TTN) * On Disconnect - unexpected disconnection")

def ttn_on_message(client, userdata, message):
	try:
		logging.debug('MQTT (TTN) * On Message - Topic "' + message.topic + '"')

		try:
			payload = message.payload.decode('utf8')
			data = json.loads(payload)							 
			
			uid = data["dev_id"]

			tem  = data["payload_fields"]["Temperature"]
			hum  = data["payload_fields"]["Humidity"]
			rpm  = data["payload_fields"]["RPM"]
			fpm  = data["payload_fields"]["FPM"]
			# aqi  = data["payload_fields"]["EAQI"]

			is_retry = data.get("is_retry", False)

		except Exception as ex:
			logging.error("MQTT (TTN) * On Message - Error parsing payload: %s - %s" % (payload, str(ex)))
			return

		if not uid in aq:
			aq[uid] = pd.DataFrame(data = {"fpm": [], "rpm": [], "tem": [], "hum": []}, index = [])

		aq[uid] = aq[uid].append(pd.DataFrame(
				data = {"fpm": [fpm], 
						"rpm": [rpm],
						"tem": [tem],
						"hum": [hum]},
				index = [datetime.datetime.now()]))

		aq[uid] = aq[uid].last('24h')

		if is_retry:
			return

	except Exception as ex:
		logging.error("MQTT (TTN) * On Message - Error : %s" % str(ex))

	return


def ttn_on_subscribe(client, userdata, mid, granted_qos):
	logging.info('MQTT (TTN) * On Subscribe - MId=' + str(mid) + ' QoS=' + str(granted_qos))


def ttn_on_log(client, userdata, level, string):
	logging.log(level, 'MQTT (TTN) * Log - %s' % str(string))


logging.info('Pengy-Luftdaten started')

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

	sch.enter(15, 1, sendLuftdaten)
	
	while True:
		sch.run(False)
		time.sleep(1)
		
	ttn_mqtt_client.loop_stop()
	ttn_mqtt_client.disconnect()

except Exception as ex:
	logging.error("Pengy-Luftdaten error: %s" % str(ex))

logging.error('Pengy-Luftdaten finished')