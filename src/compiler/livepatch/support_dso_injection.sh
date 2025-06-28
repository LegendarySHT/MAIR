#!/bin/bash

# TODO: design a more wise way to check whether it needs to be livepatched.




if echo "$1" | grep -q -E "(gcc|g\+\+)"; then
  # Check whether it contains string "XSAN_ONLY_FRONTEND"
  if strings "$(which $1)" | grep -q "XSAN_ONLY_FRONTEND"; then
    # If so, we have patch the gcc with manual patch.
    exit 1
  else
    # If is gcc/g++, we use livepatch.
    exit 0
  fi
else
  
  # For clang, we check whether it contains libclang-cpp.so, libclangDriver.so,
  # and libclangCodeGen.so.
  deps=$(ldd "$(which $1)")
  if echo "$deps" | grep -q libclang-cpp.so; then
    exit 0
  elif echo "$deps" | grep -q libclangDriver.so \
    && echo "$deps" | grep -q libclangCodeGen.so; then
    exit 0
  else
    exit 1
  fi
fi
