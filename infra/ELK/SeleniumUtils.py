from selenium import webdriver
from time import sleep
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC
from selenium.webdriver.common.by import By
import os, json
from selenium.webdriver.common.desired_capabilities import DesiredCapabilities
from selenium.webdriver.firefox.options import Options
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.common.action_chains import ActionChains



import pandas as pd

sleep_time = 10

def is_port_in_use(port):
    import socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        return s.connect_ex(('localhost', port)) == 0

class SeleniumDriver:
    def __init__(self):
        pass

    def create_driver(self):
        # To prevent download dialog
        profile = webdriver.FirefoxProfile()
        firefox_options = webdriver.FirefoxOptions()

        # Probably some of the are unncesary but you shouldn't check
        profile.set_preference("browser.download.folderList",2)
        profile.set_preference('browser.download.dir', '/home/seluser')
        profile.set_preference("browser.helperApps.neverAsk.saveToDisk","text/plain, application/octet-stream, application/binary, text/csv, application/csv, application/excel, text/comma-separated-values, text/xml, application/xml")
        profile.set_preference("browser.download.manager.showWhenStarting", False)
        profile.set_preference("browser.preferences.instantApply", True)
        profile.set_preference("browser.helperApps.alwaysAsk.force", False)
        profile.set_preference("browser.download.manager.useWindow", False)
        profile.set_preference("browser.download.manager.focusWhenStarting", False)
        profile.set_preference("browser.download.manager.showAlertOnComplete", False)
        profile.set_preference("browser.download.manager.closeWhenDone", True)

        firefox_options.profile = profile

        try:
            print("Creating remote selenium driver")
            #capabilities = webdriver.DesiredCapabilities()
            driver = webdriver.Remote(
                command_executor='http://localhost:4444/wd/hub',
               options=firefox_options
            )
            print("Remote Creation")
 
            return driver
        except Exception as e:
            print(e)
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
    def __init__(self,container_csv_dir, host_csv_dir):
        kibanaLogin = KibanaLoginPage(SeleniumDriver().create_driver())
        self.driver = kibanaLogin.loginPage()
        self.container_csv_dir = container_csv_dir
        self.host_csv_dir = host_csv_dir
    def __de__(self):
        self.driver.quit()

    def scroll_shim(self, passed_in_driver, object):
        x = object.location['x']
        y = object.location['y']
        scroll_by_coord = 'window.scrollTo(%s,%s);' % (
            x,
            y
        )
        scroll_nav_out_of_way = 'window.scrollBy(0, -120);'
        passed_in_driver.execute_script(scroll_by_coord)
        passed_in_driver.execute_script(scroll_nav_out_of_way)

    def firefox_button_click(self, button_elemnt):
        if 'firefox' in self.driver.capabilities['browserName']:
            self.scroll_shim(self.driver, button_elemnt)

        # scroll_shim is just scrolling it into view, you still need to hover over it to click using an action chain.
        actions = ActionChains(self.driver)
        actions.move_to_element(button_elemnt)
        actions.click()
        actions.perform()

    def copy_from_container_to_exports_dir(self): 

        # Get docker id, run the command on host, same as the next commmand
        id = os.popen("nsenter -t 1 -m -u -n -i sh -c \"docker ps | awk '\\$2 ~ /^selenium/ { print \\$1 }\'\"").read().strip()

        # Copy from seluser directory to exports
        print("Copying files from selenium container id " + id + " + to host") 

        # Copy the files, execute the cp command on host from container using some trick to get the permissions, docker run shuold be with --privileged for this to work
        os.popen("nsenter -t 1 -m -u -n -i sh -c \"docker cp " + id + ":/home/seluser " + self.host_csv_dir + "\"").read().strip()

    def get_dashboard_csv(self,dashboard_path, dashboard_name):
        print("Downloading Dashboard as CSV: " + dashboard_name)

        #Time it takes for the visualization to load, validate in server. Searching for HTML element isn't enough, data needs to be loaded
        sleep(sleep_time)
        self.driver.get(dashboard_path)

        sleep(sleep_time)

        #Open Panel menu
        self.driver.maximize_window()
        print("Finding options button")
        #WebDriverWait(self.driver,10).until(EC.presence_of_element_located((By.XPATH,"//button[@class='euiContextMenuItem'][.='More']")))

        self.firefox_button_click(self.driver.find_element_by_xpath("//button[@aria-label='Panel options for " + dashboard_name + "\']"))

        sleep(sleep_time)
        
        #Click More button
        print("Clicking More button")


        button = self.driver.find_element_by_xpath("//button[@class='euiContextMenuItem'][.='More']")

        self.firefox_button_click(button)

        sleep(sleep_time)

        #Click Download as CSV
        print("Clicking download as CSV button")
        #WebDriverWait(self.driver,10).until(EC.presence_of_element_located((By.XPATH,"//button[@data-test-subj='embeddablePanelAction-ACTION_EXPORT_CSV']")))

        button = self.driver.find_element_by_xpath("//button[@data-test-subj='embeddablePanelAction-ACTION_EXPORT_CSV']")
        self.firefox_button_click(button)

        sleep(sleep_time)

        self.copy_from_container_to_exports_dir()

        #return handle to csv file
        csv_file = os.path.join(self.container_csv_dir, dashboard_name) + ".csv"

        print("Finished downloading Dashboard as CSV: " + dashboard_name)


        print(csv_file)
        print("Log begin: " + open(csv_file,"r").read(20))


        return csv_file
