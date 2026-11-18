/*
        The vector instructions of the wave: the ALU ops lane by lane where exec has the lane, the compares and carries into lane masks (a lane
        exec has off gets 0), the global loads and stores at the sizes gpuasm makes (a byte, a dword, four dwords), and ds_bpermute_b32.
*/

// A vector source: a register's row (a pair's two for 64 bits), or one value for every lane.
struct gpu_model_src
{
        const p32 *row, *high;
        p64 value;
};

#define GM_LO(src, lane) ((src).row ? (src).row[lane] : (p32)(src).value)
#define GM_WIDE(src, lane) ((src).row ? (src).row[lane] | ((src).high ? (p64)(src).high[lane] << 32 : 0) : (src).value)

static int gpu_model_vsource(p16 code, unsigned int size, p32 literal, struct gpu_model_src address_to out)
{
        int e;

        out->row = out->high = null, out->value = 0;
        if (code < 256)
                return gpu_model_sread(code, size, literal, &out->value);
        if ((e = gpu_model_vgprs_ready(code - 256u, size)))
                return e;
        out->row = gpu_model.v[code - 256], out->high = size == 2 ? gpu_model.v[code - 255] : null;
        return 0;
}

static int gpu_model_unmodelled(const char *what)
{
        if (gpu_model_fail(GPU_MODEL_UNMODELLED, what))
                return gpu_model.fault.code;
        return gpu_model_fail_end();
}

static int gpu_model_valu(const struct gpu_model_insn address_to in, const struct gpu_model_name address_to row)
{
        const char *shape = row->shape;
        unsigned int key = GM_KEY(row->form, row->code), sources = 0, mask_at = 0, carry_at = 0;
        p64 exec = gpu_model.s[126] | (p64)gpu_model.s[127] << 32, mask = 0;
        struct gpu_model_src src[3];
        p32 *dst = null, *high = null;
        int e;

        // The rows that take a clamp the model runs: the integer adds' saturation, and the packed ops'.
        if (in->clamp && key != GM_KEY(GM_VOP2, 0x25) && key != GM_KEY(GM_VOP2, 0x26) && key != GM_KEY(GM_VOP2, 0x27) && row->form != GM_VOP3P)
                return gpu_model_unmodelled("clamp on an instruction the model does not run it for");
        for (unsigned int k = 0; k < in->count; k++)
        {
                p8 letter = (p8)shape[2 * k];
                unsigned int size = (unsigned int)(shape[2 * k + 1] - '0');

                if (letter == 'm' || letter == 'n')
                        carry_at = k; // a compare's mask, a carry out
                else if (letter == 'd')
                {
                        if ((e = gpu_model_vgprs_ready(in->op[k] - 256u, size)))
                                return e;
                        dst = gpu_model.v[in->op[k] - 256], high = size == 2 ? gpu_model.v[in->op[k] - 255] : null;
                }
                else if ((e = gpu_model_vsource(in->op[k], size, in->literal, src + sources)))
                        return e;
                else if (letter == 'k')
                        mask_at = ++sources; // a lane mask read: v_cndmask_b32's selector, a carry in
                else
                        sources++;
        }
        gpu_model.cost.valu++;
        for (unsigned int lane = 0; lane < 64; lane++)
        {
                p32 a, b, c, d = 0;
                p64 wide = 0;
                b32 bit = 0, select;

                if (!(exec >> lane & 1))
                        continue;
                a = GM_LO(src[0], lane), b = sources > 1 ? GM_LO(src[1], lane) : 0, c = sources > 2 ? GM_LO(src[2], lane) : 0;
                select = mask_at ? (b32)(src[mask_at - 1].value >> lane & 1) : 0;
                gpu_model.cost.lanes++;
                switch (key)
                {
                case GM_KEY(GM_VOP1, 0x01): d = a; break;
                case GM_KEY(GM_VOP1, 0x06): d = gpu_model_cvt_f32_u32(a); break;
                case GM_KEY(GM_VOP1, 0x07): d = gpu_model_cvt_u32_f32(a); break;
                case GM_KEY(GM_VOP1, 0x2a): d = gpu_model_nudged(gpu_model_rcp_f32(a), lane, gpu_model.x, gpu_model.y); break;
                case GM_KEY(GM_VOP1, 0x33): d = gpu_model_nudged(gpu_model_sqrt_f32(a), lane, gpu_model.x, gpu_model.y); break;
                case GM_KEY(GM_VOP2, 0x25): wide = (p64)a + b, d = in->clamp && wide >> 32 ? 0xffffffffu : (p32)wide; break;
                case GM_KEY(GM_VOP2, 0x26): d = in->clamp && b > a ? 0 : a - b; break;
                case GM_KEY(GM_VOP2, 0x27): d = in->clamp && a > b ? 0 : b - a; break;
                case GM_KEY(GM_VOP2, 0x1b): d = a & b; break;
                case GM_KEY(GM_VOP2, 0x1d): d = a ^ b; break;
                case GM_KEY(GM_VOP2, 0x16): d = b >> (a & 31); break;
                case GM_KEY(GM_VOP2, 0x1a): d = b << (a & 31); break;
                case GM_KEY(GM_VOP2, 0x18): d = (p32)((b32)b >> (a & 31)); break;
                case GM_KEY(GM_VOP2, 0x12): d = (b32)a > (b32)b ? a : b; break;
                case GM_KEY(GM_VOP2, 0x11): d = (b32)a < (b32)b ? a : b; break;
                case GM_KEY(GM_VOP2, 0x14): d = a > b ? a : b; break;
                case GM_KEY(GM_VOP2, 0x13): d = a < b ? a : b; break;
                case GM_KEY(GM_VOP2, 0x0b): d = (a & 0xffffffu) * (b & 0xffffffu); break;
                case GM_KEY(GM_VOP2, 0x08): d = gpu_model_mul_f32(a, b); break;
                case GM_KEY(GM_VOPC, 0xc2): bit = a == b; break;
                case GM_KEY(GM_VOPC, 0xc5): bit = a != b; break;
                case GM_KEY(GM_VOPC, 0xc4): bit = a > b; break;
                case GM_KEY(GM_VOPC, 0xc3): bit = a <= b; break;
                case GM_KEY(GM_VOPC, 0xc1): bit = a < b; break;
                case GM_KEY(GM_VOPC, 0xc6): bit = a >= b; break;
                case GM_KEY(GM_VOPC, 0x84): bit = (b32)a > (b32)b; break;
                case GM_KEY(GM_VOPC, 0x83): bit = (b32)a <= (b32)b; break;
                case GM_KEY(GM_VOPC, 0x81): bit = (b32)a < (b32)b; break;
                case GM_KEY(GM_VOPC, 0x86): bit = (b32)a >= (b32)b; break;
                case GM_KEY(GM_VOPC, 0xe4): bit = GM_WIDE(src[0], lane) > GM_WIDE(src[1], lane); break;
                case GM_KEY(GM_VOPC, 0xe3): bit = GM_WIDE(src[0], lane) <= GM_WIDE(src[1], lane); break;
                case GM_KEY(GM_VOP3, 0x101): d = select ? b : a; break;
                case GM_KEY(GM_VOP3, 0x143): d = (a & 0xffffffu) * (b & 0xffffffu) + c; break;
                case GM_KEY(GM_VOP3, 0x36d): d = a + b + c; break;
                case GM_KEY(GM_VOP3, 0x36f): d = (a << (b & 31)) | c; break;
                case GM_KEY(GM_VOP3, 0x346): d = (a << (b & 31)) + c; break;
                case GM_KEY(GM_VOP3, 0x14e): d = (p32)(((p64)a << 32 | b) >> (c & 31)); break;
                case GM_KEY(GM_VOP3, 0x344): // v_perm_b32: each byte of d is the byte of {a, b} (a the high word) c's byte names; 8 to 11 the sign of byte 1, 3, 5 or 7 as 0 or ff, 12 zero, 13 and on ff
                {
                        p64 both = (p64)a << 32 | b;

                        d = 0;
                        for (unsigned int at = 0; at < 4; at++)
                        {
                                unsigned int sel = (c >> (8 * at)) & 255, byte;

                                byte = sel < 8 ? (unsigned int)(both >> (8 * sel)) & 255 : sel < 12 ? ((both >> (8 * (2 * (sel - 8) + 1) + 7)) & 1 ? 255u : 0u) : sel == 12 ? 0u : 255u;
                                d |= (p32)byte << (8 * at);
                        }
                        break;
                }
                case GM_KEY(GM_VOP3, 0x148): d = c & 31 ? (a >> (b & 31)) & ((1u << (c & 31)) - 1) : 0; break;
                case GM_KEY(GM_VOP3, 0x169): d = a * b; break;
                case GM_KEY(GM_VOP3B, 0x176):
                {
                        p64 product = (p64)a * b, sum = product + GM_WIDE(src[2], lane);

                        bit = sum < product, d = (p32)sum, high[lane] = (p32)(sum >> 32);
                        break;
                }
                case GM_KEY(GM_VOP3B, 0x30f): wide = (p64)a + b, d = (p32)wide, bit = (b32)(wide >> 32); break;
                case GM_KEY(GM_VOP2C, 0x28): wide = (p64)a + b + (p32)select, d = (p32)wide, bit = (b32)(wide >> 32); break;
                case GM_KEY(GM_VOP3P, 0x0d): d = ((a & 0xffffu) < (b & 0xffffu) ? a & 0xffffu : b & 0xffffu) | ((a >> 16) < (b >> 16) ? a >> 16 : b >> 16) << 16; break;
                case GM_KEY(GM_VOP3P, 0x0b):
                {
                        p32 low = (a & 0xffffu) - (b & 0xffffu), top = (a >> 16) - (b >> 16);

                        if (in->clamp)
                                low = (a & 0xffffu) < (b & 0xffffu) ? 0 : low, top = (a >> 16) < (b >> 16) ? 0 : top;
                        d = (low & 0xffffu) | top << 16;
                        break;
                }
                default: return gpu_model_unmodelled("an instruction the model has no meaning for");
                }
                if (dst)
                        dst[lane] = d;
                mask |= (p64)(bit & 1) << lane;
        }
        // The lane mask a compare or a carry writes: the bits of the lanes exec has, the others 0.
        if (carry_at || row->form == GM_VOPC)
                return gpu_model_swrite(in->op[carry_at], 2, gpu_model.doubt & GPU_MODEL_DOUBT_INACTIVE ? mask | ~exec : mask);
        return 0;
}

// global_load_ubyte, _dword, _dwordx4 and global_store_dword, _dwordx4: each lane exec has at the base pair plus its register (or at its
// register pair with off) plus the offset, its bytes read or written as they lie, at any alignment.
static int gpu_model_vmem(const struct gpu_model_insn address_to in, const struct gpu_model_name address_to row)
{
        b32 store = row->form == GM_GST, off = in->op[2] == 125;
        unsigned int bytes = row->code == 0x08 ? 1 : row->code == 0x0e || row->code == 0x1e ? 16 : 4, regs = bytes == 16 ? 4 : 1;
        unsigned int address = (store ? in->op[0] : in->op[1]) - 256u, data = (store ? in->op[1] : in->op[0]) - 256u;
        p64 exec = gpu_model.s[126] | (p64)gpu_model.s[127] << 32, base = 0;
        int e;

        if ((e = gpu_model_vgprs_ready(address, off ? 2 : 1)) || (e = gpu_model_vgprs_ready(data, regs)) || (!off && (e = gpu_model_sread(in->op[2], 2, 0, &base))))
                return e;
        for (unsigned int lane = 0; lane < 64; lane++)
        {
                p32 words[4] = {0, 0, 0, 0};
                positive va;

                if (!(exec >> lane & 1))
                        continue;
                va = (off ? gpu_model.v[address][lane] | (p64)gpu_model.v[address + 1][lane] << 32 : base + gpu_model.v[address][lane]) + (positive)(b64)in->imm;
                if (gpu_model.doubt & GPU_MODEL_DOUBT_ALIGN && !store && bytes > 1)
                        va &= ~3ull;
                if (store)
                {
                        for (unsigned int r = 0; r < regs; r++)
                                words[r] = gpu_model.v[data + r][lane];
                        if ((e = gpu_model_store(va, bytes, (const p8 address_to)words, lane)))
                                return e;
                        gpu_model.cost.lane_stores++, gpu_model.cost.bytes_stored += bytes;
                        continue;
                }
                if ((e = gpu_model_load(va, bytes, (p8 address_to)words, lane)))
                        return e;
                for (unsigned int r = 0; r < regs; r++)
                        gpu_model.v[data + r][lane] = words[r];
                gpu_model.cost.lane_loads++, gpu_model.cost.bytes_loaded += bytes;
        }
        if (store)
                gpu_model.cost.vmem_stores++;
        else
                gpu_model.cost.vmem_loads++, gpu_model_issue(0, true, data, regs);
        return 0;
}

/*
        image_load of a 2D image's four channels at each lane's whole coordinates (x, y from the address pair), from its resource's eight words
        in the scalar registers: what the model takes is the one layout a processor can name, SW_MODE 0 (linear), uncompressed, of the format
        8_8_8_8 UNORM (56), type 2D (9), one level, a width that is a multiple of 64 with DEPTH 0 or the width less one (so that the row's
        pitch is the width under either reading of the field on gfx10.3), the channels' selects X, Y, Z, W or any order of them. Each channel
        is its byte over 255 as a float, rounded to nearest; a lane outside the image reads zeros, as the card's does. Any other resource
        (a tiled swizzle, DCC, another format) is UNMODELLED: the card's own addressing of those is not the model's to say.
*/
static int gpu_model_image(const struct gpu_model_insn address_to in)
{
        unsigned int data = in->op[0] - 256u, address = in->op[1] - 256u, rs = in->op[2];
        p64 exec = gpu_model.s[126] | (p64)gpu_model.s[127] << 32;
        p32 r[8];
        positive base;
        unsigned int width, height, depth, sel[4];
        int e;

        if ((e = gpu_model_vgprs_ready(address, 2)) || (e = gpu_model_vgprs_ready(data, 4)) || (e = gpu_model_sgprs_ready(rs, 8)))
                return e;
        for (unsigned int k = 0; k < 8; k++)
                r[k] = gpu_model.s[rs + k];
        base = ((positive)r[0] | (positive)(r[1] & 0xff) << 32) << 8;
        width = ((r[1] >> 30) | (r[2] & 0xfff) << 2) + 1, height = (r[2] >> 14 & 0x3fff) + 1, depth = r[4] & 0x1fff;
        for (unsigned int c = 0; c < 4; c++)
                sel[c] = r[3] >> (3 * c) & 7;
        if ((r[1] >> 20 & 0x1ff) != 56 || (r[3] >> 20 & 0x1f) != 0 || (r[3] >> 28) != 9 || (r[3] >> 12 & 0xff) != 0 || (r[6] >> 21 & 1) || r[7] ||
            (width & 63) || (depth && depth + 1 != width) || sel[0] < 4 || sel[1] < 4 || sel[2] < 4 || sel[3] < 4)
                return gpu_model_fail(GPU_MODEL_UNMODELLED, "an image resource the model does not read (not linear 8_8_8_8 UNORM 2D)") ? gpu_model.fault.code
                                                                                                                                  : gpu_model_fail_end();
        for (unsigned int lane = 0; lane < 64; lane++)
        {
                p32 x = gpu_model.v[address][lane], y = gpu_model.v[address + 1][lane];
                p8 px[4] = {0, 0, 0, 0};

                if (!(exec >> lane & 1))
                        continue;
                if (x < width && y < height && (e = gpu_model_load(base + 4 * ((positive)y * width + x), 4, px, lane)))
                        return e;
                for (unsigned int c = 0; c < 4; c++)
                {
                        p64 scaled = (p64)px[sel[c] - 4] << 40;

                        gpu_model.v[data + c][lane] = gpu_model_pack(0, scaled / 255, -40, scaled % 255 != 0);
                }
                gpu_model.cost.lane_loads++, gpu_model.cost.bytes_loaded += 4;
        }
        gpu_model.cost.vmem_loads++, gpu_model_issue(0, true, data, 4);
        return 0;
}

// ds_bpermute_b32: each lane exec has takes the datum of the lane its address names (the address over 4, modulo 64), 0 from a lane exec has
// off; the lanes are read before any is written. A lane of one half of the wave that names one of the other is refused: gfx10 wave64 runs
// the permute as two halves, which the model has not seen confirmed.
static int gpu_model_ds(const struct gpu_model_insn address_to in)
{
        unsigned int dst = in->op[0] - 256u, address = in->op[1] - 256u, data = in->op[2] - 256u;
        p64 exec = gpu_model.s[126] | (p64)gpu_model.s[127] << 32;
        p32 taken[64];
        int e;

        if ((e = gpu_model_vgprs_ready(address, 1)) || (e = gpu_model_vgprs_ready(data, 1)) || (e = gpu_model_vgprs_ready(dst, 1)))
                return e;
        for (unsigned int lane = 0; lane < 64; lane++)
        {
                unsigned int from = gpu_model.v[address][lane] >> 2 & 63;

                if (!(exec >> lane & 1))
                        continue;
                if ((from ^ lane) & 32)
                        return gpu_model_unmodelled("a permute that reads across the halves of the wave");
                taken[lane] = exec >> from & 1 || gpu_model.doubt & GPU_MODEL_DOUBT_PERMUTE ? gpu_model.v[data][from] : 0;
        }
        for (unsigned int lane = 0; lane < 64; lane++)
                if (exec >> lane & 1)
                        gpu_model.v[dst][lane] = taken[lane], gpu_model.cost.lanes++;
        gpu_model_issue(1, true, dst, 1);
        gpu_model.cost.ds++;
        return 0;
}
