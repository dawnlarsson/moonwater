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

static unsigned long wire_be32(const unsigned char *p)
{
#ifdef STANDARD_MODERN_C
        return (unsigned long)network_load_32((const p8 *)p);
#else
        return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | (unsigned long)p[3];
#endif
}

static void wire_put_be(unsigned char *to, unsigned long v)
{
#ifdef STANDARD_MODERN_C
        network_store_32((p8 *)to, (p32)v);
#else
        to[0] = (unsigned char)(v >> 24), to[1] = (unsigned char)(v >> 16), to[2] = (unsigned char)(v >> 8), to[3] = (unsigned char)v;
#endif
}

static int wire_r_tag(struct wire_r *r, unsigned char tag)
{
        if (r->bad || r->at >= r->length || r->p[r->at] != tag)
        {
                r->bad = 1;
                return 0;
        }
        r->at++;
        return 1;
}

static unsigned long wire_r_u32(struct wire_r *r)
{
        unsigned long v;

        if (!wire_r_tag(r, 'L') || r->length - r->at < 4)
        {
                r->bad = 1;
                return 0;
        }
        v = wire_be32(r->p + r->at);
        r->at += 4;
        return v;
}

static int wire_r_bool(struct wire_r *r)
{
        if (r->bad || r->at >= r->length || (r->p[r->at] != '1' && r->p[r->at] != '0'))
        {
                r->bad = 1;
                return 0;
        }
        return r->p[r->at++] == '1';
}

static const char *wire_r_string(struct wire_r *r, long limit, int *isnull)
{
        long start, avail;
        const unsigned char *nul;

        if (isnull)
                *isnull = 0;
        if (r->bad || r->at >= r->length)
        {
                r->bad = 1;
                return 0;
        }
        if (r->p[r->at] == 'N')
        {
                r->at++;
                if (isnull)
                        *isnull = 1;
                return 0;
        }
        if (r->p[r->at] != 't')
        {
                r->bad = 1;
                return 0;
        }
        start = r->at + 1;
        avail = r->length - start;
        if (limit > 0 && avail > limit + 1)
                avail = limit + 1;
        nul = avail > 0 ? (const unsigned char *)memory_first_of((void *)(r->p + start), 0, (unsigned long)avail) : 0;
        if (!nul)
        {
                r->bad = 1;
                return 0;
        }
        r->at = (long)(nul - r->p) + 1;
        return (const char *)(r->p + start);
}

static long wire_len(const char *s)
{
        return (long)string_length(s);
}

static void wire_w_raw(struct wire_w *w, const void *data, long n)
{
        if (w->bad)
                return;
        if (n < 0 || n > w->cap - w->len)
        {
                w->bad = 1;
                return;
        }
        memory_copy_apart(w->p + w->len, data, (unsigned long)n);
        w->len += n;
}

static void wire_w_byte(struct wire_w *w, unsigned char c)
{
        wire_w_raw(w, &c, 1);
}

static void wire_w_be32(struct wire_w *w, unsigned long v)
{
        unsigned char b[4];

        wire_put_be(b, v);
        wire_w_raw(w, b, 4);
}

static void wire_w_u32(struct wire_w *w, unsigned long v)
{
        wire_w_byte(w, 'L');
        wire_w_be32(w, v);
}

static void wire_w_bool(struct wire_w *w, int v)
{
        wire_w_byte(w, v ? '1' : '0');
}

static void wire_w_string(struct wire_w *w, const char *s)
{
        if (!s)
        {
                wire_w_byte(w, 'N');
                return;
        }
        wire_w_byte(w, 't');
        wire_w_raw(w, s, wire_len(s) + 1);
}

static void wire_w_cvolume(struct wire_w *w, const unsigned long *volume, long channels)
{
        wire_w_byte(w, 'v');
        wire_w_byte(w, (unsigned char)channels);
        for (long i = 0; i < channels; i++)
                wire_w_be32(w, volume[i]);
}

#endif // AUDIO_WIRE_INCLUDED
