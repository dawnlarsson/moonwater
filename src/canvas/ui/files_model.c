/*
        The file browser's model: what a window that shows a folder knows, and nothing of how it is drawn. A folder's
        listing read in bounded steps, its entries (name, kind, size, time), their order, a selection, the path and the
        way back and forward, and what went wrong as a state a view can say. No kit type is named here and nothing is
        drawn: the view (files.c) reads this through the functions below, and a check runs it with no window at all.

        The disk is not trusted, and neither is the time between two looks at it:

            A name is bytes. It is kept as the kernel gave it and is never read as text here; a view that shows one
            writes it through the kit's rule for words from outside (ui_label_foreign).

            Nothing is followed. An entry is looked at with AT_SYMLINK_NOFOLLOW, so a link is a link (FILES_KIND_LINK)
            with its own size and time and never what it points at, and a folder found in a listing is opened by
            file_walk_open_found, the tree's own open for an entry a walk found: it looks without following, opens
            with O_NOFOLLOW and holds the two to one inode, so a name swapped for a link between the listing and the
            press is refused and not entered. Only a path a person named (files_model_go) is opened as named.

            A listing is old the moment it is read. files_model_check looks at an entry again, through the folder's
            own descriptor and without following, and says whether it is the file that was listed (the same inode, the
            same device, the same kind), another one under its name, or gone; files_model_enter acts only on the first.
            files_model_stale says whether the folder itself changed, or is gone, since its listing began.

        Reading is in steps (files_model_step): a step takes at most the entries it is given and returns, so a folder of
        any size is read between a window's events and never in one of them. A listing holds FILES_MOST entries and
        FILES_NAMES bytes of their names; a folder with more is cut there and says so (cut), and is not an error. A
        folder read again (files_model_reload) is read beside the listing shown and takes its place when it is whole,
        so what is on the screen is never half of one.

        The walk is the tree's (file_walk_open, file_walk_open_found, file_walk_next, file_walk_close: src/sh/file.c) and
        the look is file_look_code; no call to the kernel is made here that the tree does not already make there.
*/
#ifndef MOONWATER_FILES_MODEL
#define MOONWATER_FILES_MODEL

#define FILES_PATH 4096              // a path's bytes with its end, the kernel's PATH_MAX
#define FILES_NAME 256               // a name's bytes with its end, the kernel's NAME_MAX and one
#ifndef FILES_MOST
#define FILES_MOST 65536u            // the entries a listing holds: a folder with more is cut there
#endif
#define FILES_NAMES (FILES_MOST * 24u) // and the bytes of their names: a listing whose names fill it is cut there too
#define FILES_HISTORY 32             // the folders back and the folders forward that are kept, each
#define FILES_NO 0xffffffffu         // no entry: no cursor, no place

// What an entry is, as it was looked at without following it.
enum
{
        FILES_KIND_DIRECTORY,
        FILES_KIND_FILE,
        FILES_KIND_LINK,  // a symbolic link: shown as one, never followed
        FILES_KIND_OTHER, // a device, a pipe, a socket
};

// What a folder's listing is.
enum
{
        FILES_NONE,          // no folder yet
        FILES_READING,       // being read: the entries so far are shown, in the order the disk gave them
        FILES_READY,         // read whole (or cut), in order
        FILES_UNREADABLE,    // it may not be read
        FILES_GONE,          // it is not there, or no longer what the path names
        FILES_NOT_DIRECTORY, // the path names something that is no folder
        FILES_FAILED,        // the kernel said something else: error has its code
};

enum
{
        FILES_SORT_NAME,
        FILES_SORT_SIZE,
        FILES_SORT_TIME,
        FILES_SORT_KIND,
};

// What a look at a listed entry now says (files_model_check), and why an entry was not entered (files_model_enter).
enum
{
        FILES_SAME,       // the file that was listed
        FILES_CHANGED,    // another file under its name: another inode, another device or another kind
        FILES_MISSING,    // nothing under its name
        FILES_REFUSED,    // not a folder (a link is not followed), a path too long, or no such place in the listing
};

// What a folder has become since its listing began (files_model_stale).
enum
{
        FILES_FRESH,
        FILES_STALE,   // its entries changed: read it again
        FILES_LOST,    // it is gone, or the path names another folder now: the state is FILES_GONE
};

enum
{
        FILES_CHOOSE_ONLY,   // this entry and no other
        FILES_CHOOSE_TOGGLE, // this entry in or out, the others as they are
        FILES_CHOOSE_RANGE,  // from the anchor (the entry last chosen alone or toggled) to this one
};

struct files_entry
{
        p32 name;      // where its name starts in the listing's names
        p16 length;    // the name's bytes
        p8 kind;       // FILES_KIND_*
        p8 looked;     // its size, time and inode are the kernel's: 0 when the look failed and its kind is the directory record's
        p64 size;
        b64 modified;  // seconds since 1970
        p64 inode;
        p32 device_major, device_minor;
        b64 born;      // when it was made, where the disk says (0 where it does not): an inode number is used again, a birth is not
        p32 born_part; // and that moment's nanoseconds
};

struct files_listing
{
        struct files_entry address_to entry;
        p8 address_to names;     // each name's bytes and an end after it
        p32 address_to order;    // the entries in the order they are shown: a place is an index into this
        p32 count, names_used;
        b32 cut;                 // the folder has more than is held
};

struct files_model
{
        p8 path[FILES_PATH];     // the folder shown: absolute, no slash at its end but the root's
        int state;               // FILES_*
        bipolar error;           // the kernel's code when state is FILES_FAILED (negative), else 0
        struct files_listing shown, next; // next: the folder read again, till it is whole
        b32 reloading;
        bipolar handle;          // the folder shown, open: every look at an entry goes through it. -1 none
        file_walk reader;        // the walk of the reading under way
        b32 walking;
        file_facts folder;       // the folder as it was when the reading began
        int sort;                // FILES_SORT_*
        b32 descending;
        b32 hidden;              // names that start with a dot are listed too
        p32 cursor;              // the place the keys are on, FILES_NO none
        p32 anchor;              // the entry (not the place) a range is chosen from, FILES_NO none
        p8 address_to chosen;    // a bit an entry (not a place: an order change keeps it)
        p32 chosen_count;
        p8 came[FILES_NAME];     // the name the cursor is put on when the listing is whole: the folder come up out of, the one it was on
        p8 address_to back, address_to forward; // FILES_HISTORY paths each, the last the nearest
        p32 back_count, forward_count;
        p32 revision;            // counted up when what a view shows of it changed
        address_any mapped;
        positive mapped_size;
};

/* ---- the memory ---- */

static positive files_model_bytes(void)
{
        return 2 * ((positive)FILES_MOST * sizeof(struct files_entry) + FILES_NAMES + (positive)FILES_MOST * sizeof(p32)) + FILES_MOST / 8 +
               2 * (positive)FILES_HISTORY * FILES_PATH;
}

// The model's memory: one anonymous mapping, which the kernel gives a page at a time as a listing grows. False when there is none.
static b32 files_model_open(struct files_model address_to m)
{
        positive size = files_model_bytes();
        bipolar mapped = (bipolar)system_call_6(syscall(mmap), 0, size, 3, 0x22, (positive)-1, 0);
        p8 address_to at;

        memory_fill(m, 0, sizeof(*m));
        m->handle = -1, m->reader.handle = -1, m->cursor = FILES_NO, m->anchor = FILES_NO;
        if (mapped < 0 && mapped > -4096)
                return false;
        m->mapped = (address_any)mapped, m->mapped_size = size;
        at = (p8 address_to)mapped;
        for (int which = 0; which < 2; which++)
        {
                struct files_listing address_to l = which ? &m->next : &m->shown;

                l->entry = (struct files_entry address_to)at, at += (positive)FILES_MOST * sizeof(struct files_entry);
                l->order = (p32 address_to)at, at += (positive)FILES_MOST * sizeof(p32);
                l->names = at, at += FILES_NAMES;
        }
        m->chosen = at, at += FILES_MOST / 8;
        m->back = at, at += (positive)FILES_HISTORY * FILES_PATH;
        m->forward = at;
        return true;
}

static fn files_model_close(struct files_model address_to m)
{
        if (m->walking)
                file_walk_close(&m->reader);
        if (m->handle >= 0)
                system_close(m->handle);
        if (m->mapped)
                system_call_2(syscall(munmap), (positive)m->mapped, m->mapped_size);
        memory_fill(m, 0, sizeof(*m));
        m->handle = -1, m->reader.handle = -1;
}

/* ---- the entries ---- */

static inline p32 files_model_count(const struct files_model address_to m)
{
        return m->shown.count;
}

// The entry at a place of the order shown, or null.
static const struct files_entry address_to files_model_entry(const struct files_model address_to m, p32 place)
{
        return place < m->shown.count ? &m->shown.entry[m->shown.order[place]] : null;
}

// An entry's name: its bytes as the disk has them, with an end after them. Not text: a view writes it through the kit's rule.
static inline const p8 address_to files_model_name(const struct files_model address_to m, const struct files_entry address_to entry)
{
        return m->shown.names + entry->name;
}

static p8 files_kind_of(p32 mode)
{
        switch (mode & MODE_FORMAT)
        {
        case MODE_DIRECTORY: return FILES_KIND_DIRECTORY;
        case MODE_LINK: return FILES_KIND_LINK;
        case MODE_FILE: return FILES_KIND_FILE;
        }
        return FILES_KIND_OTHER;
}

// The kind a directory record says, for an entry the look failed on.
static p8 files_kind_of_record(p8 type)
{
        return type == DT_DIR ? FILES_KIND_DIRECTORY : type == DT_LNK ? FILES_KIND_LINK : type == DT_REG ? FILES_KIND_FILE : FILES_KIND_OTHER;
}

/* ---- the order ---- */

// Two names in order: by their bytes with ASCII's capitals as its small letters, then by their bytes as they are, so no two names are
// equal. Both compares are the library's own (string_compare_folded, string_compare: the hardware floor's on every machine), since
// this is what a sort of a long listing runs most; a name ends at its end in the listing's names, as they want.
static int files_name_order(const p8 address_to a, const p8 address_to b)
{
        b32 folded = string_compare_folded((string_address)a, (string_address)b);

        if (!folded)
                folded = string_compare((string_address)a, (string_address)b);
        return folded < 0 ? -1 : folded > 0;
}

struct files_order
{
        const struct files_listing address_to listing;
        int sort;
        b32 descending;
};

// Folders first whatever the order is, then the key (turned over when descending), then the name, which settles every tie.
static b32 files_order_compare(address_any left, address_any right, address_any context)
{
        const struct files_order address_to o = (const struct files_order address_to)context;
        const struct files_entry address_to a = &o->listing->entry[*(const p32 address_to)left];
        const struct files_entry address_to b = &o->listing->entry[*(const p32 address_to)right];
        int key = 0;

        if ((a->kind == FILES_KIND_DIRECTORY) != (b->kind == FILES_KIND_DIRECTORY))
                return a->kind == FILES_KIND_DIRECTORY ? -1 : 1;
        if (o->sort == FILES_SORT_SIZE && a->size != b->size)
                key = a->size < b->size ? -1 : 1;
        else if (o->sort == FILES_SORT_TIME && a->modified != b->modified)
                key = a->modified < b->modified ? -1 : 1;
        else if (o->sort == FILES_SORT_KIND && a->kind != b->kind)
                key = a->kind < b->kind ? -1 : 1;
        if (!key)
                key = files_name_order(o->listing->names + a->name, o->listing->names + b->name);
        return o->descending ? -key : key;
}

static fn files_listing_sort(struct files_listing address_to l, int sort, b32 descending)
{
        struct files_order order = {l, sort, descending};

        for (p32 at = 0; at < l->count; at++)
                l->order[at] = at;
        if (l->count > 1)
                qsort_r(l->order, l->count, sizeof(p32), files_order_compare, &order);
}

// The place of the entry with this name in the listing shown, FILES_NO for none.
static p32 files_model_find(const struct files_model address_to m, const p8 address_to name)
{
        positive length = string_length((string_address)name);

        for (p32 place = 0; place < m->shown.count; place++)
        {
                const struct files_entry address_to entry = &m->shown.entry[m->shown.order[place]];

                if (entry->length == length && !memory_compare((address_any)(m->shown.names + entry->name), (address_any)name, length))
                        return place;
        }
        return FILES_NO;
}

/* ---- the selection ---- */

static inline b32 files_model_chosen(const struct files_model address_to m, p32 place)
{
        p32 entry = place < m->shown.count ? m->shown.order[place] : FILES_NO;

        return entry != FILES_NO && (m->chosen[entry >> 3] >> (entry & 7) & 1);
}

static fn files_model_choose_none(struct files_model address_to m)
{
        if (m->chosen_count)
                memory_fill(m->chosen, 0, FILES_MOST / 8), m->revision++;
        m->chosen_count = 0, m->anchor = FILES_NO;
}

static fn files_chosen_put(struct files_model address_to m, p32 entry, b32 on)
{
        p8 bit = (p8)(1u << (entry & 7));

        if (!(m->chosen[entry >> 3] & bit) == !on)
                return;
        m->chosen[entry >> 3] ^= bit;
        m->chosen_count += on ? 1 : (p32)-1;
}

// The cursor to a place, and what is chosen by going there (FILES_CHOOSE_*). A place past the listing is none.
static fn files_model_choose(struct files_model address_to m, p32 place, int how)
{
        p32 entry;

        if (place >= m->shown.count)
                return;
        entry = m->shown.order[place];
        if (how == FILES_CHOOSE_RANGE && m->anchor != FILES_NO)
        {
                p32 from = FILES_NO, low, high;

                for (p32 at = 0; at < m->shown.count && from == FILES_NO; at++)
                        from = m->shown.order[at] == m->anchor ? at : FILES_NO;
                from = from == FILES_NO ? place : from;
                low = from < place ? from : place, high = from < place ? place : from;
                memory_fill(m->chosen, 0, FILES_MOST / 8), m->chosen_count = 0;
                for (p32 at = low; at <= high; at++)
                        files_chosen_put(m, m->shown.order[at], true);
        }
        else if (how == FILES_CHOOSE_TOGGLE)
        {
                files_chosen_put(m, entry, !(m->chosen[entry >> 3] >> (entry & 7) & 1));
                m->anchor = entry;
        }
        else
        {
                memory_fill(m->chosen, 0, FILES_MOST / 8), m->chosen_count = 0;
                files_chosen_put(m, entry, true);
                m->anchor = entry;
        }
        m->cursor = place;
        m->revision++;
}

// The cursor moved by so many places and held at the listing's ends; nothing chosen changes. True when it moved.
static b32 files_model_move(struct files_model address_to m, bipolar by)
{
        bipolar to;

        if (!m->shown.count)
                return false;
        to = (m->cursor == FILES_NO ? (by > 0 ? -1 : (bipolar)m->shown.count) : (bipolar)m->cursor) + by;
        to = to < 0 ? 0 : to >= (bipolar)m->shown.count ? (bipolar)m->shown.count - 1 : to;
        if ((p32)to == m->cursor)
                return false;
        m->cursor = (p32)to, m->revision++;
        return true;
}

// Another order: the entries are the same, so what is chosen stays chosen and the cursor stays on its entry.
static fn files_model_sort(struct files_model address_to m, int sort, b32 descending)
{
        p32 on = m->cursor < m->shown.count ? m->shown.order[m->cursor] : FILES_NO;

        m->sort = sort, m->descending = descending;
        if (m->state != FILES_READY)
                return; // a listing still being read is put in order when it is whole
        files_listing_sort(&m->shown, sort, descending);
        for (p32 place = 0; place < m->shown.count && on != FILES_NO; place++)
                if (m->shown.order[place] == on)
                {
                        m->cursor = place;
                        break;
                }
        m->revision++;
}

/* ---- reading ---- */

static int files_state_of(bipolar error)
{
        return error == -ERROR_ACCESS || error == -1 ? FILES_UNREADABLE // EACCES, EPERM
               : error == -ERROR_NO_ENTRY || error == -116 ? FILES_GONE  // ENOENT, ESTALE
               : error == -ERROR_NOT_DIRECTORY || error == -40 ? FILES_NOT_DIRECTORY // ENOTDIR, ELOOP: a link where a folder was found
               : FILES_FAILED;
}

static fn files_listing_empty(struct files_listing address_to l)
{
        l->count = 0, l->names_used = 0, l->cut = false;
}

// One record of the walk into a listing: false when the listing is full (it is cut there).
static b32 files_listing_take(struct files_model address_to m, struct files_listing address_to l, const struct linux_dirent64 address_to record)
{
        positive length = string_length((string_address)record->d_name);
        struct files_entry address_to entry;
        file_facts facts;
        bipolar looked;

        if (file_is_dot((string_address)record->d_name)) // itself, and the folder it is in
                return true;
        if (record->d_name[0] == '.' && !m->hidden)
                return true;
        if (!length || length >= FILES_NAME)
                return true; // no name the kernel gives, and none a path could hold
        // Looked at as it is, never as what it points at. One that is gone already was never listed.
        looked = file_look_code(m->reader.handle, (string_address)record->d_name, AT_SYMLINK_NOFOLLOW, &facts);
        if (looked == -ERROR_NO_ENTRY)
                return true;
        if (l->count >= FILES_MOST || l->names_used + length + 1 > FILES_NAMES)
        {
                l->cut = true;
                return false;
        }
        entry = &l->entry[l->count];
        memory_fill(entry, 0, sizeof(*entry));
        entry->name = l->names_used, entry->length = (p16)length;
        memory_copy(l->names + l->names_used, record->d_name, length + 1);
        l->names_used += (p32)length + 1;
        if (looked == 0)
        {
                entry->kind = files_kind_of(facts.mode), entry->looked = 1;
                entry->size = facts.size, entry->modified = facts.modified.seconds, entry->inode = facts.inode;
                entry->device_major = facts.device_major, entry->device_minor = facts.device_minor;
                entry->born = facts.created.seconds, entry->born_part = facts.created.nanoseconds;
        }
        else
                entry->kind = files_kind_of_record(record->d_type), entry->inode = record->d_ino;
        l->order[l->count] = l->count; // as the disk gave them, till the listing is whole
        l->count++;
        return true;
}

// The reading is over: the listing put in order, a folder read again put in the place of the one shown, the cursor on the name kept for it.
static fn files_model_finish(struct files_model address_to m, bipolar error)
{
        struct files_listing address_to l = m->reloading ? &m->next : &m->shown;

        file_walk_close(&m->reader);
        m->walking = false;
        if (error < 0 && !l->count)
        {
                // Nothing of it could be read: the state says why, and a folder that was shown is shown no more.
                files_listing_empty(&m->shown);
                files_model_choose_none(m);
                m->state = files_state_of(error), m->error = m->state == FILES_FAILED ? error : 0;
                m->reloading = false, m->cursor = FILES_NO;
                m->revision++;
                return;
        }
        files_listing_sort(l, m->sort, m->descending);
        if (m->reloading)
        {
                struct files_listing was = m->shown;

                m->shown = m->next, m->next = was;
                files_listing_empty(&m->next);
                m->reloading = false;
                // The entries are other entries now: what was chosen of the old ones is not carried to them.
                files_model_choose_none(m);
        }
        m->state = FILES_READY, m->error = 0;
        m->cursor = m->came[0] ? files_model_find(m, m->came) : FILES_NO;
        if (m->cursor == FILES_NO && m->shown.count)
                m->cursor = 0;
        m->came[0] = end;
        m->revision++;
}

/*
        One step of the reading: at most `most` records of the folder are taken, each looked at once, and the step is over. True
        while there is more to read, so a caller with a window steps between its events and one with none steps till this is false.
*/
static b32 files_model_step(struct files_model address_to m, positive most)
{
        struct files_listing address_to l = m->reloading ? &m->next : &m->shown;

        if (!m->walking)
                return false;
        for (positive taken = 0; taken < most; taken++)
        {
                struct linux_dirent64 address_to record = file_walk_next(&m->reader);

                if (!record || !files_listing_take(m, l, record))
                {
                        files_model_finish(m, record ? 0 : m->reader.error);
                        return false;
                }
        }
        if (!m->reloading)
                m->revision++; // the entries so far are shown
        return true;
}

// The reading of the folder at m->handle begun: a walk of its own beside the descriptor the looks go through, and the folder as it is now.
static b32 files_model_begin(struct files_model address_to m)
{
        if (m->walking)
                file_walk_close(&m->reader);
        m->walking = false;
        if (file_look_code(m->handle, (string_address) "", AT_EMPTY_PATH, &m->folder) < 0)
                memory_fill(&m->folder, 0, sizeof(m->folder));
        if (!file_walk_open(&m->reader, m->handle, (string_address) "."))
                return false;
        m->walking = true;
        return true;
}

// The folder shown is this one from now on: its descriptor (taken over; -1 with an error for one that did not open) and its reading begun.
static fn files_model_show(struct files_model address_to m, string_address path, bipolar handle, bipolar error)
{
        if (m->walking)
                file_walk_close(&m->reader);
        m->walking = false, m->reloading = false;
        if (m->handle >= 0)
                system_close(m->handle);
        m->handle = handle;
        if ((const p8 address_to)path != m->path)
                string_copy_bounded(m->path, path, FILES_PATH);
        files_listing_empty(&m->shown), files_listing_empty(&m->next);
        files_model_choose_none(m);
        m->cursor = FILES_NO;
        m->state = FILES_READING, m->error = 0;
        if (handle < 0 || !files_model_begin(m))
        {
                bipolar why = handle < 0 ? error : m->reader.error;

                m->state = files_state_of(why), m->error = m->state == FILES_FAILED ? why : 0;
        }
        m->revision++;
}

// The folder shown read again, beside its listing: what is shown stays till the new one is whole (files_model_finish).
static fn files_model_reload(struct files_model address_to m)
{
        const struct files_entry address_to on = files_model_entry(m, m->cursor);

        if (m->handle < 0)
        {
                // A folder that never opened is tried again as the path names it now.
                file_walk opened;

                file_walk_open(&opened, AT_FDCWD, m->path);
                files_model_show(m, m->path, opened.handle, opened.error);
                return;
        }
        if (on && !m->came[0])
                string_copy_bounded(m->came, (string_address)files_model_name(m, on), FILES_NAME);
        files_listing_empty(&m->next);
        // One still being read for the first time starts again in place; one that is whole is read beside itself.
        m->reloading = m->state == FILES_READY;
        if (!m->reloading)
                files_listing_empty(&m->shown), m->state = FILES_READING;
        if (!files_model_begin(m))
        {
                m->reloading = false;
                files_listing_empty(&m->shown);
                m->state = files_state_of(m->reader.error), m->error = m->state == FILES_FAILED ? m->reader.error : 0;
        }
        m->revision++;
}

/*
        Whether the folder shown is still what was listed. FILES_LOST: it has no name left (removed), or the path names another
        folder or none now (renamed, replaced), and the state is FILES_GONE. FILES_STALE: it is the same folder and something in
        it was added, removed or renamed since the reading began (its own times moved). FILES_FRESH otherwise.
*/
static int files_model_stale(struct files_model address_to m)
{
        file_facts now, named;

        if (m->handle < 0 || m->state == FILES_GONE)
                return m->state == FILES_GONE ? FILES_LOST : FILES_FRESH;
        if (file_look_code(m->handle, (string_address) "", AT_EMPTY_PATH, &now) < 0 || !now.hard_links ||
            file_look_code(AT_FDCWD, m->path, 0, &named) < 0 || named.inode != now.inode || named.device_major != now.device_major ||
            named.device_minor != now.device_minor)
        {
                if (m->walking)
                        file_walk_close(&m->reader);
                m->walking = false, m->reloading = false;
                files_listing_empty(&m->shown);
                files_model_choose_none(m);
                m->cursor = FILES_NO, m->state = FILES_GONE, m->error = 0;
                m->revision++;
                return FILES_LOST;
        }
        if (now.modified.seconds != m->folder.modified.seconds || now.modified.nanoseconds != m->folder.modified.nanoseconds ||
            now.changed.seconds != m->folder.changed.seconds || now.changed.nanoseconds != m->folder.changed.nanoseconds)
                return FILES_STALE;
        return FILES_FRESH;
}

/* ---- acting on what was listed ---- */

/*
        A listed entry looked at again, now, before anything is done with it: through the folder's descriptor, by its name, not
        followed. FILES_SAME when it is the file that was listed (its kind, its inode, its device and, where the disk keeps one, its birth),
        and its size and time are then the ones it has now; FILES_CHANGED when another file has its name; FILES_MISSING when none has. now, when given, is what was seen.
*/
static int files_model_check(struct files_model address_to m, p32 place, file_facts address_to now)
{
        struct files_entry address_to entry = place < m->shown.count ? &m->shown.entry[m->shown.order[place]] : null;
        file_facts facts;
        bipolar looked;

        if (!entry || m->handle < 0)
                return FILES_REFUSED;
        looked = file_look_code(m->handle, (string_address)(m->shown.names + entry->name), AT_SYMLINK_NOFOLLOW, &facts);
        if (now)
                *now = facts;
        if (looked < 0)
                return looked == -ERROR_NO_ENTRY || looked == -ERROR_NOT_DIRECTORY ? FILES_MISSING : FILES_CHANGED;
        if (files_kind_of(facts.mode) != entry->kind ||
            (entry->looked && (facts.inode != entry->inode || facts.device_major != entry->device_major || facts.device_minor != entry->device_minor ||
                               facts.created.seconds != entry->born || facts.created.nanoseconds != entry->born_part)))
                return FILES_CHANGED;
        entry->size = facts.size, entry->modified = facts.modified.seconds;
        return FILES_SAME;
}

/* ---- the path, and the way back ---- */

// A path as the model keeps one: absolute, and no slash at its end but the root's. False for one that is not absolute or does not fit.
static b32 files_path_take(p8 address_to into, string_address path)
{
        positive length = path ? string_length(path) : 0;

        if (!length || path[0] != '/' || length >= FILES_PATH)
                return false;
        while (length > 1 && path[length - 1] == '/')
                length--;
        memory_copy(into, path, length);
        into[length] = end;
        return true;
}

// The folder a path is in, by its bytes (the last name taken off: no link is read to say it), and that last name. False at the root.
static b32 files_path_parent(const p8 address_to path, p8 address_to parent, p8 address_to name)
{
        positive length = string_length((string_address)path), cut = length;

        if (length <= 1)
                return false;
        while (cut > 0 && path[cut - 1] != '/')
                cut--;
        if (name)
                string_copy_bounded(name, (string_address)(path + cut), FILES_NAME);
        cut = cut > 1 ? cut - 1 : 1;
        memory_copy(parent, path, cut);
        parent[cut] = end;
        return true;
}

static fn files_history_push(p8 address_to stack, p32 address_to count, const p8 address_to path)
{
        if (*count == FILES_HISTORY)
        {
                for (p32 at = 0; at + 1 < FILES_HISTORY; at++) // the oldest is let go
                        memory_copy(stack + (positive)at * FILES_PATH, stack + (positive)(at + 1) * FILES_PATH, FILES_PATH);
                (*count)--;
        }
        string_copy_bounded(stack + (positive)*count * FILES_PATH, (string_address)path, FILES_PATH);
        (*count)++;
}

// To a folder a person named: opened as named (a link in the path is followed, which is what naming one means). The folder shown
// goes on the way back and the way forward is forgotten. False for a path that is not absolute or too long, and nothing changes.
static b32 files_model_go(struct files_model address_to m, string_address path)
{
        p8 taken[FILES_PATH];
        file_walk opened;

        if (!files_path_take(taken, path))
                return false;
        if (m->state != FILES_NONE)
                files_history_push(m->back, &m->back_count, m->path);
        m->forward_count = 0;
        m->came[0] = end;
        file_walk_open(&opened, AT_FDCWD, taken);
        files_model_show(m, taken, opened.handle, opened.error);
        return true;
}

// Into the folder at a place of the listing: only one that is, now, the folder that was listed (files_model_check), opened without
// following and held to that inode. FILES_SAME when it is the folder shown from now on; else why not, and the folder shown stays.
static int files_model_enter(struct files_model address_to m, p32 place)
{
        const struct files_entry address_to entry = files_model_entry(m, place);
        p8 into[FILES_PATH];
        positive length, name_length;
        file_walk opened;
        int seen;

        if (!entry)
                return FILES_REFUSED;
        if ((seen = files_model_check(m, place, null)) != FILES_SAME)
                return seen;
        if (entry->kind != FILES_KIND_DIRECTORY)
                return FILES_REFUSED;
        length = string_length(m->path), name_length = entry->length;
        if (length + 1 + name_length >= FILES_PATH)
                return FILES_REFUSED;
        memory_copy(into, m->path, length);
        if (length > 1)
                into[length++] = '/';
        memory_copy(into + length, files_model_name(m, entry), name_length + 1);
        // Between the look above and this open the name may have been given to something else: the open looks again, does not
        // follow, and is refused unless what it opened is what it looked at.
        if (!file_walk_open_found(&opened, m->handle, (string_address)files_model_name(m, entry)))
                return opened.error == -ERROR_NO_ENTRY ? FILES_MISSING : FILES_CHANGED;
        files_history_push(m->back, &m->back_count, m->path);
        m->forward_count = 0;
        m->came[0] = end;
        files_model_show(m, into, opened.handle, 0);
        return FILES_SAME;
}

// To the folder this one is in, with the cursor on the folder come out of. False at the root.
static b32 files_model_up(struct files_model address_to m)
{
        p8 parent[FILES_PATH], name[FILES_NAME];
        file_walk opened;

        if (m->state == FILES_NONE || !files_path_parent(m->path, parent, name))
                return false;
        files_history_push(m->back, &m->back_count, m->path);
        m->forward_count = 0;
        file_walk_open(&opened, AT_FDCWD, parent);
        files_model_show(m, parent, opened.handle, opened.error);
        string_copy_bounded(m->came, name, FILES_NAME);
        return true;
}

// Back to the folder shown before this one (forward: to the one gone back from). False when there is none.
static b32 files_model_back(struct files_model address_to m, b32 forward)
{
        p8 address_to from = forward ? m->forward : m->back, address_to onto = forward ? m->back : m->forward;
        p32 address_to from_count = forward ? &m->forward_count : &m->back_count, address_to onto_count = forward ? &m->back_count : &m->forward_count;
        p8 to[FILES_PATH], name[FILES_NAME], parent[FILES_PATH];
        file_walk opened;

        if (!*from_count)
                return false;
        (*from_count)--;
        string_copy_bounded(to, (string_address)(from + (positive)*from_count * FILES_PATH), FILES_PATH);
        files_history_push(onto, onto_count, m->path);
        // Back out of a folder into the one it is in puts the cursor on it, as going up does.
        name[0] = end;
        if (!files_path_parent(m->path, parent, name) || !string_equals(parent, to))
                name[0] = end;
        file_walk_open(&opened, AT_FDCWD, to);
        files_model_show(m, to, opened.handle, opened.error);
        string_copy_bounded(m->came, name, FILES_NAME);
        return true;
}

#endif // MOONWATER_FILES_MODEL
