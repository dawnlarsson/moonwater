/*
        test/checks/verify.c: the library's assembly bodies held to their C, at every size and alignment.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_verify
#include "../../src/lib.util.c"

/*
        Differential test for the assembly in lib.c.

        Every routine in there replaced a C one that was known to work. This
        keeps that C, under another name, and checks the two answer the same
        thing for inputs chosen to land on the edges: nothing, one byte, either
        side of every size the assembly switches strategy at, and overlaps in
        both directions.

        It is built freestanding for each architecture and run under qemu, so
        an answer that is only right on the machine this was written on does
        not pass.

        Adding a routine means adding its reference and a case below. A
        reference is the C that used to be in lib.c, copied verbatim: the
        point is to compare against what was already trusted, not against a
        second attempt at the same idea.
*/

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

fn report(string_address name, string_address detail, positive got, positive want)
{
        failures++;
        string_format(log, "  FAIL %s %s: got %p want %p\n", name, detail, got, want);
}

fn same(string_address name, string_address detail, positive got, positive want)
{
        checks++;

        if (got != want)
                report(name, detail, got, want);
}

/*
        Three pages with the outer two protected, so a read or write past
        either edge of the middle one is a fault and ends the run. Each step
        is a check named for the routine under test; the answer is the
        mapping, or null once a step has failed, with the mapping given back.
*/
static p8 address_to guard_pages(string_address routine)
{
        p8 address_to pages = memory(3 * 4096);
        p8 mapped = (bipolar)(positive)pages > 0;

        same(routine, "guard mapping", mapped, 1);
        if (!mapped)
                return null;
        p8 protected =
            system_call_3(syscall(mprotect), (positive)pages, 4096, 0) == 0 &&
            system_call_3(syscall(mprotect), (positive)(pages + 8192), 4096, 0) == 0;
        same(routine, "guard pages protected", protected, 1);
        if (!protected)
        {
                memory_free(pages, 3 * 4096);
                return null;
        }
        return pages;
}

/*
        The bytes round a routine's answer still hold the guard it was handed:
        the eight in front of got, and the eight behind its length bytes from
        the one at `after` on (one is the byte past a terminator).
*/
fn same_guarded(string_address name, p8 address_to got, positive length,
                p8 guard, positive after)
{
        for (positive back = 1; back <= 8; back++)
                same(name, "in front of it", (positive)*(got - back),
                     (positive)guard);

        for (positive at = after; at < after + 8; at++)
                same(name, "behind it", (positive)got[length + at],
                     (positive)guard);
}

fn same_bytes(string_address name, string_address detail, b8 address_to got,
              b8 address_to want, positive size)
{
        checks++;

        for (positive i = 0; i < size; i++)
                if (got[i] != want[i])
                {
                        failures++;
                        string_format(log, "  FAIL %s %s: byte %p is %p want %p\n",
                                      name, detail, i, (positive)got[i],
                                      (positive)want[i]);
                        return;
                }
}

static positive absent_count;

// A routine the library does not carry under either of its names.
//
// Not a failure: the inventory says which machines are short of which, and
// riscv64 is legitimately short of several. Saying so out loud is the point --
// a case that compiles to nothing says nothing, and that is how ninety five
// thousand checks once went missing while the lane still agreed with itself.
fn absent(string_address name)
{
        absent_count++;
        string_format(log, "  ABSENT %s: under neither name in this library\n", name);
}

// A repeatable spread of values; nothing here wants real randomness, only
// inputs nobody chose by hand.
static positive seed = 0x2545F4914F6CDD1Dull;

positive next()
{
        XORSHIFT64(seed);
        return seed;
}

/*
        The references: the C that was in lib.c before the assembly.
*/
address_any reference_fill(address_any destination, b8 value, positive size)
{
        b8 address_to dest = (b8 address_to)destination;

        while (size--)
                address_to dest++ = (b8)value;

        return destination;
}

address_any reference_copy(address_any destination, address_any source, positive size)
{
        b8 address_to dest = (b8 address_to)destination;
        b8 address_to src = (b8 address_to)source;

        if (dest > src && dest < src + size)
        {
                dest += size - 1;
                src += size - 1;
                while (size--)
                        address_to dest-- = address_to src--;
        }
        else
        {
                while (size--)
                        address_to dest++ = address_to src++;
        }

        return destination;
}

fn reference_exchange(address_any left, address_any right, positive size)
{
        p8 address_to a = left;
        p8 address_to b = right;

        if (a == b)
                return;

        while (size--)
        {
                p8 held = *a;

                *a++ = *b;
                *b++ = held;
        }
}

address_any reference_reverse(address_any block, positive size)
{
        p8 address_to left = block;
        p8 address_to right = left + size;

        while (left < right)
        {
                p8 value;

                right--;

                if (left >= right)
                        break;

                value = address_to left;
                address_to left++ = address_to right;
                address_to right = value;
        }

        return block;
}

address_any reference_frob(address_any block, positive size)
{
        p8 address_to bytes = block;

        while (size--)
                address_to bytes++ ^= 42;

        return block;
}

static p32 reference_sum_bytes(address_any block, positive size)
{
        p8 address_to at = block;
        p32 sum = 0;

        while (size--)
                sum += address_to at++;

        return sum;
}

static p32 reference_checksum_bsd16(address_any block, positive size, p32 sum)
{
        p8 address_to at = block;

        sum &= 0xffff;

        while (size--)
        {
                sum = (sum >> 1) + ((sum & 1) << 15);
                sum = (sum + address_to at++) & 0xffff;
        }

        return sum;
}

static p64 reference_rotl64(p64 value, p8 rot)
{
        return (value << rot) | (value >> (64 - rot));
}

static p64 reference_xxh64_round(p64 acc, p64 lane)
{
        return reference_rotl64(acc + lane * 0xC2B2AE3D27D4EB4Full, 31) *
               0x9E3779B185EBCA87ull;
}

static p64 reference_xxh64_load64(p8 address_to p)
{
        return (p64)p[0] | ((p64)p[1] << 8) | ((p64)p[2] << 16) |
               ((p64)p[3] << 24) | ((p64)p[4] << 32) | ((p64)p[5] << 40) |
               ((p64)p[6] << 48) | ((p64)p[7] << 56);
}

static p32 reference_xxh64_load32(p8 address_to p)
{
        return (p32)p[0] | ((p32)p[1] << 8) | ((p32)p[2] << 16) |
               ((p32)p[3] << 24);
}

static p64 reference_xxh64_merge(p64 acc, p64 lane)
{
        acc ^= reference_xxh64_round(0, lane);
        return acc * 0x9E3779B185EBCA87ull + 0x85EBCA77C2B2AE63ull;
}

static p64 reference_xxh64(address_any block, positive size, p64 seed)
{
        p8 address_to p = block;
        p64 acc;
        p64 remaining = size;

        if (size >= 32)
        {
                p64 a = seed + 0x9E3779B185EBCA87ull + 0xC2B2AE3D27D4EB4Full;
                p64 b = seed + 0xC2B2AE3D27D4EB4Full;
                p64 c = seed;
                p64 d = seed - 0x9E3779B185EBCA87ull;

                while (remaining >= 32)
                {
                        a = reference_xxh64_round(a, reference_xxh64_load64(p));
                        b = reference_xxh64_round(b, reference_xxh64_load64(p + 8));
                        c = reference_xxh64_round(c, reference_xxh64_load64(p + 16));
                        d = reference_xxh64_round(d, reference_xxh64_load64(p + 24));
                        p += 32;
                        remaining -= 32;
                }

                acc = reference_rotl64(a, 1) + reference_rotl64(b, 7) +
                      reference_rotl64(c, 12) + reference_rotl64(d, 18);
                acc = reference_xxh64_merge(acc, a);
                acc = reference_xxh64_merge(acc, b);
                acc = reference_xxh64_merge(acc, c);
                acc = reference_xxh64_merge(acc, d);
        }
        else
                acc = seed + 0x27D4EB2F165667C5ull;

        acc += size;

        while (remaining >= 8)
        {
                acc ^= reference_xxh64_round(0, reference_xxh64_load64(p));
                acc = reference_rotl64(acc, 27) * 0x9E3779B185EBCA87ull +
                      0x85EBCA77C2B2AE63ull;
                p += 8;
                remaining -= 8;
        }

        if (remaining >= 4)
        {
                acc ^= (p64)reference_xxh64_load32(p) * 0x9E3779B185EBCA87ull;
                acc = reference_rotl64(acc, 23) * 0xC2B2AE3D27D4EB4Full +
                      0x165667B19E3779F9ull;
                p += 4;
                remaining -= 4;
        }

        while (remaining)
        {
                acc ^= (p64)address_to p * 0x27D4EB2F165667C5ull;
                acc = reference_rotl64(acc, 11) * 0x9E3779B185EBCA87ull;
                p++;
                remaining--;
        }

        acc ^= acc >> 33;
        acc *= 0xC2B2AE3D27D4EB4Full;
        acc ^= acc >> 29;
        acc *= 0x165667B19E3779F9ull;
        acc ^= acc >> 32;
        return acc;
}

fn reference_copy_match(address_any dest, positive offset, positive length)
{
        p8 address_to at = dest;

        while (length--)
        {
                address_to at = address_to(at - offset);
                at++;
        }
}

p8 address_to reference_copy_end(p8 address_to destination, address_any source,
                                 positive size)
{
        reference_copy(destination, source, size);
        destination[size] = 0;
        return destination + size;
}

positive reference_length(string_address source)
{
        string_address step = source;

        while (string_get(step))
                step++;

        return step - source;
}

b32 reference_compare(string_address source, string_address input)
{
        while (string_get(source) && string_get(input))
        {
                if string_not (source, address_to input)
                        break;

                source++;
                input++;
        }

        return string_get(source) - string_get(input);
}

string_address reference_first_of(string_address source, p8 character)
{
        while (string_get(source))
        {
                if string_is (source, character)
                        return source;

                source++;
        }

        return character ? null : source;
}

// Every size the routines change strategy at, and the ones either side.
static positive edges[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33,
                           40, 63, 64, 65, 100, 127, 128, 129, 200, 255, 256,
                           257, 511, 512, 1000, 4096};

#define EDGE_COUNT (sizeof(edges) / sizeof(edges[0]))
#define ROOM 8192

static b8 mine[ROOM];
static b8 theirs[ROOM];
static b8 pattern[ROOM];

fn check_fill()
{
        for (positive e = 0; e < EDGE_COUNT; e++)
        {
                positive size = edges[e];
                b8 value = (b8)next();

                // Poisoned either side, so a routine that writes past its size
                // is caught rather than tolerated.
                reference_fill(mine, 0xA5, ROOM);
                reference_fill(theirs, 0xA5, ROOM);

                memory_fill(mine + 64, value, size);
                reference_fill(theirs + 64, value, size);

                same_bytes("memory_fill", "whole buffer", mine, theirs, ROOM);
        }
}

/*
        The two wide fills, which write a repeated four or eight byte pattern
        rather than a repeated byte.

        Their count is in ELEMENTS and not in bytes, which is the one thing a
        caller gets wrong about them, so the buffer is poisoned either side
        and compared whole: a routine that reads its count as bytes writes a
        quarter or an eighth of what it should, and the poison says so rather
        than the comparison passing on the part that was written.
*/
fn check_fill_wide()
{
        for (positive e = 0; e < EDGE_COUNT; e++)
        {
                positive count = edges[e] / 8;
                p32 narrow = (p32)next() | 1u;
                positive wide = ((positive)next() << 32) | (positive)next() | 1u;
                positive at;

                reference_fill(mine, 0xA5, ROOM);
                reference_fill(theirs, 0xA5, ROOM);
                memory_fill_32(mine + 64, narrow, count);
                for (at = 0; at < count; at++)
                        reference_copy(theirs + 64 + at * 4, address_of narrow, 4);
                same_bytes("memory_fill_32", "whole buffer", mine, theirs, ROOM);

                reference_fill(mine, 0xA5, ROOM);
                reference_fill(theirs, 0xA5, ROOM);
                memory_fill_64(mine + 64, wide, count);
                for (at = 0; at < count; at++)
                        reference_copy(theirs + 64 + at * 8, address_of wide, 8);
                same_bytes("memory_fill_64", "whole buffer", mine, theirs, ROOM);
        }
}

fn check_copy()
{
        for (positive i = 0; i < ROOM; i++)
                pattern[i] = (b8)next();

        for (positive e = 0; e < EDGE_COUNT; e++)
        {
                positive size = edges[e];

                reference_fill(mine, 0xA5, ROOM);
                reference_fill(theirs, 0xA5, ROOM);

                memory_copy_apart(mine + 64, pattern, size);
                reference_copy(theirs + 64, pattern, size);

                same_bytes("memory_copy_apart", "forward", mine, theirs, ROOM);
        }
}

/*
        The qsort floor exchanges disjoint records, but neither record has an
        alignment promise. Every short width is crossed with every pair of
        residues, and poison around both records catches a load or store on
        either side. The larger widths cross the vector/unrolled loop edges.
        Equal addresses and zero bytes are contractual no-ops and are checked
        without requiring either address to be readable.
*/
#define EXCHANGE_ROOM 4608

static p8 exchange_got[EXCHANGE_ROOM];
static p8 exchange_want[EXCHANGE_ROOM];

static fn exchange_case(positive left, positive right, positive size,
                        string_address detail)
{
        positive extent = right + size + 32;

        reference_fill(exchange_got, 0xa5, extent);
        reference_fill(exchange_want, 0xa5, extent);

        for (positive i = 0; i < size; i++)
        {
                p8 a = (p8)next();
                p8 b = (p8)next();

                exchange_got[left + i] = exchange_want[left + i] = a;
                exchange_got[right + i] = exchange_want[right + i] = b;
        }

        memory_exchange_apart(exchange_got + left, exchange_got + right, size);
        reference_exchange(exchange_want + left, exchange_want + right, size);
        same_bytes("memory_exchange_apart", detail, exchange_got, exchange_want,
                   extent);
}

fn check_exchange()
{
        static const positive large_widths[] = {
            129, 191, 192, 193, 255, 256, 257, 319, 320, 321, 511, 512, 513, 1000,
            1024, 1025, 2048,
        };

        memory_exchange_apart(null, null, 0);
        memory_exchange_apart(null, null, 37);

        reference_fill(exchange_got, 0xa5, EXCHANGE_ROOM);
        reference_fill(exchange_want, 0xa5, EXCHANGE_ROOM);
        memory_exchange_apart(exchange_got + 31, exchange_got + 31, 257);
        same_bytes("memory_exchange_apart", "equal address is untouched",
                   exchange_got, exchange_want, EXCHANGE_ROOM);

        for (positive size = 0; size <= 128; size++)
                for (positive left_residue = 0; left_residue < 16; left_residue++)
                        for (positive right_residue = 0; right_residue < 16;
                             right_residue++)
                                exchange_case(32 + left_residue,
                                              256 + right_residue, size,
                                              "every short width and residue");

        for (positive width = 0;
             width < sizeof(large_widths) / sizeof(large_widths[0]); width++)
                for (positive left_residue = 0; left_residue < 16; left_residue++)
                        for (positive right_residue = 0; right_residue < 16;
                             right_residue++)
                                exchange_case(64 + left_residue,
                                              2304 + right_residue,
                                              large_widths[width],
                                              "bulk loop edges and residues");
}

/*
        Cross every strategy boundary with every residue through a vector,
        while poison on both sides proves the exact span is the only span
        written. The second turn checks the defining involution too.
*/
fn check_frob()
{
        same("memory_frob", "null zero-sized return",
             (positive)memory_frob(null, 0), 0);

        for (positive e = 0; e < EDGE_COUNT; e++)
                for (positive residue = 0; residue < 32; residue++)
                {
                        positive size = edges[e];
                        positive offset = 64 + residue;

                        reference_fill(mine, 0xa5, ROOM);
                        reference_fill(theirs, 0xa5, ROOM);

                        for (positive i = 0; i < size; i++)
                        {
                                p8 value = (p8)next();

                                mine[offset + i] = theirs[offset + i] = value;
                        }

                        same("memory_frob", "returned original address",
                             (positive)memory_frob(mine + offset, size),
                             (positive)(address_any)(mine + offset));
                        reference_frob(theirs + offset, size);
                        same_bytes("memory_frob", "size, residue, and guards",
                                   mine, theirs, ROOM);

                        memory_frob(mine + offset, size);
                        reference_frob(theirs + offset, size);
                        same_bytes("memory_frob", "second turn restores bytes",
                                   mine, theirs, ROOM);
                }
}

/*
        Exact in-place reversal, not string reversal: zero bytes in the span
        are ordinary data and there is no terminator to find.  Every short
        size is crossed with every residue through thirty one, poison proves
        both guards remain untouched, and a second turn proves the operation
        is its own inverse.  The random, page-edge and one-megabyte cases keep
        the small exhaustive matrix from being the only shape exercised.
*/
#define REVERSE_MEDIUM (1 << 15)
#define REVERSE_LARGE ((1 << 20) + 128)

static p8 reverse_got[REVERSE_MEDIUM];
static p8 reverse_want[REVERSE_MEDIUM];
static p8 reverse_large_got[REVERSE_LARGE];
static p8 reverse_large_want[REVERSE_LARGE];

static fn reverse_prepare(positive extent, positive offset, positive size)
{
        reference_fill(reverse_got, 0xa5, extent);
        reference_fill(reverse_want, 0xa5, extent);

        for (positive i = 0; i < size; i++)
                reverse_got[offset + i] = reverse_want[offset + i] = (p8)next();
}

fn check_reverse()
{
        same("memory_reverse", "null zero-sized return",
             (positive)memory_reverse(null, 0), 0);

        for (positive size = 0; size <= 512; size++)
                for (positive residue = 0; residue < 32; residue++)
                {
                        positive offset = 64 + residue;
                        positive extent = offset + size + 64;
                        p8 address_to got = reverse_got + offset;
                        p8 address_to want = reverse_want + offset;

                        reverse_prepare(extent, offset, size);

                        same("memory_reverse", "returned original address",
                             (positive)memory_reverse(got, size),
                             (positive)(address_any)got);
                        reference_reverse(want, size);
                        same_bytes("memory_reverse", "all small sizes and residues",
                                   reverse_got, reverse_want, extent);

                        memory_reverse(got, size);
                        reference_reverse(want, size);
                        same_bytes("memory_reverse", "double reverse",
                                   reverse_got, reverse_want, extent);
                }

        // Repeatable random lengths and contents, with an independently
        // changing residue and guard width on every turn.
        for (positive turn = 0; turn < 512; turn++)
        {
                positive size = next() % 16385;
                positive offset = 33 + (next() & 63);
                positive extent = offset + size + 71;

                reverse_prepare(extent, offset, size);
                memory_reverse(reverse_got + offset, size);
                reference_reverse(reverse_want + offset, size);
                same_bytes("memory_reverse", "random length and contents",
                           reverse_got, reverse_want, extent);
        }

        // Put each exclusive end exactly on a 4096-byte boundary.  The
        // arrays keep mapped poison on both sides; exact-access floor audits
        // separately prove the implementations do not issue a wider access.
        {
                static const positive page_sizes[] = {
                    0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33,
                    63, 64, 65, 255, 256, 257, 2047, 2048, 2049,
                    4095, 4096, 4097,
                };
                positive base = (positive)(address_any)reverse_got;
                positive edge_offset = ((base + 4095) & ~(positive)4095) - base +
                                       8192;

                for (positive c = 0;
                     c < sizeof(page_sizes) / sizeof(page_sizes[0]); c++)
                {
                        positive size = page_sizes[c];
                        positive offset = edge_offset - size;
                        positive extent = edge_offset + 64;

                        reverse_prepare(extent, offset, size);
                        memory_reverse(reverse_got + offset, size);
                        reference_reverse(reverse_want + offset, size);
                        same_bytes("memory_reverse", "exclusive end at page edge",
                                   reverse_got, reverse_want, extent);
                }
        }

        // A resident one-megabyte span makes the bulk loop do real work and
        // catches counters or pointer arithmetic accidentally narrowed to 32
        // bits in a way the short matrix cannot.
        for (positive i = 0; i < REVERSE_LARGE; i++)
        {
                p8 value = (p8)next();

                reverse_large_got[i] = reverse_large_want[i] = value;
        }

        memory_reverse(reverse_large_got + 37, 1 << 20);
        reference_reverse(reverse_large_want + 37, 1 << 20);
        same_bytes("memory_reverse", "one megabyte with guards",
                   reverse_large_got, reverse_large_want, REVERSE_LARGE);

        memory_reverse(reverse_large_got + 37, 1 << 20);
        reference_reverse(reverse_large_want + 37, 1 << 20);
        same_bytes("memory_reverse", "one megabyte double reverse",
                   reverse_large_got, reverse_large_want, REVERSE_LARGE);
}

/*
        bins_count, bins_starts, bins_place, bins_skip and hash_fnv_u32 against
        the loops they stand for.

        Grids of every pitch and height in the lists, with rectangles drawn at
        random inside them (some with no columns or no rows), each given a
        number of records from none to sixteen; the three steps are run by the
        routines on one copy of the cells and by the loops on another, and the
        cells after each step, the sum answered and every list are compared.
        The cells end at a page that cannot be touched, the records end at one
        (so a record read past its count faults), and the lists end at one (so
        a list filled past what was counted faults); once more with each of
        them four bytes short of it, where a 64-bit move does not lie at a
        multiple of eight. The hash is over words that end at such a page, for
        every count in its list and a seed of each kind.
*/
static positive reference_bins_starts(p32 address_to cells, positive count)
{
        p32 sum = 0;

        for (positive at = 0; at < count; at++)
        {
                p32 had = cells[2 * at + 1];

                cells[2 * at] = sum, cells[2 * at + 1] = 0, sum += had;
        }
        return sum;
}

static fn reference_bins_count(p32 address_to cells, positive pitch, positive columns, positive rows, positive add)
{
        for (positive row = 0; row < rows; row++)
                for (positive column = 0; column < columns; column++)
                        cells[2 * (row * pitch + column) + 1] += (p32)add;
}

static fn reference_bins_place(p32 address_to cells, positive pitch, positive columns, positive rows, p32 address_to lists,
                               const p32 address_to records, positive count)
{
        for (positive row = 0; row < rows; row++)
                for (positive column = 0; column < columns; column++)
                {
                        p32 address_to cell = cells + 2 * (row * pitch + column);
                        p32 at = cell[0] + cell[1];

                        for (positive word = 0; word < 8 * count; word++)
                                lists[8 * (positive)at + word] = records[word];
                        cell[1] += (p32)count;
                }
}

static fn reference_bins_skip(p32 address_to cells, positive pitch, positive columns, positive rows)
{
        for (positive row = 0; row < rows; row++)
                for (positive column = 0; column < columns; column++)
                {
                        p32 address_to cell = cells + 2 * (row * pitch + column);

                        cell[0] += cell[1], cell[1] = 0;
                }
}

static positive reference_hash_fnv_u32(const p32 address_to words, positive count, positive hash)
{
        p32 now = (p32)hash;

        for (positive at = 0; at < count; at++)
                now = (now ^ words[at]) * 16777619u;
        return now;
}

fn check_bins()
{
        enum { RECTS = 10, LISTS = 256 * 4096 };
        static const positive pitches[] = {1, 2, 3, 8, 31};
        static const positive heights[] = {1, 2, 5, 17};
        static const positive hashed[] = {0, 1, 2, 3, 7, 8, 9, 255, 1024};
        static p32 want_cells[31 * 17 * 2], want_lists[LISTS / 4];
        p8 address_to cell_pages = memory(3 * 4096);
        p8 address_to record_pages = memory(2 * 4096);
        p8 address_to list_pages = memory(LISTS + 4096);
        p8 mapped = (bipolar)(positive)cell_pages > 0 && (bipolar)(positive)record_pages > 0 && (bipolar)(positive)list_pages > 0;
        p64 seed = 0x243f6a8885a308d3ull;
        positive counted = 1, started = 1, placed = 1, hashes = 1, grids = 0, entries = 0, empties = 0, skipped = 1, skips = 0;

        same("bins_place", "guard mappings", mapped, 1);
        if (!mapped)
                return;
        p8 protected = system_call_3(syscall(mprotect), (positive)(cell_pages + 8192), 4096, 0) == 0 &&
                       system_call_3(syscall(mprotect), (positive)(record_pages + 4096), 4096, 0) == 0 &&
                       system_call_3(syscall(mprotect), (positive)(list_pages + LISTS), 4096, 0) == 0;
        same("bins_place", "guard pages protected", protected, 1);
        if (!protected)
                return;

#define BINS_NEXT() (seed ^= seed << 13, seed ^= seed >> 7, seed ^= seed << 17, seed)
        for (positive skew = 0; skew <= 4; skew += 4)
                for (positive pw = 0; pw < array_count(pitches); pw++)
                        for (positive ph = 0; ph < array_count(heights); ph++)
                                for (positive round = 0; round < 40; round++)
                                {
                                        positive pitch = pitches[pw], height = heights[ph], count = pitch * height, total, want_total;
                                        p32 address_to cells = (p32 address_to)(cell_pages + 8192 - skew - 8 * count);
                                        positive rect[RECTS][5];

                                        for (positive at = 0; at < 2 * count; at++)
                                                cells[at] = want_cells[at] = at & 1 ? 0 : (p32)BINS_NEXT(); // the starts are rubbish until they are made
                                        for (positive r = 0; r < RECTS; r++)
                                        {
                                                positive x = BINS_NEXT() % pitch, y = BINS_NEXT() % height;

                                                rect[r][0] = x, rect[r][1] = y;
                                                rect[r][2] = BINS_NEXT() % (pitch - x + 1), rect[r][3] = BINS_NEXT() % (height - y + 1);
                                                rect[r][4] = r == 3 ? 0 : r == 5 ? 16 : BINS_NEXT() % 5;
                                                empties += !rect[r][2] || !rect[r][3] || !rect[r][4];
                                                (bins_count)(cells + 2 * (y * pitch + x), pitch, rect[r][2], rect[r][3], rect[r][4]);
                                                reference_bins_count(want_cells + 2 * (y * pitch + x), pitch, rect[r][2], rect[r][3], rect[r][4]);
                                        }
                                        counted &= !memory_compare(cells, want_cells, 8 * count);
                                        total = (bins_starts)(cells, count), want_total = reference_bins_starts(want_cells, count);
                                        started &= total == want_total && !memory_compare(cells, want_cells, 8 * count);
                                        if (32 * want_total + skew > LISTS)
                                        {
                                                placed = 0; // the lists' room is the check's to get right
                                                continue;
                                        }
                                        p32 address_to lists = (p32 address_to)(list_pages + LISTS - skew - 32 * want_total);

                                        for (positive r = 0; r < RECTS; r++)
                                        {
                                                p32 address_to records = (p32 address_to)(record_pages + 4096 - skew - 32 * rect[r][4]);

                                                for (positive word = 0; word < 8 * rect[r][4]; word++)
                                                        records[word] = (p32)BINS_NEXT();
                                                // Before some of the places, the cells of another rectangle passed over (bins_skip, a covering record's), as gpu.c's cut does
                                                // before it places that record: the starts move on by the counts so far and the counts are none.
                                                if (BINS_NEXT() % 3 == 0)
                                                {
                                                        positive sx = BINS_NEXT() % pitch, sy = BINS_NEXT() % height, sw = BINS_NEXT() % (pitch - sx + 1), sh = BINS_NEXT() % (height - sy + 1);

                                                        (bins_skip)(cells + 2 * (sy * pitch + sx), pitch, sw, sh);
                                                        reference_bins_skip(want_cells + 2 * (sy * pitch + sx), pitch, sw, sh);
                                                        skipped &= !memory_compare(cells, want_cells, 8 * count), skips++;
                                                }
                                                (bins_place)(cells + 2 * (rect[r][1] * pitch + rect[r][0]), pitch, rect[r][2], rect[r][3], lists, records, rect[r][4]);
                                                reference_bins_place(want_cells + 2 * (rect[r][1] * pitch + rect[r][0]), pitch, rect[r][2], rect[r][3], want_lists, records,
                                                                     rect[r][4]);
                                        }
                                        placed &= !memory_compare(cells, want_cells, 8 * count) && !memory_compare(lists, want_lists, 32 * want_total);
                                        grids++, entries += want_total;
                                }
        for (positive skew = 0; skew <= 4; skew += 4)
                for (positive h = 0; h < array_count(hashed); h++)
                {
                        p32 address_to words = (p32 address_to)(cell_pages + 8192 - skew - 4 * hashed[h]);

                        for (positive at = 0; at < hashed[h]; at++)
                                words[at] = (p32)BINS_NEXT();
                        for (positive kind = 0; kind < 4; kind++)
                        {
                                positive from = kind == 0 ? 2166136261u : kind == 1 ? 0 : kind == 2 ? 0xffffffffu : (p32)BINS_NEXT();

                                hashes &= (hash_fnv_u32)(words, hashed[h], from) == reference_hash_fnv_u32(words, hashed[h], from);
                        }
                }
#undef BINS_NEXT

        same("bins_count", "the counts of every rectangle's cells, every pitch and height", counted, 1);
        same("bins_starts", "the starts, the counts cleared and the sum answered", started, 1);
        same("bins_place", "the cells and every list, the records ending at a page that cannot be read", placed, 1);
        //  The lists reach rectangles with nothing in them and lists with a great deal, or the rows above prove little.
        same("bins_place", "grids compared, records placed and rectangles with nothing to place", grids == 2 * 5 * 4 * 40 && entries > 100000 && empties > 1000, 1);
        same("hash_fnv_u32", "the walk over words that end at a page that cannot be read, every count and seed", hashes, 1);
        same("hash_fnv_u32", "no words answers the hash given, from a pointer that cannot be read",
             (hash_fnv_u32)((const p32 address_to)(cell_pages + 8192), 0, 77), 77);
        same("bins_starts", "no cells answers nothing, from a pointer that cannot be touched", (bins_starts)((p32 address_to)(cell_pages + 8192), 0), 0);
        (bins_count)((p32 address_to)(cell_pages + 8192), 9, 0, 5, 1), (bins_count)((p32 address_to)(cell_pages + 8192), 9, 5, 0, 1);
        (bins_place)((p32 address_to)(cell_pages + 8192), 9, 0, 5, (p32 address_to)(list_pages + LISTS), (const p32 address_to)(record_pages + 4096), 3);
        (bins_place)((p32 address_to)(cell_pages + 8192), 9, 5, 0, (p32 address_to)(list_pages + LISTS), (const p32 address_to)(record_pages + 4096), 3);
        same("bins_place", "no columns or no rows touches no cell, list or record", 1, 1);
        same("bins_skip", "the cells of every rectangle passed over between the places, every pitch and height, the cells ending at a page that cannot be touched", skipped && skips > 1000, 1);
        (bins_skip)((p32 address_to)(cell_pages + 8192), 9, 0, 5), (bins_skip)((p32 address_to)(cell_pages + 8192), 9, 5, 0);
        same("bins_skip", "no columns or no rows touches no cell", 1, 1);
        memory_free(cell_pages, 3 * 4096);
        memory_free(record_pages, 2 * 4096);
        memory_free(list_pages, LISTS + 4096);
}

/*
        automaton_step and automaton_scan against the loop they stand for.

        Machines of every row width and row count in the lists, their cells
        and classes drawn at random within what a table may hold, started in
        every row, with the stop at every row's start, past all of them
        (nothing stops it) and at none (it is stopped where it stands). The
        bytes end at a page that cannot be read and the state is kept in the
        last two bytes before one that cannot be written, so a byte read past
        the count, or a state stored wider than it is, faults. The tables are
        compared afterwards: neither routine writes one.
*/
static positive reference_automaton_scan(const p16 address_to moves, const p8 address_to classes,
                                       positive stop, p16 address_to state,
                                       const p8 address_to bytes, positive size)
{
        positive at = 0, now = address_to state;

        while (at < size && now < stop)
                now = moves[now + classes[bytes[at++]]];
        address_to state = (p16)now;
        return at;
}

fn check_automaton()
{
        static const positive widths[] = {1, 2, 3, 5, 8, 16, 31, 64};
        static const positive rows[] = {1, 2, 4, 9, 33, 255};
        static const positive sizes[] = {0, 1, 2, 3, 7, 8, 9, 63, 64, 65, 255, 256, 1000, 4096};
        static p16 moves[255 * 64], moves_kept[255 * 64];
        static p8 classes[256], classes_kept[256];
        p8 address_to pages = memory(3 * 4096);
        p8 address_to states = memory(2 * 4096);
        p8 mapped = (bipolar)(positive)pages > 0 && (bipolar)(positive)states > 0;
        p64 seed = 0x9e3779b97f4a7c15ull;
        positive agreed = 1, kept = 1, stepped = 1, runs = 0, stops = 0, ends = 0;

        same("automaton_scan", "guard mappings", mapped, 1);
        if (!mapped)
                return;
        p8 protected =
            system_call_3(syscall(mprotect), (positive)(pages + 8192), 4096, 0) == 0 &&
            system_call_3(syscall(mprotect), (positive)(states + 4096), 4096, 0) == 0;
        same("automaton_scan", "guard pages protected", protected, 1);
        if (!protected)
                return;
        p16 address_to state = (p16 address_to)(states + 4096 - 2);

#define AUTOMATON_NEXT() (seed ^= seed << 13, seed ^= seed >> 7, seed ^= seed << 17, seed)
        for (positive w = 0; w < array_count(widths); w++)
                for (positive r = 0; r < array_count(rows); r++)
                {
                        positive width = widths[w], count = rows[r], cells = width * count;

                        for (positive at = 0; at < cells; at++)
                                moves[at] = (p16)(AUTOMATON_NEXT() % count * width);
                        for (positive at = 0; at < 256; at++)
                                classes[at] = (p8)(AUTOMATON_NEXT() % width);
                        memory_copy(moves_kept, moves, cells * sizeof(moves[0]));
                        memory_copy(classes_kept, classes, 256);

                        for (positive from = 0; from < count; from += count > 40 ? 17 : 1)
                                for (positive event = 0; event < width; event++)
                                        stepped &= (automaton_step)(moves, from * width, event) ==
                                                   moves[from * width + event];

                        for (positive s = 0; s < array_count(sizes); s++)
                        {
                                positive size = sizes[s];
                                p8 address_to bytes = pages + 8192 - size;

                                for (positive at = 0; at < size; at++)
                                        bytes[at] = (p8)AUTOMATON_NEXT();
                                for (positive from = 0; from < count; from += count > 40 ? 29 : 1)
                                        for (positive edge = 0; edge <= count + 1; edge += count > 40 ? 23 : 1)
                                        {
                                                //  count + 1 rows: a stop nothing reaches.
                                                positive stop = edge > count ? 65536 : edge * width;
                                                p16 want_state = (p16)(from * width);
                                                positive want = reference_automaton_scan(
                                                    moves, classes, stop, address_of want_state, bytes, size);

                                                address_to state = (p16)(from * width);
                                                positive got = (automaton_scan)(moves, classes, stop, state, bytes, size);

                                                agreed &= got == want && address_to state == want_state;
                                                runs++;
                                                stops += want < size;
                                                ends += want == size && size != 0;
                                        }
                        }
                        kept &= !memory_compare(moves_kept, moves, cells * sizeof(moves[0])) &&
                                !memory_compare(classes_kept, classes, 256);
                }
#undef AUTOMATON_NEXT

        same("automaton_step", "the cell at the state and the event, every row width", stepped, 1);
        same("automaton_scan", "bytes taken and the state left, every stop and start", agreed, 1);
        same("automaton_scan", "the tables are as they were", kept, 1);
        //  The lists reach both ways a run ends, or the row above proves nothing.
        same("automaton_scan", "runs that stopped early and runs that took every byte", stops > 1000 && ends > 1000, 1);
        same("automaton_scan", "runs compared", runs > 20000, 1);

        //  Nothing is read when there is nothing to do: no bytes, or a machine already stopped.
        address_to state = 3;
        same("automaton_scan", "no bytes takes none from pointers that cannot be read",
             (automaton_scan)((const p16 address_to)(pages + 8192), pages + 8192, 65536, state, pages + 8192, 0), 0);
        same("automaton_scan", "and leaves the state", address_to state, 3);
        address_to state = 9;
        same("automaton_scan", "a machine at its stop takes none and reads no table",
             (automaton_scan)((const p16 address_to)(pages + 8192), pages + 8192, 9, state, pages, 4096), 0);
        same("automaton_scan", "and leaves that state", address_to state, 9);
        memory_free(pages, 3 * 4096);
        memory_free(states, 2 * 4096);
}

/*
        The eleven routines ba2387d..ae6e42b folded into the compositor, the
        kit, the font engine and the shell's lexer, against the loops their
        callers had before them:

            memory_add_sat_u8_rect         font_cluster_plan's saturating add
            memory_copy_rect_reverse_u32   windows_rotate_u32's half turn
            memory_lerp_u32_row            ui_stretch_blend, a row at a time
            memory_mix_const_u32_rect      compositor_chip_draw's cover * alpha
                                           / 255 and compositor_mix
            memory_or_u32                  the Wayland host's XRGB8888 made
                                           opaque
            memory_over_cover_u32_rect     ui_kit_covers' ui_over(ui_tone(ink,
                                           cover), pixel)
            memory_over_u32_rect           compositor_wl_over over a rectangle
            memory_scale_u8                compositor_gpu_spots' cover * alpha
                                           / 255
            memory_swizzle_rb_u32          compositor_wl_convert_pixel's ABGR
                                           and XBGR
            memory_transpose_u8            font_blend_turned's turn of a
                                           glyph's columns into rows
            string_first_of_either_or_end  the lexer's walk to a quote, a
                                           backslash or the end

        and the four the kit's moon was floored onto, against the C its
        wallpaper had as the C stand-ins of them:

            noise_value_q16                ui_moon_noise, a run of points
            memory_toward_dither_u32       ui_moon_fresnel's lift, dithered
            memory_ramp_dither_u32         ui_moon_paper_ground's sky, dithered
            memory_grain_u32               ui_moon_fresnel's regolith grain

        Every input and every output is a room with a page that cannot be
        touched on either side of it. A case is laid flush against the page
        after it, then at residues that walk the start through every
        alignment across the shapes (each byte for a byte pointer, each
        word for a word pointer, sixty four bytes round, and every pair of
        them for one shape), then flush against the page before it; a read
        or a write past either end of what the routine was given faults and
        ends the run. The output's neighbourhood -- sixty four bytes either
        side, and every gap a stride leaves between rows -- is compared byte
        for byte with the reference's copy, and each input with what it
        held. Sizes run through every one to past the widest body's step (a
        row to three hundred, a rectangle to seventy across and nine down),
        then a few large ones. Values are the kinds that decide each: alpha
        and cover of none, all and between, sums one short of saturating and
        past it, pixels premultiplied and not, masks of nothing, the alpha
        and every bit, and for the scan each byte, both, neither, the two
        the same, a byte with its high bit set and the bytes a word-at-a-time
        test is likeliest to mistake for them. In-place calls where a caller
        makes them (memory_or_u32 and memory_swizzle_rb_u32). On x86-64 each
        tier is walked down, AVX-512 and AVX2, AVX2 alone, then neither; on
        riscv64 V as each call's first ask finds it, then said absent.

        The references are the callers' C as it stood, renamed.
*/
static inline unsigned int reference_ui_div255(unsigned int x)
{
        return (x + 1 + (x >> 8)) >> 8;
}

// from, moved amount 256ths of the way to to.
static inline unsigned int reference_window_mix(unsigned int from, unsigned int to, unsigned int amount)
{
        unsigned int mixed = 0;

        for (unsigned int shift = 0; shift < 24; shift += 8)
                mixed |= (((from >> shift & 255) * (256 - amount) + (to >> shift & 255) * amount + 128) >> 8) << shift;
        return mixed;
}

static unsigned int reference_ui_tone(unsigned int colour, unsigned int alpha)
{
        alpha = alpha > 255 ? 255 : alpha;
        return alpha << 24 | reference_window_mix(0, colour & 0xffffffu, alpha + (alpha >> 7));
}

static inline unsigned int reference_ui_over(unsigned int pixel, unsigned int under)
{
        unsigned int rest = 255 - (pixel >> 24), out = 0;

        if (!(pixel >> 24))
                return under;
        if (!rest)
                return pixel;
        if (!under)
                return pixel;
        {
                unsigned int v_rb = (under & 0x00ff00ffu) * rest + 0x007f007fu;
                unsigned int v_ag = ((under >> 8) & 0x00ff00ffu) * rest + 0x007f007fu;
                unsigned int sum_rb = (pixel & 0x00ff00ffu) + ((v_rb + 0x00010001u + ((v_rb >> 8) & 0x00ff00ffu)) >> 8 & 0x00ff00ffu);
                unsigned int sum_ag = ((pixel >> 8) & 0x00ff00ffu) + ((v_ag + 0x00010001u + ((v_ag >> 8) & 0x00ff00ffu)) >> 8 & 0x00ff00ffu);

                if (!((sum_rb | sum_ag) & 0x01000100u))
                        return (sum_rb & 0x00ff00ffu) | (sum_ag & 0x00ff00ffu) << 8;
        }
        for (unsigned int shift = 0; shift < 32; shift += 8)
        {
                unsigned int channel = (pixel >> shift & 255) + reference_ui_div255((under >> shift & 255) * rest + 127);

                out |= (channel > 255 ? 255 : channel) << shift;
        }
        return out;
}

static unsigned int reference_compositor_mix(unsigned int ground, unsigned int ink, unsigned int cover)
{
        const unsigned long long low = 0xff00ff00ffull, one = 0x100010001ull;
        unsigned long long g, i, x;

        if (cover == 0)
                return ground & 0xffffffu;
        if (cover >= 255)
                return ink & 0xffffffu;
        g = (ground & 0xffu) | (unsigned long long)(ground & 0xff00u) << 8 | (unsigned long long)(ground & 0xff0000u) << 16;
        i = (ink & 0xffu) | (unsigned long long)(ink & 0xff00u) << 8 | (unsigned long long)(ink & 0xff0000u) << 16;
        x = g * (255 - cover) + i * cover + 127 * one;
        x += one + ((x >> 8) & low);
        x = (x >> 8) & low;
        return (unsigned int)((x & 0xff) | (x >> 8 & 0xff00) | (x >> 16 & 0xff0000));
}

static unsigned int reference_compositor_wl_over(unsigned int over, unsigned int under)
{
        unsigned int through = 255u - (over >> 24), out = 0xff000000u;

        for (unsigned int shift = 0; shift < 24; shift += 8)
        {
                unsigned int sum = ((over >> shift) & 255u) + ((((under >> shift) & 255u) * through + 127u) / 255u);

                out |= (sum > 255u ? 255u : sum) << shift;
        }
        return out;
}

static inline unsigned int reference_ui_stretch_blend(unsigned int a, unsigned int b, unsigned int f)
{
        unsigned int green = ((((a >> 8) & 0x00ff00ffu) * (256 - f) + ((b >> 8) & 0x00ff00ffu) * f) >> 8) & 0x00ff00ffu;
        unsigned int blue = (((a & 0x00ff00ffu) * (256 - f) + (b & 0x00ff00ffu) * f) >> 8) & 0x00ff00ffu;

        return green << 8 | blue;
}

static fn reference_memory_add_sat_u8_rect(p8 address_to into, positive into_stride, const p8 address_to from,
                                           positive from_stride, positive width, positive height)
{
        for (positive r = 0; r < height; r++)
                for (positive c = 0; c < width; c++)
                {
                        p8 address_to at = into + r * into_stride + c;
                        unsigned int sum = (unsigned int)*at + from[r * from_stride + c];

                        *at = (p8)(sum > 255 ? 255 : sum);
                }
}

static fn reference_memory_copy_rect_reverse_u32(p32 address_to into, bipolar into_pitch, const p32 address_to from,
                                                 bipolar from_pitch, positive width, positive height)
{
        for (positive ly = 0; ly < height; ly++)
        {
                p32 address_to to = into + (bipolar)ly * into_pitch;
                const p32 address_to row = from + (bipolar)ly * from_pitch;

                for (positive each = 0; each < width; each++)
                        to[each] = row[width - 1 - each];
        }
}

static fn reference_memory_lerp_u32_row(p32 address_to into, const p32 address_to a, const p32 address_to b, positive count,
                                        positive t)
{
        for (positive k = 0; k < count; k++)
                into[k] = reference_ui_stretch_blend(a[k], b[k], (unsigned int)t);
}

//      The kit moon's hash (ui_moon_hash): a number spread over all of its bits.
static p32 reference_moon_hash(p32 n)
{
        n ^= n >> 16, n *= 0x7feb352du, n ^= n >> 15, n *= 0x846ca68bu, n ^= n >> 16;
        return n;
}

//      A pixel's dither, -256 to 254 (512ths of a step): the hash of its column and row, its two low bytes summed less 256.
static b32 reference_moon_dither(p32 x, p32 y)
{
        p32 h = reference_moon_hash(x * 73856093u ^ y * 19349663u);

        return (b32)(h & 255) + (b32)(h >> 8 & 255) - 256;
}

static b32 reference_moon_ease(b32 f)
{
        b32 a = (f * f) >> 15, b = (a * f) >> 15, u = a + 2 * (a - b);

        return u - (u >> 15);
}

static b32 reference_moon_lerp(b32 a, b32 b, b32 t)
{
        return a + (((b - a) * t) >> 15);
}

static fn reference_noise_value_q16(b32 address_to out, const b32 address_to x, const b32 address_to y, const b32 address_to z,
                                    positive count, p32 seed)
{
        for (positive k = 0; k < count; k++)
        {
                p32 ax = (p32)(x[k] >> 16) * 73856093u, ay = (p32)(y[k] >> 16) * 19349663u, az = (p32)(z[k] >> 16) * 83492791u;
                b32 u = reference_moon_ease((x[k] & 0xffff) >> 1), v = reference_moon_ease((y[k] & 0xffff) >> 1);
                b32 w = reference_moon_ease((z[k] & 0xffff) >> 1), c[8];

                for (int i = 0; i < 8; i++)
                        c[i] = (b32)(reference_moon_hash(reference_moon_hash((ax + (i & 1 ? 73856093u : 0)) ^ (ay + (i & 2 ? 19349663u : 0)) ^
                                                                             (az + (i & 4 ? 83492791u : 0)) ^ seed)) >> 18) - 8192;
                out[k] = reference_moon_lerp(reference_moon_lerp(reference_moon_lerp(c[0], c[1], u), reference_moon_lerp(c[2], c[3], u), v),
                                             reference_moon_lerp(reference_moon_lerp(c[4], c[5], u), reference_moon_lerp(c[6], c[7], u), v), w);
        }
}

static fn reference_memory_toward_dither_u32(p32 address_to into, const p32 address_to target, const p16 address_to weight, positive count,
                                             p32 x, p32 y)
{
        for (positive k = 0; k < count; k++)
        {
                b32 d = reference_moon_dither(x + (p32)k, y) * 64, w = weight[k];
                p32 out = into[k] & 0xff000000u;

                for (int c = 0; c < 3; c++)
                {
                        b32 i = (b32)(into[k] >> (8 * c) & 255), t = (b32)(target[k] >> (8 * c) & 255), v = ((i << 15) + (t - i) * w + d + 16384) >> 15;

                        out |= (p32)(v < 0 ? 0 : v > 255 ? 255 : v) << (8 * c);
                }
                into[k] = out;
        }
}

static fn reference_memory_ramp_dither_u32(p32 address_to into, positive count, p32 x, p32 y, const b32 address_to ramp)
{
        for (positive k = 0; k < count; k++)
        {
                b32 d = reference_moon_dither(x + (p32)k, y) * 128;
                p32 out = 0;

                for (int c = 0; c < 3; c++)
                {
                        b32 v = (b32)((p32)ramp[c] + (p32)ramp[3 + c] * (p32)k + (p32)d + 32768u) >> 16;

                        out |= (p32)(v < 0 ? 0 : v > 255 ? 255 : v) << (16 - 8 * c);
                }
                into[k] = out;
        }
}

static fn reference_memory_grain_u32(p32 address_to into, const b16 address_to add, positive count)
{
        for (positive k = 0; k < count; k++)
        {
                p32 out = into[k] & 0xff000000u;

                for (int c = 0; c < 3; c++)
                {
                        b32 v = (b32)(into[k] >> (8 * c) & 255) + add[k];

                        out |= (p32)(v < 0 ? 0 : v > 255 ? 255 : v) << (8 * c);
                }
                into[k] = out;
        }
}

static fn reference_memory_mix_const_u32_rect(p32 address_to into, positive into_stride, const p8 address_to cover,
                                              positive cover_stride, positive width, positive height, p32 ink, p32 alpha)
{
        for (positive py = 0; py < height; py++)
        {
                p32 address_to at = into + py * into_stride;

                for (positive px = 0; px < width; px++, at++)
                {
                        unsigned int share = (unsigned int)cover[py * cover_stride + px] * (alpha & 255) / 255;

                        if (share)
                                *at = reference_compositor_mix(*at, ink, share);
                }
        }
}

static fn reference_memory_or_u32(p32 address_to into, const p32 address_to from, positive count, p32 mask)
{
        for (positive i = 0; i < count; i++)
                into[i] = from[i] | mask;
}

static fn reference_memory_over_cover_u32_rect(p32 address_to into, positive into_stride, const p8 address_to cover,
                                               positive cover_stride, positive width, positive height, p32 ink)
{
        for (positive row = 0; row < height; row++)
        {
                p32 address_to pixels = into + row * into_stride;
                const p8 address_to covers = cover + row * cover_stride;

                for (positive column = 0; column < width; column++)
                        if (covers[column])
                                pixels[column] = reference_ui_over(reference_ui_tone(ink, covers[column]), pixels[column]);
        }
}

static fn reference_memory_over_u32_rect(p32 address_to to, positive pitch, const p32 address_to pixels, positive stride,
                                         positive width, positive height)
{
        for (positive r = 0; r < height; r++)
        {
                const p32 address_to from = pixels + r * stride;
                p32 address_to into = to + r * pitch;

                for (positive c = 0; c < width; c++)
                        into[c] = reference_compositor_wl_over(from[c], into[c]);
        }
}

static fn reference_memory_over_under_u32_rect(p32 address_to to, positive pitch, const p32 address_to pixels, positive stride,
                                               const p32 address_to under, positive under_stride, positive width, positive height)
{
        for (positive r = 0; r < height; r++)
                for (positive c = 0; c < width; c++)
                        to[r * pitch + c] = reference_compositor_wl_over(pixels[r * stride + c], under[r * under_stride + c]);
}

static fn reference_memory_scale_u8(p8 address_to table, const p8 address_to cover, positive cells, unsigned int alpha)
{
        for (positive i = 0; i < cells; i++)
                table[i] = (p8)(cover[i] * (alpha & 255) / 255);
}

static fn reference_memory_swizzle_rb_u32(p32 address_to into, const p32 address_to from, positive count, p32 or_mask)
{
        for (positive i = 0; i < count; i++)
        {
                p32 v = from[i];

                into[i] = ((v & 0xff00ff00u) | ((v & 0xffu) << 16) | ((v >> 16) & 0xffu)) | or_mask;
        }
}

static fn reference_memory_transpose_u8(p8 address_to into, positive into_pitch, const p8 address_to from,
                                        bipolar from_column, bipolar from_row, positive rows, positive cols)
{
        for (positive k = 0; k < rows; k++)
        {
                const p8 address_to column = from + (bipolar)k * from_row;
                p8 address_to row = into + k * into_pitch;

                for (positive r = 0; r < cols; r++)
                        row[r] = column[(bipolar)r * from_column];
        }
}

static string_address reference_string_first_of_either_or_end(string_address at, p8 first, p8 second)
{
        while (*at && *at != first && *at != second)
                at++;
        return at;
}

//      A room: PIXEL_ROOM bytes with a page that cannot be touched on either side.
enum { PIXEL_ROOM = 1 << 20, PIXEL_NEAR = 64, PIXEL_ROOMS = 4 };
struct pixel_room
{
        p8 address_to map, address_to low, address_to high;
};
struct pixel_window
{
        p8 address_to from;
        positive size;
};
static struct pixel_room pixel_rooms[PIXEL_ROOMS];
static p8 pixel_mirror[PIXEL_ROOM], pixel_kept[3][PIXEL_ROOM];
static positive pixel_shown, pixel_tier;

static b32 pixel_rooms_open(void)
{
        b32 fine = true;

        for (positive k = 0; k < PIXEL_ROOMS; k++)
        {
                struct pixel_room address_to room = &pixel_rooms[k];

                room->map = memory(PIXEL_ROOM + 8192);
                if ((bipolar)(positive)room->map <= 0)
                {
                        room->map = null;
                        fine = false;
                        continue;
                }
                room->low = room->map + 4096, room->high = room->low + PIXEL_ROOM;
                fine = fine && system_call_3(syscall(mprotect), (positive)room->map, 4096, 0) == 0 &&
                       system_call_3(syscall(mprotect), (positive)room->high, 4096, 0) == 0;
        }
        same("pixel rows", "a room with a page that cannot be touched either side, for each input and output", fine, 1);
        return fine;
}

static fn pixel_rooms_close(void)
{
        for (positive k = 0; k < PIXEL_ROOMS; k++)
                if (pixel_rooms[k].map)
                        memory_free(pixel_rooms[k].map, PIXEL_ROOM + 8192), pixel_rooms[k].map = null;
}

//      Where a case's pointer goes: span bytes, the lowest of them lowest bytes from the pointer (below it when a pitch
//      is negative), ending residue bytes short of the page after the room, or beginning residue bytes past the one
//      before it.
static p8 address_to pixel_place(const struct pixel_room address_to room, positive span, bipolar lowest, b32 late,
                                 positive residue)
{
        p8 address_to first = late ? room->high - span - residue : room->low + residue;

        return first - lowest;
}

//      The residue a shape's k-th case is laid at, of eight (0 flush, and k 8 flush against the page before): the
//      seven between walk with the shape, so the shapes between them meet every residue a step apart.
static positive pixel_residue(positive shape, positive k, positive step)
{
        return k == 0 || k >= 8 ? 0 : (shape * 7 + k * 9) % 64 / step * step;
}

//      What is compared round a span: PIXEL_NEAR bytes either side, cut to the room.
static struct pixel_window pixel_near(const struct pixel_room address_to room, p8 address_to first, positive span)
{
        p8 address_to from = first - room->low < PIXEL_NEAR ? room->low : first - PIXEL_NEAR;
        p8 address_to to = room->high - (first + span) < PIXEL_NEAR ? room->high : first + span + PIXEL_NEAR;

        return (struct pixel_window){from, (positive)(to - from)};
}

//      A byte that decides a saturating add or a scale: the ends, either side of the middle, or noise.
static p8 pixel_byte(positive v)
{
        static const p8 edges[] = {0, 1, 2, 127, 128, 129, 253, 254, 255};

        return v & 1 ? (p8)(v >> 8) : edges[(v >> 8) % array_count(edges)];
}

//      A cover: none a quarter of the time, all a quarter, and the bytes above.
static p8 pixel_cover(positive v)
{
        return v % 4 == 0 ? 0 : v % 4 == 1 ? 255 : pixel_byte(v >> 2);
}

//      A pixel of a kind that decides a blend: nothing, every bit, alpha none, alpha all, premultiplied (twice as
//      often), one byte four times, four bytes of the kinds above, or noise -- which is a colour past its alpha.
static p32 pixel_word(positive v)
{
        p32 r = (p32)(v >> 8), a = r >> 24;

        switch (v % 10)
        {
        case 0:
                return 0;
        case 1:
                return 0xffffffffu;
        case 2:
                return r & 0x00ffffffu;
        case 3:
                return r | 0xff000000u;
        case 4:
        case 5:
                return a << 24 | (r & 255) * a / 255 | (r >> 8 & 255) * a / 255 << 8 | (r >> 16 & 255) * a / 255 << 16;
        case 6:
                return (p32)pixel_byte(v >> 4) * 0x01010101u;
        case 7:
                return (p32)pixel_byte(v >> 4) | (p32)pixel_byte(v >> 14) << 8 | (p32)pixel_byte(v >> 24) << 16 |
                       (p32)pixel_byte(v >> 34) << 24;
        default:
                return r;
        }
}

enum { PIXEL_BYTES, PIXEL_COVERS, PIXEL_WORDS };

static fn pixel_fill(p8 address_to at, positive size, positive kind)
{
        for (positive i = 0; i < size; i += kind == PIXEL_WORDS ? 4 : 1)
                if (kind == PIXEL_WORDS)
                {
                        p32 word = pixel_word(next());

                        for (positive k = 0; k < 4 && i + k < size; k++)
                                at[i + k] = (p8)(word >> 8 * k);
                }
                else
                        at[i] = kind == PIXEL_COVERS ? pixel_cover(next()) : pixel_byte(next());
}

//      The output's window filled and the reference's copy of it made: answers where the copy of at is.
static p8 address_to pixel_out(struct pixel_window address_to window, positive room, p8 address_to at, positive span,
                               bipolar lowest, positive kind)
{
        *window = pixel_near(&pixel_rooms[room], at + lowest, span);
        pixel_fill(window->from, window->size, kind);
        memory_copy(pixel_mirror, window->from, window->size);
        return pixel_mirror + (at - window->from);
}

//      An input's window filled and kept in slot.
static fn pixel_in(struct pixel_window address_to window, positive slot, positive room, p8 address_to at, positive span,
                   bipolar lowest, positive kind)
{
        *window = pixel_near(&pixel_rooms[room], at + lowest, span);
        pixel_fill(window->from, window->size, kind);
        memory_copy(pixel_kept[slot], window->from, window->size);
}

//      One window against what it should hold: a check, and a failure when a byte is not; the first two dozen say
//      where, with the case's shape and values packed as the case wrote them.
static fn pixel_same(string_address name, string_address detail, const p8 address_to got, const p8 address_to want,
                     positive size, positive shape, positive values)
{
        checks++;
        for (positive i = 0; i < size; i++)
                if (got[i] != want[i])
                {
                        failures++;
                        if (pixel_shown++ < 8)
                                string_format(log, "  FAIL %s %s: byte %p of the window is %p want %p (shape %p, "
                                                   "values %p, tier %p)\n",
                                              name, detail, i, (positive)got[i], (positive)want[i], shape, values,
                                              pixel_tier);
                        return;
                }
}

//      After a call: the output against the reference's copy, each input against what it held.
static fn pixel_after(string_address name, const struct pixel_window address_to out,
                      const struct pixel_window address_to in, positive inputs, positive shape, positive values)
{
        pixel_same(name, "the output and the bytes round it", out->from, pixel_mirror, out->size, shape, values);
        for (positive k = 0; k < inputs; k++)
                pixel_same(name, "an input as it was", in[k].from, pixel_kept[k], in[k].size, shape, values);
}

//      An answer against the reference's, as pixel_same counts it.
static fn pixel_answer(string_address name, string_address detail, positive got, positive want, positive shape,
                       positive values)
{
        checks++;
        if (got != want)
        {
                failures++;
                if (pixel_shown++ < 8)
                        string_format(log, "  FAIL %s %s: got %p want %p (shape %p, values %p, tier %p)\n", name, detail,
                                      got, want, shape, values, pixel_tier);
        }
}

//      Before every call: on riscv64's first tier the call is the first the process makes, so a body that asks for V
//      asks; nothing on the others.
#define PIXEL_CALL() VECTOR_FIRST_CALL(pixel_tier == 0)

/*
        The rectangles: every width to forty and either side of 48 and 64 and
        70, then the wide ones (three rows at most), every height in the
        list, and four pairs of gaps after a row, one for each pointer.
*/
static const positive pixel_narrow[] = {41, 47, 48, 49, 63, 64, 65, 70};
static const positive pixel_wide[] = {127, 128, 129, 255, 256, 257, 1000, 4097};
static const positive pixel_heights[] = {0, 1, 2, 3, 5, 9};
#define PIXEL_WIDTHS (41 + array_count(pixel_narrow) + array_count(pixel_wide))
#define PIXEL_SHAPES (PIXEL_WIDTHS * array_count(pixel_heights) * 4)

static b32 pixel_shape(positive index, positive address_to width, positive address_to height, positive address_to gap,
                       positive address_to other)
{
        static const positive gaps[4][2] = {{0, 0}, {1, 5}, {3, 0}, {17, 33}};
        positive w = index / (array_count(pixel_heights) * 4), g = index % 4;

        *width = w < 41 ? w : w < 41 + array_count(pixel_narrow) ? pixel_narrow[w - 41] : pixel_wide[w - 41 - array_count(pixel_narrow)];
        *height = pixel_heights[index / 4 % array_count(pixel_heights)];
        *gap = gaps[g][0], *other = gaps[g][1];
        return *width <= 300 || *height <= 3;
}

//      The cases a shape is laid at: all nine for a narrow one, the two flush for a wide one.
#define PIXEL_EACH(k, width) for (positive k = 0; k <= 8; k += (width) > 300 ? 8 : 1)
#define PIXEL_SHAPE_CODE(width, height, gap, other, k) ((width) << 32 | (height) << 24 | (gap) << 16 | (other) << 8 | (k))

static fn pixel_add_sat(void)
{
        for (positive shape = 0; shape < PIXEL_SHAPES; shape++)
        {
                positive width, height, gi, gf;

                if (!pixel_shape(shape, &width, &height, &gi, &gf))
                        continue;
                positive is = width + gi, fs = width + gf;
                positive into_span = height ? (height - 1) * is + width : 0, from_span = height ? (height - 1) * fs + width : 0;

                PIXEL_EACH(k, width)
                {
                        struct pixel_window out, in[1];
                        p8 address_to into = pixel_place(&pixel_rooms[0], into_span, 0, k < 8, pixel_residue(shape, k, 1));
                        p8 address_to from = pixel_place(&pixel_rooms[1], from_span, 0, k < 8, pixel_residue(shape * 3 + 1, k, 1));
                        p8 address_to want = pixel_out(&out, 0, into, into_span, 0, PIXEL_BYTES);

                        pixel_in(&in[0], 0, 1, from, from_span, 0, PIXEL_BYTES);
                        reference_memory_add_sat_u8_rect(want, is, from, fs, width, height);
                        PIXEL_CALL();
                        (memory_add_sat_u8_rect)(into, is, from, fs, width, height);
                        pixel_after("memory_add_sat_u8_rect", &out, in, 1, PIXEL_SHAPE_CODE(width, height, gi, gf, k), 0);
                }
        }
        //  Every pair of residues for one shape, three rows of 33 with gaps.
        for (positive ri = 0; ri < 64; ri++)
                for (positive rf = 0; rf < 64; rf++)
                {
                        struct pixel_window out, in[1];
                        p8 address_to into = pixel_place(&pixel_rooms[0], 2 * 36 + 33, 0, true, ri);
                        p8 address_to from = pixel_place(&pixel_rooms[1], 2 * 40 + 33, 0, true, rf);
                        p8 address_to want = pixel_out(&out, 0, into, 2 * 36 + 33, 0, PIXEL_BYTES);

                        pixel_in(&in[0], 0, 1, from, 2 * 40 + 33, 0, PIXEL_BYTES);
                        reference_memory_add_sat_u8_rect(want, 36, from, 40, 33, 3);
                        PIXEL_CALL();
                        (memory_add_sat_u8_rect)(into, 36, from, 40, 33, 3);
                        pixel_after("memory_add_sat_u8_rect", &out, in, 1, ri << 8 | rf, 1);
                }
}

static fn pixel_reverse(void)
{
        for (positive shape = 0; shape < PIXEL_SHAPES; shape++)
        {
                positive width, height, gi, gf;

                if (!pixel_shape(shape, &width, &height, &gi, &gf))
                        continue;
                //  Each pitch either way: the half turn hands it the picture's pitch negative, into's rows rising.
                for (positive sign = 0; sign < 4; sign++)
                {
                        bipolar ip = (bipolar)(width + gi) * (sign & 1 ? -1 : 1), fp = (bipolar)(width + gf) * (sign & 2 ? -1 : 1);
                        positive into_span = height ? 4 * ((height - 1) * (width + gi) + width) : 0;
                        positive from_span = height ? 4 * ((height - 1) * (width + gf) + width) : 0;
                        bipolar into_lowest = height && ip < 0 ? 4 * (bipolar)(height - 1) * ip : 0;
                        bipolar from_lowest = height && fp < 0 ? 4 * (bipolar)(height - 1) * fp : 0;

                        PIXEL_EACH(k, width)
                        {
                                struct pixel_window out, in[1];
                                p8 address_to into = pixel_place(&pixel_rooms[0], into_span, into_lowest, k < 8, pixel_residue(shape + sign, k, 4));
                                p8 address_to from = pixel_place(&pixel_rooms[1], from_span, from_lowest, k < 8,
                                                                 pixel_residue(shape * 3 + sign, k, 4));
                                p8 address_to want = pixel_out(&out, 0, into, into_span, into_lowest, PIXEL_WORDS);

                                pixel_in(&in[0], 0, 1, from, from_span, from_lowest, PIXEL_WORDS);
                                reference_memory_copy_rect_reverse_u32((p32 address_to)want, ip, (const p32 address_to)from, fp, width, height);
                                PIXEL_CALL();
                                (memory_copy_rect_reverse_u32)((p32 address_to)into, ip, (const p32 address_to)from, fp, width, height);
                                pixel_after("memory_copy_rect_reverse_u32", &out, in, 1, PIXEL_SHAPE_CODE(width, height, gi, gf, k), sign);
                        }
                }
        }
}

/*
        The rows: every count to three hundred, then the wide ones, each laid
        at the nine places a shape is.
*/
static const positive pixel_long[] = {511, 512, 513, 1000, 4097, 65539};
#define PIXEL_COUNTS (301 + array_count(pixel_long))
#define PIXEL_COUNT(index) ((index) < 301 ? (index) : pixel_long[(index) - 301])

static fn pixel_lerp(void)
{
        static const positive ts[] = {0, 1, 2, 127, 128, 129, 254, 255, 256};

        for (positive c = 0; c < PIXEL_COUNTS; c++)
        {
                positive count = PIXEL_COUNT(c), span = 4 * count;

                PIXEL_EACH(k, count)
                        for (positive turn = 0; turn < (count > 300 ? array_count(ts) : 2); turn++)
                        {
                                struct pixel_window out, in[2];
                                positive t = count > 300 ? ts[turn] : turn ? next() % 257 : ts[(c + k) % array_count(ts)];
                                p8 address_to into = pixel_place(&pixel_rooms[0], span, 0, k < 8, pixel_residue(c, k, 4));
                                p8 address_to a = pixel_place(&pixel_rooms[1], span, 0, k < 8, pixel_residue(c * 3 + 1, k, 4));
                                p8 address_to b = pixel_place(&pixel_rooms[2], span, 0, k < 8, pixel_residue(c * 5 + 2, k, 4));
                                p8 address_to want = pixel_out(&out, 0, into, span, 0, PIXEL_WORDS);

                                pixel_in(&in[0], 0, 1, a, span, 0, PIXEL_WORDS);
                                pixel_in(&in[1], 1, 2, b, span, 0, PIXEL_WORDS);
                                reference_memory_lerp_u32_row((p32 address_to)want, (const p32 address_to)a, (const p32 address_to)b, count, t);
                                PIXEL_CALL();
                                (memory_lerp_u32_row)((p32 address_to)into, (const p32 address_to)a, (const p32 address_to)b, count, t);
                                pixel_after("memory_lerp_u32_row", &out, in, 2, count << 8 | k, t);
                        }
        }
}

//      The moon's four: every count, at every residue of each pointer, values of every kind (coordinates past the
//      lattice's every edge and sign, weights of none, all and between, steps that wrap, adds that saturate either way).
static fn pixel_noise(void)
{
        static const p32 seeds[] = {0, 1, 77u, 405u, 0x80000000u, 0xffffffffu};

        for (positive c = 0; c < PIXEL_COUNTS; c++)
        {
                positive count = PIXEL_COUNT(c), span = 4 * count;

                PIXEL_EACH(k, count)
                {
                        struct pixel_window out, in[3];
                        p32 seed = seeds[(c + k) % array_count(seeds)];
                        p8 address_to into = pixel_place(&pixel_rooms[0], span, 0, k < 8, pixel_residue(c, k, 4));
                        p8 address_to x = pixel_place(&pixel_rooms[1], span, 0, k < 8, pixel_residue(c * 3 + 1, k, 4));
                        p8 address_to y = pixel_place(&pixel_rooms[2], span, 0, k < 8, pixel_residue(c * 5 + 2, k, 4));
                        p8 address_to z = pixel_place(&pixel_rooms[3], span, 0, k < 8, pixel_residue(c * 7 + 3, k, 4));
                        p8 address_to want = pixel_out(&out, 0, into, span, 0, PIXEL_WORDS);

                        pixel_in(&in[0], 0, 1, x, span, 0, PIXEL_WORDS);
                        pixel_in(&in[1], 1, 2, y, span, 0, PIXEL_WORDS);
                        pixel_in(&in[2], 2, 3, z, span, 0, PIXEL_WORDS);
                        reference_noise_value_q16((b32 address_to)want, (const b32 address_to)x, (const b32 address_to)y, (const b32 address_to)z, count, seed);
                        PIXEL_CALL();
                        (noise_value_q16)((b32 address_to)into, (const b32 address_to)x, (const b32 address_to)y, (const b32 address_to)z, count, seed);
                        pixel_after("noise_value_q16", &out, in, 3, count << 8 | k, seed);
                }
        }
}

static fn pixel_toward(void)
{
        for (positive c = 0; c < PIXEL_COUNTS; c++)
        {
                positive count = PIXEL_COUNT(c), span = 4 * count;

                PIXEL_EACH(k, count)
                {
                        struct pixel_window out, in[2];
                        p32 x = (p32)next(), y = (p32)next();
                        p8 address_to into = pixel_place(&pixel_rooms[0], span, 0, k < 8, pixel_residue(c, k, 4));
                        p8 address_to target = pixel_place(&pixel_rooms[1], span, 0, k < 8, pixel_residue(c * 3 + 1, k, 4));
                        p8 address_to weight = pixel_place(&pixel_rooms[2], 2 * count, 0, k < 8, pixel_residue(c * 5 + 2, k, 2));
                        p8 address_to want = pixel_out(&out, 0, into, span, 0, PIXEL_WORDS);

                        pixel_in(&in[0], 0, 1, target, span, 0, PIXEL_WORDS);
                        pixel_in(&in[1], 1, 2, weight, 2 * count, 0, PIXEL_BYTES);
                        //  The weights 0 to 32767: the top bit of each cleared, here and in the copy kept of it.
                        for (positive at = 0; at < count; at++)
                        {
                                ((p16 address_to)weight)[at] &= 0x7fff;
                                ((p16 address_to)(pixel_kept[1] + (weight - in[1].from)))[at] &= 0x7fff;
                        }
                        reference_memory_toward_dither_u32((p32 address_to)want, (const p32 address_to)target, (const p16 address_to)weight, count, x, y);
                        PIXEL_CALL();
                        (memory_toward_dither_u32)((p32 address_to)into, (const p32 address_to)target, (const p16 address_to)weight, count, x, y);
                        pixel_after("memory_toward_dither_u32", &out, in, 2, count << 8 | k, x);
                }
        }
}

static fn pixel_ramp(void)
{
        for (positive c = 0; c < PIXEL_COUNTS; c++)
        {
                positive count = PIXEL_COUNT(c), span = 4 * count;

                PIXEL_EACH(k, count)
                {
                        struct pixel_window out, in[1];
                        p32 x = (p32)next(), y = (p32)next();
                        p8 address_to into = pixel_place(&pixel_rooms[0], span, 0, k < 8, pixel_residue(c, k, 4));
                        p8 address_to ramp = pixel_place(&pixel_rooms[1], 24, 0, k < 8, pixel_residue(c * 3 + 1, k, 4));
                        p8 address_to want = pixel_out(&out, 0, into, span, 0, PIXEL_WORDS);

                        pixel_in(&in[0], 0, 1, ramp, 24, 0, PIXEL_WORDS);
                        //  Half the cases a ramp a caller makes: colours inside a byte, steps under one a pixel.
                        if (k & 1)
                                for (positive at = 0; at < 6; at++)
                                {
                                        b32 value = at < 3 ? (b32)(next() % (256u << 16)) : (b32)(next() % 131072u) - 65536;

                                        ((b32 address_to)ramp)[at] = value, ((b32 address_to)(pixel_kept[0] + (ramp - in[0].from)))[at] = value;
                                }
                        reference_memory_ramp_dither_u32((p32 address_to)want, count, x, y, (const b32 address_to)ramp);
                        PIXEL_CALL();
                        (memory_ramp_dither_u32)((p32 address_to)into, count, x, y, (const b32 address_to)ramp);
                        pixel_after("memory_ramp_dither_u32", &out, in, 1, count << 8 | k, y);
                }
        }
}

static fn pixel_grain(void)
{
        for (positive c = 0; c < PIXEL_COUNTS; c++)
        {
                positive count = PIXEL_COUNT(c), span = 4 * count;

                PIXEL_EACH(k, count)
                {
                        struct pixel_window out, in[1];
                        p8 address_to into = pixel_place(&pixel_rooms[0], span, 0, k < 8, pixel_residue(c, k, 4));
                        p8 address_to add = pixel_place(&pixel_rooms[1], 2 * count, 0, k < 8, pixel_residue(c * 3 + 1, k, 2));
                        p8 address_to want = pixel_out(&out, 0, into, span, 0, PIXEL_WORDS);

                        pixel_in(&in[0], 0, 1, add, 2 * count, 0, PIXEL_BYTES);
                        reference_memory_grain_u32((p32 address_to)want, (const b16 address_to)add, count);
                        PIXEL_CALL();
                        (memory_grain_u32)((p32 address_to)into, (const b16 address_to)add, count);
                        pixel_after("memory_grain_u32", &out, in, 1, count << 8 | k, 0);
                }
        }
}

//      The alphas the chip's mix is handed: none, all, between, and past a byte (whose low byte is the alpha).
static const p32 pixel_alphas[] = {0, 1, 2, 127, 128, 180, 254, 255, 256, 257, 511, 0xffffff80u, 0xffffffffu};

static fn pixel_mix_const_one(positive shape, positive width, positive height, positive gi, positive gc, positive k,
                              p32 ink, p32 alpha)
{
        struct pixel_window out, in[1];
        positive is = width + gi, cs = width + gc;
        positive into_span = height ? 4 * ((height - 1) * is + width) : 0, cover_span = height ? (height - 1) * cs + width : 0;
        p8 address_to into = pixel_place(&pixel_rooms[0], into_span, 0, k < 8, pixel_residue(shape, k, 4));
        p8 address_to cover = pixel_place(&pixel_rooms[1], cover_span, 0, k < 8, pixel_residue(shape * 3 + 1, k, 1));
        p8 address_to want = pixel_out(&out, 0, into, into_span, 0, PIXEL_WORDS);

        pixel_in(&in[0], 0, 1, cover, cover_span, 0, PIXEL_COVERS);
        reference_memory_mix_const_u32_rect((p32 address_to)want, is, cover, cs, width, height, ink, alpha);
        PIXEL_CALL();
        (memory_mix_const_u32_rect)((p32 address_to)into, is, cover, cs, width, height, ink, alpha);
        pixel_after("memory_mix_const_u32_rect", &out, in, 1, PIXEL_SHAPE_CODE(width, height, gi, gc, k), (positive)ink << 32 | alpha);
}

static fn pixel_mix_const(void)
{
        for (positive shape = 0; shape < PIXEL_SHAPES; shape++)
        {
                positive width, height, gi, gc;

                if (!pixel_shape(shape, &width, &height, &gi, &gc))
                        continue;
                PIXEL_EACH(k, width)
                        pixel_mix_const_one(shape, width, height, gi, gc, k, pixel_word(next()),
                                            pixel_alphas[(shape + k) % array_count(pixel_alphas)]);
        }
        //  Every cover under every alpha a byte holds and the ones past it: a row of the 256 covers, twice over.
        for (positive alpha = 0; alpha < 512; alpha++)
        {
                struct pixel_window out;
                p8 address_to into = pixel_place(&pixel_rooms[0], 4 * 512, 0, true, 0);
                p8 address_to cover = pixel_place(&pixel_rooms[1], 512, 0, true, 0);
                p8 address_to want = pixel_out(&out, 0, into, 4 * 512, 0, PIXEL_WORDS);
                p32 ink = pixel_word(next());

                for (positive i = 0; i < 512; i++)
                        cover[i] = (p8)(i * 167);
                reference_memory_mix_const_u32_rect((p32 address_to)want, 256, cover, 256, 256, 2, ink, (p32)alpha);
                PIXEL_CALL();
                (memory_mix_const_u32_rect)((p32 address_to)into, 256, cover, 256, 256, 2, ink, (p32)alpha);
                pixel_after("memory_mix_const_u32_rect", &out, null, 0, 256, (positive)ink << 32 | alpha);
        }
}

static fn pixel_over_cover(void)
{
        for (positive shape = 0; shape < PIXEL_SHAPES; shape++)
        {
                positive width, height, gi, gc;

                if (!pixel_shape(shape, &width, &height, &gi, &gc))
                        continue;
                positive is = width + gi, cs = width + gc;
                positive into_span = height ? 4 * ((height - 1) * is + width) : 0, cover_span = height ? (height - 1) * cs + width : 0;

                PIXEL_EACH(k, width)
                {
                        struct pixel_window out, in[1];
                        p32 ink = pixel_word(next());
                        p8 address_to into = pixel_place(&pixel_rooms[0], into_span, 0, k < 8, pixel_residue(shape, k, 4));
                        p8 address_to cover = pixel_place(&pixel_rooms[1], cover_span, 0, k < 8, pixel_residue(shape * 3 + 1, k, 1));
                        p8 address_to want = pixel_out(&out, 0, into, into_span, 0, PIXEL_WORDS);

                        pixel_in(&in[0], 0, 1, cover, cover_span, 0, PIXEL_COVERS);
                        reference_memory_over_cover_u32_rect((p32 address_to)want, is, cover, cs, width, height, ink);
                        PIXEL_CALL();
                        (memory_over_cover_u32_rect)((p32 address_to)into, is, cover, cs, width, height, ink);
                        pixel_after("memory_over_cover_u32_rect", &out, in, 1, PIXEL_SHAPE_CODE(width, height, gi, gc, k), ink);
                }
        }
        //  Every cover in the inks that decide a tone (black, white, grey, each channel alone, and noise) over every kind of pixel.
        static const p32 inks[] = {0, 0xffffff, 0x808080, 0xff0000, 0x00ff00, 0x0000ff, 0xff000000u, 0x7f010203};
        for (positive i = 0; i < array_count(inks) + 24; i++)
        {
                struct pixel_window out;
                p32 ink = i < array_count(inks) ? inks[i] : pixel_word(next());
                p8 address_to into = pixel_place(&pixel_rooms[0], 4 * 256 * 4, 0, true, 0);
                p8 address_to cover = pixel_place(&pixel_rooms[1], 256 * 4, 0, true, 0);
                p8 address_to want = pixel_out(&out, 0, into, 4 * 256 * 4, 0, PIXEL_WORDS);

                for (positive at = 0; at < 256 * 4; at++)
                        cover[at] = (p8)(at * 167);
                reference_memory_over_cover_u32_rect((p32 address_to)want, 256, cover, 256, 256, 4, ink);
                PIXEL_CALL();
                (memory_over_cover_u32_rect)((p32 address_to)into, 256, cover, 256, 256, 4, ink);
                pixel_after("memory_over_cover_u32_rect", &out, null, 0, 256, ink);
        }
}

static fn pixel_over(void)
{
        for (positive shape = 0; shape < PIXEL_SHAPES; shape++)
        {
                positive width, height, gi, gf;

                if (!pixel_shape(shape, &width, &height, &gi, &gf))
                        continue;
                positive is = width + gi, fs = width + gf;
                positive into_span = height ? 4 * ((height - 1) * is + width) : 0, from_span = height ? 4 * ((height - 1) * fs + width) : 0;

                PIXEL_EACH(k, width)
                {
                        struct pixel_window out, in[1];
                        p8 address_to into = pixel_place(&pixel_rooms[0], into_span, 0, k < 8, pixel_residue(shape, k, 4));
                        p8 address_to from = pixel_place(&pixel_rooms[1], from_span, 0, k < 8, pixel_residue(shape * 3 + 1, k, 4));
                        p8 address_to want = pixel_out(&out, 0, into, into_span, 0, PIXEL_WORDS);

                        pixel_in(&in[0], 0, 1, from, from_span, 0, PIXEL_WORDS);
                        reference_memory_over_u32_rect((p32 address_to)want, is, (const p32 address_to)from, fs, width, height);
                        PIXEL_CALL();
                        (memory_over_u32_rect)((p32 address_to)into, is, (const p32 address_to)from, fs, width, height);
                        pixel_after("memory_over_u32_rect", &out, in, 1, PIXEL_SHAPE_CODE(width, height, gi, gf, k), 0);
                }
        }
        //  Every alpha over every byte below it: a row of 256 alphas whose colours are each alpha's every fraction.
        for (positive turn = 0; turn < 16; turn++)
        {
                struct pixel_window out, in[1];
                p8 address_to into = pixel_place(&pixel_rooms[0], 4 * 256 * 256, 0, true, 0);
                p8 address_to from = pixel_place(&pixel_rooms[1], 4 * 256 * 256, 0, true, 0);
                p8 address_to want = pixel_out(&out, 0, into, 4 * 256 * 256, 0, PIXEL_WORDS);
                p32 address_to over = (p32 address_to)from;

                pixel_in(&in[0], 0, 1, from, 4 * 256 * 256, 0, PIXEL_WORDS);
                for (positive at = 0; at < 256 * 256; at++)
                {
                        p32 a = (p32)(at >> 8), c = (p32)(at & 255) * (turn & 1 ? 255 : a) / 255;

                        over[at] = a << 24 | c << ((turn >> 1) % 3 * 8) | (p32)(next() & (turn & 2 ? 0 : 0xff)) << ((turn >> 1) % 3 * 8 + 8) % 24;
                }
                memory_copy(pixel_kept[0], in[0].from, in[0].size);
                reference_memory_over_u32_rect((p32 address_to)want, 256, over, 256, 256, 256);
                PIXEL_CALL();
                (memory_over_u32_rect)((p32 address_to)into, 256, over, 256, 256, 256);
                pixel_after("memory_over_u32_rect", &out, in, 1, 256, turn);
        }
}

/*
        memory_over_u32_rect's opaque path: a source of alpha 255 is stored as it is, a turn at a time where the whole
        turn is (four pixels on SSE2 and NEON, eight with AVX2 and then four, sixteen with AVX-512 and its masked tail,
        a strip of V) and one at a time in the rest of a row. Over every shape and place the other rows walk, the source
        made all opaque; opaque and not by turns of four; opaque but one pixel, at each place of a turn of sixteen in
        turn; an opaque run from every start to every tail inside a row of noise; runs of three, with every pixel between
        them alpha 0 and colour (premultiplied it cannot be, and it must answer as the reference does); and, for the
        wider turns, opaque and not by turns of eight and of sixteen, and opaque but one pixel at each place of a turn of
        sixty four, a pixel not opaque in these three made so for certain (its alpha's low bit cleared); and the word
        none, which a window's clear parts are, by turns of eight with opaque, and throughout, so a turn of it is a turn
        whose colours are all none (a test of the colours for the alpha's bits finds it opaque). That the target is not
        read on the opaque path is argued from the bodies (no page can be made writable and not readable on x86_64), not
        held here.
*/
static fn pixel_over_opaque(void)
{
        for (positive shape = 0; shape < PIXEL_SHAPES; shape++)
        {
                positive width, height, gi, gf;

                if (!pixel_shape(shape, &width, &height, &gi, &gf))
                        continue;
                positive is = width + gi, fs = width + gf;
                positive into_span = height ? 4 * ((height - 1) * is + width) : 0, from_span = height ? 4 * ((height - 1) * fs + width) : 0;

                PIXEL_EACH(k, width)
                        for (positive kind = 0; kind < 10; kind++)
                        {
                                struct pixel_window out, in[1];
                                p8 address_to into = pixel_place(&pixel_rooms[0], into_span, 0, k < 8, pixel_residue(shape + kind, k, 4));
                                p8 address_to from = pixel_place(&pixel_rooms[1], from_span, 0, k < 8, pixel_residue(shape * 3 + kind, k, 4));
                                p8 address_to want = pixel_out(&out, 0, into, into_span, 0, PIXEL_WORDS);
                                p32 address_to over = (p32 address_to)from;
                                positive lone = (shape + k) % 16, start = shape % 7, tail = k % 5, lone64 = (shape * 5 + k) % 64;

                                pixel_in(&in[0], 0, 1, from, from_span, 0, PIXEL_WORDS);
                                for (positive r = 0; r < height; r++)
                                        for (positive c = 0; c < width; c++)
                                        {
                                                positive i = r * width + c;
                                                p32 address_to at = over + r * fs + c;
                                                p8 opaque = kind == 0   ? true
                                                            : kind == 1 ? i / 4 % 2 == 0
                                                            : kind == 2 ? i % 16 != lone
                                                            : kind == 3 ? c >= start && c + tail < width
                                                            : kind == 4 ? i / 3 % 2 == 0
                                                            : kind == 5 ? i / 8 % 2 == 0
                                                            : kind == 6 ? i / 16 % 2 == 0
                                                            : kind == 7 ? i % 64 != lone64
                                                            : kind == 8 ? i / 8 % 2 == 0
                                                                        : false;

                                                if (opaque)
                                                        *at |= 0xff000000u;
                                                else if (kind == 4)
                                                        *at = (*at & 0x00ffffffu) | 1u;
                                                else if (kind >= 8)
                                                        *at = 0;
                                                else if (kind >= 5)
                                                        *at &= 0xfeffffffu;
                                        }
                                memory_copy(pixel_kept[0], in[0].from, in[0].size);
                                reference_memory_over_u32_rect((p32 address_to)want, is, (const p32 address_to)from, fs, width, height);
                                PIXEL_CALL();
                                (memory_over_u32_rect)((p32 address_to)into, is, (const p32 address_to)from, fs, width, height);
                                pixel_after("memory_over_u32_rect", &out, in, 1, PIXEL_SHAPE_CODE(width, height, gi, gf, k), kind);
                        }
        }
}

/*
        memory_over_under_u32_rect: what is under from read from under and the answer written to into, which is not read.
        Over every shape and place the other rows walk, under three ways: apart, in its own room at a stride of its own
        (the width and a gap of each pair, so a row of it is not the target's); the target itself at the target's stride,
        which is memory_over_u32_rect's reference; and a row of one colour at a stride of none, the ground, against the
        C the scene ran before it, the rectangle filled with that colour and the picture laid over it. The sources are
        the kinds that decide the blend and the opaque test: noise, all opaque, opaque and none by turns of eight, and
        opaque but one pixel at each place of a turn of sixteen. Then every alpha over every byte under it, as
        memory_over_u32_rect's rows have it, the under apart. memory_over_under_out_u32_rect walks the same, the under
        apart and the ground's row (its into is never the under), its streamed rows the widths of 256 words and more at
        every residue the places give the head to the line, and the sweep again with into a word to eight words off a
        line.
*/
static fn pixel_over_under(void)
{
        for (positive shape = 0; shape < PIXEL_SHAPES; shape++)
        {
                positive width, height, gi, gf;

                if (!pixel_shape(shape, &width, &height, &gi, &gf))
                        continue;
                positive is = width + gi, fs = width + gf;
                positive into_span = height ? 4 * ((height - 1) * is + width) : 0, from_span = height ? 4 * ((height - 1) * fs + width) : 0;

                PIXEL_EACH(k, width)
                        for (positive which = 0; which < 2; which++)
                        for (positive mode = 0; mode < 3; mode += which && mode == 0 ? 2 : 1)
                                for (positive kind = 0; kind < 4; kind++)
                                {
                                        struct pixel_window out, in[2];
                                        positive us = mode == 0 ? width + gf + gi + 1 : mode == 1 ? is : 0;
                                        positive under_span = !height ? 0 : mode == 2 ? 4 * width : 4 * ((height - 1) * us + width);
                                        p8 address_to into = pixel_place(&pixel_rooms[0], into_span, 0, k < 8, pixel_residue(shape + mode, k, 4));
                                        p8 address_to from = pixel_place(&pixel_rooms[1], from_span, 0, k < 8, pixel_residue(shape * 3 + kind, k, 4));
                                        p8 address_to under = mode == 1 ? into : pixel_place(&pixel_rooms[2], under_span, 0, k < 8, pixel_residue(shape * 5 + kind, k, 4));
                                        p8 address_to want = pixel_out(&out, 0, into, into_span, 0, PIXEL_WORDS);
                                        p32 address_to over = (p32 address_to)from;
                                        p32 ground = pixel_word(next());
                                        positive lone = (shape + k) % 16;

                                        pixel_in(&in[0], 0, 1, from, from_span, 0, PIXEL_WORDS);
                                        for (positive r = 0; r < height; r++)
                                                for (positive c = 0; c < width; c++)
                                                {
                                                        positive i = r * width + c;
                                                        p32 address_to at = over + r * fs + c;

                                                        if (kind == 1 || (kind == 2 && i / 8 % 2 == 0) || (kind == 3 && i % 16 != lone))
                                                                *at |= 0xff000000u;
                                                        else if (kind == 2)
                                                                *at = 0;
                                                        else if (kind == 3)
                                                                *at &= 0xfeffffffu;
                                                }
                                        memory_copy(pixel_kept[0], in[0].from, in[0].size);
                                        if (mode != 1)
                                        {
                                                pixel_in(&in[1], 1, 2, under, under_span, 0, PIXEL_WORDS);
                                                if (mode == 2)
                                                {
                                                        for (positive c = 0; height && c < width; c++)
                                                                ((p32 address_to)under)[c] = ground;
                                                        memory_copy(pixel_kept[1], in[1].from, in[1].size);
                                                }
                                        }
                                        if (mode == 0)
                                                reference_memory_over_under_u32_rect((p32 address_to)want, is, (const p32 address_to)from, fs,
                                                                                     (const p32 address_to)under, us, width, height);
                                        else if (mode == 1)
                                                reference_memory_over_u32_rect((p32 address_to)want, is, (const p32 address_to)from, fs, width, height);
                                        else
                                        {
                                                //  The scene's way before the routine: the rectangle filled with the ground, then the picture over it.
                                                for (positive r = 0; r < height; r++)
                                                        for (positive c = 0; c < width; c++)
                                                                ((p32 address_to)want)[r * is + c] = ground;
                                                reference_memory_over_u32_rect((p32 address_to)want, is, (const p32 address_to)from, fs, width, height);
                                        }
                                        PIXEL_CALL();
                                        (which ? memory_over_under_out_u32_rect : memory_over_under_u32_rect)(
                                            (p32 address_to)into, is, (const p32 address_to)from, fs, (const p32 address_to)under, us, width, height);
                                        pixel_after(which ? "memory_over_under_out_u32_rect" : "memory_over_under_u32_rect", &out, in, mode == 1 ? 1 : 2,
                                                    PIXEL_SHAPE_CODE(width, height, gi, gf, k), mode << 8 | kind);
                                }
        }
        //  Every alpha over every byte under it: a row of 256 alphas whose colours are each alpha's every fraction.
        for (positive turn = 0; turn < 16; turn++)
        {
                struct pixel_window out, in[2];
                p8 address_to into = pixel_place(&pixel_rooms[0], 4 * 256 * 256, 0, true, turn >= 8 ? 4 * (turn - 7) : 0);
                p8 address_to from = pixel_place(&pixel_rooms[1], 4 * 256 * 256, 0, true, 0);
                p8 address_to under = pixel_place(&pixel_rooms[2], 4 * 256 * 256, 0, true, 0);
                p8 address_to want = pixel_out(&out, 0, into, 4 * 256 * 256, 0, PIXEL_WORDS);
                p32 address_to over = (p32 address_to)from;

                pixel_in(&in[0], 0, 1, from, 4 * 256 * 256, 0, PIXEL_WORDS);
                pixel_in(&in[1], 1, 2, under, 4 * 256 * 256, 0, PIXEL_WORDS);
                for (positive at = 0; at < 256 * 256; at++)
                {
                        p32 a = (p32)(at >> 8), c = (p32)(at & 255) * (turn & 1 ? 255 : a) / 255;

                        over[at] = a << 24 | c << ((turn >> 1) % 3 * 8) | (p32)(next() & (turn & 2 ? 0 : 0xff)) << ((turn >> 1) % 3 * 8 + 8) % 24;
                }
                memory_copy(pixel_kept[0], in[0].from, in[0].size);
                reference_memory_over_under_u32_rect((p32 address_to)want, 256, over, 256, (const p32 address_to)under, 256, 256, 256);
                PIXEL_CALL();
                (turn >= 8 ? memory_over_under_out_u32_rect : memory_over_under_u32_rect)((p32 address_to)into, 256, over, 256,
                                                                                         (const p32 address_to)under, 256, 256, 256);
                pixel_after(turn >= 8 ? "memory_over_under_out_u32_rect" : "memory_over_under_u32_rect", &out, in, 2, 256, turn);
        }
}

static fn pixel_scale(void)
{
        static const p32 scales[] = {0, 1, 2, 127, 128, 180, 254, 255, 256, 257, 511, 0x80000000u, 0xffffffffu};

        for (positive c = 0; c < PIXEL_COUNTS; c++)
        {
                positive count = PIXEL_COUNT(c);

                PIXEL_EACH(k, count)
                {
                        struct pixel_window out, in[1];
                        p32 scale = k & 1 ? (p32)next() : scales[(c + k) % array_count(scales)];
                        p8 address_to into = pixel_place(&pixel_rooms[0], count, 0, k < 8, pixel_residue(c, k, 1));
                        p8 address_to from = pixel_place(&pixel_rooms[1], count, 0, k < 8, pixel_residue(c * 3 + 1, k, 1));
                        p8 address_to want = pixel_out(&out, 0, into, count, 0, PIXEL_BYTES);

                        pixel_in(&in[0], 0, 1, from, count, 0, PIXEL_COVERS);
                        reference_memory_scale_u8(want, from, count, scale);
                        PIXEL_CALL();
                        (memory_scale_u8)(into, from, count, scale);
                        pixel_after("memory_scale_u8", &out, in, 1, count << 8 | k, scale);
                }
        }
        //  Every byte at every scale a byte holds and the ones past it.
        for (positive scale = 0; scale < 512; scale++)
        {
                struct pixel_window out, in[1];
                p8 address_to into = pixel_place(&pixel_rooms[0], 256, 0, true, scale % 64);
                p8 address_to from = pixel_place(&pixel_rooms[1], 256, 0, true, scale * 7 % 64);
                p8 address_to want = pixel_out(&out, 0, into, 256, 0, PIXEL_BYTES);

                pixel_in(&in[0], 0, 1, from, 256, 0, PIXEL_BYTES);
                for (positive i = 0; i < 256; i++)
                        from[i] = (p8)i;
                memory_copy(pixel_kept[0], in[0].from, in[0].size);
                reference_memory_scale_u8(want, from, 256, (unsigned int)scale);
                PIXEL_CALL();
                (memory_scale_u8)(into, from, 256, (unsigned int)scale);
                pixel_after("memory_scale_u8", &out, in, 1, 256, scale);
        }
}

//      memory_or_u32 and memory_swizzle_rb_u32: the same shapes, apart and in place.
static const p32 pixel_masks[] = {0, 0xff000000u, 0xffffffffu, 0x00ff0000u, 0x000000ffu, 0x00ff00ffu, 0x80000001u};

static fn pixel_words_one(b32 swizzle, positive c, positive count, positive k, b32 in_place, p32 mask)
{
        string_address name = swizzle ? "memory_swizzle_rb_u32" : "memory_or_u32";
        struct pixel_window out, in[1];
        positive span = 4 * count;
        p8 address_to into = pixel_place(&pixel_rooms[0], span, 0, k < 8, pixel_residue(c + in_place, k, 4));
        p8 address_to from = in_place ? into : pixel_place(&pixel_rooms[1], span, 0, k < 8, pixel_residue(c * 3 + 1, k, 4));
        p8 address_to want = pixel_out(&out, 0, into, span, 0, PIXEL_WORDS);
        const p8 address_to source = in_place ? want : from;

        if (!in_place)
                pixel_in(&in[0], 0, 1, from, span, 0, PIXEL_WORDS);
        if (swizzle)
                reference_memory_swizzle_rb_u32((p32 address_to)want, (const p32 address_to)source, count, mask);
        else
                reference_memory_or_u32((p32 address_to)want, (const p32 address_to)source, count, mask);
        PIXEL_CALL();
        if (swizzle)
                (memory_swizzle_rb_u32)((p32 address_to)into, (const p32 address_to)from, count, mask);
        else
                (memory_or_u32)((p32 address_to)into, (const p32 address_to)from, count, mask);
        pixel_after(name, &out, in, in_place ? 0 : 1, count << 8 | k, (positive)in_place << 32 | mask);
}

static fn pixel_words(void)
{
        for (b32 swizzle = 0; swizzle < 2; swizzle++)
                for (positive c = 0; c < PIXEL_COUNTS; c++)
                {
                        positive count = PIXEL_COUNT(c);

                        PIXEL_EACH(k, count)
                                for (b32 in_place = 0; in_place < 2; in_place++)
                                        pixel_words_one(swizzle, c, count, k, in_place,
                                                        k == 4 ? (p32)next() : pixel_masks[(c + k + in_place) % array_count(pixel_masks)]);
                }
}

static fn pixel_transpose(void)
{
        static const positive sides[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                                         23, 24, 25, 31, 32, 33, 63, 64, 65};
        positive shape = 0;

        for (positive r = 0; r < array_count(sides); r++)
                for (positive c = 0; c < array_count(sides); c++)
                        for (positive kind = 0; kind < 8; kind++, shape++)
                        {
                                positive rows = sides[r], cols = sides[c], pitch = cols + (kind & 4 ? 5 : 0);
                                bipolar fr = kind & 1 ? -1 : 1, step = (bipolar)rows + (kind & 4 ? 3 : 0), fc = kind & 2 ? -step : step;
                                positive into_span = rows && cols ? (rows - 1) * pitch + cols : 0;
                                bipolar lowest = 0, highest = 0;

                                if (rows && cols)
                                {
                                        lowest = (fc < 0 ? (bipolar)(cols - 1) * fc : 0) + (fr < 0 ? -(bipolar)(rows - 1) : 0);
                                        highest = (fc > 0 ? (bipolar)(cols - 1) * fc : 0) + (fr > 0 ? (bipolar)(rows - 1) : 0);
                                }
                                positive from_span = rows && cols ? (positive)(highest - lowest + 1) : 0;

                                for (positive k = 0; k <= 8; k += 4)
                                {
                                        struct pixel_window out, in[1];
                                        positive residue = k == 4 ? (shape * 13) % 64 : 0, other = k == 4 ? (shape * 29 + 7) % 64 : 0;
                                        p8 address_to into = pixel_place(&pixel_rooms[0], into_span, 0, k < 8, residue);
                                        p8 address_to from = pixel_place(&pixel_rooms[1], from_span, lowest, k < 8, other);
                                        p8 address_to want = pixel_out(&out, 0, into, into_span, 0, PIXEL_BYTES);

                                        pixel_in(&in[0], 0, 1, from, from_span, lowest, PIXEL_BYTES);
                                        reference_memory_transpose_u8(want, pitch, from, fc, fr, rows, cols);
                                        PIXEL_CALL();
                                        (memory_transpose_u8)(into, pitch, from, fc, fr, rows, cols);
                                        pixel_after("memory_transpose_u8", &out, in, 1, rows << 24 | cols << 8 | kind, k);
                                }
                        }
}

/*
        The scan. Pairs of bytes as the lexer hands them (a quote and a
        backslash), the two the same, either nought (which is the end's own
        question), and bytes with the high bit set beside the ones without
        it. Strings of bytes that are neither, with the first match at every
        place in every string to eighty, then every length to three hundred
        and a few long at the nine places: the terminator the last byte
        before a page that cannot be read and at every distance short of it,
        the bytes past the terminator the two bytes again and again (so a
        body that read past the end and believed it answers wrong), the near
        misses a word-at-a-time test is likeliest to take for a match (each
        byte with its high bit or its low bit turned, one more, 0x01, 0x80,
        0x7f and 0xff) and matches after the first.
*/
static const p8 either_pairs[][2] = {
    {'"', '\\'}, {'\'', '\\'}, {'\\', '"'}, {'"', '"'}, {0, '\\'}, {'\'', 0}, {0, 0},
    {0xe9, 0x5c}, {0x69, 0xe9}, {0x80, 0xff}, {0xff, 0x7f}, {0x01, 0x02}, {0xa2, 0x22},
};

static p8 either_other(p8 first, p8 second, positive v)
{
        p8 near[] = {first ^ 0x80, second ^ 0x80, first ^ 1, second ^ 1, (p8)(first + 1), (p8)(second - 1), 0x01, 0x80, 0x7f, 0xff};

        for (;; v = next())
        {
                p8 byte = v & 1 ? near[(v >> 1) % array_count(near)] : (p8)(v >> 8);

                if (byte && byte != first && byte != second)
                        return byte;
        }
}

//      A string of length bytes at at: neither byte before place, a match at place (when it is inside), and after it
//      noise with more matches; the terminator; then the two bytes over and again to the end of the window.
static fn either_lay(p8 address_to at, positive length, positive place, p8 first, p8 second, positive tail, positive kind)
{
        for (positive i = 0; i < length; i++)
                at[i] = either_other(first, second, next());
        if (place < length && (first || second))
                at[place] = kind & 1 ? (second ? second : first) : (first ? first : second);
        for (positive i = place + 1; i < length; i++)
                if (next() % 5 == 0)
                        at[i] = next() & 1 ? first : second, at[i] = at[i] ? at[i] : 'x';
        at[length] = 0;
        for (positive i = 1; i <= tail; i++)
                at[length + i] = i & 1 ? (first ? first : 'q') : (second ? second : 'r');
}

static fn either_one(positive length, positive place, positive pair, positive k, positive residue, positive kind)
{
        p8 first = either_pairs[pair][0], second = either_pairs[pair][1];
        const struct pixel_room address_to room = &pixel_rooms[0];
        p8 address_to at = k < 8 ? room->high - (length + 1) - residue : room->low + residue;
        positive tail = (positive)(room->high - at) - (length + 1);

        either_lay(at, length, place, first, second, tail < 64 ? tail : 64, kind);
        string_address want = reference_string_first_of_either_or_end((string_address)at, first, second);

        PIXEL_CALL();
        string_address got = (string_first_of_either_or_end)((string_address)at, first, second);
        pixel_answer("string_first_of_either_or_end", "the first of either byte or the end", (positive)(got - (string_address)at),
                     (positive)(want - (string_address)at), length << 16 | place, pair << 16 | k << 8 | residue);
}

static fn pixel_either(void)
{
        //  The first match at every place in every string to eighty, and nowhere.
        for (positive length = 0; length <= 80; length++)
                for (positive place = 0; place <= length; place++)
                        for (positive kind = 0; kind < 2; kind++)
                                either_one(length, place, (length + place + kind) % array_count(either_pairs), 0,
                                           (length * 3 + place) % 64, kind);
        //  Every length to three hundred and a few long, at the nine places, with every pair.
        static const positive longer[] = {511, 512, 513, 1000, 4095, 4096, 4097, 65536};
        for (positive l = 0; l < 301 + array_count(longer); l++)
        {
                positive length = l < 301 ? l : longer[l - 301];

                for (positive k = 0; k <= 8; k++)
                        for (positive pair = 0; pair < array_count(either_pairs); pair++)
                        {
                                positive kind = (l + k + pair) % 4, place = kind < 2 ? length : next() % (length + 1);

                                either_one(length, place, pair, k, pixel_residue(l * 13 + pair, k, 1), kind);
                        }
        }
}

/*
        The registers a caller keeps across a call (rbx, rbp and r12 to r15
        on x86-64; x19 to x29 and d8 to d15 on arm64; s0 to s11 and fs0 to
        fs11 on riscv64), each loaded with a value of its own before a call
        of one of the eleven and read back after it: pixel_saved_call answers
        nonzero when one came back changed. A body that spends one and gives
        it back wrong corrupts whoever called it -- the riscv64 chip mix
        spent s0 to s2 unsaved until 59ebded -- and a check whose own loops
        happen not to keep anything there would never notice.
*/
extern positive pixel_saved_call(address_any routine, const positive address_to arguments) __asm__("pixel_saved_call")
        __attribute__((visibility("hidden")));

#if X64
__asm__(".pushsection .text\n   .balign 16\n   .local pixel_saved_call\n   .type pixel_saved_call, @function\n"
        "pixel_saved_call:\n"
        "push %rbx\n   push %rbp\n   push %r12\n   push %r13\n   push %r14\n   push %r15\n"
        "mov %rdi, %rax\n   mov %rsi, %r11\n   sub $8, %rsp\n   pushq 56(%r11)\n   pushq 48(%r11)\n"
        "mov (%r11), %rdi\n   mov 8(%r11), %rsi\n   mov 16(%r11), %rdx\n   mov 24(%r11), %rcx\n   mov 32(%r11), %r8\n   mov 40(%r11), %r9\n"
        "movabs $0x5b1f3c9d2e4a6b71, %rbx\n   lea 1(%rbx), %rbp\n   lea 2(%rbx), %r12\n   lea 3(%rbx), %r13\n   lea 4(%rbx), %r14\n   lea 5(%rbx), %r15\n"
        "call *%rax\n   add $24, %rsp\n"
        "movabs $0x5b1f3c9d2e4a6b71, %rcx\n   mov %rbx, %rax\n   xor %rcx, %rax\n"
        "inc %rcx\n   mov %rbp, %rdx\n   xor %rcx, %rdx\n   or %rdx, %rax\n"
        "inc %rcx\n   mov %r12, %rdx\n   xor %rcx, %rdx\n   or %rdx, %rax\n"
        "inc %rcx\n   mov %r13, %rdx\n   xor %rcx, %rdx\n   or %rdx, %rax\n"
        "inc %rcx\n   mov %r14, %rdx\n   xor %rcx, %rdx\n   or %rdx, %rax\n"
        "inc %rcx\n   mov %r15, %rdx\n   xor %rcx, %rdx\n   or %rdx, %rax\n"
        "pop %r15\n   pop %r14\n   pop %r13\n   pop %r12\n   pop %rbp\n   pop %rbx\n   ret\n"
        ".size pixel_saved_call, .-pixel_saved_call\n   .popsection\n");
#elif ARM64
__asm__(".pushsection .text\n   .balign 16\n   .local pixel_saved_call\n   .type pixel_saved_call, %function\n"
        "pixel_saved_call:\n"
        "stp x29, x30, [sp, #-160]!\n   mov x29, sp\n"
        "stp x19, x20, [sp, #16]\n   stp x21, x22, [sp, #32]\n   stp x23, x24, [sp, #48]\n   stp x25, x26, [sp, #64]\n   stp x27, x28, [sp, #80]\n"
        "stp d8, d9, [sp, #96]\n   stp d10, d11, [sp, #112]\n   stp d12, d13, [sp, #128]\n   stp d14, d15, [sp, #144]\n"
        "mov x16, x0\n   mov x17, x1\n"
        "ldp x0, x1, [x17]\n   ldp x2, x3, [x17, #16]\n   ldp x4, x5, [x17, #32]\n   ldp x6, x7, [x17, #48]\n"
        "mov x19, #0x6b71\n   movk x19, #0x2e4a, lsl #16\n   movk x19, #0x3c9d, lsl #32\n   movk x19, #0x5b1f, lsl #48\n"
        "add x20, x19, #1\n   add x21, x19, #2\n   add x22, x19, #3\n   add x23, x19, #4\n   add x24, x19, #5\n"
        "add x25, x19, #6\n   add x26, x19, #7\n   add x27, x19, #8\n   add x28, x19, #9\n"
        "add x9, x19, #10\n   fmov d8, x9\n   add x9, x19, #11\n   fmov d9, x9\n   add x9, x19, #12\n   fmov d10, x9\n"
        "add x9, x19, #13\n   fmov d11, x9\n   add x9, x19, #14\n   fmov d12, x9\n   add x9, x19, #15\n   fmov d13, x9\n"
        "add x9, x19, #16\n   fmov d14, x9\n   add x9, x19, #17\n   fmov d15, x9\n"
        "blr x16\n"
        "mov x9, #0x6b71\n   movk x9, #0x2e4a, lsl #16\n   movk x9, #0x3c9d, lsl #32\n   movk x9, #0x5b1f, lsl #48\n"
        "eor x0, x19, x9\n"
        "add x10, x9, #1\n   eor x11, x20, x10\n   orr x0, x0, x11\n   add x10, x9, #2\n   eor x11, x21, x10\n   orr x0, x0, x11\n"
        "add x10, x9, #3\n   eor x11, x22, x10\n   orr x0, x0, x11\n   add x10, x9, #4\n   eor x11, x23, x10\n   orr x0, x0, x11\n"
        "add x10, x9, #5\n   eor x11, x24, x10\n   orr x0, x0, x11\n   add x10, x9, #6\n   eor x11, x25, x10\n   orr x0, x0, x11\n"
        "add x10, x9, #7\n   eor x11, x26, x10\n   orr x0, x0, x11\n   add x10, x9, #8\n   eor x11, x27, x10\n   orr x0, x0, x11\n"
        "add x10, x9, #9\n   eor x11, x28, x10\n   orr x0, x0, x11\n"
        "fmov x11, d8\n   add x10, x9, #10\n   eor x11, x11, x10\n   orr x0, x0, x11\n"
        "fmov x11, d9\n   add x10, x9, #11\n   eor x11, x11, x10\n   orr x0, x0, x11\n"
        "fmov x11, d10\n   add x10, x9, #12\n   eor x11, x11, x10\n   orr x0, x0, x11\n"
        "fmov x11, d11\n   add x10, x9, #13\n   eor x11, x11, x10\n   orr x0, x0, x11\n"
        "fmov x11, d12\n   add x10, x9, #14\n   eor x11, x11, x10\n   orr x0, x0, x11\n"
        "fmov x11, d13\n   add x10, x9, #15\n   eor x11, x11, x10\n   orr x0, x0, x11\n"
        "fmov x11, d14\n   add x10, x9, #16\n   eor x11, x11, x10\n   orr x0, x0, x11\n"
        "fmov x11, d15\n   add x10, x9, #17\n   eor x11, x11, x10\n   orr x0, x0, x11\n"
        "mov x11, sp\n   eor x11, x11, x29\n   orr x0, x0, x11\n"
        "ldp d14, d15, [sp, #144]\n   ldp d12, d13, [sp, #128]\n   ldp d10, d11, [sp, #112]\n   ldp d8, d9, [sp, #96]\n"
        "ldp x27, x28, [sp, #80]\n   ldp x25, x26, [sp, #64]\n   ldp x23, x24, [sp, #48]\n   ldp x21, x22, [sp, #32]\n   ldp x19, x20, [sp, #16]\n"
        "ldp x29, x30, [sp], #160\n   ret\n"
        ".size pixel_saved_call, .-pixel_saved_call\n   .popsection\n");
#elif RISCV64
#define PIXEL_SAVED_RV(n) "addi t3, t2, " #n "\n   xor t4, s" #n ", t3\n   or a0, a0, t4\n"
#define PIXEL_SAVED_RV_F(n) "fmv.x.d t4, fs" #n "\n   addi t3, t2, 12 + " #n "\n   xor t4, t4, t3\n   or a0, a0, t4\n"
__asm__(".pushsection .text\n   .balign 16\n   .local pixel_saved_call\n   .type pixel_saved_call, @function\n"
        "pixel_saved_call:\n"
        "addi sp, sp, -208\n   sd ra, 0(sp)\n"
        "sd s0, 8(sp)\n   sd s1, 16(sp)\n   sd s2, 24(sp)\n   sd s3, 32(sp)\n   sd s4, 40(sp)\n   sd s5, 48(sp)\n"
        "sd s6, 56(sp)\n   sd s7, 64(sp)\n   sd s8, 72(sp)\n   sd s9, 80(sp)\n   sd s10, 88(sp)\n   sd s11, 96(sp)\n"
        "fsd fs0, 104(sp)\n   fsd fs1, 112(sp)\n   fsd fs2, 120(sp)\n   fsd fs3, 128(sp)\n   fsd fs4, 136(sp)\n   fsd fs5, 144(sp)\n"
        "fsd fs6, 152(sp)\n   fsd fs7, 160(sp)\n   fsd fs8, 168(sp)\n   fsd fs9, 176(sp)\n   fsd fs10, 184(sp)\n   fsd fs11, 192(sp)\n"
        "mv t0, a0\n   mv t1, a1\n"
        "ld a0, 0(t1)\n   ld a1, 8(t1)\n   ld a2, 16(t1)\n   ld a3, 24(t1)\n   ld a4, 32(t1)\n   ld a5, 40(t1)\n   ld a6, 48(t1)\n   ld a7, 56(t1)\n"
        "li s0, 0x5b1f3c9d2e4a6b71\n   addi s1, s0, 1\n   addi s2, s0, 2\n   addi s3, s0, 3\n   addi s4, s0, 4\n   addi s5, s0, 5\n"
        "addi s6, s0, 6\n   addi s7, s0, 7\n   addi s8, s0, 8\n   addi s9, s0, 9\n   addi s10, s0, 10\n   addi s11, s0, 11\n"
        "addi t2, s0, 12\n   fmv.d.x fs0, t2\n   addi t2, s0, 13\n   fmv.d.x fs1, t2\n   addi t2, s0, 14\n   fmv.d.x fs2, t2\n"
        "addi t2, s0, 15\n   fmv.d.x fs3, t2\n   addi t2, s0, 16\n   fmv.d.x fs4, t2\n   addi t2, s0, 17\n   fmv.d.x fs5, t2\n"
        "addi t2, s0, 18\n   fmv.d.x fs6, t2\n   addi t2, s0, 19\n   fmv.d.x fs7, t2\n   addi t2, s0, 20\n   fmv.d.x fs8, t2\n"
        "addi t2, s0, 21\n   fmv.d.x fs9, t2\n   addi t2, s0, 22\n   fmv.d.x fs10, t2\n   addi t2, s0, 23\n   fmv.d.x fs11, t2\n"
        "jalr t0\n"
        "li t2, 0x5b1f3c9d2e4a6b71\n   xor a0, s0, t2\n"
        PIXEL_SAVED_RV(1) PIXEL_SAVED_RV(2) PIXEL_SAVED_RV(3) PIXEL_SAVED_RV(4) PIXEL_SAVED_RV(5) PIXEL_SAVED_RV(6)
        PIXEL_SAVED_RV(7) PIXEL_SAVED_RV(8) PIXEL_SAVED_RV(9) PIXEL_SAVED_RV(10) PIXEL_SAVED_RV(11)
        PIXEL_SAVED_RV_F(0) PIXEL_SAVED_RV_F(1) PIXEL_SAVED_RV_F(2) PIXEL_SAVED_RV_F(3) PIXEL_SAVED_RV_F(4) PIXEL_SAVED_RV_F(5)
        PIXEL_SAVED_RV_F(6) PIXEL_SAVED_RV_F(7) PIXEL_SAVED_RV_F(8) PIXEL_SAVED_RV_F(9) PIXEL_SAVED_RV_F(10) PIXEL_SAVED_RV_F(11)
        "ld ra, 0(sp)\n"
        "ld s0, 8(sp)\n   ld s1, 16(sp)\n   ld s2, 24(sp)\n   ld s3, 32(sp)\n   ld s4, 40(sp)\n   ld s5, 48(sp)\n"
        "ld s6, 56(sp)\n   ld s7, 64(sp)\n   ld s8, 72(sp)\n   ld s9, 80(sp)\n   ld s10, 88(sp)\n   ld s11, 96(sp)\n"
        "fld fs0, 104(sp)\n   fld fs1, 112(sp)\n   fld fs2, 120(sp)\n   fld fs3, 128(sp)\n   fld fs4, 136(sp)\n   fld fs5, 144(sp)\n"
        "fld fs6, 152(sp)\n   fld fs7, 160(sp)\n   fld fs8, 168(sp)\n   fld fs9, 176(sp)\n   fld fs10, 184(sp)\n   fld fs11, 192(sp)\n"
        "addi sp, sp, 208\n   ret\n"
        ".size pixel_saved_call, .-pixel_saved_call\n   .popsection\n");
#undef PIXEL_SAVED_RV
#undef PIXEL_SAVED_RV_F
#endif

//      Each of the eleven called on shapes that reach its wide turns, its tails and more than one row, through
//      pixel_saved_call: one row a routine, a tier at a time.
static fn pixel_saved(void)
{
        static const positive widths[] = {1, 3, 4, 5, 8, 9, 17, 37, 64, 300};
        static const string_address names[] = {
            "memory_add_sat_u8_rect", "memory_copy_rect_reverse_u32", "memory_lerp_u32_row", "memory_mix_const_u32_rect",
            "memory_or_u32", "memory_over_cover_u32_rect", "memory_over_u32_rect", "memory_scale_u8",
            "memory_swizzle_rb_u32", "memory_transpose_u8", "string_first_of_either_or_end", "memory_over_under_u32_rect",
            "memory_over_under_out_u32_rect", "noise_value_q16", "memory_toward_dither_u32", "memory_ramp_dither_u32",
            "memory_grain_u32"};
        address_any const routines[] = {
            (address_any)memory_add_sat_u8_rect, (address_any)memory_copy_rect_reverse_u32, (address_any)memory_lerp_u32_row,
            (address_any)memory_mix_const_u32_rect, (address_any)memory_or_u32, (address_any)memory_over_cover_u32_rect,
            (address_any)memory_over_u32_rect, (address_any)memory_scale_u8, (address_any)memory_swizzle_rb_u32,
            (address_any)memory_transpose_u8, (address_any)string_first_of_either_or_end, (address_any)memory_over_under_u32_rect,
            (address_any)memory_over_under_out_u32_rect, (address_any)noise_value_q16, (address_any)memory_toward_dither_u32,
            (address_any)memory_ramp_dither_u32, (address_any)memory_grain_u32};
        positive changed[array_count(names)] = {0}, calls[array_count(names)] = {0};
        positive a = (positive)pixel_rooms[0].low, b = (positive)pixel_rooms[1].low, c = (positive)pixel_rooms[2].low;
        p8 address_to text = pixel_rooms[3].low;

        pixel_fill(pixel_rooms[0].low, 65536, PIXEL_WORDS);
        pixel_fill(pixel_rooms[1].low, 65536, PIXEL_WORDS);
        pixel_fill(pixel_rooms[2].low, 65536, PIXEL_COVERS);
        for (positive i = 0; i < 1000; i++)
                text[i] = (p8)('a' + i % 26);
        text[1000] = 0;
        for (positive w = 0; w < array_count(widths); w++)
                for (positive turn = 0; turn < 2; turn++)
                {
                        positive n = widths[w], rows = n > 64 ? 1 : 3, s = n + 3, ends = turn ? 0x7f : 0xff;
                        const positive arguments[array_count(names)][8] = {
                            {a, s, b, s, n, rows},
                            {a, s, b, s, n, rows},
                            {a, b, c, n * rows, 100 + turn},
                            {a, s, c, s, n, rows, 0x00123456u, 200 + turn * 55},
                            {a, b, n * rows, ends << 24},
                            {a, s, c, s, n, rows, 0xff336699u},
                            {a, s, b, s, n, rows},
                            {a, b, n * rows, 180 - turn},
                            {a, b, n * rows, ends << 24},
                            {a, n, b, rows + 3, turn ? (positive)-1 : 1, rows, n},
                            {(positive)text + n, turn ? '"' : '\\', '\''},
                            {a, s, b, s, c, turn ? 0 : s, n, rows},
                            {a, s, b, s, c, turn ? 0 : s, n, rows},
                            {a, b, c, b, n * rows, 405 + turn},
                            {a, b, c, n * rows, 5 + turn, 9},
                            {a, n * rows, 3, 4 + turn, b},
                            {a, c, n * rows},
                        };

                        for (positive r = 0; r < array_count(names); r++)
                        {
                                positive given[8];

                                for (positive k = 0; k < 8; k++)
                                        given[k] = arguments[r][k];
                                //  The transpose's from_row of -1 starts its rows at the last.
                                if (r == 9 && turn)
                                        given[2] += rows - 1;
                                PIXEL_CALL();
                                changed[r] += pixel_saved_call(routines[r], given) != 0;
                                calls[r]++;
                        }
                }
        for (positive r = 0; r < array_count(names); r++)
        {
                same(names[r], "the registers a caller keeps, kept across the call", changed[r], 0);
                same(names[r], "called through the register check", calls[r], 2 * array_count(widths));
        }
}

/*
        The rows, on every body this machine has. A tier the machine does not
        have is said, so a pass on it is not read as one.
*/
static fn pixel_each_tier(fn (*each)(void))
{
#if X64
        p8 avx2 = cpu_has_avx2, avx512 = cpu_has_avx512;

        for (pixel_tier = 0; pixel_tier < 3; pixel_tier++)
        {
                cpu_has_avx512 = pixel_tier < 1 ? avx512 : 0;
                cpu_has_avx2 = pixel_tier < 2 ? avx2 : 0;
                each();
        }
        cpu_has_avx2 = avx2, cpu_has_avx512 = avx512;
#elif RISCV64
        p8 vector = cpu_has_vector;

        for (pixel_tier = 0; pixel_tier < 2; pixel_tier++)
        {
                cpu_has_vector = pixel_tier ? 1 : 0;
                each();
        }
        cpu_has_vector = vector;
#else
        pixel_tier = 0;
        each();
#endif
}

fn check_pixel_rows(void)
{
        positive had = checks;

        pixel_shown = 0;
        if (!pixel_rooms_open())
        {
                pixel_rooms_close();
                return;
        }
#if X64
        if (!cpu_has_avx512 || !cpu_has_avx2)
                string_format(log, "  NOTE pixel rows: this machine has %s, so a tier above it ran the narrower body (NOT RUN)\n",
                              cpu_has_avx2 ? "AVX2 without AVX-512" : "neither AVX2 nor AVX-512");
#elif RISCV64
        {
                p8 vector = cpu_has_vector;

                cpu_has_vector = 0;
                (memory_or_u32)((p32 address_to)pixel_mirror, (const p32 address_to)pixel_mirror, 8, 0);
                if (cpu_has_vector != 2)
                        string_format(log, "  NOTE pixel rows: no V here, so the V tier ran the scalar bodies (NOT RUN)\n");
                cpu_has_vector = vector;
        }
#endif
        //  Each routine's cases say where its first eight disagreements are, and how many there were.
#define PIXEL_ROUTINE(each)                                                                                             \
        do                                                                                                              \
        {                                                                                                               \
                positive was = failures, asked = checks;                                                               \
                                                                                                                        \
                pixel_shown = 0;                                                                                        \
                pixel_each_tier(each);                                                                                  \
                if (failures != was)                                                                                    \
                        string_format(log, "  %s: %p of %p cases disagreed\n", #each, failures - was, checks - asked); \
                log_flush();                                                                                            \
        } while (0)
        //  The registers first: a body that gives a caller's back wrong can leave the loops after it unable to end.
        PIXEL_ROUTINE(pixel_saved);
        PIXEL_ROUTINE(pixel_add_sat);
        PIXEL_ROUTINE(pixel_reverse);
        PIXEL_ROUTINE(pixel_lerp);
        PIXEL_ROUTINE(pixel_mix_const);
        PIXEL_ROUTINE(pixel_words);
        PIXEL_ROUTINE(pixel_over_cover);
        PIXEL_ROUTINE(pixel_over);
        PIXEL_ROUTINE(pixel_over_opaque);
        PIXEL_ROUTINE(pixel_over_under);
        PIXEL_ROUTINE(pixel_scale);
        PIXEL_ROUTINE(pixel_transpose);
        PIXEL_ROUTINE(pixel_either);
        PIXEL_ROUTINE(pixel_noise);
        PIXEL_ROUTINE(pixel_toward);
        PIXEL_ROUTINE(pixel_ramp);
        PIXEL_ROUTINE(pixel_grain);
#undef PIXEL_ROUTINE
        pixel_rooms_close();
        string_format(log, "  NOTE pixel rows: %p cases\n", checks - had);
}

/*
        wire_message, wire_slot and rects_clip against the C they stand for
        (the Wayland shim's own, src/canvas/wayland.c, as it read a head,
        looked an id up and cut a commit's damage before they were these).

        wire_message: every count of bytes there from none to twelve and a
        few more, every size a head can say at and one either side of each
        bound (eight, the most, a multiple of four, what is there), four
        bounds, the bytes ending at a page that cannot be read (a head read
        before eight bytes are there, or a byte past the eighth, faults) and
        the head's two words ending at a page that cannot be written.
        wire_slot: an index that ends at a page that cannot be read, every
        cell of it, the first past it and far past. rects_clip: rectangles
        on, across and off every edge, empty, negative and the whole, in
        lists that end at a page that cannot be written, cut where they lie.
*/
static positive reference_wire_message(const p8 address_to bytes, positive size, positive most, p32 address_to head)
{
        positive said;

        if (size < 8)
                return 0;
        said = (positive)bytes[6] | (positive)bytes[7] << 8;
        if (said < 8 || said > most || (said & 3))
                return ~(positive)0;
        if (said > size)
                return 0;
        head[0] = (p32)bytes[0] | (p32)bytes[1] << 8 | (p32)bytes[2] << 16 | (p32)bytes[3] << 24;
        head[1] = (p32)bytes[4] | (p32)bytes[5] << 8;
        return said;
}

static bipolar reference_wire_slot(const p16 address_to index, positive count, positive at)
{
        return at < count ? (bipolar)index[at] - 1 : -1;
}

static positive reference_rects_clip(b64 address_to rects, positive count, positive width, positive height)
{
        positive kept = 0, whole = 0;

        for (positive at = 0; at < count; at++)
        {
                b64 x = rects[at * 4], y = rects[at * 4 + 1], x2 = x + rects[at * 4 + 2], y2 = y + rects[at * 4 + 3];

                x2 = x2 > (b64)width ? (b64)width : x2;
                y2 = y2 > (b64)height ? (b64)height : y2;
                x = x < 0 ? 0 : x;
                y = y < 0 ? 0 : y;
                if (x2 <= x || y2 <= y)
                        continue;
                rects[kept * 4] = x;
                rects[kept * 4 + 1] = y;
                rects[kept * 4 + 2] = x2 - x;
                rects[kept * 4 + 3] = y2 - y;
                whole |= !x && !y && x2 == (b64)width && y2 == (b64)height;
                kept++;
        }
        return kept | whole << 63;
}

fn check_wire()
{
        static const positive mosts[] = {8, 12, 4096, 65535};
        static const positive saids[] = {0, 1, 4, 7, 8, 9, 10, 11, 12, 13, 15, 16, 20, 4092, 4095, 4096, 4097, 4100, 65532, 65535};
        static const positive theres[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 20, 4092, 4095, 4096};
        static const positive counts[] = {0, 1, 2, 3, 7, 8, 64, 2048};
        static const b64 edges[] = {-1000000000, -65, -1, 0, 1, 31, 32, 33, 63, 64, 65, 1000000000};
        p8 address_to pages = memory(3 * 4096);
        p8 address_to heads = memory(2 * 4096);
        p8 address_to cells = memory(3 * 4096);
        p8 address_to lists = memory(3 * 4096);
        p8 mapped = (bipolar)(positive)pages > 0 && (bipolar)(positive)heads > 0 && (bipolar)(positive)cells > 0 && (bipolar)(positive)lists > 0;
        p64 seed = 0x2545f4914f6cdd1dull;
        positive agreed = 1, heads_agreed = 1, sizes = 0, refused = 0, waited = 0, runs = 0;

        same("wire_message", "guard mappings", mapped, 1);
        if (!mapped)
                return;
        p8 protected = system_call_3(syscall(mprotect), (positive)(pages + 8192), 4096, 0) == 0 &&
                       system_call_3(syscall(mprotect), (positive)(heads + 4096), 4096, 0) == 0 &&
                       system_call_3(syscall(mprotect), (positive)(cells + 8192), 4096, 0) == 0 &&
                       system_call_3(syscall(mprotect), (positive)(lists + 8192), 4096, 0) == 0;
        same("wire_message", "guard pages protected", protected, 1);
        if (!protected)
                return;
        p32 address_to head = (p32 address_to)(heads + 4096 - 8);

#define WIRE_NEXT() (seed ^= seed << 13, seed ^= seed >> 7, seed ^= seed << 17, seed)
        for (positive m = 0; m < array_count(mosts); m++)
                for (positive t = 0; t < array_count(theres); t++)
                        for (positive s = 0; s < array_count(saids); s++)
                                for (positive odd = 0; odd < 4; odd++)
                                {
                                        //  The bytes end at the guard; odd moves the whole of them off a word's boundary, with what is there one less.
                                        positive there = theres[t] > odd ? theres[t] - odd : 0, said = saids[s];
                                        p8 address_to bytes = pages + 8192 - there;
                                        p32 want_head[2] = {0x5a5a5a5a, 0xa5a5a5a5};

                                        for (positive at = 0; at < there; at++)
                                                bytes[at] = (p8)WIRE_NEXT();
                                        if (there >= 8)
                                                bytes[6] = (p8)said, bytes[7] = (p8)(said >> 8);
                                        head[0] = 0x5a5a5a5a, head[1] = 0xa5a5a5a5;
                                        positive want = reference_wire_message(bytes, there, mosts[m], want_head);
                                        positive got = (wire_message)(bytes, there, mosts[m], head);

                                        agreed &= got == want;
                                        heads_agreed &= head[0] == want_head[0] && head[1] == want_head[1];
                                        runs++;
                                        sizes += want != 0 && want != ~(positive)0;
                                        refused += want == ~(positive)0;
                                        waited += want == 0;
                                }
        same("wire_message", "the size, none yet, or no such size, every count there and every size said", agreed, 1);
        same("wire_message", "the head is the object and the opcode when a size is answered, and untouched when not", heads_agreed, 1);
        //  The lists reach all three answers, or the rows above prove nothing.
        same("wire_message", "sizes answered, sizes refused and heads waited for", sizes > 200 && refused > 500 && waited > 500, 1);
        same("wire_message", "heads compared", runs > 6000, 1);
        head[0] = 1, head[1] = 2;
        same("wire_message", "seven bytes there reads none of them", (wire_message)(pages + 8192, 7, 4096, head), 0);
        same("wire_message", "and writes no head", head[0] == 1 && head[1] == 2, 1);

        {
                positive slots = 1, kept = 1;

                for (positive c = 0; c < array_count(counts); c++)
                {
                        positive count = counts[c];
                        p16 address_to index = (p16 address_to)(cells + 8192 - count * 2);

                        for (positive at = 0; at < count; at++)
                                index[at] = (p16)(WIRE_NEXT() % 5 == 0 ? 0 : WIRE_NEXT());
                        if (count > 2)
                                index[0] = 0, index[count - 1] = 65535, index[1] = 1;
                        for (positive at = 0; at < count + 3; at++)
                                slots &= (wire_slot)(index, count, at) == reference_wire_slot(index, count, at);
                        slots &= (wire_slot)(index, count, ~(positive)0) == -1 && (wire_slot)(index, count, (positive)1 << 63) == -1 &&
                                 (wire_slot)(index, count, (positive)1 << 32) == -1;
                        if (count > 2)
                                kept &= (wire_slot)(index, count, 0) == -1 && (wire_slot)(index, count, count - 1) == 65534 && (wire_slot)(index, count, 1) == 0;
                }
                same("wire_slot", "the cell less one, and -1 for every cell past the index, which is not read", slots, 1);
                same("wire_slot", "an empty cell is -1, a full one its slot", kept, 1);
        }

        {
                static b64 want[64 * 4];
                positive cut = 1, wholes = 0, drops = 0, lists_run = 0;

                for (positive round = 0; round < 6000; round++)
                {
                        positive count = round < 8 ? round : WIRE_NEXT() % 9, width = round & 1 ? 64 : 1 + WIRE_NEXT() % 100, height = round & 2 ? 32 : 1 + WIRE_NEXT() % 100;
                        b64 address_to rects = (b64 address_to)(lists + 8192 - count * 32);

                        for (positive at = 0; at < count; at++)
                        {
                                positive kind = WIRE_NEXT() % 8;

                                rects[at * 4] = edges[WIRE_NEXT() % array_count(edges)];
                                rects[at * 4 + 1] = edges[WIRE_NEXT() % array_count(edges)];
                                rects[at * 4 + 2] = kind == 0 ? 0 : kind == 1 ? -5 : kind == 2 ? 1000000000 : (b64)(WIRE_NEXT() % 130);
                                rects[at * 4 + 3] = kind == 3 ? 0 : kind == 4 ? -1 : kind == 2 ? 1000000000 : (b64)(WIRE_NEXT() % 130);
                                if (kind == 5) // the whole picture, to the pixel
                                        rects[at * 4] = rects[at * 4 + 1] = 0, rects[at * 4 + 2] = (b64)width, rects[at * 4 + 3] = (b64)height;
                                if (kind == 6) // one short of it
                                        rects[at * 4] = rects[at * 4 + 1] = 0, rects[at * 4 + 2] = (b64)width - 1, rects[at * 4 + 3] = (b64)height;
                        }
                        memory_copy(want, rects, count * 32);
                        positive wanted = reference_rects_clip(want, count, width, height);
                        positive got = (rects_clip)(rects, count, width, height);
                        positive kept = wanted & ~((positive)1 << 63);

                        cut &= got == wanted && (!kept || !memory_compare(want, rects, kept * 32));
                        wholes += wanted >> 63;
                        drops += kept < count;
                        lists_run++;
                }
                same("rects_clip", "the kept rectangles, their count and whether one is the whole picture", cut, 1);
                same("rects_clip", "lists with a whole one and lists with one dropped", wholes > 300 && drops > 300, 1);
                same("rects_clip", "lists cut", lists_run, 6000);
                same("rects_clip", "no rectangles keeps none of a list that cannot be read", (rects_clip)((b64 address_to)(lists + 8192), 0, 10, 10), 0);
        }
#undef WIRE_NEXT
        memory_free(pages, 3 * 4096);
        memory_free(heads, 2 * 4096);
        memory_free(cells, 3 * 4096);
        memory_free(lists, 3 * 4096);
}

/*
        wire_arguments and wire_event against the C they stand for (the
        Wayland shim's argument loop and its event writer).

        wire_arguments: lists of kinds drawn from every kind the shim's
        table has and one it has not, from none to ten of them, with room
        for eight records, for as many as there are and for one fewer; the
        bytes a right encoding of the list and then that encoding cut at
        every length from nothing to all of it, with a byte too many and
        four too many, and with a count made nothing, one past what is
        left, as much as is left, the largest a word holds, and a text's
        last byte made not a zero. The bytes end at a page that cannot be
        read and the records at one that cannot be written. wire_event:
        every count of words to forty, at each of the four places in a
        word, the bytes written ending at a page that cannot be written
        and the words read at one that cannot be read.
*/
static positive reference_wire_arguments(const p8 address_to kinds, const p8 address_to classes, const p8 address_to bytes, positive left,
                                         p32 address_to out, positive room)
{
        positive n = 0, at = 0;

#define REFERENCE_WORD(place) ((p32)bytes[place] | (p32)bytes[(place) + 1] << 8 | (p32)bytes[(place) + 2] << 16 | (p32)bytes[(place) + 3] << 24)
        for (; kinds[0]; kinds += 2, n++)
        {
                positive class, word, padded;

                if (n >= room)
                        return n | WIRE_END_ROOM << 8;
                class = classes[kinds[0]];
                if (class == WIRE_NONE)
                {
                        out[n * 4] = out[n * 4 + 1] = out[n * 4 + 2] = out[n * 4 + 3] = 0;
                        continue;
                }
                if (left < 4)
                        return n | WIRE_END_SHORT << 8;
                word = REFERENCE_WORD(at);
                at += 4;
                left -= 4;
                if (!class)
                        return n | WIRE_END_KIND << 8;
                out[n * 4] = (p32)word;
                out[n * 4 + 1] = (p32)at;
                out[n * 4 + 2] = out[n * 4 + 3] = 0;
                if (!(class & WIRE_TEXT))
                        continue;
                if (!word && !(class & WIRE_EMPTY))
                        return n | WIRE_END_NOTHING << 8;
                if (word)
                {
                        padded = (word + 3) & ~(positive)3;
                        if (word > left || padded > left)
                                return n | WIRE_END_LONG << 8;
                        if ((class & WIRE_ENDED) && bytes[at + word - 1])
                                return n | WIRE_END_OPEN << 8;
                        at += padded;
                        left -= padded;
                }
                if (class & WIRE_PAIR)
                {
                        if (left < 8)
                                return n | WIRE_END_PAIR << 8;
                        out[n * 4 + 2] = REFERENCE_WORD(at);
                        out[n * 4 + 3] = REFERENCE_WORD(at + 4);
                        at += 8;
                        left -= 8;
                }
        }
#undef REFERENCE_WORD
        return n | (positive)(left ? WIRE_END_OVER : WIRE_END_DONE) << 8;
}

static positive reference_wire_event(p8 address_to into, positive object, positive opcode, const p32 address_to words, positive count)
{
        positive size = 8 + count * 4;
        p32 second = (p32)(size << 16 | (opcode & 0xffff));

        for (positive at = 0; at < 4; at++)
                into[at] = (p8)(object >> (at * 8)), into[4 + at] = (p8)(second >> (at * 8));
        for (positive word = 0; word < count; word++)
                for (positive at = 0; at < 4; at++)
                        into[8 + word * 4 + at] = (p8)(words[word] >> (at * 8));
        return size;
}

fn check_wire_arguments()
{
        static const p8 letters[] = {'i', 'u', 'f', 'o', 'O', 'n', 's', 'S', 'N', 'a', 'h', '?'};
        static p8 classes[256], kinds[2 * 12 + 2], right[4096];
        static p32 want[12 * 4];
        p8 address_to pages = memory(3 * 4096);
        p8 address_to records = memory(2 * 4096);
        p8 address_to written = memory(2 * 4096);
        p8 mapped = (bipolar)(positive)pages > 0 && (bipolar)(positive)records > 0 && (bipolar)(positive)written > 0;
        p64 seed = 0x853c49e6748fea9bull;
        positive agreed = 1, same_records = 1, runs = 0, ends[9] = {0};

        same("wire_arguments", "guard mappings", mapped, 1);
        if (!mapped)
                return;
        p8 protected = system_call_3(syscall(mprotect), (positive)(pages + 8192), 4096, 0) == 0 &&
                       system_call_3(syscall(mprotect), (positive)(records + 4096), 4096, 0) == 0 &&
                       system_call_3(syscall(mprotect), (positive)(written + 4096), 4096, 0) == 0;
        same("wire_arguments", "guard pages protected", protected, 1);
        if (!protected)
                return;
        classes['h'] = WIRE_NONE;
        classes['i'] = classes['u'] = classes['f'] = classes['o'] = classes['O'] = classes['n'] = WIRE_WORD;
        classes['s'] = WIRE_TEXT | WIRE_ENDED;
        classes['S'] = WIRE_TEXT | WIRE_ENDED | WIRE_EMPTY;
        classes['N'] = WIRE_TEXT | WIRE_ENDED | WIRE_PAIR;
        classes['a'] = WIRE_TEXT | WIRE_EMPTY;

#define WIRE_NEXT() (seed ^= seed << 13, seed ^= seed >> 7, seed ^= seed << 17, seed)
        for (positive round = 0; round < 3000; round++)
        {
                positive count = round < 11 ? round : WIRE_NEXT() % 11, size = 0, texts = 0, text_at[12], text_count[12];

                //  A list of kinds, and a right encoding of it.
                for (positive at = 0; at < count; at++)
                {
                        p8 kind = letters[WIRE_NEXT() % (round % 7 == 3 ? 12 : 11)];
                        positive class = classes[kind];

                        kinds[at * 2] = kind;
                        kinds[at * 2 + 1] = (p8)WIRE_NEXT();
                        if (class == WIRE_NONE)
                                continue;
                        if (class & WIRE_TEXT)
                        {
                                positive length = (class & WIRE_EMPTY) && WIRE_NEXT() % 4 == 0 ? 0 : 1 + WIRE_NEXT() % 23;

                                text_at[texts] = size;
                                text_count[texts++] = length;
                                right[size] = (p8)length, right[size + 1] = right[size + 2] = right[size + 3] = 0;
                                size += 4;
                                for (positive byte = 0; byte < ((length + 3) & ~(positive)3); byte++)
                                        right[size + byte] = byte + 1 < length ? (p8)(1 + WIRE_NEXT() % 255) : 0;
                                size += (length + 3) & ~(positive)3;
                                if (class & WIRE_PAIR)
                                        for (positive byte = 0; byte < 8; byte++)
                                                right[size++] = (p8)WIRE_NEXT();
                        }
                        else
                                for (positive byte = 0; byte < 4; byte++)
                                        right[size++] = (p8)WIRE_NEXT();
                }
                kinds[count * 2] = 0;
                //  The encoding cut at every length, with too many bytes, and with a count or an end spoiled.
                for (positive variant = 0; variant < size + 3 + texts * 5; variant++)
                {
                        positive left = variant <= size ? variant : variant == size + 1 ? size + 1 : variant == size + 2 ? size + 4 : size;
                        p8 address_to bytes = pages + 8192 - left;
                        positive rooms[3] = {8, count, count ? count - 1 : 0};

                        for (positive at = 0; at < left; at++)
                                bytes[at] = at < size ? right[at] : (p8)WIRE_NEXT();
                        if (variant > size + 2)
                        {
                                positive which = (variant - size - 3) / 5, how = (variant - size - 3) % 5, place = text_at[which];
                                positive after = size - place - 4;
                                p32 said = how == 0 ? 0 : how == 1 ? (p32)(after + 1) : how == 2 ? (p32)after : how == 3 ? 0xffffffffu : (p32)text_count[which];

                                for (positive at = 0; at < 4; at++)
                                        bytes[place + at] = (p8)(said >> (at * 8));
                                if (how == 4 && text_count[which])
                                        bytes[place + 4 + text_count[which] - 1] = 0x41;
                        }
                        for (positive r = 0; r < 3; r++)
                        {
                                positive room = rooms[r];
                                p32 address_to out = (p32 address_to)(records + 4096 - room * 16);
                                positive wanted = reference_wire_arguments(kinds, classes, bytes, left, want, room);
                                positive got = (wire_arguments)(kinds, classes, bytes, left, out, room);

                                agreed &= got == wanted;
                                same_records &= !(wanted & 255) || !memory_compare(want, out, (wanted & 255) * 16);
                                ends[wanted >> 8 < 9 ? wanted >> 8 : 0]++;
                                runs++;
                        }
                }
        }
        same("wire_arguments", "how many were read and why it stopped, every cut and every spoiled count", agreed, 1);
        same("wire_arguments", "the records of the arguments read", same_records, 1);
        //  Every way the walk ends is reached, or the rows above prove nothing of it.
        same("wire_arguments", "walks that ended each of the nine ways",
             ends[0] > 100 && ends[1] > 100 && ends[2] > 100 && ends[3] > 100 && ends[4] > 100 && ends[5] > 100 && ends[6] > 100 && ends[7] > 100 && ends[8] > 100, 1);
        same("wire_arguments", "walks compared", runs > 100000, 1);
        kinds[0] = 0;
        same("wire_arguments", "no kinds and no bytes is done, with nothing read or written",
             (wire_arguments)(kinds, classes, pages + 8192, 0, (p32 address_to)(records + 4096), 0), 0);

        {
                static p8 wanted_bytes[8 + 40 * 4];
                positive wrote = 1;

                for (positive count = 0; count <= 40; count++)
                        for (positive odd = 0; odd < 4; odd++)
                        {
                                positive size = 8 + count * 4, object = WIRE_NEXT(), opcode = WIRE_NEXT();
                                p8 address_to into = written + 4096 - size - odd;
                                p32 address_to words = (p32 address_to)(pages + 8192 - count * 4);

                                for (positive at = 0; at < count; at++)
                                        words[at] = (p32)WIRE_NEXT();
                                memory_fill(into, 0x6b, size + odd);
                                wrote &= (wire_event)(into, object, opcode, words, count) == reference_wire_event(wanted_bytes, object, opcode, words, count);
                                wrote &= !memory_compare(wanted_bytes, into, size);
                                for (positive at = 0; at < odd; at++)
                                        wrote &= into[size + at] == 0x6b;
                        }
                same("wire_event", "the head, the words and the size, every count and every place in a word, and no byte after them", wrote, 1);
        }
#undef WIRE_NEXT
        memory_free(pages, 3 * 4096);
        memory_free(records, 2 * 4096);
        memory_free(written, 2 * 4096);
}

/*
        evdev_decode, pointer_gain, pointer_scale, rects_point and ring_post
        against the C they stand for (the compositor's own, as
        src/canvas/compositor.c read a device's records, took a movement's
        gain and its pixels, looked for the window under a point and put an
        event in a program's ring before they were these).

        evdev_decode: every count of bytes from none to five records and
        twenty-three more, every room from none to six, the bytes ending at a
        page that cannot be read and the records at one that cannot be
        written, the seconds and microseconds any 64 bits. pointer_gain:
        counts and intervals at and either side of every bound, any 64 bits,
        and movements at every speed of the curve. pointer_scale: the same of
        the counts, the gain and the remainder, the remainder ending at a
        page that cannot be written and, for no counts, in one that cannot be
        read. rects_point: lists of rectangles on, round and far from the
        point, in every order, the columns and the order ending at pages that
        cannot be read. ring_post: every count of unread records at and
        either side of none, the cells and past them, heads and tails that
        wrap, the cells ending at pages that cannot be written and the tail
        in one that can only be read.
*/
static positive reference_evdev_decode(const p8 address_to bytes, positive size, p64 address_to records, positive room)
{
        positive count = 0;

        for (; size >= 24 && count < room; size -= 24, bytes += 24, count++)
        {
                p64 seconds, microseconds, rest;

                memory_copy(address_of seconds, bytes, 8);
                memory_copy(address_of microseconds, bytes + 8, 8);
                memory_copy(address_of rest, bytes + 16, 8);
                records[count * 2] = seconds * 1000000000ull + microseconds * 1000ull;
                records[count * 2 + 1] = rest;
        }
        return count;
}

static positive reference_pointer_gain(bipolar dx, bipolar dy, positive interval)
{
        positive value = (positive)dx * (positive)dx + (positive)dy * (positive)dy, root = 0, bit = (positive)1 << 62, speed;

        if (interval < 1000000)
                interval = 1000000;
        while (bit > value)
                bit >>= 2;
        for (; bit; bit >>= 2)
                if (value >= root + bit)
                {
                        value -= root + bit;
                        root = (root >> 1) + bit;
                }
                else
                        root >>= 1;
        speed = root * 1000000 / interval;
        if (speed <= 1)
                return 1024;
        if (speed >= 8)
                return 2560;
        return 1024 + 1536 * (speed - 1) / 7;
}

static bipolar reference_pointer_scale(bipolar delta, b32 address_to remainder, positive gain)
{
        bipolar scaled, whole;

        if (!delta)
                return 0;
        scaled = (bipolar)((positive)delta * gain + (positive)(bipolar)*remainder);
        //      Toward zero, as C divides; the least number has no negation, and is divided as it is.
        whole = scaled / 1024;
        *remainder = (b32)(scaled - whole * 1024);
        return whole > 2147483647 ? 2147483647 : whole < -2147483647 ? -2147483647 : whole;
}

static bipolar reference_rects_point(const address_any address_to columns, const p8 address_to order, positive count, bipolar x, bipolar y,
                                     positive grow)
{
        const b32 address_to xs = columns[0], address_to ys = columns[1];
        const p32 address_to widths = columns[2], address_to heights = columns[3];

        for (positive at = count; at--;)
        {
                positive row = order[at];

                if ((positive)x - (positive)(bipolar)xs[row] + grow < (positive)widths[row] + grow + grow &&
                    (positive)y - (positive)(bipolar)ys[row] + grow < (positive)heights[row] + grow + grow)
                        return (bipolar)at;
        }
        return -1;
}

// rects_point_w's reference: rects_point's with an order of 32-bit entries.
static bipolar reference_rects_point_w(const address_any address_to columns, const p32 address_to order, positive count, bipolar x, bipolar y,
                                       positive grow)
{
        const b32 address_to xs = columns[0], address_to ys = columns[1];
        const p32 address_to widths = columns[2], address_to heights = columns[3];

        for (positive at = count; at--;)
        {
                positive row = order[at];

                if ((positive)x - (positive)(bipolar)xs[row] + grow < (positive)widths[row] + grow + grow &&
                    (positive)y - (positive)(bipolar)ys[row] + grow < (positive)heights[row] + grow + grow)
                        return (bipolar)at;
        }
        return -1;
}

static positive reference_ring_post(const address_any address_to ring, positive cells, const p64 address_to record, positive replace)
{
        p16 address_to types = ring[0], address_to flags = ring[1];
        b32 address_to first = ring[2], address_to second = ring[3];
        p64 address_to stamps = ring[4];
        p32 address_to head_at = ring[5];
        const p32 address_to tail_at = ring[6];
        p32 head = *head_at, used = head - *tail_at, at = (head - 1) & (p32)(cells - 1);
        p16 type = (p16)record[2];

        if (!(replace && used && used <= cells && types[at] == type))
        {
                if (used >= cells)
                        return 0;
                at = head & (p32)(cells - 1);
        }
        else
                head--;
        types[at] = type;
        flags[at] = (p16)(record[2] >> 16);
        first[at] = (b32)(p32)record[1];
        second[at] = (b32)(p32)(record[1] >> 32);
        stamps[at] = record[0];
        *head_at = head + 1;
        return 1;
}

fn check_hand()
{
        static const p64 words[] = {0,
                                    1,
                                    2,
                                    3,
                                    7,
                                    8,
                                    9,
                                    100,
                                    1023,
                                    1024,
                                    1025,
                                    2560,
                                    46340,
                                    46341,
                                    65535,
                                    999999,
                                    1000000,
                                    1000001,
                                    2000000,
                                    7999999,
                                    8000000,
                                    16777216,
                                    1000000000,
                                    0x7fffffff,
                                    0x80000000,
                                    0xffffffff,
                                    0x100000000,
                                    0xb504f333,
                                    0xb504f334,
                                    0x4000000000000000,
                                    0x7fffffffffffffff,
                                    0x8000000000000000,
                                    0xffffffff00000000,
                                    0xffffffff80000000,
                                    0xffffffffffff0000,
                                    0xfffffffffffffc00,
                                    0xfffffffffffffbff,
                                    0xfffffffffffffff8,
                                    0xfffffffffffffffe,
                                    0xffffffffffffffff};
        p8 address_to raw = memory(3 * 4096);
        p8 address_to out = memory(3 * 4096);
        p8 address_to cols = memory(12 * 4096);
        p8 address_to rings = memory(14 * 4096);
        p8 mapped = (bipolar)(positive)raw > 0 && (bipolar)(positive)out > 0 && (bipolar)(positive)cols > 0 && (bipolar)(positive)rings > 0;
        p64 seed = 0x9e3779b97f4a7c15ull;

        same("evdev_decode", "guard mappings", mapped, 1);
        if (!mapped)
                return;
        p8 protected = system_call_3(syscall(mprotect), (positive)(raw + 8192), 4096, 0) == 0 &&
                       system_call_3(syscall(mprotect), (positive)(out + 8192), 4096, 0) == 0;

        //      The columns and the orders (a byte an entry, and a word): six stretches of a page, each before a page that cannot be read.
        for (positive at = 0; at < 6; at++)
                protected &= system_call_3(syscall(mprotect), (positive)(cols + at * 8192 + 4096), 4096, 0) == 0;
        //      The ring: its five columns and its head each before a page that cannot be written or read, and its tail in a page
        //      that can only be read (made so once it is set, each round).
        for (positive at = 0; at < 6; at++)
                protected &= system_call_3(syscall(mprotect), (positive)(rings + at * 8192 + 4096), 4096, 0) == 0;
        same("evdev_decode", "guard pages protected", protected, 1);
        if (!protected)
                return;

#define HAND_NEXT() (seed ^= seed << 13, seed ^= seed >> 7, seed ^= seed << 17, seed)
        {
                static p64 want[2 * 8];
                positive agreed = 1, kept = 1, runs = 0, cut = 0, full = 0;

                for (positive size = 0; size <= 24 * 5 + 23; size++)
                        for (positive room = 0; room <= 6; room++)
                        {
                                //  The bytes end at the guard, at an address of eight (the size's own remainder of eight is left before them);
                                //  the records end at theirs.
                                p8 address_to bytes = raw + 8192 - ((size + 7) & ~(positive)7);
                                p64 address_to records = (p64 address_to)(out + 8192 - room * 16);
                                p64 address_to before = records - 2;

                                for (positive at = 0; at < size; at++)
                                        bytes[at] = (p8)HAND_NEXT();
                                //  Some stamps a device would say, among the ones nobody would.
                                if (size >= 24 && (HAND_NEXT() & 1))
                                {
                                        p64 seconds = HAND_NEXT() % 4000000, microseconds = HAND_NEXT() % 1000000;

                                        memory_copy(bytes, address_of seconds, 8);
                                        memory_copy(bytes + 8, address_of microseconds, 8);
                                }
                                before[0] = 0x1111111111111111ull, before[1] = 0x2222222222222222ull;
                                positive wanted = reference_evdev_decode(bytes, size, want, room);
                                positive got = (evdev_decode)(bytes, size, records, room);

                                agreed &= got == wanted && (!wanted || !memory_compare(want, records, wanted * 16));
                                kept &= before[0] == 0x1111111111111111ull && before[1] == 0x2222222222222222ull;
                                runs++;
                                cut += size % 24 != 0 && wanted == size / 24;
                                full += wanted == room && size / 24 > room;
                        }
                same("evdev_decode", "the records, every count of bytes and every room", agreed, 1);
                same("evdev_decode", "nothing is written before the records", kept, 1);
                //  The lists reach a cut record and a room that is full, or the rows above prove nothing.
                same("evdev_decode", "runs with bytes past the last whole record, and runs the room ended", cut > 500 && full > 100, 1);
                same("evdev_decode", "runs", runs, (24 * 5 + 24) * 7);
                same("evdev_decode", "no bytes are no records, with nothing read or written", (evdev_decode)(raw + 8192, 0, (p64 address_to)(out + 8192), 9), 0);
                same("evdev_decode", "twenty-three bytes are none either", (evdev_decode)(raw + 8192 - 24, 23, (p64 address_to)(out + 8192), 9), 0);
                same("evdev_decode", "and no room takes none", (evdev_decode)(raw + 8192 - 48, 48, (p64 address_to)(out + 8192), 0), 0);
        }

        {
                positive agreed = 1, runs = 0, seen[8] = {0};

                for (positive a = 0; a < array_count(words); a++)
                        for (positive b = 0; b < array_count(words); b++)
                                for (positive c = 0; c < array_count(words); c++)
                                {
                                        positive wanted = reference_pointer_gain((bipolar)words[a], (bipolar)words[b], words[c]);

                                        agreed &= (pointer_gain)((bipolar)words[a], (bipolar)words[b], words[c]) == wanted;
                                        runs++;
                                }
                //  A hand's movements: a few counts in a part of a millisecond to a few of them, which is where the curve is.
                for (positive round = 0; round < 300000; round++)
                {
                        bipolar dx = (bipolar)(HAND_NEXT() % 61) - 30, dy = (bipolar)(HAND_NEXT() % 61) - 30;
                        positive interval = round & 1 ? 200000 + HAND_NEXT() % 20000000 : HAND_NEXT() % 3 * 500000 + HAND_NEXT() % 9 * 1000000;
                        positive wanted = reference_pointer_gain(dx, dy, interval);

                        agreed &= (pointer_gain)(dx, dy, interval) == wanted;
                        seen[wanted == 1024 ? 0 : wanted >= 2560 ? 7 : (wanted - 1024 + 219) * 7 / 1536] += 1;
                        runs++;
                }
                same("pointer_gain", "the gain, every bound of the counts and the interval and three hundred thousand movements", agreed, 1);
                //  Every step of the curve is reached, each many times.
                same("pointer_gain", "movements at every speed of the curve",
                     seen[0] > 1000 && seen[1] > 1000 && seen[2] > 1000 && seen[3] > 1000 && seen[4] > 1000 && seen[5] > 1000 && seen[6] > 1000 && seen[7] > 1000, 1);
                same("pointer_gain", "movements compared", runs > 360000, 1);
                same("pointer_gain", "slow is one to one", (pointer_gain)(1, 0, 8000000), 1024);
                same("pointer_gain", "fast is the ceiling", (pointer_gain)(30, 40, 1000000), 2560);
                same("pointer_gain", "three and four counts in a millisecond are a speed of five", (pointer_gain)(3, -4, 1000000), 1024 + 1536 * 4 / 7);
                same("pointer_gain", "no time is a millisecond, and not a division by nothing", (pointer_gain)(2, 0, 0), 1024 + 1536 * 1 / 7);
        }

        {
                static const b32 remainders[] = {0, 1, -1, 511, -511, 1023, -1023, 1024, -1024, 2147483647, -2147483647, -2147483647 - 1};
                b32 address_to remainder = (b32 address_to)(out + 8192 - 4);
                positive agreed = 1, left = 1, runs = 0, held = 0, negative = 0;

                for (positive a = 0; a < array_count(words); a++)
                        for (positive b = 0; b < array_count(words); b++)
                                for (positive c = 0; c < array_count(remainders); c++)
                                {
                                        b32 want_remainder = remainders[c];
                                        bipolar wanted = reference_pointer_scale((bipolar)words[a], address_of want_remainder, words[b]);

                                        *remainder = remainders[c];
                                        bipolar got = (pointer_scale)((bipolar)words[a], remainder, words[b]);

                                        agreed &= got == wanted;
                                        left &= *remainder == want_remainder;
                                        runs++;
                                        held += wanted == 2147483647 || wanted == -2147483647;
                                }
                //  A hand's: a few counts either way, the gains of the curve and of a sensitivity, the remainder the last movement left.
                *remainder = 0;
                {
                        b32 want_remainder = 0;

                        for (positive round = 0; round < 300000; round++)
                        {
                                bipolar delta = (bipolar)(HAND_NEXT() % 41) - 20;
                                positive gain = round & 1 ? 1024 + HAND_NEXT() % 1537 : HAND_NEXT() % 13000;
                                bipolar wanted = reference_pointer_scale(delta, address_of want_remainder, gain);

                                agreed &= (pointer_scale)(delta, remainder, gain) == wanted;
                                left &= *remainder == want_remainder;
                                negative += want_remainder < 0;
                                runs++;
                        }
                }
                same("pointer_scale", "the pixels, every bound of the counts, the gain and the remainder and three hundred thousand movements", agreed, 1);
                same("pointer_scale", "the remainder kept for the next movement", left, 1);
                same("pointer_scale", "answers held to the most a place is, and remainders below nothing", held > 1000 && negative > 1000, 1);
                same("pointer_scale", "movements compared", runs > 310000, 1);
                same("pointer_scale", "no counts is no pixels, with the remainder not looked at", (positive)(pointer_scale)(0, (b32 address_to)(out + 8192), 2560), 0);
                *remainder = 1000;
                same("pointer_scale", "a count at one and a half with a thousand left over is two pixels", (positive)(pointer_scale)(1, remainder, 1536), 2);
                same("pointer_scale", "and 488 are left", (positive)*remainder, 488);
                *remainder = 0;
                same("pointer_scale", "a count back at two and a half is two pixels back, toward nothing", (positive)(pointer_scale)(-1, remainder, 2560), (positive)-2);
                same("pointer_scale", "and 512 back are left", (positive)*remainder, (positive)(bipolar)-512);
        }

        {
                static const bipolar places[] = {-2147483647 - 1, -16777216, -1000, -46, -45, -7, -6, -1, 0, 1, 5, 6, 7, 44, 45, 46, 99, 100, 101, 145, 146, 1000, 16777216, 2147483647};
                static const positive grows[] = {0, 1, 6, 45, 1000, 0x7fffffff, 0x10000000000, 0x7fffffffffffffff, 0xffffffffffffffff};
                positive agreed = 1, agreed_w = 1, runs = 0, hits = 0, misses = 0, under = 0;

                for (positive round = 0; round < 40000; round++)
                {
                        //  Thirty-two rectangles at the most, as a desktop has windows; the columns end at their guards, and so does the order.
                        positive rows = 1 + HAND_NEXT() % 32, count = round < 40 ? round % 4 : HAND_NEXT() % 40;
                        b32 address_to xs = (b32 address_to)(cols + 4096 - rows * 4), address_to ys = (b32 address_to)(cols + 8192 + 4096 - rows * 4);
                        p32 address_to widths = (p32 address_to)(cols + 2 * 8192 + 4096 - rows * 4), address_to heights = (p32 address_to)(cols + 3 * 8192 + 4096 - rows * 4);
                        p8 address_to order = cols + 4 * 8192 + 4096 - count;
                        p32 address_to order_w = (p32 address_to)(cols + 5 * 8192 + 4096 - count * 4);
                        const address_any columns[4] = {xs, ys, widths, heights};
                        positive grow = round % 5 == 0 ? grows[HAND_NEXT() % array_count(grows)] : round % 5 == 1 ? 6 : 0;
                        bipolar x, y;

                        for (positive at = 0; at < rows; at++)
                        {
                                positive kind = HAND_NEXT() % 8;

                                xs[at] = kind == 0 ? (b32)places[HAND_NEXT() % array_count(places)] : (b32)(HAND_NEXT() % 300) - 100;
                                ys[at] = kind == 1 ? (b32)places[HAND_NEXT() % array_count(places)] : (b32)(HAND_NEXT() % 300) - 100;
                                widths[at] = kind == 2 ? 0 : kind == 3 ? 0xffffffffu : kind == 4 ? 0x80000000u : (p32)(HAND_NEXT() % 200);
                                heights[at] = kind == 5 ? 0 : kind == 3 ? 0xffffffffu : kind == 6 ? 1 : (p32)(HAND_NEXT() % 200);
                        }
                        for (positive at = 0; at < count; at++)
                                order[at] = (p8)(HAND_NEXT() % rows), order_w[at] = order[at];
                        //  The point: on a corner of a rectangle, a pixel or the growth either side of one, or anywhere.
                        if (round % 3)
                        {
                                positive row = HAND_NEXT() % rows, corner = HAND_NEXT() % 4;
                                bipolar off = (bipolar)(HAND_NEXT() % 5) - 2 + (HAND_NEXT() % 3 == 0 ? (HAND_NEXT() & 1 ? (bipolar)grow : -(bipolar)grow) : 0);

                                x = (bipolar)xs[row] + (corner & 1 ? (bipolar)widths[row] : 0) + off;
                                y = (bipolar)ys[row] + (corner & 2 ? (bipolar)heights[row] : 0) + (bipolar)(HAND_NEXT() % 5) - 2;
                        }
                        else
                                x = places[HAND_NEXT() % array_count(places)], y = places[HAND_NEXT() % array_count(places)];
                        if (round % 97 == 0)
                                x = (bipolar)words[HAND_NEXT() % array_count(words)], y = (bipolar)words[HAND_NEXT() % array_count(words)];

                        bipolar wanted = reference_rects_point(columns, order, count, x, y, grow);

                        agreed &= (rects_point)(columns, order, count, x, y, grow) == wanted;
                        agreed_w &= (rects_point_w)(columns, order_w, count, x, y, grow) == wanted;
                        runs++;
                        hits += wanted >= 0;
                        misses += wanted < 0 && count > 0;
                        under += wanted >= 0 && (positive)wanted + 1 < count;
                }
                same("rects_point", "the last entry whose rectangle holds the point, forty thousand lists", agreed, 1);
                //  Found, not found, and found under entries that did not hold it: or the row above proves nothing.
                same("rects_point", "lists with the point in one, in none, and in one under others", hits > 3000 && misses > 3000 && under > 1000, 1);
                same("rects_point", "lists compared", runs, 40000);
                same("rects_point_w", "the same lists with a word an entry: the same entry", agreed_w, 1);
                {
                        //  A thousand and twenty-four rectangles, the most a stretch holds, named by entries past a byte's reach: the entry that
                        //  holds the point is found among them, and none is found where none holds it.
                        b32 address_to xs = (b32 address_to)cols, address_to ys = (b32 address_to)(cols + 8192);
                        p32 address_to widths = (p32 address_to)(cols + 2 * 8192), address_to heights = (p32 address_to)(cols + 3 * 8192);
                        const address_any columns[4] = {xs, ys, widths, heights};
                        positive wide = 1, far = 0, none = 0, compared = 0;

                        for (positive at = 0; at < 1024; at++)
                                xs[at] = (b32)(at * 10), ys[at] = 0, widths[at] = 10, heights[at] = (p32)(10 + HAND_NEXT() % 10);
                        for (positive round = 0; round < 4000; round++)
                        {
                                positive count = 1 + HAND_NEXT() % 1024, grow = round % 3 == 0 ? HAND_NEXT() % 8 : 0;
                                p32 address_to order_w = (p32 address_to)(cols + 5 * 8192 + 4096 - count * 4);
                                bipolar x = (bipolar)(HAND_NEXT() % 10300) - 50, y = (bipolar)(HAND_NEXT() % 30) - 5, wanted;

                                for (positive at = 0; at < count; at++)
                                        order_w[at] = (p32)(HAND_NEXT() % 1024);
                                wanted = reference_rects_point_w(columns, order_w, count, x, y, grow);
                                wide &= (rects_point_w)(columns, order_w, count, x, y, grow) == wanted;
                                far += wanted >= 0 && order_w[wanted] > 255;
                                none += wanted < 0;
                                compared++;
                        }
                        same("rects_point_w", "a thousand rectangles named past a byte, four thousand lists: the entry the reference finds", wide, 1);
                        same("rects_point_w", "lists whose entry found names a rectangle past 255, and lists with none found", far > 300 && none > 300, 1);
                        same("rects_point_w", "no entries is none, with no column and no order read",
                             (positive)(rects_point_w)(columns, (const p32 address_to)(cols + 5 * 8192 + 4096), 0, 5, 5, 6), (positive)-1);
                        same("rects_point_w", "wide lists compared", compared, 4000);
                }
                {
                        const address_any nowhere[4] = {cols + 4096, cols + 8192 + 4096, cols + 2 * 8192 + 4096, cols + 3 * 8192 + 4096};

                        same("rects_point", "no entries is none, with no column and no order read",
                             (positive)(rects_point)(nowhere, cols + 4 * 8192 + 4096, 0, 5, 5, 6), (positive)-1);
                }
        }

        {
                static const positive sizes[] = {1, 2, 4, 64, 256};
                static p16 want_types[256], want_flags[256];
                static b32 want_first[256], want_second[256];
                static p64 want_stamps[256];
                positive agreed = 1, cells_agreed = 1, runs = 0, posted = 0, over = 0, refused = 0;
                p32 address_to tail_at = (p32 address_to)(rings + 12 * 4096);
                p64 address_to record = (p64 address_to)(raw + 8192 - 24);

                for (positive round = 0; round < 60000; round++)
                {
                        positive cells = sizes[HAND_NEXT() % array_count(sizes)], replace = HAND_NEXT() % 3 == 0 ? 0 : HAND_NEXT() % 3 == 0 ? HAND_NEXT() : 1;
                        p16 address_to types = (p16 address_to)(rings + 4096 - cells * 2), address_to flags = (p16 address_to)(rings + 8192 + 4096 - cells * 2);
                        b32 address_to first = (b32 address_to)(rings + 2 * 8192 + 4096 - cells * 4), address_to second = (b32 address_to)(rings + 3 * 8192 + 4096 - cells * 4);
                        p64 address_to stamps = (p64 address_to)(rings + 4 * 8192 + 4096 - cells * 8);
                        p32 address_to head_at = (p32 address_to)(rings + 5 * 8192 + 4096 - 4);
                        const address_any ring[7] = {types, flags, first, second, stamps, head_at, tail_at};
                        p32 want_head, kind = (p32)(HAND_NEXT() % 12), used, head = round % 7 == 0 ? (p32)(HAND_NEXT() % 3) - 1 : (p32)HAND_NEXT(), tail;
                        const address_any want_ring[7] = {want_types, want_flags, want_first, want_second, want_stamps, address_of want_head, tail_at};

                        used = kind == 0 ? 0 : kind == 1 ? 1 : kind == 2 ? (p32)cells - 1 : kind == 3 ? (p32)cells : kind == 4 ? (p32)cells + 1 :
                               kind == 5 ? 0x80000000u : kind == 6 ? 0xffffffffu : kind == 7 ? (p32)HAND_NEXT() : (p32)(HAND_NEXT() % (cells + 1));
                        tail = head - used;
                        for (positive at = 0; at < cells; at++)
                        {
                                types[at] = (p16)(HAND_NEXT() % 3), flags[at] = (p16)HAND_NEXT();
                                first[at] = (b32)HAND_NEXT(), second[at] = (b32)HAND_NEXT(), stamps[at] = HAND_NEXT();
                        }
                        memory_copy(want_types, types, cells * 2), memory_copy(want_flags, flags, cells * 2);
                        memory_copy(want_first, first, cells * 4), memory_copy(want_second, second, cells * 4), memory_copy(want_stamps, stamps, cells * 8);
                        *head_at = want_head = head;
                        //  The tail is the reader's: it is set, and then its page can only be read while the routine runs.
                        system_call_3(syscall(mprotect), (positive)(rings + 12 * 4096), 4096, 3);
                        *tail_at = tail;
                        system_call_3(syscall(mprotect), (positive)(rings + 12 * 4096), 4096, 1);
                        record[0] = HAND_NEXT(), record[1] = HAND_NEXT(), record[2] = HAND_NEXT() % 3 | HAND_NEXT() << 16;

                        positive wanted = reference_ring_post(want_ring, cells, record, replace);
                        positive got = (ring_post)(ring, cells, record, replace);

                        agreed &= got == wanted && *head_at == want_head;
                        cells_agreed &= !memory_compare(want_types, types, cells * 2) && !memory_compare(want_flags, flags, cells * 2) &&
                                        !memory_compare(want_first, first, cells * 4) && !memory_compare(want_second, second, cells * 4) &&
                                        !memory_compare(want_stamps, stamps, cells * 8);
                        runs++;
                        posted += wanted && want_head != head;
                        over += wanted && want_head == head;
                        refused += !wanted;
                }
                system_call_3(syscall(mprotect), (positive)(rings + 12 * 4096), 4096, 3);
                same("ring_post", "whether the record is in the ring, and the head, sixty thousand rings", agreed, 1);
                same("ring_post", "every cell of the ring is the record's or as it was", cells_agreed, 1);
                //  Put after the last, written over the last, and refused: or the rows above prove nothing.
                same("ring_post", "records put in, records written over the last and records refused", posted > 5000 && over > 3000 && refused > 5000, 1);
                same("ring_post", "rings compared", runs, 60000);
        }
#undef HAND_NEXT
        memory_free(raw, 3 * 4096);
        memory_free(out, 3 * 4096);
        memory_free(cols, 10 * 4096);
        memory_free(rings, 14 * 4096);
}

/*
        Call the assembly entry even for compile-time small sizes. Disjoint
        maps cover every byte value and noninjective maps; aliased maps retain
        the original four-byte load-all-then-store-all contract. On x86_64
        each body; on riscv64 every call the first in a process, which asks
        for V, then without V.
*/
fn check_translate()
{
        static const positive sizes[] = {
            0, 1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 31, 32, 33, 39, 40, 63, 64, 65, 127, 128,
            129, 255, 256, 257, 511, 512, 513, 4095, 4096,
        };
        static const bipolar deltas[] = {
            -512, -257, -256, -255, -128, -64, -1, 0, 1, 63, 64,
            127, 128, 255, 256, 257, 511, 512,
        };
        static p8 want[4096], palette[256];
        p8 address_to pages = memory(3 * 4096);
        p8 address_to tables = memory(3 * 4096);
        p8 mapped = (bipolar)(positive)pages > 0 &&
                      (bipolar)(positive)tables > 0;

        same("memory_translate", "guard mappings", mapped, 1);
        if (!mapped)
        {
                if ((bipolar)(positive)pages > 0) memory_free(pages, 3 * 4096);
                if ((bipolar)(positive)tables > 0) memory_free(tables, 3 * 4096);
                return;
        }
        p8 protected =
            system_call_3(syscall(mprotect), (positive)pages, 4096, 0) == 0 &&
            system_call_3(syscall(mprotect), (positive)(pages + 8192), 4096, 0) == 0 &&
            system_call_3(syscall(mprotect), (positive)tables, 4096, 0) == 0 &&
            system_call_3(syscall(mprotect), (positive)(tables + 8192), 4096, 0) == 0;
        same("memory_translate", "guard pages protected", protected, 1);
        if (!protected)
        {
                memory_free(pages, 3 * 4096);
                memory_free(tables, 3 * 4096);
                return;
        }
        p8 address_to got = pages + 4096;
#if X64
        p8 avx2 = cpu_has_avx2, avx512 = cpu_has_avx512;
        p8 vbmi = cpu_has_avx512_vbmi;
        positive tiers = 4;
#elif RISCV64
        p8 vector = 0;
        positive tiers = 2;
#else
        positive tiers = 1;
#endif
        for (positive tier = 0; tier < tiers; tier++)
        {
#if X64
                cpu_has_avx2 = tier < 3 ? avx2 : 0;
                cpu_has_avx512 = tier < 2 ? avx512 : 0;
                cpu_has_avx512_vbmi = tier < 1 ? vbmi : 0;
#elif RISCV64
                if (tier == 1)
                {
                        vector = cpu_has_vector;
                        cpu_has_vector = 1;
                }
#endif
                same("memory_translate", "null zero-sized return",
                     (positive)(memory_translate)(null, 0, null), 0);
                same("memory_translate", "inaccessible zero-sized return",
                     (positive)(memory_translate)(pages, 0, tables),
                     (positive)pages);
                for (positive s = 0; s < array_count(sizes); s++)
                        for (positive residue = 0; residue <= 64; residue++)
                        {
                                positive size = sizes[s];
                                positive offset = residue == 64 || size + residue > 4096
                                                      ? 4096 - size : residue;
                                p8 address_to table = residue == 64
                                    ? tables + 8192 - 256
                                    : tables + 4096 + ((residue * 17 + s) & 63);
                                for (positive i = 0; i < 256; i++)
                                        table[i] = palette[i] = (p8)(i * (s & 1 ? 197 : 128) + s);
                                for (positive i = 0; i < 4096; i++)
                                        got[i] = want[i] = (p8)(i * 197 + residue);
                                for (positive i = 0; i < size; i++)
                                        want[offset + i] = palette[want[offset + i]];
                                VECTOR_FIRST_CALL(tier == 0);
                                same("memory_translate", "returned original address",
                                     (positive)(memory_translate)(got + offset, size, table),
                                     (positive)(got + offset));
                                same_bytes("memory_translate", "sizes, residues, page bounds",
                                           got, want, 4096);
                                same_bytes("memory_translate", "table remains unchanged",
                                           table, palette, 256);
                        }
                for (positive s = 0; s < array_count(sizes) - 2; s++)
                        for (positive d = 0; d < array_count(deltas); d++)
                        {
                                positive size = sizes[s], i = 0;
                                for (positive at = 0; at < 4096; at++)
                                        got[at] = want[at] = (p8)((at * 37) ^ (at >> 3));
                                // 255 early in the block: a table ending at the block's first
                                // byte gives it the first group's answer only.
                                got[1024 + 5] = want[1024 + 5] = 255;
                                got[1024 + 70] = want[1024 + 70] = 255;
                                p8 address_to block = want + 1024;
                                p8 address_to table = block + deltas[d];
                                for (; i + 4 <= size; i += 4)
                                {
                                        p8 a = table[block[i]], b = table[block[i + 1]];
                                        p8 c = table[block[i + 2]], e = table[block[i + 3]];
                                        block[i] = a; block[i + 1] = b;
                                        block[i + 2] = c; block[i + 3] = e;
                                }
                                for (; i < size; i++) block[i] = table[block[i]];
                                VECTOR_FIRST_CALL(tier == 0);
                                same("memory_translate", "aliased return address",
                                     (positive)(memory_translate)(got + 1024, size,
                                                                 got + 1024 + deltas[d]),
                                     (positive)(got + 1024));
                                same_bytes("memory_translate", "aliased four-byte ordering",
                                           got, want, 4096);
                        }
                //      Tables a few rows from the identity, as tr a-z A-Z's is: the AVX2 body walks only
                //      the rows that differ, up to ten of them, and the four-byte groups take more.
                for (positive rows = 1; rows <= 12; rows++)
                        for (positive s = 0; s < 4; s++)
                        {
                                static const positive sparse[4] = {1024, 1055, 2065, 4096};
                                positive size = sparse[s], offset = 4096 - size;
                                p8 address_to table = tables + 8192 - 256;

                                for (positive i = 0; i < 256; i++)
                                        table[i] = palette[i] = (p8)i;
                                for (positive r = 0; r < rows; r++)
                                {
                                        positive row = (r * 7 + s * 3 + rows) & 15;

                                        for (positive i = 0; i < 16; i++)
                                                table[row * 16 + i] = palette[row * 16 + i] =
                                                    (p8)((row * 16 + i) ^ (0x5a + r + i));
                                }
                                for (positive i = 0; i < 4096; i++)
                                        got[i] = want[i] = (p8)(i * 197 + rows);
                                for (positive i = 0; i < size; i++)
                                        want[offset + i] = palette[want[offset + i]];
                                VECTOR_FIRST_CALL(tier == 0);
                                (memory_translate)(got + offset, size, table);
                                same_bytes("memory_translate", "tables a few rows from the identity",
                                           got, want, 4096);
                        }
        }
#if X64
        cpu_has_avx2 = avx2;
        cpu_has_avx512 = avx512;
        cpu_has_avx512_vbmi = vbmi;
#elif RISCV64
        cpu_has_vector = vector;
#endif
        memory_free(pages, 3 * 4096);
        memory_free(tables, 3 * 4096);
}

/*
        cells_from_ascii against the loop it stands for, on every body the
        machine has. The bytes end on a guard page and so do the cells, so a
        load past limit faults instead of passing; a run that leaves slack
        after limit instead proves nothing past it is stored. Around each
        stop the flags that are not stop's are set too, and cells past
        guarded carry stop's own, which must decide nothing.
*/
static positive cells_ascii_reference(p64 address_to cells,
                                      const p8 address_to bytes, positive limit,
                                      positive guarded, p64 attribute, p64 stop)
{
        positive at = 0;

        for (; at < limit; at++)
        {
                if ((unsigned int)bytes[at] - ' ' >= 95)
                        break;
                if (at < guarded && (cells[at] & stop))
                        break;
                cells[at] = attribute | bytes[at];
        }

        return at;
}

static p64 cells_ascii_random(p64 address_to state)
{
        p64 x = address_to state;

        XORSHIFT64(x);
        address_to state = x;
        return x;
}

// The body under test: cells_from_ascii, or cells_from_ascii_wide for the
// tiers that ask for the vector entry by name.
static positive (address_to cells_ascii_body)(address_any, address_any, positive,
                                             positive, p64, p64) = cells_from_ascii;

static positive cells_ascii_case(p8 address_to pages, p8 address_to rows,
                                 p64 address_to want, positive limit,
                                 positive slack, positive bad, positive dirty,
                                 positive guarded, p64 attribute, p64 stop,
                                 p64 address_to seed)
{
        static const p8 outside[] = {0x00, 0x1b, 0x1f, 0x7f, 0x80,
                                     0x9b, 0xc2, 0xdf, 0xe0, 0xff};
        p8 address_to bytes = pages + 8192 - limit - slack;
        p64 address_to cells = (p64 address_to)(rows + 8192 - 8 * (limit + slack));
        positive span = limit + slack, got, expected, wrong = 0;

        for (positive at = 0; at < span; at++)
        {
                p64 flags = cells_ascii_random(seed) & ~stop & ((p64)0xffff << 32);

                bytes[at] = (p8)(' ' + (at * 37 + limit) % 95);
                cells[at] = flags | (cells_ascii_random(seed) & 0xffffffff);
                if (at >= guarded && (at & 1))
                        cells[at] |= stop;
        }

        if (bad < limit)
                bytes[bad] = outside[(bad + limit) % array_count(outside)];
        if (dirty < limit)
        {
                p64 lowest = stop & (~stop + 1);
                p64 rest = stop & ~lowest;

                cells[dirty] |= (dirty & 1) && rest ? rest : lowest;
        }

        for (positive at = 0; at < span; at++)
                want[at] = cells[at];

        expected = cells_ascii_reference(want, bytes, limit, guarded, attribute, stop);
        got = cells_ascii_body(cells, bytes, limit, guarded, attribute, stop);

        if (got != expected)
                wrong++;
        for (positive at = 0; at < span; at++)
                if (cells[at] != want[at])
                        wrong++;

        return wrong != 0;
}

fn check_cells_from_ascii()
{
        static const positive long_limits[] = {63, 64, 65, 127, 180, 255,
                                               256, 257, 479, 480};
        static p64 want[512];
        p64 stop = (p64)(128 | 256) << 48;
        p64 attribute = (p64)0x0203 << 32 | (p64)0x05 << 48;
        p8 address_to pages = memory(3 * 4096);
        p8 address_to rows = memory(3 * 4096);
        p8 mapped = (bipolar)(positive)pages > 0 &&
                      (bipolar)(positive)rows > 0;

        same("cells_from_ascii", "guard mappings", mapped, 1);
        if (!mapped)
        {
                if ((bipolar)(positive)pages > 0) memory_free(pages, 3 * 4096);
                if ((bipolar)(positive)rows > 0) memory_free(rows, 3 * 4096);
                return;
        }
        p8 protected =
            system_call_3(syscall(mprotect), (positive)pages, 4096, 0) == 0 &&
            system_call_3(syscall(mprotect), (positive)(pages + 8192), 4096, 0) == 0 &&
            system_call_3(syscall(mprotect), (positive)rows, 4096, 0) == 0 &&
            system_call_3(syscall(mprotect), (positive)(rows + 8192), 4096, 0) == 0;
        same("cells_from_ascii", "guard pages protected", protected, 1);
        if (!protected)
        {
                memory_free(pages, 3 * 4096);
                memory_free(rows, 3 * 4096);
                return;
        }
        //      The tiers past the program's own are cells_from_ascii_wide by
        //      name, the kernel's vector entry, on every body it has here:
        //      AVX-512 and AVX2 on x86_64, NEON on arm64, V on riscv64.
#if X64
        p8 avx2 = cpu_has_avx2, avx512 = cpu_has_avx512;
        positive tiers = 3, vector_tiers = avx2 ? (avx512 ? 2 : 1) : 0;
#elif RISCV64
        struct { b64 key; p64 value; } probe = {4, 0};
        positive tiers = 1, vector_tiers =
            system_call_5(258, (positive)address_of probe, 1, 0, 0, 0) == 0 &&
            (probe.value & 4) ? 1 : 0;
#else
        positive tiers = 1, vector_tiers = 1;
#endif
        for (positive tier = 0; tier < tiers + vector_tiers; tier++)
        {
                p64 seed = 0x9e3779b97f4a7c15 + tier;
                positive short_wrong = 0, long_wrong = 0;
                positive vector = tier >= tiers ? tier - tiers + 1 : 0;

                cells_ascii_body = vector ? cells_from_ascii_wide : cells_from_ascii;
#if X64
                cpu_has_avx2 = vector ? avx2 : tier < 2 ? avx2 : 0;
                cpu_has_avx512 = vector ? (vector == 1 && vector_tiers == 2 ? avx512 : 0)
                                        : tier < 1 ? avx512 : 0;
#endif
                //      Every stop and every guard around it, short runs.
                for (positive limit = 0; limit <= 26; limit++)
                        for (positive slack = 0; slack <= 5; slack += 5)
                                for (positive bad = 0; bad <= limit; bad++)
                                        for (positive dirty = 0; dirty <= limit; dirty++)
                                        {
                                                positive guards[] = {0, dirty, dirty + 1,
                                                                     limit + 9};

                                                for (positive g = 0; g < array_count(guards); g++)
                                                        short_wrong += cells_ascii_case(
                                                            pages, rows, want, limit, slack,
                                                            bad, dirty, guards[g], attribute,
                                                            stop, address_of seed);
                                        }

                //      Long runs, with a stop anywhere or nowhere, any guard,
                //      any alignment and a different attribute and stop.
                for (positive round = 0; round < 3000; round++)
                {
                        positive limit = long_limits[cells_ascii_random(address_of seed) %
                                                     array_count(long_limits)];
                        positive slack = cells_ascii_random(address_of seed) % 8;
                        positive bad = cells_ascii_random(address_of seed) % (limit + limit / 3 + 1);
                        positive dirty = cells_ascii_random(address_of seed) % (limit + limit / 3 + 1);
                        positive guarded = cells_ascii_random(address_of seed) % (limit + 17);
                        p64 other = cells_ascii_random(address_of seed);
                        p64 mask = (other >> 48) ? (other >> 48) << 48 : stop;

                        if (limit + slack > 480)
                                slack = 480 - limit;
                        long_wrong += cells_ascii_case(pages, rows, want, limit, slack, bad,
                                                       dirty, guarded,
                                                       other & ~(p64)0xffffffff, mask,
                                                       address_of seed);
                }

                same("cells_from_ascii", vector ? "short runs, cells_from_ascii_wide"
                                         : tier + 1 < tiers ? tier ? "short runs, AVX2 body"
                                                                : "short runs, widest body"
                                                       : "short runs, word body",
                     short_wrong, 0);
                same("cells_from_ascii", vector ? "long runs, cells_from_ascii_wide"
                                         : tier + 1 < tiers ? tier ? "long runs, AVX2 body"
                                                                : "long runs, widest body"
                                                       : "long runs, word body",
                     long_wrong, 0);
        }
        cells_ascii_body = cells_from_ascii;
#if X64
        cpu_has_avx2 = avx2;
        cpu_has_avx512 = avx512;
#endif
        memory_free(pages, 3 * 4096);
        memory_free(rows, 3 * 4096);
}

/*
        Every size across the four-lane, 32, 64 and 512-byte boundaries,
        every residue against a protected page on either side, tables that
        mark with any nonzero byte at four densities, tables of one member
        and two over bytes from an alphabet of eight that holds them, and two
        it does not hold, the table against a page that faults on eight and
        a byte to seven short of it off eight, and on x86_64 each body in
        turn: all features, the lazily asked VBMI2 answer forced to absent,
        and AVX-512 off; on riscv64 every call the first in a process, which
        asks for V, then without V on the processor's Zbb and without. Bytes the
        routine leaves past the kept ones inside the block are unspecified;
        everything before and after is compared exactly.
*/
fn check_delete_bytes()
{
        static const positive sizes[] = {
            0, 1, 2, 3, 4, 5, 7, 8, 31, 32, 33, 63, 64, 65, 67, 127, 128, 129,
            255, 256, 257, 511, 512, 513, 519, 1000, 4095, 4096,
        };
        static p8 want[4096];
        p8 address_to pages = guard_pages("memory_delete_bytes");
        p8 address_to tables = guard_pages("memory_delete_bytes' table");

        if (!pages || !tables)
        {
                if (pages)
                        memory_free(pages, 3 * 4096);
                if (tables)
                        memory_free(tables, 3 * 4096);
                return;
        }
        p8 address_to got = pages + 4096;
        p64 seed = 0x9e3779b97f4a7c15ull;
#if X64
        p8 avx512 = cpu_has_avx512, vbmi2 = cpu_has_avx512_vbmi2;
        positive tiers = 3;
#elif RISCV64
        p8 vector = 0, zbb = cpu_has_zbb;
        positive tiers = 3;
#else
        positive tiers = 1;
#endif
        for (positive tier = 0; tier < tiers; tier++)
        {
#if X64
                cpu_has_avx512 = tier < 2 ? avx512 : 0;
                cpu_has_avx512_vbmi2 = tier == 1 ? 1 : vbmi2;
#elif RISCV64
                if (tier == 1)
                {
                        vector = cpu_has_vector;
                        cpu_has_vector = 1;
                }
                if (tier == 2)
                {
                        if (!zbb)
                                continue;
                        cpu_has_zbb = 0;
                }
#endif
                VECTOR_FIRST_CALL(tier == 0);
                same("memory_delete_bytes", "null zero-sized answer",
                     (memory_delete_bytes)(null, 0, null), 0);
                for (positive s = 0; s < array_count(sizes); s++)
                        for (positive residue = 0; residue <= 64; residue += residue < 8 ? 1 : 7)
                                for (positive density = 0; density < 8; density++)
                                {
                                        positive size = sizes[s];
                                        positive offset = residue == 64 || size + residue > 4096
                                                              ? 4096 - size : residue;
                                        p8 address_to table = tables + 8192 - 256 - (s + residue + density) % 8;
                                        // Densities 4 to 7: one member, two, two the data does not hold,
                                        // and three, the data from eight bytes the first three of which
                                        // are the members.
                                        p8 alphabet[8];
                                        XORSHIFT64(seed);
                                        for (positive i = 0; i < 8; i++)
                                                alphabet[i] = (p8)((seed >> (8 * i)) | 1) ^ (p8)(i << 1);
                                        for (positive i = 0; i < 256; i++)
                                        {
                                                XORSHIFT64(seed);
                                                // Any nonzero byte, 0x80 (no low bit set) a quarter of the time.
                                                p8 mark = i % 4 == 0 || !(p8)(seed >> 24) ? 0x80 : (p8)(seed >> 24);
                                                p8 marked = density == 3 ? (seed & 15) != 0
                                                              : density == 2 ? (seed & 1) != 0
                                                              : density == 1 ? i == 'a' || i >= 0xf0
                                                              : density == 4 ? i == alphabet[0]
                                                              : density == 5 ? i == alphabet[0] || i == alphabet[1]
                                                              : density == 6 ? i == (p8)~alphabet[0] || i == (p8)~alphabet[1]
                                                              : density == 7 ? i == alphabet[0] || i == alphabet[1] || i == alphabet[2]
                                                                             : false;
                                                table[i] = marked ? mark : 0;
                                        }
                                        for (positive i = 0; i < 4096; i++)
                                        {
                                                XORSHIFT64(seed);
                                                got[i] = want[i] = density < 4 ? (p8)seed : alphabet[(seed >> 29) % 8];
                                        }
                                        positive kept = 0;
                                        for (positive i = 0; i < size; i++)
                                        {
                                                p8 value = want[offset + i];
                                                if (!table[value])
                                                        want[offset + kept++] = value;
                                        }
                                        VECTOR_FIRST_CALL(tier == 0);
                                        positive answer = (memory_delete_bytes)(got + offset, size, table);
                                        same("memory_delete_bytes", "kept count", answer, kept);
                                        same_bytes("memory_delete_bytes", "kept bytes and those before",
                                                   got, want, offset + kept);
                                        same_bytes("memory_delete_bytes", "bytes after the block",
                                                   got + offset + size, want + offset + size,
                                                   4096 - offset - size);
                                }
        }
#if X64
        cpu_has_avx512 = avx512;
        cpu_has_avx512_vbmi2 = vbmi2;
#elif RISCV64
        cpu_has_vector = vector;
        cpu_has_zbb = zbb;
#endif
        memory_free(pages, 3 * 4096);
        memory_free(tables, 3 * 4096);
}

/*
        memory_last_of on each x86_64 body, with AVX2 and without it (the
        SSE2 one), and on riscv64 with every call the first in a process,
        which asks for V, then without V: every size to 160 and some past it,
        the span against the page before it and against the page after it,
        so that a read before its first byte, or past its last where the
        page does not have it, faults, and the byte everywhere around the
        span; the byte absent, present once at each position, and present at
        every position up to one, which is then the answer.
*/
fn check_last_of_bodies()
{
        static const positive large[] = {255, 256, 257, 1000, 4095, 4096};
        p8 address_to pages = guard_pages("memory_last_of");

        if (!pages)
                return;
        p8 address_to page = pages + 4096;
#if X64
        p8 avx2 = cpu_has_avx2;
        positive tiers = 2;
#elif RISCV64
        p8 vector = 0;
        positive tiers = 2;
#else
        positive tiers = 1;
#endif
        for (positive tier = 0; tier < tiers; tier++)
        {
#if X64
                cpu_has_avx2 = tier == 0 ? avx2 : 0;
#elif RISCV64
                if (tier == 1)
                {
                        vector = cpu_has_vector;
                        cpu_has_vector = 1;
                }
#endif
                for (positive n = 0; n < 161 + array_count(large); n++)
                        for (positive side = 0; side < 2; side++)
                        {
                                positive size = n < 161 ? n : large[n - 161];
                                positive step = size > 200 ? 37 : 1;
                                p8 address_to block = side ? page : page + 4096 - size;

                                //      The byte is everywhere outside the span, so a body that
                                //      takes a byte past either end for it answers wrong.
                                memory_fill(page, 'y', 4096);
                                memory_fill(block, 'x', size);
                                VECTOR_FIRST_CALL(tier == 0);
                                same("memory_last_of", "absent",
                                     (positive)(memory_last_of)(block, 'y', size), 0);
                                for (positive at = 0; at < size; at += step)
                                {
                                        block[at] = 'y';
                                        VECTOR_FIRST_CALL(tier == 0 && at % 7 == 0);
                                        same("memory_last_of", "present once",
                                             (positive)(memory_last_of)(block, 'y', size), (positive)(block + at));
                                        block[at] = 'x';
                                }
                                for (positive at = 0; at < size; at += step)
                                {
                                        block[at] = 'y';
                                        same("memory_last_of", "the last of those up to a position",
                                             (positive)(memory_last_of)(block, 'y', size), (positive)(block + at));
                                }
                        }
                //      Every width to 300 at every alignment from 0 to 63, the byte absent and then
                //      present at its first, middle and last place: the quad, vector and word bodies cut
                //      their blocks by where the span starts as well as by its length.
                for (positive n = 0; n <= 300; n++)
                        for (positive align = 0; align < 64; align++)
                        {
                                p8 address_to block = page + 1024 + align;

                                memory_fill(page, 'y', 4096);
                                memory_fill(block, 'x', n);
                                same("memory_last_of", "every alignment, absent",
                                     (positive)(memory_last_of)(block, 'y', n), 0);
                                for (positive pick = 0; pick < 3 && n; pick++)
                                {
                                        positive at = pick == 0 ? 0 : pick == 1 ? n / 2 : n - 1;

                                        block[at] = 'y';
                                        same("memory_last_of", "every alignment, present",
                                             (positive)(memory_last_of)(block, 'y', n), (positive)(block + at));
                                        block[at] = 'x';
                                }
                        }
        }
#if X64
        cpu_has_avx2 = avx2;
#elif RISCV64
        cpu_has_vector = vector;
#endif
        memory_free(pages, 3 * 4096);
}

/*
        memory_last_of_either against the byte loop: every width to 300 at every alignment from 0 to 63, the two
        bytes absent, one of them present at the first, middle and last place. The four-vector turn on x86-64
        cuts its blocks by where the span starts as well as by its width.
*/
fn check_last_of_either_widths()
{
        static p8 room[448];

        for (positive size = 0; size <= 300; size++)
                for (positive off = 0; off < 64; off++)
                {
                        for (positive i = 0; i < sizeof(room); i++)
                                room[i] = 'a';

                        same("memory_last_of_either", "every width and alignment, absent",
                             (positive)memory_last_of_either(room + off, 'y', 'Z', size), 0);
                        for (positive pick = 0; pick < 3 && size; pick++)
                        {
                                positive at = pick == 0 ? 0 : pick == 1 ? size / 2 : size - 1;

                                room[off + at] = 'Z';
                                same("memory_last_of_either", "every width and alignment, present",
                                     (positive)memory_last_of_either(room + off, 'y', 'Z', size),
                                     (positive)(room + off + at));
                                room[off + at] = 'a';
                        }
                }
}

/*
        memory_nth_of and memory_nth_last_of against the byte loop: every width to 300 at every alignment from 0
        to 63, the byte absent, then present once at its first, middle and last place, asked for the first and
        for the last. The four-zmm turns on x86-64 are asked for after the first block, so the cut is by where
        the span starts as well as by its width.
*/
fn check_nth_widths()
{
        static p8 room[448];

        for (positive size = 0; size <= 300; size++)
                for (positive off = 0; off < 64; off++)
                {
                        for (positive i = 0; i < sizeof(room); i++)
                                room[i] = 'a';

                        positive2 front = (memory_nth_of)(room + off, 'z', size, 1);
                        positive2 rear = (memory_nth_last_of)(room + off, 'z', size, 1);

                        same("memory_nth_of", "every width and alignment, absent", front.x, size);
                        same("memory_nth_of", "every width and alignment, absent count", front.y, 0);
                        same("memory_nth_last_of", "every width and alignment, absent", rear.x, 0);
                        same("memory_nth_last_of", "every width and alignment, absent count", rear.y, 0);
                        for (positive pick = 0; pick < 3 && size; pick++)
                        {
                                positive at = pick == 0 ? 0 : pick == 1 ? size / 2 : size - 1;

                                room[off + at] = 'z';
                                front = (memory_nth_of)(room + off, 'z', size, 1);
                                rear = (memory_nth_last_of)(room + off, 'z', size, 1);
                                same("memory_nth_of", "every width and alignment, present", front.x, at + 1);
                                same("memory_nth_of", "every width and alignment, present count", front.y, 1);
                                same("memory_nth_last_of", "every width and alignment, present", rear.x, at);
                                same("memory_nth_last_of", "every width and alignment, present count", rear.y, 1);
                                room[off + at] = 'a';
                        }
                }
}

/*
        memory_squeeze_bytes against the byte loop tr used to run: every size
        across the vector edges, every residue, marks from none to nearly all,
        bytes drawn from a four-letter alphabet so that repeats are the rule,
        and the byte before the block absent, equal to the first byte,
        different from it, and absent as a value past 255 whose low byte is
        the first byte. The table against a page that faults, on eight and
        off it, and marks of any nonzero byte, 0x80 among them. On x86_64
        each body in turn, as for deletion, and the SSE2 blocks with AVX2
        off; on riscv64 every call the first in a process, which asks for V,
        then without V.
*/
fn check_squeeze_bytes()
{
        static const positive sizes[] = {
            0, 1, 2, 3, 4, 5, 7, 8, 31, 32, 33, 63, 64, 65, 66, 67, 127, 128, 129,
            191, 255, 256, 257, 511, 512, 513, 519, 1000, 4095, 4096,
        };
        static p8 want[4096];
        p8 address_to pages = guard_pages("memory_squeeze_bytes");
        p8 address_to tables = guard_pages("memory_squeeze_bytes' table");

        if (!pages || !tables)
        {
                if (pages)
                        memory_free(pages, 3 * 4096);
                if (tables)
                        memory_free(tables, 3 * 4096);
                return;
        }
        p8 address_to got = pages + 4096;
        p64 seed = 0x2545f4914f6cdd1dull;
#if X64
        p8 avx512 = cpu_has_avx512, vbmi2 = cpu_has_avx512_vbmi2, avx2 = cpu_has_avx2;
        positive tiers = 4;
#elif RISCV64
        p8 vector = 0;
        positive tiers = 2;
#else
        positive tiers = 1;
#endif
        for (positive tier = 0; tier < tiers; tier++)
        {
#if X64
                cpu_has_avx512 = tier < 2 ? avx512 : 0;
                cpu_has_avx512_vbmi2 = tier == 1 ? 1 : vbmi2;
                cpu_has_avx2 = tier < 3 ? avx2 : 0;
#elif RISCV64
                if (tier == 1)
                {
                        vector = cpu_has_vector;
                        cpu_has_vector = 1;
                }
#endif
                VECTOR_FIRST_CALL(tier == 0);
                same("memory_squeeze_bytes", "null zero-sized answer",
                     (memory_squeeze_bytes)(null, 0, null, 256), 0);
                for (positive s = 0; s < array_count(sizes); s++)
                        for (positive residue = 0; residue <= 64; residue += residue < 8 ? 1 : 7)
                                for (positive density = 0; density < 7; density++)
                                        for (positive before = 0; before < 4; before++)
                                {
                                        positive size = sizes[s];
                                        positive offset = residue == 64 || size + residue > 4096
                                                              ? 4096 - size : residue;
                                        p8 address_to table = tables + 8192 - 256 - (s + residue + before) % 8;
                                        for (positive i = 0; i < 256; i++)
                                        {
                                                XORSHIFT64(seed);
                                                // Any nonzero byte, 0x80 (no low bit set) a quarter of the time.
                                                p8 mark = i % 4 == 0 || !(p8)(seed >> 24) ? 0x80 : (p8)(seed >> 24);
                                                // Densities 4 to 6 are runs of marks, as SSE2 takes them
                                                // apart from 256 bytes: three runs, four runs from byte
                                                // 0 to byte 255 (the last of them sixteen long, past
                                                // 0xf1), and one run of every byte.
                                                p8 marked = density == 6 ? true
                                                              : density == 5 ? i == 0 || i == ' ' || i == 'a' || i == 'b' || i >= 0xf0
                                                              : density == 4 ? i == ' ' || (i >= 'a' && i <= 'b') || (i >= 0xf0 && i <= 0xf2)
                                                              : density == 3 ? (seed & 15) != 0
                                                              : density == 2 ? (seed & 1) != 0
                                                              : density == 1 ? i == 'a' || i == 0xf1
                                                                             : false;
                                                table[i] = marked ? mark : 0;
                                        }
                                        static const p8 alphabet[4] = {'a', 'b', ' ', 0xf1};
                                        for (positive i = 0; i < 4096; i++)
                                        {
                                                XORSHIFT64(seed);
                                                got[i] = want[i] = (seed >> 40) % 5 ? alphabet[(seed >> 20) & 3]
                                                                                    : (p8)seed;
                                        }
                                        positive previous = before == 0 ? 256
                                                            : before == 1 && size ? want[offset]
                                                            : before == 3 ? 0x100 | (size ? want[offset] : 'a')
                                                                          : 'z';
                                        positive kept = 0;
                                        positive last = previous;
                                        for (positive i = 0; i < size; i++)
                                        {
                                                p8 value = want[offset + i];
                                                if (!(table[value] && value == last))
                                                        want[offset + kept++] = value;
                                                last = value;
                                        }
                                        VECTOR_FIRST_CALL(tier == 0);
                                        positive answer = (memory_squeeze_bytes)(got + offset, size,
                                                                                table, previous);
                                        same("memory_squeeze_bytes", "kept count", answer, kept);
                                        same_bytes("memory_squeeze_bytes", "kept bytes and those before",
                                                   got, want, offset + kept);
                                        same_bytes("memory_squeeze_bytes", "bytes after the block",
                                                   got + offset + size, want + offset + size,
                                                   4096 - offset - size);
                                }
        }
#if X64
        cpu_has_avx512 = avx512;
        cpu_has_avx512_vbmi2 = vbmi2;
        cpu_has_avx2 = avx2;
#elif RISCV64
        cpu_has_vector = vector;
#endif
        memory_free(pages, 3 * 4096);
        memory_free(tables, 3 * 4096);
}

/*
        memory_offsets_of_either against a byte loop: every size across the
        vector edges and every residue, delimiter densities from none to all,
        the two bytes equal and different, and limits from zero to past the
        count, so that stopping at the limit and resuming after the last
        offset is exercised at every vector position. On x86_64 with and
        without AVX-512. On riscv64 with every call the first in a process,
        which asks for V, then the word body on the processor's Zbb and
        without it.
*/
fn check_offsets_of_either()
{
        static const positive sizes[] = {1000, 4096};
        static p32 got[4200], want[4200];
        p8 address_to pages = guard_pages("memory_offsets_of_either");

        if (!pages)
                return;
        p8 address_to bytes = pages + 4096;
        p64 seed = 0x243f6a8885a308d3ull;
#if X64
        // AVX-512 with VBMI2 and without it, AVX2, and SSE2.
        p8 avx512 = cpu_has_avx512, vbmi2 = cpu_has_avx512_vbmi2, avx2 = cpu_has_avx2;
        positive tiers = 4;
#elif RISCV64
        // Every call the first in a process, which asks for V and comes back
        // to the routine's entry; then the word body without V, on the
        // processor's Zbb and then without.
        p8 vector = 0, zbb = cpu_has_zbb;
        positive tiers = 3;
#else
        positive tiers = 1;
#endif
        for (positive tier = 0; tier < tiers; tier++)
        {
#if X64
                cpu_has_avx512 = tier < 2 ? avx512 : 0;
                cpu_has_avx512_vbmi2 = tier == 1 ? 1 : vbmi2;
                cpu_has_avx2 = tier < 3 ? avx2 : 0;
#elif RISCV64
                if (tier == 1)
                {
                        vector = cpu_has_vector;
                        cpu_has_vector = 1;
                }
                if (tier == 2)
                {
                        if (!zbb)
                                continue;
                        cpu_has_zbb = 0;
                }
#endif
                VECTOR_FIRST_CALL(tier == 0);
                same("memory_offsets_of_either", "no room",
                     (memory_offsets_of_either)(got, bytes, 16, 'a', 'b', 0), 0);
                // Every size to 200, so that each short block, each wide loop
                // tail (a record over 63 bytes must not be scanned twice) and
                // the masked block that answers them is met at every length.
                for (positive s = 0; s < 201 + array_count(sizes); s++)
                        for (positive residue = 0; residue <= 64; residue++)
                                for (positive density = 0; density < 4; density++)
                                {
                                        positive size = s <= 200 ? s : sizes[s - 201];
                                        positive offset = residue == 64 || size + residue > 4096
                                                              ? 4096 - size : residue;
                                        p8 first = density == 3 ? 'x' : ',';
                                        p8 second = density == 2 ? ',' : '\n';

                                        // Only the window and the sixty four bytes round it are
                                        // drawn afresh, so that every residue costs what its size does.
                                        positive from = offset > 64 ? offset - 64 : 0;
                                        positive to = offset + size + 64 < 4096 ? offset + size + 64 : 4096;

                                        for (positive i = from; i < to; i++)
                                        {
                                                XORSHIFT64(seed);
                                                positive pick = (positive)(seed % 16);
                                                bytes[i] = density == 0 ? (p8)('a' + pick % 8)
                                                           : density == 3 ? (pick < 12 ? 'x' : '\n')
                                                           : pick == 0 ? ',' : pick == 1 ? '\n'
                                                                                         : (p8)(seed >> 8);
                                        }
                                        positive count = 0;
                                        for (positive i = 0; i < size; i++)
                                                if (bytes[offset + i] == first || bytes[offset + i] == second)
                                                        want[count++] = (p32)i;

                                        static const positive limits[] = {1, 2, 7, 16, 63, 64, 65, 256, 5000};
                                        for (positive l = 0; l < array_count(limits); l++)
                                        {
                                                positive limit = limits[l];
                                                positive have = 0;
                                                positive from = 0;
                                        p8 ok = true;
                                                // Walk the whole block limit at a time, as a caller does.
                                                for (;;)
                                                {
                                                        // The bodies that write offsets eight or sixteen at a go write
                                                        // past the last hit, and must stay inside the limit they were
                                                        // given: whatever lies beyond it is the caller's.
                                                        p8 fenced = limit + 80 <= array_count(got);
                                                        if (fenced)
                                                                for (positive i = 0; i < 80; i++)
                                                                        got[limit + i] = 0xdeadbeefu;

                                                        VECTOR_FIRST_CALL(tier == 0);
                                                        positive answer = (memory_offsets_of_either)(
                                                            got, bytes + offset + from, size - from,
                                                            first, second, limit);
                                                        if (answer > limit)
                                                        {
                                                                ok = false;
                                                                break;
                                                        }
                                                        if (fenced)
                                                                for (positive i = 0; i < 80; i++)
                                                                        if (got[limit + i] != 0xdeadbeefu)
                                                                                ok = false;
                                                        for (positive i = 0; i < answer; i++)
                                                                if (have + i >= count ||
                                                                    got[i] + from != want[have + i])
                                                                        ok = false;
                                                        have += answer;
                                                        if (answer < limit || !ok)
                                                                break;
                                                        from += got[answer - 1] + 1;
                                                }
                                                same("memory_offsets_of_either", "offsets", ok, 1);
                                                same("memory_offsets_of_either", "count", have, count);
                                        }
                                }
        }
#if X64
        cpu_has_avx512 = avx512;
        cpu_has_avx512_vbmi2 = vbmi2;
        cpu_has_avx2 = avx2;
#elif RISCV64
        cpu_has_vector = vector;
        cpu_has_zbb = zbb;
#endif
        memory_free(pages, 3 * 4096);
}

/*
        memory_span_byte against the loop it answers for, one block at a time:
        the block is size bytes of the fill, with up to sixty four bytes of
        another value on either side inside the page between the guards, and
        a byte one bit off the fill goes to every place in turn and then
        nowhere. A second byte off the fill by another bit follows it by one
        to fifteen, so it mostly shares the first's word or block, and an
        answer that names the last difference in a word rather than the first
        is wrong. The bits turn over with the place. Answers that are not the
        byte loop's over the same bytes are counted, and the first few said.

        The byte arrives with bits above its eight set, through a pointer
        that passes it as a whole register, where the calling convention
        leaves those bits undefined: AAPCS64 and the x86_64 one do, and GCC
        on arm64 passes x + 1 for a byte as it stands. RISC-V's makes the
        caller extend it, so there the byte arrives clean.
*/
static p8 address_to span_byte_page;
static positive span_byte_wrong, span_byte_calls;
typedef positive (*span_byte_register)(address_any, positive, positive);
static span_byte_register volatile span_byte_call =
    (span_byte_register)(address_any)memory_span_byte;

static positive span_byte_reference(p8 address_to at, p8 value, positive size)
{
        positive equal = 0;

        while (equal < size && at[equal] == value)
                equal++;

        return equal;
}

static fn span_byte_block(p8 address_to at, positive size, p8 fill, p8 around,
                          positive turn)
{
        p8 address_to from = at - span_byte_page >= 64 ? at - 64 : span_byte_page;
        p8 address_to top = span_byte_page + 4096;
        p8 address_to to = top - (at + size) >= 64 ? at + size + 64 : top;

        reference_fill(from, around, (positive)(at - from));
        reference_fill(at, fill, size);
        reference_fill(at + size, around, (positive)(to - at - size));
        for (positive place = 0; place <= size; place++)
        {
                positive later = place + 1 + turn % 15;

                if (place < size)
                        at[place] = (p8)(fill ^ (1u << ((place + turn) & 7)));
                if (later < size)
                        at[later] = (p8)(fill ^ (1u << ((place + turn + 3) & 7)));
#if RISCV64
                positive above = 0;
#else
                positive above = (((place + turn) & 0xff) + 1) << 8;
#endif
                positive want = span_byte_reference(at, fill, size);
                positive got = span_byte_call(at, fill | above, size);
                span_byte_calls++;
                if (got != want && span_byte_wrong++ < 8)
                        string_format(log, "  FAIL memory_span_byte size %p at page+%p "
                                           "fill %p around %p: got %p want %p\n",
                                      size, (positive)(at - span_byte_page), (positive)fill,
                                      (positive)around, got, want);
                if (place < size)
                        at[place] = fill;
                if (later < size)
                        at[later] = fill;
        }
}

/*
        memory_copy_strip_16 against the loop it is, a row at a time and sixteen bytes a row: no row to nine, and the
        counts round sixteen, thirty-two and forty (so the four-row pass, the pair after it and the last row are each met
        with every remainder), strides of the destination from none (every row on the first) through ones nearer
        than a row to a strip's, strides of the source from none to an atlas's, every alignment of each address against
        an aligned other and a few with both odd, the bytes random. Each case is placed twice in pages with a guard at
        both ends, with the first row's first byte the page's first and with the last row's last byte its last, for the
        source and for the destination, so a byte read or written before the first row or past the last one's sixteenth
        is a fault; and the destination is compared with the loop's over the whole page, so a byte written between the
        rows or beside them fails. On riscv64 with V, then with V walked down to the doubleword and byte bodies.
*/
static fn copy_strip_16_reference(p8 address_to into, positive into_stride, const p8 address_to from, positive from_stride,
                                  positive rows)
{
        for (positive r = 0; r < rows; r++)
                for (positive k = 0; k < 16; k++)
                        into[r * into_stride + k] = from[r * from_stride + k];
}

fn check_copy_strip_16()
{
        static const positive counts[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31, 32, 33, 40};
        static const positive into_strides[] = {0, 1, 7, 8, 15, 16, 17, 24, 32, 40, 96};
        static const positive from_strides[] = {0, 1, 8, 16, 17, 24, 64, 80};
        static p8 want[4096];
        p8 address_to from_pages = guard_pages("memory_copy_strip_16");
        p8 address_to into_pages = guard_pages("memory_copy_strip_16's rows");
        p64 seed = 0x9e3779b97f4a7c15ull;
        positive tiers = 1;

        if (!from_pages || !into_pages)
                return;
        p8 address_to source = from_pages + 4096, address_to made = into_pages + 4096;
#if RISCV64
        p8 vector_asked;

        memory_copy_strip_16(made, 16, source, 16, 1);
        vector_asked = cpu_has_vector;
        tiers = 2;
#endif
        for (positive tier = 0; tier < tiers; tier++)
        {
                p8 rows_same = true, around = true;
#if RISCV64
                if (tier == 0 && vector_asked != 2)
                        continue;
                cpu_has_vector = tier == 0 ? vector_asked : 1;
#endif
                for (positive c = 0; c < array_count(counts); c++)
                        for (positive i = 0; i < array_count(into_strides); i++)
                                for (positive f = 0; f < array_count(from_strides); f++)
                                        for (positive pair = 0; pair < 36; pair++)
                                                for (positive place = 0; place < 2; place++)
                                                {
                                                        positive rows = counts[c], into_stride = into_strides[i], from_stride = from_strides[f];
                                                        positive a = pair < 16 ? pair : pair < 32 ? 0 : (pair - 32) * 4 + 1;
                                                        positive b = pair < 16 ? 0 : pair < 32 ? pair - 16 : (pair - 32) * 2 + 3;
                                                        positive from_span = rows ? (rows - 1) * from_stride + 16 : 0;
                                                        positive into_span = rows ? (rows - 1) * into_stride + 16 : 0;
                                                        // Up against the front guard: no residue, the first byte the page's first. Against the back one:
                                                        // the last byte the page's last, and the residue is what that leaves.
                                                        positive from_at = place ? 4096 - from_span : a;
                                                        positive into_at = place ? 4096 - into_span : b;

                                                        if (from_at + from_span > 4096 || into_at + into_span > 4096)
                                                                continue;
                                                        for (positive k = 0; k < 4096; k += 8)
                                                        {
                                                                XORSHIFT64(seed);
                                                                memory_copy(source + k, &seed, 8);
                                                        }
                                                        memory_fill(made, (p8)(0x5a + c + pair), 4096);
                                                        memory_fill(want, (p8)(0x5a + c + pair), 4096);
                                                        copy_strip_16_reference(want + into_at, into_stride, source + from_at, from_stride, rows);
                                                        memory_copy_strip_16(made + into_at, into_stride, source + from_at, from_stride, rows);
                                                        if (memory_compare(made + into_at, want + into_at, into_span) != 0)
                                                                rows_same = false;
                                                        if (memory_compare(made, want, 4096) != 0)
                                                                around = false;
                                                }
                same("memory_copy_strip_16", tier ? "rows are the loop's, the narrow bodies" : "rows are the loop's", rows_same, 1);
                same("memory_copy_strip_16", tier ? "nothing beside the rows is written, the narrow bodies" : "nothing beside the rows is written", around, 1);
        }
#if RISCV64
        cpu_has_vector = vector_asked;
#endif
        memory_free(from_pages, 3 * 4096);
        memory_free(into_pages, 3 * 4096);
}

/*
        Every size to 300 and every residue of sixty four at the start, the
        block once flush against a guard page before it and once with its end
        flush against one after it, so at a residue of zero a read one byte
        outside faults. The bytes around it equal the fill at even residues
        and are its complement at odd ones, so a byte read outside that is
        counted is wrong either way. The fill turns over with size and
        residue: zero, all ones, the top bit alone, all but it, and two
        characters callers skip. Then, to forty bytes, the sizes callers
        mostly ask, every fill against every bit at every place. On x86_64
        once for each body the machine has, AVX-512, AVX2 and neither, since
        the wide one hands its last bytes back to the sixteen-byte blocks.
*/
fn check_span_byte()
{
        static const p8 fills[] = {0x00, 0xff, 0x80, 0x7f, '0', ' '};
        p8 address_to pages = guard_pages("memory_span_byte");

        if (!pages)
                return;
        span_byte_page = pages + 4096;
#if X64
        p8 avx2 = cpu_has_avx2, avx512 = cpu_has_avx512;
        positive tiers = 3;
#else
        positive tiers = 1;
#endif
        for (positive tier = 0; tier < tiers; tier++)
        {
#if X64
                cpu_has_avx512 = tier == 0 ? avx512 : 0;
                cpu_has_avx2 = tier == 2 ? 0 : avx2;
#endif
                span_byte_wrong = span_byte_calls = 0;
                for (positive size = 0; size <= 300; size++)
                        for (positive residue = 0; residue < 64; residue++)
                                for (positive flush = 0; flush < 2; flush++)
                                {
                                        p8 fill = fills[(size + residue) % array_count(fills)];
                                        p8 around = residue & 1 ? (p8)~fill : fill;
                                        p8 address_to at =
                                            flush ? span_byte_page + 4096 - size - residue
                                                  : span_byte_page + residue;

                                        span_byte_block(at, size, fill, around, residue + flush);
                                }
                same("memory_span_byte", "every size, residue and place", span_byte_wrong, 0);
                same("memory_span_byte", "cases", span_byte_calls, 45451 * 128);

                span_byte_wrong = span_byte_calls = 0;
                for (positive size = 0; size <= 40; size++)
                        for (positive residue = 0; residue < 64; residue++)
                                for (positive flush = 0; flush < 2; flush++)
                                        for (positive f = 0; f < array_count(fills); f++)
                                                for (positive bit = 0; bit < 8; bit++)
                                                {
                                                        p8 fill = fills[f];
                                                        p8 around = residue & 1 ? (p8)~fill : fill;
                                                        p8 address_to at =
                                                            flush ? span_byte_page + 4096 - size - residue
                                                                  : span_byte_page + residue;

                                                        span_byte_block(at, size, fill, around, bit);
                                                }
                same("memory_span_byte", "every fill and bit to forty", span_byte_wrong, 0);
                same("memory_span_byte", "short cases", span_byte_calls, 861 * 128 * 6 * 8);
        }
#if X64
        cpu_has_avx2 = avx2;
        cpu_has_avx512 = avx512;
#endif
        memory_free(pages, 3 * 4096);
}

/*
        Every routine here that takes an argument narrower than a register,
        asked twice on the same input: once through its prototype, which
        hands the argument over as the calling convention says, and once
        through a pointer that passes a whole register with bits set above
        the argument's width. The answers, and every byte either call wrote,
        must be the same.

        AAPCS64 and the x86_64 convention leave the bits above a byte, a
        half, a bool and an int undefined, and GCC uses the licence: on arm64
        it passes x + 1 for a byte as it stands, and on either it passes a
        long cut to an int as the long. memory_span_byte compared all of w1
        until the check above it said so. RISC-V's convention makes the
        caller extend, and a signed byte with its sign, so there the second
        call passes a b8 zero extended instead and every other argument as
        the convention has it. That is stricter than the convention, on
        purpose: what is asked of a routine that takes a b8 is that it reads
        its byte however the byte arrives, and the riscv64 record count
        compared a sign extended delimiter against bytes loaded unsigned. On
        every machine the C names' int is asked as well, whose bits eight to
        thirty one are the caller's to set and the routine's to drop:
        strchr(s, 0x141) is strchr(s, 'A'), and a char of 0xe9 arrives as
        0xffffffe9, which riscv64 extends to the whole register.

        The junk is one of five shapes, picked at random a call: a few bits
        just above the width, all ones above it (a sign extension's), bits
        32 to 63 alone (a long cut to an int), random, and the one bit just
        above. An int takes the same shapes above bit 31, and a bool
        arrives as 0, 1, 2 or 0x80 under them. A narrow answer is compared
        at its width. A block a routine reads is laid at every residue of
        sixty four up to seventy two bytes and at eight residues past that,
        so the byte, word and vector paths and their tails each meet the
        argument; an output is written twice, into two buffers laid out and
        filled alike, and compared whole with the bytes around it. On x86_64
        once per body the machine has: AVX-512 with VBMI, AVX-512 without,
        AVX2, and neither.

        The same source asks a kernel object's bodies when built with
        VERIFY_NARROW_KERNEL, which the verify lane does on each machine
        (kernel_narrow_one in test/run): lib.c compiled as the kernel vector
        audits compile it, every symbol prefixed kernel_, linked beside the
        library. The KERNEL_MODE bodies are then the ones called, which
        nothing else runs outside a kernel, Canvas's pixel routines among
        them; one the object does not carry is said and skipped.
*/
#define DIRTY_LIBRARY(X)                                                       \
        X(memory_first_of) X(memory_last_of) X(memory_count)                  \
        X(memory_span_byte) X(memory_span_byte_reverse)                       \
        X(memory_first_of_ascii_case) X(memory_fill)                          \
        X(memory_fill_32) X(memory_fill_u32) X(memory_copy_until)             \
        X(memory_offsets_of_either) X(memory_offsets_between)                 \
        X(memory_offsets_outside) X(memory_escape_index)                      \
        X(memory_offsets_fields_blank) X(memory_nth_of)                       \
        X(memory_nth_last_of) X(memory_last_of_either)                        \
        X(memory_into_escaped) X(memory_into_spelled)                         \
        X(memory_search_prepare)                                              \
        X(memory_count_records_with_prepared) X(memory_count_words)           \
        X(string_first_of) X(string_first_of_or_end) X(string_first_of_max)   \
        X(string_first_of_either_or_end)                                     \
        X(string_last_of) X(string_last_of_or_end) X(string_cut)              \
        X(string_replace_all) X(writer_fill) X(writer_field)                  \
        X(string_to_field) X(positive_to_padded) X(positive_into_padded)      \
        X(positive_into_base) X(positive_into_human_nearest_string)           \
        X(buffered_write_byte) X(network_store_16) X(bytes_reverse_16)        \
        X(network_store_32) X(bytes_reverse_32) X(host_into)                  \
        X(byte_class_holds) X(byte_is_alnum) X(byte_is_alpha)                 \
        X(byte_is_ascii) X(byte_is_blank) X(byte_is_control)                  \
        X(byte_is_digit) X(byte_is_graphic) X(byte_is_hexadecimal)            \
        X(byte_is_lower) X(byte_is_printable) X(byte_is_punctuation)          \
        X(byte_is_space) X(byte_is_upper) X(byte_to_ascii) X(byte_to_lower)   \
        X(byte_to_upper) X(hash_crc32) X(hash_crc32_msb) X(hash_crc32c)      \
        X(memory_checksum_bsd16) X(unicode_width) X(unicode_case)             \
        X(bits_first_set)                                                     \
        X(absolute_whole) X(wait_status_code_base) X(string_to_number)        \
        X(string_to_number_unsigned) X(string_to_number_checked)              \
        X(string_to_number_unsigned_checked) X(program_argument)              \
        X(program_environment) X(jump_to_mark) X(signal_jump_mark)            \
        X(signal_jump_to_mark) X(memchr) X(memrchr) X(memccpy) X(memset)      \
        X(strchr) X(strrchr) X(strchrnul) X(strnchr) X(memory_utf8_valid_span) X(limbs_compare) X(memory_ascii_span)

#ifdef VERIFY_NARROW_KERNEL
#define DIRTY_ROUTINE(name) kernel_##name
#define DIRTY_KERNEL(name) extern typeof(name) kernel_##name __attribute__((weak));
DIRTY_LIBRARY(DIRTY_KERNEL)

//      Canvas's pixel routines, which only a kernel object carries, and
//      every colour they take is a u32. The cell is window.c's.
struct dirty_cell
{
        p32 character;
        p8 ink, paper;
        p16 flags;
};
#define DIRTY_CANVAS(X)                                                        \
        X(canvas_rect_fill) X(canvas_rect_fill_wide) X(canvas_row_blit)       \
        X(canvas_row_blit_wide) X(canvas_glyph) X(canvas_glyph2)              \
        X(canvas_glyph_wide) X(canvas_cell) X(canvas_cell2)                   \
        X(canvas_cell_wide) X(canvas_cell2_wide) X(canvas_cells)              \
        X(canvas_cells_wide) X(canvas_glyph2_wide)
#define DIRTY_RECT(name)                                                       \
        extern fn kernel_##name(p32 address_to, positive, positive, positive, \
                                p32) __attribute__((weak));
#define DIRTY_BLIT(name)                                                       \
        extern fn kernel_##name(p32 address_to, const p32 address_to,         \
                                positive, p32) __attribute__((weak));
#define DIRTY_GLYPH(name)                                                      \
        extern fn kernel_##name(p32 address_to, positive, const p8 address_to,\
                                positive, positive, p32) __attribute__((weak));
#define DIRTY_CELL(name)                                                       \
        extern fn kernel_##name(p32 address_to, positive, const p8 address_to,\
                                positive, p32, p32) __attribute__((weak));
#define DIRTY_CELLS(name)                                                      \
        extern fn kernel_##name(p32 address_to, positive, const p8 address_to,\
                                const struct dirty_cell address_to, positive, \
                                p32, p32) __attribute__((weak));
DIRTY_RECT(canvas_rect_fill)
DIRTY_RECT(canvas_rect_fill_wide)
DIRTY_BLIT(canvas_row_blit)
DIRTY_BLIT(canvas_row_blit_wide)
DIRTY_GLYPH(canvas_glyph)
DIRTY_GLYPH(canvas_glyph2)
DIRTY_GLYPH(canvas_glyph_wide)
DIRTY_GLYPH(canvas_glyph2_wide)
DIRTY_CELL(canvas_cell)
DIRTY_CELL(canvas_cell2)
DIRTY_CELL(canvas_cell_wide)
DIRTY_CELL(canvas_cell2_wide)
DIRTY_CELLS(canvas_cells)
DIRTY_CELLS(canvas_cells_wide)
#else
#define DIRTY_ROUTINE(name) name
#define DIRTY_CANVAS(X)
#endif
#define DIRTY_LIST(X) DIRTY_LIBRARY(X) DIRTY_CANVAS(X)

#ifdef VERIFY_NARROW_KERNEL
/*
        What an arm64 kernel body ends in a branch to -- memset, strchr,
        strrchr, strlen, memcpy and memmove -- is the kernel's own, which is
        not in the object. These stand in for them as lib/string.c writes
        them, cutting the character to a char as C says, so a body is asked
        about what it does before the branch. Weak, so an object that
        carries its own, as x86_64's does, keeps it.
*/
#define DIRTY_STAND_IN KEEP __attribute__((externally_visible, weak))
DIRTY_STAND_IN char address_to kernel_strchr(const char address_to s, int c)
{
        for (; *s != (char)c; s++)
                if (!*s)
                        return null;
        return (char address_to)s;
}
DIRTY_STAND_IN char address_to kernel_strrchr(const char address_to s, int c)
{
        const char address_to last = null;

        do
                if (*s == (char)c)
                        last = s;
        while (*s++);
        return (char address_to)last;
}
DIRTY_STAND_IN positive kernel_strlen(const char address_to s)
{
        positive length = 0;

        while (s[length])
                length++;
        return length;
}
DIRTY_STAND_IN void address_to kernel_memset(void address_to to, int c, sized size)
{
        for (sized i = 0; i < size; i++)
                ((char address_to)to)[i] = (char)c;
        return to;
}
DIRTY_STAND_IN void address_to kernel_memmove(void address_to to,
                                              const void address_to from,
                                              sized size)
{
        return memory_copy(to, from, size);
}
DIRTY_STAND_IN void address_to kernel_memcpy(void address_to to,
                                             const void address_to from,
                                             sized size)
{
        return memory_copy(to, from, size);
}
#endif
#define DIRTY_HAS(name) ((address_any)DIRTY_ROUTINE(name) != null)

#define DIRTY_ENUM(name) DIRTY_##name,
enum { DIRTY_LIST(DIRTY_ENUM) DIRTY_ROUTINES };
#define DIRTY_NAME(name) (string_address) #name,
static const string_address dirty_names[DIRTY_ROUTINES] = { DIRTY_LIST(DIRTY_NAME) };
#define DIRTY_ADDRESS(name) (address_any)DIRTY_ROUTINE(name),
static address_any const dirty_addresses[DIRTY_ROUTINES] = { DIRTY_LIST(DIRTY_ADDRESS) };

static positive dirty_compared[DIRTY_ROUTINES], dirty_wrong[DIRTY_ROUTINES];
static positive dirty_either_compared, dirty_either_wrong;
static positive dirty_tier;
static p64 dirty_seed = 0x6a09e667f3bcc909ull;

static positive dirty_random(void)
{
        XORSHIFT64(dirty_seed);
        return (positive)dirty_seed;
}

//      A value of `bits` with junk above it, in one of the five shapes
//      picked at random: a turn count would line up with the number of
//      calls a case makes, and a routine would meet one shape every time.
static positive dirty(positive value, positive bits)
{
#if RISCV64
        //      The byte zero extended, which the convention would have
        //      signed for a b8; the half and the int as it has them.
        return bits == 32 ? (positive)(bipolar)(b32)value
                          : value & (((positive)1 << bits) - 1);
#endif
        positive keep = ((positive)1 << bits) - 1;
        positive turn = dirty_random();
        positive shape = turn % 5;
        positive junk = shape == 0   ? ((turn >> 8 & 0xff) + 1) << bits
                        : shape == 1 ? ~keep
                        : shape == 2 ? (positive)0xa5c3e18700000000ull
                        : shape == 3 ? dirty_random()
                                     : keep + 1;

        junk &= ~keep;
        return (value & keep) | (junk ? junk : keep + 1);
}
#define D8(v) dirty((positive)(v), 8)
#define D16(v) dirty((positive)(v), 16)
#define D32(v) dirty((positive)(v), 32)

//      The clean side: a narrow value read back from memory of its own
//      type, so the register holds exactly what the type says, whatever
//      the expression that made it left in a wider one. Two slots of each,
//      for a call with two such arguments.
static volatile p8 dirty_p8_slot[2];
static volatile b8 dirty_b8_slot[2];
static volatile p16 dirty_p16_slot;
static volatile p32 dirty_p32_slot;
static volatile b32 dirty_b32_slot[2];
#define C8(v) (dirty_p8_slot[0] = (p8)(v), dirty_p8_slot[0])
#define C8B(v) (dirty_p8_slot[1] = (p8)(v), dirty_p8_slot[1])
#define CS8(v) (dirty_b8_slot[0] = (b8)(v), dirty_b8_slot[0])
#define CS8B(v) (dirty_b8_slot[1] = (b8)(v), dirty_b8_slot[1])
#define C16(v) (dirty_p16_slot = (p16)(v), dirty_p16_slot)
#define C32(v) (dirty_p32_slot = (p32)(v), dirty_p32_slot)
#define CS32(v) (dirty_b32_slot[0] = (b32)(v), dirty_b32_slot[0])
#define CS32B(v) (dirty_b32_slot[1] = (b32)(v), dirty_b32_slot[1])

//      The dirty side: the routine's address laundered through memory, so
//      nothing about the call is known but the register-wide type it is
//      made through.
static address_any volatile dirty_slot;
static address_any dirty_launder(address_any routine)
{
        dirty_slot = routine;
        return dirty_slot;
}
typedef positive (*dirty_1)(positive);
typedef positive (*dirty_2)(positive, positive);
typedef positive (*dirty_3)(positive, positive, positive);
typedef positive (*dirty_4)(positive, positive, positive, positive);
typedef positive (*dirty_5)(positive, positive, positive, positive, positive);
typedef positive (*dirty_6)(positive, positive, positive, positive, positive,
                            positive);
typedef positive (*dirty_7)(positive, positive, positive, positive, positive,
                            positive, positive);
typedef positive2 (*dirty_pair_2)(positive, positive);
typedef positive2 (*dirty_pair_3)(positive, positive, positive);
typedef positive2 (*dirty_pair_4)(positive, positive, positive, positive);
typedef positive2 (*dirty_pair_5)(positive, positive, positive, positive,
                                  positive);
#define DIRTY(arity, name) \
        ((dirty_##arity)dirty_launder((address_any)DIRTY_ROUTINE(name)))
#define P(x) ((positive)(x))

static fn dirty_agree(positive routine, positive clean, positive answer,
                      positive where)
{
        dirty_compared[routine]++;
        if (clean != answer && dirty_wrong[routine]++ < 4)
                string_format(log, "  FAIL %s: %p through its prototype, %p "
                                   "with the bits above a narrow argument "
                                   "changed (case %p, tier %p)\n",
                              dirty_names[routine], clean, answer, where,
                              dirty_tier);
}
#define AGREE(name, clean, answer, where)                                     \
        dirty_agree(DIRTY_##name, (positive)(clean), (positive)(answer),      \
                    (positive)(where))

//      Where two outputs first differ, counted from one; nought when they
//      do not.
static positive dirty_differ(p8 address_to one, p8 address_to two,
                             positive size)
{
        for (positive i = 0; i < size; i++)
                if (one[i] != two[i])
                        return i + 1;
        return 0;
}

//      An address as a distance into its own buffer, so the two calls'
//      answers compare; null stays apart from every distance.
static positive dirty_offset(address_any answer, p8 address_to base)
{
        return answer ? (positive)((p8 address_to)answer - base) : (positive)-1;
}

#define DIRTY_ROOM 4608
static p8 dirty_in[DIRTY_ROOM] __attribute__((aligned(64)));
static p8 dirty_one[DIRTY_ROOM] __attribute__((aligned(64)));
static p8 dirty_two[DIRTY_ROOM] __attribute__((aligned(64)));
static p32 dirty_at_one[4200], dirty_at_two[4200];

static const p8 dirty_values[] = {0x00, 0x01, 0x02, '\n', ' ', '"', '0', 'A', 'a',
                                  'z', 0x7f, 0x80, 0xa0, 0xe9, 0xff};
static const positive dirty_long_sizes[] = {79, 80, 95, 96, 97, 127, 128,
                                            129, 191, 192, 255, 256, 257, 300,
                                            511, 512, 513, 1000, 4000};
static const positive dirty_long_residues[] = {0, 1, 7, 15, 17, 31, 33, 63};

//      Every size to seventy two at every residue of sixty four, then the
//      longer sizes up to largest at eight residues.
static fn dirty_plane(fn (*one)(positive size, positive residue,
                                positive turn),
                      positive largest)
{
        positive turn = 0;

        for (positive size = 0; size <= 72; size++)
                for (positive residue = 0; residue < 64; residue++)
                        one(size, residue, turn++);
        for (positive s = 0; s < array_count(dirty_long_sizes); s++)
                if (dirty_long_sizes[s] <= largest)
                        for (positive r = 0; r < array_count(dirty_long_residues); r++)
                                one(dirty_long_sizes[s], dirty_long_residues[r],
                                    turn++);
}

//      A byte that is neither value nor, for a letter, its other case.
static p8 dirty_other(p8 value, p8 nonzero)
{
        for (;;)
        {
                p8 byte = (p8)dirty_random();

                if (byte != value && byte != (p8)(value ^ 0x20) &&
                    (byte || !nonzero))
                        return byte;
        }
}

//      Once per body the machine has.
static fn dirty_tiers(fn (*each)(void))
{
#if X64 && !defined(VERIFY_NARROW_KERNEL)
        p8 avx2 = cpu_has_avx2, avx512 = cpu_has_avx512;
        p8 vbmi = cpu_has_avx512_vbmi, vbmi2 = cpu_has_avx512_vbmi2;

        for (dirty_tier = 0; dirty_tier < 4; dirty_tier++)
        {
                cpu_has_avx512 = dirty_tier < 2 ? avx512 : 0;
                cpu_has_avx512_vbmi = dirty_tier == 0 ? vbmi : 0;
                cpu_has_avx512_vbmi2 = dirty_tier == 0 ? vbmi2 : 0;
                cpu_has_avx2 = dirty_tier < 3 ? avx2 : 0;
                each();
        }
        cpu_has_avx2 = avx2;
        cpu_has_avx512 = avx512;
        cpu_has_avx512_vbmi = vbmi;
        cpu_has_avx512_vbmi2 = vbmi2;
#else
        dirty_tier = 0;
        each();
#endif
}

/*
        The byte hunts, over one arrangement: bytes that are neither the
        value nor its other case, the value at a random place and again at a
        stride after it, and on odd turns its other case just after that.
*/
static fn dirty_hunts_one(positive size, positive residue, positive turn)
{
        p8 address_to at = dirty_in + 64 + residue;
        p8 value = dirty_values[turn % array_count(dirty_values)];
        positive place = dirty_random() % (size + 1);
        positive where = size << 16 | residue << 8 | value;

        for (positive i = 0; i < size + 128; i++)
                (at - 64)[i] = dirty_other(value, false);
        for (positive i = place; i < size; i += 11 + turn % 5)
                at[i] = value;
        if ((turn & 1) && place + 3 < size)
                at[place + 3] = (p8)(value ^ 0x20);

        if (DIRTY_HAS(memory_first_of))
                AGREE(memory_first_of,
                      DIRTY_ROUTINE(memory_first_of)(at, CS8(value), size),
                      DIRTY(3, memory_first_of)(P(at), D8(value), size), where);
        if (DIRTY_HAS(memory_last_of))
                AGREE(memory_last_of,
                      DIRTY_ROUTINE(memory_last_of)(at, CS8(value), size),
                      DIRTY(3, memory_last_of)(P(at), D8(value), size), where);
        if (DIRTY_HAS(memory_count))
                AGREE(memory_count,
                      DIRTY_ROUTINE(memory_count)(at, size, CS8(value)),
                      DIRTY(3, memory_count)(P(at), size, D8(value)), where);
        if (DIRTY_HAS(memory_first_of_ascii_case))
                AGREE(memory_first_of_ascii_case,
                      DIRTY_ROUTINE(memory_first_of_ascii_case)(at, CS8(value), size),
                      DIRTY(3, memory_first_of_ascii_case)(P(at), D8(value), size),
                      where);

        //      memchr and memrchr take an int, whose bits eight to thirty one
        //      are the caller's to set on every machine.
        b32 wide = (b32)(turn & 2 ? value | 0x100 : value | 0xffffff00u);
        if (DIRTY_HAS(memchr) && DIRTY_HAS(memory_first_of))
                AGREE(memchr, DIRTY_ROUTINE(memory_first_of)(at, CS8(value), size),
                      DIRTY_ROUTINE(memchr)(at, CS32(wide), size), where);
        if (DIRTY_HAS(memrchr) && DIRTY_HAS(memory_last_of))
                AGREE(memrchr, DIRTY_ROUTINE(memory_last_of)(at, CS8(value), size),
                      DIRTY_ROUTINE(memrchr)(at, CS32(wide), size), where);

        //      A copy up to the value, into two destinations alike.
        p8 address_to one = dirty_one + 64 + residue;
        p8 address_to two = dirty_two + 64 + residue;
        p8 guard = (p8)(0x5a ^ turn);

        reference_fill(one - 64, guard, size + 128);
        reference_fill(two - 64, guard, size + 128);
        if (DIRTY_HAS(memory_copy_until))
        {
                positive clean = dirty_offset(
                    DIRTY_ROUTINE(memory_copy_until)(one, at, CS8(value), size), one);
                positive answer = dirty_offset(
                    (address_any)DIRTY(4, memory_copy_until)(P(two), P(at),
                                                             D8(value), size),
                    two);
                AGREE(memory_copy_until, clean, answer, where);
                AGREE(memory_copy_until, 0,
                      dirty_differ(one - 64, two - 64, size + 128), where);
        }
        if (DIRTY_HAS(memccpy) && DIRTY_HAS(memory_copy_until))
        {
                reference_fill(one - 64, guard, size + 128);
                reference_fill(two - 64, guard, size + 128);
                positive clean = dirty_offset(
                    DIRTY_ROUTINE(memory_copy_until)(one, at, CS8(value), size), one);
                positive answer = dirty_offset(
                    DIRTY_ROUTINE(memccpy)(two, at, CS32(wide), size), two);
                AGREE(memccpy, clean, answer, where);
                AGREE(memccpy, 0, dirty_differ(one - 64, two - 64, size + 128),
                      where);
        }

        //      And a run of the value from the start, for the span.
        for (positive i = 0; i < place; i++)
                at[i] = value;
        if (place < size)
                at[place] = dirty_other(value, false);
        if (DIRTY_HAS(memory_span_byte))
                AGREE(memory_span_byte,
                      DIRTY_ROUTINE(memory_span_byte)(at, C8(value), size),
                      DIRTY(3, memory_span_byte)(P(at), D8(value), size), where);

        //      And a run of the value to the end, for the suffix span.
        for (positive i = place; i < size; i++)
                at[i] = value;
        if (place && place <= size)
                at[place - 1] = dirty_other(value, false);
        if (DIRTY_HAS(memory_span_byte_reverse))
                AGREE(memory_span_byte_reverse,
                      DIRTY_ROUTINE(memory_span_byte_reverse)(at, C8(value), size),
                      DIRTY(3, memory_span_byte_reverse)(P(at), D8(value), size),
                      where);
}

static fn dirty_hunts(void) { dirty_plane(dirty_hunts_one, 4000); }

//      The fills, a byte, an int's byte and a word at a time.
static fn dirty_fill_one(positive size, positive residue, positive turn)
{
        p8 value = dirty_values[turn % array_count(dirty_values)];
        p8 guard = (p8)(0xa5 ^ turn);
        p8 address_to one = dirty_one + 64 + residue;
        p8 address_to two = dirty_two + 64 + residue;
        positive where = size << 16 | residue << 8 | value;

        reference_fill(one - 64, guard, size + 128);
        reference_fill(two - 64, guard, size + 128);
        if (DIRTY_HAS(memory_fill))
        {
                positive clean = dirty_offset(
                    DIRTY_ROUTINE(memory_fill)(one, CS8(value), size), one);
                positive answer = dirty_offset(
                    (address_any)DIRTY(3, memory_fill)(P(two), D8(value), size),
                    two);

                AGREE(memory_fill, clean, answer, where);
                AGREE(memory_fill, 0,
                      dirty_differ(one - 64, two - 64, size + 128), where);
                if (DIRTY_HAS(memset))
                {
                        b32 wide = (b32)(turn & 1 ? value | 0x100
                                                  : value | 0xffffff00u);

                        reference_fill(two - 64, guard, size + 128);
                        AGREE(memset, clean,
                              dirty_offset(DIRTY_ROUTINE(memset)(two, CS32(wide),
                                                                 size),
                                           two),
                              where);
                        AGREE(memset, 0,
                              dirty_differ(one - 64, two - 64, size + 128), where);
                }
        }

        //      Four bytes at a time, at the four residues of four.
        p32 word = (p32)dirty_random();
        positive count = size / 4;
        one = dirty_one + 64 + (residue & ~(positive)3);
        two = dirty_two + 64 + (residue & ~(positive)3);
        where = size << 16 | residue << 8 | 0x32;
        if (DIRTY_HAS(memory_fill_32))
        {
                reference_fill(one - 64, guard, count * 4 + 128);
                reference_fill(two - 64, guard, count * 4 + 128);
                positive clean = dirty_offset(
                    DIRTY_ROUTINE(memory_fill_32)(one, C32(word), count), one);
                positive answer = dirty_offset(
                    (address_any)DIRTY(3, memory_fill_32)(P(two),
                                                          D32(word), count),
                    two);
                AGREE(memory_fill_32, clean, answer, where);
                AGREE(memory_fill_32, 0,
                      dirty_differ(one - 64, two - 64, count * 4 + 128), where);
        }
        if (DIRTY_HAS(memory_fill_u32))
        {
                reference_fill(one - 64, guard, count * 4 + 128);
                reference_fill(two - 64, guard, count * 4 + 128);
                DIRTY_ROUTINE(memory_fill_u32)(one, count, C32(word));
                DIRTY(3, memory_fill_u32)(P(two), count, D32(word));
                AGREE(memory_fill_u32, 0,
                      dirty_differ(one - 64, two - 64, count * 4 + 128), where);
        }
}

static fn dirty_fill(void) { dirty_plane(dirty_fill_one, 4000); }

//      Offsets of either byte, and of the bytes inside or outside a range.
static fn dirty_offsets_one(positive size, positive residue, positive turn)
{
        static const p8 ranges[][2] = {{0x20, 0x7e}, {8, 10}, {0, 0},
                                       {0xff, 0xff}, {0, 0xff}, {'a', 'z'},
                                       {0x80, 0xbf}};
        static const positive limits[] = {1, 2, 7, 64, 5000};
        p8 address_to at = dirty_in + 64 + residue;
        p8 first = dirty_values[turn % array_count(dirty_values)];
        p8 second = dirty_values[(turn / 3 + 5) % array_count(dirty_values)];
        p8 low = ranges[turn % array_count(ranges)][0];
        p8 high = ranges[turn % array_count(ranges)][1];
        positive limit = limits[(turn / 7) % array_count(limits)];
        positive where = size << 16 | residue << 8 | limit;

        for (positive i = 0; i < size + 64; i++)
        {
                positive pick = dirty_random();

                at[i] = (pick & 7) == 0 ? first : (pick & 7) == 1 ? second
                                                                   : (p8)(pick >> 8);
        }
        if (DIRTY_HAS(memory_offsets_of_either))
        {
                positive clean = DIRTY_ROUTINE(memory_offsets_of_either)(
                    dirty_at_one, at, size, C8(first), C8B(second), limit);
                positive answer = DIRTY(6, memory_offsets_of_either)(
                    P(dirty_at_two), P(at), size, D8(first), D8(second), limit);
                AGREE(memory_offsets_of_either, clean, answer, where);
                AGREE(memory_offsets_of_either, 0,
                      dirty_differ((p8 address_to)dirty_at_one,
                                   (p8 address_to)dirty_at_two,
                                   (clean < answer ? clean : answer) * 4),
                      where);
        }
        if (DIRTY_HAS(memory_offsets_between))
        {
                positive clean = DIRTY_ROUTINE(memory_offsets_between)(
                    dirty_at_one, at, size, C8(low), C8B(high), limit);
                positive answer = DIRTY(6, memory_offsets_between)(
                    P(dirty_at_two), P(at), size, D8(low), D8(high), limit);
                AGREE(memory_offsets_between, clean, answer, where);
                AGREE(memory_offsets_between, 0,
                      dirty_differ((p8 address_to)dirty_at_one,
                                   (p8 address_to)dirty_at_two,
                                   (clean < answer ? clean : answer) * 4),
                      where);
        }
        if (DIRTY_HAS(memory_offsets_outside))
        {
                positive clean = DIRTY_ROUTINE(memory_offsets_outside)(
                    dirty_at_one, at, size, C8(low), C8B(high), limit);
                positive answer = DIRTY(6, memory_offsets_outside)(
                    P(dirty_at_two), P(at), size, D8(low), D8(high), limit);
                AGREE(memory_offsets_outside, clean, answer, where);
                AGREE(memory_offsets_outside, 0,
                      dirty_differ((p8 address_to)dirty_at_one,
                                   (p8 address_to)dirty_at_two,
                                   (clean < answer ? clean : answer) * 4),
                      where);
        }
}

static fn dirty_offsets(void) { dirty_plane(dirty_offsets_one, 4000); }

//      Field edges, the n-th of a byte from either end, and the last of two.
static fn dirty_record_scans_one(positive size, positive residue, positive turn)
{
        static const positive limits[] = {1, 2, 7, 64, 2000};
        p8 address_to at = dirty_in + 64 + residue;
        p8 value = dirty_values[turn % array_count(dirty_values)];
        p8 second = dirty_values[(turn / 3 + 5) % array_count(dirty_values)];
        positive limit = limits[(turn / 7) % array_count(limits)];
        positive n = 1 + turn % 5;
        positive where = size << 16 | residue << 8 | limit;

        for (positive i = 0; i < size + 64; i++)
        {
                positive pick = dirty_random();

                at[i] = (pick & 7) == 0   ? value
                        : (pick & 7) == 1 ? ' '
                        : (pick & 7) == 2 ? '\t'
                        : (pick & 7) == 3 ? second
                                          : (p8)(pick >> 8);
        }
        if (DIRTY_HAS(memory_offsets_fields_blank))
        {
                positive clean = DIRTY_ROUTINE(memory_offsets_fields_blank)(
                    dirty_at_one, at, size, C8(value), limit);
                positive answer = DIRTY(5, memory_offsets_fields_blank)(
                    P(dirty_at_two), P(at), size, D8(value), limit);
                AGREE(memory_offsets_fields_blank, clean, answer, where);
                AGREE(memory_offsets_fields_blank, 0,
                      dirty_differ((p8 address_to)dirty_at_one,
                                   (p8 address_to)dirty_at_two,
                                   (clean < answer ? clean : answer) * 8),
                      where);
        }
        if (DIRTY_HAS(memory_nth_of))
        {
                positive2 clean = DIRTY_ROUTINE(memory_nth_of)(at, CS8(value), size, n);
                positive2 answer = DIRTY(pair_4, memory_nth_of)(P(at), D8(value), size, n);
                AGREE(memory_nth_of, clean.x, answer.x, where);
                AGREE(memory_nth_of, clean.y, answer.y, where);
        }
        if (DIRTY_HAS(memory_nth_last_of))
        {
                positive2 clean = DIRTY_ROUTINE(memory_nth_last_of)(at, CS8(value), size, n);
                positive2 answer = DIRTY(pair_4, memory_nth_last_of)(P(at), D8(value), size, n);
                AGREE(memory_nth_last_of, clean.x, answer.x, where);
                AGREE(memory_nth_last_of, clean.y, answer.y, where);
        }
        if (DIRTY_HAS(memory_last_of_either))
        {
                address_any clean = DIRTY_ROUTINE(memory_last_of_either)(
                    at, CS8(value), CS8B(second), size);
                positive answer = DIRTY(4, memory_last_of_either)(
                    P(at), D8(value), D8(second), size);
                AGREE(memory_last_of_either, dirty_offset(clean, at),
                      dirty_offset((address_any)answer, at), where);
        }
}

static fn dirty_record_scans(void) { dirty_plane(dirty_record_scans_one, 4000); }

/*
        The escapes: every policy, the hex categories 0..63 and JSON's 64,
        and for the encoder each with bit 128 too, over bytes from every
        category, into room for all of it on even turns and for about the
        input on odd ones.
*/
static fn dirty_escape_one(positive size, positive residue, positive turn)
{
        static const p8 alphabet[] = {0, 1, 9, 10, 13, 31, ' ', '"', '\\', '/',
                                      'a', 'Z', '~', 0x7f, 0x80, 0xc3, 0xff};
        p8 address_to at = dirty_in + 64 + residue;
        positive pick = turn % 130;
        p8 policy = (p8)(pick < 65 ? pick : 128 | (pick - 65));
        positive capacity = turn & 1 ? size + turn % 7 : size * 6 + 16;
        p8 address_to one = dirty_one + 64 + (turn & 15);
        p8 address_to two = dirty_two + 64 + (turn & 15);
        p8 guard = (p8)(0x3c ^ turn);
        positive where = size << 16 | residue << 8 | policy;

        for (positive i = 0; i < size; i++)
        {
                positive r = dirty_random();

                at[i] = r % 3 ? (p8)('a' + (r >> 8) % 26)
                              : alphabet[(r >> 8) % array_count(alphabet)];
        }
        if (DIRTY_HAS(memory_escape_index))
                AGREE(memory_escape_index,
                      DIRTY_ROUTINE(memory_escape_index)(at, size, C8(policy & 127)),
                      DIRTY(3, memory_escape_index)(P(at), size, D8(policy & 127)),
                      where);
        if (DIRTY_HAS(memory_into_escaped))
        {
                reference_fill(one - 64, guard, capacity + 128);
                reference_fill(two - 64, guard, capacity + 128);
                positive2 clean = DIRTY_ROUTINE(memory_into_escaped)(
                    one, at, size, capacity, C8(policy));
                positive2 answer = DIRTY(pair_5, memory_into_escaped)(
                    P(two), P(at), size, capacity, D8(policy));
                AGREE(memory_into_escaped, clean.x, answer.x, where);
                AGREE(memory_into_escaped, clean.y, answer.y, where);
                AGREE(memory_into_escaped, 0,
                      dirty_differ(one - 64, two - 64, capacity + 128), where);
        }
}

static fn dirty_escape(void) { dirty_plane(dirty_escape_one, 513); }

//      Spellings through cat's caret table, on odd turns a short capacity:
//      the output and the bytes after it compared whole.
static fn dirty_spelled_one(positive size, positive residue, positive turn)
{
        p64 address_to table = spelling_caret(turn & 7);
        p8 address_to at = dirty_in + 64 + residue;
        positive capacity = turn & 1 ? size * 2 + turn % 7 : size * 4 + 8;
        p8 address_to one = dirty_one + 64 + (turn & 15);
        p8 address_to two = dirty_two + 64 + (turn & 15);
        p8 guard = (p8)(0x5a ^ turn);
        positive where = size << 16 | residue << 8 | (turn & 255);

        if (!DIRTY_HAS(memory_into_spelled))
                return;
        for (positive i = 0; i < size; i++)
        {
                positive r = dirty_random();

                at[i] = r % 3 ? (p8)(' ' + (r >> 8) % 95) : (p8)(r >> 16);
        }
        reference_fill(one - 64, guard, capacity + 128);
        reference_fill(two - 64, guard, capacity + 128);
        positive2 clean = DIRTY_ROUTINE(memory_into_spelled)(one, at, size,
                                                             capacity, table);
        positive2 answer = DIRTY(pair_5, memory_into_spelled)(
            P(two), P(at), size, capacity, P(table));
        AGREE(memory_into_spelled, clean.x, answer.x, where);
        AGREE(memory_into_spelled, clean.y, answer.y, where);
        AGREE(memory_into_spelled, 0,
              dirty_differ(one - 64, two - 64, capacity + 128), where);
}

static fn dirty_spelled(void) { dirty_plane(dirty_spelled_one, 1000); }

//      Word counts, from inside a word and out of one.
static fn dirty_words_one(positive size, positive residue, positive turn)
{
        static const p8 alphabet[] = {' ', '\t', '\n', '\v', '\f', '\r', 'a',
                                      'b', 0x80, 0, '.', 0xa0};
        static const p8 insides[] = {0, 1, 2, 0x80};
        p8 address_to at = dirty_in + 64 + residue;
        p8 inside = insides[turn % 4];

        for (positive i = 0; i < size; i++)
                at[i] = alphabet[dirty_random() % array_count(alphabet)];
        if (!DIRTY_HAS(memory_count_words))
                return;
        positive2 clean = DIRTY_ROUTINE(memory_count_words)(at, size, C8(inside));
        positive2 answer = DIRTY(pair_3, memory_count_words)(P(at), size, D8(inside));
        AGREE(memory_count_words, clean.x, answer.x, size << 16 | residue << 8 | inside);
        AGREE(memory_count_words, clean.y, answer.y, size << 16 | residue << 8 | inside);
}

static fn dirty_words(void) { dirty_plane(dirty_words_one, 1000); }

/*
        The well-formed UTF-8 prefix, against a walk written here: pieces of
        one to four bytes, well formed and not, so the stop falls at every
        place and a sequence is cut by size at every length. Its arguments are
        a pointer and a size, nothing narrower; the entry is here for the
        kernel object's body, which only this lane calls.
*/
static positive2 dirty_utf8_reference(const p8 address_to at, positive size)
{
        positive2 answer = {0};

        while (answer.x < size)
        {
                p8 lead = at[answer.x];
                positive need = lead < 0x80 ? 1 : lead < 0xc2 ? 0 : lead < 0xe0 ? 2
                                : lead < 0xf0 ? 3 : lead < 0xf5 ? 4 : 0;
                if (!need || need > size - answer.x)
                        break;
                p8 low = lead == 0xe0 ? 0xa0 : lead == 0xf0 ? 0x90 : 0x80;
                p8 high = lead == 0xed ? 0x9f : lead == 0xf4 ? 0x8f : 0xbf;
                p8 good = true;
                for (positive i = 1; i < need; i++)
                {
                        p8 byte = at[answer.x + i];
                        if (byte < (i == 1 ? low : 0x80) || byte > (i == 1 ? high : 0xbf))
                                good = false;
                }
                if (!good)
                        break;
                answer.x += need;
                answer.y++;
        }
        return answer;
}

static fn dirty_utf8_valid_one(positive size, positive residue, positive turn)
{
        static const p8 pieces[][5] = {
                {1, 'a'}, {2, 0xc3, 0xa9}, {3, 0xe6, 0xbc, 0xa2}, {4, 0xf0, 0x9f, 0x99, 0x82},
                {3, 0xed, 0x9f, 0xbf}, {4, 0xf4, 0x8f, 0xbf, 0xbf}, {3, 0xe0, 0xa0, 0x80},
                {1, 0x80}, {2, 0xc0, 0xaf}, {3, 0xed, 0xa0, 0x80}, {1, 0xf5},
                {4, 0xf4, 0x90, 0x80, 0x80}, {3, 0xe0, 0x9f, 0xbf}, {1, 0xe6},
        };
        p8 address_to at = dirty_in + 64 + residue;
        //      Mostly well formed, so the stop lands late as often as early.
        positive bad = 7 + turn % 7, odds = 2 + turn % 60;

        for (positive i = 0; i < size + 8;)
        {
                positive pick = dirty_random() % odds ? dirty_random() % 7 : bad;
                for (positive k = 0; k < pieces[pick][0] && i < size + 8; k++)
                        at[i++] = pieces[pick][1 + k];
        }
        if (!DIRTY_HAS(memory_utf8_valid_span))
                return;
        positive2 clean = dirty_utf8_reference(at, size);
        positive2 answer = DIRTY(pair_2, memory_utf8_valid_span)(P(at), size);
        AGREE(memory_utf8_valid_span, clean.x, answer.x, size << 16 | residue << 8);
        AGREE(memory_utf8_valid_span, clean.y, answer.y, size << 16 | residue << 8);
}

static fn dirty_utf8_valid(void) { dirty_plane(dirty_utf8_valid_one, 4000); }

//      The ASCII prefix: a pointer and a size, with a byte past ASCII at a
//      random place on odd turns.
static fn dirty_ascii_one(positive size, positive residue, positive turn)
{
        p8 address_to at = dirty_in + 64 + residue;

        for (positive i = 0; i < size + 8; i++)
                at[i] = (p8)dirty_random() & 0x7f;
        if ((turn & 1) && size)
                at[dirty_random() % size] = (p8)dirty_random() | 0x80;
        if (!DIRTY_HAS(memory_ascii_span))
                return;
        positive clean = DIRTY_ROUTINE(memory_ascii_span)(at, size);
        positive answer = DIRTY(2, memory_ascii_span)(P(at), size);
        AGREE(memory_ascii_span, clean, answer, size << 16 | residue << 8);
}

static fn dirty_ascii(void) { dirty_plane(dirty_ascii_one, 4000); }

/*
        Records holding a needle, split at a delimiter that is a newline,
        nought, a comma or a high byte, with the needle's anchors prepared by
        the clean call.
*/
static fn dirty_records_one(positive size, positive residue, positive turn)
{
        static const p8 delimiters[] = {'\n', 0, ',', 0x80, 0xff};
        static const p8 needle[] = "needle0123456789ABCDEFGH";
        static const positive lengths[] = {0, 1, 2, 6, 24};
        p8 address_to at = dirty_in + 64 + residue;
        p8 delimiter = delimiters[turn % array_count(delimiters)];
        positive length = lengths[(turn / 5) % array_count(lengths)];
        positive step = 5 + turn % 29;

        if (!DIRTY_HAS(memory_count_records_with_prepared) ||
            !DIRTY_HAS(memory_search_prepare))
                return;
        for (positive i = 0; i < size; i++)
                at[i] = (i + 1) % step ? 'x' : delimiter;
        for (positive i = turn % 13; length && i + length <= size; i += 3 * step)
                for (positive k = 0; k < length; k++)
                        at[i + k] = needle[k];
        positive2 anchors = DIRTY_ROUTINE(memory_search_prepare)(
            (address_any)needle, length, C8(0));
        AGREE(memory_count_records_with_prepared,
              DIRTY_ROUTINE(memory_count_records_with_prepared)(
                  at, size, (address_any)needle, length, anchors.x, anchors.y,
                  CS8(delimiter)),
              DIRTY(7, memory_count_records_with_prepared)(
                  P(at), size, P(needle), length, anchors.x, anchors.y,
                  D8(delimiter)),
              size << 16 | residue << 8 | delimiter);
}

static fn dirty_records(void) { dirty_plane(dirty_records_one, 1000); }

//      Needle anchors, folded and not, over needles of every shape.
static fn dirty_prepare(void)
{
        static const positive sizes[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16,
                                         17, 31, 32, 33, 40, 64, 100, 300};
        static const p8 truths[] = {0, 1, 2, 0x80};
        p8 address_to needle = dirty_in + 64;

        if (!DIRTY_HAS(memory_search_prepare))
                return;
        for (positive s = 0; s < array_count(sizes); s++)
                for (positive shape = 0; shape < 8; shape++)
                        for (positive t = 0; t < array_count(truths); t++)
                        {
                                positive size = sizes[s];

                                for (positive i = 0; i < size; i++)
                                {
                                        positive r = dirty_random();

                                        needle[i] = shape == 0   ? 'a'
                                                    : shape == 1 ? (p8)('A' + r % 26 + (r & 32))
                                                    : shape == 2 ? (p8)r
                                                    : shape == 3 ? (p8)("0123456789.,;:!?"[r % 16])
                                                    : shape == 4 ? (i == size / 2 ? 'Q' : 'e')
                                                    : shape == 5 ? (p8)(0x80 + r % 128)
                                                    : shape == 6 ? (p8)(r & 1 ? 'x' : 'X')
                                                                 : (p8)(' ' + r % 95);
                                }
                                positive2 clean = DIRTY_ROUTINE(memory_search_prepare)(
                                    needle, size, C8(truths[t]));
                                positive2 answer = DIRTY(pair_3, memory_search_prepare)(
                                    P(needle), size, D8(truths[t]));
                                positive where = size << 16 | shape << 8 | truths[t];

                                AGREE(memory_search_prepare, clean.x, answer.x, where);
                                AGREE(memory_search_prepare, clean.y, answer.y, where);
                        }
}

/*
        The string hunts and the two that write, over a terminated string
        of bytes that are neither the value nor nought, the value at a
        random place and at a stride after it, and bytes past the terminator
        that may hold it. The value is nought on one turn in fifteen, which
        is the terminator's own question.
*/
static fn dirty_strings_one(positive size, positive residue, positive turn)
{
        p8 address_to at = dirty_in + 64 + residue;
        p8 value = dirty_values[turn % array_count(dirty_values)];
        p8 replace = dirty_values[(turn / 3 + 1) % array_count(dirty_values)];
        positive place = dirty_random() % (size + 1);
        positive bounds[] = {0, place, size, size + 9};
        positive bound = bounds[(turn / 14) % 4];
        positive where = size << 16 | residue << 8 | value;
        b32 wide = (b32)(turn & 1 ? value | 0x100 : value | 0xffffff00u);

        for (positive i = 0; i < size; i++)
                at[i] = dirty_other(value, true);
        for (positive i = place; value && i < size; i += 7 + turn % 11)
                at[i] = value;
        at[size] = 0;
        for (positive i = 1; i <= 32; i++)
                at[size + i] = i & 3 ? value : (p8)dirty_random();

        if (DIRTY_HAS(string_first_of))
                AGREE(string_first_of,
                      DIRTY_ROUTINE(string_first_of)(at, C8(value)),
                      DIRTY(2, string_first_of)(P(at), D8(value)), where);
        if (DIRTY_HAS(string_first_of_or_end))
                AGREE(string_first_of_or_end,
                      DIRTY_ROUTINE(string_first_of_or_end)(at, C8(value)),
                      DIRTY(2, string_first_of_or_end)(P(at), D8(value)),
                      where);
        //      Two bytes, each with its own junk: the lexer's quote and backslash, and every pair the values make. The
        //      clean answer is the lexer's loop's as well: under VERIFY_NARROW_KERNEL that is the one row that holds the
        //      kernel's bodies (arm64's is a word-at-a-time body of its own) to it.
        if (DIRTY_HAS(string_first_of_either_or_end))
        {
                string_address clean = DIRTY_ROUTINE(string_first_of_either_or_end)(at, C8(value), C8B(replace));

                AGREE(string_first_of_either_or_end, clean,
                      DIRTY(3, string_first_of_either_or_end)(P(at), D8(value), D8(replace)), where);
                dirty_either_compared++;
                dirty_either_wrong += clean != reference_string_first_of_either_or_end(at, value, replace);
        }
        if (DIRTY_HAS(string_first_of_max))
                AGREE(string_first_of_max,
                      DIRTY_ROUTINE(string_first_of_max)(at, bound, C8(value)),
                      DIRTY(3, string_first_of_max)(P(at), bound, D8(value)),
                      where);
        if (DIRTY_HAS(string_last_of))
                AGREE(string_last_of,
                      DIRTY_ROUTINE(string_last_of)(at, C8(value)),
                      DIRTY(2, string_last_of)(P(at), D8(value)), where);
        if (DIRTY_HAS(string_last_of_or_end))
                AGREE(string_last_of_or_end,
                      DIRTY_ROUTINE(string_last_of_or_end)(at, C8(value)),
                      DIRTY(2, string_last_of_or_end)(P(at), D8(value)),
                      where);
        if (DIRTY_HAS(strchr) && DIRTY_HAS(string_first_of))
                AGREE(strchr, DIRTY_ROUTINE(string_first_of)(at, C8(value)),
                      DIRTY_ROUTINE(strchr)((const char address_to)at, CS32(wide)),
                      where);
        if (DIRTY_HAS(strchrnul) && DIRTY_HAS(string_first_of_or_end))
                AGREE(strchrnul, DIRTY_ROUTINE(string_first_of_or_end)(at, C8(value)),
                      DIRTY_ROUTINE(strchrnul)((const char address_to)at, CS32(wide)),
                      where);
        if (DIRTY_HAS(strnchr) && DIRTY_HAS(string_first_of_max))
                AGREE(strnchr, DIRTY_ROUTINE(string_first_of_max)(at, bound, C8(value)),
                      DIRTY_ROUTINE(strnchr)((const char address_to)at, bound,
                                             CS32(wide)),
                      where);
        if (DIRTY_HAS(strrchr) && DIRTY_HAS(string_last_of_or_end))
                AGREE(strrchr, DIRTY_ROUTINE(string_last_of_or_end)(at, C8(value)),
                      DIRTY_ROUTINE(strrchr)((const char address_to)at, CS32(wide)),
                      where);

        //      Cut and replace in place, each on its own copy.
        p8 address_to one = dirty_one + 64 + residue;
        p8 address_to two = dirty_two + 64 + residue;
        if (DIRTY_HAS(string_cut))
        {
                reference_copy(one - 64, at - 64, size + 128);
                reference_copy(two - 64, at - 64, size + 128);
                positive clean = dirty_offset(
                    DIRTY_ROUTINE(string_cut)(one, CS8(value)), one);
                positive answer = dirty_offset(
                    (address_any)DIRTY(2, string_cut)(P(two), D8(value)), two);
                AGREE(string_cut, clean, answer, where);
                AGREE(string_cut, 0, dirty_differ(one - 64, two - 64, size + 128),
                      where);
        }
        if (DIRTY_HAS(string_replace_all))
        {
                reference_copy(one - 64, at - 64, size + 128);
                reference_copy(two - 64, at - 64, size + 128);
                DIRTY_ROUTINE(string_replace_all)(one, CS8(value), CS8B(replace));
                if (turn % 3 == 0)
                        DIRTY(3, string_replace_all)(P(two), D8(value), P(CS8B(replace)));
                else if (turn % 3 == 1)
                        DIRTY(3, string_replace_all)(P(two), P(CS8(value)), D8(replace));
                else
                        DIRTY(3, string_replace_all)(P(two), D8(value), D8(replace));
                AGREE(string_replace_all, 0,
                      dirty_differ(one - 64, two - 64, size + 128), where);
        }
}

static fn dirty_strings(void) { dirty_plane(dirty_strings_one, 4000); }

/*
        What a writer was handed, call by call: each call's length in two
        bytes and then its bytes, side one for the clean call and side two
        for the other, compared whole.
*/
static p8 dirty_log_room[2][16384];
static positive dirty_logged[2], dirty_side;

static fn dirty_writer(address_any data, positive length)
{
        p8 address_to into = dirty_log_room[dirty_side];
        positive used = dirty_logged[dirty_side];

        if (used + 2 + length > sizeof(dirty_log_room[0]))
        {
                dirty_logged[dirty_side] = sizeof(dirty_log_room[0]) + 1;
                return;
        }
        into[used] = (p8)length;
        into[used + 1] = (p8)(length >> 8);
        for (positive i = 0; i < length; i++)
                into[used + 2 + i] = ((p8 address_to)data)[i];
        dirty_logged[dirty_side] = used + 2 + length;
}

static fn dirty_log(positive side)
{
        dirty_side = side;
        dirty_logged[side] = 0;
}

static positive dirty_logs_differ(void)
{
        if (dirty_logged[0] != dirty_logged[1])
                return (positive)-1;
        return dirty_differ(dirty_log_room[0], dirty_log_room[1], dirty_logged[0]);
}

static fn dirty_writers(void)
{
        static const p8 pads[] = {0, ' ', '0', '#', 0x80, 0xff};
        static const p8 prefixes[] = {0, '-', '+', 0x80};
        static const p8 truths[] = {0, 1, 2, 0x80};
        static const positive values[] = {0, 7, 42, 99999, 4294967296ull,
                                          (positive)-1};
        writer write = dirty_writer;
        if (DIRTY_HAS(writer_fill))
                for (positive count = 0; count <= 40; count++)
                        for (positive v = 0; v < array_count(dirty_values); v++)
                        {
                                p8 byte = dirty_values[v];

                                dirty_log(0);
                                DIRTY_ROUTINE(writer_fill)(write, count, C8(byte));
                                dirty_log(1);
                                DIRTY(3, writer_fill)(P(write), count, D8(byte));
                                AGREE(writer_fill, 0, dirty_logs_differ(),
                                      count << 8 | byte);
                        }

        p8 address_to data = dirty_in + 64;
        for (positive i = 0; i < 64; i++)
                data[i] = (p8)(i * 43 + 1);
        data[1] = 0;
        for (positive length = 0; length <= 24; length++)
                for (positive width = 0; width <= 40; width++)
                        for (positive p = 0; p < array_count(pads); p++)
                                for (positive t = 0; t < array_count(truths); t++)
                                {
                                        p8 pad = pads[p], left = truths[t];
                                        positive where = length << 24 | width << 16 |
                                                         pad << 8 | left;
                                        positive which = (length + width + p + t) % 3;

                                        if (DIRTY_HAS(writer_field))
                                        {
                                                dirty_log(0);
                                                DIRTY_ROUTINE(writer_field)(
                                                    write, data, length, width,
                                                    CS8(pad), C8(left));
                                                dirty_log(1);
                                                DIRTY(6, writer_field)(
                                                    P(write), P(data), length, width,
                                                    which == 1 ? P(CS8(pad)) : D8(pad),
                                                    which == 0 ? P(C8(left)) : D8(left));
                                                AGREE(writer_field, 0,
                                                      dirty_logs_differ(), where);
                                        }
                                        if (DIRTY_HAS(string_to_field))
                                        {
                                                p8 text[32];

                                                for (positive i = 0; i < length; i++)
                                                        text[i] = (p8)(1 + (i * 47 + length) % 255);
                                                text[length] = 0;
                                                dirty_log(0);
                                                DIRTY_ROUTINE(string_to_field)(
                                                    write, text, width, CS8(pad),
                                                    C8(left));
                                                dirty_log(1);
                                                DIRTY(5, string_to_field)(
                                                    P(write), P(text), width,
                                                    which == 1 ? P(CS8(pad)) : D8(pad),
                                                    which == 0 ? P(C8(left)) : D8(left));
                                                AGREE(string_to_field, 0,
                                                      dirty_logs_differ(), where);
                                        }
                                }

        if (DIRTY_HAS(positive_to_padded))
                for (positive v = 0; v < array_count(values); v++)
                        for (positive width = 0; width <= 30; width++)
                                for (positive p = 0; p < array_count(pads); p++)
                                        for (positive x = 0; x < array_count(prefixes); x++)
                                        {
                                                p8 pad = pads[p], prefix = prefixes[x];
                                                positive which = (v + width + p + x) % 3;

                                                dirty_log(0);
                                                DIRTY_ROUTINE(positive_to_padded)(
                                                    write, values[v], width, C8(pad),
                                                    C8B(prefix));
                                                dirty_log(1);
                                                DIRTY(5, positive_to_padded)(
                                                    P(write), values[v], width,
                                                    which == 1 ? P(C8(pad)) : D8(pad),
                                                    which == 0 ? P(C8B(prefix)) : D8(prefix));
                                                AGREE(positive_to_padded, 0,
                                                      dirty_logs_differ(),
                                                      v << 24 | width << 16 | pad << 8 | prefix);
                                        }
}

//      The three that write digits into a buffer.
static fn dirty_into(void)
{
        static const p8 pads[] = {0, ' ', '0', '#', 0x80, 0xff};
        static const p8 truths[] = {0, 1, 2, 0x80};
        static positive values[64];
        p8 address_to one = dirty_one + 64;
        p8 address_to two = dirty_two + 64;
        values[0] = 0;
        values[1] = (positive)-1;
        for (positive i = 2; i < 34; i++)
                values[i] = ((positive)1 << (i + 28)) - (i & 1);
        for (positive i = 34; i < 64; i++)
                values[i] = dirty_random() >> (i % 50);

        for (positive v = 0; v < array_count(values); v++)
        {
                positive value = values[v];

                if (DIRTY_HAS(positive_into_padded))
                        for (positive width = 0; width <= 40; width++)
                                for (positive p = 0; p < array_count(pads); p++)
                                {
                                        reference_fill(one - 16, 0x77, 96);
                                        reference_fill(two - 16, 0x77, 96);
                                        positive clean = DIRTY_ROUTINE(positive_into_padded)(
                                            one, value, width, C8(pads[p]));
                                        positive answer = DIRTY(4, positive_into_padded)(
                                            P(two), value, width, D8(pads[p]));
                                        positive where = v << 16 | width << 8 | pads[p];

                                        AGREE(positive_into_padded, clean, answer, where);
                                        AGREE(positive_into_padded, 0,
                                              dirty_differ(one - 16, two - 16, 96), where);
                                }
                if (DIRTY_HAS(positive_into_base))
                        for (positive base = 2; base <= 36; base++)
                                for (positive t = 0; t < array_count(truths); t++)
                                {
                                        reference_fill(one - 16, 0x77, 96);
                                        reference_fill(two - 16, 0x77, 96);
                                        positive clean = DIRTY_ROUTINE(positive_into_base)(
                                            one, value, base, C8(truths[t]));
                                        positive answer = DIRTY(4, positive_into_base)(
                                            P(two), value, base, D8(truths[t]));
                                        positive where = v << 16 | base << 8 | truths[t];

                                        AGREE(positive_into_base, clean, answer, where);
                                        AGREE(positive_into_base, 0,
                                              dirty_differ(one - 16, two - 16, 96), where);
                                }
                if (DIRTY_HAS(positive_into_human_nearest_string))
                        for (positive shift = 0; shift < 64; shift += 3)
                                for (positive t = 0; t < array_count(truths); t++)
                                {
                                        positive scaled = value >> shift;

                                        reference_fill(one - 16, 0x77, 48);
                                        reference_fill(two - 16, 0x77, 48);
                                        positive clean = DIRTY_ROUTINE(
                                            positive_into_human_nearest_string)(
                                            one, scaled, C8(truths[t]));
                                        positive answer = DIRTY(
                                            3, positive_into_human_nearest_string)(
                                            P(two), scaled, D8(truths[t]));
                                        positive where = v << 16 | shift << 8 | truths[t];

                                        AGREE(positive_into_human_nearest_string, clean,
                                              answer, where);
                                        AGREE(positive_into_human_nearest_string, 0,
                                              dirty_differ(one - 16, two - 16, 48), where);
                                }
        }
}

//      A byte into a buffer, with room, at the edge that flushes to
//      /dev/null, and with none at all.
static fn dirty_buffered(void)
{
        static const positive capacities[] = {0, 1, 8, 32};
        bipolar sink;

        if (!DIRTY_HAS(buffered_write_byte))
                return;
        sink = system_call_4(syscall(openat), AT_FDCWD, (positive)"/dev/null",
                             FILE_READ_WRITE, 0);
        if (sink < 0)
        {
                string_format(log, "  NOTE buffered_write_byte: no /dev/null to flush into\n");
                return;
        }
        for (positive c = 0; c < array_count(capacities); c++)
                for (positive used = 0; used <= capacities[c]; used++)
                        for (positive v = 0; v < array_count(dirty_values); v++)
                        {
                                positive capacity = capacities[c];
                                positive used_one = used, used_two = used;
                                p8 byte = dirty_values[v];
                                positive where = capacity << 16 | used << 8 | byte;

                                reference_fill(dirty_one, 0x66, 64);
                                reference_fill(dirty_two, 0x66, 64);
                                b32 clean = DIRTY_ROUTINE(buffered_write_byte)(
                                    (positive)sink, dirty_one, capacity, &used_one,
                                    C8(byte));
                                b32 answer = (b32)DIRTY(5, buffered_write_byte)(
                                    (positive)sink, P(dirty_two), capacity,
                                    P(&used_two), D8(byte));
                                AGREE(buffered_write_byte, (p32)clean, (p32)answer, where);
                                AGREE(buffered_write_byte, used_one, used_two, where);
                                AGREE(buffered_write_byte, 0,
                                      dirty_differ(dirty_one, dirty_two, 64), where);
                        }
        system_call_1(syscall(close), (positive)sink);
}

//      The byte orders and the address spelling.
static fn dirty_orders(void)
{
        for (positive turn = 0; turn < 4096; turn++)
        {
                p32 word = turn < 64 ? (p32)(((positive)1 << (turn & 31)) - (turn >> 5))
                                     : (p32)dirty_random();
                p16 half = (p16)(turn < 256 ? turn * 257 : word);

                if (DIRTY_HAS(bytes_reverse_16))
                        AGREE(bytes_reverse_16,
                              DIRTY_ROUTINE(bytes_reverse_16)(C16(half)),
                              (p16)DIRTY(1, bytes_reverse_16)(D16(half)), half);
                if (DIRTY_HAS(bytes_reverse_32))
                        AGREE(bytes_reverse_32,
                              DIRTY_ROUTINE(bytes_reverse_32)(C32(word)),
                              (p32)DIRTY(1, bytes_reverse_32)(D32(word)), word);
                if (DIRTY_HAS(network_store_16))
                {
                        reference_fill(dirty_one, 0x55, 16);
                        reference_fill(dirty_two, 0x55, 16);
                        DIRTY_ROUTINE(network_store_16)(dirty_one + 4 + (turn & 3), C16(half));
                        DIRTY(2, network_store_16)(P(dirty_two + 4 + (turn & 3)), D16(half));
                        AGREE(network_store_16, 0,
                              dirty_differ(dirty_one, dirty_two, 16), half);
                }
                if (DIRTY_HAS(network_store_32))
                {
                        reference_fill(dirty_one, 0x55, 16);
                        reference_fill(dirty_two, 0x55, 16);
                        DIRTY_ROUTINE(network_store_32)(dirty_one + 4 + (turn & 3), C32(word));
                        DIRTY(2, network_store_32)(P(dirty_two + 4 + (turn & 3)), D32(word));
                        AGREE(network_store_32, 0,
                              dirty_differ(dirty_one, dirty_two, 16), word);
                }
                if (DIRTY_HAS(host_into))
                {
                        reference_fill(dirty_one, 0x55, 40);
                        reference_fill(dirty_two, 0x55, 40);
                        positive clean = DIRTY_ROUTINE(host_into)(dirty_one + 8, C32(word));
                        positive answer = DIRTY(2, host_into)(P(dirty_two + 8), D32(word));
                        AGREE(host_into, clean, answer, word);
                        AGREE(host_into, 0, dirty_differ(dirty_one, dirty_two, 40), word);
                }
        }
}

/*
        The byte classes: every class number and some that are none, every
        byte, and the int-taking predicates and cases over EOF, every byte
        and some values past one.
*/
static fn dirty_classes(void)
{
        typedef b32 (*predicate)(b32);
        static const b32 whiches[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                      13, 255, 0x7fffffff, -1, (b32)0x80000000};
        static const positive routines[] = {
            DIRTY_byte_is_alnum, DIRTY_byte_is_alpha, DIRTY_byte_is_ascii,
            DIRTY_byte_is_blank, DIRTY_byte_is_control, DIRTY_byte_is_digit,
            DIRTY_byte_is_graphic, DIRTY_byte_is_hexadecimal, DIRTY_byte_is_lower,
            DIRTY_byte_is_printable, DIRTY_byte_is_punctuation,
            DIRTY_byte_is_space, DIRTY_byte_is_upper, DIRTY_byte_to_ascii,
            DIRTY_byte_to_lower, DIRTY_byte_to_upper};
        if (DIRTY_HAS(byte_class_holds))
                for (positive w = 0; w < array_count(whiches); w++)
                        for (positive value = 0; value < 256; value++)
                        {
                                b32 which = whiches[w];
                                positive where = w << 8 | value;
                                p8 clean = DIRTY_ROUTINE(byte_class_holds)(CS32(which),
                                                                           C8(value));

                                AGREE(byte_class_holds, clean,
                                      (p8)DIRTY(2, byte_class_holds)(D32(which), P(C8(value))),
                                      where);
                                AGREE(byte_class_holds, clean,
                                      (p8)DIRTY(2, byte_class_holds)(P(CS32(which)), D8(value)),
                                      where);
                        }

        for (positive r = 0; r < array_count(routines); r++)
        {
                predicate routine = (predicate)dirty_addresses[routines[r]];

                if (!routine)
                        continue;
                for (bipolar value = -300; value < 600; value++)
                {
                        b32 v = value < 520 ? (b32)value
                                            : value == 599 ? (b32)0x80000000
                                                           : (b32)dirty_random();

                        dirty_agree(routines[r], (p32)routine(CS32(v)),
                                    (p32)((dirty_1)dirty_launder((address_any)routine))(
                                        D32(v)),
                                    (positive)(p32)v);
                }
        }
}

/*
        The int arguments that are not bytes: a checksum's running value, a
        code point, a bit pattern, a signal base, a number base, an index
        and a jump's value.
*/
static fn dirty_words_32(void)
{
        static const b32 bases[] = {0, 2, 8, 10, 16, 36, 1, 37, -1, 17};
        static const char address_to inputs[] = {
            "0", "123", "-42", "0x1f", "077", "zz", "  12", "1e3",
            "99999999999999999999999", "", "+7", "0b101", "-0x80000000"};
        for (positive size = 0; size <= 300; size += size < 72 ? 1 : 57)
        {
                p8 address_to at = dirty_in + 64 + (size & 15);
                p32 seed = (p32)dirty_random();

                for (positive i = 0; i < size; i++)
                        at[i] = (p8)dirty_random();
                if (DIRTY_HAS(hash_crc32))
                        AGREE(hash_crc32, DIRTY_ROUTINE(hash_crc32)(C32(seed), at, size),
                              (p32)DIRTY(3, hash_crc32)(D32(seed), P(at), size), size);
                if (DIRTY_HAS(hash_crc32c))
                        AGREE(hash_crc32c, DIRTY_ROUTINE(hash_crc32c)(C32(seed), at, size),
                              (p32)DIRTY(3, hash_crc32c)(D32(seed), P(at), size), size);
                if (DIRTY_HAS(hash_crc32_msb))
                        AGREE(hash_crc32_msb,
                              DIRTY_ROUTINE(hash_crc32_msb)(C32(seed), at, size),
                              (p32)DIRTY(3, hash_crc32_msb)(D32(seed), P(at), size), size);
                if (DIRTY_HAS(memory_checksum_bsd16))
                        AGREE(memory_checksum_bsd16,
                              DIRTY_ROUTINE(memory_checksum_bsd16)(at, size, C32(seed)),
                              (p32)DIRTY(3, memory_checksum_bsd16)(P(at), size, D32(seed)),
                              size);
        }

        for (positive turn = 0; turn < 20000; turn++)
        {
                p32 code = turn < 12000 ? (p32)(turn * 97)
                           : turn < 12512 ? (p32)(0x10ff00 + turn - 12000)
                           : turn < 12600 ? (p32)(0xffffffffu - (turn - 12512))
                                          : (p32)dirty_random();
                b32 bits = (b32)(turn < 64 ? (positive)1 << (turn & 31) : dirty_random());

                if (DIRTY_HAS(unicode_width))
                        AGREE(unicode_width,
                              DIRTY_ROUTINE(unicode_width)(C32(code), turn & 1),
                              DIRTY(2, unicode_width)(D32(code), turn & 1), code);
                if (DIRTY_HAS(unicode_case))
                        AGREE(unicode_case,
                              DIRTY_ROUTINE(unicode_case)(C32(code), turn % 3),
                              (p32)DIRTY(2, unicode_case)(D32(code), turn % 3), code);
                if (turn < 4096 && DIRTY_HAS(bits_first_set))
                        AGREE(bits_first_set, (p32)DIRTY_ROUTINE(bits_first_set)(CS32(bits)),
                              (p32)DIRTY(1, bits_first_set)(D32(bits)), (p32)bits);
                if (turn < 4096 && DIRTY_HAS(absolute_whole))
                        AGREE(absolute_whole, (p32)DIRTY_ROUTINE(absolute_whole)(CS32(bits)),
                              (p32)DIRTY(1, absolute_whole)(D32(bits)), (p32)bits);
                //      No narrow argument, but a b32 answer: -1 as the
                //      convention hands it back, read at its width.
                if (turn < 4096 && DIRTY_HAS(limbs_compare))
                {
                        static p64 limbs_left[4], limbs_right[4];

                        limbs_left[turn & 3] = (p64)dirty_random();
                        limbs_right[turn & 3] = turn & 4 ? limbs_left[turn & 3]
                                                         : (p64)dirty_random();
                        AGREE(limbs_compare,
                              (p32)DIRTY_ROUTINE(limbs_compare)(limbs_left, limbs_right, 4),
                              (p32)DIRTY(3, limbs_compare)(P(limbs_left), P(limbs_right), 4),
                              turn);
                }
                if (turn < 4096 && DIRTY_HAS(wait_status_code_base))
                {
                        static const b32 signal_bases[] = {0, 128, 256, 384, 0x10080};
                        positive raw = turn < 1024 ? (turn & 0x7f) | (turn >> 7) << 8
                                                   : turn * 0x9e37 & 0xffff;
                        b32 base = signal_bases[turn % array_count(signal_bases)];

                        AGREE(wait_status_code_base,
                              (p32)DIRTY_ROUTINE(wait_status_code_base)(raw, CS32(base)),
                              (p32)DIRTY(2, wait_status_code_base)(raw, D32(base)),
                              raw << 32 | (p32)base);
                }
        }

        for (positive i = 0; i < array_count(inputs); i++)
                for (positive b = 0; b < array_count(bases); b++)
                {
                        string_address input = (string_address)inputs[i];
                        string_address stopped_one = null, stopped_two = null;
                        b32 range_one = 7, range_two = 7;
                        positive where = i << 8 | (p8)bases[b];

                        if (DIRTY_HAS(string_to_number))
                        {
                                AGREE(string_to_number,
                                      DIRTY_ROUTINE(string_to_number)(input, &stopped_one,
                                                                      CS32(bases[b])),
                                      DIRTY(3, string_to_number)(P(input), P(&stopped_two),
                                                                 D32(bases[b])),
                                      where);
                                AGREE(string_to_number, stopped_one, stopped_two, where);
                        }
                        if (DIRTY_HAS(string_to_number_unsigned))
                        {
                                AGREE(string_to_number_unsigned,
                                      DIRTY_ROUTINE(string_to_number_unsigned)(
                                          input, &stopped_one, CS32(bases[b])),
                                      DIRTY(3, string_to_number_unsigned)(
                                          P(input), P(&stopped_two), D32(bases[b])),
                                      where);
                                AGREE(string_to_number_unsigned, stopped_one, stopped_two,
                                      where);
                        }
                        if (DIRTY_HAS(string_to_number_checked))
                        {
                                AGREE(string_to_number_checked,
                                      DIRTY_ROUTINE(string_to_number_checked)(
                                          input, &stopped_one, CS32(bases[b]), &range_one),
                                      DIRTY(4, string_to_number_checked)(
                                          P(input), P(&stopped_two), D32(bases[b]),
                                          P(&range_two)),
                                      where);
                                AGREE(string_to_number_checked, stopped_one, stopped_two,
                                      where);
                                AGREE(string_to_number_checked, range_one, range_two, where);
                        }
                        if (DIRTY_HAS(string_to_number_unsigned_checked))
                        {
                                AGREE(string_to_number_unsigned_checked,
                                      DIRTY_ROUTINE(string_to_number_unsigned_checked)(
                                          input, &stopped_one, CS32(bases[b]), &range_one),
                                      DIRTY(4, string_to_number_unsigned_checked)(
                                          P(input), P(&stopped_two), D32(bases[b]),
                                          P(&range_two)),
                                      where);
                                AGREE(string_to_number_unsigned_checked, stopped_one,
                                      stopped_two, where);
                                AGREE(string_to_number_unsigned_checked, range_one,
                                      range_two, where);
                        }
                }

        if (DIRTY_HAS(program_argument) && DIRTY_HAS(program_environment))
        {
                b32 count = program_argument_count();
                positive environment = 0;
                string_address address_to list = program_environment_list();

                while (list && list[environment])
                        environment++;
                for (b32 index = 0; index <= count; index++)
                        AGREE(program_argument,
                              DIRTY_ROUTINE(program_argument)(CS32(index)),
                              DIRTY(1, program_argument)(D32(index)), index);
                for (b32 index = 0; index <= (b32)environment && index < 64; index++)
                        AGREE(program_environment,
                              DIRTY_ROUTINE(program_environment)(CS32(index)),
                              DIRTY(1, program_environment)(D32(index)), index);
        }
}

/*
        A jump's value is an int, and nought arrives as one. The value comes
        back through the mark's second arrival, so each round is guarded
        against a third.
*/
#if !defined(VERIFY_NARROW_KERNEL)
static jump_state dirty_jump;
static volatile positive dirty_jump_rounds;
static volatile b32 dirty_jump_value;

static fn dirty_jumps(void)
{
        static const b32 values[] = {0, 1, 5, -1, 0x7fffffff};
        for (positive v = 0; v < array_count(values); v++)
                for (positive shape = 0; shape < 5; shape++)
                {
                        b32 want = values[v] ? values[v] : 1;

                        dirty_jump_rounds = 0;
                        dirty_jump_value = jump_mark(dirty_jump);
                        if (dirty_jump_rounds++ == 0)
                                DIRTY(2, jump_to_mark)(P(dirty_jump), D32(values[v]));
                        AGREE(jump_to_mark, (p32)want, (p32)dirty_jump_value,
                              v << 8 | shape);

                        for (positive save = 0; save < 2; save++)
                        {
                                dirty_jump_rounds = 0;
                                dirty_jump_value = (b32)DIRTY(2, signal_jump_mark)(
                                    P(dirty_jump), save ? D32(1) : D32(0));
                                if (dirty_jump_rounds++ == 0)
                                {
                                        AGREE(signal_jump_mark, save,
                                              dirty_jump[26] != 0, v << 8 | shape);
                                        DIRTY(2, signal_jump_to_mark)(P(dirty_jump),
                                                                      D32(values[v]));
                                }
                                AGREE(signal_jump_to_mark, (p32)want,
                                      (p32)dirty_jump_value, v << 8 | shape);
                        }
                }
}
#endif

#ifdef VERIFY_NARROW_KERNEL
/*
        Canvas's rectangles, blits, glyphs and cells, over one pitch of three
        hundred and twenty pixels, every colour a u32 with junk above it.
        The wide bodies run here outside the kernel's bracket, which is
        theirs to need only in ring 0.
*/
#define DIRTY_PIXELS 6144
static p32 dirty_canvas_one[DIRTY_PIXELS], dirty_canvas_two[DIRTY_PIXELS];
static p32 dirty_from[512];
static p8 dirty_font[256 * 16];
static struct dirty_cell dirty_cells[48];

static fn dirty_canvas_reset(void)
{
        for (positive i = 0; i < DIRTY_PIXELS; i++)
                dirty_canvas_one[i] = dirty_canvas_two[i] = (p32)(0x5a5a0000u ^ i);
}

static positive dirty_canvas_differ(void)
{
        return dirty_differ((p8 address_to)dirty_canvas_one,
                            (p8 address_to)dirty_canvas_two,
                            sizeof(dirty_canvas_one));
}

/*
        canvas_row_blit against the C it replaced: every count from nought to
        three hundred, the destination and the source each at every pixel
        offset from nought to three, and both the copy (opaque nought) and
        the mask taking the opaque bits.
*/
static fn dirty_row_blit_every(void)
{
        static p32 source[320 + 8], target[320 + 8], want[320 + 8];

        for (positive i = 0; i < array_count(source); i++)
                source[i] = (p32)dirty_random();
        for (positive count = 0; count <= 300; count++)
                for (positive at = 0; at < 4; at++)
                        for (positive from = 0; from < 4; from++)
                                for (positive turn = 0; turn < 2; turn++)
                                {
                                        p32 opaque = turn ? 0xa5000000u | (p32)dirty_random() : 0;

                                        for (positive i = 0; i < array_count(target); i++)
                                                target[i] = want[i] = (p32)(0x5a5a0000u ^ i);
                                        for (positive i = 0; i < count; i++)
                                                want[at + i] = turn ? (p32)(source[from + i] | opaque)
                                                                    : source[from + i];
                                        DIRTY(4, canvas_row_blit)(P(target + at), P(source + from), count, D32(opaque));
                                        for (positive i = 0; i < array_count(target); i++)
                                                if (target[i] != want[i])
                                                {
                                                        failures++;
                                                        break;
                                                }
                                }
}

static fn dirty_canvas(void)
{
        dirty_row_blit_every();
        static const positive widths[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17,
                                          31, 32, 33, 63, 64, 65, 207, 208, 209, 300};
        static const positive heights[] = {0, 1, 3};
        p32 address_to one = dirty_canvas_one + 3;
        p32 address_to two = dirty_canvas_two + 3;
        for (positive i = 0; i < sizeof(dirty_font); i++)
                dirty_font[i] = (p8)dirty_random();
        for (positive i = 0; i < array_count(dirty_from); i++)
                dirty_from[i] = (p32)dirty_random();
        for (positive i = 0; i < array_count(dirty_cells); i++)
        {
                dirty_cells[i].character = (p32)dirty_random();
                dirty_cells[i].ink = (p8)i;
                dirty_cells[i].paper = (p8)(i * 7);
                dirty_cells[i].flags = 0;
        }

        for (positive turn = 0; turn < 6; turn++)
        {
                p32 colour = turn == 0 ? 0 : turn == 1 ? 0xffffffffu : (p32)dirty_random();
                p32 other = turn == 1 ? 0 : (p32)dirty_random();
                p32 opaque = turn % 3 == 0 ? 0 : turn % 3 == 1 ? 0xff000000u : colour;

                for (positive w = 0; w < array_count(widths); w++)
                {
                        positive width = widths[w];
                        positive where = turn << 16 | width;

                        for (positive h = 0; h < array_count(heights); h++)
                        {
                                positive height = heights[h];

                                if (DIRTY_HAS(canvas_rect_fill))
                                {
                                        dirty_canvas_reset();
                                        kernel_canvas_rect_fill(one, 320, width, height, C32(colour));
                                        DIRTY(5, canvas_rect_fill)(P(two), 320, width, height, D32(colour));
                                        AGREE(canvas_rect_fill, 0, dirty_canvas_differ(), where | height << 12);
                                }
                                if (DIRTY_HAS(canvas_rect_fill_wide))
                                {
                                        dirty_canvas_reset();
                                        kernel_canvas_rect_fill_wide(one, 320, width, height, C32(colour));
                                        DIRTY(5, canvas_rect_fill_wide)(P(two), 320, width, height, D32(colour));
                                        AGREE(canvas_rect_fill_wide, 0, dirty_canvas_differ(), where | height << 12);
                                }
                        }
                        if (DIRTY_HAS(canvas_row_blit))
                        {
                                dirty_canvas_reset();
                                kernel_canvas_row_blit(one, dirty_from, width, C32(opaque));
                                DIRTY(4, canvas_row_blit)(P(two), P(dirty_from), width, D32(opaque));
                                AGREE(canvas_row_blit, 0, dirty_canvas_differ(), where);
                        }
                        if (DIRTY_HAS(canvas_row_blit_wide))
                        {
                                dirty_canvas_reset();
                                kernel_canvas_row_blit_wide(one, dirty_from, width, C32(opaque));
                                DIRTY(4, canvas_row_blit_wide)(P(two), P(dirty_from), width, D32(opaque));
                                AGREE(canvas_row_blit_wide, 0, dirty_canvas_differ(), where);
                        }
                        if (DIRTY_HAS(canvas_cells) && width <= 40)
                        {
                                dirty_canvas_reset();
                                kernel_canvas_cells(one, 320, dirty_font, dirty_cells, width,
                                                    C32(colour), C32(other));
                                DIRTY(7, canvas_cells)(P(two), 320, P(dirty_font), P(dirty_cells),
                                                       width, D32(colour), D32(other));
                                AGREE(canvas_cells, 0, dirty_canvas_differ(), where);
                        }
                        if (DIRTY_HAS(canvas_cells_wide) && width <= 40)
                        {
                                dirty_canvas_reset();
                                kernel_canvas_cells_wide(one, 320, dirty_font, dirty_cells, width,
                                                         C32(colour), C32(other));
                                DIRTY(7, canvas_cells_wide)(P(two), 320, P(dirty_font), P(dirty_cells),
                                                            width, D32(colour), D32(other));
                                AGREE(canvas_cells_wide, 0, dirty_canvas_differ(), where);
                        }
                }

                for (positive rows = 0; rows <= 16; rows++)
                        for (positive stride = 1; stride <= 2; stride++)
                        {
                                p8 address_to bits = dirty_font + turn * 37 + rows;
                                positive where = turn << 16 | rows << 8 | stride;

#define DIRTY_GLYPH_CASE(name)                                                   \
                                if (DIRTY_HAS(name))                            \
                                {                                               \
                                        dirty_canvas_reset();                   \
                                        kernel_##name(one, 40, bits, stride, rows, C32(colour)); \
                                        DIRTY(6, name)(P(two), 40, P(bits), stride, rows, D32(colour)); \
                                        AGREE(name, 0, dirty_canvas_differ(), where); \
                                }
#define DIRTY_CELL_CASE(name)                                                    \
                                if (DIRTY_HAS(name) && stride == 1)             \
                                {                                               \
                                        dirty_canvas_reset();                   \
                                        kernel_##name(one, 40, bits, rows, C32(colour), C32(other)); \
                                        DIRTY(6, name)(P(two), 40, P(bits), rows, D32(colour), D32(other)); \
                                        AGREE(name, 0, dirty_canvas_differ(), where); \
                                }
                                DIRTY_GLYPH_CASE(canvas_glyph)
                                DIRTY_GLYPH_CASE(canvas_glyph2)
                                DIRTY_GLYPH_CASE(canvas_glyph_wide)
                                DIRTY_GLYPH_CASE(canvas_glyph2_wide)
                                DIRTY_CELL_CASE(canvas_cell)
                                DIRTY_CELL_CASE(canvas_cell2)
                                DIRTY_CELL_CASE(canvas_cell_wide)
                                DIRTY_CELL_CASE(canvas_cell2_wide)
#undef DIRTY_GLYPH_CASE
#undef DIRTY_CELL_CASE
                        }
        }
}
#endif

fn check_dirty_arguments(void)
{
        for (positive r = 0; r < DIRTY_ROUTINES; r++)
                dirty_compared[r] = dirty_wrong[r] = 0;
        dirty_either_compared = dirty_either_wrong = 0;

        dirty_tiers(dirty_hunts);
        dirty_tiers(dirty_fill);
        dirty_tiers(dirty_offsets);
        dirty_tiers(dirty_record_scans);
        dirty_tiers(dirty_escape);
        dirty_tiers(dirty_spelled);
        dirty_tiers(dirty_words);
        dirty_tiers(dirty_records);
        dirty_tiers(dirty_prepare);
        dirty_tiers(dirty_strings);
        dirty_tiers(dirty_utf8_valid);
        dirty_tiers(dirty_ascii);
        dirty_writers();
        dirty_into();
        dirty_buffered();
        dirty_orders();
        dirty_classes();
        dirty_words_32();
#if !defined(VERIFY_NARROW_KERNEL)
        dirty_jumps();
#else
        dirty_canvas();
#endif

        positive compared = 0, absent = 0;
        for (positive r = 0; r < DIRTY_ROUTINES; r++)
                if (!dirty_addresses[r])
                        string_format(log, "%s %s", absent++ ? "" : "  NOTE not in this library:",
                                      dirty_names[r]);
        if (absent)
                string_format(log, "\n");
        for (positive r = 0; r < DIRTY_ROUTINES; r++)
        {
                compared += dirty_compared[r];
#ifdef VERIFY_NARROW_TALLY
                string_format(log, "  %s: %p compared, %p wrong\n", dirty_names[r],
                              dirty_compared[r], dirty_wrong[r]);
#endif
                if (!dirty_addresses[r])
                        continue;
                same(dirty_names[r], "answers that change with the bits above "
                                     "a narrow argument",
                     dirty_wrong[r], 0);
                if (dirty_wrong[r])
                        string_format(log, "  %s: %p of %p answers changed\n",
                                      dirty_names[r], dirty_wrong[r],
                                      dirty_compared[r]);
                same(dirty_names[r], "asked with bits above a narrow argument",
                     dirty_compared[r] > 0, 1);
        }
        if (dirty_addresses[DIRTY_string_first_of_either_or_end])
        {
                same("string_first_of_either_or_end", "the strings it was asked of, against the lexer's loop",
                     dirty_either_wrong, 0);
                same("string_first_of_either_or_end", "asked of strings", dirty_either_compared > 0, 1);
        }
        string_format(log, "  NOTE narrow arguments: %p answers compared, "
                           "%p routines not in this library\n",
                      compared, absent);
}

/*
        The record scans against byte loops: memory_offsets_fields and
        _blank at every size to 200 and every residue of sixty four, longer
        sizes at a few, limits that stop inside a vector and resume from
        where they said, and separators dense, sparse, absent and total;
        memory_nth_of, memory_nth_last_of and memory_last_of_either over the
        same sizes and residues with the value at three densities and n on
        either side of the count. On x86_64 once per body: AVX-512 with
        VBMI, without it, and SSE2.
*/
static positive record_fields_reference(p32 address_to out, const p8 address_to at,
                                        positive size, const b8 address_to separators)
{
        positive count = 0;

        for (positive i = 0; i < size;)
        {
                if (separators[at[i]])
                {
                        i++;
                        continue;
                }
                positive start = i;

                while (i < size && !separators[at[i]])
                        i++;
                out[2 * count] = (p32)start;
                out[2 * count + 1] = (p32)i;
                count++;
        }
        return count;
}

/*
        The record scans against a block whose neighbours are unmapped: a
        short part is read from the aligned sixty four bytes that hold it, and
        that must never reach a page the block does not. Every size to 200 is
        put against the start of a mapped page and against its end, with
        PROT_NONE on both sides, on every body the machine has. A read past
        either edge is a fault, which ends the run and so fails the lane.
*/
fn check_record_guards()
{
        static p32 got[2 * 256];
        static b8 table[256];
        p8 address_to pages = guard_pages("record scans");

        if (!pages)
                return;
        for (positive i = 0; i < 256; i++)
                table[i] = i == ' ' || i == '\t';
#if X64
        p8 avx512 = cpu_has_avx512, vbmi = cpu_has_avx512_vbmi, avx2 = cpu_has_avx2;
        // AVX-512 with VBMI, AVX-512 without it, AVX2 without AVX-512 (the table form's nibble classes), and SSE2.
        positive tiers = 4;
#elif RISCV64
        // With V, then the word body without it, which is what a machine without V runs.
        p8 vector = cpu_has_vector;
        positive tiers = 2;
#else
        positive tiers = 1;
#endif
        p8 address_to middle = pages + 4096;
        positive sink = 0;

        for (positive tier = 0; tier < tiers; tier++)
        {
#if X64
                cpu_has_avx512 = tier < 2 ? avx512 : 0;
                cpu_has_avx512_vbmi = tier == 0 ? vbmi : 0;
                cpu_has_avx2 = tier == 3 ? 0 : avx2;
#elif RISCV64
                cpu_has_vector = tier == 0 ? vector : 1;
#endif
                for (positive size = 0; size <= 200; size++)
                        for (positive edge = 0; edge < 2; edge++)
                        {
                                p8 address_to at = edge ? middle + 4096 - size : middle;

                                for (positive i = 0; i < size; i++)
                                        at[i] = (p8)(next() & 3 ? 'a' + i % 7 : next() & 1 ? ' ' : '\t');
                                VECTOR_FIRST_CALL(tier == 0);
                                sink += (memory_offsets_fields)(got, at, size, table, 256);
                                VECTOR_FIRST_CALL(tier == 0);
                                sink += (memory_offsets_fields_blank)(got, at, size, '\n', 256);
                                sink += (memory_offsets_fields_blank)(got, at, size, 'a', 3);
                                sink += (memory_nth_of)(at, ' ', size, 1 + size % 5).x;
                                sink += (memory_nth_last_of)(at, ' ', size, 1 + size % 5).x;
                                sink += (positive)(memory_last_of_either)(at, ' ', '\t', size);
                        }
        }
#if X64
        cpu_has_avx512 = avx512;
        cpu_has_avx512_vbmi = vbmi;
        cpu_has_avx2 = avx2;
#elif RISCV64
        cpu_has_vector = vector;
#endif
        // Reached: nothing faulted.
        same("record scans", "reads stay inside the block", sink >= 0, 1);
        memory_free(pages, 3 * 4096);
}

fn check_record_scans()
{
        static const positive sizes[] = {255, 256, 257, 511, 1000, 4095, 4096};
        static p32 got[2 * 4200 + 8], want[2 * 4200 + 8];
        static p8 block[4096 + 256];
        static b8 table[256];
        static const p8 extras[] = {' ', '\n', 0, 0xff, 'a'};
        static const positive limits[] = {1, 2, 3, 7, 31, 32, 33, 64, 65, 5000};
#if X64
        p8 avx512 = cpu_has_avx512, vbmi = cpu_has_avx512_vbmi, avx2 = cpu_has_avx2;
        // AVX-512 with VBMI, AVX-512 without it, AVX2 without AVX-512 (the table form's nibble classes), and SSE2.
        positive tiers = 4;
#elif RISCV64
        // With V, then the word body without it, which is what a machine without V runs.
        p8 vector = cpu_has_vector;
        positive tiers = 2;
#else
        positive tiers = 1;
#endif
        for (positive tier = 0; tier < tiers; tier++)
        {
#if X64
                cpu_has_avx512 = tier < 2 ? avx512 : 0;
                cpu_has_avx512_vbmi = tier == 0 ? vbmi : 0;
                cpu_has_avx2 = tier == 3 ? 0 : avx2;
#elif RISCV64
                cpu_has_vector = tier == 0 ? vector : 1;
#endif
                for (positive turn = 0; turn < 12; turn++)
                {
                        // What the block is made of this turn: separators
                        // dense, sparse, absent, total or random bytes.
                        p8 extra = extras[turn % array_count(extras)];
                        positive kind = turn % 6;

                        for (positive i = 0; i < sizeof(block); i++)
                        {
                                positive r = next();
                                p8 b = (p8)('a' + r % 26);
                                positive pick = (r >> 8) & 15;

                                if (kind == 0 && pick < 6)
                                        b = pick < 2 ? ' ' : pick < 4 ? '\t' : extra;
                                else if (kind == 1 && pick == 0)
                                        b = (r >> 12) & 1 ? ' ' : extra;
                                else if (kind == 3)
                                        b = pick < 8 ? ' ' : '\t';
                                else if (kind == 4)
                                        b = (p8)(r >> 16);
                                else if (kind == 5 && pick < 3)
                                        b = 0;
                                block[i] = b;
                        }
                        for (positive i = 0; i < 256; i++)
                                table[i] = i == ' ' || i == '\t' || i == extra;
                        // Every size to 200 at every residue, then longer ones.
                        for (positive s = 0; s < 201 + array_count(sizes); s++)
                                for (positive residue = 0; residue < 64;
                                     residue += s < 201 ? 1 : 13)
                                {
                                        positive size = s < 201 ? s : sizes[s - 201];
                                        const p8 address_to at = block + residue;
                                        positive count = record_fields_reference(want, at, size, table);
                                        positive limit = limits[(s + residue + turn) % array_count(limits)];

                                        for (positive form = 0; form < 2; form++)
                                        {
                                                positive have = 0, from = 0;
                                        p8 ok = true;
                                                for (;;)
                                                {
                                                        VECTOR_FIRST_CALL(tier == 0);
                                                        positive answer = form
                                                            ? (memory_offsets_fields)(got, at + from, size - from, table, limit)
                                                            : (memory_offsets_fields_blank)(got, at + from, size - from, extra, limit);

                                                        if (answer > limit)
                                                        {
                                                                ok = false;
                                                                break;
                                                        }
                                                        for (positive i = 0; i < 2 * answer; i++)
                                                                if (2 * have + i >= 2 * count ||
                                                                    got[i] + from != want[2 * have + i])
                                                                        ok = false;
                                                        have += answer;
                                                        if (answer < limit || !ok)
                                                                break;
                                                        from += got[2 * answer - 1];
                                                }
                                                if (!ok || have != count)
                                                        string_format(log, "  record fields: form %p tier %p turn %p size %p residue %p limit %p\n",
                                                                      form, tier, turn, size, residue, limit);
                                                same(form ? "memory_offsets_fields" : "memory_offsets_fields_blank",
                                                     "fields", ok, 1);
                                                same(form ? "memory_offsets_fields" : "memory_offsets_fields_blank",
                                                     "count", have, count);
                                        }
                                }
                }
                // The table form with sets no three bytes spell.
                for (positive t = 0; t < 3; t++)
                {
                        for (positive i = 0; i < 256; i++)
                                table[i] = t == 0 ? 0 : t == 1 ? (b8)(i < 33 || i > 126) : (b8)(next() & 1);
                        for (positive i = 0; i < sizeof(block); i++)
                                block[i] = (p8)next();
                        for (positive size = 0; size < 300; size += 1 + size / 16)
                                for (positive residue = 0; residue < 64; residue += 7)
                                {
                                        const p8 address_to at = block + residue;
                                        positive count = record_fields_reference(want, at, size, table);
                                        positive answer = (memory_offsets_fields)(got, at, size, table, 5000);
                                        p8 ok = answer == count;

                                        for (positive i = 0; ok && i < 2 * count; i++)
                                                ok = got[i] == want[i];
                                        same("memory_offsets_fields", "a table set", ok, 1);
                                }
                }
                // n-th from either end, and the last of two.
                for (positive turn = 0; turn < 6; turn++)
                {
                        p8 value = extras[turn % array_count(extras)];
                        p8 second = (p8)(value ^ 0x41);
                        positive density = turn % 3 == 0 ? 2 : turn % 3 == 1 ? 8 : 64;

                        for (positive i = 0; i < sizeof(block); i++)
                        {
                                positive r = next();
                                p8 b = (p8)r;

                                if (b == value || b == second)
                                        b ^= 0x10;
                                if ((r >> 16) % density == 0)
                                        b = (r >> 24) & 1 ? value : second;
                                block[i] = b;
                        }
                        for (positive s = 0; s < 201 + array_count(sizes); s++)
                                for (positive residue = 0; residue < 64;
                                     residue += s < 201 ? 1 : 13)
                                {
                                        positive size = s < 201 ? s : sizes[s - 201];
                                        const p8 address_to at = block + residue;
                                        positive count = 0;
                                        const p8 address_to last = null;

                                        for (positive i = 0; i < size; i++)
                                        {
                                                count += at[i] == value;
                                                if (at[i] == value || at[i] == second)
                                                        last = at + i;
                                        }
                                        same("memory_last_of_either", "address",
                                             (positive)(memory_last_of_either)((address_any)at, (b8)value, (b8)second, size),
                                             (positive)last);

                                        positive ns[] = {0, 1, 2, 3, count ? count - 1 : 0, count, count + 1,
                                                         1 + next() % (count + 1), positive_max};
                                        for (positive k = 0; k < array_count(ns); k++)
                                        {
                                                positive n = ns[k];
                                                positive ahead = n ? size : 0, seen = 0;
                                                positive back = n ? 0 : size, seen_back = 0;

                                                for (positive i = 0; n && i < size; i++)
                                                        if (at[i] == value && ++seen == n)
                                                        {
                                                                ahead = i + 1;
                                                                break;
                                                        }
                                                for (positive i = size; n && i-- > 0;)
                                                        if (at[i] == value && ++seen_back == n)
                                                        {
                                                                back = i;
                                                                break;
                                                        }
                                                positive2 front = (memory_nth_of)((address_any)at, (b8)value, size, n);
                                                positive2 rear = (memory_nth_last_of)((address_any)at, (b8)value, size, n);

                                                if (front.x != ahead || front.y != seen ||
                                                    rear.x != back || rear.y != seen_back)
                                                        string_format(log, "  nth: tier %p turn %p size %p residue %p n %p: %p/%p want %p/%p, last %p/%p want %p/%p\n",
                                                                      tier, turn, size, residue, n, front.x, front.y, ahead, seen,
                                                                      rear.x, rear.y, back, seen_back);
                                                same("memory_nth_of", "offset", front.x, ahead);
                                                same("memory_nth_of", "seen", front.y, seen);
                                                same("memory_nth_last_of", "offset", rear.x, back);
                                                same("memory_nth_last_of", "seen", rear.y, seen_back);
                                        }
                                }
                }
        }
#if X64
        cpu_has_avx512 = avx512;
        cpu_has_avx512_vbmi = vbmi;
        cpu_has_avx2 = avx2;
#elif RISCV64
        cpu_has_vector = vector;
#endif
}

/*
        memory_offsets_in_set against a byte loop with the block ending
        against a page that faults: every size to 200 and long ones, every
        residue, tables of no member, one, two (both orders of use), three,
        cat -v's and half the bytes at random, and limits from one to past
        the count with the slots past the limit fenced. On riscv64 with every
        call the first in a process, which asks for V and comes back to the
        lookup, with V known, without V on the processor's Zbb, and without
        either, since a table of one or two members is
        memory_offsets_of_either's and the rest is the routine's own.
*/
fn check_offsets_in_set_pages()
{
        static const positive sizes[] = {1000, 4096};
        static p32 got[4200], want[4200];
        static b8 sets[6][256];
        p8 address_to pages = guard_pages("memory_offsets_in_set");

        if (!pages)
                return;
        p8 address_to bytes = pages + 4096;
        p64 seed = 0x452821e638d01377ull;

        for (positive i = 0; i < 256; i++)
        {
                sets[0][i] = 0;
                sets[1][i] = i == '\n';
                sets[2][i] = i == '\t' || i == ',';
                sets[3][i] = i == ',' || i == '\n' || i == 'x';
                sets[4][i] = i < 9 || (i > 10 && i < 32) || i > 126;
                XORSHIFT64(seed);
                sets[5][i] = (b8)(seed & 1);
        }
#if RISCV64
        p8 vector = 0, zbb = cpu_has_zbb;
        positive tiers = 4;
#else
        positive tiers = 1;
#endif
        for (positive tier = 0; tier < tiers; tier++)
        {
#if RISCV64
                if (tier == 2)
                {
                        vector = cpu_has_vector;
                        cpu_has_vector = 1;
                }
                if (tier == 3)
                {
                        if (!zbb)
                                continue;
                        cpu_has_zbb = 0;
                }
#endif
                for (positive t = 0; t < array_count(sets); t++)
                {
                        for (positive s = 0; s < 201 + array_count(sizes); s += s < 70 ? 1 : 7)
                                for (positive residue = 0; residue <= 64; residue += residue < 9 ? 1 : 11)
                                {
                                        positive size = s <= 200 ? s : sizes[s - 201];
                                        // The block's end against the page that faults, or a few bytes short.
                                        positive back = residue % 9 < 4096 - size ? residue % 9 : 4096 - size;
                                        positive offset = 4096 - size - back;

                                        for (positive i = offset > 64 ? offset - 64 : 0; i < 4096; i++)
                                        {
                                                XORSHIFT64(seed);
                                                positive pick = (positive)(seed % 16);
                                                bytes[i] = pick == 0 ? ',' : pick == 1 ? '\n' : pick == 2 ? '\t'
                                                           : pick == 3 ? 'x' : (p8)(seed >> 8);
                                        }
                                        positive count = 0;
                                        for (positive i = 0; i < size; i++)
                                                if (sets[t][bytes[offset + i]])
                                                        want[count++] = (p32)i;

                                        static const positive limits[] = {1, 2, 3, 4, 7, 64, 65, 5000};
                                        for (positive l = 0; l < array_count(limits); l++)
                                        {
                                                positive limit = limits[l], have = 0, from = 0;
                                        p8 ok = true;
                                                for (;;)
                                                {
                                                        p8 fenced = limit + 80 <= array_count(got);
                                                        if (fenced)
                                                                for (positive i = 0; i < 80; i++)
                                                                        got[limit + i] = 0xdeadbeefu;
                                                        // The detect's return must not leave its
                                                        // registers in the count or the offset.
                                                        VECTOR_FIRST_CALL(tier == 0);
                                                        positive answer = (memory_offsets_in_set)(
                                                            got, bytes + offset + from, size - from, sets[t], limit);
                                                        if (answer > limit)
                                                        {
                                                                ok = false;
                                                                break;
                                                        }
                                                        if (fenced)
                                                                for (positive i = 0; i < 80; i++)
                                                                        if (got[limit + i] != 0xdeadbeefu)
                                                                                ok = false;
                                                        for (positive i = 0; i < answer; i++)
                                                                if (have + i >= count || got[i] + from != want[have + i])
                                                                        ok = false;
                                                        have += answer;
                                                        if (answer < limit || !ok)
                                                                break;
                                                        from += got[answer - 1] + 1;
                                                }
                                                same("memory_offsets_in_set pages", "offsets", ok, 1);
                                                same("memory_offsets_in_set pages", "count", have, count);
                                        }
                                }
                }
        }
#if RISCV64
        cpu_has_vector = vector;
        cpu_has_zbb = zbb;
#endif
        memory_free(pages, 3 * 4096);
}

/*
        memory_offsets_between and memory_offsets_outside against a byte loop
        with the block ending against a page that faults: every size to 200
        and long ones, every residue, ranges of one value to all of them and
        either side of a width of 128 -- the word body tests the width, or
        255 less it, against a byte of at most 128 -- and limits from one to
        past the count with the slots past the limit fenced. On riscv64 every
        call the first in a process, which asks for V, then without V; there
        is no Zbb body.
*/
fn check_offsets_range_pages()
{
        static const positive sizes[] = {1000, 4096};
        static const p8 ranges[][2] = {{0x20, 0x7e}, {8, 10}, {0, 0}, {0xff, 0xff}, {0, 0xff}, {'a', 'z'},
                                       {0x80, 0xff}, {0x7f, 0xfe}, {0x10, 0x8f}, {0x10, 0x90}, {1, 0xfe},
                                       {0x40, 0xbf}, {0x40, 0xc0}, {0, 0x7f}, {0, 0x80}};
        static p32 got[4200], want[4200];
        p8 address_to pages = guard_pages("memory_offsets_between pages");

        if (!pages)
                return;
        p8 address_to bytes = pages + 4096;
        p64 seed = 0x3f84d5b5b5470917ull;
#if RISCV64
        p8 vector = 0;
        positive tiers = 2;
#else
        positive tiers = 1;
#endif
        for (positive tier = 0; tier < tiers; tier++)
        {
#if RISCV64
                if (tier == 1)
                {
                        vector = cpu_has_vector;
                        cpu_has_vector = 1;
                }
#endif
                for (positive r = 0; r < array_count(ranges); r++)
                        for (positive outside = 0; outside < 2; outside++)
                                for (positive s = 0; s < 201 + array_count(sizes); s += s < 70 ? 1 : 7)
                                        for (positive residue = 0; residue <= 64; residue += residue < 9 ? 1 : 11)
                                        {
                                                positive size = s <= 200 ? s : sizes[s - 201];
                                                positive back = residue % 9 < 4096 - size ? residue % 9 : 4096 - size;
                                                positive offset = 4096 - size - back;
                                                p8 low = ranges[r][0], high = ranges[r][1];

                                                for (positive i = offset > 64 ? offset - 64 : 0; i < 4096; i++)
                                                {
                                                        XORSHIFT64(seed);
                                                        bytes[i] = (seed & 3) ? (p8)(low + (seed >> 8) % 3 - 1)
                                                                   : (seed & 4) ? (p8)(high + (seed >> 8) % 3 - 1)
                                                                                : (p8)(seed >> 16);
                                                }
                                                positive count = 0;
                                                for (positive i = 0; i < size; i++)
                                                {
                                                        p8 b = bytes[offset + i];
                                                        if ((b >= low && b <= high) != (outside != 0))
                                                                want[count++] = (p32)i;
                                                }
                                                static const positive limits[] = {1, 2, 7, 8, 9, 64, 65, 5000};
                                                for (positive l = 0; l < array_count(limits); l++)
                                                {
                                                        positive limit = limits[l], have = 0, from = 0;
                                                p8 ok = true;
                                                        for (;;)
                                                        {
                                                                p8 fenced = limit + 80 <= array_count(got);
                                                                if (fenced)
                                                                        for (positive i = 0; i < 80; i++)
                                                                                got[limit + i] = 0xdeadbeefu;
                                                                VECTOR_FIRST_CALL(tier == 0);
                                                                positive answer = outside
                                                                    ? (memory_offsets_outside)(got, bytes + offset + from, size - from,
                                                                                               low, high, limit)
                                                                    : (memory_offsets_between)(got, bytes + offset + from, size - from,
                                                                                               low, high, limit);
                                                                if (answer > limit)
                                                                {
                                                                        ok = false;
                                                                        break;
                                                                }
                                                                if (fenced)
                                                                        for (positive i = 0; i < 80; i++)
                                                                                if (got[limit + i] != 0xdeadbeefu)
                                                                                        ok = false;
                                                                for (positive i = 0; i < answer; i++)
                                                                        if (have + i >= count || got[i] + from != want[have + i])
                                                                                ok = false;
                                                                have += answer;
                                                                if (answer < limit || !ok)
                                                                        break;
                                                                from += got[answer - 1] + 1;
                                                        }
                                                        same("memory_offsets_between pages", "offsets", ok, 1);
                                                        same("memory_offsets_between pages", "count", have, count);
                                                }
                                        }
        }
#if RISCV64
        cpu_has_vector = vector;
#endif
        memory_free(pages, 3 * 4096);
}

/*
        memory_offsets_between and memory_offsets_outside against a byte
        loop: the same sizes, residues and limits as memory_offsets_of_either,
        over random bytes, ranges from one value to all of them. On x86_64
        with and without AVX-512.
*/
fn check_offsets_range()
{
        static const positive sizes[] = {0, 1, 15, 16, 17, 63, 64, 65, 129, 1000, 4096};
        static const p8 ranges[][2] = {{0x20, 0x7e}, {8, 10}, {0, 0}, {0xff, 0xff}, {0, 0xff}, {'a', 'z'}};
        static p32 got[4200], want[4200];
        static p8 bytes[4096 + 64];
        p64 seed = 0x13198a2e03707344ull;
#if X64
        p8 avx512 = cpu_has_avx512, vbmi2 = cpu_has_avx512_vbmi2, avx2 = cpu_has_avx2;
        // AVX-512 with VBMI2, AVX-512 with dword packing, AVX2 without AVX-512, and SSE2.
        positive tiers = 4;
#else
        positive tiers = 1;
#endif
        for (positive i = 0; i < sizeof(bytes); i++)
        {
                XORSHIFT64(seed);
                bytes[i] = (seed & 3) ? (p8)('a' + (seed >> 8) % 40) : (p8)(seed >> 16);
        }
        for (positive tier = 0; tier < tiers; tier++)
        {
#if X64
                cpu_has_avx512 = tier < 2 ? avx512 : 0;
                cpu_has_avx512_vbmi2 = tier == 1 ? 1 : vbmi2;
                cpu_has_avx2 = tier == 3 ? 0 : avx2;
#endif
                for (positive r = 0; r < array_count(ranges); r++)
                        for (positive outside = 0; outside < 2; outside++)
                                for (positive s = 0; s < array_count(sizes); s++)
                                        for (positive residue = 0; residue < 64; residue += 21)
                                        {
                                                positive size = sizes[s];
                                                const p8 address_to at = bytes + residue;
                                                p8 low = ranges[r][0], high = ranges[r][1];
                                                positive count = 0;

                                                for (positive i = 0; i < size; i++)
                                                        if ((at[i] >= low && at[i] <= high) != (outside != 0))
                                                                want[count++] = (p32)i;

                                                static const positive limits[] = {1, 7, 64, 65, 5000};
                                                for (positive l = 0; l < array_count(limits); l++)
                                                {
                                                        positive limit = limits[l], have = 0, from = 0;
                                                p8 ok = true;
                                                        for (;;)
                                                        {
                                                                positive answer = outside
                                                                    ? (memory_offsets_outside)(got, at + from, size - from, low, high, limit)
                                                                    : (memory_offsets_between)(got, at + from, size - from, low, high, limit);
                                                                if (answer > limit)
                                                                {
                                                                        ok = false;
                                                                        break;
                                                                }
                                                                for (positive i = 0; i < answer; i++)
                                                                        if (have + i >= count || got[i] + from != want[have + i])
                                                                                ok = false;
                                                                have += answer;
                                                                if (answer < limit || !ok)
                                                                        break;
                                                                from += got[answer - 1] + 1;
                                                        }
                                                        same("memory_offsets_between/outside", "offsets", ok, 1);
                                                        same("memory_offsets_between/outside", "count", have, count);
                                                }
                                        }
                // And by table: none, one byte, cat -v's, and half at random.
                static b8 sets[4][256];
                for (positive i = 0; i < 256; i++)
                {
                        sets[0][i] = 0;
                        sets[1][i] = i == '\n';
                        sets[2][i] = i < 9 || (i > 10 && i < 32) || i > 126;
                        XORSHIFT64(seed);
                        sets[3][i] = (b8)(seed & 1);
                }
                for (positive t = 0; t < 4; t++)
                        for (positive s = 0; s < array_count(sizes); s++)
                                for (positive residue = 0; residue < 64; residue += 21)
                                {
                                        positive size = sizes[s];
                                        const p8 address_to at = bytes + residue;
                                        positive count = 0;

                                        for (positive i = 0; i < size; i++)
                                                if (sets[t][at[i]])
                                                        want[count++] = (p32)i;

                                        static const positive limits[] = {1, 7, 64, 65, 5000};
                                        for (positive l = 0; l < array_count(limits); l++)
                                        {
                                                positive limit = limits[l], have = 0, from = 0;
                                        p8 ok = true;
                                                for (;;)
                                                {
                                                        positive answer = (memory_offsets_in_set)(
                                                            got, at + from, size - from, sets[t], limit);
                                                        if (answer > limit)
                                                        {
                                                                ok = false;
                                                                break;
                                                        }
                                                        for (positive i = 0; i < answer; i++)
                                                                if (have + i >= count || got[i] + from != want[have + i])
                                                                        ok = false;
                                                        have += answer;
                                                        if (answer < limit || !ok)
                                                                break;
                                                        from += got[answer - 1] + 1;
                                                }
                                                same("memory_offsets_in_set", "offsets", ok, 1);
                                                same("memory_offsets_in_set", "count", have, count);
                                        }
                                }
        }
#if X64
        cpu_has_avx512 = avx512;
        cpu_has_avx512_vbmi2 = vbmi2;
        cpu_has_avx2 = avx2;
#endif
}

fn check_checksums()
{
        same("memory_sum_bytes", "null zero-sized span",
             memory_sum_bytes(null, 0), 0);
        same("memory_checksum_bsd16", "null zero-sized span and seed",
             memory_checksum_bsd16(null, 0, 0x12345), 0x2345);

        for (positive size = 0; size <= 1024; size++)
                for (positive residue = 0; residue < 32; residue++)
                {
                        p8 address_to at = pattern + residue;

                        for (positive i = 0; i < size; i++)
                                at[i] = (p8)next();

                        p32 seed = (p32)next();

                        same("memory_sum_bytes", "all small sizes and residues",
                             memory_sum_bytes(at, size),
                             reference_sum_bytes(at, size));
                        same("memory_checksum_bsd16",
                             "all small sizes, residues, and seeds",
                             memory_checksum_bsd16(at, size, seed),
                             reference_checksum_bsd16(at, size, seed));

                        positive split = size ? next() % size : 0;
                        p32 first = memory_checksum_bsd16(at, split, seed);

                        same("memory_checksum_bsd16", "streaming continuation",
                             memory_checksum_bsd16(at + split, size - split,
                                                   first),
                             reference_checksum_bsd16(at, size, seed));
                }

        same("hash_xxh64", "empty seed 0", hash_xxh64(null, 0, 0),
             0xEF46DB3751D8E999ull);
        same("hash_xxh64", "one byte seed 0", hash_xxh64("a", 1, 0),
             0xD24EC4F1A98C6E5Bull);

        for (positive size = 0; size <= 1024; size++)
                for (positive residue = 0; residue < 32; residue++)
                {
                        p8 address_to at = pattern + residue;
                        p64 seed;
                        p64 state[11];
                        p64 got;
                        p64 want;

                        for (positive i = 0; i < size; i++)
                                at[i] = (p8)next();

                        seed = next();
                        seed = (seed << 32) | next();
                        want = reference_xxh64(size ? at : null, size, seed);
                        same("hash_xxh64", "all small sizes, residues, and seeds",
                             hash_xxh64(size ? at : null, size, seed), want);

                        hash_xxh64_begin(state, seed);
                        hash_xxh64_add(state, size ? at : null, size);
                        got = hash_xxh64_finish(state);
                        same("hash_xxh64_add", "one update matches one-shot",
                             got, want);

                        hash_xxh64_begin(state, seed);
                        if (size)
                        {
                                positive mid = size / 2;
                                hash_xxh64_add(state, at, mid);
                                hash_xxh64_add(state, at + mid, size - mid);
                        }
                        got = hash_xxh64_finish(state);
                        same("hash_xxh64_add", "split update matches one-shot",
                             got, want);

                        if (size && size <= 64)
                        {
                                positive i;

                                hash_xxh64_begin(state, seed);
                                for (i = 0; i < size; i++)
                                        hash_xxh64_add(state, at + i, 1);
                                got = hash_xxh64_finish(state);
                                same("hash_xxh64_add", "byte updates match one-shot",
                                     got, want);
                        }
                }

        {
                p8 bytes[16];
                p64 aligned;
                p64 shifted;
                positive i;

                for (i = 0; i < 16; i++)
                        bytes[i] = (p8)i;
                aligned = memory_get64(bytes);
                same("memory_get64", "aligned little-endian", aligned,
                     0x0706050403020100ull);
                shifted = memory_get64(bytes + 1);
                same("memory_get64", "offset-1 little-endian", shifted,
                     0x0807060504030201ull);
        }
}

/*
        The limb family against the C it replaced: text.c's big_add,
        big_subtract and big_compare and the basecase row of its
        big_multiply_into, and tools.c's bit-serial wide divide. Every count
        from nothing to sixty seven limbs, so the single limbs, the pair and
        every remainder of the four-limb loop meet each routine; the second
        operand at every shorter length; the operands flush against a
        protected page on the side each routine walks toward, so a read one
        limb too far faults; the limbs around what is written compared
        whole. Values are random, all ones (every carry taken and walked
        through the longer operand) and zero (every borrow), each against
        each, and the multiplier is 0, 1, all ones and random.
*/
static p64 reference_limbs_add(p64 address_to a, positive n, const p64 address_to b,
                               positive m)
{
        p64 carry = 0;
        positive at = 0;

        for (; at < m; at++)
        {
                p64 sum = a[at] + carry;

                carry = sum < carry;
                sum += b[at];
                carry += sum < b[at];
                a[at] = sum;
        }
        for (; carry && at < n; at++)
                carry = !++a[at];
        return carry;
}

static p64 reference_limbs_subtract(p64 address_to a, positive n,
                                    const p64 address_to b, positive m)
{
        p64 borrow = 0;
        positive at = 0;

        for (; at < m; at++)
        {
                p64 x = a[at];
                p64 y = b[at];

                a[at] = x - y - borrow;
                borrow = (x < y) | ((x == y) & borrow);
        }
        for (; borrow && at < n; at++)
                borrow = !a[at]--;
        return borrow;
}

static p64 reference_limbs_multiply(p64 address_to r, const p64 address_to a,
                                    positive n, p64 w, p64 carry, p8 add)
{
        for (positive j = 0; j < n; j++)
        {
                unsigned __int128 t = (unsigned __int128)a[j] * w + carry +
                                      (add ? r[j] : 0);

                r[j] = (p64)t;
                carry = (p64)(t >> 64);
        }
        return carry;
}

static b32 reference_limbs_compare(const p64 address_to a, const p64 address_to b,
                                   positive n)
{
        while (n--)
                if (a[n] != b[n])
                        return a[n] < b[n] ? -1 : 1;
        return 0;
}

// The divide the numfmt and factor loops did: a bit a step.
static positive2 reference_divide_wide(p64 high, p64 low, p64 divisor)
{
        unsigned __int128 rest = 0;
        unsigned __int128 number = (unsigned __int128)high << 64 | low;
        p64 quotient = 0;

        for (b32 bit = 127; bit >= 0; bit--)
        {
                rest = rest << 1 | (p64)(number >> bit & 1);
                if (rest >= divisor)
                {
                        rest -= divisor;
                        if (bit < 64)
                                quotient |= (p64)1 << bit;
                }
        }
        return (positive2){{quotient, (p64)rest}};
}

static p64 limbs_value(positive shape)
{
        return shape == 0 ? next() : shape == 1 ? ~(p64)0 : shape == 2 ? 0
               : (next() & 1) ? ~(p64)0 : next() & 3;
}

static fn limbs_divide_one(p64 high, p64 low, p64 divisor)
{
        positive2 got = positive_divide_wide(high, low, divisor);
        positive2 want = reference_divide_wide(high, low, divisor);

        same("positive_divide_wide", "quotient", got.x, want.x);
        same("positive_divide_wide", "remainder", got.y, want.y);
}

static fn check_limbs_body()
{
        p8 address_to pages = memory(4 * 4096);
        p8 mapped = (bipolar)(positive)pages > 0;

        same("limbs", "guard mapping", mapped, 1);
        if (!mapped)
                return;
        p8 protected =
            system_call_3(syscall(mprotect), (positive)pages, 4096, 0) == 0 &&
            system_call_3(syscall(mprotect), (positive)(pages + 3 * 4096), 4096, 0) == 0;
        same("limbs", "guard pages protected", protected, 1);
        if (!protected)
        {
                memory_free(pages, 4 * 4096);
                return;
        }

        // Two pages of room: the one written below the other, which is read.
        p64 address_to low_page = (p64 address_to)(pages + 4096);
        p64 address_to high_page = (p64 address_to)(pages + 2 * 4096);
        p64 address_to top = high_page + 512;
        static p64 want[80], operand[80];

        for (positive n = 0; n < 68; n++)
                for (positive shape = 0; shape < 16; shape++)
                        for (positive m = 0; m <= n; m++)
                        {
                                // a in the low page after a guard limb,
                                // b flush against the protected page.
                                p64 address_to a = top - 512 - n - 1;
                                p64 address_to b = top - m;
                                p64 address_to ra = top - n;

                                for (positive i = 0; i < n; i++)
                                        a[i] = limbs_value(shape & 3);
                                for (positive i = 0; i < m; i++)
                                        b[i] = limbs_value(shape >> 2);
                                a[n] = 0x5a5a5a5a5a5a5a5aull;
                                a[-1] = 0xa5a5a5a5a5a5a5a5ull;
                                memory_copy(want, a - 1, (n + 2) * sizeof(p64));
                                same("limbs_add", "carry",
                                     limbs_add(a, n, b, m),
                                     reference_limbs_add(want + 1, n, b, m));
                                same_bytes("limbs_add", "sum and guards", (b8 address_to)(a - 1),
                                           (b8 address_to)want, (n + 2) * sizeof(p64));
                                memory_copy(want, a - 1, (n + 2) * sizeof(p64));
                                same("limbs_subtract", "borrow",
                                     limbs_subtract(a, n, b, m),
                                     reference_limbs_subtract(want + 1, n, b, m));
                                same_bytes("limbs_subtract", "difference and guards",
                                           (b8 address_to)(a - 1), (b8 address_to)want,
                                           (n + 2) * sizeof(p64));

                                // The whole of a, flush at its page's end,
                                // added to and taken from itself.
                                if (m == n && n < 64)
                                {
                                        for (positive i = 0; i < n; i++)
                                                ra[i] = limbs_value(shape & 3);
                                        memory_copy(want, ra, n * sizeof(p64));
                                        same("limbs_add", "a == b",
                                             limbs_add(ra, n, ra, n),
                                             reference_limbs_add(want, n, want, n));
                                        same_bytes("limbs_add", "a == b sum", (b8 address_to)ra,
                                                   (b8 address_to)want, n * sizeof(p64));
                                        same("limbs_subtract", "a == b",
                                             limbs_subtract(ra, n, ra, n), 0);
                                        memory_fill(want, 0, n * sizeof(p64));
                                        same_bytes("limbs_subtract", "a == b is zero",
                                                   (b8 address_to)ra, (b8 address_to)want,
                                                   n * sizeof(p64));
                                }
                        }

        for (positive n = 0; n < 68; n++)
                for (positive shape = 0; shape < 64; shape++)
                {
                        p64 w = shape % 4 == 0 ? next() : shape % 4 == 1 ? ~(p64)0
                                : shape % 4 == 2 ? (p64)(shape & 4) >> 2 : next() >> (next() & 63);
                        p64 carry = (shape & 16) ? ~(p64)0 : next();
                        // r in the low page flush below the high one, a
                        // flush against the protected page.
                        p64 address_to r = high_page - n - 1;
                        p64 address_to a = top - n;

                        for (positive i = 0; i < n; i++)
                        {
                                a[i] = limbs_value((shape >> 3) & 3);
                                r[i] = limbs_value((shape >> 5) ? 1 : 0);
                        }
                        r[-1] = 0xa5a5a5a5a5a5a5a5ull;
                        r[n] = 0x5a5a5a5a5a5a5a5aull;
                        memory_copy(want, r - 1, (n + 2) * sizeof(p64));
                        same("limbs_add_multiply_word", "high limb",
                             limbs_add_multiply_word(r, a, n, w),
                             reference_limbs_multiply(want + 1, a, n, w, 0, true));
                        same_bytes("limbs_add_multiply_word", "sum and guards",
                                   (b8 address_to)(r - 1), (b8 address_to)want,
                                   (n + 2) * sizeof(p64));
                        memory_copy(want, r - 1, (n + 2) * sizeof(p64));
                        same("limbs_multiply_word", "high limb",
                             limbs_multiply_word(r, a, n, w, carry),
                             reference_limbs_multiply(want + 1, a, n, w, carry, false));
                        same_bytes("limbs_multiply_word", "product and guards",
                                   (b8 address_to)(r - 1), (b8 address_to)want,
                                   (n + 2) * sizeof(p64));

                        // In place, a the page's last limbs.
                        memory_copy(operand, a, n * sizeof(p64));
                        same("limbs_multiply_word", "r == a",
                             limbs_multiply_word(a, a, n, w, carry),
                             reference_limbs_multiply(operand, operand, n, w, carry, false));
                        same_bytes("limbs_multiply_word", "r == a product", (b8 address_to)a,
                                   (b8 address_to)operand, n * sizeof(p64));
                }

        // Compare walks down, so both operands start at a page's first limb
        // with the protected page below.
        for (positive n = 0; n < 68; n++)
                for (positive differ = 0; differ <= n; differ++)
                        for (positive turn = 0; turn < 6; turn++)
                        {
                                p64 address_to a = low_page;
                                p64 address_to b = low_page + 256;

                                for (positive i = 0; i < n; i++)
                                        a[i] = b[i] = limbs_value(turn % 3);
                                if (differ < n)
                                {
                                        p64 delta = turn < 3 ? 1 : next() | 1;

                                        if (turn & 1)
                                                b[differ] += delta;
                                        else
                                                b[differ] -= delta;
                                }
                                same("limbs_compare", "lengths, places and directions",
                                     (positive)(bipolar)limbs_compare(a, b, n),
                                     (positive)(bipolar)reference_limbs_compare(a, b, n));
                                same("limbs_compare", "swapped",
                                     (positive)(bipolar)limbs_compare(b, a, n),
                                     (positive)(bipolar)reference_limbs_compare(b, a, n));
                        }

        // The divide at every divisor width and shift, the edges of the
        // corrections, and random pairs.
        for (b32 bits = 1; bits <= 64; bits++)
                for (positive turn = 0; turn < 200; turn++)
                {
                        p64 divisor = bits == 64 ? next() | (p64)1 << 63
                                                 : (next() & (((p64)1 << bits) - 1)) |
                                                       (p64)1 << (bits - 1);
                        p64 high = turn == 0 ? divisor - 1 : turn == 1 ? 0 : next() % divisor;
                        p64 low = turn < 3 ? ~(p64)0 : turn == 3 ? 0 : next();

                        limbs_divide_one(high, low, divisor);
                }
        limbs_divide_one(0, 0, 1);
        limbs_divide_one(0, ~(p64)0, 1);
        limbs_divide_one(~(p64)1, ~(p64)0, ~(p64)0);
        limbs_divide_one((p64)1 << 62, 0, (p64)1 << 63);
        limbs_divide_one(0x7fffffffffffffffull, ~(p64)0, 0x8000000000000000ull);
        limbs_divide_one(0xffffffff, 0xffffffffffffffffull, 0x100000000ull);
        limbs_divide_one(0x80000000, 0, 0x80000001ull);
        limbs_divide_one(0xfffffffe, 0xffffffff00000000ull, 0xffffffffull);
        for (positive turn = 0; turn < 200000; turn++)
        {
                p64 divisor = next() >> (next() & 63);

                if (!divisor)
                        continue;
                limbs_divide_one(next() % divisor, next(), divisor);
        }

        memory_free(pages, 4 * 4096);
}

fn check_limbs()
{
#if X64
        //      Once with the mulx body where the machine has it, asked from
        //      scratch, and once with the byte set to absent.
        p8 had = cpu_has_adx;

        cpu_has_adx = 0;
        check_limbs_body();
        same("limbs_add_multiply_word", "the byte was asked", cpu_has_adx != 0, 1);
        cpu_has_adx = 1;
        check_limbs_body();
        cpu_has_adx = had;
#else
        check_limbs_body();
#endif
}

fn check_copy_match()
{
        static positive offsets[] = {1, 2, 3, 4, 5, 6, 7, 8, 15, 16, 31, 32, 64};

        for (positive e = 0; e < EDGE_COUNT; e++)
                for (positive o = 0; o < sizeof(offsets) / sizeof(offsets[0]); o++)
                {
                        positive size = edges[e];
                        positive gap = offsets[o];

                        if (size + gap + 128 > ROOM)
                                continue;

                        memory_copy_apart(mine, pattern, ROOM);
                        memory_copy_apart(theirs, pattern, ROOM);

                        memory_copy_match(mine + 64, gap, size);
                        reference_copy_match(theirs + 64, gap, size);
                        same_bytes("memory_copy_match", "in-buffer LZ", mine,
                                   theirs, ROOM);
                }
}

// Both directions, and the case where they do not overlap at all: a memmove
// that only handles one of them passes half of this.
fn check_move()
{
        static positive offsets[] = {1, 3, 8, 15, 16, 64, 100};

        for (positive e = 0; e < EDGE_COUNT; e++)
                for (positive o = 0; o < sizeof(offsets) / sizeof(offsets[0]); o++)
                {
                        positive size = edges[e];
                        positive gap = offsets[o];

                        if (size + gap + 128 > ROOM)
                                continue;

                        memory_copy_apart(mine, pattern, ROOM);
                        memory_copy_apart(theirs, pattern, ROOM);

                        memory_copy(mine + 64 + gap, mine + 64, size);
                        reference_copy(theirs + 64 + gap, theirs + 64, size);
                        same_bytes("memory_copy", "forwards overlap", mine, theirs, ROOM);

                        memory_copy_apart(mine, pattern, ROOM);
                        memory_copy_apart(theirs, pattern, ROOM);

                        memory_copy(mine + 64, mine + 64 + gap, size);
                        reference_copy(theirs + 64, theirs + 64 + gap, size);
                        same_bytes("memory_copy", "backwards overlap", mine, theirs, ROOM);
                }
}

/*
        Exact bytes followed by a terminator through the non-overlap core.

        Source and destination are separate arrays by contract. Every source
        and destination alignment is crossed with every copy-size edge, and
        poison across the whole destination proves that only dst[0..size]
        moved. Embedded zeroes prove this is a memory primitive, while the
        returned address proves the adapter preserved dst+n across its call.
*/
fn check_copy_fast_end()
{
        for (positive i = 0; i < ROOM; i++)
                pattern[i] = (i % 11) ? (b8)(i * 37 + 5) : 0;

        for (positive e = 0; e < EDGE_COUNT; e++)
        {
                positive size = edges[e];

                for (positive destination_offset = 0; destination_offset < 16;
                     destination_offset++)
                        for (positive source_offset = 0; source_offset < 16;
                             source_offset++)
                        {
                                positive destination = 64 + destination_offset;
                                p8 address_to ended;

                                if (destination + size + 2 >= ROOM ||
                                    source_offset + size >= ROOM)
                                        continue;

                                reference_fill(mine, 0xA5, ROOM);
                                reference_fill(theirs, 0xA5, ROOM);

                                ended = memory_copy_apart_end(
                                    (p8 address_to)mine + destination,
                                    pattern + source_offset, size);
                                reference_copy_end(
                                    (p8 address_to)theirs + destination,
                                    pattern + source_offset, size);

                                same("memory_copy_apart_end", "returned unaligned end",
                                     (positive)(ended - (p8 address_to)mine),
                                     destination + size);
                                same_bytes("memory_copy_apart_end", "guarded exact copy",
                                           mine, theirs, ROOM);
                        }
        }

        // A zero-byte copy writes only the terminator and never reads source.
        reference_fill(mine, 0xA5, ROOM);
        reference_fill(theirs, 0xA5, ROOM);
        same("memory_copy_apart_end", "zero length end",
             (positive)(memory_copy_apart_end((p8 address_to)mine + 31, null, 0) -
                        (p8 address_to)mine),
             31);
        theirs[31] = 0;
        same_bytes("memory_copy_apart_end", "zero length guard", mine, theirs, ROOM);
}

/*
        Exact bytes followed by a terminator, through the overlap-aware core.

        The source deliberately contains embedded zeroes: this is a memory
        primitive and string_copy_max_end would stop early. Poison around every
        destination catches a write on either side of dst[0..size], while the
        returned address says the wrapper kept the original end across the
        shared copy core.
*/
fn check_copy_end()
{
        static b32 gaps[] = {-65, -32, -9, -3, -1, 1, 3, 9, 32, 65};

        for (positive i = 0; i < ROOM; i++)
                pattern[i] = (i % 11) ? (b8)(i * 37 + 5) : 0;

        // Disjoint source and destination, at every offset into a machine word.
        for (positive e = 0; e < EDGE_COUNT; e++)
        {
                positive size = edges[e];

                for (positive destination_offset = 0; destination_offset < 8;
                     destination_offset++)
                        for (positive source_offset = 0; source_offset < 8;
                             source_offset++)
                        {
                                positive destination = 64 + destination_offset;
                                p8 address_to ended;

                                if (destination + size + 2 >= ROOM ||
                                    source_offset + size >= ROOM)
                                        continue;

                                reference_fill(mine, 0xA5, ROOM);
                                reference_fill(theirs, 0xA5, ROOM);

                                ended = memory_copy_end(
                                    (p8 address_to)mine + destination,
                                    pattern + source_offset, size);
                                reference_copy_end(
                                    (p8 address_to)theirs + destination,
                                    pattern + source_offset, size);

                                same("memory_copy_end", "returned aligned end",
                                     (positive)(ended - (p8 address_to)mine),
                                     destination + size);
                                same_bytes("memory_copy_end", "guarded exact copy",
                                           mine, theirs, ROOM);
                        }
        }

        // Both overlap directions and distances inside and outside the core's
        // head/tail chunks, again at every word alignment.
        for (positive e = 0; e < EDGE_COUNT; e++)
                for (positive alignment = 0; alignment < 8; alignment++)
                        for (positive g = 0; g < sizeof(gaps) / sizeof(gaps[0]); g++)
                        {
                                positive size = edges[e];
                                b32 source = 1024 + (b32)alignment;
                                b32 destination = source + gaps[g];
                                p8 address_to ended;

                                if (destination < 0 ||
                                    (positive)destination + size + 2 >= ROOM ||
                                    (positive)source + size >= ROOM)
                                        continue;

                                reference_copy(mine, pattern, ROOM);
                                reference_copy(theirs, pattern, ROOM);

                                ended = memory_copy_end(
                                    (p8 address_to)mine + destination,
                                    mine + source, size);
                                reference_copy_end(
                                    (p8 address_to)theirs + destination,
                                    theirs + source, size);

                                same("memory_copy_end", "returned overlap end",
                                     (positive)(ended - (p8 address_to)mine),
                                     (positive)destination + size);
                                same_bytes("memory_copy_end", "overlap and guards",
                                           mine, theirs, ROOM);
                        }

        // A zero-byte copy still writes its terminator and never needs a source.
        reference_fill(mine, 0xA5, ROOM);
        reference_fill(theirs, 0xA5, ROOM);
        same("memory_copy_end", "zero length end",
             (positive)(memory_copy_end((p8 address_to)mine + 31, null, 0) -
                        (p8 address_to)mine),
             31);
        theirs[31] = 0;
        same_bytes("memory_copy_end", "zero length guard", mine, theirs, ROOM);
}

fn check_strings()
{
        static p8 text[512];

        for (positive e = 0; e < EDGE_COUNT; e++)
        {
                positive size = edges[e];

                if (size >= sizeof(text))
                        continue;

                for (positive i = 0; i < size; i++)
                {
                        p8 c = (p8)(next() % 94 + 33);

                        text[i] = c;
                }

                text[size] = 0;

                same("string_length", "", string_length(text), reference_length(text));

                // Against itself, against a copy that differs at one place, and
                // against something shorter.
                same("string_compare", "equal",
                     (positive)string_compare(text, text),
                     (positive)reference_compare(text, text));

                for (positive at = 0; at < size && at < 8; at++)
                {
                        static p8 other[512];

                        memory_copy_apart(other, text, size + 1);
                        other[at] = (p8)(other[at] ^ 1);

                        same("string_compare", "one byte apart",
                             (positive)(string_compare(text, other) > 0),
                             (positive)(reference_compare(text, other) > 0));

                        same("string_compare", "sign",
                             (positive)(string_compare(text, other) < 0),
                             (positive)(reference_compare(text, other) < 0));
                }

                // Every character it does contain, one it does not, and the
                // terminator -- which strchr answers with the end, not null.
                for (positive i = 0; i < size && i < 16; i++)
                        same("string_first_of", "present",
                             (positive)string_first_of(text, text[i]),
                             (positive)reference_first_of(text, text[i]));

                same("string_first_of", "absent",
                     (positive)string_first_of(text, 1),
                     (positive)reference_first_of(text, 1));

                same("string_first_of", "terminator",
                     (positive)string_first_of(text, 0),
                     (positive)reference_first_of(text, 0));
        }
}

/*
        The routines converted in bulk. References first, then one check each.

        A routine that reports through a writer is captured into a buffer, so
        the comparison is over what it actually emitted rather than over a
        return value it does not have.
*/
static b8 caught[512];
static positive caught_length;
static positive caught_calls;

fn catch_writer(address_any data, positive length)
{
        b8 address_to from = (b8 address_to)data;

        caught_calls++;

        if (!length)
                while (from[length])
                        length++;

        for (positive i = 0; i < length && caught_length < sizeof(caught) - 1; i++)
                caught[caught_length++] = from[i];

        caught[caught_length] = 0;
}

fn catch_reset()
{
        caught_length = 0;
        caught_calls = 0;
        caught[0] = 0;
}

fn check_shell_set_cursor()
{
        catch_reset();
        shell_set_cursor(catch_writer, 0, 0);
        same("shell_set_cursor", "zero coordinates",
             (positive)string_compare(caught, (string_address)"\033[0;0H"), 0);
        same("shell_set_cursor", "one atomic writer call", caught_calls, 1);

        catch_reset();
        shell_set_cursor(catch_writer, 12, 34);
        same("shell_set_cursor", "coordinate order",
             (positive)string_compare(caught, (string_address)"\033[34;12H"), 0);
        same("shell_set_cursor", "ordinary one-call write", caught_calls, 1);

        catch_reset();
        shell_set_cursor(catch_writer, 18446744073709551615ull,
                         18446744073709551615ull);
        same("shell_set_cursor", "full-width coordinates",
             (positive)string_compare(
                     caught,
                     (string_address)"\033[18446744073709551615;"
                                     "18446744073709551615H"),
             0);
        same("shell_set_cursor", "full-width one-call write", caught_calls, 1);
}

/*
        The last of a byte, and nothing for the terminator.

        This used to answer the terminator's own address when asked for byte
        zero, which is what strrchr does and what this is not. The library
        says so where it defines the two: string_last_of answers nothing,
        string_last_of_or_end answers the terminator, and the libc strrchr is
        an alias onto the second because that is the one POSIX describes.
*/
string_address reference_string_last_of(string_address source, p8 character)
{
        string_address found = null;

        if (!character)
                return null;

        while (string_get(source))
        {
                if string_is (source, character)
                        found = source;

                source++;
        }

        return found;
}

positive reference_to_positive(string_address input)
{
        positive value = 0;

        while (string_get(input) >= '0' && string_get(input) <= '9')
                value = value * 10 + (positive)(string_get(input++) - '0');

        return value;
}

bipolar reference_to_bipolar(string_address input)
{
        b32 negative = string_is(input, '-');

        if (negative || string_is(input, '+'))
                input++;

        bipolar value = (bipolar)reference_to_positive(input);

        return negative ? -value : value;
}

fn check_find_overlaps();
fn check_search();
fn check_append();

string_address reference_find(string_address string, string_address input)
{
        string_address step = string;
        string_address step_input = input;

        while (string_get(step))
        {
                if (string_not(step, string_get(step_input)))
                {
                        step++;
                        continue;
                }

                string_address find = step;

                while
                        string_get(step_input)
                        {
                                if string_not (step, string_get(step_input))
                                        break;

                                step++;
                                step_input++;
                        }

                if string_is (step_input, end)
                        return find;

                /*
                        Back to one past where this candidate began, not to
                        where it stopped.

                        This used to carry on from the mismatch, and the
                        assembly was written to match it -- "carry on from
                        here, as the C did" is still in the commit that
                        replaced it. Both were wrong the same way, so the
                        differential test below could not see it: "aab" is
                        not found in "aaab" if the candidate at zero eats two
                        bytes and the scan resumes past the one at one.
                */
                step = find + 1;
                step_input = input;
        }

        return null;
}

/*
        The edges the assembly reaches for and the block above does not.

        check_strings works on one static array, so every string in it starts
        at the same alignment and none of the three word-at-a-time routines
        ever has to mask off the bytes before the string, land its terminator
        on the first byte of a word, or match a character living in the same
        word as the terminator. Those are exactly the places a word loop is
        wrong, so they are walked here on purpose: every start offset zero
        through seven, every length either side of the word boundary, and the
        two pointers of a compare at different offsets from each other.
*/
static p8 room[256];
static p8 twin[256];

fn check_string_edges()
{
        for (positive offset = 0; offset < 8; offset++)
                for (positive size = 0; size <= 40; size++)
                {
                        // 64 is a multiple of eight, so the offset is the whole
                        // of the distance into the word.
                        string_address text = room + 64 + offset;
                        string_address other = twin + 64 + ((offset + 3) & 7);

                        reference_fill(room, 0xA5, sizeof(room));
                        reference_fill(twin, 0xA5, sizeof(twin));

                        for (positive i = 0; i < size; i++)
                                text[i] = (p8)(next() % 4 + 'a');

                        text[size] = 0;

                        // A length of zero puts the terminator at the offset,
                        // and a size that makes offset + size a multiple of
                        // eight puts it on the first byte of the next word --
                        // the two cases the mask has to get right.
                        same("string_length", "offset", string_length(text),
                             reference_length(text));

                        for (positive i = 0; i < size; i++)
                                same("string_first_of", "offset present",
                                     (positive)string_first_of(text, text[i]),
                                     (positive)reference_first_of(text, text[i]));

                        // Absent, and absent as 0xA5 in particular: the bytes
                        // before the string and after the terminator are that
                        // value, so a routine that masks its input rather than
                        // its result answers with one of them.
                        same("string_first_of", "offset absent",
                             (positive)string_first_of(text, 'q'),
                             (positive)reference_first_of(text, 'q'));

                        same("string_first_of", "offset poison",
                             (positive)string_first_of(text, 0xA5),
                             (positive)reference_first_of(text, 0xA5));

                        same("string_first_of", "offset terminator",
                             (positive)string_first_of(text, 0),
                             (positive)reference_first_of(text, 0));

                        // The character in the same word as the terminator,
                        // which is where a scan that stops at the terminator
                        // first gets the order wrong.
                        if (size)
                        {
                                p8 keep = text[size - 1];

                                text[size - 1] = '#';
                                same("string_first_of", "beside the terminator",
                                     (positive)string_first_of(text, '#'),
                                     (positive)reference_first_of(text, '#'));
                                text[size - 1] = keep;
                        }

                        memory_copy_apart(other, text, size + 1);

                        same("string_compare", "offset equal",
                             (positive)string_compare(text, other),
                             (positive)reference_compare(text, other));

                        for (positive at = 0; at < size; at++)
                        {
                                p8 keep = other[at];

                                other[at] = (p8)(keep ^ 1);
                                same("string_compare", "offset greater",
                                     (positive)(string_compare(text, other) > 0),
                                     (positive)(reference_compare(text, other) > 0));
                                same("string_compare", "offset less",
                                     (positive)(string_compare(text, other) < 0),
                                     (positive)(reference_compare(text, other) < 0));

                                // The same place, but ending the second string
                                // there rather than changing it: one string a
                                // prefix of the other is the case a word
                                // compare answers by looking for the
                                // terminator inside an equal word.
                                other[at] = 0;
                                same("string_compare", "offset prefix",
                                     (positive)(string_compare(text, other) > 0),
                                     (positive)(reference_compare(text, other) > 0));
                                same("string_compare", "offset prefix reversed",
                                     (positive)(string_compare(other, text) < 0),
                                     (positive)(reference_compare(other, text) < 0));

                                other[at] = keep;
                        }
                }
}

fn check_bulk_strings()
{
        static p8 subject[512];
        static p8 spare[512];

        for (positive e = 0; e < EDGE_COUNT; e++)
        {
                positive size = edges[e];

                if (size >= sizeof(subject) - 2)
                        continue;

                for (positive i = 0; i < size; i++)
                        subject[i] = (p8)(next() % 4 + 'a');

                subject[size] = 0;

                // string_copy and string_copy_max, into a poisoned buffer so a
                // byte written past the terminator is caught.
                reference_fill(spare, 0xA5, sizeof(spare));
                string_copy(spare, subject);
                same("string_copy", "length", string_length(spare), size);
                same("string_copy", "content",
                     (positive)string_compare(spare, subject), 0);

                for (positive limit = 0; limit <= size + 2 && limit < 64; limit++)
                {
                        reference_fill(spare, 0xA5, sizeof(spare));
                        string_copy_max(spare, subject, limit);

                        // Nothing at all for a limit of zero, and never a byte
                        // past the limit.
                        for (positive i = limit; i < sizeof(spare); i++)
                                if (spare[i] != 0xA5)
                                {
                                        report("string_copy_max", "wrote past the limit",
                                               i, limit);
                                        break;
                                }

                        checks++;
                }

                same("string_last_of", "found",
                     (positive)string_last_of(subject, 'a'),
                     (positive)reference_string_last_of(subject, 'a'));
                same("string_last_of", "absent",
                     (positive)string_last_of(subject, 'z'),
                     (positive)reference_string_last_of(subject, 'z'));

                // string_cut and string_replace_all change the string, so each
                // works on its own copy of it.
                string_copy(spare, subject);
                string_address after = string_cut(spare, 'b');
                same("string_cut", "terminated before the cut",
                     (positive)(after == null || string_first_of(spare, 'b') == null), 1);

                string_copy(spare, subject);
                string_replace_all(spare, 'a', 'z');
                same("string_replace_all", "none left",
                     (positive)(string_first_of(spare, 'a') == null), 1);
                same("string_replace_all", "same length", string_length(spare), size);

                // Against the C it replaced, not against an assumption: an
                // empty haystack answers null, which is not what "find
                // yourself in yourself" would suggest.
                same("string_find", "itself",
                     (positive)string_find(subject, subject),
                     (positive)reference_find(subject, subject));

                if (size > 2)
                {
                        same("string_find", "a tail of itself",
                             (positive)string_find(subject, subject + size - 2),
                             (positive)reference_find(subject, subject + size - 2));

                        same("string_find", "absent",
                             (positive)string_find(subject, (string_address)"qqq"),
                             (positive)reference_find(subject, (string_address)"qqq"));
                }
        }

        check_find_overlaps();
        check_search();
        check_append();
}

/*
        A candidate that fails, with the real match inside the part it ate.

        Everything above hunts for a needle that either is not there or
        begins where the scan first looks. Neither asks the question this
        gets wrong: after matching some of the needle and then failing, where
        does the next attempt start? One byte past where the candidate began,
        or where it gave up? The two differ only when a later start lies
        inside the run that matched, so the strings here are built to put one
        there.

        Left as literals rather than generated. Each one is a shape that
        broke something -- a repeated prefix, a needle that is its own tail,
        a run one byte longer than the needle -- and a generator that made
        them by accident would not say which was which.
*/
fn check_find_overlaps()
{
        static struct
        {
                string_address haystack;
                string_address needle;
        } cases[] = {
            {(string_address) "aaab", (string_address) "aab"},
            {(string_address) "aaaab", (string_address) "aab"},
            {(string_address) "aaaaaaaaab", (string_address) "aab"},
            {(string_address) "abababc", (string_address) "ababc"},
            {(string_address) "aabaabaaab", (string_address) "aabaaab"},
            {(string_address) "xxxxxy", (string_address) "xxy"},
            {(string_address) "banana", (string_address) "nana"},
            {(string_address) "aaa", (string_address) "aa"},
            {(string_address) "mississippi", (string_address) "issip"},
            {(string_address) "aabaacaad", (string_address) "aad"},
            // and the same shapes at every offset into a word, because the
            // scan aligns down and the first eight bytes are a special case
            {(string_address) " aaab", (string_address) "aab"},
            {(string_address) "  aaab", (string_address) "aab"},
            {(string_address) "   aaab", (string_address) "aab"},
            {(string_address) "    aaab", (string_address) "aab"},
            {(string_address) "     aaab", (string_address) "aab"},
            {(string_address) "      aaab", (string_address) "aab"},
            {(string_address) "       aaab", (string_address) "aab"},
        };

        for (positive i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        {
                string_address hay = cases[i].haystack;
                string_address needle = cases[i].needle;

                same("string_find", "a match inside a failed candidate",
                     (positive)string_find(hay, needle),
                     (positive)reference_find(hay, needle));
        }
}

/*
        string_search, which is strstr, and string_find, which is not.

        The two differ on an empty needle and only there: strstr answers the
        front of the haystack and string_find answers nothing. That is one
        byte of behaviour, which is exactly the size of thing that ships
        working on the machine it was written on and broken on the other two.
        The arm64 and riscv bodies had never been run when this was added.

        The long cases walk the needle through a haystack that is otherwise
        one repeated byte, at every offset into a word, because the front of
        the haystack is answered by a byte loop and everything past it by a
        body that aligns down and hunts the rarest byte.
*/
string_address reference_search(string_address hay, string_address needle)
{
        if (!string_get(needle))
                return hay;

        return reference_find(hay, needle);
}

fn check_search()
{
        static struct
        {
                string_address haystack;
                string_address needle;
        } cases[] = {
            {(string_address) "abc", (string_address) ""},
            {(string_address) "", (string_address) ""},
            {(string_address) "", (string_address) "a"},
            {(string_address) "abc", (string_address) "a"},
            {(string_address) "abc", (string_address) "c"},
            {(string_address) "abc", (string_address) "z"},
            {(string_address) "abc", (string_address) "abc"},
            {(string_address) "abc", (string_address) "abcd"},
            {(string_address) "abcdef", (string_address) "abc"},
            {(string_address) "abcdef", (string_address) "def"},
            {(string_address) "abcdef", (string_address) "cde"},
            {(string_address) "aaab", (string_address) "aab"},
            {(string_address) "aaaa", (string_address) "aab"},
            {(string_address) "banana", (string_address) "nana"},
            {(string_address) "mississippi", (string_address) "issip"},
        };

        for (positive i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
                same("string_search", "against a byte at a time strstr",
                     (positive)string_search(cases[i].haystack, cases[i].needle),
                     (positive)reference_search(cases[i].haystack, cases[i].needle));

        static p8 room[600];

        for (positive pad = 0; pad < 9; pad++)
                for (positive at = 16; at < 560; at++)
                {
                        for (positive i = 0; i < sizeof(room); i++) room[i] = 'a';

                        room[sizeof(room) - 1] = 0;
                        room[at] = 'q';
                        room[at + 1] = 'r';
                        room[at + 2] = 's';

                        string_address hay = (string_address)(room + pad);

                        same("string_search", "a needle far into a long haystack",
                             (positive)string_search(hay, (string_address) "qrs"),
                             (positive)reference_search(hay, (string_address) "qrs"));
                }
}

/*
        string_append, which is strcat: the source onto the end of the
        destination, and the destination handed back.

        Checked at every length either side can be, and with the join landing
        at every offset into a word, because it is two routines joined at a
        pointer one of them worked out. Guard bytes after the room, because
        strcat's whole character is that nothing bounds it and the way to get
        it wrong is to write one byte too many.
*/
fn check_append()
{
        static p8 room[160];
        static p8 want[160];

        for (positive first = 0; first < 40; first++)
                for (positive second = 0; second < 40; second++)
                {
                        for (positive i = 0; i < sizeof(room); i++)
                                room[i] = want[i] = 0xc7;

                        static p8 tail[48];

                        for (positive i = 0; i < first; i++)
                                room[i] = want[i] = (p8)('a' + i % 23);

                        room[first] = want[first] = 0;

                        for (positive i = 0; i < second; i++)
                                tail[i] = (p8)('A' + i % 19);

                        tail[second] = 0;

                        //      what it should look like afterwards
                        for (positive i = 0; i <= second; i++)
                                want[first + i] = tail[i];

                        string_address back =
                            string_append((string_address)room, (string_address)tail);

                        same("string_append", "hands the destination back",
                             (positive)back, (positive)room);

                        for (positive i = 0; i < sizeof(room); i++)
                                same("string_append", "byte for byte, guards included",
                                     (positive)room[i], (positive)want[i]);
                }
}

fn check_bulk_numbers()
{
        static positive samples[] = {0, 1, 7, 9, 10, 99, 100, 255, 999, 1000,
                                     65535, 1000000, 4294967295u};

        for (positive i = 0; i < sizeof(samples) / sizeof(samples[0]); i++)
        {
                positive n = samples[i];

                catch_reset();
                positive_to_string(catch_writer, n);
                same("positive_to_string", "round trip",
                     reference_to_positive(caught), n);

                catch_reset();
                bipolar_to_string(catch_writer, (bipolar)n);
                same("bipolar_to_string", "round trip",
                     (positive)reference_to_bipolar(caught), n);

                catch_reset();
                bipolar_to_string(catch_writer, -(bipolar)n);
                same("bipolar_to_string", "negative round trip",
                     (positive)(-reference_to_bipolar(caught)), n);

                catch_reset();
                positive_to_string(catch_writer, n);
                same("string_to_positive", "agrees",
                     string_to_positive(caught), reference_to_positive(caught));

                catch_reset();
                bipolar_to_string(catch_writer, -(bipolar)n);
                same("string_to_bipolar", "agrees",
                     (positive)string_to_bipolar(caught),
                     (positive)reference_to_bipolar(caught));
        }
}

/*
        file_load, which until now could not succeed.
/*
        Where this may write, which is not always /tmp.

        The file cases used to name /tmp directly. On a machine where /tmp is
        a small tmpfs, or one with a quota on it, or one where something else
        has filled it, those writes fail and the cases report as ordinary
        disagreements -- seven of them, with nothing anywhere saying the disk
        was the reason. That is the worst shape a test failure can take: it
        looks exactly like the thing it is supposed to be looking for.

        TMPDIR if it is set, /tmp if it is not, composed once here so the
        harness around this can point it somewhere with room.
*/
static p8 scratch_root[512];

static string_address scratch_path(string_address name)
{
        static p8 built[768];
        string_address address_to environment = program_environment_list();
        string_address root = environment
                                  ? string_get_environment(environment,
                                                           (string_address) "TMPDIR")
                                  : null;

        if (!root || !root[0])
                root = (string_address) "/tmp";

        positive at = 0;

        while (root[at] && at < sizeof(built) - 64)
        {
                built[at] = root[at];
                at++;
        }

        while (at && built[at - 1] == '/')
                at--;

        built[at++] = '/';

        for (positive i = 0; name[i] && at < sizeof(built) - 1; i++)
                built[at++] = name[i];

        built[at] = 0;
        (void)scratch_root;
        return (string_address)built;
}

/*
        file_load, which until now could not succeed.

        Its mmap flags were open() bits, so the kernel refused every mapping
        and the success path had never once run. This writes a file, loads it,
        and reads the bytes back through the mapping -- so the fix is held
        rather than merely applied.
*/
fn check_file_load()
{
        static p8 body[3000];
        static file subject;
        b8 address_to loaded;

        same("file", "mapping extent stays after established fields",
             __builtin_offsetof(file, mapped_size), 184);
        same("file", "mapping extent completes the object", sizeof(file), 192);

        for (positive i = 0; i < sizeof(body); i++)
                body[i] = (b8)(next() & 0xff);


        system_call_3(syscall(unlinkat), AT_FDCWD,
                      (positive)scratch_path((string_address)"moonwater_verify_load"), 0);

        b32 made = system_call_4(syscall(openat), AT_FDCWD,
                                 (positive)scratch_path((string_address)"moonwater_verify_load"),
                                 FILE_CREATE | FILE_WRITE | FILE_TRUNCATE, 0644);

        checks++;

        if (made < 0)
        {
                report("file_load", "could not make a file", (positive)made, 0);
                return;
        }

        system_call_3(syscall(write), made, (positive)body, sizeof(body));
        system_call_1(syscall(close), made);

        file_new(address_of subject, scratch_path((string_address)"moonwater_verify_load"),
                 FILE_READ);

        same("file_load", "opened", (positive)file_valid(address_of subject), 1);
        same("file_load", "size", subject.status.size, sizeof(body));

        loaded = (b8 address_to)file_load(address_of subject);

        checks++;

        if (!loaded)
        {
                report("file_load", "returned null", 0, 1);
                file_close(address_of subject);
                return;
        }

        same_bytes("file_load", "contents", loaded, body, sizeof(body));
        same("file_load", "already loaded returns the same",
             (positive)file_load(address_of subject), (positive)loaded);
        same("file_load", "records its exact page extent",
             subject.mapped_size, 4096);

        /*
                A read-only descriptor makes write return raw -EBADF. At offset
                nine, treating that negative result as an unsigned byte count
                wraps the computed end to zero and used to enter memory_copy
                with an enormous length.
        */
        p8 refused = 0x5a;
        positive put = file_write(address_of subject, address_of refused, 1, 9);
        same("file_write", "preserves a negative raw result",
             (positive)(bipolar)put, (positive)(bipolar)-9);
        same("file_write", "failed write keeps mapping loaded",
             (positive)subject.loaded, 1);
        same("file_write", "failed write keeps mapping extent",
             subject.mapped_size, 4096);

        file_close(address_of subject);
        same("file_close", "clears mapping pointer", (positive)subject.data, 0);
        same("file_close", "clears mapping extent", subject.mapped_size, 0);
        same("file_close", "clears loaded state", (positive)subject.loaded, 0);
        same("file_close", "clears status", (positive)subject.status.size, 0);

        /* A short file after fstat must release the exact mapping it made. */
        file_new(address_of subject,
                 scratch_path((string_address)"moonwater_verify_load"),
                 FILE_READ_WRITE);
        same("file_load", "short-read fixture opened",
             (positive)file_valid(address_of subject), 1);
        same("file_load", "short-read fixture truncated",
             (positive)system_call_2(syscall(ftruncate), subject.handle, 1000),
             0);
        same("file_load", "short read is not a complete load",
             (positive)file_load(address_of subject), 0);
        same("file_load", "short read releases mapping state",
             (positive)subject.data | subject.mapped_size |
                 (positive)subject.loaded,
             0);
        file_close(address_of subject);

        system_call_3(syscall(unlinkat), AT_FDCWD,
                      (positive)scratch_path((string_address)"moonwater_verify_load"), 0);
}

/*
        The routines merged from the conversion lanes.

        Each was verified by whoever wrote it, in a tree of their own. These
        are the same routines checked here, in one harness, so the claim does
        not rest on seven separate reports.
*/
#define SCRATCH scratch_path((string_address)"moonwater_verify_scratch")

fn check_file_round_trip()
{
        static p8 written[4096];
        static p8 read_back[4096];
        static file subject;

        for (positive i = 0; i < sizeof(written); i++)
                written[i] = (b8)(next() & 0xff);

        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)SCRATCH, 0);

        file_new(address_of subject, (string_address)SCRATCH,
                 FILE_READ_WRITE | FILE_CREATE | FILE_TRUNCATE);

        same("file_new", "valid", (positive)file_valid(address_of subject), 1);

        // Written at an offset, so the seek is exercised rather than assumed.
        positive put = file_write(address_of subject, written, sizeof(written), 0);

        same("file_write", "wrote it all", put, sizeof(written));

        file_get_status(address_of subject);
        same("file_get_status", "size follows the write",
             subject.status.size, sizeof(written));

        reference_fill(read_back, 0, sizeof(read_back));
        positive got = file_read(address_of subject, read_back, sizeof(read_back), 0);

        same("file_read", "read it all", got, sizeof(written));
        same_bytes("file_read", "contents", read_back, written, sizeof(written));

        // A partial read from the middle: the clamp is the only arithmetic in
        // the whole family.
        reference_fill(read_back, 0, sizeof(read_back));
        got = file_read(address_of subject, read_back, 100, sizeof(written) - 40);
        same("file_read", "clamped at the end", got, 40);

        got = file_read(address_of subject, read_back, 100, sizeof(written) + 500);
        same("file_read", "past the end reads nothing", got, 0);

        same("file_load", "maps the round-trip file",
             (positive)(file_load(address_of subject) != null), 1);
        same("file_load", "round-trip extent", subject.mapped_size, 4096);

        /* Extending a mapped file invalidates the old snapshot before stat. */
        p8 tail = 0xa5;
        put = file_write(address_of subject, address_of tail, 1, sizeof(written));
        same("file_write", "extends mapped file", put, 1);
        same("file_write", "extension unloads old mapping",
             (positive)subject.loaded, 0);
        same("file_write", "extension clears old extent", subject.mapped_size, 0);
        same("file_write", "status follows extension",
             (positive)subject.status.size, sizeof(written) + 1);

        file_close(address_of subject);
        same("file_close", "no longer valid",
             (positive)file_valid(address_of subject), 0);
        same("file_close", "clears path", (positive)subject.path, 0);
        same("file_close", "clears flags", subject.flags, 0);

        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)SCRATCH, 0);
}

fn check_lazy_file_and_library()
{
        static file subject;
        string_address lazy_path =
                scratch_path((string_address)"moonwater_verify_lazy");

        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)lazy_path, 0);
        b32 made = system_call_4(syscall(openat), AT_FDCWD,
                                 (positive)lazy_path,
                                 FILE_CREATE | FILE_WRITE | FILE_TRUNCATE,
                                 0644);
        checks++;
        if (made < 0)
        {
                report("file_new_lazy", "could not make fixture",
                       (positive)made, 0);
                return;
        }
        system_call_1(syscall(close), made);

        reference_fill((p8 address_to)address_of subject, 0xa5, sizeof subject);
        file_new_lazy(address_of subject, lazy_path, FILE_READ);
        same("file_new_lazy", "opens existing file",
             (positive)((bipolar)subject.handle >= 0), 1);
        same("file_new_lazy", "keeps path", (positive)subject.path,
             (positive)lazy_path);
        same("file_new_lazy", "keeps flags", subject.flags, FILE_READ);
        same("file_new_lazy", "starts status clean",
             (positive)subject.status.size, 0);
        same("file_new_lazy", "starts mapping clean",
             (positive)subject.data | subject.mapped_size |
                 (positive)subject.loaded,
             0);
        file_close(address_of subject);
        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)lazy_path, 0);

        string_address missing =
                scratch_path((string_address)"moonwater_verify_lazy_missing");
        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)missing, 0);
        reference_fill((p8 address_to)address_of subject, 0xa5, sizeof subject);
        file_new_lazy(address_of subject, missing, FILE_READ);
        same("file_new_lazy", "preserves open errno",
             (positive)((bipolar)subject.handle < 0), 1);
        same("file_valid", "rejects every negative errno",
             (positive)file_valid(address_of subject), 0);
        same("file_new_lazy", "failed path", (positive)subject.path,
             (positive)missing);
        same("file_load", "rejects every negative open result",
             (positive)file_load(address_of subject), 0);
        same("file_read", "rejects every negative open result",
             file_read(address_of subject, address_of made, 1, 0),
             (positive)-1);
        file_close(address_of subject);
        same("file_close", "normalizes a negative open result",
             subject.handle, (positive)-1);
        same("file_close", "cleans a failed object",
             (positive)subject.path | subject.flags | (positive)subject.data |
                 subject.mapped_size | (positive)subject.loaded |
                 (positive)subject.status.size,
             0);

        // Non-Windows library loading is deliberately only a thin file-open
        // placeholder today.  Pin both sides of that small contract: a slash
        // leaves storage untouched, while a bare name opens and stats it.
        static string_address bare =
                (string_address)"moonwater_verify_library";
        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)bare, 0);
        made = system_call_4(syscall(openat), AT_FDCWD, (positive)bare,
                             FILE_CREATE | FILE_WRITE | FILE_TRUNCATE, 0644);
        checks++;
        if (made < 0)
        {
                report("library_open", "could not make fixture",
                       (positive)made, 0);
                return;
        }
        system_call_1(syscall(close), made);

        subject.handle = (positive)-1;
        subject.path = null;
        library_open(address_of subject, (string_address)"./moonwater_verify_library");
        same("library_open", "slash path is left alone", subject.handle,
             (positive)-1);
        same("library_open", "slash path does not store a path",
             (positive)subject.path, 0);

        library_open(address_of subject, bare);
        same("library_open", "opens a bare name",
             (positive)((bipolar)subject.handle >= 0), 1);
        same("library_open", "stores bare name", (positive)subject.path,
             (positive)bare);
        same("library_open", "uses executable-open flags", subject.flags,
             FILE_READ | FILE_EXECUTE);
        same("library_get", "unsupported lookup is null",
             (positive)library_get(address_of subject,
                                   (string_address)"symbol"), 0);
        library_close(address_of subject);
        file_close(address_of subject);
        system_call_3(syscall(unlinkat), AT_FDCWD, (positive)bare, 0);
}

/*
        Whether the mapping holding an address has asked for huge pages: "hg"
        in the VmFlags line smaps gives it. 0 for no, 1 for yes, 2 where the
        file is not there to read or the address is in no mapping it lists.
*/
static positive memory_smaps_huge(positive address)
{
        static char text[1 << 18];
        bipolar handle = system_open_at(AT_FDCWD, "/proc/self/smaps", FILE_READ);
        positive used = 0;

        if (handle < 0)
                return 2;
        for (;;)
        {
                bipolar got = system_read_retry((positive)handle, text + used, sizeof(text) - 1 - used);

                if (got <= 0)
                        break;
                used += (positive)got;
        }
        system_close((positive)handle);
        text[used] = 0;

        for (char *line = text; *line;)
        {
                char *stop = line;
                positive low = 0, high = 0, digits = 0;

                while (*stop && *stop != '\n')
                        stop++;
                for (char *at = line; at < stop && ((*at >= '0' && *at <= '9') || (*at >= 'a' && *at <= 'f')); at++, digits++)
                        low = low * 16 + (positive)(*at <= '9' ? *at - '0' : *at - 'a' + 10);
                if (digits && line[digits] == '-')
                {
                        char *at = line + digits + 1;

                        for (; *at != ' ' && at < stop; at++)
                                high = high * 16 + (positive)(*at <= '9' ? *at - '0' : *at - 'a' + 10);
                        if (address >= low && address < high)
                        {
                                for (line = stop + 1; *line && !(line[0] == 'V' && line[1] == 'm' && line[2] == 'F'); )
                                {
                                        while (*line && *line != '\n')
                                                line++;
                                        line += *line == '\n';
                                }
                                for (char *flag = line; *flag && *flag != '\n'; flag++)
                                        if (flag[0] == ' ' && flag[1] == 'h' && flag[2] == 'g' && (flag[3] == ' ' || flag[3] == '\n'))
                                                return 1;
                                return 0;
                        }
                }
                line = *stop ? stop + 1 : stop;
        }

        return 2;
}

/*
        memory_smaps_huge of an address, where smaps can say: 2 when it
        cannot, which a page of the check's own says. The page is asked for
        huge pages by the syscall itself and read back while it is mapped; a
        kernel that does not take the advice (an error) or a smaps that does
        not show it (qemu-user writes its own, whose VmFlags never carry hg)
        cannot tell memory() asking from memory() not asking.
*/
static positive memory_asked_huge(positive address)
{
        positive probe = (positive)memory(4096);

        if (!probe || system_failed(probe))
                return 2;

        bipolar taken = (bipolar)system_call_3(syscall(madvise), probe, 4096, MEMORY_ADVISE_HUGE_PAGE);
        positive shown = taken < 0 ? 2 : memory_smaps_huge(probe);

        memory_free((address_any)probe, 4096);
        return shown == 1 ? memory_smaps_huge(address) : 2;
}

/*
        A mapping of eight megabytes or more is an arena a tool fills, and
        memory() asks for huge pages for it where the kernel is built to give
        them to whoever asks; a smaller one does not, and a store that
        memory_reserve grows to eight megabytes does, whether the mapping it
        had was one or not. The grown store is the first growth to 16 MiB,
        which is mremap's.
*/
fn check_memory_huge()
{
        positive small = (positive)memory(MEMORY_HUGE_MIN - 4096);
        positive large = (positive)memory(MEMORY_HUGE_MIN);
        positive big = (positive)memory(3 * MEMORY_HUGE_MIN + 4096);

        if (memory_asked_huge(large) == 2)
        {
                memory_free((address_any)small, MEMORY_HUGE_MIN - 4096);
                memory_free((address_any)large, MEMORY_HUGE_MIN);
                memory_free((address_any)big, 3 * MEMORY_HUGE_MIN + 4096);
                string_format(log, "  NOT RUN memory: huge page advice cannot be seen here (a page madvised for huge "
                                   "pages shows no hg in smaps: qemu-user's own smaps, or a kernel without the advice), "
                                   "so the eight megabyte rows of memory and memory_reserve are not run\n");
                return;
        }

        same("memory", "under eight megabytes does not ask", memory_asked_huge(small), 0);
        same("memory", "eight megabytes asks", memory_asked_huge(large), 1);
        same("memory", "more than eight megabytes asks", memory_asked_huge(big), 1);
        memory_free((address_any)small, MEMORY_HUGE_MIN - 4096);
        memory_free((address_any)large, MEMORY_HUGE_MIN);
        memory_free((address_any)big, 3 * MEMORY_HUGE_MIN + 4096);

        p8 address_to bytes = null;
        positive room = 0;
        positive used = 0;

        same("memory_reserve", "a first store of a megabyte", memory_reserve((address_any address_to)address_of bytes, address_of room, 0, 1 << 20, 1, 4096), 1);
        same("memory_reserve", "under eight megabytes does not ask", memory_asked_huge((positive)bytes), 0);
        bytes[0] = 1;
        same("memory_reserve", "grown past eight megabytes", memory_reserve((address_any address_to)address_of bytes, address_of room, 1, 2 * MEMORY_HUGE_MIN, 1, 4096), 1);
        same("memory_reserve", "keeps what it held", bytes[0], 1);
        same("memory_reserve", "past eight megabytes asks", memory_asked_huge((positive)bytes), 1);
        memory_release((address_any address_to)address_of bytes, address_of room, address_of used, 1);
}

fn check_memory()
{
        static positive sizes[] = {1, 8, 100, 4095, 4096, 4097, 65536};

        for (positive i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
        {
                positive size = sizes[i];
                b8 address_to block = (b8 address_to)memory(size);

                checks++;

                if (!block)
                {
                        report("memory", "returned null", size, 0);
                        continue;
                }

                // Writable over its whole extent, and it must be zero: an
                // anonymous mapping is, and anything relying on that would
                // otherwise fail somewhere far away.
                for (positive j = 0; j < size; j++)
                        if (block[j])
                        {
                                report("memory", "not zeroed", j, 0);
                                break;
                        }

                checks++;

                for (positive j = 0; j < size; j++)
                        block[j] = (b8)(j & 0xff);

                for (positive j = 0; j < size; j++)
                        if (block[j] != (b8)(j & 0xff))
                        {
                                report("memory", "did not hold what was written", j, 0);
                                break;
                        }

                checks++;
                memory_free(block, size);
        }
}

fn check_directory()
{
        string_address here = working_directory_get();

        same("working_directory_get", "answered", (positive)(here != null), 1);

        if (!here)
                return;

        same("working_directory_set", "to root",
             (positive)(working_directory_set((string_address)"/"), 1), 1);

        string_address now = working_directory_get();

        same("working_directory_get", "follows the set",
             (positive)string_compare(now, (string_address)"/"), 0);

        working_directory_set(here);
}

fn check_clock()
{
        p64 first = get_cpu_time();
        positive spin = 0;

        // Something it cannot fold away, so the second read is genuinely later.
        for (positive i = 0; i < 200000; i++)
                spin += i ^ next();

        p64 second = get_cpu_time();

        same("get_cpu_time", "moves forward", (positive)(second >= first), 1);
        same("get_cpu_time", "not stuck at zero",
             (positive)(first != 0 || second != 0), 1);
        checks += (spin == 0);
}

static b32 report_nested_status;

static fn report_nested_writer(address_any data, positive length)
{
        catch_writer(data, length);
        if (caught_calls == 1)
                report_nested_status = string_report(
                    catch_writer, -9, "<%s:%p>", "inner", (positive)99);
}

fn check_report()
{
        static b32 results[] = {0, 1, 2, 125, -1, -2147483647 - 1, 2147483647};
        static string_address endings[] = {"", "plain", "%%", "before%", "%qafter"};
        b8 expected[sizeof caught];
        for (positive i = 0; i < array_count(results); i++)
        {
                b32 result = results[i];
                for (positive j = 0; j < array_count(endings); j++)
                {
                        catch_reset();
                        string_format(catch_writer, endings[j]);
                        positive length = caught_length, calls = caught_calls;
                        memory_copy(expected, caught, length + 1);
                        catch_reset();
                        same("string_report", "early-exit result",
                             (positive)(bipolar)string_report(
                                 catch_writer, result, endings[j]),
                             (positive)(bipolar)result);
                        same("string_report", "writer calls", caught_calls, calls);
                        same_bytes("string_report", "early-exit bytes",
                                   caught, expected, length + 1);
                }

                // Exhaust GP registers and interleave stack integers with more
                // than eight FP arguments. The extra fixed result argument
                // changes only the GP cursor, never the FP/overflow ordering.
#define REPORT_MIX "%p:%f|%p:%f|%p:%f|%p:%f|%p:%f|%p:%f|%p:%f|%p:%f|%p:%f|%s:%b"
#define REPORT_VALUES (positive)1, 1.25, (positive)2, 2.5, \
        (positive)3, 3.75, (positive)4, 4.0, (positive)5, 5.25, \
        (positive)6, 6.5, (positive)7, 7.75, (positive)8, 8.0, \
        (positive)9, 9.25, "last", -37
                catch_reset();
                string_format(catch_writer, REPORT_MIX, REPORT_VALUES);
                positive length = caught_length, calls = caught_calls;
                memory_copy(expected, caught, length + 1);
                catch_reset();
                same("string_report", "mixed result",
                     (positive)(bipolar)string_report(catch_writer, result,
                                                    REPORT_MIX, REPORT_VALUES),
                     (positive)(bipolar)result);
                same("string_report", "mixed calls", caught_calls, calls);
                same("string_report", "mixed length", caught_length, length);
                same_bytes("string_report", "mixed bytes", caught, expected, length + 1);
#undef REPORT_VALUES
#undef REPORT_MIX
                catch_reset();
                same("string_report", "integer spill result",
                     (positive)(bipolar)string_report(catch_writer, result,
                         "%p %p %p %p %p %p %p %p %p %s", (positive)1,
                         (positive)2, (positive)3, (positive)4, (positive)5,
                         (positive)6, (positive)7, (positive)8, (positive)9, "end"),
                     (positive)(bipolar)result);
                same("string_report", "integer spill bytes",
                     string_compare(caught, "1 2 3 4 5 6 7 8 9 end"), 0);
                catch_reset();
                same("string_report", "nested outer result",
                     (positive)(bipolar)string_report(report_nested_writer, result,
                                                    "A%s", "B"),
                     (positive)(bipolar)result);
                same("string_report", "nested inner result",
                     (positive)(bipolar)report_nested_status, (positive)(bipolar)-9);
                same("string_report", "nested bytes",
                     string_compare(caught, "A<inner:99>B"), 0);
        }
}

static string_address diagnostic_name = "early";
static positive diagnostic_before_calls;

static fn diagnostic_before()
{
        diagnostic_before_calls++;
        catch_writer("!", 1);
        diagnostic_name = "late";
}

fn check_diagnostic()
{
        const diagnostic sink = {catch_writer, diagnostic_before, &diagnostic_name};
        const diagnostic plain = {catch_writer, null, &diagnostic_name};
        static b32 results[] = {0, 1, -1, -2147483647 - 1, 2147483647};
        static string_address subjects[] = {null, "", "subject%"};
        b8 expected[256];
        for (positive i = 0; i < array_count(results); i++)
                for (positive j = 0; j < array_count(subjects); j++)
                {
                        catch_reset();
                        catch_writer("!", 1);
                        string_format(catch_writer,
                            subjects[j] ? "%s: %s: %s\n" : "%s: %s\n",
                            "late", subjects[j] ? subjects[j] : (string_address)"reason%", "reason%");
                        positive length = caught_length, calls = caught_calls;
                        memory_copy(expected, caught, length + 1);
                        catch_reset();
                        diagnostic_name = "early";
                        diagnostic_before_calls = 0;
                        same("string_diagnostic", "result survives before and writes",
                            (positive)(bipolar)string_diagnostic(&sink, results[i], subjects[j], "reason%"),
                            (positive)(bipolar)results[i]);
                        same("string_diagnostic", "one before call", diagnostic_before_calls, 1);
                        same("string_diagnostic", "writer call count", caught_calls, calls);
                        same("string_diagnostic", "output length", caught_length, length);
                        same_bytes("string_diagnostic", "prefix read after before",
                                   caught, expected, length + 1);
                        catch_reset();
                        same("string_diagnostic", "optional before",
                            (positive)(bipolar)string_diagnostic(&plain, results[i], subjects[j], "reason%"),
                            (positive)(bipolar)results[i]);
                        same("string_diagnostic", "no before call", diagnostic_before_calls, 1);
                        same_bytes("string_diagnostic", "plain output", caught, expected + 1, length);
                }

        b32 pipes[2];
        bipolar saved = system_call_1(syscall(dup), 2);
        bipolar opened = system_call_2(syscall(pipe2), (positive)pipes, 2048);
        same("stderr writers", "pipe setup", opened, 0);
        if (opened < 0 || saved < 0)
        {
                if (saved >= 0) system_call_1(syscall(close), saved);
                if (opened == 0)
                {
                        system_call_1(syscall(close), pipes[0]);
                        system_call_1(syscall(close), pipes[1]);
                }
                return;
        }
        bipolar rebound = system_call_3(syscall(dup3), pipes[1], 2, 0);
        same("stderr writers", "stderr binding", rebound, 2);
        if (rebound == 2)
        {
                positive buffered = log_writer_buffer_length;
                writer_stderr("alpha", 0);
                writer_stderr_once("x\0z", 3);
                writer_stderr("", 0);
                writer_stderr_once("", 0);
                same("stderr writers", "no implicit flush", log_writer_buffer_length, buffered);
        }
        system_call_3(syscall(dup3), saved, 2, 0);
        system_call_1(syscall(close), saved);
        system_call_1(syscall(close), pipes[1]);
        b8 bytes[16];
        bipolar read = system_call_3(syscall(read), pipes[0], (positive)bytes, sizeof bytes);
        same("stderr writers", "exact and terminated spans", read, 8);
        if (read == 8)
                same_bytes("stderr writers", "embedded NUL", bytes, "alphax\0z", 8);
        system_call_1(syscall(close), pipes[0]);

        /* A span the kernel refuses is remembered for the tool's status; a
           taken one and the raw once-writer leave the mark alone. */
        same("stderr writers", "no failure after taken spans", writer_stderr_failed, 0);
        bipolar full = system_call_4(syscall(openat), AT_FDCWD, (positive)"/dev/full",
                                     O_WRONLY, 0);
        saved = system_call_1(syscall(dup), 2);
        if (full >= 0 && saved >= 0 &&
            system_call_3(syscall(dup3), full, 2, 0) == 2)
        {
                writer_stderr_once("x", 1);
                same("stderr writers", "once leaves the mark", writer_stderr_failed, 0);
                writer_stderr("refused", 0);
                same("stderr writers", "refused span marks", writer_stderr_failed, 1);
                writer_stderr_failed = 0;
                /* log_error, the raw writer most diagnostics go through,
                   marks a refusal too: du's warning into /dev/full is 1. */
                log_error("refused", 0);
                same("stderr writers", "log_error marks a refusal", writer_stderr_failed, 1);
                writer_stderr_failed = 0;
        }
        if (saved >= 0)
        {
                system_call_3(syscall(dup3), saved, 2, 0);
                system_call_1(syscall(close), saved);
        }
        if (full >= 0)
                system_call_1(syscall(close), full);
}

/* Reenter the formatter while its outer variadic cursor is live. */
static fn format_render_nested(writer output, string_address value)
{
        string_format(output, "{%s}", value);
}

static fn format_render_binary(writer output, address_any value)
{
        output(value, 3);
}

static fn check_format_renderers()
{
#define FORMAT_RENDER_CASE(wanted, format, ...)                              \
        do {                                                                \
                catch_reset();                                              \
                string_format(catch_writer, format, __VA_ARGS__);           \
                same_bytes("format renderer", "bytes", caught, wanted,      \
                           sizeof(wanted));                                 \
                catch_reset();                                              \
                same("format renderer", "report result",                   \
                     string_report(catch_writer, 73, format, __VA_ARGS__), 73); \
                same_bytes("format renderer", "reported bytes",             \
                           caught, wanted, sizeof(wanted));                 \
        } while (0)
        /* Exercise both halves of the renderer/value pair in every integer
           register/overflow position on SysV, AAPCS64 and RV64. */
        FORMAT_RENDER_CASE("{x}", "%w", format_render_nested, "x");
        FORMAT_RENDER_CASE("0{x}", "%p%w", (positive)0, format_render_nested, "x");
        FORMAT_RENDER_CASE("01{x}", "%p%p%w", (positive)0, (positive)1,
                           format_render_nested, "x");
        FORMAT_RENDER_CASE("012{x}", "%p%p%p%w", (positive)0, (positive)1,
                           (positive)2, format_render_nested, "x");
        FORMAT_RENDER_CASE("0123{x}", "%p%p%p%p%w", (positive)0, (positive)1,
                           (positive)2, (positive)3, format_render_nested, "x");
        FORMAT_RENDER_CASE("01234{x}", "%p%p%p%p%p%w", (positive)0, (positive)1,
                           (positive)2, (positive)3, (positive)4,
                           format_render_nested, "x");
        FORMAT_RENDER_CASE("012345{x}", "%p%p%p%p%p%p%w", (positive)0,
                           (positive)1, (positive)2, (positive)3, (positive)4,
                           (positive)5, format_render_nested, "x");
        FORMAT_RENDER_CASE("{a}{b}{c}{d}{e}end", "%w%w%w%w%w%s",
                           format_render_nested, "a", format_render_nested, "b",
                           format_render_nested, "c", format_render_nested, "d",
                           format_render_nested, "e", "end");
        FORMAT_RENDER_CASE("{x}:-9:7", "%w:%b:%p", format_render_nested, "x",
                           (b32)-9, (positive)7);
        FORMAT_RENDER_CASE("[a\0b]", "[%w]", format_render_binary, "a\0b");
        FORMAT_RENDER_CASE("{}%", "%w%%", format_render_nested, "");
#undef FORMAT_RENDER_CASE
}

fn check_format()
{
        check_format_renderers();
        check_report();
        check_diagnostic();
        // string_format drives most of what anything here prints, so the check
        // is over what it emits rather than over a return value it has none of.
        catch_reset();
        string_format(catch_writer, (string_address) "plain");
        same("string_format", "literal", string_length(caught), 5);

        catch_reset();
        string_format(catch_writer, (string_address) "[%s]", "middle");
        same("string_format", "string",
             (positive)string_compare(caught, (string_address) "[middle]"), 0);

        catch_reset();
        string_format(catch_writer, (string_address) "%p", (positive)12345);
        same("string_format", "number", reference_to_positive(caught), 12345);

        catch_reset();
        string_format(catch_writer, (string_address) "%b", (bipolar)-99);
        same("string_format", "signed",
             (positive)(-reference_to_bipolar(caught)), 99);

        catch_reset();
        string_format(catch_writer, (string_address) "%s=%p;", "n", (positive)7);
        same("string_format", "several",
             (positive)string_compare(caught, (string_address) "n=7;"), 0);

        catch_reset();
        string_format(catch_writer, (string_address) "%s", "");
        same("string_format", "empty argument", string_length(caught), 0);
}

fn check_decimals()
{
        // fast_sin is an approximation, so the check is the shape of it: the
        // zeroes, the sign either side, and that it stays inside its range.
        same("fast_sin", "zero at zero", (positive)(fast_sin(0.0) == 0.0), 1);
        same("fast_sin", "positive on the first half",
             (positive)(fast_sin(1.5) > 0.0), 1);
        same("fast_sin", "negative on the second",
             (positive)(fast_sin(4.0) < 0.0), 1);

        for (positive i = 0; i < 64; i++)
        {
                decimal x = (decimal)(bipolar)(next() % 2000) / 100.0 - 10.0;
                decimal y = fast_sin(x);

                same("fast_sin", "inside its range",
                     (positive)(y >= -1.01 && y <= 1.01), 1);
        }

        catch_reset();
        decimal_to_string(catch_writer, 0.0);
        same("decimal_to_string", "zero", (positive)(string_length(caught) > 0), 1);

        catch_reset();
        decimal_to_string(catch_writer, 1.5);
        same("decimal_to_string", "has a point",
             (positive)(string_first_of(caught, '.') != null), 1);

        catch_reset();
        decimal_to_string(catch_writer, -2.25);
        same("decimal_to_string", "keeps the sign", (positive)(caught[0] == '-'), 1);
}

/*
        The set scan, against the loop it replaces.

        Every set is exercised, not just a convenient one: the empty set stops
        immediately, the full set runs to the end, and the interesting ones are
        the metacharacter sets a shell lexer actually uses.
*/
positive reference_span(string_address source, const b8 address_to set)
{
        positive n = 0;

        while (1)
        {
                p8 c = source[n];

                if (!set[c])
                        return n;

                n++;
        }
}

static string_address verify_byte_class_names[BYTE_CLASSES] = {
    "alpha", "digit", "alnum", "upper", "lower", "space",
    "blank", "print", "graph", "cntrl", "punct", "xdigit",
};

// The three C bodies replaced by the byte-helper assembly, kept here as the
// independent specification the architecture entries are checked against.
static b32 reference_byte_class_index(string_address name, positive length)
{
        for (b32 which = 0; which < BYTE_CLASSES; which++)
        {
                string_address want = verify_byte_class_names[which];
                positive at = 0;

                while (at < length && want[at] && name[at] == want[at])
                        at++;

                if (at == length && !want[at])
                        return which;
        }

        return -1;
}

static p8 reference_byte_class_holds(b32 which, p8 value)
{
        p8 upper = value >= 'A' && value <= 'Z';
        p8 lower = value >= 'a' && value <= 'z';
        p8 digit = value >= '0' && value <= '9';
        p8 printing = value >= ' ' && value < 127;

        switch (which)
        {
        case BYTE_ALPHA:
                return upper || lower;
        case BYTE_DIGIT:
                return digit;
        case BYTE_ALNUM:
                return upper || lower || digit;
        case BYTE_UPPER:
                return upper;
        case BYTE_LOWER:
                return lower;
        case BYTE_SPACE:
                return value == ' ' || (value >= '\t' && value <= '\r');
        case BYTE_BLANK:
                return value == ' ' || value == '\t';
        case BYTE_PRINT:
                return printing;
        case BYTE_GRAPH:
                return printing && value != ' ';
        case BYTE_CNTRL:
                return value < ' ' || value == 127;
        case BYTE_PUNCT:
                return printing && value != ' ' && !upper && !lower && !digit;
        case BYTE_XDIGIT:
                return digit || (value >= 'a' && value <= 'f') ||
                       (value >= 'A' && value <= 'F');
        }

        return false;
}

fn check_byte_helpers()
{
        static p8 name_room[64];
        static p8 members[256];
        static b8 set[STRING_SET_BYTES];
        static b8 untouched[STRING_SET_BYTES];
        static string_address unknown[] = {
            "",       "alph",  "alphax", "alpha!", "Digit", "xdigi",
            "xdigitx", "thing", "prints", "printable", null,
        };

        // Every known spelling, both as a normal string and as an exact slice
        // with hostile bytes immediately after it. Shifting the latter reaches
        // every alignment without ever giving the routine a terminator.
        for (b32 which = 0; which < BYTE_CLASSES; which++)
        {
                string_address name = verify_byte_class_names[which];
                positive length = reference_length(name);

                same("byte_class_index", "known name",
                     (positive)(bipolar)byte_class_index(name, length),
                     (positive)(bipolar)which);

                for (positive shift = 0; shift < 8; shift++)
                {
                        p8 address_to slice = name_room + 8 + shift;

                        reference_fill(name_room, 0xa5, sizeof(name_room));
                        for (positive at = 0; at < length; at++)
                                slice[at] = (p8)name[at];

                        same("byte_class_index", "bounded nonterminated name",
                             (positive)(bipolar)byte_class_index(
                                 (string_address)slice, length),
                             (positive)(bipolar)which);
                }

                same("byte_class_index", "known prefix is not the class",
                     (positive)(bipolar)byte_class_index(name, length - 1),
                     (positive)(bipolar)-1);

                reference_fill(name_room, '!', sizeof(name_room));
                for (positive at = 0; at < length; at++)
                        name_room[at] = (p8)name[at];

                same("byte_class_index", "known extension is not the class",
                     (positive)(bipolar)byte_class_index(
                         (string_address)name_room, length + 1),
                     (positive)(bipolar)-1);

                name_room[length - 1] ^= 1;
                same("byte_class_index", "one wrong byte",
                     (positive)(bipolar)byte_class_index(
                         (string_address)name_room, length),
                     (positive)(bipolar)-1);

                // All twelve classes over the complete byte domain, including
                // the high half where signed comparisons tend to go wrong.
                for (positive value = 0; value < 256; value++)
                        same("byte_class_holds", "all classes and bytes",
                             byte_class_holds(which, (p8)value),
                             reference_byte_class_holds(which, (p8)value));
        }

        for (positive at = 0; unknown[at]; at++)
        {
                positive length = reference_length(unknown[at]);

                same("byte_class_index", "unknown name",
                     (positive)(bipolar)byte_class_index(unknown[at], length),
                     (positive)(bipolar)reference_byte_class_index(
                         unknown[at], length));
        }

        same("byte_class_index", "empty slice needs no address",
             (positive)(bipolar)byte_class_index(null, 0),
             (positive)(bipolar)-1);

        for (positive value = 0; value < 256; value++)
        {
                same("byte_class_holds", "negative class",
                     byte_class_holds(-1, (p8)value), 0);
                same("byte_class_holds", "class past the table",
                     byte_class_holds(BYTE_CLASSES, (p8)value), 0);
        }

        // A terminated string can carry every nonzero byte exactly once. Zero
        // ends the member list and must remain absent from the set.
        for (positive value = 1; value < 256; value++)
                members[value - 1] = (p8)value;
        members[255] = 0;
        reference_fill(set, 0, sizeof(set));
        string_set_add(set, (string_address)members);

        for (positive value = 0; value < 256; value++)
                same("string_set_add", "every nonzero byte",
                     (positive)set[value], value != 0);

        reference_fill(set, 0x5a, sizeof(set));
        reference_copy(untouched, set, sizeof(set));
        string_set_add(set, (string_address)"");
        same_bytes("string_set_add", "empty member list changes nothing",
                   set, untouched, sizeof(set));
}

fn check_span()
{
        static p8 subject[600];
        static b8 set[STRING_SET_BYTES];
        static string_address sets[] = {
            "",                          // nothing is a member
            " \t",                       // blanks, as a lexer skips them
            "|&;<>()$`\\\\\"' \t\n",        // shell metacharacters
            "abcdefghijklmnopqrstuvwxyz",
            "0123456789",
        };

        for (positive which = 0; which < sizeof(sets) / sizeof(sets[0]); which++)
        {
                reference_fill(set, 0, sizeof(set));
                string_set_add(set, sets[which]);

                for (positive e = 0; e < EDGE_COUNT; e++)
                {
                        positive size = edges[e];

                        if (size >= sizeof(subject) - 1)
                                continue;

                        for (positive i = 0; i < size; i++)
                                subject[i] = (p8)(next() % 94 + 33);

                        subject[size] = 0;

                        same("string_span", "against the loop",
                             string_span(subject, set), reference_span(subject, set));

                        // And a string entirely of members, so the run reaches
                        // the terminator rather than stopping early by luck.
                        if (string_get(sets[which]))
                        {
                                positive length = string_length(sets[which]);

                                for (positive i = 0; i < size; i++)
                                        subject[i] = sets[which][i % length];

                                subject[size] = 0;

                                same("string_span", "all members",
                                     string_span(subject, set), size);
                        }
                }

                /*
                        Every width from 0 to 300 at every alignment in
                        sixteen, with a run of the set's own members that ends
                        where it likes: the loop takes two bytes a turn, so
                        each width's exit is its own case and the edges alone
                        do not prove it.
                */
                static p8 sweep[16 + 300 + 8];
                positive length = string_length(sets[which]);

                for (positive align = 0; align < 16; align++)
                        for (positive size = 0; size <= 300; size++)
                        {
                                p8 *at = sweep + align;
                                positive run = next() % (size + 1);

                                for (positive i = 0; i < size; i++)
                                        at[i] = i < run && length ? sets[which][i % length] : (p8)'!';

                                at[size] = 0;
                                same("string_span", "every width and alignment",
                                     string_span(at, set), reference_span(at, set));
                        }
        }
}

/*
        strcmp where it decides, which is the first byte.

        The word loop now has a single byte ahead of it, so the answer for two
        strings that differ where they begin comes out of a different path than
        the answer for two that differ later. check_strings above compares only
        the sign; this compares the value, at every offset either path can end
        at, and over bytes above 127 -- where a signed byte load would give the
        wrong sign and the sign check would not see it.
*/
fn check_compare_edges()
{
        static p8 left[64];
        static p8 right[64];

        same("string_compare", "both empty",
             (positive)string_compare((string_address)"", (string_address)""),
             (positive)reference_compare((string_address)"", (string_address)""));

        same("string_compare", "empty against one",
             (positive)string_compare((string_address)"", (string_address)"a"),
             (positive)reference_compare((string_address)"", (string_address)"a"));

        same("string_compare", "one against empty",
             (positive)string_compare((string_address)"a", (string_address)""),
             (positive)reference_compare((string_address)"a", (string_address)""));

        // Every offset the byte path and the word path can part company at,
        // one past the eight byte step included.
        for (positive at = 0; at < 20; at++)
        {
                for (positive i = 0; i < 24; i++)
                {
                        left[i] = (p8)('a' + i % 26);
                        right[i] = left[i];
                }

                left[24] = 0;
                right[24] = 0;

                right[at] = (p8)(left[at] + 1);

                same("string_compare", "value at offset",
                     (positive)string_compare(left, right),
                     (positive)reference_compare(left, right));

                same("string_compare", "value at offset reversed",
                     (positive)string_compare(right, left),
                     (positive)reference_compare(right, left));

                // The same offset, but one string ends there instead.
                right[at] = left[at];
                right[at] = 0;

                same("string_compare", "ends at offset",
                     (positive)string_compare(left, right),
                     (positive)reference_compare(left, right));

                same("string_compare", "ends at offset reversed",
                     (positive)string_compare(right, left),
                     (positive)reference_compare(right, left));
        }

        // Bytes above 127 against bytes below, which is where a signed load
        // would answer with the wrong sign.
        for (positive a = 0; a < 256; a += 17)
        {
                for (positive b = 0; b < 256; b += 13)
                {
                        left[0] = (p8)a;
                        left[1] = 0;
                        right[0] = (p8)b;
                        right[1] = 0;

                        same("string_compare", "high byte",
                             (positive)string_compare(left, right),
                             (positive)reference_compare(left, right));

                        // And the same pair one byte in, so the word path
                        // reaches them too.
                        left[0] = 'z';
                        left[1] = (p8)a;
                        left[2] = 0;
                        right[0] = 'z';
                        right[1] = (p8)b;
                        right[2] = 0;

                        same("string_compare", "high byte past the first",
                             (positive)string_compare(left, right),
                             (positive)reference_compare(left, right));
                }
        }
}

/*
        The environment lookup against the loop it replaced.

        The reference is env_get's body out of src/sh/builtin.c, copied as it
        stood: a strchr for the equals, a length compare, then the bytes. That
        is the answer string_get_environment has to agree with, entry for entry
        and edge for edge -- an entry with no equals in it, an entry that is
        only an equals, a name that is a prefix of another name, and a name
        that is one byte longer than the entry it nearly matches.
*/
string_address reference_get_environment(string_address address_to list,
                                         string_address name)
{
        if (name == null)
                return null;

        positive name_len = reference_length(name);
        positive idx = 0;

        while (list[idx])
        {
                string_address entry = list[idx];
                string_address eq = reference_first_of(entry, '=');

                if (eq)
                {
                        positive key_len = eq - entry;

                        if (key_len == name_len)
                        {
                                positive i = 0;
                                for (i = 0; i < name_len; i++)
                                {
                                        if (string_get(entry + i) != string_get(name + i))
                                                break;
                                }

                                if (i == name_len)
                                        return eq + 1;
                        }
                }

                idx++;
        }

        return null;
}

fn check_environment_list()
{
        string_address address_to list = program_environment_list();
        positive count = 0;

        while (list && list[count])
        {
                same("program_environment_list", "same entry as indexed access",
                     (positive)list[count], (positive)program_environment((b32)count));
                count++;
        }

        same("program_environment_list", "same terminator as indexed access",
             (positive)(list ? list[count] : null),
             (positive)program_environment((b32)count));

        if (program_stack_base)
        {
                string_address address_to saved_words = program_words;
                b32 saved_count = program_words_count;
                static string_address borrowed[] = {(string_address) "verify"};

                program_arguments_use(borrowed, 0);

                same("program_environment_list", "borrowed arguments keep the environment",
                     (positive)program_environment_list(), (positive)list);
                same("program_environment_list", "borrowed vector agrees with index zero",
                     (positive)program_environment_list()[0],
                     (positive)program_environment(0));

                if (saved_words)
                        program_arguments_use(saved_words, saved_count);
                else
                        program_arguments_own();
        }

        {
                p8 address_to saved_stack = program_stack_base;

                program_stack_base = null;
                same("program_environment_list", "no process stack",
                     (positive)program_environment_list(), 0);
                program_stack_base = saved_stack;
        }
}

fn check_argument_list()
{
        string_address address_to saved_words = program_words;
        b32 saved_count = program_words_count;
        p8 address_to saved_stack = program_stack_base;
        static string_address borrowed[] = {
            (string_address) "storage", (string_address) "one", null};

        if (program_stack_base)
        {
                string_address address_to list = program_argument_list();

                same("program_argument_list", "process stack vector",
                     (positive)list, (positive)(program_stack_base + 8));
                same("program_argument_list", "process stack index zero",
                     (positive)list[0], (positive)program_argument(0));
        }

        program_arguments_use(borrowed, 2);
        same("program_argument_list", "borrowed vector",
             (positive)program_argument_list(), (positive)borrowed);
        same("program_argument_list", "borrowed index one",
             (positive)program_argument_list()[1],
             (positive)program_argument(1));

        program_arguments_own();
        program_stack_base = null;
        same("program_argument_list", "no vector",
             (positive)program_argument_list(), 0);

        program_stack_base = saved_stack;
        if (saved_words)
                program_arguments_use(saved_words, saved_count);
        else
                program_arguments_own();
}

fn check_environment()
{
        check_argument_list();
        check_environment_list();

        static p8 block[64][48];
        static string_address list[65];

        static const string_address shapes[] = {
            "PATH=/bin:/usr/bin:/",
            "SHELL=/bin/sh",
            "V=x",
            "VV=xx",
            "=leading",
            "NOEQUALS",
            "EMPTY=",
            "P=1",
            "PA=2",
            "PAT=3",
            "PATHX=4",
            "a=b",
            "LONGERNAMETHANANYWORD=value",
            "x",
            "",
            null,
        };

        static const string_address names[] = {
            "PATH",
            "SHELL",
            "V",
            "VV",
            "",
            "NOEQUALS",
            "EMPTY",
            "P",
            "PA",
            "PAT",
            "PATHX",
            "PATHXY",
            "a",
            "LONGERNAMETHANANYWORD",
            "LONGERNAMETHANANYWOR",
            "x",
            "MISSING",
            "PATH ",
            null,
        };

        positive count = 0;

        while (shapes[count])
        {
                string_copy(block[count], shapes[count]);
                list[count] = block[count];
                count++;
        }

        list[count] = null;

        // Every name against the whole list, then against every prefix of the
        // list, so the answer is checked where the entry is first, last and
        // absent.
        for (positive cut = 0; cut <= count; cut++)
        {
                string_address held = list[cut];

                list[cut] = null;

                for (positive n = 0; names[n]; n++)
                {
                        same("get_environment", "value",
                             (positive)string_get_environment(list, (string_address)names[n]),
                             (positive)reference_get_environment(list, (string_address)names[n]));
                }

                list[cut] = held;
        }

        // An empty list answers nothing, whatever it is asked.
        {
                static string_address nothing[1];

                nothing[0] = null;

                same("get_environment", "empty list",
                     (positive)string_get_environment(nothing, (string_address)"PATH"),
                     (positive)reference_get_environment(nothing, (string_address)"PATH"));
        }

        // Generated entries and names over a small alphabet, so the prefix and
        // equals cases turn up in combinations nobody chose.
        for (positive round = 0; round < 300; round++)
        {
                static p8 alphabet[5];

                alphabet[0] = 'a';
                alphabet[1] = 'b';
                alphabet[2] = '=';
                alphabet[3] = 'a';
                alphabet[4] = 'c';

                positive entries = next() % 8 + 1;

                for (positive e = 0; e < entries; e++)
                {
                        positive length = next() % 7;

                        for (positive i = 0; i < length; i++)
                                block[e][i] = alphabet[next() % 5];

                        block[e][length] = 0;
                        list[e] = block[e];
                }

                list[entries] = null;

                for (positive t = 0; t < 6; t++)
                {
                        static p8 wanted[8];

                        positive length = next() % 4;

                        for (positive i = 0; i < length; i++)
                                wanted[i] = alphabet[next() % 5];

                        wanted[length] = 0;

                        same("get_environment", "generated",
                             (positive)string_get_environment(list, wanted),
                             (positive)reference_get_environment(list, wanted));
                }
        }
}

/*
        The table lookup, against the loop it replaces.

        The entries are laid out as the shell lays its commands out: a name
        first, then something else, walked by a stride. The awkward cases are
        the ones a first-byte test could get wrong -- names that share a first
        byte, a name that is a prefix of another, and the empty name.
*/
typedef struct
{
        string_address name;
        positive marker;
} find_entry;

static positive reference_table_find(string_address name, address_any table,
                                     positive stride, positive count)
{
        for (positive i = 0; i < count; i++)
        {
                string_address entry =
                    address_to(string_address address_to)((p8 address_to)table + i * stride);

                if (!entry)
                        return count;

                if (!string_compare(entry, name))
                        return i;
        }

        return count;
}

/*
        Names put where reading eight bytes of them would leave the page.

        string_table_find reads a whole word of the wanted name and a whole
        word of each entry's, which is only safe while those eight bytes are
        in one page -- and both reads used to be unguarded. A static array is
        never in the wrong place by accident, so the two are put there on
        purpose: the last bytes of a page, worked out from the address the
        linker gave the buffer rather than assumed.

        The page after this one is mapped here, so what this catches is a
        wrong answer rather than a fault. That is enough: the byte paths the
        guard sends those cases down are only reached this way, and a wrong
        answer out of them says the guard is being taken.
*/
static p8 page_room[3 * 4096];

fn check_table_find_page_edge()
{
        static find_entry sitting[8];
        static string_address held[] = {"cat", "cd", "basename", "dirname",
                                        "e", "export", "z", null};
        positive base = (positive)(address_any)page_room;
        positive page = (4096 - (base & 4095)) & 4095;
        positive count = 0;

        if (page == 0)
                page = 4096;

        for (; held[count]; count++)
                ;

        //      Each entry's name at a shifting distance from the end of a
        //      page, so that some of them cross it and some do not.
        for (positive back = 1; back <= 9; back++)
        {
                p8 address_to at = page_room + page - back;

                for (positive i = 0; i < count; i++)
                {
                        positive length = reference_length(held[i]);

                        //      Only the name being looked for moves; the
                        //      others stay where they are, so the table is
                        //      the same table each time.
                        sitting[i].name = held[i];
                        sitting[i].marker = i;

                        if (i != 2)
                                continue;

                        for (positive k = 0; k <= length; k++)
                                at[k] = (p8)held[i][k];

                        sitting[i].name = (string_address)at;
                }

                for (positive i = 0; i < count; i++)
                {
                        same("table_find", "an entry at the end of a page",
                             string_table_find(held[i], sitting,
                                               sizeof(find_entry), count),
                             reference_table_find(held[i], sitting,
                                                  sizeof(find_entry), count));

                        //      And the wanted name itself at the end of a
                        //      page, which sends the whole call down the
                        //      other path.
                        {
                                positive length = reference_length(held[i]);
                                p8 address_to want = page_room + 2 * 4096 + page - back;

                                for (positive k = 0; k <= length; k++)
                                        want[k] = (p8)held[i][k];

                                same("table_find", "a name at the end of a page",
                                     string_table_find((string_address)want, sitting,
                                                       sizeof(find_entry), count),
                                     reference_table_find((string_address)want, sitting,
                                                          sizeof(find_entry), count));
                        }
                }
        }
}

fn check_table_find()
{
        static find_entry entries[16];
        static string_address names[] = {
            "cat", "cd", "cp", "chmod", "clear", "basename", "b", "",
            "echo", "exit", "export", "e", "zzz", "cata", "ca", null};
        positive count = 0;

        for (; names[count]; count++)
        {
                entries[count].name = names[count];
                entries[count].marker = count;
        }

        // A zero count must answer before touching the table, and the count is
        // an exact fence rather than merely a not-found return value.
        same("table_find", "zero count does not touch the table",
             string_table_find((string_address)"cat", null,
                               sizeof(find_entry), 0),
             0);
        same("table_find", "count fences the walk",
             string_table_find(names[5], entries, sizeof(find_entry), 5),
             5);

        // Every name in the table, which must be found at its own index.
        for (positive i = 0; i < count; i++)
                same("table_find", "present",
                     string_table_find(names[i], entries, sizeof(find_entry), count),
                     reference_table_find(names[i], entries, sizeof(find_entry), count));

        // And names that are not, including ones sharing a first byte with
        // entries that are, and prefixes and extensions of them.
        {
                static string_address absent[] = {
                    "catx", "c", "cha", "ba", "basenam", "basenames", "exporte",
                    "ec", "zz", "zzzz", "q", "", null};

                for (positive i = 0; absent[i]; i++)
                        same("table_find", "absent",
                             string_table_find(absent[i], entries,
                                               sizeof(find_entry), count),
                             reference_table_find(absent[i], entries,
                                                  sizeof(find_entry), count));
        }

        // A table that ends early on a null name rather than on the count.
        entries[5].name = null;
        same("table_find", "null entry stops the walk",
             string_table_find((string_address)"export", entries,
                               sizeof(find_entry), count),
             reference_table_find((string_address)"export", entries,
                                  sizeof(find_entry), count));

        entries[5].name = names[5];

        /*
                The same names, looked up through a copy sitting somewhere
                else.

                Every needle above is a literal, and literals are laid out end
                to end by the compiler -- so a routine that reads past one
                terminator reads the next literal, which is stable and often
                happens to give the right answer. A name of exactly seven
                characters did read past, and this is the shape that says so:
                the needle is copied into a buffer at a shifting offset, so
                what follows it is different every time.
        */
        {
                static p8 room[64];
                static find_entry sized[17];
                static p8 made[17][20];
                positive many = 0;

                // One name of every length from one to sixteen, so the eight
                // byte boundary is crossed from both sides and landed on.
                for (positive length = 1; length <= 16; length++)
                {
                        for (positive at = 0; at < length; at++)
                                made[many][at] = (p8)('a' + (length + at) % 26);

                        made[many][length] = 0;
                        sized[many].name = made[many];
                        sized[many].marker = many;
                        many++;
                }

                for (positive i = 0; i < many; i++)
                        for (positive shift = 0; shift + 20 < sizeof(room); shift++)
                        {
                                string_address moved = room + shift;

                                memory_fill(room, 'x', sizeof(room));
                                string_copy(moved, sized[i].name);

                                same("table_find", "needle copied elsewhere",
                                     string_table_find(moved, sized,
                                                       sizeof(find_entry), many),
                                     reference_table_find(moved, sized,
                                                          sizeof(find_entry), many));
                        }
        }
}

/*
        What is on either side of the string.

        Every needle above sits at the start of a fresh buffer, so nothing
        hostile has ever been in the bytes immediately in front of one. The
        word at a time hunts align their pointer down so the first load cannot
        cross a page, which means they do read those bytes, and then have to
        throw away what they found there. Whether that throwing away is exact
        is the whole question, and it can only be asked by putting something
        in them.

        The values are chosen and not random. A SWAR zero test borrows out of
        a zero byte into the byte above it, so a byte in the prefix equal to
        the one being hunted can raise a flag on the first byte of the string
        -- but only when that first byte is the next value up, which is one
        pair in two hundred and fifty six. Four hundred thousand random trials
        found none of it.

        The field is filled with the hunted byte end to end, so the bytes
        after the terminator are as hostile as the ones before it: a routine
        that reads a word past the end has to discard what it finds there too.
*/

/*
        The names, in both libraries.

        lib.c writes an assembly override for a C function and says which
        with a MOONWATER_HAVE_ macro. library.next.c has no C function to
        override -- the assembly is the routine and the libc names are aliases
        onto it -- so it defines no such macro at all, and a case guarded on
        one compiles to nothing the moment next is in place. Ninety five
        thousand checks went missing that way and the lane still said
        everything agrees, which is the same shape as the bug this section
        was written for.

        So nothing here is guarded. Every routine is declared twice, once
        under each spelling, weak and bound to its symbol by name: a weak name
        the linker cannot find is null instead of an error, so one binary asks
        the library what it has rather than asking the preprocessor what it
        once had. Bound by name because a prototype would collide -- lib.c
        declares a strrchr of its own and next declares none.

        A routine under neither name is named in the transcript and counted.
        The inventory says which machines are short of which, so that is not a
        failure; being short of one silently is.
*/

typedef string_address (address_to hunt_byte)(string_address, b32);
typedef string_address (address_to hunt_bounded)(string_address, positive, b32);
typedef address_any (address_to hunt_memory)(address_any, b32, positive);
typedef positive (address_to measure_bounded)(string_address, positive);
typedef b32 (address_to compare_bounded)(string_address, string_address, positive);

WEAK string_address verify_or_end_public(string_address source, b32 character)
    __asm__("string_first_of_or_end");
WEAK string_address verify_or_end_libc(string_address source, b32 character)
    __asm__("strchrnul");

WEAK string_address verify_first_max_public(string_address source, positive count,
                                            b32 character)
    __asm__("string_first_of_max");
WEAK string_address verify_first_max_libc(string_address source, positive count,
                                          b32 character)
    __asm__("strnchr");

WEAK string_address verify_last_or_end_public(string_address source, b32 character)
    __asm__("string_last_of_or_end");
WEAK string_address verify_last_or_end_libc(string_address source, b32 character)
    __asm__("strrchr");

WEAK address_any verify_memory_first_public(address_any source, b32 character,
                                            positive count)
    __asm__("memory_first_of");
WEAK address_any verify_memory_first_libc(address_any source, b32 character,
                                          positive count)
    __asm__("memchr");

WEAK positive verify_length_max_public(string_address source, positive bound)
    __asm__("string_length_max");
WEAK positive verify_length_max_libc(string_address source, positive bound)
    __asm__("strnlen");

WEAK b32 verify_compare_max_public(string_address a, string_address b, positive count)
    __asm__("string_compare_max");
WEAK b32 verify_compare_max_libc(string_address a, string_address b, positive count)
    __asm__("strncmp");

// Walks to the terminator and answers with it rather than with nothing, which
// is the whole difference between this and string_first_of.
string_address reference_or_end(string_address source, p8 character)
{
        while (string_get(source) && string_get(source) != character)
                source++;

        return source;
}

// The character is looked at before the terminator is, so a hunt for zero
// finds the terminator inside the bound. That is what the kernel's own does.
string_address reference_first_max(string_address source, positive count,
                                   p8 character)
{
        for (positive i = 0; i < count; i++)
        {
                if (source[i] == character)
                        return source + i;

                if (!source[i])
                        break;
        }

        return null;
}

address_any reference_memory_first(address_any source, p8 character, positive count)
{
        p8 address_to at = source;

        for (positive i = 0; i < count; i++)
                if (at[i] == character)
                        return at + i;

        return null;
}

positive reference_length_max(string_address source, positive bound)
{
        positive i = 0;

        while (i < bound && source[i])
                i++;

        return i;
}

b32 reference_compare_max(string_address a, string_address b, positive count)
{
        for (positive i = 0; i < count; i++)
        {
                if (a[i] != b[i])
                        return (b32)a[i] - (b32)b[i];

                if (!a[i])
                        return 0;
        }

        return 0;
}

string_address reference_last_or_end(string_address source, p8 character)
{
        string_address last = null;

        for (;; source++)
        {
                if (string_get(source) == character)
                        last = source;

                if (!string_get(source))
                        return last;
        }
}

static p8 field[512];
static p8 field_twin[512];

// Eight byte aligned, so an offset from here is the whole of the distance
// into a word however the linker placed the array.
static string_address aligned_at(p8 address_to array)
{
        positive at = (positive)(address_any)array;

        return array + ((8 - (at & 7)) & 7);
}

/*
        The bounded span and the digit run, and the two ways they are lied to.

        Both are new and both replaced a byte loop written out in six files, so
        the reference below is that loop and not a second idea about what it
        should do.

        Neither is ever handed a string literal. Literals sit end to end in
        .rodata, and a routine that reads one byte past its own terminator
        reads the front of the next one and usually gets the right answer
        anyway; a seven character name once survived forty two thousand checks
        that way. Everything here is built in a buffer at a shifting offset,
        with what sits in front of the subject and what sits behind the end of
        it chosen to be the worst possible answer rather than a zero.
*/
positive reference_span_max(string_address source, positive bound,
                            const b8 address_to set)
{
        positive n = 0;

        while (n < bound)
        {
                p8 c = source[n];

                if (!set[c])
                        return n;

                n++;
        }

        return n;
}

positive reference_digits(string_address source, positive bound,
                          positive address_to used)
{
        positive value = 0;
        positive n = 0;

        while (n < bound && source[n] >= '0' && source[n] <= '9')
        {
                value = value * 10 + (positive)(source[n] - '0');
                n++;
        }

        if (used)
                address_to used = n;

        return value;
}

static bipolar reference_bipolar(string_address source, positive address_to used)
{
        positive at = 0;
        positive digits = 0;
        positive value = 0;
        p8 negative = false;

        if (source)
        {
                if (source[at] == '-' || source[at] == '+')
                {
                        negative = source[at] == '-';
                        at++;
                }

                while (source[at] >= '0' && source[at] <= '9')
                {
                        value = value * 10 + (positive)(source[at] - '0');
                        at++;
                        digits++;
                }
        }

        if (!digits)
                at = 0;

        if (used)
                address_to used = at;

        return negative && digits ? (bipolar)((positive)0 - value)
                                  : (bipolar)value;
}

static positive reference_base_digit(p8 character)
{
        if (character >= '0' && character <= '9')
                return (positive)(character - '0');

        if (character >= 'a' && character <= 'z')
                return (positive)(character - 'a' + 10);

        if (character >= 'A' && character <= 'Z')
                return (positive)(character - 'A' + 10);

        return (positive)-1;
}

positive reference_digits_base_max(string_address source, positive bound,
                                   positive base, positive address_to used)
{
        positive value = 0;
        positive n = 0;

        if (base >= 2 && base <= 36)
        {
                while (n < bound)
                {
                        positive digit = reference_base_digit(source[n]);

                        if (digit >= base)
                                break;

                        value = value * base + digit;
                        n++;
                }
        }

        if (used)
                address_to used = n;

        return value;
}

p8 reference_digits_exact(string_address source, positive address_to value)
{
        positive used;

        if (!source)
                return false;

        positive parsed = reference_digits(source, (positive)-1, address_of used);

        if (!used || source[used])
                return false;

        if (value)
                address_to value = parsed;

        return true;
}

static p8 span_room[1024];

// Far enough in that a routine aligning its pointer down has plenty in front
// of the subject to be fooled by.
#define SPAN_HEAD 128

static p8 address_to span_subject(positive offset)
{
        return (p8 address_to)aligned_at(span_room) + SPAN_HEAD + offset;
}

static positive span_bounds[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 11,
                                 15, 16, 17, 23, 24, 25, 26, 31, 32, 33};

#define SPAN_BOUND_COUNT (sizeof(span_bounds) / sizeof(span_bounds[0]))

fn check_span_max()
{
        static b8 set[STRING_SET_BYTES];
        static string_address sets[] = {
            " \t",
            "0123456789",
            "abcdefghijklmnopqrstuvwxyz",
            "|&;<>()$`\\\"' \t\n",
        };

        positive count = sizeof(sets) / sizeof(sets[0]);

        // One pass past the end of that list, with every byte a member and the
        // terminator among them. The bound is then the only thing that can end
        // the run, and a routine that quietly stops at a zero the way
        // string_span does is caught here and nowhere else.
        for (positive which = 0; which <= count; which++)
        {
                positive members = 1;
                p8 breaker = 0;

                if (which == count)
                        reference_fill(set, 1, sizeof(set));
                else
                {
                        reference_fill(set, 0, sizeof(set));
                        string_set_add(set, sets[which]);
                        members = string_length(sets[which]);

                        for (positive v = 1; v < 256; v++)
                                if (!set[v])
                                {
                                        breaker = (p8)v;
                                        break;
                                }
                }

                // What sits outside the window. A member on one pass, so
                // anything reading past the bound or in front of the pointer
                // keeps counting; a non-member on the other, so anything
                // reading outside it stops short instead. Both directions,
                // because only one of the two shows up as a number too small.
                for (positive hostile = 0; hostile < 2; hostile++)
                {
                        p8 outside =
                            hostile ? (which == count ? (p8)0xff
                                                      : (p8)sets[which][0])
                                    : (p8)0x00;

                        for (positive offset = 0; offset < 8; offset++)
                                for (positive b = 0; b < SPAN_BOUND_COUNT; b++)
                                {
                                        positive bound = span_bounds[b];

                                        // The last stop is the run that
                                        // nothing inside the window ends.
                                        for (positive stop = 0; stop <= 34; stop++)
                                        {
                                                p8 address_to subject =
                                                    span_subject(offset);

                                                reference_fill(span_room, outside,
                                                               sizeof(span_room));

                                                for (positive i = 0; i < 40; i++)
                                                        subject[i] =
                                                            which == count
                                                                ? (p8)(i * 7 + 3)
                                                                : (p8)sets[which]
                                                                          [i % members];

                                                if (stop < 34 && which != count)
                                                        subject[stop] = breaker;

                                                same("string_span_max",
                                                     "against the loop",
                                                     string_span_max(subject, bound,
                                                                     set),
                                                     reference_span_max(subject, bound,
                                                                        set));
                                        }
                                }
                }
        }
}

/*
        string_span_max's vector body against the loop it answers for: bounds
        from 0 to 200, every start residue, the breaking byte at every place
        (and nowhere), a set of random members that holds bytes above 127 as
        the two halves of the table are told apart there, and a window that
        ends exactly at the page edge so that a load that runs past the bound
        is a fault. On x86_64 with the vector body on and off.
*/
fn check_span_max_wide()
{
        p8 address_to pages = guard_pages("string_span_max");

        if (!pages)
                return;
        static b8 set[256];
        p64 seed = 0x9e3779b97f4a7c15ull;
#if X64
        p8 vbmi = cpu_has_avx512_vbmi;
        positive tiers = 2;
#else
        positive tiers = 1;
#endif
        for (positive tier = 0; tier < tiers; tier++)
        {
#if X64
                cpu_has_avx512_vbmi = tier < 1 ? vbmi : 0;
#endif
                for (positive kind = 0; kind < 3; kind++)
                {
                        // Members: every byte, or about seven in eight at random.
                        for (positive v = 0; v < 256; v++)
                        {
                                XORSHIFT64(seed);
                                set[v] = kind == 0 ? 1 : (b8)((seed >> 20) % 8 != 0);
                        }
                        set[0x80] = kind == 2 ? 0 : 1;
                        set[0xff] = 1;
                        set['a'] = 1;
                        positive breaker = 0;
                        for (positive v = 255; v; v--)
                                if (!set[v])
                                        breaker = v;
                        for (positive bound = 0; bound <= 200; bound++)
                                for (positive place = 0; place < 4; place++)
                                {
                                        positive offset = place == 3 ? 4096 - bound : place * 21;
                                        p8 address_to subject = pages + 4096 + offset;

                                        for (positive stop = 0; stop <= bound + 1; stop += stop < 70 || stop > bound - 3 ? 1 : 13)
                                        {
                                                for (positive i = 0; i < bound; i++)
                                                {
                                                        XORSHIFT64(seed);
                                                        p8 pick = (p8)(seed >> 24);
                                                        while (!set[pick])
                                                                pick = (p8)(pick + 1);
                                                        subject[i] = kind == 1 && (i & 1) ? (p8)('a') : pick;
                                                }
                                                if (breaker && stop < bound)
                                                        subject[stop] = (p8)breaker;
                                                same("string_span_max", "wide against the loop",
                                                     string_span_max(subject, bound, set),
                                                     reference_span_max(subject, bound, set));
                                        }
                                }
                }
        }
#if X64
        cpu_has_avx512_vbmi = vbmi;
#endif
        memory_free(pages, 3 * 4096);
}

/*
        The five bodies that took a zmm and a ymm form -- the byte sum, the
        xor, the two case conversions and the 33 hash -- against a page with
        a dead one on each side. The span is the first bytes of the live page
        and then its last, at every length to 1100 and a few above, so a load
        that starts before the span or ends past it is a fault at some length,
        and what is written is the span and nothing of the rest of the page,
        which is poisoned and read back whole. Under each body the machine
        has: the zmm one, the ymm one, the floor, and on x86_64 the zmm one
        again with the hash told there is no DQ.
*/
static fn wide_guard_pass(p8 address_to page)
{
        static p8 poison[4096];
        static p8 want[4096];
        static const p8 odd[] = {'@', 'A', 'M', 'Z', '[', '`', 'a', 'm', 'z', '{',
                                 0, 0x7f, 0x80, 0xc1, 0xdb, 0xe1, 0xfa, 0xff, ' ', '~'};
        positive lengths = 1100 + 4;

        for (positive step = 0; step < lengths; step++)
        {
                positive length = step <= 1100 ? step : (positive[]){2048, 4095, 4096, 3000}[step - 1101];

                for (positive edge = 0; edge < 2; edge++)
                {
                        p8 address_to span = edge ? page + 4096 - length : page;
                        positive hash = 5381;
                        positive sum = 0;

                        for (positive i = 0; i < 4096; i++)
                                poison[i] = (p8)next();
                        memory_copy(page, poison, 4096);
                        for (positive i = 0; i < length; i++)
                        {
                                p64 pick = next();

                                span[i] = poison[(span - page) + i] =
                                    (pick & 1) ? (p8)(pick >> 8) : odd[(pick >> 8) % sizeof(odd)];
                                hash = hash * 33 + span[i];
                                sum += span[i];
                        }

                        same("memory_hash_33", "page edge", memory_hash_33(span, length), hash);
                        same("memory_sum_bytes", "page edge", memory_sum_bytes(span, length), sum);

                        for (positive which = 0; which < 3; which++)
                        {
                                string_address name = which == 0 ? "memory_frob"
                                                      : which == 1 ? "memory_to_lower_ascii"
                                                                   : "memory_to_upper_ascii";

                                memory_copy(want, poison, 4096);
                                memory_copy(page, poison, 4096);
                                for (positive i = 0; i < length; i++)
                                {
                                        p8 address_to at = want + (span - page) + i;

                                        if (which == 0)
                                                *at ^= 42;
                                        else if (which == 1 && *at >= 'A' && *at <= 'Z')
                                                *at += 32;
                                        else if (which == 2 && *at >= 'a' && *at <= 'z')
                                                *at -= 32;
                                }
                                same(name, "returns its argument",
                                     (positive)(which == 0   ? memory_frob(span, length)
                                                : which == 1 ? memory_to_lower_ascii(span, length)
                                                             : memory_to_upper_ascii(span, length)),
                                     (positive)(address_any)span);
                                same_bytes(name, "the span and nothing of the page", page, want, 4096);
                        }
                }
        }
}

fn check_wide_guards()
{
        p8 address_to pages = guard_pages("the wide bodies");

        if (!pages)
                return;
#if X64
        p8 avx2 = cpu_has_avx2, avx512 = cpu_has_avx512, dq = cpu_has_avx512_dq;

        for (positive tier = 0; tier < 4; tier++)
        {
                cpu_has_avx2 = tier < 2 || tier == 3 ? avx2 : 0;
                cpu_has_avx512 = tier == 0 || tier == 3 ? avx512 : 0;
                cpu_has_avx512_dq = tier == 3 ? 1 : 0;
                wide_guard_pass(pages + 4096);
        }
        cpu_has_avx2 = avx2;
        cpu_has_avx512 = avx512;
        cpu_has_avx512_dq = dq;
#else
        wide_guard_pass(pages + 4096);
#endif
        memory_free(pages, 3 * 4096);
}

/*
        The digit run.

        The buffer is digits from end to end and the run is a window inside it
        ended by one byte that is not a digit. So a routine that reads a word
        in front of its pointer, or one byte past where it was told to stop, is
        handed more digits and answers a number that is not the one asked for
        -- which is the only way this can be wrong and still look plausible.
*/
static p8 digit_breakers[] = {0x00, '/', ':', ' ', 'a', 0xff, '.', '-', '+'};

#define DIGIT_BREAKER_COUNT (sizeof(digit_breakers) / sizeof(digit_breakers[0]))

fn check_digits()
{
        // Null must be rejected before the first load, and failure must not
        // commit through the optional result pointer.
        {
                positive got = (positive)0xa5a5a5a5a5a5a5a5ull;
                positive want = got;

                same("string_digits_exact", "null source",
                     string_digits_exact(null, address_of got), false);
                same("string_digits_exact", "null leaves output alone", got, want);
                same("string_digits_exact", "null with no output",
                     string_digits_exact(null, null), false);
        }

        for (positive br = 0; br < DIGIT_BREAKER_COUNT; br++)
                for (positive offset = 0; offset < 8; offset++)
                        for (positive length = 0; length <= 26; length++)
                        {
                                p8 address_to subject = span_subject(offset);
                                positive want_used = 0;
                                positive got_used = 0;

                                reference_fill(span_room, '7', sizeof(span_room));

                                for (positive i = 0; i < length; i++)
                                        subject[i] = (p8)('0' + next() % 10);

                                subject[length] = digit_breakers[br];

                                same("string_digits", "value",
                                     string_digits(subject, address_of got_used),
                                     reference_digits(subject, (positive)-1,
                                                      address_of want_used));

                                same("string_digits", "bytes taken", got_used,
                                     want_used);

                                // The count is what tells a caller there were
                                // no digits at all: a single zero and no digits
                                // both answer nothing.
                                same("string_digits", "none is not a zero",
                                     (positive)(got_used == 0),
                                     (positive)(length == 0));

                                // Handed nowhere to put the count.
                                same("string_digits", "no count wanted",
                                     string_digits(subject, 0),
                                     reference_digits(subject, (positive)-1, 0));

                                // The exact parser accepts only the NUL-ended,
                                // nonempty run. Both answers begin poisoned so
                                // every failure also proves no output store.
                                {
                                        positive got_value =
                                            (positive)0xa5a5a5a5a5a5a5a5ull;
                                        positive want_value = got_value;

                                        same("string_digits_exact", "validity",
                                             string_digits_exact(
                                                 subject, address_of got_value),
                                             reference_digits_exact(
                                                 subject, address_of want_value));
                                        same("string_digits_exact",
                                             "value or untouched output",
                                             got_value, want_value);
                                        same("string_digits_exact",
                                             "optional output",
                                             string_digits_exact(subject, null),
                                             reference_digits_exact(subject, null));
                                }

                                for (positive bound = 0; bound <= length + 3; bound++)
                                {
                                        same("string_digits_max", "value",
                                             string_digits_max(subject, bound,
                                                               address_of got_used),
                                             reference_digits(subject, bound,
                                                              address_of want_used));

                                        same("string_digits_max", "bytes taken",
                                             got_used, want_used);
                                }

                                same("string_digits_max", "no count wanted",
                                     string_digits_max(subject, length, 0),
                                     reference_digits(subject, length, 0));
                        }
}

fn check_signed_digits_and_width()
{
        static string_address cases[] = {
            "", "+", "-", "0", "+0", "-0", "1", "+17", "-17",
            "1x", "+1x", "-1x", "18446744073709551615",
            "18446744073709551616", "-18446744073709551616",
            "999999999999999999999999999999", " 1", "\t-1",
        };

        positive got_used = (positive)-1;
        same("string_bipolar", "null value", string_bipolar(null, address_of got_used),
             0);
        same("string_bipolar", "null used", got_used, 0);
        same("string_bipolar", "null optional used", string_bipolar(null, null), 0);

        for (positive which = 0; which < sizeof(cases) / sizeof(cases[0]); which++)
                for (positive offset = 0; offset < 8; offset++)
                {
                        p8 address_to subject = span_subject(offset);
                        positive length = string_length(cases[which]);
                        positive want_used = 0;

                        reference_fill(span_room, 0xa5, sizeof(span_room));
                        memory_copy(subject, cases[which], length + 1);

                        got_used = (positive)-1;
                        bipolar want = reference_bipolar(subject,
                                                         address_of want_used);
                        same("string_bipolar", "value",
                             string_bipolar(subject, address_of got_used), want);
                        same("string_bipolar", "used", got_used, want_used);
                        same("string_bipolar", "optional used",
                             string_bipolar(subject, null), want);
                }

        positive power = 1;
        for (positive digits = 1; digits <= 20; digits++)
        {
                same("positive_digits", "power", positive_digits(power), digits);
                if (power > 1)
                        same("positive_digits", "before power",
                             positive_digits(power - 1), digits - 1);
                if (digits < 20)
                {
                        same("positive_digits", "after power",
                             positive_digits(power + 1), digits);
                        power *= 10;
                }
        }
        same("positive_digits", "maximum", positive_digits((positive)-1), 20);
}

/*
        A bounded digit run in every supported base.

        The byte at the last place is walked through all 256 possibilities for
        every base, so punctuation near the alphabet -- especially '@', '`',
        '[' and '{' -- cannot accidentally become a digit through ASCII case
        folding. The bytes after the bound are valid zeroes: crossing the
        fence therefore changes both the consumed count and usually the value.

        Every source alignment through sixteen bytes is crossed with those
        bytes and bases. A separate long run crosses every bound through 96;
        that reaches unsigned wrap even in base two and proves wrapping and
        the strict pre-load fence together.
*/
static p8 base_digit_character(positive digit, p8 upper)
{
        if (digit < 10)
                return (p8)('0' + digit);

        return (p8)((upper ? 'A' : 'a') + digit - 10);
}

fn check_digits_base()
{
        static positive invalid_bases[] = {0, 1, 37, 255, (positive)-1};

        // No source exists to load. A valid base with a zero bound and an
        // invalid base with any bound must both finish before the first load.
        for (positive base = 2; base <= 36; base++)
        {
                positive got_used = (positive)-1;

                same("string_digits_base_max", "zero bound null value",
                     string_digits_base_max(null, 0, base, address_of got_used), 0);
                same("string_digits_base_max", "zero bound null used", got_used, 0);
                same("string_digits_base_max", "zero bound no used",
                     string_digits_base_max(null, 0, base, null), 0);

                if (base == 8)
                {
                        got_used = (positive)-1;
                        same("string_digits_octal_max", "zero bound null value",
                             string_digits_octal_max(null, 0, address_of got_used), 0);
                        same("string_digits_octal_max", "zero bound null used",
                             got_used, 0);
                        same("string_digits_octal_max", "zero bound no used",
                             string_digits_octal_max(null, 0, null), 0);
                        got_used = (positive)-1;
                        same("string_digits_octal_escape_max",
                             "zero bound null value",
                             string_digits_octal_escape_max(
                                 null, 0, address_of got_used), 0);
                        same("string_digits_octal_escape_max",
                             "zero bound null used", got_used, 0);
                        same("string_digits_octal_escape_max",
                             "zero bound no used",
                             string_digits_octal_escape_max(null, 0, null), 0);
                }
                else if (base == 16)
                {
                        got_used = (positive)-1;
                        same("string_digits_hexadecimal_max", "zero bound null value",
                             string_digits_hexadecimal_max(null, 0,
                                                           address_of got_used),
                             0);
                        same("string_digits_hexadecimal_max", "zero bound null used",
                             got_used, 0);
                        same("string_digits_hexadecimal_max", "zero bound no used",
                             string_digits_hexadecimal_max(null, 0, null), 0);
                        got_used = (positive)-1;
                        same("string_digits_hexadecimal_escape_max",
                             "zero bound null value",
                             string_digits_hexadecimal_escape_max(
                                 null, 0, address_of got_used), 0);
                        same("string_digits_hexadecimal_escape_max",
                             "zero bound null used", got_used, 0);
                        same("string_digits_hexadecimal_escape_max",
                             "zero bound no used",
                             string_digits_hexadecimal_escape_max(null, 0, null),
                             0);
                }
        }

        for (positive i = 0; i < sizeof(invalid_bases) / sizeof(invalid_bases[0]); i++)
        {
                positive got_used = (positive)-1;

                same("string_digits_base_max", "invalid base null value",
                     string_digits_base_max(null, 1, invalid_bases[i],
                                            address_of got_used),
                     0);
                same("string_digits_base_max", "invalid base null used", got_used, 0);
        }

        for (positive base = 2; base <= 36; base++)
                for (positive offset = 0; offset < 16; offset++)
                        for (positive length = 0; length <= 24; length++)
                        {
                                p8 address_to subject = span_subject(offset);

                                reference_fill(span_room, 0xa5, sizeof(span_room));

                                for (positive i = 0; i < length; i++)
                                {
                                        positive digit = (i * 17 + base + offset) % base;

                                        subject[i] =
                                            base_digit_character(digit, (i & 1) != 0);
                                }

                                // A valid byte beyond the fence makes any
                                // over-read continue instead of hiding itself.
                                subject[length + 1] = '0';
                                subject[length + 2] = '0';

                                for (positive byte = 0; byte < 256; byte++)
                                {
                                        positive got_used = (positive)-1;
                                        positive want_used = (positive)-1;

                                        subject[length] = (p8)byte;

                                        same("string_digits_base_max", "every byte value",
                                             string_digits_base_max(
                                                 subject, length + 1, base,
                                                 address_of got_used),
                                             reference_digits_base_max(
                                                 subject, length + 1, base,
                                                 address_of want_used));
                                        same("string_digits_base_max", "every byte used",
                                             got_used, want_used);

                                        if (base == 8)
                                        {
                                                got_used = (positive)-1;
                                                same("string_digits_octal_max",
                                                     "every byte value",
                                                     string_digits_octal_max(
                                                         subject, length + 1,
                                                         address_of got_used),
                                                     reference_digits_base_max(
                                                         subject, length + 1, 8,
                                                         address_of want_used));
                                                same("string_digits_octal_max",
                                                     "every byte used", got_used,
                                                     want_used);
                                                got_used = (positive)-1;
                                                same("string_digits_octal_escape_max",
                                                     "every byte value",
                                                     string_digits_octal_escape_max(
                                                         subject, length + 1,
                                                         address_of got_used),
                                                     reference_digits_base_max(
                                                         subject, length + 1, 8,
                                                         address_of want_used));
                                                same("string_digits_octal_escape_max",
                                                     "every byte used", got_used,
                                                     want_used);
                                        }
                                        else if (base == 16)
                                        {
                                                got_used = (positive)-1;
                                                same("string_digits_hexadecimal_max",
                                                     "every byte value",
                                                     string_digits_hexadecimal_max(
                                                         subject, length + 1,
                                                         address_of got_used),
                                                     reference_digits_base_max(
                                                         subject, length + 1, 16,
                                                         address_of want_used));
                                                same("string_digits_hexadecimal_max",
                                                     "every byte used", got_used,
                                                     want_used);
                                                got_used = (positive)-1;
                                                same("string_digits_hexadecimal_escape_max",
                                                     "every byte value",
                                                     string_digits_hexadecimal_escape_max(
                                                         subject, length + 1,
                                                         address_of got_used),
                                                     reference_digits_base_max(
                                                         subject, length + 1, 16,
                                                         address_of want_used));
                                                same("string_digits_hexadecimal_escape_max",
                                                     "every byte used", got_used,
                                                     want_used);
                                        }
                                }

                                same("string_digits_base_max", "optional used",
                                     string_digits_base_max(subject, length, base, null),
                                     reference_digits_base_max(subject, length, base,
                                                               null));

                                if (base == 8)
                                {
                                        same("string_digits_octal_max", "optional used",
                                             string_digits_octal_max(subject, length,
                                                                     null),
                                             reference_digits_base_max(subject,
                                                                       length, 8,
                                                                       null));
                                        same("string_digits_octal_escape_max",
                                             "optional used",
                                             string_digits_octal_escape_max(subject,
                                                                            length,
                                                                            null),
                                             reference_digits_base_max(subject,
                                                                       length, 8,
                                                                       null));
                                }
                                else if (base == 16)
                                {
                                        same("string_digits_hexadecimal_max",
                                             "optional used",
                                             string_digits_hexadecimal_max(subject,
                                                                           length,
                                                                           null),
                                             reference_digits_base_max(subject,
                                                                       length, 16,
                                                                       null));
                                        same("string_digits_hexadecimal_escape_max",
                                             "optional used",
                                             string_digits_hexadecimal_escape_max(
                                                 subject, length, null),
                                             reference_digits_base_max(subject,
                                                                       length, 16,
                                                                       null));
                                }
                        }

        // Ninety six binary digits are enough to overflow a 64-bit word; the
        // same run therefore crosses the wrap point in every larger base too.
        for (positive base = 2; base <= 36; base++)
                for (positive offset = 0; offset < 16; offset++)
                {
                        p8 address_to subject = span_subject(offset);

                        reference_fill(span_room, 0xa5, sizeof(span_room));

                        for (positive i = 0; i < 98; i++)
                        {
                                positive digit = (i * 29 + base - 1) % base;

                                subject[i] = base_digit_character(digit, (i & 1) != 0);
                        }

                        for (positive bound = 0; bound <= 96; bound++)
                        {
                                positive got_used = (positive)-1;
                                positive want_used = (positive)-1;

                                same("string_digits_base_max", "bounded wrap value",
                                     string_digits_base_max(subject, bound, base,
                                                            address_of got_used),
                                     reference_digits_base_max(subject, bound, base,
                                                               address_of want_used));
                                same("string_digits_base_max", "bounded wrap used",
                                     got_used, want_used);

                                if (base == 8)
                                {
                                        got_used = (positive)-1;
                                        same("string_digits_octal_max",
                                             "bounded wrap value",
                                             string_digits_octal_max(subject, bound,
                                                                     address_of got_used),
                                             reference_digits_base_max(
                                                 subject, bound, 8,
                                                 address_of want_used));
                                        same("string_digits_octal_max",
                                             "bounded wrap used", got_used,
                                             want_used);
                                        got_used = (positive)-1;
                                        same("string_digits_octal_escape_max",
                                             "bounded wrap value",
                                             string_digits_octal_escape_max(
                                                 subject, bound,
                                                 address_of got_used),
                                             reference_digits_base_max(
                                                 subject, bound, 8,
                                                 address_of want_used));
                                        same("string_digits_octal_escape_max",
                                             "bounded wrap used", got_used,
                                             want_used);
                                }
                                else if (base == 16)
                                {
                                        got_used = (positive)-1;
                                        same("string_digits_hexadecimal_max",
                                             "bounded wrap value",
                                             string_digits_hexadecimal_max(
                                                 subject, bound,
                                                 address_of got_used),
                                             reference_digits_base_max(
                                                 subject, bound, 16,
                                                 address_of want_used));
                                        same("string_digits_hexadecimal_max",
                                             "bounded wrap used", got_used,
                                             want_used);
                                        got_used = (positive)-1;
                                        same("string_digits_hexadecimal_escape_max",
                                             "bounded wrap value",
                                             string_digits_hexadecimal_escape_max(
                                                 subject, bound,
                                                 address_of got_used),
                                             reference_digits_base_max(
                                                 subject, bound, 16,
                                                 address_of want_used));
                                        same("string_digits_hexadecimal_escape_max",
                                             "bounded wrap used", got_used,
                                             want_used);
                                }
                        }
                }
}

/*
        The numbers where the arithmetic is the question rather than the scan:
        the largest a positive holds, the one past it that wraps, and a run of
        leading zeros long enough that a routine keeping the place value in a
        register would have run out of them.

        Copied into the buffer rather than passed as the literal they are
        written as, for the reason the block above gives.
*/
static string_address digit_exacts[] = {
    "0",
    "00",
    "007",
    "9",
    "10",
    "0000000000000000000000000001",
    "18446744073709551615",
    "18446744073709551616",
    "18446744073709551625",
    "99999999999999999999",
    "999999999999999999999999999999",
    "4294967295",
    "4294967296",
    "1234567890123456789",
};

// The digit loops, whose next-byte test sits at the foot of the loop: every run
// up to three hundred digits from every start alignment to sixteen, followed by
// nothing, a space or a letter, against the C reference, with the bounds that
// matter around the run. The value wraps the way it always did.
static p8 long_digit_subject[512 + 64];

fn check_digits_long_runs()
{
        static const p8 breakers[3] = {0, ' ', 'x'};

        for (positive offset = 0; offset < 16; offset++)
                for (positive length = 0; length <= 300; length++)
                        for (positive br = 0; br < 3; br++)
                        {
                                p8 address_to subject = long_digit_subject + offset;
                                positive want_used = 0;
                                positive got_used = 0;

                                for (positive i = 0; i < length; i++)
                                        subject[i] = (p8)('0' + next() % 10);

                                subject[length] = breakers[br];

                                same("string_digits", "long run value",
                                     string_digits(subject, address_of got_used),
                                     reference_digits(subject, (positive)-1,
                                                      address_of want_used));
                                same("string_digits", "long run bytes", got_used, want_used);

                                same("string_digits_exact", "long run",
                                     string_digits_exact(subject, null),
                                     reference_digits_exact(subject, null));

                                for (positive bound = 0; bound <= length + 2;
                                     bound += (bound < 24 ? 1 : 9))
                                {
                                        positive got_max = 0;
                                        positive want_max = 0;

                                        got_max = string_digits_max(subject, bound,
                                                                    address_of got_used);
                                        want_max = reference_digits(subject, bound,
                                                                    address_of want_used);
                                        same("string_digits_max", "long run value",
                                             got_max, want_max);
                                        same("string_digits_max", "long run bytes",
                                             got_used, want_used);
                                }
                        }
}

#define DIGIT_EXACT_COUNT (sizeof(digit_exacts) / sizeof(digit_exacts[0]))

fn check_digits_exact()
{
        for (positive which = 0; which < DIGIT_EXACT_COUNT; which++)
                for (positive br = 0; br < DIGIT_BREAKER_COUNT; br++)
                        for (positive offset = 0; offset < 8; offset++)
                        {
                                p8 address_to subject = span_subject(offset);
                                positive length = string_length(digit_exacts[which]);
                                positive want_used = 0;
                                positive got_used = 0;

                                reference_fill(span_room, '7', sizeof(span_room));

                                for (positive i = 0; i < length; i++)
                                        subject[i] = digit_exacts[which][i];

                                subject[length] = digit_breakers[br];

                                same("string_digits", "the exact ones",
                                     string_digits(subject, address_of got_used),
                                     reference_digits(subject, (positive)-1,
                                                      address_of want_used));

                                same("string_digits", "the exact ones, taken",
                                     got_used, want_used);

                                // Includes positive_max, the first value past
                                // it and longer all-digit words. Validity is
                                // independent of wrap, while the stored value
                                // must wrap exactly like the prefix parser.
                                {
                                        positive got_value =
                                            (positive)0x5a5a5a5a5a5a5a5aull;
                                        positive want_value = got_value;

                                        same("string_digits_exact", "wrap validity",
                                             string_digits_exact(
                                                 subject, address_of got_value),
                                             reference_digits_exact(
                                                 subject, address_of want_value));
                                        same("string_digits_exact", "wrapped value",
                                             got_value, want_value);
                                }

                                for (positive bound = 0; bound <= length + 1; bound++)
                                        same("string_digits_max", "the exact ones",
                                             string_digits_max(subject, bound, 0),
                                             reference_digits(subject, bound, 0));
                        }
}

/*
        The digits of a number, into a buffer.

        The reference is the loop this replaced, written out the way all
        twenty two copies of it were: least significant digit first into a
        scratch array, then the array backwards into the output.

        Two things are checked that a value comparison would miss. The buffer
        is filled with a byte that is not a digit and the bytes on both sides
        of the answer are checked afterwards, because a routine that writes a
        terminator, or pads to a width, or writes the scratch out whole,
        answers the right length and corrupts what was next to it. And the
        destination is walked across eight offsets, so a store that assumed an
        aligned buffer is caught.
*/
positive reference_into(p8 address_to into, positive value)
{
        p8 scratch[24];
        positive have = 0;

        if (!value)
                scratch[have++] = '0';

        while (value)
        {
                scratch[have++] = (p8)('0' + value % 10);
                value /= 10;
        }

        for (positive i = 0; i < have; i++)
                into[i] = scratch[have - 1 - i];

        return have;
}

/*
        The compact binary spelling that used to be repeated in file_human,
        df_amount and ls_human_width. This reference says the intended
        overflow-safe rule explicitly: a ceiling is quotient plus a nonzero
        remainder, never an addition near positive_max.
*/
positive reference_into_human_1024(p8 address_to into, positive value)
{
        static p8 units[] = "BKMGTPE";
        positive divisor = 1;
        positive unit = 0;

        while (value / divisor >= 1024 && unit < 6)
        {
                divisor *= 1024;
                unit++;
        }

        if (!unit)
        {
                positive length = reference_into(into, value);

                into[length] = end;
                return length;
        }

        positive quotient = value / divisor;
        positive remainder = value % divisor;
        positive length;

        if (quotient >= 10)
                length = reference_into(into, quotient + (remainder != 0));
        else
        {
                positive fraction = (remainder * 10 + divisor - 1) / divisor;

                if (fraction == 10)
                {
                        quotient++;
                        fraction = 0;
                }

                if (quotient >= 10)
                        length = reference_into(into, quotient);
                else
                {
                        length = reference_into(into, quotient);
                        into[length++] = '.';
                        length += reference_into(into + length, fraction);
                }
        }

        into[length++] = units[unit];
        into[length] = end;

        return length;
}

positive reference_into_base(p8 address_to into, positive value, positive base,
                             p8 upper)
{
        p8 scratch[64];
        positive have = 0;

        do
        {
                positive digit = value % base;

                scratch[have++] =
                    (p8)(digit < 10 ? '0' + digit
                                    : (upper ? 'A' : 'a') + digit - 10);
                value /= base;
        } while (value);

        for (positive i = 0; i < have; i++)
                into[i] = scratch[have - 1 - i];

        return have;
}

static positive into_exacts[] = {
    0, 1, 2, 9, 10, 11, 99, 100, 101, 999, 1000, 1001,
    9999, 10000, 65535, 65536, 99999, 100000, 999999, 1000000,
    99999999ull, 100000000ull, 100000001ull,
    999999999ull, 1000000000ull,
    4294967295ull, 4294967296ull,
    9999999999ull, 10000000000ull,
    999999999999999999ull, 1000000000000000000ull,
    9999999999999999999ull, 10000000000000000000ull,
    18446744073709551615ull,
};

#define INTO_EXACT_COUNT (sizeof(into_exacts) / sizeof(into_exacts[0]))

/* The size-class entries are asm-only (lib.c has no C declaration for them): the checks call them directly. */
positive positive_into_string_2_2(p8 address_to into, positive value);
positive positive_into_string_9_12(p8 address_to into, positive value);

fn check_into_one(positive value, positive offset, p8 guard)
{
        p8 address_to got;
        p8 address_to want;
        positive got_length;
        positive want_length;

        reference_fill(span_room, guard, sizeof(span_room));
        reference_fill(field, guard, sizeof(field));

        got = span_subject(offset);
        want = (p8 address_to)aligned_at(field) + SPAN_HEAD + offset;

        got_length = positive_into(got, value);
        want_length = reference_into(want, value);

        same("positive_into", "how many", got_length, want_length);
        same_bytes("positive_into", "the digits", got, want, want_length);

        // Nothing in front of the answer and nothing behind it. A routine
        // that terminates the string, or hands back the whole scratch, gets
        // the length right and is still wrong.
        same_guarded("positive_into", got, got_length, guard, 0);

        reference_fill(span_room, guard, sizeof(span_room));
        reference_fill(field, guard, sizeof(field));

        got = span_subject(offset);
        want = (p8 address_to)aligned_at(field) + SPAN_HEAD + offset;

        got_length = positive_into_string(got, value);
        want_length = reference_into(want, value);
        want[want_length] = end;

        same("positive_into_string", "how many", got_length, want_length);
        same_bytes("positive_into_string", "the string",
                   got, want, want_length + 1);

        same_guarded("positive_into_string", got, got_length, guard, 1);

        // The size-class entries: each is the generic routine for its class, so it is checked against the same
        // reference on the values of its class only (a caller that knows the class is the only one that may call it).
        if (value >= 10 && value <= 99)
        {
                reference_fill(span_room, guard, sizeof(span_room));
                reference_fill(field, guard, sizeof(field));
                got = span_subject(offset);
                want = (p8 address_to)aligned_at(field) + SPAN_HEAD + offset;
                got_length = positive_into_string_2_2(got, value);
                want_length = reference_into(want, value);
                want[want_length] = end;
                same("positive_into_string_2_2", "how many", got_length, want_length);
                same_bytes("positive_into_string_2_2", "the string", got, want, want_length + 1);
                same_guarded("positive_into_string_2_2", got, got_length, guard, 1);
        }
        if (value >= 100000000 && value <= 999999999999)
        {
                reference_fill(span_room, guard, sizeof(span_room));
                reference_fill(field, guard, sizeof(field));
                got = span_subject(offset);
                want = (p8 address_to)aligned_at(field) + SPAN_HEAD + offset;
                got_length = positive_into_string_9_12(got, value);
                want_length = reference_into(want, value);
                want[want_length] = end;
                same("positive_into_string_9_12", "how many", got_length, want_length);
                same_bytes("positive_into_string_9_12", "the string", got, want, want_length + 1);
                same_guarded("positive_into_string_9_12", got, got_length, guard, 1);
        }
}

fn check_bipolar_into_one(bipolar value, positive offset, p8 guard)
{
        p8 address_to got;
        p8 address_to want;
        positive got_length;
        positive want_length = 0;

        reference_fill(span_room, guard, sizeof(span_room));
        reference_fill(field, guard, sizeof(field));

        got = span_subject(offset);
        want = (p8 address_to)aligned_at(field) + SPAN_HEAD + offset;

        if (value < 0)
                want[want_length++] = '-';

        want_length += reference_into(want + want_length,
                                      value < 0
                                          ? (positive)0 - (positive)value
                                          : (positive)value);

        got_length = bipolar_into(got, value);

        same("bipolar_into", "how many", got_length, want_length);
        same_bytes("bipolar_into", "the field", got, want, want_length);

        same_guarded("bipolar_into", got, got_length, guard, 0);

        reference_fill(span_room, guard, sizeof(span_room));
        got = span_subject(offset);
        want[want_length] = end;

        got_length = bipolar_into_string(got, value);

        same("bipolar_into_string", "how many", got_length, want_length);
        same_bytes("bipolar_into_string", "the string",
                   got, want, want_length + 1);

        same_guarded("bipolar_into_string", got, got_length, guard, 1);
}

fn check_into()
{
        // A digit, a byte that is not one, a zero and a high byte: the four
        // ways a stray write shows up as something plausible.
        static p8 guards[] = {0x00, '0', '9', 'x', 0xff};

        for (positive g = 0; g < sizeof(guards); g++)
                for (positive offset = 0; offset < 8; offset++)
                {
                        for (positive e = 0; e < INTO_EXACT_COUNT; e++)
                                check_into_one(into_exacts[e], offset, guards[g]);

                        // And every power of ten either side, so each length
                        // the divide loop can produce is reached.
                        positive power = 1;

                        for (positive d = 0; d < 19; d++)
                        {
                                check_into_one(power - 1, offset, guards[g]);
                                check_into_one(power, offset, guards[g]);
                                check_into_one(power + 1, offset, guards[g]);
                                power *= 10;
                        }

                        for (positive r = 0; r < 24; r++)
                                check_into_one(next(), offset, guards[g]);

                        /*
                                The minimum signed value negates to its own
                                bits. These exercise both signed entry points,
                                every destination alignment, and the bytes on
                                either side of their contracts.
                        */
                        static bipolar values[] = {
                            bipolar_min, bipolar_min + 1, -1,
                            0, 1, bipolar_max,
                        };

                        for (positive i = 0;
                             i < sizeof(values) / sizeof(values[0]); i++)
                                check_bipolar_into_one(values[i], offset, guards[g]);
                }

        /*
                The direct table path has a finite domain, so cover all of it
                rather than sampling it: every unsigned value through 9999,
                every negative magnitude through 9999, and 10000 as the first
                value handed to the shared wide core. Alignment and guard
                rotate with the value, exercising every byte position without
                multiplying this sweep by another forty identical passes.
        */
        for (positive value = 0; value <= 10000; value++)
        {
                positive offset = value & 7;
                p8 guard = guards[value % (sizeof(guards) / sizeof(guards[0]))];

                check_into_one(value, offset, guard);
                if (value)
                        check_bipolar_into_one(-(bipolar)value, offset, guard);
        }
}

/*
        Human sizes have two contracts: the terminated bytes and the writer
        protocol. The old file helper sent an unscaled number as one digit
        run, an integer-scaled number as digits then suffix, and a fractional
        one as four single-byte calls. Capture both so folding through a
        buffer cannot silently coalesce a writer that observes boundaries.
*/
static p8 human_capture[16];
static positive human_used;
static positive human_calls;
static positive human_call_lengths[4];
static p8 human_call_first[4];
static p8 human_overflow;

fn human_writer(address_any data, positive length)
{
        p8 address_to bytes = (p8 address_to)data;

        if (human_calls < sizeof(human_call_lengths) /
                          sizeof(human_call_lengths[0]))
        {
                human_call_lengths[human_calls] = length;
                human_call_first[human_calls] = length ? bytes[0] : 0;
        }
        else
                human_overflow = true;

        human_calls++;

        if (length > sizeof(human_capture) - min(human_used,
                                                  sizeof(human_capture)))
        {
                human_overflow = true;
                return;
        }

        reference_copy(human_capture + human_used, bytes, length);
        human_used += length;
}

fn check_human_one(positive value, positive offset, p8 guard)
{
        reference_fill(span_room, guard, sizeof(span_room));
        reference_fill(field, guard, sizeof(field));

        p8 address_to got = span_subject(offset);
        p8 address_to want =
            (p8 address_to)aligned_at(field) + SPAN_HEAD + offset;
        positive got_length = positive_into_human_1024_string(got, value);
        positive want_length = reference_into_human_1024(want, value);

        same("positive_into_human_1024_string", "how many", got_length, want_length);
        same_bytes("positive_into_human_1024_string", "the terminated bytes",
                   got, want, want_length + 1);

        same_guarded("positive_into_human_1024_string", got, got_length, guard, 1);

        reference_fill(human_capture, guard, sizeof(human_capture));
        human_used = 0;
        human_calls = 0;
        human_overflow = false;

        positive_to_human_1024(human_writer, value);

        same("positive_to_human_1024", "did not exceed the capture",
             human_overflow, false);
        same("positive_to_human_1024", "how many bytes",
             human_used, want_length);
        same_bytes("positive_to_human_1024", "the bytes",
                   human_capture, want, want_length);

        p8 fractional = want_length == 4 && want[1] == '.';
        p8 scaled = want[want_length - 1] > '9';
        positive expected_calls = fractional ? 4 : (scaled ? 2 : 1);

        same("positive_to_human_1024", "how many writer calls",
             human_calls, expected_calls);

        if (fractional)
                for (positive call = 0; call < 4 && call < human_calls; call++)
                {
                        same("positive_to_human_1024",
                             "one byte in each fractional call",
                             human_call_lengths[call], 1);
                        same("positive_to_human_1024",
                             "fractional call order",
                             human_call_first[call], want[call]);
                }
        else if (scaled && human_calls >= 2)
        {
                same("positive_to_human_1024", "integer digit run",
                     human_call_lengths[0], want_length - 1);
                same("positive_to_human_1024", "integer first digit",
                     human_call_first[0], want[0]);
                same("positive_to_human_1024", "integer suffix length",
                     human_call_lengths[1], 1);
                same("positive_to_human_1024", "integer suffix byte",
                     human_call_first[1], want[want_length - 1]);
        }
        else if (human_calls)
        {
                same("positive_to_human_1024", "plain digit run",
                     human_call_lengths[0], want_length);
                same("positive_to_human_1024", "plain first digit",
                     human_call_first[0], want[0]);
        }
}

fn check_human_1024()
{
        static p8 guards[] = {0x00, '0', '9', 'x', 0xff};
        positive maximum = ~(positive)0;

        // Complete finite low domain, rotating every alignment and poison.
        for (positive value = 0; value <= 65535; value++)
                check_human_one(value, value & 15,
                                guards[value % sizeof(guards)]);

        positive divisor = 1024;

        for (positive unit = 1; unit <= 6; unit++)
        {
                check_human_one(divisor - 1, unit, guards[unit % sizeof(guards)]);
                check_human_one(divisor, unit + 1,
                                guards[(unit + 1) % sizeof(guards)]);
                check_human_one(divisor + 1, unit + 2,
                                guards[(unit + 2) % sizeof(guards)]);

                // The presentation changes at nine plus any remainder, and
                // the unit changes only at the next exact power.
                static positive multipliers[] = {9, 10};

                for (positive m = 0;
                     m < sizeof(multipliers) / sizeof(multipliers[0]); m++)
                {
                        positive edge = multipliers[m] * divisor;

                        check_human_one(edge - 1, m + unit,
                                        guards[(m + unit) % sizeof(guards)]);
                        check_human_one(edge, m + unit + 1,
                                        guards[(m + unit + 1) % sizeof(guards)]);
                        check_human_one(edge + 1, m + unit + 2,
                                        guards[(m + unit + 2) % sizeof(guards)]);
                }

                // Every tenth boundary from either side, at both ends of the
                // one-decimal range. Powers of two are not divisible by ten,
                // so the floor and its neighbours exercise the ceiling.
                for (positive tenth = 1; tenth < 10; tenth++)
                {
                        positive remainder = (tenth * divisor) / 10;

                        for (positive quotient = 1; quotient <= 8; quotient += 7)
                        {
                                positive edge = quotient * divisor + remainder;

                                check_human_one(edge - 1, tenth,
                                                guards[tenth % sizeof(guards)]);
                                check_human_one(edge, tenth + 1,
                                                guards[(tenth + 1) % sizeof(guards)]);
                                check_human_one(edge + 1, tenth + 2,
                                                guards[(tenth + 2) % sizeof(guards)]);
                        }
                }

                if (divisor <= maximum / 1024)
                        divisor *= 1024;
        }

        // The exact range where the former numerator wrapped, plus the whole
        // u64 ceiling. These must say 15E, 16E, 16E rather than 15E, 0E, 0E.
        divisor = (positive)1 << 60;
        check_human_one(15 * divisor, 13, 0xa5);
        check_human_one(15 * divisor + 1, 14, 0x5a);
        check_human_one(maximum - 1, 15, 0xff);
        check_human_one(maximum, 0, 0x00);

        for (positive i = 0; i < 65536; i++)
                check_human_one(next(), i & 15,
                                guards[(i >> 4) % sizeof(guards)]);
}

/*
        dd's nearest, ties-to-even byte formatter against the exact C body it
        replaced.  Eight output bytes are possible ("1023 KiB"), and the NUL
        is the ninth destination byte.

        Every architecture's normal verify lane includes this file.  Small
        values are complete, every pointer residue is crossed repeatedly,
        and each scale boundary, promotion boundary, exact half, and u64 edge
        is approached from both sides.
*/
#define HUMAN_NEAREST_DATA_MAX 8
#define HUMAN_NEAREST_CAPACITY 9
#define HUMAN_NEAREST_ROOM 64
#define HUMAN_NEAREST_HEAD 16

static positive reference_human_nearest(p8 address_to into, positive n,
                                        p8 binary)
{
        positive base = binary ? 1024 : 1000;
        positive amount = n, tenths = 0, rounding = 0, exponent = 0;
        static p8 letters[] = {0, 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y', 'R', 'Q'};
        positive used = 0, fraction = 0;
        p8 point = false;

        if (base <= amount)
        {
                do
                {
                        positive ten = (amount % base) * 10 + tenths;
                        positive two = (ten % base) * 2 + (rounding >> 1);

                        amount /= base;
                        tenths = ten / base;
                        rounding = two < base ? ((two + rounding) != 0)
                                              : 2 + (base < two + rounding);
                        exponent++;
                }
                while (base <= amount && exponent < 10);

                if (amount < 10)
                {
                        if (2 < rounding + (tenths & 1))
                        {
                                tenths++;
                                rounding = 0;

                                if (tenths == 10)
                                {
                                        amount++;
                                        tenths = 0;
                                }
                        }

                        if (amount < 10)
                        {
                                point = true;
                                fraction = tenths;
                                tenths = 0;
                                rounding = 0;
                        }
                }
        }

        if (5 < tenths + (0 < rounding + (amount & 1)))
        {
                amount++;

                if (amount == base && exponent < 10)
                {
                        exponent++;
                        point = true;
                        fraction = 0;
                        amount = 1;
                }
        }

        used += reference_into(into + used, amount);

        if (point)
        {
                into[used++] = '.';
                into[used++] = (p8)('0' + fraction);
        }

        into[used++] = ' ';

        if (exponent)
                into[used++] = !binary && exponent == 1 ? 'k' : letters[exponent];

        if (binary && exponent)
                into[used++] = 'i';

        into[used++] = 'B';
        into[used] = end;
        return used;
}

static fn check_human_nearest_one(positive value, p8 binary,
                                  positive offset, p8 guard)
{
        p8 got_room[HUMAN_NEAREST_ROOM];
        p8 want_room[HUMAN_NEAREST_ROOM];

        reference_fill(got_room, guard, sizeof(got_room));
        reference_fill(want_room, guard, sizeof(want_room));

        p8 address_to got = got_room + HUMAN_NEAREST_HEAD + (offset & 15);
        p8 address_to want = want_room + HUMAN_NEAREST_HEAD + (offset & 15);
        positive want_length = reference_human_nearest(want, value, binary);
        positive got_length = positive_into_human_nearest_string(got, value, binary);

        same("positive_into_human_nearest_string", "length", got_length,
             want_length);
        same("positive_into_human_nearest_string", "eight data bytes at most",
             want_length <= HUMAN_NEAREST_DATA_MAX, true);
        same("positive_into_human_nearest_string", "data plus NUL fit capacity",
             want_length + 1 <= HUMAN_NEAREST_CAPACITY, true);
        same_bytes("positive_into_human_nearest_string",
                   "bytes, NUL, and every guard", got_room, want_room,
                   sizeof(got_room));
}

fn check_human_nearest()
{
        static p8 guards[] = {0, 0x55, 0xff};
        static positive fixed[] = {
            0, 1, 9, 10, 99, 999, 1000, 1023, 1024, 9999,
            999499, 999500, 999501, 999999, 1000000,
            1023 * 1024, 1024 * 1024 - 1, 1024 * 1024,
            (positive)-2, (positive)-1,
        };

        // Complete low domain.  Its index rotates all sixteen destination
        // residues and every poison without multiplying identical cases.
        for (positive binary = 0; binary < 2; binary++)
                for (positive value = 0; value <= 65535; value++)
                        check_human_nearest_one(value, (p8)binary,
                                                value & 15,
                                                guards[(value >> 4) %
                                                       sizeof(guards)]);

        // Every named edge at every residue and poison.
        for (positive binary = 0; binary < 2; binary++)
                for (positive offset = 0; offset < 16; offset++)
                        for (positive g = 0; g < sizeof(guards); g++)
                                for (positive i = 0;
                                     i < sizeof(fixed) / sizeof(fixed[0]); i++)
                                        check_human_nearest_one(fixed[i],
                                                                (p8)binary,
                                                                offset,
                                                                guards[g]);

        for (positive binary = 0; binary < 2; binary++)
        {
                positive base = binary ? 1024 : 1000;
                positive power = 1;
                positive serial = 0;

                for (positive unit = 1; unit <= 6; unit++)
                {
                        if (power > (positive)-1 / base)
                                break;
                        power *= base;

                        // Four thousand either side crosses the unit edge,
                        // its nearest promotion, and many tenths/integer
                        // rounding transitions at a rotating residue.
                        for (positive delta = 0; delta <= 4096; delta++)
                        {
                                if (power >= delta)
                                        check_human_nearest_one(power - delta,
                                                                (p8)binary,
                                                                serial++ & 15,
                                                                guards[serial %
                                                                       sizeof(guards)]);
                                if (power <= (positive)-1 - delta)
                                        check_human_nearest_one(power + delta,
                                                                (p8)binary,
                                                                serial++ & 15,
                                                                guards[serial %
                                                                       sizeof(guards)]);
                        }

                        // Exact halves and both neighbours prove ties-to-even
                        // at both odd and even retained digits.
                        positive half = power / 2;
                        for (positive q = 1; q <= 20; q++)
                                if (q <= ((positive)-1 - half) / power)
                                {
                                        positive tie = q * power + half;
                                        check_human_nearest_one(tie - 1,
                                                                (p8)binary,
                                                                serial++ & 15,
                                                                guards[serial %
                                                                       sizeof(guards)]);
                                        check_human_nearest_one(tie,
                                                                (p8)binary,
                                                                serial++ & 15,
                                                                guards[serial %
                                                                       sizeof(guards)]);
                                        check_human_nearest_one(tie + 1,
                                                                (p8)binary,
                                                                serial++ & 15,
                                                                guards[serial %
                                                                       sizeof(guards)]);
                                }
                }
        }

        for (positive binary = 0; binary < 2; binary++)
                for (positive i = 0; i < 65536; i++)
                {
                        positive value = next();
                        check_human_nearest_one(value, (p8)binary, i & 15,
                                                guards[(i >> 4) % sizeof(guards)]);
                }
}

fn check_wait_status_code()
{
        // The kernel-owned portion is sixteen bits.  Exhausting it includes
        // every exit byte, every signal value, and every otherwise-irrelevant
        // combination of the two.
        for (positive raw = 0; raw <= 65535; raw++)
        {
                positive signal = raw & 0x7f;
                b32 want = signal ? (b32)(128 + signal)
                                  : (b32)((raw >> 8) & 0xff);

                same("wait_status_code", "every raw 16-bit status",
                     (positive)wait_status_code(raw), (positive)want);
        }

        // High garbage is not part of wait status and must not leak into the
        // public answer.
        for (positive i = 0; i < 4096; i++)
        {
                positive raw = next();
                positive signal = raw & 0x7f;
                b32 want = signal ? (b32)(128 + signal)
                                  : (b32)((raw >> 8) & 0xff);

                same("wait_status_code", "ignores bits above wait4 status",
                     (positive)wait_status_code(raw), (positive)want);
        }
}

static b32 reference_wait_status_code_base(positive raw, b32 signal_base)
{
        positive signal = raw & 0x7f;

        if (!signal)
                return (b32)((raw >> 8) & 0xff);

        b32 answer = signal_base + (b32)signal;

        if ((raw & 0x80) && (signal_base & 0x100))
                answer += 0x100;

        return answer;
}

/*
        Every kernel-owned status word under each public policy.  Core is
        deliberately crossed here rather than tested with only one signal:
        base zero and the shell's 128 ignore bit seven, while gawk's base 256
        reports it as another 256.  Repeating the complete low word under
        hostile high-bit masks proves no caller garbage reaches the answer.
*/
fn check_wait_status_code_base()
{
        static const b32 bases[] = {0, 128, 256};
        static const positive high_bits[] = {
            0x0000000100000000ull,
            0x8000000000000000ull,
            0xffffffffffff0000ull,
        };

        for (positive b = 0; b < sizeof(bases) / sizeof(bases[0]); b++)
                for (positive low = 0; low <= 65535; low++)
                {
                        b32 want = reference_wait_status_code_base(low, bases[b]);

                        same("wait_status_code_base", "every raw 16-bit status",
                             (positive)wait_status_code_base(low, bases[b]),
                             (positive)want);

                        for (positive h = 0;
                             h < sizeof(high_bits) / sizeof(high_bits[0]); h++)
                        {
                                positive raw = low | high_bits[h];

                                same("wait_status_code_base", "ignores every high-bit mask",
                                     (positive)wait_status_code_base(raw, bases[b]),
                                     (positive)want);
                        }
                }
}

/*
        A padded field is a writer protocol, not merely a byte result: every
        leading byte is its own call, the optional prefix is its own call, and
        all digits are the final call. These records prove that boundary and
        order while the guarded destination proves the bytes. Widths through
        forty cross every decimal length, every destination alignment, both
        padding alphabets, disabled padding, and absent/present prefixes. The
        finite 0..100000 converter domain is swept once more with those axes
        rotating, then deliberately large widths prove there is no
        width-sized stack allocation or hidden truncation.
*/
#define PADDED_ROOM 1280
#define PADDED_CALL_MAX 1100

static p8 padded_got_room[PADDED_ROOM];
static p8 padded_want_room[PADDED_ROOM];
static p8 address_to padded_output;
static positive padded_capacity;
static positive padded_used;
static positive padded_calls;
static positive padded_call_lengths[PADDED_CALL_MAX];
static p8 padded_call_first[PADDED_CALL_MAX];
static p8 padded_overflow;

fn padded_writer(address_any data, positive length)
{
        p8 address_to bytes = (p8 address_to)data;

        if (padded_calls < PADDED_CALL_MAX)
        {
                padded_call_lengths[padded_calls] = length;
                padded_call_first[padded_calls] = length ? bytes[0] : 0;
        }
        else
                padded_overflow = true;

        padded_calls++;

        if (length > padded_capacity - min(padded_used, padded_capacity))
        {
                padded_overflow = true;
                return;
        }

        reference_copy(padded_output + padded_used, bytes, length);
        padded_used += length;
}

/*
        writer_field is a callback protocol as much as a byte operation.  The
        capture records every boundary and its address, while the assembly
        shim deliberately destroys every general caller-saved register after
        the C capture returns.  A field implementation that keeps its body,
        count, pad, or writer in scratch registers cannot pass by accident.
*/
#define FIELD_ROOM 512
#define FIELD_CALL_MAX 320

static p8 field_room[FIELD_ROOM];
static p8 field_body_room[128];
static p8 field_string_room[128];
static p8 field_body_before[128];
static p8 address_to field_output;
static positive field_capacity;
static positive field_used;
static positive field_calls;
static positive field_call_lengths[FIELD_CALL_MAX];
static p8 field_call_first[FIELD_CALL_MAX];
static address_any field_call_data[FIELD_CALL_MAX];
static p8 field_overflow;

__attribute__((noinline, noclone, used))
fn field_capture(address_any data, positive length)
{
        p8 address_to bytes = (p8 address_to)data;

        if (field_calls < FIELD_CALL_MAX)
        {
                field_call_lengths[field_calls] = length;
                field_call_first[field_calls] = length ? bytes[0] : 0;
                field_call_data[field_calls] = data;
        }
        else
                field_overflow = true;

        field_calls++;

        if (!length || length > field_capacity - min(field_used, field_capacity))
        {
                field_overflow = true;
                return;
        }

        reference_copy(field_output + field_used, bytes, length);
        field_used += length;
}

fn field_writer(address_any data, positive length);

#if X64
__asm__(
    ASM_SECTION
    ASM_FUNC(field_writer)
    "sub $8, %rsp\n   call field_capture\n   add $8, %rsp\n"
    "mov $1, %eax\n   mov $2, %ecx\n   mov $3, %edx\n"
    "mov $4, %esi\n   mov $5, %edi\n   mov $6, %r8d\n"
    "mov $7, %r9d\n   mov $8, %r10d\n   mov $9, %r11d\n"
    ASM_RET
    ASM_END(field_writer));
#elif ARM64
__asm__(
    ASM_SECTION
    ASM_FUNC(field_writer)
    "stp x29, x30, [sp, #-16]!\n   mov x29, sp\n   bl field_capture\n"
    "ldp x29, x30, [sp], #16\n"
    "mov x0, #1\n   mov x1, #2\n   mov x2, #3\n   mov x3, #4\n"
    "mov x4, #5\n   mov x5, #6\n   mov x6, #7\n   mov x7, #8\n"
    "mov x8, #9\n   mov x9, #10\n   mov x10, #11\n   mov x11, #12\n"
    "mov x12, #13\n   mov x13, #14\n   mov x14, #15\n   mov x15, #16\n"
    "mov x16, #17\n   mov x17, #18\n   mov x18, #19\n"
    ASM_RET
    ASM_END(field_writer));
#else
__asm__(
    ASM_SECTION
    ASM_FUNC(field_writer)
    "addi sp, sp, -16\n   sd ra, 8(sp)\n   call field_capture\n"
    "ld ra, 8(sp)\n   addi sp, sp, 16\n"
    "li t0, 1\n   li t1, 2\n   li t2, 3\n   li t3, 4\n"
    "li t4, 5\n   li t5, 6\n   li t6, 7\n"
    "li a0, 8\n   li a1, 9\n   li a2, 10\n   li a3, 11\n"
    "li a4, 12\n   li a5, 13\n   li a6, 14\n   li a7, 15\n"
    ASM_RET
    ASM_END(field_writer));
#endif

fn check_field_case(string_address name, address_any data, positive length,
                    positive width, p8 pad, p8 left, positive output_residue,
                    p8 string_form)
{
        positive padding = width > length ? width - length : 0;
        positive total = length + padding;
        positive body_call = left ? 0 : padding;
        p8 address_to bytes = (p8 address_to)data;

        if (length)
                reference_copy(field_body_before, data, length);

        reference_fill(field_room, 0xa5, sizeof(field_room));
        field_output = field_room + 16 + output_residue;
        field_capacity = total;
        field_used = field_calls = 0;
        field_overflow = false;

        if (string_form)
                string_to_field(field_writer, (string_address)data,
                                width, (b8)pad, left);
        else
                writer_field(field_writer, data, length,
                             width, (b8)pad, left);

        same(name, "capture stayed in bounds", field_overflow, false);
        same(name, "total byte count", field_used, total);
        same(name, "exact callback count", field_calls,
             padding + (length ? 1 : 0));

        for (positive guard = 1; guard <= 8; guard++)
        {
                same(name, "guard before output",
                     field_output[-(bipolar)guard], 0xa5);
                same(name, "guard after output",
                     field_output[total + guard - 1], 0xa5);
        }

        for (positive at = 0; at < total; at++)
        {
                p8 in_body = left ? at < length : at >= padding;
                positive body_at = left ? at : at - padding;
                p8 want = in_body ? field_body_before[body_at] : pad;

                same(name, "byte order", field_output[at], want);
        }

        for (positive call = 0; call < field_calls && call < FIELD_CALL_MAX;
             call++)
        {
                p8 is_body = length && call == body_call;

                same(name, "callback length", field_call_lengths[call],
                     is_body ? length : 1);
                same(name, "callback first byte", field_call_first[call],
                     is_body ? field_body_before[0] : pad);

                if (is_body)
                        same(name, "body callback address",
                             (positive)field_call_data[call], (positive)data);
        }

        if (length)
                same_bytes(name, "input remains unchanged",
                           (b8 address_to)data,
                           (b8 address_to)field_body_before, length);
}

fn check_writer_fields()
{
        // Counted input is deliberately binary and not NUL terminated. Every
        // small length, minimum width, pointer residue, alignment direction,
        // and a rotating full-byte pad reaches the hostile callback.
        for (positive length = 0; length <= 64; length++)
                for (positive width = 0; width <= 64; width++)
                        for (positive residue = 0; residue < 16; residue++)
                                for (positive left = 0; left < 2; left++)
                                {
                                        p8 address_to data =
                                            field_body_room + 16 + residue;
                                        p8 pad = (p8)(length * 67 + width * 29 +
                                                      residue * 17 + left * 131);

                                        reference_fill(field_body_room, 0x7d,
                                                       sizeof(field_body_room));
                                        for (positive i = 0; i < length; i++)
                                                data[i] = (p8)(i * 43 + length * 11 + 1);
                                        if (length > 2)
                                                data[1] = 0;
                                        data[length] = 0x7e;

                                        check_field_case(
                                            "writer_field", length ? data :
                                                (address_any)(positive)1,
                                            length, width, pad, (p8)left,
                                            (length * 5 + width * 3 + residue) & 15,
                                            false);
                                }

        // Every possible padding byte, explicitly including NUL and both
        // halves of the signed-char range.
        for (positive pad = 0; pad <= 255; pad++)
        {
                p8 address_to data = field_body_room + 16 + (pad & 15);

                data[0] = 0;
                data[1] = 0x80;
                data[2] = 0xff;
                data[3] = 0x7e;
                check_field_case("writer_field pad byte", data, 3, 9,
                                 (p8)pad, (p8)(pad >> 7), pad & 15, false);
        }

        // The string wrapper crosses the complete small length/width plane;
        // rotating residues and pad values keeps this focused on measuring
        // once and routing the identical core rather than repeating the much
        // larger raw-field alignment matrix above.
        for (positive length = 0; length <= 64; length++)
                for (positive width = 0; width <= 64; width++)
                        for (positive left = 0; left < 2; left++)
                        {
                                positive residue =
                                    (length * 7 + width * 11 + left) & 15;
                                p8 address_to text =
                                    field_string_room + 16 + residue;
                                p8 pad = (p8)(length * 31 + width * 73 + left);

                                reference_fill(field_string_room, 0x6d,
                                               sizeof(field_string_room));
                                for (positive i = 0; i < length; i++)
                                        text[i] = (p8)(1 + (i * 47 + length) % 255);
                                text[length] = end;

                                check_field_case("string_to_field", text, length,
                                                 width, pad, (p8)left,
                                                 (residue * 9 + width) & 15, true);
                        }

        // Wider than the exhaustive plane, while remaining below the fixed
        // transcript capacity. This rejects a small-field-only counter.
        p8 address_to data = field_body_room + 19;
        data[0] = 0x80;
        data[1] = 0;
        data[2] = 0xff;
        check_field_case("writer_field wide", data, 3, 257, 0,
                         false, 15, false);
        check_field_case("writer_field wide", data, 3, 257, 0xff,
                         true, 0, false);
}

fn check_base_field()
{
        static positive values[] = {0, 1, 7, 8, 9, 15, 16, 255,
                                    0xffffffffffffffffull};
        static positive widths[] = {0, 1, 2, 5, 24, 257};
        static bipolar precisions[] = {-1, 0, 1, 4, 25};

        // writer_fill's observable contract is exactly one one-byte call per
        // requested byte, including large counts, and no call for zero.
        for (positive count = 0; count <= 257; count += count < 2 ? 1 : 255)
        {
                padded_output = padded_got_room;
                padded_capacity = sizeof(padded_got_room);
                padded_used = padded_calls = 0;
                padded_overflow = false;
                writer_fill(padded_writer, count, 0xa5);
                same("writer_fill", "capture", padded_overflow, false);
                same("writer_fill", "bytes", padded_used, count);
                same("writer_fill", "calls", padded_calls, count);
                for (positive i = 0; i < count; i++)
                {
                        same("writer_fill", "call length", padded_call_lengths[i], 1);
                        same("writer_fill", "call byte", padded_call_first[i], 0xa5);
                }
        }

        for (positive vi = 0; vi < sizeof(values) / sizeof(values[0]); vi++)
                for (positive base = 2; base <= 36; base++)
                        for (positive wi = 0; wi < sizeof(widths) / sizeof(widths[0]); wi++)
                                for (positive pi = 0;
                                     pi < sizeof(precisions) / sizeof(precisions[0]); pi++)
                                        for (positive flags = 0; flags < 8; flags++)
                                        {
                                                p8 digits[72];
                                                positive upper = flags & 1;
                                                positive left = (flags >> 1) & 1;
                                                positive zero = (flags >> 2) & 1;
                                                positive sign = (vi & 1) ? '+' : 0;
                                                positive packed_prefix_length = vi % 4;
                                                positive prefix_length = min(packed_prefix_length, 2);
                                                positive style = sign | ((positive)'0' << 8) |
                                                    ((positive)'x' << 16) |
                                                    (packed_prefix_length << 24) | (upper << 26) |
                                                    (left << 27) | (zero << 28);
                                                positive length = reference_into_base(
                                                    digits, values[vi], base, upper);
                                                positive zeros = 0;
                                                bipolar precision = precisions[pi];
                                                positive head = (sign != 0) + prefix_length;

                                                if (precision >= 0 &&
                                                    (positive)precision > length)
                                                        zeros = (positive)precision - length;
                                                if (zero && !left && precision < 0 &&
                                                    widths[wi] > head + length)
                                                        zeros = widths[wi] - head - length;

                                                positive field = head + zeros + length;
                                                positive spaces = widths[wi] > field
                                                                      ? widths[wi] - field
                                                                      : 0;
                                                p8 want[400];
                                                positive made = 0;
                                                if (!left)
                                                        while (made < spaces)
                                                                want[made++] = ' ';
                                                if (sign)
                                                        want[made++] = (p8)sign;
                                                if (prefix_length)
                                                        want[made++] = '0';
                                                if (prefix_length == 2)
                                                        want[made++] = 'x';
                                                for (positive i = 0; i < zeros; i++)
                                                        want[made++] = '0';
                                                reference_copy(want + made, digits, length);
                                                made += length;
                                                if (left)
                                                        for (positive i = 0; i < spaces; i++)
                                                                want[made++] = ' ';

                                                padded_output = padded_got_room;
                                                padded_capacity = sizeof(padded_got_room);
                                                padded_used = padded_calls = 0;
                                                padded_overflow = false;
                                                positive_to_base_field(
                                                    padded_writer, values[vi], base,
                                                    widths[wi], precision, style);
                                                same("positive_to_base_field", "capture",
                                                     padded_overflow, false);
                                                same("positive_to_base_field", "bytes",
                                                     padded_used, made);
                                                same_bytes("positive_to_base_field", "field",
                                                           padded_got_room, want, made);
                                                positive call = 0;
                                                positive leading = left ? 0 : spaces;
                                                for (positive i = 0; i < leading; i++, call++)
                                                {
                                                        same("positive_to_base_field", "space call",
                                                             padded_call_lengths[call], 1);
                                                        same("positive_to_base_field", "space byte",
                                                             padded_call_first[call], ' ');
                                                }
                                                if (sign)
                                                {
                                                        same("positive_to_base_field", "sign call",
                                                             padded_call_lengths[call], 1);
                                                        same("positive_to_base_field", "sign byte",
                                                             padded_call_first[call++], sign);
                                                }
                                                if (prefix_length)
                                                {
                                                        same("positive_to_base_field", "prefix call",
                                                             padded_call_lengths[call], prefix_length);
                                                        same("positive_to_base_field", "prefix byte",
                                                             padded_call_first[call++], '0');
                                                }
                                                for (positive i = 0; i < zeros; i++, call++)
                                                {
                                                        same("positive_to_base_field", "zero call",
                                                             padded_call_lengths[call], 1);
                                                        same("positive_to_base_field", "zero byte",
                                                             padded_call_first[call], '0');
                                                }
                                                same("positive_to_base_field", "digit call",
                                                     padded_call_lengths[call], length);
                                                same("positive_to_base_field", "digit byte",
                                                     padded_call_first[call++], digits[0]);
                                                for (positive i = 0; i < (left ? spaces : 0);
                                                     i++, call++)
                                                        same("positive_to_base_field", "tail space call",
                                                             padded_call_lengths[call], 1);
                                                same("positive_to_base_field", "call count",
                                                     padded_calls, call);
                                        }

        static positive invalid[] = {0, 1, 37, (positive)-1};
        for (positive i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        {
                padded_output = padded_got_room;
                padded_capacity = sizeof(padded_got_room);
                padded_used = padded_calls = 0;
                padded_overflow = false;
                positive_to_base_field(padded_writer, 7, invalid[i], 20, 30,
                                       '+' | ((positive)3 << 24) |
                                           ((positive)1 << 28));
                same("positive_to_base_field", "invalid base calls", padded_calls, 0);
                same("positive_to_base_field", "invalid base bytes", padded_used, 0);
        }
}

fn check_padded_one(positive value, positive width, p8 pad, p8 prefix,
                    positive offset, p8 guard)
{
        reference_fill(padded_got_room, guard, sizeof(padded_got_room));
        reference_fill(padded_want_room, guard, sizeof(padded_want_room));

        p8 address_to got =
            (p8 address_to)aligned_at(padded_got_room) + 32 + offset;
        p8 address_to want =
            (p8 address_to)aligned_at(padded_want_room) + 32 + offset;
        p8 digits[24];
        positive digit_length = reference_into(digits, value);
        positive field_length = digit_length + (prefix != 0);
        positive padding = pad && width > field_length ? width - field_length : 0;
        positive wanted = 0;

        for (positive i = 0; i < padding; i++)
                want[wanted++] = pad;

        if (prefix)
                want[wanted++] = prefix;

        reference_copy(want + wanted, digits, digit_length);
        wanted += digit_length;

        padded_output = got;
        padded_capacity = sizeof(padded_got_room) -
                          (positive)(got - padded_got_room) - 8;
        padded_used = 0;
        padded_calls = 0;
        padded_overflow = false;

        positive_to_padded(padded_writer, value, width, pad, prefix);

        same("positive_to_padded", "did not exceed the capture",
             padded_overflow, false);
        same("positive_to_padded", "how many bytes", padded_used, wanted);
        same_bytes("positive_to_padded", "the field", got, want, wanted);

        positive expected_calls = padding + (prefix != 0) + 1;
        same("positive_to_padded", "how many writer calls",
             padded_calls, expected_calls);

        positive call = 0;

        for (; call < padding && call < padded_calls; call++)
        {
                same("positive_to_padded", "one byte in each pad call",
                     padded_call_lengths[call], 1);
                same("positive_to_padded", "the pad call byte",
                     padded_call_first[call], pad);
        }

        if (prefix && call < padded_calls)
        {
                same("positive_to_padded", "one byte in the prefix call",
                     padded_call_lengths[call], 1);
                same("positive_to_padded", "the prefix call byte",
                     padded_call_first[call], prefix);
                call++;
        }

        if (call < padded_calls)
        {
                same("positive_to_padded", "all digits in the final call",
                     padded_call_lengths[call], digit_length);
                same("positive_to_padded", "the first digit in the final call",
                     padded_call_first[call], digits[0]);
                call++;
        }

        same("positive_to_padded", "no calls after the digits",
             call, padded_calls);

        for (positive back = 1; back <= 8; back++)
                same("positive_to_padded", "in front of the field",
                     (positive)*(got - back), (positive)guard);

        for (positive after = 0; after < 8; after++)
                same("positive_to_padded", "behind the field",
                     (positive)got[wanted + after], (positive)guard);
}

fn check_padded()
{
        static p8 guards[] = {0x00, '0', '9', 'x', 0xff};
        static p8 pads[] = {0, ' ', '0', '#'};
        static p8 prefixes[] = {0, '-', '+'};

        // Every low value, with every other finite axis rotating through it.
        for (positive value = 0; value <= 100000; value++)
                check_padded_one(
                    value, value % 41,
                    pads[(value / 41) % (sizeof(pads) / sizeof(pads[0]))],
                    prefixes[(value / 164) %
                             (sizeof(prefixes) / sizeof(prefixes[0]))],
                    value & 15,
                    guards[(value / 492) %
                           (sizeof(guards) / sizeof(guards[0]))]);

        // Full width/alignment/pad/prefix cross at every selected edge.
        for (positive e = 0; e < INTO_EXACT_COUNT; e++)
                for (positive width = 0; width <= 40; width++)
                        for (positive offset = 0; offset < 16; offset++)
                                for (positive p = 0;
                                     p < sizeof(pads) / sizeof(pads[0]); p++)
                                        for (positive x = 0;
                                             x < sizeof(prefixes) /
                                                     sizeof(prefixes[0]); x++)
                                                check_padded_one(
                                                    into_exacts[e], width,
                                                    pads[p], prefixes[x], offset,
                                                    guards[(e + width + offset + p + x) %
                                                           (sizeof(guards) /
                                                            sizeof(guards[0]))]);

        // Bounded but deliberately much wider than any in-tree field.
        static positive wide[] = {64, 127, 255, 1024};
        static positive wide_values[] = {0, 9, 10, positive_max};

        for (positive w = 0; w < sizeof(wide) / sizeof(wide[0]); w++)
                for (positive v = 0;
                     v < sizeof(wide_values) / sizeof(wide_values[0]); v++)
                        for (positive p = 0;
                             p < sizeof(pads) / sizeof(pads[0]); p++)
                                for (positive x = 0;
                                     x < sizeof(prefixes) / sizeof(prefixes[0]); x++)
                                        check_padded_one(
                                            wide_values[v], wide[w], pads[p],
                                            prefixes[x], (w + v + p + x) & 15,
                                            guards[(w + v + p + x) %
                                                   (sizeof(guards) /
                                                    sizeof(guards[0]))]);
}

/*
        The buffer form has a different contract from the writer form above:
        the complete field is contiguous and one return value owns its exact
        length. Cross small values with rotating widths first, then cross all
        integer length edges with every alignment, poison, pad and deliberately
        wide minimum. The guards prove no terminator, truncation or width-sized
        overrun; positive_max proves the twenty-digit move.
*/
#define INTO_PADDED_ROOM 1280

static p8 into_padded_got_room[INTO_PADDED_ROOM];
static p8 into_padded_want_room[INTO_PADDED_ROOM];

fn check_into_padded_one(positive value, positive width, p8 pad,
                         positive offset, p8 guard)
{
        reference_fill(into_padded_got_room, guard,
                       sizeof(into_padded_got_room));
        reference_fill(into_padded_want_room, guard,
                       sizeof(into_padded_want_room));

        p8 address_to got =
            (p8 address_to)aligned_at(into_padded_got_room) + 32 + offset;
        p8 address_to want =
            (p8 address_to)aligned_at(into_padded_want_room) + 32 + offset;
        p8 digits[24];
        positive digit_length = reference_into(digits, value);
        positive padding = pad && width > digit_length
                               ? width - digit_length
                               : 0;
        positive wanted = padding + digit_length;

        reference_fill(want, pad, padding);
        reference_copy(want + padding, digits, digit_length);

        positive length = positive_into_padded(got, value, width, pad);

        same("positive_into_padded", "how many", length, wanted);
        same_bytes("positive_into_padded", "the field", got, want, wanted);

        same_guarded("positive_into_padded", got, wanted, guard, 0);
}

fn check_into_padded()
{
        static p8 guards[] = {0x00, '0', '9', 'x', 0xff};
        static p8 pads[] = {0, '0', ' ', '#'};
        static positive widths[] = {0, 1, 2, 6, 9, 19, 20,
                                    21, 32, 64, 127, 255, 1024};

        for (positive value = 0; value <= 100000; value++)
                check_into_padded_one(
                    value, value % 41,
                    pads[(value / 41) % (sizeof(pads) / sizeof(pads[0]))],
                    value & 15,
                    guards[(value / 164) %
                           (sizeof(guards) / sizeof(guards[0]))]);

        // Dense coverage of both bounded pair-emission lanes, including
        // values far above the exhaustive small sweep.
        for (positive r = 0; r < 10000; r++)
        {
                positive value6 = next() % 1000000;
                positive value9 = next() % 1000000000;

                check_into_padded_one(value6, 6, '0', r & 15,
                                      guards[r % sizeof(guards)]);
                check_into_padded_one(value9, 9, '0', (r + 7) & 15,
                                      guards[(r + 1) % sizeof(guards)]);
        }

        for (positive e = 0; e < INTO_EXACT_COUNT; e++)
                for (positive w = 0; w < sizeof(widths) / sizeof(widths[0]); w++)
                        for (positive offset = 0; offset < 16; offset++)
                                for (positive p = 0;
                                     p < sizeof(pads) / sizeof(pads[0]); p++)
                                        for (positive g = 0;
                                             g < sizeof(guards) / sizeof(guards[0]); g++)
                                                check_into_padded_one(
                                                    into_exacts[e], widths[w],
                                                    pads[p], offset, guards[g]);

        // Every width the any-pad lane takes (two to sixteen; above that, and
        // for a value past thirty two bits, it calls positive_into), on both
        // sides of every power of ten, so a value that just fits and one that just does not are
        // both asked of each width with every pad.
        for (positive w = 2; w <= 17; w++)
                for (positive k = 0, power = 1; k < 20; k++, power *= 10)
                        for (positive d = 0; d < 3; d++)
                                for (positive p = 0;
                                     p < sizeof(pads) / sizeof(pads[0]); p++)
                                        check_into_padded_one(
                                            power - 1 + d, w, pads[p],
                                            (w + k + d) & 15,
                                            guards[(w + k + d + p) % sizeof(guards)]);

        for (positive r = 0; r < 20000; r++)
        {
                positive w = 2 + r % 16;
                positive power = 1;

                for (positive k = 0; k < w; k++)
                        power *= 10;

                check_into_padded_one(next() % power, w,
                                      pads[1 + r % 3], r & 15,
                                      guards[r % sizeof(guards)]);
        }
}

fn check_into_pair()
{
        static p8 guards[] = {0x00, '0', '9', 'x', 0xff};
        p8 room[64];

        for (positive value = 0; value < 100; value++)
                for (positive offset = 0; offset < 16; offset++)
                        for (positive g = 0;
                             g < sizeof(guards) / sizeof(guards[0]); g++)
                        {
                                reference_fill(room, guards[g], sizeof(room));
                                p8 address_to got =
                                    (p8 address_to)aligned_at(room) + 16 + offset;
                                positive length = positive_into_pair(got, value);

                                same("positive_into_pair", "how many", length, 2);
                                same("positive_into_pair", "tens", got[0],
                                     '0' + value / 10);
                                same("positive_into_pair", "ones", got[1],
                                     '0' + value % 10);

                                same_guarded("positive_into_pair", got, 2, guards[g], 0);
                        }
}

/*
        Every power fast path, every conversion base the tree uses, and the
        whole public 2..36 contract around it. Invalid bases return zero and
        leave the destination untouched.

        The 16-bit value domain is finite enough to exhaust for octal,
        decimal and hexadecimal. Alignment and poison rotate through that
        sweep, then every alignment/poison pair is crossed with zero,
        positive_max and either side of every power of each tree base. The
        remaining bases get both alphabets, their UINT64 edge, and a repeatable
        spread. Together those catch a wrong digit count, an uppercase leak,
        an assumed alignment and either kind of stray terminator/padding byte.
*/
static p8 base_got_room[128];
static p8 base_want_room[128];

fn check_into_base_one(positive value, positive base, p8 upper,
                       positive offset, p8 guard)
{
        reference_fill(base_got_room, guard, sizeof(base_got_room));
        reference_fill(base_want_room, guard, sizeof(base_want_room));

        p8 address_to got = (p8 address_to)aligned_at(base_got_room) + 16 + offset;
        p8 address_to want = (p8 address_to)aligned_at(base_want_room) + 16 + offset;
        positive got_length = positive_into_base(got, value, base, upper);
        positive want_length = reference_into_base(want, value, base, upper);

        same("positive_into_base", "how many", got_length, want_length);
        same_bytes("positive_into_base", "the digits", got, want, want_length);

        same_guarded("positive_into_base", got, got_length, guard, 0);
}

fn check_into_base()
{
        static positive tree_bases[] = {2, 4, 8, 10, 16, 32};
        static positive invalid_bases[] = {0, 1, 37, ~(positive)0};
        static p8 guards[] = {0x00, '0', '9', 'x', 0xff};
        positive maximum = ~(positive)0;

        for (positive b = 0;
             b < sizeof(invalid_bases) / sizeof(invalid_bases[0]); b++)
                for (p8 upper = false; upper <= true; upper++)
                        for (positive value = 0; value <= 1; value++)
                                for (positive offset = 0; offset < 8; offset++)
                                {
                                        reference_fill(base_got_room, 0xa5,
                                                       sizeof(base_got_room));
                                        p8 address_to got =
                                            (p8 address_to)aligned_at(base_got_room) +
                                            16 + offset;
                                        positive length = positive_into_base(
                                            got, value ? maximum : 0,
                                            invalid_bases[b], upper);

                                        same("positive_into_base", "invalid length",
                                             length, 0);

                                        for (positive i = 0;
                                             i < sizeof(base_got_room); i++)
                                                same("positive_into_base",
                                                     "invalid base wrote nothing",
                                                     base_got_room[i], 0xa5);
                                }

        // Complete small-integer domain for every in-tree base and alphabet.
        for (positive b = 0; b < sizeof(tree_bases) / sizeof(tree_bases[0]); b++)
                for (p8 upper = false; upper <= true; upper++)
                        for (positive value = 0; value <= 65535; value++)
                                check_into_base_one(
                                    value, tree_bases[b], upper,
                                    (value + tree_bases[b] + upper) & 7,
                                    guards[(value + b + upper) % sizeof(guards)]);

        // Full alignment/guard cross at every length transition and UINT64.
        for (positive b = 0; b < sizeof(tree_bases) / sizeof(tree_bases[0]); b++)
                for (p8 upper = false; upper <= true; upper++)
                        for (positive offset = 0; offset < 8; offset++)
                                for (positive g = 0; g < sizeof(guards); g++)
                                {
                                        positive base = tree_bases[b];
                                        positive power = 1;

                                        check_into_base_one(maximum, base, upper,
                                                            offset, guards[g]);
                                        check_into_base_one(maximum - 1, base, upper,
                                                            offset, guards[g]);

                                        while (1)
                                        {
                                                check_into_base_one(power - 1, base, upper,
                                                                    offset, guards[g]);
                                                check_into_base_one(power, base, upper,
                                                                    offset, guards[g]);
                                                check_into_base_one(power + 1, base, upper,
                                                                    offset, guards[g]);

                                                if (power > maximum / base)
                                                        break;

                                                power *= base;
                                        }
                                }

        // The reusable surface, not only its current consumers.
        for (positive base = 2; base <= 36; base++)
                for (p8 upper = false; upper <= true; upper++)
                {
                        check_into_base_one(maximum, base, upper,
                                            base & 7, guards[base % sizeof(guards)]);

                        for (positive r = 0; r < 128; r++)
                        {
                                positive value = next();

                                check_into_base_one(
                                    value, base, upper, (value + r) & 7,
                                    guards[(value + base + upper) % sizeof(guards)]);
                        }
                }
}

/*
        The bounded copy that says where it ended.

        The destination is filled with a byte that is not a terminator and the
        bytes past the answer are checked, because the failure this can have
        and still look right is padding: strncpy zeroes out to the bound, and a
        routine that did that here would answer the right end and flatten
        whatever the caller had already written after it.

        The source is built with more bytes after its terminator, at eight
        offsets, so a copy that reads a word past the end takes them.
*/
static p8 copy_room[1024];
static p8 copy_twin[1024];

p8 address_to reference_copy_max_end(p8 address_to into, string_address source,
                                     positive bound)
{
        positive n = 0;

        while (n < bound && source[n])
        {
                into[n] = source[n];
                n++;
        }

        into[n] = end;

        return into + n;
}

fn check_copy_max_end()
{
        static p8 fillers[] = {0x00, 'z', 0xff, '/'};

        for (positive f = 0; f < sizeof(fillers); f++)
                for (positive offset = 0; offset < 8; offset++)
                        for (positive length = 0; length <= 40; length++)
                                for (positive b = 0; b < SPAN_BOUND_COUNT; b++)
                                {
                                        positive bound = span_bounds[b];
                                        p8 address_to source;
                                        p8 address_to got;
                                        p8 address_to want;
                                        p8 address_to got_end;
                                        p8 address_to want_end;

                                        reference_fill(span_room, 'S', sizeof(span_room));
                                        reference_fill(copy_room, fillers[f],
                                                       sizeof(copy_room));
                                        reference_fill(copy_twin, fillers[f],
                                                       sizeof(copy_twin));

                                        source = span_subject(offset);

                                        for (positive i = 0; i < length; i++)
                                                source[i] = (p8)('a' + i % 26);

                                        // The terminator, and then more bytes
                                        // that are not one: a read past it
                                        // takes them and the answer grows.
                                        source[length] = end;

                                        got = (p8 address_to)aligned_at(copy_room) +
                                              SPAN_HEAD + ((offset + 5) & 7);
                                        want = (p8 address_to)aligned_at(copy_twin) +
                                               SPAN_HEAD + ((offset + 5) & 7);

                                        got_end = string_copy_max_end(got, source, bound);
                                        want_end = reference_copy_max_end(want, source,
                                                                          bound);

                                        same("string_copy_max_end", "the end",
                                             (positive)(got_end - got),
                                             (positive)(want_end - want));

                                        // The bytes written, the terminator,
                                        // and eight past it that must not have
                                        // been touched.
                                        same_bytes("string_copy_max_end", "the bytes",
                                                   got, want,
                                                   (positive)(want_end - want) + 9);

                                        for (positive back = 1; back <= 8; back++)
                                                same("string_copy_max_end",
                                                     "in front of it",
                                                     (positive)*(got - back),
                                                     (positive)fillers[f]);
                                }
}


fn check_hostile_neighbours()
{
        // The byte hunted for, and so also the byte written in front of the
        // string and behind its terminator.
        static p8 hunted[] = {0x00, 0x01, 0x02, 0x40, 0x7f, 0x80,
                              0xa5, 0xfe, 0xff, 'a'};

        hunt_byte or_end =
            verify_or_end_public ? verify_or_end_public : verify_or_end_libc;
        hunt_bounded first_max =
            verify_first_max_public ? verify_first_max_public : verify_first_max_libc;
        hunt_byte last_or_end =
            verify_last_or_end_public ? verify_last_or_end_public
                                      : verify_last_or_end_libc;
        hunt_memory memory_first =
            verify_memory_first_public ? verify_memory_first_public
                                       : verify_memory_first_libc;
        measure_bounded length_max =
            verify_length_max_public ? verify_length_max_public
                                     : verify_length_max_libc;
        compare_bounded compare_max =
            verify_compare_max_public ? verify_compare_max_public
                                      : verify_compare_max_libc;

        if (!or_end)
                absent("string_first_of_or_end");
        if (!first_max)
                absent("string_first_of_max");
        if (!last_or_end)
                absent("string_last_of_or_end");
        if (!memory_first)
                absent("memory_first_of");
        if (!length_max)
                absent("string_length_max");
        if (!compare_max)
                absent("string_compare_max");

        for (positive h = 0; h < sizeof(hunted); h++)
        {
                p8 byte = hunted[h];

                for (positive offset = 0; offset < 8; offset++)
                        for (positive size = 0; size <= 24; size++)
                                for (positive planted = 0; planted < 2; planted++)
                                {
                                        string_address text;
                                        string_address twin;

                                        if (planted && !size)
                                                continue;

                                        reference_fill(field, byte, sizeof(field));
                                        reference_fill(field_twin, byte,
                                                       sizeof(field_twin));

                                        text = aligned_at(field) + 64 + offset;

                                        // The other string of a compare starts
                                        // at a different distance into its
                                        // word, so the two are never in step.
                                        twin = aligned_at(field_twin) + 64 +
                                               ((offset + 3) & 7);

                                        // Beginning with the next value up
                                        // from the byte in front of it, which
                                        // is the pair the borrow lies about.
                                        for (positive i = 0; i < size; i++)
                                        {
                                                p8 value = (p8)(byte + 1 + (i % 3));

                                                text[i] = value ? value : 1;
                                        }

                                        text[size] = 0;

                                        // Once with the hunted byte nowhere in
                                        // the string, so the answer has to be
                                        // nothing, and once with it at the far
                                        // end, so the answer has to be found
                                        // past the word the lie is in.
                                        if (planted)
                                                text[size - 1] = byte;

                                        memory_copy_apart(twin, text, size + 1);

                                        same("string_length", "hostile neighbours",
                                             string_length(text),
                                             reference_length(text));

                                        same("string_first_of", "hostile neighbours",
                                             (positive)string_first_of(text, byte),
                                             (positive)reference_first_of(text, byte));

                                        if (size)
                                                same("string_first_of", "hostile and present",
                                                     (positive)string_first_of(text, text[0]),
                                                     (positive)reference_first_of(text, text[0]));

                                        same("string_last_of", "hostile neighbours",
                                             (positive)string_last_of(text, byte),
                                             (positive)reference_string_last_of(text, byte));

                                        if (or_end)
                                                same("string_first_of_or_end", "hostile neighbours",
                                                     (positive)or_end(text, byte),
                                                     (positive)reference_or_end(text, byte));

                                        if (last_or_end)
                                                same("string_last_of_or_end", "hostile neighbours",
                                                     (positive)last_or_end(text, byte),
                                                     (positive)reference_last_or_end(text, byte));

                                        for (positive count = 0; count <= size + 8; count += 3)
                                        {
                                                if (first_max)
                                                        same("string_first_of_max", "hostile neighbours",
                                                             (positive)first_max(text, count, byte),
                                                             (positive)reference_first_max(text, count, byte));

                                                if (memory_first)
                                                        same("memory_first_of", "hostile neighbours",
                                                             (positive)memory_first(text, byte, count),
                                                             (positive)reference_memory_first(text, byte, count));

                                                if (length_max)
                                                        same("string_length_max", "hostile neighbours",
                                                             length_max(text, count),
                                                             reference_length_max(text, count));

                                                if (compare_max)
                                                {
                                                        // Equal, then differing at
                                                        // the last byte inside the
                                                        // bound, which is where a
                                                        // word compare that stops
                                                        // early stops noticing.
                                                        same("string_compare_max", "hostile equal",
                                                             (positive)(compare_max(text, twin, count) == 0),
                                                             (positive)(reference_compare_max(text, twin, count) == 0));

                                                        if (size)
                                                        {
                                                                p8 keep = twin[size - 1];

                                                                twin[size - 1] = (p8)(keep ^ 1);
                                                                same("string_compare_max", "hostile greater",
                                                                     (positive)(compare_max(text, twin, count) > 0),
                                                                     (positive)(reference_compare_max(text, twin, count) > 0));
                                                                same("string_compare_max", "hostile less",
                                                                     (positive)(compare_max(twin, text, count) < 0),
                                                                     (positive)(reference_compare_max(twin, text, count) < 0));
                                                                twin[size - 1] = keep;
                                                        }
                                                }
                                        }
                                }
        }
}


/*
        memory_count, which has a wide body and a narrow one.

        On x86_64 it compares thirty two bytes at once and drains a byte wide
        accumulator every 255 rounds, so the sizes that matter are the ones
        either side of 32 and either side of 8160. Alignment matters too: the
        wide loop starts wherever the caller's pointer does.

        The reference is the loop it replaced, which is the only thing worth
        comparing against -- an independent second guess at the answer would
        just be a second thing to be wrong.
*/
positive reference_count(address_any block, positive size, b8 value)
{
        p8 address_to at = block;
        positive found = 0;

        //      b8 is signed: the cast is what lets a byte past 0x7f be found.
        for (positive i = 0; i < size; i++)
                if (at[i] == (p8)value)
                        found++;

        return found;
}

fn check_count()
{
        static p8 room[9000];

        // every alignment the wide loop can begin on, and every length across
        // the point where it starts and the point where it drains
        for (positive off = 0; off < 40; off++)
                for (positive size = 0; size < 200; size++)
                {
                        for (positive i = 0; i < off + size + 8; i++)
                                room[i] = (p8)((i * 7 + off) % 5 ? 'a' : '\n');

                        for (b8 value = 9; value < 12; value++)
                                same("memory_count", "against the byte loop",
                                     memory_count(room + off, size, value),
                                     reference_count(room + off, size, value));
                }

        // past the drain: 255 rounds of 32 bytes is 8160, and a block of
        // nothing but the wanted byte is where a wrapped accumulator shows
        for (positive size = 8100; size < 8300; size += 7)
        {
                for (positive i = 0; i < size; i++)
                        room[i] = '\n';

                same("memory_count", "every byte matches", memory_count(room, size, '\n'), size);
                same("memory_count", "no byte matches", memory_count(room, size, 'z'), 0);
        }
}

/*
        memory_count on each body: on x86_64 with AVX-512, with AVX2 and with
        neither (the SSE2 one), and on riscv64 with every call the first in a
        process, which asks for V, then without V, then without V or Zbb.
        Every size to 160 and those either side of the word turns and of the
        strips at every VLEN past it, the block at each of eight residues
        against the page before it and against the page after it, so that a
        load before its first byte, or past its last where the page does not
        have it, faults. The byte is everywhere outside the block, so that one
        counted from outside shows; inside it is nowhere, everywhere, once at
        each position, and drawn with its neighbours (the byte with bit zero
        or bit seven turned, or any byte) for 0, 0x80, 0xff and the newline.
*/
fn check_count_bodies()
{
        static const positive large[] = {191, 192, 193, 255, 256, 257, 511, 512, 513, 1023, 1024, 1025,
                                         2039, 2040, 2041, 2047, 2048, 2049, 4088, 4095, 4096};
        static const p8 values[] = {'\n', 0, 0x80, 0xff};
        p8 address_to pages = guard_pages("memory_count");
        p64 state = 0x510e527fade682d1ull;

        if (!pages)
                return;
        p8 address_to page = pages + 4096;
#if X64
        p8 avx2 = cpu_has_avx2, avx512 = cpu_has_avx512;
        positive tiers = 3;
#elif RISCV64
        p8 vector = 0, zbb = cpu_has_zbb;
        positive tiers = 3;
#else
        positive tiers = 1;
#endif
        for (positive tier = 0; tier < tiers; tier++)
        {
#if X64
                cpu_has_avx512 = tier < 1 ? avx512 : 0;
                cpu_has_avx2 = tier < 2 ? avx2 : 0;
#elif RISCV64
                if (tier == 1)
                {
                        vector = cpu_has_vector;
                        cpu_has_vector = 1;
                }
                if (tier == 2)
                        cpu_has_zbb = 0;
#endif
                for (positive n = 0; n < 161 + array_count(large); n++)
                        for (positive place = 0; place < 16; place++)
                        {
                                positive size = n < 161 ? n : large[n - 161];
                                positive gap = place & 7;

                                if (size + gap > 4096)
                                        continue;
                                p8 address_to block = place < 8 ? page + gap : page + 4096 - size - gap;

                                for (positive v = 0; v < array_count(values); v++)
                                {
                                        p8 value = values[v];

                                        memory_fill(page, value, 4096);
                                        memory_fill(block, (p8)(value ^ 0x80), size);
                                        VECTOR_FIRST_CALL(tier == 0);
                                        same("memory_count", "nowhere in the block",
                                             (memory_count)(block, size, value), 0);
                                        memory_fill(page, (p8)(value ^ 1), 4096);
                                        memory_fill(block, value, size);
                                        VECTOR_FIRST_CALL(tier == 0);
                                        same("memory_count", "everywhere in the block",
                                             (memory_count)(block, size, value), size);
                                        memory_fill(page, value, 4096);
                                        positive want = 0;

                                        for (positive i = 0; i < size; i++)
                                        {
                                                XORSHIFT64(state);
                                                p8 draw = (p8)(state >> 32);

                                                block[i] = (draw & 3) == 0   ? value
                                                           : (draw & 3) == 1 ? (p8)(value ^ 1)
                                                           : (draw & 3) == 2 ? (p8)(value ^ 0x80)
                                                                             : (p8)(state >> 40);
                                                want += block[i] == value;
                                        }
                                        VECTOR_FIRST_CALL(tier == 0);
                                        same("memory_count", "drawn with its neighbours",
                                             (memory_count)(block, size, value), want);
                                }
                                if (size > 160 || (place != 0 && place != 8 && size > 40))
                                        continue;
                                memory_fill(page, '\n', 4096);
                                memory_fill(block, 'x', size);
                                for (positive at = 0; at < size; at++)
                                {
                                        block[at] = '\n';
                                        VECTOR_FIRST_CALL(tier == 0 && at % 7 == 0);
                                        same("memory_count", "once at a position",
                                             (memory_count)(block, size, '\n'), 1);
                                        block[at] = 'x';
                                }
                        }
        }
#if X64
        cpu_has_avx2 = avx2;
        cpu_has_avx512 = avx512;
#elif RISCV64
        cpu_has_vector = vector;
        cpu_has_zbb = zbb;
#endif
        memory_free(pages, 3 * 4096);
}


/*
        memory_first_of at the sizes that reach the wide path.

        Everything else that hunts a byte in this file works on short strings,
        and on x86_64 memory_first_of takes a thirty two byte at a time route
        that none of them are long enough to enter. A one byte error in it
        passed the whole suite -- 263472 checks -- which is what a test that
        cannot reach the code it is aimed at is worth.

        So: every alignment the wide loop can begin on, every length either
        side of thirty two and of a few multiples of it, and the wanted byte
        placed at each position in turn, including the last one before the
        bound and the first one past it.
*/
address_any reference_memory_first_of(address_any block, b8 value, positive size)
{
        p8 address_to at = block;

        //      b8 is signed: the cast is what lets a byte past 0x7f be found.
        for (positive i = 0; i < size; i++)
                if (at[i] == (p8)value)
                        return at + i;

        return null;
}

fn check_first_of_wide()
{
        static p8 room[600];

        same("memory_span_without_byte", "null empty span",
             memory_span_without_byte(null, 0, 0), 0);
        for (positive off = 0; off < 34; off++)
                for (positive size = 0; size < 140; size++)
                {
                        // nothing to find: the loop has to walk the whole bound
                        for (positive i = 0; i < off + size + 40; i++)
                                room[i] = 'a';

                        same("memory_first_of", "absent over the wide path",
                             (positive)memory_first_of(room + off, 'z', size),
                             (positive)reference_memory_first_of(room + off, 'z', size));
                        same("memory_span_without_byte", "absent wide span",
                             memory_span_without_byte(room + off, 'z', size), size);

                        // and one to find, at every position it could sit
                        for (positive where = 0; where < size; where++)
                        {
                                room[off + where] = 'z';

                                same("memory_first_of", "found over the wide path",
                                     (positive)memory_first_of(room + off, 'z', size),
                                     (positive)reference_memory_first_of(room + off, 'z', size));
                                same("memory_span_without_byte", "found wide span",
                                     memory_span_without_byte(room + off, 'z', size), where);

                                room[off + where] = 'a';
                        }

                        // just past the bound, which must not be reported
                        room[off + size] = 'z';

                        same("memory_first_of", "one past the bound",
                             (positive)memory_first_of(room + off, 'z', size),
                             (positive)reference_memory_first_of(room + off, 'z', size));
                        same("memory_span_without_byte", "byte beyond span",
                             memory_span_without_byte(room + off, 'z', size), size);

                        room[off + size] = 'a';
                }
}


/*
        The bulk routines, tier by tier and alignment by alignment.

        Two things the sweeps above cannot reach.

        The first is the tier. memory_fill, memory_copy_apart and the routines
        that hand work to them have three bodies on x86_64 -- zmm, ymm, and
        eight bytes in an integer register -- and cpu_detect picks one at
        startup. On a machine with AVX-512 that is the only one that ever runs,
        so a one byte error in the other two passes every check in this file.
        cpu_has_avx2 and cpu_has_avx512 are ordinary writable bytes read on
        every call, so the whole sweep runs three times with the byte forced
        down a tier each round and put back afterwards. Nothing forces a tier
        up: a processor cannot be told to have registers it does not have.

        The second is alignment. Everything above works at a fixed offset into
        a static buffer, so the align-up step in each of these -- which moves
        the destination by one of eight values on riscv64, 32 on the ymm path
        and 64 on the zmm and arm64 ones, and moves the source by the same --
        has only ever been entered with one of them. Destination offset crossed
        with source offset is what makes the other sixty three run.
*/
#define BULK_ROOM 1152

static b8 bulk_got[BULK_ROOM];
static b8 bulk_want[BULK_ROOM];
static b8 bulk_from[BULK_ROOM];

// Poisoned either side, so a routine that writes outside its size is caught
// rather than tolerated. The whole buffer is compared, not only the part that
// was meant to change.
fn bulk_same(string_address what, positive tier, positive size, positive off)
{
        checks++;

        for (positive i = 0; i < BULK_ROOM; i++)
                if (bulk_got[i] != bulk_want[i])
                {
                        failures++;
                        string_format(log, "  FAIL %s tier %p size %p off %p: byte %p is %p want %p\n",
                                      what, tier, size, off, i,
                                      (positive)bulk_got[i], (positive)bulk_want[i]);
                        return;
                }
}

// Every size a rung of any of the three ladders starts or ends at, and the
// ones either side of it, up past the widest loop body (256 bytes on zmm,
// which needs 512 before the loop is entered at all).
static positive bulk_sizes[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
        23, 24, 25, 31, 32, 33, 39, 40, 47, 48, 55, 56, 63, 64, 65,
        71, 72, 95, 96, 127, 128, 129, 130, 159, 160, 191, 192,
        255, 256, 257, 258, 287, 288, 319, 320, 383, 384,
        511, 512, 513, 514, 543, 544, 575, 576, 639, 640, 700};

#define BULK_SIZE_COUNT (sizeof(bulk_sizes) / sizeof(bulk_sizes[0]))

static positive bulk_source_offsets[] = {0, 1, 2, 3, 5, 7, 8};

#define BULK_SOURCE_COUNT (sizeof(bulk_source_offsets) / sizeof(bulk_source_offsets[0]))

#if X64
#define BULK_TIERS 3
#else
#define BULK_TIERS 1
#endif

fn bulk_tier(positive pass, p8 wide, p8 widest)
{
#if X64
        cpu_has_avx2 = pass < 2 ? wide : 0;
        cpu_has_avx512 = pass < 1 ? widest : 0;
#else
        (void)pass;
        (void)wide;
        (void)widest;
#endif
}

fn check_bulk_alignments()
{
        p8 wide = 0;
        p8 widest = 0;

#if X64
        wide = cpu_has_avx2;
        widest = cpu_has_avx512;
#endif

        for (positive i = 0; i < BULK_ROOM; i++)
                bulk_from[i] = (b8)(i * 13 + 5);

        for (positive pass = 0; pass < BULK_TIERS; pass++)
        {
                bulk_tier(pass, wide, widest);

                for (positive e = 0; e < BULK_SIZE_COUNT; e++)
                {
                        positive size = bulk_sizes[e];

                        for (positive off = 0; off < 72; off++)
                        {
                                if (off + size + 200 > BULK_ROOM)
                                        continue;

                                reference_fill(bulk_got, 0xA5, BULK_ROOM);
                                reference_fill(bulk_want, 0xA5, BULK_ROOM);
                                memory_fill(bulk_got + off, (b8)(size + 3), size);
                                reference_fill(bulk_want + off, (b8)(size + 3), size);
                                bulk_same("memory_fill", pass, size, off);

                                for (positive s = 0; s < BULK_SOURCE_COUNT; s++)
                                {
                                        positive from = bulk_source_offsets[s];

                                        reference_fill(bulk_got, 0xA5, BULK_ROOM);
                                        reference_fill(bulk_want, 0xA5, BULK_ROOM);
                                        memory_copy_apart(bulk_got + off, bulk_from + from, size);
                                        reference_copy(bulk_want + off, bulk_from + from, size);
                                        bulk_same("memory_copy_apart", pass, size, off);
                                }
                        }
                }
        }

        bulk_tier(0, wide, widest);
}

/*
        memmove, in one buffer, at every distance that puts the overlap in a
        different place relative to the loop body: inside the head, inside the
        chunk kept in registers, and past the whole of it. A memmove that reads
        the last chunk after it has written over it passes a disjoint copy and
        fails exactly here.
*/
static b32 bulk_gaps[] = {-700, -513, -257, -129, -65, -64, -33, -32, -9, -8,
                          -3, -1, 1, 3, 8, 9, 32, 33, 64, 65, 129, 257, 513, 700};

#define BULK_GAP_COUNT (sizeof(bulk_gaps) / sizeof(bulk_gaps[0]))

static positive bulk_move_sizes[] = {0, 1, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33,
                                     63, 64, 65, 71, 127, 128, 129, 255, 256,
                                     257, 511, 512, 513, 575, 640, 700};

#define BULK_MOVE_COUNT (sizeof(bulk_move_sizes) / sizeof(bulk_move_sizes[0]))

/*
        What the copiers answer, and where they read and write. Every length
        from nothing to three hundred, and the sizes where the wide bodies
        change loops, is run with the source and the destination each
        touching the end of a mapped page (a vector load that reads past it
        faults) or its start, at the residues the bodies change their head
        and tail handling at, in every x86 tier. The answer is the
        destination, for memory_copy, memory_copy_apart, memcpy, memmove and
        memory_fill, and the bytes either side of the span are untouched.
        Overlapping moves run against a page too, forwards and backwards, and
        a move onto itself changes nothing and still answers.
*/
static positive copy_residues[] = {0, 1, 3, 8, 15, 16, 31, 32, 63};
static positive copy_gaps[] = {1, 2, 3, 7, 8, 15, 16, 31, 32, 33, 63, 64, 65, 127,
                               128, 129, 200};
static positive copy_big[] = {511, 512, 513, 1000, 1024, 2047, 2048, 2049, 4095, 4096};
static string_address copy_names[] = {"memory_copy", "memory_copy_apart", "memcpy",
                                      "memmove", "memory_fill"};
static b8 copy_expected[4096 + 256];

#define COPY_RESIDUES (sizeof(copy_residues) / sizeof(copy_residues[0]))
#define COPY_GAPS (sizeof(copy_gaps) / sizeof(copy_gaps[0]))
#define COPY_BIG (sizeof(copy_big) / sizeof(copy_big[0]))

static address_any copy_call(positive what, p8 address_to to, p8 address_to from,
                              positive size)
{
        switch (what)
        {
        case 0: return memory_copy(to, from, size);
        case 1: return memory_copy_apart(to, from, size);
        case 2: return memcpy(to, from, size);
        case 3: return memmove(to, from, size);
        }
        return memory_fill(to, 0xC3, size);
}

fn check_copy_returns_size(p8 address_to from, p8 address_to to, positive size)
{
        for (positive what = 0; what < 5; what++)
                for (positive sm = 0; sm < 2 * COPY_RESIDUES; sm++)
                        for (positive dm = 0; dm < 2 * COPY_RESIDUES; dm++)
                        {
                                positive sr = copy_residues[sm >> 1];
                                positive dr = copy_residues[dm >> 1];
                                p8 address_to src = sm & 1 ? from + sr : from + 4096 - size - sr;
                                p8 address_to dst = dm & 1 ? to + dr : to + 4096 - size - dr;
                                address_any answer;
                                positive low = dst - to < 64 ? 0 : dst - to - 64;
                                positive high = dst - to + size + 64 > 4096 ? 4096 : dst - to + size + 64;
                                positive bad = 0;

                                if (size + sr > 4096 || size + dr > 4096)
                                        continue;

                                answer = copy_call(what, dst, src, size);
                                same(copy_names[what], "answers the destination",
                                     (positive)answer, (positive)dst);

                                for (positive i = low; i < high; i++)
                                {
                                        p8 want = 0x5A;

                                        if (to + i >= dst && to + i < dst + size)
                                                want = what == 4 ? 0xC3 : src[to + i - dst];
                                        bad += to[i] != want;
                                }

                                same(copy_names[what], "bytes round the span", bad, 0);
                                reference_fill(dst, 0x5A, size);
                        }
}

fn check_move_returns(p8 address_to page, positive gap, positive size, positive atend)
{
        for (positive what = 0; what < 2; what++)
                for (positive back = 0; back < 3; back++)
                {
                        p8 address_to base = atend ? page + 4096 - size - gap : page;
                        p8 address_to src = back == 1 ? base + gap : base;
                        p8 address_to dst = back == 0 ? base + gap : base;
                        address_any answer;

                        if (back == 2)
                                dst = src = base + gap;

                        if (size + gap > 4096)
                                continue;

                        for (positive i = 0; i < size + gap; i++)
                                base[i] = (b8)(i * 7 + 3 + size);

                        for (positive i = 0; i < size + gap; i++)
                                copy_expected[i] = base[i];

                        reference_copy(copy_expected + (dst - base), copy_expected + (src - base), size);
                        answer = what ? memmove(dst, src, size) : memory_copy(dst, src, size);
                        same(what ? "memmove" : "memory_copy", "overlap answers the destination",
                             (positive)answer, (positive)dst);
                        same_bytes(what ? "memmove" : "memory_copy", "overlap bytes", base,
                                   copy_expected, size + gap);
                }
}

fn check_copy_returns()
{
        p8 address_to pages = memory(5 * 4096);
        p8 address_to from;
        p8 address_to to;
        p8 had_avx2 = 0;
        p8 had_avx512 = 0;

        if (!((bipolar)(positive)pages > 0))
        {
                same("copy returns", "mapping", 0, 1);
                return;
        }
        system_call_3(syscall(mprotect), (positive)pages, 4096, 0);
        system_call_3(syscall(mprotect), (positive)(pages + 8192), 4096, 0);
        system_call_3(syscall(mprotect), (positive)(pages + 16384), 4096, 0);
        from = pages + 4096;
        to = pages + 12288;
#if X64
        had_avx2 = cpu_has_avx2;
        had_avx512 = cpu_has_avx512;
#endif

        for (positive i = 0; i < 4096; i++)
        {
                from[i] = (b8)(i * 29 + 11 + (i >> 8));
                to[i] = 0x5A;
        }

        for (positive pass = 0; pass < BULK_TIERS; pass++)
        {
                bulk_tier(pass, had_avx2, had_avx512);

                for (positive size = 0; size <= 300; size++)
                        check_copy_returns_size(from, to, size);

                for (positive big = 0; big < COPY_BIG; big++)
                        check_copy_returns_size(from, to, copy_big[big]);

                for (positive g = 0; g < COPY_GAPS; g++)
                {
                        for (positive size = 0; size <= 300; size++)
                                for (positive atend = 0; atend < 2; atend++)
                                        check_move_returns(from, copy_gaps[g], size, atend);

                        for (positive big = 0; big < COPY_BIG; big++)
                                for (positive atend = 0; atend < 2; atend++)
                                        check_move_returns(from, copy_gaps[g], copy_big[big], atend);

                        check_move_returns(from, copy_gaps[g], 4096 - copy_gaps[g], 0);
                }
        }

        bulk_tier(0, had_avx2, had_avx512);
        memory_free(pages, 5 * 4096);
}

/*
        The library under the names C gives it, against models written here
        from the C standard (and glibc where the standard leaves an answer
        open: memmem with no needle, the end pointer of stpncpy). Every
        input sits against a guard page, at the end of one (a read past the
        last byte faults) and at the start of one (a read in front of the
        first does), at the residues the bodies change their head handling
        at; lengths run from nothing to past the widest vector, and bytes
        come from a small alphabet that holds the high half, 0xff and the
        letters of both cases, so matches are common and sign errors show.
*/
static p8 libc_alphabet[] = {1, 2, 'a', 'b', 'A', 'B', 0x7f, 0x80, 0xff, 'c', ' '};
static positive libc_slack[] = {0, 1, 2, 3, 5, 8, 15, 16, 31, 32, 33, 63};
#define LIBC_SLACK (sizeof(libc_slack) / sizeof(libc_slack[0]))
static p8 address_to libc_a;
static p8 address_to libc_b;
static p8 address_to libc_c;
static p8 libc_model[1024];
static p8 libc_model_b[1024];

static p8 libc_byte(positive zeros)
{
        positive r = next();

        if (zeros && (r >> 40) % 8 == 0)
                return 0;
        return libc_alphabet[(r >> 8) % sizeof(libc_alphabet)];
}

static positive libc_sign(bipolar v) { return v < 0 ? 2 : v > 0 ? 1 : 0; }

// A place for n bytes: against the end of the page (guard after) or the start
// (guard before), slack bytes in from the edge.
static p8 address_to libc_place(p8 address_to page, positive n, positive slack,
                                positive at_start)
{
        return at_start ? page + slack : page + 4096 - n - slack;
}

static void libc_names_one(positive length, positive slack_a, positive slack_b,
                           positive anchor)
{
        p8 address_to s = libc_place(libc_a, length + 1, slack_a, anchor & 1);
        p8 address_to t = libc_place(libc_b, length + 1, slack_b, anchor & 2);
        p8 address_to d = libc_place(libc_c, length + 1, slack_b, anchor & 4);
        positive other = (next() >> 12) % (length + 1);
        positive at;
        p8 c;

        // a string, and one that differs from it late or not at all
        for (positive i = 0; i < length; i++)
                s[i] = libc_byte(0);
        s[length] = 0;

        for (positive i = 0; i <= length; i++)
                t[i] = s[i];
        if (other < length && (next() & 3))
                t[other] = t[other] ^ ((next() & 1) ? 0x20 : 0x80);
        if (length && !(next() & 7))
                t[length - 1] = 0;

        c = !(next() & 3) ? 0 : !(next() & 3) ? 'z' : s[(next() >> 16) % (length + 1)];

        // strlen strnlen
        same("strlen", "length", strlen((char address_to)s), length);
        for (positive bound = 0; bound <= length + 2; bound += 1 + (length >> 5))
                same("strnlen", "bound", strnlen((char address_to)s, bound),
                     bound < length ? bound : length);

        // strchr strrchr strchrnul
        at = length + 1;
        for (positive i = 0; i <= length; i++)
                if (s[i] == c)
                {
                        at = i;
                        break;
                }
        same("strchr", "first", (positive)strchr((char address_to)s, c | ((next() & 1) << 8)),
             at <= length ? (positive)(s + at) : 0);
        same("strchrnul", "first", (positive)strchrnul((char address_to)s, c),
             (positive)(s + (at <= length ? at : length)));
        at = length + 1;
        for (positive i = length + 1; i-- > 0;)
                if (s[i] == c)
                {
                        at = i;
                        break;
                }
        same("strrchr", "last", (positive)strrchr((char address_to)s, c),
             at <= length ? (positive)(s + at) : 0);

        // strcmp strncmp strcasecmp strncasecmp
        {
                positive want = 0;
                positive want_fold = 0;
                positive bound = (next() >> 20) % (length + 2);
                positive want_n = 0;
                positive want_fold_n = 0;

                for (positive i = 0;; i++)
                {
                        p8 x = s[i];
                        p8 y = t[i];
                        p8 fx = x >= 'A' && x <= 'Z' ? x + 32 : x;
                        p8 fy = y >= 'A' && y <= 'Z' ? y + 32 : y;

                        if (!want && x != y)
                                want = x < y ? 2 : 1;
                        if (!want_fold && fx != fy)
                                want_fold = fx < fy ? 2 : 1;
                        if (i < bound && !want_n && x != y)
                                want_n = x < y ? 2 : 1;
                        if (i < bound && !want_fold_n && fx != fy)
                                want_fold_n = fx < fy ? 2 : 1;
                        if (!x || !y)
                                break;
                }

                // each stops at the first terminator, so the verdict is the first
                // difference at or before it
                same("strcmp", "sign", libc_sign(strcmp((char address_to)s, (char address_to)t)), want);
                same("strcasecmp", "sign", libc_sign(strcasecmp((char address_to)s, (char address_to)t)), want_fold);
                same("strncmp", "sign", libc_sign(strncmp((char address_to)s, (char address_to)t, bound)), want_n);
                same("strncasecmp", "sign", libc_sign(strncasecmp((char address_to)s, (char address_to)t, bound)), want_fold_n);
        }

        // memcmp bcmp over the same bytes, which include the terminator
        {
                positive want = 0;

                for (positive i = 0; i <= length && !want; i++)
                        if (s[i] != t[i])
                                want = s[i] < t[i] ? 2 : 1;
                same("memcmp", "sign", libc_sign(memcmp(s, t, length + 1)), want);
                same("bcmp", "zero", bcmp(s, t, length + 1) != 0, want != 0);
        }

        // memchr memrchr: with zeros inside
        for (positive i = 0; i < length; i++)
                s[i] = libc_byte(1);
        {
                positive first = length;
                positive last = length;
                positive want_first;
                positive want_last;

                for (positive i = 0; i < length; i++)
                        if (s[i] == c)
                        {
                                if (first == length)
                                        first = i;
                                last = i;
                        }
                want_first = first == length ? 0 : (positive)(s + first);
                want_last = last == length ? 0 : (positive)(s + last);
                same("memchr", "first", (positive)memchr(s, c | ((next() & 1) << 9), length), want_first);
                same("memrchr", "last", (positive)memrchr(s, c, length), want_last);
        }

        // memmem: a needle taken from the haystack, one made up, and none
        {
                positive nl = length ? (next() >> 9) % (length < 9 ? length + 1 : 9) : 0;
                positive from = length - nl ? (next() >> 30) % (length - nl + 1) : 0;
                positive want = length + 1;

                for (positive i = 0; i < nl; i++)
                        t[i] = s[from + i];
                if (nl && !(next() & 3))
                        t[nl - 1] ^= 1;

                for (positive i = 0; i + nl <= length; i++)
                {
                        positive j = 0;

                        while (j < nl && s[i + j] == t[j])
                                j++;
                        if (j == nl)
                        {
                                want = i;
                                break;
                        }
                }
                same("memmem", "first", (positive)memmem(s, length, t, nl),
                     want <= length ? (positive)(s + want) : 0);
        }

        // copies: strcpy stpcpy strcat strncpy stpncpy strncat memccpy
        for (positive i = 0; i < length; i++)
                s[i] = libc_byte(0);
        s[length] = 0;
        {
                positive bound = (next() >> 18) % (length + 4);
                p8 address_to e = d;

                d = libc_place(libc_c, length + 1 + 3, slack_b, anchor & 4);
                same("strcpy", "answers destination", (positive)strcpy((char address_to)d, (char address_to)s), (positive)d);
                same_bytes("strcpy", "bytes", d, s, length + 1);
                same("stpcpy", "answers the terminator", (positive)stpcpy((char address_to)d, (char address_to)s),
                     (positive)(d + length));

                // strncpy pads with zeros to the bound and does not terminate when cut
                {
                        positive n = bound < 1024 ? bound : 1023;
                        p8 address_to z = libc_place(libc_c, n + 1, slack_b, anchor & 4);

                        for (positive i = 0; i < n; i++)
                                libc_model[i] = i < length ? s[i] : 0;
                        same("strncpy", "answers destination", (positive)strncpy((char address_to)z, (char address_to)s, n), (positive)z);
                        same_bytes("strncpy", "bytes", z, libc_model, n);
                        same("stpncpy", "answers", (positive)stpncpy((char address_to)z, (char address_to)s, n),
                             (positive)(z + (n < length ? n : length)));
                        same_bytes("stpncpy", "bytes", z, libc_model, n);
                }

                // strcat and strncat on a destination that already holds a string
                {
                        positive base = (next() >> 25) % 9;
                        positive room = base + length + 1;
                        positive take = bound < length ? bound : length;
                        p8 address_to z = libc_place(libc_c, room + 1, slack_b, anchor & 4);

                        for (positive i = 0; i < base; i++)
                                z[i] = libc_model_b[i] = libc_byte(0);
                        z[base] = libc_model_b[base] = 0;
                        same("strcat", "answers destination", (positive)strcat((char address_to)z, (char address_to)s), (positive)z);
                        for (positive i = 0; i < length; i++)
                                libc_model[base + i] = s[i];
                        for (positive i = 0; i < base; i++)
                                libc_model[i] = libc_model_b[i];
                        libc_model[base + length] = 0;
                        same_bytes("strcat", "bytes", z, libc_model, base + length + 1);

                        z[base] = 0;
                        same("strncat", "answers destination", (positive)strncat((char address_to)z, (char address_to)s, bound), (positive)z);
                        libc_model[base + take] = 0;
                        same_bytes("strncat", "bytes", z, libc_model, base + take + 1);
                }

                // memccpy
                {
                        p8 stop = !(next() & 3) ? 'z' : s[(next() >> 20) % (length + 1)];
                        positive upto = length + 1;
                        positive n = bound < length + 1 ? bound : length + 1;
                        p8 address_to z = libc_place(libc_c, n + 1, slack_b, anchor & 4);
                        positive found = n;

                        for (positive i = 0; i < n; i++)
                                if (s[i] == stop)
                                {
                                        found = i;
                                        break;
                                }
                        (void)upto;
                        for (positive i = 0; i < n; i++)
                                z[i] = 0x99;
                        same("memccpy", "answers past the byte", (positive)memccpy(z, s, stop, n),
                             found < n ? (positive)(z + found + 1) : 0);
                        for (positive i = 0; i < (found < n ? found + 1 : n); i++)
                                libc_model[i] = s[i];
                        same_bytes("memccpy", "bytes", z, libc_model, found < n ? found + 1 : n);
                        for (positive i = found < n ? found + 1 : n; i < n; i++)
                                same("memccpy", "left the rest", z[i], 0x99);
                }
                (void)e;
        }

        // strstr
        {
                positive nl = length ? (next() >> 9) % (length < 7 ? length + 1 : 7) : 0;
                positive from = length - nl ? (next() >> 30) % (length - nl + 1) : 0;
                positive want = length + 1;

                for (positive i = 0; i < nl; i++)
                        t[i] = s[from + i];
                t[nl] = 0;
                if (nl && !(next() & 3))
                        t[nl - 1] = 'q';
                nl = string_length((char address_to)t);

                for (positive i = 0; i + nl <= length; i++)
                {
                        positive j = 0;

                        while (j < nl && s[i + j] == t[j])
                                j++;
                        if (j == nl)
                        {
                                want = i;
                                break;
                        }
                }
                same("strstr", "first", (positive)strstr((char address_to)s, (char address_to)t),
                     want <= length ? (positive)(s + want) : 0);
        }

        // strspn strcspn strpbrk over a set of up to four members
        {
                p8 set[6];
                positive members = (next() >> 11) % 5;
                positive span = 0;
                positive cspan = 0;
                positive hit = length + 1;

                for (positive i = 0; i < members; i++)
                        set[i] = libc_alphabet[(next() >> 33) % sizeof(libc_alphabet)];
                set[members] = 0;

                while (span < length)
                {
                        positive in = 0;

                        for (positive i = 0; i < members; i++)
                                in |= set[i] == s[span];
                        if (!in)
                                break;
                        span++;
                }
                while (cspan < length)
                {
                        positive in = 0;

                        for (positive i = 0; i < members; i++)
                                in |= set[i] == s[cspan];
                        if (in)
                        {
                                hit = cspan;
                                break;
                        }
                        cspan++;
                }
                same("strspn", "count", strspn((char address_to)s, (char address_to)set), span);
                same("strcspn", "count", strcspn((char address_to)s, (char address_to)set), cspan);
                same("strpbrk", "first", (positive)strpbrk((char address_to)s, (char address_to)set),
                     hit <= length ? (positive)(s + hit) : 0);
        }

        // memset memcpy answers
        {
                p8 v = (p8)next();

                same("memset", "answers destination", (positive)memset(d, v, length), (positive)d);
                for (positive i = 0; i < length; i++)
                        libc_model[i] = v;
                same_bytes("memset", "bytes", d, libc_model, length);
        }
}

fn check_libc_names()
{
        p8 address_to pages = memory(7 * 4096);
        p8 had_avx2 = 0;
        p8 had_avx512 = 0;

        if (!((bipolar)(positive)pages > 0))
        {
                same("libc names", "mapping", 0, 1);
                return;
        }
        for (positive i = 0; i < 7; i += 2)
                system_call_3(syscall(mprotect), (positive)(pages + 4096 * i), 4096, 0);
        libc_a = pages + 4096;
        libc_b = pages + 3 * 4096;
        libc_c = pages + 5 * 4096;
#if X64
        had_avx2 = cpu_has_avx2;
        had_avx512 = cpu_has_avx512;
#endif

        for (positive pass = 0; pass < BULK_TIERS; pass++)
        {
                bulk_tier(pass, had_avx2, had_avx512);

                for (positive length = 0; length <= 140; length++)
                        for (positive a = 0; a < LIBC_SLACK; a++)
                                for (positive anchor = 0; anchor < 8; anchor++)
                                        libc_names_one(length, libc_slack[a],
                                                       libc_slack[(a + length) % LIBC_SLACK], anchor);

                for (positive length = 141; length <= 600; length += 7)
                        for (positive a = 0; a < LIBC_SLACK; a += 3)
                                for (positive anchor = 0; anchor < 8; anchor++)
                                        libc_names_one(length, libc_slack[a],
                                                       libc_slack[(a + length) % LIBC_SLACK], anchor);
        }

        bulk_tier(0, had_avx2, had_avx512);
        memory_free(pages, 7 * 4096);
}

/*
        The names of the library that C does not have, against models written
        here, on the same guarded pages as check_libc_names: strlcpy and
        strlcat (the answer is the length wanted, a capacity of zero touches
        nothing), strsep, strcasestr, the case-folding and common-prefix
        compares, the hex encoder in both cases, memory_zero and bzero, bcopy
        through an overlap, and the fills by word.
*/
static void library_names_one(positive length, positive slack_a, positive slack_b,
                              positive anchor)
{
        p8 address_to s = libc_place(libc_a, length + 1, slack_a, anchor & 1);
        p8 address_to t = libc_place(libc_b, length + 1, slack_b, anchor & 2);
        p8 address_to d;
        positive capacity = (next() >> 17) % (length + 4);
        positive base = (next() >> 27) % 7;

        for (positive i = 0; i < length; i++)
                s[i] = libc_byte(0);
        s[length] = 0;
        for (positive i = 0; i <= length; i++)
                t[i] = s[i];
        if (length && (next() & 1))
                t[(next() >> 22) % length] ^= 0x20;

        // strlcpy
        d = libc_place(libc_c, capacity + 1, slack_b, anchor & 4);
        for (positive i = 0; i < capacity + 1; i++)
                d[i] = 0x77;
        same("string_copy_bounded", "answers the length wanted",
             string_copy_bounded((string_address)d, (string_address)s, capacity), length);
        if (capacity)
        {
                positive take = length < capacity - 1 ? length : capacity - 1;

                same_bytes("string_copy_bounded", "bytes", d, s, take);
                same("string_copy_bounded", "terminator", d[take], 0);
                for (positive i = take + 1; i < capacity + 1; i++)
                        same("string_copy_bounded", "left the rest", d[i], 0x77);
        }
        else
                same("string_copy_bounded", "capacity zero touches nothing", d[0], 0x77);
        same("string_copy_bounded", "capacity zero, null destination",
             string_copy_bounded(null, (string_address)s, 0), length);

        // strlcat onto a string of `base` bytes, with room for capacity in all
        {
                positive room = capacity + base + 1;
                positive have = base < capacity ? base : capacity;

                d = libc_place(libc_c, room + 2, slack_b, anchor & 4);
                for (positive i = 0; i < room + 2; i++)
                        d[i] = 0x77;
                for (positive i = 0; i < base; i++)
                        d[i] = libc_model_b[i] = libc_byte(0);
                d[base] = libc_model_b[base] = 0;
                // a destination with no terminator inside its capacity
                if (!(next() & 7) && capacity && capacity <= base)
                        for (positive i = 0; i < capacity; i++)
                                libc_model_b[i] = d[i];
                same("string_append_bounded", "answers the length wanted",
                     string_append_bounded((string_address)d, (string_address)s, capacity),
                     have + length);
                if (capacity && base < capacity)
                {
                        positive take = length < capacity - base - 1 ? length : capacity - base - 1;

                        same_bytes("string_append_bounded", "kept", d, libc_model_b, base);
                        same_bytes("string_append_bounded", "appended", d + base, s, take);
                        same("string_append_bounded", "terminator", d[base + take], 0);
                        for (positive i = base + take + 1; i < room + 2; i++)
                                same("string_append_bounded", "left the rest", d[i], 0x77);
                }
                else
                        same_bytes("string_append_bounded", "full destination untouched", d,
                                   libc_model_b, base);
        }

        // strcasestr: a needle cut from the haystack with its case flipped, and one that is not there
        {
                positive nl = length ? (next() >> 9) % (length < 7 ? length + 1 : 7) : 0;
                positive from = length - nl ? (next() >> 30) % (length - nl + 1) : 0;
                positive want = length + 1;

                for (positive i = 0; i < nl; i++)
                {
                        p8 x = s[from + i];

                        if (!(next() & 3) && x >= 'a' && x <= 'z')
                                x -= 32;
                        else if (!(next() & 3) && x >= 'A' && x <= 'Z')
                                x += 32;
                        t[i] = x;
                }
                t[nl] = 0;
                if (nl && !(next() & 3))
                        t[nl - 1] = 'q';
                nl = string_length((char address_to)t);

                for (positive i = 0; i + nl <= length && want > length; i++)
                {
                        positive j = 0;

                        while (j < nl)
                        {
                                p8 x = s[i + j];
                                p8 y = t[j];

                                x = x >= 'A' && x <= 'Z' ? x + 32 : x;
                                y = y >= 'A' && y <= 'Z' ? y + 32 : y;
                                if (x != y)
                                        break;
                                j++;
                        }
                        if (j == nl)
                                want = i;
                }
                same("strcasestr", "first", (positive)strcasestr((char address_to)s, (char address_to)t),
                     want <= length ? (positive)(s + want) : 0);
        }

        // memory_compare_ascii_case and memory_common_prefix over the same bytes
        {
                positive want = 0;
                positive prefix = 0;

                for (positive i = 0; i < length; i++)
                        t[i] = s[i];
                if (length && (next() & 3))
                {
                        positive at = (next() >> 21) % length;

                        t[at] = (next() & 1) ? (s[at] ^ 0x20) : (s[at] + 1);
                }
                for (positive i = 0; i < length && !want; i++)
                {
                        p8 x = s[i];
                        p8 y = t[i];

                        x = x >= 'A' && x <= 'Z' ? x + 32 : x;
                        y = y >= 'A' && y <= 'Z' ? y + 32 : y;
                        if (x != y)
                                want = x < y ? 2 : 1;
                }
                while (prefix < length && s[prefix] == t[prefix])
                        prefix++;
                same("memory_compare_ascii_case", "sign",
                     libc_sign(memory_compare_ascii_case(s, t, length)), want);
                same("memory_common_prefix", "count", memory_common_prefix(s, t, length), prefix);
        }

        // memory_into_hex, in both cases; the destination ends at a guard too
        {
                static const char lower[] = "0123456789abcdef";
                static const char upper[] = "0123456789ABCDEF";

                d = libc_place(libc_c, 2 * length, slack_b, anchor & 4);
                for (positive upcase = 0; upcase < 2; upcase++)
                {
                        positive wrote = upcase ? memory_into_hex_case(d, s, length, 1)
                                                : memory_into_hex(d, s, length);
                        positive bad = 0;

                        same("memory_into_hex", "count", wrote, 2 * length);
                        for (positive i = 0; i < length; i++)
                        {
                                const char address_to digits = upcase ? upper : lower;

                                bad += d[2 * i] != (p8)digits[s[i] >> 4];
                                bad += d[2 * i + 1] != (p8)digits[s[i] & 15];
                        }
                        same("memory_into_hex", "digits", bad, 0);
                        same("memory_into_hex_case", "lower matches memory_into_hex",
                             memory_into_hex_case(d, s, length, 0), 2 * length);
                }
        }

        // memory_zero, bzero
        {
                d = libc_place(libc_c, length, slack_b, anchor & 4);
                for (positive i = 0; i < length; i++)
                        d[i] = 0x77;
                memory_zero(d, length);
                for (positive i = 0; i < length; i++)
                        libc_model[i] = 0;
                same_bytes("memory_zero", "bytes", d, libc_model, length);
                for (positive i = 0; i < length; i++)
                        d[i] = 0x77;
                bzero(d, length);
                same_bytes("bzero", "bytes", d, libc_model, length);
        }

        // bcopy through an overlap, both ways, at the end of a page
        {
                positive gap = 1 + (next() >> 13) % 70;
                p8 address_to b = libc_place(libc_c, length + gap, slack_b, anchor & 4);

                for (positive i = 0; i < length + gap; i++)
                        b[i] = libc_model_b[i] = (p8)(i * 11 + 5);
                bcopy(b, b + gap, length);
                reference_copy(libc_model_b + gap, libc_model_b, length);
                same_bytes("bcopy", "forwards", b, libc_model_b, length + gap);
                for (positive i = 0; i < length + gap; i++)
                        b[i] = libc_model_b[i] = (p8)(i * 11 + 5);
                bcopy(b + gap, b, length);
                reference_copy(libc_model_b, libc_model_b + gap, length);
                same_bytes("bcopy", "backwards", b, libc_model_b, length + gap);
        }

        // memory_exchange_apart between two guarded spans
        {
                p8 address_to a = libc_place(libc_b, length, slack_a, anchor & 2);
                p8 address_to b = libc_place(libc_c, length, slack_b, anchor & 4);

                for (positive i = 0; i < length; i++)
                {
                        a[i] = libc_model[i] = libc_byte(1);
                        b[i] = libc_model_b[i] = libc_byte(1);
                }
                memory_exchange_apart(a, b, length);
                same_bytes("memory_exchange_apart", "left gets right", a, libc_model_b, length);
                same_bytes("memory_exchange_apart", "right gets left", b, libc_model, length);
        }

        // strsep and strtok_r over the string, split at up to three delimiters
        {
                p8 delimiters[4];
                positive members = (next() >> 11) % 4;
                p8 address_to z = libc_place(libc_c, length + 1, slack_b, anchor & 4);
                char address_to holder;
                char address_to token;
                positive at = 0;
                positive seen = 0;

                for (positive i = 0; i < members; i++)
                        delimiters[i] = libc_alphabet[(next() >> 29) % 4 + 2];
                delimiters[members] = 0;

                // strsep keeps every field, empty ones too
                for (positive i = 0; i <= length; i++)
                        z[i] = s[i];
                holder = (char address_to)z;
                while ((token = strsep(&holder, (char address_to)delimiters)) != null)
                {
                        positive stop = at;
                        positive is_delimiter = 0;

                        same("strsep", "token starts where the last ended", (positive)token, (positive)(z + at));
                        while (stop < length)
                        {
                                is_delimiter = 0;
                                for (positive i = 0; i < members; i++)
                                        is_delimiter |= delimiters[i] == s[stop];
                                if (is_delimiter)
                                        break;
                                stop++;
                        }
                        same("strsep", "token ends at a delimiter or the end", string_length(token), stop - at);
                        seen++;
                        if (stop >= length)
                        {
                                same("strsep", "last field leaves the cursor null", (positive)holder, 0);
                                at = length + 1;
                                break;
                        }
                        same("strsep", "cursor after the delimiter", (positive)holder, (positive)(z + stop + 1));
                        at = stop + 1;
                }
                same("strsep", "reached the end", at, length + 1);

                // strtok_r skips runs of delimiters and never answers an empty token
                for (positive i = 0; i <= length; i++)
                        z[i] = s[i];
                {
                        char address_to save = null;
                        positive cursor = 0;
                        char address_to start = (char address_to)z;

                        for (;;)
                        {
                                positive begin = cursor;
                                positive stop;

                                while (begin < length)
                                {
                                        positive is_delimiter = 0;

                                        for (positive i = 0; i < members; i++)
                                                is_delimiter |= delimiters[i] == s[begin];
                                        if (!is_delimiter)
                                                break;
                                        begin++;
                                }
                                stop = begin;
                                while (stop < length)
                                {
                                        positive is_delimiter = 0;

                                        for (positive i = 0; i < members; i++)
                                                is_delimiter |= delimiters[i] == s[stop];
                                        if (is_delimiter)
                                                break;
                                        stop++;
                                }
                                token = strtok_r(start, (char address_to)delimiters, &save);
                                start = null;
                                if (begin >= length)
                                {
                                        same("strtok_r", "no more tokens", (positive)token, 0);
                                        break;
                                }
                                same("strtok_r", "token", (positive)token, (positive)(z + begin));
                                same("strtok_r", "token length", string_length(token), stop - begin);
                                cursor = stop < length ? stop + 1 : length;
                        }
                }
        }

        // the word fills, at every alignment their callers can have
        {
                positive count = length;
                p32 address_to w = (p32 address_to)libc_place(libc_c, 4 * count, slack_b & ~3ull, anchor & 4);
                p64 address_to q = (p64 address_to)libc_place(libc_a, 8 * count, slack_a & ~7ull, anchor & 1);

                if ((positive)w & 3)
                        w = (p32 address_to)((positive)w & ~3ull);
                if ((positive)q & 7)
                        q = (p64 address_to)((positive)q & ~7ull);
                if ((p8 address_to)w >= libc_c && (p8 address_to)q >= libc_a &&
                    (p8 address_to)w + 4 * count <= libc_c + 4096 &&
                    (p8 address_to)q + 8 * count <= libc_a + 4096)
                {
                        p32 v = (p32)next();
                        p64 v64 = next();
                        positive bad = 0;

                        same("memory_fill_32", "answers destination",
                             (positive)memory_fill_32(w, v, count), (positive)w);
                        for (positive i = 0; i < count; i++)
                                bad += w[i] != v;
                        memory_fill_u32(w, count, ~v);
                        for (positive i = 0; i < count; i++)
                                bad += w[i] != (p32)~v;
                        same("memory_fill_32/u32", "words", bad, 0);

                        bad = 0;
                        same("memory_fill_64", "answers destination",
                             (positive)memory_fill_64(q, v64, count), (positive)q);
                        for (positive i = 0; i < count; i++)
                                bad += q[i] != v64;
                        memory_fill_u64_aligned(q, count, ~v64);
                        for (positive i = 0; i < count; i++)
                                bad += q[i] != ~v64;
                        same("memory_fill_64/u64_aligned", "words", bad, 0);
                }
        }
}

fn check_library_names()
{
        p8 address_to pages = memory(7 * 4096);
        p8 had_avx2 = 0;
        p8 had_avx512 = 0;

        if (!((bipolar)(positive)pages > 0))
        {
                same("library names", "mapping", 0, 1);
                return;
        }
        for (positive i = 0; i < 7; i += 2)
                system_call_3(syscall(mprotect), (positive)(pages + 4096 * i), 4096, 0);
        libc_a = pages + 4096;
        libc_b = pages + 3 * 4096;
        libc_c = pages + 5 * 4096;
#if X64
        had_avx2 = cpu_has_avx2;
        had_avx512 = cpu_has_avx512;
#endif

        for (positive pass = 0; pass < BULK_TIERS; pass++)
        {
                bulk_tier(pass, had_avx2, had_avx512);

                for (positive length = 0; length <= 140; length++)
                        for (positive a = 0; a < LIBC_SLACK; a++)
                                for (positive anchor = 0; anchor < 8; anchor++)
                                        library_names_one(length, libc_slack[a],
                                                          libc_slack[(a + length) % LIBC_SLACK], anchor);

                for (positive length = 141; length <= 600; length += 7)
                        for (positive a = 0; a < LIBC_SLACK; a += 3)
                                for (positive anchor = 0; anchor < 8; anchor++)
                                        library_names_one(length, libc_slack[a],
                                                          libc_slack[(a + length) % LIBC_SLACK], anchor);
        }

        bulk_tier(0, had_avx2, had_avx512);
        memory_free(pages, 7 * 4096);
}

/*
        The four word fills at every width from zero to 300 and every
        destination offset in sixteen elements, each against a plain loop.
        The bodies change with width class and with machine, so this is the
        place every machine's answer is held to one reference. Each case
        compares a window that starts before the destination and runs past
        the end, so a store that lands one element early or late shows.
*/
fn check_fill_widths()
{
        p8 address_to got = memory(8192);
        p8 address_to want = memory(8192);
        positive bad_u32 = 0;
        positive bad_32 = 0;
        positive bad_u64 = 0;
        positive bad_64 = 0;

        if (!((bipolar)(positive)got > 0) || !((bipolar)(positive)want > 0))
        {
                same("fill widths", "mapping", 0, 1);
                return;
        }
        for (positive width = 0; width <= 300; width++)
                for (positive at = 0; at < 16; at++)
                {
                        positive value = width * 2654435761ull + at;
                        p32 address_to w32 = (p32 address_to)(got + 64) + at;
                        p32 address_to r32 = (p32 address_to)(want + 64) + at;
                        p64 address_to w64 = (p64 address_to)(got + 64) + at;
                        p64 address_to r64 = (p64 address_to)(want + 64) + at;

                        for (positive i = 0; i < 4096; i++)
                                got[i] = want[i] = 0x5a;
                        for (positive i = 0; i < width; i++)
                                r32[i] = (p32)value;
                        memory_fill_u32(w32, width, (p32)value);
                        for (positive i = 0; i < 4096; i++)
                                bad_u32 += got[i] != want[i];

                        for (positive i = 0; i < 4096; i++)
                                got[i] = 0x5a;
                        bad_32 += (positive)memory_fill_32(w32, (p32)value, width) != (positive)w32;
                        for (positive i = 0; i < 4096; i++)
                                bad_32 += got[i] != want[i];

                        for (positive i = 0; i < 4096; i++)
                                got[i] = want[i] = 0x5a;
                        for (positive i = 0; i < width; i++)
                                r64[i] = value;
                        memory_fill_u64_aligned(w64, width, value);
                        for (positive i = 0; i < 4096; i++)
                                bad_u64 += got[i] != want[i];

                        for (positive i = 0; i < 4096; i++)
                                got[i] = 0x5a;
                        bad_64 += (positive)memory_fill_64(w64, value, width) != (positive)w64;
                        for (positive i = 0; i < 4096; i++)
                                bad_64 += got[i] != want[i];
                }
        same("memory_fill_u32", "widths 0-300 at sixteen offsets", bad_u32, 0);
        same("memory_fill_32", "widths 0-300 at sixteen offsets", bad_32, 0);
        same("memory_fill_u64_aligned", "widths 0-300 at sixteen offsets", bad_u64, 0);
        same("memory_fill_64", "widths 0-300 at sixteen offsets", bad_64, 0);
}

/*
        Nothing asked of nothing: a null pointer and a size of zero, which
        the routines whose headers say a zero size touches neither pointer
        must take, and answer as they would for a real one. The pointers come
        through a variable the compiler cannot see through, so the call is
        made.
*/
fn check_null_zero()
{
        static address_any volatile nothing = null;
        address_any none = nothing;
        p8 address_to bytes = (p8 address_to)none;

        same("memory_copy", "null and zero answers null", (positive)memory_copy(none, none, 0), 0);
        same("memory_copy_apart", "null and zero answers null", (positive)memory_copy_apart(none, none, 0), 0);
        same("memory_fill", "null and zero answers null", (positive)memory_fill(none, 7, 0), 0);
        same("memcpy", "null and zero answers null", (positive)memcpy(none, none, 0), 0);
        same("memmove", "null and zero answers null", (positive)memmove(none, none, 0), 0);
        same("memset", "null and zero answers null", (positive)memset(none, 7, 0), 0);
        same("memcmp", "null and zero is equal", memcmp(none, none, 0), 0);
        same("bcmp", "null and zero is equal", bcmp(none, none, 0), 0);
        same("memchr", "null and zero finds nothing", (positive)memchr(none, 7, 0), 0);
        same("memrchr", "null and zero finds nothing", (positive)memrchr(none, 7, 0), 0);
        same("strnlen", "null and zero is zero", strnlen((char address_to)bytes, 0), 0);
        same("strncmp", "null and zero is equal", strncmp((char address_to)bytes, (char address_to)bytes, 0), 0);
        same("strncasecmp", "null and zero is equal", strncasecmp((char address_to)bytes, (char address_to)bytes, 0), 0);
        same("strncpy", "null and zero answers null", (positive)strncpy((char address_to)bytes, "x", 0), 0);
        same("memory_zero", "null and zero", (memory_zero(none, 0), 0), 0);
        same("bzero", "null and zero", (bzero(none, 0), 0), 0);
        same("bcopy", "null and zero", (bcopy(none, none, 0), 0), 0);
        same("memory_reverse", "null and zero answers null", (positive)memory_reverse(none, 0), 0);
        same("memory_frob", "null and zero answers null", (positive)memory_frob(none, 0), 0);
        same("memory_to_lower_ascii", "null and zero answers null", (positive)memory_to_lower_ascii(none, 0), 0);
        same("memory_to_upper_ascii", "null and zero answers null", (positive)memory_to_upper_ascii(none, 0), 0);
        same("memory_exchange_apart", "null and zero", (memory_exchange_apart(none, none, 0), 0), 0);
        same("memory_into_hex", "null and zero writes nothing", memory_into_hex(none, none, 0), 0);
        same("memory_hash_33", "null and zero is the seed", memory_hash_33(none, 0), memory_hash_33("", 0));
        same("memory_common_prefix", "null and zero is zero", memory_common_prefix(none, none, 0), 0);
        same("memory_compare_ascii_case", "null and zero is equal", memory_compare_ascii_case(none, none, 0), 0);
        same("memory_count", "null and zero is zero", memory_count(none, 0, 7), 0);
        same("memory_first_of", "null and zero finds nothing", (positive)memory_first_of(none, 7, 0), 0);
        same("memory_last_of", "null and zero finds nothing", (positive)memory_last_of(none, 7, 0), 0);
        same("memmem", "empty haystack, empty needle", (positive)memmem(none, 0, none, 0), 0);
        same("memccpy", "null and zero finds nothing", (positive)memccpy(none, none, 7, 0), 0);
        same("hash_xxh64", "null and zero is the empty digest", hash_xxh64(none, 0, 0), hash_xxh64("", 0, 0));
}

/*
        A bound that is no bound. strnlen(s, SIZE_MAX) is the idiom for "a
        string of whatever length", and the end of the span the bound names
        wraps below its start, so every routine that works out where the bound
        ends has to saturate it. The string is run against the end of a page,
        so a read, or a copy, that goes on past the terminator is a fault.
*/
fn check_huge_bounds()
{
        static const positive huge[] = {~0ull, ~0ull - 1, ~0ull - 64, 1ull << 63,
                                        (1ull << 63) - 1, 1ull << 40};
        p8 address_to pages = memory(7 * 4096);

        if (!((bipolar)(positive)pages > 0))
        {
                same("huge bounds", "mapping", 0, 1);
                return;
        }
        for (positive i = 0; i < 7; i += 2)
                system_call_3(syscall(mprotect), (positive)(pages + 4096 * i), 4096, 0);
        libc_a = pages + 4096;
        libc_b = pages + 3 * 4096;
        libc_c = pages + 5 * 4096;

        p8 had_avx2 = 0;
        p8 had_avx512 = 0;
        b8 set[256];

#if X64
        had_avx2 = cpu_has_avx2;
        had_avx512 = cpu_has_avx512;
#endif
        memory_fill(set, 0, sizeof(set));
        string_set_add(set, "a");

        for (positive pass = 0; pass < BULK_TIERS; pass++)
        {
        bulk_tier(pass, had_avx2, had_avx512);
        for (positive length = 0; length <= 300; length++)
                for (positive h = 0; h < sizeof(huge) / sizeof(huge[0]); h++)
                        for (positive a = 0; a < LIBC_SLACK; a += 2)
                        {
                                p8 address_to s = libc_place(libc_a, length + 1, libc_slack[a], 0);
                                p8 address_to t = libc_place(libc_b, length + 1, libc_slack[(a + 3) % LIBC_SLACK], 0);
                                p8 address_to d = libc_place(libc_c, length + 1, libc_slack[(a + 5) % LIBC_SLACK], 0);
                                positive bound = huge[h];

                                for (positive i = 0; i < length; i++)
                                        t[i] = s[i] = 'a' + (i & 15);
                                s[length] = t[length] = 0;

                                same("string_length_max", "huge bound", string_length_max((string_address)s, bound), length);
                                same("string_compare_max", "huge bound, equal",
                                     string_compare_max((string_address)s, (string_address)t, bound), 0);
                                same("string_compare_folded_max", "huge bound, equal",
                                     string_compare_folded_max((string_address)s, (string_address)t, bound), 0);
                                same("string_first_of_max", "huge bound, absent",
                                     (positive)string_first_of_max((string_address)s, bound, 'z'), 0);
                                same("string_first_of_max", "huge bound, the terminator",
                                     (positive)string_first_of_max((string_address)s, bound, 0), (positive)(s + length));
                                same("string_copy_max", "huge bound answers destination",
                                     (positive)string_copy_max((string_address)d, (string_address)s, bound), (positive)d);
                                same_bytes("string_copy_max", "huge bound bytes", d, s, length + 1);
                                same("string_copy_max_end", "huge bound answers the terminator",
                                     (positive)string_copy_max_end(d, (string_address)s, bound), (positive)(d + length));
                                same_bytes("string_copy_max_end", "huge bound bytes", d, s, length + 1);
                                d[0] = 0;
                                same("string_append_max", "huge bound answers destination",
                                     (positive)string_append_max((string_address)d, (string_address)s, bound), (positive)d);
                                same_bytes("string_append_max", "huge bound bytes", d, s, length + 1);
                                same("string_copy_bounded", "huge capacity answers the length",
                                     string_copy_bounded((string_address)d, (string_address)s, bound), length);
                                same_bytes("string_copy_bounded", "huge capacity bytes", d, s, length + 1);
                                d[0] = 0;
                                same("string_append_bounded", "huge capacity answers the length",
                                     string_append_bounded((string_address)d, (string_address)s, bound), length);
                                same_bytes("string_append_bounded", "huge capacity bytes", d, s, length + 1);
                                same("string_span_max", "huge bound counts the run of a",
                                     string_span_max((string_address)s, bound, set), s[0] == 'a');
                                {
                                        positive used = 77;

                                        same("string_digits_max", "huge bound, no digits",
                                             string_digits_max((string_address)s, bound, &used), 0);
                                }
                        }
        }

        // The digit readers, with the bound the string's own length gives and with none
        for (positive length = 0; length <= 40; length++)
                for (positive a = 0; a < LIBC_SLACK; a += 3)
                        for (positive kind = 0; kind < 6; kind++)
                        {
                                p8 address_to s = libc_place(libc_a, length + 1, libc_slack[a], 0);
                                static const char digits[] = "0123456789abcdefABCDEFxX7zZ89";

                                for (positive i = 0; i < length; i++)
                                        s[i] = digits[(next() >> 20) % (kind & 1 ? 10 : sizeof(digits) - 1)];
                                s[length] = 0;

                                for (positive h = 0; h < 3; h++)
                                {
                                        positive big = huge[h];
                                        positive used_big = 111;
                                        positive used_own = 222;
                                        positive got = 0;
                                        positive want = 0;

                                        switch (kind >> 1)
                                        {
                                        case 0:
                                                got = string_digits_max((string_address)s, big, &used_big);
                                                want = string_digits_max((string_address)s, length + 1, &used_own);
                                                break;
                                        case 1:
                                                got = string_digits_octal_max((string_address)s, big, &used_big);
                                                want = string_digits_octal_max((string_address)s, length + 1, &used_own);
                                                break;
                                        default:
                                                got = string_digits_hexadecimal_max((string_address)s, big, &used_big);
                                                want = string_digits_hexadecimal_max((string_address)s, length + 1, &used_own);
                                                break;
                                        }
                                        same("string_digits*_max", "huge bound, value", got, want);
                                        same("string_digits*_max", "huge bound, used", used_big, used_own);
                                }
                        }

        bulk_tier(0, had_avx2, had_avx512);
        memory_free(pages, 7 * 4096);
}

/*
        Sizes past four gigabytes, which a length held in thirty two bits
        gets wrong without anything near it failing. Two anonymous mappings
        of 4 GiB and a little more, never written except for the bytes the
        checks plant, so the pages read are the kernel's one zero page and the
        scans cost time and no memory. Each routine has its answer at the far
        end, or at the near end for the ones that look backwards, so the whole
        span is read to get to it.
*/
#define HUGE_SIZE ((4ull << 30) + 4096 + 77)

fn check_huge_sizes()
{
        p8 address_to one = memory(HUGE_SIZE);
        p8 address_to two = memory(HUGE_SIZE);
        p8 had_avx2 = 0;
        p8 had_avx512 = 0;

        if (!((bipolar)(positive)one > 0) || !((bipolar)(positive)two > 0))
        {
                // a machine that will not map this much says so and does not fail
                string_format(log, "  NOTE huge sizes: no 4 GiB mapping here, so none were tried\n");
                return;
        }
#if X64
        had_avx2 = cpu_has_avx2;
        had_avx512 = cpu_has_avx512;
#endif
        for (positive pass = 0; pass < BULK_TIERS; pass++)
        {
                bulk_tier(pass, had_avx2, had_avx512);

                one[HUGE_SIZE - 1] = 'z';
                one[HUGE_SIZE - 40] = 'z';
                one[5] = 'y';
                same("memory_first_of", "past 4 GiB", (positive)memory_first_of(one, 'z', HUGE_SIZE),
                     (positive)(one + HUGE_SIZE - 40));
                same("memory_last_of", "past 4 GiB", (positive)memory_last_of(one, 'y', HUGE_SIZE),
                     (positive)(one + 5));
                same("memory_count", "past 4 GiB", memory_count(one, HUGE_SIZE, 'z'), 2);
                same("memory_span_byte", "past 4 GiB", memory_span_byte(one, 0, HUGE_SIZE), 5);
                one[5] = 0;
                same("memory_span_byte", "past 4 GiB, far end", memory_span_byte(one, 0, HUGE_SIZE),
                     HUGE_SIZE - 40);
                same("memory_compare", "past 4 GiB, differs at the end",
                     libc_sign(memory_compare(one, two, HUGE_SIZE)), 1);
                same("memory_common_prefix", "past 4 GiB", memory_common_prefix(one, two, HUGE_SIZE),
                     HUGE_SIZE - 40);
                two[HUGE_SIZE - 40] = 'z';
                two[HUGE_SIZE - 1] = 'z';
                same("memory_compare", "past 4 GiB, equal", memory_compare(one, two, HUGE_SIZE), 0);
                same("memory_common_prefix", "past 4 GiB, equal", memory_common_prefix(one, two, HUGE_SIZE),
                     HUGE_SIZE);
                same("memory_sum_bytes", "past 4 GiB", memory_sum_bytes(one, HUGE_SIZE), 2 * 'z');
                one[HUGE_SIZE - 1] = 'Z';
                same("memory_first_of_ascii_case", "past 4 GiB",
                     (positive)memory_first_of_ascii_case(one, 'z', HUGE_SIZE), (positive)(one + HUGE_SIZE - 40));
                same("memory_last_of_either", "past 4 GiB, from the far end",
                     (positive)memory_last_of_either(one, 'y', 'Z', HUGE_SIZE), (positive)(one + HUGE_SIZE - 1));
                two[HUGE_SIZE - 1] = 'z';
                same("memory_compare_ascii_case", "past 4 GiB, folded equal",
                     memory_compare_ascii_case(one, two, HUGE_SIZE), 0);
                two[HUGE_SIZE - 1] = 'x';
                same("memory_compare_ascii_case", "past 4 GiB, differs at the end",
                     libc_sign(memory_compare_ascii_case(one, two, HUGE_SIZE)), 1);
                one[HUGE_SIZE - 1] = 'z';
                {
                        positive2 span = memory_utf8_span(two + 8, HUGE_SIZE - 8 - 40, ~0ull);

                        same("memory_utf8_span", "past 4 GiB, bytes", span.x, HUGE_SIZE - 8 - 40);
                        same("memory_utf8_span", "past 4 GiB, characters", span.y, HUGE_SIZE - 8 - 40);
                }
                one[HUGE_SIZE - 3] = 'q';
                one[HUGE_SIZE - 2] = 'r';
                one[HUGE_SIZE - 1] = 's';
                same("memory_search", "past 4 GiB", (positive)memory_search(one, HUGE_SIZE, "qrs", 3),
                     (positive)(one + HUGE_SIZE - 3));
                one[HUGE_SIZE - 3] = 0;
                one[HUGE_SIZE - 2] = 0;
                one[HUGE_SIZE - 1] = 0;
                one[HUGE_SIZE - 40] = 0;
                two[HUGE_SIZE - 1] = 0;
                two[HUGE_SIZE - 40] = 0;
        }

        bulk_tier(0, had_avx2, had_avx512);
        memory_free(one, HUGE_SIZE);
        memory_free(two, HUGE_SIZE);
}

fn check_bulk_moves()
{
        p8 wide = 0;
        p8 widest = 0;

#if X64
        wide = cpu_has_avx2;
        widest = cpu_has_avx512;
#endif

        for (positive i = 0; i < BULK_ROOM; i++)
                bulk_from[i] = (b8)(i * 29 + 11);

        for (positive pass = 0; pass < BULK_TIERS; pass++)
        {
                bulk_tier(pass, wide, widest);

                for (positive e = 0; e < BULK_MOVE_COUNT; e++)
                {
                        positive size = bulk_move_sizes[e];

                        for (positive off = 0; off < 72; off++)
                                for (positive g = 0; g < BULK_GAP_COUNT; g++)
                                {
                                        b32 gap = bulk_gaps[g];
                                        positive base = 200 + off;

                                        if ((b32)base + gap < 0)
                                                continue;

                                        if (base + size + 8 > BULK_ROOM)
                                                continue;

                                        if ((positive)((b32)base + gap) + size + 8 > BULK_ROOM)
                                                continue;

                                        reference_copy(bulk_got, bulk_from, BULK_ROOM);
                                        reference_copy(bulk_want, bulk_from, BULK_ROOM);

                                        memory_copy(bulk_got + base + gap, bulk_got + base, size);
                                        reference_copy(bulk_want + base + gap, bulk_want + base, size);

                                        bulk_same("memory_copy", pass, size, off);
                                }
                }
        }

        bulk_tier(0, wide, widest);
}

/*
        The four string routines against the C they replaced, at every
        alignment and either side of every width they switch strategy at.

        string_copy_max gets a bound that runs past the end of the source and
        one that stops short of it, and the buffer is poisoned to the end
        rather than to 64 bytes: a wide store that ignores the bound writes 32
        or 64 bytes past it, which a check that stops at 64 cannot see.
*/
static p8 bulk_subject[BULK_ROOM];
static p8 bulk_spare[BULK_ROOM];

fn check_bulk_wide_strings()
{
        p8 wide = 0;
        p8 widest = 0;

#if X64
        wide = cpu_has_avx2;
        widest = cpu_has_avx512;
#endif

        for (positive pass = 0; pass < BULK_TIERS; pass++)
        {
                bulk_tier(pass, wide, widest);

                for (positive e = 0; e < BULK_SIZE_COUNT; e++)
                {
                        positive size = bulk_sizes[e];

                        for (positive off = 0; off < 72; off++)
                        {
                                if (off + size + 200 > BULK_ROOM)
                                        continue;

                                p8 address_to text = bulk_subject + off;

                                for (positive i = 0; i < size; i++)
                                        text[i] = (p8)(next() % 4 + 'a');

                                text[size] = 0;

                                // string_copy: length, content, and not a byte
                                // written past the terminator it copied.
                                for (positive d = 0; d < 3; d++)
                                {
                                        reference_fill(bulk_spare, 0xA5, BULK_ROOM);
                                        string_copy(bulk_spare + d, (string_address)text);

                                        checks++;

                                        if (reference_length(bulk_spare + d) != size)
                                                report("string_copy", "length",
                                                       reference_length(bulk_spare + d), size);

                                        checks++;

                                        for (positive i = 0; i < size; i++)
                                                if (bulk_spare[d + i] != text[i])
                                                {
                                                        report("string_copy", "content", i, size);
                                                        break;
                                                }

                                        checks++;

                                        for (positive i = d + size + 1; i < BULK_ROOM; i++)
                                                if (bulk_spare[i] != 0xA5)
                                                {
                                                        report("string_copy", "wrote past the terminator",
                                                               i, size);
                                                        break;
                                                }
                                }

                                // string_copy_max at every bound around the
                                // length, not only the first 64.
                                for (positive b = 0; b < 6; b++)
                                {
                                        positive limit = size + 2 > b ? size + 2 - b : 0;

                                        if (limit + 8 > BULK_ROOM)
                                                continue;

                                        reference_fill(bulk_spare, 0xA5, BULK_ROOM);
                                        string_copy_max(bulk_spare, (string_address)text, limit);

                                        checks++;

                                        positive want = size < limit ? size : limit;

                                        for (positive i = 0; i < want; i++)
                                                if (bulk_spare[i] != text[i])
                                                {
                                                        report("string_copy_max", "content", i, limit);
                                                        break;
                                                }

                                        checks++;

                                        // A terminator only where the source
                                        // ended inside the bound, and nothing
                                        // at all past the bound.
                                        positive first = size < limit ? want + 1 : limit;

                                        if (size < limit && bulk_spare[want] != 0)
                                        {
                                                checks++;
                                                report("string_copy_max", "no terminator",
                                                       (positive)bulk_spare[want], limit);
                                        }

                                        for (positive i = first; i < BULK_ROOM; i++)
                                                if (bulk_spare[i] != 0xA5)
                                                {
                                                        report("string_copy_max", "wrote past the limit",
                                                               i, limit);
                                                        break;
                                                }
                                }

                                // string_cut and string_replace_all change the
                                // string, so each gets its own copy of it -- at
                                // the same offset the source is at, because both
                                // walk to a block boundary before they widen and
                                // a string that always starts aligned never
                                // enters that walk. A one byte error in it
                                // passed the whole file until this offset was
                                // anything but zero.
                                for (positive c = 0; c < 2; c++)
                                {
                                        b8 symbol = c ? 'b' : 'q';
                                        p8 address_to mine = bulk_spare + off;

                                        reference_fill(bulk_spare, 0xA5, BULK_ROOM);

                                        for (positive i = 0; i <= size; i++)
                                                mine[i] = text[i];

                                        // where the cut lands, read before the
                                        // cut writes a terminator over it
                                        string_address where = reference_first_of(
                                            (string_address)mine, (p8)symbol);
                                        positive at = where ? (positive)(where - (string_address)mine) : 0;
                                        p8 following = where ? mine[at + 1] : 0;

                                        string_address got = string_cut((string_address)mine, symbol);

                                        same("string_cut", "answer", (positive)got,
                                             where == null || following == 0
                                                 ? 0
                                                 : (positive)((string_address)mine + at + 1));

                                        checks++;

                                        if (where && mine[at] != 0)
                                                report("string_cut", "did not terminate", size, 0);

                                        checks++;

                                        // and nothing outside the string moved
                                        for (positive i = 0; i < BULK_ROOM; i++)
                                        {
                                                if (i >= off && i <= off + size)
                                                        continue;

                                                if (bulk_spare[i] != 0xA5)
                                                {
                                                        report("string_cut", "wrote outside the string",
                                                               i, size);
                                                        break;
                                                }
                                        }

                                        reference_fill(bulk_spare, 0xA5, BULK_ROOM);

                                        for (positive i = 0; i <= size; i++)
                                                mine[i] = text[i];

                                        string_replace_all((string_address)mine, symbol, 'z');

                                        checks++;

                                        for (positive i = 0; i < size; i++)
                                                if (mine[i] != (text[i] == symbol ? 'z' : text[i]))
                                                {
                                                        report("string_replace_all", "byte", i, size);
                                                        break;
                                                }

                                        checks++;

                                        for (positive i = 0; i < BULK_ROOM; i++)
                                        {
                                                if (i >= off && i < off + size)
                                                        continue;

                                                if (bulk_spare[i] != (i == off + size ? 0 : 0xA5))
                                                {
                                                        report("string_replace_all", "wrote outside the string",
                                                               i, size);
                                                        break;
                                                }
                                        }
                                }
                        }
                }
        }

        bulk_tier(0, wide, widest);
}

/*
        memory_compare and memory_search, and the sizes that reach the paths.

        Both have a body per width. memory_compare walks thirty two bytes at a
        time where the processor has the registers for it, eight where it does
        not and one below that; memory_search compares a whole block of
        positions at once above a length and hunts a byte at a time below it.
        A case that stays short never enters the wide half at all, and a one
        byte error in the wide half of memory_first_of once passed the whole
        suite -- 263472 checks -- for exactly that reason. So the lengths here
        run either side of every width, and on x86_64 the whole set runs a
        second time with cpu_has_avx2 forced off, because that is the only way
        the narrow body is reached on a machine that has the wide one.

        The references are the dumbest thing that answers the question: a byte
        loop, and a triple loop that tries every start. Written that way on
        purpose. string_find and the C beside it once had the same bug -- a
        failed candidate resuming past the bytes it had matched, so that "aab"
        was not found in "aaab" -- and the tests agreed with the bug because
        the reference was clever in the same way.
*/
b32 reference_memory_compare(address_any first, address_any second, positive size)
{
        p8 address_to a = first;
        p8 address_to b = second;

        for (positive i = 0; i < size; i++)
                if (a[i] != b[i])
                        return (b32)a[i] - (b32)b[i];

        return 0;
}

address_any reference_memory_search(address_any block, positive size,
                                    address_any needle, positive needle_size)
{
        p8 address_to hay = block;
        p8 address_to want = needle;

        if (needle_size == 0)
                return block;

        if (needle_size > size)
                return null;

        for (positive at = 0; at + needle_size <= size; at++)
        {
                positive i = 0;

                while (i < needle_size && hay[at + i] == want[i])
                        i++;

                if (i == needle_size)
                        return hay + at;
        }

        return null;
}

static p8 compare_room[400];
static p8 compare_twin[400];

fn check_memory_compare()
{
        for (positive off = 0; off < 5; off++)
                for (positive size = 0; size <= 140; size++)
                {
                        for (positive i = 0; i < 400; i++)
                        {
                                compare_room[i] = (p8)('a' + (i % 7));
                                compare_twin[i] = compare_room[i];
                        }

                        same("memory_compare", "equal",
                             (positive)memory_compare(compare_room + off,
                                                      compare_twin + off, size),
                             (positive)reference_memory_compare(compare_room + off,
                                                                compare_twin + off,
                                                                size));

                        //      Every position for the short ones, and the
                        //      edges of every block for the long ones: the
                        //      first byte, the last, and either side of each
                        //      thirty two and eight byte step.
                        for (positive where = 0; where < size; where++)
                        {
                                if (size > 48 && where > 3 && where + 4 < size &&
                                    (where % 8) > 1 && (where % 32) > 1)
                                        continue;

                                //      Two directions, because the sign of
                                //      the answer is the order of the two
                                //      strings and a compare that gets the
                                //      difference right can still get that
                                //      backwards.
                                compare_twin[off + where] =
                                    (p8)(compare_room[off + where] ^ 0x80);

                                same("memory_compare", "differs high",
                                     (positive)memory_compare(compare_room + off,
                                                              compare_twin + off, size),
                                     (positive)reference_memory_compare(
                                         compare_room + off, compare_twin + off, size));

                                compare_twin[off + where] =
                                    (p8)(compare_room[off + where] + 1);

                                same("memory_compare", "differs by one",
                                     (positive)memory_compare(compare_room + off,
                                                              compare_twin + off, size),
                                     (positive)reference_memory_compare(
                                         compare_room + off, compare_twin + off, size));

                                //      And one past the size, which must not
                                //      be looked at.
                                compare_twin[off + where] = compare_room[off + where];
                        }

                        compare_twin[off + size] = 0xff;

                        same("memory_compare", "one past the size",
                             (positive)memory_compare(compare_room + off,
                                                      compare_twin + off, size),
                             (positive)reference_memory_compare(compare_room + off,
                                                                compare_twin + off,
                                                                size));

                        compare_twin[off + size] = compare_room[off + size];
                }
}

#if RISCV64
/*
        The RV64 base ISA does not require naturally misaligned integer loads
        and stores to complete.  QEMU and many Linux systems emulate them, so
        a result-only test can agree while the shipped instruction stream is
        still outside the floor.  test/run supplies the disassembly half
        of this check; this is the semantic half.

        Every pair of pointer residues is crossed here.  Equal residues reach
        the aligned word paths after a byte peel, unequal residues can never
        align in lockstep and must remain bytewise.  The ordinary bulk tests
        cover many offsets, but did not cross all 64 pairs for compares or all
        16 overlap distances for memmove.
*/
#define RV_ALIGN_ROOM 512
#define RV_PAGE_ROOM (4096 * 3)

static p8 rv_align_left[RV_ALIGN_ROOM];
static p8 rv_align_right[RV_ALIGN_ROOM];
static p8 rv_align_got[RV_ALIGN_ROOM];
static p8 rv_align_want[RV_ALIGN_ROOM];
static p8 rv_align_from[RV_ALIGN_ROOM];
static p8 rv_page_left[RV_PAGE_ROOM];
static p8 rv_page_right[RV_PAGE_ROOM];
static p8 rv_page_got[RV_PAGE_ROOM];
static p8 rv_page_want[RV_PAGE_ROOM];

positive reference_trailing_positive(string_address input);

static p8 address_to rv_page(p8 address_to room)
{
        positive address = (positive)(address_any)room;

        return (p8 address_to)((address + 4095) & ~(positive)4095);
}

fn check_riscv_alignment_floor()
{
        static const positive bounds[] = {0, 1, 7, 8, 9, 31, 32, 33, 80, 88};

        for (positive left_offset = 0; left_offset < 8; left_offset++)
                for (positive right_offset = 0; right_offset < 8; right_offset++)
                        for (positive size = 0; size <= 80; size++)
                        {
                                p8 address_to left = aligned_at(rv_align_left) + 64 +
                                                     left_offset;
                                p8 address_to right = aligned_at(rv_align_right) + 64 +
                                                      right_offset;

                                for (positive i = 0; i < 96; i++)
                                {
                                        p8 value = (p8)('a' + (i * 5 + size) % 23);

                                        left[i] = value;
                                        right[i] = value;
                                }

                                left[size] = 0;
                                right[size] = 0;

                                same("memory_compare", "every pair of RV64 residues",
                                     (positive)memory_compare(left, right, size),
                                     (positive)reference_memory_compare(left, right,
                                                                        size));
                                same("string_compare", "every pair of RV64 residues",
                                     (positive)string_compare(left, right),
                                     (positive)reference_compare(left, right));

                                for (positive b = 0;
                                     b < sizeof(bounds) / sizeof(bounds[0]); b++)
                                        same("string_compare_max",
                                             "every pair of RV64 residues",
                                             (positive)string_compare_max(left, right,
                                                                          bounds[b]),
                                             (positive)reference_compare_max(left, right,
                                                                             bounds[b]));

                                for (positive where = 0; where < size; where++)
                                {
                                        right[where] = (p8)(left[where] ^ 0x80);

                                        same("memory_compare", "a difference at every residue",
                                             (positive)memory_compare(left, right, size),
                                             (positive)reference_memory_compare(left, right,
                                                                                size));
                                        same("string_compare", "a difference at every residue",
                                             (positive)string_compare(left, right),
                                             (positive)reference_compare(left, right));
                                        same("string_compare_max",
                                             "a bounded difference at every residue",
                                             (positive)string_compare_max(left, right, size),
                                             (positive)reference_compare_max(left, right,
                                                                             size));

                                        right[where] = left[where];
                                }
                        }

        // Every source/destination residue, every short size, and guards on
        // the complete arena.  Sizes through 80 cross every 4/8/16/32-byte
        // transition in the aligned core as well as the byte fallback.
        for (positive destination_offset = 0; destination_offset < 8;
             destination_offset++)
                for (positive source_offset = 0; source_offset < 8; source_offset++)
                        for (positive size = 0; size <= 80; size++)
                        {
                                p8 address_to got = aligned_at(rv_align_got) + 64 +
                                                    destination_offset;
                                p8 address_to want = aligned_at(rv_align_want) + 64 +
                                                     destination_offset;
                                p8 address_to from = aligned_at(rv_align_from) + 64 +
                                                     source_offset;

                                for (positive i = 0; i < RV_ALIGN_ROOM; i++)
                                {
                                        rv_align_got[i] = 0xa5;
                                        rv_align_want[i] = 0xa5;
                                        rv_align_from[i] = (p8)(i * 29 + size);
                                }

                                memory_copy_apart(got, from, size);
                                reference_copy(want, from, size);
                                same_bytes("memory_copy_apart", "every pair of RV64 residues",
                                           rv_align_got, rv_align_want, RV_ALIGN_ROOM);

                                if (source_offset == 0)
                                {
                                        memory_fill(got, (b8)(size + 3), size);
                                        reference_fill(want, (b8)(size + 3), size);
                                        same_bytes("memory_fill", "every RV64 residue",
                                                   rv_align_got, rv_align_want,
                                                   RV_ALIGN_ROOM);
                                }
                        }

        // Backwards and forwards overlap at every distance modulo eight.
        for (positive offset = 0; offset < 8; offset++)
                for (positive distance = 1; distance <= 16; distance++)
                        for (positive size = 0; size <= 96; size++)
                        {
                                positive base = 128 + offset;

                                for (positive i = 0; i < RV_ALIGN_ROOM; i++)
                                        rv_align_got[i] = rv_align_want[i] =
                                            (p8)(i * 13 + size);

                                memory_copy(rv_align_got + base + distance,
                                            rv_align_got + base, size);
                                reference_copy(rv_align_want + base + distance,
                                               rv_align_want + base, size);
                                same_bytes("memory_copy", "every backward RV64 residue",
                                           rv_align_got, rv_align_want, RV_ALIGN_ROOM);

                                for (positive i = 0; i < RV_ALIGN_ROOM; i++)
                                        rv_align_got[i] = rv_align_want[i] =
                                            (p8)(i * 17 + size);

                                memory_copy(rv_align_got + base,
                                            rv_align_got + base + distance, size);
                                reference_copy(rv_align_want + base,
                                               rv_align_want + base + distance, size);
                                same_bytes("memory_copy", "every forward RV64 residue",
                                           rv_align_got, rv_align_want, RV_ALIGN_ROOM);
                        }

        /*
                Put the exclusive end at an actual 4096-byte boundary.  The
                sixteen bytes beyond it are hostile rather than zero, so a
                scan that skips the terminator or a bulk operation that leaks
                across its bound is visible even on a host that maps the next
                page.  The static audit separately proves the final wide
                access itself is naturally aligned.
        */
        {
                p8 address_to left_edge = rv_page(rv_page_left) + 4096;
                p8 address_to right_edge = rv_page(rv_page_right) + 4096;
                p8 address_to got_edge = rv_page(rv_page_got) + 4096;
                p8 address_to want_edge = rv_page(rv_page_want) + 4096;

                for (positive residue = 0; residue < 8; residue++)
                {
                        positive size = 64 + residue;
                        p8 address_to left = left_edge - size;
                        p8 address_to right = right_edge - size;
                        p8 address_to got = got_edge - size;
                        p8 address_to want = want_edge - size;

                        for (positive i = 0; i < size; i++)
                                left[i] = right[i] = (p8)('a' + (i % 17));

                        left[size - 1] = 0;
                        right[size - 1] = 0;

                        for (positive i = 0; i < 16; i++)
                        {
                                left_edge[i] = 0xe1;
                                right_edge[i] = 0xe2;
                                got_edge[i] = 0xe3;
                                want_edge[i] = 0xe3;
                        }

                        same("string_compare", "a terminator at a page edge",
                             (positive)string_compare(left, right),
                             (positive)reference_compare(left, right));
                        same("string_compare_max", "a bound at a page edge",
                             (positive)string_compare_max(left, right, size),
                             (positive)reference_compare_max(left, right, size));
                        same("memory_compare", "a block ending at a page edge",
                             (positive)memory_compare(left, right, size),
                             (positive)reference_memory_compare(left, right, size));
                        same("memory_count", "a block ending at a page edge",
                             memory_count(left, size, 'a'),
                             reference_count(left, size, 'a'));

                        for (positive i = 0; i < size; i++)
                                got[i] = want[i] = 0xa5;

                        memory_copy_apart(got, left, size);
                        reference_copy(want, left, size);
                        same_bytes("memory_copy_apart", "an end at a page edge",
                                   got, want, size + 16);

                        memory_fill(got, (b8)(residue + 1), size);
                        reference_fill(want, (b8)(residue + 1), size);
                        same_bytes("memory_fill", "an end at a page edge",
                                   got, want, size + 16);

                        for (positive i = 0; i + 1 < size; i++)
                                left[i] = (p8)('0' + (i * 7 + residue) % 10);
                        left[size - 1] = 0;

                        same("string_to_positive", "a terminator at a page edge",
                             string_to_positive(left),
                             reference_trailing_positive(left));
                }
        }
}
#endif

static p8 search_room[400];
static p8 search_needle[200];

//      Lengths either side of both block widths -- sixteen on arm64, thirty
//      two on x86_64 -- and of the four byte probe inside the candidate step.
static const positive search_lengths[] = {1,  2,  3,  4,  5,  6,  7,  8,  15, 16,
                                          17, 31, 32, 33, 40, 63, 64, 65, 100};

/*
        memory_search and memory_search_ascii_case against the byte loops
        with the haystack against a page that faults at either end: every
        size to 200 and long ones, needles of two bytes to thirty three cut
        from the haystack, so present, and the same with one byte changed, so
        mostly near misses, the folded search's needle with letters in the
        other case at random, over bytes from an alphabet of four and of all
        256. On x86_64 with AVX2 and without, where the SSE2 block loops take
        sixty four starts a block.
*/
static address_any reference_memory_search_ascii_case(address_any block, positive size,
                                                        address_any needle,
                                                        positive needle_size);

fn check_memory_search_pages()
{
        static const positive needles[] = {2, 3, 4, 5, 8, 15, 16, 17, 33};
        static const positive longer[] = {511, 1000, 4096};
        static p8 needle[64];
        p8 address_to pages = guard_pages("memory_search pages");

        if (!pages)
                return;
        p64 seed = 0x7a3c5e1f9b2d4c68ull;
#if X64
        p8 avx2 = cpu_has_avx2;
        positive tiers = 2;
#else
        positive tiers = 1;
#endif
        for (positive tier = 0; tier < tiers; tier++)
        {
#if X64
                cpu_has_avx2 = tier ? 0 : avx2;
#endif
                for (positive s = 0; s <= 200 + array_count(longer); s++)
                        for (positive edge = 0; edge < 2; edge++)
                                for (positive shape = 0; shape < 2; shape++)
                                        for (positive n = 0; n < array_count(needles); n++)
                                        {
                                                positive size = s <= 200 ? s : longer[s - 201];
                                                positive needle_size = needles[n];
                                                p8 address_to hay = edge ? pages + 8192 - size : pages + 4096;

                                                for (positive i = 0; i < size; i++)
                                                {
                                                        XORSHIFT64(seed);
                                                        hay[i] = shape ? (p8)seed : (p8)("abcd"[seed % 4]);
                                                }
                                                for (positive miss = 0; miss < 2; miss++)
                                                {
                                                        XORSHIFT64(seed);
                                                        positive from = size >= needle_size ? (positive)(seed % (size - needle_size + 1)) : 0;
                                                        for (positive i = 0; i < needle_size; i++)
                                                                needle[i] = i + from < size ? hay[from + i] : 'a';
                                                        if (miss)
                                                                needle[(seed >> 32) % needle_size] ^= 1;
                                                        same("memory_search pages", "answer",
                                                             (positive)memory_search(hay, size, needle, needle_size),
                                                             (positive)reference_memory_search(hay, size, needle, needle_size));
                                                        // Folded: the letters of the needle in the other case at random.
                                                        for (positive i = 0; i < needle_size; i++)
                                                                if (((seed >> (i % 64)) & 1) && byte_is_alpha(needle[i]))
                                                                        needle[i] ^= 0x20;
                                                        same("memory_search_ascii_case pages", "answer",
                                                             (positive)memory_search_ascii_case(hay, size, needle, needle_size),
                                                             (positive)reference_memory_search_ascii_case(hay, size, needle,
                                                                                                          needle_size));
                                                }
                                        }
        }
#if X64
        cpu_has_avx2 = avx2;
#endif
        memory_free(pages, 3 * 4096);
}

fn check_memory_search()
{
        static const string_address alphabets[] = {"ab", "abc", "aaab"};

        //      The empty needle, which memmem answers with the front of the
        //      haystack. string_find disagrees on purpose and has its own
        //      cases elsewhere.
        same("memory_search", "an empty needle",
             (positive)memory_search(search_room, 10, search_room, 0),
             (positive)reference_memory_search(search_room, 10, search_room, 0));

        same("memory_search", "an empty needle in an empty haystack",
             (positive)memory_search(search_room, 0, search_room, 0),
             (positive)reference_memory_search(search_room, 0, search_room, 0));

        for (positive a = 0; a < 3; a++)
        {
                string_address letters = alphabets[a];
                positive count = reference_length(letters);

                for (positive i = 0; i < 400; i++)
                        search_room[i] = (p8)letters[i % count];

                for (positive off = 0; off < 3; off++)
                        for (positive n = 0; n < 19; n++)
                        {
                                positive needle_size = search_lengths[n];

                                for (positive size = 0; size <= 200; size += 7)
                                {
                                        //      A needle that is not there at
                                        //      all: the whole bound has to be
                                        //      walked, including the piece
                                        //      past the last whole block.
                                        for (positive i = 0; i < needle_size; i++)
                                                search_needle[i] = 'z';

                                        same("memory_search", "absent",
                                             (positive)memory_search(search_room + off, size,
                                                                     search_needle, needle_size),
                                             (positive)reference_memory_search(
                                                 search_room + off, size, search_needle,
                                                 needle_size));

                                        //      A needle longer than what is
                                        //      left, which must answer
                                        //      nothing rather than read past
                                        //      the end.
                                        if (needle_size > size)
                                                continue;

                                        //      And one that is there, at
                                        //      every position it could sit --
                                        //      the very first, the very last,
                                        //      and every one between.
                                        for (positive at = 0; at + needle_size <= size; at++)
                                        {
                                                for (positive i = 0; i < needle_size; i++)
                                                        search_needle[i] =
                                                            search_room[off + at + i];

                                                same("memory_search", "present",
                                                     (positive)memory_search(
                                                         search_room + off, size,
                                                         search_needle, needle_size),
                                                     (positive)reference_memory_search(
                                                         search_room + off, size,
                                                         search_needle, needle_size));
                                        }
                                }
                        }
        }

        //      A needle whose two ends are the same byte, which is the pair
        //      the wide path hunts when it has nothing rarer to pick, and a
        //      needle that overlaps itself.
        {
                static const string_address pairs[][2] = {
                    {"aaab", "aab"},
                    {"aaaab", "aab"},
                    {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab"},
                    {"xaxaxaxb", "axb"},
                    {"abababababc", "ababc"},
                    {"aaaaa", "aaa"},
                    {"aa", "aaa"},
                    {"", "a"},
                    {"a", "a"},
                    {"ababa", "aba"},
                    {"the quick brown fox jumps over the lazy dog", "the lazy"},
                    {"the quick brown fox jumps over the lazy dog", "tot"},
                    {"zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzq", "zzq"},
                };

                for (positive i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++)
                {
                        string_address hay = pairs[i][0];
                        string_address needle = pairs[i][1];
                        positive hay_size = reference_length(hay);
                        positive needle_size = reference_length(needle);

                        same("memory_search", "a match inside a failed candidate",
                             (positive)memory_search((address_any)hay, hay_size,
                                                     (address_any)needle, needle_size),
                             (positive)reference_memory_search((address_any)hay, hay_size,
                                                               (address_any)needle,
                                                               needle_size));
                }
        }

        //      Every byte value as the needle's first and last, so that no
        //      answer depends on the broadcast of a particular byte -- 0xff
        //      and 0x00 among them, which are what a mask built the wrong way
        //      round gets wrong.
        for (positive value = 0; value < 256; value += 5)
        {
                for (positive i = 0; i < 300; i++)
                        search_room[i] = (p8)value;

                for (positive n = 0; n < 12; n++)
                {
                        positive needle_size = search_lengths[n];

                        for (positive i = 0; i < needle_size; i++)
                                search_needle[i] = (p8)value;

                        search_room[200] = (p8)(value ^ 1);

                        same("memory_search", "one byte over and over",
                             (positive)memory_search(search_room, 260, search_needle,
                                                     needle_size),
                             (positive)reference_memory_search(search_room, 260,
                                                               search_needle, needle_size));

                        search_room[200] = (p8)value;
                }
        }
}

static p8 reference_ascii_fold(p8 value)
{
        return value >= 'A' && value <= 'Z' ? (p8)(value + 32) : value;
}

static address_any reference_memory_first_ascii_case(address_any block, b8 value,
                                                       positive size)
{
        p8 address_to bytes = block;
        p8 wanted = reference_ascii_fold((p8)value);

        for (positive i = 0; i < size; i++)
                if (reference_ascii_fold(bytes[i]) == wanted)
                        return bytes + i;

        return null;
}

static address_any reference_memory_search_ascii_case(address_any block, positive size,
                                                        address_any needle,
                                                        positive needle_size)
{
        p8 address_to hay = block;
        p8 address_to want = needle;

        if (!needle_size)
                return block;

        if (needle_size > size)
                return null;

        for (positive at = 0; at + needle_size <= size; at++)
        {
                positive i = 0;

                while (i < needle_size &&
                       reference_ascii_fold(hay[at + i]) ==
                           reference_ascii_fold(want[i]))
                        i++;

                if (i == needle_size)
                        return hay + at;
        }

        return null;
}

/*
        The folded byte hunt and substring search.

        Every target byte crosses every short alignment and bound.  The long
        cases cross both vector widths and put plausible matches at every
        candidate position; high bytes beside ASCII letters prove that OR 32
        is only used to recognize a letter and never to fold punctuation or
        the upper half of the byte range.
*/
fn check_memory_search_ascii_case()
{
        static p8 room[448];
        static p8 needle[80];

        same("memory_first_of_ascii_case", "zero bound null block",
             (positive)memory_first_of_ascii_case(null, 'a', 0), 0);
        same("memory_search_ascii_case", "empty needle",
             (positive)memory_search_ascii_case(room, 20, null, 0),
             (positive)room);
        same("memory_search_ascii_case", "empty both",
             (positive)memory_search_ascii_case(room, 0, null, 0),
             (positive)room);

        for (positive target = 0; target < 256; target++)
                for (positive offset = 0; offset < 9; offset++)
                {
                        for (positive i = 0; i < sizeof(room); i++)
                                room[i] = (p8)(i * 37 + offset * 11);

                        for (positive size = 0; size <= 96; size += 3)
                                same("memory_first_of_ascii_case",
                                     "every byte, alignment and bound",
                                     (positive)memory_first_of_ascii_case(
                                         room + offset, (b8)target, size),
                                     (positive)reference_memory_first_ascii_case(
                                         room + offset, (b8)target, size));
                }

        //      Every width to 300 at every alignment from 0 to 63, for a letter of each case, other
        //      punctuation and the upper half of the byte range: the bodies cut their blocks by where the
        //      span starts as well as by its width.
        {
                static const p8 targets[] = {'a', 'Z', 'm', '0', ' ', '[', 0x80, 0xff};

                for (positive ti = 0; ti < sizeof(targets); ti++)
                        for (positive offset = 0; offset < 64; offset++)
                                for (positive size = 0; size <= 300; size++)
                                {
                                        for (positive i = 0; i < sizeof(room); i++)
                                                room[i] = (p8)(i * 37 + offset * 11 + ti);

                                        same("memory_first_of_ascii_case", "every width and alignment",
                                             (positive)memory_first_of_ascii_case(room + offset, targets[ti], size),
                                             (positive)reference_memory_first_ascii_case(room + offset, targets[ti], size));
                                }
        }

        for (positive offset = 0; offset < 7; offset++)
                for (positive size = 0; size <= 160; size += 5)
                {
                        for (positive i = 0; i < sizeof(room); i++)
                        {
                                static const p8 alphabet[] = {
                                    'a', 'B', 'c', 'D', 'e', ' ', '[', '`',
                                    '{', 0, 0x7f, 0x80, 0xa0, 0xff,
                                };

                                room[i] = alphabet[(i * 5 + offset) % sizeof(alphabet)];
                        }

                        for (positive needle_size = 0; needle_size <= 40;
                             needle_size += 3)
                        {
                                for (positive i = 0; i < needle_size; i++)
                                        needle[i] = (p8)(0xe0 + (i % 13));

                                same("memory_search_ascii_case", "absent and bounded",
                                     (positive)memory_search_ascii_case(
                                         room + offset, size, needle, needle_size),
                                     (positive)reference_memory_search_ascii_case(
                                         room + offset, size, needle, needle_size));

                                if (needle_size > size)
                                        continue;

                                for (positive at = 0; at + needle_size <= size;
                                     at += 7)
                                {
                                        for (positive i = 0; i < needle_size; i++)
                                        {
                                                p8 value = room[offset + at + i];

                                                if (value >= 'a' && value <= 'z' && (i & 1))
                                                        value -= 32;
                                                else if (value >= 'A' && value <= 'Z' &&
                                                         !(i & 1))
                                                        value += 32;

                                                needle[i] = value;
                                        }

                                        same("memory_search_ascii_case",
                                             "present at every candidate region",
                                             (positive)memory_search_ascii_case(
                                                 room + offset, size, needle,
                                                 needle_size),
                                             (positive)reference_memory_search_ascii_case(
                                                 room + offset, size, needle,
                                                 needle_size));
                                }
                        }
                }

        {
                static const string_address pairs[][2] = {
                    {"aaaB", "AaB"},
                    {"abABaba", "aBaBa"},
                    {"the SAME thing twice", "Same Thing"},
                    {"[`{", "[`{"},
                };

                for (positive i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++)
                {
                        positive hay_size = reference_length(pairs[i][0]);
                        positive needle_size = reference_length(pairs[i][1]);

                        same("memory_search_ascii_case", "overlap and punctuation",
                             (positive)memory_search_ascii_case(
                                 (address_any)pairs[i][0], hay_size,
                                 (address_any)pairs[i][1], needle_size),
                             (positive)reference_memory_search_ascii_case(
                                 (address_any)pairs[i][0], hay_size,
                                 (address_any)pairs[i][1], needle_size));
                }
        }
}


/*
        string_find over a string longer than one chunk.

        string_find asks memory_search about sixty four kilobytes at a time
        and finds the terminator a page at a time, so every case in this file
        that fits in a line reaches neither loop a second time. A needle lying
        across a chunk seam, or a terminator on the last byte of a page, is
        the only thing that says the overlap and the page walk are right.

        Where those two places are is worked out from the address the linker
        gave the array rather than assumed, and that is the whole reason this
        was rewritten: with the positions written down as constants, both
        mutations -- a page read one byte short, and a chunk advanced one byte
        too far -- passed. The seam is wherever the page walk first reaches
        sixty four kilobytes, which depends on how far into a page the array
        begins, and hard coded 65536 is that place only by luck.

        The answer is known rather than computed: the background is seven
        letters repeating and every needle carries a 'z', which the background
        does not, so the only place it can be found is where it was put. A
        reference walk over two hundred thousand bytes would say the same
        thing and take an emulator a long time to say it.
*/
static p8 long_room[200000];

#define LONG_ROOM_SIZE (sizeof(long_room) - 1)

fn long_room_fill()
{
        for (positive i = 0; i < sizeof(long_room); i++)
                long_room[i] = (p8)('a' + (i % 7));

        long_room[LONG_ROOM_SIZE] = 0;
}

fn check_find_long()
{
        static positive places[64];
        static const positive sizes[] = {2, 3, 4, 5, 8, 33, 64};
        static p8 needle[80];

        positive base = (positive)(address_any)long_room;
        positive page = (4096 - (base & 4095)) & 4095;
        positive seam;
        positive count = 0;

        //      The first page boundary inside the array, and the seam where
        //      string_find stops taking pages and searches what it has.
        if (page == 0)
                page = 4096;

        seam = page;

        while (seam < 65536)
                seam += 4096;

        places[count++] = 0;
        places[count++] = 1;
        places[count++] = 100;

        for (positive i = 0; i < 5; i++)
        {
                places[count++] = page - 4 + i;
                places[count++] = page + 4092 + i;
                places[count++] = seam - 68 + i;
                places[count++] = seam - 4 + i;
                places[count++] = seam + 4092 + i;
        }

        places[count++] = 2 * seam - 3;
        places[count++] = 2 * seam - 1;
        places[count++] = 190000;
        places[count++] = LONG_ROOM_SIZE - 70;

        long_room_fill();

        for (positive p = 0; p < count; p++)
                for (positive n = 0; n < sizeof(sizes) / sizeof(sizes[0]); n++)
                {
                        positive at = places[p];
                        positive size = sizes[n];

                        if (at + size + 2 >= LONG_ROOM_SIZE)
                                continue;

                        //      A needle the background cannot hold: 'z' is
                        //      not one of the seven letters.
                        for (positive i = 0; i < size; i++)
                                needle[i] = (p8)(i == 0 || i + 1 == size
                                                     ? 'z'
                                                     : 'a' + (i % 7));

                        needle[size] = 0;

                        for (positive i = 0; i < size; i++)
                                long_room[at + i] = needle[i];

                        same("string_find", "over a string of chunks",
                             (positive)string_find((string_address)long_room,
                                                   (string_address)needle),
                             (positive)(long_room + at));

                        //      And with the string ending just after it, so
                        //      the last chunk is a short one.
                        long_room[at + size + 1] = 0;

                        same("string_find", "in the last short chunk",
                             (positive)string_find((string_address)long_room,
                                                   (string_address)needle),
                             (positive)(long_room + at));

                        long_room[at + size + 1] = (p8)('a' + ((at + size + 1) % 7));

                        //      The terminator immediately before it, so the
                        //      needle is past the end of the string and must
                        //      not be found. Where at is one short of a page
                        //      boundary this is the case that says the page
                        //      walk read the whole page.
                        long_room[at] = 0;

                        same("string_find", "past the terminator",
                             (positive)string_find((string_address)long_room,
                                                   (string_address)needle),
                             0);

                        for (positive i = 0; i < size + 1; i++)
                                long_room[at + i] = (p8)('a' + ((at + i) % 7));
                }

        //      A terminator on the last byte a page holds, with the needle
        //      just past it. A page walk that reads one byte less than a page
        //      steps over this terminator, carries on into the next page and
        //      answers with a match that is not in the string.
        for (positive p = 0; p < 3; p++)
                for (positive step = 0; step < 3; step++)
                {
                        positive edge = page + p * 4096 - 1 + step;
                        static const string_address needle_past = "zzz";

                        long_room_fill();

                        long_room[edge] = 0;
                        long_room[edge + 1] = 'z';
                        long_room[edge + 2] = 'z';
                        long_room[edge + 3] = 'z';

                        same("string_find", "a terminator at the edge of a page",
                             (positive)string_find((string_address)long_room,
                                                   needle_past),
                             0);

                        //      And the same needle inside the string, one
                        //      byte the other side of the same boundary.
                        long_room[edge - 3] = 'z';
                        long_room[edge - 2] = 'z';
                        long_room[edge - 1] = 'z';

                        same("string_find", "a match at the edge of a page",
                             (positive)string_find((string_address)long_room,
                                                   needle_past),
                             (positive)(long_room + edge - 3));
                }

        //      Nothing planted at all, so the whole two hundred thousand
        //      bytes are walked and the answer is nothing.
        {
                static const string_address absent = "zzz";

                long_room_fill();

                same("string_find", "absent over a string of chunks",
                     (positive)string_find((string_address)long_room, absent), 0);
        }
}


/*
        string_compare and string_compare_max over the lengths that reach the
        wide path.

        Both take a vector body once the two have agreed for long enough --
        thirty two bytes on x86_64, thirty two on arm64 -- and every case
        above them in this file is a word or a name, which never gets there.
        So: strings hundreds of bytes long, the difference walked over every
        position around each block edge, and the terminator moved through the
        same places, because the mask those bodies build answers "differs or
        ends" in one and either half of it can be wrong on its own.
*/
static p8 wide_left[700];
static p8 wide_right[700];

//      Under its own name rather than through a prototype, the way the
//      bounded routines above are reached: string_compare_max is assembly and
//      the library declares no C name for it.
WEAK b32 wide_compare_max(string_address a, string_address b, positive count)
    __asm__("string_compare_max");

fn check_compare_wide()
{
        static const positive lengths[] = {0,  1,  7,  8,  15,  16,  17,  31,
                                           32, 33, 40, 63, 64,  65,  96,  127,
                                           128, 129, 160, 200, 300};
        static const positive bounds[] = {0,  1,  8,  31, 32,  33,  63,  64,
                                          65, 96, 128, 129, 200, 400};

        for (positive off = 0; off < 5; off++)
                for (positive l = 0; l < sizeof(lengths) / sizeof(lengths[0]); l++)
                {
                        positive length = lengths[l];
                        string_address left = (string_address)(wide_left + off);
                        string_address right = (string_address)(wide_right + off);

                        for (positive i = 0; i < 700; i++)
                        {
                                wide_left[i] = (p8)('a' + (i % 13));
                                wide_right[i] = wide_left[i];
                        }

                        wide_left[off + length] = 0;
                        wide_right[off + length] = 0;

                        //      Past the terminator the two disagree, which is
                        //      what says a bounded compare stopped there.
                        //      With the tails equal, a body that ignores the
                        //      terminator altogether answers the same thing
                        //      and nothing here notices -- that mutation
                        //      survived until this loop was added.
                        for (positive i = off + length + 1; i < 700; i++)
                                wide_right[i] = (p8)(wide_left[i] ^ 0x55);

                        same("string_compare", "long and equal",
                             (positive)string_compare(left, right),
                             (positive)reference_compare(left, right));

                        for (positive b = 0; b < sizeof(bounds) / sizeof(bounds[0]); b++)
                                same("string_compare_max", "long and equal",
                                     (positive)wide_compare_max(left, right, bounds[b]),
                                     (positive)reference_compare_max(left, right, bounds[b]));

                        for (positive where = 0; where < length; where++)
                        {
                                //      Every position while they are short,
                                //      and the edges of every block once they
                                //      are not.
                                if (length > 40 && where > 3 &&
                                    (where % 16) > 1 && (where % 32) > 1 &&
                                    (where % 8) > 1 && where + 3 < length)
                                        continue;

                                wide_right[off + where] =
                                    (p8)(wide_left[off + where] + 1);

                                same("string_compare", "a byte higher",
                                     (positive)string_compare(left, right),
                                     (positive)reference_compare(left, right));
                                same("string_compare", "a byte lower",
                                     (positive)string_compare(right, left),
                                     (positive)reference_compare(right, left));

                                for (positive b = 0; b < sizeof(bounds) / sizeof(bounds[0]); b++)
                                {
                                        same("string_compare_max", "a byte higher",
                                             (positive)wide_compare_max(left, right, bounds[b]),
                                             (positive)reference_compare_max(left, right, bounds[b]));
                                        same("string_compare_max", "a byte lower",
                                             (positive)wide_compare_max(right, left, bounds[b]),
                                             (positive)reference_compare_max(right, left, bounds[b]));
                                }

                                //      And the same position as a terminator
                                //      rather than a difference, which is the
                                //      other half of the mask.
                                wide_right[off + where] = 0;

                                same("string_compare", "an early terminator",
                                     (positive)string_compare(left, right),
                                     (positive)reference_compare(left, right));
                                same("string_compare", "an early terminator, the other way",
                                     (positive)string_compare(right, left),
                                     (positive)reference_compare(right, left));

                                wide_right[off + where] = wide_left[off + where];
                        }
                }
}


/*
        The byte hunts at the lengths a vector body reaches.

        check_hostile_neighbours above stops at twenty four bytes and
        check_first_of_wide covers memory_first_of alone, so every other hunt
        in this file is checked only on strings shorter than one vector
        register. A thirty two or sixty four byte body could be wrong in every
        lane and the suite would still agree with itself -- which is how a one
        byte error in memory_first_of once passed 263472 checks.

        So: every alignment a sixty four byte loop can begin on, lengths
        across 32, 64, 128, 256 and 320, and the hunted byte at every position
        including the last one inside a bound and the first one past it.

        The field is filled with one letter and the hunted byte planted into
        it, so the answer is decided by the plant and not by the noise around
        it: a wide body that finds the right byte in the wrong lane is off by
        a known amount rather than off by luck.
*/

static p8 wide_field[1024];

// Sixty four byte aligned, so an offset from here is the whole of the
// distance into a vector however the linker placed the array.
static string_address wide_aligned_at(p8 address_to array)
{
        positive at = (positive)(address_any)array;

        return array + ((64 - (at & 63)) & 63);
}

static hunt_byte wide_or_end;
static hunt_bounded wide_first_max;
static hunt_byte wide_last_or_end;
static hunt_memory wide_memory_first;
static measure_bounded wide_length_max;

fn wide_bind()
{
        wide_or_end =
            verify_or_end_public ? verify_or_end_public : verify_or_end_libc;
        wide_first_max = verify_first_max_public ? verify_first_max_public
                                                 : verify_first_max_libc;
        wide_last_or_end = verify_last_or_end_public ? verify_last_or_end_public
                                                     : verify_last_or_end_libc;
        wide_memory_first = verify_memory_first_public
                                ? verify_memory_first_public
                                : verify_memory_first_libc;
        wide_length_max = verify_length_max_public ? verify_length_max_public
                                                   : verify_length_max_libc;
}

// The five unbounded hunts and the three bounded ones, the bounded ones at
// the counts a wide loop stops on: nothing at all, half way, exactly the
// string, one past its terminator, and far enough past to need another turn.
fn wide_case(string_address text, positive size, p8 byte)
{
        positive bounds[6];

        same("string_length", "wide", string_length(text), reference_length(text));

        same("string_first_of", "wide", (positive)string_first_of(text, byte),
             (positive)reference_first_of(text, byte));

        same("string_last_of", "wide", (positive)string_last_of(text, byte),
             (positive)reference_string_last_of(text, byte));

        if (wide_or_end)
                same("string_first_of_or_end", "wide",
                     (positive)wide_or_end(text, byte),
                     (positive)reference_or_end(text, byte));

        if (wide_last_or_end)
                same("string_last_of_or_end", "wide",
                     (positive)wide_last_or_end(text, byte),
                     (positive)reference_last_or_end(text, byte));

        bounds[0] = 0;
        bounds[1] = 1;
        bounds[2] = size / 2;
        bounds[3] = size;
        bounds[4] = size + 1;
        bounds[5] = size + 40;

        for (positive i = 0; i < 6; i++)
        {
                positive count = bounds[i];

                if (wide_first_max)
                        same("string_first_of_max", "wide",
                             (positive)wide_first_max(text, count, byte),
                             (positive)reference_first_max(text, count, byte));

                if (wide_memory_first)
                        same("memory_first_of", "wide",
                             (positive)wide_memory_first(text, byte, count),
                             (positive)reference_memory_first(text, byte, count));

                if (wide_length_max)
                        same("string_length_max", "wide",
                             wide_length_max(text, count),
                             reference_length_max(text, count));
        }
}

// Lengths either side of every width a body here could step by, and enough
// of the short end to catch a wide path entered when it should not be.
static positive wide_sizes[] = {
    0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,
    14,  15,  16,  17,  18,  19,  20,  23,  24,  25,  30,  31,  32,  33,
    34,  35,  39,  40,  47,  48,  55,  56,  62,  63,  64,  65,  66,  67,
    70,  79,  80,  95,  96,  97,  111, 112, 120, 126, 127, 128, 129, 130,
    131, 143, 144, 159, 160, 175, 176, 191, 192, 193, 200, 223, 224, 240,
    252, 254, 255, 256, 257, 258, 260, 280, 288, 300, 310, 318, 319, 320};

#define WIDE_SIZE_COUNT (sizeof(wide_sizes) / sizeof(wide_sizes[0]))

// The alignments a position sweep runs at. Every one of them is a distance
// into a sixty four byte block that a wide loop has to mask off; the full
// nought to seventy one sweep below covers the rest with fewer plants.
static positive wide_offsets[] = {0, 1, 7, 8, 15, 16, 17, 31, 32, 33, 63};

#define WIDE_OFFSET_COUNT (sizeof(wide_offsets) / sizeof(wide_offsets[0]))

fn hunts_wide_sweep()
{
        string_address base = wide_aligned_at(wide_field);

        //
        //      Every alignment a sixty four byte loop can begin on, against
        //      every length in the ladder, with the byte in the places a
        //      body gets wrong when its edges are off by one: nowhere at
        //      all, at the very front, at the last byte of the string, and
        //      one past the terminator where nothing unbounded may see it.
        //
        for (positive offset = 0; offset < 72; offset++)
                for (positive s = 0; s < WIDE_SIZE_COUNT; s++)
                {
                        positive size = wide_sizes[s];
                        string_address text = base + offset;

                        if (offset + size + 48 >= sizeof(wide_field))
                                continue;

                        reference_fill(wide_field, 'a', sizeof(wide_field));
                        text[size] = 0;

                        // absent, and present in every byte
                        wide_case(text, size, 'z');
                        wide_case(text, size, 'a');

                        // the terminator itself, which string_first_of and
                        // string_last_of_or_end answer with and string_last_of
                        // answers nothing for
                        wide_case(text, size, 0);

                        if (size)
                        {
                                text[size - 1] = 'z';
                                wide_case(text, size, 'z');
                                text[size - 1] = 'a';
                        }

                        // past the terminator: bounded hunts wide enough may
                        // see it, unbounded ones never may
                        text[size + 1] = 'z';
                        wide_case(text, size, 'z');
                        text[size + 1] = 'a';

                        // a high byte, in case a lane is compared signed
                        text[size / 2] = 0xff;
                        wide_case(text, size, 0xff);
                        text[size / 2] = 'a';
                }

        //
        //      The byte at every position in turn, which is the case a body
        //      that finds the right vector and the wrong lane fails and
        //      nothing else does.
        //
        for (positive o = 0; o < WIDE_OFFSET_COUNT; o++)
                for (positive s = 0; s < WIDE_SIZE_COUNT; s++)
                {
                        positive offset = wide_offsets[o];
                        positive size = wide_sizes[s];
                        string_address text = base + offset;

                        if (offset + size + 48 >= sizeof(wide_field))
                                continue;

                        reference_fill(wide_field, 'a', sizeof(wide_field));
                        text[size] = 0;

                        for (positive where = 0; where <= size + 2; where++)
                        {
                                // that byte is the terminator; moving it
                                // would be a different string
                                if (where == size)
                                        continue;

                                text[where] = 'z';
                                wide_case(text, size, 'z');
                                text[where] = 'a';
                        }
                }
}

/*
        The same sweep once for each body the routines have.

        On x86_64 every hunt below carries three: sixty four bytes at a time
        where the processor has AVX-512, thirty two where it has AVX2, and
        eight where it has neither. cpu_detect answers once at startup, so on
        this machine only the first of the three would ever run and the other
        two would be tested by nothing at all -- which is the same hole a one
        byte error in memory_first_of sat in while 263472 checks agreed.

        The two bytes it wrote are bytes, so the sweep is run again with them
        forced down. Off every architecture that has no such choice this is
        three identical passes, which costs time and proves the same thing
        three times rather than proving nothing once.
*/
fn check_hunts_wide()
{
        p8 avx2 = cpu_has_avx2;
        p8 avx512 = cpu_has_avx512;

        wide_bind();

        hunts_wide_sweep();

        cpu_has_avx512 = 0;
        hunts_wide_sweep();

        cpu_has_avx2 = 0;
        hunts_wide_sweep();

        cpu_has_avx2 = avx2;
        cpu_has_avx512 = avx512;
}

/*
        string_compare and memory_compare over the four vector loops, at every
        pair of alignments, and against a page that is not there.

        check_compare_wide above stops at three hundred bytes with one offset
        against the other. The x86_64 bodies read four vectors of each stream
        a turn once three single vectors have agreed -- two hundred and twenty
        four bytes in on AVX-512, a hundred and twenty eight on AVX2 -- and
        answer what is left before a page edge with a vector that overlaps
        what they have already read. So a difference and a terminator are
        planted in every byte across the words, the three single vectors and
        the first block of four, then at the edges of every vector after; and
        the two strings are put at every distance apart a sixty four byte
        vector can tell, because it is the later of the two offsets within a
        page that licenses the reads.

        The page argument those bodies make is that a load which does not
        cross a page cannot fault, and the only way to hold them to it is a
        page that is not there: one string ends on the last byte of a mapping
        with the page after it unmapped while the other goes on, at every
        length that puts the edge in a different part of the loop. A body
        that reads a vector across that edge dies here rather than in a shell
        somewhere. A run that cannot map the pages says so and skips that part.

        On x86_64 all of it runs once a body, the two feature bytes forced
        down a tier each pass as check_hunts_wide does.
*/
#define BLOCK_ROOM 1600
#define BLOCK_EDGE_PAGES 4

static p8 block_left[64 + BLOCK_ROOM];
static p8 block_right[64 + BLOCK_ROOM];

fn block_pair(string_address detail, p8 address_to left, p8 address_to right)
{
        same("string_compare", detail, (positive)string_compare(left, right),
             (positive)reference_compare(left, right));
        same("string_compare", detail, (positive)string_compare(right, left),
             (positive)reference_compare(right, left));
}

fn block_fill(p8 address_to left, p8 address_to right, positive count)
{
        for (positive i = 0; i < count; i++)
        {
                left[i] = (p8)('a' + i % 23);
                right[i] = left[i];
        }
}

//      The three things a lane can be wrong about at one position: a byte
//      above 127 against one below, where a signed lane answers with the
//      wrong sign; a byte one apart; and the end of one string where the
//      other goes on.
fn block_plant(p8 address_to left, p8 address_to right, positive where)
{
        p8 keep = right[where];

        right[where] = (p8)(keep ^ 0x80);
        block_pair("the high bit apart", left, right);
        right[where] = (p8)(keep + 1);
        block_pair("a byte apart", left, right);
        right[where] = 0;
        block_pair("an early terminator", left, right);
        right[where] = keep;
}

fn block_sweep()
{
        static const positive plants[] = {33, 100, 224, 300, 479, 599};
        p8 address_to left = wide_aligned_at(block_left);
        p8 address_to right = wide_aligned_at(block_right);

        block_fill(left, right, BLOCK_ROOM);
        left[1500] = 0;
        right[1500] = 0;
        block_pair("long and equal", left, right);

        //      Every byte through the first block of four and the vectors
        //      either side of it, then the edges of every vector after.
        for (positive where = 0; where < 1500; where++)
        {
                if (where > 600 && (where % 32) > 1 && (where % 32) < 30)
                        continue;

                block_plant(left, right, where);
        }

        //      Both orders of every pair of distances into a vector.
        for (positive l = 0; l < 64; l++)
                for (positive r = 0; r < 64; r++)
                {
                        p8 address_to a = left + l;
                        p8 address_to b = right + r;

                        block_fill(a, b, 601);
                        a[600] = 0;
                        b[600] = 0;
                        block_pair("long and equal, apart", a, b);

                        for (positive p = 0; p < sizeof(plants) / sizeof(plants[0]); p++)
                                block_plant(a, b, plants[p]);
                }
}

fn block_memory_pair(string_address detail, p8 address_to a, p8 address_to b,
                     positive size)
{
        same("memory_compare", detail, (positive)memory_compare(a, b, size),
             (positive)reference_memory_compare(a, b, size));
        same("memory_compare", detail, (positive)memory_compare(b, a, size),
             (positive)reference_memory_compare(b, a, size));
}

fn block_memory()
{
        static const positive sizes[] = {63,  64,  65,  96,  127, 128, 129, 191,
                                         192, 193, 255, 256, 257, 300, 319, 320,
                                         321, 385, 448, 511, 512, 513, 576, 700,
                                         768, 769, 1000, 1023, 1024, 1025, 1100};
        static const positive lefts[] = {0, 1, 7, 8, 15, 16, 17, 31, 32, 33, 63};
        p8 address_to left = wide_aligned_at(block_left);
        p8 address_to right = wide_aligned_at(block_right);

        for (positive s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
                for (positive o = 0; o < sizeof(lefts) / sizeof(lefts[0]); o++)
                {
                        positive size = sizes[s];
                        p8 address_to a = left + lefts[o];
                        p8 address_to b = right + (lefts[o] * 7 + 3) % 64;

                        block_fill(a, b, size + 1);

                        //      One past the count differs, which is what
                        //      says nothing past the count was looked at.
                        b[size] = (p8)(a[size] ^ 0xff);

                        block_memory_pair("equal over the vectors", a, b, size);

                        //      Every byte of the first vector and of the
                        //      last -- which the overlapping tail reads a
                        //      second time -- and the edges of each between.
                        for (positive where = 0; where < size; where++)
                        {
                                p8 keep = b[where];

                                if (where >= 64 && where + 64 < size &&
                                    (where % 64) > 1 && (where % 64) < 62)
                                        continue;

                                b[where] = (p8)(keep ^ 0x80);
                                block_memory_pair("the high bit apart", a, b, size);
                                b[where] = (p8)(keep + 1);
                                block_memory_pair("a byte apart", a, b, size);
                                b[where] = keep;
                        }
                }
}

//      A string whose terminator is the last readable byte before an edge,
//      against another that goes on past that length, and against a second
//      such string ending on its own edge.
fn block_edge_one(p8 address_to edge, p8 address_to other_edge, positive length)
{
        static const positive shifts[] = {0, 1, 7, 31, 32, 33, 63};
        p8 address_to text = edge - 1 - length;
        p8 address_to twin = other_edge - 1 - length;
        p8 address_to room = other_edge - (BLOCK_EDGE_PAGES - 1) * 4096;

        for (positive i = 0; i < length; i++)
                text[i] = (p8)('a' + i % 23);

        text[length] = 0;

        for (positive s = 0; s < sizeof(shifts) / sizeof(shifts[0]); s++)
        {
                p8 address_to other = room + shifts[s];
                positive longer = length + 200;

                for (positive i = 0; i < longer; i++)
                        other[i] = (p8)('a' + i % 23);

                other[longer] = 0;
                block_pair("longer than the edge", text, other);

                other[length] = 0;
                block_pair("equal up to the edge", text, other);

                other[length - 1] = (p8)(other[length - 1] ^ 0x80);
                block_pair("apart at the edge", text, other);
                other[length - 1] = text[length - 1];

                other[length / 2] = (p8)(other[length / 2] + 1);
                block_pair("apart before the edge", text, other);
        }

        //      Both on an edge: equal, and one a byte longer, so that one of
        //      the two has its terminator where the other has a byte and
        //      neither has anything readable after it.
        for (positive i = 0; i < length; i++)
                twin[i] = text[i];

        twin[length] = 0;
        block_pair("both on an edge", text, twin);

        twin--;
        for (positive i = 0; i <= length; i++)
                twin[i] = (p8)('a' + i % 23);

        twin[length + 1] = 0;
        block_pair("both on an edge, one longer", text, twin);
}

fn block_edge(p8 address_to edge, p8 address_to other_edge)
{
        //      Every length while the edge is inside the words and the first
        //      vectors, then every length that puts it in each part of the
        //      four vector loop with one page edge inside the string, then
        //      with two.
        for (positive length = 1; length <= 400; length++)
                block_edge_one(edge, other_edge, length);

        for (positive length = 4000; length <= 4300; length++)
                block_edge_one(edge, other_edge, length);

        for (positive length = 8100; length <= 8300; length++)
                block_edge_one(edge, other_edge, length);
}

fn check_compare_blocks()
{
        p8 wide = 0;
        p8 widest = 0;
        p8 address_to first = (p8 address_to)memory(BLOCK_EDGE_PAGES * 4096);
        p8 address_to second = (p8 address_to)memory(BLOCK_EDGE_PAGES * 4096);

#if X64
        wide = cpu_has_avx2;
        widest = cpu_has_avx512;
#endif

        //      Both mapped before either loses its last page, so the second
        //      cannot be handed the hole the first left.
        if (first && second)
        {
                memory_free(first + (BLOCK_EDGE_PAGES - 1) * 4096, 4096);
                memory_free(second + (BLOCK_EDGE_PAGES - 1) * 4096, 4096);
        }
        else
                string_format(log, "  NOTE string_compare: no pages to map, so "
                                   "the edge was not tested\n");

        for (positive pass = 0; pass < BULK_TIERS; pass++)
        {
                bulk_tier(pass, wide, widest);
                block_sweep();
                block_memory();

                if (first && second)
                        block_edge(first + (BLOCK_EDGE_PAGES - 1) * 4096,
                                   second + (BLOCK_EDGE_PAGES - 1) * 4096);
        }

        bulk_tier(0, wide, widest);

        if (first)
                memory_free(first, (BLOCK_EDGE_PAGES - 1) * 4096);

        if (second)
                memory_free(second, (BLOCK_EDGE_PAGES - 1) * 4096);
}

/*
        The four vector turn under the byte hunts, and the four word one.

        string_first_of and string_first_of_or_end read one vector aligned
        down and four more straight line, then round the cursor down to four
        vectors and take four a turn with one question of the fold, as
        string_length does; the word at a time body under them does the same
        with words, and memory_first_of takes four a turn inside its count.
        hunts_wide_sweep stops at three hundred and twenty bytes, which on a
        processor with AVX-512 is the straight line and one vector of the
        first block, so the loop, the fold and the ladder that says which
        vector answered would be reached by nothing.

        So: a string beginning at every offset into a two hundred and fifty
        six byte block, the widest any body rounds to, with its terminator at
        every length through the straight line and the first turns of the
        loop and then at a coarser step out to five blocks; the hunted byte
        nowhere, with the poison on either side of the string being that
        byte; and the byte at the front and the back of the string and at the
        front of every thirty two byte block it crosses, which is where a
        fold that knows the block and not the vector answers with the wrong
        one. Then the same for the bounded hunt, with the byte one past the
        count as well, and in the last byte of a count that is not whole
        vectors, which only the overlapping last vector can see.
*/
#define BLOCKS_LONGEST 1400
#define BLOCKS_ROOM (256 + 256 + BLOCKS_LONGEST + 320)

static p8 blocks_room[BLOCKS_ROOM];

static p8 address_to blocks_base(void)
{
        positive at = (positive)(address_any)blocks_room + 256;

        return (p8 address_to)((at + 255) & ~(positive)255);
}

// Every length through the straight line and the first turns of the loop,
// then coarser: thirteen is odd, so over the offsets the coarse lengths
// still end on every lane of a vector.
static positive blocks_next(positive length)
{
        return length + (length < 336 ? 1 : 13);
}

fn blocks_plant(string_address text, positive where, p8 or_end_too)
{
        text[where] = 'z';

        same("string_first_of", "blocks present",
             (positive)string_first_of(text, 'z'),
             (positive)reference_first_of(text, 'z'));

        if (or_end_too && wide_or_end)
                same("string_first_of_or_end", "blocks present",
                     (positive)wide_or_end(text, 'z'),
                     (positive)reference_or_end(text, 'z'));

        text[where] = (p8)('a' + where % 3);
}

fn first_of_blocks_sweep()
{
        p8 address_to base = blocks_base();

        for (positive offset = 0; offset < 256; offset++)
        {
                string_address text = base + offset;

                reference_fill(base - 256, 'z', 256 + 256 + BLOCKS_LONGEST + 64);

                for (positive i = 0; i < BLOCKS_LONGEST + 8; i++)
                        text[i] = (p8)('a' + i % 3);

                for (positive length = 0; length <= BLOCKS_LONGEST;
                     length = blocks_next(length))
                {
                        text[length] = 0;

                        // the hunted byte right past the terminator, where
                        // the rest of the block is
                        text[length + 1] = 'z';

                        same("string_first_of", "blocks absent",
                             (positive)string_first_of(text, 'z'),
                             (positive)reference_first_of(text, 'z'));

                        same("string_first_of", "blocks terminator",
                             (positive)string_first_of(text, 0),
                             (positive)reference_first_of(text, 0));

                        if (wide_or_end)
                                same("string_first_of_or_end", "blocks absent",
                                     (positive)wide_or_end(text, 'z'),
                                     (positive)reference_or_end(text, 'z'));

                        if (length)
                        {
                                blocks_plant(text, 0, true);

                                if (length > 1)
                                        blocks_plant(text, length - 1, true);

                                for (positive front = (offset + 31) & ~(positive)31;
                                     front < offset + length; front += 32)
                                {
                                        positive where = front - offset;

                                        if (where && where != length - 1)
                                                blocks_plant(text, where,
                                                             (front & 255) == 0);
                                }
                        }

                        text[length] = (p8)('a' + length % 3);
                        text[length + 1] = (p8)('a' + (length + 1) % 3);
                }
        }
}

fn memory_blocks_plant(p8 address_to block, positive count, positive where,
                       string_address detail)
{
        block[where] = 'z';

        same("memory_first_of", detail,
             (positive)memory_first_of(block, 'z', count),
             (positive)reference_memory_first_of(block, 'z', count));

        block[where] = 'a';
}

fn memory_first_of_blocks_sweep()
{
        p8 address_to base = blocks_base();

        for (positive offset = 0; offset < 72; offset++)
        {
                p8 address_to block = base + offset;

                reference_fill(base - 64, 'a', 64 + 72 + BLOCKS_LONGEST + 64);

                for (positive count = 0; count <= BLOCKS_LONGEST;
                     count = blocks_next(count))
                {
                        same("memory_first_of", "blocks absent",
                             (positive)memory_first_of(block, 'z', count),
                             (positive)reference_memory_first_of(block, 'z', count));

                        memory_blocks_plant(block, count, count, "one past the blocks");

                        if (!count)
                                continue;

                        // the last byte of the count, which for a count that
                        // is not whole vectors only the overlapping last
                        // vector sees
                        memory_blocks_plant(block, count, count - 1, "blocks last byte");

                        // and the front of every vector the count crosses,
                        // counted from the block itself: the bounded hunt
                        // reads from where it is told to and aligns nothing
                        for (positive where = 32; where + 1 < count; where += 32)
                                memory_blocks_plant(block, count, where, "blocks present");
                }
        }
}

/*
        A terminator on the last byte of a page, and nothing mapped after it.

        The four vector turn reads two hundred and fifty six bytes at once and
        the four word one thirty two, and both may because an aligned block
        never crosses a page: the terminator ends the string inside the block
        and the rest of the block is the same page. That argument is what
        keeps every long string safe, and a test of it has to put the string
        where the argument is all there is -- ending on the last byte of a
        page whose neighbour has been taken away with mprotect, the way
        exact_scan.c takes its neighbours away, so that a hunt reading one
        byte too far dies here rather than in a shell.

        Every length that ends there, from nothing to five blocks; the byte
        absent, which walks the whole string, at the front, and the
        terminator itself. Then memory_first_of with a count that ends on that
        byte, which is the bounded hunt's version of the same promise, with
        the byte absent and in the last byte of the count.
*/
#define PAGE_END_PROT_NONE 0

fn check_first_of_page_end()
{
        p8 address_to pages = (p8 address_to)memory(3 * 4096);

        same("string_first_of", "page end room",
             (positive)((bipolar)(positive)pages > 0), 1);

        if ((bipolar)(positive)pages <= 0)
                return;

        //      The third page taken away, so a string ending on the last
        //      byte of the second has nothing readable after it.
        bipolar protected = system_call_3(
            syscall(mprotect), (positive)(pages + 2 * 4096), 4096,
            PAGE_END_PROT_NONE);
        same("memory_first_of", "guard page protected", protected, 0);
        if (protected < 0)
        {
                memory_free(pages, 3 * 4096);
                return;
        }

        // Exercise the routine even when the compiler knows the zero count.
        same("memory_first_of", "zero count null",
             (positive)(memory_first_of)(null, 'z', 0), 0);
        same("memory_first_of", "zero count inaccessible",
             (positive)(memory_first_of)(pages + 2 * 4096, 'z', 0), 0);

        p8 address_to last = pages + 2 * 4096 - 1;

        reference_fill(pages, 'a', 2 * 4096);
        address_to last = 0;

        for (positive length = 0; length <= BLOCKS_LONGEST;
             length = blocks_next(length))
        {
                string_address text = last - length;

                same("string_first_of", "page end absent",
                     (positive)string_first_of(text, 'z'),
                     (positive)reference_first_of(text, 'z'));

                same("string_first_of", "page end terminator",
                     (positive)string_first_of(text, 0),
                     (positive)reference_first_of(text, 0));

                if (wide_or_end)
                        same("string_first_of_or_end", "page end absent",
                             (positive)wide_or_end(text, 'z'),
                             (positive)reference_or_end(text, 'z'));

                if (length)
                {
                        text[0] = 'z';
                        same("string_first_of", "page end present",
                             (positive)string_first_of(text, 'z'),
                             (positive)reference_first_of(text, 'z'));
                        text[0] = 'a';
                }

                same("memory_first_of", "page end absent",
                     (positive)memory_first_of(text, 'z', length + 1),
                     (positive)reference_memory_first_of(text, 'z', length + 1));
                same("memory_span_without_byte", "page end absent",
                     memory_span_without_byte(text, 'z', length + 1), length + 1);

                address_to last = 'z';
                same("memory_first_of", "page end last byte",
                     (positive)memory_first_of(text, 'z', length + 1),
                     (positive)reference_memory_first_of(text, 'z', length + 1));
                same("memory_span_without_byte", "page end last byte",
                     memory_span_without_byte(text, 'z', length + 1), length);
                address_to last = 0;
        }

        memory_free(pages, 3 * 4096);
}

fn first_of_blocks_tier()
{
        first_of_blocks_sweep();
        memory_first_of_blocks_sweep();
        check_first_of_page_end();
}

// Once for each body, as check_hunts_wide runs its sweep.
fn check_first_of_blocks()
{
        p8 avx2 = cpu_has_avx2;
        p8 avx512 = cpu_has_avx512;

        first_of_blocks_tier();

        cpu_has_avx512 = 0;
        first_of_blocks_tier();

        cpu_has_avx2 = 0;
        first_of_blocks_tier();

        cpu_has_avx2 = avx2;
        cpu_has_avx512 = avx512;
}

/*
        The block loops, and the page after the string.

        string_last_of_or_end walks four vectors a turn once its cursor is
        four vectors aligned, memory_count four vectors a turn from wherever
        the block starts, and string_copy is string_length's four vectors and
        then memory_copy_apart. The sweeps above stop at three hundred and
        twenty bytes, which barely leaves a head loop, so this is the same
        question asked of longer strings: the byte in every vector of every
        block, with and without the terminator in the same block, strings
        starting at every offset of a two hundred and fifty six byte block,
        counts that are not a multiple of any block, and matches under the
        vector that overlaps the end of a count.

        And the terminator as the last byte of a page with the page after it
        unmapped, which is what the aligned vector rule is for: a block that
        read one byte past the string would stop the program here rather
        than answer. The pages come from the allocator and the last of them
        is taken away, as exact_scan.c does it.

        Every sweep runs once for each body, the feature bytes walked down
        as check_hunts_wide walks them.
*/
#define BLOCK_PAGE 4096
#define BLOCK_PAGES 4
#define BLOCK_PROT_NONE 0

static p8 address_to block_pages;

// Three mapped pages and then one that is not; null if the machine refused.
static p8 address_to block_mapped(void)
{
        if (!block_pages)
        {
                p8 address_to raw = (p8 address_to)memory((BLOCK_PAGES + 1) * BLOCK_PAGE);
                positive at;

                if (!raw)
                        return null;

                at = ((positive)(address_any)raw + BLOCK_PAGE - 1) & ~(positive)(BLOCK_PAGE - 1);
                block_pages = (p8 address_to)at;

                if ((bipolar)system_call_3(syscall(mprotect),
                                           (positive)(block_pages + (BLOCK_PAGES - 1) * BLOCK_PAGE),
                                           BLOCK_PAGE, BLOCK_PROT_NONE) < 0)
                {
                        block_pages = null;
                        return null;
                }
        }

        return block_pages;
}

static positive2 reference_words(p8 address_to bytes, positive size, p8 inside)
{
        positive2 result = {.x = 0, .y = inside ? 1 : 0};

        for (positive at = 0; at < size; at++)
        {
                p8 byte = bytes[at];
                p8 next = !(byte == 32 || (byte >= 9 && byte <= 13));
                if (next && !result.y)
                        result.x++;
                result.y = next;
        }
        return result;
}

// Every call the first in a process, on riscv64's first sweep.
static p8 words_first;

static fn words_case(p8 address_to bytes, positive size, p8 inside)
{
        positive2 want = reference_words(bytes, size, inside);
        VECTOR_FIRST_CALL(words_first);
        positive2 got = memory_count_words(bytes, size, inside);
        same("memory_count_words", "count", got.x, want.x);
        same("memory_count_words", "outgoing state", got.y, want.y);

        positive split = size / 2;
        VECTOR_FIRST_CALL(words_first);
        positive2 first = memory_count_words(bytes, split, inside);
        VECTOR_FIRST_CALL(words_first);
        positive2 rest = memory_count_words(bytes + split, size - split,
                                            (p8)(first.y != 0));
        same("memory_count_words", "split count", first.x + rest.x, want.x);
        same("memory_count_words", "split state", rest.y, want.y);
}

static fn words_sweep()
{
        static p8 room[512];
        static const p8 alphabet[] = {0, 8, 9, 10, 11, 12, 13, 14,
                                      31, 32, 33, 127, 128, 255};

        // Every byte at every lane, between whitespace and between word bytes.
        for (positive value = 0; value < 256; value++)
                for (positive background = 0; background < 2; background++)
                        for (positive lane = 0; lane < 64; lane++)
                        {
                                reference_fill(room, background ? 'x' : ' ', 65);
                                room[lane] = (p8)value;
                                words_case(room, 65, false);
                                words_case(room, 65, true);
                        }

        // And in a count long enough for the widest turns (256 bytes and up), at
        // the lanes either side of each block's edge: a byte a lookup could alias
        // (73 to 77 and 96 share their low six bits with white space) must not.
        static const positive lanes[] = {0, 1, 63, 64, 127, 128, 191, 192, 255, 256, 299};
        for (positive value = 0; value < 256; value++)
                for (positive background = 0; background < 2; background++)
                        for (positive k = 0; k < sizeof(lanes) / sizeof(lanes[0]); k++)
                        {
                                reference_fill(room, background ? 'x' : ' ', 300);
                                room[lanes[k]] = (p8)value;
                                words_case(room, 300, false);
                                words_case(room, 300, true);
                        }

        for (positive offset = 0; offset < 64; offset++)
                for (positive size = 0; size <= 257; size++)
                {
                        for (positive at = 0; at < sizeof(room); at++)
                                room[at] = alphabet[(at * 7 + size + offset) %
                                                    sizeof(alphabet)];
                        words_case(room + offset, size, false);
                        words_case(room + offset, size, true);
                }

        p8 address_to pages = block_mapped();
        if (!pages)
                return;
        p8 address_to edge = pages + (BLOCK_PAGES - 1) * BLOCK_PAGE;
        for (positive shape = 0; shape < 4; shape++)
                for (positive size = 0; size <= 1100; size++)
                {
                        p8 address_to bytes = edge - size;
                        for (positive at = 0; at < size; at++)
                                bytes[at] = shape == 0 ? 9 : shape == 1 ? 128 :
                                            shape == 2 ? ((at & 1) ? ' ' : 0) :
                                            (p8)(at * 73 + size);
                        words_case(bytes, size, false);
                        words_case(bytes, size, true);
                }
}

static fn check_count_words()
{
#if X64
        // Every body the machine can run, down to SSE2: AVX-512 with VBMI, AVX-512
        // without it, AVX2, and SSE2.
        p8 avx2 = cpu_has_avx2;
        p8 avx512 = cpu_has_avx512;
        p8 vbmi = cpu_has_avx512_vbmi;
        for (p32 tier = 0; tier < 4; tier++)
        {
                cpu_has_avx512 = tier < 2 ? avx512 : 0;
                cpu_has_avx512_vbmi = tier == 0 ? vbmi : 0;
                cpu_has_avx2 = tier < 3 ? avx2 : 0;
                words_sweep();
        }
        cpu_has_avx2 = avx2;
        cpu_has_avx512 = avx512;
        cpu_has_avx512_vbmi = vbmi;
#elif RISCV64
        // Every call the first in a process, which asks for V and comes back
        // to the routine's entry; then the word body without it.
        words_first = true;
        words_sweep();
        words_first = false;
        p8 vector = cpu_has_vector;
        cpu_has_vector = 1;
        words_sweep();
        cpu_has_vector = vector;
#else
        words_sweep();
#endif
}

static p8 block_field[2048 + 256];
static p8 block_spare[2048 + 256];

static string_address block_aligned_at(p8 address_to array)
{
        positive at = (positive)(address_any)array;

        return array + ((256 - (at & 255)) & 255);
}

// Lengths across every block a body here steps by, with the ones either
// side of each boundary, and a few that are nowhere near one.
static positive block_sizes[] = {0,   1,   63,  64,  65,  127, 128, 129,
                                 191, 192, 193, 255, 256, 257, 300, 383,
                                 384, 385, 511, 512, 513, 700, 767, 768,
                                 769, 1000, 1023, 1024, 1025, 1100};

#define BLOCK_SIZE_COUNT (sizeof(block_sizes) / sizeof(block_sizes[0]))

// Where in a vector a plant goes: the ends and the middle of each.
static positive block_lanes[] = {0, 1, 31, 32, 33, 62, 63};

#define BLOCK_LANE_COUNT (sizeof(block_lanes) / sizeof(block_lanes[0]))

fn block_last_case(string_address text, p8 byte)
{
        if (wide_last_or_end)
                same("string_last_of_or_end", "blocks",
                     (positive)wide_last_or_end(text, byte),
                     (positive)reference_last_or_end(text, byte));
}

fn block_copy_case(string_address text, positive size)
{
        reference_fill(block_spare, 0xa5, sizeof(block_spare));
        string_copy(block_spare, text);
        same_bytes("string_copy", "blocks, the string and its terminator",
                   block_spare, text, size + 1);

        for (positive i = 1; i <= 8; i++)
                same("string_copy", "blocks, the byte after", block_spare[size + i], 0xa5);
}

fn blocks_sweep_strings()
{
        string_address base = block_aligned_at(block_field);

        /* A terminator at the first byte of a four-vector block leaves the
           last character in the masked head. Never rescan that head without
           its lower bound: argv strings can have a slash just before them. */
        for (positive boundary = 128; boundary <= 256; boundary += 128)
                for (positive size = 1; size <= 96; size++)
                {
                        reference_fill(base, '/', boundary + 64);
                        string_address text = base + boundary - size;
                        reference_fill(text, 'x', size);
                        text[size] = 0;
                        block_last_case(text, '/');
                        same("string_last_of", "block boundary head mask",
                             (positive)string_last_of(text, '/'), 0);
                        text[size / 2] = '/';
                        block_last_case(text, '/');
                }

        for (positive offset = 0; offset < 256; offset++)
                for (positive s = 0; s < BLOCK_SIZE_COUNT; s++)
                {
                        positive size = block_sizes[s];
                        string_address text = base + offset;

                        if (offset + size + 16 >= sizeof(block_field))
                                continue;

                        reference_fill(block_field, 'a', sizeof(block_field));
                        text[size] = 0;

                        // absent, present everywhere, and the terminator
                        block_last_case(text, 'z');
                        block_last_case(text, 'a');
                        block_last_case(text, 0);
                        block_copy_case(text, size);

                        // one past the terminator, which nothing may see
                        text[size + 1] = 'z';
                        block_last_case(text, 'z');
                        text[size + 1] = 'a';

                        // the byte in each lane of each vector of each
                        // block, on its own and with an earlier one that
                        // must lose to it, and with a later one past the
                        // terminator that must not win
                        for (positive vector = 0; vector * 64 < size; vector++)
                                for (positive l = 0; l < BLOCK_LANE_COUNT; l++)
                                {
                                        positive where = vector * 64 + block_lanes[l];

                                        if (where >= size)
                                                continue;

                                        text[where] = 'z';
                                        block_last_case(text, 'z');

                                        text[where / 2] = 'z';
                                        block_last_case(text, 'z');
                                        text[where / 2] = 'a';

                                        text[size + 1] = 'z';
                                        block_last_case(text, 'z');
                                        text[size + 1] = 'a';

                                        text[where] = 'a';
                                }
                }
}

static p8 count_room[66000];

fn blocks_sweep_count()
{
        static positive count_offsets[] = {0, 1, 7, 31, 33, 63};

        // every length across the blocks of every body, at alignments the
        // wide loops have to start on, over a pattern with more than one
        // value in it
        for (positive o = 0; o < sizeof(count_offsets) / sizeof(count_offsets[0]); o++)
                for (positive size = 200; size <= 1100; size++)
                {
                        positive off = count_offsets[o];

                        for (positive i = 0; i < off + size + 64; i++)
                                count_room[i] = (p8)((i * 7 + off) % 5 ? 'a' : '\n');

                        same("memory_count", "blocks, against the byte loop",
                             memory_count(count_room + off, size, '\n'),
                             reference_count(count_room + off, size, '\n'));
                        same("memory_count", "blocks, against the byte loop",
                             memory_count(count_room + off, size, 'a'),
                             reference_count(count_room + off, size, 'a'));
                        same("memory_count", "blocks, nothing to count",
                             memory_count(count_room + off, size, 'z'), 0);
                }

        // a match under the vector that overlaps the end, at every lane of
        // it, and just past the count where it must not be seen
        for (positive size = 1; size < 64; size++)
                for (positive at = 0; at < size + 2; at++)
                {
                        reference_fill(count_room, 'a', sizeof(count_room));
                        count_room[at] = '\n';
                        same("memory_count", "blocks, the overlapping tail",
                             memory_count(count_room, size, '\n'), at < size);
                        count_room[256 + at] = '\n';
                        same("memory_count", "blocks, the tail after a block",
                             memory_count(count_room, 256 + size, '\n'),
                             (positive)(at < size) + 1);
                }

        // past the drain of the sixty four byte body: 252 rounds of 256 is
        // 64512, and a block of nothing but the byte is where a wrapped
        // lane shows
        reference_fill(count_room, '\n', sizeof(count_room));

        for (positive size = 64400; size < 65000; size += 3)
        {
                same("memory_count", "blocks, every byte matches",
                     memory_count(count_room, size, '\n'), size);
                same("memory_count", "blocks, no byte matches",
                     memory_count(count_room, size, 'z'), 0);
        }
}

fn blocks_sweep_page_edge()
{
        p8 address_to pages = block_mapped();
        p8 address_to edge;

        if (!pages)
                return;

        edge = pages + (BLOCK_PAGES - 1) * BLOCK_PAGE;

        // the terminator is the last mapped byte, so the string runs to the
        // edge of the page and the byte after it faults
        for (positive size = 0; size <= 1100; size++)
        {
                string_address text = (string_address)(edge - 1 - size);

                for (positive i = 0; i < size; i++)
                        text[i] = 'a';

                text[size] = 0;

                block_last_case(text, 'z');
                block_last_case(text, 'a');
                block_last_case(text, 0);

                if (size)
                {
                        text[size - 1] = 'z';
                        block_last_case(text, 'z');
                        text[0] = 'z';
                        block_last_case(text, 'z');
                        text[size - 1] = 'a';
                        block_last_case(text, 'z');
                        text[0] = 'a';
                }

                block_copy_case(text, size);

                // and the block that ends at the edge, counted
                same("memory_count", "blocks ending at the page edge",
                     memory_count(text, size, 'a'), size);
                same("memory_count", "blocks ending at the page edge",
                     memory_count(text, size, 'z'), 0);

                if (size)
                {
                        text[size - 1] = 'z';
                        same("memory_count", "blocks ending at the page edge",
                             memory_count(text, size, 'z'), 1);
                }
        }
}

fn blocks_sweep()
{
        blocks_sweep_strings();
        blocks_sweep_count();
        blocks_sweep_page_edge();
}

fn check_blocks()
{
        p8 avx2 = cpu_has_avx2;
        p8 avx512 = cpu_has_avx512;

        wide_bind();

        blocks_sweep();

        cpu_has_avx512 = 0;
        blocks_sweep();

        cpu_has_avx2 = 0;
        blocks_sweep();

        cpu_has_avx2 = avx2;
        cpu_has_avx512 = avx512;
}

/*
        The six number routines, deeply.

        check_bulk_numbers above is thirteen values and a round trip. That is
        enough to catch a routine that does not work at all and nothing else:
        every one of its samples is under 2^32, so a conversion that splits
        the number into eight digit chunks would never have its second chunk
        reached, and a parser that reads eight bytes at a time would never
        read a second word. Every path here needs a case that lands on it.

        So: every value from zero to a hundred thousand, then every power of
        ten and either side of it, the top and bottom of both types, and for
        the parsers the malformed inputs -- nothing, a sign on its own,
        blanks, a digit string longer than the type holds.
*/

// The C that positive_to_string replaced: a digit at a time into a buffer
// backwards, then one call to the writer with the whole run.
fn reference_positive_to_string(writer write, positive number)
{
        b8 buffer[24];
        positive at = sizeof(buffer);

        if (!number)
                buffer[--at] = '0';

        while (number)
        {
                buffer[--at] = (b8)('0' + number % 10);
                number /= 10;
        }

        write(buffer + at, sizeof(buffer) - at);
}

// And what bipolar_to_string did: the sign is its own call, the magnitude is
// the routine above, and the most negative value negates to itself.
fn reference_bipolar_to_string(writer write, bipolar number)
{
        if (number < 0)
        {
                write((address_any) "-", 1);
                reference_positive_to_string(write, (positive)(-(positive)number));
                return;
        }

        reference_positive_to_string(write, (positive)number);
}

/*
        The trailing digits, which is what string_to_positive answers.

        reference_to_positive at the top of this file reads forwards, and the
        two agree only on a string that is all digits: "12a34" is twelve
        forwards and thirty four backwards, and the assembly's comment says
        the backwards answer is the one callers depend on. Checking malformed
        input against the forward reference would report every case as a
        disagreement for a reason that is not the one being looked for, so the
        backwards rule is written out here as its own reference.
*/
positive reference_trailing_positive(string_address input)
{
        positive length = 0;

        while (input[length])
                length++;

        positive value = 0;
        positive place = 1;

        while (length--)
        {
                p8 c = input[length];

                if (c < '0' || c > '9')
                        break;

                value += place * (positive)(c - '0');
                place *= 10;
        }

        return value;
}

// A leading minus and nothing else: a plus is not a sign here, which is what
// the assembly does and what reference_to_bipolar above does not.
bipolar reference_trailing_bipolar(string_address input)
{
        if (string_is(input, '-'))
                return (bipolar)(0 - reference_trailing_positive(input + 1));

        return (bipolar)reference_trailing_positive(input);
}

static b8 spelled[64];
static b8 spelled_twin[64];

// One value through both, compared as bytes rather than as a number, so a
// leading zero or a lost digit is a failure and not a coincidence.
fn one_positive(positive n, string_address detail)
{
        catch_reset();
        positive_to_string(catch_writer, n);

        positive length = caught_length;

        for (positive i = 0; i <= length; i++)
                spelled[i] = caught[i];

        catch_reset();
        reference_positive_to_string(catch_writer, n);

        same("positive_to_string", detail, length, caught_length);
        same_bytes("positive_to_string", detail, spelled, caught, caught_length + 1);

        // and back again, through the parser, which is the other half
        same("string_to_positive", detail, string_to_positive(spelled), n);
}

fn one_bipolar(bipolar n, string_address detail)
{
        catch_reset();
        bipolar_to_string(catch_writer, n);

        positive length = caught_length;

        for (positive i = 0; i <= length; i++)
                spelled_twin[i] = caught[i];

        catch_reset();
        reference_bipolar_to_string(catch_writer, n);

        same("bipolar_to_string", detail, length, caught_length);
        same_bytes("bipolar_to_string", detail, spelled_twin, caught, caught_length + 1);

        same("string_to_bipolar", detail, (positive)string_to_bipolar(spelled_twin),
             (positive)n);
}

fn check_numbers_exhaustive()
{
        // Every value a five digit number can be, which covers every carry
        // the pair table can make and every digit count up to five.
        for (positive n = 0; n <= 100000; n++)
        {
                one_positive(n, (string_address) "0..100000");
                one_bipolar((bipolar)n, (string_address) "0..100000");
                one_bipolar(-(bipolar)n, (string_address) "-100000..0");
        }
}

fn check_numbers_edges()
{
        // Every power of ten and either side of it: where a conversion picks
        // its number of digits wrong it is here, and nowhere else.
        positive power = 1;

        for (positive e = 0; e < 20; e++)
        {
                one_positive(power - 1, (string_address) "under a power of ten");
                one_positive(power, (string_address) "a power of ten");
                one_positive(power + 1, (string_address) "over a power of ten");

                if (power <= 9223372036854775807ull / 10)
                {
                        one_bipolar((bipolar)power - 1, (string_address) "under a power");
                        one_bipolar((bipolar)power, (string_address) "a power");
                        one_bipolar(-(bipolar)power, (string_address) "a negative power");
                }

                power *= 10;
        }

        // The ends of both types. The top of positive is twenty digits, which
        // is the only length a three chunk conversion reaches its last chunk
        // on, and the bottom of bipolar is the value that negates to itself.
        one_positive(0, (string_address) "zero");
        one_positive(18446744073709551615ull, (string_address) "positive max");
        one_positive(18446744073709551614ull, (string_address) "positive max less one");
        one_positive(10000000000000000000ull, (string_address) "twenty digits round");
        one_positive(9999999999999999999ull, (string_address) "nineteen nines");
        one_positive(4294967295u, (string_address) "thirty two bits");
        one_positive(4294967296ull, (string_address) "one past thirty two bits");
        one_positive(99999999u, (string_address) "eight nines");
        one_positive(100000000u, (string_address) "nine digits");
        one_positive(9999999999999999ull, (string_address) "sixteen nines");
        one_positive(10000000000000000ull, (string_address) "seventeen digits");

        one_bipolar(9223372036854775807ll, (string_address) "bipolar max");
        one_bipolar(-9223372036854775807ll - 1, (string_address) "bipolar min");
        one_bipolar(0, (string_address) "signed zero");
        one_bipolar(-1, (string_address) "minus one");

        // A spread nobody chose by hand, over every digit count there is.
        for (positive i = 0; i < 4000; i++)
        {
                positive digits = 1 + i % 20;
                positive top = 1;

                for (positive e = 1; e < digits; e++)
                        top *= 10;

                positive n = top + next() % (top * 9 + 1);

                one_positive(n, (string_address) "a spread of digit counts");
                one_bipolar((bipolar)(n >> 1), (string_address) "a spread, positive");
                one_bipolar(-(bipolar)(n >> 1), (string_address) "a spread, negative");
        }
}

/*
        What the parsers do with what nobody meant to hand them.

        Both read backwards from the terminator, so the interesting inputs are
        not the ones with a bad byte at the front -- they are the ones where
        the run of digits ends part way, where there are no digits at all, and
        where there are more digits than the type holds and the value has to
        wrap the same way the reference does.
*/
static string_address malformed[] = {
    "", "-", "+", "  ", "abc", "-abc", "+abc", ".", "-.", " - ",
    "0", "-0", "+0", "00000", "-00000", "007", "-007",
    "12a34", "-12a34", "1-2", "a1", "-a1", "1a", "-1a", " 12", "12 ",
    "1 2", "\t9", "9\t", "0x1f", "1e9", "--5", "++5", "-+5", "+-5",
    "18446744073709551615", "18446744073709551616", "18446744073709551617",
    "-18446744073709551615", "99999999999999999999",
    "999999999999999999999999999999", "9223372036854775807",
    "-9223372036854775808", "9223372036854775808", "-9223372036854775809",
    "4294967295", "4294967296", "-4294967296", "2147483647", "-2147483648",
    "0000000000000000000000001", "-0000000000000000000000001",
    "123456789012345678901234567890",
    "1234567", "12345678", "123456789", "1234567890123456", "12345678901234567",
    "-1234567", "-12345678", "-123456789", "-1234567890123456",
    "z1234567890", "1234567890z", "12345678z90", "\x7f" "9", "9\x7f",
    "/9", "9/", ":9", "9:", "9\xff", "\xff" "9",
};

fn check_parsers_malformed()
{
        for (positive i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++)
        {
                string_address input = malformed[i];

                same("string_to_positive", input, string_to_positive(input),
                     reference_trailing_positive(input));

                same("string_to_bipolar", input, (positive)string_to_bipolar(input),
                     (positive)reference_trailing_bipolar(input));
        }

        /*
                The same strings again at every offset into a word.

                A parser that reads eight bytes at a time reads the word the
                string ends in, and which bytes of that word are the string
                depends on where the string starts. Only one of the eight
                alignments is the one a literal happens to land on, so the
                other seven are where an off by one in the mask lives.
        */
        static b8 shifted[128];

        for (positive i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++)
                for (positive off = 0; off < 16; off++)
                {
                        positive at = 0;

                        while (malformed[i][at])
                        {
                                shifted[off + at] = malformed[i][at];
                                at++;
                        }

                        shifted[off + at] = 0;

                        same("string_to_positive", "at an offset",
                             string_to_positive(shifted + off),
                             reference_trailing_positive(shifted + off));

                        same("string_to_bipolar", "at an offset",
                             (positive)string_to_bipolar(shifted + off),
                             (positive)reference_trailing_bipolar(shifted + off));
                }

        // A run of digits longer than any word, at every length, so a parser
        // that takes eight at a time is entered with each possible remainder.
        static b8 run[64];

        for (positive length = 1; length < 40; length++)
        {
                for (positive i = 0; i < length; i++)
                        run[i] = (b8)('0' + (i * 7 + 3) % 10);

                run[length] = 0;

                same("string_to_positive", "a long run of digits",
                     string_to_positive(run), reference_trailing_positive(run));

                // and the same run with one byte in the middle spoiled, so the
                // wide path has to stop part way through a word
                for (positive where = 0; where < length; where++)
                {
                        b8 kept = run[where];

                        run[where] = 'x';

                        same("string_to_positive", "a run broken part way",
                             string_to_positive(run),
                             reference_trailing_positive(run));

                        run[where] = kept;
                }
        }
}

/*
        string_format past the six cases it had.

        The variadic ABI is the part that cannot be reasoned about from the
        outside: four integer registers on x86_64 and eight on the other two,
        then the stack, and a decimal counts against a different set. A format
        with five arguments in it reaches the stack path on x86_64 and nothing
        else here does.
*/
static b8 format_text[512];

fn expect(string_address detail, string_address want)
{
        same("string_format", detail, (positive)string_compare(caught, want), 0);
}

fn check_format_deep()
{
        catch_reset();
        string_format(catch_writer, (string_address) "");
        expect((string_address) "an empty format", (string_address) "");

        catch_reset();
        string_format(catch_writer, (string_address) "%%");
        expect((string_address) "one literal percent", (string_address) "%");

        catch_reset();
        string_format(catch_writer, (string_address) "%%%%%%");
        expect((string_address) "three of them", (string_address) "%%%");

        catch_reset();
        string_format(catch_writer, (string_address) "a%%b");
        expect((string_address) "a percent between text", (string_address) "a%b");

        // A percent that is the last byte writes nothing more, and an unknown
        // specifier is dropped with its percent. Both are relied on.
        catch_reset();
        string_format(catch_writer, (string_address) "tail%");
        expect((string_address) "a percent at the end", (string_address) "tail");

        catch_reset();
        string_format(catch_writer, (string_address) "a%zb");
        expect((string_address) "an unknown specifier", (string_address) "ab");

        catch_reset();
        string_format(catch_writer, (string_address) "%");
        expect((string_address) "a format that is one percent", (string_address) "");

        // Every argument kind, and then more of them than there are registers.
        catch_reset();
        string_format(catch_writer, (string_address) "%p", (positive)0);
        expect((string_address) "zero through the format", (string_address) "0");

        catch_reset();
        string_format(catch_writer, (string_address) "%p",
                      (positive)18446744073709551615ull);
        expect((string_address) "the top of positive",
               (string_address) "18446744073709551615");

        catch_reset();
        string_format(catch_writer, (string_address) "%b", (bipolar)-2147483647 - 1);
        expect((string_address) "the bottom of an int",
               (string_address) "-2147483648");

        catch_reset();
        string_format(catch_writer, (string_address) "%p %p %p %p %p %p %p %p",
                      (positive)1, (positive)2, (positive)3, (positive)4,
                      (positive)5, (positive)6, (positive)7, (positive)8);
        expect((string_address) "eight numbers, past every register",
               (string_address) "1 2 3 4 5 6 7 8");

        catch_reset();
        string_format(catch_writer,
                      (string_address) "%s %s %s %s %s %s %s %s",
                      "a", "b", "c", "d", "e", "f", "g", "h");
        expect((string_address) "eight strings, past every register",
               (string_address) "a b c d e f g h");

        catch_reset();
        string_format(catch_writer, (string_address) "%s%p%b%s%p%b%s%p%b",
                      "x", (positive)1, (bipolar)-1, "y", (positive)2, (bipolar)-2,
                      "z", (positive)3, (bipolar)-3);
        expect((string_address) "the kinds mixed, past every register",
               (string_address) "x1-1y2-2z3-3");

        // A format long enough that a scan taking eight bytes at a time runs
        // its loop several times, at every length either side of a word.
        for (positive length = 0; length < 40; length++)
        {
                for (positive i = 0; i < length; i++)
                        format_text[i] = (b8)('a' + i % 26);

                format_text[length] = 0;

                catch_reset();
                string_format(catch_writer, format_text);
                same("string_format", "plain text of every length",
                     (positive)string_compare(caught, format_text), 0);
        }

        /*
                A percent at every position in a run of text.

                A scan that takes a word at a time has to answer with the
                first percent in the word and not the last, and has to answer
                the terminator when both are in the same word. Only one
                position in eight is the one a hand written case lands on.
        */
        // The filler is spelled out rather than counted off the alphabet
        // because 's', 'p', 'b', 'f' and 'w' take arguments: a percent landing in
        // front of one of those asks for an argument nobody passed, and the
        // routine reads whatever the register happened to hold. It did, and
        // the crash was in the writer rather than anywhere near the cause.
        static const b8 filler[] = "acdeghijklmnoqrtuvxyz";

        for (positive length = 1; length < 40; length++)
                for (positive where = 0; where < length; where++)
                {
                        for (positive i = 0; i < length; i++)
                                format_text[i] = (b8)filler[i % (sizeof(filler) - 1)];

                        format_text[where] = '%';
                        format_text[length] = 0;

                        // "%%" is a literal percent; anything else here is a
                        // specifier or a drop, and the expected answer is
                        // built the same way the routine has to build it.
                        positive at = 0;
                        static b8 expected[64];
                        positive out = 0;
                        b32 stop = 0;

                        while (format_text[at] && !stop)
                        {
                                if (format_text[at] != '%')
                                {
                                        expected[out++] = format_text[at++];
                                        continue;
                                }

                                if (!format_text[at + 1])
                                {
                                        stop = 1;
                                        break;
                                }

                                if (format_text[at + 1] == '%')
                                        expected[out++] = '%';

                                at += 2;
                        }

                        expected[out] = 0;

                        catch_reset();
                        string_format(catch_writer, format_text);
                        same("string_format", "a percent at every position",
                             (positive)string_compare(caught, expected), 0);
                }

        /*
                Text with the top bit set in it.

                A scan that hunts a zero byte and a percent a word at a time
                does its arithmetic on whole bytes, and 0x80 through 0xFF are
                where an approximation of that test goes wrong: a byte over
                0x80 answers the same as a zero byte to (w - 0x01..) & 0x80..
                unless the & ~w is there. Nothing else in this file puts one
                in a format string.
        */
        for (positive length = 1; length < 24; length++)
                for (positive where = 0; where < length; where++)
                {
                        for (positive i = 0; i < length; i++)
                                format_text[i] = (b8)filler[i % (sizeof(filler) - 1)];

                        format_text[where] = (b8)(0x80 + (where * 7) % 128);
                        format_text[length] = 0;

                        catch_reset();
                        string_format(catch_writer, format_text);
                        same("string_format", "a byte over 0x7f in the text",
                             (positive)string_compare(caught, format_text), 0);

                        // and the same with a literal percent right after
                        // it, so the hunt has to answer the percent and not
                        // the high byte. Two of them, and the answer is the
                        // text with one percent where the pair was.
                        if (where + 2 < length)
                        {
                                static b8 expected_high[64];
                                positive out = 0;

                                format_text[where + 1] = '%';
                                format_text[where + 2] = '%';

                                for (positive i = 0; i < length; i++)
                                {
                                        if (i == where + 2)
                                                continue;

                                        expected_high[out++] = format_text[i];
                                }

                                expected_high[out] = 0;

                                catch_reset();
                                string_format(catch_writer, format_text);
                                same("string_format", "a high byte in front of a percent",
                                     (positive)string_compare(caught, expected_high), 0);
                        }
                }

        // The same text at every alignment, since a wide scan masks off what
        // sits before the string and the mask depends on where it starts.
        static b8 format_room[128];

        // Thirty two and not sixteen: where the hunt takes thirty two bytes
        // at a time it masks off what sits in front of the cursor by the low
        // five bits of it, and half the residues never came up at sixteen.
        for (positive off = 0; off < 32; off++)
                for (positive length = 0; length < 24; length++)
                {
                        for (positive i = 0; i < length; i++)
                                format_room[off + i] = (b8)('A' + i % 26);

                        format_room[off + length] = 0;

                        catch_reset();
                        string_format(catch_writer, format_room + off);
                        same("string_format", "text at every alignment",
                             (positive)string_compare(caught, format_room + off), 0);
                }
}

/*
        decimal_to_string on values that round awkwardly.

        The reference is the shape the assembly's own comment describes -- the
        sign, the whole part, a point, then six digits of fraction with the
        leading zeros written by hand -- because the C it replaced is gone and
        an approximation of it would only test itself. It is written here as
        one piece so that a change to the assembly has something to disagree
        with.
*/
fn reference_decimal_to_string(writer write, decimal value)
{
        if (0 > value)
        {
                write((address_any) "-", 1);
                value = -value;
        }

        bipolar whole = (bipolar)value;
        decimal fraction = value - (decimal)whole;

        if (whole < 0)
        {
                write((address_any) "-", 1);
                reference_positive_to_string(write, (positive)(-whole));
        }
        else
        {
                reference_positive_to_string(write, (positive)whole);
        }

        write((address_any) ".", 1);

        bipolar part = (bipolar)(fraction * 1000000.0);

        // Six digits, so the zeros a conversion would drop are written here.
        // The ladder stops at the first one that is not needed.
        if (part <= 99999)
        {
                write((address_any) "0", 1);

                if (part <= 9999)
                {
                        write((address_any) "0", 1);

                        if (part <= 999)
                        {
                                write((address_any) "0", 1);

                                if (part <= 99)
                                {
                                        write((address_any) "0", 1);

                                        if (part <= 9)
                                                write((address_any) "0", 1);
                                }
                        }
                }
        }

        if (part < 0)
        {
                write((address_any) "-", 1);
                part = -part;
        }

        reference_positive_to_string(write, (positive)part);
}

static b8 decimal_spelled[128];

fn one_decimal(decimal v, string_address detail)
{
        catch_reset();
        decimal_to_string(catch_writer, v);

        positive length = caught_length;

        for (positive i = 0; i <= length; i++)
                decimal_spelled[i] = caught[i];

        catch_reset();
        reference_decimal_to_string(catch_writer, v);

        same("decimal_to_string", detail, length, caught_length);
        same_bytes("decimal_to_string", detail, decimal_spelled, caught,
                   caught_length + 1);
}

/*
        The decimal specifier, which nothing above reaches.

        %f is the one argument kind that does not come out of the integer save
        area: it has its own cursor over its own set of registers, and on
        x86_64 a padded save area that the overflow is not padded to match.
        Every other case in this file either calls decimal_to_string directly
        or formats something that is not a decimal, so both of those arms had
        never run -- which is the shape of failure this file already carries a
        warning about further up.

        The answer to compare against is decimal_to_string's own, since that is
        what the specifier is defined to call.
*/
static b8 wanted_decimal[128];

fn one_format_decimal(decimal v, string_address detail)
{
        catch_reset();
        decimal_to_string(catch_writer, v);

        positive length = caught_length;

        for (positive i = 0; i <= length; i++)
                wanted_decimal[i] = caught[i];

        catch_reset();
        string_format(catch_writer, (string_address) "%f", v);

        same("string_format", detail, caught_length, length);
        same_bytes("string_format", detail, caught, wanted_decimal, length + 1);
}

static b8 expected_long[256];

/*
        A run of plain text long enough to reach the wide hunt, with a decimal
        argument still waiting.

        The wide hunt writes the vector registers the decimal arrived in. They
        were spilled in the prologue before any scan and the specifier reads
        them back out of the frame, so this is safe by the way the routine is
        built rather than by luck -- but every other case here has a run of one
        byte or none in front of its %f, so the two had never been in the same
        call.
*/
fn one_wide_decimal(string_address format, string_address text, decimal v)
{
        catch_reset();
        decimal_to_string(catch_writer, v);

        positive at = 0;

        while (text[at])
        {
                expected_long[at] = text[at];
                at++;
        }

        for (positive i = 0; i <= caught_length; i++)
                expected_long[at + i] = caught[i];

        catch_reset();
        string_format(catch_writer, format, v);

        same("string_format", "a wide run of text with a decimal waiting",
             (positive)string_compare(caught, expected_long), 0);
}

fn check_format_decimals()
{
        one_wide_decimal(
            (string_address) "a line of text long enough to go wide: %f",
            (string_address) "a line of text long enough to go wide: ", 1.5);
        one_wide_decimal(
            (string_address) "and one that rounds awkwardly on the end of it: %f",
            (string_address) "and one that rounds awkwardly on the end of it: ",
            0.0000005);
        one_wide_decimal(
            (string_address) "0123456789012345678901234567890123456789%f",
            (string_address) "0123456789012345678901234567890123456789", -2.25);

        static const decimal cases[] = {
            0.0, 1.5, -2.25, 0.0000005, -0.0000005, 123456.789, 1e6,
            3.14159265358979, 0.9999995, -1.0,
        };

        for (positive i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
                one_format_decimal(cases[i], (string_address) "one decimal");

        // Mixed with the other kinds, so the two cursors have to advance
        // past each other rather than in step.
        catch_reset();
        string_format(catch_writer, (string_address) "%s %f %p", "a", 1.5,
                      (positive)7);
        expect((string_address) "a decimal between the other kinds",
               (string_address) "a 1.500000 7");

        // and the other way round: the decimal first, so the run that goes
        // wide is scanned with the vector cursor already moved on
        catch_reset();
        decimal_to_string(catch_writer, 1.5);

        positive spelled_at = caught_length;

        for (positive i = 0; i <= spelled_at; i++)
                expected_long[i] = caught[i];

        {
                string_address rest =
                    (string_address) " then a run of text long enough to go wide";
                positive i = 0;

                while (rest[i])
                {
                        expected_long[spelled_at + i] = rest[i];
                        i++;
                }

                expected_long[spelled_at + i] = 0;
        }

        catch_reset();
        string_format(catch_writer,
                      (string_address) "%f then a run of text long enough to go wide",
                      1.5);
        same("string_format", "a wide run after a decimal",
             (positive)string_compare(caught, expected_long), 0);

        catch_reset();
        string_format(catch_writer, (string_address) "%f%s%f", 1.5, "|", -2.25);
        expect((string_address) "two decimals and a string",
               (string_address) "1.500000|-2.250000");

        /*
                More decimals than there are registers to pass them in.

                x86_64 has eight vector registers for this and arm64 has
                eight; riscv passes them in the integer ones, of which six are
                left after the writer and the format. So nine reaches the
                stack on all three, and the ninth is the first one that does
                on the two that have the most.
        */
        catch_reset();
        string_format(catch_writer,
                      (string_address) "%f %f %f %f %f %f %f %f %f",
                      1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0);
        expect((string_address) "nine decimals, past every register",
               (string_address) "1.000000 2.000000 3.000000 4.000000 5.000000 "
                                "6.000000 7.000000 8.000000 9.000000");

        catch_reset();
        string_format(catch_writer,
                      (string_address) "%f%f%f%f%f%f%f%f%f%f%f%f",
                      0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5,
                      10.5, 11.5);
        expect((string_address) "twelve decimals, well past them",
               (string_address) "0.5000001.5000002.5000003.5000004.500000"
                                "5.5000006.5000007.5000008.5000009.500000"
                                "10.50000011.500000");

        // and the kinds interleaved past both sets of registers at once
        catch_reset();
        string_format(catch_writer,
                      (string_address) "%p%f%s%p%f%s%p%f%s%p%f%s",
                      (positive)1, 1.5, "a", (positive)2, 2.5, "b",
                      (positive)3, 3.5, "c", (positive)4, 4.5, "d");
        expect((string_address) "every kind, past every register",
               (string_address) "11.500000a22.500000b33.500000c44.500000d");
}

fn check_decimals_deep()
{
        static const decimal awkward[] = {
            0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 1.5, -2.25, 2.675,
            0.1, 0.2, 0.3, 0.30000000000000004, 0.7, 0.9999995, -0.9999995,
            0.0000005, -0.0000005, 0.0000001, 0.9999999, 1.0000001,
            3.14159265358979, 2.71828182845905,
            123456.789, -123456.789, 999999.9999995, 1000000.0, -1000000.0,
            8388608.5, 16777216.25, 1e9, -1e9, 1e15, 4294967295.5,
            0.000001, 0.000009, 0.00001, 0.0001, 0.001, 0.01, 0.1,
            9007199254740992.0, 123.456789, 99.999999, 9.9999999,
        };

        for (positive i = 0; i < sizeof(awkward) / sizeof(awkward[0]); i++)
                one_decimal(awkward[i], (string_address) "a value that rounds awkwardly");

        // And a spread, so the fraction lands on every leading zero count
        for (positive i = 0; i < 512; i++)
        {
                decimal whole = (decimal)(bipolar)(next() % 1000000);
                decimal fraction = (decimal)(next() % 1000000) / 1000000.0;
                decimal v = whole + fraction;

                one_decimal(v, (string_address) "a spread of fractions");
                one_decimal(-v, (string_address) "a negative spread");
        }
}

/*
        Paths are byte operations with three unusually important boundaries:
        capacity zero must not even inspect its pointers, capacity one owns
        only the terminator, and a component can be much longer than the
        shell's 4096-byte path room. These references are the C bodies the
        assembly replaced, made truthfully bounded at zero and for dirname.
*/
#define PATH_ROOM 8256
#define PATH_GUARD 64

static p8 path_mine[PATH_ROOM];
static p8 path_want[PATH_ROOM];
static p8 path_source_a[PATH_ROOM];
static p8 path_source_b[PATH_ROOM];
static p8 path_page_a[3 * 4096];
static p8 path_page_b[3 * 4096];
static p8 path_written[PATH_ROOM];
static positive path_written_length;

static positive reference_path_join(p8 address_to into, positive capacity,
                                    string_address directory, string_address name)
{
        if (!capacity)
                return 0;

        positive length = reference_length(directory);

        if (length > capacity - 1)
                length = capacity - 1;

        for (positive i = 0; i < length; i++)
                into[i] = directory[i];

        if (length && into[length - 1] != '/' && length + 1 < capacity)
                into[length++] = '/';

        positive tail = reference_length(name);

        if (tail > capacity - 1 - length)
                tail = capacity - 1 - length;

        for (positive i = 0; i < tail; i++)
                into[length + i] = name[i];

        length += tail;
        into[length] = 0;
        return length;
}

static positive reference_path_tail(p8 address_to into, positive capacity,
                                    string_address path)
{
        if (!capacity)
                return 0;

        positive length = reference_length(path);

        while (length > 1 && path[length - 1] == '/')
                length--;

        positive start = length;

        if (!(length == 1 && path[0] == '/'))
                while (start && path[start - 1] != '/')
                        start--;
        else
                start = 0;

        positive found = length - start;

        if (found > capacity - 1)
                found = capacity - 1;

        for (positive i = 0; i < found; i++)
                into[i] = path[start + i];

        into[found] = 0;
        return found;
}

static positive reference_path_head(p8 address_to into, positive capacity,
                                    string_address path)
{
        if (!capacity)
                return 0;

        positive length = reference_length(path);

        while (length > 1 && path[length - 1] == '/')
                length--;

        positive cut = length;

        while (cut && path[cut - 1] != '/')
                cut--;

        if (!cut)
        {
                if (capacity > 1)
                {
                        into[0] = '.';
                        into[1] = 0;
                        return 1;
                }

                into[0] = 0;
                return 0;
        }

        while (cut > 1 && path[cut - 1] == '/')
                cut--;

        if (cut > capacity - 1)
                cut = capacity - 1;

        for (positive i = 0; i < cut; i++)
                into[i] = path[i];

        into[cut] = 0;
        return cut;
}

static fn path_test_writer(address_any data, positive length)
{
        for (positive i = 0; i < length; i++)
                path_written[i] = ((p8 address_to)data)[i];

        path_written_length = length;
        path_written[length] = 0;
}

static fn path_buffers_reset()
{
        reference_fill(path_mine, (b8)0xa5, PATH_ROOM);
        reference_fill(path_want, (b8)0xa5, PATH_ROOM);
}

static string_address path_place(p8 address_to room, positive offset,
                                 string_address text)
{
        positive length = reference_length(text);

        for (positive i = 0; i <= length; i++)
                room[offset + i] = text[i];

        return (string_address)(room + offset);
}

static fn path_tail_head_case(string_address path, positive capacity,
                              positive offset, string_address detail)
{
        p8 address_to got = path_mine + PATH_GUARD + offset;
        p8 address_to want = path_want + PATH_GUARD + offset;

        path_buffers_reset();
        positive got_length = path_tail_copy(got, capacity, path);
        positive want_length = reference_path_tail(want, capacity, path);
        same("path_tail_copy", detail, got_length, want_length);
        same_bytes("path_tail_copy", "guarded destination", path_mine,
                   path_want, PATH_ROOM);

        path_buffers_reset();
        got_length = path_head_copy(got, capacity, path);
        want_length = reference_path_head(want, capacity, path);
        same("path_head_copy", detail, got_length, want_length);
        same_bytes("path_head_copy", "guarded destination", path_mine,
                   path_want, PATH_ROOM);
}

static fn path_join_case(string_address directory, string_address name,
                         positive capacity, positive offset, string_address detail)
{
        p8 address_to got = path_mine + PATH_GUARD + offset;
        p8 address_to want = path_want + PATH_GUARD + offset;

        path_buffers_reset();
        positive got_length = path_join(got, capacity, directory, name);
        positive want_length = reference_path_join(want, capacity, directory, name);
        same("path_join", detail, got_length, want_length);
        same_bytes("path_join", "guarded destination", path_mine,
                   path_want, PATH_ROOM);
}

fn check_paths()
{
        static const string_address paths[] = {
            "",       "/",       "//",        "///",       "a",
            "a/",     "a//",     "/a",        "/a/",       "//a",
            "a/b",    "a//b",    "/a//b///",  ".",         "..",
            "./a",    "../a",    "alpha/beta", "alpha///beta////",
        };
        static const positive capacities[] = {0, 1, 2, 3, 7, 8, 4095, 4096};
        static const string_address directories[] = {
            "", "/", "//", "a", "a/", "a//", "/a", "/a/", "alpha/beta",
        };
        static const string_address names[] = {
            "", "b", "/b", "b/", "//b", "gamma/delta", "////",
        };

        // Capacity zero is stronger than a zero-byte result: no pointer is
        // inspected, so even null is a valid argument in this one case.
        same("path_join", "zero capacity null pointers",
             path_join(null, 0, null, null), 0);
        same("path_tail_copy", "zero capacity null pointers",
             path_tail_copy(null, 0, null), 0);
        same("path_head_copy", "zero capacity null pointers",
             path_head_copy(null, 0, null), 0);

        for (positive p = 0; p < sizeof(paths) / sizeof(paths[0]); p++)
        {
                for (positive c = 0; c < sizeof(capacities) / sizeof(capacities[0]); c++)
                        path_tail_head_case(paths[p], capacities[c], p & 7,
                                            "semantic corpus and capacity");

                reference_fill(path_written, (b8)0xa5, PATH_ROOM);
                path_written_length = positive_max;
                path_basename(path_test_writer, paths[p]);
                reference_fill(path_want, (b8)0xa5, PATH_ROOM);
                positive wanted = reference_path_tail(path_want, PATH_ROOM, paths[p]);
                same("path_basename", "shared split length", path_written_length,
                     wanted);
                same_bytes("path_basename", "shared split bytes", path_written,
                           path_want, wanted + 1);
        }

        for (positive i = 0; i < sizeof(directories) / sizeof(directories[0]); i++)
                for (positive c = 0; c < sizeof(capacities) / sizeof(capacities[0]); c++)
                        path_join_case(directories[i],
                                       names[(i + c) % (sizeof(names) / sizeof(names[0]))],
                                       capacities[c], (i + c) & 7,
                                       "slash and truncation corpus");

        // Every byte residue for all three pointers. The source offsets use
        // different odd strides, so the three alignments do not accidentally
        // stay congruent while each still visits all thirty two values.
        for (positive residue = 0; residue < 32; residue++)
        {
                string_address path = path_place(path_source_a, 64 + residue * 7 % 32,
                                                 "/alpha//beta///");
                path_tail_head_case(path, 64, residue, "every pointer residue");

                string_address directory =
                    path_place(path_source_a, 256 + residue * 11 % 32, "alpha/beta");
                string_address name =
                    path_place(path_source_b, 256 + residue * 13 % 32, "/gamma");
                path_join_case(directory, name, 64, residue, "every pointer residue");
        }

        // Overlong final component, overlong directory prefix, and both join
        // inputs overlong. These are the old dirname overflow and join/tail
        // truncation boundaries, with poison after the full 4096-byte room.
        for (positive i = 0; i < 6000; i++)
        {
                path_source_a[64 + i] = 'a';
                path_source_b[64 + i] = 'b';
        }
        path_source_a[64] = '/';
        path_source_a[64 + 6000] = 0;
        path_source_b[64 + 5000] = '/';
        path_source_b[64 + 6000] = 0;
        path_tail_head_case((string_address)(path_source_a + 64), 4096, 17,
                            "overlong final component");
        path_tail_head_case((string_address)(path_source_b + 64), 4096, 19,
                            "overlong directory prefix");
        path_join_case((string_address)(path_source_a + 64),
                       (string_address)(path_source_b + 64), 4096, 23,
                       "overlong directory and name");

        // Terminators immediately before and after page boundaries. The path
        // routines borrow string_length, whose aligned loads make these the
        // significant page positions even though this static test maps all
        // three pages so it is portable under every freestanding runner.
        positive base_a = (positive)(address_any)path_page_a;
        positive base_b = (positive)(address_any)path_page_b;
        positive edge_a = (4096 - (base_a & 4095)) & 4095;
        positive edge_b = (4096 - (base_b & 4095)) & 4095;
        if (!edge_a) edge_a = 4096;
        if (!edge_b) edge_b = 4096;

        string_address edge_text = "/page//edge///";
        positive edge_length = reference_length(edge_text);
        if (edge_a <= edge_length + 1) edge_a += 4096;
        if (edge_b < 5) edge_b += 4096;
        string_address edge_path = path_place(path_page_a,
                                              edge_a - edge_length - 1,
                                              edge_text);
        path_tail_head_case(edge_path, 4096, 29, "terminator at a page edge");

        string_address edge_directory = path_place(path_page_a,
                                                   edge_a + 1, "page/directory");
        string_address edge_name = path_place(path_page_b,
                                              edge_b - 5, "name");
        path_join_case(edge_directory, edge_name, 4096, 31,
                       "sources around page edges");
}

/*
        unicode_width against the two functions it replaced, at every code
        point in both variants, then past U+10FFFF at every power of two and
        either side of it. The code goes in with the register's high half
        dirty on odd code points, which a 32-bit argument allows. Last, the
        table against the one the references lay out, so the table shipped is
        the layout lib.c describes and not just one that happens to agree.
*/
#define SHARED_unicode_width_reference
#include "../checks.c"
#undef SHARED_unicode_width_reference

static fn unicode_width_one(p32 code, positive address_to wrong, p32 address_to first)
{
        positive (*volatile width)(positive, positive) =
            (positive (*)(positive, positive))(address_any)unicode_width;
        positive dirty = code & 1 ? (positive)0xa5a5a5a5u << 32 : 0;

        if (width(dirty | code, UNICODE_WIDTH_TERMINAL) !=
            unicode_width_reference_terminal(code))
                if (!wrong[0]++)
                        first[0] = code;

        if (width(dirty | code, UNICODE_WIDTH_WCWIDTH) !=
            unicode_width_reference_wcwidth(code))
                if (!wrong[1]++)
                        first[1] = code;
}

fn check_unicode_width()
{
        static p8 table[9472];
        positive wrong[2] = {0, 0};
        positive above[2] = {0, 0};
        p32 first[2] = {0, 0};
        p32 past[2] = {0, 0};
        positive tables = 0;
        positive blocks_at = 0;
        positive leaves_at = 0;

        for (p32 code = 0; code <= 0x10ffff; code++)
        {
                unicode_width_one(code, wrong, first);
                tables += unicode_width_reference_terminal(code) !=
                          unicode_width_reference_tables(code);
        }

        for (positive bit = 20; bit < 32; bit++)
                for (p32 near = 0; near < 3; near++)
                        unicode_width_one(((p32)1 << bit) + near - 1, above, past);
        for (p32 code = 0x110000; code < 0x110100; code++)
                unicode_width_one(code, above, past);
        unicode_width_one(0xffffffffu, above, past);

        same("unicode_width", "terminal at U+0000-10FFFF", wrong[0], 0);
        same("unicode_width", "wcwidth at U+0000-10FFFF", wrong[1], 0);
        same("unicode_width", "both past U+10FFFF", above[0] + above[1], 0);
        if (wrong[0] || wrong[1] || above[0] || above[1])
                string_format(log, "  first wrong: terminal %p, wcwidth %p, "
                                   "past the range %p and %p\n",
                              (positive)first[0], (positive)first[1],
                              (positive)past[0], (positive)past[1]);
        same("unicode_width", "term.c's shortcut against its tables", tables, 0);

        positive size = unicode_width_reference_table(table, sizeof table,
                                                      address_of blocks_at,
                                                      address_of leaves_at);
        same("unicode_width", "table size from the references", size,
             sizeof table);
        same("unicode_width", "where the blocks start", blocks_at, 2056);
        same("unicode_width", "where the leaves start", leaves_at, 5912);
        same_bytes("unicode_width", "table from the references",
                   (b8 address_to)unicode_width_tab, (b8 address_to)table,
                   sizeof table);
}

/*
        unicode_case against glibc 2.44's towupper and towlower, as runs: each
        row is a first code point, how many the run has, the step between
        them (1, or 2 where upper and lower alternate), and the distance each
        moves in upper case and in lower case. Every code point from U+0000
        to U+10FFFF in all three modes (the fold is the upper case's lower
        case), then the modes that are none, and the code points past
        U+10FFFF at every power of two and either side of it, with the high
        half of the register dirty on odd ones. The rows are what a program
        printing c, towupper(c) and towlower(c) for every c either changes
        gives when its consecutive answers of one distance are joined.
*/
struct unicode_case_reference_row
{
        unsigned int first, count, step;
        int upper, lower;
};

static const struct unicode_case_reference_row unicode_case_reference_rows[] = {
        {65,26,1,0,32}, {97,26,1,-32,0}, {181,1,1,743,0}, {192,23,1,0,32}, {216,7,1,0,32},
        {224,23,1,-32,0}, {248,7,1,-32,0}, {255,1,1,121,0}, {256,1,1,0,1}, {257,1,1,-1,0},
        {258,1,1,0,1}, {259,1,1,-1,0}, {260,1,1,0,1}, {261,1,1,-1,0}, {262,1,1,0,1},
        {263,1,1,-1,0}, {264,1,1,0,1}, {265,1,1,-1,0}, {266,1,1,0,1}, {267,1,1,-1,0},
        {268,1,1,0,1}, {269,1,1,-1,0}, {270,1,1,0,1}, {271,1,1,-1,0}, {272,1,1,0,1},
        {273,1,1,-1,0}, {274,1,1,0,1}, {275,1,1,-1,0}, {276,1,1,0,1}, {277,1,1,-1,0},
        {278,1,1,0,1}, {279,1,1,-1,0}, {280,1,1,0,1}, {281,1,1,-1,0}, {282,1,1,0,1},
        {283,1,1,-1,0}, {284,1,1,0,1}, {285,1,1,-1,0}, {286,1,1,0,1}, {287,1,1,-1,0},
        {288,1,1,0,1}, {289,1,1,-1,0}, {290,1,1,0,1}, {291,1,1,-1,0}, {292,1,1,0,1},
        {293,1,1,-1,0}, {294,1,1,0,1}, {295,1,1,-1,0}, {296,1,1,0,1}, {297,1,1,-1,0},
        {298,1,1,0,1}, {299,1,1,-1,0}, {300,1,1,0,1}, {301,1,1,-1,0}, {302,1,1,0,1},
        {303,1,1,-1,0}, {304,1,1,0,-199}, {305,1,1,-232,0}, {306,1,1,0,1}, {307,1,1,-1,0},
        {308,1,1,0,1}, {309,1,1,-1,0}, {310,1,1,0,1}, {311,1,1,-1,0}, {313,1,1,0,1},
        {314,1,1,-1,0}, {315,1,1,0,1}, {316,1,1,-1,0}, {317,1,1,0,1}, {318,1,1,-1,0},
        {319,1,1,0,1}, {320,1,1,-1,0}, {321,1,1,0,1}, {322,1,1,-1,0}, {323,1,1,0,1},
        {324,1,1,-1,0}, {325,1,1,0,1}, {326,1,1,-1,0}, {327,1,1,0,1}, {328,1,1,-1,0},
        {330,1,1,0,1}, {331,1,1,-1,0}, {332,1,1,0,1}, {333,1,1,-1,0}, {334,1,1,0,1},
        {335,1,1,-1,0}, {336,1,1,0,1}, {337,1,1,-1,0}, {338,1,1,0,1}, {339,1,1,-1,0},
        {340,1,1,0,1}, {341,1,1,-1,0}, {342,1,1,0,1}, {343,1,1,-1,0}, {344,1,1,0,1},
        {345,1,1,-1,0}, {346,1,1,0,1}, {347,1,1,-1,0}, {348,1,1,0,1}, {349,1,1,-1,0},
        {350,1,1,0,1}, {351,1,1,-1,0}, {352,1,1,0,1}, {353,1,1,-1,0}, {354,1,1,0,1},
        {355,1,1,-1,0}, {356,1,1,0,1}, {357,1,1,-1,0}, {358,1,1,0,1}, {359,1,1,-1,0},
        {360,1,1,0,1}, {361,1,1,-1,0}, {362,1,1,0,1}, {363,1,1,-1,0}, {364,1,1,0,1},
        {365,1,1,-1,0}, {366,1,1,0,1}, {367,1,1,-1,0}, {368,1,1,0,1}, {369,1,1,-1,0},
        {370,1,1,0,1}, {371,1,1,-1,0}, {372,1,1,0,1}, {373,1,1,-1,0}, {374,1,1,0,1},
        {375,1,1,-1,0}, {376,1,1,0,-121}, {377,1,1,0,1}, {378,1,1,-1,0}, {379,1,1,0,1},
        {380,1,1,-1,0}, {381,1,1,0,1}, {382,1,1,-1,0}, {383,1,1,-300,0}, {384,1,1,195,0},
        {385,1,1,0,210}, {386,1,1,0,1}, {387,1,1,-1,0}, {388,1,1,0,1}, {389,1,1,-1,0},
        {390,1,1,0,206}, {391,1,1,0,1}, {392,1,1,-1,0}, {393,2,1,0,205}, {395,1,1,0,1},
        {396,1,1,-1,0}, {398,1,1,0,79}, {399,1,1,0,202}, {400,1,1,0,203}, {401,1,1,0,1},
        {402,1,1,-1,0}, {403,1,1,0,205}, {404,1,1,0,207}, {405,1,1,97,0}, {406,1,1,0,211},
        {407,1,1,0,209}, {408,1,1,0,1}, {409,1,1,-1,0}, {410,1,1,163,0}, {411,1,1,42561,0},
        {412,1,1,0,211}, {413,1,1,0,213}, {414,1,1,130,0}, {415,1,1,0,214}, {416,1,1,0,1},
        {417,1,1,-1,0}, {418,1,1,0,1}, {419,1,1,-1,0}, {420,1,1,0,1}, {421,1,1,-1,0},
        {422,1,1,0,218}, {423,1,1,0,1}, {424,1,1,-1,0}, {425,1,1,0,218}, {428,1,1,0,1},
        {429,1,1,-1,0}, {430,1,1,0,218}, {431,1,1,0,1}, {432,1,1,-1,0}, {433,2,1,0,217},
        {435,1,1,0,1}, {436,1,1,-1,0}, {437,1,1,0,1}, {438,1,1,-1,0}, {439,1,1,0,219},
        {440,1,1,0,1}, {441,1,1,-1,0}, {444,1,1,0,1}, {445,1,1,-1,0}, {447,1,1,56,0},
        {452,1,1,0,2}, {453,1,1,-1,1}, {454,1,1,-2,0}, {455,1,1,0,2}, {456,1,1,-1,1},
        {457,1,1,-2,0}, {458,1,1,0,2}, {459,1,1,-1,1}, {460,1,1,-2,0}, {461,1,1,0,1},
        {462,1,1,-1,0}, {463,1,1,0,1}, {464,1,1,-1,0}, {465,1,1,0,1}, {466,1,1,-1,0},
        {467,1,1,0,1}, {468,1,1,-1,0}, {469,1,1,0,1}, {470,1,1,-1,0}, {471,1,1,0,1},
        {472,1,1,-1,0}, {473,1,1,0,1}, {474,1,1,-1,0}, {475,1,1,0,1}, {476,1,1,-1,0},
        {477,1,1,-79,0}, {478,1,1,0,1}, {479,1,1,-1,0}, {480,1,1,0,1}, {481,1,1,-1,0},
        {482,1,1,0,1}, {483,1,1,-1,0}, {484,1,1,0,1}, {485,1,1,-1,0}, {486,1,1,0,1},
        {487,1,1,-1,0}, {488,1,1,0,1}, {489,1,1,-1,0}, {490,1,1,0,1}, {491,1,1,-1,0},
        {492,1,1,0,1}, {493,1,1,-1,0}, {494,1,1,0,1}, {495,1,1,-1,0}, {497,1,1,0,2},
        {498,1,1,-1,1}, {499,1,1,-2,0}, {500,1,1,0,1}, {501,1,1,-1,0}, {502,1,1,0,-97},
        {503,1,1,0,-56}, {504,1,1,0,1}, {505,1,1,-1,0}, {506,1,1,0,1}, {507,1,1,-1,0},
        {508,1,1,0,1}, {509,1,1,-1,0}, {510,1,1,0,1}, {511,1,1,-1,0}, {512,1,1,0,1},
        {513,1,1,-1,0}, {514,1,1,0,1}, {515,1,1,-1,0}, {516,1,1,0,1}, {517,1,1,-1,0},
        {518,1,1,0,1}, {519,1,1,-1,0}, {520,1,1,0,1}, {521,1,1,-1,0}, {522,1,1,0,1},
        {523,1,1,-1,0}, {524,1,1,0,1}, {525,1,1,-1,0}, {526,1,1,0,1}, {527,1,1,-1,0},
        {528,1,1,0,1}, {529,1,1,-1,0}, {530,1,1,0,1}, {531,1,1,-1,0}, {532,1,1,0,1},
        {533,1,1,-1,0}, {534,1,1,0,1}, {535,1,1,-1,0}, {536,1,1,0,1}, {537,1,1,-1,0},
        {538,1,1,0,1}, {539,1,1,-1,0}, {540,1,1,0,1}, {541,1,1,-1,0}, {542,1,1,0,1},
        {543,1,1,-1,0}, {544,1,1,0,-130}, {546,1,1,0,1}, {547,1,1,-1,0}, {548,1,1,0,1},
        {549,1,1,-1,0}, {550,1,1,0,1}, {551,1,1,-1,0}, {552,1,1,0,1}, {553,1,1,-1,0},
        {554,1,1,0,1}, {555,1,1,-1,0}, {556,1,1,0,1}, {557,1,1,-1,0}, {558,1,1,0,1},
        {559,1,1,-1,0}, {560,1,1,0,1}, {561,1,1,-1,0}, {562,1,1,0,1}, {563,1,1,-1,0},
        {570,1,1,0,10795}, {571,1,1,0,1}, {572,1,1,-1,0}, {573,1,1,0,-163}, {574,1,1,0,10792},
        {575,2,1,10815,0}, {577,1,1,0,1}, {578,1,1,-1,0}, {579,1,1,0,-195}, {580,1,1,0,69},
        {581,1,1,0,71}, {582,1,1,0,1}, {583,1,1,-1,0}, {584,1,1,0,1}, {585,1,1,-1,0},
        {586,1,1,0,1}, {587,1,1,-1,0}, {588,1,1,0,1}, {589,1,1,-1,0}, {590,1,1,0,1},
        {591,1,1,-1,0}, {592,1,1,10783,0}, {593,1,1,10780,0}, {594,1,1,10782,0},
        {595,1,1,-210,0}, {596,1,1,-206,0}, {598,2,1,-205,0}, {601,1,1,-202,0},
        {603,1,1,-203,0}, {604,1,1,42319,0}, {608,1,1,-205,0}, {609,1,1,42315,0},
        {611,1,1,-207,0}, {612,1,1,42343,0}, {613,1,1,42280,0}, {614,1,1,42308,0},
        {616,1,1,-209,0}, {617,1,1,-211,0}, {618,1,1,42308,0}, {619,1,1,10743,0},
        {620,1,1,42305,0}, {623,1,1,-211,0}, {625,1,1,10749,0}, {626,1,1,-213,0},
        {629,1,1,-214,0}, {637,1,1,10727,0}, {640,1,1,-218,0}, {642,1,1,42307,0},
        {643,1,1,-218,0}, {647,1,1,42282,0}, {648,1,1,-218,0}, {649,1,1,-69,0},
        {650,2,1,-217,0}, {652,1,1,-71,0}, {658,1,1,-219,0}, {669,1,1,42261,0},
        {670,1,1,42258,0}, {837,1,1,84,0}, {880,1,1,0,1}, {881,1,1,-1,0}, {882,1,1,0,1},
        {883,1,1,-1,0}, {886,1,1,0,1}, {887,1,1,-1,0}, {891,3,1,130,0}, {895,1,1,0,116},
        {902,1,1,0,38}, {904,3,1,0,37}, {908,1,1,0,64}, {910,2,1,0,63}, {913,17,1,0,32},
        {931,9,1,0,32}, {940,1,1,-38,0}, {941,3,1,-37,0}, {945,17,1,-32,0}, {962,1,1,-31,0},
        {963,9,1,-32,0}, {972,1,1,-64,0}, {973,2,1,-63,0}, {975,1,1,0,8}, {976,1,1,-62,0},
        {977,1,1,-57,0}, {981,1,1,-47,0}, {982,1,1,-54,0}, {983,1,1,-8,0}, {984,1,1,0,1},
        {985,1,1,-1,0}, {986,1,1,0,1}, {987,1,1,-1,0}, {988,1,1,0,1}, {989,1,1,-1,0},
        {990,1,1,0,1}, {991,1,1,-1,0}, {992,1,1,0,1}, {993,1,1,-1,0}, {994,1,1,0,1},
        {995,1,1,-1,0}, {996,1,1,0,1}, {997,1,1,-1,0}, {998,1,1,0,1}, {999,1,1,-1,0},
        {1000,1,1,0,1}, {1001,1,1,-1,0}, {1002,1,1,0,1}, {1003,1,1,-1,0}, {1004,1,1,0,1},
        {1005,1,1,-1,0}, {1006,1,1,0,1}, {1007,1,1,-1,0}, {1008,1,1,-86,0}, {1009,1,1,-80,0},
        {1010,1,1,7,0}, {1011,1,1,-116,0}, {1012,1,1,0,-60}, {1013,1,1,-96,0}, {1015,1,1,0,1},
        {1016,1,1,-1,0}, {1017,1,1,0,-7}, {1018,1,1,0,1}, {1019,1,1,-1,0}, {1021,3,1,0,-130},
        {1024,16,1,0,80}, {1040,32,1,0,32}, {1072,32,1,-32,0}, {1104,16,1,-80,0},
        {1120,1,1,0,1}, {1121,1,1,-1,0}, {1122,1,1,0,1}, {1123,1,1,-1,0}, {1124,1,1,0,1},
        {1125,1,1,-1,0}, {1126,1,1,0,1}, {1127,1,1,-1,0}, {1128,1,1,0,1}, {1129,1,1,-1,0},
        {1130,1,1,0,1}, {1131,1,1,-1,0}, {1132,1,1,0,1}, {1133,1,1,-1,0}, {1134,1,1,0,1},
        {1135,1,1,-1,0}, {1136,1,1,0,1}, {1137,1,1,-1,0}, {1138,1,1,0,1}, {1139,1,1,-1,0},
        {1140,1,1,0,1}, {1141,1,1,-1,0}, {1142,1,1,0,1}, {1143,1,1,-1,0}, {1144,1,1,0,1},
        {1145,1,1,-1,0}, {1146,1,1,0,1}, {1147,1,1,-1,0}, {1148,1,1,0,1}, {1149,1,1,-1,0},
        {1150,1,1,0,1}, {1151,1,1,-1,0}, {1152,1,1,0,1}, {1153,1,1,-1,0}, {1162,1,1,0,1},
        {1163,1,1,-1,0}, {1164,1,1,0,1}, {1165,1,1,-1,0}, {1166,1,1,0,1}, {1167,1,1,-1,0},
        {1168,1,1,0,1}, {1169,1,1,-1,0}, {1170,1,1,0,1}, {1171,1,1,-1,0}, {1172,1,1,0,1},
        {1173,1,1,-1,0}, {1174,1,1,0,1}, {1175,1,1,-1,0}, {1176,1,1,0,1}, {1177,1,1,-1,0},
        {1178,1,1,0,1}, {1179,1,1,-1,0}, {1180,1,1,0,1}, {1181,1,1,-1,0}, {1182,1,1,0,1},
        {1183,1,1,-1,0}, {1184,1,1,0,1}, {1185,1,1,-1,0}, {1186,1,1,0,1}, {1187,1,1,-1,0},
        {1188,1,1,0,1}, {1189,1,1,-1,0}, {1190,1,1,0,1}, {1191,1,1,-1,0}, {1192,1,1,0,1},
        {1193,1,1,-1,0}, {1194,1,1,0,1}, {1195,1,1,-1,0}, {1196,1,1,0,1}, {1197,1,1,-1,0},
        {1198,1,1,0,1}, {1199,1,1,-1,0}, {1200,1,1,0,1}, {1201,1,1,-1,0}, {1202,1,1,0,1},
        {1203,1,1,-1,0}, {1204,1,1,0,1}, {1205,1,1,-1,0}, {1206,1,1,0,1}, {1207,1,1,-1,0},
        {1208,1,1,0,1}, {1209,1,1,-1,0}, {1210,1,1,0,1}, {1211,1,1,-1,0}, {1212,1,1,0,1},
        {1213,1,1,-1,0}, {1214,1,1,0,1}, {1215,1,1,-1,0}, {1216,1,1,0,15}, {1217,1,1,0,1},
        {1218,1,1,-1,0}, {1219,1,1,0,1}, {1220,1,1,-1,0}, {1221,1,1,0,1}, {1222,1,1,-1,0},
        {1223,1,1,0,1}, {1224,1,1,-1,0}, {1225,1,1,0,1}, {1226,1,1,-1,0}, {1227,1,1,0,1},
        {1228,1,1,-1,0}, {1229,1,1,0,1}, {1230,1,1,-1,0}, {1231,1,1,-15,0}, {1232,1,1,0,1},
        {1233,1,1,-1,0}, {1234,1,1,0,1}, {1235,1,1,-1,0}, {1236,1,1,0,1}, {1237,1,1,-1,0},
        {1238,1,1,0,1}, {1239,1,1,-1,0}, {1240,1,1,0,1}, {1241,1,1,-1,0}, {1242,1,1,0,1},
        {1243,1,1,-1,0}, {1244,1,1,0,1}, {1245,1,1,-1,0}, {1246,1,1,0,1}, {1247,1,1,-1,0},
        {1248,1,1,0,1}, {1249,1,1,-1,0}, {1250,1,1,0,1}, {1251,1,1,-1,0}, {1252,1,1,0,1},
        {1253,1,1,-1,0}, {1254,1,1,0,1}, {1255,1,1,-1,0}, {1256,1,1,0,1}, {1257,1,1,-1,0},
        {1258,1,1,0,1}, {1259,1,1,-1,0}, {1260,1,1,0,1}, {1261,1,1,-1,0}, {1262,1,1,0,1},
        {1263,1,1,-1,0}, {1264,1,1,0,1}, {1265,1,1,-1,0}, {1266,1,1,0,1}, {1267,1,1,-1,0},
        {1268,1,1,0,1}, {1269,1,1,-1,0}, {1270,1,1,0,1}, {1271,1,1,-1,0}, {1272,1,1,0,1},
        {1273,1,1,-1,0}, {1274,1,1,0,1}, {1275,1,1,-1,0}, {1276,1,1,0,1}, {1277,1,1,-1,0},
        {1278,1,1,0,1}, {1279,1,1,-1,0}, {1280,1,1,0,1}, {1281,1,1,-1,0}, {1282,1,1,0,1},
        {1283,1,1,-1,0}, {1284,1,1,0,1}, {1285,1,1,-1,0}, {1286,1,1,0,1}, {1287,1,1,-1,0},
        {1288,1,1,0,1}, {1289,1,1,-1,0}, {1290,1,1,0,1}, {1291,1,1,-1,0}, {1292,1,1,0,1},
        {1293,1,1,-1,0}, {1294,1,1,0,1}, {1295,1,1,-1,0}, {1296,1,1,0,1}, {1297,1,1,-1,0},
        {1298,1,1,0,1}, {1299,1,1,-1,0}, {1300,1,1,0,1}, {1301,1,1,-1,0}, {1302,1,1,0,1},
        {1303,1,1,-1,0}, {1304,1,1,0,1}, {1305,1,1,-1,0}, {1306,1,1,0,1}, {1307,1,1,-1,0},
        {1308,1,1,0,1}, {1309,1,1,-1,0}, {1310,1,1,0,1}, {1311,1,1,-1,0}, {1312,1,1,0,1},
        {1313,1,1,-1,0}, {1314,1,1,0,1}, {1315,1,1,-1,0}, {1316,1,1,0,1}, {1317,1,1,-1,0},
        {1318,1,1,0,1}, {1319,1,1,-1,0}, {1320,1,1,0,1}, {1321,1,1,-1,0}, {1322,1,1,0,1},
        {1323,1,1,-1,0}, {1324,1,1,0,1}, {1325,1,1,-1,0}, {1326,1,1,0,1}, {1327,1,1,-1,0},
        {1329,38,1,0,48}, {1377,38,1,-48,0}, {4256,38,1,0,7264}, {4295,1,1,0,7264},
        {4301,1,1,0,7264}, {4304,43,1,3008,0}, {4349,3,1,3008,0}, {5024,80,1,0,38864},
        {5104,6,1,0,8}, {5112,6,1,-8,0}, {7296,1,1,-6254,0}, {7297,1,1,-6253,0},
        {7298,1,1,-6244,0}, {7299,2,1,-6242,0}, {7301,1,1,-6243,0}, {7302,1,1,-6236,0},
        {7303,1,1,-6181,0}, {7304,1,1,35266,0}, {7305,1,1,0,1}, {7306,1,1,-1,0},
        {7312,43,1,0,-3008}, {7357,3,1,0,-3008}, {7545,1,1,35332,0}, {7549,1,1,3814,0},
        {7566,1,1,35384,0}, {7680,1,1,0,1}, {7681,1,1,-1,0}, {7682,1,1,0,1}, {7683,1,1,-1,0},
        {7684,1,1,0,1}, {7685,1,1,-1,0}, {7686,1,1,0,1}, {7687,1,1,-1,0}, {7688,1,1,0,1},
        {7689,1,1,-1,0}, {7690,1,1,0,1}, {7691,1,1,-1,0}, {7692,1,1,0,1}, {7693,1,1,-1,0},
        {7694,1,1,0,1}, {7695,1,1,-1,0}, {7696,1,1,0,1}, {7697,1,1,-1,0}, {7698,1,1,0,1},
        {7699,1,1,-1,0}, {7700,1,1,0,1}, {7701,1,1,-1,0}, {7702,1,1,0,1}, {7703,1,1,-1,0},
        {7704,1,1,0,1}, {7705,1,1,-1,0}, {7706,1,1,0,1}, {7707,1,1,-1,0}, {7708,1,1,0,1},
        {7709,1,1,-1,0}, {7710,1,1,0,1}, {7711,1,1,-1,0}, {7712,1,1,0,1}, {7713,1,1,-1,0},
        {7714,1,1,0,1}, {7715,1,1,-1,0}, {7716,1,1,0,1}, {7717,1,1,-1,0}, {7718,1,1,0,1},
        {7719,1,1,-1,0}, {7720,1,1,0,1}, {7721,1,1,-1,0}, {7722,1,1,0,1}, {7723,1,1,-1,0},
        {7724,1,1,0,1}, {7725,1,1,-1,0}, {7726,1,1,0,1}, {7727,1,1,-1,0}, {7728,1,1,0,1},
        {7729,1,1,-1,0}, {7730,1,1,0,1}, {7731,1,1,-1,0}, {7732,1,1,0,1}, {7733,1,1,-1,0},
        {7734,1,1,0,1}, {7735,1,1,-1,0}, {7736,1,1,0,1}, {7737,1,1,-1,0}, {7738,1,1,0,1},
        {7739,1,1,-1,0}, {7740,1,1,0,1}, {7741,1,1,-1,0}, {7742,1,1,0,1}, {7743,1,1,-1,0},
        {7744,1,1,0,1}, {7745,1,1,-1,0}, {7746,1,1,0,1}, {7747,1,1,-1,0}, {7748,1,1,0,1},
        {7749,1,1,-1,0}, {7750,1,1,0,1}, {7751,1,1,-1,0}, {7752,1,1,0,1}, {7753,1,1,-1,0},
        {7754,1,1,0,1}, {7755,1,1,-1,0}, {7756,1,1,0,1}, {7757,1,1,-1,0}, {7758,1,1,0,1},
        {7759,1,1,-1,0}, {7760,1,1,0,1}, {7761,1,1,-1,0}, {7762,1,1,0,1}, {7763,1,1,-1,0},
        {7764,1,1,0,1}, {7765,1,1,-1,0}, {7766,1,1,0,1}, {7767,1,1,-1,0}, {7768,1,1,0,1},
        {7769,1,1,-1,0}, {7770,1,1,0,1}, {7771,1,1,-1,0}, {7772,1,1,0,1}, {7773,1,1,-1,0},
        {7774,1,1,0,1}, {7775,1,1,-1,0}, {7776,1,1,0,1}, {7777,1,1,-1,0}, {7778,1,1,0,1},
        {7779,1,1,-1,0}, {7780,1,1,0,1}, {7781,1,1,-1,0}, {7782,1,1,0,1}, {7783,1,1,-1,0},
        {7784,1,1,0,1}, {7785,1,1,-1,0}, {7786,1,1,0,1}, {7787,1,1,-1,0}, {7788,1,1,0,1},
        {7789,1,1,-1,0}, {7790,1,1,0,1}, {7791,1,1,-1,0}, {7792,1,1,0,1}, {7793,1,1,-1,0},
        {7794,1,1,0,1}, {7795,1,1,-1,0}, {7796,1,1,0,1}, {7797,1,1,-1,0}, {7798,1,1,0,1},
        {7799,1,1,-1,0}, {7800,1,1,0,1}, {7801,1,1,-1,0}, {7802,1,1,0,1}, {7803,1,1,-1,0},
        {7804,1,1,0,1}, {7805,1,1,-1,0}, {7806,1,1,0,1}, {7807,1,1,-1,0}, {7808,1,1,0,1},
        {7809,1,1,-1,0}, {7810,1,1,0,1}, {7811,1,1,-1,0}, {7812,1,1,0,1}, {7813,1,1,-1,0},
        {7814,1,1,0,1}, {7815,1,1,-1,0}, {7816,1,1,0,1}, {7817,1,1,-1,0}, {7818,1,1,0,1},
        {7819,1,1,-1,0}, {7820,1,1,0,1}, {7821,1,1,-1,0}, {7822,1,1,0,1}, {7823,1,1,-1,0},
        {7824,1,1,0,1}, {7825,1,1,-1,0}, {7826,1,1,0,1}, {7827,1,1,-1,0}, {7828,1,1,0,1},
        {7829,1,1,-1,0}, {7835,1,1,-59,0}, {7838,1,1,0,-7615}, {7840,1,1,0,1}, {7841,1,1,-1,0},
        {7842,1,1,0,1}, {7843,1,1,-1,0}, {7844,1,1,0,1}, {7845,1,1,-1,0}, {7846,1,1,0,1},
        {7847,1,1,-1,0}, {7848,1,1,0,1}, {7849,1,1,-1,0}, {7850,1,1,0,1}, {7851,1,1,-1,0},
        {7852,1,1,0,1}, {7853,1,1,-1,0}, {7854,1,1,0,1}, {7855,1,1,-1,0}, {7856,1,1,0,1},
        {7857,1,1,-1,0}, {7858,1,1,0,1}, {7859,1,1,-1,0}, {7860,1,1,0,1}, {7861,1,1,-1,0},
        {7862,1,1,0,1}, {7863,1,1,-1,0}, {7864,1,1,0,1}, {7865,1,1,-1,0}, {7866,1,1,0,1},
        {7867,1,1,-1,0}, {7868,1,1,0,1}, {7869,1,1,-1,0}, {7870,1,1,0,1}, {7871,1,1,-1,0},
        {7872,1,1,0,1}, {7873,1,1,-1,0}, {7874,1,1,0,1}, {7875,1,1,-1,0}, {7876,1,1,0,1},
        {7877,1,1,-1,0}, {7878,1,1,0,1}, {7879,1,1,-1,0}, {7880,1,1,0,1}, {7881,1,1,-1,0},
        {7882,1,1,0,1}, {7883,1,1,-1,0}, {7884,1,1,0,1}, {7885,1,1,-1,0}, {7886,1,1,0,1},
        {7887,1,1,-1,0}, {7888,1,1,0,1}, {7889,1,1,-1,0}, {7890,1,1,0,1}, {7891,1,1,-1,0},
        {7892,1,1,0,1}, {7893,1,1,-1,0}, {7894,1,1,0,1}, {7895,1,1,-1,0}, {7896,1,1,0,1},
        {7897,1,1,-1,0}, {7898,1,1,0,1}, {7899,1,1,-1,0}, {7900,1,1,0,1}, {7901,1,1,-1,0},
        {7902,1,1,0,1}, {7903,1,1,-1,0}, {7904,1,1,0,1}, {7905,1,1,-1,0}, {7906,1,1,0,1},
        {7907,1,1,-1,0}, {7908,1,1,0,1}, {7909,1,1,-1,0}, {7910,1,1,0,1}, {7911,1,1,-1,0},
        {7912,1,1,0,1}, {7913,1,1,-1,0}, {7914,1,1,0,1}, {7915,1,1,-1,0}, {7916,1,1,0,1},
        {7917,1,1,-1,0}, {7918,1,1,0,1}, {7919,1,1,-1,0}, {7920,1,1,0,1}, {7921,1,1,-1,0},
        {7922,1,1,0,1}, {7923,1,1,-1,0}, {7924,1,1,0,1}, {7925,1,1,-1,0}, {7926,1,1,0,1},
        {7927,1,1,-1,0}, {7928,1,1,0,1}, {7929,1,1,-1,0}, {7930,1,1,0,1}, {7931,1,1,-1,0},
        {7932,1,1,0,1}, {7933,1,1,-1,0}, {7934,1,1,0,1}, {7935,1,1,-1,0}, {7936,8,1,8,0},
        {7944,8,1,0,-8}, {7952,6,1,8,0}, {7960,6,1,0,-8}, {7968,8,1,8,0}, {7976,8,1,0,-8},
        {7984,8,1,8,0}, {7992,8,1,0,-8}, {8000,6,1,8,0}, {8008,6,1,0,-8}, {8017,4,2,8,0},
        {8025,4,2,0,-8}, {8032,8,1,8,0}, {8040,8,1,0,-8}, {8048,2,1,74,0}, {8050,4,1,86,0},
        {8054,2,1,100,0}, {8056,2,1,128,0}, {8058,2,1,112,0}, {8060,2,1,126,0}, {8064,8,1,8,0},
        {8072,8,1,0,-8}, {8080,8,1,8,0}, {8088,8,1,0,-8}, {8096,8,1,8,0}, {8104,8,1,0,-8},
        {8112,2,1,8,0}, {8115,1,1,9,0}, {8120,2,1,0,-8}, {8122,2,1,0,-74}, {8124,1,1,0,-9},
        {8126,1,1,-7205,0}, {8131,1,1,9,0}, {8136,4,1,0,-86}, {8140,1,1,0,-9}, {8144,2,1,8,0},
        {8152,2,1,0,-8}, {8154,2,1,0,-100}, {8160,2,1,8,0}, {8165,1,1,7,0}, {8168,2,1,0,-8},
        {8170,2,1,0,-112}, {8172,1,1,0,-7}, {8179,1,1,9,0}, {8184,2,1,0,-128},
        {8186,2,1,0,-126}, {8188,1,1,0,-9}, {8486,1,1,0,-7517}, {8490,1,1,0,-8383},
        {8491,1,1,0,-8262}, {8498,1,1,0,28}, {8526,1,1,-28,0}, {8544,16,1,0,16},
        {8560,16,1,-16,0}, {8579,1,1,0,1}, {8580,1,1,-1,0}, {9398,26,1,0,26}, {9424,26,1,-26,0},
        {11264,48,1,0,48}, {11312,48,1,-48,0}, {11360,1,1,0,1}, {11361,1,1,-1,0},
        {11362,1,1,0,-10743}, {11363,1,1,0,-3814}, {11364,1,1,0,-10727}, {11365,1,1,-10795,0},
        {11366,1,1,-10792,0}, {11367,1,1,0,1}, {11368,1,1,-1,0}, {11369,1,1,0,1},
        {11370,1,1,-1,0}, {11371,1,1,0,1}, {11372,1,1,-1,0}, {11373,1,1,0,-10780},
        {11374,1,1,0,-10749}, {11375,1,1,0,-10783}, {11376,1,1,0,-10782}, {11378,1,1,0,1},
        {11379,1,1,-1,0}, {11381,1,1,0,1}, {11382,1,1,-1,0}, {11390,2,1,0,-10815},
        {11392,1,1,0,1}, {11393,1,1,-1,0}, {11394,1,1,0,1}, {11395,1,1,-1,0}, {11396,1,1,0,1},
        {11397,1,1,-1,0}, {11398,1,1,0,1}, {11399,1,1,-1,0}, {11400,1,1,0,1}, {11401,1,1,-1,0},
        {11402,1,1,0,1}, {11403,1,1,-1,0}, {11404,1,1,0,1}, {11405,1,1,-1,0}, {11406,1,1,0,1},
        {11407,1,1,-1,0}, {11408,1,1,0,1}, {11409,1,1,-1,0}, {11410,1,1,0,1}, {11411,1,1,-1,0},
        {11412,1,1,0,1}, {11413,1,1,-1,0}, {11414,1,1,0,1}, {11415,1,1,-1,0}, {11416,1,1,0,1},
        {11417,1,1,-1,0}, {11418,1,1,0,1}, {11419,1,1,-1,0}, {11420,1,1,0,1}, {11421,1,1,-1,0},
        {11422,1,1,0,1}, {11423,1,1,-1,0}, {11424,1,1,0,1}, {11425,1,1,-1,0}, {11426,1,1,0,1},
        {11427,1,1,-1,0}, {11428,1,1,0,1}, {11429,1,1,-1,0}, {11430,1,1,0,1}, {11431,1,1,-1,0},
        {11432,1,1,0,1}, {11433,1,1,-1,0}, {11434,1,1,0,1}, {11435,1,1,-1,0}, {11436,1,1,0,1},
        {11437,1,1,-1,0}, {11438,1,1,0,1}, {11439,1,1,-1,0}, {11440,1,1,0,1}, {11441,1,1,-1,0},
        {11442,1,1,0,1}, {11443,1,1,-1,0}, {11444,1,1,0,1}, {11445,1,1,-1,0}, {11446,1,1,0,1},
        {11447,1,1,-1,0}, {11448,1,1,0,1}, {11449,1,1,-1,0}, {11450,1,1,0,1}, {11451,1,1,-1,0},
        {11452,1,1,0,1}, {11453,1,1,-1,0}, {11454,1,1,0,1}, {11455,1,1,-1,0}, {11456,1,1,0,1},
        {11457,1,1,-1,0}, {11458,1,1,0,1}, {11459,1,1,-1,0}, {11460,1,1,0,1}, {11461,1,1,-1,0},
        {11462,1,1,0,1}, {11463,1,1,-1,0}, {11464,1,1,0,1}, {11465,1,1,-1,0}, {11466,1,1,0,1},
        {11467,1,1,-1,0}, {11468,1,1,0,1}, {11469,1,1,-1,0}, {11470,1,1,0,1}, {11471,1,1,-1,0},
        {11472,1,1,0,1}, {11473,1,1,-1,0}, {11474,1,1,0,1}, {11475,1,1,-1,0}, {11476,1,1,0,1},
        {11477,1,1,-1,0}, {11478,1,1,0,1}, {11479,1,1,-1,0}, {11480,1,1,0,1}, {11481,1,1,-1,0},
        {11482,1,1,0,1}, {11483,1,1,-1,0}, {11484,1,1,0,1}, {11485,1,1,-1,0}, {11486,1,1,0,1},
        {11487,1,1,-1,0}, {11488,1,1,0,1}, {11489,1,1,-1,0}, {11490,1,1,0,1}, {11491,1,1,-1,0},
        {11499,1,1,0,1}, {11500,1,1,-1,0}, {11501,1,1,0,1}, {11502,1,1,-1,0}, {11506,1,1,0,1},
        {11507,1,1,-1,0}, {11520,38,1,-7264,0}, {11559,1,1,-7264,0}, {11565,1,1,-7264,0},
        {42560,1,1,0,1}, {42561,1,1,-1,0}, {42562,1,1,0,1}, {42563,1,1,-1,0}, {42564,1,1,0,1},
        {42565,1,1,-1,0}, {42566,1,1,0,1}, {42567,1,1,-1,0}, {42568,1,1,0,1}, {42569,1,1,-1,0},
        {42570,1,1,0,1}, {42571,1,1,-1,0}, {42572,1,1,0,1}, {42573,1,1,-1,0}, {42574,1,1,0,1},
        {42575,1,1,-1,0}, {42576,1,1,0,1}, {42577,1,1,-1,0}, {42578,1,1,0,1}, {42579,1,1,-1,0},
        {42580,1,1,0,1}, {42581,1,1,-1,0}, {42582,1,1,0,1}, {42583,1,1,-1,0}, {42584,1,1,0,1},
        {42585,1,1,-1,0}, {42586,1,1,0,1}, {42587,1,1,-1,0}, {42588,1,1,0,1}, {42589,1,1,-1,0},
        {42590,1,1,0,1}, {42591,1,1,-1,0}, {42592,1,1,0,1}, {42593,1,1,-1,0}, {42594,1,1,0,1},
        {42595,1,1,-1,0}, {42596,1,1,0,1}, {42597,1,1,-1,0}, {42598,1,1,0,1}, {42599,1,1,-1,0},
        {42600,1,1,0,1}, {42601,1,1,-1,0}, {42602,1,1,0,1}, {42603,1,1,-1,0}, {42604,1,1,0,1},
        {42605,1,1,-1,0}, {42624,1,1,0,1}, {42625,1,1,-1,0}, {42626,1,1,0,1}, {42627,1,1,-1,0},
        {42628,1,1,0,1}, {42629,1,1,-1,0}, {42630,1,1,0,1}, {42631,1,1,-1,0}, {42632,1,1,0,1},
        {42633,1,1,-1,0}, {42634,1,1,0,1}, {42635,1,1,-1,0}, {42636,1,1,0,1}, {42637,1,1,-1,0},
        {42638,1,1,0,1}, {42639,1,1,-1,0}, {42640,1,1,0,1}, {42641,1,1,-1,0}, {42642,1,1,0,1},
        {42643,1,1,-1,0}, {42644,1,1,0,1}, {42645,1,1,-1,0}, {42646,1,1,0,1}, {42647,1,1,-1,0},
        {42648,1,1,0,1}, {42649,1,1,-1,0}, {42650,1,1,0,1}, {42651,1,1,-1,0}, {42786,1,1,0,1},
        {42787,1,1,-1,0}, {42788,1,1,0,1}, {42789,1,1,-1,0}, {42790,1,1,0,1}, {42791,1,1,-1,0},
        {42792,1,1,0,1}, {42793,1,1,-1,0}, {42794,1,1,0,1}, {42795,1,1,-1,0}, {42796,1,1,0,1},
        {42797,1,1,-1,0}, {42798,1,1,0,1}, {42799,1,1,-1,0}, {42802,1,1,0,1}, {42803,1,1,-1,0},
        {42804,1,1,0,1}, {42805,1,1,-1,0}, {42806,1,1,0,1}, {42807,1,1,-1,0}, {42808,1,1,0,1},
        {42809,1,1,-1,0}, {42810,1,1,0,1}, {42811,1,1,-1,0}, {42812,1,1,0,1}, {42813,1,1,-1,0},
        {42814,1,1,0,1}, {42815,1,1,-1,0}, {42816,1,1,0,1}, {42817,1,1,-1,0}, {42818,1,1,0,1},
        {42819,1,1,-1,0}, {42820,1,1,0,1}, {42821,1,1,-1,0}, {42822,1,1,0,1}, {42823,1,1,-1,0},
        {42824,1,1,0,1}, {42825,1,1,-1,0}, {42826,1,1,0,1}, {42827,1,1,-1,0}, {42828,1,1,0,1},
        {42829,1,1,-1,0}, {42830,1,1,0,1}, {42831,1,1,-1,0}, {42832,1,1,0,1}, {42833,1,1,-1,0},
        {42834,1,1,0,1}, {42835,1,1,-1,0}, {42836,1,1,0,1}, {42837,1,1,-1,0}, {42838,1,1,0,1},
        {42839,1,1,-1,0}, {42840,1,1,0,1}, {42841,1,1,-1,0}, {42842,1,1,0,1}, {42843,1,1,-1,0},
        {42844,1,1,0,1}, {42845,1,1,-1,0}, {42846,1,1,0,1}, {42847,1,1,-1,0}, {42848,1,1,0,1},
        {42849,1,1,-1,0}, {42850,1,1,0,1}, {42851,1,1,-1,0}, {42852,1,1,0,1}, {42853,1,1,-1,0},
        {42854,1,1,0,1}, {42855,1,1,-1,0}, {42856,1,1,0,1}, {42857,1,1,-1,0}, {42858,1,1,0,1},
        {42859,1,1,-1,0}, {42860,1,1,0,1}, {42861,1,1,-1,0}, {42862,1,1,0,1}, {42863,1,1,-1,0},
        {42873,1,1,0,1}, {42874,1,1,-1,0}, {42875,1,1,0,1}, {42876,1,1,-1,0},
        {42877,1,1,0,-35332}, {42878,1,1,0,1}, {42879,1,1,-1,0}, {42880,1,1,0,1},
        {42881,1,1,-1,0}, {42882,1,1,0,1}, {42883,1,1,-1,0}, {42884,1,1,0,1}, {42885,1,1,-1,0},
        {42886,1,1,0,1}, {42887,1,1,-1,0}, {42891,1,1,0,1}, {42892,1,1,-1,0},
        {42893,1,1,0,-42280}, {42896,1,1,0,1}, {42897,1,1,-1,0}, {42898,1,1,0,1},
        {42899,1,1,-1,0}, {42900,1,1,48,0}, {42902,1,1,0,1}, {42903,1,1,-1,0}, {42904,1,1,0,1},
        {42905,1,1,-1,0}, {42906,1,1,0,1}, {42907,1,1,-1,0}, {42908,1,1,0,1}, {42909,1,1,-1,0},
        {42910,1,1,0,1}, {42911,1,1,-1,0}, {42912,1,1,0,1}, {42913,1,1,-1,0}, {42914,1,1,0,1},
        {42915,1,1,-1,0}, {42916,1,1,0,1}, {42917,1,1,-1,0}, {42918,1,1,0,1}, {42919,1,1,-1,0},
        {42920,1,1,0,1}, {42921,1,1,-1,0}, {42922,1,1,0,-42308}, {42923,1,1,0,-42319},
        {42924,1,1,0,-42315}, {42925,1,1,0,-42305}, {42926,1,1,0,-42308}, {42928,1,1,0,-42258},
        {42929,1,1,0,-42282}, {42930,1,1,0,-42261}, {42931,1,1,0,928}, {42932,1,1,0,1},
        {42933,1,1,-1,0}, {42934,1,1,0,1}, {42935,1,1,-1,0}, {42936,1,1,0,1}, {42937,1,1,-1,0},
        {42938,1,1,0,1}, {42939,1,1,-1,0}, {42940,1,1,0,1}, {42941,1,1,-1,0}, {42942,1,1,0,1},
        {42943,1,1,-1,0}, {42944,1,1,0,1}, {42945,1,1,-1,0}, {42946,1,1,0,1}, {42947,1,1,-1,0},
        {42948,1,1,0,-48}, {42949,1,1,0,-42307}, {42950,1,1,0,-35384}, {42951,1,1,0,1},
        {42952,1,1,-1,0}, {42953,1,1,0,1}, {42954,1,1,-1,0}, {42955,1,1,0,-42343},
        {42956,1,1,0,1}, {42957,1,1,-1,0}, {42958,1,1,0,1}, {42959,1,1,-1,0}, {42960,1,1,0,1},
        {42961,1,1,-1,0}, {42962,1,1,0,1}, {42963,1,1,-1,0}, {42964,1,1,0,1}, {42965,1,1,-1,0},
        {42966,1,1,0,1}, {42967,1,1,-1,0}, {42968,1,1,0,1}, {42969,1,1,-1,0}, {42970,1,1,0,1},
        {42971,1,1,-1,0}, {42972,1,1,0,-42561}, {42997,1,1,0,1}, {42998,1,1,-1,0},
        {43859,1,1,-928,0}, {43888,80,1,-38864,0}, {65313,26,1,0,32}, {65345,26,1,-32,0},
        {66560,40,1,0,40}, {66600,40,1,-40,0}, {66736,36,1,0,40}, {66776,36,1,-40,0},
        {66928,11,1,0,39}, {66940,15,1,0,39}, {66956,7,1,0,39}, {66964,2,1,0,39},
        {66967,11,1,-39,0}, {66979,15,1,-39,0}, {66995,7,1,-39,0}, {67003,2,1,-39,0},
        {68736,51,1,0,64}, {68800,51,1,-64,0}, {68944,22,1,0,32}, {68976,22,1,-32,0},
        {71840,32,1,0,32}, {71872,32,1,-32,0}, {93760,32,1,0,32}, {93792,32,1,-32,0},
        {93856,25,1,0,27}, {93883,25,1,-27,0}, {125184,34,1,0,34}, {125218,34,1,-34,0},
};

static PURE unsigned int unicode_case_reference(unsigned int c, positive mode)
{
        if (mode > 2)
                return c;
        for (positive pass = 0; pass < (mode == 2 ? 2 : 1); pass++)
        {
                positive low = 0, high = array_count(unicode_case_reference_rows);
                positive lower = mode == 2 ? pass : mode;

                while (low < high)
                {
                        positive middle = (low + high) / 2;

                        if (unicode_case_reference_rows[middle].first <= c)
                                low = middle + 1;
                        else
                                high = middle;
                }
                if (!low)
                        continue;

                const struct unicode_case_reference_row address_to row =
                    unicode_case_reference_rows + low - 1;
                unsigned int at = c - row->first;

                if (at % row->step == 0 && at / row->step < row->count)
                        c += lower ? row->lower : row->upper;
        }
        return c;
}

static fn unicode_case_one(p32 code, positive mode, positive address_to wrong, p32 address_to first)
{
        positive (*volatile lookup)(positive, positive) =
            (positive (*)(positive, positive))(address_any)unicode_case;
        positive dirty = code & 1 ? (positive)0xa5a5a5a5u << 32 : 0;

        if ((p32)lookup(dirty | code, mode) != unicode_case_reference(code, mode))
                if (!wrong[0]++)
                        first[0] = code;
}

fn check_unicode_case()
{
        positive wrong = 0, above = 0;
        p32 first = 0, past = 0;

        for (p32 code = 0; code <= 0x10ffff; code++)
                for (positive mode = 0; mode < 3; mode++)
                        unicode_case_one(code, mode, address_of wrong, address_of first);
        for (positive mode = 3; mode < 6; mode++)
                for (p32 code = 0x41; code < 0x1e9; code += 7)
                        unicode_case_one(code, mode, address_of above, address_of past);
        for (positive bit = 20; bit < 32; bit++)
                for (p32 near = 0; near < 3; near++)
                        for (positive mode = 0; mode < 3; mode++)
                                unicode_case_one(((p32)1 << bit) + near - 1, mode,
                                                 address_of above, address_of past);
        for (p32 code = 0x110000; code < 0x110100; code++)
                unicode_case_one(code, 2, address_of above, address_of past);
        unicode_case_one(0xffffffffu, 0, address_of above, address_of past);

        same("unicode_case", "upper, lower and fold at U+0000-10FFFF", wrong, 0);
        same("unicode_case", "past U+10FFFF and under other modes", above, 0);
        if (wrong || above)
                string_format(log, "  first wrong: %p, past the range or other mode %p\n",
                              (positive)first, (positive)past);
        // The letters everyone knows, so a reference that drifts with the table shows.
        same("unicode_case", "a", unicode_case('a', UNICODE_CASE_UPPER), 'A');
        same("unicode_case", "e acute", unicode_case(0xe9, UNICODE_CASE_UPPER), 0xc9);
        same("unicode_case", "Cyrillic ya", unicode_case(0x42f, UNICODE_CASE_LOWER), 0x44f);
        same("unicode_case", "final sigma", unicode_case(0x3c2, UNICODE_CASE_UPPER), 0x3a3);
        same("unicode_case", "sharp s stays", unicode_case(0xdf, UNICODE_CASE_UPPER), 0xdf);
        same("unicode_case", "Kelvin sign folds to k", unicode_case(0x212a, UNICODE_CASE_FOLD), 'k');
}

/*
        The shared numeric core, in one list.

        It runs twice: on its own, for the small cross-machine lane that a
        formatter change should not have to wait on unrelated platform tests
        for, and again as part of the full pass. Written out in both places it
        was two lists kept the same by hand, and a routine added to the full
        pass alone would quietly stop being checked on the machines that only
        have the small lane.
*/
fn check_formatters()
{
        check_into();
        check_padded();
        check_writer_fields();
        check_base_field();
        check_into_padded();
        check_into_pair();
        check_into_base();
        check_human_1024();
        check_human_nearest();
        check_wait_status_code();
        check_wait_status_code_base();
}

/*
        memory_copy_rect_out_u32: the rectangle memory_copy_rect_u32 makes, for every width under 250 words (the narrow
        rows, which are that copy) at zero to three rows and every width from 250 to 600 (the streamed rows begin at 256
        words: every head the destination's 64-byte line leaves at its sixteen word offsets, and every tail of the 128 and
        256-byte turns) at none, one and three, the source at several word offsets, strides of the width plus a few words. Each buffer has a page
        that faults on both sides of it, and each case is laid twice: ending a few words short of the page after it, every
        word between held to a canary, and starting a few words past the page before it, so a word read or written past
        either end faults or shows; the source is held to what it was. Then rectangles that overlap, in place and across
        rows, which are memory_copy's rows in row order. On x86_64 at each tier (AVX-512, AVX2 alone, neither), on riscv64
        with V as each call's first ask finds it and then without.
*/
static p8 copy_out_one(p32 address_to twin, positive twin_words, p32 address_to fwin, positive fwin_words, positive da, positive sa,
                       positive ds, positive ss, positive w, positive height, p8 first)
{
        p32 address_to frect = fwin + sa;
        p32 address_to trect = twin + da;
        positive span_d = height ? (height - 1) * ds + w : 0;
        p8 fine = true;

        for (positive i = 0; i < fwin_words; i++)
                fwin[i] = 0x5a000000u + (p32)i;
        for (positive i = 0; i < twin_words; i++)
                twin[i] = 0xdeadbeef;
        VECTOR_FIRST_CALL(first);
        memory_copy_rect_out_u32(trect, ds, frect, ss, w, height);
        for (positive r = 0; r < height; r++)
                for (positive c = 0; c < w; c++)
                        fine = fine && trect[r * ds + c] == frect[r * ss + c];
        for (positive i = 0; i < twin_words; i++)
        {
                b64 j = (b64)i - (b64)da;
                p8 inside = j >= 0 && j < (b64)span_d && (positive)j % ds < w;

                fine = fine && (inside || twin[i] == 0xdeadbeef);
        }
        for (positive i = 0; i < fwin_words; i++)
                fine = fine && fwin[i] == 0x5a000000u + (p32)i;
        return fine;
}

static fn check_copy_rect_out()
{
        static const positive gaps[3][2] = {{0, 0}, {1, 37}, {37, 1}};
        p8 address_to from_pages = memory(5 * 4096);
        p8 address_to to_pages = memory(5 * 4096);
        p8 guarded = from_pages && to_pages && system_call_3(syscall(mprotect), (positive)from_pages, 4096, 0) == 0 &&
                     system_call_3(syscall(mprotect), (positive)(from_pages + 4 * 4096), 4096, 0) == 0 &&
                     system_call_3(syscall(mprotect), (positive)to_pages, 4096, 0) == 0 &&
                     system_call_3(syscall(mprotect), (positive)(to_pages + 4 * 4096), 4096, 0) == 0;

        same("memory_copy_rect_out_u32", "a page that faults on both sides of each buffer", guarded, 1);
        if (!guarded)
                return;
        p32 address_to from_pool = (p32 address_to)(from_pages + 4096), address_to to_pool = (p32 address_to)(to_pages + 4096);
        p32 address_to from_end = from_pool + 3 * 4096 / 4, address_to to_end = to_pool + 3 * 4096 / 4;
#if X64
        p8 avx2_on = cpu_has_avx2, avx512_on = cpu_has_avx512;
        positive tiers = 3;
#elif RISCV64
        p8 vector_was = cpu_has_vector;
        positive tiers = 2;
#else
        positive tiers = 1;
#endif
        for (positive tier = 0; tier < tiers; tier++)
        {
#if X64
                cpu_has_avx512 = tier == 0 ? avx512_on : 0;
                cpu_has_avx2 = tier < 2 ? avx2_on : 0;
#elif RISCV64
                // Tier 0: every call the first, which asks for V; tier 1: none.
                cpu_has_vector = tier ? 1 : 0;
#endif
                p8 first = tier == 0, fine = true;

                for (positive w = 0; w <= 600; w++)
                        for (positive height = 0; height <= 3; height += w < 250 || height != 1 ? 1 : 2)
                                for (positive g = 0; g < 3; g += w < 250 ? 1 : 2)
                                        for (positive sa = 0; sa < 16; sa += w < 250 ? 5 : 9)
                                                for (positive da = 0; da < 16; da += w < 250 ? 3 : 1)
                                                {
                                                        positive ds = w + gaps[g][0], ss = w + gaps[g][1];
                                                        positive span_d = height ? (height - 1) * ds + w : 0;
                                                        positive span_s = height ? (height - 1) * ss + w : 0;

                                                        // Ending a few words short of the page after it, and starting a few words past the page before it.
                                                        fine = fine && copy_out_one(to_end - (16 + da + span_d), 16 + da + span_d, from_end - (16 + sa + span_s),
                                                                                    16 + sa + span_s, da, sa, ds, ss, w, height, first);
                                                        fine = fine && copy_out_one(to_pool, da + span_d + 16, from_pool, sa + span_s + 16, da, sa, ds, ss, w,
                                                                                    height, first);
                                                }
                same("memory_copy_rect_out_u32", "every word of the rectangle, nothing else, the source unchanged", fine, 1);

                fine = true;
                for (positive w = 1; w <= 600; w += w < 250 ? 13 : 1)
                        for (positive height = 1; height <= 3; height++)
                                for (b64 shift = -20; shift <= 20; shift += 5)
                                {
                                        positive ds = w + 3, ss = w + 1;
                                        p32 address_to base = from_pool + 64;
                                        p32 address_to ref = to_pool + 64;
                                        p32 address_to dst = base + 40;
                                        p32 address_to src = (p32 address_to)((b64)dst + 4 * shift);

                                        for (positive i = 0; i < 2900; i++)
                                                base[i] = ref[i] = 0x3c000000u + (p32)i * 2654435761u;
                                        VECTOR_FIRST_CALL(first);
                                        memory_copy_rect_out_u32(dst, ds, src, ss, w, height);
                                        for (positive r = 0; r < height; r++)
                                                memory_copy(ref + 40 + r * ds, (p32 address_to)((b64)ref + 4 * (40 + shift + (b64)(r * ss))), w * 4);
                                        for (positive i = 0; i < 2900; i++)
                                                fine = fine && base[i] == ref[i];
                                }
                same("memory_copy_rect_out_u32", "overlapping rows answer as memory_copy one row at a time", fine, 1);
        }
#if X64
        cpu_has_avx2 = avx2_on;
        cpu_has_avx512 = avx512_on;
#elif RISCV64
        cpu_has_vector = vector_was;
#endif
        memory_free(from_pages, 5 * 4096);
        memory_free(to_pages, 5 * 4096);
}

b32 main()
{
#if defined(VERIFY_REPORTS_ONLY)
        check_format();
        check_format_deep();
        check_format_decimals();
#if X64
        cpu_has_avx2 = 0;
        cpu_has_avx512 = 0;
        check_format();
        check_format_deep();
        check_format_decimals();
#endif
#elif defined(VERIFY_NARROW_ONLY)
        // The narrow-argument check alone, for the kernel object's bodies
        // under VERIFY_NARROW_KERNEL: see check_dirty_arguments.
        check_dirty_arguments();
#elif defined(VERIFY_FORMATTERS_ONLY)
        // A small cross-machine lane for the shared numeric core. It avoids
        // making a formatter change wait on unrelated platform tests when a
        // target is available only through a minimal linker and qemu-user.
        check_formatters();
#elif defined(VERIFY_HUGE_ONLY)
        // The huge page rows alone (check_memory_huge): whether memory() and memory_reserve ask, where smaps can say.
        check_memory_huge();
#elif defined(VERIFY_PIXELS_ONLY)
        // The rows of the compositor's, the kit's, the font engine's and the lexer's eleven routines alone (check_pixel_rows):
        // a change to one of them, and the breaks that show the rows fail, without the whole lane.
        check_pixel_rows();
#elif defined(VERIFY_COPY_OUT_ONLY)
        // memory_copy_rect_out_u32's rows alone, for a change to it and the breaks that show them fail.
        check_copy_rect_out();
#elif defined(VERIFY_BINS_ONLY)
        // The bins' and the word hash's rows alone: a few seconds on each machine, for a change to those routines
        // and for showing the rows fail when one of them is broken.
        check_bins();
#else
        check_fill();
        check_fill_wide();
        check_count();
        check_count_bodies();
        check_count_words();
        check_memory_compare();
#if RISCV64
        check_riscv_alignment_floor();
#endif
        check_memory_search();
        check_memory_search_pages();
        check_memory_search_ascii_case();
        check_find_long();
#if X64
        //      Once more down the narrow half. Every case above takes the
        //      wide body on this machine and would take it whatever was
        //      written under the test for the flag, so the flag is turned off
        //      and the whole set runs again.
        //
        //      Put back from a copy rather than by asking the processor
        //      again, so that nothing here has to know what the routine that
        //      asks is called this week.
        {
                p8 had_avx2 = cpu_has_avx2;
                p8 had_avx512 = cpu_has_avx512;

                cpu_has_avx2 = 0;
                cpu_has_avx512 = 0;
                check_memory_compare();
                check_memory_search();
                check_memory_search_ascii_case();
                check_frob();
                check_checksums();
                check_exchange();
                check_copy();
                cpu_has_avx2 = had_avx2;
                cpu_has_avx512 = 0;
                check_frob();
                check_checksums();
                check_exchange();
                cpu_has_avx512 = had_avx512;
        }
#endif
        check_first_of_wide();
        check_hunts_wide();
        check_first_of_blocks();
        check_blocks();
        check_copy();
        check_exchange();
        check_frob();
        check_reverse();
        check_translate();
        check_automaton();
        check_bins();
        check_pixel_rows();
        check_copy_rect_out();
        check_wire();
        check_hand();
        check_wire_arguments();
        check_cells_from_ascii();
        check_unicode_width();
        check_unicode_case();
        check_delete_bytes();
        check_squeeze_bytes();
        check_last_of_bodies();
        check_last_of_either_widths();
        check_nth_widths();
        check_offsets_of_either();
        check_span_byte();
        check_copy_strip_16();
        check_dirty_arguments();
        check_offsets_range();
        check_offsets_range_pages();
        check_offsets_in_set_pages();
        check_record_scans();
        check_record_guards();
        check_checksums();
        check_limbs();
        check_copy_match();
        check_move();
        check_copy_fast_end();
        check_copy_end();
        check_strings();
        check_shell_set_cursor();
        check_string_edges();
        check_compare_edges();
        check_compare_wide();
        check_compare_blocks();
        check_environment();
        check_bulk_strings();
        check_paths();
        check_bulk_numbers();
        check_numbers_exhaustive();
        check_numbers_edges();
        check_parsers_malformed();
        check_file_load();
        check_file_round_trip();
        check_lazy_file_and_library();
        check_memory();
        check_memory_huge();
        check_directory();
        check_clock();
        check_format();
        check_format_deep();
        check_decimals();
        check_decimals_deep();
        check_format_decimals();
#if X64
        //      And the format cases once more down the narrow half.
        //
        //      string_format hunts the end of a run of plain text
        //      thirty two bytes at a time where cpu_detect found AVX2
        //      and eight otherwise, and a machine has one of those, not
        //      both. Without this pass half the routine is the half a
        //      one byte error could live in forever.
        //
        //      Put back from a copy, as the pass at the top of this
        //      function is.
        {
                p8 had_avx2 = cpu_has_avx2;
                p8 had_avx512 = cpu_has_avx512;

                if (!had_avx2)
                        string_format(log, "  NOTE string_format: no AVX2 "
                                           "here, so the wide hunt was not "
                                           "the one tested\n");

                cpu_has_avx2 = 0;
                cpu_has_avx512 = 0;
                check_format();
                check_format_deep();
                check_format_decimals();
                cpu_has_avx2 = had_avx2;
                cpu_has_avx512 = had_avx512;
        }
#endif
        check_byte_helpers();
        check_span();
        check_table_find();
        check_table_find_page_edge();
        check_hostile_neighbours();
        check_span_max();
        check_span_max_wide();
        check_wide_guards();
        check_digits();
        check_signed_digits_and_width();
        check_digits_base();
        check_digits_exact();
        check_digits_long_runs();
        check_formatters();
        check_copy_max_end();
        check_bulk_alignments();
        check_bulk_moves();
        check_copy_returns();
        check_libc_names();
        check_library_names();
        check_fill_widths();
        check_null_zero();
        check_huge_bounds();
        check_huge_sizes();
        check_bulk_wide_strings();
#endif

        if (absent_count)
                string_format(log, "  %p routine(s) under neither name here\n",
                              absent_count);

        return test_report(null);
}
#endif /* CHECK_verify */

