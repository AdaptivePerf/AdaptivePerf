#!/bin/bash
set -e

function echo_main() {
    if [[ $2 -eq 1 ]]; then
        echo -e "\033[1;31m==> $1\033[0m"
    else
        echo -e "\033[1;32m==> $1\033[0m"
    fi
}

function error() {
    echo_main "An error has occurred!" 1
    exit 2
}

trap "error" ERR

if [[ $1 == "-h" || $1 == "--help" ]]; then
    echo "Script for installing complete AdaptivePerf after building it."
    echo "Usage: ./install.sh [optional installation prefix]"
    echo "Default prefix is /usr/local."
    exit 0
fi

if [[ ! -f adaptiveperf || ! -f adaptiveperf-server ]]; then
    echo_main "No adaptiveperf and/or adaptiveperf-server detected!" 1
    echo_main "Please put them inside this directory or run build.sh." 1
    exit 1
fi

if [[ $1 == "" ]]; then
    prefix=/usr/local
else
    prefix=$1
fi

echo_main "Installing adaptiveperf..."
make install prefix=$prefix

echo_main "Installing adaptiveperf-server..."
cp adaptiveperf-server $prefix/bin

echo_main "Installing AdaptivePerf perf scripts..."
cd src/scripts
make install

echo_main "Done! You can use AdaptivePerf now, e.g. run \"adaptiveperf --help\"."
