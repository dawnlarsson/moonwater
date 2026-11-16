/*
        moonwater on a Mac: the link verb, and nothing else.

                moonwater link ...      as on a machine (src/waterlink/command.c)

        The same link a machine runs, built for macOS on arm64: the keys,
        machines and groups are the user's, in ~/Library/Application Support/
        moonwater (host_darwin.c); the C library is libSystem's and the
        platform underneath is lib.util.c's Darwin part; a listener (link on)
        is this binary started again as `moonwater link serve`.

        Built with LLVM's clang, which takes -fdefer-ts (Apple's does not yet),
        against the SDK, entered at lib.c's Darwin _start:

            /opt/homebrew/opt/llvm/bin/clang -isysroot "$(xcrun --show-sdk-path)" \
                -O2 -w -fdefer-ts -fno-stack-protector -e _start \
                -o moonwater src/waterlink/mac.c

        Linux never builds this file; the link there is a verb of the shell
        (src/sh/host.c).

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#if !defined(__APPLE__)
#error "mac.c is the Mac's program; on Linux the link is a verb of the shell"
#endif

#include "../lib.util.c"
#include "../net/net.c"
#include "command.c"

/*      The audio modem as the link's carrier (modem.c on a machine) plays
        through Moonwater's sound server, which a Mac has none of, so here it
        never carries: no datagram is queued, there is no room, and its turn is
        never due. The Mac's side of the modem is the receiver,
        tools/modem_rx.c. */
static p8 link_modem_carrying(void) { return 0; }
static p8 link_modem_put(p8 address_to bytes, positive length, p64 when)
{
        (void)bytes;
        (void)length;
        (void)when;
        return 0;
}
static p64 link_modem_turn(p64 now)
{
        (void)now;
        return ~0ull;
}
static void link_modem_stop(void) {}
static p64 link_modem_floor(void) { return 0; }
static p64 link_modem_ceiling(void) { return 0; }
static p64 link_modem_queued_from(void) { return ~0ull; }
static positive link_modem_free(void) { return 0; }
static void link_modem_streams_say(void) {}
//      link modem send refuses before it gets here (link_modem_why).
static b32 modem_send_file(string_address path)
{
        (void)path;
        return 1;
}

b32 main(void)
{
        string_address address_to words = program_argument_list();
        positive count = (positive)program_argument_count();

        //      A Linux-only call answers ENOSYS here, not SIGSYS (lib.c's
        //      table names them Darwin's nosys).
        (void)system_signal_install(SIGSYS, (positive)SIG_IGN, 0, 0, null);

        if (count < 2 || !string_equals(words[1], "link"))
        {
                string_format(log_error, "moonwater on a Mac has the link verb alone: moonwater link ...\n");
                log_flush();
                return 2;
        }
        //      lib.c's _start exits as main returns, with no C library exit
        //      in between, so what the log holds is written here.
        b32 answer = link_main(words, count);

        log_flush();
        return answer;
}
