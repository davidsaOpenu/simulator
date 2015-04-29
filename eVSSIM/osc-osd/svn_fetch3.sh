#!/bin/bash
# A dedicated script to pull from osc's OSD svn project into a single git tree
# it is asumed that an initial svn was checkout at each sub-project and an
# initial parent git tree was created. It will incrementally pull new commits
# from svn into current git HEAD. Note that this will always succeed and no
# merge conflicts will ever exist since git-checkout files are over written by
# new svn files
#
# For clean merge-less pull start with: (No additional open-osd code)
#   []$ git checkout osc
#   []$ git cherry-pick FOR_SVN_FETCH3 
#   (Above is for missing .gitignore, without it will not work)
# osc branch now holds the new patches, cherry-rebase over master

commit_txt=`pwd`/commit.txt
start_rev=695
util_first=2918

log2revlist()
{
awk -v a=$1 -v b=$2 '
	BEGIN {
	}
	
	/\|/ {
		rev = $1;
		sub("^r", "", rev);
		printf("%s\n", rev);
		next;
	}
	
	END {
	}
'
}

# get_rev_list_folder folder
# $1=folder
get_rev_list_folder()
{
cd $1
	rs=`svn info | grep "Revision:"`
	first=${rs/Revision: /}
	(svn log -q -r $first:HEAD | log2revlist)
cd ..
}

get_all_revs()
{
get_rev_list_folder osd-target
get_rev_list_folder osd-util
}

echo "$0 getting list of revisions to update"

list=$(get_all_revs | sort -n | uniq)

echo Will update: $list

# convert an osc user name to email
# $1=username
osc_user_to_mail()
{
	case $1 in
	ananth)
		echo "Ananth Devulapalli <$1@osc.edu>"
		;;
	pw)
		echo "Pete Wyckoff <$1@osc.edu>"
		;;
	dennis)
		echo "Dennis Dalessandro <$1@osc.edu>"
		;;
	alin)
		echo "Nawab Ali <$1@osc.edu>"
		;;
	dx)
		echo "Da Xu <$1@osc.edu>"
		;;
	xxx)
		echo "Template User <$1@osc.edu>"
		;;
	*)
		echo "$1 <$1@osc.edu>"
		;;
	esac
}

# parse key/attr from svn log --xml file. (with one entry). By means of
# xmlstarlet. (yum install xmlstarlet)
# $1=key/attr $2=log.xml
xml_get_logentry()
{
	xmlstarlet sel -T -t -m /log/logentry -v $1 $2
}

# Put extra newline after first line.
# TODO:
#	1. Split at "."
#	2. Truncate lines longer then 60 chars, at a word boundry but
#	   then repeat the first line inside body
msg2title_body()
{
awk '
	{
		if (!firstline) {
			firstline = $0;
			printf("%s\n\n", firstline);
		}
		else {
			print;
		}
		next;
	}
'
}

# convert svn log to a git log message.
# $1=revision#
svn_logmsg()
{
	svn log --xml -r $1 > log.xml

	echo "svn r$1: $(xml_get_logentry 'msg' log.xml | msg2title_body)" > $commit_txt
	echo >> $commit_txt

	echo "SVN Date: $(xml_get_logentry 'date' log.xml)" >> $commit_txt
	echo "Auto-commited-by: $0" >> $commit_txt

#	return the author
	a="$(xml_get_logentry 'author' log.xml)"
	echo "Signed-off-by: $(osc_user_to_mail $a)" >> $commit_txt
	echo $a
}

# author is used for two things. 1 - to return the commit author
# 2 - to signal that we allready have a commit log message
author=""

# svn_update_folder $1=folder $2=revision
svn_update_folder()
{
	echo " cd $1    [$2]"
	cd $1

	svn update -r $2
	if [ -z "$author" ]; then
		author=$(svn_logmsg $2)
	fi

echo "DEBUG ["
cat log.xml
echo "] GUBED"

	cd ..
}

git_add_files()
{
	cd $1

	# add any new files (FIXME: use find)
	for f in * */* ; do
		git-add $f 2> /dev/null
	done

	cd ..
}

for i in $list; do

	if [ "$1" == "--single-step" ]; then
		read -p "single-step $i >>> "
	fi

	author=""

	svn_update_folder osd-target $i
	git_add_files osd-target

	if [ $(( $i >= $util_first )) != 0 ]; then
		svn_update_folder osd-util $i;
		git_add_files osd-util
	fi

echo "DEBUG ["
cat $commit_txt
echo "] GUBED"

	if [ -n "$author" ]; then
		git-commit -s -a --file=$commit_txt \
			--author="\"$(osc_user_to_mail $author)\""
	else
		echo "Nothing to commit"
	fi
done

rm $commit_txt
