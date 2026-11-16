/*
        test/checks/tensor.c: the tensor routines.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_tensor
#include "../../src/lib.util.c"

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted
#endif
#ifdef CHECK_native_tensor
#include <math.h>
#include <stdio.h>
#include <sys/mman.h>
#include "tensor.h"
#endif

#if defined(CHECK_tensor) || defined(CHECK_native_tensor)
/*
        lib.c's tensor block against a double-precision model.

        Two programs are built from this: CHECK_tensor, freestanding on the
        library, which test/run's tensor lane runs on all three machines --
        x86_64 once per tier with the feature bytes walked down, riscv64 under
        qemu with no V and at VLEN 128, 256 and 512 -- and CHECK_native_tensor,
        which links the arm64 bodies lifted out of lib.c and runs them on an
        arm64 Mac (its main is with the other native sections). Everything
        between here and the first main is what both share, and is written
        against nothing but the compiler's builtins and exp(): the library's
        exponential in one, the C library's in the other, both within an ulp
        in double.

        A routine's check is a property walked over generated cases, not a
        list of values: every count from nothing to a hundred and the
        training sizes, each array ending against a page that faults and
        guarded in front by sentinels; the in-place calls the routine allows;
        a dense grid through every regime of the activation; and a stride
        through all 2^32 bit patterns of the gate. Inputs are mostly the
        values training sees, with wide magnitudes and the edges (zeros,
        denormals, +-87 to +-105, FLT_MAX, infinities, NaN) dropped in at
        random places. The judge is the numeric contract: a NaN in gives NaN
        out, an infinity in asks only that nothing traps, and otherwise
        |got - ref| <= 8 eps |ref| + 1e-35 + (1 + |g|) |up| 2^-149 with
        eps = 2^-24, or an infinity of the right sign where the product
        itself overflows.
*/
#define TENSOR_GUARD_FLOATS 4128       /* what one guarded region holds: 4096 and the sentinels */
#define TENSOR_SENTINEL 16

typedef void tensor_swiglu_call(float *, const float *, const float *, unsigned long long);

static unsigned long long tensor_state = 0x2545f4914f6cdd1dull;

static unsigned long long tensor_next(void)
{
        XORSHIFT64(tensor_state);
        return tensor_state;
}

static float tensor_float(unsigned bits)
{
        float value;

        __builtin_memcpy(&value, &bits, 4);
        return value;
}

static unsigned tensor_bits(float value)
{
        unsigned bits;

        __builtin_memcpy(&bits, &value, 4);
        return bits;
}

//      The edges, by bit pattern: zeros, denormals, where e^-|g| leaves the
//      normal range and then the denormal one, the clamp at 105, the largest
//      finite values, infinities and NaNs with and without a payload.
static const unsigned tensor_edges[] = {
    0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007fffff, 0x807fffff, 0x3f800000, 0xbf800000,
    0x41a00000, 0xc1a00000, 0x42ae0000, 0xc2ae0000, 0x42b00000, 0xc2b00000, 0x42b20000, 0xc2b20000,
    0x42c80000, 0xc2c80000, 0x42d00000, 0xc2d00000, 0x42d20000, 0xc2d20000, 0x42d40000, 0xc2d40000,
    0x461c4000, 0xc61c4000, 0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00001};

static float tensor_uniform(float low, float high)
{
        return low + (high - low) * (float)((tensor_next() >> 40) * (1.0 / 16777216.0));
}

//      A gate: mostly what a trained MLP holds, sometimes any magnitude, and
//      one in sixteen an edge.
static float tensor_draw_gate(void)
{
        unsigned long long draw = tensor_next();

        switch (draw % 16)
        {
        case 0:
                return tensor_float(tensor_edges[(draw >> 8) % (sizeof(tensor_edges) / sizeof(tensor_edges[0]))]);
        case 1:
        case 2:
                return tensor_float((unsigned)(draw >> 32 & 0x807fffff) | (unsigned)(96 + (draw >> 4) % 48) << 23);
        default:
                return tensor_uniform(-12.0f, 12.0f);
        }
}

static float tensor_draw_up(void)
{
        unsigned long long draw = tensor_next();

        switch (draw % 32)
        {
        case 0:
                return tensor_float(tensor_edges[(draw >> 8) % (sizeof(tensor_edges) / sizeof(tensor_edges[0]))]);
        case 1:
                return tensor_float((unsigned)(draw >> 32 & 0x807fffff) | (unsigned)(100 + (draw >> 4) % 40) << 23);
        default:
                return tensor_uniform(-4.0f, 4.0f);
        }
}

static double tensor_sigmoid(double g)
{
        if (g >= 0)
                return 1.0 / (1.0 + exp(-g));
        double t = exp(g);

        return t / (1.0 + t);
}

//      The worst error seen, in eps of the reference, where the reference is
//      a normal float well above the floors: what the lane log reports.
static double tensor_worst;

//      The contract for one output of swiglu_forward_f32.
static int tensor_swiglu_holds(float got, float g, float u)
{
        if (__builtin_isnan(g) || __builtin_isnan(u))
                return __builtin_isnan(got);
        if (__builtin_isinf(g) || __builtin_isinf(u))
                return 1;
        double ref = (double)g * tensor_sigmoid(g) * (double)u;
        double top = 3.4028234663852886e38;

        if (__builtin_fabs(ref) > top * (1.0 + 0x1p-24))
                return __builtin_isinf(got) && (got > 0) == (ref > 0);
        if (__builtin_isnan(got) || __builtin_isinf(got))
                return __builtin_isinf(got) && __builtin_fabs(ref) > top * (1.0 - 0x1p-23);
        double error = __builtin_fabs((double)got - ref);

        if (__builtin_fabs(ref) > 1e-30 && error > tensor_worst * 0x1p-24 * __builtin_fabs(ref))
                tensor_worst = error / (0x1p-24 * __builtin_fabs(ref));
        return error <= 8.0 * 0x1p-24 * __builtin_fabs(ref) + 1e-35 +
                        (1.0 + __builtin_fabs((double)g)) * __builtin_fabs((double)u) * 0x1p-149;
}

//      The counts every routine is walked over: every one to a hundred, the
//      vector edges past it, and the trainer's own (682 = 42 * 16 + 10).
static const unsigned tensor_counts_long[] = {127, 128, 129, 191, 192, 193, 255, 256, 257, 682, 1024, 4096};
#define TENSOR_COUNTS (101 + sizeof(tensor_counts_long) / sizeof(tensor_counts_long[0]))

static unsigned long long tensor_count(unsigned long long index)
{
        return index <= 100 ? index : tensor_counts_long[index - 101];
}

//      The faults a property found, by kind.
typedef struct
{
        unsigned long long values, outside, aliased, nans;
} tensor_faults;

//      Three regions, each TENSOR_GUARD_FLOATS long and ending against a page
//      that faults: an array of n is placed at end - n, and the sentinels in
//      front of it are checked after every call.
static void tensor_sentinels(float *start, unsigned salt)
{
        for (int i = 0; i < TENSOR_SENTINEL; i++)
                start[i - TENSOR_SENTINEL] = tensor_float(0x7fa00000u + salt * 64u + (unsigned)i);
}

static int tensor_sentinels_kept(const float *start, unsigned salt)
{
        for (int i = 0; i < TENSOR_SENTINEL; i++)
                if (tensor_bits(start[i - TENSOR_SENTINEL]) != 0x7fa00000u + salt * 64u + (unsigned)i)
                        return 0;
        return 1;
}

static void tensor_swiglu_forward_cases(tensor_swiglu_call *routine, float *const region[3], tensor_faults *faults)
{
        static float gate_copy[4096], up_copy[4096];

        for (unsigned long long index = 0; index < TENSOR_COUNTS; index++)
        {
                unsigned long long n = tensor_count(index);
                float *out = region[0] + TENSOR_GUARD_FLOATS - n;
                float *gate = region[1] + TENSOR_GUARD_FLOATS - n;
                float *up = region[2] + TENSOR_GUARD_FLOATS - n;

                for (unsigned long long i = 0; i < n; i++)
                {
                        gate_copy[i] = gate[i] = tensor_draw_gate();
                        up_copy[i] = up[i] = tensor_draw_up();
                        out[i] = tensor_float(0x7fb00000u);
                }
                tensor_sentinels(out, 1), tensor_sentinels(gate, 2), tensor_sentinels(up, 3);
                routine(out, gate, up, n);
                for (unsigned long long i = 0; i < n; i++)
                {
                        faults->values += !tensor_swiglu_holds(out[i], gate_copy[i], up_copy[i]);
                        faults->outside += tensor_bits(gate[i]) != tensor_bits(gate_copy[i]) ||
                                           tensor_bits(up[i]) != tensor_bits(up_copy[i]);
                }
                faults->outside += !tensor_sentinels_kept(out, 1) + !tensor_sentinels_kept(gate, 2) +
                                   !tensor_sentinels_kept(up, 3);

                //      In place: out over gate, then out over up.
                routine(gate, gate, up, n);
                for (unsigned long long i = 0; i < n; i++)
                        faults->aliased += !tensor_swiglu_holds(gate[i], gate_copy[i], up_copy[i]);
                for (unsigned long long i = 0; i < n; i++)
                        gate[i] = gate_copy[i];
                routine(up, gate, up, n);
                for (unsigned long long i = 0; i < n; i++)
                        faults->aliased += !tensor_swiglu_holds(up[i], gate_copy[i], up_copy[i]);
                faults->outside += !tensor_sentinels_kept(gate, 2) + !tensor_sentinels_kept(up, 3);

                //      A NaN anywhere in either input is a NaN in that output and
                //      nowhere else.
                if (n)
                {
                        unsigned long long at = tensor_next() % n;

                        for (unsigned long long i = 0; i < n; i++)
                                gate[i] = gate_copy[i], up[i] = up_copy[i];
                        if (tensor_next() & 1)
                                gate[at] = tensor_float(0x7fc00000u | (unsigned)(tensor_next() & 0x3fffff));
                        else
                                up[at] = tensor_float(0xffc00000u | (unsigned)(tensor_next() & 0x3fffff));
                        routine(out, gate, up, n);
                        faults->nans += !__builtin_isnan(out[at]);
                        for (unsigned long long i = 0; i < n; i++)
                                if (i != at)
                                        faults->nans += !tensor_swiglu_holds(out[i], gate[i], up[i]);
                }
        }
}

//      Every regime of the activation, densely: gates through [-120, 120]
//      against up = 1 and against random up, 4096 at a call.
static unsigned long long tensor_swiglu_forward_grid(tensor_swiglu_call *routine, float *const region[3])
{
        unsigned long long bad = 0;
        float *out = region[0] + TENSOR_GUARD_FLOATS - 4096;
        float *gate = region[1] + TENSOR_GUARD_FLOATS - 4096;
        float *up = region[2] + TENSOR_GUARD_FLOATS - 4096;

        for (unsigned long long block = 0; block < 32; block++)
        {
                for (unsigned long long i = 0; i < 4096; i++)
                {
                        gate[i] = -120.0f + 240.0f * (float)(block * 4096 + i) / (32.0f * 4096.0f);
                        up[i] = block & 1 ? tensor_uniform(-3.0f, 3.0f) : 1.0f;
                }
                routine(out, gate, up, 4096);
                for (unsigned long long i = 0; i < 4096; i++)
                        bad += !tensor_swiglu_holds(out[i], gate[i], up[i]);
        }
        return bad;
}

//      A stride through all 2^32 gates (a prime, so every exponent and
//      mantissa pattern turns up), each against one of a handful of ups.
static unsigned long long tensor_swiglu_forward_sweep(tensor_swiglu_call *routine, float *const region[3], unsigned stride)
{
        static const float ups[] = {1.0f, 0.7f, -1.3f, 3e-3f, 5e3f, -0.25f, 9e-7f, 1.75f};
        unsigned long long bad = 0, bits = 0;
        float *out = region[0] + TENSOR_GUARD_FLOATS - 4096;
        float *gate = region[1] + TENSOR_GUARD_FLOATS - 4096;
        float *up = region[2] + TENSOR_GUARD_FLOATS - 4096;

        while (bits < 0x100000000ull)
        {
                unsigned long long n = 0;

                for (; n < 4096 && bits < 0x100000000ull; n++, bits += stride)
                        gate[n] = tensor_float((unsigned)bits), up[n] = ups[(bits >> 7) % 8];
                routine(out + 4096 - n, gate, up, n);
                for (unsigned long long i = 0; i < n; i++)
                        bad += !tensor_swiglu_holds(out[4096 - n + i], gate[i], up[i]);
        }
        return bad;
}

/*
        rmsnorm. The model sums the squares in double, which no f32 row can
        overflow, and takes r = 1 / sqrt(sum / n + eps) with the library's
        square root or the C library's. The contract is a sum's: n positive
        terms added in f32 with none passing through more than n/4 + 8
        roundings, which every body here meets, halved by the square root;
        so r and each y are within (8 + n/8) eps of the model, y with a floor
        of two denormal steps scaled by r for an x w that lands below the
        normal range. The backward's dx is judged against its own scale,
        |r dy w| + |x| r^3 sum |dy w x| / n, which is what a dot product's
        error is measured in, and dw += dy x r rounds three times.
*/
typedef float tensor_rmsnorm_call(float *, const float *, const float *, unsigned long long, float);
typedef void tensor_rmsnorm_back_call(float *, float *, const float *, const float *, const float *,
                                      unsigned long long, float);

#ifdef CHECK_tensor
#define tensor_root square_root
#else
#define tensor_root sqrt
#endif

static double tensor_rmsnorm_k(unsigned long long n)
{
        return 8.0 + (double)n / 8.0;
}

//      The worst errors seen, as tensor_worst is for swiglu.
static double tensor_rms_worst, tensor_rmsb_worst;

static double tensor_rmsnorm_model(const float *x, unsigned long long n, float eps)
{
        double sum = 0;

        for (unsigned long long i = 0; i < n; i++)
                sum += (double)x[i] * (double)x[i];
        return 1.0 / tensor_root(sum / (double)(n ? n : 1) + (double)eps);
}

static int tensor_close(double got, double ref, double scale, double k, double floor, double *worst)
{
        double error = __builtin_fabs(got - ref);

        if (scale > 1e-30 && error > *worst * 0x1p-24 * scale)
                *worst = error / (0x1p-24 * scale);
        return error <= k * 0x1p-24 * scale + floor;
}

//      One forward row against the model: r, then every y.
static int tensor_rmsnorm_row_holds(float r, const float *y, const float *x, const float *w, unsigned long long n,
                                    float eps)
{
        double ref = tensor_rmsnorm_model(x, n, eps), k = tensor_rmsnorm_k(n);
        int good = tensor_close(r, ref, ref, k, 0, &tensor_rms_worst);

        for (unsigned long long i = 0; i < n; i++)
        {
                double yref = (double)x[i] * (double)w[i] * ref;

                good &= tensor_close(y[i], yref, __builtin_fabs(yref), k, 2.0 * 0x1p-149 * (1.0 + ref), &tensor_rms_worst);
        }
        return good;
}

//      A row of activations at a scale that varies by count, and a weight
//      row near one with a few zeros and negatives.
static float tensor_rms_scale(unsigned long long index)
{
        static const float scales[] = {1.0f, 1e-3f, 30.0f, 1e-15f, 1e15f, 0.2f};

        return scales[index % (sizeof(scales) / sizeof(scales[0]))];
}

static float tensor_rms_eps(unsigned long long index)
{
        static const float epsilons[] = {1e-5f, 1e-6f, 0.25f, 1e-12f};

        return epsilons[(index / 6) % (sizeof(epsilons) / sizeof(epsilons[0]))];
}

static void tensor_rms_rows(float *x, float *w, unsigned long long n, float scale)
{
        for (unsigned long long i = 0; i < n; i++)
        {
                unsigned long long draw = tensor_next();

                x[i] = draw % 29 == 0 ? 0.0f : tensor_uniform(-2.0f, 2.0f) * scale;
                w[i] = draw % 31 == 1 ? 0.0f : tensor_uniform(-0.5f, 2.0f);
        }
}

static void tensor_rmsnorm_forward_cases(tensor_rmsnorm_call *routine, float *const region[3], tensor_faults *faults)
{
        static float x_copy[4096], w_copy[4096];

        for (unsigned long long index = 0; index < TENSOR_COUNTS; index++)
        {
                unsigned long long n = tensor_count(index);
                float *y = region[0] + TENSOR_GUARD_FLOATS - n;
                float *x = region[1] + TENSOR_GUARD_FLOATS - n;
                float *w = region[2] + TENSOR_GUARD_FLOATS - n;
                float eps = tensor_rms_eps(index);

                tensor_rms_rows(x, w, n, tensor_rms_scale(index));
                for (unsigned long long i = 0; i < n; i++)
                        x_copy[i] = x[i], w_copy[i] = w[i], y[i] = tensor_float(0x7fb00000u);
                tensor_sentinels(y, 1), tensor_sentinels(x, 2), tensor_sentinels(w, 3);
                float r = routine(y, x, w, n, eps);

                faults->values += !tensor_rmsnorm_row_holds(r, y, x_copy, w_copy, n, eps);
                for (unsigned long long i = 0; i < n; i++)
                        faults->outside += tensor_bits(x[i]) != tensor_bits(x_copy[i]) || tensor_bits(w[i]) != tensor_bits(w_copy[i]);
                faults->outside += !tensor_sentinels_kept(y, 1) + !tensor_sentinels_kept(x, 2) + !tensor_sentinels_kept(w, 3);

                //      In place, y over x.
                r = routine(x, x, w, n, eps);
                faults->aliased += !tensor_rmsnorm_row_holds(r, x, x_copy, w_copy, n, eps);
                faults->outside += !tensor_sentinels_kept(x, 2) + !tensor_sentinels_kept(w, 3);
                if (!n)
                        continue;

                //      A NaN in x is a NaN r and every y NaN; an infinity in x
                //      makes the sum infinite and r 0, so y is 0 but at the
                //      infinity, where it is NaN; a NaN in w is NaN in that y
                //      alone.
                unsigned long long at = tensor_next() % n;

                for (unsigned long long i = 0; i < n; i++)
                        x[i] = x_copy[i];
                x[at] = tensor_float(0x7fc00000u | (unsigned)(tensor_next() & 0x3fffff));
                r = routine(y, x, w, n, eps);
                faults->nans += !__builtin_isnan(r);
                for (unsigned long long i = 0; i < n; i++)
                        faults->nans += !__builtin_isnan(y[i]);
                x[at] = tensor_float(tensor_next() & 1 ? 0x7f800000u : 0xff800000u);
                r = routine(y, x, w, n, eps);
                faults->nans += r != 0.0f;
                for (unsigned long long i = 0; i < n; i++)
                        faults->nans += i == at ? !__builtin_isnan(y[i]) : y[i] != 0.0f;
                x[at] = x_copy[at];
                w[at] = tensor_float(0xffc00000u);
                r = routine(y, x, w, n, eps);
                faults->nans += !tensor_close(r, tensor_rmsnorm_model(x, n, eps), tensor_rmsnorm_model(x, n, eps),
                                              tensor_rmsnorm_k(n), 0, &tensor_rms_worst);
                faults->nans += !__builtin_isnan(y[at]);
                w[at] = w_copy[at];
        }
}

//      An empty row is the mean of nothing taken as zero: r = 1/sqrt(eps)
//      and nothing touched. A row of zeros with eps 0 is 1/sqrt(0), inf.
static int tensor_rmsnorm_edges(tensor_rmsnorm_call *routine, float *const region[3])
{
        float *y = region[0] + TENSOR_GUARD_FLOATS - 64, *x = region[1] + TENSOR_GUARD_FLOATS - 64;
        float *w = region[2] + TENSOR_GUARD_FLOATS - 64;
        int good = 1;

        tensor_sentinels(region[0] + TENSOR_GUARD_FLOATS, 4);
        good &= tensor_close(routine(region[0] + TENSOR_GUARD_FLOATS, region[1] + TENSOR_GUARD_FLOATS,
                                     region[2] + TENSOR_GUARD_FLOATS, 0, 0.25f),
                             2.0, 2.0, tensor_rmsnorm_k(0), 0, &tensor_rms_worst);
        good &= tensor_sentinels_kept(region[0] + TENSOR_GUARD_FLOATS, 4);
        for (int i = 0; i < 64; i++)
                x[i] = 0.0f, w[i] = 1.0f;
        float r = routine(y, x, w, 64, 0.0f);

        good &= __builtin_isinf(r) && r > 0;
        for (int i = 0; i < 64; i++)
                good &= __builtin_isnan(y[i]);
        //      A square past FLT_MAX: the f32 sum is infinite, r 0, y 0.
        x[5] = 1e30f;
        r = routine(y, x, w, 64, 1e-5f);
        good &= r == 0.0f;
        for (int i = 0; i < 64; i++)
                good &= y[i] == 0.0f;
        //      A sum below the normal range, eps 0: r is 1/sqrt of that
        //      denormal, near 1e20, which an estimate that reads denormals
        //      as zero would make inf. The model is the f32 square itself.
        static const float tiny[] = {1e-20f, -3e-21f, 7e-23f, 1e-19f};
        static float sink;

        for (int t = 0; t < 4; t++)
        {
                x[0] = tiny[t];
                sink = x[0] * x[0];
                double ref = 1.0 / tensor_root((double)sink);

                r = routine(y, x, w, 1, 0.0f);
                good &= tensor_close(r, ref, ref, tensor_rmsnorm_k(1), 0, &tensor_rms_worst);
                good &= tensor_close(y[0], (double)x[0] * r, __builtin_fabs((double)x[0] * r), 2.0, 0, &tensor_rms_worst);
        }
        return good;
}

//      One backward row against the model.
static int tensor_rmsnorm_back_holds(const float *dx, const float *dw, const float *dw_before, const float *dy,
                                     const float *x, const float *w, unsigned long long n, float r)
{
        double dot = 0, magnitude = 0, rr = r, k = tensor_rmsnorm_k(n);
        int good = 1;

        for (unsigned long long i = 0; i < n; i++)
        {
                double term = (double)dy[i] * (double)w[i] * (double)x[i];

                dot += term, magnitude += __builtin_fabs(term);
        }
        for (unsigned long long i = 0; i < n; i++)
        {
                double g = rr * (double)dy[i] * (double)w[i];
                double ref = g - (double)x[i] * rr * rr * rr * dot / (double)n;
                double scale = __builtin_fabs(g) + __builtin_fabs((double)x[i]) * rr * rr * rr * magnitude / (double)n;
                double wref = (double)dw_before[i] + (double)dy[i] * (double)x[i] * rr;
                double wscale = __builtin_fabs((double)dw_before[i]) + __builtin_fabs((double)dy[i] * (double)x[i] * rr);

                good &= tensor_close(dx[i], ref, scale, k, 4.0 * 0x1p-149 * (1.0 + rr), &tensor_rmsb_worst);
                good &= tensor_close(dw[i], wref, wscale, 4.0, 4.0 * 0x1p-149 * (1.0 + rr), &tensor_rmsb_worst);
        }
        return good;
}

static void tensor_rmsnorm_backward_cases(tensor_rmsnorm_back_call *routine, float *const region[3], float *const extra[2],
                                          tensor_faults *faults)
{
        static float dy_copy[4096], x_copy[4096], w_copy[4096], dw_copy[4096];

        for (unsigned long long index = 0; index < TENSOR_COUNTS; index++)
        {
                unsigned long long n = tensor_count(index);
                float *dx = region[0] + TENSOR_GUARD_FLOATS - n, *x = region[1] + TENSOR_GUARD_FLOATS - n;
                float *w = region[2] + TENSOR_GUARD_FLOATS - n, *dy = extra[0] + TENSOR_GUARD_FLOATS - n;
                float *dw = extra[1] + TENSOR_GUARD_FLOATS - n;

                tensor_rms_rows(x, w, n, tensor_rms_scale(index));
                float r = (float)tensor_rmsnorm_model(x, n, tensor_rms_eps(index));

                for (unsigned long long i = 0; i < n; i++)
                {
                        dy[i] = tensor_uniform(-1.0f, 1.0f) * (index % 3 == 0 ? 1e-4f : 1.0f);
                        dw[i] = tensor_next() % 7 == 0 ? 0.0f : tensor_uniform(-3.0f, 3.0f);
                        dy_copy[i] = dy[i], x_copy[i] = x[i], w_copy[i] = w[i], dw_copy[i] = dw[i];
                        dx[i] = tensor_float(0x7fb00000u);
                }
                tensor_sentinels(dx, 1), tensor_sentinels(x, 2), tensor_sentinels(w, 3), tensor_sentinels(dy, 4),
                    tensor_sentinels(dw, 5);
                routine(dx, dw, dy, x, w, n, r);
                faults->values += !tensor_rmsnorm_back_holds(dx, dw, dw_copy, dy_copy, x_copy, w_copy, n, r);
                for (unsigned long long i = 0; i < n; i++)
                        faults->outside += tensor_bits(x[i]) != tensor_bits(x_copy[i]) || tensor_bits(w[i]) != tensor_bits(w_copy[i]) ||
                                           tensor_bits(dy[i]) != tensor_bits(dy_copy[i]);
                faults->outside += !tensor_sentinels_kept(dx, 1) + !tensor_sentinels_kept(x, 2) + !tensor_sentinels_kept(w, 3) +
                                   !tensor_sentinels_kept(dy, 4) + !tensor_sentinels_kept(dw, 5);

                //      In place, dx over dy.
                for (unsigned long long i = 0; i < n; i++)
                        dw[i] = dw_copy[i];
                routine(dy, dw, dy, x, w, n, r);
                faults->aliased += !tensor_rmsnorm_back_holds(dy, dw, dw_copy, dy_copy, x_copy, w_copy, n, r);
                faults->outside += !tensor_sentinels_kept(dy, 4) + !tensor_sentinels_kept(dw, 5);
                if (!n)
                        continue;

                //      A NaN in dy makes the dot NaN and so every dx, and dw NaN
                //      there alone.
                unsigned long long at = tensor_next() % n;

                for (unsigned long long i = 0; i < n; i++)
                        dy[i] = dy_copy[i], dw[i] = dw_copy[i];
                dy[at] = tensor_float(0x7fc00000u);
                routine(dx, dw, dy, x, w, n, r);
                for (unsigned long long i = 0; i < n; i++)
                        faults->nans += !__builtin_isnan(dx[i]) + (i == at ? !__builtin_isnan(dw[i]) : __builtin_isnan(dw[i]));
        }
}
#endif /* CHECK_tensor || CHECK_native_tensor */

#ifdef CHECK_tensor
static float *tensor_region[5];

//      Five regions of TENSOR_GUARD_FLOATS, each followed by a page that
//      faults: the first three for a routine's arrays, the last two for the
//      backward's other two.
static p8 tensor_guard(void)
{
        positive span = (TENSOR_GUARD_FLOATS * 4 + 4095) / 4096 * 4096;

        for (int at = 0; at < 5; at++)
        {
                p8 address_to pages = memory(span + 4096);

                if (!pages || system_call_3(syscall(mprotect), (positive)(pages + span), 4096, 0) != 0)
                        return false;
                tensor_region[at] = (float *)(pages + span) - TENSOR_GUARD_FLOATS;
        }
        return true;
}

// Every call the first in a process, on riscv64's first tier.
static p8 tensor_first;

static fn tensor_call_swiglu_forward(float *out, const float *gate, const float *up, unsigned long long n)
{
        VECTOR_FIRST_CALL(tensor_first);
        swiglu_forward_f32(out, gate, up, n);
}

static float tensor_call_rmsnorm_forward(float *y, const float *x, const float *w, unsigned long long n, float eps)
{
        VECTOR_FIRST_CALL(tensor_first);
        return rmsnorm_forward_f32(y, x, w, n, eps);
}

static fn tensor_call_rmsnorm_backward(float *dx, float *dw, const float *dy, const float *x, const float *w,
                                       unsigned long long n, float r)
{
        VECTOR_FIRST_CALL(tensor_first);
        rmsnorm_backward_f32(dx, dw, dy, x, w, n, r);
}

static fn tensor_worst_line(string_address tier, string_address routine, double worst)
{
        string_format(log, "  %s: %s worst ", tier, routine);
        decimal_to_string(log, worst);
        string_format(log, " eps\n");
}

//      One tier's properties, each one check.
static fn tensor_tier(string_address tier)
{
        tensor_faults faults = {0, 0, 0, 0};

        tensor_worst = 0;
        tensor_swiglu_forward_cases(tensor_call_swiglu_forward, tensor_region, &faults);
        check("swiglu_forward_f32: every count to 100 and 127 to 4096, within the bound", faults.values == 0);
        check("swiglu_forward_f32: inputs, sentinels and pages beside every array untouched", faults.outside == 0);
        check("swiglu_forward_f32: out over gate and out over up", faults.aliased == 0);
        check("swiglu_forward_f32: a NaN in gate or up is NaN out, there alone", faults.nans == 0);
        check("swiglu_forward_f32: gates through [-120, 120], up 1 and random",
              tensor_swiglu_forward_grid(tensor_call_swiglu_forward, tensor_region) == 0);
#if RISCV64
        //      Emulated: a coarser stride through the bit patterns.
        check("swiglu_forward_f32: a stride through all 2^32 gates",
              tensor_swiglu_forward_sweep(tensor_call_swiglu_forward, tensor_region, 262139) == 0);
#else
        check("swiglu_forward_f32: a stride through all 2^32 gates",
              tensor_swiglu_forward_sweep(tensor_call_swiglu_forward, tensor_region, 16411) == 0);
#endif
        tensor_worst_line(tier, "swiglu_forward_f32", tensor_worst);

        tensor_faults norm = {0, 0, 0, 0}, back = {0, 0, 0, 0};

        tensor_rms_worst = 0, tensor_rmsb_worst = 0;
        tensor_rmsnorm_forward_cases(tensor_call_rmsnorm_forward, tensor_region, &norm);
        check("rmsnorm_forward_f32: every count to 100 and 127 to 4096, r and y within the bound", norm.values == 0);
        check("rmsnorm_forward_f32: x, w, sentinels and pages beside every array untouched", norm.outside == 0);
        check("rmsnorm_forward_f32: y over x", norm.aliased == 0);
        check("rmsnorm_forward_f32: NaN in x is NaN everywhere, an infinity in x is r = 0, NaN in w is NaN there",
              norm.nans == 0);
        check("rmsnorm_forward_f32: no count is 1/sqrt(eps), zeros with eps 0 are inf, an overflowing square is r = 0",
              tensor_rmsnorm_edges(tensor_call_rmsnorm_forward, tensor_region));
        tensor_rmsnorm_backward_cases(tensor_call_rmsnorm_backward, tensor_region, tensor_region + 3, &back);
        check("rmsnorm_backward_f32: every count to 100 and 127 to 4096, dx and dw within the bound", back.values == 0);
        check("rmsnorm_backward_f32: dy, x, w, sentinels and pages beside every array untouched", back.outside == 0);
        check("rmsnorm_backward_f32: dx over dy", back.aliased == 0);
        check("rmsnorm_backward_f32: NaN in dy is NaN in every dx and in that dw alone", back.nans == 0);
        tensor_worst_line(tier, "rmsnorm_forward_f32", tensor_rms_worst);
        tensor_worst_line(tier, "rmsnorm_backward_f32", tensor_rmsb_worst);
}

b32 main()
{
        check("tensor: guard pages", tensor_guard());
        if (!tensor_region[2])
                return test_report(null);
#if X64
        p8 wide512 = cpu_has_avx512, wide2 = cpu_has_avx2, fused = cpu_has_fma;

        for (p32 tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier < 2 ? wide2 : 0;
                cpu_has_fma = tier < 2 ? fused : 0;
                tensor_tier(cpu_has_avx512 ? "avx512" : cpu_has_avx2 && cpu_has_fma ? "avx2 fma" : "sse2");
        }
        cpu_has_avx512 = wide512, cpu_has_avx2 = wide2, cpu_has_fma = fused;
#elif RISCV64
        //      The first call asks for V and keeps its arguments through the
        //      question.
        {
                float *out = tensor_region[0] + TENSOR_GUARD_FLOATS - 3, *gate = tensor_region[1] + TENSOR_GUARD_FLOATS - 3;
                float *up = tensor_region[2] + TENSOR_GUARD_FLOATS - 3;

                for (int i = 0; i < 3; i++)
                        gate[i] = (float)i - 1.5f, up[i] = 2.0f;
                cpu_has_vector = 0;
                swiglu_forward_f32(out, gate, up, 3);
                check("swiglu_forward_f32: the first call asks for V and keeps its arguments",
                      cpu_has_vector != 0 && tensor_swiglu_holds(out[0], gate[0], up[0]) &&
                          tensor_swiglu_holds(out[1], gate[1], up[1]) && tensor_swiglu_holds(out[2], gate[2], up[2]));
                cpu_has_vector = 0;
                float r = rmsnorm_forward_f32(out, gate, up, 3, 0.25f);

                check("rmsnorm_forward_f32: the first call asks for V and keeps its arguments, eps among them",
                      cpu_has_vector != 0 && tensor_rmsnorm_row_holds(r, out, gate, up, 3, 0.25f));

                float *dw = tensor_region[3] + TENSOR_GUARD_FLOATS - 3, *dx = tensor_region[4] + TENSOR_GUARD_FLOATS - 3;
                float before[3];

                for (int i = 0; i < 3; i++)
                        dw[i] = before[i] = (float)i;
                cpu_has_vector = 0;
                rmsnorm_backward_f32(dx, dw, gate, out, up, 3, r);
                check("rmsnorm_backward_f32: the first call asks for V and keeps its arguments, the count among them",
                      cpu_has_vector != 0 && tensor_rmsnorm_back_holds(dx, dw, before, gate, out, up, 3, r));
        }
        p8 vector = cpu_has_vector;

        //      Every call of the first tier the first in a process, which
        //      asks for V and comes back to the routine's entry.
        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
                tensor_first = tier == 0;
                tensor_tier(tier ? "no V" : vector == 2 ? "V, every call the first" : "no V, every call the first");
        }
        tensor_first = false;
        cpu_has_vector = vector;
#else
        tensor_tier("neon");
#endif
        return test_report(null);
}
#endif /* CHECK_tensor */

