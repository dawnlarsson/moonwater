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
static const char *compositor_walls_curated_path = SETTING_WALLPAPER_CURATED_PATH;
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
        char (*curated)[SETTING_WALLPAPER_WORD]; // the feed's the command verified (curated:HEX), in the feed's order
        unsigned long curated_bytes;
        unsigned int curateds;
        unsigned int gen;   // one more at every change of a word or the list, which the wallpaper is made again for
        unsigned int listed; // and of the list alone, which a picture's miniature is read again for
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
        {
                memory_zero(compositor_walls.line[at].word, SETTING_WALLPAPER_WORD);
                string_copy_bounded((p8 *)compositor_walls.line[at].word, (string_address)word, SETTING_WALLPAPER_WORD);
        }
        else if ((l = compositor_wall_line_add()))
        {
                memory_zero(l, sizeof(*l));
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

// What the words and the list are, in a word: a read that changed nothing changes nothing (the compositor hears its own writes).
static unsigned int compositor_walls_sum(int list)
{
        unsigned int sum = 2166136261u;

        if (list)
                return hash_fnv1a(hash_fnv1a(sum ^ compositor_walls.names, compositor_walls.name, (positive)compositor_walls.names * SETTING_WALLPAPER_NAME) ^
                                      compositor_walls.curateds,
                                  compositor_walls.curated, (positive)compositor_walls.curateds * SETTING_WALLPAPER_WORD);
        return hash_fnv1a(sum ^ compositor_walls.lines, compositor_walls.line, (positive)compositor_walls.lines * sizeof(compositor_walls.line[0]));
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
                        memory_zero(compositor_walls.name[compositor_walls.names], SETTING_WALLPAPER_NAME);
                        memory_copy(compositor_walls.name[compositor_walls.names], text + at, stop - at);
                        if (!compositor_wall_listed(compositor_walls.name[compositor_walls.names]))
                                compositor_walls.names++;
                }
                at = stop + 1;
        }
        if (mapped)
                compositor_call(syscall(munmap), mapped, size, 0, 0, 0, 0);
        // The feed's, as the command verified them: a line each, the picture's digest in hex and its id.
        mapped = compositor_map_file((string_address)compositor_walls_curated_path, &size), text = (const char *)mapped;
        compositor_walls.curateds = 0;
        for (positive at = 0; mapped && at < size;)
        {
                positive stop = at;
                void *p = compositor_walls.curated;
                char word[SETTING_WALLPAPER_WORD];

                while (stop < size && text[stop] != '\n')
                        stop++;
                memory_zero(word, sizeof(word));
                if (stop - at > 65 && text[at + 64] == ' ')
                        memory_copy(word, "curated:", 8), memory_copy(word + 8, text + at, 64);
                if (setting_wallpaper_word_ok(word, string_length((string_address)word)) &&
                    mem_buf_grow(&p, &compositor_walls.curated_bytes, (unsigned long)(compositor_walls.curateds + 1) * SETTING_WALLPAPER_WORD,
                                 MEM_BUF_KEEP | MEM_BUF_GROW_2X))
                {
                        compositor_walls.curated = (char (*)[SETTING_WALLPAPER_WORD])p;
                        memory_copy(compositor_walls.curated[compositor_walls.curateds++], word, SETTING_WALLPAPER_WORD);
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
        unsigned int dropped = 0, list = compositor_walls_sum(1), words = compositor_walls_sum(0);

        compositor_walls_pictures_read();
        if (compositor_walls_sum(1) != list)
                compositor_walls.listed++;
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
                {
                        memory_zero(l, sizeof(*l));
                        string_copy((string_address)l->screen, (string_address)got.screen);
                        string_copy((string_address)l->word, (string_address)got.word);
                        l->scene = got.scene;
                }
        }
        if (mapped)
                compositor_call(syscall(munmap), mapped, size, 0, 0, 0, 0);
        if (dropped)
                compositor_walls_write();
        if (dropped || compositor_walls_sum(0) != words || compositor_walls_sum(1) != list)
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
        // A curated picture fetched (canvas wallpaper curated get): the wallpaper and the miniatures made again with it.
        if (count == 1 && string_equals((string_address)word[0], "made"))
                compositor_walls.listed++, compositor_wall_changed();
        else if (count == 2 && string_equals((string_address)word[0], "edit"))
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
        if (m && m->pixels && (moon ? m->dark == own && m->accent == accent : m->gen == compositor_walls.listed))
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
        m->pixels = (unsigned int *)p, m->used = now, m->dark = own, m->accent = accent, m->gen = compositor_walls.listed;
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

static void compositor_wall_editor_tick(positive now);

// The miniatures not asked for a while let go of (every pass of the loop: a look along a short list), and the editor's pass.
static void compositor_walls_keep(positive now)
{
        compositor_wall_editor_tick(now);
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

/*
        The editor (canvas wallpaper edit, Settings' Wallpaper row): the wallpaper of the Scene you are on, chosen over the desktop itself, in
        the Scenes overview's look. A strip of cards comes down from the top of the screen as the overview's Scene cards do, on the kit's
        spring: the Moonwater moon with the theme (its card the dark moon and the white one, split along a diagonal), dark and white; then the
        pictures remembered, the most recent first, each with the close button over its corner under the hand, which forgets it; then the
        feed's; and last a card with a folder, which opens Files to pick a picture (files -pick image). The card of the word the Scene has is
        ringed in the accent, the one under the hand lit. A press on a card gives the Scene its word at once and the editor stays: a Scene
        switched to (Ctrl+Alt+arrows, the taskbar) keeps it open, so the next Scene takes the same picture with one press. A press anywhere
        else, the right button, Escape or the verb again shuts it. The cards are in rows when one row would make them small, never more than
        about half the screen high; they are as many as there are words.

        While it is shown every window of its screen is drawn at 4 percent: over them lies a veil of the wallpaper at 96 percent
        (compositor_wall_veil_keep), one blended frame, which the card's path and the processor's draw as they draw any blended picture; a
        window under it shows through at 10 of 255. The band is drawn after it and keeps its look. The veil comes up with the strip in
        COMPOSITOR_WALL_VEIL_STEPS steps, each a pass over the screen's pixels made between presents while no card job reads it.
*/
#define COMPOSITOR_WALL_VEIL 245
#define COMPOSITOR_WALL_VEIL_STEPS 4
static struct
{
        int open, screen;
        b64 shown, speed; // the spring, as the overview's (0 to COMPOSITOR_SCENES_FULL) and its speed
        positive stepped;
        int (*card)[4];   // the cards where they are all the way down, and each one's word ("" the folder)
        unsigned long card_bytes;
        char (*word)[SETTING_WALLPAPER_WORD];
        unsigned long word_bytes;
        unsigned int cards, gen;
        int hover, hover_close, hover_seg, pressed, press_card, press_seg, at_x, at_y;
        unsigned int *veil; // the veil, the screen's size, its rect, its step and the wallpaper it was made from at its turn
        unsigned long veil_bytes;
        int veil_rect[4], veil_level;
        const unsigned int *veil_of;
        b64 veil_turn;
        unsigned int *pill; // the rounded Dark | Light | Auto segmented switch drawn over the Moonwater dynamic wallpaper card
        unsigned long pill_bytes;
        int pill_w, pill_h, pill_active, pill_hover;
        unsigned int pill_key;
} compositor_wall_editor = {.hover = -1, .hover_seg = -1, .press_card = -1, .press_seg = -1};

static int compositor_wall_is_moon(const char *word)
{
        return word && (string_equals((string_address)word, "auto") || string_equals((string_address)word, "dark") || string_equals((string_address)word, "white"));
}

static int compositor_wall_moon_seg(const char *word)
{
        if (!word)
                return -1;
        if (string_equals((string_address)word, "dark"))
                return 0;
        if (string_equals((string_address)word, "white"))
                return 1;
        if (string_equals((string_address)word, "auto"))
                return 2;
        return -1;
}

static const char *compositor_wall_seg_word(int seg)
{
        return seg == 0 ? "dark" : seg == 1 ? "white" : "auto";
}

// The rounded Dark | Light | Auto switch rect inside the Moonwater dynamic card (card 0).
static int compositor_wall_editor_pill(const int card[4], int scale, int out[4])
{
        int pad = 6 * scale / 100, h = 24 * scale / 100, w;

        if (card[2] < 18 || card[3] < 18)
                return 0;
        if (pad < 3)
                pad = 3;
        if (h > card[3] / 3)
                h = card[3] / 3;
        if (h < 10)
                h = 10;
        w = card[2] - 2 * pad;
        if (w < 18)
                w = card[2];
        out[0] = card[0] + (card[2] - w) / 2, out[1] = card[1] + card[3] - h - pad, out[2] = w, out[3] = h;
        return 1;
}

static int compositor_wall_editor_pill_seg(int x, int y)
{
        int screen = compositor_wall_editor.screen, scale = compositor_output_scale(screen) > 0 ? compositor_output_scale(screen) : compositor.scale;
        int pill[4], seg;

        if (!compositor_wall_editor.open || !compositor_wall_editor.cards || !compositor_wall_editor_pill(compositor_wall_editor.card[0], scale, pill))
                return -1;
        if (x < pill[0] || x >= pill[0] + pill[2] || y < pill[1] || y >= pill[1] + pill[3])
                return -1;
        seg = (x - pill[0]) * 3 / pill[2];
        return seg < 0 ? 0 : seg > 2 ? 2 : seg;
}

static int compositor_wall_editing(void)
{
        return compositor_wall_editor.open;
}

static b64 compositor_wall_editor_t(void)
{
        if (!compositor_glides())
                return compositor_wall_editor.open ? UI_ONE : 0;
        return compositor_wall_editor.shown / 1000;
}

static int compositor_wall_editor_shown(void)
{
        return compositor_wall_editor.open || compositor_wall_editor_t() > 0;
}

static void compositor_wall_editor_damage(void)
{
        unsigned int count = 0;
        const int (*screens)[4] = compositor_screens(&count);
        int screen = compositor_wall_editor.screen;

        if (screen >= 0 && (unsigned int)screen < count)
                compositor_damage(screens[screen][0], screens[screen][1], screens[screen][2], screens[screen][3]);
        else
                compositor_damage(0, 0, compositor.width, compositor.height);
}

// The cards laid out again: the Moonwater dynamic wallpaper (with its Dark | Light | Auto switch), the pictures, the curated feed's, and the folder last.
static void compositor_wall_editor_lay(void)
{
        unsigned int count = 0, n = 1 + compositor_walls.names + compositor_walls.curateds + 1, cols, rows;
        const int (*screens)[4] = compositor_screens(&count);
        int screen = compositor_wall_editor.screen, gap = compositor_scenes_gap(), room[4], s[4], width, most;
        long long cw, ch;
        void *p;

        compositor_wall_editor.cards = 0, compositor_wall_editor.gen = compositor_walls.gen;
        if (screen < 0 || (unsigned int)screen >= count || screens[screen][2] <= 0 || screens[screen][3] <= 0)
                return;
        memory_copy(s, screens[screen], sizeof(s));
        p = compositor_wall_editor.card;
        if (!mem_buf_grow(&p, &compositor_wall_editor.card_bytes, (unsigned long)n * sizeof(compositor_wall_editor.card[0]), 0))
                return;
        compositor_wall_editor.card = (int (*)[4])p;
        p = compositor_wall_editor.word;
        if (!mem_buf_grow(&p, &compositor_wall_editor.word_bytes, (unsigned long)n * SETTING_WALLPAPER_WORD, 0))
                return;
        compositor_wall_editor.word = (char (*)[SETTING_WALLPAPER_WORD])p;
        string_copy((string_address)compositor_wall_editor.word[0], "auto");
        for (unsigned int at = 0; at < compositor_walls.names; at++)
        {
                string_copy((string_address)compositor_wall_editor.word[1 + at], "picture:");
                string_append_bounded((p8 *)compositor_wall_editor.word[1 + at], (string_address)compositor_walls.name[at], SETTING_WALLPAPER_WORD);
        }
        for (unsigned int at = 0; at < compositor_walls.curateds; at++)
                memory_copy(compositor_wall_editor.word[1 + compositor_walls.names + at], compositor_walls.curated[at], SETTING_WALLPAPER_WORD);
        compositor_wall_editor.word[n - 1][0] = 0;
        // As the overview's Scene cards: a fifth of the room high, the screen's shape; in rows, smaller, when one row would not hold them.
        compositor_room(s, room);
        width = room[2] - 2 * gap, most = room[3] * 55 / 100;
        ch = room[3] * 18 / 100, cw = ch * s[2] / s[3];
        for (;;)
        {
                cols = cw + gap > 0 ? (unsigned int)((width + gap) / (cw + gap)) : 1;
                cols = cols ? cols : 1, rows = (n + cols - 1) / cols;
                if ((long long)rows * (ch + gap) - gap <= most || cw <= 16)
                        break;
                cw = cw * 7 / 8, ch = cw * s[3] / s[2];
        }
        for (unsigned int at = 0; at < n; at++)
        {
                unsigned int row = at / cols, in_row = row + 1 == rows ? n - row * cols : cols, col = at % cols;
                long long left = room[0] + gap + (width - (long long)in_row * cw - (long long)(in_row - 1) * gap) / 2;

                compositor_wall_editor.card[at][0] = (int)(left + col * (cw + gap));
                compositor_wall_editor.card[at][1] = room[1] + gap + (int)(row * (ch + gap));
                compositor_wall_editor.card[at][2] = (int)cw, compositor_wall_editor.card[at][3] = (int)ch;
        }
        compositor_wall_editor.cards = n;
}

// Card i where it is drawn: lifted above the screen's top by the share not shown, all the rows together.
static void compositor_wall_editor_card_shown(unsigned int i, const int s[4], b64 t, int out[4])
{
        const int *last = compositor_wall_editor.card[compositor_wall_editor.cards - 1];
        long long lift = (long long)(last[1] + last[3] + compositor_scenes_gap() - s[1]);

        memory_copy(out, compositor_wall_editor.card[i], 4 * sizeof(int));
        out[1] -= (int)((lift * (UI_ONE - t)) >> UI_Q);
}

static int compositor_wall_editor_card_at(int x, int y)
{
        return compositor_wall_editor.open ? compositor_scenes_hit((const int (*)[4])compositor_wall_editor.card, compositor_wall_editor.cards, x, y) : -1;
}

// Whether card i has the close button (a picture's), and where: the window close button's place on a frame of the card (compositor_close_box).
static int compositor_wall_editor_close(int card, int *x, int *y, int *side)
{
        int screen = compositor_wall_editor.screen, scale = compositor_output_scale(screen) > 0 ? compositor_output_scale(screen) : compositor.scale;
        struct compositor_frame f;

        if (card < 0 || (unsigned int)card >= compositor_wall_editor.cards || memory_compare(compositor_wall_editor.word[card], "picture:", 8))
                return 0;
        memory_zero(&f, sizeof(f));
        f.x = compositor_wall_editor.card[card][0], f.y = compositor_wall_editor.card[card][1];
        f.width = compositor_wall_editor.card[card][2], f.height = compositor_wall_editor.card[card][3], f.scale = scale, f.close = 1;
        return compositor_close_box(&f, x, y, side);
}

static int compositor_wall_editor_on_close(int card, int px, int py)
{
        int x, y, side;

        return compositor_wall_editor_close(card, &x, &y, &side) && px >= x && px < x + side && py >= y && py < y + side;
}

static void compositor_wall_start_program(string_address *argv);

// The editor opened on a screen (how 1), shut (0), or turned (-1). The overview shuts when it opens, as the editor does when the overview opens.
static void compositor_wall_edit(int screen, int how)
{
        int open = how < 0 ? !compositor_wall_editor.open || (screen >= 0 && screen != compositor_wall_editor.screen) : how;

        if (!open)
        {
                if (compositor_wall_editor.open)
                        compositor_wall_editor_damage();
                compositor_wall_editor.open = compositor_wall_editor.pressed = 0, compositor_wall_editor.hover = -1;
                return;
        }
        if (screen < 0 || screen >= compositor_output_count())
                screen = 0;
        compositor_spaces_ensure();
        compositor_scenes_close();
        taskbar_menu_shut();
        if (compositor_wall_editor.open && compositor_wall_editor.screen != screen)
                compositor_wall_editor_damage();
        if (!compositor_wall_editor.open)
        {
                // The feed's curated wallpapers asked again (the command keeps the feed it verified and asks the network only when that is
                // hours old; with no feed that verifies there are none, and the cards are the moon's and the pictures').
                string_address argv[6] = {(string_address) "/moonwater", (string_address) "canvas", (string_address) "wallpaper", (string_address) "curated",
                                          (string_address) "fetch", null};

                compositor_wall_start_program(argv);
        }
        compositor_wall_editor.open = 1, compositor_wall_editor.screen = screen;
        compositor_wall_editor.pressed = 0, compositor_wall_editor.hover = compositor_wall_editor.press_card = -1, compositor_wall_editor.hover_close = 0;
        compositor_wall_editor_lay();
        compositor_wall_editor_damage();
}

// A program started apart from the compositor, its own session with nothing on its standard files (as compositor_alert_show starts one).
// The checks start none (compositor_wall_starts 0): what they press is the compositor's half alone.
static int compositor_wall_starts = 1;
static void compositor_wall_start_program(string_address *argv)
{
        bipolar child = compositor_wall_starts ? system_fork() : -1;

        if (child < 0)
                return;
        if (child == 0)
        {
                if (system_fork() == 0)
                {
                        bipolar quiet = system_open_at(AT_FDCWD, (string_address) "/dev/null", FILE_READ_WRITE | O_CLOEXEC);

                        system_call(syscall(setsid));
                        for (positive at = 0; quiet >= 0 && at < 3; at++)
                                system_call_3(syscall(dup3), (positive)quiet, at, 0);
                        system_call_3(syscall(execve), (positive)argv[0], (positive)argv, (positive)program_environment_list());
                        system_call_1(syscall(exit_group), 127);
                }
                system_call_1(syscall(exit_group), 0);
        }
        (void)system_wait4_retry(child, null, 0, null);
}

// A press let go where it began: a card's close button forgets its picture, a card is the Scene's word, the folder opens the picker, and
// anywhere else shuts the editor.
static void compositor_wall_editor_click(int x, int y)
{
        int card = compositor_wall_editor_card_at(x, y), screen = compositor_wall_editor.screen;
        const char *word;

        if (card < 0)
        {
                compositor_wall_edit(-1, 0);
                return;
        }
        word = compositor_wall_editor.word[card];
        if (compositor_wall_editor_on_close(card, x, y))
        {
                string_address argv[6] = {(string_address) "/moonwater", (string_address) "canvas", (string_address) "wallpaper", (string_address) "forget",
                                          (string_address)word + 8, null};

                compositor_wall_start_program(argv);
                return;
        }
        if (!word[0])
        {
                // Files to pick a picture with, over the desktop and not under the veil: the editor shuts, and Files opens it again once
                // the picture is the Scene's (files.c's pick mode runs canvas wallpaper picture FILE, then edit on).
                string_address argv[4] = {(string_address) "/files", (string_address) "-pick", (string_address) "image", null};

                compositor_wall_edit(-1, 0);
                compositor_wall_start_program(argv);
                return;
        }
        if (card == 0)
        {
                int seg = compositor_wall_editor_pill_seg(x, y);
                const char *cur = compositor_wall_word(screen, compositor_wall_current(screen));

                word = seg >= 0 ? compositor_wall_seg_word(seg) : compositor_wall_is_moon(cur) ? cur : "auto";
        }
        compositor_wall_put(screen, compositor_wall_current(screen), word);
        if (!memory_compare(word, "curated:", 8))
        {
                // The feed's picture fetched and checked if it is not here yet; the wallpaper is made again with it when it is (the ask made).
                string_address argv[7] = {(string_address) "/moonwater", (string_address) "canvas", (string_address) "wallpaper", (string_address) "curated",
                                          (string_address) "get", (string_address)word + 8, null};

                compositor_wall_start_program(argv);
        }
        compositor_wall_editor_damage();
}

// The editor takes every press while it is open but one on the band, and the release of a press it took.
static int compositor_wall_editor_takes(unsigned int code, int down)
{
        unsigned int index = code - COMPOSITOR_BTN_LEFT;

        if (!compositor_wall_editor.open)
                return 0;
        if (down)
                return compositor_band_hit(compositor.pointer_x, compositor.pointer_y) == TASKBAR_HIT_NONE;
        return index < 8 && !((compositor.button_down >> index) & 1);
}

static void compositor_wall_editor_button(unsigned int code, int down)
{
        if (code != COMPOSITOR_BTN_LEFT)
        {
                if (down)
                        compositor_wall_edit(-1, 0);
                return;
        }
        if (down)
        {
                compositor_wall_editor.pressed = 1, compositor_wall_editor.at_x = compositor.pointer_x, compositor_wall_editor.at_y = compositor.pointer_y;
                compositor_wall_editor.press_card = compositor_wall_editor_card_at(compositor.pointer_x, compositor.pointer_y);
                compositor_wall_editor.press_seg = compositor_wall_editor_pill_seg(compositor.pointer_x, compositor.pointer_y);
                return;
        }
        compositor_wall_editor.pressed = 0;
        if (compositor_wall_editor_card_at(compositor.pointer_x, compositor.pointer_y) == compositor_wall_editor.press_card &&
            compositor_wall_editor_pill_seg(compositor.pointer_x, compositor.pointer_y) == compositor_wall_editor.press_seg)
                compositor_wall_editor_click(compositor.pointer_x, compositor.pointer_y);
}

// Whether a point is the editor's while it is drawn: on its screen and not on the band, so no window under the veil takes it.
static int compositor_wall_editor_point(int x, int y)
{
        unsigned int count = 0;
        const int (*screens)[4] = compositor_screens(&count);
        int screen = compositor_wall_editor.screen;

        return compositor_wall_editor_shown() && screen >= 0 && (unsigned int)screen < count && x >= screens[screen][0] && y >= screens[screen][1] &&
               x < screens[screen][0] + screens[screen][2] && y < screens[screen][1] + screens[screen][3] &&
               compositor_band_hit(x, y) == TASKBAR_HIT_NONE;
}

/*
        The veil made again when it is not the one the frames want: the screen's rect of the wallpaper (or the colour, where there is none) at
        the step's alpha, premultiplied. Made between presents, never while a card job may read it, and let go when the editor is shut.
*/
static void compositor_wall_veil_keep(void)
{
        unsigned int count = 0, alpha;
        const int (*screens)[4] = compositor_screens(&count);
        int screen = compositor_wall_editor.screen, level, s[4];
        b64 t = compositor_wall_editor_t();
        void *p;

        level = (int)((t * COMPOSITOR_WALL_VEIL_STEPS + UI_ONE / 2) >> UI_Q);
        if (!compositor_wall_editor_shown() || screen < 0 || (unsigned int)screen >= count || !level)
        {
                if (compositor_wall_editor.veil && !compositor_wall_editor_shown() && !compositor_card_holds())
                        p = compositor_wall_editor.veil, mem_buf_free(&p, &compositor_wall_editor.veil_bytes, MEM_BUF_GPU_FORGET), compositor_wall_editor.veil = 0;
                if (compositor_wall_editor.pill && !compositor_wall_editor_shown() && !compositor_card_holds())
                        p = compositor_wall_editor.pill, mem_buf_free(&p, &compositor_wall_editor.pill_bytes, MEM_BUF_GPU_FORGET),
                        compositor_wall_editor.pill = 0, compositor_wall_editor.pill_w = compositor_wall_editor.pill_h = 0;
                compositor_wall_editor.veil_level = 0;
                return;
        }
        memory_copy(s, screens[screen], sizeof(s));
        if (s[2] <= 0 || s[3] <= 0 || compositor_card_holds())
                return;
        if (compositor_wall_editor.veil && level == compositor_wall_editor.veil_level && compositor_wall_editor.veil_of == compositor_wall &&
            compositor_wall_editor.veil_turn == compositor_wall_turn && !memory_compare(compositor_wall_editor.veil_rect, s, sizeof(s)))
                return;
        p = compositor_wall_editor.veil;
        if (!mem_buf_grow(&p, &compositor_wall_editor.veil_bytes, (unsigned long)s[2] * (unsigned long)s[3] * 4, MEM_BUF_GPU_FORGET))
                return (void)(compositor_wall_editor.veil = 0, compositor_wall_editor.veil_bytes = 0, compositor_wall_editor.veil_level = 0);
        compositor_wall_editor.veil = (unsigned int *)p;
        alpha = COMPOSITOR_WALL_VEIL * (unsigned int)level / COMPOSITOR_WALL_VEIL_STEPS;
        for (int y = 0; y < s[3]; y++)
        {
                const unsigned int *from = compositor_wall && s[0] >= 0 && s[1] >= 0 && s[0] + s[2] <= compositor_wall_w && s[1] + s[3] <= compositor_wall_h
                                               ? compositor_wall + (long)(s[1] + y) * compositor_wall_pitch + s[0] : 0;
                unsigned int *into = compositor_wall_editor.veil + (long)y * s[2];

                for (int x = 0; x < s[2]; x++)
                {
                        unsigned int px = from ? from[x] : compositor_ground, rb = (px & 0x00ff00ffu) * alpha + 0x00800080u, g = (px >> 8 & 0xffu) * alpha + 0x80u;

                        rb = ((rb + (rb >> 8 & 0x00ff00ffu)) >> 8) & 0x00ff00ffu, g = ((g + (g >> 8)) >> 8) & 0xffu;
                        into[x] = alpha << 24 | rb | g << 8;
                }
        }
        compositor_wall_editor.veil_level = level, compositor_wall_editor.veil_of = compositor_wall, compositor_wall_editor.veil_turn = compositor_wall_turn;
        memory_copy(compositor_wall_editor.veil_rect, s, sizeof(s));
        compositor_wall_editor_damage();
}

// The moon with the theme, as its card shows it: the dark moon, and the white one over the half below a diagonal.
static const unsigned int *compositor_wall_editor_auto(int w, int h)
{
        const unsigned int *dark = compositor_wall_mini("dark", w, h), *white = compositor_wall_mini("white", w, h);
        struct compositor_wall_mini *m = 0;
        void *p;

        if (!dark || !white)
                return dark ? dark : white;
        for (unsigned int at = 0; at < compositor_wall_minis.count && !m; at++)
                if (compositor_wall_minis.list[at].w == w && compositor_wall_minis.list[at].h == h && string_equals((string_address)compositor_wall_minis.list[at].word, "~auto"))
                        m = &compositor_wall_minis.list[at];
        if (m && m->pixels && m->accent == taskbar_wall_accent())
                return m->used = clock_monotonic_nanoseconds(), m->pixels;
        if (!m)
        {
                p = compositor_wall_minis.list;
                if (!mem_buf_grow(&p, &compositor_wall_minis.list_bytes, (unsigned long)(compositor_wall_minis.count + 1) * sizeof(*m), MEM_BUF_KEEP | MEM_BUF_GROW_2X))
                        return dark;
                compositor_wall_minis.list = (struct compositor_wall_mini *)p;
                m = &compositor_wall_minis.list[compositor_wall_minis.count++];
                memory_zero(m, sizeof(*m));
                string_copy((string_address)m->word, "~auto"), m->w = w, m->h = h;
        }
        m->accent = taskbar_wall_accent();
        p = m->pixels;
        if (!mem_buf_grow(&p, &m->bytes, (unsigned long)w * (unsigned long)h * 4, MEM_BUF_GPU_FORGET))
                return m->pixels = 0, m->bytes = 0, dark;
        m->pixels = (unsigned int *)p, m->used = clock_monotonic_nanoseconds();
        for (int y = 0; y < h; y++)
        {
                int cut = w - (int)((long long)y * w / h);

                memory_copy(m->pixels + (long)y * w, dark + (long)y * w, (positive)cut * 4);
                memory_copy(m->pixels + (long)y * w + cut, white + (long)y * w + cut, (positive)(w - cut) * 4);
        }
        return m->pixels;
}

// Rounded segmented switch ("Dark | Light | Auto") rendered into a cached ARGB buffer for the Moonwater dynamic wallpaper card.
static const unsigned int *compositor_wall_editor_pill_pixels(int w, int h, int active, int hover, int scale)
{
        static const char *const labels[3] = {"Dark", "Light", "Auto"};
        unsigned int track = compositor_ink_role(UI_SURFACE_HIGHEST) | 0xff000000u;
        unsigned int accent = compositor_ink_role(UI_PRIMARY) | 0xff000000u;
        unsigned int on_accent = compositor_ink_role(UI_ON_PRIMARY) | 0xff000000u;
        unsigned int on_track = compositor_ink_role(UI_ON_SURFACE) | 0xff000000u;
        unsigned int hover_fill = compositor_ink_role(UI_SURFACE_HIGH) | 0xff000000u;
        unsigned int key = (((track ^ accent) * 16777619u ^ on_track) * 16777619u ^ (unsigned int)scale);
        int pad = 2 * scale / 100, inner_h, rad;
        void *p;

        if (w <= 0 || h <= 0)
                return 0;
        if (compositor_wall_editor.pill && compositor_wall_editor.pill_w == w && compositor_wall_editor.pill_h == h &&
            compositor_wall_editor.pill_active == active && compositor_wall_editor.pill_hover == hover &&
            compositor_wall_editor.pill_key == key)
                return compositor_wall_editor.pill;
        if (compositor_card_holds())
                return compositor_wall_editor.pill && compositor_wall_editor.pill_w == w && compositor_wall_editor.pill_h == h ? compositor_wall_editor.pill : 0;
        p = compositor_wall_editor.pill;
        if (!mem_buf_grow(&p, &compositor_wall_editor.pill_bytes, (unsigned long)w * (unsigned long)h * 4, MEM_BUF_GPU_FORGET))
                return compositor_wall_editor.pill = 0, compositor_wall_editor.pill_bytes = 0, (const unsigned int *)0;
        compositor_wall_editor.pill = (unsigned int *)p;
        compositor_wall_editor.pill_w = w, compositor_wall_editor.pill_h = h;
        compositor_wall_editor.pill_active = active, compositor_wall_editor.pill_hover = hover, compositor_wall_editor.pill_key = key;

        memory_fill_u32_rect(compositor_wall_editor.pill, (positive)w, (positive)w, (positive)h, track);
        if (pad < 1)
                pad = 1;
        inner_h = h - 2 * pad;
        rad = inner_h / 2;
        for (int seg = 0; seg < 3; seg++)
        {
                int sx0 = seg * w / 3 + pad, sx1 = (seg + 1) * w / 3 - pad, sy0 = pad, sy1 = h - pad;
                unsigned int bg = seg == active ? accent : seg == hover ? hover_fill : track;
                unsigned int fg = seg == active ? on_accent : on_track;

                if (bg != track && sx1 > sx0 && sy1 > sy0)
                {
                        int r = rad;
                        if (r > (sx1 - sx0) / 2)
                                r = (sx1 - sx0) / 2;
                        for (int y = sy0; y < sy1; y++)
                        {
                                int dy = y < sy0 + r ? (sy0 + r - y) : y >= sy1 - r ? (y - (sy1 - 1 - r)) : 0;
                                int cut = 0;
                                if (dy > 0)
                                {
                                        int rem = r * r - dy * dy;
                                        while (cut < r && (r - cut) * (r - cut) > rem)
                                                cut++;
                                }
                                if (sx1 - cut > sx0 + cut)
                                        memory_fill_u32_rect(compositor_wall_editor.pill + (long)y * w + sx0 + cut, (positive)w,
                                                             (positive)(sx1 - sx0 - 2 * cut), 1, bg);
                        }
                }
                if (compositor.engine && sx1 > sx0 && inner_h >= 8)
                {
                        const font_face address_to faces[1] = {&compositor.face};
                        font_target target = {compositor_wall_editor.pill, (p32)w, (b32)w, (b32)h};
                        p32 em = (p32)((inner_h * 56 / 100) << 6);
                        font_metrics fm;
                        positive len = string_length((string_address)labels[seg]);
                        int tw, tx, ty;

                        if (em < (8u << 6))
                                em = 8u << 6;
                        fm = font_metrics_at(&compositor.face, em);
                        tw = (int)(font_draw(compositor.engine, faces, 1, (const p8 address_to)labels[seg], len, em, 0, null, 0, 0, 0, 0) >> 16);
                        tx = sx0 + ((sx1 - sx0) - tw) / 2;
                        if (tx < sx0)
                                tx = sx0;
                        ty = sy0 + (inner_h - (int)fm.cell_height) / 2 + (int)fm.baseline;
                        font_draw(compositor.engine, faces, 1, (const p8 address_to)labels[seg], len, em, 0, &target,
                                  (b32)(tx << 16), (b32)ty, fg & 0xffffffu, bg & 0xffffffu);
                }
        }
        memory_or_u32(compositor_wall_editor.pill, compositor_wall_editor.pill, (positive)w * (positive)h, 0xff000000u);
        return compositor_wall_editor.pill;
}

/*
        The editor's frames at frames[at] (room of them at the most), after every window's: the veil over its screen, then the cards, each a
        word's miniature, the rounded Dark | Light | Auto switch on card 0, and the folder's card, a plate of the kit's surface with a folder drawn on it. Answers how many.
*/
static unsigned int compositor_wall_editor_frames(struct compositor_frame *frames, unsigned int *rows, unsigned int at, unsigned int room)
{
        unsigned int count = 0, start = at, filler = compositor.stacked ? compositor.stack[0] : 0;
        const int (*screens)[4] = compositor_screens(&count);
        int screen = compositor_wall_editor.screen, scale, radius, line, lit_w, s[4], clip[4];
        unsigned int accent = compositor_ink_role(UI_ACCENT_BRIGHT) & 0xffffffu;
        const char *current;
        b64 t = compositor_wall_editor_t();

        if (!compositor_wall_editor_shown() || screen < 0 || (unsigned int)screen >= count)
                return 0;
        if (compositor_wall_editor.gen != compositor_walls.gen || !compositor_wall_editor.cards)
                compositor_wall_editor_lay();
        memory_copy(s, screens[screen], sizeof(s));
        clip[0] = s[0], clip[1] = s[1], clip[2] = s[0] + s[2], clip[3] = s[1] + s[3];
        scale = compositor_output_scale(screen) > 0 ? compositor_output_scale(screen) : compositor.scale;
        radius = UI_RADIUS * scale / 100, line = compositor_line_px(scale), lit_w = 2 * compositor_lit_px(scale);
        current = compositor_wall_word(screen, compositor_wall_current(screen));
        if (compositor_wall_editor.veil && compositor_wall_editor.veil_level && at < room &&
            !memory_compare(compositor_wall_editor.veil_rect, s, sizeof(s)))
        {
                struct compositor_frame *veil = &frames[at];

                memory_zero(veil, sizeof(*veil));
                veil->x = s[0], veil->y = s[1], veil->width = s[2], veil->height = s[3], veil->scale = scale, veil->layer = 1, veil->blend = 1;
                veil->pixels = compositor_wall_editor.veil, veil->pitch = (unsigned long)s[2];
                rows[at++] = filler;
        }
        for (unsigned int card = 0; card < compositor_wall_editor.cards; card++)
        {
                const char *word = compositor_wall_editor.word[card];
                int c[4], lit = compositor_wall_editor.open && compositor_wall_editor.hover == (int)card;
                const unsigned int *mini;
                int selected = card == 0 ? compositor_wall_is_moon(current) : word[0] && string_equals((string_address)word, (string_address)current);

                compositor_wall_editor_card_shown(card, s, t, c);
                if (!compositor_scenes_plain(frames, rows, at, room, filler, c, clip, scale))
                        continue;
                if (card == 0)
                        mini = string_equals((string_address)current, "dark") ? compositor_wall_mini("dark", c[2], c[3])
                               : string_equals((string_address)current, "white") ? compositor_wall_mini("white", c[2], c[3])
                                                                                 : compositor_wall_editor_auto(c[2], c[3]);
                else
                        mini = !word[0] ? 0 : compositor_wall_mini(word, c[2], c[3]);
                if (mini)
                        frames[at].pixels = mini + (frames[at].y - c[1]) * c[2] + (frames[at].x - c[0]), frames[at].pitch = (unsigned long)c[2];
                else
                        frames[at].fill = compositor_ink_role(UI_SURFACE_CONTAINER) | 0xff000000u;
                frames[at].radius = radius, frames[at].line = line;
                frames[at].edge = selected ? accent : compositor_line_unlit();
                if (lit)
                        frames[at].lit = COMPOSITOR_LIT_ALL, frames[at].lit_w = lit_w, frames[at].lit_ink = COMPOSITOR_INK_LINE_HOVER;
                if (lit && compositor_wall_editor_close((int)card, &(int){0}, &(int){0}, &(int){0}))
                        frames[at].close = 1;
                at++;
                if (card == 0)
                {
                        int pill[4];

                        if (compositor_wall_editor_pill(c, scale, pill) && compositor_scenes_plain(frames, rows, at, room, filler, pill, clip, scale))
                        {
                                const unsigned int *px = compositor_wall_editor_pill_pixels(
                                    pill[2], pill[3], compositor_wall_moon_seg(current),
                                    compositor_wall_editor.open ? compositor_wall_editor.hover_seg : -1, scale);

                                if (px)
                                        frames[at].pixels = px + (frames[at].y - pill[1]) * pill[2] + (frames[at].x - pill[0]),
                                        frames[at].pitch = (unsigned long)pill[2];
                                else
                                        frames[at].fill = compositor_ink_role(UI_SURFACE_HIGHEST) | 0xff000000u;
                                frames[at].radius = pill[3] / 2, frames[at].line = line, frames[at].edge = compositor_line_unlit();
                                at++;
                        }
                }
                if (!word[0])
                {
                        // A folder: its tab, and its body under it, in the quiet ink.
                        int side = (c[2] < c[3] ? c[2] : c[3]) * 2 / 5, bar = side / 6 > 1 ? side / 6 : 1;
                        int body[4] = {c[0] + c[2] / 2 - side / 2, c[1] + c[3] / 2 - side * 3 / 8 + bar, side, side * 3 / 4 - bar};
                        int tab[4] = {body[0], body[1] - bar, side * 2 / 5, bar + radius / 2};
                        unsigned int ink = compositor_ink_role(UI_ON_SURFACE_VARIANT) | 0xff000000u;

                        if (compositor_scenes_plain(frames, rows, at, room, filler, tab, clip, scale))
                                frames[at].fill = ink, frames[at].radius = bar / 2, at++;
                        if (compositor_scenes_plain(frames, rows, at, room, filler, body, clip, scale))
                                frames[at].fill = ink, frames[at].radius = bar, at++;
                }
        }
        return at - start;
}

// The editor's spring a step to its end over the time since its last, as the overview's (compositor_scenes_step); a move draws it again.
static void compositor_wall_editor_step(positive now)
{
        b64 goal = compositor_wall_editor.open ? COMPOSITOR_SCENES_FULL : 0;
        positive gone;
        b64 left;

        if (!compositor_glides())
        {
                if (compositor_wall_editor.shown != goal)
                        compositor_wall_editor.shown = goal, compositor_wall_editor_damage();
                compositor_wall_editor.speed = 0, compositor_wall_editor.stepped = 0;
                return;
        }
        if (compositor_wall_editor.shown == goal && !compositor_wall_editor.speed)
        {
                compositor_wall_editor.stepped = 0;
                return;
        }
        gone = compositor_wall_editor.stepped && now > compositor_wall_editor.stepped ? now - compositor_wall_editor.stepped : 0;
        left = ui_paced_elapsed((b64)(gone > 500 * UI_MS ? 500 * UI_MS : gone));
        while (left > 0)
        {
                b64 ns = left > UI_MS ? UI_MS : left;

                left -= ns;
                if (!ui_body_spring_step(&compositor_wall_editor.shown, &compositor_wall_editor.speed, goal, ns * UI_ONE / 1000000000))
                        break;
        }
        if (compositor_wall_editor.shown < 0)
                compositor_wall_editor.shown = 0, compositor_wall_editor.speed = 0;
        if (compositor_wall_editor.shown > COMPOSITOR_SCENES_FULL)
                compositor_wall_editor.shown = COMPOSITOR_SCENES_FULL, compositor_wall_editor.speed = 0;
        compositor_wall_editor.stepped = now;
        compositor_wall_editor_damage();
}

// What the hand is over while the editor is open: the card (its ring lit) and a picture's close button. A change draws it again.
static void compositor_wall_editor_hover(void)
{
        int x = compositor.pointer_x, y = compositor.pointer_y, card = compositor_wall_editor_card_at(x, y);
        int close = card >= 0 && compositor_wall_editor_on_close(card, x, y);
        int seg = compositor_wall_editor_pill_seg(x, y);

        if (card != compositor_wall_editor.hover || close != compositor_wall_editor.hover_close || seg != compositor_wall_editor.hover_seg)
                compositor_wall_editor_damage();
        compositor_wall_editor.hover = card, compositor_wall_editor.hover_close = close, compositor_wall_editor.hover_seg = seg;
}

// When the loop is owed its next pass for the spring: a frame after the last present while it is on its way, 0 at rest.
static positive compositor_wall_editor_due(positive now)
{
        positive period = compositor.period ? compositor.period : 16666667ull;
        b64 goal = compositor_wall_editor.open ? COMPOSITOR_SCENES_FULL : 0;

        if (!compositor_glides() || (compositor_wall_editor.shown == goal && !compositor_wall_editor.speed))
                return 0;
        return compositor.presented + period > now ? compositor.presented + period : now;
}

// Every pass of the loop, before a present: the spring, the hand, the cards laid out again for a change of the list, and the veil.
static void compositor_wall_editor_tick(positive now)
{
        if (!compositor_wall_editor.open && !compositor_wall_editor.shown && !compositor_wall_editor.veil)
                return;
        compositor_wall_editor_step(now);
        if (compositor_wall_editor.open)
        {
                if (compositor_wall_editor.gen != compositor_walls.gen)
                        compositor_wall_editor_lay(), compositor_wall_editor_damage();
                compositor_wall_editor_hover();
        }
        compositor_wall_veil_keep();
}
