#!/usr/bin/env python3
"""Audit known-argument specialization coverage for lib.c's inventory.

src/lib.util.c replaces calls whose decisive argument the compiler
already knows with the narrower operation that argument permits. Forty-eight
routines have that today. This file classifies the complete inventory so that
the question is asked once per routine and not once per reader.

A row says which parameter a call site might hand the compiler as a literal,
and what that literal is worth:

``specialized`` already ships a known-argument path. The row must name the
static inline that expands it, that name must be in src/lib.util.c, and
the routine must be exercised by literal in the exact section of test/checks.c -- a specializer
without a written-out-by-literal test is a specializer nothing reaches.

``worth_it`` has a foldable parameter that changes the shape of the work: it
redirects onto a narrower routine that already exists, or it replaces a loop
with a straight line at small literals. A row whose note begins "Measured" has
been timed and carries the numbers; the harness goes in the tree with the
specializer, and the row names it then, which turns the anchor check on. A row
that says "expected" says at what sizes it expects to win, so that a later
measurement can contradict it rather than quietly agree.

``folds_already`` has a foldable parameter and nothing worth removing. Either
the body is no longer than the call that reaches it, or the only foldable
parameter is a value no reachable call site hands over as a literal. Both are
a decision not to build, and both are recorded rather than left implicit.

``placed`` folds no argument at all. It is for a routine small enough that
the call is a measurable part of it, where the expansion puts the routine's own
instructions at the call site instead of calling them. It names an expansion
and a measurement like ``specialized`` does, and no parameter, because there
is none.

``nothing_to_fold`` takes no parameter the compiler could know: handles,
pointers into memory the caller owns, and the arguments of a syscall whose
work happens on the far side of the trap.

The check is coupled to the generated assembly inventory, the same way
test/performance_coverage.py is. A new, removed or renamed routine fails this
until somebody classifies it, and a named parameter that is not in the
routine's declaration fails it too.

    python3 test/specialization_coverage.py
    python3 test/specialization_coverage.py --gaps
    python3 test/specialization_coverage.py --all
"""

import pathlib
import re
import sys
from collections import Counter, namedtuple

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / 'assembly'))
import manifest

ROOT = manifest.ROOT
COMPILER_MEMORY = ROOT / 'src/lib.util.c'

#       Where a declaration may live. lib.c holds most of them; the
#       platform includes hold the rest, and a parameter named in a row has to
#       be findable in one of these.
DECLARATION_SOURCES = (
    'src/lib.c',
)

Coverage = namedtuple('Coverage',
                      'routine category parameter expansion evidence anchor note')
ROWS = []

CATEGORY_DESCRIPTION = {
    'specialized': 'a known-argument path ships today',
    'placed': 'no argument folds; the routine is placed rather than called',
    'worth_it': 'a foldable parameter that changes the shape of the work',
    'folds_already': 'foldable, but the expansion would remove nothing',
    'nothing_to_fold': 'no parameter the compiler could know',
}

#       Only these carry a measured claim, and only these are counted as
#       evidence in the summary.
MEASURED = ('specialized', 'worth_it', 'placed')


def cover(category, parameter, names, note, expansion=None, evidence=None,
          anchors=None):
    """Add a compact group to the manifest below."""
    anchors = anchors or {}
    for routine in names.split():
        ROWS.append(Coverage(routine, category, parameter,
                             expansion.get(routine) if isinstance(expansion, dict)
                             else expansion,
                             evidence, anchors.get(routine, routine), note))


# ----------------------------------------------------------------------
#       specialized
# ----------------------------------------------------------------------
# The first family. Each is a function-like macro naming itself, so a
# folded size expands and anything else is the ordinary call.
cover('specialized', 'size', 'memory_copy', 'memmove, overlap safe, expanded '
      'to loads before stores up to KNOWN_SIZE_MAX',
      expansion='copy_known', evidence='test/checks.c#CHECK_exact')
cover('specialized', 'size', 'memory_copy_apart', 'memcpy, the halves are '
      'apart, expanded up to KNOWN_SIZE_MAX',
      expansion='copy_apart_known', evidence='test/checks.c#CHECK_exact')
cover('specialized', 'size', 'memory_fill', 'memset, expanded up to '
      'KNOWN_SIZE_MAX with a broadcast when the byte is not folded',
      expansion='fill_known', evidence='test/checks.c#CHECK_exact')


# ----------------------------------------------------------------------
#       worth_it -- measured
# ----------------------------------------------------------------------
# The set routines take the members as a string, so the compiler has the whole
# set. gcc folds the build loop over a literal into constants: the bitmap the
# routine assembles at run time is already there, and for a set of three the
# probe is three compares and no memory at all.
cover('specialized', 'accept', 'string_span_of_set',
      'Measured, and the win is tiered by how many members the literal has. '
      'One to three members expand to a compare chain and win everywhere '
      'timed: 25% of the routine on an empty run, 39% at eight bytes, 69% at '
      'two hundred and fifty six. Four or more members all under sixty four '
      'expand to a register mask with a range guard, which wins only while '
      'the run is short -- 25% empty, 73% at eight, level by sixteen and 136% '
      'at sixty four. The whole 256 bit bitmap, which gcc does fold from a '
      'literal, is NOT a win at any run length measured (87% empty, 103-114% '
      'after) and is not proposed: it does the same probe the routine does '
      'and only saves the build',
      expansion='set_known_table', evidence='test/checks.c#CHECK_exact_set')
cover('specialized', 'accept', 'string_first_of_set',
      'Measured with string_span_of_set: one to three literal members become '
      'a compare chain, and short larger sets become a register mask',
      expansion='first_of_set_known', evidence='test/checks.c#CHECK_exact_set')
cover('specialized', 'reject', 'string_span_without_set',
      'Measured with the pair above and tiered the same way, with the '
      'terminator put into the stopping set so the end of the source stops '
      'the run too', expansion='span_without_byte_known',
      evidence='test/checks.c#CHECK_exact_set')

# Three siblings of the routines that already have a path. Each is a
# trampoline or a wrapper onto one of them, so the expansion is that
# specializer and one argument, and the cutoff is inherited rather than found.
cover('specialized', 'size', 'memory_zero',
      'Measured. bzero is "mov, xor, jmp memory_fill" and the expansion is '
      'fill_known with the byte already chosen: 19-48% of the routine from 1 to 128 '
      'bytes, tracking the fill it reuses',
      expansion='zero_known', evidence='test/checks.c#CHECK_exact_family')
cover('specialized', 'size', 'memory_copy_apart_end',
      'Measured. A lea, a push, a call to the copy, a pop and one terminator; '
      'expanded, the call goes and the lea folds into the answer: 16-40% of '
      'the routine from 1 to 128 bytes',
      expansion='copy_apart_count_end_known', evidence='test/checks.c#CHECK_verify')
# memcmp's contract here is the magnitude of the byte difference and not its
# sign, which the verify section of test/checks.c checks, so __builtin_memcmp cannot be the
# expansion: a hand written word walk has to keep the subtraction.
cover('specialized', 'size', 'memory_compare',
      'Measured. Word loads, the first differing byte from three masked tests '
      'rather than a count-trailing-zeros riscv has no instruction for, and the '
      'unsigned difference kept. Measured on equal blocks: 18-30% of the '
      'routine to 20 bytes, 60-80% at 32-64, level at 80-96, and 200% at 128 '
      'where the routine reaches its four-block round. So this family needs '
      'its own KNOWN_COMPARE_MAX of 96 and must NOT inherit KNOWN_SIZE_MAX, '
      'which is 128 and would expand into the two-to-one loss',
      expansion='compare_known', evidence='test/checks.c#CHECK_exact_scan')

# ----------------------------------------------------------------------
#       worth_it -- expected, from the shape of the body
# ----------------------------------------------------------------------
cover('specialized', 'size', 'memory_common_prefix',
      'Measured. The bounded word walk returns the first differing byte '
      'directly. Equal-span traffic wins through twenty bytes on native '
      'x86-64 and under the ARM64/RV64 instruction-set runners. Emulated RV64 '
      'is the first reversal: 95% of the routine at twenty and 115% at '
      'twenty-one, so KNOWN_PREFIX_MAX is conservatively twenty rather than '
      'inheriting the wider memcmp cutoff. Mismatch positions are guarded for '
      'correctness but are not claimed as native floor measurements',
      expansion='common_prefix_known',
      evidence='test/checks.c#CHECK_exact_prefix')

cover('specialized', 'size', 'memory_compare_ascii_case',
      'Measured at caller-shaped protocol-token lengths. The straight folded '
      'byte line is 65-92% of the routine through twelve bytes on x86-64 and '
      'ARM64 and 59-83% under the RV64 floor runner. ARM64 turns to 160% at '
      'sixteen and x86-64 loses from twenty four, so the shared cutoff is '
      'twelve, not memory_compare\'s much larger cutoff',
      expansion='compare_ascii_case_known',
      evidence='test/checks.c#CHECK_exact_ascii_case')

cover('specialized', 'needle_size', 'memory_search',
      'Measured. A one byte needle is already redirected inside the routine, '
      'so folding it removes five instructions and a jump: 84-99% of the '
      'routine, a small and uniform win. Two and four byte needles were built '
      'and timed and are NOT a uniform win -- 65% of the routine at a 16 byte '
      'haystack, 158% at 64, 137% at 256, 52% at 4096 -- so only the one byte '
      'redirect is proposed', expansion='search_known',
      evidence='test/checks.c#CHECK_needle')
cover('specialized', 'needle_size', 'memory_search_ascii_case',
      'expected: the same one byte redirect, onto memory_first_of_ascii_case, '
      'from the shape of the body rather than from measurement',
      expansion='search_case_known',
      evidence='test/checks.c#CHECK_needle')
cover('specialized', 'base', 'positive_into_base',
      'base ten is positive_into and base sixteen is a shift and a nibble '
      'table; the general path divides by a register',
      expansion='into_base_known', evidence='test/checks.c#CHECK_exact_base')
cover('specialized', 'base', 'string_to_number string_to_number_unsigned',
      'base ten and sixteen are the two a caller ever writes down, and each '
      'drops the general digit-value path and the base range check',
      expansion='number_known', evidence='test/checks.c#CHECK_exact_base')
cover('specialized', 'base',
      'string_to_number_checked string_to_number_unsigned_checked',
      'the same constant-base scanner also produces overflow status without '
      'a second digit walk; boundary and early-exit parity are checked',
      expansion='number_known', evidence='test/checks.c#CHECK_number')

cover('specialized', 'bound', 'string_length_max',
      'Measured literal bounds become a straight bounded word/byte scan',
      expansion='length_max_known', evidence='test/checks.c#CHECK_bounded')
cover('specialized', 'bound', 'string_compare_max',
      'Measured literal bounds become a straight bounded word/byte compare',
      expansion='compare_max_known', evidence='test/checks.c#CHECK_bounded')
cover('specialized', 'bound', 'string_append_max',
      'Measured literal bounds reuse the bounded length and copy expansion',
      expansion='append_max_known', evidence='test/checks.c#CHECK_bounded')
cover('specialized', 'bound', 'string_copy_max_end',
      'Measured literal bounds copy the short known pair directly',
      expansion='copy_max_end_known', evidence='test/checks.c#CHECK_bounded')
cover('specialized', 'bound', 'string_copy_max_endptr',
      'Measured literal bounds copy the short known pair directly',
      expansion='copy_max_endptr_known', evidence='test/checks.c#CHECK_bounded')

cover('specialized', 'size', 'memory_first_of memory_last_of',
      'Measured short literal spans skip the width dispatch and tail '
      'arithmetic', expansion={'memory_first_of': 'first_of_known',
                               'memory_last_of': 'last_of_known'},
      evidence='test/checks.c#CHECK_exact_scan')
cover('specialized', 'size', 'memory_copy_until',
      'Measured short literal spans expand the stop scan and bounded copy',
      expansion='copy_until_known', evidence='test/checks.c#CHECK_exact_family')
cover('specialized', 'source', 'string_copy_end',
      'A literal source has a folded length; its bytes and terminator use the '
      'known-size copy and the answer is the terminator address',
      expansion='copy_end_known', evidence='test/checks.c#CHECK_exact_family')
cover('specialized', 'size', 'memory_count',
      'Measured for folded 16-, 24-, 31- and 53-byte spans. The unaligned '
      'word reductions take 19%, 26%, 37% and 66% of assembly-call time on '
      'x86-64; the 16-, 24- and 53-byte rows take 36%, 34% and 28% under the '
      'AArch64 runner. RV64 keeps the assembly path because its baseline does '
      'not permit unaligned word loads. The cutoff is the byte before one '
      'complete AArch64 vector block and two x86-64 vector blocks',
      expansion='count_known', evidence='test/checks.c#CHECK_verify')
cover('folds_already', 'size', 'memory_delete_bytes',
      'tr hands it the length of the read it just made; no reachable call site '
      'passes a literal size, and the table is built from the command line')
cover('folds_already', 'size', 'memory_offsets_in_set',
      'cat hands it what is left of a read; no reachable call site passes a '
      'literal size')
cover('folds_already', 'size', 'memory_offsets_between',
      'wc and the layout tools hand it what is left of a read; no reachable '
      'call site passes a literal size')
cover('folds_already', 'size', 'memory_offsets_outside',
      'wc and the layout tools hand it what is left of a read; no reachable '
      'call site passes a literal size')
cover('folds_already', 'size', 'memory_offsets_of_either',
      'cut hands it what is left of a read; no reachable call site passes a '
      'literal size')
cover('folds_already', 'size', 'memory_squeeze_bytes',
      'tr hands it what its read and the delete or translate before it left; '
      'no reachable call site passes a literal size, and the table is built '
      'from the command line')
cover('specialized', 'size', 'memory_translate',
      'Measured at the six-byte temporary-name call site. Straight table '
      'loads with no loop or call take 98% of the assembly-call time on '
      'x86-64, 59% under the AArch64 runner and 67% under the RV64 runner. '
      'The expansion stops at eight bytes, where removing control overhead '
      'still dominates the scalar table loads',
      expansion='translate_known', evidence='test/checks.c#BENCH_translate')
cover('specialized', 'bound', 'string_compare_folded_max',
      'Short literal bounds reuse the measured ASCII-case compare expansion '
      'and add strncasecmp\'s equal-terminator stop. At three bytes the '
      'straight line takes about 80% of the x86-64 and AArch64 routine at '
      'eight bytes. RV64 retains the assembly path: even a '
      'branchless three-byte expansion saves only 3% while adding 228 bytes. '
      'The fixed-width targets stop at the measured eight-byte cutoff',
      expansion='compare_folded_max_known', evidence='test/checks.c#CHECK_standard')
# ----------------------------------------------------------------------
#       folds_already
# ----------------------------------------------------------------------
cover('specialized', 'value', '''
bits_counted bits_first_set bits_first_set_wide bits_leading_zeros
bits_trailing_zeros byte_is_alnum byte_is_alpha byte_is_ascii byte_is_blank
byte_is_control byte_is_digit byte_is_graphic byte_is_hexadecimal byte_is_lower
byte_is_printable byte_is_punctuation byte_is_space byte_is_upper byte_to_ascii
byte_to_lower byte_to_upper
''', 'A literal value expands through the shared KNOWN_SINGLE shape into the '
     'branchless range test or hardware bit instruction',
      expansion='KNOWN_SINGLE', evidence='test/checks.c#CHECK_single')

# Arithmetic leaves. Each body is between one and ten instructions, which is
# what the call sequence that reaches it costs, so expanding one replaces a
# call with the instructions the call was going to run anyway.
cover('folds_already', 'value', '''
absolute absolute_whole absolute_wide bytes_reverse_16 bytes_reverse_32
decimal_ceiling decimal_floor
decimal_nearest decimal_rounded decimal_truncated narrow_absolute
narrow_ceiling narrow_floor narrow_rounded narrow_square_root narrow_truncated
square_root
''', 'a branchless range test, a sign fold or the instruction the hardware '
     'already has: the body is no longer than the call that reaches it')
cover('folds_already', 'first', '''
decimal_difference decimal_larger decimal_smaller decimal_multiply_add
''', 'one or two floating point instructions; same argument as the leaves above')
cover('folds_already', 'value', 'narrow_larger narrow_smaller',
      'a min or a max the hardware does in one instruction')

# The shelf number is a pure function of the request, and malloc(sizeof(T)) is
# the common call, so an expansion could hand the pop a constant and drop the
# leading-zero count and the two shifts behind it. It is recorded here rather
# than built because nothing has timed it: the routine's own gap to its
# free-list floor is 7.3 ticks a pair on a 9950X, and which part of that the
# class arithmetic owns has not been separated from the pop's store-to-load
# chain. Build it against BENCH_allocator, or contradict this row.
cover('folds_already', 'bytes', 'memory_take',
      'the shelf number folds at a literal request, but the saving has not '
      'been separated from the free-list chain that dominates the pair')

# A specializer that folds no argument. It exists because the
# routine is small enough that the call is a measurable part of it, so the
# same instructions are placed at the call site instead.
cover('placed', None, 'string_to_decimal_short',
      'Measured. Nothing folds: the expansion places the routine\'s own '
      'instructions rather than narrowing them. An awk run over two megabytes '
      'of "word:NNN:word" is 29,831,078 core cycles calling it and 29,356,936 '
      'placing it, medians of nine on a quiet 9950X, against 32,920,010 for '
      'the general conversion it fronts. The call was five cycles of a body '
      'of about thirty. A memory clobber costs more than the call it saves -- '
      '30,852,018 with one -- so the read is declared as an incomplete array '
      'instead, which says an unspecified amount is read at that address and '
      'is what GCC documents for a span whose length the constraint cannot '
      'state. Widened to Clinger\'s window -- a point and an exponent, one '
      'exact multiply or divide by the routine\'s own table -- the placement '
      'stayed whole rather than fronting the routine: fronting it cost 283 '
      'instructions a call for "-123456.789012" against 170 placed, because '
      'the front half scans the integer digits a second time before handing '
      'over. It keeps to seven registers and the input\'s: with an eighth '
      'the caller saved one on every call and "042" took 14.4 cycles, and '
      'with seven it takes 10.2 against the integer-only placement\'s 10.0 '
      'in 68 instructions against 75',
      expansion='decimal_short_placed', evidence='test/checks.c#CHECK_number')

# The second placement, and a partial one: the macro answers the common
# nothing-to-trim case from the last byte at the call site and calls the
# routine only when there is a suffix to walk. Nothing folds -- every caller
# strips a length it counted at run time.
cover('placed', None, 'memory_span_byte_reverse',
      'the last-byte test is placed at the call site ahead of the routine; '
      'the parenthesized name reaches the routine itself for the checks',
      expansion='_reverse_byte', evidence='test/checks.c#CHECK_standard')

cover('nothing_to_fold', None, '''
string_append_bounded string_copy_bounded string_duplicate string_duplicate_max
string_search_folded string_split_next string_token string_token_next
''', 'pointers into text the caller owns, and a capacity that no call site in '
     'this tree hands over at all, there being no call site')

cover('nothing_to_fold', None, '''
lock_take lock_release lock_try thread_start thread_join thread_exit
thread_wait thread_wake
''', 'a lock word, a thread handle, a futex word or an entry pointer the call '
     'site owns; the only constant in them is threads_live, which is a run-time '
     'count')

cover('nothing_to_fold', None, 'memory_give',
      'a pointer into memory the caller owns, and a shelf number read back '
      'out of the block rather than handed over by the call site')
cover('nothing_to_fold', None, 'cells_from_ascii cells_from_ascii_wide',
      'pointers into a row and a read the caller owns, a limit and a guard from '
      'the cursor and the line, and an attribute from the terminal state; stop '
      'is a literal at the one call site, but it is only ever tested against a '
      'loaded cell, and knowing it shortens nothing')
cover('folds_already', 'magnitude', 'narrow_with_sign decimal_with_sign',
      'a sign copy: one and or one bit insert')

# Fixed-16 C bodies took 123-306% of the x86-64 floor for checksum, hash,
# reverse, span, first-of and case conversion; sum/frob were level. Word count
# won 6% but has no folded production caller. ARM64/RV64 qemu often improved,
# which cannot overrule a native regression or justify unused unrolled bodies.
cover('folds_already', 'size', '''
memory_first_of_ascii_case memory_span_byte memory_count_words
memory_hash_33 memory_sum_bytes memory_checksum_bsd16
memory_to_lower_ascii memory_to_upper_ascii memory_reverse memory_frob
''', 'fixed short bodies were measured; native x86-64 is level or slower, and '
     'every production caller carries a runtime size')
cover('folds_already', 'size', '''
hash_xxh64 hash_xxh64_add hash_crc32 hash_crc64 zstd_bits_open zstd_huffman_stream zstd_huffman_4x
''', 'production spans are compressed-block or hash lengths; short and long '
     'paths already live in the floor')
cover('folds_already', 'size', 'hash_crc32_msb',
      "cksum's read blocks, build.c's path and the one to eight length bytes "
      'are all counted at run time; no call site hands a literal')
cover('folds_already', 'variant', 'unicode_width',
      'every call site names its variant, but the variant is only the shift '
      'of a nibble the body loads for both; a body per variant would remove '
      'one addition and duplicate the compares')

# No production caller presents these bounds as literals. The proposed forms
# duplicate the same bounded scan/copy work and only remove entry dispatch;
# unlike the shipped compare/count families they have no uniform measured
# crossover that pays for the straight-line body.
cover('folds_already', 'bound', 'string_first_of_max string_span_max',
      'all production bounds are runtime slice lengths; a fixed body would be '
      'unreached and duplicate the floor')
cover('folds_already', 'length', 'string_copy_max',
      'there is no production call, and the copied length remains runtime '
      'even when the padding bound is folded')

cover('folds_already', 'count', 'memory_utf8_span',
      'character count one already skips the ASCII word loop; the byte bound '
      'and input bytes remain runtime, so a separate decoder would duplicate '
      'validation without removing its decisions')

cover('folds_already', 'size', 'memory_utf8_valid_span',
      'every caller hands a line or read length known only at run time; the '
      'bytes decide the path, so a fixed size would remove no decision')

cover('folds_already', 'size', 'memory_into_hex',
      'production dump/checksum spans carry runtime lengths; fixed expansions '
      'would duplicate bounded scalar/vector conversion with no constant caller')

cover('folds_already', 'size', 'memory_into_hex_case',
      'the case is selected once per span; production span lengths remain '
      'runtime and both cases share the existing hexadecimal core')
cover('folds_already', 'groups', 'memory_encode_power2 memory_decode_power2',
      'production group counts and input bytes are runtime; shape selection '
      'happens once per batch, with no per-utility codec expansion')
cover('folds_already', 'size', 'memory_escape_index memory_into_escaped',
      'the bounded bytes remain runtime; shared scalar/vector paths handle '
      'policy selection without adding a second escaping engine at call sites')
cover('folds_already', 'step', 'memory_decimal_series',
      'the production stride, decimal field bounds and record length are '
      'runtime; assembly already selects a short unit-stride record path')

# These shapes make a synthetic constant-call benchmark smaller, but no
# production caller presents the required constant. Keeping their macro and
# helper machinery would therefore add source without changing an image.
cover('folds_already', 'size',
      'memory_copy_end memory_copy_source_first memory_exchange_apart',
      'all production copies/exchanges carry runtime sizes; keep the compact '
      'hardware-floor entry points')
cover('folds_already', 'input', 'string_find',
      'production needles are runtime strings')
cover('folds_already', 'needle', 'string_search',
      'production needles are runtime strings, including the fixed-name '
      'tables whose selection remains runtime')
cover('folds_already', 'base', 'string_digits_base_max',
      'the scanner supplies its parsed runtime base; direct leaf wrappers '
      'would be unused')
cover('folds_already', 'which', 'byte_class_holds',
      'the shell lexer passes a parsed runtime class; direct predicates '
      'already cover callers that know the class')

# The common two-digit zero fields now dispatch inside the assembly floor;
# six- and nine-digit contiguous fields already did. Other literal widths do
# not determine the runtime digit/body length, so a caller expansion merely
# repeats the same field state machine.
cover('folds_already', 'width',
      'positive_into_padded positive_to_padded writer_field string_to_field '
      'positive_to_base_field',
      'common fixed decimal widths dispatch in the floor; otherwise width '
      'does not determine runtime body or padding length')
cover('folds_already', 'count', 'writer_fill',
      'every production count is runtime; unrolling duplicates opaque writer '
      'calls and no loop body work disappears')
cover('folds_already', 'stride', 'string_table_find',
      'smallest fixed production table has six entries and most are much '
      'larger or runtime-sized; unrolling duplicates the tuned comparison '
      'body once per entry')

# Converters whose only foldable parameter is the number being converted. No
# call site in the tree hands one a literal -- they all convert something read
# or counted at run time -- so the expansion would be unreachable code.
cover('folds_already', 'value', '''
positive_into positive_into_string positive_into_pair positive_digits
positive_into_human_1024_string positive_to_human_1024
positive_into_human_nearest_string
''', 'the foldable parameter is the number itself, and every call site in the '
     'tree converts a runtime value; the expansion would never be reached')
cover('folds_already', 'value', 'bipolar_into bipolar_into_string',
      'the signed forms of the same argument')
cover('folds_already', 'number', 'positive_to_string bipolar_to_string',
      'the same argument under the name the writer forms give it')
cover('folds_already', 'host', 'host_into',
      'an address the program was handed, spelled into dotted decimal')
cover('folds_already', 'value', 'decimal_to_string',
      'a runtime measurement in every caller; the writer walk is the work')
cover('folds_already', 'input', 'path_basename',
      'a literal path folds the answer to a constant string, and no caller '
      'has one; the routine exists for a path read at run time')
cover('folds_already', 'format', 'string_format string_report',
      'the format is a literal at nearly every call site and this is the '
      'largest thing here that cannot be built: expanding it means turning '
      'one variadic call into a sequence of per-conversion calls, and C has '
      'no construct that walks a literal and emits calls against __VA_ARGS__')
cover('folds_already', 'sink', 'string_diagnostic',
      'the descriptor selects writer, optional preflush and live prefix; '
      'literal selection still needs the shared formatter and callbacks, '
      'so expansion would duplicate reporting policy at every caller')
cover('folds_already', 'x', 'fast_sin',
      'a polynomial with no branch in it; folding the angle folds the answer, '
      'and no caller has a literal angle')
cover('folds_already', 'raw', 'wait_status_code wait_status_code_base',
      'a shift, a mask and a select on a status the kernel wrote; the base is '
      'foldable and removes one compare')
cover('folds_already', 'at', '''
network_load_16 network_load_32 network_store_16 network_store_32
''', 'a load or a store and a byte reversal; the value is foldable and the '
     'body is two instructions')
cover('folds_already', 'input', '''
string_to_positive string_to_bipolar string_to_whole string_to_whole_wide
string_to_host
''', 'a literal string would fold the whole answer, and no caller parses a '
     'literal; the routines exist for text that arrived at run time')
cover('folds_already', 'source', '''
string_length string_compare string_copy string_append
string_first_of string_first_of_or_end string_last_of string_last_of_or_end
string_compare_folded string_digits
string_digits_exact string_bipolar string_span
''', 'the string is foldable only as a literal, and a literal argument to an '
     'unbounded walk folds the answer rather than shortening the walk; the '
     'bounded forms above are where the bound is the useful literal')
cover('folds_already', 'string', 'string_cut string_replace_all',
      'the byte cut at or replaced is foldable and the walk is not; a literal '
      'string would be written into by these, which no caller does')
cover('folds_already', 'set', 'string_set_add',
      'a literal member string folds this to a fixed bitmap, but the routine '
      'exists so that a set can be built once and reused; a caller with a '
      'literal set wants string_span_of_set above instead')
cover('folds_already', 'name', 'byte_class_index string_get_environment',
      'a literal name folds the length; the body is a length and a small '
      'compare chain either way')
cover('folds_already', 'capacity', 'path_join path_tail_copy path_head_copy',
      'the capacity is a literal at every call site and bounds the writes, '
      'but the work is the scan for the last separator, which the capacity '
      'says nothing about')
cover('folds_already', 'x', 'shell_set_cursor',
      'two numbers into an escape sequence; folding them folds the sequence, '
      'and no caller has a literal cursor position')
cover('folds_already', 'size', 'memory memory_free',
      'the size is a literal often enough, and the work is an mmap or an '
      'munmap: nothing on this side of the trap gets shorter')
cover('folds_already', 'count', '''
memory_fill_u32 memory_fill_u64_aligned memory_fill_32 memory_fill_64
''',
      'the count is a runtime row or block length at every caller; when it is '
      'small the assembly routine already selects its scalar tail, and when '
      'it is large expanding stores would only duplicate its vector loop')
cover('folds_already', 'want', 'memory_growth memory_reserve',
      'growth policy arithmetic, a handful of instructions with an overflow '
      'check that a literal does not remove')
cover('folds_already', 'unit', 'memory_release',
      'a free and three stores; the unit is foldable and removes nothing')
cover('folds_already', 'length', '''
buffered_write buffered_write_deferred_equal log log_direct log_error writer_stderr writer_stderr_once
''', 'a literal length is common and the work is the copy into the buffer, '
     'which memory_copy_apart already expands from inside these')
cover('folds_already', 'length', 'system_write_all system_write_all_checked',
      'one shared syscall-progress loop; a literal length cannot predict short '
      'writes or terminal errno, and zero length already exits before trapping')
cover('folds_already', 'length', 'buffered_reserve',
      'callers commonly know the requested span, but the public hot path is '
      'already only two capacity checks, one count update and the returned '
      'address; the direct floor harness measures that residual')
cover('folds_already', 'capacity', 'buffered_write_byte',
      'a compare against the capacity and a byte store; the capacity is a '
      'literal and the body is already that short')
cover('folds_already', 'code', 'exit',
      'a syscall number and a trap, with the code in a register either way')
cover('folds_already', 'flags', 'file_new file_new_lazy',
      'the flags are a literal at every call site and are moved into a '
      'register for the open either way')
cover('folds_already', 'size', 'file_read file_write',
      'the size is a literal and the work is the syscall')
cover('folds_already', 'capacity', 'file_slurp',
      'a literal capacity bounds the read; the work is the open, the read and '
      'the close')
cover('folds_already', 'index', 'program_argument program_environment',
      'a bounds check and an indexed load; a literal index removes the '
      'multiply and nothing else')
cover('folds_already', 'count', 'program_arguments_use',
      'a count and a pointer stored into two globals')
cover('folds_already', 'syscall', '''
system_call system_call_1 system_call_2 system_call_3 system_call_4
system_call_5 system_call_6
''', 'the syscall number is always a literal and is loaded into one register; '
     'that is the whole body besides the trap')
cover('folds_already', 'handle', 'system_read_retry',
      'a retry loop around a trap; the handle is foldable and the loop is not')
cover('folds_already', 'options', 'system_wait4_retry',
      'the options are a literal at every call site and are moved into a '
      'register for the trap either way')

cover('folds_already', 'bound', '''
string_digits_max string_digits_octal_max string_digits_octal_escape_max
string_digits_hexadecimal_max string_digits_hexadecimal_escape_max
''', 'the bound is a literal, but each of these is already the base-folded '
     'form of string_digits_base_max: the specialization this family has is '
     'the one that shipped as five separate routines')
cover('folds_already', 'needle_size', '''
memory_search_prepared memory_search_ascii_case_prepared
memory_count_records_with_prepared
''', 'prepared searches are reached with compiled run-time patterns and already '
     'redirect empty and one-byte needles inside their assembly entry')
cover('folds_already', 'ascii_case', 'memory_search_prepare',
      'the case choice is made once while preparing a reusable needle; splitting '
      'the cold pass would not shorten any repeated search')
cover('folds_already', 'n', 'zstd_bits_get',
      'FSE extra-bit widths are table-driven at run time')
cover('folds_already', 'length', 'memory_copy_match',
      'match lengths are decoded at run time')
cover('folds_already', 'seed', 'hash_xxh64_begin',
      'the seed is a caller value; the mixer does not shrink when it is 0')


cover('worth_it', 'mode', 'lzma_range_encode lzma_range_decode',
      'Expected: fixed ordinary eight-bit literals can remove generic mode '
      'dispatch and unroll the tree; requires native crossover measurements '
      'before adding a separate body',
      expansion={'lzma_range_encode': 'lzma_range_encode_known',
                 'lzma_range_decode': 'lzma_range_decode_known'})
cover('folds_already', 'n', 'huffman_encode_back',
      'literal stream lengths are runtime block slices; empty input already '
      'takes the exact terminal-byte path')
cover('nothing_to_fold', None, 'zstd_huffman_codes',
      'the weights come from each literal block')
cover('nothing_to_fold', None, 'zstd_fse_cells',
      'the table size and counts come from each block')
cover('nothing_to_fold', None, 'huffman_codes',
      'the lengths are built from each block')
cover('nothing_to_fold', None, 'zstd_huffman_cells',
      'the weights and the depth are read from each block')
cover('folds_already', 'limit', 'huffman_lengths',
      'the limit only matters on the rare path that splits codes; the sort '
      'and joins do not depend on it')
cover('nothing_to_fold', None, 'deflate_tokens_count deflate_tokens_encode',
      'a pointer to a block of tokens and its code tables, all run-time data')
cover('nothing_to_fold', None, 'lzma_range_shift deflate_decode_span lzma_decode_span',
      'a pointer to evolving range or token-loop state; values live in memory '
      'and are not known at the call site')

# ----------------------------------------------------------------------
#       nothing_to_fold
# ----------------------------------------------------------------------
cover('nothing_to_fold', None, '''
_start moonwater_cpu_detect program_initial_identity get_cpu_time signal_return_trampoline term_size working_directory_get
working_directory_set program_argument_count program_argument_list program_arguments_own
program_environment_list log_failed log_failure_reset log_flush sleep buffered_flush
string_hash_33_length hash_xxh64_finish sha256_compress ghash_blocks ghash_key ghash_integer aes128_ctr_blocks
md5_blocks sha1_blocks sha256_blocks sha512_blocks blake2b_blocks cpu_hash_detect
p256_multiply p256_square p256_add p256_subtract p384_multiply p384_square p384_add p384_subtract memory_get64 zstd_bits_reload zstd_sequences_run
''', 'no argument, or an argument that is a pointer into memory the caller '
     'owns; nothing the compiler could know shortens the body')
cover('nothing_to_fold', None, 'jump_mark jump_to_mark',
     'the argument is a jump_state the caller owns and the body is the '
     'register list itself; no literal a caller could pass shortens it')
cover('nothing_to_fold', None, 'signal_jump_mark signal_jump_to_mark',
     'a caller-owned jump_state and a flag or value the stub only tests '
     'once before a system call or a tail jump')
cover('nothing_to_fold', None, 'montgomery_multiply',
     'the modulus, its inverse and the limb count come from a crypto_field or '
     'an RSA key at run time; no call site holds a literal')
cover('nothing_to_fold', None, 'x25519',
     'a secret scalar and a peer\'s point, both run-time bytes; the one call '
     'site holds no literal, and the base point u = 9 is where a fixed-base '
     'table would go, not a fold')
cover('nothing_to_fold', None, '''
file_close file_get_status file_load file_unload file_valid
library_close library_get library_open
''', 'a file or library handle the caller opened at run time')
cover('nothing_to_fold', None, '''
socket_accept socket_bind socket_close socket_connect socket_listen
socket_name socket_new socket_option_get socket_option_set socket_receive
socket_send socket_shutdown
''', 'a descriptor and an address structure; the literals among the flags are '
     'moved into a register for the trap either way')
cover('nothing_to_fold', None, '''
bipolar_into_core buffered_write_core path_split_core positive_digits_core
positive_into_core string_to_number_core writer_field_core
memory_search_prepared_core memory_search_ascii_case_prepared_core
memory_span_byte_wide memory_offsets_range_x64 memory_utf8_span_wide
''', 'a private core with no declaration, so C cannot name it and no call '
     'site can hand it a literal; its wrappers carry the classification')
cover('nothing_to_fold', None, '''
canvas_cell canvas_cell2 canvas_cells canvas_glyph canvas_glyph2
canvas_rect_fill canvas_row_blit
canvas_cell_wide canvas_cell2_wide canvas_cells_wide canvas_row_blit_wide
canvas_rect_fill_wide canvas_glyph_wide
''', 'pointers into a framebuffer, a font and a palette, with the pitch, '
     'sizes and colours of an output and a pane at run time; the literals that '
     'do reach them -- WINDOW_CELL_H rows at both cell call sites, INK_COUNT for '
     'the palette blit -- set a loop count, and these kernel-only bodies moved '
     'here unchanged with no folded form measured')


def load_declarations():
    """Every routine's C declaration, so a named parameter can be checked.

    A declaration here is a statement ending in ');' that is not inside an
    __asm__ string and not a macro line. Routines with no declaration -- the
    private cores and _start -- simply do not appear, which is what makes
    naming a parameter for one of them a failure.
    """
    found = {}
    for relative in DECLARATION_SOURCES:
        path = ROOT / relative
        if not path.is_file():
            continue
        text = path.read_text(encoding='utf-8', errors='replace')
        for match in re.finditer(r'(?<![A-Za-z0-9_])([a-z_][a-z_0-9]*)\s*\(', text):
            name = match.group(1)
            if name in found:
                continue
            begin = text.rfind('\n', 0, match.start()) + 1
            close = text.find(';', match.start())
            if close == -1:
                continue
            statement = text[begin:close + 1]
            if '"' in statement or '#' in statement or 'ASM_' in statement:
                continue
            if statement.lstrip().startswith('//'):
                continue
            if not statement.rstrip().endswith(');'):
                continue
            found[name] = ' '.join(statement.split())
    return found


cover('nothing_to_fold', None, 'file_initialize',
      'initializes stream state obtained at runtime')
cover('worth_it', 'bound', 'strncpy',
      'a constant bound could fold the padding span; no specialization is shipped',
      expansion='strncpy_known')


def validate():
    errors = []
    inventory = manifest.reconcile(ROWS, errors)

    declarations = load_declarations()
    #   The umbrella layer of lib.util.c and nothing else. The floor above
    #   it and the standard families below are in the same file now, and
    #   standard spells exit, sleep and the lock pair as function-like
    #   macros of its own -- names lib.c also has, which would read as
    #   unlabelled specializers here.
    whole = COMPILER_MEMORY.read_text(encoding='utf-8', errors='replace')
    specializers = ''.join(
        part.split('#endif // LIB_SKIP_UMBRELLA')[0]
        for part in whole.split('#ifndef LIB_SKIP_UMBRELLA')[1:])
    cache = {}

    # A public routine spelled as a function-like macro in the compiler
    # umbrella is a shipped call-site specializer. Keep this structural fact
    # tied to the manifest: the old check only proved that a row's helper
    # existed, so thirty five live macros remained labelled as future work
    # without failing the audit.
    macro_specialized = set(re.findall(
        r'^#define\s+([a-z_][a-z_0-9]*)\s*\(', specializers, re.MULTILINE)) & inventory
    manifested_specialized = {
        row.routine for row in ROWS
        if row.category in ('specialized', 'placed')
    }
    if macro_specialized != manifested_specialized:
        if macro_specialized - manifested_specialized:
            errors.append('live specializers not labelled specialized or placed: ' +
                          ', '.join(sorted(macro_specialized - manifested_specialized)))
        if manifested_specialized - macro_specialized:
            errors.append('specialized rows without public macros: ' +
                          ', '.join(sorted(manifested_specialized - macro_specialized)))

    for row in ROWS:
        if row.category not in CATEGORY_DESCRIPTION:
            errors.append('%s: unknown category %s' % (row.routine, row.category))
            continue

        if row.category == 'nothing_to_fold':
            if row.parameter is not None:
                errors.append('%s: nothing_to_fold row names a parameter' %
                              row.routine)
            if row.evidence is not None:
                errors.append('%s: nothing_to_fold row carries evidence' %
                              row.routine)
            continue

        #      A placed row folds nothing by definition: it exists because
        #      the call is worth more than any argument, so it names an
        #      expansion and its measurement and no parameter at all.
        if row.category == 'placed':
            if row.parameter is not None:
                errors.append('%s: placed row names a parameter' % row.routine)
            if not row.expansion:
                errors.append('%s: placed row names no expansion' % row.routine)
            if not row.evidence:
                errors.append('%s: placed row carries no evidence' % row.routine)
            continue

        #      Everything else claims a foldable parameter, and the claim is
        #      checked against the routine's own declaration rather than left
        #      as prose nobody reads.
        if not row.parameter:
            errors.append('%s: %s row names no foldable parameter' %
                          (row.routine, row.category))
        elif row.routine not in declarations:
            errors.append('%s: %s row names parameter %s but the routine has '
                          'no C declaration' %
                          (row.routine, row.category, row.parameter))
        elif not manifest.token_present(declarations[row.routine],
                                        row.parameter):
            errors.append('%s: parameter %s is not in its declaration -- %s' %
                          (row.routine, row.parameter, declarations[row.routine]))

        if row.category == 'folds_already':
            if row.expansion is not None:
                errors.append('%s: folds_already row names an expansion' %
                              row.routine)
            if row.evidence is not None:
                errors.append('%s: folds_already row carries evidence' %
                              row.routine)
            continue

        #      specialized and worth_it both name the expansion they mean.
        if not row.expansion:
            errors.append('%s: %s row names no expansion' %
                          (row.routine, row.category))
        elif row.category == 'specialized' and \
                not manifest.token_present(specializers, row.expansion):
            errors.append('%s: specializer %s is absent from '
                          'src/lib.util.c' % (row.routine, row.expansion))

        if row.category == 'specialized' and not row.evidence:
            errors.append('%s: a shipped specializer needs correctness '
                          'evidence' % row.routine)

        if row.evidence is None:
            continue
        manifest.anchor(row, cache, errors)
    return errors


def print_report(mode):
    counts = Counter(row.category for row in ROWS)
    total = len(ROWS)
    timed = sum(1 for row in ROWS
                if row.category in MEASURED and row.evidence is not None)

    print('specialization coverage: %d routines classified' % total)
    for category in CATEGORY_DESCRIPTION:
        print('  %-18s %3d  %s' %
              (category, counts[category], CATEGORY_DESCRIPTION[category]))
    print('  candidates whose harness is in the tree %d/%d' %
          (timed, counts['specialized'] + counts['worth_it'] +
                  counts['placed']))

    if mode == 'summary':
        pending = sorted(row.routine for row in ROWS
                         if row.category == 'worth_it'
                         and not row.note.startswith('Measured'))
        print('  worth_it and not yet timed: ' + ', '.join(pending))
        return

    if mode == 'gaps':
        selected = [row for row in ROWS if row.category == 'worth_it']
    else:
        selected = list(ROWS)
    for row in sorted(selected, key=lambda item: item.routine):
        parameter = row.parameter or '-'
        evidence = row.evidence or 'not timed'
        print('  %-34s %-16s %-12s %-28s %s' %
              (row.routine, row.category, parameter, evidence, row.note))


if __name__ == '__main__':
    sys.exit(manifest.run(
        'specialization coverage',
        'check every assembly routine for a specialization decision',
        'list every worth_it row', validate, print_report))
