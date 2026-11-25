/*
        Each Scene's wallpaper.

        A Scene has one wallpaper, said by a word (registry.c's setting_wallpaper_word_ok): auto, the moon over its sky in the theme's look;
        dark or white, the moon in that look whatever the theme; picture:NAME, a picture `canvas wallpaper picture FILE` remembered;
        curated:HEX, one of the feed's, kept by its digest; or colour:RRGGBB, a plain colour (the editor's Solid colour card, its colour
        wheel the kit's). A Scene is known by its screen's name and its number among the screen's desktop
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

// A curated wallpaper's credit, as the list the command verified says it: its author, its licence and its source page ("" for none).
struct compositor_wall_credit
{
        char author[SETTING_WALLPAPER_CREDIT], licence[SETTING_WALLPAPER_CREDIT], source[SETTING_WALLPAPER_SOURCE];
        positive bytes;      // the picture's, the feed's count: what its card's progress is shown against
        unsigned char ready; // its miniature is here (else its card is a ghost while the feed's thumbnails are fetched, and none after)
};

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
        struct compositor_wall_credit *credit;   // and each one's credit, in the same order
        unsigned long credit_bytes;
        unsigned int curateds;
        unsigned int gen;   // one more at every change of a word or the list, which the wallpaper is made again for
        unsigned int listed; // and of the list alone, which a picture's miniature is read again for
        b32 unwritten;      // a word changed live (a hand on the colour wheel) and not yet in the file (compositor_walls_flush)
        positive changed;   // when it last changed
        unsigned char sync; // the feed's thumbnails: being fetched (COMPOSITOR_WALL_SYNCING), or the list could not be had (_UNSYNCED)
        positive sync_at;   // when the sync was begun or last told of
        unsigned int laid;  // one more at every change of what the editor's cards are that is no change of a word (a thumbnail come, a sync's end)
} compositor_walls;

#define COMPOSITOR_WALL_SYNCED 0
#define COMPOSITOR_WALL_SYNCING 1
#define COMPOSITOR_WALL_UNSYNCED 2

/*
        A curated picture on its way (canvas wallpaper curated get, started by a press on its card): its digest, how it goes (UI_FETCH_GOING,
        or why it failed), and how much of it is here (its part's size, curated/HEX.part, the command's) of the feed's count. Kept as long as
        it goes or its failure is to be shown; gone when it is in place.
*/
struct compositor_wall_fetch
{
        char hex[65];
        int what;
        positive got, total;
};

static struct
{
        struct compositor_wall_fetch *list;
        unsigned long bytes;
        unsigned int count;
} compositor_wall_fetches;

static struct compositor_wall_fetch *compositor_wall_fetch_of(const char *hex)
{
        for (unsigned int at = 0; at < compositor_wall_fetches.count; at++)
                if (string_equals((string_address)compositor_wall_fetches.list[at].hex, (string_address)hex))
                        return &compositor_wall_fetches.list[at];
        return 0;
}

// A picture's fetch said: what it is now (its count where it is known); dropped with what -1.
static void compositor_wall_fetch_put(const char *hex, int what, positive total)
{
        struct compositor_wall_fetch *f = compositor_wall_fetch_of(hex);
        void *p = compositor_wall_fetches.list;

        if (what < 0)
        {
                if (f)
                        memory_copy(f, f + 1, (positive)(compositor_wall_fetches.list + --compositor_wall_fetches.count - f) * sizeof(*f));
                return;
        }
        if (!f)
        {
                if (string_length((string_address)hex) != 64 ||
                    !mem_buf_grow(&p, &compositor_wall_fetches.bytes, (unsigned long)(compositor_wall_fetches.count + 1) * sizeof(*f), MEM_BUF_KEEP | MEM_BUF_GROW_2X))
                        return;
                compositor_wall_fetches.list = (struct compositor_wall_fetch *)p;
                f = &compositor_wall_fetches.list[compositor_wall_fetches.count++];
                memory_zero(f, sizeof(*f));
                memory_copy(f->hex, hex, 65);
        }
        f->what = what, f->got = 0, f->total = total ? total : f->total;
}

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

// The words in the file now, when a live change has not been written yet.
static void compositor_walls_flush(void)
{
        if (compositor_walls.unwritten)
                compositor_walls.unwritten = 0, compositor_walls_write();
}

// Scene number scene of the screen named screen has word (auto is no line of its own), kept at once, or (live: a colour the wheel is
// moving through) shown at once and kept when the hand lets go or a moment after the last change (compositor_walls_flush).
static void compositor_wall_put_named(const char *screen, unsigned int scene, const char *word, int live)
{
        int at = compositor_wall_find(screen, scene);
        struct compositor_wall_line *l;
        char saved[SETTING_WALLPAPER_WORD];

        if (!scene || !setting_wallpaper_word_ok(word, string_length((string_address)word)))
                return;
        if (at >= 0 && string_equals((string_address)compositor_walls.line[at].word, (string_address)word))
                return;
        string_copy_bounded((p8 *)saved, (string_address)word, SETTING_WALLPAPER_WORD);
        if (string_equals((string_address)saved, "auto"))
        {
                if (at < 0)
                        return;
                compositor_wall_line_drop((unsigned int)at);
        }
        else if (at >= 0)
        {
                memory_zero(compositor_walls.line[at].word, SETTING_WALLPAPER_WORD);
                string_copy_bounded((p8 *)compositor_walls.line[at].word, (string_address)saved, SETTING_WALLPAPER_WORD);
        }
        else if ((l = compositor_wall_line_add()))
        {
                memory_zero(l, sizeof(*l));
                string_copy_bounded((p8 *)l->screen, (string_address)screen, WINDOWS_OUTPUT_NAME);
                l->scene = scene;
                string_copy_bounded((p8 *)l->word, (string_address)saved, SETTING_WALLPAPER_WORD);
        }
        if (live)
                compositor_walls.unwritten = 1, compositor_walls.changed = clock_monotonic_nanoseconds();
        else
                compositor_walls.unwritten = 0, compositor_walls_write();
        compositor_wall_changed();
}

static void compositor_wall_put(int screen, unsigned int scene, const char *word)
{
        compositor_wall_put_named(compositor_wall_screen_name(screen), scene, word, 0);
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
                return hash_fnv1a(hash_fnv1a(hash_fnv1a(sum ^ compositor_walls.names, compositor_walls.name, (positive)compositor_walls.names * SETTING_WALLPAPER_NAME) ^
                                                 compositor_walls.curateds,
                                             compositor_walls.curated, (positive)compositor_walls.curateds * SETTING_WALLPAPER_WORD),
                                  compositor_walls.credit, (positive)compositor_walls.curateds * sizeof(compositor_walls.credit[0]));
        return hash_fnv1a(sum ^ compositor_walls.lines, compositor_walls.line, (positive)compositor_walls.lines * sizeof(compositor_walls.line[0]));
}

/*
        A curated line's credit (after its digest and the blank: the id, then a tab and the author, a tab and the licence, a tab and the
        source), held to what the command writes as it is read: a credit's control bytes are blanks and it is cut to its room at a
        character's start, and a source that is no page by setting_wallpaper_source_ok's rules is none.
*/
static void compositor_wall_credit_read(struct compositor_wall_credit *credit, const char *text, positive length)
{
        char bytes[24], *field[4] = {credit->author, credit->licence, credit->source, bytes};
        positive room[4] = {SETTING_WALLPAPER_CREDIT, SETTING_WALLPAPER_CREDIT, SETTING_WALLPAPER_SOURCE, sizeof(bytes)}, at = 0, used = 0;

        memory_zero(credit, sizeof(*credit));
        bytes[0] = 0;
        while (at < length && text[at] != '\t')
                at++;
        for (int f = 0; f < 4 && at < length; f++)
        {
                positive from = ++at, cut;

                while (at < length && text[at] != '\t')
                        at++;
                cut = at - from < room[f] ? at - from : room[f] - 1;
                while (cut && cut < at - from && ((unsigned char)text[from + cut] & 0xc0) == 0x80)
                        cut--;
                for (positive k = 0; k < cut; k++)
                        field[f][k] = (unsigned char)text[from + k] < 0x20 || text[from + k] == 0x7f ? ' ' : text[from + k];
                field[f][cut] = 0;
        }
        if (!setting_wallpaper_source_ok(credit->source, string_length((string_address)credit->source)))
                credit->source[0] = 0;
        credit->bytes = string_digits_max((string_address)bytes, 12, &used);
        credit->bytes = used && !bytes[used] ? credit->bytes : 0;
}

// Whether a curated picture's miniature is here (curated/HEX.mini): its card is its picture, not a ghost.
static unsigned char compositor_wall_mini_here(const char *word)
{
        char path[sizeof(SETTING_WALLPAPER_DIR "/curated/") + SETTING_WALLPAPER_WORD + 8];

        return setting_wallpaper_file(word, ".mini", path, sizeof(path)) && compositor_wall_look(path, null) == 0100000u;
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
                        void *c = compositor_walls.credit;

                        compositor_walls.curated = (char (*)[SETTING_WALLPAPER_WORD])p;
                        if (!mem_buf_grow(&c, &compositor_walls.credit_bytes, (unsigned long)(compositor_walls.curateds + 1) * sizeof(compositor_walls.credit[0]),
                                          MEM_BUF_KEEP | MEM_BUF_GROW_2X))
                                break;
                        compositor_walls.credit = (struct compositor_wall_credit *)c;
                        compositor_wall_credit_read(&compositor_walls.credit[compositor_walls.curateds], text + at + 65, stop - at - 65);
                        compositor_walls.credit[compositor_walls.curateds].ready = compositor_wall_mini_here(word);
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

        // A live change not written yet is written before the file is read, so the read does not take it back.
        if (compositor_walls.unwritten)
        {
                if (mapped)
                        compositor_call(syscall(munmap), mapped, size, 0, 0, 0, 0);
                compositor_walls_flush();
                mapped = compositor_map_file((string_address)compositor_walls_path, &size);
        }
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
        A curated picture chosen before its file is here: the Scene keeps what it shows while the command fetches it (canvas wallpaper
        curated get), and is given it in one step when the command says the file is in place, verified and decoded (the ask "got HEX").
        The last choice wins: a later one, of any card, takes the place of this one, and a picture that comes after it was let go of is
        kept in the cache and given to no Scene. One that fails (the ask "lost HEX WHY") leaves the Scene as it was.
*/
static struct
{
        char screen[WINDOWS_OUTPUT_NAME];
        unsigned int scene;
        char word[SETTING_WALLPAPER_WORD]; // "" for none
} compositor_wall_pending;

static int compositor_wall_start_program(string_address *argv);

// A word chosen for Scene scene of the screen named screen: at once when its picture is here (or it has none), else when it has come.
static void compositor_wall_choose_named(const char *screen, unsigned int scene, const char *word)
{
        char path[sizeof(SETTING_WALLPAPER_DIR "/curated/") + SETTING_WALLPAPER_WORD + 8];

        compositor_wall_pending.word[0] = 0;
        if (memory_compare(word, "curated:", 8) || compositor_wall_picture_of(word, path, sizeof(path)))
                return compositor_wall_put_named(screen, scene, word, 0);
        if (!setting_wallpaper_word_ok(word, string_length((string_address)word)) || string_length((string_address)screen) >= WINDOWS_OUTPUT_NAME)
                return;
        string_copy_bounded((p8 *)compositor_wall_pending.screen, (string_address)screen, WINDOWS_OUTPUT_NAME);
        compositor_wall_pending.scene = scene;
        string_copy_bounded((p8 *)compositor_wall_pending.word, (string_address)word, SETTING_WALLPAPER_WORD);
        // One fetch of a picture at a time: a press on a card whose picture is already coming chooses it and starts none.
        {
                struct compositor_wall_fetch *f = compositor_wall_fetch_of(word + 8);
                positive total = 0;

                if (f && f->what == UI_FETCH_GOING)
                        return;
                for (unsigned int at = 0; at < compositor_walls.curateds; at++)
                        if (string_equals((string_address)compositor_walls.curated[at], (string_address)word))
                                total = compositor_walls.credit[at].bytes;
                compositor_wall_fetch_put(word + 8, UI_FETCH_GOING, total);
        }
        {
                string_address argv[7] = {(string_address) "/moonwater", (string_address) "canvas", (string_address) "wallpaper", (string_address) "curated",
                                          (string_address) "get", (string_address)compositor_wall_pending.word + 8, null};

                compositor_wall_start_program(argv);
        }
}

// The command's word on a curated picture: got (its file is in place) gives it to the Scene that last chose it, lost lets that choice go.
static void compositor_wall_fetched(const char *hex, int got, const char *why)
{
        char path[sizeof(SETTING_WALLPAPER_DIR "/curated/") + SETTING_WALLPAPER_WORD + 8];

        // Its card says it: gone when it is in place, its failure and a retry when it is lost.
        compositor_wall_fetch_put(hex, got ? -1 : string_equals((string_address)why, "busy") ? UI_FETCH_BUSY : string_equals((string_address)why, "digest") ? UI_FETCH_DIGEST
                                                                                                                                                            : UI_FETCH_LOST,
                                  0);
        compositor_walls.laid++;
        if (got)
                compositor_walls.listed++, compositor_wall_changed();
        if (!compositor_wall_pending.word[0] || !string_equals((string_address)compositor_wall_pending.word + 8, (string_address)hex))
                return;
        if (got && compositor_wall_picture_of(compositor_wall_pending.word, path, sizeof(path)))
                compositor_wall_put_named(compositor_wall_pending.screen, compositor_wall_pending.scene, compositor_wall_pending.word, 0);
        if (got ? path[0] != 0 : 1)
                compositor_wall_pending.word[0] = 0;
}

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

static void compositor_wall_mini_stale(const char *word); // its miniature read again at its next draw (below)
static void compositor_wall_editor_damage(void);

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
        // A curated picture fetched (canvas wallpaper curated get): the wallpaper and the miniatures made again with it, and the Scene that
        // chose it given it; or lost (its fetch, its digest or its decode failed), and the Scene kept as it was.
        if (count == 1 && string_equals((string_address)word[0], "made"))
                compositor_walls.listed++, compositor_wall_changed();
        else if ((count == 2 && string_equals((string_address)word[0], "got")) || (count == 3 && string_equals((string_address)word[0], "lost")))
        {
                char curated[SETTING_WALLPAPER_WORD];

                string_join((p8 *)curated, sizeof(curated), (string_address) "curated:", (string_address)word[1]);
                if (setting_wallpaper_word_ok(curated, string_length((string_address)curated)) && curated[7] == ':' && curated[1] == 'u')
                        compositor_wall_fetched(word[1], word[0][0] == 'g', count == 3 ? word[2] : "");
        }
        // The feed's thumbnails as they come (canvas wallpaper curated fetch): one in place, its ghost its card; the sync's end, its ghosts
        // gone (and the card to try again where some did not come); the list not had, its ghosts that card.
        else if (count == 2 && string_equals((string_address)word[0], "mini"))
        {
                char curated[SETTING_WALLPAPER_WORD];

                string_join((p8 *)curated, sizeof(curated), (string_address) "curated:", (string_address)word[1]);
                for (unsigned int at = 0; at < compositor_walls.curateds && setting_wallpaper_word_ok(curated, string_length((string_address)curated)); at++)
                        if (string_equals((string_address)compositor_walls.curated[at], (string_address)curated))
                                compositor_walls.credit[at].ready = compositor_wall_mini_here(curated);
                compositor_wall_mini_stale(curated);
                compositor_walls.laid++, compositor_walls.sync_at = clock_monotonic_nanoseconds(), compositor_wall_editor_damage();
        }
        else if ((count == 1 && string_equals((string_address)word[0], "synced")) || (count == 2 && string_equals((string_address)word[0], "unsynced")))
                compositor_walls.sync = count == 1 ? COMPOSITOR_WALL_SYNCED : COMPOSITOR_WALL_UNSYNCED, compositor_walls.laid++, compositor_wall_editor_damage();
        else if (count == 2 && string_equals((string_address)word[0], "edit"))
                compositor_wall_edit(compositor_space_current_screen(),
                                     string_equals((string_address)word[1], "on") ? 1 : string_equals((string_address)word[1], "off") ? 0 : -1);
        else if (count == 2 && string_equals((string_address)word[0], "current"))
        {
                int screen = compositor_space_current_screen();

                compositor_wall_pending.word[0] = 0;
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
                compositor_wall_pending.word[0] = 0;
                if (string_equals((string_address)word[1], "-"))
                        compositor_wall_put(compositor_space_current_screen(), scene, word[3]);
                else if (string_length((string_address)word[1]) < WINDOWS_OUTPUT_NAME)
                        compositor_wall_put_named(word[1], scene, word[3], 0);
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

// A picture's miniature file into w by h (its rows pitch pixels apart): cut at its long sides to w by h's shape, as the wallpaper is (compositor_wall_picture).
static int compositor_wall_mini_picture(const char *word, unsigned int *into, unsigned long pitch, int w, int h)
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
        memory_resample_u32_rect(into, (positive)pitch, (positive)w, (positive)h, mini + oy * (positive)width + ox, (positive)width, vis_w, vis_h,
                                 (positive)w, (positive)h, 0, 0);
        memory_free(mini, bytes);
        return 1;
}

static const unsigned int *compositor_wall_mini(const char *word, int w, int h)
{
        unsigned long pitch = compositor_whole_lines(w); // the miniature's rows: w pixels on a pitch of whole 64-byte lines, as every reader of one takes them
        int dark = !(compositor.table && compositor.table->themed && compositor.table->theme == UI_THEME_LIGHT), moon, own;
        long colour;
        unsigned int accent = taskbar_wall_accent();
        positive now = clock_monotonic_nanoseconds();
        struct compositor_wall_mini *m = 0;
        void *p;

        if (!word || !word[0] || w <= 0 || h <= 0)
                return 0;
        moon = string_equals((string_address)word, "auto") || string_equals((string_address)word, "dark") || string_equals((string_address)word, "white");
        own = setting_wallpaper_dark(word, dark);
        colour = setting_wallpaper_colour(word);
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
        if (!mem_buf_grow(&p, &m->bytes, pitch * (unsigned long)h * 4, MEM_BUF_GPU_FORGET))
                return m->pixels = 0, m->bytes = 0, (const unsigned int *)0;
        m->pixels = (unsigned int *)p, m->used = now, m->dark = own, m->accent = accent, m->gen = compositor_walls.listed;
        if (moon)
        {
                int rect[4] = {0};

                memory_fill_u32_rect(m->pixels, (positive)pitch, (positive)w, (positive)h, 0xff000000u);
                taskbar_wall_draw(m->pixels, (int)pitch, w, h, own, 1, 45, 1, accent, compositor_wall_turn, 0, rect);
                memory_or_u32(m->pixels, m->pixels, (positive)pitch * (positive)h, 0xff000000u);
                return m->pixels;
        }
        if (colour >= 0)
        {
                memory_fill_u32_rect(m->pixels, (positive)pitch, (positive)w, (positive)h, (unsigned int)colour | 0xff000000u);
                return m->pixels;
        }
        if (compositor_wall_mini_picture(word, m->pixels, pitch, w, h))
                return m->pixels;
        // No miniature to be had (gone, or not made yet): the ground's colour, made again at the next change of the list.
        memory_fill_u32_rect(m->pixels, (positive)pitch, (positive)w, (positive)h, compositor_ground | 0xff000000u);
        return m->pixels;
}

// A word's miniatures read again at their next draw (its thumbnail came after a ground was drawn for it).
static void compositor_wall_mini_stale(const char *word)
{
        for (unsigned int at = 0; at < compositor_wall_minis.count; at++)
                if (string_equals((string_address)compositor_wall_minis.list[at].word, (string_address)word))
                        compositor_wall_minis.list[at].gen = compositor_walls.listed - 1, compositor_wall_minis.list[at].dark = -1;
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
        spring: first Solid colour, the card in the colour with the kit's colour wheel on it (ui.c's ui_colour: its disc, its value bar and
        its rrggbb in the kit's field), whose every move is the Scene's colour at once, kept in the file when the hand lets go; then the
        Moonwater moon with the theme (its card the dark moon and the white one, split along a diagonal), dark and white on its switch; then
        the pictures remembered, the most recent first, each with the close button over its corner under the hand, which forgets it; then the
        feed's; and last a card with a folder, which opens Files to pick a picture (files -pick image). The card of the word the Scene has is
        ringed in the accent, the one under the hand lit. The keys while the editor is open are the wheel's (the arrows move its marker,
        Page Up and Down its value, a digit or Enter its field; Escape leaves the field, then shuts the editor). The wheel starts at the
        Scene's colour, or for a Scene with none at the background colour kept (canvas background). A press on a card gives the Scene its word at once and the editor stays: a Scene
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
// A curated card's source mark as it was drawn (ui.c's ui_source_card_draw): its picture, what it was drawn for, and the mark's box in it.
struct compositor_wall_band
{
        unsigned int *pixels;
        unsigned long bytes;
        int w, h, scale, hover, keyed, what;
        unsigned int look, listed;
        positive got, total;
        int button[4];
};

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
        unsigned int *wheel_px; // the Solid colour card's picture: its colour, the kit's wheel over it (ui.c's ui_colour_card_*)
        unsigned long wheel_bytes;
        int wheel_w, wheel_h, wheel_scale, wheel_x, wheel_y; // the picture's size and scale, and the hand's place last told the wheel
        unsigned int wheel_look; // the theme and the accent it was drawn in
        b32 wheel_opened, wheel_drawn, wheel_busy, wheel_held; // made; its picture as it is; the kit's motion asks for frames; a press is its
        struct compositor_wall_band *band; // each curated card's source mark in its top corner (and its credit beside it while lifted), as drawn
        unsigned long band_bytes;
        unsigned int bands;
        int hover_source, press_source, key_card; // the card whose Source is under the hand, was pressed, has the keys (-1 none: the wheel has them)
        int *which;            // each card's curated picture (its place in the list), or COMPOSITOR_WALL_OTHER, _GHOST or _RETRY
        unsigned long which_bytes;
        unsigned int laid;     // compositor_walls.laid when the cards were laid
        struct compositor_wall_band *strip; // a curated card's fetch across its foot, by its picture's place in the list (bands' own count)
        unsigned long strip_bytes;
        unsigned int strips;
        struct compositor_wall_band note;   // the card to try again, whole
        positive polled;       // when the fetches' parts were last looked at
} compositor_wall_editor = {.hover = -1, .hover_seg = -1, .press_card = -1, .press_seg = -1, .hover_source = -1, .press_source = -1, .key_card = -1};

#define COMPOSITOR_WALL_OTHER -1 // what a card is besides a curated picture: one of the editor's own, a ghost of the feed still to come, the retry
#define COMPOSITOR_WALL_GHOST -2
#define COMPOSITOR_WALL_RETRY -3
#define COMPOSITOR_WALL_SYNC_MOST (90ull * 1000000000ull) // a sync heard nothing of this long is one that could not be had
#define COMPOSITOR_WALL_COLOUR_CARD 0 // the cards' places: Solid colour, then the Moonwater moon
#define COMPOSITOR_WALL_MOON_CARD 1
#define COMPOSITOR_WALL_SETTLE (400ull * 1000000ull) // a live colour is written this long after its last change, if no hand has it

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

        if (!compositor_wall_editor.open || compositor_wall_editor.cards <= COMPOSITOR_WALL_MOON_CARD ||
            !compositor_wall_editor_pill(compositor_wall_editor.card[COMPOSITOR_WALL_MOON_CARD], scale, pill))
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

// The cards laid out again: Solid colour (its wheel), the Moonwater dynamic wallpaper (with its Dark | Light | Auto switch), the pictures,
// the curated feed's, and the folder last.
static void compositor_wall_editor_lay(void)
{
        unsigned int count = 0, base = 2 + compositor_walls.names, shown = 0, hidden = 0, ghosts = 0, retry, n, cols, rows, at_card;
        const int (*screens)[4] = compositor_screens(&count);
        int screen = compositor_wall_editor.screen, gap = compositor_scenes_gap(), room[4], s[4], width, most;
        long long cw, ch;
        void *p;

        compositor_wall_editor.cards = 0, compositor_wall_editor.gen = compositor_walls.gen, compositor_wall_editor.laid = compositor_walls.laid;
        if (screen < 0 || (unsigned int)screen >= count || screens[screen][2] <= 0 || screens[screen][3] <= 0)
                return;
        memory_copy(s, screens[screen], sizeof(s));
        // The feed's cards: each whose thumbnail is here, and while the thumbnails are fetched a ghost for each still to come (before the list
        // is had, a row of them); after, one card to try again where some never came or the list could not be had.
        for (unsigned int at = 0; at < compositor_walls.curateds; at++)
                compositor_walls.credit[at].ready || compositor_walls.sync == COMPOSITOR_WALL_SYNCING ? shown++ : hidden++;
        retry = compositor_walls.sync == COMPOSITOR_WALL_UNSYNCED || (compositor_walls.sync == COMPOSITOR_WALL_SYNCED && hidden);
        compositor_room(s, room);
        width = room[2] - 2 * gap, ch = room[3] * 18 / 100, cw = ch * s[2] / s[3];
        if (compositor_walls.sync == COMPOSITOR_WALL_SYNCING && !compositor_walls.curateds)
                ghosts = cw + gap > 0 ? (unsigned int)((width + gap) / (cw + gap)) : 1, ghosts = ghosts ? ghosts : 1;
        n = base + shown + ghosts + retry + 1;
        p = compositor_wall_editor.which;
        if (!mem_buf_grow(&p, &compositor_wall_editor.which_bytes, (unsigned long)n * sizeof(int), 0))
                return;
        compositor_wall_editor.which = (int *)p;
        p = compositor_wall_editor.card;
        if (!mem_buf_grow(&p, &compositor_wall_editor.card_bytes, (unsigned long)n * sizeof(compositor_wall_editor.card[0]), 0))
                return;
        compositor_wall_editor.card = (int (*)[4])p;
        p = compositor_wall_editor.word;
        if (!mem_buf_grow(&p, &compositor_wall_editor.word_bytes, (unsigned long)n * SETTING_WALLPAPER_WORD, 0))
                return;
        compositor_wall_editor.word = (char (*)[SETTING_WALLPAPER_WORD])p;
        setting_wallpaper_colour_word(ui_colour_card_rgb(), compositor_wall_editor.word[COMPOSITOR_WALL_COLOUR_CARD]);
        string_copy((string_address)compositor_wall_editor.word[COMPOSITOR_WALL_MOON_CARD], "auto");
        for (unsigned int at = 0; at < compositor_walls.names; at++)
        {
                string_copy((string_address)compositor_wall_editor.word[2 + at], "picture:");
                string_append_bounded((p8 *)compositor_wall_editor.word[2 + at], (string_address)compositor_walls.name[at], SETTING_WALLPAPER_WORD);
        }
        for (unsigned int at = 0; at < n; at++)
                compositor_wall_editor.which[at] = COMPOSITOR_WALL_OTHER;
        at_card = base;
        for (unsigned int at = 0; at < compositor_walls.curateds; at++)
                if (compositor_walls.credit[at].ready || compositor_walls.sync == COMPOSITOR_WALL_SYNCING)
                        memory_copy(compositor_wall_editor.word[at_card], compositor_walls.curated[at], SETTING_WALLPAPER_WORD),
                        compositor_wall_editor.which[at_card++] = (int)at;
        for (unsigned int at = 0; at < ghosts; at++)
                string_copy((string_address)compositor_wall_editor.word[at_card], "~ghost"), compositor_wall_editor.which[at_card++] = COMPOSITOR_WALL_GHOST;
        if (retry)
                string_copy((string_address)compositor_wall_editor.word[at_card], "~retry"), compositor_wall_editor.which[at_card++] = COMPOSITOR_WALL_RETRY;
        compositor_wall_editor.word[n - 1][0] = 0;
        // As the overview's Scene cards: a fifth of the room high, the screen's shape; in rows, smaller, when one row would not hold them.
        most = room[3] * 55 / 100;
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

// The credit of card i, a curated wallpaper's, or none.
static int compositor_wall_editor_which(int card)
{
        return card >= 0 && (unsigned int)card < compositor_wall_editor.cards && compositor_wall_editor.which ? compositor_wall_editor.which[card] : COMPOSITOR_WALL_OTHER;
}

static const struct compositor_wall_credit *compositor_wall_editor_credit(int card)
{
        int at = compositor_wall_editor_which(card);

        if (at < 0 || (unsigned int)at >= compositor_walls.curateds || !compositor_walls.credit || memory_compare(compositor_wall_editor.word[card], "curated:", 8))
                return 0;
        return &compositor_walls.credit[at];
}

// Whether card i is a ghost: a feed's card still to come (its thumbnail not here, or the list not had yet). It takes no press and no keys.
static int compositor_wall_editor_ghost(int card)
{
        const struct compositor_wall_credit *credit = compositor_wall_editor_credit(card);

        return compositor_wall_editor_which(card) == COMPOSITOR_WALL_GHOST || (credit && !credit->ready);
}

// The fetch of card i's picture, if one goes or failed.
static struct compositor_wall_fetch *compositor_wall_editor_fetch(int card)
{
        return compositor_wall_editor_credit(card) && compositor_wall_fetches.count ? compositor_wall_fetch_of(compositor_wall_editor.word[card] + 8) : 0;
}

// Whether a curated card has its source mark: a page to open, or a credit to say.
static int compositor_wall_editor_marked(const struct compositor_wall_credit *credit)
{
        return credit && credit->ready && (credit->source[0] || credit->author[0] || credit->licence[0]);
}

// The strip across a card's top its source mark is drawn in (the mark at its end): the mark's side tall, in from the card's edges.
static int compositor_wall_editor_mark_strip(const int card[4], int scale, int out[4])
{
        int pad = 6 * scale / 100, side = UI_SOURCE_MARK_SIDE * scale / 100;

        pad = pad < 2 ? 2 : pad;
        side = side > card[3] / 3 ? card[3] / 3 : side;
        if (side < 12 || card[2] < side + 2 * pad)
                return 0;
        out[0] = card[0] + pad, out[1] = card[1] + pad, out[2] = card[2] - 2 * pad, out[3] = side;
        return 1;
}

// The card whose source mark a point is on (the mark as drawn, its hit the kit's least target round it, UI_TARGET), or -1.
static int compositor_wall_editor_source_at(int x, int y)
{
        int screen = compositor_wall_editor.screen, scale = compositor_output_scale(screen) > 0 ? compositor_output_scale(screen) : compositor.scale;
        int card = compositor_wall_editor_card_at(x, y), at = compositor_wall_editor_which(card), strip[4];
        const struct compositor_wall_credit *credit = compositor_wall_editor_credit(card);
        const struct compositor_wall_band *band;

        if (!compositor_wall_editor_marked(credit) || at < 0 || (unsigned int)at >= compositor_wall_editor.bands ||
            !compositor_wall_editor_mark_strip(compositor_wall_editor.card[card], scale, strip))
                return -1;
        band = &compositor_wall_editor.band[at];
        if (band->button[2] <= 0 || band->w != strip[2] || band->h != strip[3])
                return -1;
        return ui_reach_hit(1, UI_TARGET * scale / 100, x, y, strip[0] + band->button[0], strip[1] + band->button[1], band->button[2], band->button[3]) ? card : -1;
}

// A curated card's Source pressed (or Enter on it): its page opened by the open tool in the person's browser; the wallpaper not chosen.
static int compositor_wall_start_program(string_address *argv);
static const char *compositor_wall_opened; // the page last asked of the open tool (what the checks read: they start no program)
static void compositor_wall_editor_source(int card)
{
        const struct compositor_wall_credit *credit = compositor_wall_editor_credit(card);
        string_address argv[4] = {(string_address) "/open", (string_address) "--", 0, null};

        if (!credit || !credit->source[0] || !setting_wallpaper_source_ok(credit->source, string_length((string_address)credit->source)))
                return;
        argv[2] = (string_address)credit->source, compositor_wall_opened = credit->source;
        compositor_wall_start_program(argv);
}

// Curated card i's source mark drawn in its strip, w by h (premultiplied, its rows on whole 64-byte lines, laid over the card blended); 0 with no memory.
static const unsigned int *compositor_wall_editor_band_pixels(int card, int w, int h, int scale)
{
        struct windows *table = compositor.table;
        unsigned int themed = table && table->themed, look = themed ? (table->theme * 16777619u) ^ (table->accent & 0xffffffu) : 0;
        const struct compositor_wall_credit *credit = compositor_wall_editor_credit(card);
        unsigned int at = (unsigned int)compositor_wall_editor_which(card);
        int hover = compositor_wall_editor.open && compositor_wall_editor.hover_source == card, keyed = compositor_wall_editor.key_card == card;
        struct compositor_wall_band *band;
        void *p;

        if (!compositor_wall_editor_marked(credit) || w <= 0 || h <= 0)
                return 0;
        if (at >= compositor_wall_editor.bands)
        {
                p = compositor_wall_editor.band;
                if (!mem_buf_grow(&p, &compositor_wall_editor.band_bytes, (unsigned long)compositor_walls.curateds * sizeof(*band), MEM_BUF_KEEP))
                        return 0;
                compositor_wall_editor.band = (struct compositor_wall_band *)p;
                memory_zero(compositor_wall_editor.band + compositor_wall_editor.bands, (positive)(compositor_walls.curateds - compositor_wall_editor.bands) * sizeof(*band));
                compositor_wall_editor.bands = compositor_walls.curateds;
        }
        band = &compositor_wall_editor.band[at];
        if (band->pixels && band->w == w && band->h == h && band->scale == scale && band->hover == hover && band->keyed == keyed && band->look == look &&
            band->listed == compositor_walls.listed)
                return band->pixels;
        if (compositor_card_holds())
                return band->pixels && band->w == w && band->h == h ? band->pixels : 0;
        p = band->pixels;
        if (!mem_buf_grow(&p, &band->bytes, compositor_whole_lines(w) * (unsigned long)h * 4, MEM_BUF_GPU_FORGET))
                return band->pixels = 0, band->bytes = 0, (const unsigned int *)0;
        band->pixels = (unsigned int *)p, band->w = w, band->h = h, band->scale = scale, band->hover = hover, band->keyed = keyed, band->look = look;
        band->listed = compositor_walls.listed;
        memory_zero(band->pixels, compositor_whole_lines(w) * (unsigned long)h * 4);
        ui_source_card_draw(band->pixels, compositor_whole_lines(w), w, h, scale, compositor.engine, &compositor.face, themed ? table->theme : UI_THEME_LIGHT,
                            themed ? table->accent : UI_ACCENT_STANDARD, credit->author, credit->licence, 1, hover, keyed, band->button);
        return band->pixels;
}

// The bands let go of (the editor shut, or the list made again).
static void compositor_wall_editor_bands_free(void)
{
        for (unsigned int at = 0; at < compositor_wall_editor.bands; at++)
        {
                void *p = compositor_wall_editor.band[at].pixels;

                if (p)
                        mem_buf_free(&p, &compositor_wall_editor.band[at].bytes, MEM_BUF_GPU_FORGET);
                compositor_wall_editor.band[at].pixels = 0;
        }
        compositor_wall_editor.bands = 0;
}

static int compositor_wall_start_program(string_address *argv);
static void compositor_wall_editor_wheel_asks(p32 asks);
static void compositor_wall_editor_wheel_sync(int opening);

// The feed's thumbnails fetched again (canvas wallpaper curated fetch): its ghosts shown till they come, or till it says it could not.
static void compositor_wall_editor_resync(void)
{
        string_address argv[6] = {(string_address) "/moonwater", (string_address) "canvas", (string_address) "wallpaper", (string_address) "curated",
                                  (string_address) "fetch", null};

        if (compositor_wall_start_program(argv))
                compositor_walls.sync = COMPOSITOR_WALL_SYNCING, compositor_walls.sync_at = clock_monotonic_nanoseconds();
        compositor_walls.laid++;
        compositor_wall_editor_damage();
}

// The strip across a card's foot its picture's fetch is said in (its progress, or why it failed and Retry).
static int compositor_wall_editor_fetch_strip(const int card[4], int scale, int out[4])
{
        int pad = 6 * scale / 100, h = 34 * scale / 100;

        pad = pad < 2 ? 2 : pad;
        h = h > card[3] / 2 ? card[3] / 2 : h;
        if (h < 12 || card[2] < 2 * pad + 24)
                return 0;
        out[0] = card[0] + pad, out[1] = card[1] + card[3] - pad - h, out[2] = card[2] - 2 * pad, out[3] = h;
        return 1;
}

// A kept picture of the kit's (a fetch's strip, the card to try again) made again only when what it shows changed: 1 to draw it now.
static int compositor_wall_editor_keep_band(struct compositor_wall_band *band, int w, int h, int scale, int what, positive got, positive total, int hover,
                                            int keyed, unsigned int look)
{
        void *p;

        if (band->pixels && band->w == w && band->h == h && band->scale == scale && band->what == what && band->got == got && band->total == total &&
            band->hover == hover && band->keyed == keyed && band->look == look)
                return 0;
        if (compositor_card_holds())
                return band->pixels && band->w == w && band->h == h ? 0 : -1;
        p = band->pixels;
        if (!mem_buf_grow(&p, &band->bytes, compositor_whole_lines(w) * (unsigned long)h * 4, MEM_BUF_GPU_FORGET))
                return band->pixels = 0, band->bytes = 0, -1;
        band->pixels = (unsigned int *)p, band->w = w, band->h = h, band->scale = scale, band->what = what, band->got = got, band->total = total;
        band->hover = hover, band->keyed = keyed, band->look = look;
        return 1;
}

// Card i's fetch across its foot, w by h (premultiplied, laid blended): the kit's progress and bytes, or why it failed and Retry; 0 for none.
static const unsigned int *compositor_wall_editor_strip_pixels(int card, int w, int h, int scale)
{
        struct windows *table = compositor.table;
        unsigned int themed = table && table->themed, look = themed ? (table->theme * 16777619u) ^ (table->accent & 0xffffffu) : 0;
        const struct compositor_wall_fetch *f = compositor_wall_editor_fetch(card);
        unsigned int at = (unsigned int)compositor_wall_editor_which(card);
        struct compositor_wall_band *strip;
        int made;
        void *p;

        if (!f || at >= compositor_walls.curateds || w <= 0 || h <= 0)
                return 0;
        if (at >= compositor_wall_editor.strips)
        {
                p = compositor_wall_editor.strip;
                if (!mem_buf_grow(&p, &compositor_wall_editor.strip_bytes, (unsigned long)compositor_walls.curateds * sizeof(*strip), MEM_BUF_KEEP))
                        return 0;
                compositor_wall_editor.strip = (struct compositor_wall_band *)p;
                memory_zero(compositor_wall_editor.strip + compositor_wall_editor.strips, (positive)(compositor_walls.curateds - compositor_wall_editor.strips) * sizeof(*strip));
                compositor_wall_editor.strips = compositor_walls.curateds;
        }
        strip = &compositor_wall_editor.strip[at];
        made = compositor_wall_editor_keep_band(strip, w, h, scale, f->what, f->got, f->total, compositor_wall_editor.open && compositor_wall_editor.hover == card,
                                                compositor_wall_editor.key_card == card, look);
        if (made > 0)
                ui_fetch_card_draw(strip->pixels, compositor_whole_lines(w), w, h, scale, compositor.engine, &compositor.face, themed ? table->theme : UI_THEME_LIGHT,
                                   themed ? table->accent : UI_ACCENT_STANDARD, f->what, f->got, f->total, strip->hover, strip->keyed, strip->button);
        return made < 0 ? (const unsigned int *)0 : strip->pixels;
}

// The card to try again, w by h, whole: the feed's list or some of its thumbnails could not be had.
static const unsigned int *compositor_wall_editor_note_pixels(int card, int w, int h, int scale)
{
        struct windows *table = compositor.table;
        unsigned int themed = table && table->themed, look = themed ? (table->theme * 16777619u) ^ (table->accent & 0xffffffu) : 0;
        struct compositor_wall_band *note = &compositor_wall_editor.note;
        int made = compositor_wall_editor_keep_band(note, w, h, scale, UI_FETCH_UNSYNCED, 0, 0, compositor_wall_editor.open && compositor_wall_editor.hover == card,
                                                    compositor_wall_editor.key_card == card, look);

        if (made > 0)
        {
                ui_fetch_card_draw(note->pixels, compositor_whole_lines(w), w, h, scale, compositor.engine, &compositor.face, themed ? table->theme : UI_THEME_LIGHT,
                                   themed ? table->accent : UI_ACCENT_STANDARD, UI_FETCH_UNSYNCED, 0, 0, note->hover, note->keyed, note->button);
                memory_or_u32(note->pixels, note->pixels, (positive)compositor_whole_lines(w) * (positive)h, 0xff000000u);
        }
        return made < 0 ? (const unsigned int *)0 : note->pixels;
}

// Whether the keys stop at card i (Tab): a curated card's source mark, a fetch that failed, the card to try again.
static int compositor_wall_editor_stop(int card)
{
        const struct compositor_wall_fetch *f = compositor_wall_editor_fetch(card);

        return compositor_wall_editor_which(card) == COMPOSITOR_WALL_RETRY || compositor_wall_editor_marked(compositor_wall_editor_credit(card)) ||
               (f && f->what != UI_FETCH_GOING && !compositor_wall_editor_ghost(card));
}

// The editor opened on a screen (how 1), shut (0), or turned (-1). The overview shuts when it opens, as the editor does when the overview opens.
static void compositor_wall_edit(int screen, int how)
{
        int open = how < 0 ? !compositor_wall_editor.open || (screen >= 0 && screen != compositor_wall_editor.screen) : how;

        if (!open)
        {
                if (compositor_wall_editor.open)
                        compositor_wall_editor_damage();
                // The wheel's field gives its digits up as they are (a colour, or put back), and a live colour is written now.
                if (compositor_wall_editor.wheel_opened)
                        compositor_wall_editor_wheel_asks(ui_colour_card_rest());
                compositor_wall_editor.wheel_held = 0, compositor_wall_editor.wheel_drawn = 0;
                compositor_wall_editor.hover_source = compositor_wall_editor.press_source = compositor_wall_editor.key_card = -1;
                compositor_walls_flush();
                compositor_wall_editor.open = compositor_wall_editor.pressed = compositor_wall_editor.hover_close = 0;
                compositor_wall_editor.hover = compositor_wall_editor.hover_seg = compositor_wall_editor.press_card = compositor_wall_editor.press_seg = -1;
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
                // The feed's curated wallpapers asked again (the command keeps the feed it verified and asks the network only when that is
                // hours old; with no feed that verifies there are none, and the cards are the moon's and the pictures'), its ghosts shown.
                compositor_wall_editor_resync();
        compositor_wall_editor.open = 1, compositor_wall_editor.screen = screen;
        compositor_wall_editor.hover_source = compositor_wall_editor.press_source = compositor_wall_editor.key_card = -1;
        compositor_wall_editor_wheel_sync(1);
        compositor_wall_editor.pressed = 0, compositor_wall_editor.hover_close = 0;
        compositor_wall_editor.hover = compositor_wall_editor.hover_seg = compositor_wall_editor.press_card = compositor_wall_editor.press_seg = -1;
        compositor_wall_editor_lay();
        compositor_wall_editor_damage();
}

// The editor opened on desktop Scene number (from 1) of a screen: that Scene shown first, then the editor over it (the Scenes overview's
// brush on a Scene's card, the desktop's menu). A number the screen has no desktop Scene of opens it on the Scene shown.
static void compositor_wall_edit_scene(int screen, unsigned int number)
{
        unsigned int seen = 0;

        compositor_spaces_ensure();
        if (screen < 0 || screen >= compositor_output_count())
                screen = 0;
        for (unsigned int i = 0; i < compositor.spaces.count && number; i++)
                if (compositor.spaces.list[i].screen == screen && compositor.spaces.list[i].kind == COMPOSITOR_SPACE_DESKTOP && ++seen == number)
                {
                        compositor_space_activate(screen, compositor.spaces.list[i].id);
                        break;
                }
        compositor_wall_edit(screen, 1);
}

// The editor shut at once, with no motion (the screens changed under it).
static void compositor_wall_editor_shut(void)
{
        compositor_wall_edit(-1, 0);
        compositor_wall_editor.shown = 0, compositor_wall_editor.speed = 0, compositor_wall_editor.stepped = 0;
}

// A program started apart from the compositor, its own session with nothing on its standard files (as compositor_alert_show starts one).
// The checks start none (compositor_wall_starts 0, or 2: as if it were started): what they press is the compositor's half alone.
static int compositor_wall_starts = 1;
static int compositor_wall_start_program(string_address *argv)
{
        bipolar child = compositor_wall_starts == 1 ? system_fork() : -1;

        if (compositor_wall_starts == 2)
                return 1;
        if (child < 0)
                return 0;
        if (child == 0)
        {
                if (system_fork() == 0)
                {
                        unsigned long sigmask = COMPOSITOR_SIG_MASK;
                        bipolar oom = system_open_at(AT_FDCWD, (string_address) "/proc/self/oom_score_adj", FILE_WRITE | O_CLOEXEC);
                        bipolar quiet = system_open_at(AT_FDCWD, (string_address) "/dev/null", FILE_READ_WRITE | O_CLOEXEC);

                        compositor_ordinary(0);
                        compositor_call(syscall(rt_sigprocmask), 1, (positive)address_of sigmask, 0, 8, 0, 0);
                        if (oom >= 0)
                                (void)system_write_all((positive)oom, (string_address) "0\n", 2), system_close(oom);
                        system_call(syscall(setsid));
                        for (positive at = 0; quiet >= 0 && at < 3; at++)
                                system_call_3(syscall(dup3), (positive)quiet, at, 0);
                        system_call_3(syscall(execve), (positive)argv[0], (positive)argv, (positive)program_environment_list());
                        system_call_1(syscall(exit_group), 127);
                }
                system_call_1(syscall(exit_group), 0);
        }
        (void)system_wait4_retry(child, null, 0, null);
        return 1;
}


/*
        The Solid colour card's wheel (ui.c's ui_colour, through ui_colour_card_*): drawn by the kit into the card's picture over the colour,
        at the card's size and its screen's scale, in the theme of the windows. Its events are the hand's and the keys' as window events, at
        the card's place where it is all the way down; what it asks is the Scene's colour, live while a hand moves it.
*/
static const unsigned int *compositor_wall_editor_wheel_pixels(int w, int h, int scale)
{
        struct windows *table = compositor.table;
        unsigned int themed = table && table->themed;
        unsigned long pitch = compositor_whole_lines(w);
        unsigned int look = themed ? (table->theme * 16777619u) ^ (table->accent & 0xffffffu) : 0;
        void *p;

        if (w <= 0 || h <= 0 || !compositor_wall_editor.wheel_opened)
                return 0;
        if (compositor_wall_editor.wheel_px && compositor_wall_editor.wheel_drawn && !compositor_wall_editor.wheel_busy && compositor_wall_editor.wheel_w == w &&
            compositor_wall_editor.wheel_h == h && compositor_wall_editor.wheel_scale == scale && compositor_wall_editor.wheel_look == look)
                return compositor_wall_editor.wheel_px;
        if (compositor_card_holds())
                return compositor_wall_editor.wheel_px && compositor_wall_editor.wheel_w == w && compositor_wall_editor.wheel_h == h ? compositor_wall_editor.wheel_px : 0;
        p = compositor_wall_editor.wheel_px;
        if (!mem_buf_grow(&p, &compositor_wall_editor.wheel_bytes, pitch * (unsigned long)h * 4, MEM_BUF_GPU_FORGET))
                return compositor_wall_editor.wheel_px = 0, compositor_wall_editor.wheel_bytes = 0, (const unsigned int *)0;
        compositor_wall_editor.wheel_px = (unsigned int *)p;
        compositor_wall_editor.wheel_w = w, compositor_wall_editor.wheel_h = h, compositor_wall_editor.wheel_scale = scale, compositor_wall_editor.wheel_look = look;
        memory_fill_u32_rect(compositor_wall_editor.wheel_px, (positive)pitch, (positive)w, (positive)h, 0xff000000u | ui_colour_card_rgb());
        compositor_wall_editor.wheel_busy = ui_colour_card_draw(compositor_wall_editor.wheel_px, pitch, w, h, scale, compositor.engine, &compositor.face,
                                                                themed ? table->theme : UI_THEME_LIGHT, themed ? table->accent : UI_ACCENT_STANDARD) != 0;
        memory_or_u32(compositor_wall_editor.wheel_px, compositor_wall_editor.wheel_px, (positive)pitch * (positive)h, 0xff000000u);
        compositor_wall_editor.wheel_drawn = 1;
        return compositor_wall_editor.wheel_px;
}

// What the wheel asks: its colour the Scene's (live while it moves, kept when it rests), and its card drawn again.
static void compositor_wall_editor_wheel_asks(p32 asks)
{
        int screen = compositor_wall_editor.screen;
        char word[SETTING_WALLPAPER_WORD];

        if (asks & (UI_COLOUR_CHANGED | UI_COLOUR_DONE))
        {
                compositor_wall_pending.word[0] = 0; // a colour chosen is the last choice: a picture still on its way is no longer the Scene's
                setting_wallpaper_colour_word(ui_colour_card_rgb(), word);
                if (compositor_wall_editor.cards > COMPOSITOR_WALL_COLOUR_CARD)
                        string_copy((string_address)compositor_wall_editor.word[COMPOSITOR_WALL_COLOUR_CARD], (string_address)word);
                compositor_wall_put_named(compositor_wall_screen_name(screen), compositor_wall_current(screen), word, !(asks & UI_COLOUR_DONE));
        }
        if (asks & UI_COLOUR_DONE)
                compositor_walls_flush();
        if (asks & (UI_COLOUR_CHANGED | UI_COLOUR_DONE | UI_COLOUR_DRAW | UI_COLOUR_TYPING))
                compositor_wall_editor.wheel_drawn = 0, compositor_wall_editor_damage();
}

// An event told the wheel at the hand's place on card 0 (where it is all the way down); its asks done. Answers them.
static p32 compositor_wall_editor_wheel_send(unsigned int type, unsigned int flags, int a, int b)
{
        const int *card;
        p32 asks;

        if (!compositor_wall_editor.wheel_opened || compositor_wall_editor.cards <= COMPOSITOR_WALL_COLOUR_CARD)
                return 0;
        card = compositor_wall_editor.card[COMPOSITOR_WALL_COLOUR_CARD];
        compositor_wall_editor.wheel_x = compositor.pointer_x - card[0], compositor_wall_editor.wheel_y = compositor.pointer_y - card[1];
        asks = ui_colour_card_event(type, flags, a, b, compositor_wall_editor.wheel_x, compositor_wall_editor.wheel_y);
        compositor_wall_editor_wheel_asks(asks);
        return asks;
}

// The wheel made (at the background colour kept), and at the colour of the Scene shown when it has one and no hand or keys are on the wheel.
static void compositor_wall_editor_wheel_sync(int opening)
{
        int screen = compositor_wall_editor.screen;
        long colour;

        if (!compositor_wall_editor.wheel_opened && !(compositor_wall_editor.wheel_opened = ui_colour_card_open(compositor_ground & 0xffffffu)))
                return;
        if (opening)
                ui_colour_card_rest(), compositor_wall_editor.wheel_held = 0, compositor_wall_editor.wheel_drawn = 0;
        colour = setting_wallpaper_colour(compositor_wall_word(screen, compositor_wall_current(screen)));
        if (colour >= 0 && (unsigned int)colour != ui_colour_card_rgb() && !(ui_colour_card_state() & (UI_COLOUR_HELD | UI_COLOUR_FIELD)))
                ui_colour_card_set((unsigned int)colour), compositor_wall_editor.wheel_drawn = 0;
}

// The keys while the editor is open are the wheel's (input.c's compositor_key asks here first): every key but a chord, and a chord too
// while its field types. A key's release is taken where its press was. Answers whether it was taken.
static unsigned char compositor_wall_editor_keys[32];
static int compositor_wall_editor_key(unsigned int code, int value, unsigned int modifiers, const unsigned int *characters, int count)
{
        unsigned char mask = (unsigned char)(1u << (code & 7)), *bit = code < 256 ? &compositor_wall_editor_keys[code >> 3] : 0;

        if (!value)
        {
                if (!bit || !(*bit & mask))
                        return 0;
                *bit &= (unsigned char)~mask;
                return 1;
        }
        if (value == 2 && bit && !(*bit & mask))
                return 0;
        if (!compositor_wall_editor.open || !compositor_wall_editor.wheel_opened ||
            ((modifiers & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_SUPER)) && !(ui_colour_card_state() & UI_COLOUR_FIELD)))
                return 0;
        if (bit)
                *bit |= mask;
        // Tab goes from the wheel to each curated card's source mark and back (Shift the other way); Enter or Space on one opens its page.
        if (code == 15 && !(ui_colour_card_state() & UI_COLOUR_FIELD))
        {
                int way = modifiers & WINDOW_KEY_SHIFT ? -1 : 1, at = compositor_wall_editor.key_card, n = (int)compositor_wall_editor.cards;

                do
                        at = at < 0 ? (way > 0 ? 0 : n - 1) : at + way;
                while (at >= 0 && at < n && !compositor_wall_editor_stop(at));
                compositor_wall_editor.key_card = at >= 0 && at < n ? at : -1;
                ui_colour_card_keys(compositor_wall_editor.key_card < 0);
                compositor_wall_editor.wheel_drawn = 0, compositor_wall_editor_damage();
                return 1;
        }
        if (compositor_wall_editor.key_card >= 0)
        {
                int card = compositor_wall_editor.key_card;
                const struct compositor_wall_fetch *f = compositor_wall_editor_fetch(card);
                int screen = compositor_wall_editor.screen;

                // Enter or Space: the card to try again fetches again, a failed picture is chosen again (fetched again), a mark opens its page.
                if (value == 1 && (code == 28 || code == 96 || code == 57))
                {
                        if (compositor_wall_editor_which(card) == COMPOSITOR_WALL_RETRY)
                                compositor_wall_editor_resync();
                        else if (f && f->what != UI_FETCH_GOING)
                                compositor_wall_choose_named(compositor_wall_screen_name(screen), compositor_wall_current(screen), compositor_wall_editor.word[card]),
                                compositor_wall_editor_damage();
                        else
                                compositor_wall_editor_source(card);
                }
                return 1;
        }
        for (int each = 0; each < (count ? count : 1); each++)
                compositor_wall_editor_wheel_send(WINDOW_EVENT_KEY, modifiers | WINDOW_KEY_DOWN | (value == 2 ? WINDOW_KEY_REPEAT : 0), (int)code,
                                                  count ? (int)characters[each] : 0);
        return 1;
}

// Escape while the editor is open: the wheel's field gives the keys back with the colour it had (1), or nothing is the wheel's (0).
static int compositor_wall_editor_escape(void)
{
        if (!(ui_colour_card_state() & UI_COLOUR_FIELD))
                return 0;
        compositor_wall_editor_wheel_send(WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, (int)CK_KEY_ESC, 0);
        return 1;
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
        // A ghost takes no press (nor does it shut the editor); the card to try again fetches the feed's thumbnails again.
        if (compositor_wall_editor_ghost(card))
                return;
        if (compositor_wall_editor_which(card) == COMPOSITOR_WALL_RETRY)
                return compositor_wall_editor_resync();
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
        if (card == COMPOSITOR_WALL_COLOUR_CARD)
        {
                // Solid colour, pressed away from its wheel: the wheel's colour is the Scene's.
                setting_wallpaper_colour_word(ui_colour_card_rgb(), compositor_wall_editor.word[card]);
                word = compositor_wall_editor.word[card];
        }
        if (card == COMPOSITOR_WALL_MOON_CARD)
        {
                int seg = compositor_wall_editor_pill_seg(x, y);
                const char *cur = compositor_wall_word(screen, compositor_wall_current(screen));

                word = seg >= 0 ? compositor_wall_seg_word(seg) : compositor_wall_is_moon(cur) ? cur : "auto";
        }
        // A curated picture not here yet is fetched and given to the Scene when it has come, verified and decoded: till then it shows what it did.
        compositor_wall_choose_named(compositor_wall_screen_name(screen), compositor_wall_current(screen), word);
        compositor_wall_editor_damage();
}

// The editor takes every press while it is open but one on the band, and the release of a press it took.
static int compositor_wall_editor_takes(unsigned int code, int down)
{
        unsigned int count = 0, index = code - COMPOSITOR_BTN_LEFT;
        int screen = compositor_wall_editor.screen;

        (void)compositor_screens(&count);
        if (!compositor_wall_editor.open || screen < 0 || (unsigned int)screen >= count)
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
                // A press on the wheel is the wheel's (its disc, its bar, its field); one anywhere else first takes the keys from its field.
                if (compositor_wall_editor_card_at(compositor.pointer_x, compositor.pointer_y) == COMPOSITOR_WALL_COLOUR_CARD ||
                    (ui_colour_card_state() & (UI_COLOUR_KEYS | UI_COLOUR_FIELD)))
                        if (compositor_wall_editor_wheel_send(WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, (int)COMPOSITOR_BTN_LEFT, 0) & UI_COLOUR_TAKEN)
                        {
                                compositor_wall_editor.wheel_held = 1;
                                return;
                        }
                compositor_wall_editor.pressed = 1, compositor_wall_editor.at_x = compositor.pointer_x, compositor_wall_editor.at_y = compositor.pointer_y;
                compositor_wall_editor.press_source = compositor_wall_editor_source_at(compositor.pointer_x, compositor.pointer_y);
                compositor_wall_editor.key_card = -1; // the hand's press: the keys are the wheel's again
                compositor_wall_editor.press_card = compositor_wall_editor_card_at(compositor.pointer_x, compositor.pointer_y);
                compositor_wall_editor.press_seg = compositor_wall_editor_pill_seg(compositor.pointer_x, compositor.pointer_y);
                return;
        }
        if (compositor_wall_editor.wheel_held)
        {
                compositor_wall_editor.wheel_held = 0;
                compositor_wall_editor_wheel_send(WINDOW_EVENT_BUTTON, 0, (int)COMPOSITOR_BTN_LEFT, 0);
                return;
        }
        if (!compositor_wall_editor.pressed)
                return;
        compositor_wall_editor.pressed = 0;
        // A press on a curated card's Source is its page opened when it is let go there, and never the wallpaper chosen.
        if (compositor_wall_editor.press_source >= 0)
        {
                if (compositor_wall_editor_source_at(compositor.pointer_x, compositor.pointer_y) == compositor_wall_editor.press_source)
                        compositor_wall_editor_source(compositor_wall_editor.press_source);
                compositor_wall_editor.press_source = -1;
                return;
        }
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
                if (compositor_wall_editor.bands && !compositor_wall_editor_shown() && !compositor_card_holds())
                        compositor_wall_editor_bands_free();
                if (compositor_wall_editor.wheel_px && !compositor_wall_editor_shown() && !compositor_card_holds())
                        p = compositor_wall_editor.wheel_px, mem_buf_free(&p, &compositor_wall_editor.wheel_bytes, MEM_BUF_GPU_FORGET),
                        compositor_wall_editor.wheel_px = 0, compositor_wall_editor.wheel_w = compositor_wall_editor.wheel_h = 0,
                        compositor_wall_editor.wheel_drawn = 0;
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
        if (!mem_buf_grow(&p, &compositor_wall_editor.veil_bytes, compositor_whole_lines(s[2]) * (unsigned long)s[3] * 4, MEM_BUF_GPU_FORGET))
                return (void)(compositor_wall_editor.veil = 0, compositor_wall_editor.veil_bytes = 0, compositor_wall_editor.veil_level = 0);
        compositor_wall_editor.veil = (unsigned int *)p;
        alpha = COMPOSITOR_WALL_VEIL * (unsigned int)level / COMPOSITOR_WALL_VEIL_STEPS;
        for (int y = 0; y < s[3]; y++)
        {
                const unsigned int *from = compositor_wall && s[0] >= 0 && s[1] >= 0 && s[0] + s[2] <= compositor_wall_w && s[1] + s[3] <= compositor_wall_h
                                               ? compositor_wall + (long)(s[1] + y) * compositor_wall_pitch + s[0] : 0;
                unsigned int *into = compositor_wall_editor.veil + (unsigned long)y * compositor_whole_lines(s[2]);

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
        unsigned long pitch = compositor_whole_lines(w); // the miniatures' rows, and this one's
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
        if (!mem_buf_grow(&p, &m->bytes, pitch * (unsigned long)h * 4, MEM_BUF_GPU_FORGET))
                return m->pixels = 0, m->bytes = 0, dark;
        m->pixels = (unsigned int *)p, m->used = clock_monotonic_nanoseconds();
        for (int y = 0; y < h; y++)
        {
                int cut = w - (int)((long long)y * w / h);

                memory_copy(m->pixels + (unsigned long)y * pitch, dark + (unsigned long)y * pitch, (positive)cut * 4);
                memory_copy(m->pixels + (unsigned long)y * pitch + cut, white + (unsigned long)y * pitch + cut, (positive)(w - cut) * 4);
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
        if (!mem_buf_grow(&p, &compositor_wall_editor.pill_bytes, compositor_whole_lines(w) * (unsigned long)h * 4, MEM_BUF_GPU_FORGET))
                return compositor_wall_editor.pill = 0, compositor_wall_editor.pill_bytes = 0, (const unsigned int *)0;
        compositor_wall_editor.pill = (unsigned int *)p;
        compositor_wall_editor.pill_w = w, compositor_wall_editor.pill_h = h;
        compositor_wall_editor.pill_active = active, compositor_wall_editor.pill_hover = hover, compositor_wall_editor.pill_key = key;

        // (its rows on whole 64-byte lines, as the editor's other pictures: compositor_whole_lines)
        memory_fill_u32_rect(compositor_wall_editor.pill, (positive)compositor_whole_lines(w), (positive)w, (positive)h, track);
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
                                        memory_fill_u32_rect(compositor_wall_editor.pill + (unsigned long)y * compositor_whole_lines(w) + sx0 + cut, (positive)compositor_whole_lines(w),
                                                             (positive)(sx1 - sx0 - 2 * cut), 1, bg);
                        }
                }
                if (compositor.engine && sx1 > sx0 && inner_h >= 8)
                {
                        const font_face address_to faces[1] = {&compositor.face};
                        font_target target = {compositor_wall_editor.pill, (positive)compositor_whole_lines(w), (b32)w, (b32)h};
                        p32 em = (p32)((inner_h * 56 / 100) << 6);
                        font_metrics fm;
                        positive len = string_length((string_address)labels[seg]);
                        int tw, tx, ty;

                        if (em < (8u << 6))
                                em = 8u << 6;
                        fm = font_metrics_at(&compositor.face, em);
                        // (measured by font_extent: font_draw with no target to draw into read the target's fields and the compositor fell, at the
                        // editor's first frame with the switch in it)
                        tw = (int)(font_extent(compositor.engine, faces, 1, (const p8 address_to)labels[seg], len, em, 0) >> 16);
                        tx = sx0 + ((sx1 - sx0) - tw) / 2;
                        if (tx < sx0)
                                tx = sx0;
                        ty = sy0 + (inner_h - (int)fm.cell_height) / 2 + (int)fm.baseline;
                        font_draw(compositor.engine, faces, 1, (const p8 address_to)labels[seg], len, em, 0, &target,
                                  (b32)(tx << 16), (b32)ty, fg & 0xffffffu, bg & 0xffffffu);
                }
        }
        memory_or_u32(compositor_wall_editor.pill, compositor_wall_editor.pill, (positive)compositor_whole_lines(w) * (positive)h, 0xff000000u);
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
        int screen = compositor_wall_editor.screen, scale, radius, line, lit_w, s[4], clip[4], colour;
        unsigned int accent = compositor_ink_role(UI_ACCENT_BRIGHT) & 0xffffffu;
        const char *current;
        b64 t = compositor_wall_editor_t();

        if (!compositor_wall_editor_shown() || screen < 0 || (unsigned int)screen >= count)
                return 0;
        if (compositor_wall_editor.gen != compositor_walls.gen || compositor_wall_editor.laid != compositor_walls.laid || !compositor_wall_editor.cards)
                compositor_wall_editor_lay();
        memory_copy(s, screens[screen], sizeof(s));
        clip[0] = s[0], clip[1] = s[1], clip[2] = s[0] + s[2], clip[3] = s[1] + s[3];
        scale = compositor_output_scale(screen) > 0 ? compositor_output_scale(screen) : compositor.scale;
        radius = UI_RADIUS * scale / 100, line = compositor_line_px(scale), lit_w = 2 * compositor_lit_px(scale);
        current = compositor_wall_word(screen, compositor_wall_current(screen));
        colour = setting_wallpaper_colour(current) >= 0;
        if (compositor_wall_editor.veil && compositor_wall_editor.veil_level && at < room &&
            !memory_compare(compositor_wall_editor.veil_rect, s, sizeof(s)))
        {
                struct compositor_frame *veil = &frames[at];

                memory_zero(veil, sizeof(*veil));
                veil->x = s[0], veil->y = s[1], veil->width = s[2], veil->height = s[3], veil->scale = scale, veil->layer = 1, veil->blend = 1;
                veil->pixels = compositor_wall_editor.veil, veil->pitch = compositor_whole_lines(s[2]);
                rows[at++] = filler;
        }
        for (unsigned int card = 0; card < compositor_wall_editor.cards; card++)
        {
                const char *word = compositor_wall_editor.word[card];
                int c[4], lit = compositor_wall_editor.open && compositor_wall_editor.hover == (int)card && compositor_wall_editor_which((int)card) != COMPOSITOR_WALL_GHOST;
                const unsigned int *mini;
                int selected = card == COMPOSITOR_WALL_COLOUR_CARD ? colour
                               : card == COMPOSITOR_WALL_MOON_CARD ? compositor_wall_is_moon(current)
                                                                   : word[0] && string_equals((string_address)word, (string_address)current);

                compositor_wall_editor_card_shown(card, s, t, c);
                if (!compositor_scenes_plain(frames, rows, at, room, filler, c, clip, scale))
                        continue;
                // A ghost: the card's shape on the surface, its ground breathing (still with reduced motion), nothing on it, never lit.
                if (compositor_wall_editor_ghost((int)card))
                {
                        struct windows *table = compositor.table;
                        unsigned int themed = table && table->themed;

                        frames[at].fill = ui_ghost_card_ink(themed ? table->theme : UI_THEME_LIGHT, themed ? table->accent : UI_ACCENT_STANDARD,
                                                            clock_monotonic_nanoseconds(), !compositor_glides());
                        frames[at].radius = radius, frames[at].line = line, frames[at].edge = compositor_line_unlit();
                        at++;
                        continue;
                }
                if (compositor_wall_editor_which((int)card) == COMPOSITOR_WALL_RETRY)
                        mini = compositor_wall_editor_note_pixels((int)card, c[2], c[3], scale);
                else if (card == COMPOSITOR_WALL_COLOUR_CARD)
                        mini = compositor_wall_editor_wheel_pixels(c[2], c[3], scale);
                else if (card == COMPOSITOR_WALL_MOON_CARD)
                        mini = string_equals((string_address)current, "dark") ? compositor_wall_mini("dark", c[2], c[3])
                               : string_equals((string_address)current, "white") ? compositor_wall_mini("white", c[2], c[3])
                                                                                 : compositor_wall_editor_auto(c[2], c[3]);
                else
                        mini = !word[0] ? 0 : compositor_wall_mini(word, c[2], c[3]);
                if (mini)
                        frames[at].pixels = mini + (unsigned long)(frames[at].y - c[1]) * compositor_whole_lines(c[2]) + (frames[at].x - c[0]),
                        frames[at].pitch = compositor_whole_lines(c[2]);
                else
                        frames[at].fill = compositor_ink_role(UI_SURFACE_CONTAINER) | 0xff000000u;
                frames[at].radius = radius, frames[at].line = line;
                frames[at].edge = selected ? accent : compositor_line_unlit();
                if (lit)
                        frames[at].lit = COMPOSITOR_LIT_ALL, frames[at].lit_w = lit_w, frames[at].lit_ink = COMPOSITOR_INK_LINE_HOVER;
                if (lit && compositor_wall_editor_close((int)card, &(int){0}, &(int){0}, &(int){0}))
                        frames[at].close = 1;
                at++;
                if (card == COMPOSITOR_WALL_MOON_CARD)
                {
                        int pill[4];

                        if (compositor_wall_editor_pill(c, scale, pill) && compositor_scenes_plain(frames, rows, at, room, filler, pill, clip, scale))
                        {
                                const unsigned int *px = compositor_wall_editor_pill_pixels(
                                    pill[2], pill[3], compositor_wall_moon_seg(current),
                                    compositor_wall_editor.open ? compositor_wall_editor.hover_seg : -1, scale);

                                if (px)
                                        frames[at].pixels = px + (unsigned long)(frames[at].y - pill[1]) * compositor_whole_lines(pill[2]) + (frames[at].x - pill[0]),
                                        frames[at].pitch = compositor_whole_lines(pill[2]);
                                else
                                        frames[at].fill = compositor_ink_role(UI_SURFACE_HIGHEST) | 0xff000000u;
                                frames[at].radius = pill[3] / 2, frames[at].line = line, frames[at].edge = compositor_line_unlit();
                                at++;
                        }
                }
                if (compositor_wall_editor_marked(compositor_wall_editor_credit((int)card)))
                {
                        // A curated wallpaper's source mark over its top corner, glass, blended over the picture (its credit beside it while lifted).
                        int band[4];

                        if (compositor_wall_editor_mark_strip(c, scale, band) && compositor_scenes_plain(frames, rows, at, room, filler, band, clip, scale))
                        {
                                const unsigned int *px = compositor_wall_editor_band_pixels((int)card, band[2], band[3], scale);

                                if (px)
                                {
                                        frames[at].pixels = px + (unsigned long)(frames[at].y - band[1]) * compositor_whole_lines(band[2]) + (frames[at].x - band[0]);
                                        frames[at].pitch = compositor_whole_lines(band[2]), frames[at].blend = 1;
                                        at++;
                                }
                        }
                }
                if (compositor_wall_editor_fetch((int)card))
                {
                        // Its picture on its way: the kit's progress and its bytes across its foot, or why it failed and Retry.
                        int strip[4];

                        if (compositor_wall_editor_fetch_strip(c, scale, strip) && compositor_scenes_plain(frames, rows, at, room, filler, strip, clip, scale))
                        {
                                const unsigned int *px = compositor_wall_editor_strip_pixels((int)card, strip[2], strip[3], scale);

                                if (px)
                                {
                                        frames[at].pixels = px + (unsigned long)(frames[at].y - strip[1]) * compositor_whole_lines(strip[2]) + (frames[at].x - strip[0]);
                                        frames[at].pitch = compositor_whole_lines(strip[2]), frames[at].blend = 1;
                                        at++;
                                }
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

/*
        While the editor is open: the ghosts breathe (each drawn again a frame at a time while the motion is on), a picture coming has how
        much of it is here looked at a tenth of a second at a time (its part's size), and a sync that has told nothing for
        COMPOSITOR_WALL_SYNC_MOST is one that could not be had (its ghosts the card to try again).
*/
static void compositor_wall_editor_loading(positive now)
{
        if (compositor_walls.sync == COMPOSITOR_WALL_SYNCING && now - compositor_walls.sync_at > COMPOSITOR_WALL_SYNC_MOST)
                compositor_walls.sync = COMPOSITOR_WALL_UNSYNCED, compositor_walls.laid++, compositor_wall_editor_damage();
        for (unsigned int card = 0; compositor_glides() && card < compositor_wall_editor.cards; card++)
                if (compositor_wall_editor_ghost((int)card))
                        compositor_damage(compositor_wall_editor.card[card][0], compositor_wall_editor.card[card][1], compositor_wall_editor.card[card][2],
                                          compositor_wall_editor.card[card][3]);
        if (now - compositor_wall_editor.polled < 100000000ull)
                return;
        compositor_wall_editor.polled = now;
        for (unsigned int at = 0; at < compositor_wall_fetches.count; at++)
        {
                struct compositor_wall_fetch *f = &compositor_wall_fetches.list[at];
                char path[sizeof(SETTING_WALLPAPER_DIR "/curated/") + 64 + 8];
                p8 info[256] = {0};
                positive got;

                if (f->what != UI_FETCH_GOING)
                        continue;
                string_join((p8 *)path, sizeof(path), (string_address)SETTING_WALLPAPER_DIR "/curated/", (string_address)f->hex);
                string_append_bounded((p8 *)path, (string_address) ".part", sizeof(path));
                got = compositor_call(syscall(statx), (positive)AT_FDCWD, (positive)path, 0x100, 0x200, (positive)info, 0) >= 0 ? memory_load_unaligned(p64, info + 40) : 0;
                if (got == f->got)
                        continue;
                f->got = got;
                for (unsigned int card = 0; card < compositor_wall_editor.cards; card++)
                        if (compositor_wall_editor_fetch((int)card) == f)
                                compositor_damage(compositor_wall_editor.card[card][0], compositor_wall_editor.card[card][1], compositor_wall_editor.card[card][2],
                                                  compositor_wall_editor.card[card][3]);
        }
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

        // A hand on the wheel's disc or bar (or a drag in its field) is where the pointer is now.
        if (compositor_wall_editor.wheel_held && compositor_wall_editor.cards > COMPOSITOR_WALL_COLOUR_CARD &&
            (x - compositor_wall_editor.card[COMPOSITOR_WALL_COLOUR_CARD][0] != compositor_wall_editor.wheel_x ||
             y - compositor_wall_editor.card[COMPOSITOR_WALL_COLOUR_CARD][1] != compositor_wall_editor.wheel_y))
                compositor_wall_editor_wheel_send(WINDOW_EVENT_MOTION, 0, 0, 0);

        int source = compositor_wall_editor_source_at(x, y);

        if (card != compositor_wall_editor.hover || close != compositor_wall_editor.hover_close || seg != compositor_wall_editor.hover_seg ||
            source != compositor_wall_editor.hover_source)
                compositor_wall_editor_damage();
        compositor_wall_editor.hover = card, compositor_wall_editor.hover_close = close, compositor_wall_editor.hover_seg = seg;
        compositor_wall_editor.hover_source = source;
}

// When the loop is owed its next pass for the spring: a frame after the last present while it is on its way, 0 at rest.
static positive compositor_wall_editor_due(positive now)
{
        positive period = compositor.period ? compositor.period : 16666667ull;
        b64 goal = compositor_wall_editor.open ? COMPOSITOR_SCENES_FULL : 0;

        // A live colour is written a moment after its last change; the wheel's caret blinks a frame at a time while its field types.
        if (compositor_walls.unwritten && !compositor_wall_editor.wheel_held)
                return compositor_walls.changed + COMPOSITOR_WALL_SETTLE > now ? compositor_walls.changed + COMPOSITOR_WALL_SETTLE : now;
        if (compositor_wall_editor.open && compositor_wall_editor.wheel_busy)
                return compositor.presented + period > now ? compositor.presented + period : now;
        // Ghosts breathing a frame at a time; a picture coming looked at a tenth of a second at a time.
        for (unsigned int card = 0; compositor_wall_editor.open && compositor_glides() && card < compositor_wall_editor.cards; card++)
                if (compositor_wall_editor_ghost((int)card))
                        return compositor.presented + period > now ? compositor.presented + period : now;
        for (unsigned int at = 0; compositor_wall_editor.open && at < compositor_wall_fetches.count; at++)
                if (compositor_wall_fetches.list[at].what == UI_FETCH_GOING)
                        return compositor_wall_editor.polled + 100000000ull > now ? compositor_wall_editor.polled + 100000000ull : now;
        if (!compositor_glides() || (compositor_wall_editor.shown == goal && !compositor_wall_editor.speed))
                return 0;
        return compositor.presented + period > now ? compositor.presented + period : now;
}

// Every pass of the loop, before a present: the spring, the hand, the cards laid out again for a change of the list, and the veil.
static void compositor_wall_editor_tick(positive now)
{
        if (compositor_walls.unwritten && !compositor_wall_editor.wheel_held && now - compositor_walls.changed >= COMPOSITOR_WALL_SETTLE)
                compositor_walls_flush();
        if (!compositor_wall_editor.open && !compositor_wall_editor.shown && !compositor_wall_editor.veil)
                return;
        compositor_wall_editor_step(now);
        if (compositor_wall_editor.open)
        {
                compositor_wall_editor_wheel_sync(0);
                if (compositor_wall_editor.gen != compositor_walls.gen || compositor_wall_editor.laid != compositor_walls.laid)
                        compositor_wall_editor_lay(), compositor_wall_editor_damage();
                compositor_wall_editor_hover();
                compositor_wall_editor_loading(now);
                // The caret of the wheel's field blinks: its card is drawn again, and only it.
                if (compositor_wall_editor.wheel_busy && compositor_wall_editor.cards > COMPOSITOR_WALL_COLOUR_CARD)
                {
                        const int *card = compositor_wall_editor.card[COMPOSITOR_WALL_COLOUR_CARD];

                        compositor_wall_editor.wheel_drawn = 0, compositor_damage(card[0], card[1], card[2], card[3]);
                }
        }
        compositor_wall_veil_keep();
}
