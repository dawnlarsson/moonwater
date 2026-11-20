/*
        The window gauntlet's GLFW client (and raylib's: raylib is GLFW underneath). One run on one platform:

            glfw wayland|x11

        Steps, each held to a deadline: a window made and shown, frames presented (glfwSwapBuffers must not block forever on a
        frame callback that never comes); a size asked by the program is the size it gets; fullscreen fills the screen and gives
        back the size it had; iconified and restored; the cursor disabled (relative motion, pointer constraints) and given back;
        the clipboard set and read back; a second window; everything destroyed and glfwTerminate returns.
*/
#include <GLFW/glfw3.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *step = "start";

static void watchdog(int sig)
{
        (void)sig;
        char m[200];
        int n = snprintf(m, sizeof m, "FAIL hung at: %s\n", step);
        write(2, m, (size_t)n);
        _exit(1);
}

static void fail(const char *why)
{
        const char *desc = NULL;
        glfwGetError(&desc);
        fprintf(stderr, "FAIL %s (at %s)%s%s\n", why, step, desc ? ": " : "", desc ? desc : "");
        exit(1);
}

static double now(void)
{
        return glfwGetTime();
}

// Frames drawn and presented for seconds, events pumped.
static int frames(GLFWwindow *w, double seconds)
{
        int n = 0;
        double end = now() + seconds;
        while (now() < end)
        {
                glfwMakeContextCurrent(w);
                glClearColor((float)(n & 1), 0.2f, 0.4f, 1);
                glClear(GL_COLOR_BUFFER_BIT);
                glfwSwapBuffers(w);
                glfwPollEvents();
                n++;
        }
        return n;
}

// Events pumped until the window's size is w by h, or seconds pass; 1 when it is.
static int size_is(GLFWwindow *win, int w, int h, double seconds)
{
        double end = now() + seconds;
        int cw = 0, ch = 0;
        while (now() < end)
        {
                frames(win, 0.05);
                glfwGetWindowSize(win, &cw, &ch);
                if (cw == w && ch == h)
                        return 1;
        }
        fprintf(stderr, "  size %dx%d, wanted %dx%d\n", cw, ch, w, h);
        return 0;
}

int main(int argc, char **argv)
{
        int platform = argc > 1 && !strcmp(argv[1], "x11") ? GLFW_PLATFORM_X11 : GLFW_PLATFORM_WAYLAND;

        signal(SIGALRM, watchdog);
        alarm(60);
        glfwInitHint(GLFW_PLATFORM, platform);
        step = "glfwInit";
        if (!glfwInit())
                fail("glfwInit");
        printf("GLFW %s, platform %s\n", glfwGetVersionString(), glfwGetPlatform() == GLFW_PLATFORM_WAYLAND ? "wayland" : "x11");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        step = "create";
        GLFWwindow *w = glfwCreateWindow(640, 480, "gauntlet glfw", NULL, NULL);
        if (!w)
                fail("glfwCreateWindow");
        glfwSwapInterval(1);
        step = "frames";
        int n = frames(w, 1.0);
        if (n < 10)
        {
                fprintf(stderr, "FAIL %d frames presented in a second (a swap waits on a frame that never comes)\n", n);
                return 1;
        }
        step = "resize by the program";
        glfwSetWindowSize(w, 500, 400);
        if (!size_is(w, 500, 400, 3))
                fail("the size asked is not the size had");
        int fbw, fbh;
        glfwGetFramebufferSize(w, &fbw, &fbh);
        if (fbw <= 0 || fbh <= 0)
                fail("framebuffer of nothing");
        step = "fullscreen";
        GLFWmonitor *mon = glfwGetPrimaryMonitor();
        if (!mon)
                fail("no monitor");
        const GLFWvidmode *mode = glfwGetVideoMode(mon);
        glfwSetWindowMonitor(w, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
        if (!size_is(w, mode->width, mode->height, 3))
                fail("fullscreen is not the screen's size");
        step = "back from fullscreen";
        glfwSetWindowMonitor(w, NULL, 50, 50, 500, 400, 0);
        if (!size_is(w, 500, 400, 3))
                fail("back from fullscreen, the size is not the one it had");
        step = "iconify";
        glfwIconifyWindow(w);
        frames(w, 0.3);
        glfwRestoreWindow(w);
        frames(w, 0.3);
        if (frames(w, 0.5) < 5)
                fail("restored, it presents no frames");
        step = "cursor disabled";
        glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported())
                glfwSetInputMode(w, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        frames(w, 0.3);
        glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        frames(w, 0.2);
        step = "clipboard";
        glfwSetClipboardString(w, "gauntlet-clip-1234");
        frames(w, 0.2);
        const char *clip = glfwGetClipboardString(w);
        if (!clip || strcmp(clip, "gauntlet-clip-1234"))
                fprintf(stderr, "  note: the clipboard read back %s\n", clip ? clip : "(nothing)");
        step = "second window";
        GLFWwindow *w2 = glfwCreateWindow(320, 240, "gauntlet glfw 2", NULL, w);
        if (!w2)
                fail("second window");
        if (frames(w2, 0.5) < 5 || frames(w, 0.3) < 3)
                fail("two windows: frames stall");
        step = "destroy";
        glfwDestroyWindow(w2);
        glfwDestroyWindow(w);
        alarm(10);
        step = "glfwTerminate";
        glfwTerminate();
        printf("PASS glfw %s\n", argc > 1 ? argv[1] : "wayland");
        return 0;
}
