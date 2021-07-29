
# Run the bandwidth script
./docker-elk-create-bandwidth.sh
res=$?
if [ "$res" -ne "0" ]; then
	echo "[X] Bandwidth script failed miserably"
	exit $res
fi

# Parse the results of the bandwidth script
start_time=$(grep -oP 'start_time=(.*)' /tmp/elk_bandwidth_res.txt | cut -d '=' -f 2)
end_time=$(grep -oP 'end_time=(.*)' /tmp/elk_bandwidth_res.txt | cut -d '=' -f 2)
log_file=$(grep -oP 'log_file=(.*)' /tmp/elk_bandwidth_res.txt | cut -d '=' -f 2)

echo "+++++++ results ++++++"
echo $start_time
echo $end_time
echo $log_file