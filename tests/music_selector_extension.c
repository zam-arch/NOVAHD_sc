#include "rt.h"

/* Loaded only by the Linux fixture. No Android packaging. */
static void one_item(cpu_t *cpu, uint8_t *memory) {
    (void)memory;
    rt_ret(cpu, 1);
}

void install_test_method(id collection) {
    rt_objc_method(collection, "count", one_item);
}
