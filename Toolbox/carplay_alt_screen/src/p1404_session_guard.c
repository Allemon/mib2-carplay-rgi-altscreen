#include "p1404_lock_wait.h"
/* Included by the fullchain wrapper; also compiled directly by host tests. */
#include "p1404_session_guard.h"
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define ALT_SESSION_GUARD_SLOTS 32u
#ifndef ALT_SESSION_WAIT_POLLS
#define ALT_SESSION_WAIT_POLLS 5000u
#endif

static struct {
    void *receiver;
    unsigned long owner;
    unsigned users;
    unsigned depth;
    unsigned teardown_depth;
    unsigned command_depth;
    unsigned pending_teardown;
    uint32_t stream_setup_epochs[256];
    int full_completed;
} session_guards[ALT_SESSION_GUARD_SLOTS];
static volatile unsigned session_guards_lock;

static void session_guards_acquire(void) {
    /* Metadata only. Never hold this lock across stock calls, joins or logging.
     * Yield even here: a higher-priority QNX waiter must not starve its owner. */
    while (__sync_lock_test_and_set(&session_guards_lock, 1u)) p1404_lock_wait_yield();
}

static void session_guards_release(void) {
    __sync_lock_release(&session_guards_lock);
}

int alt_session_guard_copy_stream_epochs(const struct alt_session_lease *lease, uint32_t out[256]) {
    if (!lease->registered) return 0;
    session_guards_acquire();
    memcpy(out, session_guards[lease->slot].stream_setup_epochs, 256 * sizeof(uint32_t));
    session_guards_release();
    return 1;
}
void alt_session_guard_note_stream_setup(const struct alt_session_lease *lease, unsigned type) {
    if (!lease->entered || !lease->registered || type >= 256) return;
    session_guards_acquire();
    if (!++session_guards[lease->slot].stream_setup_epochs[type]) ++session_guards[lease->slot].stream_setup_epochs[type];
    session_guards_release();
}
int alt_session_guard_enter(void *receiver, enum alt_session_operation operation,
                            struct alt_session_lease *lease) {
    unsigned i, slot = ALT_SESSION_GUARD_SLOTS;
    unsigned long self = (unsigned long)pthread_self();
    memset(lease, 0, sizeof(*lease));
    lease->operation = operation;
    if (!receiver) return ALT_SESSION_ENTERED; /* preserve stock NULL handling */

    session_guards_acquire();
    for (i = 0; i < ALT_SESSION_GUARD_SLOTS; ++i) {
        if (session_guards[i].receiver == receiver) { slot = i; break; }
        if (!session_guards[i].receiver && slot == ALT_SESSION_GUARD_SLOTS) slot = i;
    }
    if (slot == ALT_SESSION_GUARD_SLOTS) {
        session_guards_release();
        return ALT_SESSION_REFUSED; /* never fall through to concurrent stock */
    }
    session_guards[slot].receiver = receiver;
    ++session_guards[slot].users; /* includes waiters, so their slot cannot vanish */
    lease->slot = slot;
    lease->registered = 1;
    memcpy(lease->entry_stream_epochs, session_guards[slot].stream_setup_epochs, sizeof(lease->entry_stream_epochs));

    for (;;) {
        if (session_guards[slot].full_completed) {
            lease->completed = 1;
            session_guards_release();
            return ALT_SESSION_CLOSED;
        }
        /* Preserve stop-before-start ordering while a retained background
         * cleanup waits. A late SETUP must not be destroyed by that old stop. */
        if (session_guards[slot].pending_teardown &&
            operation != ALT_SESSION_TEARDOWN &&
            operation != ALT_SESSION_DEFERRED_TEARDOWN &&
            operation != ALT_SESSION_STOP_COMMAND) {
            session_guards_release();
            return ALT_SESSION_REFUSED;
        }
        if (!session_guards[slot].owner || session_guards[slot].owner == self) {
            /* Stock SETUP error recovery legitimately calls outer TearDown on
             * its own thread. Allow that nesting, but never destroy resources
             * recursively while an outer stop is still using them. */
            if (session_guards[slot].teardown_depth && operation != ALT_SESSION_STOP_COMMAND) {
                session_guards_release();
                return operation == ALT_SESSION_TEARDOWN ?
                    ALT_SESSION_REENTRANT_STOP : ALT_SESSION_CLOSED;
            }
            // An OEM synchronous command callback may reenter outer teardown.
            // It must return to the submitter before its HTTP channel is freed.
            if (session_guards[slot].command_depth && operation != ALT_SESSION_COMMAND
                && operation != ALT_SESSION_STOP_COMMAND) {
                session_guards_release();
                return ALT_SESSION_REFUSED;
            }
            session_guards[slot].owner = self;
            ++session_guards[slot].depth;
            if (operation == ALT_SESSION_TEARDOWN || operation == ALT_SESSION_DEFERRED_TEARDOWN)
                ++session_guards[slot].teardown_depth;
            if (operation == ALT_SESSION_COMMAND || operation == ALT_SESSION_STOP_COMMAND)
                ++session_guards[slot].command_depth;
            lease->entered = 1;
            session_guards_release();
            return ALT_SESSION_ENTERED;
        }
        // Monitor commands never queue behind lifecycle teardown/SETUP. This
        // avoids a fence -> worker join -> command admission cycle.
        if (operation == ALT_SESSION_COMMAND || operation == ALT_SESSION_STOP_COMMAND) {
            session_guards_release();
            return ALT_SESSION_REFUSED;
        }
        /* A stalled OEM submitter must not pin another lifecycle caller
         * forever. Refusal preserves its resources and reports unfinished;
         * it never permits teardown to overlap a command. */
        if (lease->waited >= (operation == ALT_SESSION_DEFERRED_TEARDOWN ?
            (ALT_SESSION_WAIT_POLLS + 49u) / 50u : ALT_SESSION_WAIT_POLLS)) {
            session_guards_release();
            return ALT_SESSION_REFUSED;
        }
        session_guards_release();
        ++lease->waited;
        usleep(operation == ALT_SESSION_DEFERRED_TEARDOWN ? 50000u : 1000u);
        session_guards_acquire();
    }
}

int alt_session_guard_defer(struct alt_session_lease *lease, int blocks_lifecycle) {
    if (!lease->registered || lease->entered || lease->deferred) return 0;
    session_guards_acquire();
    if (blocks_lifecycle) ++session_guards[lease->slot].pending_teardown;
    lease->blocks_lifecycle = blocks_lifecycle;
    lease->deferred = 1;
    session_guards_release();
    return 1;
}

/* Keep identity alive while an admitted teardown transfers a retry to a
 * background job. The reservation never owns the caller's execution lease. */
int alt_session_guard_reserve_retry(const struct alt_session_lease *owner,
                                   struct alt_session_lease *reservation) {
    if (!owner->registered || !owner->entered) return 0;
    memset(reservation, 0, sizeof(*reservation));
    session_guards_acquire();
    ++session_guards[owner->slot].users;
    reservation->registered = 1;
    reservation->slot = owner->slot;
    reservation->operation = ALT_SESSION_DEFERRED_TEARDOWN;
    memcpy(reservation->entry_stream_epochs, owner->entry_stream_epochs, sizeof(reservation->entry_stream_epochs));
    session_guards_release();
    return 1;
}

void alt_session_guard_leave(struct alt_session_lease *lease, int full_completed) {
    unsigned slot;
    if (!lease->registered) return;
    slot = lease->slot;
    session_guards_acquire();
    if (lease->deferred && lease->blocks_lifecycle) --session_guards[slot].pending_teardown;
    if (lease->entered) {
        if (full_completed) session_guards[slot].full_completed = 1;
        if (lease->operation == ALT_SESSION_TEARDOWN || lease->operation == ALT_SESSION_DEFERRED_TEARDOWN)
            --session_guards[slot].teardown_depth;
        if (lease->operation == ALT_SESSION_COMMAND || lease->operation == ALT_SESSION_STOP_COMMAND)
            --session_guards[slot].command_depth;
        if (!--session_guards[slot].depth) session_guards[slot].owner = 0;
    }
    /* No permanent pointer tombstone: once all overlapping calls have left,
     * a new receiver may legally be allocated at exactly the same address. */
    if (!--session_guards[slot].users)
        memset(&session_guards[slot], 0, sizeof(session_guards[slot]));
    session_guards_release();
    lease->registered = 0;
}
