/*
        tar -- ustar, GNU and pax archives.

        The kernel does not extract archives. The header checksum is
        memory_sum_bytes; a seekable uncompressed regular member is
        copy_file_range then sendfile, the same floors cp uses. Create
        walks with openat and statx on the directory fd. Extract restores
        setuid only for -p or root, the same rule GNU uses. gzip, xz and
        zstd run in-process: -z, -J, --zstd, -a, and extract looks at the
        magic so a .tar.gz needs no extra flag. A packed stream is not
        seekable. bzip2 and compress are named-refused. GNU sparse type S
        is reconstructed from the old GNU map; holes become zeros.
*/

#define TAR_BLOCK 512
#define TAR_SPARSE_MAX ((positive)1 << 22)
#define TAR_PSP_MAX 16384
#define TAR_PATH 4096
#define TAR_NAME 100
#define TAR_PREFIX 155
#define TAR_CHKSUM 148
#define TAR_CHKSUM_WIDTH 8
#define TAR_SPACE_SUM ((p32)' ' * TAR_CHKSUM_WIDTH)

static p32 tar_header_sum(p8 address_to block)
{
        return memory_sum_bytes(block, TAR_BLOCK) -
               memory_sum_bytes(block + TAR_CHKSUM, TAR_CHKSUM_WIDTH) +
               TAR_SPACE_SUM;
}

static bool tar_header_zero(p8 address_to block)
{
        return memory_sum_bytes(block, TAR_BLOCK) == 0;
}

static bool tar_base256(p8 address_to field, positive width,
                        p64 address_to value)
{
        p64 held = 0;
        positive at;

        if (!width || (field[0] & 0x40))
                return false;

        held = (p64)(field[0] & 0x7f);
        for (at = 1; at < width; at++)
        {
                if (held > (positive_max >> 8))
                        return false;

                held = (held << 8) | field[at];
        }

        address_to value = held;
        return true;
}

static bool tar_field_value(p8 address_to field, positive width,
                            p64 address_to value);

/*      A time, which base-256 may make negative: the field is a two's
        complement number under its marker bit, as GNU reads it, so 0xff
        followed by 0xff... is -1 and a stamp before 1970 lists as the year
        it was rather than as 1970, which is what refusing it -- and so
        having no time at all -- printed. Octal is never negative. */
static bool tar_field_moment(p8 address_to field, positive width,
                             b64 address_to value)
{
        p64 plain;
        b64 held;

        if (!width || !(field[0] & 0x80))
        {
                if (!tar_field_value(field, width, address_of plain) ||
                    plain > (p64)bipolar_max)
                        return false;
                address_to value = (b64)plain;
                return true;
        }

        held = (b64)(field[0] & 0x3f) - (b64)(field[0] & 0x40);
        for (positive at = 1; at < width; at++)
        {
                if (held > (bipolar_max >> 8) || held < (bipolar_min >> 8))
                        return false;
                held = held * 256 + field[at];
        }

        address_to value = held;
        return true;
}

/* Eight octal digit characters, the first the most significant, read as a
   number in a handful of instructions: false unless every byte is 0 to 7.
   The bytes are three-bit groups spread out again by multiply and mask, the
   way tar_octal_word spreads them out. */
static inline bool tar_octal_eight(p64 word, p64 address_to value)
{
        if ((word ^ 0x3030303030303030ull) & 0xf8f8f8f8f8f8f8f8ull)
                return false;
        word &= 0x0707070707070707ull;
        word = (word * 8 + (word >> 8)) & 0x00ff00ff00ff00ffull;
        word = (word * 64 + (word >> 16)) & 0x0000ffff0000ffffull;
        word = (word * 4096 + (word >> 32)) & 0xffffffull;
        address_to value = word;
        return true;
}

static bool tar_field_value(p8 address_to field, positive width,
                            p64 address_to value)
{
        p8 digits[32];
        positive at = 0;
        positive keep;
        string_address cursor;

        if (width && (field[0] & 0x80))
                return tar_base256(field, width, value);

        //      The fields as archives write them, digits and a NUL or space,
        //      without the copy and the scan.
        if (width == 8)
        {
                p64 word;
                p64 number;

                memory_copy(address_of word, field, 8);
                if ((word >> 56) == 0 || (word >> 56) == ' ')
                {
                        //      The last byte, not a digit, reads as a zero
                        //      one and is shifted away.
                        if (tar_octal_eight((word & 0x00ffffffffffffffull) |
                                                0x3000000000000000ull,
                                            address_of number))
                        {
                                address_to value = number >> 3;
                                return true;
                        }
                }
        }
        else if (width == 12 && (field[11] == 0 || field[11] == ' ') &&
                 field[0] >= '0' && field[0] <= '7' && field[1] >= '0' &&
                 field[1] <= '7' && field[2] >= '0' && field[2] <= '7')
        {
                p64 word;
                p64 number;

                memory_copy(address_of word, field + 3, 8);
                if (tar_octal_eight(word, address_of number))
                {
                        address_to value =
                            ((p64)((field[0] - '0') * 64 + (field[1] - '0') * 8 +
                                   (field[2] - '0'))
                             << 24) | number;
                        return true;
                }
        }

        if (width >= sizeof(digits))
                return false;

        memory_copy(digits, field, width);
        digits[width] = end;

        at += memory_span_byte(digits + at, ' ', width - at);

        if (at >= width || !digits[at] || digits[at] == ' ')
        {
                address_to value = 0;
                return true;
        }

        cursor = digits + at;
        if (!string_digits_checked(address_of cursor, 8, address_of keep))
                return false;

        at = (positive)(cursor - digits);
        while (at < width && (digits[at] == ' ' || !digits[at]))
                at++;

        if (at < width && digits[at])
                return false;

        address_to value = keep;
        return true;
}

/* Eight octal digits of the low 24 bits, most significant first, as the
   bytes of one little-endian word: three-bit groups spread to bytes by
   shifts and masks, then the bytes turned round and made characters. */
static inline p64 tar_octal_word(p64 value)
{
        p64 word = value & 0xffffff;

        word = (word | (word << 20)) & 0x00000fff00000fffull;
        word = (word | (word << 10)) & 0x003f003f003f003full;
        word = (word | (word << 5)) & 0x0707070707070707ull;
#if defined(__riscv)
        //      No byte swap instruction without Zbb, and the compiler's call
        //      needs a library this build does not link.
        word = ((word & 0x00ff00ff00ff00ffull) << 8) | ((word >> 8) & 0x00ff00ff00ff00ffull);
        word = ((word & 0x0000ffff0000ffffull) << 16) | ((word >> 16) & 0x0000ffff0000ffffull);
        word = (word << 32) | (word >> 32);
        return word | 0x3030303030303030ull;
#else
        return __builtin_bswap64(word) | 0x3030303030303030ull;
#endif
}

static fn tar_field_put_octal(p8 address_to field, positive width, p64 value)
{
        p8 digits[32];
        positive length;

        //      The widths a header has, whole and in a few instructions.
        if (width == 8 && value < ((p64)1 << 21))
        {
                p64 word = tar_octal_word(value) >> 8;

                memory_copy(field, address_of word, 8);
                return;
        }
        if (width == 7 && value < ((p64)1 << 18))
        {
                p64 word = tar_octal_word(value) >> 16;

                memory_copy(field, address_of word, 7);
                return;
        }
        if (width == 12 && value < ((p64)1 << 33))
        {
                p64 word = tar_octal_word(value);
                positive high = (positive)(value >> 24);

                field[0] = (p8)('0' + ((high >> 6) & 7));
                field[1] = (p8)('0' + ((high >> 3) & 7));
                field[2] = (p8)('0' + (high & 7));
                memory_copy(field + 3, address_of word, 8);
                field[11] = end;
                return;
        }

        memory_fill(field, '0', width);
        if (!width)
                return;

        field[width - 1] = end;
        length = positive_into_base(digits, (positive)value, 8, false);
        if (length >= width)
                length = width - 1;

        memory_copy(field + (width - 1 - length), digits, length);
}

static fn tar_header_put_checksum(p8 address_to block)
{
        tar_field_put_octal(block + TAR_CHKSUM, 7, tar_header_sum(block));
        block[TAR_CHKSUM + 6] = end;
        block[TAR_CHKSUM + 7] = ' ';
}

static bool tar_header_ok(p8 address_to block)
{
        p64 stored;

        if (!tar_field_value(block + TAR_CHKSUM, TAR_CHKSUM_WIDTH,
                             address_of stored))
                return false;

        return stored == tar_header_sum(block);
}

/* The same test, given the sum of the whole block a caller already has. */
static bool tar_header_ok_from(p8 address_to block, p32 total)
{
        p64 stored;
        p64 word;

        //      "006123" NUL space, as GNU writes the field.
        memory_copy(address_of word, block + TAR_CHKSUM, 8);
        if ((word >> 48) == 0x2000 &&
            tar_octal_eight((word & 0x0000ffffffffffffull) | 0x3030000000000000ull,
                            address_of stored))
                stored >>= 6;
        else if (!tar_field_value(block + TAR_CHKSUM, TAR_CHKSUM_WIDTH,
                                  address_of stored))
                return false;

        return stored == total - memory_sum_bytes(block + TAR_CHKSUM, TAR_CHKSUM_WIDTH) +
                             TAR_SPACE_SUM;
}

static positive tar_padded(p64 size)
{
        return (positive)((size + (TAR_BLOCK - 1)) & ~(p64)(TAR_BLOCK - 1));
}

static bool tar_size_fits(p64 size)
{
        return size <= (p64)bipolar_max - (TAR_BLOCK - 1);
}

static fn tar_field_text(p8 address_to field, positive width,
                         p8 address_to into, positive room)
{
        positive keep = string_length_max(field, width);

        if (keep >= room)
                keep = room ? room - 1 : 0;

        if (keep)
                memory_copy(into, field, keep);

        if (room)
                into[keep] = end;
}

static bool tar_join_name(p8 address_to prefix, p8 address_to name,
                          p8 address_to into, positive room)
{
        p8 head[TAR_PREFIX + 1];
        p8 leaf[TAR_NAME + 1];

        if (!room)
                return false;

        tar_field_text(name, TAR_NAME, leaf, sizeof(leaf));
        tar_field_text(prefix, TAR_PREFIX, head, sizeof(head));
        if (!head[0])
        {
                string_copy_max_end(into, leaf, room ? room - 1 : 0);
                return into[0] != end;
        }

        return path_join(into, room, head, leaf) && into[0];
}

/*
        Drop leading slashes unless the caller asked to keep them, drop a
        leading run of components, and refuse a path that would walk out of
        the destination with `..`.
*/
static bool tar_safe_path(string_address path, positive strip, bool absolute,
                          p8 address_to into, positive room,
                          bool address_to escaped)
{
        p8 work[TAR_PATH];
        positive used = 0;
        string_address at = path;

        if (!path || !room)
                return false;

        if (room > sizeof(work))
                room = sizeof(work);

        into[0] = end;
        if (escaped)
                address_to escaped = false;

        if (!absolute)
                at += string_span_of_set(at, "/");
        else if (string_is(path, '/'))
        {
                work[0] = '/';
                used = 1;
        }

        while (*at)
        {
                string_address slash = string_first_of(at, '/');
                positive piece = slash ? (positive)(slash - at)
                                       : string_length(at);

                if (!piece)
                {
                        at++;
                        continue;
                }

                if (piece == 1 && string_is(at, '.'))
                {
                        at += slash ? piece + 1 : piece;
                        continue;
                }

                if (piece == 2 && string_is(at, '.') && string_is(at + 1, '.'))
                {
                        if (escaped)
                                address_to escaped = true;

                        return false;
                }

                if (strip)
                {
                        strip--;
                        at += slash ? piece + 1 : piece;
                        continue;
                }

                if (used && used + 1 < room && !(used == 1 && work[0] == '/'))
                        work[used++] = '/';

                if (used + piece >= room)
                        return false;

                memory_copy(work + used, at, piece);
                used += piece;
                at += slash ? piece + 1 : piece;
        }

        if (!used || strip)
                return false;

        work[used] = end;
        string_copy_max_end(into, work, room - 1);
        return true;
}

#define TAR_PACK_NONE 0
#define TAR_PACK_GZIP 1
#define TAR_PACK_XZ 2
#define TAR_PACK_ZSTD 3
#define TAR_PACK_AUTO 4
#define TAR_PACK_BZIP2 5
#define TAR_PACK_COMPRESS 6

typedef struct
{
        p8 path[TAR_PATH];
        p8 link[TAR_PATH];
        p64 size;
        p32 user;
        p32 group;
        b64 seconds;
        p32 nanoseconds;
        bool has_path;
        bool has_link;
        bool has_size;
        bool has_user;
        bool has_group;
        bool has_time;
} tar_pax_state;

static tar_pax_state tar_pax_global;
static tar_pax_state tar_pax_local;

/* A plain decimal that fits a signed 64 bit size. */
static bool tar_pax_wide(p8 address_to text, positive length, p64 address_to into)
{
        p64 number = 0;

        if (!length)
                return false;
        for (positive at = 0; at < length; at++)
        {
                if (text[at] < '0' || text[at] > '9' ||
                    number > ((p64)bipolar_max - (p64)(text[at] - '0')) / 10)
                        return false;
                number = number * 10 + (p64)(text[at] - '0');
        }
        address_to into = number;
        return true;
}

/* What a pax header said of a sparse member: 1 the map is in its records
   (versions 0.0 and 0.1), 2 the map is text at the front of the data (1.0). */
typedef struct
{
        p64 offset;
        p64 bytes;
} tar_psp_span;

static tar_psp_span tar_psp[TAR_PSP_MAX];
static positive tar_psp_used;
static p64 tar_psp_real;
static p64 tar_psp_offset;
static p8 tar_psp_kind;
static bool tar_psp_bad;

/* Extended attributes and ACLs a pax header carried, kept as records of
   kind, name length, value length, name and value until the member they
   belong to is made: 1 an attribute, 2 the access ACL, 3 the default one. */
static p8 address_to tar_pax_attr;
static positive tar_pax_attr_room;
static positive tar_pax_attr_used;

#ifndef TAR_PARSE_ONLY
static bool tar_pax_attr_add(p8 kind, string_address name, positive name_length,
                             string_address value, positive value_length)
{
        positive need = 9 + name_length + value_length;
        p32 name_size = (p32)name_length;
        p32 value_size = (p32)value_length;

        if (tar_pax_attr_used + need > (positive)1 << 22)
                return false;
        if (!shell_array_room(tar_pax_attr, tar_pax_attr_room,
                              tar_pax_attr_used + need))
                return false;
        p8 address_to at = tar_pax_attr + tar_pax_attr_used;

        at[0] = kind;
        memory_copy(at + 1, address_of name_size, 4);
        memory_copy(at + 5, address_of value_size, 4);
        memory_copy(at + 9, name, name_length);
        memory_copy(at + 9 + name_length, value, value_length);
        tar_pax_attr_used += need;
        return true;
}
#else
static bool tar_pax_attr_add(p8 kind, string_address name, positive name_length,
                             string_address value, positive value_length)
{
        (void)kind; (void)name; (void)name_length; (void)value; (void)value_length;
        return true;
}
#endif

static fn tar_psp_add(p64 offset, p64 bytes)
{
        if (tar_psp_used >= TAR_PSP_MAX)
        {
                tar_psp_bad = true;
                return;
        }
        tar_psp[tar_psp_used].offset = offset;
        tar_psp[tar_psp_used++].bytes = bytes;
        if (!tar_psp_kind)
                tar_psp_kind = 1;
}

static fn tar_pax_clear(tar_pax_state address_to state)
{
        if (state == address_of tar_pax_local)
        {
                tar_psp_used = 0;
                tar_psp_real = 0;
                tar_psp_kind = 0;
                tar_psp_bad = false;
                tar_pax_attr_used = 0;
        }
        state->has_path = false;
        state->has_link = false;
        state->has_size = false;
        state->has_user = false;
        state->has_group = false;
        state->has_time = false;
        state->path[0] = end;
        state->link[0] = end;
}

/* A pax id is plain decimal.  One that is not, or that does not fit, is
   ignored rather than refusing the archive. */
static bool tar_pax_number(p8 address_to text, positive length,
                           p64 address_to into)
{
        p64 number = 0;

        if (!length)
                return false;
        for (positive at = 0; at < length; at++)
        {
                if (text[at] < '0' || text[at] > '9' ||
                    number > 0xffffffffull / 10)
                        return false;
                number = number * 10 + (p64)(text[at] - '0');
        }
        if (number > 0xffffffffull)
                return false;
        address_to into = number;
        return true;
}

/* Seconds since the epoch, perhaps negative, with a fraction whose first
   nine digits become nanoseconds: -1.25 is two seconds before the epoch
   plus 750000000 nanoseconds.  A malformed time restores nothing. */
static bool tar_pax_time(p8 address_to text, positive length,
                         b64 address_to seconds, p32 address_to nanoseconds)
{
        bool negative = length && text[0] == '-';
        positive at = negative;
        p64 whole = 0;
        p32 fraction = 0;
        positive digits = 0;

        if (at >= length || text[at] < '0' || text[at] > '9')
                return false;
        while (at < length && text[at] >= '0' && text[at] <= '9')
        {
                if (whole >= 922337203685477580ull)
                        return false;
                whole = whole * 10 + (p64)(text[at++] - '0');
        }
        if (at < length && text[at] == '.')
                for (at++; at < length && text[at] >= '0' && text[at] <= '9';
                     at++)
                        if (digits < 9)
                        {
                                fraction = fraction * 10 +
                                           (p32)(text[at] - '0');
                                digits++;
                        }
        if (at != length)
                return false;
        for (; digits < 9; digits++)
                fraction *= 10;
        if (negative && fraction)
        {
                whole++;
                fraction = 1000000000u - fraction;
        }
        address_to seconds = negative ? -(b64)whole : (b64)whole;
        address_to nanoseconds = fraction;
        return true;
}

static bool tar_pax_apply(tar_pax_state address_to state,
                          p8 address_to body, positive length)
{
        positive at = 0;

        while (at < length)
        {
                p8 digits[32];
                positive record;
                string_address cursor = digits;
                p8 address_to space = memory_first_of(body + at, ' ',
                                                       length - at);
                positive used = space ? (positive)(space - (body + at)) : 0;
                string_address mark;
                string_address equal;
                positive key;
                positive value;
                positive rest;

                if (!used || used >= sizeof(digits))
                        return false;

                memory_copy_end(digits, body + at, used);
                if (!string_digits_checked(address_of cursor, 10,
                                            address_of record) || *cursor ||
                    record > length - at || record < used + 4)
                        return false;

                mark = body + at + used;
                if (body[at + record - 1] != '\n')
                        return false;

                equal = memory_first_of(mark + 1, '=', record - used - 2);
                if (!equal || equal == mark + 1)
                        return false;

                key = (positive)(equal - (mark + 1));
                value = (positive)((body + at + record - 1) - (equal + 1));
                rest = record;

                if (body[at + rest - 1] != '\n')
                        return false;

                if (key == 4 && !memory_compare(mark + 1, "path", 4))
                {
                        if (value >= TAR_PATH)
                                return false;

                        memory_copy(state->path, equal + 1, value);
                        state->path[value] = end;
                        state->has_path = true;
                }
                else if (key == 8 && !memory_compare(mark + 1, "linkpath", 8))
                {
                        if (value >= TAR_PATH)
                                return false;

                        memory_copy(state->link, equal + 1, value);
                        state->link[value] = end;
                        state->has_link = true;
                }
                else if (key == 4 && !memory_compare(mark + 1, "size", 4))
                {
                        p8 keep[32];
                        positive parsed;
                        string_address cursor = keep;

                        if (value >= sizeof(keep))
                                return false;

                        memory_copy(keep, equal + 1, value);
                        keep[value] = end;
                        if (!string_digits_checked(address_of cursor, 10,
                                                    address_of parsed) ||
                            *cursor || !tar_size_fits(parsed))
                                return false;
                        state->size = parsed;
                        state->has_size = true;
                }
                else if (key == 5 && !memory_compare(mark + 1, "mtime", 5))
                        state->has_time = tar_pax_time(
                            equal + 1, value, address_of state->seconds,
                            address_of state->nanoseconds);
                else if (key == 3 && (!memory_compare(mark + 1, "uid", 3) ||
                                      !memory_compare(mark + 1, "gid", 3)))
                {
                        p64 number = 0;
                        bool good = tar_pax_number(equal + 1, value,
                                                   address_of number);

                        if (mark[1] == 'u')
                        {
                                state->user = (p32)number;
                                state->has_user = good;
                        }
                        else
                        {
                                state->group = (p32)number;
                                state->has_group = good;
                        }
                }

                else if (key > 13 && !memory_compare(mark + 1, "SCHILY.xattr.", 13))
                {
                        if (!tar_pax_attr_add(1, mark + 14, key - 13, equal + 1, value))
                                return false;
                }
                else if (key == 17 && !memory_compare(mark + 1, "SCHILY.acl.access", 17))
                {
                        if (!tar_pax_attr_add(2, "", 0, equal + 1, value))
                                return false;
                }
                else if (key == 18 && !memory_compare(mark + 1, "SCHILY.acl.default", 18))
                {
                        if (!tar_pax_attr_add(3, "", 0, equal + 1, value))
                                return false;
                }
                else if (key > 11 && !memory_compare(mark + 1, "GNU.sparse.", 11))
                {
                        p64 number = 0;
                        string_address name = mark + 12;
                        positive name_length = key - 11;
                        bool numeric = tar_pax_wide(equal + 1, value,
                                                      address_of number);

                        if (name_length == 4 && !memory_compare(name, "name", 4))
                        {
                                if (value >= TAR_PATH)
                                        return false;
                                memory_copy(state->path, equal + 1, value);
                                state->path[value] = end;
                                state->has_path = true;
                        }
                        else if ((name_length == 8 && !memory_compare(name, "realsize", 8)) ||
                                 (name_length == 4 && !memory_compare(name, "size", 4)))
                        {
                                if (numeric)
                                        tar_psp_real = number;
                                else
                                        tar_psp_bad = true;
                        }
                        else if (name_length == 5 && !memory_compare(name, "major", 5))
                                tar_psp_kind = numeric && number == 1 ? 2 : tar_psp_kind;
                        else if (name_length == 6 && !memory_compare(name, "offset", 6))
                        {
                                tar_psp_offset = number;
                                tar_psp_bad |= !numeric;
                        }
                        else if (name_length == 8 && !memory_compare(name, "numbytes", 8))
                        {
                                if (numeric)
                                        tar_psp_add(tar_psp_offset, number);
                                else
                                        tar_psp_bad = true;
                        }
                        else if (name_length == 3 && !memory_compare(name, "map", 3))
                        {
                                positive walk = 0;
                                p64 first = 0;
                                bool have = false;

                                while (walk <= value)
                                {
                                        positive stop = walk;
                                        p64 got = 0;

                                        while (stop < value && equal[1 + stop] != ',')
                                                stop++;
                                        if (!tar_pax_wide(equal + 1 + walk, stop - walk,
                                                            address_of got))
                                        {
                                                tar_psp_bad = true;
                                                break;
                                        }
                                        if (have)
                                                tar_psp_add(first, got);
                                        else
                                                first = got;
                                        have = !have;
                                        walk = stop + 1;
                                }
                                if (have)
                                        tar_psp_bad = true;
                        }
                }

                at += rest;
        }

        return true;
}

static p8 tar_pack_from_name(string_address name)
{
        positive n;

        if (!name || string_equals(name, "-"))
                return TAR_PACK_NONE;
        n = string_length(name);
        if (n >= 3 && !memory_compare(name + n - 3, ".gz", 3))
                return TAR_PACK_GZIP;
        if (n >= 4 && !memory_compare(name + n - 4, ".tgz", 4))
                return TAR_PACK_GZIP;
        if (n >= 3 && !memory_compare(name + n - 3, ".xz", 3))
                return TAR_PACK_XZ;
        if (n >= 4 && !memory_compare(name + n - 4, ".txz", 4))
                return TAR_PACK_XZ;
        if (n >= 4 && !memory_compare(name + n - 4, ".zst", 4))
                return TAR_PACK_ZSTD;
        if (n >= 5 && !memory_compare(name + n - 5, ".tzst", 5))
                return TAR_PACK_ZSTD;
        if ((n >= 4 && !memory_compare(name + n - 4, ".bz2", 4)) ||
            (n >= 5 && !memory_compare(name + n - 5, ".tbz2", 5)) ||
            (n >= 4 && !memory_compare(name + n - 4, ".tbz", 4)))
                return TAR_PACK_BZIP2;
        if (n >= 2 && !memory_compare(name + n - 2, ".Z", 2))
                return TAR_PACK_COMPRESS;
        return TAR_PACK_NONE;
}

static p8 tar_pack_from_magic(p8 address_to magic, positive n)
{
        if (n >= 2 && magic[0] == 0x1f && magic[1] == 0x8b)
                return TAR_PACK_GZIP;
        if (n >= 6 && magic[0] == 0xfd && magic[1] == 0x37 && magic[2] == 0x7a &&
            magic[3] == 0x58 && magic[4] == 0x5a && magic[5] == 0)
                return TAR_PACK_XZ;
        if (n >= 4 && magic[0] == 0x28 && magic[1] == 0xb5 && magic[2] == 0x2f &&
            magic[3] == 0xfd)
                return TAR_PACK_ZSTD;
        if (n >= 3 && magic[0] == 'B' && magic[1] == 'Z' && magic[2] == 'h')
                return TAR_PACK_BZIP2;
        if (n >= 2 && magic[0] == 0x1f && magic[1] == 0x9d)
                return TAR_PACK_COMPRESS;
        return TAR_PACK_NONE;
}

static bool tar_header_gnu_old(p8 address_to block)
{
        return !memory_compare(block + 257, "ustar ", 6) &&
               block[263] == ' ' && !block[264];
}

/*
        A directory member is spelled with a trailing slash: ustar says a
        directory's name ends in one and the reference writes it that way.
        False when the name has no room for it, and the caller keeps the
        plain spelling, which the type flag still marks as a directory.
*/
static bool tar_spell_directory(string_address name, p8 address_to into,
                                positive room)
{
        positive length = string_length(name);

        if (!length || name[length - 1] == '/' || length + 2 > room)
                return false;
        memory_copy(into, name, length);
        into[length] = '/';
        into[length + 1] = end;
        return true;
}

/*
        A member can carry its name three ways at once, and they rank: a pax
        path record, then the GNU long-name member before it, then the
        header's own fields.  The reference reads a pax record over a long
        name whichever of the two the archive wrote first, and over a global
        pax record as well, so an archive built to be read both ways gives up
        the same name here as there.  long_name is the long name this member
        was given, or null when it had none.
*/
static bool tar_member_name(p8 address_to block, p8 address_to into,
                            string_address long_name)
{
        tar_pax_state address_to state =
            tar_pax_local.has_path ? address_of tar_pax_local
                                   : address_of tar_pax_global;
        if (state->has_path)
        {
                string_copy_max_end(into, state->path, TAR_PATH - 1);
                return into[0] != end;
        }

        if (long_name)
        {
                string_copy_max_end(into, long_name, TAR_PATH - 1);
                return true;
        }

        /* GNU old format stores atime/sparse maps where ustar keeps prefix. */
        if (tar_header_gnu_old(block))
        {
                tar_field_text(block, TAR_NAME, into, TAR_PATH);
                return into[0] != end;
        }

        return tar_join_name(block + 345, block, into, TAR_PATH);
}

/* A link target ranks the same three ways, for the same reason. */
static bool tar_member_link(p8 address_to block, p8 address_to into,
                            string_address long_link)
{
        tar_pax_state address_to state =
            tar_pax_local.has_link ? address_of tar_pax_local
                                   : address_of tar_pax_global;
        if (state->has_link)
        {
                string_copy_max_end(into, state->link, TAR_PATH - 1);
                return true;
        }

        if (long_link)
        {
                string_copy_max_end(into, long_link, TAR_PATH - 1);
                return true;
        }

        tar_field_text(block + 157, TAR_NAME, into, TAR_PATH);
        return true;
}

#ifndef TAR_PARSE_ONLY

#define TAR_CREATE 'c'
#define TAR_LIST 't'
#define TAR_EXTRACT 'x'

struct tar_options
{
        p8 mode;
        p8 pack;
        string_address archive;
        string_address directory;
        positive strip;
        positive verbose;
        bool absolute;
        bipolar permissions;
        bipolar owner;
        bool touch;
        bool numeric;
        bool short_o;
        bool sparse;
        bool xattrs;
        bool acls;
        bool selinux;
        p8 sparse_version;
        string_address pax_option;
        p8 format;
        p8 format_set;
        positive blocking;
        positive first;
};

/* The selected lifecycle is independent of the archive framing/parser. */
typedef struct
{
        p8 level;
        bool (*read_begin)(bipolar, p8 address_to, positive);
        bipolar (*read)(p8 address_to, positive);
        bool (*read_end)(void);
        bool (*write_begin)(bipolar, p8);
        bool (*write)(p8 address_to, positive);
        bool (*write_end)(void);
        string_address address_to why;
} tar_codec;

static const tar_codec tar_codecs[] = {
    [TAR_PACK_GZIP] = {6, gzip_decode_begin_prefix, gzip_decode_read,
        gzip_decode_end, gzip_encode_begin, gzip_encode_write,
        gzip_encode_end, address_of gzip_why},
    [TAR_PACK_XZ] = {6, xz_decode_begin_prefix, xz_decode_read,
        xz_decode_end, xz_encode_begin, xz_encode_write,
        xz_encode_end, address_of xz_why},
    [TAR_PACK_ZSTD] = {3, zstd_decode_begin_prefix, zstd_decode_read,
        zstd_decode_end, zstd_encode_begin, zstd_encode_write,
        zstd_encode_end, address_of zstd_why},
};

static p8 tar_block[TAR_BLOCK];
static p8 tar_name[TAR_PATH];
static p8 tar_link[TAR_PATH];
static b32 tar_status;
static bool tar_preserve;
/* General-purpose tar preserves special files, but callers installing an
   untrusted root filesystem can disable them before extraction. */
static bool tar_extract_special = true;

static bool tar_extract_type_allowed(p8 type)
{
        return tar_extract_special ||
               (type != '3' && type != '4' && type != '6');
}

/* Directory permissions are an end-of-extraction property.  Applying an
   archived mode such as 0000 while later members still need to traverse the
   directory makes a valid archive extract differently according to member
   order.  Keep the inode identity and final mode, then revisit deepest first
   after the last member. */
/* What a member restores besides its bytes and mode. */
typedef struct
{
        p32 user;
        p32 group;
        b64 seconds;
        p32 nanoseconds;
        bool timed;
        positive attr_at;
        positive attr_length;
} tar_member_meta;

/* Records found by path: fixed-width records that each begin with a
   path_table_key, their paths in one arena, and an index of record numbers
   plus one, open addressed and kept under half full, so remembering or
   finding a path is not a walk over every one remembered before (an
   archive of a million directories was a trillion comparisons).  Indexes
   survive growth of the array and the arena, and the spelling comparison
   remains the proof after the hash rejects unlike paths. */
typedef struct
{
        positive path_at;
        positive path_hash;
} path_table_key;

typedef struct
{
        p8 address_to records;
        positive stride;
        positive count;
        positive room;
        p8 address_to paths;
        positive paths_used;
        positive paths_room;
        positive address_to index;
        positive slots;
        positive index_room;
} path_table;

typedef struct
{
        path_table_key key;
        positive depth;
        positive mode;
        file_facts facts;
        tar_member_meta meta;
} tar_directory_mode;

static path_table tar_directories = {.stride = sizeof(tar_directory_mode)};
static positive address_to tar_directory_order;
static positive tar_directory_order_room;
static positive address_to tar_directory_spare;
static positive tar_directory_spare_room;

/* A hard-link header names another archive member, not an arbitrary object
   that happened to exist below the extraction root.  Retain the identity of
   each non-directory member this run successfully materialized, replacing a
   path's record when a later member overwrites it. */
/* A regular member made in a directory this run made is remembered as
   pending: that directory is private (0700, ours) until the archive ends,
   so only this run can change what its names hold, and the member's own
   identity is read only if a hard link ever asks for it.  The parent's
   identity is kept instead, to prove at link time that the name still
   resolves inside that same private directory. */
typedef struct
{
        p64 inode;
        p32 device_major;
        p32 device_minor;
} tar_identity_key;

typedef struct
{
        path_table_key key;
        file_facts facts;
        tar_identity_key parent;
        bool pending;
} tar_materialized_file;

static path_table tar_materialized = {.stride = sizeof(tar_materialized_file)};

/*
        GNU default blocking is twenty 512-byte blocks (10 KiB). One 64 KiB
        record is fewer writes on a file of many small members, and copy_file
        range still takes a member that fills a record on its own. The array
        is BSS: it is not resident until the first archive byte moves.
*/
#define TAR_RECORD (TAR_BLOCK * 128)
#define TAR_TRAILING_LIMIT (TAR_RECORD * 16)
#define TAR_ADVISE_SEQUENTIAL 2

/* The size from which a member's bytes go from file to archive in the kernel
   (copy_file_range, then sendfile) rather than through the record. */
#ifndef TAR_DIRECT_MIN
#define TAR_DIRECT_MIN TAR_RECORD
#endif

static p8 tar_record[TAR_RECORD];
static positive tar_have;
static positive tar_at;
static p8 tar_pack;
static const tar_codec address_to tar_decoder;
static const tar_codec address_to tar_encoder;
static file_facts tar_output_facts;
static bool tar_output_known;
static file_facts tar_output_target_facts;
static bool tar_output_target_known;
static file_facts tar_output_stage_facts;
static bool tar_output_stage_known;
static p64 tar_archive_size;
static bool tar_archive_sized;

/* Extended headers carry ACLs and xattrs as well as names, so a body is
   sized apart from TAR_PATH, up to a bound a hostile archive cannot turn
   into unbounded memory. */
#define TAR_PAX_LIMIT (1024 * 1024)
static p8 address_to tar_pax_body;
static positive tar_pax_body_room;

/*
        Only files with nlink > 1 enter the table. Sixty-four names at
        ustar's 100-byte link limit is enough for a tree of duplicated
        inodes without a megabyte of BSS, and a miss still writes a
        second copy rather than refusing the archive.
*/
/*
        The first name each multiply linked file was archived under, found by
        device and inode in a table of the entries and an arena of the names,
        both grown as they fill and both capped so a tree of millions of links
        cannot take the machine: past the cap a further link is written as a
        copy of the file rather than as a link.
*/
#define TAR_SEEN_LIMIT ((positive)1 << 22)
#define TAR_SEEN_NAMES_LIMIT ((positive)1 << 30)

typedef struct
{
        p64 inode;
        p64 device;
        positive name_at;
        bool used;
} tar_seen_file;

static tar_seen_file address_to tar_seen;
static positive tar_seen_slots;
static positive tar_seen_room;
static p8 address_to tar_seen_names;
static positive tar_seen_names_room;
static positive tar_seen_used;
static positive tar_seen_fill;

/* A member's name as GNU tar quotes it: C's escapes for the controls it
   has letters for, three octal digits for any other control or byte past
   ASCII, and the backslash doubled, so no name can drive the terminal. */
static fn tar_quoted(writer output, string_address name)
{
        writer_spelled(output, (address_any)name, string_length(name),
                       spelling_c(0));
}

static fn tar_name_line(writer output, string_address name)
{
        tar_quoted(output, name);
        output("\n", 1);
}

/*
        tar -tv's line, which GNU tar writes as the mode string, owner/group by
        the names the archive carries or else by number, the size -- or
        major,minor for a device -- right-aligned in a width that only grows
        through a listing, the local minute, and the name, with where a link
        goes. -v was accepted and printed the bare name.
*/
static positive tar_listing_width = 19;
static positive tar_listing_date_width = 16;

/* Where member names and long lines go: standard output, as GNU's stdlis,
   except while the archive itself is being written there. */
static writer tar_listing;

/*      The owner and group are names the archive chose, so a control byte in
        one is spelled \xNN rather than written to the terminal, where
        g\nh forged a line of the listing; the column is as wide as what is
        shown. GNU writes them as they are, and so does a build for
        diffing against it. */
static positive tar_header_word(p8 address_to into, p8 address_to field,
                                positive width, p64 number, bool numeric)
{
        positive length = 0;

        if (!numeric)
                length = string_length_max(field, width);

        if (!length)
                length = positive_into(into, (positive)number);
#if MOONWATER_STRICT >= STRICT_SAFE
        else
                length = memory_into_escaped(into, field, length, 4 * width,
                                             HEX_CONTROL | HEX_TAB).y;
#else
        else
                memory_copy(into, field, length);
#endif

        into[length] = end;
        return length;
}

static fn tar_long_line(p8 address_to block, p8 type, p64 mode, p64 size,
                        p64 user, p64 group, p64 major, p64 minor, b64 stamp,
                        string_address name, string_address link, bool numeric)
{
        static const struct
        {
                p8 type;
                positive format;
        } kinds[] = {{'2', 0120000}, {'3', 0020000}, {'4', 0060000},
                     {'5', 0040000}, {'6', 0010000}, {'D', 0040000}};
        positive format = 0100000;
        p8 letters[12];
        p8 owner[4 * 32 + 1];
        p8 grouped[4 * 32 + 1];
        p8 amount[48];
        positive widths;
        positive length;
        b64 year;
        positive month, day, hour, minute, second;

        for (positive at = 0; at < array_count(kinds); at++)
                if (kinds[at].type == type)
                        format = kinds[at].format;

        file_mode_letters(letters, format | (mode & 07777));
        if (type == '1')
                letters[0] = 'h';
        else if (type == '7')
                letters[0] = 'C';

        widths = tar_header_word(owner, block + 265, 32, user, numeric) +
                 tar_header_word(grouped, block + 297, 32, group, numeric);

        if (type == '3' || type == '4')
        {
                length = positive_into(amount, (positive)major);
                amount[length++] = ',';
                length += positive_into(amount + length, (positive)minor);
        }
        else
                length = positive_into(amount, (positive)size);
        amount[length] = end;

        widths += length + 2;
        if (widths > tar_listing_width)
                tar_listing_width = widths;

        file_split_moment(stamp + clock_local_east(stamp), address_of year,
                          address_of month, address_of day, address_of hour,
                          address_of minute, address_of second);

        string_format(tar_listing, "%s %s/%s ", (string_address)letters,
                      (string_address)owner, (string_address)grouped);
        for (positive pad = widths; pad < tar_listing_width; pad++)
                tar_listing(" ", 1);
        {
                /*      The year with its sign, and room for every digit a
                        stamp can make it. A pax mtime may be negative and
                        the cast alone turned -70000000000000 into the
                        twenty digit year 18446744073707335374, which wrote
                        nine bytes past a buffer sized for the eleven an
                        unsigned one needs. GNU tar prints the minus, so
                        this prints it too rather than the wrap.
                */
                p8 when[40];
                positive at = 0;
                positive parts[4] = {month, day, hour, minute};

                if (year < 0)
                {
                        when[at++] = '-';
                        at += positive_into(when + at,
                                            (positive)0 - (positive)year);
                }
                else
                        at += positive_into(when + at, (positive)year);

                for (positive part = 0; part < 4; part++)
                {
                        when[at++] = part == 0 || part == 1 ? '-' : part == 2 ? ' ' : ':';
                        when[at++] = (p8)('0' + parts[part] / 10);
                        when[at++] = (p8)('0' + parts[part] % 10);
                }
                /*      Left in a column as wide as the widest date so far,
                        the way the size is right in one: GNU's datewidth. */
                if (at > tar_listing_date_width)
                        tar_listing_date_width = at;
                memory_fill(when + at, ' ', tar_listing_date_width - at);
                at = tar_listing_date_width;
                when[at] = end;
                string_format(tar_listing, "%s %s ", (string_address)amount, (string_address)when);
        }
        tar_quoted(tar_listing, name);
        if (type == '1' || type == '2')
        {
                tar_listing(type == '1' ? " link to " : " -> ", type == '1' ? 9 : 4);
                tar_quoted(tar_listing, link);
        }
        tar_listing("\n", 1);
}

static fn tar_fail(string_address what, bipolar failed)
{
        string_format(log_error, "tar: %w: %s\n", writer_terminal_name, what, file_reason(failed));
        tar_status = 2;
}

/* State of an archive being written. */
static p64 tar_out_bytes;
static bool tar_numeric_owner;
static bool tar_create_fatal;
static positive tar_create_verbose;
static positive tar_dumped;

/* "tar: NAME: message", the name quoted as GNU's escape style does: C's
   letters, and three octal digits for a control or a byte past ASCII. */
static fn tar_quoted(writer output, string_address name);

static fn tar_say(string_address name, string_address message)
{
        log_error("tar: ", 5);
        tar_quoted(log_error, name);
        string_format(log_error, ": %s\n", message);
}

static bool tar_usage_hint(void)
{
        string_address program = program_argument(0);

        string_format(log_error, "Try '%s --help' or '%s --usage' for more information.\n",
                      program, program);
        return false;
}

/* GNU's create-side diagnostics name what failed: "Cannot stat: ...". */
static fn tar_fail_at(string_address what, string_address doing, bipolar failed)
{
        log_error("tar: ", 5);
        tar_quoted(log_error, what);
        string_format(log_error, ": %s: %s\n", doing, file_reason(failed));
        tar_status = 2;
}

static fn tar_refuse(string_address message)
{
        string_format(log_error, "tar: %s\n", message);
        tar_status = 2;
}

static bool tar_refuse_pack(p8 pack)
{
        if (pack == TAR_PACK_BZIP2)
        {
                tar_refuse("bzip2 is not this tar");
                return true;
        }

        if (pack == TAR_PACK_COMPRESS)
        {
                tar_refuse("compress is not this tar");
                return true;
        }

        return false;
}

#define path_table_record(table, at)                                        \
        ((path_table_key address_to)((table)->records + (at) * (table)->stride))

static bool path_table_prepare(path_table address_to table, positive wanted)
{
        positive larger = table->slots ? table->slots : 64;

        if (table->slots && wanted <= table->slots / 2)
                return true;
        while (wanted > larger / 2)
        {
                if (larger > positive_max / 2)
                        return false;
                larger *= 2;
        }
        if (!shell_array_room(table->index, table->index_room, larger))
                return false;
        memory_fill(table->index, 0, larger * sizeof(table->index[0]));
        for (positive at = 0; at < table->count; at++)
        {
                positive slot = path_table_record(table, at)->path_hash &
                                (larger - 1);

                while (table->index[slot])
                        slot = (slot + 1) & (larger - 1);
                table->index[slot] = at + 1;
        }
        table->slots = larger;
        return true;
}

static p8 address_to path_table_find(path_table address_to table,
                                     string_address path, positive hash)
{
        if (!table->slots)
                return null;
        for (positive slot = hash & (table->slots - 1); table->index[slot];
             slot = (slot + 1) & (table->slots - 1))
        {
                path_table_key address_to kept =
                    path_table_record(table, table->index[slot] - 1);

                if (kept->path_hash == hash &&
                    string_equals(table->paths + kept->path_at, path))
                        return (p8 address_to)kept;
        }
        return null;
}

/* A zeroed record for a path not yet held, or null when memory is short. */
static p8 address_to path_table_add(path_table address_to table,
                                    string_address path, positive2 named)
{
        positive length = named.y + 1;

        if (named.y == positive_max ||
            length > positive_max - table->paths_used ||
            !path_table_prepare(table, table->count + 1) ||
            !shell_room((address_any address_to)address_of table->records,
                        address_of table->room, table->count + 1,
                        table->stride) ||
            !shell_array_room(table->paths, table->paths_room,
                              table->paths_used + length))
                return null;

        path_table_key address_to kept = path_table_record(table, table->count);
        positive slot = named.x & (table->slots - 1);

        memory_fill(kept, 0, table->stride);
        kept->path_at = table->paths_used;
        kept->path_hash = named.x;
        memory_copy(table->paths + table->paths_used, path, length);
        table->paths_used += length;
        while (table->index[slot])
                slot = (slot + 1) & (table->slots - 1);
        table->index[slot] = ++table->count;
        return (p8 address_to)kept;
}

static fn path_table_clear(path_table address_to table)
{
        table->count = 0;
        table->paths_used = 0;
        table->slots = 0;
}

static tar_materialized_file address_to tar_materialized_find(
    string_address path)
{
        return (tar_materialized_file address_to)path_table_find(
            address_of tar_materialized, path,
            string_hash_33_length(path).x);
}

/* parent, when not null, makes the record pending (see above); facts then
   say only that the member is a regular file. */
static bipolar tar_materialized_remember(string_address path,
                                         file_facts address_to facts,
                                         tar_identity_key address_to parent)
{
        if ((facts->mask & STATX_BASIC) != STATX_BASIC &&
            (facts->mask || ((facts->mode & MODE_FORMAT) != MODE_LINK &&
                             !parent)))
                return -ERROR_INPUT_OUTPUT;

        positive2 named = string_hash_33_length(path);
        tar_materialized_file address_to kept = (tar_materialized_file address_to)
            path_table_find(address_of tar_materialized, path, named.x);

        if (!kept)
                kept = (tar_materialized_file address_to)path_table_add(
                    address_of tar_materialized, path, named);
        if (!kept)
                return -ERROR_NO_MEMORY;
        kept->facts = *facts;
        kept->pending = parent != null;
        if (parent)
                kept->parent = *parent;
        return 0;
}

static positive tar_path_depth(string_address path)
{
        positive depth = 0;
        bool component = false;

        while (*path)
        {
                if (*path == '/')
                        component = false;
                else if (!component)
                {
                        component = true;
                        depth++;
                }
                path++;
        }
        return depth;
}

/* An extraction reads its principal once and clears the process mask for
   its whole length, so every creation lands with exactly the mode it asks
   for and no member pays to ask again.  The mask comes back when the
   archive is finished. */
static p32 tar_user;
static positive tar_session_mask;
static bool tar_restore_owner;
static bool tar_touch;

/* A parent used by a later member must remain outside another principal's
   rename control.  A trusted sticky directory is acceptable because the
   next opened entry is independently required to have a trusted owner. */
static bool tar_extract_directory_trusted(bipolar handle)
{
        file_facts facts;
        bipolar looked = file_look_code(
            handle, (string_address)"", AT_EMPTY_PATH, address_of facts);

        return looked >= 0 && (facts.mask & STATX_BASIC) == STATX_BASIC &&
               (facts.mode & MODE_FORMAT) == MODE_DIRECTORY &&
               (facts.owner == tar_user || facts.owner == 0) &&
               (!(facts.mode & 0002) || (facts.mode & MODE_STICKY));
}

/* Archives keep a directory's members together, so the parents of one
   member are nearly always the parents of the last.  Every directory
   descriptor opened on the way down stays here with the path that reached
   it: a later member reuses the shared prefix and opens, makes and checks
   only the components past it.  A held descriptor is the directory that
   was checked whatever later happens to its name, and no member can take
   a name the stack holds, since a non-directory never replaces a
   directory. */
#define TAR_STACK_DEPTH 48

typedef struct
{
        bipolar handle;
        positive stop;
        bool made;
        bool private;
        tar_identity_key identity;
} tar_stack_entry;

/* What tar_stack_parent answered last: whether that parent is a directory
   this run made, still private, and which inode it is. */
static bool tar_parent_private;
static tar_identity_key tar_parent_identity;

static tar_stack_entry tar_stack[TAR_STACK_DEPTH];
static positive tar_stack_used;
static positive tar_stack_matched;
static p8 tar_stack_path[TAR_PATH];
static bipolar tar_stack_root = -1;

static fn tar_stack_close_from(positive depth)
{
        while (tar_stack_used > depth)
                (void)system_close(tar_stack[--tar_stack_used].handle);
        if (tar_stack_matched > depth)
                tar_stack_matched = depth;
}

static fn tar_stack_release(void)
{
        tar_stack_close_from(0);
        if (tar_stack_root >= 0)
                (void)system_close(tar_stack_root);
        tar_stack_root = -1;
}

static bipolar tar_stack_base(void)
{
        if (tar_stack_root >= 0)
                return tar_stack_root;

        bipolar root = system_open_at(AT_FDCWD, (string_address)".",
                                      O_PATH | O_DIRECTORY | O_CLOEXEC);
        if (root >= 0 && !tar_extract_directory_trusted(root))
        {
                (void)system_close(root);
                root = -ERROR_ACCESS;
        }
        if (root >= 0)
                tar_stack_root = root;
        return root;
}

/* Hold handle as the child named leaf of the directory the last lookup
   matched.  False leaves handle with the caller. */
static bool tar_stack_push(bipolar handle, string_address leaf,
                           positive length, bool made,
                           file_facts address_to facts)
{
        positive depth = tar_stack_matched;
        positive start = depth ? tar_stack[depth - 1].stop + 1 : 0;

        if (depth >= TAR_STACK_DEPTH ||
            start + length >= sizeof(tar_stack_path))
                return false;

        tar_stack_close_from(depth);
        if (depth)
                tar_stack_path[start - 1] = '/';
        memory_copy(tar_stack_path + start, leaf, length);
        tar_stack[depth].handle = handle;
        tar_stack[depth].stop = start + length;
        tar_stack[depth].made = made;
        tar_stack[depth].private = made && facts &&
                                   (facts->mask & STATX_BASIC) == STATX_BASIC;
        if (tar_stack[depth].private)
                tar_stack[depth].identity = (tar_identity_key){
                    facts->inode, facts->device_major, facts->device_minor};
        tar_stack_used = depth + 1;
        tar_stack_matched = depth + 1;
        return true;
}

/* The entry, if the stack already holds leaf below the matched parent. */
static tar_stack_entry address_to tar_stack_child(string_address leaf,
                                                  positive length)
{
        positive depth = tar_stack_matched;
        positive start = depth ? tar_stack[depth - 1].stop + 1 : 0;

        if (depth >= tar_stack_used ||
            tar_stack[depth].stop - start != length ||
            memory_compare(tar_stack_path + start, leaf, length))
                return null;
        return tar_stack + depth;
}

/* A directory the stack holds that a later member took away: its entry and
   those below it would be a descriptor for a directory no longer in the
   tree, so they go, and the next member under that name looks again. */
static fn tar_stack_forget(string_address path)
{
        positive length = string_length(path);

        for (positive depth = 0; depth < tar_stack_used; depth++)
                if (tar_stack[depth].stop == length &&
                    !memory_compare(tar_stack_path, path, length))
                {
                        tar_stack_close_from(depth);
                        return;
                }
}

static bipolar tar_parent_walk(string_address path, p8 address_to leaf,
                               positive room, bool address_to owned)
{
        bipolar parent = system_open_parent_nofollow_checked(
            AT_FDCWD, path, true, 0700, leaf, room,
            tar_extract_directory_trusted);

        address_to owned = parent >= 0;
        tar_parent_private = false;
        return parent;
}

static bool tar_directory_remember(string_address path, positive mode,
                                   file_facts address_to facts,
                                   tar_member_meta address_to meta);

/* The parent of path and its last component.  The stack lends its
   descriptor; *owned says the component walk handed over one the caller
   closes.  Absolute names and paths deeper than the stack take that walk,
   which is what every member took before the stack existed. */
static bipolar tar_stack_parent(string_address path, p8 address_to leaf,
                                positive room, bool address_to owned)
{
        positive length = string_length(path);
        positive cut = length;

        address_to owned = false;
        string_address slash = memory_last_of(path, '/', cut);
        cut = slash ? (positive)(slash - path) + 1 : 0;
        if (!length || path[0] == '/' || cut == length)
                return tar_parent_walk(path, leaf, room, owned);
        if (length - cut >= room)
                return -ERROR_NAME_TOO_LONG;

        positive parent_length = cut ? cut - 1 : 0;
        bipolar held = tar_stack_base();
        if (held < 0)
                return held;

        positive depth = 0;
        positive at = 0;
        while (depth < tar_stack_used)
        {
                positive stop = tar_stack[depth].stop;

                if (stop > parent_length ||
                    (stop < parent_length && path[stop] != '/') ||
                    memory_compare(tar_stack_path + at, path + at, stop - at))
                        break;
                held = tar_stack[depth].handle;
                at = stop + 1;
                depth++;
        }
        tar_stack_matched = depth;

        while (at < parent_length)
        {
                p8 component[256];
                positive stop = at + memory_span_without_byte(
                    path + at, '/', parent_length - at);
                if (stop - at >= sizeof(component))
                        return -ERROR_NAME_TOO_LONG;
                memory_copy(component, path + at, stop - at);
                component[stop - at] = end;

                bool made = false;
                bipolar next = system_open_at(
                    held, component,
                    O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (next == -ERROR_NO_ENTRY)
                {
                        bipolar created = system_make_directory_at(
                            held, component, 0700);

                        made = created >= 0;
                        next = created < 0 && created != -ERROR_EXISTS
                                   ? created
                                   : system_open_at(
                                         held, component,
                                         O_PATH | O_DIRECTORY | O_NOFOLLOW |
                                             O_CLOEXEC);
                }
                if (next >= 0 && !made &&
                    !tar_extract_directory_trusted(next))
                {
                        (void)system_close(next);
                        next = -ERROR_ACCESS;
                }
                if (next < 0)
                        return next;
                /* A parent no member names ends as GNU makes it, 0777
                   under the umask, once the private working mode is no
                   longer needed; an entry naming it later replaces this. */
                file_facts facts;
                bool known = false;

                if (made)
                {
                        p8 prefix[TAR_PATH];
                        tar_member_meta meta = {0};

                        memory_copy(prefix, path, stop);
                        prefix[stop] = end;
                        if (file_look_code(next, (string_address) "", AT_EMPTY_PATH,
                                           address_of facts) >= 0)
                        {
                                known = true;
                                meta.user = facts.owner;
                                meta.group = facts.group;
                                if (!tar_directory_remember(prefix, 0777 & ~tar_session_mask,
                                                            address_of facts, address_of meta))
                                {
                                        (void)system_close(next);
                                        return -ERROR_NO_MEMORY;
                                }
                        }
                }
                if (!tar_stack_push(next, component, stop - at, made,
                                    known ? address_of facts : null))
                {
                        (void)system_close(next);
                        return tar_parent_walk(path, leaf, room, owned);
                }
                held = next;
                at = stop + 1;
        }

        memory_copy(leaf, path + cut, length - cut);
        leaf[length - cut] = end;
        tar_parent_private = tar_stack_matched &&
                             tar_stack[tar_stack_matched - 1].handle == held &&
                             tar_stack[tar_stack_matched - 1].private;
        if (tar_parent_private)
                tar_parent_identity = tar_stack[tar_stack_matched - 1].identity;
        return held;
}

#define TAR_RESOLVE_NO_MAGICLINKS 0x02
#define TAR_RESOLVE_NO_SYMLINKS 0x04
#define TAR_RESOLVE_BENEATH 0x08

/* Reopen a name this run made.  openat2 resolves it below the extraction
   root in one call and refuses a symlink anywhere on the way, which is
   the component walk's promise without a descriptor per component; an
   absolute name, a kernel without openat2 or a resolution that raced a
   rename takes the walk. */
static bipolar tar_open_beneath(string_address path, positive flags)
{
        if (path[0] != '/')
        {
                bipolar root = tar_stack_base();
                if (root < 0)
                        return root;

                struct
                {
                        p64 flags;
                        p64 mode;
                        p64 resolve;
                } how = {flags | O_NOFOLLOW | O_CLOEXEC, 0,
                         TAR_RESOLVE_BENEATH | TAR_RESOLVE_NO_SYMLINKS |
                             TAR_RESOLVE_NO_MAGICLINKS};
                bipolar opened = system_call_4(
                    syscall(openat2), (positive)root, (positive)path,
                    (positive)address_of how, sizeof(how));
                if (opened != -ERROR_NO_SYSTEM_CALL &&
                    opened != -ERROR_AGAIN)
                        return opened;
        }

        p8 leaf[TAR_PATH];
        bipolar parent = system_open_parent_nofollow(
            AT_FDCWD, path, false, 0, leaf, sizeof(leaf));
        if (parent < 0)
                return parent;
        bipolar opened = system_open_at(parent, leaf,
                                        flags | O_NOFOLLOW | O_CLOEXEC);
        (void)system_close(parent);
        return opened;
}

static bipolar tar_open_beneath_same(string_address path,
                                     file_facts address_to expected,
                                     positive flags)
{
        file_facts found;
        bipolar handle = tar_open_beneath(path, flags);
        bipolar looked = handle < 0 ? handle :
                         file_look_code(handle, (string_address)"",
                                        AT_EMPTY_PATH, address_of found);

        if (looked < 0 || !file_same_identity(expected, address_of found) ||
            (expected->mode & MODE_FORMAT) != (found.mode & MODE_FORMAT))
        {
                if (handle >= 0)
                        (void)system_close(handle);
                return looked < 0 ? looked : -ERROR_AGAIN;
        }
        return handle;
}

/* The regular file a pending record names, opened O_PATH through the
   parent it was made in: the parent must still be that private directory,
   whose names only this run can change, and the leaf is not followed. */
static bipolar tar_open_pending(string_address path,
                                tar_materialized_file address_to kept)
{
        positive length = string_length(path);
        string_address slash = memory_last_of(path, '/', length);
        p8 above[TAR_PATH];
        file_facts facts;

        if (!slash || slash == path || length >= sizeof(above))
                return -ERROR_ACCESS;
        memory_copy(above, path, (positive)(slash - path));
        above[slash - path] = end;

        bipolar parent = tar_open_beneath(above, O_PATH | O_DIRECTORY);
        bipolar looked = parent < 0 ? parent :
                         file_look_code(parent, (string_address)"",
                                        AT_EMPTY_PATH, address_of facts);
        if (looked >= 0 &&
            ((facts.mask & STATX_BASIC) != STATX_BASIC ||
             (facts.mode & MODE_FORMAT) != MODE_DIRECTORY ||
             facts.inode != kept->parent.inode ||
             facts.device_major != kept->parent.device_major ||
             facts.device_minor != kept->parent.device_minor))
                looked = -ERROR_ACCESS;
        if (looked < 0)
        {
                if (parent >= 0)
                        (void)system_close(parent);
                return looked;
        }

        bipolar opened = system_open_at(parent, slash + 1,
                                        O_PATH | O_NOFOLLOW | O_CLOEXEC);
        (void)system_close(parent);
        return opened;
}

/* A symlink member is remembered by kind alone.  A hard link to it names
   only the link itself, which the archive could have written anyway, so
   its identity would buy nothing and cost a statx per link. */
static bool tar_materialized_same(file_facts address_to authorized,
                                  file_facts address_to found)
{
        if ((found->mode & MODE_FORMAT) != (authorized->mode & MODE_FORMAT))
                return false;
        return authorized->mask ? file_same_identity(found, authorized)
                                : (found->mode & MODE_FORMAT) == MODE_LINK;
}

/* Owner first, since a change of owner clears the set-id bits; then the
   mode that puts them back; the modification time last, so nothing after
   it moves the clock.  whole asks for the mode even without special bits,
   for an object that was not born with its final mode. */
static bipolar tar_member_settle(bipolar handle, positive mode,
                                 tar_member_meta address_to meta, bool whole)
{
        bipolar settled = 0;

        if (tar_restore_owner)
                settled = system_call_3(syscall(fchown), (positive)handle,
                                        (positive)meta->user,
                                        (positive)meta->group);
        if (settled >= 0 && (whole || (mode & 07000)))
                settled = system_call_2(syscall(fchmod), (positive)handle,
                                        mode);
        if (settled >= 0 && meta->timed && !tar_touch)
        {
                p64 times[4] = {0, UTIME_OMIT, (p64)meta->seconds,
                                meta->nanoseconds};

                settled = system_update_times_at(handle, 0, times, 0);
        }
        return settled;
}


/*
        Extended attributes and ACLs, GNU's --xattrs and --acls, carried in
        pax records: SCHILY.xattr.NAME=value for each attribute (all of them,
        the raw POSIX ACL ones included), SCHILY.acl.access and
        SCHILY.acl.default as the long text form.  A file with either ACL
        gets both, the access one from its mode when it has none of its own.
        --xattrs-include and --xattrs-exclude pick attributes by pattern.
        Extraction sets them after the member's mode, since an ACL's mask
        is the mode's group bits; what cannot be set is said and is not an
        error, as GNU has it.
*/
static bool tar_opt_xattrs;
static bool tar_opt_acls;
static string_address tar_xattr_include[8];
static positive tar_xattr_include_count;
static string_address tar_xattr_exclude[8];
static positive tar_xattr_exclude_count;
static p8 address_to tar_attr_store;
static positive tar_attr_store_room;
static positive tar_attr_store_used;

#define TAR_ATTR_ROOM 65536
static p8 tar_attr_names[TAR_ATTR_ROOM];
static p8 tar_attr_value[TAR_ATTR_ROOM];

static bool tar_xattr_wanted(string_address name)
{
        positive at;

        for (at = 0; at < tar_xattr_exclude_count; at++)
                if (file_fnmatch(tar_xattr_exclude[at], name))
                        return false;
        if (!tar_xattr_include_count)
                return true;
        for (at = 0; at < tar_xattr_include_count; at++)
                if (file_fnmatch(tar_xattr_include[at], name))
                        return true;
        return false;
}

/* What the pax header of the member being made carried, kept for after
   the member is. */
static fn tar_attrs_keep(tar_member_meta address_to meta)
{
        meta->attr_at = 0;
        meta->attr_length = 0;
        if (!(tar_opt_xattrs || tar_opt_acls) || !tar_pax_attr_used ||
            !shell_array_room(tar_attr_store, tar_attr_store_room,
                              tar_attr_store_used + tar_pax_attr_used))
                return;
        memory_copy(tar_attr_store + tar_attr_store_used, tar_pax_attr,
                    tar_pax_attr_used);
        meta->attr_at = tar_attr_store_used;
        meta->attr_length = tar_pax_attr_used;
        tar_attr_store_used += tar_pax_attr_used;
}

#define TAR_ACL_USER_OBJ 1
#define TAR_ACL_USER 2
#define TAR_ACL_GROUP_OBJ 4
#define TAR_ACL_GROUP 8
#define TAR_ACL_MASK 0x10
#define TAR_ACL_OTHER 0x20

typedef struct
{
        p16 tag;
        p16 perm;
        p32 id;
} tar_acl_entry;

/* The kernel's ACL attribute as libacl's long text form. */
static bool tar_acl_text(p8 address_to binary, positive length,
                         p8 address_to into, positive room, positive address_to used)
{
        positive at = 0;
        positive have = 0;

        if (length < 4 || (length - 4) % 8 || binary[0] != 2 || binary[1] || binary[2] ||
            binary[3])
                return false;
        for (positive entry = 4; entry < length; entry += 8)
        {
                tar_acl_entry item;
                string_address tag = null;
                p8 who[FILE_NAME_MAX];
                p8 word[24];
                positive part;

                memory_copy(address_of item, binary + entry, 8);
                switch (item.tag)
                {
                case TAR_ACL_USER_OBJ: tag = "user:"; break;
                case TAR_ACL_USER: tag = "user:"; break;
                case TAR_ACL_GROUP_OBJ: tag = "group:"; break;
                case TAR_ACL_GROUP: tag = "group:"; break;
                case TAR_ACL_MASK: tag = "mask:"; break;
                case TAR_ACL_OTHER: tag = "other:"; break;
                default: return false;
                }
                part = string_length(tag);
                who[0] = end;
                if (item.tag == TAR_ACL_USER || item.tag == TAR_ACL_GROUP)
                {
                        bool named = !tar_numeric_owner &&
                                     (item.tag == TAR_ACL_USER
                                          ? file_user_name(item.id, who, sizeof(who))
                                          : file_group_name(item.id, who, sizeof(who)));

                        if (!named)
                                who[positive_into(who, item.id)] = end;
                }
                positive need = part + string_length((string_address)who) + 5;

                if (have + need > room)
                        return false;
                memory_copy(into + have, tag, part);
                have += part;
                memory_copy(into + have, who, string_length((string_address)who));
                have += string_length((string_address)who);
                into[have++] = ':';
                word[0] = item.perm & 4 ? 'r' : '-';
                word[1] = item.perm & 2 ? 'w' : '-';
                word[2] = item.perm & 1 ? 'x' : '-';
                memory_copy(into + have, word, 3);
                have += 3;
                into[have++] = '\n';
                at++;
        }
        /* The numeric form libacl writes has no newline after the last entry. */
        if (tar_numeric_owner && have)
                have--;
        address_to used = have;
        return at != 0;
}

/* The access ACL a mode alone gives. */
static positive tar_acl_from_mode(positive mode, p8 address_to into)
{
        static const p8 names[3][8] = {"user::", "group::", "other::"};
        positive have = 0;

        for (positive at = 0; at < 3; at++)
        {
                positive bits = (mode >> (6 - 3 * at)) & 7;
                positive length = string_length((string_address)names[at]);

                memory_copy(into + have, names[at], length);
                have += length;
                into[have++] = bits & 4 ? 'r' : '-';
                into[have++] = bits & 2 ? 'w' : '-';
                into[have++] = bits & 1 ? 'x' : '-';
                into[have++] = '\n';
        }
        return tar_numeric_owner ? have - 1 : have;
}

/* Text back to the kernel's attribute; false for anything libacl would
   refuse to read. */
static bool tar_acl_binary(string_address text, positive length,
                           p8 address_to into, positive room, positive address_to used)
{
        tar_acl_entry items[64];
        positive count = 0;
        positive at = 0;

        while (at < length)
        {
                positive stop = at;
                string_address line = text + at;
                string_address part[3] = {null, null, null};
                positive size[3] = {0, 0, 0};
                positive fields = 0;
                positive end_of = 0;

                while (stop < length && text[stop] != '\n' && text[stop] != ',')
                        stop++;
                end_of = stop - at;
                for (positive walk = 0; walk < end_of; walk++)
                        if (line[walk] == '#')
                        {
                                end_of = walk;
                                break;
                        }
                positive first = 0;

                while (first < end_of && (line[first] == ' ' || line[first] == '\t'))
                        first++;
                while (end_of > first && (line[end_of - 1] == ' ' || line[end_of - 1] == '\t'))
                        end_of--;
                at = stop + 1;
                if (first == end_of)
                        continue;
                {
                        positive from = first;

                        for (positive walk = first; walk <= end_of; walk++)
                                if (walk == end_of || line[walk] == ':')
                                {
                                        if (fields < 3)
                                        {
                                                part[fields] = line + from;
                                                size[fields] = walk - from;
                                        }
                                        fields++;
                                        from = walk + 1;
                                }
                }
                if (fields < 3 || count >= array_count(items))
                        return false;
                tar_acl_entry item;
                bool user = size[0] && (part[0][0] == 'u');
                bool group = size[0] && (part[0][0] == 'g');
                bool mask = size[0] && (part[0][0] == 'm');
                bool other = size[0] && (part[0][0] == 'o');

                item.id = (p32)-1;
                item.perm = 0;
                if (!(user || group || mask || other))
                        return false;
                if (size[1])
                {
                        p8 word[FILE_NAME_MAX];
                        positive digits;
                        positive number = 0;

                        if (mask || other || size[1] >= sizeof(word))
                                return false;
                        memory_copy(word, part[1], size[1]);
                        word[size[1]] = end;
                        number = string_digits_max((string_address)word, positive_max,
                                                   address_of digits);
                        if (digits == size[1])
                                item.id = (p32)number;
                        else
                        {
                                bipolar found = user ? file_user_id((string_address)word)
                                                     : file_group_id((string_address)word);

                                if (found < 0)
                                        return false;
                                item.id = (p32)found;
                        }
                        item.tag = user ? TAR_ACL_USER : TAR_ACL_GROUP;
                }
                else
                        item.tag = user ? TAR_ACL_USER_OBJ
                                        : group ? TAR_ACL_GROUP_OBJ
                                                : mask ? TAR_ACL_MASK : TAR_ACL_OTHER;
                if (size[2] == 1 && part[2][0] >= '0' && part[2][0] <= '7')
                        item.perm = (p16)(part[2][0] - '0');
                else
                {
                        if (size[2] > 3)
                                return false;
                        for (positive walk = 0; walk < size[2]; walk++)
                                switch (part[2][walk])
                                {
                                case 'r': item.perm |= 4; break;
                                case 'w': item.perm |= 2; break;
                                case 'x': item.perm |= 1; break;
                                case '-': break;
                                default: return false;
                                }
                }
                items[count++] = item;
        }
        if (4 + count * 8 > room)
                return false;
        for (positive one = 1; one < count; one++)
        {
                tar_acl_entry keep = items[one];
                positive back = one;

                while (back && (items[back - 1].tag > keep.tag ||
                                (items[back - 1].tag == keep.tag &&
                                 items[back - 1].id > keep.id)))
                {
                        items[back] = items[back - 1];
                        back--;
                }
                items[back] = keep;
        }
        into[0] = 2;
        into[1] = into[2] = into[3] = 0;
        memory_copy(into + 4, items, count * 8);
        address_to used = 4 + count * 8;
        return count != 0;
}

/* Set what a member carried on the object fd names. */
static fn tar_attrs_apply(bipolar fd, string_address path, positive at,
                          positive length)
{
        p8 address_to record = tar_attr_store + at;
        p8 address_to stop = record + length;

        while (record + 9 <= stop)
        {
                positive name_length;
                positive value_length;
                p8 kind = record[0];
                p8 address_to name;
                p8 address_to value;
                bipolar failed = 0;
                p8 text[64];
                string_address doing = null;

                p32 name_size;
                p32 value_size;

                memory_copy(address_of name_size, record + 1, 4);
                memory_copy(address_of value_size, record + 5, 4);
                name_length = name_size;
                value_length = value_size;
                name = record + 9;
                value = name + name_length;
                record = value + value_length;
                if (record > stop)
                        return;
                if (kind == 1)
                {
                        if (!tar_opt_xattrs || name_length >= sizeof(text))
                                continue;
                        memory_copy(text, name, name_length);
                        text[name_length] = end;
                        /* An ACL comes back from its text, under --acls. */
                        if (!tar_xattr_wanted((string_address)text) ||
                            !string_compare_max((string_address)text,
                                                "system.posix_acl_", 17))
                                continue;
#if MOONWATER_STRICT >= STRICT_TIGHT
                        /* An archive says what security.* attributes a file
                           gets -- a capability, an SELinux label -- and root
                           carrying it out gives a file what only a package
                           manager should. GNU does; STRICT_TIGHT does not. */
                        if (!string_compare_max((string_address)text, "security.", 9))
                        {
                                string_format(log_error,
                                              "tar: %s: Not setting the '%s' extended attribute from the archive\n",
                                              path, (string_address)text);
                                continue;
                        }
#endif
                        failed = system_call_5(syscall(fsetxattr), (positive)fd,
                                               (positive)text, (positive)value,
                                               value_length, 0);
                        doing = "setxattrat";
                        if (failed < 0 && failed != -EPERM && failed != -EOPNOTSUPP)
                                string_format(log_error,
                                              "tar: %s: Cannot set '%s' extended attribute for file '%s': %s\n",
                                              doing, (string_address)text, path,
                                              file_reason(failed));
                }
                else if (tar_opt_acls)
                {
                        p8 binary[4 + 64 * 8];
                        positive have = 0;

                        if (!tar_acl_binary((string_address)value, value_length, binary,
                                            sizeof(binary), address_of have))
                                failed = -ERROR_INVALID;
                        else
                                failed = system_call_5(
                                    syscall(fsetxattr), (positive)fd,
                                    (positive)(kind == 2 ? "system.posix_acl_access"
                                                         : "system.posix_acl_default"),
                                    (positive)binary, have, 0);
                        if (failed < 0 && failed != -EPERM && failed != -EOPNOTSUPP)
                                string_format(log_error,
                                              "tar: tar_acl_set_file_at: Cannot set POSIX ACLs for file '%s': %s\n",
                                              path, file_reason(failed));
                }
        }
}

static bipolar tar_member_settle_at(bipolar directory, string_address name,
                                    positive mode,
                                    tar_member_meta address_to meta,
                                    bool link)
{
        bipolar settled = 0;

        if (tar_restore_owner)
                settled = system_change_owner_at(directory, name, meta->user,
                                                 meta->group,
                                                 AT_SYMLINK_NOFOLLOW);
        if (settled >= 0 && !link)
                settled = system_change_mode_at(directory, name, mode);
        if (settled >= 0 && meta->timed && !tar_touch)
        {
                p64 times[4] = {0, UTIME_OMIT, (p64)meta->seconds,
                                meta->nanoseconds};

                settled = system_update_times_at(directory, name, times,
                                                 AT_SYMLINK_NOFOLLOW);
        }
        return settled;
}

static bool tar_directory_remember(string_address path, positive mode,
                                   file_facts address_to facts,
                                   tar_member_meta address_to meta)
{
        positive2 named = string_hash_33_length(path);
        tar_directory_mode address_to kept = (tar_directory_mode address_to)
            path_table_find(address_of tar_directories, path, named.x);

        if (!kept)
        {
                kept = (tar_directory_mode address_to)path_table_add(
                    address_of tar_directories, path, named);
                if (!kept)
                {
                        tar_refuse("out of memory while retaining directory metadata");
                        return false;
                }
                kept->depth = tar_path_depth(path);
        }
        kept->mode = mode;
        kept->facts = *facts;
        kept->meta = *meta;
        return true;
}

/* A remembered directory an archive later replaced with something else is
   no longer settled at the end. */
#define TAR_DIRECTORY_GONE ((positive)-1)

static fn tar_directory_forget(string_address path)
{
        tar_directory_mode address_to kept = (tar_directory_mode address_to)
            path_table_find(address_of tar_directories, path,
                            string_hash_33_length(path).x);

        if (kept)
                kept->mode = TAR_DIRECTORY_GONE;
}

static bipolar tar_directory_index_order(positive left, positive right)
{
        positive one = ((tar_directory_mode address_to)tar_directories.records)[left].depth;
        positive two = ((tar_directory_mode address_to)tar_directories.records)[right].depth;

        if (one != two)
                return one > two ? -1 : 1;
        return left > right ? -1 : left < right;
}

static fn tar_directories_finish(void)
{
        if (!tar_directories.count)
                return;

        if (!shell_array_room(tar_directory_order, tar_directory_order_room,
                              tar_directories.count) ||
            !shell_array_room(tar_directory_spare, tar_directory_spare_room,
                              tar_directories.count))
        {
                tar_refuse("out of memory while restoring directory metadata");
                path_table_clear(address_of tar_directories);
                return;
        }

        for (positive at = 0; at < tar_directories.count; at++)
                tar_directory_order[at] = at;
        positive address_to order = array_merge_sort(
            tar_directory_order, tar_directory_spare, tar_directories.count,
            tar_directory_index_order);

        for (positive at = 0; at < tar_directories.count; at++)
        {
                tar_directory_mode address_to kept =
                    (tar_directory_mode address_to)tar_directories.records +
                    order[at];
                string_address path = tar_directories.paths + kept->key.path_at;
                if (kept->mode == TAR_DIRECTORY_GONE)
                        continue;
                bipolar opened = tar_open_beneath_same(
                    path, address_of kept->facts,
                    FILE_READ | O_DIRECTORY);
                bipolar changed = opened < 0
                    ? opened
                    : tar_member_settle(opened, kept->mode,
                                        address_of kept->meta, true);

                if (changed >= 0 && kept->meta.attr_length)
                        tar_attrs_apply(opened, path, kept->meta.attr_at,
                                        kept->meta.attr_length);

                if (opened >= 0)
                        system_close(opened);
                if (changed < 0)
                        tar_fail(path, changed);
        }

        path_table_clear(address_of tar_directories);
}

static fn tar_reset(void)
{
        tar_have = 0;
        tar_at = 0;
        tar_seen_used = 0;
        tar_seen_fill = 0;
        if (tar_seen)
                memory_fill(tar_seen, 0, tar_seen_slots * sizeof(*tar_seen));
        tar_decoder = null;
        tar_encoder = null;
        tar_output_known = false;
        tar_output_target_known = false;
        tar_output_stage_known = false;
        path_table_clear(address_of tar_directories);
        path_table_clear(address_of tar_materialized);
        tar_attr_store_used = 0;
}

/*
        Decoding beside extraction.  A compressed archive's codec runs on a
        thread of its own and hands 128 KiB spans through a ring of eight to
        tar on the calling thread, which does everything it did before:
        headers, files, links and every message.  The spans are mapped once.
        count is the futex word both sides wait on, and the caller ends a
        producer it no longer wants by making count negative.  A span's
        length is what the codec's read answered, so the end (0) and a
        failure (-1) arrive where they happened in the stream, and read_end
        runs on the codec's thread once its stream is done.  With no second
        thread the codec reads inline, as it always did.
*/
#define TAR_RING_SPANS 8
#define TAR_RING_SPAN ((positive)1 << 17)
#define TAR_RING_QUIT ((b32)-(1 << 20))

typedef struct
{
        const tar_codec address_to codec;
        p8 address_to storage;
        bipolar length[TAR_RING_SPANS];
        positive head;
        positive tail;
        positive taken;
        b32 count;
        bool end_ok;
        bool running;
} tar_ring;

static tar_ring tar_ring_state;

/* A codec that failed once is not asked again: its error stays the one it
   first gave, as the ring's producer stops at it. */
static bool tar_decoder_failed;

static fn tar_ring_produce(address_any context, positive index)
{
        tar_ring address_to const ring = context;

        (void)index;
        for (;;)
        {
                b32 now;
                bipolar got;

                while ((now = atomic_load(address_of ring->count)) == TAR_RING_SPANS)
                        thread_wait(address_of ring->count, now);
                if (now < 0)
                        break;
                got = ring->codec->read(ring->storage + ring->head * TAR_RING_SPAN,
                                        TAR_RING_SPAN);
                ring->length[ring->head] = got;
                ring->head = (ring->head + 1) % TAR_RING_SPANS;
                now = atomic_add(address_of ring->count, 1);
                if (now < 0)
                        break;
                if (now == 0)
                        thread_wake(address_of ring->count, 1);
                if (got <= 0)
                        break;
        }
        ring->end_ok = ring->codec->read_end();
}

/* Up to n decoded bytes, waiting for spans as a read waits for a pipe;
   the end or a failure answers once nothing is left before it. */
static bipolar tar_ring_read(tar_ring address_to ring, p8 address_to into,
                             positive n)
{
        positive copied = 0;

        while (copied < n)
        {
                bipolar length;
                positive take;

                while (atomic_load(address_of ring->count) == 0)
                        thread_wait(address_of ring->count, 0);
                length = ring->length[ring->tail];
                if (length <= 0)
                        return copied ? (bipolar)copied : length;
                take = (positive)length - ring->taken;
                if (take > n - copied)
                        take = n - copied;
                memory_copy_apart(into + copied,
                                  ring->storage + ring->tail * TAR_RING_SPAN + ring->taken,
                                  take);
                copied += take;
                ring->taken += take;
                if (ring->taken == (positive)length)
                {
                        ring->taken = 0;
                        ring->tail = (ring->tail + 1) % TAR_RING_SPANS;
                        if (atomic_sub(address_of ring->count, 1) == TAR_RING_SPANS)
                                thread_wake(address_of ring->count, 1);
                }
        }
        return (bipolar)copied;
}

/* The codec's read_end, from whichever thread ran it: a producer still
   decoding is told to quit and joined first. */
static bool tar_ring_finish(const tar_codec address_to codec)
{
        tar_ring address_to const ring = address_of tar_ring_state;

        if (!ring->running)
                return codec->read_end();
        atomic_exchange(address_of ring->count, TAR_RING_QUIT);
        thread_wake(address_of ring->count, 1);
        parallel_beside_wait();
        ring->running = false;
        return ring->end_ok;
}

static fn tar_ring_start(const tar_codec address_to codec)
{
        tar_ring address_to const ring = address_of tar_ring_state;

        if (ring->running)
                tar_ring_finish(ring->codec);
        if (!ring->storage)
        {
                p8 address_to const at =
                    memory_checked(TAR_RING_SPANS * TAR_RING_SPAN);

                if (!at)
                        return;
                ring->storage = at;
        }
        tar_decoder_failed = false;
        ring->codec = codec;
        ring->head = 0;
        ring->tail = 0;
        ring->taken = 0;
        ring->count = 0;
        ring->end_ok = false;
        ring->running = parallel_beside(tar_ring_produce, ring);
}

/* The decoded bytes the ring holds next, in place: up to n of them from the
   span at its tail, without copying, or the end (0) or failure (-1) once
   nothing is left before it.  The span stays the caller's until
   tar_ring_consume gives the bytes back. */
static bipolar tar_ring_view(tar_ring address_to ring,
                             p8 address_to address_to at, positive n)
{
        bipolar length;
        positive take;

        while (atomic_load(address_of ring->count) == 0)
                thread_wait(address_of ring->count, 0);
        length = ring->length[ring->tail];
        if (length <= 0)
                return length;
        take = (positive)length - ring->taken;
        if (take > n)
                take = n;
        address_to at = ring->storage + ring->tail * TAR_RING_SPAN + ring->taken;
        return (bipolar)take;
}

static fn tar_ring_consume(tar_ring address_to ring, positive n)
{
        ring->taken += n;
        if (ring->taken < (positive)ring->length[ring->tail])
                return;
        ring->taken = 0;
        ring->tail = (ring->tail + 1) % TAR_RING_SPANS;
        if (atomic_sub(address_of ring->count, 1) == TAR_RING_SPANS)
                thread_wake(address_of ring->count, 1);
}

static bipolar tar_read_bytes(bipolar handle, p8 address_to into, positive n);

/* Archive bytes to pass over or write out as they are: the ring's own span
   when a codec runs beside, else a read into scratch.  tar_view_done hands
   them back once they are used. */
static bipolar tar_view_bytes(bipolar handle, p8 address_to address_to at,
                              positive n, p8 address_to scratch)
{
        if (tar_ring_state.running)
                return tar_ring_view(address_of tar_ring_state, at, n);
        address_to at = scratch;
        return tar_read_bytes(handle, scratch, n > TAR_RECORD ? TAR_RECORD : n);
}

static fn tar_view_done(positive n)
{
        if (tar_ring_state.running)
                tar_ring_consume(address_of tar_ring_state, n);
}

static bipolar tar_read_bytes(bipolar handle, p8 address_to into, positive n)
{
        bipolar got;

        if (tar_ring_state.running)
                return tar_ring_read(address_of tar_ring_state, into, n);
        if (!tar_decoder)
                return system_read_retry((positive)handle, into, n);
        if (tar_decoder_failed)
                return -1;
        got = tar_decoder->read(into, n);
        tar_decoder_failed = got < 0;
        return got;
}

static bool tar_write_bytes(bipolar handle, p8 address_to bytes, positive n)
{
        if (!n)
                return true;
        return tar_encoder ? tar_encoder->write(bytes, n)
                           : system_write_all((positive)handle, bytes, n) == n;
}

static bool tar_packed(void)
{
        return tar_pack > TAR_PACK_NONE && tar_pack < array_count(tar_codecs);
}

static bool tar_codec_begin_read(bipolar handle, p8 address_to magic, positive n)
{
        if (!tar_packed())
                return true;
        const tar_codec address_to codec = tar_codecs + tar_pack;
        if (!codec->read_begin(handle, magic, n))
        {
                tar_refuse("cannot decode archive");
                return false;
        }
        tar_decoder = codec;
        tar_ring_start(codec);
        return true;
}

static bool tar_bytes_zero(p8 address_to bytes, positive count)
{
        return memory_span_byte(bytes, 0, count) == count;
}

static fn tar_codec_end_read(bipolar handle)
{
        bool ok = true;
        bool excess = false;
        bool nonzero = false;

        if (!tar_decoder)
                return;

        /* Finish the codec so truncation and checksum failures cannot hide
           behind the tar end marker. Padding is permitted, but it must be
           zero and bounded: otherwise a tiny compressed suffix can demand
           unbounded expansion solely to reach the codec trailer. */
        if (tar_status != 2)
        {
                positive trailing = tar_have - tar_at;
                bipolar got = 0;

                if (trailing > TAR_TRAILING_LIMIT)
                        excess = true;
                else if (!tar_bytes_zero(tar_record + tar_at, trailing))
                        nonzero = true;

                while (!excess && !nonzero)
                {
                        positive remaining = TAR_TRAILING_LIMIT - trailing;
                        positive ask = remaining < sizeof(tar_record)
                                           ? remaining + 1
                                           : sizeof(tar_record);

                        got = tar_read_bytes(handle, tar_record, ask);
                        if (got <= 0)
                                break;
                        if (!tar_bytes_zero(tar_record, (positive)got))
                        {
                                nonzero = true;
                                break;
                        }
                        trailing += (positive)got;
                        if (trailing > TAR_TRAILING_LIMIT)
                                excess = true;
                }
                if (got < 0)
                        ok = false;
        }

        ok = tar_ring_finish(tar_decoder) && ok;
        if (excess)
                tar_refuse("compressed archive padding exceeds limit");
        else if (nonzero)
                tar_refuse("nonzero data follows archive end marker");
        else if (!ok)
                tar_refuse(*tar_decoder->why ? *tar_decoder->why
                                             : (string_address)"cannot decode archive");
        tar_decoder = null;
}

static bool tar_codec_begin_write(bipolar handle)
{
        if (!tar_packed())
                return true;
        const tar_codec address_to codec = tar_codecs + tar_pack;
        if (!codec->write_begin(handle, codec->level))
        {
                tar_refuse("cannot encode archive");
                return false;
        }
        tar_encoder = codec;
        return true;
}

static bool tar_codec_end_write(void)
{
        if (!tar_encoder)
                return true;
        bool ok = tar_encoder->write_end();
        tar_encoder = null;
        if (!ok)
                tar_refuse("cannot finish compressed archive");
        return ok;
}

static fn tar_advise(bipolar handle)
{
        system_call_4(syscall(fadvise64), (positive)handle, 0, 0,
                      TAR_ADVISE_SEQUENTIAL);
}

/* One read into the compacted record: the bytes it added, 0 at the end of
   the archive, or -1 once the read error is reported.  A pipe or a slow
   writer hands over whatever it has, so one read is never a promise of a
   whole block; callers that need one loop. */
static bipolar tar_fill(bipolar handle)
{
        bipolar got;

        //      memory_copy and not memory_copy_apart: this slides the rest
        //      of the record down over itself, and the two halves overlap
        //      whenever less than half of it has been consumed -- which is
        //      the ordinary case, 512 bytes taken out of 65,536. Under
        //      sixteen bytes the inline copy loads before it stores and
        //      either name would do; a run this long is the assembly memcpy,
        //      which is promised two regions that do not touch.
        if (tar_at && tar_at < tar_have)
                memory_copy(tar_record, tar_record + tar_at,
                            tar_have - tar_at);

        tar_have -= tar_at;
        tar_at = 0;
        got = tar_read_bytes(handle, tar_record + tar_have,
                             TAR_RECORD - tar_have);
        if (got < 0)
        {
                tar_refuse("cannot read archive");
                return -1;
        }

        tar_have += (positive)got;
        return got;
}

/* header says the block is where a member header would start.  An archive
   whose last block there is cut short ends quietly, as GNU tar drops an
   incomplete trailing block; a compressed archive still meets the trailer
   check on those bytes.  Any other block cut short is an error. */
static p8 address_to tar_next_block(bipolar handle, bool header)
{
        p8 address_to block;

        while (tar_at + TAR_BLOCK > tar_have)
        {
                bipolar got = tar_fill(handle);

                if (got < 0)
                        return null;
                if (!got)
                        break;
        }

        if (tar_at + TAR_BLOCK > tar_have)
        {
                if (tar_at >= tar_have || header)
                        return null;

                tar_refuse("unexpected EOF in archive");
                return null;
        }

        block = tar_record + tar_at;
        tar_at += TAR_BLOCK;
        return block;
}

static bool tar_skip(bipolar handle, p64 bytes, bool seekable)
{
        positive have;

        if (!bytes)
                return true;

        have = tar_have - tar_at;
        if (bytes <= have)
        {
                tar_at += (positive)bytes;
                return true;
        }

        bytes -= have;
        tar_at = 0;
        tar_have = 0;
        if (seekable && !tar_packed())
        {
                bipolar reached = system_seek(handle, (bipolar)bytes,
                                              FILE_SEEK_CUR);

                /* A seek runs past the end of a file without complaint, so
                   a member cut short inside its data would list as whole. */
                if (reached >= 0 && tar_archive_sized &&
                    (p64)reached > tar_archive_size)
                {
                        tar_refuse("unexpected EOF in archive");
                        return false;
                }
                if (reached >= 0)
                        return true;
        }

        while (bytes)
        {
                positive ask = bytes > positive_max ? positive_max
                                                    : (positive)bytes;
                p8 address_to at;
                bipolar got = tar_view_bytes(handle, address_of at, ask,
                                             tar_record);

                if (got <= 0)
                {
                        tar_refuse("unexpected EOF in archive");
                        return false;
                }

                tar_view_done((positive)got);
                bytes -= (positive)got;
        }

        return true;
}

static bool tar_copy_out(bipolar in, bipolar out, p64 size,
                         file_copy_stop address_to stopped)
{
        bool range_copy = true;
        bool send_copy = true;

        if (!size)
                return true;

        return file_copy_stream(in, out, size, true, address_of range_copy,
                                address_of send_copy, null, stopped);
}

static bool tar_rewind_unread(bipolar handle)
{
        positive unread = tar_have - tar_at;

        if (!unread)
                return true;

        if (system_seek(handle, -(bipolar)unread, FILE_SEEK_CUR) < 0)
                return false;

        tar_at = 0;
        tar_have = 0;
        return true;
}

/*
        Why the last member's bytes could not be written, for the line that
        names it: a full filesystem is "No space left on device", not an
        input/output error. Zero when every byte went.
*/
static bipolar tar_write_failure;

static bipolar tar_write_reason(bipolar out, const p8 address_to bytes,
                                positive count)
{
        while (count)
        {
                bipolar wrote = system_write_once((positive)out, bytes, count);

                if (wrote == -EINTR)
                        continue;
                if (wrote <= 0)
                        return wrote < 0 ? wrote : -ENOSPC;

                bytes += wrote;
                count -= (positive)wrote;
        }

        return 0;
}

static bool tar_copy_n(bipolar archive, bipolar out, p64 size, bool seekable)
{
        p64 left = size;

        tar_write_failure = 0;

        while (left)
        {
                positive have;
                positive take;

                if (left >= TAR_RECORD && !tar_packed() &&
                    tar_rewind_unread(archive))
                {
                        file_copy_stop stopped = {0};

                        // A member that could not be written says why, and
                        // the rest of it is passed over so the next header
                        // is where it should be; one that could not be
                        // read, or ran out, stops here as before.
                        if (out >= 0 &&
                            !tar_copy_out(archive, out, left,
                                          address_of stopped))
                        {
                                if (stopped.failure &&
                                    tar_skip(archive, stopped.unmoved,
                                             seekable))
                                        tar_write_failure = stopped.failure;
                                return false;
                        }

                        if (out < 0 && !tar_skip(archive, left, seekable))
                                return false;

                        return !tar_write_failure;
                }

                /* A packed member past what the record holds is written
                   from the codec's own span, not copied into the record
                   first. */
                if (tar_at >= tar_have && tar_packed())
                {
                        p8 address_to at;
                        bipolar got = tar_view_bytes(
                            archive, address_of at,
                            left > positive_max ? positive_max : (positive)left,
                            tar_record);

                        tar_at = 0;
                        tar_have = 0;
                        if (got <= 0)
                        {
                                tar_write_failure = 0;
                                if (got < 0)
                                        tar_refuse("cannot read archive");
                                tar_refuse("unexpected EOF in archive");
                                return false;
                        }
                        if (out >= 0)
                        {
                                bipolar wrote = tar_write_reason(
                                    out, at, (positive)got);

                                if (wrote < 0)
                                {
                                        tar_write_failure = wrote;
                                        out = -1;
                                }
                        }
                        tar_view_done((positive)got);
                        left -= (positive)got;
                        continue;
                }

                /* An archive that ends early stops here whatever was written:
                   nothing is left to read past, and a failed write before it
                   must not send the caller on to skip padding that is not
                   there. */
                if (tar_at >= tar_have && tar_fill(archive) <= 0)
                {
                        tar_write_failure = 0;
                        tar_refuse("unexpected EOF in archive");
                        return false;
                }

                have = tar_have - tar_at;
                if (!have)
                {
                        tar_write_failure = 0;
                        tar_refuse("unexpected EOF in archive");
                        return false;
                }

                take = have > left ? (positive)left : have;
                if (out >= 0)
                {
                        bipolar wrote = tar_write_reason(out, tar_record + tar_at,
                                                         take);

                        // The rest of the member is read past rather than
                        // written, so the next header is where it should be.
                        if (wrote < 0)
                        {
                                tar_write_failure = wrote;
                                out = -1;
                        }
                }

                tar_at += take;
                left -= take;
        }

        return !tar_write_failure;
}

static bool tar_deliver(bipolar archive, bipolar out, p64 size, bool seekable)
{
        bool copied = tar_copy_n(archive, out, size, seekable);

        // A member that could not be written was still read to its end, and
        // its padding goes as well; one that could not be read stops here.
        if (!copied && !tar_write_failure)
                return false;

        return tar_skip(archive, tar_padded(size) - size, seekable) && copied;
}

static bool tar_write_zeros(bipolar out, p64 size)
{
        p8 zero[TAR_BLOCK];

        if (out < 0)
                return true;

        memory_fill(zero, 0, sizeof(zero));
        while (size)
        {
                positive take = size > sizeof(zero) ? sizeof(zero)
                                                    : (positive)size;

                if (system_write_all((positive)out, zero, take) != take)
                        return false;

                size -= take;
        }

        return true;
}

#define TAR_SPARSE_HEADER 4
#define TAR_SPARSE_EXTRA 21

typedef struct
{
        p64 offset;
        p64 bytes;
} tar_sparse_span;

static tar_sparse_span address_to tar_sparse;
static positive tar_sparse_room;
static positive tar_sparse_used;
static p64 tar_sparse_real;
static bool tar_sparse_active;

static fn tar_sparse_clear(void)
{
        tar_sparse_used = 0;
        tar_sparse_real = 0;
        tar_sparse_active = false;
}

/*      A span of no bytes is kept: it is how a map says the file carries on
        past its last data as a hole -- GNU writes one at the real size when
        the file ends in one, and the extraction's length is where the map
        ends, not the real size, which only the listing shows. */
static bool tar_sparse_add(p64 offset, p64 bytes)
{
        if (offset > (p64)-1 - bytes)
                return false;

        if (tar_sparse_used >= TAR_SPARSE_MAX ||
            !shell_array_room(tar_sparse, tar_sparse_room, tar_sparse_used + 1))
                return false;

        if (tar_sparse_used &&
            offset < tar_sparse[tar_sparse_used - 1].offset +
                         tar_sparse[tar_sparse_used - 1].bytes)
                return false;

        tar_sparse[tar_sparse_used].offset = offset;
        tar_sparse[tar_sparse_used].bytes = bytes;
        tar_sparse_used++;
        return true;
}

/*      One entry of a map: 1 taken, 0 the end of this block's entries (a
        byte count that starts with a NUL is an unused slot, as GNU reads
        it, where a written zero is a span of no bytes), -1 refused. */
static bipolar tar_sparse_entry(p8 address_to field)
{
        p64 offset;
        p64 bytes;

        if (!field[12])
                return 0;

        if (!tar_field_value(field, 12, address_of offset) ||
            !tar_field_value(field + 12, 12, address_of bytes))
                return -1;

        return tar_sparse_add(offset, bytes) ? 1 : -1;
}

static bool tar_sparse_load(bipolar archive, p8 address_to header)
{
        positive at;
        bool extended;
        bipolar taken = 1;

        tar_sparse_clear();
        for (at = 0; at < TAR_SPARSE_HEADER && taken > 0; at++)
                if ((taken = tar_sparse_entry(header + 386 + at * 24)) < 0)
                        return false;

        extended = header[482] != 0;
        if (!tar_field_value(header + 483, 12, address_of tar_sparse_real))
                return false;

        while (extended)
        {
                p8 address_to extra = tar_next_block(archive, false);

                if (!extra)
                        return false;

                taken = 1;
                for (at = 0; at < TAR_SPARSE_EXTRA && taken > 0; at++)
                        if ((taken = tar_sparse_entry(extra + at * 24)) < 0)
                                return false;

                extended = extra[504] != 0;
        }

        for (at = 0; at < tar_sparse_used; at++)
                if (tar_sparse[at].offset + tar_sparse[at].bytes >
                    tar_sparse_real)
                        return false;

        tar_sparse_active = true;
        return true;
}

/*
        A sparse member the pax header described: its map from the records
        (0.0, 0.1) or from the text ahead of the data (1.0), which is read
        here a block at a time and taken out of the member's size.
*/
static bool tar_pax_sparse_take(bipolar archive, p64 address_to size)
{
        positive at;

        if (tar_psp_bad)
                return false;
        tar_sparse_clear();
        tar_sparse_real = tar_psp_real;
        if (tar_psp_kind == 1)
        {
                for (at = 0; at < tar_psp_used; at++)
                        if (!tar_sparse_add(tar_psp[at].offset, tar_psp[at].bytes))
                                return false;
        }
        else
        {
                p64 wanted = 0;
                p64 seen = 0;
                positive consumed = 0;
                p64 pending = 0;
                p64 accumulated = 0;
                positive digits = 0;
                bool first = true;
                bool half = false;

                //      The text is taken a block at a time and never held:
                //      a number is what its digits add up to when its
                //      newline comes, and anything else before the last one
                //      refuses the map.
                for (;;)
                {
                        p8 address_to block;

                        if (consumed >= (p64)*size)
                                return false;
                        block = tar_next_block(archive, false);
                        if (!block)
                                return false;
                        consumed += TAR_BLOCK;
                        for (at = 0; at < TAR_BLOCK; at++)
                        {
                                p8 byte = block[at];

                                if (byte >= '0' && byte <= '9')
                                {
                                        if (accumulated > ((p64)bipolar_max - (p64)(byte - '0')) / 10)
                                                return false;
                                        accumulated = accumulated * 10 + (p64)(byte - '0');
                                        digits++;
                                        continue;
                                }
                                if (byte != '\n' || !digits)
                                        return false;
                                if (first)
                                {
                                        wanted = accumulated;
                                        if (wanted > TAR_SPARSE_MAX)
                                                return false;
                                        first = false;
                                }
                                else if (!half)
                                {
                                        pending = accumulated;
                                        half = true;
                                }
                                else
                                {
                                        if (!tar_sparse_add(pending, accumulated))
                                                return false;
                                        half = false;
                                        seen++;
                                }
                                accumulated = 0;
                                digits = 0;
                                if (seen == wanted && !first && !half)
                                        goto mapped;
                        }
                }
mapped:
                if ((p64)consumed > address_to size)
                        return false;
                address_to size -= (p64)consumed;
        }
        for (at = 0; at < tar_sparse_used; at++)
                if (tar_sparse[at].offset + tar_sparse[at].bytes > tar_sparse_real)
                        return false;
        tar_sparse_active = true;
        return true;
}

static p64 tar_sparse_payload(void)
{
        p64 held = 0;
        positive at;

        for (at = 0; at < tar_sparse_used; at++)
                held += tar_sparse[at].bytes;

        return held;
}

/*
        The gaps between a sparse member's spans are holes where the output
        can have them, as GNU makes them: each span is written where it
        belongs, and a span of no bytes sets the length there. Nothing bounds
        an offset but the real size, a twelve byte field that base-256
        stretches past 2^64, so a member of two hundred bytes can place its
        end a petabyte out -- written as zeros, that filled the disk it was
        extracted to. A pipe (-O into one) cannot hold a hole and is given
        the zeros, which is what GNU gives it. Positions count from where the
        output stood when the member began, so a member written after another
        onto one standard output lands after it.
*/
static bool tar_deliver_sparse(bipolar archive, bipolar out, p64 size,
                               bool seekable)
{
        p64 cursor = 0;
        p64 copied = 0;
        positive at;
        bipolar start = out >= 0 ? system_seek(out, 0, FILE_SEEK_CUR) : -1;

        if (!tar_sparse_active || tar_sparse_payload() != size)
        {
                tar_refuse("invalid sparse archive");
                return false;
        }

        for (at = 0; at < tar_sparse_used; at++)
        {
                p64 offset = tar_sparse[at].offset;
                bipolar moved = 0;

                if (offset < cursor)
                        return false;

                if (start < 0)
                {
                        if (!tar_write_zeros(out, offset - cursor))
                                return false;
                }
                else if (offset > (p64)bipolar_max - (p64)start)
                        moved = -ERROR_FILE_TOO_LARGE;
                else if (!tar_sparse[at].bytes)
                        moved = system_truncate_handle(out, start + (bipolar)offset);
                else
                        moved = system_seek(out, start + (bipolar)offset, FILE_SEEK_SET);

                // Said as a write failure, and the rest of the member passed
                // over so the next header is where it should be.
                if (moved < 0)
                {
                        tar_write_failure = moved;
                        (void)tar_skip(archive, tar_padded(size) - copied, seekable);
                        return false;
                }

                if (!tar_copy_n(archive, out, tar_sparse[at].bytes, seekable))
                        return false;

                copied += tar_sparse[at].bytes;
                cursor = offset + tar_sparse[at].bytes;
        }

        return tar_skip(archive, tar_padded(size) - size, seekable);
}

static bool tar_flush(bipolar handle)
{
        if (!tar_at)
                return true;

        if (!tar_write_bytes(handle, tar_record, tar_at))
        {
                tar_refuse("cannot write archive");
                tar_create_fatal = true;
                return false;
        }

        tar_out_bytes += tar_at;
        tar_at = 0;
        return true;
}

static bool tar_put(bipolar handle, p8 address_to bytes, positive length)
{
        while (length)
        {
                positive room;
                positive take;

                if (tar_at == TAR_RECORD && !tar_flush(handle))
                        return false;

                room = TAR_RECORD - tar_at;
                take = length > room ? room : length;
                memory_copy(tar_record + tar_at, bytes, take);
                tar_at += take;
                bytes += take;
                length -= take;
        }

        return true;
}

static bool tar_write_block(bipolar handle, p8 address_to block)
{
        return tar_put(handle, block, TAR_BLOCK);
}

static bool tar_write_padding(bipolar handle, p64 size)
{
        positive pad = tar_padded(size) - (positive)size;

        while (pad)
        {
                positive take;

                if (tar_at == TAR_RECORD && !tar_flush(handle))
                        return false;

                take = TAR_RECORD - tar_at;
                if (take > pad)
                        take = pad;

                memory_fill(tar_record + tar_at, 0, take);
                tar_at += take;
                pad -= take;
        }

        return true;
}

/* The file's next size bytes into the archive, unpadded.  What could not be
   read because the file ended early is answered, and -1 is a write that
   failed. */
static bipolar tar_put_span(bipolar archive, bipolar in, p64 size)
{
        p64 left = size;

        if (size >= TAR_DIRECT_MIN && !tar_packed())
        {
                tar_advise(in);
                if (!tar_flush(archive) ||
                    !tar_copy_out(in, archive, size, null))
                        return -1;

                tar_out_bytes += size;
                return 0;
        }

        while (left)
        {
                positive room;
                bipolar got;

                if (tar_at == TAR_RECORD && !tar_flush(archive))
                        return -1;

                room = TAR_RECORD - tar_at;
                got = system_read_retry((positive)in, tar_record + tar_at,
                                        left > room ? room : (positive)left);
                if (got < 0)
                        return -1;
                if (!got)
                        break;

                tar_at += (positive)got;
                left -= (positive)got;
        }

        return (bipolar)left;
}

/* A file that ended before the size its header gave is padded with zeros
   and said, as GNU says it. */
static bool tar_put_shortfall(bipolar archive, string_address name, p64 left)
{
        p8 shown[24];

        shown[positive_into(shown, (positive)left)] = end;
        string_format(log_error, "tar: %w: File shrank by %s bytes; padding with zeros\n",
                      writer_terminal_name, name, (string_address)shown);
        tar_status = 2;
        while (left)
        {
                positive room;

                if (tar_at == TAR_RECORD && !tar_flush(archive))
                        return false;
                room = TAR_RECORD - tar_at;
                if (room > left)
                        room = (positive)left;
                memory_fill(tar_record + tar_at, 0, room);
                tar_at += room;
                left -= room;
        }
        return true;
}

static bool tar_put_file(bipolar archive, bipolar in, p64 size,
                         string_address name)
{
        bipolar rest = tar_put_span(archive, in, size);

        if (rest < 0 || (rest > 0 && !tar_put_shortfall(archive, name, (p64)rest)))
                return false;
        return tar_write_padding(archive, size);
}

static bool tar_read_payload(bipolar handle, p64 size, p8 address_to into,
                             positive room, bool seekable)
{
        p64 left = size;
        p8 address_to dst = into;

        if (size >= room)
        {
                tar_refuse("member name is too long");
                tar_skip(handle, tar_padded(size), seekable);
                return false;
        }

        while (left)
        {
                positive have;
                positive take;

                if (tar_at >= tar_have && tar_fill(handle) <= 0)
                {
                        tar_refuse("unexpected EOF in archive");
                        return false;
                }

                have = tar_have - tar_at;
                if (!have)
                {
                        tar_refuse("unexpected EOF in archive");
                        return false;
                }

                take = have > left ? (positive)left : have;
                memory_copy(dst, tar_record + tar_at, take);
                tar_at += take;
                dst += take;
                left -= take;
        }

        into[size] = end;
        return tar_skip(handle, tar_padded(size) - size, seekable);
}

static bool tar_name_matches(string_address name, string_address wanted)
{
        positive keep = string_length(wanted);

        if (string_equals(name, wanted))
                return true;

        return keep && string_length(name) > keep &&
               !memory_compare(name, wanted, keep) && name[keep] == '/';
}

/* The words that are not options, in order, wherever among the options they
   stood: GNU's getopt takes an option after a file name. */
static string_address address_to tar_words;
static positive tar_word_count;

/* Which named operands some member matched; the rest are said at the end. */
static p8 address_to tar_matched;
static positive tar_matched_count;
static bool tar_fatal_exit;

/*
        --owner and --group: the user and group every member is written
        with, as NAME, ID or NAME:ID. A number alone has no name, a name is
        looked up for its number and, unknown, leaves the file's own, and
        NAME:ID takes both as given.
*/
static bool tar_owner_forced[2];
static bipolar tar_owner_forced_id[2];
static p8 tar_owner_forced_name[2][32];

static bool tar_owner_parse(string_address text, bool group)
{
        string_address colon = string_first_of(text, ':');
        positive name_length = colon ? (positive)(colon - text) : string_length(text);
        p8 address_to name = tar_owner_forced_name[group];
        bipolar id = -1;

        tar_owner_forced[group] = true;
        tar_owner_forced_id[group] = -1;
        name[0] = end;
        if (colon)
        {
                positive number;

                if (!string_digits_checked_exact(colon + 1, 10, address_of number) || number >= p32_max)
                {
                        string_format(log_error, "tar: Invalid %s: %s\n",
                                      group ? "group" : "owner", text);
                        tar_status = 2;
                        return false;
                }
                id = (bipolar)number;
        }
        else if (string_digits_exact(text, null))
        {
                positive number;

                if (!string_digits_checked_exact(text, 10, address_of number) || number >= p32_max)
                        return true;
                tar_owner_forced_id[group] = (bipolar)number;
                // The name the number has here, if it has one.
                (void)(group ? file_group_name(number, name, 32) : file_user_name(number, name, 32));
                return true;
        }
        if (name_length && name_length < 32)
        {
                memory_copy(name, text, name_length);
                name[name_length] = end;
        }
        if (!colon && name_length)
                id = group ? file_group_id((string_address)name) : file_user_id((string_address)name);
        tar_owner_forced_id[group] = id;
        return true;
}

/*
        --exclude and -X: a name is left out when a pattern matches it, or a
        directory that leads to it, as GNU's exclude does with wildcards on
        and slashes matched by * -- so --exclude=foo leaves out a/foo and
        everything under it. Unless --anchored, a pattern may begin after any
        slash of the name.
*/
#define TAR_EXCLUDE_MAX 4096
static string_address tar_exclude[TAR_EXCLUDE_MAX];
static positive tar_exclude_count;
static bool tar_exclude_anchored;
static bool tar_dereference;
static bool tar_files_null;

static bool tar_transform_add(string_address expression);
static string_address tar_transformed(string_address name);
static fn tar_transforms_reset(void);
static positive tar_transform_count_now(void);

static bool tar_excluded(string_address name)
{
        if (!tar_exclude_count)
                return false;

        positive length = string_length(name);
        p8 part[TAR_PATH];

        for (positive start = 0; start <= length;)
        {
                for (positive stop = start; stop <= length; stop++)
                {
                        if (stop != length && name[stop] != '/')
                                continue;
                        if (stop == start || stop - start >= sizeof(part))
                                continue;

                        memory_copy(part, name + start, stop - start);
                        part[stop - start] = end;
                        for (positive at = 0; at < tar_exclude_count; at++)
                                if (file_fnmatch(tar_exclude[at], (string_address)part))
                                        return true;
                }
                if (tar_exclude_anchored)
                        break;

                string_address slash = string_first_of(name + start, '/');

                if (!slash)
                        break;
                start = (positive)(slash - name) + 1;
        }
        return false;
}

// A whole file read in, for a list of patterns or of names: false when it
// cannot be read, said the way GNU says it.
static bool tar_read_list(string_address path, byte_store address_to into)
{
        bool standard = string_equals(path, "-");
        bipolar handle = standard ? 0 : system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);

        if (handle < 0)
        {
                tar_fail(path, handle);
                tar_fatal_exit = true;
                return false;
        }

        into->used = 0;
        for (;;)
        {
                if (!byte_store_reserve(into, into->used + 4097, 4096))
                {
                        tar_refuse("out of memory");
                        return false;
                }

                bipolar got = system_read_retry((positive)handle, into->bytes + into->used, 4096);

                if (got < 0)
                {
                        tar_fail(path, got);
                        tar_fatal_exit = true;
                        if (!standard)
                                system_close((positive)handle);
                        return false;
                }
                if (!got)
                        break;
                into->used += (positive)got;
        }
        into->bytes[into->used] = end;
        if (!standard)
                system_close((positive)handle);
        return true;
}


static bool tar_wanted(string_address name, positive first, positive count)
{
        positive at;
        bool found = false;

        if (tar_excluded(name))
                return false;

        if (first >= count)
                return true;

        for (at = first; at < count; at++)
                if (tar_name_matches(name, tar_words[at]))
                {
                        if (tar_matched && at - first < tar_matched_count)
                                tar_matched[at - first] = 1;
                        found = true;
                }

        return found;
}

/* Extraction publishes a complete staged inode over the destination only if
   that name still identifies the object observed before staging began.  A
   missing name remains a no-clobber decision, and a non-directory member
   never removes a directory tree. */

static fn tar_directory_forget(string_address path);

static bipolar tar_extract_destination(
    bipolar directory, string_address leaf, string_address path,
    file_facts address_to replaced, bool address_to replaced_known)
{
        address_to replaced_known = false;
        bipolar looked = file_look_code(
            directory, leaf, AT_SYMLINK_NOFOLLOW, replaced);

        if (looked == -ERROR_NO_ENTRY)
                return 0;
        if (looked < 0)
                return looked;
        if ((replaced->mask & STATX_BASIC) != STATX_BASIC)
                return -ERROR_INPUT_OUTPUT;
        /*      An empty directory gives way, as the reference's rmdir
                does; a directory with anything in it is refused. */
        if ((replaced->mode & MODE_FORMAT) == MODE_DIRECTORY)
        {
                if (system_remove_at(directory, leaf, AT_REMOVEDIR) < 0)
                        return -ERROR_IS_DIRECTORY;
                tar_directory_forget(path);
                tar_stack_forget(path);
                return 0;
        }

        address_to replaced_known = true;
        return 0;
}

/* -C establishes the root for every later AT_FDCWD member operation.  Open
   and pin the complete directory walk first so a writable ancestor cannot
   exchange one component for a symlink during privileged extraction. */
static bipolar tar_change_directory(string_address path)
{
        bipolar directory = system_open_directory_nofollow(AT_FDCWD, path);
        if (directory < 0)
                return directory;

        bipolar changed = system_call_1(syscall(fchdir),
                                        (positive)directory);
        (void)system_close(directory);
        return changed;
}

/* A name that already exists takes the staged transaction: a complete
   object is published over the destination only if that name still
   identifies the object observed before staging began, and a
   non-directory member never removes a directory tree. */
static bool tar_extract_regular_staged(bipolar archive, bipolar directory,
                                       string_address leaf,
                                       string_address path, p64 size,
                                       positive mode, bool seekable,
                                       tar_member_meta address_to meta)
{
        system_path_stage protected;
        file_facts materialized;
        file_facts replaced;
        bool replaced_known;
        bipolar looked = tar_extract_destination(
            directory, leaf, path, address_of replaced, address_of replaced_known);
        if (looked < 0)
        {
                tar_fail(path, looked);
                return tar_skip(archive, tar_padded(size), seekable);
        }

        bipolar made = file_stage_file_open_at(
            address_of protected, directory, leaf, 0600);
        if (made < 0)
        {
                tar_fail(path, made);
                return tar_skip(archive, tar_padded(size), seekable);
        }

        if ((tar_sparse_active &&
             !tar_deliver_sparse(archive, made, size, seekable)) ||
            (!tar_sparse_active &&
             !tar_deliver(archive, made, size, seekable)))
        {
                (void)file_stage_publish_protected_at(
                    address_of protected, directory, leaf, made,
                    -ERROR_INPUT_OUTPUT, false,
                    replaced_known ? address_of replaced : null, 0);
                tar_fail(path, tar_write_failure ? tar_write_failure
                                                 : -ERROR_INPUT_OUTPUT);
                return false;
        }

        bipolar changed = tar_member_settle(made, mode, meta, true);
        if (changed >= 0 && meta->attr_length)
                tar_attrs_apply(made, path, meta->attr_at, meta->attr_length);
        bipolar published_handle = -1;
        bipolar published = file_stage_publish_protected_keep_at(
            address_of protected, directory, leaf, made, changed, false,
            replaced_known ? address_of replaced : null, 0,
            address_of published_handle, false);
        if (published >= 0)
                published = file_look_code(
                    published_handle, (string_address)"", AT_EMPTY_PATH,
                    address_of materialized);
        if (published >= 0 &&
            ((materialized.mask & STATX_BASIC) != STATX_BASIC ||
             (materialized.mode & MODE_FORMAT) != MODE_FILE))
                published = -ERROR_INPUT_OUTPUT;
        if (published_handle >= 0)
                /* This is an O_PATH identity pin.  Closing it cannot undo a
                   member already published into the extraction namespace. */
                (void)system_close(published_handle);
        if (published >= 0)
                published = tar_materialized_remember(
                    path, address_of materialized, null);
        if (published < 0)
                tar_fail(path, published);
        return published >= 0;
}

/* A regular member is created under its own name with O_EXCL and
   O_NOFOLLOW, in a parent the stack pinned: the name did not exist, so
   nothing planted there is followed and no other object is touched, and
   the bytes go straight in.  Only a name that exists pays for the staged
   transaction.  A member that fails part way is removed through the
   descriptor that made it. */
static bool tar_extract_regular(bipolar archive, bipolar directory,
                                string_address leaf, string_address path,
                                p64 size, positive mode, bool seekable,
                                tar_member_meta address_to meta)
{
        file_facts materialized;
        bipolar made = system_open_at_mode(
            directory, leaf,
            FILE_WRITE | FILE_EXCLUSIVE | O_NOFOLLOW | O_CLOEXEC,
            mode & 0777);

        if (made == -ERROR_EXISTS)
                return tar_extract_regular_staged(archive, directory, leaf,
                                                  path, size, mode, seekable,
                                                  meta);
        if (made < 0)
        {
                tar_fail(path, made);
                return tar_skip(archive, tar_padded(size), seekable);
        }

        if ((tar_sparse_active &&
             !tar_deliver_sparse(archive, made, size, seekable)) ||
            (!tar_sparse_active &&
             !tar_deliver(archive, made, size, seekable)))
        {
                (void)system_path_remove_opened_at(directory, leaf, made, 0);
                (void)system_close(made);
                tar_fail(path, tar_write_failure ? tar_write_failure
                                                 : -ERROR_INPUT_OUTPUT);
                return false;
        }

        /* In a private parent the identity waits for a hard link to ask
           for it; elsewhere it is read now, through the descriptor. */
        bool pending = tar_parent_private;
        tar_identity_key parent = tar_parent_identity;
        bipolar settled = tar_member_settle(made, mode, meta, false);
        if (settled >= 0 && meta->attr_length)
                tar_attrs_apply(made, path, meta->attr_at, meta->attr_length);
        if (settled >= 0 && pending)
        {
                memory_fill(address_of materialized, 0, sizeof(materialized));
                materialized.mode = MODE_FILE;
        }
        else if (settled >= 0)
                settled = file_look_code(made, (string_address)"",
                                         AT_EMPTY_PATH,
                                         address_of materialized);
        if (settled >= 0 && !pending &&
            (materialized.mask & STATX_BASIC) != STATX_BASIC)
                settled = -ERROR_INPUT_OUTPUT;
        bipolar closed = system_close(made);
        if (settled >= 0)
                settled = closed;
        if (settled >= 0)
                settled = tar_materialized_remember(
                    path, address_of materialized,
                    pending ? address_of parent : null);
        if (settled < 0)
                tar_fail(path, settled);
        return settled >= 0;
}

/* A directory this run made is private (0700, ours) until the archive is
   finished and needs no checking; one that was already there gets the
   owner, name-stability and traversal checks and the same private working
   mode.  Either way its final mode, owner and time wait for the end,
   applied deepest first to the inode seen here, and its descriptor joins
   the stack for the members inside it. */
static bipolar tar_extract_directory(bipolar parent, bool parent_owned,
                                     string_address leaf,
                                     string_address path,
                                     positive final_mode,
                                     tar_member_meta address_to meta)
{
        positive length = string_length(leaf);
        tar_stack_entry address_to held =
            parent_owned ? null : tar_stack_child(leaf, length);
        bipolar exact;
        bool ours;
        bipolar made = 0;
        file_facts facts;

        if (held)
        {
                exact = held->handle;
                ours = held->made;
        }
        else
        {
                made = system_make_directory_at(parent, leaf, 0700);
                ours = made >= 0;
                if (made == -ERROR_EXISTS)
                        made = 0;
                exact = made < 0 ? made : system_open_at(
                    parent, leaf,
                    O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                /*      A file or a link standing where the archive has a
                        directory is taken away, as the reference does; the
                        link itself goes, never what it points at. */
                if ((exact == -ERROR_NOT_DIRECTORY || exact == -ELOOP) &&
                    system_remove_at(parent, leaf, 0) >= 0)
                {
                        made = system_make_directory_at(parent, leaf, 0700);
                        ours = made >= 0;
                        exact = made < 0 ? made : system_open_at(
                            parent, leaf,
                            O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                }
                if (exact < 0)
                        return exact;
        }

        made = file_look_code(exact, (string_address)"", AT_EMPTY_PATH,
                              address_of facts);
        if (made >= 0 && ((facts.mask & STATX_BASIC) != STATX_BASIC ||
                          (facts.mode & MODE_FORMAT) != MODE_DIRECTORY))
                made = -ERROR_INPUT_OUTPUT;
        if (made >= 0 && !ours &&
            ((facts.owner != tar_user && facts.owner != 0) ||
             !file_name_stable(parent, address_of facts)))
                made = -ERROR_ACCESS;
        if (made >= 0 &&
            !tar_directory_remember(path, final_mode, address_of facts, meta))
                made = -ERROR_NO_MEMORY;
        if (made >= 0 && !ours)
        {
                /* Explicit directory entries remain private but traversable
                   until all descendants have been created. The final
                   archived mode is restored through the same inode later. */
                bool changed;
                positive old_mode;
                bipolar opened = file_directory_real(
                    exact, parent, leaf, address_of changed,
                    address_of old_mode);

                made = opened;
                if (made >= 0 && (facts.mode & 07777) != 0700)
                        made = system_call_2(syscall(fchmod),
                                             (positive)opened, 0700);
                if (opened >= 0)
                        system_close(opened);
        }

        if (!held &&
            (made < 0 || parent_owned ||
             (!ours && !tar_extract_directory_trusted(exact)) ||
             !tar_stack_push(exact, leaf, length, ours,
                             address_of facts)))
                system_close(exact);
        return made;
}

static fn tar_extract_member(bipolar archive, p8 type, string_address path,
                             string_address link, p64 size, p64 mode,
                             p64 major, p64 minor, bool seekable,
                             tar_member_meta address_to meta)
{
        positive final_mode = (positive)mode & (tar_preserve ? 07777 : 0777);
        bipolar made = 0;
        bipolar parent;
        bool parent_owned;
        p8 leaf[TAR_PATH];
        string_address slash = string_last_of(path, '/');
        bool directory = type == '5' || (slash && !slash[1]);
        bool regular = !directory &&
                       (!type || type == '0' || type == '7' || type == 'S');

        if (!tar_preserve && (directory || regular ||
                              type == '3' || type == '4' || type == '6'))
                final_mode &= ~tar_session_mask;

        if (!directory && !regular && type != '1' && type != '2' &&
            type != '3' && type != '4' && type != '6')
        {
                p8 shown[2] = {type, end};

                string_format(log_error, "tar: %w: unknown file type '%s'\n", writer_terminal_name,
                              path, shown);
                tar_status = tar_status ? tar_status : 1;
                tar_skip(archive, tar_padded(size), seekable);
                return;
        }

        if (!tar_extract_type_allowed(type))
        {
                tar_refuse("special files are not allowed");
                tar_skip(archive, tar_padded(size), seekable);
                return;
        }

        parent = tar_stack_parent(path, leaf, sizeof(leaf),
                                  address_of parent_owned);
        if (parent < 0)
        {
                tar_fail(path, parent);
                tar_skip(archive, tar_padded(size), seekable);
                return;
        }

        if (regular)
        {
                tar_extract_regular(archive, parent, leaf, path, size,
                                     final_mode, seekable, meta);
                if (parent_owned)
                        system_close(parent);
                return;
        }

        if (directory)
                made = tar_extract_directory(parent, parent_owned, leaf, path,
                                             final_mode, meta);
        else
        {
                system_path_stage protected;
                file_facts materialized;
                file_facts source_facts;
                file_facts replaced;
                bool replaced_known = false;
                bool materialized_known = false;
                bool destination_satisfied = false;
                bool finished = false;
                bipolar source_handle = -1;
                file_stage_expectation expected = {
                    .kind = type == '2' ? MODE_LINK : type == '6' ? MODE_PIPE :
                            type == '3' ? MODE_CHARACTER : MODE_BLOCK,
                    .link = type == '2' ? link : null,
                    .device_major = type == '6' ? 0 : (p32)major,
                    .device_minor = type == '6' ? 0 : (p32)minor,
                };

                /* Links are made under their own names first, as regular
                   members are: linkat and symlinkat never follow or replace
                   what is there, so only an existing name falls through to
                   the destination checks and the staged transaction. */
                if (type == '1')
                {
                        tar_materialized_file address_to authorized =
                            tar_materialized_find(link);

                        made = authorized ? 0 : -ERROR_ACCESS;
                        if (made >= 0)
                        {
                                source_handle = authorized->pending
                                    ? tar_open_pending(link, authorized)
                                    : tar_open_beneath(link, O_PATH);
                                made = source_handle < 0 ? source_handle :
                                    file_look_code(source_handle,
                                        (string_address)"", AT_EMPTY_PATH,
                                        address_of source_facts);
                        }
                        if (made >= 0 &&
                            (source_facts.mask & STATX_BASIC) != STATX_BASIC)
                                made = -ERROR_INPUT_OUTPUT;
                        if (made >= 0 &&
                            (authorized->pending
                                 ? (source_facts.mode & MODE_FORMAT) != MODE_FILE
                                 : !tar_materialized_same(
                                       address_of authorized->facts,
                                       address_of source_facts)))
                                made = -ERROR_ACCESS;
                        /*      A directory where the target was: link()
                                meets the name already there first, and the
                                reference takes that away and tries again
                                before the kernel refuses the directory. */
                        if (made == -ERROR_ACCESS && source_handle >= 0 &&
                            (source_facts.mask & STATX_BASIC) == STATX_BASIC &&
                            (source_facts.mode & MODE_FORMAT) == MODE_DIRECTORY)
                        {
                                if (tar_extract_destination(
                                        parent, leaf, path, address_of replaced,
                                        address_of replaced_known) >= 0 &&
                                    replaced_known)
                                        (void)system_remove_at(parent, leaf, 0);
                                made = -ERROR_NOT_PERMITTED;
                        }
                        if (made >= 0)
                        {
                                expected.kind = source_facts.mode & MODE_FORMAT;
                                expected.identity = address_of source_facts;
                                bipolar linked = system_path_link_opened_at(
                                    source_handle, parent, leaf);
                                if (linked >= 0)
                                {
                                        materialized = source_facts;
                                        materialized_known = true;
                                        finished = true;
                                }
                                else if (linked != -ERROR_EXISTS)
                                        made = linked;
                        }
                }
                else if (type == '2')
                {
                        bipolar linked = system_symbolic_link_at(link, parent,
                                                                 leaf);
                        if (linked >= 0)
                        {
                                made = tar_member_settle_at(parent, leaf,
                                                            final_mode, meta,
                                                            true);
                                memory_fill(address_of materialized, 0,
                                            sizeof(materialized));
                                materialized.mode = MODE_LINK;
                                materialized_known = true;
                                finished = true;
                        }
                        else if (linked != -ERROR_EXISTS)
                                made = linked;
                }

                if (made >= 0 && !finished)
                {
                        made = tar_extract_destination(
                            parent, leaf, path, address_of replaced,
                            address_of replaced_known);
                        if (made >= 0 && type == '1')
                        {
                                destination_satisfied =
                                    replaced_known &&
                                    file_same_identity(
                                        address_of source_facts,
                                        address_of replaced);
                                if (destination_satisfied)
                                        made = system_path_same_opened_at(
                                            source_handle, parent, leaf);
                                if (made >= 0 && destination_satisfied)
                                {
                                        materialized = source_facts;
                                        materialized_known = true;
                                }
                        }
                }
                if (made >= 0 && !finished && !destination_satisfied)
                {
                        bipolar handle = file_stage_claim_at(
                            address_of protected, parent, leaf, 0600,
                            source_handle, address_of expected);
                        made = handle;
                        if (handle >= 0)
                        {
                                bipolar published_handle = -1;
                                if (type != '1')
                                        made = tar_member_settle_at(
                                            protected.directory,
                                            SYSTEM_PATH_STAGE_LEAF,
                                            final_mode, meta, type == '2');
                                made = file_stage_publish_protected_keep_at(
                                    address_of protected, parent, leaf,
                                    handle, made, false,
                                    replaced_known ? address_of replaced
                                                   : null,
                                    0, address_of published_handle, false);
                                if (made >= 0)
                                        made = file_look_code(
                                            published_handle,
                                            (string_address)"",
                                            AT_EMPTY_PATH,
                                            address_of materialized);
                                if (made >= 0 &&
                                    ((materialized.mask & STATX_BASIC) !=
                                         STATX_BASIC ||
                                     (materialized.mode & MODE_FORMAT) !=
                                         expected.kind))
                                        made = -ERROR_INPUT_OUTPUT;
                                if (published_handle >= 0)
                                        /* Publication is already committed;
                                           closing its O_PATH identity pin is
                                           not part of member success. */
                                        (void)system_close(published_handle);
                                if (made >= 0)
                                        materialized_known = true;
                        }
                }
                if (made >= 0 && materialized_known)
                        made = tar_materialized_remember(
                            path, address_of materialized, null);
                if (source_handle >= 0)
                        system_close(source_handle);
        }

        if (parent_owned)
                system_close(parent);
        if (made < 0)
                tar_fail(path, made);
        tar_skip(archive, tar_padded(size), seekable);
}

/*
        tar -d, --diff, --compare: each member against the file of its name,
        as GNU's compare.c does it.  A difference is a line on standard output,
        the name and what differs, and the status becomes 1; a name that is
        not there is a warning and the same 1; any other failure to look is
        an error and 2.  The name is GNU's for this: leading slashes and
        everything up to a last ".." component taken off (and said once per
        prefix), a trailing slash taken off, then --strip-components.  What
        is compared follows the member's type: a file's kind, mode, owner,
        group, time, size and then bytes; a directory's kind and mode; where
        a symlink points; whether a hard link is the same inode as its
        target; a device's kind, number and mode.
*/
#define TAR_DIFF 'd'

typedef struct
{
        p8 prefix[TAR_PATH];
        bool said;
} tar_prefix_said;

static tar_prefix_said tar_diff_prefixes[8];
static positive tar_diff_prefix_count;

static fn tar_diff_prefix_once(string_address prefix, positive length,
                               bool link)
{
        for (positive at = 0; at < tar_diff_prefix_count; at++)
                if (tar_diff_prefixes[at].said == link &&
                    !memory_compare(tar_diff_prefixes[at].prefix, prefix, length) &&
                    !tar_diff_prefixes[at].prefix[length])
                        return;
        if (tar_diff_prefix_count < array_count(tar_diff_prefixes) &&
            length < TAR_PATH)
        {
                tar_prefix_said address_to kept =
                    tar_diff_prefixes + tar_diff_prefix_count++;

                memory_copy(kept->prefix, prefix, length);
                kept->prefix[length] = end;
                kept->said = link;
        }
        p8 shown[TAR_PATH];
        positive take = length < TAR_PATH ? length : TAR_PATH - 1;

        memory_copy(shown, prefix, take);
        shown[take] = end;
        string_format(log_error, link ? "tar: Removing leading `%s' from hard link targets\n"
                                      : "tar: Removing leading `%s' from member names\n",
                      (string_address)shown);
}

/* GNU's safer_name_suffix, then its trailing slash and component strip. */
static bool tar_diff_name(string_address name, positive strip, bool absolute,
                          bool link, p8 address_to into, bool address_to trailing)
{
        string_address at = name;
        positive length;

        if (!absolute)
        {
                positive prefix = 0;
                string_address walk = name;

                while (*walk)
                {
                        if (walk[0] == '.' && walk[1] == '.' &&
                            (walk[2] == '/' || !walk[2]))
                                prefix = (positive)(walk - name) + 2;
                        do
                        {
                                p8 byte = *walk++;
                                if (byte == '/')
                                        break;
                        } while (*walk);
                }
                while (name[prefix] == '/')
                        prefix++;
                if (prefix)
                        tar_diff_prefix_once(name, prefix, link);
                at = name + prefix;
        }
        if (!*at)
                at = (string_address)".";

        length = string_length(at);
        if (length >= TAR_PATH)
                return false;
        memory_copy(into, at, length + 1);
        if (trailing)
                address_to trailing = false;
        while (length > 1 && into[length - 1] == '/')
        {
                into[--length] = end;
                if (trailing)
                        address_to trailing = true;
        }

        /* --strip-components: whole components off the front, a run of
           slashes counting as one separator. */
        at = into;
        while (strip && *at)
        {
                while (*at && *at != '/')
                        at++;
                while (*at == '/')
                        at++;
                strip--;
        }
        if (strip)
                at = into + length;
        if (at != into)
                memory_copy(into, at, string_length(at) + 1);
        return true;
}

/* "name: " on standard output, the difference's line begun. */
static fn tar_diff_begin(string_address name)
{
        tar_quoted(log, name);
        log(": ", 2);
        if (tar_status < 1)
                tar_status = 1;
}

static fn tar_diff_report(string_address name, string_address what)
{
        tar_diff_begin(name);
        string_format(log, "%s\n", what);
}

/* A file that is not there is GNU's warning and a difference; anything
   else that stops the look is its error. */
static bool tar_diff_stat(string_address name, file_facts address_to facts)
{
        bipolar looked = system_stat_at(AT_FDCWD, name, AT_SYMLINK_NOFOLLOW,
                                        STATX_BASIC, facts);

        if (looked >= 0)
                return true;
        if (looked == -ERROR_NO_ENTRY)
        {
                string_format(log_error, "tar: %w: Warning: Cannot stat: %s\n",
                              writer_terminal_name, name, file_reason(looked));
                if (tar_status < 1)
                        tar_status = 1;
        }
        else
        {
                string_format(log_error, "tar: %w: Cannot stat: %s\n",
                              writer_terminal_name, name, file_reason(looked));
                tar_status = 2;
        }
        return false;
}

static bool tar_diff_mode_same(p64 mode, file_facts address_to facts)
{
        return (mode & 07777) == (facts->mode & 07777);
}

/* The archive's bytes against the file's, read beside each other; the
   first that differ are said and the rest of the member passed over. */
static bool tar_diff_bytes(bipolar archive, bipolar file, p64 offset,
                           p64 size, string_address name, bool address_to said)
{
        p8 mine[TAR_RECORD];

        while (size)
        {
                if (tar_at >= tar_have && tar_fill(archive) <= 0)
                {
                        tar_refuse("unexpected EOF in archive");
                        return false;
                }
                positive take = tar_have - tar_at;
                if (take > size)
                        take = (positive)size;
                if (!address_to said)
                {
                        positive got = 0;

                        while (got < take)
                        {
                                bipolar more = system_call_4(
                                    syscall(pread64), (positive)file,
                                    (positive)(mine + got), take - got,
                                    (positive)(offset + got));
                                if (more == -EINTR)
                                        continue;
                                if (more <= 0)
                                {
                                        if (more < 0)
                                        {
                                                string_format(log_error,
                                                    "tar: %w: Cannot read: %s\n",
                                                    writer_terminal_name, name,
                                                    file_reason(more));
                                                tar_status = 2;
                                        }
                                        else
                                        {
                                                tar_diff_begin(name);
                                                string_format(log,
                                                    take == 1 ? "Could only read %p of %p byte\n"
                                                              : "Could only read %p of %p bytes\n",
                                                    got, take);
                                        }
                                        if (tar_status < 1)
                                                tar_status = 1;
                                        address_to said = true;
                                        break;
                                }
                                got += (positive)more;
                        }
                        if (!address_to said &&
                            memory_compare(mine, tar_record + tar_at, take))
                        {
                                tar_diff_report(name, "Contents differ");
                                address_to said = true;
                        }
                }
                tar_at += take;
                offset += take;
                size -= take;
        }
        return true;
}

/* A sparse member: its spans against the file at their offsets, and what
   lies between them must read as zeros. */
static bool tar_diff_holes(bipolar file, p64 from, p64 to,
                           string_address name, bool address_to said)
{
        p8 zeros[TAR_BLOCK * 8];

        while (!address_to said && from < to)
        {
                positive ask = to - from > sizeof(zeros) ? sizeof(zeros)
                                                        : (positive)(to - from);
                bipolar got = system_call_4(syscall(pread64), (positive)file,
                                            (positive)zeros, ask,
                                            (positive)from);
                if (got == -EINTR)
                        continue;
                if (got <= 0 || !tar_bytes_zero(zeros, (positive)got))
                {
                        tar_diff_report(name, "Contents differ");
                        address_to said = true;
                        break;
                }
                from += (p64)got;
        }
        return true;
}

static fn tar_diff_file(bipolar archive, p8 address_to block, p8 type,
                        string_address name, p64 size, p64 mode, p64 user,
                        p64 group, b64 seconds, p32 nanoseconds,
                        bool precise, bool numeric, bool seekable)
{
        file_facts facts;
        p64 real = tar_sparse_active ? tar_sparse_real : size;

        if (!tar_diff_stat(name, address_of facts))
        {
                tar_skip(archive, tar_padded(size), seekable);
                return;
        }
        if ((facts.mode & MODE_FORMAT) != MODE_FILE)
        {
                tar_diff_report(name, "File type differs");
                tar_skip(archive, tar_padded(size), seekable);
                return;
        }
        if (!tar_diff_mode_same(mode, address_of facts))
                tar_diff_report(name, "Mode differs");

        p8 word[33];
        p64 want_user = user;
        p64 want_group = group;
        if (!numeric)
        {
                tar_field_text(block + 265, 32, word, sizeof(word));
                bipolar named = word[0] ? file_user_id(word) : -1;
                if (named >= 0)
                        want_user = (p64)named;
                tar_field_text(block + 297, 32, word, sizeof(word));
                named = word[0] ? file_group_id(word) : -1;
                if (named >= 0)
                        want_group = (p64)named;
        }
        if (facts.owner != want_user)
                tar_diff_report(name, "Uid differs");
        if (facts.group != want_group)
                tar_diff_report(name, "Gid differs");
        if (facts.modified.seconds != seconds ||
            (precise && facts.modified.nanoseconds != nanoseconds))
                tar_diff_report(name, "Mod time differs");
        if (type != 'S' && facts.size != real)
        {
                tar_diff_report(name, "Size differs");
                tar_skip(archive, tar_padded(size), seekable);
                return;
        }

        bipolar file = system_open_at(AT_FDCWD, name,
                                      FILE_READ | O_CLOEXEC | O_NOCTTY |
                                          O_NONBLOCK);
        if (file < 0)
        {
                string_format(log_error, "tar: %w: Cannot open: %s\n",
                              writer_terminal_name, name, file_reason(file));
                tar_status = 2;
                tar_skip(archive, tar_padded(size), seekable);
                return;
        }

        bool said = false;
        if (tar_sparse_active)
        {
                p64 cursor = 0;

                if (facts.size != real)
                {
                        tar_diff_report(name, "Size differs");
                        said = true;
                }
                for (positive at = 0; at < tar_sparse_used; at++)
                {
                        tar_diff_holes(file, cursor, tar_sparse[at].offset,
                                       name, address_of said);
                        if (!tar_diff_bytes(archive, file, tar_sparse[at].offset,
                                            tar_sparse[at].bytes, name,
                                            address_of said))
                        {
                                system_close(file);
                                return;
                        }
                        cursor = tar_sparse[at].offset + tar_sparse[at].bytes;
                }
                tar_diff_holes(file, cursor, real, name, address_of said);
        }
        else if (!tar_diff_bytes(archive, file, 0, size, name, address_of said))
        {
                system_close(file);
                return;
        }
        system_close(file);
        tar_skip(archive, tar_padded(size) - size, seekable);
}

static fn tar_diff_member(bipolar archive, p8 address_to block, p8 type,
                          string_address raw, string_address raw_link,
                          p64 size, p64 mode, p64 user, p64 group,
                          p64 major, p64 minor, b64 seconds,
                          p32 nanoseconds, bool precise,
                          struct tar_options address_to options, bool seekable)
{
        p8 name[TAR_PATH];
        p8 target[TAR_PATH];
        bool trailing = false;
        file_facts facts;

        if (!tar_diff_name(raw, options->strip, options->absolute, false, name,
                           address_of trailing))
        {
                tar_refuse("member name is too long");
                tar_skip(archive, tar_padded(size), seekable);
                return;
        }

        switch (type)
        {
        case '1':
        {
                file_facts linked;

                if (!tar_diff_name(raw_link, options->strip, options->absolute,
                                   true, target, null))
                        break;
                if (tar_diff_stat(name, address_of facts) &&
                    tar_diff_stat(target, address_of linked) &&
                    !file_same_identity(address_of facts, address_of linked))
                {
                        tar_diff_begin(name);
                        log("Not linked to ", 14);
                        tar_quoted(log, target);
                        log("\n", 1);
                }
                break;
        }
        case '2':
        {
                positive length = string_length(raw_link);
                p8 found[TAR_PATH + 1];
                bipolar got = system_read_link_at(AT_FDCWD, name, found,
                                                  length + 1 < sizeof(found)
                                                      ? length + 1
                                                      : sizeof(found));

                if (got < 0)
                {
                        if (got == -ERROR_NO_ENTRY)
                        {
                                string_format(log_error,
                                              "tar: %w: Warning: Cannot readlink: %s\n",
                                              writer_terminal_name, name,
                                              file_reason(got));
                                if (tar_status < 1)
                                        tar_status = 1;
                        }
                        else
                        {
                                string_format(log_error,
                                              "tar: %w: Cannot readlink: %s\n",
                                              writer_terminal_name, name,
                                              file_reason(got));
                                tar_status = 2;
                        }
                }
                else if ((positive)got != length ||
                         memory_compare(found, raw_link, length))
                        tar_diff_report(name, "Symlink differs");
                break;
        }
        case '3':
        case '4':
        case '6':
                if (!tar_diff_stat(name, address_of facts))
                        break;
                if ((facts.mode & MODE_FORMAT) !=
                    (type == '3' ? MODE_CHARACTER : type == '4' ? MODE_BLOCK
                                                                 : MODE_PIPE))
                {
                        tar_diff_report(name, "File type differs");
                        break;
                }
                if (type != '6' && (facts.rdev_major != major ||
                                    facts.rdev_minor != minor))
                {
                        tar_diff_report(name, "Device number differs");
                        break;
                }
                if (!tar_diff_mode_same(mode, address_of facts))
                        tar_diff_report(name, "Mode differs");
                break;
        case '5':
                if (!tar_diff_stat(name, address_of facts))
                        break;
                if ((facts.mode & MODE_FORMAT) != MODE_DIRECTORY)
                        tar_diff_report(name, "File type differs");
                else if (!tar_diff_mode_same(mode, address_of facts))
                        tar_diff_report(name, "Mode differs");
                break;
        default:
                if (type && type != '0' && type != '7' && type != 'S')
                {
                        p8 shown[2] = {type, end};

                        string_format(log_error,
                                      "tar: %w: Unknown file type '%s', diffed as normal file\n",
                                      writer_terminal_name, name, shown);
                        tar_status = 2;
                }
                if (trailing)
                {
                        if (!tar_diff_stat(name, address_of facts))
                                break;
                        if ((facts.mode & MODE_FORMAT) != MODE_DIRECTORY)
                                tar_diff_report(name, "File type differs");
                        else if (!tar_diff_mode_same(mode, address_of facts))
                                tar_diff_report(name, "Mode differs");
                        break;
                }
                tar_diff_file(archive, block, type, name, size, mode, user,
                              group, seconds, nanoseconds, precise,
                              options->numeric, seekable);
                return;
        }
        tar_skip(archive, tar_padded(size), seekable);
}

/* The members of an archive whose first block is ready, listed or
   extracted as options say, from whichever thread reads the archive. */
typedef struct
{
        struct tar_options address_to options;
        bipolar handle;
        bool seekable;
        bool listed;
        positive count;
} tar_members;

static fn tar_read_members(tar_members address_to run)
{
        struct tar_options address_to const options = run->options;
        bipolar const handle = run->handle;
        bool const seekable = run->seekable;
        positive const count = run->count;
        p8 address_to block;
        bool listed = false;
        p8 long_name[TAR_PATH];
        p8 long_link[TAR_PATH];
        bool have_long_name = false;
        bool have_long_link = false;

        long_name[0] = end;
        long_link[0] = end;

        while ((block = tar_next_block(handle, true)))
        {
                p8 type;
                p64 size = 0;
                p64 mode = 0;
                p64 major = 0;
                p64 minor = 0;
                p64 user = 0;
                p64 group = 0;
                b64 stamp = 0;
                bool stamped;
                tar_member_meta meta;
                p8 kept[TAR_PATH];
                p8 kept_link[TAR_PATH];
                p8 shown[TAR_PATH];
                bool escaped;

                p32 total = memory_sum_bytes(block, TAR_BLOCK);

                if (total == 0)
                {
                        p8 address_to second = tar_next_block(handle, false);

                        if (!second)
                        {
                                if (tar_status != 2)
                                        tar_refuse("archive end marker is incomplete");
                        }
                        else if (!tar_header_zero(second))
                                tar_refuse("archive end marker is followed by data");
                        break;
                }

                if (!tar_header_ok_from(block, total))
                {
                        tar_refuse("invalid header checksum");
                        break;
                }

                type = block[156];
                if (!tar_field_value(block + 124, 12, address_of size) ||
                    !tar_field_value(block + 100, 8, address_of mode) ||
                    !tar_field_value(block + 329, 8, address_of major) ||
                    !tar_field_value(block + 337, 8, address_of minor))
                {
                        tar_refuse("invalid header");
                        break;
                }

                if (!tar_size_fits(size))
                {
                        tar_refuse("member size is too large");
                        break;
                }

                /* Owner and time are not worth refusing an archive over: a
                   field that does not parse restores nothing. */
                stamped = tar_field_moment(block + 136, 12, address_of stamp);
                if (!tar_field_value(block + 108, 8, address_of user))
                        user = 0;
                if (!tar_field_value(block + 116, 8, address_of group))
                        group = 0;

                tar_sparse_clear();
                if (type == 'S' && !tar_sparse_load(handle, block))
                {
                        tar_refuse("invalid sparse archive");
                        break;
                }
                if (!tar_sparse_active && tar_psp_kind &&
                    (type == '0' || !type) &&
                    !tar_pax_sparse_take(handle, address_of size))
                {
                        tar_refuse("invalid sparse archive");
                        break;
                }
                if (tar_sparse_active && tar_sparse_payload() != size)
                {
                        tar_refuse("invalid sparse archive");
                        break;
                }

                /*      A record whose name we could not hold stops the
                        archive, the way a pax header we could not read
                        already does.  Carrying on would extract the member
                        the record was written for under whatever its own
                        header field happened to say, which is a name the
                        archive never asked for; tar_read_payload has said
                        why by the time it answers false. */
                if (type == 'L')
                {
                        if (!tar_read_payload(handle, size, long_name,
                                              TAR_PATH, seekable))
                                break;
                        have_long_name = true;
                        continue;
                }

                if (type == 'K')
                {
                        if (!tar_read_payload(handle, size, long_link,
                                              TAR_PATH, seekable))
                                break;
                        have_long_link = true;
                        continue;
                }

                if (type == 'x' || type == 'g')
                {
                        if (size >= TAR_PAX_LIMIT ||
                            !shell_array_room(tar_pax_body, tar_pax_body_room,
                                              (positive)size + 1) ||
                            !tar_read_payload(handle, size, tar_pax_body,
                                              (positive)size + 1, seekable) ||
                            !tar_pax_apply(
                                type == 'g'
                                    ? address_of tar_pax_global
                                    : address_of tar_pax_local,
                                tar_pax_body, (positive)size))
                        {
                                tar_refuse("invalid extended header");
                                break;
                        }

                        continue;
                }

                if (tar_pax_local.has_size)
                        size = tar_pax_local.size;
                else if (tar_pax_global.has_size)
                        size = tar_pax_global.size;
                if (!tar_size_fits(size))
                {
                        tar_refuse("member size is too large");
                        break;
                }

                if (!tar_member_name(block, tar_name,
                                     have_long_name ? long_name : null))
                {
                        tar_refuse("member name is too long");
                        break;
                }

                tar_member_link(block, tar_link,
                                have_long_link ? long_link : null);

                have_long_name = false;
                have_long_link = false;
                long_name[0] = end;
                long_link[0] = end;

                if (!tar_wanted(tar_name, options->first, count))
                {
                        tar_skip(handle, tar_padded(size), seekable);
                        tar_pax_clear(address_of tar_pax_local);
                        continue;
                }

                /*      A listing names each member as the archive spells it,
                        as the reference does, an unsafe one included; only
                        extraction is held to a safe name. A hard link's
                        target is shown the way extraction would read it,
                        without its leading slashes and parent steps. */
                if (options->mode == TAR_LIST)
                {
                        string_address target = tar_link;

                        if (type == '1')
                                for (;;)
                                {
                                        if (string_is(target, '/'))
                                                target++;
                                        else if (!string_compare_max(target, (string_address) "../", 3))
                                                target += 3;
                                        else
                                                break;
                                }

                        listed = true;
                        if (options->verbose)
                                tar_long_line(block, type, mode,
                                              tar_sparse_active ? tar_sparse_real : size,
                                              tar_pax_local.has_user ? tar_pax_local.user : user,
                                              tar_pax_local.has_group ? tar_pax_local.group : group,
                                              major, minor,
                                              tar_pax_local.has_time ? tar_pax_local.seconds
                                                                     : stamp,
                                              tar_name, target, options->numeric);
                        else
                                tar_name_line(tar_listing, tar_name);
                        tar_skip(handle, tar_padded(size), seekable);
                        tar_pax_clear(address_of tar_pax_local);
                        continue;
                }

                /*      A comparison names each member as a listing does
                        when asked to (-v, and -vv the long line), then
                        looks at the file GNU's name for it names. */
                if (options->mode == TAR_DIFF)
                {
                        tar_pax_state address_to said =
                            tar_pax_local.has_time ? address_of tar_pax_local
                                                   : address_of tar_pax_global;

                        string_address target = tar_link;

                        if (type == '1')
                                for (;;)
                                {
                                        if (string_is(target, '/'))
                                                target++;
                                        else if (!string_compare_max(target, (string_address) "../", 3))
                                                target += 3;
                                        else
                                                break;
                                }
                        listed = true;
                        if (options->verbose > 1)
                                tar_long_line(block, type, mode,
                                              tar_sparse_active ? tar_sparse_real : size,
                                              tar_pax_local.has_user ? tar_pax_local.user : user,
                                              tar_pax_local.has_group ? tar_pax_local.group : group,
                                              major, minor,
                                              said->has_time ? said->seconds : stamp,
                                              tar_name, target, options->numeric);
                        else if (options->verbose)
                                tar_name_line(tar_listing, tar_name);
                        tar_diff_member(handle, block, type, tar_name, tar_link,
                                        size, mode,
                                        tar_pax_local.has_user ? tar_pax_local.user
                                        : tar_pax_global.has_user ? tar_pax_global.user
                                                                  : user,
                                        tar_pax_local.has_group ? tar_pax_local.group
                                        : tar_pax_global.has_group ? tar_pax_global.group
                                                                   : group,
                                        major, minor,
                                        said->has_time ? said->seconds : stamp,
                                        said->has_time ? said->nanoseconds : 0,
                                        tar_pax_local.has_time,
                                        options, seekable);
                        tar_pax_clear(address_of tar_pax_local);
                        continue;
                }

                if (!tar_safe_path(tar_name, options->strip, options->absolute,
                                   kept, TAR_PATH, address_of escaped))
                {
                        /*      A name that reduces to nothing is the
                                archive's own root -- "./" is in every
                                archive written from a directory -- or a
                                member wholly removed by --strip-components.
                                The reference passes over both without a
                                word; only a name that tried to climb out
                                is worth saying anything about. */
                        if (escaped)
                        {
                                string_format(log_error, "tar: %w: member name is unsafe\n",
                                              writer_terminal_name, tar_name);
                                tar_status = 2;
                        }

                        tar_skip(handle, tar_padded(size), seekable);
                        tar_pax_clear(address_of tar_pax_local);
                        continue;
                }

                if (type == '1')
                {
                        if (!tar_safe_path(tar_link, options->strip,
                                           options->absolute, kept_link,
                                           TAR_PATH, null))
                        {
                                tar_refuse("hard link target is unsafe");
                                tar_skip(handle, tar_padded(size), seekable);
                                tar_pax_clear(address_of tar_pax_local);
                                continue;
                        }
                }
                else
                        string_copy_max_end(kept_link, tar_link, TAR_PATH - 1);

                /*      The reference names a member exactly as the archive
                        spells it, so a directory written with its trailing
                        slash keeps it and one written without stays without.
                        The slash is not on kept, which is the path every
                        openat and every remembered directory is keyed by. */
                {
                        positive length = string_length(tar_name);
                        bool trailing = length &&
                                        tar_name[length - 1] == '/';

                        if (!trailing ||
                            !tar_spell_directory(kept, shown, sizeof(shown)))
                                string_copy_max_end(shown, kept, TAR_PATH - 1);
                }

                listed = true;
                {
                        tar_pax_state address_to said =
                            tar_pax_local.has_user ? address_of tar_pax_local
                                                   : address_of tar_pax_global;

                        meta.user = said->has_user ? said->user : (p32)user;
                        said = tar_pax_local.has_group
                                   ? address_of tar_pax_local
                                   : address_of tar_pax_global;
                        meta.group = said->has_group ? said->group
                                                     : (p32)group;
                        said = tar_pax_local.has_time
                                   ? address_of tar_pax_local
                                   : address_of tar_pax_global;
                        meta.seconds = said->has_time ? said->seconds
                                                      : stamp;
                        meta.nanoseconds = said->has_time ? said->nanoseconds
                                                          : 0;
                        meta.timed = said->has_time || stamped;
                        tar_attrs_keep(address_of meta);
                        if (options->verbose > 1)
                                tar_long_line(block, type, mode,
                                              tar_sparse_active ? tar_sparse_real : size,
                                              meta.user, meta.group, major, minor,
                                              meta.seconds, shown, kept_link,
                                              options->numeric);
                        else if (options->verbose)
                                tar_name_line(tar_listing, shown);
                        tar_extract_member(handle, type, kept, kept_link, size,
                                           mode, major, minor, seekable,
                                           address_of meta);
                }

                tar_pax_clear(address_of tar_pax_local);
        }

        run->listed = listed;
}

static b32 tar_read_archive(struct tar_options address_to options)
{
        bipolar handle;
        bool seekable;
        bool listed;
        positive count = tar_word_count;

        tar_pax_clear(address_of tar_pax_global);
        tar_pax_clear(address_of tar_pax_local);
        tar_sparse_clear();
        tar_reset();
        tar_matched = null;
        tar_matched_count = 0;
        if (options->first < count)
        {
                tar_matched = memory_take_zeroed(count - options->first, 1);
                tar_matched_count = tar_matched ? count - options->first : 0;
        }
        tar_listing = log;
        tar_pack = options->pack;

        if (!options->archive || string_equals(options->archive, "-"))
                handle = 0;
        else
        {
                handle = system_open_at(AT_FDCWD, options->archive,
                                        FILE_READ | O_CLOEXEC);
                if (handle < 0)
                {
                        tar_fail_at(options->archive, "Cannot open", handle);
                        tar_fatal_exit = true;
                        return tar_status;
                }
        }

        seekable = system_seek(handle, 0, FILE_SEEK_CUR) >= 0;
        tar_archive_sized = false;
        tar_archive_size = 0;
        if (seekable)
        {
                file_facts opened;

                tar_archive_sized =
                    file_look_code(handle, (string_address)"", AT_EMPTY_PATH,
                                   address_of opened) >= 0 &&
                    (opened.mode & MODE_FORMAT) == MODE_FILE;
                if (tar_archive_sized)
                        tar_archive_size = opened.size;
        }
        tar_advise(handle);
        {
                p8 magic[6];
                bipolar got = 0;

                /* The sniff needs all six bytes a slow writer may split. */
                while ((positive)got < sizeof(magic))
                {
                        bipolar more = system_read_retry(
                            (positive)handle, magic + got,
                            sizeof(magic) - (positive)got);

                        if (more <= 0)
                        {
                                if (more < 0)
                                        got = more;
                                break;
                        }
                        got += more;
                }

                if (got < 0)
                {
                        tar_refuse("cannot read archive");
                        if (handle > 0)
                                system_close(handle);
                        return tar_status;
                }
                if (tar_pack == TAR_PACK_NONE || tar_pack == TAR_PACK_AUTO)
                {
                        p8 sniffed = tar_pack_from_magic(magic, (positive)got);

                        tar_pack = sniffed;
                }
                if (tar_refuse_pack(tar_pack))
                {
                        if (handle > 0)
                                system_close(handle);
                        return tar_status;
                }
                if (tar_packed())
                {
                        seekable = false;
                        if (!tar_codec_begin_read(handle, magic, (positive)got))
                        {
                                if (handle > 0)
                                        system_close(handle);
                                return tar_status;
                        }
                }
                else if (got > 0 && (positive)got <= sizeof(magic))
                {
                        memory_copy(tar_record, magic, (positive)got);
                        tar_have = (positive)got;
                        tar_at = 0;
                }
        }
        /* GNU: -p / root restores MODE_ALL; otherwise only 0777 & ~umask. */
        tar_user = (p32)system_call(syscall(geteuid));
        tar_preserve = options->permissions < 0
                           ? false
                           : (options->permissions > 0 || tar_user == 0);

        if (options->directory)
        {
                bipolar moved = tar_change_directory(options->directory);

                if (moved < 0)
                {
                        tar_fail(options->directory, moved);
                        tar_codec_end_read(handle);
                        if (handle > 0)
                                system_close(handle);

                        return tar_status;
                }
        }

        if (options->mode == TAR_EXTRACT)
        {
                bipolar mask = system_call_1(syscall(umask), 0);

                tar_session_mask = mask < 0 ? 0 : (positive)mask & 0777;
                tar_touch = options->touch;
                /* GNU restores owners for root unless told otherwise, and
                   always by number here: a bootstrap's ids are the ones its
                   own passwd names, not what the host calls those names. */
                tar_restore_owner = options->owner > 0 ||
                                    (!options->owner && tar_user == 0);
                tar_stack_used = 0;
                tar_stack_matched = 0;
                tar_stack_root = -1;
        }

        {
                tar_members run = {options, handle, seekable, false, count};

                tar_read_members(address_of run);
                listed = run.listed;
        }

        tar_codec_end_read(handle);
        if (handle > 0)
                system_close(handle);

        if (options->mode == TAR_EXTRACT)
        {
                tar_directories_finish();
                tar_stack_release();
                (void)system_call_1(syscall(umask), tar_session_mask);
        }

        for (positive at = options->first; at < count && tar_matched; at++)
                if (!tar_matched[at - options->first])
                {
                        string_format(log_error, "tar: %w: Not found in archive\n",
                                      writer_terminal_name, tar_words[at]);
                        tar_status = 2;
                }
        (void)listed;

        log_flush();
        return tar_status;
}

/*
        The header of each format, as GNU tar 1.35 writes it.  The formats
        differ in their magic, in what a long name or a large number becomes,
        and in what else rides with a member.  v7 has none of ustar's fields
        and refuses what it cannot hold.  ustar splits a name at a slash into
        prefix and name and refuses what will not split.  gnu and oldgnu put
        a long name or link target in a ././@LongLink member ahead of the
        header (oldgnu also keeps the file type in the mode) and write a
        number past octal in base 256.  posix (pax) puts each such thing in an
        extended header ahead of the member, with the member's fractional
        modification time and its access and change times.  What a format
        will not hold is said in GNU's words and the member is left out.
*/
#define TAR_V7 0
#define TAR_USTAR 1
#define TAR_GNU 2
#define TAR_OLDGNU 3
#define TAR_POSIX 4

static p8 tar_format = TAR_GNU;
static positive tar_blocking = 20;
static string_address tar_pax_deleted[8];
static positive tar_pax_deleted_count;

static p8 address_to tar_x;
static positive tar_x_room;
static positive tar_x_used;

static bool tar_x_wanted(string_address key)
{
        for (positive at = 0; at < tar_pax_deleted_count; at++)
                if (file_fnmatch(tar_pax_deleted[at], key))
                        return false;
        return true;
}

/* One extended header record, "<length> <key>=<value>\n", the length
   counting its own digits. */
static bool tar_x_add(string_address key, string_address value,
                      positive length)
{
        positive keys = string_length(key);
        positive body = 1 + keys + 1 + length + 1;
        positive digits = 1;
        positive total = body + 1;
        p8 text[24];

        if (!tar_x_wanted(key))
                return true;
        for (;;)
        {
                positive have = positive_into(text, total);

                if (have == digits)
                        break;
                digits = have;
                total = body + digits;
        }
        if (!shell_array_room(tar_x, tar_x_room, tar_x_used + total))
                return false;
        memory_copy(tar_x + tar_x_used, text, digits);
        tar_x_used += digits;
        tar_x[tar_x_used++] = ' ';
        memory_copy(tar_x + tar_x_used, key, keys);
        tar_x_used += keys;
        tar_x[tar_x_used++] = '=';
        memory_copy(tar_x + tar_x_used, value, length);
        tar_x_used += length;
        tar_x[tar_x_used++] = '\n';
        return true;
}

static bool tar_x_number(string_address key, b64 value)
{
        p8 text[24];
        positive length = bipolar_into(text, value);

        return tar_x_add(key, (string_address)text, length);
}

/* A time: seconds, then the fraction without its trailing zeros. */
static bool tar_x_time(string_address key, b64 seconds, p32 nanoseconds)
{
        p8 text[48];
        positive length = bipolar_into(text, seconds);

        if (nanoseconds)
        {
                p8 fraction[12];
                positive kept = 9;

                positive_into_padded(fraction, nanoseconds, 9, '0');
                while (kept && fraction[kept - 1] == '0')
                        kept--;
                text[length++] = '.';
                memory_copy(text + length, fraction, kept);
                length += kept;
        }
        return tar_x_add(key, (string_address)text, length);
}

/* A header the archive makes for itself, not for a file. */
static fn tar_private_header(p8 address_to block, string_address name,
                             p64 size, p8 type, b64 mtime)
{
        positive length = string_length(name);

        memory_fill(block, 0, TAR_BLOCK);
        memory_copy(block, name, length < TAR_NAME ? length : TAR_NAME);
        tar_field_put_octal(block + 100, 8,
                            tar_format == TAR_OLDGNU ? 0100644 : 0644);
        tar_field_put_octal(block + 108, 8, 0);
        tar_field_put_octal(block + 116, 8, 0);
        tar_field_put_octal(block + 124, 12, size);
        tar_field_put_octal(block + 136, 12,
                            mtime < 0 ? 0
                            : mtime >= ((b64)1 << 33) ? ((p64)1 << 33) - 1
                                                      : (p64)mtime);
        block[156] = type;
        if (tar_format == TAR_GNU || tar_format == TAR_OLDGNU)
        {
                memory_copy(block + 257, "ustar  ", 8);
                if (!tar_numeric_owner)
                {
                        memory_copy(block + 265, "root", 4);
                        memory_copy(block + 297, "root", 4);
                }
        }
        else
        {
                memory_copy(block + 257, "ustar", 6);
                block[263] = '0';
                block[264] = '0';
        }
        tar_header_put_checksum(block);
}

/* ././@LongLink and its text, GNU's way for a name or link target a header
   cannot hold: type L for a name, K for a target. */
static bool tar_put_long(bipolar handle, p8 type, string_address text)
{
        positive length = string_length(text) + 1;
        p8 block[TAR_BLOCK];

        tar_private_header(block, "././@LongLink", length, type, 0);
        return tar_write_block(handle, block) &&
               tar_put(handle, (p8 address_to)text, length) &&
               tar_write_padding(handle, length);
}

static bool tar_ascii(string_address text)
{
        for (; *text; text++)
                if ((p8)*text >= 128)
                        return false;
        return true;
}

/* A number in a field: octal when it fits, else what the format does.
   False when the format cannot hold it and the member is to be left out. */
static bool tar_number(p8 address_to field, positive width, b64 value,
                       string_address key, string_address kind)
{
        positive bits = 3 * (width - 1);
        p64 most = ((p64)1 << bits) - 1;

        if (value >= 0 && (p64)value <= most)
        {
                tar_field_put_octal(field, width, (p64)value);
                return true;
        }
        if (tar_format == TAR_GNU || tar_format == TAR_OLDGNU)
        {
                p64 rest = (p64)value;
                positive at;

                memory_fill(field, value < 0 ? 0xff : 0, width);
                for (at = width; at > 1 && width - at < 8; at--)
                {
                        field[at - 1] = (p8)rest;
                        rest >>= 8;
                }
                if (value >= 0)
                        field[0] |= 0x80;
                return true;
        }
        if (tar_format == TAR_POSIX && key)
        {
                tar_field_put_octal(field, width, 0);
                return tar_x_number(key, value);
        }
        {
                p8 shown[24];
                p8 limit[24];

                shown[bipolar_into(shown, value)] = end;
                limit[positive_into(limit, (positive)most)] = end;
                string_format(log_error, "tar: value %s out of %s range 0..%s\n",
                              (string_address)shown, kind, (string_address)limit);
                tar_status = 2;
                return false;
        }
}

/* GNU's ustar split: the last slash among the first 156 bytes, the rest
   being at most 100.  False when there is none such. */
static bool tar_ustar_split(string_address name, positive address_to cut)
{
        positive length = string_length(name);
        positive at;

        if (length > TAR_PREFIX + 1)
                length = TAR_PREFIX + 1;
        else if (length && name[length - 1] == '/')
                length--;
        for (at = length ? length - 1 : 0; at > 0 && name[at] != '/'; at--)
                ;
        if (!at || string_length(name) - at - 1 > TAR_NAME ||
            string_length(name) - at - 1 == 0)
                return false;
        address_to cut = at;
        return true;
}

/* An extended header's own name: the member's directory, PaxHeaders, the
   member's last component, kept to what a name field holds. */
static fn tar_x_name(string_address name, string_address middle,
                     p8 address_to into)
{
        positive length = string_length(name);
        positive stop = length;
        positive start;
        positive used = 0;
        p8 full[TAR_PATH];

        while (stop > 1 && name[stop - 1] == '/')
                stop--;
        for (start = stop; start > 0 && name[start - 1] != '/'; start--)
                ;
        if (start == 0)
        {
                memory_copy(full, ".", 1);
                used = 1;
        }
        else
        {
                positive dir = start - 1 ? start - 1 : 1;

                if (dir > sizeof(full) - 32)
                        dir = sizeof(full) - 32;
                memory_copy(full, name, dir);
                used = dir;
        }
        positive middle_length = string_length(middle);

        memory_copy(full + used, middle, middle_length);
        used += middle_length;
        positive base = stop - start;

        if (base > sizeof(full) - used - 1)
                base = sizeof(full) - used - 1;
        memory_copy(full + used, name + start, base);
        used += base;
        full[used] = end;
        memory_fill(into, 0, TAR_NAME + 1);
        memory_copy(into, full, used < TAR_NAME ? used : TAR_NAME);
}

/*
        Sparse members, as GNU's -S writes them.  A file with fewer blocks
        than its size needs is read for its data with SEEK_DATA and SEEK_HOLE,
        and only the data goes in.  gnu and oldgnu keep the map in the header,
        four spans and then 21 to an extension block, as type S; posix keeps
        it in extended header records or, by default, as text at the front of
        the data (version 1.0), the member named dir/GNUSparseFile.PID/base.
        The map always ends in an empty span at the file's size.
*/
typedef struct
{
        p64 offset;
        p64 length;
} tar_extent;

static tar_extent address_to tar_ext;
static positive tar_ext_room;
static positive tar_ext_count;
static p64 tar_ext_data;
static bool tar_sparse_on;
static bool tar_sparse_use;
static p8 tar_sparse_version = 2;
static p8 address_to tar_map;
static positive tar_map_room;
static positive tar_map_used;

static bool tar_ext_add(p64 offset, p64 length)
{
        if (!shell_array_room(tar_ext, tar_ext_room, tar_ext_count + 1))
                return false;
        tar_ext[tar_ext_count].offset = offset;
        tar_ext[tar_ext_count++].length = length;
        tar_ext_data += length;
        return true;
}

/* True when the file has holes to leave out; tar_ext then holds its map. */
static bool tar_sparse_scan(bipolar handle, file_facts address_to facts)
{
        p64 size = facts->size;
        p64 at = 0;
        bool whole;

        tar_ext_count = 0;
        tar_ext_data = 0;
        if (!size || facts->blocks >= (size + 511) / 512)
                return false;
        while (at < size)
        {
                bipolar data = system_seek(handle, (bipolar)at, 3);
                bipolar hole;

                if (data < 0)
                        break;
                if ((p64)data >= size)
                        break;
                hole = system_seek(handle, data, 4);
                if (hole < 0 || (p64)hole > size)
                        hole = (bipolar)size;
                if (!tar_ext_add((p64)data, (p64)hole - (p64)data))
                        return false;
                at = (p64)hole;
        }
        (void)system_seek(handle, 0, FILE_SEEK_SET);
        whole = tar_ext_count == 1 && tar_ext[0].offset == 0 &&
                tar_ext[0].length == size;
        return !whole && tar_ext_add(size, 0);
}

/* The data of each span, and the padding after the last. */
static bool tar_put_extents(bipolar archive, bipolar in, string_address name)
{
        for (positive at = 0; at < tar_ext_count; at++)
        {
                bipolar rest;

                if (!tar_ext[at].length)
                        continue;
                if (system_seek(in, (bipolar)tar_ext[at].offset, FILE_SEEK_SET) < 0)
                        return false;
                rest = tar_put_span(archive, in, tar_ext[at].length);
                if (rest < 0 ||
                    (rest > 0 && !tar_put_shortfall(archive, name, (p64)rest)))
                        return false;
        }
        return tar_write_padding(archive, tar_ext_data);
}

/* The 1.0 map: the count, then offset and length, a line each. */
static bool tar_map_text(void)
{
        p8 text[24];
        positive at;

        tar_map_used = 0;
        for (at = 0; at <= tar_ext_count; at++)
        {
                p64 numbers[2];
                positive each = at ? 2 : 1;

                numbers[0] = at ? tar_ext[at - 1].offset : tar_ext_count;
                numbers[1] = at ? tar_ext[at - 1].length : 0;
                for (positive one = 0; one < each; one++)
                {
                        positive length = positive_into(text, (positive)numbers[one]);

                        if (!shell_array_room(tar_map, tar_map_room,
                                              tar_map_used + length + 1))
                                return false;
                        memory_copy(tar_map + tar_map_used, text, length);
                        tar_map_used += length;
                        tar_map[tar_map_used++] = '\n';
                }
        }
        return true;
}

/* The attributes and ACLs of the object fd names, into the extended
   header being built. */
static bipolar tar_attr_fd = -1;

static bool tar_x_attributes(bipolar fd, bool directory, positive mode)
{
        p8 text[16384];
        bipolar names;
        positive have = 0;

        if (fd < 0 || (!tar_opt_xattrs && !tar_opt_acls))
                return true;
        if (tar_opt_acls)
        {
                bipolar access = system_call_4(syscall(fgetxattr), (positive)fd,
                                               (positive)"system.posix_acl_access",
                                               (positive)tar_attr_value,
                                               sizeof(tar_attr_value));
                bipolar fallback = directory
                    ? system_call_4(syscall(fgetxattr), (positive)fd,
                                    (positive)"system.posix_acl_default",
                                    (positive)(tar_attr_value + 8192), 8192)
                    : -1;

                if (access > 8192)
                        access = -1;
                if (access > 0 || fallback > 0)
                {
                        if (access > 0)
                        {
                                if (tar_acl_text(tar_attr_value, (positive)access, text,
                                                 sizeof(text), address_of have))
                                        tar_x_add("SCHILY.acl.access", (string_address)text, have);
                        }
                        else
                                tar_x_add("SCHILY.acl.access", (string_address)text,
                                          tar_acl_from_mode(mode, text));
                        if (fallback > 0 &&
                            tar_acl_text(tar_attr_value + 8192, (positive)fallback, text,
                                         sizeof(text), address_of have))
                                tar_x_add("SCHILY.acl.default", (string_address)text, have);
                }
        }
        if (!tar_opt_xattrs)
                return true;
        names = system_call_3(syscall(flistxattr), (positive)fd, (positive)tar_attr_names,
                              sizeof(tar_attr_names));
        for (positive at = 0; names > 0 && at < (positive)names;
             at += string_length((string_address)(tar_attr_names + at)) + 1)
        {
                string_address name = (string_address)(tar_attr_names + at);
                p8 key[320];
                positive length = string_length(name);
                bipolar got;

                if (length > 300 || !tar_xattr_wanted(name))
                        continue;
                got = system_call_4(syscall(fgetxattr), (positive)fd, (positive)name,
                                    (positive)tar_attr_value, sizeof(tar_attr_value));
                if (got < 0)
                        continue;
                memory_copy(key, "SCHILY.xattr.", 13);
                memory_copy(key + 13, name, length + 1);
                if (!tar_x_add((string_address)key, (string_address)tar_attr_value,
                               (positive)got))
                        return false;
        }
        return true;
}

/* The member's header, and whatever the format sends ahead of it. */
static bipolar tar_put_facts(bipolar handle, string_address name, p8 type,
                          p64 size, string_address link,
                          file_facts address_to facts)
{
        p8 transformed[TAR_PATH];
        p8 linked[TAR_PATH];

        // What a link points at is transformed too, by default.
        if (link && tar_transform_count_now() && (type == '1' || type == '2'))
        {
                string_address moved = tar_transformed(link);
                positive moved_length = string_length(moved);

                if (moved_length + 1 < sizeof(linked))
                {
                        memory_copy(linked, moved, moved_length + 1);
                        link = (string_address)linked;
                }
        }

        if (tar_transform_count_now())
        {
                // A directory's slash is added after the transform, as GNU adds it.
                positive whole = string_length(name);
                bool slash = whole > 1 && name[whole - 1] == '/';
                p8 bare[TAR_PATH];

                if (whole < sizeof(bare) - 2)
                {
                        memory_copy(bare, name, whole);
                        bare[slash ? whole - 1 : whole] = end;

                        string_address moved = tar_transformed((string_address)bare);
                        positive moved_length = string_length(moved);

                        if (moved_length + 2 < sizeof(transformed))
                        {
                                memory_copy(transformed, moved, moved_length);
                                if (slash && moved_length && transformed[moved_length - 1] != '/')
                                        transformed[moved_length++] = '/';
                                transformed[moved_length] = end;
                                name = (string_address)transformed;
                        }
                }
        }
        p8 block[TAR_BLOCK];
        p8 kept[TAR_NAME + 1];
        string_address leaf = name;
        positive name_length = string_length(name);
        positive link_length = link ? string_length(link) : 0;
        positive limit = TAR_NAME - (tar_format == TAR_OLDGNU);
        positive cut = 0;
        bool posix = tar_format == TAR_POSIX;
        bool named = tar_format == TAR_GNU || tar_format == TAR_OLDGNU;
        bool long_link = false;
        bool long_name = false;
        b64 mtime = (b64)facts->modified.seconds;
        bool time_ok = mtime >= 0 && mtime < ((b64)1 << 33);
        p64 mode = tar_format == TAR_OLDGNU ? (p64)facts->mode
                                            : (p64)(facts->mode & 07777);
        p8 owner[FILE_NAME_MAX];
        bool sparse = tar_sparse_use && type == '0';
        p64 field_size = size;
        p8 sparse_name[TAR_PATH];
        positive map_padded = 0;
        string_address real_name = name;

        if (tar_format == TAR_V7 && (type == '3' || type == '4' || type == '6'))
        {
                tar_say(name, "Unknown file type; file ignored");
                tar_status = 2;
                return 0;
        }

        tar_x_used = 0;
        if (sparse)
        {
                if (!posix)
                {
                        type = 'S';
                        field_size = (p64)tar_ext_data;
                }
                else if (tar_sparse_version == 2)
                {
                        if (!tar_map_text())
                                return -1;
                        map_padded = tar_padded(tar_map_used);
                        tar_x_add("GNU.sparse.major", "1", 1);
                        tar_x_add("GNU.sparse.minor", "0", 1);
                        tar_x_add("GNU.sparse.name", name, name_length);
                        tar_x_number("GNU.sparse.realsize", (b64)size);
                        field_size = (p64)(map_padded + tar_ext_data);
                }
                else
                {
                        tar_x_number("GNU.sparse.size", (b64)size);
                        tar_x_number("GNU.sparse.numblocks", (b64)tar_ext_count);
                        if (tar_sparse_version == 1)
                        {
                                p8 text[24];
                                positive at;

                                tar_x_add("GNU.sparse.name", name, name_length);
                                tar_map_used = 0;
                                for (at = 0; at < tar_ext_count; at++)
                                        for (positive one = 0; one < 2; one++)
                                        {
                                                positive length = positive_into(
                                                    text, (positive)(one ? tar_ext[at].length
                                                                         : tar_ext[at].offset));

                                                if (!shell_array_room(tar_map, tar_map_room,
                                                                      tar_map_used + length + 1))
                                                        return -1;
                                                if (tar_map_used)
                                                        tar_map[tar_map_used++] = ',';
                                                memory_copy(tar_map + tar_map_used, text, length);
                                                tar_map_used += length;
                                        }
                                tar_x_add("GNU.sparse.map", (string_address)tar_map,
                                          tar_map_used);
                        }
                        else
                                for (positive at = 0; at < tar_ext_count; at++)
                                {
                                        tar_x_number("GNU.sparse.offset", (b64)tar_ext[at].offset);
                                        tar_x_number("GNU.sparse.numbytes", (b64)tar_ext[at].length);
                                }
                        field_size = (p64)tar_ext_data;
                }
                if (posix && tar_sparse_version != 0)
                {
                        p8 middle[48];
                        positive at = 0;

                        memory_copy(middle, "/GNUSparseFile.", 15);
                        at = 15 + positive_into(middle + 15,
                                                (positive)system_call(syscall(getpid)));
                        middle[at++] = '/';
                        middle[at] = end;
                        tar_x_name(real_name, (string_address)middle, sparse_name);
                        name = (string_address)sparse_name;
                        leaf = name;
                        name_length = string_length(name);
                }
        }
        if ((type == '1' || type == '2') && link_length > limit)
        {
                if (posix)
                        tar_x_add("linkpath", link, link_length);
                else if (named)
                        long_link = true;
                else
                {
                        /* Said, and the header still goes out with the
                           first hundred bytes of the target. */
                        tar_say(link, "link name is too long; not dumped");
                        tar_status = 2;
                }
        }
        if (posix && !tar_ascii(name))
                tar_x_add("path", name, name_length);
        else if (name_length > limit)
        {
                if (posix)
                        tar_x_add("path", name, name_length);
                else if (named)
                        long_name = true;
                else if (tar_format == TAR_V7)
                {
                        tar_say(name, "file name is too long (max 99); not dumped");
                        tar_status = 2;
                        return 0;
                }
                else if (!tar_ustar_split(name, address_of cut))
                {
                        tar_say(name, "file name is too long (cannot be split); not dumped");
                        tar_status = 2;
                        return 0;
                }
        }

        memory_fill(block, 0, TAR_BLOCK);
        if (cut)
        {
                memory_copy(block + 345, name, cut);
                leaf = name + cut + 1;
                name_length -= cut + 1;
        }
        memory_copy(block, leaf, name_length < limit ? name_length : limit);
        tar_field_put_octal(block + 100, 8, mode);
        positive uid = tar_owner_forced[0] && tar_owner_forced_id[0] >= 0
                           ? (positive)tar_owner_forced_id[0] : facts->owner;
        positive gid = tar_owner_forced[1] && tar_owner_forced_id[1] >= 0
                           ? (positive)tar_owner_forced_id[1] : facts->group;

        if (!tar_number(block + 108, 8, (b64)uid, "uid", "uid_t") ||
            !tar_number(block + 116, 8, (b64)gid, "gid", "gid_t") ||
            !tar_number(block + 124, 12, (b64)field_size, "size", "off_t"))
                return 0;
        if (posix)
        {
                tar_field_put_octal(block + 136, 12, time_ok ? (p64)mtime : 0);
                if (facts->modified.nanoseconds || !time_ok)
                        tar_x_time("mtime", mtime, facts->modified.nanoseconds);
        }
        else if (!tar_number(block + 136, 12, mtime, null, "time_t"))
                return 0;

        block[156] = type == '0' && tar_format == TAR_V7 ? end : type;
        if (link_length)
                memory_copy(block + 157, link,
                            link_length < TAR_NAME ? link_length : TAR_NAME);

        if (tar_format != TAR_V7)
        {
                if (named)
                        memory_copy(block + 257, "ustar  ", 8);
                else
                {
                        memory_copy(block + 257, "ustar", 6);
                        block[263] = '0';
                        block[264] = '0';
                }
                if (!tar_numeric_owner)
                {
                        //      Both fields are 32 bytes holding a terminated
                        //      name, so 31 bytes of name at most.
                        if (tar_owner_forced[0])
                                string_copy_max_end(block + 265, (string_address)tar_owner_forced_name[0], 31);
                        else if (file_user_name(facts->owner, owner, sizeof(owner)))
                                string_copy_max_end(block + 265, owner, 31);
                        if (tar_owner_forced[1])
                                string_copy_max_end(block + 297, (string_address)tar_owner_forced_name[1], 31);
                        else if (file_group_name(facts->group, owner, sizeof(owner)))
                                string_copy_max_end(block + 297, owner, 31);
                }
                if (type == '3' || type == '4')
                        if (!tar_number(block + 329, 8, (b64)facts->rdev_major, null,
                                        "major_t") ||
                            !tar_number(block + 337, 8, (b64)facts->rdev_minor, null,
                                        "minor_t"))
                                return 0;
        }
        if (type == 'S')
        {
                positive at;

                for (at = 0; at < 4 && at < tar_ext_count; at++)
                        if (!tar_number(block + 386 + at * 24, 12,
                                        (b64)tar_ext[at].offset, null, "off_t") ||
                            !tar_number(block + 398 + at * 24, 12,
                                        (b64)tar_ext[at].length, null, "off_t"))
                                return 0;
                block[482] = tar_ext_count > 4;
                if (!tar_number(block + 483, 12, (b64)size, null, "off_t"))
                        return 0;
        }
        if (posix)
        {
                tar_x_time("atime", (b64)facts->accessed.seconds,
                           facts->accessed.nanoseconds);
                tar_x_time("ctime", (b64)facts->changed.seconds,
                           facts->changed.nanoseconds);
                tar_x_attributes(tar_attr_fd, (facts->mode & MODE_FORMAT) == MODE_DIRECTORY,
                                 facts->mode & 0777);
        }
        tar_header_put_checksum(block);

        if (long_link && !tar_put_long(handle, 'K', link))
                return -1;
        if (long_name && !tar_put_long(handle, 'L', name))
                return -1;
        if (tar_x_used)
        {
                p8 header[TAR_BLOCK];

                tar_x_name(real_name, "/PaxHeaders/", kept);
                tar_private_header(header, (string_address)kept, tar_x_used,
                                   'x', mtime);
                if (!tar_write_block(handle, header) ||
                    !tar_put(handle, tar_x, tar_x_used) ||
                    !tar_write_padding(handle, tar_x_used))
                        return -1;
        }
        if (tar_create_verbose > 1)
                tar_long_line(block, type, mode & 07777, size, uid,
                              gid, facts->rdev_major, facts->rdev_minor,
                              mtime, name, link, false);
        tar_dumped++;
        if (!tar_write_block(handle, block))
                return -1;
        if (type == 'S')
        {
                positive at;

                for (at = 4; at < tar_ext_count; at += 21)
                {
                        p8 more[TAR_BLOCK];

                        memory_fill(more, 0, TAR_BLOCK);
                        for (positive one = 0; one < 21 && at + one < tar_ext_count; one++)
                                if (!tar_number(more + one * 24, 12,
                                                (b64)tar_ext[at + one].offset, null, "off_t") ||
                                    !tar_number(more + one * 24 + 12, 12,
                                                (b64)tar_ext[at + one].length, null, "off_t"))
                                        return -1;
                        more[504] = at + 21 < tar_ext_count;
                        if (!tar_write_block(handle, more))
                                return -1;
                }
        }
        else if (map_padded)
        {
                if (!tar_put(handle, tar_map, tar_map_used) ||
                    !tar_write_padding(handle, tar_map_used))
                        return -1;
        }
        return 1;
}

static p64 tar_identity(file_facts address_to facts)
{
        return ((p64)facts->device_major << 32) | facts->device_minor;
}

static positive tar_seen_hash(file_facts address_to facts)
{
        p64 mix = facts->inode * 0x9e3779b97f4a7c15ull ^ tar_identity(facts);

        return (positive)(mix ^ (mix >> 29));
}

static string_address tar_seen_name(file_facts address_to facts)
{
        positive slot;

        if (facts->hard_links < 2 || !tar_seen_slots)
                return null;
        slot = tar_seen_hash(facts) & (tar_seen_slots - 1);
        while (tar_seen[slot].used)
        {
                if (tar_seen[slot].inode == facts->inode &&
                    tar_seen[slot].device == tar_identity(facts))
                        return (string_address)(tar_seen_names + tar_seen[slot].name_at);
                slot = (slot + 1) & (tar_seen_slots - 1);
        }
        return null;
}

static fn tar_seen_store(file_facts address_to facts, string_address member)
{
        positive length;
        positive slot;

        if (facts->hard_links < 2 || tar_seen_used >= TAR_SEEN_LIMIT)
                return;

        length = string_length(member);
        if (length >= TAR_PATH || tar_seen_fill + length + 1 > TAR_SEEN_NAMES_LIMIT ||
            !shell_array_room(tar_seen_names, tar_seen_names_room,
                              tar_seen_fill + length + 1))
                return;
        if (tar_seen_used + 1 > tar_seen_slots / 2)
        {
                positive larger = tar_seen_slots ? tar_seen_slots * 2 : 1024;
                tar_seen_file address_to old = tar_seen;
                positive old_slots = tar_seen_slots;
                tar_seen_file address_to fresh = memory_take_zeroed(larger, sizeof(*fresh));

                if (!fresh)
                        return;
                for (positive at = 0; at < old_slots; at++)
                        if (old[at].used)
                        {
                                file_facts moved;

                                moved.inode = old[at].inode;
                                moved.device_major = (p32)(old[at].device >> 32);
                                moved.device_minor = (p32)old[at].device;
                                positive place = tar_seen_hash(address_of moved) & (larger - 1);

                                while (fresh[place].used)
                                        place = (place + 1) & (larger - 1);
                                fresh[place] = old[at];
                        }
                if (old)
                        memory_give(old);
                tar_seen = fresh;
                tar_seen_slots = larger;
        }
        slot = tar_seen_hash(facts) & (tar_seen_slots - 1);
        while (tar_seen[slot].used)
                slot = (slot + 1) & (tar_seen_slots - 1);
        tar_seen[slot].used = true;
        tar_seen[slot].inode = facts->inode;
        tar_seen[slot].device = tar_identity(facts);
        tar_seen[slot].name_at = tar_seen_fill;
        memory_copy(tar_seen_names + tar_seen_fill, member, length + 1);
        tar_seen_fill += length + 1;
        tar_seen_used += 1;
}

/*
        -r appends to an archive and -u only what is newer than the member of
        that name already in it: the archive is read to its end marker, which
        the new members overwrite, and cut to length after the new end. Only
        an archive that is a plain file can be written in the middle, and a
        compressed one cannot be. The names and times of what is there are
        kept for -u.
*/
static bool tar_updating;
static byte_store tar_archived_names;
static p64 address_to tar_archived_times;
static positive address_to tar_archived_offsets;
static positive tar_archived_count;
static positive tar_archived_room;
static bool tar_skip_header;
static p64 tar_append_end;

static bool tar_archived_note(string_address name, p64 time)
{
        if (tar_archived_count == tar_archived_room)
        {
                positive room = tar_archived_room ? tar_archived_room * 2 : 256;
                p64 address_to times = memory_resize(tar_archived_times, room * sizeof(p64));

                if (!times)
                        return false;
                tar_archived_times = times;

                positive address_to offsets = memory_resize(tar_archived_offsets, room * sizeof(positive));

                if (!offsets)
                        return false;
                tar_archived_offsets = offsets;
                tar_archived_room = room;
        }

        positive length = string_length(name);

        if (!byte_store_reserve(address_of tar_archived_names, tar_archived_names.used + length + 1, 4096))
                return false;
        memory_copy(tar_archived_names.bytes + tar_archived_names.used, name, length + 1);
        tar_archived_offsets[tar_archived_count] = tar_archived_names.used;
        tar_archived_times[tar_archived_count] = time;
        tar_archived_names.used += length + 1;
        tar_archived_count++;
        return true;
}

// Whether the archive already holds this name as new as the file.
static bool tar_archived_current(string_address name, p64 time)
{
        p64 latest = 0;
        bool found = false;
        positive length = string_length(name);

        while (length > 1 && name[length - 1] == '/')
                length--;
        for (positive at = 0; at < tar_archived_count; at++)
        {
                string_address held = (string_address)tar_archived_names.bytes + tar_archived_offsets[at];
                positive held_length = string_length(held);

                while (held_length > 1 && held[held_length - 1] == '/')
                        held_length--;
                if (held_length == length && !memory_compare(held, name, length) &&
                    (!found || tar_archived_times[at] > latest))
                {
                        latest = tar_archived_times[at];
                        found = true;
                }
        }
        return found && latest >= time;
}

// The offset of the end marker of an archive open for reading and writing,
// with what it holds noted; -1 when it is not a tar archive.
static bipolar tar_scan_end(bipolar handle)
{
        p8 block[TAR_BLOCK];
        p8 long_name[TAR_PATH];
        bool have_long = false;
        p64 offset = 0;

        tar_archived_count = 0;
        tar_archived_names.used = 0;
        for (;;)
        {
                bipolar got = system_read_retry((positive)handle, block, TAR_BLOCK);

                if (got == 0)
                        return (bipolar)offset;
                if (got != TAR_BLOCK)
                        return -1;
                if (memory_sum_bytes(block, TAR_BLOCK) == 0)
                        return (bipolar)offset;
                if (!tar_header_ok(block))
                        return -1;

                p64 size;
                p64 time = 0;
                p8 type = block[156];

                if (!tar_field_value(block + 124, 12, address_of size))
                        return -1;
                if (!tar_field_moment(block + 136, 12, address_of time))
                        time = 0;

                offset += TAR_BLOCK;
                positive data = (positive)((size + TAR_BLOCK - 1) / TAR_BLOCK * TAR_BLOCK);

                if (type == 'L' && size < TAR_PATH)
                {
                        if (system_read_retry((positive)handle, long_name, (positive)size) != (bipolar)size)
                                return -1;
                        long_name[size] = end;
                        have_long = true;
                        if (system_seek(handle, (bipolar)(data - size), FILE_SEEK_CUR) < 0)
                                return -1;
                        offset += data;
                        continue;
                }
                if (type == 'x' || type == 'g' || type == 'K')
                {
                        if (system_seek(handle, (bipolar)data, FILE_SEEK_CUR) < 0)
                                return -1;
                        offset += data;
                        continue;
                }

                if (tar_updating)
                {
                        p8 name[TAR_PATH];

                        if (have_long)
                                string_copy_bounded((char address_to)name, (string_address)long_name, sizeof(name));
                        else
                        {
                                positive at = 0;

                                if (!memory_compare(block + 257, "ustar", 5) && block[345])
                                {
                                        for (positive i = 0; i < 155 && block[345 + i]; i++)
                                                name[at++] = block[345 + i];
                                        name[at++] = '/';
                                }
                                for (positive i = 0; i < 100 && block[i] && at < sizeof(name) - 1; i++)
                                        name[at++] = block[i];
                                name[at] = end;
                        }
                        if (!tar_archived_note((string_address)name, time))
                                return -1;
                }
                have_long = false;
                if (system_seek(handle, (bipolar)data, FILE_SEEK_CUR) < 0)
                        return -1;
                offset += data;
        }
}

static b32 tar_add_named(bipolar archive, bipolar directory,
                         string_address name, string_address member,
                         bool verbose, bool selected, p8 kind);

static b32 tar_add_directory_walk(bipolar archive, file_walk address_to walk_in,
                                  string_address member,
                                  file_facts address_to facts, bool verbose);

static b32 tar_add_directory(bipolar archive, bipolar directory,
                             string_address name, string_address member,
                             file_facts address_to facts, bool verbose)
{
        file_walk walk;

        if (!file_walk_open_found_same(address_of walk, directory, name,
                                       facts))
                return tar_fail_at(member, "Cannot open", walk.error), tar_status;
        return tar_add_directory_walk(archive, address_of walk, member, facts, verbose);
}

static b32 tar_add_directory_walk(bipolar archive, file_walk address_to walk_in,
                                  string_address member,
                                  file_facts address_to facts, bool verbose)
{
        file_walk walk = *walk_in;

        p8 spelled[TAR_PATH];

        bipolar put;

        tar_attr_fd = walk.handle;
        if (tar_updating && tar_archived_current(tar_transformed(member),
                                                 (p64)facts->modified.seconds))
                put = 1;
        else
                put = tar_put_facts(archive,
                                    tar_spell_directory(member, spelled,
                                                        sizeof(spelled))
                                        ? (string_address)spelled : member,
                                    '5', 0, null, facts);
        tar_attr_fd = -1;

        if (put <= 0)
        {
                if (put < 0)
                        tar_create_fatal = true;
                file_walk_close(address_of walk);
                return tar_status;
        }

        for (;;)
        {
                struct linux_dirent64 address_to entry = file_walk_next(
                    address_of walk);
                p8 child[TAR_PATH];

                if (!entry)
                        break;

                if (file_is_dot(entry->d_name))
                        continue;

                if (!file_path_join(child, member, entry->d_name))
                {
                        tar_refuse("member name is too long");
                        break;
                }

                tar_add_named(archive, walk.handle, entry->d_name, child,
                              verbose, false, entry->d_type);
                if (tar_create_fatal)
                        break;
        }

        file_walk_close(address_of walk);
        return tar_status;
}

/* kind is the directory entry's d_type, DT_UNKNOWN for a named operand.
   A regular file is opened first and looked at through its descriptor:
   the facts are then those of the very object whose bytes are read, and
   one statx by name is saved on every file.  Anything that will not open
   so, or turns out not to be a regular file, takes the look by name. */
static b32 tar_add_named(bipolar archive, bipolar directory,
                         string_address name, string_address member,
                         bool verbose, bool selected, p8 kind)
{
        file_facts facts;
        p8 link[TAR_PATH];
        bipolar handle = -1;
        bipolar looked = -1;
        string_address prior;
        p8 type;

        if (tar_excluded(member))
                return tar_status;

        if (tar_updating && kind != DT_DIR)
        {
                file_facts newest;

                if (file_look_code(directory, name, tar_dereference ? 0 : AT_SYMLINK_NOFOLLOW,
                                   address_of newest) >= 0 &&
                    (newest.mode & MODE_FORMAT) != MODE_DIRECTORY &&
                    tar_archived_current(tar_transformed(member), (p64)newest.modified.seconds))
                        return tar_status;
        }

        if (tar_dereference)
        {
                /*      -h archives what a link names, not the link: the name is
                        opened following it, and what was opened is looked at and
                        stored as the member of the link's own name. */
                file_facts entry;

                if (file_look_code(directory, name, AT_SYMLINK_NOFOLLOW,
                                   address_of entry) >= 0 &&
                    (entry.mode & MODE_FORMAT) == MODE_LINK)
                {
                        bipolar followed = system_open_at(
                            directory, name, O_PATH | O_CLOEXEC);
                        file_facts target;

                        if (followed < 0 ||
                            file_look_code(followed, (string_address)"", AT_EMPTY_PATH,
                                           address_of target) < 0)
                        {
                                bipolar why = followed < 0 ? followed : -ERROR_NO_ENTRY;

                                if (followed >= 0)
                                        system_close(followed);
                                tar_fail_at(member, "Cannot stat", why);
                                return tar_status;
                        }
                        if ((target.mode & MODE_FORMAT) == MODE_DIRECTORY)
                        {
                                system_close(followed);
                                followed = system_open_at(directory, name,
                                                          FILE_READ | O_DIRECTORY | O_CLOEXEC);
                                if (followed < 0)
                                {
                                        tar_fail_at(member, "Cannot open", followed);
                                        return tar_status;
                                }
                                if (verbose)
                                {
                                        p8 spelled[TAR_PATH];

                                        tar_name_line(tar_listing,
                                                      tar_spell_directory(member, spelled, sizeof(spelled))
                                                          ? (string_address)spelled : member);
                                }
                                file_walk walk = {.handle = followed, .error = 0, .have = 0, .at = 0};
                                b32 result = tar_add_directory_walk(archive, address_of walk, member,
                                                                    address_of target, verbose);

                                return result;
                        }
                        if ((target.mode & MODE_FORMAT) == MODE_FILE)
                        {
                                system_close(followed);
                                followed = system_open_at(directory, name,
                                                          FILE_READ | O_CLOEXEC | O_NONBLOCK);
                                if (followed < 0)
                                {
                                        tar_fail_at(member, "Cannot open", followed);
                                        return tar_status;
                                }
                                handle = followed;
                                facts = target;
                                looked = 0;
                                kind = DT_REG;
                                goto dereferenced;
                        }
                        system_close(followed);
                }
        }

        if (kind == DT_REG)
        {
                handle = system_open_at(
                    directory, name,
                    FILE_READ | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
                looked = handle < 0 ? handle :
                         file_look_code(handle, (string_address)"",
                                        AT_EMPTY_PATH, address_of facts);
                if (looked >= 0 &&
                    ((facts.mask & STATX_BASIC) != STATX_BASIC ||
                     (facts.mode & MODE_FORMAT) != MODE_FILE))
                        looked = -1;
                if (looked < 0 && handle >= 0)
                        system_close(handle);
                if (looked < 0)
                        handle = -1;
        }
        if (handle < 0)
                looked = system_stat_at(directory, name,
                                        AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT,
                                        STATX_BASIC, address_of facts);
        if (looked < 0)
        {
                tar_fail_at(member, "Cannot stat", looked);
                return tar_status;
        }

dereferenced:;
        bool active_output =
            tar_output_known &&
            file_same_identity(address_of facts, address_of tar_output_facts);
        bool replaced_output =
            tar_output_target_known &&
            file_same_identity(address_of facts,
                               address_of tar_output_target_facts);
        if ((tar_output_stage_known &&
             file_same_identity(address_of facts,
                                address_of tar_output_stage_facts)) ||
            active_output || replaced_output)
        {
                if (handle >= 0)
                        system_close(handle);
                handle = -1;
        }
        if (tar_output_stage_known &&
            file_same_identity(address_of facts,
                               address_of tar_output_stage_facts))
                return tar_status;
        if (active_output || replaced_output)
        {
                log_error("tar: ", 5);
                writer_terminal_name(log_error, member);
                log_error(": archive cannot contain itself; not dumped\n", 44);
                if (selected && replaced_output)
                {
                        tar_status = 2;
                        tar_create_fatal = true;
                }
                return tar_status;
        }

        if (verbose)
        {
                p8 spelled[TAR_PATH];

                tar_name_line(tar_listing,
                              (facts.mode & MODE_FORMAT) == MODE_DIRECTORY &&
                                      tar_spell_directory(member, spelled,
                                                          sizeof(spelled))
                                  ? (string_address)spelled : member);
        }

        if ((facts.mode & MODE_FORMAT) == MODE_DIRECTORY)
                return tar_add_directory(archive, directory, name, member,
                                         address_of facts, verbose);

        if ((facts.mode & MODE_FORMAT) == MODE_SOCKET)
        {
                string_format(log_error, "tar: %w: socket ignored\n", writer_terminal_name, member);
                tar_status = tar_status ? tar_status : 1;
                return tar_status;
        }

        if ((facts.mode & MODE_FORMAT) == MODE_LINK)
        {
                handle = file_open_same(directory, name, address_of facts,
                                        O_PATH | O_NOFOLLOW);
                bipolar got = handle < 0
                                  ? handle
                                  : system_read_link_at(
                                        handle, (string_address)"", link,
                                        sizeof(link));

                if (got < 0 || (positive)got >= sizeof(link))
                {
                        if (handle >= 0)
                                system_close(handle);
                        tar_fail_at(member, "Cannot readlink",
                                    got < 0 ? got : -ERROR_NAME_TOO_LONG);
                        return tar_status;
                }

                link[got] = end;
                if (tar_put_facts(archive, member, '2', 0, link, address_of facts) < 0)
                        tar_create_fatal = true;
                system_close(handle);
                return tar_status;
        }

        prior = tar_seen_name(address_of facts);
        if (prior)
        {
                if (handle >= 0)
                        system_close(handle);
                if (tar_put_facts(archive, member, '1', 0, prior, address_of facts) < 0)
                        tar_create_fatal = true;
                return tar_status;
        }

        if ((facts.mode & MODE_FORMAT) == MODE_PIPE)
                type = '6';
        else if ((facts.mode & MODE_FORMAT) == MODE_CHARACTER)
                type = '3';
        else if ((facts.mode & MODE_FORMAT) == MODE_BLOCK)
                type = '4';
        else
                type = '0';

        if (type != '0')
        {
                if (tar_put_facts(archive, member, type, 0, null, address_of facts) < 0)
                        tar_create_fatal = true;
                return tar_status;
        }

        if (handle < 0)
                handle = file_open_same(
                    directory, name, address_of facts,
                    FILE_READ | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (handle < 0)
        {
                tar_fail_at(member, "Cannot open", handle);
                return tar_status;
        }

        bool sparse = tar_sparse_on && tar_sparse_scan(handle, address_of facts);
        bipolar put;

        tar_sparse_use = sparse;
        tar_attr_fd = handle;
        put = tar_put_facts(archive, member, '0', (p64)facts.size, null,
                            address_of facts);
        tar_attr_fd = -1;
        tar_sparse_use = false;

        if (put <= 0)
        {
                if (put < 0)
                        tar_create_fatal = true;
                system_close(handle);
                return tar_status;
        }

        if (!(sparse ? tar_put_extents(archive, handle, member)
                     : tar_put_file(archive, handle, (p64)facts.size, member)))
        {
                tar_fail(member, -ERROR_INPUT_OUTPUT);
                tar_create_fatal = true;
        }
        else
                tar_seen_store(address_of facts, member);

        system_close(handle);
        return tar_status;
}

static b32 tar_add_path(bipolar archive, string_address path, bool verbose)
{
        return tar_add_named(archive, AT_FDCWD, path, path, verbose, true,
                             DT_UNKNOWN);
}

static b32 tar_write_archive(struct tar_options address_to options)
{
        bipolar handle;
        bipolar looked;
        file_staged_name output_stage;
        bool managed_output = false;
        positive count = tar_word_count;
        positive at;

        if (options->first >= count)
        {
                tar_refuse("cowardly refusing to create an empty archive");
                return tar_status;
        }

        tar_pack = options->pack;
        if (tar_pack == TAR_PACK_AUTO)
                tar_pack = tar_pack_from_name(options->archive);
        if (tar_refuse_pack(tar_pack))
                return tar_status;

        bool in_place = options->mode == 'r' || options->mode == 'u';

        tar_reset();
        tar_updating = options->mode == 'u';
        tar_create_verbose = options->verbose;
        tar_listing = log_error;
        tar_create_fatal = false;
        tar_dumped = 0;
        tar_out_bytes = 0;
        tar_numeric_owner = options->numeric;
        tar_format = options->format;
        tar_blocking = options->blocking;
        tar_sparse_on = options->sparse;
        tar_sparse_use = false;
        tar_sparse_version = options->sparse_version;
        output_stage.directory = -1;
        output_stage.handle = -1;
        if (in_place)
        {
                if (!options->archive || string_equals(options->archive, "-"))
                {
                        tar_refuse("Cannot update standard output");
                        tar_fatal_exit = true;
                        return tar_status;
                }
                if (tar_pack != TAR_PACK_NONE)
                {
                        tar_refuse("Cannot update compressed archives");
                        tar_fatal_exit = true;
                        return tar_status;
                }
                handle = system_open_at(AT_FDCWD, options->archive, O_RDWR | O_CLOEXEC);
                if (handle < 0)
                {
                        tar_fail_at(options->archive, "Cannot open", handle);
                        tar_fatal_exit = true;
                        return tar_status;
                }

                bipolar end_at = tar_scan_end(handle);

                if (end_at < 0 || system_seek(handle, end_at, FILE_SEEK_SET) < 0)
                {
                        system_close(handle);
                        tar_refuse("This does not look like a tar archive");
                        tar_fatal_exit = true;
                        return tar_status;
                }
                tar_listing = log;
                tar_append_end = (p64)end_at;
                tar_out_bytes = (p64)end_at;
        }
        else if (!options->archive || string_equals(options->archive, "-"))
                handle = 1;
        else
        {
                tar_listing = log;
                handle = file_staged_name_open(
                    address_of output_stage, options->archive,
                    0666 & ~file_umask(), FILE_STAGED_STREAM_SPECIAL);
                if (handle < 0)
                {
                        tar_fail_at(options->archive, "Cannot open", handle);
                        tar_fatal_exit = true;
                        return tar_status;
                }
                managed_output = true;
                tar_output_target_known = output_stage.replaced_known;
                if (tar_output_target_known)
                        tar_output_target_facts = output_stage.replaced;

                if (!output_stage.direct)
                {
                        looked = file_look_code(
                            output_stage.protected.directory,
                            (string_address)"", AT_EMPTY_PATH,
                            address_of tar_output_stage_facts);
                        if (looked >= 0 &&
                            ((tar_output_stage_facts.mask & STATX_BASIC) !=
                                 STATX_BASIC ||
                             (tar_output_stage_facts.mode & MODE_FORMAT) !=
                                 MODE_DIRECTORY))
                                looked = -ERROR_INPUT_OUTPUT;
                        if (looked < 0)
                        {
                                tar_fail(options->archive, looked);
                                file_staged_name_abort(address_of output_stage);
                                return tar_status;
                        }
                        tar_output_stage_known = true;
                }
        }

        looked = file_look_code(handle, (string_address)"", AT_EMPTY_PATH,
                                address_of tar_output_facts);
        if (looked < 0 ||
            (tar_output_facts.mask & STATX_BASIC) != STATX_BASIC)
        {
                if (looked >= 0)
                        looked = -ERROR_INPUT_OUTPUT;
                tar_fail(options->archive ? options->archive
                                          : (string_address)"-",
                         looked);
                if (managed_output)
                        file_staged_name_abort(address_of output_stage);
                else if (handle > 2)
                        system_close(handle);
                return tar_status;
        }
        tar_output_known = true;
        tar_advise(handle);
        if (!tar_codec_begin_write(handle))
        {
                if (managed_output)
                        file_staged_name_abort(address_of output_stage);
                else if (handle > 2)
                        system_close(handle);
                return tar_status;
        }

        if (options->directory)
        {
                bipolar moved = tar_change_directory(options->directory);

                if (moved < 0)
                {
                    tar_fail(options->directory, moved);
                    tar_codec_end_write();
                    if (managed_output)
                            file_staged_name_abort(address_of output_stage);
                    else if (handle > 2)
                            system_close(handle);

                    return tar_status;
                }
        }

        for (at = options->first; at < count && !tar_create_fatal; at++)
                tar_add_path(handle, tar_words[at],
                             options->verbose == 1);

        memory_fill(tar_block, 0, TAR_BLOCK);
        if (!tar_create_fatal)
        {
                positive record = tar_blocking * TAR_BLOCK;
                positive short_by;

                tar_write_block(handle, tar_block);
                tar_write_block(handle, tar_block);
                /* The archive ends on a whole record, as the reference's
                   does: the blocking factor's worth of blocks. */
                short_by = (positive)((tar_out_bytes + tar_at) % record);
                for (short_by = short_by ? record - short_by : 0; short_by;
                     short_by -= TAR_BLOCK)
                        if (!tar_write_block(handle, tar_block))
                                break;
        }

        tar_flush(handle);
        if (tar_encoder && !tar_codec_end_write())
                tar_create_fatal = true;
        if (managed_output)
        {
                /* What could be archived is kept, as the reference keeps it;
                   an archive of nothing, because every operand failed, does
                   not replace what the name held. */
                bool keep = !tar_create_fatal &&
                            (tar_status != 2 || tar_dumped || !tar_output_target_known);
                bipolar finished = file_staged_name_finish(
                    address_of output_stage,
                    keep, 0);
                if (finished < 0 && keep)
                        tar_fail(options->archive, finished);
        }
        else if (handle > 2)
        {
                if (in_place && !tar_create_fatal)
                {
                        // The old end marker and what followed it were written
                        // over or are left behind the new end: cut there.
                        bipolar here = system_seek(handle, 0, FILE_SEEK_CUR);

                        if (here > 0)
                                (void)system_truncate_handle(handle, (positive)here);
                }
                bipolar closed = system_close(handle);
                if (closed < 0 && !tar_create_fatal)
                        tar_fail(options->archive, closed);
        }

        log_flush();
        return tar_status;
}

/*
        Every spelling tar takes and its arity.  Long-only options answer to
        codes below any letter; long names match exactly, abbreviations are
        not taken.
*/
enum
{
        TAR_NO_SAME_PERMISSIONS = 1,
        TAR_SAME_OWNER,
        TAR_NUMERIC_OWNER,
        TAR_ZSTD,
        TAR_COMPRESS_PROGRAM,
        TAR_STRIP_COMPONENTS,
        TAR_HELP,
        TAR_VERSION,
        TAR_POSIX_OPTION,
        TAR_PAX_OPTION,
        TAR_NO_SAME_OWNER,
        TAR_SPARSE_VERSION,
        TAR_XATTRS,
        TAR_NO_XATTRS,
        TAR_ACLS,
        TAR_NO_ACLS,
        TAR_SELINUX,
        TAR_NO_SELINUX,
        TAR_XATTRS_INCLUDE,
        TAR_XATTRS_EXCLUDE,
        TAR_EXCLUDE,
        TAR_NULL,
        TAR_ANCHORED,
        TAR_NO_ANCHORED,
        TAR_OWNER,
        TAR_GROUP,
        TAR_TRANSFORM,
};

static const argument_option tar_option_rules[] = {
    {"extract", 'x'},
    {"list", 't'},
    {"create", 'c'},
    {"append", 'r'},
    {"update", 'u'},
    {"diff", 'd'},
    {"compare", 'd'},
    {"file", 'f', ARGUMENT_REQUIRED},
    {"directory", 'C', ARGUMENT_REQUIRED},
    {"strip-components", TAR_STRIP_COMPONENTS, ARGUMENT_REQUIRED | ARGUMENT_LONG_ONLY},
    {"verbose", 'v'},
    {"absolute-names", 'P'},
    {"preserve-permissions", 'p'},
    {"same-permissions", 'p'},
    {"no-same-permissions", TAR_NO_SAME_PERMISSIONS, ARGUMENT_LONG_ONLY},
    {"touch", 'm'},
    {"no-same-owner", TAR_NO_SAME_OWNER, ARGUMENT_LONG_ONLY},
    {"old-archive", 'o'},
    {"portability", 'o'},
    {"format", 'H', ARGUMENT_REQUIRED},
    {"sparse", 'S'},
    {"xattrs", TAR_XATTRS, ARGUMENT_LONG_ONLY},
    {"no-xattrs", TAR_NO_XATTRS, ARGUMENT_LONG_ONLY},
    {"acls", TAR_ACLS, ARGUMENT_LONG_ONLY},
    {"no-acls", TAR_NO_ACLS, ARGUMENT_LONG_ONLY},
    {"selinux", TAR_SELINUX, ARGUMENT_LONG_ONLY},
    {"no-selinux", TAR_NO_SELINUX, ARGUMENT_LONG_ONLY},
    {"xattrs-include", TAR_XATTRS_INCLUDE, ARGUMENT_REQUIRED | ARGUMENT_LONG_ONLY},
    {"xattrs-exclude", TAR_XATTRS_EXCLUDE, ARGUMENT_REQUIRED | ARGUMENT_LONG_ONLY},
    {"sparse-version", TAR_SPARSE_VERSION, ARGUMENT_REQUIRED | ARGUMENT_LONG_ONLY},
    {"exclude", TAR_EXCLUDE, ARGUMENT_REQUIRED | ARGUMENT_LONG_ONLY},
    {"exclude-from", 'X', ARGUMENT_REQUIRED},
    {"files-from", 'T', ARGUMENT_REQUIRED},
    {"null", TAR_NULL, ARGUMENT_LONG_ONLY},
    {"anchored", TAR_ANCHORED, ARGUMENT_LONG_ONLY},
    {"no-anchored", TAR_NO_ANCHORED, ARGUMENT_LONG_ONLY},
    {"dereference", 'h'},
    {"transform", TAR_TRANSFORM, ARGUMENT_REQUIRED | ARGUMENT_LONG_ONLY},
    {"xform", TAR_TRANSFORM, ARGUMENT_REQUIRED | ARGUMENT_LONG_ONLY},
    {"owner", TAR_OWNER, ARGUMENT_REQUIRED | ARGUMENT_LONG_ONLY},
    {"group", TAR_GROUP, ARGUMENT_REQUIRED | ARGUMENT_LONG_ONLY},
    {"posix", TAR_POSIX_OPTION, ARGUMENT_LONG_ONLY},
    {"pax-option", TAR_PAX_OPTION, ARGUMENT_REQUIRED | ARGUMENT_LONG_ONLY},
    {"blocking-factor", 'b', ARGUMENT_REQUIRED},
    {"same-owner", TAR_SAME_OWNER, ARGUMENT_LONG_ONLY},
    /* Owners are only ever restored by number. */
    {"numeric-owner", TAR_NUMERIC_OWNER, ARGUMENT_LONG_ONLY},
    {"gzip", 'z'},
    {"gunzip", 'z'},
    {"xz", 'J'},
    {"zstd", TAR_ZSTD, ARGUMENT_LONG_ONLY},
    {"auto-compress", 'a'},
    {"bzip2", 'j'},
    {"compress", 'Z'},
    {"use-compress-program", TAR_COMPRESS_PROGRAM, ARGUMENT_LONG_ONLY},
    {"help", TAR_HELP, ARGUMENT_LONG_ONLY},
    {"version", TAR_VERSION, ARGUMENT_LONG_ONLY},
    {null},
};

/* GNU's old-style keys: a first word of nothing but short option letters. */
static bool tar_is_cluster(string_address word)
{
        if (!*word)
                return false;

        for (; *word; word++)
        {
                const argument_option address_to option =
                    argument_option_short(tar_option_rules, *word);

                if (!option || (option->mode & ARGUMENT_LONG_ONLY))
                        return false;
        }

        return true;
}

/* One option, in command-line order.  False stops the parse. */
static bool tar_take(struct tar_options address_to options, p8 letter,
                     string_address value)
{
        positive used;

        switch (letter)
        {
        case 'x': case 't': case 'c': case 'd': options->mode = letter; break;
        case 'r': case 'u': options->mode = letter; break;
        case 'f': options->archive = value; break;
        case 'C': options->directory = value; break;
        case 'v': options->verbose++; break;
        case 'P': options->absolute = true; break;
        case 'p': options->permissions = 1; break;
        case TAR_NO_SAME_PERMISSIONS: options->permissions = -1; break;
        case 'm': options->touch = true; break;
        case 'o': options->short_o = true; break;
        case TAR_NO_SAME_OWNER: options->owner = -1; break;
        case TAR_POSIX_OPTION:
                options->format = TAR_POSIX;
                options->format_set = true;
                break;
        case 'H':
        {
                static const struct
                {
                        string_address name;
                        p8 format;
                } formats[] = {{"gnu", TAR_GNU}, {"oldgnu", TAR_OLDGNU},
                               {"pax", TAR_POSIX}, {"posix", TAR_POSIX},
                               {"ustar", TAR_USTAR}, {"v7", TAR_V7}};
                positive at;

                for (at = 0; at < array_count(formats); at++)
                        if (string_equals(value, formats[at].name))
                        {
                                options->format = formats[at].format;
                                options->format_set = true;
                                break;
                        }
                if (at == array_count(formats))
                {
                        string_format(log_error, "tar: %w: Invalid archive format\n",
                                      writer_terminal_name, value);
                        tar_usage_hint();
                        tar_status = 2;
                        return false;
                }
                break;
        }
        case TAR_PAX_OPTION:
                options->pax_option = value;
                break;
        case 'S': options->sparse = true; break;
        case TAR_XATTRS: options->xattrs = true; break;
        case TAR_NO_XATTRS: options->xattrs = false; break;
        case TAR_ACLS: options->acls = true; break;
        case TAR_NO_ACLS: options->acls = false; break;
        case TAR_SELINUX: options->selinux = true; break;
        case TAR_NO_SELINUX: options->selinux = false; break;
        case TAR_XATTRS_INCLUDE:
                options->xattrs = true;
                if (tar_xattr_include_count < array_count(tar_xattr_include))
                        tar_xattr_include[tar_xattr_include_count++] = value;
                break;
        case TAR_XATTRS_EXCLUDE:
                options->xattrs = true;
                if (tar_xattr_exclude_count < array_count(tar_xattr_exclude))
                        tar_xattr_exclude[tar_xattr_exclude_count++] = value;
                break;
        case TAR_SPARSE_VERSION:
                options->sparse = true;
                if (string_equals(value, "0.0"))
                        options->sparse_version = 0;
                else if (string_equals(value, "0.1"))
                        options->sparse_version = 1;
                else if (string_equals(value, "1.0"))
                        options->sparse_version = 2;
                else
                        return tar_refuse("Unknown sparse version"), false;
                break;
        case 'b':
                options->blocking = string_digits_max(value, positive_max,
                                                      address_of used);
                if (!used || value[used] || !options->blocking ||
                    options->blocking > (positive)1 << 20)
                {
                        string_format(log_error, "tar: %s: Invalid blocking factor\n", value);
                        tar_status = 2;
                        return tar_usage_hint();
                }
                break;
        case TAR_EXCLUDE:
                if (tar_exclude_count < TAR_EXCLUDE_MAX)
                        tar_exclude[tar_exclude_count++] = value;
                break;
        case TAR_OWNER:
                if (!tar_owner_parse(value, false))
                        return false;
                break;
        case TAR_GROUP:
                if (!tar_owner_parse(value, true))
                        return false;
                break;
        case 'h': tar_dereference = true; break;
        case TAR_TRANSFORM:
                if (!tar_transform_add(value))
                {
                        string_format(log_error, "tar: Invalid transform expression: %s\n", value);
                        tar_status = 2;
                        return false;
                }
                break;
        case TAR_ANCHORED: tar_exclude_anchored = true; break;
        case TAR_NO_ANCHORED: tar_exclude_anchored = false; break;
        case TAR_NULL: tar_files_null = true; break;
        case 'X':
        {
                static byte_store patterns;

                if (!tar_read_list(value, address_of patterns))
                        return false;
                for (positive at = 0; at < patterns.used;)
                {
                        string_address line = (string_address)patterns.bytes + at;
                        string_address stop = string_first_of(line, '\n');
                        positive length = stop ? (positive)(stop - line) : string_length(line);
                        p8 address_to copy = memory_checked(length + 1);

                        if (!copy)
                                return tar_refuse("out of memory"), false;
                        memory_copy(copy, line, length);
                        copy[length] = end;
                        if (length && tar_exclude_count < TAR_EXCLUDE_MAX)
                                tar_exclude[tar_exclude_count++] = (string_address)copy;
                        at += length + 1;
                }
                break;
        }
        case 'T':
        {
                // Read where the option stood, so that its names come in
                // order among the operands: a marked word the parse expands.
                p8 address_to marked = memory_checked(string_length(value) + 2);

                if (!marked)
                        return tar_refuse("out of memory"), false;
                marked[0] = 1;
                memory_copy(marked + 1, value, string_length(value) + 1);
                tar_words[tar_word_count++] = (string_address)marked;
                break;
        }
        case TAR_SAME_OWNER: options->owner = 1; break;
        case TAR_NUMERIC_OWNER: options->numeric = true; break;
        case 'z': options->pack = TAR_PACK_GZIP; break;
        case 'J': options->pack = TAR_PACK_XZ; break;
        case TAR_ZSTD: options->pack = TAR_PACK_ZSTD; break;
        case 'a': options->pack = TAR_PACK_AUTO; break;
        case 'j': return tar_refuse("bzip2 is not this tar"), false;
        case 'Z': return tar_refuse("compress is not this tar"), false;
        case TAR_COMPRESS_PROGRAM:
                return tar_refuse("use-compress-program is not this tar"), false;
        case TAR_STRIP_COMPONENTS:
                options->strip = string_digits_max(value, positive_max,
                                                   address_of used);
                if (!used || value[used])
                        return tar_refuse("invalid number of components"), false;
                break;
        case TAR_HELP:
                string_format(log, "Usage: tar [-ctxzJa] [-f ARCHIVE] [-C DIR] [-p] "
                                   "[--gzip] [--xz] [--zstd] [--strip-components N] "
                                   "[FILE...]\n");
                log_flush();
                options->mode = 'h';
                break;
        case TAR_VERSION:
                string_format(log, "tar from moonwater\n");
                log_flush();
                options->mode = 'h';
                break;
        }

        return true;
}

static bool tar_parse(struct tar_options address_to options)
{
        argument_cursor cursor = {.argc = (positive)program_argument_count(),
                                  .argv = program_argument_list(), .at = 1};
        string_address keys = cursor.argc > 1 && tar_is_cluster(cursor.argv[1])
                                  ? cursor.argv[1] : (string_address)"";
        argument_match match;
        b32 taken;

        memory_fill(options, 0, sizeof(*options));
        tar_word_count = 0;
        tar_words = memory_take((cursor.argc + 1) * sizeof(*tar_words));
        if (!tar_words)
                return tar_refuse("out of memory"), false;
        options->format = TAR_GNU;
        options->sparse_version = 2;
        options->blocking = 20;
        tar_xattr_include_count = 0;
        tar_xattr_exclude_count = 0;
        tar_pax_deleted_count = 0;
        tar_exclude_count = 0;
        tar_transforms_reset();
        tar_owner_forced[0] = tar_owner_forced[1] = false;
        tar_exclude_anchored = false;
        tar_dereference = false;
        tar_files_null = false;
        cursor.at += *keys != end;

        for (;;)
        {
                /* An old-style key takes its value from the words after the
                   keys, in order, not from the letters that follow it. */
                if (*keys)
                {
                        bool valued = argument_option_mode(tar_option_rules, *keys) &
                                      ARGUMENT_REQUIRED;

                        match.letter = *keys++;
                        match.value = valued ? argument_value(address_of cursor, true) : null;
                        taken = valued && !match.value ? ARGUMENT_MISSING : match.letter;
                }
                else
                        taken = argument_option_take(address_of cursor, tar_option_rules,
                                                     false, address_of match);

                if (taken == ARGUMENT_END)
                        break;

                if (taken == ARGUMENT_OPERAND)
                {
                        tar_words[tar_word_count++] = cursor.word;
                        continue;
                }

                if (taken == ARGUMENT_UNKNOWN || taken == ARGUMENT_MISSING)
                {
                        p8 shown[2] = {match.letter, end};

                        tar_status = 2;
                        if (taken == ARGUMENT_UNKNOWN && cursor.long_option)
                                string_format(log_error, "tar: unrecognized option '%s'\n",
                                              cursor.word);
                        else if (taken == ARGUMENT_UNKNOWN)
                                string_format(log_error, "tar: invalid option -- '%s'\n", shown);
                        else if (cursor.long_option)
                                string_format(log_error, "tar: option '%s' requires an argument\n",
                                              cursor.word);
                        else
                                string_format(log_error, "tar: option requires an argument -- '%s'\n",
                                              shown);
                        return false;
                }

                /* A valueless long option ignores a value attached to it
                   (ARGUMENT_UNEXPECTED), as it always has here. */
                if (!tar_take(options, match.letter, match.value))
                        return false;

                if (options->mode == 'h')
                        return true;
        }

        {
                /* -T's names, in place among the operands. */
                bool any = false;

                for (positive at = 0; at < tar_word_count; at++)
                        any |= tar_words[at][0] == 1;

                if (any)
                {
                        static byte_store list;
                        positive room = tar_word_count + 1;
                        string_address address_to grown = memory_take(room * sizeof(*grown));
                        positive have = 0;

                        for (positive at = 0; grown && at < tar_word_count; at++)
                        {
                                if (tar_words[at][0] != 1)
                                {
                                        if (have + 1 >= room)
                                                break;
                                        grown[have++] = tar_words[at];
                                        continue;
                                }
                                if (!tar_read_list(tar_words[at] + 1, address_of list))
                                        return false;

                                p8 separator = tar_files_null ? 0 : '\n';

                                for (positive from = 0; from < list.used;)
                                {
                                        string_address name = (string_address)list.bytes + from;
                                        positive stop = 0;

                                        while (from + stop < list.used && list.bytes[from + stop] != separator)
                                                stop++;
                                        from += stop + 1;
                                        if (!stop)
                                                continue;

                                        p8 address_to copy = memory_checked(stop + 1);

                                        if (!copy)
                                                return tar_refuse("out of memory"), false;
                                        memory_copy(copy, name, stop);
                                        copy[stop] = end;
                                        if (have + 2 >= room)
                                        {
                                                positive more = room * 2;
                                                string_address address_to bigger = memory_take(more * sizeof(*bigger));

                                                if (!bigger)
                                                        return tar_refuse("out of memory"), false;
                                                memory_copy(bigger, grown, have * sizeof(*grown));
                                                grown = bigger;
                                                room = more;
                                        }
                                        grown[have++] = (string_address)copy;
                                }
                        }
                        if (!grown)
                                return tar_refuse("out of memory"), false;
                        tar_words = grown;
                        tar_word_count = have;
                }
        }

        options->first = 0;
        if (options->short_o)
        {
                if (options->mode == TAR_CREATE && !options->format_set)
                        options->format = TAR_V7;
                else
                        options->owner = -1;
        }
        tar_opt_xattrs = options->xattrs;
        tar_opt_acls = options->acls;
        if (options->selinux)
                string_format(log_error, "tar: SELinux support is not available\n");
        if (options->mode == TAR_CREATE && (options->xattrs || options->acls || options->selinux))
        {
                if (!options->format_set)
                        options->format = TAR_POSIX;
                else if (options->format != TAR_POSIX)
                {
                        string_format(log_error, "tar: %s can be used only on POSIX archives\n",
                                      options->acls ? "--acls"
                                      : options->selinux ? "--selinux" : "--xattrs");
                        return tar_usage_hint();
                }
        }
        if (options->sparse && options->mode == TAR_CREATE &&
            (options->format == TAR_USTAR || options->format == TAR_V7))
        {
                tar_refuse("GNU features wanted on incompatible archive format");
                return tar_usage_hint();
        }
        if (options->pax_option && !options->format_set)
                options->format = TAR_POSIX;
        if (options->pax_option)
        {
                if (options->format != TAR_POSIX)
                {
                        tar_refuse("--pax-option can be used only on POSIX archives");
                        return tar_usage_hint();
                }
                tar_pax_deleted_count = 0;
                for (string_address at = options->pax_option; *at;)
                {
                        string_address comma = string_first_of(at, ',');
                        positive length = comma ? (positive)(comma - at)
                                                : string_length(at);

                        if (length > 7 && !memory_compare(at, "delete=", 7) &&
                            tar_pax_deleted_count < array_count(tar_pax_deleted))
                        {
                                p8 address_to kept = memory_checked(length - 6);

                                if (kept)
                                {
                                        memory_copy(kept, at + 7, length - 7);
                                        kept[length - 7] = end;
                                        tar_pax_deleted[tar_pax_deleted_count++] =
                                            (string_address)kept;
                                }
                        }
                        at += length + (comma != null);
                }
        }
        if (!options->mode)
        {
                tar_refuse("you must specify one of the '-c', '-t', or '-x' options");
                return string_report(log_error, false, "Try 'tar --help' for more information.\n");
        }

        return true;
}

static b32 file_tar(void)
{
        struct tar_options options;

        tar_status = 0;
        if (!tar_parse(address_of options))
                return tar_status ? tar_status : 2;

        if (options.mode == 'h')
                return 0;

        tar_fatal_exit = false;
        b32 status = options.mode == TAR_CREATE || options.mode == 'r' || options.mode == 'u'
                         ? tar_write_archive(address_of options)
                         : tar_read_archive(address_of options);

        if (status == 2)
                string_format(log_error, tar_fatal_exit
                                             ? "tar: Error is not recoverable: exiting now\n"
                                             : "tar: Exiting with failure status due to previous errors\n");
        log_flush();
        return status;
}

/* Bowl bootstraps are network-fetched package roots, not system backups.
   Keeping device nodes or FIFOs would turn an archive member into a host
   kernel capability or a blocking endpoint. */
static b32 file_tar_without_special(void)
{
        bool before = tar_extract_special;
        b32 status;

        tar_extract_special = false;
        status = file_tar();
        tar_extract_special = before;
        return status;
}

#endif /* TAR_PARSE_ONLY */
