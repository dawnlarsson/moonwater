/*
        The Moonwater kernel module: what it registers, and what it dispatches.

        Three subsystems are expanded into this one translation unit and
        this file is the only place that knows about all three at once.
        spark.c is the binary format and the loader, spawn and reports that
        serve it. moonwater.c is the bindings, the machine script and the
        scanner. src/canvas/canvas.c is the compositor. Each keeps to itself;
        the ioctl switch below is where they meet, because /dev/spark is one
        device and a caller does not care which of them answers.

        What is genuinely this file's: the mounts a Moonwater boot needs
        before anything else can run, the miscdevice and its file
        operations, and the init and exit the kernel calls.
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched/task.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/refcount.h>
#include <linux/cred.h>
#include <linux/file.h>
// Before the library's own spellings below: poll.h names a bool of its own.
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/kernel_stat.h>
#include <linux/cpumask.h>
#include <linux/timekeeping.h>
#include <linux/sched/loadavg.h>
#include <linux/swap.h>
#include <linux/netdevice.h>
#include <linux/nsproxy.h>
#include <net/net_namespace.h>
// Bindings: the machine's own keys and events, not the compositor's.
#include <linux/input.h>
#include <linux/reboot.h>
#include <linux/kmod.h>
#include <linux/sched/signal.h>
#include <linux/seccomp.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>
#ifdef CONFIG_VT
#include <linux/keyboard.h>
#include <linux/vt_kern.h>
#endif
#ifdef CONFIG_PM
#include <linux/suspend.h>
#endif
#include <linux/io.h>
#ifdef CONFIG_EFI
#include <linux/efi.h>
#endif

#ifdef CONFIG_X86_64
#include <asm/cpufeature.h>
#include <asm/fpu/api.h>
#include <asm/fpu/xcr.h>
#elif defined(CONFIG_ARM64)
#include <asm/cpufeature.h>
#include <asm/neon.h>
#elif defined(CONFIG_RISCV)
#include <asm/cpufeature.h>
#include <asm/vector.h>
// arch_setup_additional_pages: the Spark loader maps the vDSO every signal
// handler returns through. Not exported, so the core cannot be a module here.
#include <linux/elf.h>
#if !IS_BUILTIN(CONFIG_MOONWATER_CORE)
#error "on riscv64 the Moonwater core maps each program's vDSO and must be built in (CONFIG_MOONWATER_CORE=y)"
#endif
#endif

// may_use_simd, for Canvas's vector bracket (canvas_simd_begin).
#include <asm/simd.h>

// The graphics headers must precede lib.c: it defines "end" as a macro
// and asm/io.h, reached through drm_client.h, uses that word as a variable.
#ifdef CONFIG_MOONWATER_CANVAS
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
// The console keyboard's mode, which Canvas turns off while it has the keys.
#ifdef CONFIG_VT
#include <linux/kd.h>
#include <linux/vt_kern.h>
#endif
#endif

#define STANDARD_MODERN_C_KERNEL
#include "../lib.util.c"
#include "spark.c"
#include "moonwater.c"

// Defined below, next to the rest of the spawning, and called only by the
// compositor when it has a screen to put something on.
#ifdef CONFIG_MOONWATER_CANVAS
static int spawn_terminal(void);
#endif

struct spawn_strings
{
        refcount_t references;
        char **vector;
};

struct pane;

/* One open device is one independent launch/cache and window context. */
struct device_context
{
        struct mutex spawn_lock;
        struct spawn_strings *environment;
        unsigned long environment_generation;
        struct pid *environment_owner;
        /*
                The credentials the cached environment was copied under.

                tgid survives execve, so it alone cannot tell "the same shell
                asking again" from "that shell after it exec'd something
                setuid". Without this a non-CLOEXEC descriptor carried across
                a privilege change lets the stale, unprivileged environment be
                substituted for the one the now-privileged caller passed in.

                A cred is immutable and refcounted, so identity is the pointer
                and the fast path stays a single compare.
        */
        const struct cred *environment_cred;
        struct pane *pane;
};

#ifdef CONFIG_MOONWATER_CANVAS
#include "../canvas/canvas.c"
#endif

/*
        The Spark half of this module: the loader that maps the format, the
        spawn the device offers, the stats beside them, and the typed system
        state. It lives in spark.c with the format it implements, and it is
        expanded here rather than at the top of the file because it is
        written against the device context above it and, for the terminal it
        can start, the compositor.
*/
#define SPARK_KERNEL
#include "spark.c"


// The assembly in this directory. Each .asm is its own object -- assembly
// cannot be included into this translation unit the way canvas.c is
// -- so the compiler is told its shape here, in the file that calls it.
//

int path_mount(const char *dev_name, struct path *path,
               const char *type_page, unsigned long flags, void *data_page);

// Declared rather than included: linux/init_syscalls.h also declares an
// init_mount, and this file has one of its own.
int init_mkdir(const char *pathname, umode_t mode);


/* Caller-triggered failures are rate limited; boot and teardown are not. */

typedef struct
{
        string_address filesystem;
        string_address path;
        positive mount_flags;

} MountPoints;

/*
        devtmpfs is what populates /dev. The kernel will not mount it itself
        when booting from an initramfs, so without this /dev holds only the
        handful of nodes the initramfs was built with -- no /dev/dri, and so
        nothing for the compositor to open.
*/
static const MountPoints mounts[] = {
    {"proc", "/proc", 0},
    {"sysfs", "/sys", 0},
    {"devtmpfs", "/dev", 0},

    /*
            Not devpts. It registers itself with module_init, which for
            built-in code is device_initcall -- the level this file starts at,
            and kernel/ links before fs/. Even late_initcall was too early.
            init mounts it, which is where a system does it anyway, and it
            needs no pty before then.
    */
    {null, null},
};

/*
        Every request number is the encoding of the struct it carries.

        A handler copies sizeof(its struct) from the caller, and the caller
        sized its buffer from the number it sent. The two agree only while
        nobody edits one without the other, and the day they do not, the
        kernel reads or writes past what the caller allocated -- a struct
        that grew by a field, behind a number that still says the old size,
        is a copy_to_user of stack the caller never asked for. So the number
        is not typed in and trusted: it is rebuilt here from the direction,
        the request number and sizeof, and the build stops on a mismatch.
        The size is what _IOC_SIZE reads and what these are; the type is the
        letter 's', and no number is used twice.
*/
#define IOCTL_IS(command, direction, request, size)                            \
        _Static_assert((command) == (((unsigned int)(direction) << 30) |        \
                                     ((unsigned int)(size) << 16) |             \
                                     ((unsigned int)'s' << 8) |                 \
                                     (unsigned int)(request)),                  \
                       #command " does not encode the struct it carries")
#define IOCTL_NONE 0
#define IOCTL_WRITE 1
#define IOCTL_READ 2
#define IOCTL_BOTH 3

IOCTL_IS(SPARK_IOCTL_SPAWN, IOCTL_WRITE, 1, sizeof(struct spawn));
IOCTL_IS(SPARK_IOCTL_STATS, IOCTL_READ, 2, sizeof(struct stats));
IOCTL_IS(SPARK_IOCTL_INPUT_STATS, IOCTL_READ, 3, sizeof(struct input_stats));
IOCTL_IS(SPARK_IOCTL_CURSOR_STATS, IOCTL_READ, 6, sizeof(struct cursor_stats));
IOCTL_IS(SPARK_IOCTL_INPUT_DEVICES, IOCTL_READ, 7, sizeof(struct input_devices));
IOCTL_IS(SPARK_IOCTL_SNAPSHOT, IOCTL_BOTH, 9, sizeof(struct snapshot_request));
IOCTL_IS(SPARK_IOCTL_CANVAS, IOCTL_BOTH, 10, sizeof(struct canvas_control));
IOCTL_IS(SPARK_IOCTL_BIND, IOCTL_BOTH, 11, sizeof(struct bind_control));
IOCTL_IS(SPARK_IOCTL_SETTINGS_GET, IOCTL_READ, 12, sizeof(struct spark_settings_request));
IOCTL_IS(SPARK_IOCTL_SETTINGS_SET, IOCTL_WRITE, 13, sizeof(struct spark_settings_request));
IOCTL_IS(MOONWATER_IOCTL_MACHINE, IOCTL_BOTH, 14, sizeof(struct machine_control));
IOCTL_IS(MOONWATER_IOCTL_SCRIPT, IOCTL_BOTH, 15, sizeof(struct machine_script));
#ifdef CONFIG_MOONWATER_CANVAS
IOCTL_IS(WINDOW_IOCTL_CREATE, IOCTL_WRITE, 4, sizeof(struct window_request));
IOCTL_IS(WINDOW_IOCTL_COMMIT, IOCTL_NONE, 5, 0);
IOCTL_IS(WINDOW_IOCTL_STRIDE, IOCTL_NONE, 16, 0);
#endif

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
        switch (cmd)
        {
        case SPARK_IOCTL_SPAWN:
                return do_spawn(file, (struct spawn __user *)arg);
        case SPARK_IOCTL_STATS:
                return report_stats((struct stats __user *)arg);
        case SPARK_IOCTL_SNAPSHOT:
                return report_snapshot((struct snapshot_request __user *)arg);
        case SPARK_IOCTL_BIND:
                return report_bind((struct bind_control __user *)arg);
        case MOONWATER_IOCTL_MACHINE:
                return report_machine(file, (struct machine_control __user *)arg);
        case MOONWATER_IOCTL_SCRIPT:
                return report_machine_script((struct machine_script __user *)arg);
        case SPARK_IOCTL_SETTINGS_GET:
                return settings_get((struct spark_settings_request __user *)arg);
        case SPARK_IOCTL_SETTINGS_SET:
                return settings_set((struct spark_settings_request __user *)arg);
#ifdef CONFIG_MOONWATER_CANVAS
        case SPARK_IOCTL_INPUT_STATS:
                return report_input((struct input_stats __user *)arg);
        case SPARK_IOCTL_CURSOR_STATS:
                return report_cursor((struct cursor_stats __user *)arg);
        case SPARK_IOCTL_INPUT_DEVICES:
                return report_devices((struct input_devices __user *)arg);
        case SPARK_IOCTL_CANVAS:
                return report_canvas((struct canvas_control __user *)arg);
        case WINDOW_IOCTL_CREATE:
                return window_ioctl_create(file, arg);
        case WINDOW_IOCTL_COMMIT:
                return window_ioctl_commit(file);
        case WINDOW_IOCTL_STRIDE:
                return window_ioctl_stride(file, arg);
#endif
        }

        return -ENOTTY;
}

/*
        misc_open leaves the miscdevice in private_data. Replace it with one
        context per open: its environment snapshot belongs to that launcher,
        and its pane belongs to that window client.
*/
static int device_open(struct inode *inode, struct file *file)
{
        struct device_context *context = kzalloc(sizeof(*context), GFP_KERNEL);

        if (!context)
                return -ENOMEM;

        mutex_init(&context->spawn_lock);
        file->private_data = context;
        return 0;
}

static int device_close(struct inode *inode, struct file *file)
{
        struct device_context *context = file->private_data;

        bind_machine_detach(file);
#ifdef CONFIG_MOONWATER_CANVAS
        window_release(file);
#endif
        spawn_strings_put(context->environment);
        put_pid(context->environment_owner);
        put_cred(context->environment_cred);
        kfree(context);
        return 0;
}

static const struct file_operations device_ops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .unlocked_ioctl = device_ioctl,
    .release = device_close,
#ifdef CONFIG_MOONWATER_CANVAS
    .mmap = window_mmap,
    .poll = window_poll,
#endif
    .llseek = noop_llseek,
};

// A fixed minor rather than MISC_DYNAMIC_MINOR: there is no devtmpfs here to
// materialise the node, so build.sh mknods it into the initramfs and
// both sides have to agree on the number. 240-254 is the range set aside for
// local use.
static struct miscdevice device = {
    .minor = SPARK_DEVICE_MINOR,
    .name = "spark",
    .fops = &device_ops,
    .mode = 0666,
};

// static, because the kernel has its own init_mount in fs/init.c and the
// module's symbols share one namespace with it. Nothing outside this file
// calls it, so internal linkage is the answer rather than a prefix.
static fn init_mount()
{
        const MountPoints address_to mount = mounts;

        while (mount->filesystem)
        {
                struct path path;

                int ret = kern_path(mount->path, LOOKUP_FOLLOW, &path);

                if (ret == -ENOENT && !init_mkdir(mount->path, 0755))
                        ret = kern_path(mount->path, LOOKUP_FOLLOW, &path);

                if (ret)
                {
                        pr_alert("[moonwater] " "Mounting %s to %s failed with error: %d\n", mount->filesystem, mount->path, ret);
                        mount++;
                        continue;
                }

                ret = path_mount(mount->filesystem, &path, mount->filesystem, mount->mount_flags, null);
                path_put(&path);

                //      Only the failures. Three lines saying a mount that
                //      was always going to work did work is three console
                //      writes on the boot path of every machine, at
                //      KERN_ALERT so no loglevel can turn them off, and
                //      nothing reads them -- the evidence that /proc mounted
                //      is /proc.
                if (ret)
                        pr_alert("[moonwater] " "Mounting %s on %s failed with error: %d\n", mount->filesystem, mount->path, ret);

                mount++;
        }
}

/*
        Proves the assembly runs.

        A .asm that assembles and links is not a .asm that works: until
        something calls it, the only thing the build has shown is that the
        file is syntactically valid for this architecture. This reads the
        counter twice with a barrier between, which catches the two ways a
        wrong block fails -- a counter that never advances, and one that goes
        backwards because the halves were put together the wrong way round.

        Two reads and no delay. The delta is printed rather than the value,
        because a raw counter says nothing and a delta says it is counting.
*/
// Likewise: an initcall does not need external linkage.
static b32 __init start()
{
        int ret;

        /*
                KERNEL_MODE emits only the scalar bodies: the feature gates
                become direct branches to them and the userspace SIMD bodies
                are absent from the object. Nothing in this build reads the
                feature bytes, so there is nothing to detect at init time.
        */
        pr_alert("[moonwater] " "Moonwater starting...\n");

        /*
                What the wide bodies may ask of the processor, from what the
                kernel decided it has -- cpuid alone would say yes to AVX-512
                on a kernel booted to leave it off. Before anything that
                could reach one.
        */
#ifdef CONFIG_X86_64
        cpu_has_avx2 = boot_cpu_has(X86_FEATURE_AVX2);
        cpu_has_avx512 = cpu_has_avx2 && boot_cpu_has(X86_FEATURE_AVX512F) &&
                         boot_cpu_has(X86_FEATURE_AVX512BW) &&
                         boot_cpu_has(X86_FEATURE_AVX512VL);
#endif

        // Before anything that starts from them: Canvas asks at its probe.
        settings_start();

        /*
                The initramfs is unpacked on a workqueue, not inline, so at
                device_initcall time the root filesystem may still be empty --
                and mounting /proc onto a directory that does not exist yet
                fails with ENOENT rather than waiting. This is the call that
                exists to close that race, and every other early user of the
                rootfs makes it.

                It was missing and nothing went wrong, because the unpack
                happened to finish first. Tuning the kernel for latency made
                the rest of the boot quick enough to lose that race, which
                looked like the compositor breaking: no /dev, so no
                /dev/dri/card0, so nothing to attach to.
        */
        wait_for_initramfs();

#if defined(CONFIG_X86_64) || defined(CONFIG_ARM64)
        spark_cpu_features_start();
#endif
        init_mount();

        register_binfmt(&format);

        ret = misc_register(&device);
        if (ret)
        {
                pr_alert("[moonwater] " "could not register /dev/spark: %d\n", ret);
                unregister_binfmt(&format);
                return ret;
        }

        // Before the compositor: bindings have to work with no screen.
        bind_start();

#if defined(CONFIG_MOONWATER_CANVAS) && \
    defined(CONFIG_MOONWATER_CANVAS_AUTOSTART)
        canvas_start_probing();
#endif

        return 0;
}

static void __exit exit_module(void)
{
#ifdef CONFIG_MOONWATER_CANVAS
#ifdef CONFIG_MOONWATER_CANVAS_AUTOSTART
        // A probe that has not found a card owns no DRM client (and therefore
        // no module reference) to keep this callback's text resident.
        cancel_delayed_work_sync(&canvas_probe_work);
#endif
        // Before anything else: printk must stop being pointed at cells that
        // are about to be freed.
        console_stop();
        put_pid(xchg(&canvas_spawned, NULL));
#endif

        bind_stop();

        misc_deregister(&device);
        kvfree(snapshot);
        kfree(settings_current);
        unregister_binfmt(&format);
        pr_alert("[moonwater] " "Spark format unregistered\n");
}

/*
        device_initcall, the same level the display drivers register at.

        Link order puts kernel/ ahead of drivers/, so this runs before them:
        /dev is mounted and the poll for a display starts while the drivers are
        still coming up, and the compositor takes the device the moment it
        appears rather than a hundred milliseconds later. Waiting until every
        driver had finished cost exactly that.

        Anything earlier is not possible: the initramfs is not unpacked until
        rootfs_initcall, so before this point there is no /dev to mount onto.
*/
// Use device_initcall for built-in, or module_init for module
#ifdef MODULE
module_init(start);
module_exit(exit_module);
MODULE_AUTHOR("Dawn Larsson");
MODULE_DESCRIPTION("Spark direct binary format");
#else
device_initcall(start);
#endif
