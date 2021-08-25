# Delete all data in ELK
echo "Deleting all Elastic Data"
curl -X DELETE 'http://localhost:9200/_all'  -u elastic:changeme

#Kill all ELK Project related dockers
echo "Killing all dockres relted to selenium, kibama, elastic, logstash and filebeat"
docker kill $(docker ps --format="{{.Image}} {{.ID}}" | grep -e selenium -e elastic -e kibana -e kibana -e logstash -e filebeat |  cut -d' ' -f2)
