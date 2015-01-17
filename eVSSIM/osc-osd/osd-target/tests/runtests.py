#!/usr/bin/python -tu
#
# Run some tests.
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
import time
import sqlite3
import commands
from results import results
from tests import *

class runtests:
    def __init__(self, dbname=None):
        self.__checkmakedef()
        if dbname == None:
            self.con = sqlite3.connect("results.db")
        else: 
            self.con = sqlite3.connect(dbname)
        self.con.check_same_thread = False
        self.con.enable_callback_tracebacks = True
        self.cur = self.con.cursor()
        self.cur.execute("PRAGMA auto_vacuum = 1;")
        self.gentestid = gentestid(self.cur, "gentestid")
        self.results = results(self.cur, "results", self.gentestid)
        ref = (self.gentestid, self.results)
        self.create = create(self.cur, "create_", ref, "create")
        self.setattr = setattr(self.cur, "setattr", ref, "setattr")
        self.getattr = getattr(self.cur, "getattr", ref, "getattr")
        self.list = getattr(self.cur, "list", ref, "list")
        self.listattr = listattr(self.cur, "listattr", ref, "list")
        self.query = query(self.cur, "query", ref, "query")
        self.sma = sma(self.cur, "sma", ref, "set_member_attributes")
        self.coll = coll(self.cur, "coll", ref, "time-db")
        self.obj = obj(self.cur, "obj", ref, "time-db")
        self.attr = attr(self.cur, "attr", ref, "time-db")
        self.date = time.strftime('%F-%T')
        v = commands.getoutput('svn info | grep "^Rev" | cut -f2 -d\ ')
        self.version = int(v)

    def create_tests(self):
        self.gentestid.create()
        self.results.create()
        self.create.create()
        self.setattr.create()
        self.getattr.create()
        self.list.create()
        self.listattr.create()
        self.query.create()
        self.sma.create()
        self.coll.create()
        self.obj.create()
        self.attr.create()
        self.con.commit()

    def drop_tests(self):
        self.create.drop()
        self.setattr.drop()
        self.getattr.drop()
        self.list.drop()
        self.listattr.drop()
        self.query.drop()
        self.sma.drop()
        self.coll.drop()
        self.obj.drop()
        self.attr.drop()
        self.results.drop()
        self.gentestid.drop()
        self.con.commit()

    def populate_tests(self):
        self.con.isolation_level = "DEFERRED"
        self.create.populate()
        self.setattr.populate()
        self.getattr.populate()
        self.list.populate()
        self.listattr.populate()
        self.query.populate()
        self.sma.populate()
        self.coll.populate()
        self.obj.populate()
        self.attr.populate()
        self.con.commit()

    def populate_results(self):
        self.con.isolation_level = "DEFERRED"
        tests = [
                self.create, self.setattr, self.getattr, self.list,
                self.listattr, self.query, self.sma, self.coll, 
                self.obj, self.attr
                ]
        for t in tests:
            for tid, mu, sd, u in t.runall():
                self.results.insert(self.date, self.version, tid, mu, sd, 
                        None, u)
        self.con.commit()

    def __checkmakedef(self, compile=False):
        try:
            make = '../../Makedefs'
            f = open(make)
            opt = re.compile("^OPT :=", re.IGNORECASE)
            dbg = re.compile(".*-g.*", re.IGNORECASE)
            copt = 0
            for l in f.readlines():
                if opt.search(l):
                    assert not dbg.search(l), 'Incorrect compile opts: '+l
                    copt = 1
            assert copt == 1, 'OPT not defined in ' + make
            f.close()
            if compile:
                assert os.system('make clean && make') == 0, 'Make failed'
        except IOError:
            print 'unknown file:' + make
            os._exit(1)


if __name__ == "__main__":
    rt = runtests()
    #rt.drop_tests()
    #rt.create_tests()
    #rt.populate_tests()
    #rt.populate_results()

    #rt.timedb.drop()
    #rt.timedb.create()
    #rt.timedb.populate()
    #rt.timedb.populateobj()
    #rt.timedb.runall()
    #rt.timedb.runobj()
    #for tid, mu, sd, u in rt.timedb.runobj():
    #    rt.results.insert(rt.date, rt.version, tid, mu, sd, None, u)

    #rt.coll.drop()
    #rt.coll.create()
    #rt.coll.populate()
    #rt.coll.runall()
    #for tid, mu, sd, u in rt.coll.runall():
    #    rt.results.insert(rt.date, rt.version, tid, mu, sd, None, u)

    #rt.obj.drop()
    #rt.obj.create()
    #rt.obj.populate()
    #rt.obj.runall()
    #for tid, mu, sd, u in rt.obj.runall():
    #    rt.results.insert(rt.date, rt.version, tid, mu, sd, None, u)

    #rt.attr.drop()
    #rt.attr.create()
    #rt.attr.populate()
    rt.attr.runtests(rt.attr.gettest('attrdirpg'))
    #for tid, mu, sd, u in rt.attr.runall():
    #    rt.results.insert(rt.date, rt.version, tid, mu, sd, None, u)

    #rt.con.commit()

