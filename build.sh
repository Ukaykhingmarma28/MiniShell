#!/bin/bash
set -e

# Create build dir if missing
if [ ! -d "build" ]; then
  echo "Creating build directory..."
  mkdir build
fi

# Configure and build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
echo "Build complete. Run ./build/minishell"
