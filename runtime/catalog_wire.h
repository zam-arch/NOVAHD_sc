#ifndef NOVA_CATALOG_WIRE_H
#define NOVA_CATALOG_WIRE_H

#include <string.h>

enum { TRACK_GROUP, TRACK_ID, TRACK_TITLE, TRACK_ARTIST, TRACK_ALBUM, TRACK_FIELDS };

static inline int music_catalog_row(const char **cursor, const char *end, const char *fields[TRACK_FIELDS]) {
    const char *p = *cursor;
    for (int k = 0; k < TRACK_FIELDS; ++k) {
        const char *zero = memchr(p, '\0', (size_t)(end - p));
        if (!zero) return 0;
        fields[k] = p;
        p = zero + 1;
    }
    *cursor = p;
    return 1;
}

#endif
