/*
        cups_shim.c: lightweight local CUPS Unix-socket IPP shim
        (/run/moonwater/cups.sock) and saved-printer store (/root/printers).

        Speaks the subset of IPP-over-HTTP that libcups clients (Chromium,
        Firefox, GTK, Qt, LibreOffice, lp/lpr/lpstat/cancel) use over a local
        Unix socket, and proxies print jobs straight to real IPP Everywhere
        printers via src/net/ipp.c — no cupsd, no PPD compiler, no Ghostscript.

        Supported IPP operations:
          - CUPS-Get-Printers (0x4002)
          - CUPS-Get-Default (0x4001)
          - Get-Printer-Attributes (0x000b)
          - Validate-Job (0x0004)
          - Create-Job (0x0005)
          - Send-Document (0x0006)
          - Print-Job (0x0002)
          - Get-Jobs (0x000a)
          - Get-Job-Attributes (0x0009)
          - Cancel-Job (0x0008)

        Include after src/net/ipp.c.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef STANDARD_MODERN_C_NET_CUPS_SHIM
#define STANDARD_MODERN_C_NET_CUPS_SHIM

#define CUPS_SHIM_SOCKET "/run/moonwater/cups.sock"
#define CUPS_PRINTERS_PATH "/root/printers"
#define CUPS_DEFAULT_PATH "/root/printer.default"
#define CUPS_SHIM_MAX_JOBS 64u
#define CUPS_SHIM_MAX_BODY (64u * 1024u * 1024u)
#define CUPS_SHIM_HEAD_MAX 8192u

/* ---- Saved Printer Store (/root/printers, /root/printer.default) ---- */

static bool cups_field_clean(string_address s)
{
        if (!s)
                return false;
        for (positive i = 0; s[i]; i++)
                if (s[i] < 0x20 || s[i] == 0x7f || s[i] == '\t')
                        return false;
        return true;
}

static fn cups_copy_field(const p8 address_to src, positive len,
                          p8 address_to dst, positive room)
{
        positive n = len < room - 1 ? len : room - 1;

        if (n)
                memory_copy(dst, src, n);
        dst[n] = end;
}

static p32 cups_parse_hex32(const p8 address_to s, positive len)
{
        p32 v = 0;

        for (positive i = 0; i < len; i++)
        {
                positive d = digit_known(s[i], 16);

                if (d == 16)
                        break;
                v = (v << 4) | (p32)d;
        }
        return v;
}

static positive cups_store_load(string_address printers_path,
                                string_address default_path,
                                ipp_printer address_to out, positive max_out,
                                p8 address_to default_name,
                                positive default_room)
{
        p8 buf[8192];
        bipolar fd, got;
        positive count = 0;
        positive at = 0;

        if (default_name && default_room)
                default_name[0] = end;

        if (default_path && default_name && default_room)
        {
                fd = system_open_at(AT_FDCWD, default_path,
                                    FILE_READ | O_CLOEXEC);
                if (fd >= 0)
                {
                        got = system_read_once(fd, default_name,
                                               default_room - 1);
                        system_close(fd);
                        if (got > 0)
                        {
                                positive n = (positive)got;

                                while (n && (default_name[n - 1] == '\n' ||
                                             default_name[n - 1] == '\r' ||
                                             default_name[n - 1] == ' '))
                                        n--;
                                default_name[n] = end;
                        }
                }
        }

        if (!printers_path || !out || !max_out)
                return 0;

        fd = system_open_at(AT_FDCWD, printers_path, FILE_READ | O_CLOEXEC);
        if (fd < 0)
                return 0;
        got = system_read_once(fd, buf, sizeof(buf) - 1);
        system_close(fd);
        if (got <= 0)
                return 0;
        buf[got] = end;

        while (at < (positive)got && count < max_out)
        {
                positive line_start = at;
                positive line_end;

                while (at < (positive)got && buf[at] != '\n')
                        at++;
                line_end = at;
                if (at < (positive)got && buf[at] == '\n')
                        at++;
                while (line_end > line_start && buf[line_end - 1] == '\r')
                        line_end--;
                if (line_end == line_start || buf[line_start] == '#')
                        continue;

                // Fields: name \t uri \t make_model \t location \t caps_hex
                const p8 address_to f[5] = {null, null, null, null, null};
                positive flen[5] = {0, 0, 0, 0, 0};
                positive fi = 0, p = line_start;

                while (fi < 5 && p <= line_end)
                {
                        positive s = p;

                        while (p < line_end && buf[p] != '\t')
                                p++;
                        f[fi] = buf + s;
                        flen[fi] = p - s;
                        fi++;
                        if (p < line_end && buf[p] == '\t')
                                p++;
                        else
                                break;
                }
                if (fi < 2 || !flen[0] || !flen[1])
                        continue;

                ipp_printer address_to pr = out + count;
                ipp_uri u;

                memory_zero(pr, sizeof(address_to pr));
                cups_copy_field(f[0], flen[0], pr->name, sizeof(pr->name));
                cups_copy_field(f[1], flen[1], pr->uri, sizeof(pr->uri));
                if (!ipp_parse_uri(pr->uri, address_of u))
                        continue;
                pr->tls = u.tls;
                pr->port = u.port;
                cups_copy_field(u.host, string_length(u.host), pr->host,
                                sizeof(pr->host));
                if (fi >= 3 && flen[2])
                        cups_copy_field(f[2], flen[2], pr->make_model,
                                        sizeof(pr->make_model));
                if (fi >= 4 && flen[3])
                        cups_copy_field(f[3], flen[3], pr->location,
                                        sizeof(pr->location));
                if (fi >= 5 && flen[4])
                        pr->caps = cups_parse_hex32(f[4], flen[4]);
                else
                        pr->caps = IPP_CAP_PDF | IPP_CAP_JPEG | IPP_CAP_PNG;
                pr->state = IPP_PRINTER_IDLE;
                pr->accepting = true;
                count++;
        }
        return count;
}

static bool cups_store_save(string_address printers_path,
                            const ipp_printer address_to printers,
                            positive count)
{
        p8 buf[8192];
        positive used = 0;
        bipolar fd;

        if (!printers_path)
                return false;
        for (positive i = 0; i < count; i++)
        {
                const ipp_printer address_to pr = printers + i;
                p8 caps_hex[16];
                positive clen = positive_into_base(caps_hex, pr->caps, 16, false);
                positive nlen = string_length(pr->name);
                positive ulen = string_length(pr->uri);
                positive mlen = string_length(pr->make_model);
                positive llen = string_length(pr->location);
                positive need = nlen + 1 + ulen + 1 + mlen + 1 + llen + 1 +
                                clen + 1;

                if (!nlen || !ulen || !cups_field_clean(pr->name) ||
                    !cups_field_clean(pr->uri) ||
                    !cups_field_clean(pr->make_model) ||
                    !cups_field_clean(pr->location))
                        continue;
                if (need > sizeof(buf) - used)
                        return false;
                memory_copy(buf + used, pr->name, nlen);
                used += nlen;
                buf[used++] = '\t';
                memory_copy(buf + used, pr->uri, ulen);
                used += ulen;
                buf[used++] = '\t';
                if (mlen)
                {
                        memory_copy(buf + used, pr->make_model, mlen);
                        used += mlen;
                }
                buf[used++] = '\t';
                if (llen)
                {
                        memory_copy(buf + used, pr->location, llen);
                        used += llen;
                }
                buf[used++] = '\t';
                memory_copy(buf + used, caps_hex, clen);
                used += clen;
                buf[used++] = '\n';
        }

        fd = system_open_at_mode(AT_FDCWD, printers_path,
                                 FILE_WRITE | FILE_CREATE | FILE_TRUNCATE |
                                     O_CLOEXEC,
                                 0644);
        if (fd < 0)
                return false;
        {
                positive sent = 0;

                while (sent < used)
                {
                        bipolar w = system_write_once(fd, buf + sent,
                                                      used - sent);

                        if (w <= 0)
                        {
                                system_close(fd);
                                return false;
                        }
                        sent += (positive)w;
                }
        }
        system_close(fd);
        return true;
}

static bool cups_store_set_default(string_address default_path,
                                   string_address name)
{
        bipolar fd;
        positive nlen;

        if (!default_path || !name || !name[0] || !cups_field_clean(name))
                return false;
        nlen = string_length(name);
        fd = system_open_at_mode(AT_FDCWD, default_path,
                                 FILE_WRITE | FILE_CREATE | FILE_TRUNCATE |
                                     O_CLOEXEC,
                                 0644);
        if (fd < 0)
                return false;
        (void)system_write_once(fd, name, nlen);
        (void)system_write_once(fd, "\n", 1);
        system_close(fd);
        return true;
}

/* ---- Local CUPS Unix Socket Shim ---- */

typedef struct
{
        b32 job_id;
        b32 remote_job_id;
        positive printer_index;
        b32 state;
        b32 copies;
        bool duplex;
        b32 size_kb;
        p8 name[96];
        p8 user[64];
        p8 mime[64];
} cups_shim_job;

typedef bipolar (*cups_shim_forward_fn)(
    address_any ctx, const ipp_printer address_to printer,
    const ipp_job_request address_to req, const p8 address_to doc,
    positive doc_len, ipp_job_result address_to res);

typedef bipolar (*cups_shim_cancel_fn)(address_any ctx,
                                       const ipp_printer address_to printer,
                                       b32 remote_job_id);

typedef struct
{
        ipp_printer printers[IPP_MAX_PRINTERS];
        positive printer_count;
        p8 default_printer[96];
        cups_shim_job jobs[CUPS_SHIM_MAX_JOBS];
        positive job_count;
        b32 next_job_id;
        string_address printers_path;
        string_address default_path;
        cups_shim_forward_fn forward_fn;
        cups_shim_cancel_fn cancel_fn;
        address_any forward_ctx;
} cups_shim_state;

static fn cups_shim_init(cups_shim_state address_to st,
                         string_address printers_path,
                         string_address default_path)
{
        memory_zero(st, sizeof(address_to st));
        st->next_job_id = 1;
        st->printers_path = printers_path;
        st->default_path = default_path;
        if (printers_path)
                st->printer_count = cups_store_load(
                    printers_path, default_path, st->printers, IPP_MAX_PRINTERS,
                    st->default_printer, sizeof(st->default_printer));
}

static fn cups_shim_reload(cups_shim_state address_to st)
{
        if (!st->printers_path)
                return;
        st->printer_count = cups_store_load(
            st->printers_path, st->default_path, st->printers, IPP_MAX_PRINTERS,
            st->default_printer, sizeof(st->default_printer));
}

static bipolar cups_shim_find_printer(const cups_shim_state address_to st,
                                      string_address uri_or_name)
{
        string_address name = uri_or_name;

        if (!st->printer_count)
                return -1;
        if (name && name[0])
        {
                // Extract tail after "/printers/" if a CUPS URI was supplied.
                string_address slash = string_find(name, (string_address)"/printers/");

                if (slash)
                        name = slash + 10;
                for (positive i = 0; i < st->printer_count; i++)
                        if (string_equals(st->printers[i].name, name) ||
                            string_equals(st->printers[i].uri, uri_or_name))
                                return (bipolar)i;
        }
        if (st->default_printer[0])
        {
                for (positive i = 0; i < st->printer_count; i++)
                        if (string_equals(st->printers[i].name,
                                          st->default_printer))
                                return (bipolar)i;
        }
        return 0;
}

static cups_shim_job address_to cups_shim_find_job(cups_shim_state address_to st,
                                                   b32 job_id)
{
        for (positive i = 0; i < st->job_count; i++)
                if (st->jobs[i].job_id == job_id)
                        return st->jobs + i;
        return null;
}

static cups_shim_job address_to cups_shim_alloc_job(cups_shim_state address_to st)
{
        cups_shim_job address_to j;

        if (st->job_count < CUPS_SHIM_MAX_JOBS)
        {
                j = st->jobs + st->job_count++;
        }
        else
        {
                for (positive i = 1; i < CUPS_SHIM_MAX_JOBS; i++)
                        st->jobs[i - 1] = st->jobs[i];
                j = st->jobs + CUPS_SHIM_MAX_JOBS - 1;
        }
        memory_zero(j, sizeof(address_to j));
        j->job_id = st->next_job_id++;
        return j;
}

static fn cups_shim_format_local_uri(string_address prefix,
                                     string_address tail, p8 address_to out,
                                     positive room)
{
        positive plen = string_length(prefix);
        positive tlen = string_length(tail);

        if (plen + tlen >= room)
        {
                if (room)
                        out[0] = end;
                return;
        }
        memory_copy(out, prefix, plen);
        memory_copy(out + plen, tail, tlen);
        out[plen + tlen] = end;
}

static fn cups_shim_write_printer_attrs(ipp_writer address_to w,
                                        const ipp_printer address_to pr,
                                        bool is_default)
{
        p8 local_uri[160];
        b32 ptype = 0x00000004; // CUPS_PRINTER_LOCAL | copies

        if (pr->caps & IPP_CAP_COLOR)
                ptype |= 0x00000008;
        if (pr->caps & IPP_CAP_DUPLEX)
                ptype |= 0x00000010;
        if (is_default)
                ptype |= 0x00020000;

        cups_shim_format_local_uri((string_address)"ipp://localhost/printers/",
                                   pr->name, local_uri, sizeof(local_uri));

        ipp_write_group(w, IPP_TAG_PRINTER);
        ipp_write_cstr(w, IPP_TAG_URI, (string_address)"printer-uri-supported",
                       local_uri);
        ipp_write_cstr(w, IPP_TAG_NAME, (string_address)"printer-name",
                       pr->name);
        ipp_write_cstr(w, IPP_TAG_TEXT, (string_address)"printer-info",
                       pr->make_model[0] ? pr->make_model : pr->name);
        ipp_write_cstr(w, IPP_TAG_TEXT,
                       (string_address)"printer-make-and-model",
                       pr->make_model[0] ? pr->make_model
                                         : (string_address)"IPP Everywhere Printer");
        ipp_write_cstr(w, IPP_TAG_TEXT, (string_address)"printer-location",
                       pr->location);
        ipp_write_cstr(w, IPP_TAG_URI, (string_address)"device-uri", pr->uri);
        ipp_write_int(w, IPP_TAG_ENUM, (string_address)"printer-state",
                      pr->state ? pr->state : IPP_PRINTER_IDLE);
        ipp_write_cstr(w, IPP_TAG_KEYWORD,
                       (string_address)"printer-state-reasons",
                       (string_address)"none");
        ipp_write_bool(w, (string_address)"printer-is-accepting-jobs",
                       pr->accepting);
        ipp_write_bool(w, (string_address)"printer-is-shared", false);
        ipp_write_int(w, IPP_TAG_ENUM, (string_address)"printer-type", ptype);
        ipp_write_cstr(w, IPP_TAG_KEYWORD,
                       (string_address)"uri-security-supported",
                       (string_address)"none");
        ipp_write_cstr(w, IPP_TAG_KEYWORD,
                       (string_address)"uri-authentication-supported",
                       (string_address)"none");
        ipp_write_cstr(w, IPP_TAG_CHARSET, (string_address)"charset-configured",
                       (string_address)"utf-8");
        ipp_write_cstr(w, IPP_TAG_CHARSET, (string_address)"charset-supported",
                       (string_address)"utf-8");
        ipp_write_cstr(w, IPP_TAG_LANGUAGE,
                       (string_address)"natural-language-configured",
                       (string_address)"en");
        ipp_write_cstr(w, IPP_TAG_LANGUAGE,
                       (string_address)"generated-natural-language-supported",
                       (string_address)"en");
        ipp_write_cstr(w, IPP_TAG_MIMETYPE,
                       (string_address)"document-format-default",
                       (string_address)"application/pdf");
        ipp_write_cstr(w, IPP_TAG_MIMETYPE,
                       (string_address)"document-format-supported",
                       (string_address)"application/pdf");
        ipp_write_cstr(w, IPP_TAG_MIMETYPE, null, (string_address)"image/png");
        ipp_write_cstr(w, IPP_TAG_MIMETYPE, null, (string_address)"image/jpeg");
        ipp_write_cstr(w, IPP_TAG_MIMETYPE, null,
                       (string_address)"image/pwg-raster");
        ipp_write_cstr(w, IPP_TAG_MIMETYPE, null, (string_address)"text/plain");
        ipp_write_cstr(w, IPP_TAG_MIMETYPE, null,
                       (string_address)"application/octet-stream");
        ipp_write_bool(w, (string_address)"color-supported",
                       (pr->caps & IPP_CAP_COLOR) != 0);
        ipp_write_cstr(w, IPP_TAG_KEYWORD, (string_address)"sides-default",
                       (string_address)"one-sided");
        ipp_write_cstr(w, IPP_TAG_KEYWORD, (string_address)"sides-supported",
                       (string_address)"one-sided");
        if (pr->caps & IPP_CAP_DUPLEX)
        {
                ipp_write_cstr(w, IPP_TAG_KEYWORD, null,
                               (string_address)"two-sided-long-edge");
                ipp_write_cstr(w, IPP_TAG_KEYWORD, null,
                               (string_address)"two-sided-short-edge");
        }
        ipp_write_cstr(w, IPP_TAG_KEYWORD, (string_address)"media-default",
                       (string_address)"iso_a4_210x297mm");
        ipp_write_cstr(w, IPP_TAG_KEYWORD, (string_address)"media-supported",
                       (string_address)"iso_a4_210x297mm");
        ipp_write_cstr(w, IPP_TAG_KEYWORD, null,
                       (string_address)"na_letter_8.5x11in");
        ipp_write_range(w, (string_address)"copies-supported", 1, 99);
        ipp_write_int(w, IPP_TAG_INTEGER, (string_address)"copies-default", 1);
        ipp_write_resolution(w, (string_address)"printer-resolution-default",
                             600, 600, IPP_RES_DPI);
        ipp_write_resolution(w, (string_address)"printer-resolution-supported",
                             600, 600, IPP_RES_DPI);
        ipp_write_int(w, IPP_TAG_ENUM, (string_address)"operations-supported",
                      IPP_OP_PRINT_JOB);
        ipp_write_int(w, IPP_TAG_ENUM, null, IPP_OP_VALIDATE_JOB);
        ipp_write_int(w, IPP_TAG_ENUM, null, IPP_OP_CREATE_JOB);
        ipp_write_int(w, IPP_TAG_ENUM, null, IPP_OP_SEND_DOCUMENT);
        ipp_write_int(w, IPP_TAG_ENUM, null, IPP_OP_CANCEL_JOB);
        ipp_write_int(w, IPP_TAG_ENUM, null, IPP_OP_GET_JOB_ATTRIBUTES);
        ipp_write_int(w, IPP_TAG_ENUM, null, IPP_OP_GET_JOBS);
        ipp_write_int(w, IPP_TAG_ENUM, null, IPP_OP_GET_PRINTER_ATTRIBUTES);
        ipp_write_int(w, IPP_TAG_ENUM, null, IPP_OP_CUPS_GET_DEFAULT);
        ipp_write_int(w, IPP_TAG_ENUM, null, IPP_OP_CUPS_GET_PRINTERS);
}

static fn cups_shim_write_job_attrs(ipp_writer address_to w,
                                    const cups_shim_state address_to st,
                                    const cups_shim_job address_to j)
{
        p8 job_uri[64], printer_uri[160], id_buf[16];
        positive id_len = positive_into(id_buf, (positive)j->job_id);

        id_buf[id_len] = end;
        cups_shim_format_local_uri((string_address)"ipp://localhost/jobs/",
                                   id_buf, job_uri, sizeof(job_uri));
        if (j->printer_index < st->printer_count)
                cups_shim_format_local_uri(
                    (string_address)"ipp://localhost/printers/",
                    st->printers[j->printer_index].name, printer_uri,
                    sizeof(printer_uri));
        else
                cups_copy_field((const p8 address_to)"ipp://localhost/printers/default",
                                32, printer_uri, sizeof(printer_uri));

        ipp_write_group(w, IPP_TAG_JOB);
        ipp_write_int(w, IPP_TAG_INTEGER, (string_address)"job-id", j->job_id);
        ipp_write_cstr(w, IPP_TAG_URI, (string_address)"job-uri", job_uri);
        ipp_write_cstr(w, IPP_TAG_URI, (string_address)"job-printer-uri",
                       printer_uri);
        ipp_write_int(w, IPP_TAG_ENUM, (string_address)"job-state", j->state);
        ipp_write_cstr(w, IPP_TAG_KEYWORD, (string_address)"job-state-reasons",
                       (string_address)"none");
        ipp_write_cstr(w, IPP_TAG_NAME, (string_address)"job-name",
                       j->name[0] ? j->name : (string_address)"Untitled");
        ipp_write_cstr(w, IPP_TAG_NAME,
                       (string_address)"job-originating-user-name",
                       j->user[0] ? j->user : (string_address)"moonwater");
        ipp_write_int(w, IPP_TAG_INTEGER, (string_address)"job-k-octets",
                      j->size_kb);
}

static bipolar cups_shim_dispatch_print(cups_shim_state address_to st,
                                        const ipp_printer address_to pr,
                                        cups_shim_job address_to j,
                                        const p8 address_to doc,
                                        positive doc_len)
{
        ipp_job_request jr = {
            .job_name = j->name[0] ? j->name : (string_address)"Moonwater Job",
            .user_name = j->user[0] ? j->user : (string_address)"moonwater",
            .mime_type = j->mime[0] ? j->mime
                                    : ipp_detect_mime(doc, doc_len, j->name),
            .copies = j->copies > 0 ? j->copies : 1,
            .duplex = j->duplex,
            .color = (pr->caps & IPP_CAP_COLOR) != 0,
        };
        ipp_job_result res;
        bipolar rc;

        memory_zero(address_of res, sizeof(res));
        j->size_kb = (b32)((doc_len + 1023) / 1024);

        if (st->forward_fn)
        {
                rc = st->forward_fn(st->forward_ctx, pr, address_of jr, doc,
                                    doc_len, address_of res);
        }
        else
        {
                ipp_uri u;

                if (!ipp_parse_uri(pr->uri, address_of u))
                        return IPP_ERR_URI;
                rc = ipp_print_job(address_of u, pr->ip, false, address_of jr,
                                   doc, doc_len, -1, 0, address_of res);
        }

        if (rc == IPP_OK)
        {
                j->remote_job_id = res.job_id;
                j->state = res.job_state ? res.job_state : IPP_JOB_PROCESSING;
        }
        else
        {
                j->state = IPP_JOB_ABORTED;
        }
        return rc;
}

static fn cups_shim_reply_header(ipp_writer address_to w, p8 address_to out,
                                 positive room, p8 ver_major, p8 ver_minor,
                                 p16 status, p32 req_id)
{
        ipp_write_begin(w, out, room, ver_major, ver_minor, status, req_id);
        ipp_write_group(w, IPP_TAG_OPERATION);
        ipp_write_cstr(w, IPP_TAG_CHARSET, (string_address)"attributes-charset",
                       (string_address)"utf-8");
        ipp_write_cstr(w, IPP_TAG_LANGUAGE,
                       (string_address)"attributes-natural-language",
                       (string_address)"en");
}

// Handle one decoded IPP request body (IPP + optional trailing document data).
// Writes the IPP response into `resp_buf` and returns its byte length (0 on encode failure).
static positive cups_shim_handle_ipp(cups_shim_state address_to st,
                                     const p8 address_to req, positive req_len,
                                     p8 address_to resp_buf,
                                     positive resp_room)
{
        ipp_reader r;
        ipp_attr a;
        ipp_writer w;
        p8 printer_uri[256] = {0};
        p8 job_name[96] = {0};
        p8 user_name[64] = {0};
        p8 doc_format[64] = {0};
        p8 which_jobs[32] = {0};
        b32 job_id = 0;
        b32 copies = 1;
        bool duplex = false;

        if (!ipp_read_open(address_of r, req, req_len))
        {
                cups_shim_reply_header(address_of w, resp_buf, resp_room, 2, 0,
                                       IPP_STATUS_CLIENT_BAD_REQUEST, 0);
                return ipp_write_end(address_of w);
        }

        while (ipp_read_next(address_of r, address_of a))
        {
                if (ipp_attr_is(address_of a, (string_address)"printer-uri"))
                        ipp_attr_copy_str(address_of a, printer_uri,
                                          sizeof(printer_uri));
                else if (ipp_attr_is(address_of a, (string_address)"job-name"))
                        ipp_attr_copy_str(address_of a, job_name,
                                          sizeof(job_name));
                else if (ipp_attr_is(address_of a,
                                     (string_address)"requesting-user-name"))
                        ipp_attr_copy_str(address_of a, user_name,
                                          sizeof(user_name));
                else if (ipp_attr_is(address_of a,
                                     (string_address)"document-format"))
                        ipp_attr_copy_str(address_of a, doc_format,
                                          sizeof(doc_format));
                else if (ipp_attr_is(address_of a, (string_address)"which-jobs"))
                        ipp_attr_copy_str(address_of a, which_jobs,
                                          sizeof(which_jobs));
                else if (ipp_attr_is(address_of a, (string_address)"job-id"))
                        (void)ipp_attr_i32(address_of a, address_of job_id);
                else if (ipp_attr_is(address_of a, (string_address)"copies"))
                        (void)ipp_attr_i32(address_of a, address_of copies);
                else if (ipp_attr_is(address_of a, (string_address)"sides"))
                {
                        if (ipp_attr_value_eq(
                                address_of a,
                                (string_address)"two-sided-long-edge") ||
                            ipp_attr_value_eq(
                                address_of a,
                                (string_address)"two-sided-short-edge"))
                                duplex = true;
                }
        }

        if (r.failed || !r.ended)
        {
                cups_shim_reply_header(address_of w, resp_buf, resp_room,
                                       r.ver_major, r.ver_minor,
                                       IPP_STATUS_CLIENT_BAD_REQUEST,
                                       r.request_id);
                return ipp_write_end(address_of w);
        }

        const p8 address_to doc = req + r.payload_offset;
        positive doc_len = req_len - r.payload_offset;

        cups_shim_reload(st);

        switch (r.op_or_status)
        {
        case IPP_OP_CUPS_GET_PRINTERS:
        {
                if (!st->printer_count)
                {
                        cups_shim_reply_header(
                            address_of w, resp_buf, resp_room, r.ver_major,
                            r.ver_minor, IPP_STATUS_CLIENT_NOT_FOUND,
                            r.request_id);
                        return ipp_write_end(address_of w);
                }
                cups_shim_reply_header(address_of w, resp_buf, resp_room,
                                       r.ver_major, r.ver_minor, IPP_STATUS_OK,
                                       r.request_id);
                for (positive i = 0; i < st->printer_count; i++)
                {
                        bool is_def =
                            st->default_printer[0]
                                ? string_equals(st->printers[i].name,
                                                st->default_printer)
                                : (i == 0);
                        cups_shim_write_printer_attrs(
                            address_of w, st->printers + i, is_def);
                }
                return ipp_write_end(address_of w);
        }

        case IPP_OP_CUPS_GET_DEFAULT:
        {
                bipolar idx = cups_shim_find_printer(st, null);

                if (idx < 0)
                {
                        cups_shim_reply_header(
                            address_of w, resp_buf, resp_room, r.ver_major,
                            r.ver_minor, IPP_STATUS_CLIENT_NOT_FOUND,
                            r.request_id);
                        return ipp_write_end(address_of w);
                }
                cups_shim_reply_header(address_of w, resp_buf, resp_room,
                                       r.ver_major, r.ver_minor, IPP_STATUS_OK,
                                       r.request_id);
                cups_shim_write_printer_attrs(address_of w, st->printers + idx,
                                              true);
                return ipp_write_end(address_of w);
        }

        case IPP_OP_GET_PRINTER_ATTRIBUTES:
        {
                bipolar idx = cups_shim_find_printer(st, printer_uri);

                if (idx < 0)
                {
                        cups_shim_reply_header(
                            address_of w, resp_buf, resp_room, r.ver_major,
                            r.ver_minor, IPP_STATUS_CLIENT_NOT_FOUND,
                            r.request_id);
                        return ipp_write_end(address_of w);
                }
                cups_shim_reply_header(address_of w, resp_buf, resp_room,
                                       r.ver_major, r.ver_minor, IPP_STATUS_OK,
                                       r.request_id);
                cups_shim_write_printer_attrs(
                    address_of w, st->printers + idx,
                    st->default_printer[0]
                        ? string_equals(st->printers[idx].name,
                                        st->default_printer)
                        : (idx == 0));
                return ipp_write_end(address_of w);
        }

        case IPP_OP_VALIDATE_JOB:
        {
                bipolar idx = cups_shim_find_printer(st, printer_uri);

                cups_shim_reply_header(
                    address_of w, resp_buf, resp_room, r.ver_major, r.ver_minor,
                    idx >= 0 ? IPP_STATUS_OK : IPP_STATUS_CLIENT_NOT_FOUND,
                    r.request_id);
                return ipp_write_end(address_of w);
        }

        case IPP_OP_CREATE_JOB:
        {
                bipolar idx = cups_shim_find_printer(st, printer_uri);

                if (idx < 0)
                {
                        cups_shim_reply_header(
                            address_of w, resp_buf, resp_room, r.ver_major,
                            r.ver_minor, IPP_STATUS_CLIENT_NOT_FOUND,
                            r.request_id);
                        return ipp_write_end(address_of w);
                }
                cups_shim_job address_to j = cups_shim_alloc_job(st);
                j->printer_index = (positive)idx;
                j->state = IPP_JOB_PENDING;
                j->copies = copies > 0 ? copies : 1;
                j->duplex = duplex;
                cups_copy_field(job_name, string_length(job_name), j->name,
                                sizeof(j->name));
                cups_copy_field(user_name, string_length(user_name), j->user,
                                sizeof(j->user));
                cups_copy_field(doc_format, string_length(doc_format), j->mime,
                                sizeof(j->mime));

                cups_shim_reply_header(address_of w, resp_buf, resp_room,
                                       r.ver_major, r.ver_minor, IPP_STATUS_OK,
                                       r.request_id);
                cups_shim_write_job_attrs(address_of w, st, j);
                return ipp_write_end(address_of w);
        }

        case IPP_OP_SEND_DOCUMENT:
        {
                cups_shim_job address_to j = cups_shim_find_job(st, job_id);

                if (!j || j->printer_index >= st->printer_count)
                {
                        cups_shim_reply_header(
                            address_of w, resp_buf, resp_room, r.ver_major,
                            r.ver_minor, IPP_STATUS_CLIENT_NOT_FOUND,
                            r.request_id);
                        return ipp_write_end(address_of w);
                }
                if (doc_format[0])
                        cups_copy_field(doc_format, string_length(doc_format),
                                        j->mime, sizeof(j->mime));
                {
                        bipolar rc = cups_shim_dispatch_print(
                            st, st->printers + j->printer_index, j, doc,
                            doc_len);
                        cups_shim_reply_header(
                            address_of w, resp_buf, resp_room, r.ver_major,
                            r.ver_minor,
                            rc == IPP_OK ? IPP_STATUS_OK
                                         : IPP_STATUS_SERVER_INTERNAL_ERROR,
                            r.request_id);
                        cups_shim_write_job_attrs(address_of w, st, j);
                        return ipp_write_end(address_of w);
                }
        }

        case IPP_OP_PRINT_JOB:
        {
                bipolar idx = cups_shim_find_printer(st, printer_uri);

                if (idx < 0)
                {
                        cups_shim_reply_header(
                            address_of w, resp_buf, resp_room, r.ver_major,
                            r.ver_minor, IPP_STATUS_CLIENT_NOT_FOUND,
                            r.request_id);
                        return ipp_write_end(address_of w);
                }
                cups_shim_job address_to j = cups_shim_alloc_job(st);
                j->printer_index = (positive)idx;
                j->state = IPP_JOB_PENDING;
                j->copies = copies > 0 ? copies : 1;
                j->duplex = duplex;
                cups_copy_field(job_name, string_length(job_name), j->name,
                                sizeof(j->name));
                cups_copy_field(user_name, string_length(user_name), j->user,
                                sizeof(j->user));
                cups_copy_field(doc_format, string_length(doc_format), j->mime,
                                sizeof(j->mime));
                {
                        bipolar rc = cups_shim_dispatch_print(
                            st, st->printers + idx, j, doc, doc_len);
                        cups_shim_reply_header(
                            address_of w, resp_buf, resp_room, r.ver_major,
                            r.ver_minor,
                            rc == IPP_OK ? IPP_STATUS_OK
                                         : IPP_STATUS_SERVER_INTERNAL_ERROR,
                            r.request_id);
                        cups_shim_write_job_attrs(address_of w, st, j);
                        return ipp_write_end(address_of w);
                }
        }

        case IPP_OP_GET_JOB_ATTRIBUTES:
        {
                cups_shim_job address_to j = cups_shim_find_job(st, job_id);

                if (!j)
                {
                        cups_shim_reply_header(
                            address_of w, resp_buf, resp_room, r.ver_major,
                            r.ver_minor, IPP_STATUS_CLIENT_NOT_FOUND,
                            r.request_id);
                        return ipp_write_end(address_of w);
                }
                cups_shim_reply_header(address_of w, resp_buf, resp_room,
                                       r.ver_major, r.ver_minor, IPP_STATUS_OK,
                                       r.request_id);
                cups_shim_write_job_attrs(address_of w, st, j);
                return ipp_write_end(address_of w);
        }

        case IPP_OP_GET_JOBS:
        {
                bool want_completed =
                    string_equals(which_jobs, (string_address)"completed");
                bool want_all =
                    string_equals(which_jobs, (string_address)"all");

                cups_shim_reply_header(address_of w, resp_buf, resp_room,
                                       r.ver_major, r.ver_minor, IPP_STATUS_OK,
                                       r.request_id);
                for (positive i = 0; i < st->job_count; i++)
                {
                        cups_shim_job address_to j = st->jobs + i;
                        bool done = j->state >= IPP_JOB_CANCELED;

                        if (want_all || (want_completed ? done : !done))
                                cups_shim_write_job_attrs(address_of w, st, j);
                }
                return ipp_write_end(address_of w);
        }

        case IPP_OP_CANCEL_JOB:
        {
                cups_shim_job address_to j = cups_shim_find_job(st, job_id);

                if (!j)
                {
                        cups_shim_reply_header(
                            address_of w, resp_buf, resp_room, r.ver_major,
                            r.ver_minor, IPP_STATUS_CLIENT_NOT_FOUND,
                            r.request_id);
                        return ipp_write_end(address_of w);
                }
                if (j->remote_job_id > 0 && j->printer_index < st->printer_count)
                {
                        if (st->cancel_fn)
                                (void)st->cancel_fn(
                                    st->forward_ctx,
                                    st->printers + j->printer_index,
                                    j->remote_job_id);
                        else
                        {
                                ipp_uri u;

                                if (ipp_parse_uri(
                                        st->printers[j->printer_index].uri,
                                        address_of u))
                                        (void)ipp_cancel_job(
                                            address_of u,
                                            st->printers[j->printer_index].ip,
                                            false, j->remote_job_id, null);
                        }
                }
                j->state = IPP_JOB_CANCELED;
                cups_shim_reply_header(address_of w, resp_buf, resp_room,
                                       r.ver_major, r.ver_minor, IPP_STATUS_OK,
                                       r.request_id);
                return ipp_write_end(address_of w);
        }

        default:
                cups_shim_reply_header(address_of w, resp_buf, resp_room,
                                       r.ver_major, r.ver_minor,
                                       IPP_STATUS_SERVER_NOT_SUPPORTED,
                                       r.request_id);
                return ipp_write_end(address_of w);
        }
}

/* ---- HTTP/1.1 Server Loop for Unix Socket Clients ---- */

static fn cups_shim_send_http(b32 fd, string_address status_line,
                              string_address content_type,
                              const p8 address_to body, positive body_len)
{
        p8 hdr[512];
        p8 len_buf[24];
        positive llen = positive_into(len_buf, body_len);
        positive p = 0;
        static const string_address s_h1 = "HTTP/1.1 ";
        static const string_address s_ct = "\r\nContent-Type: ";
        static const string_address s_cl = "\r\nContent-Length: ";
        static const string_address s_end = "\r\nConnection: close\r\n\r\n";

        memory_copy(hdr + p, s_h1, string_length(s_h1));
        p += string_length(s_h1);
        memory_copy(hdr + p, status_line, string_length(status_line));
        p += string_length(status_line);
        if (content_type)
        {
                memory_copy(hdr + p, s_ct, string_length(s_ct));
                p += string_length(s_ct);
                memory_copy(hdr + p, content_type, string_length(content_type));
                p += string_length(content_type);
        }
        memory_copy(hdr + p, s_cl, string_length(s_cl));
        p += string_length(s_cl);
        memory_copy(hdr + p, len_buf, llen);
        p += llen;
        memory_copy(hdr + p, s_end, string_length(s_end));
        p += string_length(s_end);

        (void)network_stream_send_all_for(fd, hdr, p, 10, 0);
        if (body && body_len)
                (void)network_stream_send_all_for(fd, (p8 address_to)body,
                                                  body_len, 10, 0);
}

static fn cups_shim_serve_client(cups_shim_state address_to st, b32 fd)
{
        p8 head[CUPS_SHIM_HEAD_MAX];
        positive used = 0;
        bipolar header_end = -1;
        bool is_chunked = false;
        bool has_length = false;
        positive content_length = 0;
        bool expect_continue = false;
        http_buffer body_store = {0};

        // Read HTTP request headers
        while (used < sizeof(head))
        {
                bipolar r = network_stream_read_some_for(
                    fd, head + used, sizeof(head) - used, 10, 0);
                if (r <= 0)
                        return;
                used += (positive)r;
                header_end = http_header_end(head, used);
                if (header_end >= 0)
                        break;
        }
        if (header_end < 0)
        {
                cups_shim_send_http(fd, (string_address)"400 Bad Request", null,
                                    null, 0);
                return;
        }

        // Only POST is used for IPP
        if (header_end < 5 || memory_compare(head, "POST ", 5) != 0)
        {
                cups_shim_send_http(fd, (string_address)"405 Method Not Allowed",
                                    null, null, 0);
                return;
        }

        // Parse Content-Length, Transfer-Encoding, Expect
        {
                positive cl_len = 0, te_len = 0, ex_len = 0;
                string_address cl = http_header(
                    head, (positive)header_end, (string_address)"Content-Length",
                    address_of cl_len, null);
                string_address te = http_header(
                    head, (positive)header_end,
                    (string_address)"Transfer-Encoding", address_of te_len, null);
                string_address ex = http_header(
                    head, (positive)header_end, (string_address)"Expect",
                    address_of ex_len, null);

                if (ex && ex_len >= 12 &&
                    !memory_compare_ascii_case(ex, "100-continue", 12))
                        expect_continue = true;
                if (te && te_len >= 7 &&
                    !memory_compare_ascii_case(te, "chunked", 7))
                        is_chunked = true;
                else if (cl && cl_len)
                {
                        positive v = 0;
                        bool ok = true;

                        for (positive i = 0; i < cl_len; i++)
                        {
                                if (cl[i] == ' ' || cl[i] == '\t')
                                        continue;
                                if (cl[i] < '0' || cl[i] > '9' ||
                                    v > CUPS_SHIM_MAX_BODY / 10)
                                {
                                        ok = false;
                                        break;
                                }
                                v = v * 10 + (positive)(cl[i] - '0');
                        }
                        if (!ok || v > CUPS_SHIM_MAX_BODY)
                        {
                                cups_shim_send_http(
                                    fd, (string_address)"413 Payload Too Large",
                                    null, null, 0);
                                return;
                        }
                        has_length = true;
                        content_length = v;
                }
        }

        if (expect_continue)
        {
                static const p8 cont[] = "HTTP/1.1 100 Continue\r\n\r\n";

                (void)network_stream_send_all_for(fd, (p8 address_to)cont,
                                                  sizeof(cont) - 1, 5, 0);
        }

        // Read HTTP body (Content-Length or chunked) using net.c's http_copy_body
        {
                http_link link = {.handle = fd, .tls = false};
                http_response fake_resp = {
                    .code = 200,
                    .body_kind = is_chunked
                                     ? HTTP_BODY_CHUNKED
                                     : (has_length ? HTTP_BODY_LENGTH
                                                   : HTTP_BODY_CLOSE),
                    .body_length = content_length,
                };
                p8 scratch[HTTP_HEAD_MAX];
                http_body body = {
                    .link = address_of link,
                    .stash = head + header_end,
                    .stash_used = used - (positive)header_end,
                    .scratch = scratch,
                    .store = address_of body_store,
                    .store_limit = CUPS_SHIM_MAX_BODY,
                    .undeclared_most = CUPS_SHIM_MAX_BODY,
                };

                if (http_copy_body(address_of body, -1, address_of fake_resp) !=
                    HTTP_OK)
                {
                        http_forget(address_of body_store);
                        cups_shim_send_http(fd,
                                            (string_address)"400 Bad Request",
                                            null, null, 0);
                        return;
                }
        }

        {
                p8 resp_buf[16384];
                positive rlen = cups_shim_handle_ipp(
                    st, body_store.bytes, body_store.used, resp_buf,
                    sizeof(resp_buf));

                http_forget(address_of body_store);
                if (!rlen)
                {
                        cups_shim_send_http(
                            fd, (string_address)"500 Internal Server Error",
                            null, null, 0);
                        return;
                }
                cups_shim_send_http(fd, (string_address)"200 OK",
                                    (string_address)"application/ipp", resp_buf,
                                    rlen);
        }
}

static bipolar cups_shim_listen(string_address socket_path)
{
        struct
        {
                p16 family;
                p8 path[108];
        } addr;
        positive plen = string_length(socket_path);
        bipolar fd;

        if (!plen || plen >= sizeof(addr.path))
                return -1;
        (void)system_call_3(syscall(unlinkat), AT_FDCWD, (positive)socket_path,
                            0);
        fd = socket_new(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0)
                return -1;
        memory_zero(address_of addr, sizeof(addr));
        addr.family = AF_UNIX;
        memory_copy(addr.path, socket_path, plen + 1);
        if (socket_bind((b32)fd, address_of addr, sizeof(addr)) < 0 ||
            socket_listen((b32)fd, 16) < 0)
        {
                socket_close((b32)fd);
                return -1;
        }
        (void)system_call_4(syscall(fchmodat), AT_FDCWD, (positive)socket_path,
                            0666, 0);
        return fd;
}

static b32 cups_shim_run(cups_shim_state address_to st,
                         string_address socket_path, positive max_requests)
{
        bipolar lfd = cups_shim_listen(socket_path);
        positive served = 0;

        if (lfd < 0)
                return 1;
        for (;;)
        {
                bipolar cfd = socket_accept((b32)lfd, null, null, SOCK_CLOEXEC);

                if (cfd < 0)
                        continue;
                cups_shim_serve_client(st, (b32)cfd);
                socket_close((b32)cfd);
                served++;
                if (max_requests && served >= max_requests)
                        break;
        }
        socket_close((b32)lfd);
        (void)system_call_3(syscall(unlinkat), AT_FDCWD, (positive)socket_path,
                            0);
        return 0;
}

#endif /* STANDARD_MODERN_C_NET_CUPS_SHIM */
