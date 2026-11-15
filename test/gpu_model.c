/*
        A model of the card the GPU's kernels run on: an interpreter of the instruction words src/build/gpuasm.c makes (the programs of
        src/canvas/gpu.asm as artifacts/gpu_kernels.inc has them), so that what the kernels draw is known on a machine with no card, bit for
        bit, and every change to the programs can be checked there. CHECK_gpu_model in test/checks.c holds it to the card: gpu.c's self-test
        (gpu_test_hash) drawn by the model has to give GPU_TEST_HASH, the hash an RDNA2 card gave for the same picture.

        What it is. The words are decoded as the card decodes them (gpu_model/decode.c): only the instructions and the forms gpuasm makes,
        and any other word is a hard failure that names it and its place. A dispatch is run as gpu_amd_submit sets it up: the two resource
        words, the user data in s0 on (the arguments' address in s[4:5]), the workgroup's x and y after them (s6 and s7), the lane in v0, a
        workgroup of 64 threads that is one wave64, the grid columns by rows; the waves are run one after another, each to its end
        (gpu_model/exec.c). exec, VCC and SCC are the card's; every other register starts as gpu_model.poison, so a picture that moves with
        it reads a register nothing wrote. Memory is the card's address space (gpu_model/memory.c): regions, each an address range, the
        processor's memory behind it, and what the card may do there; a load, a store or a fetch of a byte outside every region is refused
        with its address. The model card (gpu_model/card.c, with GPU_MODEL_CARD) is that under gpu.c's check seams: a submit is read as
        the command processor reads its packets, and its regions are the card's buffer and the buffers the job's bo list names.

        A load is done when it is issued, and a register a load writes may not be read or written before the s_waitcnt that covers it
        (vmcnt the vector loads in order; lgkmcnt the scalar loads, out of order, and the permutes): the wave is refused, so a kernel
        that draws right here is not one that is right only because the model is quick.

        Taken from gfx10's documentation, not from GPU_TEST_HASH (the hash holds what the self-test's picture reaches and no more): the
        last bits of v_rcp_f32 and v_sqrt_f32 (correctly rounded here, one ulp on the card; the rows nudge them), the float mode's denormal
        flush, the clamp of the integer adds and of v_pk_sub_u16 (saturation), a compare's bit for a lane exec has off (0), the carry of a
        lane exec has off (0), ds_bpermute_b32's lane (the address over 4, modulo 64; a lane exec has off reads 0; a read across the halves
        of the wave refused), an unaligned global load read byte for byte (the text rows read unaligned dwords, and the hash holds those),
        a scalar load's address with its low two bits dropped, s_bfe_u32's fields (offset bits 0 to 4, width 16 to 22), the shift counts
        of the 32-bit shifts as their low five bits, s_and_saveexec_b64's SCC (the new exec is not zero).

        Not modelled: timing, the ring, the caches, packets other than the ones gpu_amd_submit sends, LDS, a workgroup of more than one wave,
        wave32, scratch. Use (what P2 and the workers after call):

            gpu_model_run(kernel, words, args_va, columns, rows, regions, count)

        runs kernel (its two resource words, then its code, the way gpu_kernels.inc has a kernel; words in all) over the grid with its
        arguments' block at args_va, and answers 0 with the picture in the regions' memory, or the failure's code with its words in
        gpu_model.fault.text. gpu_model.cost has what the job ran, and gpu_model.hits each code word's count of runs.
*/

#ifndef GPU_MODEL_C
#define GPU_MODEL_C

enum gpu_model_failure
{
        GPU_MODEL_OK,
        GPU_MODEL_UNKNOWN,    // a word that is not an instruction gpuasm makes
        GPU_MODEL_BOUNDS,     // a load, a store or a fetch outside every region the job may reach that way
        GPU_MODEL_REGISTER,   // a register past the ones the wave has
        GPU_MODEL_WAIT,       // a register a load writes, read or written before the wait for it
        GPU_MODEL_DISPATCH,   // a dispatch, or a resource word, not the one gpu_amd_submit sends
        GPU_MODEL_RUNAWAY,    // a wave past its budget of instructions
        GPU_MODEL_UNMODELLED, // a behaviour of the card the model does not take (a clamp it has not seen, a permute across the halves)
};

#define GPU_MODEL_READ 1u
#define GPU_MODEL_WRITE 2u
#define GPU_MODEL_RUN 4u
struct gpu_model_region
{
        positive va, bytes;
        p8 address_to host;
        unsigned int can;
};

// What a job ran: waves, instructions by kind, the lanes the vector ones ran on, the loads and stores lane by lane, and their bytes.
struct gpu_model_cost
{
        positive waves, instructions, salu, valu, smem, vmem_loads, vmem_stores, ds, branches, lanes, lane_loads, lane_stores, bytes_loaded, bytes_stored, scalar_bytes;
};

// The doubts (gpu_model.doubt), each the other way from the documentation's.
#define GPU_MODEL_DOUBT_INACTIVE 1u // a compare's and a carry's bit of a lane exec has off is 1 (gfx10: 0)
#define GPU_MODEL_DOUBT_PERMUTE 2u  // ds_bpermute_b32 from a lane exec has off reads its register (gfx10: 0)
#define GPU_MODEL_DOUBT_ROUND 4u    // the float ops round toward zero (the float mode 0xc0: to nearest even)
#define GPU_MODEL_DOUBT_TRUNC 8u    // v_cvt_u32_f32 rounds to nearest (gfx10: toward zero)
#define GPU_MODEL_DOUBT_ALIGN 16u   // a global dword load at an address not of 4 reads the aligned dword under it (amdgpu sets unaligned access: byte for byte)
#define GPU_MODEL_DOUBT_SCC 32u     // s_and_saveexec_b64's SCC is the old exec's being nonzero (gfx10: the new one's)
#define GPU_MODEL_DOUBTS 6
#define GPU_MODEL_CODE 16384      // words of a kernel's code its decoding and its counts are kept for
#define GPU_MODEL_BUDGET 4000000u // instructions a wave may run

static struct
{
        // the job: its regions, and the code (the words from code_va on, host its memory)
        const struct gpu_model_region *region;
        unsigned int regions, last;
        const p32 *code;
        unsigned int code_words, vgprs;
        positive code_va;
        // the wave: the scalar registers by their codes (vcc at 106, exec at 126), the vector ones, SCC, where it is
        p32 s[128];
        p32 v[256][64];
        unsigned int scc, x, y;
        positive pc;
        // what the caller sets: the registers' poison at a wave's start, the nudge of v_rcp_f32 and v_sqrt_f32 (float.c), and the doubts: behaviours
        // the model takes from the documentation, each of which a row turns the other way to ask whether GPU_TEST_HASH decides it
        p32 poison;
        unsigned int nudge, doubt;
        // what was run, each code word's count (an instruction's first word), and the failure
        struct gpu_model_cost cost;
        p32 hits[GPU_MODEL_CODE];
        struct
        {
                int code;
                positive address, pc;
                unsigned int bytes, lane, x, y;
                p32 word;
                char text[320];
        } fault;
} gpu_model;

// ---- The failure's words, built in gpu_model.fault.text.

static unsigned int gpu_model_text_at;

static void gpu_model_put(const char *words)
{
        while (*words && gpu_model_text_at + 1 < sizeof(gpu_model.fault.text))
                gpu_model.fault.text[gpu_model_text_at++] = *words++;
        gpu_model.fault.text[gpu_model_text_at] = 0;
}

static void gpu_model_number(p64 value, unsigned int base)
{
        char digits[24];
        unsigned int count = 0;

        if (base == 16)
                gpu_model_put("0x");
        do
                digits[count++] = "0123456789abcdef"[value % base];
        while ((value /= base) && count < sizeof(digits));
        while (count && gpu_model_text_at + 1 < sizeof(gpu_model.fault.text))
                gpu_model.fault.text[gpu_model_text_at++] = digits[--count];
        gpu_model.fault.text[gpu_model_text_at] = 0;
}

// The failure begun: its code and what it is (the first failure of a job is the one kept); gpu_model_fail_end says where.
static int gpu_model_fail(int code, const char *what)
{
        if (gpu_model.fault.code)
                return gpu_model.fault.code;
        gpu_model.fault.code = code, gpu_model.fault.pc = gpu_model.pc, gpu_model.fault.x = gpu_model.x, gpu_model.fault.y = gpu_model.y;
        gpu_model_text_at = 0;
        gpu_model_put(what);
        return 0;
}

// Where: the byte of the code the instruction is at, and the wave.
static int gpu_model_fail_end(void)
{
        gpu_model_put(", at byte ");
        gpu_model_number(gpu_model.fault.pc - gpu_model.code_va, 16);
        gpu_model_put(" of the code (wave ");
        gpu_model_number(gpu_model.fault.x, 10);
        gpu_model_put(",");
        gpu_model_number(gpu_model.fault.y, 10);
        gpu_model_put(")");
        return gpu_model.fault.code;
}

static int gpu_model_unknown(p32 word, positive pc, const char *why)
{
        if (gpu_model_fail(GPU_MODEL_UNKNOWN, "an instruction word the model does not take: "))
                return gpu_model.fault.code;
        gpu_model.fault.word = word, gpu_model.fault.pc = pc;
        gpu_model_number(word, 16);
        gpu_model_put(" (");
        gpu_model_put(why);
        gpu_model_put(")");
        return gpu_model_fail_end();
}

#include "gpu_model/float.c"
#include "gpu_model/decode.c"
#include "gpu_model/memory.c"
#include "gpu_model/exec.c"
#include "gpu_model/valu.c"

#ifdef GPU_MODEL_CARD
#include "gpu_model/card.c"
#include "gpu_model/differ.c"
#endif

#endif
