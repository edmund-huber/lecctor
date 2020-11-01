#!/bin/bash -ex

# .. gotta put this in a makefile ..
mkdir gcc-bin || true
# -O0 is default, but also makes assembly easier to parse for us?
gcc -std=c99 -Wall -O0 -D_POSIX_C_SOURCE=200809 -D_DEFAULT_SOURCE as-tracer.c decoder.c scanning.c -o gcc-bin/as

gcc -lrt -pthread -o tracer tracer.c decoder.c scanning.c
