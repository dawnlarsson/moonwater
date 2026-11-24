/*
        src/sh/host/apps.c: the verb apps: the programs of the bowls, where they come from, and the package managers they
        come through.

                moonwater apps                          the apps installed: each one's source, its id and its name
                moonwater apps search TEXT [SOURCE]     what every source that is set up offers for TEXT, or one source
                moonwater apps install SOURCE NAME      an app or a package from a source (its bowl set up first)
                moonwater apps remove SOURCE NAME       and gone again: a package's name, or an installed app's id
                moonwater apps update [SOURCE]          every source brought up to date, or one
                moonwater apps sources                  the sources: each one's state, its bowl, the bowl's size
                moonwater apps source setup BOWL        a bowl set up (its distribution downloaded and checked), or flatpak
                moonwater apps source remove BOWL yes   a whole bowl gone, everything installed in it with it; flatpak, its
                                                        profile and its apps, Arch's bowl kept

        A source is a package manager of a bowl (pacman in Arch's, apk in Alpine's, apt in Debian's, dnf in Fedora's) or
        Flatpak, whose installation is in Arch's bowl (the flatpak profile). Every install, removal and update runs in the
        bowl's isolated view, as every package manager does; a search runs where its source keeps what it knows.

        The lines a list says are a source, an id or a name, a version, a state and the rest, each a field of its own
        between tabs, so the software window (and a script) reads them as they are, and a terminal shows them in columns.

        Snap's store is named and said unavailable: snapd needs systemd and AppArmor, which this system has not.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#define APPS_LINE 2048

struct apps_source
{
        string_address name;    // the word the verb takes
        string_address distro;  // the bowl it is in (bowl_find_distro), or null for one that cannot be had here
        string_address label;   // what it is
        string_address manager; // the program in the bowl whose presence says the source is ready
        bool packages;          // its packages are searched, installed and removed here (Nix's bowl is set up and taken away only)
};

static const struct apps_source apps_sources[] = {
    {"flatpak", "arch", "Flatpak apps from Flathub", "/usr/bin/flatpak", true},
    {"arch", "arch", "Arch Linux packages (pacman)", "/usr/bin/pacman", true},
    {"alpine", "alpine", "Alpine packages (apk)", "/sbin/apk", true},
    {"debian", "debian", "Debian packages (apt)", "/usr/bin/apt-get", true},
    {"fedora", "fedora", "Fedora packages (dnf)", "/usr/bin/dnf", true},
    {"nix", "nix", "Nix's bowl: its packages with nix-env in it", "/nix/.reginfo", false},
    {"snap", null, "Snap packages from the Snap Store: unavailable, snapd needs systemd and AppArmor", null, false},
};

static const struct apps_source address_to apps_source_find(string_address name)
{
        for (positive at = 0; at < array_count(apps_sources); at++)
                if (string_equals(apps_sources[at].name, name))
                        return apps_sources + at;
        return null;
}

// The source's bowl, or null when it has none here.
static const struct bowl_distro address_to apps_bowl(const struct apps_source address_to source)
{
        return source && source->distro ? bowl_find_distro(source->distro) : null;
}

// Whether a source can be used now: its bowl is there and holds its manager.
static bool apps_ready(const struct apps_source address_to source)
{
        const struct bowl_distro address_to distro = apps_bowl(source);

        return distro && system_access_at(AT_FDCWD, distro->root, 0) >= 0 && bowl_has(distro->root, source->manager);
}

/*
        A program of a bowl run with its output read a line at a time: in the isolated view (a package manager's own
        databases are in the bowl's /var) or a session's (Flatpak's, read-only there). What it says on its error output
        goes where the verb's does.
*/
typedef fn (*apps_each)(string_address line, address_any context);

static b32 apps_read(string_address root, string_address address_to argv, bool isolated, apps_each each, address_any context)
{
        b32 pipe_ends[2];
        p8 buffer[APPS_LINE];
        positive held = 0;
        positive status = 0;
        bipolar child;

        if (isolated && bowl_configure(root))
                return 1;
        if (system_pipe(pipe_ends, O_CLOEXEC) < 0)
                return host_refuse("no pipe for %s\n", argv[0]);
        log_flush();
        child = system_fork();
        if (child < 0)
                return system_close(pipe_ends[0]), system_close(pipe_ends[1]), host_refuse("cannot start %s\n", argv[0]);
        if (child == 0)
        {
                system_close(pipe_ends[0]);
                if (system_descriptor_install(pipe_ends[1], 1) < 0)
                        exit(1);
                system_close(pipe_ends[1]);
                bowl_own_exposure = true;
                exit(bowl_launch(root, argv[0], argv, isolated));
        }
        system_close(pipe_ends[1]);
        for (;;)
        {
                bipolar got = system_read_once((positive)pipe_ends[0], buffer + held, sizeof(buffer) - 1 - held);
                positive start = 0;

                if (got <= 0)
                        break;
                held += (positive)got;
                for (positive at = 0; at < held; at++)
                        if (buffer[at] == '\n')
                        {
                                buffer[at] = end;
                                each((string_address)buffer + start, context);
                                start = at + 1;
                        }
                memmove(buffer, buffer + start, held - start);
                held -= start;
                // A line longer than the buffer is said in pieces rather than lost.
                if (held == sizeof(buffer) - 1)
                        buffer[held] = end, each((string_address)buffer, context), held = 0;
        }
        if (held)
                buffer[held] = end, each((string_address)buffer, context);
        system_close(pipe_ends[0]);
        if (system_wait4_retry(child, address_of status, 0, null) < 0)
                return 1;
        return wait_status_code(status) ? 1 : 0;
}

// A program of a bowl run in its isolated view, what it says going where the verb's does: an install, a removal, an update.
static b32 apps_run(const struct bowl_distro address_to distro, string_address address_to argv, string_address what)
{
        return bowl_setup_isolated(distro->root, argv[0], argv, what, null);
}

/*
        Text that grows as it is read, whole: a file of a bowl, or what a program says. It starts with a newline and every
        line ends with one, so a line is found as "\nLINE\n" anywhere in it.
*/
struct apps_text
{
        p8 address_to bytes;
        positive used, room;
};

static bool apps_text_add(struct apps_text address_to text, const p8 address_to from, positive length)
{
        if (text->used + length + 2 > text->room)
        {
                positive room = text->room ? text->room : 65536;
                p8 address_to grown;

                while (text->used + length + 2 > room)
                        room *= 2;
                if (!(grown = (p8 address_to)memory_checked(room)))
                        return false;
                if (text->bytes)
                        memory_copy(grown, text->bytes, text->used), memory_free(text->bytes, text->room);
                text->bytes = grown, text->room = room;
        }
        if (!text->used)
                text->bytes[text->used++] = '\n';
        memory_copy(text->bytes + text->used, from, length);
        text->used += length;
        text->bytes[text->used] = end;
        return true;
}

static fn apps_text_free(struct apps_text address_to text)
{
        if (text->bytes)
                memory_free(text->bytes, text->room);
        memory_zero(text, sizeof(*text));
}

static fn apps_text_line(string_address line, address_any context)
{
        apps_text_add(context, (const p8 address_to)line, string_length(line)) && apps_text_add(context, (const p8 address_to) "\n", 1);
}

static fn apps_text_file(struct apps_text address_to text, string_address root, string_address path)
{
        p8 full[BOWL_PATH_LIMIT];
        p8 chunk[16384];
        bipolar handle, got;

        if (!path_join(full, sizeof(full), root, path) || (handle = system_open_at(AT_FDCWD, (string_address)full, FILE_READ | O_CLOEXEC | O_NOFOLLOW)) < 0)
                return;
        while ((got = system_read_retry((positive)handle, chunk, sizeof(chunk))) > 0 && apps_text_add(text, chunk, (positive)got))
                ;
        system_close(handle);
}

/* ---- search ---- */

struct apps_found
{
        const struct apps_source address_to source;
        string_address root;
        struct apps_text known; // what the bowl's manager keeps of what is installed (Alpine's, Debian's, Fedora's)
        positive count;
        p8 name[256];    // arch's two-line results: the first line's name and version, kept for the second's description
        p8 version[96];
        bool installed;
};

// Whether a package (or a Flatpak app) of this name is installed in the bowl, read from what its manager keeps.
static bool apps_installed(struct apps_found address_to found, string_address name, positive length)
{
        p8 key[320];
        string_address source = found->source->name;
        string_address before = string_equals(source, "alpine") ? (string_address) "\nP:" : string_equals(source, "debian") ? (string_address) "\nPackage: " : (string_address) "\n";
        positive at;
        string_address hit, stanza_end, status;

        if (!length || length > 256)
                return false;
        if (string_equals(source, "flatpak"))
        {
                // app/ID/current is the branch made current, which only an install that finished makes.
                at = string_copy_bounded((string_address)key, "/var/lib/flatpak/app/", sizeof(key));
                memory_copy(key + at, name, length), at += length;
                string_copy_bounded((string_address)key + at, "/current", sizeof(key) - at);
                return bowl_has(found->root, (string_address)key);
        }
        if (!found->known.used)
                return false;
        at = string_copy_bounded((string_address)key, before, sizeof(key));
        memory_copy(key + at, name, length), at += length;
        key[at++] = '\n', key[at] = end;
        hit = string_search((string_address)found->known.bytes, (string_address)key);
        if (!hit || !string_equals(source, "debian"))
                return hit != null;
        // dpkg's status keeps a package removed but configured too: its stanza says installed in its Status line.
        stanza_end = string_search(hit + 1, "\n\n");
        status = string_search(hit, "\nStatus: install ok installed");
        return status && (!stanza_end || status < stanza_end);
}

static fn apps_say(const struct apps_source address_to source, string_address name, positive name_length, string_address version,
                   positive version_length, bool installed, string_address summary)
{
        p8 n[256], v[96];

        name_length = name_length < sizeof(n) - 1 ? name_length : sizeof(n) - 1;
        version_length = version_length < sizeof(v) - 1 ? version_length : sizeof(v) - 1;
        memory_copy(n, name, name_length), n[name_length] = end;
        memory_copy(v, version, version_length), v[version_length] = end;
        while (*summary == ' ' || *summary == '\t')
                summary++;
        string_format(log, "%s\t%s\t%s\t%s\t%s\n", source->name, (string_address)n, v[0] ? (string_address)v : (string_address) "-",
                      installed ? (string_address) "installed" : (string_address) "-", summary);
}

static positive apps_word(string_address line)
{
        positive at = 0;

        while (line[at] && line[at] != ' ' && line[at] != '\t')
                at++;
        return at;
}

// flatpak search --columns=application,version,name,description: four fields between tabs.
static fn apps_found_flatpak(string_address line, address_any context)
{
        struct apps_found address_to found = context;
        string_address field[4];
        positive fields = 0;
        string_address at = line;

        if (!line[0] || string_has_prefix(line, "No matches"))
                return;
        field[fields++] = at;
        for (; *at && fields < 4; at++)
                if (*at == '\t')
                        field[fields++] = at + 1;
        if (fields < 4)
                return;
        found->count++;
        apps_say(found->source, field[0], (positive)(field[1] - field[0] - 1), field[1], (positive)(field[2] - field[1] - 1),
                 apps_installed(found, field[0], (positive)(field[1] - field[0] - 1)), field[3]);
}

// pacman -Ss: "repo/name version [installed]", then "    description".
static fn apps_found_arch(string_address line, address_any context)
{
        struct apps_found address_to found = context;

        if (line[0] == ' ' || line[0] == '\t')
        {
                if (found->name[0])
                        found->count++, apps_say(found->source, (string_address)found->name, string_length((string_address)found->name),
                                                 (string_address)found->version, string_length((string_address)found->version), found->installed, line);
                found->name[0] = end;
                return;
        }
        {
                string_address slash = string_first_of(line, '/');
                string_address name = slash ? slash + 1 : line;
                positive name_length = apps_word(name);
                string_address version = name + name_length;
                positive version_length;

                while (*version == ' ')
                        version++;
                version_length = apps_word(version);
                name_length = name_length < sizeof(found->name) - 1 ? name_length : sizeof(found->name) - 1;
                version_length = version_length < sizeof(found->version) - 1 ? version_length : sizeof(found->version) - 1;
                memory_copy(found->name, name, name_length), found->name[name_length] = end;
                memory_copy(found->version, version, version_length), found->version[version_length] = end;
                found->installed = string_search(line, "[installed") != null;
        }
}

// apk search -v -d: "name-version - description"; the version starts at the first dash a digit follows.
static fn apps_found_alpine(string_address line, address_any context)
{
        struct apps_found address_to found = context;
        positive word = apps_word(line), dash = 0;
        string_address rest = line + word;

        if (!word)
                return;
        for (positive at = 0; at + 1 < word; at++)
                if (line[at] == '-' && line[at + 1] >= '0' && line[at + 1] <= '9')
                {
                        dash = at;
                        break;
                }
        while (*rest == ' ' || *rest == '-')
                rest++;
        found->count++;
        apps_say(found->source, line, dash ? dash : word, dash ? line + dash + 1 : (string_address) "", dash ? word - dash - 1 : 0,
                 apps_installed(found, line, dash ? dash : word), rest);
}

// apt-cache search: "name - description".
static fn apps_found_debian(string_address line, address_any context)
{
        struct apps_found address_to found = context;
        positive word = apps_word(line);
        string_address rest = line + word;

        if (!word)
                return;
        while (*rest == ' ' || *rest == '-')
                rest++;
        found->count++;
        apps_say(found->source, line, word, "", 0, apps_installed(found, line, word), rest);
}

// dnf search: " name.arch\tdescription" (dnf5) or "name.arch : description", between headings that end with a colon.
static fn apps_found_fedora(string_address line, address_any context)
{
        struct apps_found address_to found = context;
        positive word, length;
        string_address rest;

        while (*line == ' ')
                line++;
        length = string_length(line);
        if (!length || line[length - 1] == ':' || string_has_prefix(line, "Matched") || string_has_prefix(line, "Updating") ||
            string_has_prefix(line, "Repositories") || string_has_prefix(line, "Last metadata") || string_has_prefix(line, "="))
                return;
        word = apps_word(line);
        rest = line + word;
        while (*rest == ' ' || *rest == '\t' || *rest == ':')
                rest++;
        // The architecture after the last dot is the machine's, not the name's.
        {
                positive dot = word;

                while (dot > 0 && line[dot - 1] != '.')
                        dot--;
                if (dot > 1)
                        word = dot - 1;
        }
        found->count++;
        apps_say(found->source, line, word, "", 0, apps_installed(found, line, word), rest);
}

static b32 apps_search_one(const struct apps_source address_to source, string_address text)
{
        const struct bowl_distro address_to distro = apps_bowl(source);
        struct apps_found found;
        string_address argv[8];
        apps_each each;
        bool isolated = true;
        b32 failed;

        memory_zero(address_of found, sizeof(found));
        found.source = source, found.root = distro->root;
        if (string_equals(source->name, "alpine"))
                apps_text_file(address_of found.known, distro->root, "lib/apk/db/installed");
        else if (string_equals(source->name, "debian"))
                apps_text_file(address_of found.known, distro->root, "var/lib/dpkg/status");
        else if (string_equals(source->name, "fedora"))
        {
                string_address names[] = {"/usr/bin/rpm", "-qa", "--qf", "%{NAME}\n", null};

                (void)apps_read(distro->root, names, true, apps_text_line, address_of found.known);
        }
        if (string_equals(source->name, "flatpak"))
        {
                string_address words[] = {"/usr/bin/flatpak", "search", "--columns=application,version,name,description", text, null};

                memory_copy(argv, words, sizeof(words)), each = apps_found_flatpak, isolated = false;
        }
        else if (string_equals(source->name, "arch"))
        {
                string_address words[] = {"/usr/bin/pacman", "-Ss", text, null};

                memory_copy(argv, words, sizeof(words)), each = apps_found_arch;
        }
        else if (string_equals(source->name, "alpine"))
        {
                string_address words[] = {"/sbin/apk", "search", "-v", "-d", text, null};

                memory_copy(argv, words, sizeof(words)), each = apps_found_alpine;
        }
        else if (string_equals(source->name, "debian"))
        {
                string_address words[] = {"/usr/bin/apt-cache", "search", text, null};

                memory_copy(argv, words, sizeof(words)), each = apps_found_debian;
        }
        else if (string_equals(source->name, "fedora"))
        {
                string_address words[] = {"/usr/bin/dnf", "search", "-q", text, null};

                memory_copy(argv, words, sizeof(words)), each = apps_found_fedora;
        }
        else
                return apps_text_free(address_of found.known), 0;
        failed = apps_read(distro->root, argv, isolated, each, address_of found);
        apps_text_free(address_of found.known);
        return failed;
}

/* ---- the apps installed ---- */

// A desktop entry's Name= (the first, unlocalised) and whether it is one to show: an application, not hidden.
static bool apps_entry(string_address path, p8 address_to name, positive room)
{
        p8 text[16384];
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC | O_NONBLOCK);
        bipolar got;
        bool application = false, shown = true, in_entry = false;

        name[0] = end;
        if (handle < 0)
                return false;
        got = system_read_retry((positive)handle, text, sizeof(text) - 1);
        system_close(handle);
        if (got <= 0)
                return false;
        text[got] = end;
        for (string_address line = (string_address)text; line && *line;)
        {
                string_address next = string_first_of(line, '\n');
                positive length = next ? (positive)(next - line) : string_length(line);

                if (line[0] == '[')
                        in_entry = length >= 15 && !memory_compare(line, "[Desktop Entry]", 15);
                else if (in_entry && length > 5 && !memory_compare(line, "Name=", 5) && !name[0])
                {
                        positive n = length - 5 < room - 1 ? length - 5 : room - 1;

                        memory_copy(name, line + 5, n), name[n] = end;
                }
                else if (in_entry && length >= 16 && !memory_compare(line, "Type=Application", 16))
                        application = true;
                else if (in_entry && ((length >= 14 && !memory_compare(line, "NoDisplay=true", 14)) ||
                                      (length >= 11 && !memory_compare(line, "Hidden=true", 11))))
                        shown = false;
                line = next ? next + 1 : null;
        }
        return application && shown && name[0];
}

static fn apps_list_folder(string_address source, string_address root, string_address folder)
{
        p8 directory[BOWL_PATH_LIMIT];
        file_walk walk;
        struct linux_dirent64 address_to entry;

        if (!path_join(directory, sizeof(directory), root, folder) || !file_walk_open(address_of walk, AT_FDCWD, directory))
                return;
        while ((entry = file_walk_next(address_of walk)))
        {
                p8 path[BOWL_PATH_LIMIT], name[256];
                positive length = string_length(entry->d_name);

                if (length <= 8 || !memory_has_suffix(entry->d_name, length, ".desktop") || entry->d_name[0] == '.' ||
                    !path_join(path, sizeof(path), directory, entry->d_name) || !apps_entry((string_address)path, name, sizeof(name)))
                        continue;
                entry->d_name[length - 8] = end;
                string_format(log, "%s\t%s\t-\tinstalled\t%s\t%s\n", source, entry->d_name, (string_address)name, (string_address)path);
        }
        file_walk_close(address_of walk);
}

// Every bowl's apps: its own entries, and those of its Flatpak installation.
static b32 apps_list(void)
{
        for (positive at = 0; at < array_count(apps_sources); at++)
        {
                const struct apps_source address_to source = apps_sources + at;
                const struct bowl_distro address_to distro = apps_bowl(source);

                if (!source->packages || !distro || system_access_at(AT_FDCWD, distro->root, 0) < 0)
                        continue;
                if (string_equals(source->name, "flatpak"))
                        apps_list_folder("flatpak", distro->root, "var/lib/flatpak/exports/share/applications");
                else
                {
                        apps_list_folder(source->name, distro->root, "usr/share/applications");
                        apps_list_folder(source->name, distro->root, "usr/local/share/applications");
                }
        }
        log_flush();
        return 0;
}

/* ---- install, remove, update ---- */

// A name a package manager is given as a word of its own: letters, digits and . _ + - @ :, and never an option.
static bool apps_name_ok(string_address name)
{
        positive length = string_length(name);

        if (!length || length > 200 || name[0] == '-' || name[0] == '.')
                return false;
        for (positive at = 0; at < length; at++)
                if (!((name[at] >= 'a' && name[at] <= 'z') || (name[at] >= 'A' && name[at] <= 'Z') || (name[at] >= '0' && name[at] <= '9') ||
                      name[at] == '.' || name[at] == '_' || name[at] == '+' || name[at] == '-' || name[at] == '@' || name[at] == ':'))
                        return false;
        return true;
}

// The source made ready: its bowl set up, and for Flatpak the flatpak profile installed.
static b32 apps_make_ready(const struct apps_source address_to source)
{
        const struct bowl_distro address_to distro = apps_bowl(source);
        b32 failed;

        if (!distro)
                return host_refuse("%s cannot be had here\n", source->name);
        if (apps_ready(source))
                return 0;
        if (string_equals(source->name, "flatpak"))
        {
                const struct bowl_profile address_to profile = bowl_find_profile("flatpak");

                return profile ? bowl_profile_install(profile) : host_refuse("this build has no flatpak profile\n");
        }
        failed = bowl_setup_distro(distro);
        return failed ? failed : (apps_ready(source) ? 0 : host_refuse("%s's bowl has no %s\n", source->name, source->manager));
}

// The package an installed app's entry belongs to: the manager asked who owns the file.
struct apps_owner
{
        p8 name[256];
        string_address source;
};

static fn apps_owner_line(string_address line, address_any context)
{
        struct apps_owner address_to owner = context;
        string_address at = line;
        positive length;

        if (owner->name[0] || !line[0])
                return;
        // dpkg -S: "package: /path"; apk info --who-owns: "/path is owned by package-version"; pacman -Qqo and rpm -qf: the name.
        if (string_equals(owner->source, "debian"))
        {
                string_address colon = string_first_of(line, ':');

                if (!colon)
                        return;
                length = (positive)(colon - line);
        }
        else if (string_equals(owner->source, "alpine"))
        {
                string_address by = string_search(line, " is owned by ");

                if (!by)
                        return;
                at = by + 13, length = apps_word(at);
                for (positive dash = 0; dash + 1 < length; dash++)
                        if (at[dash] == '-' && at[dash + 1] >= '0' && at[dash + 1] <= '9')
                        {
                                length = dash;
                                break;
                        }
        }
        else
                length = apps_word(line);
        length = length < sizeof(owner->name) - 1 ? length : sizeof(owner->name) - 1;
        memory_copy(owner->name, at, length), owner->name[length] = end;
}

static b32 apps_owner_of(const struct apps_source address_to source, string_address id, p8 address_to into, positive room)
{
        const struct bowl_distro address_to distro = apps_bowl(source);
        struct apps_owner owner;
        p8 file[BOWL_PATH_LIMIT];
        string_address argv[6];

        memory_zero(address_of owner, sizeof(owner));
        owner.source = source->name;
        string_copy_bounded((string_address)file, "/usr/share/applications/", sizeof(file));
        string_append_bounded((string_address)file, id, sizeof(file));
        if (string_append_bounded((string_address)file, ".desktop", sizeof(file)) >= sizeof(file) || !bowl_has(distro->root, (string_address)file))
                return 1;
        if (string_equals(source->name, "arch"))
        {
                string_address words[] = {"/usr/bin/pacman", "-Qqo", (string_address)file, null};
                memory_copy(argv, words, sizeof(words));
        }
        else if (string_equals(source->name, "alpine"))
        {
                string_address words[] = {"/sbin/apk", "info", "--who-owns", (string_address)file, null};
                memory_copy(argv, words, sizeof(words));
        }
        else if (string_equals(source->name, "debian"))
        {
                string_address words[] = {"/usr/bin/dpkg", "-S", (string_address)file, null};
                memory_copy(argv, words, sizeof(words));
        }
        else if (string_equals(source->name, "fedora"))
        {
                string_address words[] = {"/usr/bin/rpm", "-qf", "--qf", "%{NAME}\n", (string_address)file, null};
                memory_copy(argv, words, sizeof(words));
        }
        else
                return 1;
        if (apps_read(distro->root, argv, true, apps_owner_line, address_of owner) || !owner.name[0])
                return 1;
        string_copy_bounded(into, owner.name, room);
        return 0;
}

static b32 apps_install(const struct apps_source address_to source, string_address name)
{
        const struct bowl_distro address_to distro = apps_bowl(source);
        string_address argv[BOWL_PROFILE_ARGV];
        positive used = 0;
        b32 failed = apps_make_ready(source);

        if (failed)
                return failed;
        if (string_equals(source->name, "flatpak"))
        {
                string_address words[] = {"/usr/bin/flatpak", "install", "--system", "-y", "--noninteractive", "flathub", name, null};

                return apps_run(distro, words, "flatpak could not install it\n");
        }
        while (distro->install[used] && used + 2 < BOWL_PROFILE_ARGV)
                argv[used] = distro->install[used], used++;
        argv[used++] = name, argv[used] = null;
        return apps_run(distro, argv, "the package manager could not install it\n");
}

static b32 apps_remove(const struct apps_source address_to source, string_address name)
{
        const struct bowl_distro address_to distro = apps_bowl(source);
        string_address argv[BOWL_PROFILE_ARGV];
        p8 package[256];
        positive used = 0;

        if (!apps_ready(source))
                return host_refuse("%s is not set up: nothing of it is installed\n", source->name);
        if (string_equals(source->name, "flatpak"))
        {
                string_address words[] = {"/usr/bin/flatpak", "uninstall", "--system", "-y", "--noninteractive", name, null};

                return apps_run(distro, words, "flatpak could not uninstall it\n");
        }
        // An installed app's id (its entry's name) is the package that owns its entry; anything else is a package's name.
        if (apps_owner_of(source, name, package, sizeof(package)))
                string_copy_bounded(package, name, sizeof(package));
        while (distro->remove[used] && used + 2 < BOWL_PROFILE_ARGV)
                argv[used] = distro->remove[used], used++;
        argv[used++] = (string_address)package, argv[used] = null;
        return apps_run(distro, argv, "the package manager could not remove it\n");
}

static b32 apps_update_one(const struct apps_source address_to source)
{
        const struct bowl_distro address_to distro = apps_bowl(source);

        if (!source->packages || !apps_ready(source))
                return 0;
        host_say(log, host_label "%s: updating\n", source->name);
        if (string_equals(source->name, "flatpak"))
        {
                string_address words[] = {"/usr/bin/flatpak", "update", "--system", "-y", "--noninteractive", null};
                string_address stream[] = {"/usr/bin/flatpak", "update", "--system", "--appstream", null};
                b32 failed = apps_run(distro, words, "flatpak could not update\n");

                return failed ? failed : apps_run(distro, stream, "flatpak could not fetch the catalogue\n");
        }
        if (string_equals(source->name, "arch"))
        {
                string_address words[] = {"/usr/bin/pacman", "-Syu", "--noconfirm", null};
                return apps_run(distro, words, "pacman could not update\n");
        }
        if (string_equals(source->name, "alpine"))
        {
                string_address words[] = {"/sbin/apk", "upgrade", "-U", null};
                return apps_run(distro, words, "apk could not update\n");
        }
        if (string_equals(source->name, "debian"))
        {
                string_address lists[] = {"/usr/bin/apt-get", "update", null};
                string_address words[] = {"/usr/bin/apt-get", "upgrade", "-y", null};
                b32 failed = apps_run(distro, lists, "apt could not read its lists\n");

                return failed ? failed : apps_run(distro, words, "apt could not update\n");
        }
        if (string_equals(source->name, "fedora"))
        {
                string_address words[] = {"/usr/bin/dnf", "upgrade", "-y", null};
                return apps_run(distro, words, "dnf could not update\n");
        }
        return 0;
}

/* ---- the sources and their bowls ---- */

// A bowl's size on its disk, in KiB, as du counts it on its own file system (0 when it cannot be said).
static fn apps_size_line(string_address line, address_any context)
{
        p64 address_to kib = context;
        positive used = 0;

        if (!*kib)
                *kib = string_digits_max(line, 15, address_of used);
}

static p64 apps_bowl_kib(string_address root)
{
        p64 kib = 0;
        b32 pipe_ends[2];
        p8 text[256];
        bipolar child, got;
        positive status = 0;

        if (system_pipe(pipe_ends, O_CLOEXEC) < 0)
                return 0;
        child = system_fork();
        if (child == 0)
        {
                string_address argv[] = {"/bin/du", "-sxk", root, null};

                system_close(pipe_ends[0]);
                if (system_descriptor_install(pipe_ends[1], 1) < 0)
                        exit(1);
                (void)shell_exec_file(argv[0], argv, 3, file_environment_all());
                exit(127);
        }
        system_close(pipe_ends[1]);
        got = child > 0 ? system_read_retry((positive)pipe_ends[0], text, sizeof(text) - 1) : -1;
        system_close(pipe_ends[0]);
        if (child > 0)
                (void)system_wait4_retry(child, address_of status, 0, null);
        if (got > 0)
                text[got] = end, apps_size_line((string_address)text, address_of kib);
        return kib;
}

static fn apps_other_bowls(void);

static b32 apps_sources_say(void)
{
        for (positive at = 0; at < array_count(apps_sources); at++)
        {
                const struct apps_source address_to source = apps_sources + at;
                const struct bowl_distro address_to distro = apps_bowl(source);
                bool there = distro && system_access_at(AT_FDCWD, distro->root, 0) >= 0;
                string_address state = !source->distro    ? (string_address) "unavailable"
                                       : apps_ready(source) ? (string_address) "ready"
                                       : there              ? (string_address) "not-installed"
                                                            : (string_address) "not-set-up";

                string_format(log, "%s\t%s\t%s\t", source->name, state, source->distro ? source->distro : (string_address) "-");
                if (there && !string_equals(source->name, "flatpak"))
                        string_format(log, "%p\t%s\n", (positive)apps_bowl_kib(distro->root), source->label);
                else
                        string_format(log, "-\t%s\n", source->label);
                log_flush();
        }
        apps_other_bowls();
        return 0;
}

// A folder of /bowls that is no distribution's here: a bowl made by hand, or by an older build.
static bool apps_other_bowl(string_address name)
{
        if (!bowl_name(name, false) || name[0] == '.' || string_equals(name, "bin") || bowl_find_distro(name))
                return false;
        for (positive at = 0; at < array_count(bowl_distros); at++)
                if (string_has_prefix(bowl_distros[at].store, BOWL_ROOT_PREFIX) && string_equals(bowl_distros[at].store + sizeof(BOWL_ROOT_PREFIX) - 1, name))
                        return false;
        return true;
}

// Those folders, said after the sources so they can be seen, and taken away with source remove.
static fn apps_other_bowls(void)
{
        file_walk walk;
        struct linux_dirent64 address_to entry;

        if (!file_walk_open(address_of walk, AT_FDCWD, BOWL_ROOT_DIRECTORY))
                return;
        while ((entry = file_walk_next(address_of walk)))
        {
                p8 root[BOWL_PATH_LIMIT];
                file_facts facts;

                if (!apps_other_bowl(entry->d_name) || !path_join(root, sizeof(root), BOWL_ROOT_DIRECTORY, entry->d_name) ||
                    file_look_code(AT_FDCWD, (string_address)root, AT_SYMLINK_NOFOLLOW, address_of facts) < 0 || (facts.mode & 0170000u) != 0040000u)
                        continue;
                string_format(log, "%s\tother\t%s\t%p\ta folder of %s no source here sets up\n", entry->d_name, entry->d_name,
                              (positive)apps_bowl_kib((string_address)root), BOWL_ROOT_DIRECTORY);
                log_flush();
        }
        file_walk_close(address_of walk);
}

// The launchers a bowl's exposure left in /bowls/bin, taken away with the bowl (each only where it names that bowl).
static fn apps_unexpose(string_address root)
{
        file_walk walk;
        struct linux_dirent64 address_to entry;

        if (!file_walk_open(address_of walk, AT_FDCWD, BOWL_EXPOSE_DIRECTORY))
                return;
        while ((entry = file_walk_next(address_of walk)))
                if (entry->d_name[0] != '.')
                        bowl_profile_drop(root, entry->d_name);
        file_walk_close(address_of walk);
}

static b32 apps_bowl_remove(string_address name, bool yes)
{
        const struct bowl_distro address_to distro = bowl_find_distro(name);
        file_facts facts;
        p8 root[BOWL_PATH_LIMIT];

        if (!distro && !apps_other_bowl(name))
                return host_refuse("no bowl is called %s\n", name);
        // The root is /bowls/NAME spelled out, NAME a word of its own: never a path built from words that could be empty.
        string_copy_bounded((string_address)root, BOWL_ROOT_PREFIX, sizeof(root));
        string_append_bounded((string_address)root, name, sizeof(root));
        if (!bowl_name(name, false) || string_equals(name, "bin") || (distro && !string_equals((string_address)root, distro->root)))
                return host_refuse("%s's bowl is not where a bowl is\n", name);
        if (file_look_code(AT_FDCWD, (string_address)root, AT_SYMLINK_NOFOLLOW, address_of facts) < 0)
                return host_refuse("%s is not set up\n", name);
        if ((facts.mode & 0170000u) != 0040000u)
                return host_refuse("%s is not a directory\n", (string_address)root);
        if (!yes)
                return host_refuse("this takes away %s and everything installed in it (its apps, its profiles, their data in it): "
                                   "moonwater apps source remove %s yes\n",
                                   (string_address)root, name);
        host_say(log, host_label "taking away %s\n", (string_address)root);
        apps_unexpose((string_address)root);
        {
                string_address argv[] = {"/bin/rm", "-rf", "--one-file-system", "--", (string_address)root, null};
                b32 failed = host_run(argv);

                if (failed)
                        return failed;
        }
        host_say(log, host_label "%s is gone\n", name);
        return 0;
}

static b32 apps_bowl_setup(string_address name)
{
        const struct bowl_distro address_to distro = bowl_find_distro(name);

        // Flatpak is a profile of Arch's bowl, not a bowl: set up is its install (the bowl first, where it is not there).
        if (string_equals(name, "flatpak"))
                return apps_make_ready(apps_source_find("flatpak"));
        return distro ? bowl_setup_distro(distro) : host_refuse("no bowl is called %s\n", name);
}

// Flatpak's removal: its profile's (flatpak, its Flathub remote and every Flatpak app with it), Arch's bowl left as it is.
static b32 apps_flatpak_remove(bool yes)
{
        const struct bowl_profile address_to profile = bowl_find_profile("flatpak");

        if (!profile)
                return host_refuse("this build has no flatpak profile\n");
        if (!apps_ready(apps_source_find("flatpak")))
                return host_refuse("flatpak is not set up\n");
        if (!yes)
                return host_refuse("this takes away Flatpak and every Flatpak app with it: moonwater apps source remove flatpak yes\n");
        return bowl_profile_remove(profile);
}

/* ---- the verb ---- */

static b32 host_apps(string_address address_to arguments, positive count)
{
        string_address sub = count >= 3 ? arguments[2] : (string_address) "list";
        const struct apps_source address_to source = null;

        if (count == 2 || (count == 3 && string_equals(sub, "list")))
                return apps_list();
        if (string_equals(sub, "sources") && count == 3)
                return apps_sources_say();
        if (string_equals(sub, "search") && (count == 4 || count == 5))
        {
                b32 failed = 0;

                if (count == 5 && !(source = apps_source_find(arguments[4])))
                        return host_refuse("no source is called %s\n", arguments[4]);
                host_need_root();
                for (positive at = 0; at < array_count(apps_sources); at++)
                        if ((count == 4 || apps_sources + at == source) && apps_sources[at].packages && apps_ready(apps_sources + at))
                                failed |= apps_search_one(apps_sources + at, arguments[3]);
                log_flush();
                return failed;
        }
        if ((string_equals(sub, "install") || string_equals(sub, "remove")) && count == 5)
        {
                if (!(source = apps_source_find(arguments[3])))
                        return host_refuse("no source is called %s: flatpak, arch, alpine, debian, fedora\n", arguments[3]);
                if (!source->packages)
                        return host_refuse("%s\n", source->label);
                if (!apps_name_ok(arguments[4]))
                        return host_refuse("%s is not a name a package has\n", arguments[4]);
                host_need_root();
                return string_equals(sub, "install") ? apps_install(source, arguments[4]) : apps_remove(source, arguments[4]);
        }
        if (string_equals(sub, "update") && (count == 3 || count == 4))
        {
                b32 failed = 0;

                if (count == 4 && !(source = apps_source_find(arguments[3])))
                        return host_refuse("no source is called %s\n", arguments[3]);
                host_need_root();
                for (positive at = 0; at < array_count(apps_sources); at++)
                        if (count == 3 || apps_sources + at == source)
                                failed |= apps_update_one(apps_sources + at);
                return failed;
        }
        if (string_equals(sub, "source") && count == 5 && string_equals(arguments[3], "setup"))
        {
                host_need_root();
                return apps_bowl_setup(arguments[4]);
        }
        if (string_equals(sub, "source") && (count == 5 || count == 6) && string_equals(arguments[3], "remove"))
        {
                host_need_root();
                if (string_equals(arguments[4], "flatpak"))
                        return apps_flatpak_remove(count == 6 && string_equals(arguments[5], HOST_YES));
                return apps_bowl_remove(arguments[4], count == 6 && string_equals(arguments[5], HOST_YES));
        }
        return host_usage();
}

/* ---- open and defaults: the open tool's (src/canvas/ui/open.c), what opens what ---- */

// The open tool run with words, standard output and error this verb's: what it says is what the verb says.
static b32 apps_open_tool(string_address address_to words, positive count)
{
        string_address argv[count + 2];

        argv[0] = (string_address) "/open";
        for (positive at = 0; at < count; at++)
                argv[at + 1] = words[at];
        argv[count + 1] = null;
        log_flush();
        return host_run(argv);
}

/*
        moonwater open is asked of the desktop through the OpenURI portal, as any program asks it: on the machine and from inside a bowl
        alike (a bowl's xdg-open and every other opener's name is this: bowl.c's bowl_openers), so what opens a page, a program's link or a file is the desktop's
        choice and is started as the desktop starts it. The session bus is reached as its clients reach it (DBUS_SESSION_BUS_ADDRESS,
        else Moonwater's), with EXTERNAL authentication, Hello, then OpenURI.OpenURI("", URI, {}). Where no bus answers (no desktop),
        root opens it with the open tool itself.
*/
// A message being made: its bytes, as many as the URI it carries needs (memory_checked), and how many are written.
struct apps_bus_message
{
        p8 address_to bytes;
        positive used, room;
};

static fn apps_bus_pad(struct apps_bus_message address_to m, positive align)
{
        while (m->used % align && m->used < m->room)
                m->bytes[m->used++] = 0;
}

static fn apps_bus_u32(struct apps_bus_message address_to m, p32 value)
{
        apps_bus_pad(m, 4);
        if (m->used + 4 <= m->room)
                memory_copy(m->bytes + m->used, &value, 4), m->used += 4;
}

static fn apps_bus_string(struct apps_bus_message address_to m, string_address text)
{
        positive length = string_length(text);

        apps_bus_u32(m, (p32)length);
        if (m->used + length + 1 <= m->room)
                memory_copy(m->bytes + m->used, text, length + 1), m->used += length + 1;
}

static fn apps_bus_signature(struct apps_bus_message address_to m, string_address text)
{
        positive length = string_length(text);

        if (m->used + length + 2 <= m->room)
                m->bytes[m->used++] = (p8)length, memory_copy(m->bytes + m->used, text, length + 1), m->used += length + 1;
}

static fn apps_bus_field(struct apps_bus_message address_to m, p8 code, string_address type, string_address value)
{
        apps_bus_pad(m, 8);
        if (m->used + 1 <= m->room)
                m->bytes[m->used++] = code;
        apps_bus_signature(m, type);
        type[0] == 'g' ? apps_bus_signature(m, value) : apps_bus_string(m, value);
}

// A method call: its header and its fields, the body after it (OpenURI's "ssa{sv}" where uri is given, Hello's none).
static b32 apps_bus_call(struct apps_bus_message address_to m, p32 serial, string_address destination, string_address path,
                         string_address interface, string_address member, string_address uri)
{
        positive fields_at, body_at;
        p32 length;

        m->used = 0;
        m->bytes[m->used++] = 'l', m->bytes[m->used++] = 1, m->bytes[m->used++] = 0, m->bytes[m->used++] = 1;
        apps_bus_u32(m, 0), apps_bus_u32(m, serial), apps_bus_u32(m, 0);
        fields_at = m->used;
        apps_bus_field(m, 1, "o", path);
        apps_bus_field(m, 2, "s", interface);
        apps_bus_field(m, 3, "s", member);
        apps_bus_field(m, 6, "s", destination);
        if (uri)
                apps_bus_field(m, 8, "g", "ssa{sv}");
        length = (p32)(m->used - fields_at);
        memory_copy(m->bytes + 12, &length, 4);
        apps_bus_pad(m, 8);
        body_at = m->used;
        if (uri)
        {
                apps_bus_string(m, "");
                apps_bus_string(m, uri);
                apps_bus_u32(m, 0); // the options: none
                apps_bus_pad(m, 8);  // where the empty array's dictionary entries would begin
        }
        length = (p32)(m->used - body_at);
        memory_copy(m->bytes + 4, &length, 4);
        return m->used < m->room;
}

// A reply's serial it answers, from a message's fields (REPLY_SERIAL, code 5): 0 where it has none.
static p32 apps_bus_reply_to(const p8 address_to b, positive fields)
{
        positive at = 16, stop = 16 + fields;

        while (at < stop)
        {
                p8 code, length;
                char type;

                at = (at + 7) & ~(positive)7;
                if (at + 2 > stop)
                        break;
                code = b[at], length = b[at + 1], type = (char)b[at + 2];
                at += 2 + length + 1;
                if (type == 'u' || type == 's' || type == 'o')
                {
                        p32 value;

                        at = (at + 3) & ~(positive)3;
                        memory_copy(&value, b + at, 4);
                        if (code == 5 && type == 'u')
                                return value;
                        at += 4 + (type == 'u' ? 0 : value + 1);
                }
                else if (type == 'g')
                        at += 1 + (positive)b[at] + 1;
                else
                        break;
        }
        return 0;
}

static b32 apps_bus_send(bipolar handle, const p8 address_to bytes, positive length)
{
        return system_write_all((positive)handle, bytes, length) == length;
}

// OpenURI asked for uri: 0 the desktop took it, 1 it refused it, -1 no bus answered.
static int apps_portal_ask(string_address uri, struct apps_bus_message address_to m);

static int apps_portal_open(string_address uri)
{
        struct apps_bus_message m;
        int said;

        m.used = 0, m.room = string_length(uri) + 1024;
        if (!(m.bytes = (p8 address_to)memory_checked(m.room)))
                return -1;
        said = apps_portal_ask(uri, &m);
        memory_free(m.bytes, m.room);
        return said;
}

static int apps_portal_ask(string_address uri, struct apps_bus_message address_to message)
{
        struct apps_bus_message m = *message;
        static p8 in[65536];
        string_address address = getenv("DBUS_SESSION_BUS_ADDRESS");
        struct { unsigned short family; char path[108]; } place;
        char line[160];
        positive held = 0, length;
        bipolar handle;
        positive uid = (positive)system_call(syscall(getuid));

        memory_zero(&place, sizeof(place));
        place.family = AF_UNIX;
        if (address && string_has_prefix(address, "unix:path="))
        {
                string_address from = address + 10;
                positive n = 0;

                while (from[n] && from[n] != ',' && n + 1 < sizeof(place.path))
                        place.path[n] = from[n], n++;
        }
        else
                string_copy_bounded((string_address)place.path, BOWL_SESSION_BUS_SOCKET, sizeof(place.path));
        if ((handle = system_call_3(syscall(socket), AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0)
                return -1;
        // A bus that never answers holds no program that asked it: ten seconds each way, and it is no bus.
        {
                positive limit[2] = {10, 0};

                system_call_5(syscall(setsockopt), (positive)handle, 1, 20, (positive)limit, sizeof(limit)); // SOL_SOCKET, SO_RCVTIMEO
                system_call_5(syscall(setsockopt), (positive)handle, 1, 21, (positive)limit, sizeof(limit)); // SO_SNDTIMEO
        }
        if (system_call_3(syscall(connect), (positive)handle, (positive)&place, sizeof(place)) < 0)
                return system_close(handle), -1;
        // EXTERNAL: the user's number in decimal, each digit as two hex digits.
        {
                char digits[24], hex[48];
                positive n = 0, h = 0;

                do
                        digits[n++] = (char)('0' + uid % 10), uid /= 10;
                while (uid && n < sizeof(digits));
                while (n)
                {
                        char d = digits[--n];

                        hex[h++] = "0123456789abcdef"[(p8)d >> 4], hex[h++] = "0123456789abcdef"[(p8)d & 15];
                }
                hex[h] = 0;
                string_copy_bounded((string_address)line + 1, "AUTH EXTERNAL ", sizeof(line) - 1);
                line[0] = 0;
                string_append_bounded((string_address)line + 1, (string_address)hex, sizeof(line) - 1);
                string_append_bounded((string_address)line + 1, "\r\n", sizeof(line) - 1);
                if (!apps_bus_send(handle, (const p8 address_to)line, 1 + string_length((string_address)line + 1)))
                        return system_close(handle), -1;
        }
        while (held < 2 || in[held - 2] != '\r' || in[held - 1] != '\n')
        {
                bipolar got = system_read_once((positive)handle, in + held, 255 - held);

                if (got <= 0 || held + (positive)got >= 255)
                        return system_close(handle), -1;
                held += (positive)got;
        }
        if (held < 3 || memory_compare(in, "OK ", 3) || !apps_bus_send(handle, (const p8 address_to) "BEGIN\r\n", 7))
                return system_close(handle), -1;
        if (!apps_bus_call(&m, 1, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "Hello", null) ||
            !apps_bus_send(handle, m.bytes, m.used) ||
            !apps_bus_call(&m, 2, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.OpenURI", "OpenURI",
                           uri) ||
            !apps_bus_send(handle, m.bytes, m.used))
                return system_close(handle), -1;
        // The answers, until the one to OpenURI: a return is taken, an error refused.
        for (held = 0;;)
        {
                bipolar got = held < sizeof(in) ? system_read_once((positive)handle, in + held, sizeof(in) - held) : -1;
                positive fields, body, total;
                p32 answers;

                if (got <= 0)
                        return system_close(handle), -1;
                held += (positive)got;
                while (held >= 16)
                {
                        memory_copy(&length, in + 4, 4), body = length & 0xffffffffu;
                        memory_copy(&length, in + 12, 4), fields = length & 0xffffffffu;
                        total = 16 + ((fields + 7) & ~(positive)7) + body;
                        if (in[0] != 'l' || total > sizeof(in))
                                return system_close(handle), -1;
                        if (held < total)
                                break;
                        answers = apps_bus_reply_to(in, fields);
                        if (answers == 2 && (in[1] == 2 || in[1] == 3))
                                return system_close(handle), in[1] == 2 ? 0 : 1;
                        memory_copy(in, in + total, held - total), held -= total;
                }
        }
}

/*
        A target as a URI: blanks and line ends around it taken off (a link copied with them); one with a scheme as it is; a site's name
        with none (www.example.com, example.com/path), where no file of that name is here, an https address; any other a file: URI of
        its absolute path (from the working folder), escaped.
*/
static b32 apps_open_uri(string_address given, char address_to into, positive room)
{
        positive scheme = 0, made = 0, from = 0, to = string_length(given), dots = 0;
        char path[4096];
        char address_to target;
        b32 site = true;

        while (from < to && string_first_of(" \t\r\n", given[from]))
                from++;
        while (to > from && string_first_of(" \t\r\n", given[to - 1]))
                to--;
        if (to == from || to - from + 9 > room)
                return false;
        // The trimmed target is made where the URI goes, and the URI after it where it is a file's (a path is made in path first).
        target = into;
        memory_copy(target, given + from, to - from), target[to - from] = 0;
        while ((target[scheme] >= 'a' && target[scheme] <= 'z') || (target[scheme] >= 'A' && target[scheme] <= 'Z') ||
               (scheme && ((target[scheme] >= '0' && target[scheme] <= '9') || target[scheme] == '+' || target[scheme] == '-' || target[scheme] == '.')))
                scheme++;
        if (scheme >= 2 && target[scheme] == ':')
                return true;
        for (positive at = 0; target[at] && target[at] != '/'; at++)
                dots += target[at] == '.', site &= (target[at] >= 'a' && target[at] <= 'z') || (target[at] >= 'A' && target[at] <= 'Z') ||
                                                  (target[at] >= '0' && target[at] <= '9') || string_first_of(".-:", target[at]);
        // A name whose last part is a file's extension (notes.txt, report.pdf) is a file's, not a site's.
        {
                positive end_at = 0, dot = 0;
                static const string_address files[] = {"txt", "md", "pdf", "png", "jpg", "jpeg", "gif", "svg", "webp", "html", "htm",
                                                       "zip", "gz", "xz", "tar", "mp3", "mp4", "mkv", "webm", "ogg", "flac", "wav",
                                                       "doc", "docx", "odt", "ods", "xls", "xlsx", "ppt", "pptx", "csv", "json", "xml",
                                                       "sh", "py", "c", "h", "log", "conf", "ini", "toml", "yaml", "yml", "iso", "desktop"};

                while (target[end_at] && target[end_at] != '/' && target[end_at] != '?' && target[end_at] != ':')
                        end_at++;
                for (positive at = 0; at < end_at; at++)
                        dot = target[at] == '.' ? at + 1 : dot;
                for (positive at = 0; dot && at < array_count(files); at++)
                        if (end_at - dot == string_length(files[at]) && !string_compare_max(target + dot, files[at], end_at - dot))
                                site = false;
        }
        if (site && dots && target[0] != '.' && system_access_at(AT_FDCWD, target, 0) < 0)
        {
                memory_copy(target + 8, target, to - from + 1);
                memory_copy(target, "https://", 8);
                return true;
        }
        if (string_length(target) >= sizeof(path))
                return false;
        string_copy_bounded((string_address)path, target, sizeof(path));
        target = path;
        if (target[0] != '/')
        {
                char whole[4096];

                if (!getcwd((string_address)whole, sizeof(whole)) || string_append_bounded((string_address)whole, "/", sizeof(whole)) >= sizeof(whole) ||
                    string_append_bounded((string_address)whole, path, sizeof(whole)) >= sizeof(whole))
                        return false;
                string_copy_bounded((string_address)path, (string_address)whole, sizeof(path));
        }
        if (room < 8)
                return false;
        memory_copy(into, "file://", 7), made = 7;
        for (positive at = 0; path[at]; at++)
        {
                p8 b = (p8)path[at];

                if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || string_first_of("-._~/", (p8)b))
                {
                        if (made + 1 >= room)
                                return false;
                        into[made++] = (char)b;
                }
                else
                {
                        if (made + 3 >= room)
                                return false;
                        into[made++] = '%', into[made++] = "0123456789ABCDEF"[b >> 4], into[made++] = "0123456789ABCDEF"[b & 15];
                }
        }
        into[made] = 0;
        return true;
}

// The open tool run here, as this binary (argv[0] names the tool, so no link of the image's is needed), waited for: 0 it opened it.
static b32 apps_open_here(string_address target)
{
        string_address argv[4] = {(string_address) "/open", (string_address) "--", target, null};
        positive status = 0;
        bipolar child;

        log_flush();
        if ((child = system_fork()) < 0)
                return 1;
        if (child == 0)
        {
                system_execute((string_address) "/proc/self/exe", argv, file_environment_all());
                (void)shell_exec_file(argv[0], argv, 3, file_environment_all());
                exit(127);
        }
        if (system_wait4_retry(child, address_of status, 0, null) < 0)
                return 1;
        return wait_status_code(status) ? 1 : 0;
}

/*
        moonwater open TARGET...: each opened with what opens its type (a page, a link of a program's scheme, a file, a folder). The
        desktop is asked first (the OpenURI portal); where no bus answers, root opens it here with the open tool. A program of a bowl
        that no bus answers is told so (its xdg-open then tries the distribution's own).
*/
static b32 host_open(string_address address_to arguments, positive count)
{
        b32 failed = 0;
        positive first = 2;

        if (count > first && string_equals(arguments[first], "--"))
                first++;
        if (count <= first)
                return host_usage();
        for (positive at = first; at < count; at++)
        {
                positive room = string_length(arguments[at]) * 3 + 4200;
                char address_to uri = (char address_to)memory_checked(room);
                int said;

                if (!uri || !apps_open_uri(arguments[at], uri, room))
                {
                        failed |= host_refuse("%s cannot be made an address to open\n", arguments[at]);
                        if (uri)
                                memory_free(uri, room);
                        continue;
                }
                said = apps_portal_open((string_address)uri);
                memory_free(uri, room);
                if (said == 1)
                        failed |= host_refuse("the desktop did not open %s (open -type says what would)\n", arguments[at]);
                else if (said < 0 && bowl_is_root())
                        failed |= apps_open_here(arguments[at]);
                else if (said < 0)
                        failed |= host_refuse("no desktop answers to open %s\n", arguments[at]);
        }
        return failed;
}

/*
        moonwater defaults                      each kind (browser, mail, folders, text, images, video, music, documents) and what opens it
        moonwater defaults KIND|TYPE            every program that opens it, * the one that does
        moonwater defaults KIND|TYPE APP|auto   the program that opens it, by its name from the list (alpine:chromium), or auto
*/
static b32 host_defaults(string_address address_to arguments, positive count)
{
        string_address words[3];

        if (count == 2)
                return words[0] = (string_address) "-defaults", apps_open_tool(words, 1);
        if (count == 3)
                return words[0] = (string_address) "-handlers", words[1] = arguments[2], apps_open_tool(words, 2);
        if (count == 4)
        {
                host_need_root();
                return words[0] = (string_address) "-set", words[1] = arguments[2], words[2] = arguments[3], apps_open_tool(words, 3);
        }
        return host_usage();
}
