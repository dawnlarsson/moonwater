/* KMS outputs, displays config, scanout buffers, hotplug/VRR/DPMS, cursor plane, and native DRM/syncobj helpers. */
static void compositor_kmsg(string_address text, positive value, string_address tail);
static void compositor_text(const char *path, char *text, unsigned int room);
static int compositor_turn_buffer(unsigned int at, unsigned int width, unsigned int height, struct drm_dumb *retired);
static void compositor_turn_retire(const struct drm_dumb *retired);
static void compositor_cursor_move(void);
static void compositor_arrange_on(int row, unsigned int how, int on);
static void compositor_place(int row, int x, int y);
static void compositor_room(const int *screen, int *room);
static void compositor_arrange_rect(int row, unsigned int how, const int *rect);
static void compositor_room_follow(void);
static void compositor_vrr_forget(void);
static int compositor_virtual(void);
static unsigned int compositor_fb_over(unsigned int handle);
static void compositor_plate_outs_drop(void);

static struct windows_display compositor_display[WINDOWS_DISPLAYS];
static unsigned int compositor_displays;

static unsigned int compositor_said(char *line, unsigned int at, const char *text)
{
        for (unsigned int each = 0; text[each] && at < 127; each++)
                line[at++] = text[each];
        return at;
}

#define COMPOSITOR_MODE_INTERLACED 48u // DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN

// Mode refresh period in ns from timings (fallback: 1e9 / refresh).
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

// The cadence's rate for a screen at width by height: the highest rate it lists at that size, progressive, that is a multiple of hz (40 on a
// screen of 144 120 100 85 60 50 is 120; 48 is 144, the rate it has). 0 when it lists none.
static unsigned int compositor_cadence_rate(const struct compositor_mode *modes, unsigned int count, unsigned int width, unsigned int height,
                                            unsigned int hz)
{
        unsigned int best = 0;

        for (unsigned int at = 0; hz && at < count; at++)
                if (modes[at].hdisplay == width && modes[at].vdisplay == height && !(modes[at].flags & COMPOSITOR_MODE_INTERLACED) &&
                    modes[at].vrefresh && modes[at].vrefresh % hz == 0 && modes[at].vrefresh > best)
                        best = modes[at].vrefresh;
        return best;
}

static void compositor_refresh_pick(const char *name, unsigned int count, const struct compositor_mode *modes,
                                    struct compositor_mode *mode)
{
        const struct windows_display *display = windows_display_of(compositor_display, compositor_displays, name,
                                                                   windows_name_length(name));
        unsigned int rates[128][3], cap = count < 128 ? count : 128, at, asked = display ? display->refresh : 0;
        char line[160];
        unsigned int length;

        for (at = 0; at < cap; at++)
                rates[at][0] = modes[at].hdisplay, rates[at][1] = modes[at].vdisplay, rates[at][2] = modes[at].vrefresh;
        if (compositor_cadence.active && compositor_cadence.hz)
        {
                unsigned int rate = compositor_cadence_rate(modes, cap, mode->hdisplay, mode->vdisplay, compositor_cadence.hz);

                compositor_cadence.rate = rate;
                if (rate)
                        asked = rate;
        }
        at = asked ? windows_mode_at(rates, cap, mode->hdisplay, mode->vdisplay, asked)
                   : compositor_fastest_at(modes, cap, mode->hdisplay, mode->vdisplay);
        for (unsigned int forced = 0; !asked && forced < cap; forced++)
                if ((modes[forced].type & COMPOSITOR_MODE_USERDEF) && rates[forced][0] == mode->hdisplay &&
                    rates[forced][1] == mode->vdisplay)
                {
                        at = forced;
                        break;
                }
        if (at < cap)
        {
                *mode = modes[at];
                return;
        }
        if (!asked)
                return;
        length = compositor_said(line, compositor_said(line, compositor_said(line, 0, "compositor: canvas.displays: no mode of "), name), " at ");
        length += positive_into_string((p8 *)line + length, display->refresh);
        line[compositor_said(line, length, " Hz, kept the mode it has")] = 0;
        compositor_kmsg((string_address)line, 0, (string_address) "");
}

static void compositor_turn_say(const char *name, unsigned int degrees, int hardware)
{
        char line[160];
        unsigned int length = compositor_said(line, compositor_said(line, compositor_said(line, 0, "compositor: canvas.displays: "), name), " turned ");

        length += positive_into_string((p8 *)line + length, degrees);
        line[compositor_said(line, length, hardware ? " degrees by hardware rotation" : " degrees by software rotation")] = 0;
        compositor_kmsg((string_address)line, 0, (string_address) "");
}

static int compositor_display_scale(const char *name)
{
        const struct windows_display *display = windows_display_of(compositor_display, compositor_displays, name,
                                                                   windows_name_length(name));

        return display && display->scale ? (int)display->scale : compositor.boot_scale ? compositor.boot_scale : WINDOWS_SCALE_DEFAULT;
}

static int compositor_output_scale(int at)
{
        return at >= 0 && at < (int)compositor.outputs ? compositor.output[at].scale : compositor.scale;
}

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

static unsigned int *compositor_connector_ids;
static unsigned long compositor_connector_bytes;
static struct compositor_mode *compositor_modes_list;
static unsigned long compositor_modes_bytes;

static void *kms_grow_list(void **ptr, unsigned long *bytes, long want, long min_n, unsigned long elem)
{
        unsigned long n = (unsigned long)(want < min_n ? min_n : want);

        if (!elem || n > (1ul << 28) / elem)
                return 0;
        return mem_buf_grow(ptr, bytes, n * elem, MEM_BUF_GROW_2X) ? *ptr : 0;
}

static unsigned int *compositor_connectors_room(long want)
{
        void *p = compositor_connector_ids;
        unsigned int *out = (unsigned int *)kms_grow_list(&p, &compositor_connector_bytes, want, 64, sizeof(unsigned int));

        if (out)
                compositor_connector_ids = out;
        return out;
}

static struct compositor_mode *compositor_modes_room(long want)
{
        void *p = compositor_modes_list;
        struct compositor_mode *out = (struct compositor_mode *)kms_grow_list(&p, &compositor_modes_bytes, want, 128, sizeof(struct compositor_mode));

        if (out)
                compositor_modes_list = out;
        return out;
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
        static unsigned char edid[COMPOSITOR_EDID_MOST]; // 32 KiB: off the stack
        unsigned long blob_id = 0;
        struct
        {
                unsigned int blob_id, length;
                unsigned long data;
        } blob;

        if (room)
                name[0] = 0;
        if (!drm_obj_get_prop(connector, COMPOSITOR_OBJECT_CONNECTOR, "EDID", &blob_id) || !blob_id)
                return 0;
        /*
                The kernel copies a blob only when the length asked is the blob's own (drm_mode_getblob_ioctl), so a
                length of 256 read only a monitor whose EDID is one extension long: its length first, then all of
                it, when it is whole blocks and no more than a base block and 255 extensions.
        */
        blob.blob_id = (unsigned int)blob_id, blob.length = 0, blob.data = 0;
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
        struct drm_dumb b;
        b32 status;

        if (width <= 0 || height <= 0)
                return -COMPOSITOR_EINVAL;
        status = drm_dumb_create(&b, (unsigned int)width, (unsigned int)height, DRM_DUMB_FB | DRM_DUMB_MAP);

        if (status < 0)
                return status;
        *fb_id = b.fb;
        *handle = b.handle;
        *pixels = b.pixels;
        *bytes = b.bytes;
        *pitch = b.pitch;
        return 0;
}

// A scanout no crtc shows any more, given back.
static void compositor_scanout_free(unsigned int fb, unsigned int handle, unsigned int *pixels, unsigned long bytes)
{
        struct drm_dumb b = {fb, handle, pixels, bytes, 0, 0, 0};

#ifdef MOONWATER_GPU_C
        // The GPU's import of it goes with it: the card hands a buffer made later the lowest free handle, this one's, and the import (which keeps this
        // buffer alive) is known by the handle, so the GPU would go on drawing into this one while the screen shows that.
        for (unsigned int at = 0; at < 2; at++)
                if (compositor_gpu_state.cache[at] && compositor_gpu_state.cache_key[at] == handle)
                {
                        gpu_unimport_gen(compositor_gpu_state.cache[at], compositor_gpu_state.cache_gen[at], GPU_GONE_UNIMPORT);
                        compositor_gpu_state.cache[at] = compositor_gpu_state.cache_key[at] = 0;
                        if (compositor_gpu_state.out_key == handle)
                                compositor_gpu_state.out = compositor_gpu_state.out_key = 0;
                }
        for (unsigned int at = 0; at < WINDOWS_OUTPUTS; at++)
                if (compositor_gpu_state.turn_key[at] == handle && compositor_gpu_state.turn_va[at])
                        gpu_unimport_gen(compositor_gpu_state.turn_va[at], compositor_gpu_state.turn_gen[at], GPU_GONE_UNIMPORT), compositor_gpu_state.turn_va[at] = 0;
#endif
        drm_dumb_destroy(&b);
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
        // Screens two or more: each screen of its own shows the spare too, over the same buffer, as it shows the scanout.
        for (unsigned int at = 1; at < compositor.outputs && at < WINDOWS_OUTPUTS; at++)
        {
                compositor_plate_out[at][1] = compositor_fb_over(handle);
                if (!compositor_plate_out[at][1])
                {
                        compositor_plate_outs_drop();
                        compositor_scanout_free(fb, handle, pixels, bytes);
                        compositor_plate_refused = 1;
                        return;
                }
        }
        compositor_spare.fb = fb, compositor_spare.handle = handle, compositor_spare.pixels = pixels;
        compositor_spare.bytes = bytes, compositor_spare.pitch = pitch;
        compositor_stale_all(compositor_spare.stale, &compositor_spare.stale_any, &compositor_spare_parts);
}

// The spare given back (the scanout is replaced: a space of another size, or the second screen). Not shown: no flip is in flight then.
static void compositor_plate_drop(void)
{
        compositor_plate_outs_drop();
        if (compositor_spare.fb && compositor.card >= 0)
                compositor_scanout_free(compositor_spare.fb, compositor_spare.handle, compositor_spare.pixels, compositor_spare.bytes);
        memory_zero(&compositor_spare, sizeof(compositor_spare));
        compositor_plate_refused = 0;
}

// Another framebuffer of the scanout, as large and over the same buffer, for a screen of its own; 0 when the card will
// not make one, and that screen shares the scanout's.
static unsigned int compositor_fb_over(unsigned int handle)
{
        struct compositor_fb fb = {0, (unsigned int)compositor.width, (unsigned int)compositor.height,
                                   (unsigned int)compositor.scanout_pitch, 32, 24, handle};

        return system_control(compositor.card, COMPOSITOR_ADDFB, address_of fb) == 0 ? fb.fb_id : 0;
}

static unsigned int compositor_scanout_fb(void)
{
        return compositor_fb_over(compositor.scanout_handle);
}

// The framebuffers of the spare plate that the screens of their own show (plate_out[at][1]), given back.
static void compositor_plate_outs_drop(void)
{
        for (unsigned int at = 1; at < WINDOWS_OUTPUTS; at++)
        {
                if (compositor_plate_out[at][1] && compositor.card >= 0)
                        system_control(compositor.card, COMPOSITOR_RMFB, address_of compositor_plate_out[at][1]);
                compositor_plate_out[at][1] = 0;
        }
}

/*
        The framebuffer a screen Canvas turns in software is drawn into, at the panel's size: one for each screen, kept
        while the size is the same. A buffer whose size changes is handed back in retired, and given back once the
        crtcs show the new one (compositor_turn_retire). False when the card gives none.
*/
static int compositor_turn_buffer(unsigned int at, unsigned int width, unsigned int height, struct drm_dumb *retired)
{
        struct drm_dumb *turn = &compositor_turns[at];

        if (turn->fb && turn->width == width && turn->height == height)
                return 1;
        if (turn->fb)
                *retired = *turn;
        return drm_dumb_create(turn, width, height, DRM_DUMB_FB | DRM_DUMB_MAP) == 0;
}

// A buffer given back, which no crtc shows any more.
static void compositor_turn_retire(const struct drm_dumb *retired)
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
        return (drm_driver_flags(compositor.driver) & DRV_VIRTUAL) != 0;
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
        compositor_grab_end(COMPOSITOR_GRAB_COMMIT, COMPOSITOR_NONE);
        compositor_assist_close();
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
        }
        // Align per-output Spaces first so fullscreen row re-arrangement targets the updated screen index.
        compositor_spaces_on_outputs_changed(names, count);
        // One that filled a screen's room fills the room of the screen it is on now, the band left out.
        for (unsigned int at = 0; at < compositor.stacked; at++)
        {
                int row = (int)compositor.stack[at];
                int to;

                if (row < 0 || !compositor.showing[row])
                        continue;
                to = compositor_screen_of(row);
                if (to < 0 || (unsigned int)to >= now_count)
                        to = 0;
                if (compositor.arranged[row] == COMPOSITOR_ROOM)
                {
                        int room[4];

                        compositor_room(now[to], room);
                        compositor_arrange_rect(row, COMPOSITOR_ROOM, room);
                }
                else if (compositor.arranged[row] != COMPOSITOR_FLOATING)
                        compositor_row_mode(row, compositor.arranged[row], compositor.fullscreen[row], to);
                else
                        compositor_place(row, compositor.x[row], compositor.y[row]);
        }
        compositor_screen_keep_point(now, now_count, nowhere, nowhere, &compositor.real_x, &compositor.real_y);
        compositor.kept_x = compositor.real_x;
        compositor.kept_y = compositor.real_y;
        compositor_pointer_shown();
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

static void kms_snapshot_outputs(int (*was)[4], char (*names)[WINDOWS_OUTPUT_NAME], unsigned int *crtc, unsigned int *count)
{
        unsigned int n = compositor.outputs < WINDOWS_OUTPUTS ? compositor.outputs : WINDOWS_OUTPUTS;

        for (unsigned int at = 0; at < n; at++)
        {
                memory_copy(was[at], compositor.screen[at], sizeof(was[at]));
                memory_copy(names[at], compositor.output[at].name, WINDOWS_OUTPUT_NAME);
                if (crtc)
                        crtc[at] = compositor.output[at].crtc;
        }
        if (count)
                *count = n;
}

// Apply scale-only changes from canvas.displays without a modeset; returns 1 if any changed.
static int compositor_scales_take(void)
{
        int was[WINDOWS_OUTPUTS][4], changed = 0;
        char names[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_NAME];

        kms_snapshot_outputs(was, names, 0, 0);
        for (unsigned int at = 0; at < compositor.outputs && at < WINDOWS_OUTPUTS; at++)
        {
                int scale = compositor_display_scale(compositor.output[at].name);

                changed |= scale != compositor.output[at].scale;
                compositor.output[at].scale = scale;
        }
        if (!changed)
                return 0;
        compositor.layout++;
        compositor_scale_set(compositor.output[0].scale);
        compositor_space_carry((const int (*)[4])was, (const char (*)[WINDOWS_OUTPUT_NAME])names, compositor.outputs);
        return 1;
}

// Probe connected outputs, lay out the desktop scanout, and commit each CRTC (dropping refused outputs).
static void compositor_cursor_lock_take(void);
static void compositor_cursor_lock_drop(void);
static void compositor_cursor_async_sync(int active, int hot_x, int hot_y, int inset, int left, int top);

static b32 compositor_space_take(int first)
{
        struct compositor_connected found;
        int rects[WINDOWS_OUTPUTS][4], was[WINDOWS_OUTPUTS][4];
        char was_names[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_NAME];
        unsigned int was_crtc[WINDOWS_OUTPUTS], was_count = 0, dropped = 0, extra[WINDOWS_OUTPUTS], extras;
        unsigned char order[WINDOWS_OUTPUTS];
        b32 status;
        int carried = 0, regained = 0, later = !first;

        if (!first && (compositor.flip_pending || compositor_card.on || rows_any(&compositor_card.list_rows)))
                return compositor.space_wanted = 1, 0;
        compositor_cursor_lock_take();
        compositor_cursor_async_sync(0, 0, 0, 0, 0, 0);
        compositor_cursor_lock_drop();
        compositor_displays_read(1);
        status = compositor_connected_read(&found);
        if (status < 0 || !found.count)
        {
                if (!first && compositor.outputs)
                        compositor.screens_gone = 1;
                return -COMPOSITOR_ENODEV;
        }
        compositor.space_wanted = 0;
        kms_snapshot_outputs(was, was_names, was_crtc, &was_count);

        for (unsigned int attempt = 0; attempt <= found.count; attempt++)
        {
                unsigned int lit = compositor_space_lay(&found, dropped, rects, order), refused = 0, same;
                struct drm_dumb retired[WINDOWS_OUTPUTS] = {{0}};
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

                // A space of another size is another scanout; the one before is given back once no crtc shows it. So is a set of screens of another count
                // (the spare's framebuffers of each screen are made for the count they had).
                if (first || box[2] != compositor.width || box[3] != compositor.height || lit != compositor.outputs)
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
                compositor_scaled_reset();
                if (compositor.crtc != compositor.output[0].crtc)
                {
                        compositor.overlay_state = 0;
                        compositor.primary = compositor.overlay = 0;
                        compositor.scale_plane_refused = 0;
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
                // The plate each screen shows is the one composed into (compositor_plate_out); the spare's are kept while the plates are.
                for (unsigned int at = 1; at < WINDOWS_OUTPUTS; at++)
                        compositor_plate_out[at][0] = at < lit ? compositor.output[at].fb : 0;
                compositor_out_planes_take(lit);
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
                // Paint directly into the new scanout FB before SETCRTC without swapping to spare or issuing a premature PAGE_FLIP.
                compositor_modesetting = 1;
                compositor_present();
                compositor_modesetting = 0;

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
                                if (compositor.output[at].fb && compositor.output[at].fb != compositor.fb)
                                        system_control(compositor.card, COMPOSITOR_RMFB, address_of compositor.output[at].fb);
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
                                kms_snapshot_outputs(was, was_names, was_crtc, &was_count);
                                first = 0;
                                if (!refused)
                                        continue;
                        }
                }
                for (unsigned int n = 0; n < found.count; n++)
                        if (refused >> n & 1)
                                compositor_kmsg((string_address) "compositor: the card refused a screen, which is left dark: ", 0,
                                                (string_address)found.name[n]);
                if (lit == 1)
                {
                        compositor.screens_gone = later;
                        return -COMPOSITOR_EINVAL;
                }
                dropped |= refused;
                kms_snapshot_outputs(was, was_names, was_crtc, &was_count);
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

// Probe every DRM card minor (real cards first, firmware cards second) until one has a connected screen.
#define COMPOSITOR_CARDS 64
static int compositor_uevents_open(void);
static int compositor_uevent_drm(const char *message, long length);

static long compositor_card_node(unsigned int minor)
{
        p8 path[24] = "/dev/dri/card";

        path[13 + positive_into_string(path + 13, minor)] = 0;
        return (long)system_open_at(AT_FDCWD, path, FILE_READ_WRITE | O_CLOEXEC | O_NOFOLLOW);
}

#ifndef COMPOSITOR_CARD_NODE
#define COMPOSITOR_CARD_NODE(minor) compositor_card_node(minor)
#endif

static int compositor_card_firmware(void) { return (compositor.driver_flags & DRV_FIRMWARE) != 0; }

static b32 compositor_card_try(unsigned int minor, int firmware)
{
        struct compositor_version version = {0};
        long card = COMPOSITOR_CARD_NODE(minor);
        b32 status;

        if (card < 0)
                return card == -COMPOSITOR_ENOENT ? -COMPOSITOR_ENOENT : -COMPOSITOR_EBUSY;
        compositor.card = (int)card;
        compositor_scaled_reset();
        version.name_length = sizeof(compositor.driver) - 1, version.name = compositor.driver;
        memory_zero(compositor.driver, sizeof(compositor.driver));
        if (system_control(compositor.card, COMPOSITOR_VERSION, address_of version) != 0)
                compositor.driver[0] = 0;
        compositor.driver_flags = drm_driver_flags(compositor.driver);
        if (!firmware && compositor_card_firmware())
                status = -COMPOSITOR_EAGAIN;
        else if (system_control(compositor.card, COMPOSITOR_SET_MASTER, 0) != 0)
                status = -COMPOSITOR_EBUSY;
        else
                status = compositor_space_first();
        if (status < 0)
                system_close(compositor.card), compositor.card = -1, compositor.driver[0] = 0, compositor.driver_flags = 0, compositor_scaled_reset();
        return status;
}

static b32 compositor_cards_walk(void)
{
        unsigned long firmware = 0;
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
                wait[0] = (long)((until - now) / 1000000000ull), wait[1] = (long)((until - now) % 1000000000ull);
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
                                        (string_address) ""), said = 1;
                compositor_screen_wait(COMPOSITOR_SCREEN_LOOK);
        }
}

// Variable refresh (VRR): enable on single-output flipping setups per policy (0=on, 1=games, 2=off).
#define COMPOSITOR_VRR_STEADY 16u
static struct
{
        unsigned int policy, crtc, connector, prop, steady;
        int capable, on, refused, said;
} compositor_vrr;

static int compositor_vrr_wants(unsigned int policy, int capable, unsigned int outputs, int flipping, int direct)
{
        return capable && outputs == 1 && flipping && policy < 2 && (policy == 0 || direct);
}

static int compositor_vrr_step(int want, int on, unsigned int *steady)
{
        *steady = want ? *steady + (*steady < COMPOSITOR_VRR_STEADY) : 0;
        return want && (on > 0 || *steady >= COMPOSITOR_VRR_STEADY);
}

static void compositor_vrr_publish(void)
{
        if (!compositor.table)
                return;
        compositor.table->vrr = (compositor_vrr.capable ? WINDOWS_VRR_CAPABLE : 0u) | (compositor_vrr.prop ? WINDOWS_VRR_PROPERTY : 0u) |
                                (compositor_vrr.on > 0 ? WINDOWS_VRR_ON : 0u) | (compositor_vrr.refused ? WINDOWS_VRR_REFUSED : 0u) |
                                compositor_vrr.policy << 8;
}

static int compositor_vrr_set(int on)
{
        positive began = clock_monotonic_nanoseconds();

        if (!drm_obj_set_prop(compositor_vrr.crtc, COMPOSITOR_OBJECT_CRTC, compositor_vrr.prop, (unsigned long)on))
                return 0;
        compositor_vrr.on = on;
        if (compositor.table)
                compositor.table->vrr_set_us = (unsigned int)((clock_monotonic_nanoseconds() - began) / 1000);
        return 1;
}

// Display DPMS off/on via /run/moonwater/display-off.
#define COMPOSITOR_DARK_FILE "/run/moonwater/display-off"
static void compositor_dark(int dark)
{
        int was = compositor.dark;

        if (was == dark && (!dark || compositor.dark_applied))
                return;
        compositor.dark = dark, compositor.dark_applied = dark;
        for (unsigned int at = 0; at < compositor.outputs && at < WINDOWS_OUTPUTS; at++)
        {
                unsigned int conn = compositor.output[at].connector;
                unsigned int prop = drm_obj_get_prop(conn, COMPOSITOR_OBJECT_CONNECTOR, "DPMS", 0);

                (void)drm_obj_set_prop(conn, COMPOSITOR_OBJECT_CONNECTOR, prop, dark ? 3ul : 0ul);
        }
        if (!dark)
        {
                if (compositor.band)
                {
                        (void)taskbar_tick();
                        compositor_tabs_changed();
                }
                compositor_damage_space();
        }
        if (was != dark)
                compositor_kmsg(dark ? (string_address) "compositor: display off" : (string_address) "compositor: display on", 0,
                                (string_address) "");
}

static void compositor_dark_set(void)
{
        compositor.dark_applied = 0;
        if (compositor.dark)
                compositor_dark(1);
}

static void compositor_dark_look(void) { compositor_dark((int)canvas_switch_exists(COMPOSITOR_DARK_FILE, O_NOFOLLOW)); }

static void compositor_dark_woken(void)
{
        if (!compositor.dark)
                return;
        (void)system_remove_at(AT_FDCWD, (string_address)COMPOSITOR_DARK_FILE, 0);
        compositor_dark(0);
}

/*
        Cadence (`canvas cadence off|40|48|50|60|72`, off by default). On a screen without variable refresh a game held at N frames a second
        is shown for whole vblanks: 40 on 144 Hz for three, then four (20.8 and 27.8 ms by turns). While a Steam game (SteamGameId in the
        environment of the window's process, or of the X client under Xwayland) is shown straight and fullscreen on the one screen, the
        screens are taken again at the highest rate of their size that is a multiple of N (compositor_cadence_rate), and the game has one
        frame every k = rate / N vblanks (compositor_present_at, compositor_wl_jit_owed): 40 on 120 Hz is every third, 25 ms each. It begins
        after the 16 steady presents variable refresh waits for and ends a second after the game stops being shown straight (a take of the
        screens drops it for a few frames), the take made at the loop's next turn (probe_owed). Each way costs a mode switch, a blank; a rate
        the screen has already (48 and 72 on 144) costs none. A screen whose variable refresh can be on needs none of it.
*/
#define COMPOSITOR_CADENCE_LEAVE 1000000000ull
static const unsigned char compositor_cadence_rates[6] = {0, 40, 48, 50, 60, 72}; // the setting's words, by index
static unsigned int compositor_row_pids(int row, unsigned int me, int server, unsigned int *out);                // input.c
static unsigned int compositor_environ_word(unsigned int pid, const char *key, char *value, unsigned int room); // input.c

// Whether the row is a Steam game's (its process carries SteamGameId): asked again when the row or the stack changes.
static int compositor_cadence_game(int row)
{
        static unsigned int gen;
        static int asked = -1, answer;
        unsigned int got[2], me;
        char game[24];

        if (row == asked && gen == compositor.stack_gen)
                return answer;
        asked = row, gen = compositor.stack_gen;
        me = (unsigned int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0);
        answer = compositor_row_pids(row, me, 0, got) && compositor_environ_word(got[0], "SteamGameId=", game, sizeof game);
        return answer;
}

// Vblanks a frame of a cadence of hz frames a second holds on a screen of this period: the rate over hz when it is within 1 Hz of a
// multiple of it, else 1 (no cadence: every path as it was).
static unsigned int compositor_cadence_k(int active, unsigned int hz, positive period)
{
        positive millihertz, k;

        if (!active || !hz || !period)
                return 1;
        millihertz = 1000000000000ull / period;
        k = (millihertz + hz * 500ull) / (hz * 1000ull);
        if (k < 2 || (millihertz > k * hz * 1000ull ? millihertz - k * hz * 1000ull : k * hz * 1000ull - millihertz) > 1000ull)
                return 1;
        return (unsigned int)k;
}

// One present's step of the cadence: want is a Steam game shown straight and fullscreen with a cadence of hz asked. It begins at the 16th
// steady present and ends a second after the last that wanted it, or when the cadence asked changes. 1 when the screens are to be taken again.
static int compositor_cadence_step(int want, unsigned int hz, positive now)
{
        if (want)
                compositor_cadence.lost_at = 0;
        else if (compositor_cadence.active && !compositor_cadence.lost_at)
                compositor_cadence.lost_at = now;
        if (!compositor_cadence.active)
        {
                compositor_cadence.steady = want ? compositor_cadence.steady + (compositor_cadence.steady < COMPOSITOR_VRR_STEADY) : 0;
                if (!want || compositor_cadence.steady < COMPOSITOR_VRR_STEADY)
                        return 0;
                compositor_cadence.active = 1, compositor_cadence.hz = hz;
                return 1;
        }
        if (hz == compositor_cadence.hz && (!compositor_cadence.lost_at || now - compositor_cadence.lost_at < COMPOSITOR_CADENCE_LEAVE))
                return 0;
        compositor_cadence.active = 0, compositor_cadence.steady = 0, compositor_cadence.lost_at = 0;
        return 1;
}

static void compositor_cadence_keep(void)
{
        unsigned int hz = compositor_cadence.choice < sizeof compositor_cadence_rates ? compositor_cadence_rates[compositor_cadence.choice] : 0;
        int row = compositor.direct_row;
        int want = hz && !(compositor_vrr.capable && compositor_vrr.policy < 2) && compositor_output_count() == 1 && row >= 0 &&
                   compositor_space_is_fs_row(row) && compositor_cadence_game(row);

        if (compositor_cadence_step(want, hz, clock_monotonic_nanoseconds()))
                compositor.probe_owed = 1;
        compositor_cadence.k = compositor_cadence_k(compositor_cadence.active, compositor_cadence.hz, compositor.period);
}

static void compositor_vrr_keep(void)
{
        unsigned long value;
        int want, flipping;

        if (compositor.card < 0 || !compositor.crtc)
                return;
        if (compositor_vrr.crtc != compositor.crtc || compositor_vrr.connector != compositor.connector)
        {
                compositor_vrr.crtc = compositor.crtc, compositor_vrr.connector = compositor.connector;
                compositor_vrr.capable = (drm_obj_get_prop(compositor.connector, COMPOSITOR_OBJECT_CONNECTOR, "vrr_capable", &value) && value) ||
                                         compositor_vrr.said;
                compositor_vrr.prop = drm_obj_get_prop(compositor.crtc, COMPOSITOR_OBJECT_CRTC, "VRR_ENABLED", &value);
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
        compositor_cadence_keep();
}

static void compositor_vrr_off(void)
{
        if (compositor.card >= 0 && compositor_vrr.prop && compositor_vrr.on > 0 && compositor_vrr.crtc == compositor.crtc)
                compositor_vrr_set(0);
}

static void compositor_vrr_forget(void) { compositor_vrr.crtc = compositor_vrr.connector = 0; }

static void compositor_vrr_moved(void)
{
        if (compositor_vrr.on > 0)
                compositor.vrr_kick = 1;
}

static void compositor_vrr_kick(void)
{
        int row = compositor.direct_row;

        if (compositor_vrr.on <= 0)
                compositor.vrr_kick = 0;
        if (!compositor.vrr_kick || compositor.flip_pending || !compositor.on_screen)
                return;
        compositor.vrr_kick = 0;
        if (compositor.scaled_direct && row >= 0)
        {
                int dx, dy, dw, dh;
                if (compositor_fullscreen_fit(row, &dx, &dy, &dw, &dh) &&
                    compositor_atomic_primary(compositor.on_screen, row, compositor.front[row],
                                              (int)compositor.picture_w[row], (int)compositor.picture_h[row], dx, dy, dw, dh) &&
                    compositor.table)
                        compositor.table->vrr_kicks++;
                return;
        }
        if ((row >= 0 || compositor.on_screen == compositor.fb) &&
            compositor_flip(compositor.on_screen, row, row >= 0 ? compositor.front[row] : 0) && compositor.table)
                compositor.table->vrr_kicks++;
}

// Hardware cursor plane & dynamic pointer state (software fallback when driver/setting/probe refuses plane).

// Where a value on its way is now, on the kit's ease: where it goes, and at rest there, once CP_SWAY_NS at the pace (ui_paced) have
// passed since it left (a time before it left is the time it left).
static int compositor_sway_at(struct compositor_sway *s, positive now)
{
        if (s->at && now > s->at && now - s->at >= (positive)ui_paced((b64)CP_SWAY_NS))
                s->at = 0;
        return s->at ? canvas_tween_lerp(s->from, s->to, canvas_tween_ease(s->at, now, (b64)CP_SWAY_NS)) : s->to;
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

static b32 compositor_access_drives(void);  // access.c
static b32 compositor_access_present(void); // access.c: a controller is read

// Whether the pointer is the dynamic one now: the style says so, or the last input was a touch (touch.hidden) or a controller's, which always is.
static int compositor_pointer_dynamic(void)
{
        return compositor.cursor.style || compositor.touch.hidden || compositor_access_drives();
}

/*
        The pointer as it is to be drawn now: its bitmap or the dynamic shape, at its size (compositor_cursor_size), smaller or larger
        by what a held button has it at, and no larger than the largest picture (CP_SPAN_MOST) holds it. The dynamic shape is on its way
        to the rectangle while the pointer is over text.
*/
static void compositor_pointer_now(struct compositor_pointer *p)
{
        positive period, now;
        int morph, lens, grow, scale, most, resize;

        // A window's client's own picture is worn as it was copied: no size of the settings', and no squeeze of a held button.
        if (compositor_picture.worn)
        {
                *p = (struct compositor_pointer){.shape = CP_ARROW, .scale = 256, .span = compositor_picture.span, .hot_x = compositor_picture.hot_x,
                                                 .hot_y = compositor_picture.hot_y, .picture = compositor_picture.worn};
                return;
        }
        // The values are read at the frame the clock is in, so the picture changes once a frame however often a moving hand asks for it.
        period = compositor.period ? compositor.period : 16000000ull, now = clock_monotonic_nanoseconds() / period * period;
        resize = compositor.cursor.shape >= CP_RESIZE_H && compositor.cursor.shape <= CP_RESIZE_NESW;
        compositor_sway_to(&compositor.cursor.morph, compositor.cursor.dyn && compositor.cursor.shape == CP_TEXT ? 256 : 0, now);
        compositor_sway_to(&compositor.cursor.lens, compositor.cursor.dyn && resize ? 256 : 0, now);
        morph = compositor_sway_at(&compositor.cursor.morph, now);
        lens = compositor_sway_at(&compositor.cursor.lens, now);
        grow = compositor_sway_at(&compositor.cursor.stretch, now) * CP_DYN_STRETCH >> 8;
        // Over a window's edge (a resize) the pointer keeps its size whatever is held: a drag of the edge neither grows it nor eases it.
        scale = resize ? compositor_cursor_size() : (compositor_cursor_size() * (256 + compositor_sway_at(&compositor.cursor.press, now)) + 128) >> 8;
        most = (CP_SPAN_MOST << 8) / (compositor.cursor.dyn ? CP_DYN_CELL : CP_CELL);
        *p = (struct compositor_pointer){.dyn = compositor.cursor.dyn, .scale = scale < most ? scale : most};
        if (!p->dyn)
        {
                p->shape = compositor.cursor.shape < CP_SHAPES ? compositor.cursor.shape : CP_ARROW;
                return;
        }
        // Over a window's edge the shape eases into the pointed circle along the way the edge moves (45 is 64 over the square root of two), never stretched.
        if (resize)
        {
                static const signed char way[4][2] = {{64, 0}, {0, 64}, {45, 45}, {45, -45}};

                compositor.cursor.lens_ux = way[compositor.cursor.shape - CP_RESIZE_H][0];
                compositor.cursor.lens_uy = way[compositor.cursor.shape - CP_RESIZE_H][1];
        }
        if (lens)
        {
                p->lens = 1;
                p->hw = 256 * CP_DYN_RADIUS + (CP_DYN_LENS_W - CP_DYN_RADIUS) * lens;
                p->hh = 256 * CP_DYN_RADIUS;
                p->c = 0;
                p->ux = compositor.cursor.lens_ux, p->uy = compositor.cursor.lens_uy;
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
        positive glass = compositor_glass_bg_due(), due = 0;

        if (compositor.cursor.press.at || compositor.cursor.morph.at || compositor.cursor.lens.at || compositor.cursor.stretch.at)
                due = clock_monotonic_nanoseconds() + (compositor.period ? compositor.period : 16000000ull);
        else if (compositor.cursor.stretch.to)
                due = compositor.cursor.idle_until;
        return glass && (!due || glass < due) ? glass : due;
}

// The pointer's own times that are up: a stretch nothing has moved under lets go, and a value on its way is drawn where it is now.
static void compositor_cursor_tick(positive now)
{
        if (compositor.cursor.stretch.to && now >= compositor.cursor.idle_until)
                compositor_sway_to(&compositor.cursor.stretch, 0, now);
        if (compositor.cursor.press.at || compositor.cursor.morph.at || compositor.cursor.lens.at || compositor.cursor.stretch.at)
                compositor_cursor_move();
}

// The square of the scene the pointer drawn into it covers is damaged.
static void compositor_cursor_damage(void)
{
        int hot_x, hot_y, span = compositor_pointer_span(&compositor.cursor.drawn_as, &hot_x, &hot_y);

        compositor_damage_over(compositor.cursor.x - hot_x, compositor.cursor.y - hot_y, span, span);
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
        return (int)canvas_switch_exists(COMPOSITOR_CURSOR_PROBE_FILE, O_NOFOLLOW);
}

// Whether the card's driver is one refused the plane by name.
static int compositor_cursor_named(void)
{
        return (drm_driver_flags(compositor.driver) & DRV_NO_CURSOR_PLANE) != 0;
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
                        if (length >= 16)
                        {
                                memory_copy(address_of data, events + at + 8, 8);
                                if (type == COMPOSITOR_EVENT_VBLANK && data == COMPOSITOR_CURSOR_PROBE_TAG)
                                        return 1;
                        }
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
                                FILE_READ | O_CLOEXEC | O_NOFOLLOW);
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

// Cached scratch keeps cursor rendering off write-combined dumb memory.
static unsigned int compositor_cursor_scratch[CP_PLANE * CP_PLANE];

static unsigned int *compositor_cursor_mapped(int which)
{
        if (!compositor.cursor.mapped[which])
                compositor.cursor.mapped[which] = drm_dumb_map(compositor.cursor.handle[which], CP_PLANE * CP_PLANE * 4);
        return compositor.cursor.mapped[which];
}

HOT static int compositor_cursor_paint(const struct compositor_pointer *p)
{
        unsigned int *mapped = compositor_cursor_mapped(compositor.cursor.back);
        struct compositor_target t = {compositor_cursor_scratch, CP_PLANE, CP_PLANE, CP_PLANE, 0, 0, CP_PLANE, CP_PLANE};
        int hot_x, hot_y, inset;

        if (!mapped)
                return -1;
        memory_zero(compositor_cursor_scratch, sizeof(compositor_cursor_scratch));
        inset = compositor_cursor_inset(compositor_pointer_span(p, &hot_x, &hot_y));
        compositor_cursor_draw(&t, hot_x - inset, hot_y - inset, p, compositor_cursor_ink(0, 0xff000000u), compositor_cursor_ink(1, 0xff000000u));
        memory_copy_rect_out_u32(mapped, CP_PLANE, compositor_cursor_scratch, CP_PLANE, CP_PLANE, CP_PLANE);
        compositor.cursor.back ^= 1;
        compositor.cursor.paints++;
        compositor.cursor.plane = *p;
        compositor.cursor.plane_on = 1;
        return 0;
}

static int compositor_wl_locked(void);
static int compositor_wl_held(void);

// Async cursor-plane fast-path: lets the SCHED_FIFO input reader thread issue DRM_MODE_CURSOR_MOVE directly
// even while the main compositor loop is busy with presents/Wayland/KMS, so cursor motion never drops a frame.
static struct
{
        unsigned int lock;
        int active, card;
        unsigned int count;
        unsigned int crtc[WINDOWS_OUTPUTS];
        int screens[WINDOWS_OUTPUTS][4];
        unsigned char shows[WINDOWS_OUTPUTS];
        int hot_x, hot_y, inset;
        int flat, sensitivity;
        int x, y, accel_x, accel_y;
        positive stamp, moved_ns;
        int last_left, last_top;
} compositor_cursor_async;

static void compositor_cursor_lock_take(void)
{
        while (__atomic_exchange_n(&compositor_cursor_async.lock, 1u, __ATOMIC_ACQUIRE))
                while (__atomic_load_n(&compositor_cursor_async.lock, __ATOMIC_RELAXED))
#if defined(__x86_64__)
                        __builtin_ia32_pause();
#elif defined(__aarch64__)
                        __asm__ __volatile__("yield");
#else
                        ;
#endif
}

static void compositor_cursor_lock_drop(void)
{
        __atomic_store_n(&compositor_cursor_async.lock, 0u, __ATOMIC_RELEASE);
}

static void compositor_cursor_async_sync(int active, int hot_x, int hot_y, int inset, int left, int top)
{
        unsigned int count = 0;
        const int (*screens)[4] = compositor_screens(&count);

        if (active && compositor.card >= 0 && compositor.cursor.plane_on && !compositor.dark && !compositor_wl_held() &&
            count > 0 && count <= WINDOWS_OUTPUTS)
        {
                compositor_cursor_async.card = compositor.card;
                compositor_cursor_async.count = count;
                for (unsigned int at = 0; at < count; at++)
                {
                        compositor_cursor_async.crtc[at] = compositor.outputs ? compositor.output[at].crtc : compositor.crtc;
                        memory_copy(compositor_cursor_async.screens[at], screens[at], sizeof(screens[at]));
                        compositor_cursor_async.shows[at] = compositor.output[at].shows != 0;
                }
                compositor_cursor_async.hot_x = hot_x, compositor_cursor_async.hot_y = hot_y, compositor_cursor_async.inset = inset;
                compositor_cursor_async.flat = compositor.flat, compositor_cursor_async.sensitivity = compositor.sensitivity;
                compositor_cursor_async.x = compositor.pointer_x, compositor_cursor_async.y = compositor.pointer_y;
                compositor_cursor_async.accel_x = compositor.accel_x, compositor_cursor_async.accel_y = compositor.accel_y;
                compositor_cursor_async.stamp = compositor.stamp;
                compositor_cursor_async.last_left = left, compositor_cursor_async.last_top = top;
                compositor_cursor_async.active = 1;
        }
        else
                compositor_cursor_async.active = 0;
}

#ifdef CONFIG_MOONWATER_WAYLAND
static void compositor_wl_direct_abs(int x, int y, positive now);
#endif
HOT static void compositor_cursor_async_put(int nx, int ny, positive now);
HOT static void compositor_cursor_async_step(long dx, long dy, positive now)
{
        int gain, ax, ay;

        if ((!dx && !dy) || !__atomic_load_n(&compositor_cursor_async.active, __ATOMIC_ACQUIRE) ||
            __atomic_load_n(&compositor.dark, __ATOMIC_RELAXED))
                return;
        // Trylock: prio-50 reader must never spin behind prio-40 main loop across a DRM ioctl.
        if (__atomic_exchange_n(&compositor_cursor_async.lock, 1u, __ATOMIC_ACQUIRE))
                return;
        if (!compositor_cursor_async.active)
        {
                compositor_cursor_lock_drop();
                return;
        }
        gain = compositor_gain_wanted(compositor_gain_of((int)dx, (int)dy, now > compositor_cursor_async.stamp ? now - compositor_cursor_async.stamp : 0),
                                      compositor_cursor_async.flat, compositor_cursor_async.sensitivity);
        compositor_cursor_async.stamp = now;
        ax = compositor_accel((int)dx, &compositor_cursor_async.accel_x, gain);
        ay = compositor_accel((int)dy, &compositor_cursor_async.accel_y, gain);
        compositor_cursor_async_put(compositor_counts(compositor_cursor_async.x, ax), compositor_counts(compositor_cursor_async.y, ay), now);
        compositor_cursor_lock_drop();
}

/*
        The cursor plane put at a point, with the lock held: the point kept on the screens, and the plane moved where its corner
        changed (no more than once a millisecond, as a mouse's reports come at most). The mouse's step comes here after its gain,
        and a stick's shift (access.c, on the reader's thread) with the pixels its time and its push made.
*/
HOT static void compositor_cursor_async_put(int nx, int ny, positive now)
{
        int left, top;

        compositor_screen_keep_point((const int (*)[4])compositor_cursor_async.screens, compositor_cursor_async.count,
                                     compositor_cursor_async.x, compositor_cursor_async.y, &nx, &ny);
        compositor_cursor_async.x = nx, compositor_cursor_async.y = ny;
        left = nx - compositor_cursor_async.hot_x + compositor_cursor_async.inset;
        top = ny - compositor_cursor_async.hot_y + compositor_cursor_async.inset;
        if ((left != compositor_cursor_async.last_left || top != compositor_cursor_async.last_top) &&
            now - compositor_cursor_async.moved_ns >= 1000000ull)
        {
                int moved = 0;

                for (unsigned int at = 0; at < compositor_cursor_async.count; at++)
                {
                        int x, y;

                        if (compositor_cursor_async.shows[at] &&
                            compositor_screen_cell(compositor_cursor_async.screens[at], left, top, CP_PLANE, &x, &y))
                        {
                                struct compositor_cursor move = {2, compositor_cursor_async.crtc[at], x, y, 0, 0, 0};

                                if (system_control(compositor_cursor_async.card, COMPOSITOR_CURSOR, address_of move) == 0)
                                        compositor.output[at].cursor_x = x, compositor.output[at].cursor_y = y, moved = 1;
                        }
                }
                if (moved)
                        compositor_cursor_async.last_left = left, compositor_cursor_async.last_top = top, compositor_cursor_async.moved_ns = now;
        }
}

// A stick's move on the reader's thread: the pixels as they are (no gain: the stick's speed is its own curve), the plane at once.
HOT static void compositor_cursor_async_shift(int dx, int dy, positive now)
{
        if ((!dx && !dy) || !__atomic_load_n(&compositor_cursor_async.active, __ATOMIC_ACQUIRE) || __atomic_load_n(&compositor.dark, __ATOMIC_RELAXED))
                return;
        if (__atomic_exchange_n(&compositor_cursor_async.lock, 1u, __ATOMIC_ACQUIRE))
                return;
        if (compositor_cursor_async.active)
                compositor_cursor_async_put(compositor_counts(compositor_cursor_async.x, dx), compositor_counts(compositor_cursor_async.y, dy), now);
        compositor_cursor_lock_drop();
}

// The height of the screen the plane's point is on, as the reader last had the screens (1080 where it has none).
static int compositor_cursor_async_height(void)
{
        unsigned int count = __atomic_load_n(&compositor_cursor_async.count, __ATOMIC_ACQUIRE);
        int x = compositor_cursor_async.x, y = compositor_cursor_async.y;

        for (unsigned int at = 0; at < count && at < WINDOWS_OUTPUTS; at++)
        {
                const int *screen = compositor_cursor_async.screens[at];
                if (x >= screen[0] && x < screen[0] + screen[2] && y >= screen[1] && y < screen[1] + screen[3] && screen[3] > 0)
                        return screen[3];
        }
        return count && compositor_cursor_async.screens[0][3] > 0 ? compositor_cursor_async.screens[0][3] : 1080;
}

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
                                output->cursor_x = x, output->cursor_y = y;
                                if (compositor_cursor_probe.asked && !compositor_cursor_probe.done &&
                                    compositor_cursor_probed(compositor_cursor_vblank(compositor.outputs ? output->crtc_index : compositor.crtc_index)) < 0)
                                        return -1;
                        }
                        else if (output->cursor_x != x || output->cursor_y != y)
                        {
                                struct compositor_cursor move = {2, crtc, x, y, 0, 0, 0};

                                if (system_control(compositor.card, COMPOSITOR_CURSOR, address_of move) == 0)
                                        output->cursor_x = x, output->cursor_y = y, compositor.cursor.moves++;
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

static void compositor_cursor_planes_off(void)
{
        unsigned int count;

        compositor_cursor_lock_take();
        compositor_cursor_async.active = 0;
        compositor_screens(&count);
        for (unsigned int at = 0; at < count; at++)
        {
                struct compositor_cursor gone = {1, compositor.outputs ? compositor.output[at].crtc : compositor.crtc, 0, 0, CP_PLANE, CP_PLANE, 0};

                system_control(compositor.card, COMPOSITOR_CURSOR, address_of gone);
                compositor.output[at].shows = 0;
        }
        compositor.cursor.plane_on = 0;
        compositor_cursor_lock_drop();
}

static void compositor_cursor_start(void)
{
        struct drm_dumb d[2];

        compositor.cursor.plane_on = 0;
        compositor_cursor_probe.asked = compositor_cursor_probe_asked();
        compositor_cursor_probe.done = compositor_cursor_probe.stopped = 0;
        if (compositor.card < 0 || !compositor_cursor_plane_wanted())
        {
                compositor.cursor.soft = compositor.card >= 0;
                return;
        }
        for (int i = 0; i < 2; i++)
                if (drm_dumb_create(&d[i], CP_PLANE, CP_PLANE, 0) != 0)
                {
                        if (i)
                                drm_dumb_destroy(&d[0]);
                        compositor.cursor.soft = 1;
                        return;
                }
        compositor.cursor.handle[0] = d[0].handle, compositor.cursor.handle[1] = d[1].handle;
}

static int compositor_drawn_x(void) { return compositor.pointer_x; }
static int compositor_drawn_y(void) { return compositor.pointer_y; }

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
        {
                int same = compositor.cursor.plane_on && !memory_compare(&now, &compositor.cursor.plane, sizeof(now));
                // While a primary page flip is pending, skip dynamic speed-stretch BO swaps (DRM_MODE_CURSOR_BO can
                // serialize behind the pending flip on amdgpu) and move the current cursor plane immediately (flags = 2).
                int defer_bo = !same && compositor.cursor.plane_on && compositor.flip_pending &&
                               now.dyn == compositor.cursor.plane.dyn && now.shape == compositor.cursor.plane.shape &&
                               now.picture == compositor.cursor.plane.picture && now.lens == compositor.cursor.plane.lens &&
                               now.scale == compositor.cursor.plane.scale;
                const struct compositor_pointer *shown = defer_bo ? &compositor.cursor.plane : &now;
                int left, top, ok;

                inset = compositor_cursor_inset(compositor_pointer_span(shown, &hot_x, &hot_y));
                left = compositor_drawn_x() - hot_x + inset, top = compositor_drawn_y() - hot_y + inset;
                ok = same || defer_bo || compositor_cursor_paint(&now) == 0;
                compositor_cursor_lock_take();
                ok = ok && compositor_cursor_planes(left, top) == 0;
                if (ok)
                        compositor_cursor_async_sync(1, hot_x, hot_y, inset, left, top);
                else
                        compositor_cursor_async.active = 0;
                compositor_cursor_lock_drop();
                if (ok)
                {
                        compositor_vrr_moved();
                        return;
                }
        }

        // During wake retrain, a transient cursor refusal must not permanently disable the hardware cursor plane.
        if (compositor.wake_stage)
                return;

        // The card will not take it: the pointer is drawn from here on, and the dumb buffers are released.
        compositor_cursor_planes_off();
        for (int i = 0; i < 2; i++)
                if (compositor.cursor.handle[i])
                {
                        struct drm_dumb b = {0, compositor.cursor.handle[i], compositor.cursor.mapped[i],
                                             compositor.cursor.mapped[i] ? CP_PLANE * CP_PLANE * 4 : 0, 0, 0, 0};

                        drm_dumb_destroy(&b);
                        compositor.cursor.mapped[i] = null;
                        compositor.cursor.handle[i] = 0;
                }
        compositor.cursor.refused++;
        compositor.cursor.soft = 1;
        compositor_cursor_move();
}

/*
        Native Canvas DRM / DMA-BUF / Timeline Syncobj primitives:
        protocol-neutral render-node classification, plane IN_FORMATS parsing,
        DMA_BUF_IOCTL_SYNC wrapper, and dynamic DRM timeline syncobj pool.
*/
#define CANVAS_DMABUF_SYNC_IOCTL 0x40086200u // DMA_BUF_IOCTL_SYNC
#define CANVAS_DRM_RENDER_GID 105u
#define CANVAS_DRM_IOCTL_VERSION 0xc0406400UL
#define CANVAS_DRM_IOCTL_GET_CAP 0xc010640cUL
#define CANVAS_DRM_IOCTL_VIRTGPU_GETPARAM 0xc0106443UL
#define CANVAS_DRM_IOCTL_SYNCOBJ_DESTROY 0xc00864c0UL // DRM_IOWR(0xC0); 0xbf is SYNCOBJ_CREATE, which the old number asked for (a leak)
#define CANVAS_DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE 0xc01064c2UL
#define CANVAS_DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT 0xc03064caUL
#define CANVAS_DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL 0xc01864cdUL
#define CANVAS_DRM_IOCTL_SYNCOBJ_EVENTFD 0xc01864cfUL
#define CANVAS_SYNCOBJ_MAX (1l << 20)

static long canvas_dmabuf_sync_fd(long fd, unsigned long flags)
{
        unsigned long arg = flags;

        return fd >= 0 ? compositor_call(syscall(ioctl), (positive)fd, CANVAS_DMABUF_SYNC_IOCTL, (positive)&arg, 0, 0, 0) : -9;
}

static void canvas_dmabuf_sync_read(long fd, int finish)
{
        if (fd >= 0)
                canvas_dmabuf_sync_fd(fd, finish ? 5ul : 1ul); // DMA_BUF_SYNC_READ | (END : START)
}

static unsigned long canvas_drm_makedev(unsigned long major, unsigned long minor)
{
        return ((major & 0xfffUL) << 8) | (minor & 0xffUL) | ((minor & ~0xffUL) << 12) | ((major & ~0xfffUL) << 32);
}

static unsigned long canvas_drm_node_dev(const char *path)
{
        unsigned char st[256];

        memory_zero(st, sizeof(st));
        if (compositor_call(syscall(statx), (positive)AT_FDCWD, (positive)path, 0x100, 0x7ff, (positive)st, 0) < 0) // AT_SYMLINK_NOFOLLOW
                return 0;
        return canvas_drm_makedev(*(unsigned int *)(st + 128), *(unsigned int *)(st + 132));
}

struct canvas_drm_render
{
        unsigned long dev;
        int hw, syncobj, fd;
        char path[96];
};

static int canvas_drm_render_classify(const char *path, unsigned long prev_dev, struct canvas_drm_render *out)
{
        unsigned char st[256];
        unsigned long dev;
        long fd;
        char name[32];
        struct
        {
                int major, minor, patch;
                unsigned long name_len;
                char *name;
                unsigned long date_len;
                char *date;
                unsigned long desc_len;
                char *desc;
        } ver;
        unsigned long cap[2] = {0x14, 0}; // DRM_CAP_SYNCOBJ_TIMELINE
        int rank = 1;

        out->dev = 0, out->hw = 0, out->syncobj = 0, out->fd = -1;
        out->path[0] = 0;
        fd = compositor_call(syscall(openat), (positive)AT_FDCWD, (positive)path, FILE_READ_WRITE | O_CLOEXEC | O_NOFOLLOW, 0, 0, 0);
        if (fd < 0)
                return 0;
        memory_zero(st, sizeof(st));
        if (compositor_call(syscall(statx), (positive)fd, (positive)"", 0x1000, 0x7ff, (positive)st, 0) < 0 ||
            (*(unsigned short *)(st + 28) & 0170000u) != 0020000u)
                return system_close((int)fd), 0;
        dev = canvas_drm_makedev(*(unsigned int *)(st + 128), *(unsigned int *)(st + 132));
        if (!dev)
                return system_close((int)fd), 0;
        out->dev = dev;
        if (dev != prev_dev)
        {
                compositor_call(syscall(fchown), (positive)fd, 0, CANVAS_DRM_RENDER_GID, 0, 0, 0);
                compositor_call(syscall(fchmod), (positive)fd, 0660, 0, 0, 0, 0);
        }
        memory_zero(name, sizeof(name));
        memory_zero(&ver, sizeof(ver));
        ver.name_len = sizeof(name) - 1, ver.name = name;
        if (compositor_call(syscall(ioctl), (positive)fd, CANVAS_DRM_IOCTL_VERSION, (positive)&ver, 0, 0, 0) == 0)
        {
                name[sizeof(name) - 1] = 0;
                if (string_equals((string_address)name, (string_address)"vgem") ||
                    string_equals((string_address)name, (string_address)"vkms"))
                        rank = 1;
                else if (string_equals((string_address)name, (string_address)"virtio_gpu"))
                {
                        unsigned long val = 0, param[2] = {1, (unsigned long)&val}; // VIRTGPU_PARAM_3D_FEATURES

                        rank = compositor_call(syscall(ioctl), (positive)fd, CANVAS_DRM_IOCTL_VIRTGPU_GETPARAM, (positive)param, 0, 0, 0) == 0 && val ? 2 : 1;
                }
                else
                        rank = 2;
        }
        out->hw = rank >= 2;
        if (compositor_call(syscall(ioctl), (positive)fd, CANVAS_DRM_IOCTL_GET_CAP, (positive)cap, 0, 0, 0) == 0 && cap[1])
                out->syncobj = 1, out->fd = (int)fd;
        else
                system_close((int)fd);
        string_copy_bounded((string_address)out->path, (string_address)path, sizeof(out->path));
        return rank;
}

static void kms_render_path(char *path, unsigned int num)
{
        unsigned int at = compositor_said(path, 0, "/dev/dri/renderD");

        at += positive_into_string((p8 *)path + at, num);
        path[at] = 0;
}

static unsigned long canvas_drm_render_probe(int card_fd, unsigned long prev_dev, struct canvas_drm_render *out)
{
        struct canvas_drm_render best = {0, 0, 0, -1, {0}}, cand;
        int best_rank = 0;
        char path[96];

        if (card_fd >= 0)
        {
                unsigned char st[256] = {0};

                if (compositor_call(syscall(statx), (positive)card_fd, (positive)"", 0x1000, 0x7ff, (positive)st, 0) >= 0)
                {
                        unsigned int maj = *(unsigned int *)(st + 128), min = *(unsigned int *)(st + 132);

                        for (unsigned int num = 128; num < 192; num++)
                        {
                                unsigned int at = compositor_said(path, 0, "/sys/dev/char/");

                                at += positive_into_string((p8 *)path + at, maj);
                                path[at++] = ':';
                                at += positive_into_string((p8 *)path + at, min);
                                at = compositor_said(path, at, "/../renderD");
                                at += positive_into_string((p8 *)path + at, num);
                                path[at] = 0;
                                if (compositor_call(syscall(faccessat), (positive)AT_FDCWD, (positive)path, 0, 0, 0, 0) < 0)
                                        continue;
                                kms_render_path(path, num);
                                if (canvas_drm_render_classify(path, prev_dev, &cand) > 0 && cand.dev)
                                        return *out = cand, cand.dev;
                                if (cand.fd >= 0)
                                        system_close(cand.fd);
                        }
                }
        }
        for (unsigned int num = 128; num < 192; num++)
        {
                int rank;

                kms_render_path(path, num);
                rank = canvas_drm_render_classify(path, prev_dev, &cand);
                if (rank > best_rank && cand.dev)
                {
                        if (best.fd >= 0)
                                system_close(best.fd);
                        best = cand, best_rank = rank;
                        if (rank >= 2)
                                break;
                }
                else if (cand.fd >= 0)
                        system_close(cand.fd);
        }
        *out = best;
        return best.dev;
}

struct canvas_drm_format_pair
{
        unsigned int format;
        unsigned long modifier;
};

static unsigned int canvas_drm_plane_formats(int card_fd, unsigned int plane_id,
                                             struct canvas_drm_format_pair *out, unsigned int cap)
{
        unsigned long blob_id = 0;
        unsigned char buf[4096];
        struct
        {
                unsigned int id, length;
                p64 data;
        } blob;
        unsigned int count = 0;

        if (card_fd < 0 || !plane_id || !out || !cap)
                return 0;
        if (!drm_obj_get_prop(plane_id, COMPOSITOR_OBJECT_PLANE, "IN_FORMATS", &blob_id) || !blob_id)
                return 0;
        blob.id = (unsigned int)blob_id, blob.length = 0, blob.data = (p64)buf;
        if (system_control(card_fd, COMPOSITOR_GETPROPBLOB, address_of blob) != 0 ||
            blob.length < 24 || blob.length > sizeof(buf))
                return 0;
        {
                unsigned int len = blob.length;

                memory_zero(buf, len);
                if (system_control(card_fd, COMPOSITOR_GETPROPBLOB, address_of blob) != 0 || blob.length != len)
                        return 0;
        }
        {
                unsigned int nf = *(unsigned int *)(buf + 8), foff = *(unsigned int *)(buf + 12);
                unsigned int nm = *(unsigned int *)(buf + 16), moff = *(unsigned int *)(buf + 20);
                const unsigned int *fmts = (const unsigned int *)(buf + foff);
                const unsigned char *mods = buf + moff;

                // 64-bit sums: the offsets and counts are the driver's, and a 32-bit sum of a large count wraps past the blob.
                if ((unsigned long)foff + (unsigned long)nf * 4ul > blob.length || (unsigned long)moff + (unsigned long)nm * 24ul > blob.length)
                        return 0;
                for (unsigned int m = 0; m < nm; m++)
                {
                        unsigned long mask = *(const unsigned long *)(mods + m * 24u);
                        unsigned int off = *(const unsigned int *)(mods + m * 24u + 8u);
                        unsigned long mod = *(const unsigned long *)(mods + m * 24u + 16u);

                        for (unsigned int bit = 0; bit < 64 && off + bit < nf; bit++)
                        {
                                unsigned int fmt, dup = 0;

                                if (!(mask & (1UL << bit)) || count >= cap)
                                        continue;
                                fmt = fmts[off + bit];
                                for (unsigned int k = 0; k < count && !dup; k++)
                                        dup = out[k].format == fmt && out[k].modifier == mod;
                                if (!dup)
                                        out[count++] = (struct canvas_drm_format_pair){fmt, mod};
                        }
                }
        }
        return count;
}

struct canvas_syncobj_slot
{
        int used;
        unsigned int handle;
        int eventfd, armed;
        unsigned long long armed_point;
};

struct canvas_syncobj_pool
{
        int drm_fd;
        struct canvas_syncobj_slot *slots;
        long count, cap;
};

static void canvas_syncobj_init(struct canvas_syncobj_pool *p, int drm_fd)
{
        p->drm_fd = drm_fd;
}

static void canvas_syncobj_drop(struct canvas_syncobj_pool *p, long slot)
{
        if (!p || slot < 0 || slot >= p->count || !p->slots[slot].used)
                return;
        if (p->drm_fd >= 0 && p->slots[slot].handle)
        {
                unsigned int arg[2] = {p->slots[slot].handle, 0};

                compositor_call(syscall(ioctl), (positive)p->drm_fd, CANVAS_DRM_IOCTL_SYNCOBJ_DESTROY, (positive)arg, 0, 0, 0);
        }
        if (p->slots[slot].eventfd >= 0)
                system_close(p->slots[slot].eventfd);
        p->slots[slot] = (struct canvas_syncobj_slot){0, 0, -1, 0, 0};
}

static long canvas_syncobj_import(struct canvas_syncobj_pool *p, long fd)
{
        long slot = -1;
        struct
        {
                unsigned int handle, flags;
                int fd;
                unsigned int pad;
        } arg = {0, 0, (int)fd, 0};

        if (!p || fd < 0 || p->drm_fd < 0)
                return -1;
        for (long i = 0; i < p->count && slot < 0; i++)
                if (!p->slots[i].used)
                        slot = i;
        if (slot < 0)
        {
                if (p->count >= CANVAS_SYNCOBJ_MAX)
                        return -1;
                if (p->count >= p->cap)
                {
                        long next = p->cap ? p->cap * 2 : 64;
                        unsigned long bytes = (unsigned long)next * sizeof(*p->slots);
                        struct canvas_syncobj_slot *grown = (struct canvas_syncobj_slot *)compositor_map_zeroed(bytes);

                        if (!grown)
                                return -1;
                        if (p->slots)
                        {
                                memory_copy(grown, p->slots, (unsigned long)p->count * sizeof(*p->slots));
                                compositor_call(syscall(munmap), (positive)p->slots, (unsigned long)p->cap * sizeof(*p->slots), 0, 0, 0, 0);
                        }
                        p->slots = grown, p->cap = next;
                }
                slot = p->count++;
                p->slots[slot] = (struct canvas_syncobj_slot){0, 0, -1, 0, 0};
        }
        if (compositor_call(syscall(ioctl), (positive)p->drm_fd, CANVAS_DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, (positive)&arg, 0, 0, 0) < 0 || !arg.handle)
                return -1;
        p->slots[slot] = (struct canvas_syncobj_slot){1, arg.handle, -1, 0, 0};
        return slot;
}

static int canvas_syncobj_ready(const struct canvas_syncobj_pool *p, long slot, unsigned long long point)
{
        unsigned int handle;
        unsigned long long pt = point;
        // struct drm_syncobj_timeline_wait, 48 bytes (the uapi's; DRM_IOWR copies all 48 back out, so a shorter struct is overwritten past its end).
        struct
        {
                unsigned long long handles, points;
                long long timeout_nsec;
                unsigned int count_handles, flags, first_signaled, pad;
                unsigned long long deadline_nsec;
        } arg;

        _Static_assert(sizeof(arg) == 48, "drm_syncobj_timeline_wait is 48 bytes (uapi drm.h)");
        if (!p || slot < 0 || slot >= p->count || !p->slots[slot].used || p->drm_fd < 0)
                return 1;
        handle = p->slots[slot].handle;
        memory_zero(&arg, sizeof(arg));
        arg.handles = (unsigned long long)(unsigned long)&handle;
        arg.points = (unsigned long long)(unsigned long)&pt;
        arg.count_handles = 1, arg.flags = 1; // DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL; timeout 0 is a poll, deadline 0 none
        return compositor_call(syscall(ioctl), (positive)p->drm_fd, CANVAS_DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT, (positive)&arg, 0, 0, 0) == 0;
}

static void canvas_syncobj_signal(const struct canvas_syncobj_pool *p, long slot, unsigned long long point)
{
        unsigned int handle;
        unsigned long long pt = point;
        struct
        {
                unsigned long long handles, points;
                unsigned int count_handles, pad;
        } arg;

        if (!p || slot < 0 || slot >= p->count || !p->slots[slot].used || p->drm_fd < 0)
                return;
        handle = p->slots[slot].handle;
        memory_zero(&arg, sizeof(arg));
        arg.handles = (unsigned long long)(unsigned long)&handle;
        arg.points = (unsigned long long)(unsigned long)&pt;
        arg.count_handles = 1;
        compositor_call(syscall(ioctl), (positive)p->drm_fd, CANVAS_DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL, (positive)&arg, 0, 0, 0);
}

static long canvas_syncobj_eventfd(struct canvas_syncobj_pool *p, long slot, unsigned long long point)
{
        unsigned long long drain;
        struct
        {
                unsigned int handle, flags;
                unsigned long long point;
                int fd;
                unsigned int pad;
        } arg;

        if (!p || slot < 0 || slot >= p->count || !p->slots[slot].used || p->drm_fd < 0)
                return -1;
        if (p->slots[slot].eventfd < 0)
        {
                long efd = compositor_call(syscall(eventfd2), 0, 0x80000 | 0x800, 0, 0, 0, 0);

                if (efd < 0)
                        return -1;
                p->slots[slot].eventfd = (int)efd;
        }
        if (p->slots[slot].armed && p->slots[slot].armed_point == point)
                return p->slots[slot].eventfd;
        compositor_call(syscall(read), (positive)p->slots[slot].eventfd, (positive)&drain, sizeof(drain), 0, 0, 0);
        arg.handle = p->slots[slot].handle, arg.flags = 0, arg.point = point, arg.fd = p->slots[slot].eventfd, arg.pad = 0;
        if (compositor_call(syscall(ioctl), (positive)p->drm_fd, CANVAS_DRM_IOCTL_SYNCOBJ_EVENTFD, (positive)&arg, 0, 0, 0) < 0)
        {
                p->slots[slot].armed = 0;
                return -1;
        }
        p->slots[slot].armed = 1, p->slots[slot].armed_point = point;
        return p->slots[slot].eventfd;
}

static void canvas_syncobj_reset(struct canvas_syncobj_pool *p)
{
        if (!p)
                return;
        for (long i = 0; i < p->count; i++)
                if (p->slots[i].used)
                        canvas_syncobj_drop(p, i);
        if (p->slots)
                compositor_call(syscall(munmap), (positive)p->slots, (unsigned long)p->cap * sizeof(*p->slots), 0, 0, 0, 0);
        p->slots = 0, p->count = p->cap = 0;
}

