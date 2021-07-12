git clone https://github.com/deviantony/docker-elk.git

chcon -R system_u:object_r:admin_home_t:s0 docker-elk/

cd docker-elk

# Download from https://www.elastic.co/downloads/beats/filebeat

./filebeat -e -c ./filebeat_conf.yml

# Put attention to #ES_JAVA_OPTS: "-Xmx256m -Xms256m" in docker-compose.yml
# Deleting logs in case of crashes curl -XDELETE http://localhost:9200/filebeat-\* -H "Authorization: Basic ZWxhc3RpYzpjaGFuZ2VtZQ=="
docker-compose up