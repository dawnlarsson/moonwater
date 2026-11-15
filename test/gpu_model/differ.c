/*
        Two drawings of the same scenes, compared pixel for pixel: the differential for a change to the scene kernel or to gpu.c's binning that
        must not move a pixel (P2's index lists, the cut at binning). A drawer is anything that draws a scene's records into a picture as
        gpu_scene does (its arguments are gpu_scene's); gpu_model_draw_shipped is gpu_scene with the scene kernel the build made, and
        gpu_model_draw_staged gpu_scene with the kernel gpu_model_stage put in the card's buffer (its own words and its own resource words,
        as a gpu.c built with it would send them). gpu_model_card_differential(one, two, seed, scenes, &difference) draws scenes random scenes
        from the seed with each and answers the pixels that differ, the first of them in the difference.

        A scene is records of every kind the scene kernel draws, stacked in any order over a picture of random pixels: fills and tones, boxes
        in 256ths with radii and with holes and clips after them (8 and 9), text runs, images copied and laid over, mixes with an ink or a
        picture, close buttons, turned pictures, translated glyphs; their rectangles off every edge, a damage rectangle that is the whole
        picture or a part, widths of a multiple of 4 to 512 and pitches past them. What the records read is in the card's buffer, from
        GPU_DATA + 4 MiB on, so a card started with 6 MiB of the caller's or more takes them.
*/

typedef b32 (*gpu_model_drawer)(positive ops_at, unsigned int n, positive bins_at, positive pic_at, unsigned int pitch, unsigned int w, unsigned int h,
                                unsigned int dx0, unsigned int dy0, unsigned int dx1, unsigned int dy1);

struct gpu_model_difference
{
        unsigned int scene, x, y, one, two; // the first pixel that differs: its scene, its place, the two drawings' pixels
        unsigned int scenes, failed;        // the scenes drawn, and the ones a drawer refused or the model failed on (the first failure in gpu_model.fault)
        positive pixels, records;           // the pixels compared, the records drawn
};

// The kernel the build made as gpu.c put it in the buffer, and the one staged.
static unsigned int gpu_model_shipped_at;

static b32 gpu_model_draw_shipped(positive ops_at, unsigned int n, positive bins_at, positive pic_at, unsigned int pitch, unsigned int w, unsigned int h, unsigned int dx0,
                                  unsigned int dy0, unsigned int dx1, unsigned int dy1)
{
        if (gpu_model_shipped_at)
                gpu_kernel_at[GPU_KERNEL_OF(6)] = gpu_model_shipped_at;
        return gpu_scene(ops_at, n, bins_at, pic_at, pitch, w, h, dx0, dy0, dx1, dy1);
}

static b32 gpu_model_draw_staged(positive ops_at, unsigned int n, positive bins_at, positive pic_at, unsigned int pitch, unsigned int w, unsigned int h, unsigned int dx0,
                                 unsigned int dy0, unsigned int dx1, unsigned int dy1)
{
        b32 drawn;

        if (!gpu_model_card.staged_words)
                return false;
        if (!gpu_model_shipped_at)
                gpu_model_shipped_at = gpu_kernel_at[GPU_KERNEL_OF(6)];
        gpu_kernel_at[GPU_KERNEL_OF(6)] = (unsigned int)gpu_model_card.staged_at;
        drawn = gpu_scene(ops_at, n, bins_at, pic_at, pitch, w, h, dx0, dy0, dx1, dy1);
        gpu_kernel_at[GPU_KERNEL_OF(6)] = gpu_model_shipped_at;
        return drawn;
}

// A scene kernel to draw with in the shipped one's place (its two resource words and its code, words in all, as gpu_kernels.inc has a kernel).
static b32 gpu_model_stage(const unsigned int address_to kernel, unsigned int words)
{
        if (words < 3 || 4ull * (words - 2) > GPU_MODEL_STAGE || !gpu.at)
                return false;
        memory_copy((p8 address_to)gpu.at + gpu_model_card.staged_at, kernel + 2, 4ull * (words - 2));
        gpu_model_card.staged_rsrc[0] = kernel[0], gpu_model_card.staged_rsrc[1] = kernel[1], gpu_model_card.staged_words = words - 2;
        return true;
}

static unsigned int gpu_model_next(unsigned int address_to seed)
{
        *seed ^= *seed << 13, *seed ^= *seed >> 17, *seed ^= *seed << 5;
        return *seed;
}

#define GM_AREA(at) (GPU_DATA + (positive)(at) * (1 << 20)) // the differential's places in the card's buffer, a MiB each
#define GM_SRC_PITCH 512 // the pictures records read: 512 by 512 at GM_AREA(4); the cover bytes 512 by 512 at GM_AREA(5); three tables of 256 tones after them

// One random record (or a box and its changes of cover) at q for a w by h picture: its words, how many records.
static unsigned int gpu_model_record(unsigned int address_to seed, unsigned int address_to q, unsigned int w, unsigned int h)
{
        unsigned int kind = gpu_model_next(seed) % 12, colour = gpu_model_next(seed), rw = 1 + gpu_model_next(seed) % 160, rh = 1 + gpu_model_next(seed) % 100;
        int x0 = (int)(gpu_model_next(seed) % (w + 40)) - 20, y0 = (int)(gpu_model_next(seed) % (h + 40)) - 20;
        positive src = GPU_VA + GM_AREA(4), at = (gpu_model_next(seed) % (GM_SRC_PITCH - 160)) + (positive)(gpu_model_next(seed) % (GM_SRC_PITCH - 100)) * GM_SRC_PITCH;
        positive cover = GM_AREA(5) + GM_SRC_PITCH + 1 + at, tones = GM_AREA(5) + (positive)GM_SRC_PITCH * GM_SRC_PITCH + 1024ull * (gpu_model_next(seed) % 3);
        unsigned int count = 1;

        if (gpu_model_next(seed) % 3 == 0)
                colour |= 0xff000000u;
        q[0] = kind, q[1] = (unsigned int)x0, q[2] = (unsigned int)y0, q[3] = (unsigned int)x0 + rw, q[4] = (unsigned int)y0 + rh, q[5] = colour, q[6] = q[7] = 0;
        switch (kind)
        {
        case 2: // a box in 256ths, its radii, and up to two holes or clips after it
        case 8:
        case 9:
        {
                unsigned int changes = gpu_model_next(seed) % 3;

                count = 1 + changes;
                for (unsigned int k = 0; k < count; k++)
                {
                        unsigned int address_to b = q + 8 * k;
                        int bx = x0 * 256 + (int)(gpu_model_next(seed) % 4096) - 2048, by = y0 * 256 + (int)(gpu_model_next(seed) % 4096) - 2048;

                        b[0] = k ? 8 + gpu_model_next(seed) % 2 : 2 | changes << 8, b[1] = (unsigned int)bx, b[2] = (unsigned int)by;
                        b[3] = (unsigned int)(bx + (int)(gpu_model_next(seed) % (rw * 256 + 1))), b[4] = (unsigned int)(by + (int)(gpu_model_next(seed) % (rh * 256 + 1)));
                        b[5] = k ? 0 : colour, b[6] = gpu_model_next(seed) % 3 ? gpu_model_next(seed) % (40 * 256) : 0, b[7] = gpu_model_next(seed) % 3 ? gpu_model_next(seed) % (40 * 256) : 0;
                }
                break;
        }
        case 3: // a text run: its bitmap's pitch, the ink, the alpha and the stroke, the bitmap's place from the buffer's start
                q[0] = 3 | GM_SRC_PITCH << 8, q[6] = (gpu_model_next(seed) & 255) | (gpu_model_next(seed) % 256) << 8, q[7] = (unsigned int)cover;
                break;
        case 4: // an image copied, and laid over (11): the picture's pixel at x0, y0 and its pitch
        case 11:
                q[0] = kind == 4 ? 4 : 11, q[5] = (unsigned int)(src + 4 * at), q[6] = (unsigned int)((src + 4 * at) >> 32), q[7] = GM_SRC_PITCH;
                break;
        case 5: // a mix: the covers' pitch (eight bits of the record's: 200 here) and place, an ink of a colour or a picture
        {
                b32 constant = gpu_model_next(seed) % 2;

                q[0] = 5 | 200u << 8 | (constant ? 0 : GM_SRC_PITCH << 16), q[5] = (unsigned int)cover;
                q[6] = constant ? colour : (unsigned int)(src + 4 * at), q[7] = constant ? 0 : (unsigned int)((src + 4 * at) >> 32);
                break;
        }
        case 6: // a close button: its side, the window's pitch, its covers, the window's pixel under x0, y0, the tone
                rw = rh = 1 + gpu_model_next(seed) % 40, q[3] = (unsigned int)x0 + rw, q[4] = (unsigned int)y0 + rh;
                q[0] = 6 | rw << 8 | GM_SRC_PITCH << 16, q[5] = (unsigned int)cover, q[6] = (unsigned int)(src + 4 * at);
                q[7] = (unsigned int)((src + 4 * at) >> 32 & 0xff) | (colour & 0xffffffu) << 8;
                break;
        case 7: // a turn: steps of a pixel or a row either way, from the middle of the pictures
        {
                static const int steps[4][2] = {{1, GM_SRC_PITCH}, {-1, -GM_SRC_PITCH}, {GM_SRC_PITCH, -1}, {-GM_SRC_PITCH, 1}};
                unsigned int way = gpu_model_next(seed) % 4;
                positive middle = src + 4ull * (256 * GM_SRC_PITCH + 256);

                q[0] = 7 | (unsigned int)(steps[way][0] + 0x800000) << 8, q[5] = (unsigned int)middle, q[6] = (unsigned int)(middle >> 32), q[7] = (unsigned int)steps[way][1];
                break;
        }
        case 10: // a glyph translated: the coverage's pitch, the tones' place, base 0, the coverage's place
                q[0] = 10 | GM_SRC_PITCH << 8, q[5] = (unsigned int)tones, q[7] = (unsigned int)cover;
                break;
        }
        return count;
}

/*
        scenes scenes from seed, each drawn by one and by two over the same random picture: the pixels that differ (and the first in difference); a
        scene a drawer refuses, or on which the model fails, is counted in difference->failed and not compared.
*/
static positive gpu_model_card_differential(gpu_model_drawer one, gpu_model_drawer two, unsigned int seed, unsigned int scenes, struct gpu_model_difference address_to difference)
{
        unsigned int address_to ops = (unsigned int address_to)((p8 address_to)gpu.at + GM_AREA(0)), address_to a = (unsigned int address_to)((p8 address_to)gpu.at + GM_AREA(2));
        unsigned int address_to b = (unsigned int address_to)((p8 address_to)gpu.at + GM_AREA(3)), address_to pixels = (unsigned int address_to)((p8 address_to)gpu.at + GM_AREA(4));
        p8 address_to covers = (p8 address_to)gpu.at + GM_AREA(5);
        positive wrong = 0;

        memory_zero(difference, sizeof(*difference));
        if (!gpu.at || gpu.size < GM_AREA(6) + (1 << 20))
                return difference->failed = 1, 0;
        for (unsigned int i = 0; i < GM_SRC_PITCH * GM_SRC_PITCH; i++)
                pixels[i] = gpu_model_next(&seed), covers[i] = (p8)(gpu_model_next(&seed) % 4 == 0 ? 0 : seed % 4 == 1 ? 255 : seed >> 24);
        for (unsigned int i = 0; i < 3 * 256; i++)
                ((unsigned int address_to)(covers + GM_SRC_PITCH * GM_SRC_PITCH))[i] = gpu_model_next(&seed);
        for (unsigned int scene = 0; scene < scenes; scene++)
        {
                unsigned int w = 4 * (1 + gpu_model_next(&seed) % 128), h = 1 + gpu_model_next(&seed) % 256, pitch = w + 4 * (gpu_model_next(&seed) % 4), n = 0;
                unsigned int records = 1 + gpu_model_next(&seed) % 24, dx0 = 0, dy0 = 0, dx1 = w, dy1 = h;
                b32 drawn;

                if (gpu_model_next(&seed) % 3 == 0)
                        dx0 = gpu_model_next(&seed) % w, dy0 = gpu_model_next(&seed) % h, dx1 = dx0 + 1 + gpu_model_next(&seed) % (w - dx0), dy1 = dy0 + 1 + gpu_model_next(&seed) % (h - dy0);
                for (unsigned int k = 0; k < records; k++)
                        n += gpu_model_record(&seed, ops + 8 * n, w, h);
                for (unsigned int i = 0; i < pitch * h; i++)
                        a[i] = b[i] = gpu_model_next(&seed);
                memory_zero(&gpu_model.fault, sizeof(gpu_model.fault));
                drawn = one(GM_AREA(0), n, GM_AREA(1), GM_AREA(2), pitch, w, h, dx0, dy0, dx1, dy1) && !gpu_model.fault.code;
                drawn = drawn && two(GM_AREA(0), n, GM_AREA(1), GM_AREA(3), pitch, w, h, dx0, dy0, dx1, dy1) && !gpu_model.fault.code;
                difference->scenes++;
                if (!drawn)
                {
                        difference->failed++;
                        continue;
                }
                difference->records += n, difference->pixels += (positive)pitch * h;
                for (unsigned int i = 0; i < pitch * h; i++)
                        if (a[i] != b[i] && !wrong++)
                                difference->scene = scene, difference->x = i % pitch, difference->y = i / pitch, difference->one = a[i], difference->two = b[i];
        }
        return wrong;
}
