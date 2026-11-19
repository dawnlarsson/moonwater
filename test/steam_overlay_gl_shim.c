/*
 * Freestanding LD_PRELOAD library for games under Steam's in-game overlay (steam-overlay-gl.so, i386 and x86_64).
 *
 * THE GAME'S STATE AROUND THE RENDERER'S DRAWS
 * Steam's overlay renderer (gameoverlayrenderer.so, preloaded into the game) draws the overlay, the FPS corner and its
 * notifications into the game's frame with fixed-function GL (glBegin quads, GL_MODULATE, unit 0) at the game's swap, and
 * leaves the game's GLSL program and the sampler object of unit 0 bound while it does. Mesa runs those quads through the
 * game's last shader, which draws them dark and samples none of the overlay's textures: Half-Life 2 (togl, program and
 * sampler bound at every swap) showed a near-black screen for Shift+Tab and no FPS corner. Here, a draw made by the renderer
 * (its glBegin, glDrawArrays or glDrawElements) first saves and unbinds the program and the unit-0 sampler, and the
 * renderer's next glPopAttrib (which ends each of its passes) binds them again, so the game finds its state as it left it.
 *
 * THE OVERLAY IN A GAME ON WAYLAND
 * The renderer draws into an OpenGL game only at its glXSwapBuffers hook and reads the hot-key only from the X events its
 * Xlib hooks see; an OpenGL game on Wayland (EGL) gives it neither, and a Vulkan game on Wayland gives Steam's Vulkan layer
 * the frames but the renderer no hot-key. So, in a game that is a Wayland client (it made a Vulkan Wayland surface, or it
 * swaps EGL buffers and never opened an X display of its own), this library gives the renderer what an X11 game would,
 * through the renderer's exported entries only (its hooks of XOpenDisplay, XCreateSimpleWindow, XSelectInput, XNextEvent,
 * XPending and glXSwapBuffers, resolved by name): a private X connection and an unmapped window of its own, which the
 * renderer tracks as the game's; for an OpenGL game, at each EGL swap, the renderer's glXSwapBuffers on that window, which
 * runs its draw into the current EGL frame (glXGetCurrentContext answers for that call; the real swap after it, on a window
 * with no GLX drawable, is an X error the bridge's own connection ignores); and a thread that hands each X event sent to the window to the renderer through its XNextEvent hook, which is
 * where the hot-key and the overlay's input are read. Canvas sends the window the person's keys and pointer while the
 * game has the keys (_MOONWATER_STEAM_OVERLAY on the window names the game's pid; _MOONWATER_STEAM_OVERLAY_OPEN says
 * whether the overlay is open, so all input goes to it then and none to the game). Anything not found leaves the game as it
 * was, and one line on standard error says what was missing. The bridge's X server going (the session ends Xwayland when
 * Steam ends) turns the bridge off and leaves the game running: the display has an exit handler of its own (libX11 1.7's
 * XSetIOErrorExitHandler, required), and the pump stops at the hang-up. A Vulkan game's bridge window is its swapchain's size.
 *
 * SDL loads libEGL and libvulkan with dlopen and takes their functions with dlsym, so this library exports dlsym: a lookup
 * by handle is answered by the next dlsym (the renderer's own hook), with wrappers for libEGL's eglSwapBuffers and libvulkan's
 * vkGetInstanceProcAddr (a Vulkan layer's own is left alone); RTLD_NEXT is a jump to the next dlsym with the caller's own
 * return address, as glibc needs.
 *
 * Built into the byte arrays in src/bowl.c (i386 and x86_64) with:
 *   clang -target {i386,x86_64}-linux-gnu -Os -fPIC -shared -nostdlib -ffreestanding -fno-stack-protector \
 *         -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident -fuse-ld=lld \
 *         -Wl,-z,now,-z,undefs,--allow-shlib-undefined,-Bsymbolic,--hash-style=both,--build-id=none,-s
 */

typedef unsigned int GLenum;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef unsigned long XID;

#if defined(__x86_64__)
typedef struct
{
	unsigned int p_type, p_flags;
	unsigned long p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} Phdr;
#else
typedef struct
{
	unsigned int p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
} Phdr;
#endif

struct dl_phdr_info
{
	unsigned long dlpi_addr;
	const char *dlpi_name;
	const Phdr *dlpi_phdr;
	unsigned short dlpi_phnum;
};

extern void *dlvsym(void *, const char *, const char *);
extern void *dlopen(const char *, int);
extern int dl_iterate_phdr(int (*)(struct dl_phdr_info *, unsigned long, void *), void *);
extern long write(int, const void *, unsigned long);
extern int getpid(void);

#define RTLD_DEFAULT ((void *)0)
#define RTLD_NEXT ((void *)-1l)
#define RTLD_NOW_GLOBAL 0x102
#define PT_LOAD 1
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_TEXTURE0 0x84C0
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_SAMPLER_BINDING 0x8919
#define EGL_HEIGHT 0x3056
#define EGL_WIDTH 0x3057

#define HIDDEN __attribute__((visibility("hidden")))

/* ---- dlsym ---- */

HIDDEN void *(*next_dlsym)(void *, const char *);
static void *(*libc_dlsym)(void *, const char *);
HIDDEN void *overlay_dlsym(void *, const char *);
HIDDEN void overlay_resolve(void);
HIDDEN void *overlay_none(void) { return 0; }

#if defined(__x86_64__)
__asm__(".globl dlsym\n.type dlsym,@function\ndlsym:\n"
	"  cmpq $-1, %rdi\n  jne 1f\n"
	"  movq next_dlsym(%rip), %rax\n  testq %rax, %rax\n  jne 2f\n"
	"  pushq %rdi\n  pushq %rsi\n  subq $8, %rsp\n  call overlay_resolve\n  addq $8, %rsp\n  popq %rsi\n  popq %rdi\n"
	"  movq next_dlsym(%rip), %rax\n  testq %rax, %rax\n  je overlay_none\n"
	"2: jmp *%rax\n"
	"1: jmp overlay_dlsym\n");
#else
__asm__(".globl dlsym\n.type dlsym,@function\ndlsym:\n"
	"  cmpl $-1, 4(%esp)\n  jne 1f\n"
	"  call 3f\n3: popl %eax\n  addl $_GLOBAL_OFFSET_TABLE_+(.-3b), %eax\n"
	"  cmpl $0, next_dlsym@GOTOFF(%eax)\n  jne 2f\n"
	"  call overlay_resolve\n"
	"  call 4f\n4: popl %eax\n  addl $_GLOBAL_OFFSET_TABLE_+(.-4b), %eax\n"
	"  cmpl $0, next_dlsym@GOTOFF(%eax)\n  je overlay_none\n"
	"2: jmp *next_dlsym@GOTOFF(%eax)\n"
	"1: jmp overlay_dlsym\n");
#endif

// glibc's own dlsym (by version, so no interposer answers), and the next dlsym after this library (the renderer's hook).
HIDDEN void overlay_resolve(void)
{
	static const char *const versions[] = {"GLIBC_2.34", "GLIBC_2.2.5", "GLIBC_2.0"};

	if (next_dlsym)
		return;
	for (unsigned at = 0; !libc_dlsym && at < 3; at++)
		libc_dlsym = (void *(*)(void *, const char *))dlvsym(RTLD_NEXT, "dlsym", versions[at]);
	if (libc_dlsym)
		next_dlsym = (void *(*)(void *, const char *))libc_dlsym(RTLD_NEXT, "dlsym");
}

// What comes after this library: the renderer's hook where it has one, the library's own function where not.
static void *after_us(const char *name)
{
	overlay_resolve();
	return libc_dlsym ? libc_dlsym(RTLD_NEXT, name) : 0;
}

static int same(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static void say(const char *what, const char *name)
{
	static const char head[] = "steam-overlay-gl: the overlay on Wayland is off: ";
	const char *s = what;
	unsigned n = 0;

	write(2, head, sizeof(head) - 1);
	while (s[n])
		n++;
	write(2, what, n);
	for (n = 0; name && name[n]; n++)
		;
	if (name)
		write(2, name, n);
	write(2, "\n", 1);
}

/* ---- Steam's renderer: where it is, and the state around its draws ---- */

static unsigned long renderer_low, renderer_high;
static int renderer_looked;
static GLint saved_program = -1, saved_sampler;

static void (*real_get_integer)(GLenum, GLint *);
static void (*real_use_program)(GLuint);
static void (*real_bind_sampler)(GLuint, GLuint);
static void (*real_active_texture)(GLenum);

static int named_renderer(const char *name)
{
	static const char want[] = "gameoverlayrenderer";

	for (; name && *name; name++)
	{
		int at = 0;

		while (want[at] && name[at] == want[at])
			at++;
		if (!want[at])
			return 1;
	}
	return 0;
}

// The span of the renderer's loaded segments, its executable ones and its data: a module of another name is not looked at.
static int find_renderer(struct dl_phdr_info *info, unsigned long size, void *unused)
{
	unsigned long low = ~0ul, high = 0;

	(void)size;
	(void)unused;
	if (!named_renderer(info->dlpi_name))
		return 0;
	for (unsigned short at = 0; at < info->dlpi_phnum; at++)
		if (info->dlpi_phdr[at].p_type == PT_LOAD)
		{
			unsigned long start = info->dlpi_addr + info->dlpi_phdr[at].p_vaddr;
			unsigned long end = start + info->dlpi_phdr[at].p_memsz;

			low = start < low ? start : low;
			high = end > high ? end : high;
		}
	if (high <= low)
		return 0;
	renderer_low = low;
	renderer_high = high;
	return 1;
}

// Steam puts the renderer in LD_PRELOAD, so it is loaded before anything asks: it is looked for once.
static int renderer_present(void)
{
	if (!renderer_looked)
	{
		renderer_looked = 1;
		dl_iterate_phdr(find_renderer, 0);
	}
	return renderer_high != 0;
}

static int from_renderer(void *caller)
{
	unsigned long at = (unsigned long)caller;

	renderer_present();
	return at >= renderer_low && at < renderer_high;
}

static void unbind_game_state(void)
{
	GLint program = 0, sampler = 0, unit = GL_TEXTURE0;

	if (saved_program >= 0)
		return;
	if (!real_get_integer)
	{
		real_get_integer = (void (*)(GLenum, GLint *))after_us("glGetIntegerv");
		real_use_program = (void (*)(GLuint))after_us("glUseProgram");
		real_bind_sampler = (void (*)(GLuint, GLuint))after_us("glBindSampler");
		real_active_texture = (void (*)(GLenum))after_us("glActiveTexture");
	}
	if (!real_get_integer)
		return;
	real_get_integer(GL_CURRENT_PROGRAM, &program);
	if (real_bind_sampler && real_active_texture)
	{
		real_get_integer(GL_ACTIVE_TEXTURE, &unit);
		if (unit != GL_TEXTURE0)
			real_active_texture(GL_TEXTURE0);
		real_get_integer(GL_SAMPLER_BINDING, &sampler);
		if (unit != GL_TEXTURE0)
			real_active_texture((GLenum)unit);
	}
	saved_program = program;
	saved_sampler = sampler;
	if (program && real_use_program)
		real_use_program(0);
	if (sampler)
		real_bind_sampler(0, 0);
}

static void bind_game_state(void)
{
	if (saved_program < 0)
		return;
	if (saved_program && real_use_program)
		real_use_program((GLuint)saved_program);
	if (saved_sampler && real_bind_sampler)
		real_bind_sampler(0, (GLuint)saved_sampler);
	saved_program = -1;
	saved_sampler = 0;
}

typedef void (*begin_fn)(GLenum);
typedef void (*draw_arrays_fn)(GLenum, GLint, GLsizei);
typedef void (*draw_elements_fn)(GLenum, GLsizei, GLenum, const void *);
typedef void (*pop_attrib_fn)(void);

#define NEXT(type, name)                                                                                                  \
	static type real_##name;                                                                                          \
	if (!real_##name)                                                                                                 \
		real_##name = (type)after_us(#name);                                                                      \
	if (!real_##name)                                                                                                 \
		return

void glBegin(GLenum mode)
{
	NEXT(begin_fn, glBegin);
	if (from_renderer(__builtin_return_address(0)))
		unbind_game_state();
	real_glBegin(mode);
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	NEXT(draw_arrays_fn, glDrawArrays);
	if (from_renderer(__builtin_return_address(0)))
		unbind_game_state();
	real_glDrawArrays(mode, first, count);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
	NEXT(draw_elements_fn, glDrawElements);
	if (from_renderer(__builtin_return_address(0)))
		unbind_game_state();
	real_glDrawElements(mode, count, type, indices);
}

void glPopAttrib(void)
{
	NEXT(pop_attrib_fn, glPopAttrib);
	real_glPopAttrib();
	if (saved_program >= 0 && from_renderer(__builtin_return_address(0)))
		bind_game_state();
}

/* ---- the bridge: a private X window the renderer takes for the game's ---- */

typedef struct
{
	int type;
	unsigned long serial;
	int send_event;
	void *display;
	XID window;
} XAnyEvent;
typedef union
{
	int type;
	XAnyEvent any;
	long pad[24];
} XEvent;

struct pollfd_
{
	int fd;
	short events, revents;
};

static int game_x_displays;   // XOpenDisplay calls made by the game itself: one says it is an X11 game
static int bridge_state;      // 0 not tried, -2 being made (one thread makes it), 1 up, -1 off (never made, or its X server went)
static int bridge_gl;         // the game draws with EGL, and the renderer's draw runs at its swaps
static void *bridge_display;
static XID bridge_window;
static int bridge_width = 1, bridge_height = 1, bridge_open = -1;
static long bridge_context[64];
static int fake_context;
static long bridge_beat_at;  // when the open overlay's state was last told again (CLOCK_MONOTONIC milliseconds): once a second

static void *(*x_open)(const char *);
static XID (*x_create)(void *, XID, int, int, unsigned, unsigned, unsigned, unsigned long, unsigned long);
static int (*x_select)(void *, XID, long);
static int (*x_pending)(void *);
static int (*x_next)(void *, XEvent *);
static int (*x_flush)(void *);
static XID (*x_root)(void *, int);
static int (*x_screen)(void *);
static int (*x_resize)(void *, XID, unsigned, unsigned);
static int (*x_screen_width)(void *, int);
static int (*x_screen_height)(void *, int);
static unsigned long (*x_atom)(void *, const char *, int);
static int (*x_property)(void *, XID, unsigned long, unsigned long, int, int, const void *, int);
static int (*x_threads)(void);
static int (*(*x_error)(int (*)(void *, void *)))(void *, void *);
static int (*x_error_before)(void *, void *);
static void (*x_io_exit)(void *, void (*)(void *, void *), void *);
static void (*r_glx_swap)(void *, XID);
static int (*r_overlay_open)(void);
static int (*thread_make)(unsigned long *, const void *, void *(*)(void *), void *);
static int (*x_fd)(void *);
static int (*poll_fn)(struct pollfd_ *, unsigned long, int);
static int (*clock_fn)(int, long *);

#define FIND(variable, name, required)                                                                                    \
	do                                                                                                                \
	{                                                                                                                 \
		*(void **)&variable = after_us(name);                                                                     \
		if (!variable && required)                                                                                \
		{                                                                                                         \
			say("not found: ", name);                                                                         \
			return 0;                                                                                         \
		}                                                                                                         \
	} while (0)

static int bridge_up_now(void)
{
	return __atomic_load_n(&bridge_state, __ATOMIC_ACQUIRE) > 0;
}

// The X error handler is the process's: an error on the bridge's own connection (the renderer's real swap on a window with no GLX
// drawable, each EGL frame) is ignored, and one on any other display goes to the handler that was there before, the game's or Xlib's.
static int ignore_x_error(void *display, void *event)
{
	if (display && display == bridge_display)
		return 0;
	return x_error_before ? x_error_before(display, event) : 0;
}

// The bridge's X server went (the session ended Xwayland, or it died): Xlib calls this, the display's own, where it would have ended
// the process (its default exit handler is exit(1)). The bridge is off from now, the game goes on, and the pump ends.
static void bridge_lost(void *display, void *unused)
{
	(void)display;
	(void)unused;
	__atomic_store_n(&bridge_state, -1, __ATOMIC_RELEASE);
}

// The overlay's state, told to Canvas on the window when it changes, and again every second while it is open (beat): Canvas takes an
// open overlay it has not heard of for longer than that as closed, so a game whose bridge stops gives the person the keys back.
static void tell_open(int beat)
{
	int open = r_overlay_open ? r_overlay_open() != 0 : 0;
	long value = open;

	if (!bridge_up_now() || !x_atom || !x_property || (open == bridge_open && !(open && beat)))
		return;
	bridge_open = open;
	x_property(bridge_display, bridge_window, x_atom(bridge_display, "_MOONWATER_STEAM_OVERLAY_OPEN", 0), 6, 32, 0, &value, 1);
	x_flush(bridge_display);
}

// Each X event Canvas sends the window goes to the renderer through its XPending and XNextEvent hooks, which read the hot-key and
// the input. The thread waits on the connection itself (poll), never inside the renderer's hooks: the renderer holds its own lock
// in them, and a thread blocked there kept its draw and its requests to Steam from running (no overlay opened, nothing drawn).
// The server hung up (POLLHUP, POLLERR, POLLNVAL), or Xlib found it gone (bridge_lost): the bridge is off and the thread ends.
static void *bridge_pump(void *unused)
{
	(void)unused;
	while (bridge_up_now())
	{
		struct pollfd_ wanted = {x_fd(bridge_display), 1, 0};

		if (poll_fn(&wanted, 1, 250) > 0 && (wanted.revents & (0x8 | 0x10 | 0x20)))
		{
			__atomic_store_n(&bridge_state, -1, __ATOMIC_RELEASE);
			break;
		}
		while (bridge_up_now() && x_pending(bridge_display) > 0)
		{
			XEvent event;

			x_next(bridge_display, &event);
		}
		{
			long now[2] = {0, 0}, at;

			clock_fn(1, now); // CLOCK_MONOTONIC: {seconds, nanoseconds}, each a long (time_t and long on i386 and x86_64)
			at = now[0] * 1000 + now[1] / 1000000;
			tell_open(at - bridge_beat_at >= 1000);
			if (at - bridge_beat_at >= 1000)
				bridge_beat_at = at;
		}
	}
	return 0;
}

static int bridge_up(int gl)
{
	void *display;
	long pid = getpid();
	int screen;

	if (!dlopen("libX11.so.6", RTLD_NOW_GLOBAL) || (gl && !dlopen("libGL.so.1", RTLD_NOW_GLOBAL)))
	{
		say("cannot load ", gl ? "libX11.so.6 or libGL.so.1" : "libX11.so.6");
		return 0;
	}
	FIND(x_threads, "XInitThreads", 1);
	FIND(x_open, "XOpenDisplay", 1);
	FIND(x_create, "XCreateSimpleWindow", 1);
	FIND(x_select, "XSelectInput", 1);
	FIND(x_pending, "XPending", 1);
	FIND(x_next, "XNextEvent", 1);
	FIND(x_flush, "XFlush", 1);
	FIND(x_root, "XRootWindow", 1);
	FIND(x_screen, "XDefaultScreen", 1);
	FIND(x_resize, "XResizeWindow", 1);
	FIND(x_screen_width, "XDisplayWidth", 1);
	FIND(x_screen_height, "XDisplayHeight", 1);
	FIND(x_atom, "XInternAtom", 1);
	FIND(x_property, "XChangeProperty", 1);
	FIND(x_error, "XSetErrorHandler", 1);
	// libX11 1.7 and newer: without it, the X server going would end the game (Xlib's exit(1)), so there is no bridge.
	FIND(x_io_exit, "XSetIOErrorExitHandler", 1);
	FIND(r_overlay_open, "IsOverlayEnabled", 1);
	FIND(thread_make, "pthread_create", 1);
	FIND(x_fd, "XConnectionNumber", 1);
	FIND(poll_fn, "poll", 1);
	FIND(clock_fn, "clock_gettime", 1);
	if (gl)
		FIND(r_glx_swap, "glXSwapBuffers", 1);
	x_threads();
	display = x_open(0);
	if (!display)
	{
		say("no X display (DISPLAY)", 0);
		return 0;
	}
	x_io_exit(display, bridge_lost, 0);
	bridge_display = display;
	x_error_before = x_error(ignore_x_error);
	if (x_error_before == ignore_x_error)
		x_error_before = 0;
	screen = x_screen(display);
	bridge_window = x_create(display, x_root(display, screen), 0, 0, (unsigned)bridge_width, (unsigned)bridge_height, 0, 0, 0);
	if (!bridge_window)
	{
		say("no window", 0);
		return 0;
	}
	// KeyPress, KeyRelease, ButtonPress, ButtonRelease, PointerMotion, FocusChange: what Canvas sends and the renderer reads.
	x_select(display, bridge_window, 1 | 2 | 4 | 8 | 64 | (1l << 21));
	x_property(display, bridge_window, x_atom(display, "_MOONWATER_STEAM_OVERLAY", 0), 6, 32, 0, &pid, 1);
	// Made 1x1 and sized now, the screen's size (an OpenGL game's surface's at its first swap): the renderer learns a window's size
	// from its ConfigureNotify, and draws nothing, the FPS corner neither, for a window whose size it does not know.
	bridge_width = x_screen_width(display, screen);
	bridge_height = x_screen_height(display, screen);
	x_resize(display, bridge_window, (unsigned)bridge_width, (unsigned)bridge_height);
	x_flush(display);
	bridge_gl = gl;
	return 1;
}

// One thread makes the bridge (0 to -2 by compare and swap); one that comes to it meanwhile goes on without it this time.
static int bridge(int gl)
{
	int expected = 0;

	if (__atomic_load_n(&bridge_state, __ATOMIC_ACQUIRE) == 0 &&
	    __atomic_compare_exchange_n(&bridge_state, &expected, -2, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
	{
		unsigned long thread;
		int up = renderer_present() && !game_x_displays && bridge_up(gl);

		__atomic_store_n(&bridge_state, up ? 1 : -1, __ATOMIC_RELEASE);
		if (up)
		{
			tell_open(0);
			if (thread_make(&thread, 0, bridge_pump, 0) != 0)
				say("no thread", 0);
		}
	}
	return bridge_up_now();
}

// The game's own X displays: a game that opens one draws through X, where the renderer works by itself.
void *XOpenDisplay(const char *name)
{
	static void *(*real)(const char *);

	if (!real)
		real = (void *(*)(const char *))after_us("XOpenDisplay");
	if (!from_renderer(__builtin_return_address(0)))
		game_x_displays++;
	return real ? real(name) : 0;
}

// glXGetCurrentContext answers for the renderer's draw at an EGL swap, which asks it before drawing.
void *glXGetCurrentContext(void)
{
	static void *(*real)(void);

	if (fake_context && from_renderer(__builtin_return_address(0)))
		return bridge_context;
	if (!real)
		real = (void *(*)(void))after_us("glXGetCurrentContext");
	return real ? real() : 0;
}

/* ---- EGL and Vulkan, as SDL takes them ---- */

static unsigned (*real_egl_swap)(void *, void *);
static unsigned (*real_egl_query)(void *, void *, int, int *);

unsigned eglSwapBuffers(void *display, void *surface)
{
	if (!real_egl_swap)
		real_egl_swap = (unsigned (*)(void *, void *))after_us("eglSwapBuffers");
	if (!real_egl_swap)
		return 0;
	if (bridge(1) && bridge_gl)
	{
		int width = 0, height = 0;

		if (!real_egl_query)
			real_egl_query = (unsigned (*)(void *, void *, int, int *))after_us("eglQuerySurface");
		if (real_egl_query && real_egl_query(display, surface, EGL_WIDTH, &width) && real_egl_query(display, surface, EGL_HEIGHT, &height) &&
		    width > 0 && height > 0 && (width != bridge_width || height != bridge_height))
		{
			bridge_width = width;
			bridge_height = height;
			x_resize(bridge_display, bridge_window, (unsigned)width, (unsigned)height);
		}
		// The renderer draws for the window it tracks; its own glXSwapBuffers after the draw, on a window GLX has no drawable
		// for, is an X error the bridge's connection ignores.
		fake_context = 1;
		r_glx_swap(bridge_display, bridge_window);
		fake_context = 0;
		tell_open(0);
	}
	return real_egl_swap(display, surface);
}

static int (*real_vk_wayland_surface)(void *, const void *, const void *, void *);
static void *(*real_vk_instance_proc)(void *, const char *);
static void *(*real_vk_device_proc)(void *, const char *);
static int (*real_vk_swapchain)(void *, const void *, const void *, void *);

// VkSwapchainCreateInfoKHR up to its imageExtent, laid out by the compiler as Vulkan's header is (the surface a 64-bit handle).
struct vk_swapchain_info
{
	int type;
	const void *next;
	unsigned flags;
	unsigned long long surface;
	unsigned images, format, space, width, height;
};

static int overlay_vk_wayland_surface(void *instance, const void *info, const void *allocator, void *surface)
{
	bridge(0);
	return real_vk_wayland_surface(instance, info, allocator, surface);
}

// A Vulkan game's swapchain: the bridge's window is made its size, the size Steam's layer draws the overlay at, so the renderer
// lays the overlay out and reads the pointer (which Canvas scales to the window) in the frame's own pixels.
static int overlay_vk_swapchain(void *device, const void *info, const void *allocator, void *swapchain)
{
	const struct vk_swapchain_info *wanted = (const struct vk_swapchain_info *)info;

	if (wanted && bridge_up_now() && !bridge_gl && wanted->width && wanted->height && wanted->width < 65536 &&
	    wanted->height < 65536 && ((int)wanted->width != bridge_width || (int)wanted->height != bridge_height))
	{
		bridge_width = (int)wanted->width;
		bridge_height = (int)wanted->height;
		x_resize(bridge_display, bridge_window, wanted->width, wanted->height);
		x_flush(bridge_display);
	}
	return real_vk_swapchain(device, info, allocator, swapchain);
}

static void *overlay_vk_swapchain_of(void *found)
{
	if (!found)
		return 0;
	real_vk_swapchain = (int (*)(void *, const void *, const void *, void *))found;
	return (void *)overlay_vk_swapchain;
}

static void *overlay_vk_device_proc(void *device, const char *name)
{
	void *found = real_vk_device_proc ? real_vk_device_proc(device, name) : 0;

	return found && name && same(name, "vkCreateSwapchainKHR") ? overlay_vk_swapchain_of(found) : found;
}

// Handed to SDL in place of libvulkan's (overlay_dlsym), never exported: a layer's own vkGetInstanceProcAddr stays its own.
HIDDEN void *overlay_vk_instance_proc(void *instance, const char *name)
{
	void *found;

	if (!real_vk_instance_proc)
		return 0;
	found = real_vk_instance_proc(instance, name);
	if (!found || !name || !renderer_present())
		return found;
	if (same(name, "vkCreateWaylandSurfaceKHR"))
	{
		real_vk_wayland_surface = (int (*)(void *, const void *, const void *, void *))found;
		return (void *)overlay_vk_wayland_surface;
	}
	if (same(name, "vkGetDeviceProcAddr"))
	{
		real_vk_device_proc = (void *(*)(void *, const char *))found;
		return (void *)overlay_vk_device_proc;
	}
	if (same(name, "vkCreateSwapchainKHR"))
		return overlay_vk_swapchain_of(found);
	return found;
}

// Whether address is in a loaded module whose name has want in it (libEGL, libvulkan): the loader's own function, not a layer's.
static const char *module_want;
static unsigned long module_address;
static int module_found;

static int module_has(struct dl_phdr_info *info, unsigned long size, void *unused)
{
	(void)size;
	(void)unused;
	for (unsigned short at = 0; at < info->dlpi_phnum; at++)
		if (info->dlpi_phdr[at].p_type == PT_LOAD && module_address >= info->dlpi_addr + info->dlpi_phdr[at].p_vaddr &&
		    module_address < info->dlpi_addr + info->dlpi_phdr[at].p_vaddr + info->dlpi_phdr[at].p_memsz)
		{
			const char *name = info->dlpi_name;

			for (; name && *name; name++)
			{
				int at2 = 0;

				while (module_want[at2] && name[at2] == module_want[at2])
					at2++;
				if (!module_want[at2])
				{
					module_found = 1;
					break;
				}
			}
			return 1;
		}
	return 0;
}

static int in_module(void *address, const char *want)
{
	module_want = want;
	module_address = (unsigned long)address;
	module_found = 0;
	dl_iterate_phdr(module_has, 0);
	return module_found;
}

// A lookup by handle: the next dlsym answers, and SDL's eglSwapBuffers (libEGL's) and vkGetInstanceProcAddr (libvulkan's, the
// loader's: a layer's own is left as it is) are this library's.
HIDDEN void *overlay_dlsym(void *handle, const char *name)
{
	void *found;

	overlay_resolve();
	found = next_dlsym ? next_dlsym(handle, name) : 0;
	if (!found || !name || !renderer_present())
		return found;
	if (same(name, "eglSwapBuffers") && in_module(found, "libEGL"))
	{
		real_egl_swap = (unsigned (*)(void *, void *))found;
		return (void *)eglSwapBuffers;
	}
	if (same(name, "vkGetInstanceProcAddr") && in_module(found, "libvulkan"))
	{
		real_vk_instance_proc = (void *(*)(void *, const char *))found;
		return (void *)overlay_vk_instance_proc;
	}
	return found;
}
