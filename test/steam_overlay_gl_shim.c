/*
 * Freestanding LD_PRELOAD shim for OpenGL games under Steam's in-game overlay.
 *
 * Steam's overlay renderer (gameoverlayrenderer.so, preloaded into the game) draws the overlay, the FPS corner and its
 * notifications into the game's frame with fixed-function GL (glBegin quads, GL_MODULATE, unit 0) at the game's swap, and
 * leaves the game's GLSL program and the sampler object of unit 0 bound while it does. Mesa runs those quads through the
 * game's last shader, which draws them dark and samples none of the overlay's textures: Half-Life 2 (togl, program and
 * sampler bound at every swap) showed a near-black screen for Shift+Tab and no FPS corner, though the textures it uploaded
 * held the overlay's pixels. Here, a draw made by the renderer (its glBegin, glDrawArrays or glDrawElements) first saves
 * and unbinds the program and the unit-0 sampler, and the renderer's next glPopAttrib (which ends each of its passes)
 * binds them again, so the game finds its state as it left it. Calls made by anything else go straight to the driver.
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

extern void *dlsym(void *, const char *);
extern int dl_iterate_phdr(int (*)(struct dl_phdr_info *, unsigned long, void *), void *);

#define RTLD_NEXT ((void *)-1l)
#define PT_LOAD 1
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_TEXTURE0 0x84C0
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_SAMPLER_BINDING 0x8919

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

// True when the call came from Steam's overlay renderer. Steam puts the renderer in LD_PRELOAD, so it is loaded before the
// first draw: it is looked for once, then, and a process without it (Xwayland, the webhelper) pays one compare a call.
static int from_renderer(void *caller)
{
	unsigned long at = (unsigned long)caller;

	if (!renderer_looked)
	{
		renderer_looked = 1;
		dl_iterate_phdr(find_renderer, 0);
	}
	return at >= renderer_low && at < renderer_high;
}

static void unbind_game_state(void)
{
	GLint program = 0, sampler = 0, unit = GL_TEXTURE0;

	if (saved_program >= 0)
		return;
	if (!real_get_integer)
	{
		real_get_integer = (void (*)(GLenum, GLint *))dlsym(RTLD_NEXT, "glGetIntegerv");
		real_use_program = (void (*)(GLuint))dlsym(RTLD_NEXT, "glUseProgram");
		real_bind_sampler = (void (*)(GLuint, GLuint))dlsym(RTLD_NEXT, "glBindSampler");
		real_active_texture = (void (*)(GLenum))dlsym(RTLD_NEXT, "glActiveTexture");
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
		real_##name = (type)dlsym(RTLD_NEXT, #name);                                                              \
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
