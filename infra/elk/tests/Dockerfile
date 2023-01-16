FROM python:3

COPY requirements.txt /tmp

RUN pip install --no-cache-dir -r /tmp/requirements.txt && rm /tmp/requirements.txt && python3 -m pip install "requests[security]"

COPY log_indexing_test.py /opt

ENTRYPOINT [ "python", "/opt/log_indexing_test.py" ]
