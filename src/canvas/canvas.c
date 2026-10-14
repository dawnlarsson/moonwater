/*
        Moonwater Canvas

        The kernel's half of the desktop, on the DRM client API the
        framebuffer console and drm_log already use. Attaches without patching
        the kernel: a DRM device can be opened like any other file, and struct
        drm_file leads back to the drm_device behind it.

        The desktop is the compositor's, a program (src/canvas/compositor.c)
        that opens the card, becomes its master, and reads the keyboard and the
        pointer itself. What Canvas does is what a program cannot do before it
        starts and what has to be done when it has gone: it finds the cards
        and claims them as a DRM client, chooses each screen's mode and the
        scale, commits one first picture, which is a ground of one colour, and
        holds the card in the meantime. A client in the kernel is never master,
        so the compositor's takes it without a fight, and Canvas draws nothing
        and listens for nothing more than two chords while it has it; when the
        last close of the card is the compositor's, the restore hook draws
        the ground again.

        The rest is the sections below, in dependency order: output, keys,
        client, pointer (the input handler, the console keyboard, the machine's
        sleep and the thread), and last the ioctls the device hands on.

        It is a kernel object of its own, built beside the core and linked
        into the same image. What it asks of the core is src/moonwater/seam.h
        and what the core asks of it is canvas.h; the bottom of this file is
        the calls it answers. The library is declared to it and emitted by the
        core, bar the pixel loops, which are its own.
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/cred.h>
#include <linux/file.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/rtmutex.h>
#include <linux/cleanup.h>
#include <uapi/linux/sched/types.h>
#include <linux/suspend.h>
#include <linux/input.h>
#include <linux/minmax.h>

// The graphics headers must precede lib.c: it defines "end" as a macro
// and asm/io.h, reached through drm_client.h, uses that word as a variable.
#include <drm/drm_client.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_mode.h>
#include <drm/drm_modes.h>
#include <drm/drm_connector.h>
#include <drm/drm_modeset_lock.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_rect.h>
#include <drm/drm_auth.h>
#ifndef MODULE
#include <../drivers/gpu/drm/clients/drm_client_internal.h>
#endif
// The console keyboard's mode, which Canvas turns off while it has the keys.
#ifdef CONFIG_VT
#include <linux/kd.h>
#include <linux/vt_kern.h>
#endif

#include "../moonwater/seam.h"
#include "canvas.h"
#include "canvas_abi.h"

// The parameters below were the module's when Canvas was in it, and a kernel
// command line and a sysfs path name them: moonwater.cursor_plane,
// moonwater.desktop. The object is canvas.o, which would say canvas.
#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX "moonwater."

// The core emits the library; this object takes its declarations, and the
// pixel loops that the core leaves to it (LIBRARY_CANVAS_ELSEWHERE there).
#define LIBRARY_BODIES_ELSEWHERE
#define STANDARD_MODERN_C_KERNEL
#include "../lib.util.c"

/*
        The ground: the one colour Canvas draws.

        Neutral grey, the compositor's own default desktop ink, so that the
        picture the kernel leaves is the same ground and not a flash of
        another colour when the compositor takes the screen.
*/
#define CANVAS_GROUND 0x1e1e1e

/* The kernel's guard for the lock the desktop is taken under; it has one for
   mutex and spinlock but none for rt_mutex. */
DEFINE_GUARD(rt_mutex, struct rt_mutex *, rt_mutex_lock(_T), rt_mutex_unlock(_T))

struct output
{
        struct list_head link;
        struct canvas *canvas;
        struct drm_client_buffer *buffer;
        struct drm_mode_set *mode_set;

        int x, y;
        unsigned int width, height;
        u32 opaque;

        // Said once. Everything drawn goes through the mapping, so a failure
        // to make one is a screen with nothing on it, not a dropped frame.
        _Bool unmappable;

        // The scanout the CRTC still holds after a grow, freed once the
        // commit that replaces it has landed. Destroying it sooner blanks
        // the pipe.
        struct drm_client_buffer *replaced;
};

struct canvas
{
        struct list_head link;
        struct drm_client_dev client;
        _Bool started;

        // What the last commit answered, so the same answer is not said
        // twice. One, because no commit ever answers that: nothing has been
        // put on this card's screens yet.
        int set_result;

        // Hotplug runs here, never on the DRM helper workqueue. A commit
        // from that queue can wait on the same queue, which is a lockup.
        struct work_struct plug;

        // A hotplug that arrived while the machine slept: the card's power was
        // going or half back, so it waits for the wake to run.
        _Bool replug;

        // The next hotplug is a change of which mode to take, and the screen
        // may get smaller by it as well as larger.
        _Bool retarget;
};

static struct desktop
{
        /*
                An rt_mutex, so whoever holds it runs at the priority of whoever
                waits for it: the canvas thread is SCHED_FIFO, and a plain mutex
                would leave it waiting behind an ordinary task that holds it.
        */
        struct rt_mutex lock;

        /*
                Serialises the input handler against itself.

                It is called straight from the input core, once per device,
                and every device has its own lock there, so two devices
                reporting at the same moment were two writers on one word of
                modifiers. A leaf lock held for the length of one event.
        */
        spinlock_t input_lock;
        struct list_head outputs;

        /*
                Another program is master of a card Canvas draws on. Nothing
                drawn would land. Written under the lock; see
                canvas_suspend_check.
        */
        _Bool suspended;

        /*
                The machine is going to sleep, or has not finished waking.

                Not the same as suspended: no program holds the card, the card
                is about to have its power taken. Anything Canvas sent it from
                here on -- a modeset commit, a dirtyfb -- reached a device
                that was off or half back, and a driver that waits on its own
                hardware for such a call never returned. So it counts as the
                card being taken (desktop_taken). Written under the lock.
        */
        _Bool asleep;

        /*
                How many device pixels one drawn pixel is. Canvas draws
                nothing that has a size, but the compositor asks for the
                number the screens say (compositor.c, SPARK_CANVAS_STATUS) so
                that the same window is the same size in millimetres on a
                ninety dot inch screen and on a Retina one.
        */
        unsigned int scale;

        // The bounding box of every output. Read by the input handler in
        // atomic context, where it cannot walk the list.
        int width, height;

        // The modifier keys held, as the handler last recorded them.
        atomic_t modifiers;

        // Control-Alt-Backspace, as the handler recorded it: the thread acts,
        // and only after it has looked at who holds the display.
        atomic_t reclaim;
} desktop = {
    .lock = __RT_MUTEX_INITIALIZER(desktop.lock),
    .input_lock = __SPIN_LOCK_UNLOCKED(desktop.input_lock),
    .outputs = LIST_HEAD_INIT(desktop.outputs),
    .scale = 1,
};

static DEFINE_MUTEX(canvas_list_lock);
static LIST_HEAD(canvas_list);

static void canvas_thread_start(void);
static void canvas_thread_stop(void);
static void canvas_thread_wake(void);

// Counted for `moonwater canvas`: how often the thread wakes while idle.
static unsigned int canvas_passes;

/*
        Whether another program is master of a card Canvas draws on, as the
        canvas thread last found it, and what the input handler costs: the
        reports it was called with, and those it had nothing to do with.

        canvas_suspend_check is the only writer; `moonwater latency` reads it.
*/
static _Bool canvas_yielded;
static DEFINE_PER_CPU(unsigned long, canvas_quiet);
static DEFINE_PER_CPU(struct handler_cost, canvas_cost);

static void output_free(struct output *output);
static void desktop_redraw(void);
static void desktop_recompose(void);

static _Bool canvas_scale_refresh(void);

static _Bool desktop_taken(void);
static _Bool desktop_lit(void);

void canvas_rect_fill(u32 *at, unsigned long pitch, unsigned long width,
                      unsigned long height, u32 colour);

/*
        moonwater.desktop is the machine script's switch: it reads the word
        from /proc/cmdline and decides what to start (the compositor, a
        profile's session, or nothing). This is only here so that the kernel
        knows the word and does not warn about a parameter it has never heard
        of.
*/
static char *canvas_desktop;

module_param_named(desktop, canvas_desktop, charp, 0444);

static CONST struct canvas *canvas_from_client(struct drm_client_dev *client)
{
        return container_of(client, struct canvas, client);
}

// The first format in the plane's own order that is one of the two wanted.
static PURE u32 canvas_plane_pick_format(struct drm_plane *plane, u32 first, u32 second)
{
        unsigned int i;

        for (i = 0; i < plane->format_count; i++)
                if (plane->format_types[i] == first ||
                    plane->format_types[i] == second)
                        return plane->format_types[i];

        return DRM_FORMAT_INVALID;
}

/*
        The scanout buffer, mapped.

        Everything Canvas draws goes through this pointer, so failing to get
        one is not a dropped frame, it is a screen that stays as it was. It
        used to be a bare return.
*/
static _Bool output_map(struct output *output, struct iosys_map *map)
{
        int ret = drm_client_buffer_vmap_local(output->buffer, map);

        if (!ret)
        {
                output->unmappable = false;
                return true;
        }

        if (!output->unmappable)
        {
                output->unmappable = true;
                pr_err("[moonwater canvas] " "the scanout buffer will not map (%d), "
                                                 "so nothing can be drawn on it\n", ret);
        }

        return false;
}

/*
        What the driver actually gave us to scan out, and whether it can be
        written to at all.

        The mapping is made once here rather than first discovered halfway
        through a fill. Everything Canvas draws goes through it, so a driver
        that will not give us one is a screen that stays exactly as it was
        while mode setting and the cursor plane both go on working -- black
        from the first moment, with a cursor moving over it. Answering no here
        is what lets that screen be handed back instead of held.
*/
static _Bool output_describe(struct output *output)
{
        struct drm_framebuffer *fb = output->buffer->fb;
        struct iosys_map map;

        if (!output_map(output, &map))
                return false;

        pr_info("[moonwater canvas] " "scanout %p4cc, %u bytes a row (%lu KiB), modifier %llx, "
                           "%s memory\n", &fb->format->format, fb->pitches[0], ((unsigned long)fb->pitches[0] * output->height) >> 10, (unsigned long long)fb->modifier, map.is_iomem ? "device" : "system");

        drm_client_buffer_vunmap_local(output->buffer);
        return true;
}

/*
        moonwater.cursor_plane=0 turns the cursor plane off: the compositor
        reads it, from /sys/module/moonwater/parameters, and draws the pointer
        into its own scanout buffer, as it does on a card with no cursor plane.

        The escape hatch for a machine whose cursor plane is broken --
        misplaced, stale, or never shown -- on a card where the software
        pointer still works, slower on every move and otherwise the same. The
        canvas lane boots with it too, because QEMU's screendump holds the
        primary plane only.
*/
static bool canvas_cursor_plane = true;

module_param_named(cursor_plane, canvas_cursor_plane, bool, 0444);

/* ---- output: outputs, what the hardware offers and what is taken from it ---- */

/*
        Canvas -- outputs

        An output is a rectangle of the desktop that one crtc scans out. They
        are laid left to right in the order their cards attached, which is the
        placement; the desktop is their bounding box.
*/

/*
        Whether this display is a window on somebody else's screen.

        The driver's own name, which is the only thing here that actually
        knows. Working it out from what the display reports does not succeed:
        a virtual one answers a physical size anyway, bochs says 320 by 200
        millimetres whatever mode it is in, and QEMU synthesises an EDID too,
        so neither the size nor the presence of one separates them.
*/
/* Is this device's driver one of these? Asked of two lists below, with
   the same answer for a device that has no driver or no name. */
static PURE _Bool canvas_driver_among(struct drm_device *dev,
                                      const char *const *names, positive count)
{
        if (!dev->driver || !dev->driver->name)
                return false;

        return string_table_find((string_address)dev->driver->name, names,
                                 sizeof(names[0]), count) < count;
}

static PURE _Bool canvas_is_virtual(struct drm_device *dev)
{
        static const char *const guests[] = {
            "bochs-drm", "virtio_gpu", "qxl", "vmwgfx", "cirrus-qemu",
            "hyperv_drm", "vkms"};

        return canvas_driver_among(dev, guests, array_count(guests));
}

/*
        The firmware's leftover framebuffer, not a GPU.

        simpledrm (and efidrm) bind to whatever GOP left in sysfb. On a
        Dell with i915 that is card0 at the firmware's size -- often
        1024x768 -- and Canvas starting there paints the kernel log into
        that buffer. i915 then takes the same pipe; the firmware client is
        kicked with a picture on it, and the machine freezes. It is a last
        resort when no real card appears.
*/
static PURE _Bool canvas_is_firmware(struct drm_device *dev)
{
        static const char *const firmware[] = {"simpledrm", "efidrm"};

        return canvas_driver_among(dev, firmware, array_count(firmware));
}

/*
        How many device pixels one drawn pixel is.

        A cell is eight by sixteen and a titlebar is twenty. Those are the
        right numbers on an ordinary screen and a quarter of the right
        numbers on a dense one, where everything comes out too small to read.
        The scale multiplies every metric the compositor draws for itself,
        so the same window covers the same area of glass whatever the panel
        is. It is a setting, kept by `moonwater canvas scale`, and not a
        build option, because it is the machine's and not the image's.

        The setting is a number, or auto. Auto asks the screens, and trusts
        only the ones that can say: a real panel, whose connector is one a
        panel is wired to and whose EDID gives a size that fits the shape of
        its mode. It is not the size a virtual display reports -- bochs
        answers 320 by 200 millimetres whatever mode it is in, and QEMU
        synthesises an EDID too, so dots per inch worked out from either
        swings with the resolution and says nothing. A screen that cannot
        say is one: what auto changes is only a panel that is dense.
        Two from 150 dots to the inch, three from 260, and never so many
        that the desktop is left under 1280 by 640 of the pixels it draws.

        A kernel command line gives the first frame its setting
        (canvas.scale=2 or canvas.scale=auto); the machine's own, kept in
        /root, arrives with the machine process seconds after.
*/
static unsigned int canvas_scale_setting = SPARK_CANVAS_SCALE_AUTO;
static unsigned int canvas_scale_origin = SPARK_CANVAS_ORIGIN_DEFAULT;

// Which mode a real screen is driven at; a virtual one is a window and has none.
static _Bool canvas_modes_largest = true;

static _Bool canvas_connector_is_panel(const struct drm_connector *connector)
{
        switch (connector->connector_type)
        {
        case DRM_MODE_CONNECTOR_eDP:
        case DRM_MODE_CONNECTOR_DisplayPort:
        case DRM_MODE_CONNECTOR_HDMIA:
        case DRM_MODE_CONNECTOR_HDMIB:
        case DRM_MODE_CONNECTOR_DVID:
        case DRM_MODE_CONNECTOR_LVDS:
        case DRM_MODE_CONNECTOR_DSI:
                return true;
        }

        return false;
}

// What one screen's mode, on its own panel, says the scale should be: 0 when
// it cannot say, and the dots per inch that decided it.
static unsigned int canvas_scale_of(const struct drm_mode_set *set,
                                    unsigned int *dpi)
{
        const struct drm_connector *connector;
        unsigned long width, height, width_mm, height_mm;
        unsigned int scale;

        if (!set || !set->mode || !set->num_connectors || !set->connectors ||
            !set->connectors[0])
                return 0;

        connector = set->connectors[0];
        if (!canvas_connector_is_panel(connector) ||
            canvas_is_virtual(connector->dev))
                return 0;

        width = set->mode->hdisplay;
        height = set->mode->vdisplay;
        width_mm = connector->display_info.width_mm;
        height_mm = connector->display_info.height_mm;

        // A size is a size when it is a panel's and its shape is the mode's,
        // to within a seventh: EDID counts whole centimetres, and a zero, a
        // one or a 16:9 claimed of a 4:3 mode is somebody filling a field.
        if (width_mm < 50 || width_mm > 3000 || height_mm < 30 || height_mm > 2000 ||
            (unsigned long)abs((long)(width_mm * height) - (long)(height_mm * width)) * 7 >
                height_mm * width)
                return 0;

        *dpi = (unsigned int)(width * 254 / (width_mm * 10));
        scale = *dpi >= 260 ? 3 : *dpi >= 150 ? 2 : 1;

        while (scale > 1 && (width / scale < 1280 || height / scale < 640))
                scale--;

        return scale;
}

// The first screen of the desktop that can say, then the ones about to join it.
static unsigned int canvas_scale_auto(struct drm_client_dev *joining,
                                      unsigned int *dpi)
{
        struct drm_mode_set *set;
        struct output *output;
        unsigned int scale;

        *dpi = 0;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                scale = canvas_scale_of(output->mode_set, dpi);
                if (scale)
                        return scale;
        }

        if (joining)
                drm_client_for_each_modeset(set, joining)
                {
                        scale = canvas_scale_of(set, dpi);
                        if (scale)
                                return scale;
                }

        return 1;
}

static unsigned int canvas_scale_wanted(struct drm_client_dev *joining)
{
        unsigned int dpi;

        return canvas_scale_setting ? canvas_scale_setting
                                    : canvas_scale_auto(joining, &dpi);
}

//      The kernel command line's: canvas.scale=2, canvas.scale=auto.
static int canvas_scale_param_set(const char *value, const struct kernel_param *kp)
{
        unsigned int number;

        if (sysfs_streq(value, "auto"))
                number = SPARK_CANVAS_SCALE_AUTO;
        else if (kstrtouint(value, 10, &number) || !number ||
                 number > SPARK_CANVAS_SCALE_MOST)
                return -EINVAL;

        canvas_scale_setting = number;
        canvas_scale_origin = SPARK_CANVAS_ORIGIN_COMMAND_LINE;
        return 0;
}

static int canvas_scale_param_get(char *buffer, const struct kernel_param *kp)
{
        return canvas_scale_setting ? sysfs_emit(buffer, "%u\n", canvas_scale_setting)
                                    : sysfs_emit(buffer, "auto\n");
}

static const struct kernel_param_ops canvas_scale_ops = {
    .set = canvas_scale_param_set,
    .get = canvas_scale_param_get,
};

static int canvas_modes_param_set(const char *value, const struct kernel_param *kp)
{
        if (sysfs_streq(value, "largest"))
                canvas_modes_largest = true;
        else if (sysfs_streq(value, "preferred"))
                canvas_modes_largest = false;
        else
                return -EINVAL;

        return 0;
}

static int canvas_modes_param_get(char *buffer, const struct kernel_param *kp)
{
        return sysfs_emit(buffer, "%s\n", canvas_modes_largest ? "largest" : "preferred");
}

static const struct kernel_param_ops canvas_modes_ops = {
    .set = canvas_modes_param_set,
    .get = canvas_modes_param_get,
};

// Read-only: a running desktop is changed by moonwater canvas, which relays it.
#undef MODULE_PARAM_PREFIX

#define MODULE_PARAM_PREFIX "canvas."

module_param_cb(scale, &canvas_scale_ops, NULL, 0444);

module_param_cb(modes, &canvas_modes_ops, NULL, 0444);

#undef MODULE_PARAM_PREFIX

#define MODULE_PARAM_PREFIX "moonwater."

static PURE unsigned int output_mode_count(struct drm_connector *connector)
{
        struct drm_display_mode *mode;
        unsigned int count = 0;

        list_for_each_entry(mode, &connector->modes, head)
                count++;

        return count;
}

/*
        The best mode a connector lists among those that pass: every bit of
        type set, a type of 0 being any, and when width is not 0 a size equal
        to width by height or, unless exact, one that fits under it. Of those:
        a refresh equal to prefer_refresh first when it is positive, then the
        most pixels, then the highest refresh.

        Same size at 60 Hz and 120 Hz is the 120 Hz entry: that is what a
        Mac's virtio EDID actually offers, and what the cursor needs. A guest
        cap of seventy percent of a 120 Hz panel often still lists a larger
        60 Hz established timing under that cap; pixels first would take it
        and throw the refresh away, so the screen's rate comes first.
*/
static struct drm_display_mode *output_pick_mode(struct drm_connector *connector,
                                                 unsigned int type, int width,
                                                 int height, _Bool exact,
                                                 int prefer_refresh)
{
        struct drm_display_mode *mode, *best = NULL;
        int best_score = 0, best_refresh = 0;
        _Bool best_preferred = false;

        list_for_each_entry(mode, &connector->modes, head)
        {
                int score, refresh;
                _Bool preferred;

                if ((mode->type & type) != type)
                        continue;
                if (width && (exact ? mode->hdisplay != width ||
                                          mode->vdisplay != height
                                    : mode->hdisplay > width ||
                                          mode->vdisplay > height))
                        continue;
                if (mode->flags & (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN))
                        continue;

                refresh = drm_mode_vrefresh(mode);
                score = mode->hdisplay * mode->vdisplay;
                preferred = prefer_refresh > 0 && refresh == prefer_refresh;
                if (best && (preferred < best_preferred ||
                             (preferred == best_preferred &&
                              (score < best_score ||
                               (score == best_score && refresh <= best_refresh)))))
                        continue;

                best = mode;
                best_score = score;
                best_refresh = refresh;
                best_preferred = preferred;
        }

        return best;
}

/*
        The mode the first successful start put on a connector.

        Off and on probes again. A guest's preferred size is then the window
        already committed -- seventy percent of that, not of the host -- and
        a real screen's probe is the preferred mode, not the largest one
        startup took. Remembering the committed size keeps restart on the
        same picture.
*/
#define CANVAS_SAVED_MODES 8

struct canvas_saved_mode
{
        char name[32];
        int hdisplay;
        int vdisplay;
        int vrefresh;
};

static struct canvas_saved_mode canvas_saved_mode[CANVAS_SAVED_MODES];
static unsigned int canvas_saved_modes;

static const struct canvas_saved_mode *canvas_mode_saved(const char *name)
{
        unsigned int i;

        if (!name)
                return NULL;

        for (i = 0; i < canvas_saved_modes; i++)
                if (!strcmp(canvas_saved_mode[i].name, name))
                        return &canvas_saved_mode[i];

        return NULL;
}

static void canvas_mode_keep(const char *name, const struct drm_display_mode *mode)
{
        unsigned int i;

        if (!name || !mode)
                return;

        for (i = 0; i < canvas_saved_modes; i++)
                if (!strcmp(canvas_saved_mode[i].name, name))
                {
                        canvas_saved_mode[i].hdisplay = mode->hdisplay;
                        canvas_saved_mode[i].vdisplay = mode->vdisplay;
                        canvas_saved_mode[i].vrefresh = drm_mode_vrefresh(mode);
                        return;
                }

        if (canvas_saved_modes >= CANVAS_SAVED_MODES)
                return;

        strscpy(canvas_saved_mode[canvas_saved_modes].name, name,
                sizeof(canvas_saved_mode[0].name));
        canvas_saved_mode[canvas_saved_modes].hdisplay = mode->hdisplay;
        canvas_saved_mode[canvas_saved_modes].vdisplay = mode->vdisplay;
        canvas_saved_mode[canvas_saved_modes].vrefresh = drm_mode_vrefresh(mode);
        canvas_saved_modes++;
}

static void canvas_modes_keep(void)
{
        struct output *output;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                struct drm_mode_set *set = output->mode_set;

                if (!set || !set->mode || !set->num_connectors ||
                    !set->connectors || !set->connectors[0] ||
                    !set->connectors[0]->name)
                        continue;

                canvas_mode_keep(set->connectors[0]->name, set->mode);
        }
}

/*
        The host's idea of the screen: the preferred mode, or the largest
        if the connector did not mark one.
*/
static struct drm_display_mode *output_screen_mode(struct drm_connector *connector)
{
        struct drm_display_mode *preferred = output_pick_mode(
            connector, DRM_MODE_TYPE_PREFERRED, 0, 0, false, 0);

        return preferred ? preferred
                         : output_pick_mode(connector, 0, 0, 0, false, 0);
}

/*
        A copy of src with its CRTC timings filled, or nothing.

        Connector modes leave crtc_clock at 0 until a modeset fills it.
        virtio-gpu's vblank timer reads that field; 0 means no vblank, and
        the next flip waits ten seconds then warns.
*/
static struct drm_display_mode *output_mode_take(struct drm_device *dev,
                                                 const struct drm_display_mode *src)
{
        struct drm_display_mode *taken;

        if (!src || src->clock <= 0)
                return NULL;

        taken = drm_mode_duplicate(dev, src);
        if (!taken)
                return NULL;

        drm_mode_set_crtcinfo(taken, CRTC_INTERLACE_HALVE_V);
        if (taken->crtc_clock <= 0)
        {
                drm_mode_destroy(dev, taken);
                return NULL;
        }

        return taken;
}

/*
        A guest is a window on somebody else's screen, so it cannot take the
        whole of that screen. Seventy percent of the host, in the pixels the
        host already scaled: cocoa then divides by the backing factor and
        centres the window, which is seventy percent of the DIP screen on a
        Retina panel.

        The mode has to be one the connector already listed. A CVT line for
        this size is a size virtio-gpu will scan out, but it arrives with
        crtc_clock still 0, the CRTC never generates vblank, and the first
        flip after the picture is on screen waits forever. The 120 Hz entry
        of a listed size is still preferred over a 60 Hz established timing
        of the same width and height. When seventy percent itself is not
        listed, the under-cap pick prefers the screen's refresh before it
        prefers more pixels.
*/
static struct drm_display_mode *output_guest_mode(struct drm_device *dev,
                                                  struct drm_connector *connector)
{
        struct drm_display_mode *screen, *listed;
        int w, h;

        screen = output_screen_mode(connector);
        if (!screen)
                return NULL;

        w = screen->hdisplay * 7 / 10;
        h = screen->vdisplay * 7 / 10;
        w &= ~1;
        h &= ~1;
        if (w < 2)
                w = 2;
        if (h < 2)
                h = 2;

        listed = output_pick_mode(connector, 0, w, h, true, 0);
        if (listed)
                return output_mode_take(dev, listed);

        listed = output_pick_mode(connector, 0, w, h, false, drm_mode_vrefresh(screen));
        if (listed)
                return output_mode_take(dev, listed);

        return output_mode_take(dev, screen);
}

/*
        Probes every connector and puts the best mode on each modeset.

        The lock order is the one drm_client_modeset_probe uses: the client's
        modesets, then the device's mode configuration, which is what guards a
        connector's list of modes.
*/
static COLD int canvas_probe_modes(struct canvas *canvas, _Bool biggest,
                                   _Bool keep_saved)
{
        struct drm_client_dev *client = &canvas->client;
        struct drm_device *dev = client->dev;
        struct drm_mode_set *mode_set;

        if (drm_client_modeset_probe(client, 0, 0))
                return -ENODEV;

        mutex_lock(&client->modeset_mutex);
        mutex_lock(&dev->mode_config.mutex);

        drm_client_for_each_modeset(mode_set, client)
        {
                struct drm_connector *connector;
                struct drm_display_mode *want, *taken;
                const struct canvas_saved_mode *saved;

                if (!mode_set->mode || !mode_set->num_connectors ||
                    !mode_set->connectors || !mode_set->connectors[0])
                        continue;

                connector = mode_set->connectors[0];
                saved = keep_saved && connector->name
                                ? canvas_mode_saved(connector->name)
                                : NULL;
                /*
                        A guest stays a window even when the first modeset is
                        refused. The retry used to take the probe's preferred
                        size, which is the host's whole screen, and that is
                        the blow-out the seventy percent cap exists to stop.

                        A later start has already committed once: take that
                        size rather than seventy percent of the window, or
                        the probe's preferred instead of the largest.

                        A hotplug on a running card must not: the first
                        picture is often a fallback written before EDID
                        finished, and restoring that size leaves a real
                        screen at 1024x768 for the rest of the boot.
                */
                if (saved)
                {
                        want = output_pick_mode(connector, 0, saved->hdisplay,
                                                saved->vdisplay, true,
                                                saved->vrefresh);
                        if (!want || drm_mode_equal(want, mode_set->mode))
                                continue;
                        taken = output_mode_take(dev, want);
                        if (!taken)
                                continue;
                }
                else if (canvas_is_virtual(dev))
                {
                        taken = output_guest_mode(dev, connector);
                        if (!taken)
                                continue;
                        if (drm_mode_equal(taken, mode_set->mode))
                        {
                                drm_mode_destroy(dev, taken);
                                continue;
                        }
                }
                else
                {
                        want = biggest ? output_pick_mode(connector, 0, 0, 0, false, 0)
                                       : output_pick_mode(connector, DRM_MODE_TYPE_PREFERRED,
                                                          0, 0, false, 0);
                        if (!want && !biggest)
                                want = output_pick_mode(connector, 0,
                                                        mode_set->mode->hdisplay,
                                                        mode_set->mode->vdisplay,
                                                        true, 0);
                        if (!want || drm_mode_equal(want, mode_set->mode))
                                continue;
                        taken = output_mode_take(dev, want);
                        if (!taken)
                                continue;
                }

                drm_mode_destroy(dev, mode_set->mode);
                mode_set->mode = taken;
        }

        mutex_unlock(&dev->mode_config.mutex);
        mutex_unlock(&client->modeset_mutex);

        return 0;
}

static struct output *output_for_modeset(struct canvas *canvas,
                                         struct drm_mode_set *mode_set)
{
        struct output *output;

        list_for_each_entry(output, &desktop.outputs, link)
                if (output->canvas == canvas && output->mode_set == mode_set)
                        return output;

        return NULL;
}

/*
        Puts this output's buffer back on the modeset that is scanning it.

        A probe may have replaced the mode with a different size. The buffer
        we already have is the one on the CRTC; a modeset of a new size and
        the old framebuffer is refused, and destroying that framebuffer while
        the CRTC still holds it blanks the scanout. So the running size is
        restored, and a guest window that grew can wait.
*/
static void output_attach(struct output *output)
{
        struct drm_mode_set *mode_set = output->mode_set;
        struct drm_device *dev;
        struct drm_connector *connector;
        struct drm_display_mode *want, *taken;

        if (!mode_set || !output->buffer)
                return;

        if (!mode_set->mode)
                return;

        if (mode_set->mode->hdisplay == (int)output->width &&
            mode_set->mode->vdisplay == (int)output->height)
        {
                mode_set->fb = output->buffer->fb;
                return;
        }

        dev = output->canvas->client.dev;
        connector = (mode_set->connectors && mode_set->num_connectors)
                        ? mode_set->connectors[0]
                        : NULL;
        if (!connector)
                return;

        mutex_lock(&dev->mode_config.mutex);
        want = output_pick_mode(connector, 0, (int)output->width,
                                (int)output->height, true, 0);
        taken = output_mode_take(dev, want);
        mutex_unlock(&dev->mode_config.mutex);

        if (!taken)
                return;

        drm_mode_destroy(dev, mode_set->mode);
        mode_set->mode = taken;
        mode_set->fb = output->buffer->fb;
}

/*
        A later probe listed a larger mode than the one already scanning.

        The first picture is often a fallback: i915 writes 1024x768 before
        EDID finishes, then hotplugs with the panel's real list. The buffer
        on the CRTC is the old size, so a modeset of the new size with that
        framebuffer is refused. A new buffer is made first; the old one is
        kept until the commit that points the pipe at the new one has
        landed, because freeing it sooner blanks the scanout.
*/
static _Bool output_grow(struct output *output, struct drm_mode_set *mode_set,
                         _Bool either)
{
        unsigned int width, height;
        u32 format;
        struct drm_client_buffer *fresh;

        if (!mode_set || !mode_set->mode || !mode_set->crtc ||
            !mode_set->crtc->primary)
                return false;

        width = mode_set->mode->hdisplay;
        height = mode_set->mode->vdisplay;
        // A hotplug only takes a screen that got larger; a change of which
        // mode to take takes the one that is not the size it has.
        if (either ? width == output->width && height == output->height
                   : (unsigned long)width * height <=
                         (unsigned long)output->width * output->height)
                return false;

        format = canvas_plane_pick_format(mode_set->crtc->primary,
                                          DRM_FORMAT_XRGB8888,
                                          DRM_FORMAT_ARGB8888);
        if (format == DRM_FORMAT_INVALID)
                return false;

        fresh = drm_client_buffer_create_dumb(&output->canvas->client, width,
                                              height, format);
        if (IS_ERR(fresh))
                return false;

        if (output->replaced)
                drm_client_buffer_delete(output->buffer);
        else
                output->replaced = output->buffer;

        output->buffer = fresh;
        output->width = width;
        output->height = height;
        output->opaque = format == DRM_FORMAT_ARGB8888 ? 0xff000000 : 0;
        mode_set->fb = fresh->fb;
        return true;
}

static void desktop_place_outputs(void);
static struct output *output_add(struct canvas *canvas, struct drm_mode_set *mode_set);
static void output_disable_modeset(struct drm_device *dev,
                                   struct drm_mode_set *mode_set);

/*
        A screen with a mode: say which, and how much of a choice there was,
        then put an output on it. A mode that turns out to be wrong on a
        machine that is not here is answered by what its connector offered,
        not by what was picked out of it. A screen that cannot take an output
        has its mode set turned off.
*/
static _Bool output_bring_up(struct canvas *canvas, struct drm_mode_set *mode_set)
{
        struct drm_connector *connector =
            mode_set->num_connectors ? mode_set->connectors[0] : NULL;
        struct output *output;

        pr_info("[moonwater canvas] " "screen %s %ux%u at %u Hz, drawn %ux, %u mode(s) offered\n", connector && connector->name ? connector->name : "?", mode_set->mode->hdisplay, mode_set->mode->vdisplay, drm_mode_vrefresh(mode_set->mode), desktop.scale, connector ? output_mode_count(connector) : 0);

        output = output_add(canvas, mode_set);
        if (!output)
        {
                output_disable_modeset(canvas->client.dev, mode_set);
                return false;
        }

        list_add_tail(&output->link, &desktop.outputs);
        return true;
}

/*
        A hotplug after the first picture.

        drm_client_modeset_probe drops every modeset's framebuffer pointer.
        The old path treated a different mode -- or a different count of them
        -- as a reason to destroy the buffers still on the CRTCs. That is
        SET_SCANOUT 0. virtio-gpu then replaces the host window with
        "Display output is not active", and a later commit does not always
        get that window back.

        Guest displays also fire a hotplug about a second after the first
        scanout, when the host window is up. That callback runs on the DRM
        helper workqueue. A commit from here disables every cursor plane
        (drm_client_modeset_commit does) and can wait on that same queue,
        so the pointer thread never runs again and the plane is left off.
        The mode already scanning is the one the window has; leave it.

        A real card is the same queue. i915 hotplugs after EDID, and with a
        cursor plane the commit from here is the virtio lockup: low-res
        kernel log, no pointer, no terminal. The callback only queues; this
        runs on moonwater/plug. A guest still returns above. A real screen
        may grow if the connector now lists more pixels than the fallback
        that was committed first; a hotplug that does not grow does not
        commit, so the cursor plane stays up.
*/
static int canvas_rebind(struct canvas *canvas)
{
        struct drm_client_dev *client = &canvas->client;
        struct drm_mode_set *mode_set;
        struct output *output;
        _Bool placed = false;
        _Bool retarget = xchg(&canvas->retarget, false);

        if (canvas_is_virtual(client->dev))
                return 0;

        if (canvas_probe_modes(canvas, READ_ONCE(canvas_modes_largest), false))
        {
                desktop_redraw();
                return 0;
        }

        mutex_lock(&client->modeset_mutex);
        drm_client_for_each_modeset(mode_set, client)
        {
                if (!mode_set->mode)
                        continue;

                output = output_for_modeset(canvas, mode_set);
                if (output)
                {
                        if (output_grow(output, mode_set, retarget))
                                placed = true;
                        else
                                output_attach(output);
                        continue;
                }

                if (output_bring_up(canvas, mode_set))
                        placed = true;
        }
        mutex_unlock(&client->modeset_mutex);

        // A change of which mode to take is a commit whatever sizes came of it:
        // the largest and the preferred can be the one size at two refresh rates.
        placed |= retarget;

        if (placed)
        {
                desktop_place_outputs();
                canvas_scale_refresh();
                canvas_modes_keep();
                desktop_redraw();
        }
        else if (!desktop_lit())
        {
                /*
                        Nothing grew, and the driver no longer shows Canvas's
                        picture: a connector that went and came back (a panel
                        after a wake) leaves a mode of the same size, which
                        used to mean no commit and a screen that stayed off.
                */
                if (!desktop_taken())
                {
                        pr_info("[moonwater canvas] " "hotplug: the screen is not showing the desktop, drawing it again\n");
                        desktop_redraw();
                }
        }

        return 0;
}

static void desktop_place_outputs(void)
{
        struct output *output;
        int x = 0, height = 0;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                output->x = x;
                output->y = 0;

                x += (int)output->width;
                height = max(height, (int)output->height);
        }

        desktop.width = x;
        desktop.height = height;
}

static struct output *output_add(struct canvas *canvas, struct drm_mode_set *mode_set)
{
        unsigned int width = mode_set->mode->hdisplay;
        unsigned int height = mode_set->mode->vdisplay;
        u32 format = canvas_plane_pick_format(mode_set->crtc->primary,
                                              DRM_FORMAT_XRGB8888,
                                              DRM_FORMAT_ARGB8888);
        struct output *output;

        if (format == DRM_FORMAT_INVALID)
        {
                pr_err("[moonwater canvas] " "no 32 bit format on this plane, skipping output\n");
                return NULL;
        }

        output = kzalloc(sizeof(*output), GFP_KERNEL);
        if (!output)
                return NULL;

        output->buffer = drm_client_buffer_create_dumb(&canvas->client, width, height, format);
        if (IS_ERR(output->buffer))
        {
                pr_err("[moonwater canvas] " "could not create a %ux%u scanout buffer\n", width, height);
                kfree(output);
                return NULL;
        }

        output->canvas = canvas;
        output->mode_set = mode_set;
        output->width = width;
        output->height = height;
        output->opaque = format == DRM_FORMAT_ARGB8888 ? 0xff000000 : 0;
        mode_set->fb = output->buffer->fb;

        if (!output_describe(output))
        {
                drm_client_buffer_delete(output->buffer);
                mode_set->fb = NULL;
                kfree(output);
                return NULL;
        }

        return output;
}

/*
        A modeset nothing will be drawn on, left the way a probe leaves one.

        A screen that could not be given a buffer -- no 32 bit format on its
        plane, no memory for one -- still carries the mode the probe chose and
        no framebuffer to go with it, and that combination is not one screen
        missing: the commit carrying it is refused whole, so one connector
        nobody can draw on takes every other screen on the card down with it.

        Clearing it is what says "this crtc is off", and then the screens that
        did build commit without it.
*/
static void output_disable_modeset(struct drm_device *dev,
                                   struct drm_mode_set *mode_set)
{
        unsigned int i;

        if (mode_set->mode)
        {
                drm_mode_destroy(dev, mode_set->mode);
                mode_set->mode = NULL;
        }

        mode_set->fb = NULL;

        for (i = 0; mode_set->connectors && i < (unsigned int)mode_set->num_connectors; i++)
        {
                drm_connector_put(mode_set->connectors[i]);
                mode_set->connectors[i] = NULL;
        }

        mode_set->num_connectors = 0;
}

static void output_free(struct output *output)
{
        drm_client_buffer_delete(output->buffer);
        drm_client_buffer_delete(output->replaced);
        kfree(output);
}

static void output_drop(struct output *output)
{
        list_del(&output->link);
        output_free(output);
}

/*
        Puts every output's buffer back on the modeset that scans it out.

        A probe releases every modeset, and releasing one takes its framebuffer
        away. A modeset carrying a mode and no framebuffer is refused, the
        whole commit with it, and what stays on the screen is whatever was
        there before this ever ran: on a machine that inherits the firmware's
        picture that is a cursor moving over it and nothing else.

        So it is set before every commit rather than once when the output was
        made. Whoever cleared it, and for whatever reason, it is right again by
        the time it matters.
*/
static void desktop_attach_buffers(void)
{
        struct output *output;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                struct drm_client_dev *client = &output->canvas->client;

                if (!output->buffer)
                        continue;

                mutex_lock(&client->modeset_mutex);
                output->mode_set->fb = output->buffer->fb;
                mutex_unlock(&client->modeset_mutex);
        }
}

/*
        A card's outputs are added together, so they are consecutive here and
        remembering the last one is enough to commit each card once.

        The answer used to be thrown away. A commit is the only thing that puts
        a mode on a screen, and one that refuses says so in the one place that
        could have noticed.

        known is a card committed a moment ago whose answer was known_set; it
        is not committed a second time. canvas_start commits a new card on its
        own to learn whether the mode sets, and committing it again straight
        after meant a second atomic commit queued behind the first one's
        vblank before the first picture was flushed.
*/
static _Bool desktop_commit_known(struct canvas *known, int known_set)
{
        struct canvas *committed = NULL;
        struct output *output;
        _Bool complete = true;

        desktop_attach_buffers();

        list_for_each_entry(output, &desktop.outputs, link)
        {
                int set;

                if (output->canvas == committed)
                        continue;

                committed = output->canvas;
                set = committed == known
                          ? known_set
                          : drm_client_modeset_commit(&committed->client);
                if (!set)
                {
                        struct output *grown;

                        list_for_each_entry(grown, &desktop.outputs, link)
                                if (grown->canvas == committed && grown->replaced)
                                {
                                        drm_client_buffer_delete(grown->replaced);
                                        grown->replaced = NULL;
                                }
                }

                // Somebody else is master, and the card is not ours to draw
                // on until they let go. The loop is woken so it watches for that.
                if (set == -EBUSY || set == -EACCES)
                {
                        desktop.suspended = true;
                        canvas_thread_wake();
                }

                if (set)
                        complete = false;

                // EBUSY is not now: something else is the device's master and
                // the next commit is the one that lands.
                if (set == -EBUSY || set == committed->set_result)
                        continue;

                committed->set_result = set;

                if (set)
                        pr_info("[moonwater canvas] " "%ux%u would not go on the screen (%d)\n", output->width, output->height, set);
                else
                        pr_info("[moonwater canvas] " "%ux%u is on the screen\n", output->width, output->height);
        }

        return complete;
}

static _Bool desktop_commit(void)
{
        return desktop_commit_known(NULL, 0);
}

/*
        Every output filled with the ground and handed to the driver.

        Canvas draws nothing else. The desktop is the compositor's, a program
        that opens the card and becomes its master; what this puts on a
        screen is the picture before it starts, the one a card shows if the
        compositor is stopped or has gone, and the colour it clears to
        (CANVAS_GROUND, the compositor's own default ground) so that the one
        is not a flash of another.

        The general-register fill, never a wide one: the buffer may be device
        memory. The driver is told the whole buffer changed. It is not only for
        drivers that shadow the framebuffer: i915 maps a dumb buffer write-back
        cached and implements dirty as a frontbuffer flush, which is what
        invalidates framebuffer compression and panel self refresh.
*/
static void output_ground(struct output *output)
{
        struct iosys_map map;

        if (!output_map(output, &map))
                return;

        canvas_rect_fill(map.vaddr, output->buffer->fb->pitches[0] / sizeof(u32),
                         output->width, output->height,
                         CANVAS_GROUND | output->opaque);
        drm_client_buffer_vunmap_local(output->buffer);
        drm_client_buffer_flush(output->buffer, NULL);
}

static void desktop_recompose(void)
{
        struct output *output;

        list_for_each_entry(output, &desktop.outputs, link)
                output_ground(output);
}

static void desktop_redraw(void)
{
        desktop_recompose();
        desktop_commit();
}

/*
        The scale the desktop is drawn at, with desktop.lock held, and whether
        it changed.

        Canvas draws nothing that has a size: the number is only what the
        compositor reads when it starts (SPARK_CANVAS_STATUS) and what
        `moonwater canvas` says is in force.
*/
static _Bool desktop_rescale(unsigned int scale)
{
        if (scale == desktop.scale)
                return false;

        desktop.scale = scale;
        return true;
}

// The scale the screens now ask for, when the setting is to ask them.
static _Bool canvas_scale_refresh(void)
{
        unsigned int dpi;

        return !canvas_scale_setting &&
               desktop_rescale(canvas_scale_auto(NULL, &dpi));
}

/*
        Whether a program other than this one is master of a card Canvas draws
        on: Weston, a game, anything that opened the card for itself.

        In-kernel clients never become master, so a device with one has a
        program in front of it. Tested under the device's master_mutex, taken
        inside desktop.lock the way drm_client_modeset_commit already takes
        it, and never dereferenced: the master can go the moment the lock is
        dropped, and all this answers is whether there was one.
*/
static _Bool desktop_taken(void)
{
        struct drm_device *checked = NULL;
        struct output *output;
        _Bool taken = false;

        // The card's power is going, or has not come back: as good as taken.
        if (desktop.asleep)
                return true;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                struct drm_device *dev = output->canvas->client.dev;

                if (dev == checked)
                        continue;

                checked = dev;
                mutex_lock(&dev->master_mutex);
                taken = dev->master != NULL;
                mutex_unlock(&dev->master_mutex);

                if (taken)
                        break;
        }

        return taken;
}

/*
        The card is Canvas's again.

        Nothing Canvas remembers about the screen can be trusted after another
        program had it: the mode on each crtc is theirs, and so may be the
        image. So the ground is drawn and committed, which puts Canvas's modes
        back and disables every plane but the primary.
*/
static void desktop_resume(void)
{
        desktop.suspended = false;

        // Before anything is drawn: nobody has the card now.
        WRITE_ONCE(canvas_yielded, false);

        desktop_redraw();

        pr_info("[moonwater canvas] " "the card is Canvas's again, and the desktop is drawn\n");
}

/*
        Whether the driver's own record has every screen showing what Canvas
        drew, and what it says, said.

        A wake, or a hotplug that changes no size, leaves nothing else to go on:
        the commit that answered zero may have been for a pipe the driver had
        already written off, and a probe that did not grow the mode used to
        commit nothing at all. Read under the device's modeset locks, the order
        drm_client_modeset_commit takes them in after desktop.lock.
*/
static _Bool output_lit_locked(struct output *output)
{
        struct drm_crtc *crtc = output->mode_set ? output->mode_set->crtc : NULL;
        struct drm_plane *primary = crtc ? crtc->primary : NULL;

        return crtc && primary && crtc->state && primary->state &&
               crtc->state->active && crtc->state->enable &&
               primary->state->crtc == crtc && output->buffer &&
               primary->state->fb == output->buffer->fb;
}

static _Bool desktop_lit(void)
{
        struct output *output;
        _Bool lit = true;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                struct drm_device *dev = output->canvas->client.dev;

                drm_modeset_lock_all(dev);
                lit &= output_lit_locked(output);
                drm_modeset_unlock_all(dev);
        }

        return lit;
}

static void desktop_report(const char *tag)
{
        struct output *output;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                struct drm_device *dev = output->canvas->client.dev;
                struct drm_crtc *crtc = output->mode_set->crtc;
                struct drm_connector *connector = output->mode_set->num_connectors
                                                      ? output->mode_set->connectors[0]
                                                      : NULL;
                struct iosys_map map;
                _Bool mapped = output_map(output, &map);
                int active, enable, lit;

                drm_modeset_lock_all(dev);
                active = crtc && crtc->state ? crtc->state->active : -1;
                enable = crtc && crtc->state ? crtc->state->enable : -1;
                lit = output_lit_locked(output);
                drm_modeset_unlock_all(dev);

                if (mapped)
                        drm_client_buffer_vunmap_local(output->buffer);

                pr_info("[moonwater canvas] " "%s: %s: crtc active %d enabled %d, %s, connector status %d dpms %d, buffer %s, last commit %d\n",
                        tag, connector && connector->name ? connector->name : "?",
                        active, enable, lit ? "primary shows ours" : "primary does NOT show ours",
                        connector ? (int)connector->status : -1,
                        connector ? connector->dpms : -1,
                        mapped ? (map.is_iomem ? "maps (device memory)" : "maps") : "does NOT map",
                        output->canvas->set_result);
        }
}

/*
        Every screen off or on through the client's own modesets, one card at a
        time: an active-false commit, and the on that follows it is an
        active-changed one, which is the only kind a driver whose idea of the
        pipe has drifted from the hardware's cannot skip.
*/
static void desktop_dpms(int mode)
{
        struct canvas *done = NULL;
        struct output *output;

        desktop_attach_buffers();

        list_for_each_entry(output, &desktop.outputs, link)
        {
                int ret;

                if (output->canvas == done)
                        continue;

                done = output->canvas;
                ret = drm_client_modeset_dpms(&done->client, mode);
                pr_info("[moonwater canvas] " "screens %s: %d\n", mode == DRM_MODE_DPMS_ON ? "on" : "off", ret);
        }
}

static COLD void canvas_release(struct canvas *canvas);

static int canvas_build(struct canvas *canvas, _Bool biggest)
{
        struct drm_client_dev *client = &canvas->client;
        struct drm_mode_set *mode_set;
        unsigned int count = 0;

        if (canvas_probe_modes(canvas, biggest, true))
                return -ENODEV;

        mutex_lock(&client->modeset_mutex);
        desktop.scale = canvas_scale_wanted(client);

        drm_client_for_each_modeset(mode_set, client)
        {
                if (!mode_set->mode)
                        continue;

                if (output_bring_up(canvas, mode_set))
                        count++;
        }
        mutex_unlock(&client->modeset_mutex);

        if (!count)
                return -ENODEV;

        desktop_place_outputs();

        return 0;
}

/*
        The biggest mode every screen offers, and what to do when it will not
        set.

        A monitor listing a mode is not a promise the link can carry it,
        especially with more than one screen sharing the bandwidth, so a
        refused commit falls back to the mode the probe would have chosen --
        which is the one that used to be taken unconditionally. A guest is
        already capped: retrying still asks for seventy percent of the host,
        not the probe's native size.
*/
static int canvas_start(struct canvas *canvas)
{
        /*
                A refused commit means the mode, not the moment.

                The claim file is closed before drm_client_register, and this
                runs from moonwater/plug after that, so a commit is not EBUSY
                because we still hold master. EBUSY here is another program.
                Falling back on that threw away every mode this ever chose and
                quietly took the probe's, which is the opposite of the point.
        */
        int ret;

        for (unsigned int attempt = 0; ; attempt++)
        {
                ret = canvas_build(canvas, !attempt && READ_ONCE(canvas_modes_largest));

                if (ret)
                {
                        pr_err("[moonwater canvas] " "no screen to draw on (%d), leaving the display alone\n", ret);
                        return ret;
                }

                desktop_attach_buffers();
                ret = drm_client_modeset_commit(&canvas->client);
                if (!ret || ret == -EBUSY)
                        break;

                // A rejected mode owns no useful picture. Release its outputs
                // before retrying, or standing aside for the console client.
                if (attempt)
                        pr_err("[moonwater canvas] " "no mode would set (%d), leaving the display alone\n", ret);
                else
                        pr_info("[moonwater canvas] " "that mode would not set (%d), taking the offered one\n", ret);
                canvas_release(canvas);
                if (attempt)
                        return ret;
        }

        canvas_modes_keep();

        // The ground, less the commit this card has just had: the picture
        // goes to the driver and the answer above is what is reported.
        desktop_recompose();
        desktop_commit_known(canvas, ret);

        {
                struct output *output;
                unsigned int count = 0;

                list_for_each_entry(output, &desktop.outputs, link)
                        count++;

                pr_info("[moonwater canvas] " "desktop %dx%d, %u output(s)\n", desktop.width, desktop.height, count);
        }

        return 0;
}

static COLD void canvas_release(struct canvas *canvas)
{
        struct output *output, *next;

        list_for_each_entry_safe(output, next, &desktop.outputs, link)
                if (output->canvas == canvas)
                        output_drop(output);

        desktop_place_outputs();

        if (!list_empty(&desktop.outputs))
                desktop_redraw();
}

/* ---- keys: the keyboard ---- */

/*
        Canvas -- keys

        Canvas hears the keyboard for two chords and nothing else. What a
        keyboard means -- the keymap, which window has the keys -- is the
        compositor's, a program that reads the same devices itself.

        Control-Alt-Backspace ends a program that holds the display and will
        not give it back, and Control-Alt-Shift-Backspace turns Canvas off to
        the text console. Both are made of the modifiers, which are tracked
        here, and Backspace.
*/

// Wide enough for every modifier key, and for Backspace.
#define KEY_TABLE 128

// The modifiers the chords are made of.
#define CHORD_SHIFT 2u
#define CHORD_CONTROL 4u
#define CHORD_ALT 8u
#define CHORD_ALTGR 64u

/*
        Which modifier a key is, in the held word's own spelling.

        A right-hand key is its left-hand flag moved up four bits, so
        keyboard_modifiers can put the side back by shifting down and one word
        carries both hands. AltGr is the one that does not fit: CHORD_CONTROL
        << 4 is 64 and CHORD_ALTGR is 64, so right Alt gets a bit of its own,
        above everything the shift can reach, and keyboard_modifiers spells it
        as CHORD_ALTGR on the way out. AltGr is not the other side of Alt: the
        chords are made of the left Alt only.
*/
#define KEY_HELD_ALTGR 256u

static unsigned short key_mod[KEY_TABLE] = {
    [KEY_LEFTSHIFT] = CHORD_SHIFT,
    [KEY_RIGHTSHIFT] = CHORD_SHIFT << 4,
    [KEY_LEFTCTRL] = CHORD_CONTROL,
    [KEY_RIGHTCTRL] = CHORD_CONTROL << 4,
    [KEY_LEFTALT] = CHORD_ALT,
    [KEY_RIGHTALT] = KEY_HELD_ALTGR,
};

// The input handler owns each device's held bits and the list to combine.
static unsigned int *keyboard_held(struct input_handle *handle);
static unsigned int keyboard_modifiers(void);

/*
        Control-Alt-Backspace ends a program that has the display and will not
        give it back.

        A compositor with no input, or no picture, or neither, holds the card
        against Canvas and the text console: nothing typed gets the machine
        out. This one is noticed here and acted on by the thread, which does
        it only when somebody holds the card.
*/
static void keyboard_reclaim(unsigned int modifiers, unsigned int code, int value)
{
        if ((modifiers & (CHORD_ALT | CHORD_SHIFT |
                          CHORD_CONTROL)) !=
                (CHORD_ALT | CHORD_CONTROL) ||
            code != KEY_BACKSPACE || value != 1)
                return;

        atomic_set(&desktop.reclaim, 1);
        canvas_thread_wake();
}

/*
        Control-Alt-Shift-Backspace turns Canvas off, to the kernel's text
        console, whoever holds the card.

        Control-Alt-Backspace ends the program that holds it, and a supervisor
        starts the next; this is for a desktop that will not stay up. It is
        heard here, in the input core, and never passes through the compositor
        or the canvas thread, so a compositor that has stopped answering
        cannot swallow it. Turning Canvas off stops the thread and waits on
        locks, so a work item does it.
*/
static void canvas_exit_request(void);

static _Bool keyboard_exit(unsigned int modifiers, unsigned int code, int value)
{
        if ((modifiers & (CHORD_ALT | CHORD_SHIFT |
                          CHORD_CONTROL)) !=
                (CHORD_ALT | CHORD_SHIFT | CHORD_CONTROL) ||
            code != KEY_BACKSPACE)
                return false;

        if (value == 1)
                canvas_exit_request();

        return true;
}

/*
        Called by the input core, so no lock and no sleeping. It records what
        happened and the thread acts: whatever the chord asks takes locks and
        sleeps.
*/
static void keyboard_event(struct input_handle *handle, unsigned int code, int value)
{
        unsigned int modifiers;
        unsigned int bit;

        modifiers = (unsigned int)atomic_read(&desktop.modifiers);
        bit = code < KEY_TABLE ? key_mod[code] : 0;

        if (bit)
        {
                unsigned int *held = keyboard_held(handle);
                *held = value ? *held | bit : *held & ~bit;
                atomic_set(&desktop.modifiers, (int)keyboard_modifiers());
                return;
        }

        // Before anything is asked of a program: a compositor that hangs
        // holds the card and every key, and this is the way out.
        if (keyboard_exit(modifiers, code, value))
                return;

        keyboard_reclaim(modifiers, code, value);
}

/* ---- client: a client, and what it is allowed to ask for ---- */

/*
        Canvas -- attaching to DRM

        Opening the node runs the driver's open path and hands back a
        drm_file, which knows its minor, which knows its device. The file is
        closed immediately; drm_client_init takes its own reference.

        Every card is taken, not just the first. The poll is here because the
        nodes appear when devtmpfs is mounted, after the initcalls that could
        otherwise have started this.
*/

/*
        The outputs come off the desktop before the card they belong to is
        released, or output->canvas dangles for anything still composing.
        canvas_thread_stop joins a thread that takes desktop.lock, so it runs under
        canvas_list_lock and never under desktop.lock. The plug work is cancelled
        first, so it cannot start or rebind a card that is leaving.
*/
static struct workqueue_struct *canvas_plug_wq;

static void canvas_claimed_forget(struct drm_device *dev);

static void canvas_plug_ensure(void)
{
        if (!canvas_plug_wq)
                canvas_plug_wq = alloc_ordered_workqueue("moonwater/plug", 0);
}

static void canvas_plug_work(struct work_struct *work)
{
        struct canvas *canvas = container_of(work, struct canvas, plug);
        int ret = 0;

        rt_mutex_lock(&desktop.lock);

        /*
                Not while the card sleeps.

                A driver reports a connector as it comes back from suspend
                (amdgpu's resume ends with a hotplug event, and the DRM core
                hands it to a client only when that client's resume returns),
                and a rebind commits. The commit belongs after the wake has
                redrawn, not in the middle of the driver's own resume.
        */
        if (canvas->started && READ_ONCE(desktop.asleep))
        {
                canvas->replug = true;
                rt_mutex_unlock(&desktop.lock);
                return;
        }

        if (!canvas->started)
        {
                ret = canvas_start(canvas);
                canvas->started = (ret == 0);
        }
        else
                ret = canvas_rebind(canvas);

        rt_mutex_unlock(&desktop.lock);
        (void)ret;
}

static COLD void client_unregister(struct drm_client_dev *client)
{
        struct canvas *canvas = canvas_from_client(client);

        canvas_claimed_forget(client->dev);

        mutex_lock(&canvas_list_lock);
        list_del(&canvas->link);
        cancel_work_sync(&canvas->plug);
        if (list_empty(&canvas_list))
                canvas_thread_stop();
        mutex_unlock(&canvas_list_lock);

        rt_mutex_lock(&desktop.lock);
        canvas->started = 0;
        canvas_release(canvas);
        rt_mutex_unlock(&desktop.lock);

        drm_client_release(client);
}

static COLD void client_free(struct drm_client_dev *client)
{
        kfree(canvas_from_client(client));
}

static int client_hotplug(struct drm_client_dev *client)
{
        struct canvas *canvas = canvas_from_client(client);

        /*
                Never start or rebind on the caller's workqueue.

                drm_client_register calls this itself, which is fine, but a
                later connector hotplug runs on the DRM helper workqueue. A
                modeset commit from there disables every cursor plane and can
                wait on that same queue: on i915 that is a frozen low-res
                kernel log and no pointer. Queue on moonwater/plug, which
                nothing in DRM flushes.
        */
        canvas_plug_ensure();
        queue_work(canvas_plug_wq ? canvas_plug_wq : system_unbound_wq,
                   &canvas->plug);
        return 0;
}

// The bool argument is whether the restore happens from an atomic context.
static int client_restore(struct drm_client_dev *client, _Bool in_atomic)
{
        struct canvas *canvas = canvas_from_client(client);

        if (in_atomic)
                return -EBUSY;

        rt_mutex_lock(&desktop.lock);
        // The wake's own redraw is the restore; a commit now is the one into
        // a card whose power is going or has not come back.
        if (canvas->started && !READ_ONCE(desktop.asleep))
        {
                /*
                        The last program that held the card has closed it.

                        Drawn again when the thread had noticed the card was
                        another program's, and otherwise only where the screen
                        does not show Canvas's picture: a second commit is a
                        modeset under desktop.lock on the exit of the program
                        that closed the card -- 49 ms on the parent after a
                        SIGKILL, the compositor's next start waiting behind it.
                */
                if (desktop.suspended || !desktop_lit())
                        desktop_resume();
        }
        rt_mutex_unlock(&desktop.lock);

        return 0;
}

static const struct drm_client_funcs client_funcs = {
    .owner = THIS_MODULE,
    .unregister = client_unregister,
    .free = client_free,
    .restore = client_restore,
    .hotplug = client_hotplug,
};

static _Bool canvas_holds(struct drm_device *dev)
{
        struct canvas *canvas;
        _Bool held = false;

        mutex_lock(&canvas_list_lock);
        list_for_each_entry(canvas, &canvas_list, link)
        {
                if (canvas->client.dev == dev)
                {
                        held = true;
                        break;
                }
        }
        mutex_unlock(&canvas_list_lock);

        return held;
}

/*
        Everything but the registering, which is deliberately not done here:
        registering fires the hotplug that chooses a mode and commits it, and
        that has to happen with no file open on the card. See canvas_claim.
*/
static struct canvas *canvas_take_over(struct drm_device *dev)
{
        struct canvas *canvas;
        _Bool first;

        if (!drm_core_check_feature(dev, DRIVER_MODESET) || canvas_holds(dev))
                return NULL;

        canvas = kzalloc(sizeof(*canvas), GFP_KERNEL);
        if (!canvas)
                return NULL;

        canvas->set_result = 1;

        if (drm_client_init(dev, &canvas->client, "moonwater", &client_funcs))
        {
                kfree(canvas);
                return NULL;
        }

        canvas_plug_ensure();
        INIT_WORK(&canvas->plug, canvas_plug_work);

        mutex_lock(&canvas_list_lock);
        first = list_empty(&canvas_list);
        list_add_tail(&canvas->link, &canvas_list);
        if (first)
                canvas_thread_start();
        mutex_unlock(&canvas_list_lock);

        return canvas;
}

// Retried fast: the node appears the moment devtmpfs is mounted, and every
// millisecond spent waiting after that is a millisecond of black screen.
#define CANVAS_RETRY_MS 5
#define CANVAS_ATTEMPTS 1000

// Rounds to keep looking after the first card, so a sibling that probes late
// is found too.
//
// A hundred rounds at CANVAS_RETRY_MS is half a second of polling after the
// screen is already up, which reads like an obvious thing to cut. It is not:
// measured at 10 rounds against 100, moonwater starts, the canvas is drawn
// and the shell runs at the same times to within the noise of five boots
// each. The poll is a delayed work item that sleeps between rounds, so what
// it costs the boot is not the half second it spans. Cutting it would buy
// nothing and lose the late sibling it is here for.
#define CANVAS_SETTLE 100

/*
        What turning Canvas on needs from a card before it is claimed.
*/
/* Every in-kernel client on a card but Canvas's, as drm_client_dev_unregister takes them. */
static void canvas_clients_clear(struct drm_device *dev)
{
        struct drm_client_dev *client, *next;

        mutex_lock(&dev->clientlist_mutex);
        list_for_each_entry_safe(client, next, &dev->clientlist, list)
        {
                if (client->funcs == &client_funcs)
                        continue;

                // Unregistering consumes and frees the client.
                list_del(&client->list);
                if (client->funcs && client->funcs->unregister)
                        client->funcs->unregister(client);
                else
                        drm_client_release(client);
        }
        mutex_unlock(&dev->clientlist_mutex);
}

/* Which program is master of a card, for a refusal to say. */
static int canvas_master_holder(struct drm_device *dev, char *command, size_t room)
{
        struct drm_file *file;
        int holder = 0;

        mutex_lock(&dev->filelist_mutex);
        list_for_each_entry(file, &dev->filelist, lhead)
        {
                struct task_struct *task;

                if (!drm_is_current_master(file))
                        continue;

                rcu_read_lock();
                task = pid_task(rcu_dereference(file->pid), PIDTYPE_TGID);
                if (task)
                {
                        // In the asker's pid namespace, where a number the
                        // asker can kill means something; 0 is a holder it
                        // cannot see. A program names itself with prctl, so
                        // only printable bytes go to the asker's terminal.
                        holder = task_tgid_nr_ns(task, task_active_pid_ns(current));
                        strscpy(command, task->comm, room);
                        for (char *at = command; *at; at++)
                                if (*at < ' ' || *at > '~')
                                        *at = '?';
                }
                rcu_read_unlock();
                break;
        }
        mutex_unlock(&dev->filelist_mutex);

        return holder;
}

static struct delayed_work canvas_probe_work;
static unsigned int canvas_attempts;
static unsigned int canvas_settled_at;

/*
        Which primary nodes are already ours.

        Not an optimisation. Opening a node we already hold and closing it
        again is a client releasing the device as far as DRM is concerned, and
        it answers by telling every client to restore -- a full repaint of
        every screen. The poll below runs for a hundred rounds, so booting
        cost a hundred and one full redraws, about two hundred milliseconds
        of drawing nobody asked for.

        One bit per minor, which is the whole of DRM's minor space.
*/
static u64 canvas_claimed;

static void canvas_firmware_yield(void);

static void canvas_claimed_forget(struct drm_device *dev)
{
        int minor;

        if (!dev || !dev->primary)
                return;

        minor = dev->primary->index;
        if (minor >= 0 && minor < 64)
                canvas_claimed &= ~BIT_ULL(minor);
}

static _Bool canvas_has_native(void)
{
        struct canvas *canvas;
        _Bool native = false;

        mutex_lock(&canvas_list_lock);
        list_for_each_entry(canvas, &canvas_list, link)
                if (!canvas_is_firmware(canvas->client.dev))
                {
                        native = true;
                        break;
                }
        mutex_unlock(&canvas_list_lock);

        return native;
}

/*
        Close a node this code opened, and have it gone before returning.

        A plain close from a program's ioctl puts the last fput on the task's
        work list, which runs as the call returns to the program, and on a
        kernel thread on a worker a jiffy later. Until it runs, the file is
        still open on the card, and the first program to open a card is its
        master. So a refused pass held the card against the next pass of the
        same call: with a firmware card as the only one, the first pass opens
        and skips it, the second opens it again, finds the first still master
        and the refusal names the caller itself -- a shell that is gone by the
        time anyone looks, and a different one on every retry. And the first
        commit of a card taken from a kernel thread lost the same race, so
        the desktop began suspended. This is the only reference, so the last
        put is done here.
*/
static void canvas_node_close(struct file *filp)
{
        __fput_sync(filp);
}

static int canvas_claim(const char *path, unsigned int minor,
                        struct canvas_control *on, _Bool firmware_ok)
{
        struct file *filp;
        struct drm_file *file_priv;
        struct canvas *canvas;
        struct drm_device *dev;

        if (canvas_claimed & BIT_ULL(minor))
                return -EBUSY;

        filp = filp_open(path, O_RDWR, 0);
        if (IS_ERR(filp))
                return PTR_ERR(filp);

        file_priv = filp->private_data;

        if (!file_priv || !file_priv->minor || !file_priv->minor->dev)
        {
                canvas_node_close(filp);
                return -ENODEV;
        }

        /*
                simpledrm is card0 on a Dell with i915: GOP's leftover
                size, often 1024x768. Starting there paints the kernel
                log, then i915 takes the same pipe and the machine
                freezes. Leave it until a GPU appears, or until the
                settle rounds have passed with none. Userspace on still
                takes it when it is the only card -- the first pass of
                claim_all skips firmware so card0 cannot start before
                card1 is tried.
        */
        dev = file_priv->minor->dev;
        if (canvas_is_firmware(dev))
        {
                int skip = 0;

                if (!firmware_ok)
                        skip = -EAGAIN;
                else if (canvas_has_native())
                        skip = -ENODEV;
                else if (!on && canvas_attempts < CANVAS_SETTLE)
                        skip = -EAGAIN;

                if (skip)
                {
                        canvas_node_close(filp);
                        return skip;
                }
        }

        /*
                Turned on from userspace, a card is taken only from nobody.

                This open is the card's master unless another program already
                is, and a Canvas started behind that program would only sit
                suspended, so the refusal names it instead. The kernel
                console's client, which off left on the card, goes while this
                file is still the master, so the close below restores nothing.
        */
        if (on && drm_core_check_feature(dev, DRIVER_MODESET))
        {
                if (!drm_is_current_master(file_priv))
                {
                        if (!on->master_pid && !on->master_command[0])
                                on->master_pid = canvas_master_holder(
                                    dev, on->master_command,
                                    sizeof(on->master_command));
                        canvas_node_close(filp);
                        return -EACCES;
                }

                canvas_clients_clear(dev);
        }

        canvas = canvas_take_over(dev);

        if (canvas)
                canvas_claimed |= BIT_ULL(minor);

        /*
                Closed before the client is registered, and drm_client_init has
                taken its own reference to the device by now.

                Registering fires the hotplug that picks a mode and commits it,
                and a commit is refused out of hand while anything else is the
                device's master -- which this file is until it is closed. With
                it still open the first commit always answered EBUSY, so
                "that mode would not set, take the offered one" could never
                run: the one answer it was written to read was the one answer
                it could never get.
        */
        canvas_node_close(filp);

        if (!canvas)
                return -EBUSY;

        drm_client_register(&canvas->client);
        pr_info("[moonwater canvas] " "attached to %s, %s display\n", canvas->client.dev->driver->name, canvas_is_virtual(canvas->client.dev) ? "a guest's" : "a real");

        if (!canvas_is_firmware(canvas->client.dev))
                canvas_firmware_yield();

        return 0;
}

/*
        Every primary node, every round. DRM allocates card minors out of an
        idr with no promise they are contiguous, and a card that probes late
        would be missed by a scan that stopped at the first gap. The whole
        minor space is cheap to try: an absent node fails in filp_open.
*/
static unsigned int canvas_claim_range(struct canvas_control *on,
                                       unsigned int *refused,
                                       _Bool firmware_ok)
{
        char path[24];
        unsigned int minor, taken = 0;

        for (minor = 0; minor < 64; minor++)
        {
                if (canvas_claimed & BIT_ULL(minor))
                        continue;

                positive_into_string(
                    memory_copy_apart_end(path, "/dev/dri/card",
                                         sizeof("/dev/dri/card") - 1),
                    minor);

                switch (canvas_claim(path, minor, on, firmware_ok))
                {
                case 0:
                        taken++;
                        break;
                case -EACCES:
                        if (refused)
                                (*refused)++;
                        break;
                }
        }

        return taken;
}

static unsigned int canvas_claim_all(struct canvas_control *on,
                                     unsigned int *refused)
{
        unsigned int taken = canvas_claim_range(on, refused, false);

        /* Firmware last: card0 is often simpledrm, and starting it
           before i915 is the freeze. If nothing else attached, take it. */
        if (!taken)
                taken = canvas_claim_range(on, refused, true);

        return taken;
}

static COLD void canvas_probe(struct work_struct *work)
{
        _Bool was_on = canvas_is_on();

        canvas_claim_all(NULL, NULL);
        if (!was_on && canvas_is_on())
                moonwater_bind_fire(SPARK_BIND_CANVAS_ON);
        canvas_attempts++;

        if (!canvas_settled_at && !list_empty(&canvas_list))
                canvas_settled_at = canvas_attempts + CANVAS_SETTLE;

        if (canvas_settled_at && canvas_attempts >= canvas_settled_at)
                return;

        if (canvas_attempts >= CANVAS_ATTEMPTS)
        {
                pr_info("[moonwater canvas] " "gave up waiting for a card\n");
                return;
        }

        schedule_delayed_work(&canvas_probe_work, msecs_to_jiffies(CANVAS_RETRY_MS));
}

static void __maybe_unused canvas_start_probing(void)
{
        schedule_delayed_work(&canvas_probe_work, 0);
}

/*
        Canvas, off and on, from userspace.

        Off undoes what attaching did, in this order: the input handler and
        the thread go, which gives every console its keyboard back; each
        card's client is released; and the kernel's own framebuffer console
        is set up on each card, which drm_client_lib.active= kept from ever
        having one. The machine script ends the compositor on the event. On
        claims the cards again the way the boot does and fires canvas on, and
        the machine script starts the compositor again.

        Lock order: canvas_control_lock, then a card's clientlist_mutex, then
        canvas_list_lock, then desktop.lock, then a card's master_mutex.
        canvas_thread_stop joins a thread that takes desktop.lock, so it is
        called under canvas_list_lock and never under desktop.lock.
*/
static DEFINE_MUTEX(canvas_control_lock);

_Bool canvas_is_on(void)
{
        _Bool on;

        mutex_lock(&canvas_list_lock);
        on = !list_empty(&canvas_list);
        mutex_unlock(&canvas_list_lock);

        return on;
}

/*
        One card's client off DRM's list and released, unless the card is
        going away at this moment and its own unregister already has it.
*/
static void canvas_client_drop(struct drm_device *dev)
{
        struct drm_client_dev *client, *next;

        mutex_lock(&dev->clientlist_mutex);
        list_for_each_entry_safe(client, next, &dev->clientlist, list)
        {
                if (client->funcs != &client_funcs)
                        continue;
                list_del(&client->list);
                client_unregister(client);
        }
        mutex_unlock(&dev->clientlist_mutex);
}

/*
        A GPU is attached: the firmware framebuffer is not a second
        screen, it is the same pipe. Leaving a client on it is what
        froze the OptiPlex -- i915 taking that pipe with a picture
        still scanning from simpledrm.
*/
static void canvas_firmware_yield(void)
{
        for (;;)
        {
                struct canvas *canvas;
                struct drm_device *dev = NULL;

                mutex_lock(&canvas_list_lock);
                list_for_each_entry(canvas, &canvas_list, link)
                        if (canvas_is_firmware(canvas->client.dev))
                        {
                                /* The device only: DRM can unregister this
                                   client between the unlock and the drop. */
                                dev = canvas->client.dev;
                                drm_dev_get(dev);
                                break;
                        }
                mutex_unlock(&canvas_list_lock);

                if (!dev)
                        return;

                pr_info("[moonwater canvas] " "dropping %s, a GPU is attached\n",
                        dev->driver->name);
                canvas_client_drop(dev);
                drm_dev_put(dev);
        }
}

static long canvas_turn_off(void)
{
        struct drm_device *released[8];
        unsigned int count = 0;

        mutex_lock(&canvas_control_lock);

        if (!canvas_is_on())
        {
                mutex_unlock(&canvas_control_lock);
                return -EALREADY;
        }

#ifdef CONFIG_MOONWATER_CANVAS_AUTOSTART
        cancel_delayed_work_sync(&canvas_probe_work);
#endif

        // Input and the thread before any output goes, so nothing draws on
        // an output being released.
        mutex_lock(&canvas_list_lock);
        atomic_set(&desktop.reclaim, 0);
        canvas_thread_stop();
        mutex_unlock(&canvas_list_lock);

        rt_mutex_lock(&desktop.lock);
        desktop.suspended = false;
        WRITE_ONCE(canvas_yielded, false);
        rt_mutex_unlock(&desktop.lock);

        for (;;)
        {
                struct canvas *canvas;
                struct drm_device *dev = NULL;

                mutex_lock(&canvas_list_lock);
                canvas = list_first_entry_or_null(&canvas_list, struct canvas, link);
                if (canvas)
                {
                        dev = canvas->client.dev;
                        drm_dev_get(dev);
                }
                mutex_unlock(&canvas_list_lock);

                if (!canvas)
                        break;

                canvas_client_drop(dev);

                if (count < ARRAY_SIZE(released))
                        released[count++] = dev;
                else
                        drm_dev_put(dev);
        }

        canvas_claimed = 0;

        // The kernel's console takes each screen back.
        for (unsigned int i = 0; i < count; i++)
        {
#ifndef MODULE
                if (released[i]->registered && !released[i]->fb_helper)
                        drm_fbdev_client_setup(released[i], NULL);
#endif
                drm_dev_put(released[i]);
        }

        pr_info("[moonwater canvas] " "off: %u card(s) given back to the console\n", count);

        mutex_unlock(&canvas_control_lock);
        moonwater_bind_fire(SPARK_BIND_CANVAS_OFF);
        return 0;
}

static long canvas_turn_on(struct canvas_control *answer)
{
        unsigned int taken, refused = 0;

        mutex_lock(&canvas_control_lock);

        if (canvas_is_on())
        {
                mutex_unlock(&canvas_control_lock);
                return -EALREADY;
        }

#ifdef CONFIG_MOONWATER_CANVAS_AUTOSTART
        cancel_delayed_work_sync(&canvas_probe_work);
#endif

        taken = canvas_claim_all(answer, &refused);

        mutex_unlock(&canvas_control_lock);

        if (taken)
        {
                moonwater_bind_fire(SPARK_BIND_CANVAS_ON);
                return 0;
        }

        return refused ? -EBUSY : -ENODEV;
}

/* What Canvas holds, for `moonwater canvas`. */
static void canvas_state(struct canvas_control *answer)
{
        struct canvas *canvas;
        struct output *output;

        mutex_lock(&canvas_list_lock);
        list_for_each_entry(canvas, &canvas_list, link)
                if (!answer->cards++)
                        strscpy(answer->driver, canvas->client.dev->driver->name,
                                sizeof(answer->driver));
        mutex_unlock(&canvas_list_lock);

        answer->running = answer->cards != 0;

        rt_mutex_lock(&desktop.lock);

        answer->suspended = desktop.suspended;
        answer->thread_passes = READ_ONCE(canvas_passes);

        answer->scale = desktop.scale;
        answer->scale_setting = canvas_scale_setting;
        answer->scale_origin = canvas_scale_origin;
        (void)canvas_scale_auto(NULL, &answer->scale_dpi);
        answer->modes = READ_ONCE(canvas_modes_largest)
                            ? SPARK_CANVAS_MODES_LARGEST
                            : SPARK_CANVAS_MODES_PREFERRED;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                struct canvas_output_state *state;
                struct drm_mode_set *set = output->mode_set;

                if (answer->output_count == SPARK_CANVAS_OUTPUTS)
                        break;

                state = &answer->output[answer->output_count++];
                state->width = output->width;
                state->height = output->height;

                if (set && set->mode)
                        state->refresh = drm_mode_vrefresh(set->mode);
                if (set && set->num_connectors && set->connectors[0])
                        strscpy(state->connector, set->connectors[0]->name,
                                sizeof(state->connector));
        }

        rt_mutex_unlock(&desktop.lock);
}

/*
        The scale and which mode to take, changed while the desktop is up.

        Both are the machine's setting first and the desktop's second: they
        are kept with Canvas off and taken when it starts, so the answer is
        the same either way. A scale is the number the compositor reads when
        it starts, so one that is already running keeps what it read, and one
        number or auto is all there is to say. A mode is the hotplug's
        business, which is where a card is looked at again, so each card is
        told it has one; a screen that is a window on another one's has no
        mode of its own and takes none.
*/
static long canvas_scale_set(unsigned int setting)
{
        unsigned int dpi;

        if (setting > SPARK_CANVAS_SCALE_MOST)
                return -EINVAL;

        guard(rt_mutex)(&desktop.lock);

        canvas_scale_setting = setting;
        canvas_scale_origin = SPARK_CANVAS_ORIGIN_SET;

        if (!list_empty(&desktop.outputs))
                desktop_rescale(setting ? setting : canvas_scale_auto(NULL, &dpi));

        return 0;
}

static long canvas_modes_set(unsigned int modes)
{
        struct canvas *canvas;

        if (modes > SPARK_CANVAS_MODES_PREFERRED)
                return -EINVAL;

        if (READ_ONCE(canvas_modes_largest) == (modes == SPARK_CANVAS_MODES_LARGEST))
                return 0;

        WRITE_ONCE(canvas_modes_largest, modes == SPARK_CANVAS_MODES_LARGEST);

        mutex_lock(&canvas_list_lock);
        list_for_each_entry(canvas, &canvas_list, link)
        {
                canvas->retarget = true;
                (void)client_hotplug(&canvas->client);
        }
        mutex_unlock(&canvas_list_lock);

        return 0;
}

/* ---- pointer: the pointer ---- */

/*
        Canvas -- input

        The handler below is called by the input core directly, in the same
        path that received the event, for every report of every device with
        keys. It hears two chords and records them; the thread acts on them,
        because what they ask takes locks and sleeps. The pointer, the
        keyboard and everything else are the compositor's, which opens the
        same devices itself.
*/

static struct task_struct __rcu *canvas_thread;

static _Bool pointer_handler_registered;

/*
        One attached device, with the modifier keys it holds.

        The list is kept here under the input lock rather than read out of
        the input core, whose handle list has a lock of its own.

        An open that fails is tried again. The input core drops a device a
        connect refuses, and a keyboard the BIOS was still driving when the
        kernel took the controller can refuse its first open and accept the
        next; a device that stays refused is logged and left.
*/
#define POINTER_OPEN_TRIES 30

struct pointer_handle
{
        struct input_handle handle;
        struct list_head link;
        struct delayed_work reopen;
        int opened;
        unsigned int tries;
        unsigned int modifiers;
};

static LIST_HEAD(pointer_handles);

static inline struct pointer_handle *pointer_handle_of(struct input_handle *handle)
{
        return container_of(handle, struct pointer_handle, handle);
}

static unsigned int *keyboard_held(struct input_handle *handle)
{
        return &pointer_handle_of(handle)->modifiers;
}

static unsigned int keyboard_modifiers(void)
{
        struct pointer_handle *pointer;
        unsigned int held = 0;

        list_for_each_entry(pointer, &pointer_handles, link)
                held |= pointer->modifiers;
        return ((held | (held >> 4)) &
                (CHORD_SHIFT | CHORD_CONTROL | CHORD_ALT)) |
               ((held & KEY_HELD_ALTGR) ? CHORD_ALTGR : 0);
}

/*
        What Canvas hears: modifiers, which the chords that end a program or
        Canvas are made of, and Backspace, which completes them. Every other
        report is the compositor's, which reads the same devices itself and
        loses nothing by this handler saying nothing, and which a handler that
        did a good deal with each one would be slowed by: every report of every
        device goes through every handler. A key that is no part of a chord
        takes no lock either.
*/
static _Bool pointer_hears(unsigned int type, unsigned int code)
{
        return type == EV_KEY && code < KEY_TABLE &&
               (key_mod[code] || code == KEY_BACKSPACE);
}

static HOT void pointer_event_run(struct input_handle *handle, unsigned int type,
                                  unsigned int code, int value)
{
        unsigned long flags;

        if (!pointer_hears(type, code))
        {
                __this_cpu_inc(canvas_quiet);
                return;
        }

        spin_lock_irqsave(&desktop.input_lock, flags);
        keyboard_event(handle, code, value);
        spin_unlock_irqrestore(&desktop.input_lock, flags);
}

// Counted and, one in sixteen, timed: what `moonwater latency` shows.
static HOT void pointer_event(struct input_handle *handle, unsigned int type,
                              unsigned int code, int value)
{
        u64 started = moonwater_cost_begin(&canvas_cost);

        pointer_event_run(handle, type, code, value);
        moonwater_cost_end(&canvas_cost, started);
}

static const char *pointer_device_name(struct input_handle *handle)
{
        return handle->dev->name ? handle->dev->name : "unnamed";
}

static void pointer_reopen(struct work_struct *work)
{
        struct pointer_handle *pointer =
            container_of(to_delayed_work(work), struct pointer_handle, reopen);
        int ret = input_open_device(&pointer->handle);

        pointer->opened = ret;

        if (!ret)
        {
                if (pointer->tries)
                        pr_info("[moonwater canvas] " "input: %s opened on try %u\n", pointer_device_name(&pointer->handle), pointer->tries + 1);
                else
                        pr_info("[moonwater canvas] " "input: %s\n", pointer_device_name(&pointer->handle));
                return;
        }

        if (!pointer->tries)
                pr_info("[moonwater canvas] " "input: %s would not open (%d), trying again\n", pointer_device_name(&pointer->handle), ret);

        if (++pointer->tries < POINTER_OPEN_TRIES)
                schedule_delayed_work(&pointer->reopen, HZ);
        else
                pr_info("[moonwater canvas] " "input: %s would not open (%d), given up\n", pointer_device_name(&pointer->handle), ret);
}

static COLD int pointer_connect(struct input_handler *handler,
                                struct input_dev *dev,
                                const struct input_device_id *id)
{
        struct pointer_handle *pointer;
        unsigned long flags;
        int ret;

        pointer = kzalloc(sizeof(*pointer), GFP_KERNEL);
        if (!pointer)
                return -ENOMEM;

        pointer->handle.dev = dev;
        pointer->handle.handler = handler;
        pointer->handle.name = "moonwater";
        INIT_DELAYED_WORK(&pointer->reopen, pointer_reopen);

        ret = input_register_handle(&pointer->handle);
        if (ret)
        {
                kfree(pointer);
                return ret;
        }

        spin_lock_irqsave(&desktop.input_lock, flags);
        list_add_tail(&pointer->link, &pointer_handles);
        spin_unlock_irqrestore(&desktop.input_lock, flags);

        /*
                Opened from a worker, not here.

                A connect runs inside the bus probe that registered the
                device, with the input core's mutex held, and opening a USB
                HID device sleeps fifty milliseconds in usbhid_open before it
                returns. Done here, that sleep sat in front of every device
                after this one on the same hub -- a mouse enumerated first
                held the keyboard back by it, and a keyboard with a media-key
                interface paid it twice -- and in front of every other input
                device registering anywhere. Nothing is lost by the worker: a
                device delivers nothing until it is open either way.
        */
        pointer->opened = -EINPROGRESS;
        schedule_delayed_work(&pointer->reopen, 0);

        return 0;
}

static COLD void pointer_disconnect(struct input_handle *handle)
{
        struct pointer_handle *pointer = pointer_handle_of(handle);
        unsigned long flags;

        cancel_delayed_work_sync(&pointer->reopen);

        // Stop callbacks before releasing held keys; no late press may
        // resurrect a modifier after this device leaves the list.
        if (!pointer->opened)
                input_close_device(handle);

        spin_lock_irqsave(&desktop.input_lock, flags);
        for (unsigned int code = 0; code < KEY_TABLE; code++)
                if (pointer->modifiers & key_mod[code])
                        keyboard_event(handle, code, 0);
        list_del(&pointer->link);
        spin_unlock_irqrestore(&desktop.input_lock, flags);

        input_unregister_handle(handle);
        kfree(pointer);
}

/*
        The console's keyboard, off while Canvas has the keys.

        The input core hands every key to every handler, and the VT's keyboard
        is one of them: it turns keys into input on the foreground console's
        tty. A machine booted without console= -- every real install -- points
        /dev/console at that tty, and init's shell reads it. So every line
        typed into a window here was typed a second time, unseen, into a root
        shell on tty1, and ran there too.

        K_OFF stops the VT turning keys into tty input and nothing else. evdev
        readers and sysrq are handlers of their own and still hear every key,
        which an exclusive grab would have taken from them, and the shell on
        tty1 stays where it is for whoever is at the console once Canvas lets
        go. Switching consoles from the keyboard is off with it, as it is under
        any display server that sets K_OFF.

        Every console, not only the one in front. Muting just that one left a
        console switch as the way round it: a program calling VT_ACTIVATE puts
        another console in front with its keyboard still on, and the keys go
        to whatever reads that tty. And a console is not muted once for good:
        allocating one runs vc_init, whose reset_vc puts its keyboard back to
        unicode, and a switch away from a VT_PROCESS owner that has died does
        the same. So the VT's own notifier is watched while Canvas has the
        keys, and a console it says was allocated or redrawn -- a switch is a
        redraw of the console coming to the front -- is muted again.

        Each console muted is remembered with the mode it had when it was
        first muted, and that is what it gets back. A mode somebody else set
        in the meantime is theirs and is left alone, and so is a console that
        was already off when Canvas arrived.

        The notifier runs with the console lock held and nothing may sleep, so
        what it and the two ends share is under a spinlock, taken outside the
        keyboard's own lock and never inside it.
*/
#ifdef CONFIG_VT

static DEFINE_SPINLOCK(canvas_vt_lock);
static _Bool canvas_vt_owned;
static _Bool canvas_vt_watching;
static signed char canvas_vt_mode[MAX_NR_CONSOLES] = {
        [0 ... MAX_NR_CONSOLES - 1] = -1,
};

// Under canvas_vt_lock.
static void canvas_keyboard_mute(unsigned int console)
{
        int mode;

        if (console >= MAX_NR_CONSOLES)
                return;

        mode = vt_do_kdgkbmode(console);
        if (mode == K_OFF || vt_do_kdskbmode(console, K_OFF))
                return;

        if (canvas_vt_mode[console] < 0)
                canvas_vt_mode[console] = (signed char)mode;
}

static int canvas_keyboard_follow(struct notifier_block *block,
                                  unsigned long event, void *data)
{
        struct vt_notifier_param *param = data;
        unsigned long flags;

        (void)block;

        if ((event != VT_ALLOCATE && event != VT_UPDATE) || !param || !param->vc)
                return NOTIFY_DONE;

        spin_lock_irqsave(&canvas_vt_lock, flags);
        if (canvas_vt_owned)
                canvas_keyboard_mute(param->vc->vc_num);
        spin_unlock_irqrestore(&canvas_vt_lock, flags);

        return NOTIFY_DONE;
}

static struct notifier_block canvas_keyboard_watch = {
        .notifier_call = canvas_keyboard_follow,
};

static void canvas_keyboard_take(void)
{
        unsigned long flags;
        unsigned int console;

        // Watching first: a console allocated between the loop below and
        // the watch starting would come up unmuted and never be told of.
        if (!canvas_vt_watching && !register_vt_notifier(&canvas_keyboard_watch))
                canvas_vt_watching = true;

        spin_lock_irqsave(&canvas_vt_lock, flags);
        canvas_vt_owned = true;
        for (console = 0; console < MAX_NR_CONSOLES; console++)
                canvas_keyboard_mute(console);
        spin_unlock_irqrestore(&canvas_vt_lock, flags);
}

static void canvas_keyboard_give(void)
{
        unsigned long flags;
        unsigned int console;

        // Unregistering waits out a notifier already running, so nothing
        // mutes a console again behind the loop below.
        if (canvas_vt_watching)
        {
                unregister_vt_notifier(&canvas_keyboard_watch);
                canvas_vt_watching = false;
        }

        spin_lock_irqsave(&canvas_vt_lock, flags);
        canvas_vt_owned = false;
        for (console = 0; console < MAX_NR_CONSOLES; console++)
        {
                if (canvas_vt_mode[console] < 0)
                        continue;

                if (vt_do_kdgkbmode(console) == K_OFF)
                        vt_do_kdskbmode(console, (unsigned int)canvas_vt_mode[console]);

                canvas_vt_mode[console] = -1;
        }
        spin_unlock_irqrestore(&canvas_vt_lock, flags);
}

#else

#define canvas_keyboard_take() ((void)0)
#define canvas_keyboard_give() ((void)0)

#endif

// Anything with keys, which is every keyboard and a mouse by its buttons.
static const struct input_device_id pointer_ids[] = {
    {
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT,
        .evbit = {BIT_MASK(EV_KEY)},
    },
    {},
};

static struct input_handler pointer_handler = {
    .event = pointer_event,
    .connect = pointer_connect,
    .disconnect = pointer_disconnect,
    .name = "moonwater",
    .id_table = pointer_ids,
};

/*
        The first redraw after a wake can land before the panel has its link
        back: the driver's resume returns, the panel is still training, and a
        commit made then is accepted and shows nothing. The desktop is drawn
        again from scratch half a second and two and a half seconds later,
        which costs nothing when the first one took.
*/
static int canvas_pm_again_left;

/*
        Whether the wake should turn the screens off and on again before the
        redraw. A real card's driver restores the state it saved, and a panel
        that state says is on but the hardware is not showing is answered by
        a plane update as if nothing had happened; only an active change makes
        it program the pipe again. A guest's display has no such drift, and
        virtio-gpu does not always get its host window back from an off.
*/
static _Bool desktop_cycle_wanted(void)
{
        struct output *output;

        list_for_each_entry(output, &desktop.outputs, link)
                if (!canvas_is_virtual(output->canvas->client.dev))
                        return true;

        return false;
}

static void canvas_pm_again_work(struct work_struct *work);

static DECLARE_DELAYED_WORK(canvas_pm_again, canvas_pm_again_work);

static void canvas_pm_again_work(struct work_struct *work)
{
        unsigned int pass = 3 - canvas_pm_again_left;

        rt_mutex_lock(&desktop.lock);
        if (!READ_ONCE(desktop.asleep) && !desktop_taken())
        {
                if (pass == 1 && desktop_cycle_wanted())
                        desktop_dpms(DRM_MODE_DPMS_OFF);

                desktop_resume();
                desktop_report(pass == 1 ? "wake +0.5 s" : "wake +2.5 s");
        }
        rt_mutex_unlock(&desktop.lock);

        if (--canvas_pm_again_left > 0)
                schedule_delayed_work(&canvas_pm_again, msecs_to_jiffies(2000));
}

static void canvas_pm_sleep(_Bool sleeping)
{
        struct output *output;

        rt_mutex_lock(&desktop.lock);

        if (sleeping)
        {
                WRITE_ONCE(desktop.asleep, true);
                WRITE_ONCE(canvas_yielded, false);
                desktop.suspended = true;
        }
        else
        {
                WRITE_ONCE(desktop.asleep, false);

                // The other program that took the card while this slept is
                // still there: the thread draws when it lets go.
                if (desktop.suspended && !desktop_taken())
                {
                        desktop_resume();
                        desktop_report("wake");
                }
                else
                        pr_info("[moonwater canvas] " "wake: another program has the card, the redraw waits for it\n");

                // What the driver reported while it came back, run now that
                // the desktop is drawn.
                list_for_each_entry(output, &desktop.outputs, link)
                        if (output->canvas->replug)
                        {
                                output->canvas->replug = false;
                                queue_work(canvas_plug_wq ? canvas_plug_wq : system_unbound_wq,
                                           &output->canvas->plug);
                        }
        }

        rt_mutex_unlock(&desktop.lock);

        if (sleeping)
        {
                cancel_delayed_work_sync(&canvas_pm_again);
                pr_info("[moonwater canvas] " "sleep: quiet\n");
        }
        else
        {
                canvas_thread_wake();
                canvas_pm_again_left = 2;
                schedule_delayed_work(&canvas_pm_again, msecs_to_jiffies(500));
        }
}

static int canvas_pm_event(struct notifier_block *block, unsigned long event,
                           void *unused)
{
        switch (event)
        {
        case PM_HIBERNATION_PREPARE:
        case PM_SUSPEND_PREPARE:
        case PM_RESTORE_PREPARE:
                canvas_pm_sleep(true);
                break;
        case PM_POST_HIBERNATION:
        case PM_POST_SUSPEND:
        case PM_POST_RESTORE:
                canvas_pm_sleep(false);
                break;
        }

        return NOTIFY_DONE;
}

static struct notifier_block canvas_pm_notifier = {
    .notifier_call = canvas_pm_event,
};

static _Bool canvas_pm_registered;

static void canvas_thread_stop(void)
{
        struct task_struct *thread = rcu_dereference_protected(
            canvas_thread, lockdep_is_held(&canvas_list_lock));

        if (!thread)
                return;
        RCU_INIT_POINTER(canvas_thread, NULL);

        if (canvas_pm_registered)
        {
                unregister_pm_notifier(&canvas_pm_notifier);
                canvas_pm_registered = false;
        }
        WRITE_ONCE(desktop.asleep, false);
        cancel_delayed_work_sync(&canvas_pm_again);

        /* Registration can be interrupted before the input core initializes
           the handler's lists.  Only hand a handler back after the matching
           registration completed. */
        if (pointer_handler_registered)
        {
                input_unregister_handler(&pointer_handler);
                pointer_handler_registered = false;
        }

        // Only once no key can reach the handler: from here the console's
        // keyboard is the only one again.
        canvas_keyboard_give();

        synchronize_rcu();
        kthread_stop(thread);

        if (canvas_plug_wq)
        {
                destroy_workqueue(canvas_plug_wq);
                canvas_plug_wq = NULL;
        }
}

/*
        Sleeps until something moves, then draws it.

        set_current_state before the flag is read, which is what makes the
        sleep safe: a wake arriving between the two finds the task already
        marked and schedule() returns at once rather than losing the event.
*/
static void canvas_thread_wake(void)
{
        struct task_struct *thread;

        rcu_read_lock();
        thread = rcu_dereference(canvas_thread);
        if (thread)
                wake_up_process(thread);
        rcu_read_unlock();
}

/*
        While another program is master of a card Canvas draws on.

        Nothing Canvas drew would land, so it draws nothing, and the consoles
        stay off throughout: the other program (the compositor, KDE's, Weston)
        reads the devices itself.

        The test is made when the thread wakes for a chord, and every ~250 ms
        while suspended with nothing else to wake for, so a program that drops
        master and stays running does not leave a frozen screen: the ground is
        drawn again within a poll of the card coming back. A commit made while
        the other program was master answered EBUSY and changed nothing.
*/
#define CANVAS_SUSPENDED_POLL_MS 250

/*
        The session of whatever holds the display, ended.

        Only the holder dying is not enough: KDE's wrapper starts KWin again
        and the new one takes the card straight back. So its session goes with
        it, which is every program the desktop started -- unless the session
        is init's own, which is every shell on a console that was never given
        another, and which is not this key's to end: the holder alone is then.
        Not its process group either, which a shell with no job control makes
        of itself and everything it starts. Kernel threads and init are never
        signalled.
*/
static void canvas_session_end(struct pid *process, struct pid *session)
{
        struct task_struct *task;
        struct task_struct *init;
        struct pid *shared;

        kill_pid(process, SIGKILL, 1);

        rcu_read_lock();
        init = find_task_by_pid_ns(1, &init_pid_ns);
        shared = init ? task_session(init) : NULL;

        if (session && session != shared)
                do_each_pid_task(session, PIDTYPE_SID, task)
                {
                        if (is_global_init(task) || (task->flags & PF_KTHREAD))
                                continue;
                        group_send_sig_info(SIGKILL, SEND_SIG_PRIV, task,
                                            PIDTYPE_TGID);
                }
                while_each_pid_task(session, PIDTYPE_SID, task);
        rcu_read_unlock();
}

// One card's master, found as canvas_master_holder finds it, and ended.
static void canvas_master_end(struct drm_device *dev)
{
        struct drm_file *file;
        struct pid *process = NULL;
        struct pid *session = NULL;
        char command[TASK_COMM_LEN] = "";
        int number = 0;

        mutex_lock(&dev->filelist_mutex);
        list_for_each_entry(file, &dev->filelist, lhead)
        {
                struct task_struct *task;

                if (!drm_is_current_master(file))
                        continue;

                rcu_read_lock();
                task = pid_task(rcu_dereference(file->pid), PIDTYPE_TGID);
                if (task)
                {
                        process = get_pid(task_tgid(task));
                        session = get_pid(task_session(task));
                        number = task_tgid_nr(task);
                        strscpy(command, task->comm, sizeof(command));

                        // A program names itself with prctl, and this goes to
                        // the kernel log.
                        for (char *at = command; *at; at++)
                                if (*at < ' ' || *at > '~')
                                        *at = '?';
                }
                rcu_read_unlock();
                break;
        }
        mutex_unlock(&dev->filelist_mutex);

        if (process)
        {
                pr_info("[moonwater canvas] " "Control-Alt-Backspace: %s (pid %d) holds the display, and it and its session are ended\n", command, number);
                canvas_session_end(process, session);
        }

        put_pid(process);
        put_pid(session);
}

/*
        Take the display back from the program that holds it.

        Outside desktop.lock and every other lock of this file: it takes a
        card's filelist_mutex, which drm_open and drm_release hold, and the
        cards are only referenced here, not locked, while it does.
*/
#define CANVAS_RECLAIM_CARDS 4

static void canvas_reclaim(void)
{
        struct drm_device *cards[CANVAS_RECLAIM_CARDS];
        unsigned int count = 0;
        struct canvas *canvas;

        mutex_lock(&canvas_list_lock);
        list_for_each_entry(canvas, &canvas_list, link)
        {
                if (count == CANVAS_RECLAIM_CARDS)
                        break;
                drm_dev_get(canvas->client.dev);
                cards[count++] = canvas->client.dev;
        }
        mutex_unlock(&canvas_list_lock);

        while (count)
        {
                canvas_master_end(cards[--count]);
                drm_dev_put(cards[count]);
        }
}

/*
        The work behind Control-Alt-Shift-Backspace.

        The holder of the card and its session are ended first, as
        Control-Alt-Backspace ends them: a stopped or hung program holds the
        card against the text console, which cannot take a screen that has a
        master. SIGKILL reaches a stopped process. Canvas is turned off once
        nobody is master, or after CANVAS_EXIT_WAIT_MS if somebody still is,
        so a holder that cannot die does not keep the machine from at least
        leaving Canvas; the console takes the card when it lets go.
*/
#define CANVAS_EXIT_WAIT_MS 2000

static void canvas_exit_work(struct work_struct *work)
{
        unsigned int waited;
        _Bool taken = false;

        pr_info("[moonwater canvas] " "Control-Alt-Shift-Backspace: Canvas off, to the text console\n");
        canvas_reclaim();

        for (waited = 0; waited < CANVAS_EXIT_WAIT_MS; waited += 5)
        {
                rt_mutex_lock(&desktop.lock);
                taken = desktop_taken();
                rt_mutex_unlock(&desktop.lock);

                if (!taken)
                        break;

                msleep(5);
        }

        canvas_turn_off();
}

static DECLARE_WORK(canvas_exit, canvas_exit_work);

static void canvas_exit_request(void)
{
        queue_work(system_dfl_long_wq, &canvas_exit);
}

static _Bool canvas_suspend_check(void)
{
        _Bool taken;

        rt_mutex_lock(&desktop.lock);

        taken = desktop_taken();

        // A card that only sleeps has no program in front of it.
        WRITE_ONCE(canvas_yielded, taken && !READ_ONCE(desktop.asleep));

        if (taken)
                desktop.suspended = true;
        else if (desktop.suspended)
                desktop_resume();

        rt_mutex_unlock(&desktop.lock);

        /*
                The chord is answered here, where who holds the card was just
                asked. A card that is only asleep has no holder to end.
        */
        if (atomic_xchg(&desktop.reclaim, 0) && taken && !READ_ONCE(desktop.asleep))
                canvas_reclaim();

        return taken;
}

/*
        Sleeps until a chord asks for something, and while another program
        holds a card polls for the card coming back.

        set_current_state before the flag is read, which is what makes the
        sleep safe: a wake arriving between the two finds the task already
        marked and schedule_timeout returns at once rather than losing the
        event. With nothing held and nothing asked it sleeps for good.
*/
static int canvas_loop(void *unused)
{
        while (!kthread_should_stop())
        {
                set_current_state(TASK_IDLE);

                if (!atomic_read(&desktop.reclaim))
                        schedule_timeout(READ_ONCE(desktop.suspended) && !READ_ONCE(desktop.asleep)
                                             ? msecs_to_jiffies(CANVAS_SUSPENDED_POLL_MS)
                                             : MAX_SCHEDULE_TIMEOUT);

                __set_current_state(TASK_RUNNING);
                WRITE_ONCE(canvas_passes, canvas_passes + 1);

                canvas_suspend_check();
        }

        return 0;
}

static void canvas_thread_start(void)
{
        /*
                A thread rather than a workqueue.

                WQ_HIGHPRI raises a kworker's nice level and leaves it an
                ordinary task, so it is still scheduled against everything
                else running. SCHED_FIFO is a different queue entirely: the
                scheduler picks it before any normal task, which is what lets
                the chord that ends a program that has the display be acted on
                while a runaway one holds every processor. Priority 1, as
                sched_set_fifo_low gives: ahead of every SCHED_OTHER task and
                behind anything the machine considers more urgent.
        */
        static const struct sched_attr canvas_policy = {
                .size = sizeof(struct sched_attr),
                .sched_policy = SCHED_FIFO,
                .sched_priority = 1,
        };
        struct task_struct *thread;

        WRITE_ONCE(canvas_yielded, false);
        thread = kthread_run(canvas_loop, NULL, "moonwater/canvas");

        if (IS_ERR(thread))
        {
                pr_info("[moonwater canvas] " "no thread for input\n");
                return;
        }

        rcu_assign_pointer(canvas_thread, thread);
        WARN_ON_ONCE(sched_setattr_nocheck(thread, &canvas_policy));

        if (!register_pm_notifier(&canvas_pm_notifier))
                canvas_pm_registered = true;

        // Before the handler, so no key is ever delivered to both.
        canvas_keyboard_take();

        if (input_register_handler(&pointer_handler))
        {
                pr_info("[moonwater canvas] " "could not register the input handler\n");
                canvas_keyboard_give();
        }
        else
                pointer_handler_registered = true;
}

/*
        What the input handler costs and whether it is keeping out of the way,
        for `moonwater latency`; the core adds the key watch's. A reset zeroes
        what is counted, and answers with the zeros.
*/
void canvas_latency(struct latency_stats *out, unsigned int request)
{
        struct handler_cost heard = {};
        int cpu;

        if (request == SPARK_LATENCY_RESET)
        {
                moonwater_cost_reset(&canvas_cost);
                for_each_possible_cpu(cpu)
                        WRITE_ONCE(*per_cpu_ptr(&canvas_quiet, cpu), 0);
        }

        out->yielded = READ_ONCE(canvas_yielded);
        out->thread_passes = READ_ONCE(canvas_passes);

        moonwater_cost_read(&canvas_cost, &heard);
        out->canvas.events = heard.events;
        out->canvas.samples = heard.samples;
        out->canvas.total_ns = heard.total_ns;
        out->canvas.worst_ns = heard.worst_ns;
        for_each_possible_cpu(cpu)
                out->quiet += READ_ONCE(*per_cpu_ptr(&canvas_quiet, cpu));
}

#undef REPORT_CANVAS

/*
        Canvas off and on, and what it holds.

        The state is copied back whatever the request answers, so a refused
        on can name the program that holds the display. Reading needs nothing;
        on and off stop and start the desktop everyone at the machine is
        using, so they need CAP_SYS_ADMIN. The three numbers the kernel's own
        desktop answered (the keymap, its log window and its terminal) are
        answered by nothing any more.
*/
static long report_canvas(struct canvas_control __user *out)
{
        struct canvas_control control;
        unsigned int request;
        long answer = 0;
        unsigned int asked_scale, asked_modes;

        if (copy_from_user(&control, out, sizeof(control)))
                return -EFAULT;

        request = control.request;
        asked_scale = control.scale_setting;
        asked_modes = control.modes;
        // 3, 4 and 5 were the kernel's own desktop's, and are answered by nothing.
        if (request > SPARK_CANVAS_SET_MODES || (request >= 3u && request <= 5u))
                return -EINVAL;
        if (request != SPARK_CANVAS_STATUS && !capable(CAP_SYS_ADMIN))
                return -EPERM;

        memset(&control, 0, sizeof(control));
        control.request = request;

        if (request == SPARK_CANVAS_ON)
                answer = canvas_turn_on(&control);
        else if (request == SPARK_CANVAS_OFF)
                answer = canvas_turn_off();
        else if (request == SPARK_CANVAS_SET_SCALE)
                answer = canvas_scale_set(asked_scale);
        else if (request == SPARK_CANVAS_SET_MODES)
                answer = canvas_modes_set(asked_modes);

        canvas_state(&control);

        if (copy_to_user(out, &control, sizeof(control)))
                return -EFAULT;

        return answer;
}

/*
        The window table the compositor shares is a memfd, which no path names
        that another process may open: its /proc/<pid>/fd link asks for the
        right to trace the compositor. The compositor gives it here, as root,
        and any program takes a descriptor of it (close-on-exec) from the
        device, as it could open the table's file before. The table is let go
        when the compositor gives another or none, or closes the /dev/spark
        descriptor it gave it with.
*/
static DEFINE_SPINLOCK(windows_table_lock);
static struct file *windows_table, *windows_table_giver;

static long windows_table_set(struct file *giver, unsigned long arg)
{
        struct file *table = NULL, *old;

        if (!capable(CAP_SYS_ADMIN))
                return -EPERM;
        if ((long)arg >= 0)
        {
                table = fget((unsigned int)arg);
                if (!table)
                        return -EBADF;
                if (!S_ISREG(file_inode(table)->i_mode) || !(table->f_mode & FMODE_WRITE))
                {
                        fput(table);
                        return -EINVAL;
                }
        }
        spin_lock(&windows_table_lock);
        old = windows_table;
        windows_table = table;
        windows_table_giver = table ? giver : NULL;
        spin_unlock(&windows_table_lock);
        if (old)
                fput(old);
        return 0;
}

static long windows_table_get(void)
{
        struct file *table;
        int fd;

        spin_lock(&windows_table_lock);
        table = windows_table ? get_file(windows_table) : NULL;
        spin_unlock(&windows_table_lock);
        if (!table)
                return -ENOENT;
        fd = get_unused_fd_flags(O_CLOEXEC);
        if (fd < 0)
        {
                fput(table);
                return fd;
        }
        fd_install(fd, table);
        return fd;
}

static void windows_table_release(struct file *giver)
{
        struct file *old = NULL;

        spin_lock(&windows_table_lock);
        if (windows_table_giver == giver)
        {
                old = windows_table;
                windows_table = windows_table_giver = NULL;
        }
        spin_unlock(&windows_table_lock);
        if (old)
                fput(old);
}

IOCTL_IS(SPARK_IOCTL_CANVAS, IOCTL_BOTH, 10, sizeof(struct canvas_control));

IOCTL_IS(WINDOWS_IOCTL_TABLE_SET, IOCTL_NONE, 17, 0);

IOCTL_IS(WINDOWS_IOCTL_TABLE_GET, IOCTL_NONE, 18, 0);

long canvas_client_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
        switch (cmd)
        {
        case SPARK_IOCTL_CANVAS:
                return report_canvas((struct canvas_control __user *)arg);
        case WINDOWS_IOCTL_TABLE_SET:
                return windows_table_set(file, arg);
        case WINDOWS_IOCTL_TABLE_GET:
                return windows_table_get();
        }

        return -ENOTTY;
}

void canvas_client_release(struct file *file)
{
        windows_table_release(file);
}

// The device is registered and the bindings work: look for a card.
void canvas_boot(void)
{
#ifdef CONFIG_MOONWATER_CANVAS_AUTOSTART
        // Set up whether or not it is scheduled: turning Canvas on cancels it.
        INIT_DELAYED_WORK(&canvas_probe_work, canvas_probe);
#endif

        /*
                The image's settings can say Canvas stays off at boot
                (moonwater bind init canvas off), and the text console is then
                what is on the screen. The settings are read before this runs,
                at the start of the core. moonwater canvas on starts Canvas
                afterwards as it would after any off.
        */
        if (moonwater_settings_flags() & SPARK_SETTINGS_CANVAS_OFF)
        {
                pr_info("[moonwater canvas] " "off at boot, as the image's settings say; moonwater canvas on starts it\n");
                return;
        }

#ifdef CONFIG_MOONWATER_CANVAS_AUTOSTART
        canvas_start_probing();
#endif
}

void canvas_unload(void)
{
#ifdef CONFIG_MOONWATER_CANVAS_AUTOSTART
        // A probe that has not found a card owns no DRM client (and therefore
        // no module reference) to keep this callback's text resident.
        cancel_delayed_work_sync(&canvas_probe_work);
#endif
}
