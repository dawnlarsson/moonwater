/*
        The kernel log, as a window on the compositor in userspace.

        `klog` is the terminal (ui/terminal.c) with a far end that is the kernel's
        ring instead of a shell: the same cells, the same text engine, the same
        bar and wheel, and a window that is sized as the terminal's is. It
        opens /dev/kmsg without waiting and reads it from the first record the
        ring still holds, so the messages of the boot are there, and then waits
        for the next with the compositor's events in one poll: a log nothing is
        written to wakes nothing, and one written to is drawn at most once an
        output frame.

        What it keeps is the lines of a ring of TERMINAL_LOG_HISTORY of them
        (about the kernel's own 128 KiB of text), and what it draws is what a
        record says: the time it was made, as the kernel prints it with
        printk.time, and the message. A record's continuation lines (the
        subsystem and the device) are not shown. The message is attacker input,
        as everything printk is given is: a byte that is a control byte is shown
        as a question mark, so nothing a driver was handed can move the cursor,
        turn on a mode or make the emulator answer into a log that cannot be
        written to.

        Keys scroll it: Page Up and Page Down by a screen, the arrows by a
        line, Home to the oldest line and End back to the newest. The wheel and
        the bar are the terminal's.

            klog [-s PIXELS] [-f FONT.ttf]... [-b BOLD.ttf] [-w FILE]

        -w names a file made once the first picture is committed, which is how
        the machine script knows the log is on the screen before it starts the
        terminal in front of it.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_KLOG
#define MOONWATER_CANVAS_KLOG

// A record is at most what the kernel's buffer reads out for one message, escapes and all; a read of less is EINVAL.
#define KLOG_RECORD 8192
#define KLOG_KEY_HOME 102
#define KLOG_KEY_UP 103
#define KLOG_KEY_PAGE_UP 104
#define KLOG_KEY_END 107
#define KLOG_KEY_DOWN 108
#define KLOG_KEY_PAGE_DOWN 109

// A number in decimal, padded on the left with spaces or zeros to at least width digits.
static positive klog_number(p8 address_to into, positive value, positive width, p8 pad)
{
        p8 digits[20];
        positive count = 0, at = 0;

        do
                digits[count++] = (p8)('0' + value % 10);
        while ((value /= 10) && count < sizeof(digits));

        for (; width > count; width--)
                into[at++] = pad;
        while (count)
                into[at++] = digits[--count];
        return at;
}

/*
        One record, "priority,sequence,microseconds,flags;message\n", with any continuation lines after it, as the
        line the window shows. Answers how many bytes of the line are made, none for a record that is not one.
*/
static positive klog_line(const p8 address_to record, positive length, p8 address_to line)
{
        positive at = 0, seconds, text = 0, made = 0, fields = 0;
        positive stamp = 0;

        // The time is the third field, before the semicolon that starts the message.
        while (at < length && record[at] != ';')
        {
                if (record[at] == ',')
                        fields++;
                else if (fields == 2 && record[at] >= '0' && record[at] <= '9')
                        stamp = stamp * 10 + (positive)(record[at] - '0');
                at++;
        }

        if (at >= length)
                return 0;
        text = at + 1;

        seconds = stamp / 1000000;
        line[made++] = '[';
        made += klog_number(line + made, seconds, 5, ' ');
        line[made++] = '.';
        made += klog_number(line + made, stamp % 1000000, 6, '0');
        line[made++] = ']';
        line[made++] = ' ';

        for (at = text; at < length && record[at] != '\n'; at++)
        {
                p8 byte = record[at];

                // A control byte, and the two-byte form of a C1 control, which a terminal takes for one.
                if (byte < ' ' || byte == 127 || (byte == 0xc2 && at + 1 < length && (record[at + 1] & 0xe0) == 0x80))
                {
                        byte = '?';
                        at += record[at] == 0xc2;
                }

                line[made++] = byte;
        }

        line[made++] = '\r';
        line[made++] = '\n';
        return made;
}

/*
        Everything the kernel has said that has not been read, up to a budget. A reader the ring overran is told so
        (EPIPE) and is given the oldest record there is on the next read, which is what is wanted, so it is not an end.
        Nothing waiting is EAGAIN and only EAGAIN; anything else is the log gone.
*/
static b32 klog_drain(b32 far, b32 address_to gone, positive budget)
{
        p8 record[KLOG_RECORD], line[KLOG_RECORD + 32];
        b32 changed = false;

        for (;;)
        {
                bipolar got = system_read_once(far, record, sizeof(record));
                positive made;

                if (got == -EPIPE)
                        continue;

                if (got > 0)
                {
                        made = klog_line(record, (positive)got, line);
                        if (made)
                        {
                                term_bytes(line, made);
                                changed = true;
                        }
                        budget = budget > (positive)got ? budget - (positive)got : 0;
                        if (!budget)
                                return changed;
                        continue;
                }

                if (got != -EAGAIN && got != -EINTR)
                        address_to gone = true;

                return changed;
        }
}

/*
        One event from the compositor. The keys scroll; the rest are the terminal's (the bar and the wheel), which
        write nothing to a far end that is a log.
*/
static b32 klog_receive(const struct window_event address_to event, b32 address_to dirty)
{
        int lines = 0;

        if (event->type != WINDOW_EVENT_KEY)
                return terminal_receive(-1, event, dirty);

        if (!(event->flags & WINDOW_KEY_DOWN))
                return true;

        switch (event->a)
        {
        case KLOG_KEY_UP:
                lines = 1;
                break;
        case KLOG_KEY_DOWN:
                lines = -1;
                break;
        case KLOG_KEY_PAGE_UP:
                lines = (int)ROWS - 1;
                break;
        case KLOG_KEY_PAGE_DOWN:
                lines = 1 - (int)ROWS;
                break;
        case KLOG_KEY_HOME:
                lines = 1 << 30;
                break;
        case KLOG_KEY_END:
                if (terminal_view_live())
                        address_to dirty = true;
                return true;
        default:
                return true;
        }

        if (terminal_view_scroll(lines))
                address_to dirty = true;
        return true;
}

static b32 klog_main()
{
        return terminal_run(true);
}

#endif // MOONWATER_CANVAS_KLOG
