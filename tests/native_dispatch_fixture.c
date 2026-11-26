/* Stand-in for the translated dispatcher, for validating the link contract only. */
#include "recomp.h"
int rc_is_entry(uint32_t address) { return address == 0x1000; }
void rc_dispatch(cpu_t *cpu, uint8_t *mem, uint32_t address) {
    (void)mem;
    cpu->r[15] = address;
}
