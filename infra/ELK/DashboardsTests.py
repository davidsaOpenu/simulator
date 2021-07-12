import pandas as pd
import os
import datetime as dt
from pandas.testing import assert_frame_equal
import numpy as np



# Get Stats
"""ChannelSwitchToReadLog_agg = JsonLog_DF [JsonLog_DF .type=="ChannelSwitchToReadLog"].set_index('logging_time').resample('100ms').sum()
ChannelSwitchToWriteLog_agg = JsonLog_DF [JsonLog_DF .type=="ChannelSwitchToWriteLog"].set_index('logging_time').resample('100ms').sum()
BlockEraseLog_agg = JsonLog_DF [JsonLog_DF .type=="BlockEraseLog"].set_index('logging_time').resample('100ms').sum()
RegisterWriteLog_agg = JsonLog_DF [JsonLog_DF .type=="RegisterWriteLog"].set_index('logging_time').resample('100ms').sum()
RegisterReadLog_agg = JsonLog_DF [JsonLog_DF .type=="RegisterReadLog"].set_index('logging_time').resample('100ms').sum()
"""
class Dashboard_Test():
	def __init__(self, de, JsonLog_DF, dashboard_path):
		self.de = de
		self.JsonLog_DF = JsonLog_DF
		self.dashboard_path = dashboard_path

	def Check_Page_Writes(self, dashboard_name):
		# Create Aggrigatoin on the json log, sum of page in every 100ms
		PhysicalCellProgramLog_agg = self.JsonLog_DF[self.JsonLog_DF.type=="PhysicalCellProgramLog"].set_index('logging_time').resample('100ms').sum()
	
		# Get Path of the dashboard csv export from Kibana
		csv_file = self.de.get_dashboard_csv(self.dashboard_path, dashboard_name)
		PhysicalCellProgramLog_Dasboard = pd.read_csv(csv_file, dtype={'Sum of page': float},thousands=',')
		os.remove(csv_file)
	
	    # Parse time from string to datetime by format
		self.JsonLog_DF .logging_time = pd.to_datetime(self.JsonLog_DF .logging_time,format="%Y-%m-%d %H:%M:%S.%f")
		PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'] = pd.to_datetime(PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'], format="%H:%M:%S.%f")
		
		# We are loading the logs from the ELK and it doesn't supply the year, month and day so we need to add them manually for the equality
		PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'] = PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'].map(lambda t: dt.datetime(2021, 4, 7, t.hour, t.minute, t.second, t.microsecond))
		PhysicalCellProgramLog_Dasboard.set_index('@timestamp per 100 milliseconds',inplace=True)
		
		# Makes the columns names equal for comparing two DataFrames
		PhysicalCellProgramLog_Dasboard.columns=PhysicalCellProgramLog_agg[["page"]].columns
		
		# Check only values and shape without index which is the time, because of different split in every method, will print nothing if equal, excpetion for difference, Double checking the values
		assert_frame_equal(PhysicalCellProgramLog_agg[["page"]].reset_index(drop=True), PhysicalCellProgramLog_Dasboard.reset_index(drop=True))
		if not (np.array_equal(PhysicalCellProgramLog_agg[["page"]].values,PhysicalCellProgramLog_Dasboard.values)):
			print("Check_Page_Writes Test Failed")
	
		print("Check_Page_Writes Passed")
	
	def Check_Page_Reads(self, dashboard_name):
		# Create Aggrigatoin on the json log, sum of page in every 100ms
		PhysicalCellReadLog_agg = self.JsonLog_DF [self.JsonLog_DF.type=="PhysicalCellReadLog"].set_index('logging_time').resample('100ms').sum()
	
		# Get Path of the dashboard csv export from Kibana
		csv_file = self.de.get_dashboard_csv(self.dashboard_path, dashboard_name)
		PhysicalCellReadLog_Dasboard = pd.read_csv(csv_file, dtype={'Sum of page': float},thousands=',')
		os.remove(csv_file)
	
	    # Parse time from string to datetime by format
		self.JsonLog_DF .logging_time = pd.to_datetime(self.JsonLog_DF .logging_time,format="%Y-%m-%d %H:%M:%S.%f")
		PhysicalCellReadLog_Dasboard['@timestamp per 100 milliseconds'] = pd.to_datetime(PhysicalCellReadLog_Dasboard['@timestamp per 100 milliseconds'], format="%H:%M:%S.%f")
		
		# We are loading the logs from the ELK and it doesn't supply the year, month and day so we need to add them manually for the equality
		PhysicalCellReadLog_Dasboard['@timestamp per 100 milliseconds'] = PhysicalCellReadLog_Dasboard['@timestamp per 100 milliseconds'].map(lambda t: dt.datetime(2021, 4, 7, t.hour, t.minute, t.second, t.microsecond))
		PhysicalCellReadLog_Dasboard.set_index('@timestamp per 100 milliseconds',inplace=True)
		
		# Makes the columns names equal for comparing two DataFrames
		PhysicalCellReadLog_Dasboard.columns=PhysicalCellReadLog_agg[["page"]].columns
		
		# Check only values and shape without index which is the time, because of different split in every method, will print nothing if equal, excpetion for difference, Double checking the values
		assert_frame_equal(PhysicalCellReadLog_agg[["page"]].reset_index(drop=True), PhysicalCellReadLog_Dasboard.reset_index(drop=True))
		if not (np.array_equal(PhysicalCellReadLog_agg[["page"]].values,PhysicalCellReadLog_Dasboard.values)):
			print("Check_Page_Reads Test Failed")
	
		print("Check_Page_Reads Passed")
	
	def Block_Writes(self, dashboard_name):
		# Create Aggrigatoin on the json log, sum of page in every 100ms
		PhysicalCellProgramLog_agg = self.JsonLog_DF[self.JsonLog_DF.type=="PhysicalCellProgramLog"].set_index('logging_time').resample('100ms').sum()
	
		# Get Path of the dashboard csv export from Kibana
		csv_file = self.de.get_dashboard_csv(self.dashboard_path, dashboard_name)
		PhysicalCellProgramLog_Dasboard = pd.read_csv(csv_file, dtype={'Sum of block': float},thousands=',')
		os.remove(csv_file)
	
	    # Parse time from string to datetime by format
		self.JsonLog_DF .logging_time = pd.to_datetime(self.JsonLog_DF .logging_time,format="%Y-%m-%d %H:%M:%S.%f")
		PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'] = pd.to_datetime(PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'], format="%H:%M:%S.%f")
		
		# We are loading the logs from the ELK and it doesn't supply the year, month and day so we need to add them manually for the equality
		PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'] = PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'].map(lambda t: dt.datetime(2021, 4, 7, t.hour, t.minute, t.second, t.microsecond))
		PhysicalCellProgramLog_Dasboard.set_index('@timestamp per 100 milliseconds',inplace=True)
		
		# Makes the columns names equal for comparing two DataFrames
		PhysicalCellProgramLog_Dasboard.columns=PhysicalCellProgramLog_agg[["block"]].columns
		
		# Check only values and shape without index which is the time, because of different split in every method, will print nothing if equal, excpetion for difference, Double checking the values
		assert_frame_equal(PhysicalCellProgramLog_agg[["block"]].reset_index(drop=True), PhysicalCellProgramLog_Dasboard.reset_index(drop=True))
		if not (np.array_equal(PhysicalCellProgramLog_agg[["block"]].values,PhysicalCellProgramLog_Dasboard.values)):
			print("Block_Writes Test Failed")
	
		print("Block_Writes Passed")
	
	def Block_Reads(self, dashboard_name):
		# Create Aggrigatoin on the json log, sum of page in every 100ms
		PhysicalCellReadLog_agg = self.JsonLog_DF [self.JsonLog_DF.type=="PhysicalCellReadLog"].set_index('logging_time').resample('100ms').sum()
	
		# Get Path of the dashboard csv export from Kibana
		csv_file = self.de.get_dashboard_csv(self.dashboard_path, dashboard_name)
		PhysicalCellReadLog_Dasboard = pd.read_csv(csv_file, dtype={'Sum of block': float},thousands=',')
		os.remove(csv_file)
	
	    # Parse time from string to datetime by format
		self.JsonLog_DF .logging_time = pd.to_datetime(self.JsonLog_DF .logging_time,format="%Y-%m-%d %H:%M:%S.%f")
		PhysicalCellReadLog_Dasboard['@timestamp per 100 milliseconds'] = pd.to_datetime(PhysicalCellReadLog_Dasboard['@timestamp per 100 milliseconds'], format="%H:%M:%S.%f")
		
		# We are loading the logs from the ELK and it doesn't supply the year, month and day so we need to add them manually for the equality
		PhysicalCellReadLog_Dasboard['@timestamp per 100 milliseconds'] = PhysicalCellReadLog_Dasboard['@timestamp per 100 milliseconds'].map(lambda t: dt.datetime(2021, 4, 7, t.hour, t.minute, t.second, t.microsecond))
		PhysicalCellReadLog_Dasboard.set_index('@timestamp per 100 milliseconds',inplace=True)
		
		# Makes the columns names equal for comparing two DataFrames
		PhysicalCellReadLog_Dasboard.columns=PhysicalCellReadLog_agg[["block"]].columns
		
		# Check only values and shape without index which is the time, because of different split in every method, will print nothing if equal, excpetion for difference, Double checking the values
		assert_frame_equal(PhysicalCellReadLog_agg[["block"]].reset_index(drop=True), PhysicalCellReadLog_Dasboard.reset_index(drop=True))
		if not (np.array_equal(PhysicalCellReadLog_agg[["block"]].values,PhysicalCellReadLog_Dasboard.values)):
			print("Block_Reads Test Failed")
	
		print("Block_Reads Passed")
	
	def Channel_Switched_Read(self, dashboard_name):
		# Create Aggrigatoin on the json log, sum of page in every 100ms
		PhysicalCellProgramLog_agg = self.JsonLog_DF[self.JsonLog_DF.type=="ChannelSwitchToReadLog"].set_index('logging_time').resample('100ms').sum()
	
		# Get Path of the dashboard csv export from Kibana
		csv_file = self.de.get_dashboard_csv(self.dashboard_path, dashboard_name)
		PhysicalCellProgramLog_Dasboard = pd.read_csv(csv_file, dtype={'Count of records': float},thousands=',')
		os.remove(csv_file)
	
	    # Parse time from string to datetime by format
		self.JsonLog_DF .logging_time = pd.to_datetime(self.JsonLog_DF .logging_time,format="%Y-%m-%d %H:%M:%S.%f")
		PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'] = pd.to_datetime(PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'], format="%H:%M:%S.%f")
		
		# We are loading the logs from the ELK and it doesn't supply the year, month and day so we need to add them manually for the equality
		PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'] = PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'].map(lambda t: dt.datetime(2021, 4, 7, t.hour, t.minute, t.second, t.microsecond))
		PhysicalCellProgramLog_Dasboard.set_index('@timestamp per 100 milliseconds',inplace=True)
		
		# Makes the columns names equal for comparing two DataFrames
		PhysicalCellProgramLog_Dasboard.columns=PhysicalCellProgramLog_agg.columns
		
		# Check only values and shape without index which is the time, because of different split in every method, will print nothing if equal, excpetion for difference, Double checking the values
		assert_frame_equal(PhysicalCellProgramLog_agg.reset_index(drop=True), PhysicalCellProgramLog_Dasboard.reset_index(drop=True))
		if not (np.array_equal(PhysicalCellProgramLog_agg.values,PhysicalCellProgramLog_Dasboard.values)):
			print("Channel_Switched_Read Test Failed")
	
		print("Channel_Switched_Read Passed")
	
	def Channel_Switched_Write(self, dashboard_name):
		# Create Aggrigatoin on the json log, sum of page in every 100ms
		PhysicalCellProgramLog_agg = self.JsonLog_DF[self.JsonLog_DF.type=="ChannelSwitchToWriteLog"].set_index('logging_time').resample('100ms').sum()
	
		# Get Path of the dashboard csv export from Kibana
		csv_file = self.de.get_dashboard_csv(self.dashboard_path, dashboard_name)
		PhysicalCellProgramLog_Dasboard = pd.read_csv(csv_file, dtype={'Count of records': float},thousands=',')
		os.remove(csv_file)
	
	    # Parse time from string to datetime by format
		self.JsonLog_DF .logging_time = pd.to_datetime(self.JsonLog_DF .logging_time,format="%Y-%m-%d %H:%M:%S.%f")
		PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'] = pd.to_datetime(PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'], format="%H:%M:%S.%f")
		
		# We are loading the logs from the ELK and it doesn't supply the year, month and day so we need to add them manually for the equality
		PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'] = PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'].map(lambda t: dt.datetime(2021, 4, 7, t.hour, t.minute, t.second, t.microsecond))
		PhysicalCellProgramLog_Dasboard.set_index('@timestamp per 100 milliseconds',inplace=True)
		
		# Makes the columns names equal for comparing two DataFrames
		PhysicalCellProgramLog_Dasboard.columns=PhysicalCellProgramLog_agg.columns
		
		# Check only values and shape without index which is the time, because of different split in every method, will print nothing if equal, excpetion for difference, Double checking the values
		assert_frame_equal(PhysicalCellProgramLog_agg.reset_index(drop=True), PhysicalCellProgramLog_Dasboard.reset_index(drop=True))
		if not (np.array_equal(PhysicalCellProgramLog_agg.values,PhysicalCellProgramLog_Dasboard.values)):
			print("Channel_Switched_Write Test Failed")
	
		print("Channel_Switched_Write Passed")
	
	def Blocks_Erased(self, dashboard_name):
		# Create Aggrigatoin on the json log, sum of page in every 100ms
		PhysicalCellProgramLog_agg = self.JsonLog_DF[self.JsonLog_DF.type=="BlockEraseLog"].set_index('logging_time').resample('100ms').sum()
	
		# Get Path of the dashboard csv export from Kibana
		csv_file = self.de.get_dashboard_csv(self.dashboard_path, dashboard_name)
		PhysicalCellProgramLog_Dasboard = pd.read_csv(csv_file, dtype={'Sum of page': float},thousands=',')
		os.remove(csv_file)
	
	    # Parse time from string to datetime by format
		self.JsonLog_DF .logging_time = pd.to_datetime(self.JsonLog_DF .logging_time,format="%Y-%m-%d %H:%M:%S.%f")
		PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'] = pd.to_datetime(PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'], format="%H:%M:%S.%f")
		
		# We are loading the logs from the ELK and it doesn't supply the year, month and day so we need to add them manually for the equality
		PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'] = PhysicalCellProgramLog_Dasboard['@timestamp per 100 milliseconds'].map(lambda t: dt.datetime(2021, 4, 7, t.hour, t.minute, t.second, t.microsecond))
		PhysicalCellProgramLog_Dasboard.set_index('@timestamp per 100 milliseconds',inplace=True)
		
		# Makes the columns names equal for comparing two DataFrames
		PhysicalCellProgramLog_Dasboard.columns=PhysicalCellProgramLog_agg[["page"]].columns
		
		# Check only values and shape without index which is the time, because of different split in every method, will print nothing if equal, excpetion for difference, Double checking the values
		assert_frame_equal(PhysicalCellProgramLog_agg[["page"]].reset_index(drop=True), PhysicalCellProgramLog_Dasboard.reset_index(drop=True))
		if not (np.array_equal(PhysicalCellProgramLog_agg[["page"]].values,PhysicalCellProgramLog_Dasboard.values)):
			print("Check_Page_Writes Test Failed")
	
		print("Check_Page_Writes Passed")

