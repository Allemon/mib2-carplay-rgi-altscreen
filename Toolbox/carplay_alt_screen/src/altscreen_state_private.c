/* Two-phase private111 state operations over the synchronized side table. */
#include "altscreen_state_private.h"
#include "p1404_abi.h"
#include <string.h>

int alt_state_stage_private_session(void *receiver_session, void *alt_screen_session) {
    struct altscreen_ctx *c;
    struct altscreen_ctx snap;
    int ok = 0, reason = 0;
    memset(&snap, 0, sizeof(snap));
    if (!receiver_session || !alt_screen_session) {
        altscreen_log("ERROR PHASE=STREAM_111_SESSION_STAGE receiver=%p alt_session=%p invalid=1",
                      receiver_session, alt_screen_session);
        return 0;
    }
    alt_state_table_lock();
    c = alt_state_lookup_any_locked(receiver_session);
    if (!c) c = alt_state_register_locked(receiver_session);
    if (!c) reason = 1;
    else if (c->state >= 2) {
        ok = c->alt_screen_session == alt_screen_session;
        if (!ok) reason = 2;
        memcpy(&snap, c, sizeof(snap));
    } else if (c->alt_screen_session && c->alt_screen_session != alt_screen_session) {
        reason = 3;
        memcpy(&snap, c, sizeof(snap));
    } else if (c->alt_screen_stream) {
        reason = 4;
        memcpy(&snap, c, sizeof(snap));
    } else {
        c->alt_screen_session = alt_screen_session;
        c->state = 1;
        memcpy(&snap, c, sizeof(snap));
        ok = 1;
    }
    alt_state_table_unlock();
    if (reason == 1)
        altscreen_log("ERROR STATE table full max=8 refusing_alt=1");
    else if (reason == 2)
        altscreen_log("ERROR PHASE=STREAM_111_SESSION_STAGE id=%u generation=%u committed_session_mismatch old=%p new=%p",
                      snap.id, snap.generation, snap.alt_screen_session, alt_screen_session);
    else if (reason == 3)
        altscreen_log("ERROR PHASE=STREAM_111_SESSION_STAGE id=%u generation=%u staged_session_mismatch old=%p new=%p",
                      snap.id, snap.generation, snap.alt_screen_session, alt_screen_session);
    else if (reason == 4)
        altscreen_log("ERROR PHASE=STREAM_111_SESSION_STAGE id=%u generation=%u unexpected_precommit_stream=%p",
                      snap.id, snap.generation, snap.alt_screen_stream);
    else if (ok)
        altscreen_log("PHASE=STREAM_111_SESSION_STAGE id=%u generation=%u receiver=%p alt_session=%p stream_pending=1 committed=0 stock110_untouched=1",
                      snap.id, snap.generation, receiver_session, alt_screen_session);
    return ok;
}

int alt_state_commit_private_session(void *receiver_session) {
    struct altscreen_ctx *c;
    struct altscreen_ctx snap;
    int ok = 0, first = 0;
    memset(&snap, 0, sizeof(snap));
    alt_state_table_lock();
    c = alt_state_lookup_any_locked(receiver_session);
    if (c && c->alt_screen_session) {
        first = c->state < 2;
        if (first) c->state = 2;
        memcpy(&snap, c, sizeof(snap));
        ok = 1;
    }
    alt_state_table_unlock();
    if (!ok) {
        altscreen_log("ERROR PHASE=STREAM_111_SESSION_COMMIT receiver=%p missing_session=1", receiver_session);
        return 0;
    }
    if (first)
        altscreen_log("PHASE=STREAM_111_SESSION_COMMIT id=%u generation=%u receiver=%p alt_session=%p final_response_merged=1 listener_ready=1 stream_pending=1 g5b=0 stock110_untouched=1",
                      snap.id, snap.generation, receiver_session, snap.alt_screen_session);
    return 1;
}

int alt_state_bind_private_stream(void *receiver_session, void *alt_screen_stream) {
    struct altscreen_ctx *c;
    struct altscreen_ctx snap;
    int ok = 0, collision = 0;
    memset(&snap, 0, sizeof(snap));
    if (!receiver_session || !alt_screen_stream) {
        altscreen_log("ERROR PHASE=STREAM_111_STREAM_BIND receiver=%p stream=%p committed_session_missing=1",
                      receiver_session, alt_screen_stream);
        return 0;
    }
    alt_state_table_lock();
    c = alt_state_lookup_locked(receiver_session);
    if (c && c->alt_screen_session) {
        if (!c->alt_screen_stream) {
            c->alt_screen_stream = alt_screen_stream;
            if (c->state < 3) c->state = 3;
            ok = 1;
        } else if (c->alt_screen_stream == alt_screen_stream) {
            ok = 1;
        } else collision = 1;
        memcpy(&snap, c, sizeof(snap));
    }
    alt_state_table_unlock();
    if (!ok && collision)
        altscreen_log("ERROR PHASE=STREAM_111_STREAM_BIND id=%u generation=%u old_stream=%p new_stream=%p collision=1",
                      snap.id, snap.generation, snap.alt_screen_stream, alt_screen_stream);
    else if (!ok)
        altscreen_log("ERROR PHASE=STREAM_111_STREAM_BIND receiver=%p stream=%p committed_session_missing=1",
                      receiver_session, alt_screen_stream);
    else
        altscreen_log("PHASE=STREAM_111_STREAM_BOUND id=%u generation=%u receiver=%p alt_session=%p alt_stream=%p post_accept=1 stock110_untouched=1",
                      snap.id, snap.generation, receiver_session, snap.alt_screen_session,
                      alt_screen_stream);
    return ok;
}

int alt_state_mark_private_processing(void *receiver_session, void *alt_screen_stream) {
    struct altscreen_ctx *c;
    struct altscreen_ctx snap;
    int ok = 0, first = 0;
    memset(&snap, 0, sizeof(snap));
    alt_state_table_lock();
    c = alt_state_lookup_locked(receiver_session);
    if (c && c->alt_screen_session && alt_screen_stream &&
        c->alt_screen_stream == alt_screen_stream) {
        first = !c->process_frames_started;
        c->process_frames_started = 1;
        memcpy(&snap, c, sizeof(snap));
        ok = 1;
    } else if (c) memcpy(&snap, c, sizeof(snap));
    alt_state_table_unlock();
    if (!ok) {
        altscreen_log("ERROR PHASE=STREAM_111_PROCESSING_COMMIT receiver=%p expected_stream=%p actual_stream=%p usable=0",
                      receiver_session, alt_screen_stream, snap.alt_screen_stream);
        return 0;
    }
    if (first) {
        altscreen_log("PHASE=STREAM_111_PROCESSING_COMMIT id=%u generation=%u receiver=%p alt_session=%p alt_stream=%p accept=1 startSession=1 bound=1 processFrames=STARTING",
                      snap.id, snap.generation, receiver_session, snap.alt_screen_session,
                      alt_screen_stream);
        altscreen_mark_stream_setup(CP_STREAM_ALT_SCREEN);
    }
    return 1;
}

int alt_state_clear_private_stream(void *receiver_session, void *alt_screen_stream) {
    struct altscreen_ctx *c;
    struct altscreen_ctx snap;
    int ok = 0;
    memset(&snap, 0, sizeof(snap));
    alt_state_table_lock();
    c = alt_state_lookup_any_locked(receiver_session);
    if (!c) ok = 1;
    else {
        memcpy(&snap, c, sizeof(snap));
        if (alt_screen_stream && c->alt_screen_stream == alt_screen_stream) {
            c->alt_screen_stream = NULL;
            c->process_frames_started = 0;
            c->nal_carry_len = 0;
            if (c->state > 2) c->state = 2;
            ok = 1;
        }
    }
    alt_state_table_unlock();
    if (!c) return 1;
    if (!ok) {
        altscreen_log("ERROR PHASE=STREAM_111_STREAM_CLEAR receiver=%p expected=%p actual=%p mismatch=1",
                      receiver_session, alt_screen_stream, snap.alt_screen_stream);
        return 0;
    }
    alt_state_forget_stream(alt_screen_stream);
    altscreen_log("PHASE=STREAM_111_STREAM_CLEARED id=%u generation=%u receiver=%p alt_session=%p old_stream=%p session_committed=1",
                  snap.id, snap.generation, receiver_session, snap.alt_screen_session,
                  alt_screen_stream);
    return 1;
}
