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
