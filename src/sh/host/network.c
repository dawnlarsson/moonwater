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
        of how this machine is reached (src/net/discovery.c), a line a key,
        which Settings' Network rows read; on and off are every lookup that
        sends a packet off the machine at once, and refresh looks again now,
        as far as the switches allow.

        moonwater network discovery LOOKUP [on|off]: one lookup's switch
        (discovery_switches: stun, https). Each is a word in /root, on when
        there is none, and off sends no packet for it.
*/
static fn network_interfaces(const discovery_here address_to here)
{
        positive listed = 0;

        for (positive at = 0; at < here->interfaces; at++)
        {
                const discovery_interface address_to link = here->interface + at;
                positive shown = 0;

                if (link->kind == DISCOVERY_KIND_LOOPBACK)
                        continue;
                listed++;
                string_format(log, "  interface %s %s:", (string_address)link->name,
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

static b32 network_discovery_verb(string_address address_to arguments, positive count)
{
        discovery_settings settings;
        discovery_found found;
        discovery_here here;
        positive which = 0;
        string_address word = count > 3 ? arguments[count - 1] : null;

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
