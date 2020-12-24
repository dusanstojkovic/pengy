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
with open("pengy-sensor.community.yml", 'r') as ymlfile:
	config = yaml.load(ymlfile, Loader=yaml.FullLoader)

# ttn config
ttn_mqtt_broker = config['ttn_mqtt']['server']
ttn_mqtt_broker_port = config['ttn_mqtt']['port']
ttn_mqtt_username = config['ttn_mqtt']['username']
ttn_mqtt_password = config['ttn_mqtt']['password']
ttn_mqtt_client_id = 'pengy-sensor.community'

# sensor_community config
sensor_community_software_version = config['sensor_community']['software_version']

sch = sched.scheduler(time.time, time.sleep)

aq = {}
ver = {}

def postSensorCommunity(sensor_id, sensor_pin, values):
	try:
		r = requests.post('https://api.sensor.community/v1/push-sensor-data/',
			json = {
				"software_version": sensor_community_software_version,
				#"sampling_rate":10000,
				"sensordatavalues": [{"value_type": key, "value": val} for key, val in values.items()],
			},
			headers = {
				"X-PIN":    str(sensor_pin),
				"X-Sensor": sensor_id,
			})		
	except requests.ConnectionError as e:
		logging.error('Sensor.Community * Post - Connection error: %s' % str(e))
	except Exception as e:
		logging.error('Sensor.Community * Post - Error: %s' % str(e))

	return


def sendSensorCommunity():
	try:
		for uid in aq:
			aq[uid] = aq[uid].append(pd.DataFrame(
				data = {"rpm": [np.nan],
						"fpm": [np.nan], 
						"tem": [np.nan],
						"hum": [np.nan],
						"pre": [np.nan]},
				index = [datetime.datetime.now()]))
			
			aq_ = aq[uid].rolling('30min').median()
			rpm = round(aq_.rpm.iat[-1],0)
			fpm = round(aq_.fpm.iat[-1],0)
			tem = round(aq_.tem.iat[-1],1)
			hum = round(aq_.hum.iat[-1],0)
			pre = round(aq_.pre.iat[-1],0)
			
			logging.debug('Sensor.Community * Send - Sensor %s [v%s]: RPM = %s   FPM = %s   t = %s   RH = %s   p = %s hPa' % (uid, ver[uid], rpm, fpm, tem, hum, pre))

			if ver[uid] == '1.0':
				postSensorCommunity('ttn-pengy-'+uid, 1, { "P1": rpm, "P2": fpm } )
				postSensorCommunity('ttn-pengy-'+uid, 7, { "temperature": tem, "humidity": hum } )
			
			if ver[uid] == '1.5':
				postSensorCommunity('TTN-'+uid, 1, { "P1": rpm, "P2": fpm } )
				postSensorCommunity('TTN-'+uid, 11, { "temperature": tem, "humidity": hum , "pressure": pre} )

	except Exception as e:
		logging.error('Sensor.Community * Send - Error: %s' % str(e))
	
	sch.enter(275, 1, sendSensorCommunity)

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

			tem  = data["payload_fields"].get("Temperature") # v1  v1.5
			hum  = data["payload_fields"].get("Humidity")    # v1  v1.5
			pre  = data["payload_fields"].get("Pressure")    #     v1.5
			
			rpm  = data["payload_fields"].get("RPM")         # v1  v1.5
			fpm  = data["payload_fields"].get("FPM")         # v1  v1.5

			aqi  = data["payload_fields"].get("EAQI", "Unknown")

			version = data["payload_fields"].get("Version")

			is_retry = data.get("is_retry", False)

		except Exception as ex:
			logging.error('MQTT (TTN) * On Message - Error parsing payload: %s - %s' % (payload, str(ex)))
			return

		if not uid in aq:
			aq[uid] = pd.DataFrame(data = {"fpm": [], "rpm": [], "tem": [], "hum": []}, index = [])
			ver[uid] = version

		aq[uid] = aq[uid].append(pd.DataFrame(
				data = {"tem" : [tem or np.nan], "hum" : [hum or np.nan], "pre" : [pre or np.nan],
						"rpm" : [rpm or np.nan], "fpm" : [fpm or np.nan]},
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


logging.info('Pengy-Sensor.Community started')

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

	sch.enter(15, 1, sendSensorCommunity)
	
	while True:
		sch.run(False)
		time.sleep(1)
		
	ttn_mqtt_client.loop_stop()
	ttn_mqtt_client.disconnect()

except Exception as ex:
	logging.error('Pengy-Sensor.Community error: %s' % str(ex))

logging.error('Pengy-Sensor.Community finished')