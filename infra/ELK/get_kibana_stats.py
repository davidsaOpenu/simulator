import time, json
from SeleniumUtils import *
from DashboardsTests import *
import os, sys
import urllib.parse

os.environ["no_proxy"] ="localhost,127.0.0.0,127.0.1.1,127.0.1.1,local.home,$no_proxy"
os.system("export no_proxy=localhost,127.0.0.0,127.0.1.1,127.0.1.1,local.home,$no_proxy")

#print("Working directory: " + sys.argv[1] + " Printed from docker")

log_file = os.path.join("/logs",sys.argv[2])

csv_dir_container = "/exports"

# Change it, put attention that docker runs in root so don't use ~ without using it in all scripts
#host_csv_dir = "/home/koilan2/eVSSIM/elk_dev/Automation/exports"
host_csv_dir = os.path.join(sys.argv[1], "exports")

print("Using host csv exports dir as: " + host_csv_dir)

start_time = sys.argv[3]
end_time = sys.argv[4]

print("Using start_time: " + start_time + "\n Using end_time: " + end_time)
# Put attention to the specific dashboard url, filterd with the time of the logs! Change it if you use different log file
#dashboard_path = "http://localhost:5601/app/dashboards#/view/ab6b5da0-db7c-11eb-a759-330433d83e5b?_g=(filters:!(),refreshInterval:(pause:!t,value:0),time:(from:now-1y,to:now))&_a=(description:'',filters:!(),fullScreenMode:!f,options:(hidePanelTitles:!f,useMargins:!t),query:(language:kuery,query:\'log.file.path%20:* " + urllib.parse.quote(sys.argv[2]) + "\'),tags:!(),timeRestore:!f,title:\'Real%20Logs\',viewMode:view)"
dashboard_path = "http://localhost:5601/app/dashboards#/view/ab6b5da0-db7c-11eb-a759-330433d83e5b?_g=(filters:!(),refreshInterval:(pause:!t,value:0),time:(from:\'" + start_time + "\',to:\'" + end_time + "\'))&_a=(description:'',filters:!(),fullScreenMode:!f,options:(hidePanelTitles:!f,useMargins:!t),query:(language:kuery,query:''),tags:!(),timeRestore:!f,title:'Real%20Logs',viewMode:view)"
#dashboard_path = "http://localhost:5601/app/dashboards#/view/ab6b5da0-db7c-11eb-a759-330433d83e5b?_g=(filters:!(),refreshInterval:(pause:!t,value:0),time:(from:'2021-07-04T15:14:47.000Z',to:'2021-07-04T15:15:00.000Z'))&_a=(description:'',filters:!(),fullScreenMode:!f,options:(hidePanelTitles:!f,useMargins:!t),query:(language:kuery,query:''),tags:!(),timeRestore:!f,title:'Real%20Logs',viewMode:view)"
if not is_port_in_use(5601):
	print("Kibana Probably down, port is closed")
	exit()

# Init log file parsing
#log_file = "/Users/ilank/Documents/Openu/OSW/real_11_log.json"

print("Begining of log file: " + log_file + " " + open(log_file).read(10))

JsonLog_DF  = pd.read_json(log_file, lines=True)
JsonLog_DF.logging_time = pd.to_datetime(JsonLog_DF .logging_time,format="%Y-%m-%d %H:%M:%S.%f")


de = DashboardExporter(csv_dir_container, host_csv_dir)

tests = Dashboard_Test(de, JsonLog_DF,dashboard_path)


tests.Check_Page_Writes("PhysicalCellProgramLog sum of page over time, each second")
tests.Check_Page_Reads("PhysicalCellReadLog sum of page over time, each second")
tests.Block_Writes("blocks accessed for writing at t")
#tests.Channel_Switched_Read("channels switched to write operation at t")
#tests.Channel_Switched_Write("channels switched to read operation at t")
#tests.Block_Reads("blocks accessed for reading at t")

de.driver.quit()






