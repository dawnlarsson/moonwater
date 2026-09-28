/*
        Every sum the shell answers for: md5sum, sha1sum, sha224sum,
        sha256sum, sha384sum, sha512sum, b2sum, and both halves of cksum.

        The two engines stay apart and the two files do not. A four-byte
        serial checksum wants its own folds and a block core wants its own
        buffering, which is why the CRC below is not routed through the
        digest streamer; but they are one subject, they were always compiled
        one after the other into the same unit, and a reader looking for how
        this shell sums a file should find all of it here.

        Every digest is the library's: md5_blocks through blake2b_blocks in
        lib.c, streamed by digest_open, digest_write and digest_close in
        lib.util.c. A file is read into the shared transfer block and
        its whole blocks are hashed where they lie.

        These used to drive the kernel's AF_ALG hash sockets and carried no
        software digest on purpose. Moonwater's kernels do not enable AF_ALG,
        so the sums had no answer on the system they ship on, and the socket
        round trip cost more than the cores it stood in for: 256 files of
        4 MiB took 0.51 s, all of it system time.
*/

typedef struct
{
        string_address command;
        string_address type;
        string_address label;
        p8 algorithm;
        p8 bytes;
        bool variable_length;
} checksum_algorithm;

/* SHA-3 and SM3 have their cores below, not in the library's streamer. */
#define CHECKSUM_DIGEST_SHA3 16
#define CHECKSUM_DIGEST_SM3 17

static const checksum_algorithm checksum_algorithms[] = {
    {(string_address) "b2sum", (string_address) "blake2b",
     (string_address) "BLAKE2b", DIGEST_BLAKE2B, 64, true},
    {(string_address) "md5sum", (string_address) "md5",
     (string_address) "MD5", DIGEST_MD5, 16, false},
    {(string_address) "sha1sum", (string_address) "sha1",
     (string_address) "SHA1", DIGEST_SHA1, 20, false},
    {(string_address) "sha224sum", (string_address) "sha224",
     (string_address) "SHA224", DIGEST_SHA224, 28, false},
    {(string_address) "sha256sum", (string_address) "sha256",
     (string_address) "SHA256", DIGEST_SHA256, 32, false},
    {(string_address) "sha384sum", (string_address) "sha384",
     (string_address) "SHA384", DIGEST_SHA384, 48, false},
    {(string_address) "sha512sum", (string_address) "sha512",
     (string_address) "SHA512", DIGEST_SHA512, 64, false},
    //  Only cksum -a names these; no command is spelled like the empty one.
    {(string_address) "", (string_address) "sha3-224",
     (string_address) "SHA3-224", CHECKSUM_DIGEST_SHA3, 28, false},
    {(string_address) "", (string_address) "sha3-256",
     (string_address) "SHA3-256", CHECKSUM_DIGEST_SHA3, 32, false},
    {(string_address) "", (string_address) "sha3-384",
     (string_address) "SHA3-384", CHECKSUM_DIGEST_SHA3, 48, false},
    {(string_address) "", (string_address) "sha3-512",
     (string_address) "SHA3-512", CHECKSUM_DIGEST_SHA3, 64, false},
    {(string_address) "", (string_address) "sm3",
     (string_address) "SM3", CHECKSUM_DIGEST_SM3, 32, false},
};

#define CHECKSUM_INTERRUPTED (-4)
#define CHECKSUM_SHA2_FIRST 3
#define CHECKSUM_SHA2_LAST 6
#define CHECKSUM_SHA3_FIRST 7
#define CHECKSUM_SHA3_LAST 10

typedef struct { p8 style, mode, verify; } checksum_selection;
_Static_assert(sizeof(checksum_selection) <= 16, "selection record fits its mask");

/* BLAKE2 alone exposes length; every other sum uses the common tail. */
static const argument_option checksum_options[] = {
    {"length", 'l', ARGUMENT_REQUIRED},
    {"binary", 'b', 0, ARGUMENT_SELECT(checksum_selection, mode)},
    {"check", 'c'},
    {"ignore-missing", 'i', ARGUMENT_LONG_ONLY},
    {"quiet", 'q', ARGUMENT_LONG_ONLY, ARGUMENT_SELECT(checksum_selection, verify)},
    {"status", 's', ARGUMENT_LONG_ONLY, ARGUMENT_SELECT(checksum_selection, verify)},
    {"strict", 'S', ARGUMENT_LONG_ONLY},
    {"tag", 'T', ARGUMENT_LONG_ONLY},
    {"text", 't', 0, ARGUMENT_SELECT(checksum_selection, mode)},
    {"warn", 'w', 0, ARGUMENT_SELECT(checksum_selection, verify)},
    {"zero", 'z'},
    {null},
};

// Which of -b/-t was given last, and whether either was: GNU refuses --tag
// with an explicit --text and both with --check.
// The last of --status, --warn and --quiet wins, as in GNU.
// Output shapes: NUL-terminated unescaped lines, base64 or raw digests.
static checksum_selection checksum_selected;
static bool checksum_zero;
static bool checksum_base64;
static bool checksum_raw;
/* The digest length -l asked BLAKE2b for, in bytes; the algorithm's own
   when -l was not given or was zero. */
static positive checksum_length;
/* cksum -a sha2 --check without -l: each record's own width picks which of
   the four SHA-2 digests reads it. */
static positive checksum_sha2_family;
/* Which untagged record shape this invocation's check run settled on:
   -1 before the first, 0 the standard "digest  name", 1 the reversed BSD
   "digest name". */
static b32 checksum_bsd_reversed;
/* cksum --check reads a digest written in base64 as well as in hex, as
   coreutils' cksum does and its md5sum and the rest do not. */
static bool checksum_base64_read;
/* Whose name a verification complains in, and the label it calls a line it
   could not read. cksum --check borrows this walk under its own name. */
static string_address checksum_program;
static string_address checksum_check_label;
/* cksum collects its operands into the shared file list rather than leaving
   them behind the options, so the manifests are named from there. */
static bool checksum_manifest_files;

static fn checksum_modes_reset()
{
        checksum_selected = (checksum_selection){};
        checksum_program = null;
        checksum_check_label = null;
        checksum_manifest_files = false;

        checksum_zero = false;
        checksum_base64 = false;
        checksum_raw = false;
        checksum_length = 0;
        checksum_sha2_family = 0;
        checksum_bsd_reversed = -1;
        checksum_base64_read = false;
}

static positive checksum_base64_length(positive bytes)
{
        return (bytes + 2) / 3 * 4;
}

/* A base64 digest of exactly bytes bytes, padding and all, into expected. */
/* A base64 digest whose last character carries bits past the digest is
   still a digest to coreutils, which compares spellings: it is read, and it
   can never match. */
static bool checksum_base64_loose;

static bool checksum_base64_decode(string_address text, positive length,
                                   positive bytes, p8 address_to expected)
{
        static const p8 alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        positive padding = (3 - bytes % 3) % 3;
        positive made = 0;
        positive bits = 0;
        positive held = 0;

        if (length != checksum_base64_length(bytes))
                return false;

        for (positive at = 0; at < length; at++)
        {
                if (at >= length - padding)
                {
                        if (text[at] != '=')
                                return false;
                        continue;
                }

                string_address place = string_first_of((string_address)alphabet, text[at]);

                if (!place || !text[at])
                        return false;
                held = held << 6 | (positive)(place - (string_address)alphabet);
                bits += 6;
                if (bits >= 8)
                {
                        bits -= 8;
                        if (made < bytes)
                                expected[made++] = (p8)(held >> bits);
                        held &= ((positive)1 << bits) - 1;
                }
        }

        checksum_base64_loose = held != 0;
        return made == bytes;
}

/* The digest of a record, hex or (for cksum) base64, at its width. */
static bool checksum_digest_read(string_address text, positive length,
                                 positive bytes, p8 address_to expected)
{
        if (length != bytes * 2)
                return checksum_base64_read &&
                       checksum_base64_decode(text, length, bytes, expected);

        for (positive i = 0; i < bytes; i++)
        {
                positive high = digit_known(text[i * 2], 16);
                positive low = digit_known(text[i * 2 + 1], 16);

                if (high >= 16 || low >= 16)
                        return false;
                expected[i] = (p8)((high << 4) | low);
        }
        return true;
}

/* coreutils' complaint about an option out of place, with its usage hint. */
static b32 checksum_usage_error(string_address command, string_address message)
{
        text_flush();
        return text_done(string_report(writer_stderr, 1,
                                       "%s: %s\nTry '%s --help' for more information.\n",
                                       command, message, command));
}

/* coreutils' refusals of options that belong to the other mode, in its order
   and words; zero when none applies. */
static b32 checksum_refuse_modes(string_address command, bool checking,
                                 bool tagged, positive flags)
{
        string_address verifying = (flags & FILE_FLAG('i')) ? "ignore-missing"
            : checksum_selected.verify == 's' ? "status"
            : checksum_selected.verify == 'w' ? "warn"
            : checksum_selected.verify == 'q' ? "quiet"
            : (flags & FILE_FLAG('S')) ? "strict" : null;

        if (checksum_zero && checking)
                return checksum_usage_error(command, "the --zero option is not supported when verifying checksums");
        if (tagged && checking)
                return checksum_usage_error(command, "the --tag option is meaningless when verifying checksums");
        if (checking && checksum_selected.mode)
                return checksum_usage_error(command, "the --binary and --text options are meaningless when verifying checksums");
        if (checking || !verifying)
                return 0;
        text_flush();
        return text_done(string_report(writer_stderr, 1,
            "%s: the --%s option is meaningful only when verifying checksums\n"
            "Try '%s --help' for more information.\n", command, verifying, command));
}

/*
        A length in bits as coreutils reads one, through strtoimax: blanks,
        a sign, then decimal digits and nothing after them. A number past
        the largest is quietly the largest, which the width checks then
        refuse; a negative one is out of range and says so. The answer is
        the tail the complaint takes, null when the length is good.
*/
static string_address checksum_decimal(string_address text, positive address_to value)
{
        positive total = 0;
        bool negative = false, digits = false;

        if (!text)
                return (string_address) "";

        while (string_get(text) == ' ' ||
               ((p8)string_get(text) >= '\t' && (p8)string_get(text) <= '\r'))
                text++;
        if (string_get(text) == '+' || string_get(text) == '-')
                negative = string_get(text++) == '-';

        for (; string_get(text); text++)
        {
                positive digit = (positive)(p8)string_get(text) - '0';

                if (digit > 9)
                        return (string_address) "";
                digits = true;
                total = total > ((positive)bipolar_max - digit) / 10
                            ? (positive)bipolar_max
                            : total * 10 + digit;
        }

        if (!digits)
                return (string_address) "";
        if (negative && total)
                return (string_address) ": Value too large for defined data type";

        address_to value = total;
        return null;
}

/* -l for BLAKE2b, in coreutils' order and words: a number, at most 512 bits,
   a whole number of bytes. Zero is the full length. */
static b32 checksum_blake2b_length(string_address program, string_address text,
                                   positive address_to bytes)
{
        positive bits;
        string_address why = checksum_decimal(text, address_of bits);

        if (why)
        {
                text_flush();
                return text_done(string_report(writer_stderr, 1,
                    "%s: invalid length: '%w'%s\n", program, writer_terminal_quoted_name, text, why));
        }
        if (bits > 512)
        {
                text_flush();
                return text_done(string_report(writer_stderr, 1,
                    "%s: invalid length: '%w'\n"
                    "%s: maximum digest length for 'BLAKE2b' is 512 bits\n",
                    program, writer_terminal_quoted_name, text, program));
        }
        if (bits % 8)
        {
                text_flush();
                return text_done(string_report(writer_stderr, 1,
                    "%s: invalid length: '%w'\n%s: length is not a multiple of 8\n",
                    program, writer_terminal_quoted_name, text, program));
        }

        address_to bytes = bits ? bits / 8 : 64;
        return 0;
}

static fn checksum_base64_put(p8 address_to digest, positive length)
{
        encoding_output output = {0};

        encoding_groups(encoding_codecs + ENCODING_BASE64, address_of output,
                        digest, length);
}

static const checksum_algorithm address_to checksum_algorithm_find(
    string_address name, bool type)
{
        for (positive i = 0; i < array_count(checksum_algorithms); i++)
                if (string_equals(name, type ? checksum_algorithms[i].type
                                             : checksum_algorithms[i].command))
                        return checksum_algorithms + i;

        return null;
}

/* The digest length an algorithm answers with under the current -l. */
static positive checksum_bytes(const checksum_algorithm address_to algorithm)
{
        return algorithm->variable_length && checksum_length ? checksum_length
                                                             : algorithm->bytes;
}

/* A missing name and "-" are standard input; an interrupted open is retried. */
static bipolar checksum_open(string_address path, bool address_to standard)
{
        bipolar input = 0;

        address_to standard = !path || (string_is(path, '-') && !string_get(path + 1));
        if (!address_to standard)
                do
                        input = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
                while (input == CHECKSUM_INTERRUPTED);
        return input;
}

/* One file's digest: read into block, a FILE_TRANSFER_SIZE buffer the
   calling thread owns, and hashed where it lies. */
/*
        SHA-3 and SM3, which only cksum -a asks for and nothing else in the
        system hashes with, so they stay here rather than beside the library's
        cores: Keccak-f[1600] under the SHA-3 padding (rate 200 bytes less
        twice the digest), and SM3's Merkle-Damgard rounds over SHA-256's
        padding. Plain C; neither has a floor to hold.
*/
#define checksum_rotl32(x, n) (((x) << (n)) | ((x) >> ((32 - (n)) & 31)))
#define checksum_rotl64(x, n) (((x) << (n)) | ((x) >> ((64 - (n)) & 63)))

static fn checksum_keccak(p64 address_to a)
{
        static const p64 round[24] = {
            0x0000000000000001ull, 0x0000000000008082ull, 0x800000000000808aull,
            0x8000000080008000ull, 0x000000000000808bull, 0x0000000080000001ull,
            0x8000000080008081ull, 0x8000000000008009ull, 0x000000000000008aull,
            0x0000000000000088ull, 0x0000000080008009ull, 0x000000008000000aull,
            0x000000008000808bull, 0x800000000000008bull, 0x8000000000008089ull,
            0x8000000000008003ull, 0x8000000000008002ull, 0x8000000000000080ull,
            0x000000000000800aull, 0x800000008000000aull, 0x8000000080008081ull,
            0x8000000000008080ull, 0x0000000080000001ull, 0x8000000080008008ull,
        };
        static const p8 rho[25] = {
            0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43,
            25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14,
        };

        for (positive r = 0; r < 24; r++)
        {
                p64 c[5], b[25];

                for (positive x = 0; x < 5; x++)
                        c[x] = a[x] ^ a[x + 5] ^ a[x + 10] ^ a[x + 15] ^ a[x + 20];
                for (positive x = 0; x < 5; x++)
                {
                        p64 d = c[(x + 4) % 5] ^ checksum_rotl64(c[(x + 1) % 5], 1);

                        for (positive y = 0; y < 25; y += 5)
                                a[y + x] ^= d;
                }
                // rho and pi: lane (x, y) moves to (y, 2x + 3y).
                for (positive x = 0; x < 5; x++)
                        for (positive y = 0; y < 5; y++)
                                b[y + 5 * ((2 * x + 3 * y) % 5)] =
                                    checksum_rotl64(a[x + 5 * y], rho[x + 5 * y]);
                for (positive y = 0; y < 25; y += 5)
                        for (positive x = 0; x < 5; x++)
                                a[y + x] = b[y + x] ^
                                           (~b[y + (x + 1) % 5] & b[y + (x + 2) % 5]);
                a[0] ^= round[r];
        }
}

static fn checksum_sm3_block(p32 address_to v, const p8 address_to data)
{
        p32 w[68];

        for (positive j = 0; j < 16; j++)
                w[j] = (p32)data[4 * j] << 24 | (p32)data[4 * j + 1] << 16 |
                       (p32)data[4 * j + 2] << 8 | data[4 * j + 3];
        for (positive j = 16; j < 68; j++)
        {
                p32 x = w[j - 16] ^ w[j - 9] ^ checksum_rotl32(w[j - 3], 15);

                w[j] = (x ^ checksum_rotl32(x, 15) ^ checksum_rotl32(x, 23)) ^
                       checksum_rotl32(w[j - 13], 7) ^ w[j - 6];
        }

        p32 a = v[0], b = v[1], c = v[2], d = v[3];
        p32 e = v[4], f = v[5], g = v[6], h = v[7];

        for (positive j = 0; j < 64; j++)
        {
                p32 t = j < 16 ? 0x79cc4519u : 0x7a879d8au;
                p32 a12 = checksum_rotl32(a, 12);
                p32 ss1 = checksum_rotl32(a12 + e + checksum_rotl32(t, j % 32), 7);
                p32 ss2 = ss1 ^ a12;
                p32 ff = j < 16 ? a ^ b ^ c : (a & b) | (a & c) | (b & c);
                p32 gg = j < 16 ? e ^ f ^ g : (e & f) | (~e & g);
                p32 tt1 = ff + d + ss2 + (w[j] ^ w[j + 4]);
                p32 tt2 = gg + h + ss1 + w[j];

                d = c;
                c = checksum_rotl32(b, 9);
                b = a;
                a = tt1;
                h = g;
                g = checksum_rotl32(f, 19);
                f = e;
                e = tt2 ^ checksum_rotl32(tt2, 9) ^ checksum_rotl32(tt2, 17);
        }

        v[0] ^= a; v[1] ^= b; v[2] ^= c; v[3] ^= d;
        v[4] ^= e; v[5] ^= f; v[6] ^= g; v[7] ^= h;
}

typedef struct
{
        p64 lanes[25];
        p32 words[8];
        p8 block[144];
        positive used, rate, size;
        p64 bytes;
        bool sm3;
} checksum_sponge;

static fn checksum_sponge_block(checksum_sponge address_to s, const p8 address_to data)
{
        if (s->sm3)
        {
                checksum_sm3_block(s->words, data);
                return;
        }
        for (positive lane = 0; lane < s->rate / 8; lane++)
        {
                p64 value = 0;

                for (positive at = 0; at < 8; at++)
                        value |= (p64)data[lane * 8 + at] << (8 * at);
                s->lanes[lane] ^= value;
        }
        checksum_keccak(s->lanes);
}

static fn checksum_sponge_open(checksum_sponge address_to s, bool sm3,
                               positive size)
{
        static const p32 initial[8] = {
            0x7380166f, 0x4914b2b9, 0x172442d7, 0xda8a0600,
            0xa96f30bc, 0x163138aa, 0xe38dee4d, 0xb0fb0e4e,
        };

        memory_fill(s, 0, sizeof(address_to s));
        s->sm3 = sm3;
        s->size = size;
        s->rate = sm3 ? 64 : 200 - 2 * size;
        memory_copy(s->words, initial, sizeof(initial));
}

static fn checksum_sponge_write(checksum_sponge address_to s,
                                const p8 address_to data, positive length)
{
        s->bytes += length;
        while (length)
        {
                if (!s->used && length >= s->rate)
                {
                        checksum_sponge_block(s, data);
                        data += s->rate;
                        length -= s->rate;
                        continue;
                }

                positive take = min(length, s->rate - s->used);

                memory_copy(s->block + s->used, data, take);
                s->used += take;
                data += take;
                length -= take;
                if (s->used == s->rate)
                {
                        checksum_sponge_block(s, s->block);
                        s->used = 0;
                }
        }
}

static fn checksum_sponge_close(checksum_sponge address_to s, p8 address_to out)
{
        memory_fill(s->block + s->used, 0, s->rate - s->used);
        if (!s->sm3)
        {
                s->block[s->used] = 0x06;
                s->block[s->rate - 1] |= 0x80;
                checksum_sponge_block(s, s->block);
                for (positive at = 0; at < s->size; at++)
                        out[at] = (p8)(s->lanes[at / 8] >> (8 * (at % 8)));
                return;
        }

        p64 bits = s->bytes << 3;

        s->block[s->used] = 0x80;
        if (s->used + 1 > 56)
        {
                checksum_sponge_block(s, s->block);
                memory_fill(s->block, 0, 64);
        }
        for (positive at = 0; at < 8; at++)
                s->block[63 - at] = (p8)(bits >> (8 * at));
        checksum_sponge_block(s, s->block);
        for (positive at = 0; at < 32; at++)
                out[at] = (p8)(s->words[at / 4] >> (24 - 8 * (at % 4)));
}

static bipolar checksum_hash_path(const checksum_algorithm address_to algorithm,
                                   positive bytes, string_address path,
                                   p8 address_to digest, p8 address_to block)
{
        bool standard;
        bipolar input = checksum_open(path, address_of standard);

        if (input < 0)
                return input;

        bipolar got;

        if (algorithm->algorithm >= CHECKSUM_DIGEST_SHA3)
        {
                checksum_sponge sponge;

                checksum_sponge_open(address_of sponge,
                                     algorithm->algorithm == CHECKSUM_DIGEST_SM3,
                                     bytes);
                while ((got = system_read_retry((positive)input, block,
                                                FILE_TRANSFER_SIZE)) > 0)
                        checksum_sponge_write(address_of sponge, block, (positive)got);
                if (!standard)
                        system_close((positive)input);
                if (got < 0)
                        return got;
                checksum_sponge_close(address_of sponge, digest);
                return 0;
        }

        digest_state state;

        digest_open(address_of state, algorithm->algorithm, bytes);
        while ((got = system_read_retry((positive)input, block,
                                        FILE_TRANSFER_SIZE)) > 0)
                digest_write(address_of state, block, (positive)got);

        if (!standard)
                system_close((positive)input);

        if (got < 0)
                return got;

        digest_close(address_of state, digest);
        return 0;
}

/* A name in a verification report or diagnostic, quoted as coreutils'
   shell-escape style quotes it; ls owns that style. */
static fn checksum_name_put(writer write, string_address name)
{
        ls_quote_shell(write, name, string_length(name), false, true);
}

/*
        The name an unreadable input is blamed by, quoted the way the FAILED
        line beside it already quotes it: the reference quotes both, so a name
        holding a blank, a quote or a shell character reads the same in either
        line, and an empty name is '' rather than nothing at all.  This is
        string_diagnostic's own shape -- text_diagnostic's preflush, prefix
        and writer -- with the subject rendered instead of copied.
*/
static b32 checksum_blame(string_address name, string_address reason)
{
        //      Standard input with no operand at all arrives without a name,
        //      and quoting nothing read address zero: cksum < dir died of
        //      SIGSEGV where coreutils says "cksum: -: Is a directory".
        if (!name)
                name = (string_address) "-";
        text_flush();
        return string_report(writer_stderr, 0, "%s: %w: %s\n", text_name,
                             checksum_name_put, name, reason);
}

static fn checksum_filename_put(string_address name, bool escaped)
{
        if (!escaped)
        {
                text_put_string(name);
                return;
        }

        string_address from = name;

        while (string_get(from))
        {
                string_address stop = string_first_of_set(from, "\\\n\r");

                if (!stop)
                {
                        text_put_string(from);
                        break;
                }

                text_put(from, (positive)(stop - from));
                text_put_character('\\');
                text_put_character(*stop == '\n' ? 'n' : *stop == '\r' ? 'r' : '\\');
                from = stop + 1;
        }
}

static fn checksum_hex_put(p8 address_to digest, positive length)
{
        p8 text[128];
        text_put(text, memory_into_hex(text, digest, length));
}

static fn checksum_digest_put(p8 address_to digest, positive length)
{
        if (checksum_base64)
                checksum_base64_put(digest, length);
        else
                checksum_hex_put(digest, length);
}

/* The BSD tag's name: BLAKE2b says its width when it is not the full one. */
static fn checksum_label_put(const checksum_algorithm address_to algorithm,
                             positive bytes)
{
        text_put_string(algorithm->label);
        if (algorithm->variable_length && bytes != algorithm->bytes)
        {
                text_put_character('-');
                positive_to_string(text_put, bytes * 8);
        }
}

static fn checksum_line_put(const checksum_algorithm address_to algorithm,
                            positive bytes, p8 address_to digest,
                            string_address name, bool tagged)
{
        // --raw is the digest's bytes and nothing else.
        if (checksum_raw)
        {
                text_put(digest, bytes);
                return;
        }

        // --zero disables the escaping that exists for newline records.
        bool escaped = !checksum_zero && string_first_of_set(name, "\\\n\r");

        if (escaped)
                text_put_character('\\');

        if (tagged)
        {
                checksum_label_put(algorithm, bytes);
                text_put_string(" (");
        }
        else
        {
                checksum_digest_put(digest, bytes);
                text_put_character(' ');
                text_put_character(checksum_selected.mode == 'b' ? '*' : ' ');
        }
        checksum_filename_put(name, escaped);
        if (tagged)
        {
                text_put_string(") = ");
                checksum_digest_put(digest, bytes);
        }
        text_put_character(checksum_zero ? '\0' : '\n');
}

/*
        Many inputs at once.

        Each input is one job of the pool's ordered run, and a job's whole
        answer is its read status and its digest; the sink, on the calling
        thread, turns that into the line or the diagnostic in argument order.
        So the bytes written are the serial bytes at any width: the work is
        cut by input, never by thread count, and nothing but the sink writes.

        A job reads into a transfer block its own thread owns -- the shared
        one on the caller, a mapping of the same size taken once for each
        worker -- and a job never opens anything that is not a regular file
        or a directory. Standard input, a FIFO and a device are read in
        order by the sink itself, so naming one twice reads it the way the
        serial walk would.
*/
typedef struct
{
        bipolar status;
        p8 digest[64];
} checksum_answer;

#define CHECKSUM_DEFERRED 1

typedef struct
{
        const checksum_algorithm address_to algorithm;
        positive bytes;
        positive first;
        positive width;
        positive slots;
        positive inputs;
        positive group;
        bool from_files;
        bool tagged;
        bool spread;
        b32 answer;
        p8 address_to address_to blocks;
} checksum_batch;

static string_address checksum_input_name(checksum_batch address_to batch,
                                          positive index)
{
        string_address name = batch->from_files
                                  ? text_file_name((b32)index)
                              : batch->first < (positive)program_argument_count()
                                  ? program_argument((b32)(batch->first + index))
                                  : null;
        return name ? name : (string_address) "-";
}

/* The transfer block of the thread running a job, taken once per worker. */
static p8 address_to checksum_slot_block(p8 address_to address_to blocks,
                                         positive slots)
{
        positive slot = parallel_slot();

        if (!slot)
                return file_transfer;
        if (!blocks || slot >= slots)
                return null;
        if (!blocks[slot])
                blocks[slot] = memory_checked(FILE_TRANSFER_SIZE);
        return blocks[slot];
}

/* Whether a job may read this input itself: a regular file, a directory
   (whose read fails at once), or a name that is not there. */
static bool checksum_parallel_readable(string_address name)
{
        file_facts facts;

        if (string_is(name, '-') && !string_get(name + 1))
                return false;
        if (system_stat_at(AT_FDCWD, name, AT_NO_AUTOMOUNT, STATX_BASIC,
                           address_of facts) < 0)
                return true;
        return (facts.mode & MODE_FORMAT) == MODE_FILE ||
               (facts.mode & MODE_FORMAT) == MODE_DIRECTORY;
}

static fn checksum_batch_job(address_any context, positive index,
                             parallel_output address_to output)
{
        checksum_batch address_to batch = context;
        positive first = index * batch->group;
        positive last = first + batch->group < batch->inputs ? first + batch->group
                                                             : batch->inputs;
        p8 address_to block = checksum_slot_block(batch->blocks, batch->slots);

        for (positive at = first; at < last; at++)
        {
                checksum_answer answer;
                string_address name = checksum_input_name(batch, at);

                if (batch->spread && !checksum_parallel_readable(name))
                        answer.status = CHECKSUM_DEFERRED;
                else if (!block)
                        answer.status = -ERROR_NO_MEMORY;
                else
                        answer.status = checksum_hash_path(batch->algorithm, batch->bytes,
                                                           name, answer.digest, block);

                if (!parallel_write(output, address_of answer, sizeof(answer)))
                        return;
        }
}

static bool checksum_batch_sink(address_any context, positive index,
                                address_any data, positive length)
{
        checksum_batch address_to batch = context;
        positive first = index * batch->group;

        for (positive at = 0; at + sizeof(checksum_answer) <= length; at += sizeof(checksum_answer))
        {
                checksum_answer answer;
                string_address name = checksum_input_name(batch, first + at / sizeof(answer));

                memory_copy(address_of answer, (p8 address_to)data + at, sizeof(answer));

                if (answer.status == CHECKSUM_DEFERRED)
                        answer.status = checksum_hash_path(batch->algorithm, batch->bytes,
                                                           name, answer.digest, file_transfer);

                if (answer.status < 0)
                {
                        checksum_blame(name, file_reason(answer.status));
                        batch->answer = 1;
                }
                else
                        checksum_line_put(batch->algorithm, batch->bytes, answer.digest,
                                          name, batch->tagged);
        }
        return true;
}

/*
        How much a run weighs, and how many inputs one job carries.

        The first CHECKSUM_WEIGHED inputs are weighed by their sizes. A run
        of no more than that many stays on one thread unless its bytes are
        worth sharing, one input a job. A longer run is spread, and its jobs
        are cut from the sample: as many inputs as make about a mebibyte, so
        a hundred thousand small files are a few hundred jobs rather than a
        hundred thousand claims and wakes, and large files stay one a job.
        The cut depends on the files, never on the thread count, and the
        bytes written do not depend on it at all.
*/
#define CHECKSUM_WEIGHED 64
#define CHECKSUM_GROUP_BYTES (1 << 20)
#define CHECKSUM_GROUP_MAX 256

static positive checksum_weigh(positive total, positive sampled, positive count,
                               positive address_to group)
{
        address_to group = 1;
        if (count <= CHECKSUM_WEIGHED)
                return total;

        positive mean = sampled ? total / sampled : 0;
        positive per = mean ? CHECKSUM_GROUP_BYTES / mean : CHECKSUM_GROUP_MAX;

        address_to group = per < 1 ? 1 : per > CHECKSUM_GROUP_MAX ? CHECKSUM_GROUP_MAX : per;
        return PARALLEL_SPREAD;
}

static positive checksum_batch_weight(checksum_batch address_to batch,
                                      positive count, positive address_to group)
{
        positive total = 0;
        positive sampled = 0;

        for (positive index = 0; index < count && index < CHECKSUM_WEIGHED; index++)
        {
                file_facts facts;

                if (system_stat_at(AT_FDCWD, checksum_input_name(batch, index),
                                   AT_NO_AUTOMOUNT, STATX_BASIC, address_of facts) == 0 &&
                    (facts.mode & MODE_FORMAT) == MODE_FILE)
                {
                        total += facts.size;
                        sampled++;
                }
        }

        return checksum_weigh(total, sampled, count, group);
}

/* cksum's collected operands and the named sums' argv tail differ only at
   the input boundary; hashing, errors and escaped line output are shared. */
static b32 checksum_generate(const checksum_algorithm address_to algorithm,
                             positive first, bool tagged, bool from_files)
{
        positive count = (positive)program_argument_count();
        positive inputs = from_files ? (positive)text_input_count()
                                     : first < count ? count - first : 1;
        checksum_batch batch = {
            .algorithm = algorithm,
            .bytes = checksum_bytes(algorithm),
            .first = first,
            .width = parallel_width(),
            .slots = parallel_slots(),
            .inputs = inputs,
            .from_files = from_files,
            .tagged = tagged,
        };
        positive weight = checksum_batch_weight(address_of batch, inputs, address_of batch.group);

        // One thread, or too little to share: the jobs run inline in order
        // on the caller and need neither worker blocks nor the stat test.
        if (inputs > 1 && batch.width > 1 && weight >= PARALLEL_MINIMUM_BYTES)
                batch.blocks = memory_checked(batch.slots * sizeof(p8 address_to));
        if (!batch.blocks)
                weight = 0;
        batch.spread = batch.blocks != null;

        if (!batch.spread)
                batch.group = 1;
        parallel_ordered(checksum_batch_job, checksum_batch_sink, address_of batch,
                         (inputs + batch.group - 1) / batch.group, weight);

        if (batch.blocks)
        {
                for (positive slot = 1; slot < batch.slots; slot++)
                        if (batch.blocks[slot])
                                memory_free(batch.blocks[slot], FILE_TRANSFER_SIZE);
                memory_free(batch.blocks, batch.slots * sizeof(p8 address_to));
        }

        return batch.answer;
}

static fn checksum_check_result_put(string_address name,
                                    string_address result)
{
        checksum_name_put(text_put, name);
        text_put_string(": ");
        text_put_string(result);
        text_put_character('\n');
}

/*
        Which algorithm a BSD tag at text_line + at names, and where its
        name begins: the label, a width for BLAKE2b (BLAKE2b-256) or for the
        SHA-2 family's other spelling (SHA2-256), at most one blank, then the
        parenthesis. Zero when the line is not tagged that way.
*/
static positive checksum_tag_parse(positive at,
                                   const checksum_algorithm address_to address_to found,
                                   positive address_to bytes)
{
        for (positive which = 0; which < array_count(checksum_algorithms); which++)
        {
                const checksum_algorithm address_to one = checksum_algorithms + which;
                positive length = string_length(one->label);
                positive width = one->bytes;
                positive from = at + length;

                if (text_line_length < from + 1 ||
                    string_compare_max(text_line + at, one->label, length))
                        continue;

                if (one->variable_length && text_line[from] == '-')
                {
                        positive digits = 0;
                        positive bits;

                        from++;
                        bits = string_digits_max(text_line + from,
                                                 text_line_length - from,
                                                 address_of digits);
                        from += digits;
                        if (!digits || digits > 3 || !bits || bits > 512 || bits % 8)
                                continue;
                        width = bits / 8;
                }

                if (from < text_line_length && text_line[from] == ' ')
                        from++;
                if (from >= text_line_length || text_line[from] != '(')
                        continue;

                address_to found = one;
                address_to bytes = width;
                return from + 1;
        }

        // SHA2-224 through SHA2-512 name the same four digests.
        if (text_line_length > at + 5 && !string_compare_max(text_line + at, "SHA2-", 5))
                for (positive which = CHECKSUM_SHA2_FIRST; which <= CHECKSUM_SHA2_LAST; which++)
                {
                        const checksum_algorithm address_to one = checksum_algorithms + which;
                        positive from = at + 5 + 3;

                        if (text_line_length < from + 1 ||
                            string_compare_max(text_line + at + 5, one->label + 3, 3))
                                continue;
                        if (from < text_line_length && text_line[from] == ' ')
                                from++;
                        if (from >= text_line_length || text_line[from] != '(')
                                continue;

                        address_to found = one;
                        address_to bytes = one->bytes;
                        return from + 1;
                }

        return 0;
}

/*
        Decode one checksum record in place.

        algorithm is what the caller reads this record with: one algorithm,
        or null for any tagged record (cksum --check without --algorithm).
        On success found and bytes say which digest and how wide. Tagged
        records must name an algorithm the caller accepts; untagged ones are
        as wide as the algorithm, except that BLAKE2b and cksum's SHA-2
        family take the width the digits have. The only escapes are the two
        GNU emits for portable newline-delimited output.
*/
static bool checksum_line_parse(const checksum_algorithm address_to algorithm,
                                const checksum_algorithm address_to address_to found,
                                positive address_to bytes,
                                p8 address_to expected,
                                string_address address_to filename)
{
        positive at = 0;

        // A record from a Windows editor ends in CR LF; GNU drops the CR.
        if (text_line_length && text_line[text_line_length - 1] == '\r')
                text_line_length--;

        // Blanks before the record, then the backslash of an escaped name.
        while (at < text_line_length && (text_line[at] == ' ' || text_line[at] == '\t'))
                at++;

        bool escaped = at < text_line_length && text_line[at] == '\\';

        if (escaped)
                at++;

        const checksum_algorithm address_to one = null;
        positive width = 0;
        positive digest_at;
        positive digest_length;
        p8 address_to name;
        bool reversed = false;
        positive named = checksum_tag_parse(at, address_of one, address_of width);

        if (named)
        {
                /*
                        The tagged record, BSD's LABEL (name) = digest or
                        OpenSSL's LABEL(name)= digest: the name runs to the
                        last parenthesis, and blanks may stand either side of
                        the equals sign.
                */
                bool accepted = !algorithm || one == algorithm ||
                                (checksum_sha2_family &&
                                 one >= checksum_algorithms + checksum_sha2_family &&
                                 one <= checksum_algorithms + checksum_sha2_family + 3);
                //      A malformed line later is called by the family a
                //      SHA2-NNN or SHA3-NNN tag names, and by its own label
                //      otherwise.
                if (!algorithm)
                        checksum_check_label =
                            !string_compare_max(text_line + at, "SHA2-", 5)
                                ? (string_address) "SHA2"
                            : one >= checksum_algorithms + CHECKSUM_SHA3_FIRST &&
                                      one <= checksum_algorithms + CHECKSUM_SHA3_LAST
                                ? (string_address) "SHA3"
                                : one->label;
                if (!accepted)
                        return false;
                // A tag without a width is the algorithm's full one,
                // whatever -l asked for.
                if (!one->variable_length ||
                    named == at + string_length(one->label) + 1 ||
                    named == at + string_length(one->label) + 2)
                        width = one->bytes;

                positive close = text_line_length;

                while (close > named && text_line[close - 1] != ')')
                        close--;
                if (close == named)
                        return false;

                positive after = close;

                while (after < text_line_length &&
                       (text_line[after] == ' ' || text_line[after] == '\t'))
                        after++;
                if (after >= text_line_length || text_line[after] != '=')
                        return false;
                after++;
                while (after < text_line_length &&
                       (text_line[after] == ' ' || text_line[after] == '\t'))
                        after++;

                name = text_line + named;
                digest_at = after;
                digest_length = text_line_length - after;
                text_line[close - 1] = end;
        }
        else
        {
                if (!algorithm)
                        return false;

                one = algorithm;
                width = checksum_bytes(algorithm);

                positive token = 0;
                positive hexes = 0;

                while (at + token < text_line_length && text_line[at + token] != ' ' &&
                       text_line[at + token] != '\t')
                        token++;
                while (hexes < token && digit_known(text_line[at + hexes], 16) < 16)
                        hexes++;

                if (algorithm->variable_length || checksum_sha2_family)
                {
                        positive paddings = 0;

                        while (paddings < token && text_line[at + token - 1 - paddings] == '=')
                                paddings++;
                        width = 0;
                        if (hexes == token)
                                width = token / 2;
                        else if (checksum_base64_read)
                                for (positive bytes = 1; bytes <= 64 && !width; bytes++)
                                        if (checksum_base64_length(bytes) == token &&
                                            (3 - bytes % 3) % 3 == paddings)
                                                width = bytes;

                        if (algorithm->variable_length)
                        {
                                if (hexes == token &&
                                    (token < 2 || token % 2 || token > 128))
                                        return false;
                        }
                        else
                        {
                                one = null;
                                for (positive which = checksum_sha2_family;
                                     which <= checksum_sha2_family + 3; which++)
                                        if (width == (positive)checksum_algorithms[which].bytes)
                                                one = checksum_algorithms + which;
                                if (!one)
                                        return false;
                        }
                        if (!width)
                                return false;
                }

                if (text_line_length < at + token + 2)
                        return false;

                digest_at = at;
                digest_length = token;
                at += token;

                /*
                        One blank, then a mode marker if one is there.

                        The reference takes a space or a tab after the digest
                        and then a second space or the asterisk of a binary
                        record, if either follows -- so "digest name" reads as
                        well as "digest  name", and a third space is the first
                        byte of the name rather than another separator.
                        Requiring the pair refused every manifest written with
                        a single space, which is most of the ones written by
                        hand rather than by the tool.
                */
                if (at >= text_line_length ||
                    (text_line[at] != ' ' && text_line[at] != '\t'))
                        return false;

                at++;

                /*
                        Then a mode marker, or the reversed BSD shape: a
                        second blank or the asterisk of a binary record is the
                        standard shape, and a name straight after the one
                        blank -- or a name one byte long -- is the other.
                        Which one this is waits for the digits to be read.
                */
                reversed = text_line_length - at == 1 ||
                           (text_line[at] != ' ' && text_line[at] != '*');

                text_line[text_line_length] = end;
                name = text_line + at;
        }

        if (!checksum_digest_read(text_line + digest_at, digest_length, width, expected))
                return false;

        /*
                coreutils will not let one run mix the two untagged shapes:
                whichever the first well-formed untagged record has, a later
                record of the other shape is malformed, and a standard record
                read after reversed ones keeps its blank in the name. The run
                is every manifest this invocation reads.
        */
        if (!named)
        {
                if (reversed)
                {
                        if (checksum_bsd_reversed == 0)
                                return false;
                        checksum_bsd_reversed = 1;
                }
                else if (checksum_bsd_reversed != 1)
                {
                        checksum_bsd_reversed = 0;
                        name++;
                }
        }

        if (escaped)
        {
                p8 address_to from = name;
                p8 address_to into = name;

                while (*from)
                {
                        if (*from != '\\')
                        {
                                *into++ = *from++;
                                continue;
                        }

                        from++;
                        if (*from == 'n')
                                *into++ = '\n';
                        else if (*from == 'r')
                                *into++ = '\r';
                        else if (*from == '\\')
                                *into++ = '\\';
                        else
                                return false;

                        from++;
                }

                *into = end;
        }

        address_to found = one;
        address_to bytes = width;
        address_to filename = name;
        return true;
}

/*
        A manifest, checked many records at once.

        The records are read and parsed first, in order, on the calling
        thread -- the reversed-shape rule and every malformed count come out
        exactly as a serial read makes them -- and kept with their line
        numbers. Then each record is one job of an ordered run that hashes
        its file the way the generating jobs do, and the sink prints OK,
        FAILED, the diagnostics and the -w warnings in line order. A record
        naming standard input, a FIFO or a device is hashed by the sink.
*/
typedef struct
{
        const checksum_algorithm address_to algorithm;
        /* What a malformed record is called: cksum without -a calls it by
           the last tag it read, as coreutils' does. */
        string_address label;
        positive line;
        positive name;
        p8 width;
        bool loose;
        p8 expected[64];
} checksum_record;

typedef struct
{
        const checksum_algorithm address_to algorithm;
        string_address manifest;
        checksum_record address_to records;
        positive count;
        positive room;
        p8 address_to names;
        positive names_used;
        positive names_room;
        positive width;
        positive slots;
        positive group;
        p8 address_to address_to blocks;
        bool spread;
        bool quiet;
        bool status;
        bool warn;
        bool ignore_missing;
        bool failed;
        positive malformed;
        positive formatted;
        positive mismatched;
        positive unreadable;
        positive verified;
} checksum_check_run;

/* A mapped region grown by doubling on the calling thread. */
static bool checksum_region_grow(p8 address_to address_to region,
                                 positive address_to room, positive wanted)
{
        if (wanted <= address_to room)
                return true;

        positive larger = address_to room ? address_to room : 65536;

        while (larger < wanted)
                larger *= 2;

        p8 address_to fresh = memory_checked(larger);

        if (!fresh)
                return false;
        if (address_to region)
        {
                memory_copy(fresh, address_to region, address_to room);
                memory_free(address_to region, address_to room);
        }
        address_to region = fresh;
        address_to room = larger;
        return true;
}

static fn checksum_check_collect(checksum_check_run address_to run)
{
        positive line = 0;

        while (text_line_next(text_line, 0))
        {
                checksum_record record = {0};
                string_address filename;
                const checksum_algorithm address_to one;
                positive bytes;

                line++;
                // An empty record is passed over in silence, and so is a
                // comment -- a line whose first byte is '#' -- and a line
                // that is nothing but the CR of a CR LF ending.
                if (!text_line_length || text_line[0] == '#' ||
                    (text_line_length == 1 && text_line[0] == '\r'))
                        continue;

                record.line = line;
                checksum_base64_loose = false;
                if (checksum_line_parse(run->algorithm, address_of one,
                                        address_of bytes, record.expected,
                                        address_of filename))
                {
                        positive length = string_length(filename) + 1;

                        if (!checksum_region_grow(address_of run->names,
                                                  address_of run->names_room,
                                                  run->names_used + length))
                                break;
                        memory_copy(run->names + run->names_used, filename, length);
                        record.algorithm = one;
                        record.width = (p8)bytes;
                        record.loose = checksum_base64_loose;
                        record.name = run->names_used;
                        run->names_used += length;
                }
                record.label = checksum_check_label;

                p8 address_to records = (p8 address_to)run->records;

                if (!checksum_region_grow(address_of records, address_of run->room,
                                          (run->count + 1) * sizeof(record)))
                        break;
                run->records = (checksum_record address_to)records;
                run->records[run->count++] = record;
        }
}

static string_address checksum_record_name(checksum_check_run address_to run,
                                           const checksum_record address_to record)
{
        return (string_address)(run->names + record->name);
}

static fn checksum_check_job(address_any context, positive index,
                             parallel_output address_to output)
{
        checksum_check_run address_to run = context;
        positive first = index * run->group;
        positive last = first + run->group < run->count ? first + run->group : run->count;
        p8 address_to block = checksum_slot_block(run->blocks, run->slots);

        for (positive at = first; at < last; at++)
        {
                const checksum_record address_to record = run->records + at;
                checksum_answer answer;

                if (!record->algorithm)
                        answer.status = CHECKSUM_DEFERRED;
                else
                {
                        string_address name = checksum_record_name(run, record);

                        if (run->spread && !checksum_parallel_readable(name))
                                answer.status = CHECKSUM_DEFERRED;
                        else if (!block)
                                answer.status = -ERROR_NO_MEMORY;
                        else
                                answer.status = checksum_hash_path(record->algorithm, record->width,
                                                                   name, answer.digest, block);
                }

                if (!parallel_write(output, address_of answer, sizeof(answer)))
                        return;
        }
}

static fn checksum_check_one(checksum_check_run address_to run,
                             const checksum_record address_to record,
                             checksum_answer address_to answer)
{
        if (!record->algorithm)
        {
                run->malformed++;
                if (run->warn)
                {
                        text_flush();
                        string_format(log_error, "%s: %w: %p: improperly formatted %s checksum line\n",
                                      checksum_program, checksum_name_put,
                                      run->manifest, record->line, record->label);
                }
                return;
        }

        string_address filename = checksum_record_name(run, record);

        run->formatted++;

        if (answer->status == CHECKSUM_DEFERRED)
                answer->status = checksum_hash_path(record->algorithm, record->width,
                                                    filename, answer->digest, file_transfer);

        if (answer->status == -ERROR_NO_ENTRY && run->ignore_missing)
                return;

        if (answer->status < 0)
        {
                run->unreadable++;
                run->failed = true;

                checksum_blame(filename, file_reason(answer->status));
                if (!run->status)
                        checksum_check_result_put(filename,
                                                  (string_address) "FAILED open or read");
                return;
        }

        run->verified++;
        if (record->loose ||
            memory_compare(record->expected, answer->digest, record->width))
        {
                run->mismatched++;
                run->failed = true;
                if (!run->status)
                        checksum_check_result_put(filename, (string_address) "FAILED");
        }
        else if (!run->quiet && !run->status)
                checksum_check_result_put(filename, (string_address) "OK");
}

static bool checksum_check_sink(address_any context, positive index,
                                address_any data, positive length)
{
        checksum_check_run address_to run = context;
        positive first = index * run->group;

        for (positive at = 0; at + sizeof(checksum_answer) <= length; at += sizeof(checksum_answer))
        {
                checksum_answer answer;

                memory_copy(address_of answer, (p8 address_to)data + at, sizeof(answer));
                checksum_check_one(run, run->records + first + at / sizeof(answer), address_of answer);
        }
        return true;
}

/* Every collected record through the pool, then the storage back. */
static fn checksum_check_records(checksum_check_run address_to run)
{
        positive total = 0;
        positive sampled = 0;

        for (positive index = 0; index < run->count && index < CHECKSUM_WEIGHED; index++)
        {
                file_facts facts;
                const checksum_record address_to record = run->records + index;

                if (record->algorithm &&
                    system_stat_at(AT_FDCWD, checksum_record_name(run, record),
                                   AT_NO_AUTOMOUNT, STATX_BASIC, address_of facts) == 0 &&
                    (facts.mode & MODE_FORMAT) == MODE_FILE)
                {
                        total += facts.size;
                        sampled++;
                }
        }

        positive weight = checksum_weigh(total, sampled, run->count, address_of run->group);

        if (run->count > 1 && run->width > 1 && weight >= PARALLEL_MINIMUM_BYTES)
                run->blocks = memory_checked(run->slots * sizeof(p8 address_to));
        run->spread = run->blocks != null;
        if (!run->spread)
                run->group = 1;

        parallel_ordered(checksum_check_job, checksum_check_sink, run,
                         (run->count + run->group - 1) / run->group,
                         run->spread ? weight : 0);

        if (run->blocks)
        {
                for (positive slot = 1; slot < run->slots; slot++)
                        if (run->blocks[slot])
                                memory_free(run->blocks[slot], FILE_TRANSFER_SIZE);
                memory_free(run->blocks, run->slots * sizeof(p8 address_to));
        }
        if (run->records)
                memory_free(run->records, run->room);
        if (run->names)
                memory_free(run->names, run->names_room);
}

static b32 checksum_verify(const checksum_algorithm address_to algorithm,
                           file_taking address_to taking)
{
        if (!checksum_program)
                checksum_program = algorithm ? algorithm->command
                                             : (string_address) "cksum";
        if (!checksum_check_label)
                checksum_check_label = algorithm ? algorithm->label
                                                 : (string_address) "CRC";

        bool status = checksum_selected.verify == 's';
        bool strict = (taking->flags & FILE_FLAG('S')) != 0;
        bool ignore_missing = (taking->flags & FILE_FLAG('i')) != 0;
        positive manifests = checksum_manifest_files
                                 ? (positive)text_input_count()
                                 : (taking->first < (positive)program_argument_count()
                                        ? (positive)program_argument_count() - taking->first
                                        : 1);
        bool failed = false;

        for (positive m = 0; m < manifests; m++)
        {
                string_address manifest =
                    checksum_manifest_files
                        ? text_file_name(m)
                        : (taking->first < (positive)program_argument_count()
                               ? program_argument((b32)(taking->first + m))
                               : null);

                if (!text_open(manifest))
                {
                        failed = true;
                        continue;
                }

                file_facts kind;
                if (file_look(text_input.handle, (string_address)"", AT_EMPTY_PATH,
                              address_of kind) &&
                    (kind.mode & MODE_FORMAT) == MODE_DIRECTORY)
                {
                        text_flush();
                        string_format(log_error, "%s: %w: read error\n",
                                      checksum_program, checksum_name_put,
                                      manifest ? manifest
                                          : (string_address)"standard input");
                        text_close();
                        failed = true;
                        continue;
                }
                if (!manifest || (manifest[0] == '-' && !manifest[1]))
                        manifest = (string_address) "standard input";

                bool read_failed = false;
                checksum_check_run run = {
                    .algorithm = algorithm,
                    .manifest = manifest,
                    .quiet = checksum_selected.verify == 'q',
                    .status = checksum_selected.verify == 's',
                    .warn = checksum_selected.verify == 'w',
                    .ignore_missing = ignore_missing,
                    .width = parallel_width(),
                    .slots = parallel_slots(),
                };

                checksum_check_collect(address_of run);

                if (text_input.failed)
                {
                        // The shared reader has already named it; GNU says
                        // nothing further about a manifest it cannot read.
                        failed = true;
                        read_failed = true;
                }

                checksum_check_records(address_of run);

                positive malformed = run.malformed;
                positive formatted = run.formatted;
                positive mismatched = run.mismatched;
                positive unreadable = run.unreadable;
                positive verified = run.verified;

                failed = failed || run.failed;
                text_close();

                /*
                        Every manifest has its own format and verification
                        contract; a valid earlier file cannot make an empty
                        later one valid. In coreutils' order: a manifest with
                        no well-formed record says so even under --status;
                        otherwise the warnings, and under --ignore-missing a
                        manifest where no checksum matched says that. A
                        manifest nothing matched in fails either way.
                */
                positive matched = verified - mismatched;

                if (!formatted)
                {
                        if (!read_failed)
                        {
                                failed = true;
                                text_flush();
                                string_format(writer_stderr, "%s: %w: %s\n",
                                    text_name, checksum_name_put, manifest,
                                    (string_address) "no properly formatted checksum lines found");
                        }
                }
                else
                {
                        if (!status)
                        {
                                if (malformed)
                                {
                                        text_flush();
                                        string_format(log_error, "%s: WARNING: %p%s\n", checksum_program, malformed, malformed == 1 ? (string_address) " line is improperly formatted" : (string_address) " lines are improperly formatted");
                                }
                                if (unreadable)
                                {
                                        text_flush();
                                        string_format(log_error, "%s: WARNING: %p%s\n", checksum_program, unreadable, unreadable == 1 ? (string_address) " listed file could not be read" : (string_address) " listed files could not be read");
                                }
                                if (mismatched)
                                {
                                        text_flush();
                                        string_format(log_error, "%s: WARNING: %p%s\n", checksum_program, mismatched, mismatched == 1 ? (string_address) " computed checksum did NOT match" : (string_address) " computed checksums did NOT match");
                                }
                                if (ignore_missing && !matched)
                                {
                                        text_flush();
                                        string_format(writer_stderr, "%s: %w: %s\n",
                                            text_name, checksum_name_put, manifest,
                                            (string_address) "no file was verified");
                                }
                        }
                        if (!matched)
                                failed = true;
                }

                if (strict && malformed)
                        failed = true;
        }

        return failed ? 1 : 0;
}

static b32 checksum_main()
{
        string_address command = program_argument(0);

        if (command)
                command = file_last_component(command);
        const checksum_algorithm address_to algorithm =
            checksum_algorithm_find(command, false);

        if (!algorithm)
                return 1;

        text_begin(command);
        checksum_modes_reset();

        file_taking taking = {
            .program = command,
            .options = checksum_options + !algorithm->variable_length,
            .selection = (p8 address_to)&checksum_selected,
        };

        if (!file_take(address_of taking))
                return text_done(1);

        if (taking.flags & FILE_FLAG('l'))
        {
                b32 refused = checksum_blake2b_length(
                    command, file_option_value(address_of taking, 'l'),
                    address_of checksum_length);
                if (refused)
                        return refused;
        }

        bool checking = (taking.flags & FILE_FLAG('c')) != 0;
        bool tagged = (taking.flags & FILE_FLAG('T')) != 0;
        checksum_zero = (taking.flags & FILE_FLAG('z')) != 0;

        b32 refused = checksum_refuse_modes(command, checking, tagged, taking.flags);
        if (refused)
                return refused;
        if (tagged && checksum_selected.mode == 't')
                return checksum_usage_error(command, "--tag does not support --text mode");

        if (checking)
                return text_done(checksum_verify(algorithm, address_of taking));

        return text_done(checksum_generate(algorithm, taking.first, tagged, false));
}

/* ---- POSIX cksum, which is a CRC and a policy rather than a digest. ---- */

/*
        POSIX cksum.

        The CRC is deliberately separate from the digest engine the named
        sums share: a four-byte serial checksum wants its own folds, not a
        block core's buffering. hash_crc32_msb in lib.c is all of it --
        a braided table floor and PCLMULQDQ, VPCLMULQDQ or PMULL folds, over
        tables the assembler built -- so nothing is prepared at run time and
        no processor is asked here. This file keeps the POSIX policy: the
        length folded in after the bytes and the final invert. Input and
        output remain the shell's shared blocks and writers.
*/
/* The length after the bytes, least significant byte first, in as few
   bytes as it takes. build.c marks a build directory the same way. */
static p32 cksum_crc_length(p32 crc, p64 length)
{
        p8 counted[8];
        positive places = 0;

        for (; length; length >>= 8)
                counted[places++] = (p8)length;

        return hash_crc32_msb(crc, counted, places);
}

static fn cksum_crc_put(p32 crc, p64 bytes, string_address name, bool named)
{
        positive_to_string(text_put, crc);
        text_put_character(' ');
        positive_to_string(text_put, (positive)bytes);

        if (named)
        {
                text_put_character(' ');
                text_put_string(name);
        }

        text_put_character(checksum_zero ? '\0' : '\n');
}

/*
        Every serial sum cksum can be asked for, which is every sum that is
        not a digest: the POSIX CRC as 'p', and -a crc32b, bsd and sysv as
        'c', 'b' and 's'.  One reader serves all four because they differ
        only in the word they carry and what they do to it at the ends --
        the kind is read once per FILE_TRANSFER_SIZE block, never per byte.

        'p' is the POSIX CRC, hash_crc32_msb with the length folded in after
        the bytes.  crc32b is the reflected IEEE CRC gzip uses, hash_crc32,
        without that length. bsd is sum -r's rotating 16-bit sum and sysv is
        sum -s's byte total folded to 16 bits, over 1024- and 512-byte
        blocks; text.c's sum computes both the same way.
*/
/* --debug's line goes to standard error through a writer that notes a
   short write: a note nobody could read is a failure, as GNU counts it. */
static bool cksum_debug_lost;

static fn cksum_debug_say(address_any data, positive length)
{
        if (!length)
                length = string_length((string_address)data);
        if (system_write_all(2, data, length) != length)
                cksum_debug_lost = true;
}

/*
        coreutils asks the CPU for each accelerated CRC and lets
        GLIBC_TUNABLES=glibc.cpu.hwcaps=-NAME,... switch one off: the last
        glibc.cpu.hwcaps= in the variable holds, up to the next colon, and a
        name counts only whole. --debug says which it tried and which it
        took, and the test suite turns them off to see the words change.
*/
static bool cksum_hwcap_allowed(string_address name)
{
        string_address tunables = file_environment("GLIBC_TUNABLES");
        string_address caps = null;
        if (!tunables)
                return true;

        for (string_address at = tunables; *at; at++)
                if (string_has_prefix(at, "glibc.cpu.hwcaps="))
                        caps = at + sizeof("glibc.cpu.hwcaps=") - 1;

        if (!caps)
                return true;

        positive length = string_length(name);

        for (string_address at = caps; *at && *at != ':'; )
        {
                string_address word = at;

                while (*at && *at != ',' && *at != ':')
                        at++;
                if ((positive)(at - word) == length + 1 && word[0] == '-' &&
                    !memory_compare(word + 1, name, length))
                        return false;
                if (*at == ',')
                        at++;
        }

        return true;
}

static bool cksum_pclmul_usable()
{
#if X64 && !defined(KERNEL_MODE)
        return cpu_has_pclmul && cpu_has_avx2 &&
               cksum_hwcap_allowed("AVX") && cksum_hwcap_allowed("PCLMULQDQ");
#else
        return false;
#endif
}

static fn cksum_debug_line(string_address kind, bool taken)
{
        text_flush();
        string_format(cksum_debug_say,
                      taken ? "cksum: using %s hardware support\n"
                            : "cksum: %s support not detected\n",
                      kind);
}

static bool cksum_other_path(p8 kind, string_address path, bool debug,
                             p32 address_to result, p64 address_to size)
{
        bool standard;
        bipolar input = checksum_open(path, address_of standard);

        if (input < 0)
                return checksum_blame(path, file_reason(input));

        // coreutils names the crc32b machinery for every input it reads.
        if (debug && kind == 'c')
        {
#if X64 && !defined(KERNEL_MODE)
                cksum_debug_line("pclmul", cksum_pclmul_usable());
#endif
        }

        p32 sum = kind == 'c' ? ~(p32)0 : 0;
        p64 bytes = 0;
        bipolar got;

        while ((got = system_read_retry((positive)input, file_transfer,
                                        FILE_TRANSFER_SIZE)) > 0)
        {
                if (kind == 'p')
                        sum = hash_crc32_msb(sum, file_transfer, (positive)got);
                else if (kind == 'c')
                        sum = hash_crc32(sum, file_transfer, (positive)got);
                else if (kind == 'b')
                        sum = memory_checksum_bsd16(file_transfer, (positive)got, sum);
                else
                        sum += (p32)memory_sum_bytes(file_transfer, (positive)got);
                bytes += (positive)got;
        }

        if (!standard)
                system_close((positive)input);

        if (got < 0)
                return checksum_blame(path, file_reason(got));

        if (kind == 'p')
                sum = ~cksum_crc_length(sum, bytes);
        else if (kind == 'c')
                sum = ~sum;
        else if (kind == 's')
        {
                p32 folded = (sum & 0xffff) + (sum >> 16);

                sum = (folded & 0xffff) + (folded >> 16);
        }

        address_to result = sum;
        address_to size = bytes;
        return true;
}

static fn cksum_other_put(p8 kind, p32 sum, p64 bytes, string_address name,
                          bool named)
{
        // --raw is the sum in network order: four bytes of CRC, two of the rest.
        if (checksum_raw)
        {
                p8 wire[4];

                if (kind == 'c' || kind == 'p')
                {
                        network_store_32(wire, sum);
                        text_put(wire, 4);
                }
                else
                {
                        network_store_16(wire, (p16)sum);
                        text_put(wire, 2);
                }
                return;
        }

        if (kind == 'c' || kind == 'p')
        {
                cksum_crc_put(sum, bytes, name, named);
                return;
        }

        positive block = kind == 's' ? 512 : 1024;
        p64 blocks = bytes / block + (bytes % block != 0);

        if (kind == 's')
        {
                positive_to_string(text_put, sum);
                text_put_character(' ');
                positive_to_string(text_put, (positive)blocks);
        }
        else
        {
                positive_to_padded(text_put, sum, 5, '0', 0);
                text_put_character(' ');
                positive_to_padded(text_put, (positive)blocks, 5, ' ', 0);
        }

        if (named)
        {
                text_put_character(' ');
                text_put_string(name);
        }

        text_put_character(checksum_zero ? '\0' : '\n');
}

static b32 cksum_others(p8 kind, bool debug)
{
        bool named = text_files_count != 0;
        b32 inputs = text_input_count();
        b32 answer = 0;

        for (b32 i = 0; i < inputs; i++)
        {
                string_address name = text_file_name(i);
                string_address said = name ? name : (string_address) "-";
                p32 sum;
                p64 bytes;

                /* A standard input the POSIX CRC cannot read has always been
                   left unnamed in the diagnostic, where the other sums name
                   it by the dash the operand would have carried. */
                if (!cksum_other_path(kind, kind == 'p' ? name : said, debug,
                                      address_of sum, address_of bytes))
                {
                        answer = 1;
                        continue;
                }

                cksum_other_put(kind, sum, bytes, said, named);
        }

        return text_done(answer);
}

/* -a sha2 or sha3 -l N: the family's digest N bits wide, or null for any
   other N. first is the family's first row, CHECKSUM_SHA2_FIRST or
   CHECKSUM_SHA3_FIRST. */
static const checksum_algorithm address_to cksum_sha2_width(positive first,
                                                            positive bits)
{
        for (positive which = first; which <= first + 3; which++)
                if ((positive)checksum_algorithms[which].bytes * 8 == bits)
                        return checksum_algorithms + which;
        return null;
}

static const argument_option cksum_options[] = {
    {"algorithm", 'a', ARGUMENT_REQUIRED},
    {"untagged", 'U', 0, 1},
    {"tag", 'T', 0, 1},
    {"raw", 'R'},
    {"base64", 'B'},
    {"zero", 'z'},
    {"length", 'l', ARGUMENT_REQUIRED},
    {"check", 'c'},
    {"ignore-missing", 'i'},
    {"quiet", 'q', 0, ARGUMENT_SELECT(checksum_selection, verify)},
    {"status", 's', 0, ARGUMENT_SELECT(checksum_selection, verify)},
    {"strict", 'S'},
    {"warn", 'w', 0, ARGUMENT_SELECT(checksum_selection, verify)},
    {"debug", 'D'},
    {"binary", 'b', 0, ARGUMENT_SELECT(checksum_selection, mode)},
    {"text", 't', 0, ARGUMENT_SELECT(checksum_selection, mode)},
    {null},
};

/* Each algorithm is checked as it is read, so an unknown one is refused
   even when a later --algorithm would supersede it. */
static bool cksum_option_seen(p8 letter, string_address value)
{
        static const string_address known[] = {
            "bsd", "sysv", "crc", "crc32b", "md5", "sha1", "sha224", "sha256",
            "sha384", "sha512", "sha2", "sha3", "blake2b", "sm3",
        };

        if (letter != 'a' || !value)
                return true;

        for (positive at = 0; at < array_count(known); at++)
                if (string_equals(value, known[at]))
                        return true;

        text_flush();
        string_format(writer_stderr,
            "cksum: invalid argument '%w' for '--algorithm'\nValid arguments are:\n",
            writer_terminal_quoted_name, value);
        for (positive at = 0; at < array_count(known); at++)
                string_format(writer_stderr, "  - '%s'\n", known[at]);
        return string_report(writer_stderr, false, "Try 'cksum --help' for more information.\n");
}

static b32 cksum_main()
{
        file_taking taking = {
            .program = (string_address) "cksum",
            .options = cksum_options,
            .operand = text_file_add,
            .seen = cksum_option_seen,
            .selection = (p8 address_to)&checksum_selected,
        };


        text_begin("cksum");
        checksum_modes_reset();
        cksum_debug_lost = false;

        if (!file_take(address_of taking) ||
            (text_files_failed && string_diagnostic(
                address_of text_diagnostic, 1, null, "too many operands")))
                return text_done(1);

        string_address algorithm = file_option_value(address_of taking, 'a');
        string_address length = file_option_value(address_of taking, 'l');
        bool raw = (taking.flags & FILE_FLAG('R')) != 0;
        bool checking = (taking.flags & FILE_FLAG('c')) != 0;
        bool tagged = checksum_selected.style == 'T';
        bool debug = (taking.flags & FILE_FLAG('D')) != 0;
        bool lengthed = (taking.flags & FILE_FLAG('l')) && length;
        bool sha2 = algorithm && string_equals(algorithm, "sha2");
        bool sha3 = algorithm && string_equals(algorithm, "sha3");
        bool blake2b = algorithm && string_equals(algorithm, "blake2b");
        positive bits = 0;

        checksum_zero = (taking.flags & FILE_FLAG('z')) != 0;

        /*
                The reference's own refusals, in the order it makes them. A
                length that is no number is refused as it is read. A length of
                zero is no length at all -- it asks for the algorithm's own
                width -- so it does not reach the second.
        */
        string_address why = lengthed ? checksum_decimal(length, address_of bits) : null;

        if (why)
        {
                text_flush();
                return text_done(string_report(writer_stderr, 1,
                    "cksum: invalid length: '%w'%s\n", writer_terminal_quoted_name, length, why));
        }
        if (lengthed && bits && !blake2b && !sha2 && !sha3)
        {
                text_flush();
                return text_done(string_report(writer_stderr, 1,
                    "cksum: --length is only supported with --algorithm blake2b, sha2, or sha3\n"));
        }
        if (lengthed && blake2b)
        {
                b32 refused = checksum_blake2b_length((string_address) "cksum",
                                                      length, address_of checksum_length);
                if (refused)
                        return refused;
        }
        positive family = sha2 ? CHECKSUM_SHA2_FIRST : CHECKSUM_SHA3_FIRST;

        if (lengthed && (sha2 || sha3) && !cksum_sha2_width(family, bits))
        {
                text_flush();
                return text_done(string_report(writer_stderr, 1,
                    "cksum: invalid length: '%w'\n"
                    "cksum: digest length for '%w' must be 224, 256, 384, or 512\n",
                    writer_terminal_quoted_name, length, writer_terminal_quoted_name, sha2 ? (string_address) "SHA2" : (string_address) "SHA3"));
        }
        // The SHA-2 and SHA-3 families need their width said, before any
        // complaint about the other mode's options.
        if ((sha2 || sha3) && !lengthed && !checking)
        {
                text_flush();
                return text_done(string_report(writer_stderr, 1,
                    "cksum: --algorithm=%s requires specifying --length 224, 256, 384, or 512\n",
                    algorithm));
        }
        if (checking && algorithm &&
            (string_equals(algorithm, "bsd") || string_equals(algorithm, "sysv") ||
             string_equals(algorithm, "crc") || string_equals(algorithm, "crc32b")))
        {
                text_flush();
                return text_done(string_report(writer_stderr, 1,
                    "cksum: --check is not supported with --algorithm={bsd,sysv,crc,crc32b}\n"));
        }
        if (raw && (taking.flags & FILE_FLAG('B')))
                return checksum_usage_error("cksum", "--base64 and --raw are mutually exclusive");
        b32 refused = checksum_refuse_modes("cksum", checking, tagged, taking.flags);
        if (refused)
                return refused;
        // cksum tags unless told not to, and a tagged line has no room for
        // the text marker.
        if (checksum_selected.mode == 't' && checksum_selected.style != 'U')
                return checksum_usage_error("cksum", "--text mode is only supported with --untagged");

        if (raw && text_files_count > 1)
                return text_done(string_diagnostic(address_of text_diagnostic, 1, null, "the --raw option is not supported with multiple files"));

        checksum_raw = raw;
        checksum_base64 = (taking.flags & FILE_FLAG('B')) != 0;

        // The digest an --algorithm names; SHA-2 by the width -l gave it.
        const checksum_algorithm address_to digest = null;

        if (sha2 || sha3)
                digest = bits ? cksum_sha2_width(family, bits)
                              : checksum_algorithms + family + 1;
        else if (algorithm)
                digest = checksum_algorithm_find(algorithm, true);

        if (checking)
        {
                /*
                        Without --algorithm the reference reads whichever
                        algorithm each tagged line names; with one, that
                        algorithm reads every line, tagged or not. sha2 and
                        sha3 read each record at the width its tag or its
                        digits have, whatever -l said: the reference checks
                        the -l it is given and then reads by the record.
                */
                if (algorithm && !digest)
                        return text_done(string_diagnostic(address_of text_diagnostic, 1, algorithm, "algorithm is not supported by the available checksum engine"));

                checksum_sha2_family = sha2 || sha3 ? family : 0;
                checksum_base64_read = true;
                checksum_manifest_files = true;
                checksum_program = (string_address) "cksum";
                checksum_check_label = sha2   ? (string_address) "SHA2"
                                       : sha3 ? (string_address) "SHA3"
                                       : digest ? digest->label
                                                : (string_address) "CRC";
                return text_done(checksum_verify(digest, address_of taking));
        }

        if (algorithm && !string_equals(algorithm, "crc"))
        {
                if (string_equals(algorithm, "crc32b"))
                {
                        b32 answered = cksum_others('c', debug);

                        return answered | cksum_debug_lost;
                }
                if (string_equals(algorithm, "bsd"))
                        return cksum_others('b', debug);
                if (string_equals(algorithm, "sysv"))
                        return cksum_others('s', debug);

                if (digest)
                        return text_done(checksum_generate(digest, 0, checksum_selected.style != 'U', true));

                return text_done(string_diagnostic(address_of text_diagnostic, 1, algorithm, "algorithm is not supported by the available checksum engine"));
        }

        /* --debug names the machinery the CRC is computed with, once. */
        if (debug)
        {
#if X64 && !defined(KERNEL_MODE)
                bool avx512 = cpu_has_avx512 && cpu_has_vpclmul &&
                              cksum_hwcap_allowed("AVX512F") &&
                              cksum_hwcap_allowed("AVX512BW") &&
                              cksum_hwcap_allowed("VPCLMULQDQ");

                cksum_debug_line("avx512", avx512);
                if (!avx512)
                {
                        bool avx2 = cpu_has_avx2 && cpu_has_vpclmul &&
                                    cksum_hwcap_allowed("AVX2") &&
                                    cksum_hwcap_allowed("VPCLMULQDQ");

                        cksum_debug_line("avx2", avx2);
                        if (!avx2)
                                cksum_debug_line("pclmul", cksum_pclmul_usable());
                }
#endif
        }

        b32 answered = cksum_others('p', debug);

        return answered | cksum_debug_lost;
}
