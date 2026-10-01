/*
        Linker script for the spark format.

        The loader maps text, data and bss as three separate regions with
        different permissions, so each one has to start on a page boundary --
        otherwise two regions share a page and cannot be given different
        protections. Aligning here is what keeps the kernel side free of
        partial page fixups.

        SPARK_BASE has to match the base recorded in the spark header. The
        image carries absolute addresses, so it is mapped MAP_FIXED there.
*/

SPARK_BASE = 0x400000;

ENTRY(_start)

SECTIONS
{
        /* The spark header lives in the first 64 bytes of the mapped image,
           so the code starts just past it. The loader maps from SPARK_BASE and
           jumps to the entry recorded in the header, which points here. */
        . = SPARK_BASE + 64;

        /* Hot code first, cold code last, because what a program touches is
           what it pays for: a page is read in whole and stays, so a cold
           routine sitting between two hot ones is read in beside them and
           never used. The compiler already says which is which -- .text.hot
           and .text.startup for the code it expects to run, .text.unlikely
           for the error paths it does not -- and this is where that answer
           is worth anything. .moonwater.span_byte is the same idea written
           by hand, and it keeps its place beside them.

           An input section goes to the first pattern that matches it, so
           .text.unlikely has to be named before the catch-all below or the
           catch-all takes it and the order here means nothing. The hot/cold
           jumps this separates are already cross-section and relocated to
           32 bits, so no distance inside an image can be too far. */
        .text : {
                *(.text.hot .text.hot.*)
                *(.text.startup .text.startup.*)
                *(.text .text.exit .text.exit.*)

                *(.moonwater.span_byte)
                /* The configuration record, which nothing reads at run time
                   and the build looks for in the image: named before
                   .rodata.* so garbage collection cannot take it. */
                KEEP(*(.rodata.moonwater_config))
                *(.rodata .rodata.*)
                /* riscv64 puts read only globals of a few bytes here. */
                *(.srodata .srodata.*)

                *(.text.unlikely .text.unlikely.*)
                /* The routines written in assembly that every command runs,
                   each in a section of its own (ASM_FUNC), first among the
                   rest: what the library's routines call one another with
                   on riscv64 is a jal, which reaches a megabyte, so they
                   stay together instead of going to the front, and a
                   command touches this part of the image as one run of
                   pages rather than one a routine. */
                *(.text.memory_compare .text.memory_compare_ascii_case)
                *(.text.memory_copy .text.memory_copy_apart .text.memory_copy_apart_end)
                *(.text.memory_copy_end .text.memory_count .text.memory_first_of)
                *(.text.memory_growth .text.memory_hash_33 .text.memory_release)
                *(.text.memory_reserve .text.memory_search .text.memory_search_prepare)
                *(.text.memory_search_prepared_core .text.memory_span_byte)
                *(.text.memory_take .text.memory_free .text.memory_give)
                *(.text.string_compare .text.string_copy .text.string_copy_max_end)
                *(.text.string_digits .text.string_digits_max .text.string_first_of)
                *(.text.string_first_of_or_end .text.string_get_environment)
                *(.text.string_hash_33_length .text.string_last_of .text.string_length)
                *(.text.string_length_max .text.string_set_add .text.string_span)
                *(.text.string_span_max .text.string_table_find .text.string_to_host)
                *(.text.strlen .text.strncmp .text.strnlen .text.strrchr .text.memset)
                *(.text.memcpy .text.bytes_reverse_16 .text.bytes_reverse_32 .text.log)
                *(.text.log_failed .text.log_failure_reset .text.log_flush)
                *(.text.positive_digits_core .text.positive_into)
                *(.text.positive_into_core .text.positive_into_string)
                *(.text.bipolar_into_core .text.bipolar_into_string)
                *(.text.program_argument .text.program_argument_count)
                *(.text.program_argument_list .text.program_arguments_use)
                *(.text.program_environment_list .text.program_initial_identity)
                *(.text.moonwater_cpu_detect .text.shell_key_next .text.lex_line_floor)
                *(.text.lex_line_store .text.system_call .text.system_call_1)
                *(.text.system_call_2 .text.system_call_3 .text.system_call_4)
                *(.text.system_call_5 .text.system_call_6 .text.system_read_retry)
                *(.text.system_wait4_retry .text.system_write_all_checked)
                *(.text.wait_status_code .text.wait_status_code_base)
                *(.text.buffered_flush)
                *(.text.*)
        }

        . = ALIGN(4096);

        .data : {
                *(.data.hot)
                *(.data .data.*)
                *(.got .got.plt)
                /* And small writable ones here, one section each because the
                   programs are built with -fdata-sections. Unmatched, they
                   were placed wherever the linker felt like, which on riscv64
                   is outside every region the loader maps. */
                *(.sdata .sdata.*)
        }

        /* RISC-V _start establishes gp as required by its psABI.  Ordinary
           ELF linker scripts provide this anchor themselves; spark owns its
           complete layout and therefore has to provide it explicitly.  The
           conventional 0x800 bias reaches the full signed twelve-bit window
           around small data.  Other architectures simply leave it unused. */
        PROVIDE(__global_pointer$ = . + 0x800);

        . = ALIGN(4096);

        .bss : {
                __bss_start = .;
                /* What every command writes, on the first page and no
                   other, so a fork copies the one page (HOT_STATE). */
                *(.bss.hot)
                /* Library state every process writes before main, one section
                   each because it is written in assembly (ASM_BSS_OBJECT_BEGIN). */
                *(.bss.program_stack_base .bss.thread_main .bss.threads_live
                  .bss.log_writer_buffer_length .bss.log_writer_failed
                  .bss.writer_stderr_failed .bss.working_directory
                  .bss.program_vdso_clock .bss.program_entry_identity .bss.program_words
                  .bss.cpu_has_* .bss.cpu_hash_probed)
                *(.bss .bss.*)
                *(.sbss .sbss.*)
                *(COMMON)
                . = ALIGN(4096);
                __bss_end = .;
        }

        /* Nothing else is loadable: the format has no section table, no
           dynamic linking and no unwind information. */
        /DISCARD/ : {
                /* A __thread object would be reached at a link-time offset
                   from the thread register, and the register points at the
                   explicit thread block in library.c, not at a TLS
                   image. Discarding the sections turns any such object into a
                   link error naming it, instead of a store into another
                   field of the block. */
                *(.tdata .tdata.* .tbss .tbss.*)
                *(.comment)
                *(.note .note.*)
                *(.eh_frame .eh_frame_hdr)
                *(.gnu.hash .hash .dynsym .dynstr)
        }
}
