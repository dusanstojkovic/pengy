FROM python:3.7

RUN pip install requests
RUN pip install paho-mqtt
RUN pip install PyYAML

WORKDIR /app

COPY pengy-luftdaten.py      ./pengy-luftdaten.py
COPY pengy-luftdaten.yml     ./pengy-luftdaten.yml
COPY thethings-network.pem   ./thethings-network.pem

CMD [ "python", "-u", "/app/pengy-luftdaten.py" ]