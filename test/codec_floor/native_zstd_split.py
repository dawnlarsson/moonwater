# The arm64 bodies of zstd_sequences_decode and zstd_sequences_exec, lifted out of src/lib.c and run natively on an
# arm64 Mac over jobs dumped from the serial decoder (tables, literals, stream, history, recorded output): decode, the
# stand-ins patched with the recorded repeat offsets, exec, the bytes against the record and against the fused routine,
# and each timed. usage: python3 test/codec_floor/native_zstd_split.py DUMP...   (jobs: zstd_sequences_run replay files)
from pathlib import Path
import re
import subprocess
import sys
root = Path('artifacts/codec-floor-native'); root.mkdir(parents=True, exist_ok=True)
head = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef uint8_t p8; typedef uint16_t p16; typedef uint32_t p32; typedef uint64_t p64; typedef uint64_t positive;
typedef struct { p16 next; p8 extra, bits; p32 base; } cell;
typedef struct { p8 log, rle, valid, pad; p32 cells_at_8; cell c[512]; } fse;
typedef struct { p8 *window; positive pos, window_size; p8 *lits; positive lit_len; p8 *seq; positive seq_len; fse *ll, *of, *ml; p32 *rep; positive nseq; p8 *output_end; } run_t;
typedef struct { p8 *seq; positive seq_len; fse *ll, *of, *ml; positive nseq; p32 *out; p32 rep[3]; p32 max_off; positive sym_end; p64 sum_ll, sum_ml; } dec_t;
typedef struct { p8 *out, *lits; p32 *recs; positive nseq; } exec_t;
extern long zstd_sequences_run(run_t *); extern long zstd_sequences_decode(dec_t *); extern void zstd_sequences_exec(exec_t *);
void *floor_copy(void *d, void *s, p64 n) __asm__("_memory_copy_apart");
void *floor_copy(void *d, void *s, p64 n) { return memcpy(d, s, n); }
void *floor_fill(void *d, p8 v, p64 n) __asm__("_memory_fill");
void *floor_fill(void *d, p8 v, p64 n) { return memset(d, v, n); }
static inline p64 now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
#define BASE 0xFFF00000u
static int resolve(p32 *v, const p32 *rep) { p32 x = *v; if (x < BASE) return 1; p32 k = (x - BASE) >> 16, d = BASE + (k << 16) + 0xFFFFu - x; if (k > 2 || rep[k] <= d) return 0; *v = rep[k] - d; return 1; }
int main(int argc, char **argv)
{
    int jobs = 0, bad = 0; double t_run = 0, t_dec = 0, t_exec = 0; unsigned long seqs = 0, bytes = 0;
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); if (!f) { perror(argv[a]); return 2; }
        p64 h[16]; if (fread(h, 8, 16, f) != 16) return 2;
        positive hist = h[1], wsize = h[2], lit_len = h[3], seq_len = h[4], nseq = h[5], out_len = h[6], blim = h[7];
        p32 before[3] = { (p32)h[8], (p32)(h[8] >> 32), (p32)h[9] };
        p32 after[3] = { (p32)(h[9] >> 32), (p32)h[10], (p32)(h[10] >> 32) };
        fse *t[3]; for (int i = 0; i < 3; i++) { t[i] = aligned_alloc(64, (sizeof(fse) + 63) & ~63ul); if (fread(t[i], sizeof(fse), 1, f) != 1) return 2; }
        p8 *lits = calloc(lit_len + 128, 1); if (fread(lits, 1, lit_len, f) != lit_len) return 2;
        p8 *seq = calloc(seq_len + 64, 1); if (fread(seq, 1, seq_len, f) != seq_len) return 2;
        positive cap = hist + blim + 4096;
        p8 *W = aligned_alloc(16384, (cap + 16383) & ~16383ul), *V = aligned_alloc(16384, (cap + 16383) & ~16383ul);
        memset(W, 0xA5, cap); memset(V, 0xA5, cap);
        if (fread(W, 1, hist, f) != hist) return 2; memcpy(V, W, hist);
        p8 *want = malloc(out_len + 1); if (fread(want, 1, out_len, f) != out_len) return 2;
        fclose(f);
        p32 *recs = aligned_alloc(64, (12 * nseq + 128 + 63) & ~63ul);
        double br = 1e30, bd = 1e30, be = 1e30;
        for (int r = 0; r < 5; r++) {
            p32 rep[3] = { before[0], before[1], before[2] };
            run_t j = { V, hist, wsize, lits, lit_len, seq, seq_len, t[0], t[1], t[2], rep, nseq, V + hist + blim };
            p64 t0 = now_ns(); long rc = zstd_sequences_run(&j); p64 t1 = now_ns();
            if (rc || j.pos != hist + out_len || memcmp(V + hist, want, out_len)) { fprintf(stderr, "FUSED MISMATCH %s\n", argv[a]); bad++; break; }
            if (t1 - t0 < br) br = (double)(t1 - t0);
            memset(V + hist, 0xA5, blim);
        }
        dec_t d = { seq, seq_len, t[0], t[1], t[2], nseq, recs, {BASE + 0xFFFF, BASE + 0x10000 + 0xFFFF, BASE + 0x20000 + 0xFFFF}, 0, 0, 0, 0 };
        for (int r = 0; r < 5; r++) {
            d.rep[0] = BASE + 0xFFFF; d.rep[1] = BASE + 0x10000 + 0xFFFF; d.rep[2] = BASE + 0x20000 + 0xFFFF;
            p64 t0 = now_ns(); long rc = zstd_sequences_decode(&d); p64 t1 = now_ns();
            if (rc) { fprintf(stderr, "DECODE FAILED %s\n", argv[a]); bad++; break; }
            if (t1 - t0 < bd) bd = (double)(t1 - t0);
        }
        p32 rep[3] = { d.rep[0], d.rep[1], d.rep[2] };
        for (int k = 0; k < 3; k++) if (!resolve(&rep[k], before)) { fprintf(stderr, "PATCH FAILED %s\n", argv[a]); bad++; }
        for (positive i = 0; i < d.sym_end; i++) if (!resolve(&recs[3 * i + 2], before)) { fprintf(stderr, "PATCH FAILED %s\n", argv[a]); bad++; }
        if (memcmp(rep, after, sizeof rep)) { fprintf(stderr, "REPEAT OFFSETS MISMATCH %s\n", argv[a]); bad++; }
        for (int r = 0; r < 5; r++) {
            exec_t e = { W + hist, lits, recs, nseq };
            memset(W + hist, 0xA5, blim);
            p64 t0 = now_ns(); zstd_sequences_exec(&e); p64 t1 = now_ns();
            memcpy(e.out, e.lits, lit_len - d.sum_ll);
            if (r == 0 && (lit_len + d.sum_ml != out_len || memcmp(W + hist, want, out_len))) { fprintf(stderr, "EXEC MISMATCH %s\n", argv[a]); bad++; break; }
            if (t1 - t0 < be) be = (double)(t1 - t0);
        }
        t_run += br; t_dec += bd; t_exec += be; seqs += nseq; bytes += out_len; jobs++;
        free(t[0]); free(t[1]); free(t[2]); free(lits); free(seq); free(W); free(V); free(want); free(recs);
    }
    printf("%d jobs %s: fused %.3f ns/seq, decode %.3f + exec %.3f = %.3f ns/seq (%lu seqs, %lu bytes)\n", jobs, bad ? "BAD" : "ok",
           t_run / seqs, t_dec / seqs, t_exec / seqs, (t_dec + t_exec) / seqs, seqs, bytes);
    return bad != 0;
}
'''
asm = subprocess.check_output(['python3', 'test/differential.py', '--harness', 'native_extract', 'src/lib.c', 'zstd_bits_open', 'zstd_bits_reload',
                               'zstd_sequences_run', 'zstd_sequences_decode', 'zstd_sequences_exec', 'memory_copy_match'], text=True)
source = Path('src/lib.c').read_text().splitlines()
blocks = []; at = 0
while at < len(source):
    if source[at].startswith(('#define ZSTD_SEQ_ARM64_', '#define ZSTD_DEC_ARM64_')):
        block = [source[at]]
        while block[-1].endswith('\\'):
            at += 1; block.append(source[at])
        blocks.append('\n'.join(block))
    at += 1
macro = '\n'.join(blocks)
macro = re.sub(r'\b(bl|b) ([a-z_][a-z0-9_]*)\b', r'\1 _\2', macro.replace('.L', 'L'))
(root / 'native-zstd-split.c').write_text(macro + '\n' + head + asm)
subprocess.run(['clang', '-O2', str(root / 'native-zstd-split.c'), '-o', str(root / 'native-zstd-split')], check=True)
subprocess.run([str(root / 'native-zstd-split')] + sys.argv[1:], check=True)
