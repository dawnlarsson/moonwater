// SPDX-License-Identifier: GPL-2.0
/*
 * Floodlight -- every runtime allowance on this machine, in one array.
 *
 * What a program is allowed to do is answered here and nowhere else. The
 * answers this kernel was built with are the array below; anything changed
 * since boot carries the time it changed and the user who changed it. Reading
 * the device prints all of it:
 *
 *     cat /dev/floodlight
 *
 * and writing one line to it changes one answer, until it is sealed:
 *
 *     echo 'awk spawn deny'   > /dev/floodlight
 *     echo 'curl network deny' > /dev/floodlight
 *     echo seal               > /dev/floodlight
 *
 * Text, not an ioctl, on purpose. A policy you can read with cat and change
 * with echo is one that can be checked by somebody holding no special tool and
 * trusting no special program -- and there is no second description of a
 * struct to drift out of step with this one.
 *
 * ONE FILE, ON PURPOSE
 *
 * This is the whole of it. Not a directory, not a library, not a header shared
 * with anything: the file you are reading is the entire trusted base of the
 * decision. Moonwater's own module is the opposite by necessity -- src/moonwater/core.c
 * includes the compositor, which includes the terminal emulator -- and a
 * policy living in there would have two hundred thousand lines inside its
 * trusted base. So this is ordinary kernel C, built on its own, exporting no
 * symbols, and nothing links it.
 *
 * The rule that keeps it that way is checked rather than intended: the
 * floodlight harness in test/differential.py fails the build if this file
 * includes anything but <linux/...>, or names anything Moonwater defines.
 * Left to good intentions somebody reaches for the library because it exists.
 *
 * WHAT IT CLOSES, AND WHAT IT DOES NOT
 *
 * The array is consulted where a program is started. `run` and `flag` are
 * decided there, because that is the only place a program's name and its
 * arguments are both known. `spawn` and `network` become a seccomp filter
 * installed before the program starts, and that is what makes them stick: a
 * program cannot take a filter off itself, and it is already on before the
 * program has read a byte of the data that might tell it to try.
 *
 * That ordering is the whole point. The dangerous programs are the ones whose
 * behaviour is driven by what they read -- awk running a string its script
 * computed, find running a command built from a filename it walked to, xargs
 * running whatever arrived on its input. The filter is on before the data is.
 *
 * Someone who replaces the launcher itself is outside what this can promise.
 * What holds even then: they cannot forge a row, cannot unseal, and cannot
 * make the report say a change was somebody else's.
 */

#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/lockdep.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/timekeeping.h>
#include <linux/uaccess.h>

#define SUBJECT 64 /* an applet name, or an absolute path */
#define DETAIL 32  /* a flag, written as it is written: "--to-command" */

/* Deliberately in this order: the first two are decided by whoever starts the
 * program, the last two by the filter installed before it does. */
enum { RUN, FLAG, SPAWN, NETWORK, SETTINGS };

static const char *const setting_name[SETTINGS] = {
	"run", "flag", "spawn", "network"
};

/*
 * The answers this kernel was built with.
 *
 * const, so they live in .rodata and the machine write-protects them once it
 * is up (CONFIG_STRICT_KERNEL_RWX). That is not tidiness. These rows are what
 * the report calls "built in", and while they sat in writable memory a stray
 * write could flip one and leave its timestamp at zero -- so the report would
 * go on calling the flipped value the one this kernel was compiled with. The
 * whole worth of this file is that "built in" means something, and it only
 * means something if the built-in answers cannot be written.
 *
 * Every applet that can start another program is here, because a program that
 * can start programs is the only kind that can be turned into something else.
 * Which applets those are is not a matter of opinion: the floodlight harness
 * derives it from the source and fails the build when this list and the source
 * disagree, in either direction.
 *
 * NAMED programs run what was written on their own command line -- a hand
 * typed it. DERIVED programs build a command out of data they read, and SHELL
 * programs start a shell, so whatever reaches them is a language rather than
 * a command. Those two are the living-off-the-land surface.
 *
 * Every row allows, all the same. A machine nobody configured behaves as any
 * other Linux does -- awk runs system() the way gawk does, script starts its
 * shell -- because a policy that breaks ordinary work out of the box is one
 * people turn off everywhere. Refusing is the configuration's to say (the
 * rows and switches below, and kernel/profile/sec_hardened), and whatever it
 * says is enforced in full: what then breaks is the policy working.
 */
struct rule {
	const char *subject;
	unsigned char setting;
	unsigned char allowed;
};

static const struct rule baseline[] = {
	/*
	 * awk is the sharpest of these, because it is the only one where the
	 * name of the program to run comes out of the data: find and xargs
	 * take the command on their own command line, while awk's system()
	 * and "cmd" | getline build it from program text that arrives in
	 * files and variables. It was refused here until 2026-09-28, and so
	 * were script and setarch; that refusal broke awk in every pipeline
	 * and substitution of a machine that had asked for nothing, and now
	 * lives in MOONWATER_FLOODLIGHT_AWK_SPAWN and _SHELL_ESCAPES.
	 *
	 * Every applet that can spawn keeps its row, so a machine that wants
	 * one shut can say so in one line and this file is still where that
	 * is written down.
	 */
	{ "awk", SPAWN, 1 },       /* DERIVED: system(), "cmd" | getline */
	{ "find", SPAWN, 1 },      /* NAMED: -exec runs what the line named */
	{ "xargs", SPAWN, 1 },     /* NAMED: likewise, with data in the args */
	{ "bowl", SPAWN, 1 },      /* NAMED: runs the program the line named */
	{ "script", SPAWN, 1 },    /* SHELL: the session it records is a shell */
	{ "setarch", SPAWN, 1 },   /* SHELL: falls back to /bin/sh given no command */
	{ "init", SPAWN, 1 },      /* SHELL: PID 1 starts everything; refusing it
				      would refuse the machine */
	{ "term", SPAWN, 1 },      /* SHELL: the window's own shell, likewise */
	{ "moonwater", SPAWN, 1 }, /* NAMED: cp, spelled in the install itself */
	{ "chroot", SPAWN, 1 },    /* NAMED */
	{ "choom", SPAWN, 1 },     /* NAMED */
	{ "chrt", SPAWN, 1 },      /* NAMED */
	{ "coresched", SPAWN, 1 }, /* NAMED */
	{ "env", SPAWN, 1 },       /* NAMED */
	{ "flock", SPAWN, 1 },     /* NAMED */
	{ "ionice", SPAWN, 1 },    /* NAMED */
	{ "install", SPAWN, 1 },   /* NAMED: strip, for -s */
	{ "nice", SPAWN, 1 },      /* NAMED */
	{ "nohup", SPAWN, 1 },     /* NAMED */
	{ "nsenter", SPAWN, 1 },   /* NAMED */
	{ "pipesz", SPAWN, 1 },    /* NAMED */
	{ "prlimit", SPAWN, 1 },   /* NAMED */
	{ "setpgid", SPAWN, 1 },   /* NAMED */
	{ "setpriv", SPAWN, 1 },   /* NAMED */
	{ "setsid", SPAWN, 1 },    /* NAMED */
	{ "sort", SPAWN, 1 },      /* NAMED: --compress-program names it */
	{ "split", SPAWN, 1 },     /* SHELL: --filter hands the line's text to
				      sh -c, as GNU's split does; a machine
				      that refuses shells refuses --filter
				      (SHELL_ESCAPES, SPLIT_FILTER) */
	{ "stdbuf", SPAWN, 1 },    /* NAMED */
	{ "taskset", SPAWN, 1 },   /* NAMED */
	{ "timeout", SPAWN, 1 },   /* NAMED */
	{ "uclampset", SPAWN, 1 }, /* NAMED */
	{ "unshare", SPAWN, 1 },   /* NAMED */
};

/*
 * The answers this kernel was configured with, on top of those.
 *
 * Two places in the kernel configuration say what a machine refuses beyond the
 * array above. CONFIG_MOONWATER_FLOODLIGHT_POLICY is rows in the words a line
 * written to the device takes, with semicolons between them:
 *
 *     awk spawn deny; tar flag --to-command deny; /usr/bin/curl network deny
 *
 * and each switch in the "Floodlight: dangerous flags" menu, turned off, adds
 * the rows that close one dangerous shape. The string comes first, so a row it
 * names wins over a switch naming the same thing; a row named twice is taken
 * the first time. The switches are read only where a configuration is there to
 * say so -- the string is always defined where Kconfig is -- so this file
 * built with no configuration at all refuses nothing more than the array.
 *
 * Read once, at boot, into rows no more writable than the array (they are
 * __ro_after_init, and summed with it), and reported as built in, which is
 * what they are. A row that does not read is not skipped: skipping a refusal
 * is an allowance nobody wrote, so the register refuses to answer at all and
 * every launch is refused until the configuration is fixed.
 *
 * The shell carries this text byte for byte, for when the register cannot be
 * read, and the floodlight harness fails the build if the two drift.
 */
static const char configured_text[] =
#ifdef CONFIG_MOONWATER_FLOODLIGHT_POLICY
	CONFIG_MOONWATER_FLOODLIGHT_POLICY ";"
#ifndef CONFIG_MOONWATER_FLOODLIGHT_AWK_SPAWN
	"awk spawn deny;"
#endif
#ifndef CONFIG_MOONWATER_FLOODLIGHT_SHELL_ESCAPES
	"script spawn deny; setarch spawn deny; split flag --filter deny;"
#endif
#ifndef CONFIG_MOONWATER_FLOODLIGHT_FIND_EXEC
	"find flag -exec deny; find flag -execdir deny;"
	"find flag -ok deny; find flag -okdir deny;"
#endif
#ifndef CONFIG_MOONWATER_FLOODLIGHT_FIND_DELETE
	"find flag -delete deny;"
#endif
#ifndef CONFIG_MOONWATER_FLOODLIGHT_FIND_WRITE
	"find flag -fprint deny; find flag -fprint0 deny; find flag -fprintf deny;"
#endif
#ifndef CONFIG_MOONWATER_FLOODLIGHT_XARGS
	"xargs run deny;"
#endif
#ifndef CONFIG_MOONWATER_FLOODLIGHT_SPLIT_FILTER
	"split flag --filter deny;"
#endif
#ifndef CONFIG_MOONWATER_FLOODLIGHT_SORT_COMPRESS
	"sort flag --compress-program deny;"
#endif
#ifndef CONFIG_MOONWATER_FLOODLIGHT_ENV_SPLIT
	"env flag -S deny; env flag --split-string deny;"
#endif
#ifndef CONFIG_MOONWATER_FLOODLIGHT_LAUNCHERS
	"timeout run deny; nice run deny; nohup run deny; stdbuf run deny;"
	"setsid run deny; flock run deny; chrt run deny; ionice run deny;"
	"taskset run deny; prlimit run deny; choom run deny; uclampset run deny;"
	"coresched run deny; setpgid run deny; pipesz run deny;"
#endif
#ifndef CONFIG_MOONWATER_FLOODLIGHT_NAMESPACES
	"unshare run deny; nsenter run deny;"
	"chroot run deny; pivot_root run deny; setpriv run deny;"
#endif
#ifndef CONFIG_MOONWATER_FLOODLIGHT_NETWORK_FETCH
	"wget run deny; fetch run deny;"
#endif
#endif
	"";

/* Rows the configuration may add. Every one is a row of the report, and the
 * shell's reader is sized from this, so it is a limit rather than a hint. */
#define CONFIGURED 64

struct configured {
	char subject[SUBJECT];
	char detail[DETAIL];
	unsigned char setting;
	unsigned char allowed;
};

static struct configured configured[CONFIGURED] __ro_after_init;
static unsigned int configured_count __ro_after_init;
static bool configured_invalid __ro_after_init;

/*
 * Every deviation from those answers, and the only policy on this machine that
 * is writable at all.
 *
 * Zero at boot, so it is .bss and costs the kernel image nothing. The single
 * array this replaced carried seven kilobytes of initialised data -- four
 * times the size of all the code here -- to hold twenty-eight short names.
 *
 * Small on purpose. A policy that needs dozens of exceptions is one nobody is
 * reading, and being unable to add the dozenth is the correct outcome.
 */
#define CHANGES 16

struct light {
	char subject[SUBJECT];
	char detail[DETAIL];
	unsigned char setting;
	unsigned char allowed;
	unsigned int who;
	unsigned long long when; /* realtime seconds when it was changed */

	/* Everything above, folded with the boot secret. A write that reached
	 * this row without coming through the device does not know to update
	 * it, and every reader checks. */
	u32 seal;
};

static struct light changed[CHANGES];

static DEFINE_MUTEX(lock);

/* One way. Nothing here clears it and no parameter relaxes it: a seal that
 * something can undo is a seal in name only. */
static bool sealed;

/*
 * ---------------------------------------------------------------------------
 * Floodlight's own protections
 * ---------------------------------------------------------------------------
 *
 * Everything below exists because the kernel's own hardening is a set of
 * config options, and a security feature that is only secure on a kernel
 * somebody remembered to configure is not one. This machine's own kernel has
 * CONFIG_HARDENED_USERCOPY, CONFIG_SLAB_FREELIST_HARDENED and the stack
 * protector all switched off. So floodlight does not ask for any of them: it
 * carries its own, and behaves the same on a kernel built without a single
 * hardening option set.
 *
 * What it cannot do is stop a write it does not mediate. What it can do is
 * notice one and refuse to keep answering as though nothing happened, which is
 * the property this file is for: not that the policy cannot be attacked, but
 * that the report cannot be made to lie about it.
 */

/*
 * Drawn once, at boot, and never shown.
 *
 * Every checksum here is folded with it, so somebody who has read this source
 * still cannot compute a seal that will pass -- they have to find and read
 * this word first. Without it the seals would be arithmetic anybody could
 * redo, which is a speed bump wearing a lock's clothes.
 */
static u32 secret __ro_after_init;

/*
 * Set when something that should not have changed has. One way, like the seal:
 * a machine that has been tampered with does not get to go back to being
 * trusted because the next write looked fine.
 */
static bool compromised;

/* FNV-1a: one multiply and one xor a byte, which is fast enough that every
 * reader can afford to check and small enough to read in one sitting. */
static u32 fold(u32 hash, const void *from, unsigned int bytes)
{
	const unsigned char *at = from;

	while (bytes--)
		hash = (hash ^ *at++) * 16777619u;

	return hash;
}

/* A row's seal covers everything about it except the seal itself. */
static u32 seal_of(const struct light *row)
{
	return fold(secret ^ 2166136261u, row,
		    offsetof(struct light, seal));
}

/*
 * The built-in answers are const and the kernel write-protects .rodata -- when
 * it was built to. This is the same question asked without depending on that:
 * the answers are summed at boot and the sum is checked before they are used.
 */
static u32 baseline_sum __ro_after_init;

static u32 baseline_seal(void)
{
	u32 sum = fold(secret ^ 2166136261u, baseline, sizeof(baseline));

	/* The configured rows are built-in answers too, and written once at
	 * boot like the secret; the same sum covers them and how many there are. */
	sum = fold(sum, configured, sizeof(configured));
	return fold(sum, &configured_count, sizeof(configured_count));
}

/*
 * The one buffer a line is parsed in, with floodlight's own redzone either
 * side of it.
 *
 * Not the stack, because this kernel has no stack protector and the generated
 * code put the old array two bytes below the saved registers. Not the heap,
 * because this kernel has no freelist hardening either -- and an allocation
 * per policy change is a dependency on the allocator being sound that buys
 * nothing a static buffer does not already give. The guards are checked after
 * every parse, so an overrun is caught here whatever the kernel was built to
 * catch, and caught before the parsed line is acted on.
 */
#define LINE (SUBJECT + DETAIL + 32)
#define GUARD 16

static struct {
	unsigned char before[GUARD];
	char line[LINE];
	unsigned char after[GUARD];
} parse;

/*
 * The two bytes the guards are filled with, worked out once.
 *
 * Never zero, because .bss begins as zeros and a guard that happened to be
 * zero is one an overrun writing zeros walks straight through -- and the low
 * byte of a random word is zero one boot in two hundred and fifty six.
 *
 * Never the same as each other either, so a run of one repeated value is
 * caught whichever end it started from. xor with a fixed non-zero byte gives
 * both properties without another draw from the pool.
 */
static unsigned char guard_head __ro_after_init;
static unsigned char guard_tail __ro_after_init;

static void guard_arm(void)
{
	guard_head = (unsigned char)secret;
	if (!guard_head || guard_head == 0x5a)
		guard_head = 0xa5;

	guard_tail = guard_head ^ 0x5a;

	memset(parse.before, guard_head, GUARD);
	memset(parse.after, guard_tail, GUARD);
}

static bool guard_intact(void)
{
	unsigned int i;

	for (i = 0; i < GUARD; i++)
		if (parse.before[i] != guard_head ||
		    parse.after[i] != guard_tail)
			return false;

	return true;
}

/*
 * Everything that should not have changed, checked together.
 *
 * Called before the register is read and before it is written, so a tampered
 * machine is caught on the next thing anybody does rather than at some later
 * moment nobody chose.
 */
static bool intact(void)
{
	unsigned int i;

	lockdep_assert_held(&lock);

	if (compromised || configured_invalid)
		return false;

	if (baseline_seal() != baseline_sum) {
		compromised = true;
		pr_alert("floodlight: the built-in answers have been altered in memory; refusing to answer further\n");
		return false;
	}

	for (i = 0; i < CHANGES; i++) {
		if (!changed[i].subject[0])
			continue;
		if (seal_of(&changed[i]) == changed[i].seal)
			continue;

		compromised = true;
		pr_alert("floodlight: a deviation was altered without going through this device; refusing to answer further\n");
		return false;
	}

	if (!guard_intact()) {
		compromised = true;
		pr_alert("floodlight: the parse buffer was overrun; refusing to answer further\n");
		return false;
	}

	return true;
}

/*
 * A name is letters, digits, and the few marks a path or a flag needs.
 *
 * This is the most important check in the file, and it is not about parsing.
 * A subject written here is printed twice: into the kernel log, which this
 * machine draws on its own screen through a terminal emulator, and into the
 * device, which is read with cat into somebody's terminal. Both of those
 * interpret escape sequences, and one of them interprets newlines as the end
 * of a row.
 *
 * So without this, a name of "x\e[2K\e[A" erases the line above it as it is
 * printed -- the previous change, scrolled off the evidence -- and a name
 * containing a newline prints a second row that was never in the array and
 * can say anything, including "built in". An attacker who has to be root to
 * write here at all is exactly the attacker this file exists to keep honest,
 * and both tricks would let them change the machine and leave the report
 * saying they had not.
 *
 * Written out rather than reached for through strchr and isalnum, because
 * this is the one function in here that has to be right on sight.
 */
static bool plain(const char *word)
{
	const char *at = word;

	for (; *at; at++) {
		if (*at >= 'a' && *at <= 'z')
			continue;
		if (*at >= 'A' && *at <= 'Z')
			continue;
		if (*at >= '0' && *at <= '9')
			continue;
		if (*at == '.' || *at == '_' || *at == '-')
			continue;
		if (*at == '/' || *at == '+' || *at == ':')
			continue;

		return false;
	}

	return at != word;
}

/*
 * The deviation for one subject, if there is one.
 *
 * Every row, not up to the first used one: a row is given back when a setting
 * returns to what it was built as, so the array has holes in it, and a scan
 * that stopped at the first hole would miss everything past it. Sixteen
 * comparisons is nothing beside the syscall that got here.
 */
static struct light *find(const char *subject, unsigned int setting,
			  const char *detail)
{
	int i;

	lockdep_assert_held(&lock);

	for (i = 0; i < CHANGES; i++) {
		struct light *row = &changed[i];

		/* The name marks the row in use, never the timestamp: a machine
		 * with no clock set boots at the epoch, and a change made in
		 * that first second would write a zero and hand its own row
		 * straight back -- losing the change and leaving the report
		 * saying it never happened. */
		if (!row->subject[0])
			continue;
		if (row->setting != setting || strcmp(row->subject, subject))
			continue;
		if (setting == FLAG && strcmp(row->detail, detail))
			continue;

		return row;
	}

	return NULL;
}

/* The configured row for one subject, setting and flag, if there is one. */
static const struct configured *configured_row(const char *subject,
					       unsigned int setting,
					       const char *detail)
{
	unsigned int i;

	for (i = 0; i < configured_count; i++)
		if (configured[i].setting == setting &&
		    !strcmp(configured[i].subject, subject) &&
		    !strcmp(configured[i].detail, detail))
			return &configured[i];

	return NULL;
}

/*
 * What this kernel was built and configured believing about one subject,
 * setting and flag: 1 allowed, 0 refused, and -1 for nothing at all. The
 * configured rows are asked first, so a configuration can refuse what the
 * array allows; the array has no flag rows, so a flag is only ever configured.
 */
static int built(const char *subject, unsigned int setting, const char *detail)
{
	const struct configured *row = configured_row(subject, setting, detail);
	unsigned int i;

	if (row)
		return row->allowed;

	if (setting == FLAG)
		return -1;

	for (i = 0; i < ARRAY_SIZE(baseline); i++)
		if (baseline[i].setting == setting &&
		    !strcmp(baseline[i].subject, subject))
			return baseline[i].allowed;

	return -1;
}

/* One row of the report, however the answer was arrived at. */
static void say(struct seq_file *seq, const char *subject, unsigned int setting,
		const char *detail, bool allowed, struct light *row)
{
	unsigned long long now = ktime_get_real_seconds();

	seq_printf(seq, "%-16s %-8s%s%-*s %-5s ", subject,
		   /* Always in range by construction. Checked anyway: this
		    * index reads a pointer out of a table and prints what it
		    * points at, which is the last thing that should trust
		    * memory to be as it was left. */
		   setting < SETTINGS ? setting_name[setting] : "?",
		   setting == FLAG ? " " : "",
		   setting == FLAG ? DETAIL - 1 : 0, setting == FLAG ? detail : "",
		   allowed ? "allow" : "deny");

	if (!row) {
		seq_puts(seq, "built in\n");
		return;
	}

	/* The wall clock can be set backwards, and an elapsed time that
	 * underflows prints as six hundred billion years -- which reads as a
	 * broken machine rather than as a moved clock. */
	seq_printf(seq, "changed %llus ago by uid %u\n",
		   now > row->when ? now - row->when : 0, row->who);
}

/*
 * Everything, every time.
 *
 * The built-in answers first, each showing the deviation if it has one, then
 * anything added since boot that this kernel was not built knowing about. A
 * row nobody has touched says "built in"; one somebody has says who and when,
 * so a machine that has been changed cannot look like one that has not.
 */
static int floodlight_show(struct seq_file *seq, void *unused)
{
	unsigned int i;

	mutex_lock(&lock);

	/* Nobody tampered with anything; the configuration said something that
	 * does not read, and the only honest answer to that is none. */
	if (configured_invalid) {
		seq_puts(seq, "# floodlight: the configured policy does not read; refusing to answer until it is fixed\n");
		mutex_unlock(&lock);
		return 0;
	}

	if (!intact()) {
		seq_puts(seq, "# floodlight: TAMPERED -- this register has been written to behind its own back and no longer answers\n");
		mutex_unlock(&lock);
		return 0;
	}

	seq_printf(seq, "# floodlight%s\n", sealed ? " (sealed)" : "");

	for (i = 0; i < ARRAY_SIZE(baseline); i++) {
		const struct rule *rule = &baseline[i];
		struct light *row = find(rule->subject, rule->setting, "");

		/* A configured row says what this one would, once. */
		if (configured_row(rule->subject, rule->setting, ""))
			continue;

		say(seq, rule->subject, rule->setting, "",
		    row ? row->allowed : rule->allowed, row);
	}

	for (i = 0; i < configured_count; i++) {
		const struct configured *rule = &configured[i];
		struct light *row = find(rule->subject, rule->setting,
					 rule->detail);

		say(seq, rule->subject, rule->setting, rule->detail,
		    row ? row->allowed : rule->allowed, row);
	}

	for (i = 0; i < CHANGES; i++) {
		struct light *row = &changed[i];

		if (!row->subject[0])
			continue;
		/* Already shown beside the built-in answer it deviates from. */
		if (built(row->subject, row->setting, row->detail) >= 0)
			continue;

		say(seq, row->subject, row->setting, row->detail,
		    row->allowed, row);
	}

	mutex_unlock(&lock);
	return 0;
}

/* One word at a time out of the line, so the parse has no allocation, no
 * length to get wrong, and nothing to leave behind on a bad line. */
static char *word(char **at)
{
	char *start = *at;

	while (*start == ' ' || *start == '\t')
		start++;
	if (!*start)
		return NULL;

	*at = start;
	while (**at && **at != ' ' && **at != '\t')
		(*at)++;
	if (**at)
		*(*at)++ = 0;

	return start;
}

/*
 * The rest of a row once its subject is out of the line: the setting, the flag
 * a flag row names, allow or deny, and nothing after it, every name short and
 * plain. One reader for the configured text and for a line written to the
 * device, so that what boot takes for a row and what a write takes for one
 * cannot drift apart.
 */
struct row_text {
	const char *detail;
	unsigned int setting;
	bool allow;
};

static bool row_take(char *at, const char *subject, struct row_text *row)
{
	char *setting = word(&at), *state = word(&at);
	unsigned int i;

	row->detail = "";
	if (!setting || !state)
		return false;

	for (i = 0; i < SETTINGS && strcmp(setting, setting_name[i]); i++)
		;
	if (i == SETTINGS)
		return false;

	if (i == FLAG) {
		row->detail = state;
		state = word(&at);
		if (!state)
			return false;
	}

	if (word(&at) || (strcmp(state, "allow") && strcmp(state, "deny")))
		return false;
	if (strlen(subject) >= SUBJECT || strlen(row->detail) >= DETAIL)
		return false;
	if (!plain(subject) || (i == FLAG && !plain(row->detail)))
		return false;

	row->setting = i;
	row->allow = !strcmp(state, "allow");
	return true;
}

/*
 * The configured text, read into rows at boot.
 *
 * Each row is copied into the one parse buffer and taken apart by the same
 * word() and plain() a written line is, and held to more: nothing may follow
 * the state, and "seal" is not a row -- sealing at boot is its own switch.
 * Answers false for a text any row of which does not read, having kept none
 * of it: the caller refuses to answer rather than answer with part.
 */
static bool __init configure(const char *text)
{
	unsigned int count = 0;

	while (*text) {
		char *at = parse.line, *subject;
		struct row_text row;
		unsigned int length = 0, i, j;

		while (text[length] && text[length] != ';' && text[length] != '\n')
			length++;
		if (length >= LINE)
			return false;
		for (i = 0; i < length; i++)
			parse.line[i] = text[i];
		parse.line[length] = 0;
		text += length + (text[length] != 0);

		subject = word(&at);
		if (!subject)
			continue; /* nothing between two semicolons */

		/* Sealing at boot is its own switch. */
		if (!strcmp(subject, "seal") || !row_take(at, subject, &row))
			return false;

		for (j = 0; j < count; j++)
			if (configured[j].setting == row.setting &&
			    !strcmp(configured[j].subject, subject) &&
			    !strcmp(configured[j].detail, row.detail))
				break;
		if (j < count)
			continue; /* named before, and the first says it */

		if (count == CONFIGURED)
			return false;

		strscpy(configured[count].subject, subject, SUBJECT);
		strscpy(configured[count].detail, row.detail, DETAIL);
		configured[count].setting = row.setting;
		configured[count].allowed = row.allow;
		count++;
	}

	configured_count = count;
	return true;
}

static ssize_t floodlight_write(struct file *file, const char __user *from,
				size_t count, loff_t *offset)
{
	char *line, *at, *subject;
	const char *detail;
	struct row_text text;
	struct light *row;
	int rule;
	unsigned int i;
	bool allow, was;
	long answer;

	/* Before anything else, so an unprivileged caller cannot even make the
	 * copy below, let alone reach the parse behind it. */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (count >= LINE)
		return -EINVAL;

	/* Held across the parse as well as the change, because the buffer the
	 * line is parsed in is floodlight's own and there is one of it. */
	mutex_lock(&lock);

	if (!intact()) {
		answer = -EPERM;
		goto out;
	}

	/*
	 * The copy happens with the lock held, because the buffer it lands in
	 * is floodlight's own and there is one of it. That means a caller can
	 * hold the lock for as long as the fault behind an unmapped address
	 * takes to service, and every reader waits. It is allowed to stay that
	 * way because getting here at all needs CAP_SYS_ADMIN, and somebody who
	 * has that can seal the register or change the policy outright -- a
	 * stalled read is not the worst thing available to them.
	 */
	line = parse.line;
	if (copy_from_user(line, from, count)) {
		answer = -EFAULT;
		goto out;
	}

	line[count] = 0;

	/* The guards are checked before a single byte of this is believed. */
	if (!guard_intact()) {
		compromised = true;
		pr_alert("floodlight: the parse buffer was overrun; refusing to answer further\n");
		answer = -EPERM;
		goto out;
	}

	at = line;
	if (count && line[count - 1] == '\n')
		line[count - 1] = 0;

	subject = word(&at);
	if (!subject) {
		answer = -EINVAL;
		goto out;
	}

	if (!strcmp(subject, "seal")) {
		bool already;

		/*
			The whole line, or not the command.

			Matched on the first word alone, "seal anything at all"
			sealed the register and threw the rest away -- so a
			mistyped line closed the machine to further change, and
			a deliberate one did it while looking like something
			else. There is nothing to say after it.
		*/
		if (word(&at)) {
			answer = -EINVAL;
			goto out;
		}

		already = sealed;
		sealed = true;

		/* Only the once. A line that can be repeated is a line that can
		 * be repeated until the record above it has scrolled out of
		 * the log. */
		if (!already)
			pr_warn("floodlight: sealed by uid %u; no change until reboot\n",
				from_kuid(&init_user_ns, current_uid()));

		answer = count;
		goto out;
	}

	/* Words after the state are refused, as after seal: a line that says
	 * more than a row is not the row it starts as. Nothing is let through
	 * that could rewrite the report it is about to appear in, either. */
	if (!row_take(at, subject, &text)) {
		answer = -EINVAL;
		goto out;
	}

	i = text.setting;
	detail = text.detail;
	allow = text.allow;
	answer = count;
	if (sealed) {
		answer = -EPERM;
		goto out;
	}

	rule = built(subject, i, detail);
	row = find(subject, i, detail);

	/*
	 * What the machine answers now: the deviation if there is one, else
	 * what it was built with, else nothing at all -- a subject this kernel
	 * has never heard of, which the first write is entitled to introduce.
	 */
	if (row)
		was = row->allowed;
	else if (rule >= 0)
		was = rule;
	else
		was = !allow;

	/* Saying again what is already true is not a change, and a line that
	 * can be repeated is a line that can be repeated until the record
	 * above it has scrolled out of the log. */
	if (was == allow && (row || rule >= 0))
		goto out;

	/*
	 * Back to what it was built as, so the deviation is given back rather
	 * than kept saying the same thing the baseline already says. The report
	 * then calls the row "built in" again, which is the truth.
	 */
	if (rule >= 0 && allow == rule) {
		if (row)
			memset(row, 0, sizeof(*row));

		pr_warn("floodlight: %s restored to built in by uid %u\n",
			subject, from_kuid(&init_user_ns, current_uid()));
		goto out;
	}

	if (!row) {
		unsigned int free;

		for (free = 0; free < CHANGES && changed[free].subject[0]; free++)
			;
		if (free == CHANGES) {
			answer = -ENOSPC;
			goto out;
		}

		row = &changed[free];

		/* Already refused above if either would not fit; bounded again
		 * here because this is the copy, and a bound that lives at the
		 * copy cannot be separated from it by a later edit. */
		if (strscpy(row->subject, subject, SUBJECT) < 0 ||
		    strscpy(row->detail, detail, DETAIL) < 0) {
			memset(row, 0, sizeof(*row));
			answer = -EINVAL;
			goto out;
		}

		row->setting = i;
	}

	row->allowed = allow;

	/* Taken here, never from the line: a writer that could supply these
	 * could say the change was somebody else's, at some other time. */
	row->who = from_kuid(&init_user_ns, current_uid());
	row->when = ktime_get_real_seconds();
	row->seal = seal_of(row);

	/*
	 * Said out loud. This machine draws its kernel log on its own screen,
	 * so a change is visible on the glass without anybody going to look.
	 */
	pr_warn("floodlight: %s %s%s%s for %s by uid %u\n",
		allow ? "allowed" : "denied", setting_name[i],
		i == FLAG ? " " : "", i == FLAG ? detail : "",
		subject, row->who);

out:
	/* Nothing is left in it between commands: the line held a policy
	 * somebody typed, and there is no reason for it to still be here. */
	memset(parse.line, 0, LINE);

	mutex_unlock(&lock);
	return answer;
}

static int floodlight_open(struct inode *inode, struct file *file)
{
	/*
	 * misc_open leaves the miscdevice in private_data for a driver that
	 * wants to know which node it was opened through, and seq_open warns
	 * if it finds anything there at all -- so every process that read this
	 * register put a kernel warning in the log, once each, for the whole
	 * life of the machine. Nothing below wants the pointer: the register is
	 * one device and one array.
	 */
	file->private_data = NULL;

	return single_open(file, floodlight_show, NULL);
}

static const struct file_operations floodlight_ops = {
	.owner = THIS_MODULE,
	.open = floodlight_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = floodlight_write,
};

/*
 * Read only once the machine is up. misc_open reaches into this structure for
 * its file operations on every open, so a stray write into it would redirect
 * every future reader of the device; after init the pages it sits in are not
 * writable and that redirection is not available.
 */
/*
 * A fixed minor, for the same reason spark has one.
 *
 * There is no devtmpfs when this registers: Moonwater's core mounts it during
 * its own init, and this module deliberately depends on nothing of Moonwater's
 * -- so a kernel built with the register and without the core would have
 * registered a device that never got a node, and the shell would have fallen
 * back to its built-in answers for ever with nothing to say why. The node is
 * made in the initramfs instead, and both sides have to agree on the number.
 * 240-254 is the range set aside for local use; spark has 250.
 */
#define FLOODLIGHT_DEVICE_MINOR 249

static struct miscdevice floodlight_device __ro_after_init = {
	.minor = FLOODLIGHT_DEVICE_MINOR,
	.name = "floodlight",
	.fops = &floodlight_ops,
	.mode = 0644, /* anyone may read what is allowed; only root may change it */
};

static int __init floodlight_start(void)
{
	int answer;

	/* Before the device exists, so nothing can be answered or written
	 * until the seals it will be checked against are in place. */
	secret = get_random_u32();
	guard_arm();

	configured_invalid = !configure(configured_text);
	if (configured_invalid) {
		memset(configured, 0, sizeof(configured));
		configured_count = 0;
		pr_alert("floodlight: the configured policy has a row this register cannot read; every launch is refused until CONFIG_MOONWATER_FLOODLIGHT_POLICY is fixed\n");
	}
	memset(parse.line, 0, LINE);
	if (!guard_intact()) {
		compromised = true;
		pr_alert("floodlight: the parse buffer was overrun; refusing to answer further\n");
	}

	baseline_sum = baseline_seal();

	/*
	 * Sealed before the device exists, so there is no moment a write could
	 * land first: the configuration is the whole policy until reboot, and
	 * root can read it but not loosen it.
	 */
#ifdef CONFIG_MOONWATER_FLOODLIGHT_SEAL
	sealed = true;
	pr_warn("floodlight: sealed at boot by the kernel's configuration; no change until reboot\n");
#endif

	answer = misc_register(&floodlight_device);
	if (answer)
		pr_err("floodlight: no device, so no policy (%d)\n", answer);

	return answer;
}

/*
 * late_initcall, not device_initcall.
 *
 * The secret this file's seals are folded with comes from the random pool, and
 * the pool is not necessarily seeded as early as the device level -- a secret
 * drawn before it is seeded is one an attacker has a chance of guessing, which
 * is the one thing it exists not to be. Nothing in the kernel consults this
 * register, and userspace does not start until every initcall has run, so
 * waiting costs nothing and there is no window where a program could ask
 * before the answers were ready.
 */
late_initcall(floodlight_start);

MODULE_DESCRIPTION("Runtime allowances, in one array and in plain sight");
MODULE_AUTHOR("Dawn Larsson");
MODULE_LICENSE("GPL");
