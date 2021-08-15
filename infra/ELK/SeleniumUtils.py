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

sleep_time = 3

def is_port_in_use(port):
    import socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        return s.connect_ex(('localhost', port)) == 0

class SeleniumDriver:
    def __init__(self):
        self.chrome_path=chromedriver_path

    def create_driver(self):
        # To prevent download dialog
        profile = webdriver.FirefoxProfile()
        firefox_options = webdriver.FirefoxOptions()

        profile.set_preference("browser.download.folderList",2)
        profile.set_preference('browser.download.dir', '/home/seluser')
        profile.set_preference("browser.helperApps.neverAsk.saveToDisk","text/plain, application/octet-stream, application/binary, text/csv, application/csv, application/excel, text/comma-separated-values, text/xml, application/xml")
        profile.set_preference("browser.download.manager.showWhenStarting", False)
        profile.set_preference("browser.preferences.instantApply", True)
        #profile.set_preference("browser.helperApps.neverAsk.openFile","application/octet-stream;text/csv;text/plain");
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
    def __init__(self,container_csv_dir, host_csv_dir):
        kibanaLogin = KibanaLoginPage(SeleniumDriver().create_driver())
        self.driver = kibanaLogin.loginPage()
        self.container_csv_dir = container_csv_dir
        self.host_csv_dir = host_csv_dir
    def __del__(self):
        #self.driver.quit()
        pass
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
        find_id = "nsenter -t 1 -m -u -n -i sh -c \"docker ps | awk '\\$2 ~ /^selenium/ { print \\$1 }\'\""
        id = os.popen(find_id).read().strip()

        # Copy from seluser directory to exports
        print("Copying files from selenium container id " + id + " +  to host") 

        # Copy the files, execute the cp command on host from container using some trick to get the permissions, docker run shuold be with --privileged for this to work
        os.popen("nsenter -t 1 -m -u -n -i sh -c \"docker cp " + id + ":/home/seluser " + self.host_csv_dir + "\"").read().strip()

        #os.popen("mv " + os.path.join(self.host_csv_dir,"seluser") + "/*.csv " + self.host_csv_dir)
    def get_dashboard_csv(self,dashboard_path, dashboard_name):

        #Time it takes for the visualization to load, validate in server. Searching for HTML element isn't enough, data needs to be loaded
        sleep(sleep_time)
        self.driver.get(dashboard_path)

        print("Taking screenshot")

        p = self.driver.get_screenshot_as_png()

        sleep(6)
        #p = self.driver.get_screenshot_as_png()
        #with open("/logs/screenshot.png", "wb") as f:
        #    f.write(p)
        # Consider changing sleeps' to WebDriverWait
        #Open Panel menu
        self.driver.maximize_window()

        self.firefox_button_click(self.driver.find_element_by_xpath("//button[@aria-label='Panel options for " + dashboard_name + "\']"))

        #button = WebDriverWait(self.driver).until(EC.element_to_be_clickable((By.XPATH,"//button[@aria-label='Panel options for " + dashboard_name + "\']")))
        #ActionChains(self.driver).move_to_element().click().perform()

        #button = self.driver.find_element_by_xpath("//button[@aria-label='Panel options for " + dashboard_name + "\']")
        #button.click()
        sleep(sleep_time)
        
        #p = self.driver.get_screenshot_as_png()
        #with open("/logs/screenshot2.png", "wb") as f:
        #    f.write(p)

        #Click More button
        button = self.driver.find_element_by_xpath("//button[@class='euiContextMenuItem'][.='More']")
        #button.click()
        self.firefox_button_click(button)
        #sleep(sleep_time)
        #p = self.driver.get_screenshot_as_png()
        #with open("/logs/screenshot3.png", "wb") as f:
        #    f.write(p)
        sleep(sleep_time)

        #Click Download as CSV
        #<button class="euiContextMenuItem" type="button" data-test-subj="embeddablePanelAction-ACTION_EXPORT_CSV"><span class="euiContextMenu__itemLayout"><svg width="16" height="16" viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg" class="euiIcon euiIcon--medium euiIcon-isLoaded euiContextMenu__icon" focusable="false" role="img" aria-hidden="true"><path d="M8.505 1c.422-.003.844.17 1.166.516l1.95 2.05c.213.228.213.6 0 .828a.52.52 0 01-.771 0L9 2.451v7.993c0 .307-.224.556-.5.556s-.5-.249-.5-.556v-7.96l-1.82 1.91a.52.52 0 01-.77 0 .617.617 0 010-.829l1.95-2.05A1.575 1.575 0 018.5 1h.005zM4.18 7c-.473 0-.88.294-.972.703l-1.189 5.25a.776.776 0 00-.019.172c0 .483.444.875.99.875H14.01c.065 0 .13-.006.194-.017.537-.095.885-.556.778-1.03l-1.19-5.25C13.7 7.294 13.293 7 12.822 7H4.18zM6 6v1h5V6h1.825c.946 0 1.76.606 1.946 1.447l1.19 5.4c.215.975-.482 1.923-1.556 2.118a2.18 2.18 0 01-.39.035H2.985C1.888 15 1 14.194 1 13.2c0-.119.013-.237.039-.353l1.19-5.4C2.414 6.606 3.229 6 4.174 6H6z"></path></svg><span class="euiContextMenuItem__text">Download as CSV</span></span></button>
        button = self.driver.find_element_by_xpath("//button[@data-test-subj='embeddablePanelAction-ACTION_EXPORT_CSV']")
        self.firefox_button_click(button)

        #button.click()
        sleep(sleep_time)
        #self.driver.quit()

        self.copy_from_container_to_exports_dir()

        #return handle to csv file
        csv_file = os.path.join(self.container_csv_dir, dashboard_name) + ".csv"

        print(csv_file)
        print(open(csv_file,"r").read(20))

        return csv_file
