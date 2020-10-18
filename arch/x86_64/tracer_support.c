#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
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

// Change the magic anytime the content or the semantics of this struct change.
#define TRACER_STRUCT_MAGIC 0xbeefcafe

typedef struct tracer {
    uint32_t magic;
    sem_t sem;
    uint32_t buffer[32];
    uint32_t buffer_sentinel;
} __attribute__((packed)) tracer_struct;

volatile tracer_struct *shm;

void set_up_tracer(void) {
    // Set up a shared memory object: pick a unique name and attempt shm_open.
    char shm_name[128] = { '\0' };
    ASSERT(snprintf(shm_name, sizeof(shm_name), "/as-tracer-%i", getpid()) < sizeof(shm_name))
    int fd;
    if ((fd = shm_open(shm_name, O_RDWR | O_CREAT | O_TRUNC, 0666)) == -1) {
        printf("set_up_tracer: shm_open() failed with %s\n", strerror(errno));
        return;
    }
    // Size it properly (this will also fill it with \0 bytes if the file
    // didn't exist or was shorter somehow).
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
    if (sem_init((sem_t *)&(shm->sem), 1, 0) != 0) {
        printf("set_up_tracer: sem_init() failed with %s\n", strerror(errno));
        return;
    }
    for (int i = 0; i < sizeof(shm->buffer) / sizeof(shm->buffer[0]); i++) {
        shm->buffer[i] = 0;
    }
    shm->buffer_sentinel = 0xffffffff;

    // TODO: read up about compiler and CPU barriers AGAIN

    // Write the magic last so that a tracer attempting to connect before this
    // point will fail, in case the data was \0's before.
    shm->magic = TRACER_STRUCT_MAGIC;

    ARCH_RESET_TRACE_POINTER
}

void wait_for_tracer(void) {
    // Give the tracer (if there is one) a turn by sem_post()ing, which
    // according to its man page should give any *other* task waiting on the
    // semaphore a turn when incrementing up from 0.
    // Side note: shouldn't sem_* functions take a volatile pointer?
    ASSERT(sem_post((sem_t *)&(shm->sem)) == 0);
    int ret;
    while ((ret = sem_wait((sem_t *)&(shm->sem))) != 0) {
        ASSERT(errno == EINTR);
    }

    // Clear out the trace buffer.
    for (int i = 0; i < sizeof(shm->buffer) / sizeof(shm->buffer[0]); i++) {
        shm->buffer[i] = 0;
    }

    // Reset %r15 to the beginning of the trace buffer.
    ARCH_RESET_TRACE_POINTER
}

// Note: currently not calling sem_destroy at any point.
