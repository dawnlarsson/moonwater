/*
        The Moonwater kernel module: what it registers, and what it dispatches.

        Two subsystems are expanded into this one translation unit. spark.c
        is the binary format and the loader, spawn and reports that serve it.
        moonwater.c is the bindings, the machine script and the scanner. Each
        keeps to itself; the ioctl switch below is where they meet, because
        /dev/spark is one device and a caller does not care which of them
        answers. The compositor is a third, and not here: src/canvas builds
        its own object, and src/canvas/canvas.h and seam.h beside this file
        are the whole of what passes between them.

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

// What a kernel object built beside this one may call, and what this asks of
// it. Plain kernel headers, so before the library's spellings below.
#include "seam.h"
#include "../canvas/canvas.h"

// Canvas's pixel loops are its own object's, not this one's: the library is
// declared to it and emitted here, and these six are the other way round.
#define LIBRARY_CANVAS_ELSEWHERE

#define STANDARD_MODERN_C_KERNEL
#include "../lib.util.c"
#include "spark.c"
#include "moonwater.c"

struct spawn_strings
{
        refcount_t references;
        // What the one allocation is, the vector and the bytes behind it, so
        // an environment a device keeps for the next launch can be counted.
        size_t size;
        char **vector;
};

/* One open device is one independent launch/cache and window context. */
struct device_context
{
        // The display client's, and the first member: see moonwater_display.
        void *display;
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
};

_Static_assert(offsetof(struct device_context, display) == 0,
               "moonwater_display reads the first word of the file's context");

// Kernel functions rewritten in assembly (kernel/kernel.c, GPL-2.0), which
// kernel/patch/functions puts in the place of the C originals. A stock build
// keeps every one of them as Linux wrote it.
#ifndef STOCK_STRINGS
#include "../../kernel/kernel.c"
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


int path_mount(const char *dev_name, struct path *path,
               const char *type_page, unsigned long flags, void *data_page);

// Declared rather than included: linux/init_syscalls.h also declares an
// init_mount, and this file has one of its own.
int init_mkdir(const char *pathname, umode_t mode);


/*
        devtmpfs is what populates /dev. The kernel will not mount it itself
        when booting from an initramfs, so without this /dev holds only the
        handful of nodes the initramfs was built with -- no /dev/dri, and so
        nothing for the compositor to open.
*/
static const struct
{
        string_address filesystem;
        string_address path;
} mounts[] = {
    {"proc", "/proc"},
    {"sysfs", "/sys"},
    {"devtmpfs", "/dev"},

    /*
            Not devpts. It registers itself with module_init, which for
            built-in code is device_initcall -- the level this file starts at,
            and kernel/ links before fs/. Even late_initcall was too early.
            init mounts it, which is where a system does it anyway, and it
            needs no pty before then.
    */
};

/*
        What the two input handlers cost a report, for `moonwater latency`:
        the machine's key watch is counted here and Canvas's by Canvas. Event
        counts are the rhythm of somebody's typing, so reading them needs the
        capability the other diagnostics of somebody's hands need, and so
        does zeroing them.
*/
static u64 latency_since;

static long report_latency(struct latency_stats __user *out)
{
        struct latency_stats request, stats;
        struct handler_cost bound = {};

        if (copy_from_user(&request, out, sizeof(request)))
                return -EFAULT;
        if (request.request > SPARK_LATENCY_RESET || request.elapsed_ns ||
            request.reserved)
                return -EINVAL;
        if (!capable(CAP_SYS_ADMIN))
                return -EPERM;

        memset(&stats, 0, sizeof(stats));
        if (request.request == SPARK_LATENCY_RESET)
        {
                moonwater_cost_reset(&bind_cost);
                WRITE_ONCE(latency_since, ktime_get_ns());
        }
        stats.elapsed_ns = ktime_get_ns() - READ_ONCE(latency_since);
        canvas_latency(&stats, request.request);
        moonwater_cost_read(&bind_cost, &bound);
        stats.bind.events = bound.events;
        stats.bind.samples = bound.samples;
        stats.bind.total_ns = bound.total_ns;
        stats.bind.worst_ns = bound.worst_ns;
        stats.request = request.request;

        return copy_to_user(out, &stats, sizeof(stats)) ? -EFAULT : 0;
}

IOCTL_IS(SPARK_IOCTL_SPAWN, IOCTL_WRITE, 1, sizeof(struct spawn));
IOCTL_IS(SPARK_IOCTL_STATS, IOCTL_READ, 2, sizeof(struct stats));
IOCTL_IS(SPARK_IOCTL_LATENCY, IOCTL_BOTH, 8, sizeof(struct latency_stats));
IOCTL_IS(SPARK_IOCTL_SNAPSHOT, IOCTL_BOTH, 9, sizeof(struct snapshot_request));
IOCTL_IS(SPARK_IOCTL_BIND, IOCTL_BOTH, 11, sizeof(struct bind_control));
IOCTL_IS(SPARK_IOCTL_SETTINGS_GET, IOCTL_READ, 12, sizeof(struct spark_settings_request));
IOCTL_IS(SPARK_IOCTL_SETTINGS_SET, IOCTL_WRITE, 13, sizeof(struct spark_settings_request));
IOCTL_IS(MOONWATER_IOCTL_MACHINE, IOCTL_BOTH, 14, sizeof(struct machine_control));
IOCTL_IS(MOONWATER_IOCTL_SCRIPT, IOCTL_BOTH, 15, sizeof(struct machine_script));

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
        // Every program a shell starts without a fork comes through here, and
        // the rest of the numbers are a monitor's or a window's: one compare
        // for the first, not the walk of a dozen for it.
        if (likely(cmd == SPARK_IOCTL_SPAWN))
                return do_spawn(file, (struct spawn __user *)arg);

        switch (cmd)
        {
        case SPARK_IOCTL_STATS:
                return report_stats((struct stats __user *)arg);
        case SPARK_IOCTL_SNAPSHOT:
                return report_snapshot((struct snapshot_request __user *)arg);
        case SPARK_IOCTL_BIND:
                return report_bind((struct bind_control __user *)arg);
        case SPARK_IOCTL_LATENCY:
                return report_latency((struct latency_stats __user *)arg);
        case MOONWATER_IOCTL_MACHINE:
                return report_machine(file, (struct machine_control __user *)arg);
        case MOONWATER_IOCTL_SCRIPT:
                return report_machine_script((struct machine_script __user *)arg);
        case SPARK_IOCTL_SETTINGS_GET:
                return settings_get((struct spark_settings_request __user *)arg);
        case SPARK_IOCTL_SETTINGS_SET:
                return settings_set((struct spark_settings_request __user *)arg);
        }

        // The compositor's requests, and the windows' ones: a program's
        // commit is a frame, and the one more compare is not measurable
        // against it. -ENOTTY from the client is what the switch would say.
        return canvas_client_ioctl(file, cmd, arg);
}

/*
        misc_open leaves the miscdevice in private_data. Replace it with one
        context per open: its environment snapshot belongs to that launcher,
        and its pane belongs to that window client.
*/
static int device_open(struct inode *inode, struct file *file)
{
        struct device_context *context = kzalloc(sizeof(*context), GFP_KERNEL);

        if (unlikely(!context))
                return -ENOMEM;

        mutex_init(&context->spawn_lock);
        file->private_data = context;
        return 0;
}

// Every close of a descriptor, not the last: see bind_machine_flush.
static int device_flush(struct file *file, fl_owner_t files)
{
        bind_machine_flush(file);
        return 0;
}

static int device_close(struct inode *inode, struct file *file)
{
        struct device_context *context = file->private_data;

        bind_machine_detach(file, NULL);
        canvas_client_release(file);
        spark_environment_release(context);
        kfree(context);
        return 0;
}

static const struct file_operations device_ops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .unlocked_ioctl = device_ioctl,
    .flush = device_flush,
    .release = device_close,
    CANVAS_FILE_OPERATIONS
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
        for (positive i = 0; i < array_count(mounts); i++)
        {
                struct path path;
                int ret = kern_path(mounts[i].path, LOOKUP_FOLLOW, &path);

                if (ret == -ENOENT && !init_mkdir(mounts[i].path, 0755))
                        ret = kern_path(mounts[i].path, LOOKUP_FOLLOW, &path);

                if (!ret)
                {
                        ret = path_mount(mounts[i].filesystem, &path, mounts[i].filesystem, 0, null);
                        path_put(&path);
                }

                //      Only the failures. Three lines saying a mount that
                //      was always going to work did work is three console
                //      writes on the boot path of every machine, at
                //      KERN_ALERT so no loglevel can turn them off, and
                //      nothing reads them -- the evidence that /proc mounted
                //      is /proc.
                if (ret)
                        pr_alert("[moonwater] " "Mounting %s on %s failed with error: %d\n", mounts[i].filesystem, mounts[i].path, ret);
        }
}

// static, for the same reason: an initcall does not need external linkage.
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
                kfree(xchg(&settings_current, NULL));
                return ret;
        }

        // Before the compositor: bindings have to work with no screen.
        bind_start();

        canvas_boot();

        return 0;
}

static void __exit exit_module(void)
{
        // Before anything else: printk must stop being pointed at cells that
        // are about to be freed.
        canvas_unload();

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
