/* Everything the game can live without: networking, local multiplayer, MediaPlayer, reachability, the in-game
 * store / ads / news screens. Each API answers the way the real one does on an iPad with no network, no
 * Bluetooth peers and no music library, so the game takes its own offline paths.
 *
 * What the game does with these (from the disassembly of N.O.V.A. HD):
 *   - Reachability (Apple's sample class, in the game): SCNetworkReachabilityCreateWithName/WithAddress, then
 *     GetFlags. Flags 0 = not reachable; GetFlags still returns true (the flags are valid).
 *   - BSD sockets (the multiplayer layer, the IGP/GLLive HTTP code, a DNS thread): every caller checks socket()
 *     for -1 and gethostbyname() for NULL. socket() fails here, so no descriptor ever exists and every call on
 *     one fails with EBADF (negative fd) or ENOTSOCK. Pure computations work: inet_addr/inet_aton/inet_ntoa,
 *     getprotobyname (the table in /etc/protocols), getaddrinfo/gethostbyname for numeric hosts and localhost.
 *     select() with no descriptors is a sleep. fcntl's advisory commands (F_RDADVISE on a file) succeed.
 *     errno: libc.c keeps the per-thread errno word behind the __error import. rt.h has no accessor for it, so
 *     set_errno calls that import through rc_import (recomp.h) to get the word's address, as game code would.
 *   - MoviePlayer (the game's intro player): -[MoviePlayer initWithFile:] makes an MPMoviePlayerViewController
 *     and observes MPMoviePlayerPlaybackDidFinish / PlaybackStateDidChange / Will/DidExitFullscreen with object
 *     [mpvc moviePlayer]; -playMovie sends presentMoviePlayerViewControllerAnimated:, spins CFRunLoopRunInMode
 *     until its execPlay timer fires, then sets isPlaying = 1. -moviePlayBackDidFinish: (and a state change to
 *     MPMoviePlaybackStateStopped) clear isPlaying, which the C++ side polls. So here the movie "plays" for
 *     MOVIE_SECONDS (long enough for playMovie to have returned) and then finishes: on a run loop callback the
 *     controller posts PlaybackStateDidChange (stopped) and PlaybackDidFinish (reason Ended).
 *   - MPMusicPlayerController iPodMusicPlayer/applicationMusicPlayer: a stopped player with no item; it never
 *     posts its notifications. MPMediaQuery: no items. MPMediaPickerController: the user cancels at once
 *     (mediaPickerDidCancel: on the next run loop turn after its view is first asked for).
 *   - NSURLConnection: the delegate gets connection:didFailWithError: (NSURLErrorDomain -1009, "The Internet
 *     connection appears to be offline.", userInfo with NSErrorFailingURLStringKey, which
 *     -[AppController connection:didFailWithError:] reads) on the next run loop turn.
 *   - GKSession: Bluetooth is off. -[BluetoothController initWithProtocol] makes a session and sends
 *     setAvailable:YES; the delegate then gets session:didFailWithError: with GKSessionCannotEnableError
 *     (30509), the one code -[BluetoothController session:didFailWithError:] checks to clear the flag that
 *     -isBluetoothEnabled reports. Sends fail with NO and there are never any peers.
 *   - NSNetService, NSNetServiceBrowser: work but never find, resolve or connect anybody.
 *   - UIWebView, UITableView, UITableViewCell, UIImagePickerController (no source type available),
 *     UIPopoverController, UITabBarController: inert. Tables never ask their data source for rows.
 *
 * Host state: NSObject subclasses defined here keep theirs with rt_objc_state. Subclasses of UIView and
 * UIViewController must not (uikit.c keeps its view state there), so they use a small side table (ext_*) that
 * their -dealloc clears before calling the superclass's -dealloc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "rt.h"
#include "catalog_wire.h"
#ifdef __ANDROID__
#include <pthread.h>
#endif

#define SELF c->r[0]
#define ARG(k) rt_arg(c, (k) + 2)
#define M(name) static void name(cpu_t *c, uint8_t *m)
#define A(k) rt_arg(c, k)
#define RET(v) rt_ret(c, (uint32_t)(v))
#define IMP(name) static void imp_##name(cpu_t *c)
#define REG(name) rt_import_register(#name, imp_##name)

#define MOVIE_SECONDS 0.5 /* how long the intro movie "plays" */

/* Darwin errno values */
enum { D_EBADF = 9, D_EACCES = 13, D_EINVAL = 22, D_ENOTTY = 25, D_ENOTSOCK = 38 };
/* Darwin getaddrinfo errors and flags */
enum { D_EAI_FAMILY = 5, D_EAI_NONAME = 8, D_EAI_SERVICE = 9 };
enum { D_AI_PASSIVE = 1, D_AI_CANONNAME = 2, D_AI_NUMERICHOST = 4 };
enum { D_AF_UNSPEC = 0, D_AF_INET = 2, D_SOCK_STREAM = 1, D_SOCK_DGRAM = 2 };

static void log_once(int *flag, const char *what) {
    if (!*flag) { *flag = 1; rt_log("offline: %s", what); }
}

/* ---- small shared method bodies ----------------------------------------------------------------------- */
M(ret_self) { (void)m; (void)c; }
M(ret_zero) { (void)m; RET(0); }
M(ret_one) { (void)m; RET(1); }
M(ret_dzero) { (void)m; rt_dret(c, 0.0); }
M(ret_empty_array) { /* an autoreleased empty NSArray */
    (void)m;
    id none = 0;
    RET(rt_autorelease(rt_nsarray_new(&none, 0)));
}

static id super_of(id cls) { return cls ? rt_ld32(cls + 4) : 0; } /* class_t.superclass */

/* run the superclass's implementation of `sel` on self with the same argument words */
static uint32_t call_super(cpu_t *c, id cls, const char *sel, int nargs) {
    SEL s = rt_sel(sel);
    uint32_t imp = rt_lookup(super_of(cls), s);
    if (!imp) return 0;
    uint32_t w[8] = {SELF, s};
    for (int k = 0; k < nargs && k < 6; k++) w[2 + k] = ARG(k);
    return rt_callv(c, imp, 2 + nargs, w);
}

static void set_retained(cpu_t *c, id *slot, id v) { id old = *slot; *slot = rt_retain(v); rt_release(c, old); }

/* ---- side table for UIView / UIViewController subclasses ---------------------------------------------- */
typedef struct { id obj; id a[4]; uint32_t w[4]; } ext_t;
static ext_t **exts;
static uint32_t n_ext, cap_ext;
static volatile int ext_lock;

static void elk(void) { while (__atomic_exchange_n(&ext_lock, 1, __ATOMIC_ACQUIRE)) {} }
static void eulk(void) { __atomic_store_n(&ext_lock, 0, __ATOMIC_RELEASE); }

static ext_t *ext(id obj) {
    elk();
    for (uint32_t k = 0; k < n_ext; k++)
        if (exts[k]->obj == obj) { ext_t *e = exts[k]; eulk(); return e; }
    if (n_ext == cap_ext) {
        cap_ext = cap_ext ? cap_ext * 2 : 32;
        exts = realloc(exts, sizeof *exts * cap_ext);
    }
    ext_t *e = calloc(1, sizeof *e);
    e->obj = obj;
    exts[n_ext++] = e;
    eulk();
    return e;
}

/* release the retained slots listed in `owned` (a bit per a[k]) and forget the entry */
static void ext_drop(cpu_t *c, id obj, unsigned owned) {
    ext_t *e = NULL;
    elk();
    for (uint32_t k = 0; k < n_ext; k++)
        if (exts[k]->obj == obj) { e = exts[k]; exts[k] = exts[--n_ext]; break; }
    eulk();
    if (!e) return;
    for (int k = 0; k < 4; k++)
        if (owned & (1u << k)) rt_release(c, e->a[k]);
    free(e);
}

/* ---- errno through libc.c's __error ------------------------------------------------------------------- */
static void set_errno(cpu_t *c, uint32_t e) {
    static uint32_t slot = ~1u;
    if (slot == ~1u) slot = rt_import_slot("__error");
    if (slot == ~0u) return;
    uint32_t r[4] = {c->r[0], c->r[1], c->r[2], c->r[3]}, pc = c->r[15];
    rc_import(c, rt_mem, slot);
    uint32_t word = c->r[0];
    c->r[0] = r[0]; c->r[1] = r[1]; c->r[2] = r[2]; c->r[3] = r[3]; c->r[15] = pc;
    if (word) rt_st32(word, e);
}

static void fail(cpu_t *c, uint32_t e) { set_errno(c, e); RET(-1); }
static void fail_fd(cpu_t *c, int32_t fd) { fail(c, fd < 0 ? D_EBADF : D_ENOTSOCK); } /* no socket ever exists */

/* ---- NSString constants (data imports) -------------------------------------------------------------- */
static const char *const const_names[] = {
    "MPMediaItemPropertyAlbumTitle", "MPMediaItemPropertyArtist", "MPMediaItemPropertyTitle",
    "MPMediaPlaylistPropertyName", "MPMoviePlayerDidExitFullscreenNotification",
    "MPMoviePlayerPlaybackDidFinishNotification", "MPMoviePlayerPlaybackStateDidChangeNotification",
    "MPMoviePlayerWillExitFullscreenNotification", "MPMusicPlayerControllerNowPlayingItemDidChangeNotification",
    "MPMusicPlayerControllerPlaybackStateDidChangeNotification", "NSErrorFailingURLStringKey",
};
/* the strings' values (the real constants are these strings) */
static const char *const const_values[] = {
    "albumTitle", "artist", "title", "name", "MPMoviePlayerDidExitFullscreenNotification",
    "MPMoviePlayerPlaybackDidFinishNotification", "MPMoviePlayerPlaybackStateDidChangeNotification",
    "MPMoviePlayerWillExitFullscreenNotification", "MPMusicPlayerControllerNowPlayingItemDidChangeNotification",
    "MPMusicPlayerControllerPlaybackStateDidChangeNotification", "NSErrorFailingURLStringKey",
};
#define NCONST (sizeof const_names / sizeof *const_names)
enum { K_FAILING_URL_STRING = 10 };
static uint32_t const_words[NCONST];

static uint32_t const_word(unsigned k) {
    if (!const_words[k]) {
        const_words[k] = rt_calloc(1, 4);
        rt_st32(const_words[k], rt_nsstring_new(const_values[k]));
    }
    return const_words[k];
}
static id const_string(unsigned k) { return rt_ld32(const_word(k)); }

static uint32_t resolve(const char *name) {
    for (unsigned k = 0; k < NCONST; k++)
        if (!strcmp(name, const_names[k])) return const_word(k);
    return 0;
}

/* a +1 dictionary from (C string key, object) pairs; the values are released by the caller */
static id dict_of(cpu_t *c, int n, const char *const *keys, const id *values) {
    id d = rt_nsdict_new();
    for (int k = 0; k < n; k++) {
        if (!values[k]) continue;
        id key = rt_nsstring_new(keys[k]);
        rt_nsdict_set(d, key, values[k]);
        rt_release(c, key);
    }
    return d;
}

/* ============================== SystemConfiguration ================================================== */
static id cls_reach;

/* SCNetworkReachabilityCreateWithName(allocator, nodename) / ...WithAddress(allocator, sockaddr *): +1 */
IMP(SCNetworkReachabilityCreateWithName) { RET(rt_objc_alloc(cls_reach)); }
IMP(SCNetworkReachabilityCreateWithAddress) { RET(rt_objc_alloc(cls_reach)); }
/* GetFlags(target, flags *): no flag set = not reachable; the flags are valid, so true */
IMP(SCNetworkReachabilityGetFlags) {
    if (A(1)) rt_st32(A(1), 0);
    RET(A(0) != 0);
}
/* the callback is accepted and never called: reachability never changes */
IMP(SCNetworkReachabilitySetCallback) { RET(1); }
IMP(SCNetworkReachabilityScheduleWithRunLoop) { RET(1); }
IMP(SCNetworkReachabilityUnscheduleFromRunLoop) { RET(1); }

/* ============================== BSD sockets and DNS ================================================== */
static uint32_t bswap32(uint32_t v) { return v >> 24 | (v >> 8 & 0xff00) | (v << 8 & 0xff0000) | v << 24; }

/* BSD inet_aton: a, a.b, a.b.c or a.b.c.d with decimal, octal (0...) or hex (0x...) parts; host order out */
static int parse_inet(const char *cp, uint32_t *out) {
    uint32_t parts[4];
    int n = 0;
    if (!cp) return 0;
    for (;;) {
        if (*cp < '0' || *cp > '9') return 0;
        uint32_t base = 10, v = 0;
        if (*cp == '0') {
            cp++;
            if (*cp == 'x' || *cp == 'X') { base = 16; cp++; } else base = 8;
        }
        for (;; cp++) {
            int d;
            if (*cp >= '0' && *cp <= '9') d = *cp - '0';
            else if (base == 16 && *cp >= 'a' && *cp <= 'f') d = *cp - 'a' + 10;
            else if (base == 16 && *cp >= 'A' && *cp <= 'F') d = *cp - 'A' + 10;
            else break;
            if ((uint32_t)d >= base) return 0;
            uint64_t nv = (uint64_t)v * base + (uint32_t)d;
            if (nv > 0xffffffffu) return 0;
            v = (uint32_t)nv;
        }
        if (n == 4) return 0;
        parts[n++] = v;
        if (*cp == '.') { cp++; continue; }
        break;
    }
    if (*cp && *cp != ' ' && *cp != '\t' && *cp != '\n') return 0;
    uint32_t v = parts[n - 1];
    switch (n) {
    case 1: break;
    case 2: if (parts[0] > 0xff || v > 0xffffff) return 0; v |= parts[0] << 24; break;
    case 3: if (parts[0] > 0xff || parts[1] > 0xff || v > 0xffff) return 0; v |= parts[0] << 24 | parts[1] << 16; break;
    case 4:
        if (parts[0] > 0xff || parts[1] > 0xff || parts[2] > 0xff || v > 0xff) return 0;
        v |= parts[0] << 24 | parts[1] << 16 | parts[2] << 8;
        break;
    }
    *out = v;
    return 1;
}

/* in_addr_t inet_addr(const char *): network order (the word as it sits in memory), INADDR_NONE on error */
IMP(inet_addr) {
    uint32_t h;
    RET(parse_inet(rt_str(A(0)), &h) ? bswap32(h) : 0xffffffffu);
}
/* int inet_aton(const char *, struct in_addr *) */
IMP(inet_aton) {
    uint32_t h;
    if (!parse_inet(rt_str(A(0)), &h)) { RET(0); return; }
    if (A(1)) rt_st32(A(1), bswap32(h));
    RET(1);
}
/* char *inet_ntoa(struct in_addr): the struct travels by value in r0; the result is one static buffer, like
 * the real one */
IMP(inet_ntoa) {
    static uint32_t buf;
    if (!buf) buf = rt_calloc(1, 16);
    uint32_t w = A(0);
    snprintf((char *)G(buf), 16, "%u.%u.%u.%u", w & 0xff, w >> 8 & 0xff, w >> 16 & 0xff, w >> 24);
    RET(buf);
}

/* struct protoent { char *p_name; char **p_aliases; int p_proto; } from /etc/protocols */
IMP(getprotobyname) {
    static const struct { const char *name, *alias; int proto; } tab[] = {
        {"ip", "IP", 0}, {"icmp", "ICMP", 1}, {"tcp", "TCP", 6}, {"udp", "UDP", 17},
    };
    static uint32_t ents[4];
    const char *n = rt_str(A(0));
    for (int k = 0; n && k < 4; k++) {
        if (strcmp(n, tab[k].name) && strcmp(n, tab[k].alias)) continue;
        if (!ents[k]) {
            uint32_t e = rt_calloc(1, 12 + 8);
            rt_st32(e + 12, rt_static_string(tab[k].alias));
            rt_st32(e + 16, 0);
            rt_st32(e + 0, rt_static_string(tab[k].name));
            rt_st32(e + 4, e + 12);
            rt_st32(e + 8, (uint32_t)tab[k].proto);
            ents[k] = e;
        }
        RET(ents[k]);
        return;
    }
    RET(0);
}

/* numeric hosts and localhost resolve without a network; names do not. Host order out. */
static int resolve_host(const char *name, uint32_t *addr) {
    if (!name) return 0;
    if (!strcmp(name, "localhost")) { *addr = 0x7f000001u; return 1; }
    return parse_inet(name, addr);
}

/* struct hostent { char *h_name; char **h_aliases; int h_addrtype; int h_length; char **h_addr_list; }:
 * one static result, like the real one */
IMP(gethostbyname) {
    static uint32_t he;
    uint32_t h;
    const char *name = rt_str(A(0));
    if (!resolve_host(name, &h)) { RET(0); return; }
    if (!he) he = rt_calloc(1, 20 + 4 + 8 + 4 + 64);
    /* layout: hostent | aliases[1] | addr_list[2] | address | name */
    uint32_t aliases = he + 20, list = he + 24, addr = he + 32, nm = he + 36;
    snprintf((char *)G(nm), 64, "%s", name);
    rt_st32(he + 0, nm);
    rt_st32(he + 4, aliases);
    rt_st32(he + 8, D_AF_INET);
    rt_st32(he + 12, 4);
    rt_st32(he + 16, list);
    rt_st32(aliases, 0);
    rt_st32(list, addr);
    rt_st32(list + 4, 0);
    rt_st32(addr, bswap32(h));
    RET(he);
}

/* getaddrinfo(node, service, hints, res). Darwin struct addrinfo (32 bytes): ai_flags 0, ai_family 4,
 * ai_socktype 8, ai_protocol 12, ai_addrlen 16, ai_canonname 20, ai_addr 24, ai_next 28. Each result is one
 * block: the addrinfo, then its sockaddr_in { sin_len, sin_family, sin_port, sin_addr, sin_zero[8] }. */
IMP(getaddrinfo) {
    const char *node = rt_str(A(0)), *service = rt_str(A(1));
    uint32_t hints = A(2), res = A(3);
    uint32_t flags = hints ? rt_ld32(hints) : 0, family = hints ? rt_ld32(hints + 4) : 0;
    uint32_t socktype = hints ? rt_ld32(hints + 8) : 0, protocol = hints ? rt_ld32(hints + 12) : 0;
    if (res) rt_st32(res, 0);
    if (!node && !service) { RET(D_EAI_NONAME); return; }
    if (family != D_AF_UNSPEC && family != D_AF_INET) { RET(D_EAI_FAMILY); return; }
    uint32_t h;
    if (!node) h = (flags & D_AI_PASSIVE) ? 0 : 0x7f000001u;
    else if ((flags & D_AI_NUMERICHOST) ? !parse_inet(node, &h) : !resolve_host(node, &h)) { RET(D_EAI_NONAME); return; }
    uint32_t port = 0;
    if (service && *service) {
        char *end;
        unsigned long p = strtoul(service, &end, 10);
        if (!*end && p < 65536) port = (uint32_t)p;
        else if (!strcmp(service, "http")) port = 80;
        else if (!strcmp(service, "https")) port = 443;
        else { RET(D_EAI_SERVICE); return; }
    }
    uint32_t types[2], ntypes = 0;
    if (socktype) types[ntypes++] = socktype;
    else { types[ntypes++] = D_SOCK_STREAM; types[ntypes++] = D_SOCK_DGRAM; }
    uint32_t first = 0, prev = 0;
    for (uint32_t k = 0; k < ntypes; k++) {
        uint32_t ai = rt_calloc(1, 32 + 16), sa = ai + 32;
        rt_st32(ai + 0, flags);
        rt_st32(ai + 4, D_AF_INET);
        rt_st32(ai + 8, types[k]);
        rt_st32(ai + 12, protocol ? protocol : types[k] == D_SOCK_STREAM ? 6 : types[k] == D_SOCK_DGRAM ? 17 : 0);
        rt_st32(ai + 16, 16);
        if ((flags & D_AI_CANONNAME) && node && !k) rt_st32(ai + 20, rt_strdup_guest(node));
        rt_st32(ai + 24, sa);
        rt_mem[sa] = 16;
        rt_mem[sa + 1] = D_AF_INET;
        rt_mem[sa + 2] = (uint8_t)(port >> 8);
        rt_mem[sa + 3] = (uint8_t)port;
        rt_st32(sa + 4, bswap32(h));
        if (prev) rt_st32(prev + 28, ai); else first = ai;
        prev = ai;
    }
    if (res) rt_st32(res, first);
    RET(0);
}

IMP(freeaddrinfo) {
    for (uint32_t ai = A(0); ai;) {
        uint32_t nx = rt_ld32(ai + 28);
        if (rt_ld32(ai + 20)) rt_free(rt_ld32(ai + 20));
        rt_free(ai);
        ai = nx;
    }
}

/* int socket(domain, type, protocol): no network stack here. EACCES is what a sandbox that forbids network
 * access reports; every caller in the game only checks for -1. */
IMP(socket) {
    static int once;
    log_once(&once, "socket() fails: networking is not available");
    fail(c, D_EACCES);
}
/* operations on a descriptor: none of them is a socket */
IMP(connect) { fail_fd(c, (int32_t)A(0)); }
IMP(bind) { fail_fd(c, (int32_t)A(0)); }
IMP(listen) { fail_fd(c, (int32_t)A(0)); }
IMP(accept) { fail_fd(c, (int32_t)A(0)); }
IMP(send) { fail_fd(c, (int32_t)A(0)); }
IMP(recv) { fail_fd(c, (int32_t)A(0)); }
IMP(sendto) { fail_fd(c, (int32_t)A(0)); }
IMP(recvfrom) { fail_fd(c, (int32_t)A(0)); }
IMP(setsockopt) { fail_fd(c, (int32_t)A(0)); }
IMP(getsockopt) { fail_fd(c, (int32_t)A(0)); }
IMP(getsockname) { fail_fd(c, (int32_t)A(0)); }
IMP(ioctl) { fail(c, (int32_t)A(0) < 0 ? D_EBADF : D_ENOTTY); }

/* int fcntl(fd, cmd, ...): the game uses F_RDADVISE on its data files (advisory: succeeds) and F_GETFL/F_SETFL
 * on sockets (which never exist) */
IMP(fcntl) {
    int32_t fd = (int32_t)A(0);
    uint32_t cmd = A(1);
    if (fd < 0) { fail(c, D_EBADF); return; }
    switch (cmd) {
    case 44: /* F_RDADVISE */
    case 45: /* F_RDAHEAD */
    case 48: /* F_NOCACHE */
    case 51: /* F_FULLFSYNC */
    case 1: case 2: case 4: /* F_GETFD, F_SETFD, F_SETFL */
        RET(0);
        return;
    case 3: RET(0); return; /* F_GETFL: O_RDONLY */
    default: fail(c, D_EINVAL); return;
    }
}

static void host_sleep_us(uint64_t us) {
#ifdef _WIN32
    Sleep((DWORD)((us + 999) / 1000));
#else
    struct timespec ts = {(time_t)(us / 1000000), (long)(us % 1000000) * 1000};
    nanosleep(&ts, NULL);
#endif
}

/* int select(nfds, readfds, writefds, exceptfds, struct timeval *): Darwin fd_set is 1024 bits in 32-bit words;
 * struct timeval { long tv_sec; int tv_usec; } is 8 bytes. Any descriptor asked about is not open (EBADF);
 * with none, select is the sleep it is used as. */
IMP(select) {
    int32_t nfds = (int32_t)A(0);
    uint32_t sets[3] = {A(1), A(2), A(3)}, tv = A(4);
    if (nfds < 0 || nfds > 1024) { fail(c, D_EINVAL); return; }
    for (int s = 0; s < 3; s++) {
        if (!sets[s]) continue;
        for (int32_t fd = 0; fd < nfds; fd++)
            if (rt_ld32(sets[s] + 4 * (uint32_t)(fd / 32)) >> (fd % 32) & 1) { fail(c, D_EBADF); return; }
    }
    if (!tv) { fail(c, D_EINVAL); return; } /* would block forever */
    int32_t sec = (int32_t)rt_ld32(tv), usec = (int32_t)rt_ld32(tv + 4);
    if (sec < 0 || usec < 0) { fail(c, D_EINVAL); return; }
    host_sleep_us((uint64_t)sec * 1000000u + (uint64_t)usec);
    RET(0);
}

/* ============================== NSError ============================================================== */
static id cls_error;
typedef struct { id domain, user_info; int32_t code; } error_state;
static error_state *es(id e) { return rt_objc_state(e, sizeof(error_state)); }

static id error_new(id domain, int32_t code, id user_info) {
    id e = rt_objc_alloc(cls_error);
    error_state *s = es(e);
    s->domain = rt_retain(domain);
    s->code = code;
    s->user_info = rt_retain(user_info);
    return e;
}

M(err_with) { (void)m; RET(rt_autorelease(error_new(ARG(0), (int32_t)ARG(1), ARG(2)))); }
M(err_init) {
    (void)m;
    error_state *s = es(SELF);
    set_retained(c, &s->domain, ARG(0));
    s->code = (int32_t)ARG(1);
    set_retained(c, &s->user_info, ARG(2));
    RET(SELF);
}
M(err_code) { (void)m; RET(es(SELF)->code); }
M(err_domain) { (void)m; RET(es(SELF)->domain); }
M(err_user_info) { (void)m; RET(es(SELF)->user_info); }
static id user_info_string(cpu_t *c, const char *key) {
    id ui = es(SELF)->user_info;
    if (!ui) return 0;
    id k = rt_nsstring_new(key);
    id v = rt_nsdict_get(ui, k);
    rt_release(c, k);
    return v;
}
M(err_localized) {
    (void)m;
    id v = user_info_string(c, "NSLocalizedDescription");
    if (v) { RET(v); return; }
    char b[256];
    const char *d = rt_nsstring_utf8(es(SELF)->domain);
    snprintf(b, sizeof b, "The operation couldn't be completed. (%s error %d.)", d ? d : "", es(SELF)->code);
    RET(rt_nsstring(b));
}
M(err_reason) { (void)m; RET(user_info_string(c, "NSLocalizedFailureReason")); }
M(err_description) {
    (void)m;
    char b[512];
    const char *d = rt_nsstring_utf8(es(SELF)->domain);
    id l = rt_send(c, SELF, "localizedDescription", 0);
    snprintf(b, sizeof b, "Error Domain=%s Code=%d \"%s\"", d ? d : "", es(SELF)->code,
             l ? rt_nsstring_utf8(l) : "");
    RET(rt_nsstring(b));
}
M(err_copy) { (void)m; RET(rt_retain(SELF)); }
M(err_dealloc) {
    (void)m;
    error_state *s = es(SELF);
    rt_release(c, s->domain);
    rt_release(c, s->user_info);
    rt_objc_free(SELF);
}

/* ============================== NSURL ================================================================ */
static id cls_url;
typedef struct { int is_file; char s[]; } url_state; /* the absolute string, inline */

static int url_char_ok(char ch) {
    return (unsigned char)ch > ' ' && (unsigned char)ch < 0x7f && !strchr("\"<>\\^`{|}", ch);
}

static id url_init(id u, const char *abs, int is_file) {
    url_state *s = rt_objc_state(u, sizeof(url_state) + strlen(abs) + 1);
    s->is_file = is_file;
    strcpy(s->s, abs);
    return u;
}
static url_state *us(id u) { url_state *s = rt_objc_state_peek(u); return s; }
static const char *url_abs(id u) { url_state *s = us(u); return s ? s->s : ""; }

/* "file://localhost" + the path, percent-encoding what a URL cannot hold */
static char *file_url_string(const char *path) {
    size_t n = strlen(path);
    char *o = malloc(n * 3 + 32), *p = o;
    p += sprintf(p, "file://localhost");
    if (path[0] != '/') *p++ = '/';
    for (const char *q = path; *q; q++) {
        if (url_char_ok(*q) && *q != '%' && *q != '#' && *q != '?') *p++ = *q;
        else p += sprintf(p, "%%%02X", (unsigned char)*q);
    }
    *p = 0;
    return o;
}

static int valid_url_string(const char *s) {
    if (!s) return 0;
    for (const char *p = s; *p; p++)
        if (!url_char_ok(*p)) return 0;
    return 1;
}

static id url_from_string(const char *s) {
    if (!valid_url_string(s)) return 0;
    return url_init(rt_objc_alloc(cls_url), s, !strncmp(s, "file:", 5));
}
/* a file URL in `u` (a fresh instance); is_dir: the path names a directory, so the URL ends with '/' */
static id url_init_file(id u, const char *path, int is_dir) {
    char *t = file_url_string(path);
    size_t n = strlen(t);
    if (is_dir && t[n - 1] != '/') {
        t = realloc(t, n + 2);
        t[n] = '/';
        t[n + 1] = 0;
    }
    url_init(u, t, 1);
    free(t);
    return u;
}
static id url_from_path(const char *path, int is_dir) {
    if (!path) return 0;
    return url_init_file(rt_objc_alloc(cls_url), path, is_dir);
}

static const char *utf8_or_null(id s) { return s ? rt_nsstring_utf8(s) : NULL; } /* nil stays NULL */
M(url_with_string) { (void)m; RET(rt_autorelease(url_from_string(utf8_or_null(ARG(0))))); }
M(url_with_path) { (void)m; RET(rt_autorelease(url_from_path(utf8_or_null(ARG(0)), 0))); }
M(url_with_path_dir) { (void)m; RET(rt_autorelease(url_from_path(utf8_or_null(ARG(0)), ARG(1) & 0xff))); }
M(url_init_string) {
    (void)m;
    const char *s = utf8_or_null(ARG(0));
    if (!valid_url_string(s)) { rt_objc_free(SELF); RET(0); return; }
    url_init(SELF, s, !strncmp(s, "file:", 5));
    RET(SELF);
}
static void url_init_path_common(cpu_t *c, int is_dir) {
    const char *p = utf8_or_null(ARG(0));
    if (!p) { rt_objc_free(SELF); RET(0); return; }
    url_init_file(SELF, p, is_dir);
    RET(SELF);
}
M(url_init_path) { (void)m; url_init_path_common(c, 0); }
M(url_init_path_dir) { (void)m; url_init_path_common(c, ARG(1) & 0xff); }
/* URLWithString:relativeToURL: resolves only the simple cases (absolute strings, absolute paths) */
M(url_with_string_rel) {
    (void)m;
    const char *s = utf8_or_null(ARG(0)), *base = ARG(1) ? url_abs(ARG(1)) : NULL;
    if (!s || strstr(s, "://") || !base) { RET(rt_autorelease(url_from_string(s))); return; }
    const char *h = strstr(base, "://");
    const char *root = h ? strchr(h + 3, '/') : NULL; /* the first '/' of the base's path */
    size_t keep;
    int slash = 0;
    if (s[0] == '/') keep = root ? (size_t)(root - base) : strlen(base);
    else if (h && !root) { keep = strlen(base); slash = 1; } /* "http://host" + "a" = "http://host/a" */
    else keep = (size_t)((strrchr(base, '/') ? strrchr(base, '/') + 1 : base) - base);
    char *t = malloc(keep + strlen(s) + 2);
    memcpy(t, base, keep);
    if (slash) t[keep++] = '/';
    strcpy(t + keep, s);
    RET(rt_autorelease(url_from_string(t)));
    free(t);
}

static void percent_decode(char *s) {
    char *o = s;
    for (char *p = s; *p; p++) {
        if (p[0] == '%' && p[1] && p[2]) {
            char h[3] = {p[1], p[2], 0};
            char *end;
            long v = strtol(h, &end, 16);
            if (!*end) { *o++ = (char)v; p += 2; continue; }
        }
        *o++ = *p;
    }
    *o = 0;
}

/* split scheme://host[:port]/path?query#fragment; returns pointers into one buffer: a copy of the string
 * (cut at ':', '?', '#'), followed by a separate copy of the host (so the path keeps its leading '/') */
typedef struct { char *buf; const char *scheme, *host, *path, *query; } url_parts;
static url_parts url_split(const char *abs) {
    size_t n = strlen(abs);
    url_parts r = {malloc(2 * n + 2), "", "", "", NULL};
    memcpy(r.buf, abs, n + 1);
    char *hostcopy = r.buf + n + 1;
    char *s = r.buf, *colon = strchr(s, ':');
    if (colon && colon > s && !memchr(s, '/', (size_t)(colon - s))) { *colon = 0; r.scheme = s; s = colon + 1; }
    char *hash = strchr(s, '#');
    if (hash) *hash = 0;
    char *q = strchr(s, '?');
    if (q) { *q = 0; r.query = q + 1; }
    if (s[0] == '/' && s[1] == '/') {
        s += 2;
        char *slash = strchr(s, '/');
        size_t hl = slash ? (size_t)(slash - s) : strlen(s);
        memcpy(hostcopy, s, hl);
        hostcopy[hl] = 0;
        char *pc = strchr(hostcopy, ':');
        if (pc) *pc = 0;
        r.host = hostcopy;
        r.path = slash ? slash : "";
    } else {
        r.path = s;
    }
    return r;
}

M(url_absolute) { (void)m; RET(rt_nsstring(url_abs(SELF))); }
M(url_path) {
    (void)m;
    url_parts p = url_split(url_abs(SELF));
    char *t = strdup(p.path);
    percent_decode(t);
    size_t n = strlen(t);
    if (n > 1 && t[n - 1] == '/') t[n - 1] = 0; /* -path drops a trailing slash */
    RET(rt_nsstring(t));
    free(t);
    free(p.buf);
}
M(url_scheme) { (void)m; url_parts p = url_split(url_abs(SELF)); RET(*p.scheme ? rt_nsstring(p.scheme) : 0); free(p.buf); }
M(url_host) {
    (void)m;
    url_parts p = url_split(url_abs(SELF));
    RET(*p.host ? rt_nsstring(p.host) : 0);
    free(p.buf);
}
M(url_query) {
    (void)m;
    url_parts p = url_split(url_abs(SELF));
    RET(p.query ? rt_nsstring(p.query) : 0);
    free(p.buf);
}
M(url_is_file) { (void)m; url_state *s = us(SELF); RET(s && s->is_file); }
M(url_is_equal) {
    (void)m;
    id o = ARG(0);
    RET(o && rt_is_kind(o, cls_url) && !strcmp(url_abs(SELF), url_abs(o)));
}
M(url_hash) {
    (void)m;
    uint32_t h = 0;
    for (const char *p = url_abs(SELF); *p; p++) h = h * 31 + (uint8_t)*p;
    RET(h);
}
M(url_copy) { (void)m; RET(rt_retain(SELF)); }

/* ============================== NSURLRequest ========================================================= */
static id cls_request, cls_mrequest;
typedef struct { id url, method, body; double timeout; uint32_t policy; } request_state;
static request_state *rs(id r) { return rt_objc_state(r, sizeof(request_state)); }

static id request_init(cpu_t *c, id r, id url, uint32_t policy, double timeout) {
    request_state *s = rs(r);
    set_retained(c, &s->url, url);
    s->policy = policy;
    s->timeout = timeout;
    return r;
}
/* the receiver class: +requestWithURL: on NSMutableURLRequest makes a mutable request */
M(req_with_url) { (void)m; RET(rt_autorelease(request_init(c, rt_objc_alloc(SELF), ARG(0), 0, 60.0))); }
/* requestWithURL:(NSURL *)cachePolicy:(NSURLRequestCachePolicy)timeoutInterval:(NSTimeInterval, 2 words) */
M(req_with_url_full) {
    (void)m;
    RET(rt_autorelease(request_init(c, rt_objc_alloc(SELF), ARG(0), ARG(1), rt_darg(c, 2 + 2))));
}
M(req_init_url) { (void)m; RET(request_init(c, SELF, ARG(0), 0, 60.0)); }
M(req_init_url_full) { (void)m; RET(request_init(c, SELF, ARG(0), ARG(1), rt_darg(c, 2 + 2))); }
M(req_url) { (void)m; RET(rs(SELF)->url); }
M(req_method) { (void)m; id v = rs(SELF)->method; RET(v ? v : rt_nsstring("GET")); }
M(req_body) { (void)m; RET(rs(SELF)->body); }
M(req_timeout) { (void)m; rt_dret(c, rs(SELF)->timeout); }
M(req_policy) { (void)m; RET(rs(SELF)->policy); }
M(req_set_url) { (void)m; set_retained(c, &rs(SELF)->url, ARG(0)); }
M(req_set_method) { (void)m; set_retained(c, &rs(SELF)->method, ARG(0)); }
M(req_set_body) { (void)m; set_retained(c, &rs(SELF)->body, ARG(0)); }
M(req_set_timeout) { (void)m; rs(SELF)->timeout = rt_darg(c, 2); }
M(req_set_policy) { (void)m; rs(SELF)->policy = ARG(0); }
M(req_copy) { /* an immutable copy */
    (void)m;
    request_state *s = rs(SELF);
    id r = request_init(c, rt_objc_alloc(cls_request), s->url, s->policy, s->timeout);
    set_retained(c, &rs(r)->method, s->method);
    set_retained(c, &rs(r)->body, s->body);
    RET(r);
}
M(req_mutable_copy) {
    (void)m;
    request_state *s = rs(SELF);
    id r = request_init(c, rt_objc_alloc(cls_mrequest), s->url, s->policy, s->timeout);
    set_retained(c, &rs(r)->method, s->method);
    set_retained(c, &rs(r)->body, s->body);
    RET(r);
}
M(req_dealloc) {
    (void)m;
    request_state *s = rs(SELF);
    rt_release(c, s->url);
    rt_release(c, s->method);
    rt_release(c, s->body);
    rt_objc_free(SELF);
}

/* ============================== NSURLConnection ====================================================== */
static id cls_connection;
typedef struct { id request, delegate; int scheduled, done; } conn_state;
static conn_state *cs(id x) { return rt_objc_state(x, sizeof(conn_state)); }

/* the error an offline request ends with: NSURLErrorNotConnectedToInternet (+1) */
static id offline_error(cpu_t *c, id request) {
    id url = request ? rs(request)->url : 0;
    id url_string = url ? rt_nsstring_new(url_abs(url)) : 0;
    id desc = rt_nsstring_new("The Internet connection appears to be offline.");
    const char *keys[] = {"NSLocalizedDescription", "NSErrorFailingURLKey"};
    id vals[] = {desc, url};
    id ui = dict_of(c, 2, keys, vals);
    if (url_string) rt_nsdict_set(ui, const_string(K_FAILING_URL_STRING), url_string);
    id domain = rt_nsstring_new("NSURLErrorDomain");
    id e = error_new(domain, -1009, ui);
    rt_release(c, domain);
    rt_release(c, ui);
    rt_release(c, desc);
    rt_release(c, url_string);
    return e;
}

static void conn_start(id x) {
    conn_state *s = cs(x);
    if (s->scheduled || s->done) return;
    s->scheduled = 1;
    rt_runloop_perform_later(x, rt_sel("_rtOfflineFail"), 0, 0.0);
}

static id conn_init(cpu_t *c, id x, id request, id delegate, int start) {
    conn_state *s = cs(x);
    set_retained(c, &s->request, request);
    s->delegate = rt_retain(delegate); /* NSURLConnection retains its delegate until it finishes */
    rt_log("offline: NSURLConnection to %s fails (no network)", request && rs(request)->url ? url_abs(rs(request)->url) : "?");
    if (start) conn_start(x);
    return x;
}

M(conn_init_rd) { (void)m; RET(conn_init(c, SELF, ARG(0), ARG(1), 1)); }
M(conn_init_rds) { (void)m; RET(conn_init(c, SELF, ARG(0), ARG(1), ARG(2) & 0xff)); }
M(conn_with) { (void)m; RET(rt_autorelease(conn_init(c, rt_objc_alloc(cls_connection), ARG(0), ARG(1), 1))); }
M(conn_start_m) { (void)m; conn_start(SELF); }
M(conn_cancel) {
    (void)m;
    conn_state *s = cs(SELF);
    if (s->done) return;
    s->done = 1;
    id d = s->delegate;
    s->delegate = 0;
    rt_release(c, d);
}
/* the run loop callback: tell the delegate, then let it go */
M(conn_fail) {
    (void)m;
    conn_state *s = cs(SELF);
    if (s->done) return;
    s->done = 1;
    id d = s->delegate;
    s->delegate = 0;
    if (d && rt_responds(d, "connection:didFailWithError:")) {
        id e = offline_error(c, s->request);
        rt_send(c, d, "connection:didFailWithError:", 2, SELF, e);
        rt_release(c, e);
    }
    rt_release(c, d);
}
/* +sendSynchronousRequest:returningResponse:(NSURLResponse **)error:(NSError **) */
M(conn_send_sync) {
    (void)m;
    if (ARG(1)) rt_st32(ARG(1), 0);
    if (ARG(2)) rt_st32(ARG(2), rt_autorelease(offline_error(c, ARG(0))));
    RET(0);
}
M(conn_dealloc) {
    (void)m;
    conn_state *s = cs(SELF);
    rt_release(c, s->request);
    rt_release(c, s->delegate);
    rt_objc_free(SELF);
}

/* ============================== GameKit: GKSession =================================================== */
static id cls_session;
typedef struct { id delegate, session_id, display_name; uint32_t mode, available, fail_scheduled; } session_state;
static session_state *ss(id x) { return rt_objc_state(x, sizeof(session_state)); }

/* initWithSessionID:displayName:sessionMode: (nil ID: the bundle's; nil name: the device's) */
M(gk_init) {
    (void)m;
    session_state *s = ss(SELF);
    set_retained(c, &s->session_id, ARG(0));
    set_retained(c, &s->display_name, ARG(1));
    s->mode = ARG(2);
    rt_log("offline: GKSession created (no peers will ever be found)");
    RET(SELF);
}
M(gk_set_delegate) { (void)m; ss(SELF)->delegate = ARG(0); } /* not retained */
M(gk_delegate) { (void)m; RET(ss(SELF)->delegate); }
/* setAvailable:YES with Bluetooth off: on the next run loop turn the delegate gets session:didFailWithError:
 * with GKSessionCannotEnableError (30509), and the session stays unavailable. -[BluetoothController
 * session:didFailWithError:] checks exactly that code and clears its "Bluetooth enabled" flag, which is the
 * game's offline path for local multiplayer. */
enum { GK_CANNOT_ENABLE = 30509 };
M(gk_set_available) {
    (void)m;
    session_state *s = ss(SELF);
    s->available = ARG(0) & 0xff;
    if (s->available && !s->fail_scheduled) {
        s->fail_scheduled = 1;
        rt_runloop_perform_later(SELF, rt_sel("_rtSessionFail"), 0, 0.0);
    }
}
M(gk_fail) {
    (void)m;
    session_state *s = ss(SELF);
    s->fail_scheduled = 0;
    if (!s->available) return; /* made unavailable again meanwhile */
    s->available = 0;
    id d = s->delegate; /* the game clears it (setDelegate: nil) before releasing the session */
    if (!d || !rt_responds(d, "session:didFailWithError:")) return;
    id desc = rt_nsstring_new("Bluetooth is not available.");
    const char *keys[] = {"NSLocalizedDescription"};
    id ui = dict_of(c, 1, keys, &desc);
    id domain = rt_nsstring_new("com.apple.gamekit.GKSessionErrorDomain"); /* GKSessionErrorDomain */
    id e = error_new(domain, GK_CANNOT_ENABLE, ui);
    rt_release(c, domain);
    rt_release(c, ui);
    rt_release(c, desc);
    rt_log("offline: GKSession cannot be enabled (no Bluetooth)");
    rt_send(c, d, "session:didFailWithError:", 2, SELF, e);
    rt_release(c, e);
}
M(gk_available) { (void)m; RET(ss(SELF)->available); }
M(gk_session_id) { (void)m; id v = ss(SELF)->session_id; RET(v ? v : rt_nsstring("com.gameloft.novahd")); }
M(gk_display_name) { (void)m; id v = ss(SELF)->display_name; RET(v ? v : rt_nsstring("iPad")); }
M(gk_mode) { (void)m; RET(ss(SELF)->mode); }
M(gk_peer_id) { (void)m; RET(rt_nsstring("1")); }
M(gk_timeout) { (void)m; rt_dret(c, 20.0); }
/* sendData:toPeers:withDataMode:error: and sendDataToAllPeers:withDataMode:error:: NO, nobody to send to */
M(gk_send_peers) { (void)m; if (ARG(3)) rt_st32(ARG(3), 0); RET(0); }
M(gk_send_all) { (void)m; if (ARG(2)) rt_st32(ARG(2), 0); RET(0); }
M(gk_accept) { (void)m; if (ARG(1)) rt_st32(ARG(1), 0); RET(0); } /* acceptConnectionFromPeer:error: */
M(gk_dealloc) {
    (void)m;
    session_state *s = ss(SELF);
    rt_release(c, s->session_id);
    rt_release(c, s->display_name);
    rt_objc_free(SELF);
}

/* ============================== Bonjour: NSNetService, NSNetServiceBrowser =========================== */
static id cls_netservice, cls_browser;
typedef struct { id delegate, domain, type, name; int32_t port; } netservice_state;
static netservice_state *ns(id x) { return rt_objc_state(x, sizeof(netservice_state)); }

static void ns_set(cpu_t *c, id x, id domain, id type, id name, int32_t port) {
    netservice_state *s = ns(x);
    set_retained(c, &s->domain, domain);
    set_retained(c, &s->type, type);
    set_retained(c, &s->name, name);
    s->port = port;
}
M(ns_init4) { (void)m; ns_set(c, SELF, ARG(0), ARG(1), ARG(2), (int32_t)ARG(3)); RET(SELF); } /* initWithDomain:type:name:port: */
M(ns_init3) { (void)m; ns_set(c, SELF, ARG(0), ARG(1), ARG(2), -1); RET(SELF); }             /* initWithDomain:type:name: */
M(ns_set_delegate) { (void)m; ns(SELF)->delegate = ARG(0); } /* not retained */
M(ns_delegate) { (void)m; RET(ns(SELF)->delegate); }
M(ns_domain) { (void)m; RET(ns(SELF)->domain); }
M(ns_type) { (void)m; RET(ns(SELF)->type); }
M(ns_name) { (void)m; RET(ns(SELF)->name); }
M(ns_port) { (void)m; RET(ns(SELF)->port); }
/* -publish: accepted; nobody will ever see the service, so there is no further callback.
 * -resolve / -resolveWithTimeout: are only sent for services a browser found, which never happens. */
M(ns_publish) { (void)m; (void)c; rt_log("offline: NSNetService publish (no client will ever connect)"); }
M(ns_dealloc) {
    (void)m;
    netservice_state *s = ns(SELF);
    rt_release(c, s->domain);
    rt_release(c, s->type);
    rt_release(c, s->name);
    rt_objc_free(SELF);
}

typedef struct { id delegate; int searching; } browser_state;
static browser_state *bs(id x) { return rt_objc_state(x, sizeof(browser_state)); }
M(br_set_delegate) { (void)m; bs(SELF)->delegate = ARG(0); }
M(br_delegate) { (void)m; RET(bs(SELF)->delegate); }
M(br_search) { (void)m; bs(SELF)->searching = 1; } /* searchForServicesOfType:inDomain: finds nothing */
M(br_stop) {
    (void)m;
    browser_state *s = bs(SELF);
    if (!s->searching) return;
    s->searching = 0;
    if (s->delegate && rt_responds(s->delegate, "netServiceBrowserDidStopSearch:"))
        rt_send(c, s->delegate, "netServiceBrowserDidStopSearch:", 1, SELF);
}

/* ============================== MediaPlayer: movies ================================================== */
static id cls_movie, cls_movie_vc;
enum { MP_STOPPED = 0, MP_PLAYING = 1, MP_PAUSED = 2 };
typedef struct { id url, view; uint32_t state, fullscreen, scheduled, finished; } movie_state;
static movie_state *mvs(id x) { return rt_objc_state(x, sizeof(movie_state)); }

static void movie_notify(cpu_t *c, id player, const char *name, id user_info) {
    rt_notify(c, name, player, user_info);
}

static void movie_set_state(cpu_t *c, id player, uint32_t state) {
    movie_state *s = mvs(player);
    if (s->state == state) return;
    s->state = state;
    movie_notify(c, player, "MPMoviePlayerPlaybackStateDidChangeNotification", 0);
}

M(mp_init_url) {
    (void)m;
    movie_state *s = mvs(SELF);
    set_retained(c, &s->url, ARG(0));
    rt_log("offline: movie %s is not played (finishes after %.1f s)", ARG(0) ? url_abs(ARG(0)) : "(nil)", MOVIE_SECONDS);
    RET(SELF);
}
M(mp_play) {
    (void)m;
    movie_state *s = mvs(SELF);
    s->finished = 0;
    movie_set_state(c, SELF, MP_PLAYING);
    if (!s->scheduled) {
        s->scheduled = 1;
        rt_runloop_perform_later(SELF, rt_sel("_rtMovieEnded"), 0, MOVIE_SECONDS);
    }
}
M(mp_pause) { (void)m; if (mvs(SELF)->state == MP_PLAYING) movie_set_state(c, SELF, MP_PAUSED); }
M(mp_stop) {
    (void)m;
    mvs(SELF)->finished = 1;
    movie_set_state(c, SELF, MP_STOPPED);
}
/* the end of the movie: stopped, then PlaybackDidFinish with MPMovieFinishReasonPlaybackEnded */
M(mp_ended) {
    (void)m;
    movie_state *s = mvs(SELF);
    s->scheduled = 0;
    if (s->finished || s->state != MP_PLAYING) return; /* stopped meanwhile, or paused: plays on when resumed */
    s->finished = 1;
    movie_set_state(c, SELF, MP_STOPPED);
    id reason = rt_nsnumber_new_int(0);
    const char *keys[] = {"MPMoviePlayerPlaybackDidFinishReasonUserInfoKey"};
    id ui = dict_of(c, 1, keys, &reason);
    movie_notify(c, SELF, "MPMoviePlayerPlaybackDidFinishNotification", ui);
    rt_release(c, ui);
    rt_release(c, reason);
    if (s->fullscreen) {
        movie_notify(c, SELF, "MPMoviePlayerWillExitFullscreenNotification", 0);
        s->fullscreen = 0;
        movie_notify(c, SELF, "MPMoviePlayerDidExitFullscreenNotification", 0);
    }
}
M(mp_state) { (void)m; RET(mvs(SELF)->state); }
M(mp_set_fullscreen) { (void)m; mvs(SELF)->fullscreen = ARG(0) & 0xff; } /* setFullscreen: and setFullscreen:animated: */
M(mp_fullscreen) { (void)m; RET(mvs(SELF)->fullscreen); }
M(mp_url) { (void)m; RET(mvs(SELF)->url); }
M(mp_set_url) { (void)m; set_retained(c, &mvs(SELF)->url, ARG(0)); }
M(mp_view) { /* 3.2: the player's own view, an empty UIView */
    (void)m;
    movie_state *s = mvs(SELF);
    if (!s->view)
        s->view = rt_send(c, rt_objc_alloc(rt_class("UIView")), "initWithFrame:", 4, 0, 0, 0x44800000u /*1024*/,
                          0x44400000u /*768*/);
    RET(s->view);
}
M(mp_dealloc) {
    (void)m;
    movie_state *s = mvs(SELF);
    rt_release(c, s->url);
    rt_release(c, s->view);
    rt_objc_free(SELF);
}

/* MPMoviePlayerViewController (a UIViewController): a[0] = its MPMoviePlayerController (retained) */
static id movie_vc_player(id vc) { return ext(vc)->a[0]; }
/* -[super init] for a UIViewController subclass (UIKit's init, then NSObject's); self if there is none */
static id super_init(cpu_t *c, id cls) {
    SEL s = rt_sel("init");
    uint32_t imp = rt_lookup(super_of(cls), s);
    return imp ? rt_call(c, imp, 2, SELF, s) : SELF;
}
M(mpvc_init_url) {
    (void)m;
    id url = ARG(0);
    id self = super_init(c, cls_movie_vc);
    if (self) ext(self)->a[0] = rt_send(c, rt_objc_alloc(cls_movie), "initWithContentURL:", 1, url);
    RET(self);
}
M(mpvc_player) { (void)m; RET(movie_vc_player(SELF)); }
M(mpvc_dealloc) {
    (void)m;
    ext_drop(c, SELF, 1);
    call_super(c, cls_movie_vc, "dealloc", 0);
}
/* the MediaPlayer category on UIViewController: presenting the controller starts the movie (shouldAutoplay) */
M(vc_present_movie) {
    (void)m;
    id vc = ARG(0);
    id p = vc ? rt_send(c, vc, "moviePlayer", 0) : 0;
    if (p) rt_send(c, p, "play", 0);
}

/* ============================== MediaPlayer: music and the library =================================== */
static id cls_music, ipod_player, app_player;
M(mu_ipod) { (void)m; RET(ipod_player); }
M(mu_app) { (void)m; RET(app_player); }
M(mu_volume) { (void)m; rt_fret(c, 1.0f); }

/* MPMediaQuery: optional Android catalog. */
static id cls_query;
typedef struct { id predicates; uint32_t grouping; } query_state;
static query_state *qs(id x) { return rt_objc_state(x, sizeof(query_state)); }
static id query_new(uint32_t grouping) { id q = rt_objc_alloc(cls_query); qs(q)->grouping = grouping; return q; }
/* grouping types: title 0, album 1, artist 2, album artist 3, composer 4, genre 5, playlist 6, podcast 7 */
M(q_songs) { (void)m; RET(rt_autorelease(query_new(0))); }
M(q_albums) { (void)m; RET(rt_autorelease(query_new(1))); }
M(q_artists) { (void)m; RET(rt_autorelease(query_new(2))); }
M(q_composers) { (void)m; RET(rt_autorelease(query_new(4))); }
M(q_genres) { (void)m; RET(rt_autorelease(query_new(5))); }
M(q_playlists) { (void)m; RET(rt_autorelease(query_new(6))); }
M(q_podcasts) { (void)m; RET(rt_autorelease(query_new(7))); }
M(q_audiobooks) { (void)m; RET(rt_autorelease(query_new(0))); }
M(q_compilations) { (void)m; RET(rt_autorelease(query_new(1))); }
M(q_init_preds) { (void)m; set_retained(c, &qs(SELF)->predicates, ARG(0)); RET(SELF); }
M(q_grouping) { (void)m; RET(qs(SELF)->grouping); }
M(q_set_grouping) { (void)m; qs(SELF)->grouping = ARG(0); }
M(q_predicates) { (void)m; RET(qs(SELF)->predicates); }
M(q_dealloc) { (void)m; rt_release(c, qs(SELF)->predicates); rt_objc_free(SELF); }

/* MPMediaItemCollection */
static id cls_collection, cls_playlist, cls_media_item;
typedef struct { id title, artist, album, track_id; } media_item_state;
#ifdef __ANDROID__
static id music_items;
static pthread_mutex_t music_lock = PTHREAD_MUTEX_INITIALIZER;

/* Stable objects survive switches among Songs, Artists and Playlists. */
static id catalog_item(cpu_t *c, const char *fields[TRACK_FIELDS]) {
    pthread_mutex_lock(&music_lock);
    if (!music_items) music_items = rt_nsdict_new();
    id key = rt_nsstring_new(fields[TRACK_ID]);
    id item = rt_nsdict_get(music_items, key);
    if (item) rt_retain(item);
    else {
        item = rt_objc_alloc(cls_media_item);
        media_item_state *state = rt_objc_state(item, sizeof(media_item_state));
        state->title = rt_nsstring_new(fields[TRACK_TITLE]);
        state->artist = rt_nsstring_new(fields[TRACK_ARTIST]);
        state->album = rt_nsstring_new(fields[TRACK_ALBUM]);
        state->track_id = rt_retain(key);
        if (rt_nsdict_count(music_items) < 2048) rt_nsdict_set(music_items, key, item);
    }
    rt_release(c, key);
    pthread_mutex_unlock(&music_lock);
    return item;
}
#endif

typedef struct { id items, name; } coll_state;
static coll_state *colls(id x) { return rt_objc_state(x, sizeof(coll_state)); }
M(co_with_items) {
    (void)m;
    id x = rt_objc_alloc(cls_collection);
    colls(x)->items = rt_retain(ARG(0));
    RET(rt_autorelease(x));
}
M(co_init_items) { (void)m; set_retained(c, &colls(SELF)->items, ARG(0)); RET(SELF); }
M(co_items) { (void)m; RET(colls(SELF)->items); }
M(co_count) { (void)m; RET(rt_nsarray_count(colls(SELF)->items)); }
M(co_representative) { (void)m; id it = colls(SELF)->items; RET(rt_nsarray_count(it) ? rt_nsarray_at(it, 0) : 0); }
M(co_dealloc) {
    (void)m;
    coll_state *state = colls(SELF);
    rt_release(c, state->items);
    rt_release(c, state->name);
    rt_objc_free(SELF);
}
M(co_name) { (void)m; RET(colls(SELF)->name); }

static id media_property(id obj, const char *name) {
    media_item_state *state = rt_objc_state(obj, sizeof(media_item_state));
    if (!strcmp(name, "title")) return state->title;
    if (!strcmp(name, "artist")) return state->artist;
    if (!strcmp(name, "albumTitle")) return state->album;
    if (!strcmp(name, "mediaType")) return rt_autorelease(rt_nsnumber_new_int(1));
    return 0;
}

M(mi_title) { (void)m; RET(media_property(SELF, "title")); }
M(mi_artist) { (void)m; RET(media_property(SELF, "artist")); }
M(mi_album) { (void)m; RET(media_property(SELF, "albumTitle")); }
M(mi_property) { (void)m; RET(media_property(SELF, rt_nsstring_utf8(ARG(0)))); }
M(mi_dealloc) {
    (void)m;
    media_item_state *state = rt_objc_state(SELF, sizeof(media_item_state));
    rt_release(c, state->title);
    rt_release(c, state->artist);
    rt_release(c, state->album);
    rt_release(c, state->track_id);
    rt_objc_free(SELF);
}

M(co_property) {
    (void)m;
    const char *property = rt_nsstring_utf8(ARG(0));
    id items = colls(SELF)->items;
    if (!strcmp(property, "name")) RET(colls(SELF)->name);
    else {
        id representative = rt_nsarray_at(items, 0);
        RET(representative && rt_is_kind(representative, cls_media_item) ? media_property(representative, property) : 0);
    }
}

static id query_catalog(cpu_t *c, const query_state *query, int collections) {
    id result = rt_nsarray_new(NULL, 0);
#ifdef __ANDROID__
    size_t length;
    char *wire = rt_platform_music_catalog(query->grouping, &length);
    if (!wire) return rt_autorelease(result);
    id groups = collections ? rt_nsdict_new() : 0;
    id seen = !collections && query->grouping == 6 ? rt_nsdict_new() : 0;
    const char *cursor = wire, *end = wire + length, *fields[TRACK_FIELDS];
    while (music_catalog_row(&cursor, end, fields)) {
        if (!fields[TRACK_ID][0]) continue;
        if (seen) {
            id key = rt_nsstring_new(fields[TRACK_ID]);
            int duplicate = rt_nsdict_get(seen, key) != 0;
            if (!duplicate) rt_nsdict_set(seen, key, key);
            rt_release(c, key);
            if (duplicate) continue;
        }
        id item = catalog_item(c, fields);
        id items = result;
        if (groups) {
            id key = rt_nsstring_new(fields[TRACK_GROUP]);
            id group = rt_nsdict_get(groups, key);
            if (!group) {
                group = rt_objc_alloc(query->grouping == 6 ? cls_playlist : cls_collection);
                colls(group)->items = rt_nsarray_new(NULL, 0);
                colls(group)->name = rt_retain(key);
                rt_nsdict_set(groups, key, group);
                rt_nsarray_add(result, group);
                rt_release(c, group);
            }
            items = colls(group)->items;
            rt_release(c, key);
        }
        rt_nsarray_add(items, item);
        rt_release(c, item);
    }
    rt_release(c, groups);
    rt_release(c, seen);
    free(wire);
    rt_log("music: grouping %u %s -> %u", query->grouping, collections ? "collections" : "items",
           rt_nsarray_count(result));
#else
    (void)c; (void)query; (void)collections;
#endif
    return rt_autorelease(result);
}

M(q_items) { (void)m; RET(query_catalog(c, qs(SELF), 0)); }
M(q_collections) { (void)m; RET(query_catalog(c, qs(SELF), 1)); }

#ifdef __ANDROID__
static id music_queue, music_selected;
static char music_snapshot[40] = "0:", music_reported_id[38];
static unsigned music_observers;
static int music_selection_pending, music_requested;
static int music_slot(id player) { return player == ipod_player ? 1 : 2; }

static const char *music_item_id(id item) {
    if (!item || !rt_is_kind(item, cls_media_item)) return "";
    media_item_state *state = rt_objc_state(item, sizeof(media_item_state));
    return rt_nsstring_utf8(state->track_id);
}

M(mu_queue_collection) {
    (void)m;
    id collection = ARG(0);
    id items = collection && rt_is_kind(collection, cls_collection) ? colls(collection)->items : 0;
    pthread_mutex_lock(&music_lock);
    set_retained(c, &music_queue, items);
    set_retained(c, &music_selected, 0);
    music_selection_pending = 1;
    pthread_mutex_unlock(&music_lock);
}

M(mu_queue_query) {
    (void)m;
    id query = ARG(0);
    id items = query && rt_is_kind(query, cls_query) ? query_catalog(c, qs(query), 0) : 0;
    pthread_mutex_lock(&music_lock);
    set_retained(c, &music_queue, items);
    set_retained(c, &music_selected, 0);
    music_selection_pending = 1;
    pthread_mutex_unlock(&music_lock);
}

static void music_start(const char *preferred) {
    enum { MAX_QUEUE = 1024 };
    const char *ids[MAX_QUEUE];
    size_t count = 0, total = rt_nsarray_count(music_queue);
    if (preferred[0]) ids[count++] = preferred;
    else if (total) {
        const char *first = music_item_id(rt_nsarray_at(music_queue, 0));
        if (*first) ids[count++] = first;
    }
    for (size_t k = 0; k < total && count < MAX_QUEUE; ++k) {
        const char *id = music_item_id(rt_nsarray_at(music_queue, k));
        if (*id) ids[count++] = id;
    }
    if (count) {
        music_requested = 1;
        rt_platform_music_command(RT_MUSIC_PLAY, ids, count);
    }
}

M(mu_set_item) {
    (void)m;
    id item = ARG(0);
    if (!*music_item_id(item)) return;
    pthread_mutex_lock(&music_lock);
    set_retained(c, &music_selected, item);
    music_selection_pending = 1;
    music_requested = 1;
    const char *selected = music_item_id(item);
    rt_platform_music_command(RT_MUSIC_SELECT, &selected, 1);
    if (music_snapshot[0] == '1') music_start(selected);
    pthread_mutex_unlock(&music_lock);
}
M(mu_play) {
    (void)m;
    pthread_mutex_lock(&music_lock);
    const char *selected = music_item_id(music_selected);
    const char *current = music_snapshot + 2;
    music_start(music_selection_pending || !*current ? selected : current);
    pthread_mutex_unlock(&music_lock);
}
M(mu_pause) { (void)m; (void)c; rt_platform_music_command(RT_MUSIC_PAUSE, NULL, 0); }
M(mu_stop) {
    (void)m;
    pthread_mutex_lock(&music_lock);
    set_retained(c, &music_selected, 0);
    music_selection_pending = 0;
    rt_platform_music_command(RT_MUSIC_STOP, NULL, 0);
    pthread_mutex_unlock(&music_lock);
}
M(mu_next) { (void)m; (void)c; rt_platform_music_command(RT_MUSIC_NEXT, NULL, 0); }
M(mu_prev) { (void)m; (void)c; rt_platform_music_command(RT_MUSIC_PREVIOUS, NULL, 0); }
M(mu_state) {
    (void)m;
    pthread_mutex_lock(&music_lock);
    RET(music_snapshot[0] - '0');
    pthread_mutex_unlock(&music_lock);
}

M(mu_current) {
    (void)m;
    pthread_mutex_lock(&music_lock);
    const char *current = music_snapshot + 2;
    id result = music_selection_pending ? music_selected : 0;
    if (!result && *current) {
        for (uint32_t k = 0; k < rt_nsarray_count(music_queue); ++k) {
            id item = rt_nsarray_at(music_queue, k);
            if (!strcmp(current, music_item_id(item))) { result = item; break; }
        }
        if (!result && music_selected && !strcmp(current, music_item_id(music_selected))) result = music_selected;
        if (!result && music_items) {
            id key = rt_nsstring_new(current);
            result = rt_nsdict_get(music_items, key);
            rt_release(c, key);
        }
    }
    RET(rt_autorelease(rt_retain(result)));
    pthread_mutex_unlock(&music_lock);
}

M(mu_observe) {
    (void)m;
    pthread_mutex_lock(&music_lock);
    music_observers |= (unsigned)music_slot(SELF);
    pthread_mutex_unlock(&music_lock);
}
M(mu_unobserve) {
    (void)m;
    pthread_mutex_lock(&music_lock);
    music_observers &= ~(unsigned)music_slot(SELF);
    pthread_mutex_unlock(&music_lock);
}

void rt_offline_music_tick(cpu_t *c) {
    static double next;
    pthread_mutex_lock(&music_lock);
    int requested = music_requested || music_observers;
    pthread_mutex_unlock(&music_lock);
    if (!requested) return;
    double now = rt_now();
    if (now < next) return;
    next = now + 0.2;
    char state[40];
    rt_platform_music_state(state);
    pthread_mutex_lock(&music_lock);
    int playback_changed = music_snapshot[0] != state[0];
    memcpy(music_snapshot, state, strlen(state) + 1);
    /* Keep the synchronous selection until Android confirms its ID. Later
     * automatic/explicit advances must use the backend's current item. */
    if (music_selected && !strcmp(state + 2, music_item_id(music_selected)))
        music_selection_pending = 0;
    const char *visible = music_selection_pending && music_selected ? music_item_id(music_selected) : music_snapshot + 2;
    int item_changed = strcmp(music_reported_id, visible) != 0;
    snprintf(music_reported_id, sizeof music_reported_id, "%s", visible);
    unsigned observers = music_observers;
    pthread_mutex_unlock(&music_lock);
    for (int bit = 1; bit <= 2; bit *= 2) {
        if (!(observers & bit)) continue;
        id player = bit == 1 ? ipod_player : app_player;
        if (item_changed) rt_notify(c, "MPMusicPlayerControllerNowPlayingItemDidChangeNotification", player, 0);
        if (playback_changed) rt_notify(c, "MPMusicPlayerControllerPlaybackStateDidChangeNotification", player, 0);
    }
}
#endif

/* MPMediaPropertyPredicate */
static id cls_predicate;
typedef struct { id value, property; uint32_t comparison; } pred_state;
static pred_state *ps(id x) { return rt_objc_state(x, sizeof(pred_state)); }
static id pred_new(id value, id property, uint32_t comparison) {
    id x = rt_objc_alloc(cls_predicate);
    pred_state *s = ps(x);
    s->value = rt_retain(value);
    s->property = rt_retain(property);
    s->comparison = comparison;
    return x;
}
M(pr_with2) { (void)m; RET(rt_autorelease(pred_new(ARG(0), ARG(1), 0))); }
M(pr_with3) { (void)m; RET(rt_autorelease(pred_new(ARG(0), ARG(1), ARG(2)))); }
M(pr_value) { (void)m; RET(ps(SELF)->value); }
M(pr_property) { (void)m; RET(ps(SELF)->property); }
M(pr_comparison) { (void)m; RET(ps(SELF)->comparison); }
M(pr_dealloc) {
    (void)m;
    rt_release(c, ps(SELF)->value);
    rt_release(c, ps(SELF)->property);
    rt_objc_free(SELF);
}

/* MPMediaPickerController (a UIViewController): a[0] delegate (not retained); w[0] media types,
 * w[1] multiple items, w[2] cancel scheduled. With no library to pick from, the user cancels. */
static id cls_picker;
static void pk_init_common(cpu_t *c, uint32_t types) {
    id self = super_init(c, cls_picker);
    if (self) ext(self)->w[0] = types;
    RET(self);
}
M(pk_init_types) { (void)m; pk_init_common(c, ARG(0)); }
M(pk_init) { (void)m; pk_init_common(c, ~0u); } /* MPMediaTypeAny */
M(pk_set_delegate) { (void)m; ext(SELF)->a[0] = ARG(0); }
M(pk_delegate) { (void)m; RET(ext(SELF)->a[0]); }
M(pk_set_multiple) { (void)m; ext(SELF)->w[1] = ARG(0) & 0xff; }
M(pk_multiple) { (void)m; RET(ext(SELF)->w[1]); }
M(pk_types) { (void)m; RET(ext(SELF)->w[0]); }
M(pk_view) {
    (void)m;
    uint32_t v = call_super(c, cls_picker, "view", 0);
    ext_t *e = ext(SELF);
    if (!e->w[2]) {
        e->w[2] = 1;
        rt_runloop_perform_later(SELF, rt_sel("_rtPickerCancel"), 0, 0.0);
    }
    RET(v);
}
M(pk_cancel) {
    (void)m;
    id d = ext(SELF)->a[0];
    rt_log("offline: media picker cancelled (no music library)");
    if (d && rt_responds(d, "mediaPickerDidCancel:")) rt_send(c, d, "mediaPickerDidCancel:", 1, SELF);
}
M(pk_dealloc) { (void)m; ext_drop(c, SELF, 0); call_super(c, cls_picker, "dealloc", 0); }

/* ============================== UIKit screens the store/ads/news use ================================= */
/* UIWebView: a[0] delegate. Nothing ever loads. */
static id cls_webview;
M(wv_set_delegate) { (void)m; ext(SELF)->a[0] = ARG(0); }
M(wv_delegate) { (void)m; RET(ext(SELF)->a[0]); }
M(wv_load) { (void)m; static int once; log_once(&once, "UIWebView loads nothing"); (void)c; }
M(wv_eval_js) { (void)m; RET(rt_nsstring("")); }
M(wv_dealloc) { (void)m; ext_drop(c, SELF, 0); call_super(c, cls_webview, "dealloc", 0); }

/* UITableView: a[0] data source, a[1] delegate (neither retained); w[0] style. -reloadData asks for nothing. */
static id cls_table, cls_cell;
M(tv_init_style) {
    (void)m;
    id self = SELF;
    uint32_t style = ARG(4);
    id r = rt_send(c, self, "initWithFrame:", 4, ARG(0), ARG(1), ARG(2), ARG(3));
    if (r) ext(r)->w[0] = style;
    RET(r);
}
M(tv_set_source) { (void)m; ext(SELF)->a[0] = ARG(0); }
M(tv_source) { (void)m; RET(ext(SELF)->a[0]); }
M(tv_set_delegate) { (void)m; ext(SELF)->a[1] = ARG(0); }
M(tv_delegate) { (void)m; RET(ext(SELF)->a[1]); }
M(tv_style) { (void)m; RET(ext(SELF)->w[0]); }
M(tv_row_height) { (void)m; rt_fret(c, 44.0f); }
M(tv_dealloc) { (void)m; ext_drop(c, SELF, 0); call_super(c, cls_table, "dealloc", 0); }

/* UITableViewCell: its content view is the cell itself */
M(cell_init_frame) { /* initWithFrame:(CGRect)reuseIdentifier: */
    (void)m;
    id r = rt_send(c, SELF, "initWithFrame:", 4, ARG(0), ARG(1), ARG(2), ARG(3));
    RET(r);
}
M(cell_init_style) { /* initWithStyle:reuseIdentifier: */
    (void)m;
    RET(rt_send(c, SELF, "initWithFrame:", 4, 0, 0, 0x43a00000u /*320*/, 0x42300000u /*44*/));
}
M(cell_content) { (void)m; (void)c; }

/* UIImagePickerController: no source type is available, so the game never presents one */
static id cls_image_picker;
M(ip_available) { (void)m; RET(0); }
M(ip_set_source) { (void)m; ext(SELF)->w[0] = ARG(0); }
M(ip_source) { (void)m; RET(ext(SELF)->w[0]); }
M(ip_set_delegate) { (void)m; ext(SELF)->a[0] = ARG(0); }
M(ip_delegate) { (void)m; RET(ext(SELF)->a[0]); }
M(ip_dealloc) { (void)m; ext_drop(c, SELF, 0); call_super(c, cls_image_picker, "dealloc", 0); }

/* UIPopoverController (an NSObject): never shown */
static id cls_popover;
typedef struct { id content, delegate; } popover_state;
static popover_state *pos(id x) { return rt_objc_state(x, sizeof(popover_state)); }
M(po_init) { (void)m; set_retained(c, &pos(SELF)->content, ARG(0)); RET(SELF); }
M(po_content) { (void)m; RET(pos(SELF)->content); }
M(po_set_content) { (void)m; set_retained(c, &pos(SELF)->content, ARG(0)); }
M(po_set_delegate) { (void)m; pos(SELF)->delegate = ARG(0); }
M(po_delegate) { (void)m; RET(pos(SELF)->delegate); }
M(po_present) { (void)m; static int once; log_once(&once, "UIPopoverController is never shown"); (void)c; }
M(po_dealloc) { (void)m; rt_release(c, pos(SELF)->content); rt_objc_free(SELF); }

/* UITabBarController: a[0] view controllers (retained), a[1] delegate; w[0] selected index */
static id cls_tabbar;
M(tb_set_vcs) { (void)m; ext_t *e = ext(SELF); set_retained(c, &e->a[0], ARG(0)); e->w[0] = 0; }
M(tb_vcs) { (void)m; RET(ext(SELF)->a[0]); }
M(tb_selected_vc) {
    (void)m;
    ext_t *e = ext(SELF);
    RET(e->w[0] < rt_nsarray_count(e->a[0]) ? rt_nsarray_at(e->a[0], e->w[0]) : 0);
}
M(tb_set_selected_vc) {
    (void)m;
    ext_t *e = ext(SELF);
    for (uint32_t k = 0, n = rt_nsarray_count(e->a[0]); k < n; k++)
        if (rt_nsarray_at(e->a[0], k) == ARG(0)) e->w[0] = k;
}
M(tb_selected_index) { (void)m; RET(ext(SELF)->w[0]); }
M(tb_set_selected_index) { (void)m; ext(SELF)->w[0] = ARG(0); }
M(tb_set_delegate) { (void)m; ext(SELF)->a[1] = ARG(0); }
M(tb_delegate) { (void)m; RET(ext(SELF)->a[1]); }
M(tb_dealloc) { (void)m; ext_drop(c, SELF, 1); call_super(c, cls_tabbar, "dealloc", 0); }

/* ============================== definitions ========================================================== */
typedef struct { const char *sel; rt_hostfn fn; } meth;
static void methods(id cls, const meth *t, size_t n) { for (size_t k = 0; k < n; k++) rt_objc_method(cls, t[k].sel, t[k].fn); }
static void cmethods(id cls, const meth *t, size_t n) { for (size_t k = 0; k < n; k++) rt_objc_cmethod(cls, t[k].sel, t[k].fn); }
static void noops(id cls, const char *const *sels, size_t n) { for (size_t k = 0; k < n; k++) rt_objc_method(cls, sels[k], ret_self); }
#define METHODS(cls, ...) do { static const meth t_[] = {__VA_ARGS__}; methods(cls, t_, sizeof t_ / sizeof *t_); } while (0)
#define CMETHODS(cls, ...) do { static const meth t_[] = {__VA_ARGS__}; cmethods(cls, t_, sizeof t_ / sizeof *t_); } while (0)
#define NOOPS(cls, ...) do { static const char *const t_[] = {__VA_ARGS__}; noops(cls, t_, sizeof t_ / sizeof *t_); } while (0)

/* define a class under the first of the given superclasses that exists (uikit.c defines the UIKit bases) */
static id def(const char *name, const char *super, const char *fallback) {
    if (!rt_class(super)) super = fallback;
    return rt_objc_define(name, super, 0);
}

void rt_offline_define(void) {
    /* SystemConfiguration */
    cls_reach = def("__SCNetworkReachability", "NSObject", "NSObject");

    /* NSError */
    cls_error = def("NSError", "NSObject", "NSObject");
    CMETHODS(cls_error, {"errorWithDomain:code:userInfo:", err_with});
    METHODS(cls_error, {"initWithDomain:code:userInfo:", err_init}, {"code", err_code}, {"domain", err_domain},
            {"userInfo", err_user_info}, {"localizedDescription", err_localized},
            {"localizedFailureReason", err_reason}, {"localizedRecoverySuggestion", ret_zero},
            {"description", err_description}, {"copy", err_copy}, {"copyWithZone:", err_copy},
            {"dealloc", err_dealloc});

    /* NSURL */
    cls_url = def("NSURL", "NSObject", "NSObject");
    CMETHODS(cls_url, {"URLWithString:", url_with_string}, {"fileURLWithPath:", url_with_path},
             {"fileURLWithPath:isDirectory:", url_with_path_dir}, {"URLWithString:relativeToURL:", url_with_string_rel});
    METHODS(cls_url, {"initWithString:", url_init_string}, {"initFileURLWithPath:", url_init_path},
            {"initFileURLWithPath:isDirectory:", url_init_path_dir}, {"absoluteString", url_absolute},
            {"relativeString", url_absolute}, {"description", url_absolute}, {"path", url_path},
            {"relativePath", url_path}, {"scheme", url_scheme}, {"host", url_host}, {"query", url_query},
            {"isFileURL", url_is_file}, {"isEqual:", url_is_equal}, {"hash", url_hash}, {"copy", url_copy},
            {"copyWithZone:", url_copy}, {"absoluteURL", ret_self}, {"standardizedURL", ret_self},
            {"baseURL", ret_zero}, {"port", ret_zero}, {"fragment", ret_zero});

    /* NSURLRequest, NSMutableURLRequest */
    cls_request = def("NSURLRequest", "NSObject", "NSObject");
    cls_mrequest = def("NSMutableURLRequest", "NSURLRequest", "NSObject");
    CMETHODS(cls_request, {"requestWithURL:", req_with_url},
             {"requestWithURL:cachePolicy:timeoutInterval:", req_with_url_full});
    METHODS(cls_request, {"initWithURL:", req_init_url}, {"initWithURL:cachePolicy:timeoutInterval:", req_init_url_full},
            {"URL", req_url}, {"HTTPMethod", req_method}, {"HTTPBody", req_body}, {"timeoutInterval", req_timeout},
            {"cachePolicy", req_policy}, {"allHTTPHeaderFields", ret_zero}, {"valueForHTTPHeaderField:", ret_zero},
            {"copy", req_copy}, {"copyWithZone:", req_copy}, {"mutableCopy", req_mutable_copy},
            {"mutableCopyWithZone:", req_mutable_copy}, {"dealloc", req_dealloc});
    METHODS(cls_mrequest, {"setURL:", req_set_url}, {"setHTTPMethod:", req_set_method}, {"setHTTPBody:", req_set_body},
            {"setTimeoutInterval:", req_set_timeout}, {"setCachePolicy:", req_set_policy});
    NOOPS(cls_mrequest, "setValue:forHTTPHeaderField:", "addValue:forHTTPHeaderField:", "setAllHTTPHeaderFields:",
          "setHTTPShouldHandleCookies:", "setMainDocumentURL:");

    /* NSURLConnection */
    cls_connection = def("NSURLConnection", "NSObject", "NSObject");
    CMETHODS(cls_connection, {"connectionWithRequest:delegate:", conn_with}, {"canHandleRequest:", ret_one},
             {"sendSynchronousRequest:returningResponse:error:", conn_send_sync});
    METHODS(cls_connection, {"initWithRequest:delegate:", conn_init_rd},
            {"initWithRequest:delegate:startImmediately:", conn_init_rds}, {"start", conn_start_m},
            {"cancel", conn_cancel}, {"_rtOfflineFail", conn_fail}, {"dealloc", conn_dealloc});
    NOOPS(cls_connection, "scheduleInRunLoop:forMode:", "unscheduleFromRunLoop:forMode:");

    /* GameKit */
    cls_session = def("GKSession", "NSObject", "NSObject");
    METHODS(cls_session, {"initWithSessionID:displayName:sessionMode:", gk_init}, {"setDelegate:", gk_set_delegate},
            {"delegate", gk_delegate}, {"setAvailable:", gk_set_available}, {"isAvailable", gk_available},
            {"available", gk_available}, {"sessionID", gk_session_id}, {"displayName", gk_display_name},
            {"sessionMode", gk_mode}, {"peerID", gk_peer_id}, {"disconnectTimeout", gk_timeout},
            {"peersWithConnectionState:", ret_empty_array}, {"displayNameForPeer:", ret_zero},
            {"sendData:toPeers:withDataMode:error:", gk_send_peers},
            {"sendDataToAllPeers:withDataMode:error:", gk_send_all},
            {"acceptConnectionFromPeer:error:", gk_accept}, {"_rtSessionFail", gk_fail}, {"dealloc", gk_dealloc});
    NOOPS(cls_session, "setDisconnectTimeout:", "disconnectFromAllPeers", "disconnectPeerFromAllPeers:",
          "setDataReceiveHandler:withContext:", "connectToPeer:withTimeout:", "cancelConnectToPeer:",
          "denyConnectionFromPeer:");

    /* Bonjour */
    cls_netservice = def("NSNetService", "NSObject", "NSObject");
    METHODS(cls_netservice, {"initWithDomain:type:name:port:", ns_init4}, {"initWithDomain:type:name:", ns_init3},
            {"setDelegate:", ns_set_delegate}, {"delegate", ns_delegate}, {"domain", ns_domain}, {"type", ns_type},
            {"name", ns_name}, {"port", ns_port}, {"publish", ns_publish}, {"addresses", ret_empty_array},
            {"hostName", ret_zero}, {"TXTRecordData", ret_zero}, {"dealloc", ns_dealloc});
    NOOPS(cls_netservice, "resolve", "resolveWithTimeout:", "stop", "scheduleInRunLoop:forMode:",
          "removeFromRunLoop:forMode:", "startMonitoring", "stopMonitoring");
    cls_browser = def("NSNetServiceBrowser", "NSObject", "NSObject");
    METHODS(cls_browser, {"setDelegate:", br_set_delegate}, {"delegate", br_delegate},
            {"searchForServicesOfType:inDomain:", br_search}, {"searchForBrowsableDomains", br_search},
            {"searchForRegistrationDomains", br_search}, {"stop", br_stop});
    NOOPS(cls_browser, "scheduleInRunLoop:forMode:", "removeFromRunLoop:forMode:");

    /* MediaPlayer: movies */
    cls_movie = def("MPMoviePlayerController", "NSObject", "NSObject");
    METHODS(cls_movie, {"initWithContentURL:", mp_init_url}, {"play", mp_play}, {"pause", mp_pause},
            {"stop", mp_stop}, {"_rtMovieEnded", mp_ended}, {"playbackState", mp_state},
            {"setFullscreen:", mp_set_fullscreen}, {"setFullscreen:animated:", mp_set_fullscreen},
            {"isFullscreen", mp_fullscreen}, {"contentURL", mp_url}, {"setContentURL:", mp_set_url},
            {"view", mp_view}, {"backgroundView", mp_view}, {"isPreparedToPlay", ret_one},
            {"currentPlaybackTime", ret_dzero}, {"duration", ret_dzero}, {"initialPlaybackTime", ret_dzero},
            {"playableDuration", ret_dzero}, {"loadState", ret_zero}, {"shouldAutoplay", ret_one},
            {"dealloc", mp_dealloc});
    NOOPS(cls_movie, "prepareToPlay", "setUseApplicationAudioSession:", "setControlStyle:", "setScalingMode:",
          "setMovieControlMode:", "setShouldAutoplay:", "setRepeatMode:", "setInitialPlaybackTime:",
          "setCurrentPlaybackTime:", "setBackgroundColor:", "setEndPlaybackTime:", "beginSeekingForward",
          "beginSeekingBackward", "endSeeking");
    cls_movie_vc = def("MPMoviePlayerViewController", "UIViewController", "NSObject");
    METHODS(cls_movie_vc, {"initWithContentURL:", mpvc_init_url}, {"moviePlayer", mpvc_player},
            {"dealloc", mpvc_dealloc});
    id vc = rt_class("UIViewController");
    if (vc) {
        rt_objc_method(vc, "presentMoviePlayerViewControllerAnimated:", vc_present_movie);
        rt_objc_method(vc, "dismissMoviePlayerViewControllerAnimated", ret_self);
    }

    /* MediaPlayer: music, the library */
    cls_music = def("MPMusicPlayerController", "NSObject", "NSObject");
    CMETHODS(cls_music, {"iPodMusicPlayer", mu_ipod}, {"applicationMusicPlayer", mu_app});
    METHODS(cls_music, {"playbackState", ret_zero}, {"nowPlayingItem", ret_zero}, {"repeatMode", ret_zero},
            {"shuffleMode", ret_zero}, {"volume", mu_volume}, {"currentPlaybackTime", ret_dzero},
            {"retain", ret_self}, {"release", ret_self}, {"autorelease", ret_self}, {"dealloc", ret_self});
#ifdef __ANDROID__
    METHODS(cls_music, {"play", mu_play}, {"pause", mu_pause}, {"stop", mu_stop},
            {"skipToNextItem", mu_next}, {"skipToPreviousItem", mu_prev},
            {"setQueueWithQuery:", mu_queue_query}, {"setQueueWithItemCollection:", mu_queue_collection},
            {"setNowPlayingItem:", mu_set_item}, {"playbackState", mu_state}, {"nowPlayingItem", mu_current},
            {"beginGeneratingPlaybackNotifications", mu_observe}, {"endGeneratingPlaybackNotifications", mu_unobserve});
#else
    NOOPS(cls_music, "play", "pause", "stop", "setNowPlayingItem:", "setQueueWithQuery:",
          "setQueueWithItemCollection:", "beginGeneratingPlaybackNotifications",
          "endGeneratingPlaybackNotifications", "skipToNextItem", "skipToPreviousItem");
#endif
    NOOPS(cls_music, "skipToBeginning", "setVolume:", "setRepeatMode:", "setShuffleMode:",
          "setCurrentPlaybackTime:", "beginSeekingForward", "beginSeekingBackward", "endSeeking");
    ipod_player = rt_objc_alloc(cls_music);
    app_player = rt_objc_alloc(cls_music);

    cls_query = def("MPMediaQuery", "NSObject", "NSObject");
    CMETHODS(cls_query, {"songsQuery", q_songs}, {"albumsQuery", q_albums}, {"artistsQuery", q_artists},
             {"composersQuery", q_composers}, {"genresQuery", q_genres}, {"playlistsQuery", q_playlists},
             {"podcastsQuery", q_podcasts}, {"audiobooksQuery", q_audiobooks},
             {"compilationsQuery", q_compilations});
    METHODS(cls_query, {"initWithFilterPredicates:", q_init_preds}, {"items", q_items},
            {"collections", q_collections}, {"groupingType", q_grouping}, {"setGroupingType:", q_set_grouping},
            {"filterPredicates", q_predicates}, {"dealloc", q_dealloc});
    NOOPS(cls_query, "addFilterPredicate:", "removeFilterPredicate:", "setFilterPredicates:");

    cls_collection = def("MPMediaItemCollection", "NSObject", "NSObject");
    CMETHODS(cls_collection, {"collectionWithItems:", co_with_items});
    METHODS(cls_collection, {"initWithItems:", co_init_items}, {"items", co_items}, {"count", co_count},
            {"representativeItem", co_representative}, {"mediaTypes", ret_one}, {"valueForProperty:", co_property},
            {"dealloc", co_dealloc});

    cls_playlist = def("MPMediaPlaylist", "MPMediaItemCollection", "MPMediaItemCollection");
    METHODS(cls_playlist, {"name", co_name});
    cls_media_item = def("MPMediaItem", "NSObject", "NSObject");
    METHODS(cls_media_item, {"title", mi_title}, {"artist", mi_artist}, {"albumTitle", mi_album},
            {"mediaType", ret_one}, {"valueForProperty:", mi_property}, {"dealloc", mi_dealloc});

    cls_predicate = def("MPMediaPropertyPredicate", "NSObject", "NSObject");
    CMETHODS(cls_predicate, {"predicateWithValue:forProperty:", pr_with2},
             {"predicateWithValue:forProperty:comparisonType:", pr_with3});
    METHODS(cls_predicate, {"value", pr_value}, {"property", pr_property}, {"comparisonType", pr_comparison},
            {"dealloc", pr_dealloc});

    cls_picker = def("MPMediaPickerController", "UIViewController", "NSObject");
    METHODS(cls_picker, {"init", pk_init}, {"initWithMediaTypes:", pk_init_types}, {"setDelegate:", pk_set_delegate},
            {"delegate", pk_delegate}, {"setAllowsPickingMultipleItems:", pk_set_multiple},
            {"allowsPickingMultipleItems", pk_multiple}, {"mediaTypes", pk_types}, {"view", pk_view},
            {"_rtPickerCancel", pk_cancel}, {"dealloc", pk_dealloc});
    NOOPS(cls_picker, "setPrompt:");

    /* UIKit screens */
    cls_webview = def("UIWebView", "UIView", "NSObject");
    METHODS(cls_webview, {"setDelegate:", wv_set_delegate}, {"delegate", wv_delegate}, {"loadRequest:", wv_load},
            {"loadHTMLString:baseURL:", wv_load}, {"loadData:MIMEType:textEncodingName:baseURL:", wv_load},
            {"canGoBack", ret_zero}, {"canGoForward", ret_zero}, {"isLoading", ret_zero}, {"request", ret_zero},
            {"scalesPageToFit", ret_zero}, {"stringByEvaluatingJavaScriptFromString:", wv_eval_js},
            {"dealloc", wv_dealloc});
    NOOPS(cls_webview, "stopLoading", "reload", "goBack", "goForward", "setScalesPageToFit:",
          "setDataDetectorTypes:", "setDetectsPhoneNumbers:");

    cls_table = def("UITableView", "UIScrollView", "UIView");
    METHODS(cls_table, {"initWithFrame:style:", tv_init_style}, {"setDataSource:", tv_set_source},
            {"dataSource", tv_source}, {"setDelegate:", tv_set_delegate}, {"delegate", tv_delegate},
            {"style", tv_style}, {"rowHeight", tv_row_height}, {"dequeueReusableCellWithIdentifier:", ret_zero},
            {"indexPathForSelectedRow", ret_zero}, {"cellForRowAtIndexPath:", ret_zero},
            {"visibleCells", ret_empty_array}, {"indexPathsForVisibleRows", ret_empty_array},
            {"numberOfSections", ret_zero}, {"numberOfRowsInSection:", ret_zero}, {"dealloc", tv_dealloc});
    NOOPS(cls_table, "reloadData", "setRowHeight:", "deselectRowAtIndexPath:animated:",
          "selectRowAtIndexPath:animated:scrollPosition:", "scrollToRowAtIndexPath:atScrollPosition:animated:",
          "setSeparatorStyle:", "setSeparatorColor:", "setAllowsSelection:", "setBackgroundView:",
          "setTableHeaderView:", "setTableFooterView:", "beginUpdates", "endUpdates", "setEditing:animated:",
          "setSectionHeaderHeight:", "setSectionFooterHeight:", "flashScrollIndicators");

    cls_cell = def("UITableViewCell", "UIView", "NSObject");
    METHODS(cls_cell, {"initWithFrame:reuseIdentifier:", cell_init_frame},
            {"initWithStyle:reuseIdentifier:", cell_init_style}, {"contentView", cell_content},
            {"textLabel", ret_zero}, {"detailTextLabel", ret_zero}, {"imageView", ret_zero},
            {"reuseIdentifier", ret_zero}, {"isSelected", ret_zero}, {"isHighlighted", ret_zero});
    NOOPS(cls_cell, "setText:", "setFont:", "setTextColor:", "setSelectedTextColor:", "setImage:",
          "setSelectedImage:", "setSelectionStyle:", "setAccessoryType:", "setAccessoryView:", "setSelected:",
          "setSelected:animated:", "setHighlighted:", "setHighlighted:animated:", "setBackgroundView:",
          "setSelectedBackgroundView:", "setLineBreakMode:", "setTextAlignment:", "setIndentationLevel:",
          "prepareForReuse");

    cls_image_picker = def("UIImagePickerController", "UINavigationController", "UIViewController");
    CMETHODS(cls_image_picker, {"isSourceTypeAvailable:", ip_available},
             {"availableMediaTypesForSourceType:", ret_zero});
    METHODS(cls_image_picker, {"setSourceType:", ip_set_source}, {"sourceType", ip_source},
            {"setDelegate:", ip_set_delegate}, {"delegate", ip_delegate}, {"dealloc", ip_dealloc});
    NOOPS(cls_image_picker, "setAllowsImageEditing:", "setAllowsEditing:", "setMediaTypes:");

    cls_popover = def("UIPopoverController", "NSObject", "NSObject");
    METHODS(cls_popover, {"initWithContentViewController:", po_init}, {"contentViewController", po_content},
            {"setContentViewController:", po_set_content}, {"setContentViewController:animated:", po_set_content},
            {"setDelegate:", po_set_delegate}, {"delegate", po_delegate}, {"isPopoverVisible", ret_zero},
            {"presentPopoverFromRect:inView:permittedArrowDirections:animated:", po_present},
            {"presentPopoverFromBarButtonItem:permittedArrowDirections:animated:", po_present},
            {"dealloc", po_dealloc});
    NOOPS(cls_popover, "dismissPopoverAnimated:", "setPopoverContentSize:", "setPopoverContentSize:animated:",
          "setPassthroughViews:");

    cls_tabbar = def("UITabBarController", "UIViewController", "NSObject");
    METHODS(cls_tabbar, {"setViewControllers:", tb_set_vcs}, {"setViewControllers:animated:", tb_set_vcs},
            {"viewControllers", tb_vcs}, {"selectedViewController", tb_selected_vc},
            {"setSelectedViewController:", tb_set_selected_vc}, {"selectedIndex", tb_selected_index},
            {"setSelectedIndex:", tb_set_selected_index}, {"setDelegate:", tb_set_delegate},
            {"delegate", tb_delegate}, {"tabBar", ret_zero}, {"dealloc", tb_dealloc});

    rt_data_resolver_add(resolve);
}

void rt_offline_register(void) {
    REG(SCNetworkReachabilityCreateWithName); REG(SCNetworkReachabilityCreateWithAddress);
    REG(SCNetworkReachabilityGetFlags); REG(SCNetworkReachabilitySetCallback);
    REG(SCNetworkReachabilityScheduleWithRunLoop); REG(SCNetworkReachabilityUnscheduleFromRunLoop);
    REG(socket); REG(connect); REG(bind); REG(listen); REG(accept); REG(send); REG(recv); REG(sendto);
    REG(recvfrom); REG(select); REG(setsockopt); REG(getsockopt); REG(getsockname); REG(fcntl); REG(ioctl);
    REG(gethostbyname); REG(getaddrinfo); REG(freeaddrinfo); REG(getprotobyname); REG(inet_addr);
    REG(inet_aton); REG(inet_ntoa);
}
