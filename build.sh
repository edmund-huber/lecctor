#!/bin/bash -ex

# .. gotta put this in a makefile ..
mkdir gcc-bin || true
# -O0 is default, but also makes assembly easier to parse for us?
DIR="$(dirname "$(readlink -f "$0")")"
gcc -std=c99 -Wall -O0 -D_POSIX_C_SOURCE=200809 -D_DEFAULT_SOURCE -DBASE_DIRECTORY=$DIR as-tracer.c decoder.c scanning.c -o gcc-bin/as
gcc -std=c99 -Wall -O0 -D_POSIX_C_SOURCE=200809 -D_DEFAULT_SOURCE -DBASE_DIRECTORY=$DIR gcc-tracer.c -o gcc-tracer
gcc -lrt -pthread -o tracer tracer.c decoder.c scanning.c
