#!/bin/bash

sudo apt-get install software-properties-common
echo -ne '\n' | sudo add-apt-repository ppa:ansible/ansible
sudo apt-get update
while true; do echo 'Y'; done | sudo apt-get install ansible
