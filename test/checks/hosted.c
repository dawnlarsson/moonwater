/*
        test/checks/hosted.c: the static probes a booted image runs: the radio, ring 0 and the firewall.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_hwsim_radio
/*
        The wifi lane's hands inside the guest: radios made and tuned where
        the image has no tool for either. Built by test/run with the box's
        own compiler, static, and carried in on the lane's stick.

            hwsim_radio new             make a mac80211_hwsim radio; print its id
            hwsim_radio power IF DBM    fix IF's transmit power, which is what
                                        another radio hears as its signal
            hwsim_radio hold            make the access points' network namespace;
                                        print the pid that holds it
            hwsim_radio move PID IF     move IF's radio into it; print IF's name there
            hwsim_radio in PID CMD...   run CMD inside it
            hwsim_radio dhcp IF SECONDS a DHCP server in raw frames on IF: one
                                        address, 192.168.77.100, leased for SECONDS
            hwsim_radio inject IF MHZ KIND TO FROM BSSID REASON TIMES
                                        IF becomes a monitor on that channel and sends
                                        TIMES unprotected management frames of KIND
                                        (deauth or disassoc) from the address FROM to
                                        TO in BSSID's network: the spoof a radio in
                                        range can send, as it heard no key

        The access points live in a namespace of their own so that the
        station's machine sees only its station, and the DHCP server, with
        no address on its interface, never lets the guest's own routing
        short-cut the station's packets.

        Radios outlive this program: nothing asks hwsim to destroy them when
        the socket closes, so one made after `moonwater wifi add` is a card
        that arrived late and stays.
*/
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/genetlink.h>
#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <linux/nl80211.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static char hwsim_buffer[8192];
static unsigned hwsim_sequence = 1;

static int hwsim_put(char *at, int used, int type, const void *data, int length)
{
        struct nlattr *attribute = (struct nlattr *)(at + used);

        attribute->nla_type = (unsigned short)type;
        attribute->nla_len = (unsigned short)(NLA_HDRLEN + length);
        memcpy((char *)attribute + NLA_HDRLEN, data, (size_t)length);
        return used + NLA_ALIGN(attribute->nla_len);
}

/* Send one request and return the ack's error: negative errno, or what the
   command answered, which for a new radio is its id. */
static int hwsim_ask(int handle, int family, int command, char *attributes, int length,
                     int want_family_id)
{
        struct nlmsghdr *header = (struct nlmsghdr *)hwsim_buffer;
        struct genlmsghdr *generic = (struct genlmsghdr *)NLMSG_DATA(header);
        int used = NLMSG_HDRLEN + GENL_HDRLEN;

        memset(hwsim_buffer, 0, sizeof(hwsim_buffer));
        memcpy(hwsim_buffer + used, attributes, (size_t)length);
        used += length;
        header->nlmsg_len = (unsigned)used;
        header->nlmsg_type = (unsigned short)family;
        header->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
        header->nlmsg_seq = ++hwsim_sequence;
        generic->cmd = (unsigned char)command;
        generic->version = 1;
        if (send(handle, hwsim_buffer, (size_t)used, 0) < 0)
                return -1;

        for (;;)
        {
                int got = (int)recv(handle, hwsim_buffer, sizeof(hwsim_buffer), 0);
                int found = -1000;

                if (got <= 0)
                        return -1;
                for (header = (struct nlmsghdr *)hwsim_buffer; NLMSG_OK(header, got);
                     header = NLMSG_NEXT(header, got))
                {
                        if (header->nlmsg_type == NLMSG_ERROR)
                                return ((struct nlmsgerr *)NLMSG_DATA(header))->error;
                        if (want_family_id && header->nlmsg_type == GENL_ID_CTRL)
                        {
                                char *at = (char *)NLMSG_DATA(header) + GENL_HDRLEN;
                                int left = (int)header->nlmsg_len - NLMSG_HDRLEN - GENL_HDRLEN;

                                while (left >= NLA_HDRLEN)
                                {
                                        struct nlattr *attribute = (struct nlattr *)at;

                                        if (attribute->nla_len < NLA_HDRLEN)
                                                break;
                                        if (attribute->nla_type == CTRL_ATTR_FAMILY_ID)
                                                found = *(unsigned short *)(at + NLA_HDRLEN);
                                        left -= NLA_ALIGN(attribute->nla_len);
                                        at += NLA_ALIGN(attribute->nla_len);
                                }
                        }
                }
                if (found != -1000)
                        return found;
        }
}

static int hwsim_family(int handle, const char *name)
{
        char attributes[64];
        int length = hwsim_put(attributes, 0, CTRL_ATTR_FAMILY_NAME, name,
                               (int)strlen(name) + 1);

        return hwsim_ask(handle, GENL_ID_CTRL, CTRL_CMD_GETFAMILY, attributes, length, 1);
}

static int hwsim_enter(const char *pid)
{
        char path[64];
        int handle;

        snprintf(path, sizeof path, "/proc/%s/ns/net", pid);
        handle = open(path, O_RDONLY | O_CLOEXEC);
        if (handle < 0 || setns(handle, CLONE_NEWNET) < 0)
        {
                perror("hwsim_radio: setns");
                return -1;
        }
        close(handle);
        return 0;
}

static unsigned short hwsim_sum(const unsigned char *at, int length)
{
        unsigned long sum = 0;

        for (int i = 0; i + 1 < length; i += 2)
                sum += (unsigned)(at[i] << 8 | at[i + 1]);
        while (sum >> 16)
                sum = (sum & 0xffff) + (sum >> 16);
        return (unsigned short)~sum;
}

/* One DHCP reply in place of the request in frame: an OFFER to a DISCOVER,
   an ACK to a REQUEST for the pool address, a NAK to one for any other;
   the frame's length, or 0 for nothing to answer. */
static int hwsim_dhcp_answer(const unsigned char *frame, int got, const unsigned char *mine,
                             unsigned lease, unsigned char *out, const char **said)
{
        const unsigned server = 0xc0a84d01u, pool = 0xc0a84d64u;
        const unsigned char *ip = frame + 14;
        const unsigned char *udp, *boot, *option, *stop = frame + got;
        unsigned char *rip = out + 14, *rudp = rip + 20, *rboot = rudp + 8, *ropt = rboot + 240;
        unsigned ciaddr, value, wanted = 0;
        int kind = 0, nak, length;

        if (got < 14 + 20 + 8 + 240 || frame[12] != 0x08 || frame[13] != 0 || ip[9] != 17 ||
            (ip[0] & 15) < 5 || 14 + (ip[0] & 15) * 4 + 8 + 240 > got)
                return 0;
        udp = ip + (ip[0] & 15) * 4;
        boot = udp + 8;
        if (udp[2] != 0 || udp[3] != 67 || boot[0] != 1 ||
            memcmp(boot + 236, "\x63\x82\x53\x63", 4))
                return 0;
        for (option = boot + 240; option + 2 <= stop && *option != 255;)
        {
                if (!*option)
                {
                        option++;
                        continue;
                }
                if (option[1] > stop - option - 2)
                        break;
                if (option[0] == 53 && option[1] >= 1)
                        kind = option[2];
                if (option[0] == 50 && option[1] >= 4)
                        memcpy(&wanted, option + 2, 4), wanted = ntohl(wanted);
                option += 2 + option[1];
        }
        memcpy(&ciaddr, boot + 12, 4);
        ciaddr = ntohl(ciaddr);
        if (kind != 1 && kind != 3)
                return 0;
        if (kind == 3 && !wanted)
                wanted = ciaddr;
        nak = kind == 3 && wanted != pool;

        memset(out, 0, 14 + 20 + 8 + 300);
        //      To the client's own address on a renewal, else to everyone.
        if (ciaddr)
                memcpy(out, boot + 28, 6);
        else
                memset(out, 0xff, 6);
        memcpy(out + 6, mine, 6);
        out[12] = 0x08;
        rboot[0] = 2, rboot[1] = 1, rboot[2] = 6;
        memcpy(rboot + 4, boot + 4, 4);
        memcpy(rboot + 10, boot + 10, 2);
        value = htonl(nak ? 0 : pool);
        memcpy(rboot + 16, &value, 4);
        value = htonl(server);
        memcpy(rboot + 20, &value, 4);
        memcpy(rboot + 28, boot + 28, 16);
        memcpy(rboot + 236, "\x63\x82\x53\x63", 4);
        *ropt++ = 53, *ropt++ = 1, *ropt++ = kind == 1 ? 2 : nak ? 6 : 5;
        *ropt++ = 54, *ropt++ = 4;
        memcpy(ropt, &value, 4), ropt += 4;
        if (!nak)
        {
                unsigned words[6] = {htonl(lease), htonl(lease / 2), htonl(lease * 7 / 8),
                                     htonl(0xffffff00u), htonl(server), htonl(server)};
                unsigned char codes[6] = {51, 58, 59, 1, 3, 6};

                for (int i = 0; i < 6; i++)
                {
                        *ropt++ = codes[i], *ropt++ = 4;
                        memcpy(ropt, &words[i], 4), ropt += 4;
                }
        }
        *ropt++ = 255;
        length = (int)(ropt - rboot) < 300 ? 300 : (int)(ropt - rboot);
        rip[0] = 0x45;
        rip[2] = (unsigned char)((28 + length) >> 8), rip[3] = (unsigned char)(28 + length);
        rip[8] = 64, rip[9] = 17;
        value = htonl(server);
        memcpy(rip + 12, &value, 4);
        value = ciaddr && !nak ? htonl(ciaddr) : 0xffffffffu;
        memcpy(rip + 16, &value, 4);
        value = hwsim_sum(rip, 20);
        rip[10] = (unsigned char)(value >> 8), rip[11] = (unsigned char)value;
        rudp[1] = 67, rudp[3] = 68;
        rudp[4] = (unsigned char)((8 + length) >> 8), rudp[5] = (unsigned char)(8 + length);
        *said = kind == 1 ? "offer" : nak ? "nak" : "ack";
        return 14 + 28 + length;
}

static int hwsim_dhcp(const char *name, unsigned lease)
{
        unsigned char mine[6], frame[2048], out[14 + 28 + 300];
        const unsigned server = htonl(0xc0a84d01u);
        struct ifreq request;
        struct sockaddr_ll self;
        int handle = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

        memset(&request, 0, sizeof request);
        strncpy(request.ifr_name, name, IFNAMSIZ - 1);
        if (handle < 0 || ioctl(handle, SIOCGIFINDEX, &request) < 0)
                return perror("hwsim_radio: dhcp"), 1;
        memset(&self, 0, sizeof self);
        self.sll_family = AF_PACKET;
        self.sll_protocol = htons(ETH_P_ALL);
        self.sll_ifindex = request.ifr_ifindex;
        if (ioctl(handle, SIOCGIFHWADDR, &request) < 0 ||
            bind(handle, (struct sockaddr *)&self, sizeof self) < 0)
                return perror("hwsim_radio: dhcp"), 1;
        memcpy(mine, request.ifr_hwaddr.sa_data, 6);
        for (;;)
        {
                struct sockaddr_ll from;
                socklen_t from_length = sizeof from;
                int got = (int)recvfrom(handle, frame, sizeof frame, 0,
                                        (struct sockaddr *)&from, &from_length);
                const char *said = "";
                int length;

                if (got < 42 || from.sll_pkttype == PACKET_OUTGOING)
                        continue;
                //      ARP for the server's address, answered with this one.
                if (frame[12] == 0x08 && frame[13] == 0x06 && frame[21] == 1 &&
                    !memcmp(frame + 38, &server, 4))
                {
                        memcpy(out, frame + 6, 6);
                        memcpy(out + 6, mine, 6);
                        memcpy(out + 12, frame + 12, 8);
                        out[21] = 2;
                        memcpy(out + 22, mine, 6);
                        memcpy(out + 28, frame + 38, 4);
                        memcpy(out + 32, frame + 22, 10);
                        send(handle, out, 42, 0);
                        continue;
                }
                length = hwsim_dhcp_answer(frame, got, mine, lease, out, &said);
                if (length && send(handle, out, (size_t)length, 0) == length)
                {
                        printf("dhcp-serve %s %s\n", name, said);
                        fflush(stdout);
                }
        }
}

static int hwsim_mac(const char *text, unsigned char *mac)
{
        unsigned v[6];

        if (sscanf(text, "%x:%x:%x:%x:%x:%x", v, v + 1, v + 2, v + 3, v + 4, v + 5) != 6)
                return -1;
        for (int i = 0; i < 6; i++)
                mac[i] = (unsigned char)v[i];
        return 0;
}

/* The monitor and the frames of `inject`. The radio is an ordinary station
   interface until this makes it a monitor, which mac80211 lets send what
   it is handed behind a radiotap header. */
static int hwsim_inject(int handle, char **words)
{
        unsigned index = if_nametoindex(words[2]);
        unsigned monitor = NL80211_IFTYPE_MONITOR;
        unsigned frequency = (unsigned)atoi(words[3]);
        unsigned reason = (unsigned)atoi(words[8]);
        unsigned times = (unsigned)atoi(words[9]);
        unsigned char frame[8 + 26];
        char attributes[64];
        struct ifreq request;
        struct sockaddr_ll self;
        int family, answer, length, raw;
        int disassoc = !strcmp(words[4], "disassoc");

        if (!index || (!disassoc && strcmp(words[4], "deauth")))
                return fprintf(stderr, "hwsim_radio: inject: no %s or kind %s\n", words[2],
                               words[4]), 1;
        family = hwsim_family(handle, "nl80211");
        if (family < 0)
                return fprintf(stderr, "hwsim_radio: no nl80211\n"), 1;
        length = hwsim_put(attributes, 0, NL80211_ATTR_IFINDEX, &index, 4);
        length = hwsim_put(attributes, length, NL80211_ATTR_IFTYPE, &monitor, 4);
        answer = hwsim_ask(handle, family, NL80211_CMD_SET_INTERFACE, attributes, length, 0);
        if (answer < 0)
                return fprintf(stderr, "hwsim_radio: monitor: %s\n", strerror(-answer)), 1;
        memset(&request, 0, sizeof request);
        strncpy(request.ifr_name, words[2], IFNAMSIZ - 1);
        raw = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (raw < 0 || ioctl(raw, SIOCGIFFLAGS, &request) < 0)
                return perror("hwsim_radio: inject"), 1;
        request.ifr_flags |= IFF_UP;
        if (ioctl(raw, SIOCSIFFLAGS, &request) < 0)
                return perror("hwsim_radio: up"), 1;
        length = hwsim_put(attributes, 0, NL80211_ATTR_IFINDEX, &index, 4);
        length = hwsim_put(attributes, length, NL80211_ATTR_WIPHY_FREQ, &frequency, 4);
        answer = hwsim_ask(handle, family, NL80211_CMD_SET_CHANNEL, attributes, length, 0);
        if (answer < 0)
                return fprintf(stderr, "hwsim_radio: channel: %s\n", strerror(-answer)), 1;

        memset(frame, 0, sizeof frame);
        frame[2] = 8;                      // radiotap: version 0, length 8, no fields
        frame[8] = disassoc ? 0xa0 : 0xc0; // management: disassociation or deauthentication
        if (hwsim_mac(words[5], frame + 8 + 4) || hwsim_mac(words[6], frame + 8 + 10) ||
            hwsim_mac(words[7], frame + 8 + 16))
                return fprintf(stderr, "hwsim_radio: inject: a bad address\n"), 1;
        frame[8 + 24] = (unsigned char)reason;
        memset(&self, 0, sizeof self);
        self.sll_family = AF_PACKET;
        self.sll_protocol = htons(ETH_P_ALL);
        self.sll_ifindex = (int)index;
        if (bind(raw, (struct sockaddr *)&self, sizeof self) < 0)
                return perror("hwsim_radio: inject bind"), 1;
        for (unsigned at = 0; at < times; at++)
        {
                if (send(raw, frame, sizeof frame, 0) != (ssize_t)sizeof frame)
                        return perror("hwsim_radio: inject send"), 1;
                usleep(50000);
        }
        return 0;
}

int main(int count, char **words)
{
        int handle = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
        char attributes[64];
        int family;
        int answer;

        if (handle < 0)
        {
                perror("hwsim_radio: netlink");
                return 1;
        }
        if (count == 2 && !strcmp(words[1], "hold"))
        {
                pid_t child = fork();

                if (child == 0)
                {
                        if (unshare(CLONE_NEWNET) < 0)
                                _exit(1);
                        close(0), close(1), close(2);
                        for (;;)
                                pause();
                }
                usleep(200000);
                printf("%d\n", (int)child);
                return child > 0 ? 0 : 1;
        }
        if (count == 4 && !strcmp(words[1], "move"))
        {
                char name[IF_NAMESIZE];
                unsigned index = if_nametoindex(words[3]);
                unsigned pid = (unsigned)atoi(words[2]);
                int length = 0;

                family = hwsim_family(handle, "nl80211");
                if (!index || family < 0)
                {
                        fprintf(stderr, "hwsim_radio: no %s or no nl80211\n", words[3]);
                        return 1;
                }
                length = hwsim_put(attributes, length, NL80211_ATTR_IFINDEX, &index, 4);
                length = hwsim_put(attributes, length, NL80211_ATTR_PID, &pid, 4);
                answer = hwsim_ask(handle, family, NL80211_CMD_SET_WIPHY_NETNS, attributes,
                                   length, 0);
                if (answer < 0)
                {
                        fprintf(stderr, "hwsim_radio: move: %s\n", strerror(-answer));
                        return 1;
                }
                if (hwsim_enter(words[2]) < 0 || !if_indextoname(index, name))
                {
                        fprintf(stderr, "hwsim_radio: %s did not arrive\n", words[3]);
                        return 1;
                }
                printf("%s\n", name);
                return 0;
        }
        if (count >= 4 && !strcmp(words[1], "in"))
        {
                if (hwsim_enter(words[2]) < 0)
                        return 1;
                execv(words[3], words + 3);
                perror("hwsim_radio: exec");
                return 127;
        }
        if (count == 4 && !strcmp(words[1], "dhcp"))
                return hwsim_dhcp(words[2], (unsigned)atoi(words[3]));
        if (count == 10 && !strcmp(words[1], "inject"))
                return hwsim_inject(handle, words);
        if (count == 2 && !strcmp(words[1], "new"))
        {
                family = hwsim_family(handle, "MAC80211_HWSIM");
                if (family < 0)
                {
                        fprintf(stderr, "hwsim_radio: no MAC80211_HWSIM family\n");
                        return 1;
                }
                answer = hwsim_ask(handle, family, 4 /* HWSIM_CMD_NEW_RADIO */,
                                   attributes, 0, 0);
                if (answer < 0)
                {
                        fprintf(stderr, "hwsim_radio: new radio: %s\n", strerror(-answer));
                        return 1;
                }
                printf("%d\n", answer);
                return 0;
        }
        if (count == 4 && !strcmp(words[1], "power"))
        {
                unsigned index = if_nametoindex(words[2]);
                unsigned setting = NL80211_TX_POWER_FIXED;
                int mbm = atoi(words[3]) * 100;
                int length = 0;

                family = hwsim_family(handle, "nl80211");
                if (!index || family < 0)
                {
                        fprintf(stderr, "hwsim_radio: no %s or no nl80211\n", words[2]);
                        return 1;
                }
                length = hwsim_put(attributes, length, NL80211_ATTR_IFINDEX, &index, 4);
                length = hwsim_put(attributes, length, NL80211_ATTR_WIPHY_TX_POWER_SETTING,
                                   &setting, 4);
                length = hwsim_put(attributes, length, NL80211_ATTR_WIPHY_TX_POWER_LEVEL,
                                   &mbm, 4);
                answer = hwsim_ask(handle, family, NL80211_CMD_SET_WIPHY, attributes, length,
                                   0);
                if (answer < 0)
                {
                        fprintf(stderr, "hwsim_radio: power: %s\n", strerror(-answer));
                        return 1;
                }
                return 0;
        }
        fprintf(stderr, "usage: hwsim_radio new | power IF DBM | hold | move PID IF |"
                        " in PID CMD... | dhcp IF SECONDS | inject IF MHZ KIND TO FROM BSSID"
                        " REASON TIMES\n");
        return 2;
}
#endif /* CHECK_hwsim_radio */

#ifdef CHECK_ring0_hostile
/*
        A hostile program for /dev/spark and everything behind it, built to run
        in a guest whose kernel is the one under test: the lane that boots it
        is `sh test/run ring0` (asked for by name; MOONWATER_IMAGE names the
        image). Ring 0 is one translation unit that anything on the machine
        may open -- the device is mode 0666 -- so this is what such a program
        does with it, from as many threads as it can start:

            ring0_hostile MODE SEED SECONDS [UID [OPMASK [THREADS]]]

            ioctl  every request number with shaped and random arguments, and
                   well formed snapshots into real buffers from many threads
            exec   spark images with hostile headers, by execve
            admin  canvas off and on and the machine, script and bind
                   requests made as root, from threads that go on together

        Everything after the setup runs as UID (default 65534, nobody) except
        admin, which needs root. A fault the program takes is caught and the
        operation abandoned; one taken outside any operation says FATAL and
        ends the run. The run ends, after SECONDS,
        with one FUZZ-DONE line, and a kernel that is a sanitizer build says
        the rest: KASAN, UBSAN, lockdep, sleeping in atomic context, hung
        tasks. FAILPCT in the environment makes every allocation the
        program's tasks cause in the kernel fail, after the caller has set
        the probability of /sys/kernel/debug/failslab and fail_page_alloc,
        so the error paths of everything it reaches run as well.
*/
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pthread.h>
#include <poll.h>
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

/* The module's own headers, so a struct that changes changes this program:
   spark.c's settings code wants the CRC the kernel and the shell supply. */
static unsigned int hash_crc32(unsigned int crc, void *bytes, unsigned long length)
{
        (void)bytes;
        (void)length;
        return crc;
}
/* spark.c and moonwater.c hold their structures to the ABI with offset_of, the
   library's name for offsetof, and this program does not include the library */
#ifndef offset_of
#define offset_of(type, member) offsetof(type, member)
#endif
#include "../../src/moonwater/spark.c"
#include "../../src/moonwater/moonwater.c"
/* The window table's two requests, which the device takes from the program that
   holds the table and hands to any program: numbers a program has to know. */
#ifndef WINDOWS_IOCTL_TABLE_SET
#define WINDOWS_IOCTL_TABLE_SET 0x00007311u
#define WINDOWS_IOCTL_TABLE_GET 0x00007312u
#endif

static unsigned long long rng_state = 88172645463325252ull;
static __thread unsigned long long rs;
static unsigned long long rnd(void)
{
        unsigned long long x = rs ? rs : (rs = rng_state ^ (unsigned long long)(uintptr_t)&rs);
        XORSHIFT64(x);
        return rs = x;
}
static unsigned below(unsigned n) { return n ? (unsigned)(rnd() % n) : 0; }

static volatile int stop_now;
static time_t deadline;
static int time_up(void) { return stop_now || time(NULL) >= deadline; }

static const unsigned dict[] = {
        0, 0, 0, 1, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 95, 96, 100, 127, 128, 255, 256,
        511, 512, 1000, 1024, 2047, 2048, 4095, 4096, 4097, 65535, 65536, 0x7fff, 0x8000,
        0xffffff, 0x1000000, 0x1000001, 0x7fffffff, 0x80000000u, 0x80000001u, 0xfffffffeu,
        0xffffffffu, 0xfffff000u, 3840, 2160, 1920, 1080, 480, 135, 270, 8192, 16384,
};
static unsigned dictv(void) { return dict[below(sizeof(dict) / sizeof(dict[0]))]; }
static unsigned value(void)
{
        switch (below(6)) {
        case 0: case 1: case 2: return dictv();
        case 3: return dictv() + (below(3) - 1);
        case 4: return (unsigned)rnd();
        default: return below(300);
        }
}

// ---- crash containment: a hostile program can fault itself ----
static __thread sigjmp_buf *guard;
static pid_t main_pid;
static void hexout(const char *label, unsigned long v)
{
        char b[64];
        int n = 0, i;

        while (*label) b[n++] = *label++;
        for (i = 60; i >= 0; i -= 4) b[n++] = "0123456789abcdef"[(v >> i) & 15];
        b[n++] = '\n';
        if (write(2, b, (size_t)n) < 0) return;
}
static void on_fault(int sig, siginfo_t *si, void *uc)
{
        if (guard)
                siglongjmp(*guard, 1);
        // outside any guarded operation: the fuzzer itself, or the kernel
        // refusing a page it should have given, ends the run; say which
        hexout(getpid() == main_pid ? "FATAL-MAIN-SIG " : "FATAL-CHILD-SIG ", (unsigned long)sig);
        hexout("FATAL-RIP ", (unsigned long)((ucontext_t *)uc)->uc_mcontext.gregs[REG_RIP]);
        hexout("FATAL-ADDR ", (unsigned long)si->si_addr);
        hexout("FATAL-CODE ", (unsigned long)si->si_code);
        _exit(90);
}

// ---- the device ----
static unsigned long ops_done, ioctl_ok, ioctl_fail;
static unsigned long opmask = ~0ul;   // which of an operation's variants may run

static int spark_open(void)
{
        return open("/dev/spark", O_RDWR | O_CLOEXEC);
}

// ---- ioctl fuzz ----
static const unsigned cmds[] = {
        SPARK_IOCTL_SPAWN, SPARK_IOCTL_STATS, SPARK_IOCTL_SNAPSHOT,
        SPARK_IOCTL_CANVAS, SPARK_IOCTL_BIND, SPARK_IOCTL_SETTINGS_GET,
        SPARK_IOCTL_SETTINGS_SET, MOONWATER_IOCTL_MACHINE, MOONWATER_IOCTL_SCRIPT,
        WINDOWS_IOCTL_TABLE_SET, WINDOWS_IOCTL_TABLE_GET,
};

static void shaped(unsigned char *b, size_t n)
{
        size_t i;
        for (i = 0; i + 4 <= n; i += 4) {
                unsigned v = below(3) ? 0 : value();
                memcpy(b + i, &v, 4);
        }
        // pointer-looking words
        for (i = 0; i + 8 <= n; i += 8) {
                if (below(5) == 0) {
                        unsigned long long p = below(3) == 0 ? 0 : below(2) ? (unsigned long long)(uintptr_t)b : 0xffff888000000000ull + below(4096);
                        memcpy(b + i, &p, 8);
                }
        }
}

static void ioctl_op(int fd, unsigned char *buf, size_t bufn, unsigned char *aux, size_t auxn)
{
        if (below(5) == 0) {
                // a well formed snapshot into a real buffer, from many threads
                static const unsigned caps[] = {112, 4096, 5000, 65536, 1u << 20};
                static __thread unsigned char big[1u << 20];
                struct snapshot_request q = {.buffer = (unsigned long)big, .capacity = caps[below(5)],
                                             .flags = below(8), .version = SPARK_SNAPSHOT_VERSION};
                long got = ioctl(fd, SPARK_IOCTL_SNAPSHOT, &q);
                if (got == 0 || (got < 0 && errno == ENOSPC)) {
                        struct snapshot_header *h = (void *)big;
                        if (q.used >= sizeof(*h) && (h->bytes != q.used || h->version != SPARK_SNAPSHOT_VERSION)) {
                                fprintf(stderr, "SNAPSHOT-BAD used=%u bytes=%u version=%u\n", q.used, h->bytes, h->version);
                                stop_now = 1;
                        }
                        ioctl_ok++;
                } else
                        ioctl_fail++;
                ops_done++;
                return;
        }
        unsigned cmd = below(10) ? cmds[below(sizeof(cmds) / sizeof(cmds[0]))]
                                 : (below(2) ? (0x7300u | below(32)) | (below(4) << 30) | (below(0x400) << 16) : (unsigned)rnd());
        unsigned long arg;
        long r;
        switch (below(8)) {
        case 0: arg = 0; break;
        case 1: arg = 0xffff888000000000ul + below(65536); break;
        case 2: arg = (unsigned long)value(); break;
        default: shaped(buf, bufn); arg = (unsigned long)buf; break;
        }
        // Point inner user pointers at a second shaped buffer half the time.
        if (arg == (unsigned long)buf && below(2)) {
                shaped(aux, auxn);
                unsigned long inner = (unsigned long)aux;
                memcpy(buf, &inner, 8);
                if (below(2)) { unsigned x = value(); memcpy(buf + 8, &x, 4); }
        }
        // spawn: a sane-ish request some of the time
        if (cmd == SPARK_IOCTL_SPAWN && arg == (unsigned long)buf && below(2)) {
                static const char path[] = "/bin/true";
                static char argv[] = "true\0-x\0";
                static char envp[] = "A=1\0B=2\0";
                struct spawn s = {
                        .path = (unsigned long)path,
                        .argv = (unsigned long)argv,
                        .argv_bytes = below(3) ? sizeof(argv) : value(),
                        .argv_count = below(3) ? 2 : value(),
                        .envp = (unsigned long)envp,
                        .envp_bytes = below(3) ? sizeof(envp) : value(),
                        .envp_count = below(3) ? 2 : value(),
                        .envp_generation = below(4),
                        .flags = value() & 0xff,
                        .stdio = {-1, -1, -1},
                };
                memcpy(buf, &s, sizeof(s));
        }
        r = ioctl(fd, cmd, arg);
        if (r >= 0) {
                ioctl_ok++;
                (void)r;
        } else
                ioctl_fail++;
        ops_done++;
}

static void *ioctl_thread(void *arg)
{
        int fd = spark_open();
        unsigned char *buf = calloc(1, 8192), *aux = calloc(1, 8192);
        sigjmp_buf jb;
        (void)arg;
        rs = 0;
        while (!time_up()) {
                guard = &jb;
                if (sigsetjmp(jb, 1) == 0)
                        ioctl_op(fd, buf, 8192, aux, 8192);
                guard = NULL;
                if (below(200) == 0) {
                        close(fd);
                        fd = spark_open();
                }
        }
        return NULL;
}

// ---- exec fuzz: spark images with hostile headers ----
static void write_image(const char *path, const struct header *h, size_t file_bytes)
{
        int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0755);
        unsigned char *page = calloc(1, 4096);
        size_t left = file_bytes;
        if (fd < 0)
                return;
        memcpy(page, h, sizeof(*h));
        // x86-64: ud2 at the entry, so a loaded image dies at once
        page[64] = 0x0f; page[65] = 0x0b;
        while (left) {
                size_t n = left < 4096 ? left : 4096;
                if (write(fd, page, n) < 0)
                        break;
                memset(page, 0, 4096);
                left -= n;
        }
        close(fd);
        free(page);
}

static unsigned long pick_addr(void)
{
        static const unsigned long a[] = {
                0, 0x1000, 0x10000, 0x400000, 0x10000000, 0x7fffffff000ul, 0x7ffffffff000ul,
                0x7ffffffde000ul, 0x800000000000ul, 0xfffffffffffff000ul, 0xffffffff00000000ul,
                0x100000000ul, 0xfffff000ul, 0xffffe000ul, 0x7fff00000000ul, 0x7ffc00000000ul,
        };
        switch (below(3)) {
        case 0: return a[below(sizeof(a) / sizeof(a[0]))];
        case 1: return a[below(sizeof(a) / sizeof(a[0]))] + (below(8) - 4) * 4096ul;
        default: return (unsigned long)(rnd() & ~0xffful);
        }
}
static unsigned long pick_size(void)
{
        static const unsigned long s[] = {0, 4096, 8192, 65536, 1ul << 20, 16ul << 20, 256ul << 20,
                                          (256ul << 20) + 4096, 0xfffffffffffff000ul, 0x100000000ul, 1ul << 63};
        switch (below(3)) {
        case 0: return s[below(sizeof(s) / sizeof(s[0]))];
        case 1: return 4096ul * below(64);
        default: return (unsigned long)rnd() & (below(2) ? ~0xffful : ~0ul);
        }
}

static void exec_op(unsigned n)
{
        char path[64];
        struct header h = {SPARK_MAGIC, SPARK_VERSION, 0, 0x400000, 0x400040, 4096, 0, 0, {0, 0}};
        size_t file_bytes = 4096 * (1 + below(4));
        pid_t p;
        snprintf(path, sizeof(path), "/tmp/img%u", n);
        if (below(4)) h.base = pick_addr();
        if (below(4)) h.entry = below(2) ? h.base + below(8192) : pick_addr();
        if (below(4)) h.text_size = pick_size();
        if (below(3) == 0) h.data_size = pick_size();
        if (below(3) == 0) h.bss_size = pick_size();
        if (below(12) == 0) h.magic ^= 1u << below(32);
        if (below(12) == 0) h.version = (unsigned short)value();
        if (below(12) == 0) h.flags = (unsigned short)value();
        if (below(6) == 0) file_bytes = below(200);
        write_image(path, &h, file_bytes);
        p = fork();
        if (p == 0) {
                char *av[] = {path, NULL};
                if (below(2)) {
                        // make the stack of the caller look unusual first
                        struct rlimit rl = {below(2) ? 8u << 20 : 64u << 10, RLIM_INFINITY};
                        setrlimit(RLIMIT_STACK, &rl);
                }
                execv(path, av);
                _exit(91);
        }
        if (p > 0) {
                int st, waited = 0;

                // SIGCHLD is ignored, so a child that has ended is reaped and
                // waitpid says ECHILD; one still going after a second is
                // killed, since a program that starts must not run away
                while (waitpid(p, &st, WNOHANG) == 0 && waited++ < 100)
                        usleep(10000);
                if (waited > 100) {
                        kill(p, SIGKILL);
                        waitpid(p, &st, 0);
                }
        }
        unlink(path);
        ops_done++;
}

static void *exec_thread(void *arg)
{
        unsigned n = (unsigned)(uintptr_t)arg * 1000000u;
        rs = 0;
        while (!time_up())
                exec_op(n++);
        return NULL;
}

// ---- admin: privileged operations, as root, against each other ----
static void *admin_thread(void *arg)
{
        int fd = spark_open();
        unsigned char *buf = calloc(1, 8192);
        (void)arg;
        rs = 0;
        while (!time_up()) {
                struct canvas_control c = {0};
                switch (below(6)) {
                case 0: c.request = SPARK_CANVAS_OFF; ioctl(fd, SPARK_IOCTL_CANVAS, &c); break;
                case 1: c.request = SPARK_CANVAS_ON; ioctl(fd, SPARK_IOCTL_CANVAS, &c); break;
                case 2: c.request = below(6); ioctl(fd, SPARK_IOCTL_CANVAS, &c); break;
                case 3: {
                        struct machine_control m = {0};
                        m.op = below(5);
                        m.reserved[0] = m.op == MOONWATER_WAIT ? 1 : 0;
                        ioctl(fd, MOONWATER_IOCTL_MACHINE, &m);
                        break;
                }
                case 4: {
                        struct bind_control b = {0};
                        b.op = below(2); b.event = 1 + below(23);
                        snprintf(b.command, sizeof(b.command), "%s", below(2) ? "true" : "");
                        ioctl(fd, SPARK_IOCTL_BIND, &b);
                        break;
                }
                default: {
                        struct machine_script s = {0};
                        s.op = below(2); s.address = (unsigned long)buf; s.length = below(3) ? 8192 : value() & 0xffff;
                        if (s.op) { memset(buf, 'a' + below(20), 8192); if (below(2)) memcpy(buf, "function moonwater_poweroff() { true; }\n", 41); }
                        ioctl(fd, MOONWATER_IOCTL_SCRIPT, &s);
                        break;
                }
                }
                usleep(below(50) * 1000);
                ops_done++;
        }
        return NULL;
}

/* A thread that could not be made (an injected allocation failure, say) is
   not one to join: joining what pthread_create never filled in waits for
   ever. */
static void start(pthread_t *th, int *nth, void *(*fn)(void *), void *arg)
{
        if (pthread_create(&th[*nth], NULL, fn, arg) == 0)
                (*nth)++;
}

static void drop(unsigned uid)
{
        if (uid) {
                setgroups(0, NULL);
                if (setresgid(uid, uid, uid) || setresuid(uid, uid, uid))
                        perror("drop");
        }
}

int main(int argc, char **argv)
{
        const char *mode = argc > 1 ? argv[1] : "ioctl";
        unsigned long long seed = argc > 2 ? strtoull(argv[2], NULL, 0) : 1;
        unsigned seconds = argc > 3 ? (unsigned)atoi(argv[3]) : 30;
        unsigned uid = argc > 4 ? (unsigned)atoi(argv[4]) : 65534;
        if (argc > 5) opmask = strtoul(argv[5], NULL, 0);
        unsigned threads = argc > 6 ? (unsigned)atoi(argv[6]) : 6;
        pthread_t th[16];
        int nth = 0;
        struct sigaction sa = {0};

        main_pid = getpid();
        rng_state = seed * 0x9e3779b97f4a7c15ull + 1;
        rs = rng_state;
        deadline = time(NULL) + seconds;
        sa.sa_sigaction = on_fault;
        sa.sa_flags = SA_NODEFER | SA_SIGINFO;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
        sigaction(SIGALRM, &(struct sigaction){.sa_handler = SIG_DFL}, NULL);
        signal(SIGPIPE, SIG_IGN);
        signal(SIGCHLD, SIG_IGN);

        // FAILPCT set: every allocation this process's tasks make in the kernel
        // may fail (the caller has set the probability, with task-filter on),
        // so the error paths of everything the fuzzer reaches run too. Opened
        // here, before the drop, and written once uid is what the mode wants.
        int failfd = getenv("FAILPCT") ? open("/proc/self/make-it-fail", O_WRONLY) : -1;

        if (!strcmp(mode, "admin")) {
                if (failfd >= 0 && write(failfd, "1", 1) != 1)
                        perror("make-it-fail");
                for (int i = 0; i < 4; i++) start(th, &nth, admin_thread, NULL);
        } else {
                drop(uid);
                if (failfd >= 0 && write(failfd, "1", 1) != 1)
                        perror("make-it-fail");
                if (!strcmp(mode, "ioctl")) {
                        for (unsigned i = 0; i < threads; i++) start(th, &nth, ioctl_thread, NULL);
                } else if (!strcmp(mode, "exec")) {
                        for (int i = 0; i < 3; i++) start(th, &nth, exec_thread, (void *)(uintptr_t)(i + 1));
                } else {
                        fprintf(stderr, "unknown mode %s\n", mode);
                        return 2;
                }
        }
        for (int i = 0; i < nth; i++)
                pthread_join(th[i], NULL);
        printf("FUZZ-DONE %s seed=%llu ops=%lu ioctl_ok=%lu ioctl_fail=%lu\n",
               mode, seed, ops_done, ioctl_ok, ioctl_fail);
        return 0;
}
#endif /* CHECK_ring0_hostile */

#ifdef CHECK_firewall_probe
/*
        A probe of what a closed port answers, for the `firewall` lane in
        test/run. The shell has no /dev/tcp, so a closed-port connect and a
        closed-port datagram are made here, in a static binary built on the
        host and carried into a guest on a stick, as ring0_hostile is. One
        word is printed for a scripted shell to read:

            firewall_probe tcp  HOST PORT MS   connected | refused | timeout | error:E
            firewall_probe udp  HOST PORT MS   reply | refused | none | error:E
            firewall_probe accept ADDR PORT MS accepted | none | error:E

        tcp is a nonblocking connect watched for MS milliseconds: refused is
        ECONNREFUSED (a RST, or loopback's instant refusal), connected is an
        open listener answering, timeout is neither (a silent drop). udp
        connects, sends one byte and waits MS for the connected socket to
        take an ICMP error: refused is ECONNREFUSED (port unreachable came
        back), reply is a datagram (a listener answered), none is silence.
        accept binds and listens on ADDR and takes one connection within MS,
        for the row that an open port still serves the peer. HOST and ADDR
        are numeric, v4 (dotted) or v6 (with a colon, and an optional %zone
        for a link-local address); nothing here resolves a name.
*/
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>

/* Fill *ss from a numeric address and a port; the length, or 0 on a bad
   address. A v6 address may carry %zone for a link-local scope. */
static socklen_t fw_addr(const char *host, int port, struct sockaddr_storage *ss)
{
        memset(ss, 0, sizeof *ss);
        if (strchr(host, ':'))
        {
                struct sockaddr_in6 *a = (void *)ss;
                char copy[128], *zone;

                snprintf(copy, sizeof copy, "%s", host);
                zone = strchr(copy, '%');
                if (zone)
                        *zone++ = 0;
                a->sin6_family = AF_INET6;
                a->sin6_port = htons((unsigned short)port);
                if (inet_pton(AF_INET6, copy, &a->sin6_addr) != 1)
                        return 0;
                if (zone)
                        a->sin6_scope_id = if_nametoindex(zone);
                return sizeof *a;
        }
        else
        {
                struct sockaddr_in *a = (void *)ss;

                a->sin_family = AF_INET;
                a->sin_port = htons((unsigned short)port);
                if (inet_pton(AF_INET, host, &a->sin_addr) != 1)
                        return 0;
                return sizeof *a;
        }
}

static int fw_nonblock(int fd)
{
        int flags = fcntl(fd, F_GETFL, 0);

        return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char **argv)
{
        struct sockaddr_storage ss;
        socklen_t length;
        int port, ms, fd;
        const char *mode, *host;

        if (argc != 5)
        {
                fprintf(stderr, "firewall_probe tcp|udp|accept HOST PORT MS\n");
                return 2;
        }
        mode = argv[1];
        host = argv[2];
        port = atoi(argv[3]);
        ms = atoi(argv[4]);
        length = fw_addr(host, port, &ss);
        if (!length)
        {
                printf("error:address\n");
                return 1;
        }

        if (!strcmp(mode, "tcp"))
        {
                struct pollfd pfd;
                int error = 0;
                socklen_t errlen = sizeof error;

                fd = socket(ss.ss_family, SOCK_STREAM, 0);
                if (fd < 0 || fw_nonblock(fd) < 0)
                {
                        printf("error:socket\n");
                        return 1;
                }
                if (connect(fd, (void *)&ss, length) == 0)
                {
                        printf("connected\n");
                        return 0;
                }
                if (errno != EINPROGRESS)
                {
                        printf(errno == ECONNREFUSED ? "refused\n" : "error:%d\n", errno);
                        return 0;
                }
                pfd.fd = fd;
                pfd.events = POLLOUT;
                if (poll(&pfd, 1, ms) <= 0)
                {
                        printf("timeout\n");
                        return 0;
                }
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &errlen) < 0)
                {
                        printf("error:getsockopt\n");
                        return 0;
                }
                printf(error == 0 ? "connected\n"
                       : error == ECONNREFUSED ? "refused\n" : "error:%d\n", error);
                return 0;
        }

        if (!strcmp(mode, "udp"))
        {
                struct pollfd pfd;
                struct timespec start, now;
                char byte = 'x', buffer[64];

                fd = socket(ss.ss_family, SOCK_DGRAM, 0);
                if (fd < 0 || fw_nonblock(fd) < 0 || connect(fd, (void *)&ss, length) < 0)
                {
                        printf("error:socket\n");
                        return 1;
                }
                (void)send(fd, &byte, 1, 0);
                clock_gettime(CLOCK_MONOTONIC, &start);
                pfd.fd = fd;
                pfd.events = POLLIN;
                for (;;)
                {
                        long left;
                        ssize_t got;

                        clock_gettime(CLOCK_MONOTONIC, &now);
                        left = ms - ((now.tv_sec - start.tv_sec) * 1000 +
                                     (now.tv_nsec - start.tv_nsec) / 1000000);
                        if (left <= 0)
                        {
                                printf("none\n");
                                return 0;
                        }
                        if (poll(&pfd, 1, (int)left) <= 0)
                                continue;
                        got = recv(fd, buffer, sizeof buffer, 0);
                        if (got >= 0)
                        {
                                printf("reply\n");
                                return 0;
                        }
                        if (errno == ECONNREFUSED)
                        {
                                printf("refused\n");
                                return 0;
                        }
                        if (errno != EAGAIN && errno != EWOULDBLOCK)
                        {
                                printf("error:%d\n", errno);
                                return 0;
                        }
                }
        }

        if (!strcmp(mode, "accept"))
        {
                struct pollfd pfd;
                int one = 1, client;

                fd = socket(ss.ss_family, SOCK_STREAM, 0);
                if (fd < 0)
                {
                        printf("error:socket\n");
                        return 1;
                }
                setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
                if (bind(fd, (void *)&ss, length) < 0 || listen(fd, 4) < 0)
                {
                        printf("error:bind\n");
                        return 1;
                }
                pfd.fd = fd;
                pfd.events = POLLIN;
                if (poll(&pfd, 1, ms) <= 0)
                {
                        printf("none\n");
                        return 0;
                }
                client = accept(fd, NULL, NULL);
                printf(client < 0 ? "error:accept\n" : "accepted\n");
                if (client >= 0)
                        close(client);
                return 0;
        }

        if (!strcmp(mode, "flood"))
        {
                /* MS here is seconds of load, not a timeout: fire a SYN at the
                   port as fast as one process can, a nonblocking connect and
                   an immediate close each time, and print how many went. Each
                   SYN to a closed port is a RST (switch 0) or a drop (switch
                   1) for the target to make, which is the cost being weighed.
                   The address is reachable, so the connect never blocks. */
                struct timespec start, now;
                unsigned long sent = 0;

                clock_gettime(CLOCK_MONOTONIC, &start);
                for (;;)
                {
                        clock_gettime(CLOCK_MONOTONIC, &now);
                        if (now.tv_sec - start.tv_sec >= ms)
                                break;
                        fd = socket(ss.ss_family, SOCK_STREAM, 0);
                        if (fd < 0)
                                continue;
                        fw_nonblock(fd);
                        connect(fd, (void *)&ss, length);
                        close(fd);
                        sent++;
                }
                printf("sent %lu\n", sent);
                return 0;
        }

        fprintf(stderr, "firewall_probe: unknown mode %s\n", mode);
        return 2;
}
#endif /* CHECK_firewall_probe */

#ifdef CHECK_shim_security
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define MWI_CHECK
#include "../../src/shim/input.c"

static pthread_barrier_t shim_sec_barrier;

static void *shim_sec_note_worker(void *arg)
{
        unsigned int id = *(unsigned int *)arg;

        pthread_barrier_wait(&shim_sec_barrier);
        for (unsigned int i = 0; i < 200000; i++)
                mwi_note(id ? 'W' : 'S', (unsigned long long)i, 0, (int)id, (int)i, 0, 0);
        return NULL;
}

int main(void)
{
        unsigned int checks = 0, failures = 0;
        pthread_t t0, t1;
        unsigned int id0 = 0, id1 = 1;

        /* SHIM-2: mwi_button(0, ...) must not evaluate 1u << (0 - 1) */
        mwi.held = 0;
        mwi_button(0, 1, 0, 0, 0);
        mwi_button(0, 0, 0, 0, 0);
        checks++;
        if (mwi.held != 0)
        {
                fprintf(stderr, "FAIL: mwi_button(0, ...) modified mwi.held (%u)\n", mwi.held);
                failures++;
        }

        /* SHIM-1: concurrent mwi_note() from event and render threads must not race or overflow mwi_log[] */
        mwi.log_looked = 1;
        mwi.log_fd = 0;
        mwi.logged = 0;
        pthread_barrier_init(&shim_sec_barrier, NULL, 2);
        pthread_create(&t0, NULL, shim_sec_note_worker, &id0);
        pthread_create(&t1, NULL, shim_sec_note_worker, &id1);
        pthread_join(t0, NULL);
        pthread_join(t1, NULL);
        pthread_barrier_destroy(&shim_sec_barrier);
        checks++;
        if (mwi.logged > MWI_LOG_RECORDS)
        {
                fprintf(stderr, "FAIL: mwi.logged overflowed MWI_LOG_RECORDS (%u > %u)\n", mwi.logged, MWI_LOG_RECORDS);
                failures++;
        }
        mwi_log_flush();

        printf("CHECK_shim_security: %u checks, %u failures\n", checks, failures);
        return failures ? 1 : 0;
}
#endif /* CHECK_shim_security */


