#include <stdint.h>

#include "assert.h"

// We can't instrument this file, because we haven't set up the tracer
// datastructures yet!
asm (
    "\n# as-tracer-do-not-instrument"
);

// Note: non-aligned r/w (see: attribute(packed)) could become a problem on
// other arches.
typedef struct tracer {
    uint32_t magic;
    uint32_t buffer[32];
    uint8_t tracer_connected;
    uint8_t tracers_turn;
    struct tracer *pointer_to_start;
} __attribute__((packed)) tracer_struct;

tracer_struct should_be_shm;

void set_up_tracer(void) {
    // If this changes, then all the asm code is wrong.
    ASSERT(sizeof(tracer_struct) == 142);

    // TODO: shm initialization.
    tracer_struct *trace = &should_be_shm;

    // Initialize the tracer_struct.
    trace->magic = 0xbeefcafe;
    for (int i = 0; i < sizeof(trace->buffer) / sizeof(trace->buffer[0]); i++) {
        trace->buffer[i] = 0;
    }
    trace->tracer_connected = 0;
    trace->tracers_turn = 0;
    trace->pointer_to_start = trace;

    // OK, now stuff it in r15.
    #ifdef __x86_64__
        asm volatile (
              "# trust-me-i-know-what-im-doing\n"
              "movq %0, %%r15"
            : // No output..
            : "r"(trace)
            : "%r15" // We'll let gcc know that we clobbered r15, even though
                     // gcc is never going to be allowed to use it anyway.
        );
    #else
        #error "unsupported arch"
    #endif
}
