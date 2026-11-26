#include <assert.h>
#include <stdio.h>
#include "host_pages.h"

int main(void) {
    const uintptr_t base = UINT64_C(0x4000000000);
    for (size_t page = 4096; page <= 16384; page *= 4) {
        uintptr_t guest = base + page - 4096, start, end;
        assert(rt_host_page_range(guest, page, 0x1000, 4517888, &start, &end));
        assert(start == base + page && end >= guest + 0x1000 + 4517888);
        assert(!rt_host_page_range(guest, page, 0xfff, 8, &start, &end));
        assert(rt_host_page_range(guest, page, 0xf00000, 0x1000, &start, &end));
        assert(start % page == 0 && end % page == 0);
        assert(rt_host_page_range(guest, page, 0x02000000, 0x100000, &start, &end));
        assert(start >= base + page && end >= guest + 0x02100000);
        assert(rt_host_page_range(guest, page, 0xfffff000, 0x1000, &start, &end));
        assert(end <= base + (UINT64_C(1) << 32) + page);
        assert(!rt_host_page_range(guest, page, 0xfffff000, 0x1001, &start, &end));
    }
    puts("4 KiB and 16 KiB host alignment, guest null guard, import range, heap, 4 GiB boundary passed");
}
