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

## 2026-09-29 · finding room in a directory block (`ext4_find_dest_de`)

Every create, rename and mkdir walks the directory block it lands in, entry by entry,
for a gap that fits. `kernel/kernel.c` carries that walk as assembly on x86_64, arm64
and riscv64, packed two to four instructions a line like `lib.c`.

- **Microbenchmark, native, Zen 5:** a full 4 KiB block of 134 entries, a name that is
  not there: 590 ns in the C, 350 ns in the port (1.7x). The loop is a chain of one
  record length per entry; the gain is the two calls and nine arguments the C makes
  for each entry.
- **In a guest, same kernel with and without it:** create + unlink 2989 -> 2909 ns
  (-2.7%), rename -4.0%, mkdir + rmdir -1.3%; every other call within ±2%.

**What it does not show.** The guest figures are inside this method's ±4% noise floor,
so they show a small gain in one direction, not a size. Nothing here says a create is
faster by 1.7x; the walk is a small share of a create. The arm64 and riscv64 ports are
checked against the kernel's own C by a differential harness under qemu-user and both
kernels build and link, but neither has been booted, and neither has been timed.

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

---

## 2026-09-30 · listing a tmpfs directory (`offset_iterate_dir`)

`getdents` on tmpfs, and on every directory that keeps its entries in offset order
(the live image's root is one), walks the entries one at a time. Each step takes the
parent's lock to find the next sibling, the sibling's own lock to take a reference
on it, and a compare-and-swap to give the previous entry's reference back: about
two locked operations and one reference drop per name; then it calls `filldir64`,
which checks the name for a slash, opens a user-access window (stac, the stores,
clac) and closes it, per name. The directory cannot change while it is being listed
(the caller holds its `i_rwsem` shared, and every create, unlink and rename takes
it exclusive), so `kernel/kernel.c` reads the entries in batches: one lock, up to
sixteen names copied to the stack, the lock let go. One reference is kept from
batch to batch, as the C keeps one on the entry it stands at. On x86_64, when the
caller is `getdents64`, the batch is then made into one image of dirents in the
frame, checked with `filldir64`'s own checks in its own order, and written with two
`_copy_to_user` calls; anything that does not go through that way is handed to
`filldir64` a name at a time, as before.

- **In a guest, same kernel, four builds booted in turn:** one `getdents64` pass
  over a thousand names, minimum over 1200 bursts, three rounds. The C 20.2 to 20.7
  microseconds; batches 16.5 to 16.7 (-18%); with the directory type written out
  15.5 to 15.8 (-24%); with the dirent image 9.1 to 9.3 (-55%, 2.2x, 20.4 -> 9.1 ns
  a name).
- **Why, in a guest profile of the batches build:** `filldir64` 40% of the cycles,
  its `memchr` 18%, `fs_umode_to_dtype` 14%, the walk 24%; the locked operations,
  2.1 spinlocks and 1.0 `dput` a name in the C against 0.39 and 0.14, were the
  first 18% and under 2% after.
- **Checked against the kernel's own C** (`test/differential.py --harness
  kernel_ports`, x86_64 native, arm64 and riscv64 under qemu-user): the oracle is cut
  from `fs/libfs.c`, `fs/readdir.c` and `fs/fs_dirent.c` of the pinned tarball at run
  time, names are planted at the end of a page with an unmapped page after, and the
  buffer the dirents go to ends wherever the test says. Return values, `ctx->pos`,
  `ctx->count`, the callback's `current_dir`, `prev_reclen` and `error`, every field
  of every dirent, and the reference count of every entry agree at buffers that end
  in the middle of a dirent, counts that run out, signals pending and names with a
  slash; four planted bugs in the x86_64 image code (a record length, an error
  code, the signal rule, the slash test) were each caught. In the guest, both
  kernels list the same 484-entry directory byte for byte at ten buffer sizes,
  resumed every third entry, and return the same lengths and the same dirents with
  the buffer ending 1 to 600 bytes before an unmapped page.

**What it does not show.** 55% is of one call over a warm directory of short names in
a VM, not of a program's run time, and it says nothing about directories on disk. One
thing is different from the C and it is a choice: the zeros after a name's NUL, up to
the next eight bytes of a dirent, are written where the C leaves whatever the buffer
held; the kernel promises nothing about those bytes. The arm64 and riscv64 bodies
have the batches and the written-out type, not the image; they are checked against
the C under qemu-user and have not been booted or timed.

**A measurement note.** The first runs of this, on a box that had 40 forgotten
guests from earlier sessions running, read 60 microseconds a pass and showed no
change. With them killed, the same images read 20.6 and 16.9. A timing taken under
load is a timing of the load.
