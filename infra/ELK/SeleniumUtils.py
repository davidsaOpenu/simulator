from selenium import webdriver
from time import sleep
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC
import os
import pandas as pd

chromedriver_path="./chromedriver"
sleep_time = 4

def is_port_in_use(port):
    import socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        return s.connect_ex(('localhost', port)) == 0

class ChromeSettings:
	def __init__(self):
	    self.chrome_path=chromedriver_path

	def settingChrome(self, csv_dir):
		chrome_options = webdriver.ChromeOptions()
		chrome_options.add_argument('--no-sandbox')
		#chrome_options.add_argument('--window-size=1920,1080')
		#chrome_options.add_argument('--headless')
		chrome_options.add_argument('--disable-gpu')        
		#Download directory
		prefs = {"download.default_directory" : csv_dir}
		chrome_options.add_experimental_option("prefs",prefs)
		try:
			driver = webdriver.Chrome(self.chrome_path, chrome_options=chrome_options)
			return driver
		except:
			print ('Error Creating chromedriver')

class KibanaLoginPage:
    def __init__(self,driver):
        self.driver=driver
        self.__kibana_url="http://localhost:5601"
        self.__kibana_username="elastic"
        self.__kibana_password="changeme"

    def loginPage(self):
        print("Logging in to Kibana")
        self.driver.get(self.__kibana_url)
        WebDriverWait(self.driver,10).until(EC.presence_of_element_located((By.XPATH,'//*[@name="username"]')))
        try:
            _username = self.driver.find_element_by_name('username')
            _username.clear()
            _username.send_keys(self.__kibana_username)
            _password = self.driver.find_element_by_name('password')
            _password.clear()
            _password.send_keys(self.__kibana_password)
            button = self.driver.find_element_by_xpath("//button[@class='euiButton euiButton--primary euiButton--fill'][.='Log in']")
            button.click()
            return self.driver
        except Exception as e:
            print(e)
            print ('Login to Kibana FAILED')

    def getKibanaUrl(self):
        return self.__kibana_url

    def setKibanaUrl(self,url):
        self.__kibana_url=url

    def getKibanaUserName(self):
        return self.__kibana_username

    def setKibanaUserName(self, username):
        self.__kibana_username = username

    def setKibanaPassword(self, password):
        self.__kibana_password = password

class DashboardExporter:
    def __init__(self,csv_dir):
        kibanaLogin = KibanaLoginPage(ChromeSettings().settingChrome(csv_dir))
        self.driver = kibanaLogin.loginPage()
        self.csv_dir = csv_dir
    def __del__(self):
        self.driver.quit()

    def get_dashboard_csv(self,dashboard_path, dashboard_name):
        sleep(sleep_time)

        self.driver.get(dashboard_path);
        #Time it takes for the visualization to load
        sleep(sleep_time)
        
        #Open Panel menu
        button = self.driver.find_element_by_xpath("//button[@aria-label='Panel options for " + dashboard_name + "\']")
        button.click()
        sleep(sleep_time)
        
        #Click More button
        button = self.driver.find_element_by_xpath("//button[@class='euiContextMenuItem'][.='More']")
        button.click()
        sleep(sleep_time)

        #Click Download as CSV
        button = self.driver.find_element_by_xpath("//button[@data-test-subj='embeddablePanelAction-ACTION_EXPORT_CSV'][.='Download as CSV']")
        button.click()
        sleep(sleep_time)
        #self.driver.quit()

        #return handle to csv file
        csv_file = os.path.join(self.csv_dir, dashboard_name) + ".csv"
        
        return csv_file
