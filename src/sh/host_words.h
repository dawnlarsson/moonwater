/*
        The words a host file holds, read the one way on every host. host.c
        (the box) and host_darwin.c (the Mac's link) both include this, after
        lib.util.c, which has the file reads these are made on: the one copy
        here is the one both use.
*/

/* The bytes of a word a file held, its newline and the blanks after it taken off
   and the end made: how many are left. */
static bipolar host_word_end(p8 address_to into, bipolar got)
{
        while (got > 0 && (into[got - 1] == '\n' || into[got - 1] == '\r' ||
                           into[got - 1] == ' '))
                got--;

        into[got] = end;
        return got;
}

/* Persistent state is a regular file owned by this program. Opening it
   nonblocking first means a FIFO planted at a state name cannot stop a root
   command before it has a chance to reject the object; O_NOFOLLOW gives a
   planted final symlink the same refusal as the writers. */
static bipolar host_read_state(string_address path, p8 address_to into,
                               positive capacity)
{
        return file_slurp_regular_at(AT_FDCWD, path, into, capacity, O_NOFOLLOW);
}

/* A state word as host_read_state reads it, without its newline or the blanks
   after it; negative, and empty, when the file is not there or not a plain one.
   A FIFO or a link planted at the name on the data partition is no word, and
   cannot stop the command. */
static bipolar host_read_word(string_address path, p8 address_to into, positive room)
{
        bipolar got = host_read_state(path, into, room);

        if (got < 0)
        {
                into[0] = end;
                return got;
        }
        return host_word_end(into, got);
}
