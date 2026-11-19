/*
        The card and its screens: the displays file's lines (a screen's scale, rate, turn and place), the connectors and
        their modes and names, the space the screens are laid in and its scanout and second plate, a turned screen's own
        buffer, the screens taken at the start and again (compositor_space_take), the card found and opened, and the
        pointer's plane.

        The pointer's plane (compositor_sway_*, compositor_pointer_dynamic, _now and _press, compositor_cursor_* and
        compositor_drawn_x and _y) is here because it is set up with the card and moved across its crtcs, but it is
        the pointer's: what it shows is pointer.c's and where the hand is, input.c's. What a window does when the space
        changes is manage.c's (compositor_space_carry asks it), the rules of the space are outputs.c's, and the uevent
        that says a screen came is read in compositor.c's loop. It needs the compositor's state and the kernel's
        interfaces, outputs.c and pointer.c, and scanout.c and present.c, since a take drops what was shown and presents
        the new space at once; what it asks of session.c, manage.c, input.c and compositor.c's tail is declared before
        the call. Called by compositor.c's startup and loop (the card, the take, the pointer's tick), damage.c and
        manage.c (a screen's scale), input.c and session.c (the pointer moved) and wayland_host.c.
*/

/*
        The card, and every screen it has connected, at the mode Canvas left
        on each.

        Taking Canvas's own modes is what keeps the switch a plane change and
        not a modeset, and what keeps every coordinate the same on both sides
        of it. A connector with no crtc driving it takes a free crtc it can
        have and its first mode (the kernel lists the preferred one first).

        One scanout is the whole space the screens make (compositor.width by
        compositor.height), and each screen's crtc shows its own rectangle of
        it: the scene is composed once across them, a window that straddles
        two is in both, and one DIRTYFB a frame is one commit that takes in
        every screen showing what changed, so no screen waits on another's
        vertical blank in turn. The rows of the space no screen shows (under a
        shorter screen) are composed with the rest and seen by none.
*/
static void compositor_kmsg(string_address text, positive value, string_address tail);
static void compositor_text(const char *path, char *text, unsigned int room);
static int compositor_turn_buffer(unsigned int at, unsigned int width, unsigned int height, struct compositor_turn *retired);
static void compositor_turn_retire(const struct compositor_turn *retired);
static void compositor_cursor_move(void);
static void compositor_arrange_on(int row, unsigned int how, int on);
static void compositor_place(int row, int x, int y);
static void compositor_room(const int *screen, int *room);
static void compositor_arrange_rect(int row, unsigned int how, const int *rect);
static void compositor_room_follow(void);
static void compositor_vrr_forget(void);
static int compositor_virtual(void);

// The displays file as the compositor has it: its lines, read when the screens are taken (compositor_displays_read),
// and how many.
static struct windows_display compositor_display[WINDOWS_DISPLAYS];
static unsigned int compositor_displays;

// Appends text to line at at, bounded; answers where it ends.
static unsigned int compositor_said(char *line, unsigned int at, const char *text)
{
        for (unsigned int each = 0; text[each] && at < 127; each++)
                line[at++] = text[each];
        return at;
}

/*
        The mode a screen is driven at for the rate its displays line asks for: the first mode it lists at the size it
        has now and that rate (windows_mode_at), in mode. A rate no mode of the screen has keeps the mode it has, and
        the kernel log says so. A preferred rate, which a screen with no line has too, is the fastest mode it lists at that
        size, the first of those (compositor_fastest_at; or the one the kernel's command line forced there, video=), which a
        screen at another rate than it goes back to: the mode it has is the one it was switched to, so keeping that was no
        switch back, and the settings' first rate, the recommended one, never took after another. The settings write that
        rate as no line at all once nothing else of the screen is asked (a line of the defaults is not kept), so a screen
        with none is not a screen whose mode is its own to keep. The fastest and not the first: the kernel lists the mode
        the monitor prefers first, and a 144 Hz monitor's is often its 60 Hz one, which Canvas had passed over for the 144
        at the start and the compositor then drove the screen at, a frame every 16.7 ms. An interlaced or doubled mode
        is no rate of the screen's (its refresh is its fields').
*/
#define COMPOSITOR_MODE_INTERLACED 48u // DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN

/*
        A mode's refresh period in nanoseconds, as its timings make it: htotal by vtotal pixels at clock kHz (twice the fields of
        an interlaced mode, half of a doubled one and of one scanned vscan times), rounded to the nearest; 1e9 / vrefresh when the
        timings are not there. The vrefresh the kernel gives is a whole number of Hz, and its period was every frame's and every
        presentation feedback's refresh: box's 144 Hz mode measures 6957 us a vblank, the whole number said 6944, and a client
        that steps from presented + refresh was 13 us a frame early, a frame of drift every 530 frames.
*/
static positive compositor_mode_period(const struct compositor_mode *mode, positive refresh)
{
        positive pixels = (positive)mode->htotal * (positive)mode->vtotal * 1000000ull, clock = mode->clock;

        if (mode->flags & 16u)
                clock *= 2;
        if (mode->flags & 32u)
                pixels *= 2;
        if (mode->vscan > 1)
                pixels *= mode->vscan;
        if (!clock || !pixels || pixels / clock < 1000000ull || pixels / clock > 1000000000ull / 24)
                return 1000000000ull / refresh;
        return (pixels + clock / 2) / clock;
}

static unsigned int compositor_fastest_at(const struct compositor_mode *modes, unsigned int count, unsigned int width,
                                          unsigned int height)
{
        unsigned int best = count;

        for (unsigned int at = 0; at < count; at++)
                if (modes[at].hdisplay == width && modes[at].vdisplay == height && !(modes[at].flags & COMPOSITOR_MODE_INTERLACED) &&
                    (best == count || modes[at].vrefresh > modes[best].vrefresh))
                        best = at;
        return best;
}

static void compositor_refresh_pick(const char *name, unsigned int count, const struct compositor_mode *modes,
                                    struct compositor_mode *mode)
{
        const struct windows_display *display = windows_display_of(compositor_display, compositor_displays, name,
                                                                   windows_name_length(name));
        unsigned int rates[128][3], at, asked = display ? display->refresh : 0;
        char line[160];
        unsigned int length;

        for (at = 0; at < count && at < 128; at++)
                rates[at][0] = modes[at].hdisplay, rates[at][1] = modes[at].vdisplay, rates[at][2] = modes[at].vrefresh;
        at = asked ? windows_mode_at(rates, count < 128 ? count : 128, mode->hdisplay, mode->vdisplay, asked)
                   : compositor_fastest_at(modes, count < 128 ? count : 128, mode->hdisplay, mode->vdisplay);
        // No rate asked: the mode the command line forced at this size, if one was, is the screen's own.
        for (unsigned int forced = 0; !asked && forced < count && forced < 128; forced++)
                if ((modes[forced].type & COMPOSITOR_MODE_USERDEF) && rates[forced][0] == mode->hdisplay &&
                    rates[forced][1] == mode->vdisplay)
                {
                        at = forced;
                        break;
                }
        if (at < count && at < 128)
        {
                *mode = modes[at];
                return;
        }
        if (!asked)
                return;
        length = compositor_said(line, 0, "compositor: canvas.displays: no mode of ");
        length = compositor_said(line, length, name);
        length = compositor_said(line, length, " at ");
        length += positive_into_string((p8 *)line + length, display->refresh);
        length = compositor_said(line, length, " Hz, kept the mode it has");
        line[length] = 0;
        compositor_kmsg((string_address)line, 0, (string_address) "");
}

// Says on the kernel log that a screen is turned degrees, by the card's rotation (hardware) or by Canvas's (software).
static void compositor_turn_say(const char *name, unsigned int degrees, int hardware)
{
        char line[160];
        unsigned int length = compositor_said(line, 0, "compositor: canvas.displays: ");

        length = compositor_said(line, length, name);
        length = compositor_said(line, length, " turned ");
        length += positive_into_string((p8 *)line + length, degrees);
        length = compositor_said(line, length, hardware ? " degrees by hardware rotation" : " degrees by software rotation");
        line[length] = 0;
        compositor_kmsg((string_address)line, 0, (string_address) "");
}

// The degrees a screen is turned by its displays line (0 when it has none): the screen is laid out at its turned size.
// The percent a screen's displays line draws it at: the program's boot scale (-s, else 100) when it has no line or none for it.
static int compositor_display_scale(const char *name)
{
        const struct windows_display *display = windows_display_of(compositor_display, compositor_displays, name,
                                                                   windows_name_length(name));

        return display && display->scale ? (int)display->scale : compositor.boot_scale ? compositor.boot_scale : WINDOWS_SCALE_DEFAULT;
}

// The percent screen at is drawn at (the desktop's when there is no such screen).
static int compositor_output_scale(int at)
{
        return at >= 0 && at < (int)compositor.outputs ? compositor.output[at].scale : compositor.scale;
}

// The percent a window is drawn at: the scale of the screen it is mostly on, which is what a window straddling a seam takes.
static int compositor_row_scale(int row)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);

        return compositor_output_scale(compositor_screen_owner(screens, count, compositor.x[row], compositor.y[row],
                                                              (int)compositor.w[row], (int)compositor.h[row]));
}

static unsigned int compositor_display_degrees(const char *name)
{
        const struct windows_display *display = windows_display_of(compositor_display, compositor_displays, name,
                                                                   windows_name_length(name));

        return display ? display->rotation : 0;
}

/*
        What the card has connected: each connector that says it is connected
        and lists a mode, in the card's order, with its name, the crtc that
        drives it now and the mode that crtc is at (Canvas's: current), or the
        crtcs it could have and its first mode. GETCONNECTOR, asked by the
        card's master, probes the connector again, which is what a hotplug
        is to be answered by.
*/
/*
        The card's connectors are as many as it says (a dock's MST ports add theirs), read into a list that grows (compositor_connector_ids,
        a doubling at a time); its CRTCs are WINDOWS_OUTPUTS at most, the kernel's own bound. Where a card of more than 16 of either had no
        screen at all.
*/
static unsigned int *compositor_connector_ids;
static long compositor_connector_room;

// Room for want connector ids: the list, or 0 when the memory is not there.
static unsigned int *compositor_connectors_room(long want)
{
        long room = compositor_connector_room ? compositor_connector_room : 64, got;

        if (want <= compositor_connector_room)
                return compositor_connector_ids;
        while (room < want)
                room *= 2;
        got = compositor_call(syscall(mmap), 0, (positive)(room * (long)sizeof(unsigned int)), 3, 0x22, (positive)-1, 0);
        if (got < 0 && got > -4096)
                return 0;
        if (compositor_connector_ids)
                compositor_call(syscall(munmap), (positive)compositor_connector_ids, (positive)(compositor_connector_room * (long)sizeof(unsigned int)), 0, 0, 0, 0);
        compositor_connector_ids = (unsigned int *)got, compositor_connector_room = room;
        return compositor_connector_ids;
}

// The modes of a connector, read into a list that grows as the connectors' list does (compositor_modes_room): a screen may list more than 128.
static struct compositor_mode *compositor_modes_list;
static long compositor_modes_count;

// Room for want modes: the list, or 0 when the memory is not there.
static struct compositor_mode *compositor_modes_room(long want)
{
        long room = compositor_modes_count ? compositor_modes_count : 128, got;

        if (want <= compositor_modes_count)
                return compositor_modes_list;
        while (room < want)
                room *= 2;
        got = compositor_call(syscall(mmap), 0, (positive)(room * (long)sizeof(struct compositor_mode)), 3, 0x22, (positive)-1, 0);
        if (got < 0 && got > -4096)
                return 0;
        if (compositor_modes_list)
                compositor_call(syscall(munmap), (positive)compositor_modes_list, (positive)(compositor_modes_count * (long)sizeof(struct compositor_mode)), 0, 0, 0, 0);
        compositor_modes_list = (struct compositor_mode *)got, compositor_modes_count = room;
        return compositor_modes_list;
}

#define COMPOSITOR_MODE_PREFERRED 8u // DRM_MODE_TYPE_PREFERRED

struct compositor_connected
{
        unsigned int count, crtc_count, max_width, max_height;
        unsigned int crtcs[WINDOWS_OUTPUTS];
        unsigned int connector[WINDOWS_OUTPUTS], crtc[WINDOWS_OUTPUTS], possible[WINDOWS_OUTPUTS];
        int current[WINDOWS_OUTPUTS];
        struct compositor_mode mode[WINDOWS_OUTPUTS];
        char name[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_NAME], model[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_MODEL];
        int size[WINDOWS_OUTPUTS][2];
        unsigned int hz[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_HZ], hz_count[WINDOWS_OUTPUTS];
        // A screen taken at the size Canvas's word asks (compositor_modes_pick) and not at the one its crtc is at: what the crtc
        // is at, its size and rates, which the take goes back to when the card refuses the other (compositor_space_take).
        int over[WINDOWS_OUTPUTS];
        struct compositor_mode held[WINDOWS_OUTPUTS];
        int held_size[WINDOWS_OUTPUTS][2];
        unsigned int held_hz[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_HZ], held_hz_count[WINDOWS_OUTPUTS];
        // Every size it lists, each once, the most pixels first, and which one it marks preferred (compositor_sizes_take).
        unsigned short sizes[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_SIZES][2];
        unsigned int size_count[WINDOWS_OUTPUTS], size_preferred[WINDOWS_OUTPUTS];
};

/*
        The sizes screen n lists, for the Displays page's Resolution: each width by height once (an interlaced or doubled mode's
        none), the most pixels first and, of the same pixels, the wider first, at most WINDOWS_OUTPUT_SIZES of them (a list longer
        keeps the largest). size_preferred is the one of the first mode marked preferred, or size_count when none is.
*/
static void compositor_sizes_take(struct compositor_connected *found, unsigned int n, const struct compositor_mode *modes,
                                  unsigned int count)
{
        unsigned short (*sizes)[2] = found->sizes[n];
        unsigned int had = 0, preferred_w = 0, preferred_h = 0;

        for (unsigned int each = 0; each < count; each++)
        {
                unsigned int w = modes[each].hdisplay, h = modes[each].vdisplay, at;
                unsigned long pixels = (unsigned long)w * h;

                if ((modes[each].flags & COMPOSITOR_MODE_INTERLACED) || !w || !h || w > 0xffff || h > 0xffff)
                        continue;
                if (!preferred_w && (modes[each].type & COMPOSITOR_MODE_PREFERRED))
                        preferred_w = w, preferred_h = h;
                for (at = 0; at < had && !(sizes[at][0] == w && sizes[at][1] == h); at++)
                        ;
                if (at < had)
                        continue;
                for (at = had; at > 0 && ((unsigned long)sizes[at - 1][0] * sizes[at - 1][1] < pixels ||
                                          ((unsigned long)sizes[at - 1][0] * sizes[at - 1][1] == pixels && sizes[at - 1][0] < w));
                     at--)
                        if (at < WINDOWS_OUTPUT_SIZES)
                                sizes[at][0] = sizes[at - 1][0], sizes[at][1] = sizes[at - 1][1];
                if (at >= WINDOWS_OUTPUT_SIZES)
                        continue;
                sizes[at][0] = (unsigned short)w, sizes[at][1] = (unsigned short)h;
                had += had < WINDOWS_OUTPUT_SIZES;
        }
        found->size_count[n] = had;
        found->size_preferred[n] = had;
        for (unsigned int at = 0; at < had; at++)
                if (sizes[at][0] == preferred_w && sizes[at][1] == preferred_h)
                        found->size_preferred[n] = at;
}

/*
        Which mode a real screen is driven at: Canvas's word for it (moonwater canvas modes, the kernel's canvas.modes), the largest
        it lists or the one it marks preferred, which the compositor took no notice of. It took the mode the crtc was at whenever a
        crtc drove the screen, so the size it started at was the size for the session: on snug-asteroid (a Dell all-in-one, UHD 630)
        Canvas's first picture was the no-EDID 1024x768 of a connector i915 had not looked at yet, the compositor took the panel's
        crtc at that, and the 1920x1080 the panel's EDID listed a moment later was never taken; and a word changed with the
        compositor up changed nothing on the screen, since Canvas does not commit while the compositor is the card's master. Now
        the mode a screen lists that the word asks is taken, the crtc's kept when it is that size. A mode the kernel's command line
        forced (video=, USERDEF) is the person's and comes first, as Canvas has it; a guest's card keeps the crtc's (Canvas drives a
        guest at seventy percent of its host, a size of its own). The word is read from the kernel's parameter, and from the kept
        file when Canvas is not there; largest, Canvas's default, when neither says.
*/
#define COMPOSITOR_MODES_WORD "/sys/module/canvas/parameters/modes"
#define COMPOSITOR_MODES_KEPT "/root/canvas.modes"

static int compositor_modes_largest(void)
{
        char word[16];

        compositor_text(COMPOSITOR_MODES_WORD, word, sizeof(word));
        if (!word[0])
                compositor_text(COMPOSITOR_MODES_KEPT, word, sizeof(word));
        return string_compare_max((string_address)word, (string_address) "preferred", 9) != 0;
}

// The mode of count that the word asks (USERDEF first, then the largest, or the first marked preferred): its index, or count.
static unsigned int compositor_modes_pick(const struct compositor_mode *modes, unsigned int count, int largest)
{
        unsigned int best = count;

        for (unsigned int at = 0; at < count; at++)
                if (modes[at].type & COMPOSITOR_MODE_USERDEF)
                        return at;
        for (unsigned int at = 0; at < count; at++)
        {
                unsigned long pixels = (unsigned long)modes[at].hdisplay * modes[at].vdisplay, most;

                if ((modes[at].flags & COMPOSITOR_MODE_INTERLACED) || !pixels)
                        continue;
                if (!largest)
                {
                        if (modes[at].type & COMPOSITOR_MODE_PREFERRED)
                                return at;
                        continue;
                }
                most = best < count ? (unsigned long)modes[best].hdisplay * modes[best].vdisplay : 0;
                if (best == count || pixels > most || (pixels == most && modes[at].vrefresh > modes[best].vrefresh))
                        best = at;
        }
        return best;
}

/*
        Sizes the card refused a screen at, by connector, which the next takes do not ask again of it: the take goes back to the
        crtc's mode when the card refuses the word's (compositor_space_take), and the look every few seconds would ask the same
        refused size for ever. Forgotten when the word or the displays file is written (compositor_modes_forget).
*/
#define COMPOSITOR_REFUSED 8
static unsigned int compositor_refused[COMPOSITOR_REFUSED][3];

static void compositor_modes_forget(void)
{
        memory_zero(compositor_refused, sizeof(compositor_refused));
}

static int compositor_mode_refused(unsigned int connector, const struct compositor_mode *mode)
{
        for (unsigned int at = 0; at < COMPOSITOR_REFUSED; at++)
                if (compositor_refused[at][0] == connector && compositor_refused[at][1] == (unsigned int)mode->hdisplay &&
                    compositor_refused[at][2] == (unsigned int)mode->vdisplay)
                        return 1;
        return 0;
}

static void compositor_mode_refuse(unsigned int connector, const struct compositor_mode *mode)
{
        unsigned int at = 0;

        while (at < COMPOSITOR_REFUSED - 1 && compositor_refused[at][0] && compositor_refused[at][0] != connector)
                at++;
        compositor_refused[at][0] = connector;
        compositor_refused[at][1] = (unsigned int)mode->hdisplay;
        compositor_refused[at][2] = (unsigned int)mode->vdisplay;
}

// Adds a mode's refresh rate to a screen's list when the mode is the screen's size and the list has no such rate yet:
// the rates a screen lists at its size, which the Displays field offers, the fastest first (the preferred rate the
// settings recommend, compositor_refresh_pick's), an interlaced mode's none.
static void compositor_rate_add(unsigned int *hz, unsigned int *count, const struct compositor_mode *mode, int width,
                                int height)
{
        unsigned int at;

        if (mode->hdisplay != width || mode->vdisplay != height || !mode->vrefresh || *count >= WINDOWS_OUTPUT_HZ ||
            (mode->flags & COMPOSITOR_MODE_INTERLACED))
                return;
        for (at = 0; at < *count; at++)
                if (hz[at] == mode->vrefresh)
                        return;
        for (at = *count; at > 0 && hz[at - 1] < mode->vrefresh; at--)
                hz[at] = hz[at - 1];
        hz[at] = mode->vrefresh;
        (*count)++;
}

#define COMPOSITOR_OBJECT_CONNECTOR 0xc0c0c0c0u // DRM_MODE_OBJECT_CONNECTOR
#define COMPOSITOR_GETPROPBLOB COMPOSITOR_DRM(0xacu, 16u)
// An EDID is a block of 128 bytes and its extensions, 128 each, 255 of them at the most.
#define COMPOSITOR_EDID_BLOCK 128u
#define COMPOSITOR_EDID_MOST (COMPOSITOR_EDID_BLOCK * 256u)

/*
        The monitor's name from an EDID block of length bytes: the descriptor (18 bytes, at 54, 72, 90 or 108) tagged 0xFC,
        its text of up to 13 bytes ended by a newline and its blanks at the end dropped. A byte that is not a printable
        character is a '?'. Answers the length written into name (room bytes, with a null after it); 0, and the empty name,
        when the block is not an EDID or has no such descriptor.
*/
static unsigned int compositor_edid_name(const unsigned char *edid, unsigned int length, char *name, unsigned int room)
{
        static const unsigned char header[8] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
        unsigned int count = 0;

        if (room)
                name[0] = 0;
        if (length < 128 || !room || memory_compare(edid, header, sizeof(header)))
                return 0;
        for (unsigned int at = 54; at <= 108; at += 18)
        {
                if (edid[at] || edid[at + 1] || edid[at + 2] || edid[at + 3] != 0xfc)
                        continue;
                for (unsigned int each = 0; each < 13 && count + 1 < room; each++)
                {
                        unsigned char c = edid[at + 5 + each];

                        if (c == 0x0a)
                                break;
                        name[count++] = c >= 0x20 && c < 0x7f ? (char)c : '?';
                }
                while (count && name[count - 1] == ' ')
                        count--;
                name[count] = 0;
                return count;
        }
        return 0;
}

// The monitor's name, from the EDID blob the connector's EDID property names: the connector's properties are listed and the one
// called EDID is read. Empty when the connector has no EDID (a virtual screen may have none), or the card will not say. Answers
// whether the connector has an EDID blob at all (a monitor's EDID need not name it).
static int compositor_connector_model(unsigned int connector, char *name, unsigned int room)
{
        unsigned int ids[64];
        unsigned long values[64];
        struct
        {
                unsigned long props, values;
                unsigned int count, object, type;
        } got = {(unsigned long)ids, (unsigned long)values, 64, connector, COMPOSITOR_OBJECT_CONNECTOR};
        static unsigned char edid[COMPOSITOR_EDID_MOST]; // 32 KiB: off the stack

        if (room)
                name[0] = 0;
        if (system_control(compositor.card, COMPOSITOR_OBJ_GETPROPERTIES, address_of got) != 0 || got.count > 64)
                return 0;
        for (unsigned int at = 0; at < got.count; at++)
        {
                struct
                {
                        unsigned long values, enums;
                        unsigned int id, flags;
                        char name[32];
                        unsigned int count_values, count_enums;
                } property = {0, 0, ids[at], 0, {0}, 0, 0};
                struct
                {
                        unsigned int blob_id, length;
                        unsigned long data;
                } blob;

                if (system_control(compositor.card, COMPOSITOR_GETPROPERTY, address_of property) != 0 ||
                    !string_equals((string_address)property.name, (string_address) "EDID"))
                        continue;
                if (!values[at])
                        return 0;
                /*
                        The kernel copies a blob only when the length asked is the blob's own (drm_mode_getblob_ioctl), so a
                        length of 256 read only a monitor whose EDID is one extension long: its length first, then all of
                        it, when it is whole blocks and no more than a base block and 255 extensions.
                */
                blob.blob_id = (unsigned int)values[at], blob.length = 0, blob.data = 0;
                if (system_control(compositor.card, COMPOSITOR_GETPROPBLOB, address_of blob) != 0 || !blob.length ||
                    blob.length % COMPOSITOR_EDID_BLOCK || blob.length > COMPOSITOR_EDID_MOST)
                        return 1;
                {
                        unsigned int length = blob.length;

                        memory_zero(edid, length);
                        blob.data = (unsigned long)edid;
                        if (system_control(compositor.card, COMPOSITOR_GETPROPBLOB, address_of blob) == 0 && blob.length == length)
                                compositor_edid_name(edid, length, name, room);
                }
                return 1;
        }
        return 0;
}

/*
        The rates screen n lists at the size its panel has: the size of the mode it is at, which is not the size it is laid
        out at when it is turned a quarter (its width and height the other way round: no mode has that), each rate once, the
        fastest first, which is the order the settings list them in.
*/
static void compositor_rates_take(struct compositor_connected *found, unsigned int n, const struct compositor_mode *modes,
                                  unsigned int count)
{
        for (unsigned int each = 0; each < count; each++)
                compositor_rate_add(found->hz[n], &found->hz_count[n], &modes[each], found->mode[n].hdisplay,
                                    found->mode[n].vdisplay);
}

/*
        Slot n of the list emptied: a connector read into it and then passed over (it lists no mode it can be shown at and no crtc
        drives it, or its mode has no size) leaves nothing there for the connector read into the slot next. What it had written
        stayed: the crtc its encoder was on, which the next connector then took as the one driving it, whether or not it can have
        that crtc; the crtcs its encoders can have, added to the next one's; a mode, which kept the next one's from being looked
        for; and its rates.
*/
static void compositor_connected_blank(struct compositor_connected *found, unsigned int n)
{
        found->connector[n] = found->crtc[n] = found->possible[n] = 0;
        found->current[n] = 0;
        memory_zero(&found->mode[n], sizeof(found->mode[n]));
        memory_zero(found->name[n], sizeof(found->name[n]));
        memory_zero(found->model[n], sizeof(found->model[n]));
        found->size[n][0] = found->size[n][1] = 0;
        memory_zero(found->hz[n], sizeof(found->hz[n]));
        found->hz_count[n] = 0;
        found->over[n] = 0;
        found->size_count[n] = 0;
}

/*
        Whether a connector is asked for what the kernel already has of it, and not made to look. GETCONNECTOR with no room
        for modes is the kernel's cue to probe the connector, which on a real screen is its EDID read again over the wire,
        tens of milliseconds a screen, and Canvas read it a moment before the compositor was started. So the first take of
        a card (compositor_space_first) asks with room, which the kernel answers from its list without looking, and owes a
        look (probe_owed), which the loop makes once its first frame of its own is drawn (compositor_space_retake): a list
        that was stale is put right then, a frame or two after the first, and when the list has no screen at all, or the
        take it gives fails, the same take is made again at once by looking. Every other read looks, as it did.
*/
static int compositor_list_kept;

static b32 compositor_connected_read(struct compositor_connected *found)
{
        struct compositor_card_res resources = {0};
        unsigned int *connectors, taken = 0, kept = 0;
        int largest = compositor_virtual() ? -1 : compositor_modes_largest();

        memory_zero(found, sizeof(*found));
        if (system_control(compositor.card, COMPOSITOR_GETRESOURCES, address_of resources) != 0 ||
            !resources.crtc_count || !(connectors = compositor_connectors_room((long)resources.connector_count + 1)))
                return -COMPOSITOR_EINVAL;
        // A card with more CRTCs than the kernel numbers drives the first WINDOWS_OUTPUTS of them.
        resources.crtc_count = resources.crtc_count > WINDOWS_OUTPUTS ? WINDOWS_OUTPUTS : resources.crtc_count;
        resources.connectors = (unsigned long)connectors;
        resources.crtcs = (unsigned long)found->crtcs;
        resources.fb_count = resources.encoder_count = 0;
        {
                unsigned int room_connectors = resources.connector_count, room_crtcs = resources.crtc_count;

                if (system_control(compositor.card, COMPOSITOR_GETRESOURCES, address_of resources) != 0)
                        return -COMPOSITOR_EINVAL;
                // The kernel writes no more than the room it was given and answers how many there are now, which a screen
                // that came between the two calls (a dock's MST port) makes more than were written: only those are read.
                resources.connector_count = resources.connector_count < room_connectors ? resources.connector_count : room_connectors;
                resources.crtc_count = resources.crtc_count < room_crtcs ? resources.crtc_count : room_crtcs;
        }
        found->crtc_count = resources.crtc_count;
        found->max_width = resources.max_width;
        found->max_height = resources.max_height;

        for (unsigned int at = 0; at < resources.connector_count && found->count < WINDOWS_OUTPUTS; at++)
        {
                struct compositor_connector probe = {.connector_id = connectors[at]};
                struct compositor_mode *modes = 0;
                unsigned int encoders[8], n = found->count, asked_modes = 0, asked_encoders = 8;
                int listed;

                if (compositor_list_kept)
                {
                        // Room for the lists in the one call: the kernel answers from what it has, and looks at nothing.
                        modes = compositor_modes_room(128);
                        if (!modes)
                                continue;
                        probe.mode_count = asked_modes = 128, probe.encoder_count = asked_encoders;
                        probe.modes = (unsigned long)modes;
                        probe.encoders = (unsigned long)encoders;
                }
                if (system_control(compositor.card, COMPOSITOR_GETCONNECTOR, address_of probe) != 0 ||
                    probe.connection != 1 || !probe.mode_count)
                        continue;
                if (probe.mode_count > asked_modes)
                {
                        // The kernel copies a list only when it fits what was asked for, and says how long it is either way: a list longer
                        // than the room is asked again with room for all of it.
                        modes = compositor_modes_room((long)probe.mode_count);
                        if (!modes)
                                continue;
                        asked_modes = probe.mode_count;
                        asked_encoders = probe.encoder_count = probe.encoder_count > 8 ? 8 : probe.encoder_count;
                        probe.modes = (unsigned long)modes;
                        probe.encoders = (unsigned long)encoders;
                        probe.prop_count = 0;
                        if (system_control(compositor.card, COMPOSITOR_GETCONNECTOR, address_of probe) != 0)
                                continue;
                }
                listed = probe.mode_count && probe.mode_count <= asked_modes;
                probe.encoder_count = probe.encoder_count <= asked_encoders ? probe.encoder_count : 0;

                found->connector[n] = connectors[at];
                compositor_connector_name(probe.connector_type, probe.connector_type_id, found->name[n]);
                /*
                        A real screen with no EDID, read from the kernel's list without a look: the list may be the no-EDID
                        one (up to 1024x768) of a connector the driver had not looked at yet when Canvas read it, and a first
                        take of it is the session's room for every window (compositor.stride). The list is not trusted: the
                        first take is made again by looking (compositor_space_first), which reads the EDID if there is one.
                */
                if (!compositor_connector_model(connectors[at], found->model[n], WINDOWS_OUTPUT_MODEL) && compositor_list_kept &&
                    largest >= 0)
                        return -COMPOSITOR_EINVAL;
                // The mode the kernel's command line names (video=, which the kernel marks the person's) before the
                // first listed, which is the one the screen prefers; Canvas takes them in that order too.
                for (unsigned int each = 0; listed && each < probe.mode_count && !found->mode[n].clock; each++)
                        if (modes[each].type & COMPOSITOR_MODE_USERDEF)
                                found->mode[n] = modes[each];
                if (listed && !found->mode[n].clock)
                        found->mode[n] = modes[0];
                if (probe.encoder_id)
                {
                        struct compositor_encoder encoder = {.encoder_id = probe.encoder_id};

                        if (system_control(compositor.card, COMPOSITOR_GETENCODER, address_of encoder) == 0 && encoder.crtc_id)
                        {
                                struct compositor_crtc current = {.crtc_id = encoder.crtc_id};

                                found->crtc[n] = encoder.crtc_id;
                                if (system_control(compositor.card, COMPOSITOR_GETCRTC, address_of current) == 0 &&
                                    current.mode_valid)
                                        found->mode[n] = current.mode, found->current[n] = 1;
                        }
                }
                for (unsigned int each = 0; each < probe.encoder_count; each++)
                {
                        struct compositor_encoder encoder = {.encoder_id = encoders[each]};

                        if (system_control(compositor.card, COMPOSITOR_GETENCODER, address_of encoder) == 0)
                                found->possible[n] |= encoder.possible_crtcs;
                }
                // Every size the screen lists, for the Displays page (compositor_sizes_take).
                if (listed)
                        compositor_sizes_take(found, n, modes, probe.mode_count);
                /*
                        The size the screen's displays line asks (mode=WxH), where the screen lists it, and else the size Canvas's
                        word asks, where the card has not refused it, over the crtc's (compositor_modes_largest); the crtc's mode is
                        held to go back to (found->held). A guest's (largest -1) takes only a size its line asks.
                */
                {
                        const struct windows_display *display = windows_display_of(compositor_display, compositor_displays, found->name[n],
                                                                                   windows_name_length(found->name[n]));
                        unsigned int pick = probe.mode_count;

                        if (listed && display && display->width)
                        {
                                pick = compositor_fastest_at(modes, probe.mode_count, display->width, display->height);
                                if (pick < probe.mode_count && compositor_mode_refused(connectors[at], &modes[pick]))
                                        pick = probe.mode_count;
                        }
                        if (pick == probe.mode_count && listed && largest >= 0)
                                pick = compositor_modes_pick(modes, probe.mode_count, largest);

                        if (pick < probe.mode_count && !compositor_mode_refused(connectors[at], &modes[pick]) &&
                            (modes[pick].hdisplay != found->mode[n].hdisplay || modes[pick].vdisplay != found->mode[n].vdisplay))
                        {
                                if (found->current[n])
                                {
                                        found->held[n] = found->mode[n];
                                        compositor_refresh_pick(found->name[n], probe.mode_count, modes, &found->held[n]);
                                        found->held_size[n][0] = found->held[n].hdisplay;
                                        found->held_size[n][1] = found->held[n].vdisplay;
                                        if (compositor_display_degrees(found->name[n]) % 180 == 90)
                                                found->held_size[n][0] = found->held[n].vdisplay, found->held_size[n][1] = found->held[n].hdisplay;
                                        for (unsigned int each = 0; each < probe.mode_count; each++)
                                                compositor_rate_add(found->held_hz[n], &found->held_hz_count[n], &modes[each],
                                                                    found->held[n].hdisplay, found->held[n].vdisplay);
                                        found->over[n] = 1;
                                }
                                found->mode[n] = modes[pick];
                        }
                }
                // The rate the displays file asks for: the mode of this size at it that the screen lists, or the mode it has.
                if (listed)
                        compositor_refresh_pick(found->name[n], probe.mode_count, modes, &found->mode[n]);
                if (!found->current[n] && !listed)
                {
                        compositor_connected_blank(found, n);
                        continue;
                }
                found->size[n][0] = found->mode[n].hdisplay;
                found->size[n][1] = found->mode[n].vdisplay;
                // A screen turned a quarter is laid out at its turned size, its width and height the other way round.
                if (compositor_display_degrees(found->name[n]) % 180 == 90)
                {
                        int turned = found->size[n][0];

                        found->size[n][0] = found->size[n][1], found->size[n][1] = turned;
                }
                if (found->size[n][0] && found->size[n][1])
                {
                        if (listed)
                                compositor_rates_take(found, n, modes, probe.mode_count);
                        found->count++;
                }
                else
                        compositor_connected_blank(found, n);
        }

        // A crtc is one screen's: those a crtc drives now keep it, and the rest take the first free one they can have.
        for (unsigned int n = 0; n < found->count; n++)
                for (unsigned int at = 0; found->crtc[n] && at < found->crtc_count; at++)
                        if (found->crtcs[at] == found->crtc[n])
                        {
                                if (taken >> at & 1)
                                        found->crtc[n] = 0, found->current[n] = 0;
                                else
                                        taken |= 1u << at;
                                break;
                        }
        for (unsigned int n = 0; n < found->count; n++)
                for (unsigned int at = 0; !found->crtc[n] && at < found->crtc_count; at++)
                        if (!(taken >> at & 1) && (!found->possible[n] || found->possible[n] >> at & 1))
                        {
                                found->crtc[n] = found->crtcs[at];
                                taken |= 1u << at;
                        }
        // One no crtc is left for is no screen of this desktop.
        for (unsigned int n = 0; n < found->count; n++)
        {
                if (!found->crtc[n])
                        continue;
                if (kept != n)
                {
                        found->connector[kept] = found->connector[n], found->crtc[kept] = found->crtc[n];
                        found->possible[kept] = found->possible[n], found->current[kept] = found->current[n];
                        found->mode[kept] = found->mode[n];
                        memory_copy(found->name[kept], found->name[n], WINDOWS_OUTPUT_NAME);
                        memory_copy(found->model[kept], found->model[n], WINDOWS_OUTPUT_MODEL);
                        found->size[kept][0] = found->size[n][0], found->size[kept][1] = found->size[n][1];
                        memory_copy(found->hz[kept], found->hz[n], sizeof(found->hz[n]));
                        found->hz_count[kept] = found->hz_count[n];
                        found->over[kept] = found->over[n], found->held[kept] = found->held[n];
                        memory_copy(found->held_size[kept], found->held_size[n], sizeof(found->held_size[n]));
                        memory_copy(found->held_hz[kept], found->held_hz[n], sizeof(found->held_hz[n]));
                        found->held_hz_count[kept] = found->held_hz_count[n];
                        memory_copy(found->sizes[kept], found->sizes[n], sizeof(found->sizes[n]));
                        found->size_count[kept] = found->size_count[n], found->size_preferred[kept] = found->size_preferred[n];
                }
                kept++;
        }
        found->count = kept;
        return 0;
}

// The displays file, /root's, and a check's own.
static const char *compositor_displays_path = SETTING_CANVAS_DISPLAYS_PATH;

// Reads the displays file into compositor_display. What is bad in it is said on the kernel log (report), line by line:
// the field and the screen it is on, which is kept at its default.
static void compositor_displays_read(int report)
{
        static const char *const fields[] = {"name", "rotation", "refresh", "x", "y", "field"};
        char text[1024];
        unsigned int bits[] = {WINDOWS_DISPLAY_NAME, WINDOWS_DISPLAY_ROTATION, WINDOWS_DISPLAY_REFRESH, WINDOWS_DISPLAY_X,
                               WINDOWS_DISPLAY_Y, WINDOWS_DISPLAY_OTHER};

        compositor_text(compositor_displays_path, text, sizeof(text));
        compositor_displays = windows_displays_read(text, compositor_display, WINDOWS_DISPLAYS);
        for (unsigned int each = 0; report && each < compositor_displays; each++)
                for (unsigned int bit = 0; bit < sizeof(bits) / sizeof(bits[0]); bit++)
                {
                        char line[160];
                        unsigned int at = compositor_said(line, 0, "compositor: canvas.displays: bad ");

                        if (!(compositor_display[each].bad & bits[bit]))
                                continue;
                        at = compositor_said(line, at, fields[bit]);
                        at = compositor_said(line, at, " on ");
                        at = compositor_said(line, at, compositor_display[each].name[0] ? compositor_display[each].name : "a line");
                        at = compositor_said(line, at, ", kept at its default");
                        line[at] = 0;
                        compositor_kmsg((string_address)line, 0, (string_address) "");
                }
}

// The connected screens laid by the arrangement, but for those in dropped (a bit each), which are dark: what
// windows_arrange_lay answers, in the found list's order.
static unsigned int compositor_space_lay(const struct compositor_connected *found, unsigned int dropped,
                                         int (*rects)[4], unsigned char *order)
{
        char names[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_NAME], text[512], pinned[512];
        int sizes[WINDOWS_OUTPUTS][2], some[WINDOWS_OUTPUTS][4];
        unsigned char map[WINDOWS_OUTPUTS], laid[WINDOWS_OUTPUTS];
        unsigned int count = 0, lit, made;

        for (unsigned int n = 0; n < found->count; n++)
        {
                rects[n][0] = rects[n][1] = rects[n][2] = rects[n][3] = 0;
                if (dropped >> n & 1)
                        continue;
                memory_copy(names[count], found->name[n], WINDOWS_OUTPUT_NAME);
                sizes[count][0] = found->size[n][0];
                sizes[count][1] = found->size[n][1];
                map[count++] = (unsigned char)n;
        }
        compositor_text(SETTING_CANVAS_ARRANGE_PATH, text, sizeof(text));
        // A display's place in the file is its word in the arrangement: the arrangement laid is that text with the places put in.
        made = windows_arrange_pinned(text, (const char (*)[WINDOWS_OUTPUT_NAME])names, count, compositor_display,
                                      compositor_displays, pinned, sizeof(pinned));
        if (made)
                memory_copy(text, pinned, made);
        lit = windows_arrange_lay(text, (const char (*)[WINDOWS_OUTPUT_NAME])names, (const int (*)[2])sizes, count, some,
                                     laid);
        for (unsigned int at = 0; at < count; at++)
                memory_copy(rects[map[at]], some[at], sizeof(some[at]));
        for (unsigned int at = 0; at < lit; at++)
                order[at] = map[laid[at]];
        /*
                The screen whose displays line says primary=yes is the primary (the table's index 0: the taskbar's, where a window
                opens) wherever it is laid: it goes first in the order, the rest after it as they were. Where it is on the desk
                does not change. With no such screen lit the first laid is, as before.
        */
        for (unsigned int at = 1; at < lit; at++)
        {
                const struct windows_display *display = windows_display_of(compositor_display, compositor_displays, found->name[order[at]],
                                                                           windows_name_length(found->name[order[at]]));
                unsigned char primary = order[at];

                if (!display || !display->primary)
                        continue;
                for (unsigned int back = at; back > 0; back--)
                        order[back] = order[back - 1];
                order[0] = primary;
                break;
        }
        return lit;
}

// A scanout of width by height: a dumb buffer, its framebuffer and its mapping. Answers 0, or a negative errno.
static b32 compositor_scanout_make(int width, int height, unsigned int *fb_id, unsigned int *handle,
                                   unsigned int **pixels, unsigned long *bytes, unsigned long *pitch)
{
        struct compositor_dumb dumb = {(unsigned int)height, (unsigned int)width, 32, 0, 0, 0, 0};
        struct compositor_map_dumb map = {0};
        struct compositor_fb fb = {0};
        long mapped;

        if (system_control(compositor.card, COMPOSITOR_CREATE_DUMB, address_of dumb) != 0)
                return -COMPOSITOR_ENOSPC;
        fb.width = dumb.width;
        fb.height = dumb.height;
        fb.pitch = dumb.pitch;
        fb.bpp = 32;
        fb.depth = 24;
        fb.handle = dumb.handle;
        map.handle = dumb.handle;
        mapped = system_control(compositor.card, COMPOSITOR_ADDFB, address_of fb) != 0 ||
                         system_control(compositor.card, COMPOSITOR_MAP_DUMB, address_of map) != 0
                     ? -COMPOSITOR_EINVAL
                     : compositor_call(syscall(mmap), 0, dumb.size, 3, 1, (positive)compositor.card, map.offset);
        if (mapped < 0 && mapped > -4096)
        {
                unsigned int close[2] = {dumb.handle, 0};

                if (fb.fb_id)
                        system_control(compositor.card, COMPOSITOR_RMFB, address_of fb.fb_id);
                system_control(compositor.card, COMPOSITOR_GEM_CLOSE, address_of close);
                return -COMPOSITOR_EINVAL;
        }
        *fb_id = fb.fb_id;
        *handle = dumb.handle;
        *pixels = (unsigned int *)mapped;
        *bytes = dumb.size;
        *pitch = dumb.pitch;
        return 0;
}

// A scanout no crtc shows any more, given back.
static void compositor_scanout_free(unsigned int fb, unsigned int handle, unsigned int *pixels, unsigned long bytes)
{
        unsigned int close[2] = {handle, 0};

#ifdef MOONWATER_GPU_C
        // The GPU's import of it goes with it: the card hands a buffer made later the lowest free handle, this one's, and the import (which keeps this
        // buffer alive) is known by the handle, so the GPU would go on drawing into this one while the screen shows that.
        for (unsigned int at = 0; at < 2; at++)
                if (compositor_gpu_state.cache[at] && compositor_gpu_state.cache_key[at] == handle)
                {
                        gpu_unimport(compositor_gpu_state.cache[at], GPU_GONE_UNIMPORT);
                        compositor_gpu_state.cache[at] = compositor_gpu_state.cache_key[at] = 0;
                        if (compositor_gpu_state.out_key == handle)
                                compositor_gpu_state.out = compositor_gpu_state.out_key = 0;
                }
        for (unsigned int at = 0; at < WINDOWS_OUTPUTS; at++)
                if (compositor_gpu_state.turn_key[at] == handle && compositor_gpu_state.turn_va[at])
                        gpu_unimport(compositor_gpu_state.turn_va[at], GPU_GONE_UNIMPORT), compositor_gpu_state.turn_va[at] = 0;
#endif
        gpu_forget((const void *)(pixels), (positive)(bytes)), compositor_call(syscall(munmap), (positive)pixels, bytes, 0, 0, 0, 0);
        system_control(compositor.card, COMPOSITOR_RMFB, address_of fb);
        system_control(compositor.card, COMPOSITOR_GEM_CLOSE, address_of close);
}

// The spare: a dumb buffer as the scanout is (compositor_scanout_make), all stale until it has been composed into. Made when a present wants it.
static void compositor_plate_make(void)
{
        unsigned int fb = 0, handle = 0, *pixels = 0;
        unsigned long bytes = 0, pitch = 0;

        if (compositor.card < 0 || compositor_spare.fb || compositor_plate_refused || !compositor.scanout_pitch)
                return;
        if (compositor_scanout_make(compositor.width, compositor.height, &fb, &handle, &pixels, &bytes, &pitch) != 0 || pitch != compositor.scanout_pitch)
        {
                if (!compositor_plate_refused)
                        compositor_kmsg((string_address) "compositor: no second scanout: the frames are composed in place", 0, (string_address) "");
                compositor_plate_refused = 1;
                if (fb)
                        compositor_scanout_free(fb, handle, pixels, bytes);
                return;
        }
        compositor_spare.fb = fb, compositor_spare.handle = handle, compositor_spare.pixels = pixels;
        compositor_spare.bytes = bytes, compositor_spare.pitch = pitch;
        compositor_stale_all(compositor_spare.stale, &compositor_spare.stale_any, &compositor_spare_parts);
}

// The spare given back (the scanout is replaced: a space of another size, or the second screen). Not shown: no flip is in flight then.
static void compositor_plate_drop(void)
{
        if (compositor_spare.fb && compositor.card >= 0)
                compositor_scanout_free(compositor_spare.fb, compositor_spare.handle, compositor_spare.pixels, compositor_spare.bytes);
        memory_zero(&compositor_spare, sizeof(compositor_spare));
        compositor_plate_refused = 0;
}

// Another framebuffer of the scanout, as large and over the same buffer, for a screen of its own; 0 when the card will
// not make one, and that screen shares the scanout's.
static unsigned int compositor_scanout_fb(void)
{
        struct compositor_fb fb = {0, (unsigned int)compositor.width, (unsigned int)compositor.height,
                                   (unsigned int)compositor.scanout_pitch, 32, 24, compositor.scanout_handle};

        return system_control(compositor.card, COMPOSITOR_ADDFB, address_of fb) == 0 ? fb.fb_id : 0;
}

/*
        The framebuffer a screen Canvas turns in software is drawn into, at the panel's size: one for each screen, kept
        while the size is the same. A buffer whose size changes is handed back in retired, and given back once the
        crtcs show the new one (compositor_turn_retire). False when the card gives none.
*/
static int compositor_turn_buffer(unsigned int at, unsigned int width, unsigned int height, struct compositor_turn *retired)
{
        struct compositor_turn *turn = &compositor_turns[at];

        if (turn->fb && turn->width == width && turn->height == height)
                return 1;
        if (turn->fb)
                *retired = *turn;
        memory_zero(turn, sizeof(*turn));
        if (compositor_scanout_make((int)width, (int)height, &turn->fb, &turn->handle, &turn->pixels, &turn->bytes,
                                    &turn->pitch) < 0)
                return 0;
        turn->width = width;
        turn->height = height;
        return 1;
}

// A buffer given back, which no crtc shows any more.
static void compositor_turn_retire(const struct compositor_turn *retired)
{
        if (retired->fb)
                compositor_scanout_free(retired->fb, retired->handle, retired->pixels, retired->bytes);
}

// A crtc that shows nothing: off, its screen dark.
static void compositor_crtc_off(unsigned int crtc)
{
        struct compositor_crtc off = {0, 0, crtc, 0, 0, 0, 0, 0, {0}};

        system_control(compositor.card, COMPOSITOR_SETCRTC, address_of off);
}

/*
        Whether the card is a guest's display (a virtual machine's, or the kernel's own vkms): one with no link to train, whose crtcs a wake
        sets without turning them off first (virtio-gpu does not always get its host window back from an off).
*/
static int compositor_virtual(void)
{
        static const char *const guests[] = {"bochs-drm", "virtio_gpu", "qxl", "vmwgfx", "cirrus-qemu", "hyperv_drm", "vkms"};

        return compositor_driver_in(guests, sizeof(guests) / sizeof(guests[0]));
}

// Whether two modes are the same timing: what a crtc scans at, whatever the name says.
static int compositor_mode_same(const struct compositor_mode *a, const struct compositor_mode *b)
{
        return a->clock == b->clock && a->hdisplay == b->hdisplay && a->vdisplay == b->vdisplay &&
               a->htotal == b->htotal && a->vtotal == b->vtotal && a->vrefresh == b->vrefresh && a->flags == b->flags;
}

/*
        The windows and the pointer, from the screens they were on (was, with
        their names, before the screens were taken again) to the screens now.
        A window on a screen that is still there, by its name, goes with it
        where it moved; one on a screen that went goes to the primary, at the
        same share of the room it had (compositor_screen_carry); one that
        filled a screen or half of one fills the same of the screen it is on
        now; and every one is then kept reachable. A drag or a resize under way
        ends. The pointer goes the same way and is kept on a screen.
*/
static void compositor_space_carry(const int (*was)[4], const char (*names)[WINDOWS_OUTPUT_NAME], unsigned int count)
{
        unsigned int now_count;
        const int (*now)[4] = compositor_screens(&now_count);
        int nowhere = -2147483647 - 1;

        compositor_pointer_sync();
        compositor_preview(COMPOSITOR_NONE);
        compositor.drag = compositor.resize = COMPOSITOR_NONE;
        for (int row = -1; row < (int)compositor_rows_in_use; row++)
        {
                int x = row < 0 ? compositor.real_x : compositor.x[row], y = row < 0 ? compositor.real_y : compositor.y[row];
                int w = row < 0 ? 1 : (int)compositor.w[row], h = row < 0 ? 1 : (int)compositor.h[row], from, to = 0;

                if (row >= 0 && !compositor.showing[row])
                        continue;
                from = count ? compositor_screen_owner(was, count, x, y, w, h) : -1;
                for (unsigned int at = 0; from >= 0 && at < now_count && at < compositor.outputs; at++)
                        if (!string_compare((string_address)compositor.output[at].name, (string_address)names[from]))
                                to = (int)at;
                if (from < 0 || !memory_compare(was[from], now[to], sizeof(was[from])))
                        ;
                else if (row < 0)
                        compositor_screen_carry(was[from], now[to], &compositor.real_x, &compositor.real_y, 1, 1);
                else
                {
                        int sx = compositor.saved_x[row], sy = compositor.saved_y[row];

                        compositor_screen_carry(was[from], now[to], &x, &y, w, h);
                        compositor_screen_carry(was[from], now[to], &sx, &sy, (int)compositor.saved_w[row],
                                                (int)compositor.saved_h[row]);
                        compositor.saved_x[row] = sx, compositor.saved_y[row] = sy;
                        compositor.x[row] = compositor.table->x[row] = x;
                        compositor.y[row] = compositor.table->y[row] = y;
                }
                // One that filled a screen's room fills the room of the screen it is on now, the band left out.
                if (row >= 0 && compositor.arranged[row] == COMPOSITOR_ROOM)
                {
                        int room[4];

                        compositor_room(now[to], room);
                        compositor_arrange_rect(row, COMPOSITOR_ROOM, room);
                }
                else if (row >= 0 && compositor.arranged[row] != COMPOSITOR_FLOATING)
                        compositor_arrange_on(row, compositor.arranged[row], to);
                else if (row >= 0)
                        compositor_place(row, compositor.x[row], compositor.y[row]);
        }
        compositor_screen_keep_point(now, now_count, nowhere, nowhere, &compositor.real_x, &compositor.real_y);
        compositor.kept_x = compositor.real_x;
        compositor.kept_y = compositor.real_y;
        compositor_pointer_shown();
        // A window on a screen that stayed whose band moved (the primary is another now) follows its room.
        compositor_room_follow();
}

static void compositor_table_outputs(void);
static void compositor_band_room(void);

/*
        The card is to show the compositor's own scanout on every crtc: no window straight, none on the overlay, and
        no flip whose row a picture held back waits on (compositor_flip_done lets go of every held picture once the
        crtcs are set; flip_row would have kept back the one a direct window's newest buffer was taken into, and with
        two screens nothing flips again).
*/
static void compositor_scanout_own(void)
{
        compositor_overlay_off();
        compositor.direct_row = compositor.plane_row = compositor.flip_row = -1;
        compositor.plane_fb = 0;
        compositor.plane_gone = 0;
        compositor.on_screen = compositor.fb;
}

/*
        The whole space to be drawn again, and what waited to be drawn before it forgotten: those are rectangles of the space
        before, and a space that is another shape holds none of them. A turn makes 2560 by 1080 of 1080 by 2560, the band's
        rectangle along the old bottom (rows past the new space's end) is made just before the screens are taken again, and
        composed it was stored into memory past the scanout's end: a write the compositor died of on a booted guest at the
        third or fourth turn, when the memory there was not mapped.
*/
static void compositor_damage_space(void)
{
        compositor.damages = 0;
        compositor_damage(0, 0, compositor.width, compositor.height);
}

static void compositor_scale_fit(void);
static void compositor_scale_set(int scale);

/*
        The scale each screen's displays line asks, taken by the screens as they are driven. A scale is no mode and no
        place, so when it is all that changed the screens are not taken again (compositor_space_take): no scanout and
        no commit, and this follows it instead: each screen's scale, the desktop's (the primary's) and the table's, the
        pointer's plane painted again at the scale, the windows laid again in the room the bands leave at it, the
        bands' memory for the size they are drawn at, and every pixel drawn again. A window's own scale is its screen's,
        counted into the table as it is next drawn (compositor_frame_of), which also knocks it. True when one changed.
*/
static int compositor_scales_take(void)
{
        int was[WINDOWS_OUTPUTS][4], changed = 0;
        char names[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_NAME];

        for (unsigned int at = 0; at < compositor.outputs && at < WINDOWS_OUTPUTS; at++)
        {
                int scale = compositor_display_scale(compositor.output[at].name);

                changed |= scale != compositor.output[at].scale;
                compositor.output[at].scale = scale;
                memory_copy(was[at], compositor.screen[at], sizeof(was[at]));
                memory_copy(names[at], compositor.output[at].name, WINDOWS_OUTPUT_NAME);
        }
        if (!changed)
                return 0;
        compositor_scale_set(compositor.output[0].scale);
        compositor_space_carry((const int (*)[4])was, (const char (*)[WINDOWS_OUTPUT_NAME])names, compositor.outputs);
        return 1;
}

/*
        The screens taken: what the card has connected, laid by the
        arrangement, one scanout as large as the space they make, and each
        crtc showing its screen's rectangle of it. A screen the card will not
        drive -- one that puts the space past the largest framebuffer the card
        makes, or whose mode a commit refuses -- is left dark and the rest laid
        again without it, so one screen that cannot be shown takes none of the
        others down with it. The first take starts from Canvas's state; a
        later one (a screen plugged in or out, the arrangement changed) is
        nothing when nothing changed, and otherwise carries the windows and
        the pointer to the space it makes. Answers 0, or a negative errno
        when not one screen could be shown.
*/
static b32 compositor_space_take(int first)
{
        struct compositor_connected found;
        int rects[WINDOWS_OUTPUTS][4], was[WINDOWS_OUTPUTS][4];
        char was_names[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_NAME];
        unsigned int was_crtc[WINDOWS_OUTPUTS], was_count = compositor.outputs, dropped = 0, extra[WINDOWS_OUTPUTS], extras;
        unsigned char order[WINDOWS_OUTPUTS];
        b32 status;
        int carried = 0, regained = 0, later = !first;

        compositor_displays_read(1);
        status = compositor_connected_read(&found);

        // No screen (or a card that is not one for screens) is its own answer: the start waits for one (compositor_card_open), and a
        // desktop whose screens all went keeps what it has and is marked, so that the take that finds one again is a mode set.
        if (status < 0 || !found.count)
        {
                if (!first && compositor.outputs)
                        compositor.screens_gone = 1;
                return -COMPOSITOR_ENODEV;
        }
        // A flip in flight is the card showing a window's buffer on one crtc: the screens are taken once it has landed.
        if (!first && compositor.flip_pending)
        {
                compositor.space_wanted = 1;
                return 0;
        }
        compositor.space_wanted = 0;
        for (unsigned int at = 0; at < was_count; at++)
        {
                memory_copy(was[at], compositor.screen[at], sizeof(was[at]));
                memory_copy(was_names[at], compositor.output[at].name, WINDOWS_OUTPUT_NAME);
                was_crtc[at] = compositor.output[at].crtc;
        }

        for (unsigned int attempt = 0; attempt <= found.count; attempt++)
        {
                unsigned int lit = compositor_space_lay(&found, dropped, rects, order), refused = 0, same;
                struct compositor_turn retired[WINDOWS_OUTPUTS] = {{0}};
                unsigned int old_fb = compositor.fb, old_handle = compositor.scanout_handle, *old_pixels = compositor.scanout;
                unsigned long old_bytes = compositor.scanout_bytes;
                unsigned int fb = compositor.fb, handle = compositor.scanout_handle, *pixels = compositor.scanout;
                unsigned long bytes = compositor.scanout_bytes, pitch = compositor.scanout_pitch;
                int box[4], screens[WINDOWS_OUTPUTS][4], most_w = 0, most_h = 0;
                positive period = 0;

                if (!lit)
                        return -COMPOSITOR_EINVAL;
                for (unsigned int at = 0; at < lit; at++)
                        memory_copy(screens[at], rects[order[at]], sizeof(screens[at]));
                compositor_screens_box((const int (*)[4])screens, lit, box);

                // Past the largest framebuffer the card makes: the last screen laid is left dark, and the rest laid again.
                if (lit > 1 && found.max_width && found.max_height &&
                    ((unsigned int)box[2] > found.max_width || (unsigned int)box[3] > found.max_height))
                {
                        compositor_kmsg((string_address) "compositor: the screens make a space larger than the card's largest framebuffer, dark: ",
                                        0, (string_address)found.name[order[lit - 1]]);
                        dropped |= 1u << order[lit - 1];
                        continue;
                }

                /*
                        Every screen had gone and one is back. It may be the same connector at the same mode on the same crtc, and
                        nothing of the desktop's has changed; the link has, and is set up only by a mode set (a DisplayPort link is
                        trained by one), which the card makes of a SETCRTC only when something of the crtc changes. So the crtcs
                        the screens were on are turned off first, and what follows turns each on: a commit that enables a crtc is
                        always a mode set. Taken as the same, the screen that came back stayed dark with the desktop running.
                */
                /*
                        A wake is the same (compositor_woke, retrain): what the card restored may be a crtc that says it is on and a
                        link that is not, and a monitor that came back may be another connector or at other modes, which this take
                        read again. Each crtc is set anew, turned off first but on a guest's card.
                */
                if (!first && (compositor.screens_gone || compositor.retrain))
                {
                        for (unsigned int at = 0; at < was_count && (compositor.screens_gone || !compositor_virtual()); at++)
                                compositor_crtc_off(was_crtc[at]);
                        compositor.screens_gone = compositor.retrain = 0;
                        regained = 1;
                }
                same = !first && !regained && lit == compositor.outputs && box[2] == compositor.width && box[3] == compositor.height;
                for (unsigned int at = 0; same && at < lit; at++)
                        same = compositor.output[at].crtc == found.crtc[order[at]] &&
                               compositor.output[at].connector == found.connector[order[at]] &&
                               !memory_compare(compositor.screen[at], screens[at], sizeof(screens[at])) &&
                               compositor_mode_same(&compositor.output[at].mode, &found.mode[order[at]]) &&
                               compositor.output[at].degrees == compositor_display_degrees(found.name[order[at]]);
                if (same)
                {
                        // The same screens at the same modes: only the scale a line asks can have changed, and it takes no commit.
                        // The table says what the lines ask now, beside what was done of it (the settings show both).
                        compositor_scales_take();
                        compositor_table_outputs();
                        return 0;
                }

                // A space of another size is another scanout; the one before is given back once no crtc shows it.
                if (first || box[2] != compositor.width || box[3] != compositor.height)
                {
                        compositor_plate_drop();
                        status = compositor_scanout_make(box[2], box[3], &fb, &handle, &pixels, &bytes, &pitch);
                        if (status < 0 && lit > 1)
                        {
                                compositor_kmsg((string_address) "compositor: no scanout as large as the screens' space, dark: ",
                                                0, (string_address)found.name[order[lit - 1]]);
                                dropped |= 1u << order[lit - 1];
                                continue;
                        }
                        if (status < 0)
                                return status;
                }

                // The screens, the primary first, each at its own mode; the framebuffers the screens before had of their
                // own are given back once no crtc shows them.
                extras = 0;
                for (unsigned int at = 0; at < compositor.outputs; at++)
                        if (compositor.output[at].fb && compositor.output[at].fb != compositor.fb && compositor.output[at].turned != 2)
                                extra[extras++] = compositor.output[at].fb;
                memory_zero(compositor.output, sizeof(compositor.output));
                for (unsigned int at = 0; at < lit; at++)
                {
                        unsigned int n = order[at];
                        positive refresh = found.mode[n].vrefresh >= 24 && found.mode[n].vrefresh <= 1000 ? found.mode[n].vrefresh : 60;

                        compositor.output[at].crtc = found.crtc[n];
                        compositor.output[at].connector = found.connector[n];
                        compositor.output[at].mode = found.mode[n];
                        memory_copy(compositor.output[at].hz, found.hz[n], sizeof(found.hz[n]));
                        compositor.output[at].hz_count = found.hz_count[n];
                        memory_copy(compositor.output[at].sizes, found.sizes[n], sizeof(found.sizes[n]));
                        compositor.output[at].size_count = found.size_count[n];
                        compositor.output[at].size_preferred = found.size_preferred[n];
                        memory_copy(compositor.output[at].name, found.name[n], WINDOWS_OUTPUT_NAME);
                        memory_copy(compositor.output[at].model, found.model[n], WINDOWS_OUTPUT_MODEL);
                        for (unsigned int index = 0; index < found.crtc_count; index++)
                                if (found.crtcs[index] == found.crtc[n])
                                        compositor.output[at].crtc_index = index;
                        // The turn its displays line asks for: the card's own rotation where its plane has one, else Canvas's.
                        compositor.output[at].degrees = compositor_display_degrees(found.name[n]);
                        compositor.output[at].scale = compositor_display_scale(found.name[n]);
                        compositor.output[at].turned = !compositor.output[at].degrees ? 0 :
                                compositor_rotation_hardware(compositor.output[at].crtc_index, compositor.output[at].degrees,
                                                             &compositor.output[at].turn_plane,
                                                             &compositor.output[at].turn_property,
                                                             &compositor.output[at].turn_value) ? 1 : 2;
                        // A screen the card turns has the buffer of one Canvas turns too: it is set up as that one is, and is
                        // that one if the card refuses the turn (below).
                        if (compositor.output[at].turned &&
                            !compositor_turn_buffer(at, (unsigned int)found.mode[n].hdisplay, (unsigned int)found.mode[n].vdisplay,
                                                    &retired[at]))
                        {
                                compositor.output[at].turned = 0;
                                compositor_kmsg((string_address) "compositor: canvas.displays: no buffer to turn a screen in software, left unturned",
                                                0, (string_address) "");
                        }
                        memory_copy(compositor.screen[at], screens[at], sizeof(screens[at]));
                        most_w = screens[at][2] > most_w ? screens[at][2] : most_w;
                        most_h = screens[at][3] > most_h ? screens[at][3] : most_h;
                        // Frames go out at the fastest screen's rate.
                        {
                                positive own = compositor_mode_period(&found.mode[n], refresh);

                                period = !period || own < period ? own : period;
                        }
                }
                int scale_was = compositor.scale;

                compositor.outputs = lit;
                if (lit)
                        compositor.scale = compositor.output[0].scale;
                if (lit && compositor.table)
                        compositor.table->scale = (unsigned int)compositor.scale;
                // A frame is the fastest screen's period; what its vblanks measure is measured again from the next flip.
                if (period != compositor.period && compositor.table)
                        compositor.table->period_seen = 0;
                compositor.period = period;
                compositor_vblank_last.when = 0;
                if (compositor.crtc != compositor.output[0].crtc)
                {
                        compositor.overlay_state = 0;
                        compositor.primary = compositor.overlay = 0;
                        compositor.scaled_direct = compositor.scale_plane_refused = 0;
#ifdef MOONWATER_GPU_C
                        compositor_fence_prop = 0; // the new primary plane's IN_FENCE_FD is looked for again (present.c's, with the GPU)
#endif
                }
                compositor.crtc = compositor.output[0].crtc;
                compositor.crtc_index = compositor.output[0].crtc_index;
                compositor.connector = compositor.output[0].connector;
                compositor.mode = compositor.output[0].mode;
                // The room every window's buffer has: the widest and the tallest screen connected at the start, dark or
                // lit, for the session; what a window may be is the largest screen now, in that room.
                if (first)
                {
                        compositor.stride = compositor.room_h = 0;
                        for (unsigned int n = 0; n < found.count; n++)
                        {
                                compositor.stride = found.size[n][0] > compositor.stride ? found.size[n][0] : compositor.stride;
                                compositor.room_h = found.size[n][1] > compositor.room_h ? found.size[n][1] : compositor.room_h;
                        }
                }
                compositor.most_w = most_w < compositor.stride ? most_w : compositor.stride;
                compositor.most_h = most_h < compositor.room_h ? most_h : compositor.room_h;

                // The scanout the space is composed into; what was composed apart for the one before is let go.
                if (fb != compositor.fb)
                {
                        compositor_apart_drop();
                        compositor.scanout = pixels;
                        compositor.scanout_bytes = bytes;
                        compositor.scanout_pitch = pitch;
                        compositor.scanout_handle = handle;
                        compositor.fb = fb;
                        compositor.width = box[2];
                        compositor.height = box[3];
                }
                // The primary shows the scanout's framebuffer, and each other screen one of its own over it.
                for (unsigned int at = 0; at < lit; at++)
                        compositor.output[at].fb = compositor.output[at].turned == 2 ? compositor_turns[at].fb :
                                                   at ? compositor_scanout_fb() : compositor.fb;
                if (!first)
                {
                        carried = 1;
                        compositor_scanout_own();
                        compositor_space_carry((const int (*)[4])was, (const char (*)[WINDOWS_OUTPUT_NAME])was_names, was_count);
                        compositor_band_room();
                        // A desktop scale taken with the screens refits the floating windows to it (compositor_scale_fit, as compositor_scale_set does).
                        if (compositor.scale != scale_was)
                                compositor_scale_fit();
                        compositor_tabs_changed();
                }
                compositor.mirror_stale = 1;
                compositor_damage_space();
                compositor_present();

                /*
                        A turned screen is set up on its own buffer, the panel's size, from the origin: a screen Canvas turns
                        is shown from it, whole, and one the card turns starts there too, since its mode set on the scanout
                        with the plane not turned yet is a viewport the space does not hold (a quarter turn's panel is the
                        space's height wide), which the kernel refuses. A plane still turned from before (a compositor
                        started again) refuses that buffer the same way, and then the scanout is the one that fits.
                */
                unsigned int on_scanout = 0;

                // Disable unused CRTCs before enabling new ones so Intel Haswell PCH FDI / shared PLLs are freed first.
                for (unsigned int at = 0; at < was_count; at++)
                {
                        int used = 0;

                        for (unsigned int now = 0; now < lit; now++)
                                used |= compositor.output[now].crtc == was_crtc[at];
                        if (!used)
                                compositor_crtc_off(was_crtc[at]);
                }
                for (unsigned int n = 0; n < found.count; n++)
                        if (found.current[n] && !rects[n][2])
                                compositor_crtc_off(found.crtc[n]);

                for (unsigned int at = 0; at < lit; at++)
                {
                        int turned = compositor.output[at].turned != 0;
                        unsigned int fb_of = compositor.output[at].fb ? compositor.output[at].fb : compositor.fb;
                        struct compositor_crtc set = {(unsigned long)address_of compositor.output[at].connector, 1,
                                                      compositor.output[at].crtc,
                                                      turned ? compositor_turns[at].fb : fb_of,
                                                      turned ? 0 : (unsigned int)screens[at][0],
                                                      turned ? 0 : (unsigned int)screens[at][1], 0, 1, compositor.output[at].mode};

                        if (system_control(compositor.card, COMPOSITOR_SETCRTC, address_of set) == 0)
                                continue;
                        set.fb_id = fb_of, set.x = (unsigned int)screens[at][0], set.y = (unsigned int)screens[at][1];
                        if (compositor.output[at].turned == 1 && system_control(compositor.card, COMPOSITOR_SETCRTC, address_of set) == 0)
                                on_scanout |= 1u << at;
                        else
                                refused |= 1u << order[at];
                }
                // The screens the card turns are turned by their plane once their crtcs are set: a refusal leaves one on its
                // own buffer turned in software, and one the plane already turned as the plane has it.
                for (unsigned int at = 0; at < lit; at++)
                {
                        unsigned int fb_of = compositor.output[at].fb ? compositor.output[at].fb : compositor.fb;
                        int source[2] = {screens[at][0], screens[at][1]};

                        if (compositor.output[at].turned == 1 && !(refused >> order[at] & 1) &&
                            !compositor_rotate_commit(&compositor.output[at], source, (unsigned int)screens[at][2],
                                                      (unsigned int)screens[at][3], fb_of) &&
                            !(on_scanout >> at & 1))
                        {
                                // Its buffer was not drawn by the present above, which the card was to turn: the next one draws it.
                                compositor.output[at].turned = 2;
                                compositor.output[at].fb = compositor_turns[at].fb;
                                compositor_damage_space();
                        }
                        if (compositor.output[at].turned && compositor.output[at].degrees)
                                compositor_turn_say(compositor.output[at].name, compositor.output[at].degrees,
                                                    compositor.output[at].turned == 1);
                }
                // The turn each screen has, for the table's output_turn and output_software (compositor_table_outputs).
                for (unsigned int at = 0; at < lit; at++)
                {
                        compositor.output[at].turn = compositor.output[at].turned ? compositor.output[at].degrees : 0;
                        compositor.output[at].turn_software = compositor.output[at].turned == 2;
                }
                // The buffers the screens had before, which the crtcs now show no more.
                for (unsigned int at = 0; at < WINDOWS_OUTPUTS; at++)
                        compositor_turn_retire(&retired[at]);
                // The crtcs no screen of the space has: a screen that went, one arranged dark, one left dark above.
                for (unsigned int at = 0; at < was_count; at++)
                {
                        int used = 0;

                        for (unsigned int now = 0; now < lit; now++)
                                used |= compositor.output[now].crtc == was_crtc[at];
                        if (!used)
                                compositor_crtc_off(was_crtc[at]);
                }
                for (unsigned int n = 0; n < found.count; n++)
                        if (found.current[n] && !rects[n][2])
                                compositor_crtc_off(found.crtc[n]);
                for (unsigned int at = 0; at < extras; at++)
                        system_control(compositor.card, COMPOSITOR_RMFB, address_of extra[at]);
                // Only a scanout this take replaced (fb is the one it made). A present above whose flip the card refused swaps the
                // plates, which leaves compositor.fb the other plate and the scanout taken at the start the spare: freed as old, the
                // spare's mapping went while the compositor still composed into it, and the next present wrote into memory no longer
                // mapped (the wake's take after a refused one was a compositor that died of it).
                if (old_fb && old_fb != fb)
                        compositor_scanout_free(old_fb, old_handle, old_pixels, old_bytes);

                if (!refused)
                        break;
                /*
                        A screen the card refused at the size Canvas's word asks, where its crtc was at another a moment ago: it goes
                        back to that one, which the card had, and is not left dark; the size is not asked of it again until the word
                        or the displays file is written (compositor_mode_refuse). The take is made again with every screen in it.
                */
                {
                        unsigned int back = 0;

                        for (unsigned int n = 0; n < found.count; n++)
                                if ((refused >> n & 1) && found.over[n])
                                {
                                        compositor_mode_refuse(found.connector[n], &found.mode[n]);
                                        compositor_kmsg((string_address) "compositor: the card refused a screen at the size asked, back to the one it had: ",
                                                        0, (string_address)found.name[n]);
                                        found.mode[n] = found.held[n];
                                        found.size[n][0] = found.held_size[n][0], found.size[n][1] = found.held_size[n][1];
                                        memory_copy(found.hz[n], found.held_hz[n], sizeof(found.hz[n]));
                                        found.hz_count[n] = found.held_hz_count[n];
                                        found.over[n] = 0;
                                        back |= 1u << n;
                                }
                        refused &= ~back;
                        if (back)
                        {
                                was_count = compositor.outputs;
                                for (unsigned int at = 0; at < was_count; at++)
                                {
                                        memory_copy(was[at], compositor.screen[at], sizeof(was[at]));
                                        memory_copy(was_names[at], compositor.output[at].name, WINDOWS_OUTPUT_NAME);
                                        was_crtc[at] = compositor.output[at].crtc;
                                }
                                first = 0;
                                if (!refused)
                                        continue;
                        }
                }
                // A commit refused a screen: it is left dark, and the rest are taken again without it, from what was taken.
                for (unsigned int n = 0; n < found.count; n++)
                        if (refused >> n & 1)
                                compositor_kmsg((string_address) "compositor: the card refused a screen, which is left dark: ", 0,
                                                (string_address)found.name[n]);
                // The only screen refused: it is dark, and is looked at again as a screen that went is, until a take sets it.
                if (lit == 1)
                {
                        compositor.screens_gone = later;
                        return -COMPOSITOR_EINVAL;
                }
                dropped |= refused;
                was_count = compositor.outputs;
                for (unsigned int at = 0; at < was_count; at++)
                {
                        memory_copy(was[at], compositor.screen[at], sizeof(was[at]));
                        memory_copy(was_names[at], compositor.output[at].name, WINDOWS_OUTPUT_NAME);
                        was_crtc[at] = compositor.output[at].crtc;
                }
                first = 0;
        }

        // The crtcs show the compositor's scanout now, and no window's buffer: what a flip would have let go of is.
        if (carried)
                compositor_flip_done();
        compositor.layout++;
        compositor_table_outputs();
        for (unsigned int at = 0; at < compositor.outputs; at++)
                compositor.output[at].shows = 0;
        compositor.cursor.plane.scale = 0;
        compositor_vrr_forget();
        if (!first)
                compositor_cursor_move();
        return 0;
}

// The first take of a card: from the kernel's own lists, a look owed; by looking, at once, when those give no screen or no take.
static b32 compositor_space_first(void)
{
        b32 status;

        compositor_list_kept = 1;
        status = compositor_space_take(1);
        compositor_list_kept = 0;
        if (status >= 0)
        {
                compositor.probe_owed = 1;
                return status;
        }
        return compositor_space_take(1);
}

/*
        The card. DRM gives its cards minors with no promise that the first is 0 or that they are contiguous, and the
        display is not always card0: a firmware framebuffer's driver (simpledrm, efidrm) takes the first minor and goes
        when the real driver takes its screen over, which leaves card1 the only card, and a machine with two cards has its
        screen on either. So every minor is tried, as Canvas tries them (canvas_claim_range), and the card taken is the first
        that makes this program its master and has a connected screen: the real cards first, and a firmware card only when
        no other has a screen, the order Canvas keeps too. Only /dev/dri/card0 was opened before, and a machine whose screen
        was on another card had a compositor that ended at once every time it was started.
*/
#define COMPOSITOR_CARDS 64 // DRM's minors for primary nodes

static int compositor_uevents_open(void);
static int compositor_uevent_drm(const char *message, long length);

static long compositor_card_node(unsigned int minor)
{
        p8 path[24] = "/dev/dri/card";

        path[13 + positive_into_string(path + 13, minor)] = 0;
        return (long)system_open_at(AT_FDCWD, path, FILE_READ_WRITE | O_CLOEXEC);
}

// A check has cards of its own.
#ifndef COMPOSITOR_CARD_NODE
#define COMPOSITOR_CARD_NODE(minor) compositor_card_node(minor)
#endif

static int compositor_card_firmware(void)
{
        static const char *const firmware[] = {"simpledrm", "efidrm"};

        return compositor_driver_in(firmware, sizeof(firmware) / sizeof(firmware[0]));
}

/*
        One minor tried: 0 with the card taken and its first frame shown; -COMPOSITOR_ENOENT for no card there;
        -COMPOSITOR_EAGAIN for a firmware card while those are passed over; -COMPOSITOR_EBUSY for one another program is
        the master of; -COMPOSITOR_ENODEV for one with no connected screen; and any other error is a card that has a
        screen and could not be taken, which ends the walk. A card that is not taken is closed, and nothing of it is kept.
*/
static b32 compositor_card_try(unsigned int minor, int firmware)
{
        struct compositor_version version = {0};
        long card = COMPOSITOR_CARD_NODE(minor);
        b32 status;

        if (card < 0)
                return card == -COMPOSITOR_ENOENT ? -COMPOSITOR_ENOENT : -COMPOSITOR_EBUSY;
        compositor.card = (int)card;
        version.name_length = sizeof(compositor.driver) - 1;
        version.name = compositor.driver;
        memory_zero(compositor.driver, sizeof(compositor.driver));
        if (system_control(compositor.card, COMPOSITOR_VERSION, address_of version) != 0)
                compositor.driver[0] = 0;
        if (!firmware && compositor_card_firmware())
                status = -COMPOSITOR_EAGAIN;
        else if (system_control(compositor.card, COMPOSITOR_SET_MASTER, 0) != 0)
                status = -COMPOSITOR_EBUSY;
        else
                status = compositor_space_first();
        if (status < 0)
        {
                system_close(compositor.card);
                compositor.card = -1;
                compositor.driver[0] = 0;
        }
        return status;
}

/*
        Every card tried once. 0 with one taken. With none: the error of a card that has a screen and would not be taken;
        else -COMPOSITOR_ENODEV when a card that could be had has no screen, which is a wait for one (compositor_card_open);
        else -COMPOSITOR_EBUSY when every card there is has another master; else -COMPOSITOR_ENOENT, no card at all.
*/
static b32 compositor_cards_walk(void)
{
        unsigned long firmware = 0; // the minors passed over the first time, which are the only ones tried the second
        b32 none = -COMPOSITOR_ENOENT;

        for (unsigned int at = 0; at < 2 * COMPOSITOR_CARDS; at++)
        {
                unsigned int minor = at % COMPOSITOR_CARDS;
                b32 status;

                if (at >= COMPOSITOR_CARDS && !(firmware >> minor & 1))
                        continue;
                status = compositor_card_try(minor, at >= COMPOSITOR_CARDS);
                if (status >= 0)
                        return status;
                if (status == -COMPOSITOR_EAGAIN)
                        firmware |= 1ul << minor;
                else if (status == -COMPOSITOR_ENODEV || (status == -COMPOSITOR_EBUSY && none == -COMPOSITOR_ENOENT))
                        none = status;
                else if (status != -COMPOSITOR_ENOENT && status != -COMPOSITOR_EBUSY)
                        return status;
        }
        return none;
}

/*
        The wait for a screen: until the kernel says something of the drm subsystem (a screen plugged in, a card that
        came), which is 1, or the time given has passed, which is 0. What other subsystems say in between is read and
        passed over, and a read that fails for anything but nothing to read (the socket overran: ENOBUFS) is taken as
        the word, since what was lost may have been it. The uevent is only the wake: the cards are walked again
        whichever it was.
*/
static int compositor_screen_wait(positive nanoseconds)
{
        positive until = clock_monotonic_nanoseconds() + nanoseconds;

        for (;;)
        {
                struct compositor_poll wanted = {compositor.uevents, COMPOSITOR_POLLIN, 0};
                positive now = clock_monotonic_nanoseconds();
                char message[2048];
                long wait[2];
                int drm = 0;

                if (now >= until)
                        return 0;
                wait[0] = (long)((until - now) / 1000000000ull);
                wait[1] = (long)((until - now) % 1000000000ull);
                if (compositor_call(syscall(ppoll), (positive)&wanted, 1, (positive)wait, 0, 0, 0) <= 0)
                        continue;
                for (int reads = 0; reads < 64; reads++)
                {
                        bipolar got = socket_receive((b32)compositor.uevents, message, sizeof(message), MSG_DONTWAIT, 0, 0);

                        if (got <= 0)
                        {
                                drm |= got < 0 && got != -COMPOSITOR_EAGAIN;
                                break;
                        }
                        drm |= compositor_uevent_drm(message, got);
                }
                if (drm)
                        return 1;
        }
}

/*
        The card taken, and waited for while no card has a screen: a machine started with its monitor off, behind a switch
        or on a dock that is not yet up has a compositor that waits for the kernel's word that a screen came (the uevent
        socket the loop listens to afterwards, opened before the first walk so that nothing said between the walk and the
        wait is missed) and takes it then; where it ended at once, was started again by its supervisor three times in two
        and a half seconds and then turned the desktop off for good. No card is held while it waits, so the screens are
        Canvas's. A uevent can be lost and a connector with no hotplug line says nothing, so the cards are also walked
        again every COMPOSITOR_SCREEN_LOOK, which asks each connector. Every other failure ends the program as before.
        The signals are not yet the loop's here: a TERM ends the wait as it ends any program.
*/
#define COMPOSITOR_SCREEN_LOOK 5000000000ull

static b32 compositor_card_open(void)
{
        int said = 0;

        compositor.uevents = compositor_uevents_open();
        for (;;)
        {
                b32 status = compositor_cards_walk();

                if (status != -COMPOSITOR_ENODEV || compositor.uevents < 0)
                        return status;
                if (!said)
                        compositor_kmsg((string_address) "compositor: no screen is connected to a card; the desktop starts when one is", 0,
                                        (string_address) "");
                said = 1;
                compositor_screen_wait(COMPOSITOR_SCREEN_LOOK);
        }
}

/*
        Variable refresh (adaptive sync, FreeSync, VRR): a screen that can waits for the next frame instead of drawing at a fixed rate, so a
        frame is on the glass as soon as it is flipped, within the panel's range, where at a fixed rate it waited for the next vblank, 0 to a
        period (0 to 6.9 ms at 144 Hz, 3.5 ms on average, on every key and every frame); and a game whose frames come at less than the mode's
        rate shows each when it is done, with no frame shown twice as a stutter. The connector says whether its screen can (vrr_capable, which
        its driver sets from the monitor's range and the link) and the crtc is told the frames are fit for it (VRR_ENABLED); the driver keeps
        the rate in the panel's range itself, and repeats a frame below its least rate (amdgpu's low framerate compensation), so the compositor
        needs no range of its own: the mode's period stays the soonest a frame can follow the last, which is what the presents are paced by
        (compositor_present_due, which needs nothing else: compositor_present_at).

        It is on only where every frame is a flip the screen can wait for: one screen (on more, the scanout spans the crtcs and only one of them
        flips), the plates flipping or a window shown straight. A frame composed into the buffer on the screen (a window on a plane, a screen
        Canvas turns, a card told by DIRTYFB) has no flip to end the wait, and at variable refresh would be shown at the panel's least rate.
        The policy is canvas vrr: on (the standard: every app's input is shown a vblank's wait sooner), games (only while a window is shown
        straight from its buffer: a fullscreen game or video, for a panel whose brightness swings with its rate, which the kernel's documents
        warn some do when the rate changes much and often, as a desktop's does between idle and typing) and off.

        The pointer is on its own plane, and a move of it is no flip: amdgpu pulls the primary plane into a commit at variable refresh for its
        refresh to come at once, but not for the cursor's legacy updates (amdgpu_dm_crtc_helper_atomic_check), so a pointer moved over an idle
        desktop was shown at the panel's least rate, 48 Hz on a 48-144 Hz panel. A move with nothing else to draw is a kick (compositor.vrr_kick):
        the screen's own picture flipped again when a present would be due, which costs a flip and no compose, and none at all when a present
        flips first (compositor_flip clears it). Each kick and each setting of the property is counted for moonwater debug (the table's vrr_*).
        The machine's owner says a screen can whatever its connector says with the file /run/moonwater/vrr-capable (looked for at the start): a
        driver that does not say, or a test of the path on a screen that cannot (amdgpu takes VRR_ENABLED for one and changes nothing).
*/
#define COMPOSITOR_OBJECT_CRTC 0xccccccccu // DRM_MODE_OBJECT_CRTC
#define COMPOSITOR_OBJ_SETPROPERTY COMPOSITOR_DRM(0xbau, 24u)
#define COMPOSITOR_VRR_STEADY 16u
static struct
{
        unsigned int policy;          // canvas vrr: 0 on, 1 games, 2 off (the registry's order)
        unsigned int crtc, connector; // what the properties were read for (0: to read again, as a take of the screens asks)
        unsigned int prop;            // the crtc's VRR_ENABLED, 0 for none
        int capable;                  // the connector's vrr_capable
        int on;                       // VRR_ENABLED as the crtc has it (read with the property, then as set)
        int refused;                  // the card refused setting it: not asked again until the properties are read again
        int said;                     // the owner says the screen can whatever its connector says (/run/moonwater/vrr-capable, looked for at the start)
        unsigned int steady;          // the presents in a row that wanted it on: it is set on after COMPOSITOR_VRR_STEADY of them
} compositor_vrr;

// The id of an object's property called name, and its value; 0 when the object has none, or the card will not say.
static unsigned int compositor_object_prop(unsigned int object, unsigned int type, const char *name, unsigned long *value)
{
        unsigned int ids[64];
        unsigned long values[64];
        struct
        {
                unsigned long props, values;
                unsigned int count, object, type;
        } got = {(unsigned long)ids, (unsigned long)values, 64, object, type};

        *value = 0;
        if (!object || system_control(compositor.card, COMPOSITOR_OBJ_GETPROPERTIES, address_of got) != 0 || got.count > 64)
                return 0;
        for (unsigned int at = 0; at < got.count; at++)
        {
                struct
                {
                        unsigned long values, enums;
                        unsigned int id, flags;
                        char name[32];
                        unsigned int count_values, count_enums;
                } property = {0, 0, ids[at], 0, {0}, 0, 0};

                if (system_control(compositor.card, COMPOSITOR_GETPROPERTY, address_of property) != 0)
                        continue;
                property.name[sizeof(property.name) - 1] = 0;
                if (string_equals((string_address)property.name, (string_address)name))
                {
                        *value = values[at];
                        return ids[at];
                }
        }
        return 0;
}

// Whether the screen is to run at variable refresh: the policy (0 on, 1 games, 2 off), a screen that can, the screens one, every frame a flip
// the screen waits for (flipping), and for games a window shown straight (direct).
static int compositor_vrr_wants(unsigned int policy, int capable, unsigned int outputs, int flipping, int direct)
{
        if (!capable || outputs != 1 || !flipping || policy >= 2)
                return 0;
        return policy == 0 || direct;
}

// What the crtc is to have, from what the present wants (want) and what it has (on): off at once, and on once COMPOSITOR_VRR_STEADY presents
// in a row wanted it (steady, counted here).
static int compositor_vrr_step(int want, int on, unsigned int *steady)
{
        *steady = want ? *steady + (*steady < COMPOSITOR_VRR_STEADY) : 0;
        return want && (on > 0 || *steady >= COMPOSITOR_VRR_STEADY);
}

// The table's word for moonwater debug: WINDOWS_VRR_ bits and the policy from bit 8.
static void compositor_vrr_publish(void)
{
        if (!compositor.table)
                return;
        compositor.table->vrr = (compositor_vrr.capable ? WINDOWS_VRR_CAPABLE : 0u) | (compositor_vrr.prop ? WINDOWS_VRR_PROPERTY : 0u) |
                                (compositor_vrr.on > 0 ? WINDOWS_VRR_ON : 0u) | (compositor_vrr.refused ? WINDOWS_VRR_REFUSED : 0u) |
                                compositor_vrr.policy << 8;
}

// VRR_ENABLED set to on (0 or 1), with no flip in flight (the set is a commit of its own, which would wait for it); false when the card refused.
static int compositor_vrr_set(int on)
{
        struct
        {
                unsigned long value;
                unsigned int prop, object, type, pad;
        } set = {(unsigned long)on, compositor_vrr.prop, compositor_vrr.crtc, COMPOSITOR_OBJECT_CRTC, 0};
        positive began = clock_monotonic_nanoseconds();

        if (system_control(compositor.card, COMPOSITOR_OBJ_SETPROPERTY, address_of set) != 0)
                return 0;
        compositor_vrr.on = on;
        if (compositor.table)
                compositor.table->vrr_set_us = (unsigned int)((clock_monotonic_nanoseconds() - began) / 1000);
        return 1;
}

/*
        display off and on: every screen's connector set to DPMS off (3) or on (0), which the card does as its crtc
        put out and lit again with what it showed. The rules say it (display off, display on) by the file
        /run/moonwater/display-off, made and removed (compositor_dark_look); a key, a press or a move of the pointer
        lights the screens again and removes the file, as any desktop's screen comes back.
*/
#define COMPOSITOR_DARK_FILE "/run/moonwater/display-off"
static void compositor_dark(int dark)
{
        int was = compositor.dark;

        if (was == dark && (!dark || compositor.dark_applied))
                return;
        compositor.dark = dark, compositor.dark_applied = dark;
        for (unsigned int at = 0; at < compositor.outputs && at < WINDOWS_OUTPUTS; at++)
        {
                unsigned long value;
                unsigned int prop = compositor_object_prop(compositor.output[at].connector, COMPOSITOR_OBJECT_CONNECTOR, "DPMS", &value);
                struct
                {
                        unsigned long value;
                        unsigned int prop, object, type, pad;
                } set = {dark ? 3ul : 0ul, prop, compositor.output[at].connector, COMPOSITOR_OBJECT_CONNECTOR, 0};

                if (prop)
                        (void)system_control(compositor.card, COMPOSITOR_OBJ_SETPROPERTY, address_of set);
        }
        if (!dark)
                compositor_damage_space();
        if (was != dark)
                compositor_kmsg(dark ? (string_address) "compositor: display off" : (string_address) "compositor: display on", 0,
                                (string_address) "");
}

// A mode set (a take of the screens, a crtc set where a flip was refused) has the kernel light each screen it sets (DPMS on): while display-off
// holds they are put out again, where the screens stayed lit with the file there until the next key.
static void compositor_dark_set(void)
{
        compositor.dark_applied = 0;
        if (compositor.dark)
                compositor_dark(1);
}

// Whether the file is there, looked at the start and at each change of it: opened as the other switch files are
// (compositor.c's /run/moonwater ones), which every build of this file has.
static void compositor_dark_look(void)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address)COMPOSITOR_DARK_FILE, FILE_READ | O_CLOEXEC | O_NOFOLLOW);

        if (handle >= 0)
                system_close(handle);
        compositor_dark(handle >= 0);
}

// A person at the machine: the screens lit again.
static void compositor_dark_woken(void)
{
        if (!compositor.dark)
                return;
        (void)system_remove_at(AT_FDCWD, (string_address)COMPOSITOR_DARK_FILE, 0);
        compositor_dark(0);
}

/*
        The screen's variable refresh as the policy and the presents want it, before each present and at a change of the setting: the properties
        read when the screen's crtc or connector is another than they were read for (and again after every take of the screens), and the crtc
        set only when what it has is not what is wanted, so a crtc another program left on is set off where it is not wanted. Off at once, for a
        frame with no flip at variable refresh waits for the panel's least rate; on only after COMPOSITOR_VRR_STEADY presents in a row that
        wanted it (a ninth of a second at 144 Hz), so frames that go on and off a plane do not set the crtc at every present, each set being a
        commit of its own. Not while a flip is in flight: the next present asks again.
*/
static void compositor_vrr_keep(void)
{
        unsigned long value;
        int want, flipping;

        if (compositor.card < 0 || !compositor.crtc)
                return;
        if (compositor_vrr.crtc != compositor.crtc || compositor_vrr.connector != compositor.connector)
        {
                compositor_vrr.crtc = compositor.crtc, compositor_vrr.connector = compositor.connector;
                compositor_vrr.capable = (compositor_object_prop(compositor.connector, COMPOSITOR_OBJECT_CONNECTOR, "vrr_capable", &value) && value) ||
                                         compositor_vrr.said;
                compositor_vrr.prop = compositor_object_prop(compositor.crtc, COMPOSITOR_OBJECT_CRTC, "VRR_ENABLED", &value);
                compositor_vrr.on = compositor_vrr.prop && value;
                compositor_vrr.refused = 0;
        }
        flipping = compositor.direct_row >= 0 || (compositor_plate_wanted(compositor_turning_software()) && compositor_spare.fb);
        want = compositor_vrr_wants(compositor_vrr.policy, compositor_vrr.capable, (unsigned int)compositor_output_count(), flipping,
                                    compositor.direct_row >= 0);
        want = compositor_vrr_step(want, compositor_vrr.on, &compositor_vrr.steady);
        if (compositor_vrr.prop && !compositor_vrr.refused && want != compositor_vrr.on && !compositor.flip_pending)
                compositor_vrr.refused = !compositor_vrr_set(want);
        if (compositor_vrr.on <= 0)
                compositor.vrr_kick = 0;
        compositor_vrr_publish();
}

// The compositor ends: a crtc it set on is set off, so the console or the next desktop is not left waiting for flips it never makes.
static void compositor_vrr_off(void)
{
        if (compositor.card >= 0 && compositor_vrr.prop && compositor_vrr.on > 0 && compositor_vrr.crtc == compositor.crtc)
                compositor_vrr_set(0);
}

// A take of the screens: the properties read again at the next present, for a screen plugged in where another was is another screen.
static void compositor_vrr_forget(void)
{
        compositor_vrr.crtc = compositor_vrr.connector = 0;
}

// The pointer moved on its plane: at variable refresh the screen is flipped again for it, unless a present flips first (compositor_flip).
static void compositor_vrr_moved(void)
{
        if (compositor_vrr.on > 0)
                compositor.vrr_kick = 1;
}

/*
        The kick, when a present would be due (the loop): the picture on the screen flipped to again, the compositor's own or a window's shown
        straight (its row and the buffer its present flipped to), with no compose. Not with a flip in flight: its landing is the next chance.
*/
static void compositor_vrr_kick(void)
{
        int row = compositor.direct_row;

        if (compositor_vrr.on <= 0)
                compositor.vrr_kick = 0;
        if (!compositor.vrr_kick || compositor.flip_pending || !compositor.on_screen)
                return;
        compositor.vrr_kick = 0;
        if ((row >= 0 || compositor.on_screen == compositor.fb) &&
            compositor_flip(compositor.on_screen, row, row >= 0 ? compositor.front[row] : 0) && compositor.table)
                compositor.table->vrr_kicks++;
}

/*
        The pointer: on the card's cursor plane where it has one, and drawn into
        the scanout where it has not, as the kernel desktop does. It has none
        where the driver's plane does not take a dumb buffer (i915 and xe
        stop the pipe), where Canvas was started without one (moonwater.cursor_plane=0,
        which the lane's pixel checks need, as a screendump has no plane in it),
        and where the card refuses what it is given; a card with no plane has
        a pointer that moves and presses all the same.

        The plane holds the shape the pointer wears, at the scale of the
        desktop but no larger than it takes, in one of two buffers: a shape
        change is painted into the one not showing and set, because a driver
        that keeps its framebuffer elsewhere sends the image only when the
        framebuffer changes. The software pointer is a rectangle of damage
        where it was and where it is, and is drawn over what is composed there.
*/

// Where a value on its way is now, on the kit's ease: where it goes, and at rest there, once CP_SWAY_NS at the pace (ui_paced) have
// passed since it left (a time before it left is the time it left).
static int compositor_sway_at(struct compositor_sway *s, positive now)
{
        positive gone = now > s->at ? now - s->at : 0, span = (positive)ui_paced((b64)CP_SWAY_NS);

        if (s->at && gone >= span)
                s->at = 0;
        return s->at ? s->from + (int)((b64)(s->to - s->from) * ui_bezier_at(&ui_ease, ui_progress((b64)gone, (b64)span)) / UI_ONE) : s->to;
}

// A value sent on its way from where it is now; with reduced motion it is there at once.
static void compositor_sway_to(struct compositor_sway *s, int to, positive now)
{
        if (to == s->to)
                return;
        s->from = compositor_sway_at(s, now);
        s->to = to;
        s->at = compositor.table && compositor.table->motion ? 0 : now;
}

// Whether the pointer is the dynamic one now: the style says so, or the last input was a touch (touch.hidden), which always is.
static int compositor_pointer_dynamic(void)
{
        return compositor.cursor.style || compositor.touch.hidden;
}

/*
        The pointer as it is to be drawn now: its bitmap or the dynamic shape, at its size (compositor_cursor_size), smaller or larger
        by what a held button has it at, and no larger than the largest picture (CP_SPAN_MOST) holds it. The dynamic shape is on its way
        to the rectangle while the pointer is over text.
*/
static void compositor_pointer_now(struct compositor_pointer *p)
{
        // The values are read at the frame the clock is in, so the picture changes once a frame however often a moving hand asks for it.
        positive period = compositor.period ? compositor.period : 16000000ull, now = clock_monotonic_nanoseconds() / period * period;
        int morph, grow, scale, most, resize;

        // A window's client's own picture is worn as it was copied: no size of the settings', and no squeeze of a held button.
        if (compositor_picture.worn)
        {
                *p = (struct compositor_pointer){.shape = CP_ARROW, .scale = 256, .span = compositor_picture.span, .hot_x = compositor_picture.hot_x,
                                                 .hot_y = compositor_picture.hot_y, .picture = compositor_picture.worn};
                return;
        }
        compositor_sway_to(&compositor.cursor.morph, compositor.cursor.dyn && compositor.cursor.shape == CP_TEXT ? 256 : 0, now);
        morph = compositor_sway_at(&compositor.cursor.morph, now);
        grow = compositor_sway_at(&compositor.cursor.stretch, now) * CP_DYN_STRETCH >> 8;
        // Over a window's edge (a resize) the pointer keeps its size whatever is held: a drag of the edge neither grows it nor eases it.
        resize = compositor.cursor.shape >= CP_RESIZE_H && compositor.cursor.shape <= CP_RESIZE_NESW;
        scale = resize ? compositor_cursor_size() : (compositor_cursor_size() * (256 + compositor_sway_at(&compositor.cursor.press, now)) + 128) >> 8;
        most = (CP_SPAN_MOST << 8) / (compositor.cursor.dyn ? CP_DYN_CELL : CP_CELL);
        *p = (struct compositor_pointer){.dyn = compositor.cursor.dyn, .scale = scale < most ? scale : most};
        if (!p->dyn)
        {
                p->shape = compositor.cursor.shape < CP_SHAPES ? compositor.cursor.shape : CP_ARROW;
                return;
        }
        // Over a window's edge the shape is the lens along the way the edge moves (45 is 64 over the square root of two), never stretched.
        if (resize)
        {
                static const signed char way[4][2] = {{64, 0}, {0, 64}, {45, 45}, {45, -45}};

                p->lens = 1;
                p->hw = 256 * CP_DYN_LENS_W, p->hh = 256 * CP_DYN_LENS_H, p->c = 0;
                p->ux = way[compositor.cursor.shape - CP_RESIZE_H][0], p->uy = way[compositor.cursor.shape - CP_RESIZE_H][1];
                return;
        }
        p->hw = 256 * CP_DYN_RADIUS + (CP_DYN_TEXT_W - CP_DYN_RADIUS) * morph;
        p->hh = 256 * CP_DYN_RADIUS + (CP_DYN_TEXT_H - CP_DYN_RADIUS) * morph;
        p->c = 256 * CP_DYN_RADIUS + (CP_DYN_TEXT_R - CP_DYN_RADIUS) * morph;
        // A stretch never takes the shape's long side past its square (CP_DYN_CELL): the rectangle over text is the longest.
        {
                int longest = p->hw > p->hh ? p->hw : p->hh, room = (CP_DYN_CELL / 2 * 256) * 256 / longest - 256;

                grow = grow < room ? grow : room > 0 ? room : 0;
        }
        p->grow = grow;
        p->ux = grow ? compositor.cursor.ux : 0, p->uy = grow ? compositor.cursor.uy : 0;
}

/*
        A button held or let go. While any button is down the pointer is a little larger, by half the squeeze the settings give
        (`moonwater canvas cursor squeeze`, 0 to 50 percent, 25 by default, so 12.5 percent), in every style and shape but a window's edge,
        which keeps its size; with none down it
        is its own size, and it eases from one to the other. `moonwater canvas cursor click off` leaves it its size.
*/
static void compositor_pointer_press(unsigned int code, int down)
{
        unsigned int bit = 1u << ((code - COMPOSITOR_BTN_LEFT) & 7);
        int by = compositor.cursor.click_off ? 0 : compositor.cursor.squeeze * 256 / 100;

        compositor.cursor.held = down ? compositor.cursor.held | bit : compositor.cursor.held & ~bit;
        compositor_sway_to(&compositor.cursor.press, !compositor.cursor.held ? 0 : by / 2, clock_monotonic_nanoseconds());
        compositor_cursor_move();
}

/*
        A motion of the pointer, for the dynamic shape's stretch: its speed, taken over CP_DYN_SAMPLE_NS of motion or more, in logical
        pixels a second, is how far it is stretched (compositor_pointer_now), along the way it went. A motion with no movement (a button,
        a redraw) changes nothing, and the stretch lets go CP_DYN_IDLE_NS after the last one that had some (compositor_cursor_tick).
*/
static void compositor_cursor_moved(positive now)
{
        long long dx = compositor.pointer_x - compositor.cursor.last_x, dy = compositor.pointer_y - compositor.cursor.last_y, far, speed;
        positive took = now - compositor.cursor.last_ns;

        if ((!dx && !dy) || took < CP_DYN_SAMPLE_NS)
                return;
        compositor.cursor.last_x = compositor.pointer_x, compositor.cursor.last_y = compositor.pointer_y, compositor.cursor.last_ns = now;
        far = (long long)compositor_isqrt((unsigned long long)(dx * dx + dy * dy));
        speed = far * 1000000000ll / (long long)took * 100 / (compositor.scale > 0 ? compositor.scale : 100);
        speed = speed < CP_DYN_SLOW ? 0 : speed > CP_DYN_FAST ? 256 : (speed - CP_DYN_SLOW) * 256 / (CP_DYN_FAST - CP_DYN_SLOW);
        if (speed)
                compositor.cursor.ux = (int)(dx * 64 / far), compositor.cursor.uy = (int)(dy * 64 / far);
        compositor_sway_to(&compositor.cursor.stretch, (int)speed, now);
        compositor.cursor.idle_until = now + CP_DYN_IDLE_NS;
}

// When the pointer is to be drawn again for its own reasons: the next frame while a value of it is on its way, or when its stretch lets
// go (0 for neither). The main loop sleeps until then.
static positive compositor_cursor_due(void)
{
        if (compositor.cursor.press.at || compositor.cursor.morph.at || compositor.cursor.stretch.at)
                return clock_monotonic_nanoseconds() + (compositor.period ? compositor.period : 16000000ull);
        return compositor.cursor.stretch.to ? compositor.cursor.idle_until : 0;
}

// The pointer's own times that are up: a stretch nothing has moved under lets go, and a value on its way is drawn where it is now.
static void compositor_cursor_tick(positive now)
{
        if (compositor.cursor.stretch.to && now >= compositor.cursor.idle_until)
                compositor_sway_to(&compositor.cursor.stretch, 0, now);
        if (compositor.cursor.press.at || compositor.cursor.morph.at || compositor.cursor.stretch.at)
                compositor_cursor_move();
}

// The square of the scene the pointer drawn into it covers is damaged.
static void compositor_cursor_damage(void)
{
        int hot_x, hot_y, span = compositor_pointer_span(&compositor.cursor.drawn_as, &hot_x, &hot_y);

        compositor.damage_by = COMPOSITOR_OVER;
        compositor_damage(compositor.cursor.x - hot_x, compositor.cursor.y - hot_y, span, span);
        compositor.damage_by = 0;
}

/*
        The cursor plane's probe, made where the machine's owner asks for it (COMPOSITOR_CURSOR_PROBE_FILE there at the pointer's start).
        i915 and xe are refused the plane by their names, for the reason 23811f25 gives in four words ("stop their pipe on one", a
        dumb buffer on the plane), with no machine or log behind it, and there is none here to try it on. Asked, the names are not
        refused: the plane is given its first picture and its crtc asked for one vblank event, waited for COMPOSITOR_CURSOR_PROBE_NS.
        One that comes is a pipe that runs on with the plane, which is kept; none is the pipe the note means, and the plane is taken
        off and the pointer drawn, which the log and `moonwater debug`'s pointer line say. A card that cannot be asked for a vblank
        tells nothing, and the names' refusal stands for it. Without the file all is as it was; the names go only once the probe
        has been run on such a machine and shown to be enough alone.
*/
#define COMPOSITOR_CURSOR_PROBE_FILE "/run/moonwater/cursor-plane-probe"
#define COMPOSITOR_CURSOR_PROBE_NS 100000000ull
#define COMPOSITOR_WAIT_VBLANK COMPOSITOR_DRM(0x3au, 24u)
#define COMPOSITOR_EVENT_VBLANK 1u
#define COMPOSITOR_CURSOR_PROBE_TAG 0x65626f7270ull // the probe's vblank, by its user data

static struct
{
        int asked;   // the file was there at the pointer's start
        int done;    // the probe has been made
        int stopped; // and the plane was taken off for it
} compositor_cursor_probe;

// Whether the owner asks for the probe: the file is there.
static int compositor_cursor_probe_asked(void)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address)COMPOSITOR_CURSOR_PROBE_FILE, FILE_READ | O_CLOEXEC);

        if (handle < 0)
                return 0;
        system_close(handle);
        return 1;
}

// Whether the card's driver is one refused the plane by name.
static int compositor_cursor_named(void)
{
        const char *name = compositor.driver;

        return (name[0] == 'i' && name[1] == '9' && name[2] == '1' && name[3] == '5' && !name[4]) || (name[0] == 'x' && name[1] == 'e' && !name[2]);
}

/*
        One vblank event of the crtc at crtc_index asked for (DRM_IOCTL_WAIT_VBLANK, relative, an event) and waited for, at most
        COMPOSITOR_CURSOR_PROBE_NS: 1 when it came, 0 when none did, -1 when the card would not be asked. An event of another kind read
        meanwhile (a flip's) is handled as the loop handles it.
*/
static int compositor_cursor_vblank(unsigned int crtc_index)
{
        struct
        {
                unsigned int type, sequence;
                unsigned long signal, pad;
        } ask = {0x1u | 0x4000000u | (crtc_index << 1 & 0x3eu), 1, COMPOSITOR_CURSOR_PROBE_TAG, 0}; // _DRM_VBLANK_RELATIVE | _EVENT, the high crtc
        positive until;

        if (compositor.card < 0 || system_control(compositor.card, COMPOSITOR_WAIT_VBLANK, address_of ask) != 0)
                return -1;
        until = clock_monotonic_nanoseconds() + COMPOSITOR_CURSOR_PROBE_NS;
        for (;;)
        {
                positive now = clock_monotonic_nanoseconds();
                struct compositor_poll wanted = {compositor.card, COMPOSITOR_POLLIN, 0};
                long wait[2];
                p8 events[512];
                long got;

                if (now >= until)
                        return 0;
                wait[0] = (long)((until - now) / 1000000000ull);
                wait[1] = (long)((until - now) % 1000000000ull);
                if (compositor_call(syscall(ppoll), (positive)&wanted, 1, (positive)wait, 0, 0, 0) <= 0)
                        continue;
                got = compositor_call(syscall(read), (positive)compositor.card, (positive)events, sizeof(events), 0, 0, 0);
                if (got <= 0)
                        continue;
                compositor_card_events(events, got);
                for (long at = 0; at + 16 <= got;)
                {
                        unsigned int type, length;
                        unsigned long data;

                        memory_copy(address_of type, events + at, 4);
                        memory_copy(address_of length, events + at + 4, 4);
                        if (length < 8 || at + (long)length > got)
                                break;
                        memory_copy(address_of data, events + at + 8, 8);
                        if (type == COMPOSITOR_EVENT_VBLANK && data == COMPOSITOR_CURSOR_PROBE_TAG)
                                return 1;
                        at += length;
                }
        }
}

// What the probe found (compositor_cursor_vblank's answer): 0 when the plane is kept, -1 when it is to be taken off, each said.
static int compositor_cursor_probed(int answer)
{
        compositor_cursor_probe.done = 1;
        if (answer > 0 || (answer < 0 && !compositor_cursor_named()))
        {
                if (answer > 0)
                        log_direct(str("compositor: the cursor plane's probe: the screen's vblank came with the plane showing; the plane is kept\n"));
                else
                        log_direct(str("compositor: the cursor plane's probe: the card gives no vblank to ask for; the plane is kept\n"));
                return 0;
        }
        compositor_cursor_probe.stopped = 1;
        if (answer == 0)
                log_direct(str("compositor: the cursor plane's probe: no vblank within 100 ms of the plane's first picture; the plane is taken off and the pointer drawn\n"));
        else
                log_direct(str("compositor: the cursor plane's probe: the card gives no vblank to ask for, and its driver is refused the plane by name; the pointer is drawn\n"));
        return -1;
}

// What `moonwater debug` says of the pointer (struct windows pointer_plane): 1 on the plane, 0 drawn, 2 drawn because the probe took the plane off.
static unsigned int compositor_pointer_plane_word(void)
{
        return !compositor.cursor.soft ? 1u : compositor_cursor_probe.stopped ? 2u : 0u;
}

// Whether this card's cursor plane is worth using: the driver's name (unless the probe is asked for) and the setting Canvas was started with.
static int compositor_cursor_plane_wanted(void)
{
        p8 setting[4] = {0};
        bipolar handle;

        if (!compositor_cursor_probe.asked && compositor_cursor_named())
                return 0;

        handle = system_open_at(AT_FDCWD, (string_address) "/sys/module/moonwater/parameters/cursor_plane",
                                FILE_READ | O_CLOEXEC);
        if (handle >= 0)
        {
                bipolar got = system_read_once(handle, setting, sizeof(setting) - 1);

                system_close(handle);
                if (got > 0 && (setting[0] == 'N' || setting[0] == 'n' || setting[0] == '0'))
                        return 0;
        }
        return 1;
}

// How far a picture's corner is left of and above its plane's: none unless the picture is larger than the plane, which then shows its middle.
static int compositor_cursor_inset(int span)
{
        return span > CP_PLANE ? (span - CP_PLANE) / 2 : 0;
}

// A picture painted into the buffer of the plane that is not showing, which is the one shown from here on: paints counts
// them, and a screen's plane given the newest wears paints.
/*
        The plane's buffers are mapped once each and kept so (a stretching pointer is painted every frame it moves: a map, a zeroing and an
        unmap of the card's memory each time were most of the main loop's pointer cost on the Deck), and the shape is drawn into a cached
        scratch and written to the buffer whole, since the dumb buffer is write-combined: the draw's blending read it back uncached.
*/
static unsigned int compositor_cursor_scratch[CP_PLANE * CP_PLANE];

static unsigned int *compositor_cursor_mapped(int which)
{
        struct compositor_map_dumb map = {.handle = compositor.cursor.handle[which]};
        long mapped;

        if (compositor.cursor.mapped[which])
                return compositor.cursor.mapped[which];
        if (system_control(compositor.card, COMPOSITOR_MAP_DUMB, address_of map) != 0)
                return null;
        mapped = compositor_call(syscall(mmap), 0, CP_PLANE * CP_PLANE * 4, 3, 1, (positive)compositor.card, map.offset);
        if (mapped < 0 && mapped > -4096)
                return null;
        return compositor.cursor.mapped[which] = (unsigned int *)mapped;
}

// A buffer's mapping let go, before its handle is.
static void compositor_cursor_unmap(int which)
{
        if (compositor.cursor.mapped[which])
                compositor_call(syscall(munmap), (positive)compositor.cursor.mapped[which], CP_PLANE * CP_PLANE * 4, 0, 0, 0, 0);
        compositor.cursor.mapped[which] = null;
}

static int compositor_cursor_paint(const struct compositor_pointer *p)
{
        unsigned int *mapped = compositor_cursor_mapped(compositor.cursor.back);

        if (!mapped)
                return -1;
        memory_zero(compositor_cursor_scratch, sizeof(compositor_cursor_scratch));
        {
                struct compositor_target t = {compositor_cursor_scratch, CP_PLANE, CP_PLANE, CP_PLANE, 0, 0, CP_PLANE, CP_PLANE};
                int hot_x, hot_y, inset = compositor_cursor_inset(compositor_pointer_span(p, &hot_x, &hot_y));

                compositor_cursor_draw(&t, hot_x - inset, hot_y - inset, p, compositor_cursor_ink(0, 0xff000000u), compositor_cursor_ink(1, 0xff000000u));
        }
        memory_copy(mapped, compositor_cursor_scratch, sizeof(compositor_cursor_scratch));

        compositor.cursor.back ^= 1;
        compositor.cursor.paints++;
        compositor.cursor.plane = *p;
        compositor.cursor.plane_on = 1;
        return 0;
}

/*
        The plane is a crtc's, and the pointer is on one screen or, at an
        edge two screens share, on both: each screen's plane shows the cell
        at left, top where the cell meets its screen, at the cell's place on
        that screen, and nothing where it does not, so no screen keeps a
        pointer the hand has taken to another. A plane that wears an older
        shape is given the one painted last; the rest are moved. Answers -1
        when a plane refuses the shape.
*/
static int compositor_cursor_planes(int left, int top)
{
        unsigned int count, front = compositor.cursor.handle[compositor.cursor.back ^ 1];
        const int (*screens)[4] = compositor_screens(&count);

        for (unsigned int at = 0; at < count; at++)
        {
                struct compositor_output *output = &compositor.output[at];
                unsigned int crtc = compositor.outputs ? output->crtc : compositor.crtc;
                int x, y;

                if (compositor_screen_cell(screens[at], left, top, CP_PLANE, &x, &y))
                {
                        if (output->shows != (unsigned int)compositor.cursor.paints)
                        {
                                struct compositor_cursor give = {3, crtc, x, y, CP_PLANE, CP_PLANE, front};

                                if (system_control(compositor.card, COMPOSITOR_CURSOR, address_of give) != 0)
                                        return -1;
                                output->shows = (unsigned int)compositor.cursor.paints;
                                // The probe, asked for, after the plane's first picture: a pipe with no vblank has the plane refused.
                                if (compositor_cursor_probe.asked && !compositor_cursor_probe.done &&
                                    compositor_cursor_probed(compositor_cursor_vblank(compositor.outputs ? output->crtc_index : compositor.crtc_index)) < 0)
                                        return -1;
                        }
                        else
                        {
                                struct compositor_cursor move = {2, crtc, x, y, 0, 0, 0};

                                compositor.cursor.moves += system_control(compositor.card, COMPOSITOR_CURSOR, address_of move) == 0;
                        }
                }
                else if (output->shows)
                {
                        struct compositor_cursor gone = {1, crtc, 0, 0, CP_PLANE, CP_PLANE, 0};

                        system_control(compositor.card, COMPOSITOR_CURSOR, address_of gone);
                        output->shows = 0;
                }
        }
        return 0;
}

// Every screen's plane showing nothing; the next shape wanted is painted again.
static void compositor_cursor_planes_off(void)
{
        unsigned int count;

        compositor_screens(&count);
        for (unsigned int at = 0; at < count; at++)
        {
                struct compositor_cursor gone = {1, compositor.outputs ? compositor.output[at].crtc : compositor.crtc, 0, 0,
                                                 CP_PLANE, CP_PLANE, 0};

                system_control(compositor.card, COMPOSITOR_CURSOR, address_of gone);
                compositor.output[at].shows = 0;
        }
        compositor.cursor.plane_on = 0;
}

static void compositor_cursor_start(void)
{
        struct compositor_dumb dumb[2] = {{CP_PLANE, CP_PLANE, 32, 0, 0, 0, 0}, {CP_PLANE, CP_PLANE, 32, 0, 0, 0, 0}};

        compositor.cursor.plane_on = 0;
        compositor_cursor_probe.asked = compositor_cursor_probe_asked();
        compositor_cursor_probe.done = compositor_cursor_probe.stopped = 0;
        if (compositor.card < 0 || !compositor_cursor_plane_wanted() ||
            system_control(compositor.card, COMPOSITOR_CREATE_DUMB, address_of dumb[0]) != 0)
        {
                compositor.cursor.soft = compositor.card >= 0;
                return;
        }
        if (system_control(compositor.card, COMPOSITOR_CREATE_DUMB, address_of dumb[1]) != 0)
        {
                // The first buffer is given back: nothing holds it yet.
                unsigned int close[2] = {dumb[0].handle, 0};

                system_control(compositor.card, COMPOSITOR_GEM_CLOSE, address_of close);
                compositor.cursor.soft = 1;
                return;
        }

        compositor.cursor.handle[0] = dumb[0].handle;
        compositor.cursor.handle[1] = dumb[1].handle;
}

// Where the pointer is drawn: the pointer, which is already where it will be (compositor_pointer_shown).
static int compositor_drawn_x(void)
{
        return compositor.pointer_x;
}

static int compositor_drawn_y(void)
{
        return compositor.pointer_y;
}

/*
        The pointer where it is and as it is now. Drawn into the scanout, it is a square of damage where it was and where it is, and is
        drawn over what is composed there; on the plane, the plane is moved, and painted first when the picture is not the one it wears (a
        value of the dynamic shape or of a held button on its way is a new picture each frame). The classic pointer and the dynamic one
        are two pictures like any other two, and so is a picture a window's client drew (compositor_picture), the same copy on the plane
        and in the scene. A window's client that asked for no pointer has none.
*/
static void compositor_cursor_move(void)
{
        struct compositor_pointer now;
        int hot_x, hot_y, inset;

        int hidden = compositor.cursor.client_hidden || compositor.cursor.hold_hidden;

        compositor.cursor.dyn = compositor_pointer_dynamic();
        compositor_pointer_now(&now);
        if (compositor.cursor.drawn)
                compositor_cursor_damage(), compositor.cursor.drawn = 0;
        if (hidden || compositor_cursor_in_scene())
        {
                // A plane that was showing the pointer shows nothing now, and says so again when it is wanted.
                if (compositor.cursor.handle[0] && compositor.cursor.plane_on)
                        compositor_cursor_planes_off();
                if (!hidden)
                {
                        compositor.cursor.x = compositor_drawn_x(), compositor.cursor.y = compositor_drawn_y();
                        compositor.cursor.drawn_as = now, compositor.cursor.drawn = 1;
                        compositor_cursor_damage();
                }
                return;
        }
        if (!compositor.cursor.handle[0])
                return;
        inset = compositor_cursor_inset(compositor_pointer_span(&now, &hot_x, &hot_y));
        if ((compositor.cursor.plane_on && !memory_compare(&now, &compositor.cursor.plane, sizeof(now))) || compositor_cursor_paint(&now) == 0)
                if (compositor_cursor_planes(compositor_drawn_x() - hot_x + inset, compositor_drawn_y() - hot_y + inset) == 0)
                {
                        compositor_vrr_moved();
                        return;
                }

        // During wake retrain, a transient cursor refusal must not permanently disable the hardware cursor plane.
        if (compositor.wake_stage)
                return;

        // The card will not take it: the pointer is drawn from here on, and the dumb buffers are released.
        compositor_cursor_planes_off();
        for (int i = 0; i < 2; i++)
                if (compositor.cursor.handle[i])
                {
                        unsigned int close[2] = {compositor.cursor.handle[i], 0};

                        compositor_cursor_unmap(i);
                        system_control(compositor.card, COMPOSITOR_GEM_CLOSE, address_of close);
                        compositor.cursor.handle[i] = 0;
                }
        compositor.cursor.refused++;
        compositor.cursor.soft = 1;
        compositor_cursor_move();
}

