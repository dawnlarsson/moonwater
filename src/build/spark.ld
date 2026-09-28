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
                *(.text.*)
        }

        . = ALIGN(4096);

        .data : {
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
