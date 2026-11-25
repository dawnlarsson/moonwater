/*
        ipp.c: native IPP Everywhere (RFC 8010/8011, PWG 5100.14) core and
        mDNS (_ipp._tcp / _ipps._tcp) printer discovery.

        Freestanding, bounded, zero-daemon printing core:
          - RFC 8010 binary IPP encoder and hardened decoder (strict bounds,
            fixed-size value validation, 1setOf inheritance, attribute count
            ceiling, and payload offset reporting for document data).
          - IPP / IPPS / HTTP / HTTPS URI parser hardened against HTTP
            header injection.
          - DNS-SD / mDNS _ipp._tcp.local and _ipps._tcp.local query builder
            and response parser (PTR, SRV, TXT, A) using net.c's bounded
            dns_copy_name.
          - IPP-over-HTTP(S) client reusing net.c's http_link, TLS 1.3, and
            HTTP/1.1 framing (Content-Length, chunked, close-delimited), with
            streaming file-descriptor document upload for Print-Job.

        Include after src/net/net.c.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef STANDARD_MODERN_C_NET_IPP
#define STANDARD_MODERN_C_NET_IPP

/* ---- IPP Wire Constants (RFC 8010 / RFC 8011 / CUPS Extensions) ---- */

#define IPP_VERSION_1_1_MAJOR 1
#define IPP_VERSION_1_1_MINOR 1
#define IPP_VERSION_2_0_MAJOR 2
#define IPP_VERSION_2_0_MINOR 0

#define IPP_PORT_DEFAULT 631
#define IPP_MAX_ATTRIBUTES 4096u
#define IPP_MAX_REPLY_BYTES (256u * 1024u)
#define IPP_MAX_PRINTERS 16u
#define IPP_MAX_JOBS 32u

// Delimiter tags (0x00..0x0f)
#define IPP_TAG_OPERATION 0x01
#define IPP_TAG_JOB 0x02
#define IPP_TAG_END 0x03
#define IPP_TAG_PRINTER 0x04
#define IPP_TAG_UNSUPPORTED_GROUP 0x05
#define IPP_TAG_SUBSCRIPTION 0x06
#define IPP_TAG_EVENT_NOTIFICATION 0x07

// Value tags (0x10..0x7f)
#define IPP_TAG_UNSUPPORTED 0x10
#define IPP_TAG_UNKNOWN 0x12
#define IPP_TAG_NOVALUE 0x13
#define IPP_TAG_INTEGER 0x21
#define IPP_TAG_BOOLEAN 0x22
#define IPP_TAG_ENUM 0x23
#define IPP_TAG_STRING 0x30
#define IPP_TAG_DATETIME 0x31
#define IPP_TAG_RESOLUTION 0x32
#define IPP_TAG_RANGE 0x33
#define IPP_TAG_BEGIN_COLLECTION 0x34
#define IPP_TAG_TEXTLANG 0x35
#define IPP_TAG_NAMELANG 0x36
#define IPP_TAG_END_COLLECTION 0x37
#define IPP_TAG_TEXT 0x41
#define IPP_TAG_NAME 0x42
#define IPP_TAG_KEYWORD 0x44
#define IPP_TAG_URI 0x45
#define IPP_TAG_URISCHEME 0x46
#define IPP_TAG_CHARSET 0x47
#define IPP_TAG_LANGUAGE 0x48
#define IPP_TAG_MIMETYPE 0x49
#define IPP_TAG_MEMBERNAME 0x4a

// Operation IDs
#define IPP_OP_PRINT_JOB 0x0002
#define IPP_OP_VALIDATE_JOB 0x0004
#define IPP_OP_CREATE_JOB 0x0005
#define IPP_OP_SEND_DOCUMENT 0x0006
#define IPP_OP_CANCEL_JOB 0x0008
#define IPP_OP_GET_JOB_ATTRIBUTES 0x0009
#define IPP_OP_GET_JOBS 0x000a
#define IPP_OP_GET_PRINTER_ATTRIBUTES 0x000b
#define IPP_OP_CUPS_GET_DEFAULT 0x4001
#define IPP_OP_CUPS_GET_PRINTERS 0x4002
#define IPP_OP_CUPS_GET_DEVICES 0x400b

// Status Codes (RFC 8011 section 13.1)
#define IPP_STATUS_OK 0x0000
#define IPP_STATUS_OK_IGNORED 0x0001
#define IPP_STATUS_OK_CONFLICTING 0x0002
#define IPP_STATUS_CLIENT_BAD_REQUEST 0x0400
#define IPP_STATUS_CLIENT_FORBIDDEN 0x0401
#define IPP_STATUS_CLIENT_NOT_AUTHENTICATED 0x0402
#define IPP_STATUS_CLIENT_NOT_AUTHORIZED 0x0403
#define IPP_STATUS_CLIENT_NOT_POSSIBLE 0x0404
#define IPP_STATUS_CLIENT_TIMEOUT 0x0405
#define IPP_STATUS_CLIENT_NOT_FOUND 0x0406
#define IPP_STATUS_CLIENT_GONE 0x0407
#define IPP_STATUS_CLIENT_DOCUMENT_FORMAT 0x040a
#define IPP_STATUS_CLIENT_ATTRIBUTES 0x040b
#define IPP_STATUS_SERVER_INTERNAL_ERROR 0x0500
#define IPP_STATUS_SERVER_NOT_SUPPORTED 0x0501
#define IPP_STATUS_SERVER_SERVICE_UNAVAILABLE 0x0502
#define IPP_STATUS_SERVER_VERSION_NOT_SUPPORTED 0x0503
#define IPP_STATUS_SERVER_BUSY 0x0507

// Printer states
#define IPP_PRINTER_IDLE 3
#define IPP_PRINTER_PROCESSING 4
#define IPP_PRINTER_STOPPED 5

// Job states
#define IPP_JOB_PENDING 3
#define IPP_JOB_HELD 4
#define IPP_JOB_PROCESSING 5
#define IPP_JOB_STOPPED 6
#define IPP_JOB_CANCELED 7
#define IPP_JOB_ABORTED 8
#define IPP_JOB_COMPLETED 9

// Resolution units
#define IPP_RES_DPI 3
#define IPP_RES_DPCM 4

// Printer capability bitmask
#define IPP_CAP_PDF (1u << 0)
#define IPP_CAP_PWG_RASTER (1u << 1)
#define IPP_CAP_URF (1u << 2)
#define IPP_CAP_JPEG (1u << 3)
#define IPP_CAP_PNG (1u << 4)
#define IPP_CAP_COLOR (1u << 5)
#define IPP_CAP_DUPLEX (1u << 6)
#define IPP_CAP_TEXT (1u << 7)

// Client error codes (negative; 0 = OK)
#define IPP_OK 0
#define IPP_ERR_URI (-1)
#define IPP_ERR_ENCODE (-2)
#define IPP_ERR_RESOLVE (-3)
#define IPP_ERR_CONNECT (-4)
#define IPP_ERR_IO (-5)
#define IPP_ERR_HTTP (-6)
#define IPP_ERR_DECODE (-7)
#define IPP_ERR_STATUS (-8)

static inline bool ipp_status_ok(p16 status)
{
        return status < 0x0100;
}

static string_address ipp_printer_state_name(b32 state)
{
        if (state == IPP_PRINTER_IDLE)
                return (string_address)"idle";
        if (state == IPP_PRINTER_PROCESSING)
                return (string_address)"printing";
        if (state == IPP_PRINTER_STOPPED)
                return (string_address)"stopped";
        return (string_address)"unknown";
}

static string_address ipp_job_state_name(b32 state)
{
        switch (state)
        {
        case IPP_JOB_PENDING:
                return (string_address)"pending";
        case IPP_JOB_HELD:
                return (string_address)"held";
        case IPP_JOB_PROCESSING:
                return (string_address)"processing";
        case IPP_JOB_STOPPED:
                return (string_address)"stopped";
        case IPP_JOB_CANCELED:
                return (string_address)"canceled";
        case IPP_JOB_ABORTED:
                return (string_address)"aborted";
        case IPP_JOB_COMPLETED:
                return (string_address)"completed";
        default:
                return (string_address)"unknown";
        }
}

/* ---- Bounded IPP Binary Encoder (RFC 8010) ---- */

typedef struct
{
        p8 address_to bytes;
        positive room;
        positive used;
        bool failed;
} ipp_writer;

static bool ipp_write_room(ipp_writer address_to w, positive need)
{
        if (w->failed || need > w->room - w->used)
        {
                w->failed = true;
                return false;
        }
        return true;
}

static fn ipp_write_u8(ipp_writer address_to w, p8 v)
{
        if (!ipp_write_room(w, 1))
                return;
        w->bytes[w->used++] = v;
}

static fn ipp_write_u16(ipp_writer address_to w, p16 v)
{
        if (!ipp_write_room(w, 2))
                return;
        network_store_16(w->bytes + w->used, v);
        w->used += 2;
}

static fn ipp_write_u32(ipp_writer address_to w, p32 v)
{
        if (!ipp_write_room(w, 4))
                return;
        network_store_32(w->bytes + w->used, v);
        w->used += 4;
}

static fn ipp_write_raw(ipp_writer address_to w, const p8 address_to data,
                        positive len)
{
        if (!len)
                return;
        if (!data || !ipp_write_room(w, len))
        {
                w->failed = true;
                return;
        }
        memory_copy(w->bytes + w->used, data, len);
        w->used += len;
}

static fn ipp_write_begin(ipp_writer address_to w, p8 address_to buf,
                          positive room, p8 ver_major, p8 ver_minor,
                          p16 op_or_status, p32 request_id)
{
        w->bytes = buf;
        w->room = room;
        w->used = 0;
        w->failed = false;
        if (!buf || room < 9)
        {
                w->failed = true;
                return;
        }
        ipp_write_u8(w, ver_major);
        ipp_write_u8(w, ver_minor);
        ipp_write_u16(w, op_or_status);
        ipp_write_u32(w, request_id);
}

static fn ipp_write_group(ipp_writer address_to w, p8 group_tag)
{
        if (group_tag < 0x01 || group_tag > 0x0f || group_tag == IPP_TAG_END)
        {
                w->failed = true;
                return;
        }
        ipp_write_u8(w, group_tag);
}

static fn ipp_write_attr(ipp_writer address_to w, p8 tag,
                         string_address name, const p8 address_to val,
                         positive val_len)
{
        positive name_len = name ? string_length(name) : 0;

        if (tag < 0x10 || name_len > 0x7fff || val_len > 0x7fff)
        {
                w->failed = true;
                return;
        }
        if (!ipp_write_room(w, 1 + 2 + name_len + 2 + val_len))
                return;
        ipp_write_u8(w, tag);
        ipp_write_u16(w, (p16)name_len);
        ipp_write_raw(w, (const p8 address_to)name, name_len);
        ipp_write_u16(w, (p16)val_len);
        ipp_write_raw(w, val, val_len);
}

static fn ipp_write_str(ipp_writer address_to w, p8 tag, string_address name,
                        const p8 address_to val, positive val_len)
{
        ipp_write_attr(w, tag, name, val, val_len);
}

static fn ipp_write_cstr(ipp_writer address_to w, p8 tag, string_address name,
                         string_address val)
{
        positive len = val ? string_length(val) : 0;

        ipp_write_attr(w, tag, name, (const p8 address_to)val, len);
}

static fn ipp_write_int(ipp_writer address_to w, p8 tag, string_address name,
                        b32 val)
{
        p8 buf[4];

        network_store_32(buf, (p32)val);
        ipp_write_attr(w, tag, name, buf, 4);
}

static fn ipp_write_bool(ipp_writer address_to w, string_address name, bool val)
{
        p8 b = val ? 1 : 0;

        ipp_write_attr(w, IPP_TAG_BOOLEAN, name, address_of b, 1);
}

static fn ipp_write_range(ipp_writer address_to w, string_address name,
                          b32 lower, b32 upper)
{
        p8 buf[8];

        network_store_32(buf, (p32)lower);
        network_store_32(buf + 4, (p32)upper);
        ipp_write_attr(w, IPP_TAG_RANGE, name, buf, 8);
}

static fn ipp_write_resolution(ipp_writer address_to w, string_address name,
                               b32 cross, b32 feed, p8 units)
{
        p8 buf[9];

        network_store_32(buf, (p32)cross);
        network_store_32(buf + 4, (p32)feed);
        buf[8] = units;
        ipp_write_attr(w, IPP_TAG_RESOLUTION, name, buf, 9);
}

static positive ipp_write_end(ipp_writer address_to w)
{
        ipp_write_u8(w, IPP_TAG_END);
        return w->failed ? 0 : w->used;
}

static fn ipp_write_op_header(ipp_writer address_to w, p8 ver_major,
                              p8 ver_minor, p16 op, p32 request_id,
                              string_address printer_uri)
{
        ipp_write_group(w, IPP_TAG_OPERATION);
        ipp_write_cstr(w, IPP_TAG_CHARSET, (string_address)"attributes-charset",
                       (string_address)"utf-8");
        ipp_write_cstr(w, IPP_TAG_LANGUAGE,
                       (string_address)"attributes-natural-language",
                       (string_address)"en");
        if (printer_uri && printer_uri[0])
                ipp_write_cstr(w, IPP_TAG_URI, (string_address)"printer-uri",
                               printer_uri);
        (void)ver_major;
        (void)ver_minor;
        (void)op;
        (void)request_id;
}

/* ---- Hardened IPP Binary Decoder (RFC 8010) ---- */

typedef struct
{
        const p8 address_to bytes;
        positive length;
        positive at;
        p8 ver_major;
        p8 ver_minor;
        p16 op_or_status;
        p32 request_id;
        p8 group;
        const p8 address_to last_name;
        positive last_name_len;
        positive attr_count;
        positive payload_offset;
        bool ended;
        bool failed;
} ipp_reader;

typedef struct
{
        p8 group;
        p8 tag;
        const p8 address_to name;
        positive name_len;
        const p8 address_to value;
        positive value_len;
        bool is_additional;
} ipp_attr;

static bool ipp_read_open(ipp_reader address_to r, const p8 address_to bytes,
                          positive length)
{
        memory_zero(r, sizeof(address_to r));
        if (!bytes || length < 9)
        {
                r->failed = true;
                return false;
        }
        r->bytes = bytes;
        r->length = length;
        r->ver_major = bytes[0];
        r->ver_minor = bytes[1];
        if (r->ver_major < 1 || r->ver_major > 2)
        {
                r->failed = true;
                return false;
        }
        r->op_or_status = network_load_16((p8 address_to)bytes + 2);
        r->request_id = network_load_32((p8 address_to)bytes + 4);
        r->at = 8;
        return true;
}

static bool ipp_read_next(ipp_reader address_to r, ipp_attr address_to out)
{
        memory_zero(out, sizeof(address_to out));
        if (r->failed || r->ended)
                return false;

        for (;;)
        {
                p8 tag;
                p16 name_len, val_len;

                if (r->at >= r->length)
                {
                        r->failed = true;
                        return false;
                }
                tag = r->bytes[r->at++];
                if (tag == IPP_TAG_END)
                {
                        r->ended = true;
                        r->payload_offset = r->at;
                        return false;
                }
                if (tag < 0x10)
                {
                        if (tag == 0x00)
                        {
                                r->failed = true;
                                return false;
                        }
                        r->group = tag;
                        r->last_name = null;
                        r->last_name_len = 0;
                        continue;
                }
                if (!r->group)
                {
                        r->failed = true;
                        return false;
                }
                if (r->attr_count >= IPP_MAX_ATTRIBUTES)
                {
                        r->failed = true;
                        return false;
                }
                if (r->length - r->at < 2)
                {
                        r->failed = true;
                        return false;
                }
                name_len = network_load_16((p8 address_to)r->bytes + r->at);
                r->at += 2;
                if (name_len > r->length - r->at)
                {
                        r->failed = true;
                        return false;
                }
                if (name_len)
                {
                        out->name = r->bytes + r->at;
                        out->name_len = name_len;
                        out->is_additional = false;
                        r->last_name = out->name;
                        r->last_name_len = name_len;
                        r->at += name_len;
                }
                else
                {
                        // Multi-valued (1setOf) or collection member/end.
                        if (tag != IPP_TAG_END_COLLECTION &&
                            tag != IPP_TAG_MEMBERNAME && !r->last_name)
                        {
                                r->failed = true;
                                return false;
                        }
                        out->name = r->last_name;
                        out->name_len = r->last_name_len;
                        out->is_additional = true;
                }
                if (r->length - r->at < 2)
                {
                        r->failed = true;
                        return false;
                }
                val_len = network_load_16((p8 address_to)r->bytes + r->at);
                r->at += 2;
                if (val_len > r->length - r->at)
                {
                        r->failed = true;
                        return false;
                }
                out->value = r->bytes + r->at;
                out->value_len = val_len;
                r->at += val_len;

                // Validate fixed-size RFC 8010 value encodings.
                if ((tag == IPP_TAG_INTEGER || tag == IPP_TAG_ENUM) &&
                    val_len != 4)
                {
                        r->failed = true;
                        return false;
                }
                if (tag == IPP_TAG_BOOLEAN &&
                    (val_len != 1 || out->value[0] > 1))
                {
                        r->failed = true;
                        return false;
                }
                if (tag == IPP_TAG_RANGE && val_len != 8)
                {
                        r->failed = true;
                        return false;
                }
                if (tag == IPP_TAG_RESOLUTION && val_len != 9)
                {
                        r->failed = true;
                        return false;
                }
                if (tag == IPP_TAG_DATETIME && val_len != 11)
                {
                        r->failed = true;
                        return false;
                }

                out->group = r->group;
                out->tag = tag;
                r->attr_count++;
                return true;
        }
}

static bool ipp_attr_is(const ipp_attr address_to a, string_address name)
{
        positive n = string_length(name);

        return a->name && a->name_len == n &&
               !memory_compare(a->name, name, n);
}

static bool ipp_attr_i32(const ipp_attr address_to a, b32 address_to out)
{
        if ((a->tag != IPP_TAG_INTEGER && a->tag != IPP_TAG_ENUM) ||
            a->value_len != 4)
                return false;
        address_to out = (b32)network_load_32((p8 address_to)a->value);
        return true;
}

static bool ipp_attr_bool(const ipp_attr address_to a, bool address_to out)
{
        if (a->tag != IPP_TAG_BOOLEAN || a->value_len != 1)
                return false;
        address_to out = a->value[0] != 0;
        return true;
}

static positive ipp_attr_copy_str(const ipp_attr address_to a,
                                  p8 address_to dst, positive room)
{
        positive n;

        if (!dst || !room)
                return 0;
        n = a->value_len;
        if (n >= room)
                n = room - 1;
        if (n)
                memory_copy(dst, a->value, n);
        dst[n] = end;
        return n;
}

static bool ipp_attr_value_eq(const ipp_attr address_to a, string_address text)
{
        positive n = string_length(text);

        return a->value && a->value_len == n &&
               !memory_compare_ascii_case(a->value, text, n);
}

/* ---- IPP URI Parser & Formatter ---- */

typedef struct
{
        bool tls;
        p8 host[128];
        p16 port;
        p8 path[160];
} ipp_uri;

static bool ipp_parse_uri(string_address text, ipp_uri address_to out)
{
        string_address p = text;
        p16 default_port = IPP_PORT_DEFAULT;
        positive host_len = 0, path_len = 0;

        memory_zero(out, sizeof(address_to out));
        if (!text || !text[0])
                return false;

        if (!memory_compare_ascii_case(p, "ipps://", 7))
        {
                out->tls = true;
                default_port = IPP_PORT_DEFAULT;
                p += 7;
        }
        else if (!memory_compare_ascii_case(p, "ipp://", 6))
        {
                out->tls = false;
                default_port = IPP_PORT_DEFAULT;
                p += 6;
        }
        else if (!memory_compare_ascii_case(p, "https://", 8))
        {
                out->tls = true;
                default_port = 443;
                p += 8;
        }
        else if (!memory_compare_ascii_case(p, "http://", 7))
        {
                out->tls = false;
                default_port = 80;
                p += 7;
        }
        else if (string_find(p, "://"))
        {
                return false;
        }

        // Host
        while (p[host_len] && p[host_len] != ':' && p[host_len] != '/')
        {
                p8 c = p[host_len];

                if (!http_host_byte(c))
                        return false;
                if (host_len >= sizeof(out->host) - 1)
                        return false;
                out->host[host_len++] = c;
        }
        if (!host_len)
                return false;
        out->host[host_len] = end;
        p += host_len;

        // Optional :port
        if (*p == ':')
        {
                positive port_val = 0, digits = 0;

                p++;
                while (*p >= '0' && *p <= '9')
                {
                        port_val = port_val * 10 + (positive)(*p - '0');
                        if (++digits > 5 || port_val > 65535)
                                return false;
                        p++;
                }
                if (!digits || !port_val)
                        return false;
                out->port = (p16)port_val;
        }
        else
        {
                out->port = default_port;
        }

        // Optional /path (default "/ipp/print")
        if (!*p)
        {
                memory_copy(out->path, "/ipp/print", 11);
                return true;
        }
        if (*p != '/')
                return false;
        while (p[path_len])
        {
                p8 c = p[path_len];

                if (byte_is_control(c) || c == ' ' || c == '\\' || c > 0x7f)
                        return false;
                if (path_len >= sizeof(out->path) - 1)
                        return false;
                out->path[path_len++] = c;
        }
        out->path[path_len] = end;
        return true;
}

static positive ipp_format_uri(const ipp_uri address_to u, p8 address_to into,
                               positive room)
{
        string_address scheme = u->tls ? (string_address)"ipps://"
                                       : (string_address)"ipp://";
        positive slen = string_length(scheme);
        positive hlen = string_length(u->host);
        positive plen = string_length(u->path);
        p8 port_buf[8];
        positive port_len = positive_into(port_buf, u->port);
        positive need = slen + hlen + 1 + port_len + plen + 1;
        positive used = 0;

        if (!into || room < need)
                return 0;
        memory_copy(into + used, scheme, slen);
        used += slen;
        memory_copy(into + used, u->host, hlen);
        used += hlen;
        into[used++] = ':';
        memory_copy(into + used, port_buf, port_len);
        used += port_len;
        memory_copy(into + used, u->path, plen);
        used += plen;
        into[used] = end;
        return used;
}

/* ---- Printer & Job Data Structures ---- */

typedef struct
{
        p8 name[96];
        p8 make_model[96];
        p8 location[96];
        p8 uri[256];
        p8 rp[96];
        p8 host[128];
        p8 uuid[64];
        p32 ip; // host byte order IPv4 (0 if unresolved)
        p16 port;
        bool tls;
        p32 caps; // IPP_CAP_*
        b32 state; // IPP_PRINTER_*
        bool accepting;
} ipp_printer;

typedef struct
{
        string_address job_name;
        string_address user_name;
        string_address mime_type;
        b32 copies;
        bool duplex;
        bool color;
} ipp_job_request;

typedef struct
{
        p16 ipp_status;
        b32 job_id;
        b32 job_state;
        p8 job_uri[256];
        p8 status_message[128];
} ipp_job_result;

typedef struct
{
        b32 job_id;
        b32 state;
        p8 name[96];
        p8 user[64];
        b32 size_kb;
} ipp_job_info;

static fn ipp_caps_from_mime(const p8 address_to mime, positive len,
                             p32 address_to caps)
{
        if (len == 15 && !memory_compare_ascii_case(mime, "application/pdf", 15))
                address_to caps |= IPP_CAP_PDF;
        else if (len == 16 && !memory_compare_ascii_case(mime, "image/pwg-raster", 16))
                address_to caps |= IPP_CAP_PWG_RASTER;
        else if (len == 9 && !memory_compare_ascii_case(mime, "image/urf", 9))
                address_to caps |= IPP_CAP_URF;
        else if (len == 10 && !memory_compare_ascii_case(mime, "image/jpeg", 10))
                address_to caps |= IPP_CAP_JPEG;
        else if (len == 9 && !memory_compare_ascii_case(mime, "image/png", 9))
                address_to caps |= IPP_CAP_PNG;
        else if (len == 10 && !memory_compare_ascii_case(mime, "text/plain", 10))
                address_to caps |= IPP_CAP_TEXT;
}

static string_address ipp_detect_mime(const p8 address_to head, positive len,
                                      string_address filename)
{
        if (head && len >= 5 && !memory_compare(head, "%PDF-", 5))
                return (string_address)"application/pdf";
        if (head && len >= 4 && head[0] == 0x89 && head[1] == 'P' &&
            head[2] == 'N' && head[3] == 'G')
                return (string_address)"image/png";
        if (head && len >= 3 && head[0] == 0xff && head[1] == 0xd8 &&
            head[2] == 0xff)
                return (string_address)"image/jpeg";
        if (head && len >= 4 && !memory_compare(head, "RaS2", 4))
                return (string_address)"image/pwg-raster";
        if (filename)
        {
                positive n = string_length(filename);

                if (n >= 4 && !memory_compare_ascii_case(filename + n - 4, ".pdf", 4))
                        return (string_address)"application/pdf";
                if (n >= 4 && !memory_compare_ascii_case(filename + n - 4, ".png", 4))
                        return (string_address)"image/png";
                if ((n >= 4 && !memory_compare_ascii_case(filename + n - 4, ".jpg", 4)) ||
                    (n >= 5 && !memory_compare_ascii_case(filename + n - 5, ".jpeg", 5)))
                        return (string_address)"image/jpeg";
                if (n >= 4 && !memory_compare_ascii_case(filename + n - 4, ".txt", 4))
                        return (string_address)"text/plain";
        }
        return (string_address)"application/octet-stream";
}

/* ---- mDNS / DNS-SD Printer Discovery (_ipp._tcp.local, _ipps._tcp.local) ---- */

static const p8 ipp_mdns_type_ipp[17] = "\x04_ipp\x04_tcp\x05local";
static const p8 ipp_mdns_type_ipps[18] = "\x05_ipps\x04_tcp\x05local";

static positive ipp_mdns_query(bool tls, p8 address_to packet, positive room)
{
        const p8 address_to type = tls ? ipp_mdns_type_ipps : ipp_mdns_type_ipp;
        positive type_len = tls ? sizeof(ipp_mdns_type_ipps)
                                : sizeof(ipp_mdns_type_ipp);

        if (!packet || room < 16 + type_len)
                return 0;
        memory_zero(packet, 12);
        network_store_16(packet + 4, 1); // 1 question
        memory_copy(packet + 12, type, type_len);
        network_store_16(packet + 12 + type_len, 12); // PTR
        network_store_16(packet + 12 + type_len + 2, 1); // IN
        return 12 + type_len + 4;
}

static bool ipp_mdns_match_service(const p8 address_to name, positive len,
                                   bool address_to is_tls)
{
        if (len == sizeof(ipp_mdns_type_ipp) &&
            !memory_compare_ascii_case(name, ipp_mdns_type_ipp, len))
        {
                address_to is_tls = false;
                return true;
        }
        if (len == sizeof(ipp_mdns_type_ipps) &&
            !memory_compare_ascii_case(name, ipp_mdns_type_ipps, len))
        {
                address_to is_tls = true;
                return true;
        }
        return false;
}

static bool ipp_mdns_split_instance(const p8 address_to name, positive len,
                                    p8 address_to label,
                                    positive address_to label_len,
                                    bool address_to is_tls)
{
        positive first;

        if (!len)
                return false;
        first = name[0];
        if (!first || first > 63 || 1 + first >= len)
                return false;
        if (!ipp_mdns_match_service(name + 1 + first, len - 1 - first, is_tls))
                return false;
        memory_copy(label, name + 1, first);
        address_to label_len = first;
        return true;
}

// Convert wire-format DNS name (length-prefixed labels) to dotted ASCII host.
static positive ipp_mdns_wire_to_host(const p8 address_to wire, positive len,
                                      p8 address_to out, positive room)
{
        positive at = 0, used = 0;

        if (!out || !room)
                return 0;
        while (at < len)
        {
                p8 l = wire[at++];

                if (!l)
                        break;
                if (l > 63 || l > len - at)
                        return 0;
                if (used)
                {
                        if (used + 1 >= room)
                                return 0;
                        out[used++] = '.';
                }
                if (l >= room - used)
                        return 0;
                memory_copy(out + used, wire + at, l);
                used += l;
                at += l;
        }
        out[used] = end;
        return used;
}

static fn ipp_mdns_parse_txt(const p8 address_to data, positive len,
                             ipp_printer address_to pr)
{
        positive at = 0;

        while (at < len)
        {
                p8 slen = data[at++];
                const p8 address_to s;
                positive eq = 0;

                if (slen > len - at)
                        break;
                s = data + at;
                at += slen;
                while (eq < slen && s[eq] != '=')
                        eq++;
                if (!eq || eq >= slen)
                        continue;

                const p8 address_to val = s + eq + 1;
                positive vlen = slen - eq - 1;

                if (eq == 2 && !memory_compare_ascii_case(s, "rp", 2))
                {
                        positive n = vlen < sizeof(pr->rp) - 1 ? vlen : sizeof(pr->rp) - 1;

                        memory_copy(pr->rp, val, n);
                        pr->rp[n] = end;
                }
                else if (eq == 2 && !memory_compare_ascii_case(s, "ty", 2))
                {
                        positive n = vlen < sizeof(pr->make_model) - 1 ? vlen : sizeof(pr->make_model) - 1;

                        memory_copy(pr->make_model, val, n);
                        pr->make_model[n] = end;
                }
                else if (eq == 4 && !memory_compare_ascii_case(s, "note", 4))
                {
                        positive n = vlen < sizeof(pr->location) - 1 ? vlen : sizeof(pr->location) - 1;

                        memory_copy(pr->location, val, n);
                        pr->location[n] = end;
                }
                else if (eq == 4 && !memory_compare_ascii_case(s, "UUID", 4))
                {
                        positive n = vlen < sizeof(pr->uuid) - 1 ? vlen : sizeof(pr->uuid) - 1;

                        memory_copy(pr->uuid, val, n);
                        pr->uuid[n] = end;
                }
                else if (eq == 5 && !memory_compare_ascii_case(s, "Color", 5))
                {
                        if (vlen >= 1 && (val[0] == 'T' || val[0] == 't'))
                                pr->caps |= IPP_CAP_COLOR;
                }
                else if (eq == 6 && !memory_compare_ascii_case(s, "Duplex", 6))
                {
                        if (vlen >= 1 && (val[0] == 'T' || val[0] == 't'))
                                pr->caps |= IPP_CAP_DUPLEX;
                }
                else if (eq == 3 && !memory_compare_ascii_case(s, "pdl", 3))
                {
                        positive p = 0;

                        while (p < vlen)
                        {
                                positive start = p;

                                while (p < vlen && val[p] != ',')
                                        p++;
                                ipp_caps_from_mime(val + start, p - start,
                                                   address_of pr->caps);
                                if (p < vlen && val[p] == ',')
                                        p++;
                        }
                }
        }
}

typedef struct
{
        p8 label[64];
        positive label_len;
        bool tls;
        bool has_ptr;
        bool has_srv;
        p8 target_wire[256];
        positive target_wire_len;
        ipp_printer printer;
} ipp_mdns_entry;

typedef struct
{
        p8 host_wire[256];
        positive host_wire_len;
        p32 ip;
} ipp_mdns_host;

static ipp_mdns_entry address_to
ipp_mdns_find_or_add(ipp_mdns_entry address_to entries, positive address_to count,
                     positive max_entries, const p8 address_to label,
                     positive label_len, bool tls)
{
        for (positive i = 0; i < address_to count; i++)
                if (entries[i].tls == tls &&
                    entries[i].label_len == label_len &&
                    !memory_compare(entries[i].label, label, label_len))
                        return entries + i;
        if (address_to count >= max_entries)
                return null;
        {
                ipp_mdns_entry address_to e = entries + (address_to count)++;

                memory_zero(e, sizeof(address_to e));
                memory_copy(e->label, label, label_len);
                e->label[label_len] = end;
                e->label_len = label_len;
                e->tls = tls;
                e->printer.tls = tls;
                memory_copy(e->printer.name, label, label_len);
                e->printer.name[label_len] = end;
                e->printer.state = IPP_PRINTER_IDLE;
                e->printer.accepting = true;
                return e;
        }
}

static positive ipp_mdns_read(const p8 address_to packet, positive length,
                              p32 source_ip, ipp_printer address_to out,
                              positive max_out)
{
        ipp_mdns_entry entries[IPP_MAX_PRINTERS];
        ipp_mdns_host hosts[IPP_MAX_PRINTERS];
        positive entry_count = 0, host_count = 0;
        positive cap = max_out < IPP_MAX_PRINTERS ? max_out : IPP_MAX_PRINTERS;
        p8 name[256];
        positive steps = length;
        positive at = 12;
        p16 flags;
        p32 questions, records;

        if (!packet || !out || !cap || length < 12 || length > 1500)
                return 0;

        flags = network_load_16((p8 address_to)packet + 2);
        if (!(flags & DNS_FLAG_RESPONSE) ||
            (flags & (DNS_OPCODE_MASK | DNS_CODE_MASK)))
                return 0;

        questions = network_load_16((p8 address_to)packet + 4);
        records = (p32)network_load_16((p8 address_to)packet + 6) +
                  (p32)network_load_16((p8 address_to)packet + 8) +
                  (p32)network_load_16((p8 address_to)packet + 10);
        if (questions * 5 + records * 11 > length - 12)
                return 0;

        for (p32 q = 0; q < questions; q++)
        {
                positive ended = 0;
                bipolar nlen = dns_copy_name((p8 address_to)packet, length, at,
                                             name, sizeof(name) - 1,
                                             address_of ended,
                                             address_of steps);

                if (nlen < 0 || ended + 4 > length)
                        return 0;
                at = ended + 4;
        }

        for (p32 r = 0; r < records; r++)
        {
                positive ended = 0;
                bipolar nlen = dns_copy_name((p8 address_to)packet, length, at,
                                             name, sizeof(name) - 1,
                                             address_of ended,
                                             address_of steps);
                p16 type, cls, rdlen;
                p32 ttl;
                positive rdata_at;

                if (nlen < 0 || ended + 10 > length)
                        return 0;
                type = network_load_16((p8 address_to)packet + ended);
                cls = network_load_16((p8 address_to)packet + ended + 2);
                ttl = network_load_32((p8 address_to)packet + ended + 4);
                rdlen = network_load_16((p8 address_to)packet + ended + 8);
                rdata_at = ended + 10;
                if (rdlen > length - rdata_at)
                        return 0;
                at = rdata_at + rdlen;

                if (!ttl || (cls & 0x7fff) != DNS_CLASS_IN)
                        continue;

                // PTR -> instance
                if (type == 12)
                {
                        bool is_tls = false;

                        if (ipp_mdns_match_service(name, (positive)nlen,
                                                   address_of is_tls))
                        {
                                p8 target[256], label[64];
                                positive t_end = 0, label_len = 0;
                                bipolar tlen = dns_copy_name(
                                    (p8 address_to)packet, length, rdata_at,
                                    target, sizeof(target) - 1,
                                    address_of t_end, address_of steps);
                                bool target_tls = false;

                                if (tlen > 0 && t_end == at &&
                                    ipp_mdns_split_instance(
                                        target, (positive)tlen, label,
                                        address_of label_len,
                                        address_of target_tls) &&
                                    target_tls == is_tls)
                                {
                                        ipp_mdns_entry address_to e =
                                            ipp_mdns_find_or_add(
                                                entries, address_of entry_count,
                                                cap, label, label_len, is_tls);
                                        if (e)
                                                e->has_ptr = true;
                                }
                        }
                        continue;
                }

                // A record -> remember host -> IPv4
                if (type == 1 && rdlen == 4)
                {
                        p32 ip = network_load_32((p8 address_to)packet + rdata_at);

                        if (ip && (ip >> 24) != 0 && (ip >> 24) < 224 &&
                            host_count < IPP_MAX_PRINTERS)
                        {
                                memory_copy(hosts[host_count].host_wire, name,
                                            (positive)nlen);
                                hosts[host_count].host_wire_len = (positive)nlen;
                                hosts[host_count].ip = ip;
                                host_count++;
                        }
                        continue;
                }

                // SRV or TXT for an instance
                {
                        p8 label[64];
                        positive label_len = 0;
                        bool is_tls = false;

                        if (!ipp_mdns_split_instance(name, (positive)nlen, label,
                                                     address_of label_len,
                                                     address_of is_tls))
                                continue;

                        ipp_mdns_entry address_to e = ipp_mdns_find_or_add(
                            entries, address_of entry_count, cap, label,
                            label_len, is_tls);
                        if (!e)
                                continue;

                        if (type == 33 && rdlen >= 7)
                        {
                                p16 port = network_load_16(
                                    (p8 address_to)packet + rdata_at + 4);
                                p8 target[256];
                                positive t_end = 0;
                                bipolar tlen = dns_copy_name(
                                    (p8 address_to)packet, length, rdata_at + 6,
                                    target, sizeof(target) - 1,
                                    address_of t_end, address_of steps);

                                if (port && tlen > 1 && t_end == at)
                                {
                                        e->printer.port = port;
                                        e->has_srv = true;
                                        memory_copy(e->target_wire, target,
                                                    (positive)tlen);
                                        e->target_wire_len = (positive)tlen;
                                        ipp_mdns_wire_to_host(
                                            target, (positive)tlen,
                                            e->printer.host,
                                            sizeof(e->printer.host));
                                }
                        }
                        else if (type == 16 && rdlen > 0)
                        {
                                ipp_mdns_parse_txt(packet + rdata_at, rdlen,
                                                   address_of e->printer);
                        }
                }
        }

        if (at != length)
                return 0;

        positive found = 0;
        for (positive i = 0; i < entry_count && found < cap; i++)
        {
                ipp_mdns_entry address_to e = entries + i;
                ipp_uri u;

                if (!e->has_ptr || !e->has_srv)
                        continue;

                // Match A record for SRV target, else fall back to packet source IP.
                e->printer.ip = source_ip;
                for (positive h = 0; h < host_count; h++)
                {
                        if (hosts[h].host_wire_len == e->target_wire_len &&
                            !memory_compare_ascii_case(hosts[h].host_wire,
                                                       e->target_wire,
                                                       e->target_wire_len))
                        {
                                e->printer.ip = hosts[h].ip;
                                break;
                        }
                }

                memory_zero(address_of u, sizeof(u));
                u.tls = e->printer.tls;
                u.port = e->printer.port;
                if (e->printer.ip)
                {
                        positive pos = 0;

                        for (positive b = 0; b < 4; b++)
                        {
                                if (b)
                                        u.host[pos++] = '.';
                                pos += positive_into(
                                    u.host + pos,
                                    (e->printer.ip >> (24 - b * 8)) & 0xffu);
                        }
                        u.host[pos] = end;
                }
                else
                {
                        positive hlen = string_length(e->printer.host);

                        if (hlen >= sizeof(u.host))
                                hlen = sizeof(u.host) - 1;
                        memory_copy(u.host, e->printer.host, hlen);
                        u.host[hlen] = end;
                }

                if (e->printer.rp[0])
                {
                        positive rplen = string_length(e->printer.rp);
                        positive off = 0;

                        if (e->printer.rp[0] != '/')
                                u.path[off++] = '/';
                        if (rplen >= sizeof(u.path) - off)
                                rplen = sizeof(u.path) - off - 1;
                        memory_copy(u.path + off, e->printer.rp, rplen);
                        u.path[off + rplen] = end;
                }
                else
                {
                        memory_copy(u.path, "/ipp/print", 11);
                }

                ipp_format_uri(address_of u, e->printer.uri,
                               sizeof(e->printer.uri));
                out[found++] = e->printer;
        }
        return found;
}

/* ---- IPP-over-HTTP(S) Client ---- */

static bipolar ipp_resolve_host(string_address host, p32 address_to ip_out)
{
        positive hlen = string_length(host);

        if (http_ipv4_spelling((const p8 address_to)host, hlen, ip_out))
                return IPP_OK;
        if (string_equals(host, (string_address)"localhost"))
        {
                address_to ip_out = 0x7f000001u;
                return IPP_OK;
        }
        {
                if (dns_resolve_any((string_address)"/etc/resolv.conf", host,
                                    ip_out, 5) == 0 &&
                    address_to ip_out)
                        return IPP_OK;
        }
        return IPP_ERR_RESOLVE;
}

static bipolar ipp_exchange_link(http_link address_to link,
                                 const ipp_uri address_to uri,
                                 const p8 address_to ipp_req,
                                 positive ipp_req_len,
                                 const p8 address_to doc, positive doc_len,
                                 bipolar doc_fd, positive doc_fd_len,
                                 http_buffer address_to reply,
                                 positive max_reply, b32 address_to http_code)
{
        p8 head[HTTP_HEAD_MAX];
        p8 req_head[768];
        positive total_body = ipp_req_len + (doc ? doc_len : 0) +
                              (doc_fd >= 0 ? doc_fd_len : 0);
        positive pos = 0;
        p8 port_buf[8], len_buf[24];
        positive port_len, len_len;
        positive head_used = 0, header_len = 0;
        http_response resp;
        bipolar status;

        if (!http_request_component_valid(uri->host, HTTP_REQUEST_HOST) ||
            !http_request_component_valid(uri->path, HTTP_REQUEST_TARGET))
                return IPP_ERR_URI;

        port_len = positive_into(port_buf, uri->port);
        len_len = positive_into(len_buf, total_body);

        {
                static const string_address s_post = "POST ";
                static const string_address s_ver = " HTTP/1.1\r\nHost: ";
                static const string_address s_ct =
                    "\r\nContent-Type: application/ipp\r\nContent-Length: ";
                static const string_address s_end =
                    "\r\nConnection: close\r\nUser-Agent: Moonwater-IPP/1.0\r\n\r\n";
                positive path_len = string_length(uri->path);
                positive host_len = string_length(uri->host);
                positive need = string_length(s_post) + path_len +
                                string_length(s_ver) + host_len + 1 + port_len +
                                string_length(s_ct) + len_len +
                                string_length(s_end);

                if (need > sizeof(req_head))
                        return IPP_ERR_URI;

                memory_copy(req_head + pos, s_post, string_length(s_post));
                pos += string_length(s_post);
                memory_copy(req_head + pos, uri->path, path_len);
                pos += path_len;
                memory_copy(req_head + pos, s_ver, string_length(s_ver));
                pos += string_length(s_ver);
                memory_copy(req_head + pos, uri->host, host_len);
                pos += host_len;
                req_head[pos++] = ':';
                memory_copy(req_head + pos, port_buf, port_len);
                pos += port_len;
                memory_copy(req_head + pos, s_ct, string_length(s_ct));
                pos += string_length(s_ct);
                memory_copy(req_head + pos, len_buf, len_len);
                pos += len_len;
                memory_copy(req_head + pos, s_end, string_length(s_end));
                pos += string_length(s_end);
        }

        if (http_link_write(link, req_head, pos) != HTTP_OK)
                return IPP_ERR_IO;
        if (ipp_req_len &&
            http_link_write(link, (p8 address_to)ipp_req, ipp_req_len) != HTTP_OK)
                return IPP_ERR_IO;
        if (doc && doc_len &&
            http_link_write(link, (p8 address_to)doc, doc_len) != HTTP_OK)
                return IPP_ERR_IO;
        if (doc_fd >= 0 && doc_fd_len)
        {
                p8 chunk[16384];
                positive left = doc_fd_len;

                while (left)
                {
                        positive want = left < sizeof(chunk) ? left : sizeof(chunk);
                        bipolar got = system_read_once(doc_fd, chunk, want);

                        if (got <= 0)
                                return IPP_ERR_IO;
                        if (http_link_write(link, chunk, (positive)got) != HTTP_OK)
                                return IPP_ERR_IO;
                        left -= (positive)got;
                }
        }

        memory_zero(address_of resp, sizeof(resp));
        status = http_response_head(link, head, sizeof(head),
                                    address_of head_used, address_of header_len,
                                    address_of resp, HTTP_IDLE_SECONDS, 0, true,
                                    null);
        if (status != HTTP_OK)
                return IPP_ERR_HTTP;
        if (http_code)
                address_to http_code = resp.code;
        if (!http_response_is_success(resp.code))
                return IPP_ERR_HTTP;

        {
                p8 scratch[HTTP_HEAD_MAX];
                http_body body = {
                    .link = link,
                    .stash = head + header_len,
                    .stash_used = head_used - header_len,
                    .scratch = scratch,
                    .store = reply,
                    .store_limit = max_reply ? max_reply : IPP_MAX_REPLY_BYTES,
                    .undeclared_most = max_reply ? max_reply : IPP_MAX_REPLY_BYTES,
                };

                status = http_copy_body(address_of body, -1, address_of resp);
                if (status != HTTP_OK)
                        return IPP_ERR_IO;
        }
        return IPP_OK;
}

static bipolar ipp_exchange(const ipp_uri address_to uri, p32 ip_override,
                            bool verify_tls, const p8 address_to ipp_req,
                            positive ipp_req_len, const p8 address_to doc,
                            positive doc_len, bipolar doc_fd,
                            positive doc_fd_len, http_buffer address_to reply,
                            positive max_reply, b32 address_to http_code)
{
        p32 ip = ip_override;
        http_link link;
        bipolar rc;

        if (!ip)
        {
                rc = ipp_resolve_host(uri->host, address_of ip);
                if (rc != IPP_OK)
                        return rc;
        }
        if (http_link_open(address_of link, ip, uri->port, uri->host, uri->tls,
                           verify_tls) != HTTP_OK)
                return IPP_ERR_CONNECT;
        rc = ipp_exchange_link(address_of link, uri, ipp_req, ipp_req_len, doc,
                               doc_len, doc_fd, doc_fd_len, reply, max_reply,
                               http_code);
        http_link_close(address_of link);
        return rc;
}

/* ---- High-Level IPP Operations ---- */

static bipolar ipp_parse_printer_attrs(const p8 address_to data, positive len,
                                       ipp_printer address_to info,
                                       p16 address_to status_out)
{
        ipp_reader r;
        ipp_attr a;

        if (!ipp_read_open(address_of r, data, len))
                return IPP_ERR_DECODE;
        if (status_out)
                address_to status_out = r.op_or_status;
        if (!ipp_status_ok(r.op_or_status))
                return IPP_ERR_STATUS;

        while (ipp_read_next(address_of r, address_of a))
        {
                if (a.group != IPP_TAG_PRINTER)
                        continue;
                if (ipp_attr_is(address_of a, (string_address)"printer-name"))
                        ipp_attr_copy_str(address_of a, info->name,
                                          sizeof(info->name));
                else if (ipp_attr_is(address_of a,
                                     (string_address)"printer-make-and-model"))
                        ipp_attr_copy_str(address_of a, info->make_model,
                                          sizeof(info->make_model));
                else if (ipp_attr_is(address_of a,
                                     (string_address)"printer-location"))
                        ipp_attr_copy_str(address_of a, info->location,
                                          sizeof(info->location));
                else if (ipp_attr_is(address_of a,
                                     (string_address)"printer-uuid"))
                        ipp_attr_copy_str(address_of a, info->uuid,
                                          sizeof(info->uuid));
                else if (ipp_attr_is(address_of a,
                                     (string_address)"printer-state"))
                        (void)ipp_attr_i32(address_of a, address_of info->state);
                else if (ipp_attr_is(address_of a,
                                     (string_address)"printer-is-accepting-jobs"))
                        (void)ipp_attr_bool(address_of a,
                                            address_of info->accepting);
                else if (ipp_attr_is(address_of a,
                                     (string_address)"color-supported"))
                {
                        bool c = false;

                        if (ipp_attr_bool(address_of a, address_of c) && c)
                                info->caps |= IPP_CAP_COLOR;
                }
                else if (ipp_attr_is(address_of a,
                                     (string_address)"sides-supported"))
                {
                        if (ipp_attr_value_eq(address_of a,
                                              (string_address)"two-sided-long-edge") ||
                            ipp_attr_value_eq(address_of a,
                                              (string_address)"two-sided-short-edge"))
                                info->caps |= IPP_CAP_DUPLEX;
                }
                else if (ipp_attr_is(address_of a,
                                     (string_address)"document-format-supported"))
                {
                        ipp_caps_from_mime(a.value, a.value_len,
                                           address_of info->caps);
                }
        }
        return r.failed || !r.ended ? IPP_ERR_DECODE : IPP_OK;
}

static positive ipp_build_get_printer_attributes(p8 address_to buf,
                                                 positive room, p32 req_id,
                                                 string_address printer_uri)
{
        ipp_writer w;

        ipp_write_begin(address_of w, buf, room, IPP_VERSION_2_0_MAJOR,
                        IPP_VERSION_2_0_MINOR, IPP_OP_GET_PRINTER_ATTRIBUTES,
                        req_id);
        ipp_write_op_header(address_of w, IPP_VERSION_2_0_MAJOR,
                            IPP_VERSION_2_0_MINOR,
                            IPP_OP_GET_PRINTER_ATTRIBUTES, req_id, printer_uri);
        ipp_write_cstr(address_of w, IPP_TAG_KEYWORD,
                       (string_address)"requested-attributes",
                       (string_address)"printer-name");
        ipp_write_cstr(address_of w, IPP_TAG_KEYWORD, null,
                       (string_address)"printer-make-and-model");
        ipp_write_cstr(address_of w, IPP_TAG_KEYWORD, null,
                       (string_address)"printer-location");
        ipp_write_cstr(address_of w, IPP_TAG_KEYWORD, null,
                       (string_address)"printer-state");
        ipp_write_cstr(address_of w, IPP_TAG_KEYWORD, null,
                       (string_address)"printer-is-accepting-jobs");
        ipp_write_cstr(address_of w, IPP_TAG_KEYWORD, null,
                       (string_address)"document-format-supported");
        ipp_write_cstr(address_of w, IPP_TAG_KEYWORD, null,
                       (string_address)"color-supported");
        ipp_write_cstr(address_of w, IPP_TAG_KEYWORD, null,
                       (string_address)"sides-supported");
        ipp_write_cstr(address_of w, IPP_TAG_KEYWORD, null,
                       (string_address)"printer-uuid");
        return ipp_write_end(address_of w);
}

static bipolar ipp_get_printer_attributes_link(http_link address_to link,
                                               const ipp_uri address_to uri,
                                               ipp_printer address_to info)
{
        p8 req[1024];
        p8 uri_str[256];
        positive req_len;
        http_buffer reply = {0};
        bipolar rc;

        if (!ipp_format_uri(uri, uri_str, sizeof(uri_str)))
                return IPP_ERR_URI;
        req_len = ipp_build_get_printer_attributes(req, sizeof(req), 1, uri_str);
        if (!req_len)
                return IPP_ERR_ENCODE;
        rc = ipp_exchange_link(link, uri, req, req_len, null, 0, -1, 0,
                               address_of reply, IPP_MAX_REPLY_BYTES, null);
        if (rc == IPP_OK)
        {
                memory_copy(info->uri, uri_str, string_length(uri_str) + 1);
                info->port = uri->port;
                info->tls = uri->tls;
                rc = ipp_parse_printer_attrs(reply.bytes, reply.used, info,
                                             null);
        }
        http_forget(address_of reply);
        return rc;
}

static bipolar ipp_get_printer_attributes(const ipp_uri address_to uri,
                                          p32 ip_override, bool verify_tls,
                                          ipp_printer address_to info)
{
        p8 req[1024];
        p8 uri_str[256];
        positive req_len;
        http_buffer reply = {0};
        bipolar rc;

        if (!ipp_format_uri(uri, uri_str, sizeof(uri_str)))
                return IPP_ERR_URI;
        req_len = ipp_build_get_printer_attributes(req, sizeof(req), 1, uri_str);
        if (!req_len)
                return IPP_ERR_ENCODE;
        rc = ipp_exchange(uri, ip_override, verify_tls, req, req_len, null, 0,
                          -1, 0, address_of reply, IPP_MAX_REPLY_BYTES, null);
        if (rc == IPP_OK)
        {
                memory_copy(info->uri, uri_str, string_length(uri_str) + 1);
                info->port = uri->port;
                info->tls = uri->tls;
                rc = ipp_parse_printer_attrs(reply.bytes, reply.used, info,
                                             null);
        }
        http_forget(address_of reply);
        return rc;
}

static positive ipp_build_print_job(p8 address_to buf, positive room,
                                    p32 req_id, string_address printer_uri,
                                    const ipp_job_request address_to job)
{
        ipp_writer w;

        ipp_write_begin(address_of w, buf, room, IPP_VERSION_2_0_MAJOR,
                        IPP_VERSION_2_0_MINOR, IPP_OP_PRINT_JOB, req_id);
        ipp_write_op_header(address_of w, IPP_VERSION_2_0_MAJOR,
                            IPP_VERSION_2_0_MINOR, IPP_OP_PRINT_JOB, req_id,
                            printer_uri);
        if (job && job->user_name && job->user_name[0])
                ipp_write_cstr(address_of w, IPP_TAG_NAME,
                               (string_address)"requesting-user-name",
                               job->user_name);
        if (job && job->job_name && job->job_name[0])
                ipp_write_cstr(address_of w, IPP_TAG_NAME,
                               (string_address)"job-name", job->job_name);
        if (job && job->mime_type && job->mime_type[0])
                ipp_write_cstr(address_of w, IPP_TAG_MIMETYPE,
                               (string_address)"document-format",
                               job->mime_type);
        if (job && (job->copies > 1 || job->duplex || !job->color))
        {
                ipp_write_group(address_of w, IPP_TAG_JOB);
                if (job->copies > 1)
                        ipp_write_int(address_of w, IPP_TAG_INTEGER,
                                      (string_address)"copies", job->copies);
                if (job->duplex)
                        ipp_write_cstr(address_of w, IPP_TAG_KEYWORD,
                                       (string_address)"sides",
                                       (string_address)"two-sided-long-edge");
        }
        return ipp_write_end(address_of w);
}

static bipolar ipp_parse_job_result(const p8 address_to data, positive len,
                                    ipp_job_result address_to res)
{
        ipp_reader r;
        ipp_attr a;

        memory_zero(res, sizeof(address_to res));
        if (!ipp_read_open(address_of r, data, len))
                return IPP_ERR_DECODE;
        res->ipp_status = r.op_or_status;
        while (ipp_read_next(address_of r, address_of a))
        {
                if (a.group == IPP_TAG_OPERATION &&
                    ipp_attr_is(address_of a, (string_address)"status-message"))
                        ipp_attr_copy_str(address_of a, res->status_message,
                                          sizeof(res->status_message));
                else if (a.group == IPP_TAG_JOB)
                {
                        if (ipp_attr_is(address_of a, (string_address)"job-id"))
                                (void)ipp_attr_i32(address_of a,
                                                   address_of res->job_id);
                        else if (ipp_attr_is(address_of a,
                                             (string_address)"job-state"))
                                (void)ipp_attr_i32(address_of a,
                                                   address_of res->job_state);
                        else if (ipp_attr_is(address_of a,
                                             (string_address)"job-uri"))
                                ipp_attr_copy_str(address_of a, res->job_uri,
                                                  sizeof(res->job_uri));
                }
        }
        if (r.failed || !r.ended)
                return IPP_ERR_DECODE;
        return ipp_status_ok(res->ipp_status) ? IPP_OK : IPP_ERR_STATUS;
}

static bipolar ipp_print_job_link(http_link address_to link,
                                  const ipp_uri address_to uri,
                                  const ipp_job_request address_to job,
                                  const p8 address_to doc, positive doc_len,
                                  bipolar doc_fd, positive doc_fd_len,
                                  ipp_job_result address_to res)
{
        p8 req[1024];
        p8 uri_str[256];
        positive req_len;
        http_buffer reply = {0};
        bipolar rc;

        if (!ipp_format_uri(uri, uri_str, sizeof(uri_str)))
                return IPP_ERR_URI;
        req_len = ipp_build_print_job(req, sizeof(req), 2, uri_str, job);
        if (!req_len)
                return IPP_ERR_ENCODE;
        rc = ipp_exchange_link(link, uri, req, req_len, doc, doc_len, doc_fd,
                               doc_fd_len, address_of reply,
                               IPP_MAX_REPLY_BYTES, null);
        if (rc == IPP_OK)
                rc = ipp_parse_job_result(reply.bytes, reply.used, res);
        http_forget(address_of reply);
        return rc;
}

static bipolar ipp_print_job(const ipp_uri address_to uri, p32 ip_override,
                             bool verify_tls,
                             const ipp_job_request address_to job,
                             const p8 address_to doc, positive doc_len,
                             bipolar doc_fd, positive doc_fd_len,
                             ipp_job_result address_to res)
{
        p8 req[1024];
        p8 uri_str[256];
        positive req_len;
        http_buffer reply = {0};
        bipolar rc;

        if (!ipp_format_uri(uri, uri_str, sizeof(uri_str)))
                return IPP_ERR_URI;
        req_len = ipp_build_print_job(req, sizeof(req), 2, uri_str, job);
        if (!req_len)
                return IPP_ERR_ENCODE;
        rc = ipp_exchange(uri, ip_override, verify_tls, req, req_len, doc,
                          doc_len, doc_fd, doc_fd_len, address_of reply,
                          IPP_MAX_REPLY_BYTES, null);
        if (rc == IPP_OK)
                rc = ipp_parse_job_result(reply.bytes, reply.used, res);
        http_forget(address_of reply);
        return rc;
}

static bipolar ipp_cancel_job(const ipp_uri address_to uri, p32 ip_override,
                              bool verify_tls, b32 job_id,
                              p16 address_to status_out)
{
        ipp_writer w;
        p8 req[512];
        p8 uri_str[256];
        positive req_len;
        http_buffer reply = {0};
        bipolar rc;

        if (!ipp_format_uri(uri, uri_str, sizeof(uri_str)))
                return IPP_ERR_URI;
        ipp_write_begin(address_of w, req, sizeof(req), IPP_VERSION_2_0_MAJOR,
                        IPP_VERSION_2_0_MINOR, IPP_OP_CANCEL_JOB, 3);
        ipp_write_op_header(address_of w, IPP_VERSION_2_0_MAJOR,
                            IPP_VERSION_2_0_MINOR, IPP_OP_CANCEL_JOB, 3,
                            uri_str);
        ipp_write_int(address_of w, IPP_TAG_INTEGER, (string_address)"job-id",
                      job_id);
        req_len = ipp_write_end(address_of w);
        if (!req_len)
                return IPP_ERR_ENCODE;
        rc = ipp_exchange(uri, ip_override, verify_tls, req, req_len, null, 0,
                          -1, 0, address_of reply, IPP_MAX_REPLY_BYTES, null);
        if (rc == IPP_OK)
        {
                ipp_reader r;

                if (!ipp_read_open(address_of r, reply.bytes, reply.used))
                        rc = IPP_ERR_DECODE;
                else
                {
                        if (status_out)
                                address_to status_out = r.op_or_status;
                        rc = ipp_status_ok(r.op_or_status) ? IPP_OK
                                                           : IPP_ERR_STATUS;
                }
        }
        http_forget(address_of reply);
        return rc;
}

static bipolar ipp_get_jobs(const ipp_uri address_to uri, p32 ip_override,
                            bool verify_tls, bool completed,
                            ipp_job_info address_to jobs, positive max_jobs,
                            positive address_to out_count)
{
        ipp_writer w;
        p8 req[512];
        p8 uri_str[256];
        positive req_len;
        http_buffer reply = {0};
        bipolar rc;

        if (out_count)
                address_to out_count = 0;
        if (!ipp_format_uri(uri, uri_str, sizeof(uri_str)))
                return IPP_ERR_URI;
        ipp_write_begin(address_of w, req, sizeof(req), IPP_VERSION_2_0_MAJOR,
                        IPP_VERSION_2_0_MINOR, IPP_OP_GET_JOBS, 4);
        ipp_write_op_header(address_of w, IPP_VERSION_2_0_MAJOR,
                            IPP_VERSION_2_0_MINOR, IPP_OP_GET_JOBS, 4, uri_str);
        ipp_write_cstr(address_of w, IPP_TAG_KEYWORD,
                       (string_address)"which-jobs",
                       completed ? (string_address)"completed"
                                 : (string_address)"not-completed");
        ipp_write_cstr(address_of w, IPP_TAG_KEYWORD,
                       (string_address)"requested-attributes",
                       (string_address)"job-id");
        ipp_write_cstr(address_of w, IPP_TAG_KEYWORD, null,
                       (string_address)"job-state");
        ipp_write_cstr(address_of w, IPP_TAG_KEYWORD, null,
                       (string_address)"job-name");
        ipp_write_cstr(address_of w, IPP_TAG_KEYWORD, null,
                       (string_address)"job-originating-user-name");
        ipp_write_cstr(address_of w, IPP_TAG_KEYWORD, null,
                       (string_address)"job-k-octets");
        req_len = ipp_write_end(address_of w);
        if (!req_len)
                return IPP_ERR_ENCODE;
        rc = ipp_exchange(uri, ip_override, verify_tls, req, req_len, null, 0,
                          -1, 0, address_of reply, IPP_MAX_REPLY_BYTES, null);
        if (rc == IPP_OK)
        {
                ipp_reader r;
                ipp_attr a;
                positive count = 0;
                bool in_job = false;

                if (!ipp_read_open(address_of r, reply.bytes, reply.used))
                        rc = IPP_ERR_DECODE;
                else if (!ipp_status_ok(r.op_or_status))
                        rc = IPP_ERR_STATUS;
                else
                {
                        while (ipp_read_next(address_of r, address_of a))
                        {
                                if (a.group != IPP_TAG_JOB)
                                {
                                        in_job = false;
                                        continue;
                                }
                                if (!in_job ||
                                    (ipp_attr_is(address_of a,
                                                 (string_address)"job-id") &&
                                     count && jobs[count - 1].job_id))
                                {
                                        if (count >= max_jobs)
                                        {
                                                in_job = false;
                                                continue;
                                        }
                                        memory_zero(jobs + count,
                                                    sizeof(jobs[0]));
                                        count++;
                                        in_job = true;
                                }
                                {
                                        ipp_job_info address_to j =
                                            jobs + count - 1;

                                        if (ipp_attr_is(address_of a,
                                                        (string_address)"job-id"))
                                                (void)ipp_attr_i32(
                                                    address_of a,
                                                    address_of j->job_id);
                                        else if (ipp_attr_is(
                                                     address_of a,
                                                     (string_address)"job-state"))
                                                (void)ipp_attr_i32(
                                                    address_of a,
                                                    address_of j->state);
                                        else if (ipp_attr_is(
                                                     address_of a,
                                                     (string_address)"job-name"))
                                                ipp_attr_copy_str(
                                                    address_of a, j->name,
                                                    sizeof(j->name));
                                        else if (
                                            ipp_attr_is(
                                                address_of a,
                                                (string_address)"job-originating-user-name"))
                                                ipp_attr_copy_str(
                                                    address_of a, j->user,
                                                    sizeof(j->user));
                                        else if (
                                            ipp_attr_is(
                                                address_of a,
                                                (string_address)"job-k-octets"))
                                                (void)ipp_attr_i32(
                                                    address_of a,
                                                    address_of j->size_kb);
                                }
                        }
                        if (r.failed || !r.ended)
                                rc = IPP_ERR_DECODE;
                        else if (out_count)
                                address_to out_count = count;
                }
        }
        http_forget(address_of reply);
        return rc;
}

#endif /* STANDARD_MODERN_C_NET_IPP */
