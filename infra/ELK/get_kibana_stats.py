import time, json
from SeleniumUtils import *
from DashboardsTests import *

csv_dir = "/Users/ilank/Documents/Openu/OSW/Automation"

time_to_search = "from:'2021-07-04T15:14:47.000Z',to:'2021-07-04T15:15:00.000Z'"
dashboard_path = "http://localhost:5601/app/dashboards#/view/ab6b5da0-db7c-11eb-a759-330433d83e5b?_g=(filters:!(),query:(language:kuery,query:'log.file.path%20:%22%2FUsers%2Filank%2FDocuments%2FOpenu%2FOSW%2F36_log.json%22%20'),refreshInterval:(pause:!t,value:0),time:(" + time_to_search + "))&_a=(description:'',filters:!(),fullScreenMode:!f,options:(hidePanelTitles:!f,useMargins:!t),query:(language:kuery,query:'log.file.path%20:%22%2FUsers%2Filank%2FDocuments%2FOpenu%2FOSW%2Freal_11_log.json%22%20'),tags:!(),timeRestore:!f,title:'Real%20Logs',viewMode:edit)"

if not is_port_in_use(5601):
	print("Kibana Probably down, port is closed")
	exit()

# Init log file parsing
log_file = "/Users/ilank/Documents/Openu/OSW/real_11_log.json"
JsonLog_DF  = pd.read_json(log_file, lines=True)
JsonLog_DF .logging_time = pd.to_datetime(JsonLog_DF .logging_time,format="%Y-%m-%d %H:%M:%S.%f")


de = DashboardExporter(csv_dir)

tests = Dashboard_Test(de, JsonLog_DF,dashboard_path)

tests.Block_Writes("blocks accessed for writing at t")
tests.Check_Page_Reads("PhysicalCellReadLog sum of page over time, each second")
tests.Check_Page_Writes("PhysicalCellProgramLog sum of page over time, each second")
tests.Channel_Switched_Read("channels switched to write operation at t")
tests.Channel_Switched_Write("channels switched to read operation at t")
tests.Block_Reads("blocks accessed for reading at t")






