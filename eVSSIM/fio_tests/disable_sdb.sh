#!/bin/bash

sed -i 's@.*filename=/dev/sdb.*@#filename=/dev/sdb@g' fio_jobs/*.fio
sed -i 's@.*filename=/dev/sdb.*@#filename=/dev/sdb@g' fio_jobs/*.fio 
