/*
        A qemu plugin that counts guest instructions, and with trace=on
        also folds where in its page every guest load and store falls into
        a hash.

        Built and used by test/insns. qemu ships one of these -- libinsn.so --
        but it is not installed everywhere and the whole of what is needed from
        it is the count, so it is here rather than assumed.

        Every translation block is asked how many instructions it holds when it
        is translated, and every execution of it adds that many. Single
        threaded, which the programs under test/ are.
*/
#include <qemu-plugin.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static uint64_t total;
static uint64_t trace = 14695981039346656037ull;
static int tracing;

static void mem_exec(unsigned int cpu, qemu_plugin_meminfo_t info, uint64_t vaddr,
                     void *udata)
{
        trace = (trace ^ (vaddr & 0xfff)) * 1099511628211ull;
}

static void tb_exec(unsigned int cpu, void *udata)
{
        total += (uint64_t)(uintptr_t)udata;
}

static void tb_trans(struct qemu_plugin_tb *tb, void *udata_unused)
{
        size_t n = qemu_plugin_tb_n_insns(tb);

        qemu_plugin_register_vcpu_tb_exec_cb(tb, tb_exec, QEMU_PLUGIN_CB_NO_REGS,
                                             (void *)(uintptr_t)n);
        for (size_t at = 0; tracing && at < n; at++)
                qemu_plugin_register_vcpu_mem_cb(qemu_plugin_tb_get_insn(tb, at), mem_exec,
                                                 QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_MEM_RW,
                                                 NULL);
}

static void done(void *p)
{
        if (tracing)
                fprintf(stderr, "%llu %016llx\n", (unsigned long long)total,
                        (unsigned long long)trace);
        else
                fprintf(stderr, "%llu\n", (unsigned long long)total);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                                           int argc, char **argv)
{
        for (int at = 0; at < argc; at++)
                if (!strcmp(argv[at], "trace=on"))
                        tracing = 1;
        qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans, NULL);
        qemu_plugin_register_atexit_cb(id, done, NULL);
        return 0;
}
