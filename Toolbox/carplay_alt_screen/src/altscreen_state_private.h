#ifndef ALTSCREEN_STATE_PRIVATE_H
#define ALTSCREEN_STATE_PRIVATE_H

/* Two-phase private111 state contract.
 * SETUP commits the private ScreenSession/listener first; the actual ScreenStream
 * is bound only after the phone connects and the stock-equivalent worker starts. */
int alt_state_stage_private_session(void *receiver_session, void *alt_screen_session);
int alt_state_commit_private_session(void *receiver_session);
int alt_state_bind_private_stream(void *receiver_session, void *alt_screen_stream);

/* G5B is emitted only at the final worker boundary: accept, StartSession, real
 * stream extraction and binding have succeeded and ProcessFrames is about to
 * own the transport loop. A merged response or listener alone is not G5B. */
int alt_state_mark_private_processing(void *receiver_session, void *alt_screen_stream);

/* ProcessFrames/StopSession destroys the concrete ScreenStream while the private
 * ScreenSession may remain committed for control-plane teardown/reconfigure.
 * Remove only that stream ownership and return the context to session-only
 * control-plane state. */
int alt_state_clear_private_stream(void *receiver_session, void *alt_screen_stream);

#endif
