/* Foundation: NSString and its family, NSLog, NSClassFromString.
 *
 * Every string, constant or created here, has the layout of a compiler-emitted constant string (CFString):
 *   { isa, flags, characters, length }
 * Constant strings come from __cfstring with flags 0x7c8 (8-bit characters) or 0x7d0 (UTF-16 characters).
 * Strings made here hold NUL-terminated UTF-8 in game memory; flag STR_OWNED says the characters are a heap block
 * of their own (freed with the string, grown in place by NSMutableString). Lengths count bytes: the game's
 * strings are ASCII, where bytes and UTF-16 units agree.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt.h"
#include "utf16_access.h"

#define STR_OWNED 0x80000000u
#define STR_UTF16 0x10u /* in the constant-string flags: 0x7d0 vs 0x7c8 */

#define SELF c->r[0]
#define ARG(k) rt_arg(c, (k) + 2)
#define M(name) static void name(cpu_t *c, uint8_t *m)

static id cls_string, cls_mutable, cls_constant;

/* ---- characters ------------------------------------------------------------------------------------------ */
static int is_string(id s) {
    for (id k = rt_object_class(s); k; k = rt_ld32(k + 4))
        if (k == cls_string) return 1;
    return 0;
}

/* host UTF-8 view; UTF-16 constants are converted once into a host buffer kept as the object's host state */
const char *rt_nsstring_utf8(id s) {
    if (!s || !is_string(s)) return NULL;
    uint32_t flags = rt_ld32(s + 4), chars = rt_ld32(s + 8), len = rt_ld32(s + 12);
    if (!(flags & STR_OWNED) && (flags & 0xff) == 0xd0 && (flags & STR_UTF16)) {
        char *buf = rt_objc_state_peek(s);
        if (buf) return buf;
        buf = rt_objc_state(s, (size_t)len * 3 + 1);
        char *o = buf;
        for (uint32_t k = 0; k < len; k++) {
            uint16_t u;
            memcpy(&u, rt_mem + chars + 2 * k, 2);
            if (u < 0x80) *o++ = (char)u;
            else if (u < 0x800) { *o++ = (char)(0xc0 | u >> 6); *o++ = (char)(0x80 | (u & 63)); }
            else { *o++ = (char)(0xe0 | u >> 12); *o++ = (char)(0x80 | ((u >> 6) & 63)); *o++ = (char)(0x80 | (u & 63)); }
        }
        *o = 0;
        return buf;
    }
    return chars ? rt_str(chars) : "";
}

uint32_t rt_nsstring_len(id s) {
    const char *u = rt_nsstring_utf8(s);
    return u ? (uint32_t)strlen(u) : 0;
}

/* fill a string object with a copy of `n` bytes */
static id str_set(id s, const char *p, uint32_t n) {
    uint32_t old = rt_ld32(s + 8), flags = rt_ld32(s + 4);
    uint32_t buf = rt_malloc(n + 1);
    memcpy(rt_mem + buf, p, n);
    rt_mem[buf + n] = 0;
    rt_st32(s + 4, STR_OWNED);
    rt_st32(s + 8, buf);
    rt_st32(s + 12, n);
    if ((flags & STR_OWNED) && old) rt_free(old);
    return s;
}

static id str_new(id cls, const char *p, uint32_t n) { return str_set(rt_objc_alloc(cls), p, n); }

id rt_nsstring_new(const char *utf8) { return str_new(cls_string, utf8, (uint32_t)strlen(utf8)); }
id rt_nsstring(const char *utf8) { return rt_autorelease(rt_nsstring_new(utf8)); }

static id str_auto(cpu_t *c, const char *p, uint32_t n) {
    (void)c;
    return rt_autorelease(str_new(cls_string, p, n));
}

/* the class a +string... method should produce: the receiver, or NSString for the abstract class */
static id result_class(cpu_t *c) { return SELF == cls_constant ? cls_string : SELF; }

/* format with the arguments that follow argument word `first` (or a va_list) */
static char *format(cpu_t *c, id fmt, int first, uint32_t va) {
    const char *f = rt_nsstring_utf8(fmt);
    if (!f) return strdup("");
    rt_args a = {c, first, va};
    char small[512];
    int n = rt_vformat(small, sizeof small, f, &a);
    char *out = malloc((size_t)n + 1);
    if (n < (int)sizeof small) { memcpy(out, small, (size_t)n + 1); return out; }
    rt_args b = {c, first, va};
    rt_vformat(out, (size_t)n + 1, f, &b);
    return out;
}

/* ---- class methods ----------------------------------------------------------------------------------------- */
M(s_string) { (void)m; rt_ret(c, rt_autorelease(str_new(result_class(c), "", 0))); }
M(s_with_utf8) {
    (void)m;
    const char *p = rt_str(ARG(0));
    rt_ret(c, p ? rt_autorelease(str_new(result_class(c), p, (uint32_t)strlen(p))) : 0);
}
M(s_with_string) {
    (void)m;
    const char *p = rt_nsstring_utf8(ARG(0));
    rt_ret(c, rt_autorelease(str_new(result_class(c), p ? p : "", p ? (uint32_t)strlen(p) : 0)));
}
M(s_with_format) {
    (void)m;
    char *s = format(c, ARG(0), 3, 0);
    rt_ret(c, rt_autorelease(str_new(result_class(c), s, (uint32_t)strlen(s))));
    free(s);
}
M(s_with_capacity) { (void)m; rt_ret(c, rt_autorelease(str_new(cls_mutable, "", 0))); }

/* ---- initializers (on an allocated instance) ----------------------------------------------------------------- */
M(s_init) { (void)m; str_set(SELF, "", 0); }
M(s_init_utf8) {
    (void)m;
    const char *p = rt_str(ARG(0));
    if (!p) { rt_objc_free(SELF); rt_ret(c, 0); return; }
    str_set(SELF, p, (uint32_t)strlen(p));
}
M(s_init_bytes) { /* initWithBytes:length:encoding: and initWithCString:length: */
    (void)m;
    str_set(SELF, (const char *)rt_mem + ARG(0), ARG(1));
}
M(s_init_string) {
    (void)m;
    const char *p = rt_nsstring_utf8(ARG(0));
    str_set(SELF, p ? p : "", p ? (uint32_t)strlen(p) : 0);
}
M(s_init_format) {
    (void)m;
    char *s = format(c, ARG(0), 3, 0);
    str_set(SELF, s, (uint32_t)strlen(s));
    free(s);
}
M(s_init_format_args) { /* initWithFormat:arguments: */
    (void)m;
    char *s = format(c, ARG(0), 0, ARG(1));
    str_set(SELF, s, (uint32_t)strlen(s));
    free(s);
}

/* ---- queries ------------------------------------------------------------------------------------------------- */
static const char *U(cpu_t *c) { const char *s = rt_nsstring_utf8(SELF); return s ? s : ""; }

M(s_utf8) {
    (void)m;
    uint32_t flags = rt_ld32(SELF + 4);
    if ((flags & 0xff) == 0xd0 && !(flags & STR_OWNED)) { rt_ret(c, rt_strdup_guest(U(c))); return; } /* leaks once */
    rt_ret(c, rt_ld32(SELF + 8));
}
M(s_get_cstring) { /* getCString:maxLength:encoding: */
    (void)m;
    const char *s = U(c);
    uint32_t dst = ARG(0), max = ARG(1), n = (uint32_t)strlen(s);
    if (n + 1 > max) { rt_ret(c, 0); return; }
    memcpy(rt_mem + dst, s, n + 1);
    rt_ret(c, 1);
}
M(s_length) { (void)m; const char *s = U(c); rt_ret(c, utf16_units(s, s + strlen(s))); }
M(s_char_at) { (void)m; rt_ret(c, utf16_unit_at(U(c), ARG(0))); }
M(s_is_equal) {
    (void)m;
    const char *o = rt_nsstring_utf8(ARG(0));
    rt_ret(c, o && !strcmp(U(c), o));
}
M(s_hash) {
    (void)m;
    uint32_t h = 0;
    for (const char *p = U(c); *p; p++) h = h * 31 + (uint8_t)*p;
    rt_ret(c, h);
}
M(s_compare) {
    (void)m;
    const char *o = rt_nsstring_utf8(ARG(0));
    int r = strcmp(U(c), o ? o : "");
    rt_ret(c, (uint32_t)(r < 0 ? -1 : r > 0));
}
M(s_casecmp) {
    (void)m;
    const char *o = rt_nsstring_utf8(ARG(0));
    int r = strcasecmp(U(c), o ? o : "");
    rt_ret(c, (uint32_t)(r < 0 ? -1 : r > 0));
}
M(s_has_prefix) {
    (void)m;
    const char *o = rt_nsstring_utf8(ARG(0));
    rt_ret(c, o && !strncmp(U(c), o, strlen(o)));
}
M(s_has_suffix) {
    (void)m;
    const char *o = rt_nsstring_utf8(ARG(0)), *s = U(c);
    size_t a = strlen(s), b = o ? strlen(o) : 0;
    rt_ret(c, o && b <= a && !strcmp(s + a - b, o));
}
M(s_int) { (void)m; rt_ret(c, (uint32_t)atoi(U(c))); }
M(s_float) { (void)m; rt_fret(c, (float)atof(U(c))); }
M(s_double) { (void)m; rt_dret(c, atof(U(c))); }
M(s_bool) {
    (void)m;
    const char *s = U(c);
    while (*s == ' ') s++;
    rt_ret(c, *s == 'Y' || *s == 'y' || *s == 'T' || *s == 't' || (*s >= '1' && *s <= '9'));
}
M(s_description) { (void)m; (void)c; } /* a string describes itself */

/* ---- derived strings ----------------------------------------------------------------------------------------- */
M(s_append) {
    (void)m;
    const char *a = U(c), *b = rt_nsstring_utf8(ARG(0));
    size_t na = strlen(a), nb = b ? strlen(b) : 0;
    char *t = malloc(na + nb + 1);
    memcpy(t, a, na);
    memcpy(t + na, b ? b : "", nb + 1);
    rt_ret(c, str_auto(c, t, (uint32_t)(na + nb)));
    free(t);
}
M(s_append_format) {
    (void)m;
    char *f = format(c, ARG(0), 3, 0);
    const char *a = U(c);
    size_t na = strlen(a), nb = strlen(f);
    char *t = malloc(na + nb + 1);
    memcpy(t, a, na);
    memcpy(t + na, f, nb + 1);
    rt_ret(c, str_auto(c, t, (uint32_t)(na + nb)));
    free(t);
    free(f);
}
M(s_append_path) {
    (void)m;
    const char *a = U(c), *b = rt_nsstring_utf8(ARG(0));
    char *t = malloc(strlen(a) + (b ? strlen(b) : 0) + 2);
    size_t na = strlen(a);
    while (na > 1 && a[na - 1] == '/') na--;
    memcpy(t, a, na);
    t[na] = 0;
    if (b && *b) {
        while (*b == '/') b++;
        if (na) strcat(t, "/");
        strcat(t, b);
    }
    rt_ret(c, str_auto(c, t, (uint32_t)strlen(t)));
    free(t);
}
M(s_last_path) {
    (void)m;
    const char *s = U(c), *p = strrchr(s, '/');
    p = p ? p + 1 : s;
    rt_ret(c, str_auto(c, p, (uint32_t)strlen(p)));
}
M(s_delete_last_path) {
    (void)m;
    const char *s = U(c), *p = strrchr(s, '/');
    uint32_t n = p ? (uint32_t)(p - s) : 0;
    if (p == s) n = 1;
    rt_ret(c, str_auto(c, s, n));
}
M(s_path_ext) {
    (void)m;
    const char *s = U(c), *slash = strrchr(s, '/'), *dot = strrchr(s, '.');
    if (!dot || (slash && dot < slash)) dot = s + strlen(s) - 0;
    else dot++;
    rt_ret(c, str_auto(c, dot, (uint32_t)strlen(dot)));
}
M(s_delete_ext) {
    (void)m;
    const char *s = U(c), *slash = strrchr(s, '/'), *dot = strrchr(s, '.');
    uint32_t n = (uint32_t)strlen(s);
    if (dot && (!slash || dot > slash)) n = (uint32_t)(dot - s);
    rt_ret(c, str_auto(c, s, n));
}
static void caseconv(cpu_t *c, int up) {
    char *t = strdup(U(c));
    for (char *p = t; *p; p++) *p = (char)(up ? toupper((uint8_t)*p) : tolower((uint8_t)*p));
    rt_ret(c, str_auto(c, t, (uint32_t)strlen(t)));
    free(t);
}
M(s_lower) { (void)m; caseconv(c, 0); }
M(s_upper) { (void)m; caseconv(c, 1); }
M(s_substr_from) {
    (void)m;
    const char *s = U(c);
    const char *start = utf16_boundary(s, ARG(0));
    rt_ret(c, str_auto(c, start, (uint32_t)strlen(start)));
}
M(s_substr_to) {
    (void)m;
    const char *s = U(c);
    rt_ret(c, str_auto(c, s, (uint32_t)(utf16_boundary(s, ARG(0)) - s)));
}
M(s_substr_range) { /* substringWithRange:(NSRange){loc, len} in two argument words */
    (void)m;
    const char *s = U(c);
    uint64_t loc = ARG(0), len = ARG(1);
    const char *start = utf16_boundary(s, loc), *end = utf16_boundary(s, loc + len);
    rt_ret(c, str_auto(c, start, (uint32_t)(end - start)));
}
/* rangeOfString: returns an NSRange through objc_msgSend_stret: r0 result, r1 self, r2 _cmd, r3 the argument */
M(s_range_of) {
    (void)m;
    const char *s = rt_nsstring_utf8(c->r[1]), *o = rt_nsstring_utf8(c->r[3]);
    const char *hit = (s && o && *o) ? strstr(s, o) : NULL;
    rt_st32(c->r[0], hit ? utf16_units(s, hit) : 0x7fffffffu); /* NSNotFound */
    rt_st32(c->r[0] + 4, hit ? utf16_units(o, o + strlen(o)) : 0);
}
M(s_copy) {
    (void)m;
    if (rt_object_class(SELF) == cls_mutable) {
        const char *s = U(c);
        rt_ret(c, str_new(cls_string, s, (uint32_t)strlen(s)));
    } else {
        rt_ret(c, rt_retain(SELF));
    }
}
M(s_mutable_copy) { (void)m; const char *s = U(c); rt_ret(c, str_new(cls_mutable, s, (uint32_t)strlen(s))); }
M(s_dealloc) {
    (void)m;
    if (rt_ld32(SELF + 4) & STR_OWNED) rt_free(rt_ld32(SELF + 8));
    rt_objc_free(SELF);
}

/* ---- NSMutableString ----------------------------------------------------------------------------------------- */
static void mut_set(cpu_t *c, const char *a, size_t na, const char *b, size_t nb) {
    char *t = malloc(na + nb + 1);
    memcpy(t, a, na);
    memcpy(t + na, b, nb);
    t[na + nb] = 0;
    str_set(SELF, t, (uint32_t)(na + nb));
    free(t);
}
M(ms_append) {
    (void)m;
    const char *b = rt_nsstring_utf8(ARG(0));
    if (b) mut_set(c, U(c), strlen(U(c)), b, strlen(b));
}
M(ms_append_format) {
    (void)m;
    char *f = format(c, ARG(0), 3, 0);
    mut_set(c, U(c), strlen(U(c)), f, strlen(f));
    free(f);
}
M(ms_set) {
    (void)m;
    const char *b = rt_nsstring_utf8(ARG(0));
    str_set(SELF, b ? b : "", b ? (uint32_t)strlen(b) : 0);
}

/* constant strings live forever */
M(k_retain) { (void)m; (void)c; }
M(k_release) { (void)m; (void)c; }

/* ---- functions ----------------------------------------------------------------------------------------------- */
static void imp_NSLog(cpu_t *c) {
    char *s = format(c, rt_arg(c, 0), 1, 0);
    rt_log("NSLog: %s", s);
    free(s);
}

static void imp_NSClassFromString(cpu_t *c) {
    const char *n = rt_nsstring_utf8(rt_arg(c, 0));
    rt_ret(c, n ? rt_class(n) : 0);
}

void rt_foundation_define(void) {
    cls_string = rt_objc_define("NSString", "NSObject", 12);
    cls_mutable = rt_objc_define("NSMutableString", "NSString", 0);
    cls_constant = rt_objc_define("__NSCFConstantString", "NSString", 0);
    struct { const char *sel; rt_hostfn fn; } cm[] = {
        {"string", s_string}, {"stringWithUTF8String:", s_with_utf8}, {"stringWithCString:", s_with_utf8},
        {"stringWithCString:encoding:", s_with_utf8}, {"stringWithString:", s_with_string},
        {"stringWithFormat:", s_with_format}, {"stringWithCapacity:", s_with_capacity},
    };
    for (size_t k = 0; k < sizeof cm / sizeof *cm; k++) rt_objc_cmethod(cls_string, cm[k].sel, cm[k].fn);
    struct { const char *sel; rt_hostfn fn; } im[] = {
        {"init", s_init}, {"initWithUTF8String:", s_init_utf8}, {"initWithCString:", s_init_utf8},
        {"initWithCString:encoding:", s_init_utf8}, {"initWithBytes:length:encoding:", s_init_bytes},
        {"initWithCString:length:", s_init_bytes}, {"initWithString:", s_init_string},
        {"initWithFormat:", s_init_format}, {"initWithFormat:arguments:", s_init_format_args},
        {"UTF8String", s_utf8}, {"cString", s_utf8}, {"cStringUsingEncoding:", s_utf8},
        {"fileSystemRepresentation", s_utf8}, {"lossyCString", s_utf8},
        {"getCString:maxLength:encoding:", s_get_cstring}, {"length", s_length},
        {"characterAtIndex:", s_char_at}, {"isEqualToString:", s_is_equal}, {"isEqual:", s_is_equal},
        {"hash", s_hash}, {"compare:", s_compare}, {"caseInsensitiveCompare:", s_casecmp},
        {"hasPrefix:", s_has_prefix}, {"hasSuffix:", s_has_suffix}, {"intValue", s_int}, {"integerValue", s_int},
        {"floatValue", s_float}, {"doubleValue", s_double}, {"boolValue", s_bool}, {"description", s_description},
        {"stringByAppendingString:", s_append}, {"stringByAppendingFormat:", s_append_format},
        {"stringByAppendingPathComponent:", s_append_path}, {"lastPathComponent", s_last_path},
        {"stringByDeletingLastPathComponent", s_delete_last_path}, {"pathExtension", s_path_ext},
        {"stringByDeletingPathExtension", s_delete_ext}, {"lowercaseString", s_lower},
        {"uppercaseString", s_upper}, {"substringFromIndex:", s_substr_from}, {"substringToIndex:", s_substr_to},
        {"substringWithRange:", s_substr_range}, {"rangeOfString:", s_range_of}, {"copy", s_copy},
        {"copyWithZone:", s_copy}, {"mutableCopy", s_mutable_copy}, {"mutableCopyWithZone:", s_mutable_copy},
        {"dealloc", s_dealloc},
    };
    for (size_t k = 0; k < sizeof im / sizeof *im; k++) rt_objc_method(cls_string, im[k].sel, im[k].fn);
    rt_objc_method(cls_mutable, "appendString:", ms_append);
    rt_objc_method(cls_mutable, "appendFormat:", ms_append_format);
    rt_objc_method(cls_mutable, "setString:", ms_set);
    rt_objc_method(cls_constant, "retain", k_retain);
    rt_objc_method(cls_constant, "release", k_release);
    rt_objc_method(cls_constant, "autorelease", k_retain);
    rt_objc_method(cls_constant, "dealloc", k_release);
    rt_import_register("NSLog", imp_NSLog);
    rt_import_register("NSClassFromString", imp_NSClassFromString);
}
