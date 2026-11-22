/*
        The host layer the link needs on Darwin: the names that src/sh/host.c
        gives the link's verbs and listener on Linux, for a Mac. host.c is the
        box's moonwater (EFI, disks, mounts, the machine's own state) and is not
        built on a Mac; the link takes these from here instead. Included by
        service.c on Darwin only (MACOS), before any of the link's paths are
        named, and after lib.c and file.c, as host.c is after them in the shell.
        The Linux build never includes this file, and host.c is unchanged.

        WHERE THE LINK KEEPS ITS STATE

        link_state_dir() is the one choice of directory on Darwin:
        ~/Library/Application Support/moonwater, made with mode 0700 when it
        is not there. Every path the link keeps (the switch, the key, the
        machines, the groups, the port, the transport, the modem's counts, the
        lock, the state) is link_state_path(LINK_LEAF_*): that directory and a
        leaf name. Where HOME is missing or the directory cannot be made the
        directory is empty, so a path under it is a name that does not open and
        nothing is written in some other place. Linux keeps /root and
        /run/moonwater, as literals in service.c.

        THE ROOT CHECK

        host_need_root() does nothing on Darwin. The client runs as the user,
        and the link's verbs are the user's own: the keys, the machines and the
        groups are files of that user, kept in the directory above. The box's
        root-only commands are still root-only there: host.c keeps its check.
        bowl_is_root (bowl.c) is unchanged and still answers whether this
        process is root, which on a Mac it is not; nothing in the link asks it.

        THE LOCK AND THE CLOCK

        The lock constants are the platform's own (fcntl.h): F_SETLK is 8 on
        Darwin, where Linux's is 6. struct flock has Darwin's layout, which is
        the one the kernel reads. HOST_CLOCK_BOOTTIME is CLOCK_MONOTONIC here:
        Darwin has no boot clock that clock_gettime reads, and the waits the
        link makes are seconds long.

        The helpers below are host.c's, the ones the link calls, with the
        Linux-only parts (inotify, dup3, exit_group, the termios byte layout,
        the state directory under /run) replaced. Where host.c's are generic
        they are repeated here, not shared: host.c is the box's file and is not
        restructured on a Mac's account.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#if !defined(MACOS)
#error "host_darwin.c is the Darwin host layer; the Linux build uses src/sh/host.c"
#endif

/*
        Only the headers whose names do not collide with lib.c's. lib.c spells
        O_NONBLOCK, AT_FDCWD, the signals, the socket constants and errno
        with Darwin's values on Darwin, as sys/fcntl.h, sys/signal.h and
        sys/socket.h spell them (service.c includes those after it), so a
        header that is included meets the same macro. The few Darwin values
        this file needs that lib.c has no name for are written here, from
        sys/fcntl.h and netinet/in.h, with their structs in Darwin's layout.
*/
#include <ifaddrs.h>
#include <net/if.h>
#include <termios.h>

/*
        THE FILES

        The names of src/sh/file.c the link calls, on libSystem: a look at a
        name or an open descriptor (fstatat, or fstat for the empty name the
        Linux build hands AT_EMPTY_PATH with), a whole file published beside
        its name and renamed over it, the transfer loop, the umask, the
        environment, a reason's words and the two plain writers. file.c is the
        shell's, statx and openat2 all through, and is not built on a Mac.
        file_facts keeps the fields the link reads.
*/

#define ERROR_EXISTS 17
#define ERROR_NAME_TOO_LONG 36
#define ERROR_INPUT_OUTPUT 5
#define MODE_FORMAT 0170000
#define MODE_SOCKET 0140000
#define MODE_LINK 0120000
#define MODE_FILE 0100000
#define MODE_BLOCK 0060000
#define MODE_DIRECTORY 0040000
#define MODE_CHARACTER 0020000
#define MODE_PIPE 0010000
#define FILE_STAGED_NO_REPLACE 4
#define FILE_STAGED_EXACT_MODE 8

typedef struct
{
        b64 seconds;
        p32 nanoseconds;
        b32 reserved;
} file_moment;

//      The fields the link reads, filled from Darwin's stat.
typedef struct
{
        p32 mode;
        p32 owner;
        p32 group;
        p64 inode;
        p64 size;
        p64 device;
        file_moment modified;
        file_moment changed;
} file_facts;

extern char *strerror(int number);
extern char **environ;
extern int mkstemp(char *template_name);
extern int fsync(int handle);
extern int link(const char *from, const char *to);
extern int rename(const char *from, const char *to);
extern int unlink(const char *path);
extern long pwrite(int handle, const void *data, unsigned long length, long long offset);
extern long pread(int handle, void *data, unsigned long length, long long offset);
extern char *getenv(const char *name);

static bool file_look(bipolar directory, string_address path, positive flags,
                      file_facts address_to out)
{
        struct stat facts;
        int result = (path && path[0]) ? fstatat((int)directory, (const char *)path, &facts, (int)flags)
                                       : fstat((int)directory, &facts);

        memory_zero(out, sizeof(file_facts));
        if (result != 0)
                return false;
        out->mode = facts.st_mode;
        out->owner = facts.st_uid;
        out->group = facts.st_gid;
        out->inode = facts.st_ino;
        out->size = (p64)facts.st_size;
        out->device = (p64)(p32)facts.st_dev;
        out->modified.seconds = facts.st_mtimespec.tv_sec;
        out->modified.nanoseconds = (p32)facts.st_mtimespec.tv_nsec;
        out->changed.seconds = facts.st_ctimespec.tv_sec;
        out->changed.nanoseconds = (p32)facts.st_ctimespec.tv_nsec;
        return true;
}

static bool file_look_at(string_address path, file_facts address_to out)
{
        return file_look(AT_FDCWD, path, 0, out);
}

static bool file_same_identity(const file_facts address_to one, const file_facts address_to two)
{
        return one->inode == two->inode && one->device == two->device;
}

static positive file_umask(void)
{
        mode_t mask = umask(0);

        umask(mask);
        return mask & 0777;
}

static string_address address_to file_environment_all(void)
{
        return (string_address address_to)environ;
}

static string_address file_environment(string_address name)
{
        return (string_address)getenv((const char *)name);
}

static string_address file_reason(bipolar code)
{
        return (string_address)strerror((int)(code < 0 ? -code : code));
}

//      The writer convention: a zero length is a string to its terminator.
static fn plain_out(address_any data, positive length)
{
        (void)system_write_all(1, data, length ? length : string_length((string_address)data));
}

static fn plain_err(address_any data, positive length)
{
        (void)system_write_all(2, data, length ? length : string_length((string_address)data));
}

//      operation is the Linux build's syscall(pwrite64) or syscall(pread64);
//      on Darwin those name pwrite and pread (lib.c's table).
static bipolar file_transfer_exact(positive operation, bipolar handle,
                                   p8 address_to buffer, positive length,
                                   p64 offset)
{
        positive done = 0;

        while (done < length)
        {
                long moved = operation == (positive)(syscall(pwrite64))
                                 ? pwrite((int)handle, buffer + done, length - done, (long long)(offset + done))
                                 : pread((int)handle, buffer + done, length - done, (long long)(offset + done));

                if (moved < 0)
                {
                        if (*__error() == 4)
                                continue;
                        return -(bipolar)*__error();
                }
                if (moved == 0)
                        break;
                done += (positive)moved;
        }
        return (bipolar)done;
}

/*      A whole file, written beside its name and renamed over it: the old
        bytes or the new, never half of each. NO_REPLACE refuses a name that
        is there. */
static bipolar file_publish_with(string_address path, address_any bytes,
                                 positive length, positive mode, bool sync,
                                 positive behavior)
{
        p8 stage[1100];
        positive used = string_length(path);
        int handle;
        bipolar written;
        file_facts there;

        if (!used)
                return -ENOENT;
        if (used + 16 > sizeof(stage))
                return -ERROR_NAME_TOO_LONG;
        if ((behavior & FILE_STAGED_NO_REPLACE) && file_look_at(path, address_of there))
                return -ERROR_EXISTS;
        memory_copy(stage, path, used);
        memory_copy(stage + used, ".stage.XXXXXX", 14);
        handle = mkstemp((char *)stage);
        if (handle < 0)
                return -(bipolar)*__error();
        (void)fcntl(handle, F_SETFD, FD_CLOEXEC);
        (void)fchmod(handle, (mode_t)(mode & 07777));
        written = file_transfer_exact(syscall(pwrite64), handle, (p8 address_to)bytes, length, 0);
        if (written != (bipolar)length || (sync && fsync(handle) != 0))
        {
                (void)close(handle);
                (void)unlink((const char *)stage);
                return written < 0 ? written : -ERROR_INPUT_OUTPUT;
        }
        (void)close(handle);
        if (behavior & FILE_STAGED_NO_REPLACE)
        {
                if (link((const char *)stage, (const char *)path) != 0)
                {
                        bipolar error = -(bipolar)*__error();

                        (void)unlink((const char *)stage);
                        return error;
                }
                (void)unlink((const char *)stage);
                return 0;
        }
        if (rename((const char *)stage, (const char *)path) != 0)
        {
                bipolar error = -(bipolar)*__error();

                (void)unlink((const char *)stage);
                return error;
        }
        return 0;
}

static bipolar file_publish_bytes(string_address path, address_any bytes,
                                  positive length, positive mode, bool sync)
{
        return file_publish_with(path, bytes, length, mode, sync, 0);
}

/*
        THE SHELL'S NAMES

        system_clock_ns (snapshot.c on Linux) on Darwin's clock ids, which the
        link names by HOST_CLOCK_BOOTTIME and 0; shell_exec_file (shell.c),
        which here is execve of the path, the listener's own binary; and
        process_pty_open (pty.c) on posix_openpt.
*/
extern int execve(const char *path, char *const arguments[], char *const environment[]);
extern int posix_openpt(int flags);
extern int grantpt(int handle);
extern int unlockpt(int handle);
extern char *ptsname(int handle);

static positive system_clock_ns(b32 clock)
{
        return (positive)clock_gettime_nsec_np((clockid_t)clock);
}

bipolar shell_exec_file(string_address path, string_address address_to arguments,
                        positive count, string_address address_to environment)
{
        string_address words[64];

        if (count >= array_count(words))
                return -E2BIG;
        if (count)
                memory_copy(words, arguments, count * sizeof(string_address));
        words[count] = null;
        (void)execve((const char *)path, (char *const *)words, (char *const *)environment);
        return -darwin_errno();
}

static bipolar process_pty_open(b32 address_to master_out, b32 address_to slave_out,
                                bool nonblocking)
{
        int master = posix_openpt(O_RDWR | O_NOCTTY);
        char *name;
        int slave;

        if (master < 0)
                return -darwin_errno();
        name = grantpt(master) == 0 && unlockpt(master) == 0
                   ? ptsname(master) : null;
        slave = name ? open(name, O_RDWR | O_NOCTTY | O_CLOEXEC) : -1;
        if (slave < 0)
        {
                bipolar error = -darwin_errno();

                (void)close(master);
                return error;
        }
        (void)fcntl(master, F_SETFD, FD_CLOEXEC);
        (void)fcntl(slave, F_SETFD, FD_CLOEXEC);
        if (nonblocking)
        {
                int flags = fcntl(master, F_GETFL);

                if (flags >= 0)
                        (void)fcntl(master, F_SETFL, flags | O_NONBLOCK);
        }
        address_to master_out = master;
        address_to slave_out = slave;
        return 0;
}

/*      The client's terminal raw and back, as edit.c's pair does it on
        Linux: keys and not lines, no echo, no signals from the keys, no
        flow control and no output processing. */
static struct termios edit_modes_before;
static bool edit_modes_held;

static p8 edit_terminal_raw()
{
        struct termios modes;

        if (tcgetattr(0, &edit_modes_before) != 0)
                return false;
        modes = edit_modes_before;
        modes.c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR |
                                      IGNCR | ICRNL | IXON);
        modes.c_oflag &= ~(tcflag_t)OPOST;
        modes.c_cflag &= ~(tcflag_t)(CSIZE | PARENB);
        modes.c_cflag |= CS8;
        modes.c_lflag &= ~(tcflag_t)(ISIG | ICANON | ECHO | ECHONL | IEXTEN);
        modes.c_cc[VMIN] = 1;
        modes.c_cc[VTIME] = 0;
        if (tcsetattr(0, TCSANOW, &modes) != 0)
                return false;
        edit_modes_held = true;
        return true;
}

static fn edit_terminal_restore()
{
        if (!edit_modes_held)
                return;
        (void)tcsetattr(0, TCSANOW, &edit_modes_before);
        edit_modes_held = false;
}

/*      In the child: a session of its own, the terminal its controlling one
        (TIOCSCTTY, _IO('t', 97) on Darwin), and the slave on 0, 1 and 2. */
extern int setsid(void);
extern int ioctl(int handle, unsigned long request, ...);
extern int dup2(int from, int to);

static bipolar process_pty_child_setup(b32 master, b32 slave, bipolar close_one,
                                       bipolar close_two)
{
        if (close_one >= 0 && close_one != master && close_one != slave)
                (void)close((int)close_one);
        if (close_two >= 0 && close_two != close_one && close_two != master &&
            close_two != slave)
                (void)close((int)close_two);
        if (setsid() < 0 || ioctl(slave, 0x20007461ul, 0) < 0)
                return -darwin_errno();
        (void)close(master);
        for (int target = 0; target < 3; target++)
                if (dup2(slave, target) < 0)
                        return -darwin_errno();
        if (slave > 2)
                (void)close(slave);
        return 0;
}

#define log plain_out
#define log_error plain_err
#define host_label TERM_BOLD "[Moonwater]" TERM_RESET " "

/* CLOCK_MONOTONIC: see the note above. */
#define HOST_CLOCK_BOOTTIME 6

/* Darwin's record-lock commands and types (sys/fcntl.h), and its struct flock:
   off_t start, off_t length, pid_t pid, short type, short whence. */
#define HOST_FILE_GETLK 7
#define HOST_FILE_SETLK 8
#define HOST_FILE_SETLKW 9
#define HOST_FILE_WRLCK 3
#define HOST_FILE_UNLCK 2

typedef struct
{
        bipolar start;
        bipolar length;
        b32 pid;
        b16 type;
        b16 whence;
} link_flock;

/* struct ip_mreq (netinet/in.h): the group and the interface's address, both
   in network order. Darwin takes the interface by address, not by index. */
typedef struct
{
        p32 multiaddr;
        p32 interface;
} link_ip_mreq;

/*
        The link's files on Darwin, by leaf name. The order is the order of
        the enum, and each leaf's name is the Linux file's name without its
        directory: /root/link.key is link.key here, and /run/moonwater/link.lock
        is link.lock.
*/
enum
{
        LINK_LEAF_SWITCH,
        LINK_LEAF_KEY,
        LINK_LEAF_PEERS,
        LINK_LEAF_GROUPS,
        LINK_LEAF_PORT,
        LINK_LEAF_TRANSPORT,
        LINK_LEAF_MODEM,
        LINK_LEAF_MODEM_SEQ,
        LINK_LEAF_MODEM_RX,
        LINK_LEAF_MODEM_ZIP,
        LINK_LEAF_MODEM_STREAMS,
        LINK_LEAF_LOCK,
        LINK_LEAF_STATE,
        LINK_LEAF_STAMPS,
        LINK_LEAF_PEERS_LOCK,
        LINK_LEAF_PLACES,
        LINK_LEAF_HWIDS,
        LINK_LEAF_POLICY,
        LINK_LEAF_MACHINE,
        LINK_LEAF_COUNT
};

static const string_address link_leaf_names[LINK_LEAF_COUNT] = {
    "link",          "link.key",          "link.peers",
    "link.groups",   "link.port",         "link.transport",
    "link.modem",    "link.modem.seq",    "link.modem.rx",
    "link.modem.gz", "link.modem.streams", "link.lock",
    "link.state",    "link.stamps",       "link.peers.lock",
    "link.places",   "link.hwids",        "link.policy",
    "main.moonwater.sh",
};

#define LINK_STATE_ROOM 1024

static p8 link_dir_text[LINK_STATE_ROOM];
static bool link_dir_ready;
static p8 link_slot_text[LINK_LEAF_COUNT][LINK_STATE_ROOM];

/*
        The one choice of the link's directory on Darwin. Made once, on the
        first path asked for. The mode is exact (0700, whatever the umask):
        the keys and the machines in it are this user's and no one else's.
        An existing directory is fine; any other failure leaves the name
        empty, which is what a missing HOME is.
*/
static string_address link_state_dir(void)
{
        string_address home;

        if (link_dir_ready)
                return (string_address)link_dir_text;
        link_dir_ready = true;
        link_dir_text[0] = end;
        home = file_environment((string_address)"HOME");
        if (!home || home[0] != '/' || string_length(home) > (positive)(LINK_STATE_ROOM - 64))
                return (string_address)link_dir_text;
        string_copy_bounded((string_address)link_dir_text, home, LINK_STATE_ROOM);
        string_append_bounded((string_address)link_dir_text,
                              "/Library/Application Support/moonwater",
                              LINK_STATE_ROOM);
        {
                bipolar made = system_make_directory_exact_at(
                    AT_FDCWD, (string_address)link_dir_text, 0700);
                file_facts facts;

                if (made < 0 &&
                    (made != -EEXIST ||
                     !file_look(AT_FDCWD, (string_address)link_dir_text, 0x0020, address_of facts) ||
                     (facts.mode & MODE_FORMAT) != MODE_DIRECTORY))
                        link_dir_text[0] = end;
        }
        return (string_address)link_dir_text;
}

/*
        A leaf of the state directory. Each leaf has its own slot, so two paths
        named in one call (the modem's counts, say) are both right.
*/
static string_address link_state_path(positive leaf)
{
        p8 address_to slot;
        string_address dir;

        if (leaf >= LINK_LEAF_COUNT)
                return (string_address)"";
        slot = link_slot_text[leaf];
        dir = link_state_dir();

        slot[0] = end;
        if (dir[0])
        {
                string_copy_bounded((string_address)slot, dir, LINK_STATE_ROOM);
                string_append_bounded((string_address)slot, "/", LINK_STATE_ROOM);
                string_append_bounded((string_address)slot, link_leaf_names[leaf],
                                      LINK_STATE_ROOM);
        }
        return (string_address)slot;
}

/* The directory a command and a push start in, and the HOME a command is given:
   the user's own, where Linux's are root's /root. Empty when HOME is not set. */
static string_address link_run_home(void)
{
        string_address home = file_environment((string_address)"HOME");

        return home ? home : (string_address)"";
}

/*
        The listener's own binary, to exec as the listener. Darwin has no
        /proc/self/exe; _NSGetExecutablePath gives the path it was loaded from.
        Empty when that cannot be had, and an exec of nothing fails in the
        child, which exits 127 as an exec that fails does.
*/
static string_address link_executable(void)
{
        static p8 path[1024];
        p32 size = sizeof path; /* uint32_t on Darwin */

        path[0] = end;
        if (_NSGetExecutablePath((char *)path, &size) != 0)
                path[0] = end;
        else
                path[sizeof path - 1] = end;
        return (string_address)path;
}

#define HOST_MACHINE_SCRIPT link_state_path(LINK_LEAF_MACHINE)

/* A line to the writer, then the buffer out: the two calls every report ends on. */
#define host_say(...) \
        do \
        { \
                string_format(__VA_ARGS__); \
                log_flush(); \
        } while (0)

/* A refusal, in the voice of the rest: the label, the words and what they name. */
#define host_refuse(...) \
        ({ \
                string_format(log_error, host_label); \
                host_say(log_error, __VA_ARGS__); \
                (b32)1; \
        })

/* The root check the link's verbs open with. On Darwin the client is the
   user's own process, and the link's verbs are the user's; see the note above. */
#define host_need_root() ((void)0)

static b32 host_fail(string_address what, bipolar error)
{
        host_say(log_error, host_label "%s: %s\n", what, file_reason(error));
        return 1;
}

static fn host_pause(p64 nanoseconds)
{
        process_nap_ns(nanoseconds);
}

static bool host_starts(string_address text, string_address prefix)
{
        return !string_compare_max(text, prefix, string_length(prefix));
}

#include "../sh/host_words.h"

/* A lock file, made if it is not there. Not through a link: the lock is made
   where it is named. */
static bipolar host_lock_open(string_address path)
{
        return system_open_at_mode(AT_FDCWD, path,
                                   FILE_READ_WRITE | FILE_CREATE |
                                       O_NOFOLLOW | O_CLOEXEC,
                                   0600);
}

/* The file's handle, or the error, after command (F_GETLK, F_SETLK or
   F_SETLKW) over all of it; with owner, whose lock it is. The lock is held
   for as long as the handle is. */
static bipolar host_lock_file(string_address path, positive command,
                              b32 address_to owner)
{
        link_flock lock = {.type = HOST_FILE_WRLCK};
        bipolar handle = host_lock_open(path);
        bipolar asked;

        if (handle < 0)
                return handle;
        do
                asked = system_call_3(syscall(fcntl), (positive)handle, command,
                                      (positive)address_of lock);
        while (asked == -4);
        if (asked < 0)
        {
                system_close(handle);
                return asked;
        }
        if (owner)
                address_to owner = lock.type == HOST_FILE_UNLCK ? 0 : lock.pid;
        return handle;
}

static b32 host_lock_owner(string_address path)
{
        b32 owner = 0;
        bipolar handle = host_lock_file(path, HOST_FILE_GETLK, address_of owner);

        if (handle >= 0)
                system_close(handle);
        return owner;
}

/*
        A signal for whoever holds the lock. Darwin has no pidfd to send through,
        so this kills the pid, as service.c's session close does, after checking
        that it still holds the lock. Linux's pidfd pins the process between the
        check and the signal; Darwin's kill does not, so a pid that exited and was
        reused in that instant can be signalled. The holder is the listener, which
        lives until it is told to go, so the window is the one the rest of the
        link already accepts.
*/
static bool host_lock_signal(string_address path, b32 signal)
{
        b32 owner = host_lock_owner(path);

        if (!owner)
                return false;
        return host_lock_owner(path) == owner &&
               system_call_2(syscall(kill), (positive)owner, (positive)signal) >= 0;
}

/*
        The child of a fork made a daemon of its own: forked once more so
        that init reaps it and not whatever started it, a session of its
        own, /dev/null for its three descriptors and nothing else open but
        the two named (descriptors_none for a missing one). True in the one
        that is left; false in the caller, which has waited for the first
        child, and when nothing could be forked.
*/
static bool host_detach(p32 keep, p32 keep_too)
{
        positive status = 0;
        bipolar quiet;
        bipolar child = system_fork();

        if (child)
        {
                if (child > 0)
                        system_call_4(syscall(wait4), (positive)child,
                                      (positive)address_of status, 0, 0);
                return false;
        }
        //      The second fork: a negative answer is a failure, not the parent's turn to go (it was taken as one).
        {
                bipolar grandchild = system_fork();

                if (grandchild != 0)
                        system_call_1(syscall(exit), grandchild < 0 ? 1 : 0);
        }

        system_call(syscall(setsid));
        quiet = system_open_at(AT_FDCWD, "/dev/null", FILE_READ_WRITE | O_CLOEXEC);
        if (quiet >= 0 && quiet < 3)
                (void)fcntl((int)quiet, F_SETFD, 0);
        for (positive at = 0; quiet >= 0 && at < 3; at++)
                if ((positive)quiet != at)
                        system_call_2(syscall(dup2), (positive)quiet, at);
        (void)descriptors_close_except(3, keep, keep_too);
        return true;
}

/*
        A stage published over a state name, as host.c publishes one: the name
        is looked at again when the stage is renamed onto it, and a race that
        lost is written again over what is there now.
*/
static bipolar host_publish_word(string_address path, p8 address_to bytes,
                                 positive length, positive mode)
{
        bipolar result = file_publish_with(path, bytes, length, mode, true,
                                           FILE_STAGED_EXACT_MODE);

        for (positive attempt = 0; attempt < 16 &&
                                   (result == -EEXIST || result == -EAGAIN ||
                                    (result == -ENOENT && attempt < 2));
             attempt++)
                result = file_publish_with(path, bytes, length, mode, true,
                                           FILE_STAGED_EXACT_MODE);
        return result;
}

/* One word and its newline over a state file, unless it is already there. */
static bipolar radio_write_word(string_address path, string_address word)
{
        p8 line[96];
        p8 have[96];

        string_copy_bounded(line, word, sizeof(line));
        string_append_bounded(line, "\n", sizeof(line));
        if (host_read_state(path, have, sizeof(have)) == (bipolar)string_length(line) &&
            !string_compare(have, line))
                return 0;
        return host_publish_word(path, line, string_length(line), 0644);
}

/*
        A password, without argv: ps shows every process's arguments to every
        user. At a terminal it is asked for with the echo off and the line
        read here a byte at a time, with the terminal's line editing and
        Control-C and Control-D handled here, so the terminal is put back on
        every way out. Anywhere else it is one line of standard input.
        Negative when cancelled; else its length.
*/
static bipolar radio_password_read(p8 address_to into, positive room,
                                   string_address ssid, bool only_asked,
                                   bool group)
{
        struct termios saved;
        struct termios quiet;
        bool terminal = tcgetattr(0, &saved) == 0;
        positive used = 0;
        bipolar result = 0;

        if (!room)
                return -1;
        into[0] = end;
        if (only_asked && !terminal)
                return 0;
        if (terminal)
        {
                quiet = saved;
                quiet.c_lflag &= ~(tcflag_t)(ECHO | ICANON | ISIG);
                quiet.c_cc[VMIN] = 1;
                quiet.c_cc[VTIME] = 0;
                (void)tcsetattr(0, TCSANOW, &quiet);
                if (group)
                        host_say(log_error, host_label "secret for %s: ", ssid);
                else
                        host_say(log_error, host_label "password for %s (empty for an open network): ", ssid);
        }

        for (;;)
        {
                p8 byte;
                bipolar got = system_read_once(0, address_of byte, 1);

                if (got == -4)
                        continue;
                if (got <= 0)
                {
                        //      The end of input with nothing before it is no
                        //      answer: a pipe that failed would otherwise save
                        //      the network as an open one.
                        if (!used)
                                result = -1;
                        break;
                }
                if (byte == '\n' || byte == '\r')
                        break;
                if (terminal)
                {
                        if (byte == 3 || (byte == 4 && !used))
                        {
                                result = -1;
                                break;
                        }
                        if (byte == 0x7f || byte == 8)
                        {
                                while (used && (into[used - 1] & 0xc0) == 0x80)
                                        used--;
                                if (used)
                                        used--;
                                continue;
                        }
                        if (byte == 0x15)
                        {
                                used = 0;
                                continue;
                        }
                        if (byte < 32)
                                continue;
                }
                else if (byte < 32)
                {
                        //      Not what somebody typed: a NUL would end it
                        //      where it is kept.
                        result = -1;
                        break;
                }
                if (used + 1 < room)
                        into[used++] = byte;
        }

        if (terminal)
        {
                (void)tcsetattr(0, TCSANOW, &saved);
                host_say(log_error, "\n");
        }
        into[used] = end;
        //      A password given up on leaves nothing of it in the caller's bytes.
        if (result < 0)
                crypto_forget(into, room);
        return result < 0 ? result : (bipolar)used;
}

/*
        The link's verb lines, for `link` with a wrong argument and for the
        link's page. host.c's table has the box's pages; on Darwin the link
        has its own. Only "link" has a page here.
*/
static const string_address host_link_rows[] = {
    "  moonwater link                 who this machine is linked with, and what each may do",
    "  moonwater link pair [NAME]     a code, and wait for a machine to use it",
    "  moonwater link NAME CODE       link to the machine called NAME",
    "  moonwater link cancel          end every wait for a code",
    "  moonwater link NAME [COMMAND]  a terminal on NAME, or one command",
    "  moonwater link push NAME FILE PATH   a file there, whole or not at all",
    "  moonwater link pull NAME PATH FILE   and back",
    "  moonwater link log NAME        follow the far kernel log",
    "  moonwater link add NAME KEY [HOST[:PORT]]   link by key, with no code",
    "  moonwater link remove NAME",
    "  moonwater link allow NAME GRANT...",
    "  moonwater link deny NAME GRANT...",
    "  moonwater link group ...       machines on one network that link themselves",
    "  moonwater link on | off        the listener",
    "  moonwater link doctor          the listener, port, addresses, route, discovery",
    "  moonwater link transport [network|tcp|auto|modem]   how machines reach each other",
    "  moonwater link modem key       the modem's key, made on first use",
    "  moonwater link modem send FILE       FILE over the sound card, to the receiver",
};

static positive host_rows_write(writer out, string_address verb)
{
        if (!string_equals(verb, "link"))
                return 0;
        for (positive at = 0; at < array_count(host_link_rows); at++)
                string_format(out, "%s\n", host_link_rows[at]);
        return array_count(host_link_rows);
}

/* A wrong argument's page: the link's lines. */
static b32 host_usage(void)
{
        (void)host_rows_write(log_error, "link");
        log_flush();
        return 2;
}
