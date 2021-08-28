import time
import datetime
import sched
import yaml
import json
import logging
from logstash_async.handler import AsynchronousLogstashHandler
from logstash_async.transport import HttpTransport
from logstash_async.handler import LogstashFormatter
import paho.mqtt.client
import requests
import pandas as pd
import numpy as np

# reading configuration
with open("pengy-sensor.community.yml", 'r') as ymlfile:
	config = yaml.load(ymlfile, Loader=yaml.FullLoader)

# logstash config
logstash_host = config['logstash']['host']
logstash_port = 443
logstash_username = config['logstash']['username']
logstash_password = config['logstash']['password']
logstash_environment= config['logstash']['environment']
logstash_log_level = config['logstash']['log_level']

# logging config
logging.basicConfig(
	level=logstash_log_level,
	format='%(asctime)s [%(levelname)-8s] #%(lineno)03d / %(funcName)s / (%(threadName)-10s) %(message)s',
)

# remote logging set-up
log = logging.getLogger("stash")
log.setLevel(logstash_log_level)

transport = HttpTransport(
    host=logstash_host,
    port=logstash_port,
    ssl_enable=True,
    ssl_verify=True,
    timeout=5.0,
    username=logstash_username,
    password=logstash_password)

handler = AsynchronousLogstashHandler(
    host=logstash_host,
    port=logstash_port,
	transport=transport,
    database_path='log-stash.db')

formatter = LogstashFormatter(
	extra={
		'application': "pengy",
		'component': "sensor.community",
		'environment': logstash_environment})

handler.setFormatter(formatter)

log.addHandler(handler)

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
		log.error('Sensor.Community * Post - Connection error: %s' % str(e), exc_info=True)
	except Exception as e:
		log.error('Sensor.Community * Post - Error: %s' % str(e), exc_info=True)

	return


def sendSensorCommunity():
	try:
		naned_uids = []

		for uid in aq:
			aq[uid] = aq[uid].append(pd.DataFrame(
				data = {"rpm": [np.nan],
						"fpm": [np.nan], 
						"upm": [np.nan], 
						"tem": [np.nan],
						"hum": [np.nan],
						"pre": [np.nan]},
				index = [datetime.datetime.now()]))
			
			aq_ = aq[uid].rolling('30min').median()
			rpm = round(aq_.rpm.iat[-1],0)
			fpm = round(aq_.fpm.iat[-1],0)
			upm = round(aq_.upm.iat[-1],0)
			tem = round(aq_.tem.iat[-1],1)
			hum = round(aq_.hum.iat[-1],0)
			pre = round(aq_.pre.iat[-1],0)
			
			log.info('Sensor.Community * Send - Sensor %s [v%s]: RPM = %s   FPM = %s   UPM = %s   t = %s   RH = %s   p = %s hPa' % (uid, ver[uid], rpm, fpm, upm, tem, hum, pre))

			if ver[uid] == '1.0':
				postSensorCommunity('ttn-pengy-'+uid, 1, { "P1": rpm, "P2": fpm } )
				postSensorCommunity('ttn-pengy-'+uid, 7, { "temperature": tem, "humidity": hum } )
			
			if ver[uid] == '1.5':
				postSensorCommunity('TTN-'+uid, 1, { "P1": rpm, "P2": fpm } )
				postSensorCommunity('TTN-'+uid, 11, { "temperature": tem, "humidity": hum , "pressure": pre} )

			if ver[uid] == '2.0':
				postSensorCommunity('TTN-'+uid, 1, { "P0": upm, "P1": rpm, "P2": fpm } )
				postSensorCommunity('TTN-'+uid, 11, { "temperature": tem, "humidity": hum , "pressure": pre} )

			if np.isnan(rpm) and np.isnan(fpm) and np.isnan(upm) and np.isnan(tem) and np.isnan(hum) and np.isnan(pre):
				naned_uids.append(uid)

		for naned_uid in naned_uids:
			aq.pop(naned_uid, None)
			ver.pop(naned_uid, None)
			log.warning('Sensor.Community * Send - Sensor %s - removing due to the lack of data' % (naned_uid))

	except Exception as e:
		log.error('Sensor.Community * Send - Error: %s' % str(e), exc_info=True)
	
	sch.enter(275, 1, sendSensorCommunity)

	return


def ttn_on_connect(client, userdata, flags, rc):
	log.info('MQTT (TTN) * On Connect - Result: ' + paho.mqtt.client.connack_string(rc))
	ttn_mqtt_client.subscribe("v3/pengy@ttn/devices/+/up", 0)


def ttn_on_disconnect(client, userdata, rc):
	if rc != 0:
		log.warning('MQTT (TTN) * On Disconnect - unexpected disconnection')

def ttn_on_message(client, userdata, message):
	try:
		log.debug('MQTT (TTN) * On Message - Topic "' + message.topic + '"')

		try:
			payload = message.payload.decode('utf8')

			message_data = json.loads(payload)

			device_ids = message_data["end_device_ids"]
			uplink_message = message_data["uplink_message"]
			
			data = uplink_message["decoded_payload"]
			rx_data = uplink_message["rx_metadata"]
            
			uid = device_ids.get("device_id")

			version = data.get("Version")
			
			tem  = data.get("Temperature") # v1  v1.5  v2.0
			hum  = data.get("Humidity")    # v1  v1.5  v2.0
			pre  = data.get("Pressure")    #     v1.5  v2.0
			
			rpm  = data.get("RPM")         # v1  v1.5  v2.0
			fpm  = data.get("FPM")         # v1  v1.5  v2.0
			upm  = data.get("UPM")         #           v2.0

			aqi  = data.get("EAQI", "Unknown")
		except Exception as ex:
			log.error('MQTT (TTN) * On Message - Error parsing payload: %s - %s' % (payload, str(ex)), exc_info=True)
			return

		if not uid in aq:
			aq[uid] = pd.DataFrame(data = {"rpm": [], "fpm": [], "upm": [], "tem": [], "hum": []}, index = [])
			ver[uid] = version

		aq[uid] = aq[uid].append(pd.DataFrame(
				data = {"tem" : [tem or np.nan], "hum" : [hum or np.nan], "pre" : [pre or np.nan],
						"rpm" : [rpm or np.nan], "fpm" : [fpm or np.nan], "upm" : [upm or np.nan]},
				index = [datetime.datetime.now()]))

		aq[uid] = aq[uid].last('24h')

	except Exception as ex:
		log.error('MQTT (TTN) * On Message - Error : %s' % str(ex), exc_info=True)

	return


def ttn_on_subscribe(client, userdata, mid, granted_qos):
	log.info('MQTT (TTN) * On Subscribe - MId=' + str(mid) + ' QoS=' + str(granted_qos))


def ttn_on_log(client, userdata, level, string):
	if level >= logging.INFO:
		log.log(level, 'MQTT (TTN) * Log - %s' % str(string))


log.info('Pengy-Sensor.Community started')

try:
	ttn_mqtt_client = paho.mqtt.client.Client(client_id=ttn_mqtt_client_id, clean_session=False, userdata=None)

	ttn_mqtt_client.on_connect = ttn_on_connect
	ttn_mqtt_client.on_disconnect = ttn_on_disconnect

	ttn_mqtt_client.on_message = ttn_on_message

	ttn_mqtt_client.on_subscribe = ttn_on_subscribe
	ttn_mqtt_client.on_log = ttn_on_log

	ttn_mqtt_client.username_pw_set(username=ttn_mqtt_username, password=ttn_mqtt_password)
	ttn_mqtt_client.tls_set('thethings-network.pem')
	ttn_mqtt_client.connect(ttn_mqtt_broker, ttn_mqtt_broker_port, 60)

	ttn_mqtt_client.loop_start()

	sch.enter(15, 1, sendSensorCommunity)
	
	while True:
		sch.run(False)
		time.sleep(1)
		
	ttn_mqtt_client.loop_stop()
	ttn_mqtt_client.disconnect()

except Exception as ex:
	log.error('Pengy-Sensor.Community error: %s' % str(ex), exc_info=True)

log.error('Pengy-Sensor.Community finished')