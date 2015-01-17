#!/usr/bin/python -tu
#
# Table class.
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
import commands

class table:
	def __init__(self, cur, name, ref=None):
		assert cur != None, ("Invalid value None for cur")
		assert name != None, ("Invalid value None for name")
		self.cur = cur
		self.name = name
		self.ref = ref #ref: tables on which this one depends

	def create(self):
		raise NotImplementedError

	def drop(self):
		q = "DROP TABLE IF EXISTS %s;" % self.name
		self.cur.execute(q)

	def insert(self):
		raise NotImplementedError

	def delete(self):
		raise NotImplementedError

	def getall(self):
		q = " SELECT * FROM %s;" % self.name
		self.cur.execute(q)
		return self.cur.fetchall()

