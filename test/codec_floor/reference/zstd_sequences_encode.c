/* Semantic reference for zstd_sequences_encode: the backward loop of zstd's
   sequence bitstream, one field at a time. The sequences are written last
   to first; each sends its offset, match length and literal length states'
   low bits, steps the three states, then sends the literal length, match
   length and offset extra bits. The room check is the routine's only
   liberty: given 16 bytes a sequence and 8 it finishes, given less it may
   stop at a run boundary and answer 1. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;
typedef long i64;

typedef struct
{
        u32 lit;        /* 0 */
        u32 match;      /* 4 */
        u32 off;        /* 8: the offset value, 1 << code or more */
        u8 ll_code;     /* 12 */
        u8 ml_code;     /* 13 */
        u8 of_code;     /* 14 */
        u8 spare;
} seq;

typedef struct
{
        u16 state[512];         /* 0 */
        u32 delta_nb[53];       /* 1024 */
        i64 delta_find[53];     /* 1240 */
        u8 log;                 /* 1664 */
} table;

typedef struct
{
        const seq *seqs;        /* 0 */
        u64 count;              /* 8: sequences left, counted down to 0 */
        const table *of;        /* 16 */
        const table *ml;        /* 24 */
        const table *ll;        /* 32 */
        u64 of_state;           /* 40 */
        u64 ml_state;           /* 48 */
        u64 ll_state;           /* 56 */
        u64 acc;                /* 64: the pending bits, under 1 << bits */
        u64 bits;               /* 72: under 8 */
        u8 *out;                /* 80 */
        u8 *limit;              /* 88 */
} sequences_job;

extern const u32 ll_base[36], ml_base[53];
extern const u8 ll_extra[36], ml_extra[53];

static void put(u8 *out, u64 *at, u64 value, u64 n)
{
        for (u64 i = 0; i < n; i++, (*at)++)
        {
                u64 byte = *at / 8, bit = *at % 8;
                out[byte] = (u8)((out[byte] & ~(1u << bit)) | (((value >> i) & 1) << bit));
        }
}

/* The call that is given room for all of them. */
long zstd_sequences_encode(sequences_job *j)
{
        u8 *out = j->out;
        u64 at = 0;
        u64 state[3] = {j->of_state, j->ml_state, j->ll_state};
        const table *t[3] = {j->of, j->ml, j->ll};

        out[0] = 0;
        put(out, &at, j->acc, j->bits);
        while (j->count)
        {
                const seq *s = j->seqs + --j->count;
                u8 code[3] = {s->of_code, s->ml_code, s->ll_code};

                for (int k = 0; k < 3; k++)
                {
                        u64 nb = (u32)(state[k] + t[k]->delta_nb[code[k]]) >> 16;

                        put(out, &at, state[k], nb);
                        state[k] = t[k]->state[(i64)(state[k] >> nb) + t[k]->delta_find[code[k]]];
                }
                put(out, &at, s->lit - ll_base[s->ll_code], ll_extra[s->ll_code]);
                put(out, &at, s->match - ml_base[s->ml_code], ml_extra[s->ml_code]);
                put(out, &at, s->off - ((u32)1 << s->of_code), s->of_code);
        }
        j->of_state = state[0];
        j->ml_state = state[1];
        j->ll_state = state[2];
        j->out = out + at / 8;
        j->bits = at % 8;
        j->acc = at % 8 ? out[at / 8] & ((1u << (at % 8)) - 1) : 0;
        return 0;
}
