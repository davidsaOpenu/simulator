#!/bin/bash
#

dest=root@10.65.163.104:/root/

scp tgt/usr/tgtd 	$dest
scp tgt/usr/tgtadm 	$dest
scp tgt/usr/tgtimg 	$dest

scp /usr/local/lib/libsqlite3.so.8 $dest
