#!/usr/bin/python -tu
#
# Put test results into a DB.
#
# Copyright (C) 2007 OSD Team <pvfs-osd@osc.edu>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 2 of the License.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import re
import os
import sqlite3
from table import table 

class results(table):
	def __init__(self, cur, name, ref):
		table.__init__(self, cur, name, ref)

	def create(self):
		q = """
			CREATE TABLE IF NOT EXISTS %s (
				tid INTEGER NOT NULL,
				date TEXT NOT NULL,
				version INTEGER NOT NULL,
				testid INTEGER NOT NULL,
				mu FLOAT,
				sd FLOAT,
				median FLOAT,
				units TEXT,
				PRIMARY KEY (tid),
				FOREIGN KEY (testid) REFERENCES %s (testid)
			); """ % (self.name, self.ref.name)
		self.cur.execute(q)

	def insert(self, date, ver, testid, mu, sd, median=None, units='us'):
		if median == None:
			q = """ 
				INSERT INTO %s VALUES (NULL, '%s', %d, %d, %f, %f, NULL, '%s');
				""" % (self.name, date, ver, testid, mu, sd, units)
		else:
			q = """
				INSERT INTO %s VALUES (NULL, '%s', %d, %d, %f, %f, %f, '%s');
				""" % (self.name, date, ver, testid, mu, sd, median, units)
		self.cur.execute(q) 

	def delete(self, date, version, testid):
		q = """ DELETE FROM %s WHERE date = '%s' AND version = %d AND 
					testid = %d;""" % (self.name, date, version, testid)
		self.cur.execute(q) 

