/*
        The library's names that src/audio/mix.c calls, for the lane's standalone build of mix.c (gcc -c and clang -c, with
        -include of this file): declarations only, so the compile sees them and nothing links. The definitions are lib.c's
        in the service, and the hosted checks give CHECK_audio_mix and CHECK_audio_pulse their own shims (test/checks.c).
*/
#include <stddef.h>

#ifndef offset_of
#define offset_of(type, member) offsetof(type, member)
#endif

void *memory_copy_apart(void *destination, const void *source, unsigned long size);
void *memory_copy(void *destination, const void *source, unsigned long size);
void *memory_fill(void *destination, unsigned char value, unsigned long size);
