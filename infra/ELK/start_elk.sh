#Validate basedir commandline parameter 
if [[ -d $1 ]]; then
   echo "Project Base dir: " $1
else
   echo "Wrong base dir, please provide base dir for the project to run in the first argument"
   exit 1
fi
########################################

#Check log file exists

# Setup ELK
git clone https://github.com/deviantony/docker-elk.git

#chcon -R system_u:object_r:admin_home_t:s0 docker-elk/
export no_proxy=localhost,127.0.0.0,127.0.1.1,127.0.1.1,local.home,$no_proxy

cd docker-elk

#Increase Containers Java heap from 256m to 512m so it won't crash, depends on your logs size you may need larger memory
sed -i 's/Xmx256m/Xmx512m/g' docker-compose.yml
sed -i 's/Xms256m/Xms512m/g' docker-compose.yml

# Deleting logs in case of crashes curl -XDELETE http://localhost:9200/filebeat-\* -H "Authorization: Basic ZWxhc3RpYzpjaGFuZ2VtZQ=="
docker-compose up -d
########################################

#Wait until kibana is up to continue 
attempt_counter=0
max_attempts=20

until $(curl --output /dev/null --silent --head --fail http://localhost:5601); do
    if [ ${attempt_counter} -eq ${max_attempts} ];then
      echo "Kibana is unavailable, exiting"
      exit 1
    fi

    printf '.'
    attempt_counter=$(($attempt_counter+1))
    sleep 5
done
########################################

# Go out from docker-elk
cd ..

# Setup filebeat 
docker pull docker.elastic.co/beats/filebeat:7.14.0
docker run docker.elastic.co/beats/filebeat:7.14.0 setup -E setup.kibana.host=localhost:5601 -E output.elasticsearch.hosts=["localhost:9200"]  
docker run -d --network host --volume=$1"/filebeat.yml:/usr/share/filebeat/filebeat.yml:ro" --volume="/var/lib/docker/containers:/var/lib/docker/containers:ro"  --volume="/var/run/docker.sock:/var/run/docker.sock:ro"  --volume=$1"/logs:/logs" docker.elastic.co/beats/filebeat:7.14.0 filebeat -e -strict.perms=false -E output.elasticsearch.hosts=["127.0.0.1:9200"]  

echo "ELK Is ready to use! put your logs in "$1"/logs directory and they will be loaded to ELK"