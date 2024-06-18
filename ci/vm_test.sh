#!/bin/bash
./build.sh -DENABLE_TESTS=ON
cd build
ctest
