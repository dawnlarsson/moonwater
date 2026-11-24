/*
        The wave64 interpreter: the registers, the waits, the scalar instructions, the branches, the wave's loop, and the dispatch, which
        runs the grid a wave at a time as gpu_amd_submit's packets ask (gpu_model/valu.c has the vector instructions). The scalar registers
        are kept by their codes: s0 to s105, vcc at 106 and 107, exec at 126 and 127.
*/

static struct gpu_model_insn gpu_model_decoded[GPU_MODEL_CODE];
static p8 gpu_model_known[GPU_MODEL_CODE];

// The loads not waited for: how many are outstanding into each register, and each counter's list in the order they were issued
// (vmcnt the vector loads; lgkmcnt the scalar loads, which the card ends in any order, and the permutes, in order).
static struct
{
        p8 v[256], s[128];
        struct
        {
                p16 first;
                p8 count, vector;
        } list[2][64];
        unsigned int count[2];
} gpu_model_pend;

static const p32 gpu_model_floats[9] = {0x3f000000u, 0xbf000000u, 0x3f800000u, 0xbf800000u, 0x40000000u, 0xc0000000u, 0x40800000u, 0xc0800000u, 0x3e22f983u};

static int gpu_model_valu(const struct gpu_model_insn address_to in, const struct gpu_model_name address_to row);
static int gpu_model_vmem(const struct gpu_model_insn address_to in, const struct gpu_model_name address_to row);
static int gpu_model_ds(const struct gpu_model_insn address_to in);
static int gpu_model_image(const struct gpu_model_insn address_to in);

static int gpu_model_refused(int code, const char *what, unsigned int reg, b32 vector)
{
        if (gpu_model_fail(code, what))
                return gpu_model.fault.code;
        gpu_model_put(vector ? " v" : " s");
        gpu_model_number(reg, 10);
        if (code == GPU_MODEL_REGISTER)
                gpu_model_put(" (the wave has "), gpu_model_number(gpu_model.vgprs, 10), gpu_model_put(")");
        return gpu_model_fail_end();
}

// Registers an instruction reads or writes: there, and no load into them outstanding.
static int gpu_model_vgprs_ready(unsigned int reg, unsigned int count)
{
        for (unsigned int r = reg; r < reg + count; r++)
        {
                if (r >= gpu_model.vgprs)
                        return gpu_model_refused(GPU_MODEL_REGISTER, "a vector register past the ones the first resource word allocates:", r, true);
                if (gpu_model_pend.v[r])
                        return gpu_model_refused(GPU_MODEL_WAIT, "a register read or written before the wait for the load into it:", r, true);
        }
        return 0;
}

static int gpu_model_sgprs_ready(unsigned int code, unsigned int count)
{
        for (unsigned int r = code; r < code + count && r <= 105; r++)
                if (gpu_model_pend.s[r])
                        return gpu_model_refused(GPU_MODEL_WAIT, "a register read or written before the wait for the load into it:", r, false);
        return 0;
}

// The oldest load of a counter's list ended.
static void gpu_model_retire(unsigned int counter)
{
        unsigned int n = gpu_model_pend.count[counter];

        if (!n)
                return;
        for (unsigned int r = 0; r < gpu_model_pend.list[counter][0].count; r++)
        {
                unsigned int at = gpu_model_pend.list[counter][0].first + r;

                if (gpu_model_pend.list[counter][0].vector)
                        gpu_model_pend.v[at]--;
                else
                        gpu_model_pend.s[at]--;
        }
        memory_copy(gpu_model_pend.list[counter], gpu_model_pend.list[counter] + 1, (n - 1) * sizeof(gpu_model_pend.list[counter][0]));
        gpu_model_pend.count[counter] = n - 1;
}

// A load issued into count registers from first (vector ones or scalar ones), on counter 0 (vmcnt) or 1 (lgkmcnt). A list that is full is
// one the card waits on before it issues another: its oldest ends.
static void gpu_model_issue(unsigned int counter, b32 vector, unsigned int first, unsigned int count)
{
        unsigned int n;

        if (gpu_model_pend.count[counter] == 64)
                gpu_model_retire(counter);
        n = gpu_model_pend.count[counter]++;
        gpu_model_pend.list[counter][n].first = (p16)first, gpu_model_pend.list[counter][n].count = (p8)count, gpu_model_pend.list[counter][n].vector = (p8)vector;
        for (unsigned int r = first; r < first + count; r++)
        {
                if (vector)
                        gpu_model_pend.v[r]++;
                else
                        gpu_model_pend.s[r]++;
        }
}

// s_waitcnt: vmcnt in bits 0 to 3 and 14 to 15, lgkmcnt in 8 to 13. The vector loads end in order; of lgkmcnt's, only a count of 0 says
// which have ended while a scalar load is outstanding (they end in any order).
static void gpu_model_wait(p32 imm)
{
        unsigned int vm = (imm & 15) | (imm >> 14 & 3) << 4, lgkm = imm >> 8 & 63;
        b32 scalar = false;

        while (gpu_model_pend.count[0] > vm)
                gpu_model_retire(0);
        for (unsigned int k = 0; k < gpu_model_pend.count[1]; k++)
                scalar = scalar || !gpu_model_pend.list[1][k].vector;
        while (gpu_model_pend.count[1] > lgkm && (!scalar || !lgkm))
                gpu_model_retire(1);
}

// A scalar source: a register (or a pair), vcc, exec, an inline constant, a float constant, the literal.
static int gpu_model_sread(p16 code, unsigned int size, p32 literal, p64 address_to value)
{
        int e;

        if (code <= 107 || code == 126 || code == 127)
        {
                if ((e = gpu_model_sgprs_ready(code, size)))
                        return e;
                *value = gpu_model.s[code] | (size == 2 ? (p64)gpu_model.s[code + 1] << 32 : 0);
        }
        else if (code >= 128 && code <= 192)
                *value = code - 128u;
        else if (code >= 193 && code <= 208)
                *value = size == 2 ? (p64) - (b64)(code - 192) : (p32) - (b32)(code - 192);
        else if (code >= 240 && code <= 248)
                *value = gpu_model_floats[code - 240];
        else
                *value = literal;
        return 0;
}

static int gpu_model_swrite(p16 code, unsigned int size, p64 value)
{
        int e;

        if (code == 125)
                return 0;
        if ((e = gpu_model_sgprs_ready(code, size)))
                return e;
        gpu_model.s[code] = (p32)value;
        if (size == 2)
                gpu_model.s[code + 1] = (p32)(value >> 32);
        return 0;
}

#define GM_KEY(form, code) ((unsigned int)(form) << 12 | (unsigned int)(code))

static int gpu_model_salu(const struct gpu_model_insn address_to in, const struct gpu_model_name address_to row)
{
        unsigned int first = row->form == GM_SOPC ? 0 : 1, size = (unsigned int)(row->shape[2 * first + 1] - '0'), scc = gpu_model.scc;
        p64 a = 0, b = 0, d = 0, exec;
        p32 x, y;
        int e;

        if ((e = gpu_model_sread(in->op[first], size, in->literal, &a)) || (in->count > first + 1 && (e = gpu_model_sread(in->op[first + 1], size, in->literal, &b))))
                return e;
        x = (p32)a, y = (p32)b;
        gpu_model.cost.salu++;
        switch (GM_KEY(row->form, row->code))
        {
        case GM_KEY(GM_SOP1, 0x03): d = x; break;
        case GM_KEY(GM_SOP1, 0x04): d = a; break;
        case GM_KEY(GM_SOP1, 0x24):
                d = exec = gpu_model.s[126] | (p64)gpu_model.s[127] << 32, exec &= a;
                gpu_model.s[126] = (p32)exec, gpu_model.s[127] = (p32)(exec >> 32), scc = gpu_model.doubt & GPU_MODEL_DOUBT_SCC ? d != 0 : exec != 0;
                break;
        case GM_KEY(GM_SOP2, 0x00): d = (p64)x + y, scc = (unsigned int)(d >> 32), d = (p32)d; break;
        case GM_KEY(GM_SOP2, 0x01): d = (p32)(x - y), scc = y > x; break;
        case GM_KEY(GM_SOP2, 0x02): d = (p32)(x + y), scc = ((x ^ ~y) & (x ^ (p32)d)) >> 31; break;
        case GM_KEY(GM_SOP2, 0x03): d = (p32)(x - y), scc = ((x ^ y) & (x ^ (p32)d)) >> 31; break;
        case GM_KEY(GM_SOP2, 0x04): d = (p64)x + y + (scc & 1), scc = (unsigned int)(d >> 32), d = (p32)d; break;
        case GM_KEY(GM_SOP2, 0x06): scc = (b32)x < (b32)y, d = scc ? x : y; break;
        case GM_KEY(GM_SOP2, 0x07): scc = x < y, d = scc ? x : y; break;
        case GM_KEY(GM_SOP2, 0x0e): d = x & y, scc = d != 0; break;
        case GM_KEY(GM_SOP2, 0x10): d = x | y, scc = d != 0; break;
        case GM_KEY(GM_SOP2, 0x1e): d = (p32)(x << (y & 31)), scc = d != 0; break;
        case GM_KEY(GM_SOP2, 0x20): d = x >> (y & 31), scc = d != 0; break;
        case GM_KEY(GM_SOP2, 0x26): d = (p32)(x * y); break;
        case GM_KEY(GM_SOP2, 0x27):
        {
                p32 width = y >> 16 & 127;

                d = (x >> (y & 31)) & (width >= 32 ? 0xffffffffu : (1u << width) - 1), scc = d != 0;
                break;
        }
        case GM_KEY(GM_SOP2, 0x0f): d = a & b, scc = d != 0; break;
        case GM_KEY(GM_SOP2, 0x11): d = a | b, scc = d != 0; break;
        case GM_KEY(GM_SOP2, 0x15): d = a & ~b, scc = d != 0; break;
        case GM_KEY(GM_SOPC, 0x00): scc = x == y; break;
        case GM_KEY(GM_SOPC, 0x02): scc = (b32)x > (b32)y; break;
        case GM_KEY(GM_SOPC, 0x03): scc = (b32)x >= (b32)y; break;
        case GM_KEY(GM_SOPC, 0x04): scc = (b32)x < (b32)y; break;
        case GM_KEY(GM_SOPC, 0x05): scc = (b32)x <= (b32)y; break;
        case GM_KEY(GM_SOPC, 0x06): scc = x == y; break;
        case GM_KEY(GM_SOPC, 0x07): scc = x != y; break;
        case GM_KEY(GM_SOPC, 0x08): scc = x > y; break;
        case GM_KEY(GM_SOPC, 0x09): scc = x >= y; break;
        case GM_KEY(GM_SOPC, 0x0a): scc = x < y; break;
        case GM_KEY(GM_SOPC, 0x0b): scc = x <= y; break;
        case GM_KEY(GM_SOPC, 0x0c): scc = !(x >> (y & 31) & 1); break;
        case GM_KEY(GM_SOPC, 0x0d): scc = x >> (y & 31) & 1; break;
        default: return gpu_model_fail(GPU_MODEL_UNMODELLED, "a scalar instruction the model has no meaning for") ? gpu_model.fault.code : gpu_model_fail_end();
        }
        gpu_model.scc = scc & 1;
        if (row->form == GM_SOPC)
                return 0;
        return gpu_model_swrite(in->op[0], (unsigned int)(row->shape[1] - '0'), d);
}

// s_load_dword and its wider forms: the dwords at the pair's address plus the offset, the low two bits dropped.
static int gpu_model_smem(const struct gpu_model_insn address_to in, const struct gpu_model_name address_to row)
{
        unsigned int count = 1u << row->code;
        p64 base = 0;
        p32 words[8];
        int e;

        if ((e = gpu_model_sread(in->op[1], 2, 0, &base)) || (e = gpu_model_sgprs_ready(in->op[0], count)))
                return e;
        base = (base + (p64)(b64)in->imm) & ~3ull;
        if ((e = gpu_model_load(base, 4 * count, (p8 address_to)words, 0)))
                return e;
        memory_copy(gpu_model.s + in->op[0], words, 4 * count);
        gpu_model_issue(1, false, in->op[0], count);
        gpu_model.cost.scalar_bytes += 4 * count;
        return 0;
}

// One wave of the grid, at workgroup x, y: its registers as the dispatch sets them, then its instructions to s_endpgm.
static int gpu_model_wave(unsigned int x, unsigned int y, const p32 address_to user, unsigned int users)
{
        int e = 0;

        gpu_model.x = x, gpu_model.y = y, gpu_model.pc = gpu_model.code_va;
        for (unsigned int r = 0; r < 128; r++)
                gpu_model.s[r] = gpu_model.poison;
        for (unsigned int r = 0; r < gpu_model.vgprs; r++)
                for (unsigned int lane = 0; lane < 64; lane++)
                        gpu_model.v[r][lane] = r ? gpu_model.poison : lane;
        memory_copy(gpu_model.s, user, 4 * users);
        gpu_model.s[users] = x, gpu_model.s[users + 1] = y, gpu_model.s[126] = gpu_model.s[127] = 0xffffffffu, gpu_model.scc = gpu_model.poison & 1;
        memory_zero(&gpu_model_pend, sizeof(gpu_model_pend));
        gpu_model.cost.waves++;
        for (unsigned int ran = 0;; ran++)
        {
                positive offset = gpu_model.pc - gpu_model.code_va, next;
                unsigned int at = (unsigned int)(offset / 4);
                const struct gpu_model_insn *in;
                const struct gpu_model_name *row;

                if (ran >= GPU_MODEL_BUDGET)
                        return gpu_model_fail(GPU_MODEL_RUNAWAY, "a wave past its budget of instructions") ? gpu_model.fault.code : gpu_model_fail_end();
                if (gpu_model.pc < gpu_model.code_va || (offset & 3) || offset / 4 >= gpu_model.code_words)
                        return gpu_model_bounds(gpu_model.pc, 4, GPU_MODEL_RUN, 0);
                if (!gpu_model_known[at])
                {
                        if ((e = gpu_model_decode(gpu_model.code + at, gpu_model.code_words - at, gpu_model.pc, gpu_model_decoded + at)))
                                return e;
                        gpu_model_known[at] = 1;
                }
                in = gpu_model_decoded + at, row = gpu_model_names + in->id;
                gpu_model.hits[at]++, gpu_model.cost.instructions++;
                next = gpu_model.pc + 4ull * in->words;
                switch (row->form)
                {
                case GM_SOPP:
                {
                        p64 exec = gpu_model.s[126] | (p64)gpu_model.s[127] << 32;
                        b32 taken = row->code == 0x02 || (row->code == 0x04 && !gpu_model.scc) || (row->code == 0x05 && gpu_model.scc) || (row->code == 0x08 && !exec);

                        gpu_model.cost.salu++;
                        if (row->code == 0x01)
                                return 0;
                        if (row->code == 0x0c)
                                gpu_model_wait((p32)in->imm & 0xffff);
                        if (taken)
                                next = gpu_model.pc + 4 + (positive)((b64)in->imm * 4);
                        break;
                }
                case GM_SOP1:
                case GM_SOP2:
                case GM_SOPC: e = gpu_model_salu(in, row); break;
                case GM_SMEM: e = gpu_model_smem(in, row); break;
                case GM_DS: e = gpu_model_ds(in); break;
                case GM_MIMG: e = gpu_model_image(in); break;
                case GM_GLD:
                case GM_GST: e = gpu_model_vmem(in, row); break;
                default: e = gpu_model_valu(in, row); break;
                }
                if (e)
                        return e;
                gpu_model.pc = next;
        }
}

/*
        A dispatch as gpu_amd_submit sends one: the code (words of it readable from code_va, at code in the processor's memory), the two resource
        words, the user data, the grid. The resource words are held to the ones gpuasm writes (wave64, the float mode 0xc0, six user registers,
        the workgroup's x and y and no z, no scratch, no LDS, the lane's x alone) save the vector registers, which the first allocates in fours.
*/
static int gpu_model_dispatch(const p32 address_to code, unsigned int words, positive code_va, p32 rsrc1, p32 rsrc2, const p32 address_to user, unsigned int columns,
                              unsigned int rows)
{
        int e = 0;

        gpu_model.code = code, gpu_model.code_words = words < GPU_MODEL_CODE ? words : GPU_MODEL_CODE, gpu_model.code_va = code_va, gpu_model.pc = code_va;
        gpu_model.vgprs = ((rsrc1 & 63) + 1) * 4;
        // gfx9's first word has no forward progress, memory ordering or workgroup mode (the top three bits), and has the scalar registers' count in eights
        if ((gpu_model.isa == 9 ? (rsrc1 & ~0x3ffu) != 0x00ac0000u : (rsrc1 & ~63u) != 0xe0ac0000u) || rsrc2 != 0x18cu)
        {
                if (gpu_model_fail(GPU_MODEL_DISPATCH, "resource words the model does not take: "))
                        return gpu_model.fault.code;
                gpu_model_number(rsrc1, 16), gpu_model_put(" "), gpu_model_number(rsrc2, 16);
                return gpu_model.fault.code;
        }
        memory_zero(gpu_model_known, sizeof(gpu_model_known));
        memory_zero(gpu_model.hits, sizeof(gpu_model.hits));
        for (unsigned int y = 0; y < rows && !e; y++)
                for (unsigned int x = 0; x < columns && !e; x++)
                        e = gpu_model_wave(x, y, user, (rsrc2 >> 1) & 31);
        return e;
}

// The entry P2 calls: kernel is its two resource words and then its code (words in all), run over columns by rows workgroups with the
// arguments' block at args_va (s[4:5]; s[0:3] are 0, as gpu_amd_submit sends them) in the card's address space the regions are.
static int gpu_model_run(const unsigned int address_to kernel, unsigned int words, positive args_va, unsigned int columns, unsigned int rows,
                         const struct gpu_model_region address_to regions, unsigned int count)
{
        const p32 user[6] = {0, 0, 0, 0, (p32)args_va, (p32)(args_va >> 32)};

        memory_zero(&gpu_model.fault, sizeof(gpu_model.fault)), memory_zero(&gpu_model.cost, sizeof(gpu_model.cost));
        gpu_model.region = regions, gpu_model.regions = count, gpu_model.last = 0, gpu_model.code_va = 0;
        if (words < 3)
        {
                gpu_model_fail(GPU_MODEL_DISPATCH, "a kernel with no code");
                return gpu_model.fault.code;
        }
        return gpu_model_dispatch(kernel + 2, words - 2, 0, kernel[0], kernel[1], user, columns, rows);
}
