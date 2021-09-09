import os, sys,json
from time import sleep

log_path = os.path.join(sys.argv[1], "logs", sys.argv[2])
print("Waiting until filebeat finished uploading log file from path: " + log_path)

def is_port_in_use(port):
    import socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        return s.connect_ex(('localhost', port)) == 0

if not is_port_in_use(9200):
     print("Elastic Probably down, port is closed")
     exit()

try:
     log_file_count = int(os.popen("wc -l " + log_path + " |  awk \'{print $1}\'").read().strip())

     log_hits_query = os.popen("curl --silent -X GET \'http://elastic:changeme@localhost:9200/_count?q=log.file.path:*\'" + sys.argv[2])
     indexed_logs = int(json.load(log_hits_query)['count'])
except:
     exit(1)

print("Log file count: " + str(log_file_count))

max_tries = 30
current_try = 0
while indexed_logs != log_file_count:
     if current_try > max_tries:
          print("Too many time for filebeat indexing, exiting")
          exit(1)
     log_hits_query = os.popen("curl --silent -X GET \'http://elastic:changeme@localhost:9200/_count?q=log.file.path:*\'" + sys.argv[2])
     indexed_logs = int(json.load(log_hits_query)['count'])
     print("Hits: " + str(indexed_logs) , end='\r')
     sleep(2)

print("Filebeat finished inxeding given log")