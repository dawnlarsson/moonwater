/*
        The tag structure of PulseAudio's native protocol, written and read, once for both ends of the wire: the sound server
        (pulse.c) and its control client (control.c). A writer appends to a buffer of a known size and sets bad when a piece does
        not fit; a reader checks each tag and each length before it takes a value and sets bad on a mismatch, after which every
        read is 0. It needs only the library, so a file that includes it needs no other part of the sound server before it.
*/
#ifndef AUDIO_WIRE_INCLUDED
#define AUDIO_WIRE_INCLUDED

struct wire_w
{
        unsigned char *p;
        long cap, len;
        int bad;
};

struct wire_r
{
        const unsigned char *p;
        long length, at;
        int bad;
};

#ifndef AUDIO_WIRE_ASM_NO_IMPL
#define AUDIO_WIRE_ASM_IMPLEMENTATION
#endif
#include "wire.asm.c"

_Static_assert(sizeof(long) == sizeof(aw_i64), "wire.c requires 64-bit long");
_Static_assert(sizeof(struct wire_w) == sizeof(struct aw_w) &&
               AW_OFFSET(struct wire_w, p)   == AW_OFFSET(struct aw_w, p) &&
               AW_OFFSET(struct wire_w, cap) == AW_OFFSET(struct aw_w, cap) &&
               AW_OFFSET(struct wire_w, len) == AW_OFFSET(struct aw_w, len) &&
               AW_OFFSET(struct wire_w, bad) == AW_OFFSET(struct aw_w, bad),
               "wire_w / aw_w ABI mismatch");
_Static_assert(sizeof(struct wire_r) == sizeof(struct aw_r) &&
               AW_OFFSET(struct wire_r, p)      == AW_OFFSET(struct aw_r, p) &&
               AW_OFFSET(struct wire_r, length) == AW_OFFSET(struct aw_r, length) &&
               AW_OFFSET(struct wire_r, at)     == AW_OFFSET(struct aw_r, at) &&
               AW_OFFSET(struct wire_r, bad)    == AW_OFFSET(struct aw_r, bad),
               "wire_r / aw_r ABI mismatch");

static unsigned long wire_be32(const unsigned char *p)
{
        return (unsigned long)aw_be32(p);
}

static void wire_put_be(unsigned char *to, unsigned long v)
{
        aw_put_be32(to, (aw_u32)v);
}

static int wire_r_tag(struct wire_r *r, unsigned char tag)
{
        return (int)aw_r_tag((struct aw_r *)r, tag);
}

static unsigned long wire_r_u32(struct wire_r *r)
{
        return (unsigned long)aw_r_u32((struct aw_r *)r);
}

static int wire_r_bool(struct wire_r *r)
{
        return (int)aw_r_bool((struct aw_r *)r);
}

static const char *wire_r_string(struct wire_r *r, long limit, int *isnull)
{
        return aw_r_string((struct aw_r *)r, (aw_i64)limit, (aw_i32 *)isnull);
}

static long wire_len(const char *s)
{
        return (long)aw_strlen(s);
}

static void wire_w_raw(struct wire_w *w, const void *data, long n)
{
        aw_w_raw((struct aw_w *)w, data, (aw_i64)n);
}

static void wire_w_byte(struct wire_w *w, unsigned char c)
{
        aw_w_byte((struct aw_w *)w, c);
}

static void wire_w_be32(struct wire_w *w, unsigned long v)
{
        aw_w_be32((struct aw_w *)w, (aw_u32)v);
}

static void wire_w_u32(struct wire_w *w, unsigned long v)
{
        aw_w_tagged32((struct aw_w *)w, 'L', (aw_u32)v);
}

static void wire_w_bool(struct wire_w *w, int v)
{
        aw_w_bool((struct aw_w *)w, v);
}

static void wire_w_string(struct wire_w *w, const char *s)
{
        aw_w_string((struct aw_w *)w, s);
}

static void wire_w_cvolume(struct wire_w *w, const unsigned long *volume, long channels)
{
        aw_w_cvolume((struct aw_w *)w, (const aw_u64 *)volume, (aw_i64)channels);
}

#endif // AUDIO_WIRE_INCLUDED
