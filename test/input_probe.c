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
#include <sys/stat.h>
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
