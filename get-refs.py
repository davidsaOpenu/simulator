#!/usr/bin/python
#
# Gets refs for all open (i.e. not closed or abandoned) changes in Gerrit
#
project = 'davidsaOpenu/simulator'
status = 'open'

import sys, json, urllib

# Make request and parse
file = urllib.urlopen('https://review.gerrithub.io/changes/?q=project:%s+status:%s&o=CURRENT_REVISION' % (project, status))
file.readline()  # skip over cookie
changes = json.load(file)
for change in changes:
    rev = change['revisions'].itervalues().next()
    print rev['ref']
