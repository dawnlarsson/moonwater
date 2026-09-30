/*
        awk.

        Its own file because it is its own language: a lexer, a parser, an
        expression evaluator, fields, associative arrays and output formatting.
        Nothing else here needs any of that, and everything else here would
        have to be read around it.

        The reference is the awk on the machine, which is gawk here, and where
        gawk and the standard disagree the tests say which one this followed.
        The regular expression machine is text.c's, above this in the same
        translation unit; there is no second one here.

        Three ceilings, all of them refusals rather than crashes: text.c's
        pool holds a few dozen patterns at once, so a program written around
        more than that compiles the rest one at a time through a small cache;
        a function calls itself until what is left of the machine's stack runs
        out, measured rather than counted; and a value that would need more
        than the arena has says so.
*/

static DEAD_END fn awk_leave(b32 code);
static DEAD_END fn awk_die(string_address word, string_address message);

/*
        One point of failure, because awk makes garbage.

        The size class allocator that used to live here was promoted into
        src/standard/allocator.c and is linked into this same binary, so
        taking is memory_take and giving back is memory_give. What awk adds
        is only the contract on failure: a value that cannot be made is a
        refusal with a message, never a crash.
*/
static address_any awk_take(positive bytes)
{
        address_any made = memory_take(bytes);

        if (!made)
                awk_leave(string_diagnostic(&text_diagnostic, 2, null, "out of memory"));

        return made;
}

static positive awk_size_add(positive left, positive right)
{
        if (right > positive_max - left)
                awk_leave(string_diagnostic(&text_diagnostic, 2, null, "out of memory"));
        return left + right;
}

/*
        Characters, in a UTF-8 locale, where awk counts bytes in any other:
        length, substr, index, match, split with no separator and the widths
        and precisions of %s and %c count what GNU awk counts, a character to
        a well-formed sequence and a byte that is none to itself. Text with
        no byte past ASCII, which is nearly all of it, answers with its byte
        arithmetic untouched, after one mask a block.
*/
static bool text_locale_utf8();
static p8 awk_utf8_known;

static bool awk_wide(string_address text, positive length)
{
        if (!awk_utf8_known)
                awk_utf8_known = text_locale_utf8() ? 2 : 1;

        return awk_utf8_known == 2 && memory_ascii_span(text, length) != length;
}

// The characters of a text that awk_wide said is wide.
static positive awk_characters(string_address text, positive length)
{
        return memory_utf8_span(text, length, positive_max).y;
}

/*
        Strings, counted.

        A value holds a pointer to one of these rather than a copy, so passing
        a field to a function is a store and an increment. The one that is
        empty is a single object everything shares.
*/
typedef struct
{
        b32 refs;
        positive length;
        p8 text[1];
} awk_text;

// The same bytes: a length and a compare, which eleven lookups each spelled
// on two lines of their own.
static inline INLINE bool awk_text_is(awk_text address_to text,
                                      string_address bytes, positive length)
{
        return text->length == length && !memory_compare(text->text, bytes, length);
}

static awk_text awk_empty_text = {1000000000, 0, {0}};

static awk_text address_to awk_text_room(positive length)
{
        awk_text address_to made = (awk_text address_to)awk_take(
            awk_size_add(length, sizeof(awk_text) + 8));

        made->refs = 1;
        made->length = length;
        made->text[length] = end;
        return made;
}

static awk_text address_to awk_text_new(string_address from, positive length)
{
        if (!length)
        {
                awk_empty_text.refs++;
                return address_of awk_empty_text;
        }

        awk_text address_to made = awk_text_room(length);

        memory_copy_apart(made->text, from, length);
        return made;
}

static awk_text address_to awk_text_hold(awk_text address_to which)
{
        if (which)
                which->refs++;

        return which;
}

static fn awk_text_drop(awk_text address_to which)
{
        if (!which)
                return;

        if (--which->refs > 0)
                return;

        memory_give(which);
}

/*
        Arithmetic.

        The freestanding standard layer supplies the hardware square root,
        exact modulo and shared transcendental reducers. Its reduced Horner
        polynomials avoid the division loops formerly duplicated here, and
        its full-range reducers preserve huge angles and special values.
        The two conversions between a double and its decimal spelling are
        the layer's as well: its printf writes a double's digits exactly and
        its strtod rounds correctly, so printing 1e300 in full comes out as
        the reference prints it.
*/
#define awk_from_bits(value) memory_cast(decimal, (value))

static decimal awk_infinity = 0;
static decimal awk_not_a_number = 0;

/*
        A double where a count is wanted.

        Not a cast: a cast of something that is not a number, or of something
        too big for the width, is undefined, and substr of a nan reached the
        allocator asking for two to the sixty fourth bytes.
*/
static b32 awk_whole(decimal value)
{
        if (value != value)
                return 0;

        if (value >= 2147483647.0)
                return 2147483647;

        if (value <= -2147483648.0)
                return -2147483647 - 1;

        return (b32)value;
}

static bipolar awk_whole_wide(decimal value)
{
        if (value != value)
                return 0;

        if (value >= 9223372036854775808.0)
                return bipolar_max;

        if (value <= -9223372036854775808.0)
                return bipolar_min;

        return (bipolar)value;
}

static decimal awk_truncate(decimal value)
{
        if (!decimal_is_finite(value))
                return value;

        if (math_magnitude(value) >= 9223372036854775808.0)
                return value;

        return (decimal)(bipolar)value;
}

static decimal awk_power(decimal base, decimal exponent)
{
        if (exponent == 0 || base == 1)
                return 1;

        if (decimal_is_nan(base) || decimal_is_nan(exponent))
                return awk_not_a_number;

        decimal whole = awk_truncate(exponent);

        if (whole == exponent && math_magnitude(exponent) <= 4096)
        {
                bool invert = exponent < 0;
                positive count = (positive)(invert ? -whole : whole);
                decimal result = 1;
                decimal factor = base;

                while (count)
                {
                        if (count & 1)
                                result *= factor;

                        count >>= 1;

                        if (count)
                                factor *= factor;
                }

                return invert ? 1 / result : result;
        }

        return power(base, exponent);
}

/*
        A double, spelled out exactly.

        The standard layer's printf generates a double's digits rather than
        estimating them, rounds half to even at the cut, and keeps %g's
        trailing zeros under the # flag -- which is what makes print 1e300
        the same three hundred and nine digits the reference prints. The
        field around the number is awk's own: width, zero fill and the
        sign flags go on in awk_sprintf, so the formatter is asked for the
        bare body. That body carries a minus whenever the sign bit is set,
        -0 included, which is how the reference writes one.
*/
/* The places a conversion may ask for: the integer part of the largest
   double, its sign, point and exponent fit in the slack, and a precision
   past the maximum is a room no allocation would give. */
#define AWK_PLACES_SLACK 400
#define AWK_PLACES_MAX ((positive)1 << 26)

static positive awk_write_decimal(decimal value, b32 precision, p8 address_to out,
                                  positive room, p8 conversion, bool alternate)
{
        format_sink sink = {0};
        format_spec spec = {0};

        spec.flags = alternate ? FORMAT_FLAG_ALTERNATE : 0;
        spec.precision = precision;
        spec.conversion = conversion;
        sink.buffer = out;
        sink.capacity = room - 1;

        format_decimal_field(address_of sink, value, address_of spec);
        out[sink.used] = end;
        return sink.used;
}

// The reference spells these with a sign, always, wherever they are written.
/* Every spelling is a sign and three letters, in capitals for the capital
   conversions %E, %F, %G and %X. */
#define AWK_NOT_FINITE_LENGTH 4

static string_address awk_not_finite_name(decimal value, bool capitals)
{
        static const string_address names[] = {"+inf", "-inf", "+nan", "-nan",
                                               "+INF", "-INF", "+NAN", "-NAN"};
        bool nan = decimal_is_nan(value);

        return names[capitals * 4 + nan * 2 +
                     (nan ? decimal_sign_bit(value) != 0 : value < 0)];
}

/*
        A number read out of a string, and how much of the string it took.

        Every rule about what counts as a number here is asked through this
        one: a field is a number to compare against a number only when this
        reaches the end of it. The digits are read by the standard layer's
        strtod, which rounds correctly; what is decided here is the
        reference's grammar around them, which is narrower than C's. Space
        and a sign, then digits with a point and an exponent -- but no 0x,
        so "0x1A" is a zero followed by text, and no words, except that a
        string which is exactly +inf, -inf, +nan or -nan in any case, with
        nothing but space around it, is the value it names.

        The text is terminated at length: a field always is, and the program
        source is given one before the lexer starts.
*/
static decimal awk_scan_number(string_address text, positive length, positive address_to used)
{
        positive at = 0;
        bool marked = false;
        bool negative = false;

        address_to used = 0;

        if (at < length)
                at += string_span_max(text + at, length - at,
                                      string_set_space);

        if (at < length && (text[at] == '+' || text[at] == '-'))
        {
                marked = true;
                negative = text[at] == '-';
                at++;
        }

        if (at >= length)
                return 0;

        if (byte_is_alpha(text[at]))
        {
                bool infinite;
                positive stop = at + 3;

                if (!marked || length - at < 3)
                        return 0;

                if (!string_compare_folded_max(text + at, "inf", 3))
                        infinite = true;
                else if (!string_compare_folded_max(text + at, "nan", 3))
                        infinite = false;
                else
                        return 0;

                // Only the bare word: "+inf5" and "+infinity" are text.
                if (stop < length)
                        stop += string_span_max(text + stop, length - stop,
                                                string_set_space);

                if (stop != length)
                        return 0;

                address_to used = at + 3;

                if (infinite)
                        return negative ? -awk_infinity : awk_infinity;

                // The quiet nan, with the sign that was written.
                return awk_from_bits(negative ? (positive)0xfff8000000000000ull
                                              : (positive)0x7ff8000000000000ull);
        }

        // strtod would read the hexadecimal; the reference stops at the x.
        if (text[at] == '0' && at + 1 < length &&
            (text[at + 1] == 'x' || text[at + 1] == 'X'))
        {
                address_to used = at + 1;
                return negative ? -0.0 : 0.0;
        }

        string_address stopped;
        decimal value = string_to_decimal(text, address_of stopped);

        address_to used = (positive)(stopped - text);
        return value;
}

/*
        Values.

        A value is a number, a string, or both, and which of those it is
        decides what a comparison means. The awkward one is the third state:
        something that came in from outside -- a field, a getline, an argument
        assignment -- and happens to look like a number. Those compare as
        numbers against numbers and as strings against string constants, and
        the difference is visible: a field holding 10.0 equals 10 and does not
        equal "10".
*/
enum
{
        AWK_HAS_NUMBER = 1,
        AWK_HAS_TEXT = 2,
        AWK_STRNUM = 4,
        AWK_UNSET = 8,
        AWK_INPUT = 16,
        // Made by arithmetic or a numeric constant, whatever spelling has
        // been cached beside it since: still a number to a comparison and
        // to a test, where a value assigned to a field and read back after
        // the record was rebuilt used to turn into a string.
        AWK_NUMERIC = 32,
        // The splitter has recorded this field's span in awk_pieces, but no
        // operation has needed its separately-owned value yet. Most awk
        // programs touch only a few fields, so defer their copies until then.
        AWK_FIELD_PENDING = 64
};

typedef struct
{
        decimal number;
        awk_text address_to text;
        p8 state;
} awk_value;

static string_address awk_convfmt();
static string_address awk_ofmt();
static awk_text address_to awk_sprintf(string_address format, positive length,
                                       awk_value address_to arguments, b32 count);

/* Every value replacement owns the same text-reference transition.  Keep the
   invariant once and force the four typed entry shapes back into their hot
   callers, where the constant state and null/zero arguments disappear. */
static inline INLINE fn awk_value_set(awk_value address_to which,
                                      awk_text address_to text,
                                      decimal number, p8 state)
{
        awk_text_drop(which->text);
        which->text = text;
        which->number = number;
        which->state = state;
}

#define awk_value_clear(which)                                              \
        awk_value_set((which), null, 0, AWK_UNSET)
#define awk_set_number(which, number)                                       \
        awk_value_set((which), null, (number), AWK_HAS_NUMBER | AWK_NUMERIC)

// Takes the reference the caller was holding.
#define awk_set_text(which, text)                                           \
        awk_value_set((which), (text), 0, AWK_HAS_TEXT)

static fn awk_set_bytes(awk_value address_to which, string_address from, positive length)
{
        awk_set_text(which, awk_text_new(from, length));
}

// What came from outside is a string which may also be a number.
#define awk_set_input(which, text)                                          \
        awk_value_set((which), (text), 0, AWK_HAS_TEXT | AWK_INPUT)

static fn awk_set_input_bytes(awk_value address_to which, string_address from, positive length)
{
        awk_set_input(which, awk_text_new(from, length));
}

static fn awk_value_copy(awk_value address_to to, awk_value address_to from)
{
        if (to == from)
                return;

        awk_text address_to kept = awk_text_hold(from->text);

        awk_text_drop(to->text);
        to->text = kept;
        to->number = from->number;
        to->state = from->state;
}

/* Input only needs its string/number classification when an operation can
   observe it.  Fields which are merely printed used to pay the complete
   decimal scanner anyway.  Cache both outcomes so truth, arithmetic and a
   later comparison still share exactly one scan. */
static fn awk_classify_input(awk_value address_to which)
{
        if (!(which->state & AWK_INPUT))
                return;

        positive used;
        decimal number = awk_scan_number(which->text->text, which->text->length,
                                         address_of used);

        which->number = number;
        which->state = (which->state & ~AWK_INPUT) | AWK_HAS_NUMBER;

        if (used)
        {
                // isspace on both sides, as the reference awk reads it: a
                // field ending in a carriage return is still a number.
                while (used < which->text->length &&
                       byte_is_space(which->text->text[used]))
                        used++;

                if (used == which->text->length)
                        which->state |= AWK_STRNUM;
        }
}

static decimal awk_to_number(awk_value address_to which)
{
        awk_classify_input(which);

        if (which->state & AWK_HAS_NUMBER)
                return which->number;

        if (which->state & AWK_UNSET)
                return 0;

        positive used;

        which->number = awk_scan_number(which->text->text, which->text->length,
                                        address_of used);
        which->state |= AWK_HAS_NUMBER;
        return which->number;
}

/*
        A number spelled the way awk spells one.

        An integral value is written out in full whatever the format says --
        which is why 2^53 and 1e300 come out as digits rather than as 9e+15 --
        and everything else goes through CONVFMT or OFMT.
*/
static awk_text address_to awk_text_of_number(decimal number, string_address format)
{
        p8 room[512];

        if (!decimal_is_finite(number))
        {
                string_address name = awk_not_finite_name(number, false);

                return awk_text_new(name, string_length(name));
        }

        if (number == awk_truncate(number))
        {
                if (number > -1e18 && number < 1e18)
                {
                        bipolar whole = (bipolar)number;
                        positive at = bipolar_into(room, whole);

                        return awk_text_new(room, at);
                }

                positive at = awk_write_decimal(number, 0, room, sizeof(room), 'f', false);

                return awk_text_new(room, at);
        }

        awk_value one;

        one.text = null;
        one.number = number;
        one.state = AWK_HAS_NUMBER;

        awk_text address_to made = awk_sprintf(format, string_length(format), address_of one, 1);

        awk_text_drop(one.text);
        return made;
}

static awk_text address_to awk_to_text(awk_value address_to which)
{
        if (which->state & AWK_HAS_TEXT)
                return which->text;

        if (which->state & AWK_UNSET)
        {
                awk_empty_text.refs++;
                which->text = address_of awk_empty_text;
                which->state |= AWK_HAS_TEXT;
                return which->text;
        }

        which->text = awk_text_of_number(which->number, awk_convfmt());
        which->state |= AWK_HAS_TEXT;
        return which->text;
}

// What print writes, which differs from the above in one variable's name.
static awk_text address_to awk_to_output_text(awk_value address_to which)
{
        if (which->state & AWK_HAS_TEXT)
                return awk_text_hold(which->text);

        if (which->state & AWK_UNSET)
                return awk_text_hold(address_of awk_empty_text);

        return awk_text_of_number(which->number, awk_ofmt());
}

static bool awk_truth(awk_value address_to which)
{
        awk_classify_input(which);

        if (which->state & AWK_UNSET)
                return false;

        if ((which->state & AWK_HAS_TEXT) && !(which->state & AWK_NUMERIC))
        {
                if (which->state & AWK_STRNUM)
                        return awk_to_number(which) != 0;

                return which->text->length != 0;
        }

        return which->number != 0;
}

static bool awk_numeric_side(awk_value address_to which)
{
        awk_classify_input(which);

        if (which->state & (AWK_UNSET | AWK_STRNUM | AWK_NUMERIC))
                return true;

        return (which->state & AWK_HAS_NUMBER) && !(which->state & AWK_HAS_TEXT);
}

// Below, above, the same, or -- for a value that is not a number at all --
// none of the three, which is what two says.
static b32 awk_compare_numbers(decimal a, decimal b)
{
        if (a != a || b != b)
                return 2;
        return a < b ? -1 : (a > b ? 1 : 0);
}

static b32 awk_compare(awk_value address_to left, awk_value address_to right)
{
        if (awk_numeric_side(left) && awk_numeric_side(right))
                return awk_compare_numbers(awk_to_number(left), awk_to_number(right));

        awk_text address_to a = awk_to_text(left);
        awk_text address_to b = awk_to_text(right);
        positive shortest = a->length < b->length ? a->length : b->length;
        b32 order = memory_compare(a->text, b->text, shortest);

        if (order)
                return order < 0 ? -1 : 1;

        return a->length == b->length ? 0 : (a->length < b->length ? -1 : 1);
}

/*
        Arrays, which are the only kind awk has.

        Keys are strings even when they were written as numbers, and the
        spelling a number takes as a key is CONVFMT's -- except for an
        integral one, which is its digits. a[1] and a["1"] are one element.
*/
typedef struct awk_slot
{
        struct awk_slot address_to next;
        awk_text address_to key;
        positive hash;
        awk_value value;
} awk_slot;

typedef struct
{
        awk_slot address_to address_to buckets;
        positive width;
        positive count;
} awk_array;

static positive awk_hash(string_address text, positive length)
{
        // Four is the first length where the four-byte hardware floor beats
        // the dependent C chain on every architecture. One-byte keys stay
        // inline: the call is 102% on ARM64 and RV64 there, while four bytes
        // are 82%/92%/99% on x86-64/ARM64/RV64 and the lead grows after it.
        if (length >= 4)
                return memory_hash_33(text, length);

        positive value = 5381;

        for (positive i = 0; i < length; i++)
                value = value * 33 + text[i];

        return value;
}

static awk_array address_to awk_array_new()
{
        awk_array address_to made = (awk_array address_to)awk_take(sizeof(awk_array));

        made->width = 16;
        made->count = 0;
        made->buckets = (awk_slot address_to address_to)awk_take(made->width * sizeof(address_any));

        memory_fill(made->buckets, 0,
                    made->width * sizeof(address_any));

        return made;
}

static fn awk_array_grow(awk_array address_to which)
{
        if (which->width > positive_max / (4 * sizeof(address_any)))
                awk_leave(string_diagnostic(&text_diagnostic, 2, null, "out of memory"));

        positive width = which->width * 4;
        awk_slot address_to address_to buckets =
            (awk_slot address_to address_to)awk_take(width * sizeof(address_any));

        memory_fill(buckets, 0, width * sizeof(address_any));

        for (positive i = 0; i < which->width; i++)
        {
                awk_slot address_to slot = which->buckets[i];

                while (slot)
                {
                        awk_slot address_to next = slot->next;
                        positive where = slot->hash & (width - 1);

                        slot->next = buckets[where];
                        buckets[where] = slot;
                        slot = next;
                }
        }

        memory_give(which->buckets);
        which->buckets = buckets;
        which->width = width;
}

static PURE awk_slot address_to awk_array_find_hash(awk_array address_to which,
                                               string_address key,
                                               positive length, positive hash)
{
        positive where = hash & (which->width - 1);

        for (awk_slot address_to slot = which->buckets[where]; slot; slot = slot->next)
                if (slot->hash == hash && awk_text_is(slot->key, key, length))
                        return slot;

        return null;
}

static PURE awk_slot address_to awk_array_find(awk_array address_to which,
                                          string_address key, positive length)
{
        return awk_array_find_hash(which, key, length, awk_hash(key, length));
}

static awk_slot address_to awk_array_place(awk_array address_to which, string_address key,
                                           positive length)
{
        positive hash = awk_hash(key, length);
        awk_slot address_to found = awk_array_find_hash(which, key, length, hash);

        if (found)
                return found;

        if (which->count >= which->width * 2)
                awk_array_grow(which);

        positive where = hash & (which->width - 1);
        awk_slot address_to slot = (awk_slot address_to)awk_take(sizeof(awk_slot));

        slot->key = awk_text_new(key, length);
        slot->hash = hash;
        slot->value.text = null;
        slot->value.number = 0;
        slot->value.state = AWK_UNSET;
        slot->next = which->buckets[where];
        which->buckets[where] = slot;
        which->count++;
        return slot;
}

static fn awk_array_remove(awk_array address_to which, string_address key, positive length)
{
        positive hash = awk_hash(key, length);
        positive where = hash & (which->width - 1);
        awk_slot address_to address_to link = address_of which->buckets[where];

        while (address_to link)
        {
                awk_slot address_to slot = address_to link;

                if (slot->hash == hash && awk_text_is(slot->key, key, length))
                {
                        address_to link = slot->next;
                        awk_text_drop(slot->key);
                        awk_text_drop(slot->value.text);
                        memory_give(slot);
                        which->count--;
                        return;
                }

                link = address_of slot->next;
        }
}

static fn awk_array_empty(awk_array address_to which)
{
        for (positive i = 0; i < which->width; i++)
        {
                awk_slot address_to slot = which->buckets[i];

                while (slot)
                {
                        awk_slot address_to next = slot->next;

                        awk_text_drop(slot->key);
                        awk_text_drop(slot->value.text);
                        memory_give(slot);
                        slot = next;
                }

                which->buckets[i] = null;
        }

        which->count = 0;
}

/*
        Variables.

        A name is resolved once, where the program is parsed, into either a
        slot in the current function's frame or a slot in the one global
        table. Nothing looks a name up while the program runs.

        Thirteen of the globals mean something to the machinery underneath,
        and assigning to those has to be noticed: NF rebuilds the record, RS
        changes what a record is, FS changes what a field is.
*/
enum
{
        AWK_CELL_UNKNOWN = 0,
        AWK_CELL_SCALAR,
        AWK_CELL_ARRAY
};

typedef struct awk_cell
{
        p8 kind;
        bool owned;
        struct awk_cell address_to link;
        awk_value value;
        awk_array address_to array;
} awk_cell;

enum
{
        AWK_PLAIN = 0,
        AWK_NR,
        AWK_NF,
        AWK_FNR,
        AWK_FS,
        AWK_OFS,
        AWK_ORS,
        AWK_RS,
        AWK_FILENAME,
        AWK_SUBSEP,
        AWK_RSTART,
        AWK_RLENGTH,
        AWK_CONVFMT,
        AWK_OFMT
};

#define AWK_GLOBALS_MAX 1024
#define AWK_FRAME_MAX 32768
#define AWK_LOCALS_MAX 128

static awk_cell awk_globals[AWK_GLOBALS_MAX];
static awk_text address_to awk_global_names[AWK_GLOBALS_MAX];
static p8 awk_global_meaning[AWK_GLOBALS_MAX];
static b32 awk_global_count;

static awk_cell (address_to awk_stack_held)[AWK_FRAME_MAX];
#define awk_stack UTILITY_HELD(awk_stack)
static b32 awk_frame;
static b32 awk_frame_size;

/*
        How much of the machine's stack is left.

        A function that calls itself uses a frame of this program's own and a
        handful of the C ones under it, and the C ones are what runs out
        first. Measuring the address of a local against the address of one
        taken at the start says how much has gone, so the answer is the same
        whatever the shape of the recursion, and a script that recurses too
        far is told so rather than dying on a page that is not there.
*/
// One recursion of the interpreter is a few hundred bytes, and this is the
// room awk_call keeps above the interpreter's floor so that it refuses a
// deep call first.
#define AWK_WALK_SLACK (64u << 10)

static positive awk_stack_start;
static positive awk_stack_room;
// awk_stack_start - awk_stack_room, so a walker compares its own frame
// against one loaded word.
static positive awk_stack_floor;
// The interpreter's own floor, a little below it. A call checks the floor
// above from a frame the eval frames beneath it have already passed, so at
// one shared floor the interpreter would refuse a deep recursion first and
// awk_call's diagnostic, which names the function, would never be reached.
static positive awk_walk_floor;

static b32 awk_where_environ;
static b32 awk_where_argv;
static b32 awk_where_argc;

static fn awk_flush_everything();

static b32 awk_global_find(string_address name, positive length)
{
        for (b32 i = 0; i < awk_global_count; i++)
                if (awk_text_is(awk_global_names[i], name, length))
                        return i;

        if (awk_global_count == AWK_GLOBALS_MAX)
                awk_die(null, "too many variables");

        b32 which = awk_global_count++;

        awk_global_names[which] = awk_text_new(name, length);
        awk_globals[which].kind = AWK_CELL_UNKNOWN;
        awk_globals[which].value.state = AWK_UNSET;
        return which;
}

static awk_cell address_to awk_cell_of(b32 index)
{
        return index < 0 ? address_of awk_stack[awk_frame + (-index - 1)]
                         : address_of awk_globals[index];
}

static awk_array address_to awk_cell_array(awk_cell address_to cell)
{
        while (cell->link)
                cell = cell->link;

        if (!cell->array)
        {
                cell->array = awk_array_new();
                cell->owned = true;
        }

        cell->kind = AWK_CELL_ARRAY;
        return cell->array;
}

static string_address awk_global_string(b32 which)
{
        return awk_to_text(address_of awk_globals[which].value)->text;
}

static b32 awk_where_fs, awk_where_ofs, awk_where_ors, awk_where_rs;
static b32 awk_where_nr, awk_where_nf, awk_where_fnr, awk_where_filename;
static b32 awk_where_subsep, awk_where_rstart, awk_where_rlength;
static b32 awk_where_convfmt, awk_where_ofmt;

static string_address awk_convfmt()
{
        return awk_global_string(awk_where_convfmt);
}

static string_address awk_ofmt()
{
        return awk_global_string(awk_where_ofmt);
}

static awk_text address_to awk_special_text(b32 which)
{
        return awk_to_text(address_of awk_globals[which].value);
}

static fn awk_set_global_number(b32 which, decimal value)
{
        awk_set_number(address_of awk_globals[which].value, value);
}

static decimal awk_global_number(b32 which)
{
        return awk_to_number(address_of awk_globals[which].value);
}

/*
        Regular expressions, out of text.c's machine.

        The ones written in the program are compiled once and kept forever.
        The ones built at run time -- a string used where a pattern goes --
        share a small cache in the pool above the kept ones, and when it fills
        the pool is wound back to where the kept ones end rather than to zero,
        which is what keeps a program compiled at parse time valid for the
        whole run.
*/
#define AWK_REGEX_KEPT 32
#define AWK_REGEX_CACHED 6

static regex_program awk_regex_kept[AWK_REGEX_KEPT];
static b32 awk_regex_kept_count;

static regex_program awk_regex_cache[AWK_REGEX_CACHED];
static awk_text address_to awk_regex_cache_key[AWK_REGEX_CACHED];
static b32 awk_regex_cache_count;

static rx_mark awk_regex_mark_pool;

static awk_text address_to awk_regex_escapes(string_address pattern);
static DEAD_END fn awk_syntax(string_address reason);
static bool awk_parsing;

static fn awk_regex_build(regex_program address_to into, string_address pattern)
{
        awk_text address_to plain =
            string_first_of(pattern, '\\') ? awk_regex_escapes(pattern) : null;

        if (!regex_compile(plain ? plain->text : pattern, true, false, true,
                           text_regex_policy()))
        {
                // Written in the program, it is the program that is wrong.
                if (awk_parsing)
                        awk_syntax("invalid regular expression");

                awk_die(pattern, "invalid regular expression");
        }

        awk_text_drop(plain);
        regex_keep(into);
}

static regex_program address_to awk_regex_keep(string_address pattern)
{
        if (awk_regex_kept_count == AWK_REGEX_KEPT)
                return null;

        regex_program address_to into = address_of awk_regex_kept[awk_regex_kept_count++];

        awk_regex_build(into, pattern);
        return into;
}


static regex_program address_to awk_regex_dynamic(awk_text address_to pattern)
{
        for (b32 i = 0; i < awk_regex_cache_count; i++)
                if (awk_text_is(awk_regex_cache_key[i], pattern->text,
                                pattern->length))
                        return address_of awk_regex_cache[i];

        if (awk_regex_cache_count == AWK_REGEX_CACHED)
        {
                for (b32 i = 0; i < awk_regex_cache_count; i++)
                        awk_text_drop(awk_regex_cache_key[i]);

                awk_regex_cache_count = 0;
                regex_retained = awk_regex_mark_pool;
                regex_pool.used = regex_retained;
        }

        b32 which = awk_regex_cache_count++;

        awk_regex_cache_key[which] = awk_text_hold(pattern);
        awk_regex_build(address_of awk_regex_cache[which], pattern->text);
        return address_of awk_regex_cache[which];
}

/*
        The record and its fields.

        $0 and $1..$NF are one array of values with the record at zero, and
        either side can be the stale one: assigning to a field marks the
        record for rebuilding with OFS, assigning to the record splits it
        again. Everything a field holds arrived from outside, so every field
        is a value that may compare as a number.
*/
static awk_value address_to awk_fields;
static positive awk_fields_room;
static b32 awk_nf;
static bool awk_record_stale;
static awk_text address_to awk_record_separator;
static awk_value awk_field_nothing;

typedef struct
{
        positive start;
        positive length;
} awk_piece;

static awk_piece address_to awk_pieces;
static positive awk_piece_count;
static positive awk_piece_room;
// Constant fields named by the parsed program. A computed field needs the
// original eager policy; otherwise fields not named here can remain spans.
static positive awk_fields_fixed;
static bool awk_fields_computed;

static HOT fn awk_piece_add(positive start, positive length)
{
        if (unlikely(awk_piece_count >= awk_piece_room))
        {
                positive bytes = awk_piece_room * sizeof(awk_piece);

                if (awk_piece_count >= positive_max / sizeof(awk_piece) ||
                    !memory_resize_reserve(
                        address_of awk_pieces, address_of bytes,
                        (awk_piece_count + 1) * sizeof(awk_piece),
                        64 * sizeof(awk_piece)))
                        awk_leave(string_diagnostic(&text_diagnostic, 2, null, "out of memory"));

                awk_piece_room = bytes / sizeof(awk_piece);
        }

        awk_pieces[awk_piece_count++] = (awk_piece){start, length};
}

// The field edges of a record split by the default separator.
#define AWK_EDGE_FIELDS 256
static p32 awk_edges[2 * AWK_EDGE_FIELDS];

/*
        One splitter, for fields and for split().

        A separator of one space is the rule nobody writes down the same way
        twice: leading and trailing blanks are not separators at all, and a
        run of them is one. Any other single character is itself and not a
        pattern -- -F. cuts on dots, not on everything.
*/
static fn awk_split_pieces(string_address text, positive length, string_address separator,
                           positive separator_length, bool paragraph, bool as_pattern)
{
        awk_piece_count = 0;

        // A string of one space is the default splitting; a pattern of one
        // space, written / /, is a space and nothing more.
        //
        // The runs outside space, tab and newline come from one pass of
        // memory_offsets_fields_blank a record, where two string_span_max a
        // field were a quarter of awk '{print $2}'.
        if (separator_length == 1 && separator[0] == ' ' && !as_pattern)
        {
                positive at = 0;

                for (;;)
                {
                        positive found = memory_offsets_fields_blank(
                            awk_edges, text + at, length - at, '\n', AWK_EDGE_FIELDS);

                        for (positive f = 0; f < found; f++)
                                awk_piece_add(at + awk_edges[2 * f],
                                              awk_edges[2 * f + 1] - awk_edges[2 * f]);

                        if (found < AWK_EDGE_FIELDS)
                                break;

                        at += awk_edges[2 * found - 1];
                }

                return;
        }

        if (!length)
                return;

        if (!separator_length)
        {
                if (awk_wide(text, length))
                {
                        for (positive i = 0; i < length;)
                        {
                                positive size = memory_utf8_span(text + i, length - i, 1).x;

                                awk_piece_add(i, size);
                                i += size;
                        }

                        return;
                }

                for (positive i = 0; i < length; i++)
                        awk_piece_add(i, 1);

                return;
        }

        //      A one-byte separator is a list of offsets, and lib.c writes
        //      the whole list in one pass: a hunt per field paid a call and
        //      its setup for every field, which on short fields -- a passwd
        //      line, a CSV row -- was most of the split. Paragraph mode stops
        //      at a newline as well, which is the second byte; otherwise the
        //      separator is both.
        if (separator_length == 1 && !as_pattern)
        {
                p32 cuts[256];
                positive start = 0;
                positive from = 0;
                p8 also = paragraph ? (p8)'\n' : separator[0];

                for (;;)
                {
                        positive found = memory_offsets_of_either(
                                cuts, text + from, length - from, separator[0],
                                also, array_count(cuts));

                        for (positive i = 0; i < found; i++)
                        {
                                positive cut = from + cuts[i];

                                awk_piece_add(start, cut - start);
                                start = cut + 1;
                        }
                        if (found < array_count(cuts))
                                break;
                        from += cuts[found - 1] + 1;
                }

                awk_piece_add(start, length - start);
                return;
        }

        awk_text address_to pattern = awk_text_new(separator, separator_length);
        regex_program address_to program = awk_regex_dynamic(pattern);
        positive start = 0;
        positive at = 0;

        awk_text_drop(pattern);
        regex_current = *program;

        while (at < length)
        {
                positive cut = TEXT_UNSET;
                positive stop = 0;

                if (regex_find(REGEX_LONGEST, text, length, at))
                {
                        cut = regex_slots[0];
                        stop = regex_slots[1];

                        // An empty match separates nothing; it would cut the
                        // record at every byte and never move forward.
                        if (stop == cut)
                        {
                                cut = TEXT_UNSET;

                                for (positive scan = at; scan < length && cut == TEXT_UNSET; scan++)
                                        if (regex_find(REGEX_LONGEST, text, length, scan) &&
                                            regex_slots[1] > regex_slots[0])
                                        {
                                                cut = regex_slots[0];
                                                stop = regex_slots[1];
                                        }
                        }
                }

                // Paragraph mode adds no newline here: with a pattern for
                // FS the newline is only a byte, in the reference awk as in
                // its POSIX mode; the one-byte separators above add it.
                if (cut == TEXT_UNSET)
                        break;

                awk_piece_add(start, cut - start);
                start = stop;
                at = stop;
        }

        awk_piece_add(start, length - start);
}

static fn awk_fields_reserve(positive want)
{
        if (want < awk_fields_room)
                return;

        positive before = awk_fields_room;
        positive bytes = before * sizeof(awk_value);
        if (want >= positive_max / sizeof(awk_value) ||
            !memory_resize_reserve(address_of awk_fields, address_of bytes,
                                    (want + 1) * sizeof(awk_value),
                                    64 * sizeof(awk_value)))
                awk_leave(string_diagnostic(&text_diagnostic, 2, null, "out of memory"));
        awk_fields_room = bytes / sizeof(awk_value);
        for (positive i = before; i < awk_fields_room; i++)
                awk_fields[i] = (awk_value){.state = AWK_UNSET};
}

static string_address awk_separator(b32 which, positive address_to length)
{
        awk_text address_to text = awk_special_text(which);

        address_to length = text->length;
        return text->text;
}

static bool awk_paragraph_mode()
{
        positive length;

        awk_separator(awk_where_rs, address_of length);
        return length == 0;
}

static fn awk_field_from_piece(awk_value address_to field,
                               awk_text address_to record, awk_piece piece)
{
        // A one-field record already is the exact immutable string wanted by
        // $1. Sharing it avoids an allocation and copy; assigning either
        // value still replaces only that value.
        if (!piece.start && piece.length == record->length)
                awk_set_input(field, awk_text_hold(record));
        else
                awk_set_input_bytes(field, record->text + piece.start,
                                    piece.length);
}

static fn awk_split_record()
{
        // This record is authoritative again. A separator retained from a
        // field-authored preceding record must not affect this one.
        if (awk_record_separator)
        {
                awk_text address_to prior = awk_record_separator;

                awk_record_separator = null;
                awk_text_drop(prior);
        }

        positive separator_length;
        string_address separator = awk_separator(awk_where_fs, address_of separator_length);
        awk_text address_to record = awk_to_text(address_of awk_fields[0]);

        awk_split_pieces(record->text, record->length, separator, separator_length,
                         awk_paragraph_mode(), false);

        awk_fields_reserve(awk_piece_count + 1);

        bool eager = awk_fields_computed;

        if (!eager && awk_piece_count <= 63)
        {
                positive all = awk_piece_count == 63
                                   ? positive_max - 1
                                   : (((positive)1 << (awk_piece_count + 1)) - 2);

                eager = (awk_fields_fixed & all) == all;
        }

        for (positive i = 0; i < awk_piece_count; i++)
        {
                awk_value address_to field = address_of awk_fields[i + 1];

                if (eager || (i < 63 && (awk_fields_fixed & ((positive)2 << i))))
                        awk_field_from_piece(field, record, awk_pieces[i]);
                else
                        awk_value_set(field, null, 0, AWK_FIELD_PENDING);
        }

        for (positive i = awk_piece_count + 1; i <= (positive)awk_nf; i++)
                awk_value_clear(address_of awk_fields[i]);

        awk_nf = (b32)awk_piece_count;
        awk_set_global_number(awk_where_nf, (decimal)awk_nf);
        awk_record_stale = false;
}

static awk_value address_to awk_field(b32 which);

static fn awk_record_rebuild()
{
        string_address separator = awk_record_separator ? awk_record_separator->text
                                                        : (string_address) "";
        positive length = awk_record_separator ? awk_record_separator->length : 0;
        positive total = 0;

        for (b32 i = 1; i <= awk_nf; i++)
                total = awk_size_add(total, awk_size_add(awk_to_text(awk_field(i))->length, length));

        awk_text address_to made = awk_text_room(total ? total : 1);
        positive at = 0;

        for (b32 i = 1; i <= awk_nf; i++)
        {
                awk_text address_to piece = awk_to_text(awk_field(i));

                if (i > 1)
                {
                        memory_copy_apart(made->text + at, separator, length);
                        at += length;
                }

                memory_copy_apart(made->text + at, piece->text, piece->length);
                at += piece->length;
        }

        made->length = at;
        made->text[at] = end;
        awk_set_input(address_of awk_fields[0], made);
        awk_record_stale = false;
}

static fn awk_record_set(string_address text, positive length)
{
        awk_fields_reserve(1);
        awk_set_input_bytes(address_of awk_fields[0], text, length);
        awk_split_record();
}

static awk_value address_to awk_field(b32 which)
{
        if (which < 0)
                awk_die(null, "attempt to access a field before the first");

        if (!which)
        {
                if (awk_record_stale)
                        awk_record_rebuild();

                awk_fields_reserve(1);
                return address_of awk_fields[0];
        }

        if (which > awk_nf)
        {
                awk_value_clear(address_of awk_field_nothing);
                awk_set_bytes(address_of awk_field_nothing, "", 0);
                return address_of awk_field_nothing;
        }

        awk_value address_to field = address_of awk_fields[which];

        if (field->state & AWK_FIELD_PENDING)
        {
                awk_piece piece = awk_pieces[which - 1];
                awk_text address_to record = awk_to_text(address_of awk_fields[0]);

                awk_field_from_piece(field, record, piece);
        }

        return field;
}

static fn awk_fields_materialize()
{
        for (b32 i = 1; i <= awk_nf; i++)
                if (awk_fields[i].state & AWK_FIELD_PENDING)
                        awk_field(i);
}

static fn awk_field_grow(b32 want)
{
        awk_fields_reserve((positive)want + 1);

        for (b32 i = awk_nf + 1; i <= want; i++)
                awk_set_bytes(address_of awk_fields[i], "", 0);

        if (want > awk_nf)
        {
                awk_nf = want;
                awk_set_global_number(awk_where_nf, (decimal)awk_nf);
        }
}

static fn awk_field_written(b32 which)
{
        if (!which)
        {
                awk_split_record();
                return;
        }

        if (which > awk_nf)
                awk_field_grow(which);

        awk_text_drop(awk_record_separator);
        awk_record_separator = awk_text_hold(awk_special_text(awk_where_ofs));
        awk_record_stale = true;
}

static fn awk_nf_written(b32 want)
{
        if (want < 0)
                awk_die(null, "NF set to a negative value");

        awk_fields_reserve((positive)want + 1);

        for (b32 i = want + 1; i <= awk_nf; i++)
                awk_value_clear(address_of awk_fields[i]);

        for (b32 i = awk_nf + 1; i <= want; i++)
                awk_set_bytes(address_of awk_fields[i], "", 0);

        awk_nf = want;
        awk_set_global_number(awk_where_nf, (decimal)awk_nf);
        awk_text_drop(awk_record_separator);
        awk_record_separator = awk_text_hold(awk_special_text(awk_where_ofs));
        awk_record_rebuild();
}

/*
        Where output goes, and where getline reads from.

        Both are tables keyed by the string that named them, because that is
        what close() is given: print > "out" and getline < "out" are two
        entries under one name and close("out") ends both. A name that starts
        with | is a command, which means a pipe and a child.
*/
#define AWK_READ_CHUNK 65536

enum
{
        AWK_TO_FILE = 0,
        AWK_TO_APPEND,
        AWK_TO_PIPE
};

typedef struct
{
        awk_text address_to name;
        b32 handle;
        p8 kind;
        bool live;
        bipolar child;
        positive used;
        p8 buffer[8192];
} awk_writer;

typedef struct
{
        awk_text address_to name;
        b32 handle;
        bool live;
        bool pipe;
        bool ended;
        bool failed;
        bipolar child;
        p8 address_to data;
        positive room;
        positive filled;
        positive at;
} awk_reader;

// Both tables grow as the program opens; an entry is made once and reused,
// so a pointer into either stays good while the table moves.
static awk_writer address_to address_to awk_writers;
static positive awk_writers_room;
static b32 awk_writer_count;
static awk_reader address_to address_to awk_readers;
static positive awk_readers_room;
static b32 awk_reader_count;
static awk_writer awk_standard_out;
static bool awk_write_failed;

/*
        A write into a command's pipe whose reader has gone is refused, not
        fatal to the program: the reference ignores SIGPIPE for everything
        but standard output, so print | "cmd" into a command that has
        exited ends with "print to "cmd" failed" and a status of 2, where
        the signal ended this one with 141 or, when it arrived late, with 0
        and the lines lost unsaid. The signal is set aside only around the
        write, so the commands awk starts are started with it as they found
        it and standard output still dies of it quietly, as the reference's
        does.
*/
#define AWK_SIGPIPE 13

static bool awk_writer_quiet(awk_writer address_to which, positive address_to previous)
{
        return which != address_of awk_standard_out && which->handle != 2 &&
               system_signal_install(AWK_SIGPIPE, 1, 0, 0, previous);
}

static fn awk_writer_loud(positive address_to previous)
{
        system_signal_action(AWK_SIGPIPE, previous, null, 8);
}

static bool awk_writer_flush(awk_writer address_to which)
{
        positive previous[4];
        bool quiet = awk_writer_quiet(which, previous);
        bool flushed = buffered_flush((positive)which->handle, which->buffer,
                                      address_of which->used);

        if (quiet)
                awk_writer_loud(previous);
        if (flushed)
                return true;

        //      What is left in a command's pipe when it is closed, flushed or
        //      the program ends goes nowhere once the command has gone, and
        //      gawk says nothing of it and still ends with 0: only a print
        //      or printf that finds the reader gone is refused.
        if (which->kind == AWK_TO_PIPE && which != address_of awk_standard_out)
        {
                which->used = 0;
                return true;
        }

        awk_write_failed = true;
        return false;
}

static bool awk_writer_put(awk_writer address_to which, string_address data, positive length)
{
        if (which->handle == 2)
        {
                awk_writer_flush(address_of awk_standard_out);

                if (system_write_all(2, (address_any)data, length) == length)
                        return true;

                awk_write_failed = true;
                return false;
        }

        positive previous[4];
        bool quiet = awk_writer_quiet(which, previous);
        bool written = buffered_write((positive)which->handle, which->buffer,
                                      sizeof(which->buffer), address_of which->used,
                                      (address_any)data, length);

        if (quiet)
                awk_writer_loud(previous);
        if (written)
                return true;

        awk_write_failed = true;
        return false;
}

static DEAD_END fn awk_leave(b32 code);

/*
        A print or printf the kernel refused is the end of the program, as it
        is for the reference awk: a loop printing into a full disk, a closed
        descriptor or a pipe nobody reads would otherwise print for ever.
*/
static DEAD_END fn awk_write_refused(awk_writer address_to which,
                                     string_address verb)
{
        string_address name = which == address_of awk_standard_out
                                  ? (string_address) "standard output"
                                  : which->name->text;

        awk_leave(string_report(writer_stderr, 2, "%s: %s to \"%s\" failed\n",
                                text_name, verb, name));
}

static fn awk_flush_everything()
{
        awk_writer_flush(address_of awk_standard_out);

        for (b32 i = 0; i < awk_writer_count; i++)
                if (awk_writers[i]->live)
                        awk_writer_flush(awk_writers[i]);
}

/*
        A command, in a child, with one end of a pipe.

        There is no exec.c under this -- awk.c is included before it -- so the
        three syscalls are made here. /bin/sh -c is what the standard says the
        command is handed to.
*/
static string_address address_to awk_child_environment;
static positive awk_child_environment_room;

static bipolar awk_spawn(string_address command, b32 into, b32 out_of)
{
        string_address words[4];
        bipolar child;

        words[0] = "/bin/sh";
        words[1] = "-c";
        words[2] = (string_address)command;
        words[3] = null;

        awk_flush_everything();

        child = system_fork();

        if (child)
                return child;

        if (into >= 0)
        {
                if (shell_child_fd_move(into, 0) < 0)
                        exit(126);
        }

        if (out_of >= 0)
        {
                if (shell_child_fd_move(out_of, 1) < 0)
                        exit(126);
        }

        for (b32 i = 0; i < awk_writer_count; i++)
                if (awk_writers[i]->live && awk_writers[i]->handle > 2)
                        system_close(awk_writers[i]->handle);

        for (b32 i = 0; i < awk_reader_count; i++)
                if (awk_readers[i]->live && awk_readers[i]->handle > 2)
                        system_close(awk_readers[i]->handle);

        (void)shell_exec_file((string_address)"/bin/sh", words, 3,
                              awk_child_environment);
        exit(127);
        return -1;
}

static b32 awk_wait_for(bipolar child)
{
        positive status = 0;

        if (!child)
                return 0;
        if (child < 0 ||
            system_wait4_retry(child, address_of status, 0, null) < 0)
                return -1;

        return wait_status_code_base(status, 256);
}

static bool awk_name_is(awk_text address_to name, string_address what)
{
        return awk_text_is(name, what, string_length(what));
}

/* The names standard output answers to, and whether a redirection opened
   each: close() answers 0 once for a name a rule opened, -1 otherwise. */
static const string_address awk_standard_names[2] = {"/dev/stdout", "-"};
static bool awk_standard_named[2];

static awk_writer address_to awk_writer_for(awk_text address_to name, p8 kind)
{
        b32 free_slot = -1;

        /*
                Standard output under another name is still standard output.
                A second buffer for it would put what a rule wrote through it
                after everything the rules around it wrote directly.
        */
        for (b32 i = 0; kind != AWK_TO_PIPE && i < 2; i++)
                if (awk_name_is(name, awk_standard_names[i]))
                {
                        awk_standard_named[i] = true;
                        return address_of awk_standard_out;
                }

        for (b32 i = 0; i < awk_writer_count; i++)
        {
                if (awk_writers[i]->live)
                {
                        if (awk_text_is(awk_writers[i]->name, name->text, name->length))
                                return awk_writers[i];
                }
                else if (free_slot < 0)
                        free_slot = i;
        }

        if (free_slot < 0)
        {
                if (!shell_array_room(awk_writers, awk_writers_room,
                                      (positive)awk_writer_count + 1))
                        awk_die(null, "too many open files");

                free_slot = awk_writer_count++;
                awk_writers[free_slot] = (awk_writer address_to)awk_take(sizeof(awk_writer));
                memory_fill(awk_writers[free_slot], 0, sizeof(awk_writer));
        }

        awk_writer address_to made = awk_writers[free_slot];

        made->name = awk_text_hold(name);
        made->used = 0;
        made->kind = kind;
        made->child = 0;
        made->live = true;

        if (kind == AWK_TO_PIPE)
        {
                b32 ends[2];

                if (system_pipe(ends, SHELL_PIPE_CLOSE_ON_EXEC) < 0)
                        awk_die(null, "cannot open pipe");

                // The child has to be told about its own end of the pipe
                // before it is made, or it inherits the writing end and the
                // command never sees the input stop.
                made->handle = ends[1];
                made->child = awk_spawn(name->text, ends[0], -1);
                system_close(ends[0]);
                return made;
        }

        if (awk_name_is(name, "/dev/stderr"))
        {
                made->handle = 2;
                return made;
        }

        bipolar handle = text_open_handle(name->text,
                                          kind == AWK_TO_APPEND ? TEXT_APPEND : TEXT_WRITE, 0666);

        if (handle < 0)
                awk_die(name->text, "cannot open for writing");

        made->handle = (b32)handle;
        return made;
}

static bool awk_reader_fill(awk_reader address_to which)
{
        if (which->ended)
                return false;

        // What has been consumed is moved out before more is read: kept,
        // the buffer grew with the whole of the input rather than with the
        // unread tail of it.
        if (which->at)
        {
                memory_copy(which->data, which->data + which->at,
                            which->filled - which->at);
                which->filled -= which->at;
                which->at = 0;
        }

        if (!memory_resize_reserve(address_of which->data, address_of which->room,
                                    awk_size_add(which->filled, AWK_READ_CHUNK + 1),
                                    AWK_READ_CHUNK * 2))
                awk_leave(string_diagnostic(&text_diagnostic, 2, null, "out of memory"));

        bipolar got = system_read_retry((positive)which->handle,
                                        which->data + which->filled,
                                        AWK_READ_CHUNK);

        if (got <= 0)
        {
                which->ended = true;
                which->failed = got < 0;
                return false;
        }

        which->filled += (positive)got;
        return true;
}

static awk_reader address_to awk_reader_for(awk_text address_to name, bool pipe)
{
        b32 free_slot = -1;

        for (b32 i = 0; i < awk_reader_count; i++)
        {
                if (awk_readers[i]->live)
                {
                        if (awk_readers[i]->pipe == pipe &&
                            awk_text_is(awk_readers[i]->name, name->text, name->length))
                                return awk_readers[i];
                }
                else if (free_slot < 0)
                        free_slot = i;
        }

        if (free_slot < 0)
        {
                if (!shell_array_room(awk_readers, awk_readers_room,
                                      (positive)awk_reader_count + 1))
                        return null;

                free_slot = awk_reader_count++;
                awk_readers[free_slot] = (awk_reader address_to)awk_take(sizeof(awk_reader));
                memory_fill(awk_readers[free_slot], 0, sizeof(awk_reader));
        }

        awk_reader address_to made = awk_readers[free_slot];

        made->name = awk_text_hold(name);
        made->at = 0;
        made->filled = 0;
        made->ended = false;
        made->failed = false;
        made->pipe = pipe;
        made->child = 0;

        if (pipe)
        {
                b32 ends[2];

                if (system_pipe(ends, SHELL_PIPE_CLOSE_ON_EXEC) < 0)
                        return null;

                made->handle = ends[0];
                made->live = true;
                made->child = awk_spawn(name->text, -1, ends[1]);
                system_close(ends[1]);
                return made;
        }

        if (awk_name_is(name, "-") || awk_name_is(name, "/dev/stdin"))
        {
                made->handle = 0;
                made->live = true;
                return made;
        }

        bipolar handle = text_open_handle(name->text, FILE_READ, 0);

        if (handle < 0)
        {
                awk_text_drop(made->name);
                made->name = null;
                return null;
        }

        made->handle = (b32)handle;
        made->live = true;
        return made;
}

static inline INLINE b32 awk_writer_close(awk_writer address_to which)
{
        awk_writer_flush(which);

        if (which->handle > 2)
                system_close(which->handle);

        b32 answer = which->kind == AWK_TO_PIPE ? awk_wait_for(which->child) : 0;

        awk_text_drop(which->name);
        which->name = null;
        which->live = false;
        return answer;
}

static inline INLINE b32 awk_reader_close(awk_reader address_to which)
{
        if (which->handle > 2)
                system_close(which->handle);

        b32 answer = which->pipe ? awk_wait_for(which->child) : 0;

        awk_text_drop(which->name);
        which->name = null;
        which->live = false;
        which->at = 0;
        which->filled = 0;
        which->ended = true;
        return answer;
}

static b32 awk_close_named(awk_text address_to name)
{
        b32 answer = -1;

        for (b32 i = 0; i < 2; i++)
                if (awk_standard_named[i] &&
                    awk_name_is(name, awk_standard_names[i]))
                {
                        awk_standard_named[i] = false;
                        awk_writer_flush(address_of awk_standard_out);
                        answer = 0;
                }

        for (b32 i = 0; i < awk_writer_count; i++)
        {
                awk_writer address_to which = awk_writers[i];

                if (!which->live ||
                    !awk_text_is(which->name, name->text, name->length))
                        continue;

                answer = awk_writer_close(which);
        }

        for (b32 i = 0; i < awk_reader_count; i++)
        {
                awk_reader address_to which = awk_readers[i];

                if (!which->live ||
                    !awk_text_is(which->name, name->text, name->length))
                        continue;

                answer = awk_reader_close(which);
        }

        return answer;
}

static fn awk_close_everything()
{
        for (b32 i = 0; i < awk_writer_count; i++)
        {
                awk_writer address_to which = awk_writers[i];

                if (!which->live)
                        continue;

                awk_writer_close(which);
        }

        for (b32 i = 0; i < awk_reader_count; i++)
        {
                awk_reader address_to which = awk_readers[i];

                if (!which->live)
                        continue;

                awk_reader_close(which);
        }
}

/*
        One record, however RS says a record ends.

        An empty RS is paragraph mode: blank lines separate, leading ones are
        skipped, and a newline becomes a field separator as well. A single
        character is itself. Anything longer is a pattern, which is not what
        POSIX says and is what the awk on this machine does.
*/
static b32 awk_reader_end(awk_reader address_to which)
{
        b32 answer = which->failed ? -1 : 0;
        which->failed = false;
        return answer;
}

static b32 awk_read_record(awk_reader address_to which, awk_text address_to address_to into)
{
        positive length;
        string_address separator = awk_separator(awk_where_rs, address_of length);

        if (!length)
        {
                for (;;)
                {
                        which->at += memory_span_byte(
                            which->data + which->at, '\n',
                            which->filled - which->at);

                        if (which->at < which->filled || !awk_reader_fill(which))
                                break;
                }

                if (which->at >= which->filled)
                        return awk_reader_end(which);

                positive scan = 0;
                bool found = false;

                for (;;)
                {
                        p8 address_to cut = (p8 address_to)memory_search(
                            which->data + which->at + scan,
                            which->filled - which->at - scan, "\n\n", 2);

                        if (cut)
                        {
                                scan = (positive)(cut - (which->data + which->at));
                                found = true;
                        }
                        else
                                scan = which->filled - which->at - 1;

                        if (found || !awk_reader_fill(which))
                                break;
                }

                if (!found)
                        scan = which->filled - which->at;

                positive stop = scan;

                stop -= memory_span_byte_reverse(which->data + which->at,
                                                 '\n', stop);

                address_to into = awk_text_new(which->data + which->at, stop);
                which->at += scan;
                return true;
        }

        if (length == 1)
        {
                positive scan = 0;

                for (;;)
                {
                        p8 address_to cut = (p8 address_to)memory_first_of(
                            which->data + which->at + scan, separator[0],
                            which->filled - which->at - scan);

                        if (cut)
                        {
                                scan = (positive)(cut - (which->data + which->at));
                                break;
                        }

                        scan = which->filled - which->at;

                        if (!awk_reader_fill(which))
                                break;
                }

                positive here = which->at + scan;

                if (!scan && here >= which->filled)
                        return awk_reader_end(which);

                address_to into = awk_text_new(which->data + which->at, scan);
                which->at = here < which->filled ? here + 1 : here;
                return true;
        }

        awk_text address_to pattern = awk_text_new(separator, length);
        positive cut = TEXT_UNSET;
        positive stop = 0;

        for (;;)
        {
                regex_current = *awk_regex_dynamic(pattern);

                bool got = regex_find(REGEX_LONGEST, which->data + which->at,
                                                which->filled - which->at, 0) &&
                           regex_slots[1] > regex_slots[0];

                cut = got ? regex_slots[0] : TEXT_UNSET;
                stop = got ? regex_slots[1] : 0;

                if (which->ended)
                        break;

                // A match that runs to the end of what has been read may
                // still be growing, so read more before believing it.
                if (got && stop < which->filled - which->at)
                        break;

                awk_reader_fill(which);
        }

        awk_text_drop(pattern);

        if (cut == TEXT_UNSET)
        {
                if (which->at >= which->filled)
                        return awk_reader_end(which);

                address_to into = awk_text_new(which->data + which->at,
                                               which->filled - which->at);
                which->at = which->filled;
                return true;
        }

        address_to into = awk_text_new(which->data + which->at, cut);
        which->at += stop;
        return true;
}

/*
        Somewhere to build a string that does not know its length yet.

        Small ones never leave the stack; the arena is only reached for when
        one outgrows the thousand bytes it starts with.
*/
typedef struct
{
        p8 address_to data;
        positive room;
        positive used;
        bool heap;
        p8 fixed[1024];
} awk_builder;

static fn awk_builder_start(awk_builder address_to build)
{
        build->data = build->fixed;
        build->room = sizeof(build->fixed);
        build->used = 0;
        build->heap = false;
}

static fn awk_builder_room(awk_builder address_to build, positive want)
{
        if (want <= build->room)
                return;

        positive room = memory_growth(build->room, want, sizeof(build->fixed));

        if (!room)
                awk_leave(string_diagnostic(&text_diagnostic, 2, null, "out of memory"));

        p8 address_to made = (p8 address_to)awk_take(room);

        memory_copy_apart(made, build->data, build->used);

        if (build->heap)
                memory_give(build->data);

        build->data = made;
        build->room = room;
        build->heap = true;
}

static fn awk_builder_put(awk_builder address_to build, string_address data, positive length)
{
        awk_builder_room(build, awk_size_add(build->used, awk_size_add(length, 1)));
        memory_copy_apart(build->data + build->used, data, length);
        build->used += length;
}

static fn awk_builder_char(awk_builder address_to build, p8 character)
{
        awk_builder_room(build, awk_size_add(build->used, 2));
        build->data[build->used++] = character;
}

static fn awk_builder_fill(awk_builder address_to build, p8 character, positive count)
{
        if (!count)
                return;

        awk_builder_room(build, awk_size_add(build->used, awk_size_add(count, 1)));
        memory_fill(build->data + build->used, character, count);
        build->used += count;
}

static awk_text address_to awk_builder_text(awk_builder address_to build)
{
        awk_text address_to made = awk_text_new(build->data, build->used);

        if (build->heap)
                memory_give(build->data);

        return made;
}

/*
        printf.

        The conversions are C's, because that is what awk's are: the same
        flags, the same star for a width taken from the arguments, and the
        same six digits when no precision is given. %c is the one that is not
        C's -- a number is a character code and a string is its first byte,
        and which one a value is follows the same rule comparison follows.
*/
static b32 awk_integer_digits(decimal value, p8 address_to out, positive room,
                              bool address_to negative)
{
        address_to negative = value < 0;

        if (!decimal_is_finite(value))
        {
                string_address name = awk_not_finite_name(value, false);
                b32 at = AWK_NOT_FINITE_LENGTH;

                address_to negative = false;
                memory_copy(out, name, AWK_NOT_FINITE_LENGTH + 1);
                return at;
        }

        decimal magnitude = awk_truncate(value < 0 ? -value : value);

        if (magnitude < 1e18)
        {
                b32 at = (b32)positive_into_string(out, (positive)magnitude);

                if (at == 1 && out[0] == '0')
                        address_to negative = false;

                return at;
        }

        return (b32)awk_write_decimal(magnitude, 0, out, room, 'f', false);
}

static awk_text address_to awk_sprintf(string_address format, positive length,
                                       awk_value address_to arguments, b32 count)
{
        awk_builder build;
        awk_value nothing;
        b32 taken = 0;
        positive at = 0;

        nothing.text = null;
        nothing.number = 0;
        nothing.state = AWK_UNSET;
        awk_builder_start(address_of build);

        while (at < length)
        {
                // The bytes up to the next conversion go across as one run.
                // A byte at a time, with a room check in each, was the cost
                // of every CONVFMT and OFMT conversion.
                if (format[at] != '%')
                {
                        p8 address_to next = (p8 address_to)memory_first_of(
                            format + at, '%', length - at);
                        positive run = next ? (positive)(next - (format + at))
                                            : length - at;

                        awk_builder_put(address_of build, format + at, run);
                        at += run;
                        continue;
                }

                positive directive = at++;

                if (at < length && format[at] == '%')
                {
                        awk_builder_char(address_of build, '%');
                        at++;
                        continue;
                }

                string_address fields_at = format + at;
                conversion_spec parsed = conversion_spec_take_max(&fields_at, length - at);

                // The ' flag asks for the locale's digit grouping. Digits are
                // written ungrouped whatever LC_NUMERIC says, as in the C
                // locale, so the flag is read among the others and means
                // nothing more: %'d and %-'8d are %d and %-8d.
                while (parsed.fields == 1 && !parsed.field[0] && !parsed.stars &&
                       (positive)(fields_at - format) < length && fields_at[0] == '\'')
                {
                        p32 flags = parsed.flags;

                        fields_at++;
                        parsed = conversion_spec_take_max(&fields_at,
                                                          length - (positive)(fields_at - format));
                        parsed.flags |= flags;
                }
                //      An overflowed field is not a width. Nothing below
                //      guarded it, so a count past what the field holds
                //      arrived as whatever it wrapped to and was padded out.
                positive width = (parsed.overflow & 1) ? 0 : parsed.field[0];
                b32 precision = parsed.fields == 2 && !(parsed.overflow & 2)
                    ? (b32)min(2147483647ul, parsed.field[1]) : -1;
                for (p8 field = 0; field < parsed.fields; field++)
                        if (parsed.stars & (1u << field))
                        {
                                bipolar value = awk_whole(taken < count
                                    ? awk_to_number(&arguments[taken++]) : 0);
                                if (field)
                                        precision = value < 0 ? -1 : (b32)value;
                                else
                                {
                                        parsed.flags |= value < 0 ? CONVERSION_FLAG_LEFT : 0;
                                        width = (positive)absolute_wide(value);
                                }
                        }
                bool left = (parsed.flags & CONVERSION_FLAG_LEFT) != 0;
                bool sign = (parsed.flags & CONVERSION_FLAG_PLUS) != 0;
                bool space = (parsed.flags & CONVERSION_FLAG_SPACE) != 0;
                bool zero = (parsed.flags & CONVERSION_FLAG_ZERO) != 0;
                bool alternate = (parsed.flags & CONVERSION_FLAG_ALTERNATE) != 0;
                at = (positive)(fields_at - format);

                // The length modifiers C needs and awk has no use for.
                while (at < length && (format[at] == 'l' || format[at] == 'h' ||
                                       format[at] == 'L' || format[at] == 'q' ||
                                       format[at] == 'j' || format[at] == 'z' ||
                                       format[at] == 't'))
                        at++;

                // A conversion the format ends in the middle of is written
                // as it stands, which is what the reference awk writes.
                if (at >= length)
                {
                        awk_builder_put(address_of build, format + directive, length - directive);
                        break;
                }

                p8 conversion = format[at++];

                // A width on %% is read and ignored: one percent sign.
                if (conversion == '%')
                {
                        awk_builder_char(address_of build, '%');
                        continue;
                }

                bool took = taken < count;
                awk_value address_to argument = took ? address_of arguments[taken++]
                                                     : address_of nothing;
                p8 room[2048];
                p8 address_to grown = null;
                p8 prefix[4];
                b32 prefixed = 0;
                positive body = 0;
                positive shown = (positive)-1;
                bool from_string = false;
                string_address body_at = room;

                switch (conversion)
                {
                case 'd':
                case 'i':
                {
                        bool negative;
                        decimal value = awk_to_number(argument);

                        body = awk_integer_digits(value, room, sizeof(room), address_of negative);

                        // Not a number: the name carries its own sign and
                        // takes no zero fill and no precision.
                        if (!decimal_is_finite(value))
                        {
                                zero = false;
                                precision = -1;
                                break;
                        }

                        // A precision of zero on a value of exactly zero
                        // writes no digits at all, which is C's rule; a
                        // value that only rounds to zero writes the zero, as
                        // the reference does.
                        if (!precision && body == 1 && room[0] == '0' && value == 0)
                                body = 0;

                        if (negative)
                                prefix[prefixed++] = '-';
                        else if (sign)
                                prefix[prefixed++] = '+';
                        else if (space)
                                prefix[prefixed++] = ' ';

                        break;
                }

                case 'o':
                case 'u':
                case 'x':
                case 'X':
                {
                        decimal exact = awk_to_number(argument);
                        decimal value = awk_truncate(exact);
                        positive whole;

                        /*
                                A value with no place in sixty four bits is
                                written the way the reference writes it,
                                which is not as a number in that base at all.
                        */
                        if (value >= 18446744073709551616.0 ||
                            value < -9223372036854775808.0 || !decimal_is_finite(value))
                        {
                                if (!decimal_is_finite(exact))
                                {
                                        string_address name = awk_not_finite_name(exact, conversion == 'X');

                                        body = AWK_NOT_FINITE_LENGTH;
                                        memory_copy(room, name, AWK_NOT_FINITE_LENGTH);
                                        zero = false;
                                        precision = -1;
                                        break;
                                }

                                body = awk_write_decimal(
                                    exact, precision < 0 ? 6 : precision, room,
                                    sizeof(room), 'g', alternate && conversion != 'u');

                                if (room[0] == '-')
                                {
                                        prefix[prefixed++] = '-';
                                        body--;
                                        body_at = room + 1;
                                }
                                else if (sign)
                                        prefix[prefixed++] = '+';
                                else if (space)
                                        prefix[prefixed++] = ' ';

                                // The precision was %g's; it is not a count
                                // of digits to pad the text out to, which
                                // made %.10x of 1.2e23 "0001.2e+23". It did
                                // take the zero flag with it, as on any
                                // integer conversion.
                                if (precision >= 0)
                                        zero = false;

                                precision = -1;
                                break;
                        }

                        whole = value < 0 ? (positive)(bipolar)value : (positive)value;

                        positive base = conversion == 'o' ? 8
                                                          : (conversion == 'u' ? 10 : 16);

                        /*
                                Octal's alternate zero is a digit, not a
                                prefix: it counts towards integer precision.
                                Giving the converter the following byte keeps
                                that rule without moving the result. It also
                                makes %#.0o of zero the required single zero.
                        */
                        if (alternate && conversion == 'o')
                        {
                                room[0] = '0';
                                body = whole ? 1 + positive_into_base(room + 1, whole, 8,
                                                                          false)
                                             : 1;
                        }
                        else if (whole || precision != 0 || exact != 0)
                                body = positive_into_base(room, whole, base,
                                                               conversion == 'X');

                        if (alternate && (conversion == 'x' || conversion == 'X') && exact != 0)
                        {
                                prefix[prefixed++] = '0';
                                prefix[prefixed++] = conversion;
                        }

                        break;
                }

                case 'c':
                {
                        if (awk_numeric_side(argument))
                        {
                                decimal value = awk_to_number(argument);

                                // The low byte of the machine's conversion:
                                // a value with no place in sixty four signed
                                // bits, or not a number, is the byte 0, as
                                // the reference has it.
                                room[body++] = !decimal_is_finite(value) ||
                                                       value >= 9223372036854775808.0 ||
                                                       value < -9223372036854775808.0
                                                   ? (p8)0
                                                   : (p8)((positive)(bipolar)value & 0xff);

                                // In a UTF-8 locale a value that is a
                                // character is written as its sequence.
                                if (!awk_utf8_known)
                                        awk_utf8_known = text_locale_utf8() ? 2 : 1;

                                if (awk_utf8_known == 2 && decimal_is_finite(value) &&
                                    value >= 128.0 && value < 1114112.0)
                                {
                                        positive made = memory_utf8_encode(room, 4, (positive)value);

                                        if (made)
                                                body = made;
                                }
                        }
                        else
                        {
                                awk_text address_to text = awk_to_text(argument);

                                if (awk_wide(text->text, text->length))
                                {
                                        body = memory_utf8_span(text->text, text->length, 1).x;
                                        memory_copy(room, text->text, body);
                                        shown = 1;
                                }
                                else
                                        room[body++] = text->length ? text->text[0] : (p8)0;
                        }

                        precision = -1;
                        break;
                }

                case 's':
                {
                        awk_text address_to text = awk_to_text(argument);

                        body_at = text->text;
                        body = text->length;
                        from_string = true;

                        if (awk_wide(text->text, text->length))
                        {
                                // A precision and a width count characters.
                                if (precision >= 0)
                                        body = memory_utf8_span(text->text, text->length, precision).x;

                                shown = awk_characters(text->text, body);
                                break;
                        }

                        if (precision >= 0 && precision < body)
                                body = precision;

                        break;
                }

                case 'e':
                case 'E':
                case 'f':
                case 'F':
                case 'g':
                case 'G':
                {
                        decimal value = awk_to_number(argument);
                        b32 places = precision < 0 ? 6 : precision;

                        if (!decimal_is_finite(value))
                        {
                                string_address name = awk_not_finite_name(value, conversion <= 'Z');

                                body = AWK_NOT_FINITE_LENGTH;
                                memory_copy(room, name, AWK_NOT_FINITE_LENGTH);
                                zero = false;
                                break;
                        }

                        /*
                                A precision past what the room holds is
                                given a room of its own: the formatter
                                writes a double's digits exactly, as many as
                                are asked for, and the reference writes
                                them all. The places were cut at a thousand,
                                so %.1500f of a third was five hundred
                                digits short with nothing said. A precision
                                no memory could hold is refused as the
                                reference refuses it.
                        */
                        p8 address_to digits = room;
                        positive digits_room = sizeof(room);

                        if ((positive)places > AWK_PLACES_MAX)
                                awk_leave(string_diagnostic(&text_diagnostic, 2, null, "out of memory"));
                        if ((positive)places + AWK_PLACES_SLACK > sizeof(room))
                        {
                                digits_room = (positive)places + AWK_PLACES_SLACK;
                                digits = grown = (p8 address_to)awk_take(digits_room);
                        }

                        body = awk_write_decimal(value, places, digits, digits_room,
                                                      conversion, alternate);
                        body_at = digits;

                        // The minus is the formatter's and the field is
                        // awk's: it has to stand in front of the zero fill,
                        // so it moves out of the body and into the prefix.
                        if (digits[0] == '-')
                        {
                                prefix[prefixed++] = '-';
                                body--;
                                body_at = digits + 1;
                        }
                        else if (sign)
                                prefix[prefixed++] = '+';
                        else if (space)
                                prefix[prefixed++] = ' ';

                        break;
                }

                default:
                        // An unknown conversion is written whole, flags and
                        // width included, and takes no argument.
                        awk_builder_put(address_of build, format + directive, at - directive);
                        taken -= took ? 1 : 0;
                        continue;
                }

                // A precision on an integer is a minimum number of digits,
                // and it takes the zero flag out of the argument.
                positive zeros = 0;

                if (!from_string && precision >= 0 &&
                    (conversion == 'd' || conversion == 'i' || conversion == 'o' ||
                     conversion == 'u' || conversion == 'x' || conversion == 'X'))
                {
                        if (precision > body)
                                zeros = precision - body;

                        zero = false;
                }

                // The zero flag is for numbers: a string or a character is
                // padded with spaces, as the reference awk pads them.
                if (from_string || conversion == 'c')
                        zero = false;
                positive total = awk_size_add(awk_size_add(prefixed, zeros),
                                              shown == (positive)-1 ? body : shown);
                positive padding = width > total ? width - total : 0;

                if (padding && !left && !zero)
                        awk_builder_fill(address_of build, ' ', padding);

                for (b32 i = 0; i < prefixed; i++)
                        awk_builder_char(address_of build, prefix[i]);

                if (padding && !left && zero)
                        awk_builder_fill(address_of build, '0', padding);

                awk_builder_fill(address_of build, '0', zeros);
                awk_builder_put(address_of build, body_at, body);
                if (grown)
                        memory_give(grown);

                if (padding && left)
                        awk_builder_fill(address_of build, ' ', padding);
        }

        return awk_builder_text(address_of build);
}

/*
        The lexer.

        One thing here is not decidable from the character: a slash is either
        a division or the start of a pattern, and which one depends on whether
        the parser is standing where an operand would go. That is what the
        previous token says, so it is kept.
*/
enum
{
        T_END = 0,
        T_NEWLINE,
        T_OPEN_BRACE,
        T_CLOSE_BRACE,
        T_OPEN,
        T_CLOSE,
        T_OPEN_SQUARE,
        T_CLOSE_SQUARE,
        T_SEMICOLON,
        T_COMMA,
        T_NUMBER,
        T_STRING,
        T_ERE,
        T_NAME,
        T_CALL_NAME,
        T_BUILTIN,
        T_GETLINE,
        T_BEGIN,
        T_FINISH,
        T_FUNCTION,
        T_IF,
        T_ELSE,
        T_WHILE,
        T_FOR,
        T_DO,
        T_BREAK,
        T_CONTINUE,
        T_NEXT,
        T_NEXTFILE,
        T_EXIT,
        T_RETURN,
        T_DELETE,
        T_IN,
        T_PRINT,
        T_PRINTF,
        T_ASSIGN,
        T_ASSIGN_ADD,
        T_ASSIGN_SUB,
        T_ASSIGN_MUL,
        T_ASSIGN_DIV,
        T_ASSIGN_MOD,
        T_ASSIGN_POWER,
        T_OR,
        T_AND,
        T_NOT,
        T_LESS,
        T_LESS_EQUAL,
        T_GREATER,
        T_GREATER_EQUAL,
        T_EQUAL,
        T_UNEQUAL,
        T_MATCH,
        T_UNMATCH,
        T_PLUS,
        T_MINUS,
        T_TIMES,
        T_DIVIDE,
        T_MODULO,
        T_POWER,
        T_QUESTION,
        T_COLON,
        T_PLUS_PLUS,
        T_MINUS_MINUS,
        T_DOLLAR,
        T_APPEND,
        T_PIPE
};

enum
{
        B_LENGTH = 0,
        B_SUBSTR,
        B_INDEX,
        B_SPLIT,
        B_SUB,
        B_GSUB,
        B_MATCH,
        B_SPRINTF,
        B_SIN,
        B_COS,
        B_ATAN2,
        B_EXP,
        B_LOG,
        B_SQRT,
        B_INT,
        B_RAND,
        B_SRAND,
        B_TOLOWER,
        B_TOUPPER,
        B_SYSTEM,
        B_CLOSE,
        B_FFLUSH
};

typedef struct
{
        string_address name;
        b32 token;
        b32 value;
} awk_word;

static awk_word awk_words[] = {
    {"BEGIN", T_BEGIN, 0},
    {"END", T_FINISH, 0},
    {"atan2", T_BUILTIN, B_ATAN2},
    {"break", T_BREAK, 0},
    {"continue", T_CONTINUE, 0},
    {"cos", T_BUILTIN, B_COS},
    {"close", T_BUILTIN, B_CLOSE},
    {"do", T_DO, 0},
    {"delete", T_DELETE, 0},
    {"else", T_ELSE, 0},
    {"exit", T_EXIT, 0},
    {"exp", T_BUILTIN, B_EXP},
    {"function", T_FUNCTION, 0},
    {"func", T_FUNCTION, 0},
    {"for", T_FOR, 0},
    {"fflush", T_BUILTIN, B_FFLUSH},
    {"getline", T_GETLINE, 0},
    {"gsub", T_BUILTIN, B_GSUB},
    {"if", T_IF, 0},
    {"in", T_IN, 0},
    {"index", T_BUILTIN, B_INDEX},
    {"int", T_BUILTIN, B_INT},
    {"length", T_BUILTIN, B_LENGTH},
    {"log", T_BUILTIN, B_LOG},
    {"match", T_BUILTIN, B_MATCH},
    {"next", T_NEXT, 0},
    {"nextfile", T_NEXTFILE, 0},
    {"print", T_PRINT, 0},
    {"printf", T_PRINTF, 0},
    {"return", T_RETURN, 0},
    {"rand", T_BUILTIN, B_RAND},
    {"substr", T_BUILTIN, B_SUBSTR},
    {"split", T_BUILTIN, B_SPLIT},
    {"sub", T_BUILTIN, B_SUB},
    {"sprintf", T_BUILTIN, B_SPRINTF},
    {"sin", T_BUILTIN, B_SIN},
    {"sqrt", T_BUILTIN, B_SQRT},
    {"srand", T_BUILTIN, B_SRAND},
    {"system", T_BUILTIN, B_SYSTEM},
    {"tolower", T_BUILTIN, B_TOLOWER},
    {"toupper", T_BUILTIN, B_TOUPPER},
    {"while", T_WHILE, 0},
    {null, 0, 0}};

#define AWK_WORD_COUNT (array_count(awk_words) - 1)
#define AWK_WORD_RANGE(start, count) ((p16)(((start) << 8) | (count)))

/*
        The table above is grouped by first byte. An identifier already paid
        to read that byte, so this index rejects most ordinary variable names
        in one load and narrows the assembly exact-match walk to at most eight
        entries. The high byte is the first entry and the low byte its count.
*/
static const p16 awk_word_range[128] = {
    ['B'] = AWK_WORD_RANGE(0, 1),  ['E'] = AWK_WORD_RANGE(1, 1),
    ['a'] = AWK_WORD_RANGE(2, 1),  ['b'] = AWK_WORD_RANGE(3, 1),
    ['c'] = AWK_WORD_RANGE(4, 3),  ['d'] = AWK_WORD_RANGE(7, 2),
    ['e'] = AWK_WORD_RANGE(9, 3),  ['f'] = AWK_WORD_RANGE(12, 4),
    ['g'] = AWK_WORD_RANGE(16, 2), ['i'] = AWK_WORD_RANGE(18, 4),
    ['l'] = AWK_WORD_RANGE(22, 2), ['m'] = AWK_WORD_RANGE(24, 1),
    ['n'] = AWK_WORD_RANGE(25, 2), ['p'] = AWK_WORD_RANGE(27, 2),
    ['r'] = AWK_WORD_RANGE(29, 2), ['s'] = AWK_WORD_RANGE(31, 8),
    ['t'] = AWK_WORD_RANGE(39, 2), ['w'] = AWK_WORD_RANGE(41, 1)};

_Static_assert(AWK_WORD_COUNT == 42, "update the AWK keyword ranges");

/*
        How many arguments each of them takes.

        Checked where the call is parsed, because the evaluator reaches for
        the second argument of substr without looking and there is nothing
        there to reach for.
*/
static p8 awk_builtin_least[] = {0, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2,
                                 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0};

static p8 awk_builtin_most[] = {1, 3, 2, 3, 3, 3, 2, 255, 1, 1, 2,
                                1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1};

typedef struct
{
        p8 one;
        p8 equal;
        p8 doubled;
} awk_operator;

static const awk_operator awk_operators[128] = {
    ['{'] = {T_OPEN_BRACE},     ['}'] = {T_CLOSE_BRACE},
    ['('] = {T_OPEN},           [')'] = {T_CLOSE},
    ['['] = {T_OPEN_SQUARE},    [']'] = {T_CLOSE_SQUARE},
    [';'] = {T_SEMICOLON},      [','] = {T_COMMA},
    ['?'] = {T_QUESTION},       [':'] = {T_COLON},
    ['$'] = {T_DOLLAR},         ['~'] = {T_MATCH},
    ['+'] = {T_PLUS, T_ASSIGN_ADD, T_PLUS_PLUS},
    ['-'] = {T_MINUS, T_ASSIGN_SUB, T_MINUS_MINUS},
    ['*'] = {T_TIMES, T_ASSIGN_MUL, T_POWER},
    ['/'] = {T_DIVIDE, T_ASSIGN_DIV},
    ['%'] = {T_MODULO, T_ASSIGN_MOD},
    ['^'] = {T_POWER, T_ASSIGN_POWER},
    ['='] = {T_ASSIGN, T_EQUAL},
    ['!'] = {T_NOT, T_UNEQUAL},
    ['<'] = {T_LESS, T_LESS_EQUAL},
    ['>'] = {T_GREATER, T_GREATER_EQUAL, T_APPEND},
    ['&'] = {0, 0, T_AND},      ['|'] = {T_PIPE, 0, T_OR}};

/* The lexer and parser ask two versions of the same token-class question.
   One property table keeps the grammar in one place and makes either answer
   one indexed bit instead of two independent branch trees. */
#define AWK_TOKEN_BEFORE 1
#define AWK_TOKEN_STARTS 2
static const p8 awk_token_properties[T_PIPE + 1] = {
    [T_NUMBER] = 3,       [T_STRING] = 3,      [T_ERE] = 3,
    [T_NAME] = 3,         [T_BUILTIN] = 3,     [T_PLUS_PLUS] = 3,
    [T_MINUS_MINUS] = 3,  [T_CLOSE] = AWK_TOKEN_BEFORE,
    [T_CLOSE_SQUARE] = AWK_TOKEN_BEFORE,
    [T_CALL_NAME] = AWK_TOKEN_STARTS,
    [T_DOLLAR] = AWK_TOKEN_STARTS, [T_OPEN] = AWK_TOKEN_STARTS,
    [T_NOT] = AWK_TOKEN_STARTS,
};

#define awk_operand_before()                                                \
        (awk_token_properties[awk_last_token] & AWK_TOKEN_BEFORE)
#define awk_starts_operand()                                                \
        (awk_token_properties[awk_token] & AWK_TOKEN_STARTS)

static string_address awk_source;
static positive awk_source_length;
static positive awk_source_at;
static b32 awk_token;
static b32 awk_token_value;
static b32 awk_last_token = T_NEWLINE;
static decimal awk_token_number;
static awk_text address_to awk_token_text;
static b32 awk_line_number = 1;

/*
        Where the program went wrong.

        The line is awk's own state, so this is string_report through the
        error writer rather than the shared diagnostic sink, which is the
        form the lexer and awk_expect use around it.
*/
static DEAD_END fn awk_syntax(string_address reason)
{
        awk_leave((awk_flush_everything(), text_flush(),
                   string_report(writer_stderr, 1, "%s: line %p: %s\n",
                                 text_name, (positive)awk_line_number, reason)));
}

static bool awk_name_start(p8 character)
{
        return byte_is_alpha(character) || character == '_';
}

/*
        The escapes a string has, in a pattern: \101 and \x41 are an A, \t
        a tab, \/ a slash, \" a quote. That is the reference awk's reading,
        and it leaves no back-references -- \1 is the byte 1. The byte goes
        in as it is, so \056 is a dot that matches anything, which is also
        the reference's reading. Any other escape belongs to the regular
        expression machine.
*/
static b32 awk_escape(string_address source, positive address_to at, positive stop);

static awk_text address_to awk_regex_escapes(string_address pattern)
{
        positive length = string_length(pattern);
        awk_builder build;

        awk_builder_start(address_of build);

        for (positive i = 0; i < length;)
        {
                p8 here = pattern[i];

                if (here != '\\' || i + 1 >= length)
                {
                        awk_builder_char(address_of build, here);
                        i++;
                        continue;
                }

                p8 next = pattern[i + 1];
                bool simple = next == 'a' || next == 'b' || next == 'f' || next == 'n' ||
                              next == 'r' || next == 't' || next == 'v' || next == '/' ||
                              next == '"';
                positive at = i + 1;
                b32 made = simple || (next >= '0' && next <= '7') || next == 'x'
                               ? awk_escape(pattern, address_of at, length)
                               : 0;

                // Not one of those, or \x with no digit after it: the
                // machine's own.
                if (at <= i + 1 || (next == 'x' && at == i + 2) || !made)
                {
                        awk_builder_char(address_of build, here);
                        awk_builder_char(address_of build, next);
                        i += 2;
                        continue;
                }

                awk_builder_char(address_of build, (p8)made);
                i = at;
        }

        return awk_builder_text(address_of build);
}

static b32 awk_escape(string_address source, positive address_to at, positive stop)
{
        p8 character = source[address_to at];
        p8 escaped;

        address_to at += 1;

        escaped = byte_simple_escape(character);

        if (escaped)
                return escaped;

        if (character >= '0' && character <= '7')
        {
                positive first = address_to at - 1;
                positive limit = stop - first;
                positive used;

                if (limit > 3)
                        limit = 3;

                positive value = string_digits_octal_escape_max(
                    source + first, limit, address_of used);

                address_to at = first + used;

                return value & 0xff;
        }

        if (character == 'x')
        {
                positive limit = stop - address_to at;
                positive used;

                if (limit > 2)
                        limit = 2;

                positive value = string_digits_hexadecimal_escape_max(
                    source + address_to at, limit, address_of used);

                if (used)
                {
                        address_to at += used;
                        return value & 0xff;
                }
        }

        return character;
}

static fn awk_next_token()
{
        awk_last_token = awk_token;

        for (;;)
        {
                while (awk_source_at < awk_source_length &&
                       (awk_source[awk_source_at] == ' ' || awk_source[awk_source_at] == '\t' ||
                        awk_source[awk_source_at] == '\r'))
                        awk_source_at++;

                if (awk_source_at + 1 < awk_source_length && awk_source[awk_source_at] == '\\' &&
                    awk_source[awk_source_at + 1] == '\n')
                {
                        awk_source_at += 2;
                        awk_line_number++;
                        continue;
                }

                if (awk_source_at < awk_source_length && awk_source[awk_source_at] == '#')
                {
                        awk_source_at += memory_span_without_byte(
                            awk_source + awk_source_at, '\n',
                            awk_source_length - awk_source_at);

                        continue;
                }

                break;
        }

        if (awk_source_at >= awk_source_length)
        {
                awk_token = T_END;
                return;
        }

        p8 character = awk_source[awk_source_at];

        if (character == '\n')
        {
                awk_source_at++;
                awk_line_number++;
                awk_token = T_NEWLINE;
                return;
        }

        if (byte_is_digit(character) ||
            (character == '.' && awk_source_at + 1 < awk_source_length &&
             byte_is_digit(awk_source[awk_source_at + 1])))
        {
                positive used;

                awk_token_number = awk_scan_number(awk_source + awk_source_at,
                                                   awk_source_length - awk_source_at,
                                                   address_of used);
                awk_source_at += used;
                awk_token = T_NUMBER;
                return;
        }

        if (awk_name_start(character))
        {
                positive start = awk_source_at;

                awk_source_at += string_span_max(awk_source + awk_source_at,
                                                 awk_source_length - awk_source_at,
                                                 string_set_name);

                positive length = awk_source_at - start;

                positive word = AWK_WORD_COUNT;

                p16 range = awk_word_range[character];
                positive first = range >> 8;
                positive count = range & 255;

                if (count)
                {
                        /* The source builder owns one spare byte even when
                           this token ends at the exact end of the program. */
                        p8 delimiter = awk_source[awk_source_at];

                        awk_source[awk_source_at] = end;
                        positive found_in_range =
                            string_table_find(awk_source + start,
                                              awk_words + first,
                                              sizeof(awk_words[0]), count);
                        awk_source[awk_source_at] = delimiter;

                        if (found_in_range < count)
                                word = first + found_in_range;
                }

                if (word < AWK_WORD_COUNT)
                {
                        awk_token = awk_words[word].token;
                        awk_token_value = awk_words[word].value;
                        return;
                }

                awk_text_drop(awk_token_text);
                awk_token_text = awk_text_new(awk_source + start, length);
                awk_token = awk_source_at < awk_source_length &&
                                    awk_source[awk_source_at] == '('
                                ? T_CALL_NAME
                                : T_NAME;
                return;
        }

        if (character == '"')
        {
                awk_builder build;

                awk_builder_start(address_of build);
                awk_source_at++;

                while (awk_source_at < awk_source_length && awk_source[awk_source_at] != '"')
                {
                        if (awk_source[awk_source_at] == '\n')
                                awk_syntax("newline in string");

                        if (awk_source[awk_source_at] == '\\' &&
                            awk_source_at + 1 < awk_source_length)
                        {
                                awk_source_at++;
                                awk_builder_char(address_of build,
                                                 (p8)awk_escape(awk_source, address_of awk_source_at,
                                                                awk_source_length));
                                continue;
                        }

                        awk_builder_char(address_of build, awk_source[awk_source_at++]);
                }

                if (awk_source_at >= awk_source_length)
                        awk_syntax("unterminated string");

                awk_source_at++;
                awk_text_drop(awk_token_text);
                awk_token_text = awk_builder_text(address_of build);
                awk_token = T_STRING;
                return;
        }

        if (character == '/' && !awk_operand_before())
        {
                awk_builder build;
                bool inside = false;
                positive members = 0;

                awk_builder_start(address_of build);
                awk_source_at++;

                while (awk_source_at < awk_source_length)
                {
                        p8 here = awk_source[awk_source_at];

                        if (here == '\n')
                                awk_syntax("newline in regular expression");

                        if (here == '\\' && awk_source_at + 1 < awk_source_length)
                        {
                                // The engine below wants the backslash kept,
                                // except before the slash that would have
                                // ended the pattern.
                                if (awk_source[awk_source_at + 1] == '/')
                                {
                                        awk_builder_char(address_of build, '/');
                                        awk_source_at += 2;
                                        continue;
                                }

                                awk_builder_char(address_of build, here);
                                awk_builder_char(address_of build,
                                                 awk_source[awk_source_at + 1]);
                                awk_source_at += 2;
                                continue;
                        }

                        /*
                                A slash in a bracket is a member. The bracket
                                is closed by a ] that is not its first member
                                (after an optional ^) and not the end of a
                                [: :], [. .] or [= =] inside it, which is why
                                /[[:alpha:]/]+/ and /[]/]/ were cut short at
                                their slash as invalid expressions.
                        */
                        if (!inside)
                        {
                                if (here == '/')
                                        break;
                                if (here == '[')
                                {
                                        inside = true;
                                        members = awk_source_at + 1;
                                        if (members < awk_source_length &&
                                            awk_source[members] == '^')
                                                members++;
                                }
                        }
                        else if (here == '[' &&
                                 awk_source_at + 1 < awk_source_length &&
                                 (awk_source[awk_source_at + 1] == ':' ||
                                  awk_source[awk_source_at + 1] == '.' ||
                                  awk_source[awk_source_at + 1] == '='))
                        {
                                p8 kind = awk_source[awk_source_at + 1];

                                awk_builder_char(address_of build, here);
                                awk_builder_char(address_of build, kind);
                                awk_source_at += 2;
                                while (awk_source_at + 1 < awk_source_length &&
                                       awk_source[awk_source_at] != '\n' &&
                                       !(awk_source[awk_source_at] == kind &&
                                         awk_source[awk_source_at + 1] == ']'))
                                        awk_builder_char(address_of build,
                                                         awk_source[awk_source_at++]);
                                if (awk_source_at + 1 < awk_source_length &&
                                    awk_source[awk_source_at] == kind)
                                {
                                        awk_builder_char(address_of build, kind);
                                        awk_builder_char(address_of build, ']');
                                        awk_source_at += 2;
                                }
                                continue;
                        }
                        else if (here == ']' && awk_source_at != members)
                                inside = false;

                        awk_builder_char(address_of build, here);
                        awk_source_at++;
                }

                if (awk_source_at >= awk_source_length)
                        awk_syntax("unterminated regular expression");

                awk_source_at++;
                awk_text_drop(awk_token_text);
                awk_token_text = awk_builder_text(address_of build);
                awk_token = T_ERE;
                return;
        }

        awk_source_at++;

        p8 next = awk_source_at < awk_source_length ? awk_source[awk_source_at] : 0;

        awk_operator op = character < 128 ? awk_operators[character]
                                          : (awk_operator){0, 0, 0};

        if (next == '=' && op.equal)
        {
                awk_source_at++;
                awk_token = op.equal;
                return;
        }

        if (next == character && op.doubled)
        {
                awk_source_at++;
                awk_token = op.doubled;

                if (character == '*' && awk_source_at < awk_source_length &&
                    awk_source[awk_source_at] == '=')
                {
                        awk_source_at++;
                        awk_token = T_ASSIGN_POWER;
                }

                return;
        }

        if (character == '!' && next == '~')
        {
                awk_source_at++;
                awk_token = T_UNMATCH;
                return;
        }

        if (op.one)
        {
                awk_token = op.one;
                return;
        }

        awk_syntax("unexpected character");
}

/*
        The parser.

        Recursive descent, one function per level of precedence, in the order
        the grammar in the standard gives them. Two of those levels are the
        ones every awk gets wrong at least once: concatenation, which binds
        tighter than a comparison and looser than a minus -- so 1 " " -1 is
        one string and a subtraction -- and the greater-than after print,
        which is a redirection and not a comparison until a parenthesis says
        otherwise.
*/
enum
{
        N_NUMBER = 0,
        N_STRING,
        N_REGEX,
        N_VARIABLE,
        N_FIELD,
        N_SUBSCRIPT,
        N_GROUP,
        N_ASSIGN,
        N_COND,
        N_OR,
        N_AND,
        N_NOT,
        N_IN,
        N_MATCH,
        N_COMPARE,
        N_CONCAT,
        N_ARITH,
        N_NEGATE,
        N_AFFIRM,
        N_STEP,
        N_CALL,
        N_BUILTIN,
        N_GETLINE,
        S_PRINT,
        S_PRINTF,
        S_EXPRESSION,
        S_IF,
        S_LOOP,
        S_FORIN,
        S_BLOCK,
        S_NEXT,
        S_NEXTFILE,
        S_EXIT,
        S_RETURN,
        S_BREAK,
        S_CONTINUE,
        S_DELETE
};

enum
{
        G_MAIN = 0,
        G_FILE,
        G_COMMAND
};

enum
{
        R_NONE = 0,
        R_FILE,
        R_APPEND,
        R_PIPE
};

typedef struct awk_node
{
        p8 kind;
        p8 sub;
        b32 index;
        b32 count;
        struct awk_node address_to a;
        struct awk_node address_to b;
        struct awk_node address_to c;
        struct awk_node address_to d;
        struct awk_node address_to next;
        awk_text address_to text;
        decimal number;
        regex_program address_to program;
} awk_node;

typedef struct
{
        awk_text address_to name;
        b32 parameters;
        awk_node address_to body;
        bool defined;
} awk_function;

#define AWK_FUNCTIONS_MAX 256

static awk_function awk_functions[AWK_FUNCTIONS_MAX];
static b32 awk_function_count;

static awk_text address_to awk_local_names[AWK_LOCALS_MAX];
static b32 awk_local_count;
static bool awk_inside_function;
static b32 awk_print_depth;

typedef struct
{
        p8 kind;
        awk_node address_to first;
        awk_node address_to second;
        awk_node address_to action;
        bool running;
} awk_rule;

enum
{
        RULE_BEGIN = 0,
        RULE_END,
        RULE_PLAIN
};

#define AWK_RULES_MAX 512

static awk_rule awk_rules[AWK_RULES_MAX];
static b32 awk_rule_count;
// Where the parser stands: the kind of rule whose action this is, and how
// many loops deep, for the statements that are only allowed in one place.
static p8 awk_parsing_rule = RULE_PLAIN;
static b32 awk_loop_depth;

static awk_node address_to awk_node_new(p8 kind)
{
        awk_node address_to made = (awk_node address_to)awk_take(sizeof(awk_node));

        memory_fill(made, 0, sizeof(awk_node));
        made->kind = kind;
        return made;
}

static b32 awk_resolve(awk_text address_to name)
{
        if (awk_inside_function)
                for (b32 i = 0; i < awk_local_count; i++)
                        if (awk_text_is(awk_local_names[i], name->text, name->length))
                                return -(i + 1);

        return awk_global_find(name->text, name->length);
}

static b32 awk_function_named(awk_text address_to name)
{
        for (b32 i = 0; i < awk_function_count; i++)
                if (awk_text_is(awk_functions[i].name, name->text, name->length))
                        return i;

        if (awk_function_count == AWK_FUNCTIONS_MAX)
                awk_syntax("too many functions");

        b32 which = awk_function_count++;

        awk_functions[which].name = awk_text_hold(name);
        awk_functions[which].parameters = 0;
        awk_functions[which].body = null;
        awk_functions[which].defined = false;
        return which;
}

typedef struct
{
        positive at;
        b32 token;
        b32 value;
        b32 last;
        b32 line;
        decimal number;
        awk_text address_to text;
} awk_place;

static fn awk_mark(awk_place address_to place)
{
        place->at = awk_source_at;
        place->token = awk_token;
        place->value = awk_token_value;
        place->last = awk_last_token;
        place->line = awk_line_number;
        place->number = awk_token_number;
        place->text = awk_text_hold(awk_token_text);
}

static fn awk_reset(awk_place address_to place)
{
        awk_source_at = place->at;
        awk_token = place->token;
        awk_token_value = place->value;
        awk_last_token = place->last;
        awk_line_number = place->line;
        awk_token_number = place->number;
        awk_text_drop(awk_token_text);
        awk_token_text = place->text;
}

static fn awk_forget(awk_place address_to place)
{
        awk_text_drop(place->text);
}

static fn awk_expect(b32 what, string_address complaint)
{
        if (awk_token != what)
                awk_syntax(complaint);

        awk_next_token();
}

/*
        Whether the native stack has room for another level of nesting.

        A program is input, and both the parser and the interpreter descend
        once for every level of nesting in it: 30,000 nested parentheses,
        braces or ! is more stack than the process was given, and the fault
        was the whole of the diagnostic. Neither one can stand for the other
        -- an interpreter level is some five times a parser level here, so a
        depth the parser accepts is not a depth the interpreter can walk.

        The measure awk_call is already held to answers both, and asking the
        machine rather than counting levels needs no level counted back down
        and stays true as either one changes shape.
*/
static bool awk_room_left()
{
        b32 here;

        // One global and one compare: the interpreter asks this of every
        // node it walks, so the subtraction is done once in awk_stack_measure
        // rather than here.
        return (positive)address_of here >= awk_stack_floor;
}

static bool awk_walk_room_left()
{
        b32 here;

        return (positive)address_of here >= awk_walk_floor;
}

static fn awk_parse_room()
{
        if (!awk_room_left())
                awk_syntax("nested too deeply");
}

static fn awk_skip_newlines()
{
        while (awk_token == T_NEWLINE)
                awk_next_token();
}

static fn awk_skip_terminators()
{
        while (awk_token == T_NEWLINE || awk_token == T_SEMICOLON)
                awk_next_token();
}

static awk_node address_to awk_expression();
static awk_node address_to awk_concat_level();
static awk_node address_to awk_statement();
static awk_node address_to awk_statement_list(b32 stop);

static bool awk_is_lvalue(awk_node address_to node)
{
        return node && (node->kind == N_VARIABLE || node->kind == N_FIELD ||
                        node->kind == N_SUBSCRIPT);
}

/* Add one expression, then consume the comma and newlines that ask for
   another. Whether that next expression is allowed is the caller's policy. */
static bool awk_expression_list_one(awk_node address_to node,
                                    awk_node address_to address_to last)
{
        awk_node address_to one = awk_expression();

        if (address_to last)
                (address_to last)->next = one;
        else
                node->a = one;

        address_to last = one;
        node->count++;

        if (awk_token != T_COMMA)
                return false;

        awk_next_token();
        awk_skip_newlines();
        return true;
}

static awk_node address_to awk_subscript_list(b32 array)
{
        awk_node address_to node = awk_node_new(N_SUBSCRIPT);
        awk_node address_to last = null;

        node->index = array;

        for (;;)
                if (!awk_expression_list_one(node, address_of last))
                        break;

        return node;
}

static bool awk_statement_ends();

static awk_node address_to awk_primary()
{
        awk_node address_to node;

        awk_parse_room();

        switch (awk_token)
        {
        case T_NUMBER:
                node = awk_node_new(N_NUMBER);
                node->number = awk_token_number;
                awk_next_token();
                return node;

        case T_STRING:
                node = awk_node_new(N_STRING);
                node->text = awk_text_hold(awk_token_text);
                awk_next_token();
                return node;

        case T_ERE:
                node = awk_node_new(N_REGEX);
                node->text = awk_text_hold(awk_token_text);
                node->program = awk_regex_keep(awk_token_text->text);
                awk_next_token();
                return node;

        case T_DOLLAR:
                awk_next_token();
                node = awk_node_new(N_FIELD);
                node->a = awk_primary();

                if (node->a->kind == N_NUMBER &&
                    node->a->number == awk_truncate(node->a->number) &&
                    node->a->number >= 0 && node->a->number < 64)
                {
                        b32 field = (b32)node->a->number;

                        if (field)
                        {
                                awk_fields_fixed |= (positive)1 << field;
                                node->sub = 1;
                                node->index = field;
                        }
                }
                else
                        awk_fields_computed = true;

                return node;

        case T_PLUS_PLUS:
        case T_MINUS_MINUS:
        {
                b32 which = awk_token;

                awk_next_token();
                node = awk_node_new(N_STEP);
                node->sub = (p8)(which == T_PLUS_PLUS ? 1 : 0);
                node->count = 1;
                node->a = awk_primary();

                if (!awk_is_lvalue(node->a))
                        awk_syntax("++ wants a variable");

                return node;
        }

        case T_NOT:
                awk_next_token();
                node = awk_node_new(N_NOT);
                node->a = awk_primary();
                return node;

        case T_MINUS:
                awk_next_token();
                node = awk_node_new(N_NEGATE);
                node->a = awk_primary();
                return node;

        case T_PLUS:
                awk_next_token();
                node = awk_node_new(N_AFFIRM);
                node->a = awk_primary();
                return node;

        case T_OPEN:
        {
                b32 kept = awk_print_depth;
                awk_node address_to first;
                awk_node address_to last;
                b32 count = 1;

                awk_print_depth = 0;
                awk_next_token();
                awk_skip_newlines();
                first = awk_expression();
                last = first;

                while (awk_token == T_COMMA)
                {
                        awk_next_token();
                        awk_skip_newlines();
                        last->next = awk_expression();
                        last = last->next;
                        count++;
                }

                awk_print_depth = kept;
                awk_expect(T_CLOSE, "expected )");

                if (count == 1)
                        return first;

                // A list in parentheses is what in takes, or all that print
                // and printf take; printf("%d", 1) (2) is a syntax error.
                if (awk_token != T_IN &&
                    !(kept && (awk_statement_ends() || awk_token == T_GREATER ||
                               awk_token == T_APPEND || awk_token == T_PIPE)))
                        awk_syntax("a parenthesized list wants in or print");

                node = awk_node_new(N_GROUP);
                node->a = first;
                node->count = count;
                return node;
        }

        case T_NAME:
        {
                awk_text address_to name = awk_text_hold(awk_token_text);
                b32 where = awk_resolve(name);

                awk_next_token();

                if (awk_token == T_OPEN_SQUARE)
                {
                        awk_next_token();
                        node = awk_subscript_list(where);
                        awk_expect(T_CLOSE_SQUARE, "expected ]");
                        awk_text_drop(name);
                        return node;
                }

                node = awk_node_new(N_VARIABLE);
                node->index = where;
                node->text = name;
                return node;
        }

        case T_CALL_NAME:
        {
                awk_text address_to name = awk_text_hold(awk_token_text);
                b32 which = awk_function_named(name);
                b32 kept = awk_print_depth;
                awk_node address_to last = null;

                awk_text_drop(name);
                awk_next_token();
                awk_print_depth = 0;
                awk_expect(T_OPEN, "expected (");
                node = awk_node_new(N_CALL);
                node->index = which;
                awk_skip_newlines();

                while (awk_token != T_CLOSE)
                        if (!awk_expression_list_one(node, address_of last))
                                break;

                awk_print_depth = kept;
                awk_expect(T_CLOSE, "expected ) after arguments");
                return node;
        }

        case T_BUILTIN:
        {
                b32 which = awk_token_value;
                b32 kept = awk_print_depth;
                awk_node address_to last = null;

                awk_next_token();
                node = awk_node_new(N_BUILTIN);
                node->index = which;

                if (awk_token != T_OPEN)
                {
                        if (awk_builtin_least[which])
                                awk_syntax("expected ( after a function name");

                        return node;
                }

                awk_print_depth = 0;
                awk_next_token();
                awk_skip_newlines();

                while (awk_token != T_CLOSE)
                        if (!awk_expression_list_one(node, address_of last))
                                break;

                awk_print_depth = kept;
                awk_expect(T_CLOSE, "expected ) after arguments");

                if (node->count < awk_builtin_least[which] ||
                    node->count > awk_builtin_most[which])
                        awk_syntax("wrong number of arguments to a function");

                if (which == B_SPLIT && node->a->next->kind != N_VARIABLE)
                        awk_syntax("split wants an array");

                return node;
        }

        case T_GETLINE:
        {
                awk_next_token();
                node = awk_node_new(N_GETLINE);
                node->sub = G_MAIN;

                if (awk_token == T_NAME || awk_token == T_DOLLAR)
                {
                        node->a = awk_primary();

                        if (!awk_is_lvalue(node->a))
                                awk_syntax("getline wants a variable");
                }

                if (awk_token == T_LESS)
                {
                        awk_next_token();
                        node->sub = G_FILE;
                        node->b = awk_concat_level();
                }

                return node;
        }
        }

        awk_syntax("unexpected token");
}

static awk_node address_to awk_postfix()
{
        awk_node address_to node = awk_primary();

        while ((awk_token == T_PLUS_PLUS || awk_token == T_MINUS_MINUS) &&
               awk_is_lvalue(node))
        {
                awk_node address_to made = awk_node_new(N_STEP);

                made->sub = (p8)(awk_token == T_PLUS_PLUS ? 1 : 0);
                made->count = 0;
                made->a = node;
                node = made;
                awk_next_token();
        }

        return node;
}

static awk_node address_to awk_unary();

static awk_node address_to awk_power_level()
{
        awk_node address_to node = awk_postfix();

        if (awk_token == T_POWER)
        {
                awk_node address_to made = awk_node_new(N_ARITH);

                awk_next_token();
                made->sub = '^';
                made->a = node;
                made->b = awk_unary();
                return made;
        }

        return node;
}

static awk_node address_to awk_unary()
{
        awk_parse_room();

        if (awk_token == T_MINUS || awk_token == T_PLUS || awk_token == T_NOT)
        {
                b32 which = awk_token;
                awk_node address_to made =
                    awk_node_new(which == T_MINUS ? N_NEGATE
                                                  : (which == T_PLUS ? N_AFFIRM : N_NOT));

                awk_next_token();
                made->a = awk_unary();
                return made;
        }

        return awk_power_level();
}

/* Left-associative binary levels share one parser transition. Conditions,
   operator maps and newline policy remain literal at each expansion. */
#define AWK_BINARY_LEVEL(name, lower, kind, accepts, operation, newlines)    \
        static awk_node address_to name()                                   \
        {                                                                   \
                awk_node address_to node = lower();                         \
                                                                            \
                while (accepts)                                             \
                {                                                           \
                        awk_node address_to made = awk_node_new(kind);       \
                                                                            \
                        made->sub = (p8)(operation);                         \
                        awk_next_token();                                   \
                        if (newlines)                                       \
                                awk_skip_newlines();                        \
                        made->a = node;                                     \
                        made->b = lower();                                  \
                        node = made;                                        \
                }                                                           \
                                                                            \
                return node;                                                \
        }

AWK_BINARY_LEVEL(awk_multiply_level, awk_unary, N_ARITH,
                awk_token == T_TIMES || awk_token == T_DIVIDE ||
                    awk_token == T_MODULO,
                awk_token == T_TIMES ? '*' : (awk_token == T_DIVIDE ? '/' : '%'), false)
AWK_BINARY_LEVEL(awk_add_level, awk_multiply_level, N_ARITH,
                awk_token == T_PLUS || awk_token == T_MINUS,
                awk_token == T_PLUS ? '+' : '-', false)

static awk_node address_to awk_concat_level()
{
        awk_node address_to node = awk_add_level();

        while (awk_starts_operand())
        {
                awk_node address_to made = awk_node_new(N_CONCAT);

                made->a = node;
                made->b = awk_add_level();
                node = made;
        }

        return node;
}

static awk_node address_to awk_in_level()
{
        awk_node address_to node = awk_concat_level();

        while (awk_token == T_IN)
        {
                awk_node address_to made = awk_node_new(N_IN);

                awk_next_token();

                if (awk_token != T_NAME)
                        awk_syntax("in wants an array");

                made->index = awk_resolve(awk_token_text);
                made->a = node;
                awk_next_token();
                node = made;
        }

        return node;
}

static awk_node address_to awk_pipe_level();

// A comparison's operands can each be a "cmd" | getline, which is how the
// reference grammar puts it: the pipe binds tighter than the comparison, so
// while ("cmd" | getline line > 0) reads until the command is done.
static awk_node address_to awk_relational_level();

// ~ and !~ bind looser than a comparison, so "a" ~ "a" == 1 matches against
// the comparison's answer; a chain of them reads left to right, as the
// reference reads it.
AWK_BINARY_LEVEL(awk_compare_level, awk_relational_level, N_MATCH,
                awk_token == T_MATCH || awk_token == T_UNMATCH,
                awk_token == T_UNMATCH, false)

static bool awk_relational_token(b32 which)
{
        if (which == T_GREATER && awk_print_depth)
                return false;

        return which == T_LESS || which == T_LESS_EQUAL || which == T_GREATER ||
               which == T_GREATER_EQUAL || which == T_EQUAL || which == T_UNEQUAL;
}

// The comparisons do not associate: 1 == 1 == 1 is a syntax error, in the
// grammar and in the reference.
static awk_node address_to awk_relational_level()
{
        awk_node address_to node = awk_pipe_level();

        if (!awk_relational_token(awk_token))
                return node;

        awk_node address_to made = awk_node_new(N_COMPARE);

        made->sub = (p8)awk_token;
        awk_next_token();
        made->a = node;
        made->b = awk_pipe_level();

        if (awk_relational_token(awk_token))
                awk_syntax("a comparison cannot be compared again");

        return made;
}

// "command" | getline sits between && and a comparison, which is where the
// grammar puts it and not where anybody would guess.
static awk_node address_to awk_pipe_level()
{
        awk_node address_to node = awk_in_level();

        while (awk_token == T_PIPE)
        {
                awk_place place;

                awk_mark(address_of place);
                awk_next_token();

                if (awk_token != T_GETLINE)
                {
                        awk_reset(address_of place);
                        break;
                }

                awk_forget(address_of place);
                awk_next_token();

                awk_node address_to made = awk_node_new(N_GETLINE);

                made->sub = G_COMMAND;
                made->b = node;

                if (awk_token == T_NAME || awk_token == T_DOLLAR)
                {
                        made->a = awk_primary();

                        if (!awk_is_lvalue(made->a))
                                awk_syntax("getline wants a variable");
                }

                node = made;
        }

        return node;
}

AWK_BINARY_LEVEL(awk_and_level, awk_compare_level, N_AND, awk_token == T_AND, 0, true)
AWK_BINARY_LEVEL(awk_or_level, awk_and_level, N_OR, awk_token == T_OR, 0, true)
#undef AWK_BINARY_LEVEL

static awk_node address_to awk_expression()
{
        awk_node address_to node = awk_or_level();

        if (awk_token == T_QUESTION)
        {
                awk_node address_to made = awk_node_new(N_COND);

                awk_next_token();
                awk_skip_newlines();
                made->a = node;
                made->b = awk_expression();
                awk_skip_newlines();
                awk_expect(T_COLON, "expected : in ?:");
                awk_skip_newlines();
                made->c = awk_expression();
                return made;
        }

        switch (awk_token)
        {
        case T_ASSIGN:
        case T_ASSIGN_ADD:
        case T_ASSIGN_SUB:
        case T_ASSIGN_MUL:
        case T_ASSIGN_DIV:
        case T_ASSIGN_MOD:
        case T_ASSIGN_POWER:
        {
                if (!awk_is_lvalue(node))
                        awk_syntax("assignment wants a variable on the left");

                awk_node address_to made = awk_node_new(N_ASSIGN);

                made->sub = awk_token == T_ASSIGN ? 0
                                : (p8)"+-*/%^"[awk_token - T_ASSIGN_ADD];
                awk_next_token();
                awk_skip_newlines();
                made->a = node;
                made->b = awk_expression();
                return made;
        }
        }

        return node;
}

static bool awk_statement_ends()
{
        return awk_token == T_SEMICOLON || awk_token == T_NEWLINE ||
               awk_token == T_CLOSE_BRACE || awk_token == T_END;
}

static fn awk_finish_statement()
{
        if (awk_token == T_SEMICOLON || awk_token == T_NEWLINE)
        {
                awk_next_token();
                awk_skip_terminators();
                return;
        }

        if (awk_token == T_CLOSE_BRACE || awk_token == T_END)
                return;

        awk_syntax("expected ; or a newline");
}

static awk_node address_to awk_print_statement(bool formatted)
{
        awk_node address_to node = awk_node_new(formatted ? S_PRINTF : S_PRINT);
        awk_node address_to last = null;

        awk_next_token();
        awk_print_depth++;

        if (!awk_statement_ends() && awk_token != T_GREATER && awk_token != T_APPEND &&
            awk_token != T_PIPE)
                for (;;)
                        if (!awk_expression_list_one(node, address_of last))
                                break;

        // print (a, b) is the list in parentheses, not one expression.
        if (node->count == 1 && node->a->kind == N_GROUP)
        {
                node->count = node->a->count;
                node->a = node->a->a;
        }

        if (awk_token == T_GREATER || awk_token == T_APPEND || awk_token == T_PIPE)
        {
                node->sub = (p8)(awk_token == T_GREATER ? R_FILE
                                                        : (awk_token == T_APPEND ? R_APPEND
                                                                                 : R_PIPE));
                awk_next_token();
                node->b = awk_concat_level();
        }

        awk_print_depth--;
        return node;
}

static awk_node address_to awk_simple_statement()
{
        awk_node address_to node;

        switch (awk_token)
        {
        case T_PRINT: return awk_print_statement(false);
        case T_PRINTF: return awk_print_statement(true);

        case T_DELETE:
        {
                awk_next_token();

                if (awk_token != T_NAME)
                        awk_syntax("delete wants an array");

                node = awk_node_new(S_DELETE);
                node->index = awk_resolve(awk_token_text);
                awk_next_token();

                if (awk_token == T_OPEN_SQUARE)
                {
                        awk_next_token();
                        node->a = awk_subscript_list(node->index);
                        awk_expect(T_CLOSE_SQUARE, "expected ]");
                }

                return node;
        }

        case T_NEXT:
        case T_NEXTFILE:
                if (!awk_inside_function && awk_parsing_rule != RULE_PLAIN)
                        awk_syntax(awk_token == T_NEXT ? "next used in BEGIN or END"
                                                       : "nextfile used in BEGIN or END");

                node = awk_node_new((p8)(awk_token == T_NEXT ? S_NEXT : S_NEXTFILE));
                awk_next_token();
                return node;

        case T_BREAK:
        case T_CONTINUE:
                if (!awk_loop_depth)
                        awk_syntax(awk_token == T_BREAK ? "break is not allowed outside a loop"
                                                        : "continue is not allowed outside a loop");

                node = awk_node_new((p8)(awk_token == T_BREAK ? S_BREAK : S_CONTINUE));
                awk_next_token();
                return node;

        case T_EXIT:
                awk_next_token();
                node = awk_node_new(S_EXIT);

                if (!awk_statement_ends())
                        node->a = awk_expression();

                return node;

        case T_RETURN:
                if (!awk_inside_function)
                        awk_syntax("return used outside a function");

                awk_next_token();
                node = awk_node_new(S_RETURN);

                if (!awk_statement_ends())
                        node->a = awk_expression();

                return node;
        }

        node = awk_node_new(S_EXPRESSION);
        node->a = awk_expression();
        return node;
}

/* A loop's body. A semicolon where the statement would go is an empty one,
   which while and the three-clause for spell the same way, and a body that is
   there is parsed knowing a break has somewhere to go. */
static awk_node address_to awk_loop_body(awk_node address_to node)
{
        if (awk_token == T_SEMICOLON)
        {
                awk_next_token();
                awk_skip_terminators();
                return node;
        }

        awk_loop_depth++;
        node->d = awk_statement();
        awk_loop_depth--;
        return node;
}

static awk_node address_to awk_statement()
{
        awk_node address_to node;

        awk_parse_room();

        switch (awk_token)
        {
        case T_SEMICOLON:
                awk_next_token();
                awk_skip_terminators();
                return awk_node_new(S_BLOCK);

        case T_OPEN_BRACE:
                awk_next_token();
                node = awk_node_new(S_BLOCK);
                node->a = awk_statement_list(T_CLOSE_BRACE);
                awk_expect(T_CLOSE_BRACE, "expected }");

                if (awk_token == T_SEMICOLON)
                        awk_next_token();

                awk_skip_terminators();
                return node;

        case T_IF:
                awk_next_token();
                awk_expect(T_OPEN, "expected ( after if");
                node = awk_node_new(S_IF);
                node->a = awk_expression();
                awk_expect(T_CLOSE, "expected ) after the condition");
                awk_skip_newlines();
                node->b = awk_statement();
                awk_skip_terminators();

                if (awk_token == T_ELSE)
                {
                        awk_next_token();
                        awk_skip_newlines();
                        node->c = awk_statement();
                }

                return node;

        case T_WHILE:
                awk_next_token();
                awk_expect(T_OPEN, "expected ( after while");
                node = awk_node_new(S_LOOP);
                node->b = awk_expression();
                awk_expect(T_CLOSE, "expected ) after the condition");
                awk_skip_newlines();
                return awk_loop_body(node);

        case T_DO:
                awk_next_token();
                awk_skip_newlines();
                node = awk_node_new(S_LOOP);
                node->sub = 1;
                awk_loop_depth++;
                node->d = awk_statement();
                awk_loop_depth--;
                awk_skip_terminators();
                awk_expect(T_WHILE, "expected while after do");
                awk_expect(T_OPEN, "expected ( after while");
                node->b = awk_expression();
                awk_expect(T_CLOSE, "expected ) after the condition");
                return node;

        case T_FOR:
        {
                awk_place place;

                awk_next_token();
                awk_expect(T_OPEN, "expected ( after for");

                if (awk_token == T_NAME)
                {
                        awk_text address_to name = awk_text_hold(awk_token_text);

                        awk_mark(address_of place);
                        awk_next_token();

                        if (awk_token == T_IN)
                        {
                                awk_forget(address_of place);
                                awk_next_token();

                                if (awk_token != T_NAME)
                                        awk_syntax("for wants an array after in");

                                node = awk_node_new(S_FORIN);
                                node->index = awk_resolve(name);
                                node->count = awk_resolve(awk_token_text);
                                awk_text_drop(name);
                                awk_next_token();
                                awk_expect(T_CLOSE, "expected ) after in");
                                awk_skip_newlines();
                                awk_loop_depth++;
                                node->b = awk_statement();
                                awk_loop_depth--;
                                return node;
                        }

                        awk_reset(address_of place);
                        awk_text_drop(name);
                }

                node = awk_node_new(S_LOOP);

                if (awk_token != T_SEMICOLON)
                        node->a = awk_simple_statement();

                awk_expect(T_SEMICOLON, "expected ; in for");
                awk_skip_newlines();

                if (awk_token != T_SEMICOLON)
                        node->b = awk_expression();

                awk_expect(T_SEMICOLON, "expected ; in for");
                awk_skip_newlines();

                if (awk_token != T_CLOSE)
                        node->c = awk_simple_statement();

                awk_expect(T_CLOSE, "expected ) after for");
                awk_skip_newlines();
                return awk_loop_body(node);
        }
        }

        node = awk_simple_statement();
        awk_finish_statement();
        return node;
}

static awk_node address_to awk_statement_list(b32 stop)
{
        awk_node address_to first = null;
        awk_node address_to last = null;

        awk_skip_terminators();

        while (awk_token != stop && awk_token != T_END)
        {
                awk_node address_to one = awk_statement();

                if (last)
                        last->next = one;
                else
                        first = one;

                last = one;
                awk_skip_terminators();
        }

        return first;
}

// The variables awk itself defines: no function or parameter may be one.
static bool awk_special_name(awk_text address_to name)
{
        static string_address specials[] = {
            "FS", "OFS", "ORS", "RS", "NR", "NF", "FNR", "FILENAME", "SUBSEP",
            "RSTART", "RLENGTH", "CONVFMT", "OFMT", "ENVIRON", "ARGV", "ARGC"};

        for (positive i = 0; i < array_count(specials); i++)
                if (awk_name_is(name, specials[i]))
                        return true;

        return false;
}

static fn awk_parse_function()
{
        awk_next_token();

        if (awk_token != T_NAME && awk_token != T_CALL_NAME)
                awk_syntax("function wants a name");

        if (awk_special_name(awk_token_text))
                awk_syntax("function name is a variable awk defines");

        b32 which = awk_function_named(awk_token_text);

        if (awk_functions[which].defined)
                awk_syntax("function defined twice");

        awk_next_token();
        awk_expect(T_OPEN, "expected ( after a function name");
        awk_local_count = 0;
        awk_skip_newlines();

        while (awk_token != T_CLOSE)
        {
                if (awk_token != T_NAME)
                        awk_syntax("expected a parameter name");

                if (awk_local_count == AWK_LOCALS_MAX)
                        awk_syntax("too many parameters");

                // Not the function's own name, not a variable awk defines,
                // and not a name already among the parameters.
                if (awk_special_name(awk_token_text) ||
                    awk_text_is(awk_functions[which].name, awk_token_text->text,
                                awk_token_text->length))
                        awk_syntax("parameter name is taken");

                for (b32 i = 0; i < awk_local_count; i++)
                        if (awk_text_is(awk_local_names[i], awk_token_text->text,
                                        awk_token_text->length))
                                awk_syntax("parameter named twice");

                awk_local_names[awk_local_count++] = awk_text_hold(awk_token_text);
                awk_next_token();

                if (awk_token != T_COMMA)
                        break;

                awk_next_token();
                awk_skip_newlines();
        }

        awk_expect(T_CLOSE, "expected ) after the parameters");
        awk_skip_newlines();

        awk_functions[which].parameters = awk_local_count;
        awk_functions[which].defined = true;
        awk_inside_function = true;

        awk_expect(T_OPEN_BRACE, "expected { after a function header");
        awk_functions[which].body = awk_statement_list(T_CLOSE_BRACE);
        awk_expect(T_CLOSE_BRACE, "expected } after a function body");

        awk_inside_function = false;
        awk_local_count = 0;
}

static fn awk_parse_program()
{
        bool after_rule = false;

        awk_next_token();

        for (;;)
        {
                awk_skip_newlines();

                // A semicolon ends the rule before it, across newlines; on
                // its own, or twice, it is a rule with nothing in it, which
                // the reference refuses.
                if (awk_token == T_SEMICOLON)
                {
                        if (!after_rule)
                                awk_syntax("each rule must have a pattern or an action");

                        after_rule = false;
                        awk_next_token();
                        continue;
                }

                if (awk_token == T_END)
                        break;

                after_rule = true;

                if (awk_token == T_FUNCTION)
                {
                        awk_parse_function();
                        continue;
                }

                if (awk_rule_count == AWK_RULES_MAX)
                        awk_syntax("too many rules");

                awk_rule address_to rule = address_of awk_rules[awk_rule_count++];

                rule->kind = RULE_PLAIN;
                rule->first = null;
                rule->second = null;
                rule->action = null;
                rule->running = false;

                if (awk_token == T_BEGIN || awk_token == T_FINISH)
                {
                        rule->kind = (p8)(awk_token == T_BEGIN ? RULE_BEGIN : RULE_END);
                        awk_next_token();
                        awk_skip_newlines();

                        if (awk_token != T_OPEN_BRACE)
                                awk_syntax("BEGIN and END want an action");
                }
                else if (awk_token != T_OPEN_BRACE)
                {
                        rule->first = awk_expression();

                        if (awk_token == T_COMMA)
                        {
                                awk_next_token();
                                awk_skip_newlines();
                                rule->second = awk_expression();
                        }
                }

                if (awk_token == T_OPEN_BRACE)
                {
                        awk_next_token();
                        awk_parsing_rule = rule->kind;
                        rule->action = awk_statement_list(T_CLOSE_BRACE);
                        awk_parsing_rule = RULE_PLAIN;
                        awk_expect(T_CLOSE_BRACE, "expected } after an action");

                        // An action with nothing in it still is one: the
                        // record is not printed.
                        if (!rule->action)
                                rule->action = awk_node_new(S_BLOCK);
                }
                else if (!awk_statement_ends())
                        awk_syntax("expected an action or the end of the rule");
        }
}

/*
        Running it.

        A tree walk. Statements answer with what should happen next -- keep
        going, leave the loop, leave the record, leave the program -- and that
        answer travels back up through everything that called them, which is
        how next inside a function inside an if gets out to the record loop.
*/
enum
{
        RUN_ON = 0,
        RUN_BREAK,
        RUN_CONTINUE,
        RUN_NEXT,
        RUN_NEXTFILE,
        RUN_EXIT,
        RUN_RETURN
};

enum
{
        LV_CELL = 0,
        LV_FIELD,
        LV_ELEMENT
};

typedef struct
{
        p8 kind;
        b32 index;
        b32 field;
        awk_cell address_to cell;
        awk_slot address_to entry;
} awk_target;

static b32 awk_exit_code;
static bool awk_exiting;
// next or nextfile said inside a function: the call cannot answer with it,
// so it is kept here and the statement after the call answers with it.
static b32 awk_skipping;
static awk_value awk_returned;
static positive awk_seed = 1;
static positive awk_seed_state = 1;

static fn awk_eval(awk_node address_to node, awk_value address_to out);
static b32 awk_run(awk_node address_to node);
static b32 awk_main_next_record(awk_text address_to address_to into);

#define awk_value_start(which)          \
        do                              \
        {                               \
                (which).text = null;    \
                (which).number = 0;     \
                (which).state = AWK_UNSET; \
        } while (0)

static fn awk_value_done(awk_value address_to which)
{
        awk_text_drop(which->text);
        which->text = null;
        which->state = AWK_UNSET;
}

static DEAD_END fn awk_leave(b32 code)
{
        // The redirections end before standard output goes out, as the
        // reference awk has it: a command still on a pipe writes first, and
        // what the program printed after opening it comes after.
        awk_close_everything();
        awk_writer_flush(address_of awk_standard_out);

        if (awk_write_failed && !code)
                code = 1;

        exit(code & 0xff);
}

// A runtime error, about WORD where there is one: everything the program
// wrote goes out first, then the word and its message, and the program ends.
static DEAD_END fn awk_die(string_address word, string_address message)
{
        awk_flush_everything();
        awk_leave(string_diagnostic(&text_diagnostic, 2, word, message));
}

static awk_text address_to awk_eval_text(awk_node address_to node)
{
        awk_value one;
        awk_value_start(one);
        awk_eval(node, address_of one);
        awk_text address_to value = awk_text_hold(awk_to_text(address_of one));
        awk_value_done(address_of one);
        return value;
}

static bool awk_eval_compare(awk_node address_to node)
{
        b32 order;
        // A literal RHS cannot mutate the variable while it is borrowed.
        // Other expressions keep owned snapshots and left-to-right order.
        if (node->a->kind == N_VARIABLE && node->b->kind == N_NUMBER &&
            awk_numeric_side(address_of awk_cell_of(node->a->index)->value))
                order = awk_compare_numbers(
                    awk_to_number(address_of awk_cell_of(node->a->index)->value),
                    node->b->number);
        else
        {
                awk_value left;
                awk_value right;
                awk_value_start(left);
                awk_value_start(right);
                awk_eval(node->a, address_of left);
                awk_eval(node->b, address_of right);
                order = awk_compare(address_of left, address_of right);
                awk_value_done(address_of left);
                awk_value_done(address_of right);
        }

        switch (node->sub)
        {
        case T_LESS: return order == -1;
        case T_LESS_EQUAL: return order == -1 || order == 0;
        case T_GREATER: return order == 1;
        case T_GREATER_EQUAL: return order == 1 || order == 0;
        case T_EQUAL: return order == 0;
        default: return order != 0;
        }
}

static bool awk_eval_truth(awk_node address_to node)
{
        if (node->kind == N_COMPARE)
                return awk_eval_compare(node);
        if (node->kind == N_VARIABLE)
                return awk_truth(address_of awk_cell_of(node->index)->value);
        if (node->kind == N_NUMBER)
                return node->number != 0;

        awk_value one;
        awk_value_start(one);
        awk_eval(node, address_of one);
        bool value = awk_truth(address_of one);
        awk_value_done(address_of one);
        return value;
}

static inline INLINE decimal awk_arithmetic(p8 operation, decimal left, decimal right)
{
        switch (operation)
        {
        case '+': return left + right;
        case '-': return left - right;
        case '*': return left * right;
        case '/':
                if (right == 0)
                        awk_die(null, "division by zero attempted");
                return left / right;
        case '%':
                if (right == 0)
                        awk_die(null, "division by zero attempted in %");
                return decimal_modulo(left, right);
        }
        return awk_power(left, right);
}

static decimal awk_eval_number(awk_node address_to node)
{
        // An arithmetic tree recurses here and never through awk_eval, so
        // this path needs the same measure: 1+1+...+1 two hundred thousand
        // terms long is a left-leaning tree that deep.
        if_rare (!awk_walk_room_left())
                awk_leave((awk_flush_everything(), text_flush(),
                           string_diagnostic(&text_diagnostic, 2, null,
                                             "nested too deeply")));

        // Numeric consumers need no owned values along an arithmetic tree.
        // Convert in place so input classification is cached on the source,
        // rather than repeated on a temporary copy on every loop iteration.
        switch (node->kind)
        {
        case N_NUMBER:
                return node->number;
        case N_VARIABLE:
                return awk_to_number(address_of awk_cell_of(node->index)->value);
        case N_FIELD:
                if (node->sub && node->index > 0 && node->index <= awk_nf)
                        return awk_to_number(address_of awk_fields[node->index]);
                return awk_to_number(awk_field(awk_whole(awk_eval_number(node->a))));
        case N_NEGATE:
                return -awk_eval_number(node->a);
        case N_AFFIRM:
                return awk_eval_number(node->a);
        case N_ARITH:
        {
                decimal left = awk_eval_number(node->a);
                decimal right = awk_eval_number(node->b);

                return awk_arithmetic(node->sub, left, right);
        }
        case N_BUILTIN:
                if (node->index >= B_SIN && node->index <= B_INT)
                {
                        decimal first = node->count ? awk_eval_number(node->a) : 0;
                        switch (node->index)
                        {
                        case B_SIN: return sine(first);
                        case B_COS: return cosine(first);
                        case B_ATAN2: return atan2(first, awk_eval_number(node->a->next));
                        case B_EXP: return exponential(first);
                        case B_LOG: return logarithm(first);
                        case B_SQRT: return square_root(first);
                        case B_INT: return awk_truncate(first);
                        }
                }
                break;
        }

        awk_value one;
        awk_value_start(one);
        awk_eval(node, address_of one);
        decimal value = awk_to_number(address_of one);
        awk_value_done(address_of one);
        return value;
}

// The key a subscript list makes, which is the pieces joined by SUBSEP.
static awk_text address_to awk_subscript_key(awk_node address_to list, b32 count)
{
        if (count == 1)
                return awk_eval_text(list);

        awk_builder build;

        awk_builder_start(address_of build);

        for (awk_node address_to one = list; one; one = one->next)
        {
                awk_text address_to separator = one == list
                    ? null
                    : awk_text_hold(awk_special_text(awk_where_subsep));
                awk_text address_to piece = awk_eval_text(one);

                if (separator)
                {
                        awk_builder_put(address_of build, separator->text,
                                        separator->length);
                        awk_text_drop(separator);
                }

                awk_builder_put(address_of build, piece->text, piece->length);
                awk_text_drop(piece);
        }

        return awk_builder_text(address_of build);
}

static inline INLINE fn awk_target_field_locate(awk_node address_to node,
                                  awk_target address_to into)
{
        into->kind = LV_FIELD;
        into->field = node->sub ? node->index
                                : awk_whole(awk_eval_number(node->a));
        into->index = node->sub;

        if (into->field < 0)
                awk_die(null, "attempt to assign to a field before the first");
}

static inline INLINE fn awk_target_field_prepare(awk_target address_to into)
{
        if (into->field > awk_nf)
                awk_field_grow(into->field);

        // sub(/^ /, "") after $1 = "" works on the record the fields now
        // spell, not the one that was read.
        if (into->field == 0 && awk_record_stale)
                awk_record_rebuild();
}

static fn awk_target_of(awk_node address_to node, awk_target address_to into)
{
        into->field = -1;
        into->index = 0;
        into->cell = null;
        into->entry = null;

        switch (node->kind)
        {
        case N_VARIABLE:
                into->kind = LV_CELL;
                into->index = node->index;
                into->cell = awk_cell_of(node->index);
                return;

        case N_FIELD:
                awk_target_field_locate(node, into);
                awk_target_field_prepare(into);
                return;

        case N_SUBSCRIPT:
        {
                into->kind = LV_ELEMENT;
                awk_text address_to key = awk_subscript_key(node->a, node->count);
                into->cell = awk_cell_of(node->index);
                awk_array address_to array = awk_cell_array(into->cell);

                into->entry = awk_array_place(array, key->text, key->length);
                awk_text_drop(key);

                return;
        }
        }

        awk_die(null, "not something that can be assigned to");
}

static bool awk_target_of_sub(awk_node address_to node, awk_target address_to into)
{
        if (node->kind != N_FIELD)
        {
                awk_target_of(node, into);
                return false;
        }

        awk_target_field_locate(node, into);

        // Extending fields that already spell the record must make a later
        // $0 include the new empty fields even when sub() finds no match.
        bool rebuild_on_miss = into->field > awk_nf && awk_record_separator;

        awk_target_field_prepare(into);
        return rebuild_on_miss;
}

static awk_value address_to awk_target_slot(awk_target address_to which)
{
        switch (which->kind)
        {
        case LV_FIELD:
                if (which->index && which->field > 0 && which->field <= awk_nf)
                        return address_of awk_fields[which->field];

                return awk_field(which->field);

        case LV_ELEMENT:
                return address_of which->entry->value;
        }

        return address_of which->cell->value;
}

static fn awk_target_written(awk_target address_to which)
{
        if (which->kind == LV_FIELD)
        {
                awk_field_written(which->field);
                return;
        }

        if (which->kind != LV_CELL || which->index < 0)
                return;

        if (awk_global_meaning[which->index] == AWK_NF)
                awk_nf_written(awk_whole(awk_to_number(address_of which->cell->value)));
}

static fn awk_do_assign(awk_node address_to node, awk_value address_to out)
{
        awk_target target;

        if (!node->sub)
        {
                awk_value right;
                awk_value_start(right);
                awk_eval(node->b, address_of right);
                awk_target_of(node->a, address_of target);
                awk_value_copy(awk_target_slot(address_of target), address_of right);
                awk_target_written(address_of target);
                awk_value_copy(out, address_of right);
                awk_value_done(address_of right);
                return;
        }

        decimal value = awk_eval_number(node->b);

        // Compound assignment follows the reference's right-before-left
        // order too: the RHS may change the field number or array key used by
        // the target expression before that expression is evaluated.
        awk_target_of(node->a, address_of target);
        decimal left = awk_to_number(awk_target_slot(address_of target));

        left = awk_arithmetic(node->sub, left, value);

        awk_set_number(awk_target_slot(address_of target), left);
        awk_target_written(address_of target);
        awk_set_number(out, left);
}

static regex_program address_to awk_program_of(awk_node address_to node)
{
        if (node->kind == N_REGEX)
        {
                if (node->program)
                        return node->program;

                return awk_regex_dynamic(node->text);
        }

        awk_text address_to pattern = awk_eval_text(node);
        regex_program address_to made = awk_regex_dynamic(pattern);

        awk_text_drop(pattern);
        return made;
}

static bool awk_matches(awk_node address_to pattern, awk_text address_to subject)
{
        regex_current = *awk_program_of(pattern);
        return regex_find(REGEX_FIRST, subject->text, subject->length, 0);
}

static fn awk_call(awk_node address_to node, awk_value address_to out);

static fn awk_builtin(awk_node address_to node, awk_value address_to out);
static awk_text address_to awk_format_list(awk_text address_to format,
                                           awk_node address_to arguments,
                                           b32 want);

static fn awk_getline(awk_node address_to node, awk_value address_to out);

static fn awk_eval(awk_node address_to node, awk_value address_to out)
{
        if_rare (!awk_walk_room_left())
                awk_leave((awk_flush_everything(), text_flush(),
                           string_diagnostic(&text_diagnostic, 2, null,
                                             "nested too deeply")));

        switch (node->kind)
        {
        case N_NUMBER:
                awk_set_number(out, node->number);
                return;

        case N_STRING:
                awk_set_text(out, awk_text_hold(node->text));
                return;

        case N_REGEX:
        {
                awk_value address_to record = awk_field(0);

                awk_set_number(out, awk_matches(node, awk_to_text(record)) ? 1 : 0);
                return;
        }

        case N_VARIABLE:
                awk_value_copy(out, address_of awk_cell_of(node->index)->value);
                return;

        case N_FIELD:
                if (node->sub && node->index > 0 && node->index <= awk_nf)
                        awk_value_copy(out, address_of awk_fields[node->index]);
                else
                        awk_value_copy(out, awk_field(awk_whole(awk_eval_number(node->a))));
                return;

        case N_SUBSCRIPT:
        {
                awk_text address_to key = awk_subscript_key(node->a, node->count);
                awk_slot address_to slot =
                    awk_array_place(awk_cell_array(awk_cell_of(node->index)), key->text,
                                    key->length);

                awk_text_drop(key);
                awk_value_copy(out, address_of slot->value);
                return;
        }

        case N_GROUP:
                awk_eval(node->a, out);
                return;

        case N_ASSIGN:
                awk_do_assign(node, out);
                return;

        case N_COND:
                awk_eval(awk_eval_truth(node->a) ? node->b : node->c, out);
                return;

        case N_OR:
                awk_set_number(out, awk_eval_truth(node->a) || awk_eval_truth(node->b) ? 1 : 0);
                return;

        case N_AND:
                awk_set_number(out, awk_eval_truth(node->a) && awk_eval_truth(node->b) ? 1 : 0);
                return;

        case N_NOT:
                awk_set_number(out, awk_eval_truth(node->a) ? 0 : 1);
                return;

        case N_IN:
        {
                awk_text address_to key;

                if (node->a->kind == N_GROUP)
                        key = awk_subscript_key(node->a->a, node->a->count);
                else
                        key = awk_eval_text(node->a);

                awk_array address_to array = awk_cell_array(awk_cell_of(node->index));

                awk_set_number(out, awk_array_find(array, key->text, key->length) ? 1 : 0);
                awk_text_drop(key);
                return;
        }

        case N_MATCH:
        {
                awk_text address_to subject = awk_eval_text(node->a);
                bool got = awk_matches(node->b, subject);

                awk_text_drop(subject);
                awk_set_number(out, (got != (node->sub != 0)) ? 1 : 0);
                return;
        }

        case N_COMPARE:
                awk_set_number(out, awk_eval_compare(node) ? 1 : 0);
                return;

        case N_CONCAT:
        {
                awk_text address_to left = awk_eval_text(node->a);
                awk_text address_to right = awk_eval_text(node->b);
                awk_text address_to made = awk_text_room(awk_size_add(left->length, right->length));

                memory_copy_apart(made->text, left->text, left->length);
                memory_copy_apart(made->text + left->length, right->text, right->length);
                awk_text_drop(left);
                awk_text_drop(right);
                awk_set_text(out, made);
                return;
        }

        case N_ARITH:
        case N_NEGATE:
        case N_AFFIRM:
                awk_set_number(out, awk_eval_number(node));
                return;

        case N_STEP:
        {
                awk_target target;

                awk_target_of(node->a, address_of target);

                decimal before = awk_to_number(awk_target_slot(address_of target));
                decimal after = node->sub ? before + 1 : before - 1;

                awk_set_number(awk_target_slot(address_of target), after);
                awk_target_written(address_of target);
                awk_set_number(out, node->count ? after : before);
                return;
        }

        case N_CALL:
                awk_call(node, out);
                return;

        case N_BUILTIN:
                awk_builtin(node, out);
                return;

        case N_GETLINE:
                awk_getline(node, out);
                return;
        }

        awk_die(null, "cannot evaluate this");
}

static fn awk_call(awk_node address_to node, awk_value address_to out)
{
        awk_function address_to which = address_of awk_functions[node->index];

        if (!which->defined)
                awk_die(which->name->text, "calling a function that is not defined");

        b32 base = awk_frame + awk_frame_size;
        b32 count = which->parameters;
        b32 kept_frame = awk_frame;
        b32 kept_size = awk_frame_size;
        awk_node address_to argument = node->a;

        b32 mark = 0;

        if (base + count >= AWK_FRAME_MAX ||
            awk_stack_start - (positive)address_of mark > awk_stack_room)
                awk_die(which->name->text, "too deep");

        awk_frame_size += count;

        for (b32 i = 0; i < count; i++)
        {
                awk_cell address_to cell = address_of awk_stack[base + i];

                cell->kind = AWK_CELL_UNKNOWN;
                cell->owned = false;
                cell->link = null;
                cell->array = null;
                cell->value.text = null;
                cell->value.number = 0;
                cell->value.state = AWK_UNSET;

                if (!argument)
                        continue;

                if (argument->kind == N_VARIABLE)
                {
                        awk_cell address_to source = awk_cell_of(argument->index);

                        if (source->kind != AWK_CELL_SCALAR)
                                cell->link = source;

                        if (source->kind != AWK_CELL_ARRAY)
                                awk_value_copy(address_of cell->value, address_of source->value);
                }
                else
                        awk_eval(argument, address_of cell->value);

                argument = argument->next;
        }

        awk_frame = base;
        awk_frame_size = count;

        b32 answer = which->body ? awk_run(which->body) : RUN_ON;

        if (answer == RUN_RETURN)
        {
                // "return g()" evaluates g() straight into the return slot;
                // moving the slot onto itself and then clearing it is what
                // made every function that returned a call return nothing.
                if (out != address_of awk_returned)
                {
                        awk_value_copy(out, address_of awk_returned);
                        awk_value_done(address_of awk_returned);
                }
        }
        else
                awk_value_clear(out);

        for (b32 i = 0; i < count; i++)
        {
                awk_cell address_to cell = address_of awk_stack[base + i];

                awk_value_done(address_of cell->value);

                if (cell->owned && cell->array)
                {
                        awk_array_empty(cell->array);
                        memory_give(cell->array->buckets);
                        memory_give(cell->array);
                }

                cell->array = null;
                cell->owned = false;
                cell->link = null;
        }

        awk_frame = kept_frame;
        awk_frame_size = kept_size;

        // exit inside a function still has to leave, and the answer above
        // told the caller nothing about it. next and nextfile the same:
        // dropped here, a function's next ran the rest of the rule.
        if (answer == RUN_EXIT)
                awk_exiting = true;
        else if (answer == RUN_NEXT || answer == RUN_NEXTFILE)
                awk_skipping = answer;
}

static awk_text address_to awk_replace(awk_text address_to subject, regex_program address_to program,
                                       awk_text address_to with, bool every, b32 address_to made)
{
        awk_builder build;
        positive done = 0;
        positive at = 0;
        positive last = TEXT_UNSET;
        // Past an empty match the next attempt is one character on, which in
        // a UTF-8 locale is a whole sequence and not its first byte.
        bool wide = awk_wide(subject->text, subject->length);

        address_to made = 0;
        awk_builder_start(address_of build);

        while (at <= subject->length)
        {
                regex_current = *program;

                if (!regex_find(REGEX_LONGEST, subject->text, subject->length, at))
                        break;

                positive start = regex_slots[0];
                positive stop = regex_slots[1];

                // An empty match where the last one ended would put the same
                // replacement in twice.
                if (start == stop && start == last)
                {
                        if (start >= subject->length)
                                break;

                        at = start + (wide ? memory_utf8_span(subject->text + start,
                                                              subject->length - start, 1).x
                                           : 1);
                        continue;
                }

                awk_builder_put(address_of build, subject->text + done, start - done);

                /*
                        A backslash before an ampersand takes its meaning
                        away. A backslash before anything else is a
                        backslash, including before another backslash --
                        which is not what the standard says and is what the
                        awk this is measured against does. The one place two
                        of them collapse is directly before an ampersand that
                        is meant to be the match.
                */
                for (positive i = 0; i < with->length; i++)
                {
                        if (with->text[i] == '\\' && i + 1 < with->length)
                        {
                                if (with->text[i + 1] == '&')
                                {
                                        awk_builder_char(address_of build, '&');
                                        i++;
                                        continue;
                                }

                                // Three backslashes and an ampersand are a
                                // literal backslash and a literal ampersand,
                                // and four backslashes are two, in the
                                // reference awk's table; asked after the
                                // two-backslash case they read as that case
                                // and the match.
                                if (with->text[i + 1] == '\\' && i + 3 < with->length &&
                                    with->text[i + 2] == '\\' &&
                                    (with->text[i + 3] == '&' || with->text[i + 3] == '\\'))
                                {
                                        awk_builder_char(address_of build, '\\');
                                        awk_builder_char(address_of build, with->text[i + 3]);
                                        i += 3;
                                        continue;
                                }

                                if (with->text[i + 1] == '\\' && i + 2 < with->length &&
                                    with->text[i + 2] == '&')
                                {
                                        awk_builder_char(address_of build, '\\');
                                        i++;
                                        continue;
                                }

                                awk_builder_char(address_of build, '\\');
                                continue;
                        }

                        if (with->text[i] == '&')
                        {
                                awk_builder_put(address_of build, subject->text + start,
                                                stop - start);
                                continue;
                        }

                        awk_builder_char(address_of build, with->text[i]);
                }

                address_to made += 1;
                done = stop;
                last = stop;
                at = start == stop
                         ? start + (wide && start < subject->length
                                        ? memory_utf8_span(subject->text + start,
                                                           subject->length - start, 1).x
                                        : 1)
                         : stop;

                if (!every)
                        break;
        }

        if (!address_to made)
                return awk_text_hold(subject);

        awk_builder_put(address_of build, subject->text + done, subject->length - done);
        return awk_builder_text(address_of build);
}

static fn awk_builtin(awk_node address_to node, awk_value address_to out)
{
        awk_node address_to first = node->a;
        awk_node address_to second = first ? first->next : null;
        awk_node address_to third = second ? second->next : null;

        switch (node->index)
        {
        case B_LENGTH:
        {
                if (!node->count)
                {
                        awk_text address_to record = awk_to_text(awk_field(0));

                        awk_set_number(out, (decimal)(awk_wide(record->text, record->length)
                                                          ? awk_characters(record->text, record->length)
                                                          : record->length));
                        return;
                }

                if (first->kind == N_VARIABLE)
                {
                        awk_cell address_to cell = awk_cell_of(first->index);

                        while (cell->link)
                                cell = cell->link;

                        if (cell->kind == AWK_CELL_ARRAY && cell->array)
                        {
                                awk_set_number(out, (decimal)cell->array->count);
                                return;
                        }
                }

                awk_text address_to text = awk_eval_text(first);

                awk_set_number(out, (decimal)(awk_wide(text->text, text->length)
                                                  ? awk_characters(text->text, text->length)
                                                  : text->length));
                awk_text_drop(text);
                return;
        }

        case B_SUBSTR:
        {
                awk_text address_to text = awk_eval_text(first);
                decimal start = awk_truncate(awk_eval_number(second));
                bool wide = awk_wide(text->text, text->length);
                positive total = wide ? awk_characters(text->text, text->length) : text->length;
                positive from;
                positive want;

                if (decimal_is_nan(start) || start < 1)
                        start = 1;

                from = start > (decimal)total ? total : (positive)start - 1;

                if (third)
                {
                        decimal length = awk_truncate(awk_eval_number(third));

                        if (decimal_is_nan(length) || length < 0)
                                length = 0;

                        want = length > (decimal)total ? total : (positive)length;
                }
                else
                        want = total;

                if (from + want > total)
                        want = total - from;

                if (wide)
                {
                        // Characters to bytes: where the from'th begins, and
                        // where the want'th after it ends.
                        positive begin = memory_utf8_span(text->text, text->length, from).x;

                        from = begin;
                        want = memory_utf8_span(text->text + begin, text->length - begin, want).x;
                }

                awk_set_text(out, awk_text_new(text->text + from, want));
                awk_text_drop(text);
                return;
        }

        case B_INDEX:
        {
                awk_text address_to text = awk_eval_text(first);
                awk_text address_to want = awk_eval_text(second);
                positive answer = 0;

                if (want->length <= text->length)
                {
                        p8 address_to at = (p8 address_to)memory_search(
                            text->text, text->length, want->text, want->length);

                        if (at)
                        {
                                answer = (positive)(at - text->text);
                                answer = (awk_wide(text->text, answer)
                                              ? awk_characters(text->text, answer)
                                              : answer) + 1;
                        }
                }

                awk_text_drop(text);
                awk_text_drop(want);
                awk_set_number(out, (decimal)answer);
                return;
        }

        case B_SPLIT:
        {
                awk_text address_to text = awk_eval_text(first);
                awk_array address_to array = awk_cell_array(awk_cell_of(second->index));
                string_address separator;
                positive separator_length;
                awk_text address_to held = null;
                bool pattern = false;

                if (!third)
                        separator = awk_separator(awk_where_fs, address_of separator_length);
                else if (third->kind == N_REGEX)
                {
                        held = awk_text_hold(third->text);
                        separator = held->text;
                        separator_length = held->length;
                        pattern = true;
                }
                else
                {
                        held = awk_eval_text(third);
                        separator = held->text;
                        separator_length = held->length;
                }

                awk_array_empty(array);
                // split() shares the piece scratch with record splitting.
                // Keep any lazy current-record fields before replacing it.
                awk_fields_materialize();
                awk_split_pieces(text->text, text->length, separator, separator_length, false,
                                 pattern);

                for (positive i = 0; i < awk_piece_count; i++)
                {
                        p8 name[24];
                        positive at = positive_into(name, i + 1);

                        awk_slot address_to slot = awk_array_place(array, name, at);

                        awk_set_input_bytes(address_of slot->value,
                                            text->text + awk_pieces[i].start,
                                            awk_pieces[i].length);
                }

                awk_set_number(out, (decimal)awk_piece_count);
                awk_text_drop(text);
                awk_text_drop(held);
                return;
        }

        case B_SUB:
        case B_GSUB:
        {
                regex_program address_to program = null;
                awk_text address_to pattern = null;

                // GNU awk evaluates sub()/gsub() arguments in pattern,
                // replacement, destination order. Delay compiling a dynamic
                // pattern until the other two have finished: either can
                // compile another expression and wind the shared regex cache.
                if (first->kind == N_REGEX && first->program)
                        program = first->program;
                else if (first->kind == N_REGEX)
                        pattern = awk_text_hold(first->text);
                else
                        pattern = awk_eval_text(first);

                awk_text address_to with = awk_eval_text(second);
                awk_target target;
                awk_node address_to where = third;
                awk_node holder;
                b32 count = 0;

                if (!where)
                {
                        memory_fill(address_of holder, 0, sizeof(awk_node));
                        holder.kind = N_FIELD;
                        holder.sub = 1;
                        holder.index = 0;
                        where = address_of holder;
                }

                bool rebuild_on_miss = awk_target_of_sub(where, address_of target);

                if (!program)
                        program = awk_regex_dynamic(pattern);

                awk_text address_to subject = awk_text_hold(awk_to_text(awk_target_slot(address_of target)));
                awk_text address_to made = awk_replace(subject, program, with, node->index == B_GSUB,
                                                       address_of count);

                if (count)
                {
                        awk_set_text(awk_target_slot(address_of target), made);
                        awk_target_written(address_of target);
                }
                else
                {
                        awk_text_drop(made);

                        // Merely selecting a field beyond NF extends NF in
                        // this lvalue context, even when nothing matched.
                        if (rebuild_on_miss)
                                awk_field_written(target.field);
                }

                awk_text_drop(subject);
                awk_text_drop(with);
                awk_text_drop(pattern);
                awk_set_number(out, (decimal)count);
                return;
        }

        case B_MATCH:
        {
                awk_text address_to text = awk_eval_text(first);

                regex_current = *awk_program_of(second);

                if (regex_find(REGEX_LONGEST, text->text, text->length, 0))
                {
                        positive begin = regex_slots[0];
                        positive length = regex_slots[1] - regex_slots[0];

                        if (awk_wide(text->text, text->length))
                        {
                                length = awk_characters(text->text + begin, length);
                                begin = awk_characters(text->text, begin);
                        }

                        awk_set_global_number(awk_where_rstart, (decimal)(begin + 1));
                        awk_set_global_number(awk_where_rlength, (decimal)length);
                        awk_set_number(out, (decimal)(begin + 1));
                }
                else
                {
                        awk_set_global_number(awk_where_rstart, 0);
                        awk_set_global_number(awk_where_rlength, -1);
                        awk_set_number(out, 0);
                }

                awk_text_drop(text);
                return;
        }

        case B_SPRINTF:
        {
                awk_text address_to format = awk_eval_text(first);

                awk_set_text(out, awk_format_list(format, second, node->count - 1));
                awk_text_drop(format);
                return;
        }

        case B_SIN:
        case B_COS:
        case B_ATAN2:
        case B_EXP:
        case B_LOG:
        case B_SQRT:
        case B_INT:
                awk_set_number(out, awk_eval_number(node));
                return;

        case B_RAND:
        {
                awk_seed_state = awk_seed_state * 6364136223846793005ull + 1442695040888963407ull;

                positive value = (awk_seed_state >> 11) & (((positive)1 << 53) - 1);

                awk_set_number(out, (decimal)value / 9007199254740992.0);
                return;
        }

        case B_SRAND:
        {
                positive before = awk_seed;

                if (node->count)
                        awk_seed = (positive)awk_whole_wide(awk_eval_number(first));
                else
                {
                        timespec when = {0, 0};

                        clock_gettime(CLOCK_REALTIME, address_of when);
                        awk_seed = when.tv_sec;
                }

                awk_seed_state = awk_seed + 0x9e3779b97f4a7c15ull;
                awk_set_number(out, (decimal)before);
                return;
        }

        case B_TOLOWER:
        case B_TOUPPER:
        {
                awk_text address_to text = awk_eval_text(first);
                awk_text address_to made = awk_text_room(text->length);

                if (text->length < 256)
                {
                        p8 begin = node->index == B_TOLOWER ? 'A' : 'a';
                        b32 shift = node->index == B_TOLOWER ? 32 : -32;

                        for (positive i = 0; i < text->length; i++)
                        {
                                p8 character = text->text[i];

                                made->text[i] = character >= begin && character <= begin + 25
                                                    ? (p8)(character + shift)
                                                    : character;
                        }
                }
                else
                {
                        memory_copy_apart(made->text, text->text, text->length);

                        if (node->index == B_TOLOWER)
                                memory_to_lower_ascii(made->text, text->length);
                        else
                                memory_to_upper_ascii(made->text, text->length);
                }

                awk_text_drop(text);
                awk_set_text(out, made);
                return;
        }

        case B_SYSTEM:
        {
                awk_text address_to command = awk_eval_text(first);
                bipolar child = awk_spawn(command->text, -1, -1);

                awk_text_drop(command);
                awk_set_number(out, (decimal)awk_wait_for(child));
                return;
        }

        case B_CLOSE:
        {
                awk_text address_to name = awk_eval_text(first);

                awk_set_number(out, (decimal)awk_close_named(name));
                awk_text_drop(name);
                return;
        }

        case B_FFLUSH:
                if (node->count)
                {
                        awk_text address_to name = awk_eval_text(first);
                        b32 found = -1;

                        if (awk_name_is(name, "/dev/stdout") || awk_name_is(name, "-") ||
                            awk_name_is(name, "/dev/stderr"))
                        {
                                awk_writer_flush(address_of awk_standard_out);
                                found = 0;
                        }

                        for (b32 i = 0; i < awk_writer_count; i++)
                                if (awk_writers[i]->live &&
                                    awk_text_is(awk_writers[i]->name, name->text,
                                                name->length))
                                {
                                        awk_writer_flush(awk_writers[i]);
                                        found = 0;
                                }

                        // A name nothing is open under is -1 and a warning,
                        // as the reference awk answers it.
                        if (found < 0)
                                string_diagnostic(&text_diagnostic, 0, name->text,
                                                  "fflush: not an open file or pipe");

                        awk_text_drop(name);
                        awk_set_number(out, (decimal)found);
                        return;
                }

                awk_flush_everything();
                awk_set_number(out, 0);
                return;
        }

        awk_die(null, "no such function");
}

static fn awk_getline_store(awk_node address_to node, awk_text address_to record)
{
        if (!node->a)
        {
                awk_record_set(record->text, record->length);
                return;
        }

        awk_target target;

        awk_target_of(node->a, address_of target);
        awk_set_input(awk_target_slot(address_of target), awk_text_hold(record));
        awk_target_written(address_of target);
}

static fn awk_getline(awk_node address_to node, awk_value address_to out)
{
        awk_text address_to record = null;
        b32 answer = 0;

        if (node->sub == G_MAIN)
                answer = awk_main_next_record(address_of record);
        else
        {
                awk_text address_to name = awk_eval_text(node->b);

                if (!name->length)
                        awk_die(null, "expression for getline redirection is the null string");

                awk_reader address_to from = awk_reader_for(name, node->sub == G_COMMAND);

                awk_text_drop(name);

                answer = from ? awk_read_record(from, address_of record) : -1;
        }

        if (answer > 0)
        {
                awk_getline_store(node, record);
                awk_text_drop(record);
        }

        awk_set_number(out, (decimal)answer);
}

static awk_writer address_to awk_output_of(awk_node address_to node)
{
        if (!node->sub)
                return address_of awk_standard_out;

        awk_text address_to name = awk_eval_text(node->b);

        if (!name->length)
                awk_die(null, "expression for redirection is the null string");

        awk_writer address_to where =
            awk_writer_for(name, (p8)(node->sub == R_FILE ? AWK_TO_FILE
                                                          : (node->sub == R_APPEND ? AWK_TO_APPEND
                                                                                   : AWK_TO_PIPE)));

        awk_text_drop(name);
        return where;
}

static fn awk_do_print(awk_node address_to node)
{
        awk_builder build;
        positive ors_length;
        string_address ors;
        awk_writer address_to where;

        awk_builder_start(address_of build);

        if (!node->a)
        {
                awk_text address_to record = awk_to_text(awk_field(0));

                awk_builder_put(address_of build, record->text, record->length);
        }
        else
                for (awk_node address_to one = node->a; one; one = one->next)
                {
                        awk_value value;
                        awk_text address_to ofs = one == node->a
                            ? null
                            : awk_text_hold(awk_special_text(awk_where_ofs));

                        awk_value_start(value);
                        awk_eval(one, address_of value);

                        awk_text address_to piece = awk_to_output_text(address_of value);

                        if (ofs)
                        {
                                awk_builder_put(address_of build, ofs->text,
                                                ofs->length);
                                awk_text_drop(ofs);
                        }

                        awk_builder_put(address_of build, piece->text, piece->length);
                        awk_text_drop(piece);
                        awk_value_done(address_of value);
                }

        ors = awk_separator(awk_where_ors, address_of ors_length);
        awk_builder_put(address_of build, ors, ors_length);
        where = awk_output_of(node);
        if (!awk_writer_put(where, build.data, build.used))
                awk_write_refused(where, "print");

        if (build.heap)
                memory_give(build.data);
}

/*
        A format and the list of arguments after it, evaluated and handed to
        the formatter as one: what printf and sprintf both do before they
        differ in where the text goes. A few arguments live on the stack;
        more than sixteen come from the arena.
*/
static awk_text address_to awk_format_list(awk_text address_to format,
                                           awk_node address_to arguments,
                                           b32 want)
{
        awk_value few[16];
        awk_value address_to room = want <= 16 ? few
                                               : (awk_value address_to)awk_take(
                                                     (positive)want * sizeof(awk_value));
        b32 have = 0;

        for (awk_node address_to one = arguments; one; one = one->next)
        {
                awk_value_start(room[have]);
                awk_eval(one, address_of room[have]);
                have++;
        }

        awk_text address_to made = awk_sprintf(format->text, format->length, room, have);

        for (b32 i = 0; i < have; i++)
                awk_value_done(address_of room[i]);

        if (room != few)
                memory_give(room);

        return made;
}

static fn awk_do_printf(awk_node address_to node)
{
        awk_writer address_to where;

        if (!node->a)
                awk_die(null, "printf wants a format");

        awk_text address_to format = awk_eval_text(node->a);
        awk_text address_to made = awk_format_list(
            format, node->a->next, node->count > 1 ? node->count - 1 : 0);

        where = awk_output_of(node);
        if (!awk_writer_put(where, made->text, made->length))
                awk_write_refused(where, "printf");
        awk_text_drop(made);
        awk_text_drop(format);
}

static b32 awk_run(awk_node address_to node)
{
        if_rare (!awk_walk_room_left())
                awk_leave((awk_flush_everything(), text_flush(),
                           string_diagnostic(&text_diagnostic, 2, null,
                                             "nested too deeply")));

        for (; node; node = node->next)
        {
                if (awk_exiting)
                        return RUN_EXIT;

                switch (node->kind)
                {
                case S_BLOCK:
                {
                        b32 answer = awk_run(node->a);

                        if (answer != RUN_ON)
                                return answer;

                        break;
                }

                case S_EXPRESSION:
                {
                        awk_value value;

                        awk_value_start(value);
                        awk_eval(node->a, address_of value);
                        awk_value_done(address_of value);
                        break;
                }

                case S_PRINT:
                        awk_do_print(node);
                        break;

                case S_PRINTF:
                        awk_do_printf(node);
                        break;

                case S_IF:
                {
                        b32 answer = RUN_ON;

                        if (awk_eval_truth(node->a))
                                answer = node->b ? awk_run(node->b) : RUN_ON;
                        else if (node->c)
                                answer = awk_run(node->c);

                        if (answer != RUN_ON)
                                return answer;

                        break;
                }

                // All counted loops share init / test / step / body slots.
                // A do-loop enters the body once before its first test;
                // continue still reaches the step and then the condition.
                case S_LOOP:
                {
                        if (node->a)
                        {
                                b32 answer = awk_run(node->a);

                                if (answer != RUN_ON)
                                        return answer;
                        }

                        if (node->sub)
                                goto loop_body;

                        while (!awk_exiting && (!node->b || awk_eval_truth(node->b)))
                        {
                        loop_body:;
                                b32 answer = node->d ? awk_run(node->d) : RUN_ON;

                                if (answer == RUN_BREAK)
                                        break;

                                if (answer != RUN_ON && answer != RUN_CONTINUE)
                                        return answer;

                                if (node->c)
                                {
                                        answer = awk_run(node->c);

                                        if (answer != RUN_ON)
                                                return answer;
                                }
                        }

                        break;
                }

                case S_FORIN:
                {
                        awk_array address_to array = awk_cell_array(awk_cell_of(node->count));
                        awk_target target;
                        positive have = 0;
                        awk_text address_to address_to keys;
                        b32 answer = RUN_ON;

                        if (!array->count)
                                break;

                        keys = (awk_text address_to address_to)awk_take(array->count *
                                                                        sizeof(address_any));

                        for (positive i = 0; i < array->width; i++)
                                for (awk_slot address_to slot = array->buckets[i]; slot;
                                     slot = slot->next)
                                        keys[have++] = awk_text_hold(slot->key);

                        awk_node holder;

                        memory_fill(address_of holder, 0, sizeof(awk_node));
                        holder.kind = N_VARIABLE;
                        holder.index = node->index;

                        for (positive i = 0; i < have && answer == RUN_ON; i++)
                        {
                                if (awk_exiting)
                                {
                                        answer = RUN_EXIT;
                                        break;
                                }

                                awk_target_of(address_of holder, address_of target);
                                // A subscript is a string, and so is the key
                                // it comes back as: "10" sorts before "9".
                                awk_set_text(awk_target_slot(address_of target),
                                             awk_text_hold(keys[i]));
                                awk_target_written(address_of target);

                                b32 got = node->b ? awk_run(node->b) : RUN_ON;

                                if (got == RUN_BREAK)
                                        break;

                                if (got != RUN_ON && got != RUN_CONTINUE)
                                        answer = got;
                        }

                        for (positive i = 0; i < have; i++)
                                awk_text_drop(keys[i]);

                        memory_give(keys);

                        if (answer != RUN_ON)
                                return answer;

                        break;
                }

                case S_DELETE:
                {
                        awk_array address_to array = awk_cell_array(awk_cell_of(node->index));

                        if (!node->a)
                        {
                                awk_array_empty(array);
                                break;
                        }

                        awk_text address_to key = awk_subscript_key(node->a->a, node->a->count);

                        awk_array_remove(array, key->text, key->length);
                        awk_text_drop(key);
                        break;
                }

                case S_NEXT:
                        return RUN_NEXT;

                case S_NEXTFILE:
                        return RUN_NEXTFILE;

                case S_BREAK:
                        return RUN_BREAK;

                case S_CONTINUE:
                        return RUN_CONTINUE;

                case S_EXIT:
                        if (node->a)
                                awk_exit_code = awk_whole(awk_eval_number(node->a));

                        awk_exiting = true;
                        return RUN_EXIT;

                case S_RETURN:
                        awk_value_done(address_of awk_returned);

                        if (node->a)
                                awk_eval(node->a, address_of awk_returned);

                        return RUN_RETURN;
                }

                if (awk_skipping)
                {
                        b32 skip = awk_skipping;

                        awk_skipping = RUN_ON;
                        return skip;
                }
        }

        return RUN_ON;
}

/*
        The input, in the order the arguments give it.

        ARGV is walked rather than copied, because a program is allowed to
        change it while it runs: an element that is a name=value assignment is
        performed where it stands rather than opened, which is how awk lets a
        variable change between one file and the next.
*/
static awk_reader awk_main;
static bool awk_main_live;
static b32 awk_argv_at = 1;
static bool awk_input_used;

static awk_text address_to awk_unescape(string_address text, positive length)
{
        awk_builder build;

        awk_builder_start(address_of build);

        for (positive i = 0; i < length;)
        {
                p8 address_to slash = memory_first_of(text + i, '\\', length - i);
                positive plain = slash && slash + 1 < text + length
                                     ? (positive)(slash - text - i) : length - i;
                awk_builder_put(address_of build, text + i, plain);
                i += plain;

                if (i < length)
                {
                        i++;
                        awk_builder_char(address_of build,
                                         (p8)awk_escape(text, address_of i, length));
                }
        }

        return awk_builder_text(address_of build);
}

static awk_text address_to awk_number_key(positive value)
{
        p8 room[24];
        positive at = positive_into(room, value);

        return awk_text_new(room, at);
}

// Where the = of a name=value stands, or the length when it is not one.
static positive awk_assignment_split(string_address text, positive length)
{
        if (!length || !awk_name_start(text[0]))
                return length;

        positive at = string_span_max(text, length, string_set_name);

        return at < length && text[at] == '=' ? at : length;
}

static bool awk_assignment(string_address text, positive length)
{
        positive at = awk_assignment_split(text, length);

        if (at >= length)
                return false;

        awk_text address_to name = awk_text_new(text, at);
        awk_text address_to value = awk_unescape(text + at + 1, length - at - 1);
        b32 where = awk_global_find(name->text, name->length);

        awk_set_input(address_of awk_globals[where].value, value);
        awk_globals[where].kind = AWK_CELL_SCALAR;

        if (awk_global_meaning[where] == AWK_NF)
                awk_nf_written(awk_whole(awk_global_number(where)));

        awk_text_drop(name);
        return true;
}

bool file_is_directory_through(string_address path);

static bool awk_open_next_input()
{
        awk_array address_to argv = awk_cell_array(address_of awk_globals[awk_where_argv]);

        for (;;)
        {
                b32 count = awk_whole(awk_global_number(awk_where_argc));

                if (awk_argv_at >= count)
                {
                        if (awk_input_used)
                                return false;

                        awk_input_used = true;
                        awk_main.handle = 0;
                        awk_main.live = true;
                        awk_main.pipe = false;
                        awk_main.ended = false;
                        awk_main.failed = false;
                        awk_main.at = 0;
                        awk_main.filled = 0;
                        // Standard input, read because nothing named a
                        // file, is called - as the reference awk calls it.
                        awk_main.name = awk_text_new("-", 1);
                        awk_set_input_bytes(address_of awk_globals[awk_where_filename].value,
                                            "-", 1);
                        awk_set_global_number(awk_where_fnr, 0);
                        awk_main_live = true;
                        return true;
                }

                awk_text address_to key = awk_number_key((positive)awk_argv_at++);
                awk_slot address_to slot = awk_array_find(argv, key->text, key->length);

                awk_text_drop(key);

                if (!slot)
                        continue;

                awk_text address_to name = awk_text_hold(awk_to_text(address_of slot->value));

                if (!name->length)
                {
                        awk_text_drop(name);
                        continue;
                }

                if (awk_assignment(name->text, name->length))
                {
                        awk_text_drop(name);
                        continue;
                }

                awk_input_used = true;
                awk_main.pipe = false;
                awk_main.ended = false;
                awk_main.failed = false;
                awk_main.at = 0;
                awk_main.filled = 0;
                awk_main.live = true;
                awk_main.name = awk_text_hold(name);

                if (name->length == 1 && name->text[0] == '-')
                        awk_main.handle = 0;
                else
                {
                        // A directory is skipped with a warning, as the
                        // reference awk skips it, rather than read and failed.
                        if (file_is_directory_through(name->text))
                        {
                                string_diagnostic(&text_diagnostic, 0, name->text,
                                                  "is a directory: skipped");
                                awk_text_drop(awk_main.name);
                                awk_main.name = null;
                                awk_main.live = false;
                                awk_text_drop(name);
                                continue;
                        }

                        bipolar handle = text_open_handle(name->text, FILE_READ, 0);

                        if (handle < 0)
                        {
                                awk_text_drop(name);
                                awk_die(awk_main.name->text, "cannot open file for reading");
                        }

                        awk_main.handle = (b32)handle;
                }

                // Input, like a field: a number only when it looks like one,
                // where every name used to compare equal to zero.
                awk_set_input_bytes(address_of awk_globals[awk_where_filename].value,
                                    name->text, name->length);
                awk_set_global_number(awk_where_fnr, 0);
                awk_text_drop(name);
                awk_main_live = true;
                return true;
        }
}

static fn awk_close_main()
{
        if (!awk_main_live)
                return;

        if (awk_main.handle > 2)
                system_close(awk_main.handle);

        awk_text_drop(awk_main.name);
        awk_main.name = null;
        awk_main.live = false;
        awk_main_live = false;
}

static b32 awk_main_next_record(awk_text address_to address_to into)
{
        for (;;)
        {
                if (!awk_main_live && !awk_open_next_input())
                        return false;

                b32 answer = awk_read_record(address_of awk_main, into);
                if (answer > 0)
                {
                        awk_set_global_number(awk_where_nr, awk_global_number(awk_where_nr) + 1);
                        awk_set_global_number(awk_where_fnr,
                                              awk_global_number(awk_where_fnr) + 1);
                        return true;
                }
                if (answer < 0)
                        return answer;

                awk_close_main();
        }
}

/*
        The rules, in the order they were written.

        A pattern with a comma is a range, and whether it is open is kept on
        the rule itself: the line that starts one is tested against the line
        that ends it before the next line is read, so /a/,/a/ is one line and
        not every line to the end.
*/
static b32 awk_run_rules()
{
        b32 answer = RUN_ON;
        bool wanted = false;

        for (b32 i = 0; i < awk_rule_count; i++)
                if (awk_rules[i].kind == RULE_BEGIN)
                {
                        answer = awk_run(awk_rules[i].action);

                        if (answer == RUN_EXIT)
                                break;

                        // Only a function can have said it here; the parser
                        // let it through because a rule can call it too.
                        if (answer == RUN_NEXT || answer == RUN_NEXTFILE)
                                awk_die(null, "next used in a BEGIN action");
                }

        for (b32 i = 0; i < awk_rule_count; i++)
                if (awk_rules[i].kind != RULE_BEGIN)
                        wanted = true;

        while (answer != RUN_EXIT && wanted)
        {
                awk_text address_to record;

                b32 got = awk_main_next_record(address_of record);
                if (got <= 0)
                {
                        if (got < 0)
                                awk_die(awk_main.name->text, "error reading input file");
                        break;
                }

                awk_record_set(record->text, record->length);
                awk_text_drop(record);

                for (b32 i = 0; i < awk_rule_count; i++)
                {
                        awk_rule address_to rule = address_of awk_rules[i];
                        bool matched;

                        if (rule->kind != RULE_PLAIN)
                                continue;

                        if (!rule->first)
                                matched = true;
                        else if (!rule->second)
                                matched = awk_eval_truth(rule->first);
                        else if (rule->running)
                        {
                                matched = true;

                                if (awk_eval_truth(rule->second))
                                        rule->running = false;
                        }
                        else if (awk_eval_truth(rule->first))
                        {
                                matched = true;
                                rule->running = !awk_eval_truth(rule->second);
                        }
                        else
                                matched = false;

                        if (!matched)
                                continue;

                        if (!rule->action)
                        {
                                awk_text address_to line = awk_to_text(awk_field(0));
                                positive length;
                                string_address ors = awk_separator(awk_where_ors,
                                                                   address_of length);

                                if (!awk_writer_put(address_of awk_standard_out,
                                                    line->text, line->length) ||
                                    !awk_writer_put(address_of awk_standard_out, ors,
                                                    length))
                                        awk_write_refused(address_of awk_standard_out,
                                                          "print");
                                continue;
                        }

                        answer = awk_run(rule->action);

                        if (answer == RUN_NEXT || answer == RUN_EXIT)
                                break;

                        if (answer == RUN_NEXTFILE)
                        {
                                awk_close_main();
                                break;
                        }

                        answer = RUN_ON;
                }

                if (answer == RUN_EXIT)
                        break;

                answer = RUN_ON;
        }

        awk_exiting = false;
        awk_skipping = RUN_ON;

        for (b32 i = 0; i < awk_rule_count; i++)
                if (awk_rules[i].kind == RULE_END)
                {
                        b32 got = awk_run(awk_rules[i].action);

                        if (got == RUN_EXIT)
                                break;

                        if (got == RUN_NEXT || got == RUN_NEXTFILE)
                                awk_die(null, "next used in an END action");
                }

        return awk_exit_code;
}

static fn awk_stack_room_set(positive room)
{
        awk_stack_room = room;

        // A floor below zero would be a huge unsigned address and would
        // refuse every program rather than the deep ones. No machine here
        // puts a stack that low, and a zero floor refuses none, which is
        // the side to be wrong on.
        awk_stack_floor = room < awk_stack_start ? awk_stack_start - room : 0;
        awk_walk_floor = awk_stack_floor > AWK_WALK_SLACK
                             ? awk_stack_floor - AWK_WALK_SLACK
                             : 0;
}

static fn awk_stack_measure()
{
        positive limits[2] = {0, 0};
        b32 here = 0;

        /*
                The top of the stack, not the frame awk started in. The
                reserve below is measured down from this, and a frame is as
                deep as the shell already was when it reached awk: a
                thousand nested shell functions spent the whole megabyte
                before awk read a byte, and a program that should have been
                refused faulted instead. The stack _start was handed is the
                real top and does not move. Where no entry recorded one,
                the frame is all there is to measure from.
        */
        awk_stack_start = program_stack_base ? (positive)program_stack_base
                                             : (positive)address_of here;
        awk_stack_room_set(6u << 20);

        if (system_call_4(syscall(prlimit64), 0, 3, 0, (positive)limits))
                return;

        if (!limits[0])
                return;

        if (limits[0] == ~(positive)0)
        {
                awk_stack_room_set(256u << 20);
                return;
        }

        awk_stack_room_set(limits[0] > (2u << 20) ? limits[0] - (1u << 20)
                                                  : limits[0] / 2);
}

static fn awk_start()
{
        awk_write_failed = false;
        awk_stack_measure();
        awk_infinity = awk_from_bits((positive)0x7ff0000000000000ull);
        // The sign bit is set because that is the one the machine's own log
        // and square root hand back, and it is visible: it prints as -nan.
        awk_not_a_number = awk_from_bits((positive)0xfff8000000000000ull);
        awk_returned.state = AWK_UNSET;
        awk_field_nothing.state = AWK_UNSET;

        awk_standard_out.handle = 1;
        awk_standard_out.used = 0;
        awk_standard_out.live = true;
        awk_standard_out.kind = AWK_TO_FILE;
        awk_standard_named[0] = awk_standard_named[1] = false;

        string_address address_to process_environment = program_environment_list();
        positive environment_count = 0;

        while (process_environment && process_environment[environment_count])
                environment_count++;

        if (!shell_array_room(awk_child_environment,
                              awk_child_environment_room,
                              environment_count + 1))
                awk_die(null, "no room for the environment");

        if (environment_count)
                memory_copy(awk_child_environment, process_environment,
                            environment_count *
                                sizeof(awk_child_environment[0]));

        awk_child_environment[environment_count] = null;

        awk_where_fs = awk_global_find("FS", 2);
        awk_where_ofs = awk_global_find("OFS", 3);
        awk_where_ors = awk_global_find("ORS", 3);
        awk_where_rs = awk_global_find("RS", 2);
        awk_where_nr = awk_global_find("NR", 2);
        awk_where_nf = awk_global_find("NF", 2);
        awk_where_fnr = awk_global_find("FNR", 3);
        awk_where_filename = awk_global_find("FILENAME", 8);
        awk_where_subsep = awk_global_find("SUBSEP", 6);
        awk_where_rstart = awk_global_find("RSTART", 6);
        awk_where_rlength = awk_global_find("RLENGTH", 7);
        awk_where_convfmt = awk_global_find("CONVFMT", 7);
        awk_where_ofmt = awk_global_find("OFMT", 4);
        awk_where_environ = awk_global_find("ENVIRON", 7);
        awk_where_argv = awk_global_find("ARGV", 4);
        awk_where_argc = awk_global_find("ARGC", 4);

        awk_global_meaning[awk_where_nf] = AWK_NF;

        awk_set_bytes(address_of awk_globals[awk_where_fs].value, " ", 1);
        awk_set_bytes(address_of awk_globals[awk_where_ofs].value, " ", 1);
        awk_set_bytes(address_of awk_globals[awk_where_ors].value, "\n", 1);
        awk_set_bytes(address_of awk_globals[awk_where_rs].value, "\n", 1);
        awk_set_bytes(address_of awk_globals[awk_where_subsep].value, "\034", 1);
        awk_set_bytes(address_of awk_globals[awk_where_convfmt].value, "%.6g", 4);
        awk_set_bytes(address_of awk_globals[awk_where_ofmt].value, "%.6g", 4);
        awk_set_bytes(address_of awk_globals[awk_where_filename].value, "", 0);
        awk_set_global_number(awk_where_nr, 0);
        awk_set_global_number(awk_where_nf, 0);
        awk_set_global_number(awk_where_fnr, 0);
        awk_set_global_number(awk_where_rstart, 0);
        awk_set_global_number(awk_where_rlength, -1);

        awk_array address_to environ = awk_cell_array(address_of awk_globals[awk_where_environ]);

        for (b32 i = 0; process_environment && process_environment[i]; i++)
        {
                string_address entry = process_environment[i];
                positive at = (positive)(string_first_of_or_end(entry, '=') - entry);

                if (!entry[at])
                        continue;

                awk_slot address_to slot = awk_array_place(environ, entry, at);

                awk_set_input_bytes(address_of slot->value, entry + at + 1,
                                    string_length(entry + at + 1));
        }

        awk_fields_reserve(1);
        awk_set_bytes(address_of awk_fields[0], "", 0);
}

/*
        -f and -v come as often as there are program files and assignments, so
        the options are done as they arrive rather than counted and looked at
        afterwards. The program being built and the two lists it fills are
        reached from there rather than passed to it.
*/
static awk_builder address_to awk_reading;
static awk_text address_to awk_field_split;
static awk_text address_to address_to awk_pending;
static positive awk_pending_room;
static b32 awk_pending_count;
static bool awk_have_program;

static DEAD_END fn awk_usage()
{
        writer_stderr("usage: awk [-F sepstring] [-v assignment]... program"
                      " [argument...]\n", 0);
        writer_stderr("       awk [-F sepstring] -f progfile [-f progfile]..."
                      " [-v assignment]... [argument...]\n", 0);
        awk_leave(1);
}

static bool awk_option_seen(p8 letter, string_address value)
{
        if (letter == 'F')
        {
                awk_text_drop(awk_field_split);
                awk_field_split = awk_unescape(value, string_length(value));

                return true;
        }

        if (letter == 'v')
        {
                if (!shell_array_room(awk_pending, awk_pending_room,
                                      (positive)awk_pending_count + 1))
                        awk_die(null, "no room for assignments");

                awk_pending[awk_pending_count++] =
                    awk_text_new(value, string_length(value));

                return true;
        }

        bipolar handle = value[0] == '-' && !value[1]
                             ? 0
                             : text_open_handle(value, FILE_READ, 0);

        if (handle < 0)
                awk_die(value, "cannot open the program file");

        for (;;)
        {
                static p8 room[65536];
                bipolar got = system_read_retry((positive)handle, room, sizeof(room));

                if (got < 0)
                        awk_leave((awk_flush_everything(),
                                   string_diagnostic(&text_diagnostic, 1, value,
                                                     "cannot read the program file")));

                if (!got)
                        break;

                awk_builder_put(awk_reading, room, (positive)got);
        }

        if (handle > 0)
                system_close(handle);

        awk_builder_char(awk_reading, '\n');
        awk_have_program = true;

        return true;
}

static b32 text_awk()
{
        awk_builder program;
        file_taking taking = {
            .program = (string_address) "awk",
            .options = (const argument_option[]){
    {"Ffv", 0, ARGUMENT_REQUIRED},
    {null},
        },
            .seen = awk_option_seen,
        };

        text_begin("awk");
        awk_builder_start(address_of program);
        awk_start();

        awk_reading = address_of program;
        awk_field_split = null;
        awk_pending_count = 0;
        awk_have_program = false;

        if (!file_take(address_of taking) ||
            (!awk_have_program && (b32)taking.first >= text_argument_count))
                awk_usage();

        b32 at = (b32)taking.first;

        if (!awk_have_program)
        {
                string_address text = program_argument(at++);

                awk_builder_put(address_of program, text, string_length(text));
        }

        awk_array address_to argv = awk_cell_array(address_of awk_globals[awk_where_argv]);
        awk_text address_to zero = awk_number_key(0);
        awk_slot address_to slot = awk_array_place(argv, zero->text, zero->length);
        b32 count = 1;

        awk_set_input_bytes(address_of slot->value, "awk", 3);
        awk_text_drop(zero);

        for (b32 i = at; i < text_argument_count; i++)
        {
                awk_text address_to key = awk_number_key((positive)count++);
                string_address one = program_argument(i);

                slot = awk_array_place(argv, key->text, key->length);
                awk_set_input_bytes(address_of slot->value, one, string_length(one));
                awk_text_drop(key);
        }

        awk_set_global_number(awk_where_argc, (decimal)count);

        // The builder keeps a byte past the end for this: the lexer hands
        // a number's text to strtod, which reads to a terminator.
        program.data[program.used] = end;

        awk_source = program.data;
        awk_source_length = program.used;
        awk_source_at = 0;
        awk_parsing = true;
        awk_parse_program();

        // A name is a function or a variable, not both, and a use as a
        // variable anywhere in the program has made a global of it.
        for (b32 i = 0; i < awk_function_count; i++)
                if (awk_functions[i].defined)
                        for (b32 g = 0; g < awk_global_count; g++)
                                if (awk_text_is(awk_global_names[g],
                                                awk_functions[i].name->text,
                                                awk_functions[i].name->length))
                                        awk_syntax("function name used as a variable");

        awk_parsing = false;
        awk_regex_mark_pool = regex_retained;

        if (awk_field_split)
        {
                awk_set_text(address_of awk_globals[awk_where_fs].value,
                             awk_field_split);
                awk_field_split = null;
        }

        for (b32 i = 0; i < awk_pending_count; i++)
        {
                // Not var=value: without an = it is a usage error, with one
                // and a name that is not a name it is fatal, as the
                // reference awk grades them -- after the program files were
                // read and the program parsed, which is the reference's
                // order too.
                if (!awk_assignment(awk_pending[i]->text, awk_pending[i]->length))
                {
                        string_diagnostic(&text_diagnostic, 0, awk_pending[i]->text,
                                          "argument to -v is not in var=value form");

                        if (!memory_first_of(awk_pending[i]->text, '=',
                                             awk_pending[i]->length))
                                awk_usage();

                        awk_leave(2);
                }

                awk_text_drop(awk_pending[i]);
        }

        awk_leave(awk_run_rules());
}
