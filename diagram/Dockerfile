FROM python:3
RUN pip install diagrams
RUN apt-get update && apt-get install -y \
    graphviz \
    && rm -rf /var/lib/apt/lists/*

# docker run --rm -it -v "${PWD}":/diagram -w /diagram diagrams python diagram.py
