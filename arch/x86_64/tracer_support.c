#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
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
    sem_t tracer_ready;
    sem_t tracers_turn;
    sem_t tracer_done;
    uint32_t buffer[32];
    uint32_t buffer_sentinel;
} __attribute__((packed)) tracer_struct;

tracer_struct *shm;

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
    if (sem_init(&(shm->tracer_ready), 1, 0) != 0) {
        printf("set_up_tracer: sem_init() failed with %s\n", strerror(errno));
        return;
    }
    if (sem_init(&(shm->tracers_turn), 1, 0) != 0) {
        printf("set_up_tracer: sem_init() failed with %s\n", strerror(errno));
        return;
    }
    if (sem_init(&(shm->tracer_done), 1, 0) != 0) {
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
    // If the tracer is ready ..
    if (sem_trywait(&(shm->tracer_ready)) == 0) {
        // .. give it a chance to do its thing.
        ASSERT(sem_post(&(shm->tracers_turn)) == 0);
        struct timespec timeout;
        ASSERT(clock_gettime(CLOCK_REALTIME, &timeout) == 0);
        timeout.tv_sec += 1;
        if (sem_timedwait(&(shm->tracer_done), &timeout) == -1) {
            ASSERT((errno == ETIMEDOUT) || (errno == EINTR));
            sem_trywait(&(shm->tracers_turn));
        }
    } else {
        ASSERT(errno == EAGAIN);
    }

    // Clear out the trace buffer.
    for (int i = 0; i < sizeof(shm->buffer) / sizeof(shm->buffer[0]); i++) {
        ASSERT(shm->buffer[i] != 0);
        shm->buffer[i] = 0;
    }

    // Reset %r15 to the beginning of the trace buffer.
    ARCH_RESET_TRACE_POINTER
}

// Note: currently not calling sem_destroy at any point.
