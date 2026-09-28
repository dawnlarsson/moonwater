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
        netlink_address address_to body;
        positive size = 0;
        positive label_size = 0;
        p8 address_to held;
        string_address label;
        p8 written[32];
        p32 host;

        if (header->length < NETLINK_HEADER + sizeof(netlink_address))
                return true;

        body = (netlink_address address_to)((p8 address_to)header +
                                             NETLINK_HEADER);
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
        netlink_route address_to body;
        positive gateway_size = 0;
        positive destination_size = 0;
        positive out_size = 0;
        p8 address_to gateway;
        p8 address_to destination;
        p8 address_to out;
        p8 written[32];
        (void)context;

        if (header->length < NETLINK_HEADER + sizeof(netlink_route))
                return true;

        body = (netlink_route address_to)((p8 address_to)header +
                                           NETLINK_HEADER);
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

        bipolar prepared = file_staged_name_prepare(stage);
        bipolar synced = prepared < 0 ? prepared : system_call_1(
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

/*
        wget [ -q ] [ -O FILE ] [ --no-check-certificate ] URL

        BusyBox's subset: save the body under the URL's last component, or
        under -O, and follow redirects. HTTPS is the reason this exists;
        fetch remains the small plaintext tool.
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
        string_address path;
        p16 port;
        bool tls;
        bool quiet;
        bool check_cert;
        bipolar dest = -1;
        bipolar status;
        b32 code = 0;
        bool own_file = false;
        file_staged_name staged;

        if (!file_take(address_of taking))
                return 1;

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
                return string_report(log_error, 1, "Usage: wget [-q] [-O FILE] "
                                         "[--no-check-certificate] URL\n");
        }

        if (taking.first + 1 < (positive)program_argument_count())
        {
                return string_report(log_error, 1, "wget: extra operand '%w'\n", writer_terminal_quoted_name,
                              program_argument((b32)(taking.first + 1)));
        }

        url = program_argument((b32)taking.first);
        quiet = (taking.flags & FILE_FLAG('q')) != 0;
        check_cert = (taking.flags & FILE_FLAG('K')) == 0;
        output = file_option_value(address_of taking, 'O');

        status = http_split_into(url, name, sizeof name, address_of port,
                                 address_of path, address_of tls);
        if (status)
        {
                return string_report(log_error, 1, "wget: %w is not a url this understands\n",
                              writer_terminal_quoted_name, url);
        }

        if (output && string_equals(output, (string_address) "-"))
                dest = 1;
        else
        {
                if (!output)
                {
                        http_url_leaf(path, leaf, sizeof leaf);
                        output = leaf;
                }
                dest = file_staged_name_open(
                    address_of staged, output, 0644 & ~file_umask(), 0);
                if (dest < 0)
                {
                        return string_report(log_error, 1, "wget: cannot write %w\n",
                                      writer_terminal_quoted_name, output);
                }
                own_file = true;
        }

        if (!quiet)
        {
                string_format(log_error, "%w\nSaving to: '%w'\n", writer_terminal_quoted_name, url,
                              writer_terminal_quoted_name, output);
        }

        status = http_fetch_to(url, dest, check_cert, address_of code);

        if (status)
        {
                if (own_file)
                {
                        file_staged_name_abort(address_of staged);
                }
                if (status == HTTP_NO_HOST)
                        string_format(log_error, "wget: cannot resolve %w\n",
                                      writer_terminal_quoted_name, name);
                else if (status == HTTP_NO_ROUTE)
                        string_format(log_error, "wget: cannot reach %w\n",
                                      writer_terminal_quoted_name, name);
                else if (status == HTTP_TLS)
                        string_format(log_error, "wget: TLS handshake failed\n");
                else if (status == HTTP_DOWNGRADE)
                        string_format(log_error,
                                      "wget: refused an HTTPS to HTTP redirect\n");
                else if (status == HTTP_REDIRECTS)
                        string_format(log_error, "wget: too many redirects\n");
                //      GNU wget's own line, and its file I/O status below.
                else if (status == HTTP_WRITE)
                        string_format(log_error, "Cannot write to '%w' (%s).\n",
                                      writer_terminal_quoted_name, output,
                                      file_reason(http_write_failure));
                else if (status == HTTP_NO_REPLY)
                        string_format(log_error, "wget: no reply from %w\n",
                                      writer_terminal_quoted_name, name);
                else if (status == HTTP_STATUS)
                        string_format(log_error, "wget: server returned %p\n",
                                      (positive)code);
                else
                        string_format(log_error, "wget: download failed\n");
                return status == HTTP_WRITE ? 3 : 1;
        }

        if (own_file && net_staged_name_publish(address_of staged) < 0)
        {
                return string_report(log_error, 1, "wget: cannot publish %w\n",
                              writer_terminal_quoted_name, output);
        }

        return 0;
}


/*
        /etc/resolv.conf, written with a resolver that is known to work.

        Cloudflare goes first, and the one the network handed out goes under
        it. A DHCP nameserver is usually the router in the corner, which is
        also the thing most likely to answer slowly, cache a stale record, or
        have been handed a captive portal's idea of the truth. Naming a public
        resolver first is what lets a machine work on a network whose own
        resolver does not.

        The network's own is still written, and is still asked, because it is
        the only one that knows the names inside the network. The resolver
        walks this list in order and does not stop at a public resolver saying
        it has never heard of something local.

        Order is the whole of the policy. Putting it here rather than in the
        resolver means changing which server is preferred is one line in a
        file, not a rebuild.
*/
static COLD bipolar net_write_resolv_to(string_address path, p32 nameserver)
{
        const p32 servers[2] = {
            DNS_FALLBACK, nameserver == DNS_FALLBACK ? 0 : nameserver};
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
} net_holding;

static positive net_seconds(void)
{
        positive now = clock_monotonic_nanoseconds();

        //      A clock that will not answer leaves every lease looking
        //      expired, so an address is never kept beyond an unknown
        //      deadline. Zero is that answer, and the monotonic clock's
        //      first second is when a boot takes its lease, so it reads 1.
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

static COLD positive net_lease_due_in(const net_holding address_to held,
                                 positive now)
{
        positive gone;
        positive retry;

        if (!held || !held->index || !held->lease.seconds)
                return 0;
        if (held->lost)
                return 1;
        if (net_lease_expired_at(held, now))
                return 1;

        gone = now - held->taken;
        retry = held->retry ? held->retry : held->lease.renewal;
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
                bipolar status = netlink_address_add(
                    handle, previous->index, previous->lease.address,
                    dhcp_prefix_of(previous->lease.mask));
                if (status < 0 && !failed)
                        failed = status;
        }

        if (route_changed)
        {
                bool same = false;

                if (net_owns_route(previous))
                {
                        bipolar status = netlink_route_add(
                            handle, 0, 0, previous->lease.router,
                            previous->index);
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

        if (address_changed)
        {
                status = net_owns_address(previous)
                             ? netlink_address_add(
                                   handle, index, lease->address,
                                   dhcp_prefix_of(lease->mask))
                             : netlink_address_acquire(
                                   handle, index, lease->address,
                                   dhcp_prefix_of(lease->mask));
                /* EEXIST from the exclusive create is the address already
                   there -- an operator's, or this watcher's before init
                   restarted it -- which stays theirs: present, not owned. */
                address_applied = status >= 0;
                if (status < 0 && status != -EEXIST)
                {
                        doing = (string_address) "addr add";
                        goto failed;
                }
        }

        if (route_changed && lease->router)
        {
                status = net_owns_route(previous)
                             ? netlink_route_add(handle, 0, 0, lease->router,
                                                 index)
                             : netlink_route_acquire(handle, 0, 0,
                                                     lease->router, index);
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
                              net_host_text(written, DNS_FALLBACK));

                if (lease->nameserver && lease->nameserver != DNS_FALLBACK)
                        string_format(net_out, ", then %s",
                                      net_host_text(written,
                                                    lease->nameserver));

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

static COLD bipolar net_dhcp_apart(string_address device, p8 address_to hardware,
                                   dhcp_lease address_to lease, bool renew,
                                   bool rebinding, positive wait)
{
        b32 ends[2];
        net_dhcp_answer answer = {DHCP_NO_SOCKET, *lease};
        network_deadline deadline;
        bool heard;

        if (system_call_2(syscall(pipe2), (positive)ends, O_CLOEXEC) < 0)
                return DHCP_NO_SOCKET;

        bipolar child = system_fork();

        if (child == 0)
        {
                dhcp_apart = ends[1];
                answer.status = renew ? dhcp_reacquire(device, hardware,
                                                       address_of answer.lease,
                                                       rebinding, wait)
                                      : dhcp_ask(device, hardware,
                                                 address_of answer.lease);
                system_write_all((positive)dhcp_apart, address_of answer,
                                 sizeof answer);
                system_call_1(syscall(exit_group), 0);
        }

        system_close(ends[1]);
        //      dhcp_ask's twenty attempts can each wait out a DISCOVER
        //      and a REQUEST, 150 s in all, after a CSPRNG that may take
        //      a second; a renewal is its own wait.
        heard = child > 0 &&
                network_deadline_begin(address_of deadline,
                                       renew ? wait + 5 : 180, 0) &&
                network_wait_readable_until(ends[0], address_of deadline) > 0 &&
                system_read_retry((positive)ends[0], address_of answer,
                                  sizeof answer) == (bipolar)sizeof answer;
        if (child > 0)
        {
                system_call_2(syscall(kill), (positive)child, SIGKILL);
                system_call_4(syscall(wait4), (positive)child, 0, 0, 0);
        }
        system_close(ends[0]);

        if (!heard)
                return DHCP_NO_SOCKET;
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

static COLD b32 net_auto(b32 handle, net_holding address_to held)
{
        netlink_search search;
        dhcp_lease lease;
        bipolar status;

        memory_fill(address_of search, 0, sizeof search);
        search.skip_loopback = true;
        search.prefer = net_internet_prefer();

        if (netlink_link_find(handle, address_of search) < 0)
        {
                string_format(net_out, "ip: no interface to configure\n");
                net_flush();
                return 1;
        }

        if (held && held->index == search.index && !held->lost)
                return 0;

        if (!search.has_hardware)
        {
                string_format(net_out, "ip: %w has no hardware address\n",
                              writer_terminal_quoted_name, search.name);
                net_flush();
                return 1;
        }

        string_format(net_out, "ip: using %w\n", writer_terminal_quoted_name, search.name);

        if (!(search.flags & IFF_UP))
        {
                status = netlink_link_up(handle, search.index);

                if (status < 0)
                        return net_refused((string_address) "link up", status);
        }

        string_format(net_out, "ip: asking for a lease\n");
        net_flush();

        status = net_dhcp_apart(search.name, search.hardware, address_of lease,
                                false, false, 0);

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

        return net_apply_lease(handle, search.index, search.name,
                               search.hardware, address_of lease, held, true);
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

/* News without IFF_RUNNING is also what the kernel sends the moment a link
   is brought up; linkwatch says RUNNING up to a second later, and the lease
   is taken between the two, so every boot released a lease a millisecond
   old and asked again. The held link is asked as it is now, and carrier
   (IFF_LOWER_UP) counts. */
#define NET_LOWER_UP 0x10000

static COLD bool net_link_carrier_now(const net_holding address_to held)
{
        netlink_search now = {.wanted = (string_address)held->name};
        bipolar handle = netlink_open_groups(0);
        bool carrier = handle >= 0 &&
                       netlink_link_find((b32)handle, address_of now) >= 0 &&
                       now.index == held->index &&
                       (now.flags & (IFF_RUNNING | NET_LOWER_UP));

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
            !net_link_carrier_now(held))
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
        netlink_link address_to link;

        if (!header || header->port ||
            header->length < NETLINK_HEADER + sizeof(netlink_link))
                return false;

        link = (netlink_link address_to)((p8 address_to)header +
                                         NETLINK_HEADER);
        if (header->type == RTM_DELLINK)
                return net_link_removed(link->index, held);
        if (header->type != RTM_NEWLINK || (link->flags & IFF_LOOPBACK))
                return false;
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

        wake = net_wake_listen();

        //      Configure whatever is already plugged in before waiting for
        //      anything to change, or a machine that boots with its cable in
        //      would wait forever for an event that already happened.
        memory_fill(address_of held, 0, sizeof held);
        net_reconfigure_fresh(address_of held);

        for (;;)
        {
                netlink_header address_to header;
                positive at = 0;
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

                        if (held.index && held.lease.seconds)
                                due = net_lease_due_in(address_of held,
                                                       net_seconds());
                        else
                                due = retry_seconds;

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
                                        wake = net_wake_listen();

                                if (!held.index || !held.lease.seconds)
                                {
                                        net_reconfigure_fresh(address_of held);
                                        if (!held.index || !held.lease.seconds)
                                        {
                                                retry_seconds *= 2;
                                                if (retry_seconds > 30)
                                                        retry_seconds = 30;
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
                                    address_of renewed, true, rebinding,
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
                        got = netlink_receive((b32)events, address_of message,
                                              null);

                        /* recvfrom can still be interrupted in the narrow
                           interval after the readiness poll.  Nothing was
                           consumed, and the lease deadline is recomputed at
                           the top of the loop. */
                        if (got == NETWORK_INTERRUPTED)
                                continue;
                        if (got < 0)
                                break;

                        while (at + NETLINK_HEADER <= message.used)
                        {
                                bool interesting = false;

                                header = (netlink_header address_to)(
                                    message.bytes + at);

                                if (header->length < NETLINK_HEADER ||
                                    at + header->length > message.used)
                                        break;

                                interesting = net_link_event(header,
                                                             address_of held);

                                at += netlink_align(header->length);

                                if (interesting)
                                {
                                        retry_seconds = 4;
                                        net_reconfigure_fresh(address_of held);
                                        woken = false;
                                }
                        }
                }

                if (woken)
                {
                        retry_seconds = 4;
                        net_reconfigure_fresh(address_of held);
                }
        }

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

                        if (index < 0)
                                status = net_refused((string_address) "link set", index);
                        else
                        {
                                bipolar done = netlink_link_up((b32)handle, (p32)index);

                                if (done < 0)
                                        status = net_refused((string_address) "link set",
                                                             done);
                        }
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
                        else if ((index = net_index_of((b32)handle, net_word(5))) < 0)
                                status = net_refused((string_address) "addr add", index);
                        else
                        {
                                bipolar done = netlink_address_add((b32)handle, (p32)index,
                                                                   host, bits);

                                if (done < 0)
                                        status = net_refused((string_address) "addr add",
                                                             done);
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
                        else if (net_words() == 8 &&
                                 (index = net_index_of((b32)handle, net_word(7))) < 0)
                                status = net_refused((string_address) "route add", index);
                        else
                        {
                                bipolar done = netlink_route_add(
                                    (b32)handle, 0, 0, (p32)gateway, (p32)index);

                                if (done < 0)
                                        status = net_refused((string_address) "route add", done);
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
