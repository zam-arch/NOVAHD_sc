#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "rt.h"

void rt_log(const char *fmt, ...) { (void)fmt; }

void rt_fatal(const char *fmt, ...) {
    fputs(fmt, stderr);
    abort();
}

int main(void) {
    const uint8_t image[] = {1, 2, 3, 4};
    assert(rt_mem_init_from(image, sizeof image) == 0);
    assert(rt_ld32(rt_image_base) == 0x04030201);
    assert(rt_ld32(rt_import_stub_base) == 0xe12fff1e);
    uint32_t heap = rt_malloc(32);
    assert(heap >= RT_HEAP_BASE);
    rt_st32(heap, 0x12345678);
    assert(rt_ld32(heap) == 0x12345678);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        struct rlimit core = {0, 0};
        if (setrlimit(RLIMIT_CORE, &core)) _exit(2);
        volatile uint8_t *guard = rt_mem;
        *guard = 1;
        _exit(1);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
    printf("Host page %ld: mapped image, import stubs, heap, and protected null page passed\n", sysconf(_SC_PAGESIZE));
}
