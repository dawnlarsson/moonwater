/*
 * Freestanding LD_PRELOAD shim for Alpine Chromium under musl 1.2.6+.
 *
 * musl 1.2.6 implements pwrite() and pwritev() via SYS_pwritev2 with
 * RWF_NOAPPEND (0x20), falling back to SYS_pwrite64 only on ENOSYS or
 * EOPNOTSUPP.  Chromium's seccomp-bpf sandbox allows __NR_pwrite64 and
 * __NR_writev, and traps __NR_pwritev2 (0x148 on x86_64, 287 on aarch64)
 * with SIGSYS.  Routing pwrite/pwrite64/pwritev/pwritev64/pwritev2 to
 * __NR_pwrite64 (and __NR_writev when pwritev2 gets offset == -1) keeps
 * Chromium's namespace and seccomp-bpf sandboxes enabled.
 *
 * Built into the split byte arrays in src/bowl.c with:
 *   $CC -Os -fPIC -fno-plt -shared -nostdlib -nostartfiles \
 *       -fno-stack-protector -fno-asynchronous-unwind-tables \
 *       -fno-unwind-tables -fno-ident \
 *       -Wl,-z,now,-z,noseparate-code,-Bsymbolic,--hash-style=both,--build-id=none,-s
 */

struct iovec {
	const void *iov_base;
	unsigned long iov_len;
};

extern int *__errno_location(void);

static inline long sys_pwrite64(int fd, const void *buf, unsigned long count,
				long long offset)
{
#if defined(__x86_64__)
	register long r10 __asm__("r10") = offset;
	long ret;

	__asm__ volatile("syscall"
			 : "=a"(ret)
			 : "a"(18L), "D"(fd), "S"(buf), "d"(count), "r"(r10)
			 : "rcx", "r11", "memory");
	return ret;
#elif defined(__aarch64__)
	register long x0 __asm__("x0") = fd;
	register long x1 __asm__("x1") = (long)buf;
	register long x2 __asm__("x2") = (long)count;
	register long x3 __asm__("x3") = offset;
	register long x8 __asm__("x8") = 68;

	__asm__ volatile("svc #0"
			 : "+r"(x0)
			 : "r"(x1), "r"(x2), "r"(x3), "r"(x8)
			 : "memory");
	return x0;
#elif defined(__riscv)
	register long a0 __asm__("a0") = fd;
	register long a1 __asm__("a1") = (long)buf;
	register long a2 __asm__("a2") = (long)count;
	register long a3 __asm__("a3") = offset;
	register long a7 __asm__("a7") = 68;

	__asm__ volatile("ecall"
			 : "+r"(a0)
			 : "r"(a1), "r"(a2), "r"(a3), "r"(a7)
			 : "memory");
	return a0;
#else
#error unsupported architecture
#endif
}

static inline long sys_writev(int fd, const struct iovec *iov, int iovcnt)
{
#if defined(__x86_64__)
	long ret;

	__asm__ volatile("syscall"
			 : "=a"(ret)
			 : "a"(20L), "D"(fd), "S"(iov), "d"(iovcnt)
			 : "rcx", "r11", "memory");
	return ret;
#elif defined(__aarch64__)
	register long x0 __asm__("x0") = fd;
	register long x1 __asm__("x1") = (long)iov;
	register long x2 __asm__("x2") = iovcnt;
	register long x8 __asm__("x8") = 66;

	__asm__ volatile("svc #0"
			 : "+r"(x0)
			 : "r"(x1), "r"(x2), "r"(x8)
			 : "memory");
	return x0;
#elif defined(__riscv)
	register long a0 __asm__("a0") = fd;
	register long a1 __asm__("a1") = (long)iov;
	register long a2 __asm__("a2") = iovcnt;
	register long a7 __asm__("a7") = 66;

	__asm__ volatile("ecall"
			 : "+r"(a0)
			 : "r"(a1), "r"(a2), "r"(a7)
			 : "memory");
	return a0;
#else
#error unsupported architecture
#endif
}

static long set_err(long r)
{
	if (r < 0 && r >= -4095) {
		*__errno_location() = (int)(-r);
		return -1;
	}
	return r;
}

long pwrite(int fd, const void *buf, unsigned long count, long long offset)
{
	return set_err(sys_pwrite64(fd, buf, count, offset));
}

long pwrite64(int fd, const void *buf, unsigned long count, long long offset)
	__attribute__((alias("pwrite")));

static long do_pwritev(int fd, const struct iovec *iov, int iovcnt,
		       long long offset)
{
	if (iovcnt == 0)
		return 0;
	if (iovcnt < 0)
		return set_err(-22);

	long total = 0;
	int did = 0;

	for (int i = 0; i < iovcnt; i++) {
		if (iov[i].iov_len == 0 && (did || i + 1 < iovcnt))
			continue;
		long r = sys_pwrite64(fd, iov[i].iov_base, iov[i].iov_len, offset);

		did = 1;
		if (r < 0)
			return total > 0 ? total : set_err(r);
		total += r;
		offset += r;
		if ((unsigned long)r < iov[i].iov_len)
			return total;
	}
	return total;
}

long pwritev(int fd, const struct iovec *iov, int iovcnt, long long offset)
{
	return do_pwritev(fd, iov, iovcnt, offset);
}

long pwritev64(int fd, const struct iovec *iov, int iovcnt, long long offset)
	__attribute__((alias("pwritev")));

long pwritev2(int fd, const struct iovec *iov, int iovcnt, long long offset,
	      int flags)
{
	if ((flags & ~0x20) != 0)
		return set_err(-95);
	if (offset == -1)
		return set_err(sys_writev(fd, iov, iovcnt));
	return do_pwritev(fd, iov, iovcnt, offset);
}
