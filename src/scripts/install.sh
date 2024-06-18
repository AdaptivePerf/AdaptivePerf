#!/bin/bash
set -e

# This script is not meant to be run directly!
# Please use "make install prefix=<install prefix>" or "make uninstall".

if [[ "$1" == "uninstall" ]]; then
    if [[ -f prefix.txt ]]; then
        prefix=$(cat prefix.txt)
        rm "$prefix"/adaptiveperf-*.py
        rm prefix.txt
    else
        echo "No prefix.txt found! Have you installed the scripts before?"
        exit 1
    fi
else
    cp adaptiveperf-syscall-process.py adaptiveperf-process.py "$1"
    echo "$1" > prefix.txt
fi
