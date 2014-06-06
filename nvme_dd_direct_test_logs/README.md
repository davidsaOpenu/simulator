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
Analyse results of the tests logs is in `analyze_results.md`

Experiment findings:
 * After all series of writes there is 83-84 read operation, whose purpose is still a mystery.
 * PRP
  * on 4k block size writes only PRP1 is used
  * on 8k block size writes PRP1 and PRP2 is used
  * on 16k block size writes PRP1 and prp_list is used
 * for block size of 16k, starting the size of 128k, non single submission queue is used (still not known why, as we had expected a single sq to be used). for block sizes of 4 & 8, non single submission queue is used starting on TBD & TBD, respectively.


