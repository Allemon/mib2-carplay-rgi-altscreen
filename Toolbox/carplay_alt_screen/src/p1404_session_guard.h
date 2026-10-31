/* Serialize receiver lifecycle operations without assuming a QNX mutex ABI. */
#ifndef P1404_SESSION_GUARD_H
#define P1404_SESSION_GUARD_H
#include <stdint.h>

enum alt_session_operation {
    ALT_SESSION_SETUP,
    ALT_SESSION_START,
    ALT_SESSION_CONTROL,
    ALT_SESSION_TEARDOWN,
    ALT_SESSION_COMMAND,
    ALT_SESSION_STOP_COMMAND, /* lifecycle owner may send stopUI before joining */
    ALT_SESSION_DEFERRED_TEARDOWN /* retained background waiter, no caller stall */
};

enum alt_session_admission {
    ALT_SESSION_REFUSED = -1,
    ALT_SESSION_CLOSED = 0,
    ALT_SESSION_ENTERED = 1,
    ALT_SESSION_REENTRANT_STOP = 2
};

struct alt_session_lease {
    unsigned slot;
    unsigned waited;
    int registered;
    int entered;
    int retained;
    int completed;
    int deferred;
    int blocks_lifecycle;
    enum alt_session_operation operation;
    uint32_t entry_stream_epochs[256]; /* immutable ordering point, before waiting */
};

int alt_session_guard_copy_stream_epochs(const struct alt_session_lease *, uint32_t out[256]);
void alt_session_guard_note_stream_setup(const struct alt_session_lease *, unsigned type);
int alt_session_guard_enter(void *receiver, enum alt_session_operation operation,
                            struct alt_session_lease *lease);
void alt_session_guard_leave(struct alt_session_lease *lease, int full_completed);
int alt_session_guard_defer(struct alt_session_lease *lease, int blocks_lifecycle);
int alt_session_guard_reserve_retry(const struct alt_session_lease *owner,
                                   struct alt_session_lease *reservation);

#endif
