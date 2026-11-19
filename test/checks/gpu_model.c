/*
        test/checks/gpu_model.c: the model of the card held to the card and to itself, and the GPU kit's rows drawn on
        it.

        The terminal's program, the kit's frames, the draw list and the GPU model's kit run with the four faces:
        fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf
        fonts/inter/Inter-Regular.ttf fonts/inter/Inter-Bold.ttf.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_gpu_model
#include "../../src/lib.util.c"
#include "../../src/build/gpuasm.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted
#define GPU_MODEL_CARD
#include "../gpu_model.c"
/*
        The model of the card (test/gpu_model.c) held to the card and to itself, on a machine with no card. Its decoder is gpuasm's table
        (the two are compared row by row) and takes every word gpuasm makes and nothing else: every instruction of the six kernels and of
        test/gpu_stress.asm goes there and back unchanged, and words of forms gpuasm does not make are refused with their place. The row that
        matters: gpu.c's self-test, gpu_test_hash, run by gpu.c's own path on the model card, gives GPU_TEST_HASH, the hash a Van Gogh card
        gave for the same picture, whatever the registers nothing writes start as and with v_rcp_f32 and v_sqrt_f32 an ulp off either way;
        and a kernel with one bit of one word flipped does not. A record that reaches outside every buffer its job named is refused with the
        address, and gpu_model_run, the call the workers after this make, draws what the kernel says.
*/

// The instructions of count words decoded and made again: true when every one comes back as the words it was, with their number.
static b32 model_round_trip(const p32 address_to words, unsigned int count, unsigned int address_to instructions, unsigned int address_to stop)
{
        *instructions = 0, *stop = 0;
        gpu_model.code_va = 0;
        for (unsigned int at = 0; at < count;)
        {
                struct gpu_model_insn in;
                p32 again[3];

                memory_zero(&gpu_model.fault, sizeof(gpu_model.fault));
                *stop = at;
                if (gpu_model_decode(words + at, count - at, 4ull * at, &in) || gpu_model_encode(&in, again) != in.words || memory_compare(again, words + at, 4ull * in.words))
                        return false;
                at += in.words, (*instructions)++;
        }
        return true;
}

// One word (and the one after it) refused by the decoder as a form gpuasm does not make, at pc.
static b32 model_refuses(p32 w0, p32 w1)
{
        p32 words[3] = {w0, w1, 0};
        struct gpu_model_insn in;

        memory_zero(&gpu_model.fault, sizeof(gpu_model.fault));
        gpu_model.code_va = 0;
        return gpu_model_decode(words, 3, 0x40, &in) == GPU_MODEL_UNKNOWN && gpu_model.fault.word == w0 && gpu_model.fault.pc == 0x40;
}

static unsigned int model_hash(void)
{
        gpu_model_card_reset();
        return gpu_test_hash();
}

// gpu_scene with the cut at binning off (gpu.c's gpu_cut_off): the drawing the cut is held to.
static b32 model_draw_uncut(positive ops_at, unsigned int n, positive bins_at, positive pic_at, unsigned int pitch, unsigned int w, unsigned int h, unsigned int dx0,
                            unsigned int dy0, unsigned int dx1, unsigned int dy1)
{
        b32 drawn;

        gpu_cut_off = 1, drawn = gpu_model_draw_shipped(ops_at, n, bins_at, pic_at, pitch, w, h, dx0, dy0, dx1, dy1), gpu_cut_off = 0;
        return drawn;
}

// A desktop on 1920 by 1080 as the compositor's records draw it: a ground, then 12 windows, each its picture copied (4) from the card's buffer, its rounded
// line as an opaque box under it and a tone of a shadow, overlapping one another; drawn once without the cut and once with it (cut), the list entries of each
// counted in model_entries, the pictures compared. Answers the pixels that differ (0 when cut is 0: the first drawing is the reference).
static positive model_entries[2];
static unsigned int model_desktop_picture[1920 * 1080];
static positive model_desktop(int cut)
{
        enum { DW = 1920, DH = 1080 };
        positive ops_at = GPU_DATA, bins_at = GPU_DATA + (1 << 20), pic_at = GPU_DATA + (4 << 20), src_at = GPU_DATA + (13 << 20), differ = 0;
        unsigned int address_to ops = (unsigned int address_to)((p8 address_to)gpu.at + ops_at), address_to pic = (unsigned int address_to)((p8 address_to)gpu.at + pic_at);
        unsigned int address_to src = (unsigned int address_to)((p8 address_to)gpu.at + src_at), address_to tab = (unsigned int address_to)((p8 address_to)gpu.at + bins_at);
        unsigned int n = 0, seed = 4242u, entries = 0;

#define MODEL_OP(a, b, c, d, e, f, g, h) (ops[8 * n] = (a), ops[8 * n + 1] = (unsigned int)(b), ops[8 * n + 2] = (unsigned int)(c), ops[8 * n + 3] = (unsigned int)(d), \
                                          ops[8 * n + 4] = (unsigned int)(e), ops[8 * n + 5] = (f), ops[8 * n + 6] = (g), ops[8 * n + 7] = (h), n++)
        for (unsigned int i = 0; i < 1024 * 768; i++)
                seed = seed * 1664525u + 1013904223u, src[i] = seed | 0xff000000u;
        MODEL_OP(0, 0, 0, DW, DH, 0xff203040u, 0, 0);
        for (int k = 0; k < 12; k++)
        {
                int x = 40 + k * 80, y = 20 + k * 34;

                MODEL_OP(1, x - 8, y - 4, x + 908, y + 666, 0x30000000u, 0, 0);
                MODEL_OP(2, (x - 2) * 256, (y - 2) * 256, (x + 902) * 256, (y + 652) * 256, 0xff404040u, 12 * 256, 12 * 256);
                MODEL_OP(4, x, y, x + 900, y + 650, (unsigned int)(GPU_VA + src_at), (unsigned int)((GPU_VA + src_at) >> 32), 1024);
        }
#undef MODEL_OP
        memory_zero(pic, (positive)DW * DH * 4);
        gpu_cut_off = !cut;
        gpu_scene(ops_at, n, bins_at, pic_at, DW, DW, DH, 0, 0, DW, DH);
        gpu_cut_off = 0;
        for (unsigned int c = 0; c < ((DW + 63) >> 6) * ((DH + 63) >> 6); c++)
                entries += tab[2 * c + 1];
        model_entries[cut] = entries;
        if (!cut)
                memory_copy(model_desktop_picture, pic, sizeof(model_desktop_picture));
        else
                for (unsigned int i = 0; i < DW * DH; i++)
                        differ += pic[i] != model_desktop_picture[i];
        return differ;
}

static b32 model_has(const char address_to text, const char address_to words)
{
        return string_find((string_address)text, (string_address)words) != 0;
}

static b32 model_seen(unsigned int handle)
{
        for (unsigned int k = 0; k < gpu_model_seen_count; k++)
                if (gpu_model_seen[k] == handle)
                        return true;
        return false;
}

static unsigned int model_source[64 * 4];
static unsigned int model_mutant_word, model_mutant[sizeof(gpu_k_scene) / 4];

// gpu.asm assembled here by gpuasm, for its labels: a kernel's label's first and last word in gpuasm_words.
static b32 model_asm_ready;
static b32 model_kernel_span(const char address_to name, positive address_to first, positive address_to stop)
{
        for (positive at = 0; model_asm_ready && at < gpuasm_label_count; at++)
                if (gpuasm_labels[at].sized && gpuasm_labels[at].length == string_length((string_address)name) &&
                    !memory_compare(gpuasm_pool + gpuasm_labels[at].name, name, gpuasm_labels[at].length))
                        return *first = gpuasm_labels[at].at, *stop = gpuasm_labels[at].stop, true;
        return false;
}

// The label of gpu.asm a word of a kernel is under (the last one at or before it in the kernel's span), appended to into.
static void model_label_of(positive first, positive stop, positive word, char address_to into, unsigned int room)
{
        positive best = first, length = 0;
        const p8 *name = (const p8 *)"";
        char text[48];

        for (positive at = 0; at < gpuasm_label_count; at++)
                if (gpuasm_labels[at].defined && gpuasm_labels[at].at >= best && gpuasm_labels[at].at <= first + word && gpuasm_labels[at].at < stop)
                        best = gpuasm_labels[at].at, name = gpuasm_pool + gpuasm_labels[at].name, length = gpuasm_labels[at].length;
        length = length < sizeof(text) - 1 ? length : sizeof(text) - 1;
        memory_copy(text, name, length), text[length] = 0;
        string_append_bounded((string_address)into, (string_address)text, room);
}

static void model_say_number(char address_to into, unsigned int room, const char address_to before, positive value)
{
        p8 number[24];

        memory_zero(number, sizeof(number));
        positive_into_string(number, value);
        string_append_bounded((string_address)into, (string_address)before, room);
        string_append_bounded((string_address)into, (string_address)number, room);
}

/*
        What of a kernel the card ran since its reset (gpu_model_card.hits): its instructions that ran and its instructions, and in the log each run
        of instructions that did not, by the label of gpu.asm it begins under, its byte in the kernel's code and its count.
*/
static void model_coverage(const char address_to name, unsigned int k, unsigned int address_to ran, unsigned int address_to of)
{
        const p32 *code = gpu_kernels[k].words + 2;
        unsigned int words = (unsigned int)(gpu_kernels[k].size / 4 - 2), gaps = 0, from = 0, missed = 0;
        positive first = 0, stop = 0;
        b32 labelled = model_kernel_span(name, &first, &stop), in_gap = false;
        static char said[2048];

        *ran = *of = 0, said[0] = 0;
        gpu_model.code_va = 0;
        for (unsigned int at = 0; at <= words;)
        {
                struct gpu_model_insn in;
                b32 hit = true;

                in.words = 1;
                if (at < words && !gpu_model_decode(code + at, words - at, 4ull * at, &in))
                        hit = at < GPU_MODEL_CARD_WORDS && gpu_model_card.hits[k][at], (*of)++, *ran += hit;
                if (!hit && !in_gap)
                        in_gap = true, from = at, missed = 0;
                missed += !hit;
                if (hit && in_gap)
                {
                        in_gap = false, gaps++;
                        string_append_bounded((string_address)said, (string_address) " ", sizeof(said));
                        if (labelled)
                                model_label_of(first, stop, from, said, sizeof(said));
                        model_say_number(said, sizeof(said), "@", 4ull * from);
                        model_say_number(said, sizeof(said), ":", missed);
                }
                if (at == words)
                        break;
                at += in.words;
        }
        string_format(log, "gpu model coverage: %s: %p of %p instructions run; %p runs not (label@byte:instructions):%s\n", name, (positive)*ran, (positive)*of,
                      (positive)gaps, said);
}

// One scene of records over a w by h picture at pic_at (pitch w), its cost: the counts of what the model ran, said per 64 by 64 cell and per pixel.
static b32 model_cost(const char address_to what, const unsigned int address_to records, unsigned int n, unsigned int w, unsigned int h)
{
        positive ops_at = GPU_DATA, bins_at = GPU_DATA + 65536, pic_at = GPU_DATA + (1 << 20), cells = ((w + 63) / 64) * ((h + 63) / 64), pixels = (positive)w * h;
        b32 done;

        memory_copy((p8 address_to)gpu.at + ops_at, records, 32ull * n);
        memory_zero(&gpu_model.fault, sizeof(gpu_model.fault)), memory_zero(&gpu_model.cost, sizeof(gpu_model.cost)); // (the coverage goes on counting)
        done = gpu_scene(ops_at, n, bins_at, pic_at, w, w, h, 0, 0, w, h) && !gpu_model.fault.code;
        string_format(log, "gpu model cost: %s: %p instructions a cell (%p scalar, %p vector, %p vector lanes), %p.%p loads and %p.%p stores of a lane a pixel, %p.%p bytes "
                           "loaded and %p.%p stored a pixel, %p scalar bytes a wave\n",
                      what, gpu_model.cost.instructions / cells, gpu_model.cost.salu / cells, gpu_model.cost.valu / cells, gpu_model.cost.lanes / cells,
                      gpu_model.cost.lane_loads / pixels, gpu_model.cost.lane_loads * 100 / pixels % 100, gpu_model.cost.lane_stores / pixels,
                      gpu_model.cost.lane_stores * 100 / pixels % 100, gpu_model.cost.bytes_loaded / pixels, gpu_model.cost.bytes_loaded * 100 / pixels % 100,
                      gpu_model.cost.bytes_stored / pixels, gpu_model.cost.bytes_stored * 100 / pixels % 100,
                      gpu_model.cost.waves ? gpu_model.cost.scalar_bytes / gpu_model.cost.waves : 0);
        return done;
}

b32 main()
{
        static const struct
        {
                const char *name;
                const unsigned int *words;
                unsigned int size;
        } kernels[] = {{"copy", gpu_k_copy, sizeof(gpu_k_copy)}, {"fill", gpu_k_fill, sizeof(gpu_k_fill)},     {"tone", gpu_k_tone, sizeof(gpu_k_tone)},
                       {"glass", gpu_k_glass, sizeof(gpu_k_glass)}, {"scene", gpu_k_scene, sizeof(gpu_k_scene)}, {"scale", gpu_k_scale, sizeof(gpu_k_scale)},
                       {"image", gpu_k_image, sizeof(gpu_k_image)}, {"nv12", gpu_k_nv12, sizeof(gpu_k_nv12)}};
        unsigned int hash, instructions = 0, stop = 0, total = 0;
        b32 same, all = true;

        // The decoder's table is gpuasm's own, row for row, and its clamps are gpuasm's.
        same = sizeof(gpuasm_ops) / sizeof(gpuasm_ops[0]) == GM_NAMED && GM_IDS == GM_NAMED + 1 && string_equals((string_address)gpu_model_names[GM_NAMED].name, (string_address) "s_nop");
        for (unsigned int k = 0; same && k < GM_NAMED; k++)
                same = string_equals((string_address)gpu_model_names[k].name, gpuasm_ops[k].name) && gpu_model_names[k].form == gpuasm_ops[k].form &&
                       gpu_model_names[k].code == gpuasm_ops[k].code && string_equals((string_address)gpu_model_names[k].shape, gpuasm_ops[k].shape);
        same = same && sizeof(gpu_model_clamps) / sizeof(gpu_model_clamps[0]) == sizeof(gpuasm_clamps) / sizeof(gpuasm_clamps[0]);
        for (unsigned int k = 0; same && k < sizeof(gpuasm_clamps) / sizeof(gpuasm_clamps[0]); k++)
                same = string_equals((string_address)gpu_model_clamps[k], gpuasm_clamps[k]);
        check("gpu model: the decoder's table is gpuasm's, its 94 rows of mnemonic, encoding, opcode and operands and its clamps, and s_nop", same);

        // Every instruction of every kernel gpuasm made (artifacts/gpu_kernels.inc) decodes, and is made again as the words it was.
        for (unsigned int k = 0; k < sizeof(kernels) / sizeof(kernels[0]); k++)
        {
                b32 back = model_round_trip(kernels[k].words + 2, kernels[k].size / 4 - 2, &instructions, &stop);

                if (!back)
                        string_format(log, "  gpu model: %s stops at word %p: %s\n", kernels[k].name, (positive)stop, gpu_model.fault.text);
                all = all && back && (kernels[k].words[0] & ~63u) == 0xe0ac0000u && kernels[k].words[1] == 0x18cu, total += instructions;
        }
        string_format(log, "gpu model: %p instructions of the eight kernels decoded and made again\n", (positive)total);
        check("gpu model: every word of copy, fill, tone, glass, scene, scale, image and nv12 decodes to an instruction of gpuasm's, and goes there and back unchanged", all && total > 1000);
        {
                static p8 text[1 << 20];
                bipolar got = file_slurp((string_address) "test/gpu_stress.asm", text, sizeof(text));

                if (got < 0 || (positive)got + 1 >= sizeof(text))
                        string_format(log, "gpu model: test/gpu_stress.asm round trip NOT RUN -- the file does not read from here\n");
                else
                {
                        text[got] = 0;
                        same = gpuasm_assemble((string_address) "test/gpu_stress.asm", text) && model_round_trip(gpuasm_words, (unsigned int)gpuasm_position, &instructions, &stop);
                        if (!same)
                                string_format(log, "  gpu model: gpu_stress.asm stops at word %p: %s%s\n", (positive)stop, gpu_model.fault.text, gpuasm_message);
                        string_format(log, "gpu model: %p instructions of test/gpu_stress.asm (every form gpuasm takes) decoded and made again\n", (positive)instructions);
                        check("gpu model: every word gpuasm makes of test/gpu_stress.asm, every instruction in every operand form, goes there and back unchanged", same && instructions > 200);
                }
        }

        // Words of forms gpuasm does not make are refused, with the word and its place: an opcode not in the table (s_cmov_b32), SDWA, a VOP3 with neg,
        // a scalar load with glc, a global load with slc, a packed op with op_sel, SOPK, a VOP3 with abs, m0 as a source, a permute with an offset.
        check("gpu model: a word not in gpuasm's table, or in a form gpuasm does not make, is refused with the word and its place",
              model_refuses(0xbe800505u, 0) && model_refuses(0x7e0002f9u, 0x06060000u) && model_refuses(0xd76f0001u, 0x24010c06u) && model_refuses(0xf4090202u, 0xfa000000u) &&
                  model_refuses(0xdc328000u, 0x04080002u) && model_refuses(0xcc0d4805u, 0x1a002d05u) && model_refuses(0xb0000000u, 0) &&
                  model_refuses(0xd76f0101u, 0x04010c06u) && model_refuses(0x7e02027cu, 0) && model_refuses(0xdacc0004u, 0x2d003d2cu));
        {
                static const unsigned int bad[] = {0xe0ac0000u, 0x18cu, 0xbf800000u, 0xbe800505u, 0xbf810000u};
                static unsigned int out[64];
                struct gpu_model_region r = {0x10000, sizeof(out), (p8 address_to)out, GPU_MODEL_READ | GPU_MODEL_WRITE};
                int failed = gpu_model_run(bad, 5, 0x10000, 1, 1, &r, 1);

                string_format(log, "gpu model: %s\n", gpu_model.fault.text);
                check("gpu model: a kernel with an unknown word is refused when the wave reaches it, the word and its byte in the code named",
                      failed == GPU_MODEL_UNKNOWN && gpu_model.fault.word == 0xbe800505u && gpu_model.fault.pc == 4 && model_has(gpu_model.fault.text, "0xbe800505") &&
                          model_has(gpu_model.fault.text, "byte 0x4 of the code"));
        }

        // gpu_model_run, the call P2 makes: fill (op 1) over a w of 100 of two rows of 128, the args block in the same region. Each pixel below w is v; the rest is as it was.
        {
                static unsigned int memory[64 + 2 * 128];
                struct gpu_model_region r = {0x200000000ull, sizeof(memory), (p8 address_to)memory, GPU_MODEL_READ | GPU_MODEL_WRITE};
                struct gpu_args address_to a = (struct gpu_args address_to)memory;
                unsigned int address_to d = memory + 64;
                b32 right = true;
                int failed;

                for (unsigned int i = 0; i < 2 * 128; i++)
                        d[i] = 0x5a5a5a5au;
                a->d = r.va + 256, a->dp = 128, a->w = 100, a->op = 1, a->v = 0xff102030u, a->rows = 2;
                failed = gpu_model_run(gpu_k_fill, sizeof(gpu_k_fill) / 4, r.va, 2, 2, &r, 1);
                for (unsigned int y = 0; y < 2; y++)
                        for (unsigned int x = 0; x < 128; x++)
                                right = right && d[y * 128 + x] == (x < 100 ? 0xff102030u : 0x5a5a5a5au);
                check("gpu model: gpu_model_run runs a kernel of gpuasm's over its grid in the regions it is given (fill: w of 100 pixels in two rows of 128)", !failed && right);
                check("gpu model: its cost says what ran: four waves, the stores of 200 lanes", gpu_model.cost.waves == 4 && gpu_model.cost.lane_stores == 200);
        }

        // nv12 (op 9): a picture of NV12 of 40 by 10 (every luma and chroma byte, 0 to 255, at pitches past the width) scaled by tables to 70 by 13,
        // into XRGB, is compositor_wl_decode_pixel's colour pixel for pixel; the columns past w are left as they were.
        {
                enum { SW = 40, SH = 10, YP = 48, UP = 56, W = 70, H = 13, DP = 80 };
                static p8 memory[256 + YP * SH + UP * SH / 2 + 4 * (W + H) + 4 * DP * H + 64];
                struct gpu_model_region r = {0x300000000ull, sizeof(memory), memory, GPU_MODEL_READ | GPU_MODEL_WRITE};
                struct gpu_args address_to a = (struct gpu_args address_to)memory;
                p8 address_to luma = memory + 256, address_to chroma = luma + YP * SH;
                unsigned int address_to tab = (unsigned int address_to)(chroma + UP * SH / 2), address_to d = tab + W + H;
                unsigned int seed = 0x9e3779b9u, wrong = 0;
                int failed;

                for (unsigned int i = 0; i < YP * SH + UP * SH / 2; i++)
                        seed = seed * 1664525u + 1013904223u, luma[i] = (p8)(i < 256 ? i : seed >> 24);
                for (unsigned int x = 0; x < W; x++)
                        tab[x] = (2 * x + 1) * SW / (2 * W);
                for (unsigned int y = 0; y < H; y++)
                        tab[W + y] = (2 * y + 1) * SH / (2 * H);
                for (unsigned int i = 0; i < DP * H; i++)
                        d[i] = 0x5a5a5a5au;
                a->s = r.va + 256, a->ops = r.va + 256 + YP * SH, a->g = r.va + (positive)((p8 address_to)tab - memory), a->d = r.va + (positive)((p8 address_to)d - memory);
                a->sp = YP, a->gp = UP, a->dp = DP, a->w = W, a->op = 9, a->rows = H;
                failed = gpu_model_run(gpu_k_nv12, sizeof(gpu_k_nv12) / 4, r.va, (W + 63) / 64, H, &r, 1);
                for (unsigned int y = 0; y < H; y++)
                        for (unsigned int x = 0; x < DP; x++)
                        {
                                unsigned int sx = tab[x < W ? x : 0], sy = tab[W + y], want = 0x5a5a5a5au;

                                if (x < W)
                                {
                                        long c = (long)luma[sy * YP + sx] - 16, u = (long)chroma[sy / 2 * UP + (sx & ~1u)] - 128, v = (long)chroma[sy / 2 * UP + (sx & ~1u) + 1] - 128;
                                        long rr = (298 * (c < 0 ? 0 : c) + 409 * v + 128) >> 8, gg = (298 * (c < 0 ? 0 : c) - 100 * u - 208 * v + 128) >> 8,
                                             bb = (298 * (c < 0 ? 0 : c) + 516 * u + 128) >> 8;

                                        rr = rr < 0 ? 0 : rr > 255 ? 255 : rr, gg = gg < 0 ? 0 : gg > 255 ? 255 : gg, bb = bb < 0 ? 0 : bb > 255 ? 255 : bb;
                                        want = 0xff000000u | (unsigned int)rr << 16 | (unsigned int)gg << 8 | (unsigned int)bb;
                                }
                                wrong += d[y * DP + x] != want;
                        }
                string_format(log, "gpu model: nv12, %p pixels made, %p not the processor's, %p instructions run%s%s\n", (positive)gpu_model.cost.lane_stores,
                              (positive)wrong, (positive)gpu_model.cost.instructions, failed ? ": " : "", failed ? gpu_model.fault.text : "");
                check("gpu model: nv12 (op 9) makes XRGB of an NV12 picture scaled by tables, compositor_wl_decode_pixel's colour pixel for pixel (40 by 10 to 70 by 13)",
                      !failed && !wrong && gpu_model.cost.lane_stores == W * H);
        }

        if (!gpu_model_card_start(24 << 20)) // (the cut's desktop of 1920 by 1080 and its pictures, from GPU_DATA to 16 MiB on)
        {
                check("gpu model: the model card's buffer is made", false);
                return test_report(null);
        }

        // The row that matters: the self-test on the model is the card's picture.
        hash = model_hash();
        string_format(log, "gpu model: the self-test's hash is %p (GPU_TEST_HASH %p), %p jobs, %p waves, %p instructions%s%s\n", (positive)hash, (positive)GPU_TEST_HASH,
                      (positive)gpu_model_card.jobs, gpu_model.cost.waves, gpu_model.cost.instructions, gpu_model.fault.code ? ": " : "", gpu_model.fault.text);
        check("gpu model: gpu_test_hash drawn by the model card is GPU_TEST_HASH, the hash an RDNA2 card drew the self-test's two scenes to", hash == GPU_TEST_HASH && !gpu_model.fault.code &&
              gpu_model_card.jobs == 2);
        gpu_model.poison = 0xdeadbeefu, hash = model_hash();
        same = hash == GPU_TEST_HASH && !gpu_model.fault.code;
        gpu_model.poison = 0xffffffffu, hash = model_hash();
        gpu_model.poison = 0;
        check("gpu model: the same with every register nothing writes started as 0xdeadbeef, and as all ones", same && hash == GPU_TEST_HASH && !gpu_model.fault.code);
        same = true;
        for (unsigned int way = 1; way <= 3; way++)
                gpu_model.nudge = way, same = same && model_hash() == GPU_TEST_HASH && !gpu_model.fault.code;
        gpu_model.nudge = 0;
        check("gpu model: GPU_TEST_HASH does NOT pin the last bits of v_rcp_f32 and v_sqrt_f32: the hash is the same with them an ulp up, an ulp down, and either or "
              "neither lane by lane (the box corners correct both to the integer), so the model's correctly rounded ones are the documentation's, not the card's",
              same);
        // Each other behaviour the model takes from the documentation, the other way: one the hash moves with is pinned by the card's picture, one it does not is not.
        {
                static const char *const doubts[GPU_MODEL_DOUBTS] = {"a compare's and a carry's bit of a lane exec has off is 0",
                                                                     "ds_bpermute_b32 from a lane exec has off reads 0",
                                                                     "the float ops round to nearest even (the float mode 0xc0)",
                                                                     "v_cvt_u32_f32 rounds toward zero",
                                                                     "a global dword load at an address not of 4 reads its bytes where they lie",
                                                                     "s_and_saveexec_b64's SCC is the new exec's being nonzero"};

                for (unsigned int k = 0; k < GPU_MODEL_DOUBTS; k++)
                {
                        gpu_model.doubt = 1u << k, hash = model_hash();
                        if (hash != GPU_TEST_HASH || gpu_model.fault.code)
                                string_format(log, "gpu model doubt: %s: pinned by GPU_TEST_HASH (the other way the self-test hashes to %p%s%s)\n", doubts[k], (positive)hash,
                                              gpu_model.fault.code ? ", or the model fails: " : "", gpu_model.fault.text);
                        else
                                string_format(log, "gpu model doubt: %s: NOT pinned by GPU_TEST_HASH (the self-test is the same the other way): the model's is the documentation's\n",
                                              doubts[k]);
                }
                gpu_model.doubt = 0;
                check("gpu model: each behaviour the model takes from the documentation is asked of GPU_TEST_HASH the other way (the log says which it pins), and the model put back is the card's",
                      model_hash() == GPU_TEST_HASH && !gpu_model.fault.code);
        }

        // One bit of one word of the card's copy of the scene kernel flipped (premulv's v_lshrrev_b32 v35, 12, v35 made 13): the model runs it to its end and the hash is another.
        {
                p32 address_to code = (p32 address_to)((p8 address_to)gpu.at + gpu_kernel_at[6]);
                unsigned int words = (unsigned int)(gpu_kernels[6].size - 8) / 4, found = 0;

                gpu_model.code_va = 0;
                for (unsigned int at = 0; at < words && !found;)
                {
                        struct gpu_model_insn in;

                        if (gpu_model_decode(code + at, words - at, 4ull * at, &in))
                                break;
                        if (in.id == gpu_model_find(GM_VOP2, 0x16) && !in.e64 && in.op[0] == 256 + 35 && in.op[1] == 128 + 12 && in.op[2] == 256 + 35)
                                found = at + 1;
                        at += in.words;
                }
                if (found)
                        code[found - 1] ^= 1, hash = model_hash(), code[found - 1] ^= 1, model_mutant_word = found - 1;
                string_format(log, "gpu model: the mutant (word %p of scene, bit 0) hashes to %p\n", (positive)(found - 1), (positive)hash);
                check("gpu model: a scene kernel with one bit of one instruction word flipped runs to its end and its hash is not GPU_TEST_HASH", found && !gpu_model.fault.code && hash != GPU_TEST_HASH);
                check("gpu model: and the kernel put back is GPU_TEST_HASH again", model_hash() == GPU_TEST_HASH);
        }

        // The differential the changes to the scene kernel and to the binning are held to (test/gpu_model/differ.c): the same random scenes drawn by two
        // drawers, here the scene kernel the build made and a staged kernel in its place, compared pixel for pixel. A staged copy of the kernel draws every
        // scene the same; one with the mutant's bit flipped does not, and the first pixel that differs is named.
        {
                struct gpu_model_difference d;
                positive wrong;
                b32 staged;

                gpu_model_card_reset();
                staged = gpu_model_stage(gpu_k_scene, sizeof(gpu_k_scene) / 4);
                wrong = gpu_model_card_differential(gpu_model_draw_shipped, gpu_model_draw_staged, 0x2545f491u, 200, &d);
                string_format(log, "gpu model differential: the scene kernel against a staged copy of it, %p scenes from the seed 0x2545f491, %p records, %p pixels: %p differ, %p failed%s%s\n",
                              (positive)d.scenes, d.records, d.pixels, wrong, (positive)d.failed, d.failed ? ": " : "", gpu_model.fault.text);
                check("gpu model: gpu_model_card_differential draws 200 random scenes of every kind of record with the shipped scene kernel and with a staged copy of it the same, pixel for pixel",
                      staged && !wrong && !d.failed && d.records > 1000);
                {
                        unsigned int ran = 0, of = 0;

                        model_coverage("scene", 6, &ran, &of); // what the differential's scenes run
                }
                memory_copy(model_mutant, gpu_k_scene, sizeof(gpu_k_scene));
                model_mutant[2 + model_mutant_word] ^= 1;
                staged = model_mutant_word && gpu_model_stage(model_mutant, sizeof(model_mutant) / 4);
                wrong = gpu_model_card_differential(gpu_model_draw_shipped, gpu_model_draw_staged, 0x2545f491u, 200, &d);
                string_format(log, "gpu model differential: against the mutant staged, %p pixels differ, the first in scene %p at %p,%p: %p shipped, %p staged\n", wrong, (positive)d.scene,
                              (positive)d.x, (positive)d.y, (positive)d.one, (positive)d.two);
                check("gpu model: and with the mutant staged in its place, pixels differ and the first is named", staged && wrong > 0 && !d.failed);
                gpu_model_card.staged_words = 0;
                // The cut at binning (gpu.c's gpu_record_holds and bins_skip): the same scenes binned without it and with it draw the same, and what the waves
                // ran with each (the model's cost) says what it saved; then over scenes of a desktop's stack, windows whole over one another, where it is meant to.
                {
                        struct gpu_model_cost whole, cut;

                        gpu_model_card_reset();
                        wrong = gpu_model_card_differential(model_draw_uncut, model_draw_uncut, 0x2545f491u, 200, &d);
                        whole = gpu_model.cost;
                        gpu_model_card_reset();
                        wrong = gpu_model_card_differential(gpu_model_draw_shipped, gpu_model_draw_shipped, 0x2545f491u, 200, &d);
                        cut = gpu_model.cost;
                        wrong = gpu_model_card_differential(model_draw_uncut, gpu_model_draw_shipped, 0x2545f491u, 200, &d);
                        string_format(log, "gpu model cut: 200 random scenes binned without the cut and with it, %p records, %p pixels: %p differ, %p failed; the waves ran %p instructions "
                                           "(%p lane loads, %p lane stores) without it and %p (%p, %p) with it\n",
                                      d.records, d.pixels, wrong, (positive)d.failed, whole.instructions / 2, whole.lane_loads / 2, whole.lane_stores / 2, cut.instructions / 2,
                                      cut.lane_loads / 2, cut.lane_stores / 2);
                        check("gpu model: the scenes binned with the cut (a record that covers a cell whole passes over the ones before it there) draw what they draw without it, "
                              "pixel for pixel",
                              !wrong && !d.failed && d.records > 1000);
                        gpu_model_card_reset();
                        model_desktop(0);
                        whole = gpu_model.cost;
                        gpu_model_card_reset();
                        wrong = model_desktop(1);
                        cut = gpu_model.cost;
                        string_format(log, "gpu model cut: a desktop of 12 windows over a ground on 1920 by 1080, list entries %p without the cut and %p with it; the waves ran %p "
                                           "instructions (%p lane loads) without it and %p (%p) with it, the pictures %s\n",
                                      model_entries[0], model_entries[1], whole.instructions, whole.lane_loads, cut.instructions, cut.lane_loads, wrong ? "DIFFER" : "the same");
                        check("gpu model: on a desktop's stack of windows the cut passes over the hidden records, and draws the same picture", !wrong && model_entries[1] < model_entries[0]);
                }
        }

        // Bounds: a record that reaches past every buffer the job named is refused with the address, and no wave past it runs.
        {
                positive ops_at = GPU_DATA, bins_at = GPU_DATA + 4096, pic_at = GPU_DATA + 65536, outside = GPU_VA + gpu.size;
                unsigned int address_to ops = (unsigned int address_to)((p8 address_to)gpu.at + ops_at);
                unsigned int image[8] = {4, 0, 0, 64, 4, (unsigned int)outside, (unsigned int)(outside >> 32), 64};
                unsigned int fill[8] = {0, 0, 0, 64, 4, 0xff00ff00u, 0, 0};

                gpu_model_card_reset(), memory_copy(ops, image, 32);
                gpu_scene(ops_at, 1, bins_at, pic_at, 64, 64, 4, 0, 0, 64, 4);
                string_format(log, "gpu model: %s\n", gpu_model.fault.text);
                check("gpu model: an image record whose picture is past the card's buffer is refused at its first lane, with the address",
                      gpu_model.fault.code == GPU_MODEL_BOUNDS && gpu_model.fault.address == outside && gpu_model.fault.lane == 0 && gpu_model.fault.bytes == 4 &&
                          model_has(gpu_model.fault.text, "outside every buffer the job named"));
                gpu_model_card_reset(), memory_copy(ops, fill, 32);
                gpu_scene(ops_at, 1, bins_at, outside - 3 * 256, 64, 64, 4, 0, 0, 64, 4);
                string_format(log, "gpu model: %s\n", gpu_model.fault.text);
                check("gpu model: a picture whose fourth row is past the buffer: the store of its first lane there is refused with the address",
                      gpu_model.fault.code == GPU_MODEL_BOUNDS && gpu_model.fault.address == outside && gpu_model.fault.lane == 48 && gpu_model.fault.bytes == 16);

                // A map the job does not name (not handed to a record, not used in this tick) is not the job's to read; handed, it is, and the picture is its pixels.
                for (unsigned int i = 0; i < 64 * 4; i++)
                        model_source[i] = 0xff000000u | i * 0x010203u;
                gpu_tick = 100, gpu_map_count = 1;
                gpu_maps[0].from = (positive)model_source, gpu_maps[0].to = (positive)model_source + sizeof(model_source), gpu_maps[0].va = GPU_VA_MAPS, gpu_maps[0].bytes = sizeof(model_source);
                gpu_maps[0].handle = 7, gpu_maps[0].used = 90, gpu_maps[0].written = 0, gpu_maps[0].dma = 0, gpu_maps[0].handed = 0;
                image[5] = (unsigned int)GPU_VA_MAPS, image[6] = (unsigned int)(GPU_VA_MAPS >> 32);
                gpu_model_card_reset(), memory_copy(ops, image, 32);
                gpu_scene(ops_at, 1, bins_at, pic_at, 64, 64, 4, 0, 0, 64, 4);
                check("gpu model: an image record of a map the job's bo list does not name is refused, with the map's address",
                      gpu_model.fault.code == GPU_MODEL_BOUNDS && gpu_model.fault.address == GPU_VA_MAPS && !model_seen(7));
                gpu_model_card_reset(), memory_copy(ops, image, 32);
                same = gpu_va_bytes(model_source, sizeof(model_source)) == GPU_VA_MAPS && gpu_scene(ops_at, 1, bins_at, pic_at, 64, 64, 4, 0, 0, 64, 4) && !gpu_model.fault.code && model_seen(7);
                same = same && !memory_compare((p8 address_to)gpu.at + pic_at, model_source, sizeof(model_source));
                check("gpu model: handed to the record, the map is named and read, and the picture is its pixels", same);
                gpu_model_card_reset(), memory_copy(ops, fill, 32);
                gpu_va_bytes(model_source, sizeof(model_source));
                gpu_scene(ops_at, 1, bins_at, GPU_VA_MAPS, 64, 64, 4, 0, 0, 64, 4);
                check("gpu model: a store into a map bound to be read is refused: the job may read it and not write it",
                      gpu_model.fault.code == GPU_MODEL_BOUNDS && gpu_model.fault.address == GPU_VA_MAPS && model_has(gpu_model.fault.text, "may read and not write"));
                gpu_map_count = 0, gpu_handed_clear();
        }

        // Coverage: which instructions of each kernel the self-test runs (the words GPU_TEST_HASH holds) and which the scenes of every kind below and the
        // copy and scale rows run. gpu.asm is assembled here too, for its labels, and what gpuasm makes of it is the arrays the build wrote.
        {
                static p8 text[1 << 20];
                bipolar got = file_slurp((string_address) "src/canvas/gpu.asm", text, sizeof(text));
                unsigned int ran = 0, of = 0;

                model_asm_ready = false;
                if (got > 0 && (positive)got + 1 < sizeof(text))
                {
                        text[got] = 0;
                        model_asm_ready = gpuasm_assemble((string_address) "src/canvas/gpu.asm", text) &&
                                          string_find((string_address)gpuasm_output, (string_address)"gpu_k_scene") != 0;
                }
                if (model_asm_ready)
                {
                        positive first = 0, stop = 0;

                        same = model_kernel_span("scene", &first, &stop) && stop - first == sizeof(gpu_k_scene) / 4 - 2 &&
                               !memory_compare(gpuasm_words + first, gpu_k_scene + 2, sizeof(gpu_k_scene) - 8);
                        check("gpu model: gpu.asm as gpuasm makes it here is the scene kernel the build wrote (artifacts/gpu_kernels.inc is the source's)", same);
                }
                else
                        string_format(log, "gpu model coverage: the labels of gpu.asm NOT RUN -- src/canvas/gpu.asm does not read or assemble from here\n");
                model_hash();
                model_coverage("scene", 6, &ran, &of);
                string_format(log, "gpu model coverage: the self-test runs %p of the scene kernel's %p instructions (%p percent): the words GPU_TEST_HASH holds\n", (positive)ran,
                              (positive)of, (positive)(100 * ran / (of ? of : 1)));
                check("gpu model: the self-test's coverage of the scene kernel is counted, instruction by instruction", ran > 0 && ran <= of);
        }

        // The cost of each kind of record, and of a scene like a desktop's, on the model: a 256 by 256 picture over a ground, said per 64 by 64 cell.
        {
                enum { W = 256, H = 256 };
                positive src_at = GPU_DATA + (2 << 20), cover_at = GPU_DATA + (3 << 20), tones_at = cover_at + (W + 2) * (H + 2) + 64;
                positive src = GPU_VA + src_at;
                unsigned int address_to pixels = (unsigned int address_to)((p8 address_to)gpu.at + src_at), address_to tones = (unsigned int address_to)((p8 address_to)gpu.at + tones_at);
                p8 address_to cover = (p8 address_to)gpu.at + cover_at;
                unsigned int seed = 2463534242u, ok = 1, ran = 0, of = 0;
                const unsigned int ground[8] = {0, 0, 0, W, H, 0xff203040u, 0, 0};
                unsigned int kinds[][2][8] = {
                    {{1, 0, 0, W, H, 0x80c0a020u, 0, 0}},
                    {{2, 16 * 256, 16 * 256, 240 * 256, 240 * 256, 0xc0ff4020u, 24 * 256, 24 * 256}},
                    {{2, 0, 0, W * 256, H * 256, 0xffff4020u, 0, 0}},
                    {{2 | 1 << 8, 16 * 256, 16 * 256, 240 * 256, 240 * 256, 0xe0ffffffu, 30 * 256, 30 * 256}, {9, 40 * 256, 40 * 256, 216 * 256, 216 * 256, 0, 12 * 256, 12 * 256}},
                    {{3 | (W + 2) << 8, 0, 0, W, H, 0xff102030u, 180 | 64 << 8, (unsigned int)(cover_at + W + 3)}},
                    {{4, 0, 0, W, H, (unsigned int)src, (unsigned int)(src >> 32), W}},
                    {{11, 0, 0, W, H, (unsigned int)src, (unsigned int)(src >> 32), W}},
                    {{5 | (W + 2) << 8 | W << 16, 0, 0, W, H, (unsigned int)(cover_at + W + 3), (unsigned int)src, (unsigned int)(src >> 32)}},
                    {{7 | (0x800000u + W) << 8, 0, 0, W, H, (unsigned int)(src + 4ull * (W - 1)), (unsigned int)(src >> 32), (unsigned int)-1}},
                    {{10 | (W + 2) << 8, 0, 0, W, H, (unsigned int)tones_at, 0, (unsigned int)(cover_at + W + 3)}},
                    {{6 | W << 8 | W << 16, 0, 0, W, H, (unsigned int)(cover_at + W + 3), (unsigned int)src, (unsigned int)(src >> 32 & 0xff) | 0xe0e0e0u << 8}}};
                static const char *const names[] = {"tone", "box with corners", "box whole", "box with a clip", "text run", "image", "image over", "mix", "turn", "translate", "close button"};
                unsigned int records[16 * 8];

                for (unsigned int i = 0; i < W * H; i++)
                        seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5, pixels[i] = seed;
                for (unsigned int i = 0; i < (W + 2) * (H + 2); i++)
                        seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5, cover[i] = (p8)(seed % 3 == 0 ? 0 : seed % 3 == 1 ? 255 : seed >> 24);
                for (unsigned int i = 0; i < 256; i++)
                        seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5, tones[i] = seed | 0xff000000u;
                gpu_model_card_reset();
                ok = ok && model_cost("ground (a fill)", ground, 1, W, H);
                for (unsigned int k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++)
                {
                        unsigned int n = 1 + 1 + (kinds[k][0][0] == (2 | 1 << 8));

                        memory_copy(records, ground, 32), memory_copy(records + 8, kinds[k], 32ull * (n - 1));
                        ok = ok && model_cost(names[k], records, n, W, H);
                }
                check("gpu model: the cost of every kind of record is counted on a scene the model draws to its end", ok);
                model_coverage("scene", 6, &ran, &of);
                string_format(log, "gpu model coverage: the scenes of every kind run %p of the scene kernel's %p instructions\n", (positive)ran, (positive)of);
        }

        // The copy (op 0, wayland_host's) and the scale (op 7, the compositor's landings) on the model against the processor's memory_copy_rect_u32 and
        // memory_resample_u32_rect, from a seed: a differential against the processor only, for no card hash holds these two kernels.
        {
                enum { ROUNDS = 40 };
                positive from_at = GPU_DATA + (2 << 20), into_at = GPU_DATA + (3 << 20), tab_at = GPU_DATA + (1 << 20);
                unsigned int address_to from = (unsigned int address_to)((p8 address_to)gpu.at + from_at), address_to into = (unsigned int address_to)((p8 address_to)gpu.at + into_at);
                unsigned int address_to tables = (unsigned int address_to)((p8 address_to)gpu.at + tab_at);
                static unsigned int want[512 * 512];
                unsigned int seed = 0x9e3779b9u, wrong = 0, ran = 0, of = 0;
                positive pixels = 0;

                gpu_model_card_reset();
                for (unsigned int i = 0; i < 512 * 512; i++)
                        seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5, from[i] = seed;
                for (unsigned int round = 0; round < 2 * ROUNDS; round++)
                {
                        unsigned int w, h, sp, dp, fw, fh;
                        b32 scaled = round >= ROUNDS;

                        seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5, w = 1 + seed % 300;
                        seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5, h = 1 + seed % 200;
                        seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5, fw = 1 + seed % 400, sp = fw + seed % 7;
                        seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5, fh = 1 + seed % 300, dp = w + seed % 5;
                        if (!scaled)
                                fw = w, fh = h, sp = w + sp % 9;
                        for (unsigned int i = 0; i < 512 * 512; i++)
                                into[i] = want[i] = 0x5a5a5a5au;
                        if (scaled)
                        {
                                // The compositor's tables (compositor_gpu_scale_frames): half a cell in, the floored step in 24-bit fractions.
                                positive step_x = ((positive)fw << 24) / w, fx = ((positive)fw << 23) / w, step_y = ((positive)fh << 24) / h, fy = ((positive)fh << 23) / h;

                                for (unsigned int x = 0; x < w; x++, fx += step_x)
                                        tables[x] = (unsigned int)(fx >> 24);
                                for (unsigned int y = 0; y < h; y++, fy += step_y)
                                        tables[w + y] = (unsigned int)(fy >> 24);
                                memory_resample_u32_rect(want, dp, w, h, from, sp, fw, fh, w, h, 0, 0);
                        }
                        else
                                memory_copy_rect_u32(want, dp, from, sp, w, h);
                        if (!gpu_blit(GPU_VA + from_at, sp, GPU_VA + into_at, dp, GPU_VA + tab_at, scaled ? 7 : 0, w, h) || gpu_model.fault.code)
                        {
                                wrong++;
                                break;
                        }
                        for (unsigned int i = 0; i < 512 * 512; i++)
                                wrong += into[i] != want[i];
                        pixels += (positive)w * h;
                }
                string_format(log, "gpu model: copy and scale, %p rounds each from the seed 0x9e3779b9, %p pixels: %p differ from the processor's%s%s\n", (positive)ROUNDS, pixels,
                              (positive)wrong, gpu_model.fault.code ? ": " : "", gpu_model.fault.text);
                check("gpu model: the copy and scale kernels on the model are memory_copy_rect_u32 and memory_resample_u32_rect, pixel for pixel (against the processor only: no card hash holds them)",
                      !wrong && pixels > 0);
                model_coverage("copy", 0, &ran, &of);
                model_coverage("scale", 7, &ran, &of);
        }

        // The texture unit's read (op 8): gpu.c's own test of it (the one a card is held to at its start) passes on the model, every pixel of a picture read
        // through a linear resource is the picture's, straight and with red and blue the other way; a tiled or compressed resource is not the model's to read.
        {
                positive from_at = GPU_DATA + (2 << 20), into_at = GPU_DATA + (3 << 20);
                unsigned int address_to from = (unsigned int address_to)((p8 address_to)gpu.at + from_at), address_to into = (unsigned int address_to)((p8 address_to)gpu.at + into_at);
                static unsigned int want[256 * 64];
                unsigned int desc[8], seed = 0x2545f491u, wrong = 0, ran = 0, of = 0;
                b32 tested, read, refused;

                gpu_model_card_reset();
                tested = gpu_image_test() && !gpu_model.fault.code;
                check("gpu model: gpu_image_test (the texture unit's read a card is held to at its start) passes on the model card", tested);
                gpu_model_card_reset();
                tested = gpu_nv12_test() && !gpu_model.fault.code;
                check("gpu model: gpu_nv12_test (the NV12 kernel held at the card's start to the processor's colour, exactly) passes on the model card", tested);
                gpu_nv12_ok = 0, gpu_nv12_trying = 0;
                check("gpu model: an NV12 picture is not drawn on a card whose start did not pass the NV12 test (the processor's walk makes it)",
                      !gpu_nv12(GPU_VA, 64, GPU_VA, 64, GPU_VA, 64, GPU_VA, 64, 4) && gpu_end == GPU_END_NONE);
                gpu_model_card_reset();
                for (unsigned int i = 0; i < 256 * 64; i++)
                        seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5, from[i] = seed, into[i] = 0x5a5a5a5au;
                read = true;
                for (unsigned int unorm = 0; unorm < 2; unorm++)
                {
                        unsigned int kind = gpu_amd_image(desc, GPU_VA + from_at, 0, 256, 64, 0, false, unorm);

                        read = read && kind && gpu_image(desc, kind, 5, 7, GPU_VA + into_at, 200, 190, 50) && !gpu_model.fault.code;
                        for (unsigned int y = 0; read && y < 50; y++)
                                for (unsigned int x = 0; x < 190; x++)
                                        wrong += into[y * 200 + x] != from[(y + 7) * 256 + x + 5];
                        kind = gpu_amd_image(desc, GPU_VA + from_at, 0, 256, 64, 0, true, unorm);
                        read = read && kind && gpu_image(desc, kind, 60, 0, GPU_VA + into_at, 256, 196, 64) && !gpu_model.fault.code;
                        for (unsigned int y = 0; read && y < 64; y++)
                                for (unsigned int x = 0; x < 196; x++)
                                {
                                        unsigned int c = from[y * 256 + x + 60];

                                        wrong += into[y * 256 + x] != ((c & 0xff00ff00u) | (c >> 16 & 0xff) | (c & 0xff) << 16);
                                }
                }
                check("gpu model: the image kernel reads a linear picture's rectangle through its resource as it is, and as ABGR with red and blue the other way, as 32_UINT's word and as UNORM's channels, pixel for pixel",
                      read && !wrong);
                // Laid over: each pixel the scene's image over of the picture's (premultiplied: the colours held under the alpha) on what was there.
                for (unsigned int i = 0; i < 256 * 64; i++)
                {
                        unsigned int a = from[i] >> 24, c = from[i];

                        from[i] = a << 24 | ((c >> 16 & 0xff) * a / 255) << 16 | ((c >> 8 & 0xff) * a / 255) << 8 | (c & 0xff) * a / 255;
                        seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5, into[i] = seed;
                        want[i] = into[i];
                }
                gpu_model_card_reset();
                read = gpu_amd_image(desc, GPU_VA + from_at, 0, 256, 64, 0, false, false) &&
                       gpu_image(desc, GPU_IMAGE_RAW | GPU_IMAGE_OVER, 9, 2, GPU_VA + into_at, 256, 230, 60) && !gpu_model.fault.code;
                wrong = 0;
                for (unsigned int y = 0; read && y < 64; y++)
                        for (unsigned int x = 0; x < 256; x++)
                                wrong += into[y * 256 + x] != (x < 230 && y < 60 ? gpu_test_overv(want[y * 256 + x], from[(y + 2) * 256 + x + 9]) : want[y * 256 + x]);
                check("gpu model: laid over, the image kernel's pixel is the scene's image over (gpu_test_overv) of the picture on what the picture had, and nothing past the rectangle",
                      read && !wrong);
                model_coverage("image", 8, &ran, &of);
                gpu_model_card_reset();
                refused = gpu_amd_image(desc, GPU_VA + from_at, GPU_VA + into_at, 256, 64, 0x0200000000533b03ull, false, false) == GPU_IMAGE_MADE;
                refused = refused && (gpu_image(desc, GPU_IMAGE_MADE, 0, 0, GPU_VA + into_at, 256, 64, 4), gpu_model.fault.code == GPU_MODEL_UNMODELLED);
                check("gpu model: a resource of a tiled, compressed picture (64K_R_X with DCC) is refused by the model as unmodelled, not read as linear", refused);
                check("gpu model: gpu_amd_image refuses a modifier not AMD's, another tile, a pitch not of whole 64 KiB blocks, DCC with no metadata, and a block of 256 bytes, reads DCC as UNORM and a tiled picture without as 32_UINT",
                      !gpu_amd_image(desc, 0x100000, 0, 1920, 1080, 0x0100000000000001ull, false, false) && !gpu_amd_image(desc, 0x100000, 0, 1920, 1080, 0x0200000000401503ull, false, false) &&
                          !gpu_amd_image(desc, 0x100000, 0, 1900, 1080, 0x0200000000401b03ull, false, false) && !gpu_amd_image(desc, 0x100000, 0, 1920, 1080, 0x0200000000533b03ull, false, false) &&
                          !gpu_amd_image(desc, 0x100000, 0x200000, 1920, 1080, 0x0200000000533b03ull | 2ull << 18, false, false) &&
                          gpu_amd_image(desc, 0x100000, 0x234500, 1920, 1080, 0x0200000000537b03ull, false, false) && desc[6] >> 19 == (1u << 0 | 1u << 2 | 1u << 3 | 0x45u << 5) &&
                          desc[7] == 0x23 && (desc[3] >> 20 & 0x1f) == 27 && (desc[1] >> 20 & 0x1ff) == 56 &&
                          gpu_amd_image(desc, 0x100000, 0, 1920, 1080, 0x0200000000401b03ull, true, false) == (GPU_IMAGE_MADE | GPU_IMAGE_RAW | GPU_IMAGE_SWAP) &&
                          (desc[1] >> 20 & 0x1ff) == 20 && (desc[3] & 0xfff) == 4);
                // 1920 by 1080 in 64K_R_X: 15 blocks a row of blocks, 9 rows of them (1080 up to 1152), 8,847,360 bytes; its DCC keys 34,560.
                check("gpu model: gpu_amd_image_fits holds a tiled picture to its 64 KiB blocks' rows (1080 rows read as 1152), linear to its rows, and DCC's "
                      "metadata to a key byte a 256, past an offset, and refuses one byte short of each",
                      gpu_amd_image_fits(8847360, 0, 0, 1920, 1080, 0x0200000000401b03ull) && !gpu_amd_image_fits(8847359, 0, 0, 1920, 1080, 0x0200000000401b03ull) &&
                          !gpu_amd_image_fits(1920 * 4 * 1080, 0, 0, 1920, 1080, 0x0200000000401b03ull) && gpu_amd_image_fits(1920 * 4 * 1080, 0, 0, 1920, 1080, 0) &&
                          !gpu_amd_image_fits(1920 * 4 * 1080 - 1, 0, 0, 1920, 1080, 0) && gpu_amd_image_fits(8847360 + 4096, 4096, 0, 1920, 1080, 0x0200000000401b03ull) &&
                          !gpu_amd_image_fits(8847360 + 4095, 4096, 0, 1920, 1080, 0x0200000000401b03ull) &&
                          gpu_amd_image_fits(8847360 + 34560, 0, 8847360, 1920, 1080, 0x0200000000537b03ull) &&
                          !gpu_amd_image_fits(8847360 + 34559, 0, 8847360, 1920, 1080, 0x0200000000537b03ull) &&
                          !gpu_amd_image_fits(8847360, 0, 8847360 + 1, 1920, 1080, 0x0200000000537b03ull) && !gpu_amd_image_fits(100, 200, 0, 128, 1, 0));
                // Single-pass tiled/DCC sampling inside the scene kernel (kinds 4/11 and 5/12 with 0x40 tiled, 0x20 UNORM, 0x80 swap):
                // bit-exact against CPU for raw 32_UINT, swapped 32_UINT, UNORM 8_8_8_8, alpha-over, and cover-mixed corners.
                {
                        positive ops_at = GPU_DATA + 65536, desc_at = GPU_DATA + 131072, cov_at = GPU_DATA + 131200, bins_at = GPU_DATA + 196608;
                        unsigned int *ops = (unsigned int *)((p8 *)gpu.at + ops_at);
                        unsigned int *desc_mem = (unsigned int *)((p8 *)gpu.at + desc_at);
                        p8 *cov_mem = (p8 *)gpu.at + cov_at;
                        unsigned int scene_wrong = 0;
                        b32 scene_ok = true;

                        gpu_model_card_reset();
                        for (unsigned int i = 0; i < 256 * 64; i++)
                        {
                                unsigned int a, c;

                                seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
                                a = seed >> 24, c = seed;
                                from[i] = a << 24 | ((c >> 16 & 0xff) * a / 255) << 16 | ((c >> 8 & 0xff) * a / 255) << 8 | (c & 0xff) * a / 255;
                        }
                        for (unsigned int i = 0; i < 16 * 16; i++)
                                cov_mem[i] = (p8)((i * 17u) & 255u);
                        for (unsigned int mode = 0; mode < 3; mode++)
                        {
                                b32 swap = mode == 1, unorm = mode == 2;
                                unsigned int kind = gpu_amd_image(desc_mem, GPU_VA + from_at, 0, 256, 64, 0, swap, unorm);
                                unsigned int flags = 0x40u | (unorm ? 0x20u : 0u) | (swap && !unorm ? 0x80u : 0u);

                                for (unsigned int i = 0; i < 256 * 64; i++)
                                        into[i] = 0xff112233u;
                                // op 0: opaque background fill
                                ops[0] = 0, ops[1] = 0, ops[2] = 0, ops[3] = 256, ops[4] = 64, ops[5] = 0xff112233u, ops[6] = 0, ops[7] = 0;
                                // op 1: single-pass tiled copy (kind 4 | flags) at (12, 8)..(140, 40) from source (5, 7)
                                ops[8] = 4u | flags, ops[9] = 12, ops[10] = 8, ops[11] = 140, ops[12] = 40;
                                ops[13] = (unsigned int)desc_at, ops[14] = 5u | (7u << 16), ops[15] = 0;
                                // op 2: single-pass tiled alpha-over (kind 11 | flags) at (148, 4)..(220, 44) from source (10, 3)
                                ops[16] = 11u | flags, ops[17] = 148, ops[18] = 4, ops[19] = 220, ops[20] = 44;
                                ops[21] = (unsigned int)desc_at, ops[22] = 10u | (3u << 16), ops[23] = 0;
                                // op 3: single-pass tiled mix corner (kind 5 | flags | 16<<8) at (0, 44)..(16, 60) from source (2, 1)
                                ops[24] = 5u | flags | (16u << 8), ops[25] = 0, ops[26] = 44, ops[27] = 16, ops[28] = 60;
                                ops[29] = (unsigned int)cov_at, ops[30] = (unsigned int)desc_at, ops[31] = 2u | (1u << 16);
                                scene_ok = scene_ok && kind && gpu_scene(ops_at, 4, bins_at, into_at, 256, 256, 64, 0, 0, 256, 64) && !gpu_model.fault.code;
                                for (unsigned int y = 0; scene_ok && y < 64; y++)
                                        for (unsigned int x = 0; x < 256; x++)
                                        {
                                                unsigned int want_px = 0xff112233u;

                                                if (x >= 12 && x < 140 && y >= 8 && y < 40)
                                                {
                                                        unsigned int c = from[(y - 8 + 7) * 256 + (x - 12 + 5)];
                                                        want_px = swap ? ((c & 0xff00ff00u) | (c >> 16 & 0xff) | (c & 0xff) << 16) : c;
                                                }
                                                else if (x >= 148 && x < 220 && y >= 4 && y < 44)
                                                {
                                                        unsigned int c = from[(y - 4 + 3) * 256 + (x - 148 + 10)];
                                                        if (swap)
                                                                c = (c & 0xff00ff00u) | (c >> 16 & 0xff) | (c & 0xff) << 16;
                                                        want_px = gpu_test_overv(0xff112233u, c);
                                                }
                                                else if (x < 16 && y >= 44 && y < 60)
                                                {
                                                        unsigned int c = from[(y - 44 + 1) * 256 + (x + 2)];
                                                        unsigned char cv = cov_mem[(y - 44) * 16 + x];
                                                        if (swap)
                                                                c = (c & 0xff00ff00u) | (c >> 16 & 0xff) | (c & 0xff) << 16;
                                                        memory_mix_u32_row(&want_px, &c, &cv, 1);
                                                }
                                                scene_wrong += into[y * 256 + x] != want_px;
                                        }
                        }
                        check("gpu model: scene kernel samples tiled/DCC resources directly (32_UINT, swapped, UNORM) for copy (4), over (11), and corner mix (5), bit for bit",
                              scene_ok && !scene_wrong);
                }
                gpu_model_card_reset();
        }

        // The scene kernels a compiler made (gpu_cp and gpu_bx, ops 4 and 5, for widths that are not a multiple of 4) are in forms gpuasm does not make: the model does not run them.
        {
                const struct
                {
                        const char *name;
                        const unsigned int *words;
                        unsigned int size;
                } blobs[] = {{"gpu_cp", gpu_cp, sizeof(gpu_cp)}, {"gpu_bx", gpu_bx, sizeof(gpu_bx)}};

                for (unsigned int b = 0; b < 2; b++)
                {
                        unsigned int refused = 0, words = blobs[b].size / 4 - 2;
                        struct gpu_model_insn in;

                        for (unsigned int at = 0; at < words;)
                        {
                                memory_zero(&gpu_model.fault, sizeof(gpu_model.fault));
                                if (gpu_model_decode(blobs[b].words + 2 + at, words - at, 4ull * at, &in))
                                        refused++, at++;
                                else
                                        at += in.words;
                        }
                        memory_zero(&gpu_model.fault, sizeof(gpu_model.fault));
                        gpu_model_decode(blobs[b].words + 2, words, 0, &in);
                        string_format(log, "gpu model: the clang scene kernel %s on the model NOT RUN -- %p of its %p words are not of gpuasm's forms, the first %s\n", blobs[b].name,
                                      (positive)refused, (positive)words, gpu_model.fault.text);
                }
        }
        gpu_model_card_stop();
        return test_report(null);
}
#endif /* CHECK_gpu_model */

#ifdef CHECK_gpu_model_kit
/*
        CHECK_gpu_kit's rows that draw on a card, which say NOT RUN on a machine with no card, run with the model as the card (test/gpu_model.c,
        whose self-test row in CHECK_gpu_model holds it to GPU_TEST_HASH): the blocks are CHECK_gpu_kit's, copied as they are by the script of
        this commit's message, with each block's pixels counted as one row (a pixel that differs from the processor's is a failure of that row and
        is named, the first five of a block) where CHECK_gpu_kit counts every pixel. They are the seeded differential of the scene kernel against
        the processor's painters: random boxes over random pictures off every edge with every radius and alpha (ui_kit_fill), rings, clipped boxes,
        fills, dots and clears (the records of every cover change, 8 and 9), glyphs translated through 256 tones, images from the card's buffer and
        from a memfd's pages, mixes, turns a quarter, a half and three quarters with a damage rectangle drawn alone, and text runs as a scene and as
        a program's list (with the face, the fourth argument). The seed is CHECK_gpu_kit's, 88172645. The rounds 697 wide are clang's kernels, which
        the model does not take: NOT RUN.
*/
static positive model_wrong; // the pixels that differ from the processor's, every block's

// A pixel of a block that differs from the processor's: counted in model_wrong, and the first five of a block named with their round.
static void model_kit_pixel(positive *bad, string_address label, positive round, positive x, positive y, unsigned int got, unsigned int want)
{
        if (got != want && (++model_wrong, ++*bad) < 6)
                string_format(log, "  FAIL %s round %p: pixel %p,%p is %p, wanted %p\n", label, round, x, y, (positive)got, (positive)want);
}

b32 main()
{
        compositor_rows_make(compositor_rows_in_use); // the compositor's rows, made before any check reads one
        enum { PH = 419, PP = 700, N = 64, ROUNDS = 40 };
        int PW = 696;
        positive pic_at = GPU_DATA, ops_at = pic_at + (positive)PP * PH * 4, bins_at = ops_at + (1 << 20), wrong_before;
        unsigned int address_to pic, address_to ops;
        struct ui ui;
        positive bad = 0;

        if (!gpu_model_card_start((positive)PP * PH * 4 + (1 << 20) + (8 << 20) + (positive)7680 * 4320 * 4 + (16 << 20) + (24 << 20)))
        {
                check("gpu model kit: the model card's buffer is made", false);
                return test_report(null);
        }
        check("gpu model kit: the model card draws the self-test to GPU_TEST_HASH here too", gpu_test_hash() == GPU_TEST_HASH && !gpu_model.fault.code);
        gpu_model_card_reset();
        gpu_patient = 1;
        pic = (unsigned int address_to)((p8 address_to)gpu.at + pic_at), ops = (unsigned int address_to)((p8 address_to)gpu.at + ops_at);
        memory_zero(&ui, sizeof(ui));
        ui.pixels = gpu_kit_want, ui.pitch = PP, ui.width = PW, ui.height = PH, ui.top = 0, ui.bottom = PH, ui.scale = 100;
        wrong_before = model_wrong;
        for (int round = 0; round < ROUNDS; round++)
        {
                PW = round & 1 ? 697 : 696, ui.width = PW;
                if (round & 1) // 697 wide: clang's kernels (ops 4 and 5), which the model does not take
                        continue;
                for (positive i = 0; i < (positive)PP * PH; i++)
                        pic[i] = gpu_kit_want[i] = gpu_random();
                for (int k = 0; k < N; k++)
                {
                        unsigned int address_to q = ops + 8 * k, mode = gpu_random() % 8;
                        int x0 = (int)(gpu_random() % ((PW + 16) * 256)) - 8 * 256, y0 = (int)(gpu_random() % ((PH + 16) * 256)) - 8 * 256;
                        int w = (int)(gpu_random() % (mode == 0 ? 3 * 256 : mode < 3 ? 40 * 256 : 400 * 256)) - (mode == 7 ? 100 : 0);
                        int h = (int)(gpu_random() % (mode == 1 ? 3 * 256 : mode < 3 ? 40 * 256 : 300 * 256)) - (mode == 7 ? 100 : 0);
                        int rx = mode == 4 ? 0 : (int)(gpu_random() % (mode == 5 ? 900 * 256 : 24 * 256)), ry = mode == 4 ? 0 : (int)(gpu_random() % (mode == 6 ? 900 * 256 : 24 * 256));
                        unsigned int argb = gpu_random();
                        struct ui_box box;

                        if (gpu_random() % 3 == 0)
                                argb |= 0xff000000u;
                        if (gpu_random() % 11 == 0)
                                argb &= 0x00ffffffu;
                        box.x0 = x0, box.y0 = y0, box.x1 = x0 + w, box.y1 = y0 + h, box.rx = rx, box.ry = ry;
                        q[0] = 2, q[1] = (unsigned int)box.x0, q[2] = (unsigned int)box.y0, q[3] = (unsigned int)box.x1, q[4] = (unsigned int)box.y1, q[5] = argb, q[6] = (unsigned int)rx,
                        q[7] = (unsigned int)ry;
                        ui_kit_fill(&ui, &box, argb, 0);
                }
                if (!gpu_scene_taken(gpu_scene(ops_at, N, bins_at, pic_at, PP, PW, PH, 0, 0, PW, PH), "gpu kit: the scene finishes"))
                        return test_report(null);
                for (int y = 0; y < PH; y++)
                        for (int x = 0; x < PW; x++)
                        {
                                model_kit_pixel(&bad, "gpu kit", round, x, y, pic[(positive)y * PP + x], gpu_kit_want[(positive)y * PP + x]);
                        }
        }

        check("gpu model kit: the boxes of the even rounds (ui_kit_fill, 696 wide) drawn by the model card are the processor's, pixel for pixel", model_wrong == wrong_before && !gpu_model.fault.code);
        string_format(log, "gpu model kit: the 20 odd rounds of the boxes NOT RUN -- 697 wide is clang's scene kernels (ops 4 and 5), whose words the model does not take\n");
        wrong_before = model_wrong;
        // A glyph translated (op 10, font_cell's cell on its ground): coverage bytes through 256 tones, against the processor's translate, as a program's list through the cut
        // (compositor_gpu_list_ops) with the base of the list the buffer's start; rectangles anywhere in the picture, pitches past the rectangle's width, three tables
        // shared by the ops, coverage with zeros and full bytes, over a random picture, in order.
        {
                enum { TRW = 400, TRH = 100, TRP = 404, TRN = 24, TRROUNDS = 30 };
                positive base_t = bins_at + (8 << 20) + (positive)7680 * 4320 * 4 + (16 << 20) + (16 << 20), ops_t = base_t + (1 << 20), bins_t = ops_t + (1 << 20), cover_t = bins_t + (2 << 20);
                unsigned int address_to tpic = (unsigned int address_to)((p8 address_to)gpu.at + base_t), address_to tops = (unsigned int address_to)((p8 address_to)gpu.at + ops_t);
                unsigned int address_to cut = tops + 8 * 100, want_t[TRP * TRH];
                p8 address_to cover = (p8 address_to)gpu.at + cover_t;
                positive address_to tbases = (positive address_to)((p8 address_to)gpu.at + GPU_BASES_AT), translate_bad = 0;

                tbases[7] = GPU_VA;
                for (int round = 0; round < TRROUNDS; round++)
                {
                        positive used = 4096, tables[3];
                        unsigned int made;

                        for (int t = 0; t < 3; t++)
                        {
                                unsigned int address_to table = (unsigned int address_to)(cover + used);

                                for (int i = 0; i < 256; i++)
                                        table[i] = gpu_random();
                                tables[t] = cover_t + used, used += 1024;
                        }
                        for (positive i = 0; i < (positive)TRP * TRH; i++)
                                tpic[i] = want_t[i] = gpu_random();
                        for (int k = 0; k < TRN; k++)
                        {
                                unsigned int address_to q = tops + 8 * k;
                                int x0 = (int)(gpu_random() % (TRW - 1)), y0 = (int)(gpu_random() % (TRH - 1));
                                int x1 = x0 + 1 + (int)(gpu_random() % (unsigned int)(TRW - x0 - 1 < 40 ? TRW - x0 - 1 : 40)), y1 = y0 + 1 + (int)(gpu_random() % (unsigned int)(TRH - y0 - 1 < 30 ? TRH - y0 - 1 : 30));
                                positive cols = (positive)(x1 - x0), rows = (positive)(y1 - y0), pitch = cols + gpu_random() % 24, table = tables[gpu_random() % 3], at = cover_t + used;
                                unsigned int address_to table_words = (unsigned int address_to)(cover + (table - cover_t));

                                for (positive i = 0; i < rows * pitch + 16; i++)
                                        cover[used + i] = (p8)(gpu_random() % 5 == 0 ? 0 : gpu_random() % 7 == 0 ? 255 : gpu_random());
                                for (positive r = 0; r < rows; r++)
                                        for (positive c = 0; c < cols; c++)
                                                want_t[(positive)(y0 + (int)r) * TRP + (positive)(x0 + (int)c)] = table_words[cover[used + r * pitch + c]];
                                q[0] = 10u | (unsigned int)pitch << 8, q[1] = (unsigned int)x0, q[2] = (unsigned int)y0, q[3] = (unsigned int)x1, q[4] = (unsigned int)y1;
                                q[5] = (unsigned int)table, q[6] = 0, q[7] = (unsigned int)at;
                                used += (rows * pitch + 16 + 15) & ~15ull;
                        }
                        made = compositor_gpu_list_ops(tops, TRN, cover_t, used, gpu.size, TRW, TRH, 7, cut, 1000);
                        check("gpu translate: the glyphs of a frame are taken whole", made == TRN);
                        if (made != TRN || !gpu_scene(ops_t + 32 * 100, made, bins_t, base_t, TRP, TRW, TRH, 0, 0, TRW, TRH))
                        {
                                check("gpu translate: the scene finishes", false);
                                break;
                        }
                        for (int y = 0; y < TRH; y++)
                                for (int x = 0; x < TRW; x++)
                                {
                                        model_kit_pixel(&translate_bad, "gpu translate", round, x, y, tpic[(positive)y * TRP + x], want_t[(positive)y * TRP + x]);
                                }
                }
        }

        check("gpu model kit: the translated glyphs (op 10, through compositor_gpu_list_ops) drawn by the model card are the processor's, pixel for pixel", model_wrong == wrong_before && !gpu_model.fault.code);
        wrong_before = model_wrong;
        // Rings, ripples and rows: the kit's own calls, ui_kit_ring and ui_kit_fill with a clip, on the processor and recorded (the box's record and the
        // records that change its cover), over rows a list is scrolled under (top and bottom of the ui), against a random picture; a width that is not a
        // multiple of 4 is for the kernels that cannot, and is refused.
        {
                struct ui_scene scene;
                struct ui rec;
                positive ring_bad = 0;

                for (int round = 0; round < 40; round++)
                {
                        int top = round % 3 ? 0 : (int)(gpu_random() % 200), bottom = round % 3 == 1 ? PH : PH - (int)(gpu_random() % 200);

                        PW = 696;
                        ui.width = PW, ui.top = top, ui.bottom = bottom, ui.opaque = round & 1;
                        ui.band_top = ui.band_bottom = 0;
                        if (round % 5 == 4)
                                ui.band_top = (int)(gpu_random() % 60), ui.band_bottom = PH - (int)(gpu_random() % 60);
                        rec = ui;
                        rec.scene = &scene;
                        memory_zero(&scene, sizeof(scene));
                        scene.ops = ops, scene.room = 4 * N;
                        for (positive i = 0; i < (positive)PP * PH; i++)
                                pic[i] = gpu_kit_want[i] = gpu_random();
                        for (int k = 0; k < N; k++)
                        {
                                unsigned int kind = gpu_random() % 5, mode = gpu_random() % 8, argb = gpu_random();
                                int x0 = (int)(gpu_random() % ((PW + 16) * 256)) - 8 * 256, y0 = (int)(gpu_random() % ((PH + 16) * 256)) - 8 * 256;
                                int w = (int)(gpu_random() % (mode == 0 ? 3 * 256 : mode < 3 ? 40 * 256 : 300 * 256)) - (mode == 7 ? 100 : 0);
                                int h = (int)(gpu_random() % (mode == 1 ? 3 * 256 : mode < 3 ? 40 * 256 : 200 * 256)) - (mode == 7 ? 100 : 0);
                                int rx = mode == 4 ? 0 : (int)(gpu_random() % (mode == 5 ? 900 * 256 : 24 * 256)), ry = mode == 4 ? 0 : (int)(gpu_random() % (mode == 6 ? 900 * 256 : 24 * 256));
                                int thickness = gpu_random() % 3 ? (int)(gpu_random() % (12 * 256)) : (int)(gpu_random() % 600);
                                struct ui_box box = {x0, y0, x0 + w, y0 + h, rx, ry}, clip;

                                if (gpu_random() % 3 == 0)
                                        argb |= 0xff000000u;
                                if (gpu_random() % 11 == 0)
                                        argb &= 0x00ffffffu;
                                if (gpu_random() % 16 == 0)
                                        thickness = -thickness;
                                clip.x0 = x0 + (int)(gpu_random() % (unsigned int)(w > 0 ? w : 1)) - w / 3, clip.y0 = y0 + (int)(gpu_random() % (unsigned int)(h > 0 ? h : 1)) - h / 3;
                                clip.x1 = clip.x0 + (w > 0 ? w : 256) / 2 + (int)(gpu_random() % 4096), clip.y1 = clip.y0 + (h > 0 ? h : 256) / 2 + (int)(gpu_random() % 4096);
                                clip.rx = (int)(gpu_random() % (20 * 256)), clip.ry = (int)(gpu_random() % (20 * 256));
                                // every so often a wide box with a small clip in it: most of its patches miss the clip
                                if (k % 16 == 0)
                                        kind = 2, box = (struct ui_box){20 * 256, 20 * 256, 620 * 256, 170 * 256, 8 * 256, 8 * 256},
                                        clip = (struct ui_box){300 * 256, 90 * 256, 340 * 256, 110 * 256, 4 * 256, 4 * 256};
                                // the rectangles, the single pixels and the clear are the kit's other painters of the same picture
                                if (k % 24 == 5)
                                        ui_clear(&ui), ui_clear(&rec);
                                else if (kind == 3)
                                        ui_fill(&ui, x0 >> 8, y0 >> 8, w >> 8, h >> 8, argb), ui_fill(&rec, x0 >> 8, y0 >> 8, w >> 8, h >> 8, argb);
                                else if (kind == 4)
                                        ui_dot(&ui, x0 >> 8, y0 >> 8, argb), ui_dot(&rec, x0 >> 8, y0 >> 8, argb);
                                else if (kind == 1)
                                        ui_kit_ring(&ui, &box, thickness, argb), ui_kit_ring(&rec, &box, thickness, argb);
                                else
                                        ui_kit_fill(&ui, &box, argb, kind == 2 ? &clip : 0), ui_kit_fill(&rec, &box, argb, kind == 2 ? &clip : 0);
                        }
                        check("gpu rings: the records fit", !scene.lost && scene.count >= N / 2);
                        check("gpu rings: an odd width is refused", !gpu_scene(ops_at, scene.count, bins_at, pic_at, PP, 697, PH, 0, 0, 697, PH));
                        if (!gpu_scene_taken(gpu_scene(ops_at, scene.count, bins_at, pic_at, PP, PW, PH, 0, 0, PW, PH), "gpu rings: the scene finishes"))
                                return test_report(null);
                        for (int y = 0; y < PH; y++)
                                for (int x = 0; x < PW; x++)
                                {
                                        model_kit_pixel(&ring_bad, "gpu rings", round, x, y, pic[(positive)y * PP + x], gpu_kit_want[(positive)y * PP + x]);
                                }
                }
                ui.top = 0, ui.bottom = PH, ui.width = PW, ui.band_top = ui.band_bottom = 0, ui.opaque = 0;
        }

        check("gpu model kit: the rings, the clipped boxes, the fills, dots and clears of the kit (ui_kit_ring, ui_kit_fill with a clip, ui_fill, ui_dot, ui_clear) drawn by the model card are the processor's, pixel for pixel", model_wrong == wrong_before && !gpu_model.fault.code);
        wrong_before = model_wrong;
        // Images: pictures copied into the picture by the GPU, from two places: a buffer of the GPU's own, and the pages of a memfd mapped shared (what a
        // window's pixels are), read by the GPU as a userptr buffer at an address of their own. The rectangles are cut at the picture's edges, the copies start
        // anywhere in the source; against memory_copy_rect_u32 on the processor over a random picture.
        {
                enum { SW = 400, SH = 300, SP = 416, IMAGES = 40, IROUNDS = 12 };
                positive img_at = bins_at + (8 << 20) + (positive)7680 * 4320 * 4 + (16 << 20) + (12 << 20), up_va = 0;
                unsigned int address_to gpu_img = (unsigned int address_to)((p8 address_to)gpu.at + img_at), address_to shared;
                long fd = system_call_2(syscall(memfd_create), (positive) "mw-window", 1), mapped = -1;
                positive image_bad = 0;

                if (fd >= 0 && system_call_2(syscall(ftruncate), (positive)fd, 1 << 20) == 0)
                        mapped = system_call_6(syscall(mmap), 0, 1 << 20, 3, 1, (positive)fd, 0);
                shared = (unsigned int address_to)mapped;
                if (!(mapped < 0 && mapped > -4096) && gpu_map(shared, 1 << 20))
                        up_va = gpu_va(shared);
                check("gpu images: the memfd's pages are a userptr buffer", up_va != 0);
                for (positive i = 0; i < (positive)SP * SH; i++)
                        gpu_img[i] = gpu_random();
                for (positive i = 0; up_va && i < (positive)SW * SH; i++)
                        shared[i] = gpu_random();
                PW = 696;
                ui.width = PW;
                for (int round = 0; round < IROUNDS; round++)
                {
                        positive count = 0;

                        for (positive i = 0; i < (positive)PP * PH; i++)
                                pic[i] = gpu_kit_want[i] = gpu_random();
                        for (int k = 0; k < IMAGES; k++)
                        {
                                unsigned int address_to q = ops + 8 * count;
                                int from_shared = up_va && gpu_random() % 2, pitch = from_shared ? SW : SP;
                                int sx = (int)(gpu_random() % 300), sy = (int)(gpu_random() % 200), w = 1 + (int)(gpu_random() % (unsigned int)(SW - sx)), h = 1 + (int)(gpu_random() % (unsigned int)(SH - sy));
                                int x0 = (int)(gpu_random() % (PW + 100)) - 50, y0 = (int)(gpu_random() % (PH + 100)) - 50;
                                int cx0, cy0, cx1, cy1;
                                positive from, at;

                                if (gpu_random() % 6 == 0)
                                        x0 = y0 = 0; // the picture's top left corner
                                cx0 = x0 < 0 ? 0 : x0, cy0 = y0 < 0 ? 0 : y0, cx1 = x0 + w > PW ? PW : x0 + w, cy1 = y0 + h > PH ? PH : y0 + h;
                                if (cx1 <= cx0 || cy1 <= cy0)
                                        continue;
                                from = (positive)(sy + cy0 - y0) * (positive)pitch + (positive)(sx + cx0 - x0); // the source's pixel at the cut rectangle's corner
                                at = (from_shared ? up_va : GPU_VA + img_at) + 4ull * from;
                                q[0] = 4, q[1] = (unsigned int)cx0, q[2] = (unsigned int)cy0, q[3] = (unsigned int)cx1, q[4] = (unsigned int)cy1, q[5] = (unsigned int)at, q[6] = (unsigned int)(at >> 32), q[7] = (unsigned int)pitch;
                                memory_copy_rect_u32(gpu_kit_want + (positive)cy0 * PP + (positive)cx0, PP, (from_shared ? shared : gpu_img) + from, (positive)pitch,
                                                     (positive)(cx1 - cx0), (positive)(cy1 - cy0));
                                count++;
                        }
                        if (!gpu_scene_taken(gpu_scene(ops_at, (unsigned int)count, bins_at, pic_at, PP, PW, PH, 0, 0, PW, PH), "gpu images: the scene finishes"))
                                return test_report(null);
                        for (int y = 0; y < PH; y++)
                                for (int x = 0; x < PW; x++)
                                {
                                        model_kit_pixel(&image_bad, "gpu images", round, x, y, pic[(positive)y * PP + x], gpu_kit_want[(positive)y * PP + x]);
                                }
                }
                // Mixes: memory_mix_u32_row over rectangles (a corner's square or a line's end), the ink a colour or a picture, the covers a table of bytes in the
                // buffer with a pitch of its own, a quarter of them none and a quarter whole.
                {
                        enum { CP = 96, CH = 80, MIXES = 48, MROUNDS = 12 };
                        positive cov_at = img_at + (positive)SP * SH * 4 + 4096;
                        unsigned char address_to table = (unsigned char address_to)((p8 address_to)gpu.at + cov_at);
                        static unsigned int ink_row[CP];
                        positive mix_bad = 0;

                        for (int round = 0; round < MROUNDS; round++)
                        {
                                positive count = 0;
                                int cp = CP - round % 5; // the table's rows are cp bytes apart in this round

                                for (positive i = 0; i < (positive)cp * CH; i++)
                                {
                                        unsigned int r = gpu_random();

                                        table[i] = (unsigned char)(r % 4 == 0 ? 0 : r % 4 == 1 ? 255 : r >> 8);
                                }
                                for (positive i = 0; i < (positive)PP * PH; i++)
                                        pic[i] = gpu_kit_want[i] = gpu_random();
                                for (int k = 0; k < MIXES; k++)
                                {
                                        unsigned int address_to q = ops + 8 * count;
                                        int constant = gpu_random() % 3 == 0, from_shared = up_va && gpu_random() % 2, ink_pitch = from_shared ? SW : SP;
                                        int w = 1 + (int)(gpu_random() % 80), h = 1 + (int)(gpu_random() % 70), tx = (int)(gpu_random() % (unsigned int)(cp - w - 4)), ty = (int)(gpu_random() % (unsigned int)(CH - h));
                                        int sx = (int)(gpu_random() % 250), sy = (int)(gpu_random() % 200);
                                        int x0 = (int)(gpu_random() % (PW + 60)) - 30, y0 = (int)(gpu_random() % (PH + 60)) - 30;
                                        int cx0 = x0 < 0 ? 0 : x0, cy0 = y0 < 0 ? 0 : y0, cx1 = x0 + w > PW ? PW : x0 + w, cy1 = y0 + h > PH ? PH : y0 + h;
                                        unsigned int colour = gpu_random();
                                        positive cover_place, ink_place;

                                        if (cx1 <= cx0 || cy1 <= cy0)
                                                continue;
                                        cover_place = (positive)(ty + cy0 - y0) * (positive)cp + (positive)(tx + cx0 - x0);
                                        ink_place = (positive)(sy + cy0 - y0) * (positive)ink_pitch + (positive)(sx + cx0 - x0);
                                        q[0] = 5u | (unsigned int)cp << 8 | (constant ? 0u : (unsigned int)ink_pitch << 16);
                                        q[1] = (unsigned int)cx0, q[2] = (unsigned int)cy0, q[3] = (unsigned int)cx1, q[4] = (unsigned int)cy1, q[5] = (unsigned int)(cov_at + cover_place);
                                        if (constant)
                                                q[6] = colour, q[7] = 0;
                                        else
                                        {
                                                positive at = (from_shared ? up_va : GPU_VA + img_at) + 4ull * ink_place;

                                                q[6] = (unsigned int)at, q[7] = (unsigned int)(at >> 32);
                                        }
                                        for (int y = cy0; y < cy1; y++)
                                        {
                                                const unsigned int address_to ink = constant ? ink_row : (from_shared ? shared : gpu_img) + ink_place + (positive)(y - cy0) * (positive)ink_pitch;

                                                if (constant)
                                                        memory_fill_u32(ink_row, (positive)(cx1 - cx0), colour);
                                                memory_mix_u32_row(gpu_kit_want + (positive)y * PP + (positive)cx0, ink, table + cover_place + (positive)(y - cy0) * (positive)cp, (positive)(cx1 - cx0));
                                        }
                                        count++;
                                }
                                if (!gpu_scene_taken(gpu_scene(ops_at, (unsigned int)count, bins_at, pic_at, PP, PW, PH, 0, 0, PW, PH), "gpu mixes: the scene finishes"))
                                        return test_report(null);
                                for (int y = 0; y < PH; y++)
                                        for (int x = 0; x < PW; x++)
                                        {
                                                model_kit_pixel(&mix_bad, "gpu mixes", round, x, y, pic[(positive)y * PP + x], gpu_kit_want[(positive)y * PP + x]);
                                        }
                        }
                }
                // Turns: a screen's rectangle of a picture turned a quarter, a half and three quarters into a panel's buffer (windows_rotate_u32, which Canvas turns a
                // screen with), one op over the whole panel with a step along its rows and one down them, and a damage rectangle drawn alone: the rounded-out
                // rectangle of it is the whole turn's and the rest of the buffer is as it was.
                {
                        enum { TP = 544, TH = 320, DP = 544, DH = 540, TROUNDS = 12 };
                        positive tsrc_at = img_at + (1 << 20), tdst_at = img_at + (2 << 20);
                        unsigned int address_to tsrc = (unsigned int address_to)((p8 address_to)gpu.at + tsrc_at), address_to tdst = (unsigned int address_to)((p8 address_to)gpu.at + tdst_at);
                        static unsigned int turned_want[DP * DH];
                        positive turn_bad = 0;

                        for (int round = 0; round < TROUNDS; round++)
                        {
                                unsigned int degrees = round % 3 == 0 ? 90 : round % 3 == 1 ? 180 : 270;
                                int lw = 4 * (60 + (int)(gpu_random() % 70)), lh = 4 * (40 + (int)(gpu_random() % 40)), ox = (int)(gpu_random() % 20), oy = (int)(gpu_random() % 10);
                                int pw = degrees == 180 ? lw : lh, ph = degrees == 180 ? lh : lw, stepx, stepy;
                                long first;
                                positive at;
                                unsigned int x0 = gpu_random() % (unsigned int)lw, y0 = gpu_random() % (unsigned int)lh, x1, y1, panel[4];
                                unsigned int address_to q = ops;

                                x1 = x0 + 1 + gpu_random() % (unsigned int)(lw - (int)x0), y1 = y0 + 1 + gpu_random() % (unsigned int)(lh - (int)y0);
                                if (round % 4 == 3)
                                        x0 = 0, y0 = 0, x1 = (unsigned int)lw, y1 = (unsigned int)lh;
                                for (positive i = 0; i < (positive)TP * TH; i++)
                                        tsrc[i] = gpu_random();
                                for (positive i = 0; i < (positive)DP * DH; i++)
                                        tdst[i] = turned_want[i] = 0;
                                windows_rotate_u32(turned_want, DP, tsrc + (positive)oy * TP + (positive)ox, TP, (unsigned int)lw, (unsigned int)lh, degrees, 0, 0, (unsigned int)lw, (unsigned int)lh);
                                if (degrees == 90)
                                        stepx = -TP, stepy = 1, first = (long)(oy + lh - 1) * TP + ox;
                                else if (degrees == 270)
                                        stepx = TP, stepy = -1, first = (long)oy * TP + ox + lw - 1;
                                else
                                        stepx = -1, stepy = -TP, first = (long)(oy + lh - 1) * TP + ox + lw - 1;
                                at = GPU_VA + tsrc_at + 4ull * (positive)first;
                                q[0] = 7u | (unsigned int)(stepx + 0x800000) << 8, q[1] = 0, q[2] = 0, q[3] = (unsigned int)pw, q[4] = (unsigned int)ph;
                                q[5] = (unsigned int)at, q[6] = (unsigned int)(at >> 32), q[7] = (unsigned int)stepy;
                                windows_rotate_panel((unsigned int)lw, (unsigned int)lh, degrees, x0, y0, x1, y1, panel);
                                if (!gpu_scene_taken(gpu_scene(ops_at, 1, bins_at, tdst_at, DP, (unsigned int)pw, (unsigned int)ph, panel[0], panel[1], panel[2], panel[3]), "gpu turns: the scene finishes"))
                                        return test_report(null);
                                {
                                        unsigned int rx0 = (panel[0] >> 6) << 6, rx1 = ((panel[2] + 63) >> 6) << 6, ry0 = (panel[1] >> 2) << 2, ry1 = ((panel[3] + 3) >> 2) << 2;

                                        rx1 = rx1 > (unsigned int)pw ? (unsigned int)pw : rx1, ry1 = ry1 > (unsigned int)ph ? (unsigned int)ph : ry1;
                                        for (unsigned int y = 0; y < (unsigned int)ph; y++)
                                                for (unsigned int x = 0; x < (unsigned int)pw; x++)
                                                {
                                                        int inside = x >= rx0 && x < rx1 && y >= ry0 && y < ry1;
                                                        unsigned int want_pixel = inside ? turned_want[(positive)y * DP + x] : 0;

                                                        if (tdst[(positive)y * DP + x] != want_pixel && (++model_wrong, ++turn_bad) < 6)
                                                                string_format(log, "  FAIL gpu turns %p (%p degrees): pixel %p,%p is %p, wanted %p\n", (positive)round, (positive)degrees, (positive)x, (positive)y,
                                                                              (positive)tdst[(positive)y * DP + x], (positive)want_pixel);
                                                }
                                }
                        }
                }
        }

        check("gpu model kit: the images from the card's buffer and from a memfd's pages, the mixes and the turns drawn by the model card are the processor's, pixel for pixel", model_wrong == wrong_before && !gpu_model.fault.code);
        wrong_before = model_wrong;
        // Text: runs painted by ui_run_in on the processor, the same runs recorded (the font engine's coverage, packed to bytes) and drawn by the GPU, a window's
        // clear first; every size, ink, alpha, stroke and clip, over a random ground. Needs the face (argument 3); without it the rows are skipped.
        if (program_argument_count() > 3)
        {
                enum { TW = 696, TH = 220, TP = 700, RUNS = 40, TROUNDS = 30 };
                static font_engine engine;
                static p8 atlas[512 * 512];
                static font_face face;
                static unsigned int scratch[TW * UI_SCRATCH_ROWS], want_text[TP * TH];
                static const char *const words[] = {"Settings", "Display and brightness", "The quick brown fox jumps over the lazy dog 0123456789",
                                                    "Wi-Fi   Bluetooth   USB", "Zwischenablage \xc3\x9f \xc3\xbc \xc3\xb6 \xc3\xa9", "AaBbCc", "WWWW iiii llll", "[x] (y) {z} 100%"};
                positive size = 0, bytes_of_face = compositor_map_file(program_argument(3), &size);
                positive text_at = bins_at + (8 << 20) + (positive)7680 * 4320 * 4 + (16 << 20), tpic_at = text_at, tops_at = tpic_at + (positive)TP * TH * 4,
                         tcover_at = tops_at + (1 << 20), tbins_at = tcover_at + (8 << 20);
                unsigned int address_to tpic = (unsigned int address_to)((p8 address_to)gpu.at + tpic_at), address_to tops = (unsigned int address_to)((p8 address_to)gpu.at + tops_at);
                struct ui_scene scene;
                struct ui cpu, rec;
                const font_face address_to faces[2] = {&face, &face};
                positive text_bad = 0;

                if (bytes_of_face && font_load(&face, (const p8 address_to)bytes_of_face, size, 0) == 0)
                {
                        font_start(&engine, atlas, 512, 512);
                        memory_zero(&cpu, sizeof(cpu));
                        cpu.engine = &engine, cpu.regular = cpu.bold = &face, cpu.scratch = scratch, cpu.scratch_width = TW, cpu.scale = 100;
                        cpu.width = TW, cpu.height = TH, cpu.top = 0, cpu.bottom = TH, cpu.pitch = TP, cpu.pixels = want_text;
                        rec = cpu;
                        rec.scene = &scene;
                        for (int round = 0; round < TROUNDS; round++)
                        {
                                unsigned int ground = gpu_random() | 0xff000000u;

                                memory_zero(&scene, sizeof(scene));
                                scene.ops = tops + 8, scene.room = 512, scene.cover = (unsigned char address_to)((p8 address_to)gpu.at + tcover_at), scene.size = 8 << 20, scene.at = tcover_at;
                                for (positive i = 0; i < (positive)TP * TH; i++)
                                        want_text[i] = ground, tpic[i] = 0x12345678u;
                                tops[0] = 0, tops[1] = tops[2] = 0, tops[3] = TW, tops[4] = TH, tops[5] = ground, tops[6] = tops[7] = 0;
                                for (int k = 0; k < RUNS; k++)
                                {
                                        const char *word = words[gpu_random() % (sizeof(words) / sizeof(words[0]))];
                                        int size_px = 10 + (int)(gpu_random() % 30), x = (int)(gpu_random() % (TW + 80)) - 60, baseline = (int)(gpu_random() % (TH + 30)) - 5;
                                        int low = gpu_random() % 4 ? 0 : (int)(gpu_random() % (TW / 2)), high = gpu_random() % 4 ? TW : TW / 2 + (int)(gpu_random() % (TW / 2));
                                        unsigned int colour = gpu_random() & 0xffffffu, alpha = gpu_random() % 3 ? 255 : gpu_random() & 255, stroke = gpu_random() % 4 ? 0 : 1 + gpu_random() % 255;

                                        ui_run_in(&cpu, faces, x, baseline, (string_address)word, size_px, colour, alpha, low, high, stroke);
                                        ui_run_in(&rec, faces, x, baseline, (string_address)word, size_px, colour, alpha, low, high, stroke);
                                }
                                check("gpu text: the runs were recorded", scene.count > 10);
                                if (!gpu_scene_taken(gpu_scene(tops_at, 1 + scene.count, tbins_at, tpic_at, TP, TW, TH, 0, 0, TW, TH), "gpu text: the scene finishes"))
                                        break;
                                for (int y = 0; y < TH; y++)
                                        for (int x = 0; x < TW; x++)
                                        {
                                                model_kit_pixel(&text_bad, "gpu text", round, x, y, tpic[(positive)y * TP + x], want_text[(positive)y * TP + x]);
                                        }
                                // The same runs as a program's list: the ops read by the compositor's cut (compositor_gpu_list_ops), the cover bytes being the list's, its base
                                // the buffer's start (base number 3) and its cover bytes [tcover_at, tcover_at + scene.used), and drawn from what the cut wrote.
                                {
                                        positive address_to bases = (positive address_to)((p8 address_to)gpu.at + GPU_BASES_AT);
                                        unsigned int address_to cut = tops + 8 * 600, made;

                                        bases[3] = GPU_VA;
                                        made = compositor_gpu_list_ops(tops, 1 + scene.count, tcover_at, scene.used, gpu.size, TW, TH, 3, cut, 1000);
                                        check("gpu list: the runs of a frame are taken whole", made == 1 + scene.count);
                                        for (positive i = 0; i < (positive)TP * TH; i++)
                                                tpic[i] = 0x12345678u;
                                        if (made == COMPOSITOR_GPU_LIST_REFUSED || !gpu_scene(tops_at + 32 * 600, made, tbins_at, tpic_at, TP, TW, TH, 0, 0, TW, TH))
                                        {
                                                check("gpu list: the scene finishes", false);
                                                break;
                                        }
                                        for (int y = 0; y < TH; y++)
                                                for (int x = 0; x < TW; x++)
                                                {
                                                        model_kit_pixel(&text_bad, "gpu list", round, x, y, tpic[(positive)y * TP + x], want_text[(positive)y * TP + x]);
                                                }
                                }
                                // The same runs as a program's published list in a memfd of its own, drawn by compositor_gpu_list_render_bytes into a picture in another memfd: the
                                // reader, the cut, the map of the cover bytes and the number of their base, the map of the picture the card writes, the damage; against the processor's pixels.
                                {
                                        static unsigned char address_to list_memory;
                                        static unsigned int address_to list_picture;
                                        enum { LIST_BYTES = 4 << 20 };
                                        struct ui_draw_list list;
                                        struct ui lui = rec;
                                        int damage[4] = {-1, -1, -1, -1}, drawn;
                                        positive picture_bytes = (positive)TP * TH * 4 + 8192;

                                        if (!list_memory)
                                        {
                                                long lfd = system_call_2(syscall(memfd_create), (positive) "mw-list", 1), pfd = system_call_2(syscall(memfd_create), (positive) "mw-picture", 1);
                                                long lm = -1, pm = -1;

                                                if (lfd >= 0 && system_call_2(syscall(ftruncate), (positive)lfd, LIST_BYTES) == 0)
                                                        lm = system_call_6(syscall(mmap), 0, LIST_BYTES, 3, 1, (positive)lfd, 0);
                                                if (pfd >= 0 && system_call_2(syscall(ftruncate), (positive)pfd, picture_bytes) == 0)
                                                        pm = system_call_6(syscall(mmap), 0, picture_bytes, 3, 1, (positive)pfd, 0);
                                                if (!(lm < 0 && lm > -4096) && !(pm < 0 && pm > -4096))
                                                        list_memory = (unsigned char address_to)lm, list_picture = (unsigned int address_to)pm;
                                        }
                                        check("gpu list render: the memory for a list and a picture", list_memory && list_picture);
                                        if (!list_memory || !list_picture)
                                                break;
                                        lui.scene = null;
                                        compositor_gpu_state.up = 1, compositor_gpu_state.dead = 0, compositor_gpu = 1;
                                        compositor_gscene.ops = (unsigned int address_to)((p8 address_to)gpu.at + COMPOSITOR_GPU_OPS_AT), compositor_gscene.room = (1 << 20) / 32;
                                        check("gpu list render: the list is made", ui_draw_list_init(&list, list_memory, LIST_BYTES, 4096) && ui_scene_begin(&lui, &list));
                                        if (1 + scene.count <= list.scene.room && scene.used <= list.scene.size)
                                        {
                                                memory_copy(list.scene.ops, tops, 32 * (1 + scene.count));
                                                memory_copy(list.scene.cover, scene.cover, scene.used);
                                                for (unsigned int k = 0; k < 1 + scene.count; k++)
                                                        if ((list.scene.ops[8 * k] & 255) == 3)
                                                                list.scene.ops[8 * k + 7] = (unsigned int)(list.scene.ops[8 * k + 7] - tcover_at + list.scene.at);
                                                list.scene.count = 1 + scene.count, list.scene.used = scene.used;
                                                list.scene.dx0 = 0, list.scene.dy0 = 0, list.scene.dx1 = TW, list.scene.dy1 = TH;
                                        }
                                        else
                                                list.scene.lost = 1;
                                        check("gpu list render: the frame is published", ui_scene_end(&lui, &list));
                                        for (positive i = 0; i < picture_bytes / 4; i++)
                                                list_picture[i] = 0x12345678u;
                                        drawn = compositor_gpu_list_render_bytes(5, list_memory, LIST_BYTES, list_picture, TP, TW, TH, damage);
                                        check("gpu list render: the frame is drawn, with the damage it had", drawn == 1 && damage[0] == 0 && damage[1] == 0 && damage[2] == TW && damage[3] == TH);
                                        for (int y = 0; y < TH; y++)
                                                for (int x = 0; x < TW; x++)
                                                {
                                                        model_kit_pixel(&text_bad, "gpu list render", round, x, y, list_picture[(positive)y * TP + x], want_text[(positive)y * TP + x]);
                                                }
                                        // The same list again (the next frame of a window whose cover bytes are the same, or a few less): its cover bytes are in the map the
                                        // first frame made, and no map is made for them (a map a frame was a userptr and a mapping of the card's each time).
                                        {
                                                unsigned int mapped = gpu_maps_user + gpu_maps_dma, held = gpu_map_count;
                                                int again = compositor_gpu_list_render_bytes(5, list_memory, LIST_BYTES, list_picture, TP, TW, TH, damage);

                                                check("gpu list render: the same list drawn again makes no map", again == 1 && gpu_maps_user + gpu_maps_dma == mapped && gpu_map_count == held);
                                        }
                                        if (round == 0)
                                        {
                                                positive began = clock_monotonic_nanoseconds();

                                                for (unsigned int k = 0; k < 64; k++)
                                                        compositor_gpu_list_render_bytes(5, list_memory, LIST_BYTES, list_picture, TP, TW, TH, damage);
                                                string_format(log, "  gpu list render: 64 frames of one list took %p us, %p maps held\n", (clock_monotonic_nanoseconds() - began) / 1000, (positive)gpu_map_count);
                                        }
                                        gpu_trim();
                                }
                                // The same runs with their bitmaps named by a base of the table (a list's memfd is mapped at an address of its own): the text op's word 6 holds the
                                // base's number and its word 7 the place from that base, which is the buffer's start plus 4096 here, so the place is 4096 less than it was.
                                {
                                        positive address_to bases = (positive address_to)((p8 address_to)gpu.at + GPU_BASES_AT);

                                        bases[1] = GPU_VA + 4096, bases[2] = GPU_VA + 1000000;
                                        for (unsigned int k = 0; k < scene.count; k++)
                                        {
                                                unsigned int address_to q = tops + 8 + 8 * k;

                                                if ((q[0] & 255) == 3)
                                                        q[6] |= (k & 1 ? 2u : 1u) << 16, q[7] -= k & 1 ? 1000000u : 4096u;
                                        }
                                        for (positive i = 0; i < (positive)TP * TH; i++)
                                                tpic[i] = 0x12345678u;
                                        if (!gpu_scene_taken(gpu_scene(tops_at, 1 + scene.count, tbins_at, tpic_at, TP, TW, TH, 0, 0, TW, TH), "gpu text bases: the scene finishes"))
                                                break;
                                        for (int y = 0; y < TH; y++)
                                                for (int x = 0; x < TW; x++)
                                                {
                                                        model_kit_pixel(&text_bad, "gpu text bases", round, x, y, tpic[(positive)y * TP + x], want_text[(positive)y * TP + x]);
                                                }
                                }
                        }
                }
        }

        check("gpu model kit: the text runs (ui_run_in), as a scene and as a program's list drawn by the model card are the processor's, pixel for pixel", model_wrong == wrong_before && !gpu_model.fault.code);

        // A blended window (a client's premultiplied picture with alpha, frame->blend) with round corners, as the compositor's records draw it on the card
        // (compositor_gpu_frames: the line, kind 11 over the middle, each round corner's line by its cover (5) and the picture laid over by the inner cover (12)),
        // against compositor_draw on the processor over the same ground: every corner state (f->square's 16 values), radii from 1 to 48, lines of 0 to 4, windows
        // partly off every edge, and a damage rectangle of the picture's, the pixels inside it compared whole (all four bytes). The pictures have every alpha,
        // fully opaque and fully clear pixels, and clear ones with colour left in them (a premultiplied picture a client got wrong).
        {
                enum { BW = 256, BH = 160, BROUNDS = 96, BPIC = 200 * 160 };
                static unsigned int picture[BPIC], want_b[BW * BH];
                positive d_at = bins_at + (8 << 20) + (positive)7680 * 4320 * 4 + (16 << 20), blend_bad = 0, compared = 0;
                unsigned int address_to got_b = (unsigned int address_to)((p8 address_to)gpu.at + d_at), corners_12 = 0;
                int kept_width = compositor.width, kept_height = compositor.height;

                static const unsigned int poisons[3] = {0, 0xdeadbeefu, 0xffffffffu};
                positive differ[3] = {0, 0, 0};
                unsigned int kept_seed = gpu_seed;

                compositor_gpu_layout();
                for (int pass = 0; pass < 3; pass++)
                for (int round = 0; round < BROUNDS; round++)
                {
                        struct compositor_frame f;
                        positive before_pass = blend_bad;

                        if (round == 0)
                                gpu_seed = kept_seed, gpu_model.poison = poisons[pass];
                        struct compositor_target t;
                        unsigned int ground = gpu_random() | 0xff000000u;
                        int pw = 24 + (int)(gpu_random() % 170), ph = 24 + (int)(gpu_random() % 130);
                        int dx0 = (int)(gpu_random() % BW), dy0 = (int)(gpu_random() % BH), dx1, dy1;

                        for (int i = 0; i < pw * ph; i++)
                        {
                                unsigned int a = gpu_random() % 4 == 0 ? 255 : gpu_random() % 4 == 0 ? 0 : gpu_random() & 255, s = gpu_random();

                                picture[i] = a << 24 | (((s & 255) * a / 255) | ((s >> 8 & 255) * a / 255) << 8 | ((s >> 16 & 255) * a / 255) << 16);
                                if (!a && gpu_random() % 5 == 0)
                                        picture[i] = s & 0x00ffffffu; // clear, with colour left in it
                        }
                        memory_zero(&f, sizeof(f));
                        f.x = (int)(gpu_random() % (BW + 60)) - 60, f.y = (int)(gpu_random() % (BH + 60)) - 60, f.width = pw, f.height = ph, f.scale = 100;
                        f.pixels = picture, f.pitch = (unsigned long)pw, f.blend = 1, f.square = round % 16;
                        f.line = (int)(gpu_random() % 5), f.radius = 1 + (int)(gpu_random() % 48), f.edge = gpu_random() | 0xff000000u, f.slot = -1;
                        dx1 = dx0 + 1 + (int)(gpu_random() % (unsigned int)(BW - dx0)), dy1 = dy0 + 1 + (int)(gpu_random() % (unsigned int)(BH - dy0));
                        if (round % 3 == 0)
                                dx0 = dy0 = 0, dx1 = BW, dy1 = BH;
                        compositor.width = BW, compositor.height = BH;
                        compositor_gscene.width = BW, compositor_gscene.height = BH;
                        if (!gpu_va_bytes(picture, sizeof(picture)) && !gpu_map(picture, sizeof(picture)))
                        {
                                check("gpu model kit: the blended window's picture is mapped for the model card", false);
                                break;
                        }
                        if (!compositor_gpu_frames(&f, 1, ground))
                        {
                                if (++blend_bad < 6)
                                        string_format(log, "  FAIL gpu blend round %p: the records were refused (why %p)\n", (positive)round, (positive)compositor_gscene.why);
                                model_wrong++;
                                continue;
                        }
                        for (unsigned int k = 0; k < compositor_gscene.count; k++)
                                corners_12 += (compositor_gscene.ops[8 * k] & 255) == 12;
                        for (int i = 0; i < BW * BH; i++)
                                got_b[i] = want_b[i] = ground;
                        memory_zero(&t, sizeof(t));
                        t.pixels = want_b, t.pitch = BW, t.width = BW, t.height = BH, t.x1 = 0, t.y1 = 0, t.x2 = BW, t.y2 = BH;
                        compositor_draw(&t, &f);
                        if (!gpu_scene(COMPOSITOR_GPU_OPS_AT, compositor_gscene.count, COMPOSITOR_GPU_BINS_AT, d_at, BW, BW, BH, (unsigned int)dx0, (unsigned int)dy0,
                                       (unsigned int)dx1, (unsigned int)dy1))
                        {
                                check("gpu model kit: the blended window's scene finishes", false);
                                break;
                        }
                        for (int y = dy0; y < dy1; y++)
                                for (int x = dx0; x < dx1; x++, compared++)
                                        if (got_b[y * BW + x] != want_b[y * BW + x] && (++model_wrong, ++blend_bad) < 6)
                                                string_format(log, "  FAIL gpu blend round %p, poison %p (square %p, radius %p, line %p, at %p,%p): pixel %p,%p is %p, wanted %p\n",
                                                              (positive)round, (positive)poisons[pass], (positive)f.square, (positive)f.radius, (positive)f.line, (positive)(f.x + 1000),
                                                              (positive)(f.y + 1000), (positive)x, (positive)y, (positive)got_b[y * BW + x], (positive)want_b[y * BW + x]);
                        differ[pass] += blend_bad - before_pass;
                }
                gpu_model.poison = 0;
                string_format(log, "gpu model kit: the blended windows' pixels that differ with every register nothing writes 0: %p, 0xdeadbeef: %p, all ones: %p\n",
                              differ[0], differ[1], differ[2]);
                compositor.width = kept_width, compositor.height = kept_height;
                check("gpu model kit: a blended window with round corners (kind 12 in its corners) drawn by the model card is compositor_draw's, pixel for pixel, over every "
                      "corner state, radius, line, place and damage, with every register nothing writes started as 0, 0xdeadbeef and all ones",
                      blend_bad == 0 && corners_12 > 0 && !gpu_model.fault.code);
                string_format(log, "gpu model kit: %p blended windows under each of three poisons, %p corner records of kind 12, %p pixels compared, %p differ\n", (positive)BROUNDS, (positive)corners_12, compared,
                              blend_bad);
        }

        // The coverage of CHECK_gpu_kit's scenes: the scene kernel's instructions they ran, and the bytes in its code where each run of the ones they did not begins.
        {
                const p32 *code = gpu_kernels[6].words + 2;
                unsigned int words = (unsigned int)(gpu_kernels[6].size / 4 - 2), ran = 0, of = 0;
                b32 gap = false;

                string_format(log, "gpu model kit coverage: the scene kernel's instructions not run by these blocks begin at the bytes:");
                gpu_model.code_va = 0;
                for (unsigned int at = 0; at < words;)
                {
                        struct gpu_model_insn in;
                        b32 hit;

                        if (gpu_model_decode(code + at, words - at, 4ull * at, &in))
                                break;
                        hit = at < GPU_MODEL_CARD_WORDS && gpu_model_card.hits[6][at] != 0, of++, ran += hit;
                        if (!hit && !gap)
                                string_format(log, " %p", (positive)(4 * at));
                        gap = !hit, at += in.words;
                }
                string_format(log, "\ngpu model kit coverage: these blocks ran %p of the scene kernel's %p instructions, in %p jobs; %p pixels of them all differ from the processor's%s%s\n",
                              (positive)ran, (positive)of, (positive)gpu_model_card.jobs, model_wrong, gpu_model.fault.code ? "; the model's failure: " : "", gpu_model.fault.text);
        }
        gpu_model_card_stop();
        return test_report(null);
}
#endif /* CHECK_gpu_model_kit */

