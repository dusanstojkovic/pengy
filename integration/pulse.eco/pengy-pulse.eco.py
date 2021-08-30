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
with open("pengy-pulse.eco.yml", 'r') as ymlfile:
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
		'component': "pulse.eco",
		'environment': logstash_environment})
handler.setFormatter(formatter)

log.addHandler(handler)

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
		naned_uids = []
		uids = aq.keys()

		for uid in uids:
			aq[uid] = aq[uid].append(pd.DataFrame(
				data = {"pm1": [np.nan],
						"pm25": [np.nan], 
						"pm10": [np.nan], 						
						"tem": [np.nan],
						"hum": [np.nan],
						"pre": [np.nan],
						"no2": [np.nan],
						"co": [np.nan],
						"nh3": [np.nan],
						"noi": [np.nan]},
				index = [datetime.datetime.now()]))
			
			aq_ = aq[uid].rolling('30min').median()
			pm1  = round(aq_.pm1.iat[-1],0)
			pm25 = round(aq_.pm25.iat[-1],0)
			pm10 = round(aq_.pm10.iat[-1],0)
			tem = round(aq_.tem.iat[-1],1)
			hum = round(aq_.hum.iat[-1],0)
			pre = round(aq_.pre.iat[-1],0)
			no2 = round(aq_.no2.iat[-1],0)
			co = round(aq_.co.iat[-1],0)
			nh3 = round(aq_.nh3.iat[-1],0)
			noi = round(aq_.noi.iat[-1],0)
			alt = al[uid]

			if (np.isnan(tem) and np.isnan(hum) and np.isnan(pre) and np.isnan(noi) and
				np.isnan(pm1) and np.isnan(pm25) and np.isnan(pm10)):
				naned_uids.append(uid)
				log.info('Pulse.Eco * Send - Sensor %s : NULL' % (uid))
				continue

			log.info('Pulse.Eco * Send - Sensor %s : PM1.0 = %s μg/m³   PM2.5 = %s μg/m³   PM10 = %s μg/m³   t = %s °C   RH = %s %%   p = %s hPa   MASL = %s m   NO2 = %s ppm   CO = %s ppm   NH3 = %s ppm   Noise = %s dBa' % (uid, pm1, pm25, pm10, tem, hum, pre, alt, no2, co, nh3, noi), extra={"device": uid})

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
				if not np.isnan(pm1): params["pm1"] = pm1
				if not np.isnan(pm25): params["pm25"] = pm25
				if not np.isnan(pm10): params["pm10"] = pm10

				# noise
				if not np.isnan(noi): params["noise_dba"] = noi

				# gas
				if not np.isnan(no2): params["no2_ppb"] = 1000.0 * no2 
				if not np.isnan(co): params["co_ppb"]  = 1000.0 * co
				if not np.isnan(nh3): params["nh3_ppb"] = 1000.0 * nh3

				r = requests.get('https://pulse.eco/wifipoint/store', params)

				log.debug('Pulse.Eco * GET : %s' % r.url)

			except requests.ConnectionError as e:
				log.error('Pulse.Eco * Send - Connection error: %s' % str(e), exc_info=True)

		for naned_uid in naned_uids:
			aq.pop(naned_uid, None)
			log.warning('Pulse.Eco * Send - Sensor %s - removing due to the lack of data' % (naned_uid))

	except Exception as e:
		log.error('Pulse.Eco * Send - Error: %s' % str(e), exc_info=True)
	
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

			alt  = uplink_message["locations"]["user"]["altitude"]

			ver =  data.get("Version")
			
			tem  = data.get("Temperature") # v1  v1.5  v2.0
			hum  = data.get("Humidity")    # v1  v1.5  v2.0
			pre  = data.get("Pressure")    #     v1.5  v2.0
			
			pm1  = data.get("UPM")         #           v2.0
			pm25 = data.get("FPM")         # v1  v1.5  v2.0
			pm10 = data.get("RPM")         # v1  v1.5  v2.0

			no2  = data.get("NO2")         # v1  v1.5
			co   = data.get("CO")          # v1  v1.5
			nh3  = data.get("NH3")         # v1  v1.5

			noi  = data.get("Noise")       #     v1.5  v2.0

			aqi  = data.get("EAQI", "Unknown")
		except Exception as ex:
			log.error('MQTT (TTN) * On Message - Error parsing payload: %s - %s' % (payload, str(ex)), exc_info=True)
			return

		if not uid in aq:
			aq[uid] = pd.DataFrame(data = {"tem": [], "hum": [], "pre": [], "pm1": [], "pm25": [], "pm10": [], "no2": [], "co": [], "nh3": [], "noi": []}, index = [])
			al[uid] = alt

		aq[uid] = aq[uid].append(pd.DataFrame(
				data = {"tem" : [tem or np.nan], "hum" : [hum or np.nan], "pre" : [pre or np.nan],
						"pm1" : [pm1 or np.nan], "pm25" : [pm25 or np.nan], "pm10" : [pm10 or np.nan], 
						"no2" : [no2 or np.nan], "co"  : [co or np.nan], "nh3" : [nh3 or np.nan],
						"noi" : [noi or np.nan]},
				index = [datetime.datetime.now()]))

		aq[uid] = aq[uid].last('24h')

	except Exception as ex:
		log.error('MQTT (TTN) * On Message - Error : %s' % str(ex), exc_info=True)

	return


def ttn_on_subscribe(client, userdata, mid, granted_qos):
	log.info('MQTT (TTN) * On Subscribe - MId=' + str(mid) + ' QoS=' + str(granted_qos))


def ttn_on_log(client, userdata, level, string):
	if level >= logging.INFO: # or MQTT_LOG_WARNING
		log.log(level, 'MQTT (TTN) * Log - %s' % str(string))


log.info('Pengy-Pulse.Eco started')

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

	sch.enter(15, 1, sendPulseEco)
	
	while True:
		sch.run(False)
		time.sleep(1)
		
	ttn_mqtt_client.loop_stop()
	ttn_mqtt_client.disconnect()

except Exception as ex:
	log.error('Pengy-Pulse.Eco error: %s' % str(ex), exc_info=True)

log.error('Pengy-Pulse.Eco finished')