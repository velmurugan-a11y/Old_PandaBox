/*
 * syscalls.c -- minimal newlib syscall stubs for this bare-metal, no-OS
 * build. Only _sbrk is actually needed (snprintf's internal buffering in
 * cmd.c pulls it in); no file I/O exists on this target.
 */
#include <stdint.h>

extern uint8_t _end; /* linker-provided: see linker/gd32f305vct6.ld */
static uint8_t *heap_end = &_end;

void *_sbrk(int incr)
{
    uint8_t *prev_heap_end = heap_end;
    heap_end += incr;
    return prev_heap_end;
}
