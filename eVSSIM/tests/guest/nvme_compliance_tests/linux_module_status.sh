#!/bin/sh

MODULE="$1"

if lsmod | grep -w $MODULE ; then
  echo "$MODULE is loaded!"
  exit 0
else
  echo "$MODULE is not loaded!"
  exit 1
fi

