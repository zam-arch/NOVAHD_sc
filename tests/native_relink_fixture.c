/* Synthetic linker input. It does not boot a game. */
#include "rt.h"

const uint32_t rt_image_base = 0x1000, rt_image_size = 4, rt_import_stub_base = 0x00f00000;
const uint32_t rt_init_funcs[] = {0}, rt_init_func_count = 0, rt_main = 0x1000;
const char *const rt_import_names[] = {0};
const uint32_t rt_import_count = 0;
const rt_bind rt_binds[] = {{0, 0, 0}};
const uint32_t rt_bind_count = 0;
const uint32_t rt_objc_sections[RT_OBJC_NSECTIONS][2] = {{0, 0}};
const uint32_t rt_func_addrs[] = {0}, rt_func_count = 0;
const char *const rt_func_names[] = {0};
