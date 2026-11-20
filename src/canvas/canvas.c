/*
        Moonwater Canvas — kernel DRM client holding the boot splash until the userspace
        compositor takes master, plus global rescue chords (Ctrl-Alt-Backspace / +Shift)
        and /dev/spark canvas & window-table ioctls.
*/

// Userspace-safe ABI region (included with CANVAS_ABI_ONLY and no kernel headers).
#ifndef CANVAS_ABI_INCLUDED
#define CANVAS_ABI_INCLUDED

struct latency_cost {
        unsigned long events;   // reports the handler was called with
        unsigned long samples;  // the ones that were timed
        unsigned long total_ns; // their time, added
        unsigned long worst_ns; // the longest of them
};

#define SPARK_LATENCY_READ 0u
#define SPARK_LATENCY_RESET 1u

struct latency_stats {
        unsigned int request;      // SPARK_LATENCY_*
        unsigned int yielded;      // 1 while another program has the display
        unsigned int retired_hold;
        unsigned int retired_holds;
        unsigned int thread_passes; // times the canvas thread woke
        unsigned int retired_ticks;
        struct latency_cost canvas; // Canvas's input handler
        struct latency_cost bind;   // the machine's key watch
        unsigned long quiet;        // canvas events ignored
        unsigned long elapsed_ns;   // since reset or boot
        unsigned long reserved;
};

_Static_assert(sizeof(struct latency_cost) == 32, "spark latency cost ABI");
_Static_assert(sizeof(struct latency_stats) == 112, "spark latency ABI");

#define SPARK_IOCTL_LATENCY 0xc0707308u

// 3, 4 and 5 are retired and return -EINVAL.
#define SPARK_CANVAS_STATUS 0u
#define SPARK_CANVAS_ON 1u
#define SPARK_CANVAS_OFF 2u
#define SPARK_CANVAS_SET_SCALE 6u
#define SPARK_CANVAS_SET_MODES 7u
#define SPARK_CANVAS_OUTPUTS 4u

#define SPARK_CANVAS_SCALE_AUTO 0u
#define SPARK_CANVAS_SCALE_MOST 4u
#define SPARK_CANVAS_ORIGIN_DEFAULT 0u
#define SPARK_CANVAS_ORIGIN_COMMAND_LINE 1u
#define SPARK_CANVAS_ORIGIN_SET 2u

#define SPARK_CANVAS_MODES_LARGEST 0u
#define SPARK_CANVAS_MODES_PREFERRED 1u

struct canvas_output_state {
        char connector[16];
        unsigned int width, height, refresh;
        unsigned int reserved;
};

struct canvas_control {
        unsigned int request;      // SPARK_CANVAS_*
        unsigned int running;      // 1 while Canvas holds a card
        unsigned int cards;        // cards Canvas holds
        unsigned int retired_windows;
        unsigned int retired_detached;
        unsigned int suspended;    // 1 while another program is a card's master
        int master_pid;            // holder on refusal, 0 for nobody
        unsigned int output_count; // at most SPARK_CANVAS_OUTPUTS
        char master_command[16];
        char driver[16];           // first card's driver
        struct canvas_output_state output[SPARK_CANVAS_OUTPUTS];
        unsigned int retired_hold;
        unsigned int retired_holds;
        unsigned int thread_passes;
        unsigned int retired_ticks;
        unsigned int scale;
        unsigned int scale_setting; // SPARK_CANVAS_SCALE_AUTO or requested
        unsigned int scale_dpi;
        unsigned int scale_origin;  // SPARK_CANVAS_ORIGIN_*
        unsigned int modes;         // SPARK_CANVAS_MODES_*
        unsigned int reserved;
};

_Static_assert(sizeof(struct canvas_output_state) == 32, "spark canvas output ABI");
_Static_assert(sizeof(struct canvas_control) == 232, "spark canvas control ABI");

#define SPARK_IOCTL_CANVAS 0xc0e8730au
#define WINDOWS_IOCTL_TABLE_SET 0x00007311u
#define WINDOWS_IOCTL_TABLE_GET 0x00007312u

#define SPARK_IOCTL_SIZE(request) (((request) >> 16) & 0x3fffu)
_Static_assert(SPARK_IOCTL_SIZE(SPARK_IOCTL_LATENCY) == sizeof(struct latency_stats), "spark ioctl size");
_Static_assert(SPARK_IOCTL_SIZE(SPARK_IOCTL_CANVAS) == sizeof(struct canvas_control), "spark ioctl size");

#define SPARK_BIND_CANVAS_ON 12u
#define SPARK_BIND_CANVAS_OFF 13u
#define SPARK_BIND_CANVAS_ON_NAME "canvas on"
#define SPARK_BIND_CANVAS_OFF_NAME "canvas off"

#define SPARK_SETTINGS_CANVAS_OFF 0x1u

#endif // CANVAS_ABI_INCLUDED

#ifndef CANVAS_ABI_ONLY
#ifndef CANVAS_DECLARED
#define CANVAS_DECLARED
#include <linux/fs.h>
#include <linux/errno.h>

/*
        What the core asks of Canvas. With CANVAS_DECLARE_ONLY defined the core takes these
        and not the body below. With Canvas off every one is an empty inline.
*/
#ifdef CONFIG_MOONWATER_CANVAS

// The device is registered and the machine is listening: look for a card.
void canvas_boot(void);

// The core is going away. Before anything of its own is.
void canvas_unload(void);

// Whether the desktop holds a card.
_Bool canvas_is_on(void);

// What Canvas's input handler costs, for `moonwater latency`; see the ABI above.
void canvas_latency(struct latency_stats *out, unsigned int request);

// The requests /dev/spark takes that are not the core's; -ENOTTY for the rest.
long canvas_client_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
void canvas_client_release(struct file *file);

#else

static inline void canvas_latency(struct latency_stats *out, unsigned int request) {}
static inline void canvas_boot(void) {}
static inline void canvas_unload(void) {}
static inline _Bool canvas_is_on(void) { return false; }

static inline long canvas_client_ioctl(struct file *file, unsigned int cmd,
                                       unsigned long arg)
{
        return -ENOTTY;
}

static inline void canvas_client_release(struct file *file) {}

#endif // CONFIG_MOONWATER_CANVAS
#endif // CANVAS_DECLARED

#ifndef CANVAS_DECLARE_ONLY
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/cred.h>
#include <linux/file.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/fs_struct.h>
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
#include "../moonwater/splash.c"
#include "../sh/splash_mark.c"

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

// White boot splash ground matching the EFI splash stub.
#define CANVAS_GROUND 0xffffff
#define CANVAS_LOG "[moonwater canvas] "

DEFINE_GUARD(rt_mutex, struct rt_mutex *, rt_mutex_lock(_T), rt_mutex_unlock(_T))

struct output
{
        struct list_head link;
        struct canvas *canvas;
        struct drm_client_buffer *buffer;
        struct drm_mode_set *mode_set;
        unsigned int width, height;
        u32 opaque;
        _Bool unmappable;
        struct drm_client_buffer *replaced; // freed after replacement commit lands
};

struct canvas
{
        struct list_head link;
        struct drm_client_dev client;
        _Bool started;
        int set_result;           // 1 before first commit so every real result logs once
        struct work_struct plug;  // runs on moonwater/plug, never DRM helper wq
        _Bool replug;             // hotplug deferred while asleep
        _Bool retarget;           // next hotplug may change mode size either way
        u32 rules_connected;
        unsigned int rules_count, rules_external;
};

static struct desktop
{
        struct rt_mutex lock;     // priority-inheriting for SCHED_FIFO canvas thread
        spinlock_t input_lock;    // leaf lock serialising input callbacks across devices
        struct list_head outputs;
        _Bool suspended;          // another program holds DRM master
        _Bool asleep;             // machine suspending/resuming
        unsigned int scale;
        int width, height;
        atomic_t modifiers;
        atomic_t reclaim;         // Ctrl-Alt-Backspace pending for thread
} desktop = {
    .lock = __RT_MUTEX_INITIALIZER(desktop.lock),
    .input_lock = __SPIN_LOCK_UNLOCKED(desktop.input_lock),
    .outputs = LIST_HEAD_INIT(desktop.outputs),
    .scale = 1,
};

static DEFINE_MUTEX(canvas_list_lock);
static LIST_HEAD(canvas_list);

static _Bool canvas_thread_start(void);
static void canvas_thread_stop(void);
static void canvas_thread_wake(void);
static void output_free(struct output *output);
static void desktop_redraw(void);
static void desktop_recompose(void);
static _Bool canvas_scale_refresh(void);
static _Bool desktop_taken(void);
static _Bool desktop_lit(void);
void canvas_rect_fill(u32 *at, unsigned long pitch, unsigned long width,
                      unsigned long height, u32 colour);

static unsigned int canvas_passes;
static _Bool canvas_yielded;
static DEFINE_PER_CPU(unsigned long, canvas_quiet);
static DEFINE_PER_CPU(struct handler_cost, canvas_cost);

static char *canvas_desktop;
module_param_named(desktop, canvas_desktop, charp, 0444);

static p8 canvas_cursor_plane = true;
module_param_named(cursor_plane, canvas_cursor_plane, bool, 0444);

static CONST struct canvas *canvas_from_client(struct drm_client_dev *client)
{
        return container_of(client, struct canvas, client);
}

static PURE u32 canvas_plane_pick_format(struct drm_plane *plane, u32 first, u32 second)
{
        for (unsigned int i = 0; i < plane->format_count; i++)
                if (plane->format_types[i] == first || plane->format_types[i] == second)
                        return plane->format_types[i];
        return DRM_FORMAT_INVALID;
}

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
                pr_err(CANVAS_LOG "the scanout buffer will not map (%d), so nothing can be drawn on it\n", ret);
        }
        return false;
}

static _Bool output_describe(struct output *output)
{
        struct drm_framebuffer *fb = output->buffer->fb;
        struct iosys_map map;

        if (!output_map(output, &map))
                return false;
        pr_info(CANVAS_LOG "scanout %p4cc, %u bytes a row (%lu KiB), modifier %llx, %s memory\n",
                &fb->format->format, fb->pitches[0], ((unsigned long)fb->pitches[0] * output->height) >> 10,
                (unsigned long long)fb->modifier, map.is_iomem ? "device" : "system");
        drm_client_buffer_vunmap_local(output->buffer);
        return true;
}

/* ---- output: outputs, modes, and scale ---- */

static PURE _Bool canvas_driver_among(struct drm_device *dev,
                                      const char *const *names, positive count)
{
        return dev->driver && dev->driver->name &&
               string_table_find((string_address)dev->driver->name, names,
                                 sizeof(names[0]), count) < count;
}

static PURE _Bool canvas_is_virtual(struct drm_device *dev)
{
        static const char *const guests[] = {
            "bochs-drm", "virtio_gpu", "qxl", "vmwgfx", "cirrus-qemu", "hyperv_drm", "vkms"};
        return canvas_driver_among(dev, guests, array_count(guests));
}

// Firmware GOP framebuffer (simpledrm/efidrm); deferred so real GPUs probe first.
static PURE _Bool canvas_is_firmware(struct drm_device *dev)
{
        static const char *const firmware[] = {"simpledrm", "efidrm"};
        return canvas_driver_among(dev, firmware, array_count(firmware));
}

static unsigned int canvas_scale_setting = SPARK_CANVAS_SCALE_AUTO;
static unsigned int canvas_scale_origin = SPARK_CANVAS_ORIGIN_DEFAULT;
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

// Scale (2 from 150 DPI, 3 from 260 DPI, floor 1280x640 logical) or 0 when panel cannot say.
static unsigned int canvas_scale_of(const struct drm_mode_set *set, unsigned int *dpi)
{
        const struct drm_connector *connector;
        unsigned long width, height, width_mm, height_mm;
        unsigned int scale;

        if (!set || !set->mode || !set->num_connectors || !set->connectors || !set->connectors[0])
                return 0;
        connector = set->connectors[0];
        if (!canvas_connector_is_panel(connector) || canvas_is_virtual(connector->dev))
                return 0;

        width = set->mode->hdisplay;
        height = set->mode->vdisplay;
        width_mm = connector->display_info.width_mm;
        height_mm = connector->display_info.height_mm;
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

static unsigned int canvas_scale_auto(struct drm_client_dev *joining, unsigned int *dpi)
{
        struct drm_mode_set *set;
        struct output *output;
        unsigned int scale;

        *dpi = 0;
        list_for_each_entry(output, &desktop.outputs, link)
                if ((scale = canvas_scale_of(output->mode_set, dpi)))
                        return scale;
        if (joining)
                drm_client_for_each_modeset(set, joining)
                        if ((scale = canvas_scale_of(set, dpi)))
                                return scale;
        return 1;
}

static unsigned int canvas_scale_wanted(struct drm_client_dev *joining)
{
        unsigned int dpi;
        return canvas_scale_setting ? canvas_scale_setting : canvas_scale_auto(joining, &dpi);
}

static int canvas_scale_param_set(const char *value, const struct kernel_param *kp)
{
        unsigned int number;

        if (sysfs_streq(value, "auto"))
                number = SPARK_CANVAS_SCALE_AUTO;
        else if (kstrtouint(value, 10, &number) || !number || number > SPARK_CANVAS_SCALE_MOST)
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
    .set = canvas_scale_param_set, .get = canvas_scale_param_get,
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
    .set = canvas_modes_param_set, .get = canvas_modes_param_get,
};

// Read-only: a running desktop is changed by moonwater canvas, which relays it.
#undef MODULE_PARAM_PREFIX

#define MODULE_PARAM_PREFIX "canvas."

module_param_cb(scale, &canvas_scale_ops, null, 0444);

module_param_cb(modes, &canvas_modes_ops, null, 0444);

#undef MODULE_PARAM_PREFIX

#define MODULE_PARAM_PREFIX "moonwater."

static struct drm_connector *modeset_connector(const struct drm_mode_set *set)
{
        return set && set->num_connectors && set->connectors ? set->connectors[0] : null;
}

static PURE unsigned int output_mode_count(struct drm_connector *connector)
{
        struct drm_display_mode *mode;
        unsigned int count = 0;

        list_for_each_entry(mode, &connector->modes, head)
                count++;
        return count;
}

// Best non-interlaced mode matching type and size: prefer_refresh first, then pixels, then refresh.
static struct drm_display_mode *output_pick_mode(struct drm_connector *connector,
                                                 unsigned int type, int width,
                                                 int height, _Bool exact, int prefer_refresh)
{
        struct drm_display_mode *mode, *best = null;
        int best_score = 0, best_refresh = 0;
        _Bool best_preferred = false;

        list_for_each_entry(mode, &connector->modes, head)
        {
                int score, refresh;
                _Bool preferred;

                if ((mode->type & type) != type ||
                    (mode->flags & (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN)))
                        continue;
                if (width && (exact ? mode->hdisplay != width || mode->vdisplay != height
                                    : mode->hdisplay > width || mode->vdisplay > height))
                        continue;

                refresh = drm_mode_vrefresh(mode);
                score = mode->hdisplay * mode->vdisplay;
                preferred = prefer_refresh > 0 && refresh == prefer_refresh;
                if (best && (preferred < best_preferred ||
                             (preferred == best_preferred &&
                              (score < best_score || (score == best_score && refresh <= best_refresh)))))
                        continue;
                best = mode;
                best_score = score;
                best_refresh = refresh;
                best_preferred = preferred;
        }
        return best;
}

// First committed mode per connector, restored across canvas off/on.
#define CANVAS_SAVED_MODES 8

struct canvas_saved_mode { char name[32]; int hdisplay, vdisplay, vrefresh; };

static struct canvas_saved_mode canvas_saved_mode[CANVAS_SAVED_MODES];
static unsigned int canvas_saved_modes;

static struct canvas_saved_mode *canvas_mode_saved(const char *name)
{
        if (!name)
                return null;
        for (unsigned int i = 0; i < canvas_saved_modes; i++)
                if (string_equals((string_address)canvas_saved_mode[i].name, (string_address)name))
                        return &canvas_saved_mode[i];
        return null;
}

static void canvas_mode_keep(const char *name, const struct drm_display_mode *mode)
{
        struct canvas_saved_mode *slot;

        if (!name || !mode)
                return;
        slot = canvas_mode_saved(name);
        if (!slot)
        {
                if (canvas_saved_modes >= CANVAS_SAVED_MODES)
                        return;
                slot = &canvas_saved_mode[canvas_saved_modes++];
                strscpy(slot->name, name, sizeof(slot->name));
        }
        slot->hdisplay = mode->hdisplay;
        slot->vdisplay = mode->vdisplay;
        slot->vrefresh = drm_mode_vrefresh(mode);
}

static void canvas_modes_keep(void)
{
        struct output *output;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                struct drm_mode_set *set = output->mode_set;
                struct drm_connector *connector = modeset_connector(set);

                if (!set || !set->mode || !connector || !connector->name)
                        continue;
                // Skip no-EDID fallback modes on real screens so later restarts don't pin 1024x768.
                if (!READ_ONCE(connector->edid_blob_ptr) && !canvas_is_virtual(output->canvas->client.dev))
                        continue;
                canvas_mode_keep(connector->name, set->mode);
        }
}

static struct drm_display_mode *output_screen_mode(struct drm_connector *connector)
{
        struct drm_display_mode *preferred = output_pick_mode(connector, DRM_MODE_TYPE_PREFERRED, 0, 0, false, 0);
        return preferred ? preferred : output_pick_mode(connector, 0, 0, 0, false, 0);
}

// Duplicate src with CRTC timings filled (virtio-gpu vblank timer needs crtc_clock > 0).
static struct drm_display_mode *output_mode_take(struct drm_device *dev, const struct drm_display_mode *src)
{
        struct drm_display_mode *taken;

        if (!src || src->clock <= 0)
                return null;
        taken = drm_mode_duplicate(dev, src);
        if (!taken)
                return null;
        drm_mode_set_crtcinfo(taken, CRTC_INTERLACE_HALVE_V);
        if (taken->crtc_clock <= 0)
        {
                drm_mode_destroy(dev, taken);
                return null;
        }
        return taken;
}

// Guest window mode: listed mode at/under 70% of host screen, preferring screen refresh.
static struct drm_display_mode *output_guest_mode(struct drm_device *dev, struct drm_connector *connector)
{
        struct drm_display_mode *screen = output_screen_mode(connector), *listed;
        int w, h;

        if (!screen)
                return null;
        w = max((screen->hdisplay * 7 / 10) & ~1, 2);
        h = max((screen->vdisplay * 7 / 10) & ~1, 2);
        listed = output_pick_mode(connector, 0, w, h, true, 0);
        if (!listed)
                listed = output_pick_mode(connector, 0, w, h, false, drm_mode_vrefresh(screen));
        return output_mode_take(dev, listed ? listed : screen);
}

// Real card whose connectors are still all unknown (e.g. i915 before user_access opens).
#define CANVAS_UNSETTLED_TRIES 50
#define CANVAS_UNSETTLED_MS 20

static _Bool canvas_unsettled(struct drm_device *dev)
{
        struct drm_connector_list_iter iter;
        struct drm_connector *connector;
        _Bool connected = false, unknown = false;

        if (canvas_is_virtual(dev) || canvas_is_firmware(dev))
                return false;
        drm_connector_list_iter_begin(dev, &iter);
        drm_for_each_connector_iter(connector, &iter)
        {
                enum drm_connector_status status = READ_ONCE(connector->status);
                connected |= status == connector_status_connected;
                unknown |= status == connector_status_unknown;
        }
        drm_connector_list_iter_end(&iter);
        return unknown && !connected;
}

static COLD int canvas_probe_modes(struct canvas *canvas, _Bool biggest, _Bool keep_saved)
{
        struct drm_client_dev *client = &canvas->client;
        struct drm_device *dev = client->dev;
        struct drm_mode_set *mode_set;
        unsigned int waited = 0;

        for (;;)
        {
                if (drm_client_modeset_probe(client, 0, 0))
                        return -ENODEV;
                if (!keep_saved || waited >= CANVAS_UNSETTLED_TRIES || !canvas_unsettled(dev))
                        break;
                msleep(CANVAS_UNSETTLED_MS);
                waited++;
        }
        if (waited)
                pr_info(CANVAS_LOG "%s: no connector was known, waited %u ms for them%s\n",
                        dev->driver->name, waited * CANVAS_UNSETTLED_MS,
                        canvas_unsettled(dev) ? ", and they still are not" : "");

        mutex_lock(&client->modeset_mutex);
        mutex_lock(&dev->mode_config.mutex);
        drm_client_for_each_modeset(mode_set, client)
        {
                struct drm_connector *connector = modeset_connector(mode_set);
                struct drm_display_mode *want = null, *taken = null;
                const struct canvas_saved_mode *saved;

                if (!mode_set->mode || !connector)
                        continue;
                saved = keep_saved ? canvas_mode_saved(connector->name) : null;
                if (connector->cmdline_mode.specified)
                        taken = output_mode_take(dev, mode_set->mode);
                else if (saved)
                        want = output_pick_mode(connector, 0, saved->hdisplay, saved->vdisplay, true, saved->vrefresh);
                else if (canvas_is_virtual(dev))
                        taken = output_guest_mode(dev, connector);
                else
                {
                        want = output_pick_mode(connector, biggest ? 0 : DRM_MODE_TYPE_PREFERRED, 0, 0, false, 0);
                        if (!want && !biggest)
                                want = output_pick_mode(connector, 0, mode_set->mode->hdisplay,
                                                        mode_set->mode->vdisplay, true, 0);
                }

                if (want)
                {
                        if (drm_mode_equal(want, mode_set->mode))
                                continue;
                        taken = output_mode_take(dev, want);
                }
                if (!taken)
                        continue;
                if (!connector->cmdline_mode.specified && drm_mode_equal(taken, mode_set->mode))
                {
                        drm_mode_destroy(dev, taken);
                        continue;
                }
                drm_mode_destroy(dev, mode_set->mode);
                mode_set->mode = taken;
        }
        mutex_unlock(&dev->mode_config.mutex);
        mutex_unlock(&client->modeset_mutex);
        return 0;
}

static struct output *output_for_modeset(struct canvas *canvas, struct drm_mode_set *mode_set)
{
        struct output *output;

        list_for_each_entry(output, &desktop.outputs, link)
                if (output->canvas == canvas && output->mode_set == mode_set)
                        return output;
        return null;
}

static void output_attach(struct output *output)
{
        struct drm_mode_set *mode_set = output->mode_set;
        struct drm_connector *connector;
        struct drm_device *dev;
        struct drm_display_mode *want, *taken;

        if (!mode_set || !output->buffer || !mode_set->mode)
                return;
        if (mode_set->mode->hdisplay == (int)output->width &&
            mode_set->mode->vdisplay == (int)output->height)
        {
                mode_set->fb = output->buffer->fb;
                return;
        }
        connector = modeset_connector(mode_set);
        if (!connector)
                return;
        dev = output->canvas->client.dev;
        mutex_lock(&dev->mode_config.mutex);
        want = output_pick_mode(connector, 0, (int)output->width, (int)output->height, true, 0);
        taken = output_mode_take(dev, want);
        mutex_unlock(&dev->mode_config.mutex);
        if (!taken)
                return;
        drm_mode_destroy(dev, mode_set->mode);
        mode_set->mode = taken;
        mode_set->fb = output->buffer->fb;
}

// Walk each unique card on desktop.outputs (a card's outputs are consecutive); caller initializes seen = null.
#define desktop_for_each_card(output, seen)                        \
        list_for_each_entry((output), &desktop.outputs, link)      \
                if ((output)->canvas == (seen))                    \
                        continue;                                  \
                else if (((seen) = (output)->canvas), 0)           \
                        ;                                          \
                else

static struct drm_client_buffer *output_buffer_make(struct canvas *canvas,
                                                    struct drm_mode_set *mode_set,
                                                    unsigned int width, unsigned int height,
                                                    u32 *opaque)
{
        u32 format = canvas_plane_pick_format(mode_set->crtc->primary,
                                              DRM_FORMAT_XRGB8888, DRM_FORMAT_ARGB8888);
        struct drm_client_buffer *buffer;

        if (format == DRM_FORMAT_INVALID)
                return ERR_PTR(-ENODEV);
        buffer = drm_client_buffer_create_dumb(&canvas->client, width, height, format);
        if (IS_ERR(buffer))
                return buffer;
        *opaque = format == DRM_FORMAT_ARGB8888 ? 0xff000000 : 0;
        return buffer;
}

static _Bool output_grow(struct output *output, struct drm_mode_set *mode_set, _Bool either)
{
        unsigned int width, height;
        u32 opaque;
        struct drm_client_buffer *fresh;

        if (!mode_set || !mode_set->mode || !mode_set->crtc || !mode_set->crtc->primary)
                return false;
        width = mode_set->mode->hdisplay;
        height = mode_set->mode->vdisplay;
        if (either ? width == output->width && height == output->height
                   : (unsigned long)width * height <= (unsigned long)output->width * output->height)
                return false;
        fresh = output_buffer_make(output->canvas, mode_set, width, height, &opaque);
        if (IS_ERR(fresh))
                return false;
        if (output->replaced)
                drm_client_buffer_delete(output->buffer);
        else
                output->replaced = output->buffer;
        output->buffer = fresh;
        output->width = width;
        output->height = height;
        output->opaque = opaque;
        mode_set->fb = fresh->fb;
        return true;
}

static void desktop_place_outputs(void);
static void output_drop(struct output *output);
static struct output *output_add(struct canvas *canvas, struct drm_mode_set *mode_set);
static void output_disable_modeset(struct drm_device *dev, struct drm_mode_set *mode_set);

static _Bool output_bring_up(struct canvas *canvas, struct drm_mode_set *mode_set)
{
        struct drm_connector *connector = modeset_connector(mode_set);
        struct output *output;

        pr_info(CANVAS_LOG "screen %s %ux%u at %u Hz, drawn %ux, %u mode(s) offered\n",
                connector && connector->name ? connector->name : "?",
                mode_set->mode->hdisplay, mode_set->mode->vdisplay, drm_mode_vrefresh(mode_set->mode),
                desktop.scale, connector ? output_mode_count(connector) : 0);
        output = output_add(canvas, mode_set);
        if (!output)
        {
                output_disable_modeset(canvas->client.dev, mode_set);
                return false;
        }
        list_add_tail(&output->link, &desktop.outputs);
        return true;
}

// Hotplug handler on moonwater/plug: grow/add/drop outputs on real cards without dropping live CRTCs.
static int canvas_rebind(struct canvas *canvas)
{
        struct drm_client_dev *client = &canvas->client;
        struct drm_mode_set *mode_set;
        struct output *output;
        _Bool placed = false, retarget = xchg(&canvas->retarget, false);

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
                output = output_for_modeset(canvas, mode_set);
                if (!mode_set->mode)
                {
                        if (output)
                        {
                                pr_info(CANVAS_LOG "a screen went, and its %ux%u output with it\n",
                                        output->width, output->height);
                                output_drop(output);
                                placed = true;
                        }
                }
                else if (output)
                {
                        if (output_grow(output, mode_set, retarget))
                                placed = true;
                        else
                                output_attach(output);
                }
                else if (output_bring_up(canvas, mode_set))
                        placed = true;
        }
        mutex_unlock(&client->modeset_mutex);

        placed |= retarget;
        if (placed)
        {
                desktop_place_outputs();
                canvas_scale_refresh();
                canvas_modes_keep();
                desktop_redraw();
        }
        else if (!desktop_lit() && !desktop_taken())
        {
                pr_info(CANVAS_LOG "hotplug: the screen is not showing the desktop, drawing it again\n");
                desktop_redraw();
        }
        return 0;
}

static void desktop_place_outputs(void)
{
        struct output *output;
        int x = 0, height = 0;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                x += (int)output->width;
                height = max(height, (int)output->height);
        }
        desktop.width = x;
        desktop.height = height;
}

static struct output *output_add(struct canvas *canvas, struct drm_mode_set *mode_set)
{
        unsigned int width = mode_set->mode->hdisplay, height = mode_set->mode->vdisplay;
        struct drm_client_buffer *buffer;
        struct output *output;
        u32 opaque;

        buffer = output_buffer_make(canvas, mode_set, width, height, &opaque);
        if (IS_ERR(buffer))
        {
                if (PTR_ERR(buffer) == -ENODEV)
                        pr_err(CANVAS_LOG "no 32 bit format on this plane, skipping output\n");
                else
                        pr_err(CANVAS_LOG "could not create a %ux%u scanout buffer\n", width, height);
                return null;
        }
        output = kzalloc(sizeof(*output), GFP_KERNEL);
        if (!output)
        {
                drm_client_buffer_delete(buffer);
                return null;
        }
        output->buffer = buffer;
        output->canvas = canvas;
        output->mode_set = mode_set;
        output->width = width;
        output->height = height;
        output->opaque = opaque;
        mode_set->fb = buffer->fb;
        if (!output_describe(output))
        {
                drm_client_buffer_delete(buffer);
                mode_set->fb = null;
                kfree(output);
                return null;
        }
        return output;
}

// Clear an unusable modeset so it does not cause the card's entire atomic commit to fail.
static void output_disable_modeset(struct drm_device *dev, struct drm_mode_set *mode_set)
{
        if (mode_set->mode)
        {
                drm_mode_destroy(dev, mode_set->mode);
                mode_set->mode = null;
        }
        mode_set->fb = null;
        for (unsigned int i = 0; mode_set->connectors && i < (unsigned int)mode_set->num_connectors; i++)
        {
                drm_connector_put(mode_set->connectors[i]);
                mode_set->connectors[i] = null;
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

// Re-attach framebuffers before commit (probes clear mode_set->fb).
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

// Commit every card once; skip re-committing `known` whose result was already `known_set`.
static _Bool desktop_commit_known(struct canvas *known, int known_set)
{
        struct canvas *committed = null;
        struct output *output;
        _Bool complete = true;

        desktop_attach_buffers();
        desktop_for_each_card(output, committed)
        {
                int set = committed == known ? known_set : drm_client_modeset_commit(&committed->client);
                if (!set)
                {
                        struct output *grown;
                        list_for_each_entry(grown, &desktop.outputs, link)
                                if (grown->canvas == committed && grown->replaced)
                                {
                                        drm_client_buffer_delete(grown->replaced);
                                        grown->replaced = null;
                                }
                }
                if (set == -EBUSY || set == -EACCES)
                {
                        desktop.suspended = true;
                        canvas_thread_wake();
                }
                if (set)
                        complete = false;
                if (set == -EBUSY || set == committed->set_result)
                        continue;
                committed->set_result = set;
                if (set)
                        pr_info(CANVAS_LOG "%ux%u would not go on the screen (%d)\n", output->width, output->height, set);
                else
                        pr_info(CANVAS_LOG "%ux%u is on the screen\n", output->width, output->height);
        }
        return complete;
}

// Paint the white ground + centered wordmark via scalar writes (safe for device memory) and flush.
static void output_ground(struct output *output)
{
        struct splash_image mark = {SPLASH_MARK_W, SPLASH_MARK_H, null};
        struct splash_place place;
        struct iosys_map map;
        unsigned long pitch;

        if (!output_map(output, &map))
                return;
        pitch = output->buffer->fb->pitches[0] / sizeof(u32);
        canvas_rect_fill(map.vaddr, pitch, output->width, output->height, CANVAS_GROUND | output->opaque);
        splash_place(&mark, output->width, output->height, &place);
        for (unsigned int row = 0; row < place.height; row++)
        {
                unsigned long offset = ((unsigned long)(place.y + row) * pitch + place.x) * sizeof(u32);
                for (unsigned int col = 0; col < place.width; col++)
                {
                        unsigned long at = (unsigned long)(place.source_y + row) * SPLASH_MARK_W + (place.source_x + col);
                        u32 grey = 255u - (((unsigned int)splash_mark[at >> 2] >> (6 - 2 * (at & 3))) & 3u) * 85u;
                        iosys_map_wr(&map, offset + (unsigned long)col * sizeof(u32), u32,
                                     (grey << 16) | (grey << 8) | grey | output->opaque);
                }
        }
        drm_client_buffer_vunmap_local(output->buffer);
        drm_client_buffer_flush(output->buffer, null);
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
        desktop_commit_known(null, 0);
}

/*
        The scale the desktop is drawn at, with desktop.lock held, and whether it changed.
*/
static _Bool desktop_rescale(unsigned int scale)
{
        if (scale == desktop.scale)
                return false;
        desktop.scale = scale;
        return true;
}

static _Bool canvas_scale_refresh(void)
{
        unsigned int dpi;
        return !canvas_scale_setting && desktop_rescale(canvas_scale_auto(null, &dpi));
}

/*
        Whether a program other than this one is master of a card Canvas draws on (or machine asleep).
*/
static _Bool desktop_taken(void)
{
        struct canvas *checked = null;
        struct output *output;
        _Bool taken = false;

        if (desktop.asleep)
                return true;
        desktop_for_each_card(output, checked)
        {
                struct drm_device *dev = checked->client.dev;
                mutex_lock(&dev->master_mutex);
                taken = dev->master != null;
                mutex_unlock(&dev->master_mutex);
                if (taken)
                        break;
        }
        return taken;
}

static void desktop_resume(void)
{
        desktop.suspended = false;
        WRITE_ONCE(canvas_yielded, false);
        desktop_redraw();
        pr_info(CANVAS_LOG "the card is Canvas's again, and the desktop is drawn\n");
}

static _Bool output_lit_locked(struct output *output)
{
        struct drm_crtc *crtc = output->mode_set ? output->mode_set->crtc : null;
        struct drm_plane *primary = crtc ? crtc->primary : null;

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
                struct drm_connector *connector = modeset_connector(output->mode_set);
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
                pr_info(CANVAS_LOG "%s: %s: crtc active %d enabled %d, %s, connector status %d dpms %d, buffer %s, last commit %d\n",
                        tag, connector && connector->name ? connector->name : "?",
                        active, enable, lit ? "primary shows ours" : "primary does NOT show ours",
                        connector ? (int)connector->status : -1, connector ? connector->dpms : -1,
                        mapped ? (map.is_iomem ? "maps (device memory)" : "maps") : "does NOT map",
                        output->canvas->set_result);
        }
}

static void desktop_dpms(int mode)
{
        struct canvas *done = null;
        struct output *output;

        desktop_attach_buffers();
        desktop_for_each_card(output, done)
                pr_info(CANVAS_LOG "screens %s: %d\n", mode == DRM_MODE_DPMS_ON ? "on" : "off",
                        drm_client_modeset_dpms(&done->client, mode));
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
                if (mode_set->mode && output_bring_up(canvas, mode_set))
                        count++;
        mutex_unlock(&client->modeset_mutex);
        if (!count)
                return -ENODEV;
        desktop_place_outputs();
        return 0;
}

static int canvas_start(struct canvas *canvas)
{
        int ret;

        for (unsigned int attempt = 0; ; attempt++)
        {
                ret = canvas_build(canvas, !attempt && READ_ONCE(canvas_modes_largest));
                if (ret)
                {
                        pr_err("[moonwater canvas] " "no screen to draw on (%d), leaving the display alone\n", ret);
                        return ret;
                }
                desktop_recompose();
                desktop_attach_buffers();
                ret = drm_client_modeset_commit(&canvas->client);
                if (!ret || ret == -EBUSY)
                        break;
                if (attempt)
                        pr_err("[moonwater canvas] " "no mode would set (%d), leaving the display alone\n", ret);
                else
                        pr_info("[moonwater canvas] " "that mode would not set (%d), taking the offered one\n", ret);
                canvas_release(canvas);
                if (attempt)
                        return ret;
        }

        canvas_modes_keep();
        desktop_commit_known(canvas, ret);
        {
                struct output *output;
                unsigned int count = 0;
                list_for_each_entry(output, &desktop.outputs, link)
                        count++;
                pr_info(CANVAS_LOG "desktop %dx%d, %u output(s)\n", desktop.width, desktop.height, count);
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

/* ---- keys: rescue chords (Ctrl-Alt-Backspace / Ctrl-Alt-Shift-Backspace) ---- */

#define KEY_TABLE 128

#define CHORD_SHIFT 2u
#define CHORD_CONTROL 4u
#define CHORD_ALT 8u
#define CHORD_ALTGR 64u
#define KEY_HELD_ALTGR 256u // separate from CHORD_CONTROL << 4 (64)

static unsigned short key_mod[KEY_TABLE] = {
    [KEY_LEFTSHIFT] = CHORD_SHIFT,
    [KEY_RIGHTSHIFT] = CHORD_SHIFT << 4,
    [KEY_LEFTCTRL] = CHORD_CONTROL,
    [KEY_RIGHTCTRL] = CHORD_CONTROL << 4,
    [KEY_LEFTALT] = CHORD_ALT,
    [KEY_RIGHTALT] = KEY_HELD_ALTGR,
};

static unsigned int *keyboard_held(struct input_handle *handle);
static unsigned int keyboard_modifiers(void);
static void canvas_exit_request(void);

static void keyboard_event(struct input_handle *handle, unsigned int code, int value)
{
        unsigned int modifiers = (unsigned int)atomic_read(&desktop.modifiers);
        unsigned int bit = code < KEY_TABLE ? key_mod[code] : 0;

        if (bit)
        {
                unsigned int *held = keyboard_held(handle);
                *held = value ? *held | bit : *held & ~bit;
                atomic_set(&desktop.modifiers, (int)keyboard_modifiers());
                return;
        }
        if (code != KEY_BACKSPACE || value != 1)
                return;

        modifiers &= CHORD_ALT | CHORD_SHIFT | CHORD_CONTROL;
        if (modifiers == (CHORD_ALT | CHORD_SHIFT | CHORD_CONTROL))
                canvas_exit_request();
        else if (modifiers == (CHORD_ALT | CHORD_CONTROL))
        {
                atomic_set(&desktop.reclaim, 1);
                canvas_thread_wake();
        }
}

/* ---- client: a client, and what it is allowed to ask for ---- */

static struct workqueue_struct *canvas_plug_wq;
static void canvas_claimed_forget(struct drm_device *dev);

static void canvas_plug_ensure(void)
{
        struct workqueue_struct *made;

        if (READ_ONCE(canvas_plug_wq))
                return;
        made = alloc_ordered_workqueue("moonwater/plug", 0);
        if (made && cmpxchg(&canvas_plug_wq, null, made))
                destroy_workqueue(made);
}

static void canvas_plug_queue(struct canvas *canvas)
{
        queue_work(canvas_plug_wq ? canvas_plug_wq : system_unbound_wq, &canvas->plug);
}

static void canvas_plug_work(struct work_struct *work)
{
        struct canvas *canvas = container_of(work, struct canvas, plug);

        rt_mutex_lock(&desktop.lock);
        if (canvas->started && READ_ONCE(desktop.asleep))
                canvas->replug = true;
        else if (!canvas->started)
                canvas->started = (canvas_start(canvas) == 0);
        else
                (void)canvas_rebind(canvas);
        rt_mutex_unlock(&desktop.lock);
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

// Notify rover of connector changes and total/external display counts.
static void canvas_rules_hotplug(struct canvas *canvas)
{
        struct drm_connector_list_iter iter;
        struct drm_connector *connector;
        struct canvas *each;
        u32 now = 0, before = canvas->rules_connected, changed;
        unsigned int count = 0, external = 0, total = 0, outside = 0;
        char name[32];

        drm_connector_list_iter_begin(canvas->client.dev, &iter);
        drm_for_each_connector_iter(connector, &iter)
        {
                if (connector->index >= 32 || connector->status != connector_status_connected)
                        continue;
                now |= 1u << connector->index;
                count++;
                if (connector->connector_type != DRM_MODE_CONNECTOR_eDP &&
                    connector->connector_type != DRM_MODE_CONNECTOR_LVDS &&
                    connector->connector_type != DRM_MODE_CONNECTOR_DSI)
                        external++;
        }
        drm_connector_list_iter_end(&iter);

        mutex_lock(&canvas_list_lock);
        canvas->rules_connected = now;
        canvas->rules_count = count;
        canvas->rules_external = external;
        list_for_each_entry(each, &canvas_list, link)
        {
                total += each->rules_count;
                outside += each->rules_external;
        }
        mutex_unlock(&canvas_list_lock);

        changed = now ^ before;
        if (!changed)
        {
                moonwater_rules_display(total, outside, NULL, false);
                return;
        }
        drm_connector_list_iter_begin(canvas->client.dev, &iter);
        drm_for_each_connector_iter(connector, &iter)
        {
                if (connector->index >= 32 || !(changed & (1u << connector->index)))
                        continue;
                strscpy(name, connector->name ? connector->name : "", sizeof(name));
                moonwater_rules_display(total, outside, name, (now >> connector->index) & 1);
        }
        drm_connector_list_iter_end(&iter);
}

static int client_hotplug(struct drm_client_dev *client)
{
        struct canvas *canvas = canvas_from_client(client);

        lockdep_assert_held(&client->dev->clientlist_mutex);
        canvas_rules_hotplug(canvas);
        canvas_plug_ensure();
        canvas_plug_queue(canvas);
        return 0;
}

static int client_restore(struct drm_client_dev *client, _Bool in_atomic)
{
        struct canvas *canvas = canvas_from_client(client);

        if (in_atomic)
                return -EBUSY;
        rt_mutex_lock(&desktop.lock);
        if (canvas->started && !READ_ONCE(desktop.asleep) && (desktop.suspended || !desktop_lit()))
                desktop_resume();
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
                if (canvas->client.dev == dev)
                {
                        held = true;
                        break;
                }
        mutex_unlock(&canvas_list_lock);
        return held;
}

static struct canvas *canvas_take_over(struct drm_device *dev)
{
        struct canvas *canvas;
        _Bool first;

        if (!drm_core_check_feature(dev, DRIVER_MODESET) || canvas_holds(dev))
                return null;
        canvas = kzalloc(sizeof(*canvas), GFP_KERNEL);
        if (!canvas)
                return null;
        canvas->set_result = 1;
        if (drm_client_init(dev, &canvas->client, "moonwater", &client_funcs))
        {
                kfree(canvas);
                return null;
        }
        canvas_plug_ensure();
        INIT_WORK(&canvas->plug, canvas_plug_work);

        mutex_lock(&canvas_list_lock);
        first = list_empty(&canvas_list);
        list_add_tail(&canvas->link, &canvas_list);
        if (first && !canvas_thread_start())
        {
                list_del(&canvas->link);
                mutex_unlock(&canvas_list_lock);
                drm_client_release(&canvas->client);
                return null;
        }
        mutex_unlock(&canvas_list_lock);
        return canvas;
}

#define CANVAS_RETRY_MS 5
#define CANVAS_ATTEMPTS 1000
#define CANVAS_SETTLE 100

// Unregister either Canvas's own client (own=true) or every other in-kernel client (own=false).
static void canvas_clients_drop(struct drm_device *dev, _Bool own)
{
        struct drm_client_dev *client, *next;

        mutex_lock(&dev->clientlist_mutex);
        list_for_each_entry_safe(client, next, &dev->clientlist, list)
        {
                if ((client->funcs == &client_funcs) != own)
                        continue;
                list_del(&client->list);
                if (own)
                        client_unregister(client);
                else if (client->funcs && client->funcs->unregister)
                        client->funcs->unregister(client);
                else
                        drm_client_release(client);
        }
        mutex_unlock(&dev->clientlist_mutex);
}

// Find dev's current DRM master: sanitized comm, tgid, and optional referenced pids.
static int canvas_master_find(struct drm_device *dev, char *command, positive room,
                              _Bool in_current_ns, struct pid **out_process,
                              struct pid **out_session)
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
                        holder = in_current_ns ? task_tgid_nr_ns(task, task_active_pid_ns(current))
                                               : task_tgid_nr(task);
                        if (out_process)
                                *out_process = get_pid(task_tgid(task));
                        if (out_session)
                                *out_session = get_pid(task_session(task));
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
static unsigned int canvas_attempts, canvas_settled_at;
static unsigned long canvas_claimed; // atomic bitmask of claimed DRM minors 0..63
static_assert(BITS_PER_LONG >= 64, "a card minor up to 63 is a bit of canvas_claimed");

static void canvas_firmware_yield(void);

static void canvas_claimed_forget(struct drm_device *dev)
{
        int minor;

        if (!dev || !dev->primary)
                return;
        minor = dev->primary->index;
        if (minor >= 0 && minor < 64)
                clear_bit(minor, &canvas_claimed);
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

// Synchronous last fput so the card's master is released before drm_client_register / retries.
static void canvas_node_close(struct file *filp) { __fput_sync(filp); }

static int canvas_claim(const char *path, unsigned int minor,
                        struct canvas_control *on, _Bool firmware_ok)
{
        struct file *filp;
        struct drm_file *file_priv;
        struct canvas *canvas;
        struct drm_device *dev;

        if (test_bit(minor, &canvas_claimed))
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

        dev = file_priv->minor->dev;
        if (canvas_is_firmware(dev))
        {
                int skip = !firmware_ok ? -EAGAIN : canvas_has_native() ? -ENODEV
                           : (!on && canvas_attempts < CANVAS_SETTLE) ? -EAGAIN : 0;
                if (skip)
                {
                        canvas_node_close(filp);
                        return skip;
                }
        }
        if (on && drm_core_check_feature(dev, DRIVER_MODESET))
        {
                if (!drm_is_current_master(file_priv))
                {
                        if (!on->master_pid && !on->master_command[0])
                                on->master_pid = canvas_master_find(
                                    dev, on->master_command, sizeof(on->master_command), true, null, null);
                        canvas_node_close(filp);
                        return -EACCES;
                }
                canvas_clients_drop(dev, false);
        }

        canvas = canvas_take_over(dev);
        if (canvas)
                set_bit(minor, &canvas_claimed);
        canvas_node_close(filp);
        if (!canvas)
                return -EBUSY;

        drm_client_register(&canvas->client);
        pr_info(CANVAS_LOG "attached to %s, %s display\n",
                canvas->client.dev->driver->name,
                canvas_is_virtual(canvas->client.dev) ? "a guest's" : "a real");
        if (!canvas_is_firmware(canvas->client.dev))
                canvas_firmware_yield();
        return 0;
}

static unsigned int canvas_claim_range(struct canvas_control *on,
                                       unsigned int *refused, _Bool firmware_ok)
{
        char path[24];
        unsigned int taken = 0;

        for (unsigned int minor = 0; minor < 64; minor++)
        {
                if (test_bit(minor, &canvas_claimed))
                        continue;
                positive_into_string(memory_copy_apart_end(path, "/dev/dri/card",
                                                           sizeof("/dev/dri/card") - 1), minor);
                switch (canvas_claim(path, minor, on, firmware_ok))
                {
                case 0: taken++; break;
                case -EACCES: if (refused) (*refused)++; break;
                }
        }
        return taken;
}

static unsigned int canvas_claim_all(struct canvas_control *on, unsigned int *refused)
{
        unsigned int taken = canvas_claim_range(on, refused, false);
        return taken ? taken : canvas_claim_range(on, refused, true);
}

static COLD void canvas_probe(struct work_struct *work)
{
        _Bool was_on = canvas_is_on();

        scoped_with_init_fs()
                canvas_claim_all(null, null);
        if (!was_on && canvas_is_on())
                moonwater_bind_fire(SPARK_BIND_CANVAS_ON);
        canvas_attempts++;

        if (!canvas_settled_at && !list_empty(&canvas_list))
                canvas_settled_at = canvas_attempts + CANVAS_SETTLE;
        if (canvas_settled_at && canvas_attempts >= canvas_settled_at)
                return;
        if (canvas_attempts >= CANVAS_ATTEMPTS)
        {
                pr_info(CANVAS_LOG "gave up waiting for a card\n");
                return;
        }
        schedule_delayed_work(&canvas_probe_work, msecs_to_jiffies(CANVAS_RETRY_MS));
}

// Lock order: canvas_control_lock -> dev->clientlist_mutex -> canvas_list_lock -> desktop.lock -> master_mutex.
static DEFINE_MUTEX(canvas_control_lock);

_Bool canvas_is_on(void)
{
        _Bool on;

        mutex_lock(&canvas_list_lock);
        on = !list_empty(&canvas_list);
        mutex_unlock(&canvas_list_lock);
        return on;
}

static struct drm_device *canvas_next_dev(_Bool firmware_only)
{
        struct canvas *canvas;
        struct drm_device *dev = null;

        mutex_lock(&canvas_list_lock);
        list_for_each_entry(canvas, &canvas_list, link)
                if (!firmware_only || canvas_is_firmware(canvas->client.dev))
                {
                        dev = canvas->client.dev;
                        drm_dev_get(dev);
                        break;
                }
        mutex_unlock(&canvas_list_lock);
        return dev;
}

static void canvas_firmware_yield(void)
{
        struct drm_device *dev;

        while ((dev = canvas_next_dev(true)))
        {
                pr_info(CANVAS_LOG "dropping %s, a GPU is attached\n", dev->driver->name);
                canvas_clients_drop(dev, true);
                drm_dev_put(dev);
        }
}

static long canvas_turn_off(void)
{
        struct drm_device *released[8], *dev;
        unsigned int count = 0;

        if (mutex_lock_killable(&canvas_control_lock))
                return -EINTR;
        if (!canvas_is_on())
        {
                mutex_unlock(&canvas_control_lock);
                return -EALREADY;
        }
#ifdef CONFIG_MOONWATER_CANVAS_AUTOSTART
        cancel_delayed_work_sync(&canvas_probe_work);
#endif
        mutex_lock(&canvas_list_lock);
        atomic_set(&desktop.reclaim, 0);
        canvas_thread_stop();
        mutex_unlock(&canvas_list_lock);

        rt_mutex_lock(&desktop.lock);
        desktop.suspended = false;
        WRITE_ONCE(canvas_yielded, false);
        rt_mutex_unlock(&desktop.lock);

        while ((dev = canvas_next_dev(false)))
        {
                canvas_clients_drop(dev, true);
                if (count < ARRAY_SIZE(released))
                        released[count++] = dev;
                else
                        drm_dev_put(dev);
        }
        WRITE_ONCE(canvas_claimed, 0);

        for (unsigned int i = 0; i < count; i++)
        {
#ifndef MODULE
                if (released[i]->registered && !released[i]->fb_helper)
                        drm_fbdev_client_setup(released[i], null);
#endif
                drm_dev_put(released[i]);
        }
        pr_info(CANVAS_LOG "off: %u card(s) given back to the console\n", count);
        mutex_unlock(&canvas_control_lock);
        moonwater_bind_fire(SPARK_BIND_CANVAS_OFF);
        return 0;
}

static long canvas_turn_on(struct canvas_control *answer)
{
        unsigned int taken, refused = 0;

        if (mutex_lock_killable(&canvas_control_lock))
                return -EINTR;
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

static long canvas_state(struct canvas_control *answer)
{
        struct canvas *canvas;
        struct output *output;

        if (mutex_lock_killable(&canvas_list_lock))
                return -EINTR;
        list_for_each_entry(canvas, &canvas_list, link)
                if (!answer->cards++)
                        strscpy(answer->driver, canvas->client.dev->driver->name, sizeof(answer->driver));
        mutex_unlock(&canvas_list_lock);
        answer->running = answer->cards != 0;

        if (rt_mutex_lock_killable(&desktop.lock))
                return -EINTR;
        answer->suspended = desktop_taken();
        answer->thread_passes = READ_ONCE(canvas_passes);
        answer->scale = desktop.scale;
        answer->scale_setting = canvas_scale_setting;
        answer->scale_origin = canvas_scale_origin;
        (void)canvas_scale_auto(null, &answer->scale_dpi);
        answer->modes = READ_ONCE(canvas_modes_largest) ? SPARK_CANVAS_MODES_LARGEST : SPARK_CANVAS_MODES_PREFERRED;

        list_for_each_entry(output, &desktop.outputs, link)
        {
                struct canvas_output_state *state;
                struct drm_mode_set *set = output->mode_set;
                struct drm_connector *connector = modeset_connector(set);

                if (answer->output_count == SPARK_CANVAS_OUTPUTS)
                        break;
                state = &answer->output[answer->output_count++];
                state->width = output->width;
                state->height = output->height;
                if (set && set->mode)
                        state->refresh = drm_mode_vrefresh(set->mode);
                if (connector)
                        strscpy(state->connector, connector->name, sizeof(state->connector));
        }
        rt_mutex_unlock(&desktop.lock);
        return 0;
}

static long canvas_scale_set(unsigned int setting)
{
        unsigned int dpi;

        if (setting > SPARK_CANVAS_SCALE_MOST)
                return -EINVAL;
        if (rt_mutex_lock_killable(&desktop.lock))
                return -EINTR;
        canvas_scale_setting = setting;
        canvas_scale_origin = SPARK_CANVAS_ORIGIN_SET;
        if (!list_empty(&desktop.outputs))
                desktop_rescale(setting ? setting : canvas_scale_auto(null, &dpi));
        rt_mutex_unlock(&desktop.lock);
        return 0;
}

static long canvas_modes_set(unsigned int modes)
{
        struct canvas *canvas;

        if (modes > SPARK_CANVAS_MODES_PREFERRED)
                return -EINVAL;
        if (READ_ONCE(canvas_modes_largest) == (modes == SPARK_CANVAS_MODES_LARGEST))
                return 0;
        canvas_plug_ensure();
        if (mutex_lock_killable(&canvas_list_lock))
                return -EINTR;
        WRITE_ONCE(canvas_modes_largest, modes == SPARK_CANVAS_MODES_LARGEST);
        list_for_each_entry(canvas, &canvas_list, link)
        {
                canvas->retarget = true;
                queue_work(canvas_plug_wq ? canvas_plug_wq : system_unbound_wq, &canvas->plug);
        }
        mutex_unlock(&canvas_list_lock);
        return 0;
}

/* ---- pointer: the pointer ---- */

static struct task_struct __rcu *canvas_thread;
static _Bool pointer_handler_registered;

#define POINTER_OPEN_TRIES 30

struct pointer_handle
{
        struct input_handle handle;
        struct list_head link;
        struct delayed_work reopen;
        int opened;
        unsigned int tries, modifiers;
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
        return ((held | (held >> 4)) & (CHORD_SHIFT | CHORD_CONTROL | CHORD_ALT)) |
               ((held & KEY_HELD_ALTGR) ? CHORD_ALTGR : 0);
}

static _Bool pointer_hears(unsigned int type, unsigned int code)
{
        return type == EV_KEY && code < KEY_TABLE && (key_mod[code] || code == KEY_BACKSPACE);
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
        const char *name = pointer_device_name(&pointer->handle);
        int ret = input_open_device(&pointer->handle);

        pointer->opened = ret;
        if (!ret)
        {
                if (pointer->tries)
                        pr_info(CANVAS_LOG "input: %s opened on try %u\n", name, pointer->tries + 1);
                else
                        pr_info(CANVAS_LOG "input: %s\n", name);
                return;
        }
        if (!pointer->tries)
                pr_info(CANVAS_LOG "input: %s would not open (%d), trying again\n", name, ret);
        if (++pointer->tries < POINTER_OPEN_TRIES)
                schedule_delayed_work(&pointer->reopen, HZ);
        else
                pr_info(CANVAS_LOG "input: %s would not open (%d), given up\n", name, ret);
}

static COLD int pointer_connect(struct input_handler *handler, struct input_dev *dev,
                                const struct input_device_id *id)
{
        struct pointer_handle *pointer = kzalloc(sizeof(*pointer), GFP_KERNEL);
        unsigned long flags;
        int ret;

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

        // Open asynchronously so USB HID open sleeps do not stall bus enumeration.
        pointer->opened = -EINPROGRESS;
        schedule_delayed_work(&pointer->reopen, 0);
        return 0;
}

static COLD void pointer_disconnect(struct input_handle *handle)
{
        struct pointer_handle *pointer = pointer_handle_of(handle);
        unsigned long flags;

        cancel_delayed_work_sync(&pointer->reopen);
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

// Mute VT keyboard (K_OFF) on all consoles while Canvas is active; restore original modes on exit.
#ifdef CONFIG_VT

static DEFINE_SPINLOCK(canvas_vt_lock);
static _Bool canvas_vt_owned, canvas_vt_watching;
static signed char canvas_vt_mode[MAX_NR_CONSOLES] = {[0 ... MAX_NR_CONSOLES - 1] = -1};

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

static int canvas_keyboard_follow(struct notifier_block *block, unsigned long event, void *data)
{
        struct vt_notifier_param *param = data;
        unsigned long flags;

        if ((event != VT_ALLOCATE && event != VT_UPDATE) || !param || !param->vc)
                return NOTIFY_DONE;
        spin_lock_irqsave(&canvas_vt_lock, flags);
        if (canvas_vt_owned)
                canvas_keyboard_mute(param->vc->vc_num);
        spin_unlock_irqrestore(&canvas_vt_lock, flags);
        return NOTIFY_DONE;
}

static struct notifier_block canvas_keyboard_watch = {.notifier_call = canvas_keyboard_follow};

static void canvas_keyboard_take(void)
{
        unsigned long flags;

        if (!canvas_vt_watching && !register_vt_notifier(&canvas_keyboard_watch))
                canvas_vt_watching = true;
        spin_lock_irqsave(&canvas_vt_lock, flags);
        canvas_vt_owned = true;
        for (unsigned int console = 0; console < MAX_NR_CONSOLES; console++)
                canvas_keyboard_mute(console);
        spin_unlock_irqrestore(&canvas_vt_lock, flags);
}

static void canvas_keyboard_give(void)
{
        unsigned long flags;

        if (canvas_vt_watching)
        {
                unregister_vt_notifier(&canvas_keyboard_watch);
                canvas_vt_watching = false;
        }
        spin_lock_irqsave(&canvas_vt_lock, flags);
        canvas_vt_owned = false;
        for (unsigned int console = 0; console < MAX_NR_CONSOLES; console++)
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
    {.flags = INPUT_DEVICE_ID_MATCH_EVBIT, .evbit = {BIT_MASK(EV_KEY)}},
    {},
};

static struct input_handler pointer_handler = {
    .event = pointer_event,
    .connect = pointer_connect,
    .disconnect = pointer_disconnect,
    .name = "moonwater",
    .id_table = pointer_ids,
};

static int canvas_pm_again_left;

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
                struct canvas *seen = null;

                WRITE_ONCE(desktop.asleep, false);
                if (desktop.suspended && !desktop_taken())
                {
                        desktop_resume();
                        desktop_report("wake");
                }
                else
                        pr_info(CANVAS_LOG "wake: another program has the card, the redraw waits for it\n");

                desktop_for_each_card(output, seen)
                        if (seen->replug)
                        {
                                seen->replug = false;
                                canvas_plug_queue(seen);
                        }
        }
        rt_mutex_unlock(&desktop.lock);

        if (sleeping)
        {
                cancel_delayed_work_sync(&canvas_pm_again);
                pr_info(CANVAS_LOG "sleep: quiet\n");
        }
        else
        {
                canvas_thread_wake();
                canvas_pm_again_left = 2;
                schedule_delayed_work(&canvas_pm_again, msecs_to_jiffies(500));
        }
}

static int canvas_pm_event(struct notifier_block *block, unsigned long event, void *unused)
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

static struct notifier_block canvas_pm_notifier = {.notifier_call = canvas_pm_event};
static _Bool canvas_pm_registered;

static void canvas_thread_stop(void)
{
        struct task_struct *thread = rcu_dereference_protected(
            canvas_thread, lockdep_is_held(&canvas_list_lock));

        if (!thread)
                return;
        RCU_INIT_POINTER(canvas_thread, null);
        if (canvas_pm_registered)
        {
                unregister_pm_notifier(&canvas_pm_notifier);
                canvas_pm_registered = false;
        }
        WRITE_ONCE(desktop.asleep, false);
        cancel_delayed_work_sync(&canvas_pm_again);
        if (pointer_handler_registered)
        {
                input_unregister_handler(&pointer_handler);
                pointer_handler_registered = false;
        }
        canvas_keyboard_give();
        synchronize_rcu();
        kthread_stop(thread);
        if (canvas_plug_wq)
        {
                destroy_workqueue(canvas_plug_wq);
                canvas_plug_wq = null;
        }
}

static void canvas_thread_wake(void)
{
        struct task_struct *thread;

        rcu_read_lock();
        thread = rcu_dereference(canvas_thread);
        if (thread)
                wake_up_process(thread);
        rcu_read_unlock();
}

#define CANVAS_SUSPENDED_POLL_MS 250

/*
        The session of whatever holds the display, ended.
*/
static void canvas_session_end(struct pid *process, struct pid *session)
{
        struct task_struct *task, *init;
        struct pid *shared;

        kill_pid(process, SIGKILL, 1);
        rcu_read_lock();
        init = find_task_by_pid_ns(1, &init_pid_ns);
        shared = init ? task_session(init) : null;
        if (session && session != shared)
                do_each_pid_task(session, PIDTYPE_SID, task)
                {
                        if (!is_global_init(task) && !(task->flags & PF_KTHREAD))
                                group_send_sig_info(SIGKILL, SEND_SIG_PRIV, task, PIDTYPE_TGID);
                }
                while_each_pid_task(session, PIDTYPE_SID, task);
        rcu_read_unlock();
}

static void canvas_master_end(struct drm_device *dev)
{
        struct pid *process = null, *session = null;
        char command[TASK_COMM_LEN] = "";
        int number = canvas_master_find(dev, command, sizeof(command), false, &process, &session);

        if (process)
        {
                pr_info(CANVAS_LOG "Control-Alt-Backspace: %s (pid %d) holds the display, and it and its session are ended\n",
                        command, number);
                canvas_session_end(process, session);
        }
        put_pid(process);
        put_pid(session);
}

#define CANVAS_RECLAIM_CARDS 4

static void canvas_reclaim(void)
{
        struct drm_device *cards[CANVAS_RECLAIM_CARDS];
        unsigned int count = 0;
        struct canvas *seen = null;
        struct output *output;

        rt_mutex_lock(&desktop.lock);
        desktop_for_each_card(output, seen)
        {
                struct drm_device *dev = seen->client.dev;
                if (count == CANVAS_RECLAIM_CARDS)
                        break;
                drm_dev_get(dev);
                cards[count++] = dev;
        }
        rt_mutex_unlock(&desktop.lock);
        while (count)
        {
                canvas_master_end(cards[--count]);
                drm_dev_put(cards[count]);
        }
}

#define CANVAS_EXIT_WAIT_MS 2000

static void canvas_exit_work(struct work_struct *work)
{
        _Bool taken = false;

        pr_info(CANVAS_LOG "Control-Alt-Shift-Backspace: Canvas off, to the text console\n");
        canvas_reclaim();
        for (unsigned int waited = 0; waited < CANVAS_EXIT_WAIT_MS; waited += 5)
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

static void canvas_exit_request(void) { queue_work(system_dfl_long_wq, &canvas_exit); }

static _Bool canvas_suspend_check(void)
{
        _Bool taken;

        rt_mutex_lock(&desktop.lock);
        taken = desktop_taken();
        WRITE_ONCE(canvas_yielded, taken && !READ_ONCE(desktop.asleep));
        if (taken)
                desktop.suspended = true;
        else if (desktop.suspended)
                desktop_resume();
        rt_mutex_unlock(&desktop.lock);

        if (atomic_xchg(&desktop.reclaim, 0) && taken && !READ_ONCE(desktop.asleep))
                canvas_reclaim();
        return taken;
}

/*
        Looks at who holds the cards four times a second, and immediately on a chord.
*/
static int canvas_loop(void *unused)
{
        while (!kthread_should_stop())
        {
                set_current_state(TASK_IDLE);
                if (!atomic_read(&desktop.reclaim))
                        schedule_timeout(READ_ONCE(desktop.asleep)
                                             ? MAX_SCHEDULE_TIMEOUT
                                             : msecs_to_jiffies(CANVAS_SUSPENDED_POLL_MS));
                __set_current_state(TASK_RUNNING);
                WRITE_ONCE(canvas_passes, canvas_passes + 1);
                canvas_suspend_check();
        }
        return 0;
}

static _Bool canvas_thread_start(void)
{
        static const struct sched_attr canvas_policy = {
            .size = sizeof(struct sched_attr), .sched_policy = SCHED_FIFO, .sched_priority = 1,
        };
        struct task_struct *thread;

        WRITE_ONCE(canvas_yielded, false);
        thread = kthread_run(canvas_loop, null, "moonwater/canvas");
        if (IS_ERR(thread))
        {
                pr_info(CANVAS_LOG "no thread for input\n");
                return false;
        }
        rcu_assign_pointer(canvas_thread, thread);
        WARN_ON_ONCE(sched_setattr_nocheck(thread, &canvas_policy));
        if (!register_pm_notifier(&canvas_pm_notifier))
                canvas_pm_registered = true;
        canvas_keyboard_take();
        if (input_register_handler(&pointer_handler))
        {
                pr_info(CANVAS_LOG "could not register the input handler\n");
                canvas_keyboard_give();
        }
        else
                pointer_handler_registered = true;
        return true;
}

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
        out->canvas = (struct latency_cost){heard.events, heard.samples, heard.total_ns, heard.worst_ns};
        for_each_possible_cpu(cpu)
                out->quiet += READ_ONCE(*per_cpu_ptr(&canvas_quiet, cpu));
}

static long report_canvas(struct canvas_control __user *out)
{
        struct canvas_control control;
        unsigned int request, asked_scale, asked_modes;
        long answer = 0;

        if (copy_from_user(&control, out, sizeof(control)))
                return -EFAULT;
        request = control.request;
        asked_scale = control.scale_setting;
        asked_modes = control.modes;
        if (request > SPARK_CANVAS_SET_MODES || (request >= 3u && request <= 5u))
                return -EINVAL;
        if (request != SPARK_CANVAS_STATUS && !capable(CAP_SYS_ADMIN))
                return -EPERM;

        memory_zero(&control, sizeof(control));
        control.request = request;
        switch (request)
        {
        case SPARK_CANVAS_ON: answer = canvas_turn_on(&control); break;
        case SPARK_CANVAS_OFF: answer = canvas_turn_off(); break;
        case SPARK_CANVAS_SET_SCALE: answer = canvas_scale_set(asked_scale); break;
        case SPARK_CANVAS_SET_MODES: answer = canvas_modes_set(asked_modes); break;
        }
        if (canvas_state(&control))
                return answer ? answer : -EINTR;
        if (copy_to_user(out, &control, sizeof(control)))
                return -EFAULT;
        return answer;
}

/*
        The window table the compositor shares is a memfd, passed root-to-root via /dev/spark.
*/
static DEFINE_SPINLOCK(windows_table_lock);
static struct file *windows_table, *windows_table_giver;

static long windows_table_set(struct file *giver, unsigned long arg)
{
        struct file *table = null, *old;

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
        windows_table_giver = table ? giver : null;
        spin_unlock(&windows_table_lock);
        if (old)
                fput(old);
        return 0;
}

static long windows_table_get(void)
{
        struct file *table;
        int fd;

        if (!capable(CAP_SYS_ADMIN))
                return -EPERM;
        spin_lock(&windows_table_lock);
        table = windows_table ? get_file(windows_table) : null;
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
        struct file *old = null;

        spin_lock(&windows_table_lock);
        if (windows_table_giver == giver)
        {
                old = windows_table;
                windows_table = windows_table_giver = null;
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
        case SPARK_IOCTL_CANVAS: return report_canvas((struct canvas_control __user *)arg);
        case WINDOWS_IOCTL_TABLE_SET: return windows_table_set(file, arg);
        case WINDOWS_IOCTL_TABLE_GET: return windows_table_get();
        }
        return -ENOTTY;
}

void canvas_client_release(struct file *file) { windows_table_release(file); }

void canvas_boot(void)
{
#ifdef CONFIG_MOONWATER_CANVAS_AUTOSTART
        INIT_DELAYED_WORK(&canvas_probe_work, canvas_probe);
#endif
        if (moonwater_settings_flags() & SPARK_SETTINGS_CANVAS_OFF)
        {
                pr_info(CANVAS_LOG "off at boot, as the image's settings say; moonwater desktop on starts it\n");
                return;
        }
#ifdef CONFIG_MOONWATER_CANVAS_AUTOSTART
        schedule_delayed_work(&canvas_probe_work, 0);
#endif
}

void canvas_unload(void)
{
#ifdef CONFIG_MOONWATER_CANVAS_AUTOSTART
        cancel_delayed_work_sync(&canvas_probe_work);
#endif
}
#endif // CANVAS_DECLARE_ONLY
#endif // CANVAS_ABI_ONLY
