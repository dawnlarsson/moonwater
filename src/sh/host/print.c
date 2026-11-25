/*
        src/sh/host/print.c: the verb print.

        Native IPP Everywhere printing and local CUPS Unix-socket shim:
          moonwater print [list]             saved printers (* marks default)
          moonwater print discover           find IPP Everywhere printers on the LAN via mDNS
          moonwater print add URI [NAME]     probe and save an IPP/IPPS printer
          moonwater print remove NAME        remove a saved printer
          moonwater print default NAME       set the default printer
          moonwater print FILE [PRINTER]     print a PDF, PNG, JPEG or text file
          moonwater print jobs [PRINTER]     list active jobs on a printer
          moonwater print cancel ID [PRINTER] cancel a print job
          moonwater print serve [SOCKET]     run the local CUPS Unix-socket shim

        A run of src/sh/host.c, which includes this file where the run stands.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

static fn print_caps_text(p32 caps, p8 address_to out, positive room)
{
        static const struct
        {
                p32 bit;
                string_address word;
        } table[] = {
            {IPP_CAP_PDF, "PDF"},
            {IPP_CAP_PWG_RASTER, "PWG"},
            {IPP_CAP_JPEG, "JPEG"},
            {IPP_CAP_PNG, "PNG"},
            {IPP_CAP_COLOR, "color"},
            {IPP_CAP_DUPLEX, "duplex"},
        };
        positive used = 0;

        if (!room)
                return;
        out[0] = end;
        for (positive i = 0; i < array_count(table); i++)
        {
                if (!(caps & table[i].bit))
                        continue;
                positive wlen = string_length(table[i].word);

                if (used && used + 1 < room)
                        out[used++] = ',';
                if (used + wlen >= room)
                        break;
                memory_copy(out + used, table[i].word, wlen);
                used += wlen;
        }
        out[used] = end;
}

static bipolar print_resolve_saved(const ipp_printer address_to printers,
                                   positive count, string_address def_name,
                                   string_address wanted)
{
        if (!count)
                return -1;
        if (wanted && wanted[0])
        {
                for (positive i = 0; i < count; i++)
                        if (string_equals(printers[i].name, wanted))
                                return (bipolar)i;
                return -1;
        }
        if (def_name && def_name[0])
        {
                for (positive i = 0; i < count; i++)
                        if (string_equals(printers[i].name, def_name))
                                return (bipolar)i;
        }
        return 0;
}

static b32 print_list(void)
{
        ipp_printer printers[IPP_MAX_PRINTERS];
        p8 def_name[96];
        positive count = cups_store_load(
            (string_address)CUPS_PRINTERS_PATH,
            (string_address)CUPS_DEFAULT_PATH, printers, IPP_MAX_PRINTERS,
            def_name, sizeof(def_name));

        if (!count)
        {
                host_say(log, host_label "no printers saved (try: moonwater print discover, or moonwater print add ipp://HOST/ipp/print)\n");
                return 0;
        }
        for (positive i = 0; i < count; i++)
        {
                p8 caps[64];
                bool is_def = def_name[0]
                                  ? string_equals(printers[i].name, def_name)
                                  : (i == 0);

                print_caps_text(printers[i].caps, caps, sizeof(caps));
                host_say(log, "%s %s  %s  %s%s%s\n", is_def ? "*" : " ",
                         printers[i].name, printers[i].uri,
                         printers[i].make_model[0] ? printers[i].make_model
                                                   : (string_address)"IPP",
                         caps[0] ? " [" : "", caps[0] ? caps : (string_address)"");
                if (caps[0])
                        host_say(log, "]");
        }
        log_flush();
        return 0;
}

static b32 print_discover(void)
{
        bipolar fd;
        p8 q_ipp[64], q_ipps[64];
        positive q_ipp_len, q_ipps_len;
        socket_address_internet group = {
            .family = AF_INET,
            .port = network_order_16(DISCOVERY_MDNS_PORT),
            .host = network_order_32(DISCOVERY_MDNS_GROUP),
        };
        ipp_printer all[IPP_MAX_PRINTERS];
        positive total = 0;
        network_deadline deadline;

        fd = socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (fd < 0)
                return host_refuse("could not open mDNS socket\n");

        q_ipp_len = ipp_mdns_query(false, q_ipp, sizeof(q_ipp));
        q_ipps_len = ipp_mdns_query(true, q_ipps, sizeof(q_ipps));
        (void)socket_send((b32)fd, q_ipp, q_ipp_len, 0, address_of group,
                          sizeof(group));
        (void)socket_send((b32)fd, q_ipps, q_ipps_len, 0, address_of group,
                          sizeof(group));

        network_deadline_begin(address_of deadline, 1, 500000000u);
        for (;;)
        {
                p8 pkt[1500];
                socket_address_internet from;
                p32 from_len = sizeof(from);
                positive left_s = 0, left_ns = 0;
                bipolar got;

                if (!network_deadline_left(address_of deadline,
                                           address_of left_s,
                                           address_of left_ns))
                        break;
                if (!network_stream_timeout(fd, left_s, left_ns))
                        break;
                got = socket_receive((b32)fd, pkt, sizeof(pkt), 0,
                                     address_of from, address_of from_len);
                if (got <= 0)
                        break;

                ipp_printer batch[IPP_MAX_PRINTERS];
                p32 src_ip = network_order_32(from.host);
                positive n = ipp_mdns_read(pkt, (positive)got, src_ip, batch,
                                           IPP_MAX_PRINTERS);
                for (positive i = 0; i < n && total < IPP_MAX_PRINTERS; i++)
                {
                        bool dup = false;

                        for (positive j = 0; j < total; j++)
                                if (string_equals(all[j].uri, batch[i].uri))
                                {
                                        dup = true;
                                        break;
                                }
                        if (!dup)
                                all[total++] = batch[i];
                }
        }
        socket_close((b32)fd);

        if (!total)
        {
                host_say(log, host_label "no IPP printers answered on the local network\n");
                return 0;
        }
        for (positive i = 0; i < total; i++)
        {
                p8 caps[64];

                print_caps_text(all[i].caps, caps, sizeof(caps));
                host_say(log, "  %s  %s  %s  [%s]\n", all[i].name, all[i].uri,
                         all[i].make_model[0] ? all[i].make_model
                                              : (string_address)"IPP Everywhere",
                         caps);
        }
        log_flush();
        return 0;
}

static b32 print_add(string_address uri_text, string_address custom_name)
{
        ipp_uri u;
        ipp_printer printers[IPP_MAX_PRINTERS];
        ipp_printer added;
        p8 def_name[96];
        positive count;

        if (!ipp_parse_uri(uri_text, address_of u))
                return host_refuse("invalid IPP URI (expected ipp://HOST[:PORT][/PATH])\n");

        memory_zero(address_of added, sizeof(added));
        (void)ipp_format_uri(address_of u, added.uri, sizeof(added.uri));
        added.tls = u.tls;
        added.port = u.port;
        added.state = IPP_PRINTER_IDLE;
        added.accepting = true;
        added.caps = IPP_CAP_PDF | IPP_CAP_JPEG | IPP_CAP_PNG;

        // Probe the printer live if reachable; keep defaults if offline.
        (void)ipp_get_printer_attributes(address_of u, 0, false,
                                         address_of added);
        if (custom_name && custom_name[0])
                cups_copy_field((const p8 address_to)custom_name,
                                string_length(custom_name), added.name,
                                sizeof(added.name));
        else if (!added.name[0])
                cups_copy_field(u.host, string_length(u.host), added.name,
                                sizeof(added.name));

        count = cups_store_load((string_address)CUPS_PRINTERS_PATH,
                                (string_address)CUPS_DEFAULT_PATH, printers,
                                IPP_MAX_PRINTERS, def_name, sizeof(def_name));

        // Update existing entry with same name, or append.
        {
                bool replaced = false;

                for (positive i = 0; i < count; i++)
                        if (string_equals(printers[i].name, added.name))
                        {
                                printers[i] = added;
                                replaced = true;
                                break;
                        }
                if (!replaced)
                {
                        if (count >= IPP_MAX_PRINTERS)
                                return host_refuse("printer table is full\n");
                        printers[count++] = added;
                }
        }
        if (!cups_store_save((string_address)CUPS_PRINTERS_PATH, printers,
                             count))
                return host_refuse("could not write " CUPS_PRINTERS_PATH "\n");
        if (count == 1 || !def_name[0])
                (void)cups_store_set_default((string_address)CUPS_DEFAULT_PATH,
                                             added.name);

        host_say(log, host_label "added printer %s (%s)\n", added.name,
                 added.uri);
        log_flush();
        return 0;
}

static b32 print_remove(string_address name)
{
        ipp_printer printers[IPP_MAX_PRINTERS];
        p8 def_name[96];
        positive count = cups_store_load(
            (string_address)CUPS_PRINTERS_PATH,
            (string_address)CUPS_DEFAULT_PATH, printers, IPP_MAX_PRINTERS,
            def_name, sizeof(def_name));
        positive kept = 0;
        bool removed = false;

        for (positive i = 0; i < count; i++)
        {
                if (string_equals(printers[i].name, name))
                {
                        removed = true;
                        continue;
                }
                printers[kept++] = printers[i];
        }
        if (!removed)
                return host_refuse("printer not found\n");
        if (!cups_store_save((string_address)CUPS_PRINTERS_PATH, printers, kept))
                return host_refuse("could not write " CUPS_PRINTERS_PATH "\n");
        host_say(log, host_label "removed printer %s\n", name);
        log_flush();
        return 0;
}

static b32 print_set_default(string_address name)
{
        ipp_printer printers[IPP_MAX_PRINTERS];
        p8 def_name[96];
        positive count = cups_store_load(
            (string_address)CUPS_PRINTERS_PATH,
            (string_address)CUPS_DEFAULT_PATH, printers, IPP_MAX_PRINTERS,
            def_name, sizeof(def_name));

        if (print_resolve_saved(printers, count, def_name, name) < 0)
                return host_refuse("printer not found\n");
        if (!cups_store_set_default((string_address)CUPS_DEFAULT_PATH, name))
                return host_refuse("could not write " CUPS_DEFAULT_PATH "\n");
        host_say(log, host_label "default printer: %s\n", name);
        log_flush();
        return 0;
}

static b32 print_jobs(string_address printer_name)
{
        ipp_printer printers[IPP_MAX_PRINTERS];
        p8 def_name[96];
        positive count = cups_store_load(
            (string_address)CUPS_PRINTERS_PATH,
            (string_address)CUPS_DEFAULT_PATH, printers, IPP_MAX_PRINTERS,
            def_name, sizeof(def_name));
        bipolar idx = print_resolve_saved(printers, count, def_name,
                                          printer_name);
        ipp_uri u;
        ipp_job_info jobs[IPP_MAX_JOBS];
        positive n = 0;

        if (idx < 0)
                return host_refuse("no matching printer\n");
        if (!ipp_parse_uri(printers[idx].uri, address_of u))
                return host_refuse("invalid printer URI\n");
        if (ipp_get_jobs(address_of u, printers[idx].ip, false, false, jobs,
                         IPP_MAX_JOBS, address_of n) != IPP_OK)
                return host_refuse("could not query jobs from printer\n");
        if (!n)
        {
                host_say(log, host_label "no active jobs on %s\n",
                         printers[idx].name);
                return 0;
        }
        for (positive i = 0; i < n; i++)
                host_say(log, "  #%d  %s  %s  (%s)\n", (b32)jobs[i].job_id,
                         ipp_job_state_name(jobs[i].state),
                         jobs[i].name[0] ? jobs[i].name
                                         : (string_address)"Untitled",
                         jobs[i].user[0] ? jobs[i].user
                                         : (string_address)"unknown");
        log_flush();
        return 0;
}

static b32 print_cancel(string_address id_text, string_address printer_name)
{
        ipp_printer printers[IPP_MAX_PRINTERS];
        p8 def_name[96];
        positive count = cups_store_load(
            (string_address)CUPS_PRINTERS_PATH,
            (string_address)CUPS_DEFAULT_PATH, printers, IPP_MAX_PRINTERS,
            def_name, sizeof(def_name));
        bipolar idx = print_resolve_saved(printers, count, def_name,
                                          printer_name);
        ipp_uri u;
        positive job_id = 0;

        if (idx < 0)
                return host_refuse("no matching printer\n");
        for (positive i = 0; id_text[i]; i++)
        {
                if (id_text[i] < '0' || id_text[i] > '9')
                        return host_refuse("invalid job id\n");
                job_id = job_id * 10 + (positive)(id_text[i] - '0');
        }
        if (!job_id)
                return host_refuse("invalid job id\n");
        if (!ipp_parse_uri(printers[idx].uri, address_of u))
                return host_refuse("invalid printer URI\n");
        if (ipp_cancel_job(address_of u, printers[idx].ip, false, (b32)job_id,
                           null) != IPP_OK)
                return host_refuse("could not cancel job\n");
        host_say(log, host_label "canceled job #%d on %s\n", (b32)job_id,
                 printers[idx].name);
        log_flush();
        return 0;
}

static b32 print_file(string_address path, string_address printer_name)
{
        ipp_printer printers[IPP_MAX_PRINTERS];
        p8 def_name[96];
        positive count = cups_store_load(
            (string_address)CUPS_PRINTERS_PATH,
            (string_address)CUPS_DEFAULT_PATH, printers, IPP_MAX_PRINTERS,
            def_name, sizeof(def_name));
        bipolar idx = print_resolve_saved(printers, count, def_name,
                                          printer_name);
        ipp_uri u;
        bipolar fd;
        p8 magic[16];
        bipolar got;
        bipolar file_size;
        ipp_job_request jr;
        ipp_job_result res;
        bipolar rc;

        if (idx < 0)
                return host_refuse("no printer configured (try: moonwater print add URI)\n");
        if (!ipp_parse_uri(printers[idx].uri, address_of u))
                return host_refuse("invalid printer URI\n");

        fd = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        if (fd < 0)
                return host_refuse("could not open file to print\n");

        file_size = system_call_3(syscall(lseek), (positive)fd, 0, 2 /* SEEK_END */);
        (void)system_call_3(syscall(lseek), (positive)fd, 0, 0 /* SEEK_SET */);
        if (file_size <= 0)
        {
                system_close(fd);
                return host_refuse("file is empty or not seekable\n");
        }
        got = system_read_once(fd, magic, sizeof(magic));
        (void)system_call_3(syscall(lseek), (positive)fd, 0, 0 /* SEEK_SET */);

        memory_zero(address_of jr, sizeof(jr));
        jr.job_name = path;
        jr.user_name = (string_address)"moonwater";
        jr.mime_type = ipp_detect_mime(magic, got > 0 ? (positive)got : 0, path);
        jr.copies = 1;
        jr.color = (printers[idx].caps & IPP_CAP_COLOR) != 0;

        rc = ipp_print_job(address_of u, printers[idx].ip, false, address_of jr,
                           null, 0, fd, (positive)file_size, address_of res);
        system_close(fd);
        if (rc != IPP_OK)
                return host_refuse("printer rejected or could not receive job\n");

        host_say(log, host_label "sent %s (%s) to %s -> job #%d (%s)\n", path,
                 jr.mime_type, printers[idx].name, (b32)res.job_id,
                 ipp_job_state_name(res.job_state));
        log_flush();
        return 0;
}

static b32 host_print(string_address address_to arguments, positive count)
{
        if (count == 2 || string_equals(arguments[2], "list"))
                return print_list();
        if (string_equals(arguments[2], "discover"))
                return print_discover();
        if (string_equals(arguments[2], "add") && count >= 4)
                return print_add(arguments[3], count >= 5 ? arguments[4] : null);
        if (string_equals(arguments[2], "remove") && count >= 4)
                return print_remove(arguments[3]);
        if (string_equals(arguments[2], "default") && count >= 4)
                return print_set_default(arguments[3]);
        if (string_equals(arguments[2], "jobs"))
                return print_jobs(count >= 4 ? arguments[3] : null);
        if (string_equals(arguments[2], "cancel") && count >= 4)
                return print_cancel(arguments[3], count >= 5 ? arguments[4] : null);
        if (string_equals(arguments[2], "serve"))
        {
                cups_shim_state st;
                string_address sock = count >= 4
                                          ? arguments[3]
                                          : (string_address)CUPS_SHIM_SOCKET;

                cups_shim_init(address_of st,
                               (string_address)CUPS_PRINTERS_PATH,
                               (string_address)CUPS_DEFAULT_PATH);
                return cups_shim_run(address_of st, sock, 0);
        }
        return print_file(arguments[2], count >= 4 ? arguments[3] : null);
}
