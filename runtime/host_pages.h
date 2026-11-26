#ifndef NOVA_HOST_PAGES_H
#define NOVA_HOST_PAGES_H

#include <stddef.h>
#include <stdint.h>

/* The biased guest base puts guest address 0x1000 at a host page boundary.
 * Reject overlaps with the guest null guard and commits past 4 GiB. */
static inline int rt_host_page_range(uintptr_t guest, size_t page, uint32_t addr, uint32_t size,
                                     uintptr_t *start, uintptr_t *end) {
    const uint64_t limit = UINT64_C(1) << 32;
    uint64_t guest_end = (uint64_t)addr + size;
    if (page < 0x1000 || (page & (page - 1)) || !size || addr < 0x1000
            || guest_end > limit || (guest & (page - 1)) != page - 0x1000
            || page - 1 > UINTPTR_MAX - guest || guest_end > UINTPTR_MAX - guest - (page - 1))
        return 0;
    uintptr_t mask = (uintptr_t)page - 1;
    *start = (guest + addr) & ~mask;
    *end = (guest + guest_end + mask) & ~mask;
    return 1;
}

#endif
