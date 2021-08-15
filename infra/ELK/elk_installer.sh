git clone https://github.com/deviantony/docker-elk.git

#chcon -R system_u:object_r:admin_home_t:s0 docker-elk/
export no_proxy=localhost,127.0.0.0,127.0.1.1,127.0.1.1,local.home,$no_proxy

cd docker-elk

# Download from https://www.elastic.co/downloads/beats/filebeat
curl -L -O https://artifacts.elastic.co/downloads/beats/filebeat/filebeat-7.13.3-linux-x86_64.tar.gz
tar xzvf filebeat-7.13.3-linux-x86_64.tar.gz
chmod go-w ../minimal_filebeat_conf.yml
./filebeat-7.13.3-linux-x86_64.tar.gz/filebeat -e -c ../minimal_filebeat_conf.yml

# Put attention to #ES_JAVA_OPTS: "-Xmx256m -Xms256m" in docker-compose.yml
# Deleting logs in case of crashes curl -XDELETE http://localhost:9200/filebeat-\* -H "Authorization: Basic ZWxhc3RpYzpjaGFuZ2VtZQ=="
docker-compose up -d

cd ..
mkdir exports

# Upload dashboard
curl -X POST "http://localhost:5601/api/saved_objects/_import" -H "kbn-xsrf: true" -H "securitytenant: global" --form file=@real_logs_dashboard.ndjson -u elastic:changeme

# Download from https://www.elastic.co/downloads/beats/filebeat
curl -L -O https://artifacts.elastic.co/downloads/beats/filebeat/filebeat-7.13.3-linux-x86_64.tar.gz
tar xzvf filebeat-7.13.3-linux-x86_64.tar.gz
./filebeat-7.13.3-linux-x86_64/filebeat  -e -c minimal_filebeat_conf.yml

docker run -d  --network host --shm-size="2g" selenium/standalone-firefox:4.0.0-rc-1-prerelease-20210804

# Shell on container 
# docker exec -t -i 5cb14ca924db /bin/bash

docker build -t automated_testing .
docker run  --privileged --pid=host --network host -v /home/koilan2/eVSSIM/elk_dev/Proj/logs:/logs -v /home/koilan2/eVSSIM/elk_dev/Proj/exports/seluser:/exports -i -t automated_testing