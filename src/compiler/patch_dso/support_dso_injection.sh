#!/bin/bash

deps=$(ldd "$(which $1)")
if echo "$deps" | grep -q libclang-cpp.so; then
  exit 0
elif echo "$deps" | grep -q libclangDriver.so \
  && echo "$deps" | grep -q libclangCodeGen.so; then
  exit 0
else
  exit 1
fi
