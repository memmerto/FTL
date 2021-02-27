#!/bin/sh
# Pi-hole: A black hole for Internet advertisements
# (c) 2020 Pi-hole, LLC (https://pi-hole.net)
# Network-wide ad blocking via your own hardware.
#
# FTL Engine
# Build script for FTL
#
# This file is copyright under the latest version of the EUPL.
# Please see LICENSE file for your rights under this license.

# Abort script if one command returns a non-zero value
set -e

# Prepare build environment
if [ "${1}" = "clean" ]; then
    rm -rf cmake/
    exit 0
fi

# Configure build, pass CMake CACHE entries if present
# Wrap multiple options in "" as first argument to ./build.sh:
#     ./build.sh "-DA=1 -DB=2" install
mkdir -p cmake
cd cmake
if [[ "${1}" == "-D"* ]]; then
    cmake "${1}" ..
else
    cmake ..
fi

case $(uname) in
FreeBSD) NCPU=$(sysctl -n hw.ncpu);;
Linux) NCPU=$(nproc);;
*) NCPU=1;;
esac

# Build the sources
cmake --build . -- -j $NCPU

# If we are asked to install, we do this here
# Otherwise, we simply copy the binary one level up
if [ "${1}" = "install" -o "${2} = "install" ]; then
    sudo make install
else
    cp pihole-FTL ../
fi
