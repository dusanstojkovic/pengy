FROM python:3.9-slim

RUN pip install requests
RUN pip install paho-mqtt
RUN pip install PyYAML
RUN pip install pandas
RUN pip install python-logstash-async

WORKDIR /app

COPY pengy-pulse.eco.py      ./pengy-pulse.eco.py
COPY pengy-pulse.eco.yml     ./pengy-pulse.eco.yml
COPY thethings-network.pem   ./thethings-network.pem

CMD [ "python", "-u", "/app/pengy-pulse.eco.py" ]