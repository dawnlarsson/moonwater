from pathlib import Path
import re, subprocess
root=Path('artifacts/codec-floor-native');root.mkdir(parents=True,exist_ok=True)
lib=Path('src/lib.c').read_text(); checks=Path('test/checks.c').read_text();gz=Path('src/sh/gzip.c').read_text();xz=Path('src/sh/xz.c').read_text()
head='''#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
typedef uint8_t p8; typedef uint16_t p16; typedef uint32_t p32; typedef uint64_t p64;
typedef uint64_t positive; typedef int64_t bipolar; typedef int32_t b32;
#define address_to *
#define address_of &
#define fn void
#define null NULL
#define array_count(x) (sizeof(x)/sizeof((x)[0]))
#define memory_fill memset
#define memory_compare memcmp
#define memory_copy memmove
#define memory_copy_apart memcpy
#define memory_free munmap
#define system_failed(p) ((void*)(p)==MAP_FAILED)
static void *memory(size_t n) { return mmap(0,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0); }
#define syscall(n) 0
#define system_call_1(number, a) (-1)
#define system_call_3(no,p,n,prot) mprotect((void*)(p),n,prot)
static int failed, total;
static void check(const char *what, int ok) { total++; if(!ok) { failed++; fprintf(stderr,"FAIL %s\\n",what); } }
static int test_report(void *unused) { printf("%d/%d checks passed on native arm64\\n",total-failed,total); return !!failed; }
extern p32 hash_crc32(p32,void*,positive);
extern p64 hash_crc64(p64,void*,positive);
extern void lzma_range_shift(void*);
extern void lzma_range_encode(void*,void*,positive,positive);
extern bipolar lzma_range_decode(void*,void*,positive);
extern positive huffman_encode_back(void*,void*,positive,void*);
extern void deflate_decode_span(void*);
extern void lzma_decode_span(void*);
extern positive memory_common_prefix(void*,void*,positive);
extern void memory_copy_match(void*,positive,positive);
extern const p32 hash_crc32_tab[2048];
extern const p64 hash_crc64_tab[2048];
p8 cpu_has_pclmul;
void *floor_copy(void *d,void *s,positive n) __asm__("_memory_copy_apart");
void *floor_copy(void *d,void *s,positive n) { return memcpy(d,s,n); }
void *floor_fill(void *d,p8 v,positive n) __asm__("_memory_fill");
void *floor_fill(void *d,p8 v,positive n) { return memset(d,v,n); }

typedef const char *string_address;
#define INLINE __attribute__((always_inline))
#define min(value, input) ((value) > (input) ? (input) : (value))
#define max(value, input) ((value) < (input) ? (input) : (value))
#define address_any void *
#define bits_leading_zeros(x) __builtin_clzll(x)
#define bits_trailing_zeros(x) __builtin_ctzll(x)
/* The parallel runtime, serial: native checks run on one thread. */
#define PARALLEL_SPREAD ((positive)-1)
typedef struct { p8 *bytes; positive room; positive used; } parallel_output;
typedef void (*parallel_emit_job)(void *context, positive index, parallel_output *output);
typedef bool (*parallel_sink)(void *context, positive index, void *data, positive length);
static positive parallel_width(void) { return 1; }
static positive parallel_slot(void) { return 0; }
static positive parallel_slots(void) { return 1; }
static bool parallel_write(parallel_output *o, void *data, positive length)
{
        if (!length) return true;
        if (o->room - o->used < length)
        {
                positive room = o->room ? o->room : 65536;
                while (room - o->used < length) room *= 2;
                p8 *bytes = realloc(o->bytes, room);
                if (!bytes) return false;
                o->bytes = bytes; o->room = room;
        }
        memcpy(o->bytes + o->used, data, length);
        o->used += length;
        return true;
}
static bool parallel_ordered(parallel_emit_job job, parallel_sink sink, void *context,
                             positive count, positive bytes)
{
        parallel_output o = {0};
        bool ok = true;
        (void)bytes;
        for (positive i = 0; i < count && ok; i++)
        {
                o.used = 0;
                job(context, i, &o);
                ok = sink(context, i, o.bytes, o.used);
        }
        free(o.bytes);
        return ok;
}
/* lib.util.c and lib.c helpers xz.c and gzip.c reach, in plain C. */
#define bipolar_max INT64_MAX
#define memory_zero(d, n) memset((d), 0, (n))
static void *memory_checked(positive n) { void *at = memory(n); return at == MAP_FAILED ? NULL : at; }
static positive memory_vli_put(p8 *into, p64 value)
{
        positive used = 0;
        for (; value >= 0x80; value >>= 7) into[used++] = (p8)(value | 0x80);
        into[used++] = (p8)value;
        return used;
}
static positive memory_vli_get(const p8 *bytes, positive length, positive most, p64 *value)
{
        p64 got = 0;
        for (positive at = 0; at < length && at < most && at < 10; at++)
        {
                if (at == 9 && bytes[at] > 1) return 0;
                got |= (p64)(bytes[at] & 0x7f) << (7 * at);
                if (!(bytes[at] & 0x80))
                {
                        if (at && !bytes[at]) return 0;
                        *value = got;
                        return at + 1;
                }
        }
        return 0;
}
static const bool string_set_high[256] = {[128 ... 255] = 1};
static positive string_span_max(const p8 *bytes, positive n, const bool *set)
{
        positive at = 0;
        while (at < n && set[bytes[at]]) at++;
        return at;
}
extern bool huffman_lengths(const p32 *freq, positive n, p8 *length, positive limit);
extern void huffman_codes(const p8 *length, positive n, p32 *table);
extern void zstd_huffman_cells(void *cells, const p8 *weight, positive count, positive max_bits, p32 *first);
extern void zstd_fse_cells(void *cells, const p8 *symbol, positive size, positive log, p16 *next, const p64 *template_cells);
extern void zstd_huffman_codes(p32 *table, const p8 *weight, positive count, positive max_bits);
extern void deflate_tokens_count(void *job);
extern void deflate_tokens_encode(void *job);
static bipolar system_read_retry(positive fd,void *p,positive n) { return read((int)fd,p,n); }
static bipolar system_write_all(positive fd,void *p,positive n) { return write((int)fd,p,n); }

'''
common=Path('src/lib.util.c').read_text()
def lift(start, end):
    a=common.index(start); b=common.index(end, a)
    return common[a:b]+'\n'
head+=lift('#define memory_load_unaligned(type, source)', '#define memory_cast(type, value)')
head+=lift('typedef struct\n{\n        p8 address_to bytes;\n        positive room;\n        positive used;\n} byte_store;', '/* Stable storage owns')
head+=lift('typedef struct\n{\n        bipolar fd;', '/* Read no more than maximum bytes')
head+='#define FLOOR_NATIVE\n'+gz[gz.index('#define GZIP_MAGIC0'):gz.index('static const p8 gzip_len_extra')]
for name in ('gzip_len_extra','gzip_len_base','gzip_dist_extra','gzip_dist_base'):
 head+=re.search(r'static const p(?:8|16) '+name+r'\[.*?};',gz,re.S).group(0)+'\n'
head+=re.search(r'static bipolar gzip_code_space\(.*?\n}\n',gz,re.S).group(0)
head+=re.search(r'typedef struct\n{\n        p8 address_to src;\n        p32 address_to mpos;.*?} gzip_tokens;\n',gz,re.S).group(0)
a=gz.index('#define GZIP_CELL_LITERAL');b=gz.index('/* End of the span kernel contract. */',a);head+=gz[a:b]+'\n'
# xz.c verifies SHA-256 checks through lib.util.c's digest, whose cores
# the floor does not lift; no floor check decodes a SHA-256 stream, so these
# stand-ins only let xz.c compile and abort if one is ever reached.
head+='''#define DIGEST_SHA256 3
typedef struct { p8 unused; } digest_state;
static inline void digest_open(digest_state *d, positive algorithm, positive size) { (void)d; (void)algorithm; (void)size; abort(); }
static inline void digest_write(digest_state *d, void *bytes, positive n) { (void)d; (void)bytes; (void)n; abort(); }
static inline void digest_close(digest_state *d, p8 *out) { (void)d; (void)out; abort(); }
'''
head+='#define XZ_CORE_ONLY\n'+xz+'\n'
a=lib.index('#define ASM_CRC_BASIS(bit)');b=lib.index('__asm__(',a);head+=lib[a:b]
for name in ('hash_crc32_tab','hash_crc64_tab','deflate_symbol_tab'):
 a=lib.index('ASM_RODATA_OBJECT_BEGIN('+name);a=lib.index('\n',a)+1;b=lib.index('    ASM_OBJECT_END('+name,a)
 head+='__asm__(".section __TEXT,__const\\n.globl _'+name+'\\n.p2align 4\\n_'+name+':\\n"\n'+lib[a:b]+'".text\\n");\n'
head+=subprocess.check_output(['python3','test/differential.py','--harness','native_extract','src/lib.c','hash_crc32','hash_crc64','lzma_range_shift','lzma_range_encode','lzma_range_decode','huffman_encode_back','huffman_lengths','huffman_codes','zstd_huffman_cells','zstd_fse_cells','zstd_huffman_codes','deflate_tokens_count','deflate_tokens_encode','deflate_decode_span','lzma_decode_span','memory_common_prefix','memory_copy_match'],text=True)
a=checks.index('static p64 floor_crc(');b=checks.index('#endif\n#ifdef BENCH_compression_floor',a)
body=checks[a:b].replace('#ifdef CHECK_compression_floor','')
# Darwin pages are 16 KiB; the LZMA span check sizes its guards by FLOOR_PAGE.
head+='#define FLOOR_PAGE ((positive)getpagesize())\n#define FLOOR_PAGE_STATIC 16384\n'
a=body.index('static p8 address_to floor_pages(');b=body.index('static fn floor_checksums',a)
body=body[:a]+body[a:b].replace('4096','(positive)getpagesize()')+body[b:]
body=body.replace('[8192]', '[8 * 1024]')
body=body.replace('p[4096 + i]','p[getpagesize() + i]').replace('8192','(2 * (positive)getpagesize())')
body=body.replace(': 4096)',': (positive)getpagesize())')
body=body.replace('input + 4096, 0, 4096','input + getpagesize(), 0, getpagesize()')
body=body.replace('positive i = 4096; i < 10 * 4096','positive i = getpagesize(); i < 10 * (positive)getpagesize()')
body=body.replace('10 * 4096','10 * (positive)getpagesize()').replace('3 * 4096','3 * (positive)getpagesize()').replace('3*4096','3*(positive)getpagesize()').replace('11*4096','11*(positive)getpagesize()')
body=body.replace('for (positive i = 0; i < 4096; i++) p[getpagesize() + i]', 'for (positive i = 0; i < (positive)getpagesize(); i++) p[getpagesize() + i]')
(root/'native-arm64.c').write_text(head+body)
subprocess.run(['clang','-O2','-Isrc/sh',str(root/'native-arm64.c'),'-o',str(root/'native-arm64')],check=True)
subprocess.run([str(root/'native-arm64')],check=True)
