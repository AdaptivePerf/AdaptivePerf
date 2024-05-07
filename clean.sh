#!/bin/bash
if [[ $1 == "-h" || $1 == "--help" ]]; then
    echo "Script for cleaning all AdaptivePerf build files."
    echo "Usage: ./clean.sh"
    exit 0
fi

make clean

set -v
rm -f adaptiveperf-server
rm -rf build
