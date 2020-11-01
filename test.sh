#!/bin/bash -ex

./build.sh

rm decoder || true
export GCC_TRACER_DECODER=decoder
./gcc-tracer -c state_and_time.c
./gcc-tracer state_and_time.o arch/x86_64/tracer_support.c -o state_and_time

./state_and_time 854729 5 &
PID="$!"

# Give a chance for set_up_tracer() to be run..
sleep 1

./tracer "$PID" decoder
