/*
        The file browser's model (src/canvas/ui/files_model.c): bounded-step folder listing, sorting/filtering,
        selection, history, kept-listing cache, trail, recursive delete/copy/move jobs, sidebar folder/drive tree,
        trash bin, and git index/ignore marks. Pure model with no UI kit dependency.
        Disk invariants: names are raw kernel bytes; entries are stat'd with AT_SYMLINK_NOFOLLOW and opened with
        O_NOFOLLOW held to device+inode (file_walk_open_found, file_open_same, file_remove_same); only user-named
        paths follow symlinks.
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
        p32 listed;              // counted up each time a listing is whole (read or kept): what is worked out of a listing is kept against it
        p32 reads;               // the readings of a folder begun, ever: a kept listing shown begins none
        bipolar seat;            // where a view has the rows of the folder shown (its scroll): kept with a listing and given back with it
        b32 restored;            // the folder shown came from a kept listing, as it was left, and was not read
        struct files_kept kept[FILES_KEPT];
        p64 kept_clock;
        address_any mapped;
        positive mapped_size;
};

/* ---- the memory ---- */

static address_any files_anon_map(positive bytes, bipolar address_to error)
{
        bipolar mapped = (bipolar)system_call_6(syscall(mmap), 0, bytes, 3, 0x22, (positive)-1, 0);

        if (mapped < 0 && mapped > -4096)
        {
                if (error)
                        *error = mapped;
                return null;
        }
        return (address_any)mapped;
}

static inline fn files_unmap(address_any ptr, positive bytes)
{
        if (ptr)
                system_call_2(syscall(munmap), (positive)ptr, bytes);
}

static inline fn files_close(bipolar address_to fd)
{
        if (*fd >= 0)
                system_close(*fd), *fd = -1;
}

static inline bipolar files_look_fd(bipolar fd, file_facts address_to facts)
{
        return file_look_code(fd, (string_address) "", AT_EMPTY_PATH, facts);
}

static inline bipolar files_look_at(bipolar dir, const p8 address_to name, file_facts address_to facts)
{
        return file_look_code(dir, (string_address)name, AT_SYMLINK_NOFOLLOW, facts);
}

static inline bipolar files_open_dir(bipolar dir, const p8 address_to path, p32 extra)
{
        return system_open_at(dir, (string_address)path, FILE_READ | O_DIRECTORY | O_CLOEXEC | extra);
}

static inline b32 files_same_file(p64 inode, p32 major, p32 minor, const file_facts address_to facts)
{
        return inode == facts->inode && major == facts->device_major && minor == facts->device_minor;
}

static inline b32 files_same_facts(const file_facts address_to a, const file_facts address_to b)
{
        return files_same_file(a->inode, a->device_major, a->device_minor, b);
}

static b32 files_path_join(p8 address_to into, positive room, const p8 address_to folder, const p8 address_to name)
{
        positive flen = string_length((string_address)folder), nlen = string_length((string_address)name);
        b32 slash = flen > 1 && folder[flen - 1] != '/';

        if (flen + slash + nlen >= room)
                return false;
        memory_copy(into, folder, flen);
        if (slash)
                into[flen++] = '/';
        memory_copy(into + flen, name, nlen + 1);
        return true;
}

#define FILES_MODEL_BYTES                                                                                                  \
        (2 * ((positive)FILES_MOST * sizeof(struct files_entry) + FILES_NAMES + (positive)FILES_MOST * sizeof(p32)) +      \
         FILES_MOST / 8 + 2 * (positive)FILES_HISTORY * FILES_PATH)

// The model's memory: one anonymous mapping, paged in lazily by the kernel.
static b32 files_model_open(struct files_model address_to m)
{
        p8 address_to at;

        memory_fill(m, 0, sizeof(*m));
        m->handle = -1, m->reader.handle = -1, m->cursor = FILES_NO, m->anchor = FILES_NO;
        if (!(at = (p8 address_to)files_anon_map(FILES_MODEL_BYTES, null)))
                return false;
        m->mapped = at, m->mapped_size = FILES_MODEL_BYTES;
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

static fn files_walk_stop(struct files_model address_to m)
{
        if (m->walking)
                file_walk_close(&m->reader);
        m->walking = false;
}

static fn files_model_close(struct files_model address_to m)
{
        for (int k = 0; k < FILES_KEPT; k++)
                files_keep_drop(m, k);
        files_walk_stop(m);
        files_close(&m->handle);
        files_unmap(m->mapped, m->mapped_size);
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
        p32 fmt = mode & MODE_FORMAT;

        return fmt == MODE_DIRECTORY ? FILES_KIND_DIRECTORY : fmt == MODE_LINK ? FILES_KIND_LINK : fmt == MODE_FILE ? FILES_KIND_FILE : FILES_KIND_OTHER;
}

static p8 files_kind_of_record(p8 type)
{
        return type == DT_DIR ? FILES_KIND_DIRECTORY : type == DT_LNK ? FILES_KIND_LINK : type == DT_REG ? FILES_KIND_FILE : FILES_KIND_OTHER;
}

/* ---- the order ---- */

// Case-folded ASCII order, tie-broken by raw bytes so no two distinct names compare equal.
static int files_name_order(const p8 address_to a, const p8 address_to b)
{
        b32 folded = string_compare_folded((string_address)a, (string_address)b);

        if (!folded)
                folded = string_compare((string_address)a, (string_address)b);
        return folded < 0 ? -1 : folded > 0;
}

// Index of the last extension dot (ignoring a leading dot), or length when there is none.
static positive files_name_dot(const p8 address_to name, positive length)
{
        for (positive at = length; at > 1; at--)
                if (name[at - 1] == '.')
                        return at - 1;
        return length;
}

struct files_order
{
        const struct files_listing address_to listing;
        int sort;
        b32 descending;
};

// Folders first, then sort key (flipped when descending), then name tie-breaker.
static b32 files_order_compare(address_any left, address_any right, address_any context)
{
        const struct files_order address_to o = (const struct files_order address_to)context;
        const struct files_entry address_to a = &o->listing->entry[*(const p32 address_to)left];
        const struct files_entry address_to b = &o->listing->entry[*(const p32 address_to)right];
        const p8 address_to na = o->listing->names + a->name, address_to nb = o->listing->names + b->name;
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
        {
                positive da = files_name_dot(na, a->length), db = files_name_dot(nb, b->length);

                key = files_name_order(na + (da < a->length ? da + 1 : da), nb + (db < b->length ? db + 1 : db));
        }
        if (!key)
                key = files_name_order(na, nb);
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

static p32 files_order_place(const struct files_listing address_to l, p32 entry)
{
        for (p32 place = 0; entry != FILES_NO && place < l->places; place++)
                if (l->order[place] == entry)
                        return place;
        return FILES_NO;
}

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

static fn files_model_choose(struct files_model address_to m, p32 place, int how)
{
        p32 entry;

        if (place >= m->shown.places)
                return;
        entry = m->shown.order[place];
        if (how == FILES_CHOOSE_TOGGLE)
        {
                files_chosen_put(m, entry, !(m->chosen[entry >> 3] >> (entry & 7) & 1));
                m->anchor = entry;
        }
        else
        {
                p32 from = how == FILES_CHOOSE_RANGE && m->anchor != FILES_NO ? files_order_place(&m->shown, m->anchor) : place;
                p32 low, high;

                if (how != FILES_CHOOSE_RANGE || m->anchor == FILES_NO)
                        m->anchor = entry;
                if (from == FILES_NO)
                        from = place;
                low = from < place ? from : place, high = from < place ? place : from;
                memory_fill(m->chosen, 0, FILES_MOST / 8), m->chosen_count = 0;
                for (p32 at = low; at <= high; at++)
                        files_chosen_put(m, m->shown.order[at], true);
        }
        m->cursor = place;
        m->revision++;
}

static fn files_select_all(struct files_model address_to m)
{
        if (!files_model_count(m))
                return;
        files_model_choose(m, 0, FILES_CHOOSE_ONLY);
        files_model_choose(m, files_model_count(m) - 1, FILES_CHOOSE_RANGE);
}

// Next chosen place (or cursor alone when nothing is chosen); advances *at, returns FILES_NO at end.
static p32 files_selected_next(const struct files_model address_to m, p32 address_to at)
{
        p32 count = files_model_count(m);

        if (!m->chosen_count)
        {
                p32 cur = *at == 0 && m->cursor < count ? m->cursor : FILES_NO;

                *at = count;
                return cur;
        }
        while (*at < count)
        {
                p32 place = (*at)++;

                if (files_model_chosen(m, place))
                        return place;
        }
        return FILES_NO;
}

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

// Re-sort and re-filter the shown listing, keeping cursor on its entry (or 0 if filtered out).
static fn files_model_order(struct files_model address_to m)
{
        p32 on = m->cursor < m->shown.places ? m->shown.order[m->cursor] : FILES_NO, place;

        files_listing_sort(&m->shown, m->sort, m->descending);
        files_listing_filter(&m->shown, m->filter, m->filter_length);
        place = files_order_place(&m->shown, on);
        m->cursor = place != FILES_NO ? place : m->shown.places ? 0 : FILES_NO;
        m->revision++;
}

static fn files_model_sort(struct files_model address_to m, int sort, b32 descending)
{
        m->sort = sort, m->descending = descending;
        if (m->state == FILES_READY)
                files_model_order(m);
}

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

static fn files_model_fail(struct files_model address_to m, bipolar error, b32 clear)
{
        if (clear)
        {
                files_listing_empty(&m->shown);
                files_model_choose_none(m);
                m->reloading = false, m->cursor = FILES_NO;
        }
        m->state = files_state_of(error), m->error = m->state == FILES_FAILED ? error : 0;
}

// One record of the walk into a listing: false when the listing is full (it is cut there).
static b32 files_listing_take(struct files_model address_to m, struct files_listing address_to l, const struct linux_dirent64 address_to record)
{
        positive length = string_length((string_address)record->d_name);
        struct files_entry address_to entry;
        file_facts facts;
        bipolar looked;

        if (file_is_dot((string_address)record->d_name) || (record->d_name[0] == '.' && !m->hidden) || !length || length >= FILES_NAME)
                return true;
        looked = files_look_at(m->reader.handle, (const p8 address_to)record->d_name, &facts);
        if (looked == -ERROR_NO_ENTRY)
                return true;
        if (l->count >= FILES_MOST || l->names_used + length + 1 > FILES_NAMES)
                return l->cut = true, false;
        entry = &l->entry[l->count];
        memory_fill(entry, 0, sizeof(*entry));
        entry->name = l->names_used, entry->length = (p16)length;
        memory_copy(l->names + l->names_used, record->d_name, length + 1);
        l->names_used += (p32)length + 1;
        if (looked == 0)
        {
                entry->kind = files_kind_of(facts.mode), entry->looked = 1, entry->mode = (p16)facts.mode;
                entry->size = facts.size, entry->modified = facts.modified.seconds, entry->inode = facts.inode;
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
        l->order[l->count] = l->count;
        l->places = ++l->count;
        return true;
}

static fn files_model_finish(struct files_model address_to m, bipolar error)
{
        struct files_listing address_to l = m->reloading ? &m->next : &m->shown;

        files_walk_stop(m);
        if (error < 0 && !l->count)
        {
                files_model_fail(m, error, true);
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
                files_model_choose_none(m);
        }
        m->state = FILES_READY, m->error = 0, m->listed++;
        m->cursor = m->came[0] ? files_model_find(m, m->came) : FILES_NO;
        if (m->cursor == FILES_NO && m->shown.places)
                m->cursor = 0;
        m->came[0] = end;
        m->revision++;
}

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
                m->revision++;
        return true;
}

static b32 files_model_begin(struct files_model address_to m)
{
        files_walk_stop(m);
        if (files_look_fd(m->handle, &m->folder) < 0)
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

        files_unmap(k->mapped, k->bytes);
        k->mapped = null, k->bytes = 0, k->path[0] = end, k->count = 0;
}

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

static p8 address_to files_keep_listing(const struct files_kept address_to k, struct files_listing address_to l)
{
        p8 address_to at = (p8 address_to)k->mapped;

        l->entry = (struct files_entry address_to)at, at += (positive)k->count * sizeof(struct files_entry);
        l->order = (p32 address_to)at, at += (positive)k->count * sizeof(p32);
        l->names = at, at += k->names_used;
        l->count = k->count, l->names_used = k->names_used, l->cut = k->cut;
        return at;
}

static fn files_keep_copy(const struct files_kept address_to k, struct files_model address_to m, b32 save)
{
        struct files_listing kl;
        p8 address_to chosen = files_keep_listing(k, &kl);
        positive eb = (positive)k->count * sizeof(struct files_entry), ob = (positive)k->count * sizeof(p32), cb = (k->count + 7) / 8;

        memory_copy(save ? kl.entry : m->shown.entry, save ? m->shown.entry : kl.entry, eb);
        memory_copy(save ? kl.order : m->shown.order, save ? m->shown.order : kl.order, ob);
        memory_copy(save ? kl.names : m->shown.names, save ? m->shown.names : kl.names, k->names_used);
        memory_copy(save ? chosen : m->chosen, save ? m->chosen : chosen, cb);
}

static int files_keep_oldest(const struct files_model address_to m, int but)
{
        int oldest = -1;

        for (int k = 0; k < FILES_KEPT; k++)
                if (k != but && m->kept[k].path[0] && (oldest < 0 || m->kept[k].left < m->kept[oldest].left))
                        oldest = k;
        return oldest;
}

static fn files_keep_put(struct files_model address_to m)
{
        positive bytes = files_keep_bytes(m->shown.count, m->shown.names_used), held = 0;
        int which = files_keep_find(m, m->path);
        struct files_kept address_to k;
        address_any mapped = null;

        if (which >= 0)
                files_keep_drop(m, which);
        if (m->state != FILES_READY || m->handle < 0 || bytes > FILES_KEPT_BYTES)
                return;
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
        if (bytes && !(mapped = files_anon_map(bytes, null)))
                return;
        k = &m->kept[which];
        string_copy_bounded(k->path, m->path, FILES_PATH);
        k->mapped = mapped, k->bytes = bytes;
        k->count = m->shown.count, k->names_used = m->shown.names_used, k->cut = m->shown.cut;
        k->folder = m->folder, k->sort = m->sort, k->descending = m->descending, k->hidden = m->hidden;
        k->cursor = m->cursor, k->anchor = m->anchor, k->chosen_count = m->chosen_count, k->seat = m->seat;
        k->left = ++m->kept_clock;
        if (bytes)
                files_keep_copy(k, m, true);
}

static fn files_keep_take(struct files_model address_to m, int which)
{
        struct files_kept address_to k = &m->kept[which];

        m->shown.count = m->shown.places = k->count, m->shown.names_used = k->names_used, m->shown.cut = k->cut;
        if (k->bytes)
                files_keep_copy(k, m, false);
        m->chosen_count = k->chosen_count, m->anchor = k->anchor, m->cursor = k->cursor < k->count ? k->cursor : FILES_NO;
        m->folder = k->folder, m->seat = k->seat;
        if (k->sort != m->sort || k->descending != m->descending)
        {
                p32 on = m->cursor != FILES_NO ? m->shown.order[m->cursor] : FILES_NO;

                files_listing_sort(&m->shown, m->sort, m->descending);
                m->cursor = files_order_place(&m->shown, on);
        }
        files_keep_drop(m, which);
}

static fn files_model_show(struct files_model address_to m, string_address path, bipolar handle, bipolar error)
{
        file_facts now;
        int kept;

        files_walk_stop(m);
        m->reloading = false;
        files_keep_put(m);
        m->filter_length = 0, m->filter[0] = end;
        files_close(&m->handle);
        m->handle = handle;
        if ((const p8 address_to)path != m->path)
                string_copy_bounded(m->path, path, FILES_PATH);
        files_listing_empty(&m->shown), files_listing_empty(&m->next);
        files_model_choose_none(m);
        m->cursor = FILES_NO, m->seat = 0, m->restored = false;
        if ((kept = files_keep_find(m, m->path)) >= 0)
        {
                const struct files_kept address_to k = &m->kept[kept];

                if (handle >= 0 && k->hidden == m->hidden && files_look_fd(handle, &now) >= 0 && files_same_facts(&k->folder, &now))
                {
                        files_keep_take(m, kept);
                        m->state = FILES_READY, m->error = 0, m->restored = true, m->came[0] = end, m->listed++;
                        m->revision++;
                        return;
                }
                files_keep_drop(m, kept);
        }
        m->state = FILES_READING, m->error = 0;
        if (handle < 0 || !files_model_begin(m))
                files_model_fail(m, handle < 0 ? error : m->reader.error, false);
        m->revision++;
}

static fn files_model_open_path(struct files_model address_to m, string_address path, b32 commit);

static fn files_model_reload(struct files_model address_to m)
{
        const struct files_entry address_to on = files_model_entry(m, m->cursor);

        if (m->handle < 0)
        {
                files_model_open_path(m, m->path, false);
                return;
        }
        if (on && !m->came[0])
                string_copy_bounded(m->came, (string_address)files_model_name(m, on), FILES_NAME);
        files_listing_empty(&m->next);
        m->reloading = m->state == FILES_READY;
        if (!m->reloading)
                files_listing_empty(&m->shown), m->state = FILES_READING;
        if (!files_model_begin(m))
        {
                m->reloading = false;
                files_listing_empty(&m->shown);
                files_model_fail(m, m->reader.error, false);
        }
        m->revision++;
}

// FILES_LOST (state -> FILES_GONE), FILES_STALE (dir timestamps changed), or FILES_FRESH.
static int files_model_stale(struct files_model address_to m)
{
        file_facts now, named;

        if (m->handle < 0 || m->state == FILES_GONE)
                return m->state == FILES_GONE ? FILES_LOST : FILES_FRESH;
        if (files_look_fd(m->handle, &now) < 0 || !now.hard_links ||
            file_look_code(AT_FDCWD, m->path, 0, &named) < 0 || !files_same_facts(&now, &named))
        {
                files_walk_stop(m);
                files_model_fail(m, -ERROR_NO_ENTRY, true);
                m->revision++;
                return FILES_LOST;
        }
        if (now.modified.seconds != m->folder.modified.seconds || now.modified.nanoseconds != m->folder.modified.nanoseconds ||
            now.changed.seconds != m->folder.changed.seconds || now.changed.nanoseconds != m->folder.changed.nanoseconds)
                return FILES_STALE;
        return FILES_FRESH;
}

/* ---- acting on what was listed ---- */

// Re-stat a listed entry through m->handle without following: FILES_SAME / FILES_CHANGED / FILES_MISSING / FILES_REFUSED.
static int files_model_check(struct files_model address_to m, p32 place, file_facts address_to now)
{
        struct files_entry address_to entry = place < m->shown.places ? &m->shown.entry[m->shown.order[place]] : null;
        file_facts facts;
        bipolar looked;

        if (!entry || m->handle < 0)
                return FILES_REFUSED;
        looked = files_look_at(m->handle, m->shown.names + entry->name, &facts);
        if (now)
                *now = facts;
        if (looked < 0)
                return looked == -ERROR_NO_ENTRY || looked == -ERROR_NOT_DIRECTORY ? FILES_MISSING : FILES_CHANGED;
        if (files_kind_of(facts.mode) != entry->kind ||
            (entry->looked && (!files_same_file(entry->inode, entry->device_major, entry->device_minor, &facts) ||
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
        if (parent != path)
                memory_copy(parent, path, cut);
        parent[cut] = end;
        return true;
}

static fn files_history_push(p8 address_to stack, p32 address_to count, const p8 address_to path)
{
        if (*count == FILES_HISTORY)
        {
                for (p32 at = 0; at + 1 < FILES_HISTORY; at++)
                        memory_copy(stack + (positive)at * FILES_PATH, stack + (positive)(at + 1) * FILES_PATH, FILES_PATH);
                (*count)--;
        }
        string_copy_bounded(stack + (positive)*count * FILES_PATH, (string_address)path, FILES_PATH);
        (*count)++;
}

static fn files_model_commit(struct files_model address_to m, string_address path, bipolar handle, bipolar error)
{
        if (m->state != FILES_NONE)
                files_history_push(m->back, &m->back_count, m->path);
        m->forward_count = 0;
        m->came[0] = end;
        files_model_show(m, path, handle, error);
}

static fn files_model_open_path(struct files_model address_to m, string_address path, b32 commit)
{
        file_walk opened;

        file_walk_open(&opened, AT_FDCWD, path);
        if (commit)
                files_model_commit(m, path, opened.handle, opened.error);
        else
                files_model_show(m, path, opened.handle, opened.error);
}

static b32 files_model_go(struct files_model address_to m, string_address path)
{
        p8 taken[FILES_PATH];

        if (!files_path_take(taken, path))
                return false;
        files_model_open_path(m, taken, true);
        return true;
}

// Open a user-typed path; only switches folder on success (returns FILES_READING), else returns failure state unchanged.
static int files_model_try(struct files_model address_to m, string_address path)
{
        p8 taken[FILES_PATH];
        file_walk opened;

        if (!files_path_take(taken, path))
                return FILES_NOT_DIRECTORY;
        if (!file_walk_open(&opened, AT_FDCWD, taken))
                return files_state_of(opened.error);
        files_model_commit(m, taken, opened.handle, 0);
        return FILES_READING;
}

// Complete a directory-name prefix against a listing in memory; returns match count and writes longest shared prefix.
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

static fn files_model_take(struct files_model address_to m, string_address path, bipolar handle)
{
        files_model_commit(m, path, handle, 0);
}

// Enter a listed directory (re-checked and opened with O_NOFOLLOW held to its inode).
static int files_model_enter(struct files_model address_to m, p32 place)
{
        const struct files_entry address_to entry = files_model_entry(m, place);
        p8 into[FILES_PATH];
        file_walk opened;
        int seen;

        if (!entry)
                return FILES_REFUSED;
        if ((seen = files_model_check(m, place, null)) != FILES_SAME)
                return seen;
        if (entry->kind != FILES_KIND_DIRECTORY || !files_path_join(into, FILES_PATH, m->path, files_model_name(m, entry)))
                return FILES_REFUSED;
        if (!file_walk_open_found(&opened, m->handle, (string_address)files_model_name(m, entry)))
                return opened.error == -ERROR_NO_ENTRY ? FILES_MISSING : FILES_CHANGED;
        files_model_commit(m, into, opened.handle, 0);
        return FILES_SAME;
}

static fn files_model_came(struct files_model address_to m, const p8 address_to name)
{
        p32 place = m->restored && name[0] ? files_model_find(m, name) : FILES_NO;

        if (!m->restored)
                string_copy_bounded(m->came, (string_address)name, FILES_NAME);
        else if (place != FILES_NO && place != m->cursor)
                m->cursor = place, m->revision++;
}

static b32 files_model_up(struct files_model address_to m)
{
        p8 parent[FILES_PATH], name[FILES_NAME];

        if (m->state == FILES_NONE || !files_path_parent(m->path, parent, name))
                return false;
        files_model_open_path(m, parent, true);
        files_model_came(m, name);
        return true;
}

static b32 files_model_back(struct files_model address_to m, b32 forward)
{
        p8 address_to from = forward ? m->forward : m->back, address_to onto = forward ? m->back : m->forward;
        p32 address_to from_count = forward ? &m->forward_count : &m->back_count, address_to onto_count = forward ? &m->back_count : &m->forward_count;
        p8 to[FILES_PATH], name[FILES_NAME], parent[FILES_PATH];

        if (!*from_count)
                return false;
        (*from_count)--;
        string_copy_bounded(to, (string_address)(from + (positive)*from_count * FILES_PATH), FILES_PATH);
        files_history_push(onto, onto_count, m->path);
        if (!files_path_parent(m->path, parent, name) || !string_equals(parent, to))
                name[0] = end;
        files_model_open_path(m, to, false);
        files_model_came(m, name);
        return true;
}

/* ---- the trail ---- */
// Deepest path visited on the current branch, remembering each crumb's device+inode for verified re-entry.
#define FILES_TRAIL 48

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

static int files_trail_index(const struct files_trail address_to t, const p8 address_to path, positive length)
{
        for (int k = 0; length && k < t->count; k++)
                if (t->to[k] == length && !memory_compare((address_any)t->path, (address_any)path, length))
                        return k;
        return -1;
}

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

static b32 files_trail_note(struct files_trail address_to t, const p8 address_to shown, const file_facts address_to folder)
{
        positive length = string_length((string_address)shown);
        int place = files_trail_index(t, shown, length);

        if (place < 0)
        {
                files_trail_become(t, shown);
                place = t->count - 1;
        }
        t->current = place < 0 ? 0 : place;
        for (int k = 0; k <= t->current && k < t->count; k++)
                t->lost[k] = 0;
        if (folder && place >= 0 && folder->inode)
                t->known[place] = 1, t->inode[place] = folder->inode, t->device_major[place] = folder->device_major, t->device_minor[place] = folder->device_minor;
        return t->to[t->count ? t->count - 1 : 0] != length;
}

static fn files_trail_path(const struct files_trail address_to t, int which, p8 address_to into)
{
        positive length = which >= 0 && which < t->count ? t->to[which] : 0;

        memory_copy(into, t->path, length);
        into[length] = end;
}

// Open trail crumb `which` with O_NOFOLLOW and verify its device+inode on the opened fd.
static int files_trail_open(const struct files_trail address_to t, int which, bipolar address_to handle, bipolar address_to error)
{
        p8 path[FILES_PATH];
        file_facts facts;
        bipolar opened;

        *handle = -1, *error = 0;
        if (which < 0 || which >= t->count)
                return FILES_REFUSED;
        files_trail_path(t, which, path);
        opened = files_open_dir(AT_FDCWD, path, O_NOFOLLOW);
        if (opened < 0)
        {
                if (opened == -ERROR_NO_ENTRY)
                        return FILES_MISSING;
                if (opened == -ERROR_NOT_DIRECTORY || opened == -40)
                        return files_look_at(AT_FDCWD, path, &facts) < 0 ? FILES_MISSING : FILES_CHANGED;
                return *error = opened, FILES_REFUSED;
        }
        if (files_look_fd(opened, &facts) < 0 ||
            (t->known[which] && !files_same_file(t->inode[which], t->device_major[which], t->device_minor[which], &facts)))
        {
                system_close(opened);
                return FILES_CHANGED;
        }
        *handle = opened;
        return FILES_SAME;
}

static int files_trail_check(const struct files_trail address_to t, int which)
{
        bipolar handle, error;
        int seen = files_trail_open(t, which, &handle, &error);

        files_close(&handle);
        return seen == FILES_REFUSED && error ? FILES_CHANGED : seen;
}

static fn files_trail_cut(struct files_trail address_to t, int which)
{
        if (which <= t->current || which >= t->count)
                return;
        t->path[t->to[which - 1]] = end;
        t->count = which;
}

static fn files_trail_lose(struct files_trail address_to t, int which)
{
        for (int k = which > t->current ? which : t->current + 1; k < t->count; k++)
                t->lost[k] = 1;
}

static fn files_keep_trim(struct files_model address_to m, const struct files_trail address_to t)
{
        for (int k = 0; k < FILES_KEPT; k++)
        {
                positive length = string_length((string_address)m->kept[k].path);

                if (length && files_trail_index(t, m->kept[k].path, length) < 0)
                        files_keep_drop(m, k);
        }
}

// Replace crumb `which` with `text`, collapsing repeated slashes and rejecting control chars or `.`/`..` segments.
static b32 files_trail_spell(const struct files_trail address_to t, int which, const p8 address_to text, p8 address_to into)
{
        positive length = string_length((string_address)text), used = 0, whole = string_length((string_address)t->path);
        const p8 address_to from[3] = {t->path, text, t->path + (which >= 0 && which < t->count ? t->to[which] : 0)};
        positive lens[3] = {which >= 0 && which < t->count ? t->from[which] : 0, length, which >= 0 && which < t->count ? whole - t->to[which] : 0};

        if (which < 0 || which >= t->count || t->to[which] <= 1 || (which == 0 && t->from[0] == 0))
                return false;
        for (positive at = 0; at < length; at++)
                if (text[at] < 0x20 || text[at] == 0x7f)
                        return false;
        for (int part = 0; part < 3; part++)
                for (positive at = 0; at < lens[part]; at++)
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

// Set trail to `path` and show its deepest existing ancestor, marking deeper crumbs lost.
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
// Disk mutations through m->handle (O_NOFOLLOW / RENAME_NOREPLACE / file_remove_same); recursive delete runs in bounded steps.
#define FILES_JOB_DEEP 64

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
        p32 address_to offsets;       // byte offset of each name in names (O(1) lookup)
        file_facts address_to facts;  // and each as it was looked at when the job began
        bipolar stack[FILES_JOB_DEEP]; // the folders it is inside, the innermost last
        p8 inside[FILES_JOB_DEEP][FILES_NAME]; // and the name each has in the one before it
        int depth;
};

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

static inline int files_did_code(bipolar res)
{
        return res >= 0 ? FILES_DID : res == -ERROR_EXISTS ? FILES_DID_EXISTS : FILES_DID_FAILED;
}

static int files_model_make_folder(struct files_model address_to m, const p8 address_to name)
{
        if (m->handle < 0 || !files_name_good(name))
                return FILES_DID_BAD;
        return files_did_code((bipolar)system_call_3(syscall(mkdirat), (positive)m->handle, (positive)name, 0755));
}

static int files_model_rename(struct files_model address_to m, p32 place, const p8 address_to name)
{
        const struct files_entry address_to entry = files_model_entry(m, place);

        if (!entry || !files_name_good(name) || files_model_check(m, place, null) != FILES_SAME)
                return FILES_DID_BAD;
        return files_did_code((bipolar)system_call_5(syscall(renameat2), (positive)m->handle, (positive)files_model_name(m, entry),
                                                     (positive)m->handle, (positive)name, FILE_RENAME_NOREPLACE));
}

#define FILES_JOB_NAMES ((positive)FILES_MOST * FILES_NAME / 8)
#define FILES_JOB_BYTES (FILES_JOB_NAMES + (positive)FILES_MOST / 8 * (sizeof(file_facts) + sizeof(p32)))

static fn files_job_close(struct files_job address_to job)
{
        while (job->depth > 0)
                system_close(job->stack[--job->depth]);
}

static fn files_job_end(struct files_job address_to job)
{
        files_job_close(job);
        files_unmap(job->names, FILES_JOB_BYTES);
        memory_fill(job, 0, sizeof(*job));
}

static fn files_job_cancel(struct files_job address_to job)
{
        if (job->state != FILES_JOB_RUNNING)
                return;
        files_job_close(job);
        job->state = FILES_JOB_CANCELLED;
}

// Re-check and snapshot selected entries (up to FILES_MOST/8) before starting a stepped recursive delete.
static b32 files_delete_begin(struct files_model address_to m, struct files_job address_to job)
{
        bipolar err = 0;
        positive used = 0;
        p32 iter = 0, place;

        files_job_end(job);
        if (!(job->names = (p8 address_to)files_anon_map(FILES_JOB_BYTES, &err)))
                return job->state = FILES_JOB_FAILED, job->error = err, false;
        job->facts = (file_facts address_to)(job->names + FILES_JOB_NAMES);
        job->offsets = (p32 address_to)(job->facts + FILES_MOST / 8);
        while ((place = files_selected_next(m, &iter)) != FILES_NO)
        {
                const struct files_entry address_to entry = &m->shown.entry[m->shown.order[place]];

                if (job->count >= FILES_MOST / 8 || used + entry->length + 1 > FILES_JOB_NAMES)
                        break;
                if (files_model_check(m, place, &job->facts[job->count]) != FILES_SAME)
                {
                        files_job_end(job);
                        string_copy_bounded(job->at, (string_address)files_model_name(m, entry), FILES_NAME);
                        return job->state = FILES_JOB_FAILED, job->error = -ERROR_NO_ENTRY, false;
                }
                job->offsets[job->count] = (p32)used;
                memory_copy(job->names + used, files_model_name(m, entry), (positive)entry->length + 1);
                used += (positive)entry->length + 1;
                job->count++;
        }
        job->state = job->count ? FILES_JOB_RUNNING : FILES_JOB_DONE;
        return job->count != 0;
}

// One step of recursive DFS directory removal (shared by delete and cross-device move): 1 = top removed, 0 = in progress, <0 = error.
static int files_rmdir_step(bipolar root_fd, bipolar address_to stack, p8 (*inside)[FILES_NAME], int address_to depth, p8 address_to at,
                            const p8 address_to top_name, const file_facts address_to top_facts, p32 address_to removed, bipolar address_to error)
{
        bipolar in = *depth ? stack[*depth - 1] : root_fd, gone;
        file_walk walk;
        struct linux_dirent64 address_to record;
        file_facts facts;
        b32 top = !*depth;

        if (top)
        {
                string_copy_bounded(at, (string_address)top_name, FILES_NAME);
                facts = *top_facts;
        }
        else
        {
                if (!file_walk_open(&walk, in, (string_address) "."))
                        return *error = walk.error, -1;
                while ((record = file_walk_next(&walk)) && file_is_dot((string_address)record->d_name))
                        ;
                if (record)
                        string_copy_bounded(at, (string_address)record->d_name, FILES_NAME);
                gone = walk.error;
                file_walk_close(&walk);
                if (!record)
                {
                        if (gone < 0)
                                return *error = gone, -1;
                        files_close(&stack[--(*depth)]);
                        string_copy_bounded(at, inside[*depth], FILES_NAME);
                        gone = (bipolar)system_call_3(syscall(unlinkat), (positive)(*depth ? stack[*depth - 1] : root_fd), (positive)at, AT_REMOVEDIR);
                        if (gone < 0)
                                return *error = gone, -1;
                        if (removed)
                                (*removed)++;
                        return !*depth ? 1 : 0;
                }
                if ((gone = files_look_at(in, at, &facts)) < 0)
                        return *error = gone, -1;
        }
        if ((facts.mode & MODE_FORMAT) == MODE_DIRECTORY)
        {
                if (*depth >= FILES_JOB_DEEP)
                        return *error = -ERROR_LOOP, -1;
                if (!file_walk_open_found_same(&walk, in, (string_address)at, &facts))
                        return *error = walk.error, -1;
                string_copy_bounded(inside[*depth], at, FILES_NAME);
                stack[(*depth)++] = walk.handle;
                return 0;
        }
        if ((gone = file_remove_same(in, (string_address)at, 0, &facts)) < 0)
                return *error = gone, -1;
        if (removed)
                (*removed)++;
        return top ? 1 : 0;
}

static b32 files_delete_one(struct files_model address_to m, struct files_job address_to job)
{
        bipolar err = 0;
        int step;

        if (!job->depth && job->next >= job->count)
                return job->state = FILES_JOB_DONE, false;
        step = files_rmdir_step(m->handle, job->stack, job->inside, &job->depth, job->at,
                                job->depth ? null : job->names + job->offsets[job->next],
                                job->depth ? null : &job->facts[job->next], &job->removed, &err);
        if (step < 0)
        {
                files_job_close(job);
                return job->state = FILES_JOB_FAILED, job->error = err, false;
        }
        job->next += step;
        return true;
}

static b32 files_job_step(struct files_model address_to m, struct files_job address_to job, positive most)
{
        for (positive done = 0; done < most && job->state == FILES_JOB_RUNNING; done++)
                if (!files_delete_one(m, job))
                        break;
        return job->state == FILES_JOB_RUNNING;
}

// Sidebar folder/drive tree: flat pre-order array of nodes with bounded lazy expansion and mountinfo drive sync.
#define FILES_TREE_MOST 4096       // the nodes a tree holds
#define FILES_TREE_NAMES (FILES_TREE_MOST * 32u) // and the bytes of their names
#define FILES_TREE_CHILDREN 1024   // the folders a node opened shows at most
#define FILES_TREE_SCAN 65536      // and the entries looked through to find them
#define FILES_DRIVES_MOST 32       // extra mounted storage drives listed beside the root drive
#define FILES_TREE_BYTES ((positive)FILES_TREE_MOST * sizeof(struct files_node) + FILES_TREE_NAMES)

enum
{
        FILES_NODE_FOLDER, // an ordinary subfolder in a tree
        FILES_NODE_PLACE,  // a common folder root above the drives (Home, Downloads, Pictures)
        FILES_NODE_DRIVE,  // a storage drive root (/ or another mounted drive)
};

enum
{
        FILES_PLACE_NONE,
        FILES_PLACE_HOME,
        FILES_PLACE_DOWNLOADS,
        FILES_PLACE_PICTURES,
        FILES_PLACE_ROOT,
};

struct files_node
{
        p32 name;          // display label in the tree's names
        p32 root_path;     // depth 0 only: full absolute path in the tree's names
        p16 length;
        p16 depth;         // 0 a root (place or drive)
        p8 kind;           // FILES_NODE_*
        p8 place;          // FILES_PLACE_*
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
        p64 drives_hash;
        address_any mapped;
        positive mapped_size;
};

struct files_drive
{
        p8 label[FILES_NAME];
        p8 path[FILES_PATH];
        p8 device[32];
        p8 root[256];
};

static fn files_tree_reset(struct files_tree address_to t)
{
        t->count = 0, t->names_used = 0, t->drives_hash = 0;
}

static p32 files_tree_store(struct files_tree address_to t, const p8 address_to s, positive len)
{
        p32 at = t->names_used;

        memory_copy(t->names + at, s, len + 1);
        t->names_used += (p32)(len + 1);
        return at;
}

static b32 files_tree_add_root(struct files_tree address_to t, const p8 address_to label, const p8 address_to path, p8 kind, p8 place)
{
        positive label_len = string_length((string_address)label), path_len = string_length((string_address)path);
        struct files_node address_to n;
        file_facts facts;

        if (!t->node || t->count >= FILES_TREE_MOST || !label_len || !path_len || path[0] != '/' ||
            t->names_used + label_len + path_len + 2 > FILES_TREE_NAMES)
                return false;
        n = &t->node[t->count++];
        memory_fill(n, 0, sizeof(*n));
        n->name = files_tree_store(t, label, label_len), n->length = (p16)label_len;
        n->root_path = files_tree_store(t, path, path_len);
        n->kind = kind, n->place = place;
        if (file_look_code(AT_FDCWD, (string_address)path, 0, &facts) >= 0)
                n->inode = facts.inode, n->device_major = facts.device_major, n->device_minor = facts.device_minor;
        return true;
}

static b32 files_tree_begin(struct files_tree address_to t)
{
        address_any mapped = files_anon_map(FILES_TREE_BYTES, null);

        memory_fill(t, 0, sizeof(*t));
        if (!mapped)
                return false;
        t->mapped = mapped, t->mapped_size = FILES_TREE_BYTES;
        t->node = (struct files_node address_to)mapped;
        t->names = (p8 address_to)mapped + (positive)FILES_TREE_MOST * sizeof(struct files_node);
        return files_tree_add_root(t, (const p8 address_to) "/", (const p8 address_to) "/", FILES_NODE_DRIVE, FILES_PLACE_ROOT);
}

static fn files_tree_end(struct files_tree address_to t)
{
        files_unmap(t->mapped, t->mapped_size);
        memory_fill(t, 0, sizeof(*t));
}

static b32 files_path_under(const p8 address_to path, const char address_to prefix)
{
        positive n = string_length((string_address)prefix);

        return !memory_compare(path, prefix, n) && (path[n] == 0 || path[n] == '/');
}

static positive files_mount_unescape(const p8 address_to from, positive length, p8 address_to into, positive room)
{
        positive used = 0;

        for (positive at = 0; at < length && used + 1 < room; at++)
        {
                if (from[at] == '\\' && at + 3 < length && from[at + 1] >= '0' && from[at + 1] <= '3' &&
                    from[at + 2] >= '0' && from[at + 2] <= '7' && from[at + 3] >= '0' && from[at + 3] <= '7')
                {
                        into[used++] = (p8)(((from[at + 1] - '0') << 6) | ((from[at + 2] - '0') << 3) | (from[at + 3] - '0'));
                        at += 3;
                }
                else
                        into[used++] = from[at];
        }
        into[used] = end;
        return used;
}

static b32 files_drive_virtual_fs(const p8 address_to fs)
{
        static const char address_to const skip[] = {
            "proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "cgroup", "cgroup2", "pstore", "bpf", "tracefs", "debugfs",
            "securityfs", "configfs", "fusectl", "mqueue", "hugetlbfs", "autofs", "rpc_pipefs", "efivarfs", "ramfs",
            "sockfs", "pipefs", "nsfs", "overlay", "squashfs", "binfmt_misc", "fuse.portal", "fuse.gvfsd-fuse",
        };

        for (positive k = 0; k < sizeof(skip) / sizeof(skip[0]); k++)
                if (string_equals((string_address)fs, (string_address)skip[k]))
                        return true;
        return false;
}

static b32 files_drive_skip_target(const p8 address_to path)
{
        static const char address_to const skip[] = {
            "/proc", "/sys", "/dev", "/boot", "/snap", "/run/lock", "/run/shm", "/run/user", "/var/lib/docker", "/var/lib/containers",
        };

        if (string_equals((string_address)path, (string_address) "/"))
                return true;
        for (positive k = 0; k < sizeof(skip) / sizeof(skip[0]); k++)
                if (files_path_under(path, skip[k]))
                        return true;
        return false;
}

// Parse /proc/self/mountinfo for extra mounted drives (real block devices or /mnt/*, /media/*, /run/media/*).
static int files_drives_parse(const p8 address_to text, positive length, struct files_drive address_to out, int most, p64 address_to out_hash)
{
        int count = 0;
        p64 hash = 14695981039346656037ull;
        positive at = 0;

        while (at < length && count < most)
        {
                positive line_start = at, line_end;
                const p8 address_to tok[16];
                positive tok_len[16], tlen;
                int ntok = 0, sep = -1;
                p8 dev[32], root[256], target[FILES_PATH], fs[64], src[256];
                b32 dup = false, block, user_mnt;
                const p8 address_to nl = (const p8 address_to)memory_first_of(text + at, '\n', length - at);

                line_end = nl ? (positive)(nl - text) : length;
                at = nl ? line_end + 1 : length;
                for (positive p = line_start; p < line_end && ntok < 16;)
                {
                        while (p < line_end && (text[p] == ' ' || text[p] == '\t'))
                                p++;
                        if (p >= line_end)
                                break;
                        tok[ntok] = text + p;
                        while (p < line_end && text[p] != ' ' && text[p] != '\t')
                                p++;
                        tok_len[ntok] = (positive)(text + p - tok[ntok]);
                        if (sep < 0 && ntok >= 6 && tok_len[ntok] == 1 && tok[ntok][0] == '-')
                                sep = ntok;
                        ntok++;
                }
                if (sep < 6 || sep + 2 >= ntok)
                        continue;
                files_mount_unescape(tok[2], tok_len[2], dev, sizeof(dev));
                files_mount_unescape(tok[3], tok_len[3], root, sizeof(root));
                files_mount_unescape(tok[4], tok_len[4], target, sizeof(target));
                files_mount_unescape(tok[sep + 1], tok_len[sep + 1], fs, sizeof(fs));
                files_mount_unescape(tok[sep + 2], tok_len[sep + 2], src, sizeof(src));
                block = string_has_prefix(src, "/dev/") && !string_has_prefix(src, "/dev/loop") &&
                        !string_has_prefix(src, "/dev/zram") && !string_has_prefix(src, "/dev/ram");
                user_mnt = (files_path_under(target, "/mnt") && target[4] == '/') ||
                           (files_path_under(target, "/media") && target[6] == '/') ||
                           (files_path_under(target, "/run/media") && target[10] == '/');
                if (target[0] != '/' || files_drive_skip_target(target) || files_drive_virtual_fs(fs) || (!block && !user_mnt))
                        continue;
                for (int k = 0; k < count && !dup; k++)
                        dup = string_equals((string_address)out[k].path, (string_address)target) ||
                              (string_equals((string_address)out[k].device, (string_address)dev) &&
                               string_equals((string_address)out[k].root, (string_address)root));
                if (dup)
                        continue;
                tlen = string_length((string_address)target);
                while (tlen > 1 && target[tlen - 1] == '/')
                        target[--tlen] = end;
                string_copy_bounded((string_address)out[count].path, (string_address)target, sizeof(out[count].path));
                if (tlen <= 1 && target[0] == '/')
                        out[count].label[0] = 0;
                else
                        path_tail_copy((p8 address_to)out[count].label, sizeof(out[count].label), target);
                string_copy_bounded((string_address)out[count].device, (string_address)dev, sizeof(out[count].device));
                string_copy_bounded((string_address)out[count].root, (string_address)root, sizeof(out[count].root));
                for (positive i = 0; i < tlen; i++)
                        hash = (hash ^ target[i]) * 1099511628211ull;
                hash = (hash ^ 0xffu) * 1099511628211ull;
                count++;
        }
        if (out_hash)
                *out_hash = hash;
        return count;
}

static fn files_tree_apply(struct files_tree address_to t, const p8 address_to home, const struct files_drive address_to drives, int n, p64 hash)
{
        static const struct
        {
                const char address_to name;
                p8 place;
        } places[] = {{"Downloads", FILES_PLACE_DOWNLOADS}, {"Pictures", FILES_PLACE_PICTURES}};
        p8 sub[FILES_PATH];

        files_tree_reset(t);
        if (home && home[0] == '/')
        {
                files_tree_add_root(t, (const p8 address_to) "Home", home, FILES_NODE_PLACE, FILES_PLACE_HOME);
                for (positive k = 0; k < sizeof(places) / sizeof(places[0]); k++)
                        if (files_path_join(sub, sizeof(sub), home, (const p8 address_to)places[k].name))
                                files_tree_add_root(t, (const p8 address_to)places[k].name, sub, FILES_NODE_PLACE, places[k].place);
        }
        files_tree_add_root(t, (const p8 address_to) "/", (const p8 address_to) "/", FILES_NODE_DRIVE, FILES_PLACE_ROOT);
        for (int k = 0; k < n; k++)
                files_tree_add_root(t, drives[k].label, drives[k].path, FILES_NODE_DRIVE, FILES_PLACE_NONE);
        t->drives_hash = hash;
}

struct files_mount_scratch
{
        p8 buf[65536];
        struct files_drive drives[FILES_DRIVES_MOST];
};

static fn files_tree_populate(struct files_tree address_to t, const p8 address_to home, const p8 address_to mountinfo, positive mountinfo_len)
{
        struct files_drive address_to drives = mountinfo && mountinfo_len ? (struct files_drive address_to)files_anon_map(sizeof(struct files_drive) * FILES_DRIVES_MOST, null) : null;
        p64 hash = 0;
        int n = drives ? files_drives_parse(mountinfo, mountinfo_len, drives, FILES_DRIVES_MOST, &hash) : 0;

        files_tree_apply(t, home, drives, n, hash);
        files_unmap(drives, sizeof(struct files_drive) * FILES_DRIVES_MOST);
}

static b32 files_tree_sync_drives(struct files_tree address_to t, const p8 address_to home, b32 force)
{
        struct files_mount_scratch address_to s = (struct files_mount_scratch address_to)files_anon_map(sizeof(*s), null);
        bipolar got;
        p64 hash = 0;
        int n = 0;
        b32 changed;

        if (!s)
                return false;
        got = file_slurp_once_at(AT_FDCWD, (string_address) "/proc/self/mountinfo", s->buf, sizeof(s->buf) - 1);
        if (got > 0)
                n = files_drives_parse(s->buf, (positive)got, s->drives, FILES_DRIVES_MOST, &hash);
        changed = force || hash != t->drives_hash;
        if (changed)
                files_tree_apply(t, home, s->drives, n, hash);
        files_unmap(s, sizeof(*s));
        return changed;
}

static fn files_tree_populate_live(struct files_tree address_to t, const p8 address_to home)
{
        files_tree_sync_drives(t, home, true);
}

static inline const p8 address_to files_tree_name(const struct files_tree address_to t, p32 at)
{
        return t->names + t->node[at].name;
}

static p32 files_tree_after(const struct files_tree address_to t, p32 at)
{
        p32 next = at + 1;

        while (next < t->count && t->node[next].depth > t->node[at].depth)
                next++;
        return next;
}

static p32 files_tree_parent(const struct files_tree address_to t, p32 at)
{
        if (at >= t->count || t->node[at].depth == 0)
                return FILES_NO;
        for (p32 up = at, depth = t->node[at].depth; up-- > 0;)
                if (t->node[up].depth < depth)
                        return up;
        return FILES_NO;
}

static b32 files_tree_path(const struct files_tree address_to t, p32 at, p8 address_to into)
{
        p32 chain[256];
        int links = 0;
        positive used = 0, rlen;
        const p8 address_to rpath;

        if (at >= t->count)
                return false;
        for (p32 here = at; here != FILES_NO && here < t->count && links < 256; here = files_tree_parent(t, here))
                chain[links++] = here;
        if (!links)
                return false;
        rpath = t->names + t->node[chain[links - 1]].root_path;
        rlen = string_length((string_address)rpath);
        if (!rlen || rlen >= FILES_PATH)
                return false;
        memory_copy(into, rpath, rlen);
        used = rlen;
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

static int files_tree_name_order(address_any left, address_any right, address_any context)
{
        const p8 address_to names = (const p8 address_to)context;

        return files_name_order(names + *(const p32 address_to)left, names + *(const p32 address_to)right);
}

struct files_tree_open_scratch
{
        p8 path[FILES_PATH];
        p32 offsets[FILES_TREE_CHILDREN];
        p8 found[FILES_TREE_CHILDREN * 64];
};

static b32 files_tree_open(struct files_tree address_to t, p32 at)
{
        struct files_tree_open_scratch address_to s;
        file_walk walk = {.handle = -1};
        struct linux_dirent64 address_to record;
        file_facts folder;
        p32 count = 0, scanned = 0, after, depth;
        positive used = 0;
        b32 ok = false;

        if (at >= t->count || t->node[at].open)
                return at < t->count;
        if (!(s = (struct files_tree_open_scratch address_to)files_anon_map(sizeof(*s), null)))
                return false;
        if (!files_tree_path(t, at, s->path) || !file_walk_open(&walk, AT_FDCWD, (string_address)s->path) ||
            files_look_fd(walk.handle, &folder) < 0 ||
            (t->node[at].inode && !files_same_file(t->node[at].inode, t->node[at].device_major, t->node[at].device_minor, &folder)))
        {
                t->node[at].read = true, t->node[at].has = 0;
                goto done;
        }
        while (count < FILES_TREE_CHILDREN && scanned < FILES_TREE_SCAN && (record = file_walk_next(&walk)))
        {
                positive length = string_length((string_address)record->d_name);
                file_facts facts;

                scanned++;
                if (file_is_dot((string_address)record->d_name) || record->d_name[0] == '.' || length >= FILES_NAME || used + length + 1 > sizeof(s->found))
                        continue;
                if (record->d_type != DT_DIR && (record->d_type != DT_UNKNOWN || files_look_at(walk.handle, (const p8 address_to)record->d_name, &facts) < 0 ||
                                                 (facts.mode & MODE_FORMAT) != MODE_DIRECTORY))
                        continue;
                s->offsets[count++] = (p32)used;
                memory_copy(s->found + used, record->d_name, length + 1);
                used += length + 1;
        }
        if (count > 1)
                qsort_r(s->offsets, count, sizeof(p32), files_tree_name_order, s->found);
        after = at + 1, depth = t->node[at].depth + 1u;
        t->node[at].read = true, t->node[at].has = (p16)count;
        if (t->count + count > FILES_TREE_MOST || t->names_used + used > FILES_TREE_NAMES)
                goto done;
        t->node[at].open = true;
        memory_copy(&t->node[after + count], &t->node[after], (positive)(t->count - after) * sizeof(struct files_node));
        for (p32 k = 0; k < count; k++)
        {
                const p8 address_to name = s->found + s->offsets[k];
                positive length = string_length((string_address)name);
                struct files_node address_to n = &t->node[after + k];
                file_facts facts;

                memory_fill(n, 0, sizeof(*n));
                n->name = files_tree_store(t, name, length), n->length = (p16)length, n->depth = (p16)depth;
                if (files_look_at(walk.handle, name, &facts) >= 0)
                        n->inode = facts.inode, n->device_major = facts.device_major, n->device_minor = facts.device_minor;
        }
        t->count += count;
        ok = true;
done:
        if (walk.handle >= 0)
                file_walk_close(&walk);
        files_unmap(s, sizeof(*s));
        return ok;
}

static fn files_tree_compact(struct files_tree address_to t)
{
        p8 address_to fresh = (p8 address_to)files_anon_map(FILES_TREE_NAMES, null);
        p32 used = 0;

        if (!fresh)
                return;
        for (p32 k = 0; k < t->count; k++)
        {
                struct files_node address_to n = &t->node[k];
                positive nlen = (positive)n->length + 1;

                memory_copy(fresh + used, t->names + n->name, nlen);
                n->name = used, used += (p32)nlen;
                if (n->depth == 0)
                {
                        positive rlen = string_length((string_address)(t->names + n->root_path)) + 1;

                        memory_copy(fresh + used, t->names + n->root_path, rlen);
                        n->root_path = used, used += (p32)rlen;
                }
        }
        memory_copy(t->names, fresh, used);
        t->names_used = used;
        files_unmap(fresh, FILES_TREE_NAMES);
}

static fn files_tree_shut(struct files_tree address_to t, p32 at)
{
        p32 after;

        if (at >= t->count || !t->node[at].open)
                return;
        after = files_tree_after(t, at);
        memory_copy(&t->node[at + 1], &t->node[after], (positive)(t->count - after) * sizeof(struct files_node));
        t->count -= after - at - 1;
        t->node[at].open = false, t->node[at].read = false;
        if (after > at + 1)
                files_tree_compact(t);
}

static p32 files_tree_find(const struct files_tree address_to t, const p8 address_to path)
{
        p8 made[FILES_PATH];
        p32 found = FILES_NO;

        for (p32 at = 0; at < t->count; at++)
                if (files_tree_path(t, at, made) && string_equals((string_address)made, (string_address)path))
                {
                        if (t->node[at].depth == 0)
                                return at;
                        if (found == FILES_NO)
                                found = at;
                }
        return found;
}

// Reveal `path` under the longest-prefix depth-0 root containing it (exact root matches stay collapsed).
static p32 files_tree_reveal(struct files_tree address_to t, const p8 address_to path)
{
        positive length = string_length((string_address)path), best_len = 0, start;
        p32 at = FILES_NO;

        if (!t->count || path[0] != '/')
                return FILES_NO;
        for (p32 k = 0; k < t->count; k++)
        {
                const p8 address_to rpath;
                positive rlen;

                if (t->node[k].depth != 0)
                        continue;
                rpath = t->names + t->node[k].root_path;
                rlen = string_length((string_address)rpath);
                if (rlen && rlen <= length && (rlen == 1 ? rpath[0] == '/' : files_path_under(path, (const char address_to)rpath)) && rlen > best_len)
                        best_len = rlen, at = k;
        }
        if (at == FILES_NO)
                return FILES_NO;
        start = best_len == 1 ? 1 : path[best_len] == '/' ? best_len + 1 : length;
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

// Stepped/threaded copy, move, and size-measurement job: never overwrites (O_EXCL / RENAME_NOREPLACE / .copy suffix) and never follows symlinks.
#define FILES_MOVE_MOST 4096          // the sources one move takes
#define FILES_MOVE_PIECE (8u << 20)   // a file's bytes copied in one piece: a cancel is seen between two
#define FILES_MOVE_BUFFER (1u << 20)  // the bytes read and written at a time where the kernel will not copy a range
#define FILES_MOVE_INSIDE 1000        // error: a folder into itself
#define FILES_MOVE_COPY 1             // what a move does: the values the drag's actions have (WINDOWS_DRAG_COPY, _MOVE)
#define FILES_MOVE_MOVE 2
#define FILES_MOVE_MEASURE 3          // or only counts what the sources hold (a properties sheet's size): nothing is made or moved
#define FILES_MOVE_PATHS ((positive)FILES_MOVE_MOST * FILES_PATH / 4)
#define FILES_MOVE_BYTES (FILES_MOVE_PATHS + (positive)FILES_MOVE_MOST * (sizeof(p32) + 1 + FILES_NAME))

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
        p32 address_to offsets;           // byte offset of each source in paths (O(1) lookup)
        p8 address_to copied;             // per source: 1 when it is copied (a copy, or a move across filesystems), 2 when it is done
        p8 address_to landed;             // per source, FILES_NAME bytes: the name it has in into once it is there ("" none), for an undo
        p32 count, next;
        bipolar from;
        p8 name[FILES_NAME], named[FILES_NAME];
        file_facts facts;
        int depth;
        file_walk walk[FILES_JOB_DEEP];
        bipolar made[FILES_JOB_DEEP];
        file_facts made_facts[FILES_JOB_DEEP];
        p8 inside[FILES_JOB_DEEP][FILES_NAME];
        bipolar in, out, out_folder;
        p8 out_name[FILES_NAME];
        p64 left;
        file_facts piece_facts;
        b32 ranged;
        p8 address_to buffer;
        thread address_to worker;
        b32 begun;
};

static inline p8 address_to files_move_landed(const struct files_move address_to job, p32 which)
{
        return job->landed + (positive)which * FILES_NAME;
}

static inline b32 files_move_cancelled(const struct files_move address_to job)
{
        return __atomic_load_n(&job->cancel, __ATOMIC_ACQUIRE) != 0;
}

static inline b32 files_move_running(const struct files_move address_to job)
{
        return __atomic_load_n(&job->state, __ATOMIC_ACQUIRE) == FILES_JOB_RUNNING;
}

static b32 files_move_fail(struct files_move address_to job, bipolar error)
{
        job->error = error;
        __atomic_store_n(&job->state, FILES_JOB_FAILED, __ATOMIC_RELEASE);
        return false;
}

static fn files_move_file_drop(struct files_move address_to job, b32 whole)
{
        file_facts made;

        files_close(&job->in);
        if (job->out >= 0)
        {
                if (!whole && files_look_fd(job->out, &made) >= 0)
                        file_remove_same(job->out_folder, (string_address)job->out_name, 0, &made);
                files_close(&job->out);
        }
        job->left = 0;
}

static fn files_move_close(struct files_move address_to job)
{
        files_move_file_drop(job, false);
        while (job->depth > 0)
        {
                job->depth--;
                file_walk_close(&job->walk[job->depth]);
                files_close(&job->made[job->depth]);
        }
        files_close(&job->from);
        files_close(&job->into);
}

static b32 files_move_begin(struct files_move address_to job, int how, bipolar into, const p8 address_to paths, p32 count)
{
        bipolar err = 0;
        positive used = 0;

        memory_fill(job, 0, sizeof(*job));
        job->into = into, job->from = job->in = job->out = job->out_folder = -1, job->begun = true;
        job->how = how == FILES_MOVE_MOVE || how == FILES_MOVE_MEASURE ? how : FILES_MOVE_COPY;
        for (int k = 0; k < FILES_JOB_DEEP; k++)
                job->made[k] = -1, job->walk[k].handle = -1;
        if (into < 0 || files_look_fd(into, &job->into_facts) < 0 || (job->into_facts.mode & MODE_FORMAT) != MODE_DIRECTORY)
                return files_move_fail(job, into < 0 ? into : -ERROR_NOT_DIRECTORY);
        if (!(job->paths = (p8 address_to)files_anon_map(FILES_MOVE_BYTES, &err)))
                return files_move_fail(job, err);
        job->offsets = (p32 address_to)(job->paths + FILES_MOVE_PATHS);
        job->copied = (p8 address_to)(job->offsets + FILES_MOVE_MOST);
        job->landed = job->copied + FILES_MOVE_MOST;
        for (p32 k = 0; k < count && job->count < FILES_MOVE_MOST; k++)
        {
                positive length = string_length((string_address)paths);

                if (length > 1 && paths[0] == '/' && length < FILES_PATH && used + length + 1 <= FILES_MOVE_PATHS)
                {
                        job->offsets[job->count] = (p32)used;
                        memory_copy(job->paths + used, paths, length + 1);
                        used += length + 1, job->count++;
                }
                paths += length + 1;
        }
        job->state = job->count ? FILES_JOB_RUNNING : FILES_JOB_DONE;
        return job->count != 0;
}

static inline const p8 address_to files_move_path(const struct files_move address_to job, p32 which)
{
        return job->paths + job->offsets[which];
}

static b32 files_move_take(struct files_move address_to job, p32 which)
{
        const p8 address_to path = files_move_path(job, which);
        positive length = string_length((string_address)path), cut = length;
        p8 folder[FILES_PATH];
        bipolar looked;

        while (cut > 0 && path[cut - 1] != '/')
                cut--;
        files_close(&job->from);
        if (!cut || cut == length || length - cut >= FILES_NAME)
                return files_move_fail(job, -ERROR_INVALID);
        memory_copy(job->name, path + cut, length - cut + 1);
        string_copy_bounded(job->at, (string_address)job->name, FILES_NAME);
        memory_copy(folder, path, cut > 1 ? cut - 1 : 1);
        folder[cut > 1 ? cut - 1 : 1] = end;
        job->from = files_open_dir(AT_FDCWD, folder, 0);
        looked = job->from < 0 ? job->from : files_look_at(job->from, job->name, &job->facts);
        return looked < 0 ? files_move_fail(job, looked) : true;
}

// Whether directory fd `folder` is `facts` or a descendant of it.
static b32 files_move_within(bipolar folder, const file_facts address_to facts)
{
        bipolar at = files_open_dir(folder, (const p8 address_to) ".", 0);
        b32 inside = false;

        for (int up = 0; at >= 0 && up < 4096 && !inside; up++)
        {
                file_facts here, above;
                bipolar next;

                if (files_look_fd(at, &here) < 0)
                        break;
                inside = files_same_facts(facts, &here);
                next = files_open_dir(at, (const p8 address_to) "..", 0);
                if (next < 0 || files_look_fd(next, &above) < 0 || files_same_facts(&here, &above))
                {
                        files_close(&next);
                        break;
                }
                system_close(at);
                at = next;
        }
        files_close(&at);
        return inside;
}

// Find a free name in `into`: `name`, else `stem.copy.ext`, `stem.copy2.ext`, ... up to 999.
static b32 files_move_free_name(bipolar into, const p8 address_to name, p8 address_to free, b32 address_to numbered)
{
        positive length = string_length((string_address)name), dot;
        file_facts there;

        *numbered = false;
        if (files_look_at(into, name, &there) == -ERROR_NO_ENTRY)
                return memory_copy(free, name, length + 1), true;
        dot = files_name_dot(name, length);
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
                if (files_look_at(into, free, &there) == -ERROR_NO_ENTRY)
                        return *numbered = true, true;
        }
        return false;
}

static bipolar files_move_target(const struct files_move address_to job)
{
        return job->depth ? job->made[job->depth - 1] : job->into;
}

static fn files_move_settle(bipolar made, const file_facts address_to facts)
{
        b64 times[4] = {facts->accessed.seconds, facts->accessed.nanoseconds, facts->modified.seconds, facts->modified.nanoseconds};

        system_call_2(syscall(fchmod), (positive)made, (positive)(facts->mode & 0777));
        system_call_4(syscall(utimensat), (positive)made, 0, (positive)times, 0);
}

static b32 files_move_one(struct files_move address_to job, bipolar source, const p8 address_to name, file_facts address_to facts, const p8 address_to made_name,
                          b32 counting)
{
        p32 format = facts->mode & MODE_FORMAT;
        bipolar target = files_move_target(job), made;

        if (format == MODE_DIRECTORY)
        {
                if (job->depth >= FILES_JOB_DEEP)
                        return files_move_fail(job, -ERROR_LOOP);
                if (!file_walk_open_found_same(&job->walk[job->depth], source, (string_address)name, facts))
                        return files_move_fail(job, job->walk[job->depth].error);
                job->made[job->depth] = -1, job->made_facts[job->depth] = *facts;
                string_copy_bounded(job->inside[job->depth], (string_address)name, FILES_NAME);
                if (!counting)
                {
                        made = (bipolar)system_call_3(syscall(mkdirat), (positive)target, (positive)made_name, 0700);
                        made = made < 0 ? made : files_open_dir(target, made_name, O_NOFOLLOW);
                        if (made < 0)
                        {
                                file_walk_close(&job->walk[job->depth]);
                                return files_move_fail(job, made);
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
                        return files_move_fail(job, length);
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

                job->out = -1;
                files_move_file_drop(job, true);
                return files_move_fail(job, why);
        }
        job->out_folder = target, job->left = facts->size, job->ranged = true;
        string_copy_bounded(job->out_name, (string_address)made_name, FILES_NAME);
        job->piece_facts = *facts;
        return true;
}

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
                                job->ranged = false;
                                continue;
                        }
                }
                else
                {
                        if (!job->buffer && !(job->buffer = (p8 address_to)files_anon_map(FILES_MOVE_BUFFER, &got)))
                                break;
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
                if (got == 0)
                        job->left = 0;
                else
                {
                        files_move_file_drop(job, false);
                        return files_move_fail(job, got);
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
                        return files_move_fail(job, walk->error);
                job->depth--;
                file_walk_close(walk);
                if (job->made[job->depth] >= 0)
                {
                        files_move_settle(job->made[job->depth], &job->made_facts[job->depth]);
                        files_close(&job->made[job->depth]);
                }
                __atomic_add_fetch(counting ? &job->items_total : &job->items_done, 1, __ATOMIC_RELEASE);
                return job->depth > 0;
        }
        if ((looked = files_look_at(walk->handle, (const p8 address_to)record->d_name, &facts)) < 0)
        {
                string_copy_bounded(job->at, (string_address)record->d_name, FILES_NAME);
                return files_move_fail(job, looked);
        }
        return files_move_one(job, walk->handle, (const p8 address_to)record->d_name, &facts, (const p8 address_to)record->d_name, counting) &&
               (job->depth > 0 || job->out >= 0);
}

static b32 files_move_remove(struct files_move address_to job)
{
        bipolar err = 0;
        int step = files_rmdir_step(job->from, job->made, job->inside, &job->depth, job->at,
                                    job->depth ? null : job->name, job->depth ? null : &job->facts, null, &err);

        return step < 0 ? files_move_fail(job, err) : step == 0;
}

static b32 files_move_step(struct files_move address_to job)
{
        if (!files_move_running(job))
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
                        return job->phase = FILES_MOVE_COUNT, job->next = 0, true;
                if (!files_move_take(job, job->next))
                        return false;
                if (job->how == FILES_MOVE_MEASURE)
                        return job->copied[job->next++] = 1, true;
                if ((job->facts.mode & MODE_FORMAT) == MODE_DIRECTORY && files_move_within(job->into, &job->facts))
                        return files_move_fail(job, -FILES_MOVE_INSIDE);
                job->copied[job->next] = 1;
                if (job->how == FILES_MOVE_MOVE)
                {
                        if (files_look_fd(job->from, &folder) >= 0 && files_same_facts(&job->into_facts, &folder))
                                job->copied[job->next] = 2;
                        else if (!files_move_free_name(job->into, job->name, job->named, &numbered))
                                return files_move_fail(job, -ERROR_EXISTS);
                        else if ((moved = (bipolar)system_call_5(syscall(renameat2), (positive)job->from, (positive)job->name, (positive)job->into,
                                                                 (positive)job->named, FILE_RENAME_NOREPLACE)) >= 0)
                        {
                                job->copied[job->next] = 2, job->renamed += numbered;
                                string_copy_bounded((string_address)files_move_landed(job, job->next), (string_address)job->named, FILES_NAME);
                                __atomic_add_fetch(&job->items_total, 1, __ATOMIC_RELEASE);
                                __atomic_add_fetch(&job->items_done, 1, __ATOMIC_RELEASE);
                        }
                        else if (moved != -ERROR_CROSS_DEVICE)
                                return files_move_fail(job, moved);
                }
                job->next++;
                return true;
        }
        if (job->phase == FILES_MOVE_COUNT || job->phase == FILES_MOVE_COPYING)
        {
                b32 counting = job->phase == FILES_MOVE_COUNT, numbered;

                if (job->depth || job->out >= 0)
                {
                        if (files_move_walk(job, counting))
                                return true;
                        if (!files_move_running(job))
                                return false;
                        if (counting)
                                job->next++;
                        else if (job->how == FILES_MOVE_MOVE)
                                job->phase = FILES_MOVE_REMOVE;
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
                        return files_move_fail(job, -ERROR_EXISTS);
                job->renamed += numbered;
                string_copy_bounded((string_address)files_move_landed(job, job->next), (string_address)job->named, FILES_NAME);
                if (!files_move_one(job, job->from, job->name, &job->facts, job->named, false))
                        return false;
                if (!job->depth && job->out < 0)
                {
                        if (job->how == FILES_MOVE_MOVE && (job->facts.mode & MODE_FORMAT) == MODE_LINK)
                                job->phase = FILES_MOVE_REMOVE;
                        else
                                job->copied[job->next++] = 2;
                }
                return true;
        }
        if (job->phase == FILES_MOVE_REMOVE)
        {
                if (files_move_remove(job))
                        return true;
                if (!files_move_running(job))
                        return false;
                job->copied[job->next++] = 2;
                job->phase = FILES_MOVE_COPYING;
                return true;
        }
        return false;
}

static fn files_move_run(address_any argument)
{
        while (files_move_step((struct files_move address_to)argument))
                ;
}

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
                files_unmap(job->paths, FILES_MOVE_BYTES);
                files_unmap(job->buffer, FILES_MOVE_BUFFER);
        }
        memory_fill(job, 0, sizeof(*job));
        job->into = job->from = job->in = job->out = job->out_folder = -1;
}

/* ---- the bin ---- */
// Desktop trash bin (BIN/files + BIN/info/NAME.trashinfo); same-fs rename only (cross-fs returns FILES_DID_CROSS).
#define FILES_DID_CROSS 9

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

static const p8 address_to files_base_name(const p8 address_to path)
{
        const p8 address_to base = path;

        for (const p8 address_to c = path; *c; c++)
                if (*c == '/' && c[1])
                        base = c + 1;
        return base;
}

static inline positive files_uri_byte(p8 address_to into, p8 c)
{
        static const char hex[] = "0123456789ABCDEF";

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
                return into[0] = c, 1;
        into[0] = '%', into[1] = (p8)hex[c >> 4], into[2] = (p8)hex[c & 15];
        return 3;
}

static inline int files_hex_nibble(p8 c)
{
        return c <= '9' ? c - '0' : (c | 32) - 'a' + 10;
}

static int files_bin_put(const p8 address_to bin, const p8 address_to path, const char address_to when, p8 address_to named)
{
        p8 files_dir[FILES_PATH], into[FILES_PATH], info[FILES_PATH], text[FILES_PATH * 3 + 96];
        positive used = 18;
        b32 numbered;
        bipolar folder, moved, out = -1;
        int res = FILES_DID_FAILED;

        if (!files_bin_join(files_dir, bin, "/files", null, null) || !files_bin_folders(files_dir) || !files_bin_join(info, bin, "/info", null, null) ||
            !files_bin_folders(info) || (folder = files_open_dir(AT_FDCWD, files_dir, 0)) < 0)
                return FILES_DID_FAILED;
        for (int tries = 0; tries < 8 && out < 0; tries++)
        {
                if (!files_move_free_name(folder, files_base_name(path), named, &numbered) || !files_bin_join(info, bin, "/info", named, ".trashinfo"))
                        goto done;
                out = system_open_at_mode(AT_FDCWD, (string_address)info, FILE_WRITE | FILE_EXCLUSIVE | O_NOFOLLOW | O_CLOEXEC, 0600);
        }
        if (out < 0)
                goto done;
        memory_copy(text, "[Trash Info]\nPath=", 18);
        for (const p8 address_to c = path; *c && used + 4 < sizeof(text) - 64; c++)
                used += files_uri_byte(text + used, *c);
        memory_copy(text + used, "\nDeletionDate=", 14), used += 14;
        for (const char address_to c = when; *c && used + 2 < sizeof(text); c++)
                text[used++] = (p8)*c;
        text[used++] = '\n';
        system_write_once((b32)out, text, used);
        system_close(out);
        files_bin_join(into, bin, "/files", named, null);
        moved = (bipolar)system_call_5(syscall(renameat2), AT_FDCWD, (positive)path, AT_FDCWD, (positive)into, FILE_RENAME_NOREPLACE);
        if (moved < 0)
        {
                system_remove_at(AT_FDCWD, (string_address)info, 0);
                res = moved == -ERROR_CROSS_DEVICE ? FILES_DID_CROSS : FILES_DID_FAILED;
        }
        else
                res = FILES_DID;
done:
        system_close(folder);
        return res;
}

static b32 files_bin_was(const p8 address_to bin, const p8 address_to name, p8 address_to path)
{
        p8 info[FILES_PATH], text[FILES_PATH * 3 + 128];
        bipolar got;
        positive at = 0, out = 0;

        if (!files_bin_join(info, bin, "/info", name, ".trashinfo"))
                return false;
        if ((got = file_slurp_once_at(AT_FDCWD, (string_address)info, text, sizeof(text) - 1)) <= 0)
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
                        c = (p8)(files_hex_nibble(text[at + 1]) << 4 | files_hex_nibble(text[at + 2])), at += 2;
                path[out++] = c;
        }
        path[out] = 0;
        return out > 1 && path[0] == '/';
}

static int files_bin_restore(const p8 address_to bin, const p8 address_to name, p8 address_to back)
{
        p8 from[FILES_PATH], info[FILES_PATH], folder_path[FILES_PATH], free[FILES_NAME];
        b32 numbered;
        bipolar folder, moved;

        if (!files_bin_was(bin, name, back) || !files_bin_join(from, bin, "/files", name, null) || !files_bin_join(info, bin, "/info", name, ".trashinfo") ||
            !files_path_parent(back, folder_path, null) || !files_bin_folders(folder_path) || (folder = files_open_dir(AT_FDCWD, folder_path, 0)) < 0)
                return FILES_DID_FAILED;
        moved = files_move_free_name(folder, files_base_name(back), free, &numbered)
                    ? (bipolar)system_call_5(syscall(renameat2), AT_FDCWD, (positive)from, (positive)folder, (positive)free, FILE_RENAME_NOREPLACE)
                    : -1;
        system_close(folder);
        if (moved < 0)
                return FILES_DID_FAILED;
        system_remove_at(AT_FDCWD, (string_address)info, 0);
        files_path_join(back, FILES_PATH, folder_path, free);
        return FILES_DID;
}

/* ---- git: what a work tree tracks ---- */
// Marks listing entries as tracked/untracked/ignored directly from .git/index (v2/v3) and .gitignore files without invoking git.
#define FILES_GIT_NONE 0
#define FILES_GIT_TRACKED 1
#define FILES_GIT_UNTRACKED 2
#define FILES_GIT_IGNORED 3
#define FILES_GIT_INDEX_MOST (64u << 20)
#define FILES_GIT_NAMES 131072u
#define FILES_GIT_LITERALS 4096u
#define FILES_GIT_GLOBS 256
#define FILES_GIT_BUDGET 4000000ul
#define FILES_GIT_IGNORE_MOST 16384

static b32 files_git_glob(const p8 address_to glob, positive glob_length, const p8 address_to text, positive length, positive address_to budget)
{
        positive g = 0, t = 0, star = (positive)-1, mark = 0;

        while (t < length)
        {
                if (!*budget)
                        return false;
                --*budget;
                if (g < glob_length && (glob[g] == '?' || glob[g] == text[t]))
                        g++, t++;
                else if (g < glob_length && glob[g] == '*')
                        star = g++, mark = t;
                else if (star != (positive)-1)
                        g = star + 1, t = ++mark;
                else
                        return false;
        }
        while (g < glob_length && glob[g] == '*')
                g++;
        return g == glob_length;
}

struct files_git_pattern
{
        const p8 address_to text;
        p16 length;
        p8 only_folders, rooted;
};

struct files_git_rules
{
        struct files_git_pattern literal[FILES_GIT_LITERALS];
        struct files_git_pattern glob[FILES_GIT_GLOBS];
        int globs;
};

struct files_git_scratch
{
        struct files_git_rules rules;
        p32 table[FILES_GIT_NAMES];
        p8 root[FILES_PATH];
        p8 ignore[2][FILES_GIT_IGNORE_MOST];
};

static inline p32 files_git_hash(const p8 address_to text, positive length)
{
        return hash_fnv1a(2166136261u, text, length);
}

static fn files_git_rules_add(struct files_git_rules address_to rules, const p8 address_to text, positive size, b32 rooted)
{
        for (positive at = 0; at < size;)
        {
                positive start = at, stop;
                b32 only_folders = false, anchored = false, inside = false, wild = false;
                struct files_git_pattern pattern;

                while (at < size && text[at] != '\n')
                        at++;
                stop = at++;
                while (stop > start && (text[stop - 1] == ' ' || text[stop - 1] == '\r'))
                        stop--;
                if (stop == start || text[start] == '#' || text[start] == '!')
                        continue;
                if (text[stop - 1] == '/')
                        only_folders = true, stop--;
                if (stop > start && text[start] == '/')
                        anchored = true, start++;
                for (positive k = start; k < stop; k++)
                        inside |= text[k] == '/', wild |= text[k] == '*' || text[k] == '?';
                if (inside || stop == start || stop - start > 255 || (anchored && !rooted))
                        continue;
                pattern = (struct files_git_pattern){text + start, (p16)(stop - start), (p8)only_folders, (p8)(rooted || anchored)};
                if (wild && rules->globs < FILES_GIT_GLOBS)
                        rules->glob[rules->globs++] = pattern;
                else if (!wild)
                {
                        p32 hash = files_git_hash(pattern.text, pattern.length);

                        for (p32 probe = 0; probe < FILES_GIT_LITERALS; probe++, hash++)
                        {
                                struct files_git_pattern address_to slot = &rules->literal[hash & (FILES_GIT_LITERALS - 1)];

                                if (!slot->length)
                                {
                                        *slot = pattern;
                                        break;
                                }
                                if (slot->length == pattern.length && !memory_compare(slot->text, pattern.text, pattern.length))
                                {
                                        slot->only_folders &= pattern.only_folders;
                                        break;
                                }
                        }
                }
        }
}

static b32 files_git_ignored(const struct files_git_rules address_to rules, const p8 address_to name, positive length, b32 folder, positive address_to budget)
{
        p32 hash = files_git_hash(name, length);

        for (p32 probe = 0; probe < FILES_GIT_LITERALS; probe++, hash++)
        {
                const struct files_git_pattern address_to slot = &rules->literal[hash & (FILES_GIT_LITERALS - 1)];

                if (!slot->length)
                        break;
                if (slot->length == length && !memory_compare(slot->text, name, length) && (folder || !slot->only_folders))
                        return true;
        }
        for (int k = 0; k < rules->globs; k++)
                if ((folder || !rules->glob[k].only_folders) && files_git_glob(rules->glob[k].text, rules->glob[k].length, name, length, budget))
                        return true;
        return false;
}

static b32 files_git_marks(const struct files_model address_to m, p8 address_to state)
{
        struct files_git_scratch address_to s;
        p8 at_path[FILES_PATH];
        positive length = string_length((string_address)m->path), rel, size = 0, mapped_size = 0, got = 0, budget = FILES_GIT_BUDGET;
        file_facts facts;
        bipolar handle;
        p8 address_to mapped = null;
        p32 count, version;
        b32 at_root, ok = false;

        memory_fill(state, FILES_GIT_NONE, FILES_MOST);
        if (!length || length >= FILES_PATH - 16 || !(s = (struct files_git_scratch address_to)files_anon_map(sizeof(*s), null)))
                return false;
        string_copy_bounded((string_address)s->root, (string_address)m->path, FILES_PATH);
        for (;;)
        {
                files_path_join(at_path, FILES_PATH, s->root, (const p8 address_to) ".git");
                if (file_look_code(AT_FDCWD, (string_address)at_path, 0, &facts) == 0 && (facts.mode & MODE_FORMAT) == MODE_DIRECTORY)
                        break;
                if (!files_path_parent(s->root, s->root, null))
                        goto done;
        }
        rel = string_length((string_address)s->root);
        rel = rel > 1 ? rel + 1 : 1;
        at_root = rel <= 1 || length + 1 == rel;
        string_append_bounded((string_address)at_path, (string_address) "/index", FILES_PATH);
        if ((handle = system_open_at(AT_FDCWD, (string_address)at_path, FILE_READ | O_CLOEXEC)) < 0)
                goto done;
        mapped_size = files_look_fd(handle, &facts) == 0 ? (positive)facts.size : 0;
        mapped = mapped_size >= 12 && mapped_size <= FILES_GIT_INDEX_MOST ? (p8 address_to)files_anon_map(mapped_size, null) : null;
        while (mapped && got < mapped_size)
        {
                bipolar part = (bipolar)system_read_once(handle, mapped + got, mapped_size - got);

                if (part <= 0)
                        break;
                got += (positive)part;
        }
        system_close(handle);
        if (!mapped)
                goto done;
        size = got;
        version = size >= 12 ? (p32)mapped[4] << 24 | (p32)mapped[5] << 16 | (p32)mapped[6] << 8 | mapped[7] : 0;
        count = size >= 12 ? (p32)mapped[8] << 24 | (p32)mapped[9] << 16 | (p32)mapped[10] << 8 | mapped[11] : 0;
        if (size < 12 || memory_compare(mapped, "DIRC", 4) || (version != 2 && version != 3))
                goto done;
        memory_fill(s->table, 0xff, sizeof(s->table));
        for (p32 k = 0; k < m->shown.count; k++)
        {
                const struct files_entry address_to entry = &m->shown.entry[k];
                p32 hash = files_git_hash(m->shown.names + entry->name, entry->length);

                while (s->table[hash & (FILES_GIT_NAMES - 1)] != 0xffffffffu)
                        hash++;
                s->table[hash & (FILES_GIT_NAMES - 1)] = k;
                state[k] = FILES_GIT_UNTRACKED;
        }
        for (positive at = 12, k = 0; k < count && at + 62 < size; k++)
        {
                p16 flags = (p16)(mapped[at + 60] << 8 | mapped[at + 61]);
                positive fixed = 62 + (version >= 3 && (flags & 0x4000) ? 2 : 0), name_at = at + fixed, end_at = name_at, first, stop;
                p32 hash, found;

                while (end_at < size && mapped[end_at])
                        end_at++;
                at = at + ((fixed + (end_at - name_at) + 8) & ~(positive)7);
                first = name_at;
                if (!at_root)
                {
                        positive inside = length - (rel - 1);

                        if (end_at - name_at < inside || memory_compare(mapped + name_at, m->path + rel, inside - 1) || mapped[name_at + inside - 1] != '/')
                                continue;
                        first = name_at + inside;
                }
                for (stop = first; stop < end_at && mapped[stop] != '/'; stop++)
                        ;
                hash = files_git_hash(mapped + first, stop - first);
                while ((found = s->table[hash & (FILES_GIT_NAMES - 1)]) != 0xffffffffu)
                {
                        const struct files_entry address_to entry = &m->shown.entry[found];

                        if (entry->length == stop - first && !memory_compare(m->shown.names + entry->name, mapped + first, stop - first))
                        {
                                state[found] = FILES_GIT_TRACKED;
                                break;
                        }
                        hash++;
                }
        }
        for (int which = 0; which < 2; which++)
        {
                bipolar read;

                files_path_join(at_path, FILES_PATH, which ? m->path : s->root, (const p8 address_to) ".gitignore");
                read = file_slurp_once_at(AT_FDCWD, (string_address)at_path, s->ignore[which], sizeof(s->ignore[which]));
                files_git_rules_add(&s->rules, s->ignore[which], read > 0 ? (positive)read : 0, which ? true : at_root);
                if (at_root)
                        break;
        }
        for (p32 k = 0; k < m->shown.count; k++)
        {
                const struct files_entry address_to entry = &m->shown.entry[k];
                const p8 address_to name = m->shown.names + entry->name;

                if (entry->length == 4 && !memory_compare(name, ".git", 4))
                        state[k] = FILES_GIT_NONE;
                else if (state[k] == FILES_GIT_UNTRACKED && files_git_ignored(&s->rules, name, entry->length, entry->kind == FILES_KIND_DIRECTORY, &budget))
                        state[k] = FILES_GIT_IGNORED;
        }
        ok = true;
done:
        files_unmap(mapped, mapped_size);
        files_unmap(s, sizeof(*s));
        return ok;
}

#endif // MOONWATER_FILES_MODEL
