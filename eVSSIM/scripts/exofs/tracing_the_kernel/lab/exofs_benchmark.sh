#!/bin/bash

MOUNT_POINT="/mnt/exofs0"
OUTPUT_FILE="fio_benchmark_summary.txt"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================" | tee $OUTPUT_FILE
echo "EXOFS NVMe KV Benchmark Results" | tee -a $OUTPUT_FILE
echo "Date: $(date)" | tee -a $OUTPUT_FILE
echo "========================================" | tee -a $OUTPUT_FILE
echo "" | tee -a $OUTPUT_FILE

# Test 1: 1000 small files (4KB each) - CREATE
echo -e "${BLUE}[1/6] Creating 1000 small files (4KB each)...${NC}"
echo "Test 1: Create 1000 x 4KB files" >> $OUTPUT_FILE
START=$(date +%s.%N)
fio --name=test --directory=$MOUNT_POINT --rw=write --bs=4k --size=4k --numjobs=1000 --ioengine=sync --group_reporting > /tmp/fio_output.txt 2>&1
END=$(date +%s.%N)
TIME1=$(echo "$END - $START" | bc)
IOPS1=$(grep "WRITE:" /tmp/fio_output.txt | awk '{print $3}' | cut -d'=' -f2 | cut -d',' -f1)
BW1=$(grep "WRITE:" /tmp/fio_output.txt | awk '{print $2}' | cut -d'=' -f2 | cut -d',' -f1)
echo "  Time: ${TIME1}s | Bandwidth: $BW1 | IOPS: $IOPS1" | tee -a $OUTPUT_FILE
echo "" | tee -a $OUTPUT_FILE

# Test 2: 1000 small files - UPDATE
echo -e "${BLUE}[2/6] Updating 1000 small files...${NC}"
echo "Test 2: Update 1000 x 4KB files (random write)" >> $OUTPUT_FILE
START=$(date +%s.%N)
fio --name=test --directory=$MOUNT_POINT --rw=randwrite --bs=4k --size=4k --numjobs=1000 --ioengine=sync --group_reporting --overwrite=1 > /tmp/fio_output.txt 2>&1
END=$(date +%s.%N)
TIME2=$(echo "$END - $START" | bc)
IOPS2=$(grep "WRITE:" /tmp/fio_output.txt | awk '{print $3}' | cut -d'=' -f2 | cut -d',' -f1)
BW2=$(grep "WRITE:" /tmp/fio_output.txt | awk '{print $2}' | cut -d'=' -f2 | cut -d',' -f1)
echo "  Time: ${TIME2}s | Bandwidth: $BW2 | IOPS: $IOPS2" | tee -a $OUTPUT_FILE
echo "" | tee -a $OUTPUT_FILE

# Test 3: Delete small files
echo -e "${BLUE}[3/6] Deleting 1000 small files...${NC}"
echo "Test 3: Delete 1000 x 4KB files" >> $OUTPUT_FILE
START=$(date +%s.%N)
rm -rf $MOUNT_POINT/test.*
END=$(date +%s.%N)
TIME3=$(echo "$END - $START" | bc)
echo "  Time: ${TIME3}s" | tee -a $OUTPUT_FILE
echo "" | tee -a $OUTPUT_FILE

# Test 4: 10 big files (10MB each) - CREATE
echo -e "${BLUE}[4/6] Creating 10 big files (10MB each)...${NC}"
echo "Test 4: Create 10 x 10MB files" >> $OUTPUT_FILE
START=$(date +%s.%N)
fio --name=bigtest --directory=$MOUNT_POINT --rw=write --bs=1M --size=10M --numjobs=10 --ioengine=sync --group_reporting > /tmp/fio_output.txt 2>&1
END=$(date +%s.%N)
TIME4=$(echo "$END - $START" | bc)
IOPS4=$(grep "WRITE:" /tmp/fio_output.txt | awk '{print $3}' | cut -d'=' -f2 | cut -d',' -f1)
BW4=$(grep "WRITE:" /tmp/fio_output.txt | awk '{print $2}' | cut -d'=' -f2 | cut -d',' -f1)
echo "  Time: ${TIME4}s | Bandwidth: $BW4 | IOPS: $IOPS4" | tee -a $OUTPUT_FILE
echo "" | tee -a $OUTPUT_FILE

# Test 5: 10 big files - UPDATE
echo -e "${BLUE}[5/6] Updating 10 big files...${NC}"
echo "Test 5: Update 10 x 10MB files (random write)" >> $OUTPUT_FILE
START=$(date +%s.%N)
fio --name=bigtest --directory=$MOUNT_POINT --rw=randwrite --bs=1M --size=10M --numjobs=10 --ioengine=sync --group_reporting --overwrite=1 > /tmp/fio_output.txt 2>&1
END=$(date +%s.%N)
TIME5=$(echo "$END - $START" | bc)
IOPS5=$(grep "WRITE:" /tmp/fio_output.txt | awk '{print $3}' | cut -d'=' -f2 | cut -d',' -f1)
BW5=$(grep "WRITE:" /tmp/fio_output.txt | awk '{print $2}' | cut -d'=' -f2 | cut -d',' -f1)
echo "  Time: ${TIME5}s | Bandwidth: $BW5 | IOPS: $IOPS5" | tee -a $OUTPUT_FILE
echo "" | tee -a $OUTPUT_FILE

# Test 6: Delete big files
echo -e "${BLUE}[6/6] Deleting 10 big files...${NC}"
echo "Test 6: Delete 10 x 10MB files" >> $OUTPUT_FILE
START=$(date +%s.%N)
rm -rf $MOUNT_POINT/bigtest.*
END=$(date +%s.%N)
TIME6=$(echo "$END - $START" | bc)
echo "  Time: ${TIME6}s" | tee -a $OUTPUT_FILE
echo "" | tee -a $OUTPUT_FILE

# Summary
echo "========================================" | tee -a $OUTPUT_FILE
echo "SUMMARY" | tee -a $OUTPUT_FILE
echo "========================================" | tee -a $OUTPUT_FILE
printf "%-40s %10s\n" "Operation" "Time (s)" | tee -a $OUTPUT_FILE
echo "----------------------------------------" | tee -a $OUTPUT_FILE
printf "%-40s %10.2f\n" "Create 1000 x 4KB files" "$TIME1" | tee -a $OUTPUT_FILE
printf "%-40s %10.2f\n" "Update 1000 x 4KB files" "$TIME2" | tee -a $OUTPUT_FILE
printf "%-40s %10.2f\n" "Delete 1000 x 4KB files" "$TIME3" | tee -a $OUTPUT_FILE
printf "%-40s %10.2f\n" "Create 10 x 10MB files" "$TIME4" | tee -a $OUTPUT_FILE
printf "%-40s %10.2f\n" "Update 10 x 10MB files" "$TIME5" | tee -a $OUTPUT_FILE
printf "%-40s %10.2f\n" "Delete 10 x 10MB files" "$TIME6" | tee -a $OUTPUT_FILE
echo "========================================" | tee -a $OUTPUT_FILE

echo ""
echo -e "${GREEN}Benchmark complete! Results saved to $OUTPUT_FILE${NC}"