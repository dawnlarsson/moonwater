/*
        Experimental C standard library

        ip -- links, addresses and routes

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

#ifndef STANDARD_MODERN_C_SHELL_NET
#define STANDARD_MODERN_C_SHELL_NET

#include "../net/net.c"

#define NET_STATE_DIR "/run/moonwater"
#define NET_WAKE_PATH NET_STATE_DIR "/net.wake"
#define NET_INTERNET_RUN NET_STATE_DIR "/internet"
#define NET_INTERNET_ROOT "/root/internet"
#define NET_WIFI_LIST "/root/wifi"
#define NET_WIFI_POWER "/root/wifi.power"
#define NET_WIRED_POWER "/root/wired.power"
#define NET_BLUETOOTH_LIST "/root/bluetooth"
#define NET_BLUETOOTH_POWER "/root/bluetooth.power"

/*
        The words this was called with come from the process, not from the
        shell's own table.

        A utility here is reached two ways: typed at a prompt, where the shell
        points program_argument at the words it just split, and exec'd by name
        -- which is how init starts `ip watch` -- where they are the real argv
        the kernel handed over. program_argument answers both; shell_argv only
        answers the first, and a tool reading it comes up with no arguments at
        all when something execs it.
*/
#define net_words() ((positive)program_argument_count())
#define net_word(at) program_argument((b32)(at))

/*
        The subset of ip that configures a machine.

                ip link                     what interfaces there are
                ip link set NAME up         bring one up
                ip addr                     what addresses they have
                ip addr add A.B.C.D/N dev NAME
                ip route                    what routes there are
                ip route add default via A.B.C.D [dev NAME]

        iproute2 accepts abbreviations of everything and a grammar that goes
        on for a manual. This takes the words it needs in the order iproute2
        takes them, accepts the usual short forms of the objects, and refuses
        anything else rather than guessing -- a networking command that half
        understands what it was told is worse than one that says it did not.

        The output is shaped like iproute2's because scripts read it with awk,
        not because anything here parses it back.
*/


/*
        Where this says what it did.

        Typed at a prompt, ip writes to the terminal like any other command.
        Started by init, it is a service, and a service that writes to the
        console is writing into whatever else is using it -- which is a real
        problem rather than an untidy one: the boot test reads that console
        back, and an asynchronous DHCP exchange lands wherever it lands.

        So the watcher writes to /dev/kmsg instead. printk serialises whole
        records, so a line from here can never appear in the middle of a line
        from somewhere else, and it ends up in dmesg where a system message
        belongs while still reaching the console. If /dev/kmsg will not open
        -- an older image without the node -- this falls back to writing the
        same words the ordinary way, which is worse but not silent.

        A record is one write, so the bytes are gathered until a newline
        rather than passed through as string_format produces them.
*/
static b32 net_kmsg_handle = -1;
static p8 net_kmsg_line[512];
static positive net_kmsg_used;

/*
        Every record says what level it is, and says 6.

        A write to /dev/kmsg with no level on the front is given the default
        one, which this kernel sets to 7. The console prints what is BELOW its
        own loglevel, also 7, so a message at 7 goes into the log and never
        appears -- which is exactly what happened: the machine configured
        itself perfectly and said nothing about it. 6 is KERN_INFO, which is
        what this is.
*/
#define NET_KMSG_LEVEL "<6>"
#define NET_KMSG_LEVEL_BYTES 3

static COLD fn net_kmsg_begin(void)
{
        memory_copy(net_kmsg_line, NET_KMSG_LEVEL, NET_KMSG_LEVEL_BYTES);
        net_kmsg_used = NET_KMSG_LEVEL_BYTES;
}

static COLD fn net_kmsg(address_any data, positive length)
{
        p8 address_to bytes = (p8 address_to)data;
        positive at;

        if (!length)
                length = string_length(bytes);

        if (net_kmsg_used < NET_KMSG_LEVEL_BYTES)
                net_kmsg_begin();

        for (at = 0; at < length; at++)
        {
                if (bytes[at] == '\n' || net_kmsg_used + 2 >= sizeof net_kmsg_line)
                {
                        if (net_kmsg_used > NET_KMSG_LEVEL_BYTES)
                                system_write_all((positive)net_kmsg_handle,
                                                 net_kmsg_line, net_kmsg_used);

                        net_kmsg_begin();

                        if (bytes[at] == '\n')
                                continue;
                }

                net_kmsg_line[net_kmsg_used++] = bytes[at];
        }
}

//      The terminal by default; the kernel log once init owns this.
static writer net_out = log;

static COLD fn net_kmsg_close(void)
{
        if (net_kmsg_handle >= 0)
                (void)system_close(net_kmsg_handle);
        net_kmsg_handle = -1;
        net_kmsg_used = 0;
        net_out = log;
}

static fn net_flush(void)
{
        if (net_out == log)
                log_flush();
}

static COLD bool net_word_is(string_address word, const char *full, positive least)
{
        if (!word)
                return false;
        positive length = string_length(word);
        positive full_length = string_length((string_address)full);
        return length >= least && length <= full_length &&
               !string_compare_max(word, (string_address)full, length);
}

/* A refusal in ip's own voice, for the cases with no errno behind them: the
   grammar it was given is not one it knows. */
static COLD b32 net_ip_refused(const char address_to why)
{
        string_format(net_out, "ip: %s\n", (string_address)why);
        net_flush();

        return 1;
}

//      The errno the kernel gave, in the words libc gives it.
static COLD b32 net_refused(string_address doing, bipolar status)
{
        string_address text = status < 0 ? system_error_message(-status) : null;

        if (text)
                string_format(net_out, "ip: %s: %s\n", doing, text);
        else
                string_format(net_out, "ip: %s: failed (%p)\n", doing, (positive)(-status));

        net_flush();

        return 1;
}

//      What a netlink request answered, as ip's status: nothing to say when it worked.
static COLD b32 net_done(string_address doing, bipolar done)
{
        return done < 0 ? net_refused(doing, done) : 0;
}

/*
        One address written into the caller's bytes as text it can hand to %s.

        host_into answers with a length and not with a string, because it also
        fills a field in the middle of a longer line. Eight places here wanted
        the other thing, and every one of them wrote the terminator itself --
        eight chances to write it one byte early or one byte late, in a buffer
        whose length only the call site knows.
*/
static COLD string_address net_host_text(p8 address_to into, p32 host)
{
        into[host_into(into, host)] = end;

        return (string_address)into;
}

/*
        A.B.C.D/N split into the two halves it is written as.

        A missing prefix is /32 for an address, which is what iproute2 does
        and is almost never what was meant -- but guessing /24 from the first
        octet is the classful arithmetic that stopped being true in 1993, so
        the answer is to be literal and let the user say.
*/
static COLD bool net_split_prefix(string_address text, p32 address_to host,
                             p8 address_to bits)
{
        string_address slash = string_first_of(text, '/');
        string_address digits;
        p8 kept[64];
        bipolar parsed;
        positive prefix;
        positive length;

        length = slash ? (positive)(slash - text) : string_length(text);

        if (length >= sizeof(kept))
                return false;

        string_copy_max_end(kept, text, length);

        parsed = string_to_host(kept);

        if (parsed < 0)
                return false;

        address_to host = (p32)parsed;

        if (!slash)
        {
                address_to bits = 32;
                return true;
        }

        if (!string_get(slash + 1))
                return false;

        /* string_to_positive deliberately reads only the trailing digits of
           a general string, so `junk24` means 24 to its other callers.  A
           network prefix is a complete grammar token: accepting that suffix
           here silently configures a different route than the user wrote. */
        digits = slash + 1;
        if (!string_digits_checked(address_of digits, 10,
                                   address_of prefix) ||
            string_get(digits) || prefix > 32)
                return false;

        address_to bits = (p8)prefix;

        return true;
}

// The flags of a link, in the shape iproute2 writes them.
static COLD fn net_say_flags(p32 flags)
{
        static const struct { p32 bit; const char address_to name; } names[] = {
            {IFF_UP, "UP"}, {IFF_BROADCAST, "BROADCAST"},
            {IFF_LOOPBACK, "LOOPBACK"}, {IFF_RUNNING, "LOWER_UP"}};
        string_address between = (string_address) "";

        string_format(net_out, "<");
        for (positive at = 0; at < array_count(names); at++)
                if (flags & names[at].bit)
                {
                        string_format(net_out, "%s%s", between,
                                      (string_address)names[at].name);
                        between = (string_address) ",";
                }
        string_format(net_out, ">");
}

/*
        Which index is which name, gathered once.

        A route names its interface by index and wants printing by name, and
        the obvious answer -- look the name up while walking the routes -- is
        the one thing netlink will not do: a dump started inside a dump is
        refused with EBUSY, because the first is still in progress on the
        socket. So the links are walked first, into a table, and the route
        walk only reads it.
*/
typedef struct
{
        p32 index;
        p8 name[IFNAME_SIZE];
} net_name;

static netlink_buffer net_names;

static COLD bool net_name_seen(netlink_header address_to header, address_any context)
{
        netlink_buffer address_to names =
            (netlink_buffer address_to)context;
        netlink_link address_to link;
        string_address found = netlink_link_name(header, address_of link);
        net_name address_to entry;
        positive count = names->used / sizeof(net_name);

        if (!found)
                return true;

        if (names->used > positive_max - sizeof(net_name))
        {
                names->failed = true;
                return false;
        }
        if (!net_room(names, names->used + sizeof(net_name)))
                return false;

        entry = ((net_name address_to)names->bytes) + count;
        entry->index = link->index;
        string_copy_max_end(entry->name, found, IFNAME_SIZE - 1);
        names->used += sizeof(net_name);

        return true;
}

static COLD bool net_names_commit(netlink_buffer address_to next, bipolar status)
{
        if (status < 0 || next->failed)
        {
                netlink_forget(next);
                return false;
        }

        netlink_forget(address_of net_names);
        net_names = *next;
        memory_fill(next, 0, sizeof(*next));
        return true;
}

static COLD bipolar net_names_gather(b32 handle)
{
        netlink_buffer next = {0};
        bipolar status = netlink_dump(
            handle, RTM_GETLINK, sizeof(netlink_link), AF_UNSPEC,
            net_name_seen, address_of next);

        if (net_names_commit(address_of next, status))
                return 0;
        return status < 0 ? status : -ERROR_NO_MEMORY;
}

static PURE COLD string_address net_name_of(p32 index)
{
        positive at;
        positive count = net_names.used / sizeof(net_name);

        for (at = 0; at < count; at++)
        {
                net_name address_to entry = ((net_name address_to)net_names.bytes) + at;

                if (entry->index == index)
                        return entry->name;
        }

        return null;
}

static COLD bool net_link_line(netlink_header address_to header, address_any context)
{
        netlink_link address_to link;
        string_address name = netlink_link_name(header, address_of link);
        (void)context;

        if (!name)
                return true;

        string_format(net_out, "%p: %w: ", (positive)link->index, writer_terminal_name, name);
        net_say_flags(link->flags);
        return string_report(net_out, true, " state %s\n",
                      (link->flags & IFF_UP) ? (string_address) "UP"
                                             : (string_address) "DOWN");
}

//      An address line wants the interface's name, and the address dump gives
//      only its index, so the name is looked up once per line rather than the
//      whole link table being held.
static COLD bool net_address_line(netlink_header address_to header, address_any context)
{
        netlink_address address_to body =
            netlink_message_body(header, sizeof(netlink_address));
        positive size = 0;
        positive label_size = 0;
        p8 address_to held;
        string_address label;
        p8 written[32];
        p32 host;

        if (!body)
                return true;

        held = (p8 address_to)netlink_find(header, sizeof(netlink_address),
                                           IFA_LOCAL, address_of size);
        label = (string_address)netlink_find(header, sizeof(netlink_address),
                                             IFA_LABEL,
                                             address_of label_size);
        if (!label_size || !label || !memory_first_of(label, 0, label_size))
                label = null;

        if (!held || size != 4 || body->family != AF_INET)
                return true;

        host = network_order_32(address_to((p32 address_to)held));

        string_format(net_out, "%p: %w    inet %s/%p\n", (positive)body->index,
                      writer_terminal_name, label ? label : (string_address) "?",
                      net_host_text(written, host), (positive)body->prefix);

        (void)context;

        return true;
}

static COLD bool net_route_line(netlink_header address_to header, address_any context)
{
        netlink_route address_to body =
            netlink_message_body(header, sizeof(netlink_route));
        positive gateway_size = 0;
        positive destination_size = 0;
        positive out_size = 0;
        p8 address_to gateway;
        p8 address_to destination;
        p8 address_to out;
        p8 written[32];
        (void)context;

        if (!body)
                return true;

        gateway = (p8 address_to)netlink_find(
            header, sizeof(netlink_route), RTA_GATEWAY,
            address_of gateway_size);
        destination = (p8 address_to)netlink_find(
            header, sizeof(netlink_route), RTA_DST,
            address_of destination_size);
        out = (p8 address_to)netlink_find(
            header, sizeof(netlink_route), RTA_OIF, address_of out_size);

        if (body->family != AF_INET || body->table != RT_TABLE_MAIN)
                return true;

        /* Every IPv4 route value used below is exactly one p32.  A shorter
           attribute would read into padding or the next attribute; a longer
           one is not the route shape this formatter understands. */
        if ((gateway && gateway_size != 4) ||
            (destination && destination_size != 4) ||
            (out && out_size != 4))
                return true;

        if (destination)
                string_format(net_out, "%s/%p",
                              net_host_text(written, network_order_32(
                                  address_to((p32 address_to)destination))),
                              (positive)body->destination_bits);
        else
                string_format(net_out, "default");

        if (gateway)
                string_format(net_out, " via %s",
                              net_host_text(written, network_order_32(
                                  address_to((p32 address_to)gateway))));

        if (out)
        {
                p32 index = address_to((p32 address_to)out);
                string_address name = net_name_of(index);

                if (name)
                {
                        string_format(net_out, " dev %w", writer_terminal_name, name);
                }
                else
                        string_format(net_out, " dev %p", (positive)index);
        }

        return string_report(net_out, true, "\n");
}

//      The index of the link the user named.
static COLD bipolar net_index_of(b32 handle, string_address name)
{
        netlink_search search = {.wanted = name};
        bipolar status = netlink_link_find(handle, address_of search);

        return status < 0 ? status : (bipolar)search.index;
}


/*
        host NAME [SERVER]

        The server is normally the first nameserver line in /etc/resolv.conf.
        It can be given instead, which is the only way to ask anything on a
        machine that has no resolv.conf -- which the boot image does not, and
        which is exactly when someone is trying to find out whether the
        network works at all.
*/
static COLD b32 net_host(void)
{
        p32 found = 0;
        p8 written[32];
        bipolar server;
        bipolar status;

        if (net_words() < 2)
        {
                string_format(net_out, "usage: host NAME [SERVER]\n");
                net_flush();
                return 1;
        }

        if (net_words() > 2)
        {
                server = string_to_host(net_word(2));

                if (server < 0)
                {
                        string_format(net_out, "host: %w is not an address\n",
                                      writer_terminal_quoted_name, net_word(2));
                        net_flush();
                        return 1;
                }

                status = dns_resolve_at((p32)server, DNS_PORT, net_word(1),
                                        address_of found, 5);
        }
        else
        {
                status = dns_resolve_any((string_address) "/etc/resolv.conf",
                                         net_word(1), address_of found, 3);
        }

        switch (status)
        {
        case DNS_OK:
                string_format(net_out, "%w has address ", writer_terminal_quoted_name, net_word(1));
                string_format(net_out, "%s\n", net_host_text(written, found));
                break;
        case DNS_NO_SUCH_NAME:
                string_format(net_out, "host: %w: no such name\n", writer_terminal_quoted_name,
                              net_word(1));
                break;
        case DNS_NO_ADDRESS:
                string_format(net_out, "host: %w exists but has no address\n",
                              writer_terminal_quoted_name, net_word(1));
                break;
        case DNS_NO_REPLY:
                string_format(net_out, "host: no reply from the nameserver\n");
                break;
        case DNS_NO_SERVER:
                //      Not a bad answer -- no way to ask at all. Before an
                //      address exists there is no route to a nameserver, and
                //      saying the reply made no sense would send somebody
                //      looking at the wrong end of it.
                string_format(net_out, "host: cannot reach a nameserver; "
                                   "is the network up? try: ip auto\n");
                break;
        case DNS_REFUSED:
                string_format(net_out, "host: the nameserver refused the question\n");
                break;
        case DNS_NO_RANDOM:
                string_format(net_out, "host: secure randomness is not ready\n");
                break;
        default:
                string_format(net_out, "host: the reply made no sense\n");
                break;
        }

        net_flush();

        return status == DNS_OK ? 0 : 1;
}


/*
        fetch URL

        The body goes to standard output, so it redirects into a file or pipes
        into anything else the way every other tool here does. What went wrong
        goes to the log, which is where a script looking only at the bytes
        will not mistake it for content.
*/
static b32 net_fetch(void)
{
        p8 name[256];
        http_buffer body = {0};
        string_address path;
        p16 port;
        bool tls;
        bipolar status;
        b32 code = 0;

        if (net_words() < 2)
        {
                string_format(net_out, "usage: fetch http://host[:port]/path\n");
                goto failed;
        }

        status = http_split_into(net_word(1), name, sizeof name, address_of port,
                                 address_of path, address_of tls);

        //      A url that did not come apart is not a url whose scheme is
        //      worth reporting, so the shape is answered before the scheme.
        if (status < 0)
        {
                string_format(net_out, "fetch: %w is not a url this understands\n",
                              writer_terminal_quoted_name, net_word(1));
                goto failed;
        }

        if (tls)
        {
                string_format(net_out, "fetch: https is not implemented; this speaks "
                                   "http only. use wget\n");
                goto failed;
        }

        //      The url is handed over whole rather than the pieces above: the
        //      client resolves it, and a literal address needs no resolver,
        //      which is what makes the test able to fetch from a socket on
        //      loopback with no nameserver anywhere in sight.
        status = http_get(net_word(1), address_of body, address_of code);

        if (status == HTTP_NO_HOST)
                string_format(net_out, "fetch: cannot resolve %w\n",
                              writer_terminal_quoted_name, name);
        else if (status == HTTP_NO_ROUTE)
                string_format(net_out, "fetch: cannot reach %w\n",
                              writer_terminal_quoted_name, name);
        else if (status == HTTP_NO_REPLY)
                string_format(net_out, "fetch: no reply from %w\n",
                              writer_terminal_quoted_name, name);
        else if (status == HTTP_STATUS && http_response_is_redirect(code))
                string_format(net_out, "fetch: %p, which is a redirect this does not "
                                   "follow\n", (positive)code);
        else if (status == HTTP_STATUS)
                string_format(net_out, "fetch: the server answered %p\n", (positive)code);
        else if (status < 0)
                string_format(net_out, "fetch: the reply made no sense\n");
        else
        {
                bool short_write = body.used &&
                    system_write_all(1, body.bytes, body.used) != body.used;

                http_forget(address_of body);
                return short_write ? string_report(log_error, 1,
                                         "fetch: write error on standard output\n")
                                   : 0;
        }

failed:
        net_flush();
        http_forget(address_of body);
        return 1;
}


static const argument_option wget_options[] = {
    {"output-document", 'O', ARGUMENT_REQUIRED},
    {"quiet", 'q'},
    {"help", 'h'},
    {"version", 'V'},
    {"no-check-certificate", 'K'},
    {null},
};

/* Network files use the same descriptor-bound output transaction as the file
   tools. They add a data sync before publication because a downloaded image
   and resolv.conf must survive the power loss that may immediately follow
   boot-time networking. */
static COLD bipolar net_staged_name_publish(file_staged_name address_to stage)
{
        bipolar directory = system_open_at(
            stage->directory, (string_address)".",
            FILE_READ | O_DIRECTORY | O_CLOEXEC);
        if (directory < 0)
        {
                file_staged_name_abort(stage);
                return directory;
        }

        /* A device or FIFO written in place has no data of its own to
           sync, and fsync on one answers EINVAL. */
        bipolar prepared = file_staged_name_prepare(stage);
        bipolar synced = prepared < 0 || stage->direct ? prepared : system_call_1(
            syscall(fsync), (positive)stage->handle);

        if (synced < 0)
        {
                file_staged_name_abort(stage);
                system_close(directory);
                return synced;
        }

        bipolar published = file_staged_name_finish(stage, true, 0);
        if (published < 0)
        {
                system_close(directory);
                return published;
        }

        /* The rename above committed the caller-visible result.  Sync and
           close still strengthen crash durability, but neither can turn that
           published name back into a pre-publication failure for callers that
           would otherwise roll back unrelated network state. */
        (void)system_call_1(syscall(fsync), (positive)directory);
        (void)system_close(directory);
        return 0;
}

/* wget's exit statuses are GNU wget's: 1 anything else, 2 a command line
   it cannot parse, 3 a file it cannot write, 4 the network, 6 a server
   refusing credentials, 8 a server answering with an error. */
#define WGET_GENERIC 1
#define WGET_USAGE 2
#define WGET_FILE 3
#define WGET_NETWORK 4
#define WGET_AUTH 6
#define WGET_SERVER 8

/* -O through a link: GNU wget writes wherever the link points, so -O
   /dev/stdout reaches the terminal, pipe or file behind it. Here the link
   is followed only where it and what it names belong to root or to the
   caller, and only to a character device, a FIFO, or the file already open
   as standard output -- which is then written through that descriptor, at
   its offset. Any other link to a regular file stays replaced rather than
   written through: planted in a shared directory it would aim a download
   at somebody's file. */
static COLD bipolar net_wget_stream_link(string_address output)
{
        file_facts link;
        file_facts target;
        file_facts standard;
        p32 me;
        positive kind;

        if (file_look_code(AT_FDCWD, output, AT_SYMLINK_NOFOLLOW,
                           address_of link) < 0 ||
            (link.mode & MODE_FORMAT) != MODE_LINK)
                return -1;
        me = (p32)system_call(syscall(geteuid));
        if ((link.owner && link.owner != me) ||
            file_look_code(AT_FDCWD, output, 0, address_of target) < 0)
                return -1;
        kind = target.mode & MODE_FORMAT;
        if (kind == MODE_FILE &&
            file_look_code(1, (string_address) "", AT_EMPTY_PATH,
                           address_of standard) >= 0 &&
            file_same_identity(address_of target, address_of standard))
                return 1;
        if ((kind != MODE_CHARACTER && kind != MODE_PIPE) ||
            (target.owner && target.owner != me))
                return -1;
        return file_open_same(AT_FDCWD, output, address_of target,
                              O_WRONLY | O_NOCTTY);
}

//      Why a download failed, in GNU wget's words where it has them.
static COLD b32 net_wget_failed(bipolar status, b32 code, p8 address_to where,
                                string_address output)
{
        p8 host[256];
        string_address path;
        p16 port;
        bool tls;

        if (http_split_into(where, host, sizeof host, address_of port,
                            address_of path, address_of tls))
                host[0] = end;
        switch (status)
        {
        case HTTP_SCHEME:
                return string_report(log_error, WGET_GENERIC,
                                     "%w: Unsupported scheme.\n",
                                     writer_terminal_quoted_name, where);
        case HTTP_BAD_URL:
                return string_report(log_error, WGET_GENERIC,
                                     "wget: %w is not a url this understands\n",
                                     writer_terminal_quoted_name, where);
        case HTTP_NO_HOST:
                return string_report(log_error, WGET_NETWORK,
                                     "wget: unable to resolve host address '%w'\n",
                                     writer_terminal_quoted_name, host);
        case HTTP_NO_ROUTE:
                return string_report(log_error, WGET_NETWORK,
                                     "wget: cannot reach %w\n",
                                     writer_terminal_quoted_name, host);
        case HTTP_NO_REPLY:
                return string_report(log_error, WGET_NETWORK,
                                     "wget: no reply from %w\n",
                                     writer_terminal_quoted_name, host);
        case HTTP_MALFORMED:
                return string_report(log_error, WGET_NETWORK,
                                     "wget: %w sent a reply this cannot read\n",
                                     writer_terminal_quoted_name, host);
        case HTTP_TLS:
                return string_report(log_error, WGET_NETWORK,
                                     "wget: TLS handshake with %w failed\n",
                                     writer_terminal_quoted_name, host);
        case HTTP_PRIVATE:
                return string_report(log_error, WGET_GENERIC,
                                     "wget: refused a redirect to %w, an address "
                                     "that is not public\n",
                                     writer_terminal_quoted_name, where);
        case HTTP_DOWNGRADE:
                return string_report(log_error, WGET_GENERIC,
                                     "wget: refused an HTTPS to HTTP redirect "
                                     "to %w\n",
                                     writer_terminal_quoted_name, where);
        case HTTP_REDIRECTS:
                return string_report(log_error, WGET_SERVER,
                                     "%p redirections exceeded.\n",
                                     (positive)(HTTP_HOPS - 1));
        //      GNU wget's own line, and its file I/O status.
        case HTTP_WRITE:
                return string_report(log_error, WGET_FILE,
                                     "Cannot write to '%w' (%s).\n",
                                     writer_terminal_quoted_name, output,
                                     file_reason(http_write_failure));
        case HTTP_STATUS:
                if (code == 401)
                        return string_report(
                            log_error, WGET_AUTH,
                            "Username/Password Authentication Failed.\n");
                return string_report(log_error, WGET_SERVER,
                                     "wget: %w returned %p\n",
                                     writer_terminal_quoted_name, host,
                                     (positive)code);
        }
        return string_report(log_error, WGET_GENERIC, "wget: download failed\n");
}

/*
        wget [ -q ] [ -O FILE ] [ --no-check-certificate ] URL

        BusyBox's subset of GNU wget, with GNU's defaults: save the body under
        the URL's last component -- as NAME.1, NAME.2 and on when that name is
        taken -- or under -O, which replaces; follow redirects; exit with
        GNU's statuses. HTTPS is the reason this exists; fetch remains the
        small plaintext tool.
*/
static b32 net_wget(void)
{
        file_taking taking = {
            .program = (string_address) "wget",
            .options = wget_options,
        };
        string_address url;
        string_address output;
        p8 name[256];
        p8 leaf[256];
        p8 numbered[256 + 24];
        p8 where[HTTP_URL_MAX];
        string_address path;
        p16 port;
        bool tls;
        bool quiet;
        bool check_cert;
        bipolar dest = -1;
        bipolar status;
        b32 code = 0;
        bool own_file = false;
        bool own_stream = false;
        file_staged_name staged;

        if (!file_take(address_of taking))
                return WGET_USAGE;

        if (file_meta(address_of taking,
                      (string_address) "[-q] [-O FILE] [--no-check-certificate] URL",
                      log))
        {
                log_flush();
                return 0;
        }

        if (taking.first >= (positive)program_argument_count())
        {
                string_format(log_error, "wget: missing URL\n");
                return string_report(log_error, WGET_GENERIC,
                                     "Usage: wget [-q] [-O FILE] "
                                     "[--no-check-certificate] URL\n");
        }

        if (taking.first + 1 < (positive)program_argument_count())
        {
                return string_report(log_error, WGET_GENERIC,
                                     "wget: extra operand '%w'\n",
                                     writer_terminal_quoted_name,
                                     program_argument((b32)(taking.first + 1)));
        }

        url = program_argument((b32)taking.first);
        quiet = (taking.flags & FILE_FLAG('q')) != 0;
        check_cert = (taking.flags & FILE_FLAG('K')) == 0;
        output = file_option_value(address_of taking, 'O');

        status = http_split_into(url, name, sizeof name, address_of port,
                                 address_of path, address_of tls);
        if (status)
                return net_wget_failed(status, 0, url, null);

        if (output && string_equals(output, (string_address) "-"))
                dest = 1;
        else if (output && (dest = net_wget_stream_link(output)) >= 0)
                own_stream = dest != 1;
        else
        {
                /* A device or FIFO is written into, as GNU wget does, never
                   replaced: as root, -O /dev/null would otherwise put a
                   regular file where the device was. Without -O a name that
                   is taken is left alone for the next number, and a
                   directory there is GNU's "Is a directory"; publication
                   refuses to replace a name that appears meanwhile. */
                bool named = output != null;

                if (!named)
                        http_url_leaf(path, leaf, sizeof leaf);
                output = named ? output : leaf;
                for (positive copy = 1;; copy++)
                {
                        file_facts taken;

                        dest = file_staged_name_open(
                            address_of staged, output, 0666 & ~file_umask(),
                            FILE_STAGED_STREAM_SPECIAL |
                                (named ? 0 : FILE_STAGED_NO_REPLACE));
                        if (named || dest != -ERROR_EXISTS || copy > 999999)
                                break;
                        if (file_look_code(AT_FDCWD, output, 0,
                                           address_of taken) >= 0 &&
                            (taken.mode & MODE_FORMAT) == MODE_DIRECTORY)
                        {
                                dest = -ERROR_IS_DIRECTORY;
                                break;
                        }
                        positive used = string_length(leaf);

                        memory_copy(numbered, leaf, used);
                        numbered[used++] = '.';
                        numbered[used + positive_into(numbered + used, copy)] = end;
                        output = numbered;
                }
                if (dest < 0)
                        return named ? string_report(log_error, WGET_GENERIC,
                                                     "%w: %s\n",
                                                     writer_terminal_quoted_name,
                                                     output, file_reason(dest))
                                     : string_report(log_error, WGET_FILE,
                                                     "Cannot write to '%w' (%s).\n",
                                                     writer_terminal_quoted_name,
                                                     output, file_reason(dest));
                own_file = true;
        }

        if (!quiet)
        {
                string_format(log_error, "%w\nSaving to: '%w'\n", writer_terminal_quoted_name, url,
                              writer_terminal_quoted_name, output);
        }

        string_copy(where, url);
        status = http_fetch_to(url, dest, check_cert, address_of code, where);
        if (own_stream)
                system_close((positive)dest);

        if (status)
        {
                if (own_file)
                        file_staged_name_abort(address_of staged);
                return net_wget_failed(status, code, where, output);
        }

        if (own_file)
        {
                bipolar published = net_staged_name_publish(address_of staged);

                if (published < 0)
                        return string_report(log_error, WGET_FILE,
                                             "Cannot write to '%w' (%s).\n",
                                             writer_terminal_quoted_name,
                                             output, file_reason(published));
        }

        return 0;
}


/*
        /etc/resolv.conf: the network's resolver first, a public one behind it.

        The resolver the DHCP server handed out goes first, as dhclient and
        every other client write it, and Cloudflare's goes second, to be
        asked only when the first gives no answer at all (a router whose
        resolver is down, slow or refusing): the resolver believes the first
        server that answers, "no such name" included (dns_resolve_any). A
        network with no resolver of its own, or one that names Cloudflare
        itself, has Cloudflare alone.

        This replaces an order written in the other direction, public resolver
        first. Its reason was a network whose own resolver was slow or
        answered with a captive portal's idea of the truth; it cost every
        machine on every network one name in the clear per lookup, to a
        third party, before the network's own resolver (which knows the names
        inside the network and is the only one that should be told them) was
        asked at all. The two failures it guarded against are the ones the
        fallback still covers, the first directly (no answer goes on), the
        second only as far as a portal that lies about a name is one that
        also lied before.

        Order is the whole of the policy. Putting it here rather than in the
        resolver means changing which server is preferred is one line in a
        file, not a rebuild.
*/
static COLD bipolar net_write_resolv_to(string_address path, p32 nameserver)
{
        const p32 servers[2] = {
            nameserver ? nameserver : DNS_FALLBACK,
            nameserver && nameserver != DNS_FALLBACK ? DNS_FALLBACK : 0};
        p8 line[64];
        positive used = 0;
        file_staged_name staged;
        bipolar handle;

        for (positive at = 0; at < 2 && servers[at]; at++)
        {
                memory_copy(line + used, "nameserver ", 11);
                used += 11 + host_into(line + used + 11, servers[at]);
                line[used++] = '\n';
        }

        handle = file_staged_name_open(
            address_of staged, path, 0644 & ~file_umask(), 0);

        if (handle < 0)
                return handle;

        if (system_write_all((positive)handle, line, used) != used)
        {
                file_staged_name_abort(address_of staged);
                return -ERROR_INPUT_OUTPUT;
        }

        return net_staged_name_publish(address_of staged);
}

static COLD bipolar net_write_resolv(p32 nameserver)
{
        return net_write_resolv_to((string_address) "/etc/resolv.conf",
                                   nameserver);
}

/*
        ip auto -- the whole thing, without being told anything.

        Find a link that is not loopback, bring it up, ask for a lease, and
        apply what comes back. This is what "the network just works" means in
        practice, and it is deliberately userspace rather than kernel: the
        kernel's own IP_PNP runs once, before init, for one interface, and
        cannot write a resolv.conf or try again when a cable is plugged in.

        Every step says what it did, because the failure that matters is not
        an error code but the machine coming up silently unreachable.
*/

/*
        What this machine is holding, and since when.

        A lease has a renewal boundary (T1), a rebinding boundary (T2), and an
        expiry.  Failed requests are retried within the remaining interval;
        the address is kept until a server rejects it or expiry is reached.
        Nothing else here needs a clock, so this is the only place one is read.
*/
typedef struct
{
        p32 index;
        p8 name[IFNAME_SIZE];
        p8 hardware[6];
        dhcp_lease lease;
        positive taken;
        p32 retry;
        bool address_owned;
        bool route_owned;
        bool lost;
        //      The link's count of carrier losses when the lease was asked
        //      for, where the kernel keeps one.
        bool carrier_counted;
        p32 carrier_downs;
} net_holding;

/* The lease clock is CLOCK_BOOTTIME: a lease runs out on the server's clock
   while this machine sleeps, and the monotonic clock stops for a suspend, so
   an hour's lease taken before a night asleep still looked half new in the
   morning and the address stayed on the wire after the server had given it
   to somebody else. */
#define NET_CLOCK_BOOTTIME 7

static positive net_seconds(void)
{
        timespec stamp = {0, 0};
        positive now = 0;

        if (clock_gettime(NET_CLOCK_BOOTTIME, address_of stamp) >= 0 &&
            stamp.tv_sec >= 0)
                now = (positive)stamp.tv_sec * NETWORK_NANOSECONDS +
                      (positive)stamp.tv_nsec;

        //      A clock that will not answer leaves every lease looking
        //      expired, so an address is never kept beyond an unknown
        //      deadline. Zero is that answer, and the clock's first second
        //      is when a boot takes its lease, so it reads 1.
        return now ? now / NETWORK_NANOSECONDS + 1 : 0;
}

/* Deleting state which the kernel already discarded is the same outcome as
   deleting it ourselves. A vanished interface reports ENODEV; absent
   addresses and routes are reported as ENOENT or ESRCH. */
static COLD bool net_change_gone(bipolar status)
{
        return status >= 0 || status == -ERROR_NO_ENTRY ||
               status == -ERROR_NO_PROCESS || status == -ERROR_NO_DEVICE;
}

static COLD bool net_holds_address(const net_holding address_to held, p32 index,
                              const dhcp_lease address_to lease)
{
        return held && held->index == index &&
               held->lease.address == lease->address &&
               dhcp_prefix_of(held->lease.mask) == dhcp_prefix_of(lease->mask);
}

static COLD bool net_holds_route(const net_holding address_to held, p32 index,
                            const dhcp_lease address_to lease)
{
        if (!held)
                return !lease->router;

        return held->lease.router == lease->router &&
               (!lease->router || held->index == index);
}

/* Kernel objects are removed only when an exclusive create, or a transition
   from an object already owned here, established that right.  A matching
   address or route discovered at process start remains somebody else's. */
static COLD bool net_owns_address(const net_holding address_to held)
{
        return held && held->index && held->lease.address &&
               held->address_owned;
}

static COLD bool net_owns_route(const net_holding address_to held)
{
        return held && held->index && held->lease.router && held->route_owned;
}

static COLD bool net_ownership_next(bool owned, bool changed, bool installed,
                               bool exists)
{
        return exists && (changed ? installed : owned);
}

/* Unsigned subtraction deliberately treats a clock failure or regression as
   an expired lease: keeping an address past the server's deadline can create
   an address collision, while releasing it merely requires reacquisition. */
static COLD bool net_lease_expired_at(const net_holding address_to held,
                                 positive now)
{
        if (!held || !held->index || !held->lease.seconds)
                return false;

        return !now || !held->taken || now < held->taken ||
               now - held->taken >= held->lease.seconds;
}

/* A server picks T1 and T2 for the lease it hands out, and a T1 of a second on
   a lease of an hour makes a client fork, ask, rewrite resolv.conf and log every
   second for as long as the server says so. The renewal is not scheduled sooner
   than ten seconds after the lease was taken, or than half the lease where that
   is shorter (a lease of three seconds is renewed at one, as it says): the lease
   is taken as it was given and the interval is the client's own. */
#define NET_RENEW_FLOOR_SECONDS 10

static COLD positive net_lease_due_in(const net_holding address_to held,
                                 positive now)
{
        positive gone;
        positive retry;
        positive floor;

        if (!held || !held->index || !held->lease.seconds)
                return 0;
        if (held->lost)
                return 1;
        if (net_lease_expired_at(held, now))
                return 1;

        gone = now - held->taken;
        retry = held->retry ? held->retry : held->lease.renewal;
        floor = min(held->lease.seconds / 2, (positive)NET_RENEW_FLOOR_SECONDS);
        if (retry && retry < floor)
                retry = floor;
        if (!retry || retry > held->lease.seconds)
                retry = held->lease.seconds;
        return gone >= retry ? 1 : retry - gone;
}

static COLD bool net_lease_rebinding_at(const net_holding address_to held,
                                   positive now)
{
        return held && held->index && held->taken && held->lease.rebinding &&
               now >= held->taken &&
               now - held->taken >= held->lease.rebinding;
}

/* A request never waits beyond the next state boundary.  This is observable
   for very short but legal leases: a four-second socket deadline must not keep
   using an address after T2 or expiry. */
static COLD positive net_lease_attempt_time(const net_holding address_to held,
                                       positive now, bool rebinding)
{
        positive gone;
        positive boundary;
        positive remaining;

        if (!held || net_lease_expired_at(held, now))
                return 0;
        gone = now - held->taken;
        boundary = rebinding ? held->lease.seconds : held->lease.rebinding;
        if (!boundary || gone >= boundary)
                return 0;
        remaining = boundary - gone;
        return remaining < 4 ? remaining : 4;
}

/* RFC 2131 retries half way to the next state boundary, with sixty seconds as
   the normal floor.  A short remaining lease clamps that floor at the boundary
   so RENEWING cannot run past T2 and REBINDING cannot run past expiry. */
static COLD fn net_lease_retry_after(net_holding address_to held, positive now,
                                bool attempted_rebinding)
{
        positive gone;
        positive boundary;
        positive remaining;
        positive delay;

        if (!held || net_lease_expired_at(held, now))
                return;

        gone = now - held->taken;
        if (!attempted_rebinding &&
            (!held->lease.rebinding || gone >= held->lease.rebinding))
        {
                held->retry = (p32)gone;
                return;
        }

        boundary = attempted_rebinding ? held->lease.seconds
                                       : held->lease.rebinding;
        remaining = boundary - gone;
        delay = remaining / 2;
        if (delay < 60)
                delay = 60;
        if (delay > remaining)
                delay = remaining;
        held->retry = (p32)(gone + delay);
}

/* A lease's default route. The router is on the leased prefix, or the lease
   is a /32 whose router is off it, which dhcp_lease_usable allows because
   the clouds hand them out: then the kernel is told the router is on the
   link, as dhclient's script and systemd-networkd do, since it refuses a
   gateway no address covers ("Nexthop has invalid gateway"), and that
   refusal rolled the whole lease back. */
static COLD bipolar net_lease_route(b32 handle, const dhcp_lease address_to lease,
                                    p32 index, bool exclusive)
{
        p32 mask = lease->mask ? lease->mask : 0xffffff00u;

        return netlink_route_change(
            handle, RTM_NEWROUTE,
            NLM_REQUEST | NLM_ACK | NLM_CREATE |
                (exclusive ? NLM_EXCLUSIVE : NLM_REPLACE),
            0, 0, lease->router, index,
            (lease->router & mask) != (lease->address & mask)
                ? NETLINK_ROUTE_ONLINK
                : 0);
}

static COLD fn net_rollback_record(bipolar status,
                              bipolar address_to first)
{
        if (!net_change_gone(status) && !*first)
                *first = status;
}

static COLD bipolar net_holding_release(b32 handle, net_holding address_to held)
{
        bipolar failed = 0;

        if (!held || !held->index)
                return 0;

        if (net_owns_route(held))
                net_rollback_record(
                    netlink_route_delete(handle, 0, 0, held->lease.router,
                                         held->index),
                    address_of failed);

        if (net_owns_address(held))
                net_rollback_record(
                    netlink_address_delete(handle, held->index,
                                           held->lease.address,
                                           dhcp_prefix_of(held->lease.mask)),
                    address_of failed);

        /* Once the address is no longer ours, a DHCP-provided resolver is no
           longer ours either.  Keep the always-available fallback as the
           complete resolver file, using the same checked atomic publication
           path as lease installation. */
        {
                bipolar status = net_write_resolv(0);

                if (status < 0 && !failed)
                        failed = status;
        }

        if (!failed)
                memory_fill(held, 0, sizeof(*held));
        return failed;
}

/* Put the kernel back on the previous lease after any later step fails. The
   old address is restored before its route; a new route is removed before
   its address. Failures are remembered, but every independent cleanup is
   still attempted so one refusal cannot strand the rest. */
static COLD bipolar net_lease_rollback(
    b32 handle, const net_holding address_to previous,
    p32 index, const dhcp_lease address_to lease,
    bool address_changed, bool route_changed)
{
        bipolar failed = 0;

        if (address_changed && net_owns_address(previous))
        {
                bipolar status = netlink_address_lease(
                    handle, previous->index, previous->lease.address,
                    dhcp_prefix_of(previous->lease.mask),
                    previous->lease.seconds, false);
                if (status < 0 && !failed)
                        failed = status;
        }

        if (route_changed)
        {
                bool same = false;

                if (net_owns_route(previous))
                {
                        bipolar status = net_lease_route(
                            handle, address_of previous->lease,
                            previous->index, false);
                        if (status < 0 && !failed)
                                failed = status;
                        same = lease->router == previous->lease.router &&
                               index == previous->index;
                }

                //      The new route goes unless restoring the old one
                //      already put back that very route.
                if (lease->router && !same)
                        net_rollback_record(
                            netlink_route_delete(handle, 0, 0,
                                                 lease->router, index),
                            address_of failed);
        }

        /* address_changed includes a prefix-only replacement.  The newly
           installed address is therefore always distinct from the previous
           kernel object and must be removed on rollback. */
        if (address_changed)
                net_rollback_record(
                    netlink_address_delete(handle, index, lease->address,
                                           dhcp_prefix_of(lease->mask)),
                    address_of failed);

        return failed;
}

static COLD b32 net_apply_lease(b32 handle, p32 index, string_address name,
                           p8 address_to hardware,
                           const dhcp_lease address_to lease,
                           net_holding address_to held, bool announce)
{
        net_holding previous_value = {0};
        const net_holding address_to previous =
            held && held->index ? address_of previous_value : null;
        bool address_changed;
        bool route_changed;
        bool address_applied = false;
        bool route_applied = false;
        string_address doing = null;
        bipolar status = 0;
        p8 written[32];

        if (previous)
                previous_value = *held;

        address_changed = !net_holds_address(previous, index, lease);
        route_changed = !net_holds_route(previous, index, lease);

        /* A watcher's address carries the lease's lifetimes, so the kernel
           drops it at expiry even with no watcher left, and a renewal of
           the same address puts the new lease's on it; a one-shot ip auto,
           which nothing renews, installs it as it always did. EEXIST from
           the exclusive create is the address already there: an operator's
           stays theirs, present and not owned, but one this client leased
           -- a watcher's before init restarted this one -- is taken back,
           so that a NAK or expiry removes it as it would have. A watcher's
           first lease also removes any other lease left on the link: the
           server that handed out a new address may hand the old one to
           somebody else. */
        if (address_changed || net_owns_address(previous))
        {
                p8 prefix = dhcp_prefix_of(lease->mask);
                p32 seconds = held ? lease->seconds : 0;
                netlink_lease_search found = {.index = index,
                                              .host = lease->address,
                                              .prefix = prefix};

                status = netlink_address_lease(
                    handle, index, lease->address, prefix, seconds,
                    !net_owns_address(previous));
                if (held && !previous &&
                    (status >= 0 || status == -EEXIST) &&
                    netlink_leases_on(handle, address_of found) >= 0)
                {
                        for (positive at = 0; at < found.others; at++)
                                (void)netlink_address_delete(
                                    handle, index, found.other_host[at],
                                    found.other_prefix[at]);
                        if (status == -EEXIST && found.leased)
                                status = netlink_address_lease(
                                    handle, index, lease->address, prefix,
                                    seconds, false);
                }
                address_applied = address_changed && status >= 0;
                if (status < 0 && status != -EEXIST)
                {
                        doing = (string_address) "addr add";
                        goto failed;
                }
        }

        if (route_changed && lease->router)
        {
                status = net_lease_route(handle, lease, index,
                                         !net_owns_route(previous));
                route_applied = status >= 0;
                if (status < 0 && status != -EEXIST)
                {
                        doing = (string_address) "route add";
                        goto failed;
                }
        }

        if (route_changed && net_owns_route(previous))
        {
                status = netlink_route_delete(handle, 0, 0,
                                              previous->lease.router,
                                              previous->index);
                if (!net_change_gone(status))
                {
                        doing = (string_address) "old route delete";
                        goto failed;
                }
                route_applied = true;
        }

        /* A prefix is part of an address object's identity.  Since
           address_changed compares index, host and prefix, every previous
           object in this branch must be removed, including an otherwise
           identical address whose mask changed. */
        if (address_changed && net_owns_address(previous))
        {
                status = netlink_address_delete(
                    handle, previous->index, previous->lease.address,
                    dhcp_prefix_of(previous->lease.mask));
                if (!net_change_gone(status))
                {
                        doing = (string_address) "old addr delete";
                        goto failed;
                }
        }

        status = net_write_resolv(lease->nameserver);
        if (status < 0)
        {
                doing = (string_address) "write resolv.conf";
                goto failed;
        }

        if (announce)
        {
                string_format(net_out, "ip: %s/%p on %w\n", net_host_text(written, lease->address),
                              (positive)dhcp_prefix_of(lease->mask), writer_terminal_name, name);

                if (lease->router)
                        string_format(net_out, "ip: default via %s\n",
                                      net_host_text(written, lease->router));

                string_format(net_out, "ip: nameserver %s",
                              net_host_text(written, lease->nameserver
                                                         ? lease->nameserver
                                                         : DNS_FALLBACK));

                if (lease->nameserver && lease->nameserver != DNS_FALLBACK)
                        string_format(net_out, ", then %s",
                                      net_host_text(written, DNS_FALLBACK));

                string_format(net_out, "\n");
        }

        if (held)
        {
                net_holding next = {
                    .index = index,
                    .lease = *lease,
                    .taken = net_seconds(),
                    .retry = lease->renewal,
                    .address_owned = net_ownership_next(
                        net_owns_address(previous), address_changed,
                        address_applied, lease->address != 0),
                    .route_owned = net_ownership_next(
                        net_owns_route(previous), route_changed,
                        route_applied, lease->router != 0),
                    .carrier_counted = previous && previous->carrier_counted,
                    .carrier_downs = previous ? previous->carrier_downs : 0,
                };
                string_copy_max_end(next.name, name, IFNAME_SIZE - 1);
                memory_copy(next.hardware, hardware, 6);
                *held = next;
        }

        net_flush();
        return 0;

failed:
        {
                bipolar rollback = net_lease_rollback(
                    handle, previous, index, lease,
                    address_applied, route_applied);

                if (rollback < 0)
                {
                        if (held)
                                held->lost = true;
                        net_refused((string_address) "lease rollback",
                                    rollback);
                }
        }

        return net_refused(doing, status);
}

/* Whether `moonwater wired off` was said: no wired link is then one a lease
   is asked on, and the walk goes to whatever else has carrier. */
static COLD bool net_wired_off(void)
{
        p8 text[8];
        bipolar got = file_slurp_once_at(AT_FDCWD, NET_WIRED_POWER, text, sizeof(text));

        while (got > 0 && (text[got - 1] == '\n' || text[got - 1] == ' '))
                got--;
        if (got < 0)
                return false;
        text[got] = end;
        return string_equals(text, "off");
}

static COLD p8 net_internet_prefer(void)
{
        p8 text[16];
        bipolar got = file_slurp_once_at(AT_FDCWD, NET_INTERNET_RUN, text,
                                         sizeof(text));

        if (got < 0)
                got = file_slurp_once_at(AT_FDCWD, NET_INTERNET_ROOT, text,
                                         sizeof(text));
        if (got < 0)
                return NETLINK_PREFER_WIRED;

        while (got > 0 && (text[got - 1] == '\n' || text[got - 1] == ' '))
                got--;
        text[got] = end;
        return string_equals(text, "wifi") ? NETLINK_PREFER_WIFI
                                           : NETLINK_PREFER_WIRED;
}

/*
        A lease exchange, apart from the process that applies it.

        The child confines itself once its socket is open (dhcp_confine) and
        hands back one fixed-size answer. That answer is judged here the way
        a reply is: a usable lease with its timers in RFC 2131's order, and on
        renewal the address already held -- the child can say only what a
        server could have. A child that says nothing within the exchange's
        own schedule is killed rather than waited for.
*/
typedef struct
{
        bipolar status;
        dhcp_lease lease;
} net_dhcp_answer;

/*
        The watcher's exchange is not allowed to be deaf.

        dhcp_ask is a twenty-attempt schedule, about seventy-six seconds when
        nobody answers, and the watcher sat in it: a boot whose first pass
        found a wired port with no cable, or a wifi station not yet joined,
        heard nothing of the join, the cable, or any moonwater command until
        the schedule ran out, so the internet came up at exactly seventy-six
        seconds whatever the machine had ready sooner. While the watcher runs,
        an acquisition therefore waits on the exchange, the wake pipe and the
        link news together, gives a link twelve seconds to answer, and is cut
        the moment either of the others has something to say; the loop reads
        that at once and asks again.
*/
static bipolar net_wake_watch = -1;
static bipolar net_events_watch = -1;
static bool net_exchange_cut;

#define NET_DHCP_WATCH_SECONDS 12
#define NET_DHCP_TIMED_OUT (-10)
#define NET_DHCP_INTERRUPTED (-11)

/*
        Link news may cut an exchange, but not again at once.

        Every carrier change on any link is news while no lease is held, so
        a second port whose cable flaps (or a radio whose association an
        attacker keeps dropping) cut each exchange as it started, and a
        server whose round trip was longer than the flap's period never got
        to finish one: the good link's lease waited for the flapping to
        stop. After a cut the link news is left unread for
        NET_NEWS_HOLDOFF_SECONDS, which is longer than any round trip worth
        waiting for, and read when it ends. The wake pipe is the operator's
        and always cuts.
*/
#define NET_NEWS_HOLDOFF_SECONDS 4
static positive net_news_holdoff_until;

static bool net_news_may_cut(positive now)
{
        return now >= net_news_holdoff_until;
}

//      An exchange the wake pipe or the link news cut: the loop asks again
//      at once, and the news is left unread for the hold-off.
static COLD bipolar net_exchange_was_cut(void)
{
        net_exchange_cut = true;
        net_news_holdoff_until = net_seconds() + NET_NEWS_HOLDOFF_SECONDS;
        return NET_DHCP_INTERRUPTED;
}

/* 1 when the exchange spoke, 0 when the budget ran out, -2 when the wake
   pipe or the link news did, negative for a broken descriptor. */
static COLD bipolar net_exchange_wait(bipolar answer,
                                      const network_deadline address_to deadline)
{
        for (;;)
        {
                positive seconds;
                positive nanoseconds;
                timespec limit;
                system_poll_descriptor waited[3];
                positive count = 1;
                positive now = net_seconds();
                bool news = net_events_watch >= 0 && net_news_may_cut(now);
                bipolar ready;

                if (!network_deadline_left(deadline, address_of seconds,
                                           address_of nanoseconds))
                        return 0;
                //      While the news is held off, wake up when it ends.
                if (net_events_watch >= 0 && !news &&
                    seconds >= net_news_holdoff_until - now)
                {
                        seconds = net_news_holdoff_until - now;
                        nanoseconds = 0;
                }
                limit.tv_sec = (b64)seconds;
                limit.tv_nsec = (b64)nanoseconds;
                waited[0].descriptor = (b32)answer;
                waited[0].events = SYSTEM_POLL_READ;
                waited[0].returned = 0;
                if (net_wake_watch >= 0)
                {
                        waited[count].descriptor = (b32)net_wake_watch;
                        waited[count].events = SYSTEM_POLL_READ;
                        waited[count++].returned = 0;
                }
                if (news)
                {
                        waited[count].descriptor = (b32)net_events_watch;
                        waited[count].events = SYSTEM_POLL_READ;
                        waited[count++].returned = 0;
                }
                ready = system_poll_wait(waited, count, address_of limit, null);
                if (ready == NETWORK_INTERRUPTED)
                        continue;
                if (ready == 0 && !news)
                        continue;
                if (ready <= 0)
                        return ready;
                if (waited[0].returned & SYSTEM_POLL_INVALID)
                        return -9;
                if (waited[0].returned)
                        return 1;
                return -2;
        }
}

/* What the child is asked to do: discover, renew with the server that gave
   the lease, rebind with any, or decline the lease it names. */
#define NET_DHCP_DISCOVER 0
#define NET_DHCP_RENEW 1
#define NET_DHCP_REBIND 2
#define NET_DHCP_DECLINE 3

static COLD bipolar net_dhcp_apart(string_address device, p8 address_to hardware,
                                   dhcp_lease address_to lease, p8 exchange,
                                   positive wait)
{
        b32 ends[2];
        net_dhcp_answer answer = {DHCP_NO_SOCKET, *lease};
        network_deadline deadline;
        bool renew = exchange == NET_DHCP_RENEW || exchange == NET_DHCP_REBIND;
        bool heard;

        if (system_call_2(syscall(pipe2), (positive)ends, O_CLOEXEC) < 0)
                return DHCP_NO_SOCKET;

        bipolar child = system_fork();

        if (child == 0)
        {
                dhcp_apart = ends[1];
                answer.status =
                    renew ? dhcp_reacquire(device, hardware,
                                           address_of answer.lease,
                                           exchange == NET_DHCP_REBIND, wait)
                    : exchange == NET_DHCP_DECLINE
                        ? dhcp_decline(device, hardware, address_of answer.lease)
                        : dhcp_ask(device, hardware, address_of answer.lease);
                system_write_all((positive)dhcp_apart, address_of answer,
                                 sizeof answer);
                system_call_1(syscall(exit_group), 0);
        }

        system_close(ends[1]);
        //      dhcp_ask's twenty attempts can each wait out a DISCOVER
        //      and a REQUEST, 150 s in all, after a CSPRNG that may take
        //      a second; a renewal is its own wait, and a DECLINE is one
        //      send after the same CSPRNG.
        bool watching = exchange == NET_DHCP_DISCOVER && net_wake_watch >= 0;
        bipolar waited = -1;

        heard = child > 0 &&
                network_deadline_begin(address_of deadline,
                                       renew ? wait + 5
                                       : exchange == NET_DHCP_DECLINE ? 5
                                       : watching ? NET_DHCP_WATCH_SECONDS
                                                  : 180, 0) &&
                (watching
                     ? (waited = net_exchange_wait(ends[0], address_of deadline))
                     : network_wait_readable_until(ends[0], address_of deadline)) > 0 &&
                system_read_retry((positive)ends[0], address_of answer,
                                  sizeof answer) == (bipolar)sizeof answer;
        if (child > 0)
        {
                system_call_2(syscall(kill), (positive)child, SIGKILL);
                system_call_4(syscall(wait4), (positive)child, 0, 0, 0);
        }
        system_close(ends[0]);

        if (!heard)
        {
                if (watching && waited == -2)
                        return net_exchange_was_cut();
                return watching && waited == 0 ? NET_DHCP_TIMED_OUT
                                               : DHCP_NO_SOCKET;
        }
        if (exchange == NET_DHCP_DECLINE)
                return answer.status == DHCP_OK ? DHCP_OK : DHCP_NO_SOCKET;
        if (answer.status != DHCP_OK)
                return answer.status == DHCP_NO_OFFER ||
                               answer.status == DHCP_REFUSED ||
                               answer.status == DHCP_NO_RANDOM
                           ? answer.status
                           : DHCP_NO_SOCKET;
        if (!dhcp_lease_usable(address_of answer.lease) ||
            !dhcp_lease_timers(address_of answer.lease) ||
            (renew && answer.lease.address != lease->address))
                return DHCP_NO_OFFER;
        *lease = answer.lease;
        return DHCP_OK;
}

static COLD fn radio_links_unleased(netlink_search address_to search);

/*
        A link that declined an address waits before it asks again.

        RFC 2131 4.4.1: after a DHCPDECLINE the client waits at least ten
        seconds before it starts over, so that a server with nothing else to
        offer, or a station that answers for every address, does not turn
        the link into a DISCOVER, REQUEST and probe as fast as the watcher
        can loop. The wait is the declined link's: the watcher still asks on
        any other link, and link news and the wake pipe do not end it. A
        handful of links is kept; one more takes the slot whose wait is
        nearest its end.
*/
#define NET_DECLINE_HOLDOFF_SECONDS 10
#define NET_DECLINE_LINKS 4

static struct
{
        p32 index;
        network_deadline until;
} net_declined[NET_DECLINE_LINKS];

/* The milliseconds left of a link's wait (of the wait nearest its end when
   index is zero), and 0 when there is none. */
static COLD positive net_decline_left(p32 index)
{
        positive least = 0;

        for (positive at = 0; at < NET_DECLINE_LINKS; at++)
        {
                positive seconds;
                positive nanoseconds;
                positive left;

                if (!net_declined[at].index ||
                    (index && net_declined[at].index != index))
                        continue;
                if (!network_deadline_left(address_of net_declined[at].until,
                                           address_of seconds,
                                           address_of nanoseconds))
                {
                        net_declined[at].index = 0;
                        continue;
                }
                left = seconds * 1000 + nanoseconds / 1000000 + 1;
                if (!least || left < least)
                        least = left;
        }
        return least;
}

static COLD bool net_decline_waiting(p32 index)
{
        return index && net_decline_left(index) != 0;
}

static COLD fn net_decline_hold(p32 index)
{
        positive slot = NET_DECLINE_LINKS;

        for (positive at = 0; at < NET_DECLINE_LINKS; at++)
                if (net_declined[at].index == index)
                        slot = at;
        for (positive at = 0; slot == NET_DECLINE_LINKS && at < NET_DECLINE_LINKS;
             at++)
                if (!net_declined[at].index)
                        slot = at;
        //      Every slot taken: the wait that began first ends first.
        if (slot == NET_DECLINE_LINKS)
        {
                slot = 0;
                for (positive at = 1; at < NET_DECLINE_LINKS; at++)
                        if (net_declined[at].until.began <
                            net_declined[slot].until.began)
                                slot = at;
        }
        net_declined[slot].index =
            network_deadline_begin(address_of net_declined[slot].until,
                                   NET_DECLINE_HOLDOFF_SECONDS, 0)
                ? index
                : 0;
}

/* RFC 5227-style probes before installing a newly leased address.  DHCP is
   not proof that the address is unused: a stale lease, a broken server, or a
   hostile responder can hand out an address already active on the link. */
#define NET_ARP_PACKET 28
#define NET_ARP_PROBES 3

/* An ARP packet (cooked: the link header is the kernel's) that says another
   station holds the address: it sends from it, or probes for it as this one
   does (RFC 5227 2.1.1). Ethernet's frame minimum pads the 28 bytes, so a
   longer packet is the same packet. */
static bool net_arp_claims(const p8 address_to packet, positive size,
                           const p8 address_to hardware, p32 address)
{
        byte_reader reader = byte_reader_open(packet, size);
        p16 medium = byte_reader_u16(address_of reader);
        p16 protocol = byte_reader_u16(address_of reader);
        p8 hardware_length = byte_reader_u8(address_of reader);
        p8 protocol_length = byte_reader_u8(address_of reader);
        p16 operation = byte_reader_u16(address_of reader);
        const p8 address_to sender = byte_reader_take(address_of reader, 6);
        p32 sender_address = byte_reader_u32(address_of reader);
        p32 target_address = byte_reader_skip(address_of reader, 6)
                                 ? byte_reader_u32(address_of reader)
                                 : 0;

        return byte_reader_ok(address_of reader) && medium == 1 &&
               protocol == ETH_P_IP && hardware_length == 6 &&
               protocol_length == 4 && (operation == 1 || operation == 2) &&
               (sender_address == address ||
                (!sender_address && target_address == address)) &&
               memory_compare(sender, hardware, 6);
}

/* A packet socket of one ethertype on one interface: the cooked link header
   is the kernel's, the payload is ours. */
static COLD bipolar net_packet_open(p32 index, p16 protocol)
{
        socket_address_packet self = {
            .family = AF_PACKET,
            .protocol = network_order_16(protocol),
            .index = index};
        bipolar handle = socket_new(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC,
                                    (b32)network_order_16(protocol));

        if (handle >= 0 &&
            socket_bind((b32)handle, address_of self, sizeof self) < 0)
        {
                socket_close((b32)handle);
                return -1;
        }
        return handle;
}

/* 0 means quiet, 1 means another station claims the address, -1 means the
   probe could not be performed, and -2 that the wake pipe or the link news
   cut it, as they cut an exchange (net_exchange_wait): a cable pulled during
   the probe is not a quiet link.  Failure is not permission to configure an
   address whose ownership was never checked. */
static COLD bipolar net_arp_conflict(p32 index, p8 address_to hardware,
                                     p32 address)
{
        p8 probe[NET_ARP_PACKET] = {0};
        p8 reply[64];
        socket_address_packet all = {
            .family = AF_PACKET,
            .protocol = network_order_16(ETH_P_ARP),
            .index = index,
            .halen = 6};
        bipolar handle = net_packet_open(index, ETH_P_ARP);

        if (handle < 0)
                return -1;
        memory_fill(all.addr, 0xff, 6);
        network_store_16(probe, 1);
        network_store_16(probe + 2, ETH_P_IP);
        probe[4] = 6;
        probe[5] = 4;
        network_store_16(probe + 6, 1);
        memory_copy(probe + 8, hardware, 6);
        network_store_32(probe + 24, address);

        for (positive attempt = 0; attempt < NET_ARP_PROBES; attempt++)
        {
                network_deadline deadline;

                if (socket_send((b32)handle, probe, sizeof probe, 0,
                                address_of all, sizeof all) != sizeof probe ||
                    !network_deadline_begin(address_of deadline, 0, 200000000))
                        goto failed;
                for (;;)
                {
                        bipolar ready = net_exchange_wait(
                            handle, address_of deadline);
                        bipolar got;

                        if (ready == -2)
                        {
                                socket_close((b32)handle);
                                return -2;
                        }
                        if (ready < 0)
                                goto failed;
                        if (!ready)
                                break;
                        got = socket_receive((b32)handle, reply, sizeof reply,
                                             0, 0, 0);
                        if (got == NETWORK_INTERRUPTED)
                                continue;
                        if (got < 0)
                                goto failed;
                        if (net_arp_claims(reply, (positive)got, hardware,
                                           address))
                        {
                                socket_close((b32)handle);
                                return 1;
                        }
                }
        }
        socket_close((b32)handle);
        return 0;

failed:
        socket_close((b32)handle);
        return -1;
}

static COLD b32 net_auto(b32 handle, net_holding address_to held)
{
        netlink_search search;
        dhcp_lease lease;
        bipolar status;
        positive tried;

        memory_fill(address_of search, 0, sizeof search);
        search.skip_loopback = true;
        search.prefer = net_internet_prefer();
        search.skip_wired = net_wired_off();
        radio_links_unleased(address_of search);

        /*
                The best link first, and when it stays silent the next one.

                A wired port with no cable, or one whose switch never answers,
                is the best link by preference and a dead end: without this
                the wifi that had joined was never asked while the port kept
                its place. Only the watcher does it (held is its state); `ip
                auto` asks the one link, as it always has. Each failed link is
                added to the skip list for the rest of this pass, and the
                next pass starts over with all of them.
        */
        for (tried = 0;; tried++)
        {
                if (netlink_link_find(handle, address_of search) < 0)
                {
                        if (tried)
                                return 1;
                        string_format(net_out, "ip: no interface to configure\n");
                        net_flush();
                        return 1;
                }

                if (held && held->index == search.index && !held->lost)
                        return 0;

                if (held && net_decline_waiting(search.index))
                {
                        if (search.skip_count >= 8)
                                return 1;
                        search.skip[search.skip_count++] = search.index;
                        continue;
                }

                if (!search.has_hardware)
                {
                        string_format(net_out, "ip: %w has no hardware address\n",
                                      writer_terminal_quoted_name, search.name);
                        net_flush();
                        return 1;
                }

                string_format(net_out, "ip: using %w\n", writer_terminal_quoted_name,
                              search.name);

                if (!(search.flags & IFF_UP))
                {
                        status = netlink_link_up(handle, search.index);

                        if (status < 0)
                                return net_refused((string_address) "link up", status);
                }

                string_format(net_out, "ip: asking for a lease\n");
                net_flush();

                status = net_dhcp_apart(search.name, search.hardware,
                                        address_of lease, NET_DHCP_DISCOVER, 0);

                if (status == NET_DHCP_INTERRUPTED)
                        return 1;
                if (status == NET_DHCP_TIMED_OUT && held && tried < 3 &&
                    search.skip_count < 8)
                {
                        string_format(net_out, "ip: nobody answered on %w\n",
                                      writer_terminal_quoted_name, search.name);
                        net_flush();
                        search.skip[search.skip_count++] = search.index;
                        continue;
                }
                if (status == NET_DHCP_TIMED_OUT)
                        status = DHCP_NO_OFFER;
                break;
        }

        if (status != DHCP_OK)
        {
                if (status == DHCP_REFUSED)
                        string_format(net_out, "ip: the server refused the request\n");
                else if (status == DHCP_NO_OFFER)
                        string_format(net_out, "ip: nobody offered a lease\n");
                else if (status == DHCP_NO_RANDOM)
                        string_format(net_out,
                                      "ip: kernel randomness is unavailable\n");
                else
                        string_format(net_out, "ip: could not ask for a lease\n");

                net_flush();
                return 1;
        }

        status = net_arp_conflict(search.index, search.hardware, lease.address);
        if (status == -2)
        {
                (void)net_exchange_was_cut();
                return 1;
        }
        if (status > 0)
        {
                bipolar declined = net_dhcp_apart(search.name, search.hardware,
                                                  address_of lease,
                                                  NET_DHCP_DECLINE, 0);

                if (held)
                        net_decline_hold(search.index);
                string_format(net_out,
                              declined == DHCP_OK
                                  ? "ip: the offered address is already in use; declined\n"
                                  : "ip: the offered address is already in use\n");
                net_flush();
                return 1;
        }
        if (status)
        {
                string_format(net_out, "ip: could not check the offered address\n");
                net_flush();
                return 1;
        }

        if (net_apply_lease(handle, search.index, search.name,
                            search.hardware, address_of lease, held, true))
                return 1;
        /* Counted once the lease is taken, not before the exchange:
           bringing a link up can itself cost a carrier loss -- a PHY that
           starts by dropping the carrier it was registered with -- which
           is no reason to doubt a lease taken after it. */
        if (held)
        {
                netlink_search now = {.wanted = (string_address)search.name};

                if (netlink_link_find(handle, address_of now) >= 0 &&
                    now.index == search.index)
                {
                        held->carrier_counted = now.carrier_counted;
                        held->carrier_downs = now.carrier_downs;
                }
        }
        return 0;
}

static COLD b32 net_reconfigure(b32 handle, net_holding address_to held)
{
        if (held && held->lost)
        {
                bipolar status = net_holding_release(handle, held);

                if (status < 0)
                        return net_refused((string_address) "lease release",
                                           status);
        }

        return net_auto(handle, held);
}


/*
        ip watch -- configure now, and again whenever the wires change.

        The kernel will tell you when a link gains or loses carrier if you ask
        it to: a netlink socket bound to the RTNLGRP_LINK multicast group
        receives an RTM_NEWLINK every time an interface changes state. A
        machine that boots with a cable in is configured before the first
        event, and a cable that moves is followed without anybody typing.

        What counts as "something" is deliberately narrow. An RTM_NEWLINK
        arrives for changes nobody cares about here, so only a change in
        IFF_RUNNING on a link that is not loopback causes anything: that is
        the kernel saying a cable was plugged in or pulled out. Everything
        else is read and dropped.

        On such a change the whole of ip auto runs again, which re-picks the
        best link rather than assuming the one that changed is the one to use.
        Pull the cable, the wired link loses carrier, the walk picks whatever
        else has it. Plug a cable in while wifi is up, and prefer (wired by
        default, or wifi if `moonwater priority internet wifi` said so) is
        what breaks the tie. A second live link is therefore worth a look,
        not ignored because a lease is already held.

        Preference can change without a carrier event, so the watcher also
        reads /run/moonwater/net.wake. moonwater writes a byte there after
        it changes the radio or the preference file. If the fifo is missing
        at start, the watcher opens it again on the next idle pass.

        With no lease, idle is a few seconds and grows to half a minute, so a
        DHCP server that comes back, or a wireless interface that appears
        after firmware, is configured without waiting for a cable event.

        Re-running is safe to do at any time. Adding an address uses REPLACE
        and adding a route is idempotent, so a spurious run costs a DHCP
        exchange and changes nothing else. If the preferred link is already
        the one with the lease, auto does not ask again.
*/
typedef struct
{
        p32 index;
        p32 flags;
} net_state;

static netlink_buffer net_states;

static COLD net_state address_to net_state_of(p32 index)
{
        net_state address_to states = (net_state address_to)net_states.bytes;

        for (positive at = 0; at < net_states.used / sizeof(net_state); at++)
                if (states[at].index == index)
                        return states + at;
        return null;
}

/* Whether the held link lost its carrier since the lease was asked for, by
   the kernel's count: a cable pulled and put back while the watcher was
   busy -- into another network, perhaps -- leaves the link looking as it
   did, and only the count says otherwise. Only a count that went up does;
   news queued from before the lease carries an older one. */
static COLD bool net_link_bounced(const net_holding address_to held,
                                  bool counted, p32 downs)
{
        return held->carrier_counted && counted &&
               (b32)(downs - held->carrier_downs) > 0;
}

/* News without IFF_RUNNING is also what the kernel sends the moment a link
   is brought up; linkwatch says RUNNING up to a second later, and the lease
   is taken between the two, so every boot released a lease a millisecond
   old and asked again. The held link is asked as it is now: carrier
   (IFF_LOWER_UP) counts, and so does a carrier lost and found since. */
static COLD bool net_link_carrier_kept(const net_holding address_to held)
{
        netlink_search now = {.wanted = (string_address)held->name};
        bipolar handle = netlink_open_groups(0);
        bool carrier = handle >= 0 &&
                       netlink_link_find((b32)handle, address_of now) >= 0 &&
                       now.index == held->index &&
                       (now.flags & (IFF_RUNNING | IFF_LOWER_UP)) &&
                       !net_link_bounced(held, now.carrier_counted,
                                         now.carrier_downs);

        if (handle >= 0)
                socket_close((b32)handle);
        return carrier;
}

/* Remember every carrier transition, and reconfigure when no lease is
   active, when its interface loses carrier, or when another link gains it:
   the preference may favour that one, and auto keeps the lease when the
   preferred link is the one holding it. A newly probed down link is
   actionable while unconfigured. */
static COLD bool net_link_news(p32 index, p32 flags, net_holding address_to held)
{
        net_state address_to entry = net_state_of(index);

        if (entry && ((entry->flags ^ flags) & IFF_RUNNING) == 0)
                return false;

        if (!entry)
        {
                if (net_states.used > positive_max - sizeof(net_state) ||
                    !net_room(address_of net_states,
                              net_states.used + sizeof(net_state)))
                        return false;
                entry = (net_state address_to)(net_states.bytes +
                                               net_states.used);
                entry->index = index;
                net_states.used += sizeof(net_state);
        }
        entry->flags = flags;

        if (held && held->index == index && !(flags & IFF_RUNNING) &&
            !net_link_carrier_kept(held))
                held->lost = true;
        if (!held || held->index == 0 || held->lost)
                return true;
        return (flags & IFF_RUNNING) != 0 && index != held->index;
}

static COLD bool net_link_removed(p32 index, net_holding address_to held)
{
        net_state address_to state = net_state_of(index);

        /* Forget the carrier snapshot as well as the lease.  Interface
           indexes may be reused, and retaining the deleted device's flags
           could suppress the replacement device's first event. */
        if (state)
        {
                net_states.used -= sizeof(net_state);
                *state = *(net_state address_to)(net_states.bytes +
                                                 net_states.used);
        }

        if (!held || held->index != index)
                return false;
        held->lost = true;
        return true;
}

/* Link multicast records are untrusted variable-length netlink messages.
   DELLINK needs only the fixed interface index; it must not consult flags or
   optional attributes from a device which no longer exists. */
static COLD bool net_link_event(netlink_header address_to header,
                           net_holding address_to held)
{
        netlink_link address_to link =
            header ? netlink_message_body(header, sizeof(netlink_link)) : null;

        if (!link || header->port)
                return false;
        if (header->type == RTM_DELLINK)
                return net_link_removed(link->index, held);
        if (header->type != RTM_NEWLINK || (link->flags & IFF_LOOPBACK))
                return false;
        {
                p32 downs = 0;
                bool counted = netlink_link_carrier_downs(header,
                                                          address_of downs);

                if (held && held->index == link->index && !held->lost &&
                    net_link_bounced(held, counted, downs))
                {
                        (void)net_link_news(link->index, link->flags, held);
                        held->lost = true;
                        return true;
                }
        }
        return net_link_news(link->index, link->flags, held);
}

//      Discovery again, on a routing socket of its own, if one will open.
static COLD fn net_reconfigure_fresh(net_holding address_to held)
{
        bipolar handle = netlink_open_groups(0);

        if (handle >= 0)
        {
                net_reconfigure((b32)handle, held);
                socket_close((b32)handle);
        }
}

static COLD bipolar net_wake_listen(void)
{
        system_make_directory_at(AT_FDCWD, "/run", 0755);
        system_make_directory_at(AT_FDCWD, NET_STATE_DIR, 0755);
        system_call_4(syscall(mknodat), AT_FDCWD,
                      (positive)(string_address)NET_WAKE_PATH,
                      S_IFIFO | 0600, 0);
        // The FIFO itself, never what a link standing there names.
        bipolar handle = system_open_at(AT_FDCWD, NET_WAKE_PATH,
                                        FILE_READ_WRITE | O_NONBLOCK |
                                            O_NOFOLLOW | O_CLOEXEC);
        if (handle >= 0 && !file_handle_is_pipe(handle))
        {
                system_close((positive)handle);
                return -ERROR_INVALID;
        }
        return handle;
}

static COLD fn net_wake_drain(b32 handle)
{
        p8 sink[64];
        bipolar got;

        for (;;)
        {
                got = system_call_3(syscall(read), (positive)handle,
                                    (positive)sink, sizeof(sink));
                if (got <= 0)
                        return;
        }
}

/* One datagram of link news, acted on: 1 when it reconfigured, 0 when
   nothing in it mattered, negative when the socket failed for good. News
   that came faster than it was read is dropped by the kernel with ENOBUFS,
   which is not the end of the socket: what it would have said is asked
   instead. Every carrier snapshot is forgotten, so each link's next news
   counts, the held link is asked whether it kept its carrier since the
   lease, and the best link is picked again. */
static COLD bipolar net_watch_events(b32 events, netlink_buffer address_to message,
                                     net_holding address_to held)
{
        bipolar got = netlink_receive(events, message, null);
        positive at = 0;
        bipolar acted = 0;

        /* recvfrom can still be interrupted in the narrow interval after the
           readiness poll.  Nothing was consumed, and the lease deadline is
           recomputed by the caller. */
        if (got == NETWORK_INTERRUPTED)
                return 0;
        if (got == -ENOBUFS)
        {
                netlink_forget(address_of net_states);
                if (held->index && !held->lost && !net_link_carrier_kept(held))
                        held->lost = true;
                net_reconfigure_fresh(held);
                return 1;
        }
        if (got < 0)
                return got;

        while (at + NETLINK_HEADER <= message->used)
        {
                netlink_header address_to header =
                    (netlink_header address_to)(message->bytes + at);

                if (header->length < NETLINK_HEADER ||
                    at + header->length > message->used)
                        break;
                at += netlink_align(header->length);
                if (net_link_event(header, held))
                {
                        acted = 1;
                        net_reconfigure_fresh(held);
                }
        }
        return acted;
}

/*
        What the image asks of the kernel's network stack, once, when the
        watcher starts (before any link is brought up, so before the first
        frame of a lease).

        Nothing else in the image writes /proc/sys, so a machine ran on
        Linux's own defaults: ICMP redirects accepted from whoever sits on the
        link (a host that does not forward takes one when EITHER the interface
        or `all` allows it, so both are written, and `default` reaches every
        interface not set by hand, wlan0 as it appears included), the
        per-interface source-route switch on for new interfaces, TIME-WAIT
        assassination by a forged RST allowed, and IPv6 router advertisements
        and SLAAC on although nothing here speaks IPv6 (waterlink's dual-stack
        sockets and link-local addresses stay: only what a neighbour can push
        at the machine is refused). IPv6 has no copy from `default` to the
        interfaces already there, so each directory under net/ipv6/conf is
        written.

        The reference tier (STRICT_REFERENCE) keeps the kernel's own values,
        the rest write the table below. rp_filter is left alone on purpose: the
        DHCP client is a UDP socket, its OFFER comes from an address with no
        route yet, and with rp_filter 1 or 2 the OFFER is dropped and no lease
        is ever taken (measured: OFFERs sent, no REQUEST, in a KVM guest booted
        with either).
*/
#if MOONWATER_STRICT >= STRICT_SAFE
static COLD fn net_sysctl(string_address path, string_address value)
{
        bipolar handle = system_open_at(AT_FDCWD, path, 1 | O_CLOEXEC);

        if (handle < 0)
                return;
        system_write_all((positive)handle, (p8 address_to)value,
                         string_length(value));
        system_close((positive)handle);
}

//      "directory/name/leaf" into `to`, cut short when it would not fit.
static COLD fn net_sysctl_path(p8 address_to to, positive size,
                               string_address directory, string_address name,
                               string_address leaf)
{
        string_address parts[3] = {directory, name, leaf};
        positive at = 0;

        for (positive part = 0; part < 3; part++)
        {
                positive length = string_length(parts[part]);

                if (!parts[part][0])
                        continue;
                if (at + length + 2 > size)
                {
                        to[0] = 0;
                        return;
                }
                if (at)
                        to[at++] = '/';
                memory_copy(to + at, parts[part], length);
                at += length;
        }
        to[at] = 0;
}

//      leaf, written in `all`, `default` and every interface the directory
//      lists, or only the first two for the IPv4 switches the kernel copies.
//      The listing is read to its end, however many interfaces there are:
//      one read of a small buffer left the later ones on the kernel's value.
static COLD fn net_sysctl_family(string_address directory, string_address leaf,
                                 string_address value, bool each)
{
        p8 path[128];
        static const string_address wide[] = {"all", "default", null};

        for (positive at = 0; wide[at]; at++)
        {
                net_sysctl_path(path, sizeof path, directory, wide[at], leaf);
                net_sysctl((string_address)path, value);
        }
        if (each)
        {
                p8 names[1024];
                struct linux_dirent64 address_to entry;
                positive have = 0;
                positive at = 0;
                bipolar error = 0;
                bipolar handle = system_open_at(AT_FDCWD, directory,
                                                O_DIRECTORY | O_CLOEXEC);

                if (handle < 0)
                        return;
                while ((entry = file_directory_next(handle, names, sizeof names,
                                                    address_of have,
                                                    address_of at,
                                                    address_of error)))
                {
                        string_address name = (string_address)entry->d_name;

                        if (name[0] == '.' || string_equals(name, "all") ||
                            string_equals(name, "default"))
                                continue;
                        net_sysctl_path(path, sizeof path, directory, name,
                                        leaf);
                        net_sysctl((string_address)path, value);
                }
                system_close((positive)handle);
        }
}

static COLD fn net_kernel_defaults(void)
{
        static const string_address quiet[] = {
            "accept_redirects", "secure_redirects", "accept_source_route",
            "send_redirects", null};
        static const string_address quiet_v6[] = {
            "accept_ra", "autoconf", "accept_redirects", null};

        for (positive at = 0; quiet[at]; at++)
                net_sysctl_family("/proc/sys/net/ipv4/conf", quiet[at], "0\n",
                                  false);
        for (positive at = 0; quiet_v6[at]; at++)
                net_sysctl_family("/proc/sys/net/ipv6/conf", quiet_v6[at],
                                  "0\n", true);
        net_sysctl("/proc/sys/net/ipv4/tcp_rfc1337", "1\n");
        net_sysctl("/proc/sys/net/ipv4/tcp_syncookies", "1\n");
}
#endif

#define NET_RESUME_LOOK_SECONDS 60

static COLD b32 net_watch(void)
{
        netlink_buffer message = {0};
        net_holding held;
        bipolar events;
        bipolar wake;
        bipolar handle;
        positive retry_seconds = 4;

        /* A watcher may return to the hosting shell after a descriptor
           failure and later be started again.  Its carrier snapshots belong
           to the former event stream; retaining them can suppress the
           replacement stream's first transition while no lease is held. */
        netlink_forget(address_of net_states);

        //      O_WRONLY. A failure leaves the handle at -1 and net_out at
        //      log, which is exactly the old behaviour.
        net_kmsg_handle = (b32)system_open_at(
            AT_FDCWD, "/dev/kmsg", 1 | O_CLOEXEC);

        if (net_kmsg_handle >= 0)
        {
                net_kmsg_begin();
                net_out = net_kmsg;
        }

        events = netlink_open_groups(RTNLGRP_LINK_MASK);

        if (events < 0)
        {
                string_format(net_out, "ip: %s\n", (string_address) "cannot listen for link changes");
                net_flush();
                net_kmsg_close();
                return 1;
        }

#if MOONWATER_STRICT >= STRICT_SAFE
        net_kernel_defaults();
#endif
        wake = net_wake_listen();
        net_wake_watch = wake;
        net_events_watch = events;
        net_exchange_cut = false;
        net_news_holdoff_until = 0;

        //      Configure whatever is already plugged in before waiting for
        //      anything to change, or a machine that boots with its cable in
        //      would wait forever for an event that already happened.
        memory_fill(address_of held, 0, sizeof held);
        net_reconfigure_fresh(address_of held);

        for (;;)
        {
                bipolar got;
                bool link_ready = false;
                bool woken = false;

                /*
                        Wait for a link to change, for moonwater to say the
                        preferred internet changed, or for the lease to reach
                        the point where it should be renewed, whichever comes
                        first. Without the second, a machine that nobody
                        touches keeps an address the server has long since
                        considered free, and the first sign of trouble is
                        somebody else being handed it.
                */
                {
                        positive due = 0;
                        bipolar ready;
                        timespec limit;
                        system_poll_descriptor waited[2];
                        positive count = 1;

                        positive declined = 0;
                        bool looking = false;

                        if (held.index && held.lease.seconds)
                        {
                                due = net_lease_due_in(address_of held,
                                                       net_seconds());
                                /* The wait below is the monotonic clock's
                                   and sleeps through a suspend, so a lease
                                   due in an hour is looked at every
                                   NET_RESUME_LOOK_SECONDS: a machine that
                                   woke past its lease's end finds out
                                   within that, not an hour of being awake
                                   later. */
                                if (due > NET_RESUME_LOOK_SECONDS)
                                {
                                        due = NET_RESUME_LOOK_SECONDS;
                                        looking = true;
                                }
                        }
                        else
                        {
                                due = retry_seconds;
                                declined = net_decline_left(0);
                        }

                        waited[0].descriptor = (b32)events;
                        waited[0].events = SYSTEM_POLL_READ;
                        waited[0].returned = 0;
                        if (wake >= 0)
                        {
                                waited[1].descriptor = (b32)wake;
                                waited[1].events = SYSTEM_POLL_READ;
                                waited[1].returned = 0;
                                count = 2;
                        }

                        limit.tv_sec = (b64)(due ? due : retry_seconds);
                        limit.tv_nsec = 0;
                        //      A declined link is asked when its wait ends,
                        //      not a backoff step later.
                        if (declined && declined < (positive)limit.tv_sec * 1000)
                        {
                                limit.tv_sec = (b64)(declined / 1000);
                                limit.tv_nsec = (b64)(declined % 1000 * 1000000);
                        }
                        ready = system_poll_wait(waited, count, address_of limit,
                                                 null);
                        if (ready < 0)
                        {
                                /* A signal does not turn the following
                                   receive into an unbounded wait: recompute
                                   the lease deadline and poll again. Other
                                   descriptor failures terminate the watcher. */
                                if (ready == NETWORK_INTERRUPTED)
                                        continue;
                                break;
                        }

                        if (ready > 0)
                        {
                                if (waited[0].returned & SYSTEM_POLL_INVALID)
                                        break;
                                link_ready = (waited[0].returned &
                                              SYSTEM_POLL_READ) != 0;
                                if (count == 2 &&
                                    (waited[1].returned & SYSTEM_POLL_READ))
                                {
                                        net_wake_drain((b32)wake);
                                        woken = true;
                                }
                        }

                        if (!ready)
                        {
                                if (wake < 0)
                                {
                                        wake = net_wake_listen();
                                        net_wake_watch = wake;
                                }

                                if (looking)
                                        continue;

                                if (!held.index || !held.lease.seconds)
                                {
                                        net_reconfigure_fresh(address_of held);
                                        if (declined)
                                                continue;
                                        if (!held.index || !held.lease.seconds)
                                        {
                                                retry_seconds *= 2;
                                                if (retry_seconds > 10)
                                                        retry_seconds = 10;
                                        }
                                        else
                                                retry_seconds = 4;
                                        continue;
                                }

                                /* A renewal timeout must not extend a lease.
                                   At the actual deadline first remove the old
                                   address, route and resolver, then discover
                                   from a clean state. */
                                if (net_lease_expired_at(address_of held,
                                                         net_seconds()))
                                {
                                        held.lost = true;
                                        net_reconfigure_fresh(address_of held);
                                        continue;
                                }

                                positive now = net_seconds();
                                bool rebinding = net_lease_rebinding_at(
                                    address_of held, now);
                                positive attempt = net_lease_attempt_time(
                                    address_of held, now, rebinding);
                                dhcp_lease renewed = held.lease;
                                bipolar renewal = net_dhcp_apart(
                                    held.name, held.hardware,
                                    address_of renewed,
                                    rebinding ? NET_DHCP_REBIND : NET_DHCP_RENEW,
                                    attempt);

                                if (renewal == DHCP_OK)
                                {
                                        bool applied = false;

                                        handle = netlink_open_groups(0);

                                        if (handle >= 0)
                                        {
                                                if (!net_apply_lease(
                                                        (b32)handle,
                                                        held.index, held.name,
                                                        held.hardware,
                                                        address_of renewed,
                                                        address_of held,
                                                        false))
                                                {
                                                        applied = true;
                                                        string_format(net_out, "ip: lease %s on %w\n",
                                                                      rebinding ? (string_address)"rebound"
                                                                                : (string_address)"renewed",
                                                                      writer_terminal_quoted_name,
                                                                      held.name);
                                                        net_flush();
                                                }
                                                socket_close((b32)handle);
                                        }
                                        if (applied)
                                                continue;
                                }

                                {
                                        positive now = net_seconds();

                                        if (renewal != DHCP_REFUSED &&
                                            !net_lease_expired_at(
                                                address_of held, now))
                                        {
                                                net_lease_retry_after(
                                                    address_of held, now,
                                                    rebinding);
                                                continue;
                                        }
                                        held.lost = true;
                                }

                                net_reconfigure_fresh(address_of held);
                                continue;
                        }
                }

                if (link_ready)
                {
                        got = net_watch_events((b32)events,
                                               address_of message,
                                               address_of held);
                        if (got < 0)
                                break;
                        if (got > 0)
                        {
                                retry_seconds = 4;
                                woken = false;
                        }
                }

                if (woken)
                {
                        retry_seconds = 4;
                        net_reconfigure_fresh(address_of held);
                }
                else if (net_exchange_cut)
                {
                        /* An exchange was cut for news that turned out to
                           matter to nobody: it asks again at once, not after
                           the idle wait. */
                        net_exchange_cut = false;
                        net_reconfigure_fresh(address_of held);
                }
        }

        net_wake_watch = -1;
        net_events_watch = -1;
        netlink_forget(address_of message);
        socket_close((b32)events);
        if (wake >= 0)
                system_close((b32)wake);
        net_kmsg_close();
        netlink_forget(address_of net_states);

        return 1;
}

//      One table dumped, one line per entry; routes print interface names.
static COLD b32 net_show(b32 handle, p16 type, positive body, p8 family,
                    netlink_visitor line, const char address_to doing)
{
        bipolar shown = type == RTM_GETROUTE ? net_names_gather(handle) : 0;

        if (shown >= 0)
                shown = netlink_dump(handle, type, body, family, line, null);
        return shown < 0 ? net_refused((string_address)doing, shown) : 0;
}

static COLD b32 net_ip(void)
{
        bipolar handle;
        string_address object = net_words() > 1 ? net_word(1) : null;
        string_address verb = net_words() > 2 ? net_word(2) : null;
        bool show = !verb || net_word_is(verb, "show", 1) ||
                    net_word_is(verb, "list", 1);
        b32 status = 0;

        if (!object || net_word_is(object, "help", 4))
        {
                string_format(log, "usage: ip auto | link | addr | route\n");
                string_format(log, "       ip auto   find a link, bring it up, "
                                   "take a lease\n");
                string_format(log, "       ip watch  the same, now and whenever "
                                   "a cable changes\n");
                string_format(log, "       ip link set NAME up\n");
                string_format(log, "       ip addr add A.B.C.D/N dev NAME\n");
                string_format(log, "       ip route add default via A.B.C.D [dev NAME]\n");
                log_flush();
                return object ? 0 : 1;
        }

        handle = netlink_open_groups(0);

        if (handle < 0)
                return net_ip_refused("cannot open a netlink socket");

        //      auto ------------------------------------------------------
        if (net_word_is(object, "auto", 2))
        {
                status = net_auto((b32)handle, null);
        }
        //      watch -----------------------------------------------------
        else if (net_word_is(object, "watch", 1))
        {
                socket_close((b32)handle);
                return net_watch();
        }
        //      link ------------------------------------------------------
        else if (net_word_is(object, "link", 1))
        {
                if (show)
                        status = net_show((b32)handle, RTM_GETLINK, sizeof(netlink_link),
                                          AF_UNSPEC, net_link_line, "link show");
                else if (net_word_is(verb, "set", 3) && net_words() == 5 &&
                         net_word_is(net_word(4), "up", 2))
                {
                        bipolar index = net_index_of((b32)handle, net_word(3));

                        status = net_done("link set", index < 0 ? index
                                          : netlink_link_up((b32)handle, (p32)index));
                }
                else
                        status = net_ip_refused(
                            "link: only 'show' and 'set NAME up'");
        }
        //      addr ------------------------------------------------------
        else if (net_word_is(object, "addr", 1) || net_word_is(object, "address", 1))
        {
                if (show)
                        status = net_show((b32)handle, RTM_GETADDR, sizeof(netlink_address),
                                          AF_INET, net_address_line, "addr show");
                else if (net_word_is(verb, "add", 1) && net_words() == 6 &&
                         net_word_is(net_word(4), "dev", 3))
                {
                        p32 host = 0;
                        p8 bits = 32;
                        bipolar index;

                        if (!net_split_prefix(net_word(3), address_of host,
                                              address_of bits))
                                status = net_ip_refused(
                                    "addr add: not an address");
                        else
                        {
                                index = net_index_of((b32)handle, net_word(5));
                                status = net_done("addr add", index < 0 ? index
                                    : netlink_address_add((b32)handle, (p32)index,
                                                          host, bits));
                        }
                }
                else
                        status = net_ip_refused(
                            "addr: only 'show' and 'add A.B.C.D/N dev NAME'");
        }
        //      route -----------------------------------------------------
        else if (net_word_is(object, "route", 1))
        {
                if (show)
                        status = net_show((b32)handle, RTM_GETROUTE, sizeof(netlink_route),
                                          AF_INET, net_route_line, "route show");
                else if (net_word_is(verb, "add", 1) &&
                         (net_words() == 6 || (net_words() == 8 &&
                          net_word_is(net_word(6), "dev", 3))) &&
                         net_word_is(net_word(3), "default", 3) &&
                         net_word_is(net_word(4), "via", 3))
                {
                        bipolar gateway = string_to_host(net_word(5));
                        bipolar index = 0;

                        if (gateway < 0)
                                status = net_ip_refused(
                                    "route add: not an address");
                        else
                        {
                                if (net_words() == 8)
                                        index = net_index_of((b32)handle, net_word(7));
                                status = net_done("route add", index < 0 ? index
                                    : netlink_route_add((b32)handle, 0, 0,
                                                        (p32)gateway, (p32)index));
                        }
                }
                else
                        status = net_ip_refused(
                            "route: only 'show' and 'add default via A.B.C.D'");
        }
        else
                status = net_ip_refused(
                    "unknown object; try link, addr or route");

        log_flush();
        socket_close((b32)handle);

        return status;
}

#endif // STANDARD_MODERN_C_SHELL_NET
