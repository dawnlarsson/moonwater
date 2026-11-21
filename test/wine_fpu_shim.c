/*
 * Freestanding LD_PRELOAD library for 32-bit Wine/Proton games whose x87 FPU
 * control word leaks into native Linux GStreamer decoder threads (wine-fpu.so,
 * i386 and x86_64).
 *
 * WHY THIS EXISTS
 * Many 32-bit Windows games (especially 1998-2012 Direct3D 7/8/9 titles such as
 * RollerCoaster Tycoon 3) set the x87 FPU control word to 0x0c7f (24-bit single
 * precision + round-toward-zero RC=11 for MSVC fast ftol) and MXCSR to 0x7fb7.
 * When Wine's winegstreamer spawns native Linux 32-bit GStreamer streaming
 * threads via pthread_create()/clone(), Linux fpu_clone inherits the caller's
 * x87 FCW/MXCSR (unlike Windows CreateThread, which initializes 0x027f).
 * Native 32-bit libvorbis (and other SysV i386 decoders) rely on round-to-nearest
 * (FCW=0x037f); RC=11 round-toward-zero turns music into harsh robotic/metallic
 * audio before it ever leaves vorbisdec.
 *
 * WHAT THIS DOES
 * On i386, resets x87 FCW to standard Linux SysV (0x037f) and clears SSE
 * rounding bits in MXCSR (round-to-nearest) on GStreamer streaming entry points
 * (gst_pad_push, gst_audio_converter_samples) and libvorbis synthesis entry
 * points (vorbis_synthesis_init, vorbis_synthesis, vorbis_synthesis_blockin,
 * vorbis_synthesis_pcmout). These only run on native Linux GStreamer threads,
 * never on Win32 game physics/render threads. On x86_64, the library is a tiny
 * no-op stub so LD_PRELOAD=/usr/local/$LIB/wine-fpu.so loads silently for both
 * 64-bit and 32-bit processes without ld.so ELF-class warnings.
 *
 * Built into the byte arrays in src/bowl.c (i386 and x86_64) with:
 *   clang -target {i386,x86_64}-linux-gnu -Os -fPIC -shared -nostdlib -ffreestanding \
 *         -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables \
 *         -fno-ident -fuse-ld=lld \
 *         -Wl,-z,now,-z,undefs,--allow-shlib-undefined,-Bsymbolic,--hash-style=both,--build-id=none,-s
 */

#if defined(__i386__)

extern void *dlsym(void *, const char *);
extern void *dlopen(const char *, int);

#define RTLD_NEXT ((void *)-1l)
#define RTLD_LAZY_NOLOAD 0x00005 /* RTLD_LAZY (1) | RTLD_NOLOAD (4) */

static void *resolve(const char *lib, const char *name)
{
	void *fn = dlsym(RTLD_NEXT, name);

	if (!fn)
	{
		void *h = dlopen(lib, RTLD_LAZY_NOLOAD);

		if (h)
			fn = dlsym(h, name);
	}
	return fn;
}

static inline void fpu_sysv_reset(void)
{
	unsigned short cw = 0;
	unsigned int mx = 0;

	__asm__ volatile("fnstcw %0" : "=m"(cw));
	if ((cw & 0x0f00u) != 0x0300u)
	{
		unsigned short std_cw = 0x037fu;

		__asm__ volatile("fldcw %0" : : "m"(std_cw));
	}
	__asm__ volatile("stmxcsr %0" : "=m"(mx));
	if ((mx & 0x6000u) != 0u)
	{
		unsigned int std_mx = mx & ~0x6000u;

		__asm__ volatile("ldmxcsr %0" : : "m"(std_mx));
	}
}

#define WRAP2(ret, lib, name, t1, t2)                                 \
	ret name(t1 a, t2 b)                                          \
	{                                                             \
		static ret (*real)(t1, t2);                           \
		fpu_sysv_reset();                                     \
		if (!real)                                            \
			real = (ret (*)(t1, t2))resolve(lib, #name);  \
		return real ? real(a, b) : (ret)0;                    \
	}

WRAP2(int, "libgstreamer-1.0.so.0", gst_pad_push, void *, void *)
WRAP2(int, "libvorbis.so.0", vorbis_synthesis_init, void *, void *)
WRAP2(int, "libvorbis.so.0", vorbis_synthesis, void *, void *)
WRAP2(int, "libvorbis.so.0", vorbis_synthesis_blockin, void *, void *)
WRAP2(int, "libvorbis.so.0", vorbis_synthesis_pcmout, void *, void *)

int gst_audio_converter_samples(void *c, int fl, void **in, unsigned long ni, void **out, unsigned long no)
{
	static int (*real)(void *, int, void **, unsigned long, void **, unsigned long);

	fpu_sysv_reset();
	if (!real)
		real = (int (*)(void *, int, void **, unsigned long, void **, unsigned long))
		    resolve("libgstaudio-1.0.so.0", "gst_audio_converter_samples");
	return real ? real(c, fl, in, ni, out, no) : 0;
}

#else

/* x86_64 no-op stub so /usr/local/$LIB/wine-fpu.so loads cleanly in 64-bit processes. */
int moonwater_wine_fpu_stub(void) { return 0; }

#endif
