#ifndef P1404_CONTROL_FENCE_H
#define P1404_CONTROL_FENCE_H
#include <stdint.h>
/* Cancellation and ownership are per receiver. Only metadata is globally locked.
 * Contended acquisition is bounded; refusal retains resources for safe retry. */
#define ALT_CONTROL_FENCE_UNTRACKED UINT32_MAX /* no private transaction exists */
uint32_t alt_control_fence_begin(void *receiver);
int alt_control_fence_is_current(void *receiver, uint32_t generation);
int alt_control_fence_lock_current(void *receiver, uint32_t generation);
uint32_t alt_control_fence_cancel_and_lock(void *receiver);
void alt_control_fence_unlock(void *receiver, int cancelled);
void alt_control_fence_reset(void);
#endif
