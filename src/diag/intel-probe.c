/*
 * intel-probe.c (v3): Intel GPU compute self-test through the i915 render node.
 *
 * Scope: Ivy Bridge (2012) and newer. Gen6 (Sandy Bridge, 2011) and older are
 * out of scope and only classified as "pre-2012".
 *
 * One file, freestanding x86-64 Linux: raw syscalls, own _start, no libc, no
 * libdrm. Build:
 *   clang -target x86_64-linux-gnu -ffreestanding -nostdlib -static -fno-builtin
 *         -fno-stack-protector -O2 -fuse-ld=lld -o intel-probe intel-probe.c
 *
 * Support matrix (kern = the command-set generation):
 *   kern  parts                          compute here
 *   7     Ivy Bridge, Valleyview         gen7 path; needs --allow-gen7
 *   75    Haswell (GT2 HD 4400, 0x041e)  gen7 path + Haswell channel selects; needs --allow-gen7
 *   8     Broadwell, Cherry View         gen8 path; needs --allow-untested
 *   9     Skylake..Comet Lake, Gemini    gen9 path; verified on 0x3e92 (Dell UHD 630)
 *   11    Ice Lake, Elkhart, Jasper      gen9 path + gen11 kernel; --allow-untested
 *   12    Tiger Lake, Rocket Lake, ADL   gen9 path + gen12 kernel; --allow-untested
 *   xe    DG1, DG2, Meteor/Arrow/Lunar   facts only (xe-era; i915 or xe driver)
 *
 * Chain (default): facts, step a (store dword), userptr store. Each needs the
 * one before it to pass.
 *   --walker            after the chain, the GPGPU fill walker, --repeat N times
 *   --b-only            the walker alone (no store), --repeat N times
 *   --facts             facts only (read-only: no context, no BO)
 *   --dump [--kern N]   batch dwords for a config; no device
 *
 * Walker knobs: --w --h --pitch --colour --rgba --kalt --mocs --control --guard
 *   --ggtt 0|1 (gen7 store), --fault and --badlen (positive controls), --repeat N
 *
 * Exit: 0 every step that ran passed; 1 a step failed; 2 not i915 / no node.
 *
 * Sources: i915 uapi from i915_drm.h (struct sizes asserted); command dwords and
 * kernels from IGT lib/gpu_cmds.c and lib/gpgpu_fill.c (gen7, gen8, gen9 path,
 * gen11 and gen12 kernel tables, verbatim); Mesa genxml gen80/gen90 for the walker
 * and state-base fields; device ids from include/drm/intel/pciids.h.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;
typedef long i64;
typedef int i32;

/* ---- freestanding runtime ------------------------------------------------ */

__attribute__((used)) void *memset(void *d, int c, u64 n)
{
	volatile u8 *p = d;

	while (n--)
		*p++ = (u8)c;
	return d;
}

__attribute__((used)) void *memcpy(void *d, const void *s, u64 n)
{
	volatile u8 *p = d;
	const volatile u8 *q = s;

	while (n--)
		*p++ = *q++;
	return d;
}

static i64 sys3(i64 n, i64 a, i64 b, i64 c)
{
	i64 r;

	__asm__ volatile("syscall"
			 : "=a"(r)
			 : "a"(n), "D"(a), "S"(b), "d"(c)
			 : "rcx", "r11", "memory");
	return r;
}

static i64 sys6(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f)
{
	register i64 r10 __asm__("r10") = d;
	register i64 r8 __asm__("r8") = e;
	register i64 r9 __asm__("r9") = f;
	i64 r;

	__asm__ volatile("syscall"
			 : "=a"(r)
			 : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
			 : "rcx", "r11", "memory");
	return r;
}

#define SYS_WRITE 1
#define SYS_MMAP 9
#define SYS_MUNMAP 11
#define SYS_IOCTL 16
#define SYS_ALARM 37
#define SYS_EXIT_GROUP 231
#define SYS_OPENAT 257
#define SYS_SYSLOG 103

__asm__(".text\n"
	".globl _start\n"
	"_start:\n"
	"  xor %ebp, %ebp\n"
	"  mov %rsp, %rdi\n"
	"  and $-16, %rsp\n"
	"  call probe_entry\n"
	"  hlt\n");

/* ---- output -------------------------------------------------------------- */

static char ob[4096];
static u32 on;

static void flush(void)
{
	u32 o = 0;

	while (o < on) {
		i64 r = sys3(SYS_WRITE, 1, (i64)(ob + o), on - o);

		if (r == -4)
			continue;
		if (r <= 0)
			break;
		o += (u32)r;
	}
	on = 0;
}

static void put(const char *s, u32 n)
{
	for (u32 i = 0; i < n; i++) {
		if (on == sizeof ob)
			flush();
		ob[on++] = s[i];
	}
}

static u32 slen(const char *s)
{
	u32 n = 0;

	while (s[n])
		n++;
	return n;
}

static void S(const char *s)
{
	put(s, slen(s));
}

static void H(u64 v)
{
	char t[16];
	u32 n = 0;

	do {
		t[n++] = "0123456789abcdef"[v & 15];
		v >>= 4;
	} while (v);
	S("0x");
	while (n)
		put(&t[--n], 1);
}

static void D(i64 v)
{
	char t[24];
	u32 n = 0;
	u64 u = v < 0 ? 0 - (u64)v : (u64)v;

	if (v < 0)
		S("-");
	do {
		t[n++] = (char)('0' + u % 10);
		u /= 10;
	} while (u);
	while (n)
		put(&t[--n], 1);
}

static void NL(void)
{
	put("\n", 1);
	flush();
}

static const char *ename(i64 e)
{
	switch (e) {
	case 1: return "EPERM";
	case 2: return "ENOENT";
	case 4: return "EINTR";
	case 5: return "EIO";
	case 6: return "ENXIO";
	case 9: return "EBADF";
	case 11: return "EAGAIN";
	case 12: return "ENOMEM";
	case 13: return "EACCES";
	case 14: return "EFAULT";
	case 16: return "EBUSY";
	case 19: return "ENODEV";
	case 22: return "EINVAL";
	case 25: return "ENOTTY";
	case 28: return "ENOSPC";
	case 38: return "ENOSYS";
	case 62: return "ETIME";
	case 95: return "EOPNOTSUPP";
	}
	return "?";
}

static void RC(i64 r)
{
	if (r >= 0) {
		S("ok");
		return;
	}
	S("errno ");
	D(-r);
	S(" ");
	S(ename(-r));
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static u64 parse_num(const char *s)
{
	u64 v = 0;
	u32 base = 10;

	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		s += 2;
	}
	for (; *s; s++) {
		u32 d;
		char c = *s;

		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			d = c - 'A' + 10;
		else
			break;
		if (d >= base)
			break;
		v = v * base + d;
	}
	return v;
}

/* ---- uapi: one definition per struct, sizes checked against i915_drm.h ---- */

#define DRM_COMMAND_BASE 0x40u
#define IOC(dir, type, nr, size) \
	((((u32)(dir)) << 30) | (((u32)(size)) << 16) | (((u32)(type)) << 8) | ((u32)(nr)))
#define IOW 1u
#define IOWR 3u

#define DRM_IOCTL_VERSION IOC(IOWR, 'd', 0x00, 64)
#define DRM_IOCTL_GEM_CLOSE IOC(IOW, 'd', 0x09, 8)
#define I915_GETPARAM IOC(IOWR, 'd', DRM_COMMAND_BASE + 0x06, 16)
#define I915_GEM_CREATE IOC(IOWR, 'd', DRM_COMMAND_BASE + 0x1b, 16)
#define I915_GEM_PREAD IOC(IOW, 'd', DRM_COMMAND_BASE + 0x1c, 32)
#define I915_GEM_PWRITE IOC(IOW, 'd', DRM_COMMAND_BASE + 0x1d, 32)
#define I915_GEM_EXECBUFFER2 IOC(IOW, 'd', DRM_COMMAND_BASE + 0x29, 64)
#define I915_GEM_WAIT IOC(IOWR, 'd', DRM_COMMAND_BASE + 0x2c, 16)
#define I915_GEM_CONTEXT_CREATE IOC(IOWR, 'd', DRM_COMMAND_BASE + 0x2d, 8)
#define I915_GEM_CONTEXT_DESTROY IOC(IOW, 'd', DRM_COMMAND_BASE + 0x2e, 8)
#define I915_GEM_USERPTR IOC(IOWR, 'd', DRM_COMMAND_BASE + 0x33, 24)
#define I915_QUERY IOC(IOWR, 'd', DRM_COMMAND_BASE + 0x39, 16)

#define PARAM_CHIPSET_ID 4
#define PARAM_HAS_ALIASING_PPGTT 18
#define PARAM_HAS_EXEC_NO_RELOC 25
#define PARAM_REVISION 32
#define PARAM_SUBSLICE_TOTAL 33
#define PARAM_EU_TOTAL 34
#define PARAM_HAS_GPU_RESET 35
#define PARAM_HAS_EXEC_SOFTPIN 37
#define PARAM_SLICE_MASK 46
#define PARAM_SUBSLICE_MASK 47
#define PARAM_HAS_EXEC_FENCE_ARRAY 49
#define PARAM_HAS_CONTEXT_ISOLATION 50
#define PARAM_HAS_USERPTR_PROBE 56

#define EXEC_RENDER (1u << 0)
#define EXEC_SECURE (1u << 9) /* privileged batch: needs CAP_SYS_ADMIN (root) */
#define EXEC_NO_RELOC (1u << 11)

static int exec_secure;
static int mi_store7; /* --mi-store: the gen7 MI_STORE_DWORD_IMM form instead of PIPE_CONTROL */

#define OBJ_WRITE (1ull << 2)
#define OBJ_SUPPORTS_48B (1ull << 3)
#define OBJ_PINNED (1ull << 4)

#define USERPTR_READ_ONLY 0x1u
#define QUERY_ENGINE_INFO 2
#define QUERY_TOPOLOGY_INFO 1

struct drm_version {
	i32 major, minor, patch;
	u64 name_len;
	char *name;
	u64 date_len;
	char *date;
	u64 desc_len;
	char *desc;
};

struct getparam {
	i32 param;
	i32 *value;
};

struct gem_create {
	u64 size;
	u32 handle;
	u32 pad;
};

struct gem_rw {
	u32 handle;
	u32 pad;
	u64 offset;
	u64 size;
	u64 data_ptr;
};

struct gem_wait {
	u32 bo_handle;
	u32 flags;
	i64 timeout_ns;
};

struct gem_close {
	u32 handle;
	u32 pad;
};

struct ctx_arg {
	u32 ctx_id;
	u32 pad;
};

struct userptr {
	u64 user_ptr;
	u64 user_size;
	u32 flags;
	u32 handle;
};

struct exec_obj {
	u32 handle;
	u32 relocation_count;
	u64 relocs_ptr;
	u64 alignment;
	u64 offset;
	u64 flags;
	u64 rsvd1;
	u64 rsvd2;
};

struct execbuf {
	u64 buffers_ptr;
	u32 buffer_count;
	u32 batch_start_offset;
	u32 batch_len;
	u32 DR1;
	u32 DR4;
	u32 num_cliprects;
	u64 cliprects_ptr;
	u64 flags;
	u64 rsvd1; /* the context id */
	u64 rsvd2;
};

struct query {
	u32 num_items;
	u32 flags;
	u64 items_ptr;
};

struct query_item {
	u64 query_id;
	i32 length;
	u32 flags;
	u64 data_ptr;
};

_Static_assert(sizeof(struct drm_version) == 64, "drm_version");
_Static_assert(sizeof(struct getparam) == 16, "drm_i915_getparam");
_Static_assert(sizeof(struct gem_create) == 16, "drm_i915_gem_create");
_Static_assert(sizeof(struct gem_rw) == 32, "drm_i915_gem_pread/pwrite");
_Static_assert(sizeof(struct gem_wait) == 16, "drm_i915_gem_wait");
_Static_assert(sizeof(struct gem_close) == 8, "drm_gem_close");
_Static_assert(sizeof(struct ctx_arg) == 8, "drm_i915_gem_context_create");
_Static_assert(sizeof(struct userptr) == 24, "drm_i915_gem_userptr");
_Static_assert(sizeof(struct exec_obj) == 56, "drm_i915_gem_exec_object2");
_Static_assert(sizeof(struct execbuf) == 64, "drm_i915_gem_execbuffer2");
_Static_assert(sizeof(struct query) == 16, "drm_i915_query");
_Static_assert(sizeof(struct query_item) == 24, "drm_i915_query_item");

static i64 fd = -1;
static u32 ctx_id;
static u32 chip_id, chip_kern;

static i64 ioc(u32 req, void *arg)
{
	return sys3(SYS_IOCTL, fd, (i64)req, (i64)(u64)arg);
}

static i64 gem_new(u64 size, u32 *h)
{
	struct gem_create c;
	i64 r;

	memset(&c, 0, sizeof c);
	c.size = size;
	r = ioc(I915_GEM_CREATE, &c);
	*h = c.handle;
	return r;
}

static i64 gem_rw(u32 req, u32 h, void *p, u64 n)
{
	struct gem_rw x;

	memset(&x, 0, sizeof x);
	x.handle = h;
	x.size = n;
	x.data_ptr = (u64)p;
	return ioc(req, &x);
}

static i64 gem_wait(u32 h, i64 ns)
{
	struct gem_wait w;

	memset(&w, 0, sizeof w);
	w.bo_handle = h;
	w.timeout_ns = ns;
	return ioc(I915_GEM_WAIT, &w);
}

static void gem_close(u32 h)
{
	struct gem_close c;

	if (!h)
		return;
	memset(&c, 0, sizeof c);
	c.handle = h;
	ioc(DRM_IOCTL_GEM_CLOSE, &c);
}

static i64 ctx_new(void)
{
	struct ctx_arg c;
	i64 r;

	memset(&c, 0, sizeof c);
	r = ioc(I915_GEM_CONTEXT_CREATE, &c);
	ctx_id = c.ctx_id;
	return r;
}

static void ctx_destroy(void)
{
	struct ctx_arg c;

	if (!ctx_id)
		return;
	memset(&c, 0, sizeof c);
	c.ctx_id = ctx_id;
	ioc(I915_GEM_CONTEXT_DESTROY, &c);
	ctx_id = 0;
}

static struct exec_obj obj(u32 h, u64 flags, u64 addr)
{
	struct exec_obj o;

	memset(&o, 0, sizeof o);
	o.handle = h;
	o.flags = flags;
	o.offset = addr;
	return o;
}

static i64 run(struct exec_obj *o, u32 n, u32 batch_len)
{
	struct execbuf e;

	memset(&e, 0, sizeof e);
	e.buffers_ptr = (u64)o;
	e.buffer_count = n;
	e.batch_len = batch_len;
	e.flags = EXEC_RENDER | EXEC_NO_RELOC | (exec_secure ? EXEC_SECURE : 0);
	e.rsvd1 = ctx_id;
	return ioc(I915_GEM_EXECBUFFER2, &e);
}

/* ---- device classification ------------------------------------------------
 * Ids from include/drm/intel/pciids.h (the Linux i915 tables). The kern value is
 * the command-set generation the probe would use. Gen6 and older are not listed:
 * they are out of scope (pre-2012) and the probe reports them as such.
 */
static const u16 ids_k7[] = {
	0x0156, 0x0166, 0x0152, 0x015A, 0x0162, 0x016A, /* Ivy Bridge */
	0x0F30, 0x0F31, 0x0F32, 0x0F33,                 /* Valleyview (Baytrail) */
};
static const u16 ids_k75[] = { /* Haswell: the command set differs from gen7 */
	0x0A02, 0x0A06, 0x0A0A, 0x0A0B, 0x0A0E, 0x0402, 0x0406, 0x040A, 0x040B, 0x040E,
	0x0C02, 0x0C06, 0x0C0A, 0x0C0B, 0x0C0E, 0x0D02, 0x0D06, 0x0D0A, 0x0D0B, 0x0D0E,
	0x0A12, 0x0A16, 0x0A1A, 0x0A1B, 0x0A1E, 0x0412, 0x0416, 0x041A, 0x041B, 0x041E,
	0x0C12, 0x0C16, 0x0C1A, 0x0C1B, 0x0C1E, 0x0D12, 0x0D16, 0x0D1A, 0x0D1B, 0x0D1E,
	0x0A22, 0x0A26, 0x0A2A, 0x0A2B, 0x0A2E, 0x0422, 0x0426, 0x042A, 0x042B, 0x042E,
	0x0C22, 0x0C26, 0x0C2A, 0x0C2B, 0x0C2E, 0x0D22, 0x0D26, 0x0D2A, 0x0D2B, 0x0D2E,
};
static const u16 ids_k8[] = {
	0x1606, 0x160B, 0x160E, 0x1602, 0x160A, 0x160D, /* Broadwell GT1 */
	0x1616, 0x161B, 0x161E, 0x1612, 0x161A, 0x161D, /* GT2 */
	0x1626, 0x162B, 0x162E, 0x1622, 0x162A, 0x162D, /* GT3 */
	0x1636, 0x163B, 0x163E, 0x1632, 0x163A, 0x163D, /* GT RSVD */
	0x22B0, 0x22B1, 0x22B2, 0x22B3,                 /* Cherry View */
};
static const u16 ids_k9[] = {
	0x1906, 0x1913, 0x190E, 0x1915, 0x1902, 0x190A, 0x190B, 0x1917, /* Skylake GT1 */
	0x1916, 0x1921, 0x191E, 0x1912, 0x191A, 0x191B, 0x191D,         /* GT2 */
	0x1923, 0x1926, 0x1927, 0x192A, 0x192B, 0x192D,                 /* GT3 */
	0x1932, 0x193A, 0x193B, 0x193D,                                 /* GT4 */
	0x0A84, 0x1A84, 0x1A85, 0x5A84, 0x5A85,                         /* Broxton/Apollo */
	0x3184, 0x3185,                                                 /* Gemini Lake */
	0x5906, 0x5913, 0x590E, 0x5915, 0x5902, 0x5908, 0x590A, 0x590B, /* Kaby Lake GT1 */
	0x5916, 0x5921, 0x591E, 0x5912, 0x5917, 0x591A, 0x591B, 0x591D, /* GT2 */
	0x5926, 0x5923, 0x5927, 0x593B, 0x591C, 0x87C0,                 /* GT3/GT4, AML */
	0x3E90, 0x3E93, 0x3E99, 0x3E91, 0x3E92, 0x3E96, 0x3E98, 0x3E9A, /* Coffee Lake S */
	0x3E9C, 0x3E94, 0x3E9B, 0x3EA9, 0x3EA5, 0x3EA6, 0x3EA7, 0x3EA8, /* H, U, Whiskey */
	0x3EA1, 0x3EA4, 0x3EA0, 0x3EA3, 0x3EA2, 0x87CA,                 /* WHL, AML CFL */
	0x9BA2, 0x9BA4, 0x9BA5, 0x9BA8, 0x9BC2, 0x9BC4, 0x9BC5, 0x9BC6, /* Comet Lake */
	0x9BC8, 0x9BE6, 0x9BF6, 0x9B21, 0x9BAA, 0x9BAC, 0x9B41, 0x9BCA, 0x9BCC,
};
static const u16 ids_k11[] = {
	0x8A50, 0x8A52, 0x8A53, 0x8A54, 0x8A56, 0x8A57, 0x8A58, 0x8A59, /* Ice Lake */
	0x8A5A, 0x8A5B, 0x8A5C, 0x8A70, 0x8A71, 0x8A51, 0x8A5D,
	0x4541, 0x4551, 0x4555, 0x4557, 0x4570, 0x4571,                 /* Elkhart Lake */
	0x4E51, 0x4E55, 0x4E57, 0x4E61, 0x4E71,                         /* Jasper Lake */
};
static const u16 ids_k12[] = {
	0x9A60, 0x9A68, 0x9A70, 0x9A40, 0x9A49, 0x9A59, 0x9A78, 0x9AC0, /* Tiger Lake */
	0x9AC9, 0x9AD9, 0x9AF8,
	0x4C80, 0x4C8A, 0x4C8B, 0x4C8C, 0x4C90, 0x4C9A,                 /* Rocket Lake */
	0x4680, 0x4682, 0x4688, 0x468A, 0x468B, 0x4690, 0x4692, 0x4693, /* Alder Lake S */
	0xA780, 0xA781, 0xA782, 0xA783, 0xA788, 0xA789, 0xA78A, 0xA78B, /* Raptor Lake S */
	0x46A0, 0x46A1, 0x46A2, 0x46A3, 0x46A6, 0x46A8, 0x46AA, 0x462A, /* Alder Lake P */
	0x4626, 0x4628, 0x46B0, 0x46B1, 0x46B2, 0x46B3, 0x46C0, 0x46C1,
	0x46C2, 0x46C3,
	0x46D0, 0x46D1, 0x46D2, 0x46D3, 0x46D4,                         /* Alder Lake N */
	0xA721, 0xA7A1, 0xA7A9, 0xA7AC, 0xA7AD,                         /* Raptor Lake U */
	0xA720, 0xA7A0, 0xA7A8, 0xA7AA, 0xA7AB,                         /* Raptor Lake P */
};
static const u16 ids_xe[] = { /* Xe-HP and later: xe-era, not implemented here */
	0x4905, 0x4906, 0x4907, 0x4908, 0x4909,                         /* DG1 */
	0x56A0, 0x56A1, 0x56A2, 0x56BE, 0x56BF, 0x5690, 0x5691, 0x5692, /* DG2 */
	0x56A5, 0x56A6, 0x56B0, 0x56B1, 0x56BA, 0x56BB, 0x56BC, 0x56BD,
	0x5693, 0x5694, 0x5695, 0x56A3, 0x56A4, 0x56B2, 0x56B3, 0x5696, 0x5697,
	0x56C0, 0x56C2, 0x56C1,                                         /* ATS-M */
	0x7D40, 0x7D45, 0x7D55, 0x7D60, 0x7DD5,                         /* Meteor Lake */
	0x7D51, 0x7DD1, 0x7D41, 0x7D67, 0xB640,                         /* Arrow Lake */
	0x6420, 0x64A0, 0x64B0,                                         /* Lunar Lake */
	0xE202, 0xE209, 0xE20B, 0xE20C, 0xE20D, 0xE210, 0xE211, 0xE212, /* Battlemage */
	0xE216, 0xE220, 0xE221, 0xE222, 0xE223,
	0xB080, 0xB081, 0xB082, 0xB083, 0xB084, 0xB085, 0xB086, 0xB087, /* Panther Lake */
	0xB08F, 0xB090, 0xB0A0, 0xB0A1, 0xB0B0,
};

static u32 in_list(u32 id, const u16 *l, u32 n)
{
	for (u32 i = 0; i < n; i++)
		if (l[i] == id)
			return 1;
	return 0;
}

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* 0 when the id is not in any table (pre-2012 or unknown) */
static u32 kern_of(u32 id)
{
	if (in_list(id, ids_k7, COUNT(ids_k7)))
		return 7;
	if (in_list(id, ids_k75, COUNT(ids_k75)))
		return 75;
	if (in_list(id, ids_k8, COUNT(ids_k8)))
		return 8;
	if (in_list(id, ids_k9, COUNT(ids_k9)))
		return 9;
	if (in_list(id, ids_k11, COUNT(ids_k11)))
		return 11;
	if (in_list(id, ids_k12, COUNT(ids_k12)))
		return 12;
	if (in_list(id, ids_xe, COUNT(ids_xe)))
		return 99;
	return 0;
}

/* The command path a kern uses: 7, 8, or 9 (9 covers 9, 11 and 12). Haswell
 * (75) uses the gen7 path with the Haswell surface channel selects. */
static u32 path_of(u32 kern)
{
	if (kern == 75)
		return 7;
	if (kern == 7 || kern == 8)
		return kern;
	return 9;
}

static const char *kern_name(u32 kern)
{
	switch (kern) {
	case 7: return "gen7 (Ivy Bridge, Valleyview)";
	case 75: return "gen7.5 (Haswell)";
	case 8: return "gen8 (Broadwell, Cherry View)";
	case 9: return "gen9 (Skylake..Comet Lake, Broxton, Gemini Lake)";
	case 11: return "gen11 (Ice Lake, Elkhart Lake, Jasper Lake)";
	case 12: return "gen12 (Tiger Lake, Rocket Lake, Alder Lake, Raptor Lake)";
	case 99: return "xe-era (DG1, DG2, Meteor/Arrow/Lunar Lake, Battlemage, Panther Lake)";
	}
	return "pre-2012 or not in the device tables";
}

/* ---- kernel log: i915 lines carrying a fault word, counted before and after ---- */

static char kbuf[1 << 20];
static char klast[256];
static u32 klast_n;

static int has(const char *s, u32 n, const char *p)
{
	u32 pl = slen(p);

	for (u32 i = 0; i + pl <= n; i++) {
		u32 j = 0;

		while (j < pl && s[i + j] == p[j])
			j++;
		if (j == pl)
			return 1;
	}
	return 0;
}

/* i915 fault signatures only: generic words (error, failed, warn) also match
 * unrelated display and USB lines on a live machine. */
static const char *kwords[] = {"GPU HANG", "GPU hang", "context reset", "Resetting",
			       "wedged", "*ERROR*", "time out", "stopped heartbeat",
			       "GPU fault", 0};
static int kmsg_ok;   /* the last kernel-log read worked */
static i64 kmsg_err;  /* its errno when it did not */

static u32 kmsg_count(void)
{
	i64 len = sys3(SYS_SYSLOG, 3, (i64)kbuf, sizeof kbuf); /* SYSLOG_ACTION_READ_ALL */
	u32 hits = 0, start = 0;

	klast_n = 0;
	kmsg_ok = len > 0;
	kmsg_err = len < 0 ? -len : 0;
	if (len <= 0)
		return 0;
	for (u32 i = 0; i <= (u32)len; i++) {
		if (i == (u32)len || kbuf[i] == '\n') {
			u32 n = i - start;
			const char *line = kbuf + start;
			int fault = 0;

			if (has(line, n, "i915") || has(line, n, "[drm]")) {
				for (u32 k = 0; kwords[k]; k++)
					if (has(line, n, kwords[k]))
						fault = 1;
			}
			if (fault) {
				hits++;
				klast_n = n < sizeof klast - 1 ? n : sizeof klast - 1;
				memcpy(klast, line, klast_n);
			}
			start = i + 1;
		}
	}
	return hits;
}

/* ---- facts ----------------------------------------------------------------- */

static const struct {
	i32 id;
	const char *name;
} params[] = {
	{PARAM_CHIPSET_ID, "CHIPSET_ID"},
	{PARAM_REVISION, "REVISION"},
	{PARAM_EU_TOTAL, "EU_TOTAL"},
	{PARAM_SUBSLICE_TOTAL, "SUBSLICE_TOTAL"},
	{PARAM_SLICE_MASK, "SLICE_MASK"},
	{PARAM_SUBSLICE_MASK, "SUBSLICE_MASK"},
	{PARAM_HAS_ALIASING_PPGTT, "HAS_ALIASING_PPGTT"},
	{PARAM_HAS_EXEC_SOFTPIN, "HAS_EXEC_SOFTPIN"},
	{PARAM_HAS_EXEC_NO_RELOC, "HAS_EXEC_NO_RELOC"},
	{PARAM_HAS_USERPTR_PROBE, "HAS_USERPTR_PROBE"},
	{PARAM_HAS_GPU_RESET, "HAS_GPU_RESET"},
	{PARAM_HAS_EXEC_FENCE_ARRAY, "HAS_EXEC_FENCE_ARRAY"},
	{PARAM_HAS_CONTEXT_ISOLATION, "HAS_CONTEXT_ISOLATION"},
};

static const char *engine_class(u16 c)
{
	switch (c) {
	case 0: return "render";
	case 1: return "copy";
	case 2: return "video";
	case 3: return "video-enhance";
	case 4: return "compute";
	}
	return "?";
}

static u32 popcnt8(u8 v)
{
	u32 n = 0;

	while (v) {
		n += v & 1;
		v >>= 1;
	}
	return n;
}

/* DRM_I915_QUERY_TOPOLOGY_INFO: 16-byte header (flags, max_slices, max_subslices,
 * max_eus_per_subslice, subslice_offset, subslice_stride, eu_offset, eu_stride);
 * subslice_offset and eu_offset are relative to data[], which starts at 16. */
static void topology(void)
{
	static u8 tb[4096] __attribute__((aligned(16)));
	struct query_item it;
	struct query q;
	i64 r;
	u32 slices = 0, subs = 0, eus = 0;
	u16 max_slices, max_subs, subo, subs_stride, eu_off, eu_stride;

	memset(&it, 0, sizeof it);
	memset(&q, 0, sizeof q);
	it.query_id = QUERY_TOPOLOGY_INFO;
	q.num_items = 1;
	q.items_ptr = (u64)&it;
	r = ioc(I915_QUERY, &q);
	if (r < 0 || it.length <= 0 || (u64)it.length > sizeof tb) {
		S("   topology: not reported for this part (query item ");
		if (it.length < 0) {
			S("errno ");
			D(-it.length);
			S(" ");
			S(ename(-it.length));
		} else {
			S("length ");
			D(it.length);
		}
		S(")");
		NL();
		return;
	}
	it.data_ptr = (u64)tb;
	r = ioc(I915_QUERY, &q);
	if (r < 0 || it.length < 16 || (u64)it.length > sizeof tb) {
		S("   topology: ");
		RC(r < 0 ? r : (it.length < 0 ? it.length : -22));
		NL();
		return;
	}
	max_slices = *(u16 *)(tb + 2);
	max_subs = *(u16 *)(tb + 4);
	subo = *(u16 *)(tb + 8);
	subs_stride = *(u16 *)(tb + 10);
	eu_off = *(u16 *)(tb + 12);
	eu_stride = *(u16 *)(tb + 14);
	{
		/* every byte the loops read must lie inside the reply */
		u64 L = (u64)(u32)it.length;
		u64 need_sl = 16ull + ((u64)max_slices + 7ull) / 8ull;
		u64 need_sub = max_slices ? 16ull + subo + (u64)(max_slices - 1) * subs_stride + ((u64)max_subs + 7ull) / 8ull : 0;
		u64 need_eu = (max_slices && max_subs)
				  ? 16ull + eu_off + (u64)max_slices * max_subs * eu_stride
				  : 0;

		if (L < 16 || need_sl > L || need_sub > L || need_eu > L) {
			S("   topology: malformed reply (offsets outside the data)");
			NL();
			return;
		}
	}
	for (u32 s = 0; s < max_slices; s++) {
		if (!((tb[16 + s / 8] >> (s & 7)) & 1))
			continue;
		slices++;
		for (u32 ss = 0; ss < max_subs; ss++) {
			u8 *sub = tb + 16 + subo + s * subs_stride;

			if (!((sub[ss / 8] >> (ss & 7)) & 1))
				continue;
			subs++;
			for (u32 b = 0; b < eu_stride; b++)
				eus += popcnt8(tb[16 + eu_off + (s * max_subs + ss) * eu_stride + b]);
		}
	}
	S("   topology: slices ");
	D(slices);
	S(" subslices ");
	D(subs);
	S(" EUs ");
	D(eus);
	S(" (max slices ");
	D(max_slices);
	S(", max subslices ");
	D(max_subs);
	S(")");
	NL();
}

/* 0 ok, 1 failed, 2 not i915 */
static int facts(void)
{
	static char name[64], date[64], desc[128];
	static u8 qbuf[4096] __attribute__((aligned(16)));
	struct drm_version v;
	struct query_item it;
	struct query q;
	i64 r;
	u32 n;

	memset(&v, 0, sizeof v);
	v.name_len = sizeof name;
	v.name = name;
	v.date_len = sizeof date;
	v.date = date;
	v.desc_len = sizeof desc;
	v.desc = desc;
	r = ioc(DRM_IOCTL_VERSION, &v);
	S("1 facts: DRM_IOCTL_VERSION ");
	RC(r);
	NL();
	if (r < 0)
		return 1;
	if (v.name_len != 4 || name[0] != 'i' || name[1] != '9' || name[2] != '1' || name[3] != '5') {
		S("   refused: driver is ");
		put(name, v.name_len > 63 ? 63 : (u32)v.name_len);
		if (v.name_len == 2 && name[0] == 'x' && name[1] == 'e')
			S(" (the xe driver: this probe speaks i915 only)");
		else
			S(" (this probe speaks i915 only)");
		NL();
		return 2;
	}
	S("   driver i915 ");
	D(v.major);
	S(".");
	D(v.minor);
	S(".");
	D(v.patch);
	S(" ");
	put(date, v.date_len > 63 ? 63 : (u32)v.date_len);
	S(" ");
	put(desc, v.desc_len > 127 ? 127 : (u32)v.desc_len);
	NL();

	for (u32 i = 0; i < sizeof params / sizeof params[0]; i++) {
		i32 val = -1;
		struct getparam g;

		g.param = params[i].id;
		g.value = &val;
		r = ioc(I915_GETPARAM, &g);
		S("   GETPARAM ");
		S(params[i].name);
		S(" (");
		D(params[i].id);
		S(") = ");
		if (r < 0) {
			RC(r);
		} else {
			D(val);
			S(" ");
			H((u64)(u32)val);
			if (params[i].id == PARAM_CHIPSET_ID)
				chip_id = (u32)val;
		}
		NL();
	}
	chip_kern = kern_of(chip_id);
	S("   device: ");
	H(chip_id);
	S(" class ");
	D(chip_kern);
	S(": ");
	S(kern_name(chip_kern));
	NL();

	topology();

	memset(&it, 0, sizeof it);
	memset(&q, 0, sizeof q);
	it.query_id = QUERY_ENGINE_INFO;
	q.num_items = 1;
	q.items_ptr = (u64)&it;
	r = ioc(I915_QUERY, &q);
	if (r < 0 || it.length <= 0 || (u64)it.length > sizeof qbuf) {
		S("   QUERY ENGINE_INFO size: ");
		RC(r);
		S(" length ");
		D(it.length);
		NL();
		return 1;
	}
	it.data_ptr = (u64)qbuf;
	r = ioc(I915_QUERY, &q);
	S("   QUERY ENGINE_INFO ");
	RC(r);
	S(" bytes ");
	D(it.length);
	NL();
	if (r < 0 || it.length < 16 || (u64)it.length > sizeof qbuf)
		return 1;
	n = *(u32 *)qbuf;
	S("   engines ");
	D(n);
	NL();
	for (u32 i = 0; i < n && 16ull + (u64)(i + 1) * 56ull <= (u64)(u32)it.length; i++) {
		u8 *e = qbuf + 16 + i * 56;
		u16 cls = *(u16 *)(e + 0), inst = *(u16 *)(e + 2), logical = *(u16 *)(e + 24);
		u64 flags = *(u64 *)(e + 8), caps = *(u64 *)(e + 16);

		S("     class ");
		D(cls);
		S(" (");
		S(engine_class(cls));
		S(") instance ");
		D(inst);
		S(" logical ");
		if (flags & 1)
			D(logical);
		else
			S("-");
		S(" caps ");
		H(caps);
		NL();
	}
	return 0;
}

/* ---- addresses (softpinned; all below 4 GiB so gen7 aliasing PPGTT takes them) */

/* --base N moves every pinned address up by N (gen7 shares the GGTT with the
 * display: low addresses can be taken by the scanout buffers). */
static u64 addr_bias;
#define ADDR_STORE_BATCH (addr_bias + 0x400000ull)
#define ADDR_STORE_TARGET (addr_bias + 0x800000ull)
#define ADDR_USERPTR (addr_bias + 0x3000000ull)
#define ADDR_USERPTR_BATCH (addr_bias + 0x420000ull)
#define ADDR_STATE (addr_bias + 0x1000000ull)
#define ADDR_TARGET (addr_bias + 0x2000000ull)
#define TARGET_MAX (1u << 20)

/* gen8 and later have full PPGTT (48-bit addresses); gen7 has aliasing only */
static u64 objflags(u32 path)
{
	return path == 7 ? OBJ_PINNED : (OBJ_SUPPORTS_48B | OBJ_PINNED);
}

/* gen7: MI_STORE_DWORD_IMM 0x10000001 (3 dwords), bit 22 = global GTT.
 * gen8+: MI_STORE_DWORD_IMM 0x10000002 (4 dwords, PPGTT). */
static i64 store_dword(u32 path, u32 ggtt, u32 target, u64 taddr, u32 batch, u64 baddr, u32 value)
{
	u32 b[8];
	struct exec_obj o[2];
	u32 len;
	i64 r;

	memset(b, 0, sizeof b);
	if (path == 7 && !mi_store7) {
		/* PIPE_CONTROL write-immediate (DW, PPGTT destination). The MI store form
		 * is privileged on gen7 outside a secure batch, which the root shell cannot
		 * get (EPERM); PIPE_CONTROL is an ordinary render command. */
		(void)ggtt;
		b[0] = 0x7a000003;          /* PIPE_CONTROL, 5 dwords */
		b[1] = 1u << 14;            /* post-sync: write immediate data (DW) */
		b[2] = (u32)taddr & ~7u;    /* address, PPGTT */
		b[3] = value;
		b[4] = 0;
		b[5] = 0x05000000;          /* MI_BATCH_BUFFER_END */
		b[6] = 0;
		b[7] = 0;
		len = 32;
	} else if (path == 7) {
		b[0] = 0x10000001 | (ggtt ? (1u << 22) : 0);
		b[1] = (u32)taddr & ~3u;
		b[2] = value;
		b[3] = 0x05000000;
		len = 16;
	} else {
		b[0] = 0x10000002;
		b[1] = (u32)taddr & ~3u;
		b[2] = (u32)(taddr >> 32);
		b[3] = value;
		b[4] = 0x05000000;
		len = 24;
	}
	r = gem_rw(I915_GEM_PWRITE, batch, b, len);
	if (r < 0)
		return r;
	o[0] = obj(target, objflags(path) | OBJ_WRITE, taddr);
	o[1] = obj(batch, objflags(path), baddr);
	return run(o, 2, len);
}

static int ensure_ctx(void)
{
	i64 r;

	if (ctx_id)
		return 1;
	r = ctx_new();
	S("   context ");
	RC(r);
	S(" id ");
	D(ctx_id);
	NL();
	return r >= 0;
}

static int step_a(u32 path, u32 ggtt)
{
	u32 tgt = 0, bat = 0, v = 0, sentinel = 0x5a5a5a5a;
	i64 r;

	sys3(SYS_ALARM, 30, 0, 0);
	S("2 step a (store dword), path gen");
	D(path);
	NL();
	if (!ensure_ctx()) {
		S("RESULT step a: FAIL (no context)\n");
		flush();
		return 0;
	}
	r = gem_new(4096, &tgt);
	S("   target BO ");
	RC(r);
	S(" handle ");
	D(tgt);
	NL();
	r = gem_new(4096, &bat);
	S("   batch BO ");
	RC(r);
	S(" handle ");
	D(bat);
	NL();
	if (tgt == 0 || bat == 0) {
		gem_close(tgt);
		gem_close(bat);
		S("RESULT step a: FAIL (no BO handle)\n");
		flush();
		return 0;
	}
	r = gem_rw(I915_GEM_PWRITE, tgt, &sentinel, 4);
	S("   PWRITE sentinel 0x5a5a5a5a ");
	RC(r);
	NL();
	r = store_dword(path, ggtt, tgt, ADDR_STORE_TARGET, bat, ADDR_STORE_BATCH, 0xdeadbeef);
	S("   EXECBUFFER2 render, NO_RELOC, pinned target 0x800000 ");
	RC(r);
	NL();
	r = gem_wait(tgt, 2000000000);
	S("   GEM_WAIT 2 s ");
	RC(r);
	NL();
	r = gem_rw(I915_GEM_PREAD, tgt, &v, 4);
	S("   GEM_PREAD dword ");
	RC(r);
	S(" = ");
	H(v);
	NL();
	gem_close(tgt);
	gem_close(bat);
	if (v == 0xdeadbeef) {
		S("RESULT step a: PASS\n");
		flush();
		return 1;
	}
	S("RESULT step a: FAIL\n");
	flush();
	return 0;
}

static void munmap_page(i64 pg)
{
	if (pg > 0)
		sys3(SYS_MUNMAP, pg, 4096, 0);
}

static int step_userptr(u32 path, u32 ggtt)
{
	i64 pg, r0, r1, r;
	struct userptr u0, u1;
	u32 *page, bat = 0, gpu = 0, cpu;
	int ok;

	sys3(SYS_ALARM, 30, 0, 0);
	S("3 userptr: mmap one anonymous page\n");
	pg = sys6(SYS_MMAP, 0, 4096, 3, 0x22, -1, 0);
	S("   mmap ");
	RC(pg);
	NL();
	if (pg < 0 && pg > -4096) {
		S("RESULT userptr: FAIL\n");
		flush();
		return 0;
	}
	page = (u32 *)(u64)pg;
	page[0] = 0x5a5a5a5a;

	memset(&u0, 0, sizeof u0);
	u0.user_ptr = (u64)page;
	u0.user_size = 4096;
	u0.flags = 0;
	r0 = ioc(I915_GEM_USERPTR, &u0);
	S("   GEM_USERPTR flags 0x0: ");
	RC(r0);
	S(" handle ");
	D(u0.handle);
	NL();

	memset(&u1, 0, sizeof u1);
	u1.user_ptr = (u64)page;
	u1.user_size = 4096;
	u1.flags = USERPTR_READ_ONLY;
	r1 = ioc(I915_GEM_USERPTR, &u1);
	S("   GEM_USERPTR flags READ_ONLY 0x1: ");
	RC(r1);
	S(" handle ");
	D(u1.handle);
	NL();
	gem_close(u1.handle); /* accepted or not, never written by the GPU */

	ok = 0;
	if (r0 < 0) {
		S("   plain userptr refused\n");
	} else if (!ensure_ctx()) {
		S("   no context\n");
	} else {
		r = gem_new(4096, &bat);
		S("   batch BO ");
		RC(r);
		S(" handle ");
		D(bat);
		NL();
		if (r >= 0 && bat) {
			r = store_dword(path, ggtt, u0.handle, ADDR_USERPTR, bat, ADDR_USERPTR_BATCH, 0xdeadbeef);
			S("   EXECBUFFER2 store to userptr pinned 0x3000000 ");
			RC(r);
			NL();
			r = gem_wait(u0.handle, 2000000000);
			S("   GEM_WAIT 2 s ");
			RC(r);
			NL();
			cpu = page[0];
			S("   CPU reads the page: ");
			H(cpu);
			NL();
			r = gem_rw(I915_GEM_PREAD, u0.handle, &gpu, 4);
			S("   GEM_PREAD of the userptr BO ");
			RC(r);
			S(" (the CPU page is the check) = ");
			H(gpu);
			NL();
			ok = (cpu == 0xdeadbeef);
		}
		gem_close(bat);
	}
	gem_close(u0.handle);
	munmap_page(pg);
	if (ok) {
		S("RESULT userptr: PASS\n");
		flush();
		return 1;
	}
	S("RESULT userptr: FAIL\n");
	flush();
	return 0;
}

/* ---- the GPGPU fill walker (IGT gpgpu_fill.c) ----------------------------- */

/* IGT gen7_gpgpu_kernel, gen8_gpgpu_kernel, gen9_gpgpu_kernel,
 * gen11_gpgpu_kernel, gen12_gpgpu_kernel: [][4], verbatim. */
static const u32 kern7[10][4] = {
	{0x00400001, 0x20200231, 0x00000020, 0x00000000},
	{0x00000041, 0x20400c21, 0x00000004, 0x00000010},
	{0x00000001, 0x20440021, 0x00000018, 0x00000000},
	{0x00600001, 0x20800021, 0x008d0000, 0x00000000},
	{0x00200001, 0x20800021, 0x00450040, 0x00000000},
	{0x00000001, 0x20880061, 0x00000000, 0x0000000f},
	{0x00800001, 0x20a00021, 0x00000020, 0x00000000},
	{0x05800031, 0x24001ca8, 0x00000080, 0x060a8000},
	{0x00600001, 0x2e000021, 0x008d0000, 0x00000000},
	{0x07800031, 0x20001ca8, 0x00000e00, 0x82000010},
};
static const u32 kern8[10][4] = {
	{0x00400001, 0x20202288, 0x00000020, 0x00000000},
	{0x00000041, 0x20400208, 0x06000004, 0x00000010},
	{0x00000001, 0x20440208, 0x00000018, 0x00000000},
	{0x00600001, 0x20800208, 0x008d0000, 0x00000000},
	{0x00200001, 0x20800208, 0x00450040, 0x00000000},
	{0x00000001, 0x20880608, 0x00000000, 0x0000000f},
	{0x00800001, 0x20a00208, 0x00000020, 0x00000000},
	{0x0c800031, 0x24000a40, 0x0e000080, 0x060a8000},
	{0x00600001, 0x2e000208, 0x008d0000, 0x00000000},
	{0x07800031, 0x20000a40, 0x0e000e00, 0x82000010},
};
static const u32 kern9[10][4] = {
	{0x00400001, 0x20202288, 0x00000020, 0x00000000},
	{0x00000041, 0x20400208, 0x06000004, 0x00000010},
	{0x00000001, 0x20440208, 0x00000018, 0x00000000},
	{0x00600001, 0x20800208, 0x008d0000, 0x00000000},
	{0x00200001, 0x20800208, 0x00450040, 0x00000000},
	{0x00000001, 0x20880608, 0x00000000, 0x0000000f},
	{0x00800001, 0x20a00208, 0x00000020, 0x00000000},
	{0x0c800031, 0x24000a40, 0x06000080, 0x060a8000},
	{0x00600001, 0x2e000208, 0x008d0000, 0x00000000},
	{0x07800031, 0x20000a40, 0x06000e00, 0x82000010},
};
static const u32 kern11[10][4] = {
	{0x00400001, 0x20202288, 0x00000020, 0x00000000},
	{0x00000009, 0x20400208, 0x06000004, 0x00000004},
	{0x00000001, 0x20440208, 0x00000018, 0x00000000},
	{0x00600001, 0x20800208, 0x008d0000, 0x00000000},
	{0x00200001, 0x20800208, 0x00450040, 0x00000000},
	{0x00000001, 0x20880608, 0x00000000, 0x0000000f},
	{0x00800001, 0x20a00208, 0x00000020, 0x00000000},
	{0x0c800031, 0x24000a40, 0x06000080, 0x040a8000},
	{0x00600001, 0x2e000208, 0x008d0000, 0x00000000},
	{0x07800031, 0x20000a40, 0x06000e00, 0x82000010},
};
static const u32 kern12[10][4] = {
	{0x00020061, 0x01050000, 0x00000104, 0x00000000},
	{0x00000069, 0x02058220, 0x02000024, 0x00000004},
	{0x00000061, 0x02250220, 0x000000c4, 0x00000000},
	{0x00030061, 0x04050220, 0x00460005, 0x00000000},
	{0x00010261, 0x04050220, 0x00220205, 0x00000000},
	{0x00000061, 0x04454220, 0x00000000, 0x0000000f},
	{0x00040661, 0x05050220, 0x00000104, 0x00000000},
	{0x00049031, 0x00000000, 0xc0000414, 0x02a00000},
	{0x00030061, 0x70050220, 0x00460005, 0x00000000},
	{0x00040131, 0x00000004, 0x7020700c, 0x10000000},
};

struct cfg {
	u32 kern;  /* 7, 8, 9, 11, 12 */
	u32 w, h, pitch;
	u32 colour;
	u32 rgba, kalt, mocs, control, guard, ggtt;
	u32 padmask;       /* all 16 lanes in the last group (avoids the one-lane right mask) */
	u32 fault, badlen;
	u32 repeat;
};

static u8 img[65536] __attribute__((aligned(4096)));
static u32 bump, cbuf_o, bt_o, ss_o, kern_o, idd_o;
static u8 tbuf[TARGET_MAX] __attribute__((aligned(4096)));
static u8 gout[2][4096] __attribute__((aligned(16)));
static u8 gfill[4096] __attribute__((aligned(16)));

static void w32(u32 off, u32 v)
{
	img[off + 0] = (u8)v;
	img[off + 1] = (u8)(v >> 8);
	img[off + 2] = (u8)(v >> 16);
	img[off + 3] = (u8)(v >> 24);
}

static u32 alloc64(u32 n)
{
	bump = (bump + 63) & ~63u;
	bump += n;
	return bump - n;
}

static const u32 (*kernel_of(u32 kern))[4]
{
	switch (kern) {
	case 7: return kern7;
	case 75: return kern7; /* IGT's gen7 fill kernel is used on Haswell */
	case 8: return kern8;
	case 11: return kern11;
	case 12: return kern12;
	}
	return kern9;
}

/* Lays out the batch at offset 0 and the state from 2048, in IGT's order.
 * Returns the batch length in bytes (the padded length the execbuffer sends). */
static u32 build(const struct cfg *c)
{
	const u32 path = path_of(c->kern);
	const u32 ss_size = path == 7 ? 32 : 64;
	const u32 idd_size = path == 7 ? 20 : 32;
	const u32 fmt = c->rgba ? 0x0c7 : 0x140; /* R8G8B8A8_UNORM : R8_UNORM */
	const u32 wel = c->rgba ? c->w / 4 : c->w;
	const u32 xgroups = (0 + c->w + 15) / 16;
	const u32 ygroups = 0 + c->h;
	const u32 tmp = (0 + c->w) & 15;
	/* A right mask with exactly one lane (w = 16k+1) makes the walker dispatch one
	 * row only (measured on gen7 and gen9): --padmask asks for all 16 lanes. */
	const u32 right = (tmp && !c->padmask) ? (1u << tmp) - 1 : 0xffff;
	const u32 blen = path == 7 ? 160 : 224;
	const u32 (*kt)[4] = kernel_of(c->kern);
	u32 bw[64];
	u32 n = 0;

	memset(img, 0, sizeof img);
	bump = 2048;
	cbuf_o = alloc64(64);
	bt_o = alloc64(64);
	ss_o = alloc64(ss_size);
	kern_o = alloc64(160);
	idd_o = alloc64(idd_size);

	w32(cbuf_o, c->colour); /* CURBE dword 0 = the colour */

	for (u32 i = 0; i < 10; i++)
		for (u32 j = 0; j < 4; j++)
			w32(kern_o + 16 * i + 4 * j, kt[i][j]);
	if (c->kern == 9 && c->kalt) /* the other acc0/null bit in row 7 dword 1 */
		w32(kern_o + 16 * 7 + 4, 0x20000a40);
	if (c->kern == 8 && c->kalt)
		w32(kern_o + 16 * 7 + 4, 0x20000a40);
	if (c->kern == 11 && c->kalt)
		w32(kern_o + 16 * 7 + 4, 0x20000a40);

	/* RENDER_SURFACE_STATE. ss0: type 2D at 31:29, format 26:18, render cache RW
	 * bit 8; gen8 and gen9 also set halign/valign 4 at 15:14 and 17:16 (IGT's
	 * gen8/gen9 fill); gen7 (IGT gen7_fill_surface_state) leaves them zero.
	 * ss1: MOCS at 30:25 on gen9 and gen11/12, at 30:24 on gen8 (memory object
	 * control); gen7 has the base address here. ss2 width-1 13:0 and height-1 29:16,
	 * ss3 pitch-1, ss7 channel select R4 G5 B6 A7 at 27:25..18:16 (gen7 has no
	 * channel fields; IGT writes them only for gen8+). ss8/ss9 base (gen8+). */
	if (path == 7) {
		w32(ss_o + 0, (1u << 29) | (fmt << 18) | (1u << 8));
		w32(ss_o + 4, (u32)ADDR_TARGET);
		w32(ss_o + 8, (wel - 1) | ((c->h - 1) << 16));
		w32(ss_o + 12, c->pitch - 1);
		if (c->kern == 75) /* Haswell has the channel-select fields in ss7 (IGT gen7 fill) */
			w32(ss_o + 28, (4u << 25) | (5u << 22) | (6u << 19) | (7u << 16));
	} else {
		w32(ss_o + 0, (1u << 29) | (fmt << 18) | (1u << 8) | (1u << 14) | (1u << 16));
		w32(ss_o + 4, c->mocs << (path == 8 ? 24 : 25));
		w32(ss_o + 8, (wel - 1) | ((c->h - 1) << 16));
		w32(ss_o + 12, c->pitch - 1);
		w32(ss_o + 28, (4u << 25) | (5u << 22) | (6u << 19) | (7u << 16));
		w32(ss_o + 32, (u32)ADDR_TARGET);
		w32(ss_o + 36, (u32)(ADDR_TARGET >> 32));
	}

	w32(bt_o, ss_o); /* binding table entry 0 */

	if (path == 7) {
		w32(idd_o + 0, kern_o);
		w32(idd_o + 4, 1u << 18); /* single program flow */
		w32(idd_o + 8, 0);
		w32(idd_o + 12, bt_o);
		w32(idd_o + 16, 1u << 16);
	} else {
		w32(idd_o + 0, kern_o);
		w32(idd_o + 4, 0);
		w32(idd_o + 8, 1u << 18);
		w32(idd_o + 12, 0);
		w32(idd_o + 16, bt_o);
		w32(idd_o + 20, 1u << 16);
		w32(idd_o + 24, 1);
		w32(idd_o + 28, 0);
	}
	if (c->fault) /* the EU fetches an unbound address: a GPU page fault, hang, reset */
		w32(idd_o + 0, 0x3ffff00);

	if (path == 7) {
		bw[n++] = 0x69040002; /* PIPELINE_SELECT, GPGPU (gen7: no mask) */
		bw[n++] = 0x61010008; /* STATE_BASE_ADDRESS, 10 dwords */
		bw[n++] = 0;
		bw[n++] = (u32)ADDR_STATE | 1;
		bw[n++] = (u32)ADDR_STATE | 1;
		bw[n++] = 0;
		bw[n++] = (u32)ADDR_STATE | 1;
		bw[n++] = 0; bw[n++] = 1; bw[n++] = 0; bw[n++] = 1;
		bw[n++] = 0x70000006; /* MEDIA_VFE_STATE, 8 dwords, GPGPU mode */
		bw[n++] = 0;
		bw[n++] = (1u << 16) | (1u << 2);
		bw[n++] = 0;
		bw[n++] = 1;
		bw[n++] = 0; bw[n++] = 0; bw[n++] = 0;
	} else if (path == 8) {
		bw[n++] = 0x69040002; /* PIPELINE_SELECT, GPGPU */
		bw[n++] = 0x6101000e; /* STATE_BASE_ADDRESS, 16 dwords (no bindless) */
		bw[n++] = 1; bw[n++] = 0;
		bw[n++] = 1;
		bw[n++] = (u32)ADDR_STATE | 1; bw[n++] = (u32)(ADDR_STATE >> 32);
		bw[n++] = (u32)ADDR_STATE | 1; bw[n++] = (u32)(ADDR_STATE >> 32);
		bw[n++] = 0; bw[n++] = 0;
		bw[n++] = (u32)ADDR_STATE | 1; bw[n++] = (u32)(ADDR_STATE >> 32);
		bw[n++] = 0xfffff000 | 1;
		bw[n++] = 65536 | 1;
		bw[n++] = 0xfffff000 | 1;
		bw[n++] = 65536 | 1;
		bw[n++] = 0x70000007; /* MEDIA_VFE_STATE, 9 dwords */
		bw[n++] = 0; bw[n++] = 0;
		bw[n++] = (1u << 16) | (1u << 8);
		bw[n++] = 0;
		bw[n++] = 1;
		bw[n++] = 0; bw[n++] = 0; bw[n++] = 0;
	} else {
		bw[n++] = 0x69040302; /* PIPELINE_SELECT, mask 3<<8, GPGPU */
		bw[n++] = 0x61010011; /* STATE_BASE_ADDRESS, 19 dwords */
		bw[n++] = 1; bw[n++] = 0;
		bw[n++] = 1;
		bw[n++] = (u32)ADDR_STATE | 1; bw[n++] = (u32)(ADDR_STATE >> 32);
		bw[n++] = (u32)ADDR_STATE | 1; bw[n++] = (u32)(ADDR_STATE >> 32);
		bw[n++] = 0; bw[n++] = 0;
		bw[n++] = (u32)ADDR_STATE | 1; bw[n++] = (u32)(ADDR_STATE >> 32);
		bw[n++] = 0xfffff000 | 1;
		bw[n++] = 65536 | 1;
		bw[n++] = 0xfffff000 | 1;
		bw[n++] = 65536 | 1;
		bw[n++] = 1; bw[n++] = 0;
		bw[n++] = 0xfffff000;
		bw[n++] = 0x70000007; /* MEDIA_VFE_STATE, 9 dwords */
		bw[n++] = 0; bw[n++] = 0;
		bw[n++] = (1u << 16) | (1u << 8);
		bw[n++] = 0;
		bw[n++] = 1;
		bw[n++] = 0; bw[n++] = 0; bw[n++] = 0;
	}
	bw[n++] = 0x70010002; /* MEDIA_CURBE_LOAD, 64 bytes */
	bw[n++] = 0; bw[n++] = 64; bw[n++] = cbuf_o;
	bw[n++] = 0x70020002; /* MEDIA_INTERFACE_DESCRIPTOR_LOAD */
	bw[n++] = 0; bw[n++] = idd_size; bw[n++] = idd_o;

	if (!c->control) {
		if (path == 7) {
			bw[n++] = 0x71050009; /* GPGPU_WALKER, 11 dwords */
			bw[n++] = 0;
			bw[n++] = 1u << 30; /* SIMD16 */
			bw[n++] = 0; bw[n++] = xgroups;
			bw[n++] = 0; bw[n++] = ygroups;
			bw[n++] = 0; bw[n++] = 1;
			bw[n++] = right;
			bw[n++] = 0xffffffff;
		} else {
			bw[n++] = 0x7105000d; /* GPGPU_WALKER, 15 dwords */
			bw[n++] = 0; bw[n++] = 0; bw[n++] = 0;
			bw[n++] = 1u << 30; /* SIMD16 */
			bw[n++] = 0; bw[n++] = 0; bw[n++] = xgroups;
			bw[n++] = 0; bw[n++] = 0; bw[n++] = ygroups;
			bw[n++] = 0; bw[n++] = 1;
			bw[n++] = right;
			bw[n++] = 0xffffffff;
		}
	}
	bw[n++] = 0x05000000; /* MI_BATCH_BUFFER_END */
	while (n < blen / 4)
		bw[n++] = 0;
	for (u32 i = 0; i < blen / 4; i++)
		w32(4 * i, bw[i]);
	return blen;
}

/* A walker run that fails before it runs: still a RESULT line, so the matrix counts it. */
static int fail_early(const struct cfg *c, u32 index, const char *why, i64 r)
{
	S("SUMMARY kern");
	D(c->kern);
	S(" walker #");
	D(index);
	S(" early failure: ");
	S(why);
	if (r < 0) {
		S(" errno ");
		D(-r);
		S(" ");
		S(ename(-r));
	}
	S(" RESULT FAIL\n");
	flush();
	return 0;
}

/* One walker run: build, fill guards, execbuffer, read back, check. Returns 1 on PASS. */
static int walker_once(const struct cfg *c, u32 index)
{
	const u32 path = path_of(c->kern);
	const u64 raw_bytes = (u64)c->pitch * (u64)c->h;
	const u32 tb = raw_bytes <= TARGET_MAX ? (u32)((raw_bytes + 4095ull) & ~4095ull) : (u32)(TARGET_MAX + 1u);
	const u64 post = ADDR_TARGET + tb;
	u32 sh = 0, th = 0, g1 = 0, g2 = 0;
	u32 blen, inside_bad = 0, pad_hits = 0, guard_bad = 0, written = 0;
	u32 k0, k1, kdelta;
	i64 r, wt, rr;
	struct exec_obj o[4];
	u32 no = 0;
	int pass;

	sys3(SYS_ALARM, 30, 0, 0); /* re-armed per run: a long matrix is not one budget */
	if (c->kalt && c->kern != 8 && c->kern != 9 && c->kern != 11)
		return fail_early(c, index, "--kalt is defined for kern 8, 9 and 11 only", 0);
	if (!ensure_ctx())
		return fail_early(c, index, "no context", 0);
	if (tb > TARGET_MAX || c->pitch < c->w || (c->pitch & 63))
		return fail_early(c, index, "bad shape (pitch must cover w, 64-aligned, target <= 1 MiB)", 0);
	blen = build(c);
	r = gem_new(65536, &sh);
	if (r < 0 || !sh)
		return fail_early(c, index, "state BO", r);
	r = gem_new(tb, &th);
	if (r < 0 || !th) {
		gem_close(sh);
		return fail_early(c, index, "target BO", r);
	}
	if (c->guard) {
		memset(gfill, 0x3c, sizeof gfill);
		r = gem_new(4096, &g1);
		if (r >= 0)
			r = gem_new(4096, &g2);
		if (r < 0) {
			gem_close(g1);
			gem_close(th);
			gem_close(sh);
			return fail_early(c, index, "guard BO", r);
		}
		gem_rw(I915_GEM_PWRITE, g1, gfill, 4096);
		gem_rw(I915_GEM_PWRITE, g2, gfill, 4096);
	}
	/* The target starts as the inverse of the colour, so no unwritten byte can
	 * match the colour (a zero-filled target with colour 0 would pass vacuously). */
	memset(tbuf, (int)(u8)~c->colour, tb);
	gem_rw(I915_GEM_PWRITE, th, tbuf, tb);
	gem_rw(I915_GEM_PWRITE, sh, img, 65536);

	k0 = kmsg_count();
	o[no++] = obj(th, objflags(path) | OBJ_WRITE, ADDR_TARGET);
	if (c->guard) {
		o[no++] = obj(g1, objflags(path) | OBJ_WRITE, ADDR_TARGET - 4096);
		o[no++] = obj(g2, objflags(path) | OBJ_WRITE, post);
	}
	o[no++] = obj(sh, objflags(path), ADDR_STATE);
	r = run(o, no, c->badlen ? blen + 1 : blen);
	wt = gem_wait(th, 2000000000);
	rr = gem_rw(I915_GEM_PREAD, th, tbuf, tb);

	if (rr >= 0) {
		for (u32 y = 0; y < c->h; y++)
			for (u32 x = 0; x < c->pitch; x++) {
				u8 v = tbuf[y * c->pitch + x];

				if (x < c->w) {
					if (v == (u8)c->colour)
						written++;
					if (v != (u8)c->colour)
						inside_bad++;
				} else if (v == (u8)c->colour) {
					pad_hits++;
				}
			}
	}
	if (c->guard && rr >= 0) {
		u32 gb[2] = {g1, g2};

		for (u32 k = 0; k < 2; k++) {
			if (gem_rw(I915_GEM_PREAD, gb[k], gout[k], 4096) < 0) {
				guard_bad += 4096;
				continue;
			}
			for (u32 i = 0; i < 4096; i++)
				if (gout[k][i] != 0x3c)
					guard_bad++;
		}
	}
	k1 = kmsg_count();
	kdelta = k1 > k0 ? k1 - k0 : 0;

	if (c->badlen)
		/* a batch length that is not 8-aligned must be refused (EINVAL), and nothing may run */
		pass = r == -22 && wt == 0 && kmsg_ok && kdelta == 0;
	else if (c->fault)
		/* the hang must be seen: the batch went in and the wait timed out (ETIME),
		 * or the kernel logged the reset. Any other error proves nothing. */
		pass = r == 0 && (wt == -62 || (kmsg_ok && kdelta > 0));
	else if (c->control)
		pass = r == 0 && wt == 0 && rr >= 0 && written == 0 && kmsg_ok && kdelta == 0;
	else
		pass = r == 0 && wt == 0 && rr >= 0 && inside_bad == 0 && guard_bad == 0 && kmsg_ok && kdelta == 0;

	S("   walker #");
	D(index);
	S(" kern");
	D(c->kern);
	S(" w=");
	D(c->w);
	S(" h=");
	D(c->h);
	S(" pitch=");
	D(c->pitch);
	S(c->rgba ? " fmt=rgba" : " fmt=r8");
	S(" colour=");
	H(c->colour);
	S(c->kalt ? " kalt" : "");
	S(" mocs=");
	D(c->mocs);
	S(c->control ? " CONTROL" : "");
	S(c->guard ? " guard" : "");
	S(c->fault ? " FAULT" : "");
	S(c->badlen ? " BADLEN" : "");
	NL();
	S("     execbuf ");
	RC(r);
	S(", wait ");
	RC(wt);
	S(", readback ");
	RC(rr);
	NL();
	S("     inside: bytes != colour ");
	D(inside_bad);
	S(", colour hits in pitch padding ");
	D(pad_hits);
	S(", colour hits (control) ");
	D(written);
	S(", guard bytes changed ");
	D(guard_bad);
	S(", kernel log lines new ");
	D(kdelta);
	NL();
	if (kdelta && klast_n) {
		S("     kernel: ");
		put(klast, klast_n);
		NL();
	}
	S("SUMMARY kern");
	D(c->kern);
	S(" ");
	D(c->w);
	S("x");
	D(c->h);
	S(" pitch ");
	D(c->pitch);
	S(c->rgba ? " rgba" : " r8");
	S(" colour ");
	H(c->colour);
	S(" kalt=");
	D(c->kalt);
	S(" mocs=");
	D(c->mocs);
	S(" control=");
	D(c->control);
	S(" guard=");
	D(c->guard);
	S(" badlen=");
	D(c->badlen);
	S(" fault=");
	D(c->fault);
	S(" | not_colour ");
	D(inside_bad);
	S(" pad_hits ");
	D(pad_hits);
	S(" colour_when_control ");
	D(c->control ? written : 0);
	S(" guard_bad ");
	D(guard_bad);
	S(" kmsg ");
	D(kdelta);
	S(" kmsg_readable ");
	D(kmsg_ok);
	S(" kmsg_errno ");
	D(kmsg_err);
	S(" execbuf_errno ");
	D(r < 0 ? -r : 0);
	S(" wait_errno ");
	D(wt < 0 ? -wt : 0);
	S(pass ? " RESULT PASS\n" : " RESULT FAIL\n");
	flush();

	gem_close(th);
	gem_close(sh);
	gem_close(g1);
	gem_close(g2);
	return pass;
}

/* ---- entry ----------------------------------------------------------------- */

static void cfg_default(struct cfg *c)
{
	memset(c, 0, sizeof *c);
	c->kern = 9;
	c->w = 256;
	c->h = 16;
	c->pitch = 0;
	c->colour = 0xa5;
	c->mocs = 1;
	c->ggtt = 1;
	c->repeat = 1;
}

static int walker_loop(struct cfg *c)
{
	int ok = 1;

	if (!c->pitch)
		c->pitch = (c->w + 63) & ~63u;
	for (u32 i = 0; i < c->repeat; i++)
		ok &= walker_once(c, i);
	return ok;
}

static i32 probe_main(i32 argc, char **argv)
{
	struct cfg c;
	int facts_only = 0, walker = 0, b_only = 0, dump_only = 0;
	int allow7 = 0, untested = 0, base_given = 0, kern_given = 0;
	int f, ok;

	cfg_default(&c);
	{
		const struct { const char *name; void *slot; int is_u32; } bool_opts[] = {
			{"--facts", &facts_only, 0},
			{"--walker", &walker, 0},
			{"--b-only", &b_only, 0},
			{"--dump", &dump_only, 0},
			{"--allow-gen7", &allow7, 0},
			{"--allow-untested", &untested, 0},
			{"--rgba", &c.rgba, 1},
			{"--kalt", &c.kalt, 1},
			{"--control", &c.control, 1},
			{"--guard", &c.guard, 1},
			{"--fault", &c.fault, 1},
			{"--badlen", &c.badlen, 1},
			{"--padmask", &c.padmask, 1},
			{"--secure", &exec_secure, 0},
			{"--mi-store", &mi_store7, 0},
		};
		const struct { const char *name; u32 *u32_slot; u64 *u64_slot; int *given; } num_opts[] = {
			{"--w", &c.w, 0, 0},
			{"--h", &c.h, 0, 0},
			{"--pitch", &c.pitch, 0, 0},
			{"--colour", &c.colour, 0, 0},
			{"--mocs", &c.mocs, 0, 0},
			{"--ggtt", &c.ggtt, 0, 0},
			{"--repeat", &c.repeat, 0, 0},
			{"--kern", &c.kern, 0, &kern_given},
			{"--base", 0, &addr_bias, &base_given},
		};

		for (i32 i = 1; i < argc; i++) {
			const char *a = argv[i];
			int matched = 0;

			for (u32 k = 0; k < COUNT(bool_opts); k++) {
				if (streq(a, bool_opts[k].name)) {
					if (bool_opts[k].is_u32)
						*(u32 *)bool_opts[k].slot = 1;
					else
						*(int *)bool_opts[k].slot = 1;
					matched = 1;
					break;
				}
			}
			if (matched)
				continue;
			for (u32 k = 0; k < COUNT(num_opts); k++) {
				if (streq(a, num_opts[k].name)) {
					u64 parsed;

					if (i + 1 >= argc) {
						S("missing value for ");
						S(a);
						NL();
						return 2;
					}
					parsed = parse_num(argv[++i]);
					if (num_opts[k].u32_slot)
						*num_opts[k].u32_slot = (u32)parsed;
					else
						*num_opts[k].u64_slot = parsed;
					if (num_opts[k].given)
						*num_opts[k].given = 1;
					matched = 1;
					break;
				}
			}
			if (!matched) {
				S("unknown argument: ");
				S(a);
				NL();
				return 2;
			}
		}
	}
	if (c.kern != 7 && c.kern != 75 && c.kern != 8 && c.kern != 9 && c.kern != 11 && c.kern != 12) {
		S("--kern must be 7, 75, 8, 9, 11 or 12\n");
		return 2;
	}
	if (c.w == 0 || c.h == 0 || c.w > 16384 || c.h > 16384 || (c.rgba && (c.w & 3))) {
		S("bad --w/--h (rgba needs w % 4 == 0)\n");
		return 2;
	}
	if (c.repeat == 0) {
		S("--repeat must be at least 1 (0 would pass without running anything)\n");
		return 2;
	}
	if (c.colour > 255) {
		S("--colour must be a byte (0..255)\n");
		return 2;
	}
	if (addr_bias & 0xfffull) {
		S("--base must be 4 KiB aligned (surfaces need page-aligned bases)\n");
		return 2;
	}
	if (addr_bias > 0xF0000000ull) {
		/* gen7 surface and store addresses are 32-bit: keep every pinned object below 4 GiB */
		S("--base is too high (keep it below 0xF0000000)\n");
		return 2;
	}

	if (dump_only) {
		if (!c.pitch)
			c.pitch = (c.w + 63) & ~63u;
		/* the dump shows the addresses a real gen7-family run would use */
		if (path_of(c.kern) == 7 && !addr_bias && !base_given)
			addr_bias = 0x10000000ull;
		build(&c);
		S("batch dwords, kern");
		D(c.kern);
		S(" path gen");
		D(path_of(c.kern));
		S(", w=");
		D(c.w);
		S(" h=");
		D(c.h);
		S(" pitch=");
		D(c.pitch);
		S(c.control ? " control" : "");
		NL();
		{
			u32 words = path_of(c.kern) == 7 ? 40u : 56u;

			for (u32 i = 0; i < words; i++) {
				u32 v = img[4 * i] | ((u32)img[4 * i + 1] << 8) | ((u32)img[4 * i + 2] << 16) |
					((u32)img[4 * i + 3] << 24);
				H(v);
				NL();
			}
		}
		S("surface state dwords (binding table entry 0 points here):");
		NL();
		for (u32 i = 0; i < (path_of(c.kern) == 7 ? 8u : 16u); i++) {
			u32 o = ss_o + 4 * i;
			u32 v = img[o] | ((u32)img[o + 1] << 8) | ((u32)img[o + 2] << 16) | ((u32)img[o + 3] << 24);
			H(v);
			NL();
		}
		return 0;
	}

	sys3(SYS_ALARM, 30, 0, 0); /* a hang ends the probe, not the machine */

	fd = sys6(SYS_OPENAT, -100, (i64)"/dev/dri/renderD128", 2 | 02000000, 0, 0, 0);
	S("1 facts: open /dev/dri/renderD128 ");
	RC(fd);
	NL();
	if (fd < 0)
		return 2;

	f = facts();
	if (f == 2) {
		S("RESULT facts: REFUSED (not i915)\n");
		flush();
		return 2;
	}
	if (f) {
		S("RESULT facts: FAIL\n");
		flush();
		return 1;
	}
	S("RESULT facts: PASS\n");
	flush();
	if (facts_only)
		return 0;

	/* Compute was asked for and is not run: exit 3, never 0, so a gated run cannot
	 * read as a pass. */
	if (chip_kern == 0 || chip_kern == 99) {
		S("RESULT compute: NOT RUN (no compute path for this part: ");
		S(kern_name(chip_kern));
		S(")\n");
		flush();
		return 3;
	}
	if (kern_given && c.kern != chip_kern) {
		S("--kern ");
		D(c.kern);
		S(" does not match this device (class ");
		D(chip_kern);
		S(")\n");
		return 2;
	}
	c.kern = chip_kern;
	if (path_of(c.kern) == 7 && !allow7) {
		S("RESULT compute: NOT RUN (gen7 family, Ivy Bridge and Haswell, needs --allow-gen7)\n");
		flush();
		return 3;
	}
	/* gen7 shares the GGTT with the display; the low pinned range can be taken
	 * (ENOSPC at 0x800000 on intel2), so move the pinned range up unless --base */
	if (path_of(c.kern) == 7 && !addr_bias && !base_given)
		addr_bias = 0x10000000ull;
	if ((c.kern == 8 || c.kern == 11 || c.kern == 12) && !untested) {
		S("RESULT compute: NOT RUN (gen8/11/12 are not device-tested here; needs --allow-untested)\n");
		flush();
		return 3;
	}

	if (b_only) {
		ok = walker_loop(&c);
		S(ok ? "RESULT walker: PASS\n" : "RESULT walker: FAIL\n");
		flush();
		return ok ? 0 : 1;
	}

	ok = step_a(path_of(c.kern), c.ggtt) && step_userptr(path_of(c.kern), c.ggtt);
	if (!ok)
		return 1;
	if (!walker)
		return 0;
	ok = walker_loop(&c);
	S(ok ? "RESULT step b: PASS\n" : "RESULT step b: FAIL\n");
	flush();
	return ok ? 0 : 1;
}

__attribute__((noreturn, used)) void probe_entry(u64 *sp)
{
	i32 argc = (i32)sp[0];
	char **argv = (char **)(sp + 1);
	i32 rc = probe_main(argc, argv);

	ctx_destroy();
	flush();
	sys3(SYS_EXIT_GROUP, rc, 0, 0);
	for (;;)
		;
}
