# It is a composite rather than obj-y += core.o, because assembly
# cannot be included into a translation unit -- every .asm becomes its own
# object and they have to be listed together to end up in one module. The
# module is named moonwater, because the kernel's namespace is flat and
# that is one of the few places here that needs a name of its own.
obj-$(CONFIG_MOONWATER_CORE) += moonwater.o

#       A stock comparison: MOONWATER_STOCK in the environment builds the
#       module without the alias block, so the kernel keeps its own strlen,
#       memcpy and the rest and the two images can be timed against each
#       other. build.sh empties the claim and displace lists to match.
ccflags-y += $(if $(MOONWATER_STOCK),-DSTOCK_STRINGS)

moonwater-y := core.o

#       The machine script baked into the module. CONFIG_MOONWATER_MACHINE_SCRIPT
#       is a path relative to the repository root. kbuild sees this directory
#       through a symlink at kernel/moonwater, so resolve $(src) first and
#       then walk up, otherwise ../ is kernel/ rather than the repo. Two
#       levels, because this directory is src/moonwater and the root is
#       above src. A miss leaves realpath empty and .incbin is handed "",
#       which the assembler reports as "unable to include `./'".
mw-machine-script := $(patsubst "%",%,$(CONFIG_MOONWATER_MACHINE_SCRIPT))
ifeq ($(mw-machine-script),)
mw-machine-script := main.moonwater.sh
endif
mw-src := $(realpath $(src))
mw-machine-abs := $(realpath $(mw-src)/../../$(mw-machine-script))

moonwater-y += machine_script.o
targets += machine_script.S

quiet_cmd_mwscript = SCRIPT  $@
      cmd_mwscript = { \
	echo '.section .rodata,"a"'; \
	echo '.balign 1'; \
	echo '.globl moonwater_machine_builtin'; \
	echo '.globl moonwater_machine_builtin_end'; \
	echo 'moonwater_machine_builtin:'; \
	echo '.incbin "$(mw-machine-abs)"'; \
	echo 'moonwater_machine_builtin_end:'; \
} > $@

$(obj)/machine_script.S: $(mw-machine-abs) FORCE
	$(call if_changed,mwscript)

#       Floodlight is not part of the module and must never become part of it.
#       It is a separate object with its own initcall, because the whole worth
#       of the file is that its trusted base is the file: folded into
#       moonwater.o it would share a translation unit with the compositor.
#       Nothing links it and it exports nothing, so a separate object here is
#       the only thing that has to be true for it to run.
obj-$(CONFIG_MOONWATER_FLOODLIGHT) += floodlight.o

#
#       Assembly, for every architecture, out of one file each.
#
#       A .asm dropped in this directory is picked up here, translated to a .S
#       for whichever architecture the kernel is being configured for, and
#       assembled by the toolchain's own assembler through the kernel's
#       ordinary rule for .S files. See `build asm` for the dialect.
#
#       This directory only, not found recursively. kernel/replace/ holds the
#       other kind of .asm -- the ones that stand in for a file the kernel
#       already builds -- and those must not be linked into the module as
#       well, or the symbol they define exists twice and vmlinux does not
#       link. Canvas's pixel loops are not .asm either: they are assembly in
#       lib.c, which src/canvas builds into its own object.
#
#       Which architecture that is comes from the kernel's own configuration
#       rather than from the build scripts, so the answer cannot disagree with
#       what is actually being compiled. That holds for the .asm files built
#       here and nowhere else: kernel/replace/apply, which puts a .asm in the
#       place of a file the kernel already builds, runs before any of this and
#       has only the profile's "#> arch" to go on. The two agree because the
#       profile sets both, not because anything checks.
#
#       The pattern -- and the shape of the rule below -- is the one
#       lib/crypto/mips/Makefile uses to generate .S from perl.
#
asm-arch-$(CONFIG_X86_64) := x86_64
asm-arch-$(CONFIG_ARM64) := arm64
asm-arch-$(CONFIG_ARCH_RV64I) := riscv64

asm-sources := $(notdir $(wildcard $(src)/*.asm))

moonwater-y += $(asm-sources:.asm=.o)
targets += $(asm-sources:.asm=.S)

#       $(src) is kernel/moonwater inside the kernel tree, which is a symlink
#       to this repository's src/moonwater. A relative path out of it would
#       land back in the kernel tree, so realpath follows the link home
#       first, and then two levels up to the root where build lives.
quiet_cmd_asm = ASM     $@
      cmd_asm = $(realpath $(src))/../../build asm '$(asm-arch-y)' $< $@

$(obj)/%.S: $(src)/%.asm FORCE
	$(call if_changed,asm)
