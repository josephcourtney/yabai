#ifndef LAYOUT_MATCH_H
#define LAYOUT_MATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Window titles may change between saves and restores. Prefer refusing a
 * restore to guessing which of several same-app windows belongs to a leaf.
 * No fallback to application-only or arbitrary window order is permitted. */
struct layout_match_key {
    const char *app;
    const char *title;
};

enum layout_match_result {
    LAYOUT_MATCH_OK,
    LAYOUT_MATCH_INVALID,
    LAYOUT_MATCH_MISSING,
    LAYOUT_MATCH_AMBIGUOUS,
    LAYOUT_MATCH_ALREADY_USED
};

static inline enum layout_match_result
layout_match_unique(const char *app, const char *title,
                    const struct layout_match_key *keys,
                    const bool *used, size_t count, size_t *index)
{
    if (!app || !title || !keys || !used || !index) return LAYOUT_MATCH_INVALID;

    size_t found = count;
    for (size_t i = 0; i < count; ++i) {
        if (keys[i].app && keys[i].title &&
            strcmp(keys[i].app, app) == 0 &&
            strcmp(keys[i].title, title) == 0) {
            if (found != count) return LAYOUT_MATCH_AMBIGUOUS;
            found = i;
        }
    }

    if (found == count) return LAYOUT_MATCH_MISSING;
    if (used[found]) return LAYOUT_MATCH_ALREADY_USED;
    *index = found;
    return LAYOUT_MATCH_OK;
}

#endif
