/*
        The assembler for the GPU's kernels.

        src/canvas/gpu.asm is the source of the programs the card runs
        (AMD RDNA2, gfx1036, wave64), and this turns it into the arrays
        src/canvas/gpu.c is built with: for each kernel, the two resource
        words the dispatch is given and then the code. build.c includes this
        file and `build gpu-kernels` runs it, so the words are made by the
        build from the source and nothing generated is kept in the tree, and
        no LLVM is needed to build.

        It takes the subset gpu.asm is written in and nothing more:

            statements    labels (name:), directives, macros, instructions,
                          ; to the end of the line for a comment
            directives    .amdgcn_target .text .rodata .globl .p2align .type
                          .size .macro/.endm (\name in the body) .irp/.endr
                          and the .amdhsa_kernel block
            instructions  the SALU, SMEM, VALU (32-bit and VOP3 encodings),
                          packed, DS and global forms the table below lists
            operands      s5 s[8:11] v3 v[5:8] vcc exec null, inline
                          constants, one 32-bit literal, labels in branches

        Anything else stops with the file and line, so a form this has not
        been checked against LLVM is a refusal and never a guess: the other
        special registers, a literal in a 64-bit or a packed operand, the
        modifiers, SDWA and DPP are all refused. The table was made by
        assembling one instruction of each with llvm-mc and reading the opcode
        field back; test/gpu_kernels.py assembles src/canvas/gpu.asm and
        test/gpu_stress.asm with both and requires every word to agree.

        A kernel's code is what lies between its label and its .size; its
        resource words come from the .amdhsa_next_free_vgpr in its block (the
        rest of the block must say what this assembles for: wave64, the two
        workgroup ids, the kernarg pointer and the private segment buffer in
        the user registers, no scratch, forward progress), because those are
        the words the file never varies. Forward progress is the first
        resource word's top bit, which this always sets: an LLVM before 19
        leaves it clear unless the block asks, and a later one sets it, so the
        block has to ask for the two to agree whatever LLVM is at hand.
*/

#define GPUASM_WORDS 32768
#define GPUASM_LABELS 1024
#define GPUASM_FIXUPS 2048
#define GPUASM_MACROS 32
#define GPUASM_KERNELS 32
#define GPUASM_PARAMS 8
#define GPUASM_VALUES 64
#define GPUASM_OPERANDS 8
#define GPUASM_DEPTH 8
#define GPUASM_ARENA (256 << 10)
#define GPUASM_POOL (32 << 10)
#define GPUASM_TEXT (1 << 20)
#define GPUASM_NOP 0xbf800000u
#define GPUASM_RSRC1 0xe0ac0000u
#define GPUASM_RSRC2 0x0000018cu
#define GPUASM_TARGET "\"amdgcn-amd-amdhsa--gfx1036\""

enum
{
        GPUASM_SOP1,
        GPUASM_SOP2,
        GPUASM_SOPC,
        GPUASM_SOPP,
        GPUASM_SMEM,
        GPUASM_VOP1,
        GPUASM_VOP2,
        GPUASM_VOP2C,
        GPUASM_VOPC,
        GPUASM_VOP3,
        GPUASM_VOP3B,
        GPUASM_VOP3P,
        GPUASM_DS,
        GPUASM_GLD,
        GPUASM_GST
};

/*
        The instructions. Each row is a mnemonic, the encoding it is in, its
        opcode there, and its operands in the order they are written, a class
        letter and a size in dwords each:

            d vector destination         v vector register source
            p scalar destination         i scalar source
            s vector source: a register, an inline constant or a literal
            m lane mask destination      k lane mask source
            n destination of the carry (a pair, vcc, exec or null)
            b base address (a pair, or off)  o byte offset (of a load)
            l label                      w wait counters

        VOP1, VOP2 and VOPC rows are for both encodings: the short one when
        the operands fit it (the second source a vector register, a VOPC's
        destination vcc), the VOP3 one when they do not or when the name says
        _e64, as LLVM chooses. Their opcode is the short encoding's. The same
        goes for a VOP2 that reads and writes a carry (VOP2C), whose short
        encoding has the carry in vcc and says so.
*/
typedef struct gpuasm_op
{
        string_address name;
        p8 form;
        p16 code;
        string_address shape;
} gpuasm_op;

static const gpuasm_op gpuasm_ops[] = {
        {"s_mov_b32", GPUASM_SOP1, 0x03, "p1i1"},
        {"s_mov_b64", GPUASM_SOP1, 0x04, "p2i2"},
        {"s_and_saveexec_b64", GPUASM_SOP1, 0x24, "p2i2"},
        {"s_add_u32", GPUASM_SOP2, 0x00, "p1i1i1"},
        {"s_sub_u32", GPUASM_SOP2, 0x01, "p1i1i1"},
        {"s_add_i32", GPUASM_SOP2, 0x02, "p1i1i1"},
        {"s_sub_i32", GPUASM_SOP2, 0x03, "p1i1i1"},
        {"s_addc_u32", GPUASM_SOP2, 0x04, "p1i1i1"},
        {"s_min_i32", GPUASM_SOP2, 0x06, "p1i1i1"},
        {"s_min_u32", GPUASM_SOP2, 0x07, "p1i1i1"},
        {"s_and_b32", GPUASM_SOP2, 0x0e, "p1i1i1"},
        {"s_or_b32", GPUASM_SOP2, 0x10, "p1i1i1"},
        {"s_lshl_b32", GPUASM_SOP2, 0x1e, "p1i1i1"},
        {"s_lshr_b32", GPUASM_SOP2, 0x20, "p1i1i1"},
        {"s_mul_i32", GPUASM_SOP2, 0x26, "p1i1i1"},
        {"s_bfe_u32", GPUASM_SOP2, 0x27, "p1i1i1"},
        {"s_and_b64", GPUASM_SOP2, 0x0f, "p2i2i2"},
        {"s_or_b64", GPUASM_SOP2, 0x11, "p2i2i2"},
        {"s_andn2_b64", GPUASM_SOP2, 0x15, "p2i2i2"},
        {"s_cmp_eq_u32", GPUASM_SOPC, 0x06, "i1i1"},
        {"s_cmp_lg_u32", GPUASM_SOPC, 0x07, "i1i1"},
        {"s_cmp_gt_u32", GPUASM_SOPC, 0x08, "i1i1"},
        {"s_cmp_ge_u32", GPUASM_SOPC, 0x09, "i1i1"},
        {"s_cmp_lt_u32", GPUASM_SOPC, 0x0a, "i1i1"},
        {"s_cmp_le_u32", GPUASM_SOPC, 0x0b, "i1i1"},
        {"s_cmp_eq_i32", GPUASM_SOPC, 0x00, "i1i1"},
        {"s_cmp_gt_i32", GPUASM_SOPC, 0x02, "i1i1"},
        {"s_cmp_ge_i32", GPUASM_SOPC, 0x03, "i1i1"},
        {"s_cmp_lt_i32", GPUASM_SOPC, 0x04, "i1i1"},
        {"s_cmp_le_i32", GPUASM_SOPC, 0x05, "i1i1"},
        {"s_bitcmp0_b32", GPUASM_SOPC, 0x0c, "i1i1"},
        {"s_bitcmp1_b32", GPUASM_SOPC, 0x0d, "i1i1"},
        {"s_endpgm", GPUASM_SOPP, 0x01, ""},
        {"s_waitcnt", GPUASM_SOPP, 0x0c, "w1"},
        {"s_branch", GPUASM_SOPP, 0x02, "l1"},
        {"s_cbranch_scc0", GPUASM_SOPP, 0x04, "l1"},
        {"s_cbranch_scc1", GPUASM_SOPP, 0x05, "l1"},
        {"s_cbranch_execz", GPUASM_SOPP, 0x08, "l1"},
        {"s_load_dword", GPUASM_SMEM, 0x00, "p1b2o1"},
        {"s_load_dwordx2", GPUASM_SMEM, 0x01, "p2b2o1"},
        {"s_load_dwordx4", GPUASM_SMEM, 0x02, "p4b2o1"},
        {"s_load_dwordx8", GPUASM_SMEM, 0x03, "p8b2o1"},
        {"v_mov_b32", GPUASM_VOP1, 0x01, "d1s1"},
        {"v_cvt_f32_u32", GPUASM_VOP1, 0x06, "d1s1"},
        {"v_cvt_u32_f32", GPUASM_VOP1, 0x07, "d1s1"},
        {"v_rcp_f32", GPUASM_VOP1, 0x2a, "d1s1"},
        {"v_sqrt_f32", GPUASM_VOP1, 0x33, "d1s1"},
        {"v_add_nc_u32", GPUASM_VOP2, 0x25, "d1s1v1"},
        {"v_sub_nc_u32", GPUASM_VOP2, 0x26, "d1s1v1"},
        {"v_subrev_nc_u32", GPUASM_VOP2, 0x27, "d1s1v1"},
        {"v_and_b32", GPUASM_VOP2, 0x1b, "d1s1v1"},
        {"v_xor_b32", GPUASM_VOP2, 0x1d, "d1s1v1"},
        {"v_lshrrev_b32", GPUASM_VOP2, 0x16, "d1s1v1"},
        {"v_lshlrev_b32", GPUASM_VOP2, 0x1a, "d1s1v1"},
        {"v_max_i32", GPUASM_VOP2, 0x12, "d1s1v1"},
        {"v_min_i32", GPUASM_VOP2, 0x11, "d1s1v1"},
        {"v_mul_u32_u24", GPUASM_VOP2, 0x0b, "d1s1v1"},
        {"v_mul_f32", GPUASM_VOP2, 0x08, "d1s1v1"},
        {"v_ashrrev_i32", GPUASM_VOP2, 0x18, "d1s1v1"},
        {"v_max_u32", GPUASM_VOP2, 0x14, "d1s1v1"},
        {"v_min_u32", GPUASM_VOP2, 0x13, "d1s1v1"},
        {"v_cmp_eq_u32", GPUASM_VOPC, 0xc2, "m2s1v1"},
        {"v_cmp_ne_u32", GPUASM_VOPC, 0xc5, "m2s1v1"},
        {"v_cmp_gt_u32", GPUASM_VOPC, 0xc4, "m2s1v1"},
        {"v_cmp_le_u32", GPUASM_VOPC, 0xc3, "m2s1v1"},
        {"v_cmp_lt_u32", GPUASM_VOPC, 0xc1, "m2s1v1"},
        {"v_cmp_ge_u32", GPUASM_VOPC, 0xc6, "m2s1v1"},
        {"v_cmp_gt_i32", GPUASM_VOPC, 0x84, "m2s1v1"},
        {"v_cmp_le_i32", GPUASM_VOPC, 0x83, "m2s1v1"},
        {"v_cmp_lt_i32", GPUASM_VOPC, 0x81, "m2s1v1"},
        {"v_cmp_ge_i32", GPUASM_VOPC, 0x86, "m2s1v1"},
        {"v_cmp_gt_u64", GPUASM_VOPC, 0xe4, "m2s2v2"},
        {"v_cmp_le_u64", GPUASM_VOPC, 0xe3, "m2s2v2"},
        {"v_cndmask_b32_e64", GPUASM_VOP3, 0x101, "d1s1s1k2"},
        {"v_mad_u32_u24", GPUASM_VOP3, 0x143, "d1s1s1s1"},
        {"v_add3_u32", GPUASM_VOP3, 0x36d, "d1s1s1s1"},
        {"v_lshl_or_b32", GPUASM_VOP3, 0x36f, "d1s1s1s1"},
        {"v_lshl_add_u32", GPUASM_VOP3, 0x346, "d1s1s1s1"},
        {"v_alignbit_b32", GPUASM_VOP3, 0x14e, "d1s1s1s1"},
        {"v_bfe_u32", GPUASM_VOP3, 0x148, "d1s1s1s1"},
        {"v_mul_lo_u32", GPUASM_VOP3, 0x169, "d1s1s1"},
        {"v_mad_u64_u32", GPUASM_VOP3B, 0x176, "d2n2s1s1s2"},
        {"v_add_co_u32", GPUASM_VOP3B, 0x30f, "d1n2s1s1"},
        {"v_add_co_ci_u32", GPUASM_VOP2C, 0x28, "d1m2s1v1k2"},
        {"v_pk_min_u16", GPUASM_VOP3P, 0x0d, "d1s1s1"},
        {"v_pk_sub_u16", GPUASM_VOP3P, 0x0b, "d1s1s1"},
        {"ds_bpermute_b32", GPUASM_DS, 0xb3, "d1v1v1"},
        {"global_load_ubyte", GPUASM_GLD, 0x08, "d1v1b2"},
        {"global_load_dword", GPUASM_GLD, 0x0c, "d1v1b2"},
        {"global_load_dwordx4", GPUASM_GLD, 0x0e, "d4v1b2"},
        {"global_store_dword", GPUASM_GST, 0x1c, "v1v1b2"},
        {"global_store_dwordx4", GPUASM_GST, 0x1e, "v1v4b2"},
};

//      The instructions that take clamp, which LLVM says instruction by instruction.
static const string_address gpuasm_clamps[] = {
        "v_cvt_f32_u32", "v_cvt_u32_f32", "v_rcp_f32", "v_sqrt_f32", "v_add_nc_u32", "v_sub_nc_u32", "v_subrev_nc_u32",
        "v_mul_u32_u24", "v_mul_f32", "v_mad_u32_u24", "v_alignbit_b32", "v_mad_u64_u32", "v_add_co_u32", "v_pk_min_u16", "v_pk_sub_u16"};

/*      The block a kernel's .amdhsa properties are. A row with a value is a
        property this assembles for only that value; the two with -1 are the
        ones a file may vary (the vector registers set the first resource
        word, the scalar ones are not in it on this chip). */
static const struct
{
        string_address name;
        b64 value;
} gpuasm_properties[] = {
        {".amdhsa_group_segment_fixed_size", 0},
        {".amdhsa_private_segment_fixed_size", 0},
        {".amdhsa_kernarg_size", 64},
        {".amdhsa_user_sgpr_private_segment_buffer", 1},
        {".amdhsa_user_sgpr_kernarg_segment_ptr", 1},
        {".amdhsa_system_sgpr_workgroup_id_x", 1},
        {".amdhsa_system_sgpr_workgroup_id_y", 1},
        {".amdhsa_wavefront_size32", 0},
        {".amdhsa_next_free_vgpr", -1},
        {".amdhsa_next_free_sgpr", -1},
        {".amdhsa_forward_progress", 1},
};
#define GPUASM_VGPRS 8
#define GPUASM_SGPRS 9
#define GPUASM_PROPERTIES 11

//      The float constants LLVM takes as inline codes 240 to 248 wherever a 32-bit
//      operand may be a literal, an integer operand as much as a float one:
//      the bit patterns of 0.5, -0.5, 1, -1, 2, -2, 4, -4 and 1/2pi.
static const p32 gpuasm_floats[] = {0x3f000000u, 0xbf000000u, 0x3f800000u, 0xbf800000u, 0x40000000u,
                                    0xc0000000u, 0x40800000u, 0xc0800000u, 0x3e22f983u};

typedef struct gpuasm_span
{
        string_address at;
        positive length;
} gpuasm_span;

typedef struct gpuasm_operand
{
        p32 code;
        p32 literal;
        b64 written;
        bool has_literal;
        bool vector;
        gpuasm_span label;
} gpuasm_operand;

typedef struct gpuasm_macro
{
        gpuasm_span name;
        gpuasm_span params[GPUASM_PARAMS];
        positive count;
        gpuasm_span body;
} gpuasm_macro;

typedef struct gpuasm_cursor
{
        string_address at;
        string_address stop;
        positive line;
} gpuasm_cursor;

typedef struct gpuasm_label
{
        positive name;
        positive length;
        positive at;
        positive stop;
        positive line;
        bool defined;
        bool sized;
} gpuasm_label;

typedef struct gpuasm_kernel
{
        positive name;
        positive length;
        positive vgprs;
        positive line;
} gpuasm_kernel;

typedef struct gpuasm_fixup
{
        positive at;
        positive label;
        positive line;
} gpuasm_fixup;

static p32 gpuasm_words[GPUASM_WORDS];
static positive gpuasm_position;
static gpuasm_label gpuasm_labels[GPUASM_LABELS];
static positive gpuasm_label_count;
static gpuasm_fixup gpuasm_fixups[GPUASM_FIXUPS];
static positive gpuasm_fixup_count;
static gpuasm_macro gpuasm_macros[GPUASM_MACROS];
static positive gpuasm_macro_count;
static gpuasm_kernel gpuasm_kernels[GPUASM_KERNELS];
static positive gpuasm_kernel_count;
static p8 gpuasm_arena[GPUASM_ARENA];
static positive gpuasm_arena_used;
static p8 gpuasm_pool[GPUASM_POOL];
static positive gpuasm_pool_used;

//      What the last run made, or the one line it stopped with.
static p8 gpuasm_output[GPUASM_TEXT];
static positive gpuasm_output_length;
static p8 gpuasm_message[512];

static struct
{
        string_address file;
        positive line;
        positive depth;
        gpuasm_span macro;
        bool text;
        bool target;
        bool block;
        positive seen;
        positive vgprs;
        gpuasm_span kernel;
        positive kernel_line;
} gpuasm;

static bool gpuasm_run(string_address from, string_address stop, positive line);

/*
        The message. file:line: what, and the text it is about; inside a
        macro's expansion the line is the one that called it, and the macro
        is named.
*/
static void gpuasm_put(p8 address_to into, positive address_to used, positive room,
                       string_address text, positive length)
{
        for (positive at = 0; at < length && *used + 1 < room; at++)
                into[(*used)++] = text[at];
        into[*used] = end;
}

static bool gpuasm_fail(string_address what, string_address detail, positive length)
{
        positive used = 0;
        p8 number[24];
        positive digits = 0;
        positive value = gpuasm.line;

        gpuasm_put(gpuasm_message, address_of used, sizeof(gpuasm_message), gpuasm.file, string_length(gpuasm.file));
        gpuasm_put(gpuasm_message, address_of used, sizeof(gpuasm_message), ":", 1);
        do
                number[digits++] = (p8)('0' + value % 10);
        while (value /= 10);
        while (digits)
                gpuasm_put(gpuasm_message, address_of used, sizeof(gpuasm_message), number + --digits, 1);
        gpuasm_put(gpuasm_message, address_of used, sizeof(gpuasm_message), ": ", 2);
        if (gpuasm.macro.length)
        {
                gpuasm_put(gpuasm_message, address_of used, sizeof(gpuasm_message), "in macro ", 9);
                gpuasm_put(gpuasm_message, address_of used, sizeof(gpuasm_message), gpuasm.macro.at, gpuasm.macro.length);
                gpuasm_put(gpuasm_message, address_of used, sizeof(gpuasm_message), ": ", 2);
        }
        gpuasm_put(gpuasm_message, address_of used, sizeof(gpuasm_message), what, string_length(what));
        if (length)
        {
                gpuasm_put(gpuasm_message, address_of used, sizeof(gpuasm_message), " '", 2);
                gpuasm_put(gpuasm_message, address_of used, sizeof(gpuasm_message), detail, length);
                gpuasm_put(gpuasm_message, address_of used, sizeof(gpuasm_message), "'", 1);
        }
        return false;
}

static bool gpuasm_fail_span(string_address what, gpuasm_span text)
{
        return gpuasm_fail(what, text.at, text.length);
}

static bool gpuasm_blank(p8 byte)
{
        return byte == ' ' || byte == '\t' || byte == '\r';
}

//      The bytes of a name: a mnemonic, a label, a register, a number.
static bool gpuasm_name_byte(p8 byte)
{
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
               byte == '_' || byte == '.' || byte == '$';
}

static gpuasm_span gpuasm_trim(gpuasm_span text)
{
        while (text.length && gpuasm_blank(*text.at))
        {
                text.at++;
                text.length--;
        }
        while (text.length && gpuasm_blank(text.at[text.length - 1]))
                text.length--;
        return text;
}

//      A line without its comment (; to the end, outside a string) or its blanks.
static gpuasm_span gpuasm_code(gpuasm_span line)
{
        bool quoted = false;

        for (positive at = 0; at < line.length; at++)
        {
                if (line.at[at] == '"')
                        quoted = !quoted;
                else if (line.at[at] == ';' && !quoted)
                {
                        line.length = at;
                        break;
                }
        }
        return gpuasm_trim(line);
}

static bool gpuasm_is(gpuasm_span text, string_address word)
{
        positive length = string_length(word);

        return text.length == length && !memory_compare(text.at, word, length);
}

static bool gpuasm_ends(gpuasm_span text, string_address suffix)
{
        positive length = string_length(suffix);

        return text.length > length && !memory_compare(text.at + text.length - length, suffix, length);
}

//      The first word of a trimmed span, and what follows it trimmed.
static gpuasm_span gpuasm_word(gpuasm_span text, gpuasm_span address_to rest)
{
        gpuasm_span word = {text.at, 0};

        while (word.length < text.length && gpuasm_name_byte(text.at[word.length]))
                word.length++;
        if (!word.length && text.length)
                word.length = 1;
        rest->at = text.at + word.length;
        rest->length = text.length - word.length;
        *rest = gpuasm_trim(*rest);
        return word;
}

/*
        A number as the file writes one: decimal or 0x hex, a minus sign in
        front. A leading zero before more digits is octal to LLVM, so it is
        refused here and not read as decimal.
*/
static bool gpuasm_number(gpuasm_span text, b64 address_to value)
{
        positive at = 0;
        positive base = 10;
        bool negative = false;
        p64 magnitude = 0;

        if (at < text.length && text.at[at] == '-')
        {
                negative = true;
                at++;
        }
        if (at + 1 < text.length && text.at[at] == '0' && (text.at[at + 1] == 'x' || text.at[at + 1] == 'X'))
        {
                base = 16;
                at += 2;
        }
        else if (at + 1 < text.length && text.at[at] == '0')
                return false;
        if (at >= text.length)
                return false;
        for (; at < text.length; at++)
        {
                p8 byte = text.at[at];
                positive digit;

                if (byte >= '0' && byte <= '9')
                        digit = byte - '0';
                else if (base == 16 && byte >= 'a' && byte <= 'f')
                        digit = byte - 'a' + 10;
                else if (base == 16 && byte >= 'A' && byte <= 'F')
                        digit = byte - 'A' + 10;
                else
                        return false;
                magnitude = magnitude * base + digit;
                if (magnitude > 0xffffffffffull)
                        return false;
        }
        *value = negative ? -(b64)magnitude : (b64)magnitude;
        return true;
}

static bool gpuasm_emit(p32 word)
{
        if (gpuasm_position >= GPUASM_WORDS)
                return gpuasm_fail("the kernels are more than the room for their code", null, 0);
        gpuasm_words[gpuasm_position++] = word;
        return true;
}

/*
        Names that must outlive the text they were read from (an expansion's
        text is gone when it returns): a label's, a kernel's.
*/
static bool gpuasm_keep(gpuasm_span name, positive address_to at)
{
        if (gpuasm_pool_used + name.length > GPUASM_POOL)
                return gpuasm_fail("too many names", null, 0);
        memory_copy(gpuasm_pool + gpuasm_pool_used, name.at, name.length);
        *at = gpuasm_pool_used;
        gpuasm_pool_used += name.length;
        return true;
}

static positive gpuasm_find_label(gpuasm_span name)
{
        for (positive at = 0; at < gpuasm_label_count; at++)
                if (gpuasm_labels[at].length == name.length &&
                    !memory_compare(gpuasm_pool + gpuasm_labels[at].name, name.at, name.length))
                        return at;
        return GPUASM_LABELS;
}

//      The label of a name, made if there is none yet.
static positive gpuasm_label_of(gpuasm_span name)
{
        positive at = gpuasm_find_label(name);

        if (at < GPUASM_LABELS)
                return at;
        if (gpuasm_label_count >= GPUASM_LABELS)
        {
                gpuasm_fail("too many labels", null, 0);
                return GPUASM_LABELS;
        }
        at = gpuasm_label_count;
        if (!gpuasm_keep(name, address_of gpuasm_labels[at].name))
                return GPUASM_LABELS;
        gpuasm_labels[at].length = name.length;
        gpuasm_labels[at].line = gpuasm.line;
        gpuasm_label_count++;
        return at;
}

/*
        Registers. A scalar register pair is even and a quad or an octet is a
        multiple of four, as the hardware has it; vector ranges are anywhere.
        0 when the text is not a register at all, -1 when it is one that is
        wrong (and the message is made).
*/
enum
{
        GPUASM_SGPR = 1,
        GPUASM_VGPR,
        GPUASM_VCC,
        GPUASM_EXEC,
        GPUASM_NULL
};

static b32 gpuasm_register(gpuasm_span text, p32 address_to kind, p32 address_to first, p32 address_to count)
{
        gpuasm_span digits;
        b64 low;
        b64 high;

        *count = 1;
        if (gpuasm_is(text, "vcc") || gpuasm_is(text, "exec"))
        {
                *kind = text.at[0] == 'v' ? GPUASM_VCC : GPUASM_EXEC;
                *first = text.at[0] == 'v' ? 106 : 126;
                *count = 2;
                return 1;
        }
        if (gpuasm_is(text, "null"))
        {
                *kind = GPUASM_NULL;
                *first = 125;
                return 1;
        }
        if (text.length < 2 || (text.at[0] != 's' && text.at[0] != 'v'))
                return 0;
        *kind = text.at[0] == 's' ? GPUASM_SGPR : GPUASM_VGPR;
        digits.at = text.at + 1;
        digits.length = text.length - 1;
        if (digits.at[0] == '[')
        {
                positive colon = 1;

                if (digits.at[digits.length - 1] != ']')
                        return 0;
                while (colon < digits.length && digits.at[colon] != ':')
                        colon++;
                if (colon >= digits.length)
                        return 0;
                gpuasm_span from = {digits.at + 1, colon - 1};
                gpuasm_span to = {digits.at + colon + 1, digits.length - colon - 2};

                for (positive at = 0; at < from.length + to.length + 1; at++)
                {
                        p8 byte = at < from.length ? from.at[at] : at == from.length ? '0' : to.at[at - from.length - 1];

                        if (byte < '0' || byte > '9')
                                return 0;
                }
                if (!gpuasm_number(from, address_of low) || !gpuasm_number(to, address_of high))
                        return 0;
                if (low < 0 || high < low)
                        return gpuasm_fail("a register range that is not low:high", text.at, text.length), -1;
                *count = (p32)(high - low + 1);
        }
        else
        {
                for (positive at = 0; at < digits.length; at++)
                        if (digits.at[at] < '0' || digits.at[at] > '9')
                                return 0;
                if (!gpuasm_number(digits, address_of low))
                        return 0;
                high = low;
        }
        if (high > (*kind == GPUASM_SGPR ? 105 : 255))
                return gpuasm_fail("a register out of range", text.at, text.length), -1;
        *first = (p32)low;
        return 1;
}

//      The code of an inline integer constant, or 0 when the value has none.
static p32 gpuasm_inline(b64 value)
{
        if (value == 0)
                return 128;
        if (value >= 1 && value <= 64)
                return (p32)(128 + value);
        if (value <= -1 && value >= -16)
                return (p32)(192 - value);
        return 0;
}

/*
        A source: a register or a constant. kind says what may be there:
        i the scalar sources (no vector register), k the lane mask (a pair,
        vcc, exec), s the vector ones. A 32-bit constant is an inline code
        when it has one (the integers -16 to 64 and the nine floats), and a
        literal when not; the packed forms take neither of the floats nor a
        literal, and size 2 takes an inline integer only.
*/
static bool gpuasm_source(gpuasm_span text, p8 kind, positive size, bool literals, gpuasm_operand address_to out)
{
        p32 type;
        p32 first;
        p32 count;
        b32 found = gpuasm_register(text, address_of type, address_of first, address_of count);
        b64 value;

        if (found < 0)
                return false;
        if (found)
        {
                if (type == GPUASM_NULL)
                        return gpuasm_fail_span("null is not a source here", text);
                if (type == GPUASM_VCC || type == GPUASM_EXEC)
                {
                        if (size != 2)
                                return gpuasm_fail_span("a 64-bit register where 32 bits are wanted", text);
                        out->code = first;
                        return true;
                }
                if (count != size)
                        return gpuasm_fail_span("the wrong number of registers", text);
                if (type == GPUASM_VGPR)
                {
                        if (kind == 'i' || kind == 'k')
                                return gpuasm_fail_span("a vector register where a scalar one is wanted", text);
                        out->code = 256 + first;
                        out->vector = true;
                        return true;
                }
                if ((size == 2 && (first & 1)) || (size > 2 && (first & 3)))
                        return gpuasm_fail_span("a scalar register range that is not aligned", text);
                out->code = first;
                return true;
        }
        if (kind == 'k')
                return gpuasm_fail_span("a lane mask is a register pair, vcc or exec", text);
        if (!gpuasm_number(text, address_of value))
                return gpuasm_fail_span("unknown operand", text);
        if (value < -2147483648ll || value > 4294967295ll)
                return gpuasm_fail_span("a constant of more than 32 bits", text);
        out->code = gpuasm_inline(value);
        if (!out->code && size == 1 && value > 2147483647ll)
                out->code = gpuasm_inline(value - 4294967296ll);
        if (out->code)
                return true;
        if (size == 2)
                return gpuasm_fail_span("a 64-bit operand takes an inline constant only", text);
        if (!literals)
                return gpuasm_fail_span("a literal where this form has none", text);
        for (positive at = 0; at < sizeof(gpuasm_floats) / sizeof(gpuasm_floats[0]); at++)
                if (gpuasm_floats[at] == (p32)value)
                {
                        out->code = 240 + (p32)at;
                        return true;
                }
        out->code = 255;
        out->has_literal = true;
        out->literal = (p32)value;
        out->written = value;
        return true;
}

//      A destination register: p scalar (a pair may be vcc or exec), m the
//      lane mask the same, n that or null, d vector.
static bool gpuasm_destination(gpuasm_span text, p8 kind, positive size, gpuasm_operand address_to out)
{
        p32 type;
        p32 first;
        p32 count;
        b32 found = gpuasm_register(text, address_of type, address_of first, address_of count);

        if (found < 0)
                return false;
        if (!found)
                return gpuasm_fail_span("unknown operand", text);
        if (kind == 'd' && type != GPUASM_VGPR)
                return gpuasm_fail_span("a vector register is wanted", text);
        if (kind != 'd' && type == GPUASM_VGPR)
                return gpuasm_fail_span("a scalar register is wanted", text);
        if (type == GPUASM_NULL && kind != 'n')
                return gpuasm_fail_span("null is not a destination here", text);
        if (type == GPUASM_VCC || type == GPUASM_EXEC)
        {
                if (size != 2)
                        return gpuasm_fail_span("a 64-bit register where 32 bits are wanted", text);
        }
        else if (type != GPUASM_NULL && count != size)
                return gpuasm_fail_span("the wrong number of registers", text);
        if (type == GPUASM_SGPR && ((size == 2 && (first & 1)) || (size > 2 && (first & 3))))
                return gpuasm_fail_span("a scalar register range that is not aligned", text);
        out->code = first;
        out->vector = type == GPUASM_VGPR;
        return true;
}

//      s_waitcnt's counters, vmcnt(n) expcnt(n) lgkmcnt(n) in any order, joined by blanks, commas or &,
//      the ones left out as large as they go.
static bool gpuasm_waitcnt(gpuasm_span text, p32 address_to bits)
{
        p32 vm = 63;
        p32 exp = 7;
        p32 lgkm = 63;
        positive seen = 0;

        text = gpuasm_trim(text);
        if (!text.length)
                return gpuasm_fail("s_waitcnt wants its counters", null, 0);
        while (text.length)
        {
                gpuasm_span name = {text.at, 0};
                gpuasm_span number;
                b64 value;
                positive which;

                while (name.length < text.length && text.at[name.length] != '(')
                        name.length++;
                if (name.length >= text.length)
                        return gpuasm_fail_span("a counter that is not name(number)", text);
                number.at = text.at + name.length + 1;
                number.length = 0;
                while (name.length + 1 + number.length < text.length && number.at[number.length] != ')')
                        number.length++;
                if (name.length + 1 + number.length >= text.length || !gpuasm_number(number, address_of value) || value < 0)
                        return gpuasm_fail_span("a counter that is not name(number)", text);
                if (gpuasm_is(name, "vmcnt"))
                        which = 0;
                else if (gpuasm_is(name, "expcnt"))
                        which = 1;
                else if (gpuasm_is(name, "lgkmcnt"))
                        which = 2;
                else
                        return gpuasm_fail_span("unknown counter", name);
                if (seen & (1u << which))
                        return gpuasm_fail_span("a counter given twice", name);
                if (value > (which == 1 ? 7 : 63))
                        return gpuasm_fail_span("a counter past its largest", name);
                seen |= 1u << which;
                if (which == 0)
                        vm = (p32)value;
                else if (which == 1)
                        exp = (p32)value;
                else
                        lgkm = (p32)value;
                text.at += name.length + 1 + number.length + 1;
                text.length -= name.length + 1 + number.length + 1;
                while (text.length && (gpuasm_blank(*text.at) || *text.at == '&' || *text.at == ','))
                {
                        text.at++;
                        text.length--;
                }
        }
        *bits = (vm & 15) | exp << 4 | lgkm << 8 | (vm >> 4) << 14;
        return true;
}

//      The operands of a line, split at the commas outside brackets.
static bool gpuasm_split(gpuasm_span text, gpuasm_span address_to parts, positive address_to count)
{
        positive depth = 0;
        positive start = 0;

        *count = 0;
        text = gpuasm_trim(text);
        if (!text.length)
                return true;
        for (positive at = 0; at <= text.length; at++)
        {
                if (at < text.length && (text.at[at] == '[' || text.at[at] == '('))
                        depth++;
                else if (at < text.length && (text.at[at] == ']' || text.at[at] == ')') && depth)
                        depth--;
                else if (at == text.length || (text.at[at] == ',' && !depth))
                {
                        gpuasm_span part = {text.at + start, at - start};

                        if (*count >= GPUASM_OPERANDS)
                                return gpuasm_fail("too many operands", null, 0);
                        part = gpuasm_trim(part);
                        if (!part.length)
                                return gpuasm_fail("an empty operand", null, 0);
                        parts[(*count)++] = part;
                        start = at + 1;
                }
        }
        return true;
}

static const gpuasm_op *gpuasm_find(gpuasm_span name)
{
        for (positive at = 0; at < sizeof(gpuasm_ops) / sizeof(gpuasm_ops[0]); at++)
                if (gpuasm_is(name, gpuasm_ops[at].name))
                        return gpuasm_ops + at;
        return null;
}

static bool gpuasm_instruction(gpuasm_span mnemonic, gpuasm_span rest)
{
        const gpuasm_op *op = gpuasm_find(mnemonic);
        gpuasm_span part[GPUASM_OPERANDS];
        gpuasm_operand o[GPUASM_OPERANDS];
        positive found = 0;
        positive wanted;
        positive force = 0;
        bool clamp = false;
        bool offset_given = false;
        bool off;
        bool literal = false;
        b64 offset = 0;
        p32 immediate = 0;
        p32 value = 0;
        b64 written = 0;
        p32 w0;
        p32 w1 = 0;
        p32 src[3] = {128, 128, 128};
        positive form;

        if (!gpuasm.text)
                return gpuasm_fail_span("an instruction outside .text", mnemonic);
        if (!op && gpuasm_ends(mnemonic, "_e64"))
        {
                gpuasm_span base = {mnemonic.at, mnemonic.length - 4};

                op = gpuasm_find(base);
                force = 64;
                if (op && ((op->form != GPUASM_VOP1 && op->form != GPUASM_VOP2 && op->form != GPUASM_VOP2C &&
                            op->form != GPUASM_VOPC && op->form != GPUASM_VOP3 && op->form != GPUASM_VOP3B) ||
                           gpuasm_ends((gpuasm_span){(string_address)op->name, string_length(op->name)}, "_e64")))
                        op = null;
        }
        else if (!op && gpuasm_ends(mnemonic, "_e32"))
        {
                gpuasm_span base = {mnemonic.at, mnemonic.length - 4};

                op = gpuasm_find(base);
                force = 32;
                if (op && op->form != GPUASM_VOP1 && op->form != GPUASM_VOP2 && op->form != GPUASM_VOP2C &&
                    op->form != GPUASM_VOPC)
                        op = null;
        }
        if (!op)
                return gpuasm_fail_span("unknown instruction", mnemonic);
        form = op->form;
        wanted = string_length(op->shape) / 2;

        //      The modifiers after the operands: clamp, and offset:N on a memory instruction.
        for (;;)
        {
                positive start = rest.length;
                gpuasm_span word;

                while (start && !gpuasm_blank(rest.at[start - 1]))
                        start--;
                word.at = rest.at + start;
                word.length = rest.length - start;
                if (gpuasm_is(word, "clamp") && !clamp)
                        clamp = true;
                else if (word.length > 7 && !memory_compare(word.at, "offset:", 7) && !offset_given)
                {
                        gpuasm_span number = {word.at + 7, word.length - 7};

                        if (!gpuasm_number(number, address_of offset) || offset < -2048 || offset > 2047)
                                return gpuasm_fail_span("an offset of -2048 to 2047, not", word);
                        offset_given = true;
                }
                else
                        break;
                rest.length = start;
                rest = gpuasm_trim(rest);
        }
        if (offset_given && form != GPUASM_GLD && form != GPUASM_GST)
                return gpuasm_fail_span("an offset on an instruction that has none:", mnemonic);
        if (clamp)
        {
                bool takes = false;

                for (positive at = 0; at < sizeof(gpuasm_clamps) / sizeof(gpuasm_clamps[0]); at++)
                        takes = takes || string_equals(op->name, gpuasm_clamps[at]);
                if (!takes)
                        return gpuasm_fail_span("clamp on an instruction that has none:", mnemonic);
        }

        if (wanted == 1 && op->shape[0] == 'w')
        {
                part[0] = rest;
                found = rest.length ? 1 : 0;
        }
        else if (!gpuasm_split(rest, part, address_of found))
                return false;
        if (found != wanted)
        {
                gpuasm_fail("the wrong number of operands for", mnemonic.at, mnemonic.length);
                return false;
        }

        //      A global's address is a vector register and a scalar pair, or, with off, a pair of vector registers.
        off = (form == GPUASM_GLD || form == GPUASM_GST) && gpuasm_is(part[2], "off");
        memory_zero(o, sizeof(o));
        for (positive at = 0; at < wanted; at++)
        {
                p8 kind = op->shape[2 * at];
                positive size = (positive)(op->shape[2 * at + 1] - '0');
                bool ok = true;

                if (off && kind == 'v' && at == (form == GPUASM_GLD ? 1u : 0u))
                        size = 2;

                if ((form == GPUASM_VOP2 || form == GPUASM_VOPC) && kind == 'v' && at == 2)
                        kind = op->shape[2];
                if (form == GPUASM_VOP2C && kind == 'v' && at == 3)
                        kind = op->shape[4];
                if (kind == 'd' || kind == 'p' || kind == 'm' || kind == 'n')
                {
                        ok = gpuasm_destination(part[at], kind, size, o + at);
                        if (ok && form == GPUASM_SMEM && o[at].code > 105)
                                ok = gpuasm_fail_span("a scalar register to load into", part[at]);
                }
                else if (kind == 'v')
                {
                        ok = gpuasm_source(part[at], 's', size, false, o + at);
                        if (ok && !o[at].vector)
                                ok = gpuasm_fail_span("a vector register is wanted", part[at]);
                }
                else if (kind == 'b' && off)
                        o[at].code = 125;
                else if (kind == 'b')
                {
                        ok = gpuasm_destination(part[at], 'p', size, o + at);
                        if (ok && o[at].code > 105)
                                ok = gpuasm_fail_span("a scalar register pair is wanted", part[at]);
                }
                else if (kind == 's' || kind == 'i' || kind == 'k')
                        ok = gpuasm_source(part[at], kind, size, form != GPUASM_VOP3P, o + at);
                else if (kind == 'o')
                {
                        b64 offset;

                        if (!gpuasm_number(part[at], address_of offset) || offset < -0x100000 || offset > 0xfffff)
                                ok = gpuasm_fail_span("an offset of -0x100000 to 0xfffff", part[at]);
                        else
                                o[at].literal = (p32)offset & 0x1fffff;
                }
                else if (kind == 'l')
                {
                        gpuasm_span name = part[at];

                        for (positive in = 0; in < name.length; in++)
                                if (!gpuasm_name_byte(name.at[in]))
                                        return gpuasm_fail_span("a label is wanted", name);
                        o[at].label = name;
                }
                else if (kind == 'w')
                        ok = gpuasm_waitcnt(part[at], address_of immediate);
                if (!ok)
                        return false;
                if (o[at].has_literal)
                {
                        if (literal && written != o[at].written)
                                return gpuasm_fail("only one literal to an instruction", part[at].at, part[at].length);
                        literal = true;
                        written = o[at].written;
                        value = o[at].literal;
                }
        }

        //      Which encoding a VOP1, VOP2 or VOPC is written in.
        if (form == GPUASM_VOP1 || form == GPUASM_VOP2 || form == GPUASM_VOPC)
        {
                bool fits = !clamp;

                if (form == GPUASM_VOP2 || form == GPUASM_VOPC)
                        fits = fits && o[2].vector;
                if (form == GPUASM_VOPC)
                        fits = fits && o[0].code == 106;
                if (force == 32 && !fits)
                        return gpuasm_fail_span("operands that do not fit the 32-bit encoding of", mnemonic);
                if (force == 64 || !fits)
                {
                        p32 base = form == GPUASM_VOP1 ? 0x180 : form == GPUASM_VOP2 ? 0x100 : 0;

                        src[0] = o[1].code;
                        if (form != GPUASM_VOP1)
                                src[1] = o[2].code;
                        w0 = 0xd4000000u | (base + op->code) << 16 | (clamp ? 1u << 15 : 0) | o[0].code;
                        w1 = src[0] | src[1] << 9 | src[2] << 18;
                        form = GPUASM_VOP3;
                }
                else if (form == GPUASM_VOP1)
                        w0 = 0x7e000000u | o[0].code << 17 | (p32)op->code << 9 | o[1].code;
                else if (form == GPUASM_VOP2)
                        w0 = (p32)op->code << 25 | o[0].code << 17 | (o[2].code - 256) << 9 | o[1].code;
                else
                        w0 = 0x7c000000u | (p32)op->code << 17 | (o[2].code - 256) << 9 | o[1].code;
                if (form != GPUASM_VOP3)
                        w1 = 0;
        }
        else if (form == GPUASM_VOP2C)
        {
                bool fits = o[1].code == 106 && o[3].vector && (wanted < 5 || o[4].code == 106);

                if (force == 32 && !fits)
                        return gpuasm_fail_span("operands that do not fit the 32-bit encoding of", mnemonic);
                if (force == 64 || !fits)
                {
                        w0 = 0xd4000000u | (0x100u + op->code) << 16 | o[1].code << 8 | o[0].code;
                        w1 = o[2].code | o[3].code << 9 | (wanted < 5 ? 128u : o[4].code) << 18;
                        form = GPUASM_VOP3B;
                }
                else
                        w0 = (p32)op->code << 25 | o[0].code << 17 | (o[3].code - 256) << 9 | o[2].code;
        }
        else if (form == GPUASM_VOP3)
        {
                for (positive at = 1; at < wanted; at++)
                        src[at - 1] = o[at].code;
                w0 = 0xd4000000u | (p32)op->code << 16 | (clamp ? 1u << 15 : 0) | o[0].code;
                w1 = src[0] | src[1] << 9 | src[2] << 18;
        }
        else if (form == GPUASM_VOP3B)
        {
                w0 = 0xd4000000u | (p32)op->code << 16 | (clamp ? 1u << 15 : 0) | o[1].code << 8 | o[0].code;
                w1 = o[2].code | o[3].code << 9 | (wanted < 5 ? 128u : o[4].code) << 18;
        }
        else if (form == GPUASM_VOP3P)
        {
                w0 = 0xcc000000u | (p32)op->code << 16 | (clamp ? 1u << 15 : 0) | 1u << 14 | o[0].code;
                w1 = o[1].code | o[2].code << 9 | 128u << 18 | 3u << 27;
        }
        else if (form == GPUASM_SOP1)
                w0 = 0xbe800000u | o[0].code << 16 | (p32)op->code << 8 | o[1].code;
        else if (form == GPUASM_SOP2)
                w0 = 0x80000000u | (p32)op->code << 23 | o[0].code << 16 | o[2].code << 8 | o[1].code;
        else if (form == GPUASM_SOPC)
                w0 = 0xbf000000u | (p32)op->code << 16 | o[1].code << 8 | o[0].code;
        else if (form == GPUASM_SOPP)
        {
                w0 = GPUASM_NOP | (p32)op->code << 16 | immediate;
                if (wanted && op->shape[0] == 'l')
                {
                        gpuasm_fixup *fix;

                        if (gpuasm_fixup_count >= GPUASM_FIXUPS)
                                return gpuasm_fail("too many branches", null, 0);
                        fix = gpuasm_fixups + gpuasm_fixup_count++;
                        fix->at = gpuasm_position;
                        fix->line = gpuasm.line;
                        fix->label = gpuasm_label_of(o[0].label);
                        if (fix->label >= GPUASM_LABELS)
                                return false;
                }
        }
        else if (form == GPUASM_SMEM)
        {
                w0 = 0xf4000000u | (p32)op->code << 18 | o[0].code << 6 | o[1].code >> 1;
                w1 = 0xfa000000u | o[2].literal;
        }
        else if (form == GPUASM_DS)
        {
                w0 = 0xd8000000u | (p32)op->code << 18;
                w1 = (o[1].code - 256) | (o[2].code - 256) << 8 | o[0].code << 24;
        }
        else if (form == GPUASM_GLD)
        {
                w0 = 0xdc008000u | (p32)op->code << 18 | ((p32)offset & 0xfff);
                w1 = (o[1].code - 256) | o[2].code << 16 | o[0].code << 24;
        }
        else
        {
                w0 = 0xdc008000u | (p32)op->code << 18 | ((p32)offset & 0xfff);
                w1 = (o[0].code - 256) | (o[1].code - 256) << 8 | o[2].code << 16;
        }

        //      The long encodings read two scalar values at most: distinct registers, and a literal as one.
        if (form == GPUASM_VOP3 || form == GPUASM_VOP3B || form == GPUASM_VOP3P)
        {
                p32 scalars[GPUASM_OPERANDS];
                positive used = 0;

                for (positive at = form == GPUASM_VOP3B ? 2 : 1; at < wanted; at++)
                {
                        bool known = false;
                        p32 key = o[at].code | (op->shape[2 * at + 1] == '2' ? 0x1000u : 0);

                        if (o[at].vector || (o[at].code >= 128 && o[at].code != 255))
                                continue;
                        for (positive in = 0; in < used; in++)
                                known = known || scalars[in] == key;
                        if (!known)
                                scalars[used++] = key;
                }
                if (used > 2)
                        return gpuasm_fail_span("more than two scalar values to read at once:", mnemonic);
        }

        if (!gpuasm_emit(w0))
                return false;
        if (form == GPUASM_VOP3 || form == GPUASM_VOP3B || form == GPUASM_VOP3P || form == GPUASM_SMEM ||
            form == GPUASM_DS || form == GPUASM_GLD || form == GPUASM_GST)
                if (!gpuasm_emit(w1))
                        return false;
        if (literal && !gpuasm_emit(value))
                return false;
        return true;
}

static bool gpuasm_define(gpuasm_span name)
{
        positive at = gpuasm_label_of(name);

        if (at >= GPUASM_LABELS)
                return false;
        if (!gpuasm.text)
                return gpuasm_fail_span("a label outside .text", name);
        if (gpuasm_labels[at].defined)
                return gpuasm_fail_span("a label defined twice", name);
        gpuasm_labels[at].defined = true;
        gpuasm_labels[at].at = gpuasm_position;
        gpuasm_labels[at].line = gpuasm.line;
        return true;
}

static bool gpuasm_next(gpuasm_cursor address_to cursor, gpuasm_span address_to line)
{
        string_address stop = cursor->at;

        if (cursor->at >= cursor->stop)
                return false;
        while (stop < cursor->stop && *stop != '\n')
                stop++;
        line->at = cursor->at;
        line->length = (positive)(stop - cursor->at);
        cursor->at = stop < cursor->stop ? stop + 1 : stop;
        cursor->line++;
        return true;
}

//      The lines up to the close that matches the open just read, returned whole as they are.
static bool gpuasm_block(gpuasm_cursor address_to cursor, string_address open, string_address close,
                         gpuasm_span address_to body)
{
        positive depth = 1;
        gpuasm_span line;

        body->at = cursor->at;
        for (;;)
        {
                string_address before = cursor->at;
                gpuasm_span rest;
                gpuasm_span word;

                if (!gpuasm_next(cursor, address_of line))
                        return gpuasm_fail("a block that is never closed, wanted", (string_address)close, string_length(close));
                word = gpuasm_word(gpuasm_code(line), address_of rest);
                if (gpuasm_is(word, open))
                        depth++;
                else if (gpuasm_is(word, close) && --depth == 0)
                {
                        if (rest.length)
                        {
                                if (!gpuasm.depth)
                                        gpuasm.line = cursor->line;
                                return gpuasm_fail_span("text after the close of a block:", rest);
                        }
                        body->length = (positive)(before - body->at);
                        return true;
                }
        }
}

/*
        The text of a body with the names given to it in place of \name (a
        macro's parameters, an .irp's variable), put in the arena, one line
        after another, and run. A name that is not one given is left, as LLVM
        leaves it, for whatever text it is inside to take; one that is still
        there when a statement is read is an error.
*/
static bool gpuasm_expand(gpuasm_span body, const gpuasm_span *names, const gpuasm_span *values, positive count,
                          gpuasm_span label)
{
        positive mark = gpuasm_arena_used;
        gpuasm_cursor cursor = {body.at, body.at + body.length, 0};
        gpuasm_span raw;
        bool ok;
        gpuasm_span saved = gpuasm.macro;

        if (gpuasm.depth >= GPUASM_DEPTH)
                return gpuasm_fail("expansions nested too deep", null, 0);
        while (gpuasm_next(address_of cursor, address_of raw))
        {
                gpuasm_span line = gpuasm_code(raw);

                for (positive at = 0; at < line.length; at++)
                {
                        positive taken = 1;
                        gpuasm_span put = {line.at + at, 1};

                        if (line.at[at] == '\\')
                        {
                                positive length = 0;

                                while (at + 1 + length < line.length && gpuasm_name_byte(line.at[at + 1 + length]) &&
                                       line.at[at + 1 + length] != '.')
                                        length++;
                                for (positive which = 0; which < count; which++)
                                        if (names[which].length == length && !memory_compare(names[which].at, line.at + at + 1, length))
                                        {
                                                put = values[which];
                                                taken = 1 + length;
                                                break;
                                        }
                        }
                        if (gpuasm_arena_used + put.length + 1 >= GPUASM_ARENA)
                        {
                                gpuasm_arena_used = mark;
                                return gpuasm_fail("expansions past the room for them", null, 0);
                        }
                        memory_copy(gpuasm_arena + gpuasm_arena_used, put.at, put.length);
                        gpuasm_arena_used += put.length;
                        at += taken - 1;
                }
                gpuasm_arena[gpuasm_arena_used++] = '\n';
        }
        gpuasm.depth++;
        if (label.length)
                gpuasm.macro = label;
        ok = gpuasm_run(gpuasm_arena + mark, gpuasm_arena + gpuasm_arena_used, 0);
        gpuasm.depth--;
        gpuasm.macro = saved;
        gpuasm_arena_used = mark;
        return ok;
}

static bool gpuasm_macro_define(gpuasm_cursor address_to cursor, gpuasm_span rest)
{
        gpuasm_macro *macro;
        gpuasm_span name;
        gpuasm_span parts[GPUASM_PARAMS + 1];
        positive found;

        if (gpuasm.depth)
                return gpuasm_fail("a macro defined inside an expansion", null, 0);
        if (gpuasm_macro_count >= GPUASM_MACROS)
                return gpuasm_fail("too many macros", null, 0);
        macro = gpuasm_macros + gpuasm_macro_count;
        name = gpuasm_word(rest, address_of rest);
        if (!name.length || !gpuasm_name_byte(*name.at))
                return gpuasm_fail(".macro wants a name", null, 0);
        for (positive at = 0; at < gpuasm_macro_count; at++)
                if (gpuasm_macros[at].name.length == name.length &&
                    !memory_compare(gpuasm_macros[at].name.at, name.at, name.length))
                        return gpuasm_fail_span("a macro defined twice", name);
        if (!gpuasm_split(rest, parts, address_of found) || found > GPUASM_PARAMS)
                return gpuasm_fail("a macro with more parameters than this takes", null, 0);
        for (positive at = 0; at < found; at++)
        {
                for (positive in = 0; in < parts[at].length; in++)
                        if (!gpuasm_name_byte(parts[at].at[in]) || parts[at].at[in] == '.')
                                return gpuasm_fail_span("a parameter that is not a plain name", parts[at]);
                macro->params[at] = parts[at];
        }
        macro->count = found;
        macro->name = name;
        if (!gpuasm_block(cursor, ".macro", ".endm", address_of macro->body))
                return false;
        gpuasm_macro_count++;
        return true;
}

static bool gpuasm_irp(gpuasm_cursor address_to cursor, gpuasm_span rest)
{
        gpuasm_span variable;
        gpuasm_span body;
        gpuasm_span parts[GPUASM_VALUES];

        variable = gpuasm_word(rest, address_of rest);
        if (!variable.length || !gpuasm_name_byte(*variable.at))
                return gpuasm_fail(".irp wants a variable", null, 0);
        if (rest.length && *rest.at == ',')
        {
                rest.at++;
                rest.length--;
        }
        if (!gpuasm_block(cursor, ".irp", ".endr", address_of body))
                return false;
        {
                positive count = 0;
                positive start = 0;
                gpuasm_span list = gpuasm_trim(rest);

                for (positive at = 0; at <= list.length && list.length; at++)
                        if (at == list.length || list.at[at] == ',')
                        {
                                gpuasm_span part = {list.at + start, at - start};

                                part = gpuasm_trim(part);
                                if (count >= sizeof(parts) / sizeof(parts[0]))
                                        return gpuasm_fail("an .irp with more values than this takes", null, 0);
                                parts[count++] = part;
                                start = at + 1;
                        }
                for (positive at = 0; at < count; at++)
                        if (!gpuasm_expand(body, address_of variable, parts + at, 1, (gpuasm_span){0, 0}))
                                return false;
        }
        return true;
}

static bool gpuasm_macro_call(const gpuasm_macro *macro, gpuasm_span rest)
{
        gpuasm_span values[GPUASM_PARAMS + 1];
        positive found;

        if (!gpuasm_split(rest, values, address_of found))
                return false;
        if (found != macro->count)
                return gpuasm_fail_span("the wrong number of arguments to the macro", macro->name);
        return gpuasm_expand(macro->body, macro->params, values, found, macro->name);
}

//      .size name, .-name: where a kernel's code ends.
static bool gpuasm_size(gpuasm_span rest)
{
        gpuasm_span parts[GPUASM_OPERANDS];
        positive found;
        positive at;

        if (!gpuasm_split(rest, parts, address_of found) || found != 2)
                return gpuasm_fail(".size wants a name and .-name", null, 0);
        if (parts[1].length != parts[0].length + 2 || parts[1].at[0] != '.' || parts[1].at[1] != '-' ||
            memory_compare(parts[1].at + 2, parts[0].at, parts[0].length))
                return gpuasm_fail_span(".size is read as name, .-name only", rest);
        at = gpuasm_find_label(parts[0]);
        if (at >= GPUASM_LABELS || !gpuasm_labels[at].defined)
                return gpuasm_fail_span(".size of a name with no label", parts[0]);
        gpuasm_labels[at].stop = gpuasm_position;
        gpuasm_labels[at].sized = true;
        return true;
}

static bool gpuasm_property(gpuasm_span head, gpuasm_span rest)
{
        b64 value;

        if (!gpuasm.block)
                return gpuasm_fail_span("a property outside an .amdhsa_kernel block", head);
        for (positive at = 0; at < GPUASM_PROPERTIES; at++)
        {
                if (!gpuasm_is(head, gpuasm_properties[at].name))
                        continue;
                if (!gpuasm_number(rest, address_of value) || value < 0)
                        return gpuasm_fail_span("a number is wanted", rest);
                if (gpuasm.seen & (1u << at))
                        return gpuasm_fail_span("a property given twice", head);
                gpuasm.seen |= 1u << at;
                if (at == GPUASM_VGPRS)
                {
                        if (value < 1 || value > 256)
                                return gpuasm_fail_span("vector registers of 1 to 256", rest);
                        gpuasm.vgprs = (positive)value;
                }
                else if (at == GPUASM_SGPRS)
                {
                        if (value > 106)
                                return gpuasm_fail_span("scalar registers of 0 to 106", rest);
                }
                else if (value != gpuasm_properties[at].value)
                        return gpuasm_fail_span(at == 1 ? "the dispatch gives a kernel no scratch:"
                                                        : "not the value this assembles for:",
                                                head);
                return true;
        }
        return gpuasm_fail_span("unknown directive", head);
}

static bool gpuasm_directive(gpuasm_cursor address_to cursor, gpuasm_span head, gpuasm_span rest)
{
        if (gpuasm_is(head, ".amdgcn_target"))
        {
                if (!gpuasm_is(rest, GPUASM_TARGET))
                        return gpuasm_fail_span("this assembles for " GPUASM_TARGET " only, not", rest);
                gpuasm.target = true;
                return true;
        }
        if (gpuasm_is(head, ".text") || gpuasm_is(head, ".rodata"))
        {
                if (rest.length)
                        return gpuasm_fail_span("a section with arguments", rest);
                gpuasm.text = head.at[1] == 't';
                return true;
        }
        if (gpuasm_is(head, ".globl") || gpuasm_is(head, ".global"))
        {
                if (!rest.length)
                        return gpuasm_fail(".globl wants one name", null, 0);
                for (positive at = 0; at < rest.length; at++)
                        if (!gpuasm_name_byte(rest.at[at]))
                                return gpuasm_fail(".globl wants one name", null, 0);
                return true;
        }
        if (gpuasm_is(head, ".p2align"))
        {
                b64 power;

                if (!gpuasm_number(rest, address_of power) || power < 0 || power > 12)
                        return gpuasm_fail_span(".p2align wants a power of 0 to 12, not", rest);
                if (gpuasm.text)
                        while (power > 2 && (gpuasm_position & ((1u << (power - 2)) - 1)))
                                if (!gpuasm_emit(GPUASM_NOP))
                                        return false;
                return true;
        }
        if (gpuasm_is(head, ".type"))
        {
                gpuasm_span parts[GPUASM_OPERANDS];
                positive found;

                if (!gpuasm_split(rest, parts, address_of found) || found != 2 || !gpuasm_is(parts[1], "@function"))
                        return gpuasm_fail_span(".type is read as name,@function only, not", rest);
                for (positive at = 0; at < parts[0].length; at++)
                        if (!gpuasm_name_byte(parts[0].at[at]))
                                return gpuasm_fail_span(".type is read as name,@function only, not", rest);
                return true;
        }
        if (gpuasm_is(head, ".size"))
                return gpuasm_size(rest);
        if (gpuasm_is(head, ".macro"))
                return gpuasm_macro_define(cursor, rest);
        if (gpuasm_is(head, ".irp"))
                return gpuasm_irp(cursor, rest);
        if (gpuasm_is(head, ".endm") || gpuasm_is(head, ".endr"))
                return gpuasm_fail_span("a close with no open", head);
        if (gpuasm_is(head, ".amdhsa_kernel"))
        {
                if (gpuasm.text)
                        return gpuasm_fail(".amdhsa_kernel belongs in .rodata", null, 0);
                if (gpuasm.block)
                        return gpuasm_fail("an .amdhsa_kernel inside another", null, 0);
                if (!rest.length)
                        return gpuasm_fail(".amdhsa_kernel wants a name", null, 0);
                for (positive at = 0; at < rest.length; at++)
                        if (!gpuasm_name_byte(rest.at[at]) || rest.at[at] == '.' || rest.at[at] == '$' ||
                            (!at && rest.at[at] >= '0' && rest.at[at] <= '9'))
                                return gpuasm_fail_span("a kernel's name is a C name, not", rest);
                if (gpuasm_kernel_count >= GPUASM_KERNELS)
                        return gpuasm_fail("too many kernels", null, 0);
                for (positive at = 0; at < gpuasm_kernel_count; at++)
                        if (gpuasm_kernels[at].length == rest.length &&
                            !memory_compare(gpuasm_pool + gpuasm_kernels[at].name, rest.at, rest.length))
                                return gpuasm_fail_span("a kernel defined twice", rest);
                gpuasm.block = true;
                gpuasm.seen = 0;
                gpuasm.kernel = rest;
                gpuasm.kernel_line = gpuasm.line;
                return true;
        }
        if (gpuasm_is(head, ".end_amdhsa_kernel"))
        {
                gpuasm_kernel *kernel;

                if (!gpuasm.block)
                        return gpuasm_fail(".end_amdhsa_kernel with no .amdhsa_kernel", null, 0);
                if (rest.length)
                        return gpuasm_fail_span("text after .end_amdhsa_kernel:", rest);
                for (positive at = 0; at < GPUASM_PROPERTIES; at++)
                        if (!(gpuasm.seen & (1u << at)))
                                return gpuasm_fail("the kernel's block has no", gpuasm_properties[at].name,
                                                   string_length(gpuasm_properties[at].name));
                kernel = gpuasm_kernels + gpuasm_kernel_count++;
                if (!gpuasm_keep(gpuasm.kernel, address_of kernel->name))
                        return false;
                kernel->length = gpuasm.kernel.length;
                kernel->vgprs = gpuasm.vgprs;
                kernel->line = gpuasm.kernel_line;
                gpuasm.block = false;
                return true;
        }
        if (head.length > 8 && !memory_compare(head.at, ".amdhsa_", 8))
                return gpuasm_property(head, rest);
        return gpuasm_fail_span("unknown directive", head);
}

static bool gpuasm_statement(gpuasm_cursor address_to cursor, gpuasm_span code)
{
        gpuasm_span head;
        gpuasm_span rest;

        for (;;)
        {
                positive length = 0;

                while (length < code.length && gpuasm_name_byte(code.at[length]))
                        length++;
                if (!length || length >= code.length || code.at[length] != ':')
                        break;
                if (!gpuasm_define((gpuasm_span){code.at, length}))
                        return false;
                code.at += length + 1;
                code.length -= length + 1;
                code = gpuasm_trim(code);
                if (!code.length)
                        return true;
        }
        head = gpuasm_word(code, address_of rest);
        if (!gpuasm_name_byte(*head.at))
                return gpuasm_fail_span("cannot read", code);
        if (*head.at == '.')
                return gpuasm_directive(cursor, head, rest);
        for (positive at = 0; at < gpuasm_macro_count; at++)
                if (gpuasm_macros[at].name.length == head.length &&
                    !memory_compare(gpuasm_macros[at].name.at, head.at, head.length))
                        return gpuasm_macro_call(gpuasm_macros + at, rest);
        for (positive at = 0; at < code.length; at++)
                if (code.at[at] == '\\')
                        return gpuasm_fail_span("a \\name that no macro or .irp gave a value", code);
        if (!gpuasm.target)
                return gpuasm_fail(".amdgcn_target must come before the code", null, 0);
        return gpuasm_instruction(head, rest);
}

//      The statements of a stretch of text. line is the number the text's first line has in the file
//      (0 for an expansion, whose lines are counted as the line that asked for them).
static bool gpuasm_run(string_address from, string_address stop, positive line)
{
        gpuasm_cursor cursor = {from, stop, line};
        gpuasm_span raw;

        while (gpuasm_next(address_of cursor, address_of raw))
        {
                gpuasm_span code = gpuasm_code(raw);

                if (!code.length)
                        continue;
                if (!gpuasm.depth)
                        gpuasm.line = cursor.line;
                if (!gpuasm_statement(address_of cursor, code))
                        return false;
        }
        return true;
}

static void gpuasm_out(string_address text, positive length)
{
        if (gpuasm_output_length + length < GPUASM_TEXT)
                memory_copy(gpuasm_output + gpuasm_output_length, text, length);
        gpuasm_output_length += length;
}

#define GPUASM_OUT(text) gpuasm_out((string_address)(text), sizeof(text) - 1)

static void gpuasm_out_hex(p32 word)
{
        p8 digits[10] = {'0', 'x'};

        for (positive at = 0; at < 8; at++)
                digits[2 + at] = (p8)"0123456789abcdef"[word >> (28 - 4 * at) & 15];
        gpuasm_out(digits, 10);
}

/*
        The branches resolved, and each kernel written out as an array of
        its resource words and its code: ten words to a line, the form the
        file this replaces had.
*/
static bool gpuasm_finish(void)
{
        if (gpuasm.block)
                return gpuasm_fail("an .amdhsa_kernel that is never closed", null, 0);
        for (positive at = 0; at < gpuasm_fixup_count; at++)
        {
                gpuasm_label *label = gpuasm_labels + gpuasm_fixups[at].label;
                b64 offset;

                gpuasm.line = gpuasm_fixups[at].line;
                if (!label->defined)
                        return gpuasm_fail("a branch to a label that is never defined:", (string_address)gpuasm_pool + label->name,
                                           label->length);
                offset = (b64)label->at - (b64)(gpuasm_fixups[at].at + 1);
                if (offset < -32768 || offset > 32767)
                        return gpuasm_fail("a branch farther than 32767 words to", (string_address)gpuasm_pool + label->name,
                                           label->length);
                gpuasm_words[gpuasm_fixups[at].at] |= (p32)offset & 0xffff;
        }
        if (!gpuasm_kernel_count)
                return gpuasm_fail("no .amdhsa_kernel in the file", null, 0);

        GPUASM_OUT("// Generated by build gpu-kernels from ");
        gpuasm_out(gpuasm.file, string_length(gpuasm.file));
        GPUASM_OUT(". Do not edit; edit the .asm and build again.\n");
        for (positive at = 0; at < gpuasm_kernel_count; at++)
        {
                gpuasm_kernel *kernel = gpuasm_kernels + at;
                gpuasm_span name = {gpuasm_pool + kernel->name, kernel->length};
                positive label = gpuasm_find_label(name);
                positive first;
                positive last;
                positive total;

                gpuasm.line = kernel->line;
                if (label >= GPUASM_LABELS || !gpuasm_labels[label].defined)
                        return gpuasm_fail_span("a kernel with no label of its name:", name);
                if (!gpuasm_labels[label].sized)
                        return gpuasm_fail_span("a kernel with no .size:", name);
                first = gpuasm_labels[label].at;
                last = gpuasm_labels[label].stop;
                if (last <= first)
                        return gpuasm_fail_span("a kernel with no code:", name);
                total = last - first + 2;
                GPUASM_OUT("static const unsigned int gpu_k_");
                gpuasm_out(name.at, name.length);
                GPUASM_OUT("[] = {\n");
                for (positive word = 0; word < total; word += 10)
                {
                        GPUASM_OUT("    ");
                        for (positive in = word; in < word + 10 && in < total; in++)
                        {
                                p32 value = in == 0   ? GPUASM_RSRC1 | ((kernel->vgprs + 3) / 4 - 1)
                                            : in == 1 ? GPUASM_RSRC2
                                                      : gpuasm_words[first + in - 2];

                                gpuasm_out_hex(value);
                                if (in + 1 < word + 10 && in + 1 < total)
                                        GPUASM_OUT(", ");
                        }
                        if (word + 10 < total)
                                GPUASM_OUT(",\n");
                        else
                                GPUASM_OUT("};\n");
                }
        }
        if (gpuasm_output_length >= GPUASM_TEXT)
                return gpuasm_fail("the arrays are more than the room for them", null, 0);
        gpuasm_output[gpuasm_output_length] = end;
        return true;
}

/*
        Assemble a file's text (terminated) under the name its messages
        give. True with the arrays in gpuasm_output, or false with the line
        that says why in gpuasm_message.
*/
static bool gpuasm_assemble(string_address file, string_address source)
{
        memory_zero(address_of gpuasm, sizeof(gpuasm));
        gpuasm_position = 0;
        gpuasm_label_count = 0;
        memory_zero(gpuasm_labels, sizeof(gpuasm_labels));
        gpuasm_fixup_count = 0;
        gpuasm_macro_count = 0;
        gpuasm_kernel_count = 0;
        gpuasm_arena_used = 0;
        gpuasm_pool_used = 0;
        gpuasm_output_length = 0;
        gpuasm_message[0] = end;
        gpuasm.file = file;
        gpuasm.text = true;

        return gpuasm_run(source, source + string_length(source), 0) && gpuasm_finish();
}
