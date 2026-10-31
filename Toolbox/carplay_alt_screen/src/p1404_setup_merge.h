/*
 * p1404_setup_merge.h - CF-container-only SETUP split/merge helpers.
 *
 * This module deliberately knows NOTHING about the P1404 Screen_* call ABI. It
 * only manipulates already-materialized CarPlay SETUP dictionaries through
 * callbacks supplied by p1404_airplay_fullchain.c. Creator-owned temporary CF
 * objects are released through ops->release after a container has retained them.
 */
#ifndef P1404_SETUP_MERGE_H
#define P1404_SETUP_MERGE_H

#include <stdint.h>

typedef void *alt_setup_obj;

struct alt_setup_cf_ops {
    int      (*is_dict)(const void *);
    int      (*is_array)(const void *);
    unsigned (*dict_count)(alt_setup_obj);
    const void *(*dict_get)(alt_setup_obj, const void *);
    void     (*dict_set)(alt_setup_obj, const void *, const void *);
    void     (*dict_get_keys_values)(alt_setup_obj, const void **, const void **);
    alt_setup_obj (*dict_create_mutable)(alt_setup_obj, unsigned, const void *, const void *, const void *);
    unsigned (*array_count)(alt_setup_obj);
    const void *(*array_at)(alt_setup_obj, unsigned);
    void     (*array_append)(alt_setup_obj, const void *);
    alt_setup_obj (*array_create_mutable)(alt_setup_obj, unsigned, const void *, const void *);
    alt_setup_obj (*string_new)(const char *, int);
    void     (*release)(alt_setup_obj);
    int      (*int64_value)(const void *, int64_t *);
};

/*
 * Build a shallow-cloned request for the stock handler with every type
 * == alt_stream_type removed from the top-level "streams" array. All other root
 * keys and stream descriptors retain their original object identity and order.
 *
 * Returns a new mutable root dictionary, or NULL if the request cannot be copied
 * without guessing. The original request is never mutated. The returned root is
 * creator-owned by the caller; all helper-local keys/arrays are released here.
 */
alt_setup_obj alt_setup_build_stock_only(const struct alt_setup_cf_ops *ops,
                                         alt_setup_obj request,
                                         uint32_t alt_stream_type,
                                         alt_setup_obj *alt_descriptor_out,
                                         unsigned *removed_out);

/*
 * Merge one private stream response into final_response["streams"] exactly once.
 * Existing stock Main/audio entries are copied in-order into a fresh array and are
 * never overwritten. If an existing type==alt_stream_type is already present,
 * this returns 0 rather than silently replacing it. The fresh array is released
 * after dict_set() retains it, so this helper leaves no creator-owned temporary.
 *
 * The caller must commit G5B only after this returns 1.
 */
int alt_setup_merge_private_response(const struct alt_setup_cf_ops *ops,
                                     alt_setup_obj final_response,
                                     alt_setup_obj private_stream_response,
                                     uint32_t alt_stream_type);

/* Remove an audio teardown type mask in one pass, preserving up to 128 original
 * stream descriptors and all root fields. Refuse an empty result (stock treats
 * that as full teardown). Caller owns the returned shallow dictionary clone. */
alt_setup_obj alt_setup_filter_teardown_types(const struct alt_setup_cf_ops *,
                                              alt_setup_obj request,
                                              const unsigned char stale[256]);

#endif
