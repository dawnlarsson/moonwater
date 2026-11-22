/*
        The Moonwater kernel module: what it registers, and what it dispatches.

        Two subsystems are expanded into this one translation unit. spark.c
        is the binary format and the loader, spawn and reports that serve it.
        moonwater.c is the bindings, the machine script and the scanner. Each
        keeps to itself; the ioctl switch below is where they meet, because
        /dev/spark is one device and a caller does not care which of them
        answers. The compositor is a third, and not here: src/canvas builds
        its own object, and src/canvas/canvas.c and seam.h beside this file
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
#include <uapi/linux/mount.h>
#include <linux/miscdevice.h>
#ifdef CONFIG_FAULT_INJECTION
#include <linux/fault-inject.h>
#endif
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
// Rover's kernel half (rules.c): its queue, its holds and the notifiers it hears.
#include <linux/irq_work.h>
#include <linux/pm_qos.h>
#include <linux/rcupdate.h>
#include <linux/time.h>
#if IS_REACHABLE(CONFIG_POWER_SUPPLY)
#include <linux/power_supply.h>
#endif
#if IS_REACHABLE(CONFIG_USB)
#include <linux/usb.h>
#endif
#if IS_REACHABLE(CONFIG_BACKLIGHT_CLASS_DEVICE)
#include <linux/backlight.h>
#endif
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
#define CANVAS_DECLARE_ONLY
#include "../canvas/canvas.c"
#undef CANVAS_DECLARE_ONLY

// Canvas's pixel loops are its own object's, not this one's: the library is
// declared to it and emitted here, and these six are the other way round.
#define LIBRARY_CANVAS_ELSEWHERE

#define STANDARD_MODERN_C_KERNEL
#include "../lib.util.c"
#include "spark.c"
#include "moonwater.c"
#include "rules.c"

struct spawn_strings
{
        refcount_t references;
        // What the one allocation is, the vector and the bytes behind it, so
        // an environment a device keeps for the next launch can be counted.
        size_t size;
        char **vector;
};

/* One open device is one independent launch/cache context. */
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

        // Rover's ring: the head this descriptor read last (rules_read, rules_poll).
        u64 rover_seen;
};

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
/*
        None of the three is somewhere a program is run from or a setuid file
        is kept, so each is mounted without the right to honour one: nosuid
        and noexec on all of them, and nodev on all but devtmpfs, which is
        where the device nodes are. They were mounted with no flags at all;
        every distribution's init mounts them this way, and a bowl's private
        /proc already is.

        The locked tier (MOONWATER_STRICT_HOST 3, or the whole build's level
        when that region follows it) also hides every other user's process
        from /proc with hidepid=invisible. Root still sees all of them.
*/
#if defined(CONFIG_MOONWATER_STRICT_HOST) && CONFIG_MOONWATER_STRICT_HOST >= 0
#define MOUNT_HOST_LEVEL CONFIG_MOONWATER_STRICT_HOST
#elif defined(CONFIG_MOONWATER_STRICT)
#define MOUNT_HOST_LEVEL CONFIG_MOONWATER_STRICT
#else
#define MOUNT_HOST_LEVEL 1
#endif

#if MOUNT_HOST_LEVEL >= 3
#define PROC_MOUNT_DATA "hidepid=invisible"
#else
#define PROC_MOUNT_DATA null
#endif

static const struct
{
        string_address filesystem;
        string_address path;
        unsigned long flags;
        string_address data;
} mounts[] = {
    {"proc", "/proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, PROC_MOUNT_DATA},
    {"sysfs", "/sys", MS_NOSUID | MS_NODEV | MS_NOEXEC, null},
    {"devtmpfs", "/dev", MS_NOSUID | MS_NOEXEC, null},

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

static long report_latency(struct file *file, struct latency_stats __user *out)
{
        struct latency_stats request, stats;
        struct handler_cost bound = {};

        if (copy_from_user(&request, out, sizeof(request)))
                return -EFAULT;
        if (request.request > SPARK_LATENCY_RESET || request.elapsed_ns ||
            request.reserved)
                return -EINVAL;
        if (!spark_file_capable(file, CAP_SYS_ADMIN))
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
                return report_bind(file, (struct bind_control __user *)arg);
        case SPARK_IOCTL_LATENCY:
                return report_latency(file, (struct latency_stats __user *)arg);
        case MOONWATER_IOCTL_MACHINE:
                return report_machine(file, (struct machine_control __user *)arg);
        case MOONWATER_IOCTL_SCRIPT:
                return report_machine_script(file, (struct machine_script __user *)arg);
        case ROVER_IOCTL:
                return report_rover(file, (struct rover_control __user *)arg);
        case SPARK_IOCTL_SETTINGS_GET:
                return settings_get(file, (struct spark_settings_request __user *)arg);
        case SPARK_IOCTL_SETTINGS_SET:
                return settings_set(file, (struct spark_settings_request __user *)arg);
        }

        // The compositor's requests: -ENOTTY from the client is what the
        // switch would say.
        return canvas_client_ioctl(file, cmd, arg);
}

/*
        misc_open leaves the miscdevice in private_data. Replace it with one
        context per open: its environment snapshot belongs to that launcher.
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

        spark_input_release(file);
        bind_machine_detach(file, NULL);
        canvas_client_release(file);
        spark_environment_release(context);
        kfree(context);
        return 0;
}

// Rover's state page and ring (rules.c, pgoff 0) and direct input ring (moonwater.c, pgoff SPARK_INPUT_MMAP_PGOFF).
static int device_mmap(struct file *file, struct vm_area_struct *vma)
{
        if (vma->vm_pgoff == SPARK_INPUT_MMAP_PGOFF)
                return spark_input_mmap(file, vma);
        return rules_mmap(file, vma);
}

static __poll_t device_poll(struct file *file, poll_table *wait)
{
        struct device_context *context = file->private_data;

        if (spark_input_is_owner(file))
                return spark_input_poll(file, wait);
        return rules_poll(file, &context->rover_seen, wait);
}

static ssize_t device_read(struct file *file, char __user *into, size_t room, loff_t *at)
{
        struct device_context *context = file->private_data;

        (void)at;
        return rules_read(file, &context->rover_seen, into, room);
}

static const struct file_operations device_ops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .read = device_read,
    .poll = device_poll,
    .mmap = device_mmap,
    .unlocked_ioctl = device_ioctl,
    .flush = device_flush,
    .release = device_close,
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

// Whether start() got /dev/spark registered, so that the exit undoes only
// what was done: misc_deregister of a device that never registered destroys
// whatever device holds the minor and frees a minor it does not own.
static bool device_registered;

#ifdef CONFIG_FAULT_INJECTION
/*
        fail_spark_device=<interval>,<probability>,<space>,<times> on the
        kernel command line, read as failslab= reads its four, makes the
        registration of /dev/spark fail at boot -- the one way to reach
        start()'s failure path in a booted machine (test/run ring0 does, on a
        sanitizer image). It exists only where CONFIG_FAULT_INJECTION does,
        which is the debug profiles (kernel/profile/sec_sanitize, sec_kcsan);
        no profile a machine ships with builds it.
*/
static DECLARE_FAULT_ATTR(fail_spark_device);

static int __init fail_spark_device_setup(char *text)
{
        return setup_fault_attr(&fail_spark_device, text);
}
__setup("fail_spark_device=", fail_spark_device_setup);
#endif

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
                        ret = path_mount(mounts[i].filesystem, &path, mounts[i].filesystem,
                                         mounts[i].flags, (void *)mounts[i].data);
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

        /*
                /dev/spark is the fast way to start a program, not the way the
                machine runs. /init is a Spark binary, and the format just
                registered is the only thing that can execute it; the device is
                how the shell spawns without a fork, and how the moonwater
                command and the desktop talk to the module. So a device that
                cannot be registered -- its fixed minor already some other
                driver's (-EBUSY), or no memory for its struct device -- is
                said, and the boot goes on without it: the format, the
                settings, the bindings and Canvas all stay. devtmpfs then has
                no /dev/spark (it is on /dev by now, over the node the
                initramfs carries), the shell's open of it fails, and the shell
                forks each program as it does on any kernel without the device
                (shell_spawn_device_open in src/sh/shell.c); what asks the
                device for more than a spawn says the device is missing.

                It used to give up instead: unregister the format, free the
                settings and return the error. With the format gone the kernel
                had nothing to run /init with, tried the fallback inits, which
                are the same Spark binaries, and panicked with "No working init
                found" -- a machine that could have run, stopped for want of an
                optimisation. A retry would not help: -EBUSY does not clear,
                and memory short at device_initcall is short for everything
                after it.
        */
#ifdef CONFIG_FAULT_INJECTION
        if (should_fail(&fail_spark_device, 1))
                ret = -ENOMEM;
        else
#endif
                ret = misc_register(&device);

        if (ret)
                pr_alert("[moonwater] " "could not register /dev/spark: %d; going on without it, and the shell will fork\n", ret);
        else
                device_registered = true;

        // Before the compositor: bindings have to work with no screen. Rover's
        // page and ring first, so the watch's first device writes its switches.
        rules_start();
        bind_start();

        canvas_boot();

        return 0;
}

static void __exit exit_module(void)
{
        // Deregister /dev/spark before tearing down the state its handlers touch.
        if (device_registered)
                misc_deregister(&device);
        device_registered = false;

        // Before anything else: printk must stop being pointed at cells that
        // are about to be freed.
        canvas_unload();

        bind_stop();
        rules_stop();

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
