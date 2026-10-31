/* p1404_setup_merge.c - protocol container policy, no Screen_* ABI calls. */
#include "p1404_setup_merge.h"
#include <stddef.h>
#include <string.h>

#define ALT_SETUP_ROOT_MAX 64u
#define ALT_SETUP_STREAM_MAX 32u

static int ops_ready(const struct alt_setup_cf_ops *o) {
    return o && o->is_dict && o->is_array && o->dict_count && o->dict_get &&
           o->dict_set && o->dict_get_keys_values && o->dict_create_mutable &&
           o->array_count && o->array_at && o->array_append &&
           o->array_create_mutable && o->string_new && o->release &&
           o->int64_value;
}

static int object_stream_type(const struct alt_setup_cf_ops *o, alt_setup_obj obj,
                              uint32_t *type_out) {
    alt_setup_obj k;
    const void *v;
    int64_t n = -1;
    if (!o || !obj || !type_out || !o->is_dict(obj)) return 0;

    /* Match LIVI's stream SETUP dispatcher: `type` is the sole selector. */
    k = o->string_new("type", -1);
    if (!k) return 0;
    v = o->dict_get(obj, k);
    o->release(k);
    if (v && o->int64_value(v, &n) && n >= 0 && (uint64_t)n <= UINT32_MAX) {
        *type_out = (uint32_t)n;
        return 1;
    }
    return 0;
}

alt_setup_obj alt_setup_build_stock_only(const struct alt_setup_cf_ops *o,
                                         alt_setup_obj request,
                                         uint32_t alt_stream_type,
                                         alt_setup_obj *alt_descriptor_out,
                                         unsigned *removed_out) {
    const void *keys[ALT_SETUP_ROOT_MAX];
    const void *vals[ALT_SETUP_ROOT_MAX];
    alt_setup_obj streams_key = NULL, streams = NULL;
    alt_setup_obj stock_streams = NULL, copy = NULL, result = NULL;
    alt_setup_obj found_alt = NULL;
    unsigned root_n, stream_n, i, removed = 0;

    if (alt_descriptor_out) *alt_descriptor_out = NULL;
    if (removed_out) *removed_out = 0;
    if (!ops_ready(o) || !request || !o->is_dict(request)) return NULL;

    root_n = o->dict_count(request);
    if (root_n == 0 || root_n > ALT_SETUP_ROOT_MAX) goto out;
    memset(keys, 0, sizeof(keys));
    memset(vals, 0, sizeof(vals));
    o->dict_get_keys_values(request, keys, vals);

    streams_key = o->string_new("streams", -1);
    if (!streams_key) goto out;
    streams = (alt_setup_obj)o->dict_get(request, streams_key);
    if (!streams || !o->is_array(streams)) goto out;
    stream_n = o->array_count(streams);
    if (stream_n == 0 || stream_n > ALT_SETUP_STREAM_MAX) goto out;

    stock_streams = o->array_create_mutable(NULL, 0, NULL, NULL);
    copy = o->dict_create_mutable(NULL, 0, NULL, NULL, NULL);
    if (!stock_streams || !copy) goto out;

    for (i = 0; i < root_n; ++i) {
        if (!keys[i] || !vals[i]) goto out;
        o->dict_set(copy, keys[i], vals[i]);
    }

    for (i = 0; i < stream_n; ++i) {
        alt_setup_obj item = (alt_setup_obj)o->array_at(streams, i);
        uint32_t t = 0;
        if (!item) goto out;
        if (object_stream_type(o, item, &t) && t == alt_stream_type) {
            ++removed;
            if (!found_alt) found_alt = item;
            continue;
        }
        o->array_append(stock_streams, item);
    }

    /* This helper protects the stock dispatcher, so remove EVERY explicit alt
     * descriptor. Private ownership/uniqueness is a separate fullchain decision.
     * Returning a stock-safe copy for removed>1 preserves Main110 even when the
     * phone sends an ambiguous/repeated 111 request. */
    if (removed == 0) goto out;
    o->dict_set(copy, streams_key, stock_streams);
    if (o->dict_get(copy, streams_key) != stock_streams) goto out;

    result = copy;
    copy = NULL;
    if (alt_descriptor_out) *alt_descriptor_out = found_alt;
    if (removed_out) *removed_out = removed;

out:
    if (!result) {
        if (alt_descriptor_out) *alt_descriptor_out = NULL;
        if (removed_out) *removed_out = 0;
    }
    if (streams_key) o->release(streams_key);
    if (stock_streams) o->release(stock_streams);
    if (copy) o->release(copy);
    return result;
}

int alt_setup_merge_private_response(const struct alt_setup_cf_ops *o,
                                     alt_setup_obj final_response,
                                     alt_setup_obj private_stream_response,
                                     uint32_t alt_stream_type) {
    alt_setup_obj streams_key = NULL, streams = NULL, merged = NULL;
    unsigned n, i;
    uint32_t private_type = 0;
    int ok = 0;

    if (!ops_ready(o) || !final_response || !private_stream_response) return 0;
    if (!o->is_dict(final_response) || !o->is_dict(private_stream_response)) return 0;
    if (!object_stream_type(o, private_stream_response, &private_type) ||
        private_type != alt_stream_type) return 0;

    streams_key = o->string_new("streams", -1);
    if (!streams_key) goto out;
    streams = (alt_setup_obj)o->dict_get(final_response, streams_key);
    /* P1404 returns an empty dictionary when the split request contains an
     * empty streams array (the phone's SETUP contained only type 111).  LIVI
     * answers that transaction with a newly-created streams array containing
     * the private response.  Treat a missing stock array as an empty one; an
     * existing non-array value remains malformed and is rejected. */
    if (streams && !o->is_array(streams)) goto out;
    n = streams ? o->array_count(streams) : 0u;
    if (n > ALT_SETUP_STREAM_MAX) goto out;

    merged = o->array_create_mutable(NULL, 0, NULL, NULL);
    if (!merged) goto out;
    for (i = 0; i < n; ++i) {
        alt_setup_obj item = (alt_setup_obj)o->array_at(streams, i);
        uint32_t t = 0;
        if (!item) goto out;
        if (object_stream_type(o, item, &t) && t == alt_stream_type)
            goto out; /* never overwrite or duplicate an existing 111 response */
        o->array_append(merged, item);
    }
    o->array_append(merged, private_stream_response);
    if (o->array_count(merged) != n + 1u) goto out;
    o->dict_set(final_response, streams_key, merged);
    if (o->dict_get(final_response, streams_key) != merged) goto out;
    ok = 1;

out:
    if (streams_key) o->release(streams_key);
    if (merged) o->release(merged);
    return ok;
}

alt_setup_obj alt_setup_filter_teardown_types(const struct alt_setup_cf_ops *o,
                                              alt_setup_obj request,
                                              const unsigned char stale[256]) {
    const void *keys[ALT_SETUP_ROOT_MAX], *vals[ALT_SETUP_ROOT_MAX];
    alt_setup_obj key = NULL, streams, array = NULL, copy = NULL, result = NULL;
    unsigned roots, n, i, kept = 0;
    if (!ops_ready(o) || !stale || !request || !o->is_dict(request)) return NULL;
    roots = o->dict_count(request);
    if (!roots || roots > ALT_SETUP_ROOT_MAX) return NULL;
    memset(keys, 0, sizeof(keys)); memset(vals, 0, sizeof(vals));
    o->dict_get_keys_values(request, keys, vals);
    key = o->string_new("streams", -1);
    if (!key) goto out;
    streams = (alt_setup_obj)o->dict_get(request, key);
    if (!streams || !o->is_array(streams)) goto out;
    n = o->array_count(streams);
    if (!n || n > 128u) goto out;
    array = o->array_create_mutable(NULL, 0, NULL, NULL);
    copy = o->dict_create_mutable(NULL, 0, NULL, NULL, NULL);
    if (!array || !copy) goto out;
    for (i = 0; i < roots; ++i) {
        if (!keys[i] || !vals[i]) goto out;
        o->dict_set(copy, keys[i], vals[i]);
        if (o->dict_get(copy, keys[i]) != vals[i]) goto out;
    }
    for (i = 0; i < n; ++i) {
        alt_setup_obj item = (alt_setup_obj)o->array_at(streams, i);
        uint32_t type;
        if (!object_stream_type(o, item, &type) || type >= 256u) goto out;
        if (!stale[type]) {
            o->array_append(array, item);
            if (o->array_count(array) != ++kept || o->array_at(array, kept - 1u) != item) goto out;
        }
    }
    if (!kept) goto out;
    o->dict_set(copy, key, array);
    if (o->dict_get(copy, key) != array) goto out;
    result = copy; copy = NULL;
out:
    if (key) o->release(key);
    if (array) o->release(array);
    if (copy) o->release(copy);
    return result;
}
