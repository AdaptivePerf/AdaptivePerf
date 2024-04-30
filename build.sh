#!/bin/bash
set -e

function echo_main() {
    if [[ $2 -eq 1 ]]; then
        echo -e "\033[1;31m==> $1\033[0m"
    else
        echo -e "\033[1;32m==> $1\033[0m"
    fi
}

function echo_sub() {
    if [[ $2 -eq 1 ]]; then
        echo -e "\033[0;31m-> $1\033[0m"
    else
        echo -e "\033[0;34m-> $1\033[0m"
    fi
}

function error() {
    echo_main "An error has occurred!" 1
    exit 2
}

trap "error" ERR

echo_main "Checking build dir..."
if [[ -d build ]]; then
    echo_sub "Non-empty build dir detected! Please run clean.sh first." 1
    echo_sub "If you want to rebuild the server, go to the build dir and run \"./make.sh\" to get the adaptiveperf-server binary."
    echo_sub "If you want to rebuild the bash frontend, run \"make\" to get the adaptiveperf shell script."
    exit 1
else
    mkdir build
    echo "#!/bin/bash" > build/make.sh
    echo "cmake --build . && mv adaptiveperf-server ../" >> build/make.sh
    chmod +x build/make.sh
fi

echo_main "Building adaptiveperf-server..."
cd build
cmake ../src/server $@
cmake --build .
mv adaptiveperf-server ../

echo_main "Building adaptiveperf..."
cd ..
make

echo_main "Done! You can run install.sh now."
