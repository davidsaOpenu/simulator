git clone https://github.com/deviantony/docker-elk.git

chcon -R system_u:object_r:admin_home_t:s0 docker-elk/

cd docker-elk

# Download from https://www.elastic.co/downloads/beats/filebeat
curl -L -O https://artifacts.elastic.co/downloads/beats/filebeat/filebeat-7.13.3-linux-x86_64.tar.gz
tar xzvf filebeat-7.13.3-linux-x86_64.tar.gz
./filebeat-7.13.3-linux-x86_64.tar.gz/filebeat -e -c ../ELK/minimal_filebeat_conf.yml

# Put attention to #ES_JAVA_OPTS: "-Xmx256m -Xms256m" in docker-compose.yml
# Deleting logs in case of crashes curl -XDELETE http://localhost:9200/filebeat-\* -H "Authorization: Basic ZWxhc3RpYzpjaGFuZ2VtZQ=="
docker-compose up

curl -X POST "localhost:5601/api/saved_objects/_import?createNewCopies=true -H \"kbn-xsrf: true'" --form file=../ELK/real_logs_dashboard.ndjson -H 'kbn-xsrf: true'