#!/bin/bash

sudo apt-get install software-properties-common
echo -ne '\n' | sudo add-apt-repository ppa:ansible/ansible
sudo apt-get update
echo -ne 'Y' | sudo apt-get install ansible
