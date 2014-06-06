DD tests to determine nvme behaviour
=========

Purpose of experiemnt is to understand nvme device behaviour using the nvmeqemu project.
nvmequmo is compiled with original debug logs configured and additional code to dump all read and write memory (not this generates quite long logs).

Tests are conducted run using dd, using different block sizes & count of blocks.
dd is used direct mode against in order to bypass all caching mechanism.
input file is just (also in the repository) a dump file of sequential ascii numbers so its easier to read memory contents.
example for the tests command is 
`dd if=seq512.dat of=/dev/nvme0n1 bs=16k count=7 oflag=direct`

Test result logs are provided in this directory.
Each test log file is composed of sequential number, block size and count of writes.
For example "dd_1_block_4k_count_16_direct" is the #1, using block size of 4 and 16 block writes (to a sum up of 4l*16-64k).

Also provided is a simple `anaylize.sh` script to digest the logs for various paramters.
Currenlty displayed is the distribution of read/writes, PRP1/PRP2/prp_list & submission queues usage.

Experiment findings:
 * After all series of writes there is 83-84 read operation, whose purpose is still a mystery.
 * PRP
  * on 4k block size writes only PRP1 is used
  * on 8k block size writes PRP1 and PRP2 is used
  * on 16k block size writes PRP1 and prp_list is used
 * starting the size of 128k, non single submission queue is used (still not known why, as we had expected a single sq to be used). 


Following are the analyse results of the tests logs.

dd_1_block_4k_count_16_direct
----------------------------------
Read 83
	sqid 1 0
	sqid 2 4
	sqid 3 70
	sqid 4 9
	PRP1 83
	PRP2 0
	prp_list 0
Write 16
	sqid 1 0
	sqid 2 0
	sqid 3 0
	sqid 4 16
	PRP1 16
	PRP2 0
	prp_list 0

dd_3_block_16k_count_4_direct
----------------------------------
Read 83
	sqid 1 39
	sqid 2 0
	sqid 3 44
	sqid 4 0
	PRP1 83
	PRP2 0
	prp_list 0
Write 16
	sqid 1 0
	sqid 2 4
	sqid 3 0
	sqid 4 0
	PRP1 4
	PRP2 0
	prp_list 4

dd_4_block_16k_count_16_direct
----------------------------------
Read 83
	sqid 1 0
	sqid 2 0
	sqid 3 83
	sqid 4 0
	PRP1 83
	PRP2 0
	prp_list 0
Write 64
	sqid 1 12
	sqid 2 0
	sqid 3 3
	sqid 4 1
	PRP1 16
	PRP2 0
	prp_list 16

dd_4_block_16k_count_5_direct
----------------------------------
Read 83
	sqid 1 39
	sqid 2 44
	sqid 3 0
	sqid 4 0
	PRP1 83
	PRP2 0
	prp_list 0
Write 20
	sqid 1 0
	sqid 2 0
	sqid 3 5
	sqid 4 0
	PRP1 5
	PRP2 0
	prp_list 5

dd_6_block_16k_count_6_direct
----------------------------------
Read 83
	sqid 1 0
	sqid 2 83
	sqid 3 0
	sqid 4 0
	PRP1 83
	PRP2 0
	prp_list 0
Write 24
	sqid 1 0
	sqid 2 0
	sqid 3 6
	sqid 4 0
	PRP1 6
	PRP2 0
	prp_list 6

dd_7_block_16k_count_7_direct
----------------------------------
Read 83
	sqid 1 0
	sqid 2 55
	sqid 3 0
	sqid 4 28
	PRP1 83
	PRP2 0
	prp_list 0
Write 28
	sqid 1 0
	sqid 2 0
	sqid 3 0
	sqid 4 7
	PRP1 7
	PRP2 0
	prp_list 7

dd_8_block_16k_count_8_direct
----------------------------------
Read 83
	sqid 1 0
	sqid 2 17
	sqid 3 66
	sqid 4 0
	PRP1 83
	PRP2 0
	prp_list 0
Write 32
	sqid 1 5
	sqid 2 0
	sqid 3 3
	sqid 4 0
	PRP1 8
	PRP2 0
	prp_list 8

dd_9_block_16k_count_256_direct
----------------------------------
Read 84
	sqid 1 0
	sqid 2 22
	sqid 3 0
	sqid 4 62
	PRP1 84
	PRP2 0
	prp_list 0
Write 129
	sqid 1 11
	sqid 2 0
	sqid 3 18
	sqid 4 4
	PRP1 33
	PRP2 0
	prp_list 32
