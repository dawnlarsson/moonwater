# Canvas is its own object, linked into the same image as the core and
# called by name from it (src/canvas/canvas.c), so there is nothing here
# that registers or loads. kbuild sees this directory through a symlink at
# kernel/canvas, made by kernel/patch/apply beside kernel/moonwater.
#
# canvas.c is the one translation unit. The pixel loops are assembly in
# lib.c, which this unit declares and does not emit, bar the six that are
# Canvas's.
obj-$(CONFIG_MOONWATER_CANVAS) += canvas.o

#       The hook file is built when either switch is on (Kconfig, off by
#       default): CONFIG_MOONWATER_WAYLAND for the Wayland hooks and
#       CONFIG_MOONWATER_X11 for the X11 and XWayland hooks, both in wayland.c.
#       With both off the object is not built at all. The or names the object
#       once, so both switches on do not list it twice.
obj-$(or $(CONFIG_MOONWATER_WAYLAND),$(CONFIG_MOONWATER_X11)) += wayland.o

#       A stock comparison: MOONWATER_STOCK in the environment builds the
#       module without the alias block, so the kernel keeps its own strlen,
#       memcpy and the rest. The core reads the same switch; this has to
#       agree with it, or the two disagree about which names are the library's.
ccflags-y += $(if $(MOONWATER_STOCK),-DSTOCK_STRINGS)
