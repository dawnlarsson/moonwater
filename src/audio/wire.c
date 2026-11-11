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
        return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | (unsigned long)p[3];
}

static void wire_put_be(unsigned char *to, unsigned long v)
{
        to[0] = (unsigned char)(v >> 24), to[1] = (unsigned char)(v >> 16), to[2] = (unsigned char)(v >> 8), to[3] = (unsigned char)v;
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

static long wire_len(const char *s)
{
        return (long)string_length(s);
}

static void wire_w_raw(struct wire_w *w, const void *data, long n)
{
        if (w->len + n > w->cap)
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

#endif // AUDIO_WIRE_INCLUDED
