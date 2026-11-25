/*
        The sound graph: what is wired to what in the sound server (service.c), and the person's wiring kept in one file.

        THE NODES

        A node is a source of sound (a program's playback streams, by the program's name and user; a microphone or line in
        of a card; another machine's microphone or what it plays, over Waterlink), a sink (an output of a card; the loopback
        microphone, which programs record what is wired into it from; another machine's speakers, over Waterlink), or a
        combiner, many in and one out: Main, which every program joins unless it is wired elsewhere and which goes to the
        output that plays, and as many more as the person makes, each offered to programs as a microphone too (a virtual
        cable: what is wired into it is what a program that records it hears). Any out may be wired to any in, a level and a
        mute on each wire; a wire that would close a loop of combiners is refused.

        THE FILE

        /root/audio.graph, a line a thing, its fields apart by a tab, read whole by the server when it moves:

            combiner NAME                    a combiner the person made (Main is always there and is not listed)
            wire FROM TO LEVEL [muted]       a wire: its two ends as keys, its level in percent (0 to 200)
            cut FROM TO                      a wire the server makes of its own (below) that the person took off

        A key is a node's kind and name: program:UID/NAME, capture:NAME, combiner:NAME, output:NAME, loopback:NAME,
        remote:NAME, and output:default, the output that plays whichever it is. The server's own wires, there unless cut:
        each program into Main, Main to the output that plays, a machine's sound playing here into Main, and Main and the
        default microphone to a machine that listens here. A line this file does not know is passed over. The old list of
        mirrors (/root/audio.mirror, a name and a level a line) is read once, as wires from Main, when there is no graph.

        The plan below is the file as tables; the UI that edits the wiring (Settings, the mixer) reads the file, changes the
        plan and writes it whole (audio_plan_format); the server only reads it, but when it takes the old mirrors in.

        THE LIVE GRAPH (with the mixing core before it: the server's)

        The nodes there are and the wires between them, rebuilt when a node comes or goes or the file moves, as flat tables:
        a column a field for the nodes and for the edges, the edges in the order of the node they leave (each node's are
        first[k] to first[k + 1]), and an order to mix in where every combiner comes after what feeds it. A block is mixed by
        walking that order, adding each node's block along its edges at their gains (audio_graph_spread): the shape of the
        graph is in the tables, not in a test a sample.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef AUDIO_GRAPH_INCLUDED
#define AUDIO_GRAPH_INCLUDED

#define AUDIO_GRAPH_API static __attribute__((unused)) // a program that includes the plan for its file uses only some of it
#define AUDIO_GRAPH_FILE "audio.graph"           // in the server's settings directory
#define AUDIO_GRAPH_PATH "/root/audio.graph"     // where the person's session has it
#define AUDIO_GRAPH_KEY 128                      // bytes of a node's key, "kind:name", its end with it
#define AUDIO_GRAPH_LEVEL_MOST 200               // a wire's level in percent at most
#define AUDIO_GRAPH_MAIN "Main"                  // the first combiner's name
#define AUDIO_GRAPH_DEFAULT_OUTPUT "output:default" // the output that plays, whichever it is

/* The kinds of node. */
#define AUDIO_NODE_PROGRAM 1       // a program's playback streams (its name and its user)
#define AUDIO_NODE_CAPTURE 2       // a microphone or line in of a card
#define AUDIO_NODE_COMBINER 3      // many in, one out: Main and the person's
#define AUDIO_NODE_OUTPUT 4        // an output of a card
#define AUDIO_NODE_LOOPBACK 5      // the loopback microphone: what is wired into it, recorded by programs
#define AUDIO_NODE_REMOTE_SOURCE 6 // sound arriving from another machine over Waterlink
#define AUDIO_NODE_REMOTE_OUTPUT 7 // sound going to another machine over Waterlink
#define AUDIO_NODE_KINDS 8

/* A node's flags. The first five are a remote device's, as Waterlink's side of the server says them (remote.c, struct audio_remote_row). */
#define AUDIO_NODE_DENIED 1u     // the machine is linked and has not allowed sound: allow is the line that allows it, run on that machine
#define AUDIO_NODE_AWAY 2u       // the machine is linked and no session is up now
#define AUDIO_NODE_LIVE 4u       // sound flows to or from it now
#define AUDIO_NODE_HEARD 8u      // the far side's: a machine that plays into this one or listens to it
#define AUDIO_NODE_MONITOR 16u   // what a machine plays, not its microphone
#define AUDIO_NODE_CLOCK 32u     // the output the mix runs on (the output that plays)
#define AUDIO_NODE_UNPLUGGED 64u // an output whose card says nothing is plugged in
#define AUDIO_NODE_PLAYING 128u  // a program with a stream that plays; a card that is open and played to
#define AUDIO_NODE_FAILED 256u   // a card that could not be opened (it is tried again)
#define AUDIO_NODE_MIC 512u      // a combiner offered to programs as a microphone

/* An edge's flags. */
#define AUDIO_EDGE_MUTED 1u   // wired, and carrying nothing
#define AUDIO_EDGE_DEFAULT 2u // the server's own wire (not in the file): taken off, it is cut
#define AUDIO_EDGE_PLAYING 4u // Main to the output that plays: its level is the master volume's, not its own
#define AUDIO_EDGE_LIVE 8u    // carrying sound now
#define AUDIO_EDGE_CUT 128u   // the plan's: a wire of the server's the person took off

/*
        A device of another machine, as Waterlink's half of the server keeps it (src/audio/remote.c fills a table of these and the
        graph reads it, audio_remote_rows): dev is remote.c's own number for it, way says which way the sound goes (a SOURCE is
        pulled from, an OUTPUT pushed to), flags are AUDIO_NODE_DENIED, _AWAY, _LIVE, _HEARD and _MONITOR.
*/
struct audio_remote_row
{
        long dev;
        char name[64], description[96], machine[32], allow[64];
        unsigned int way, flags, latency_us, lost;
};

// The bytes of a text before its end (the names here are short; a file may include this with no library of strings).
AUDIO_GRAPH_API long audio_graph_length(const char *text)
{
        long n = 0;

        while (text[n])
                n++;
        return n;
}

AUDIO_GRAPH_API int audio_node_outs(int kind)
{
        return kind == AUDIO_NODE_PROGRAM || kind == AUDIO_NODE_CAPTURE || kind == AUDIO_NODE_COMBINER || kind == AUDIO_NODE_REMOTE_SOURCE;
}

AUDIO_GRAPH_API int audio_node_ins(int kind)
{
        return kind == AUDIO_NODE_COMBINER || kind == AUDIO_NODE_OUTPUT || kind == AUDIO_NODE_LOOPBACK || kind == AUDIO_NODE_REMOTE_OUTPUT;
}

// The word of a kind in a key (a remote source and a remote output share one: their names tell them apart).
AUDIO_GRAPH_API const char *audio_node_word(int kind)
{
        static const char words[AUDIO_NODE_KINDS][10] = {"", "program", "capture", "combiner", "output", "loopback", "remote", "remote"};

        return kind > 0 && kind < AUDIO_NODE_KINDS ? words[kind] : "";
}

// A field of a line into to (room bytes, always ended): its bytes up to a tab or the line's end, a control byte a question mark, cut at
// a whole character to fit. Answers where the field ended.
AUDIO_GRAPH_API const char *audio_graph_field(char *to, long room, const char *at, const char *limit)
{
        long n = 0;

        while (at < limit && *at != '\t' && *at != '\n' && *at != '\r')
        {
                unsigned char c = (unsigned char)*at++;

                if (n < room - 1)
                        to[n++] = c < 0x20 || c == 0x7f ? '?' : (char)c;
        }
        // A character cut in its middle is cut whole: its continuation bytes, and the byte that began it.
        if (n == room - 1 && at < limit && ((unsigned char)*at & 0xc0) == 0x80)
        {
                while (n > 0 && ((unsigned char)to[n - 1] & 0xc0) == 0x80)
                        n--;
                if (n > 0 && ((unsigned char)to[n - 1] & 0x80))
                        n--;
        }
        if (room > 0)
                to[n] = 0;
        return at;
}

// A text copied into to (room bytes, always ended), cut as a field is.
AUDIO_GRAPH_API void audio_graph_copy(char *to, long room, const char *from)
{
        audio_graph_field(to, room, from, from + audio_graph_length(from));
}

AUDIO_GRAPH_API int audio_graph_same(const char *a, const char *b)
{
        long n = audio_graph_length(a);

        if (n != audio_graph_length(b))
                return 0;
        while (n-- > 0)
                if (a[n] != b[n])
                        return 0;
        return 1;
}

// A node's key: its kind's word, a colon, its name.
AUDIO_GRAPH_API long audio_node_key(char *key, long room, int kind, const char *name)
{
        const char *word = audio_node_word(kind);
        long n = audio_graph_length(word);

        if (room < n + 2)
                return room > 0 ? (key[0] = 0, 0) : 0;
        memory_copy(key, word, (positive)n);
        key[n++] = ':';
        audio_graph_copy(key + n, room - n, name);
        return audio_graph_length(key);
}

// Whether a key names a node of a kind and a name (a remote key either remote kind).
AUDIO_GRAPH_API int audio_node_is(const char *key, int kind, const char *name)
{
        const char *word = audio_node_word(kind);
        long n = audio_graph_length(word);

        for (long at = 0; at < n; at++)
                if (key[at] != word[at])
                        return 0;
        return key[n] == ':' && audio_graph_same(key + n + 1, name);
}

/*
        Whether b is reached from a along the edges: the nodes reached are marked (mark has a byte for each of nodes), each pass of the
        edges marking the ends of the edges that leave a marked node, until a pass marks nothing. A wire from x to y closes a loop when
        x is reached from y. The UI asks this of the page's edges before it draws a wire, the server of its own before it makes one.
*/
AUDIO_GRAPH_API int audio_graph_reaches(const unsigned int *from, const unsigned int *to, long edges, unsigned char *mark, long nodes, unsigned int a, unsigned int b)
{
        long changed = 1;

        if (a >= (unsigned long)nodes || b >= (unsigned long)nodes)
                return 0;
        for (long k = 0; k < nodes; k++)
                mark[k] = 0;
        mark[a] = 1;
        while (changed && !mark[b])
        {
                changed = 0;
                for (long e = 0; e < edges; e++)
                        if (from[e] < (unsigned long)nodes && to[e] < (unsigned long)nodes && mark[from[e]] && !mark[to[e]])
                                mark[to[e]] = 1, changed = 1;
        }
        return mark[b];
}

/* ---- the columns of a table, grown together ---- */

// The host's memory (the server's tables, or a UI's pages): grow(context, 0, 0, bytes) is a new zeroed block of bytes, or 0;
// grow(context, block, bytes, 0) lets a block of bytes go (and answers 0).
typedef void *(*audio_graph_grow_fn)(void *context, void *old, long old_bytes, long bytes);

struct audio_column
{
        void **at; // where the column's pointer is
        long size; // bytes of an element
};

// Every column of a table made room for want rows (doubling from what it has): 0 when there was no memory (the table is as it was).
AUDIO_GRAPH_API int audio_columns_fit(audio_graph_grow_fn grow, void *context, const struct audio_column *columns, int count, long *room, long want)
{
        long next = *room ? *room : 8;
        void *made[24];

        if (want <= *room)
                return 1;
        if (count > 24)
                return 0;
        while (next < want)
                next *= 2;
        for (int c = 0; c < count; c++)
                if (!(made[c] = grow(context, 0, 0, next * columns[c].size)))
                {
                        for (int k = 0; k < c; k++)
                                grow(context, made[k], next * columns[k].size, 0);
                        return 0;
                }
        for (int c = 0; c < count; c++)
        {
                if (*columns[c].at)
                {
                        memory_copy(made[c], *columns[c].at, (positive)(*room * columns[c].size));
                        grow(context, *columns[c].at, *room * columns[c].size, 0);
                }
                *columns[c].at = made[c];
        }
        *room = next;
        return 1;
}

/* ---- the plan: the file as tables ---- */

struct audio_plan
{
        audio_graph_grow_fn grow;
        void *context;
        long combiners, combiner_room;
        char (*combiner)[AUDIO_GRAPH_KEY];      // the person's combiners' names
        long wires, wire_room;
        char (*from)[AUDIO_GRAPH_KEY], (*to)[AUDIO_GRAPH_KEY];
        unsigned short *level;                  // percent
        unsigned char *flags;                   // AUDIO_EDGE_MUTED, AUDIO_EDGE_CUT
};

AUDIO_GRAPH_API int audio_plan_fit(struct audio_plan *plan, long combiners, long wires)
{
        const struct audio_column made[1] = {{(void **)&plan->combiner, AUDIO_GRAPH_KEY}};
        const struct audio_column wired[4] = {{(void **)&plan->from, AUDIO_GRAPH_KEY}, {(void **)&plan->to, AUDIO_GRAPH_KEY}, {(void **)&plan->level, 2}, {(void **)&plan->flags, 1}};

        return audio_columns_fit(plan->grow, plan->context, made, 1, &plan->combiner_room, combiners) &&
               audio_columns_fit(plan->grow, plan->context, wired, 4, &plan->wire_room, wires);
}

AUDIO_GRAPH_API void audio_plan_clear(struct audio_plan *plan)
{
        plan->combiners = plan->wires = 0;
}

// The plan's memory given back (a UI's plan that is done with).
AUDIO_GRAPH_API void audio_plan_free(struct audio_plan *plan)
{
        void **columns[5] = {(void **)&plan->combiner, (void **)&plan->from, (void **)&plan->to, (void **)&plan->level, (void **)&plan->flags};
        long sizes[5] = {plan->combiner_room * AUDIO_GRAPH_KEY, plan->wire_room * AUDIO_GRAPH_KEY, plan->wire_room * AUDIO_GRAPH_KEY, plan->wire_room * 2, plan->wire_room};

        for (int c = 0; c < 5; c++)
                if (*columns[c])
                        plan->grow(plan->context, *columns[c], sizes[c], 0), *columns[c] = 0;
        plan->combiners = plan->wires = plan->combiner_room = plan->wire_room = 0;
}

AUDIO_GRAPH_API long audio_plan_find(const struct audio_plan *plan, const char *from, const char *to)
{
        for (long w = 0; w < plan->wires; w++)
                if (audio_graph_same(plan->from[w], from) && audio_graph_same(plan->to[w], to))
                        return w;
        return -1;
}

// A line of the plan between two keys: a wire at a level, or a cut (flags AUDIO_EDGE_CUT), the pair's earlier line replaced. -1 with no memory.
AUDIO_GRAPH_API long audio_plan_set(struct audio_plan *plan, const char *from, const char *to, long level, unsigned int flags)
{
        long w = audio_plan_find(plan, from, to);

        if (!from[0] || !to[0])
                return -1;
        if (w < 0)
        {
                if (!audio_plan_fit(plan, plan->combiners, plan->wires + 1))
                        return -1;
                w = plan->wires++;
                audio_graph_copy(plan->from[w], AUDIO_GRAPH_KEY, from);
                audio_graph_copy(plan->to[w], AUDIO_GRAPH_KEY, to);
        }
        plan->level[w] = (unsigned short)(level < 0 ? 0 : level > AUDIO_GRAPH_LEVEL_MOST ? AUDIO_GRAPH_LEVEL_MOST : level);
        plan->flags[w] = (unsigned char)(flags & (AUDIO_EDGE_MUTED | AUDIO_EDGE_CUT));
        return w;
}

AUDIO_GRAPH_API void audio_plan_drop(struct audio_plan *plan, long w)
{
        if (w < 0 || w >= plan->wires)
                return;
        plan->wires--;
        for (long at = w; at < plan->wires; at++)
        {
                memory_copy(plan->from[at], plan->from[at + 1], AUDIO_GRAPH_KEY);
                memory_copy(plan->to[at], plan->to[at + 1], AUDIO_GRAPH_KEY);
                plan->level[at] = plan->level[at + 1], plan->flags[at] = plan->flags[at + 1];
        }
}

/*
        The person drew a wire between two keys at a level (mute 0 or 1): a wire of the server's own (was_default) at a whole and not
        muted is the server's again (its lines go); any other is a line of the file.
*/
AUDIO_GRAPH_API long audio_plan_wired(struct audio_plan *plan, const char *from, const char *to, long level, int mute, int was_default)
{
        if (was_default && level == 100 && !mute)
        {
                audio_plan_drop(plan, audio_plan_find(plan, from, to));
                return 0;
        }
        return audio_plan_set(plan, from, to, level, mute ? AUDIO_EDGE_MUTED : 0) < 0 ? -1 : 0;
}

// The person took a wire off: a wire of the server's own is cut (it is not made again), the person's own line goes.
AUDIO_GRAPH_API long audio_plan_unwired(struct audio_plan *plan, const char *from, const char *to, int was_default)
{
        if (was_default)
                return audio_plan_set(plan, from, to, 0, AUDIO_EDGE_CUT) < 0 ? -1 : 0;
        audio_plan_drop(plan, audio_plan_find(plan, from, to));
        return 0;
}

AUDIO_GRAPH_API long audio_plan_combiner_of(const struct audio_plan *plan, const char *name)
{
        for (long c = 0; c < plan->combiners; c++)
                if (audio_graph_same(plan->combiner[c], name))
                        return c;
        return -1;
}

// A combiner of the person's made: its name cleaned (no tab, no line, no control byte) and not Main's or another's. Its place, or -1.
AUDIO_GRAPH_API long audio_plan_combiner_add(struct audio_plan *plan, const char *name)
{
        char clean[AUDIO_GRAPH_KEY - 10];
        long c;

        audio_graph_copy(clean, sizeof(clean), name);
        if (!clean[0] || audio_graph_same(clean, AUDIO_GRAPH_MAIN) || audio_plan_combiner_of(plan, clean) >= 0)
                return -1;
        if (!audio_plan_fit(plan, plan->combiners + 1, plan->wires))
                return -1;
        c = plan->combiners++;
        audio_graph_copy(plan->combiner[c], AUDIO_GRAPH_KEY, clean);
        return c;
}

// A combiner let go, and every line that names it.
AUDIO_GRAPH_API void audio_plan_combiner_remove(struct audio_plan *plan, const char *name)
{
        long c = audio_plan_combiner_of(plan, name);

        if (c < 0)
                return;
        for (long w = plan->wires - 1; w >= 0; w--)
                if (audio_node_is(plan->from[w], AUDIO_NODE_COMBINER, name) || audio_node_is(plan->to[w], AUDIO_NODE_COMBINER, name))
                        audio_plan_drop(plan, w);
        plan->combiners--;
        for (long at = c; at < plan->combiners; at++)
                memory_copy(plan->combiner[at], plan->combiner[at + 1], AUDIO_GRAPH_KEY);
}

// A combiner named again, its wires with it: 0, or -1 for a name that is empty, Main's or another's.
AUDIO_GRAPH_API long audio_plan_combiner_rename(struct audio_plan *plan, const char *was, const char *now)
{
        char clean[AUDIO_GRAPH_KEY - 10], key[AUDIO_GRAPH_KEY];
        long c = audio_plan_combiner_of(plan, was);

        audio_graph_copy(clean, sizeof(clean), now);
        if (c < 0 || !clean[0] || audio_graph_same(clean, AUDIO_GRAPH_MAIN) || audio_plan_combiner_of(plan, clean) >= 0)
                return -1;
        audio_node_key(key, sizeof(key), AUDIO_NODE_COMBINER, clean);
        for (long w = 0; w < plan->wires; w++)
        {
                if (audio_node_is(plan->from[w], AUDIO_NODE_COMBINER, was))
                        audio_graph_copy(plan->from[w], AUDIO_GRAPH_KEY, key);
                if (audio_node_is(plan->to[w], AUDIO_NODE_COMBINER, was))
                        audio_graph_copy(plan->to[w], AUDIO_GRAPH_KEY, key);
        }
        audio_graph_copy(plan->combiner[c], AUDIO_GRAPH_KEY, clean);
        return 0;
}

// A level's digits: 0 to 200, or fallback when there are none.
AUDIO_GRAPH_API long audio_plan_level(const char *at, const char *limit, long fallback)
{
        long value = 0, digits = 0;

        for (; at < limit && *at >= '0' && *at <= '9' && digits < 4; at++, digits++)
                value = value * 10 + (*at - '0');
        return !digits ? fallback : value > AUDIO_GRAPH_LEVEL_MOST ? AUDIO_GRAPH_LEVEL_MOST : value;
}

// Whether a text has a colon (a key has one between its kind and its name).
AUDIO_GRAPH_API int audio_graph_colon(const char *text)
{
        while (*text && *text != ':')
                text++;
        return *text == ':';
}

/*
        The file's text as the plan: what it had is let go first. A line it does not know, or one whose fields are not all there, is
        passed over; a key with no colon is not a key. Answers the lines taken, or -1 when there was no memory for them (what was taken
        is kept).
*/
AUDIO_GRAPH_API long audio_plan_parse(struct audio_plan *plan, const char *text, long length)
{
        const char *at = text, *limit = text + (length > 0 ? length : 0);
        long taken = 0;

        audio_plan_clear(plan);
        while (at < limit)
        {
                const char *stop = at;
                char word[12], from[AUDIO_GRAPH_KEY], to[AUDIO_GRAPH_KEY], level[8], mute[8];

                while (stop < limit && *stop != '\n')
                        stop++;
                word[0] = from[0] = to[0] = level[0] = mute[0] = 0;
                at = audio_graph_field(word, sizeof(word), at, stop);
                if (at < stop && *at == '\t')
                        at = audio_graph_field(from, sizeof(from), at + 1, stop);
                if (at < stop && *at == '\t')
                        at = audio_graph_field(to, sizeof(to), at + 1, stop);
                if (at < stop && *at == '\t')
                        at = audio_graph_field(level, sizeof(level), at + 1, stop);
                if (at < stop && *at == '\t')
                        at = audio_graph_field(mute, sizeof(mute), at + 1, stop);
                at = stop < limit ? stop + 1 : stop;
                if (audio_graph_same(word, "combiner") && from[0])
                {
                        if (audio_plan_combiner_of(plan, from) < 0 && !audio_graph_same(from, AUDIO_GRAPH_MAIN))
                        {
                                if (audio_plan_combiner_add(plan, from) < 0)
                                        return -1;
                                taken++;
                        }
                        continue;
                }
                if ((!audio_graph_same(word, "wire") && !audio_graph_same(word, "cut")) || !audio_graph_colon(from) || !audio_graph_colon(to))
                        continue;
                if (audio_plan_set(plan, from, to, audio_plan_level(level, level + audio_graph_length(level), 100),
                                   word[0] == 'c' ? AUDIO_EDGE_CUT : audio_graph_same(mute, "muted") ? AUDIO_EDGE_MUTED : 0) < 0)
                        return -1;
                taken++;
        }
        return taken;
}

// The plan as the file's text into text (room bytes): its length, or the bytes it needs (more than room) when it does not fit.
AUDIO_GRAPH_API long audio_plan_format(const struct audio_plan *plan, char *text, long room)
{
        long n = 0;

#define AUDIO_PLAN_PUT(bytes, count)                                                                                                     \
        do                                                                                                                               \
        {                                                                                                                                \
                long put_ = (long)(count);                                                                                               \
                if (n + put_ < room)                                                                                                     \
                        memory_copy(text + n, (bytes), (positive)put_);                                                                  \
                n += put_;                                                                                                               \
        } while (0)
        for (long c = 0; c < plan->combiners; c++)
        {
                AUDIO_PLAN_PUT("combiner\t", 9);
                AUDIO_PLAN_PUT(plan->combiner[c], audio_graph_length(plan->combiner[c]));
                AUDIO_PLAN_PUT("\n", 1);
        }
        for (long w = 0; w < plan->wires; w++)
        {
                char digits[8];
                long count = 0, v = plan->level[w];

                if (plan->flags[w] & AUDIO_EDGE_CUT)
                        AUDIO_PLAN_PUT("cut\t", 4);
                else
                        AUDIO_PLAN_PUT("wire\t", 5);
                AUDIO_PLAN_PUT(plan->from[w], audio_graph_length(plan->from[w]));
                AUDIO_PLAN_PUT("\t", 1);
                AUDIO_PLAN_PUT(plan->to[w], audio_graph_length(plan->to[w]));
                if (!(plan->flags[w] & AUDIO_EDGE_CUT))
                {
                        do
                                digits[count++] = (char)('0' + v % 10);
                        while ((v /= 10) > 0);
                        AUDIO_PLAN_PUT("\t", 1);
                        while (count-- > 0)
                                AUDIO_PLAN_PUT(&digits[count], 1); // the count moved apart: the macro does not take its bytes when there is no room
                        if (plan->flags[w] & AUDIO_EDGE_MUTED)
                                AUDIO_PLAN_PUT("\tmuted", 6);
                }
                AUDIO_PLAN_PUT("\n", 1);
        }
#undef AUDIO_PLAN_PUT
        if (n < room)
                text[n] = 0;
        return n;
}

/*
        The old list of mirrors (/root/audio.mirror: an output's name a line, a tab and its level in percent where it is not the whole)
        taken in as wires from Main: to the output, or to the loopback microphone (moonwater.loopback). Answers the wires made.
*/
AUDIO_GRAPH_API long audio_plan_mirrors(struct audio_plan *plan, const char *list, long length)
{
        const char *at = list, *limit = list + (length > 0 ? length : 0);
        char main[AUDIO_GRAPH_KEY];
        long made = 0;

        audio_node_key(main, sizeof(main), AUDIO_NODE_COMBINER, AUDIO_GRAPH_MAIN);
        while (at < limit)
        {
                const char *stop = at;
                char name[96], key[AUDIO_GRAPH_KEY];
                long level;

                while (stop < limit && *stop != '\n')
                        stop++;
                while (at < stop && (*at == ' ' || *at == '\r'))
                        at++;
                at = audio_graph_field(name, sizeof(name), at, stop);
                while (at < stop && (*at == '\t' || *at == ' '))
                        at++;
                level = audio_plan_level(at, stop, 100);
                at = stop < limit ? stop + 1 : stop;
                if (!name[0])
                        continue;
                audio_node_key(key, sizeof(key), audio_graph_same(name, "moonwater.loopback") ? AUDIO_NODE_LOOPBACK : AUDIO_NODE_OUTPUT, name);
                if (audio_plan_set(plan, main, key, level > 100 ? 100 : level, 0) >= 0)
                        made++;
        }
        return made;
}

#endif // AUDIO_GRAPH_INCLUDED

/*
        The live graph: the server's (with the mixing core before it). It is apart from the plan's guard so a file that included the
        plan alone (a UI) and the server after it in one program each get what they need.
*/
#if defined(AUDIO_MIX_INCLUDED) && !defined(AUDIO_GRAPH_LIVE_INCLUDED)
#define AUDIO_GRAPH_LIVE_INCLUDED

struct audio_graph
{
        audio_graph_grow_fn grow;
        void *context;
        // The nodes, a column a field.
        long nodes, node_room;
        unsigned char *kind;
        unsigned int *flags;
        unsigned long long *id;                 // its key's hash: what a node is from one build to the next
        char (*name)[AUDIO_GRAPH_KEY];          // its key's name
        char (*description)[96];                // what the person is shown
        char (*machine)[32];                    // a remote node's machine
        char (*allow)[64];                      // and the line that allows it there, when it does not
        long *slot;                             // what it is of the server's (a capture's input, an output's device, a combiner's microphone, a remote's dev)
        unsigned int *latency_us, *lost;        // a remote node's
        long *first;                            // its edges out are first[k] to first[k + 1] (room for nodes + 1)
        unsigned int *ins;                      // edges in
        float **bus;                            // its bus, two channels (bus[2k], bus[2k + 1]): a combiner's or a sink's with an edge in; 0 for none
        unsigned char *own;                     // the bus is the host's own (Main's is the engine's sink), not the pool's: not cleared here
        unsigned int *order;                    // what a block mixes, in order: the sources that are not programs, then the combiners, each after what feeds it
        long ordered;
        unsigned char *mark;
        // The edges, a column a field, in the order of the node they leave once the build is ended.
        long edges, edge_room;
        unsigned int *from, *to;
        unsigned short *level;                  // percent
        unsigned char *eflags;                  // AUDIO_EDGE_*
        float *now, *target;                    // the gain an edge is at, and where it goes (it ramps)
        float *peak;                            // the greatest sample it carried since the page took it
        // The buses' memory: two channels of a block each.
        long buses, bus_room;
        float *pool;
        // The last build's gains by the ids of the ends of their edges, which the next build takes up (no wire that stays is ramped again).
        long kept, kept_room;
        unsigned long long *kept_from, *kept_to;
        float *kept_now;
        unsigned int gen;                       // a build's number
        long refused;                           // wires refused since the start (a loop of combiners)
};

AUDIO_GRAPH_API unsigned long long audio_graph_hash(int kind, const char *name)
{
        unsigned long long h = 1469598103934665603ull ^ (unsigned long long)kind;

        for (; *name; name++)
                h = (h ^ (unsigned char)*name) * 1099511628211ull;
        return h;
}

AUDIO_GRAPH_API int audio_graph_fit_nodes(struct audio_graph *g, long want)
{
        const struct audio_column columns[16] = {
            {(void **)&g->kind, 1},           {(void **)&g->flags, 4},       {(void **)&g->id, 8},          {(void **)&g->name, AUDIO_GRAPH_KEY},
            {(void **)&g->description, 96},   {(void **)&g->machine, 32},    {(void **)&g->allow, 64},      {(void **)&g->slot, sizeof(long)},
            {(void **)&g->latency_us, 4},     {(void **)&g->lost, 4},        {(void **)&g->first, sizeof(long)}, {(void **)&g->ins, 4},
            {(void **)&g->bus, 2 * sizeof(float *)}, {(void **)&g->own, 1}, {(void **)&g->order, 4},       {(void **)&g->mark, 1},
        };

        // first has a place past the last node: room for want + 1.
        return audio_columns_fit(g->grow, g->context, columns, 16, &g->node_room, want + 1);
}

AUDIO_GRAPH_API int audio_graph_fit_edges(struct audio_graph *g, long want)
{
        const struct audio_column columns[7] = {{(void **)&g->from, 4}, {(void **)&g->to, 4}, {(void **)&g->level, 2}, {(void **)&g->eflags, 1},
                                                {(void **)&g->now, 4},  {(void **)&g->target, 4}, {(void **)&g->peak, 4}};

        return audio_columns_fit(g->grow, g->context, columns, 7, &g->edge_room, want);
}

// A build begins: the gains the edges are at are kept by their ends, and the tables are emptied.
AUDIO_GRAPH_API void audio_graph_begin(struct audio_graph *g)
{
        const struct audio_column columns[3] = {{(void **)&g->kept_from, 8}, {(void **)&g->kept_to, 8}, {(void **)&g->kept_now, 4}};

        g->kept = 0;
        if (g->edges && audio_columns_fit(g->grow, g->context, columns, 3, &g->kept_room, g->edges))
        {
                for (long e = 0; e < g->edges; e++)
                        g->kept_from[e] = g->id[g->from[e]], g->kept_to[e] = g->id[g->to[e]], g->kept_now[e] = g->now[e];
                g->kept = g->edges;
        }
        g->nodes = g->edges = g->ordered = 0;
}

// A node: its kind, its name and what the person is shown of it. Its place, or -1 with no memory.
AUDIO_GRAPH_API long audio_graph_add(struct audio_graph *g, int kind, const char *name, const char *description)
{
        long k = g->nodes;

        if (!audio_graph_fit_nodes(g, k + 1))
                return -1;
        g->kind[k] = (unsigned char)kind, g->flags[k] = 0, g->id[k] = audio_graph_hash(kind == AUDIO_NODE_REMOTE_OUTPUT ? AUDIO_NODE_REMOTE_SOURCE : kind, name);
        audio_graph_copy(g->name[k], AUDIO_GRAPH_KEY, name);
        audio_graph_copy(g->description[k], 96, description ? description : name);
        g->machine[k][0] = g->allow[k][0] = 0;
        g->slot[k] = -1, g->latency_us[k] = g->lost[k] = 0, g->ins[k] = 0, g->bus[2 * k] = g->bus[2 * k + 1] = 0, g->own[k] = 0;
        g->nodes++;
        return k;
}

// The node a key names, or -1.
AUDIO_GRAPH_API long audio_graph_named(const struct audio_graph *g, const char *key)
{
        for (long k = 0; k < g->nodes; k++)
                if (audio_node_is(key, g->kind[k], g->name[k]))
                        return k;
        return -1;
}

/*
        An edge from one node's out to another's in, at a level, with its flags: an edge between the same two already there takes the new
        level and flags. 1 when the graph has it, 0 when it is refused: an end that has no such side, a node to itself, or a wire that
        would close a loop of combiners (counted in refused); -1 with no memory.
*/
AUDIO_GRAPH_API int audio_graph_wire(struct audio_graph *g, long from, long to, long level, unsigned int flags)
{
        long e;

        if (from < 0 || to < 0 || from >= g->nodes || to >= g->nodes || from == to || !audio_node_outs(g->kind[from]) || !audio_node_ins(g->kind[to]))
                return 0;
        for (e = 0; e < g->edges && !(g->from[e] == (unsigned int)from && g->to[e] == (unsigned int)to); e++)
                ;
        if (e == g->edges)
        {
                if (g->kind[from] == AUDIO_NODE_COMBINER && g->kind[to] == AUDIO_NODE_COMBINER &&
                    audio_graph_reaches(g->from, g->to, g->edges, g->mark, g->nodes, (unsigned int)to, (unsigned int)from))
                        return g->refused++, 0;
                if (!audio_graph_fit_edges(g, e + 1))
                        return -1;
                g->from[e] = (unsigned int)from, g->to[e] = (unsigned int)to, g->now[e] = 0, g->peak[e] = 0;
                g->edges++;
        }
        g->level[e] = (unsigned short)(level < 0 ? 0 : level > AUDIO_GRAPH_LEVEL_MOST ? AUDIO_GRAPH_LEVEL_MOST : level);
        g->eflags[e] = (unsigned char)(flags & ~AUDIO_EDGE_CUT);
        g->target[e] = (flags & AUDIO_EDGE_MUTED) ? 0.0f : g->level[e] == 100 ? 1.0f : audio_volume_gain((unsigned long)g->level[e] * AUDIO_VOLUME_NORM / 100);
        return 1;
}

// An edge taken out of the build (a server's wire the plan cut).
AUDIO_GRAPH_API void audio_graph_unwire(struct audio_graph *g, long from, long to)
{
        for (long e = 0; e < g->edges; e++)
                if (g->from[e] == (unsigned int)from && g->to[e] == (unsigned int)to)
                {
                        g->edges--;
                        g->from[e] = g->from[g->edges], g->to[e] = g->to[g->edges], g->level[e] = g->level[g->edges], g->eflags[e] = g->eflags[g->edges];
                        g->now[e] = g->now[g->edges], g->target[e] = g->target[g->edges], g->peak[e] = g->peak[g->edges];
                        return;
                }
}

/*
        The build ended: the edges put in the order of the node they leave (a counting sort, stable), the edges in counted, the gains of
        the edges that were there before taken up, the order of the mix made (Kahn's, over the combiners), and a bus given to every
        combiner and sink an edge goes into (the host sets its own after, audio_graph_own). 0 with no memory for the buses.
*/
AUDIO_GRAPH_API int audio_graph_end(struct audio_graph *g)
{
        long n = g->nodes, combiners = 0, at, need = 0;

        if (!audio_graph_fit_nodes(g, n) || !audio_graph_fit_edges(g, g->edges * 2))
                return 0;
        for (long k = 0; k <= n; k++)
                g->first[k] = 0;
        for (long k = 0; k < n; k++)
                g->ins[k] = 0;
        for (long e = 0; e < g->edges; e++)
                g->first[g->from[e] + 1]++, g->ins[g->to[e]]++;
        for (long k = 0; k < n; k++)
                g->first[k + 1] += g->first[k];
        // Sorted into the second half of the columns, then moved down (the room was made for twice the edges).
        {
                unsigned int *from2 = g->from + g->edges, *to2 = g->to + g->edges;
                unsigned short *level2 = g->level + g->edges;
                unsigned char *flags2 = g->eflags + g->edges;
                float *now2 = g->now + g->edges, *target2 = g->target + g->edges, *peak2 = g->peak + g->edges;

                for (long k = 0; k < n; k++)
                        g->order[k] = (unsigned int)g->first[k]; // the next place of each node's edges (order is free until below)
                for (long e = 0; e < g->edges; e++)
                {
                        unsigned int to_at = g->order[g->from[e]]++;

                        from2[to_at] = g->from[e], to2[to_at] = g->to[e], level2[to_at] = g->level[e], flags2[to_at] = g->eflags[e];
                        now2[to_at] = g->now[e], target2[to_at] = g->target[e], peak2[to_at] = g->peak[e];
                }
                memory_copy(g->from, from2, (positive)g->edges * 4);
                memory_copy(g->to, to2, (positive)g->edges * 4);
                memory_copy(g->level, level2, (positive)g->edges * 2);
                memory_copy(g->eflags, flags2, (positive)g->edges);
                memory_copy(g->now, now2, (positive)g->edges * 4);
                memory_copy(g->target, target2, (positive)g->edges * 4);
                memory_copy(g->peak, peak2, (positive)g->edges * 4);
        }
        // The gains taken up: an edge whose ends were there before goes on at the gain it was at.
        for (long e = 0; e < g->edges; e++)
                for (long k = 0; k < g->kept; k++)
                        if (g->kept_from[k] == g->id[g->from[e]] && g->kept_to[k] == g->id[g->to[e]])
                        {
                                g->now[e] = g->kept_now[k];
                                break;
                        }
        // The order: the sources that are not programs (they are mixed by the engine's streams), then the combiners, each once every
        // combiner that feeds it is in (mark counts what feeds it that is not in yet).
        at = 0;
        for (long k = 0; k < n; k++)
                if ((g->kind[k] == AUDIO_NODE_CAPTURE || g->kind[k] == AUDIO_NODE_REMOTE_SOURCE) && g->first[k + 1] > g->first[k])
                        g->order[at++] = (unsigned int)k;
        for (long k = 0; k < n; k++)
                g->mark[k] = 0;
        for (long e = 0; e < g->edges; e++)
                if (g->kind[g->from[e]] == AUDIO_NODE_COMBINER && g->kind[g->to[e]] == AUDIO_NODE_COMBINER && g->mark[g->to[e]] < 255)
                        g->mark[g->to[e]]++;
        for (long k = 0; k < n; k++)
                combiners += g->kind[k] == AUDIO_NODE_COMBINER;
        for (long placed = 0; placed < combiners;)
        {
                long before = placed;

                for (long k = 0; k < n; k++)
                        if (g->kind[k] == AUDIO_NODE_COMBINER && g->mark[k] == 0)
                        {
                                g->order[at++] = (unsigned int)k, placed++;
                                g->mark[k] = 255; // in
                                for (long e = g->first[k]; e < g->first[k + 1]; e++)
                                        if (g->kind[g->to[e]] == AUDIO_NODE_COMBINER && g->mark[g->to[e]] != 255 && g->mark[g->to[e]] > 0)
                                                g->mark[g->to[e]]--;
                        }
                if (placed == before)
                        break; // a loop, which the wires refuse: what is left is not mixed
        }
        g->ordered = at;
        // The buses: a combiner's and a sink's that an edge goes into (a combiner with none still has one: it is mixed, as silence).
        for (long k = 0; k < n; k++)
                need += audio_node_ins(g->kind[k]) && (g->ins[k] || g->kind[k] == AUDIO_NODE_COMBINER);
        if (need > g->bus_room)
        {
                long next = g->bus_room ? g->bus_room : 4;
                float *made;

                while (next < need)
                        next *= 2;
                if (!(made = g->grow(g->context, 0, 0, next * 2 * AUDIO_BLOCK * (long)sizeof(float))))
                        return 0;
                if (g->pool)
                        g->grow(g->context, g->pool, g->bus_room * 2 * AUDIO_BLOCK * (long)sizeof(float), 0);
                g->pool = made, g->bus_room = next;
        }
        g->buses = 0;
        for (long k = 0; k < n; k++)
                if (audio_node_ins(g->kind[k]) && (g->ins[k] || g->kind[k] == AUDIO_NODE_COMBINER))
                {
                        g->bus[2 * k] = g->pool + g->buses * 2 * AUDIO_BLOCK;
                        g->bus[2 * k + 1] = g->bus[2 * k] + AUDIO_BLOCK;
                        g->buses++;
                }
        g->gen++;
        return 1;
}

// A node's bus is the host's own (Main's: the engine's sink): not cleared by the graph, and what its edges in are added to.
AUDIO_GRAPH_API void audio_graph_own(struct audio_graph *g, long k, float *left, float *right)
{
        if (k < 0 || k >= g->nodes)
                return;
        g->bus[2 * k] = left, g->bus[2 * k + 1] = right, g->own[k] = 1;
}

// The buses a block is mixed into, silent: every one the graph gave (the host's own are the host's).
AUDIO_GRAPH_API void audio_graph_clear(struct audio_graph *g, long frames)
{
        for (long k = 0; k < g->nodes; k++)
                if (g->bus[2 * k] && !g->own[k])
                {
                        memory_fill(g->bus[2 * k], 0, (positive)frames * sizeof(float));
                        memory_fill(g->bus[2 * k + 1], 0, (positive)frames * sizeof(float));
                }
}

/*
        A node's block given along its edges: each edge's bus gets left and right at the edge's gain (times scale, the node's own: a
        microphone's level), the gain ramped to its target as a stream's is. The work is a multiply and an add a sample an edge, in the
        library's loop (f32_scale_add_peak) once the gain is steady.
*/
AUDIO_GRAPH_API void audio_graph_spread(struct audio_graph *g, long k, const float *left, const float *right, long frames, float scale)
{
        for (long e = g->first[k], limit = g->first[k + 1]; e < limit; e++)
        {
                float *to0 = g->bus[2 * g->to[e]], *to1 = g->bus[2 * g->to[e] + 1], target = g->target[e] * scale, now = g->now[e];

                if (!to0 || (now == 0.0f && target == 0.0f))
                        continue;
                audio_gain_run(to0, left, frames, &now, target, audio_slope(g->now[e], target), g->peak + e, 1);
                now = g->now[e];
                audio_gain_run(to1, right, frames, &now, target, audio_slope(g->now[e], target), g->peak + e, 1);
                g->now[e] = now;
        }
}

// Whether a node's only edge out goes to one node at a gain of one, steady: what it plays can go straight into that node's bus.
AUDIO_GRAPH_API int audio_graph_straight(const struct audio_graph *g, long k, long to)
{
        long e = g->first[k];

        return g->first[k + 1] == e + 1 && g->to[e] == (unsigned int)to && g->target[e] == 1.0f && g->now[e] == 1.0f;
}

#endif // AUDIO_GRAPH_LIVE_INCLUDED
