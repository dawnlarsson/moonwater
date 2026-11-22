/*
 *      input_probe: what the game controller switches and the render node
 *      do, run as root in a booted guest by test/run's input lane.
 *
 *      It makes a virtual joystick through /dev/uinput, waits for joydev's
 *      /dev/input/js* node for it, presses the button and reads the press back
 *      from the joystick node, then makes and destroys a DRM sync object on
 *      the first render node. Every step prints one line of its own, so the
 *      lane can say which step failed. Built static by the lane, with gcc and
 *      with clang: it needs no library but libc.
 *
 *      The DRM ioctl numbers are the uapi's (include/uapi/drm/drm.h, 7.3):
 *      DRM_IOWR(0xBF, drm_syncobj_create) and DRM_IOWR(0xC0,
 *      drm_syncobj_destroy). The lane checks them against the box's header.
 */
#include <errno.h>
#include <grp.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/joystick.h>
#include <linux/uinput.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

struct drm_syncobj_create {
        uint32_t handle;
        uint32_t flags;
};

struct drm_syncobj_destroy {
        uint32_t handle;
        uint32_t pad;
};

#define PROBE_DRM_SYNCOBJ_CREATE _IOWR('d', 0xBF, struct drm_syncobj_create)
#define PROBE_DRM_SYNCOBJ_DESTROY _IOWR('d', 0xC0, struct drm_syncobj_destroy)

#define PROBE_NAME "moonwater probe pad"

static int failures;

static void answer(const char *what, int ok)
{
        printf("%s %s\n", what, ok ? "ok" : "failed");
        if (!ok)
                failures++;
}

static int wait_for(int milliseconds, int (*ready)(void *), void *arg)
{
        for (int waited = 0; waited < milliseconds; waited += 50) {
                if (ready(arg))
                        return 1;
                usleep(50 * 1000);
        }
        return ready(arg);
}

static int joystick_named(void *arg)
{
        char name[128] = "";
        char path[32];
        int fd;
        int found = 0;

        (void)arg;
        for (int number = 0; number < 8 && !found; number++) {
                snprintf(path, sizeof path, "/dev/input/js%d", number);
                fd = open(path, O_RDONLY | O_NONBLOCK);
                if (fd < 0)
                        continue;
                if (ioctl(fd, JSIOCGNAME(sizeof name), name) >= 0 &&
                    strcmp(name, PROBE_NAME) == 0) {
                        printf("joystick node %s\n", path);
                        found = 1;
                }
                close(fd);
        }
        return found;
}

static int send_event(int fd, uint16_t type, uint16_t code, int32_t value)
{
        struct input_event event;

        memset(&event, 0, sizeof event);
        event.type = type;
        event.code = code;
        event.value = value;
        return write(fd, &event, sizeof event) == sizeof event;
}

/* Reads joystick events until a button press arrives or two seconds pass. */
static int read_press(int js)
{
        struct js_event event;

        for (int waited = 0; waited < 2000; waited += 100) {
                struct pollfd pfd = {.fd = js, .events = POLLIN};

                if (poll(&pfd, 1, 100) <= 0)
                        continue;
                if (read(js, &event, sizeof event) != sizeof event)
                        return 0;
                if ((event.type & ~JS_EVENT_INIT) == JS_EVENT_BUTTON &&
                    event.value == 1) {
                        printf("button %u pressed\n", event.number);
                        return 1;
                }
        }
        return 0;
}

/*
 *      --bowl-user: become uid 1000 in the input group (gid 104), as a bowl's
 *      user is: the nodes the desktop image makes are 0660 in that group.
 *      --bowl-user-no-input: the same user with no supplementary groups, which
 *      must be refused /dev/uinput. The render node is root's and skipped as
 *      the user; the root run checks it.
 */
#define PROBE_BOWL_UID 1000
#define PROBE_INPUT_GID 104

/*
 *      --focus: the focused page (src/moonwater/spark.c, spark_focus_page), on
 *      a guest with no compositor, so this program is the direct ring's owner
 *      and names the page's thread group itself, as the compositor does. A
 *      child is the game: it maps its page and claims it; a virtual mouse
 *      moves, presses and turns; the page, evdev and a second, unnamed
 *      child's page say where each report went. The numbers are spark.c's.
 */
#define PROBE_RING_PGOFF 16
#define PROBE_FOCUS_PGOFF 17
#define PROBE_FOCUS_MAGIC 0x53434f46u
#define PROBE_IOCTL_FOCUS 0x00007314u
#define PROBE_IOCTL_CLAIM 0x00007315u
#define PROBE_MOUSE "moonwater probe mouse"

struct probe_slot {
        int32_t dx, dy;
        uint64_t time_us;
        uint16_t kind, code;
        uint32_t flags;
};

struct probe_page {
        uint32_t magic, version, mask, state, generation, claim, head, buttons, drops, armed_ms, armed_head, reserved[5];
        struct probe_slot slots[128];
};

/* One byte from the other side of a pipe, or 0 after two seconds. */
static int hear(int fd)
{
        struct pollfd pfd = {.fd = fd, .events = POLLIN};
        unsigned char byte = 0;

        if (poll(&pfd, 1, 2000) <= 0 || read(fd, &byte, 1) != 1)
                return 0;
        return byte;
}

static void tell(int fd, unsigned char byte)
{
        if (write(fd, &byte, 1) != 1)
                printf("focus pipe write failed errno %d\n", errno);
}

/* The events evdev hands the mouse's node within 200 ms: REL and KEY, counted apart. */
static void evdev_heard(int fd, int *rel, int *key)
{
        struct input_event event;

        *rel = *key = 0;
        for (;;) {
                struct pollfd pfd = {.fd = fd, .events = POLLIN};

                if (poll(&pfd, 1, 200) <= 0 || read(fd, &event, sizeof event) != sizeof event)
                        return;
                *rel += event.type == EV_REL;
                *key += event.type == EV_KEY;
        }
}

static int mouse_named(void *arg)
{
        int *found = arg;
        char name[128], path[32];

        for (int number = 0; number < 32 && *found < 0; number++) {
                int fd;

                snprintf(path, sizeof path, "/dev/input/event%d", number);
                if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
                        continue;
                name[0] = 0;
                if (ioctl(fd, EVIOCGNAME(sizeof name), name) >= 0 && strcmp(name, PROBE_MOUSE) == 0)
                        *found = fd;
                else
                        close(fd);
        }
        return *found >= 0;
}

/* The game: its page, the refusals, and each step's page as the parent asks for it. */
static int focus_game(int to_parent, int from_parent, int claim, int named)
{
        int fd = open("/dev/spark", O_RDONLY | O_CLOEXEC), other;
        volatile struct probe_page *page;
        void *mapped;
        int step;

        if (fd < 0)
                return printf("focus game open errno %d\n", errno), 1;
        /* Private mappings, which the file's mode does not stop from writing: what refuses them is the page's own rule. */
        mapped = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, PROBE_FOCUS_PGOFF * 4096l);
        answer(named ? "focus a writable page is refused" : "focus unnamed a writable page is refused", mapped == MAP_FAILED);
        mapped = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, PROBE_FOCUS_PGOFF * 4096l);
        if (mapped == MAP_FAILED)
                return printf("focus game mmap errno %d\n", errno), 1;
        page = mapped;
        answer(named ? "focus the page is spark's" : "focus unnamed the page is spark's",
               page->magic == PROBE_FOCUS_MAGIC && page->version == 1 && page->mask == 127 && page->state == 0 && page->head == 0);
        if (named) {
                answer("focus the page cannot be made writable", mprotect(mapped, 4096, PROT_READ | PROT_WRITE) != 0);
                other = open("/dev/spark", O_RDONLY | O_CLOEXEC);
                answer("focus one page for a thread group",
                       other >= 0 && mmap(NULL, 4096, PROT_READ, MAP_SHARED, other, PROBE_FOCUS_PGOFF * 4096l) == MAP_FAILED && errno == EBUSY);
                if (other >= 0)
                        close(other);
                answer("focus naming is the ring owner's", ioctl(fd, PROBE_IOCTL_FOCUS, -1) != 0 && errno == EPERM);
                answer("focus a claim past take is refused", ioctl(fd, PROBE_IOCTL_CLAIM, 3) != 0 && errno == EINVAL);
        }
        answer(named ? "focus the game claims its page" : "focus unnamed claims its page", ioctl(fd, PROBE_IOCTL_CLAIM, claim) == 0);
        tell(to_parent, 'r');
        while ((step = hear(from_parent)) != 0 && step != 'q') {
                printf("focus page %c state %u generation %u head %u buttons %x", step, page->state, page->generation, page->head,
                       page->buttons);
                for (uint32_t at = 0; at < page->head && at < 8; at++)
                        printf(" [%u %u %d %d]", page->slots[at].kind, page->slots[at].code, page->slots[at].dx, page->slots[at].dy);
                printf(" zero %d\n", page->slots[0].kind == 0 && page->slots[0].code == 0 && page->slots[0].dx == 0 &&
                                         page->slots[0].time_us == 0);
                if (step == 'o')
                        answer("focus the game claims observe", ioctl(fd, PROBE_IOCTL_CLAIM, 1) == 0);
                fflush(stdout);
                tell(to_parent, step);
        }
        return failures ? 1 : 0;
}

static int focus_probe(void)
{
        int up[2], down[2], up2[2], down2[2], ring_fd, ui, ev = -1, rel, key, pidfd;
        struct uinput_user_dev device;
        void *ring;
        pid_t game, stranger;

        ring_fd = open("/dev/spark", O_RDWR | O_CLOEXEC);
        ring = ring_fd < 0 ? MAP_FAILED : mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, ring_fd, PROBE_RING_PGOFF * 4096l);
        answer("focus this program owns the direct ring", ring != MAP_FAILED);
        if (ring == MAP_FAILED)
                return printf("focus ring errno %d\n", errno), 1;
        answer("focus a name that is no pidfd is refused", ioctl(ring_fd, PROBE_IOCTL_FOCUS, 0) != 0);

        ui = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        answer("focus uinput open", ui >= 0);
        if (ui < 0)
                return 1;
        ioctl(ui, UI_SET_EVBIT, EV_REL);
        ioctl(ui, UI_SET_RELBIT, REL_X);
        ioctl(ui, UI_SET_RELBIT, REL_Y);
        ioctl(ui, UI_SET_RELBIT, REL_WHEEL);
        ioctl(ui, UI_SET_EVBIT, EV_KEY);
        ioctl(ui, UI_SET_KEYBIT, BTN_LEFT);
        ioctl(ui, UI_SET_KEYBIT, BTN_RIGHT);
        memset(&device, 0, sizeof device);
        strncpy(device.name, PROBE_MOUSE, UINPUT_MAX_NAME_SIZE - 1);
        device.id.bustype = BUS_VIRTUAL;
        device.id.vendor = 0x1d6b;
        device.id.product = 0x0002;
        answer("focus mouse created", write(ui, &device, sizeof device) == sizeof device && ioctl(ui, UI_DEV_CREATE) == 0);
        answer("focus mouse node found", wait_for(5000, mouse_named, &ev));

        if (pipe(up) || pipe(down) || pipe(up2) || pipe(down2))
                return 1;
        fflush(stdout);
        if ((game = fork()) == 0)
                _exit(focus_game(up[1], down[0], 2, 1));
        if ((stranger = fork()) == 0)
                _exit(focus_game(up2[1], down2[0], 2, 0));
        answer("focus the game is ready", hear(up[0]) == 'r');
        answer("focus the stranger is ready", hear(up2[0]) == 'r');

        pidfd = (int)syscall(434, game, 0); /* pidfd_open */
        answer("focus the owner names the game", pidfd >= 0 && ioctl(ring_fd, PROBE_IOCTL_FOCUS, pidfd) == 0);
        if (pidfd >= 0)
                close(pidfd);
        if (ev >= 0)
                evdev_heard(ev, &rel, &key);
        send_event(ui, EV_REL, REL_X, 5), send_event(ui, EV_REL, REL_Y, -3), send_event(ui, EV_SYN, SYN_REPORT, 0);
        send_event(ui, EV_KEY, BTN_LEFT, 1), send_event(ui, EV_SYN, SYN_REPORT, 0);
        send_event(ui, EV_REL, REL_WHEEL, 1), send_event(ui, EV_SYN, SYN_REPORT, 0);
        rel = key = -1;
        if (ev >= 0)
                evdev_heard(ev, &rel, &key);
        printf("focus take evdev rel %d key %d\n", rel, key);
        answer("focus take leaves evdev nothing", rel == 0 && key == 0);
        tell(down[1], 't');
        answer("focus the game read its page", hear(up[0]) == 't');
        tell(down2[1], 't');
        answer("focus the stranger read its page", hear(up2[0]) == 't');

        answer("focus the owner names none", ioctl(ring_fd, PROBE_IOCTL_FOCUS, -1) == 0);
        tell(down[1], 'd');
        answer("focus the game read its page after", hear(up[0]) == 'd');
        send_event(ui, EV_KEY, BTN_LEFT, 0), send_event(ui, EV_SYN, SYN_REPORT, 0);
        if (ev >= 0)
                evdev_heard(ev, &rel, &key);
        printf("focus held release evdev rel %d key %d\n", rel, key);
        answer("focus a release whose press went to a page goes nowhere else", key == 0);
        send_event(ui, EV_KEY, BTN_RIGHT, 1), send_event(ui, EV_SYN, SYN_REPORT, 0);
        send_event(ui, EV_KEY, BTN_RIGHT, 0), send_event(ui, EV_SYN, SYN_REPORT, 0);
        if (ev >= 0)
                evdev_heard(ev, &rel, &key);
        printf("focus none evdev rel %d key %d\n", rel, key);
        answer("focus named none, evdev hears the buttons", key == 2);

        pidfd = (int)syscall(434, game, 0);
        answer("focus the owner names the game again", pidfd >= 0 && ioctl(ring_fd, PROBE_IOCTL_FOCUS, pidfd) == 0);
        if (pidfd >= 0)
                close(pidfd);
        tell(down[1], 'o');
        answer("focus the game moved to observe", hear(up[0]) == 'o');
        send_event(ui, EV_REL, REL_X, 7), send_event(ui, EV_SYN, SYN_REPORT, 0);
        if (ev >= 0)
                evdev_heard(ev, &rel, &key);
        printf("focus observe evdev rel %d key %d\n", rel, key);
        answer("focus observe leaves evdev the motion", rel == 1);
        tell(down[1], 'v');
        answer("focus the game read its observed page", hear(up[0]) == 'v');

        tell(down[1], 'q'), tell(down2[1], 'q');
        waitpid(game, NULL, 0), waitpid(stranger, NULL, 0);
        ioctl(ring_fd, PROBE_IOCTL_FOCUS, -1);
        ioctl(ui, UI_DEV_DESTROY);
        close(ui);
        munmap(ring, 4096);
        close(ring_fd);
        printf("focus probe %s\n", failures ? "failed" : "passed");
        return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
        int ui;
        struct uinput_user_dev device;
        struct stat node;
        int js = -1;
        int render = -1;
        char path[32];
        int as_user = 0;
        int with_input = 1;

        if (argc > 1 && strcmp(argv[1], "--who") == 0) {
                gid_t list[32];
                int count = getgroups(32, list);

                printf("who uid %u gid %u groups", (unsigned)getuid(), (unsigned)getgid());
                for (int at = 0; at < count; at++)
                        printf(" %u", (unsigned)list[at]);
                printf("\n");
                {
                        int fd = open("/dev/dri/renderD128", O_RDWR);
                        printf("who render %s errno %d\n", fd >= 0 ? "opened" : "refused",
                               fd >= 0 ? 0 : errno);
                        if (fd >= 0)
                                close(fd);
                }
                return 0;
        }
        if (argc > 1 && strcmp(argv[1], "--focus") == 0)
                return focus_probe();
        if (argc > 1 && strcmp(argv[1], "--bowl-user") == 0)
                as_user = 1;
        else if (argc > 1 && strcmp(argv[1], "--bowl-user-no-input") == 0) {
                as_user = 1;
                with_input = 0;
        }
        if (as_user) {
                gid_t groups[1] = {PROBE_INPUT_GID};

                if (setgroups(with_input ? 1 : 0, with_input ? groups : NULL) != 0 ||
                    setgid(PROBE_BOWL_UID) != 0 || setuid(PROBE_BOWL_UID) != 0) {
                        printf("drop to the bowl user failed errno %d\n", errno);
                        return 1;
                }
                printf("running as uid %u gid %u\n", (unsigned)getuid(), (unsigned)getgid());
        }

        ui = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        answer("uinput open", ui >= 0);
        if (ui < 0) {
                printf("uinput errno %d\n", errno);
                return 1;
        }
        if (fstat(ui, &node) == 0)
                printf("uinput mode %o\n", (unsigned)(node.st_mode & 0777));

        answer("uinput key bits", ioctl(ui, UI_SET_EVBIT, EV_KEY) == 0 &&
                                      ioctl(ui, UI_SET_KEYBIT, BTN_JOYSTICK) == 0);
        answer("uinput axis bits", ioctl(ui, UI_SET_EVBIT, EV_ABS) == 0 &&
                                       ioctl(ui, UI_SET_ABSBIT, ABS_X) == 0 &&
                                       ioctl(ui, UI_SET_ABSBIT, ABS_Y) == 0);

        memset(&device, 0, sizeof device);
        strncpy(device.name, PROBE_NAME, UINPUT_MAX_NAME_SIZE - 1);
        device.id.bustype = BUS_VIRTUAL;
        device.id.vendor = 0x1d6b;
        device.id.product = 0x0001;
        device.id.version = 1;
        device.absmin[ABS_X] = -32768;
        device.absmax[ABS_X] = 32767;
        device.absmin[ABS_Y] = -32768;
        device.absmax[ABS_Y] = 32767;
        answer("uinput device written", write(ui, &device, sizeof device) == sizeof device);
        answer("uinput device created", ioctl(ui, UI_DEV_CREATE) == 0);

        answer("joystick node appears", wait_for(5000, joystick_named, NULL));

        for (int number = 0; number < 8 && js < 0; number++) {
                snprintf(path, sizeof path, "/dev/input/js%d", number);
                int fd = open(path, O_RDONLY | O_NONBLOCK);

                if (fd < 0)
                        continue;
                {
                        char name[128] = "";

                        if (ioctl(fd, JSIOCGNAME(sizeof name), name) >= 0 &&
                            strcmp(name, PROBE_NAME) == 0)
                                js = fd;
                        else
                                close(fd);
                }
        }
        answer("joystick node opened", js >= 0);
        if (js >= 0 && fstat(js, &node) == 0)
                printf("joystick mode %o\n", (unsigned)(node.st_mode & 0777));
        if (js >= 0) {
                answer("button press sent",
                       send_event(ui, EV_KEY, BTN_JOYSTICK, 1) &&
                       send_event(ui, EV_SYN, SYN_REPORT, 0));
                answer("button press read back", read_press(js));
                close(js);
        }

        ioctl(ui, UI_DEV_DESTROY);
        close(ui);

        if (as_user) {
                printf("render node skipped as the bowl user\n");
        } else for (int number = 128; number < 136 && render < 0; number++) {
                snprintf(path, sizeof path, "/dev/dri/renderD%d", number);
                render = open(path, O_RDWR);
        }
        if (!as_user) answer("render node opened", render >= 0);
        if (render >= 0) {
                struct drm_syncobj_create create;
                struct drm_syncobj_destroy destroy;

                memset(&create, 0, sizeof create);
                answer("syncobj created", ioctl(render, PROBE_DRM_SYNCOBJ_CREATE, &create) == 0);
                printf("syncobj handle %u\n", create.handle);
                if (create.handle != 0) {
                        memset(&destroy, 0, sizeof destroy);
                        destroy.handle = create.handle;
                        answer("syncobj destroyed",
                               ioctl(render, PROBE_DRM_SYNCOBJ_DESTROY, &destroy) == 0);
                }
                close(render);
        }

        printf("probe %s\n", failures ? "failed" : "passed");
        return failures ? 1 : 0;
}
