# Canvas is its own object, linked into the same image as the core and
# called by name from it (src/canvas/canvas.h), so there is nothing here
# that registers or loads. kbuild sees this directory through a symlink at
# kernel/canvas, made by kernel/patch/apply beside kernel/moonwater.
#
# canvas.c is the one translation unit: window.c and term.c are included
# into it, because the terminal emulator is shared with the shell and the
# window is the page a program shares with the compositor, and neither is
# an object of its own. The pixel loops are assembly in lib.c, which this
# unit declares and does not emit, bar the six that are Canvas's.
obj-$(CONFIG_MOONWATER_CANVAS) += canvas.o

#       A stock comparison: MOONWATER_STOCK in the environment builds the
#       module without the alias block, so the kernel keeps its own strlen,
#       memcpy and the rest. The core reads the same switch; this has to
#       agree with it, or the two disagree about which names are the library's.
ccflags-y += $(if $(MOONWATER_STOCK),-DSTOCK_STRINGS)
