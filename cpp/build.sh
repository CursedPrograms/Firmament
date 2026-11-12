#!/usr/bin/env bash
# Builds Firmament in C++ on Linux (cmake, a C++17 compiler, libsdl2-dev), with stable-diffusion.cpp built as a shared
# library in SDCPP/build-cuda (cmake -DSD_CUDA=ON -DSD_BUILD_SHARED_LIBS=ON).
set -e
cd "$(dirname "$0")"
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSDCPP="${SDCPP:-$HOME/stable-diffusion.cpp}"
cmake --build build -j
echo "Built: build/Firmament"
