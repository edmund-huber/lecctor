#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "assert.h"

#ifndef __x86_64__
    #error "wrong arch"
#endif

// We can't instrument this file, because we haven't set up the tracer
// datastructures yet!
asm (
    "\n# as-tracer-do-not-instrument"
);

#define ARCH_RESET_TRACE_POINTER asm volatile ( \
          "# trust-me-i-know-what-im-doing\n" \
          "movq %0, %%r15" \
        : \
        : "r"(shm->buffer) \
        : "%r15" \
    );
// We'll let gcc know that we clobbered r15, even though gcc is never going
// to be allowed to use it anyway.

// Note: non-aligned r/w (see: attribute(packed)) could become a problem on
// other arches.
typedef struct tracer {
    uint32_t magic;
    uint32_t buffer[32];
    uint32_t buffer_sentinel;
    uint8_t tracer_connected;
    uint8_t tracers_turn;
} __attribute__((packed)) tracer_struct;

tracer_struct *shm;

void set_up_tracer(void) {
    // Set up a shared memory object:
    // First, give it a name and create it ..
    char shm_name[128] = { '\0' };
    ASSERT(snprintf(shm_name, sizeof(shm_name), "/as-tracer-%i", getpid()) < sizeof(shm_name))
    int fd;
    if ((fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0666)) == -1) {
        printf("set_up_tracer: shm_open() failed with %s\n", strerror(errno));
        return;
    }
    // .. size it properly ..
    if (ftruncate(fd, sizeof(tracer_struct)) == -1) {
        printf("set_up_tracer: ftruncate() failed with %s\n", strerror(errno));
        return;
    }
    // .. and mmap it in.
    if ((shm = mmap(NULL, sizeof(tracer_struct), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0))
        == MAP_FAILED) {
        printf("set_up_tracer: mmap() failed with %s\n", strerror(errno));
        return;
    }

    // Initialize the tracer_struct.
    shm->magic = 0xbeefcafe;
    for (int i = 0; i < sizeof(shm->buffer) / sizeof(shm->buffer[0]); i++) {
        shm->buffer[i] = 0;
    }
    shm->buffer_sentinel = 0xffffffff;
    shm->tracer_connected = 0;
    shm->tracers_turn = 0;

    ARCH_RESET_TRACE_POINTER
}

void wait_for_tracer(void) {
    // Clear out the trace buffer.
    for (int i = 0; i < sizeof(shm->buffer) / sizeof(shm->buffer[0]); i++) {
        shm->buffer[i] = 0;
    }

    // Reset %r15 to the beginning of the trace buffer.
    ARCH_RESET_TRACE_POINTER
}
