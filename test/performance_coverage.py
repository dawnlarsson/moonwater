#!/usr/bin/env python3
"""Audit performance-evidence coverage for lib.c's assembly inventory.

This is a coverage manifest, not a result database. ``direct_benchmark`` means
that a shipped harness times the named routine as the subject of a row. It does
not mean that a recent native result is stored in the repository, and a qemu
run is never treated as hardware timing. ``benchmark_context`` means a private
core runs inside such a row but is not isolated. ``static_leaf`` records a small
syscall/ABI/counter/byte-order leaf whose source shape can be reviewed; it is
explicitly neither a measurement nor a proof of optimality. The remaining two
classes carry correctness evidence only, or no focused evidence at all.

The check is intentionally coupled to the generated assembly inventory. A new,
removed, or renamed routine makes this fail until somebody classifies it and
names an evidence anchor that actually exists.

    python3 test/performance_coverage.py
    python3 test/performance_coverage.py --gaps
    python3 test/performance_coverage.py --all
"""

import pathlib
import sys
from collections import Counter, namedtuple

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / 'assembly'))
import manifest

ROOT = manifest.ROOT

Coverage = namedtuple('Coverage', 'routine category evidence anchor note')
ROWS = []

CATEGORY_DESCRIPTION = {
    'direct_benchmark': 'direct timing/floor harness',
    'benchmark_context': 'timed only inside a benchmarked wrapper',
    'static_leaf': 'static syscall/ABI/byte-order review; no timing claim',
    'correctness_only': 'correctness evidence only; no timing claim',
    'unmeasured': 'no focused correctness or performance evidence located',
}


def cover(category, evidence, names, note, anchors=None):
    """Add a compact evidence group to the manifest below."""
    anchors = anchors or {}
    for routine in names.split():
        ROWS.append(Coverage(routine, category, evidence,
                             anchors.get(routine, routine), note))


# Direct subjects: these names are passed to a timed runner or appear in a
# dedicated floor row, rather than merely helping the harness print or count.
cover('direct_benchmark', 'test/checks.c#BENCH_floor', '''
memory_compare memory_copy memory_copy_apart memory_count memory_fill
memory_first_of string_compare string_first_of
string_last_of_or_end string_length
''', 'floor-relative rows over multiple sizes')

cover('direct_benchmark', 'test/checks.c#BENCH_numbers', '''
positive_into positive_to_string
''', 'paired former-C/assembly timing over numeric distributions')

cover('direct_benchmark', 'test/checks.c#BENCH_bases', '''
positive_into_base
''', 'paired scalar/assembly timing across bases and value widths')

cover('direct_benchmark', 'test/checks.c#BENCH_hex', 'memory_into_hex',
      'paired former-C/assembly timing over byte tails, dump rows and large spans')

cover('direct_benchmark', 'test/checks.c#BENCH_fixed', 'memory_decimal_series',
      'bounded decimal-record expansion against a C carry loop; native timing required')

cover('benchmark_context', 'test/checks.c#BENCH_escape', '''
memory_escape_index memory_into_escaped
''', 'shared primitives timed through sparse/dense hex and JSON writer workloads',
      anchors={'memory_escape_index': 'writer_hex_escaped',
               'memory_into_escaped': 'writer_hex_escaped'})

cover('direct_benchmark', 'test/checks.c#BENCH_spelled', 'memory_into_spelled',
      'paired former-C/assembly timing at name, line and read sizes over text, sparse and binary bytes')

cover('direct_benchmark', 'test/checks.c#BENCH_codec', '''
memory_encode_power2 memory_decode_power2
''', 'bounded codec quanta against independent scalar bit loops; native timing required')

cover('correctness_only', 'test/checks.c#CHECK_codec', 'memory_into_hex_case',
      'case-selectable entry shares the existing hexadecimal assembly core')

cover('direct_benchmark', 'test/checks.c#BENCH_padded', '''
positive_to_padded
''', 'paired former-C/assembly timing across field shapes')

cover('direct_benchmark', 'test/checks.c#BENCH_input_bases', '''
string_digits_base_max string_digits_hexadecimal_escape_max
string_digits_hexadecimal_max string_digits_octal_escape_max
string_digits_octal_max
''', 'paired scalar/assembly timing with bounded parser inputs')

cover('direct_benchmark', 'test/checks.c#BENCH_reserve', 'memory_reserve',
      'fresh-process mapping growth timing and peak resident memory, with '
      'dense and sparse inputs; hardware/RSS claims require native execution')

# The text family: exported for programs that link this library, and called
# by nothing in the tree, so there is no workload here whose speed they could
# change and no timing row is claimed for them.
cover('correctness_only', 'test/checks.c#CHECK_declare', '''
string_append_bounded string_copy_bounded string_duplicate string_duplicate_max
string_search_folded string_split_next string_token string_token_next
''', 'the standard names, compiled twice from two files sharing nothing and '
     'diffed against the host headers; no caller in this tree, so no timing')

cover('correctness_only', 'test/checks.c#CHECK_strings', '''
jump_mark jump_to_mark
''', 'the callee-saved register lists, held by a longjmp across three frames '
     'that scrambled every saved register, on three machines; a jump has no '
     'C twin to time against')

cover('correctness_only', 'test/checks.c#CHECK_signal', '''
signal_jump_mark signal_jump_to_mark
''', 'the mask travels or stays across a jump, from a handler and back, on '
     'three machines; timed before and after in the commit that made them asm')

cover('correctness_only', 'test/checks.c#CHECK_canvas_cells', '''
canvas_cell canvas_cell2 canvas_cells canvas_glyph canvas_glyph2
canvas_rect_fill canvas_row_blit
canvas_cell_wide canvas_cell2_wide canvas_cells_wide canvas_row_blit_wide
canvas_rect_fill_wide canvas_glyph_wide
''', 'the kernel-only Canvas pixel loops, lifted out of lib.c and linked '
     'into the canvas cells check, which compares what the compositor draws '
     'with them against a per-pixel reference; canvas_cell and canvas_cell2 '
     'are also driven directly over 36 colour pairs and 256 bit patterns, '
     'and canvas_cells against canvas_cell once a cell over 4,096 runs. '
     'The _wide bodies are held to their narrow ones on the same inputs and '
     'drawn through the dispatch on every eighth compose row. '
     'No timing row here: they are timed by whole composes in the commit '
     'that made them')

cover('correctness_only', 'test/differential.py', 'hash_half_md4_wide',
      'ext4 directory hashing, sixteen names to a call, held bit for bit to '
      "Linux's own half_md4 -- cut out of the pinned kernel source when the "
      'harness runs, since that file is GPL-2.0 and is not kept here -- on '
      'x86_64 natively and on arm64 and riscv64 under qemu-user, with a copy '
      'that has one constant changed required to disagree. Timed where the '
      'kernel calls it, not by a row here: 96 to 19 ticks a name on Zen 5 at '
      'twelve characters (33.8 million names checked), 22.6 to 7.6 ns on an '
      'Apple M-series, and getdents over a thousand names 97.2 to 57.8 us in '
      'a KVM guest; riscv64 is untimed')

cover('correctness_only', 'test/checks.c#CHECK_number', 'string_to_decimal_short',
      'the short-decimal reader is held to the general path by name on '
      '131,072 generated inputs a machine in CHECK_number, and reached '
      'through strtod by the numbers lane\'s glibc differential '
      '(CHECK_number_differential: 1,048,576 cases natively and 131,072 under '
      'each emulator, 50,331,648 in the sweep that widened it); its speed was '
      'measured per call and on an awk workload rather than in a dedicated '
      'harness, so no isolated timing row is claimed here')

cover('direct_benchmark', 'test/checks.c#BENCH_lock', 'lock_take lock_release lock_try',
      'take/release pairs alone (the threads_live elision) and with a parked '
      'thread alive (the atomic path), against two stores and the inline C '
      'compare-and-swap pair they replaced; lock_try shares the take body. '
      'CHECK_lock proves exclusion and sleeping across eight threads on three '
      'machines, and the BENCH_hardware_floor section has the same two rows',
      anchors={'lock_try': 'lock_take'})

cover('correctness_only', 'test/checks.c#CHECK_lock', '''
thread_start thread_join thread_exit thread_wait thread_wake
''', 'start/join storms with the mapping count before and after, per-thread '
     'blocks and errno, blocked signal masks, fork and exit_group behaviour, '
     'futex gates; a thread start is a clone and an mmap, measured in '
     'microseconds, not a floor claim',
      anchors={'thread_exit': 'thread_start'})

cover('direct_benchmark', 'test/checks.c#BENCH_allocator', 'memory_take memory_give',
      'malloc/free pair timing against an empty ABI control and a free-list '
      'traffic floor that pops and pushes the same words; the shelf-hit path '
      'is what is timed, and refill/mapping stay in the C it jumps to')

cover('direct_benchmark', 'test/checks.c#BENCH_fields', '''
positive_into_padded positive_into_pair
''', 'paired former-C/assembly timing across padded converter shapes')

cover('direct_benchmark', 'test/checks.c#BENCH_human', '''
positive_into_human_1024_string positive_to_human_1024
''', 'paired former-C/assembly timing for buffer and writer forms')

cover('direct_benchmark', 'test/checks.c#BENCH_human_nearest', '''
positive_into_human_nearest_string
''', 'paired former-C/assembly timing for decimal and binary forms')

cover('direct_benchmark', 'test/checks.c#BENCH_startup', '''
moonwater_cpu_detect program_environment_list program_initial_identity
''', 'isolated runtime-entry components, including the Spark loader identity '
     'handoff and the stock-kernel fallback')

cover('direct_benchmark', 'test/checks.c#BENCH_paths', '''
path_head_copy path_join path_tail_copy
''', 'paired former-C/assembly timing over short, nested, and long paths')

cover('direct_benchmark', 'test/checks.c#BENCH_reverse', '''
memory_reverse
''', 'paired former-C/assembly timing over primitive and folded rev shapes')

cover('direct_benchmark', 'test/checks.c#BENCH_writer_field', '''
string_to_field writer_field
''', 'paired former-C/assembly timing over exact and padded fields')
cover('direct_benchmark', 'test/checks.c#BENCH_writer_text', '''
buffered_flush buffered_reserve buffered_write buffered_write_byte buffered_write_deferred_equal log
''', 'paired former-C/assembly timing over buffered and direct output shapes')
cover('direct_benchmark', 'test/checks.c#BENCH_prefix_known', 'memory_common_prefix',
      'literal-size expansion against the out-of-line hardware routine on '
      'equal spans: native x86-64 plus ARM64/RV64 instruction-set runners')
cover('direct_benchmark', 'test/checks.c#BENCH_compare_max', 'string_compare_max',
      'dynamic-bound first-mismatch semantic floor and equal/late traffic proxies')
cover('direct_benchmark', 'test/checks.c#BENCH_string_copy', 'string_copy',
      'caller-shaped sizes against copy-only traffic and exact scalar semantic proxies')
cover('direct_benchmark', 'test/checks.c#BENCH_hash_33',
      'memory_hash_33 string_hash_33_length',
      'bounded/string verifier and paired scalar/four-byte or one-pass/two-pass timing')
cover('direct_benchmark', 'test/checks.c#BENCH_span_byte',
      'memory_span_byte memory_span_byte_reverse',
      'page-edge verifier and paired scalar/vector equal-run timing in both directions')
cover('direct_benchmark', 'test/checks.c#BENCH_cells_ascii', 'cells_from_ascii',
      'guard and page-edge verifier, and term.c\'s C cell loop against the '
      'assembly in cycles and instructions per cell over fresh and guarded runs')
cover('correctness_only', 'test/checks.c#CHECK_verify', 'cells_from_ascii_wide',
      'the kernel console\'s vector entry, held to the same reference as '
      'cells_from_ascii over every stop and guard on every body it has; timed '
      'against the word body by run length and by the console drain in a guest '
      'in the commit that made it')
cover('direct_benchmark', 'test/checks.c#BENCH_fill_u32', 'memory_fill_u32',
      '32-bit span fill against scalar and bulk-store traffic floors')
cover('direct_benchmark', 'test/checks.c#BENCH_fill_u64', 'memory_fill_u64_aligned',
      'aligned 64-bit pattern fill against scalar and bulk-store traffic floors')
cover('direct_benchmark', 'test/checks.c#BENCH_ascii_case', 'memory_compare_ascii_case',
      'exhaustive byte-pair validation and paired folded comparison timing')
cover('direct_benchmark', 'test/checks.c#BENCH_ascii_convert', '''
byte_to_lower byte_to_upper memory_to_lower_ascii memory_to_upper_ascii
''', 'exhaustive byte/page-edge validation and paired scalar-call/inlined-loop timing')
cover('direct_benchmark', 'test/checks.c#BENCH_ascii_search', 'memory_search_ascii_case',
      'bounded exhaustive verifier plus paired former-C/assembly fixed-search timing')
cover('direct_benchmark', 'test/checks.c#BENCH_grep_search', '''
memory_search_prepared memory_search_ascii_case_prepared
''', 'paired repeated-search timing over sparse, folded, false-candidate and dense matches')
cover('direct_benchmark', 'test/checks.c#BENCH_grep_count',
      'memory_count_records_with_prepared',
      'bounded record verifier and traffic/repeated/fused resident-input timing')
cover('direct_benchmark', 'test/checks.c#BENCH_last_of', 'memory_last_of',
      'guarded reverse-search validation and paired scalar/assembly timing')
cover('direct_benchmark', 'test/checks.c#BENCH_words', 'memory_count_words',
      'state/split validation and paired word-transition timing')
cover('direct_benchmark', 'test/checks.c#BENCH_translate', 'memory_translate',
      'paired former-C/assembly timing over byte-table translation sizes')
cover('direct_benchmark', 'test/checks.c#BENCH_delete', 'memory_delete_bytes',
      'paired former-C/assembly timing over byte deletion by table at three densities')
cover('direct_benchmark', 'test/checks.c#BENCH_squeeze', 'memory_squeeze_bytes',
      'paired former-C/assembly timing over squeezing repeats by table at three set sizes')
cover('direct_benchmark', 'test/checks.c#BENCH_offsets', 'memory_offsets_of_either',
      'paired byte-loop/assembly timing over delimiter and line-end offsets of comma rows')
cover('direct_benchmark', 'test/checks.c#BENCH_offsets', 'memory_offsets_outside',
      'byte-loop/assembly timing over the offsets of every byte outside printable ASCII')
cover('direct_benchmark', 'test/checks.c#BENCH_offsets', 'memory_offsets_in_set',
      'byte-loop/assembly timing over the offsets of the bytes a table marks')
cover('direct_benchmark', 'test/checks.c#BENCH_record_scans', 'memory_offsets_fields_blank',
      "cut -w's two string_span_max a field against one pass a line, over lines of text.txt's shape")
cover('benchmark_context', 'test/checks.c#BENCH_record_scans', 'memory_offsets_fields',
      'the table entry of the same body, which the blank entry times',
      anchors={'memory_offsets_fields': 'memory_offsets_fields_blank'})
cover('direct_benchmark', 'test/checks.c#BENCH_record_scans', 'memory_nth_of',
      "split -l's memory_first_of a line against one call a piece, at 1, 10 and 1000 lines")
cover('direct_benchmark', 'test/checks.c#BENCH_record_scans', 'memory_nth_last_of',
      "tail's memory_last_of a line against one call, at 1, 10 and 1000 lines")
cover('direct_benchmark', 'test/checks.c#BENCH_record_scans', 'memory_last_of_either',
      "fold -s's two memory_last_of over a window against one call, at widths 20, 80 and 200")
cover('benchmark_context', 'test/checks.c#BENCH_offsets', 'memory_offsets_between',
      'the inside half of the same body, entered with the complement off',
      anchors={'memory_offsets_between': 'memory_offsets_outside'})


# Private cores are present in the timed call graph, but the harness cannot
# assign their cost independently from their public wrappers.
cover('benchmark_context', 'test/checks.c#BENCH_ascii_search',
      'memory_first_of_ascii_case',
      'bounded hunt reached by the directly timed folded search',
      {'memory_first_of_ascii_case': 'memory_search_ascii_case'})
cover('benchmark_context', 'test/checks.c#BENCH_grep_search', '''
memory_search_prepared_core memory_search_ascii_case_prepared_core
''', 'private cores reached by the directly timed prepared searches',
      {'memory_search_prepared_core': 'memory_search_prepared',
       'memory_search_ascii_case_prepared_core': 'memory_search_ascii_case_prepared'})
cover('benchmark_context', 'test/checks.c#BENCH_record_scans',
      'memory_offsets_fields_x64 memory_offsets_fields_arm64 memory_offsets_fields_rv',
      'private body both field entries tail-jump into, timed through them',
      {'memory_offsets_fields_x64': 'memory_offsets_fields_blank',
       'memory_offsets_fields_arm64': 'memory_offsets_fields_blank',
       'memory_offsets_fields_rv': 'memory_offsets_fields_blank'})
cover('benchmark_context', 'test/checks.c#BENCH_offsets', 'memory_offsets_range_x64',
      'private body both range entries tail-jump into, timed through them',
      {'memory_offsets_range_x64': 'memory_offsets_outside'})
cover('benchmark_context', 'test/checks.c#BENCH_paths', 'path_split_core',
      'private core reached by all three directly timed path wrappers',
      {'path_split_core': 'path_head_copy'})
cover('benchmark_context', 'test/checks.c#BENCH_numbers', '''
positive_digits_core positive_into_core
''', 'private conversion core reached by a directly timed public converter',
      {'positive_digits_core': 'positive_to_string',
       'positive_into_core': 'positive_into'})
cover('benchmark_context', 'test/checks.c#BENCH_writer_field', 'writer_field_core',
      'private core reached by both directly timed field wrappers',
      {'writer_field_core': 'writer_field'})
cover('benchmark_context', 'test/checks.c#BENCH_writer_text', 'buffered_write_core',
      'private core reached by both directly timed buffer-policy wrappers',
      {'buffered_write_core': 'buffered_write'})
cover('direct_benchmark', 'test/checks.c#BENCH_utf8_valid', 'memory_ascii_span',
      'paired against the word loop and string_span_max that rev and wc asked for the ASCII run')
cover('direct_benchmark', 'test/checks.c#BENCH_utf8_valid', 'memory_utf8_valid_span',
      'paired against grep_text_valid\'s former C over ASCII and mixed lines '
      'of grep\'s sizes, with and without the AVX2 body')
cover('benchmark_context', 'test/checks.c#BENCH_utf8_valid', 'cpu_vector_detect',
      'the RISC-V V probe, asked once by the first long span; no timing claim',
      {'cpu_vector_detect': 'memory_utf8_span'})
cover('benchmark_context', 'test/checks.c#BENCH_utf8_valid', 'memory_utf8_span_wide',
      'the x86_64 AVX2 block body of memory_utf8_span, timed in that routine\'s '
      'unbounded-count row against the walk it had alone',
      {'memory_utf8_span_wide': 'memory_utf8_span'})
cover('benchmark_context', 'test/checks.c#BENCH_span_byte', 'memory_span_byte_wide',
      'the out-of-line SSE and AVX bulk of the directly timed span, which the '
      'same row reaches for every run past sixteen bytes',
      {'memory_span_byte_wide': 'memory_span_byte'})


# These are deliberately a static classification. Loading a syscall number and
# trapping, moving ABI arguments, reading the architectural counter, or doing a
# byte swap has no direct timing row here. Do not turn this category into a
# "hardware floor" assertion without measured evidence.
cover('static_leaf', 'src/lib.c', '_start exit sleep',
      'startup or direct Linux syscall ABI body; statically reviewed only')
cover('static_leaf', 'src/lib.c', '''
byte_is_alnum byte_is_alpha byte_is_digit byte_is_hexadecimal
byte_is_lower byte_is_space byte_is_upper absolute_whole absolute_wide
absolute square_root bits_counted bits_first_set bits_first_set_wide
bits_leading_zeros bits_trailing_zeros byte_is_ascii byte_is_blank
byte_is_control byte_is_graphic byte_is_printable byte_is_punctuation
byte_to_ascii decimal_ceiling decimal_difference decimal_floor
decimal_larger decimal_multiply_add decimal_nearest decimal_rounded
decimal_smaller decimal_truncated decimal_with_sign
memory_copy_source_first memory_copy_until memory_zero narrow_absolute
narrow_ceiling narrow_floor narrow_larger narrow_rounded narrow_smaller
narrow_square_root narrow_truncated narrow_with_sign string_append_max
string_compare_folded string_compare_folded_max string_copy_end
string_copy_max_endptr string_first_of_set string_span_of_set
string_span_without_set string_to_number string_to_number_core
string_to_number_unsigned string_to_whole string_to_whole_wide
''', 'branchless range test, register bitmap, sign fold, or the instruction '
     'the hardware already has; each measured against what gcc emits from '
     'the obvious C, on each architecture')

cover('static_leaf', 'src/lib.c', '''
bytes_reverse_16 bytes_reverse_32 network_load_16 network_load_32
network_store_16 network_store_32 socket_accept socket_bind socket_close
socket_connect socket_listen socket_name socket_new socket_option_get
socket_option_set socket_receive socket_send socket_shutdown
''', 'straight-line byte-order or socket syscall ABI body; statically reviewed only')
cover('static_leaf', 'src/lib.c', '''
get_cpu_time signal_return_trampoline system_call system_call_1 system_call_2 system_call_3
system_call_4 system_call_5 system_call_6
memory_sum_bytes memory_checksum_bsd16
''', 'counter read, syscall ABI, or checksum leaf loop; statically reviewed '
     'instruction by instruction on all architecture floors')


# Direct correctness references in the exhaustive assembly verifier. None of
# these references supplies isolated timing evidence.
cover('correctness_only', 'test/checks.c#CHECK_verify', '''
bipolar_into bipolar_into_string bipolar_to_string byte_class_holds
byte_class_index decimal_to_string fast_sin file_close file_get_status
file_load file_new file_read file_valid file_write memory memory_copy_end
memory_copy_apart_end memory_exchange_apart memory_frob memory_free memory_search
path_basename positive_digits
memory_fill_32 memory_fill_64
positive_into_string positive_to_base_field program_argument_list
program_arguments_own
program_arguments_use program_environment
string_append string_bipolar string_copy_max
string_copy_max_end string_cut string_digits string_digits_exact
string_digits_max string_find string_first_of_max string_first_of_or_end
string_format string_report string_diagnostic writer_stderr writer_stderr_once
string_get_environment string_last_of string_length_max
string_replace_all string_search string_set_add string_span
string_span_max string_table_find string_to_bipolar string_to_positive
wait_status_code_base working_directory_get working_directory_set writer_fill
''', 'direct correctness coverage in the assembly verifier')

cover('correctness_only', 'test/checks.c#CHECK_needle', 'memory_search_prepare',
      'prepared-anchor ABI and empty, one-byte, long, exact and folded needles on all architectures')

cover('correctness_only', 'test/checks.c#CHECK_utf8', 'memory_utf8_span',
      'bounded scalar reference, invalid sequences, alignment and guard pages '
      'on all architecture floors; no isolated timing claim')

cover('correctness_only', 'test/checks.c#CHECK_number', '''
string_to_number_checked string_to_number_unsigned_checked
''', 'single-scan overflow status, end pointers, and folded/dynamic parity; '
     'no shipped isolated timing harness')

cover('correctness_only', 'test/checks.c#CHECK_verify', '''
file_new_lazy library_close library_get library_open shell_set_cursor
''', 'focused ABI, error-path and writer-output coverage on all three architectures')

# Indirect correctness anchors and focused subsystem tests. The anchor is named
# separately whenever the private/helper routine itself is not in the test.
cover('correctness_only', 'test/checks.c#CHECK_verify', 'bipolar_into_core file_unload',
      'correctness exercised through a public wrapper, not timed',
      {'bipolar_into_core': 'bipolar_into', 'file_unload': 'file_close'})
cover('correctness_only', 'test/checks.c#CHECK_slurp', 'file_slurp',
      'focused file-slurp correctness and error-path test')
cover('correctness_only', 'test/checks.c#CHECK_socket', 'host_into string_to_host',
      'focused socket conversion correctness test')
cover('correctness_only', 'test/checks.c#CHECK_writer_buffer', '''
log_direct log_failed log_failure_reset log_flush
''', 'focused deferred, flush, direct, sticky and reset failure checks')
cover('correctness_only', 'test/checks.c#CHECK_probe', '''
log_error program_argument program_argument_count term_size
''', 'focused runtime/probe behavior checks; no isolated timing')
cover('correctness_only', 'test/checks.c#CHECK_wait_retry', '''
system_read_retry system_wait4_retry system_write_all wait_status_code
''', 'focused retry/status correctness and signal-interruption checks')
cover('correctness_only', 'test/checks.c#CHECK_stream', 'system_write_all_checked',
      'checked aggregate ABI, partial writes and errno on all three floors; '
      'no shipped isolated timing harness')
cover('correctness_only', 'test/checks.c#CHECK_verify', '''
hash_xxh64 hash_xxh64_begin hash_xxh64_add hash_xxh64_finish
memory_copy_match memory_get64
''', 'XXH64, LZ match copies, and unaligned little-endian 64-bit loads')

cover('direct_benchmark', 'test/checks.c#BENCH_compression_floor', '''
hash_crc32 hash_crc64 huffman_encode_back lzma_range_encode
''', 'bounded native CRC, backwards Huffman and range-tree timing, with scalar '
      'CRC/range references; copy traffic is a proxy, not an entropy floor')
cover('direct_benchmark', 'test/checks.c#BENCH_cksum_crc', 'hash_crc32_msb',
      "cksum's CRC over 4 KiB and 128 KiB blocks at every feature tier, "
      'against the former cksum.c C copied into the section')
cover('direct_benchmark', 'test/checks.c#BENCH_cksum_crc', 'hash_crc32c',
      "ext4's CRC-32C calls (group bitmaps, numbers and descriptors; inode "
      'seeds and bodies) at every feature tier, against the loop storage.c '
      'carried, copied into the section')
cover('direct_benchmark', 'test/checks.c#BENCH_cksum_digests', 'keccak_blocks sm3_blocks',
      "cksum's SHA-3 (136 and 72 byte blocks) and SM3 cores at every feature "
      'tier, a read block and a single block a call, against the C '
      'src/sh/checksum.c carried, copied into the section')
cover('direct_benchmark', 'test/checks.c#BENCH_unicode_width', 'unicode_width',
      'both variants over mixed, Latin, box-drawing, wide, emoji and mark '
      'streams against the C term.c and text.c ran '
      '(SHARED_unicode_width_reference); every code point in CHECK_verify')
cover('correctness_only', 'test/checks.c#CHECK_verify', 'unicode_case',
      'a table lookup that is three loads a code point, timed by no row: every '
      'code point in all three modes against glibc 2.44 runs on the three '
      'machines, and dirty-register agreement, both in CHECK_verify')
cover('benchmark_context', 'test/checks.c#BENCH_compression_floor', 'lzma_range_shift',
      'carry flushing inside the measured range-tree work; not isolated')
cover('correctness_only', 'test/checks.c#CHECK_compression_floor', 'lzma_decode_span',
      'packet-state differential and guarded input/output; xz benchmarks '
      'include the span, but no isolated native span timing row exists',
      anchors={'lzma_decode_span': 'floor_lzma_span'})
cover('correctness_only', 'test/checks.c#CHECK_compression_floor', 'lzma_range_decode',
      'scalar-model differential and guarded input; end-to-end xz timing is '
      'available, but there is no isolated decode timing row',
      anchors={'lzma_range_decode': 'floor_range'})
cover('correctness_only', 'test/checks.c#CHECK_compression_floor', 'zstd_huffman_codes',
      "every code against zstd's weight-by-weight order; end-to-end zstd timing",
      anchors={'zstd_huffman_codes': 'floor_zstd_huffman_codes'})
cover('correctness_only', 'test/checks.c#CHECK_compression_floor', 'zstd_fse_cells',
      'cell for cell and the counts after against a cell at a time over '
      'random spreads; end-to-end zstd -d timing',
      anchors={'zstd_fse_cells': 'floor_zstd_fse_cells'})
cover('correctness_only', 'test/checks.c#CHECK_compression_floor', 'huffman_codes',
      'every code against the canonical assignment reversed a bit at a time; '
      'end-to-end gzip timing',
      anchors={'huffman_codes': 'floor_huffman_codes'})
cover('correctness_only', 'test/checks.c#CHECK_compression_floor', 'zstd_huffman_cells',
      'cell for cell against a one-at-a-time fill of complete codes into a '
      'guarded table; end-to-end zstd -d timing',
      anchors={'zstd_huffman_cells': 'floor_zstd_huffman_cells'})
cover('correctness_only', 'test/checks.c#CHECK_compression_floor', 'huffman_lengths',
      'the same lengths as the heap it replaced over 60000 count shapes; '
      'end-to-end gzip and zstd timing, and the native arm64 row against '
      'the C reference in test/codec_floor/reference/huffman_lengths.c',
      anchors={'huffman_lengths': 'floor_huffman_lengths'})
cover('correctness_only', 'test/checks.c#CHECK_compression_floor',
      'deflate_tokens_count deflate_tokens_encode deflate_tokens_encode_bmi2',
      'bit-exact against a bit-at-a-time model from every pending width with '
      'the output guarded, blocks of fewer and of more than 64 pairs (the BMI2 '
      'body takes the second); end-to-end gzip timing, and the native arm64 row '
      'against the C reference in test/codec_floor/reference/deflate_tokens.c',
      anchors={'deflate_tokens_count': 'floor_deflate_tokens',
               'deflate_tokens_encode': 'floor_deflate_tokens',
               'deflate_tokens_encode_bmi2': 'floor_deflate_tokens'})
cover('correctness_only', 'test/checks.c#CHECK_compression_floor', 'deflate_parse_fast',
      'the pairs, literal and pair counts, bucket table, hash and stopping '
      'place of 180 blocks (random bytes, four symbols, copies of earlier '
      'bytes, runs, short periods) against gzip level 1 written out a '
      'position at a time, started from the first byte to past the first '
      'window, with a pair cap that bites and a nice it leaves to the caller; '
      'end-to-end gzip -1 timing on three corpora, and the native arm64 row '
      'against gcc and clang builds of the C it replaces',
      anchors={'deflate_parse_fast': 'floor_deflate_parse'})
cover('correctness_only', 'test/checks.c#CHECK_compression_floor', 'deflate_decode_span',
      'all length/distance combinations with guard pages; end-to-end gzip '
      'timing is available, but there is no isolated token-loop timing row',
      anchors={'deflate_decode_span': 'floor_deflate'})

cover('correctness_only', 'test/checks.c#CHECK_checksum_crc', '''
md5_blocks sha1_blocks sha256_blocks sha512_blocks sha512_blocks_avx2 blake2b_blocks cpu_hash_detect
''', 'differential against the textbook rounds over every run length to 40 '
      'blocks under every feature-byte subset the machine has, protected-page '
      'tails, blake2b counter carries, and streaming known answers; the lazy '
      'detector runs on the first dispatching call; timed against the same '
      'rounds compiled from C in the BENCH_hardware_floor section, which test/run bench '
      'does not dispatch')
cover('direct_benchmark', 'test/checks.c#BENCH_link', 'hash_hmac_sha256_prepared',
      'the gate\'s HMAC alone and inside the gate row: the assembly (x86_64 '
      'with the SHA extensions), the C that stands in for it elsewhere, and '
      'the digest functions both replace; held to the key\'s own HMAC at '
      'every length to 300 with a guard page at the end of the message in '
      'CHECK_checksum_crc')
cover('correctness_only', 'test/checks.c#BENCH_hardware_floor', 'sha256_compress',
      'one 64-byte compression; timed in the hardware-floor harness, not the '
      'test/run bench catalogue')
cover('correctness_only', 'test/checks.c#CHECK_net', '''
p256_multiply p256_square p256_add p256_subtract
p384_multiply p384_square p384_add p384_subtract
''', 'differential against the C Montgomery arithmetic on all three floors, '
      'edges and aliasing included; timed against the same arithmetic '
      'compiled from C in the BENCH_hardware_floor section, which test/run bench does '
      'not dispatch')
cover('direct_benchmark', 'test/checks.c#BENCH_montgomery', 'montgomery_multiply',
      'multiply and square at 4, 6, 32 and 64 limbs against the C Montgomery '
      'arithmetic crypto.c ran (SHARED_montgomery_reference), and the ECDSA '
      'and RSA verifies over it; differential at every limb count in CHECK_net')
cover('direct_benchmark', 'test/checks.c#BENCH_montgomery', 'x25519',
      'X25519 at its caller, crypto_x25519, against the five-limb C crypto.c '
      'ran (SHARED_x25519_reference, the x25519-c row); held to OpenSSL by '
      'CHECK_crypto_vectors on every body and by crypto_fuzz, which links '
      'the x86_64 bodies themselves')
cover('direct_benchmark', 'test/checks.c#BENCH_limbs', '''
limbs_add limbs_subtract limbs_add_multiply_word limbs_multiply_word limbs_compare
positive_divide_wide
''', 'each row against the C its callers ran (text.c base58, the bit-serial '
      'divide of numfmt and factor) at 4, 8, 16, 31 and 64 limbs, and the '
      'n-by-n basecase product base58 makes of them; differential over every '
      'length to 67 limbs against guard pages in CHECK_verify on all three '
      'machines')
cover('correctness_only', 'test/checks.c#CHECK_net', 'ghash_blocks ghash_key ghash_integer aes128_ctr_blocks',
      'bit-serial differential over every body each machine has (feature '
      'bytes toggled); ghash_blocks timed against the carry-less multiply '
      'compiled from C in the BENCH_hardware_floor section, which test/run bench does '
      'not dispatch; ghash_key runs once a traffic key')

cover('correctness_only', 'test/checks.c#CHECK_zstd', '''
zstd_bits_open zstd_bits_reload zstd_bits_get
''', 'backward bitstream open, reload, and get against marked and empty streams')

cover('correctness_only', 'src/sh/zstd.c', '''
zstd_huffman_stream zstd_huffman_4x zstd_sequences_run
''', 'Huffman and sequence kernels called from the RFC 8878 decoder')

cover('correctness_only', 'test/checks.c#CHECK_native_reserve', '''
memory_growth memory_release
''', 'exact lifted ARM64 growth, overflow, failure and release checks')


cover('unmeasured', None, 'file_initialize strncpy',
      'assembly entries retained by the source inventory; no focused timing claim')


def validate():
    errors = []
    manifest.reconcile(ROWS, errors)

    cache = {}
    benchmark_dispatch = (ROOT / 'test/run').read_text(encoding='utf-8')
    for row in ROWS:
        if row.category not in CATEGORY_DESCRIPTION:
            errors.append('%s: unknown category %s' %
                          (row.routine, row.category))
            continue
        if row.category == 'unmeasured':
            if row.evidence is not None:
                errors.append('%s: unmeasured row has evidence' % row.routine)
            continue
        if not row.evidence:
            errors.append('%s: %s row lacks evidence' %
                          (row.routine, row.category))
            continue
        if not manifest.anchor(row, cache, errors):
            continue
        if row.category in ('direct_benchmark', 'benchmark_context'):
            # The evidence is a section of test/checks.c, and the claim that
            # it is a benchmark is only worth anything if something runs it:
            # the section name has to appear in test/run's bench catalogue.
            section = row.evidence.partition('#')[2]
            if not section or section not in benchmark_dispatch:
                errors.append('%s: benchmark %s is not dispatched by test/run bench' %
                              (row.routine, row.evidence))
    return errors


def print_report(mode):
    counts = Counter(row.category for row in ROWS)
    total = len(ROWS)
    direct = counts['direct_benchmark']
    context = counts['benchmark_context']

    print('performance evidence coverage: %d routines classified' % total)
    for category in CATEGORY_DESCRIPTION:
        print('  %-20s %3d  %s' %
              (category, counts[category], CATEGORY_DESCRIPTION[category]))
    print('  isolated timing coverage %d/%d; performance-unproven %d/%d' %
          (direct, total, total - direct, total))
    print('  isolated timing is only a measurement candidate; floor evidence '
          'also requires a defensible native lower bound and absolute gap')
    print('  benchmark context only %d/%d (not counted as direct)' %
          (context, total))

    if mode == 'summary':
        gaps = [row.routine for row in ROWS if row.category == 'unmeasured']
        if gaps:
            print('  wholly unmeasured manifest entries: ' +
                  ', '.join(sorted(gaps)))
        else:
            print('  wholly unmeasured manifest entries: none; non-direct '
                  'categories remain performance-unproven')
        return

    if mode == 'gaps':
        selected = [row for row in ROWS if row.category != 'direct_benchmark']
    else:
        selected = list(ROWS)
    for row in sorted(selected, key=lambda item: item.routine):
        evidence = row.evidence or '-'
        anchor = '' if row.anchor == row.routine else ' via ' + row.anchor
        print('  %-30s %-18s %s%s -- %s' %
              (row.routine, row.category, evidence, anchor, row.note))


if __name__ == '__main__':
    sys.exit(manifest.run(
        'performance coverage',
        'check all assembly routines for explicit performance evidence',
        'list every routine without a direct benchmark row',
        validate, print_report))
