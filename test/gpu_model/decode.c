/*
        The model's decoder: an instruction word (and the words after it) to the instruction gpuasm's table names, its operands in the
        order the table writes them, and back (gpu_model_encode, which makes the words as src/build/gpuasm.c makes them, for the row that
        every word of every kernel goes there and back unchanged).

        A word is decoded as the card decodes it: the encoding from the top bits, the opcode from its field, the operands from theirs. What
        is taken is what gpuasm can make and nothing more: each field the card would act on has to hold a value gpuasm writes there (the
        modifiers it never sets are zero, the packed op's selects are the ones it writes, a load's cache bits are clear, an operand is of the
        class the table's shape letter allows), so a word gpuasm cannot make is refused, with the word and its place, and never guessed at.
*/

enum gpu_model_form
{
        GM_SOP1,
        GM_SOP2,
        GM_SOPC,
        GM_SOPP,
        GM_SMEM,
        GM_VOP1,
        GM_VOP2,
        GM_VOP2C,
        GM_VOPC,
        GM_VOP3,
        GM_VOP3B,
        GM_VOP3P,
        GM_DS,
        GM_GLD,
        GM_GST,
        GM_MIMG
};

// gpuasm's table row for row (src/build/gpuasm.c, gpuasm_ops: the mnemonic, the encoding, the opcode there, the operands' shape), and
// s_nop, which gpuasm writes for .p2align. The order is the table's: a row's index is its id.
static const struct gpu_model_name
{
        const char *name;
        p8 form;
        p16 code;
        const char *shape;
} gpu_model_names[] = {
    {"s_mov_b32", GM_SOP1, 0x03, "p1i1"},          {"s_mov_b64", GM_SOP1, 0x04, "p2i2"},          {"s_and_saveexec_b64", GM_SOP1, 0x24, "p2i2"},
    {"s_add_u32", GM_SOP2, 0x00, "p1i1i1"},        {"s_sub_u32", GM_SOP2, 0x01, "p1i1i1"},        {"s_add_i32", GM_SOP2, 0x02, "p1i1i1"},
    {"s_sub_i32", GM_SOP2, 0x03, "p1i1i1"},        {"s_addc_u32", GM_SOP2, 0x04, "p1i1i1"},       {"s_min_i32", GM_SOP2, 0x06, "p1i1i1"},
    {"s_min_u32", GM_SOP2, 0x07, "p1i1i1"},        {"s_and_b32", GM_SOP2, 0x0e, "p1i1i1"},        {"s_or_b32", GM_SOP2, 0x10, "p1i1i1"},
    {"s_lshl_b32", GM_SOP2, 0x1e, "p1i1i1"},       {"s_lshr_b32", GM_SOP2, 0x20, "p1i1i1"},       {"s_mul_i32", GM_SOP2, 0x26, "p1i1i1"},
    {"s_bfe_u32", GM_SOP2, 0x27, "p1i1i1"},        {"s_and_b64", GM_SOP2, 0x0f, "p2i2i2"},        {"s_or_b64", GM_SOP2, 0x11, "p2i2i2"},
    {"s_andn2_b64", GM_SOP2, 0x15, "p2i2i2"},      {"s_cmp_eq_u32", GM_SOPC, 0x06, "i1i1"},       {"s_cmp_lg_u32", GM_SOPC, 0x07, "i1i1"},
    {"s_cmp_gt_u32", GM_SOPC, 0x08, "i1i1"},       {"s_cmp_ge_u32", GM_SOPC, 0x09, "i1i1"},       {"s_cmp_lt_u32", GM_SOPC, 0x0a, "i1i1"},
    {"s_cmp_le_u32", GM_SOPC, 0x0b, "i1i1"},       {"s_cmp_eq_i32", GM_SOPC, 0x00, "i1i1"},       {"s_cmp_gt_i32", GM_SOPC, 0x02, "i1i1"},
    {"s_cmp_ge_i32", GM_SOPC, 0x03, "i1i1"},       {"s_cmp_lt_i32", GM_SOPC, 0x04, "i1i1"},       {"s_cmp_le_i32", GM_SOPC, 0x05, "i1i1"},
    {"s_bitcmp0_b32", GM_SOPC, 0x0c, "i1i1"},      {"s_bitcmp1_b32", GM_SOPC, 0x0d, "i1i1"},      {"s_endpgm", GM_SOPP, 0x01, ""},
    {"s_waitcnt", GM_SOPP, 0x0c, "w1"},            {"s_branch", GM_SOPP, 0x02, "l1"},             {"s_cbranch_scc0", GM_SOPP, 0x04, "l1"},
    {"s_cbranch_scc1", GM_SOPP, 0x05, "l1"},       {"s_cbranch_execz", GM_SOPP, 0x08, "l1"},      {"s_load_dword", GM_SMEM, 0x00, "p1b2o1"},
    {"s_load_dwordx2", GM_SMEM, 0x01, "p2b2o1"},   {"s_load_dwordx4", GM_SMEM, 0x02, "p4b2o1"},   {"s_load_dwordx8", GM_SMEM, 0x03, "p8b2o1"},
    {"v_mov_b32", GM_VOP1, 0x01, "d1s1"},          {"v_cvt_f32_u32", GM_VOP1, 0x06, "d1s1"},      {"v_cvt_u32_f32", GM_VOP1, 0x07, "d1s1"},
    {"v_rcp_f32", GM_VOP1, 0x2a, "d1s1"},          {"v_sqrt_f32", GM_VOP1, 0x33, "d1s1"},         {"v_add_nc_u32", GM_VOP2, 0x25, "d1s1v1"},
    {"v_sub_nc_u32", GM_VOP2, 0x26, "d1s1v1"},     {"v_subrev_nc_u32", GM_VOP2, 0x27, "d1s1v1"},  {"v_and_b32", GM_VOP2, 0x1b, "d1s1v1"},
    {"v_xor_b32", GM_VOP2, 0x1d, "d1s1v1"},        {"v_lshrrev_b32", GM_VOP2, 0x16, "d1s1v1"},    {"v_lshlrev_b32", GM_VOP2, 0x1a, "d1s1v1"},
    {"v_max_i32", GM_VOP2, 0x12, "d1s1v1"},        {"v_min_i32", GM_VOP2, 0x11, "d1s1v1"},        {"v_mul_u32_u24", GM_VOP2, 0x0b, "d1s1v1"},
    {"v_mul_f32", GM_VOP2, 0x08, "d1s1v1"},        {"v_ashrrev_i32", GM_VOP2, 0x18, "d1s1v1"},    {"v_max_u32", GM_VOP2, 0x14, "d1s1v1"},
    {"v_min_u32", GM_VOP2, 0x13, "d1s1v1"},        {"v_cmp_eq_u32", GM_VOPC, 0xc2, "m2s1v1"},     {"v_cmp_ne_u32", GM_VOPC, 0xc5, "m2s1v1"},
    {"v_cmp_gt_u32", GM_VOPC, 0xc4, "m2s1v1"},     {"v_cmp_le_u32", GM_VOPC, 0xc3, "m2s1v1"},     {"v_cmp_lt_u32", GM_VOPC, 0xc1, "m2s1v1"},
    {"v_cmp_ge_u32", GM_VOPC, 0xc6, "m2s1v1"},     {"v_cmp_gt_i32", GM_VOPC, 0x84, "m2s1v1"},     {"v_cmp_le_i32", GM_VOPC, 0x83, "m2s1v1"},
    {"v_cmp_lt_i32", GM_VOPC, 0x81, "m2s1v1"},     {"v_cmp_ge_i32", GM_VOPC, 0x86, "m2s1v1"},     {"v_cmp_gt_u64", GM_VOPC, 0xe4, "m2s2v2"},
    {"v_cmp_le_u64", GM_VOPC, 0xe3, "m2s2v2"},     {"v_cndmask_b32_e64", GM_VOP3, 0x101, "d1s1s1k2"},
    {"v_mad_i32_i24", GM_VOP3, 0x142, "d1s1s1s1"}, {"v_mad_u32_u24", GM_VOP3, 0x143, "d1s1s1s1"},
    {"v_med3_i32", GM_VOP3, 0x158, "d1s1s1s1"},
    {"v_add3_u32", GM_VOP3, 0x36d, "d1s1s1s1"},    {"v_lshl_or_b32", GM_VOP3, 0x36f, "d1s1s1s1"}, {"v_lshl_add_u32", GM_VOP3, 0x346, "d1s1s1s1"},
    {"v_alignbit_b32", GM_VOP3, 0x14e, "d1s1s1s1"}, {"v_perm_b32", GM_VOP3, 0x344, "d1s1s1s1"},  {"v_bfe_u32", GM_VOP3, 0x148, "d1s1s1s1"},    {"v_mul_lo_u32", GM_VOP3, 0x169, "d1s1s1"},
    {"v_mad_u64_u32", GM_VOP3B, 0x176, "d2n2s1s1s2"}, {"v_add_co_u32", GM_VOP3B, 0x30f, "d1n2s1s1"}, {"v_add_co_ci_u32", GM_VOP2C, 0x28, "d1m2s1v1k2"},
    {"v_pk_min_u16", GM_VOP3P, 0x0d, "d1s1s1"},    {"v_pk_sub_u16", GM_VOP3P, 0x0b, "d1s1s1"},    {"ds_bpermute_b32", GM_DS, 0xb3, "d1v1v1"},
    {"global_load_ubyte", GM_GLD, 0x08, "d1v1b2"}, {"global_load_ushort", GM_GLD, 0x0a, "d1v1b2"}, {"global_load_dword", GM_GLD, 0x0c, "d1v1b2"}, {"global_load_dwordx4", GM_GLD, 0x0e, "d4v1b2"},
    {"global_store_dword", GM_GST, 0x1c, "v1v1b2"}, {"global_store_dwordx4", GM_GST, 0x1e, "v1v4b2"}, {"image_load", GM_MIMG, 0x00, "d4v2r8"},
    {"s_nop", GM_SOPP, 0x00, ""},
};
#define GM_IDS (sizeof(gpu_model_names) / sizeof(gpu_model_names[0]))

// The same rows' opcodes on gfx9 (gpuasm's third column, in the table's order; s_nop last): the model decodes a gfx9 kernel's words to the same
// rows, and what differs in the encodings themselves is in gpu_model_decode.
static const p16 gpu_model_code9[] = {
    0x00, 0x01, 0x20, 0x00, 0x01, 0x02, 0x03, 0x04, 0x06, 0x07, 0x0c, 0x0e, 0x1c, 0x1e, 0x24, 0x25,
    0x0d, 0x0f, 0x13, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x00, 0x02, 0x03, 0x04, 0x05, 0x0c, 0x0d,
    0x01, 0x0c, 0x02, 0x04, 0x05, 0x08, 0x00, 0x01, 0x02, 0x03, 0x01, 0x06, 0x07, 0x22, 0x27, 0x34,
    0x35, 0x36, 0x13, 0x15, 0x10, 0x12, 0x0d, 0x0c, 0x08, 0x05, 0x11, 0x0f, 0x0e, 0xca, 0xcd, 0xcc,
    0xcb, 0xc9, 0xce, 0xc4, 0xc3, 0xc1, 0xc6, 0xec, 0xeb, 0x100, 0x1c2, 0x1c3, 0x1d7, 0x1ff, 0x200, 0x1fd,
    0x1ce, 0x1ed, 0x1c8, 0x285, 0x1e8, 0x119, 0x1c, 0x0d, 0x0b, 0x3f, 0x10, 0x12, 0x14, 0x17, 0x1c, 0x1f,
    0x00, 0x00,
};
typedef char gpu_model_code9_rows[sizeof(gpu_model_code9) / sizeof(gpu_model_code9[0]) == sizeof(gpu_model_names) / sizeof(gpu_model_names[0]) ? 1 : -1];
#define GM_NAMED 97 // the rows of gpuasm's table (the last row, s_nop, is the padding's)

// gpuasm_clamps: the rows that take clamp.
static const char *const gpu_model_clamps[] = {"v_cvt_f32_u32", "v_cvt_u32_f32", "v_rcp_f32", "v_sqrt_f32", "v_add_nc_u32", "v_sub_nc_u32", "v_subrev_nc_u32", "v_mul_u32_u24",
                                               "v_mul_f32", "v_mad_u32_u24", "v_alignbit_b32", "v_mad_u64_u32", "v_add_co_u32", "v_pk_min_u16", "v_pk_sub_u16"};

// One instruction: its row, its words, whether a VOP1, VOP2, VOPC or VOP2C is in the long encoding, the clamp bit, its operands in the
// shape's order in the card's 9-bit numbering (a vector register 256 on, a base of off 125), the literal, and the immediate (SOPP's,
// or a load's or a store's offset, sign-extended).
struct gpu_model_insn
{
        p8 id, words, e64, clamp, count;
        p16 op[5];
        p32 literal;
        b32 imm;
};

// A row's opcode on the chip the model is (gpu_model.isa: 9 gfx9, else gfx10).
static p16 gpu_model_code_of(unsigned int id)
{
        return gpu_model.isa == 9 ? gpu_model_code9[id] : gpu_model_names[id].code;
}

static int gpu_model_find(p8 form, p16 code)
{
        for (unsigned int at = 0; at < GM_IDS; at++)
                if (gpu_model_names[at].form == form && gpu_model_code_of(at) == code)
                        return (int)at;
        return -1;
}

static b32 gpu_model_takes_clamp(int id)
{
        for (unsigned int at = 0; at < sizeof(gpu_model_clamps) / sizeof(gpu_model_clamps[0]); at++)
                if (string_equals((string_address)gpu_model_names[id].name, (string_address)gpu_model_clamps[at]))
                        return true;
        return false;
}

// A scalar pair gpuasm takes: an even register to s104, vcc, exec.
static b32 gpu_model_pair(p32 code)
{
        return (code <= 104 && !(code & 1)) || code == 106 || code == 126;
}

// Whether code is an operand of the shape letter's class and size, as gpuasm_source and gpuasm_destination allow it (packed: a VOP3P,
// which takes neither a float constant nor a literal; global: a base may be off).
static b32 gpu_model_class(p8 letter, unsigned int size, p32 code, b32 packed, b32 global)
{
        b32 inline_int = code >= 128 && code <= 208, constant32 = inline_int || (!packed && ((code >= 240 && code <= 248) || code == 255));
        b32 vector = code >= 256 && code - 256 + size <= 256;

        switch (letter)
        {
        case 'd':
        case 'v':
                return vector;
        case 'r':
                return code + size - 1 <= 105 && !(code & 3);
        case 'p':
                return size == 1 ? code <= 105 : size == 2 ? gpu_model_pair(code) : code + size - 1 <= 105 && !(code & 3);
        case 'm':
        case 'k':
                return gpu_model_pair(code);
        case 'n':
                return gpu_model_pair(code) || code == 125;
        case 'b':
                return (code <= 104 && !(code & 1)) || (global && code == 125);
        case 'i':
                return size == 1 ? code <= 105 || constant32 : gpu_model_pair(code) || inline_int;
        case 's':
                return size == 1 ? code <= 105 || constant32 || vector : gpu_model_pair(code) || inline_int || vector;
        }
        return false;
}

static int gpu_model_unknown(p32 word, positive pc, const char *why);

static p32 gpu_model_field(p32 word, unsigned int at, unsigned int bits)
{
        return word >> at & ((1u << bits) - 1);
}

/*
        The instruction at the words (room of them readable) at pc: its decoding in out, 0, or the failure. The layouts are gfx10's (the RDNA2
        ISA's encodings); the fixed bits are the ones gpuasm_instruction writes.

        gfx9 (gpu_model.isa 9), where it is another layout, as gpuasm writes it there: the long vector encodings' top bits (0x34, the packed
        ones among them from opcode 0x380), a short encoding's opcodes in the long one from 0x100 and a VOP1's from 0x140, a source the
        instruction does not have 0, a scalar load's own top bits with the offset an immediate, the lane permute's opcode a bit lower, a
        global's offset thirteen bits and its base of off 127, an image with no dim, lgkmcnt four bits. A carry's add (v_add_co_u32) has a
        short encoding there. What comes out is the same instruction in the same numbering (a base of off 125, a wait's counters as gfx10
        packs them), so the wave runs it as it runs gfx10's. And what gfx9 has not is refused: a scalar register past s101, null, a literal
        in a long encoding, a second scalar register there.
*/
static int gpu_model_decode(const p32 address_to at, unsigned int room, positive pc, struct gpu_model_insn address_to out)
{
        p32 w0 = at[0], w1 = room > 1 ? at[1] : 0;
        p64 fixed = 0, want = 0; // the fields that must hold what gpuasm writes: the first word's in the low half, the second's in the high
        int id = -1;
        p8 form;
        const char *shape;
        unsigned int literals = 0, scalars = 0, first = 1;
        p32 keys[5];
        const b32 nine = gpu_model.isa == 9;
        const p16 unused = nine ? 0 : 128; // what a source the instruction does not have holds

        memory_zero(out, sizeof(*out));
        out->words = 1;
        if (!(w0 >> 31)) // VOP1, VOPC, VOP2 (and VOP2C) in the short encodings
        {
                p32 top = w0 >> 25;

                if (top == 0x3f)
                        id = gpu_model_find(GM_VOP1, (p16)gpu_model_field(w0, 9, 8)), out->op[0] = (p16)(256 + gpu_model_field(w0, 17, 8)), out->op[1] = (p16)(w0 & 0x1ff);
                else if (top == 0x3e)
                        id = gpu_model_find(GM_VOPC, (p16)gpu_model_field(w0, 17, 8)), out->op[0] = 106, out->op[1] = (p16)(w0 & 0x1ff), out->op[2] = (p16)(256 + gpu_model_field(w0, 9, 8));
                else if ((id = gpu_model_find(GM_VOP2, (p16)top)) >= 0)
                        out->op[0] = (p16)(256 + gpu_model_field(w0, 17, 8)), out->op[1] = (p16)(w0 & 0x1ff), out->op[2] = (p16)(256 + gpu_model_field(w0, 9, 8));
                else if ((id = gpu_model_find(GM_VOP2C, (p16)top)) >= 0)
                        out->op[0] = (p16)(256 + gpu_model_field(w0, 17, 8)), out->op[1] = 106, out->op[2] = (p16)(w0 & 0x1ff), out->op[3] = (p16)(256 + gpu_model_field(w0, 9, 8)),
                        out->op[4] = 106;
                else if (nine && (id = gpu_model_find(GM_VOP3B, (p16)(0x100 + top))) >= 0) // gfx9's short carry add: the carry in vcc
                        out->op[0] = (p16)(256 + gpu_model_field(w0, 17, 8)), out->op[1] = 106, out->op[2] = (p16)(w0 & 0x1ff), out->op[3] = (p16)(256 + gpu_model_field(w0, 9, 8)),
                        out->op[4] = unused;
                if (id < 0)
                        return gpu_model_unknown(w0, pc, "a vector opcode not in gpuasm's table");
        }
        else if ((w0 >> 30) == 2) // the scalar encodings
        {
                p32 head = w0 >> 23;

                if (head == 0x17d)
                        id = gpu_model_find(GM_SOP1, (p16)gpu_model_field(w0, 8, 8)), out->op[0] = (p16)gpu_model_field(w0, 16, 7), out->op[1] = (p16)(w0 & 0xff);
                else if (head == 0x17e)
                        id = gpu_model_find(GM_SOPC, (p16)gpu_model_field(w0, 16, 7)), out->op[0] = (p16)(w0 & 0xff), out->op[1] = (p16)gpu_model_field(w0, 8, 8);
                else if (head == 0x17f)
                {
                        id = gpu_model_find(GM_SOPP, (p16)gpu_model_field(w0, 16, 7)), out->imm = (b32)(b16)(w0 & 0xffff);
                        if (nine && id >= 0 && gpu_model_names[id].shape[0] == 'w')
                        {
                                // gfx9's lgkmcnt is bits 8 to 11, and 15 there is no wait for it: the counters as gfx10 packs them
                                if (w0 & 0x3000)
                                        return gpu_model_unknown(w0, pc, "a wait with bits gfx9's has not");
                                out->imm = (b32)((w0 & 0xc0ff) | (gpu_model_field(w0, 8, 4) == 15 ? 63u : gpu_model_field(w0, 8, 4)) << 8);
                        }
                }
                else if ((w0 >> 28) == 0xb)
                        return gpu_model_unknown(w0, pc, "SOPK, which gpuasm does not make");
                else
                        id = gpu_model_find(GM_SOP2, (p16)gpu_model_field(w0, 23, 7)), out->op[0] = (p16)gpu_model_field(w0, 16, 7), out->op[1] = (p16)(w0 & 0xff),
                        out->op[2] = (p16)gpu_model_field(w0, 8, 8);
                if (id < 0)
                        return gpu_model_unknown(w0, pc, "a scalar opcode not in gpuasm's table");
        }
        else // the long encodings: two words
        {
                p32 head = w0 >> 26;

                if (room < 2)
                        return gpu_model_unknown(w0, pc, "a long encoding with no second word");
                out->words = 2;
                if (head == (nine ? 0x30u : 0x3du))
                {
                        id = gpu_model_find(GM_SMEM, (p16)gpu_model_field(w0, 18, 8));
                        out->op[0] = (p16)gpu_model_field(w0, 6, 7), out->op[1] = (p16)((w0 & 0x3f) << 1), out->imm = (b32)(w1 << 11) >> 11;
                        fixed = (w0 & 0x3e000u) | (p64)(w1 & 0xffe00000u) << 32, want = 0xfa000000ull << 32; // glc, dlc and the bits between clear; no offset register
                        if (nine)
                                want = 0x20000u; // the offset an immediate (bit 17), no glc, nv or offset register, and nothing above the offset
                }
                else if (nine && head == 0x34 && gpu_model_field(w0, 16, 10) >= 0x380)
                {
                        id = gpu_model_find(GM_VOP3P, (p16)gpu_model_field(w0, 16, 7));
                        out->clamp = (p8)gpu_model_field(w0, 15, 1), out->op[0] = (p16)(256 + (w0 & 0xff)), out->op[1] = (p16)(w1 & 0x1ff), out->op[2] = (p16)gpu_model_field(w1, 9, 9);
                        fixed = (w0 & 0x7f00u) | (p64)(w1 & 0xfffc0000u) << 32, want = 0x4000u | (p64)(3u << 27) << 32; // op_sel_hi set, the third source 0
                }
                else if (head == (nine ? 0x34u : 0x35u))
                {
                        p32 code = gpu_model_field(w0, 16, 10), one = nine ? 0x140 : 0x180; // where a VOP1's opcodes begin in the long encoding

                        if ((id = gpu_model_find(GM_VOP3, (p16)code)) < 0 && (id = gpu_model_find(GM_VOP3B, (p16)code)) < 0)
                                id = code < 0x100                   ? gpu_model_find(GM_VOPC, (p16)code)
                                     : code < 0x140                 ? (gpu_model_find(GM_VOP2, (p16)(code - 0x100)) >= 0 ? gpu_model_find(GM_VOP2, (p16)(code - 0x100))
                                                                                                                       : gpu_model_find(GM_VOP2C, (p16)(code - 0x100)))
                                     : code >= one && code < one + 0x80 ? gpu_model_find(GM_VOP1, (p16)(code - one))
                                                                     : -1;
                        if (id < 0)
                                return gpu_model_unknown(w0, pc, "a VOP3 opcode not in gpuasm's table");
                        form = gpu_model_names[id].form;
                        out->e64 = form == GM_VOP1 || form == GM_VOP2 || form == GM_VOPC || form == GM_VOP2C;
                        out->clamp = (p8)gpu_model_field(w0, 15, 1);
                        out->op[0] = (p16)(form == GM_VOPC ? w0 & 0xff : 256 + (w0 & 0xff));
                        if (form == GM_VOP3B || form == GM_VOP2C)
                                out->op[1] = (p16)gpu_model_field(w0, 8, 7), out->op[2] = (p16)(w1 & 0x1ff), out->op[3] = (p16)gpu_model_field(w1, 9, 9), out->op[4] = (p16)gpu_model_field(w1, 18, 9);
                        else
                                out->op[1] = (p16)(w1 & 0x1ff), out->op[2] = (p16)gpu_model_field(w1, 9, 9), out->op[3] = (p16)gpu_model_field(w1, 18, 9);
                        fixed = (form == GM_VOP3B || form == GM_VOP2C ? 0 : w0 & 0x7f00u) | (p64)(w1 & 0xf8000000u) << 32; // abs and op_sel, omod and neg
                }
                else if (!nine && (w0 >> 23) == 0x198)
                {
                        id = gpu_model_find(GM_VOP3P, (p16)gpu_model_field(w0, 16, 7));
                        out->clamp = (p8)gpu_model_field(w0, 15, 1), out->op[0] = (p16)(256 + (w0 & 0xff)), out->op[1] = (p16)(w1 & 0x1ff), out->op[2] = (p16)gpu_model_field(w1, 9, 9);
                        fixed = (w0 & 0x7f00u) | (p64)(w1 & 0xfffc0000u) << 32, want = 0x4000u | (p64)(128u << 18 | 3u << 27) << 32; // op_sel_hi set, the other selects and negates clear
                }
                else if (head == 0x36)
                {
                        id = gpu_model_find(GM_DS, (p16)gpu_model_field(w0, nine ? 17 : 18, 8));
                        out->op[0] = (p16)(256 + (w1 >> 24)), out->op[1] = (p16)(256 + (w1 & 0xff)), out->op[2] = (p16)(256 + gpu_model_field(w1, 8, 8));
                        fixed = (w0 & (nine ? 0x1ffffu : 0x3ffffu)) | (p64)(w1 & 0xff0000u) << 32; // no offsets, no gds, no second datum
                }
                else if (head == 0x37)
                {
                        p8 store;

                        id = gpu_model_find(GM_GLD, (p16)gpu_model_field(w0, 18, 7));
                        if (id < 0)
                                id = gpu_model_find(GM_GST, (p16)gpu_model_field(w0, 18, 7));
                        store = id >= 0 && gpu_model_names[id].form == GM_GST;
                        out->op[0] = (p16)(256 + (store ? w1 & 0xff : w1 >> 24)), out->op[1] = (p16)(256 + (store ? gpu_model_field(w1, 8, 8) : w1 & 0xff));
                        out->op[2] = (p16)gpu_model_field(w1, 16, 7), out->imm = (b32)(w0 << 20) >> 20;
                        fixed = (w0 & 0x0203f000u) | (p64)((w1 & 0x800000u) | (store ? w1 & 0xff000000u : w1 & 0xff00u)) << 32, want = 0x8000u; // global; no dlc, lds, glc, slc or nv
                        if (nine) // the offset is thirteen bits, and a base of off is 127: 125 here, as everywhere after
                        {
                                out->imm = (b32)(w0 << 19) >> 19;
                                fixed = (w0 & 0x0203e000u) | (p64)((w1 & 0x800000u) | (store ? w1 & 0xff000000u : w1 & 0xff00u)) << 32;
                                if (out->op[2] == 125)
                                        return gpu_model_unknown(w0, pc, "a base gfx9 does not have");
                                if (out->op[2] == 127)
                                        out->op[2] = 125;
                        }
                }
                else if (head == 0x3c)
                {
                        // MIMG, as gpuasm writes image_load: 2D (dim 1 at bit 3), dmask 0xf, unorm, no glc, slc, dlc, tfe, lwe, a16, d16 or nsa;
                        // the address, the data and the resource's first register over four in the second word, no sampler.
                        id = gpu_model_find(GM_MIMG, (p16)gpu_model_field(w0, 18, 7));
                        out->op[0] = (p16)(256 + gpu_model_field(w1, 8, 8)), out->op[1] = (p16)(256 + (w1 & 0xff)), out->op[2] = (p16)(gpu_model_field(w1, 16, 5) * 4);
                        fixed = (w0 & ~(0x7fu << 18 | 0xfu << 8)) | (p64)(w1 & 0xffe00000u) << 32, want = nine ? 0xf0001000u : 0xf0001008u;
                        if (gpu_model_field(w0, 8, 4) != 0xf && gpu_model_field(w0, 8, 4) != 1)
                                return gpu_model_unknown(w0, pc, "an image dmask gpuasm does not write (0xf or 0x1)");
                        out->imm = (b32)gpu_model_field(w0, 8, 4);
                }
                else
                        return gpu_model_unknown(w0, pc, "an encoding gpuasm does not make");
                if (id < 0)
                        return gpu_model_unknown(w0, pc, "an opcode not in gpuasm's table");
                if (fixed != want)
                        return gpu_model_unknown(w0, pc, "a modifier or a field gpuasm does not set");
        }

        form = gpu_model_names[id].form, shape = gpu_model_names[id].shape;
        out->id = (p8)id, out->count = (p8)(string_length((string_address)shape) / 2);
        if (out->clamp && !gpu_model_takes_clamp(id))
                return gpu_model_unknown(w0, pc, "clamp on an instruction gpuasm gives none");
        if (form == GM_SOPP)
        {
                if (!shape[0] && out->imm)
                        return gpu_model_unknown(w0, pc, "an immediate gpuasm does not write");
                if (shape[0] == 'w' && (out->imm & 0x80))
                        return gpu_model_unknown(w0, pc, "a wait with expcnt past 7");
                return 0;
        }
        // The long form's unused third source is 128, as gpuasm writes it; the short forms and the long one of a VOP1 the same for the second.
        if ((form == GM_VOP1 && out->e64 && (out->op[2] != unused || out->op[3] != unused)) || ((form == GM_VOP2 || form == GM_VOPC) && out->e64 && out->op[3] != unused) ||
            ((form == GM_VOP3 || form == GM_VOP3B) && out->count < 4 + (form == GM_VOP3B) && out->op[out->count] != unused))
                return gpu_model_unknown(w0, pc, "a source gpuasm leaves unused that is not 0");
        for (unsigned int k = 0; k < out->count; k++)
        {
                p8 letter = (p8)shape[2 * k];
                unsigned int size = (unsigned int)(shape[2 * k + 1] - '0');

                if (letter == 'o')
                        continue;
                if ((form == GM_GLD && k == 1) || (form == GM_GST && k == 0))
                        size = out->op[2] == 125 ? 2 : 1; // the address is a pair of vector registers with off, else one
                if (out->e64 && letter == 'v')
                        letter = 's'; // the long form takes any source where the short takes a vector register
                if (!gpu_model_class(letter, size, out->op[k], form == GM_VOP3P, form == GM_GLD || form == GM_GST))
                        return gpu_model_unknown(w0, pc, "an operand gpuasm does not write there");
                if (nine && ((out->op[k] >= 102 && out->op[k] <= 105) || (out->op[k] == 125 && letter == 'n')))
                        return gpu_model_unknown(w0, pc, "a register gfx9 does not have (a scalar past s101, or null)");
                if (out->op[k] == 255 && letter != 'p' && letter != 'm' && letter != 'n' && letter != 'b')
                        literals++;
                // The long encodings read two scalar values at most (gpuasm counts a pair and a single of one register as two).
                if (k >= first && (form == GM_VOP3 || form == GM_VOP3B || form == GM_VOP3P || out->e64) && (out->op[k] < 128 || out->op[k] == 255))
                {
                        p32 key = out->op[k] | (size == 2 ? 0x1000u : 0);
                        b32 known = false;

                        for (unsigned int j = 0; j < scalars; j++)
                                known = known || keys[j] == key;
                        if (!known)
                                keys[scalars++] = key;
                }
                if (k == 0 && (form == GM_VOP3B || (form == GM_VOP2C && out->e64)))
                        first = 2;
        }
        if (scalars > 2)
                return gpu_model_unknown(w0, pc, "more than two scalar values to an instruction");
        if (nine && out->words == 2 && (form == GM_VOP3 || form == GM_VOP3B || form == GM_VOP3P || out->e64) && (literals || scalars > 1))
                return gpu_model_unknown(w0, pc, "a literal or a second scalar register in a long encoding, which gfx9 does not take");
        if (literals)
        {
                if (room < out->words + 1u)
                        return gpu_model_unknown(w0, pc, "a literal past the end of the code");
                out->literal = at[out->words++];
        }
        return 0;
}

// The words gpuasm makes of a decoded instruction, in out (three at most): their count.
static unsigned int gpu_model_encode(const struct gpu_model_insn address_to in, p32 address_to out)
{
        const struct gpu_model_name *row = gpu_model_names + in->id;
        const p16 *o = in->op;
        const b32 nine = gpu_model.isa == 9;
        p32 code = gpu_model_code_of(in->id), clamp = (p32)in->clamp << 15;
        p32 head = nine ? 0xd0000000u : 0xd4000000u, unused = nine ? 0 : 128; // a long vector encoding's top bits, and a source it has not
        unsigned int words = 1;
        b32 literal = false;

        for (unsigned int k = 0; k < in->count; k++)
                literal = literal || (o[k] == 255 && row->shape[2 * k] != 'o');
        switch (row->form)
        {
        case GM_SOP1: out[0] = 0xbe800000u | (p32)o[0] << 16 | code << 8 | o[1]; break;
        case GM_SOP2: out[0] = 0x80000000u | code << 23 | (p32)o[0] << 16 | (p32)o[2] << 8 | o[1]; break;
        case GM_SOPC: out[0] = 0xbf000000u | code << 16 | (p32)o[1] << 8 | o[0]; break;
        case GM_SOPP:
                out[0] = 0xbf800000u | code << 16 | ((p32)in->imm & 0xffff);
                if (nine && row->shape[0] == 'w') // lgkmcnt in four bits, 15 for no wait
                        out[0] = 0xbf800000u | code << 16 | ((p32)in->imm & 0xc0ff) | (((p32)in->imm >> 8 & 63) == 63 ? 15u : (p32)in->imm >> 8 & 15) << 8;
                break;
        case GM_SMEM:
                out[0] = 0xf4000000u | code << 18 | (p32)o[0] << 6 | (p32)o[1] >> 1, out[1] = 0xfa000000u | ((p32)in->imm & 0x1fffff), words = 2;
                if (nine)
                        out[0] = 0xc0020000u | code << 18 | (p32)o[0] << 6 | (p32)o[1] >> 1, out[1] = (p32)in->imm & 0x1fffff;
                break;
        case GM_VOP1:
        case GM_VOP2:
        case GM_VOPC:
                if (in->e64)
                {
                        p32 base = row->form == GM_VOP1 ? (nine ? 0x140 : 0x180) : row->form == GM_VOP2 ? 0x100 : 0;

                        out[0] = head | (base + code) << 16 | clamp | (row->form == GM_VOPC ? o[0] : (p32)o[0] - 256);
                        out[1] = o[1] | (p32)(row->form == GM_VOP1 ? unused : o[2]) << 9 | unused << 18, words = 2;
                }
                else if (row->form == GM_VOP1)
                        out[0] = 0x7e000000u | ((p32)o[0] - 256) << 17 | code << 9 | o[1];
                else if (row->form == GM_VOP2)
                        out[0] = code << 25 | ((p32)o[0] - 256) << 17 | ((p32)o[2] - 256) << 9 | o[1];
                else
                        out[0] = 0x7c000000u | code << 17 | ((p32)o[2] - 256) << 9 | o[1];
                break;
        case GM_VOP2C:
                if (in->e64)
                        out[0] = head | (0x100u + code) << 16 | (p32)o[1] << 8 | ((p32)o[0] - 256), out[1] = o[2] | (p32)o[3] << 9 | (p32)o[4] << 18, words = 2;
                else
                        out[0] = code << 25 | ((p32)o[0] - 256) << 17 | ((p32)o[3] - 256) << 9 | o[2];
                break;
        case GM_VOP3: out[0] = head | code << 16 | clamp | ((p32)o[0] - 256), out[1] = o[1] | (p32)o[2] << 9 | (p32)(in->count > 3 ? o[3] : unused) << 18, words = 2; break;
        case GM_VOP3B:
                if (nine && in->words == 1) // gfx9's short carry add
                {
                        out[0] = (code - 0x100u) << 25 | ((p32)o[0] - 256) << 17 | ((p32)o[3] - 256) << 9 | o[2];
                        break;
                }
                out[0] = head | code << 16 | clamp | (p32)o[1] << 8 | ((p32)o[0] - 256), out[1] = o[2] | (p32)o[3] << 9 | (p32)(in->count > 4 ? o[4] : unused) << 18, words = 2;
                break;
        case GM_VOP3P:
                out[0] = (nine ? 0xd3800000u : 0xcc000000u) | code << 16 | clamp | 1u << 14 | ((p32)o[0] - 256), out[1] = o[1] | (p32)o[2] << 9 | unused << 18 | 3u << 27, words = 2;
                break;
        case GM_DS: out[0] = 0xd8000000u | code << (nine ? 17 : 18), out[1] = ((p32)o[1] - 256) | ((p32)o[2] - 256) << 8 | ((p32)o[0] - 256) << 24, words = 2; break;
        case GM_MIMG:
                out[0] = (nine ? 0xf0001000u : 0xf0001008u) | (p32)in->imm << 8 | code << 18, out[1] = ((p32)o[1] - 256) | ((p32)o[0] - 256) << 8 | ((p32)o[2] >> 2) << 16, words = 2;
                break;
        case GM_GLD:
                out[0] = 0xdc008000u | code << 18 | ((p32)in->imm & (nine ? 0x1fffu : 0xfffu)),
                out[1] = ((p32)o[1] - 256) | (p32)(nine && o[2] == 125 ? 127 : o[2]) << 16 | ((p32)o[0] - 256) << 24, words = 2;
                break;
        default:
                out[0] = 0xdc008000u | code << 18 | ((p32)in->imm & (nine ? 0x1fffu : 0xfffu)),
                out[1] = ((p32)o[0] - 256) | ((p32)o[1] - 256) << 8 | (p32)(nine && o[2] == 125 ? 127 : o[2]) << 16, words = 2;
                break;
        }
        if (literal)
                out[words++] = in->literal;
        return words;
}
