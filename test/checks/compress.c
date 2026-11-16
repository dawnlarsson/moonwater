/*
        test/checks/compress.c: tar, zstd, gzip and xz.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_tar
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#define TAR_PARSE_ONLY
#include "../../src/sh/tar.c"
#undef TAR_PARSE_ONLY
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static fn checksums(void)
{
        p8 block[TAR_BLOCK];
        p8 flipped[TAR_BLOCK];
        positive at;

        memory_fill(block, 0, TAR_BLOCK);
        check("a zero block sums to zero", tar_header_zero(block));

        block[156] = '0';
        memory_copy(block + 257, "ustar", 5);
        block[263] = '0';
        block[264] = '0';
        tar_header_put_checksum(block);
        check("a ustar header checksums with memory_sum_bytes",
              tar_header_ok(block) && !tar_header_zero(block));

        for (at = 0; at < TAR_BLOCK; at++)
        {
                if (at >= TAR_CHKSUM && at < TAR_CHKSUM + TAR_CHKSUM_WIDTH)
                        continue;
                memory_copy(flipped, block, TAR_BLOCK);
                flipped[at] ^= 1;
                check("any non-checksum header byte is covered by the checksum",
                      !tar_header_ok(flipped));
        }
}

static fn tar_field_put_base256(p8 address_to field, positive width, p64 value)
{
        positive at;

        memory_fill(field, 0, width);
        for (at = width; at; at--)
        {
                field[at - 1] = (p8)value;
                value >>= 8;
        }

        field[0] |= 0x80;
}

static fn tar_field_put(p8 address_to field, positive width, p64 value)
{
        positive bits = width > 1 ? 3 * (width - 1) : 0;

        if (bits && bits < 64 && value <= (((p64)1 << bits) - 1))
                tar_field_put_octal(field, width, value);
        else
                tar_field_put_base256(field, width, value);
}

static fn fields(void)
{
        static p64 values[] = {0, 1, 7, 8, 0777, 0x7f, 0x80, 0xff, TAR_BLOCK - 1,
                               TAR_BLOCK, TAR_BLOCK + 1, 0xffff, 0x7fffffff};
        static positive widths[] = {8, 12};
        p8 field[12];
        p64 got;
        p64 size;
        positive width;
        positive at;

        for (width = 0; width < array_count(widths); width++)
        for (at = 0; at < array_count(values); at++)
        {
                memory_fill(field, 0xa5, sizeof(field));
                tar_field_put(field, widths[width], values[at]);
                check("ustar numeric fields round-trip every width",
                      tar_field_value(field, widths[width], address_of got) &&
                          got == values[at]);
        }

        memory_fill(field, ' ', sizeof(field));
        check("a blank field is zero",
              tar_field_value(field, 8, address_of got) && got == 0);

        for (size = 0; size <= TAR_BLOCK * 3 + 1; size++)
        {
                positive padded = tar_padded(size);
                positive want = size
                    ? (positive)((size + TAR_BLOCK - 1) & ~(p64)(TAR_BLOCK - 1))
                    : 0;

                check("tar padding is a whole number of blocks", padded == want);
        }
}

static fn paths(void)
{
        static string_address piece[] = {"a", "b", ".", ".."};
        static string_address prefixes[] = {"", "usr", "usr/local"};
        static string_address names[] = {"a", "bin/jq", "x"};
        p8 path[48];
        p8 into[64];
        p8 joined[64];
        positive n;
        positive a;
        positive b;
        positive c;
        positive strip;
        positive left;
        positive slash;
        positive abs;
        positive prefix;
        positive name;

        for (n = 1; n <= 3; n++)
        for (a = 0; a < array_count(piece); a++)
        for (b = 0; b < (n > 1 ? array_count(piece) : 1); b++)
        for (c = 0; c < (n > 2 ? array_count(piece) : 1); c++)
        for (slash = 0; slash < 2; slash++)
        for (abs = 0; abs < 2; abs++)
        for (strip = 0; strip <= n; strip++)
        {
                string_address parts[3];
                positive used = 0;
                positive kept = 0;
                positive at;
                p8 climb = false;
                p8 ok;
                p8 expect;

                parts[0] = piece[a];
                parts[1] = piece[b];
                parts[2] = piece[c];
                if (slash)
                        path[used++] = '/';
                for (at = 0; at < n; at++)
                {
                        positive length = string_length(parts[at]);

                        if (at)
                                path[used++] = '/';
                        memory_copy(path + used, parts[at], length);
                        used += length;
                }
                path[used] = 0;

                /* GNU counts a "." component toward --strip-components, as it
                   counts any other. What is left after the strip is the name:
                   an absolute `/` alone is the archive root and is kept, a
                   relative path that reduces to nothing is not, and a strip
                   count that outlasts the components refuses the member. */
                left = strip;
                kept = abs && slash ? 1 : 0;
                for (at = 0; at < n; at++)
                {
                        if (string_equals(parts[at], ".."))
                                climb = true;
                        else if (left)
                                left--;
                        else if (!string_equals(parts[at], "."))
                                kept++;
                }
                expect = !climb && kept && !left;
                ok = tar_safe_path(path, strip, abs, false, into, sizeof(into), null);
                check("tar path component grammar", ok == expect);
                if (ok && abs && slash)
                        check("an absolute path keeps its leading slash",
                              into[0] == '/');
                if (ok && !abs)
                        check("a relative path has no leading slash",
                              into[0] != '/');
        }

        for (prefix = 0; prefix < array_count(prefixes); prefix++)
        for (name = 0; name < array_count(names); name++)
        {
                p8 head[TAR_PREFIX + 1];
                p8 leaf[TAR_NAME + 1];
                p8 ok;
                p8 expect = prefixes[prefix][0] || names[name][0];

                memory_fill(head, 0, sizeof(head));
                memory_fill(leaf, 0, sizeof(leaf));
                memory_copy(head, prefixes[prefix],
                            string_length(prefixes[prefix]));
                memory_copy(leaf, names[name], string_length(names[name]));
                ok = tar_join_name(head, leaf, joined, sizeof(joined));
                check("ustar prefix and name join", ok == expect &&
                      (!ok || (prefixes[prefix][0]
                                   ? path_join(into, sizeof(into),
                                               prefixes[prefix],
                                               names[name]) &&
                                         string_equals(joined, into)
                                   : string_equals(joined, names[name]))));
        }
}

static fn packs(void)
{
        static const struct
        {
                p8 magic[8];
                positive n;
                p8 pack;
                p8 packed;
        } magics[] = {
            {{0x1f, 0x8b}, 2, TAR_PACK_GZIP, true},
            {{0xfd, 0x37, 0x7a, 0x58, 0x5a, 0}, 6, TAR_PACK_XZ, true},
            {{0x28, 0xb5, 0x2f, 0xfd}, 4, TAR_PACK_ZSTD, true},
            {{0x1f, 0x9d}, 2, TAR_PACK_NONE, true},
            {{'u', 's', 't', 'a', 'r'}, 5, TAR_PACK_NONE, false},
        };
        static string_address stems[] = {"", "a", "a.tar", "archive"};
        static const struct
        {
                string_address suffix;
                p8 pack;
                p8 packed;
        } suffixes[] = {
            {".gz", TAR_PACK_GZIP, true},
            {".tgz", TAR_PACK_GZIP, true},
            {".xz", TAR_PACK_XZ, true},
            {".txz", TAR_PACK_XZ, true},
            {".zst", TAR_PACK_ZSTD, true},
            {".tzst", TAR_PACK_ZSTD, true},
            {".bz2", TAR_PACK_NONE, true},
            {".tbz2", TAR_PACK_NONE, true},
            {".tbz", TAR_PACK_NONE, true},
            {".Z", TAR_PACK_NONE, true},
            {".tar", TAR_PACK_NONE, false},
            {"", TAR_PACK_NONE, false},
        };
        p8 held[16];
        p8 name[80];
        p8 digit;
        positive at;
        positive stem;
        positive suffix;

        check("empty magic is not a packed stream",
              tar_pack_from_magic(held, 0) == TAR_PACK_NONE);

        for (at = 0; at < array_count(magics); at++)
        {
                p8 sniffed = tar_pack_from_magic(magics[at].magic, magics[at].n);

                if (magics[at].pack != TAR_PACK_NONE)
                        check("supported tar compression magics sniff as their pack",
                              sniffed == magics[at].pack);
                else if (magics[at].packed)
                        check("unsupported tar compression is still named as compression",
                              sniffed != TAR_PACK_NONE);
                else
                        check("uncompressed tar is not a packed stream",
                              sniffed == TAR_PACK_NONE);

                if (magics[at].n > 1)
                {
                        p8 short_magic = tar_pack_from_magic(magics[at].magic,
                                                             magics[at].n - 1);

                        check("a truncated compression magic is not that pack",
                              short_magic != magics[at].pack ||
                                  magics[at].pack == TAR_PACK_NONE);
                }

                memory_copy(held, magics[at].magic, magics[at].n);
                held[magics[at].n] = 0xff;
                if (magics[at].pack != TAR_PACK_NONE)
                        check("trailing bytes after a magic keep the pack",
                              tar_pack_from_magic(held, magics[at].n + 1) ==
                                  magics[at].pack);

                memory_copy(held, magics[at].magic, magics[at].n);
                held[magics[at].n - 1] ^= 1;
                if (magics[at].packed && magics[at].pack != TAR_PACK_NONE)
                        check("a mutated supported magic is not that pack",
                              tar_pack_from_magic(held, magics[at].n) !=
                                  magics[at].pack);
        }

        for (digit = '0'; digit <= '9'; digit++)
        {
                p8 magic[4] = {'B', 'Z', 'h', digit};

                check("unsupported tar compression is still named as compression",
                      tar_pack_from_magic(magic, 4) != TAR_PACK_NONE);
        }

        for (stem = 0; stem < array_count(stems); stem++)
        for (suffix = 0; suffix < array_count(suffixes); suffix++)
        {
                positive used = string_length(stems[stem]);
                p8 named;

                memory_copy(name, stems[stem], used);
                memory_copy(name + used, suffixes[suffix].suffix,
                            string_length(suffixes[suffix].suffix) + 1);
                named = tar_pack_from_name(name);
                if (suffixes[suffix].pack != TAR_PACK_NONE)
                        check("supported tar compression suffixes agree",
                              named == suffixes[suffix].pack);
                else if (suffixes[suffix].packed)
                        check("unsupported tar compression is still named as compression",
                              named != TAR_PACK_NONE);
                else
                        check("uncompressed tar is not a packed stream",
                              named == TAR_PACK_NONE);
        }
}

static p8 tar_pax_record(p8 address_to into, positive room,
                           positive address_to used, string_address key,
                           const p8 address_to value, positive n)
{
        p8 digits[16];
        positive key_len = string_length(key);
        positive width;

        for (width = 1; width < sizeof(digits); width++)
        {
                positive rest = 1 + key_len + 1 + n + 1;
                positive total = width + rest;
                positive shown = positive_into_base(digits, total, 10, false);

                if (shown != width)
                        continue;
                if (address_to used + total > room)
                        return false;

                memory_copy(into + address_to used, digits, width);
                into[address_to used + width] = ' ';
                memory_copy(into + address_to used + width + 1, key, key_len);
                into[address_to used + width + 1 + key_len] = '=';
                if (n)
                        memory_copy(into + address_to used + width + 2 + key_len,
                                    value, n);
                into[address_to used + total - 1] = '\n';
                address_to used += total;
                return true;
        }

        return false;
}

static fn pax_records(void)
{
        static string_address keys[] = {
            "path", "linkpath", "size", "uid", "gid", "uname", "gname", "atime",
            "ctime", "mtime", "comment", "hdrcharset",
            "SCHILY.xattr.security.capability", "SCHILY.xattr.security.selinux",
            "LIBARCHIVE.xattr.user.foo", "GNU.sparse.major", "GNU.sparse.minor",
            "GNU.sparse.map"};
        static const struct
        {
                p8 bytes[8];
                positive n;
                p8 numeric;
        } values[] = {
            {{0}, 0, true},
            {{'0'}, 1, true},
            {{'1'}, 1, true},
            {{'1', '2'}, 2, true},
            {{'a'}, 1, false},
            {{'/', 'b', 'i', 'n'}, 4, false},
            {{1, 0, 0, 2}, 4, false},
            {{0xff, 0xfe}, 2, false},
            {{'a', '=', 'b'}, 3, false},
            {{'a', ' ', 'b'}, 3, false},
        };
        p8 body[TAR_PATH];
        p8 extra[64];
        positive key;
        positive value;
        positive poke;

        for (key = 0; key < array_count(keys); key++)
        for (value = 0; value < array_count(values); value++)
        {
                positive used = 0;
                positive extra_used = 0;
                p8 size_key = string_equals(keys[key], "size");
                p8 well = !size_key ||
                    (values[value].numeric && values[value].n);

                tar_pax_clear(address_of tar_pax_local);
                check("a pax record encodes its own length",
                      tar_pax_record(body, sizeof(body), address_of used,
                                     keys[key], values[value].bytes,
                                     values[value].n) &&
                          used && body[used - 1] == '\n');
                check("well-formed pax records apply, including NULs in values",
                      tar_pax_apply(address_of tar_pax_local, body, used) == well);
                if (well && string_equals(keys[key], "path"))
                        check("pax path values are kept",
                              tar_pax_local.has_path &&
                                  !memory_compare(tar_pax_local.path,
                                                  values[value].bytes,
                                                  values[value].n) &&
                                  tar_pax_local.path[values[value].n] == 0);
                if (well && string_equals(keys[key], "linkpath"))
                        check("pax linkpath values are kept",
                              tar_pax_local.has_link &&
                                  !memory_compare(tar_pax_local.link,
                                                  values[value].bytes,
                                                  values[value].n));
                if (well && size_key && values[value].n)
                        check("pax size values are kept",
                              tar_pax_local.has_size);

                extra_used = 0;
                check("a following pax record still encodes",
                      tar_pax_record(extra, sizeof(extra), address_of extra_used,
                                     "comment", (const p8 address_to) "z", 1));
                memory_copy(body + used, extra, extra_used);
                tar_pax_clear(address_of tar_pax_local);
                check("pax bodies are a sequence of records",
                      tar_pax_apply(address_of tar_pax_local, body,
                                    used + extra_used) == well);
        }

        for (key = 0; key < array_count(keys); key++)
        {
                positive used = 0;
                p8 good[TAR_PATH];

                tar_pax_record(good, sizeof(good), address_of used, keys[key],
                               (const p8 address_to) "abc", 3);
                for (poke = 0; poke < 5; poke++)
                {
                        p8 bad[TAR_PATH];
                        positive length = used;

                        memory_copy(bad, good, used);
                        if (poke == 0)
                                length = used - 1;
                        else if (poke == 1)
                        {
                                p8 address_to equal = memory_first_of(bad, '=',
                                                                      used);
                                if (equal)
                                        address_to equal = ' ';
                        }
                        else if (poke == 2)
                                bad[0] = '9';
                        else if (poke == 3)
                                bad[0] = '0';
                        else
                                bad[0] = 'x';
                        tar_pax_clear(address_of tar_pax_local);
                        check("a mutated pax record is refused",
                              !tar_pax_apply(address_of tar_pax_local,
                                             bad, length));
                }
        }
}

/*
        Which of a member's three name sources wins.

        A member can carry a pax path record, a GNU long-name member and the
        header's own fields all at once, and they rank in that order: the
        reference reads a pax record over a long name whichever of the two
        the archive wrote first, a local pax record over a global one, and
        the header only once neither pax state said anything.  A link target
        ranks the same three ways.  Every combination is walked because an
        archive carrying two of them is an archive built to be read two ways,
        and the name extracted has to be the one the archiver meant.
*/
static fn name_precedence(void)
{
        p8 block[TAR_BLOCK];
        p8 into[TAR_PATH];
        p8 given[TAR_PATH];
        positive shape;

        string_copy_max_end(given, (string_address) "long", sizeof(given) - 1);

        for (shape = 0; shape < 8; shape++)
        {
                p8 by_local = (shape & 1) != 0;
                p8 by_global = (shape & 2) != 0;
                p8 by_long = (shape & 4) != 0;
                string_address want =
                    by_local ? (string_address) "local"
                    : by_global ? (string_address) "global"
                    : by_long ? (string_address) "long"
                              : (string_address) "header";

                memory_fill(block, 0, sizeof(block));
                memory_copy(block, "header", 6);
                memory_copy(block + 157, "header", 6);
                memory_copy(block + 257, "ustar", 5);
                memory_copy(block + 263, "00", 2);

                tar_pax_clear(address_of tar_pax_local);
                tar_pax_clear(address_of tar_pax_global);
                if (by_local)
                {
                        string_copy_max_end(tar_pax_local.path,
                                            (string_address) "local",
                                            TAR_PATH - 1);
                        string_copy_max_end(tar_pax_local.link,
                                            (string_address) "local",
                                            TAR_PATH - 1);
                        tar_pax_local.has_path = true;
                        tar_pax_local.has_link = true;
                }
                if (by_global)
                {
                        string_copy_max_end(tar_pax_global.path,
                                            (string_address) "global",
                                            TAR_PATH - 1);
                        string_copy_max_end(tar_pax_global.link,
                                            (string_address) "global",
                                            TAR_PATH - 1);
                        tar_pax_global.has_path = true;
                        tar_pax_global.has_link = true;
                }

                into[0] = end;
                check("a pax path outranks a GNU long name, which outranks the header",
                      tar_member_name(block, into, by_long ? given : null) &&
                          string_equals(into, want));

                into[0] = end;
                check("a pax linkpath ranks the same three ways",
                      tar_member_link(block, into, by_long ? given : null) &&
                          string_equals(into, want));
        }

        tar_pax_clear(address_of tar_pax_local);
        tar_pax_clear(address_of tar_pax_global);
}

/*
        How a directory member is spelled: with one trailing slash, unless
        the name is empty, ends in one already, or leaves no room for it.
*/
static fn name_spelling(void)
{
        static string_address names[] = {
            "", "a", "a/", "/a", "a/b", "a/b/", "a//b", "a//b/", "/", "//",
            "abc/def/ghi", "abc/def/ghi/", "dir/sub/", "x/y/z/", "xyz"};
        p8 spelled[TAR_PATH];
        p8 wide[TAR_PATH];
        positive at;

        for (at = 0; at < array_count(names); at++)
        {
                string_address name = names[at];
                positive length = string_length(name);
                p8 made = tar_spell_directory(name, spelled,
                                                sizeof(spelled));

                if (!length || name[length - 1] == '/')
                        check("an empty name, or one already ending in a slash, is left alone",
                              !made);
                else
                        check("a directory member gains exactly one trailing slash",
                              made && !memory_compare(spelled, name, length) &&
                                  spelled[length] == '/' &&
                                  spelled[length + 1] == end);
        }

        memory_fill(wide, 'x', TAR_PATH - 1);
        wide[TAR_PATH - 1] = end;
        check("a name with no room for the slash keeps its plain spelling",
              !tar_spell_directory(wide, spelled, TAR_PATH));
}

/* The digit words od and tar write numbers with, against the digits one at a
   time: every 17-bit octal value, every 16-bit hexadecimal value, and 300,000
   random and edge ones, and the byte reversal they end in
   (shifts on the RV64 floor, a byte swap elsewhere). */
static fn digit_words(void)
{
        static const char digits[] = "0123456789abcdef";
        p64 state = 0x9e3779b97f4a7c15ull;
        p8 octal_whole = true;
        p8 hex_short = true;
        p8 hex_wide = true;
        p8 reversed = true;

        for (p64 value = 0; value < ((p64)1 << 17); value++)
        {
                p64 word = octal_digits_word(value);
                p8 text[8];

                memory_copy(text, address_of word, 8);
                for (positive at = 0; at < 8; at++)
                        octal_whole = octal_whole && text[at] == (p8)('0' + ((value >> (21 - 3 * at)) & 7));
        }
        for (p64 value = 0; value < ((p64)1 << 16); value++)
        {
                p64 word = hex_digits_word(value);
                p8 text[8];

                memory_copy(text, address_of word, 8);
                for (positive at = 0; at < 8; at++)
                        hex_short = hex_short && text[at] == (p8)digits[(value >> (28 - 4 * at)) & 15];
        }
        for (positive turn = 0; turn < 300000; turn++)
        {
                p64 value;
                p64 word;
                p8 text[8];
                p8 bytes[8];

                state = state * 6364136223846793005ull + 1442695040888963407ull;
                value = state >> (turn % 33);
                if (turn % 7 == 0)
                        value = (p64)0xffffffff - (turn % 5);
                word = octal_digits_word(value);
                memory_copy(text, address_of word, 8);
                for (positive at = 0; at < 8; at++)
                        octal_whole = octal_whole && text[at] == (p8)('0' + (((value & 0xffffff) >> (21 - 3 * at)) & 7));
                word = hex_digits_word(value);
                memory_copy(text, address_of word, 8);
                for (positive at = 0; at < 8; at++)
                        hex_wide = hex_wide && text[at] == (p8)digits[((value & 0xffffffff) >> (28 - 4 * at)) & 15];
                word = word_bytes_reversed(state);
                memory_copy(bytes, address_of word, 8);
                for (positive at = 0; at < 8; at++)
                        reversed = reversed && bytes[at] == (p8)(state >> (8 * (7 - at)));
        }
        check("octal_digits_word spells every 17-bit value and random 24-bit ones", octal_whole);
        check("hex_digits_word spells every 16-bit value", hex_short);
        check("hex_digits_word spells random and edge 32-bit values", hex_wide);
        check("word_bytes_reversed reverses the eight bytes", reversed);
}

b32 main(void)
{
        checksums();
        fields();
        paths();
        packs();
        pax_records();
        name_precedence();
        name_spelling();
        digit_words();
        return test_report(null);
}
#endif /* CHECK_tar */

#ifdef CHECK_zstd
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#define ZSTD_CORE_ONLY
#define ZSTD_PAR_AUDIT
#include "../../src/sh/zstd.c"
#undef ZSTD_CORE_ONLY
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

#include "../codec_floor/reference/zstd_sequences_decode.c"
#include "../codec_floor/reference/zstd_sequences_exec.c"

static p8 zstd_empty_sum[] = {
        0x28, 0xb5, 0x2f, 0xfd, 0x24, 0x00, 0x01, 0x00, 0x00, 0x99, 0xe9,
        0xd8, 0x51};
static p8 zstd_empty[] = {0x28, 0xb5, 0x2f, 0xfd, 0x20, 0x00, 0x01, 0x00, 0x00};
static p8 zstd_abc[] = {0x28, 0xb5, 0x2f, 0xfd, 0x20, 0x03, 0x2d, 0x00,
                        0x00, 0x18, 0x61, 0x62, 0x63, 0x00};
static p8 zstd_rle[] = {0x28, 0xb5, 0x2f, 0xfd, 0x20, 0x05, 0x2b, 0x00,
                        0x00, 0x41};
static p8 zstd_dict[] = {0x28, 0xb5, 0x2f, 0xfd, 0x21, 0x01, 0x00, 0x01,
                         0x00, 0x00};
static p8 zstd_host_a[] = {0x28, 0xb5, 0x2f, 0xfd, 0x04, 0x68, 0x09, 0x00,
                           0x00, 0x61, 0x5b, 0x6e, 0x8c, 0xa9};
static p8 zstd_host_abc[] = {0x28, 0xb5, 0x2f, 0xfd, 0x04, 0x68, 0x19, 0x00,
                             0x00, 0x61, 0x62, 0x63, 0x99, 0x09, 0x77, 0xad};
static p8 zstd_host_hello[] = {
        0x28, 0xb5, 0x2f, 0xfd, 0x04, 0x68, 0x61, 0x00, 0x00, 0x68, 0x65,
        0x6c, 0x6c, 0x6f, 0x20, 0x77, 0x6f, 0x72, 0x6c, 0x64, 0x0a, 0x8c,
        0x6d, 0x7d, 0x20};
static p8 zstd_host_repeat[] = {
        0x28, 0xb5, 0x2f, 0xfd, 0x04, 0x68, 0x55, 0x00, 0x00, 0x20, 0x61,
        0x62, 0x63, 0x61, 0x01, 0x00, 0xda, 0x8e, 0x08, 0x57, 0x43, 0xdd,
        0xc7};
static p8 zstd_host_huff[] = {
        0x28, 0xb5, 0x2f, 0xfd, 0x64, 0x00, 0x01, 0x15, 0x05, 0x00, 0x72, 0x87,
        0x19, 0x1c, 0x70, 0x69, 0x75, 0xc0, 0x60, 0xd9, 0x8a, 0xdf, 0xf7, 0xb3,
        0x45, 0xbb, 0x4b, 0x0c, 0x9d, 0xfe, 0x89, 0xec, 0x10, 0x42, 0x7f, 0x06,
        0xd2, 0x01, 0xa0, 0x47, 0xe1, 0x08, 0x8d, 0xdd, 0x93, 0x08, 0x46, 0x95,
        0x38, 0xcc, 0xfd, 0xd4, 0x09, 0xa8, 0x13, 0x4d, 0xf9, 0x60, 0x97, 0xec,
        0xd5, 0x30, 0xcd, 0x77, 0x8d, 0xb5, 0xc5, 0xe9, 0x4c, 0xf4, 0x14, 0x0c,
        0x17, 0xd7, 0xb8, 0x1e, 0xd7, 0xc0, 0x75, 0x17, 0x37, 0x1c, 0x71, 0x08,
        0x0e, 0x0d, 0x9b, 0xe4, 0x1a, 0x43, 0x60, 0x82, 0xb0, 0x66, 0xc5, 0x99,
        0x04, 0xe3, 0xf2, 0xd4, 0x9b, 0x95, 0xb8, 0xbe, 0x5e, 0xa7, 0xe6, 0x44,
        0x8f, 0x2e, 0xe0, 0xa9, 0x7b, 0xdd, 0x07, 0x17, 0x00, 0xc6, 0x6b, 0x38,
        0x34, 0x4f, 0xa9, 0xd9, 0xa7, 0x1d, 0xdd, 0xb1, 0x22, 0x56, 0x31, 0x5b,
        0x0c, 0x24, 0xf8, 0x99, 0x0e, 0x80, 0x89, 0x41, 0xc3, 0xbb, 0xb9, 0xb1,
        0x7a, 0xf0, 0x36, 0x10, 0xba, 0x6a, 0x18, 0x9b, 0x0c, 0x00, 0x4e, 0x08,
        0x98, 0xb0, 0x15, 0x07, 0x20, 0xb9, 0xfa, 0x44, 0x4f, 0x82, 0x2e, 0x3b,
        0xe3, 0x32, 0x10, 0x07, 0x66, 0xe1, 0x39, 0x2d};
static p8 zstd_host_huff_out[] = {
        0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x2d, 0x31, 0x2c, 0x20, 0x2d, 0x31, 0x2c, 0x20, 0x2d,
        0x31, 0x2c, 0x20, 0x2d, 0x31, 0x7d, 0x3b, 0x0a, 0x73, 0x74, 0x61, 0x74,
        0x69, 0x63, 0x20, 0x63, 0x6f, 0x6e, 0x73, 0x74, 0x20, 0x62, 0x69, 0x70,
        0x6f, 0x6c, 0x61, 0x72, 0x20, 0x7a, 0x73, 0x74, 0x64, 0x5f, 0x6d, 0x6c,
        0x5f, 0x64, 0x65, 0x66, 0x61, 0x75, 0x6c, 0x74, 0x5b, 0x35, 0x33, 0x5d,
        0x20, 0x3d, 0x20, 0x7b, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x31, 0x2c, 0x20, 0x34, 0x2c, 0x20, 0x33, 0x2c, 0x20, 0x32, 0x2c,
        0x20, 0x32, 0x2c, 0x20, 0x32, 0x2c, 0x20, 0x32, 0x2c, 0x20, 0x32, 0x2c,
        0x20, 0x32, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c,
        0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c,
        0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x31, 0x2c, 0x20,
        0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20,
        0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20,
        0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20,
        0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x0a, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31,
        0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31,
        0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31,
        0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x2d,
        0x31, 0x2c, 0x20, 0x2d, 0x31, 0x2c, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x2d, 0x31, 0x2c, 0x20, 0x2d, 0x31, 0x2c, 0x20, 0x2d,
        0x31, 0x2c, 0x20, 0x2d, 0x31, 0x2c, 0x20, 0x2d, 0x31, 0x7d, 0x3b, 0x0a,
        0x73, 0x74, 0x61, 0x74, 0x69, 0x63, 0x20, 0x63, 0x6f, 0x6e, 0x73, 0x74,
        0x20, 0x62, 0x69, 0x70, 0x6f, 0x6c, 0x61, 0x72, 0x20, 0x7a, 0x73, 0x74,
        0x64, 0x5f, 0x6f, 0x66, 0x5f, 0x64, 0x65, 0x66, 0x61, 0x75, 0x6c, 0x74,
        0x5b, 0x32, 0x39, 0x5d, 0x20, 0x3d, 0x20, 0x7b, 0x0a, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31,
        0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x32,
        0x2c, 0x20, 0x32, 0x2c, 0x20, 0x32, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31,
        0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31,
        0x2c, 0x20, 0x31, 0x2c, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c,
        0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c, 0x20, 0x31, 0x2c,
        0x20, 0x2d, 0x31, 0x2c, 0x20, 0x2d, 0x31, 0x2c, 0x20, 0x2d, 0x31, 0x2c,
        0x20, 0x2d, 0x31, 0x2c, 0x20, 0x2d, 0x31, 0x7d, 0x3b, 0x0a, 0x0a, 0x74,
        0x79, 0x70, 0x65, 0x64, 0x65, 0x66, 0x20, 0x73, 0x74, 0x72, 0x75, 0x63,
        0x74, 0x0a, 0x7b, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x70, 0x31, 0x36, 0x20, 0x6e, 0x65, 0x78, 0x74, 0x3b, 0x0a, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x70, 0x38, 0x20, 0x62, 0x69, 0x74,
        0x73, 0x3b, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x70,
        0x38, 0x20, 0x73, 0x79, 0x6d, 0x62, 0x6f, 0x6c, 0x3b, 0x0a, 0x7d, 0x20,
        0x7a, 0x73, 0x74, 0x64, 0x5f, 0x66, 0x73, 0x65};

static p8 zstd_decoded_is(p8 address_to src, positive src_len,
                            string_address want)
{
        p8 into[64];
        bipolar n = zstd_inflate(src, src_len, into, sizeof(into));
        positive want_len = string_length(want);

        return n == (bipolar)want_len &&
               !memory_compare(into, want, want_len);
}

static fn frames(void)
{
        p8 into[64];
        bipolar n;

        n = zstd_inflate(zstd_empty_sum, sizeof(zstd_empty_sum), into,
                         sizeof(into));
        check("an empty frame with a content checksum", n == 0);

        n = zstd_inflate(zstd_empty, sizeof(zstd_empty), into, sizeof(into));
        check("an empty frame without a checksum", n == 0);

        check("raw literals and no sequences are the block",
              zstd_decoded_is(zstd_abc, sizeof(zstd_abc), "abc"));

        check("an RLE block repeats its byte",
              zstd_decoded_is(zstd_rle, sizeof(zstd_rle), "AAAAA"));

        check("a host raw frame of one byte",
              zstd_decoded_is(zstd_host_a, sizeof(zstd_host_a), "a"));
        check("a host raw frame of three bytes",
              zstd_decoded_is(zstd_host_abc, sizeof(zstd_host_abc), "abc"));
        check("a host raw frame with a newline",
              zstd_decoded_is(zstd_host_hello, sizeof(zstd_host_hello),
                              "hello world\n"));
        check("a host compressed frame with matches",
              zstd_decoded_is(zstd_host_repeat, sizeof(zstd_host_repeat),
                              "abcabcabcabcabcabcabcabcabcabc"));

        {
                p8 into[sizeof(zstd_host_huff_out)];
                bipolar n = zstd_inflate(zstd_host_huff, sizeof(zstd_host_huff),
                                         into, sizeof(into));

                check("a host Huffman-literal frame",
                      n == (bipolar)sizeof(zstd_host_huff_out) &&
                          !memory_compare(into, zstd_host_huff_out,
                                          sizeof(zstd_host_huff_out)));
        }

        n = zstd_inflate(null, 0, into, sizeof(into));
        check("empty input is empty output", n == 0);

        n = zstd_inflate(zstd_dict, sizeof(zstd_dict), into, sizeof(into));
        check("a dictionary id is refused", n < 0);

        {
                p8 both[sizeof(zstd_abc) * 2];

                memory_copy(both, zstd_abc, sizeof(zstd_abc));
                memory_copy(both + sizeof(zstd_abc), zstd_abc, sizeof(zstd_abc));
                n = zstd_inflate(both, sizeof(both), into, sizeof(into));
                check("concatenated frames join",
                      n == 6 && !memory_compare(into, "abcabc", 6));
        }

        {
                p8 skip[4 + 4 + sizeof(zstd_abc)];

                skip[0] = 0x50;
                skip[1] = 0x2a;
                skip[2] = 0x4d;
                skip[3] = 0x18;
                skip[4] = 0;
                skip[5] = 0;
                skip[6] = 0;
                skip[7] = 0;
                memory_copy(skip + 8, zstd_abc, sizeof(zstd_abc));
                check("a skippable frame is skipped",
                      zstd_decoded_is(skip, sizeof(skip), "abc"));
        }

        n = zstd_inflate(zstd_abc, 3, into, sizeof(into));
        check("a truncated magic fails", n < 0);
}

static fn bits(void)
{
        zstd_bits b;
        p8 one[] = {0x80};
        p8 mark[] = {0xC0};
        p8 zero[] = {0x01, 0x00};
        p8 eight[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x80};

        check("an empty bitstream fails to open",
              zstd_bits_open(address_of b, one, 0) != 0);
        check("a trailing zero fails to open",
              zstd_bits_open(address_of b, zero, sizeof(zero)) != 0);
        check("a marked byte opens",
              zstd_bits_open(address_of b, one, 1) == 0);
        check("the end mark of 0x80 is skipped",
              zstd_bits_get(address_of b, 1) == 0);
        check("reload of a live stream succeeds",
              zstd_bits_reload(address_of b) == 0);
        check("a marked C0 byte's first payload bit is one",
              zstd_bits_open(address_of b, mark, 1) == 0 &&
                  zstd_bits_get(address_of b, 1) == 1);
        check("an eight-byte marked stream opens",
              zstd_bits_open(address_of b, eight, sizeof(eight)) == 0);
}

/* The backward reader against a C model of its state, the reader zstd.c
   kept before it called the library: every length to 40 bytes at every
   pointer alignment, starting on a mapping's first page and ending on a
   hole, through gets of 0 to 16 bits and reloads until one overflows. */
static p64 bits_model_load(p8 address_to at, positive count)
{
        p64 value = 0;

        for (positive i = 0; i < count; i++)
                value |= (p64)at[i] << (8 * i);
        return value;
}

static p8 bits_model_open(zstd_bits address_to b, p8 address_to src, positive size)
{
        positive high = 7;

        if (!size || !src[size - 1])
                return false;
        while (!(src[size - 1] >> high))
                high--;
        b->start = src;
        b->last = src + size;
        b->limit = src + 8;
        b->ptr = size >= 8 ? src + size - 8 : src;
        b->bits = bits_model_load(b->ptr, size >= 8 ? 8 : size);
        b->consumed = (size >= 8 ? 0 : (8 - size) * 8) + 8 - high;
        return true;
}

static p64 bits_model_get(zstd_bits address_to b, positive n)
{
        p64 const value = !n || b->consumed >= 64 ? 0 : (b->bits << b->consumed) >> (64 - n);

        b->consumed += n;
        return value;
}

static p8 bits_model_reload(zstd_bits address_to b)
{
        positive bytes;
        positive have;

        if (b->consumed > 64)
                return false;
        if (b->ptr >= b->limit)
        {
                b->ptr -= b->consumed >> 3;
                b->consumed &= 7;
                b->bits = bits_model_load(b->ptr, 8);
                return true;
        }
        if (b->ptr == b->start)
                return true;
        bytes = b->consumed >> 3;
        if ((positive)(b->ptr - b->start) < bytes)
                bytes = (positive)(b->ptr - b->start);
        b->ptr -= bytes;
        b->consumed -= bytes * 8;
        have = (positive)(b->last - b->ptr);
        b->bits = bits_model_load(b->ptr, have < 8 ? have : 8);
        return true;
}

static fn bits_model(void)
{
        positive const page = 4096;
        p8 address_to const map = memory(3 * page);
        p32 random = 0x1b873593u;
        positive streams = 0;
        p8 same = true;

        if (!map || system_failed(map))
        {
                check("the reader model maps its pages", false);
                return;
        }
        memory_free(map + 2 * page, page);
        for (positive size = 1; size <= 40; size++)
                for (positive place = 0; place < 3; place++)
                        for (positive shift = 0; shift < 8; shift++)
                        {
                                p8 address_to const src = place == 0   ? map + shift
                                                          : place == 1 ? map + 2 * page - shift - size
                                                                       : map + page / 2 + shift;
                                zstd_bits ours;
                                zstd_bits model;
                                p8 opened;

                                for (positive i = 0; i < size; i++)
                                {
                                        XORSHIFT32(random);
                                        src[i] = (p8)(random >> 11);
                                }
                                if (!src[size - 1] && (random & 7))
                                        src[size - 1] = (p8)(1 << (random >> 3) % 8);
                                memory_fill(address_of ours, 0, sizeof(ours));
                                memory_fill(address_of model, 0, sizeof(model));
                                opened = bits_model_open(address_of model, src, size);
                                same &= (zstd_bits_open(address_of ours, src, size) == 0) == opened;
                                if (!opened)
                                        continue;
                                same &= !memory_compare(address_of ours, address_of model, sizeof(ours));
                                streams++;
                                for (positive step = 0; step < 96; step++)
                                {
                                        XORSHIFT32(random);
                                        if (random & 3)
                                        {
                                                positive const n = (random >> 8) % 17;

                                                same &= zstd_bits_get(address_of ours, n) ==
                                                        bits_model_get(address_of model, n);
                                        }
                                        else
                                        {
                                                p8 const live = bits_model_reload(address_of model);

                                                same &= (zstd_bits_reload(address_of ours) == 0) == live;
                                                if (!live)
                                                        break;
                                        }
                                        same &= !memory_compare(address_of ours, address_of model,
                                                                sizeof(ours));
                                }
                        }
        memory_free(map, 2 * page);
        check("the backward reader is its model at every alignment and mapping edge",
              same && streams > 900);
}

/* A single RLE sequence expands a five-byte match from one prior byte.  The
   sequence walker must reject it before writing when only three bytes remain,
   and the identical job must retain its ordinary result with enough room. */
static fn sequence_capacity(void)
{
        zstd_fse ll = {0};
        zstd_fse of = {0};
        zstd_fse ml = {0};
        zstd_seq_job job = {0};
        /* One marker bit and nothing else: every bit is read. */
        p8 sequence[8] = {0x01};
        p8 literals[1] = {0};
        p8 window[32];
        p32 rep[3] = {1, 1, 1};
        p8 guard = true;

        zstd_fse_rle(address_of ll, 0);
        zstd_fse_rle(address_of of, 0);
        zstd_fse_rle(address_of ml, 2);
        check("sequence capacity RLE tables",
              zstd_seq_fuse(address_of ll, 0) &&
                  zstd_seq_fuse(address_of of, 1) &&
                  zstd_seq_fuse(address_of ml, 2));

        memory_fill(window, 0x5a, sizeof(window));
        window[0] = 'A';
        job.window = window;
        job.pos = 1;
        job.window_size = 16;
        job.lits = literals;
        job.lit_len = 0;
        job.seq = sequence;
        job.seq_len = 1;
        job.ll = address_of ll;
        job.of = address_of of;
        job.ml = address_of ml;
        job.rep = rep;
        job.nseq = 1;
        job.output_end = window + 4;
        check("sequence output limit is enforced",
              zstd_sequences_run(address_of job) != 0 && job.pos == 1);
        for (positive i = 1; i < sizeof(window); i++)
                guard &= window[i] == 0x5a;
        check("rejected sequence leaves the destination untouched", guard);

        memory_fill(window, 0x5a, sizeof(window));
        rep[0] = rep[1] = rep[2] = 1;
        window[0] = 'A';
        job.pos = 1;
        job.output_end = window + 16;
        check("sequence with enough room keeps its ordinary result",
              zstd_sequences_run(address_of job) == 0 && job.pos == 6 &&
                  !memory_compare(window, "AAAAAA", 6) &&
                  window[6] == 0x5a);

        /* 0x80 puts the marker seven bits up: seven bits no field reads. */
        sequence[0] = 0x80;
        rep[0] = rep[1] = rep[2] = 1;
        job.pos = 1;
        check("a sequence stream with bits no field reads is refused",
              zstd_sequences_run(address_of job) != 0 && job.pos == 1);
}

/* Every Huffman stream ends on its last bit.  A byte put in front of a
   stream's start adds bits no symbol reads while every symbol decodes as
   before, so the only thing wrong with the section is the unread bits. */
static fn huffman_exact_end(void)
{
        static p8 src[65536];
        static p8 section[65536 + 1024];
        static p8 back[65536 + 64];
        p8 header[5];
        p32 random = 0x2545f491u;

        for (positive i = 0; i < sizeof(src); i++)
        {
                XORSHIFT32(random);
                src[i] = (random & 3) ? (p8)('a' + (random >> 8) % 16)
                                      : (p8)(random >> 16);
        }
        positive hn = 0;
        p8 fresh = false;
        positive cn = zstd_pack_literals(null, src, sizeof(src), header,
                                         address_of hn, address_of fresh);
        positive tree = 0;
        zstd_huff huff;
        check("the exactness section packs and its tree reads",
              cn && zstd_huff_read(zstd_arena->packed_lits, cn, address_of tree,
                                   address_of huff));
        if (!cn || tree >= cn)
                return;
        p8 address_to streams = zstd_arena->packed_lits + tree;
        positive size = cn - tree;
        check("four streams that end on their last bits decode",
              zstd_huffman_4x(back, sizeof(src), streams, size, huff.cell,
                              huff.max_bits) == 0 &&
                  !memory_compare(back, src, sizeof(src)));
        /* Under six bytes out, four segments of (need + 3) / 4 start past
           the end: the fast loop's room test on stream four wrapped and
           passed and the slow path's last length underflowed, so the
           routine wrote past dest. zstd.c refuses such a block first; the
           routine refuses it too, and writes nothing. */
        for (positive need = 1; need < 6; need++)
        {
                p8 small[96];
                p8 untouched = true;

                memory_fill(small, 0xa5, sizeof(small));
                check("four streams into fewer than six bytes are refused",
                      zstd_huffman_4x(small, need, streams, size, huff.cell,
                                      huff.max_bits) < 0);
                for (positive i = 0; i < sizeof(small); i++)
                        untouched &= small[i] == 0xa5;
                check("and nothing is written for them", untouched);
        }
        for (positive k = 0; k < 4; k++)
        {
                positive start = 6;

                for (positive j = 0; j < k; j++)
                        start += memory_load_unaligned(p16, streams + 2 * j);
                memory_copy_apart(section, streams, start);
                section[start] = 0xa5;
                memory_copy_apart(section + start + 1, streams + start,
                                  size - start);
                if (k < 3)
                        memory_store_unaligned(
                            p16, section + 2 * k,
                            (p16)(memory_load_unaligned(p16, streams + 2 * k) + 1));
                check("a four-stream section with bits no symbol reads is refused",
                      zstd_huffman_4x(back, sizeof(src), section, size + 1,
                                      huff.cell, huff.max_bits) != 0);
        }

        /* One-bit codes: 0 is 'a', 1 is 'b'; 0x05 is the marker over "ab". */
        p16 cells[2] = {(p16)('a' << 8 | 1), (p16)('b' << 8 | 1)};
        p8 ab[1] = {0x05};
        p8 padded[2] = {0x00, 0x05};
        p8 out[16];
        check("one stream read to its last bit decodes",
              zstd_huffman_stream(out, 2, ab, 1, cells, 1) == 0 &&
                  out[0] == 'a' && out[1] == 'b');
        check("one stream with an unread bit is refused",
              zstd_huffman_stream(out, 1, ab, 1, cells, 1) != 0);
        check("one stream asked past its last bit is refused",
              zstd_huffman_stream(out, 3, ab, 1, cells, 1) != 0);
        check("one stream with an unread byte is refused",
              zstd_huffman_stream(out, 2, padded, 2, cells, 1) != 0);
        check("the same byte read as eight more symbols decodes",
              zstd_huffman_stream(out, 10, padded, 2, cells, 1) == 0 &&
                  out[1] == 'b' && out[9] == 'a');
}

static fn roundtrip(void)
{
        p8 src[4096];
        p8 packed[8192];
        p8 back[4096];
        bipolar n;
        bipolar m;
        positive at;

        n = zstd_deflate_mem((p8 address_to) "", 0, packed, sizeof(packed));
        check("encode empty", n > 0);
        m = zstd_inflate(packed, (positive)n, back, sizeof(back));
        check("decode empty encode", m == 0);

        n = zstd_deflate_mem((p8 address_to) "hello\n", 6, packed, sizeof(packed));
        check("encode hello", n > 0);
        m = zstd_inflate(packed, (positive)n, back, sizeof(back));
        check("hello roundtrip",
              m == 6 && !memory_compare(back, "hello\n", 6));

        for (at = 0; at < sizeof(src); at++)
                src[at] = (p8)(at * 3 + 7);
        n = zstd_deflate_mem(src, sizeof(src), packed, sizeof(packed));
        check("encode 4k", n > 0 && n < (bipolar)sizeof(packed));
        m = zstd_inflate(packed, (positive)n, back, sizeof(back));
        check("4k roundtrip",
              m == (bipolar)sizeof(src) && !memory_compare(back, src, sizeof(src)));

        memory_fill(src, 'a', sizeof(src));
        n = zstd_deflate_mem(src, sizeof(src), packed, sizeof(packed));
        check("encode repeated", n > 0 && n < 128);
        m = zstd_inflate(packed, (positive)n, back, sizeof(back));
        check("repeated roundtrip",
              m == (bipolar)sizeof(src) && !memory_compare(back, src, sizeof(src)));
}

static fn literal_codebooks(void)
{
        static p8 src[131072];
        static p8 packed[262656];
        static p8 back[131072];
        const positive sizes[] = {64, 65, 127, 129, 511, 513, 4095, 4096,
                                   16383, 16384, 65535, 131072};
        const positive alphabets[] = {31, 127, 128, 129, 255};
        for (positive a = 0; a < array_count(alphabets); a++)
                for (positive k = 0; k < array_count(sizes); k++)
                {
                        positive n = sizes[k];
                        p32 random = 0x97346112u;
                        for (positive i = 0; i < n; i++)
                        {
                                XORSHIFT32(random);
                                src[i] = (random & 7) ? (random >> 8) % 8
                                                     : (random >> 16) % (alphabets[a] + 1);
                        }
                        src[n - 1] = (p8)alphabets[a];
                        positive hn = 0;
                        p8 fresh = false;
                        positive cn = zstd_pack_literals(null, src, n, packed,
                                                         address_of hn, address_of fresh);
                        if (!cn)
                        {
                                /* A short alphabet header may cost more than
                                   the payload; larger cases must compress. */
                                check("literal tree worth encoding", n < 513);
                                continue;
                        }
                        memory_copy_apart(packed + hn, zstd_arena->packed_lits, cn);
                        positive used = 0, got = 0;
                        zstd_block_limit = ZSTD_BLOCK_MAX;
                        check("four streams and complete direct/FSE tree",
                              zstd_literals(packed, hn + cn, address_of used,
                                             back, address_of got) && got == n &&
                              used == hn + cn && !memory_compare(src, back, n));
                }
}

static fn pull_block_shapes(void)
{
        static p8 packed[3 * ZSTD_BLOCK_MAX + 64];
        static p8 expected[3 * ZSTD_BLOCK_MAX];
        static p8 back[3 * ZSTD_BLOCK_MAX];
        const positive takes[] = {1, 7, 511, 512, 4097, 131072};
        for (positive shape = 0; shape < 8; shape++)
        {
                p8 header[6] = {0x28,0xb5,0x2f,0xfd,0,0x58};
                memory_copy_apart(packed, header, sizeof(header));
                positive pn = 6, size = 0;
                for (positive block = 0; block < 3; block++)
                {
                        positive n = block == 0 && (shape & 4) ? 4097 : ZSTD_BLOCK_MAX;
                        p8 rle = (shape >> (block % 2)) & 1;
                        p32 h = (p32)(n << 3) | (rle ? 2 : 0) | (block == 2);
                        packed[pn++] = (p8)h; packed[pn++] = (p8)(h >> 8); packed[pn++] = (p8)(h >> 16);
                        if (rle)
                        {
                                packed[pn++] = (p8)('a' + block);
                                memory_fill(expected + size, 'a' + block, n);
                        }
                        else
                        {
                                for (positive i = 0; i < n; i++) packed[pn + i] = expected[size + i] = (p8)(i * 17 + block);
                                pn += n;
                        }
                        size += n;
                }
                for (positive cut = 0; cut < 2; cut++)
                {
                        zstd_decode_begin(-1);
                        zstd_src.mem = packed; zstd_src.mem_len = pn - cut;
                        positive n = 0, step = 0;
                        bipolar got = 0;
                        while (n < sizeof(back))
                        {
                                positive take = takes[step++ % array_count(takes)];
                                if (take > sizeof(back) - n) take = sizeof(back) - n;
                                got = zstd_decode_read(back + n, take);
                                if (got <= 0) break;
                                n += (positive)got;
                        }
                        if (got >= 0)
                        {
                                p8 extra;
                                got = zstd_decode_read(address_of extra, 1);
                        }
                        p8 done = zstd_decode_end();
                        if (cut)
                                check("pull reader refuses a truncated raw/RLE block", got < 0 && !done);
                        else
                                check("pull raw/RLE boundaries and final block survive partial reads",
                                      got == 0 && done && n == size && !memory_compare(back, expected, size));
                }
        }
        p8 compressed[1024];
        p8 data[512];
        memory_fill(data, 'x', sizeof(data));
        bipolar n = zstd_deflate_mem(data, sizeof(data), compressed, sizeof(compressed));
        if (n > 4)
        {
                zstd_decode_begin(-1);
                zstd_src.mem = compressed; zstd_src.mem_len = (positive)n - 4;
                bipolar got = zstd_decode_read(back, sizeof(back));
                check("pull reader refuses a completely missing checksum", got < 0 && !zstd_decode_end());
        }
}

/* A frame cut into jobs is the same bytes at every pool width: five MiB at
   level 1 is three 2 MiB jobs, each with the tail of the one before as its
   history, built in its own output and written in order. */
static fn job_widths(void)
{
        static p8 src[5 << 20];
        static p8 first[6 << 20];
        static p8 again[6 << 20];
        static p8 back[5 << 20];
        const positive widths[] = {1, 2, 3, 8};
        positive first_n = 0;
        p32 random = 0x6d2b79f5u;
        zstd_params params;

        for (positive i = 0; i < sizeof(src); i++)
        {
                XORSHIFT32(random);
                src[i] = i >= 3000 && (random & 3) ? src[i - 3000 + (random >> 20) % 8]
                                                   : (p8)('a' + (random >> 8) % 20);
        }
        /* As in a zstd process that only ever runs jobs: nothing has mapped
           the single encoder's arena, and a slot encoder must not need it. */
        zstd_arena = null;
        for (positive w = 0; w < array_count(widths); w++)
        {
                p8 address_to const into = w ? again : first;
                p8 ok;

                parallel_reset(widths[w]);
                zstd_output.bytes = into;
                zstd_output.room = sizeof(first);
                zstd_output.used = 0;
                zstd_out_fd = -1;
                zstd_level_params(1, 0, 0, address_of params);
                ok = zstd_encode_start(address_of params, true) &&
                     zstd_encode_write(src, sizeof(src)) && zstd_encode_end();
                zstd_output.bytes = null;
                if (!w)
                {
                        first_n = zstd_output.used;
                        check("five MiB at level 1 is a frame of three jobs",
                              ok && zstd_jobs.count == 3 && first_n > 0);
                }
                else
                        check("the jobs' frame is the same bytes at a wider pool",
                              ok && zstd_output.used == first_n &&
                                  !memory_compare(again, first, first_n));
        }
        /* One encoder over the same input: the cuts cost the jobs only what
           the overlap leaves out, 8 bytes of 2874068 here. When a slot
           encoder's literals went out raw for want of that arena, the jobs'
           frame was 5242903 bytes here, the input all but stored, and 17%
           bigger than libzstd's at -1 on a source tar. */
        {
                p8 single;

                zstd_single_job = true;
                zstd_output.bytes = again;
                zstd_output.room = sizeof(again);
                zstd_output.used = 0;
                zstd_out_fd = -1;
                zstd_level_params(1, 0, 0, address_of params);
                single = zstd_encode_start(address_of params, true) &&
                         zstd_encode_write(src, sizeof(src)) && zstd_encode_end();
                zstd_output.bytes = null;
                zstd_single_job = false;
                check("the jobs' frame is within a fiftieth of one encoder's frame",
                      single && first_n * 50 <= zstd_output.used * 51);
        }
        parallel_reset(0);
        check("the jobs' frame decodes to its input",
              zstd_inflate(first, first_n, back, sizeof(back)) == (bipolar)sizeof(src) &&
                  !memory_compare(back, src, sizeof(src)));
}

static const p8 zstd_dict_bytes[] = {
        0x37, 0xa4, 0x30, 0xec, 0xc7, 0x32, 0x69, 0x1b, 0x22, 0x10, 0x18, 0x3a,
        0x48, 0xa1, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xc3, 0x30,
        0x0c, 0xc3, 0x20, 0x06, 0x31, 0xcd, 0x0c, 0x33, 0x91, 0x5e, 0x4a, 0x99,
        0xa4, 0x94, 0x52, 0x31, 0x84, 0x4b, 0x69, 0x73, 0x01, 0x00, 0x18, 0x16,
        0x90, 0x12, 0x2c, 0x87, 0x03, 0x00, 0x00, 0x04, 0xa0, 0x40, 0x04, 0x1c,
        0x45, 0xc9, 0xb6, 0x2e, 0x22, 0x04, 0x18, 0x42, 0x80, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x4c, 0x15, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x74, 0xc9, 0xd3, 0x70, 0x27, 0x04, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x61, 0x6c,
        0x70, 0x68, 0x61, 0x3d, 0x36, 0x31, 0x3b, 0x61, 0x6c, 0x70, 0x68, 0x61,
        0x3d, 0x35, 0x37, 0x0a, 0x62, 0x65, 0x74, 0x61, 0x3d, 0x31, 0x31, 0x3b,
        0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0x3d, 0x39, 0x37, 0x0a, 0x67, 0x61,
        0x6d, 0x6d, 0x61, 0x3d, 0x39, 0x30, 0x0a, 0x63, 0x6f, 0x6e, 0x66, 0x69,
        0x67, 0x3d, 0x31, 0x30, 0x3b, 0x67, 0x61, 0x6d, 0x6d, 0x61, 0x3d, 0x33,
        0x31, 0x3b, 0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x3d, 0x32, 0x33, 0x0a,
        0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0x3d, 0x36, 0x39, 0x3b, 0x73, 0x65,
        0x72, 0x76, 0x65, 0x72, 0x3d, 0x32, 0x38, 0x0a, 0x61, 0x6c, 0x70, 0x68,
        0x61, 0x3d, 0x37, 0x3b, 0x62, 0x65, 0x74, 0x61, 0x3d, 0x36, 0x35, 0x0a,
        0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0x3d, 0x37, 0x32, 0x3b, 0x61, 0x6c,
        0x70, 0x68, 0x61, 0x3d, 0x37, 0x31, 0x0a, 0x73, 0x65, 0x72, 0x76, 0x65,
        0x72, 0x3d, 0x34, 0x35, 0x3b, 0x67, 0x61, 0x6d, 0x6d, 0x61, 0x3d, 0x37,
        0x3b, 0x64, 0x65, 0x6c, 0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x3d, 0x38,
        0x30, 0x0a, 0x61, 0x6c, 0x70, 0x68, 0x61, 0x3d, 0x31, 0x30, 0x0a, 0x67,
        0x61, 0x6d, 0x6d, 0x61, 0x3d, 0x37, 0x32, 0x3b, 0x67, 0x61, 0x6d, 0x6d,
        0x61, 0x3d, 0x38, 0x32, 0x0a, 0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0x3d,
        0x35, 0x37, 0x0a, 0x73, 0x65, 0x72, 0x74, 0x61, 0x3d, 0x31, 0x3b, 0x61,
        0x6c, 0x70, 0x68, 0x61, 0x3d, 0x31, 0x31, 0x0a, 0x67, 0x61, 0x6d, 0x6d,
        0x61, 0x3d, 0x32, 0x30, 0x0a, 0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x3d,
        0x35, 0x31, 0x3b, 0x64, 0x65, 0x6c, 0x74, 0x61, 0x3d, 0x37, 0x35, 0x0a,
        0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x3d, 0x38, 0x74, 0x61, 0x3d, 0x37,
        0x32, 0x0a, 0x64, 0x65, 0x6c, 0x74, 0x61, 0x3d, 0x39, 0x31, 0x3b, 0x64,
        0x65, 0x6c, 0x74, 0x61, 0x3d, 0x33, 0x31, 0x3b, 0x73, 0x65, 0x72, 0x76,
        0x65, 0x72, 0x3d, 0x31, 0x31, 0x3b, 0x67, 0x61, 0x6d, 0x6d, 0x61, 0x3d,
        0x31, 0x39, 0x0a, 0x61, 0x6c, 0x70, 0x68, 0x61, 0x3d, 0x37, 0x3d, 0x34,
        0x38, 0x3b, 0x61, 0x6c, 0x70, 0x68, 0x61, 0x3d, 0x38, 0x30, 0x3b, 0x73,
        0x65, 0x72, 0x76, 0x65, 0x72, 0x3d, 0x34, 0x38, 0x0a, 0x64, 0x65, 0x6c,
        0x74, 0x61, 0x3d, 0x37, 0x36, 0x0a, 0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67,
        0x3d, 0x39, 0x35, 0x0a, 0x61, 0x6c, 0x70, 0x68, 0x61, 0x3d, 0x38, 0x32,
        0x36, 0x3b, 0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x3d, 0x33, 0x39, 0x3b,
        0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x3d, 0x32, 0x34, 0x0a, 0x64, 0x65,
        0x6c, 0x74, 0x61, 0x3d, 0x34, 0x35, 0x0a, 0x64, 0x65, 0x6c, 0x74, 0x61,
        0x3d, 0x38, 0x32, 0x3b, 0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0x3d, 0x39,
        0x38, 0x0a, 0x67, 0x3d, 0x33, 0x33, 0x0a, 0x73, 0x65, 0x72, 0x76, 0x65,
        0x72, 0x3d, 0x33, 0x37, 0x3b, 0x62, 0x65, 0x74, 0x61, 0x3d, 0x38, 0x32,
        0x0a, 0x61, 0x6c, 0x70, 0x68, 0x61, 0x3d, 0x39, 0x30, 0x3b, 0x63, 0x6f,
        0x6e, 0x66, 0x69, 0x67, 0x3d, 0x31, 0x34, 0x3b, 0x63, 0x6f, 0x6e, 0x66,
        0x69, 0x67, 0x3d, 0x36, 0x72, 0x76, 0x65, 0x72, 0x3d, 0x35, 0x33, 0x0a,
        0x67, 0x61, 0x6d, 0x6d, 0x61, 0x3d, 0x32, 0x36, 0x3b, 0x67, 0x61, 0x6d,
        0x6d, 0x61, 0x3d, 0x34, 0x33, 0x0a, 0x62, 0x65, 0x74, 0x61, 0x3d, 0x39,
        0x30, 0x0a, 0x64, 0x65, 0x6c, 0x74, 0x61, 0x3d, 0x37, 0x3b, 0x64, 0x65,
        0x6c, 0x74, 0x61, 0x3d, 0x31, 0x37, 0x6d, 0x6d, 0x61, 0x3d, 0x35, 0x33,
        0x3b, 0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0x3d, 0x38, 0x36, 0x0a, 0x67,
        0x61, 0x6d, 0x6d, 0x61, 0x3d, 0x34, 0x39, 0x0a, 0x63, 0x6f, 0x6e, 0x66,
        0x69, 0x67, 0x3d, 0x34, 0x36, 0x0a, 0x67, 0x61, 0x6d, 0x6d, 0x61, 0x3d,
        0x38, 0x3b, 0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x68, 0x61, 0x3d, 0x34,
        0x37, 0x0a, 0x67, 0x61, 0x6d, 0x6d, 0x61, 0x3d, 0x36, 0x35, 0x3b, 0x61,
        0x6c, 0x70, 0x68, 0x61, 0x3d, 0x36, 0x34, 0x3b, 0x64, 0x65, 0x6c, 0x74,
        0x61, 0x3d, 0x35, 0x32,
};
static const p8 zstd_dict_frame[] = {
        0x28, 0xb5, 0x2f, 0xfd, 0x27, 0xc7, 0x32, 0x69, 0x1b, 0x97, 0xc5, 0x01,
        0x00, 0xb0, 0x62, 0x33, 0x34, 0x35, 0x37, 0x31, 0x39, 0x34, 0x31, 0x33,
        0x0a, 0x38, 0x33, 0x39, 0x32, 0x37, 0x37, 0x38, 0x33, 0x38, 0x31, 0x3b,
        0x11, 0xfc, 0x68, 0xc4, 0x3c, 0xc8, 0x4f, 0xa3, 0xec, 0x76, 0x80, 0x32,
        0x87, 0x40, 0xb5, 0xeb, 0xb8, 0xfe, 0xc9, 0xac, 0x8d, 0x17, 0xd4, 0x9b,
        0xcb, 0x3c, 0xfb, 0x2a, 0xd0, 0x1a, 0xd2, 0x7b, 0x11, 0x2c, 0xef, 0xee,
        0x2e,
};
static const p8 zstd_dict_plain[] = {
        0x62, 0x65, 0x74, 0x61, 0x3d, 0x33, 0x34, 0x3b, 0x67, 0x61, 0x6d, 0x6d,
        0x61, 0x3d, 0x35, 0x37, 0x0a, 0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x3d,
        0x31, 0x39, 0x3b, 0x64, 0x65, 0x6c, 0x74, 0x61, 0x3d, 0x34, 0x31, 0x0a,
        0x67, 0x61, 0x6d, 0x6d, 0x61, 0x3d, 0x36, 0x33, 0x0a, 0x64, 0x65, 0x6c,
        0x74, 0x61, 0x3d, 0x33, 0x31, 0x3b, 0x64, 0x65, 0x6c, 0x74, 0x61, 0x3d,
        0x38, 0x35, 0x3b, 0x67, 0x61, 0x6d, 0x6d, 0x61, 0x3d, 0x32, 0x34, 0x0a,
        0x64, 0x65, 0x6c, 0x74, 0x61, 0x3d, 0x33, 0x35, 0x3b, 0x67, 0x61, 0x6d,
        0x6d, 0x61, 0x3d, 0x39, 0x32, 0x3b, 0x67, 0x61, 0x6d, 0x6d, 0x61, 0x3d,
        0x31, 0x39, 0x0a, 0x62, 0x65, 0x74, 0x61, 0x3d, 0x37, 0x35, 0x3b, 0x67,
        0x61, 0x6d, 0x6d, 0x61, 0x3d, 0x37, 0x38, 0x3b, 0x64, 0x65, 0x6c, 0x74,
        0x61, 0x3d, 0x31, 0x31, 0x3b, 0x61, 0x6c, 0x70, 0x68, 0x61, 0x3d, 0x33,
        0x38, 0x0a, 0x62, 0x65, 0x74, 0x61, 0x3d, 0x39, 0x0a, 0x73, 0x65, 0x72,
        0x76, 0x65, 0x72, 0x3d, 0x37, 0x31, 0x3b,
};


/* A dictionary libzstd trained (700 bytes, tables and content) and a frame
   it made with it: the dictionary loads, the frame decodes to its input
   with it and is refused without it and with another, a raw-content
   dictionary carries no ID and no tables, and a truncated or damaged
   dictionary is refused. */
static fn dictionaries(void)
{
        p8 into[1024];
        p8 damaged[sizeof(zstd_dict_bytes)];
        bipolar n;
        positive content_size;

        zstd_dict_on = false;
        n = zstd_inflate(zstd_dict_frame, sizeof(zstd_dict_frame), into, sizeof(into));
        check("a frame that names a dictionary is refused without one", n < 0);
        check("a trained dictionary loads",
              zstd_dictionary_load((p8 address_to)zstd_dict_bytes, sizeof(zstd_dict_bytes)) &&
                  zstd_dict_entropy && zstd_dict_size > 0);
        content_size = zstd_dict_size;
        n = zstd_inflate(zstd_dict_frame, sizeof(zstd_dict_frame), into, sizeof(into));
        check("a frame made with it decodes to its input",
              n == (bipolar)sizeof(zstd_dict_plain) &&
                  !memory_compare(into, zstd_dict_plain, sizeof(zstd_dict_plain)));
        memory_copy(damaged, zstd_dict_bytes, sizeof(damaged));
        damaged[4] ^= 1;
        check("a dictionary loads under another ID",
              zstd_dictionary_load(damaged, sizeof(damaged)));
        n = zstd_inflate(zstd_dict_frame, sizeof(zstd_dict_frame), into, sizeof(into));
        check("and the frame of the first is refused under it", n < 0);
        check("a dictionary cut short of its tables is refused",
              !zstd_dictionary_load((p8 address_to)zstd_dict_bytes, 60));
        check("one cut short of its repeat offsets is refused",
              !zstd_dictionary_load((p8 address_to)zstd_dict_bytes,
                                    sizeof(zstd_dict_bytes) - content_size - 8));
        check("a raw dictionary is content and no tables",
              zstd_dictionary_load((p8 address_to)zstd_dict_plain, sizeof(zstd_dict_plain)) &&
                  !zstd_dict_entropy && zstd_dict_id == 0 &&
                  zstd_dict_size == sizeof(zstd_dict_plain));
        zstd_dict_on = false;
}

/* zstd_fast_parse (lib.c) against the parse as it was in C, which
   test/codec_floor/reference/zstd_fast_parse.c keeps, over random blocks: data made of literals, copies at near,
   far and repeated distances and runs; every table width, minimum match,
   step and window; repeat offsets of the first block, of a job's later ones
   (zero) and random; a table empty or full of old positions. Everything it
   leaves is compared: the sequences, the literals, the code counts, the
   repeat offsets, the table, the sequence and literal counts and where the
   unparsed literals begin. */
#include "../codec_floor/reference/zstd_fast_parse.c"
#include "../codec_floor/reference/zstd_dfast_parse.c"

static zstd_encoder fast_parse_a, fast_parse_b;
static zstd_enc_seq fast_parse_seqs_a[ZSTD_ENC_SEQ_MAX], fast_parse_seqs_b[ZSTD_ENC_SEQ_MAX];
static p8 fast_parse_lits_a[ZSTD_BLOCK_MAX + 64], fast_parse_lits_b[ZSTD_BLOCK_MAX + 64];
static p32 fast_parse_table_a[1 << 16], fast_parse_table_b[1 << 16];
static p32 fast_parse_chain_a[1 << 16], fast_parse_chain_b[1 << 16];
static p8 fast_parse_data[ZSTD_BLOCK_MAX * 2 + 256];

/* The two walks, fast and then dfast, over the same kinds of blocks. */
static fn fast_parse_walk(p8 dfast)
{
        p32 random = dfast ? 0x1b873593u : 0x7f4a7c15u;
        p8 same = true;
        positive total = 0;

#define FAST_RANDOM() (random ^= random << 13, random ^= random >> 17, random ^= random << 5, random)
        for (positive trial = 0; trial < 240; trial++)
        {
                positive const history = trial % 3 ? FAST_RANDOM() % 90000 : 0;
                positive const block = trial % 5 ? 12 + FAST_RANDOM() % 40000 : 12 + FAST_RANDOM() % (ZSTD_BLOCK_MAX - 12);
                positive const size = history + block;
                positive const hlog = 8 + FAST_RANDOM() % 9;
                positive const slog = 8 + FAST_RANDOM() % 9;
                p32 const from = (p32)(1 + history);
                p32 const to = from + (p32)block;
                p32 low = 1;
                positive pos = 0;
                positive distance = 1;
                positive kind = FAST_RANDOM() % 4;
                p8 alphabet = (p8)(2 + FAST_RANDOM() % 60);

                while (pos < size)
                {
                        positive action = FAST_RANDOM() % 8;

                        if (pos > 8 && action < (kind == 0 ? 1 : 5))
                        {
                                positive length = 3 + FAST_RANDOM() % (kind == 2 ? 300 : 24);
                                positive far = FAST_RANDOM() % 4;

                                if (far == 0)
                                        distance = 1 + FAST_RANDOM() % pos;
                                else if (far == 1)
                                        distance = 1 + FAST_RANDOM() % (pos < 64 ? pos : 64);
                                else if (far == 2 && distance >= pos)
                                        distance = 1;
                                if (distance > pos)
                                        distance = pos;
                                for (positive i = 0; i < length && pos < size; i++, pos++)
                                        fast_parse_data[pos] = fast_parse_data[pos - distance];
                        }
                        else
                        {
                                positive length = 1 + FAST_RANDOM() % 12;

                                for (positive i = 0; i < length && pos < size; i++, pos++)
                                        fast_parse_data[pos] = (p8)(kind == 3 ? FAST_RANDOM() : FAST_RANDOM() % alphabet);
                        }
                }
                memory_fill(fast_parse_data + size, 0, 128);
                if (history && FAST_RANDOM() % 2)
                        low = (p32)(1 + FAST_RANDOM() % history);
                for (positive which = 0; which < 2; which++)
                {
                        zstd_encoder address_to e = which ? address_of fast_parse_b : address_of fast_parse_a;
                        p32 address_to table = which ? fast_parse_table_b : fast_parse_table_a;
                        p32 address_to chain = which ? fast_parse_chain_b : fast_parse_chain_a;
                        p32 sample = 0x2545f491u + (p32)trial;

                        memory_fill(e, 0, sizeof(*e));
                        e->base = fast_parse_data - 1;
                        e->hash = table;
                        e->chain = chain;
                        e->seqs = which ? fast_parse_seqs_b : fast_parse_seqs_a;
                        e->lits = which ? fast_parse_lits_b : fast_parse_lits_a;
                        e->p.hash_log = (p8)hlog;
                        e->p.chain_log = (p8)slog;
                        e->p.min_match = (p8)(trial % 7 ? 4 + trial % 5 : trial % 11);
                        e->p.target_length = (p32)(trial % 4 ? trial % 3 : 3 + trial % 130);
                        if (trial % 4 == 1)
                                e->rep[0] = e->rep[1] = e->rep[2] = 0;
                        else if (trial % 4 == 2)
                        {
                                e->rep[0] = 1 + (p32)(FAST_RANDOM() % from);
                                e->rep[1] = 1 + (p32)(FAST_RANDOM() % from);
                                e->rep[2] = 1 + (p32)(FAST_RANDOM() % from);
                        }
                        else
                        {
                                e->rep[0] = 1;
                                e->rep[1] = 4;
                                e->rep[2] = 8;
                        }
                        memory_fill(table, 0, sizeof(fast_parse_table_a));
                        memory_fill(chain, 0, sizeof(fast_parse_chain_a));
                        if (trial % 2)
                                for (positive i = 0; i < ((positive)1 << hlog); i++)
                                {
                                        sample ^= sample << 13;
                                        sample ^= sample >> 17;
                                        sample ^= sample << 5;
                                        table[i] = sample % from;
                                        chain[i] = (sample >> 7) % from;
                                }
                        //      Both runs start from the first's draw.
                        if (which)
                        {
                                memory_copy(e->rep, fast_parse_a.rep, sizeof(e->rep));
                                memory_copy(table, fast_parse_table_a, sizeof(fast_parse_table_a));
                                memory_copy(chain, fast_parse_chain_a, sizeof(fast_parse_chain_a));
                        }
                }
                {
                        p8 address_to const reference = dfast
                            ? zstd_parse_dfast_reference(address_of fast_parse_a, from, to, low)
                            : zstd_parse_fast_reference(address_of fast_parse_a, from, to, low);
                        p8 address_to const got = dfast
                            ? zstd_parse_dfast(address_of fast_parse_b, from, to, low)
                            : zstd_parse_fast(address_of fast_parse_b, from, to, low);

                        total += fast_parse_a.nseq;
                        same = same && reference - fast_parse_a.base == got - fast_parse_b.base &&
                               fast_parse_a.nseq == fast_parse_b.nseq && fast_parse_a.nlit == fast_parse_b.nlit &&
                               !memory_compare(fast_parse_a.rep, fast_parse_b.rep, sizeof(fast_parse_a.rep)) &&
                               !memory_compare(address_of fast_parse_a.freq, address_of fast_parse_b.freq, sizeof(fast_parse_a.freq)) &&
                               !memory_compare(fast_parse_lits_a, fast_parse_lits_b, fast_parse_a.nlit) &&
                               !memory_compare(fast_parse_table_a, fast_parse_table_b, sizeof(fast_parse_table_a)) &&
                               !memory_compare(fast_parse_chain_a, fast_parse_chain_b, sizeof(fast_parse_chain_a));
                        for (positive i = 0; same && i < fast_parse_a.nseq; i++)
                                same = fast_parse_seqs_a[i].lit == fast_parse_seqs_b[i].lit &&
                                       fast_parse_seqs_a[i].match == fast_parse_seqs_b[i].match &&
                                       fast_parse_seqs_a[i].off == fast_parse_seqs_b[i].off &&
                                       fast_parse_seqs_a[i].ll_code == fast_parse_seqs_b[i].ll_code &&
                                       fast_parse_seqs_a[i].ml_code == fast_parse_seqs_b[i].ml_code &&
                                       fast_parse_seqs_a[i].of_code == fast_parse_seqs_b[i].of_code;
                }
        }
#undef FAST_RANDOM
        if (dfast)
        {
                check("the dfast parse leaves what the C parse left: sequences, literals, counts, repeats, tables, anchor", same);
                check("the dfast parse trials found sequences to compare", total > 20000);
        }
        else
        {
                check("the fast parse leaves what the C parse left: sequences, literals, counts, repeats, table, anchor", same);
                check("the fast parse trials found sequences to compare", total > 20000);
        }
}

static fn fast_parse(void)
{
        fast_parse_walk(false);
        fast_parse_walk(true);
}

/* zstd_row_mask against one comparison a slot, over rows of 16, 32 and 64 tags
   drawn from few values (so most tags match one of them), from one value (all
   match) and from two disjoint ones (none), at every tag it is asked about. */
static fn row_mask(void)
{
        static p8 row[64 + 16];
        p32 random = 0x2545f491u;
        p8 same = true;

        for (positive trial = 0; trial < 3000; trial++)
        {
                positive const entries = (positive)16 << (trial % 3);
                p8 const spread = (p8)(trial % 7 == 0 ? 1 : trial % 5 == 0 ? 2 : 4 + trial % 9);

                for (positive i = 0; i < sizeof(row); i++)
                {
                        random ^= random << 13;
                        random ^= random >> 17;
                        random ^= random << 5;
                        row[i] = (p8)(random % spread);
                }
                for (positive offset = 0; offset < 8; offset++)
                        for (p8 tag = 0; tag < 4; tag++)
                        {
                                p64 want = 0;

                                for (positive i = 0; i < entries; i++)
                                        want |= (p64)(row[offset + i] == tag) << i;
                                same = same && zstd_row_mask(row + offset, tag, entries) == want;
                        }
        }
        check("the row finder's tag mask has one bit for each tag that equals the one asked", same);
}

/*
        A frame on the pool against the same frame in the serial decoder, and
        both kernels against their models. Every block a job decodes is held
        to zstd_sequences_decode_reference (the records, the totals, the
        largest offset, where the stand-ins end, the repeat offsets left and
        the answer) and every block exec copies to zstd_sequences_exec_reference
        (the bytes of the block). The frames come from the encoder at many
        levels over data of every shape, are cut and mutated, and the pool's
        answer -- bytes, count and reason -- must be the serial decoder's.
*/
static b32 par_audited, par_exec_audited, par_decode_bad, par_exec_bad;

static fn par_audit_decode(const zstd_seq_decode_job address_to before,
                           const zstd_seq_decode_job address_to after, bipolar result)
{
        zstd_seq_decode_job model = *before;
        p32 address_to const out = memory_take(12 * before->nseq + 16);
        p8 same = out != null;

        __atomic_fetch_add(address_of par_audited, 1, __ATOMIC_RELAXED);
        if (same)
        {
                bipolar want;

                model.out = out;
                want = zstd_sequences_decode_reference(address_of model);
                same = (want == 0) == (result == 0);
                if (same && !want)
                        same = after->max_off == model.max_off && after->sym_end == model.sym_end &&
                               after->sum_ll == model.sum_ll && after->sum_ml == model.sum_ml &&
                               after->rep[0] == model.rep[0] && after->rep[1] == model.rep[1] &&
                               after->rep[2] == model.rep[2] &&
                               !memory_compare(out, after->out, 12 * before->nseq);
        }
        if (!same)
                __atomic_fetch_add(address_of par_decode_bad, 1, __ATOMIC_RELAXED);
        memory_give(out);
}

static fn par_audit_exec(p8 address_to out, p8 address_to lits, p32 address_to recs, positive nseq)
{
        positive total = 0;
        p8 address_to copy;

        for (positive i = 0; i < nseq; i++)
                total += (positive)recs[3 * i] + recs[3 * i + 1];
        copy = memory_take(total + 1);
        __atomic_fetch_add(address_of par_exec_audited, 1, __ATOMIC_RELAXED);
        if (!copy)
        {
                __atomic_fetch_add(address_of par_exec_bad, 1, __ATOMIC_RELAXED);
                return;
        }
        memory_copy(copy, out, total);
        {
                zstd_seq_exec_job job = {out, lits, recs, nseq};

                zstd_sequences_exec_reference(address_of job);
        }
        if (memory_compare(copy, out, total))
                __atomic_fetch_add(address_of par_exec_bad, 1, __ATOMIC_RELAXED);
        memory_give(copy);
}

/* Data of every shape a block can be made of, in stretches: random bytes
   (raw blocks), runs (RLE blocks), words, records, copies at distances from
   one to seventy thousand and zeros with noise. */
static fn par_data(p8 address_to dst, positive n, p32 seed)
{
        static const positive reach[] = {1, 2, 3, 5, 8, 13, 40, 200, 3000, 70000};
        positive at = 0;
        p32 random = seed | 1;

        while (at < n)
        {
                positive stretch = 4096 + (XORSHIFT32(random) >> 8) % 90000;
                p8 const kind = (p8)((XORSHIFT32(random) >> 5) % 7);
                positive period = 24 + (random >> 9) % 70;

                if (stretch > n - at)
                        stretch = n - at;
                for (positive i = 0; i < stretch; i++, at++)
                {
                        XORSHIFT32(random);
                        if (kind == 0)
                                dst[at] = (p8)(random >> 11);
                        else if (kind == 1)
                                dst[at] = 'q';
                        else if (kind == 2)
                                dst[at] = (p8)('a' + (random >> 8) % 6 + ((random >> 16) % 5 == 0 ? 20 : 0));
                        else if (kind == 3)
                                dst[at] = (p8)(i % period < 6 ? (random >> 9) % 4 + i % period * 16 : 'k');
                        else if (kind == 4)
                                dst[at] = (random >> 12) % 61 ? 0 : (p8)(random >> 20);
                        else
                        {
                                positive back = reach[(random >> 7) % array_count(reach)];

                                dst[at] = at > back && (random & 7) ? dst[at - back] : (p8)(random >> 14);
                        }
                }
        }
}

static bipolar par_encode(p8 address_to src, positive n, p8 address_to dst, positive cap, b32 level)
{
        zstd_params params;
        p8 ok;

        zstd_output.bytes = dst;
        zstd_output.room = cap;
        zstd_output.used = 0;
        zstd_out_fd = -1;
        zstd_level_params(level, 0, 0, address_of params);
        ok = zstd_encode_start(address_of params, true) && zstd_encode_write(src, n) &&
             zstd_encode_end();
        zstd_output.bytes = null;
        return ok ? (bipolar)zstd_output.used : -1;
}

typedef struct
{
        bipolar n;
        string_address why;
} par_result;

static par_result par_decode_frame(p8 address_to frame, positive len, p8 address_to dst,
                                   positive cap, positive width)
{
        par_result r;

        parallel_reset(width);
        zstd_par_least = width > 1 ? 0 : ZSTD_PAR_LEAST;
        memory_fill(dst, 0xcc, cap);
        r.n = zstd_inflate(frame, len, dst, cap);
        r.why = zstd_why;
        parallel_reset(0);
        zstd_par_least = ZSTD_PAR_LEAST;
        return r;
}

static p8 par_same_text(string_address a, string_address b)
{
        return is_null(a) ? is_null(b) : !is_null(b) && !string_compare_max(a, b, 200);
}

/* The two decoders on one frame: the same count, the same bytes (whole
   buffer, so nothing is written that the serial decoder does not write) and
   the same reason. */
static p8 par_agree(p8 address_to frame, positive len, p8 address_to a, p8 address_to b, positive cap)
{
        par_result const serial = par_decode_frame(frame, len, a, cap, 1);
        par_result const pooled = par_decode_frame(frame, len, b, cap, 4);

        return serial.n == pooled.n && par_same_text(serial.why, pooled.why) &&
               !memory_compare(a, b, cap);
}

#define PAR_MAX (1u << 20)
static p8 par_src[PAR_MAX], par_frame[PAR_MAX + 4096], par_copy[PAR_MAX + 4096];
static p8 par_out_a[PAR_MAX + 4096], par_out_b[PAR_MAX + 4096];

/* Frames at every level over every shape of data, whole, with the models
   watching the pool's kernels. */
static fn par_frames(void)
{
        static const b32 levels[] = {1, 2, 3, 4, 5, 7, 9, 12, 15, 19};
        positive agreed = 0;
        positive tried = 0;
        p8 good = true;

        zstd_par_audit_decode = par_audit_decode;
        zstd_par_audit_exec = par_audit_exec;
        for (positive l = 0; l < array_count(levels); l++)
                for (positive seed = 1; seed <= 3; seed++)
                {
                        positive const n = (l < 7 ? 700000 : 300000) + seed * 41111;
                        bipolar packed;

                        par_data(par_src, n, (p32)(seed * 2654435761u + l));
                        packed = par_encode(par_src, n, par_frame, sizeof(par_frame), levels[l]);
                        if (packed <= 0)
                                continue;
                        tried++;
                        good &= par_agree(par_frame, (positive)packed, par_out_a, par_out_b, n + 64) &&
                                !memory_compare(par_out_b, par_src, n);
                        agreed++;
                }
        zstd_par_audit_decode = null;
        zstd_par_audit_exec = null;
        check("frames of every level and shape decode alike on the pool and serially, to their input",
              good && agreed == tried && tried >= 24);
        check("the pool decoded blocks the models looked at", par_audited > 100 && par_exec_audited > 100);
        check("the decode kernel is its model on every block", !par_decode_bad);
        check("exec is its model on every block", !par_exec_bad);
}

/* The same frames cut short and with bytes changed: whatever the serial
   decoder writes and says, the pool writes and says. */
static fn par_damage(void)
{
        static const b32 levels[] = {1, 3, 7, 12};
        p32 random = 0x2545f491u;
        positive agreed = 0;
        positive tried = 0;

        for (positive l = 0; l < array_count(levels); l++)
        {
                positive const n = 450000;
                bipolar packed;

                par_data(par_src, n, (p32)(l * 977 + 5));
                packed = par_encode(par_src, n, par_frame, sizeof(par_frame), levels[l]);
                if (packed <= 0)
                        continue;
                for (positive trial = 0; trial < 160; trial++)
                {
                        positive len = (positive)packed;

                        memory_copy(par_copy, par_frame, len);
                        XORSHIFT32(random);
                        if (trial % 4 == 0)
                                len = 6 + random % (len - 6);
                        else
                                for (positive k = 0; k <= trial % 3; k++)
                                {
                                        //      Half the changes land where a block's header, literal
                                        //      header and sequence modes are: the first bytes of a block.
                                        positive at = 6 + XORSHIFT32(random) % (len - 6);

                                        if (trial % 2)
                                                at &= ~(positive)0x1ffff, at += 6 + (random >> 17) % 40;
                                        if (at < len)
                                                par_copy[at] ^= (p8)(1u << (random >> 27) % 8);
                                }
                        tried++;
                        agreed += par_agree(par_copy, len, par_out_a, par_out_b, n + 64);
                }
        }
        check("damaged and cut frames decode to the same bytes and reasons on the pool and serially",
              agreed == tried && tried >= 400);
}

/* Several frames and a skippable one in a row, a trailing byte, an empty
   frame: the frame loop around the pool's path. */
static fn par_streams(void)
{
        positive const n = 600000;
        bipolar one;
        bipolar two;
        positive len;
        static const p8 skippable[] = {0x50, 0x2a, 0x4d, 0x18, 0x03, 0, 0, 0, 1, 2, 3};

        par_data(par_src, n, 99);
        one = par_encode(par_src, n, par_frame, PAR_MAX / 2, 3);
        two = par_encode(par_src, n / 2, par_frame + one, PAR_MAX / 2, 1);
        if (one <= 0 || two <= 0)
        {
                check("the frames for the stream checks are made", false);
                return;
        }
        len = (positive)(one + two);
        check("two frames in a row decode alike", par_agree(par_frame, len, par_out_a, par_out_b, n + n / 2 + 64));
        memory_copy(par_frame + len, skippable, sizeof(skippable));
        check("frames and a skippable frame decode alike",
              par_agree(par_frame, len + sizeof(skippable), par_out_a, par_out_b, n + n / 2 + 64));
        par_frame[len] = 0x7f;
        check("a trailing byte is refused alike", par_agree(par_frame, len + 1, par_out_a, par_out_b, n + n / 2 + 64));
        check("a missing last byte is refused alike", par_agree(par_frame, len - 1, par_out_a, par_out_b, n + n / 2 + 64));
}

/* Random tables for the decode kernel: counts that fill 2^log, with
   low-probability symbols now and then, drawn so offsets are mostly repeats
   and small codes; or one symbol and no log. */
static fn par_table(zstd_fse address_to table, p8 kind, p32 address_to random)
{
        static const positive top[3] = {35, 31, 52};
        static const p8 logs[3] = {9, 8, 9};
        bipolar norm[256];
        positive const max = top[kind];
        positive left;
        positive log;

        memory_fill(norm, 0, sizeof norm);
        if (XORSHIFT32(*random) % 7 == 0)
        {
                positive sym = (*random >> 6) % (max + 1);

                if (kind == 1 && sym > 24 && (*random & 16))
                        sym = (*random >> 11) % 3;
                zstd_fse_rle(table, (p8)sym);
                zstd_seq_fuse(table, kind);
                return;
        }
        log = 5 + (XORSHIFT32(*random) >> 4) % (positive)(logs[kind] - 4);
        left = (positive)1 << log;
        while (left)
        {
                positive sym = XORSHIFT32(*random) % (kind == 1 && (*random & 0x300) ? 5 : max + 1);
                positive take;

                if (kind == 1 && !(*random & 0xf000))
                        sym = 28 + (*random >> 8) % 4;
                if (sym > max)
                        sym = max;
                take = 1 + XORSHIFT32(*random) % (left > 3 ? left / 2 + 1 : left);
                if (norm[sym] == 0 && !((*random >> 9) % 9) && left > 1)
                {
                        norm[sym] = -1;
                        left--;
                        continue;
                }
                if (norm[sym] < 0)
                        continue;
                norm[sym] += (bipolar)take;
                left -= take;
        }
        zstd_seq_build(table, norm, max, (p8)log, kind);
}

/* A stream of nseq sequences over three tables from random bits, in the
   order they are read, then laid out from the top; flaws on request. */
static positive par_stream(p8 address_to dst, zstd_fse address_to table[3], positive nseq,
                           p32 address_to random)
{
        static p8 bits[1 << 20];
        positive used = 0;
        p32 state[3];
        positive bytes;

#define PAR_BITS(into, n)                                                      \
        do                                                                     \
        {                                                                      \
                p32 value_ = 0;                                                \
                for (positive i_ = 0; i_ < (n); i_++)                          \
                {                                                              \
                        p8 bit_ = (p8)(XORSHIFT32(*random) >> 13) & 1;         \
                        bits[used++] = bit_;                                   \
                        value_ = value_ << 1 | bit_;                           \
                }                                                              \
                into = value_;                                                 \
        } while (0)
        for (positive k = 0; k < 3; k++)
                PAR_BITS(state[k], table[k]->log);
        for (positive i = 0; i < nseq; i++)
        {
                zstd_fse_cell const of = table[1]->cell[state[1]];
                zstd_fse_cell const ml = table[2]->cell[state[2]];
                zstd_fse_cell const ll = table[0]->cell[state[0]];
                p32 skip;

                PAR_BITS(skip, of.extra);
                PAR_BITS(skip, ml.extra);
                PAR_BITS(skip, ll.extra);
                if (i + 1 < nseq)
                {
                        p32 more;

                        PAR_BITS(more, ll.bits);
                        state[0] = ll.next + more;
                        PAR_BITS(more, ml.bits);
                        state[2] = ml.next + more;
                        PAR_BITS(more, of.bits);
                        state[1] = of.next + more;
                }
        }
#undef PAR_BITS
        bytes = used / 8 + 1;
        memory_fill(dst, 0, bytes);
        for (positive k = 0; k < used; k++)
        {
                positive const g = used - 1 - k;

                dst[g / 8] |= (p8)(bits[k] << (g % 8));
        }
        dst[used / 8] |= (p8)(1 << (used % 8));
        return bytes;
}

/* The decode kernel against its model on random tables and streams that are
   what they say (every field read, the last bit the stream's last), then the
   same streams flawed: a bit flipped, a byte more, a byte less, the
   sentinel moved. Both answer alike, and when they succeed they leave alike. */
static fn par_decode_kernel(void)
{
        static zstd_fse tables[3];
        static p8 stream[1 << 16];
        static p32 out_a[3 * 700 + 8], out_b[3 * 700 + 8];
        p32 random = 0x9e3779b9u;
        positive agree = 0;
        positive total = 0;
        positive succeeded = 0;
        positive refused = 0;

        for (positive trial = 0; trial < 6000; trial++)
        {
                zstd_fse address_to use[3] = {address_of tables[0], address_of tables[1], address_of tables[2]};
                positive const nseq = 1 + XORSHIFT32(random) % (trial % 5 ? 40 : 600);
                positive bytes;
                p8 flaw = trial % 3 == 2;
                zstd_seq_decode_job a;
                zstd_seq_decode_job b;
                bipolar ra;
                bipolar rb;

                for (p8 k = 0; k < 3; k++)
                        par_table(address_of tables[k], k, address_of random);
                bytes = par_stream(stream, use, nseq, address_of random);
                if (flaw)
                {
                        p8 const how = (p8)(XORSHIFT32(random) >> 7) % 4;

                        if (how == 0)
                                stream[(random >> 9) % bytes] ^= (p8)(1u << (random >> 4) % 8);
                        else if (how == 1)
                                stream[bytes++] = (p8)(random >> 5);
                        else if (how == 2 && bytes > 1)
                                bytes--;
                        else
                                stream[bytes - 1] = (p8)((random >> 9) & 0xff);
                }
                a = (zstd_seq_decode_job){stream, bytes, use[0], use[1], use[2], nseq, out_a,
                                          {0xfff0ffffu, 0xfff1ffffu, 0xfff2ffffu}, 0, 0, 0, 0};
                b = a;
                b.out = out_b;
                ra = zstd_sequences_decode(address_of a);
                rb = zstd_sequences_decode_reference(address_of b);
                total++;
                if ((ra == 0) != (rb == 0))
                        continue;
                if (ra)
                {
                        refused++;
                        agree++;
                        continue;
                }
                succeeded++;
                agree += a.max_off == b.max_off && a.sym_end == b.sym_end && a.sum_ll == b.sum_ll &&
                         a.sum_ml == b.sum_ml && a.rep[0] == b.rep[0] && a.rep[1] == b.rep[1] &&
                         a.rep[2] == b.rep[2] && !memory_compare(out_a, out_b, 12 * nseq);
        }
        check("the decode kernel is its model on random tables and streams, whole and flawed",
              agree == total && succeeded > 3000 && refused > 500);
}

/* Records that keep exec's contract, made at random: literal lengths mostly
   zero, match lengths from the minimum up, offsets of every size in the
   neighbourhood of 8, 16 and 32 and some far, none past the bytes written. */
static positive par_records(p32 address_to recs, positive nseq, positive history, p32 address_to random,
                            positive address_to lit_total)
{
        static const p32 near[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 15, 16, 17, 24, 31, 32, 33, 47, 64, 100, 1000};
        positive out = history;
        positive total = 0;

        *lit_total = 0;
        for (positive i = 0; i < nseq; i++)
        {
                p32 ll = XORSHIFT32(*random) % 5 < 3 ? 0 : (*random >> 8) % (*random & 0x1000 ? 300 : 20);
                p32 ml = 3 + (XORSHIFT32(*random) >> 4) % ((*random & 0x700) ? 30 : 700);
                p32 off = (*random >> 9) & 1 ? near[(*random >> 10) % array_count(near)]
                                              : 1 + (*random >> 3) % (out + ll);

                if (off > out + ll)
                        off = 1 + (*random >> 3) % (out + ll);
                recs[3 * i] = ll;
                recs[3 * i + 1] = ml;
                recs[3 * i + 2] = off;
                out += ll + ml;
                total += ll + ml;
                *lit_total += ll;
        }
        return total;
}

static fn par_exec_kernel(void)
{
        static p8 lits[1 << 16];
        static p8 win_a[1 << 17], win_b[1 << 17];
        static p32 recs[3 * 80];
        positive const page = 4096;
        p8 address_to const map = memory(3 * page);
        p32 random = 0x7f4a7c15u;
        positive agree = 0;
        positive total_cases = 0;
        positive guarded = 0;
        p8 mapped = map && !system_failed(map);

        if (mapped)
                memory_free(map + 2 * page, page);
        for (positive trial = 0; trial < 3000; trial++)
        {
                positive const history = 2000 + XORSHIFT32(random) % 1000;
                positive const nseq = 1 + (random >> 6) % (trial % 4 ? 12 : 80);
                positive lit_total;
                positive total = par_records(recs, nseq, history, address_of random, address_of lit_total);
                zstd_seq_exec_job a;
                zstd_seq_exec_job b;
                p8 same;

                if (history + total + 64 > sizeof(win_a) || lit_total + 64 > sizeof(lits))
                        continue;
                for (positive i = 0; i < sizeof(lits); i++)
                        lits[i] = (p8)(XORSHIFT32(random) >> 11);
                for (positive i = 0; i < history; i++)
                        win_a[i] = win_b[i] = (p8)(XORSHIFT32(random) >> 13);
                memory_fill(win_a + history, 0xaa, sizeof(win_a) - history);
                memory_fill(win_b + history, 0xaa, sizeof(win_b) - history);
                a = (zstd_seq_exec_job){win_a + history, lits, recs, nseq};
                b = (zstd_seq_exec_job){win_b + history, lits, recs, nseq};
                zstd_sequences_exec(address_of a);
                zstd_sequences_exec_reference(address_of b);
                same = a.out == win_a + history + total && a.lits == lits + lit_total &&
                       !memory_compare(win_a, win_b, history + total);
                for (positive i = history + total + 32; i < sizeof(win_a); i++)
                        same &= win_a[i] == 0xaa;
                total_cases++;
                agree += same;

                //      The same block with its output, then its literals, then its
                //      records ending where the next page is not.
                if (mapped && total + 32 <= page - history && lit_total + 32 <= page && nseq * 12 <= page)
                {
                        p8 address_to const edge = map + 2 * page;
                        p8 address_to out_at = edge - total - 32;
                        p8 address_to lit_at = edge - lit_total - 32;
                        p32 address_to rec_at = (p32 address_to)(edge - nseq * 12);

                        for (positive mode = 0; mode < 3; mode++)
                        {
                                zstd_seq_exec_job g;
                                p8 address_to const base = mode == 0 ? out_at : map + page / 2 + history;
                                p8 address_to const lit_use = mode == 1 ? lit_at : lits;
                                p32 address_to const rec_use = mode == 2 ? rec_at : recs;

                                memory_copy(base - history, win_b, history);
                                if (mode == 1)
                                        memory_copy(lit_at, lits, lit_total);
                                if (mode == 2)
                                        memory_copy(rec_at, recs, nseq * 12);
                                g = (zstd_seq_exec_job){base, lit_use, rec_use, nseq};
                                zstd_sequences_exec(address_of g);
                                guarded += !memory_compare(base - history, win_b, history + total);
                        }
                }
        }
        check("exec is its model on random records of every length and offset",
              agree == total_cases && total_cases > 2000);
        check("exec stays inside the bytes its contract gives it at the edge of a mapping",
              !mapped || guarded > 3 * 500);
}

b32 main(void)
{
        pull_block_shapes();
        literal_codebooks();
        frames();
        dictionaries();
        bits();
        bits_model();
        sequence_capacity();
        huffman_exact_end();
        roundtrip();
        job_widths();
        fast_parse();
        row_mask();
        par_frames();
        par_damage();
        par_streams();
        par_decode_kernel();
        par_exec_kernel();
#if X64
        {
                p8 const probed = cpu_hash_probed;
                p8 const mulx = cpu_has_mulx;

                //      The sequence decoder and encoder without BMI2 are what
                //      the kernel and older processors run and a processor with
                //      it never would, so the decoding and round trip checks
                //      are walked once more with the flag down.
                cpu_hash_probed = 1;
                cpu_has_mulx = 0;
                frames();
                sequence_capacity();
                roundtrip();
                pull_block_shapes();
                dictionaries();
                par_frames();
                par_decode_kernel();
                cpu_hash_probed = probed;
                cpu_has_mulx = mulx;
        }
#endif
        return test_report(null);
}
#endif /* CHECK_zstd */

#ifdef CHECK_gzip
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#define GZIP_CORE_ONLY
#include "../../src/sh/gzip.c"
#undef GZIP_CORE_ONLY
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static p8 gzip_empty_py[] = {
        0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff,
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static p8 gzip_hello_py[] = {
        0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff,
        0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0xe7, 0x02, 0x00, 0x20, 0x30,
        0x3a, 0x36, 0x06, 0x00, 0x00, 0x00};

static p8 gzip_out_is(p8 address_to src, positive src_len, string_address want)
{
        p8 into[64];
        bipolar n = gzip_inflate_mem(src, src_len, into, sizeof(into));
        positive want_len = string_length(want);

        return n == (bipolar)want_len &&
               !memory_compare(into, want, want_len);
}

static fn members(void)
{
        p8 into[64];
        bipolar n;

        n = gzip_inflate_mem(gzip_empty_py, sizeof(gzip_empty_py), into,
                             sizeof(into));
        check("python empty gzip", n == 0);

        check("python hello gzip",
              gzip_out_is(gzip_hello_py, sizeof(gzip_hello_py), "hello\n"));

        n = gzip_inflate_mem(null, 0, into, sizeof(into));
        check("empty input fails", n < 0);

        check("ieee crc32 123456789",
              (hash_crc32(0xffffffffu, "123456789", 9) ^ 0xffffffffu) ==
                  0xcbf43926u);
        check("ieee crc32 empty",
              hash_crc32(0xffffffffu, null, 0) == 0xffffffffu);
        check("ecma crc64 123456789",
              (hash_crc64(0xffffffffffffffffull, "123456789", 9) ^
               0xffffffffffffffffull) == 0x995dc9bbdf1939faull);
}

static fn finder_tables(void)
{
        static gzip_encoder encoder;
        p16 address_to tables[] = {encoder.head4, encoder.head3, encoder.prev};
        const positive counts[] = {array_count(encoder.head4),
            array_count(encoder.head3), array_count(encoder.prev)};
        const p16 positions[] = {0, 1, GZIP_WINDOW - 1, GZIP_WINDOW,
            GZIP_WINDOW + 1, 65535};
        encoder.level = 6;
        encoder.slid = 123;
        encoder.mark = 456;
        for (positive table = 0; table < array_count(tables); table++)
                for (positive i = 0; i < counts[table]; i++)
                        tables[table][i] = positions[i % array_count(positions)];
        gzip_slide(&encoder);
        for (positive table = 0; table < array_count(tables); table++)
        {
                p8 same = true;
                for (positive i = 0; i < counts[table]; i++)
                {
                        p16 before = positions[i % array_count(positions)];
                        p16 expected = before > GZIP_WINDOW ? before - GZIP_WINDOW : 0;
                        same &= tables[table][i] == expected;
                }
                check("gzip slides every finder table with saturating positions", same);
        }
        check("gzip slide updates origin once and preserves neighboring state",
              encoder.slid == 123 + GZIP_WINDOW && encoder.level == 6 && encoder.mark == 456);
        encoder.hash3 = encoder.hash4 = 99;
        gzip_finder_open(&encoder);
        for (positive table = 0; table < array_count(tables); table++)
                check("gzip reset clears exactly each finder table",
                      memory_span_byte(tables[table], 0, counts[table] * sizeof(p16)) ==
                          counts[table] * sizeof(p16));
        check("gzip reset clears hash state and preserves neighboring fields",
              !encoder.slid && !encoder.hash3 && !encoder.hash4 &&
              encoder.level == 6 && encoder.mark == 456);
}

static fn roundtrip(void)
{
        p8 src[4096];
        p8 packed[8192];
        p8 back[4096];
        bipolar n;
        bipolar m;
        positive at;

        n = gzip_deflate_mem((p8 address_to) "", 0, packed, sizeof(packed), 6);
        check("encode empty", n > 0);
        m = gzip_inflate_mem(packed, (positive)n, back, sizeof(back));
        check("decode empty encode", m == 0);

        n = gzip_deflate_mem((p8 address_to) "hello\n", 6, packed,
                             sizeof(packed), 1);
        check("encode hello level 1", n > 0);
        check("hello level 1 roundtrip",
              gzip_out_is(packed, (positive)n, "hello\n"));

        n = gzip_deflate_mem((p8 address_to) "hello\n", 6, packed,
                             sizeof(packed), 9);
        check("encode hello level 9", n > 0);
        check("hello level 9 roundtrip",
              gzip_out_is(packed, (positive)n, "hello\n"));

        for (at = 0; at < sizeof(src); at++)
                src[at] = (p8)(at * 3 + 7);
        n = gzip_deflate_mem(src, sizeof(src), packed, sizeof(packed), 6);
        check("encode 4k", n > 0 && n < (bipolar)sizeof(packed));
        m = gzip_inflate_mem(packed, (positive)n, back, sizeof(back));
        check("4k roundtrip",
              m == (bipolar)sizeof(src) && !memory_compare(back, src, sizeof(src)));

        memory_fill(src, 'a', sizeof(src));
        n = gzip_deflate_mem(src, sizeof(src), packed, sizeof(packed), 6);
        check("encode repeated", n > 0 && n < 64);
        m = gzip_inflate_mem(packed, (positive)n, back, sizeof(back));
        check("repeated roundtrip",
              m == (bipolar)sizeof(src) && !memory_compare(back, src, sizeof(src)));
}

static fn large_roundtrip(void)
{
        static p8 src[65536];
        static p8 packed[8192];
        static p8 back[65536];
        bipolar n;
        bipolar m;

        memory_fill(src, 'a', sizeof(src));
        n = gzip_deflate_mem(src, sizeof(src), packed, sizeof(packed), 9);
        check("encode 64k repeated", n > 0 && n < (bipolar)sizeof(packed));
        m = gzip_inflate_mem(packed, (positive)n, back, sizeof(back));
        check("64k repeated roundtrip",
              m == (bipolar)sizeof(src) && !memory_compare(back, src, sizeof(src)));
        n = gzip_deflate_mem(src, sizeof(src), packed, sizeof(packed), GZIP_ULTRA);
        check("ultra encode 64k repeated", n > 0 && n < (bipolar)sizeof(packed));
        m = gzip_inflate_mem(packed, (positive)n, back, sizeof(back));
        check("ultra 64k repeated roundtrip",
              m == (bipolar)sizeof(src) && !memory_compare(back, src, sizeof(src)));
}

/* Every level, the optimal parser's included, over text-like data with
   long and short repeats, and over bytes with nothing to find. */
static fn every_level(void)
{
        static p8 src[400000];
        static p8 packed[500000];
        static p8 back[400000];
        p32 random = 0x9e3779b9u;

        for (positive kind = 0; kind < 2; kind++)
        {
                for (positive i = 0; i < sizeof(src); i++)
                {
                        XORSHIFT32(random);
                        src[i] = kind ? (p8)random
                                      : (i > 300 && (random & 7) < 6) ? src[i - 1 - (random >> 8) % 250]
                                                                       : (p8)('a' + (random >> 20) % 12);
                }
                for (positive level = 1; level <= GZIP_ULTRA; level++)
                {
                        bipolar n = gzip_deflate_mem(src, sizeof(src), packed, sizeof(packed),
                                                     (p8)level);
                        bipolar m = n > 0 ? gzip_inflate_mem(packed, (positive)n, back, sizeof(back))
                                          : -1;

                        check("every level decodes to its input",
                              m == (bipolar)sizeof(src) && !memory_compare(back, src, sizeof(src)));
                }
        }
}

/* Fixed blocks through the pool: the same bytes at every width. */
static fn threaded(positive level)
{
        static p8 src[3 * 1048576 + 12345];
        static p8 first[3 * 1048576 + 65536];
        static p8 again[3 * 1048576 + 65536];
        static p8 back[3 * 1048576 + 12345];
        const positive widths[] = {1, 2, 3, 8};
        p32 random = 0x2545f491u;
        bipolar n1 = -1;

        for (positive i = 0; i < sizeof(src); i++)
        {
                XORSHIFT32(random);
                src[i] = (i / 4096) % 3 ? (p8)(random & 7) : (p8)random;
        }
        for (positive w = 0; w < array_count(widths); w++)
        {
                parallel_reset(widths[w]);
                bipolar n = gzip_deflate_mem(src, sizeof(src), w ? again : first,
                                             sizeof(first), (p8)level);
                if (!w)
                {
                        n1 = n;
                        check("gzip blocks encode at width 1", n > 0);
                        continue;
                }
                check("gzip blocks are the same bytes at widths 2, 3 and 8",
                      n == n1 && n > 0 && !memory_compare(first, again, (positive)n));
        }
        parallel_reset(0);
        bipolar m = n1 > 0 ? gzip_inflate_mem(first, (positive)n1, back, sizeof(back)) : -1;
        check("gzip blocks decode back to the input",
              m == (bipolar)sizeof(src) && !memory_compare(back, src, sizeof(src)));
}

/*
        One deflate stream on many cores. The pool's decode of a member held in
        memory is held to the serial decoder's: the same bytes in the same order
        whole, and when the pool gives it up (a damaged stream, a second member,
        anything the scan or the chunks do not agree on) the serial decoder, run
        from the start leaving unwritten what the pool wrote, ends with the same
        bytes and the same reason as the serial decoder alone. The streams are
        the encoder's at several levels over data that copies from far back
        (so the chunks begin without their window), cut into chunks of 16 KiB
        so there are many.
*/
#define GZ_MAX (3u << 20)
static p8 gz_src[GZ_MAX], gz_packed[GZ_MAX + 65536], gz_copy[GZ_MAX + 65536];
static p8 gz_a[2 * GZ_MAX], gz_b[2 * GZ_MAX];

static fn gz_data(p8 address_to dst, positive n, p32 seed)
{
        static const positive reach[] = {1, 2, 3, 5, 8, 13, 40, 200, 3000, 20000, 32000};
        positive at = 0;
        p32 random = seed | 1;

        while (at < n)
        {
                positive stretch = 3000 + (XORSHIFT32(random) >> 8) % 60000;
                p8 const kind = (p8)((XORSHIFT32(random) >> 5) % 6);
                positive period = 24 + (random >> 9) % 70;

                if (stretch > n - at)
                        stretch = n - at;
                for (positive i = 0; i < stretch; i++, at++)
                {
                        XORSHIFT32(random);
                        if (kind == 0)
                                dst[at] = (p8)(random >> 11);
                        else if (kind == 1)
                                dst[at] = 'q';
                        else if (kind == 2)
                                dst[at] = (p8)('a' + (random >> 8) % 6);
                        else if (kind == 3)
                                dst[at] = (p8)(i % period < 6 ? (random >> 9) % 4 + i % period * 16 : 'k');
                        else
                        {
                                positive back = reach[(random >> 7) % array_count(reach)];

                                dst[at] = at > back && (random & 15) ? dst[at - back] : (p8)(random >> 14);
                        }
                }
        }
}

static bipolar gz_memfd(const p8 address_to bytes, positive n)
{
        bipolar fd = (bipolar)system_call_2(syscall(memfd_create), (positive)"gzip pool", 0);

        if (fd >= 0 && n && system_write_all((positive)fd, (p8 address_to)bytes, n) != (bipolar)n)
                return -1;
        system_seek(fd, 0, FILE_SEEK_SET);
        return fd;
}

static positive gz_slurp(bipolar fd, p8 address_to into, positive cap)
{
        positive got = 0;

        system_seek(fd, 0, FILE_SEEK_SET);
        for (;;)
        {
                bipolar n = system_read_retry((positive)fd, into + got, cap - got);

                if (n <= 0)
                        break;
                got += (positive)n;
        }
        return got;
}

typedef struct
{
        p8 ok;
        positive length;
        string_address why;
        bipolar pooled;
} gz_answer;

/* The member through the serial decoder alone, or through the pool first. */
static gz_answer gz_decode(const p8 address_to member, positive len, p8 address_to into, p8 pooled)
{
        gz_answer r = {false, 0, null, 0};
        bipolar const in = gz_memfd(member, len);
        bipolar const out = gz_memfd(null, 0);

        if (in < 0 || out < 0)
                return r;
        gzip_skip = 0;
        if (pooled)
        {
                r.pooled = gzip_parallel_mem(member, len, out);
                if (r.pooled == 1)
                {
                        r.ok = true;
                        r.length = gz_slurp(out, into, 2 * GZ_MAX);
                        system_close((positive)in);
                        system_close((positive)out);
                        return r;
                }
                system_seek(in, 0, FILE_SEEK_SET);
        }
        r.ok = gzip_stream_serial(in, out);
        gzip_skip = 0;
        r.length = gz_slurp(out, into, 2 * GZ_MAX);
        r.why = gzip_note;
        system_close((positive)in);
        system_close((positive)out);
        return r;
}

static p8 gz_same_note(string_address a, string_address b)
{
        return is_null(a) ? is_null(b) : !is_null(b) && !string_compare_max(a, b, 90);
}

static p8 gz_agree(const p8 address_to member, positive len)
{
        gz_answer const alone = gz_decode(member, len, gz_a, false);
        gz_answer const both = gz_decode(member, len, gz_b, true);

        return alone.length == both.length && alone.ok == both.ok &&
               (both.pooled == 1 || gz_same_note(alone.why, both.why)) &&
               !memory_compare(gz_a, gz_b, alone.length);
}

static fn gz_pool(void)
{
        static const b32 levels[] = {1, 6, 9};
        p32 random = 0x2545f491u;
        positive pooled = 0;
        positive total = 0;
        positive whole = 0;
        p8 good = true;
        p8 good_whole = true;
        p8 good_cut = true;
        p8 good_more = true;
        p8 good_double = true;
        p8 good_flips = true;

        parallel_reset(4);
        gzip_par_least = 0;
        gzip_par_chunk_least = 16384;
        for (positive l = 0; l < array_count(levels); l++)
                for (positive seed = 1; seed <= 3; seed++)
                {
                        positive const n = 900000 + seed * 100003;
                        bipolar packed;
                        gz_answer r;

                        gz_data(gz_src, n, (p32)(seed * 2654435761u + l));
                        packed = gzip_deflate_mem(gz_src, n, gz_packed, sizeof(gz_packed), levels[l]);
                        if (packed <= 0)
                                continue;
                        r = gz_decode(gz_packed, (positive)packed, gz_b, true);
                        total++;
                        pooled += r.pooled == 1;
                        whole += r.ok && r.length == n && !memory_compare(gz_b, gz_src, n);
                        good_whole &= gz_agree(gz_packed, (positive)packed);
                        //      Damage: bytes changed, the end cut, a byte more, two members.
                        for (positive trial = 0; trial < 14; trial++)
                        {
                                positive cut = (positive)packed;

                                memory_copy(gz_copy, gz_packed, cut);
                                XORSHIFT32(random);
                                if (trial % 7 == 0)
                                        cut -= 1 + random % 2000;
                                else if (trial % 7 == 1)
                                {
                                        gz_copy[cut++] = (p8)random;
                                }
                                else if (trial % 7 == 2 && cut * 2 < sizeof(gz_copy))
                                {
                                        memory_copy(gz_copy + cut, gz_packed, cut);
                                        cut *= 2;
                                }
                                else
                                        for (positive k = 0; k <= trial % 3; k++)
                                                gz_copy[10 + XORSHIFT32(random) % (cut - 10)] ^=
                                                    (p8)(1u << (random >> 27) % 8);
                                if (trial % 7 == 0)
                                        good_cut &= gz_agree(gz_copy, cut);
                                else if (trial % 7 == 1)
                                        good_more &= gz_agree(gz_copy, cut);
                                else if (trial % 7 == 2)
                                        good_double &= gz_agree(gz_copy, cut);
                                else
                                        good_flips &= gz_agree(gz_copy, cut);
                        }
                }
        parallel_reset(0);
        gzip_par_least = GZIP_PAR_LEAST;
        gzip_par_chunk_least = (positive)1 << 20;
        check("members at three levels decode on the pool to their input", total >= 9 && whole == total);
        check("most of them take the pool's path to the end", pooled >= total - 1);
        check("the pool and the serial decoder agree on whole members", good_whole);
        check("the pool and the serial decoder agree on members cut short", good_cut);
        check("the pool and the serial decoder agree on members with a byte after", good_more);
        check("the pool and the serial decoder agree on two members in a row", good_double);
        check("the pool and the serial decoder agree on members with bits flipped", good_flips);
}

/* A checksum of two runs joined, from the checksum of each. */
static fn gz_combine(void)
{
        p32 random = 0x9e3779b9u;
        p8 good = true;

        for (positive trial = 0; trial < 300; trial++)
        {
                positive const total = 1 + XORSHIFT32(random) % 5000;
                positive const cut = (random >> 13) % (total + 1);
                static p8 data[5000];
                p32 whole;
                p32 left;
                p32 right;

                for (positive i = 0; i < total; i++)
                        data[i] = (p8)(XORSHIFT32(random) >> 9);
                whole = ~hash_crc32(0xffffffffu, data, total);
                left = ~hash_crc32(0xffffffffu, data, cut);
                right = ~hash_crc32(0xffffffffu, data + cut, total - cut);
                good &= gzip_par_combine(left, right, total - cut) == whole;
        }
        check("the checksum of two runs joined is the checksum of the whole", good);
}

/* The scan: random bytes have no block header in them, and the headers of a
   member's dynamic blocks are found where they are. */
static fn gz_scan(void)
{
        static p8 noise[1 << 20];
        p32 random = 0x7f4a7c15u;
        bipolar packed;
        positive found = 0;
        p64 at = 0;

        for (positive i = 0; i < sizeof(noise); i++)
                noise[i] = (p8)(XORSHIFT32(random) >> 11);
        check("a megabyte of noise holds no dynamic block header",
              gzip_par_find(noise, sizeof(noise), 0, (p64)sizeof(noise) * 8 - 64) == GZIP_NONE);
        gz_data(gz_src, 800000, 4242);
        packed = gzip_deflate_mem(gz_src, 800000, gz_packed, sizeof(gz_packed), 6);
        check("a member to scan", packed > 0);
        if (packed <= 0)
                return;
        while (at < (p64)packed * 8 - 640)
        {
                p64 const next = gzip_par_find(gz_packed, (positive)packed, at, (p64)packed * 8 - 640);

                if (next == GZIP_NONE)
                        break;
                found++;
                at = next + 1;
        }
        check("the scan finds the dynamic blocks of a member (and a few chances)", found >= 3);
}

b32 main(void)
{
        members();
        finder_tables();
        roundtrip();
        large_roundtrip();
        every_level();
        threaded(6);
        threaded(GZIP_ULTRA);
        gz_combine();
        gz_scan();
        gz_pool();
        return test_report(null);
}
#endif /* CHECK_gzip */

#ifdef CHECK_xz
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#define XZ_CORE_ONLY
#include "../../src/sh/xz.c"
#undef XZ_CORE_ONLY
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static p8 xz_empty_py[] = {
        0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00, 0x00, 0x04, 0xe6, 0xd6, 0xb4,
        0x46, 0x00, 0x00, 0x00, 0x00, 0x1c, 0xdf, 0x44, 0x21, 0x1f, 0xb6,
        0xf3, 0x7d, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x59, 0x5a};
static p8 xz_hello_py[] = {
        0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00, 0x00, 0x04, 0xe6, 0xd6, 0xb4,
        0x46, 0x02, 0x00, 0x21, 0x01, 0x0c, 0x00, 0x00, 0x00, 0x8f, 0x98,
        0x41, 0x9c, 0x01, 0x00, 0x05, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x0a,
        0x00, 0x00, 0x00, 0xa5, 0x60, 0x97, 0xf1, 0x94, 0xf6, 0xfd, 0xe0,
        0x00, 0x01, 0x1e, 0x06, 0xc1, 0x2f, 0xa4, 0x1d, 0x1f, 0xb6, 0xf3,
        0x7d, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x59, 0x5a};

static p8 xz_sha_hello_py[] = {
        0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00, 0x00, 0x0a, 0xe1, 0xfb, 0x0c,
        0xa1, 0x02, 0x00, 0x21, 0x01, 0x16, 0x00, 0x00, 0x00, 0x74, 0x2f,
        0xe5, 0xa3, 0x01, 0x00, 0x0c, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x20,
        0x73, 0x68, 0x61, 0x32, 0x35, 0x36, 0x0a, 0x00, 0x00, 0x00, 0x00,
        0x02, 0xf3, 0x52, 0xe1, 0x83, 0xfd, 0xf9, 0xdc, 0xc6, 0x08, 0xdf,
        0x7f, 0xfe, 0x7a, 0xeb, 0xe0, 0x87, 0x20, 0x7d, 0x44, 0x62, 0x80,
        0x3d, 0x41, 0xb1, 0x18, 0xee, 0x28, 0xf4, 0x08, 0x30, 0x61, 0x00,
        0x01, 0x3d, 0x0d, 0x28, 0x81, 0xdf, 0x34, 0x18, 0x9b, 0x4b, 0x9a,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x59, 0x5a};
static p8 xz_sha_text_py[] = {
        0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00, 0x00, 0x0a, 0xe1, 0xfb, 0x0c,
        0xa1, 0x02, 0x00, 0x21, 0x01, 0x16, 0x00, 0x00, 0x00, 0x74, 0x2f,
        0xe5, 0xa3, 0xe0, 0x0d, 0x77, 0x01, 0x38, 0x5d, 0x00, 0x2a, 0x1a,
        0x08, 0xa2, 0x03, 0x5f, 0x2e, 0xbc, 0x2b, 0x6c, 0xc5, 0x6c, 0xef,
        0x7c, 0xcd, 0x5f, 0xed, 0x72, 0x73, 0xd9, 0x18, 0x0b, 0xc8, 0x27,
        0x61, 0xc7, 0x74, 0x23, 0x11, 0x2a, 0xe8, 0x24, 0xc6, 0x04, 0x3d,
        0x2d, 0x54, 0xfd, 0x9a, 0xf5, 0x08, 0xb0, 0x01, 0x87, 0xe8, 0x5f,
        0x59, 0x6c, 0x4c, 0xfc, 0x92, 0xde, 0xb7, 0xe8, 0xe1, 0x78, 0xc8,
        0x41, 0x8d, 0x3e, 0x6c, 0x65, 0x6a, 0x62, 0x13, 0x83, 0x22, 0x5d,
        0x84, 0x6b, 0xa1, 0x91, 0x42, 0x5f, 0xed, 0xca, 0x87, 0x42, 0x63,
        0x87, 0x77, 0x4a, 0x15, 0x8d, 0x9a, 0xe7, 0xb4, 0x0b, 0xf5, 0x87,
        0xfb, 0x14, 0x4c, 0x73, 0xea, 0xf7, 0x90, 0x11, 0x0f, 0x0c, 0xf3,
        0xb9, 0xd2, 0x15, 0xf6, 0x70, 0xcd, 0xaf, 0x9e, 0x2a, 0xa3, 0x01,
        0x12, 0x61, 0xdd, 0xd6, 0x78, 0xd2, 0xad, 0x94, 0xd7, 0xc4, 0x67,
        0xb7, 0x82, 0x62, 0xb3, 0x4f, 0xbc, 0x34, 0xa9, 0xc4, 0xdb, 0x42,
        0xb2, 0x82, 0x57, 0x80, 0x78, 0x2a, 0x85, 0x45, 0xda, 0xc2, 0x65,
        0x9f, 0xa3, 0xc6, 0x76, 0x1e, 0x4f, 0xf7, 0x92, 0x4c, 0x5f, 0xf4,
        0x08, 0xe2, 0xa4, 0xe1, 0xfa, 0x69, 0xed, 0xae, 0x8e, 0x8d, 0xb0,
        0x35, 0x29, 0x8d, 0x4a, 0x6e, 0xf3, 0x73, 0x9c, 0x9a, 0xcf, 0xb1,
        0x92, 0x52, 0xc4, 0xa4, 0x6f, 0x8c, 0x65, 0x6a, 0x3d, 0x52, 0x0d,
        0x8a, 0x83, 0xf5, 0x83, 0xe7, 0xb5, 0x58, 0xc2, 0x72, 0xcf, 0x3a,
        0x25, 0x31, 0xb7, 0x93, 0x20, 0x0a, 0x69, 0x7c, 0xdb, 0xf3, 0xc9,
        0x58, 0x75, 0xdb, 0x65, 0x4c, 0x09, 0x68, 0xd1, 0x5e, 0x38, 0xd4,
        0xa1, 0xb9, 0x5c, 0x4e, 0x8b, 0x88, 0xe8, 0x7d, 0x37, 0xb7, 0x3e,
        0xb6, 0x27, 0x65, 0xff, 0xf6, 0x2a, 0x09, 0xaa, 0x5d, 0x2b, 0xe7,
        0x3d, 0x81, 0xa2, 0x2a, 0x88, 0x41, 0x71, 0x9e, 0xf0, 0xe3, 0x16,
        0xdf, 0xe5, 0x7d, 0xa2, 0x01, 0xeb, 0xbc, 0x71, 0x32, 0xd9, 0xb6,
        0x6c, 0x70, 0xf5, 0x8d, 0xc5, 0xf6, 0xe0, 0xec, 0x4b, 0xe4, 0xbf,
        0x1e, 0x37, 0x0c, 0x36, 0xe0, 0xff, 0xfb, 0xd5, 0x9d, 0x60, 0xe6,
        0xa2, 0x61, 0x8a, 0x25, 0xa5, 0x24, 0x44, 0x75, 0xd3, 0xb4, 0xdb,
        0xaf, 0x00, 0xe1, 0x26, 0x4d, 0x9a, 0xc3, 0x4b, 0x32, 0x8c, 0x02,
        0x24, 0x6a, 0x00, 0xdb, 0xbb, 0x1f, 0xb1, 0xa0, 0x85, 0x45, 0x30,
        0xc4, 0xd0, 0x32, 0x08, 0x0d, 0x7c, 0xfb, 0x97, 0x66, 0x42, 0xcd,
        0x9f, 0x16, 0x1d, 0xf6, 0x37, 0x96, 0xaf, 0x92, 0xd2, 0xb9, 0x14,
        0x4b, 0x95, 0x00, 0x01, 0xec, 0x02, 0xf8, 0x1a, 0x00, 0x00, 0xaa,
        0x44, 0x70, 0x8a, 0xb6, 0xe9, 0xdf, 0x1c, 0x02, 0x00, 0x00, 0x00,
        0x00, 0x0a, 0x59, 0x5a};

static p8 xz_out_is(p8 address_to src, positive src_len, string_address want)
{
        p8 into[64];
        bipolar n = xz_inflate_mem(src, src_len, into, sizeof(into));
        positive want_len = string_length(want);

        return n == (bipolar)want_len &&
               !memory_compare(into, want, want_len);
}

static fn members(void)
{
        p8 into[64];
        bipolar n;

        n = xz_inflate_mem(xz_empty_py, sizeof(xz_empty_py), into, sizeof(into));
        check("python empty xz", n == 0);

        check("python hello xz",
              xz_out_is(xz_hello_py, sizeof(xz_hello_py), "hello\n"));

        n = xz_inflate_mem(null, 0, into, sizeof(into));
        check("empty input fails", n < 0);
}

static fn roundtrip(void)
{
        p8 src[256];
        p8 packed[1024];
        p8 back[256];
        bipolar n;
        bipolar m;
        positive at;

        n = xz_deflate_mem((p8 address_to) "", 0, packed, sizeof(packed), 6);
        check("encode empty", n > 0);
        m = xz_inflate_mem(packed, (positive)n, back, sizeof(back));
        check("decode empty encode", m == 0);

        n = xz_deflate_mem((p8 address_to) "hello\n", 6, packed, sizeof(packed),
                           1);
        check("encode hello", n > 0);
        check("hello roundtrip",
              xz_out_is(packed, (positive)n, "hello\n"));

        for (at = 0; at < sizeof(src); at++)
                src[at] = (p8)(at * 3 + 7);
        n = xz_deflate_mem(src, sizeof(src), packed, sizeof(packed), 6);
        check("encode 256", n > 0);
        m = xz_inflate_mem(packed, (positive)n, back, sizeof(back));
        check("256 roundtrip",
              m == (bipolar)sizeof(src) && !memory_compare(back, src, sizeof(src)));

        {
                p8 many[4096];
                p8 packed2[8192];
                p8 back2[4096];

                memory_fill(many, 'a', sizeof(many));
                n = xz_deflate_mem(many, sizeof(many), packed2, sizeof(packed2),
                                   6);
                check("encode repeated", n > 0 && n < 256);
                m = xz_inflate_mem(packed2, (positive)n, back2, sizeof(back2));
                check("repeated roundtrip",
                      m == (bipolar)sizeof(many) &&
                          !memory_compare(back2, many, sizeof(many)));
        }
}

static fn large_roundtrip(void)
{
        static p8 src[65536];
        static p8 packed[8192];
        static p8 back[65536];
        bipolar n;
        bipolar m;

        memory_fill(src, 'a', sizeof(src));
        n = xz_deflate_mem(src, sizeof(src), packed, sizeof(packed), 6);
        check("encode 64k repeated", n > 0 && n < (bipolar)sizeof(packed));
        m = xz_inflate_mem(packed, (positive)n, back, sizeof(back));
        check("64k repeated roundtrip",
              m == (bipolar)sizeof(src) && !memory_compare(back, src, sizeof(src)));
}

static fn streamed(void)
{
        static p8 src[65536 + 1024];
        static p8 packed[8192];
        static p8 back[65536 + 1024];
        positive at;
        p8 wrote = true;
        bipolar m;

        memory_fill(src, 'a', sizeof(src));
        xz_input.fd = -1;
        xz_out_fd = -1;
        xz_output.bytes = packed;
        xz_output.room = sizeof(packed);
        xz_output.used = 0;
        check("stream begin", xz_encode_setup(6));
        for (at = 0; at < sizeof(src); at += 512)
        {
                positive n = sizeof(src) - at;

                if (n > 512)
                        n = 512;
                if (!xz_encode_write(src + at, n))
                        wrote = false;
        }
        check("stream writes", wrote);
        check("stream end", xz_encode_end());
        check("stream size",
              xz_output.used > 0 && xz_output.used < sizeof(packed));
        m = xz_inflate_mem(packed, xz_output.used, back, sizeof(back));
        check("stream roundtrip",
              m == (bipolar)sizeof(src) &&
                  !memory_compare(back, src, sizeof(src)));
        xz_output.bytes = null;
}

/* Preset blocks through the pool: the same bytes at every width. */
static fn threaded(void)
{
        static p8 src[3 * 1048576 + 12345];
        static p8 first[3 * 1048576 + 65536];
        static p8 again[3 * 1048576 + 65536];
        static p8 back[3 * 1048576 + 12345];
        const positive widths[] = {1, 2, 3, 8};
        p32 random = 0x2545f491u;
        bipolar n1 = -1;

        for (positive i = 0; i < sizeof(src); i++)
        {
                XORSHIFT32(random);
                src[i] = (i / 4096) % 3 ? (p8)(random & 7) : (p8)random;
        }
        for (positive w = 0; w < array_count(widths); w++)
        {
                parallel_reset(widths[w]);
                bipolar n = xz_deflate_mem(src, sizeof(src), w ? again : first,
                                           sizeof(first), 0);
                if (!w)
                {
                        n1 = n;
                        check("xz blocks encode at width 1", n > 0);
                        continue;
                }
                check("xz blocks are the same bytes at widths 2, 3 and 8",
                      n == n1 && n > 0 && !memory_compare(first, again, (positive)n));
        }
        parallel_reset(0);
        bipolar m = n1 > 0 ? xz_inflate_mem(first, (positive)n1, back, sizeof(back)) : -1;
        check("xz blocks decode back to the input",
              m == (bipolar)sizeof(src) && !memory_compare(back, src, sizeof(src)));
}

/* A stream of two megabytes that compresses unevenly. */
static fn xz_fixture(p8 address_to src, positive n, p32 seed)
{
        p32 random = seed;

        for (positive i = 0; i < n; i++)
        {
                XORSHIFT32(random);
                src[i] = (i / 4096) % 3 ? (p8)(random & 7) : (p8)random;
        }
}

/* The pull interface over a descriptor that holds all but a prefix. */
static fn pulled(void)
{
        static p8 src[2 * 1048576 + 777];
        static p8 packed[3 * 1048576];
        static p8 back[2 * 1048576 + 777];
        bipolar n;
        bipolar handle;

        xz_fixture(src, sizeof(src), 0x13572468u);
        n = xz_deflate_mem(src, sizeof(src), packed, sizeof(packed), 6);
        check("pull fixture encodes", n > 64);
        handle = system_call_2(syscall(memfd_create), (positive)"xz-pull", 0);
        check("pull fixture descriptor", handle >= 0);
        if (n <= 64 || handle < 0)
                return;
        for (positive pass = 0; pass < 3; pass++)
        {
                if (pass == 2)
                        packed[n / 2] ^= 0x40;
                p8 prepared = system_seek(handle, 0, FILE_SEEK_SET) == 0 &&
                                system_truncate_handle(handle, 0) == 0 &&
                                system_write_all(handle, packed + 7, (positive)n - 7) == n - 7 &&
                                system_seek(handle, 0, FILE_SEEK_SET) == 0;
                check("pull fixture prepared", prepared);
                if (!prepared)
                        break;

                address_any d = xz_pull_open(handle, packed, 7);
                positive got = 0;
                bipolar last = 0;

                check("pull opens", d != null);
                if (!d)
                        break;
                for (positive turn = 0;; turn++)
                {
                        if (pass == 0)
                        {
                                p8 address_to span = null;

                                last = xz_pull_span(d, address_of span);
                                if (last > 0 && got + (positive)last <= sizeof(back))
                                        memory_copy_apart(back + got, span, (positive)last);
                        }
                        else
                        {
                                positive want = turn % 3 == 0 ? 1 : turn % 3 == 1 ? 4099 : 65537;

                                if (want > sizeof(back) - got)
                                        want = sizeof(back) - got;
                                last = want ? xz_pull_read(d, back + got, want) : 0;
                        }
                        if (last <= 0 || got + (positive)last > sizeof(back))
                                break;
                        got += (positive)last;
                }
                if (pass < 2)
                {
                        p8 rebuilt = last == 0 && got == sizeof(src) &&
                                       !memory_compare(back, src, sizeof(src)) &&
                                       !xz_pull_error(d);

                        if (pass)
                                check("pull reads rebuild the input", rebuilt);
                        else
                                check("pull spans rebuild the input", rebuilt);
                        check("pull close reports success", xz_pull_close(d));
                }
                else
                {
                        check("pull reports a corrupt stream by name",
                              last < 0 && xz_pull_error(d) != null);
                        check("pull close reports the failure", !xz_pull_close(d));
                        packed[n / 2] ^= 0x40;
                }
        }
        system_call_1(syscall(close), handle);
}

static positive xz_index_vli(p8 address_to bytes, positive address_to at)
{
        positive value = 0;

        for (positive shift = 0;; shift += 7)
        {
                p8 byte = bytes[address_to at];

                address_to at += 1;
                value |= (positive)(byte & 0x7f) << shift;
                if (!(byte & 0x80))
                        return value;
        }
}

/* Each block of a many-block stream on its own call, located through the
   index, in reverse order and on one reused decoder. */
static fn blocks(void)
{
        static p8 src[3 * 1048576 + 4321];
        static p8 packed[4 * 1048576];
        static p8 back[3 * 1048576 + 4321];
        static positive starts[64];
        static positive lengths[64];
        static positive sizes[64];
        static positive offsets[64];
        p8 check_bytes[32];
        bipolar n;

        xz_fixture(src, sizeof(src), 0x2545f491u);
        //      -0 writes blocks of eight MiB or more; --block-size gives
        //      the fixture the several it is about.
        xz_block_size = 1048576;
        n = xz_deflate_mem(src, sizeof(src), packed, sizeof(packed), 0);
        xz_block_size = 0;
        check("block fixture encodes", n > 64);
        if (n <= 64)
                return;

        p8 check_type = packed[7] & 15;
        positive backward = ((positive)memory_load_unaligned(p32, packed + n - 8) + 1) * 4;
        positive at = (positive)n - 12 - backward;
        positive count;
        positive start = 12;
        positive plain = 0;

        check("block fixture index", packed[at] == 0);
        at++;
        count = xz_index_vli(packed, address_of at);
        check("block fixture has several blocks", count > 1 && count <= 64);
        if (count <= 1 || count > 64)
                return;
        for (positive i = 0; i < count; i++)
        {
                positive unpadded = xz_index_vli(packed, address_of at);

                sizes[i] = xz_index_vli(packed, address_of at);
                starts[i] = start;
                lengths[i] = (unpadded + 3) & ~(positive)3;
                offsets[i] = plain;
                start += lengths[i];
                plain += sizes[i];
        }
        check("block fixture index adds up", plain == sizeof(src));

        address_any d = xz_block_decoder();
        p8 all = d != null;

        for (positive k = count; all && k--;)
        {
                positive stored = 0;

                all = xz_block_decode(d, packed + starts[k], lengths[k], sizes[k],
                                      back + offsets[k], check_type, check_bytes, null);
                memory_copy_apart(address_of stored, packed + starts[k] + lengths[k] -
                                  (check_type == XZ_CHECK_CRC64 ? 8 : 4),
                                  check_type == XZ_CHECK_CRC64 ? 8 : 4);
                all = all && !memory_compare(address_of stored, check_bytes, 8);
        }
        check("every block decodes alone into its exact span",
              all && !memory_compare(back, src, sizeof(src)) && !xz_pull_error(d));
        if (d)
        {
                check("a block one byte longer than its data is refused by name",
                      !xz_block_decode(d, packed + starts[0], lengths[0], sizes[0] + 1,
                                       back, check_type, null, null) &&
                      xz_pull_error(d) != null);
                packed[starts[1] + lengths[1] - 1] ^= 1;
                check("a block whose check fails is refused by name",
                      !xz_block_decode(d, packed + starts[1], lengths[1], sizes[1],
                                       back + offsets[1], check_type, null, null) &&
                      xz_pull_error(d) != null);
                packed[starts[1] + lengths[1] - 1] ^= 1;
                check("the decoder recovers for the next block",
                      xz_block_decode(d, packed + starts[1], lengths[1], sizes[1],
                                      back + offsets[1], check_type, null, null) &&
                      !memory_compare(back + offsets[1], src + offsets[1], sizes[1]));

                /* Less room than the block's data, cut inside each of the
                   three kinds of 4096-byte run the fixture lays down so that
                   some cut falls inside a match: refused by name, and not a
                   byte lands past the room. */
                p8 contained = sizes[0] > 4 * 4096;

                for (positive run = 0; contained && run < 3; run++)
                        for (positive cut = 1; contained && cut < 40; cut += 3)
                        {
                                positive room = sizes[0] - run * 4096 - 2048 - cut;
                                p8 untouched = true;

                                memory_fill(back + room, 0xa5, 512);

                                p8 refused = !xz_block_decode(d, packed + starts[0], lengths[0],
                                                                room, back, check_type, null, null) &&
                                               xz_pull_error(d) != null;

                                for (positive i = 0; i < 512; i++)
                                        untouched = untouched && back[room + i] == 0xa5;
                                contained = refused && untouched;
                        }
                check("a block given less room than its data is refused and writes nothing past it",
                      contained);
        }
        /* A block shorter than its dictionary: the history ends at the
           block's first byte, not at the dictionary size. */
        n = xz_deflate_mem(src, 1048576 + 17, packed, sizeof(packed), 6);
        if (d && n > 64)
        {
                p8 type = packed[7] & 15;
                positive tail = ((positive)memory_load_unaligned(p32, packed + n - 8) + 1) * 4;
                positive index = (positive)n - 12 - tail + 1;
                positive records = xz_index_vli(packed, address_of index);
                positive unpadded = xz_index_vli(packed, address_of index);
                positive size = xz_index_vli(packed, address_of index);

                check("a block shorter than its dictionary decodes alone",
                      records == 1 && size == 1048576 + 17 &&
                      xz_block_decode(d, packed + 12, (unpadded + 3) & ~(positive)3,
                                      size, back, type, null, null) &&
                      !memory_compare(back, src, size));
        }
        check("block decoder closes", xz_pull_close(d));
}

/* SHA-256 checks: a stream decodes, and one flipped check byte fails it,
   as a whole stream and as a block on its own. */
/* The filters, whole against chopped: a chain encoded in place over a block
   decodes to the block whole, and through the stage-by-stage carry that a
   ring decode uses, in pieces of every awkward size with the final flush.
   The bytes are planted with what each converter converts. */
static fn filters_split(void)
{
        static p8 plain[300000];
        static p8 coded[300000];
        static p8 back[300000];
        static p8 window[300000 + 4 * XZ_FILTER_HEAD];
        static p8 rebuilt[300000 + 4 * XZ_FILTER_HEAD];
        static const p8 ids[] = {XZ_FILTER_X86, XZ_FILTER_PPC, XZ_FILTER_IA64, XZ_FILTER_ARM,
                                 XZ_FILTER_ARMT, XZ_FILTER_SPARC, XZ_FILTER_ARM64,
                                 XZ_FILTER_RISCV, XZ_FILTER_DELTA};
        p32 random = 0x9e3779b9u;

        for (positive i = 0; i < sizeof(plain); i++)
        {
                XORSHIFT32(random);
                plain[i] = (p8)random;
                if ((random >> 24) % 9 == 0 && i + 8 < sizeof(plain))
                {
                        static const p8 shapes[][8] = {
                            {0xe8, 1, 2, 3, 0, 0, 0, 0}, {1, 2, 3, 0xeb, 0, 0, 0, 0},
                            {0xef, 0, 1, 2, 0x17, 5, 0, 0}, {0x48, 1, 2, 1, 0, 0, 0, 0},
                            {0x40, 3, 4, 5, 0, 0, 0, 0}, {0, 0xf0, 4, 5, 0, 0xf8, 0, 0},
                            {0x25, 0, 0, 0, 0x90, 1, 2, 3}};

                        memory_copy_apart(plain + i, shapes[(random >> 8) % 7], 8);
                }
        }
        for (positive at = 0; at < 2 * array_count(ids) + 3; at++)
        {
                xz_filter list[XZ_FILTERS_MAX];
                positive count = at < array_count(ids) ? 1 : at < 2 * array_count(ids) ? 2 : 3;
                p8 all_ok = true;

                memory_fill(list, 0, sizeof(list));
                for (positive k = 0; k < count; k++)
                {
                        list[k].id = ids[(at + 4 * k) % array_count(ids)];
                        list[k].start = (at & 1) && list[k].id != XZ_FILTER_DELTA
                                            ? 16 * xz_filter_alignment(list[k].id) : 0;
                        list[k].dist = (p8)(at + k) % 8;
                }
                memory_copy_apart(coded, plain, sizeof(plain));
                xz_filter_block(list, count, true, coded, sizeof(coded));
                memory_copy_apart(back, coded, sizeof(coded));
                xz_filter_block(list, count, false, back, sizeof(back));
                all_ok = !memory_compare(back, plain, sizeof(plain));
                check("filter chain decodes whole", all_ok);

                for (positive k = 0; k < count; k++)
                        xz_filter_reset(list + k);

                positive from = 0;
                positive made = 0;
                positive step = 1 + (at * 977) % 61;

                while (from < sizeof(coded))
                {
                        positive n = min(step, sizeof(coded) - from);
                        p8 address_to head = window + XZ_FILTER_HEAD;
                        positive got;

                        memory_copy_apart(head, coded + from, n);
                        got = xz_filter_chain(list, count, false, address_of head, n, false);
                        memory_copy_apart(rebuilt + made, head, got);
                        made += got;
                        from += n;
                        XORSHIFT32(random);
                        step = 1 + random % (from & 1 ? 40 : 9000);
                }

                p8 address_to head = window + XZ_FILTER_HEAD;
                positive got = xz_filter_chain(list, count, false, address_of head, 0, true);

                memory_copy_apart(rebuilt + made, head, got);
                made += got;
                check("filter chain decodes in pieces", made == sizeof(plain) &&
                                                          !memory_compare(rebuilt, plain, sizeof(plain)));
        }
}

static fn sha256_checks(void)
{
        static p8 into[16384];
        bipolar n;

        check("python SHA-256 hello xz",
              xz_out_is(xz_sha_hello_py, sizeof(xz_sha_hello_py), "hello sha256\n"));
        n = xz_inflate_mem(xz_sha_text_py, sizeof(xz_sha_text_py), into, sizeof(into));
        check("python SHA-256 text xz decodes", n > 256);

        /* The index sits before the 12-byte footer; the check is the last
           32 bytes of the single block before the index. */
        positive backward = ((positive)memory_load_unaligned(p32, xz_sha_text_py + sizeof(xz_sha_text_py) - 8) + 1) * 4;
        positive index = sizeof(xz_sha_text_py) - 12 - backward;
        p8 address_to stored = xz_sha_text_py + index - 32;
        p8 flipped[sizeof(xz_sha_text_py)];

        memory_copy_apart(flipped, xz_sha_text_py, sizeof(flipped));
        flipped[index - 1] ^= 1;
        check("a SHA-256 mismatch fails the stream",
              xz_inflate_mem(flipped, sizeof(flipped), into, sizeof(into)) < 0);

        address_any d = xz_block_decoder();
        p8 got[32];
        static p8 out[16384];

        check("a SHA-256 block decodes alone",
              d && n > 0 &&
              xz_block_decode(d, xz_sha_text_py + 12, index - 12, (p64)n, out,
                              XZ_CHECK_SHA256, got, null) &&
              !memory_compare(out, into, (positive)n) &&
              !memory_compare(got, stored, 32));
        check("a SHA-256 block with a flipped check is refused by name",
              d && !xz_block_decode(d, flipped + 12, index - 12, (p64)n, out,
                                    XZ_CHECK_SHA256, null, null) &&
              xz_pull_error(d) != null);
        xz_pull_close(d);
}

/* Write n bytes to handle and pull them back through a reader until it
   stops: what it handed out lands in into, last is its final return. */
static positive xz_salvage_pull(bipolar handle, p8 address_to bytes, positive n,
                                p8 address_to into, positive room, bipolar address_to last)
{
        positive got = 0;
        address_any d;

        address_to last = -2;
        if (system_seek(handle, 0, FILE_SEEK_SET) != 0 ||
            system_truncate_handle(handle, 0) != 0 ||
            system_write_all(handle, bytes, n) != (bipolar)n ||
            system_seek(handle, 0, FILE_SEEK_SET) != 0)
                return 0;
        d = xz_pull_open(handle, null, 0);
        if (!d)
                return 0;
        for (;;)
        {
                positive want = room - got < 65536 ? room - got : 65536;

                address_to last = want ? xz_pull_read(d, into + got, want) : 0;
                if (address_to last <= 0)
                        break;
                got += (positive)address_to last;
        }
        xz_pull_close(d);
        return got;
}

/* What a damaged stream still yields, by liblzma's rules, through the pull
   reader and through a block decode alike: a block whose check fails is
   handed out whole, a cut input every packet its bytes complete, and a
   chunk that claims a byte less than its packets make exactly that many
   bytes, its last match cut at the new end. */
static fn salvaged(void)
{
        static p8 src[1048576 + 333];
        static p8 packed[2 * 1048576];
        static p8 damaged[2 * 1048576];
        static p8 back[1048576 + 333 + 65536];
        static p8 out[1048576 + 333];
        bipolar handle;
        bipolar last;
        positive written;

        xz_fixture(src, sizeof(src), 0x0badf00du);

        bipolar n = xz_deflate_mem(src, sizeof(src), packed, sizeof(packed), 6);

        handle = system_call_2(syscall(memfd_create), (positive)"xz-salvage", 0);
        check("salvage fixture encodes into one block", n > 64 && handle >= 0);
        if (n <= 64 || handle < 0)
                return;

        p8 type = packed[7] & 15;
        positive backward = ((positive)memory_load_unaligned(p32, packed + n - 8) + 1) * 4;
        positive index = (positive)n - 12 - backward;
        positive block_len = index - 12;
        positive header = ((positive)packed[12] + 1) * 4;
        address_any d = xz_block_decoder();

        check("salvage decoder", d != null);
        if (!d)
                return;

        memory_copy_apart(damaged, packed, (positive)n);
        damaged[index - 1] ^= 1;
        positive got = xz_salvage_pull(handle, damaged, (positive)n, back, sizeof(back), address_of last);
        p8 refused = !xz_block_decode(d, damaged + 12, block_len, sizeof(src), out, type,
                                        null, address_of written);

        check("a block whose check fails is handed out whole before the failure",
              type == XZ_CHECK_NONE ||
              (got == sizeof(src) && last < 0 && !memory_compare(back, src, sizeof(src)) &&
               refused && written == sizeof(src) && !memory_compare(out, src, sizeof(src))));

        positive before = 0;
        p8 cut_ok = true;

        for (positive part = 1; part <= 9; part += 2)
        {
                positive cut = block_len * part / 10;

                got = xz_salvage_pull(handle, packed, 12 + cut, back, sizeof(back), address_of last);
                refused = !xz_block_decode(d, packed + 12, cut, sizeof(src), out, type,
                                           null, address_of written);
                cut_ok = cut_ok && last < 0 && refused && got == written && got >= before &&
                         got < sizeof(src) && !memory_compare(back, src, got) &&
                         !memory_compare(out, src, written);
                before = got;
        }
        check("a cut input yields the same completed packets by pull and by block", cut_ok && before);

        positive at = 12 + header;
        p8 control = packed[at];

        if (control >= 0xe0)
        {
                positive claimed = (((positive)control & 0x1f) << 16 |
                                    (positive)packed[at + 1] << 8 | packed[at + 2]) + 1;
                positive less = claimed - 2;

                memory_copy_apart(damaged, packed, (positive)n);
                damaged[at] = (p8)((control & 0xe0) | (less >> 16));
                damaged[at + 1] = (p8)(less >> 8);
                damaged[at + 2] = (p8)less;
                got = xz_salvage_pull(handle, damaged, (positive)n, back, sizeof(back), address_of last);
                refused = !xz_block_decode(d, damaged + 12, block_len, sizeof(src), out, type,
                                           null, address_of written);
                check("a chunk one byte short yields exactly its claimed bytes",
                      claimed > 1 && got == claimed - 1 && last < 0 &&
                      !memory_compare(back, src, got) && refused && written == got &&
                      !memory_compare(out, src, written));
        }
        else
                check("salvage fixture starts with an LZMA chunk", false);
        xz_pull_close(d);
        system_call_1(syscall(close), handle);
}

b32 main(void)
{
        members();
        roundtrip();
        large_roundtrip();
        streamed();
        threaded();
        pulled();
        blocks();
        sha256_checks();
        filters_split();
        salvaged();
        return test_report(null);
}
#endif /* CHECK_xz */

