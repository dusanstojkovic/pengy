FROM python:3.9-slim

RUN pip install requests
RUN pip install paho-mqtt
RUN pip install PyYAML
RUN pip install pandas
RUN pip install python-logstash-async

WORKDIR /app

COPY pengy-sensor.community.py      ./pengy-sensor.community.py
COPY pengy-sensor.community.yml     ./pengy-sensor.community.yml
COPY thethings-network.pem   ./thethings-network.pem

CMD [ "python", "-u", "/app/pengy-sensor.community.py" ]