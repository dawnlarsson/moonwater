/*
        The capture engine's video encoder: frames of a share's ring in, the coded stream out as RTP packets, the shape the link
        carries a frame in (src/waterlink/cast.c) and a viewer takes it apart from.

        The coding is VA-API's, which needs Mesa, which is a bowl's: the encoder is ffmpeg run in the Arch bowl, fed raw frames
        on its standard input (0xaarrggbb words, the ring's), coding H.264 on the card with no B-frames, one frame in flight
        (async_depth 1), a constant rate with a buffer of one frame, and sending each coded frame as RTP packets of at most
        CAST_ENCODE_PACKET bytes to a loopback UDP port of this process's: a packet is a slice or a part of one (RFC 6184), and
        the last of a frame has the marker bit, so a frame is known whole the moment its last packet comes, with nothing
        waited for after it (a raw H.264 stream says where a frame ends only when the next begins, a frame later).

        Each frame put is stamped (its present, CLOCK_MONOTONIC ns); the coder keeps the order (no B-frames), so frame n is the
        packets of RTP timestamp n * 1500 (90 kHz at 60 Hz) from the first, and the stamp of the frame a packet is of is the
        stamp of the nth put. What arrives on the port is the coder's alone: the socket is connected to nothing, and a packet
        from any other port than the first one's is dropped.

        Linux only. The includer gives the system call: CAST_CALL(number, six arguments) answering a negative errno, and
        CAST_SYS(name) the call's number.
*/
#ifndef CAST_ENCODE_INCLUDED
#define CAST_ENCODE_INCLUDED

#define CAST_ENCODE_PACKET 1100u    // an RTP packet at most: one Waterlink frame with its head
#define CAST_ENCODE_STAMPS 256u     // frames put and not yet whole, at most: the coder holds one, a full pipe a few
#define CAST_ENCODE_BOWL "/bowls/arch"
#define CAST_ENCODE_FFMPEG "/usr/bin/ffmpeg"
#define CAST_ENCODE_DEVICE "/dev/dri/renderD128"

struct cast_encoder
{
        int pid, in, rtp, log;       // ffmpeg's process, its standard input, the port its packets come to, its error output
        unsigned int width, height;
        unsigned short port, from;   // the port, and the coder's own (the first packet's)
        unsigned long long stamp[CAST_ENCODE_STAMPS];
        unsigned long long put, whole; // frames put, frames whole
        unsigned int first_ts;       // the RTP timestamp of frame 0
        int have_first;
        unsigned long long packets, bytes, dropped;
};

struct cast_encode_sockaddr
{
        unsigned short family, port;
        unsigned int address;
        unsigned char zero[8];
};

static int cast_encode_text(char *to, unsigned long room, unsigned long value)
{
        char digits[24];
        int n = 0, at = 0;

        do
                digits[n++] = (char)('0' + value % 10), value /= 10;
        while (value && n < 20);
        while (n && at < (int)room - 1)
                to[at++] = digits[--n];
        to[at] = 0;
        return at;
}

/*
        The coder started for frames of width x height at rate frames a second and bits a second: 0, or a negative errno (no
        bowl, no ffmpeg, no socket). The process is the bowl's ffmpeg, its standard input a pipe of ours, its output the port.
*/
static long cast_encoder_start(struct cast_encoder *e, unsigned int width, unsigned int height, unsigned int rate, unsigned long bits)
{
        struct cast_encode_sockaddr where = {2, 0, 0x0100007fu, {0}}; // AF_INET, 127.0.0.1
        unsigned int size = sizeof(where);
        int ends[2];
        long fd, pid;
        static char size_word[32], rate_word[16], bits_word[24], buffer_word[24], url[48];
        static char line[1024];
        char *argv[] = {"sh", "-c", line, 0};
        unsigned long at = 0;
        static char *envp[] = {"PATH=/usr/bin:/bin", "LIBVA_MESSAGING_LEVEL=0", 0};

        for (unsigned long b = 0; b < sizeof(*e); b++)
                ((unsigned char *)e)[b] = 0;
        e->pid = e->in = e->rtp = e->log = -1;
        e->width = width, e->height = height;
        if (!width || !height || width > 8192 || height > 8192 || (width & 1) || (height & 1) || !rate || rate > 1000 || !bits)
                return -22;
        fd = CAST_CALL(CAST_SYS(socket), 2, 2 | 0x80000 | 0x800, 0, 0, 0, 0); // AF_INET, SOCK_DGRAM, CLOEXEC, NONBLOCK
        if (fd < 0)
                return fd;
        if (CAST_CALL(CAST_SYS(bind), fd, (long)&where, sizeof(where), 0, 0, 0) < 0 ||
            CAST_CALL(CAST_SYS(getsockname), fd, (long)&where, (long)&size, 0, 0, 0) < 0)
        {
                CAST_CALL(CAST_SYS(close), fd, 0, 0, 0, 0, 0);
                return -98;
        }
        {
                int room = 4 << 20; // a frame's packets come in a burst: room for a few frames of them

                CAST_CALL(CAST_SYS(setsockopt), fd, 1, 8, (long)&room, sizeof(room), 0); // SOL_SOCKET, SO_RCVBUF
        }
        e->rtp = (int)fd, e->port = (unsigned short)(where.port >> 8 | where.port << 8);
        if (CAST_CALL(CAST_SYS(pipe2), (long)ends, 0x80000, 0, 0, 0, 0) < 0) // O_CLOEXEC: the child's end is dup'd to 0
                return -24;
        {
                int at = cast_encode_text(size_word, sizeof(size_word), width);

                size_word[at++] = 'x';
                cast_encode_text(size_word + at, sizeof(size_word) - (unsigned long)at, height);
        }
        cast_encode_text(rate_word, sizeof(rate_word), rate);
        cast_encode_text(bits_word, sizeof(bits_word), bits);
        cast_encode_text(buffer_word, sizeof(buffer_word), bits / rate); // a frame's worth: the rate holds within a frame
        {
                static const char head[] = "rtp://127.0.0.1:";
                int at = 0;

                for (; head[at]; at++)
                        url[at] = head[at];
                at += cast_encode_text(url + at, sizeof(url) - (unsigned long)at, e->port);
                {
                        static const char tail[] = "?pkt_size=1100";

                        for (int k = 0; tail[k] && at < (int)sizeof(url) - 1; k++)
                                url[at++] = tail[k];
                        url[at] = 0;
                }
        }
        {
                char *words[] = {"bowl", CAST_ENCODE_BOWL, CAST_ENCODE_FFMPEG, "-hide_banner", "-loglevel", "error", "-nostdin", "-fflags",
                                 "nobuffer", "-probesize", "32", "-analyzeduration", "0", "-flags", "low_delay", "-threads", "1",
                                 "-filter_threads", "1", "-f", "rawvideo", "-pix_fmt", "bgra", "-video_size", size_word, "-framerate", rate_word, "-i",
                                 "pipe:0", "-vaapi_device", CAST_ENCODE_DEVICE, "-vf", "format=nv12,hwupload", "-c:v", "h264_vaapi", "-bf", "0",
                                 "-g", "600", "-async_depth", "1", "-rc_mode", "CBR", "-b:v", bits_word, "-maxrate", bits_word, "-bufsize",
                                 buffer_word, "-flush_packets", "1", "-muxdelay", "0", "-muxpreload", "0", "-f", "rtp", url, 0};

                //      bowl is a tool of the shell's, not a file: the line is the shell's, every word of it ours (no blank, no quote).
                for (int w = 0; words[w]; w++)
                        for (const char *c = words[w]; at < sizeof(line) - 2;)
                        {
                                if (!*c)
                                {
                                        line[at++] = ' ';
                                        break;
                                }
                                line[at++] = *c++;
                        }
                line[at] = 0;
        }
        pid = CAST_CALL(CAST_SYS(clone), 17, 0, 0, 0, 0, 0); // SIGCHLD: a fork
        if (pid == 0)
        {
                long null = CAST_CALL(CAST_SYS(openat), -100, (long)"/dev/null", 2, 0, 0, 0);

                CAST_CALL(CAST_SYS(dup3), ends[0], 0, 0, 0, 0, 0);
                if (null >= 0)
                        CAST_CALL(CAST_SYS(dup3), null, 1, 0, 0, 0, 0); // the SDP ffmpeg says goes nowhere; errors go where ours do
                CAST_CALL(CAST_SYS(execve), (long)"/bin/sh", (long)argv, (long)envp, 0, 0, 0);
                CAST_CALL(CAST_SYS(exit_group), 127, 0, 0, 0, 0, 0);
        }
        CAST_CALL(CAST_SYS(close), ends[0], 0, 0, 0, 0, 0);
        if (pid < 0)
        {
                CAST_CALL(CAST_SYS(close), ends[1], 0, 0, 0, 0, 0);
                return pid;
        }
        e->pid = (int)pid, e->in = ends[1];
        return 0;
}

// A frame put: its pixels (rows of stride bytes) written whole to the coder, and its stamp kept. 0, or a negative errno.
static long cast_encoder_put(struct cast_encoder *e, const unsigned char *pixels, unsigned long stride, unsigned long long stamp)
{
        unsigned long row = (unsigned long)e->width * 4;

        if (e->in < 0)
                return -32;
        if (e->put - e->whole >= CAST_ENCODE_STAMPS)
                return -11; // the coder is that far behind: the frame is dropped
        for (unsigned int y = 0; y < e->height; y++)
        {
                const unsigned char *at = pixels + (unsigned long)y * stride;
                unsigned long left = row;

                while (left)
                {
                        long wrote = CAST_CALL(CAST_SYS(write), e->in, (long)(at + row - left), (long)left, 0, 0, 0);

                        if (wrote == -4)
                                continue;
                        if (wrote <= 0)
                                return wrote ? wrote : -5;
                        left -= (unsigned long)wrote;
                }
        }
        e->stamp[e->put % CAST_ENCODE_STAMPS] = stamp;
        e->put++;
        return 0;
}

/*
        The next packet the coder sent, into packet (CAST_ENCODE_PACKET bytes of room): its length, 0 when there is none now, or
        a negative errno. *stamp is the stamp of the frame it is of, *last whether it ends that frame (whole is then counted).
*/
static long cast_encoder_take(struct cast_encoder *e, unsigned char *packet, unsigned long long *stamp, int *last)
{
        struct cast_encode_sockaddr from;
        unsigned int size = sizeof(from);
        long got;
        unsigned int ts;
        unsigned long long frame;

        for (;;)
        {
                got = CAST_CALL(CAST_SYS(recvfrom), e->rtp, (long)packet, CAST_ENCODE_PACKET, 0x40, (long)&from, (long)&size); // DONTWAIT
                if (got == -11)
                        return 0;
                if (got < 0)
                        return got;
                if (got < 12 || (packet[0] >> 6) != 2 || from.address != 0x0100007fu)
                {
                        e->dropped++;
                        continue;
                }
                if (!e->from)
                        e->from = from.port;
                if (from.port != e->from)
                {
                        e->dropped++;
                        continue;
                }
                break;
        }
        ts = (unsigned int)packet[4] << 24 | (unsigned int)packet[5] << 16 | (unsigned int)packet[6] << 8 | packet[7];
        if (!e->have_first)
                e->first_ts = ts, e->have_first = 1;
        frame = (unsigned long long)((ts - e->first_ts) / 1500u); // 90 kHz over 60 frames a second
        *stamp = frame < e->put && e->put - frame <= CAST_ENCODE_STAMPS ? e->stamp[frame % CAST_ENCODE_STAMPS] : 0;
        *last = (packet[1] & 0x80) != 0;
        if (*last && frame + 1 > e->whole)
                e->whole = frame + 1;
        e->packets++, e->bytes += (unsigned long long)got;
        return got;
}

static void cast_encoder_stop(struct cast_encoder *e)
{
        if (e->in >= 0)
                CAST_CALL(CAST_SYS(close), e->in, 0, 0, 0, 0, 0);
        if (e->rtp >= 0)
                CAST_CALL(CAST_SYS(close), e->rtp, 0, 0, 0, 0, 0);
        if (e->pid > 0)
        {
                int status;

                CAST_CALL(CAST_SYS(kill), e->pid, 15, 0, 0, 0, 0);
                CAST_CALL(CAST_SYS(wait4), e->pid, (long)&status, 0, 0, 0, 0);
        }
        e->in = e->rtp = -1, e->pid = -1;
}

#endif /* CAST_ENCODE_INCLUDED */
