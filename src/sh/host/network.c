/*
        src/sh/host/network.c: the verb network.

        A run of src/sh/host.c, which includes this file where the run stands: it is compiled only as part of that
        file, with its statics and macros, and never on its own. host_main, at the end of src/sh/host.c, names the
        function each verb runs.
*/

/*
        moonwater network: the network module as one page. Each interface
        with its kind and its addresses, the servers names are asked of in
        their order (what `moonwater dns` says), and what discovery may look
        up and when it last did. It reads the kernel's table and what was
        kept, and sends nothing.

        moonwater network discovery [on|off|refresh]: what discovery learned
        of how this machine is reached (src/net/discovery.c), a line a key
        with the value alone, which Settings' Network rows read, and how and
        when each was learned on lines of their own; on and off are every
        switch below at once, and refresh looks again now, as far as the
        switches allow.

        moonwater network discovery LOOKUP [on|off]: one switch
        (discovery_switches: stun, https, announce, listen). Each is a word
        in /root, on when there is none, and off sends no packet for it.

        moonwater network discovery stun servers [NAME[:PORT]...|add
        NAME[:PORT]|remove NAME[:PORT]|reset]: the STUN servers, up to four
        in the order they are tried; reset is the default pair.

        moonwater network discovery https url [URL|default]: where the trace
        is fetched.

        The time limits (network discovery stun timeout, network discovery
        https timeout), network proxy and network keepalive are plain rows
        of registry.c, answered before this verb is asked.
*/
static fn network_interfaces(const discovery_here address_to here)
{
        positive listed = 0;

        for (positive at = 0; at < here->interfaces; at++)
        {
                const discovery_interface address_to link = here->interface + at;
                p8 name[DISCOVERY_NAME_TEXT];
                positive shown = 0;

                if (link->kind == DISCOVERY_KIND_LOOPBACK)
                        continue;
                listed++;
                discovery_name_text(link->name, name);
                string_format(log, "  interface %s %s:", (string_address)name,
                              (string_address)discovery_kind_names[link->kind]);
                for (positive own = 0; own < here->owns; own++)
                {
                        const discovery_local address_to mine = here->own + own;
                        p8 text[48];

                        if (mine->index != link->index)
                                continue;
                        discovery_address_text(mine->address, text);
                        string_format(log, "%s %s/%p %s", shown++ ? ";" : "",
                                      (string_address)text, (positive)mine->prefix,
                                      (string_address)discovery_scope_names[mine->scope]);
                }
                string_format(log, "%s\n", shown ? "" : " no address");
        }
        if (!listed)
                string_format(log, "  interface none but loopback\n");
}

static b32 network_page(void)
{
        discovery_here here;
        p8 path[DISCOVERY_PATH_ROOM];
        p8 word[8];

        discovery_here_read(address_of here);
        host_say(log, host_label "network\n");
        network_interfaces(address_of here);
        dns_say(false);
        //      The switches are root's words: anybody else is told so, and
        //      not that a machine with every lookup off has them on.
        discovery_kept_path(DISCOVERY_KEPT, discovery_switches[0].leaf, path);
        if (!locale_readable((string_address)path, word, sizeof(word)))
                string_format(log, "  discovery " LOCALE_NEEDS_ROOT "\n");
        else
        {
                discovery_settings settings;
                discovery_found found;
                p8 when[32];

                discovery_settings_read(address_of settings);
                discovery_found_read(address_of found);
                discovery_when(found.asked, when);
                string_format(log, "  discovery %s", settings.switched[0] ? "on" : "off");
                for (positive at = 1; at < array_count(discovery_switches); at++)
                        string_format(log, "; %s %s",
                                      (string_address)discovery_switches[at].word,
                                      settings.switched[at] ? "on" : "off");
                string_format(log, "; looked %s (moonwater network discovery)\n",
                              (string_address)when);
        }
        log_flush();
        return 0;
}

//      The list kept as one line, the servers a blank apart.
static b32 network_servers_save(const discovery_stun_server address_to servers,
                                positive count)
{
        p8 path[DISCOVERY_PATH_ROOM];
        p8 line[DISCOVERY_LIST_ROOM];
        bipolar failed;

        line[0] = end;
        for (positive at = 0; at < count; at++)
        {
                p8 text[DISCOVERY_NAME_ROOM + 8];

                discovery_server_text(servers + at, text);
                if (at)
                        string_append_bounded((string_address)line, " ", sizeof line);
                string_append_bounded((string_address)line, (string_address)text, sizeof line);
        }
        discovery_kept_path(DISCOVERY_KEPT, DISCOVERY_STUN_LEAF, path);
        failed = radio_write_word((string_address)path, (string_address)line);
        return_if(failed < 0, host_fail("network discovery stun servers", failed));
        return 0;
}

static fn network_servers_say(const discovery_settings address_to settings)
{
        host_say(log, host_label "stun servers");
        for (positive at = 0; at < settings->servers; at++)
        {
                p8 text[DISCOVERY_NAME_ROOM + 8];

                discovery_server_text(settings->server + at, text);
                string_format(log, " %s", (string_address)text);
        }
        string_format(log, "%s\n", settings->servers_kept ? "" : " (the default)");
}

/*      network discovery stun servers ...: words from arguments[5] on. A
        server is named once, the list holds one to four, and a word that
        is no server is refused with the list as it was. */
static b32 network_servers_verb(string_address address_to arguments, positive count)
{
        discovery_settings settings;
        discovery_stun_server servers[DISCOVERY_STUN_SERVERS];
        discovery_stun_server named;
        positive have;
        string_address word = count > 5 ? arguments[5] : null;
        bool add = word && string_equals(word, "add");
        bool remove = word && string_equals(word, "remove");
        b32 failed = 0;

        return_if((add || remove) && count != 7, host_usage());
        host_need_root();
        discovery_settings_read(address_of settings);
        if (count == 5)
        {
                network_servers_say(address_of settings);
                return 0;
        }
        if (count == 6 && string_equals(word, "reset"))
        {
                p8 path[DISCOVERY_PATH_ROOM];
                bipolar gone;

                discovery_kept_path(DISCOVERY_KEPT, DISCOVERY_STUN_LEAF, path);
                gone = system_remove_at(AT_FDCWD, (string_address)path, 0);
                return_if(gone < 0 && gone != -ENOENT,
                          host_fail("network discovery stun servers", gone));
        }
        else if (add || remove)
        {
                string_address server = arguments[6];

                if (!discovery_server_parse((const p8 address_to)server, string_length(server),
                                            address_of named))
                        return host_refuse("%s is not a STUN server: a name or an IPv4 address, "
                                           "and :PORT when it is not 3478\n", server);
                have = settings.servers;
                memory_copy(servers, settings.server, sizeof servers);
                if (add)
                {
                        for (positive at = 0; at < have; at++)
                                if (servers[at].port == named.port &&
                                    string_equals((string_address)servers[at].name,
                                                  (string_address)named.name))
                                        return host_refuse("%s is listed already\n", server);
                        if (have == DISCOVERY_STUN_SERVERS)
                                return host_refuse("the list holds %p servers at most; remove "
                                                   "one first\n", (positive)DISCOVERY_STUN_SERVERS);
                        servers[have++] = named;
                }
                else
                {
                        positive kept = 0;
                        //      A name alone is every port it is listed with.
                        bool any_port = !memory_search((address_any)server, string_length(server),
                                                       (address_any) ":", 1);

                        for (positive at = 0; at < have; at++)
                                if (!string_equals((string_address)servers[at].name,
                                                   (string_address)named.name) ||
                                    (!any_port && servers[at].port != named.port))
                                        servers[kept++] = servers[at];
                        if (kept == have)
                                return host_refuse("%s is not listed\n", server);
                        if (!kept)
                                return host_refuse("the list keeps one server at least: "
                                                   "network discovery stun off asks none\n");
                        have = kept;
                }
                failed = network_servers_save(servers, have);
        }
        else
        {
                p8 line[DISCOVERY_LIST_ROOM];

                line[0] = end;
                for (positive at = 5; at < count; at++)
                {
                        if (at > 5)
                                string_append_bounded((string_address)line, " ", sizeof line);
                        string_append_bounded((string_address)line, arguments[at], sizeof line);
                }
                if (count - 5 > DISCOVERY_STUN_SERVERS ||
                    string_length((string_address)line) + 1 >= sizeof line ||
                    !discovery_servers_parse(line, string_length((string_address)line),
                                             servers, address_of have))
                        return host_refuse("stun servers are one to %p servers, each a name or an "
                                           "IPv4 address with :PORT when it is not 3478, each "
                                           "named once, or reset\n",
                                           (positive)DISCOVERY_STUN_SERVERS);
                failed = network_servers_save(servers, have);
        }
        if (failed)
                return failed;
        discovery_settings_read(address_of settings);
        network_servers_say(address_of settings);
        return 0;
}

//      network discovery https url [URL|default].
static b32 network_url_verb(string_address address_to arguments, positive count)
{
        discovery_settings settings;
        p8 path[DISCOVERY_PATH_ROOM];
        bipolar failed;

        host_need_root();
        discovery_kept_path(DISCOVERY_KEPT, DISCOVERY_URL_LEAF, path);
        if (count == 6 && string_equals(arguments[5], "default"))
        {
                failed = system_remove_at(AT_FDCWD, (string_address)path, 0);
                return_if(failed < 0 && failed != -ENOENT,
                          host_fail("network discovery https url", failed));
        }
        else if (count == 6)
        {
                if (!discovery_url_good((const p8 address_to)arguments[5],
                                        string_length(arguments[5])))
                        return host_refuse("the trace URL is https:// and a host, with no blank, "
                                           "quote, backslash or semicolon, under %p bytes, or "
                                           "default, not %s\n",
                                           (positive)DISCOVERY_URL_ROOM, arguments[5]);
                failed = radio_write_word((string_address)path, arguments[5]);
                return_if(failed < 0, host_fail("network discovery https url", failed));
        }
        discovery_settings_read(address_of settings);
        host_say(log, host_label "https url %s%s\n", (string_address)settings.url,
                 settings.url_kept ? (string_address) "" : (string_address) " (the default)");
        return 0;
}

static b32 network_discovery_verb(string_address address_to arguments, positive count)
{
        discovery_settings settings;
        discovery_found found;
        discovery_here here;
        positive which = 0;
        string_address word = count > 3 ? arguments[count - 1] : null;

        if (count >= 5 && string_equals(arguments[3], "stun") &&
            string_equals(arguments[4], "servers"))
                return network_servers_verb(arguments, count);
        if (count >= 5 && count <= 6 && string_equals(arguments[3], "https") &&
            string_equals(arguments[4], "url"))
                return network_url_verb(arguments, count);
        return_if(count > 5, host_usage());
        if (count > 3 && !(count == 4 && (string_equals(word, "refresh") ||
                                          host_onoff(word) >= 0)))
        {
                //      A lookup's own switch, said or set.
                for (which = 1; which < array_count(discovery_switches); which++)
                        if (string_equals(arguments[3], discovery_switches[which].word))
                                break;
                return_if(which == array_count(discovery_switches) ||
                          (count == 5 && host_onoff(word) < 0), host_usage());
        }
        //      What was learned is root's to read, and the switches root's to set.
        host_need_root();
        if (count == 4 + (which != 0) && !string_equals(word, "refresh"))
        {
                p8 path[DISCOVERY_PATH_ROOM];
                bipolar failed;

                discovery_kept_path(DISCOVERY_KEPT, discovery_switches[which].leaf, path);
                failed = radio_write_word((string_address)path, word);
                return_if(failed < 0, host_fail("network discovery", failed));
                host_say(log, host_label "%s %s: %s%s\n",
                         (string_address)discovery_switches[which].word, word,
                         (string_address)discovery_switches[which].says,
                         string_equals(word, "off") ? ", and no packet leaves for it"
                                                    : "");
                return 0;
        }
        discovery_settings_read(address_of settings);
        if (which)
        {
                host_say(log, host_label "%s %s: %s\n",
                         (string_address)discovery_switches[which].word,
                         settings.switched[which] ? "on" : "off",
                         (string_address)discovery_switches[which].says);
                return 0;
        }
        if (count == 4)
                discovery_gather(address_of settings, address_of found, address_of here);
        else
        {
                discovery_found_read(address_of found);
                discovery_here_read(address_of here);
        }
        discovery_report(log, address_of settings, address_of found, address_of here, "");
        log_flush();
        return 0;
}

static b32 host_network(string_address address_to arguments, positive count)
{
        if (count == 2)
                return network_page();
        if (string_equals(arguments[2], "discovery"))
                return network_discovery_verb(arguments, count);
        return host_usage();
}
