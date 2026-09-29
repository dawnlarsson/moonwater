# Field notes

Measured results kept for the Moonwater page, one entry per finding, written as the
page writes them: what was run, against what, under what conditions. Each entry says
what it does **not** show. A number here is a number somebody reran; a claim that
could not be reproduced is written down as one that could not.

Conditions unless an entry says otherwise: AMD Ryzen 9 9950X (Zen 5), a shared
machine with a load average near 100, the Moonwater kernel booted under KVM with two
vCPUs, an ext4 made the way the installer makes one (`mkfs.ext4 -b 4096 -I 256
-i 16384 -m 0`, metadata checksums on, half_md4 directory hash). Times are the
*minimum* over hundreds of short bursts inside the guest, because a shared machine
has no quiet cores and the minimum is the burst nobody interrupted; the same image
measured twice agrees to about 4%, so that is the smallest difference read as real.

---

## 2026-09-29 · ext4 directory reads

`ls`, `find`, a shell glob, `readdir`: every one is `getdents64`, and on ext4 every
`getdents64` hashes every name it returns. A directory that fits in one block is read
in hash order like a large one, the hash is stored nowhere, and `ext4fs_dirhash`
runs over each entry each time the directory is read. Profiled in the guest, over a
directory of a thousand 35-character names, the hash and its string packer were
**47% of the syscall**.

The default hash (half_md4) is twenty-four rounds in which every step needs the one
before, so one name cannot go faster than that chain, and the kernel's C is already
at it: a word-at-a-time rewrite of the packer, checked against it, measured level.
Sixteen names do not need each other. `hash_half_md4_wide` runs sixteen side by side
(AVX2 on x86_64, Advanced SIMD on arm64, RVV on riscv64), and the readdir walk that
asks for the hashes, `htree_dirblock_to_tree`, is rewritten from the top in assembly
(`kernel/kernel.c`, x86_64 for now) so the batch and the entry checks run in
registers with no C in the path.

**One `getdents64` pass, guest, one directory (microseconds):**

| directory                     | before | after | same kernel, wide hash off | change |
|-------------------------------|-------:|------:|---------------------------:|-------:|
| 1000 names, 35 characters     |   92.5 |  52.5 |                       91.1 | −43.2% |
| 1000 names, 10 characters     |   65.9 |  42.4 |                       65.4 | −35.7% |
| 5000 names                    |  387.3 | 261.9 |                      374.9 | −32.4% |
| 100 names                     |   6.53 |  4.21 |                       6.33 | −35.6% |
| 50 names                      |   3.40 |  2.30 |                       3.34 | −32.2% |
| 24 names                      |   1.80 |  1.32 |                       1.77 | −26.8% |
| 20 names                      |   1.55 |  1.18 |                       1.54 | −23.5% |
| 16 names                      |   1.30 |  1.05 |                       1.30 | −19.0% |
| 15 names                      |   1.23 |  1.02 |                       1.24 | −17.3% |
| 8 names                       |  0.805 | 0.821 |                      0.813 |  +1.9% |

*Before* is the Moonwater kernel with the C function, not stock Linux. The gain grows
with the directory and is still 19% at sixteen names; below about a dozen entries
the batch is not used at all, and the +1.9% at eight is that path, inside the noise.

The fourth column is the control that says what the gain is. It is the *same image
as "after"*, booted with `moonwater.dirhash_simd=0`, which turns the wide hash off and
leaves everything else this change did. It lands on "before" to within 2% in every
row. `htree_dirblock_to_tree` was rewritten from the top in assembly for this
(`kernel/kernel.c`), and that rewrite by itself is **level with the C it replaced**:
written out by hand the walk comes to what GCC made of it. None of the gain is
"assembly beats the compiler". All of it is sixteen names hashed at once, which a
compiler cannot make out of a loop over one name.

**The hash itself, per name** (the same routine, timed alone; 1 block of names of
12 characters / of 35):

| machine                    | kernel's C | `hash_half_md4_wide` | ratio       |
|----------------------------|-----------:|---------------------:|------------:|
| Zen 5, x86_64, AVX2        | 96 / 190 ticks | 19 / 38 ticks     | 5.0× / 5.0× |
| Apple M-series, NEON       | 22.6 / 49.1 ns | 7.6 / 14.9 ns     | 3.0× / 3.3× |
| riscv64, RVV               | not timed — no hardware here                       |

**It is the same hash, and the same directory listing.**

- The routine matches the kernel's own function bit for bit: 33.8 million names on
  x86_64, 2.26 million on arm64 (on the Mac, and under qemu-user), 168 thousand on
  riscv64 at vector lengths 128 and 512 (qemu-user). Names of 1 to 255 bytes, every
  byte value, both `char` signednesses, seeded and unseeded. The oracle is Linux's
  own `fs/ext4/hash.c`, taken from the pinned kernel tarball each time the check runs,
  so it is Linux's answer and not ours; a copy with one constant changed is required
  to disagree, and does.
- The same ext4 image read by the stock Arch kernel on the host and by the Moonwater
  guest returns **byte-identical directory streams**: 69 digests, 23 directories ×
  3 buffer sizes, covering hash order, resuming a listing from a hash, names with
  non-ASCII and arbitrary bytes, names past 200 characters, and directories with
  deleted entries. The listing order is the hash order, so this can only be true if
  every hash is.

**What this does not show.** It is the *directory read*. A `stat`, an `open` or a
`read` costs what it did (below). It is a guest under KVM on one machine. On arm64
and riscv64 the hash routine exists and is checked, but the kernel's readdir there is
still the C function, so those two see none of this yet. Directories of fewer than
about a dozen entries do not change.

---

## 2026-09-29 · what a file-system syscall costs, and what does not help

Guest, warm cache, one file at a depth of five path components, ns per call:

| call                          | ns    |
|-------------------------------|------:|
| `lseek`                       |    44 |
| `fstat`                       |    69 |
| `pread` 64 B / 4 KiB          | 72 / 88 |
| `pwrite` 4 KiB                |   142 |
| `stat`                        |   154 |
| `open` + `close`              |   232 |
| create + unlink               |  3300 |
| `mkdir` + `rmdir`             |  6100 |
| two `rename`s                 |  4500 |

Profiling `open`, `stat`, `read` and `write` in this kernel finds no string or memory
loop to replace: the time is in the dentry hash chain (`__d_lookup_rcu`, 11% of
`open`+`close`), the path walk (8%), reference counting and slab allocation. The
copy to user space is `rep movsb`, which is already the floor.

**Not a claim, and why.**

- *"Faster than Arch's kernel."* The same benchmark on the stock Arch kernel on the
  host reads 2× slower on `stat`, but that kernel is built with BPF LSM, hardened
  usercopy and audit hooks this one is not, and it ran on bare metal against a
  guest. That is configuration, not assembly, and it says nothing about our code.
- *"Our string and memory routines speed up file-system calls."* Measured the honest
  way, on the same tree built twice (`MOONWATER_STOCK=1` keeps the kernel's own
  `memcpy`, `memchr` and the rest), every call is within ±5% either way, including
  `lseek` and `fstat`, which touch no strings at all. That is the noise floor, so the
  answer is *no measurable effect*, in either direction. (`getdents` read 5% faster,
  which is about the size of what its profile says those routines are worth, and is
  at the edge of what this method can see.)
- Anything about riscv64 speed.

---

## 2026-09-29 · kernel 7.2.8

The pinned kernel moved from 7.2.6 to 7.2.8, the latest stable (7.3 is at rc5). The
tarball is checked against a signature pinned in `src/build/build.c`, and the
signature verifies against Greg Kroah-Hartman's key. Every edit this tree makes to
Linux's source applied to 7.2.8 unchanged (`fs/ext4/namei.c`, the file the directory
port edits, is byte-identical between the two releases), and the image booted and
listed the same directories the same way. On the calls the port does not touch
(`open`, `stat`, `read`, `write`, `create`, `unlink`, `rename`) the 7.2.8 image is
1% to 5% faster than the 7.2.6 one, uniformly; the two differ in more than the
version, so that is a drift to watch and not something to claim.
