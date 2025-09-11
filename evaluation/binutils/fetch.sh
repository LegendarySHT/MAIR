#!/bin/bash
export PROJ=binutils
URL="https://github.com/bminor/binutils-gdb.git"


# Use depth 1 to avoid cloning the history
git clone $URL repo --depth 1
