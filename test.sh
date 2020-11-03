#!/bin/bash -ex

./build.sh

rm decoder || true
touch decoder
export GCC_TRACER_DECODER=decoder
./gcc-tracer state_and_time.c arch/x86_64/tracer_support.c arch/x86_64/wait_for_tracer_wrapper.s -o state_and_time

./state_and_time 854729 5 &
PID="$!"

# Give a chance for set_up_tracer() to be run..
sleep 1

./tracer "$PID" decoder
