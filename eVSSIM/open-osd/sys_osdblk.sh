#!/bin/bash

dir=/sys/block/osdblk0/queue

chap()
{
	echo -n "$1 =	"
	cat $dir/$1
}

chap hw_sector_size
# chap iosched
chap iostats
chap logical_block_size
chap max_hw_sectors_kb
chap max_sectors_kb
chap minimum_io_size
chap nomerges
chap nr_requests
chap optimal_io_size
chap physical_block_size
chap read_ahead_kb
chap rotational
chap rq_affinity
chap scheduler
