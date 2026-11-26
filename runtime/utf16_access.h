#ifndef NOVA_UTF16_ACCESS_H
#define NOVA_UTF16_ACCESS_H
#include <stdint.h>
#include <stddef.h>

static inline uint32_t utf8_scalar(const char **cursor) {
    const unsigned char *p = (const unsigned char *)*cursor;
    uint32_t c = *p++;
    if (c >= 0x80) {
        int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc2 ? 1 : 0;
        if (!n || c > 0xf4) c = 0xfffd;
        else {
            c &= 0x7fu >> (n + 1);
            static const uint32_t minimum[] = {0, 0x80, 0x800, 0x10000};
            int k = 0;
            while (k < n && (p[k] & 0xc0) == 0x80) c = (c << 6) | (p[k++] & 0x3f);
            if (k == n && c >= minimum[n] && c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff))
                p += n;
            else c = 0xfffd;
        }
    }
    *cursor = (const char *)p;
    return c;
}

static inline uint32_t utf16_units(const char *start, const char *end) {
    uint32_t count = 0;
    while (start < end) count += utf8_scalar(&start) > 0xffff ? 2 : 1;
    return count;
}

static inline uint16_t utf16_unit_at(const char *text, uint32_t index) {
    while (*text) {
        uint32_t cp = utf8_scalar(&text);
        if (cp > 0xffff) {
            cp -= 0x10000;
            if (index == 0) return (uint16_t)(0xd800 + (cp >> 10));
            if (index == 1) return (uint16_t)(0xdc00 + (cp & 0x3ff));
            index -= 2;
        } else if (index-- == 0) return (uint16_t)cp;
    }
    return 0;
}

static inline const char *utf16_boundary(const char *text, uint64_t index) {
    while (index && *text) {
        const char *next = text;
        unsigned len = utf8_scalar(&next) > 0xffff ? 2 : 1;
        if (len > index) break;
        index -= len;
        text = next;
    }
    return text;
}
#endif
