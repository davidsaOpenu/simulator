#!/bin/bash

start_rev=695
util_first=2918

svn_start_update()
{
	cd $1
	svn update -r $2
	cd ..
}

# git-reset --hard BEGIN

svn_start_update osd-target $start_rev
svn_start_update osd-util $util_first
