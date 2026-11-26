/* Game memory: the 4 GB reservation, the loaded image, and the game heap. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#include "host_pages.h"
static size_t host_page_size;
#endif

uint8_t *rt_mem;

void rt_commit(uint32_t addr, uint32_t size) {
    if (!size) return;
#ifdef _WIN32
    uint64_t end = ((uint64_t)addr + size + 0xfff) & ~UINT64_C(0xfff);
    uint32_t start = addr & ~0xfffu;
    if (addr < 0x1000 || end > (UINT64_C(1) << 32)
            || !VirtualAlloc(rt_mem + start, end - start, MEM_COMMIT, PAGE_READWRITE))
        rt_fatal("commit %#x+%#x failed", addr, size);
#else
    uintptr_t start, end;
    if (!rt_host_page_range((uintptr_t)rt_mem, host_page_size, addr, size, &start, &end)
            || mprotect((void *)start, end - start, PROT_READ | PROT_WRITE))
        rt_fatal("commit %#x+%#x failed (host page %zu)", addr, size, host_page_size);
#endif
}

static void reserve(void) {
#ifdef _WIN32
    rt_mem = VirtualAlloc(NULL, 1ull << 32, MEM_RESERVE, PAGE_NOACCESS);
#else
    long page = sysconf(_SC_PAGESIZE);
    if (page < 0x1000 || (page & (page - 1)))
        rt_fatal("unsupported host page size %ld", page);
    host_page_size = (size_t)page;
    uint8_t *mapping = mmap(NULL, (1ull << 32) + host_page_size, PROT_NONE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    rt_mem = mapping == MAP_FAILED ? NULL : mapping + host_page_size - 0x1000;
#endif
    if (!rt_mem) rt_fatal("cannot reserve 4 GB of address space for game memory");
    rt_commit(rt_image_base, rt_image_size);
}

static void finish(void) {
    /* import stubs hold `bx lr`, as in the test harness: code may read through a pointer to an import */
    rt_commit(rt_import_stub_base, 0x1000);
    for (uint32_t k = 0; k < 0x400; k++) rt_st32(rt_import_stub_base + 4 * k, 0xE12FFF1Eu);
    rt_commit(RT_DATAIMP_BASE, RT_DATAIMP_SIZE);
}

int rt_mem_init(const char *image_path) {
    reserve();
    FILE *f = fopen(image_path, "rb");
    if (!f) rt_fatal("cannot open %s", image_path);
    size_t got = fread(rt_mem + rt_image_base, 1, rt_image_size, f);
    fclose(f);
    if (got != rt_image_size) rt_fatal("%s: %zu bytes, expected %u", image_path, got, rt_image_size);
    finish();
    return 0;
}

int rt_mem_init_from(const void *image, size_t size) {
    reserve();
    if (size != rt_image_size) rt_fatal("image: %zu bytes, expected %u", size, rt_image_size);
    memcpy(rt_mem + rt_image_base, image, size);
    finish();
    return 0;
}

/* ---- heap --------------------------------------------------------------------------------------
 * Blocks carry an 8-byte header {usable size, magic}. Requests up to 4 KB come from 256 size classes of 16
 * bytes with one free list each; larger ones from a first-fit list. Fresh memory comes from a break that
 * commits 1 MB at a time. Simple, thread-safe with a spinlock, and enough for the game's allocation pattern. */
#define HDR 8u
#define MAGIC_USED 0xA110C8EDu
#define MAGIC_FREE 0xF4EEB10Cu
#define SMALL_MAX 4096u

static volatile int heap_lock;
static uint32_t brk_ptr = RT_HEAP_BASE, committed = RT_HEAP_BASE;
static uint32_t small_free[SMALL_MAX / 16 + 1];
static uint32_t large_free; /* singly linked through the first word of the block */

static void lock(void) { while (__atomic_exchange_n(&heap_lock, 1, __ATOMIC_ACQUIRE)) { } }
static void unlock(void) { __atomic_store_n(&heap_lock, 0, __ATOMIC_RELEASE); }

static uint32_t carve(uint32_t usable) {
    uint32_t block = brk_ptr, end = brk_ptr + HDR + usable;
    if (end > RT_HEAP_END) return 0;
    if (end > committed) {
        uint32_t grow = (end - committed + 0xFFFFFu) & ~0xFFFFFu;
        rt_commit(committed, grow);
        committed += grow;
    }
    brk_ptr = end;
    rt_st32(block, usable);
    return block + HDR;
}

uint32_t rt_malloc(uint32_t size) {
    uint32_t usable = (size + 15u) & ~15u, p = 0;
    if (usable == 0) usable = 16;
    lock();
    if (usable <= SMALL_MAX) {
        uint32_t *head = &small_free[usable / 16];
        if (*head) { p = *head; *head = rt_ld32(p); }
    } else {
        uint32_t prev = 0, cur = large_free;
        while (cur) {
            uint32_t have = rt_ld32(cur - HDR), next = rt_ld32(cur);
            if (have >= usable && have <= usable * 2 + 65536) {
                if (prev) rt_st32(prev, next); else large_free = next;
                p = cur;
                break;
            }
            prev = cur;
            cur = next;
        }
    }
    if (!p) p = carve(usable);
    if (p) rt_st32(p - 4, MAGIC_USED);
    unlock();
    return p;
}

void rt_free(uint32_t p) {
    if (!p) return;
    lock();
    if (rt_ld32(p - 4) != MAGIC_USED) {
        unlock();
        rt_log("free(%#x): not an allocated block (magic %#x)", p, rt_ld32(p - 4));
        return;
    }
    uint32_t usable = rt_ld32(p - HDR);
    rt_st32(p - 4, MAGIC_FREE);
    if (usable <= SMALL_MAX) {
        rt_st32(p, small_free[usable / 16]);
        small_free[usable / 16] = p;
    } else {
        rt_st32(p, large_free);
        large_free = p;
    }
    unlock();
}

uint32_t rt_malloc_usable(uint32_t p) { return p ? rt_ld32(p - HDR) : 0; }

uint32_t rt_calloc(uint32_t n, uint32_t size) {
    uint64_t total = (uint64_t)n * size;
    if (total > 0x7FFFFFFF) return 0;
    uint32_t p = rt_malloc((uint32_t)total);
    if (p) memset(rt_mem + p, 0, (size_t)total);
    return p;
}

uint32_t rt_realloc(uint32_t p, uint32_t size) {
    if (!p) return rt_malloc(size);
    if (!size) { rt_free(p); return 0; }
    uint32_t have = rt_malloc_usable(p);
    if (size <= have) return p;
    uint32_t q = rt_malloc(size);
    if (q) { memcpy(rt_mem + q, rt_mem + p, have); rt_free(p); }
    return q;
}

uint32_t rt_strdup_guest(const char *s) {
    size_t n = strlen(s) + 1;
    uint32_t p = rt_malloc((uint32_t)n);
    if (p) memcpy(rt_mem + p, s, n);
    return p;
}

/* Interned strings for values the game keeps a pointer to (glGetString, getenv, strerror, ...). */
#define INTERN_SLOTS 1024
static struct { char *s; uint32_t addr; } interned[INTERN_SLOTS];
static volatile int intern_lock;

uint32_t rt_static_string(const char *s) {
    uint32_t h = 2166136261u;
    for (const char *p = s; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    while (__atomic_exchange_n(&intern_lock, 1, __ATOMIC_ACQUIRE)) { }
    uint32_t addr = 0;
    for (uint32_t k = 0; k < INTERN_SLOTS; k++) {
        uint32_t i = (h + k) % INTERN_SLOTS;
        if (!interned[i].s) {
            interned[i].s = strdup(s);
            interned[i].addr = addr = rt_strdup_guest(s);
            break;
        }
        if (!strcmp(interned[i].s, s)) { addr = interned[i].addr; break; }
    }
    __atomic_store_n(&intern_lock, 0, __ATOMIC_RELEASE);
    if (!addr) addr = rt_strdup_guest(s); /* table full: leak a copy */
    return addr;
}
