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

        Kept listings. A folder that was read whole and is left is not forgotten at once: its listing is kept (its entries, their
        order, what was chosen, where the keys were and where a view had its rows), and going to that folder again shows it as it
        was left with no reading at all; the view then asks files_model_stale, as it does of any folder shown, and only one that
        changed is read again, beside what is shown. At most FILES_KEPT listings are kept and FILES_KEPT_BYTES bytes of them
        together; when another would pass either the one left longest ago is let go, a listing larger than the bytes alone is
        not kept, and files_keep_trim lets go of every one whose folder is not on a trail, so what is kept is the trail's. A kept
        listing is the folder's only while the path opens to the same device and inode; anything else under its path is read.
        m->reads counts the readings begun, which is how a check knows that moving along a trail began none.

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
#define FILES_KEPT 8                 // the listings kept of folders left,
#ifndef FILES_KEPT_BYTES
#define FILES_KEPT_BYTES (8u << 20)  // and the bytes of all of them together
#endif
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
        p16 mode;      // its type and permissions (st_mode), a link's those of what it names (0 when that is gone): what its symbol says it is
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
        p32 places;              // the places shown: every entry (count), or those a filter leaves (files_model_filter)
        b32 cut;                 // the folder has more than is held
};

// The listing of a folder that was left, as it was left: one mapping of its entries, their order, their names and what was chosen.
struct files_kept
{
        p8 path[FILES_PATH];     // "" for a place that keeps nothing
        address_any mapped;
        positive bytes;
        p32 count, names_used;
        b32 cut;
        file_facts folder;       // the folder as it was when the reading began: what a look for staleness compares
        int sort;
        b32 descending, hidden;
        p32 cursor, anchor, chosen_count;
        bipolar seat;
        p64 left;                // when it was left, by the model's own count: the least is the oldest
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
        p8 filter[FILES_NAME];   // only names with these bytes in them are shown (letters of either case alike), when filter_length is not 0
        positive filter_length;
        p32 cursor;              // the place the keys are on, FILES_NO none
        p32 anchor;              // the entry (not the place) a range is chosen from, FILES_NO none
        p8 address_to chosen;    // a bit an entry (not a place: an order change keeps it)
        p32 chosen_count;
        p8 came[FILES_NAME];     // the name the cursor is put on when the listing is whole: the folder come up out of, the one it was on
        p8 address_to back, address_to forward; // FILES_HISTORY paths each, the last the nearest
        p32 back_count, forward_count;
        p32 revision;            // counted up when what a view shows of it changed
        p32 reads;               // the readings of a folder begun, ever: a kept listing shown begins none
        bipolar seat;            // where a view has the rows of the folder shown (its scroll): kept with a listing and given back with it
        b32 restored;            // the folder shown came from a kept listing, as it was left, and was not read
        struct files_kept kept[FILES_KEPT];
        p64 kept_clock;
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

static fn files_keep_drop(struct files_model address_to m, int which);

static fn files_model_close(struct files_model address_to m)
{
        for (int k = 0; k < FILES_KEPT; k++)
                files_keep_drop(m, k);
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
        return m->shown.places;
}

// The entry at a place of the order shown, or null.
static const struct files_entry address_to files_model_entry(const struct files_model address_to m, p32 place)
{
        return place < m->shown.places ? &m->shown.entry[m->shown.order[place]] : null;
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

// Where a name's extension starts (after its last dot, a dot first not counting), or at its end when it has none.
static const p8 address_to files_name_extension(const p8 address_to name, positive length)
{
        positive dot = 0;

        for (positive at = 1; at < length; at++)
                if (name[at] == '.')
                        dot = at + 1;
        return name + (dot ? dot : length);
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
        else if (o->sort == FILES_SORT_KIND)
                key = files_name_order(files_name_extension(o->listing->names + a->name, a->length), files_name_extension(o->listing->names + b->name, b->length));
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
        l->places = l->count;
}

// The order kept to the entries whose names hold the filter's bytes (letters of either case alike); a filter of none keeps them all.
static fn files_listing_filter(struct files_listing address_to l, const p8 address_to filter, positive length)
{
        p32 kept = 0;

        if (!length)
                return;
        for (p32 at = 0; at < l->places; at++)
        {
                const struct files_entry address_to entry = &l->entry[l->order[at]];

                if (entry->length >= length && memory_search_ascii_case(l->names + entry->name, entry->length, filter, length))
                        l->order[kept++] = l->order[at];
        }
        l->places = kept;
}

// The place of the entry with this name in the listing shown, FILES_NO for none.
static p32 files_model_find(const struct files_model address_to m, const p8 address_to name)
{
        positive length = string_length((string_address)name);

        for (p32 place = 0; place < m->shown.places; place++)
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
        p32 entry = place < m->shown.places ? m->shown.order[place] : FILES_NO;

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

        if (place >= m->shown.places)
                return;
        entry = m->shown.order[place];
        if (how == FILES_CHOOSE_RANGE && m->anchor != FILES_NO)
        {
                p32 from = FILES_NO, low, high;

                for (p32 at = 0; at < m->shown.places && from == FILES_NO; at++)
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

        if (!m->shown.places)
                return false;
        to = (m->cursor == FILES_NO ? (by > 0 ? -1 : (bipolar)m->shown.places) : (bipolar)m->cursor) + by;
        to = to < 0 ? 0 : to >= (bipolar)m->shown.places ? (bipolar)m->shown.places - 1 : to;
        if ((p32)to == m->cursor)
                return false;
        m->cursor = (p32)to, m->revision++;
        return true;
}

/*
        The listing shown put in the order asked for and kept to the filter, the cursor on the entry it was on (none when the filter leaves
        that one out: then the first place). What is chosen is kept: it is of entries, not places.
*/
static fn files_model_order(struct files_model address_to m)
{
        p32 on = m->cursor < m->shown.places ? m->shown.order[m->cursor] : FILES_NO;

        files_listing_sort(&m->shown, m->sort, m->descending);
        files_listing_filter(&m->shown, m->filter, m->filter_length);
        m->cursor = m->shown.places ? 0 : FILES_NO;
        for (p32 place = 0; place < m->shown.places && on != FILES_NO; place++)
                if (m->shown.order[place] == on)
                {
                        m->cursor = place;
                        break;
                }
        m->revision++;
}

// Another order: the entries are the same, so what is chosen stays chosen and the cursor stays on its entry.
static fn files_model_sort(struct files_model address_to m, int sort, b32 descending)
{
        m->sort = sort, m->descending = descending;
        if (m->state != FILES_READY)
                return; // a listing still being read is put in order when it is whole
        files_model_order(m);
}

/*
        Only the entries whose names hold these bytes shown (letters of either case alike: a search within the folder), or every one
        again for none; the folder is not read again. A filter is the folder's: another folder shown has none.
*/
static fn files_model_filter(struct files_model address_to m, const p8 address_to text, positive length)
{
        length = length < FILES_NAME ? length : FILES_NAME - 1;
        if (length == m->filter_length && !memory_compare(text, m->filter, length))
                return;
        memory_copy(m->filter, text, length);
        m->filter[length] = end, m->filter_length = length;
        if (m->state == FILES_READY)
                files_model_order(m);
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
        l->count = 0, l->names_used = 0, l->cut = false, l->places = 0;
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
                entry->kind = files_kind_of(facts.mode), entry->looked = 1, entry->mode = (p16)facts.mode;
                entry->size = facts.size, entry->modified = facts.modified.seconds, entry->inode = facts.inode;
                // A link's symbol is what it names, looked at through it once; a link to nothing is a link to a plain file.
                if (entry->kind == FILES_KIND_LINK)
                {
                        file_facts named;

                        entry->mode = file_look_code(m->reader.handle, (string_address)record->d_name, 0, &named) == 0 ? (p16)named.mode : 0;
                }
                entry->device_major = facts.device_major, entry->device_minor = facts.device_minor;
                entry->born = facts.created.seconds, entry->born_part = facts.created.nanoseconds;
        }
        else
                entry->kind = files_kind_of_record(record->d_type), entry->inode = record->d_ino;
        l->order[l->count] = l->count; // as the disk gave them, till the listing is whole
        l->count++;
        l->places = l->count;
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
        files_listing_filter(l, m->filter, m->filter_length);
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
        if (m->cursor == FILES_NO && m->shown.places)
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
        m->walking = true, m->reads++;
        return true;
}

/* ---- kept listings ---- */

static fn files_keep_drop(struct files_model address_to m, int which)
{
        struct files_kept address_to k = &m->kept[which];

        if (k->mapped)
                system_call_2(syscall(munmap), (positive)k->mapped, k->bytes);
        k->mapped = null, k->bytes = 0, k->path[0] = end, k->count = 0;
}

// The place that keeps this path's listing, -1 for none.
static int files_keep_find(const struct files_model address_to m, const p8 address_to path)
{
        for (int k = 0; path[0] && k < FILES_KEPT; k++)
                if (string_equals((string_address)m->kept[k].path, (string_address)path))
                        return k;
        return -1;
}

static inline positive files_keep_bytes(p32 count, p32 names_used)
{
        return (positive)count * (sizeof(struct files_entry) + sizeof(p32)) + names_used + (count + 7) / 8;
}

// A kept listing as a listing: its entries, order and names where they lie in its mapping. Answers where its chosen bits are, after them.
static p8 address_to files_keep_listing(const struct files_kept address_to k, struct files_listing address_to l)
{
        p8 address_to at = (p8 address_to)k->mapped;

        l->entry = (struct files_entry address_to)at, at += (positive)k->count * sizeof(struct files_entry);
        l->order = (p32 address_to)at, at += (positive)k->count * sizeof(p32);
        l->names = at, at += k->names_used;
        l->count = k->count, l->names_used = k->names_used, l->cut = k->cut;
        return at;
}

// The kept listing left longest ago, other than `but`: -1 for none.
static int files_keep_oldest(const struct files_model address_to m, int but)
{
        int oldest = -1;

        for (int k = 0; k < FILES_KEPT; k++)
                if (k != but && m->kept[k].path[0] && (oldest < 0 || m->kept[k].left < m->kept[oldest].left))
                        oldest = k;
        return oldest;
}

// The folder shown is being left: its listing, if it is whole, is kept as it is. One too large to keep, or with no memory for it, is not.
static fn files_keep_put(struct files_model address_to m)
{
        positive bytes = files_keep_bytes(m->shown.count, m->shown.names_used), held = 0;
        int which = files_keep_find(m, m->path);
        struct files_kept address_to k;
        struct files_listing into;
        bipolar mapped = 0;
        p8 address_to chosen;

        if (which >= 0)
                files_keep_drop(m, which);
        if (m->state != FILES_READY || m->handle < 0 || bytes > FILES_KEPT_BYTES)
                return;
        // A listing kept to a filter is kept whole: the filter is the folder's while it is shown, and is let go with it.
        if (m->shown.places != m->shown.count)
        {
                m->filter_length = 0, m->filter[0] = end;
                files_model_order(m);
        }
        for (int at = 0; which < 0 && at < FILES_KEPT; at++)
                which = m->kept[at].path[0] ? -1 : at;
        if (which < 0)
                files_keep_drop(m, which = files_keep_oldest(m, -1));
        for (int at = 0; at < FILES_KEPT; at++)
                held += m->kept[at].bytes;
        while (held + bytes > FILES_KEPT_BYTES)
        {
                int oldest = files_keep_oldest(m, which);

                held -= m->kept[oldest].bytes;
                files_keep_drop(m, oldest);
        }
        if (bytes && (mapped = (bipolar)system_call_6(syscall(mmap), 0, bytes, 3, 0x22, (positive)-1, 0)) < 0 && mapped > -4096)
                return;
        k = &m->kept[which];
        string_copy_bounded(k->path, m->path, FILES_PATH);
        k->mapped = (address_any)mapped, k->bytes = bytes;
        k->count = m->shown.count, k->names_used = m->shown.names_used, k->cut = m->shown.cut;
        k->folder = m->folder, k->sort = m->sort, k->descending = m->descending, k->hidden = m->hidden;
        k->cursor = m->cursor, k->anchor = m->anchor, k->chosen_count = m->chosen_count, k->seat = m->seat;
        k->left = ++m->kept_clock;
        if (!bytes)
                return;
        chosen = files_keep_listing(k, &into);
        memory_copy(into.entry, m->shown.entry, (positive)k->count * sizeof(struct files_entry));
        memory_copy(into.order, m->shown.order, (positive)k->count * sizeof(p32));
        memory_copy(into.names, m->shown.names, k->names_used);
        memory_copy(chosen, m->chosen, (k->count + 7) / 8);
}

// A kept listing shown again, as it was left (in the order asked for now), and its place given up: nothing is read.
static fn files_keep_take(struct files_model address_to m, int which)
{
        struct files_kept address_to k = &m->kept[which];
        struct files_listing from;
        const p8 address_to chosen;
        p32 on;

        m->shown.count = m->shown.places = k->count, m->shown.names_used = k->names_used, m->shown.cut = k->cut;
        if (k->bytes)
        {
                chosen = files_keep_listing(k, &from);
                memory_copy(m->shown.entry, from.entry, (positive)k->count * sizeof(struct files_entry));
                memory_copy(m->shown.order, from.order, (positive)k->count * sizeof(p32));
                memory_copy(m->shown.names, from.names, k->names_used);
                memory_copy(m->chosen, chosen, (k->count + 7) / 8);
        }
        m->chosen_count = k->chosen_count, m->anchor = k->anchor, m->cursor = k->cursor < k->count ? k->cursor : FILES_NO;
        m->folder = k->folder, m->seat = k->seat;
        if (k->sort != m->sort || k->descending != m->descending)
        {
                on = m->cursor != FILES_NO ? m->shown.order[m->cursor] : FILES_NO;
                files_listing_sort(&m->shown, m->sort, m->descending);
                for (p32 place = 0; place < m->shown.count && on != FILES_NO; place++)
                        if (m->shown.order[place] == on)
                        {
                                m->cursor = place;
                                break;
                        }
        }
        files_keep_drop(m, which);
}

// The folder shown is this one from now on: its descriptor (taken over; -1 with an error for one that did not open), and its reading
// begun, unless its listing was kept and it is still that folder (the same device and inode on the descriptor): then it is shown as
// it was left and nothing is read (m->restored). The folder left, if its listing was whole, is kept.
static fn files_model_show(struct files_model address_to m, string_address path, bipolar handle, bipolar error)
{
        file_facts now;
        int kept;

        if (m->walking)
                file_walk_close(&m->reader);
        m->walking = false, m->reloading = false;
        files_keep_put(m);
        m->filter_length = 0, m->filter[0] = end;
        if (m->handle >= 0)
                system_close(m->handle);
        m->handle = handle;
        if ((const p8 address_to)path != m->path)
                string_copy_bounded(m->path, path, FILES_PATH);
        files_listing_empty(&m->shown), files_listing_empty(&m->next);
        files_model_choose_none(m);
        m->cursor = FILES_NO, m->seat = 0, m->restored = false;
        if ((kept = files_keep_find(m, m->path)) >= 0)
        {
                const struct files_kept address_to k = &m->kept[kept];

                if (handle >= 0 && k->hidden == m->hidden && file_look_code(handle, (string_address) "", AT_EMPTY_PATH, &now) >= 0 && now.inode == k->folder.inode &&
                    now.device_major == k->folder.device_major && now.device_minor == k->folder.device_minor)
                {
                        files_keep_take(m, kept);
                        m->state = FILES_READY, m->error = 0, m->restored = true, m->came[0] = end;
                        m->revision++;
                        return;
                }
                files_keep_drop(m, kept);
        }
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
        struct files_entry address_to entry = place < m->shown.places ? &m->shown.entry[m->shown.order[place]] : null;
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

/*
        To a folder a person typed, if it is one: the path is opened first, as typed (a link anywhere in it is followed, its last
        part too: typing a name is asking for what it names, which is what files_model_go's rule is), and only one that opened is
        shown, with the folder shown before it on the way back. FILES_READING when it is the folder shown from now on. Else the
        state it would have had (FILES_GONE, FILES_UNREADABLE, FILES_NOT_DIRECTORY, FILES_FAILED; FILES_NOT_DIRECTORY too for text
        that is no absolute path or does not fit one), and nothing has changed: the folder shown stays, whole, and so does the
        way back. The text is bytes with an end and is not trusted: its length is held to FILES_PATH and nothing in it is read as
        anything but a path for the kernel to resolve.
*/
static int files_model_try(struct files_model address_to m, string_address path)
{
        p8 taken[FILES_PATH];
        file_walk opened;

        if (!files_path_take(taken, path))
                return FILES_NOT_DIRECTORY;
        if (!file_walk_open(&opened, AT_FDCWD, taken))
                return files_state_of(opened.error);
        if (m->state != FILES_NONE)
                files_history_push(m->back, &m->back_count, m->path);
        m->forward_count = 0;
        m->came[0] = end;
        files_model_show(m, taken, opened.handle, 0);
        return FILES_READING;
}

/*
        The start of a folder's name finished from the listing shown: of the folders listed whose names start with the bytes given,
        what all of them go on to share is written to into (the whole name when there is one such folder), with its end. Answers
        how many folders start so; into is the bytes given when none goes further. Files are not offered: a path being typed is a
        folder's. The listing is the one in memory, so this reads no disk.
*/
static p32 files_listing_complete(const struct files_listing address_to l, const p8 address_to start, positive length, p8 address_to into, positive room)
{
        p32 found = 0;
        positive shared = 0;

        if (!room)
                return 0;
        length = length < room - 1 ? length : room - 1;
        memory_copy(into, start, length);
        into[length] = end;
        for (p32 place = 0; l && place < l->count; place++)
        {
                const struct files_entry address_to entry = &l->entry[l->order[place]];
                const p8 address_to name = l->names + entry->name;

                if (entry->kind != FILES_KIND_DIRECTORY || entry->length < length || entry->length >= room ||
                    (length && memory_compare((address_any)name, (address_any)start, length)))
                        continue;
                if (!found++)
                {
                        memory_copy(into, name, (positive)entry->length + 1);
                        shared = entry->length;
                        continue;
                }
                for (positive at = length; at < shared; at++)
                        if (at >= entry->length || into[at] != name[at])
                        {
                                shared = at;
                                break;
                        }
                into[shared] = end;
        }
        return found;
}

static p32 files_model_complete(const struct files_model address_to m, const p8 address_to start, positive length, p8 address_to into, positive room)
{
        return files_listing_complete(m->state == FILES_READY ? &m->shown : null, start, length, into, room);
}

// The same from the listing of any folder that is in memory, the one shown or one kept: none found for a folder that is neither.
static p32 files_model_complete_in(const struct files_model address_to m, const p8 address_to folder, const p8 address_to start, positive length, p8 address_to into, positive room)
{
        struct files_listing kept;
        int which = files_keep_find(m, folder);

        if (string_equals((string_address)folder, (string_address)m->path))
                return files_model_complete(m, start, length, into, room);
        if (which >= 0 && m->kept[which].bytes)
                files_keep_listing(&m->kept[which], &kept);
        return files_listing_complete(which >= 0 && m->kept[which].bytes ? &kept : null, start, length, into, room);
}

// A folder that is open already, by a descriptor that is taken over, is the folder shown: the one before it goes on the way back.
static fn files_model_take(struct files_model address_to m, string_address path, bipolar handle)
{
        if (m->state != FILES_NONE)
                files_history_push(m->back, &m->back_count, m->path);
        m->forward_count = 0;
        m->came[0] = end;
        files_model_show(m, path, handle, 0);
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

// The cursor is to be on this name (the folder come up out of): when the listing is whole, or at once in one shown as it was left,
// where everything else is as it was left.
static fn files_model_came(struct files_model address_to m, const p8 address_to name)
{
        p32 place = m->restored && name[0] ? files_model_find(m, name) : FILES_NO;

        if (!m->restored)
                string_copy_bounded(m->came, (string_address)name, FILES_NAME);
        else if (place != FILES_NO && place != m->cursor)
                m->cursor = place, m->revision++;
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
        files_model_came(m, name);
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
        files_model_came(m, name);
        return true;
}

/* ---- the trail ---- */
/*
        The trail: the deepest path a person has gone to on the branch they are on, and where on it the folder shown is. Going to a
        folder that is on the trail (one above the folder shown, or one of the deeper ones gone up out of) moves the place and leaves
        the trail as it is, so up three folders and down again is two moves and nothing is typed. Going to a folder that is not on it
        (another branch, opened from a listing or typed) makes the trail that folder's path: what was deeper than the fork is dropped
        and the new folder is its end. files_trail_note is that rule, told the path shown after every move, however the move was made
        (a crumb, Up, Back and Forward, a row opened, a path typed): the way back and forward keeps paths, as it did, and the trail is
        worked out from the path gone to by the same rule, so the two cannot disagree.

        A folder of the trail is remembered by what it was, its device and inode, from when it was shown. Before a deeper one is gone
        back to, files_trail_check looks at its path again without following: FILES_SAME for the folder it was, FILES_MISSING for a
        path that names nothing now, FILES_CHANGED for anything else there (another folder, a file, a link put in its place). Only
        the first is gone to; the others are for a view to say, and files_trail_cut then drops the trail from that folder on.
*/
#define FILES_TRAIL 48 // the folders of a trail, the root with them: a deeper path keeps its last ones as crumbs

struct files_trail
{
        p8 path[FILES_PATH];
        p16 from[FILES_TRAIL], to[FILES_TRAIL]; // each folder's name is the path's bytes from..to, and its own path the first `to` bytes (the root's: 1)
        p64 inode[FILES_TRAIL];                 // what each was when it was shown,
        p32 device_major[FILES_TRAIL], device_minor[FILES_TRAIL];
        p8 known[FILES_TRAIL];                  // for those that were
        p8 lost[FILES_TRAIL];                   // a folder after the current one that was looked for and is not there, or is not the folder it was
        int count, current;
};

// The folders of a path: the root, then each name between strokes. A path with more than FILES_TRAIL keeps its last ones.
static fn files_trail_split(struct files_trail address_to t)
{
        positive length = string_length(t->path), at = 1;

        t->count = 0;
        if (!length || t->path[0] != '/')
                return;
        t->from[0] = 0, t->to[0] = 1, t->count = 1;
        while (at < length)
        {
                positive stop = at;

                while (stop < length && t->path[stop] != '/')
                        stop++;
                if (stop > at)
                {
                        if (t->count == FILES_TRAIL)
                        {
                                for (int k = 1; k < FILES_TRAIL; k++)
                                        t->from[k - 1] = t->from[k], t->to[k - 1] = t->to[k];
                                t->count--;
                        }
                        t->from[t->count] = (p16)at, t->to[t->count] = (p16)stop, t->count++;
                }
                at = stop + 1;
        }
}

// The trail is this path, whole. What the folders it shares with the old one were is kept with them; no other is known, and none is lost.
static fn files_trail_become(struct files_trail address_to t, const p8 address_to path)
{
        struct files_trail was = *t;

        string_copy_bounded(t->path, (string_address)path, FILES_PATH);
        files_trail_split(t);
        for (int k = 0; k < t->count; k++)
        {
                b32 same = k < was.count && was.known[k] && was.to[k] == t->to[k] && was.from[k] == t->from[k] && !memory_compare((address_any)was.path, (address_any)t->path, t->to[k]);

                t->known[k] = same, t->lost[k] = 0;
                t->inode[k] = same ? was.inode[k] : 0, t->device_major[k] = same ? was.device_major[k] : 0, t->device_minor[k] = same ? was.device_minor[k] : 0;
        }
}

// The path shown is this one now, and this is the folder it is (null: not known). True when the trail was kept and only the place moved.
static b32 files_trail_note(struct files_trail address_to t, const p8 address_to shown, const file_facts address_to folder)
{
        positive length = string_length((string_address)shown);
        int place = -1;

        for (int k = 0; k < t->count && place < 0; k++)
                if (t->to[k] == length && !memory_compare((address_any)t->path, (address_any)shown, length))
                        place = k;
        if (place < 0)
        {
                // Another branch: the trail is this path.
                files_trail_become(t, shown);
                place = t->count - 1;
        }
        t->current = place < 0 ? 0 : place;
        for (int k = 0; k <= t->current && k < t->count; k++)
                t->lost[k] = 0; // what was gone to is there
        if (folder && place >= 0 && folder->inode)
                t->known[place] = 1, t->inode[place] = folder->inode, t->device_major[place] = folder->device_major, t->device_minor[place] = folder->device_minor;
        return t->to[t->count ? t->count - 1 : 0] != length;
}

// The path of the trail's folder number `which`, with its end.
static fn files_trail_path(const struct files_trail address_to t, int which, p8 address_to into)
{
        positive length = which >= 0 && which < t->count ? t->to[which] : 0;

        memory_copy(into, t->path, length);
        into[length] = end;
}

/*
        A folder of the trail opened as the folder it was. The open comes first and does not follow (O_NOFOLLOW: a link under its
        name is refused by the kernel), and what it was is then compared on the descriptor that was opened, its device and inode,
        so there is no moment between a look and an open in which the name can be given to something else: whatever the
        descriptor is, it is what was compared. FILES_SAME with the descriptor in *handle (the caller's from then on);
        FILES_MISSING for a path that names nothing now; FILES_CHANGED for anything else there (another folder, a file, a link);
        FILES_REFUSED for no such folder of the trail, or one that may not be opened (*error has the kernel's code).
*/
static int files_trail_open(const struct files_trail address_to t, int which, bipolar address_to handle, bipolar address_to error)
{
        p8 path[FILES_PATH];
        file_facts facts;
        bipolar opened;

        *handle = -1, *error = 0;
        if (which < 0 || which >= t->count)
                return FILES_REFUSED;
        files_trail_path(t, which, path);
        opened = system_open_at(AT_FDCWD, path, FILE_READ | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (opened < 0)
        {
                // Why not: nothing there, or something that is no folder (a look, for the word alone: nothing is done by it).
                if (opened == -ERROR_NO_ENTRY)
                        return FILES_MISSING;
                if (opened == -ERROR_NOT_DIRECTORY || opened == -40)
                        return file_look_code(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW, &facts) < 0 ? FILES_MISSING : FILES_CHANGED;
                *error = opened;
                return FILES_REFUSED;
        }
        if (file_look_code(opened, (string_address) "", AT_EMPTY_PATH, &facts) < 0 ||
            (t->known[which] && (facts.inode != t->inode[which] || facts.device_major != t->device_major[which] || facts.device_minor != t->device_minor[which])))
        {
                system_close(opened);
                return FILES_CHANGED;
        }
        *handle = opened;
        return FILES_SAME;
}

// Whether a folder of the trail is the folder it was, and nothing more: opened as files_trail_open opens it, and closed.
static int files_trail_check(const struct files_trail address_to t, int which)
{
        bipolar handle, error;
        int seen = files_trail_open(t, which, &handle, &error);

        if (handle >= 0)
                system_close(handle);
        return seen == FILES_REFUSED && error ? FILES_CHANGED : seen;
}

// The trail ends before its folder number `which`: that one and all after it are dropped. The current one is never dropped.
static fn files_trail_cut(struct files_trail address_to t, int which)
{
        if (which <= t->current || which >= t->count)
                return;
        t->path[t->to[which - 1]] = end;
        t->count = which;
}

// From its folder number `which` on, the trail's folders are lost: looked for and not there. They stay, struck through, to be put right.
static fn files_trail_lose(struct files_trail address_to t, int which)
{
        for (int k = which > t->current ? which : t->current + 1; k < t->count; k++)
                t->lost[k] = 1;
}

// Every kept listing whose folder is not on the trail is let go: what is kept is the trail's.
static fn files_keep_trim(struct files_model address_to m, const struct files_trail address_to t)
{
        for (int k = 0; k < FILES_KEPT; k++)
        {
                positive length = string_length((string_address)m->kept[k].path);
                b32 on = false;

                for (int at = 0; length && at < t->count && !on; at++)
                        on = t->to[at] == length && !memory_compare((address_any)t->path, (address_any)m->kept[k].path, length);
                if (length && !on)
                        files_keep_drop(m, k);
        }
}

/*
        One folder of the trail spelled again: the path the trail would be with its folder number `which` replaced by the text
        and every folder after it kept, written to into. /path/to/something/else with "to" spelled "foo" is
        /path/foo/something/else. A stroke in the text makes several folders of it; no text takes the folder out. The text is a
        person's and is not trusted: a byte below a blank or the delete byte, and a folder spelled . or .., are refused, and so
        is a path that would not fit. The root cannot be spelled again. False for what is refused, and into is not to be read.
*/
static b32 files_trail_spell(const struct files_trail address_to t, int which, const p8 address_to text, p8 address_to into)
{
        positive length = string_length((string_address)text), used = 0, whole = string_length((string_address)t->path);
        positive parts[3][2];
        const p8 address_to from[3];

        if (which < 0 || which >= t->count || t->to[which] <= 1 || (which == 0 && t->from[0] == 0))
                return false;
        for (positive at = 0; at < length; at++)
                if (text[at] < 0x20 || text[at] == 0x7f)
                        return false;
        // What is before it (its stroke with it), the text, what is after it (its stroke with it): strokes run together are one.
        from[0] = t->path, parts[0][0] = 0, parts[0][1] = t->from[which];
        from[1] = text, parts[1][0] = 0, parts[1][1] = length;
        from[2] = t->path, parts[2][0] = t->to[which], parts[2][1] = whole;
        for (int part = 0; part < 3; part++)
                for (positive at = parts[part][0]; at < parts[part][1]; at++)
                {
                        p8 byte = from[part][at];

                        if (byte == '/' && used && into[used - 1] == '/')
                                continue;
                        if (used + 1 >= FILES_PATH)
                                return false;
                        into[used++] = byte;
                }
        while (used > 1 && into[used - 1] == '/')
                used--;
        into[used] = end;
        for (positive at = 1; at < used;)
        {
                positive stop = at;

                while (stop < used && into[stop] != '/')
                        stop++;
                if ((stop - at == 1 && into[at] == '.') || (stop - at == 2 && into[at] == '.' && into[at + 1] == '.') || stop - at >= FILES_NAME)
                        return false;
                at = stop + 1;
        }
        return used && into[0] == '/';
}

/*
        The trail made a path a person spelled, and the folder shown its deepest folder that is one. The path's folders are tried
        from the deepest up, each opened as named (a link is followed, which is what naming a folder means: files_model_go's rule),
        and the first that opens is shown (not opened again when it is the folder shown already); the whole path is the trail,
        with that folder the current one and every folder after it lost. Answers FILES_READY when the whole path was a folder,
        else the state the first folder that was not would have had (FILES_GONE, FILES_NOT_DIRECTORY, FILES_UNREADABLE,
        FILES_FAILED), for a view to say; FILES_NONE, and nothing changes, when not even its first folder opens.
*/
static int files_trail_resolve(struct files_model address_to m, struct files_trail address_to t, const p8 address_to path)
{
        static struct files_trail next;
        p8 part[FILES_PATH];
        file_walk opened;
        int why = FILES_READY, deepest;

        memory_fill(&next, 0, sizeof(next));
        string_copy_bounded(next.path, (string_address)path, FILES_PATH);
        files_trail_split(&next);
        opened.handle = -1;
        for (deepest = next.count - 1; deepest >= 0; deepest--)
        {
                files_trail_path(&next, deepest, part);
                if (file_walk_open(&opened, AT_FDCWD, part))
                        break;
                why = files_state_of(opened.error);
        }
        if (deepest < 0)
                return FILES_NONE;
        if (string_equals((string_address)part, (string_address)m->path) && m->handle >= 0)
                system_close(opened.handle);
        else
                files_model_take(m, part, opened.handle);
        files_trail_become(t, path);
        files_trail_note(t, m->path, m->handle >= 0 ? &m->folder : null);
        files_trail_lose(t, deepest + 1);
        return why;
}

/* ---- changing what is in the folder ---- */
/*
        What changes the disk. Each works on the folder shown through its own descriptor (m->handle), by a name of the listing that
        has just been looked at again (files_model_check) or by a new name that is held to be a name; nothing is resolved by a path,
        so a folder renamed or swapped under the window cannot send an operation somewhere else. Nothing is followed.

        A new folder and a new name are one call each (mkdirat, renameat2 with RENAME_NOREPLACE): a name that is taken is refused
        (FILES_DID_EXISTS) and never written over. The kernel's refusal is kept (m->did_error) for a view to say.

        Deleting is a job (struct files_job), run in steps as a listing is read: files_delete_begin takes the names (each looked at
        again first: one that is no longer the entry listed stops the whole job before anything is removed), files_job_step removes
        at most `most` things and returns, files_job_cancel stops it between two. It is a real delete, not a move to a trash: a trash
        is a folder on the same filesystem with a record of where each thing came from, which this tree does not have, and a delete
        that is asked twice (the view's) is honest about what it does. A file, a link and anything else that is no folder is removed
        as the entry it is, by the tree's own remove for an entry that must still be the one looked at (file_remove_same: opened
        without following, held to its inode, removed through the held folder), so a link is removed and never what it names. A
        folder is emptied from the inside, each folder on the way opened without following and held to its inode
        (file_walk_open_found), and removed when it is empty. What stops it (a folder that may not be written, a busy mount) stops the
        job there: its state is FILES_JOB_FAILED with the kernel's code and the name it stopped at, and removed says how many things
        went before it. Nothing is half-made by a delete; what a stopped or cancelled one leaves is whatever it had not reached, and
        the count says how far it got.
*/
#define FILES_JOB_DEEP 64 // the folders inside one another a delete goes down: one deeper stops the job (FILES_JOB_FAILED, too deep)

enum
{
        FILES_DID,        // done
        FILES_DID_EXISTS, // the name is taken: nothing was written over
        FILES_DID_BAD,    // not a name (empty, a stroke or a NUL in it, . or .., too long), or the entry is no longer the one listed
        FILES_DID_FAILED, // the kernel refused: did_error has its code
};

enum
{
        FILES_JOB_IDLE,
        FILES_JOB_RUNNING,
        FILES_JOB_DONE,
        FILES_JOB_FAILED,    // error has the kernel's code (or -ERROR_NOT_EMPTY's kin), at the name it stopped at
        FILES_JOB_CANCELLED,
};

struct files_job
{
        int state;
        bipolar error;
        p8 at[FILES_NAME];            // the name it is on, or stopped at
        p32 removed;                  // the things removed so far, folders and what was in them alike
        p32 count, next;              // the entries asked for, and the next of them
        p8 address_to names;          // their names, each with its end (the job's own copy: the listing may be read again under it)
        file_facts address_to facts;  // and each as it was looked at when the job began
        bipolar stack[FILES_JOB_DEEP]; // the folders it is inside, the innermost last
        p8 inside[FILES_JOB_DEEP][FILES_NAME]; // and the name each has in the one before it
        int depth;
};

// Whether bytes are a name a folder can hold: not empty, not . or .., no stroke, and short enough.
static b32 files_name_good(const p8 address_to name)
{
        positive length = string_length((string_address)name);

        if (!length || length >= FILES_NAME || file_is_dot((string_address)name))
                return false;
        for (positive at = 0; at < length; at++)
                if (name[at] == '/')
                        return false;
        return true;
}

// A folder of this name made in the folder shown. A name that is taken is refused, whatever has it.
static int files_model_make_folder(struct files_model address_to m, const p8 address_to name)
{
        bipolar made;

        if (m->handle < 0 || !files_name_good(name))
                return FILES_DID_BAD;
        made = (bipolar)system_call_3(syscall(mkdirat), (positive)m->handle, (positive)name, 0755);
        if (made >= 0)
                return FILES_DID;
        return made == -ERROR_EXISTS ? FILES_DID_EXISTS : FILES_DID_FAILED;
}

// The entry at a place given another name in the folder shown: only the entry that was listed, and never over a name that is taken.
static int files_model_rename(struct files_model address_to m, p32 place, const p8 address_to name)
{
        const struct files_entry address_to entry = files_model_entry(m, place);
        bipolar moved;

        if (!entry || !files_name_good(name) || files_model_check(m, place, null) != FILES_SAME)
                return FILES_DID_BAD;
        moved = (bipolar)system_call_5(syscall(renameat2), (positive)m->handle, (positive)files_model_name(m, entry), (positive)m->handle, (positive)name,
                                       FILE_RENAME_NOREPLACE);
        if (moved >= 0)
                return FILES_DID;
        return moved == -ERROR_EXISTS ? FILES_DID_EXISTS : FILES_DID_FAILED;
}

static positive files_job_bytes(void)
{
        return (positive)FILES_MOST * FILES_NAME / 8 + (positive)FILES_MOST / 8 * sizeof(file_facts);
}

static fn files_job_close(struct files_job address_to job)
{
        while (job->depth > 0)
                system_close(job->stack[--job->depth]);
}

// The job let go of: its folders closed and its memory given back.
static fn files_job_end(struct files_job address_to job)
{
        files_job_close(job);
        if (job->names)
                system_call_2(syscall(munmap), (positive)job->names, files_job_bytes());
        memory_fill(job, 0, sizeof(*job));
}

static fn files_job_fail(struct files_job address_to job, bipolar error)
{
        files_job_close(job);
        job->state = FILES_JOB_FAILED, job->error = error;
}

static fn files_job_cancel(struct files_job address_to job)
{
        if (job->state != FILES_JOB_RUNNING)
                return;
        files_job_close(job);
        job->state = FILES_JOB_CANCELLED;
}

/*
        A delete of the entries chosen (or of the one at the cursor when none is chosen) begun. Every one is looked at again first and
        must be the entry listed; one that is not stops the job before it starts (false, and the job is FILES_JOB_FAILED at that
        name), so a listing that is stale deletes nothing. At most FILES_MOST / 8 entries are taken in one job.
*/
static b32 files_delete_begin(struct files_model address_to m, struct files_job address_to job)
{
        bipolar mapped;
        positive used = 0;

        files_job_end(job);
        mapped = (bipolar)system_call_6(syscall(mmap), 0, files_job_bytes(), 3, 0x22, (positive)-1, 0);
        if (mapped < 0 && mapped > -4096)
        {
                job->state = FILES_JOB_FAILED, job->error = mapped;
                return false;
        }
        job->names = (p8 address_to)mapped;
        job->facts = (file_facts address_to)(job->names + (positive)FILES_MOST * FILES_NAME / 8);
        for (p32 place = 0; place < m->shown.places; place++)
        {
                const struct files_entry address_to entry = &m->shown.entry[m->shown.order[place]];

                if (m->chosen_count ? !files_model_chosen(m, place) : place != m->cursor)
                        continue;
                if (job->count >= FILES_MOST / 8 || used + entry->length + 1 > (positive)FILES_MOST * FILES_NAME / 8)
                        break;
                string_copy_bounded(job->at, (string_address)files_model_name(m, entry), FILES_NAME);
                if (files_model_check(m, place, &job->facts[job->count]) != FILES_SAME)
                {
                        files_job_end(job);
                        job->state = FILES_JOB_FAILED, job->error = -ERROR_NO_ENTRY;
                        return false;
                }
                memory_copy(job->names + used, files_model_name(m, entry), (positive)entry->length + 1);
                used += (positive)entry->length + 1;
                job->count++;
        }
        job->at[0] = end;
        job->state = job->count ? FILES_JOB_RUNNING : FILES_JOB_DONE;
        return job->count != 0;
}

// The name of the job's entry number `which`.
static const p8 address_to files_job_name(const struct files_job address_to job, p32 which)
{
        const p8 address_to name = job->names;

        while (which--)
                name += string_length((string_address)name) + 1;
        return name;
}

// One thing of a delete: the next entry inside the innermost folder it is in removed or gone into, or that folder, empty, removed
// from the one it is in. False when the job is over (done or failed).
static b32 files_delete_one(struct files_model address_to m, struct files_job address_to job)
{
        bipolar in = job->depth ? job->stack[job->depth - 1] : m->handle, gone;
        file_walk walk;
        struct linux_dirent64 address_to record;
        file_facts facts;
        const p8 address_to name = null;
        b32 top = !job->depth;

        if (top)
        {
                if (job->next >= job->count)
                {
                        job->state = FILES_JOB_DONE;
                        return false;
                }
                name = files_job_name(job, job->next);
                facts = job->facts[job->next];
        }
        else
        {
                // The first thing still in the folder: read afresh each time, since the last step changed what is there.
                if (!file_walk_open(&walk, in, (string_address) "."))
                {
                        files_job_fail(job, walk.error);
                        return false;
                }
                while ((record = file_walk_next(&walk)) && file_is_dot((string_address)record->d_name))
                        ;
                if (record)
                        string_copy_bounded(job->at, (string_address)record->d_name, FILES_NAME);
                gone = walk.error;
                file_walk_close(&walk);
                if (!record)
                {
                        // Empty (or unreadable: the remove says which): out of it, and it is removed from the folder it is in.
                        if (gone < 0)
                        {
                                files_job_fail(job, gone);
                                return false;
                        }
                        system_close(job->stack[--job->depth]);
                        string_copy_bounded(job->at, job->inside[job->depth], FILES_NAME);
                        gone = (bipolar)system_call_3(syscall(unlinkat), (positive)(job->depth ? job->stack[job->depth - 1] : m->handle), (positive)job->at, AT_REMOVEDIR);
                        if (gone < 0)
                        {
                                files_job_fail(job, gone);
                                return false;
                        }
                        job->removed++;
                        job->next += !job->depth;
                        return true;
                }
                name = job->at;
                gone = file_look_code(in, (string_address)name, AT_SYMLINK_NOFOLLOW, &facts);
                if (gone < 0)
                {
                        files_job_fail(job, gone);
                        return false;
                }
        }
        if (top)
                string_copy_bounded(job->at, (string_address)name, FILES_NAME);
        if ((facts.mode & MODE_FORMAT) == MODE_DIRECTORY)
        {
                // A folder: gone into, opened without following and held to the inode looked at. It is removed when it is empty.
                if (job->depth >= FILES_JOB_DEEP)
                {
                        files_job_fail(job, -40);
                        return false;
                }
                if (!file_walk_open_found_same(&walk, in, (string_address)job->at, &facts))
                {
                        files_job_fail(job, walk.error);
                        return false;
                }
                string_copy_bounded(job->inside[job->depth], job->at, FILES_NAME);
                job->stack[job->depth++] = walk.handle;
                return true;
        }
        // Anything else is removed as the entry it is: a link is the link, and it must still be what was looked at.
        gone = file_remove_same(in, (string_address)job->at, 0, &facts);
        if (gone < 0)
        {
                files_job_fail(job, gone);
                return false;
        }
        job->removed++;
        job->next += top;
        return true;
}

// A step of the job: at most `most` things, and back. True while there is more to do.
static b32 files_job_step(struct files_model address_to m, struct files_job address_to job, positive most)
{
        for (positive done = 0; done < most && job->state == FILES_JOB_RUNNING; done++)
                if (!files_delete_one(m, job))
                        break;
        return job->state == FILES_JOB_RUNNING;
}

/*
        The folder tree (struct files_tree): the folders of the disk as a tree a view shows beside the listing, its nodes in the order they
        are shown (each node's folders straight after it, one level in), opened and shut by the person. A node opened has its folders read
        (files_tree_open): its entries looked at without following, the folders among them (a link to a folder is not one) kept in name
        order, at most FILES_TREE_CHILDREN of them and FILES_TREE_SCAN entries looked through, so a folder of any size costs a bounded
        read; a node shut lets go of everything under it. Each node is held to the device and inode it was read as, so a path made of
        the tree (files_tree_path) names what was listed or, when it was swapped since, is found out by the model's own open (it is
        opened as a named path, files_model_try, and the model says what it found). files_tree_reveal opens the nodes down a path, so the
        folder shown is in the tree; a folder that is not there is not added.
*/
#define FILES_TREE_MOST 4096       // the nodes a tree holds
#define FILES_TREE_NAMES (FILES_TREE_MOST * 32u) // and the bytes of their names
#define FILES_TREE_CHILDREN 1024   // the folders a node opened shows at most
#define FILES_TREE_SCAN 65536      // and the entries looked through to find them

struct files_node
{
        p32 name;          // where its name starts in the tree's names
        p16 length;
        p16 depth;         // 0 the root
        b8 open, read;     // its folders are shown; they were read (and it has has of them)
        p16 has;           // the folders it was read to have
        p64 inode;
        p32 device_major, device_minor;
};

struct files_tree
{
        struct files_node address_to node;
        p8 address_to names;
        p32 count, names_used;
        address_any mapped;
        positive mapped_size;
};

static positive files_tree_bytes(void)
{
        return (positive)FILES_TREE_MOST * sizeof(struct files_node) + FILES_TREE_NAMES;
}

// The tree's memory, and its root, the folder /. False when there is no memory.
static b32 files_tree_begin(struct files_tree address_to t)
{
        bipolar mapped = (bipolar)system_call_6(syscall(mmap), 0, files_tree_bytes(), 3, 0x22, (positive)-1, 0);
        file_facts facts;

        memory_fill(t, 0, sizeof(*t));
        if (mapped < 0 && mapped > -4096)
                return false;
        t->mapped = (address_any)mapped, t->mapped_size = files_tree_bytes();
        t->node = (struct files_node address_to)mapped;
        t->names = (p8 address_to)mapped + (positive)FILES_TREE_MOST * sizeof(struct files_node);
        t->names[0] = '/', t->names[1] = end, t->names_used = 2;
        t->node[0] = (struct files_node){0, 1, 0, false, false, 0, 0, 0, 0};
        if (file_look_code(AT_FDCWD, (string_address) "/", 0, &facts) >= 0)
                t->node[0].inode = facts.inode, t->node[0].device_major = facts.device_major, t->node[0].device_minor = facts.device_minor;
        t->count = 1;
        return true;
}

static fn files_tree_end(struct files_tree address_to t)
{
        if (t->mapped)
                system_call_2(syscall(munmap), (positive)t->mapped, t->mapped_size);
        memory_fill(t, 0, sizeof(*t));
}

static inline const p8 address_to files_tree_name(const struct files_tree address_to t, p32 at)
{
        return t->names + t->node[at].name;
}

// The place after a node's last one under it: where its folders end.
static p32 files_tree_after(const struct files_tree address_to t, p32 at)
{
        p32 next = at + 1;

        while (next < t->count && t->node[next].depth > t->node[at].depth)
                next++;
        return next;
}

// The node's parent's place, or FILES_NO for the root.
static p32 files_tree_parent(const struct files_tree address_to t, p32 at)
{
        while (at > 0 && at < t->count)
        {
                p32 depth = t->node[at].depth;

                for (p32 up = at; up-- > 0;)
                        if (t->node[up].depth < depth)
                                return up;
                break;
        }
        return FILES_NO;
}

// A node's path, the root's names joined with strokes: into, FILES_PATH bytes. False when it would not fit.
static b32 files_tree_path(const struct files_tree address_to t, p32 at, p8 address_to into)
{
        p32 chain[256];
        int links = 0;
        positive used = 0;

        for (p32 here = at; here != FILES_NO && here < t->count && links < 256; here = files_tree_parent(t, here))
                chain[links++] = here;
        into[used++] = '/';
        for (int k = links - 2; k >= 0; k--)
        {
                const struct files_node address_to n = &t->node[chain[k]];

                if (used + n->length + 2 >= FILES_PATH)
                        return false;
                if (used > 1)
                        into[used++] = '/';
                memory_copy(into + used, t->names + n->name, n->length);
                used += n->length;
        }
        into[used] = end;
        return true;
}

// The names of a node's folders read, in order: the collected names (each with its end) of names_used bytes, count of them.
static int files_tree_name_order(address_any left, address_any right, address_any context)
{
        const p8 address_to names = (const p8 address_to)context;

        return files_name_order(names + *(const p32 address_to)left, names + *(const p32 address_to)right);
}

/*
        A node opened: its folders read (once; a node read before shows what it read, and is read again by files_tree_shut then open) and
        put after it, one level in. False when it cannot be read or there is no room; it is then shown with none.
*/
static b32 files_tree_open(struct files_tree address_to t, p32 at)
{
        static p8 path[FILES_PATH];
        static p32 offsets[FILES_TREE_CHILDREN];
        static p8 found[FILES_TREE_CHILDREN * 64];
        file_walk walk;
        struct linux_dirent64 address_to record;
        file_facts folder;
        p32 count = 0, scanned = 0, after, depth;
        positive used = 0, need = 0;

        if (at >= t->count || t->node[at].open)
                return at < t->count;
        if (!files_tree_path(t, at, path) || !file_walk_open(&walk, AT_FDCWD, (string_address)path))
        {
                t->node[at].read = true, t->node[at].has = 0;
                return false;
        }
        // The folder held to what the node was read as: another one under its path is not this node's.
        if (file_look_code(walk.handle, (string_address) "", AT_EMPTY_PATH, &folder) < 0 ||
            (t->node[at].inode && (folder.inode != t->node[at].inode || folder.device_major != t->node[at].device_major ||
                                   folder.device_minor != t->node[at].device_minor)))
        {
                file_walk_close(&walk);
                t->node[at].read = true, t->node[at].has = 0;
                return false;
        }
        while (count < FILES_TREE_CHILDREN && scanned < FILES_TREE_SCAN && (record = file_walk_next(&walk)))
        {
                positive length = string_length((string_address)record->d_name);
                file_facts facts;

                scanned++;
                if (file_is_dot((string_address)record->d_name) || record->d_name[0] == '.' || length >= FILES_NAME || used + length + 1 > sizeof(found))
                        continue;
                // A folder as it is, not followed: d_type when the disk says it, a look otherwise.
                if (record->d_type != DT_DIR && (record->d_type != DT_UNKNOWN || file_look_code(walk.handle, (string_address)record->d_name, AT_SYMLINK_NOFOLLOW, &facts) < 0 ||
                                                 (facts.mode & MODE_FORMAT) != MODE_DIRECTORY))
                        continue;
                offsets[count++] = (p32)used;
                memory_copy(found + used, record->d_name, length + 1);
                used += length + 1;
        }
        if (count > 1)
                qsort_r(offsets, count, sizeof(p32), files_tree_name_order, found);
        for (p32 k = 0; k < count; k++)
                need += string_length((string_address)(found + offsets[k]));
        after = at + 1, depth = t->node[at].depth + 1u;
        t->node[at].read = true, t->node[at].has = (p16)count, t->node[at].open = true;
        if (t->count + count > FILES_TREE_MOST || t->names_used + need + count > FILES_TREE_NAMES)
        {
                file_walk_close(&walk);
                t->node[at].open = false;
                return false;
        }
        // Room after the node, and each folder put there, its device and inode looked at without following.
        memory_copy(&t->node[after + count], &t->node[after], (positive)(t->count - after) * sizeof(struct files_node));
        for (p32 k = 0; k < count; k++)
        {
                const p8 address_to name = found + offsets[k];
                positive length = string_length((string_address)name);
                struct files_node address_to n = &t->node[after + k];
                file_facts facts;

                memory_fill(n, 0, sizeof(*n));
                n->name = t->names_used, n->length = (p16)length, n->depth = (p16)depth;
                memory_copy(t->names + t->names_used, name, length + 1);
                t->names_used += (p32)length + 1;
                if (file_look_code(walk.handle, (string_address)name, AT_SYMLINK_NOFOLLOW, &facts) >= 0)
                        n->inode = facts.inode, n->device_major = facts.device_major, n->device_minor = facts.device_minor;
        }
        t->count += count;
        file_walk_close(&walk);
        return true;
}

// A node shut: every node under it let go (their names stay in the tree's bytes till it is made again), and read again when opened.
static fn files_tree_shut(struct files_tree address_to t, p32 at)
{
        p32 after;

        if (at >= t->count || !t->node[at].open)
                return;
        after = files_tree_after(t, at);
        memory_copy(&t->node[at + 1], &t->node[after], (positive)(t->count - after) * sizeof(struct files_node));
        t->count -= after - at - 1;
        t->node[at].open = false, t->node[at].read = false;
}

/*
        The nodes down a path opened, the root first, so the folder at its end shows: the node of the path's last folder (FILES_NO when a
        folder of it is not in the tree: hidden, gone, or past what a node shows).
*/
static p32 files_tree_reveal(struct files_tree address_to t, const p8 address_to path)
{
        p32 at = 0;
        positive start = 1, length = string_length((string_address)path);

        if (!t->count || path[0] != '/')
                return FILES_NO;
        while (start < length)
        {
                positive stop = start;
                p32 next = FILES_NO;

                while (stop < length && path[stop] != '/')
                        stop++;
                if (!t->node[at].open)
                        files_tree_open(t, at);
                for (p32 k = at + 1; k < t->count && t->node[k].depth > t->node[at].depth; k = files_tree_after(t, k))
                        if (t->node[k].length == stop - start && !memory_compare(t->names + t->node[k].name, path + start, stop - start))
                        {
                                next = k;
                                break;
                        }
                if (next == FILES_NO)
                        return FILES_NO;
                at = next, start = stop + 1;
        }
        return at;
}

/*
        Copying and moving (struct files_move): files named by their paths (a drop's, a paste's, the browser's own rows) put into a
        folder held open (into), copied or moved, run in steps by a thread of its own (files_move_start) so that a folder of any size
        and a disk of any speed never holds the window, which reads the counts as they go (bytes_done of bytes_total, items_done of
        items_total) and may ask it to stop (files_move_cancel) between two pieces.

        Nothing is written over and nothing is followed. A source is a path a person handed over: its folder is opened as the path
        names it, and the source itself is looked at without following (a link is copied as the link it is, never what it names) and
        opened held to the inode looked at (file_open_same, file_walk_open_found_same), so a name swapped between the look and the
        open is refused. What is made in the folder is made only where the name is free (mkdirat, O_EXCL with O_NOFOLLOW, symlinkat,
        renameat2 with RENAME_NOREPLACE); a name that is taken there is given a copy's, "name.copy.txt", "name.copy2.txt" and on (renamed counts them),
        so two things never become one. A device, a pipe or a socket is not copied (skipped counts them).

        A move is a rename where the source and the folder are on one filesystem, which is whole and instant; one into the folder it
        is already in is nothing; across filesystems it is a copy and then, once the copy of that source is whole, its removal (as
        the delete removes: held to what was looked at, a link removed and never what it names). A folder copied or moved into
        itself or a folder inside it is refused (FILES_MOVE_INSIDE) before anything of it is made.

        A copy that stops (cancelled, or the kernel refused) leaves what was whole and removes the file it was in the middle of; the
        sources of a move are removed only when their copy is whole. The thread touches nothing of the model: the folder shown is read
        again when the view sees the move end.
*/
#define FILES_MOVE_MOST 4096          // the sources one move takes
#define FILES_MOVE_PIECE (8u << 20)   // a file's bytes copied in one piece: a cancel is seen between two
#define FILES_MOVE_BUFFER (1u << 20)  // the bytes read and written at a time where the kernel will not copy a range
#define FILES_MOVE_INSIDE 1000        // error: a folder into itself
#define FILES_MOVE_COPY 1             // what a move does: the values the drag's actions have (WINDOWS_DRAG_COPY, _MOVE)
#define FILES_MOVE_MOVE 2
#define FILES_MOVE_MEASURE 3          // or only counts what the sources hold (a properties sheet's size): nothing is made or moved

enum
{
        FILES_MOVE_RENAME, // the moves by rename, and which sources must be copied
        FILES_MOVE_COUNT,  // what is to be copied, counted
        FILES_MOVE_COPYING,
        FILES_MOVE_REMOVE, // a moved source's copy is whole: the source goes
        FILES_MOVE_OVER,
};

struct files_move
{
        int how, state, phase;            // FILES_MOVE_COPY or _MOVE; FILES_JOB_*; FILES_MOVE_RENAME and on
        bipolar error;                    // why it stopped: the kernel's code, or FILES_MOVE_INSIDE
        b32 cancel;                       // asked by the view
        p64 bytes_total, bytes_done;      // the bytes to copy and copied (moves by rename count none)
        p32 items_total, items_done;      // the things: a file, a link, a folder each
        p32 skipped, renamed;             // not copied (a device, a pipe), and given a number for a name that was taken
        p8 at[FILES_NAME];                // the source it stopped at (the view reads it once the move is over)
        bipolar into;                     // the folder it goes into, its own descriptor
        file_facts into_facts;
        p8 address_to paths;              // the sources, absolute, each with its end
        p8 address_to copied;             // per source: 1 when it is copied (a copy, or a move across filesystems), 2 when it is done
        p8 address_to landed;             // per source, FILES_NAME bytes: the name it has in into once it is there ("" none), for an undo
        p32 count, next;
        // The source being worked: its folder, its name, how it was looked at, the name it has in into.
        bipolar from;
        p8 name[FILES_NAME], named[FILES_NAME];
        file_facts facts;
        // The folders inside it being copied (or counted, or removed), innermost last: each source folder's walk and the folder made
        // for it, and the source folder's facts (its mode and times, given to the copy when it is whole).
        int depth;
        file_walk walk[FILES_JOB_DEEP];
        bipolar made[FILES_JOB_DEEP];
        file_facts made_facts[FILES_JOB_DEEP];
        p8 inside[FILES_JOB_DEEP][FILES_NAME];
        // A file being copied, a piece at a time.
        bipolar in, out, out_folder;
        p8 out_name[FILES_NAME];
        p64 left;
        file_facts piece_facts;           // and how its source was looked at: its mode and times, given to it when it is whole
        b32 ranged;                       // the kernel copies a range here: no buffer
        p8 address_to buffer;
        thread address_to worker;
        b32 begun;                        // files_move_begin was called: files_move_end has what it holds to let go of
};

// The name source which has in the folder it went into, "" while it has none there.
static inline p8 address_to files_move_landed(const struct files_move address_to job, p32 which)
{
        return job->landed + (positive)which * FILES_NAME;
}

static b32 files_move_cancelled(const struct files_move address_to job)
{
        return __atomic_load_n(&job->cancel, __ATOMIC_ACQUIRE) != 0;
}

static fn files_move_fail(struct files_move address_to job, bipolar error)
{
        job->error = error;
        __atomic_store_n(&job->state, FILES_JOB_FAILED, __ATOMIC_RELEASE);
}

// The file half copied let go of: its descriptors closed and, when it was not whole, what was made of it removed (it is the move's own:
// made with O_EXCL, and removed held to the inode it has).
static fn files_move_file_drop(struct files_move address_to job, b32 whole)
{
        file_facts made;

        if (job->in >= 0)
                system_close(job->in);
        if (job->out >= 0)
        {
                if (!whole && file_look_code(job->out, (string_address) "", AT_EMPTY_PATH, &made) >= 0)
                        file_remove_same(job->out_folder, (string_address)job->out_name, 0, &made);
                system_close(job->out);
        }
        job->in = job->out = -1, job->left = 0;
}

// Everything the move holds let go of (its thread joined first by files_move_end): its folders, its file, its memory.
static fn files_move_close(struct files_move address_to job)
{
        files_move_file_drop(job, false);
        while (job->depth > 0)
        {
                job->depth--;
                file_walk_close(&job->walk[job->depth]);
                if (job->made[job->depth] >= 0)
                        system_close(job->made[job->depth]);
        }
        if (job->from >= 0)
                system_close(job->from);
        if (job->into >= 0)
                system_close(job->into);
        job->from = job->into = -1;
}

// The bytes of a move's own memory: the sources' paths, and a mark each.
static positive files_move_bytes(void)
{
        return (positive)FILES_MOVE_MOST * FILES_PATH / 4 + FILES_MOVE_MOST + (positive)FILES_MOVE_MOST * FILES_NAME;
}

/*
        A move set up, not started: the sources (paths, count of them, each with its end) into the folder at into (a descriptor that is
        the move's from now on, closed when it ends, whatever comes of this). A source that is no absolute path, the folder itself, or
        more than FILES_MOVE_MOST of them are left out. The job's memory is a mapping of its own (files_move_end gives it back).
*/
static b32 files_move_begin(struct files_move address_to job, int how, bipolar into, const p8 address_to paths, p32 count)
{
        bipolar mapped;
        positive used = 0, room = (positive)FILES_MOVE_MOST * FILES_PATH / 4;

        memory_fill(job, 0, sizeof(*job));
        job->into = into, job->from = job->in = job->out = job->out_folder = -1, job->begun = true;
        job->how = how == FILES_MOVE_MOVE || how == FILES_MOVE_MEASURE ? how : FILES_MOVE_COPY;
        for (int k = 0; k < FILES_JOB_DEEP; k++)
                job->made[k] = -1, job->walk[k].handle = -1;
        if (into < 0 || file_look_code(into, (string_address) "", AT_EMPTY_PATH, &job->into_facts) < 0 ||
            (job->into_facts.mode & MODE_FORMAT) != MODE_DIRECTORY)
        {
                files_move_fail(job, into < 0 ? into : -ERROR_NOT_DIRECTORY);
                return false;
        }
        mapped = (bipolar)system_call_6(syscall(mmap), 0, files_move_bytes(), 3, 0x22, (positive)-1, 0);
        if (mapped < 0 && mapped > -4096)
        {
                files_move_fail(job, mapped);
                return false;
        }
        job->paths = (p8 address_to)mapped, job->copied = job->paths + room, job->landed = job->copied + FILES_MOVE_MOST;
        for (p32 k = 0; k < count && job->count < FILES_MOVE_MOST; k++)
        {
                positive length = string_length((string_address)paths);

                if (length > 1 && paths[0] == '/' && length < FILES_PATH && used + length + 1 <= room)
                {
                        memory_copy(job->paths + used, paths, length + 1);
                        used += length + 1, job->count++;
                }
                paths += length + 1;
        }
        job->state = job->count ? FILES_JOB_RUNNING : FILES_JOB_DONE;
        return job->count != 0;
}

static const p8 address_to files_move_path(const struct files_move address_to job, p32 which)
{
        const p8 address_to path = job->paths;

        while (which--)
                path += string_length((string_address)path) + 1;
        return path;
}

/*
        The source number which made the one worked: its folder opened as its path names it and the source looked at in it without
        following. False (the move failed) when either is not there.
*/
static b32 files_move_take(struct files_move address_to job, p32 which)
{
        const p8 address_to path = files_move_path(job, which);
        positive length = string_length((string_address)path), cut = length;
        p8 folder[FILES_PATH];
        bipolar looked;

        while (cut > 0 && path[cut - 1] != '/')
                cut--;
        if (job->from >= 0)
                system_close(job->from);
        job->from = -1;
        if (!cut || cut == length || length - cut >= FILES_NAME)
        {
                files_move_fail(job, -ERROR_INVALID);
                return false;
        }
        memory_copy(job->name, path + cut, length - cut + 1);
        string_copy_bounded(job->at, (string_address)job->name, FILES_NAME);
        memory_copy(folder, path, cut > 1 ? cut - 1 : 1);
        folder[cut > 1 ? cut - 1 : 1] = end;
        job->from = system_open_at(AT_FDCWD, folder, FILE_READ | O_DIRECTORY | O_CLOEXEC);
        looked = job->from < 0 ? job->from : file_look_code(job->from, (string_address)job->name, AT_SYMLINK_NOFOLLOW, &job->facts);
        if (looked < 0)
        {
                files_move_fail(job, looked);
                return false;
        }
        return true;
}

// Whether the folder (a descriptor) is the folder shown by facts or inside it: its folders looked at up to the root.
static b32 files_move_within(bipolar folder, const file_facts address_to facts)
{
        bipolar at = system_open_at(folder, (string_address) ".", FILE_READ | O_DIRECTORY | O_CLOEXEC);
        b32 inside = false;

        for (int up = 0; at >= 0 && up < 4096 && !inside; up++)
        {
                file_facts here, above;
                bipolar next;

                if (file_look_code(at, (string_address) "", AT_EMPTY_PATH, &here) < 0)
                        break;
                inside = here.inode == facts->inode && here.device_major == facts->device_major && here.device_minor == facts->device_minor;
                next = system_open_at(at, (string_address) "..", FILE_READ | O_DIRECTORY | O_CLOEXEC);
                if (next < 0 || file_look_code(next, (string_address) "", AT_EMPTY_PATH, &above) < 0 ||
                    (above.inode == here.inode && above.device_major == here.device_major && above.device_minor == here.device_minor))
                {
                        if (next >= 0)
                                system_close(next);
                        break;
                }
                system_close(at);
                at = next;
        }
        if (at >= 0)
                system_close(at);
        return inside;
}

/*
        A name for the source in the folder at into that is free there now: its own, else "stem.copy.ext", then "stem.copy2.ext" and on (a
        name with its dot first has no extension: ".profile.copy"), so the copy sorts beside what it is a copy of and keeps its kind.
        Answers false when none of the first thousand is free, or the name would not fit.
*/
static b32 files_move_free_name(bipolar into, const p8 address_to name, p8 address_to free, b32 address_to numbered)
{
        positive length = string_length((string_address)name), dot = length;
        file_facts there;

        *numbered = false;
        if (file_look_code(into, (string_address)name, AT_SYMLINK_NOFOLLOW, &there) == -ERROR_NO_ENTRY)
        {
                memory_copy(free, name, length + 1);
                return true;
        }
        for (positive at = length; at > 1; at--)
                if (name[at - 1] == '.')
                {
                        dot = at - 1;
                        break;
                }
        for (positive number = 1; number < 1000; number++)
        {
                p8 digits[8];
                positive count = number > 1 ? positive_into(digits, number) : 0;

                if (length + count + 6 >= FILES_NAME)
                        return false;
                memory_copy(free, name, dot);
                memory_copy(free + dot, ".copy", 5);
                memory_copy(free + dot + 5, digits, count);
                memory_copy(free + dot + 5 + count, name + dot, length - dot + 1);
                if (file_look_code(into, (string_address)free, AT_SYMLINK_NOFOLLOW, &there) == -ERROR_NO_ENTRY)
                {
                        *numbered = true;
                        return true;
                }
        }
        return false;
}

// The innermost folder being copied into, or into itself when none.
static bipolar files_move_target(const struct files_move address_to job)
{
        return job->depth ? job->made[job->depth - 1] : job->into;
}

// The innermost source folder, or the source's own folder.
static bipolar files_move_source(const struct files_move address_to job)
{
        return job->depth ? job->walk[job->depth - 1].handle : job->from;
}

// A folder's mode and its times given to its copy, now that what is in it is whole.
static fn files_move_settle(bipolar made, const file_facts address_to facts)
{
        b64 times[4] = {facts->accessed.seconds, facts->accessed.nanoseconds, facts->modified.seconds, facts->modified.nanoseconds};

        system_call_2(syscall(fchmod), (positive)made, (positive)(facts->mode & 0777));
        system_call_4(syscall(utimensat), (positive)made, 0, (positive)times, 0);
}

/*
        One thing of a copy (or of its count, when counting): the source as it was looked at (in the folder source, by name), made in
        target under made_name. A folder is made and gone into (its walk and the folder made pushed), a file opened for its pieces, a link
        made again, anything else skipped. False when the move stopped (failed).
*/
static b32 files_move_one(struct files_move address_to job, bipolar source, const p8 address_to name, file_facts address_to facts, const p8 address_to made_name,
                          b32 counting)
{
        p32 format = facts->mode & MODE_FORMAT;
        bipolar target = files_move_target(job), made;

        if (format == MODE_DIRECTORY)
        {
                if (job->depth >= FILES_JOB_DEEP)
                {
                        files_move_fail(job, -ERROR_LOOP);
                        return false;
                }
                if (!file_walk_open_found_same(&job->walk[job->depth], source, (string_address)name, facts))
                {
                        files_move_fail(job, job->walk[job->depth].error);
                        return false;
                }
                job->made[job->depth] = -1, job->made_facts[job->depth] = *facts;
                string_copy_bounded(job->inside[job->depth], (string_address)name, FILES_NAME);
                if (!counting)
                {
                        made = (bipolar)system_call_3(syscall(mkdirat), (positive)target, (positive)made_name, 0700);
                        made = made < 0 ? made : system_open_at(target, (string_address)made_name, FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                        if (made < 0)
                        {
                                file_walk_close(&job->walk[job->depth]);
                                files_move_fail(job, made);
                                return false;
                        }
                        job->made[job->depth] = made;
                }
                job->depth++;
                return true;
        }
        if (counting)
        {
                __atomic_add_fetch(&job->items_total, 1, __ATOMIC_RELEASE);
                if (format == MODE_FILE)
                        __atomic_add_fetch(&job->bytes_total, facts->size, __ATOMIC_RELEASE);
                return true;
        }
        if (format == MODE_LINK)
        {
                p8 points[FILES_PATH];
                bipolar length = (bipolar)system_call_4(syscall(readlinkat), (positive)source, (positive)name, (positive)points, sizeof(points) - 1);

                if (length >= 0)
                {
                        points[length] = end;
                        length = (bipolar)system_call_3(syscall(symlinkat), (positive)points, (positive)target, (positive)made_name);
                }
                if (length < 0)
                {
                        files_move_fail(job, length);
                        return false;
                }
                __atomic_add_fetch(&job->items_done, 1, __ATOMIC_RELEASE);
                return true;
        }
        if (format != MODE_FILE)
        {
                job->skipped++;
                __atomic_add_fetch(&job->items_done, 1, __ATOMIC_RELEASE);
                return true;
        }
        job->in = file_open_same(source, (string_address)name, facts, FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        job->out = job->in < 0 ? -1 : system_open_at_mode(target, (string_address)made_name, FILE_WRITE | FILE_EXCLUSIVE | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (job->in < 0 || job->out < 0)
        {
                bipolar why = job->in < 0 ? job->in : job->out;

                job->out = -1; // nothing was made
                files_move_file_drop(job, true);
                files_move_fail(job, why);
                return false;
        }
        job->out_folder = target, job->left = facts->size, job->ranged = true;
        string_copy_bounded(job->out_name, (string_address)made_name, FILES_NAME);
        job->piece_facts = *facts; // the file's own mode and times, for when it is whole
        return true;
}

// A piece of the file under way: FILES_MOVE_PIECE bytes, by the kernel's range copy or, where it will not, read and written. When it is
// whole its mode and times are given to the copy. False when the move stopped.
static b32 files_move_piece(struct files_move address_to job)
{
        positive want = job->left < FILES_MOVE_PIECE ? (positive)job->left : FILES_MOVE_PIECE, moved = 0;
        bipolar got = 0;

        while (moved < want)
        {
                if (job->ranged)
                {
                        got = (bipolar)system_call_6(syscall(copy_file_range), (positive)job->in, 0, (positive)job->out, 0, want - moved, 0);
                        if (got == -ERROR_CROSS_DEVICE || got == -38 || got == -ERROR_INVALID || got == -95 || (got == 0 && !moved))
                        {
                                job->ranged = false; // EXDEV, ENOSYS, EINVAL, EOPNOTSUPP, or nothing: the bytes go by hand
                                continue;
                        }
                }
                else
                {
                        if (!job->buffer)
                        {
                                bipolar mapped = (bipolar)system_call_6(syscall(mmap), 0, FILES_MOVE_BUFFER, 3, 0x22, (positive)-1, 0);

                                if (mapped < 0 && mapped > -4096)
                                {
                                        got = mapped;
                                        break;
                                }
                                job->buffer = (p8 address_to)mapped;
                        }
                        got = (bipolar)system_read_once(job->in, job->buffer, want - moved < FILES_MOVE_BUFFER ? want - moved : FILES_MOVE_BUFFER);
                        if (got > 0 && (bipolar)system_write_all((positive)job->out, job->buffer, (positive)got) != got)
                                got = -5;
                }
                if (got == -ERROR_INTERRUPTED)
                        continue;
                if (got <= 0)
                        break;
                moved += (positive)got;
                __atomic_add_fetch(&job->bytes_done, (p64)got, __ATOMIC_RELEASE);
        }
        job->left -= moved;
        if (got < 0 || (!moved && job->left))
        {
                // A file that shrank under the copy ends where it ended; anything else the kernel said stops the move.
                if (got == 0)
                        job->left = 0;
                else
                {
                        files_move_file_drop(job, false);
                        files_move_fail(job, got);
                        return false;
                }
        }
        if (!job->left)
        {
                files_move_settle(job->out, &job->piece_facts);
                files_move_file_drop(job, true);
                __atomic_add_fetch(&job->items_done, 1, __ATOMIC_RELEASE);
        }
        return true;
}

/*
        A step of a copy (or a count) of the source worked: the file under way's next piece; else the next thing in the innermost
        folder, or that folder ended (its copy given its mode and times). True while the source has more; false at its end or when
        the move stopped (state says which).
*/
static b32 files_move_walk(struct files_move address_to job, b32 counting)
{
        struct linux_dirent64 address_to record;
        file_facts facts;
        bipolar looked;
        file_walk address_to walk;

        if (job->out >= 0)
                return files_move_piece(job) && (job->out >= 0 || job->depth > 0);
        if (!job->depth)
                return false;
        walk = &job->walk[job->depth - 1];
        while ((record = file_walk_next(walk)) && file_is_dot((string_address)record->d_name))
                ;
        if (!record)
        {
                if (walk->error < 0)
                {
                        files_move_fail(job, walk->error);
                        return false;
                }
                job->depth--;
                file_walk_close(walk);
                if (job->made[job->depth] >= 0)
                {
                        files_move_settle(job->made[job->depth], &job->made_facts[job->depth]);
                        system_close(job->made[job->depth]);
                        job->made[job->depth] = -1;
                }
                __atomic_add_fetch(counting ? &job->items_total : &job->items_done, 1, __ATOMIC_RELEASE);
                return job->depth > 0;
        }
        looked = file_look_code(walk->handle, (string_address)record->d_name, AT_SYMLINK_NOFOLLOW, &facts);
        if (looked < 0)
        {
                string_copy_bounded(job->at, (string_address)record->d_name, FILES_NAME);
                files_move_fail(job, looked);
                return false;
        }
        return files_move_one(job, walk->handle, (const p8 address_to)record->d_name, &facts, (const p8 address_to)record->d_name, counting) &&
               (job->depth > 0 || job->out >= 0);
}

/*
        A step of a moved source's removal (its copy is whole): the first thing left in the innermost folder removed or gone into, or
        that folder, empty, removed from the one it is in; the source itself last. False at the end, or when it stopped.
*/
static b32 files_move_remove(struct files_move address_to job)
{
        bipolar in = files_move_source(job), gone;
        file_walk walk;
        struct linux_dirent64 address_to record;
        file_facts facts;

        if (!job->depth)
        {
                // The source itself: a folder is emptied first, anything else removed as it was looked at.
                if ((job->facts.mode & MODE_FORMAT) == MODE_DIRECTORY && !job->made_facts[0].mode)
                {
                        job->made_facts[0].mode = 1; // gone into once
                        if (!file_walk_open_found_same(&job->walk[0], job->from, (string_address)job->name, &job->facts))
                        {
                                files_move_fail(job, job->walk[0].error);
                                return false;
                        }
                        string_copy_bounded(job->inside[0], (string_address)job->name, FILES_NAME);
                        job->made[0] = -1, job->depth = 1;
                        return true;
                }
                gone = (job->facts.mode & MODE_FORMAT) == MODE_DIRECTORY
                           ? (bipolar)system_call_3(syscall(unlinkat), (positive)job->from, (positive)job->name, AT_REMOVEDIR)
                           : file_remove_same(job->from, (string_address)job->name, 0, &job->facts);
                if (gone < 0)
                        files_move_fail(job, gone);
                return false;
        }
        if (!file_walk_open(&walk, in, (string_address) "."))
        {
                files_move_fail(job, walk.error);
                return false;
        }
        while ((record = file_walk_next(&walk)) && file_is_dot((string_address)record->d_name))
                ;
        if (record)
                string_copy_bounded(job->at, (string_address)record->d_name, FILES_NAME);
        gone = walk.error;
        file_walk_close(&walk);
        if (!record)
        {
                if (gone < 0)
                {
                        files_move_fail(job, gone);
                        return false;
                }
                job->depth--;
                file_walk_close(&job->walk[job->depth]);
                if (job->depth)
                {
                        gone = (bipolar)system_call_3(syscall(unlinkat), (positive)files_move_source(job), (positive)job->inside[job->depth], AT_REMOVEDIR);
                        if (gone < 0)
                        {
                                files_move_fail(job, gone);
                                return false;
                        }
                }
                return true;
        }
        gone = file_look_code(in, (string_address)job->at, AT_SYMLINK_NOFOLLOW, &facts);
        if (gone >= 0 && (facts.mode & MODE_FORMAT) == MODE_DIRECTORY)
        {
                if (job->depth >= FILES_JOB_DEEP)
                {
                        files_move_fail(job, -ERROR_LOOP);
                        return false;
                }
                if (!file_walk_open_found_same(&job->walk[job->depth], in, (string_address)job->at, &facts))
                {
                        files_move_fail(job, job->walk[job->depth].error);
                        return false;
                }
                string_copy_bounded(job->inside[job->depth], (string_address)job->at, FILES_NAME);
                job->made[job->depth] = -1, job->depth++;
                return true;
        }
        gone = gone < 0 ? gone : file_remove_same(in, (string_address)job->at, 0, &facts);
        if (gone < 0)
        {
                files_move_fail(job, gone);
                return false;
        }
        return true;
}

/*
        A step of the move: a few things of the phase it is in, and back (a cancel is seen at each). True while there is more. The phases:
        the sources moved by rename where they can be (a move), and marked to be copied where they cannot; what is to be copied counted;
        each copied, and a moved one removed once its copy is whole; over.
*/
static b32 files_move_step(struct files_move address_to job)
{
        if (__atomic_load_n(&job->state, __ATOMIC_ACQUIRE) != FILES_JOB_RUNNING)
                return false;
        if (files_move_cancelled(job))
        {
                files_move_file_drop(job, false);
                __atomic_store_n(&job->state, FILES_JOB_CANCELLED, __ATOMIC_RELEASE);
                return false;
        }
        if (job->phase == FILES_MOVE_RENAME)
        {
                b32 numbered;
                bipolar moved;
                file_facts folder;

                if (job->next >= job->count)
                {
                        job->phase = FILES_MOVE_COUNT, job->next = 0;
                        return true;
                }
                if (!files_move_take(job, job->next))
                        return false;
                if (job->how == FILES_MOVE_MEASURE)
                {
                        job->copied[job->next++] = 1;
                        return true;
                }
                // A folder into itself, or into a folder inside it, is no copy and no move: nothing of it is made.
                if ((job->facts.mode & MODE_FORMAT) == MODE_DIRECTORY && files_move_within(job->into, &job->facts))
                {
                        files_move_fail(job, -FILES_MOVE_INSIDE);
                        return false;
                }
                job->copied[job->next] = 1;
                if (job->how == FILES_MOVE_MOVE)
                {
                        // Already in the folder: nothing to do. Else a rename where the name is free, and a copy where it crosses filesystems.
                        if (file_look_code(job->from, (string_address) "", AT_EMPTY_PATH, &folder) >= 0 && folder.inode == job->into_facts.inode &&
                            folder.device_major == job->into_facts.device_major && folder.device_minor == job->into_facts.device_minor)
                                job->copied[job->next] = 2;
                        else if (!files_move_free_name(job->into, job->name, job->named, &numbered))
                        {
                                files_move_fail(job, -ERROR_EXISTS);
                                return false;
                        }
                        else if ((moved = (bipolar)system_call_5(syscall(renameat2), (positive)job->from, (positive)job->name, (positive)job->into,
                                                                 (positive)job->named, FILE_RENAME_NOREPLACE)) >= 0)
                        {
                                job->copied[job->next] = 2, job->renamed += numbered;
                                string_copy_bounded((string_address)files_move_landed(job, job->next), (string_address)job->named, FILES_NAME);
                                __atomic_add_fetch(&job->items_total, 1, __ATOMIC_RELEASE);
                                __atomic_add_fetch(&job->items_done, 1, __ATOMIC_RELEASE);
                        }
                        else if (moved != -ERROR_CROSS_DEVICE)
                        {
                                files_move_fail(job, moved);
                                return false;
                        }
                }
                job->next++;
                return true;
        }
        // Counting and copying go source by source: each taken, then walked.
        if (job->phase == FILES_MOVE_COUNT || job->phase == FILES_MOVE_COPYING)
        {
                b32 counting = job->phase == FILES_MOVE_COUNT, numbered;

                if (job->depth || job->out >= 0)
                {
                        if (files_move_walk(job, counting))
                                return true;
                        if (__atomic_load_n(&job->state, __ATOMIC_ACQUIRE) != FILES_JOB_RUNNING)
                                return false;
                        // That source is counted, or whole: a moved one is removed now, else on to the next.
                        if (counting)
                                job->next++;
                        else if (job->how == FILES_MOVE_MOVE)
                                job->phase = FILES_MOVE_REMOVE, job->made_facts[0].mode = 0;
                        else
                                job->copied[job->next++] = 2;
                        return true;
                }
                while (job->next < job->count && job->copied[job->next] != 1)
                        job->next++;
                if (job->next >= job->count)
                {
                        job->next = 0;
                        job->phase = counting && job->how != FILES_MOVE_MEASURE ? FILES_MOVE_COPYING : FILES_MOVE_OVER;
                        if (job->phase == FILES_MOVE_OVER)
                                __atomic_store_n(&job->state, FILES_JOB_DONE, __ATOMIC_RELEASE);
                        return job->phase != FILES_MOVE_OVER;
                }
                if (!files_move_take(job, job->next))
                        return false;
                if (counting)
                {
                        if (!files_move_one(job, job->from, job->name, &job->facts, job->name, true))
                                return false;
                        if (!job->depth)
                                job->next++;
                        return true;
                }
                if (!files_move_free_name(job->into, job->name, job->named, &numbered))
                {
                        files_move_fail(job, -ERROR_EXISTS);
                        return false;
                }
                job->renamed += numbered;
                string_copy_bounded((string_address)files_move_landed(job, job->next), (string_address)job->named, FILES_NAME);
                if (!files_move_one(job, job->from, job->name, &job->facts, job->named, false))
                        return false;
                if (!job->depth && job->out < 0)
                {
                        // A link is whole at once (a moved one removed now), and something skipped stays where it is.
                        if (job->how == FILES_MOVE_MOVE && (job->facts.mode & MODE_FORMAT) == MODE_LINK)
                                job->phase = FILES_MOVE_REMOVE, job->made_facts[0].mode = 0;
                        else
                                job->copied[job->next++] = 2;
                }
                return true;
        }
        if (job->phase == FILES_MOVE_REMOVE)
        {
                if (files_move_remove(job))
                        return true;
                if (__atomic_load_n(&job->state, __ATOMIC_ACQUIRE) != FILES_JOB_RUNNING)
                        return false;
                job->copied[job->next++] = 2;
                job->phase = FILES_MOVE_COPYING;
                return true;
        }
        return false;
}

// The thread: steps to the end, or till it is stopped.
static fn files_move_run(address_any argument)
{
        struct files_move address_to job = (struct files_move address_to)argument;

        while (files_move_step(job))
                ;
}

/*
        The move begun on a thread of its own; where no thread can be made, the view steps it between its events (files_move_step, as
        the delete is stepped). True when a thread has it.
*/
static b32 files_move_start(struct files_move address_to job)
{
        if (job->state != FILES_JOB_RUNNING)
                return false;
        job->worker = thread_start(files_move_run, (address_any)job);
        return job->worker != null;
}

static fn files_move_cancel(struct files_move address_to job)
{
        __atomic_store_n(&job->cancel, 1, __ATOMIC_RELEASE);
}

// Whether the move is under way (on its thread, or waiting for the view's steps).
static b32 files_move_running(const struct files_move address_to job)
{
        return __atomic_load_n(&job->state, __ATOMIC_ACQUIRE) == FILES_JOB_RUNNING;
}

// The move let go of, once it is over (or stopped first): its thread joined, everything it holds closed, its memory given back. A move
// never begun (a view's at its start) is only made empty.
static fn files_move_end(struct files_move address_to job)
{
        if (job->begun)
        {
                if (job->worker)
                {
                        files_move_cancel(job);
                        thread_join(job->worker);
                }
                files_move_close(job);
                if (job->paths)
                        system_call_2(syscall(munmap), (positive)job->paths, files_move_bytes());
                if (job->buffer)
                        system_call_2(syscall(munmap), (positive)job->buffer, FILES_MOVE_BUFFER);
        }
        memory_fill(job, 0, sizeof(*job));
        job->into = job->from = job->in = job->out = job->out_folder = -1;
}

/* ---- the bin ---- */
/*
        The bin is the desktop's common one under the home folder (on the data partition): BIN/files holds what was put there, each
        under a name free there (files_move_free_name), and BIN/info/NAME.trashinfo says where it was and when:

            [Trash Info]
            Path=/root/a/one.txt
            DeletionDate=2026-11-18T17:45:00

        the path with every byte but letters, digits and / . - _ ~ as %XX. Putting is a rename on one filesystem (nothing is copied, so
        nothing is lost if it stops); one on another filesystem is refused (FILES_DID_CROSS), which a real delete (Shift+Delete) does.
        Restoring renames it back where it was, under a free name there if that is taken, and removes its info.
*/
#define FILES_DID_CROSS 9 // the bin is on another filesystem: nothing was moved

// A path's folders made where they are missing (mode 0700), as mkdir -p does.
static b32 files_bin_folders(const p8 address_to path)
{
        p8 at[FILES_PATH];
        positive length = string_length((string_address)path);

        if (!length || length >= FILES_PATH)
                return false;
        memory_copy(at, path, length + 1);
        for (positive k = 1; k <= length; k++)
                if (at[k] == '/' || at[k] == 0)
                {
                        p8 was = at[k];
                        bipolar made;

                        at[k] = 0;
                        made = (bipolar)system_call_3(syscall(mkdirat), AT_FDCWD, (positive)at, 0700);
                        if (made < 0 && made != -ERROR_EXISTS)
                                return false;
                        at[k] = was;
                }
        return true;
}

// The bin's own folder kind into into: BIN/files or BIN/info, with a name after it when one is given.
static b32 files_bin_join(p8 address_to into, const p8 address_to bin, const char address_to part, const p8 address_to name, const char address_to suffix)
{
        string_copy_bounded((string_address)into, (string_address)bin, FILES_PATH);
        string_append_bounded((string_address)into, (string_address)part, FILES_PATH);
        if (name)
        {
                string_append_bounded((string_address)into, (string_address) "/", FILES_PATH);
                string_append_bounded((string_address)into, (string_address)name, FILES_PATH);
        }
        if (suffix)
                string_append_bounded((string_address)into, (string_address)suffix, FILES_PATH);
        return string_length((string_address)into) + 1 < FILES_PATH;
}

// The last name of a path.
static const p8 address_to files_base_name(const p8 address_to path)
{
        const p8 address_to base = path;

        for (const p8 address_to c = path; *c; c++)
                if (*c == '/' && c[1])
                        base = c + 1;
        return base;
}

/*
        The entry at path (absolute) put in the bin, its info saying it was deleted at when ("YYYY-MM-DDThh:mm:ss"): FILES_DID with the
        name it has in BIN/files in named (FILES_NAME), FILES_DID_CROSS where the bin is on another filesystem, FILES_DID_FAILED else.
*/
static int files_bin_put(const p8 address_to bin, const p8 address_to path, const char address_to when, p8 address_to named)
{
        static const char hex[] = "0123456789ABCDEF";
        p8 files_dir[FILES_PATH], into[FILES_PATH], info[FILES_PATH], text[FILES_PATH * 3 + 96];
        positive used = 0;
        b32 numbered;
        bipolar folder, moved, out;

        if (!files_bin_join(files_dir, bin, "/files", null, null) || !files_bin_folders(files_dir) || !files_bin_join(info, bin, "/info", null, null) ||
            !files_bin_folders(info))
                return FILES_DID_FAILED;
        if ((folder = system_open_at(AT_FDCWD, (string_address)files_dir, FILE_READ | O_DIRECTORY | O_CLOEXEC)) < 0)
                return FILES_DID_FAILED;
        // The info first, under the name the entry will have: a name whose info could not be made is never taken.
        for (int tries = 0; tries < 8; tries++)
        {
                if (!files_move_free_name(folder, files_base_name(path), named, &numbered) || !files_bin_join(info, bin, "/info", named, ".trashinfo"))
                {
                        system_close(folder);
                        return FILES_DID_FAILED;
                }
                out = system_open_at_mode(AT_FDCWD, (string_address)info, FILE_WRITE | FILE_EXCLUSIVE | O_NOFOLLOW | O_CLOEXEC, 0600);
                if (out >= 0)
                        break;
        }
        if (out < 0)
        {
                system_close(folder);
                return FILES_DID_FAILED;
        }
        memory_copy(text, "[Trash Info]\nPath=", 18), used = 18;
        for (const p8 address_to c = path; *c && used + 4 < sizeof(text) - 64; c++)
                if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '/' || *c == '.' || *c == '-' || *c == '_' || *c == '~')
                        text[used++] = *c;
                else
                        text[used++] = '%', text[used++] = (p8)hex[*c >> 4], text[used++] = (p8)hex[*c & 15];
        memory_copy(text + used, "\nDeletionDate=", 14), used += 14;
        for (const char address_to c = when; *c && used + 2 < sizeof(text); c++)
                text[used++] = (p8)*c;
        text[used++] = '\n';
        system_write_once((b32)out, text, used);
        system_close(out);
        files_bin_join(into, bin, "/files", named, null);
        moved = (bipolar)system_call_5(syscall(renameat2), AT_FDCWD, (positive)path, AT_FDCWD, (positive)into, FILE_RENAME_NOREPLACE);
        system_close(folder);
        if (moved < 0)
        {
                system_remove_at(AT_FDCWD, (string_address)info, 0);
                return moved == -ERROR_CROSS_DEVICE ? FILES_DID_CROSS : FILES_DID_FAILED;
        }
        return FILES_DID;
}

// Where an entry of the bin (its name in BIN/files) was, from its info, %XX made bytes again: false when it has no info or none says.
static b32 files_bin_was(const p8 address_to bin, const p8 address_to name, p8 address_to path)
{
        p8 info[FILES_PATH], text[FILES_PATH * 3 + 128];
        bipolar got;
        positive at = 0, out = 0;

        if (!files_bin_join(info, bin, "/info", name, ".trashinfo"))
                return false;
        got = file_slurp_once_at(AT_FDCWD, (string_address)info, text, sizeof(text) - 1);
        if (got <= 0)
                return false;
        text[got] = 0;
        for (; at + 5 < (positive)got; at++)
                if ((at == 0 || text[at - 1] == '\n') && !memory_compare(text + at, "Path=", 5))
                        break;
        if (at + 5 >= (positive)got)
                return false;
        for (at += 5; text[at] && text[at] != '\n' && out + 1 < FILES_PATH; at++)
        {
                p8 c = text[at];

                if (c == '%' && text[at + 1] && text[at + 2])
                {
                        int high = text[at + 1] <= '9' ? text[at + 1] - '0' : (text[at + 1] | 32) - 'a' + 10;
                        int low = text[at + 2] <= '9' ? text[at + 2] - '0' : (text[at + 2] | 32) - 'a' + 10;

                        c = (p8)(high << 4 | low), at += 2;
                }
                path[out++] = c;
        }
        path[out] = 0;
        return out > 1 && path[0] == '/';
}

/*
        The entry of the bin named name put back where it was: FILES_DID with the path it has now in back (FILES_PATH; a free name in its
        folder if its own is taken, the folder made if it is gone), FILES_DID_FAILED else.
*/
static int files_bin_restore(const p8 address_to bin, const p8 address_to name, p8 address_to back)
{
        p8 from[FILES_PATH], info[FILES_PATH], folder_path[FILES_PATH], free[FILES_NAME];
        positive cut;
        b32 numbered;
        bipolar folder, moved;

        if (!files_bin_was(bin, name, back) || !files_bin_join(from, bin, "/files", name, null) || !files_bin_join(info, bin, "/info", name, ".trashinfo"))
                return FILES_DID_FAILED;
        string_copy_bounded((string_address)folder_path, (string_address)back, FILES_PATH);
        cut = (positive)(files_base_name(folder_path) - folder_path);
        folder_path[cut > 1 ? cut - 1 : 1] = 0;
        if (!files_bin_folders(folder_path) || (folder = system_open_at(AT_FDCWD, (string_address)folder_path, FILE_READ | O_DIRECTORY | O_CLOEXEC)) < 0)
                return FILES_DID_FAILED;
        if (!files_move_free_name(folder, files_base_name(back), free, &numbered))
        {
                system_close(folder);
                return FILES_DID_FAILED;
        }
        moved = (bipolar)system_call_5(syscall(renameat2), AT_FDCWD, (positive)from, (positive)folder, (positive)free, FILE_RENAME_NOREPLACE);
        system_close(folder);
        if (moved < 0)
                return FILES_DID_FAILED;
        system_remove_at(AT_FDCWD, (string_address)info, 0);
        back[cut] = 0;
        string_append_bounded((string_address)back, (string_address)free, FILES_PATH);
        return FILES_DID;
}

#endif // MOONWATER_FILES_MODEL
