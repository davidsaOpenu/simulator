#!/usr/bin/python

import sys
import json
import os
from datetime import datetime

MODE_R = 0
MODE_W = 1
MODE_RW = 2

PAGES_PER_BLOCK = 10

def get_new_stats():
    return {
            'LogicalCellProgramLog': 0
            'PhysicalCellProgramLog': 0,
            'PhysicalCellReadLog': 0,
            'RegisterReadLog': 0,
            'RegisterWriteLog': 0,        
            'ChannelSwitchToReadLog': 0,
            'ChannelSwitchToWriteLog': 0,
    }

def predict_test_stats(mode, blocks_num):
    stats = get_new_stats()

    if mode == MODE_R:
        stats['PhysicalCellReadLog'] = PAGES_PER_BLOCK * blocks_num
    elif mode == MODE_W:
        pass
    else:
        pass

    return stats

def get_test_stats(log_lines):
    current_json_data = map(lambda x: json.loads(x), log_lines)
    stats = get_new_stats()

    for doc in current_json_data:
        t = doc['type']
        stats[t] += 1

    return stats

if __name__ == "__main__":
    USAGE= '''check_log_validity.py <LOGS_PATH> <BLOCKS_NUM> <MODE>
       @ LOGS_PATH: The directory containing the log files
       @ BLOCKS_NUM: Number of blocks the test worked on
       @ MODE: one of r=0, w=1, rw=2'''

    if len(sys.argv) != 4:
        print (USAGE)
        exit(-1)

    logs_path = sys.argv[1]
    blocks_num = int(sys.argv[2])
    mode = int(sys.argv[3])

    # Get all the log files from logs_path and sort them by time
    log_files = filter(lambda x: 'elk_log_file' in x, os.listdir(logs_path))
    log_files = list(log_files)

    def time_sorter(x, y):
        fmt = "%Y-%m-%d_%H:%M:%S"
        dt_x = datetime.strptime(x, fmt)
        dt_y = datetime.strptime(y, fmt)
        return x < y
    log_files.sort(cmp=time_sorter)

    current_json_data = []
    for log_file in log_files:
        with open(log_file, 'r') as f:
            current_json_data += f.readlines()

    # Predict the json data for the event.. returns a list of json documents.
    predicted_test_stats = predict_test_stats(mode, blocks_num)

    # Get the actual stats of the test
    actual_test_stats = get_test_stats(current_json_data)

    # Compare the stats
    for key in predicted_test_stats.keys():
        if predicted_test_stats[key] != actual_test_stats[key]:
            print ("[+] TEST FAILED on key %s".format(key))
            print ("\t{}!={}".format(predicted_test_stats[key], actual_test_stats[key]))
            exit(-1)

    print ("[+] TEST PASSED for {} passed successfully!".format(fio_file_name))
    exit(0)
