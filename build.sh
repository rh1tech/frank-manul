#!/bin/bash
# Build Manul for RP2350 (M2 board only)
rm -rf ./build
mkdir build
cd build
cmake ..
make -j4
