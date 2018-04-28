#!/bin/bash
source ./builder.sh

# Make a fresh copy
evssim_qemu_fresh_image

# Run qemu detached
evssim_qemu_detached

# Run ssh inside
evssim_guest bash
