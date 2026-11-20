/*
        The terminal emulator: VT/CSI/OSC/DCS parser, cell ring, scrollback, line editor, and key/pointer encoding.
        Pure byte-stream-to-grid core (no syscalls except claim_standard_descriptors and grid_tell).
*/

#define SHELL "/shell"
#ifndef EINTR
#define EINTR 4
#endif
#ifndef EAGAIN
#define EAGAIN 11
#endif
#define SPARE __attribute__((unused))

static struct window *window;
static unsigned int COLUMNS, ROWS, row, column;
static unsigned char ink = 7, paper = 0;
static unsigned short style;
static b32 reverse;
static unsigned int last_character, touched_top, touched_bottom;
static unsigned int region_top, region_bottom;

static b32 autowrap = true, insert_mode, cursor_visible = true;
static b32 application_keys, application_keypad, synchronized_output;
static unsigned int mouse_mode, mouse_bits;
static b32 mouse_sgr, focus_events, bracketed_paste, origin_mode;

static b32 alternate, alternate_conceded;
static unsigned int alternate_head, alternate_rows, history_lines;

#define CHARSET_ASCII 0
#define CHARSET_ACS 1
static unsigned char charset_g0, charset_g1, charset_gl;
static p8 escape_kind, csi_intermediate;

static unsigned int shown_row, shown_column, cursor_shape;
static b32 shown, shown_block;
static unsigned short shown_marks;

struct cursor_state
{
        unsigned int row, column;
        unsigned char ink, paper;
        unsigned short style;
        b32 reverse;
        unsigned char charset_g0, charset_g1, charset_gl;
        b32 origin_mode;
};

static struct cursor_state cursor_saved = {0, 0, 7, 0, 0, false};
static struct cursor_state cursor_primary = {0, 0, 7, 0, 0, false};

#define TAB_COLUMNS 4096u
#define TAB_WORD (8 * sizeof(unsigned long))
static unsigned long tab_stop[TAB_COLUMNS / TAB_WORD] __attribute__((aligned(8)));

static fn touch(unsigned int at)
{
        if (at < touched_top)
                touched_top = at;
        if (at + 1 > touched_bottom)
                touched_bottom = at + 1;
}

static fn touch_all()
{
        touched_top = 0, touched_bottom = ROWS;
}

static inline INLINE unsigned int row_slot(unsigned int r)
{
        return (window->head - ROWS + r) % window->history;
}

static inline INLINE unsigned int slot_after(unsigned int slot, unsigned int count)
{
        unsigned int after = slot + count;

        if (after < window->history)
                return after;
        after -= window->history;
        return after < window->history ? after : after % window->history;
}

#define slot_cells(slot) (window_cells(window) + (slot) * window->stride)
#define slot_length(slot) (window_lengths(window) + (slot))
#define row_cells(row) slot_cells(row_slot(row))
#define row_length(row) slot_length(row_slot(row))

#define BLANK_CELL_WORD ((positive)' ' | ((positive)7 << 32))

static PURE unsigned char ink_drawn()
{
        return (style & WINDOW_CELL_BOLD) && ink < 8 ? (unsigned char)(ink | 8) : ink;
}

static PURE positive blank_cell_word()
{
        return BLANK_CELL_WORD | (positive)paper << 40 | (positive)(style & WINDOW_CELL_PAPER_SET) << 48;
}

static PURE b32 paper_set()
{
        return (style & WINDOW_CELL_PAPER_SET) != 0;
}

static fn cells_fill(unsigned int r, unsigned int first, unsigned int count, positive word)
{
        if (!count)
                return;
        memory_fill_u64_aligned(row_cells(r) + first, count, word);
        touch(r);
}

static fn cells_clear(unsigned int r, unsigned int first, unsigned int count)
{
        cells_fill(r, first, count, blank_cell_word());
}

static fn cells_blank(unsigned int r, unsigned int first, unsigned int count)
{
        cells_fill(r, first, count, BLANK_CELL_WORD);
}

static inline INLINE fn ring_scroll()
{
        window_scroll(window);
        if (!alternate && history_lines + ROWS < window->history)
                history_lines++;
        touch_all();
}

static inline INLINE fn slot_copy(unsigned int to, unsigned int from)
{
        unsigned int length = address_to slot_length(from);

        memory_copy(slot_cells(to), slot_cells(from), (positive)length * sizeof(struct window_cell));
        address_to slot_length(to) = length;
}

static fn row_blank(unsigned int r)
{
        if (paper_set())
        {
                cells_clear(r, 0, COLUMNS);
                address_to row_length(r) = COLUMNS;
                return;
        }
        address_to row_length(r) = 0;
        touch(r);
}

static fn region_scroll(unsigned int count, b32 up)
{
        unsigned int height = region_bottom - region_top, top = row_slot(0);

        if (count > height)
                count = height;
        for (unsigned int n = 0; n + count < height; n++)
        {
                unsigned int to = up ? region_top + n : region_bottom - 1 - n;
                slot_copy(slot_after(top, to), slot_after(top, up ? to + count : to - count));
        }
        for (unsigned int n = 0; n < count; n++)
                row_blank(up ? region_bottom - 1 - n : region_top + n);
        touch_all();
}

// DECSTBM anchored at row 0 keeps scrolled lines in primary scrollback by scrolling the ring and restoring rows below region_bottom.
static fn __attribute__((__noinline__)) region_scroll_kept(unsigned int count)
{
        unsigned int top;

        if (count > region_bottom)
                count = region_bottom;
        for (unsigned int n = count; n; n--)
                ring_scroll();
        top = row_slot(0);
        for (unsigned int r = ROWS; r-- > region_bottom;)
                slot_copy(slot_after(top, r), slot_after(top, r - count));
        for (unsigned int r = region_bottom - count; r < region_bottom; r++)
                row_blank(r);
        touch_all();
}

static unsigned int alternate_taken(void)
{
        return max(alternate_rows, ROWS) + (window->head - alternate_head);
}

static unsigned int alternate_room()
{
        unsigned int used = history_lines + alternate_taken();

        return used < window->history ? window->history - used : 0;
}

static fn alternate_claim()
{
        unsigned int taken = alternate_taken();
        unsigned int kept = taken < window->history ? window->history - taken : 0;

        if (history_lines > kept)
                history_lines = kept;
}

static fn alternate_compact()
{
        unsigned int to = alternate_head % window->history, from = row_slot(0);

        if (window->head - alternate_head < 2 * ROWS)
                return;
        for (unsigned int r = 0; r < ROWS; r++)
                slot_copy(slot_after(to, r), slot_after(from, r));
        __atomic_store_n(address_of window->head, alternate_head + ROWS, __ATOMIC_RELEASE);
        touch_all();
}

static fn __attribute__((__noinline__)) alternate_scroll(unsigned int count)
{
        if (count > ROWS)
                count = ROWS;
        if (count > alternate_room())
        {
                alternate_compact();
                if (count > alternate_room() && !alternate_conceded)
                        history_lines -= min(history_lines, ROWS), alternate_conceded = true;
                if (count > alternate_room())
                        return region_scroll(count, true);
        }
        for (unsigned int n = count; n; n--)
                window_scroll(window);
        touch_all();
        if (paper_set())
                for (unsigned int r = ROWS - count; r < ROWS; r++)
                        row_blank(r);
}

static fn scroll_up(unsigned int count)
{
        if (region_top || region_bottom != ROWS)
        {
                if (region_top == 0 && !alternate)
                        region_scroll_kept(count);
                else
                        region_scroll(count, true);
                return;
        }
        if (count > ROWS)
                count = ROWS;
        if (!count)
                return;
        if (alternate)
                return alternate_scroll(count);
        for (unsigned int n = count; n; n--)
                ring_scroll();
        if (paper_set())
                for (unsigned int r = ROWS - count; r < ROWS; r++)
                        row_blank(r);
}

static fn line_feed()
{
        if (row + 1 == region_bottom)
                scroll_up(1);
        else if (row + 1 < ROWS)
                row++;
}

static fn reach(unsigned int r, unsigned int to)
{
        unsigned int address_to length = row_length(r);

        if (address_to length < to)
                cells_blank(r, address_to length, to - address_to length), address_to length = to;
}

static fn open_gap(unsigned int at, unsigned int count)
{
        unsigned int address_to length = row_length(row), last;
        struct window_cell address_to cells;

        if (at >= COLUMNS || !count)
                return;
        if (count > COLUMNS - at)
                count = COLUMNS - at;
        reach(row, at);
        last = min(address_to length + count, COLUMNS);
        reach(row, last);
        cells = row_cells(row);
        memory_copy(cells + at + count, cells + at, (positive)(last - at - count) * sizeof(struct window_cell));
        cells_clear(row, at, min(count, last - at));
        touch(row);
}

static fn unpair(struct window_cell address_to cells, unsigned int length, unsigned int at)
{
        if (at >= length)
                return;
        if ((cells[at].flags & WINDOW_CELL_WIDE) && at + 1 < length)
                cells[at + 1].character = ' ', cells[at + 1].flags &= (unsigned short)~WINDOW_CELL_WIDE_RIGHT;
        if ((cells[at].flags & WINDOW_CELL_WIDE_RIGHT) && at)
                cells[at - 1].character = ' ', cells[at - 1].flags &= (unsigned short)~WINDOW_CELL_WIDE;
}

static fn erase(unsigned int from_row, unsigned int from_column, unsigned int to_row, unsigned int to_column);

static inline INLINE fn wrap_for(unsigned int width)
{
        if (column + width <= COLUMNS)
                return;
        if (!autowrap)
                column = COLUMNS - width;
        else
        {
                if (column < COLUMNS)
                        erase(row, column, row, COLUMNS - 1);
                column = 0;
                line_feed();
        }
}

static inline INLINE fn column_held(void)
{
        if (!autowrap && column >= COLUMNS)
                column = COLUMNS - 1;
}

static PURE unsigned short flags_swap(unsigned short f)
{
        return (unsigned short)((f & ~(WINDOW_CELL_INK_SET | WINDOW_CELL_PAPER_SET | WINDOW_CELL_SWAP)) |
                                (f & WINDOW_CELL_INK_SET ? WINDOW_CELL_PAPER_SET : 0) |
                                (f & WINDOW_CELL_PAPER_SET ? WINDOW_CELL_INK_SET : 0) |
                                (f & WINDOW_CELL_SWAP ? 0 : WINDOW_CELL_SWAP));
}

static PURE unsigned short cell_flags()
{
        return reverse ? flags_swap(style) : style;
}

static PURE positive cell_attribute()
{
        return ((positive)(reverse ? paper : ink_drawn()) << 32) |
               ((positive)(reverse ? ink_drawn() : paper) << 40) |
               ((positive)cell_flags() << 48);
}

static inline INLINE struct window_cell address_to term_row_prepare(unsigned int r, unsigned int col, unsigned int address_to address_to length_out)
{
        unsigned int slot = row_slot(r);
        unsigned int address_to length = slot_length(slot);

        if (address_to length < col)
                cells_blank(r, address_to length, col - address_to length), address_to length = col;
        address_to length_out = length;
        return slot_cells(slot);
}

static inline INLINE unsigned int term_char_width(unsigned int character)
{
        return character < 0x300 ? 1 : (unsigned int)unicode_width(character, UNICODE_WIDTH_TERMINAL);
}

static fn put_cells(unsigned int character, unsigned int width)
{
        struct window_cell address_to cells, address_to cell;
        unsigned int address_to length;

        if (width > COLUMNS)
                width = 1;
        wrap_for(width);
        cells = term_row_prepare(row, column, address_of length);
        if (insert_mode)
                open_gap(column, width);
        unpair(cells, address_to length, column);
        if (width == 2)
                unpair(cells, address_to length, column + 1);
        cell = cells + column;
        cell->character = character;
        cell->ink = reverse ? paper : ink_drawn();
        cell->paper = reverse ? ink_drawn() : paper;
        cell->flags = cell_flags();
        if (width == 2)
                cell->flags |= WINDOW_CELL_WIDE, cell[1] = cell[0],
                cell[1].flags = (unsigned short)(cell_flags() | WINDOW_CELL_WIDE_RIGHT);
        last_character = character;
        touch(row);
        column += width;
        if (address_to length < column)
                address_to length = column;
        column_held();
}

static positive term_repeated;

static fn put(unsigned int character)
{
        unsigned int width = term_char_width(character);

        if (width)
                put_cells(character, width);
}

/* ---- output queue back to the pty ---- */

#ifdef TERM_FIXED_QUEUE
#define TO_SHELL_MAX 2048
#define TO_SHELL_FIXED
static p8 to_shell[TO_SHELL_MAX];
#else
static p8 address_to to_shell;
static positive to_shell_room;
#endif
static positive to_shell_length;

#define TERM_MESSAGE_MAX 64
#define TERM_ANSWERS_HELD 4096

static b32 to_shell_fits(positive length)
{
#ifdef TO_SHELL_FIXED
        return length <= TO_SHELL_MAX - to_shell_length;
#else
        (void)length;
        return true;
#endif
}

static fn emit_bytes(address_any data, positive length)
{
        if (!length)
                return;
#ifdef TO_SHELL_FIXED
        if (!to_shell_fits(length))
                return;
#else
        if (!array_store_reserve(to_shell, to_shell_room, to_shell_length, to_shell_length + length, 64))
                return;
#endif
        memory_copy_apart(to_shell + to_shell_length, data, length);
        to_shell_length += length;
}

static b32 answering(void)
{
#ifdef TO_SHELL_FIXED
        return to_shell_fits(TERM_MESSAGE_MAX);
#else
        return to_shell_length < TERM_ANSWERS_HELD;
#endif
}

static fn emit(unsigned int byte)
{
        p8 one = (p8)byte;

        emit_bytes(address_of one, 1);
}

static fn SPARE emit_character(unsigned int character)
{
        p8 bytes[4];

        emit_bytes(bytes, memory_utf8_encode(bytes, sizeof(bytes), character));
}

#define emit_literal(text) emit_bytes((address_any)(text), sizeof(text) - 1)

/* ---- CSI parameter machine (shared with sh/edit.c) ---- */

#define TERMINAL_PARAMS_INLINE 16

typedef struct
{
        unsigned int *value;
        p8 *joined;
        positive room, flag_room;
        unsigned int count;
        p8 marker, full;
        unsigned int inline_value[TERMINAL_PARAMS_INLINE];
        p8 inline_joined[TERMINAL_PARAMS_INLINE];
} terminal_parameters;

static void terminal_parameters_reset(terminal_parameters address_to sequence)
{
        if (!sequence->value)
                sequence->value = sequence->inline_value, sequence->joined = sequence->inline_joined;
        sequence->count = 0, sequence->marker = 0, sequence->full = 0;
        sequence->value[0] = 0, sequence->joined[0] = 0;
}

static b32 terminal_parameters_room(terminal_parameters address_to sequence, positive slots)
{
        if (!sequence->value)
                sequence->value = sequence->inline_value, sequence->joined = sequence->inline_joined;
        if (slots <= TERMINAL_PARAMS_INLINE)
                return true;
        if (slots > sequence->room)
        {
                unsigned int address_to values = sequence->room ? sequence->value : null;
                positive room = sequence->room;

                if (!array_store_reserve(values, room, sequence->room, slots, 32))
                        return sequence->full = 1, false;
                if (!sequence->room)
                        memory_copy(values, sequence->inline_value, sizeof(sequence->inline_value));
                sequence->value = values, sequence->room = room;
        }
        if (slots > sequence->flag_room)
        {
                p8 address_to flags = sequence->flag_room ? sequence->joined : null;
                positive flag_room = sequence->flag_room;

                if (!array_store_reserve(flags, flag_room, sequence->flag_room, slots, 32))
                        return sequence->full = 1, false;
                if (!sequence->flag_room)
                        memory_copy(flags, sequence->inline_joined, sizeof(sequence->inline_joined));
                sequence->joined = flags, sequence->flag_room = flag_room;
        }
        return true;
}

static b32 terminal_parameters_first(terminal_parameters address_to sequence)
{
        if (sequence->count)
                return true;
        if (!terminal_parameters_room(sequence, 1))
                return false;
        sequence->value[0] = 0, sequence->joined[0] = 0, sequence->count = 1;
        return true;
}

static inline INLINE positive terminal_parameters_scan(terminal_parameters address_to sequence, const p8 address_to bytes,
                                                       positive count, p8 address_to intermediate)
{
        for (positive at = 0; at < count; at++)
        {
                unsigned int byte = bytes[at];

                if (byte - '0' < 10)
                {
                        unsigned int address_to value;

                        if (sequence->full || !terminal_parameters_first(sequence))
                                continue;
                        value = address_of sequence->value[sequence->count - 1];
                        for (;;)
                        {
                                unsigned int digit = bytes[at] - '0';

                                address_to value = address_to value > (~0u - digit) / 10 ? ~0u : address_to value * 10 + digit;
                                if (at + 1 == count || (unsigned int)bytes[at + 1] - '0' >= 10)
                                        break;
                                at++;
                        }
                }
                else if (byte == ';' || byte == ':')
                {
                        if (sequence->full || !terminal_parameters_first(sequence) ||
                            !terminal_parameters_room(sequence, sequence->count + 1))
                                continue;
                        sequence->joined[sequence->count] = byte == ':';
                        sequence->value[sequence->count++] = 0;
                }
                else if (byte >= '@' && byte <= '~')
                        return at + 1;
                else if (intermediate && byte < ' ')
                        return 0;
                else if (intermediate && byte >= 0x20 && byte <= 0x2f)
                        address_to intermediate = (p8)byte;
                else if (byte == '?' || byte == '<' || byte == '=' || byte == '>')
                        sequence->marker = (p8)byte;
        }
        return 0;
}

static inline INLINE p8 terminal_parameters_take(terminal_parameters address_to sequence, unsigned int byte)
{
        p8 one = (p8)byte;

        return terminal_parameters_scan(sequence, address_of one, 1, null) != 0;
}

static terminal_parameters terminal_csi = {terminal_csi.inline_value, terminal_csi.inline_joined, 0, 0, 0, 0, 0, {0}, {0}};
static b32 in_escape, in_csi, in_string, escape_intermediate, string_escape;
static p8 string_kind, osc_bytes[WINDOW_TITLE_MAX];
static unsigned int osc_length;

static CONST unsigned int acs_character(unsigned int c)
{
        static const unsigned int map[] = {
            0x25c6, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1, 0x2424, 0x240b, 0x2518,
            0x2510, 0x250c, 0x2514, 0x253c, 0x23ba, 0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524,
            0x2534, 0x252c, 0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7,
        };
        static const unsigned int arrows[] = {0x2192, 0x2190, 0x2191, 0x2193, '/', 0x2588};

        if (c >= '+' && c <= '0')
                return arrows[c - '+'];
        return c >= 0x60 && c <= 0x7e ? map[c - 0x60] : c;
}

/* ---- OSC, DCS, SGR, and CSI dispatch ---- */

static unsigned int (*term_palette)(unsigned int index);
#define TERM_INDEX_INK 256
#define TERM_INDEX_PAPER 257

static fn osc_colour(unsigned int colour, b32 bell)
{
        p8 text[] = "rgb:0000/0000/0000";

        for (unsigned int channel = 0; channel < 3; channel++)
        {
                p8 value = (p8)((colour >> (16 - channel * 8)) & 255), address_to at = text + 4 + channel * 5;

                memory_into_hex(at, address_of value, 1);
                at[2] = at[0], at[3] = at[1];
        }
        emit_bytes(text, sizeof(text) - 1);
        if (bell)
                emit(7);
        else
                emit_literal("\x1b\\");
}

static unsigned int osc_number(unsigned int address_to at)
{
        unsigned int value = 0;

        while (address_to at < osc_length && byte_is_digit((b32)osc_bytes[address_to at]))
        {
                unsigned int digit = osc_bytes[address_to at] - '0';

                value = value > (~0u - digit) / 10 ? ~0u : value * 10 + digit;
                address_to at += 1;
        }
        return value;
}

static fn emit_csi_nums(const char *prefix, p8 marker, const unsigned int *nums, positive count, const char *suffix)
{
        if (prefix)
                emit_bytes((address_any)prefix, string_length((string_address)prefix));
        if (marker)
                emit(marker);
        for (positive i = 0; i < count; i++)
        {
                if (i)
                        emit(';');
                positive_to_string(emit_bytes, nums[i]);
        }
        if (suffix)
                emit_bytes((address_any)suffix, string_length((string_address)suffix));
}

static const struct
{
        unsigned char p;
        unsigned short set, clear;
} sgr_styles[] = {
    {1, WINDOW_CELL_BOLD, 0},       {2, WINDOW_CELL_DIM, 0},        {3, WINDOW_CELL_ITALIC, 0},
    {4, WINDOW_CELL_UNDERLINE, 0},  {5, WINDOW_CELL_BLINK, 0},      {6, WINDOW_CELL_BLINK, 0},
    {8, WINDOW_CELL_HIDDEN, 0},     {9, WINDOW_CELL_STRIKE, 0},     {21, WINDOW_CELL_UNDERLINE, 0},
    {22, 0, WINDOW_CELL_BOLD | WINDOW_CELL_DIM},
    {23, 0, WINDOW_CELL_ITALIC},    {24, 0, WINDOW_CELL_UNDERLINE}, {25, 0, WINDOW_CELL_BLINK},
    {28, 0, WINDOW_CELL_HIDDEN},    {29, 0, WINDOW_CELL_STRIKE},
};

static fn sgr_colour_report(unsigned int colour, unsigned int base, unsigned int extended)
{
        if (colour < 16)
        {
                unsigned int code = colour < 8 ? base + colour : base + 60 + colour - 8;
                emit_csi_nums(";", 0, address_of code, 1, null);
        }
        else
                emit_csi_nums(";", 0, (unsigned int[]){extended, 5, colour}, 3, null);
}

static fn sgr_report(void)
{
        unsigned short reported = 0;

        emit('0');
        for (positive i = 0; i < array_count(sgr_styles); i++)
                if (sgr_styles[i].set && (style & sgr_styles[i].set) && !(reported & sgr_styles[i].set))
                {
                        unsigned int code = sgr_styles[i].p;

                        reported |= sgr_styles[i].set;
                        emit_csi_nums(";", 0, address_of code, 1, null);
                }
        if (reverse)
                emit_literal(";7");
        if (style & WINDOW_CELL_INK_SET)
                sgr_colour_report(ink, 30, 38);
        if (style & WINDOW_CELL_PAPER_SET)
                sgr_colour_report(paper, 40, 48);
        emit('m');
}

static fn dcs_answer(void)
{
        if (osc_length >= 2 && osc_bytes[0] == '$' && osc_bytes[1] == 'q')
        {
                if (!answering())
                        return;
                if (osc_length == 3 && osc_bytes[2] == 'm')
                        emit_literal("\x1bP1$r"), sgr_report(), emit_literal("\x1b\\");
                else if (osc_length == 3 && osc_bytes[2] == 'r')
                        emit_csi_nums("\x1bP1$r", 0, (unsigned int[]){region_top + 1, region_bottom}, 2, "r\x1b\\");
                else
                        emit_literal("\x1bP0$r\x1b\\");
                return;
        }
        if (osc_length >= 2 && osc_bytes[0] == '+' && osc_bytes[1] == 'q')
                for (unsigned int at = 2; at <= osc_length;)
                {
                        unsigned int stop = at;

                        while (stop < osc_length && osc_bytes[stop] != ';')
                                stop++;
                        if (!answering())
                                return;
                        emit_literal("\x1bP0+r");
                        emit_bytes(osc_bytes + at, stop - at);
                        emit_literal("\x1b\\");
                        at = stop + 1;
                }
}

static unsigned int title_said;

static fn osc_finish(b32 bell)
{
        unsigned int i = 0, command, n;

        if (string_kind == 'P')
                return dcs_answer();
        if (string_kind != ']')
                return;
        command = osc_number(address_of i);
        if (i < osc_length && osc_bytes[i] == ';')
                i++;
        if ((command == 0 || command == 2) && window)
        {
                n = min(osc_length - i, WINDOW_TITLE_MAX - 1);
                memory_copy(window->title, osc_bytes + i, n);
                window->title[n] = 0, title_said++;
        }
        while (command == 4 || (command >= 10 && command <= 12))
        {
                unsigned int index = command == 11 ? TERM_INDEX_PAPER : TERM_INDEX_INK;

                if (command == 4)
                {
                        unsigned int from = i;
                        index = osc_number(address_of i);
                        if (i == from || i >= osc_length || osc_bytes[i++] != ';')
                                break;
                }
                if (i < osc_length && osc_bytes[i] == '?' && (command != 4 || index < 256) && answering())
                {
                        emit_csi_nums("\x1b]", 0, (unsigned int[]){command, index}, command == 4 ? 2 : 1, ";");
                        osc_colour(term_palette ? term_palette(index)
                                                : window_cell_colour(index == TERM_INDEX_PAPER ? 0 : index == TERM_INDEX_INK ? 7 : index),
                                   bell);
                }
                while (i < osc_length && osc_bytes[i++] != ';')
                        ;
                if (i >= osc_length)
                        break;
                if (command != 4)
                        command++;
        }
}

static fn erase(unsigned int from_row, unsigned int from_column, unsigned int to_row, unsigned int to_column)
{
        b32 coloured = paper_set();

        for (unsigned int r = from_row; r <= to_row && r < ROWS; r++)
        {
                unsigned int first = r == from_row ? from_column : 0;
                unsigned int past = min((r == to_row ? to_column : COLUMNS - 1) + 1, COLUMNS);
                unsigned int slot = row_slot(r);
                unsigned int address_to length = slot_length(slot);

                if (first < past)
                        unpair(slot_cells(slot), address_to length, first),
                        unpair(slot_cells(slot), address_to length, past - 1);
                if (!coloured)
                {
                        if (first >= address_to length)
                                continue;
                        if (past >= address_to length || past == COLUMNS)
                                address_to length = first, touch(r);
                        else
                                cells_clear(r, first, past - first);
                        continue;
                }
                if (first >= past)
                {
                        if (address_to length > COLUMNS)
                                address_to length = COLUMNS, touch(r);
                        continue;
                }
                reach(r, first);
                cells_clear(r, first, past - first);
                if (past == COLUMNS || address_to length < past)
                        address_to length = past;
        }
}

static fn tab_set(unsigned int c, b32 on)
{
        if (c >= TAB_COLUMNS)
                return;
        if (on)
                tab_stop[c / TAB_WORD] |= 1ul << (c % TAB_WORD);
        else
                tab_stop[c / TAB_WORD] &= ~(1ul << (c % TAB_WORD));
}

static b32 tab_is(unsigned int c)
{
        return c < TAB_COLUMNS && ((tab_stop[c / TAB_WORD] >> (c % TAB_WORD)) & 1);
}

static fn tabs_reset()
{
        memory_fill_u64_aligned(tab_stop, sizeof(tab_stop) / 8, 0x0101010101010101ul);
}

static fn tab_forward()
{
        unsigned int c = column;

        while (++c < COLUMNS && !tab_is(c))
                ;
        column = c < COLUMNS ? c : COLUMNS - 1;
}

static fn tab_backward()
{
        unsigned int c = column;

        while (c && !tab_is(--c))
                ;
        column = c;
}

static CONST unsigned char colour_cube_level(unsigned int v)
{
        if (v < 48)
                return 0;
        if (v < 115)
                return 1;
        v = (v - 35) / 40;
        return (unsigned char)(v > 5 ? 5 : v);
}

static CONST unsigned int colour_diff(unsigned int a, unsigned int b)
{
        return a > b ? a - b : b - a;
}

static CONST unsigned char colour_rgb(unsigned int r, unsigned int g, unsigned int b)
{
        unsigned int qr, qg, qb, grey, cr, cg, cb, gr, dcube, dgrey;

        r = min(r, 255u), g = min(g, 255u), b = min(b, 255u);
        qr = colour_cube_level(r), qg = colour_cube_level(g), qb = colour_cube_level(b);
        cr = qr ? qr * 40 + 55 : 0, cg = qg ? qg * 40 + 55 : 0, cb = qb ? qb * 40 + 55 : 0;
        dcube = colour_diff(r, cr) + colour_diff(g, cg) + colour_diff(b, cb);
        grey = r < 8 ? 0 : r > 238 ? 23 : (r - 8) / 10;
        gr = 8 + grey * 10;
        dgrey = colour_diff(r, gr) + colour_diff(g, gr) + colour_diff(b, gr);
        return (unsigned char)(dgrey < dcube ? 232 + grey : 16 + 36 * qr + 6 * qg + qb);
}

static unsigned int sgr_joined(unsigned int at)
{
        unsigned int n = 0;

        while (at + n + 1 < terminal_csi.count && terminal_csi.joined[at + n + 1])
                n++;
        return n;
}

static unsigned int sgr_extended(unsigned int at, unsigned int joined, unsigned char address_to which)
{
        unsigned int kind = at + 1 < terminal_csi.count ? terminal_csi.value[at + 1] : 0;
        unsigned int took = joined ? joined : kind == 5 ? 2 : kind == 2 ? 4 : terminal_csi.count - at - 1;

        if (at + took >= terminal_csi.count)
                return terminal_csi.count - at - 1;
        if (kind == 5 && took >= 2 && terminal_csi.value[at + 2] < 256)
                address_to which = (unsigned char)terminal_csi.value[at + 2];
        else if (kind == 2 && took >= 4)
                address_to which = colour_rgb(terminal_csi.value[at + took - 2], terminal_csi.value[at + took - 1],
                                              terminal_csi.value[at + took]);
        return took;
}

static unsigned int sgr_colour_set(unsigned int p, unsigned int i, unsigned int took, unsigned int base,
                                   unsigned char reset_colour, unsigned short flag, unsigned char address_to target)
{
        if (p == base + 9)
                return address_to target = reset_colour, style = (unsigned short)(style & ~flag), took;
        if (p <= base + 7)
                address_to target = (unsigned char)(p - base);
        else if (p == base + 8)
                took = sgr_extended(i, took, target);
        else
                address_to target = (unsigned char)(8 + (p - (base + 60)));
        style = (unsigned short)(style | flag);
        return took;
}

static fn sgr()
{
        unsigned int count = terminal_csi.count ? terminal_csi.count : 1;

        for (unsigned int i = 0; i < count; i++)
        {
                unsigned int p = terminal_csi.value[i], took = sgr_joined(i);
                unsigned char unused;

                if (p == 0)
                        ink = 7, paper = 0, reverse = false, style = 0;
                else if (p == 4 && took && !terminal_csi.value[i + 1])
                        style &= (unsigned short)~WINDOW_CELL_UNDERLINE;
                else if (p == 7 || p == 27)
                        reverse = p == 7;
                else if (p < 30)
                {
                        for (positive k = 0; k < array_count(sgr_styles); k++)
                                if (sgr_styles[k].p == p)
                                        style = (unsigned short)((style | sgr_styles[k].set) & ~sgr_styles[k].clear);
                }
                else if ((p >= 30 && p <= 39) || (p >= 90 && p <= 97))
                        took = sgr_colour_set(p, i, took, 30, 7, WINDOW_CELL_INK_SET, address_of ink);
                else if ((p >= 40 && p <= 49) || (p >= 100 && p <= 107))
                        took = sgr_colour_set(p, i, took, 40, 0, WINDOW_CELL_PAPER_SET, address_of paper);
                else if (p == 58)
                        took = sgr_extended(i, took, address_of unused);
                i += took;
        }
}

static fn cursor_save(struct cursor_state address_to into)
{
        address_to into = (struct cursor_state){row, column, ink, paper, style, reverse, charset_g0, charset_g1, charset_gl, origin_mode};
}

static fn cursor_restore(const struct cursor_state address_to from)
{
        row = from->row < ROWS ? from->row : ROWS - 1;
        column = from->column <= COLUMNS ? from->column : COLUMNS - 1;
        ink = from->ink, paper = from->paper, style = from->style, reverse = from->reverse;
        charset_g0 = from->charset_g0, charset_g1 = from->charset_g1, charset_gl = from->charset_gl, origin_mode = from->origin_mode;
}

static unsigned int regrid_row(unsigned int at, bipolar shift)
{
        bipolar to = (bipolar)at + shift;

        return to < 0 ? 0 : to < (bipolar)ROWS ? (unsigned int)to : ROWS - 1;
}

static bipolar primary_regrid(unsigned int at, unsigned int was_rows)
{
        unsigned int head_was = window->head;

        if (ROWS < was_rows)
        {
                unsigned int blank = 0;

                while (blank < was_rows - ROWS && at + blank + 1 < was_rows &&
                       !address_to slot_length((window->head - 1 - blank) % window->history))
                        blank++;
                __atomic_store_n(address_of window->head, window->head - blank, __ATOMIC_RELEASE);
                history_lines = min(history_lines + (was_rows - ROWS - blank), window->history - ROWS);
        }
        else
        {
                unsigned int back = min(ROWS - was_rows, history_lines);

                for (unsigned int n = ROWS - was_rows - back; n; n--)
                        window_scroll(window);
                history_lines -= back;
        }
        return (bipolar)ROWS - (bipolar)was_rows - ((bipolar)window->head - (bipolar)head_was);
}

static fn alternate_enter(b32 save)
{
        if (alternate)
                return;
        if (save)
                cursor_save(address_of cursor_primary);
        alternate_head = window->head, alternate_rows = ROWS, alternate_conceded = false;
        for (unsigned int r = 0; r < ROWS; r++)
                window_scroll(window);
        alternate = true;
        alternate_claim();
        touch_all();
}

static fn alternate_leave(b32 restore)
{
        if (!alternate)
                return;
        __atomic_store_n(address_of window->head, alternate_head, __ATOMIC_RELEASE);
        alternate = false;
        cursor_primary.row = regrid_row(cursor_primary.row, primary_regrid(restore ? cursor_primary.row : row, alternate_rows));
        if (restore)
                cursor_restore(address_of cursor_primary);
        touch_all();
}

static unsigned int mode(unsigned int p, b32 change, b32 on)
{
        static const struct
        {
                unsigned short p;
                b32 private;
                b32 address_to flag;
        } bits[] = {
            {4, false, address_of insert_mode},      {1, true, address_of application_keys},
            {7, true, address_of autowrap},          {25, true, address_of cursor_visible},
            {1004, true, address_of focus_events},   {2004, true, address_of bracketed_paste},
            {1006, true, address_of mouse_sgr},      {2026, true, address_of synchronized_output},
        };
        b32 now;

        for (positive i = 0; i < array_count(bits); i++)
                if (bits[i].p == p && bits[i].private == (terminal_csi.marker != 0))
                {
                        if (change)
                                address_to bits[i].flag = on;
                        return address_to bits[i].flag ? 1 : 2;
                }
        if (!terminal_csi.marker)
                return 0;
        switch (p)
        {
        case 6:
                if (change)
                        origin_mode = on, row = on ? region_top : 0, column = 0;
                now = origin_mode;
                break;
        case 47:
        case 1047:
        case 1049:
                if (change)
                        on ? alternate_enter(p == 1049) : alternate_leave(p == 1049);
                now = alternate;
                break;
        case 1000:
        case 1002:
        case 1003:
        {
                unsigned int bit = p == 1000 ? 1u : p == 1002 ? 2u : 4u;

                if (change)
                {
                        mouse_bits = on ? mouse_bits | bit : mouse_bits & ~bit;
                        mouse_mode = mouse_bits & 4u ? 1003u : mouse_bits & 2u ? 1002u : mouse_bits & 1u ? 1000u : 0u;
                        if (window)
                                window->want = mouse_mode ? WINDOW_WANT_POINTER : 0;
                }
                now = (mouse_bits & bit) != 0;
                break;
        }
        default:
                return 0;
        }
        return now ? 1 : 2;
}

static fn mode_set(b32 on)
{
        for (unsigned int i = 0; i < terminal_csi.count; i++)
                mode(terminal_csi.value[i], true, on);
}

static fn attributes_reset()
{
        ink = 7, paper = 0, reverse = false, style = 0;
        autowrap = true, insert_mode = false, cursor_visible = true;
        application_keys = application_keypad = synchronized_output = false;
        mouse_mode = mouse_bits = 0, mouse_sgr = focus_events = bracketed_paste = origin_mode = false;
        charset_g0 = charset_g1 = CHARSET_ASCII, charset_gl = 0;
        if (window)
                window->want = 0;
}

static fn soft_reset()
{
        attributes_reset();
        last_character = 0, region_top = 0, region_bottom = ROWS;
        cursor_saved = (struct cursor_state){0, 0, 7, 0, 0, false};
}

static fn escape_discard();

static fn full_reset()
{
        alternate_leave(false);
        soft_reset();
        escape_discard();
        row = column = cursor_shape = 0;
        tabs_reset();
        erase(0, 0, ROWS - 1, COLUMNS - 1);
}

static fn history_clear()
{
        unsigned int slot = row_slot(0);

        if (alternate)
                return;
        for (unsigned int n = ROWS; n < window->history; n++)
                slot = slot ? slot - 1 : window->history - 1, address_to slot_length(slot) = 0;
        history_lines = 0;
}

static unsigned int term_cell_w = WINDOW_CELL_W, term_cell_h = WINDOW_CELL_H;

static unsigned int addressed_row(unsigned int a)
{
        unsigned int top = origin_mode ? region_top : 0, bottom = origin_mode ? region_bottom : ROWS;

        return a - 1 < bottom - top ? top + a - 1 : (bottom ? bottom - 1 : 0);
}

#define CSI_MARKER_ANY 0xff
#define CSI_MARKER_PRIV 0xfe

enum
{
        CSI_CUP, CSI_VMOVE, CSI_RIGHT, CSI_LEFT, CSI_COL, CSI_ROW,
        CSI_ERASE, CSI_IL_DL, CSI_DCH, CSI_ICH, CSI_ECH, CSI_SCROLL,
        CSI_TBC, CSI_MODE, CSI_SGR, CSI_DSR, CSI_REPLY, CSI_TAB, CSI_REP,
        CSI_CURSOR_SHAPE, CSI_SOFT_RESET, CSI_DECRQM, CSI_WINOPS, CSI_DECSTBM,
        CSI_SAVE_RESTORE,
};

static const struct
{
        p8 final, marker, intermediate, action, arg;
        const char address_to literal;
} csi_rule[] = {
    {'H', 0, 0, CSI_CUP, 0, null},          {'f', 0, 0, CSI_CUP, 0, null},
    {'A', 0, 0, CSI_VMOVE, 0, null},        {'F', 0, 0, CSI_VMOVE, 1, null},
    {'B', 0, 0, CSI_VMOVE, 2, null},        {'e', 0, 0, CSI_VMOVE, 2, null},
    {'E', 0, 0, CSI_VMOVE, 3, null},        {'C', 0, 0, CSI_RIGHT, 0, null},
    {'a', 0, 0, CSI_RIGHT, 0, null},        {'D', 0, 0, CSI_LEFT, 0, null},
    {'G', 0, 0, CSI_COL, 0, null},          {'`', 0, 0, CSI_COL, 0, null},
    {'d', 0, 0, CSI_ROW, 0, null},          {'J', CSI_MARKER_PRIV, 0, CSI_ERASE, 0, null},
    {'K', CSI_MARKER_PRIV, 0, CSI_ERASE, 1, null},
    {'L', 0, 0, CSI_IL_DL, 0, null},        {'M', 0, 0, CSI_IL_DL, 1, null},
    {'P', 0, 0, CSI_DCH, 0, null},          {'@', 0, 0, CSI_ICH, 0, null},
    {'X', 0, 0, CSI_ECH, 0, null},          {'S', 0, 0, CSI_SCROLL, 1, null},
    {'T', 0, 0, CSI_SCROLL, 0, null},       {'g', 0, 0, CSI_TBC, 0, null},
    {'h', CSI_MARKER_PRIV, 0, CSI_MODE, 1, null},
    {'l', CSI_MARKER_PRIV, 0, CSI_MODE, 0, null},
    {'m', 0, 0, CSI_SGR, 0, null},          {'n', CSI_MARKER_PRIV, 0, CSI_DSR, 0, null},
    {'c', 0, 0, CSI_REPLY, 0, "\x1b[?1;2c"},
    {'c', '>', 0, CSI_REPLY, 0, "\x1b[>0;115;0c"},
    {'c', '=', 0, CSI_REPLY, 0, "\x1bP!|00000000\x1b\\"},
    {'q', '>', 0, CSI_REPLY, 1, "\x1bP>|moonwater\x1b\\"},
    {'I', 0, 0, CSI_TAB, 1, null},          {'Z', 0, 0, CSI_TAB, 0, null},
    {'b', 0, 0, CSI_REP, 0, null},          {'q', 0, ' ', CSI_CURSOR_SHAPE, 0, null},
    {'p', 0, '!', CSI_SOFT_RESET, 0, null}, {'p', CSI_MARKER_ANY, '$', CSI_DECRQM, 0, null},
    {'t', 0, 0, CSI_WINOPS, 0, null},       {'r', 0, 0, CSI_DECSTBM, 0, null},
    {'s', 0, 0, CSI_SAVE_RESTORE, 1, null}, {'u', 0, 0, CSI_SAVE_RESTORE, 0, null},
};

static fn csi_final(unsigned int final)
{
        unsigned int a = terminal_csi.count && terminal_csi.value[0] ? terminal_csi.value[0] : 1;
        unsigned int b = terminal_csi.count > 1 && terminal_csi.value[1] ? terminal_csi.value[1] : 1;
        positive match = array_count(csi_rule);
        p8 arg;

        for (positive i = 0; i < array_count(csi_rule); i++)
        {
                p8 m = csi_rule[i].marker;

                if (csi_rule[i].final == final && csi_rule[i].intermediate == csi_intermediate &&
                    (m == CSI_MARKER_ANY || (m == CSI_MARKER_PRIV ? (!terminal_csi.marker || terminal_csi.marker == '?')
                                                                  : m == terminal_csi.marker)))
                {
                        match = i;
                        break;
                }
        }
        if (match == array_count(csi_rule))
                return;
        arg = csi_rule[match].arg;
        switch (csi_rule[match].action)
        {
        case CSI_CUP:
                row = addressed_row(a), column = b - 1 < COLUMNS ? b - 1 : COLUMNS - 1;
                break;
        case CSI_VMOVE:
                if (arg & 2)
                {
                        unsigned int limit = row < region_bottom ? region_bottom - 1 : ROWS - 1;
                        row = limit - row > a ? row + a : limit;
                }
                else
                {
                        unsigned int limit = row >= region_top ? region_top : 0;
                        row = row - limit > a ? row - a : limit;
                }
                column = (arg & 1) ? 0 : min(column, COLUMNS - 1);
                break;
        case CSI_RIGHT:
                column = a < COLUMNS - column ? column + a : COLUMNS - 1;
                break;
        case CSI_LEFT:
                column = min(column, COLUMNS - 1), column = column > a ? column - a : 0;
                break;
        case CSI_COL:
                column = a - 1 < COLUMNS ? a - 1 : COLUMNS - 1;
                break;
        case CSI_ROW:
                row = addressed_row(a);
                break;
        case CSI_ERASE:
        {
                unsigned int op = terminal_csi.value[0], top = arg ? row : 0, bot = arg ? row : ROWS - 1;

                if (op == 0)
                        erase(row, column, bot, COLUMNS - 1);
                else if (op == 1)
                        erase(top, 0, row, column);
                else if (op == 2)
                        erase(top, 0, bot, COLUMNS - 1);
                else if (op == 3 && !arg)
                        history_clear();
                break;
        }
        case CSI_IL_DL:
                if (row >= region_top && row < region_bottom)
                {
                        unsigned int was = region_top;
                        region_top = row;
                        if (!arg)
                                region_scroll(a, false);
                        else if (alternate && region_top == 0 && region_bottom == ROWS)
                                scroll_up(a);
                        else
                                region_scroll(a, true);
                        region_top = was;
                }
                break;
        case CSI_DCH:
        {
                unsigned int address_to length, last, gone;
                struct window_cell address_to cells;

                if (paper_set() && column < COLUMNS)
                        reach(row, COLUMNS);
                length = row_length(row), cells = row_cells(row), last = address_to length;
                if (column >= last)
                        break;
                gone = min(a, last - column);
                memory_copy(cells + column, cells + column + gone, (positive)(last - column - gone) * sizeof(struct window_cell));
                if (paper_set())
                        cells_clear(row, last - gone, gone);
                else
                        address_to length = last - gone;
                touch(row);
                break;
        }
        case CSI_ICH:
                open_gap(column, a);
                break;
        case CSI_ECH:
                if (column < COLUMNS)
                        erase(row, column, row, column + min(a, COLUMNS - column) - 1);
                break;
        case CSI_SCROLL:
                arg ? scroll_up(a) : region_scroll(a, false);
                break;
        case CSI_TBC:
                if (terminal_csi.count && terminal_csi.value[0] == 3)
                        memory_fill(tab_stop, 0, sizeof(tab_stop));
                else if (terminal_csi.value[0] == 0)
                        tab_set(column, false);
                break;
        case CSI_MODE:
                mode_set(arg);
                break;
        case CSI_SGR:
                sgr();
                break;
        case CSI_DSR:
                if (!answering() || !terminal_csi.count)
                        break;
                if (terminal_csi.value[0] == 6)
                        emit_csi_nums("\x1b[", terminal_csi.marker ? '?' : 0,
                                      (unsigned int[]){(origin_mode && row >= region_top ? row - region_top : row) + 1,
                                                       column < COLUMNS ? column + 1 : COLUMNS, 1},
                                      terminal_csi.marker ? 3 : 2, "R");
                else if (terminal_csi.value[0] == 5 && !terminal_csi.marker)
                        emit_literal("\x1b[0n");
                break;
        case CSI_REPLY:
                if (answering() && (!arg || terminal_csi.value[0] == 0))
                        emit_bytes((address_any)csi_rule[match].literal, string_length((string_address)csi_rule[match].literal));
                break;
        case CSI_TAB:
                for (a = min(a, COLUMNS); a; a--)
                        arg ? tab_forward() : tab_backward();
                break;
        case CSI_REP:
                if (last_character)
                {
                        unsigned int n = min(a, ROWS * COLUMNS);
                        for (unsigned int k = 0; k < n; k++)
                                put(last_character);
                        term_repeated += n;
                }
                break;
        case CSI_CURSOR_SHAPE:
                if (a <= 6)
                        cursor_shape = a;
                break;
        case CSI_SOFT_RESET:
                soft_reset();
                break;
        case CSI_DECRQM:
                if (answering())
                {
                        unsigned int p = terminal_csi.count ? terminal_csi.value[0] : 0;
                        emit_csi_nums("\x1b[", terminal_csi.marker, (unsigned int[]){p, mode(p, false, false)}, 2, "$y");
                }
                break;
        case CSI_WINOPS:
        {
                unsigned int op = terminal_csi.value[0];

                if ((op == 14 || op == 16 || op == 18 || op == 19) && answering())
                        emit_csi_nums("\x1b[", 0,
                                      (unsigned int[]){op - 10, op == 14 ? ROWS * term_cell_h : op == 16 ? term_cell_h : ROWS,
                                                       op == 14 ? COLUMNS * term_cell_w : op == 16 ? term_cell_w : COLUMNS},
                                      3, "t");
                break;
        }
        case CSI_DECSTBM:
        {
                unsigned int top = terminal_csi.count && terminal_csi.value[0] ? terminal_csi.value[0] - 1 : 0;
                unsigned int bottom = min(terminal_csi.count > 1 && terminal_csi.value[1] ? terminal_csi.value[1] : ROWS, ROWS);

                if (top + 1 < bottom)
                        region_top = top, region_bottom = bottom, row = origin_mode ? region_top : 0, column = 0;
                break;
        }
        case CSI_SAVE_RESTORE:
                arg ? cursor_save(address_of cursor_saved) : cursor_restore(address_of cursor_saved);
                break;
        }
}

/* ---- UTF-8, byte parser, and fast text runs ---- */

static memory_utf8_state terminal_utf8;

static fn utf8_byte(unsigned int c)
{
        p8 byte = (p8)c;

        for (;;)
        {
                b32 had = terminal_utf8.left != 0, fed = memory_utf8_feed(address_of terminal_utf8, byte);

                if (fed > 0)
                {
                        if (terminal_utf8.value >= 0xa0)
                                put(terminal_utf8.value);
                        return;
                }
                if (!fed)
                {
                        unsigned long low = (unsigned long)terminal_utf8.value << (6 * terminal_utf8.left);
                        unsigned long high = low + ((1ul << (6 * terminal_utf8.left)) - 1);

                        if (high >= terminal_utf8.least && low <= 0x10ffff && !(low >= 0xd800 && high <= 0xdfff))
                                return;
                        terminal_utf8.left = 0;
                }
                put(0xfffd);
                if (!had)
                        return;
        }
}

static fn line_forget();

static fn utf8_flush()
{
        if (terminal_utf8.left)
                terminal_utf8.left = 0, put(0xfffd);
}

static fn escape_discard()
{
        terminal_utf8.left = 0;
        in_escape = in_csi = in_string = escape_intermediate = string_escape = false;
}

static fn screen_align()
{
        region_top = 0, region_bottom = ROWS, row = column = 0;
        for (unsigned int r = 0; r < ROWS; r++)
                memory_fill_u64_aligned(row_cells(r), COLUMNS, (positive)'E' | (positive)7 << 32),
                address_to row_length(r) = COLUMNS, touch(r);
}

static fn consume(unsigned int c)
{
        if (c == 24 || c == 26)
        {
                in_escape = in_csi = in_string = false, terminal_utf8.left = 0;
                return;
        }
        line_forget();
        if (in_string)
        {
                if (!string_escape)
                {
                        if (c == 27)
                                string_escape = true;
                        else if (c == 7 && string_kind == ']')
                                osc_finish(true), in_string = in_escape = false;
                        else if (c >= ' ' && c != 127 && osc_length < WINDOW_TITLE_MAX)
                                osc_bytes[osc_length++] = (p8)c;
                        return;
                }
                string_escape = in_string = false;
                osc_finish(false);
                if (c == '\\')
                {
                        in_escape = false;
                        return;
                }
                in_escape = true, escape_intermediate = false;
        }
        if (c == 27)
        {
                utf8_flush();
                in_escape = true, in_csi = escape_intermediate = false;
                return;
        }
        if (c < ' ' || c == 127)
        {
                utf8_flush();
                switch (c)
                {
                case '\n': case 11: case 12: line_feed(); touch(row); break;
                case '\r': column = 0; break;
                case '\b': column = min(column, COLUMNS - 1); if (column) column--; break;
                case '\t': tab_forward(); break;
                case 14:   charset_gl = 1; break;
                case 15:   charset_gl = 0; break;
                }
                return;
        }
        if (in_csi)
        {
                if (c >= 0x20 && c <= 0x2f)
                        return (void)(csi_intermediate = (p8)c);
                if (!terminal_parameters_take(address_of terminal_csi, c))
                        return;
                csi_final(c);
                in_csi = in_escape = false, csi_intermediate = 0;
                return;
        }
        if (in_escape)
        {
                if (c == '[')
                        return in_csi = true, csi_intermediate = 0, terminal_parameters_reset(address_of terminal_csi);
                if (c == ']' || c == 'P' || c == 'X' || c == '^' || c == '_')
                        return (void)(in_string = true, string_escape = false, string_kind = (p8)c, osc_length = 0);
                if (c >= ' ' && c <= '/')
                        return (void)(escape_intermediate = true, escape_kind = (p8)c);
                in_escape = false;
                if (escape_intermediate)
                {
                        if (escape_kind == '(')
                                charset_g0 = c == '0' ? CHARSET_ACS : CHARSET_ASCII;
                        else if (escape_kind == ')')
                                charset_g1 = c == '0' ? CHARSET_ACS : CHARSET_ASCII;
                        else if (escape_kind == '#' && c == '8')
                                screen_align();
                        escape_intermediate = false;
                        return;
                }
                switch (c)
                {
                case '7': cursor_save(address_of cursor_saved); break;
                case '8': cursor_restore(address_of cursor_saved); break;
                case 'D': line_feed(); break;
                case 'E': line_feed(); column = 0; break;
                case 'M': if (row == region_top) region_scroll(1, false); else if (row) row--; break;
                case 'H': tab_set(column, true); break;
                case 'c': full_reset(); break;
                case '=': application_keypad = true; break;
                case '>': application_keypad = false; break;
                }
                return;
        }
        if (c < 128)
        {
                utf8_flush();
                put((charset_gl ? charset_g1 : charset_g0) == CHARSET_ACS ? acs_character(c) : c);
                return;
        }
        utf8_byte(c);
}

static inline INLINE b32 text_ground()
{
        return !in_escape && !in_csi && !in_string && !terminal_utf8.left && !insert_mode &&
               (charset_gl ? charset_g1 : charset_g0) == CHARSET_ASCII;
}

static inline INLINE fn text_cell(struct window_cell address_to cells, unsigned int had, unsigned int at, positive word)
{
        if (at < had && (cells[at].flags & (WINDOW_CELL_WIDE | WINDOW_CELL_WIDE_RIGHT)))
                unpair(cells, had, at);
        memory_copy(cells + at, address_of word, sizeof(word));
}

static positive text_ascii(const p8 address_to bytes, positive count)
{
        struct window_cell address_to cells;
        unsigned int address_to length, first, had;
        positive room, limit, guarded, attribute, n = 0;

        wrap_for(1);
        first = column, room = COLUMNS - first;
        cells = term_row_prepare(row, first, address_of length);
        had = address_to length, limit = count < room ? count : room, guarded = had - first;
        attribute = cell_attribute();
        for (;;)
        {
                text_cell(cells, had, first + (unsigned int)n, attribute | bytes[n]);
                n++;
                if (n == limit || (unsigned int)bytes[n] - ' ' >= 95)
                        break;
                n += cells_from_ascii(cells + first + n, (address_any)(bytes + n), limit - n,
                                      guarded > n ? guarded - n : 0, attribute,
                                      (positive)(WINDOW_CELL_WIDE | WINDOW_CELL_WIDE_RIGHT) << 48);
                if (n == limit || (unsigned int)bytes[n] - ' ' >= 95)
                        break;
        }
        last_character = bytes[n - 1], column = first + (unsigned int)n;
        touch(row);
        if (address_to length < column)
                address_to length = column;
        column_held();
        return n;
}

static unsigned int term_utf8_decode(const p8 address_to bytes, positive count, unsigned int address_to character)
{
        memory_utf8_state state = {0, 0, 0};

        for (unsigned int at = 0; at < count && at < 4; at++)
        {
                b32 whole = memory_utf8_feed(address_of state, bytes[at]);
                if (whole < 0)
                        return 0;
                if (whole > 0)
                        return address_to character = state.value, at + 1;
        }
        return 0;
}

static unsigned int text_utf8(const p8 address_to bytes, positive count, unsigned int address_to character)
{
        unsigned int value, used = term_utf8_decode(bytes, count, address_of value);

        return used && value >= 0xa0 ? (address_to character = value, used) : 0;
}

static positive __attribute__((__noinline__)) text_wide(const p8 address_to bytes, positive count)
{
        struct window_cell address_to cells = null;
        unsigned int address_to length = null, at = column, had = 0, drawn = 0;
        positive n = 0, attribute = 0;

        while (n < count && bytes[n] >= 0x80)
        {
                unsigned int character, width, used, lead = bytes[n];

                if (lead - 0xc2 < 0x1e && count - n >= 2 && (bytes[n + 1] & 0xc0) == 0x80)
                        character = (lead & 0x1f) << 6 | (bytes[n + 1] & 0x3f), used = character < 0xa0 ? 0 : 2;
                else if (lead - 0xe0 < 0x10 && count - n >= 3 && (bytes[n + 1] & 0xc0) == 0x80 &&
                         (bytes[n + 2] & 0xc0) == 0x80 &&
                         (character = (lead & 0x0f) << 12 | (bytes[n + 1] & 0x3f) << 6 | (bytes[n + 2] & 0x3f)) >= 0x800 &&
                         character - 0xd800 >= 0x800)
                        used = 3;
                else
                        used = text_utf8(bytes + n, count - n, address_of character);
                if (!used)
                        break;
                width = term_char_width(character);
                if (width)
                {
                        positive word;

                        if (width > COLUMNS)
                                width = 1;
                        if (at + width > COLUMNS)
                                break;
                        if (!cells)
                                cells = term_row_prepare(row, at, address_of length), had = address_to length, attribute = cell_attribute();
                        unpair(cells, had, at);
                        word = attribute | character;
                        if (width == 2)
                        {
                                positive right = word | (positive)WINDOW_CELL_WIDE_RIGHT << 48;

                                unpair(cells, had, at + 1);
                                word |= (positive)WINDOW_CELL_WIDE << 48;
                                memory_copy(cells + at + 1, address_of right, sizeof(right));
                        }
                        memory_copy(cells + at, address_of word, sizeof(word));
                        drawn = character, at += width;
                }
                n += used;
        }
        if (cells)
        {
                last_character = drawn, column = at, touch(row);
                if (address_to length < column)
                        address_to length = column;
                column_held();
        }
        if (!n)
        {
                unsigned int character;
                if ((n = text_utf8(bytes, count, address_of character)))
                        put(character);
        }
        return n;
}

static positive text_run(const p8 address_to bytes, positive count)
{
        positive at = 0;

        while (at < count)
        {
                unsigned int c = bytes[at], used;

                if (c - ' ' < 95)
                        at += text_ascii(bytes + at, count - at);
                else if (c < 0x80 || !(used = (unsigned int)text_wide(bytes + at, count - at)))
                        break;
                else
                        at += used;
        }
        if (at)
                line_forget();
        return at;
}

static positive csi_parse(const p8 address_to bytes, positive count)
{
        p8 intermediate = 0;
        positive used;

        if (count < 3 || bytes[1] != '[')
                return 0;
        terminal_parameters_reset(address_of terminal_csi);
        if (!(used = terminal_parameters_scan(address_of terminal_csi, bytes + 2, count - 2, address_of intermediate)))
                return 0;
        line_forget();
        utf8_flush();
        escape_intermediate = false, in_escape = in_csi = true, csi_intermediate = intermediate;
        csi_final(bytes[2 + used - 1]);
        in_csi = in_escape = false, csi_intermediate = 0;
        return 2 + used;
}

static fn term_bytes(const p8 address_to bytes, positive count)
{
        for (positive at = 0; at < count;)
        {
                unsigned int c = bytes[at];
                positive used = 0;

                if (c >= ' ' && c != 127 && text_ground())
                        used = text_run(bytes + at, count - at);
                else if (c == 27 && !in_string)
                        used = csi_parse(bytes + at, count - at);
                if (used)
                        at += used;
                else
                        consume(c), at++;
        }
}

/* ---- cursor overlay ---- */

static fn cell_turn(struct window_cell address_to cell)
{
        unsigned char was = cell->ink;

        cell->ink = cell->paper, cell->paper = was;
        cell->flags = flags_swap(cell->flags);
}

static fn cursor_hide()
{
        struct window_cell address_to cell;

        if (!shown)
                return;
        cell = row_cells(shown_row) + shown_column;
        if (shown_block)
                cell_turn(cell);
        else
                cell->flags &= (unsigned short)~shown_marks;
        touch(shown_row);
        shown = false;
}

static fn SPARE cursor_show()
{
        unsigned int at = column < COLUMNS ? column : COLUMNS - 1;
        struct window_cell address_to cell;

        if (shown || !cursor_visible)
                return;
        reach(row, at + 1);
        cell = row_cells(row) + at;
        shown_block = cursor_shape < 3;
        if (shown_block)
                cell_turn(cell);
        else
                shown_marks = (unsigned short)((cursor_shape >= 5 ? WINDOW_CELL_BAR : WINDOW_CELL_UNDERLINE) & ~cell->flags),
                cell->flags |= shown_marks;
        shown_row = row, shown_column = at, shown = true;
        touch(row);
}

/* ---- canonical-mode line editor ---- */

#define LINE_HISTORY 32

static positive line_drawn, line_view;
static b32 line_anchored, line_editing, line_wide;
static unsigned int line_anchor, line_screen_anchor, line_screen_column, line_anchor_column;
static p8 address_to line;
static positive line_room, line_length, line_point;
static p8 address_to history[LINE_HISTORY];
static positive history_room[LINE_HISTORY], history_length[LINE_HISTORY], history_count, history_at;
static p8 address_to history_held;
static positive history_held_room, history_held_length;
static struct window_cell address_to line_prompt;
static positive line_prompt_room, line_prompt_length;

static fn line_forget()
{
        line_anchored = false, line_drawn = line_view = 0;
}

static unsigned int line_anchor_row()
{
        unsigned int at = line_anchor - (window->head - ROWS);

        return at < ROWS ? at : 0;
}

static fn line_erase(b32 shorten);

static positive line_prev(positive at)
{
        if (at)
                at--;
        while (at && (line[at] & 0xc0) == 0x80)
                at--;
        return at;
}

static positive line_next(positive at)
{
        if (at < line_length)
                at++;
        while (at < line_length && (line[at] & 0xc0) == 0x80)
                at++;
        return at;
}

static positive line_snap(positive at)
{
        if (at > line_length)
                at = line_length;
        while (at && at < line_length && (line[at] & 0xc0) == 0x80)
                at--;
        return at;
}

static b32 line_ascii()
{
        for (positive at = 0; at < line_length; at++)
                if (line[at] & 0x80)
                        return false;
        return true;
}

static positive line_decode(positive at, unsigned int address_to character)
{
        unsigned int used = term_utf8_decode(line + at, line_length - at, character);

        return used ? at + used : (address_to character = line[at], at + 1);
}

static unsigned int line_width(unsigned int character)
{
        unsigned int width = term_char_width(character);

        return width > COLUMNS ? 1 : width;
}

static positive line_place(positive f, unsigned int width)
{
        positive col = f % COLUMNS;

        return width && col + width > COLUMNS ? f - col + COLUMNS : f;
}

static positive line_walk(positive at, positive to, positive f)
{
        if (!line_wide)
                return f + (to - at);
        while (at < to)
        {
                unsigned int character, width;
                positive next = line_decode(at, address_of character);

                width = line_width(character), f = line_place(f, width) + width, at = next;
        }
        return f;
}

static positive line_start(positive at, positive f)
{
        unsigned int character;

        if (!line_wide || at >= line_length)
                return f;
        line_decode(at, address_of character);
        return line_place(f, line_width(character));
}

static positive line_at(positive target)
{
        positive at = 0, f = line_anchor_column;

        if (!line_wide)
                return target > f ? min(target - f, line_length) : 0;
        while (at < line_length)
        {
                unsigned int character, width;
                positive next = line_decode(at, address_of character), start;

                width = line_width(character), start = line_place(f, width);
                if (start >= target)
                        break;
                f = start + width, at = next;
        }
        return at;
}

static fn line_show()
{
        positive at, caret, caret_cells, view, first, cursor, limit, f;
        bipolar anchor_row;
        unsigned int top;

        if (!line_anchored)
                return;
        caret = line_point < line_length ? line_point : line_prev(line_point);
        caret_cells = line_start(caret, line_walk(0, caret, line_anchor_column));
        view = line_snap(line_view);
        line_erase(false);
        line_drawn = 0;
        while ((positive)line_anchor + caret_cells / COLUMNS >= window->head)
                ring_scroll();
        if (alternate)
                alternate_claim();
        top = window->head - ROWS, anchor_row = (bipolar)line_anchor - (bipolar)top;
        if (view && caret >= view && caret_cells - line_walk(0, view, line_anchor_column) < (positive)ROWS * COLUMNS)
                first = view, line_screen_anchor = top, line_screen_column = 0;
        else if (anchor_row < 0)
        {
                first = line_at((positive)(-anchor_row) * COLUMNS);
                if (caret < first)
                        first = line_at(caret_cells - caret_cells % COLUMNS);
                line_screen_anchor = top, line_screen_column = 0;
        }
        else
                first = 0, line_screen_anchor = top + (unsigned int)anchor_row, line_screen_column = line_anchor_column;

        limit = ((positive)window->head - line_screen_anchor) * COLUMNS;
        cursor = line_point < first ? line_screen_column : limit;
        f = line_screen_column;
        if (!line_wide)
        {
                positive count = min(line_length - first, limit - f);
                unsigned int r = line_screen_anchor - top, c = line_screen_column;

                if (line_point >= first && line_point - first <= count)
                        cursor = f + (line_point - first);
                for (at = 0; at < count;)
                {
                        positive take;

                        if (c >= COLUMNS)
                                c -= COLUMNS, r++;
                        take = min(count - at, COLUMNS - c);
                        row = r, column = c;
                        for (positive k = 0; k < take; k++)
                                put_cells(line[first + at + k], 1);
                        at += take, r++, c = 0;
                }
                f += count, at = first + count;
        }
        else
                for (at = first; at < line_length;)
                {
                        unsigned int character, width;
                        positive next = line_decode(at, address_of character), start;

                        width = line_width(character), start = line_place(f, width);
                        if (at == line_point)
                                cursor = start;
                        if (start + width > limit)
                                break;
                        if (width)
                                row = line_screen_anchor - top + (unsigned int)(start / COLUMNS),
                                column = (unsigned int)(start % COLUMNS), put_cells(character, width);
                        f = start + width, at = next;
                }
        if (line_point == line_length && at == line_length)
                cursor = f;
        line_view = first, line_drawn = f - line_screen_column;
        row = line_screen_anchor - top + (unsigned int)(cursor / COLUMNS), column = (unsigned int)(cursor % COLUMNS);
        if (row >= ROWS)
                row = ROWS - 1, column = COLUMNS - 1;
}

static fn line_erase(b32 shorten)
{
        unsigned int r, c, top;
        positive left, skipped = 0;
        bipolar screen_row;

        if (!line_anchored || !line_drawn)
                return;
        top = window->head - ROWS, screen_row = (bipolar)line_screen_anchor - (bipolar)top, c = line_screen_column;
        if (screen_row < 0)
        {
                positive past = (positive)(-screen_row) * COLUMNS;
                skipped = past > c ? past - c : 0, screen_row = 0, c = 0;
        }
        if (skipped >= line_drawn || screen_row >= (bipolar)ROWS)
                return;
        r = (unsigned int)screen_row, left = line_drawn - skipped;
        while (left && r < ROWS)
        {
                unsigned int room = COLUMNS - min(c, COLUMNS), address_to length = row_length(r);
                positive taken = left < room ? left : room;

                if (shorten && address_to length > c)
                        address_to length = c;
                else if (!shorten)
                        cells_clear(r, c, (unsigned int)taken);
                touch(r);
                left -= taken, r++, c = 0;
                if (!room)
                        break;
        }
}

static fn line_insert(unsigned int character)
{
        p8 bytes[4];
        positive size = memory_utf8_encode(bytes, sizeof(bytes), character);

        if (!size || !array_store_reserve(line, line_room, line_length, line_length + size, 64))
                return;
        memory_copy(line + line_point + size, line + line_point, line_length - line_point);
        memory_copy_apart(line + line_point, bytes, size);
        line_point += size, line_length += size;
        if (size > 1)
                line_wide = true;
}

static fn line_take(positive from, positive to)
{
        memory_copy(line + from, line + to, line_length - to);
        line_length -= to - from, line_point = from;
        if (line_wide)
                line_wide = !line_ascii();
}

static PURE positive line_word()
{
        positive at = line_point - memory_span_byte_reverse(line, ' ', line_point);
        p8 address_to space = memory_last_of(line, ' ', at);

        return space ? (positive)(space - line) + 1 : 0;
}

static fn line_from_history()
{
        const p8 address_to from = history_at < history_count ? history[history_at % LINE_HISTORY] : history_held;
        positive length = history_at < history_count ? history_length[history_at % LINE_HISTORY] : history_held_length;

        if (!array_store_reserve(line, line_room, line_length, length, 64))
                return;
        memory_copy_apart(line, (address_any)from, length);
        line_length = line_point = length, line_wide = !line_ascii();
}

static fn line_remember()
{
        unsigned int slot;

        if (!line_length)
                return;
        if (history_count && history_length[(history_count - 1) % LINE_HISTORY] == line_length &&
            history[(history_count - 1) % LINE_HISTORY][0] == line[0] &&
            (line_length == 1 || !memory_compare(history[(history_count - 1) % LINE_HISTORY], line, line_length)))
                return;
        slot = history_count % LINE_HISTORY;
        if (!array_store_reserve(history[slot], history_room[slot], history_length[slot], line_length, 64))
                return;
        memory_copy_apart(history[slot], line, line_length);
        history_length[slot] = line_length, history_count++;
}

static fn line_clear_screen()
{
        unsigned int kept = line_anchored ? min((unsigned int)line_prompt_length, COLUMNS) : 0;

        erase(0, 0, ROWS - 1, COLUMNS - 1);
        if (kept)
                reach(0, kept), memory_copy_apart(row_cells(0), line_prompt, kept * sizeof(line_prompt[0]));
        line_anchor = window->head - ROWS, line_anchor_column = kept, line_anchored = true, line_drawn = 0;
        touch_all();
}

static fn line_reset()
{
        line_length = line_point = 0, line_wide = false, history_at = history_count, history_held_length = 0;
        line_forget();
}

static fn line_done()
{
        row = line_anchor_row(), column = line_anchor_column + line_drawn;
        while (column >= COLUMNS)
                column -= COLUMNS, row++;
        if (row >= ROWS)
                row = ROWS - 1;
        line_feed();
        column = 0, touch(row);
        line_reset();
}

static fn line_accept()
{
        line_remember();
        if (to_shell_fits(line_length + 1))
                emit_bytes(line, line_length), emit('\n');
        line_done();
}

#define KEY_HOME 102
#define KEY_UP 103
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define KEY_END 107
#define KEY_DOWN 108
#define KEY_DELETE 111
#define KEY_ENTER 28
#define KEY_KPENTER 96

static b32 line_key(unsigned int character, unsigned int code)
{
        if (!line_anchored)
        {
                positive prompt;

                line_anchor = window->head - ROWS + row;
                line_anchor_column = column < COLUMNS ? column : COLUMNS - 1;
                prompt = line_anchor_column;
                if (array_store_reserve(line_prompt, line_prompt_room, line_prompt_length, prompt, 64))
                        memory_copy_apart(line_prompt, row_cells(row), prompt * sizeof(*line_prompt)), line_prompt_length = prompt;
                else
                        line_prompt_length = 0;
                line_anchored = true, line_drawn = 0;
        }
        switch (code)
        {
        case KEY_LEFT:   character = 2; break;
        case KEY_RIGHT:  character = 6; break;
        case KEY_HOME:   character = 1; break;
        case KEY_END:    character = 5; break;
        case KEY_DELETE: if (line_point < line_length) line_take(line_point, line_next(line_point)); return true;
        case KEY_UP:
        {
                positive first = history_count > LINE_HISTORY ? history_count - LINE_HISTORY : 0;

                if (history_at <= first)
                        return true;
                if (history_at == history_count)
                {
                        if (!array_store_reserve(history_held, history_held_room, history_held_length, line_length, 64))
                                return true;
                        memory_copy_apart(history_held, line, line_length);
                        history_held_length = line_length;
                }
                history_at--, line_from_history();
                return true;
        }
        case KEY_DOWN:
                if (history_at < history_count)
                        history_at++, line_from_history();
                return true;
        }
        if (!character)
                return true;
        switch (character)
        {
        case 1:           line_point = 0; break;
        case 2:           if (line_point) line_point = line_prev(line_point); break;
        case 5:           line_point = line_length; break;
        case 6:           if (line_point < line_length) line_point = line_next(line_point); break;
        case 8: case 127: if (line_point) line_take(line_prev(line_point), line_point); break;
        case 4:           if (line_length) { if (line_point < line_length) line_take(line_point, line_next(line_point)); } else emit(4); break;
        case 11:          if (line_point < line_length) line_take(line_point, line_length); break;
        case 21:          if (line_point) line_take(0, line_point); break;
        case 23:          if (line_point) line_take(line_word(), line_point); break;
        case 12:          line_clear_screen(); break;
        case '\r': case '\n': line_accept(); break;
        case 3:           emit(3); line_done(); break;
        default:
                if (character == '\t' || character >= ' ')
                        line_insert(character);
                else if (character == 26 || character == 28)
                        emit(character);
                break;
        }
        return true;
}

/* ---- key, paste, pointer, and focus encoding ---- */

static const struct
{
        unsigned int code;
        const char address_to sequence, address_to application;
} key_sequences[] = {
    {KEY_UP, "\x1b[A", "\x1bOA"},    {KEY_DOWN, "\x1b[B", "\x1bOB"},
    {KEY_RIGHT, "\x1b[C", "\x1bOC"}, {KEY_LEFT, "\x1b[D", "\x1bOD"},
    {KEY_HOME, "\x1b[H", "\x1bOH"},  {KEY_END, "\x1b[F", "\x1bOF"},
    {104, "\x1b[5~", 0}, {109, "\x1b[6~", 0}, {110, "\x1b[2~", 0}, {KEY_DELETE, "\x1b[3~", 0},
    {59, "\x1bOP", 0},   {60, "\x1bOQ", 0},   {61, "\x1bOR", 0},   {62, "\x1bOS", 0},
    {63, "\x1b[15~", 0}, {64, "\x1b[17~", 0}, {65, "\x1b[18~", 0}, {66, "\x1b[19~", 0},
    {67, "\x1b[20~", 0}, {68, "\x1b[21~", 0}, {87, "\x1b[23~", 0}, {88, "\x1b[24~", 0},
};

static string_address key_sequence(unsigned int code)
{
        for (positive i = 0; i < array_count(key_sequences); i++)
                if (key_sequences[i].code == code)
                        return (string_address)(application_keys && key_sequences[i].application ? key_sequences[i].application
                                                                                                 : key_sequences[i].sequence);
        return null;
}

static unsigned int keypad_letter(unsigned int code)
{
        static const struct { unsigned short code; unsigned char letter; } keys[] = {
            {82, 'p'}, {79, 'q'}, {80, 'r'}, {81, 's'}, {75, 't'}, {76, 'u'}, {77, 'v'}, {71, 'w'}, {72, 'x'},
            {73, 'y'}, {96, 'M'}, {78, 'k'}, {74, 'm'}, {55, 'j'}, {98, 'o'}, {83, 'n'}, {117, 'X'},
        };

        for (positive i = 0; i < array_count(keys); i++)
                if (keys[i].code == code)
                        return keys[i].letter;
        return 0;
}

static fn SPARE term_key_modified(unsigned int character, unsigned int code, unsigned int modifiers)
{
        string_address sequence;
        unsigned int held = modifiers & (WINDOW_KEY_SHIFT | WINDOW_KEY_ALT | WINDOW_KEY_CONTROL), kp;

        if (!to_shell_fits(TERM_MESSAGE_MAX))
                return;
        if (code == KEY_ENTER || code == KEY_KPENTER)
                character = '\r';
        if ((held & WINDOW_KEY_CONTROL) && character && !(code >= 71 && code <= 83) && code != 55 && code != 98)
        {
                static const char symbols[] = " 2@3[4\\5]6^7_/8?";
                static const unsigned char codes[] = {0, 0, 0, 27, 27, 28, 28, 29, 29, 30, 30, 31, 31, 31, 127, 127};
                const char *hit = character < 256 ? (const char *)memory_first_of((address_any)symbols, (b8)character, sizeof(symbols) - 1) : 0;

                if (hit && !(character = codes[hit - symbols]))
                {
                        if (!line_editing)
                        {
                                if (held & WINDOW_KEY_ALT)
                                        emit(27);
                                emit(0);
                        }
                        return;
                }
        }
        if (line_editing && line_key(character, code))
                return line_show();
        if (application_keypad && (kp = keypad_letter(code)))
                return emit_bytes((p8[]){27, 'O', (p8)kp}, 3);
        if (character)
        {
                if (character == '\t' && (held & WINDOW_KEY_SHIFT))
                        return emit_literal("\x1b[Z");
                if (held & WINDOW_KEY_ALT)
                        emit(27);
                return emit_character(character);
        }
        if (!(sequence = key_sequence(code)))
                return;
        if (held)
        {
                unsigned int value = 1 + ((held & WINDOW_KEY_SHIFT) ? 1 : 0) + ((held & WINDOW_KEY_ALT) ? 2 : 0) +
                                     ((held & WINDOW_KEY_CONTROL) ? 4 : 0);
                positive length = string_length((string_address)sequence);

                if (length > 2 && sequence[1] == '[' && sequence[length - 1] == '~')
                        return emit_bytes((address_any)sequence, length - 1), emit_csi_nums(";", 0, address_of value, 1, "~");
                if (length == 3 && (sequence[1] == '[' || sequence[1] == 'O'))
                        return emit_csi_nums("\x1b[", 0, (unsigned int[]){1, value}, 2, (char[]){(char)sequence[2], 0});
        }
        emit_bytes((address_any)sequence, string_length((string_address)sequence));
}

static positive term_paste_filter(const p8 address_to text, positive length, p8 address_to into)
{
        static const p16 moves[4] = {0, 2, 2, 2};
        static p8 classes[256], classes_ready;
        positive out = 0, at = 0;

        if (!classes_ready)
        {
                for (int i = 0; i < 256; i++)
                        classes[i] = ((i >= ' ' && i != 0x7f && i != 0xc2) || i == '\t') ? 0 : 1;
                classes_ready = 1;
        }
        while (at < length)
        {
                p16 state = 0;
                positive taken = automaton_scan(moves, classes, 2, &state, text + at, length - at);

                if (state < 2)
                {
                        memory_copy_apart(into + out, text + at, taken), out += taken;
                        break;
                }
                if (taken > 1)
                        memory_copy_apart(into + out, text + at, taken - 1), out += taken - 1;
                at += taken - 1;
                {
                        p8 byte = text[at++];

                        if (byte == '\r' && at < length && text[at] == '\n')
                                continue;
                        if (byte == '\n' || byte == '\r')
                                into[out++] = '\r';
                        else if ((byte < ' ' && byte != '\t') || byte == 0x7f)
                                continue;
                        else if (byte == 0xc2 && at < length && text[at] >= 0x80 && text[at] <= 0x9f)
                                at++;
                        else
                                into[out++] = byte;
                }
        }
        return out;
}

static fn SPARE term_paste(const p8 address_to text, positive length)
{
        static p8 address_to filtered;
        static positive filtered_room;
        positive size;

        if (!length || !array_store_reserve(filtered, filtered_room, 0, length, 4096))
                return;
        size = term_paste_filter(text, length, filtered);
        if (line_editing)
        {
                memory_utf8_state state = {0, 0, 0};

                for (positive at = 0; at < size; at++)
                        if (memory_utf8_feed(address_of state, filtered[at]) > 0)
                                line_insert(state.value == '\r' ? '\n' : state.value);
                return line_show();
        }
        if (bracketed_paste)
                emit_literal("\x1b[200~");
        emit_bytes(filtered, size);
        if (bracketed_paste)
                emit_literal("\x1b[201~");
}

static fn SPARE term_key(unsigned int character, unsigned int code)
{
        term_key_modified(character, code, 0);
}

static fn SPARE term_line_editing(b32 on)
{
        if (line_editing != on)
                line_editing = on, line_reset();
}

static fn term_pointer(unsigned int col, unsigned int row, unsigned int button, unsigned int flags)
{
        b32 down = (flags & WINDOW_KEY_DOWN) != 0, move = (flags & WINDOW_KEY_POINTER_MOVE) != 0;
        unsigned int code = button + (move ? 32 : 0);

        if (!to_shell_fits(TERM_MESSAGE_MAX) || !mouse_mode || (move && (mouse_mode == 1000 || (mouse_mode == 1002 && !down))))
                return;
        col = col ? col : 1, row = row ? row : 1;
        if (mouse_sgr)
                return emit_csi_nums("\x1b[<", 0, (unsigned int[]){code, col, row}, 3, (down || move) ? "M" : "m");
        col = min(col, 223u), row = min(row, 223u);
        if (!down && !move)
                code = 3;
        emit_bytes((p8[]){27, '[', 'M', (p8)(32 + code), (p8)(32 + col), (p8)(32 + row)}, 6);
}

static fn term_focus(b32 in)
{
        if (focus_events)
                emit_bytes(in ? (address_any) "\x1b[I" : (address_any) "\x1b[O", 3);
}

/* ---- pty sizing and grid resize ---- */

#define F_GETFD 1

fn claim_standard_descriptors()
{
        for (b32 i = 0; i < 3; i++)
                if (system_call_3(syscall(fcntl), i, F_GETFD, 0) < 0)
                        system_open_at(AT_FDCWD, "/dev/null", FILE_READ_WRITE);
}

static fn grid_take()
{
        COLUMNS = window->columns ? window->columns : 1;
        ROWS = window->rows ? window->rows : 1;
        window_widen(window, COLUMNS);
        COLUMNS = min(COLUMNS, min(window->stride, TAB_COLUMNS));
}

static fn grid_tell(b32 master)
{
        winsize size = {(unsigned short)ROWS, (unsigned short)COLUMNS,
                        (unsigned short)(COLUMNS * term_cell_w), (unsigned short)(ROWS * term_cell_h)};

        system_control(master, TIOCSWINSZ, address_of size);
}

static fn alternate_cut(void)
{
        for (unsigned int r = 0; r < ROWS; r++)
        {
                unsigned int slot = row_slot(r), address_to length = slot_length(slot);
                struct window_cell address_to cells = slot_cells(slot);

                if (address_to length <= COLUMNS)
                        continue;
                if (cells[COLUMNS - 1].flags & WINDOW_CELL_WIDE)
                        cells[COLUMNS - 1].character = ' ', cells[COLUMNS - 1].flags &= (unsigned short)~WINDOW_CELL_WIDE;
                address_to length = COLUMNS;
        }
}

fn regrid(b32 master)
{
        unsigned int was_rows = ROWS;
        bipolar shift;
        b32 cursor_was_shown = shown;

        synchronized_output = false;
        cursor_hide();
        if (line_editing)
                line_erase(true);
        grid_take();
        if (alternate)
        {
                unsigned int added = 0;

                for (; window->head - alternate_head < ROWS; added++)
                        window_scroll(window);
                alternate_claim();
                alternate_cut();
                shift = (bipolar)ROWS - (bipolar)was_rows - (bipolar)added;
        }
        else
                shift = primary_regrid(row, was_rows);

        row = regrid_row(row, shift);
        cursor_saved.row = regrid_row(cursor_saved.row, shift);
        if (column >= COLUMNS)
                column = COLUMNS - 1;
        if (line_anchor_column >= COLUMNS)
                line_anchor_column = COLUMNS - 1;
        region_top = 0, region_bottom = ROWS, line_drawn = line_view = 0;
        if (line_editing)
                line_show();
        shown = false;
        touch_all();
        window_grid(window, COLUMNS, ROWS);
        if (master >= 0 || cursor_was_shown)
                cursor_show();
        if (master >= 0)
        {
                window_damage(window, 0, ROWS);
                __atomic_store_n(&((struct window *)(window))->sequence, ((struct window *)(window))->sequence + 1, __ATOMIC_RELEASE);
                touched_top = ROWS, touched_bottom = 0;
                grid_tell(master);
        }
}
