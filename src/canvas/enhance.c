/*
        The wallpaper enhancer: small learned networks a wallpaper goes through once, before it is cached at each
        screen's size. restore takes the decoded picture's JPEG blocking, ringing and banding out and puts back a touch
        of the detail the quantizer took; upscale doubles a picture smaller than the screen. Each is a residual
        network of 3x3 convolutions (lib.c's conv3x3_f32), a few tens of thousands of weights, deterministic: the same
        picture and the same weights give the same bytes on every machine of one architecture.

        The weights are not in this tree. They are one file, ENHANCE_MODEL_NAME, made and proven against torch in the
        owner's model repository and served by the feed's host at ENHANCE_MODEL_URL; its size and SHA-256 are pinned
        here, because code and weights must match, and a program that needs it fetches it once through the feed's
        client (mwfd_fetch_asset checks both) into ENHANCE_MODEL_PATH. No file, a file that does not check, or a
        machine offline: the stage is skipped and the picture goes on without it. Nothing that draws ever waits for the
        network.

        THE FILE (all little-endian):

                0   8   "MWENHNC1"
                8   4   the version, 1
                12  4   the number of networks
                        then each network: its kind (1 restore, 2 upscale, 3 grade), its pixel unshuffle, its upscale,
                        its layer count, and each layer's in channels, out channels and form (0 a 3x3 convolution, 1
                        one with a ReLU, 2 a dense layer with a ReLU, 3 a dense layer), each a 32-bit word
                        then each network's layers in turn, each its weights (a convolution's 9 * in * out floats,
                        [dy][dx][in][out]; a dense layer's in * out, [in][out]) and its bias (out floats), IEEE single

        A file is read as hostile: the header must equal, word for word, the architecture this code was written for
        (enhance_shapes), so every size is known before anything is read past the header and nothing is allocated from
        a number the file gave; the byte count must be exactly what that architecture takes; every float must be
        finite. Anything else refuses the file, which the loader then deletes so that the next fetch takes it again.

        THE NETWORKS. A picture in 0xaarrggbb words. restore works at half the resolution: each 2x2 quad of pixels is
        one position of 16 channels (the four pixels' blue, green, red and alpha bytes in memory order, the alpha's
        weights zero), so every convolution runs on a quarter of the positions; its last layer gives 16 channels again,
        each colour byte's residual, which picture_quads_add_f32 adds back, clamps and rounds. upscale works at the
        input's resolution with 4 channels a pixel and gives 16 a pixel, the 2x2 quad it becomes, added to the pixel
        repeated. ReLU between layers, zero padding at the picture's edges, as torch's Conv2d(padding=1) pads.

        HOW IT RUNS. The picture is cut into bands of ENHANCE_BAND positions, claimed one at a time by the caller and
        threads of the enhancer's own (not the process's pool: a compositor's frame withdraws a pool run only when every
        worker is idle, so a band of tens of milliseconds in the pool would hold a frame); a band computes
        each layer over its own columns and, for the layers before the last, as many more each side as the layers
        after need (a layer fewer each layer down), so the bands share nothing. Down the band each layer keeps a ring of
        its last three input rows: the input row t is converted, then layer l makes its output row t - 1 - l from rows
        t - 2 - l to t - l of its input, so every row of every layer is made once and the working set of a band of 120
        positions with 32 channels is about 300 KB, a core's L2.
*/
#ifndef ENHANCE_C
#define ENHANCE_C

#define ENHANCE_MAGIC "MWENHNC1"
#define ENHANCE_VERSION 1
#define ENHANCE_LAYERS_MOST 16
#define ENHANCE_CHANNELS_MOST 64
#define ENHANCE_BAND 120
#define ENHANCE_THREADS_MOST 64
#define ENHANCE_RESTORE 1
#define ENHANCE_UPSCALE 2
#define ENHANCE_GRADE 3
#define ENHANCE_NETS_MOST 3

typedef struct
{
        const f32 address_to weights;
        const f32 address_to bias;
        p32 in, out, relu;
} enhance_layer;

typedef struct
{
        enhance_layer layers[ENHANCE_LAYERS_MOST];
        p32 kind, shuffle, up, count;
} enhance_net;

typedef struct
{
        enhance_net nets[ENHANCE_NETS_MOST];
        p32 count;
        p8 address_to file; // the file's bytes, which the layers point into
        positive file_room;
} enhance_model;

/*
        The architecture this code runs: per network its kind, unshuffle, upscale and layers (in, out, ReLU). A file
        whose header says anything else is refused.
*/
typedef struct
{
        p32 kind, shuffle, up, count;
        p32 layers[ENHANCE_LAYERS_MOST][3];
} enhance_shape;

#ifndef ENHANCE_SHAPES
static const enhance_shape enhance_shapes[] = {
    {ENHANCE_RESTORE, 2, 1, 6, {{16, 32, 1}, {32, 32, 1}, {32, 32, 1}, {32, 32, 1}, {32, 32, 1}, {32, 16, 0}}},
    {ENHANCE_GRADE, 1, 1, 6, {{4, 16, 1}, {16, 24, 1}, {24, 24, 1}, {24, 24, 1}, {24, 32, 2}, {32, 6, 3}}},
};
#define ENHANCE_SHAPES (sizeof(enhance_shapes) / sizeof(enhance_shapes[0]))
#endif

static p32 enhance_word(const p8 address_to at)
{
        return (p32)at[0] | (p32)at[1] << 8 | (p32)at[2] << 16 | (p32)at[3] << 24;
}

// The bytes a file of the shapes takes: its header and every layer's floats.
static positive enhance_file_bytes(const enhance_shape address_to shapes, positive count)
{
        positive bytes = 16;

        for (positive n = 0; n < count; n++)
        {
                bytes += 16 + 12 * (positive)shapes[n].count;
                for (p32 l = 0; l < shapes[n].count; l++)
                        bytes += 4 * ((shapes[n].layers[l][2] >= 2 ? 1 : 9) * (positive)shapes[n].layers[l][0] * shapes[n].layers[l][1] +
                                      shapes[n].layers[l][1]);
        }
        return bytes;
}

/*
        enhance_parse: the file's size bytes against the shapes; on success m's layers point into file, which must stay
        as long as m is used and be 4-byte aligned. False for anything but the exact architecture and finite floats.
*/
static bool enhance_parse(const p8 address_to file, positive size, const enhance_shape address_to shapes,
                          positive count, enhance_model address_to m)
{
        positive at = 16, floats_at;

        memory_fill(m, 0, sizeof(*m));
        if (!file || ((positive)file & 3) || count == 0 || count > ENHANCE_NETS_MOST || size != enhance_file_bytes(shapes, count) ||
            memory_compare(file, ENHANCE_MAGIC, 8) || enhance_word(file + 8) != ENHANCE_VERSION ||
            enhance_word(file + 12) != count)
                return false;
        for (positive n = 0; n < count; n++)
        {
                const enhance_shape address_to s = shapes + n;

                if (s->count == 0 || s->count > ENHANCE_LAYERS_MOST || enhance_word(file + at) != s->kind ||
                    enhance_word(file + at + 4) != s->shuffle || enhance_word(file + at + 8) != s->up ||
                    enhance_word(file + at + 12) != s->count)
                        return false;
                at += 16;
                for (p32 l = 0; l < s->count; l++, at += 12)
                        if (enhance_word(file + at) != s->layers[l][0] || enhance_word(file + at + 4) != s->layers[l][1] ||
                            enhance_word(file + at + 8) != s->layers[l][2] || s->layers[l][0] > ENHANCE_CHANNELS_MOST ||
                            s->layers[l][1] > ENHANCE_CHANNELS_MOST || s->layers[l][1] == 0 || s->layers[l][2] > 3 ||
                            (s->kind != ENHANCE_GRADE && s->layers[l][2] > 1) ||
                            (l && s->layers[l][0] != s->layers[l - 1][1]))
                                return false;
        }
        floats_at = at;
        for (positive i = floats_at; i < size; i += 4)
                if ((enhance_word(file + i) & 0x7f800000u) == 0x7f800000u) // an infinity or a NaN
                        return false;
        for (positive n = 0; n < count; n++)
        {
                const enhance_shape address_to s = shapes + n;
                enhance_net address_to net = m->nets + n;

                net->kind = s->kind, net->shuffle = s->shuffle, net->up = s->up, net->count = s->count;
                for (p32 l = 0; l < s->count; l++)
                {
                        enhance_layer address_to layer = net->layers + l;

                        layer->in = s->layers[l][0], layer->out = s->layers[l][1], layer->relu = s->layers[l][2];
                        layer->weights = (const f32 address_to)(file + at);
                        at += 4 * (layer->relu >= 2 ? 1 : 9) * (positive)layer->in * layer->out;
                        layer->bias = (const f32 address_to)(file + at);
                        at += 4 * (positive)layer->out;
                }
        }
        m->count = (p32)count;
        return at == size;
}

static const enhance_net address_to enhance_find(const enhance_model address_to m, p32 kind)
{
        for (p32 n = 0; m && n < m->count; n++)
                if (m->nets[n].kind == kind)
                        return m->nets + n;
        return null;
}

/*
        One band of one network over a picture. src is the picture the network reads (w by h words, pitch apart), dst
        the one it writes: for restore the same size, for upscale twice it. dst must not overlap src.
*/
typedef struct
{
        const enhance_net address_to net;
        p32 address_to dst;
        positive dst_pitch;
        const p32 address_to src;
        positive src_pitch;
        positive w, h;   // the network's positions across and down (quads for restore, pixels for upscale)
        positive bands;
        positive next;   // the next band to claim
        b32 failed;
} enhance_run;

static fn enhance_band(address_any context, positive index)
{
        enhance_run address_to run = (enhance_run address_to)context;
        const enhance_net address_to net = run->net;
        positive layers = net->count, c0 = index * ENHANCE_BAND, c1 = min(c0 + ENHANCE_BAND, run->w);
        positive gw = (c1 - c0) + 2 * layers + 2, widest = 16, room = 0;
        bipolar g0 = (bipolar)c0 - (bipolar)layers - 1; // the picture column of a ring row's first float
        f32 address_to rings[ENHANCE_LAYERS_MOST], address_to zero, address_to last, address_to block;

        for (p32 l = 0; l < layers; l++)
        {
                room += 3 * gw * net->layers[l].in;
                widest = net->layers[l].in > widest ? net->layers[l].in : widest;
        }
        room += gw * widest + (c1 - c0) * 16;
        if (!(block = (f32 address_to)memory_checked(room * 4)))
        {
                run->failed = 1;
                return;
        }
        positive used = 0;

        for (p32 l = 0; l < layers; l++)
                rings[l] = block + used, used += 3 * gw * net->layers[l].in;
        zero = block + used, used += gw * widest;
        last = block + used;
        for (positive t = 0; t < run->h + layers; t++)
        {
                if (t < run->h)
                {
                        positive a = c0 > layers ? c0 - layers : 0, b = min(c1 + layers, run->w);
                        f32 address_to into = rings[0] + (t % 3) * gw * net->layers[0].in + (positive)((bipolar)a - g0) * net->layers[0].in;

                        if (net->kind == ENHANCE_RESTORE)
                                picture_quads_f32(into, run->src + 2 * t * run->src_pitch + 2 * a,
                                                  run->src + (2 * t + 1) * run->src_pitch + 2 * a, b - a);
                        else
                                u8_to_f32(into, (const p8 address_to)(run->src + t * run->src_pitch + a), 4 * (b - a));
                }
                for (p32 l = 0; l < layers && t >= l + 1; l++)
                {
                        positive y = t - 1 - l, reach = layers - 1 - l;
                        const enhance_layer address_to layer = net->layers + l;
                        const f32 address_to rows[3];

                        if (y >= run->h)
                                continue;
                        positive a = c0 > reach ? c0 - reach : 0, b = min(c1 + reach, run->w);

                        for (int k = 0; k < 3; k++)
                        {
                                bipolar r = (bipolar)y - 1 + k;

                                rows[k] = (r < 0 || r >= (bipolar)run->h ? zero : rings[l] + (positive)(r % 3) * gw * layer->in) +
                                          (positive)((bipolar)a - 1 - g0) * layer->in;
                        }
                        if (l + 1 < layers)
                        {
                                conv3x3_f32(rings[l + 1] + (y % 3) * gw * layer->out + (positive)((bipolar)a - g0) * layer->out,
                                            rows, layer->weights, layer->bias, b - a, layer->in, layer->out, layer->relu);
                                continue;
                        }
                        conv3x3_f32(last, rows, layer->weights, layer->bias, c1 - c0, layer->in, layer->out, layer->relu);
                        p32 address_to top = run->dst + 2 * y * run->dst_pitch + 2 * c0, address_to bottom = top + run->dst_pitch;

                        if (net->kind == ENHANCE_RESTORE)
                        {
                                memory_copy(top, run->src + 2 * y * run->src_pitch + 2 * c0, 8 * (c1 - c0));
                                memory_copy(bottom, run->src + (2 * y + 1) * run->src_pitch + 2 * c0, 8 * (c1 - c0));
                        }
                        else
                                for (positive x = c0; x < c1; x++)
                                {
                                        p32 pixel = run->src[y * run->src_pitch + x];

                                        top[2 * (x - c0)] = top[2 * (x - c0) + 1] = bottom[2 * (x - c0)] = bottom[2 * (x - c0) + 1] = pixel;
                                }
                        picture_quads_add_f32(top, bottom, last, c1 - c0);
                }
        }
        memory_free(block, room * 4);
}

// A thread of the run, the caller's among them: bands claimed in turn until none is left.
static fn enhance_worker(address_any context)
{
        enhance_run address_to run = (enhance_run address_to)context;

        for (;;)
        {
                positive band = atomic_add(&run->next, 1);

                if (band >= run->bands)
                        return;
                enhance_band(run, band);
        }
}

/*
        A network over a picture: restore keeps the size (w by h pixels into dst, w by h), upscale doubles it (dst 2w by
        2h). An odd side is run on a copy with its last row or column repeated, as the trainer pads, and cut back.
        threads is the most threads to run on, the caller's included; 0 takes every processor the process may run on
        (sched_getaffinity), and a machine whose library starts no threads runs every band on the caller. False when
        there is no network, the picture is empty or larger than 32768 a side, or memory ran out; dst is then not to be
        used.
*/
static bool enhance_run_net(const enhance_net address_to net, p32 address_to dst, positive dst_pitch,
                            const p32 address_to src, positive src_pitch, positive w, positive h, positive threads)
{
        enhance_run run;
        p32 address_to even = null, address_to wide = null;
        positive ew = w, eh = h, even_bytes = 0, wide_bytes = 0;
        bool restore;

        if (!net || !w || !h || w > 32768 || h > 32768)
                return false;
        restore = net->kind == ENHANCE_RESTORE;
        if (restore && ((w | h) & 1))
        {
                ew = (w + 1) & ~(positive)1, eh = (h + 1) & ~(positive)1;
                even_bytes = ew * eh * 4, wide_bytes = even_bytes;
                if (!(even = (p32 address_to)memory_checked(even_bytes)) || !(wide = (p32 address_to)memory_checked(wide_bytes)))
                {
                        if (even)
                                memory_free(even, even_bytes);
                        return false;
                }
                for (positive y = 0; y < eh; y++)
                {
                        const p32 address_to row = src + (y < h ? y : h - 1) * src_pitch;

                        memory_copy(even + y * ew, row, 4 * w);
                        if (ew > w)
                                even[y * ew + w] = row[w - 1];
                }
        }
        memory_fill(&run, 0, sizeof(run));
        run.net = net;
        run.src = even ? even : src, run.src_pitch = even ? ew : src_pitch;
        run.dst = wide ? wide : dst, run.dst_pitch = wide ? ew : dst_pitch;
        run.w = restore ? ew / 2 : w, run.h = restore ? eh / 2 : h;
        run.bands = (run.w + ENHANCE_BAND - 1) / ENHANCE_BAND;
        threads = threads ? threads : parallel_width();
        threads = min(min(threads, run.bands), (positive)ENHANCE_THREADS_MOST);
        thread address_to helpers[ENHANCE_THREADS_MOST];
        positive started = 0;

        while (started + 1 < threads && (helpers[started] = thread_start(enhance_worker, &run)))
                started++;
        enhance_worker(&run);
        while (started)
                thread_join(helpers[--started]);
        if (wide)
        {
                if (!run.failed)
                        for (positive y = 0; y < h; y++)
                                memory_copy(dst + y * dst_pitch, wide + y * ew, 4 * w);
                memory_free(wide, wide_bytes), memory_free(even, even_bytes);
        }
        return !run.failed;
}

/*
        THE GRADE. A global, image-adaptive colour grade that cannot add content: six parameters (white balance
        temperature and tint, exposure, a monotone S curve on L*, chroma saturation and vibrance), each a tanh of the
        grade network's output times its reach, predicted from a 64 x 64 thumbnail of the picture (exact integer
        boxes), scaled by the creative licence s (0 to 1), and baked into picture_lut3d_u32's 18-node table, each node
        then held to the bounds: a CIE76 shift of at most 3 + 7 s from its own colour, and at most (8 + 17 s) / 255 of
        any sRGB channel. The trainer (the owner's enhance/grade.py) holds the network to colourfulness, contrast,
        exposure, memory-colour and fidelity indicators; this file runs it. s = 0 is the identity, exactly.
*/
#define ENHANCE_THUMB 64
#define ENHANCE_LUT_NODES (18 * 18 * 18)
static const f64 enhance_reach[6] = {0.5, 0.5, 0.5, 0.35, 0.35, 0.35};

// The thumbnail: box i of rows [i h / 64, (i + 1) h / 64), each channel its box's sum rounded. w and h at least 64.
static fn enhance_thumb(p32 address_to thumb, const p32 address_to src, positive pitch, positive w, positive h)
{
        for (positive i = 0; i < ENHANCE_THUMB; i++)
        {
                positive y0 = i * h / ENHANCE_THUMB, y1 = (i + 1) * h / ENHANCE_THUMB;

                for (positive j = 0; j < ENHANCE_THUMB; j++)
                {
                        positive x0 = j * w / ENHANCE_THUMB, x1 = (j + 1) * w / ENHANCE_THUMB, r = 0, g = 0, b = 0;
                        positive k = (y1 - y0) * (x1 - x0);

                        for (positive y = y0; y < y1; y++)
                                for (positive x = x0; x < x1; x++)
                                {
                                        p32 px = src[y * pitch + x];

                                        r += px >> 16 & 255, g += px >> 8 & 255, b += px & 255;
                                }
                        thumb[i * ENHANCE_THUMB + j] = 0xff000000u | (p32)((r + k / 2) / k) << 16 | (p32)((g + k / 2) / k) << 8 |
                                                       (p32)((b + k / 2) / k);
                }
        }
}

/*
        The grade network on a thumbnail: four 3x3 convolutions with ReLU (each but the first after a 2x2 mean), a
        global mean, a dense layer with ReLU and one more, each output's tanh times its reach. False when memory ran out.
*/
static bool enhance_grade_params(const enhance_net address_to net, const p32 address_to thumb, f64 address_to params)
{
        positive room = (ENHANCE_THUMB + 2) * (ENHANCE_THUMB + 2) * ENHANCE_CHANNELS_MOST * 4;
        f32 address_to padded = (f32 address_to)memory_checked(room), address_to plain = (f32 address_to)memory_checked(room);
        f32 mean[ENHANCE_CHANNELS_MOST], hidden[ENHANCE_CHANNELS_MOST];
        positive side = ENHANCE_THUMB, ch = 4;

        if (!padded || !plain || net->count != 6)
        {
                if (padded)
                        memory_free(padded, room);
                if (plain)
                        memory_free(plain, room);
                return false;
        }
        for (positive y = 0; y < side; y++)
                u8_to_f32(padded + ((y + 1) * (side + 2) + 1) * 4, (const p8 address_to)(thumb + y * side), 4 * side);
        for (p32 l = 0; l < 4; l++)
        {
                const enhance_layer address_to layer = net->layers + l;

                if (l)
                {
                        positive half = side / 2;

                        memory_fill(padded, 0, (half + 2) * (half + 2) * ch * 4);
                        for (positive y = 0; y < half; y++)
                                for (positive x = 0; x < half; x++)
                                        for (positive c = 0; c < ch; c++)
                                        {
                                                const f32 address_to q = plain + ((2 * y) * side + 2 * x) * ch + c;

                                                padded[((y + 1) * (half + 2) + x + 1) * ch + c] = (((q[0] + q[ch]) + q[side * ch]) + q[side * ch + ch]) / 4.0f;
                                        }
                        side = half;
                }
                for (positive y = 0; y < side; y++)
                {
                        const f32 address_to rows[3] = {padded + y * (side + 2) * ch, padded + (y + 1) * (side + 2) * ch,
                                                        padded + (y + 2) * (side + 2) * ch};

                        conv3x3_f32(plain + y * side * layer->out, rows, layer->weights, layer->bias, side, ch, layer->out, 1);
                }
                ch = layer->out;
        }
        for (positive c = 0; c < ch; c++)
        {
                f32 sum = 0;

                for (positive i = 0; i < side * side; i++)
                        sum += plain[i * ch + c];
                mean[c] = sum / (f32)(side * side);
        }
        for (p32 l = 4; l < 6; l++)
        {
                const enhance_layer address_to layer = net->layers + l;
                const f32 address_to in = l == 4 ? mean : hidden;
                f32 out[ENHANCE_CHANNELS_MOST];

                for (positive o = 0; o < layer->out; o++)
                {
                        f32 sum = layer->bias[o];

                        for (positive i = 0; i < layer->in; i++)
                                sum += in[i] * layer->weights[i * layer->out + o];
                        out[o] = l == 4 ? (sum > 0 ? sum : 0) : sum;
                }
                memory_copy(hidden, out, sizeof(out));
        }
        for (positive o = 0; o < 6; o++)
        {
                f64 e = exponential(2.0 * (f64)hidden[o]);

                params[o] = (f64)((f32)(1.0 - 2.0 / (e + 1.0)) * (f32)enhance_reach[o]); // in floats, as the trainer
        }
        memory_free(padded, room), memory_free(plain, room);
        return true;
}

// The colour of grade.py, operation for operation, in double precision.
static f64 enhance_linear(f64 c)
{
        return c <= 0.04045 ? c / 12.92 : power(((c > 0 ? c : 0) + 0.055) / 1.055, 2.4);
}

static f64 enhance_srgb(f64 c)
{
        c = c < 0 ? 0 : c > 1 ? 1 : c;
        return c <= 0.0031308 ? 12.92 * c : 1.055 * power(c > 1e-12 ? c : 1e-12, 1.0 / 2.4) - 0.055;
}

static const f64 enhance_rgb_xyz[3][3] = {{0.4124564, 0.3575761, 0.1804375}, {0.2126729, 0.7151522, 0.0721750},
                                         {0.0193339, 0.1191920, 0.9503041}};
static const f64 enhance_white[3] = {0.95047, 1.0, 1.08883};

static f64 enhance_f(f64 t)
{
        const f64 d = 6.0 / 29.0;

        return t > d * d * d ? power(t > 1e-12 ? t : 1e-12, 1.0 / 3.0) : t / (3 * d * d) + 4.0 / 29.0;
}

static f64 enhance_finv(f64 t)
{
        const f64 d = 6.0 / 29.0;

        return t > d ? t * t * t : 3 * d * d * (t - 4.0 / 29.0);
}

static fn enhance_lab(const f64 address_to lin, f64 address_to lab)
{
        f64 f[3];

        for (int i = 0; i < 3; i++)
                f[i] = enhance_f((lin[0] * enhance_rgb_xyz[i][0] + lin[1] * enhance_rgb_xyz[i][1] + lin[2] * enhance_rgb_xyz[i][2]) /
                                 enhance_white[i]);
        lab[0] = 116 * f[1] - 16, lab[1] = 500 * (f[0] - f[1]), lab[2] = 200 * (f[1] - f[2]);
}

static fn enhance_lab_srgb(const f64 address_to lab, f64 address_to rgb, const f64 address_to inverse)
{
        f64 fy = (lab[0] + 16) / 116, xyz[3] = {enhance_finv(fy + lab[1] / 500) * enhance_white[0], enhance_finv(fy) * enhance_white[1],
                                                enhance_finv(fy - lab[2] / 200) * enhance_white[2]};

        for (int i = 0; i < 3; i++)
                rgb[i] = enhance_srgb(xyz[0] * inverse[3 * i] + xyz[1] * inverse[3 * i + 1] + xyz[2] * inverse[3 * i + 2]);
}

static fn enhance_invert3(const f64 m[3][3], f64 address_to out)
{
        f64 det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                  m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);

        out[0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det, out[1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
        out[2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det, out[3] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det;
        out[4] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det, out[5] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
        out[6] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det, out[7] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
        out[8] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
}

/*
        The table of params at creative licence s, as picture_lut3d_u32 reads it: node (r, g, b), r, g and b its bytes
        15 r, 15 g, 15 b, graded, held to the bounds, its blue, green and red in 128ths of a level.
*/
static fn enhance_bake(const f64 address_to params, f64 s, p16 address_to lut)
{
        f64 inverse[9], p[6], most_shift = 3.0 + 7.0 * s, most_channel = (8.0 + 17.0 * s) / 255.0;

        enhance_invert3(enhance_rgb_xyz, inverse);
        for (int i = 0; i < 6; i++)
                p[i] = params[i] * s;
        f64 g[3] = {exponential_two(0.25 * p[0]), exponential_two(0.15 * p[1]), exponential_two(-0.25 * p[0])};
        f64 norm = 0.2126 * g[0] + 0.7152 * g[1] + 0.0722 * g[2], gain = exponential_two(p[2]);

        for (int r = 0; r < 18; r++)
                for (int gr = 0; gr < 18; gr++)
                        for (int b = 0; b < 18; b++)
                        {
                                f64 rgb[3] = {r * 15 / 255.0, gr * 15 / 255.0, b * 15 / 255.0}, lin[3], plain[3], lab_in[3], lab[3], out[3];

                                for (int c = 0; c < 3; c++)
                                        plain[c] = enhance_linear(rgb[c]), lin[c] = plain[c] * (g[c] / norm) * gain;
                                enhance_lab(plain, lab_in);
                                enhance_lab(lin, lab);
                                f64 l = lab[0] / 100;

                                l = l < 0 ? 0 : l > 1 ? 1 : l;
                                l = l + 2 * p[3] * l * (1 - l) * (2 * l - 1);
                                f64 chroma = square_root(lab[1] * lab[1] + lab[2] * lab[2] + 1e-12), cap = chroma / 60 > 1 ? 1 : chroma / 60;
                                f64 k = (1 + p[4]) * (1 + p[5] * (1 - cap));

                                lab[0] = 100 * l, lab[1] *= k, lab[2] *= k;
                                enhance_lab_srgb(lab, out, inverse);     // the graded colour, as grade() gives it
                                for (int c = 0; c < 3; c++)
                                        out[c] = enhance_linear(out[c]);
                                enhance_lab(out, lab);
                                f64 d0 = lab[0] - lab_in[0], d1 = lab[1] - lab_in[1], d2 = lab[2] - lab_in[2];
                                f64 shift = square_root(d0 * d0 + d1 * d1 + d2 * d2), scale = shift > most_shift ? most_shift / shift : 1.0;

                                lab[0] = lab_in[0] + d0 * scale, lab[1] = lab_in[1] + d1 * scale, lab[2] = lab_in[2] + d2 * scale;
                                enhance_lab_srgb(lab, out, inverse);
                                p16 address_to node = lut + 4 * ((r * 18 + gr) * 18 + b);

                                for (int c = 0; c < 3; c++)
                                {
                                        f64 v = out[c];

                                        v = v > rgb[c] + most_channel ? rgb[c] + most_channel : v < rgb[c] - most_channel ? rgb[c] - most_channel : v;
                                        v = v < 0 ? 0 : v > 1 ? 1 : v;
                                        node[2 - c] = (p16)decimal_nearest(v * 32640);
                                }
                                node[3] = 0;
                        }
}

typedef struct
{
        p32 address_to pixels;
        positive pitch, w, h, bands, next;
        const p16 address_to lut;
} enhance_grade_run;

static fn enhance_grade_worker(address_any context)
{
        enhance_grade_run address_to run = (enhance_grade_run address_to)context;

        for (;;)
        {
                positive band = atomic_add(&run->next, 1);

                if (band >= run->bands)
                        return;
                for (positive y = band * 64; y < run->h && y < band * 64 + 64; y++)
                        picture_lut3d_u32(run->pixels + y * run->pitch, run->w, run->lut);
        }
}

/*
        The grade of a picture in place at creative licence s (0 to 1; 0 changes nothing): its thumbnail, the network's
        parameters, the bounded table, every pixel through it. False, and the picture untouched, when there is no grade
        network, the picture is under 64 a side, or memory ran out.
*/
static bool enhance_grade(const enhance_model address_to m, p32 address_to pixels, positive pitch, positive w, positive h,
                          f64 s, positive threads)
{
        const enhance_net address_to net = enhance_find(m, ENHANCE_GRADE);
        p32 thumb[ENHANCE_THUMB * ENHANCE_THUMB];
        f64 params[6];
        static p16 lut[4 * ENHANCE_LUT_NODES];
        enhance_grade_run run;

        if (!net || w < ENHANCE_THUMB || h < ENHANCE_THUMB || !(s > 0))
                return net && s == 0;
        s = s > 1 ? 1 : s;
        enhance_thumb(thumb, pixels, pitch, w, h);
        if (!enhance_grade_params(net, thumb, params))
                return false;
        enhance_bake(params, s, lut);
        memory_fill(&run, 0, sizeof(run));
        run.pixels = pixels, run.pitch = pitch, run.w = w, run.h = h, run.lut = lut, run.bands = (h + 63) / 64;
        threads = threads ? threads : parallel_width();
        threads = min(min(threads, run.bands), (positive)ENHANCE_THREADS_MOST);
        thread address_to helpers[ENHANCE_THREADS_MOST];
        positive started = 0;

        while (started + 1 < threads && (helpers[started] = thread_start(enhance_grade_worker, &run)))
                started++;
        enhance_grade_worker(&run);
        while (started)
                thread_join(helpers[--started]);
        return true;
}

/*
        THE PIN. The one file this code runs, made in the owner's model repository (enhance/, proven against torch there)
        and served by the feed's host: its name carries the first 16 hex digits of its SHA-256, and its size and digest
        are checked on every load as mwfd_fetch_asset checks them on the fetch. Until a file is published the size is 0
        and no stage runs.
*/
#define ENHANCE_MODEL_NAME "enhancer-0000000000000000.bin"
#define ENHANCE_MODEL_BYTES 0
#define ENHANCE_MODEL_URL "https://db.mw.dawning.dev/mw/v1/models/" ENHANCE_MODEL_NAME
static const p8 enhance_model_sha256[32] = {0};

#if defined(SETTING_WALLPAPER_DIR) && !defined(KERNEL_MODE) && !defined(STANDARD_NO_PLATFORM)
#define ENHANCE_MODEL_DIR SETTING_WALLPAPER_DIR "/models"
#define ENHANCE_MODEL_PATH ENHANCE_MODEL_DIR "/" ENHANCE_MODEL_NAME

static bool enhance_model_pinned(const p8 address_to file, positive size)
{
        digest_state digest;
        p8 sum[32];

        if (!ENHANCE_MODEL_BYTES || size != ENHANCE_MODEL_BYTES)
                return false;
        digest_open(&digest, DIGEST_SHA256, 32);
        digest_write(&digest, file, size);
        digest_close(&digest, sum);
        return !memory_compare(sum, enhance_model_sha256, 32);
}

/*
        The pinned model from ENHANCE_MODEL_PATH: a regular file, not a link, exactly ENHANCE_MODEL_BYTES, the pinned
        SHA-256, and the parser's architecture. Anything else at the name is deleted, so the next fetch takes the file
        again; false then, and the stage is skipped. m->file is the model's to free with enhance_model_drop.
*/
static bool enhance_model_load(enhance_model address_to m)
{
        positive room = (ENHANCE_MODEL_BYTES + 2 + 4095) & ~(positive)4095;
        p8 address_to file;
        bipolar got;

        memory_fill(m, 0, sizeof(*m));
        if (!ENHANCE_MODEL_BYTES || !(file = (p8 address_to)memory_checked(room)))
                return false;
        got = file_slurp_regular_at(AT_FDCWD, (string_address)ENHANCE_MODEL_PATH, file, ENHANCE_MODEL_BYTES + 2, O_NOFOLLOW);
        if (got == (bipolar)ENHANCE_MODEL_BYTES && enhance_model_pinned(file, (positive)got) &&
            enhance_parse(file, (positive)got, enhance_shapes, ENHANCE_SHAPES, m))
        {
                m->file = file, m->file_room = room;
                return true;
        }
        memory_free(file, room);
        if (got >= 0)
                system_remove_at(AT_FDCWD, ENHANCE_MODEL_PATH, 0);
        return false;
}

static fn enhance_model_drop(enhance_model address_to m)
{
        if (m->file)
                memory_free(m->file, m->file_room);
        memory_fill(m, 0, sizeof(*m));
}


/*
        THE WALLPAPER'S STAGE (src/canvas/compositor/present.c, compositor_wall_picture). ENHANCE_WALLPAPER says whether
        it runs; SETTING_WALLPAPER_DIR "/enhance" may say otherwise, "off" or the creative licence in percent (0 grades
        nothing; the restore and the doubling still run). No model, no stage: the path is then what it was without it.
        The model is loaded for each wallpaper made (a few hundred KB read and hashed, a millisecond) and dropped after.
*/
#define ENHANCE_WALLPAPER 0 // on once the published model is measured to win
#define ENHANCE_LICENCE 40  // percent

static bipolar enhance_wallpaper_licence(void)
{
        p8 word[16];
        bipolar got = file_slurp_regular_at(AT_FDCWD, (string_address)SETTING_WALLPAPER_DIR "/enhance", word, sizeof(word), O_NOFOLLOW);
        bipolar n = 0;

        if (got <= 0)
                return ENHANCE_WALLPAPER ? ENHANCE_LICENCE : -1;
        if (got >= 3 && !memory_compare(word, "off", 3))
                return -1;
        for (bipolar i = 0; i < got && word[i] >= '0' && word[i] <= '9' && n <= 100; i++)
                n = n * 10 + (word[i] - '0');
        return n > 100 ? 100 : n;
}

// The model, when the stage is to run: *licence the percent, or false and nothing to drop.
static bool enhance_wallpaper_begin(enhance_model address_to m, bipolar address_to licence)
{
        *licence = enhance_wallpaper_licence();
        memory_fill(m, 0, sizeof(*m));
        return *licence >= 0 && enhance_model_load(m);
}

/*
        The picture the decoder made (w by h words, memory_checked's), restored, and doubled while it is smaller than
        the screen it is to cover (twice at most): the picture to use, *w and *h its size; the one given is freed when
        another replaces it. On any failure the picture so far is kept.
*/
static p32 address_to enhance_wallpaper_picture(const enhance_model address_to m, p32 address_to picture, int address_to w,
                                               int address_to h, int screen_w, int screen_h)
{
        const enhance_net address_to restore = enhance_find(m, ENHANCE_RESTORE), address_to up = enhance_find(m, ENHANCE_UPSCALE);
        positive pw = (positive)*w, ph = (positive)*h;

        if (restore && pw * ph <= 64 * 1024 * 1024)
        {
                p32 address_to made = (p32 address_to)memory_checked(pw * ph * 4);

                if (made && enhance_run_net(restore, made, pw, picture, pw, pw, ph, 0))
                        memory_free(picture, pw * ph * 4), picture = made;
                else if (made)
                        memory_free(made, pw * ph * 4);
        }
        for (int times = 0; up && times < 2 && (pw * (positive)screen_h < (positive)screen_w * ph ? pw < (positive)screen_w : ph < (positive)screen_h) &&
                            pw * ph * 4 <= 64 * 1024 * 1024; times++)
        {
                p32 address_to made = (p32 address_to)memory_checked(pw * ph * 16);

                if (!made || !enhance_run_net(up, made, 2 * pw, picture, pw, pw, ph, 0))
                {
                        if (made)
                                memory_free(made, pw * ph * 16);
                        break;
                }
                memory_free(picture, pw * ph * 4), picture = made, pw *= 2, ph *= 2;
        }
        *w = (int)pw, *h = (int)ph;
        return picture;
}

#if defined(STANDARD_MODERN_C_NET_APART)
/*
        The model fetched once, where a program that may use the network already fetches a wallpaper (never from
        anything that draws): nothing when the file there loads; otherwise the feed's client fetches it (its size and
        SHA-256 checked before a byte is kept), and it is written beside its name and renamed over it. 0 when the model
        is there afterwards, else the failure.
*/
static bipolar enhance_model_fetch(void)
{
        enhance_model m;
        http_buffer body;
        bipolar status, handle;

        if (!ENHANCE_MODEL_BYTES)
                return -2;
        if (enhance_model_load(&m))
                return enhance_model_drop(&m), 0;
        status = mwfd_fetch_asset((string_address)ENHANCE_MODEL_URL, (p32)ENHANCE_MODEL_BYTES, enhance_model_sha256, &body);
        if (status)
                return status;
        system_make_directory_at(AT_FDCWD, SETTING_WALLPAPER_DIR, 0755);
        system_make_directory_at(AT_FDCWD, ENHANCE_MODEL_DIR, 0755);
        handle = system_open_at_mode(AT_FDCWD, ENHANCE_MODEL_PATH ".part", FILE_WRITE | O_NOFOLLOW | O_CLOEXEC, 0644);
        status = handle < 0 ? handle : system_write_all((positive)handle, body.bytes, body.used) == body.used ? 0 : -5;
        if (handle >= 0)
                system_close(handle);
        if (!status)
                status = system_rename_at(AT_FDCWD, ENHANCE_MODEL_PATH ".part", AT_FDCWD, ENHANCE_MODEL_PATH, 0);
        if (status)
                system_remove_at(AT_FDCWD, ENHANCE_MODEL_PATH ".part", 0);
        byte_store_release(&body);
        return status;
}
#endif
#endif

#endif // ENHANCE_C
