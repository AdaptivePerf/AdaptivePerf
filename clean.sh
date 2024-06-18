#!/bin/bash

# AdaptivePerf: comprehensive profiling tool based on Linux perf
# Copyright (C) CERN. See LICENSE for details.

if [[ $1 == "-h" || $1 == "--help" ]]; then
    echo "Script for cleaning all AdaptivePerf build files."
    echo "Usage: ./clean.sh"
    exit 0
fi

set -v
rm -f adaptiveperf
rm -f libaperfserv.so adaptiveperf-server
rm -rf build
