#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "helpers.h"

#ifndef __x86_64__
    #error "wrong arch"
#endif

#define MIN(a, b) ((a) > (b) ? (b) : (a))

// Change the magic anytime the content or the semantics of this struct change.
#define TRACER_STRUCT_MAGIC 0xbeefcafe

// I don't care about instrumenting this file, and if I did, I'd have to
// somehow make this code reentrant.
asm (
    "\n# as-tracer-do-not-instrument"
);

// TODO: use this to order trace blocks on the tracer end
uint64_t trace_block_ordering = 0;

#define PER_THREAD_TRACE_BUFFER_LEN 32
__thread struct {
    uint32_t buffer[PER_THREAD_TRACE_BUFFER_LEN];
    uint32_t sentinel;
} per_thread_trace_buffer = {
    .buffer = { 0 },
    .sentinel = 0xffffffff
    // ^ This sentinel tells the record stub when it's run out of space.
};

__thread struct {
    uint64_t rax_copy;
    uint64_t rsp_copy;
    uint64_t offset;
    pid_t tid;
} __attribute__((packed)) per_thread_trace_context = {
    .offset = 0,
    .tid = -1
};

__thread uint8_t per_thread_trace_stack[1024000];

// To know the exact number of bytes that `xsave` will need, we would need to
// probe cpuid at runtime, which won't work for us because when a record stub
// runs this area needs to be allocated already. So I just picked a number that
// seems big enough (10KB).
__thread uint8_t per_thread_trace_xsave[10240] __attribute__ ((aligned (64)));

#define COALESCED_TRACE_BUFFER_LEN 32
typedef struct {
    uint32_t magic;
    sem_t one_thread_at_a_time;
    sem_t tracer_ready;
    sem_t tracers_turn;
    sem_t tracer_done;
    // When a thread calls wait_for_tracer, it'll dump its own trace buffer
    // into this consolidated buffer.
    struct {
        pid_t tid;
        uint32_t value;
    } buffer[COALESCED_TRACE_BUFFER_LEN];
    size_t remaining;
} __attribute__((packed)) coalesced_trace_struct;

coalesced_trace_struct *coalesced_trace;

void set_up_tracer(void) {
    // Set up a shared memory object: pick a unique name and attempt shm_open.
    char shm_name[128] = { '\0' };
    ASSERT(snprintf(shm_name, sizeof(shm_name), "/as-tracer-%i", getpid()) < sizeof(shm_name));
    int fd;
    if ((fd = shm_open(shm_name, O_RDWR | O_CREAT | O_TRUNC, 0666)) == -1) {
        printf("set_up_tracer: shm_open() failed with %s\n", strerror(errno));
        return;
    }
    // Size it properly (this will also fill it with \0 bytes if the file
    // didn't exist or was shorter somehow).
    if (ftruncate(fd, sizeof(*coalesced_trace)) == -1) {
        printf("set_up_tracer: ftruncate() failed with %s\n", strerror(errno));
        return;
    }
    // .. and mmap it in.
    if ((coalesced_trace = mmap(NULL, sizeof(*coalesced_trace), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
        printf("set_up_tracer: mmap() failed with %s\n", strerror(errno));
        return;
    }

    // Initialize the coalesced_trace.
    if (sem_init(&(coalesced_trace->one_thread_at_a_time), 1, 1) != 0) {
        printf("set_up_tracer: sem_init() failed with %s\n", strerror(errno));
        return;
    }
    if (sem_init(&(coalesced_trace->tracer_ready), 1, 0) != 0) {
        printf("set_up_tracer: sem_init() failed with %s\n", strerror(errno));
        return;
    }
    if (sem_init(&(coalesced_trace->tracers_turn), 1, 0) != 0) {
        printf("set_up_tracer: sem_init() failed with %s\n", strerror(errno));
        return;
    }
    if (sem_init(&(coalesced_trace->tracer_done), 1, 0) != 0) {
        printf("set_up_tracer: sem_init() failed with %s\n", strerror(errno));
        return;
    }
    coalesced_trace->remaining = COALESCED_TRACE_BUFFER_LEN;

    // I'm being paranoid here: mfence and compiler barrier.
    __sync_synchronize();
    asm volatile("": : :"memory");

    // Write the magic last so that a tracer attempting to connect before this
    // point will fail.
    coalesced_trace->magic = TRACER_STRUCT_MAGIC;
}

int wait_for_tracer(void) {
    // If the magic isn't set properly then it means that set_up_tracer hasn't
    // run yet, just clear the buffer and return.
    if (coalesced_trace->magic != TRACER_STRUCT_MAGIC) {
        per_thread_trace_context.offset = 0;
        // TODO: could also atomically update a coalesced_trace->traces_lost
        // value so that set_up_tracer() can be aware that a bunch of data was
        // lost.
        return 0;
    }

    // Grab the mutex.
    ASSERT(sem_wait(&(coalesced_trace->one_thread_at_a_time)) == 0);

    // If the tid hasn't been set yet, do so.
    if (per_thread_trace_context.tid == -1)
        per_thread_trace_context.tid = (pid_t)syscall(SYS_gettid);

    // Since we only get called if one of the per-thread trace buffers is full,
    // we need to empty that buffer before we return. We want to store that
    // data in the coalesced trace buffer, because otherwise it's lost forever.
    size_t remaining = PER_THREAD_TRACE_BUFFER_LEN;
    while (1) {
        // Fit whatever we can in the coalesced trace buffer.
        size_t can_fit = MIN(remaining, coalesced_trace->remaining);
        for (size_t i = 0; i < can_fit; i++) {
            size_t coalesced_off = COALESCED_TRACE_BUFFER_LEN - coalesced_trace->remaining + i;
            size_t per_thread_off = PER_THREAD_TRACE_BUFFER_LEN - remaining + i;
            coalesced_trace->buffer[coalesced_off].value = per_thread_trace_buffer.buffer[per_thread_off];
            coalesced_trace->buffer[coalesced_off].tid = per_thread_trace_context.tid;
        }

        // Did we copy all the per-trace data in?
        remaining -= can_fit;
        coalesced_trace->remaining -= can_fit;
        if (remaining == 0) break;

        // If not, then we need to make room in the coalesced tracer buffer, so
        // if the tracer is present/ready, give it a (very healthy) chance to
        // copy the data out.
        if (sem_trywait(&(coalesced_trace->tracer_ready)) == 0) {
            ASSERT(sem_post(&(coalesced_trace->tracers_turn)) == 0);
            struct timespec timeout;
            ASSERT(clock_gettime(CLOCK_REALTIME, &timeout) == 0);
            timeout.tv_sec += 1;
            if (sem_timedwait(&(coalesced_trace->tracer_done), &timeout) == -1) {
                ASSERT((errno == ETIMEDOUT) || (errno == EINTR));
                sem_trywait(&(coalesced_trace->tracers_turn));
            }
        } else {
            ASSERT(errno == EAGAIN);
        }

        // Then "clear" the coalesced tracer buffer.
        coalesced_trace->remaining = COALESCED_TRACE_BUFFER_LEN;
    }

    // Now we can clear the per-thread trace buffer.
    for (size_t i = 0; i < PER_THREAD_TRACE_BUFFER_LEN; i++) {
        ASSERT(per_thread_trace_buffer.buffer[i] != 0);
        per_thread_trace_buffer.buffer[i] = 0;
    }
    per_thread_trace_context.offset = 0;

    // Release the mutex.
    ASSERT(sem_post(&(coalesced_trace->one_thread_at_a_time)) == 0);

    // The asm stub needs `eax` to be populated with 0, so it knows that the
    // offset has been reset.
    return 0;
}

asm (
    "\n# as-tracer-do-instrument"
);
