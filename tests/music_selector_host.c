#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include "rt.h"

static void empty(cpu_t *cpu, uint8_t *memory) {
    (void)memory;
    rt_ret(cpu, 0);
}

static uint32_t count(id collection) {
    uint32_t imp = rt_lookup(collection, rt_sel("count"));
    assert(imp > RT_HOSTFN_BASE);
    cpu_t cpu = {0};
    rc_hostcall(&cpu, rt_mem, (imp - RT_HOSTFN_BASE) / 4);
    return cpu.r[0];
}

int main(int argc, char **argv) {
    const unsigned char image[4] = {0};
    assert(argc == 2 && rt_mem_init_from(image, sizeof image) == 0);
    id collection = rt_objc_define("FixtureCollection", NULL, 0);
    rt_objc_method(collection, "count", empty);
    assert(count(collection) == 0);
    void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(module);
    void (*install)(id) = dlsym(module, "install_test_method");
    assert(install);
    install(collection);
    assert(count(collection) == 1);
    rt_objc_method(collection, "count", empty);
    assert(count(collection) == 0);
    /* Keep module resident until process exit (the runtime retains host slots). */
    puts("Cached guest selector changed 0 -> 1 -> 0 using a companion DSO");
}
