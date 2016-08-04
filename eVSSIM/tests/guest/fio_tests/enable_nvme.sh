#!/bin/bash

sed -i 's@.*filename=/dev/nvme0n1.*@filename=/dev/nvme0n1@g' fio_jobs/*.fio
sed -i 's@.*filename=/dev/nvme0n1.*@filename=/dev/nvme0n1@g' fio_jobs/*.fio 
