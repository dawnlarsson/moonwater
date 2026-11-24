/*
        Each Scene's wallpaper.

        A Scene has one wallpaper, said by a word (registry.c's setting_wallpaper_word_ok): auto, the moon over its sky in the theme's look;
        dark or white, the moon in that look whatever the theme; picture:NAME, a picture `canvas wallpaper picture FILE` remembered; or
        curated:HEX, one of the feed's, kept by its digest. A Scene is known by its screen's name and its number among the screen's desktop
        Scenes, from 1, so it is the same Scene again when it is made again: at a boot each screen starts with its first Scene, and the second
        has its wallpaper back when it is added. A Scene with no word of its own is auto, and a Scene added takes the word of the one it was
        added from, unless its number has one kept from before. A fullscreen Scene has none: its ground is black.

        The compositor alone writes the words (SETTING_WALLPAPER_SCENES_PATH, compositor_walls_write), which it changes for a press of its
        own and for what the command asks it in a file of SETTING_WALLPAPER_ASK's name (compositor_walls_asks); the list of the pictures
        remembered is the command's alone, which this reads, and a word of a picture the list does not have is auto again. present.c makes
        each screen's wallpaper from the word of the Scene shown on it (compositor_wall_start), and a Scene's card in the overview, or a
        word's in the editor, is drawn from a miniature of the word (compositor_wall_mini): the moon drawn at the card's size, or a picture's
        miniature file read, never the picture.
*/

static positive compositor_map_file(string_address path, positive address_to size); // compositor.c

static const char *compositor_walls_path = SETTING_WALLPAPER_SCENES_PATH;
static const char *compositor_walls_pictures_path = SETTING_WALLPAPER_PICTURES_PATH;
static const char *compositor_walls_ask_dir = SETTING_WALLPAPER_ASK_DIR;

struct compositor_wall_line
{
        char screen[WINDOWS_OUTPUT_NAME];
        unsigned int scene;
        char word[SETTING_WALLPAPER_WORD];
};

static struct
{
        struct compositor_wall_line *line; // the Scenes' words, as many as there are
        unsigned long line_bytes;
        unsigned int lines;
        char (*name)[SETTING_WALLPAPER_NAME]; // the pictures remembered, the most recent first
        unsigned long name_bytes;
        unsigned int names;
        unsigned int gen; // one more at every change of a word or the list, which the wallpaper and the miniatures are made again for
} compositor_walls;

static unsigned int compositor_walls_gen(void)
{
        return compositor_walls.gen;
}

// A screen's name as its Scenes are kept by: its connector's, or "-" for a space with no screen named.
static const char *compositor_wall_screen_name(int screen)
{
        if (screen < 0 || (unsigned int)screen >= compositor.outputs || screen >= WINDOWS_OUTPUTS || !compositor.output[screen].name[0])
                return "-";
        return compositor.output[screen].name;
}

// The number of a desktop Scene among its screen's desktop Scenes, from 1; 0 for a fullscreen one or none.
static unsigned int compositor_wall_number(const struct compositor_space *sp)
{
        unsigned int number = 0;

        if (!sp || sp->kind != COMPOSITOR_SPACE_DESKTOP)
                return 0;
        for (unsigned int i = 0; i < compositor.spaces.count; i++)
                if (compositor.spaces.list[i].screen == sp->screen && compositor.spaces.list[i].kind == COMPOSITOR_SPACE_DESKTOP)
                {
                        number++;
                        if (compositor.spaces.list[i].id == sp->id)
                                return number;
                }
        return 0;
}

static int compositor_wall_find(const char *screen, unsigned int scene)
{
        for (unsigned int at = 0; at < compositor_walls.lines; at++)
                if (compositor_walls.line[at].scene == scene && string_equals((string_address)compositor_walls.line[at].screen, (string_address)screen))
                        return (int)at;
        return -1;
}

// The word of Scene number scene of a screen: its own, else auto.
static const char *compositor_wall_word(int screen, unsigned int scene)
{
        int at = scene ? compositor_wall_find(compositor_wall_screen_name(screen), scene) : -1;

        return at >= 0 ? compositor_walls.line[at].word : "auto";
}

// The word of a Scene, "" for a fullscreen one (black).
static const char *compositor_wall_word_of(const struct compositor_space *sp)
{
        unsigned int number = compositor_wall_number(sp);

        return number ? compositor_wall_word(sp->screen, number) : "";
}

// The word of the Scene shown on a screen: "" while it is a fullscreen Scene. Before the Scenes are, the first's.
static const char *compositor_wall_shown(int screen)
{
        if (!compositor.spaces.ready)
                return compositor_wall_word(screen, 1);
        return compositor_wall_word_of(compositor_space_find(screen >= 0 && screen < WINDOWS_OUTPUTS ? compositor.spaces.active[screen] : 0));
}

static int compositor_wall_listed(const char *name)
{
        for (unsigned int at = 0; at < compositor_walls.names; at++)
                if (string_equals((string_address)compositor_walls.name[at], (string_address)name))
                        return 1;
        return 0;
}

static void compositor_walls_write(void)
{
        unsigned long room = (unsigned long)compositor_walls.lines * (WINDOWS_OUTPUT_NAME + SETTING_WALLPAPER_WORD + 8) + 1, at = 0;
        void *text = 0;
        unsigned long cap = 0;

        if (!mem_buf_grow(&text, &cap, room, 0))
                return compositor_kmsg((string_address) "compositor: no memory to keep the Scenes' wallpapers in ", 0, (string_address)compositor_walls_path);
        for (unsigned int k = 0; k < compositor_walls.lines; k++)
        {
                const struct compositor_wall_line *l = &compositor_walls.line[k];
                char *line = (char *)text + at;

                string_copy((string_address)line, (string_address)l->screen);
                at += string_length((string_address)line);
                ((char *)text)[at++] = ' ';
                at += positive_into_string((p8 *)text + at, l->scene);
                ((char *)text)[at++] = ' ';
                string_copy((string_address)text + at, (string_address)l->word);
                at += string_length((string_address)text + at);
                ((char *)text)[at++] = '\n';
        }
        (void)file_write_atomic_at(AT_FDCWD, (string_address)compositor_walls_path, (p8 *)text, (positive)at, 0644);
        mem_buf_free(&text, &cap, 0);
}

// Room for one more line; the line added (zeroed), or none with no memory for it.
static struct compositor_wall_line *compositor_wall_line_add(void)
{
        void *p = compositor_walls.line;

        if (!mem_buf_grow(&p, &compositor_walls.line_bytes, (unsigned long)(compositor_walls.lines + 1) * sizeof(compositor_walls.line[0]),
                          MEM_BUF_KEEP | MEM_BUF_GROW_2X))
                return 0;
        compositor_walls.line = (struct compositor_wall_line *)p;
        memory_zero(&compositor_walls.line[compositor_walls.lines], sizeof(compositor_walls.line[0]));
        return &compositor_walls.line[compositor_walls.lines++];
}

static void compositor_wall_line_drop(unsigned int at)
{
        if (at >= compositor_walls.lines)
                return;
        memory_copy(&compositor_walls.line[at], &compositor_walls.line[at + 1], (positive)(compositor_walls.lines - at - 1) * sizeof(compositor_walls.line[0]));
        compositor_walls.lines--;
}

static void compositor_wall_changed(void)
{
        compositor_walls.gen++;
        compositor_damage(0, 0, compositor.width, compositor.height);
}

// Scene number scene of the screen named screen has word (auto is no line of its own), kept at once.
static void compositor_wall_put_named(const char *screen, unsigned int scene, const char *word)
{
        int at = compositor_wall_find(screen, scene);
        struct compositor_wall_line *l;

        if (!scene || !setting_wallpaper_word_ok(word, string_length((string_address)word)))
                return;
        if (at >= 0 && string_equals((string_address)compositor_walls.line[at].word, (string_address)word))
                return;
        if (string_equals((string_address)word, "auto"))
        {
                if (at < 0)
                        return;
                compositor_wall_line_drop((unsigned int)at);
        }
        else if (at >= 0)
                string_copy_bounded((p8 *)compositor_walls.line[at].word, (string_address)word, SETTING_WALLPAPER_WORD);
        else if ((l = compositor_wall_line_add()))
        {
                string_copy_bounded((p8 *)l->screen, (string_address)screen, WINDOWS_OUTPUT_NAME);
                l->scene = scene;
                string_copy_bounded((p8 *)l->word, (string_address)word, SETTING_WALLPAPER_WORD);
        }
        compositor_walls_write();
        compositor_wall_changed();
}

static void compositor_wall_put(int screen, unsigned int scene, const char *word)
{
        compositor_wall_put_named(compositor_wall_screen_name(screen), scene, word);
}

// The desktop Scene a word for "the Scene you are on" goes to on a screen: the one shown, or, on a fullscreen Scene, the one it came from.
static unsigned int compositor_wall_current(int screen)
{
        struct compositor_space *sp;

        compositor_spaces_ensure();
        sp = compositor_space_find(screen >= 0 && screen < WINDOWS_OUTPUTS ? compositor.spaces.active[screen] : 0);
        if (sp && sp->kind != COMPOSITOR_SPACE_DESKTOP)
                sp = compositor_space_find(sp->return_id);
        if (!sp || sp->kind != COMPOSITOR_SPACE_DESKTOP)
                sp = compositor_space_find(compositor_space_first_desktop(screen));
        return compositor_wall_number(sp);
}

// A Scene added on a screen, the last of its desktop Scenes: with no word kept for its number, it takes the word of the Scene shown there.
static void compositor_wall_scene_added(int screen)
{
        unsigned int number = compositor_space_desktop_count(screen);

        if (number && compositor_wall_find(compositor_wall_screen_name(screen), number) < 0)
                compositor_wall_put(screen, number, compositor_wall_word(screen, compositor_wall_current(screen)));
}

// A desktop Scene taken away: its word goes, and the Scenes after it are a number less, as they are on the screen.
static void compositor_wall_scene_removed(const struct compositor_space *sp)
{
        unsigned int number = compositor_wall_number(sp), moved = 0;
        const char *screen;
        int at;

        if (!number)
                return;
        screen = compositor_wall_screen_name(sp->screen);
        if ((at = compositor_wall_find(screen, number)) >= 0)
                compositor_wall_line_drop((unsigned int)at), moved = 1;
        for (unsigned int k = 0; k < compositor_walls.lines; k++)
                if (compositor_walls.line[k].scene > number && string_equals((string_address)compositor_walls.line[k].screen, (string_address)screen))
                        compositor_walls.line[k].scene--, moved = 1;
        if (moved)
                compositor_walls_write(), compositor_wall_changed();
}

// The pictures remembered, read again: a name a line, each once.
static void compositor_walls_pictures_read(void)
{
        positive size = 0, mapped = compositor_map_file((string_address)compositor_walls_pictures_path, &size);
        const char *text = (const char *)mapped;

        compositor_walls.names = 0;
        for (positive at = 0; mapped && at < size;)
        {
                positive stop = at;
                void *p = compositor_walls.name;

                while (stop < size && text[stop] != '\n')
                        stop++;
                if (setting_wallpaper_name_ok(text + at, stop - at) &&
                    mem_buf_grow(&p, &compositor_walls.name_bytes, (unsigned long)(compositor_walls.names + 1) * SETTING_WALLPAPER_NAME,
                                 MEM_BUF_KEEP | MEM_BUF_GROW_2X))
                {
                        compositor_walls.name = (char (*)[SETTING_WALLPAPER_NAME])p;
                        memory_copy(compositor_walls.name[compositor_walls.names], text + at, stop - at);
                        compositor_walls.name[compositor_walls.names][stop - at] = 0;
                        if (!compositor_wall_listed(compositor_walls.name[compositor_walls.names]))
                                compositor_walls.names++;
                }
                at = stop + 1;
        }
        if (mapped)
                compositor_call(syscall(munmap), mapped, size, 0, 0, 0, 0);
}

// Every word read again, and the list; a picture's word whose picture the list does not have any more is auto again, and the file says so.
static void compositor_walls_read(void)
{
        positive size = 0, mapped = compositor_map_file((string_address)compositor_walls_path, &size), at = 0;
        struct compositor_wall_line got;
        unsigned int dropped = 0;

        compositor_walls_pictures_read();
        compositor_walls.lines = 0;
        while (mapped && setting_wallpaper_line((const char *)mapped, size, &at, got.screen, &got.scene, got.word))
        {
                struct compositor_wall_line *l;

                if (!memory_compare(got.word, "picture:", 8) && !compositor_wall_listed(got.word + 8))
                {
                        dropped++;
                        continue;
                }
                if (compositor_wall_find(got.screen, got.scene) < 0 && (l = compositor_wall_line_add()))
                        *l = got;
        }
        if (mapped)
                compositor_call(syscall(munmap), mapped, size, 0, 0, 0, 0);
        if (dropped)
                compositor_walls_write();
        compositor_wall_changed();
}

static void compositor_wall_edit(int screen, int how); // the editor: on (1), off (0) or turned (-1)

/*
        What the command asks (setting.c's locale_wallpaper_ask), a file each in SETTING_WALLPAPER_ASK_DIR named SETTING_WALLPAPER_ASK, the
        asker's pid, a dash and its count: root's own, written by root alone, read and taken away. A line is "edit on|off|toggle", "scene
        SCREEN N WORD" (SCREEN - for the screen you are on) or "current WORD" (the Scene you are on). Read at the start too, for what was asked
        while the desktop was not running.
*/
static int compositor_wall_ask_name(const char *name)
{
        positive length = string_length((string_address)name), prefix = sizeof(SETTING_WALLPAPER_ASK) - 1;

        if (length <= prefix || length > prefix + 40 || memory_compare(name, SETTING_WALLPAPER_ASK, prefix))
                return 0;
        for (positive at = prefix; at < length; at++)
                if (!((name[at] >= '0' && name[at] <= '9') || name[at] == '-'))
                        return 0;
        return 1;
}

static void compositor_wall_ask_line(char *line)
{
        char *word[4] = {0};
        unsigned int count = 0;

        for (char *walk = line; *walk && count < 4;)
        {
                while (*walk == ' ')
                        *walk++ = 0;
                if (!*walk)
                        break;
                word[count++] = walk;
                while (*walk && *walk != ' ')
                        walk++;
        }
        if (count == 2 && string_equals((string_address)word[0], "edit"))
                compositor_wall_edit(compositor_space_current_screen(),
                                     string_equals((string_address)word[1], "on") ? 1 : string_equals((string_address)word[1], "off") ? 0 : -1);
        else if (count == 2 && string_equals((string_address)word[0], "current"))
        {
                int screen = compositor_space_current_screen();

                compositor_wall_put(screen, compositor_wall_current(screen), word[1]);
        }
        else if (count == 4 && string_equals((string_address)word[0], "scene"))
        {
                unsigned int scene = 0;
                positive digits = 0;

                for (; word[2][digits] >= '0' && word[2][digits] <= '9' && digits < 4; digits++)
                        scene = scene * 10 + (unsigned int)(word[2][digits] - '0');
                if (word[2][digits] || !scene)
                        return;
                if (string_equals((string_address)word[1], "-"))
                        compositor_wall_put(compositor_space_current_screen(), scene, word[3]);
                else if (string_length((string_address)word[1]) < WINDOWS_OUTPUT_NAME)
                        compositor_wall_put_named(word[1], scene, word[3]);
        }
}

static void compositor_walls_asks(void)
{
        bipolar directory = system_open_at(AT_FDCWD, (string_address)compositor_walls_ask_dir, FILE_READ | O_DIRECTORY | O_CLOEXEC);
        p8 entries[2048];
        long got;

        while (directory >= 0 && (got = compositor_call(syscall(getdents64), (positive)directory, (positive)entries, sizeof(entries), 0, 0, 0)) > 0)
                for (long at = 0; at < got;)
                {
                        const struct linux_dirent64 *entry = (const struct linux_dirent64 *)(entries + at);
                        char text[SETTING_WALLPAPER_WORD + WINDOWS_OUTPUT_NAME + 32];
                        p8 info[256] = {0};
                        bipolar handle;
                        long read = -1;

                        at += entry->d_reclen;
                        if (!compositor_wall_ask_name(entry->d_name))
                                continue;
                        handle = system_open_at(directory, (string_address)entry->d_name, FILE_READ | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
                        // A regular file of root's that no one else may write: anything else at the name is no ask.
                        if (handle >= 0 && compositor_call(syscall(statx), (positive)handle, (positive) "", 0x1000, 0x7ff, (positive)info, 0) >= 0 &&
                            (memory_load_unaligned(p16, info + 28) & 0170000u) == 0100000u && !memory_load_unaligned(p32, info + 20) &&
                            !(memory_load_unaligned(p16, info + 28) & 0022))
                                read = system_read_once(handle, text, sizeof(text) - 1);
                        if (handle >= 0)
                                system_close(handle);
                        (void)compositor_call(syscall(unlinkat), (positive)directory, (positive)entry->d_name, 0, 0, 0, 0);
                        if (read <= 0)
                                continue;
                        text[read] = 0;
                        for (char *line = text; *line;)
                        {
                                char *stop = line;

                                while (*stop && *stop != '\n')
                                        stop++;
                                if (*stop)
                                        *stop++ = 0;
                                compositor_wall_ask_line(line);
                                line = stop;
                        }
                }
        if (directory >= 0)
                system_close(directory);
}

/*
        A word's miniature, w by h, made once and kept while it is asked for (compositor_walls_keep lets go of one not asked for in
        COMPOSITOR_WALL_MINI_KEEP): the moon at that size in the word's look (made on the loop: a card's few thousand pixels are a fraction of
        a millisecond of ui_moon_night), or a picture's miniature file (NAME.mini, a few hundred kilobytes) cut to the card's shape as the
        wallpaper is cut to the screen's. 0 when there is none: a picture whose miniature is not there yet, or no memory.
*/
#define COMPOSITOR_WALL_MINI_KEEP (5ull * 1000000000ull)
struct compositor_wall_mini
{
        char word[SETTING_WALLPAPER_WORD];
        int w, h, dark;
        unsigned int accent, gen;
        unsigned int *pixels;
        unsigned long bytes;
        positive used;
};

static struct
{
        struct compositor_wall_mini *list;
        unsigned long list_bytes;
        unsigned int count;
} compositor_wall_minis;

// A picture's miniature file into w by h: cut at its long sides to w by h's shape, as the wallpaper is (compositor_wall_picture).
static int compositor_wall_mini_picture(const char *word, unsigned int *into, int w, int h)
{
        char path[sizeof(SETTING_WALLPAPER_DIR "/curated/") + SETTING_WALLPAPER_WORD + 8];
        int width = 0, height = 0;
        positive bytes = 0, vis_w, vis_h, ox, oy;
        unsigned int *mini;

        if (!setting_wallpaper_file(word, ".mini", path, sizeof(path)) || !(mini = setting_wallpaper_mini_read(path, &width, &height, &bytes)))
                return 0;
        if ((positive)width * (positive)h > (positive)height * (positive)w)
                vis_h = (positive)height, vis_w = (positive)height * (positive)w / (positive)h, ox = ((positive)width - vis_w) / 2, oy = 0;
        else
                vis_w = (positive)width, vis_h = (positive)width * (positive)h / (positive)w, ox = 0, oy = ((positive)height - vis_h) / 2;
        vis_w = vis_w ? vis_w : 1, vis_h = vis_h ? vis_h : 1;
        memory_resample_u32_rect(into, (positive)w, (positive)w, (positive)h, mini + oy * (positive)width + ox, (positive)width, vis_w, vis_h,
                                 (positive)w, (positive)h, 0, 0);
        memory_free(mini, bytes);
        return 1;
}

static const unsigned int *compositor_wall_mini(const char *word, int w, int h)
{
        int dark = !(compositor.table && compositor.table->themed && compositor.table->theme == UI_THEME_LIGHT), moon, own;
        unsigned int accent = taskbar_wall_accent();
        positive now = clock_monotonic_nanoseconds();
        struct compositor_wall_mini *m = 0;
        void *p;

        if (!word || !word[0] || w <= 0 || h <= 0)
                return 0;
        moon = string_equals((string_address)word, "auto") || string_equals((string_address)word, "dark") || string_equals((string_address)word, "white");
        own = setting_wallpaper_dark(word, dark);
        for (unsigned int at = 0; at < compositor_wall_minis.count; at++)
        {
                struct compositor_wall_mini *k = &compositor_wall_minis.list[at];

                if (k->w == w && k->h == h && string_equals((string_address)k->word, (string_address)word))
                {
                        m = k;
                        break;
                }
        }
        if (m && m->pixels && (moon ? m->dark == own && m->accent == accent : m->gen == compositor_walls.gen))
                return m->used = now, m->pixels;
        if (!m)
        {
                p = compositor_wall_minis.list;
                if (!mem_buf_grow(&p, &compositor_wall_minis.list_bytes, (unsigned long)(compositor_wall_minis.count + 1) * sizeof(*m),
                                  MEM_BUF_KEEP | MEM_BUF_GROW_2X))
                        return 0;
                compositor_wall_minis.list = (struct compositor_wall_mini *)p;
                m = &compositor_wall_minis.list[compositor_wall_minis.count++];
                memory_zero(m, sizeof(*m));
                string_copy_bounded((p8 *)m->word, (string_address)word, SETTING_WALLPAPER_WORD);
                m->w = w, m->h = h;
        }
        p = m->pixels;
        if (!mem_buf_grow(&p, &m->bytes, (unsigned long)w * (unsigned long)h * 4, MEM_BUF_GPU_FORGET))
                return m->pixels = 0, m->bytes = 0, (const unsigned int *)0;
        m->pixels = (unsigned int *)p, m->used = now, m->dark = own, m->accent = accent, m->gen = compositor_walls.gen;
        if (moon)
        {
                int rect[4] = {0};

                memory_fill_u32_rect(m->pixels, (positive)w, (positive)w, (positive)h, 0xff000000u);
                taskbar_wall_draw(m->pixels, w, w, h, own, 1, 45, 1, accent, compositor_wall_turn, 0, rect);
                memory_or_u32(m->pixels, m->pixels, (positive)w * (positive)h, 0xff000000u);
                return m->pixels;
        }
        if (compositor_wall_mini_picture(word, m->pixels, w, h))
                return m->pixels;
        // No miniature to be had (gone, or not made yet): the ground's colour, made again at the next change of the list.
        memory_fill_u32_rect(m->pixels, (positive)w, (positive)w, (positive)h, compositor_ground | 0xff000000u);
        return m->pixels;
}

// The miniatures not asked for a while let go of (every pass of the loop: a look along a short list).
static void compositor_walls_keep(positive now)
{
        for (unsigned int at = 0; at < compositor_wall_minis.count;)
        {
                struct compositor_wall_mini *m = &compositor_wall_minis.list[at];
                void *p = m->pixels;

                if (now - m->used < COMPOSITOR_WALL_MINI_KEEP)
                {
                        at++;
                        continue;
                }
                mem_buf_free(&p, &m->bytes, MEM_BUF_GPU_FORGET);
                memory_copy(m, m + 1, (positive)(compositor_wall_minis.count - at - 1) * sizeof(*m));
                compositor_wall_minis.count--;
        }
}

// A Scene card's ground in the overview, w by h of the screen s: the wallpaper's own miniature for a Scene whose word is the one shown,
// else its word's; 0 for none (the card is the colour).
static const unsigned int *compositor_wall_card(const struct compositor_space *sp, const int s[4], int w, int h)
{
        const char *word = compositor_wall_word_of(sp);

        if (string_equals((string_address)word, (string_address)compositor_wall_shown(sp->screen)))
                return compositor_scenes_wall_of(s, w, h);
        return compositor_wall_mini(word, w, h);
}

// The editor of a Scene's wallpaper over the desktop: the next step's.
static void compositor_wall_edit(int screen, int how)
{
        (void)screen, (void)how;
}
