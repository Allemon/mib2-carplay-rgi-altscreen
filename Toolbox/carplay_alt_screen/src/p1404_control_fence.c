/* Receiver-local private111 transactions. Only metadata is globally locked. */
#include "p1404_control_fence.h"
#include "p1404_lock_wait.h"
#include <string.h>
#define FENCE_SLOTS 32u
#ifndef ALT_CONTROL_FENCE_WAIT_POLLS
#define ALT_CONTROL_FENCE_WAIT_POLLS 5000u
#endif
static struct { void *receiver; uint32_t generation; unsigned busy, waiters, retired; } fences[FENCE_SLOTS];
static volatile unsigned metadata_lock;
static uint32_t next_token;
static void lock_metadata(void) {
    while (__sync_lock_test_and_set(&metadata_lock, 1u)) p1404_lock_wait_yield();
}
static void unlock_metadata(void) { __sync_lock_release(&metadata_lock); }
static unsigned find_locked(void *receiver, int create) {
    unsigned i, available = FENCE_SLOTS;
    for (i = 0; i < FENCE_SLOTS; ++i) {
        if (fences[i].receiver == receiver) return i;
        if (!fences[i].receiver && available == FENCE_SLOTS) available = i;
    }
    if (create && available < FENCE_SLOTS) fences[available].receiver = receiver;
    return create ? available : FENCE_SLOTS;
}
static uint32_t token_locked(void) {
    if (!++next_token || next_token == ALT_CONTROL_FENCE_UNTRACKED) next_token = 1u;
    return next_token;
}
uint32_t alt_control_fence_begin(void *receiver) {
    uint32_t token = 0;
    unsigned i;
    if (!receiver) return 0;
    lock_metadata();
    i = find_locked(receiver, 1);
    if (i < FENCE_SLOTS) {
        fences[i].retired = 0;
        token = fences[i].generation = token_locked();
    }
    unlock_metadata();
    return token;
}
int alt_control_fence_is_current(void *receiver, uint32_t generation) {
    unsigned i;
    int current;
    lock_metadata();
    i = find_locked(receiver, 0);
    current = i < FENCE_SLOTS && generation && fences[i].generation == generation;
    unlock_metadata();
    return current;
}
static uint32_t acquire(void *receiver, uint32_t generation, int cancel) {
    unsigned i, polls = 0;
    uint32_t token = 0;
    lock_metadata();
    i = find_locked(receiver, 0);
    /* An OEM-only receiver has nothing to cancel. Teardown must not allocate
     * a private slot or be refused because unrelated slots are occupied. */
    if (i == FENCE_SLOTS) { unlock_metadata(); return cancel ? ALT_CONTROL_FENCE_UNTRACKED : 0; }
    ++fences[i].waiters;
    while (fences[i].busy && polls < ALT_CONTROL_FENCE_WAIT_POLLS) {
        unlock_metadata();
        p1404_lock_wait_yield();
        ++polls;
        lock_metadata();
    }
    if (!fences[i].busy && (cancel || (!fences[i].retired && generation && fences[i].generation == generation))) {
        fences[i].busy = 1;
        fences[i].retired = 0;
        if (cancel) fences[i].generation = token_locked();
        token = fences[i].generation;
    }
    --fences[i].waiters;
    if (fences[i].retired && !fences[i].busy && !fences[i].waiters)
        memset(&fences[i], 0, sizeof(fences[i]));
    unlock_metadata();
    return token;
}
int alt_control_fence_lock_current(void *receiver, uint32_t generation) {
    return acquire(receiver, generation, 0) != 0;
}
uint32_t alt_control_fence_cancel_and_lock(void *receiver) {
    return receiver ? acquire(receiver, 0, 1) : 0;
}
void alt_control_fence_unlock(void *receiver, int cancelled) {
    unsigned i;
    lock_metadata();
    i = find_locked(receiver, 0);
    if (i < FENCE_SLOTS) {
        fences[i].busy = 0;
        if (cancelled) fences[i].retired = 1;
        if (fences[i].retired && !fences[i].waiters) memset(&fences[i], 0, sizeof(fences[i]));
    }
    unlock_metadata();
}
void alt_control_fence_reset(void) {
    memset(fences, 0, sizeof(fences)); next_token = 0; metadata_lock = 0;
}
