/*
        The stress bench's animation through GLFW (its Wayland build): an OpenGL ES 2 context by EGL, swap interval 1,
        a field and a bar that moves (a scissored clear), for SECONDS. Its frame times are the returns of glfwSwapBuffers
        (toolkit_common.h).

            glfw-anim [SECONDS] [W] [H]
*/
#define GLFW_INCLUDE_ES2
#include <GLFW/glfw3.h>

#include "toolkit_common.h"

int main(int argc, char **argv)
{
        tk_exec = tk_now();
        double seconds = argc > 1 ? atof(argv[1]) : 3;
        int w = argc > 2 ? atoi(argv[2]) : 1280, h = argc > 3 ? atoi(argv[3]) : 720;
        if (!glfwInit())
        {
                printf("NOTE GLFW cannot start here\n");
                return 3;
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        GLFWwindow *win = glfwCreateWindow(w, h, "glfw-anim", NULL, NULL);
        if (!win)
        {
                const char *why = NULL;
                glfwGetError(&why);
                printf("NOTE GLFW made no window: %s\n", why ? why : "?");
                return 3;
        }
        glfwMakeContextCurrent(win);
        glfwSwapInterval(1);
        const char *renderer = (const char *)glGetString(GL_RENDERER);
        char name[64];
        snprintf(name, sizeof(name), "%s", renderer ? renderer : "?");
        for (char *c = name; *c; c++)
                if (*c == ' ')
                        *c = '_';
        uint64_t end = tk_now() + (uint64_t)(seconds * 1e9);
        tk_warm = tk_now() + 500000000ull;
        unsigned frame = 0;
        while (tk_now() < end && !glfwWindowShouldClose(win))
        {
                glfwPollEvents();
                glDisable(GL_SCISSOR_TEST);
                glClearColor((float)((frame * 3u) & 0xff) / 255.0f, 0x30 / 255.0f, 0x60 / 255.0f, 1);
                glClear(GL_COLOR_BUFFER_BIT);
                glEnable(GL_SCISSOR_TEST);
                glScissor((int)((frame * 8u) % (unsigned)w), 0, 24, h);
                glClearColor(1, 1, 1, 1);
                glClear(GL_COLOR_BUFFER_BIT);
                glfwSwapBuffers(win);
                tk_frame();
                frame++;
        }
        tk_report("glfw", name);
        glfwTerminate();
        return 0;
}
