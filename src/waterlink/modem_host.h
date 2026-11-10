/*
	What a hosted build of the audio modem links against: the test (test/modem.c) and the Mac's receiver (tools/modem_rx.c).

	modem.c calls two functions of lib.c, sha256_blocks and hash_hmac_sha256_prepared. A hosted build compiles lib.c as an
	object with STANDARD_NO_ENTRY (its own _start is the C runtime's) and links it in. Two platform symbols that lib.c's
	assembly names directly (system_write_all, and the flag writer_stderr_failed that its logger sets) are not defined on
	a Darwin build, so they are defined here, weakly: where the platform has them, lib.c's win. Here a write is refused,
	and the codec's paths do not reach it.

	Include this once, before modem.c, in a hosted program. The freestanding build never includes it.
*/

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The house names modem.c and tools/modem_rx.c spell their types with, as lib.c declares them: lib.c is compiled apart
   from this program, so its typedefs are not in scope here. */
typedef unsigned char p8;
typedef unsigned short p16;
typedef unsigned int p32;
typedef unsigned long long p64;
typedef signed char b8;
typedef signed short b16;
typedef signed int b32;
typedef signed long long b64;
typedef size_t sized;
#define null ((void *)0)

/* The lib.c helpers the codec and the receiver call. The program links lib.c's object with only sha256_blocks and
   hash_hmac_sha256_prepared global (see the lanes in test/run and tools/modem_rx.c), so these are defined here, as libc's.
   memory_copy is overlap-safe, as memmove is: the receiver's window shift depends on it. */
static inline void memory_copy(void *destination, const void *source, sized size)
{
	memmove(destination, source, size);
}

static inline void memory_zero(void *destination, sized size)
{
	memset(destination, 0, size);
}

static inline void memory_fill(void *destination, b8 value, sized size)
{
	memset(destination, value, size);
}

static inline b32 string_compare(const void *source, const void *input)
{
	return strcmp(source, input);
}

/* lib.c's SHA-256 block core and HMAC: the state is eight words, the data whole blocks, the answer true when the tag is
   made (the x86_64 body with the SHA extensions) and false, with out untouched, where it is not. */
void sha256_blocks(uint32_t *state, const uint8_t *data, size_t blocks);
unsigned char hash_hmac_sha256_prepared(const uint32_t *inner, const uint32_t *outer, const uint8_t *data, size_t length,
					uint8_t *out);

/* The names lib.c's assembly uses, as the platform's symbols spell them (no leading underscore on Darwin either). */
unsigned char modem_host_writer_stderr_failed __asm__("writer_stderr_failed") __attribute__((weak));
unsigned long long modem_host_system_write_all(unsigned long long handle, const void *data, unsigned long long length)
    __asm__("system_write_all") __attribute__((weak));

unsigned long long modem_host_system_write_all(unsigned long long handle, const void *data, unsigned long long length)
{
	(void)handle;
	(void)data;
	(void)length;
	return (unsigned long long)-1;
}
