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

static bool tar_field_value(p8 address_to field, positive width,
                            p64 address_to value)
{
        p8 digits[32];
        positive at = 0;
        positive keep;
        string_address cursor;

        if (width && (field[0] & 0x80))
                return tar_base256(field, width, value);

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

static fn tar_field_put_octal(p8 address_to field, positive width, p64 value)
{
        p8 digits[32];
        positive length;

        memory_fill(field, '0', width);
        if (!width)
                return;

        field[width - 1] = end;
        length = positive_into_base(digits, (positive)value, 8, false);
        if (length >= width)
                length = width - 1;

        memory_copy(field + (width - 1 - length), digits, length);
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

static fn tar_pax_clear(tar_pax_state address_to state)
{
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
        Where a name too long for one field splits.

        The piece after the split is the member's own last component, so a
        directory member's trailing slash is not a split point: it belongs to
        that last piece.  Without this a long directory name split at its own
        trailing slash and left the leaf field empty.
*/
static string_address tar_split_at(string_address name, positive length)
{
        positive at = length;

        if (at && name[at - 1] == '/')
                at--;
        while (at)
                if (name[--at] == '/')
                        return name + at;
        return null;
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
        bool verbose;
        bool absolute;
        bipolar permissions;
        bipolar owner;
        bool touch;
        bool numeric;
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
} tar_member_meta;

typedef struct
{
        positive path_at;
        positive path_hash;
        positive depth;
        positive mode;
        file_facts facts;
        tar_member_meta meta;
} tar_directory_mode;

static tar_directory_mode address_to tar_directories;
static positive tar_directory_count;
static positive tar_directory_room;
static p8 address_to tar_directory_paths;
static positive tar_directory_paths_used;
static positive tar_directory_paths_room;
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
        positive path_at;
        positive path_hash;
        file_facts facts;
        tar_identity_key parent;
        bool pending;
} tar_materialized_file;

static tar_materialized_file address_to tar_materialized;
static positive tar_materialized_count;
static positive tar_materialized_room;
static p8 address_to tar_materialized_paths;
static positive tar_materialized_paths_used;
static positive tar_materialized_paths_room;
static positive address_to tar_materialized_index;
static positive tar_materialized_index_slots;
static positive tar_materialized_index_room;

/*
        GNU default blocking is twenty 512-byte blocks (10 KiB). One 64 KiB
        record is fewer writes on a file of many small members, and copy_file
        range still takes a member that fills a record on its own. The array
        is BSS: it is not resident until the first archive byte moves.
*/
#define TAR_RECORD (TAR_BLOCK * 128)
#define TAR_TRAILING_LIMIT (TAR_RECORD * 16)
#define TAR_ADVISE_SEQUENTIAL 2

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
#define TAR_SEEN_CAP 64

typedef struct
{
        p64 inode;
        p64 device;
        p32 name_at;
} tar_seen_file;

static tar_seen_file tar_seen[TAR_SEEN_CAP];
static p8 tar_seen_names[TAR_SEEN_CAP * TAR_NAME];
static positive tar_seen_used;
static positive tar_seen_fill;

/* A member's name as GNU tar quotes it: C's escapes for the controls it
   has letters for, three octal digits for any other control or byte past
   ASCII, and the backslash doubled, so no name can drive the terminal. */
static fn tar_quoted(writer output, string_address name)
{
        for (; string_get(name); name++)
        {
                p8 byte = (p8)string_get(name);
                p8 escaped[4] = {'\\', 0, 0, 0};

                if (byte == '\\')
                        output("\\\\", 2);
                else if (byte >= 7 && byte <= 13)
                {
                        escaped[1] = "abtnvfr"[byte - 7];
                        output(escaped, 2);
                }
                else if (byte < ' ' || byte >= 127)
                {
                        escaped[1] = (p8)('0' + (byte >> 6));
                        escaped[2] = (p8)('0' + ((byte >> 3) & 7));
                        escaped[3] = (p8)('0' + (byte & 7));
                        output(escaped, 4);
                }
                else
                        output(address_of byte, 1);
        }
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

        string_format(log, "%s %s/%s ", (string_address)letters,
                      (string_address)owner, (string_address)grouped);
        for (positive pad = widths; pad < tar_listing_width; pad++)
                log(" ", 1);
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
                string_format(log, "%s %s ", (string_address)amount, (string_address)when);
        }
        tar_quoted(log, name);
        if (type == '1' || type == '2')
        {
                log(type == '1' ? " link to " : " -> ", type == '1' ? 9 : 4);
                tar_quoted(log, link);
        }
        log("\n", 1);
}

static fn tar_fail(string_address what, bipolar failed)
{
        string_format(log_error, "tar: %w: %s\n", writer_terminal_name, what, file_reason(failed));
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

/* Keep this table below one-half full.  Stored indexes survive growth of both
   the record array and path arena, and the spelling comparison remains the
   proof after the hash rejects unlike paths. */
static bool tar_materialized_index_prepare(positive wanted)
{
        if (tar_materialized_index_slots &&
            wanted <= tar_materialized_index_slots / 2)
                return true;

        positive larger = tar_materialized_index_slots
                              ? tar_materialized_index_slots : 64;
        while (wanted > larger / 2)
        {
                if (larger > positive_max / 2)
                        return false;
                larger *= 2;
        }
        if (!shell_array_room(tar_materialized_index,
                              tar_materialized_index_room, larger))
                return false;

        memory_fill(tar_materialized_index, 0,
                    larger * sizeof(tar_materialized_index[0]));
        for (positive at = 0; at < tar_materialized_count; at++)
        {
                positive slot =
                    tar_materialized[at].path_hash & (larger - 1);
                while (tar_materialized_index[slot])
                        slot = (slot + 1) & (larger - 1);
                tar_materialized_index[slot] = at + 1;
        }
        tar_materialized_index_slots = larger;
        return true;
}

static tar_materialized_file address_to tar_materialized_find_hashed(
    string_address path, positive hash)
{
        if (!tar_materialized_index_slots)
                return null;

        positive slot = hash & (tar_materialized_index_slots - 1);
        while (tar_materialized_index[slot])
        {
                tar_materialized_file address_to kept =
                    tar_materialized + tar_materialized_index[slot] - 1;
                if (kept->path_hash == hash &&
                    string_equals(tar_materialized_paths + kept->path_at,
                                  path))
                        return kept;
                slot = (slot + 1) & (tar_materialized_index_slots - 1);
        }
        return null;
}

static tar_materialized_file address_to tar_materialized_find(
    string_address path)
{
        positive2 named = string_hash_33_length(path);
        return tar_materialized_find_hashed(path, named.x);
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
        tar_materialized_file address_to kept =
            tar_materialized_find_hashed(path, named.x);
        if (kept)
        {
                kept->facts = *facts;
                kept->pending = parent != null;
                if (parent)
                        kept->parent = *parent;
                return 0;
        }

        if (!tar_materialized_index_prepare(tar_materialized_count + 1))
                return -ERROR_NO_MEMORY;

        positive length = named.y + 1;
        if (named.y == positive_max ||
            length > positive_max - tar_materialized_paths_used ||
            !shell_array_room(tar_materialized, tar_materialized_room,
                              tar_materialized_count + 1) ||
            !shell_array_room(tar_materialized_paths,
                              tar_materialized_paths_room,
                              tar_materialized_paths_used + length))
                return -ERROR_NO_MEMORY;

        kept = tar_materialized + tar_materialized_count;
        kept->path_at = tar_materialized_paths_used;
        kept->path_hash = named.x;
        kept->facts = *facts;
        kept->pending = parent != null;
        if (parent)
                kept->parent = *parent;
        memory_copy(tar_materialized_paths + tar_materialized_paths_used,
                    path, length);
        tar_materialized_paths_used += length;
        positive slot = named.x & (tar_materialized_index_slots - 1);
        while (tar_materialized_index[slot])
                slot = (slot + 1) & (tar_materialized_index_slots - 1);
        tar_materialized_index[slot] = ++tar_materialized_count;
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

        for (positive at = 0; at < tar_directory_count; at++)
        {
                tar_directory_mode address_to kept = tar_directories + at;
                if (kept->path_hash == named.x &&
                    string_equals(tar_directory_paths + kept->path_at, path))
                {
                        kept->mode = mode;
                        kept->facts = *facts;
                        kept->meta = *meta;
                        return true;
                }
        }

        positive length = named.y + 1;
        if (length > positive_max - tar_directory_paths_used ||
            !shell_array_room(tar_directories, tar_directory_room,
                              tar_directory_count + 1) ||
            !shell_array_room(tar_directory_paths, tar_directory_paths_room,
                              tar_directory_paths_used + length))
        {
                tar_refuse("out of memory while retaining directory metadata");
                return false;
        }

        tar_directory_mode address_to kept =
            tar_directories + tar_directory_count++;
        kept->path_at = tar_directory_paths_used;
        kept->path_hash = named.x;
        kept->depth = tar_path_depth(path);
        kept->mode = mode;
        kept->facts = *facts;
        kept->meta = *meta;
        memory_copy(tar_directory_paths + tar_directory_paths_used,
                    path, length);
        tar_directory_paths_used += length;
        return true;
}

/* A remembered directory an archive later replaced with something else is
   no longer settled at the end. */
#define TAR_DIRECTORY_GONE ((positive)-1)

static fn tar_directory_forget(string_address path)
{
        positive2 named = string_hash_33_length(path);

        for (positive at = 0; at < tar_directory_count; at++)
                if (tar_directories[at].path_hash == named.x &&
                    string_equals(tar_directory_paths + tar_directories[at].path_at, path))
                        tar_directories[at].mode = TAR_DIRECTORY_GONE;
}

static bipolar tar_directory_index_order(positive left, positive right)
{
        positive one = tar_directories[left].depth;
        positive two = tar_directories[right].depth;

        if (one != two)
                return one > two ? -1 : 1;
        return left > right ? -1 : left < right;
}

static fn tar_directories_finish(void)
{
        if (!tar_directory_count)
                return;

        if (!shell_array_room(tar_directory_order, tar_directory_order_room,
                              tar_directory_count) ||
            !shell_array_room(tar_directory_spare, tar_directory_spare_room,
                              tar_directory_count))
        {
                tar_refuse("out of memory while restoring directory metadata");
                tar_directory_count = 0;
                tar_directory_paths_used = 0;
                return;
        }

        for (positive at = 0; at < tar_directory_count; at++)
                tar_directory_order[at] = at;
        positive address_to order = array_merge_sort(
            tar_directory_order, tar_directory_spare, tar_directory_count,
            tar_directory_index_order);

        for (positive at = 0; at < tar_directory_count; at++)
        {
                tar_directory_mode address_to kept =
                    tar_directories + order[at];
                string_address path = tar_directory_paths + kept->path_at;
                if (kept->mode == TAR_DIRECTORY_GONE)
                        continue;
                bipolar opened = tar_open_beneath_same(
                    path, address_of kept->facts,
                    FILE_READ | O_DIRECTORY);
                bipolar changed = opened < 0
                    ? opened
                    : tar_member_settle(opened, kept->mode,
                                        address_of kept->meta, true);

                if (opened >= 0)
                        system_close(opened);
                if (changed < 0)
                        tar_fail(path, changed);
        }

        tar_directory_count = 0;
        tar_directory_paths_used = 0;
}

static fn tar_reset(void)
{
        tar_have = 0;
        tar_at = 0;
        tar_seen_used = 0;
        tar_seen_fill = 0;
        tar_decoder = null;
        tar_encoder = null;
        tar_output_known = false;
        tar_output_target_known = false;
        tar_output_stage_known = false;
        tar_directory_count = 0;
        tar_directory_paths_used = 0;
        tar_materialized_count = 0;
        tar_materialized_paths_used = 0;
        tar_materialized_index_slots = 0;
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
#if defined(LIBRARY_THREAD_RUNTIME)
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
#else
static fn tar_ring_start(const tar_codec address_to codec)
{
        (void)codec;
}

static bool tar_ring_finish(const tar_codec address_to codec)
{
        return codec->read_end();
}

static bipolar tar_read_bytes(bipolar handle, p8 address_to into, positive n)
{
        return tar_decoder ? tar_decoder->read(into, n)
                           : system_read_retry((positive)handle, into, n);
}

static bipolar tar_view_bytes(bipolar handle, p8 address_to address_to at,
                              positive n, p8 address_to scratch)
{
        address_to at = scratch;
        return tar_read_bytes(handle, scratch, n > TAR_RECORD ? TAR_RECORD : n);
}

static fn tar_view_done(positive n)
{
        (void)n;
}
#endif

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
#define TAR_SPARSE_MAX 256

typedef struct
{
        p64 offset;
        p64 bytes;
} tar_sparse_span;

static tar_sparse_span tar_sparse[TAR_SPARSE_MAX];
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

        if (tar_sparse_used >= TAR_SPARSE_MAX)
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
                return false;
        }

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

static bool tar_put_file(bipolar archive, bipolar in, p64 size)
{
        p64 left = size;

        if (size >= TAR_RECORD && !tar_packed())
        {
                tar_advise(in);
                if (!tar_flush(archive) ||
                    !tar_copy_out(in, archive, size, null))
                        return false;

                return tar_write_padding(archive, size);
        }

        while (left)
        {
                positive room;
                bipolar got;

                if (tar_at == TAR_RECORD && !tar_flush(archive))
                        return false;

                room = TAR_RECORD - tar_at;
                got = system_read_retry((positive)in, tar_record + tar_at,
                                        left > room ? room : (positive)left);
                if (got <= 0)
                        return false;

                tar_at += (positive)got;
                left -= (positive)got;
        }

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

static bool tar_wanted(string_address name, positive first, positive count)
{
        positive at;

        if (first >= count)
                return true;

        for (at = first; at < count; at++)
                if (tar_name_matches(name, program_argument((b32)at)))
                        return true;

        return false;
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

static b32 tar_read_archive(struct tar_options address_to options)
{
        bipolar handle;
        p8 address_to block;
        bool seekable;
        bool listed = false;
        p8 long_name[TAR_PATH];
        p8 long_link[TAR_PATH];
        bool have_long_name = false;
        bool have_long_link = false;
        positive count = (positive)program_argument_count();

        tar_pax_clear(address_of tar_pax_global);
        tar_pax_clear(address_of tar_pax_local);
        tar_sparse_clear();
        tar_reset();
        long_name[0] = end;
        long_link[0] = end;
        tar_pack = options->pack;

        if (!options->archive || string_equals(options->archive, "-"))
                handle = 0;
        else
        {
                handle = system_open_at(AT_FDCWD, options->archive,
                                        FILE_READ | O_CLOEXEC);
                if (handle < 0)
                {
                        tar_fail(options->archive, handle);
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

                if (tar_header_zero(block))
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

                if (!tar_header_ok(block))
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
                                tar_name_line(log, tar_name);
                        tar_skip(handle, tar_padded(size), seekable);
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
                        if (options->verbose)
                                tar_name_line(log_error, shown);
                        tar_extract_member(handle, type, kept, kept_link, size,
                                           mode, major, minor, seekable,
                                           address_of meta);
                }

                tar_pax_clear(address_of tar_pax_local);
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

        if (options->first < count && !listed && !tar_status)
                tar_refuse("the requested members were not in the archive");

        log_flush();
        return tar_status;
}

/*
        Who owns a member, as the reference writes it: the numbers in the
        ustar uid and gid fields and the names beside them, looked up in
        /etc/passwd and /etc/group the way ls -l looks them up. Both went out
        as zero with no names, so an archive made here lost its ownership
        altogether and an extraction that is allowed to restore it -- root's,
        or --same-owner -- gave every member to root.
*/
static fn tar_header_owner(p8 address_to block, p32 user, p32 group)
{
        p8 name[FILE_NAME_MAX];

        tar_field_put(block + 108, 8, user);
        tar_field_put(block + 116, 8, group);

        //      Both fields are 32 bytes holding a terminated name, so 31
        //      bytes of name at most: the bounded copy writes the terminator
        //      past its bound, and a 32-byte name would put it in the first
        //      byte of the field after.
        if (file_user_name(user, name, sizeof(name)))
                string_copy_max_end(block + 265, name, 31);
        if (file_group_name(group, name, sizeof(name)))
                string_copy_max_end(block + 297, name, 31);
}

static fn tar_header_ustar(p8 address_to block, string_address name,
                           p8 type, p64 size, p64 mode, p64 mtime,
                           string_address link, p32 user, p32 group)
{
        p8 prefix[TAR_PREFIX + 1];
        p8 leaf[TAR_NAME + 1];
        positive length = string_length(name);
        string_address slash;

        memory_fill(block, 0, TAR_BLOCK);
        prefix[0] = end;
        leaf[0] = end;

        if (length < TAR_NAME)
                string_copy_max_end(leaf, name, TAR_NAME);
        else
        {
                slash = tar_split_at(name, length);
                if (!slash || (positive)(slash - name) >= TAR_PREFIX ||
                    length - (positive)(slash - name) - 1 >= TAR_NAME)
                {
                        tar_refuse("member name is too long for ustar");
                        return;
                }

                memory_copy(prefix, name, (positive)(slash - name));
                prefix[slash - name] = end;
                string_copy_max_end(leaf, slash + 1, TAR_NAME);
        }

        memory_copy(block, leaf, string_length(leaf));
        if (prefix[0])
                memory_copy(block + 345, prefix, string_length(prefix));

        tar_field_put_octal(block + 100, 8, mode);
        tar_field_put(block + 124, 12, size);
        tar_field_put(block + 136, 12, mtime);
        block[156] = type;
        if (link)
                memory_copy(block + 157, link,
                            min(string_length(link), TAR_NAME - 1));

        memory_copy(block + 257, "ustar", 6);
        block[263] = '0';
        block[264] = '0';
        tar_header_owner(block, user, group);
        tar_header_put_checksum(block);
}

static bool tar_ustar_fits(string_address name)
{
        positive length = string_length(name);
        string_address slash;

        if (length < TAR_NAME)
                return true;
        slash = tar_split_at(name, length);
        return slash && (positive)(slash - name) < TAR_PREFIX &&
               length - (positive)(slash - name) - 1 < TAR_NAME;
}

/* A name or link target ustar cannot hold goes ahead of its header as a
   GNU ././@LongLink member, 'L' for the name and 'K' for the target, the
   way GNU tar writes them; the header then keeps the part that fits. */
static bool tar_put_long(bipolar handle, p8 type, string_address text)
{
        positive length = string_length(text) + 1;

        if (tar_at + TAR_BLOCK > TAR_RECORD && !tar_flush(handle))
                return false;
        tar_header_ustar(tar_record + tar_at, (string_address)"././@LongLink",
                         type, length, 0644, 0, null, 0, 0);
        tar_at += TAR_BLOCK;
        return tar_put(handle, (p8 address_to)text, length) &&
               tar_write_padding(handle, length);
}

static bool tar_put_header(bipolar handle, string_address name, p8 type,
                           p64 size, p64 mode, p64 mtime, string_address link,
                           p32 user, p32 group)
{
        p8 kept[TAR_NAME];

        if (link && string_length(link) >= TAR_NAME &&
            !tar_put_long(handle, 'K', link))
                return false;
        if (!tar_ustar_fits(name))
        {
                if (!tar_put_long(handle, 'L', name))
                        return false;
                string_copy_max_end(kept, name, TAR_NAME - 1);
                name = kept;
        }

        if (tar_at + TAR_BLOCK > TAR_RECORD && !tar_flush(handle))
                return false;

        tar_header_ustar(tar_record + tar_at, name, type, size, mode, mtime,
                         link, user, group);
        if (tar_status == 2)
                return false;

        tar_at += TAR_BLOCK;
        return true;
}

static p64 tar_identity(file_facts address_to facts)
{
        return ((p64)facts->device_major << 32) | facts->device_minor;
}

static string_address tar_seen_name(file_facts address_to facts)
{
        positive at;

        if (facts->hard_links < 2)
                return null;

        for (at = 0; at < tar_seen_used; at++)
                if (tar_seen[at].inode == facts->inode &&
                    tar_seen[at].device == tar_identity(facts))
                        return tar_seen_names + tar_seen[at].name_at;

        return null;
}

static fn tar_seen_store(file_facts address_to facts, string_address member)
{
        positive length;

        if (facts->hard_links < 2 || tar_seen_used >= TAR_SEEN_CAP)
                return;

        length = string_length(member);
        if (length >= TAR_NAME ||
            tar_seen_fill + length + 1 > sizeof(tar_seen_names))
                return;

        tar_seen[tar_seen_used].inode = facts->inode;
        tar_seen[tar_seen_used].device = tar_identity(facts);
        tar_seen[tar_seen_used].name_at = (p32)tar_seen_fill;
        memory_copy(tar_seen_names + tar_seen_fill, member, length + 1);
        tar_seen_fill += length + 1;
        tar_seen_used += 1;
}

static b32 tar_add_named(bipolar archive, bipolar directory,
                         string_address name, string_address member,
                         bool verbose, bool selected, p8 kind);

static b32 tar_add_directory(bipolar archive, bipolar directory,
                             string_address name, string_address member,
                             file_facts address_to facts, bool verbose)
{
        file_walk walk;

        if (!file_walk_open_found_same(address_of walk, directory, name,
                                       facts))
                return tar_fail(member, walk.error), tar_status;

        p8 spelled[TAR_PATH];

        if (!tar_put_header(archive,
                            tar_spell_directory(member, spelled,
                                                sizeof(spelled))
                                ? (string_address)spelled : member,
                            '5', 0, facts->mode & 07777,
                            (p64)facts->modified.seconds, null,
                            facts->owner, facts->group))
        {
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
                if (tar_status == 2)
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
                tar_fail(member, looked);
                return tar_status;
        }

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
                        tar_status = 2;
                return tar_status;
        }

        if (verbose)
        {
                p8 spelled[TAR_PATH];

                tar_name_line(log_error,
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
                        tar_fail(member, got < 0 ? got
                                                 : -ERROR_NAME_TOO_LONG);
                        return tar_status;
                }

                link[got] = end;
                tar_put_header(archive, member, '2', 0, facts.mode & 07777,
                               (p64)facts.modified.seconds, link,
                               facts.owner, facts.group);
                system_close(handle);
                return tar_status;
        }

        prior = tar_seen_name(address_of facts);
        if (prior)
        {
                if (handle >= 0)
                        system_close(handle);
                tar_put_header(archive, member, '1', 0, facts.mode & 07777,
                               (p64)facts.modified.seconds, prior,
                               facts.owner, facts.group);
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
                p8 address_to header;

                if (tar_at + TAR_BLOCK > TAR_RECORD && !tar_flush(archive))
                        return tar_status;

                header = tar_record + tar_at;
                tar_header_ustar(header, member, type, 0, facts.mode & 07777,
                                 (p64)facts.modified.seconds, null,
                                 facts.owner, facts.group);
                if (tar_status == 2)
                        return tar_status;

                if (type == '3' || type == '4')
                {
                        tar_field_put_octal(header + 329, 8, facts.rdev_major);
                        tar_field_put_octal(header + 337, 8, facts.rdev_minor);
                        tar_header_put_checksum(header);
                }

                tar_at += TAR_BLOCK;
                return tar_status;
        }

        if (handle < 0)
                handle = file_open_same(
                    directory, name, address_of facts,
                    FILE_READ | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (handle < 0)
        {
                tar_fail(member, handle);
                return tar_status;
        }

        if (!tar_put_header(archive, member, '0', facts.size,
                            facts.mode & 07777, (p64)facts.modified.seconds,
                            null, facts.owner, facts.group))
        {
                system_close(handle);
                return tar_status;
        }

        if (!tar_put_file(archive, handle, (p64)facts.size))
                tar_fail(member, -ERROR_INPUT_OUTPUT);
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
        positive count = (positive)program_argument_count();
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

        tar_reset();
        output_stage.directory = -1;
        output_stage.handle = -1;
        if (!options->archive || string_equals(options->archive, "-"))
                handle = 1;
        else
        {
                handle = file_staged_name_open(
                    address_of output_stage, options->archive,
                    0666 & ~file_umask(), FILE_STAGED_STREAM_SPECIAL);
                if (handle < 0)
                {
                        tar_fail(options->archive, handle);
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

        for (at = options->first; at < count && tar_status != 2; at++)
                tar_add_path(handle, program_argument((b32)at),
                             options->verbose);

        memory_fill(tar_block, 0, TAR_BLOCK);
        if (tar_status != 2)
        {
                tar_write_block(handle, tar_block);
                tar_write_block(handle, tar_block);
        }

        tar_flush(handle);
        if (tar_encoder)
                tar_codec_end_write();
        if (managed_output)
        {
                bipolar finished = file_staged_name_finish(
                    address_of output_stage, tar_status != 2, 0);
                if (finished < 0 && tar_status != 2)
                        tar_fail(options->archive, finished);
        }
        else if (handle > 2)
        {
                bipolar closed = system_close(handle);
                if (closed < 0 && tar_status != 2)
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
};

static const argument_option tar_option_rules[] = {
    {"extract", 'x'},
    {"list", 't'},
    {"create", 'c'},
    {"file", 'f', ARGUMENT_REQUIRED},
    {"directory", 'C', ARGUMENT_REQUIRED},
    {"strip-components", TAR_STRIP_COMPONENTS, ARGUMENT_REQUIRED | ARGUMENT_LONG_ONLY},
    {"verbose", 'v'},
    {"absolute-names", 'P'},
    {"preserve-permissions", 'p'},
    {"same-permissions", 'p'},
    {"no-same-permissions", TAR_NO_SAME_PERMISSIONS, ARGUMENT_LONG_ONLY},
    {"touch", 'm'},
    {"no-same-owner", 'o'},
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
        case 'x': case 't': case 'c': options->mode = letter; break;
        case 'f': options->archive = value; break;
        case 'C': options->directory = value; break;
        case 'v': options->verbose = true; break;
        case 'P': options->absolute = true; break;
        case 'p': options->permissions = 1; break;
        case TAR_NO_SAME_PERMISSIONS: options->permissions = -1; break;
        case 'm': options->touch = true; break;
        case 'o': options->owner = -1; break;
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
                        cursor.at--;
                        break;
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

        options->first = cursor.at;
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

        if (options.mode == TAR_CREATE)
                return tar_write_archive(address_of options);

        return tar_read_archive(address_of options);
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
