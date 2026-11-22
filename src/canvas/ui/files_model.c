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

#define FILES_PATH 4096
#define FILES_NAME 256
#ifndef FILES_MOST
#define FILES_MOST 65536u
#endif
#define FILES_NAMES (FILES_MOST * 24u)
#define FILES_HISTORY 32
#define FILES_KEPT 8
#ifndef FILES_KEPT_BYTES
#define FILES_KEPT_BYTES (8u << 20)
#endif
#define FILES_NO 0xffffffffu

enum { FILES_KIND_DIRECTORY, FILES_KIND_FILE, FILES_KIND_LINK, FILES_KIND_OTHER };
enum { FILES_NONE, FILES_READING, FILES_READY, FILES_UNREADABLE, FILES_GONE, FILES_NOT_DIRECTORY, FILES_FAILED };
enum { FILES_SORT_NAME, FILES_SORT_SIZE, FILES_SORT_TIME, FILES_SORT_KIND };
enum { FILES_SAME, FILES_CHANGED, FILES_MISSING, FILES_REFUSED };
enum { FILES_FRESH, FILES_STALE, FILES_LOST };
enum { FILES_CHOOSE_ONLY, FILES_CHOOSE_TOGGLE, FILES_CHOOSE_RANGE };

struct files_entry
{
        p32 name;
        p16 length, mode;
        p8 kind, looked;
        p64 size, inode;
        b64 modified, born;
        p32 device_major, device_minor, born_part;
};

struct files_listing
{
        struct files_entry address_to entry;
        p8 address_to names;
        p32 address_to order;
        p32 count, names_used, places;
        b32 cut;
};

struct files_kept
{
        p8 path[FILES_PATH];
        address_any mapped;
        positive bytes;
        p32 count, names_used, cursor, anchor, chosen_count;
        b32 cut, descending, hidden;
        file_facts folder;
        int sort;
        bipolar seat;
        p64 left;
};

struct files_model
{
        p8 path[FILES_PATH], filter[FILES_NAME], came[FILES_NAME], peer[WATERLINK_NAME_MAX];
        int state, sort;
        bipolar error, handle, seat;
        struct files_listing shown, next;
        b32 reloading, walking, descending, hidden, restored;
        file_walk reader;
        file_facts folder;
        positive filter_length, mapped_size;
        p32 cursor, anchor, chosen_count, back_count, forward_count, revision, listed, reads;
        p8 address_to chosen, address_to back, address_to forward, address_to back_peer, address_to forward_peer;
        struct files_kept kept[FILES_KEPT];
        p64 kept_clock;
        address_any mapped;
};

static inline b32 files_model_is_remote(const struct files_model address_to m) { return m->peer[0] != 0; }

/* ---- memory & syscall primitives ---- */

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

static inline fn files_unmap(address_any ptr, positive bytes) { if (ptr) system_call_2(syscall(munmap), (positive)ptr, bytes); }
static inline fn files_close(bipolar address_to fd) { if (*fd >= 0) system_close(*fd), *fd = -1; }
static inline bipolar files_look_fd(bipolar fd, file_facts address_to facts) { return file_look_code(fd, (string_address) "", AT_EMPTY_PATH, facts); }
static inline bipolar files_look_at(bipolar dir, const p8 address_to name, file_facts address_to facts) { return file_look_code(dir, (string_address)name, AT_SYMLINK_NOFOLLOW, facts); }
static inline bipolar files_open_dir(bipolar dir, const p8 address_to path, p32 extra) { return system_open_at(dir, (string_address)path, FILE_READ | O_DIRECTORY | O_CLOEXEC | extra); }
static inline bipolar files_mkdir(bipolar dir, const p8 address_to path, p32 mode) { return (bipolar)system_call_3(syscall(mkdirat), (positive)dir, (positive)path, mode); }
static inline bipolar files_rename_nr(bipolar d1, const p8 address_to a, bipolar d2, const p8 address_to b) { return (bipolar)system_call_5(syscall(renameat2), (positive)d1, (positive)a, (positive)d2, (positive)b, FILE_RENAME_NOREPLACE); }
static inline b32 files_same_file(p64 ino, p32 maj, p32 min, const file_facts address_to f) { return ino == f->inode && maj == f->device_major && min == f->device_minor; }
static inline b32 files_same_facts(const file_facts address_to a, const file_facts address_to b) { return files_same_file(a->inode, a->device_major, a->device_minor, b); }
static inline fn files_walk_stop(struct files_model address_to m) { if (m->walking) file_walk_close(&m->reader); m->walking = false; }
static inline fn files_listing_empty(struct files_listing address_to l) { l->count = l->names_used = l->places = 0, l->cut = false; }

static b32 files_path_join(p8 address_to into, positive room, const p8 address_to folder, const p8 address_to name)
{
        positive flen = string_length((string_address)folder), nlen = string_length((string_address)name);
        b32 slash = flen > 1 && folder[flen - 1] != '/';
        if (flen + slash + nlen >= room)
                return false;
        memory_copy(into, folder, flen);
        if (slash)
                into[flen++] = '/';
        return memory_copy(into + flen, name, nlen + 1), true;
}

#define FILES_MODEL_BYTES                                                                                             \
        (2 * ((positive)FILES_MOST * (sizeof(struct files_entry) + sizeof(p32)) + FILES_NAMES) + FILES_MOST / 8 +     \
         2 * (positive)FILES_HISTORY * (FILES_PATH + WATERLINK_NAME_MAX))

static fn files_keep_drop(struct files_model address_to m, int which);

static b32 files_model_open(struct files_model address_to m)
{
        p8 address_to at;
        memory_fill(m, 0, sizeof(*m));
        m->handle = m->reader.handle = -1, m->cursor = m->anchor = FILES_NO;
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
        m->forward = at, at += (positive)FILES_HISTORY * FILES_PATH;
        m->back_peer = at, at += (positive)FILES_HISTORY * WATERLINK_NAME_MAX;
        m->forward_peer = at;
        return true;
}

static fn files_model_close(struct files_model address_to m)
{
        for (int k = 0; k < FILES_KEPT; k++)
                files_keep_drop(m, k);
        files_walk_stop(m), files_close(&m->handle), files_unmap(m->mapped, m->mapped_size);
        memory_fill(m, 0, sizeof(*m));
        m->handle = m->reader.handle = -1;
}

/* ---- entries, order, filtering, selection ---- */

static inline p32 files_model_count(const struct files_model address_to m) { return m->shown.places; }
static inline const struct files_entry address_to files_model_entry(const struct files_model address_to m, p32 place) { return place < m->shown.places ? &m->shown.entry[m->shown.order[place]] : null; }
static inline const p8 address_to files_model_name(const struct files_model address_to m, const struct files_entry address_to e) { return m->shown.names + e->name; }
static inline p8 files_kind_of(p32 mode) { p32 f = mode & MODE_FORMAT; return f == MODE_DIRECTORY ? FILES_KIND_DIRECTORY : f == MODE_LINK ? FILES_KIND_LINK : f == MODE_FILE ? FILES_KIND_FILE : FILES_KIND_OTHER; }
static inline p8 files_kind_of_record(p8 t) { return t == DT_DIR ? FILES_KIND_DIRECTORY : t == DT_LNK ? FILES_KIND_LINK : t == DT_REG ? FILES_KIND_FILE : FILES_KIND_OTHER; }

static int files_name_order(const p8 address_to a, const p8 address_to b)
{
        b32 d = string_compare_folded((string_address)a, (string_address)b);
        if (!d)
                d = string_compare((string_address)a, (string_address)b);
        return d < 0 ? -1 : d > 0;
}

static positive files_name_dot(const p8 address_to name, positive length)
{
        for (positive at = length; at > 1; at--)
                if (name[at - 1] == '.')
                        return at - 1;
        return length;
}

struct files_order { const struct files_listing address_to listing; int sort; b32 descending; };

// Folders always first, then sort key (flipped when descending), then name tie-breaker.
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
                const struct files_entry address_to e = &l->entry[l->order[at]];
                if (e->length >= length && memory_search_ascii_case(l->names + e->name, e->length, filter, length))
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
                const struct files_entry address_to e = &m->shown.entry[m->shown.order[place]];
                if (e->length == length && !memory_compare((address_any)(m->shown.names + e->name), (address_any)name, length))
                        return place;
        }
        return FILES_NO;
}

static inline b32 files_model_chosen(const struct files_model address_to m, p32 place)
{
        p32 e = place < m->shown.places ? m->shown.order[place] : FILES_NO;
        return e != FILES_NO && (m->chosen[e >> 3] >> (e & 7) & 1);
}

static fn files_model_choose_none(struct files_model address_to m)
{
        if (m->chosen_count)
                memory_fill(m->chosen, 0, FILES_MOST / 8), m->revision++;
        m->chosen_count = 0, m->anchor = FILES_NO;
}

static inline fn files_chosen_put(struct files_model address_to m, p32 e, b32 on)
{
        p8 bit = (p8)(1u << (e & 7));
        if (!(m->chosen[e >> 3] & bit) != !on)
                m->chosen[e >> 3] ^= bit, m->chosen_count += on ? 1 : (p32)-1;
}

static fn files_model_choose(struct files_model address_to m, p32 place, int how)
{
        p32 entry;
        if (place >= m->shown.places)
                return;
        entry = m->shown.order[place];
        if (how == FILES_CHOOSE_TOGGLE)
                files_chosen_put(m, entry, !(m->chosen[entry >> 3] >> (entry & 7) & 1)), m->anchor = entry;
        else
        {
                p32 from = how == FILES_CHOOSE_RANGE && m->anchor != FILES_NO ? files_order_place(&m->shown, m->anchor) : place, lo, hi;
                if (how != FILES_CHOOSE_RANGE || m->anchor == FILES_NO)
                        m->anchor = entry;
                if (from == FILES_NO)
                        from = place;
                lo = from < place ? from : place, hi = from < place ? place : from;
                memory_fill(m->chosen, 0, FILES_MOST / 8), m->chosen_count = 0;
                for (p32 at = lo; at <= hi; at++)
                        files_chosen_put(m, m->shown.order[at], true);
        }
        m->cursor = place, m->revision++;
}

static inline fn files_select_all(struct files_model address_to m) { if (files_model_count(m)) files_model_choose(m, 0, FILES_CHOOSE_ONLY), files_model_choose(m, files_model_count(m) - 1, FILES_CHOOSE_RANGE); }

static p32 files_selected_next(const struct files_model address_to m, p32 address_to at)
{
        p32 count = files_model_count(m);
        if (!m->chosen_count)
        {
                p32 cur = *at == 0 && m->cursor < count ? m->cursor : FILES_NO;
                return *at = count, cur;
        }
        while (*at < count)
        {
                p32 place = (*at)++;
                if (files_model_chosen(m, place))
                        return place;
        }
        return FILES_NO;
}

static fn files_model_order(struct files_model address_to m)
{
        p32 on = m->cursor < m->shown.places ? m->shown.order[m->cursor] : FILES_NO, place;
        files_listing_sort(&m->shown, m->sort, m->descending);
        files_listing_filter(&m->shown, m->filter, m->filter_length);
        place = files_order_place(&m->shown, on);
        m->cursor = place != FILES_NO ? place : m->shown.places ? 0 : FILES_NO, m->revision++;
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
        memory_copy(m->filter, text, length), m->filter[length] = end, m->filter_length = length;
        if (m->state == FILES_READY)
                files_model_order(m);
}

/* ---- reading & kept listings ---- */

static int files_state_of(bipolar e)
{
        return e == -ERROR_ACCESS || e == -1 ? FILES_UNREADABLE : e == -ERROR_NO_ENTRY || e == -116 ? FILES_GONE
               : e == -ERROR_NOT_DIRECTORY || e == -40 ? FILES_NOT_DIRECTORY : FILES_FAILED;
}

static fn files_model_fail(struct files_model address_to m, bipolar error, b32 clear)
{
        if (clear)
                files_listing_empty(&m->shown), files_model_choose_none(m), m->reloading = false, m->cursor = FILES_NO;
        m->state = files_state_of(error), m->error = m->state == FILES_FAILED ? error : 0;
}

static b32 files_listing_take(struct files_model address_to m, struct files_listing address_to l, const struct linux_dirent64 address_to record)
{
        positive length = string_length((string_address)record->d_name);
        struct files_entry address_to e;
        file_facts facts;
        bipolar looked;
        if (file_is_dot((string_address)record->d_name) || (record->d_name[0] == '.' && !m->hidden) || !length || length >= FILES_NAME)
                return true;
        if ((looked = files_look_at(m->reader.handle, (const p8 address_to)record->d_name, &facts)) == -ERROR_NO_ENTRY)
                return true;
        if (l->count >= FILES_MOST || l->names_used + length + 1 > FILES_NAMES)
                return l->cut = true, false;
        e = &l->entry[l->count];
        memory_fill(e, 0, sizeof(*e));
        e->name = l->names_used, e->length = (p16)length;
        memory_copy(l->names + l->names_used, record->d_name, length + 1), l->names_used += (p32)length + 1;
        if (looked == 0)
        {
                file_facts named;
                e->kind = files_kind_of(facts.mode), e->looked = 1, e->mode = (p16)facts.mode;
                e->size = facts.size, e->modified = facts.modified.seconds, e->inode = facts.inode;
                if (e->kind == FILES_KIND_LINK)
                        e->mode = file_look_code(m->reader.handle, (string_address)record->d_name, 0, &named) == 0 ? (p16)named.mode : 0;
                e->device_major = facts.device_major, e->device_minor = facts.device_minor;
                e->born = facts.created.seconds, e->born_part = facts.created.nanoseconds;
        }
        else
                e->kind = files_kind_of_record(record->d_type), e->inode = record->d_ino;
        return l->order[l->count] = l->count, l->places = ++l->count, true;
}

static fn files_model_finish(struct files_model address_to m, bipolar error)
{
        struct files_listing address_to l = m->reloading ? &m->next : &m->shown;
        files_walk_stop(m);
        if (error < 0 && !l->count)
        {
                files_model_fail(m, error, true), m->revision++;
                return;
        }
        files_listing_sort(l, m->sort, m->descending);
        files_listing_filter(l, m->filter, m->filter_length);
        if (m->reloading)
        {
                struct files_listing was = m->shown;
                m->shown = m->next, m->next = was;
                files_listing_empty(&m->next), m->reloading = false, files_model_choose_none(m);
        }
        m->state = FILES_READY, m->error = 0, m->listed++;
        m->cursor = m->came[0] ? files_model_find(m, m->came) : FILES_NO;
        if (m->cursor == FILES_NO && m->shown.places)
                m->cursor = 0;
        m->came[0] = end, m->revision++;
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
                        return files_model_finish(m, record ? 0 : m->reader.error), false;
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
        return m->walking = true, m->reads++, true;
}

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

static inline positive files_keep_bytes(p32 count, p32 names_used) { return (positive)count * (sizeof(struct files_entry) + sizeof(p32)) + names_used + (count + 7) / 8; }

static p8 address_to files_keep_listing(const struct files_kept address_to k, struct files_listing address_to l)
{
        p8 address_to at = (p8 address_to)k->mapped;
        l->entry = (struct files_entry address_to)at, at += (positive)k->count * sizeof(struct files_entry);
        l->order = (p32 address_to)at, at += (positive)k->count * sizeof(p32);
        l->names = at, l->count = k->count, l->names_used = k->names_used, l->cut = k->cut;
        return at + k->names_used;
}

static fn files_keep_copy(const struct files_kept address_to k, struct files_model address_to m, b32 save)
{
        struct files_listing kl;
        p8 address_to chosen = files_keep_listing(k, &kl);
        struct { address_any kptr; address_any mptr; positive n; } seg[4] = {
            {kl.entry, m->shown.entry, (positive)k->count * sizeof(struct files_entry)},
            {kl.order, m->shown.order, (positive)k->count * sizeof(p32)},
            {kl.names, m->shown.names, k->names_used},
            {chosen, m->chosen, (k->count + 7) / 8},
        };
        for (int i = 0; i < 4; i++)
                memory_copy(save ? seg[i].kptr : seg[i].mptr, save ? seg[i].mptr : seg[i].kptr, seg[i].n);
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
                m->filter_length = 0, m->filter[0] = end, files_model_order(m);
        for (int at = 0; which < 0 && at < FILES_KEPT; at++)
                which = m->kept[at].path[0] ? -1 : at;
        if (which < 0)
                files_keep_drop(m, which = files_keep_oldest(m, -1));
        for (int at = 0; at < FILES_KEPT; at++)
                held += m->kept[at].bytes;
        while (held + bytes > FILES_KEPT_BYTES)
        {
                int oldest = files_keep_oldest(m, which);
                held -= m->kept[oldest].bytes, files_keep_drop(m, oldest);
        }
        if (bytes && !(mapped = files_anon_map(bytes, null)))
                return;
        k = &m->kept[which];
        string_copy_bounded(k->path, m->path, FILES_PATH);
        k->mapped = mapped, k->bytes = bytes, k->left = ++m->kept_clock;
        k->count = m->shown.count, k->names_used = m->shown.names_used, k->cut = m->shown.cut;
        k->folder = m->folder, k->sort = m->sort, k->descending = m->descending, k->hidden = m->hidden;
        k->cursor = m->cursor, k->anchor = m->anchor, k->chosen_count = m->chosen_count, k->seat = m->seat;
        if (bytes)
                files_keep_copy(k, m, true);
}

static fn files_model_show(struct files_model address_to m, string_address path, bipolar handle, bipolar error)
{
        file_facts now;
        int kept;
        files_walk_stop(m), m->reloading = false, files_keep_put(m);
        m->peer[0] = end;
        m->filter_length = 0, m->filter[0] = end;
        files_close(&m->handle), m->handle = handle;
        if ((const p8 address_to)path != m->path)
                string_copy_bounded(m->path, path, FILES_PATH);
        files_listing_empty(&m->shown), files_listing_empty(&m->next), files_model_choose_none(m);
        m->cursor = FILES_NO, m->seat = 0, m->restored = false;
        if ((kept = files_keep_find(m, m->path)) >= 0)
        {
                const struct files_kept address_to k = &m->kept[kept];
                if (handle >= 0 && k->hidden == m->hidden && files_look_fd(handle, &now) >= 0 && files_same_facts(&k->folder, &now))
                {
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
                        files_keep_drop(m, kept);
                        m->state = FILES_READY, m->error = 0, m->restored = true, m->came[0] = end, m->listed++, m->revision++;
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
                m->reloading = false, files_listing_empty(&m->shown), files_model_fail(m, m->reader.error, false);
        m->revision++;
}

static int files_model_stale(struct files_model address_to m)
{
        file_facts now, named;
        if (m->handle < 0 || m->state == FILES_GONE)
                return m->state == FILES_GONE ? FILES_LOST : FILES_FRESH;
        if (files_look_fd(m->handle, &now) < 0 || !now.hard_links ||
            file_look_code(AT_FDCWD, m->path, 0, &named) < 0 || !files_same_facts(&now, &named))
                return files_walk_stop(m), files_model_fail(m, -ERROR_NO_ENTRY, true), m->revision++, FILES_LOST;
        return (now.modified.seconds != m->folder.modified.seconds || now.modified.nanoseconds != m->folder.modified.nanoseconds ||
                now.changed.seconds != m->folder.changed.seconds || now.changed.nanoseconds != m->folder.changed.nanoseconds)
                   ? FILES_STALE : FILES_FRESH;
}

static int files_model_check(struct files_model address_to m, p32 place, file_facts address_to now)
{
        struct files_entry address_to e = place < m->shown.places ? &m->shown.entry[m->shown.order[place]] : null;
        file_facts facts;
        bipolar looked;
        if (!e || m->handle < 0)
                return FILES_REFUSED;
        looked = files_look_at(m->handle, m->shown.names + e->name, &facts);
        if (now)
                *now = facts;
        if (looked < 0)
                return looked == -ERROR_NO_ENTRY || looked == -ERROR_NOT_DIRECTORY ? FILES_MISSING : FILES_CHANGED;
        if (files_kind_of(facts.mode) != e->kind ||
            (e->looked && (!files_same_file(e->inode, e->device_major, e->device_minor, &facts) ||
                           facts.created.seconds != e->born || facts.created.nanoseconds != e->born_part)))
                return FILES_CHANGED;
        return e->size = facts.size, e->modified = facts.modified.seconds, FILES_SAME;
}

/* ---- paths, history, navigation, completion ---- */

static b32 files_path_take(p8 address_to into, string_address path)
{
        positive length = path ? string_length(path) : 0;
        if (!length || path[0] != '/' || length >= FILES_PATH)
                return false;
        while (length > 1 && path[length - 1] == '/')
                length--;
        return memory_copy(into, path, length), into[length] = end, true;
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
        return parent[cut] = end, true;
}

static fn files_history_push(p8 address_to stack, p8 address_to peer_stack, p32 address_to count, const p8 address_to peer, const p8 address_to path)
{
        if (*count == FILES_HISTORY)
        {
                for (p32 at = 0; at + 1 < FILES_HISTORY; at++)
                {
                        memory_copy(stack + (positive)at * FILES_PATH, stack + (positive)(at + 1) * FILES_PATH, FILES_PATH);
                        memory_copy(peer_stack + (positive)at * WATERLINK_NAME_MAX, peer_stack + (positive)(at + 1) * WATERLINK_NAME_MAX, WATERLINK_NAME_MAX);
                }
                (*count)--;
        }
        string_copy_bounded(stack + (positive)(*count) * FILES_PATH, (string_address)path, FILES_PATH);
        string_copy_bounded(peer_stack + (positive)(*count) * WATERLINK_NAME_MAX, (string_address)(peer ? peer : (const p8 address_to) ""), WATERLINK_NAME_MAX);
        (*count)++;
}

static fn files_model_commit(struct files_model address_to m, string_address path, bipolar handle, bipolar error)
{
        if (m->state != FILES_NONE)
                files_history_push(m->back, m->back_peer, &m->back_count, m->peer, m->path);
        m->forward_count = 0, m->came[0] = end, files_model_show(m, path, handle, error);
}

static b32 files_model_go_remote(struct files_model address_to m, const p8 address_to peer, string_address path, b32 commit)
{
        p8 taken[FILES_PATH];
        if (!peer || !peer[0] || !files_path_take(taken, path))
                return false;
        files_walk_stop(m), m->reloading = false, files_keep_put(m);
        if (commit && m->state != FILES_NONE)
                files_history_push(m->back, m->back_peer, &m->back_count, m->peer, m->path), m->forward_count = 0, m->came[0] = end;
        m->filter_length = 0, m->filter[0] = end;
        files_close(&m->handle);
        string_copy_bounded(m->peer, (string_address)peer, WATERLINK_NAME_MAX);
        string_copy_bounded(m->path, (string_address)taken, FILES_PATH);
        files_listing_empty(&m->shown), files_listing_empty(&m->next), files_model_choose_none(m);
        m->cursor = FILES_NO, m->seat = 0, m->restored = false;
        m->state = FILES_READING, m->error = 0, m->revision++;
        return true;
}

// A name a peer sent: one component, not "." or "..", no slash or NUL. Its bytes are not trusted until this says so.
static b32 files_peer_name_ok(const p8 address_to s, positive n)
{
        if (!n || n >= FILES_NAME)
                return false;
        if ((n == 1 && s[0] == '.') || (n == 2 && s[0] == '.' && s[1] == '.'))
                return false;
        for (positive k = 0; k < n; k++)
                if (s[k] == '/' || s[k] == 0)
                        return false;
        return true;
}

static fn files_model_take_remote(struct files_model address_to m, const p8 address_to data, positive length)
{
        struct files_listing address_to l = m->reloading ? &m->next : &m->shown;
        b32 status;
        p32 count;
        positive at = 8;
        if (!data || length < 4)
        {
                files_model_finish(m, -5);
                return;
        }
        status = (b32)link_fs_get32(data);
        if (status < 0)
        {
                files_model_finish(m, (bipolar)status);
                return;
        }
        if (length < 8)
        {
                files_model_finish(m, -5);
                return;
        }
        count = link_fs_get32(data + 4);
        files_listing_empty(l);
        for (p32 k = 0; k < count && at + 37 <= length; k++)
        {
                p8 kind = data[at];
                p16 mode = link_fs_get16(data + at + 1);
                p64 size = link_fs_get64(data + at + 3);
                b64 modified = (b64)link_fs_get64(data + at + 11);
                b64 born = (b64)link_fs_get64(data + at + 19);
                p64 inode = link_fs_get64(data + at + 27);
                p16 nlen = link_fs_get16(data + at + 35);
                at += 37;
                if (at + nlen > length)
                        break;
                if (files_peer_name_ok(data + at, nlen))
                {
                        if (l->count >= FILES_MOST || l->names_used + nlen + 1 > FILES_NAMES)
                        {
                                l->cut = true;
                                break;
                        }
                        {
                                struct files_entry address_to e = &l->entry[l->count];
                                memory_fill(e, 0, sizeof(*e));
                                e->name = l->names_used, e->length = nlen;
                                memory_copy(l->names + l->names_used, data + at, nlen);
                                l->names[l->names_used + nlen] = end;
                                l->names_used += (p32)nlen + 1;
                                e->kind = kind > FILES_KIND_OTHER ? FILES_KIND_OTHER : kind, e->looked = 1, e->mode = mode;
                                e->size = size, e->modified = modified, e->born = born, e->inode = inode;
                                l->order[l->count] = l->count, l->places = ++l->count;
                        }
                }
                at += nlen;
        }
        files_model_finish(m, 0);
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
        return files_path_take(taken, path) ? (files_model_open_path(m, taken, true), true) : false;
}

static int files_model_try(struct files_model address_to m, string_address path)
{
        p8 taken[FILES_PATH];
        file_walk opened;
        if (!files_path_take(taken, path))
                return FILES_NOT_DIRECTORY;
        if (!file_walk_open(&opened, AT_FDCWD, taken))
                return files_state_of(opened.error);
        return files_model_commit(m, taken, opened.handle, 0), FILES_READING;
}

static p32 files_listing_complete(const struct files_listing address_to l, const p8 address_to start, positive length, p8 address_to into, positive room)
{
        p32 found = 0;
        positive shared = 0;
        if (!room)
                return 0;
        length = length < room - 1 ? length : room - 1;
        memory_copy(into, start, length), into[length] = end;
        for (p32 place = 0; l && place < l->count; place++)
        {
                const struct files_entry address_to e = &l->entry[l->order[place]];
                const p8 address_to name = l->names + e->name;
                if (e->kind != FILES_KIND_DIRECTORY || e->length < length || e->length >= room ||
                    (length && memory_compare((address_any)name, (address_any)start, length)))
                        continue;
                if (!found++)
                {
                        memory_copy(into, name, (positive)e->length + 1), shared = e->length;
                        continue;
                }
                for (positive at = length; at < shared; at++)
                        if (at >= e->length || into[at] != name[at])
                        {
                                shared = at;
                                break;
                        }
                into[shared] = end;
        }
        return found;
}

static inline p32 files_model_complete(const struct files_model address_to m, const p8 address_to start, positive length, p8 address_to into, positive room)
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
                return files_keep_listing(&m->kept[which], &kept), files_listing_complete(&kept, start, length, into, room);
        return files_listing_complete(null, start, length, into, room);
}

static inline fn files_model_take(struct files_model address_to m, string_address path, bipolar handle) { files_model_commit(m, path, handle, 0); }

static int files_model_enter(struct files_model address_to m, p32 place)
{
        const struct files_entry address_to e = files_model_entry(m, place);
        p8 into[FILES_PATH];
        file_walk opened;
        int seen;
        if (!e)
                return FILES_REFUSED;
        if ((seen = files_model_check(m, place, null)) != FILES_SAME)
                return seen;
        if (e->kind != FILES_KIND_DIRECTORY || !files_path_join(into, FILES_PATH, m->path, files_model_name(m, e)))
                return FILES_REFUSED;
        if (!file_walk_open_found(&opened, m->handle, (string_address)files_model_name(m, e)))
                return opened.error == -ERROR_NO_ENTRY ? FILES_MISSING : FILES_CHANGED;
        return files_model_commit(m, into, opened.handle, 0), FILES_SAME;
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
        if (files_model_is_remote(m))
                return files_model_go_remote(m, m->peer, (string_address)parent, true), files_model_came(m, name), true;
        return files_model_open_path(m, parent, true), files_model_came(m, name), true;
}

static b32 files_model_back(struct files_model address_to m, b32 forward)
{
        p8 address_to from = forward ? m->forward : m->back, address_to onto = forward ? m->back : m->forward;
        p8 address_to from_peer = forward ? m->forward_peer : m->back_peer, address_to onto_peer = forward ? m->back_peer : m->forward_peer;
        p32 address_to fc = forward ? &m->forward_count : &m->back_count, address_to oc = forward ? &m->back_count : &m->forward_count;
        p8 to[FILES_PATH], to_peer[WATERLINK_NAME_MAX], name[FILES_NAME], parent[FILES_PATH];
        if (!*fc)
                return false;
        --(*fc);
        string_copy_bounded(to, (string_address)(from + (positive)(*fc) * FILES_PATH), FILES_PATH);
        string_copy_bounded(to_peer, (string_address)(from_peer + (positive)(*fc) * WATERLINK_NAME_MAX), WATERLINK_NAME_MAX);
        files_history_push(onto, onto_peer, oc, m->peer, m->path);
        if (!string_equals((string_address)m->peer, (string_address)to_peer) || !files_path_parent(m->path, parent, name) || !string_equals(parent, to))
                name[0] = end;
        if (to_peer[0])
                return files_model_go_remote(m, to_peer, (string_address)to, false), files_model_came(m, name), true;
        return files_model_open_path(m, to, false), files_model_came(m, name), true;
}

/* ---- trail ---- */

#define FILES_TRAIL 48

struct files_trail
{
        p8 path[FILES_PATH], known[FILES_TRAIL], lost[FILES_TRAIL];
        p16 from[FILES_TRAIL], to[FILES_TRAIL];
        p64 inode[FILES_TRAIL];
        p32 device_major[FILES_TRAIL], device_minor[FILES_TRAIL];
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
                        t->from[t->count] = (p16)at, t->to[t->count++] = (p16)stop;
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
                b32 same = k < was.count && was.known[k] && was.to[k] == t->to[k] && was.from[k] == t->from[k] &&
                           !memory_compare((address_any)was.path, (address_any)t->path, t->to[k]);
                t->known[k] = same, t->lost[k] = 0, t->inode[k] = same ? was.inode[k] : 0;
                t->device_major[k] = same ? was.device_major[k] : 0, t->device_minor[k] = same ? was.device_minor[k] : 0;
        }
}

static b32 files_trail_note(struct files_trail address_to t, const p8 address_to shown, const file_facts address_to folder)
{
        positive length = string_length((string_address)shown);
        int place = files_trail_index(t, shown, length);
        if (place < 0)
                files_trail_become(t, shown), place = t->count - 1;
        t->current = place < 0 ? 0 : place;
        for (int k = 0; k <= t->current && k < t->count; k++)
                t->lost[k] = 0;
        if (folder && place >= 0 && folder->inode)
                t->known[place] = 1, t->inode[place] = folder->inode, t->device_major[place] = folder->device_major, t->device_minor[place] = folder->device_minor;
        return t->to[t->count ? t->count - 1 : 0] != length;
}

static inline fn files_trail_path(const struct files_trail address_to t, int which, p8 address_to into)
{
        positive len = which >= 0 && which < t->count ? t->to[which] : 0;
        memory_copy(into, t->path, len), into[len] = end;
}

static int files_trail_open(const struct files_trail address_to t, int which, bipolar address_to handle, bipolar address_to error)
{
        p8 path[FILES_PATH];
        file_facts facts;
        bipolar opened;
        *handle = -1, *error = 0;
        if (which < 0 || which >= t->count)
                return FILES_REFUSED;
        files_trail_path(t, which, path);
        if ((opened = files_open_dir(AT_FDCWD, path, O_NOFOLLOW)) < 0)
        {
                if (opened == -ERROR_NO_ENTRY)
                        return FILES_MISSING;
                if (opened == -ERROR_NOT_DIRECTORY || opened == -40)
                        return files_look_at(AT_FDCWD, path, &facts) < 0 ? FILES_MISSING : FILES_CHANGED;
                return *error = opened, FILES_REFUSED;
        }
        if (files_look_fd(opened, &facts) < 0 ||
            (t->known[which] && !files_same_file(t->inode[which], t->device_major[which], t->device_minor[which], &facts)))
                return system_close(opened), FILES_CHANGED;
        return *handle = opened, FILES_SAME;
}

static int files_trail_check(const struct files_trail address_to t, int which)
{
        bipolar handle, error;
        int seen = files_trail_open(t, which, &handle, &error);
        return files_close(&handle), seen == FILES_REFUSED && error ? FILES_CHANGED : seen;
}

static inline fn files_trail_cut(struct files_trail address_to t, int which) { if (which > t->current && which < t->count) t->path[t->to[which - 1]] = end, t->count = which; }
static inline fn files_trail_lose(struct files_trail address_to t, int which) { for (int k = which > t->current ? which : t->current + 1; k < t->count; k++) t->lost[k] = 1; }

static fn files_keep_trim(struct files_model address_to m, const struct files_trail address_to t)
{
        for (int k = 0; k < FILES_KEPT; k++)
        {
                positive len = string_length((string_address)m->kept[k].path);
                if (len && files_trail_index(t, m->kept[k].path, len) < 0)
                        files_keep_drop(m, k);
        }
}

static b32 files_trail_spell(const struct files_trail address_to t, int which, const p8 address_to text, p8 address_to into)
{
        positive length = string_length((string_address)text), used = 0, whole = string_length((string_address)t->path);
        if (which < 0 || which >= t->count || t->to[which] <= 1 || (which == 0 && t->from[0] == 0))
                return false;
        for (positive at = 0; at < length; at++)
                if (text[at] < 0x20 || text[at] == 0x7f)
                        return false;
        {
                const p8 address_to from[3] = {t->path, text, t->path + t->to[which]};
                positive lens[3] = {t->from[which], length, whole - t->to[which]};
                for (int part = 0; part < 3; part++)
                        for (positive at = 0; at < lens[part]; at++)
                        {
                                p8 b = from[part][at];
                                if (b == '/' && used && into[used - 1] == '/')
                                        continue;
                                if (used + 1 >= FILES_PATH)
                                        return false;
                                into[used++] = b;
                        }
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

static int files_trail_resolve(struct files_model address_to m, struct files_trail address_to t, const p8 address_to path)
{
        static struct files_trail next;
        p8 part[FILES_PATH];
        file_walk opened = {.handle = -1};
        int why = FILES_READY, deepest;
        memory_fill(&next, 0, sizeof(next));
        string_copy_bounded(next.path, (string_address)path, FILES_PATH);
        files_trail_split(&next);
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
        return files_trail_lose(t, deepest + 1), why;
}

/* ---- folder mutations & recursive delete ---- */

#define FILES_JOB_DEEP 64
enum { FILES_DID, FILES_DID_EXISTS, FILES_DID_BAD, FILES_DID_FAILED };
enum { FILES_JOB_IDLE, FILES_JOB_RUNNING, FILES_JOB_DONE, FILES_JOB_FAILED, FILES_JOB_CANCELLED };

struct files_job
{
        int state, depth;
        bipolar error, stack[FILES_JOB_DEEP];
        p8 at[FILES_NAME], inside[FILES_JOB_DEEP][FILES_NAME], address_to names;
        p32 removed, count, next, address_to offsets;
        file_facts address_to facts;
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

static inline int files_did_code(bipolar res) { return res >= 0 ? FILES_DID : res == -ERROR_EXISTS ? FILES_DID_EXISTS : FILES_DID_FAILED; }
static int files_model_make_folder(struct files_model address_to m, const p8 address_to name) { return m->handle < 0 || !files_name_good(name) ? FILES_DID_BAD : files_did_code(files_mkdir(m->handle, name, 0755)); }

static int files_model_rename(struct files_model address_to m, p32 place, const p8 address_to name)
{
        const struct files_entry address_to e = files_model_entry(m, place);
        if (!e || !files_name_good(name) || files_model_check(m, place, null) != FILES_SAME)
                return FILES_DID_BAD;
        return files_did_code(files_rename_nr(m->handle, files_model_name(m, e), m->handle, name));
}

#define FILES_JOB_NAMES ((positive)FILES_MOST * FILES_NAME / 8)
#define FILES_JOB_BYTES (FILES_JOB_NAMES + (positive)FILES_MOST / 8 * (sizeof(file_facts) + sizeof(p32)))

static inline fn files_job_close(struct files_job address_to job) { while (job->depth > 0) system_close(job->stack[--job->depth]); }
static inline fn files_job_end(struct files_job address_to job) { files_job_close(job), files_unmap(job->names, FILES_JOB_BYTES), memory_fill(job, 0, sizeof(*job)); }
static inline fn files_job_cancel(struct files_job address_to job) { if (job->state == FILES_JOB_RUNNING) files_job_close(job), job->state = FILES_JOB_CANCELLED; }

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
                const struct files_entry address_to e = &m->shown.entry[m->shown.order[place]];
                if (job->count >= FILES_MOST / 8 || used + e->length + 1 > FILES_JOB_NAMES)
                        break;
                if (files_model_check(m, place, &job->facts[job->count]) != FILES_SAME)
                {
                        files_job_end(job);
                        string_copy_bounded(job->at, (string_address)files_model_name(m, e), FILES_NAME);
                        return job->state = FILES_JOB_FAILED, job->error = -ERROR_NO_ENTRY, false;
                }
                job->offsets[job->count++] = (p32)used;
                memory_copy(job->names + used, files_model_name(m, e), (positive)e->length + 1), used += (positive)e->length + 1;
        }
        return job->state = job->count ? FILES_JOB_RUNNING : FILES_JOB_DONE, job->count != 0;
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
                string_copy_bounded(at, (string_address)top_name, FILES_NAME), facts = *top_facts;
        else
        {
                if (!file_walk_open(&walk, in, (string_address) "."))
                        return *error = walk.error, -1;
                while ((record = file_walk_next(&walk)) && file_is_dot((string_address)record->d_name))
                        ;
                if (record)
                        string_copy_bounded(at, (string_address)record->d_name, FILES_NAME);
                gone = walk.error, file_walk_close(&walk);
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
                return string_copy_bounded(inside[*depth], at, FILES_NAME), stack[(*depth)++] = walk.handle, 0;
        }
        if ((gone = file_remove_same(in, (string_address)at, 0, &facts)) < 0)
                return *error = gone, -1;
        if (removed)
                (*removed)++;
        return top ? 1 : 0;
}

static b32 files_job_step(struct files_model address_to m, struct files_job address_to job, positive most)
{
        for (positive done = 0; done < most && job->state == FILES_JOB_RUNNING; done++)
        {
                bipolar err = 0;
                int step;
                if (!job->depth && job->next >= job->count)
                        return job->state = FILES_JOB_DONE, false;
                step = files_rmdir_step(m->handle, job->stack, job->inside, &job->depth, job->at,
                                        job->depth ? null : job->names + job->offsets[job->next],
                                        job->depth ? null : &job->facts[job->next], &job->removed, &err);
                if (step < 0)
                        return files_job_close(job), job->state = FILES_JOB_FAILED, job->error = err, false;
                job->next += step;
        }
        return job->state == FILES_JOB_RUNNING;
}

/* ---- sidebar folder/drive tree ---- */

#define FILES_TREE_MOST 4096
#define FILES_TREE_NAMES (FILES_TREE_MOST * 32u)
#define FILES_TREE_CHILDREN 1024
#define FILES_TREE_SCAN 65536
#define FILES_DRIVES_MOST 32
#define FILES_TREE_BYTES ((positive)FILES_TREE_MOST * sizeof(struct files_node) + FILES_TREE_NAMES)

enum { FILES_NODE_FOLDER, FILES_NODE_PLACE, FILES_NODE_DRIVE, FILES_NODE_MACHINE };
enum { FILES_PLACE_NONE, FILES_PLACE_HOME, FILES_PLACE_DOWNLOADS, FILES_PLACE_PICTURES, FILES_PLACE_ROOT };
#define FILES_NODE_ONLINE 1u
#define FILES_NODE_MAY_FILES 2u
#define FILES_NODE_LOADING 4u

struct files_node { p32 name, root_path, device_major, device_minor; p16 length, depth, has; p8 kind, place, machine, flags; b8 open, read; p64 inode; };
struct files_tree { struct files_node address_to node; p8 address_to names; p32 count, names_used; p64 drives_hash; address_any mapped; positive mapped_size; };
struct files_drive { p8 label[FILES_NAME], path[FILES_PATH], device[32], root[256]; };

static inline p32 files_tree_store(struct files_tree address_to t, const p8 address_to s, positive len)
{
        p32 at = t->names_used;
        return memory_copy(t->names + at, s, len + 1), t->names_used += (p32)(len + 1), at;
}

static b32 files_tree_insert_root(struct files_tree address_to t, p32 at, const p8 address_to label, const p8 address_to path, p8 kind, p8 place, p16 depth, p8 machine, b32 stat_local)
{
        positive llen = string_length((string_address)label), plen = path ? string_length((string_address)path) : 0;
        struct files_node address_to n;
        file_facts facts;
        if (!t->node || t->count >= FILES_TREE_MOST || at > t->count || !llen || (path && (!plen || path[0] != '/')) || t->names_used + llen + plen + 2 > FILES_TREE_NAMES)
                return false;
        if (at < t->count)
                memory_copy(&t->node[at + 1], &t->node[at], (positive)(t->count - at) * sizeof(struct files_node));
        n = &t->node[at], t->count++;
        memory_fill(n, 0, sizeof(*n));
        n->name = files_tree_store(t, label, llen), n->length = (p16)llen;
        n->root_path = path ? files_tree_store(t, path, plen) : 0;
        n->kind = kind, n->place = place, n->depth = depth, n->machine = machine;
        if (stat_local && path && file_look_code(AT_FDCWD, (string_address)path, 0, &facts) >= 0)
                n->inode = facts.inode, n->device_major = facts.device_major, n->device_minor = facts.device_minor;
        return true;
}

static b32 files_tree_add_root(struct files_tree address_to t, const p8 address_to label, const p8 address_to path, p8 kind, p8 place)
{
        return files_tree_insert_root(t, t->count, label, path, kind, place, 1, 0, true);
}

static b32 files_tree_add_machine(struct files_tree address_to t, const p8 address_to label, p8 machine, p8 flags, b32 open)
{
        p32 at = t->count;
        if (!files_tree_insert_root(t, at, label, null, FILES_NODE_MACHINE, FILES_PLACE_NONE, 0, machine, false))
                return false;
        t->node[at].flags = flags, t->node[at].open = (b8)open, t->node[at].read = (b8)open;
        return true;
}

static b32 files_tree_begin(struct files_tree address_to t)
{
        address_any mapped = files_anon_map(FILES_TREE_BYTES, null);
        memory_fill(t, 0, sizeof(*t));
        if (!mapped)
                return false;
        t->mapped = mapped, t->mapped_size = FILES_TREE_BYTES, t->node = (struct files_node address_to)mapped;
        t->names = (p8 address_to)mapped + (positive)FILES_TREE_MOST * sizeof(struct files_node);
        if (!files_tree_add_machine(t, (const p8 address_to) "This Machine", 0, FILES_NODE_ONLINE | FILES_NODE_MAY_FILES, true))
                return false;
        if (!files_tree_add_root(t, (const p8 address_to) "/", (const p8 address_to) "/", FILES_NODE_DRIVE, FILES_PLACE_ROOT))
                return false;
        t->node[0].has = 1;
        return true;
}


static inline fn files_tree_end(struct files_tree address_to t) { files_unmap(t->mapped, t->mapped_size), memory_fill(t, 0, sizeof(*t)); }

static inline b32 files_path_under(const p8 address_to path, const char address_to prefix)
{
        positive n = string_length((string_address)prefix);
        return !memory_compare(path, prefix, n) && (path[n] == 0 || path[n] == '/');
}

static b32 files_in_strtab(const p8 address_to s, const char address_to const address_to tab, positive n, int mode)
{
        for (positive k = 0; k < n; k++)
                if (mode == 0 ? string_equals((string_address)s, (string_address)tab[k])
                    : mode == 1 ? files_path_under(s, tab[k])
                                : !string_compare_max((string_address)s, (string_address)tab[k], string_length((string_address)tab[k])))
                        return true;
        return false;
}

static positive files_mount_unescape(const p8 address_to from, positive length, p8 address_to into, positive room)
{
        positive used = 0;
        for (positive at = 0; at < length && used + 1 < room; at++)
                if (from[at] == '\\' && at + 3 < length && from[at + 1] >= '0' && from[at + 1] <= '3' &&
                    from[at + 2] >= '0' && from[at + 2] <= '7' && from[at + 3] >= '0' && from[at + 3] <= '7')
                        into[used++] = (p8)(((from[at + 1] - '0') << 6) | ((from[at + 2] - '0') << 3) | (from[at + 3] - '0')), at += 3;
                else
                        into[used++] = from[at];
        return into[used] = end, used;
}

static int files_drives_parse(const p8 address_to text, positive length, struct files_drive address_to out, int most, p64 address_to out_hash)
{
        static const char address_to const skip_fs[] = {
            "proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "cgroup", "cgroup2", "pstore", "bpf", "tracefs", "debugfs",
            "securityfs", "configfs", "fusectl", "mqueue", "hugetlbfs", "autofs", "rpc_pipefs", "efivarfs", "ramfs",
            "sockfs", "pipefs", "nsfs", "overlay", "squashfs", "binfmt_misc", "fuse.portal", "fuse.gvfsd-fuse",
        };
        static const char address_to const skip_tgt[] = {
            "/proc", "/sys", "/dev", "/boot", "/snap", "/run/lock", "/run/shm", "/run/user", "/var/lib/docker", "/var/lib/containers",
        };
        static const char address_to const skip_dev[] = {"/dev/loop", "/dev/zram", "/dev/ram"};
        static const char address_to const mnt_roots[] = {"/mnt/", "/media/", "/run/media/"};
        int count = 0;
        p64 hash = 14695981039346656037ull;
        for (positive at = 0; at < length && count < most;)
        {
                const p8 address_to nl = (const p8 address_to)memory_first_of(text + at, '\n', length - at);
                positive line_end = nl ? (positive)(nl - text) : length, tlen;
                const p8 address_to tok[16];
                positive tok_len[16];
                int ntok = 0, sep = -1;
                p8 dev[32], root[256], target[FILES_PATH], fs[64], src[256];
                b32 dup = false, block, user_mnt;
                for (positive p = at; p < line_end && ntok < 16;)
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
                at = nl ? line_end + 1 : length;
                if (sep < 6 || sep + 2 >= ntok)
                        continue;
                {
                        struct { int idx; p8 address_to dst; positive cap; } f[5] = {
                            {2, dev, sizeof(dev)}, {3, root, sizeof(root)}, {4, target, sizeof(target)},
                            {sep + 1, fs, sizeof(fs)}, {sep + 2, src, sizeof(src)},
                        };
                        for (int k = 0; k < 5; k++)
                                files_mount_unescape(tok[f[k].idx], tok_len[f[k].idx], f[k].dst, f[k].cap);
                }
                block = string_has_prefix(src, "/dev/") && !files_in_strtab(src, skip_dev, 3, 2);
                user_mnt = files_in_strtab(target, mnt_roots, 3, 2);
                if (target[0] != '/' || string_equals((string_address)target, (string_address) "/") ||
                    files_in_strtab(target, skip_tgt, sizeof(skip_tgt) / sizeof(skip_tgt[0]), 1) ||
                    files_in_strtab(fs, skip_fs, sizeof(skip_fs) / sizeof(skip_fs[0]), 0) || (!block && !user_mnt))
                        continue;
                for (int k = 0; k < count && !dup; k++)
                        dup = string_equals((string_address)out[k].path, (string_address)target) ||
                              (string_equals((string_address)out[k].device, (string_address)dev) && string_equals((string_address)out[k].root, (string_address)root));
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
                hash = (hash ^ 0xffu) * 1099511628211ull, count++;
        }
        if (out_hash)
                *out_hash = hash;
        return count;
}

static p32 files_tree_after(const struct files_tree address_to t, p32 at);
static fn files_tree_compact(struct files_tree address_to t);

static b32 files_tree_load(struct files_tree address_to t, const p8 address_to home, const p8 address_to mountinfo, positive len, b32 force)
{
        static const struct { const char address_to name; p8 place; } places[] = {{"Downloads", FILES_PLACE_DOWNLOADS}, {"Pictures", FILES_PLACE_PICTURES}};
        struct files_drive address_to drives = mountinfo && len ? (struct files_drive address_to)files_anon_map(sizeof(struct files_drive) * FILES_DRIVES_MOST, null) : null;
        p64 hash = 0;
        int n = drives ? files_drives_parse(mountinfo, len, drives, FILES_DRIVES_MOST, &hash) : 0;
        b32 changed = force || hash != t->drives_hash;
        if (changed)
        {
                p8 sub[FILES_PATH];
                p32 after, ins = 1, added = 0;
                t->drives_hash = hash;
                if (!t->count || t->node[0].kind != FILES_NODE_MACHINE || t->node[0].machine != 0)
                {
                        t->count = t->names_used = 0;
                        files_tree_add_machine(t, (const p8 address_to) "This Machine", 0, FILES_NODE_ONLINE | FILES_NODE_MAY_FILES, true);
                }
                else
                {
                        after = files_tree_after(t, 0);
                        if (after > 1)
                        {
                                memory_copy(&t->node[1], &t->node[after], (positive)(t->count - after) * sizeof(struct files_node));
                                t->count -= after - 1;
                                files_tree_compact(t);
                        }
                }
                if (t->node[0].open)
                {
                        if (home && home[0] == '/')
                        {
                                if (files_tree_insert_root(t, ins, (const p8 address_to) "Home", home, FILES_NODE_PLACE, FILES_PLACE_HOME, 1, 0, true))
                                        ins++, added++;
                                for (int k = 0; k < 2; k++)
                                        if (files_path_join(sub, sizeof(sub), home, (const p8 address_to)places[k].name) &&
                                            files_tree_insert_root(t, ins, (const p8 address_to)places[k].name, sub, FILES_NODE_PLACE, places[k].place, 1, 0, true))
                                                ins++, added++;
                        }
                        if (files_tree_insert_root(t, ins, (const p8 address_to) "/", (const p8 address_to) "/", FILES_NODE_DRIVE, FILES_PLACE_ROOT, 1, 0, true))
                                ins++, added++;
                        for (int k = 0; k < n; k++)
                                if (files_tree_insert_root(t, ins, drives[k].label, drives[k].path, FILES_NODE_DRIVE, FILES_PLACE_NONE, 1, 0, true))
                                        ins++, added++;
                        t->node[0].has = (p16)added, t->node[0].read = true;
                }
        }
        return files_unmap(drives, sizeof(struct files_drive) * FILES_DRIVES_MOST), changed;
}

static b32 files_tree_set_machine_children(struct files_tree address_to t, p32 at, const p8 address_to home, p8 home_flags, const p8 address_to mountinfo, positive mi_len)
{
        struct files_drive address_to drives = mountinfo && mi_len ? (struct files_drive address_to)files_anon_map(sizeof(struct files_drive) * FILES_DRIVES_MOST, null) : null;
        int n = drives ? files_drives_parse(mountinfo, mi_len, drives, FILES_DRIVES_MOST, null) : 0;
        p32 after, ins, added = 0;
        p8 machine;
        p8 sub[FILES_PATH];
        if (at >= t->count || t->node[at].kind != FILES_NODE_MACHINE)
                return files_unmap(drives, sizeof(struct files_drive) * FILES_DRIVES_MOST), false;
        machine = t->node[at].machine;
        after = files_tree_after(t, at);
        if (after > at + 1)
        {
                memory_copy(&t->node[at + 1], &t->node[after], (positive)(t->count - after) * sizeof(struct files_node));
                t->count -= after - at - 1;
                files_tree_compact(t);
        }
        ins = at + 1;
        if (home && home[0] == '/')
        {
                if (files_tree_insert_root(t, ins, (const p8 address_to) "Home", home, FILES_NODE_PLACE, FILES_PLACE_HOME, 1, machine, false))
                        ins++, added++;
                if ((home_flags & 1) && files_path_join(sub, sizeof(sub), home, (const p8 address_to) "Downloads") &&
                    files_tree_insert_root(t, ins, (const p8 address_to) "Downloads", sub, FILES_NODE_PLACE, FILES_PLACE_DOWNLOADS, 1, machine, false))
                        ins++, added++;
                if ((home_flags & 2) && files_path_join(sub, sizeof(sub), home, (const p8 address_to) "Pictures") &&
                    files_tree_insert_root(t, ins, (const p8 address_to) "Pictures", sub, FILES_NODE_PLACE, FILES_PLACE_PICTURES, 1, machine, false))
                        ins++, added++;
        }
        if (files_tree_insert_root(t, ins, (const p8 address_to) "/", (const p8 address_to) "/", FILES_NODE_DRIVE, FILES_PLACE_ROOT, 1, machine, false))
                ins++, added++;
        for (int k = 0; k < n; k++)
                if (files_tree_insert_root(t, ins, drives[k].label, drives[k].path, FILES_NODE_DRIVE, FILES_PLACE_NONE, 1, machine, false))
                        ins++, added++;
        t->node[at].open = true, t->node[at].read = true, t->node[at].has = (p16)added, t->node[at].flags &= (p8)~FILES_NODE_LOADING;
        return files_unmap(drives, sizeof(struct files_drive) * FILES_DRIVES_MOST), true;
}

static inline fn files_tree_populate(struct files_tree address_to t, const p8 address_to home, const p8 address_to mi, positive len) { (void)files_tree_load(t, home, mi, len, true); }

static b32 files_tree_sync_drives(struct files_tree address_to t, const p8 address_to home, b32 force)
{
        p8 address_to buf = (p8 address_to)files_anon_map(65536, null);
        bipolar got;
        b32 changed;
        if (!buf)
                return false;
        got = file_slurp_once_at(AT_FDCWD, (string_address) "/proc/self/mountinfo", buf, 65535);
        changed = files_tree_load(t, home, got > 0 ? buf : null, got > 0 ? (positive)got : 0, force);
        return files_unmap(buf, 65536), changed;
}

static inline fn files_tree_populate_live(struct files_tree address_to t, const p8 address_to home) { (void)files_tree_sync_drives(t, home, true); }
static inline const p8 address_to files_tree_name(const struct files_tree address_to t, p32 at) { return t->names + t->node[at].name; }

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
        positive used, rlen;
        const p8 address_to rpath;
        if (at >= t->count || t->node[at].kind == FILES_NODE_MACHINE)
                return false;
        for (p32 here = at; here != FILES_NO && here < t->count && links < 256; here = files_tree_parent(t, here))
        {
                if (t->node[here].kind == FILES_NODE_MACHINE)
                        break;
                chain[links++] = here;
                if (t->node[here].depth <= 1)
                        break;
        }
        if (!links)
                return false;
        rpath = t->names + t->node[chain[links - 1]].root_path, rlen = string_length((string_address)rpath);
        if (!rlen || rlen >= FILES_PATH)
                return false;
        memory_copy(into, rpath, rlen), used = rlen;
        for (int k = links - 2; k >= 0; k--)
        {
                const struct files_node address_to n = &t->node[chain[k]];
                if (used + n->length + 2 >= FILES_PATH)
                        return false;
                if (used > 1)
                        into[used++] = '/';
                memory_copy(into + used, t->names + n->name, n->length), used += n->length;
        }
        return into[used] = end, true;
}

static int files_tree_name_order(address_any l, address_any r, address_any ctx) { return files_name_order((const p8 address_to)ctx + *(const p32 address_to)l, (const p8 address_to)ctx + *(const p32 address_to)r); }

struct files_tree_open_scratch { p8 path[FILES_PATH], found[FILES_TREE_CHILDREN * 64]; p32 offsets[FILES_TREE_CHILDREN]; };

static b32 files_tree_splice_folders(struct files_tree address_to t, p32 at, p8 address_to found, p32 address_to offsets, p32 count, positive used)
{
        p32 after, depth;
        p8 machine;
        if (at >= t->count)
                return false;
        if (count > 1)
                qsort_r(offsets, count, sizeof(p32), files_tree_name_order, found);
        after = at + 1, depth = t->node[at].depth + 1u, machine = t->node[at].machine;
        t->node[at].read = true, t->node[at].has = (p16)count, t->node[at].flags &= (p8)~FILES_NODE_LOADING;
        if (t->count + count > FILES_TREE_MOST || t->names_used + used > FILES_TREE_NAMES)
                return false;
        t->node[at].open = true;
        if (count)
                memory_copy(&t->node[after + count], &t->node[after], (positive)(t->count - after) * sizeof(struct files_node));
        for (p32 k = 0; k < count; k++)
        {
                const p8 address_to name = found + offsets[k];
                positive length = string_length((string_address)name);
                struct files_node address_to n = &t->node[after + k];
                memory_fill(n, 0, sizeof(*n));
                n->name = files_tree_store(t, name, length), n->length = (p16)length, n->depth = (p16)depth, n->machine = machine;
        }
        return t->count += count, true;
}

static b32 files_tree_open(struct files_tree address_to t, p32 at)
{
        struct files_tree_open_scratch address_to s;
        file_walk walk = {.handle = -1};
        struct linux_dirent64 address_to record;
        file_facts folder;
        p32 count = 0, scanned = 0, after;
        positive used = 0;
        b32 ok = false;
        if (at >= t->count || t->node[at].open)
                return at < t->count;
        if (t->node[at].kind == FILES_NODE_MACHINE || t->node[at].machine != 0)
                return false;
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
                memory_copy(s->found + used, record->d_name, length + 1), used += length + 1;
        }
        after = at + 1;
        if (files_tree_splice_folders(t, at, s->found, s->offsets, count, used))
        {
                for (p32 k = 0; k < count; k++)
                {
                        struct files_node address_to n = &t->node[after + k];
                        file_facts facts;
                        if (files_look_at(walk.handle, t->names + n->name, &facts) >= 0)
                                n->inode = facts.inode, n->device_major = facts.device_major, n->device_minor = facts.device_minor;
                }
                ok = true;
        }
done:
        if (walk.handle >= 0)
                file_walk_close(&walk);
        return files_unmap(s, sizeof(*s)), ok;
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
                memory_copy(fresh + used, t->names + n->name, nlen), n->name = used, used += (p32)nlen;
                if (n->depth == 1 && n->kind != FILES_NODE_MACHINE)
                {
                        positive rlen = string_length((string_address)(t->names + n->root_path)) + 1;
                        memory_copy(fresh + used, t->names + n->root_path, rlen), n->root_path = used, used += (p32)rlen;
                }
        }
        memory_copy(t->names, fresh, used), t->names_used = used, files_unmap(fresh, FILES_TREE_NAMES);
}

static fn files_tree_shut(struct files_tree address_to t, p32 at)
{
        p32 after;
        if (at >= t->count || !t->node[at].open)
                return;
        after = files_tree_after(t, at);
        memory_copy(&t->node[at + 1], &t->node[after], (positive)(t->count - after) * sizeof(struct files_node));
        t->count -= after - at - 1, t->node[at].open = t->node[at].read = false;
        if (after > at + 1)
                files_tree_compact(t);
}

static p32 files_tree_find_in(const struct files_tree address_to t, p8 machine, const p8 address_to path)
{
        p8 made[FILES_PATH];
        p32 found = FILES_NO;
        for (p32 at = 0; at < t->count; at++)
                if (t->node[at].machine == machine && files_tree_path(t, at, made) && string_equals((string_address)made, (string_address)path))
                {
                        if (t->node[at].depth == 1)
                                return at;
                        if (found == FILES_NO)
                                found = at;
                }
        return found;
}


static inline p32 files_tree_find(const struct files_tree address_to t, const p8 address_to path) { return files_tree_find_in(t, 0, path); }

static p32 files_tree_reveal_in(struct files_tree address_to t, p8 machine, const p8 address_to path)
{
        positive length = string_length((string_address)path), best_len = 0, start;
        p32 at = FILES_NO;
        if (!t->count || path[0] != '/')
                return FILES_NO;
        for (p32 k = 0; k < t->count; k++)
                if (t->node[k].machine == machine && t->node[k].depth == 1 && t->node[k].kind != FILES_NODE_MACHINE)
                {
                        const p8 address_to rpath = t->names + t->node[k].root_path;
                        positive rlen = string_length((string_address)rpath);
                        if (rlen && rlen <= length && (rlen == 1 ? rpath[0] == '/' : files_path_under(path, (const char address_to)rpath)) && rlen > best_len)
                                best_len = rlen, at = k;
                }
        if (at == FILES_NO)
                return FILES_NO;
        for (start = best_len == 1 ? 1 : path[best_len] == '/' ? best_len + 1 : length; start < length;)
        {
                positive stop = start;
                p32 next = FILES_NO;
                while (stop < length && path[stop] != '/')
                        stop++;
                if (!t->node[at].open)
                {
                        if (machine != 0)
                                return at;
                        files_tree_open(t, at);
                }
                for (p32 k = at + 1; k < t->count && t->node[k].depth > t->node[at].depth; k = files_tree_after(t, k))
                        if (t->node[k].length == stop - start && !memory_compare(t->names + t->node[k].name, path + start, stop - start))
                        {
                                next = k;
                                break;
                        }
                if (next == FILES_NO)
                        return machine != 0 ? at : FILES_NO;
                at = next, start = stop + 1;
        }
        return at;
}

static inline p32 files_tree_reveal(struct files_tree address_to t, const p8 address_to path) { return files_tree_reveal_in(t, 0, path); }

/* ---- stepped/threaded copy, move, and size-measurement job ---- */

#define FILES_MOVE_MOST 4096
#define FILES_MOVE_PIECE (8u << 20)
#define FILES_MOVE_BUFFER (1u << 20)
#define FILES_MOVE_INSIDE 1000
#define FILES_MOVE_COPY 1
#define FILES_MOVE_MOVE 2
#define FILES_MOVE_MEASURE 3
#define FILES_MOVE_PATHS ((positive)FILES_MOVE_MOST * FILES_PATH / 4)
#define FILES_MOVE_BYTES (FILES_MOVE_PATHS + (positive)FILES_MOVE_MOST * (sizeof(p32) + 1 + FILES_NAME))
enum { FILES_MOVE_RENAME, FILES_MOVE_COUNT, FILES_MOVE_COPYING, FILES_MOVE_OVER };

struct files_move
{
        int how, state, phase, depth;
        bipolar error, into, from, made[FILES_JOB_DEEP], in, out, out_folder, src_dir;
        b32 cancel, ranged, begun;
        p64 bytes_total, bytes_done, left;
        p32 items_total, items_done, skipped, renamed, count, next, address_to offsets;
        p8 at[FILES_NAME], name[FILES_NAME], named[FILES_NAME], out_name[FILES_NAME], src_name[FILES_NAME], inside[FILES_JOB_DEEP][FILES_NAME];
        p8 address_to paths, address_to copied, address_to landed, address_to buffer;
        file_facts into_facts, facts, made_facts[FILES_JOB_DEEP], piece_facts;
        file_walk walk[FILES_JOB_DEEP];
        thread address_to worker;
};

static inline p8 address_to files_move_landed(const struct files_move address_to j, p32 w) { return j->landed + (positive)w * FILES_NAME; }
static inline const p8 address_to files_move_path(const struct files_move address_to j, p32 w) { return j->paths + j->offsets[w]; }
static inline b32 files_move_running(const struct files_move address_to j) { return __atomic_load_n(&j->state, __ATOMIC_ACQUIRE) == FILES_JOB_RUNNING; }
static inline fn files_move_cancel(struct files_move address_to j) { __atomic_store_n(&j->cancel, 1, __ATOMIC_RELEASE); }
static inline b32 files_move_fail(struct files_move address_to j, bipolar e) { return j->error = e, __atomic_store_n(&j->state, FILES_JOB_FAILED, __ATOMIC_RELEASE), false; }

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
        job->copied = (p8 address_to)(job->offsets + FILES_MOVE_MOST), job->landed = job->copied + FILES_MOVE_MOST;
        for (p32 k = 0; k < count && job->count < FILES_MOVE_MOST; k++)
        {
                positive len = string_length((string_address)paths);
                if (len > 1 && paths[0] == '/' && len < FILES_PATH && used + len + 1 <= FILES_MOVE_PATHS)
                        job->offsets[job->count++] = (p32)used, memory_copy(job->paths + used, paths, len + 1), used += len + 1;
                paths += len + 1;
        }
        return job->state = job->count ? FILES_JOB_RUNNING : FILES_JOB_DONE, job->count != 0;
}

static const p8 address_to files_base_name(const p8 address_to path)
{
        const p8 address_to base = path;
        for (const p8 address_to c = path; *c; c++)
                if (*c == '/' && c[1])
                        base = c + 1;
        return base;
}

static b32 files_move_take(struct files_move address_to job, p32 which)
{
        const p8 address_to path = files_move_path(job, which);
        positive length = string_length((string_address)path);
        p8 folder[FILES_PATH];
        bipolar looked;
        files_close(&job->from);
        if (!length || path[length - 1] == '/' || string_length((string_address)files_base_name(path)) >= FILES_NAME || !files_path_parent(path, folder, job->name))
                return files_move_fail(job, -ERROR_INVALID);
        string_copy_bounded(job->at, (string_address)job->name, FILES_NAME);
        job->from = files_open_dir(AT_FDCWD, folder, 0);
        looked = job->from < 0 ? job->from : files_look_at(job->from, job->name, &job->facts);
        return looked < 0 ? files_move_fail(job, looked) : true;
}

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
                system_close(at), at = next;
        }
        return files_close(&at), inside;
}

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
                memory_copy(free, name, dot), memory_copy(free + dot, ".copy", 5);
                memory_copy(free + dot + 5, digits, count), memory_copy(free + dot + 5 + count, name + dot, length - dot + 1);
                if (files_look_at(into, free, &there) == -ERROR_NO_ENTRY)
                        return *numbered = true, true;
        }
        return false;
}

// A source entry a move has copied: removed only if it is still the very entry that was copied, unchanged in size and time.
static b32 files_move_drop_source(struct files_move address_to job, bipolar dir, const p8 address_to name, const file_facts address_to was)
{
        file_facts now;
        bipolar gone = files_look_at(dir, name, &now);
        if (gone < 0)
                return files_move_fail(job, gone);
        if (now.size != was->size || now.modified.seconds != was->modified.seconds || now.modified.nanoseconds != was->modified.nanoseconds)
                return files_move_fail(job, -5);
        gone = file_remove_same(dir, (string_address)name, 0, &now);
        return gone < 0 ? files_move_fail(job, gone) : true;
}

static fn files_move_settle(bipolar made, const file_facts address_to facts)
{
        b64 times[4] = {facts->accessed.seconds, facts->accessed.nanoseconds, facts->modified.seconds, facts->modified.nanoseconds};
        system_call_2(syscall(fchmod), (positive)made, (positive)(facts->mode & 0777));
        system_call_4(syscall(utimensat), (positive)made, 0, (positive)times, 0);
}

static b32 files_move_one(struct files_move address_to job, bipolar source, const p8 address_to name, file_facts address_to facts, const p8 address_to made_name, b32 counting)
{
        p32 format = facts->mode & MODE_FORMAT;
        bipolar target = job->depth ? job->made[job->depth - 1] : job->into, made;
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
                        made = files_mkdir(target, made_name, 0700);
                        made = made < 0 ? made : files_open_dir(target, made_name, O_NOFOLLOW);
                        if (made < 0)
                                return file_walk_close(&job->walk[job->depth]), files_move_fail(job, made);
                        job->made[job->depth] = made;
                }
                return job->depth++, true;
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
                bipolar len = (bipolar)system_call_4(syscall(readlinkat), (positive)source, (positive)name, (positive)points, sizeof(points) - 1);
                if (len >= 0)
                        points[len] = end, len = (bipolar)system_call_3(syscall(symlinkat), (positive)points, (positive)target, (positive)made_name);
                if (len < 0)
                        return files_move_fail(job, len);
                if (job->how == FILES_MOVE_MOVE && !files_move_drop_source(job, source, name, facts))
                        return false;
                return __atomic_add_fetch(&job->items_done, 1, __ATOMIC_RELEASE), true;
        }
        if (format != MODE_FILE)
                return job->skipped++, __atomic_add_fetch(&job->items_done, 1, __ATOMIC_RELEASE), true;
        job->in = file_open_same(source, (string_address)name, facts, FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        job->out = job->in < 0 ? -1 : system_open_at_mode(target, (string_address)made_name, FILE_WRITE | FILE_EXCLUSIVE | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (job->in < 0 || job->out < 0)
        {
                bipolar why = job->in < 0 ? job->in : job->out;
                return job->out = -1, files_move_file_drop(job, true), files_move_fail(job, why);
        }
        job->out_folder = target, job->left = facts->size, job->ranged = true, job->piece_facts = *facts;
        job->src_dir = source, string_copy_bounded(job->src_name, (string_address)name, FILES_NAME);
        return string_copy_bounded(job->out_name, (string_address)made_name, FILES_NAME), true;
}

static b32 files_move_walk(struct files_move address_to job, b32 counting)
{
        struct linux_dirent64 address_to record;
        file_facts facts;
        bipolar looked;
        file_walk address_to walk;
        if (job->out >= 0)
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
                        moved += (positive)got, __atomic_add_fetch(&job->bytes_done, (p64)got, __ATOMIC_RELEASE);
                }
                job->left -= moved;
                if (got < 0 || (!moved && job->left))
                        return files_move_file_drop(job, false), files_move_fail(job, got < 0 ? got : -5);
                if (!job->left)
                {
                        files_move_settle(job->out, &job->piece_facts), files_move_file_drop(job, true);
                        if (job->how == FILES_MOVE_MOVE && !files_move_drop_source(job, job->src_dir, job->src_name, &job->piece_facts))
                                return false;
                        __atomic_add_fetch(&job->items_done, 1, __ATOMIC_RELEASE);
                }
                return job->out >= 0 || job->depth > 0;
        }
        if (!job->depth)
                return false;
        walk = &job->walk[job->depth - 1];
        while ((record = file_walk_next(walk)) && file_is_dot((string_address)record->d_name))
                ;
        if (!record)
        {
                if (walk->error < 0)
                        return files_move_fail(job, walk->error);
                job->depth--, file_walk_close(walk);
                if (job->made[job->depth] >= 0)
                        files_move_settle(job->made[job->depth], &job->made_facts[job->depth]), files_close(&job->made[job->depth]);
                if (!counting && job->how == FILES_MOVE_MOVE)
                {
                        // The source folder goes only if it is empty: an entry that was not copied (a special, or one added during the copy) stays.
                        bipolar parent = job->depth ? job->walk[job->depth - 1].handle : job->from;
                        file_facts now;
                        bipolar gone = files_look_at(parent, job->inside[job->depth], &now);
                        if (gone < 0 || !files_same_facts(&now, &job->made_facts[job->depth]))
                                return files_move_fail(job, gone < 0 ? gone : -5);
                        gone = (bipolar)system_call_3(syscall(unlinkat), (positive)parent, (positive)job->inside[job->depth], AT_REMOVEDIR);
                        if (gone == -39)
                                job->skipped++;
                        else if (gone < 0)
                                return files_move_fail(job, gone);
                }
                __atomic_add_fetch(counting ? &job->items_total : &job->items_done, 1, __ATOMIC_RELEASE);
                return job->depth > 0;
        }
        if ((looked = files_look_at(walk->handle, (const p8 address_to)record->d_name, &facts)) < 0)
                return string_copy_bounded(job->at, (string_address)record->d_name, FILES_NAME), files_move_fail(job, looked);
        return files_move_one(job, walk->handle, (const p8 address_to)record->d_name, &facts, (const p8 address_to)record->d_name, counting) &&
               (job->depth > 0 || job->out >= 0);
}

static b32 files_move_step(struct files_move address_to job)
{
        if (!files_move_running(job))
                return false;
        if (__atomic_load_n(&job->cancel, __ATOMIC_ACQUIRE))
                return files_move_file_drop(job, false), __atomic_store_n(&job->state, FILES_JOB_CANCELLED, __ATOMIC_RELEASE), false;
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
                        else if ((moved = files_rename_nr(job->from, job->name, job->into, job->named)) >= 0)
                        {
                                job->copied[job->next] = 2, job->renamed += numbered;
                                string_copy_bounded((string_address)files_move_landed(job, job->next), (string_address)job->named, FILES_NAME);
                                __atomic_add_fetch(&job->items_total, 1, __ATOMIC_RELEASE);
                                __atomic_add_fetch(&job->items_done, 1, __ATOMIC_RELEASE);
                        }
                        else if (moved != -ERROR_CROSS_DEVICE)
                                return files_move_fail(job, moved);
                }
                return job->next++, true;
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
                        else
                                job->copied[job->next++] = 2;
                        return true;
                }
                while (job->next < job->count && job->copied[job->next] != 1)
                        job->next++;
                if (job->next >= job->count)
                {
                        job->next = 0, job->phase = counting && job->how != FILES_MOVE_MEASURE ? FILES_MOVE_COPYING : FILES_MOVE_OVER;
                        if (job->phase == FILES_MOVE_OVER)
                                __atomic_store_n(&job->state, FILES_JOB_DONE, __ATOMIC_RELEASE);
                        return job->phase != FILES_MOVE_OVER;
                }
                if (!files_move_take(job, job->next))
                        return false;
                if (counting)
                        return files_move_one(job, job->from, job->name, &job->facts, job->name, true) ? (job->next += !job->depth, true) : false;
                if (!files_move_free_name(job->into, job->name, job->named, &numbered))
                        return files_move_fail(job, -ERROR_EXISTS);
                job->renamed += numbered;
                string_copy_bounded((string_address)files_move_landed(job, job->next), (string_address)job->named, FILES_NAME);
                if (!files_move_one(job, job->from, job->name, &job->facts, job->named, false))
                        return false;
                if (!job->depth && job->out < 0)
                        job->copied[job->next++] = 2;
                return true;
        }
        return false;
}

static fn files_move_run(address_any arg) { while (files_move_step((struct files_move address_to)arg)) ; }
static b32 files_move_start(struct files_move address_to job) { return job->state == FILES_JOB_RUNNING && (job->worker = thread_start(files_move_run, (address_any)job)) != null; }

static fn files_move_end(struct files_move address_to job)
{
        if (job->begun)
        {
                if (job->worker)
                        files_move_cancel(job), thread_join(job->worker);
                files_move_file_drop(job, false);
                while (job->depth > 0)
                        job->depth--, file_walk_close(&job->walk[job->depth]), files_close(&job->made[job->depth]);
                files_close(&job->from), files_close(&job->into);
                files_unmap(job->paths, FILES_MOVE_BYTES), files_unmap(job->buffer, FILES_MOVE_BUFFER);
        }
        memory_fill(job, 0, sizeof(*job));
        job->into = job->from = job->in = job->out = job->out_folder = -1;
}

/* ---- desktop trash bin ---- */

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
                        at[k] = 0, made = files_mkdir(AT_FDCWD, at, 0700), at[k] = was;
                        if (made < 0 && made != -ERROR_EXISTS)
                                return false;
                }
        return true;
}

static b32 files_bin_join(p8 address_to into, const p8 address_to bin, const char address_to part, const p8 address_to name, const char address_to suffix)
{
        const char address_to parts[5] = {(const char address_to)bin, part, name ? "/" : null, (const char address_to)name, suffix};
        into[0] = 0;
        for (int k = 0; k < 5; k++)
                if (parts[k])
                        string_append_bounded((string_address)into, (string_address)parts[k], FILES_PATH);
        return string_length((string_address)into) + 1 < FILES_PATH;
}

static inline positive files_uri_byte(p8 address_to into, p8 c)
{
        static const char hex[] = "0123456789ABCDEF";
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
                return into[0] = c, 1;
        return into[0] = '%', into[1] = (p8)hex[c >> 4], into[2] = (p8)hex[c & 15], 3;
}

static inline int files_hex_nibble(p8 c)
{
        if (c >= '0' && c <= '9')
                return c - '0';
        if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
        return -1;
}

static int files_bin_put(const p8 address_to bin, const p8 address_to path, const char address_to when, p8 address_to named)
{
        p8 files_dir[FILES_PATH], into[FILES_PATH], info[FILES_PATH], text[FILES_PATH * 3 + 96];
        positive used = 18;
        b32 numbered;
        bipolar folder, moved, out = -1;
        int res = FILES_DID_FAILED;
        if (!files_bin_join(files_dir, bin, "/files", null, null) || !files_bin_folders(files_dir) ||
            !files_bin_join(info, bin, "/info", null, null) || !files_bin_folders(info) ||
            (folder = files_open_dir(AT_FDCWD, files_dir, 0)) < 0)
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
        system_write_once((b32)out, text, used), system_close(out);
        files_bin_join(into, bin, "/files", named, null);
        if ((moved = files_rename_nr(AT_FDCWD, path, AT_FDCWD, into)) < 0)
                system_remove_at(AT_FDCWD, (string_address)info, 0), res = moved == -ERROR_CROSS_DEVICE ? FILES_DID_CROSS : FILES_DID_FAILED;
        else
                res = FILES_DID;
done:
        return system_close(folder), res;
}

static b32 files_bin_was(const p8 address_to bin, const p8 address_to name, p8 address_to path)
{
        p8 info[FILES_PATH], text[FILES_PATH * 3 + 128];
        bipolar got;
        positive at = 0, out = 0;
        if (!files_name_good(name) || !files_bin_join(info, bin, "/info", name, ".trashinfo") ||
            (got = file_slurp_regular_at(AT_FDCWD, (string_address)info, text, sizeof(text), O_NOFOLLOW)) <= 0)
                return false;
        text[got] = 0;
        for (; at + 5 < (positive)got; at++)
                if ((at == 0 || text[at - 1] == '\n') && !memory_compare(text + at, "Path=", 5))
                        break;
        if (at + 5 >= (positive)got)
                return false;
        for (at += 5; text[at] && text[at] != '\n' && text[at] != '\r' && out + 1 < FILES_PATH; at++)
        {
                p8 c = text[at];
                if (c == '%')
                {
                        int hi = text[at + 1] ? files_hex_nibble(text[at + 1]) : -1;
                        int lo = text[at + 1] && text[at + 2] ? files_hex_nibble(text[at + 2]) : -1;
                        if (hi < 0 || lo < 0)
                                return false;
                        c = (p8)(hi << 4 | lo), at += 2;
                }
                if (!c)
                        return false;
                path[out++] = c;
        }
        path[out] = 0;
        if (out <= 1 || path[0] != '/' || path[out - 1] == '/')
                return false;
        for (positive pos = 1; pos < out;)
        {
                positive stop = pos;
                while (stop < out && path[stop] != '/')
                        stop++;
                if (stop == pos || stop - pos >= FILES_NAME ||
                    (stop - pos == 1 && path[pos] == '.') ||
                    (stop - pos == 2 && path[pos] == '.' && path[pos + 1] == '.'))
                        return false;
                pos = stop + 1;
        }
        return true;
}

static int files_bin_restore(const p8 address_to bin, const p8 address_to name, p8 address_to back)
{
        p8 from[FILES_PATH], info[FILES_PATH], folder_path[FILES_PATH], free[FILES_NAME];
        b32 numbered;
        bipolar folder, moved;
        if (!files_name_good(name))
                return FILES_DID_BAD;
        if (!files_bin_was(bin, name, back) || !files_name_good(files_base_name(back)) ||
            !files_bin_join(from, bin, "/files", name, null) ||
            !files_bin_join(info, bin, "/info", name, ".trashinfo") || !files_path_parent(back, folder_path, null) ||
            !files_bin_folders(folder_path) || (folder = files_open_dir(AT_FDCWD, folder_path, O_NOFOLLOW)) < 0)
                return FILES_DID_FAILED;
        moved = files_move_free_name(folder, files_base_name(back), free, &numbered) ? files_rename_nr(AT_FDCWD, from, folder, free) : -1;
        system_close(folder);
        if (moved < 0)
                return FILES_DID_FAILED;
        return system_remove_at(AT_FDCWD, (string_address)info, 0), files_path_join(back, FILES_PATH, folder_path, free), FILES_DID;
}

/* ---- git index & ignore marks ---- */

enum { FILES_GIT_NONE, FILES_GIT_TRACKED, FILES_GIT_UNTRACKED, FILES_GIT_IGNORED };
#define FILES_GIT_INDEX_MOST (64u << 20)
#define FILES_GIT_NAMES 131072u
#define FILES_GIT_LITERALS 4096u
#define FILES_GIT_GLOBS 256
#define FILES_GIT_BUDGET 4000000ul
#define FILES_GIT_IGNORE_MOST 16384

struct files_git_pattern { const p8 address_to text; p16 length; p8 only_folders, rooted; };
struct files_git_rules { struct files_git_pattern literal[FILES_GIT_LITERALS], glob[FILES_GIT_GLOBS]; int globs; };
struct files_git_scratch { struct files_git_rules rules; p32 table[FILES_GIT_NAMES]; p8 root[FILES_PATH], ignore[2][FILES_GIT_IGNORE_MOST]; };

static inline p32 files_git_hash(const p8 address_to text, positive length) { return hash_fnv1a(2166136261u, text, length); }
static inline p32 files_be32(const p8 address_to p) { return (p32)p[0] << 24 | (p32)p[1] << 16 | (p32)p[2] << 8 | p[3]; }

static b32 files_git_glob(const p8 address_to glob, positive gl, const p8 address_to text, positive tl, positive address_to budget)
{
        positive g = 0, t = 0, star = (positive)-1, mark = 0;
        while (t < tl)
        {
                if (!*budget)
                        return false;
                --*budget;
                if (g < gl && (glob[g] == '?' || glob[g] == text[t]))
                        g++, t++;
                else if (g < gl && glob[g] == '*')
                        star = g++, mark = t;
                else if (star != (positive)-1)
                        g = star + 1, t = ++mark;
                else
                        return false;
        }
        while (g < gl && glob[g] == '*')
                g++;
        return g == gl;
}

static struct files_git_pattern address_to files_git_lit_slot(struct files_git_rules address_to rules, const p8 address_to name, positive length)
{
        p32 h = files_git_hash(name, length);
        for (p32 probe = 0; probe < FILES_GIT_LITERALS; probe++, h++)
        {
                struct files_git_pattern address_to slot = &rules->literal[h & (FILES_GIT_LITERALS - 1)];
                if (!slot->length || (slot->length == length && !memory_compare(slot->text, name, length)))
                        return slot;
        }
        return null;
}

static fn files_git_rules_add(struct files_git_rules address_to rules, const p8 address_to text, positive size, b32 rooted)
{
        for (positive at = 0; at < size;)
        {
                positive start = at, stop;
                b32 only_folders = false, anchored = false, inside = false, wild = false;
                struct files_git_pattern pat, address_to slot;
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
                pat = (struct files_git_pattern){text + start, (p16)(stop - start), (p8)only_folders, (p8)(rooted || anchored)};
                if (wild && rules->globs < FILES_GIT_GLOBS)
                        rules->glob[rules->globs++] = pat;
                else if (!wild && (slot = files_git_lit_slot(rules, pat.text, pat.length)))
                {
                        if (!slot->length)
                                *slot = pat;
                        else
                                slot->only_folders &= pat.only_folders;
                }
        }
}

static b32 files_git_ignored(struct files_git_rules address_to rules, const p8 address_to name, positive length, b32 folder, positive address_to budget)
{
        const struct files_git_pattern address_to slot = files_git_lit_slot(rules, name, length);
        if (slot && slot->length && (folder || !slot->only_folders))
                return true;
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
        rel = rel > 1 ? rel + 1 : 1, at_root = rel <= 1 || length + 1 == rel;
        string_append_bounded((string_address)at_path, (string_address) "/index", FILES_PATH);
        if ((handle = system_open_at(AT_FDCWD, (string_address)at_path, FILE_READ | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)) < 0)
                goto done;
        if (files_look_fd(handle, &facts) != 0 || (facts.mode & MODE_FORMAT) != MODE_FILE)
        {
                system_close(handle);
                goto done;
        }
        mapped_size = (positive)facts.size;
        mapped = mapped_size >= 12 && mapped_size <= FILES_GIT_INDEX_MOST ? (p8 address_to)files_anon_map(mapped_size, null) : null;
        for (bipolar part; mapped && got < mapped_size && (part = (bipolar)system_read_once(handle, mapped + got, mapped_size - got)) > 0;)
                got += (positive)part;
        system_close(handle);
        if (!mapped || (size = got) < 12 || memory_compare(mapped, "DIRC", 4))
                goto done;
        version = files_be32(mapped + 4), count = files_be32(mapped + 8);
        if (version != 2 && version != 3)
                goto done;
        memory_fill(s->table, 0xff, sizeof(s->table));
        for (p32 k = 0; k < m->shown.count; k++)
        {
                const struct files_entry address_to e = &m->shown.entry[k];
                p32 h = files_git_hash(m->shown.names + e->name, e->length);
                while (s->table[h & (FILES_GIT_NAMES - 1)] != FILES_NO)
                        h++;
                s->table[h & (FILES_GIT_NAMES - 1)] = k, state[k] = FILES_GIT_UNTRACKED;
        }
        for (positive at = 12, k = 0; k < count && at + 62 < size; k++)
        {
                p16 flags = (p16)(mapped[at + 60] << 8 | mapped[at + 61]);
                positive fixed = 62 + (version >= 3 && (flags & 0x4000) ? 2 : 0), name_at, end_at, first, stop;
                p32 h, found;
                if (at + fixed >= size)
                        break;
                name_at = at + fixed, end_at = name_at;
                while (end_at < size && mapped[end_at])
                        end_at++;
                at += (fixed + (end_at - name_at) + 8) & ~(positive)7;
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
                for (h = files_git_hash(mapped + first, stop - first); (found = s->table[h & (FILES_GIT_NAMES - 1)]) != FILES_NO; h++)
                {
                        const struct files_entry address_to e = &m->shown.entry[found];
                        if (e->length == stop - first && !memory_compare(m->shown.names + e->name, mapped + first, stop - first))
                        {
                                state[found] = FILES_GIT_TRACKED;
                                break;
                        }
                }
        }
        for (int which = 0; which < 2; which++)
        {
                bipolar read;
                files_path_join(at_path, FILES_PATH, which ? m->path : s->root, (const p8 address_to) ".gitignore");
                read = file_slurp_regular_at(AT_FDCWD, (string_address)at_path, s->ignore[which], sizeof(s->ignore[which]), O_NOFOLLOW);
                files_git_rules_add(&s->rules, s->ignore[which], read > 0 ? (positive)read : 0, which ? true : at_root);
                if (at_root)
                        break;
        }
        for (p32 k = 0; k < m->shown.count; k++)
        {
                const struct files_entry address_to e = &m->shown.entry[k];
                const p8 address_to name = m->shown.names + e->name;
                if (e->length == 4 && !memory_compare(name, ".git", 4))
                        state[k] = FILES_GIT_NONE;
                else if (state[k] == FILES_GIT_UNTRACKED && files_git_ignored(&s->rules, name, e->length, e->kind == FILES_KIND_DIRECTORY, &budget))
                        state[k] = FILES_GIT_IGNORED;
        }
        ok = true;
done:
        return files_unmap(mapped, mapped_size), files_unmap(s, sizeof(*s)), ok;
}

#endif // MOONWATER_FILES_MODEL
