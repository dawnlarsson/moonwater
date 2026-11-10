/*
        The on-screen keyboard, as a window on the compositor in userspace.

        A strip along the bottom of the primary screen, above the panel: four rows of keys drawn with the kit's buttons (ui.c), and
        nothing round them. The keyboard never has the keys. Its window is WINDOW_NO_KEYS, so a press on a key does not move the keys
        from the field that has them (the compositor never gives a WINDOW_NO_KEYS window the keys, when it is made or pressed), and
        WINDOW_NO_TAB, so it is in no focus cycle. A key is sent to the window with the keys as a key of the keyboard's own would be:
        its evdev code, the character the key makes, and the modifiers (Shift), through the window table's key ring (windows_keyboard_send,
        window.c), which the compositor reads (compositor_osk_read) only while a press of the person's is on this window. So the
        editor, the terminal, the launcher and the Settings fields take the keys unchanged.

        Pages: the letters; the numbers and the common symbols, with Tab and the arrows on the second symbols page; and the
        second symbols page. The letters follow the keyboard layout the compositor keeps (the table's osk_layout): QWERTY, QWERTZ
        (de) or AZERTY (fr), and QWERTY for every other layout (osk_layouts). Dvorak is not one of the layouts the setting offers. Shift is a latch: one tap makes the next letter a capital, two taps in a row (within OSK_SHIFT_NS) hold it
        for every letter, one more tap lets go. Backspace and the arrows repeat while their key is held (after OSK_HOLD_NS, every
        OSK_REPEAT_NS). A key's feedback is the kit's: its ripple, its press and its hover.

        A letter held for the kit's hold (UI_HOLD_NS) shows its accents (osk_accents, the Latin letters of the Settings languages) in a
        row above the key, or over it on the top row. The letter is typed on the press, as any key's; lifting on an accent takes that
        letter back with one Backspace and types the accent on the key's own code; lifting anywhere else sends nothing more.

        The keyboard's height is a share of the output's (osk_height_of): 40 percent landscape, 50 percent portrait, never over half, and
        its rows at least the touch target. The keyboard is sized again when the output changes (osk_tick), and the compositor puts it away
        when a screen turns (compositor_osk_turned).

        Two fingers at once (a shift held with a letter) is not done: a window's events carry no finger, so the compositor does not tell
        a second press from a first. The keyboard takes presses one at a time.

            osk [-f FONT.ttf] [-t FILE]

        -f is the face the keys are drawn in. -t FILE writes the screen rectangle of each key, in pixels, when the keyboard opens (the
        keyboard's own test hook, for the e2e lane): one line a key, "page row column x y w h label".

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_OSK
#define MOONWATER_CANVAS_OSK

#define OSK_WIDTH 640     // logical pixels: the keyboard's width, its keys and their gaps are the kit's (never wider than the output)
#define OSK_TOUCH 48      // a key's touch target, logical: the rows are never shorter
#define OSK_GAP 6         // between keys, logical
#define OSK_ROWS 4
#define OSK_COLS 12       // the most keys a row has (spacers count)
#define OSK_PAGES 3
// The keyboard's height is a share of the output's height, in device pixels: 40 percent when the output is wider than it is (landscape),
// 50 percent when it is taller (portrait), and never more than half the output. The rows are at least the touch target, when that fits.
#define OSK_HEIGHT_LANDSCAPE 40
#define OSK_HEIGHT_PORTRAIT 50
#define OSK_HEIGHT_LEAST (2 * UI_PAD + OSK_ROWS * OSK_TOUCH + (OSK_ROWS - 1) * OSK_GAP) // logical
#define OSK_HOLD_NS 500000000ull   // a held key repeats after this
#define OSK_REPEAT_NS 60000000ull  // and then every this
#define OSK_SHIFT_NS 400000000ull  // two taps of Shift this close make it hold (caps)
#define OSK_ALT_MOST 9             // the accents a letter has in the table below (its row is ten: the letter and nine)
#define OSK_ALT_PATH_MOST 256      // the trace's name with ".alt" on it
#define OSK_TRACE_LINE 96
#define OSK_TRACE_MOST ((OSK_PAGES + 1) * OSK_ROWS * OSK_COLS * OSK_TRACE_LINE) // the pages, and the email page the trace writes after them

// What a key does.
#define OSK_NONE 0   // a space between keys: drawn nowhere, hit by nothing
#define OSK_CHAR 1   // sends its character (and its evdev code)
#define OSK_SHIFT 2  // the latch
#define OSK_BACK 3   // Backspace, repeats
#define OSK_ENTER 4
#define OSK_SPACE 5
#define OSK_TAB 6
#define OSK_ARROW 7  // repeats; its code is the arrow's
#define OSK_PAGE 8   // shows the page in its alt
#define OSK_WORD 9   // a short run of letters and dots (.com): each character is a key press and a release

// The content purposes the page is picked by (the protocol's content_purpose values, the table's osk_purpose).
#define OSK_PURPOSE_DIGITS 2
#define OSK_PURPOSE_NUMBER 3
#define OSK_PURPOSE_URL 5
#define OSK_PURPOSE_EMAIL 6

// The evdev codes the keys send (linux/input-event-codes.h).
#define OSK_CODE_BACKSPACE 14
#define OSK_CODE_TAB 15
#define OSK_CODE_ENTER 28
#define OSK_CODE_SPACE 57
#define OSK_CODE_LEFT 105
#define OSK_CODE_RIGHT 106
#define OSK_CODE_UP 103
#define OSK_CODE_DOWN 108

struct osk_key
{
        unsigned char kind, span;   // what the key does; its width in halves of a unit, against the others in its row
        unsigned short code;        // the evdev code it sends
        unsigned int ch, alt;       // the character it makes (OSK_CHAR), and the one with Shift (0: the letter's capital); OSK_PAGE: the page
        const char address_to word; // the words on it, for a key that makes no character
};

#define OSK_L(code, ch) {OSK_CHAR, 2, code, ch, 0, null}
#define OSK_W(kind, span, code, word) {kind, span, code, 0, 0, word}
#define OSK_S(kind, span) {kind, span, 0, 0, 0, null}
#define OSK_P(span, page, word) {OSK_PAGE, span, 0, 0, page, word}
#define OSK_C(code, ch, alt) {OSK_CHAR, 2, code, ch, alt, null}

#define OSK_PAGE_LETTERS 0
#define OSK_PAGE_NUMBERS 1
#define OSK_PAGE_SYMBOLS 2

// The letter rows of each keyboard layout the compositor keeps, by its name (two letters, packed: us is 'u' | 's' << 8). A row's
// letters are in order, and the row's evdev codes are the physical keys' from the row's first code on (osk_letter_start): the key a
// letter is on, as the layout has it. QWERTY is the default, for a layout that is not listed.
#define OSK_LAYOUT(a, b) ((unsigned int)(a) | (unsigned int)(b) << 8)
static const struct osk_layout
{
        unsigned int name;
        const char address_to rows[3];
} osk_layouts[] = {
    {OSK_LAYOUT('u', 's'), {"qwertyuiop", "asdfghjkl", "zxcvbnm"}},
    {OSK_LAYOUT('d', 'e'), {"qwertzuiop", "asdfghjkl", "yxcvbnm"}},
    {OSK_LAYOUT('f', 'r'), {"azertyuiop", "qsdfghjklm", "wxcvbn"}},
};
static const unsigned short osk_letter_start[3] = {16, 30, 44};

// The accents a long press of a letter shows: each row is the lower-case letter and the characters it takes (code points), in the
// order they are shown. They cover the Latin-script languages of the Settings list (Español, Français, Deutsch, Português, Italiano,
// Nederlands, Polski, Türkçe, Svenska; English has none, and the Cyrillic, Greek, Arabic, Hebrew, Persian and Urdu keyboards are not
// Latin letters). The layout does not change them. Capitals come from osk_upper (Shift on the key makes the capital of each).
static const unsigned int osk_accents[][10] = {
    {'a', 0xe0, 0xe1, 0xe2, 0xe4, 0xe3, 0xe5, 0xe6, 0x105},          // à á â ä ã å æ ą
    {'c', 0xe7, 0x107, 0x10d},                                       // ç ć č
    {'e', 0xe9, 0xe8, 0xea, 0xeb, 0x119},                            // é è ê ë ę
    {'g', 0x11f},                                                    // ğ
    {'i', 0xec, 0xed, 0xee, 0xef, 0x131},                            // ì í î ï ı
    {'l', 0x142},                                                    // ł
    {'n', 0xf1, 0x144},                                              // ñ ń
    {'o', 0xf3, 0xf2, 0xf4, 0xf6, 0xf5, 0x153},                      // ó ò ô ö õ œ
    {'s', 0xdf, 0x15b, 0x15f, 0x161},                                // ß ś ş š
    {'u', 0xf9, 0xfa, 0xfb, 0xfc},                                   // ù ú û ü
    {'y', 0xff},                                                     // ÿ
    {'z', 0x17a, 0x17c, 0x17e},                                      // ź ż ž
};

// The capital of an accent in the table: Latin-1's small letters are 0x20 above their capitals (ÿ is Ÿ), the Latin Extended-A letters
// in the table are the code after their capital, and ı is I. ß has no capital here and stays.
static unsigned int osk_upper(unsigned int ch)
{
        if (ch == 0x131)
                return 'I';
        if (ch >= 0xe0 && ch <= 0xfe && ch != 0xf7)
                return ch - 0x20;
        if (ch == 0xff)
                return 0x178;
        if (ch >= 0x100 && ch <= 0x17f)
                return ch - 1;
        return ch;
}

// The pages: the letters (built for the layout, osk_letters_build), the numbers and the common symbols, the second symbols page. A row
// of keys is the kit's buttons in a line, each as wide as its span of the row's halves says, and the row fills the keyboard's width.
static struct osk_key osk_letter_keys[OSK_ROWS][OSK_COLS];
static const struct osk_key osk_bottom[OSK_COLS] = {
    OSK_P(3, OSK_PAGE_NUMBERS, "123"), OSK_L(51, ','), OSK_W(OSK_SPACE, 10, OSK_CODE_SPACE, "Space"), OSK_L(52, '.'), OSK_W(OSK_ENTER, 3, OSK_CODE_ENTER, "Enter"),
};

static const struct osk_key osk_numbers[OSK_ROWS][OSK_COLS] = {
    {OSK_C(2, '1', 0), OSK_C(3, '2', 0), OSK_C(4, '3', 0), OSK_C(5, '4', 0), OSK_C(6, '5', 0), OSK_C(7, '6', 0), OSK_C(8, '7', 0), OSK_C(9, '8', 0),
     OSK_C(10, '9', 0), OSK_C(11, '0', 0)},
    {OSK_C(12, '-', 0), OSK_C(53, '/', 0), OSK_C(39, ':', 0), OSK_C(39, ';', 0), OSK_C(10, '(', 0), OSK_C(11, ')', 0), OSK_C(5, '$', 0), OSK_C(8, '&', 0),
     OSK_C(3, '@', 0), OSK_C(40, '"', 0)},
    {OSK_P(3, OSK_PAGE_SYMBOLS, "#+="), OSK_C(52, '.', 0), OSK_C(51, ',', 0), OSK_C(53, '?', 0), OSK_C(2, '!', 0), OSK_C(40, '\'', 0),
     OSK_W(OSK_BACK, 3, OSK_CODE_BACKSPACE, "Back")},
    {OSK_P(3, OSK_PAGE_LETTERS, "ABC"), OSK_W(OSK_SPACE, 5, OSK_CODE_SPACE, "Space"), OSK_W(OSK_ENTER, 3, OSK_CODE_ENTER, "Enter")},
};

static const struct osk_key osk_symbols[OSK_ROWS][OSK_COLS] = {
    {OSK_C(26, '[', 0), OSK_C(27, ']', 0), OSK_C(26, '{', 0), OSK_C(27, '}', 0), OSK_C(4, '#', 0), OSK_C(6, '%', 0), OSK_C(7, '^', 0), OSK_C(9, '*', 0),
     OSK_C(13, '+', 0), OSK_C(13, '=', 0)},
    {OSK_S(OSK_NONE, 1), OSK_C(12, '_', 0), OSK_C(43, '\\', 0), OSK_C(43, '|', 0), OSK_C(41, '~', 0), OSK_C(51, '<', 0), OSK_C(52, '>', 0),
     OSK_C(41, '`', 0), OSK_C(40, '\'', 0), OSK_S(OSK_NONE, 1)},
    {OSK_P(3, OSK_PAGE_NUMBERS, "123"), OSK_W(OSK_TAB, 2, OSK_CODE_TAB, "Tab"), OSK_W(OSK_ARROW, 2, OSK_CODE_UP, "Up"),
     OSK_W(OSK_ARROW, 2, OSK_CODE_DOWN, "Down"), OSK_W(OSK_BACK, 3, OSK_CODE_BACKSPACE, "Back")},
    {OSK_P(3, OSK_PAGE_LETTERS, "ABC"), OSK_W(OSK_ARROW, 2, OSK_CODE_LEFT, "Left"), OSK_W(OSK_SPACE, 5, OSK_CODE_SPACE, "Space"),
     OSK_W(OSK_ARROW, 2, OSK_CODE_RIGHT, "Right"), OSK_W(OSK_ENTER, 3, OSK_CODE_ENTER, "Enter")},
};

// The symbols page of an email or a url field: the same, with @ for the first key of its second row and .com for the last.
static struct osk_key osk_symbols_mail[OSK_ROWS][OSK_COLS];
static const struct osk_key (*osk_page_keys[OSK_PAGES])[OSK_COLS] = {osk_letter_keys, osk_numbers, osk_symbols};

// The letters of a layout into the letters page: its three letter rows, the rows' spacers (a row of nine letters is centred, ten fill
// it) and Shift and Back at the ends of the last, and the bottom row as it is. A layout that is not listed is QWERTY.
static void osk_letters_build(unsigned int name)
{
        const struct osk_layout address_to layout = &osk_layouts[0];

        for (unsigned int at = 0; at < sizeof(osk_layouts) / sizeof(osk_layouts[0]); at++)
                if (osk_layouts[at].name == name)
                        layout = &osk_layouts[at];
        for (int row = 0; row < 3; row++)
        {
                const char address_to letters = layout->rows[row];
                int count = (int)string_length((string_address)letters), at = 0;

                for (int k = 0; k < OSK_COLS; k++)
                        osk_letter_keys[row][k] = (struct osk_key){OSK_NONE, 0, 0, 0, 0, null};
                if (row == 1 && count == 9)
                        osk_letter_keys[row][at++] = (struct osk_key)OSK_S(OSK_NONE, 1);
                if (row == 2)
                        osk_letter_keys[row][at++] = (struct osk_key)OSK_W(OSK_SHIFT, 3, 42, "Shift");
                for (int k = 0; k < count; k++, at++)
                        osk_letter_keys[row][at] = (struct osk_key)OSK_L(osk_letter_start[row] + k, letters[k]);
                if (row == 1 && count == 9)
                        osk_letter_keys[row][at++] = (struct osk_key)OSK_S(OSK_NONE, 1);
                if (row == 2)
                        osk_letter_keys[row][at++] = (struct osk_key)OSK_W(OSK_BACK, 3, OSK_CODE_BACKSPACE, "Back");
        }
        for (int k = 0; k < OSK_COLS; k++)
                osk_letter_keys[3][k] = osk_bottom[k];
}

// A row's keys in a page: the count is the keys a row has, the spacers included.
static int osk_row_count(int page, int row)
{
        int count = 0;

        for (int k = 0; k < OSK_COLS; k++)
                count += osk_page_keys[page][row][k].kind != 0 || osk_page_keys[page][row][k].span != 0;
        return count;
}

// The keyboard's state. keys are the kit's controls, one a key of the page shown (the spacers and the empty ones have no size).
static struct
{
        struct window_row own;
        struct ui ui;
        struct ui_control keys[OSK_ROWS * OSK_COLS];
        char words[OSK_ROWS * OSK_COLS][4]; // the character a key makes, drawn: upper case under Shift
        int width, height, shown;
        int screen_width, screen_height;    // the output the keyboard is sized for (osk_metrics); another one sizes it again (osk_tick)
        int left, top;                      // where the keyboard is placed, in the screen's pixels
        b32 dirty, focused;
        unsigned int ground;
        int page;
        unsigned int layout;                // the keyboard layout the letters are built for (the table's osk_layout; ~0 before the first)
        unsigned int purpose;               // the content purpose the page was picked by (the table's osk_purpose; ~0 before the first)
        unsigned int shift;                 // 0 off, 1 the next letter, 2 held (caps)
        b64 shift_at;                       // the last tap of Shift
        int held;                           // the key the press is on (its index), or -1
        unsigned short held_code;           // what its release sends
        int repeat;                         // the key that repeats, or -1
        b64 next;                           // when the held key next repeats
        int long_key;                       // the letter a press is on, until it is a long press (its index), or -1
        b64 long_at;                        // when that press becomes a long one (the kit's hold)
        unsigned int long_capital;          // the press typed a capital (Shift was on): its accents are capitals too
        int alt_key;                        // the letter whose accents are shown (its index), or -1
        int alt_count;                      // the accents shown, in alts (the row above the key, or over it on the top row)
        unsigned int alt_chars[OSK_ALT_MOST];
        struct ui_control alts[OSK_ALT_MOST];
        char alt_words[OSK_ALT_MOST][5];    // the accent each control says (UTF-8, a NUL)
        const char *trace;                  // -t FILE
        char trace_text[OSK_TRACE_MOST];
} osk;

static struct ui_app osk_app;

// The code and the character a key sends now: the shift state makes a letter a capital and a symbol its Shift form.
static unsigned int osk_character(const struct osk_key address_to key)
{
        unsigned int ch = key->ch;

        if (osk.shift && ch >= 'a' && ch <= 'z')
                return ch - 'a' + 'A';
        if (osk.shift && key->alt)
                return key->alt;
        return ch;
}

static unsigned int osk_modifiers(void)
{
        return osk.shift ? WINDOW_KEY_SHIFT : 0;
}

// The keys of the page shown, laid out in the window's pixels. A row fills the width: each key is its span of the row's halves.
static void osk_layout(void)
{
        struct ui address_to ui = &osk.ui;
        int pad = ui_px(ui, UI_PAD), gap = ui_px(ui, OSK_GAP), row_h, top = pad;

        row_h = (osk.height - 2 * pad - (OSK_ROWS - 1) * gap) / OSK_ROWS;
        for (int row = 0; row < OSK_ROWS; row++)
        {
                int count = osk_row_count(osk.page, row), total = 0, avail, x = pad;

                for (int k = 0; k < count; k++)
                        total += osk_page_keys[osk.page][row][k].span;
                avail = osk.width - 2 * pad - (count > 1 ? (count - 1) * gap : 0);
                for (int k = 0; k < OSK_COLS; k++)
                {
                        const struct osk_key address_to key = &osk_page_keys[osk.page][row][k];
                        struct ui_control address_to c = &osk.keys[row * OSK_COLS + k];
                        int w, index = row * OSK_COLS + k;

                        if (k >= count)
                        {
                                c->w = c->h = 0;
                                c->kind = 0;
                                continue;
                        }
                        w = (int)((long)avail * key->span / total);
                        c->kind = key->kind == OSK_NONE ? 0 : UI_BUTTON;
                        c->variant = key->kind == OSK_SHIFT && osk.shift ? UI_BUTTON_PRIMARY : UI_BUTTON_KEY;
                        c->disabled = 0;
                        c->x = x * 100 / ui->scale, c->y = top * 100 / ui->scale;
                        c->w = w * 100 / ui->scale, c->h = row_h * 100 / ui->scale;
                        if (key->kind == OSK_CHAR)
                        {
                                unsigned int ch = osk_character(key);

                                osk.words[index][0] = (char)ch, osk.words[index][1] = 0;
                                c->label = osk.words[index];
                        }
                        else
                                c->label = key->word;
                        x += w + gap;
                }
                top += row_h + gap;
        }
}

static void osk_relayout(void)
{
        osk_layout();
        osk.dirty = true;
}

static void osk_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &osk.ui;

        ui_begin(ui, pixels, osk.own.table->stride, osk.width, osk.height);
        for (int k = 0; k < OSK_ROWS * OSK_COLS; k++)
                if (osk.keys[k].kind)
                        ui_control_draw(ui, &osk.keys[k]);
        // The accents of a long press are drawn over the keys, after them.
        if (osk.alt_key >= 0)
                for (int at = 0; at < osk.alt_count; at++)
                        ui_control_draw(ui, &osk.alts[at]);
}

static b32 osk_frame(void)
{
        return ui_app_present(&osk.own, &osk.shown, &osk.dirty, osk.width, osk.height, osk_draw, null);
}

// A capital or a symbol that is a shifted key on the US keyboard: its key is sent with Shift, so a client that makes its character from
// the key and the modifiers (a Wayland client) makes the same one. The kit's windows take the character as it is.
static b32 osk_shifted(unsigned int ch)
{
        static const char shifted[] = "!@#$%^&*()_+{}|:\"~<>?";

        if (ch >= 'A' && ch <= 'Z')
                return true;
        for (int at = 0; shifted[at]; at++)
                if ((unsigned int)(unsigned char)shifted[at] == ch)
                        return true;
        return false;
}

// A key sent: a press (with the Shift the keyboard holds, or the one its character takes) or a release, to the window with the keys.
static void osk_send(unsigned int code, unsigned int ch, b32 down)
{
        unsigned int mods = osk_modifiers();

        if (down && osk_shifted(ch))
                mods |= WINDOW_KEY_SHIFT;
        windows_keyboard_send(&osk.own, (int)code, (int)ch, (down ? WINDOW_KEY_DOWN : 0) | mods);
}

// The evdev code of a letter or a dot on the page of letters (the layout's, so the word follows the layout), or 0 for none.
static unsigned int osk_code_of(unsigned int ch)
{
        for (int row = 0; row < OSK_ROWS; row++)
                for (int k = 0; k < OSK_COLS; k++)
                        if (osk_letter_keys[row][k].kind == OSK_CHAR && osk_letter_keys[row][k].ch == ch)
                                return osk_letter_keys[row][k].code;
        return 0;
}

// The Shift latch: one tap for the next letter, a second tap soon after for every letter, a third tap, or a tap after one, off.
static void osk_shift_tap(void)
{
        b64 now = ui_kit.now;

        if (osk.shift == 2)
                osk.shift = 0;
        else if (osk.shift == 1 && now - osk.shift_at < OSK_SHIFT_NS)
                osk.shift = 2;
        else
                osk.shift = osk.shift ? 0 : 1;
        osk.shift_at = now;
}

// The accents of a letter (its row of osk_accents), or null for a letter that has none.
static const unsigned int *osk_accents_of(unsigned int letter)
{
        for (unsigned int at = 0; at < sizeof(osk_accents) / sizeof(osk_accents[0]); at++)
                if (osk_accents[at][0] == letter)
                        return osk_accents[at];
        return null;
}

// A press on a key: what it does, and the repeat it starts when it repeats.
static void osk_press(int index)
{
        const struct osk_key address_to key = &osk_page_keys[osk.page][index / OSK_COLS][index % OSK_COLS];

        osk.held = index;
        osk.repeat = -1;
        osk.long_key = -1;
        switch (key->kind)
        {
        case OSK_CHAR:
                osk.held_code = key->code;
                osk_send(key->code, osk_character(key), true);
                // A letter with accents: held for the kit's hold, its accents are shown (osk_tick). The capital the press typed decides
                // their case, so it is read before the Shift latch lets go.
                if (osk_accents_of(key->ch))
                {
                        osk.long_key = index;
                        osk.long_at = ui_kit.now + UI_HOLD_NS;
                        osk.long_capital = osk_character(key) != key->ch;
                }
                if (osk.shift == 1)
                {
                        osk.shift = 0;
                        osk_relayout();
                }
                break;
        case OSK_SHIFT:
                osk.held = -1;
                osk_shift_tap();
                osk_relayout();
                break;
        case OSK_PAGE:
                osk.held = -1;
                osk.shift = 0;
                osk.page = (int)key->alt;
                osk_relayout();
                break;
        case OSK_WORD:
                // Each character is a press and a release of its key, with no Shift (the latch is not the word's).
                osk.held = -1;
                for (int at = 0; key->word[at]; at++)
                {
                        unsigned int ch = (unsigned char)key->word[at], code = osk_code_of(ch);

                        if (!code)
                                continue;
                        windows_keyboard_send(&osk.own, (int)code, (int)ch, WINDOW_KEY_DOWN);
                        windows_keyboard_send(&osk.own, (int)code, (int)ch, 0);
                }
                break;
        case OSK_BACK:
        case OSK_ARROW:
                osk.held_code = key->code;
                osk_send(key->code, 0, true);
                osk.repeat = index;
                osk.next = ui_kit.now + OSK_HOLD_NS;
                break;
        default:
                osk.held_code = key->code;
                osk_send(key->code, key->kind == OSK_SPACE ? ' ' : 0, true);
                break;
        }
}

// The release of the key the press was on: its key goes up, its repeat ends, and a long press is over (its accents are shut by the
// caller, which has read the one lifted on first).
static void osk_release(void)
{
        if (osk.held >= 0)
                osk_send(osk.held_code, 0, false);
        osk.held = -1;
        osk.repeat = -1;
        osk.long_key = -1;
}

// The accents of a long press are shut: the row is not drawn, and nothing is hit under it.
static void osk_alts_close(void)
{
        if (osk.alt_key < 0)
                return;
        osk.alt_key = -1;
        osk.alt_count = 0;
        osk.dirty = true;
}

// A letter's accent picked on the row: the letter the press typed is taken back with a Backspace, and the accent is sent on the
// letter's own key (the same key, so the evdev code is the letter's and the character is the accent's).
static void osk_accent(int letter, unsigned int ch)
{
        const struct osk_key address_to key = &osk_page_keys[osk.page][letter / OSK_COLS][letter % OSK_COLS];

        osk_send(OSK_CODE_BACKSPACE, 0, true);
        osk_send(OSK_CODE_BACKSPACE, 0, false);
        osk_send(key->code, ch, true);
        osk_send(key->code, 0, false);
}

// The accent under a point of the row, or -1.
static int osk_alt_hit(int x, int y)
{
        for (int at = 0; at < osk.alt_count; at++)
                if (ui_control_hit(&osk.ui, &osk.alts[at], x, y) >= 0)
                        return at;
        return -1;
}

static int osk_put_number(char address_to into, int n);

// The trace's accents: "index x y w h word" a line each, in the file the trace's name with .alt on it, written when the row opens (the
// e2e lane picks an accent by it).
static void osk_alts_trace(void)
{
        char path[OSK_ALT_PATH_MOST], text[OSK_ALT_MOST * OSK_TRACE_LINE];
        const char *suffix = ".alt";
        int at = 0, length = 0;
        bipolar handle;

        if (!osk.trace)
                return;
        for (; osk.trace[at] && at < OSK_ALT_PATH_MOST - 5; at++)
                path[at] = osk.trace[at];
        for (int k = 0; suffix[k]; k++)
                path[at++] = suffix[k];
        path[at] = 0;
        for (int k = 0; k < osk.alt_count; k++)
        {
                const struct ui_control address_to c = &osk.alts[k];
                int values[5];

                values[0] = k;
                values[1] = osk.left + ui_dev(&osk.ui, c->x);
                values[2] = osk.top + ui_dev(&osk.ui, c->y);
                values[3] = ui_dev(&osk.ui, c->w);
                values[4] = ui_dev(&osk.ui, c->h);
                for (int v = 0; v < 5; v++)
                {
                        length += osk_put_number(text + length, values[v]);
                        text[length++] = ' ';
                }
                for (int w = 0; c->label[w] && w < OSK_TRACE_LINE - 40; w++)
                        text[length++] = c->label[w];
                text[length++] = '\n';
        }
        handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (handle >= 0)
        {
                system_write_once(handle, text, (positive)length);
                system_close(handle);
        }
}

// The accents of the letter held (its index): the row is laid out above the key, or over it on the top row, centred on it, kept on the
// keyboard, and its accents are the letter's (capitals when the press typed one). A letter with none, or a press no longer held, is no row.
static void osk_alts_open(void)
{
        struct ui address_to ui = &osk.ui;
        int letter = osk.long_key, count = 0, gap = OSK_GAP, row = letter / OSK_COLS;
        const struct osk_key address_to key;
        const unsigned int address_to accents;
        struct ui_control address_to k;
        int width, x0, y, total, most, at;

        osk.long_key = -1;
        if (letter < 0 || osk.held != letter)
                return;
        key = &osk_page_keys[osk.page][row][letter % OSK_COLS];
        accents = osk_accents_of(key->ch);
        if (!accents)
                return;
        for (unsigned int n = 1; n < sizeof(osk_accents[0]) / sizeof(osk_accents[0][0]) && accents[n] && count < OSK_ALT_MOST; n++)
                osk.alt_chars[count++] = osk.long_capital ? osk_upper(accents[n]) : accents[n];
        k = &osk.keys[letter];
        width = k->w, total = count * width + (count - 1) * gap;
        most = osk.width * 100 / ui->scale;
        x0 = k->x + k->w / 2 - total / 2;
        if (x0 < UI_PAD)
                x0 = UI_PAD;
        if (x0 + total > most - UI_PAD)
                x0 = most - UI_PAD - total;
        y = row > 0 ? k->y - k->h - gap : k->y;
        for (at = 0; at < count; at++)
        {
                struct ui_control address_to c = &osk.alts[at];
                p8 bytes[4];
                positive size;

                memory_zero(c, sizeof(*c));
                c->kind = UI_BUTTON;
                c->variant = UI_BUTTON_KEY;
                c->x = x0 + at * (width + gap), c->y = y, c->w = width, c->h = k->h;
                size = memory_utf8_encode(bytes, sizeof(bytes), osk.alt_chars[at]);
                for (positive b = 0; b < size && b < 4; b++)
                        osk.alt_words[at][b] = (char)bytes[b];
                osk.alt_words[at][size < 4 ? size : 4] = 0;
                c->label = osk.alt_words[at];
        }
        osk.alt_key = letter;
        osk.alt_count = count;
        osk.dirty = true;
        osk_alts_trace();
}

static void osk_repeat_key(void)
{
        const struct osk_key address_to key;

        if (osk.repeat < 0)
                return;
        key = &osk_page_keys[osk.page][osk.repeat / OSK_COLS][osk.repeat % OSK_COLS];
        osk_send(key->code, 0, true);
        osk.next = ui_kit.now + OSK_REPEAT_NS;
}

// The key under a point of the window, or -1.
static int osk_hit(int x, int y)
{
        for (int k = 0; k < OSK_ROWS * OSK_COLS; k++)
                if (osk.keys[k].kind && ui_control_hit(&osk.ui, &osk.keys[k], x, y) >= 0)
                        return k;
        return -1;
}

static b32 osk_event(const struct window_event address_to event)
{
        struct ui address_to ui = &osk.ui;

        // The keyboard has no keys of its own: a key the window gets is the kit's to ignore (the window never has them).
        if (event->type == WINDOW_EVENT_MOTION)
        {
                ui_kit_pointer(ui, osk.keys, OSK_ROWS * OSK_COLS, WINDOW_EVENT_MOTION, event->a, event->b, false, event->flags);
                osk.dirty = true;
        }
        else if (event->type == WINDOW_EVENT_BUTTON && event->a == UI_POINTER_LEFT)
        {
                b32 down = (event->flags & WINDOW_KEY_DOWN) != 0;

                ui_kit_pointer(ui, osk.keys, OSK_ROWS * OSK_COLS, WINDOW_EVENT_BUTTON, ui_kit.pointer_x, ui_kit.pointer_y, down, event->flags);
                if (down)
                {
                        int index = osk_hit(ui_kit.pointer_x, ui_kit.pointer_y);

                        if (index >= 0)
                        {
                                // A press while another key is still down (a second finger, a press whose release went nowhere) lets that
                                // key up first: osk_press would forget it, and the program would keep it down.
                                osk_release();
                                osk_press(index);
                        }
                }
                else
                {
                        // Lifted on an accent of the row: it is typed (osk_accent). Lifted anywhere else, nothing is sent but the key's up.
                        int letter = osk.alt_key, at = letter >= 0 ? osk_alt_hit(ui_kit.pointer_x, ui_kit.pointer_y) : -1;
                        unsigned int ch = at >= 0 ? osk.alt_chars[at] : 0;

                        osk_release();
                        osk_alts_close();
                        if (ch)
                                osk_accent(letter, ch);
                }
                osk.dirty = true;
        }
        return true;
}

static b32 osk_layout_take(void);
static b32 osk_purpose_take(void);
static b32 osk_screen_changed(void);
static void osk_metrics(void);

// The held key repeats while the keyboard is held down: a key's repeat is due at its time and the loop waits for it.
static long osk_tick(long timeout, long address_to other)
{
        b64 now = ui_kit.now, wait = 0;
        b32 layout, purpose, waiting = false;

        (void)other;
        // The layout or the focus's content purpose changed (the compositor woke the keyboard for it): the letters are built again,
        // the page is picked again, and the picture is due.
        layout = osk_layout_take();
        purpose = osk_purpose_take();
        if (layout || purpose)
        {
                osk.long_key = -1;
                osk_alts_close();
                osk_relayout();
                return 0;
        }
        // The output changed (a turn, a screen plugged in or out): the keyboard is sized for it, and placed, again.
        if (osk_screen_changed())
        {
                osk_metrics();
                return 0;
        }
        // A letter held for the kit's hold is a long press: its accents are shown (the picture is due). Until then the loop waits for it.
        if (osk.long_key >= 0)
        {
                if (now >= osk.long_at)
                        osk_alts_open();
                else
                        wait = osk.long_at - now, waiting = true;
        }
        if (osk.repeat >= 0)
        {
                if (now >= osk.next)
                        osk_repeat_key();
                if (!waiting || (osk.next > now ? osk.next - now : 0) < wait)
                        wait = osk.next > now ? osk.next - now : 0;
                waiting = true;
        }
        if (!waiting)
                return timeout;
        return timeout < 0 || (b64)timeout > wait ? (long)wait : timeout;
}

// The kit moved a control (a ripple, a hover): the picture is due.
static void osk_moving(int top, int bottom, b32 all)
{
        (void)top, (void)bottom, (void)all;
        osk.dirty = true;
}

static void osk_dock(b32 moved);

// The keyboard's height in device pixels for an output of this size: its share of the output's height, 40 percent when the output is
// wider than it is and 50 percent when it is not, and never more than half of it. The rows are at least the touch target (48 logical
// pixels), so a height too short for them is the least that gives them; a screen too short for that is still never over half.
static int osk_height_of(const struct ui address_to ui, int screen_width, int screen_height)
{
        int percent = screen_width > screen_height ? OSK_HEIGHT_LANDSCAPE : OSK_HEIGHT_PORTRAIT;
        int height = screen_height * percent / 100, most = screen_height / 2, least = ui_px(ui, OSK_HEIGHT_LEAST);

        if (height < least && least <= most)
                height = least;
        return height > most ? most : height;
}

// The output the keyboard is on has another size than the one it was sized for (osk_metrics).
static b32 osk_screen_changed(void)
{
        int x, y, width, height;

        windows_output(osk.own.table, 0, &x, &y, &width, &height);
        return width != osk.screen_width || height != osk.screen_height;
}

// The keyboard's size and place for the scale it is drawn at and the output it is on: its pixels (the width never wider than the output),
// its height (osk_height_of), the scratch rows its text is written through (ui_scratch_fit), its keys laid out again, and its place asked
// of the compositor (osk_dock), which moves it when it is up.
static void osk_metrics(void)
{
        b32 up = osk.shown >= 0;
        int x, y;

        windows_output(osk.own.table, 0, &x, &y, &osk.screen_width, &osk.screen_height);
        osk.width = ui_px(&osk.ui, OSK_WIDTH);
        if (osk.width > osk.screen_width)
                osk.width = osk.screen_width;
        osk.height = osk_height_of(&osk.ui, osk.screen_width, osk.screen_height);
        ui_scratch_fit(&osk.ui, (unsigned int)osk.width);
        osk_alts_close();
        osk_layout();
        osk_dock(up);
        osk.dirty = true;
}

// The keyboard layout the compositor keeps (the table's osk_layout): the letters are built for it when it is not the one they are.
// True when they changed; the caller lays the keys out again.
static b32 osk_layout_take(void)
{
        unsigned int name = __atomic_load_n(&osk.own.table->osk_layout, __ATOMIC_ACQUIRE);

        if (name == osk.layout)
                return false;
        osk.layout = name;
        osk_letters_build(name);
        return true;
}

// The symbols page the keyboard shows: the one of an email or a url field (mail, with @ and .com), or the plain one.
static void osk_symbols_use(b32 mail)
{
        if (mail)
        {
                for (int row = 0; row < OSK_ROWS; row++)
                        for (int k = 0; k < OSK_COLS; k++)
                                osk_symbols_mail[row][k] = osk_symbols[row][k];
                osk_symbols_mail[1][0] = (struct osk_key)OSK_C(3, '@', 0);
                osk_symbols_mail[1][9] = (struct osk_key)OSK_W(OSK_WORD, 3, 0, ".com"); // the last key of the row (its ten keys are 0 to 9)
                osk_page_keys[OSK_PAGE_SYMBOLS] = osk_symbols_mail;
        }
        else
                osk_page_keys[OSK_PAGE_SYMBOLS] = osk_symbols;
}

// The focus's content purpose (the table's osk_purpose), which picks the page: a number or digits open the numbers page, an email or a
// url the symbols page (with @ and .com), and every other purpose (a password included: the keyboard keeps nothing of what is typed, and
// it has no suggestions) the letters. True when it changed; the caller lays the keys out again.
static b32 osk_purpose_take(void)
{
        unsigned int purpose = __atomic_load_n(&osk.own.table->osk_purpose, __ATOMIC_ACQUIRE);
        b32 mail = purpose == OSK_PURPOSE_EMAIL || purpose == OSK_PURPOSE_URL;

        if (purpose == osk.purpose)
                return false;
        osk.purpose = purpose;
        osk_symbols_use(mail);
        osk.page = purpose == OSK_PURPOSE_DIGITS || purpose == OSK_PURPOSE_NUMBER ? OSK_PAGE_NUMBERS
                   : mail                                                          ? OSK_PAGE_SYMBOLS
                                                                                   : OSK_PAGE_LETTERS;
        osk.shift = 0;
        return true;
}

// The keyboard's place: centred across the primary screen, its bottom above the panel (the panel is its own strip). It is asked with
// the picture it is for (the first one takes it); a keyboard that is up is moved by a count of the placement (moved).
static void osk_dock(b32 moved)
{
        struct windows address_to table = osk.own.table;
        unsigned int row = osk.own.row;
        struct window_place place;
        int x, y, screen_width, screen_height;

        windows_output(table, 0, &x, &y, &screen_width, &screen_height);
        memory_zero(&place, sizeof(place));
        place.how = WINDOW_PLACE_AT | WINDOW_PLACE_SIZE;
        place.x = (screen_width - osk.width) / 2;
        place.y = screen_height - osk.height - ui_px(&osk.ui, COMPOSITOR_PANEL);
        place.width = (unsigned int)osk.width;
        place.height = (unsigned int)osk.height;
        windows_place_set(table, row, &place);
        osk.left = x + place.x, osk.top = y + place.y;
        if (moved)
                __atomic_add_fetch(&table->moved[row], 1u, __ATOMIC_RELEASE);
}

static fn osk_changed(void)
{
        osk_metrics();
}

// The language changed: the keys' words are the same (they are letters and the few words the keys say), so the picture is due.
static fn osk_language(void)
{
        osk_relayout();
}

// The trace: one line a key of the page shown, "page row column x y w h word", the rectangle in the screen's pixels as the window
// stands when the keyboard opens. A number is written with its sign and the rest with a space before it.
static int osk_put_number(char address_to into, int n)
{
        char digits[12];
        int at = 0, length = 0;

        if (n < 0)
                into[at++] = '-', n = -n;
        do
                digits[length++] = (char)('0' + n % 10), n /= 10;
        while (n);
        while (length)
                into[at++] = digits[--length];
        return at;
}

static void osk_trace(void)
{
        int x = osk.left, y = osk.top, length = 0, shown = osk.page;
        bipolar handle;

        if (!osk.trace)
                return;
        // Every page is laid out in turn for the trace, and the page shown is laid out again after it. The symbols page of an email or
        // a url field is written after them as page OSK_PAGES (the plain one has no @ and no .com).
        for (int variant = 0; variant < 2; variant++)
        {
                int first = variant ? OSK_PAGE_SYMBOLS : 0, last = variant ? OSK_PAGE_SYMBOLS + 1 : OSK_PAGES;

                osk_symbols_use(variant);
                for (int page = first; page < last; page++)
                {
                        int number = variant ? OSK_PAGES : page;

                        osk.page = page;
                        osk_layout();
                        for (int row = 0; row < OSK_ROWS; row++)
                                for (int k = 0; k < OSK_COLS; k++)
                                {
                                        const struct ui_control address_to c = &osk.keys[row * OSK_COLS + k];
                                        int values[7] = {number, row, k, x + ui_dev(&osk.ui, c->x), y + ui_dev(&osk.ui, c->y), ui_dev(&osk.ui, c->w), ui_dev(&osk.ui, c->h)};

                                        if (!c->kind || length + OSK_TRACE_LINE > OSK_TRACE_MOST)
                                                continue;
                                        for (int v = 0; v < 7; v++)
                                        {
                                                length += osk_put_number(osk.trace_text + length, values[v]);
                                                osk.trace_text[length++] = ' ';
                                        }
                                        for (int w = 0; c->label[w] && w < OSK_TRACE_LINE - 40; w++)
                                                osk.trace_text[length++] = c->label[w];
                                        osk.trace_text[length++] = '\n';
                                }
                }
        }
        osk_symbols_use(osk.purpose == OSK_PURPOSE_EMAIL || osk.purpose == OSK_PURPOSE_URL);
        osk.page = shown;
        osk_layout();
        handle = system_open_at_mode(AT_FDCWD, (string_address)osk.trace, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (handle >= 0)
        {
                system_write_once(handle, osk.trace_text, (positive)length);
                system_close(handle);
        }
}

static int osk_open(void)
{
        // A second keyboard lifts the one up (it is none of a person's keys): its window goes on as it is, and this one stops.
        if (ui_lift_other(&osk.own, (string_address) "osk", false))
                return 0;
        osk_layout_take();
        osk_purpose_take();
        osk_metrics();
        osk_frame();
        osk_trace();
        return -1;
}

// The keyboard's size is its own: the hand does not resize it.
static fn osk_size(unsigned int width, unsigned int height)
{
        (void)width, (void)height;
        osk_metrics();
}

static struct ui_app osk_app = {
        .own = &osk.own,
        .ui = &osk.ui,
        .missing = (string_address) "osk: no window: the compositor's desktop is not up\n",
        .title = (string_address) "Keyboard",
        .name = (string_address) "osk",
        .glass = WINDOW_NO_KEYS | WINDOW_NO_TAB | WINDOW_NO_CLOSE | WINDOW_NO_GRAB | WINDOW_KEYBOARD, // never the keys, never a tab, no frame
        .nofont = (string_address) "osk: no font to draw with (-f FONT.ttf), pid ",
        .dirty = &osk.dirty,
        .focused = &osk.focused,
        .ground = &osk.ground,
        .open = osk_open,
        .frame = osk_frame,
        .language = osk_language,
        .event = osk_event,
        .changed = osk_changed,
        .size = osk_size,
        .tick = osk_tick,
        .moving = osk_moving,
};

static b32 osk_main()
{
        string_address regular = (string_address) UI_FONT_REGULAR;

        memory_zero(&osk, sizeof(osk));
        osk.layout = ~0u; // no layout yet: the first take builds the letters
        osk.purpose = ~0u; // no purpose yet: the first take picks the page
        osk.shown = -1;
        osk.held = -1;
        osk.repeat = -1;
        osk.page = OSK_PAGE_LETTERS;
        for (positive at = 1; at < (positive)program_argument_count(); at++)
        {
                string_address value;

                if ((value = ui_argument(at, (string_address) "-f")))
                        regular = value;
                else if ((value = ui_argument(at, (string_address) "-t")))
                        osk.trace = (const char address_to)value;
                else
                        continue;
                at++;
        }
        osk_app.font = regular;
        return ui_app_run(&osk_app);
}

#endif // MOONWATER_CANVAS_OSK
