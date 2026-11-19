/*
        The window manager of Xwayland: the compositor is the X11 window manager of the rootless X server the bowl runs, over
        one connection it accepts on a socket of its own. It is included by wayland_host.c, after the shim's host, when
        CONFIG_MOONWATER_WAYLAND is on.

        THE SOCKET

        /run/moonwater/xwm/wm-0 is a unix socket in a directory of mode 0711 (no other user lists it); the socket is mode 0660,
        its group 1000 when the compositor is root, so the bowl's programs (uid 1000) reach it, and a peer whose uid is neither
        the compositor's nor, for a root compositor, 1000 is closed at once (SO_PEERCRED). A wrapper (xwayland-moonwater, a
        bowl program: the compositor does not start it) connects to it and gives the connected descriptor to Xwayland as its
        -wm argument, so Xwayland's end of the connection is its window manager's, and this end is ours: the setup handshake
        is ours to start, and Xwayland answers it as a server does. The peer is any program of uid 1000, so a connection is
        held to being an X server's: see ONE CONNECTION.

        ONE CONNECTION

        One Xwayland has one window manager, and each program run has an Xwayland of its own, so up to COMPOSITOR_XWM_SLOTS
        connections are live at once, one for each wrapper: a second connection from the same wrapper (its pid) takes the
        place of the first, which is closed (a wrapper whose display did not come up tries the next with a new connection).
        A connection that does not finish its setup within COMPOSITOR_XWM_SETUP_NS is closed, and so is one whose X server,
        the wrapper's child, has not come to the Wayland socket within COMPOSITOR_XWM_BIND_NS (compositor_xwm_server), or
        has left it: a program that only holds the socket holds no slot, and is offered no selection, for longer than that.
        So is one whose bytes break the codec's bounds (xwm_wire.c refuses every length it cannot hold, and the connection
        is dropped on the first refusal).

        WHAT THE WINDOW MANAGER DOES

        It selects SubstructureRedirect, SubstructureNotify and PropertyChange on the root, which makes it the one that
        maps and configures every top-level window. A MapRequest maps the window and, for a normal window, gives it the
        keyboard (SetInputFocus). A ConfigureRequest is honoured for the fields the client asked for (a fullscreen window
        keeps its size). A _NET_WM_STATE request with _NET_WM_STATE_FULLSCREEN makes the window fill the output: its X
        window is configured to the output's size at the origin, and the shim's row for it is arranged to cover the screen.
        A WL_SURFACE_SERIAL client message gives the window its X11 serial; the shim's surface that set the same serial
        (xwayland_surface_v1) names its row (wayland_serial_row), which is looked for on each pass until the shim has it.
        When the compositor asks a window to go (WINDOW_CLOSING on its row), the window manager sends it WM_DELETE_WINDOW, once for
        each time it is asked (compositor.close_asks).

        THE SELECTIONS

        The compositor holds the clipboard and the primary selection as copies of any types (compositor_clip_*, windows_clip_build's
        format), whoever made them, and the window manager is their X side, as Mutter's and wlroots' are (Xwayland binds no data
        device). A selection the compositor holds that an X client did not make is owned in X by the window manager's selection window:
        TARGETS is TARGETS, TIMESTAMP, MULTIPLE and each name the copy is offered under (its types as the atoms of their names, and for
        its text UTF8_STRING, TEXT, STRING, text/plain;charset=utf-8 and text/plain), interned before the ownership is taken so their
        atoms come before any request for them. A value is the copy's type read where it is (a descriptor of the compositor's own, so a
        newer copy does not change it half way), STRING made Latin-1. A value longer than a piece (XWM_INCR_CHUNK) goes by INCR, a piece
        each time the requestor deletes the last. MULTIPLE is each of its pairs answered into its property, one notify for them all
        (a pair that cannot be is None in the list). A value goes only to an X client whose window has the keys and has just had the
        person's key or button (a client's paste in the compositor's rule); TARGETS and TIMESTAMP go to any.

        An X client's copy (XFIXES SelectionNotify on CLIPBOARD or PRIMARY) is read once into the compositor's, as a Wayland client's
        is, when the person has just touched a window of that X client: its TARGETS (the names of the atoms asked for), then its best
        text (UTF8_STRING, the UTF-8 types, then STRING made UTF-8, then TEXT) as the copy's text, then each other target that is a
        Wayland type (a name with a slash), one after another into one memfd (ConvertSelection into a requestor window of its own, INCR
        too), however large, which is given its index and sealed, checked and kept as any copy (compositor_clip_keep) with that window as
        its owner, so it outlives its X owner and the window manager owns it in X again when that owner goes. The window manager is the
        clipboard manager too (CLIPBOARD_MANAGER): a SAVE_TARGETS of an owner that quits is answered once its copy is the compositor's.
        Every transfer is a record in a table that grows, let go of when it ends or moves nothing for COMPOSITOR_XWM_TRANSFER_NS;
        nothing is buffered past one reply or one piece.

        DRAG AND DROP

        XDND version 5 both ways (the drag and drop section below says each step). A Wayland drag over an X window is offered to the
        window manager by the shim (xdrag), and the window manager is its XDND source: XdndSelection owned by the selection window,
        XdndEnter with the drag's types, XdndPosition as it moves, XdndStatus told to the shim as the type and the action the X window
        takes, XdndDrop, the conversion answered from the Wayland source's send, and XdndFinished the drag's end (XdndLeave on a leave, a
        cancel or the source gone). An X drag over a Wayland window finds the window manager's proxy there (XdndAware, mapped over the
        Wayland window under the pointer while an X client drags), whose XdndEnter, XdndPosition, XdndLeave and XdndDrop are a Wayland drag
        of the X side's in the shim (its offer the X source's types, its receive a conversion of XdndSelection), answered by XdndStatus
        and XdndFinished. XdndActionCopy, Move and Ask are the drag's copy, move and ask: an X source's Ask is ask and the actions of its
        XdndActionList that Wayland has (copy alone when it lists none), and a Wayland source with a choice lists its own on the
        selection window (XdndActionList, XdndActionDescription); XdndFinished names the action the drag came to. An X target's
        XdndProxy is followed (a proxy that names itself: the messages go to it, naming the window under the pointer). The proxy is
        mapped while any button held on an X window, or a finger down on one, may be an X drag, under that button's pointer or that
        finger. The host's Escape ends an X drag on both sides: the Wayland drag of it ends, and the X source is given the Escape
        through XTEST (its grab has the keyboard). X's root is the compositor's pixels, its origin the top left of the screens
        (compositor_xwm_origin), and the X server is told its outputs so (wayland_xwayland_client), at any scale and layout.

        WHAT IT KEEPS

        A table of records that grows (the shim's struct wayland_table: its records never move), one for each top-level window the
        server reports (CreateNotify) or the client maps (MapRequest) or the shim names (serial), found by its X id through an index
        (open addressing, half full at most), and the property requests waited for in a ring that grows. A record the memory is not
        there for is refused and counted, as one past the fixed table of 64 was. The output queue and the input buffer are fixed: the
        queue holds back the reads of the server while it is full (flow, not a count), and the input is the largest message the
        protocol has. No thread, no fork.
*/
#ifndef COMPOSITOR_XWM_INCLUDED
#define COMPOSITOR_XWM_INCLUDED

#include "xwm_wire.c"

#define COMPOSITOR_XWM_DIR "/run/moonwater/xwm"
#define COMPOSITOR_XWM_PATH "/run/moonwater/xwm/wm-0"
#define COMPOSITOR_XWM_BACKLOG 4
#define COMPOSITOR_XWM_SLOTS 8                       // one window manager connection per X server (one Xwayland per program run): N slots
#define COMPOSITOR_XWM_POLLS (1 + COMPOSITOR_XWM_SLOTS) // what compositor_xwm_poll adds to the loop's list: the listening socket and each connection
#define COMPOSITOR_XWM_IN (XWM_REPLY_MAX + 4096)     // what the server has sent and no message is whole yet: one whole message and more
#define COMPOSITOR_XWM_OUT 131072                    // what is queued for the server (a selection's piece of 32 KiB and more); a queue that would overflow closes the connection
#define COMPOSITOR_XWM_SETUP_NS 5000000000ull        // a connection that has not finished its setup in this long is closed
#define COMPOSITOR_XWM_TARGETS_MOST 64               // the types of an X copy or drag taken, and their names asked: no toolkit offers more
#define COMPOSITOR_XWM_BIND_NS 15000000000ull        // nor one whose X server is not a Wayland client by then (the wrapper waits 5 s for it)
#define COMPOSITOR_XWM_ATOMS 63
#define COMPOSITOR_XWM_A_PROTOCOLS 0
#define COMPOSITOR_XWM_A_DELETE 1
#define COMPOSITOR_XWM_A_NET_STATE 2
#define COMPOSITOR_XWM_A_FULLSCREEN 3
#define COMPOSITOR_XWM_A_SERIAL 4
#define COMPOSITOR_XWM_A_ALLOW_COMMITS 5
#define COMPOSITOR_XWM_A_WM_S0 6
#define COMPOSITOR_XWM_A_WM_STATE 7
#define COMPOSITOR_XWM_A_NET_NAME 8
#define COMPOSITOR_XWM_A_UTF8 9
#define COMPOSITOR_XWM_A_TAKE_FOCUS 10
#define COMPOSITOR_XWM_A_TYPE 11              // _NET_WM_WINDOW_TYPE, then the types that are popups: tooltip, drop-down menu, popup menu,
#define COMPOSITOR_XWM_A_TYPE_FIRST 12        // combo, notification, drag and drop
#define COMPOSITOR_XWM_A_TYPE_LAST 17
#define COMPOSITOR_XWM_A_NET_SUPPORTED 18
#define COMPOSITOR_XWM_A_NET_CHECK 19
#define COMPOSITOR_XWM_A_NET_MOVERESIZE 20
#define COMPOSITOR_XWM_A_NET_ACTIVE 21
#define COMPOSITOR_XWM_A_NET_CLOSE 22
#define COMPOSITOR_XWM_A_MAX_VERT 23
#define COMPOSITOR_XWM_A_MAX_HORZ 24
#define COMPOSITOR_XWM_A_FOCUSED 25
#define COMPOSITOR_XWM_A_HIDDEN 26
#define COMPOSITOR_XWM_A_CHANGE_STATE 27
#define COMPOSITOR_XWM_A_TYPE_NORMAL 28
#define COMPOSITOR_XWM_A_TYPE_DIALOG 29
#define COMPOSITOR_XWM_A_TYPE_UTILITY 30
#define COMPOSITOR_XWM_A_TYPE_SPLASH 31
#define COMPOSITOR_XWM_A_CLIPBOARD 32
#define COMPOSITOR_XWM_A_TARGETS 33
#define COMPOSITOR_XWM_A_TIMESTAMP 34
#define COMPOSITOR_XWM_A_INCR 35
#define COMPOSITOR_XWM_A_TEXT 36
#define COMPOSITOR_XWM_A_MIME_UTF8 37         // text/plain;charset=utf-8
#define COMPOSITOR_XWM_A_MIME_TEXT 38         // text/plain
#define COMPOSITOR_XWM_A_SELECTION 39         // _MOONWATER_SELECTION: the property every conversion the window manager asks for lands in
#define COMPOSITOR_XWM_A_XDND_AWARE 40
#define COMPOSITOR_XWM_A_XDND_ENTER 41
#define COMPOSITOR_XWM_A_XDND_POSITION 42
#define COMPOSITOR_XWM_A_XDND_STATUS 43
#define COMPOSITOR_XWM_A_XDND_LEAVE 44
#define COMPOSITOR_XWM_A_XDND_DROP 45
#define COMPOSITOR_XWM_A_XDND_FINISHED 46
#define COMPOSITOR_XWM_A_XDND_SELECTION 47
#define COMPOSITOR_XWM_A_XDND_TYPES 48        // XdndTypeList
#define COMPOSITOR_XWM_A_XDND_COPY 49
#define COMPOSITOR_XWM_A_XDND_MOVE 50
#define COMPOSITOR_XWM_A_XDND_ASK 51
#define COMPOSITOR_XWM_A_MULTIPLE 52
#define COMPOSITOR_XWM_A_ATOM_PAIR 53
#define COMPOSITOR_XWM_A_MANAGER 54          // CLIPBOARD_MANAGER: the window manager is the clipboard manager (SAVE_TARGETS)
#define COMPOSITOR_XWM_A_SAVE_TARGETS 55
#define COMPOSITOR_XWM_A_XDND_PROXY 56
#define COMPOSITOR_XWM_A_XDND_ACTIONS 57      // XdndActionList
#define COMPOSITOR_XWM_A_XDND_DESCRIPTIONS 58 // XdndActionDescription
#define COMPOSITOR_XWM_A_XDND_PRIVATE 59
#define COMPOSITOR_XWM_A_XDND_LINK 60
#define COMPOSITOR_XWM_A_STEAM 61            // _MOONWATER_STEAM_OVERLAY: a game's window for Steam's overlay (its pid)
#define COMPOSITOR_XWM_A_STEAM_OPEN 62       // _MOONWATER_STEAM_OVERLAY_OPEN: whether the overlay is open
#define COMPOSITOR_XWM_XA_PRIMARY 1
#define COMPOSITOR_XWM_XA_INTEGER 19
#define COMPOSITOR_XWM_MOVERESIZE_MOVE 8u
#define COMPOSITOR_XWM_MOVERESIZE_CANCEL 11u
#define COMPOSITOR_XWM_XA_WM_TRANSIENT_FOR 68
#define COMPOSITOR_XWM_XA_WINDOW 33
#define COMPOSITOR_XWM_XA_WM_HINTS 35
#define COMPOSITOR_XWM_XA_WM_NAME 39          // predefined atoms: WM_HINTS, WM_NAME, WM_NORMAL_HINTS, and the types STRING and ATOM
#define COMPOSITOR_XWM_XA_WM_NORMAL_HINTS 40
#define COMPOSITOR_XWM_XA_STRING 31
#define COMPOSITOR_XWM_TITLE 128              // a title's bytes, its NUL included (the shim takes fewer)
#define COMPOSITOR_XWM_PROP_NET_NAME 1
#define COMPOSITOR_XWM_PROP_NAME 2
#define COMPOSITOR_XWM_PROP_PROTOCOLS 3
#define COMPOSITOR_XWM_PROP_TRANSIENT 4
#define COMPOSITOR_XWM_PROP_TYPE 5
#define COMPOSITOR_XWM_PROP_STATE 6
#define COMPOSITOR_XWM_PROP_WM_HINTS 7
#define COMPOSITOR_XWM_PROP_NORMAL_HINTS 8
#define COMPOSITOR_XWM_PROP_OWNER 9          // not a property: X-Resource's pid of the window's client (compositor_xwm_ask_owner)
#define COMPOSITOR_XWM_PROP_INTERN 10         // an InternAtom of a type's name (slot: the atom's record)
#define COMPOSITOR_XWM_PROP_ATOM_NAME 11      // a GetAtomName (slot: the atom's record)
#define COMPOSITOR_XWM_PROP_SEL 12            // a piece of a conversion (slot: the transfer; window: its requestor window)
#define COMPOSITOR_XWM_PROP_SEL_SYNC 13       // a reply that says every name asked before it has come (slot, window: the transfer)
#define COMPOSITOR_XWM_PROP_XDND_AWARE 14     // the XdndAware of the X window a Wayland drag is over (window: it)
#define COMPOSITOR_XWM_PROP_XDND_TYPES 15     // the XdndTypeList of an X drag's source (window: the source)
#define COMPOSITOR_XWM_PROP_XDND_SYNC 16      // a reply that says the names of an X drag's types have come (window: the source)
#define COMPOSITOR_XWM_PROP_MULTIPLE 17       // a MULTIPLE's pairs (slot: its answer; window: its requestor)
#define COMPOSITOR_XWM_PROP_XDND_PROXY 18     // the XdndProxy of the X window a Wayland drag is over (window: it)
#define COMPOSITOR_XWM_PROP_XDND_PROXIED 19   // the XdndProxy of that proxy, which names itself when it is one (window: the X window; slot: the proxy)
#define COMPOSITOR_XWM_PROP_XDND_ACTIONS 20   // the XdndActionList of an X drag's source (window: the source)
#define COMPOSITOR_XWM_PROP_XDND_WORDS 21     // its XdndActionDescription (window: the source)
#define COMPOSITOR_XWM_PROP_STEAM 22          // _MOONWATER_STEAM_OVERLAY of a window (the game's pid)
#define COMPOSITOR_XWM_PROP_STEAM_OPEN 23     // _MOONWATER_STEAM_OVERLAY_OPEN of that window
#define COMPOSITOR_XWM_PROP_CLASS 24          // WM_CLASS: the instance and the class, two strings (a window's app_id: compositor_xwm_app)
#define COMPOSITOR_XWM_XA_WM_CLASS 67         // a predefined atom
#define COMPOSITOR_XWM_DND_REPLY(what) (((what) >= COMPOSITOR_XWM_PROP_XDND_AWARE && (what) <= COMPOSITOR_XWM_PROP_XDND_SYNC) || \
                                        ((what) >= COMPOSITOR_XWM_PROP_XDND_PROXY && (what) <= COMPOSITOR_XWM_PROP_XDND_WORDS))
#define COMPOSITOR_XWM_PROTO_DELETE 1u
#define COMPOSITOR_XWM_PROTO_FOCUS 2u
#define COMPOSITOR_XWM_NO_PLACE (-100000)
#define COMPOSITOR_XWM_ROOM 2048              // the room left in the output queue that a message is taken with
#define COMPOSITOR_XWM_STATE_NORMAL 1
#define COMPOSITOR_XWM_STATE_ICONIC 3u
#define COMPOSITOR_XWM_OP_SET_SELECTION_OWNER 22
#define COMPOSITOR_XWM_ALLOW_TYPE 6          // CARDINAL
#define COMPOSITOR_XWM_ATOM_FIRST_SEQ 2              // the sequence of the first InternAtom: ChangeWindowAttributes on the root is the first request
#define COMPOSITOR_XWM_COMPOSITE_SEQ (COMPOSITOR_XWM_ATOM_FIRST_SEQ + COMPOSITOR_XWM_ATOMS) // QueryExtension("Composite"), after the atoms
#define COMPOSITOR_XWM_COMPOSITE_NAME "Composite"
#define COMPOSITOR_XWM_XRES_SEQ (COMPOSITOR_XWM_COMPOSITE_SEQ + 1) // QueryExtension("X-Resource"), after Composite's
#define COMPOSITOR_XWM_XRES_NAME "X-Resource"
#define COMPOSITOR_XWM_XFIXES_SEQ (COMPOSITOR_XWM_COMPOSITE_SEQ + 2) // QueryExtension("XFIXES"), after X-Resource's
#define COMPOSITOR_XWM_XFIXES_NAME "XFIXES"
#define COMPOSITOR_XWM_XTEST_SEQ (COMPOSITOR_XWM_COMPOSITE_SEQ + 3) // QueryExtension("XTEST"), after XFIXES's
#define COMPOSITOR_XWM_XTEST_NAME "XTEST"
#define COMPOSITOR_XWM_KEY_ESCAPE 9                  // Escape's X keycode (evdev's 1, and 8)
#define COMPOSITOR_XWM_XA_WINDOW_TYPE 33             // the predefined WINDOW, XdndProxy's type
#define COMPOSITOR_XWM_ATOM_TYPE 4                   // the predefined ATOM
#define COMPOSITOR_XWM_NET_REMOVE 0
#define COMPOSITOR_XWM_NET_ADD 1
#define COMPOSITOR_XWM_NET_TOGGLE 2
#define COMPOSITOR_XWM_MAPPED 1u
#define COMPOSITOR_XWM_OVERRIDE 2u
#define COMPOSITOR_XWM_FULLSCREEN 4u
#define COMPOSITOR_XWM_SERIAL 8u
#define COMPOSITOR_XWM_MAXIMIZED 16u
#define COMPOSITOR_XWM_NET_TITLE 32u          // the title is _NET_WM_NAME's, which WM_NAME does not replace
#define COMPOSITOR_XWM_ADOPTED 64u            // the shim has the window's surface and has been told what the window is
#define COMPOSITOR_XWM_PROTO_KNOWN 128u       // WM_PROTOCOLS has been read
#define COMPOSITOR_XWM_TITLE_AGAIN 256u       // the title changed while its requests were in flight
#define COMPOSITOR_XWM_POPUPTYPE 512u         // its _NET_WM_WINDOW_TYPE is a popup's: it is shown as an override-redirect window is
#define COMPOSITOR_XWM_PROPS_READ 1024u       // the properties asked for at the map (the last is the type) have come
#define COMPOSITOR_XWM_RAISED 2048u           // a transient window has been raised over its parent once
#define COMPOSITOR_XWM_COVERED 4096u          // made fullscreen for mapping over a whole screen (compositor_xwm_covers), not by asking
#define COMPOSITOR_XWM_POPUPISH (COMPOSITOR_XWM_OVERRIDE | COMPOSITOR_XWM_POPUPTYPE)
#define COMPOSITOR_XWM_EAGAIN (-11)
#define COMPOSITOR_XWM_MSG_DONTWAIT 0x40
#define COMPOSITOR_XWM_MSG_NOSIGNAL 0x4000
#define COMPOSITOR_XWM_SO_PEERCRED 17

struct compositor_xwm_window
{
        unsigned int id;               // the X window; 0 for a free record
        unsigned int state;            // COMPOSITOR_XWM_* bits
        unsigned long long serial;     // the X11 serial its WL_SURFACE_SERIAL gave, when COMPOSITOR_XWM_SERIAL is set
        int row;                       // the shim's row for the window, -1 until the shim has one
        unsigned int protocols;        // COMPOSITOR_XWM_PROTO_* from WM_PROTOCOLS
        unsigned char closes;          // the asks to close of its row that it has been told of (compositor.close_asks)
        unsigned int transient;        // the window it is transient for (WM_TRANSIENT_FOR), 0 for none
        int x, y;                      // the X window's place and size, as the server last said (CreateNotify, ConfigureNotify)
        unsigned int width, height;
        int sent_x, sent_y;            // what the compositor last asked of its place and size
        unsigned int sent_w, sent_h;
        unsigned long long sync_present; // last present a resize configure was sent on
        unsigned int restore_w, restore_h; // its size before fullscreen
        unsigned int min_w, min_h, max_w, max_h; // WM_NORMAL_HINTS size bounds (0 = unset)
        unsigned char placed;          // WM_NORMAL_HINTS has USPosition or PPosition: its place is its own, kept where X says
        unsigned char no_input;        // 1 if WM_HINTS InputHint is explicitly False
        unsigned int owner;            // the pid of the program whose window it is, as X-Resource says (0 until it has; compositor_xwm_owned)
        unsigned int asking;           // title requests in flight
        unsigned int steam_pid;        // the game it is Steam's overlay window for (_MOONWATER_STEAM_OVERLAY), 0 for none
        unsigned char steam_open;      // the overlay is open (_MOONWATER_STEAM_OVERLAY_OPEN): the person's input is the overlay's
        unsigned char steam_focused;   // told FocusIn, as the renderer takes the hot-key from the window with the focus
        unsigned char steam_tab;       // the Tab of a Shift+Tab sent it is held: its repeats and its release are the overlay's too
        int steam_x, steam_y;          // the pointer's last place, in the X window's pixels (the game's frame's)
        int steam_wheel[2];            // a wheel's turn not yet a whole notch (vertical, horizontal), in its own units
        unsigned int steam_told;       // when the overlay's state was last told (milliseconds): an open one beats every second
        unsigned int dirty;            // the shim has not the window's latest title
        char title[COMPOSITOR_XWM_TITLE];
        char app[WINDOWS_APP_ID];      // its WM_CLASS's class (else its instance), the app_id its row is given (compositor_xwm_app)
};

struct compositor_xwm_pending
{
        unsigned int seq, window, what; // the request's sequence, the window it is for (0: free), and COMPOSITOR_XWM_PROP_*
        unsigned int slot;              // the record it is for, when it is not a window's
};

// A name the window manager knows the atom of (or asks for, the atom 0 until its reply), or an atom it knows the name of.
struct compositor_xwm_atom
{
        unsigned int atom;
        unsigned int at, length; // its name in the names table
        unsigned char known;     // both the atom and the name are known
};

/*
        A conversion the window manager asked for (it is the requestor): its window (one of its own for each, 0 for a free record), the
        selection (0 the clipboard, 1 the primary selection, 2 XdndSelection), how far it is (TARGETS asked, the value asked, its pieces
        coming by INCR, the names of TARGETS awaited), and where the bytes go: into the memfd of a copy the compositor will keep (eager),
        or into the write end of a pipe a pump empties into a Wayland client's descriptor. offset is where in the property the next
        GetProperty starts (a piece longer than a reply holds), ready that a piece waits in the property for room in the pipe.
*/
struct compositor_xwm_in
{
        unsigned int window;
        unsigned char which, stage, eager, latin1, incr, asking, ready;
        unsigned int selection, target, time, text; // text: the target an eager copy's text is read as (0 for none)
        int fd, row, index;                         // index: an eager copy's index, a record a type read whole
        long pump, next;                            // next: the TARGETS an eager copy looks at after this one (-1: its text first)
        unsigned long done, offset, total, count;   // total and count: an eager copy's bytes and types kept before this one
        positive deadline;
        struct wayland_table targets; // TARGETS: the atoms the owner listed
};

/*
        A conversion the window manager answers (it is the owner): the requestor's window and property (0 for a free record), the type
        written, where the bytes come from (the compositor's text, a descriptor of its own; or a pipe a pump fills from a Wayland
        source), its size when it is known, how far it is (to start, a piece written and its delete awaited, the next piece to write),
        and STRING's Latin-1 made a piece at a time.
*/
struct compositor_xwm_out
{
        unsigned int requestor, property, type, selection, target, time;
        unsigned char which, from, latin1, stage, quiet; // quiet: a part of a MULTIPLE, whose one notify is its own
        struct xwm_utf8_cut cut;
        int fd;
        long pump;
        unsigned long base, offset, size; // base: where the type is in the compositor's copy
        positive deadline;
};

// A selection's X side: its atom, whether the window manager owns it in X (and from when), the X window that does when one does,
// whether its ownership is to be looked at again (dirty), and an X copy being read.
struct compositor_xwm_selection
{
        unsigned int atom, owned_time, x_owner;
        unsigned char owned, dirty;
        long reading;
};

/*
        A Wayland drag over an X window, the window manager its XDND source (DRAG AND DROP): the X window it is over (0 for none) and its
        row, the version its XdndAware says (none read yet, or none: aware_no), whether it was entered, a position waits for its status
        (waiting) and the pointer moved since (moved), the last place, and the source's action as X's atom.
*/
struct compositor_xwm_wdrag
{
        unsigned int target, version, place, action;
        unsigned int proxy; // where its messages go: the X window's XdndProxy when it names one that names itself, else the window
        unsigned char live, entered, waiting, moved, dropped, aware_no;
        int row;
};

/*
        An X drag over the Wayland windows, the window manager's proxy its target: the X source's window and version, its types (their
        atoms, whose names are asked for: naming until they have come), whether the Wayland drag of it is begun, dropped or was cancelled
        under it (until the source's next enter), its action atom and its drop's time, what it was last told (told_*), and the proxy.
*/
struct compositor_xwm_xdrag
{
        unsigned int source, version, action, time, told_action;
        unsigned int listed, words; // XdndActionList as the drag's bits (XWM_DND_*), and how many words its XdndActionDescription has
        unsigned char entered, naming, began, dropped, cancelled, told_accept, proxy_mapped, escaped;
        int proxy_row, proxy_x, proxy_y, proxy_w, proxy_h;
        struct wayland_table types;
};

struct compositor_xwm
{
        int fd, ready, broken, claimed, composite;  // composite: 1 once the Composite extension has redirected the root (the slot's own connection: fd -1 is a free slot)
        unsigned int xtest;            // XTEST's major opcode (an X drag's Escape is given to its source through it), 0 without it
        unsigned int xres;             // X-Resource's major opcode, 0 without it or when its pids are not this compositor's to use
        int peer_pid;                  // the window manager's peer: the wrapper that started the server (its child is the server), from the credentials
        long client, client_pid;       // the wayland client that is this X server (its parent is peer_pid), and its pid when found; -1 until it is
        unsigned int resource_base, resource_mask;
        int poll_at;                   // where the two polls are in the loop's list, relative to the wayland polls; -1 without a socket
        unsigned int root, seq, atoms[COMPOSITOR_XWM_ATOMS];
        unsigned int atoms_seen, refused, errors;
        long in_len, out_len;
        positive since;
        unsigned char in[COMPOSITOR_XWM_IN], out[COMPOSITOR_XWM_OUT];
        struct wayland_table window_table, window_index; // the records, and the index by X id (each bucket a record's slot and one)
        struct compositor_xwm_window *window;
        long windows;                  // records in use
        struct wayland_table pending_table; // the ring of property requests waited for: pending_room of them, grown by doubling
        struct compositor_xwm_pending *pending;
        unsigned int pending_at, pending_count, pending_room; // the oldest request waited for, how many are, and the ring's size
        int focus_row;                 // the row with the keys at the last pass
        unsigned int last_stack_gen;   // compositor.stack_gen last synced to X11 stacking order
        unsigned int focus_window;     // and the X window that row was then (0 for none)
        // The selections (THE SELECTIONS): XFIXES's opcode and first event (0 without it), which selection the window manager is making
        // the compositor's (its number and one, so its own change is not taken for another's), the next requestor window's number, the
        // three selections, the atoms and names known, the conversions asked for and answered, and the types of an X copy as a list.
        unsigned int xfixes_opcode, xfixes_event, setting, next_requestor;
        struct compositor_xwm_selection sel[3];
        struct wayland_table atom_table, name_table, in_table, out_table, type_table;
        struct wayland_table atom_index; // the atoms by their number (each bucket a record's slot and one: compositor_xwm_atom_index)
        long atoms_indexed;
        struct compositor_xwm_atom *atom;
        struct compositor_xwm_in *ins;
        struct compositor_xwm_out *outs;
        long name_bytes;
        struct compositor_xwm_wdrag wdrag;
        struct compositor_xwm_xdrag xdrag;
        // A SAVE_TARGETS of the clipboard's X owner, answered once its copy is the compositor's (requestor 0 for none).
        struct
        {
                unsigned int requestor, property, time;
        } save;
};

/*
        The window manager's connections (THE SLOTS): one per X server, each a whole compositor_xwm (its own window records, selections,
        drags, input and output queues) and free when its fd is -1. compositor_xwm_at is the slot the code is working on: every
        function reads it as `compositor_xwm` (the macro below), so a function that serves one server runs on that server's slot.
        The listening socket and the user the servers may be are the compositor's (compositor_xwm_listen_fd, compositor_xwm_uid).
*/
#define COMPOSITOR_XWM_FRESH {.fd = -1, .poll_at = -1, .focus_row = -2, .client = -1, .client_pid = -1, \
                              .sel = {{0, 0, 0, 0, 0, -1}, {0, 0, 0, 0, 0, -1}, {0, 0, 0, 0, 0, -1}}, \
                              .xdrag = {.proxy_row = -1}}
static const struct compositor_xwm compositor_xwm_fresh = COMPOSITOR_XWM_FRESH;
static struct compositor_xwm compositor_xwm_slots[COMPOSITOR_XWM_SLOTS] = {[0 ... COMPOSITOR_XWM_SLOTS - 1] = COMPOSITOR_XWM_FRESH};
static struct compositor_xwm *compositor_xwm_at = &compositor_xwm_slots[0];
static unsigned int compositor_xwm_counted[4]; // the table's xwm_counts: refused connections, refused records, refused MapRequests, closed connections
static int compositor_xwm_listen_fd = -1, compositor_xwm_listen_at = -1; // the listening socket, and its place in the loop's list
static int compositor_xwm_uid;                                            // the user the servers may be (the compositor's own, or 1000 under root)

static struct compositor_xwm *compositor_xwm_slot_of_row(long row);
static long compositor_xwm_server(struct compositor_xwm *x);
static void compositor_xwm_drop(const char *why);
static unsigned int compositor_xwm_id(unsigned int number);
static void compositor_xwm_atoms_seed(void);
static void compositor_xwm_flush(void);

#define COMPOSITOR_XWM_SEL_WINDOW 2u                 // the selection window's number (the check window is 1)
#define COMPOSITOR_XWM_PROXY_WINDOW 3u               // XDND's proxy over the Wayland windows
#define COMPOSITOR_XWM_FIRST_REQUESTOR 16u           // the requestor windows' numbers start here

/* A line of the compositor's log, from a text that is not a literal. */
static void compositor_xwm_log(const char *text)
{
        unsigned long length = 0;

        while (text[length])
                length++;
        log_direct((address_any)text, (positive)length);
}

/* ---- the requests: each is encoded by xwm_wire.c, queued for the server, and counted in its sequence ---- */

/* Queues one request: its sequence is the count of requests queued, so the server's replies to it carry this number. */
static int compositor_xwm_queue(const unsigned char *bytes, long count)
{
        struct compositor_xwm *x = compositor_xwm_at;

        if (count <= 0)
                return -1;
        if (x->out_len + count > COMPOSITOR_XWM_OUT)
        {
                x->broken = 1;
                return -1;
        }
        memory_copy(x->out + x->out_len, bytes, (positive)count);
        x->out_len += count;
        x->seq = (x->seq + 1) & 0xffffu;
        return 0;
}

static void compositor_xwm_window_op(unsigned int opcode, unsigned int window)
{
        unsigned char req[XWM_REQUEST_SHORT];
        long n = xwm_enc_window_op(req, sizeof req, opcode, window);

        if (n > 0)
                compositor_xwm_queue(req, n);
}

static void compositor_xwm_active(unsigned int window)
{
        unsigned char req[64], data[4];
        long n;

        if (compositor_xwm_at->root && compositor_xwm_at->atoms[COMPOSITOR_XWM_A_NET_ACTIVE])
        {
                xwm_put32(data, window);
                n = xwm_enc_change_property(req, sizeof req, XWM_PROP_REPLACE, compositor_xwm_at->root,
                                            compositor_xwm_at->atoms[COMPOSITOR_XWM_A_NET_ACTIVE], COMPOSITOR_XWM_XA_WINDOW,
                                            XWM_FORMAT_32, data, 4);
                if (n > 0)
                        compositor_xwm_queue(req, n);
        }
}

static void compositor_xwm_focus(unsigned int window)
{
        unsigned char req[64];
        long n = xwm_enc_set_input_focus(req, 16, XWM_REVERT_POINTER_ROOT, window, XWM_CURRENT_TIME);

        if (n > 0)
                compositor_xwm_queue(req, n);
        compositor_xwm_active(window);
}

static void compositor_xwm_configure(unsigned int window, const struct xwm_configure *c)
{
        unsigned char req[64];
        long n = xwm_enc_configure_window(req, sizeof req, window, c);

        if (n > 0)
                compositor_xwm_queue(req, n);
}

/* WM_STATE, as ICCCM 4.1.3.1 has the window manager set it when it maps a window: NormalState, with no icon window. */
static void compositor_xwm_wm_state(unsigned int window)
{
        unsigned char req[64], data[8];
        long n;

        xwm_put32(data, COMPOSITOR_XWM_STATE_NORMAL);
        xwm_put32(data + 4, 0);
        n = xwm_enc_change_property(req, sizeof req, XWM_PROP_REPLACE, window, compositor_xwm_at->atoms[COMPOSITOR_XWM_A_WM_STATE],
                                    compositor_xwm_at->atoms[COMPOSITOR_XWM_A_WM_STATE], XWM_FORMAT_32, data, 8);
        if (n > 0)
                compositor_xwm_queue(req, n);
}

/* The window manager's own window, input only, its ownership of WM_S0 (ICCCM 4.3.8), and the EWMH root properties
   (_NET_SUPPORTING_WM_CHECK and _NET_SUPPORTED) that toolkits check before asking for _NET_WM_MOVERESIZE or fullscreen. */
static void compositor_xwm_claim(void)
{
        static const unsigned int supported_atoms[] = {
            COMPOSITOR_XWM_A_NET_SUPPORTED,  COMPOSITOR_XWM_A_NET_CHECK,    COMPOSITOR_XWM_A_NET_NAME,
            COMPOSITOR_XWM_A_NET_STATE,      COMPOSITOR_XWM_A_FULLSCREEN,   COMPOSITOR_XWM_A_MAX_VERT,
            COMPOSITOR_XWM_A_MAX_HORZ,       COMPOSITOR_XWM_A_FOCUSED,      COMPOSITOR_XWM_A_HIDDEN,
            COMPOSITOR_XWM_A_NET_MOVERESIZE, COMPOSITOR_XWM_A_NET_ACTIVE,   COMPOSITOR_XWM_A_NET_CLOSE,
            COMPOSITOR_XWM_A_TYPE,           COMPOSITOR_XWM_A_TYPE_NORMAL,  COMPOSITOR_XWM_A_TYPE_DIALOG,
            COMPOSITOR_XWM_A_TYPE_UTILITY,   COMPOSITOR_XWM_A_TYPE_SPLASH};
        static const unsigned char wm_name[] = "moonwater";
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned char req[256], data[128];
        long n;
        unsigned int window = x->resource_base | (1u & x->resource_mask), count = 0;

        if (x->claimed)
                return;
        x->claimed = 1;
        n = xwm_enc_create_window(req, sizeof req, 0, window, x->root, -1, -1, 1, 1, XWM_CLASS_INPUT_ONLY, 0);
        if (n > 0)
                compositor_xwm_queue(req, n);
        n = xwm_request_start(req, sizeof req, 16, COMPOSITOR_XWM_OP_SET_SELECTION_OWNER);
        if (n > 0)
        {
                xwm_put32(req + 4, window);
                xwm_put32(req + 8, x->atoms[COMPOSITOR_XWM_A_WM_S0]);
                xwm_put32(req + 12, XWM_CURRENT_TIME);
                compositor_xwm_queue(req, n);
        }
        xwm_put32(data, window);
        n = xwm_enc_change_property(req, sizeof req, XWM_PROP_REPLACE, window, x->atoms[COMPOSITOR_XWM_A_NET_CHECK],
                                    COMPOSITOR_XWM_XA_WINDOW, XWM_FORMAT_32, data, 4);
        if (n > 0)
                compositor_xwm_queue(req, n);
        n = xwm_enc_change_property(req, sizeof req, XWM_PROP_REPLACE, window, x->atoms[COMPOSITOR_XWM_A_NET_NAME],
                                    x->atoms[COMPOSITOR_XWM_A_UTF8], XWM_FORMAT_8, wm_name, sizeof wm_name - 1);
        if (n > 0)
                compositor_xwm_queue(req, n);
        n = xwm_enc_change_property(req, sizeof req, XWM_PROP_REPLACE, x->root, x->atoms[COMPOSITOR_XWM_A_NET_CHECK],
                                    COMPOSITOR_XWM_XA_WINDOW, XWM_FORMAT_32, data, 4);
        if (n > 0)
                compositor_xwm_queue(req, n);
        xwm_put32(data, 0);
        n = xwm_enc_change_property(req, sizeof req, XWM_PROP_REPLACE, x->root, x->atoms[COMPOSITOR_XWM_A_NET_ACTIVE],
                                    COMPOSITOR_XWM_XA_WINDOW, XWM_FORMAT_32, data, 4);
        if (n > 0)
                compositor_xwm_queue(req, n);
        for (unsigned int i = 0; i < (unsigned int)(sizeof supported_atoms / sizeof supported_atoms[0]); i++)
                xwm_put32(data + 4 * count++, x->atoms[supported_atoms[i]]);
        for (unsigned int type = COMPOSITOR_XWM_A_TYPE_FIRST; type <= COMPOSITOR_XWM_A_TYPE_LAST; type++)
                xwm_put32(data + 4 * count++, x->atoms[type]);
        n = xwm_enc_change_property(req, sizeof req, XWM_PROP_REPLACE, x->root, x->atoms[COMPOSITOR_XWM_A_NET_SUPPORTED],
                                    COMPOSITOR_XWM_ATOM_TYPE, XWM_FORMAT_32, data, count * 4);
        if (n > 0)
                compositor_xwm_queue(req, n);
        // The selection window (THE SELECTIONS): input only, override-redirect, out of sight, its property changes told; and XDND's proxy,
        // XdndAware, unmapped until an X drag is over a Wayland window.
        {
                unsigned int selection = compositor_xwm_id(COMPOSITOR_XWM_SEL_WINDOW), proxy = compositor_xwm_id(COMPOSITOR_XWM_PROXY_WINDOW);
                unsigned int values[2] = {1, XWM_MASK_PROPERTY_CHANGE};

                n = xwm_enc_create_window(req, sizeof req, 0, selection, x->root, -1, -1, 1, 1, XWM_CLASS_INPUT_ONLY, 0);
                if (n > 0)
                        compositor_xwm_queue(req, n);
                n = xwm_enc_change_window_attributes(req, sizeof req, selection, XWM_CW_OVERRIDE_REDIRECT | XWM_CW_EVENT_MASK, values, 2);
                if (n > 0)
                        compositor_xwm_queue(req, n);
                n = xwm_enc_create_window(req, sizeof req, 0, proxy, x->root, 0, 0, 1, 1, XWM_CLASS_INPUT_ONLY, 0);
                if (n > 0)
                        compositor_xwm_queue(req, n);
                n = xwm_enc_change_window_attributes(req, sizeof req, proxy, XWM_CW_OVERRIDE_REDIRECT, values, 1);
                if (n > 0)
                        compositor_xwm_queue(req, n);
                xwm_put32(data, XWM_XDND_VERSION);
                n = xwm_enc_change_property(req, sizeof req, XWM_PROP_REPLACE, proxy, x->atoms[COMPOSITOR_XWM_A_XDND_AWARE], COMPOSITOR_XWM_ATOM_TYPE,
                                            XWM_FORMAT_32, data, 4);
                if (n > 0)
                        compositor_xwm_queue(req, n);
                x->sel[0].atom = x->atoms[COMPOSITOR_XWM_A_CLIPBOARD];
                x->sel[1].atom = COMPOSITOR_XWM_XA_PRIMARY;
                x->sel[2].atom = x->atoms[COMPOSITOR_XWM_A_XDND_SELECTION];
                compositor_xwm_atoms_seed();
        }
}

/* _XWAYLAND_ALLOW_COMMITS: the window manager's word that Xwayland may commit the window's surface, set once it manages the window. */
static void compositor_xwm_allow_commits(unsigned int window)
{
        unsigned char req[64], data[4];
        long n;

        xwm_put32(data, 1);
        n = xwm_enc_change_property(req, sizeof req, XWM_PROP_REPLACE, window, compositor_xwm_at->atoms[COMPOSITOR_XWM_A_ALLOW_COMMITS],
                                    COMPOSITOR_XWM_ALLOW_TYPE, XWM_FORMAT_32, data, 4);
        if (n > 0)
                compositor_xwm_queue(req, n);
}

/* The window's _NET_WM_STATE: fullscreen and/or maximized atoms. */
static void compositor_xwm_net_state(unsigned int window, int state)
{
        unsigned char req[64], data[16];
        unsigned int count = 0;
        long n;

        if ((state & COMPOSITOR_XWM_FULLSCREEN) && compositor_xwm_at->atoms[COMPOSITOR_XWM_A_FULLSCREEN])
                xwm_put32(data + 4 * count++, compositor_xwm_at->atoms[COMPOSITOR_XWM_A_FULLSCREEN]);
        if ((state & COMPOSITOR_XWM_MAXIMIZED) && compositor_xwm_at->atoms[COMPOSITOR_XWM_A_MAX_VERT] &&
            compositor_xwm_at->atoms[COMPOSITOR_XWM_A_MAX_HORZ])
        {
                xwm_put32(data + 4 * count++, compositor_xwm_at->atoms[COMPOSITOR_XWM_A_MAX_VERT]);
                xwm_put32(data + 4 * count++, compositor_xwm_at->atoms[COMPOSITOR_XWM_A_MAX_HORZ]);
        }
        n = xwm_enc_change_property(req, sizeof req, XWM_PROP_REPLACE, window, compositor_xwm_at->atoms[COMPOSITOR_XWM_A_NET_STATE],
                                    COMPOSITOR_XWM_ATOM_TYPE, XWM_FORMAT_32, data, count * 4);
        if (n > 0)
                compositor_xwm_queue(req, n);
}

/* A ClientMessage of WM_PROTOCOLS sent to the window: WM_DELETE_WINDOW, which the ICCCM asks a client to answer by going, or
   WM_TAKE_FOCUS, which a client that wants the keys given it takes with SetInputFocus. */
static void compositor_xwm_protocol(unsigned int window, unsigned int atom)
{
        unsigned int data[2] = {atom, XWM_CURRENT_TIME};
        unsigned char event[XWM_EVENT_SIZE], req[64];
        long n = xwm_enc_client_message(event, sizeof event, window, compositor_xwm_at->atoms[COMPOSITOR_XWM_A_PROTOCOLS], data, 2);

        if (n == XWM_EVENT_SIZE)
        {
                n = xwm_enc_send_event(req, sizeof req, 0, window, 0, event, XWM_EVENT_SIZE);
                if (n > 0)
                        compositor_xwm_queue(req, n);
        }
}

static const char *const compositor_xwm_names[COMPOSITOR_XWM_ATOMS] = {"WM_PROTOCOLS", "WM_DELETE_WINDOW", "_NET_WM_STATE",
                                                                "_NET_WM_STATE_FULLSCREEN", "WL_SURFACE_SERIAL",
                                                                "_XWAYLAND_ALLOW_COMMITS", "WM_S0", "WM_STATE", "_NET_WM_NAME",
                                                                "UTF8_STRING", "WM_TAKE_FOCUS", "_NET_WM_WINDOW_TYPE",
                                                                "_NET_WM_WINDOW_TYPE_TOOLTIP", "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
                                                                "_NET_WM_WINDOW_TYPE_POPUP_MENU", "_NET_WM_WINDOW_TYPE_COMBO",
                                                                "_NET_WM_WINDOW_TYPE_NOTIFICATION", "_NET_WM_WINDOW_TYPE_DND",
                                                                "_NET_SUPPORTED", "_NET_SUPPORTING_WM_CHECK", "_NET_WM_MOVERESIZE",
                                                                "_NET_ACTIVE_WINDOW", "_NET_CLOSE_WINDOW",
                                                                "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ",
                                                                "_NET_WM_STATE_FOCUSED", "_NET_WM_STATE_HIDDEN", "WM_CHANGE_STATE",
                                                                "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_WINDOW_TYPE_DIALOG",
                                                                "_NET_WM_WINDOW_TYPE_UTILITY", "_NET_WM_WINDOW_TYPE_SPLASH",
                                                                "CLIPBOARD", "TARGETS", "TIMESTAMP", "INCR", "TEXT", "text/plain;charset=utf-8",
                                                                "text/plain", "_MOONWATER_SELECTION", "XdndAware", "XdndEnter", "XdndPosition",
                                                                "XdndStatus", "XdndLeave", "XdndDrop", "XdndFinished", "XdndSelection",
                                                                "XdndTypeList", "XdndActionCopy", "XdndActionMove", "XdndActionAsk",
                                                                "MULTIPLE", "ATOM_PAIR", "CLIPBOARD_MANAGER", "SAVE_TARGETS", "XdndProxy",
                                                                "XdndActionList", "XdndActionDescription", "XdndActionPrivate", "XdndActionLink",
                                                                "_MOONWATER_STEAM_OVERLAY", "_MOONWATER_STEAM_OVERLAY_OPEN"};

static void compositor_xwm_intern(unsigned int at)
{
        unsigned char req[64];
        unsigned long length = 0;
        long n;

        while (compositor_xwm_names[at][length])
                length++;
        n = xwm_enc_intern_atom(req, sizeof req, 0, (const unsigned char *)compositor_xwm_names[at], length);
        if (n > 0)
                compositor_xwm_queue(req, n);
}

/* The setup and the first requests, once the server's setup reply is read: the root's events, and the atoms the events name. */
static void compositor_xwm_start_session(void)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned char req[64];
        long n;

        // SubstructureRedirect, SubstructureNotify, PropertyChange and StructureNotify on the root: the one window manager's
        // (a server with another running refuses this with BadAccess, the first error, and the connection is dropped).
        n = xwm_enc_select_events(req, sizeof req, x->root, XWM_MASK_WM_ROOT);
        if (n > 0)
                compositor_xwm_queue(req, n);
        for (unsigned int at = 0; at < COMPOSITOR_XWM_ATOMS; at++)
                compositor_xwm_intern(at);
        // The Composite extension: a rootless Xwayland makes a surface only for a window it redirects, and its reply
        // (QueryExtension, then QueryVersion and RedirectSubwindows of the root) is taken when it comes (compositor_xwm_composite).
        n = xwm_enc_query_extension(req, sizeof req, (const unsigned char *)COMPOSITOR_XWM_COMPOSITE_NAME, 9);
        if (n > 0)
                compositor_xwm_queue(req, n);
        // X-Resource, for whose each window is (compositor_xwm_owned): its reply at COMPOSITOR_XWM_XRES_SEQ (compositor_xwm_xres).
        n = xwm_enc_query_extension(req, sizeof req, (const unsigned char *)COMPOSITOR_XWM_XRES_NAME, 10);
        if (n > 0)
                compositor_xwm_queue(req, n);
        // XFIXES, which tells the window manager who owns a selection (THE SELECTIONS): its reply is taken when it comes.
        n = xwm_enc_query_extension(req, sizeof req, (const unsigned char *)COMPOSITOR_XWM_XFIXES_NAME, 6);
        if (n > 0)
                compositor_xwm_queue(req, n);
        // XTEST, through which an X drag's source is given the Escape the compositor took (compositor_xwm_drag_escape).
        n = xwm_enc_query_extension(req, sizeof req, (const unsigned char *)COMPOSITOR_XWM_XTEST_NAME, 5);
        if (n > 0)
                compositor_xwm_queue(req, n);
}

/*
        Where X's root is in the compositor's space. Xwayland's windows are drawn a buffer pixel a pixel, so the X root is in the
        compositor's pixels (Xwayland is told its outputs so, wayland_xwayland_client: each screen at its place in pixels and its size in
        pixels, whatever its scale); only its origin is another, the top left of the screens as laid out, since an X coordinate is
        no less than 0 where a screen may be laid out left of or above the compositor's origin. X places are compositor places less
        the origin, and the other way.
*/
static void compositor_xwm_origin(int *ox, int *oy)
{
        int count = compositor_output_count();

        *ox = *oy = 0;
        for (int at = 0; at < count; at++)
        {
                int x, y, width, height;

                compositor_output_rect(at, &x, &y, &width, &height);
                if (!at || x < *ox)
                        *ox = x;
                if (!at || y < *oy)
                        *oy = y;
        }
}

// The X place of a compositor place on an axis (vertical: y), and the compositor place of an X one.
static int compositor_xwm_to_x(int place, int vertical)
{
        int ox, oy;

        compositor_xwm_origin(&ox, &oy);
        return place - (vertical ? oy : ox);
}

static int compositor_xwm_from_x(int place, int vertical)
{
        int ox, oy;

        compositor_xwm_origin(&ox, &oy);
        return place + (vertical ? oy : ox);
}

/*
        The QueryExtension reply for X-Resource. Its pids are the server's view of its clients, so they are this compositor's to act
        on (the taskbar's force stop) only when the server is in the compositor's pid namespace: a server in an isolated bowl has
        its own numbers, which name other processes here. Without the extension, or in another namespace, an X window's row keeps
        the server's pid, and nothing that acts on a row's program acts on the server for one window.
*/
static void compositor_xwm_xres(const unsigned char *in, long size)
{
        struct compositor_xwm *x = compositor_xwm_at;
        struct xwm_extension_reply r;
        char mine[64], theirs[64], path[48] = "/proc/";
        long a, b;
        unsigned char req[16];
        long n;

        x->xres = 0;
        if (xwm_parse_extension_reply(in, (unsigned long)size, &r) != XWM_OK || !r.present || r.major_opcode < XWM_EXT_MAJOR_FIRST)
                return;
        if (x->peer_pid <= 1)
                return;
        positive_into_string((p8 *)path + 6, (positive)x->peer_pid);
        string_append_bounded((p8 *)path, "/ns/pid", sizeof path);
        a = compositor_call(syscall(readlinkat), (positive)AT_FDCWD, (positive) "/proc/self/ns/pid", (positive)mine, sizeof mine - 1, 0, 0);
        b = compositor_call(syscall(readlinkat), (positive)AT_FDCWD, (positive)path, (positive)theirs, sizeof theirs - 1, 0, 0);
        if (a <= 0 || a != b || memory_compare(mine, theirs, (positive)a))
        {
                compositor_xwm_log("xwm: the X server is in another pid namespace: its windows' rows keep its pid\n");
                return;
        }
        n = xwm_enc_xres_query_version(req, sizeof req, r.major_opcode, 1, 2);
        if (n > 0)
                compositor_xwm_queue(req, n);
        x->xres = r.major_opcode;
}

/*
        A window's owner, into its row: the row's pid becomes the program's (the shim made the row with the server's, the client of
        the shim). Only a pid that is a process here and none of the desktop's own: not 1, not this compositor, not the server.
*/
static void compositor_xwm_owned(struct compositor_xwm_window *w)
{
        struct windows *table = compositor.table;
        unsigned int now;

        if (!w->owner || w->row < 0 || !table || w->row >= (int)compositor_rows_in_use)
                return;
        now = __atomic_load_n(&table->pid[w->row], __ATOMIC_ACQUIRE);
        if (!now || now == WINDOWS_TAKING || now == w->owner)
                return;
        __atomic_compare_exchange_n(&table->pid[w->row], &now, w->owner, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

/* The QueryExtension reply for Composite: when the extension is there, the root's subwindows are redirected (Manual). */
static void compositor_xwm_composite(const unsigned char *in, long size)
{
        struct compositor_xwm *x = compositor_xwm_at;
        struct xwm_extension_reply r;
        unsigned char req[32];
        long n;

        if (xwm_parse_extension_reply(in, (unsigned long)size, &r) != XWM_OK)
        {
                x->broken = 1;
                return;
        }
        if (!r.present || r.major_opcode < XWM_EXT_MAJOR_FIRST)
        {
                compositor_xwm_log("xwm: no Composite extension: the X windows are not made surfaces\n");
                return;
        }
        n = xwm_enc_composite_query_version(req, sizeof req, r.major_opcode, 0, 4);
        if (n > 0)
                compositor_xwm_queue(req, n);
        n = xwm_enc_composite_redirect_subwindows(req, sizeof req, r.major_opcode, x->root, XWM_REDIRECT_MANUAL);
        if (n > 0)
                compositor_xwm_queue(req, n);
        x->composite = 1;
        compositor_xwm_log("xwm: the root's subwindows are redirected (Composite, Manual)\n");
}

/* ---- the properties: a request, and the table that says whose reply it is ---- */

/* GetProperty for a window's property (the first 128 bytes of it), the reply to be found by its sequence. The replies come back in
   the order of the requests, so the table is a queue: a reply or an error to the oldest one asked is its answer. 0 when the request
   is queued and its reply waited for, and -1 when it is not (nothing will answer it, so nothing is to wait for it). */
static int compositor_xwm_pending_grow(void)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned int room = x->pending_room ? 2 * x->pending_room : 512;
        struct wayland_table grown = {0, sizeof(struct compositor_xwm_pending), 0, 0, 0, 0, 0, 0, 0, 0};
        struct compositor_xwm_pending *to;

        wayland_tables_host(&compositor_wl_host);
        if (!wayland_table_reach(&grown, room))
                return 0;
        to = (struct compositor_xwm_pending *)grown.at;
        for (unsigned int k = 0; k < x->pending_count; k++)
                to[k] = x->pending[(x->pending_at + k) % x->pending_room];
        wayland_table_drop(&x->pending_table);
        x->pending_table = grown;
        x->pending = to;
        x->pending_at = 0;
        x->pending_room = room;
        return 1;
}

/* A request whose reply is waited for, queued and put in the ring: 0, or -1 when it is not (nothing waits for it then). */
static int compositor_xwm_wait(const unsigned char *req, long n, unsigned int window, unsigned int what, unsigned int slot)
{
        struct compositor_xwm *x = compositor_xwm_at;
        struct compositor_xwm_pending *p;

        if (n <= 0 || compositor_xwm_queue(req, n) != 0)
                return -1;
        // A ring that is full doubles: the requests waited for are laid in order at the start of a new one. Only the memory not being
        // there closes the connection (a reply that is forgotten leaves its window without the word it waits for, so it is never shown).
        if (x->pending_count == x->pending_room && !compositor_xwm_pending_grow())
        {
                x->broken = 1;
                return -1;
        }
        p = &x->pending[(x->pending_at + x->pending_count) % x->pending_room];
        p->seq = x->seq;
        p->window = window;
        p->what = what;
        p->slot = slot;
        x->pending_count++;
        return 0;
}

static int compositor_xwm_ask(unsigned int window, unsigned int what, unsigned int property, unsigned int type)
{
        unsigned char req[32];
        long n = xwm_enc_get_property(req, sizeof req, 0, window, property, type, 0, 32);

        return compositor_xwm_wait(req, n, window, what, 0);
}

/* X-Resource's word on whose the window is: the pid of the client that made it, as the server knows it from its connection (never
   what the client says of itself, as _NET_WM_PID is). Asked once a window maps, with the properties, and waited for the same way. */
static void compositor_xwm_ask_owner(unsigned int window)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned char req[16];
        long n;
        struct compositor_xwm_pending *p;

        if (!x->xres)
                return;
        n = xwm_enc_xres_client_pid(req, sizeof req, x->xres, window);
        if (n <= 0 || compositor_xwm_queue(req, n) != 0)
                return;
        if (x->pending_count == x->pending_room && !compositor_xwm_pending_grow())
        {
                x->broken = 1;
                return;
        }
        p = &x->pending[(x->pending_at + x->pending_count) % x->pending_room];
        p->seq = x->seq;
        p->window = window;
        p->what = COMPOSITOR_XWM_PROP_OWNER;
        x->pending_count++;
}

/* The oldest request waited for has been answered (or refused): it is no longer waited for. */
static void compositor_xwm_pending_done(void)
{
        struct compositor_xwm *x = compositor_xwm_at;

        x->pending_at = (x->pending_at + 1) % x->pending_room;
        x->pending_count--;
}

static struct compositor_xwm_window *compositor_xwm_find(unsigned int id, int add);
static void compositor_xwm_app(struct compositor_xwm_window *w);

static void compositor_xwm_ask_title(struct compositor_xwm_window *w)
{
        struct compositor_xwm *x = compositor_xwm_at;

        // A title that changes in a loop is asked of once at a time: what changes while the replies are awaited is asked of again.
        if (w->asking)
        {
                w->state |= COMPOSITOR_XWM_TITLE_AGAIN;
                return;
        }
        // The title is _NET_WM_NAME's when it has one, and WM_NAME's when it has not: WM_NAME is asked of only then (its reply is the
        // second of the two, and it is counted in asking until it comes). A request that could not be queued is not waited for: counted,
        // no reply ever took it off, and the window's title was never asked of again.
        w->asking = compositor_xwm_ask(w->id, COMPOSITOR_XWM_PROP_NET_NAME, x->atoms[COMPOSITOR_XWM_A_NET_NAME], x->atoms[COMPOSITOR_XWM_A_UTF8]) == 0;
}

/* One of a window's title requests has been answered, or refused: when the last is, a title that changed meanwhile is asked of. */
static void compositor_xwm_title_done(struct compositor_xwm_window *w)
{
        if (w->asking && --w->asking == 0 && (w->state & COMPOSITOR_XWM_TITLE_AGAIN))
        {
                w->state &= ~COMPOSITOR_XWM_TITLE_AGAIN;
                compositor_xwm_ask_title(w);
        }
}

/* A title's bytes as plain text, cut to what the record takes: control bytes become spaces, and a byte that is no valid UTF-8 (or any
   byte over 0x7f of a Latin-1 string) becomes a question mark. Answers the length. */
static unsigned int compositor_xwm_text(char *to, const unsigned char *from, unsigned long count, int latin1)
{
        unsigned int length = 0;

        for (unsigned long at = 0; at < count && length < COMPOSITOR_XWM_TITLE - 4;)
        {
                unsigned char c = from[at];

                if (c < 0x20 || c == 0x7f)
                {
                        to[length++] = ' ';
                        at++;
                        continue;
                }
                if (c < 0x80)
                {
                        to[length++] = (char)c;
                        at++;
                        continue;
                }
                if (latin1)
                {
                        to[length++] = '?';
                        at++;
                        continue;
                }
                {
                        memory_utf8_state state = {0};
                        unsigned long start = at;
                        b32 said = 0;

                        while (at < count)
                        {
                                said = memory_utf8_feed(&state, from[at]);
                                if (said != 0)
                                        break;
                                at++;
                        }
                        if (said > 0)
                        {
                                at++;
                                for (unsigned long k = start; k < at; k++)
                                        to[length++] = (char)from[k];
                        }
                        else
                        {
                                to[length++] = '?';
                                at = start + 1;
                        }
                }
        }
        to[length] = 0;
        return length;
}

/* A property reply, whole, for the request it answers. */
static void compositor_xwm_property(const unsigned char *in, long size, const struct compositor_xwm_pending *p)
{
        struct compositor_xwm *x = compositor_xwm_at;
        struct compositor_xwm_window *w;
        struct xwm_property_reply r;

        if (xwm_parse_property_reply(in, (unsigned long)size, &r) != XWM_OK)
        {
                x->broken = 1;
                return;
        }
        w = compositor_xwm_find(p->window, 0);
        if (!w)
                return;
        if (p->what == COMPOSITOR_XWM_PROP_NET_NAME || p->what == COMPOSITOR_XWM_PROP_NAME)
        {
                char text[COMPOSITOR_XWM_TITLE];
                unsigned int length = 0;

                if (r.format == XWM_FORMAT_8)
                        length = compositor_xwm_text(text, r.value, r.value_len, p->what == COMPOSITOR_XWM_PROP_NAME);
                if (p->what == COMPOSITOR_XWM_PROP_NET_NAME)
                {
                        if (length)
                                w->state |= COMPOSITOR_XWM_NET_TITLE;
                        else
                        {
                                // No _NET_WM_NAME: the title is WM_NAME's, asked of now. The request in hand stays counted until that reply.
                                // One that could not be queued is the end of this title's asking, as its reply would have been.
                                w->state &= ~COMPOSITOR_XWM_NET_TITLE;
                                if (compositor_xwm_ask(w->id, COMPOSITOR_XWM_PROP_NAME, COMPOSITOR_XWM_XA_WM_NAME, COMPOSITOR_XWM_XA_STRING) != 0)
                                        compositor_xwm_title_done(w);
                                return;
                        }
                }
                if ((p->what == COMPOSITOR_XWM_PROP_NET_NAME && length) || (p->what == COMPOSITOR_XWM_PROP_NAME && !(w->state & COMPOSITOR_XWM_NET_TITLE)))
                {
                        memory_copy(w->title, text, (positive)length + 1);
                        w->dirty = 1;
                }
                compositor_xwm_title_done(w);
        }
        else if (p->what == COMPOSITOR_XWM_PROP_STEAM)
        {
                // Whose overlay window it is: believed only once X-Resource says the window is that program's own (the owner asked now).
                w->steam_pid = r.format == XWM_FORMAT_32 && r.value_len >= 4 ? xwm_get32(r.value) : 0;
                if (w->steam_pid && !w->owner)
                        compositor_xwm_ask_owner(w->id);
        }
        else if (p->what == COMPOSITOR_XWM_PROP_CLASS)
        {
                // WM_CLASS is the instance and the class, each ended by a null: the class is the app's name (Steam), as a desktop entry's
                // StartupWMClass says it, and the instance stands in where there is no class. One word of printable ASCII, or none.
                unsigned long at = 0, start = 0, length = 0;

                if (r.format == XWM_FORMAT_8)
                {
                        while (at < r.value_len && r.value[at])
                                at++;
                        if (at + 1 < r.value_len && r.value[at + 1])
                                start = at + 1;
                        for (length = 0; start + length < r.value_len && r.value[start + length] && length < WINDOWS_APP_ID - 1; length++)
                                if (r.value[start + length] <= ' ' || r.value[start + length] >= 0x7f)
                                        break;
                        if (start + length < r.value_len && r.value[start + length])
                                length = 0;
                }
                memory_copy(w->app, r.value + start, length);
                w->app[length] = 0;
                compositor_xwm_app(w);
        }
        else if (p->what == COMPOSITOR_XWM_PROP_STEAM_OPEN)
        {
                w->steam_open = r.format == XWM_FORMAT_32 && r.value_len >= 4 && xwm_get32(r.value) != 0;
                w->steam_told = (unsigned int)(clock_monotonic_nanoseconds() / 1000000);
        }
        else if (p->what == COMPOSITOR_XWM_PROP_TRANSIENT)
                w->transient = r.format == XWM_FORMAT_32 && r.value_len >= 4 && xwm_get32(r.value) != w->id ? xwm_get32(r.value) : 0;
        else if (p->what == COMPOSITOR_XWM_PROP_STATE)
        {
                for (unsigned long at = 0; r.format == XWM_FORMAT_32 && at + 4 <= r.value_len; at += 4)
                {
                        unsigned int atom = xwm_get32(r.value + at);

                        if (atom && atom == x->atoms[COMPOSITOR_XWM_A_FULLSCREEN])
                                w->state |= COMPOSITOR_XWM_FULLSCREEN;
                        else if (atom && (atom == x->atoms[COMPOSITOR_XWM_A_MAX_VERT] || atom == x->atoms[COMPOSITOR_XWM_A_MAX_HORZ]))
                                w->state |= COMPOSITOR_XWM_MAXIMIZED;
                }
        }
        else if (p->what == COMPOSITOR_XWM_PROP_TYPE)
        {
                // A window of a popup's type (a tooltip, a menu, a combo's list, a notification, a drag) is shown as a popup.
                for (unsigned long at = 0; r.format == XWM_FORMAT_32 && at + 4 <= r.value_len; at += 4)
                        for (unsigned int type = COMPOSITOR_XWM_A_TYPE_FIRST; type <= COMPOSITOR_XWM_A_TYPE_LAST; type++)
                                if (x->atoms[type] && xwm_get32(r.value + at) == x->atoms[type])
                                        w->state |= COMPOSITOR_XWM_POPUPTYPE;
                w->state |= COMPOSITOR_XWM_PROPS_READ;
        }
        else if (p->what == COMPOSITOR_XWM_PROP_PROTOCOLS)
        {
                // A window with no WM_PROTOCOLS has none of the protocols (ICCCM 4.1.2.7): that is known as much as a list is.
                w->protocols = 0;
                for (unsigned long at = 0; r.format == XWM_FORMAT_32 && at + 4 <= r.value_len; at += 4)
                {
                        unsigned int atom = xwm_get32(r.value + at);

                        if (atom == x->atoms[COMPOSITOR_XWM_A_DELETE])
                                w->protocols |= COMPOSITOR_XWM_PROTO_DELETE;
                        else if (atom == x->atoms[COMPOSITOR_XWM_A_TAKE_FOCUS])
                                w->protocols |= COMPOSITOR_XWM_PROTO_FOCUS;
                }
                w->state |= COMPOSITOR_XWM_PROTO_KNOWN;
        }
        else if (p->what == COMPOSITOR_XWM_PROP_WM_HINTS)
        {
                w->no_input = 0;
                if (r.format == XWM_FORMAT_32 && r.value_len >= 8 && (xwm_get32(r.value) & 1u))
                        w->no_input = xwm_get32(r.value + 4) == 0;
        }
        else if (p->what == COMPOSITOR_XWM_PROP_NORMAL_HINTS)
        {
                w->min_w = w->min_h = w->max_w = w->max_h = 0;
                w->placed = 0;
                if (r.format == XWM_FORMAT_32 && r.value_len >= 4)
                {
                        unsigned int flags = xwm_get32(r.value);

                        // USPosition (1) or PPosition (4): the program or the person set where the window goes.
                        w->placed = (flags & 5u) != 0;

                        if ((flags & 16u) && r.value_len >= 28)
                        {
                                w->min_w = xwm_get32(r.value + 20);
                                w->min_h = xwm_get32(r.value + 24);
                        }
                        if ((flags & 32u) && r.value_len >= 36)
                        {
                                w->max_w = xwm_get32(r.value + 28);
                                w->max_h = xwm_get32(r.value + 32);
                        }
                }
        }
}

/* ---- the window table ---- */

static unsigned long compositor_xwm_hash(unsigned int id, long buckets)
{
        return ((unsigned long)id * 0x9e3779b97f4a7c15ul >> 24) & (unsigned long)(buckets - 1);
}

static void compositor_xwm_index_put(struct wayland_table *index, long slot)
{
        unsigned int *bucket = (unsigned int *)index->at;
        unsigned long b = compositor_xwm_hash(compositor_xwm_at->window[slot].id, index->count);

        while (bucket[b])
                b = (b + 1) & (unsigned long)(index->count - 1);
        bucket[b] = (unsigned int)(slot + 1);
}

// The record's slot for an X window, or -1; *at the bucket it is in.
static long compositor_xwm_slot(unsigned int id, unsigned long *at)
{
        struct compositor_xwm *x = compositor_xwm_at;
        long buckets = x->window_index.count;
        const unsigned int *bucket = (const unsigned int *)x->window_index.at;

        for (unsigned long b = buckets ? compositor_xwm_hash(id, buckets) : 0, n = 0; buckets && n < (unsigned long)buckets;
             n++, b = (b + 1) & (unsigned long)(buckets - 1))
        {
                if (!bucket[b])
                        return -1;
                if (x->window[bucket[b] - 1].id == id)
                {
                        if (at)
                                *at = b;
                        return (long)bucket[b] - 1;
                }
        }
        return -1;
}

// The record of an X window let go of: out of the index (backward shift), its slot free to be taken again.
static void compositor_xwm_release(struct compositor_xwm_window *w)
{
        struct compositor_xwm *x = compositor_xwm_at;
        long buckets = x->window_index.count, slot;
        unsigned int *bucket = (unsigned int *)x->window_index.at;
        unsigned long hole = 0;

        if (w->id && w->id == x->focus_window)
                x->focus_window = 0, x->focus_row = -3;
        if (!w->id || (slot = compositor_xwm_slot(w->id, &hole)) < 0)
        {
                w->id = 0;
                return;
        }
        bucket[hole] = 0;
        for (unsigned long b = (hole + 1) & (unsigned long)(buckets - 1); bucket[b]; b = (b + 1) & (unsigned long)(buckets - 1))
        {
                unsigned long home = compositor_xwm_hash(x->window[bucket[b] - 1].id, buckets);

                if ((b > hole && (home <= hole || home > b)) || (b < hole && home <= hole && home > b))
                {
                        bucket[hole] = bucket[b];
                        bucket[b] = 0;
                        hole = b;
                }
        }
        w->id = 0;
        wayland_table_give(&x->window_table, slot);
        x->windows--;
}

static struct compositor_xwm_window *compositor_xwm_find(unsigned int id, int add)
{
        struct compositor_xwm *x = compositor_xwm_at;
        struct compositor_xwm_window *free;
        long slot;

        if (!id)
                return 0;
        slot = compositor_xwm_slot(id, 0);
        if (slot >= 0)
                return &x->window[slot];
        if (!add)
                return 0;
        // The index doubles at half full (64 buckets first), every record put in the new one; then the record's slot.
        if (2 * (x->windows + 1) > x->window_index.count)
        {
                struct wayland_table grown = {0, sizeof(unsigned int), 0, 0, 0, 0, 0, 0, 0, 0};

                wayland_tables_host(&compositor_wl_host);
                if (!wayland_table_reach(&grown, x->window_index.count ? 2 * x->window_index.count : 64))
                {
                        x->refused++, compositor_xwm_counted[1]++;
                        return 0;
                }
                for (long at = 0; at < x->window_table.count; at++)
                        if (x->window[at].id)
                                compositor_xwm_index_put(&grown, at);
                wayland_table_drop(&x->window_index);
                x->window_index = grown;
        }
        x->window_table.unit = sizeof(struct compositor_xwm_window);
        slot = wayland_table_take(&x->window_table);
        if (slot < 0)
        {
                x->refused++, compositor_xwm_counted[1]++;
                return 0;
        }
        x->window = (struct compositor_xwm_window *)x->window_table.at;
        free = &x->window[slot];
        free->id = id;
        x->windows++;
        compositor_xwm_index_put(&x->window_index, slot);
        free->state = 0;
        free->serial = 0;
        free->row = -1;
        free->owner = 0;
        free->protocols = 0;
        free->closes = 0;
        free->transient = 0;
        free->placed = 0;
        free->x = free->y = 0;
        free->width = free->height = 0;
        free->sent_x = free->sent_y = COMPOSITOR_XWM_NO_PLACE;
        free->sent_w = free->sent_h = 0;
        free->restore_w = free->restore_h = 0;
        free->asking = 0;
        free->dirty = 0;
        free->title[0] = 0;
        return free;
}

static void compositor_xwm_forget(unsigned int id)
{
        struct compositor_xwm_window *w = compositor_xwm_find(id, 0);

        if (w)
                compositor_xwm_release(w);
}

/*
        The shim told what the window is: a window (or an override-redirect popup at its place) with its title, said again when the
        title changes, and asked again on each pass until the shim has the window's surface, whichever of the two (the serial from the
        X side, the surface from the Wayland side) comes first. The shim's answer is the window's row, once it has one.
*/
/*
        A window's mapping ended: its surface is gone or is another one. What belonged to that mapping goes with it -- the row the shim
        gave it, that the shim was told of it, that it was asked to close and that it was raised over its parent once -- so the next
        mapping starts as the first did. They were kept for the life of the X window, which outlives its mappings (a window hidden to
        a tray, a dialog a program keeps): the row was then another program's window, which a close, a _NET_WM_STATE message and the
        transient raise of the hidden window acted on, a window shown again under another row never had the focus or a close, and one
        that had been asked to close once was never asked again.
*/
static void compositor_xwm_unmapped(struct compositor_xwm_window *w)
{
        // The X focus it had goes with the mapping: the next window with the keys (a window of the same id, of a client after it) is given it.
        if (w->id && w->id == compositor_xwm_at->focus_window)
                compositor_xwm_at->focus_window = 0, compositor_xwm_at->focus_row = -3;
        w->row = -1;
        w->state &= ~(COMPOSITOR_XWM_ADOPTED | COMPOSITOR_XWM_RAISED);
        w->closes = 0;
        w->sent_x = w->sent_y = COMPOSITOR_XWM_NO_PLACE;
        w->sent_w = w->sent_h = 0;
        compositor_xwm_at->last_stack_gen = 0;
}

/*
        Whether the window is owed an ask to close: its row has been asked more times than the window was told. Each ask of the
        compositor's is told once (a WM_DELETE_WINDOW, or the unmap of a popup), so a program that answered the first with a
        question and stayed is asked by the next press of its button. It was one bit, set at the first and cleared only when the
        mapping ended. The WM_PROTOCOLS reply is waited for first: the ask is owed until then.
*/
static int compositor_xwm_close_owed(const struct compositor_xwm_window *w)
{
        return w->row >= 0 && w->row < COMPOSITOR_ROWS && (compositor.state[w->row] & WINDOW_CLOSING) && w->closes != compositor.close_asks[w->row] &&
               ((w->state & COMPOSITOR_XWM_POPUPISH) || (w->state & COMPOSITOR_XWM_PROTO_KNOWN));
}

/*
        The screen an X window covers exactly, at its place and its size (a game that maps borderless at the output's size, the common way
        a game goes fullscreen without asking for _NET_WM_STATE_FULLSCREEN), or -1. Recognises both managed windows and fullscreen
        override-redirect windows (Wine/Proton borderless fullscreen), excluding explicit popup types and transients.
*/
static int compositor_xwm_covers(const struct compositor_xwm_window *w)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int x = compositor_xwm_from_x(w->x, 0), y = compositor_xwm_from_x(w->y, 1);

        if ((w->state & COMPOSITOR_XWM_POPUPTYPE) || w->transient)
                return -1;
        for (unsigned int at = 0; at < count; at++)
                if (screens[at][0] == x && screens[at][1] == y && screens[at][2] == (int)w->width && screens[at][3] == (int)w->height)
                        return (int)at;
        return -1;
}

// Whether an X window is an unmanaged popup/menu/tooltip rather than a real toplevel. Fullscreen override-redirect
// windows (e.g. Wine/Proton borderless fullscreen games) are promoted to managed fullscreen toplevels so they hide
// the taskbar, appear in the taskbar, receive X11 input focus, and stack above Steam/desktop windows.
static int compositor_xwm_is_popup(const struct compositor_xwm_window *w)
{
        if (!(w->state & COMPOSITOR_XWM_POPUPISH))
                return 0;
        if ((w->state & COMPOSITOR_XWM_FULLSCREEN) || ((w->state & COMPOSITOR_XWM_OVERRIDE) && !w->transient && compositor_xwm_covers(w) >= 0))
                return 0;
        return 1;
}

// A window's class as its row's app_id, once it has both (the panel matches it to its app by it: ui_apps_match).
static void compositor_xwm_app(struct compositor_xwm_window *w)
{
        if (w->row >= 0 && w->row < COMPOSITOR_ROWS && w->app[0] && compositor.table &&
            windows_app_id_set(compositor.table, (unsigned int)w->row, w->app))
                compositor_take(w->row);
}

static void compositor_xwm_resolve(struct compositor_xwm_window *w)
{
        long row;
        int covers = -1;

        if (!(w->state & COMPOSITOR_XWM_SERIAL))
                return;
        // What kind of window it is waits for what the client says of it at the map (the shim waits for the word a moment).
        if (!(w->state & COMPOSITOR_XWM_OVERRIDE) && !(w->state & COMPOSITOR_XWM_PROPS_READ))
                return;
        if (!(w->state & COMPOSITOR_XWM_ADOPTED) || w->dirty)
        {
                // A window that hard-set its place is where X says; one transient for a window that shows is centred on it, and the
                // rest on the screen under the pointer (compositor_free_place).
                struct compositor_xwm_window *owner = w->transient ? compositor_xwm_find(w->transient, 0) : 0;
                long how = (compositor_xwm_is_popup(w) ? WAYLAND_XWINDOW_POPUP : 0) | (w->placed ? WAYLAND_XWINDOW_PLACED : 0) |
                           WAYLAND_XWINDOW_OWNER(owner && owner->row >= 0 && owner->row < COMPOSITOR_ROWS ? owner->row : -1);

                // A window mapped over a whole screen is fullscreen on it, said so (_NET_WM_STATE), as if it had asked.
                if (!(w->state & (COMPOSITOR_XWM_ADOPTED | COMPOSITOR_XWM_FULLSCREEN)) && (covers = compositor_xwm_covers(w)) >= 0)
                {
                        w->restore_w = w->width, w->restore_h = w->height;
                        w->state |= COMPOSITOR_XWM_FULLSCREEN | COMPOSITOR_XWM_COVERED;
                        compositor_xwm_net_state(w->id, (int)w->state);
                }
                row = wayland_xwindow_adopt(compositor_xwm_server(compositor_xwm_at), w->serial, how, compositor_xwm_from_x(w->x, 0),
                                            compositor_xwm_from_x(w->y, 1),
                                            w->title);
                if (row == -2)
                        return;
                w->state |= COMPOSITOR_XWM_ADOPTED;
                w->dirty = 0;
        }
        else if (w->row < 0)
                row = wayland_serial_row(compositor_xwm_server(compositor_xwm_at), w->serial);
        else
                return;
        if (row < 0 || w->row == (int)row)
                return;
        w->row = (int)row;
        compositor_xwm_at->last_stack_gen = 0;
        compositor_xwm_app(w);
        compositor_xwm_owned(w);
        if (w->state & COMPOSITOR_XWM_FULLSCREEN)
                compositor_fullscreen_on((int)row, covers >= 0 ? covers : compositor_xwm_covers(w));
        else if (w->state & COMPOSITOR_XWM_MAXIMIZED)
                compositor_arrange((int)row, COMPOSITOR_ROOM);
}

/* Fullscreen: the window's X size is the output's, at the origin, and the shim's row covers the screen. Let go: floats again. */
static void compositor_xwm_fullscreen(struct compositor_xwm_window *w, int on)
{
        struct xwm_configure c;

        w->state &= ~COMPOSITOR_XWM_COVERED; // asked for now, either way
        if (on == !!(w->state & COMPOSITOR_XWM_FULLSCREEN))
                return;
        if (on)
        {
                if (!(w->state & COMPOSITOR_XWM_MAXIMIZED))
                {
                        w->restore_w = w->width;
                        w->restore_h = w->height;
                }
                w->state |= COMPOSITOR_XWM_FULLSCREEN;
                if (w->row >= 0)
                        compositor_fullscreen_row(w->row);
                c.mask = XWM_CONFIGURE_X | XWM_CONFIGURE_Y | XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT;
                c.x = compositor_xwm_to_x(w->row >= 0 && compositor.showing[w->row] ? compositor.x[w->row] : 0, 0);
                c.y = compositor_xwm_to_x(w->row >= 0 && compositor.showing[w->row] ? compositor.y[w->row] : 0, 1);
                // The size the row is asked to be now (its screen's, set by compositor_fullscreen_row), not the picture's: compositor.w and
                // .h are the size it was drawn at until the client draws at the new one, so the X window was told its old size with
                // _NET_WM_STATE_FULLSCREEN, and stayed that small whenever no later sync of the row said the screen's (Black Mesa: a game
                // sure it was fullscreen, drawn in a little rectangle of the screen).
                {
                        b32 asked = w->row >= 0 && compositor.showing[w->row] && compositor.table && compositor.table->want_width[w->row] > 0 &&
                                    compositor.table->want_height[w->row] > 0;
                        b32 shown = w->row >= 0 && compositor.showing[w->row] && compositor.w[w->row] > 0 && compositor.h[w->row] > 0;

                        c.width = asked ? compositor.table->want_width[w->row] : shown ? compositor.w[w->row] : (compositor.width > 0 ? (unsigned int)compositor.width : 1);
                        c.height = asked ? compositor.table->want_height[w->row] : shown ? compositor.h[w->row] : (compositor.height > 0 ? (unsigned int)compositor.height : 1);
                }
                compositor_xwm_configure(w->id, &c);
                w->sent_x = c.x, w->sent_y = c.y, w->sent_w = c.width, w->sent_h = c.height;
                compositor_xwm_net_state(w->id, (int)w->state);
        }
        else
        {
                w->state &= ~COMPOSITOR_XWM_FULLSCREEN;
                compositor_xwm_net_state(w->id, (int)w->state);
                // Back to what it filled before (compositor_fullscreen_off); one made fullscreen before it had a row is maximized or floats.
                if (w->row >= 0 && compositor.fullscreen[w->row])
                        compositor_fullscreen_off(w->row);
                else if (w->row >= 0 && (w->state & COMPOSITOR_XWM_MAXIMIZED))
                        compositor_arrange(w->row, COMPOSITOR_ROOM);
                else if (w->row >= 0)
                        compositor_float(w->row);
                if (w->row >= 0 && compositor.arranged[w->row] != COMPOSITOR_FLOATING)
                        return;
                // The size it had is its own again: floating asks nothing of it.
                if (w->restore_w && w->restore_h)
                {
                        c.mask = XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT;
                        c.width = w->restore_w;
                        c.height = w->restore_h;
                        compositor_xwm_configure(w->id, &c);
                        w->sent_w = w->restore_w;
                        w->sent_h = w->restore_h;
                }
        }
}

/*
        What the compositor says of a window's place and size, sent to the X window when it changes: the place the window is at in the
        output (so that the root coordinates an X client reads are the output's), and the size the compositor asks of it (a window it
        has arranged, or is resizing) in the units of the screen's scale. A size is asked once; what the client then makes of it is its
        own, and a window the compositor asks nothing of keeps the size it has.
*/
static void compositor_xwm_clamp_size(const struct compositor_xwm_window *w, unsigned int *width, unsigned int *height)
{
        if (!w || (w->state & COMPOSITOR_XWM_FULLSCREEN))
                return;
        if (w->max_w && *width > w->max_w)
                *width = w->max_w;
        if (w->max_h && *height > w->max_h)
                *height = w->max_h;
        if (w->min_w && *width < w->min_w)
                *width = w->min_w;
        if (w->min_h && *height < w->min_h)
                *height = w->min_h;
}

static void compositor_xwm_sync(struct compositor_xwm_window *w)
{
        struct windows *table = compositor.table;
        struct xwm_configure c;
        int row = w->row;

        // Fullscreen made or ended by the compositor (Super+F11, a drag off, a chord) is said to the client in _NET_WM_STATE.
        if (!!(w->state & COMPOSITOR_XWM_FULLSCREEN) != !!compositor.fullscreen[row])
        {
                if (compositor.fullscreen[row])
                        w->restore_w = w->width, w->restore_h = w->height, w->state |= COMPOSITOR_XWM_FULLSCREEN;
                else
                        w->state &= ~(COMPOSITOR_XWM_FULLSCREEN | COMPOSITOR_XWM_COVERED);
                compositor_xwm_net_state(w->id, (int)w->state);
        }
        if (!(w->state & COMPOSITOR_XWM_FULLSCREEN))
        {
                int is_max = compositor.arranged[row] == COMPOSITOR_ROOM || compositor.arranged[row] == COMPOSITOR_MAXIMIZED;
                if (is_max != !!(w->state & COMPOSITOR_XWM_MAXIMIZED))
                {
                        if (is_max)
                                w->state |= COMPOSITOR_XWM_MAXIMIZED;
                        else
                                w->state &= ~COMPOSITOR_XWM_MAXIMIZED;
                        compositor_xwm_net_state(w->id, (int)w->state);
                }
        }
        c.mask = 0;
        c.x = compositor_xwm_to_x(compositor.x[row], 0);
        c.y = compositor_xwm_to_x(compositor.y[row], 1);
        if (c.x != w->sent_x || c.y != w->sent_y || c.x != w->x || c.y != w->y)
                c.mask |= XWM_CONFIGURE_X | XWM_CONFIGURE_Y;
        if (table && (compositor.arranged[row] != COMPOSITOR_FLOATING || compositor.asking[row]))
        {
                long width = (long)table->want_width[row], height = (long)table->want_height[row];
                if (width > 0 && height > 0)
                {
                        unsigned int cw = (unsigned int)width, ch = (unsigned int)height;

                        compositor_xwm_clamp_size(w, &cw, &ch);
                        if ((cw != w->sent_w || ch != w->sent_h) &&
                            (compositor.resize != row || w->sync_present != compositor_metric_presents))
                        {
                                c.mask |= XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT;
                                c.width = cw;
                                c.height = ch;
                                w->sync_present = compositor_metric_presents;
                        }
                }
        }
        if (!c.mask)
                return;
        compositor_xwm_configure(w->id, &c);
        if (c.mask & XWM_CONFIGURE_X)
        {
                // ICCCM 4.1.5: deliver synthetic ConfigureNotify on position-only sync so Wine/Proton x11drv updates its cached
                // window origin; when width/height also changed, XWayland emits a real ConfigureNotify with the new size.
                if (!(c.mask & (XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT)) && w->width > 0 && w->height > 0)
                {
                        unsigned char ev[XWM_EVENT_SIZE], req[64];
                        long n = xwm_enc_configure_notify(ev, sizeof ev, w->id, c.x, c.y, w->width, w->height, 0, (w->state & COMPOSITOR_XWM_OVERRIDE) != 0);

                        if (n > 0 && (n = xwm_enc_send_event(req, sizeof req, 0, w->id, XWM_MASK_STRUCTURE_NOTIFY, ev, (unsigned long)n)) > 0)
                                compositor_xwm_queue(req, n);
                }
                w->sent_x = c.x;
                w->sent_y = c.y;
                w->x = c.x;
                w->y = c.y;
        }
        if (c.mask & XWM_CONFIGURE_WIDTH)
        {
                w->sent_w = c.width;
                w->sent_h = c.height;
                w->width = c.width;
                w->height = c.height;
        }
}

/* EWMH _NET_WM_MOVERESIZE: an X client asks to move or resize its window while the person holds the left button on it. */
static void compositor_xwm_moveresize(struct compositor_xwm_window *w, unsigned int direction)
{
        static const unsigned int edge_of[8] = {
            COMPOSITOR_EDGE_TOP | COMPOSITOR_EDGE_LEFT,    COMPOSITOR_EDGE_TOP,
            COMPOSITOR_EDGE_TOP | COMPOSITOR_EDGE_RIGHT,   COMPOSITOR_EDGE_RIGHT,
            COMPOSITOR_EDGE_BOTTOM | COMPOSITOR_EDGE_RIGHT, COMPOSITOR_EDGE_BOTTOM,
            COMPOSITOR_EDGE_BOTTOM | COMPOSITOR_EDGE_LEFT,  COMPOSITOR_EDGE_LEFT};
        int row;

        if (!w || w->row < 0 || w->row >= COMPOSITOR_ROWS || !compositor.showing[w->row])
                return;
        row = w->row;
        if (direction == COMPOSITOR_XWM_MOVERESIZE_CANCEL)
        {
                compositor_drag_stop(row);
                return;
        }
        // Only while the person holds the left button on this window: a client cannot take the pointer.
        if (!(compositor.button_down & 1u) || compositor.button_row[0] != row || compositor.drag >= 0 || compositor.resize >= 0)
                return;
        if (direction == COMPOSITOR_XWM_MOVERESIZE_MOVE)
                compositor_drag_begin_client(row);
        else if (direction < 8)
        {
                compositor_resize_begin(row, edge_of[direction]);
                compositor_lit(row, edge_of[direction], 1);
        }
}

/* ---- the selections (THE SELECTIONS) ---- */

#define COMPOSITOR_XWM_TRANSFER_NS 5000000000ull     // a transfer that moves nothing for this long is let go
#define COMPOSITOR_XWM_PIPE 262144                   // the size asked for a pipe of the window manager's (F_SETPIPE_SZ)
#define COMPOSITOR_XWM_PIPE_SLACK 8192               // what a pipe's room is held back by: the kernel lays a write in whole pages
#define COMPOSITOR_XWM_PIECE_UNITS (XWM_PROPERTY_MAX / XWM_UNIT) // the most a GetProperty of a value asks for, in units
#define COMPOSITOR_XWM_IN_TARGETS 1
#define COMPOSITOR_XWM_IN_ASKED 2
#define COMPOSITOR_XWM_IN_PIECES 3
#define COMPOSITOR_XWM_IN_NAMING 4
#define COMPOSITOR_XWM_OUT_START 0
#define COMPOSITOR_XWM_OUT_WAIT 1
#define COMPOSITOR_XWM_OUT_NEXT 2
#define COMPOSITOR_XWM_OUT_MULTIPLE 3        // a MULTIPLE whose pairs are being read
#define COMPOSITOR_XWM_FROM_FILE 1
#define COMPOSITOR_XWM_FROM_PIPE 2
#define COMPOSITOR_XWM_FIONREAD 0x541b
#define COMPOSITOR_XWM_F_SETFL 4
#define COMPOSITOR_XWM_F_SETPIPE_SZ 1031
#define COMPOSITOR_XWM_F_GETPIPE_SZ 1032
#define COMPOSITOR_XWM_F_ADD_SEALS 1033
#define COMPOSITOR_XWM_O_NONBLOCK 0x800
#define COMPOSITOR_XWM_O_CLOEXEC 0x80000

static long compositor_wl_pump(int in, int out);
static void compositor_wl_pump_nudge(long at);

static unsigned int compositor_xwm_id(unsigned int number)
{
        return compositor_xwm_at->resource_base | (number & compositor_xwm_at->resource_mask);
}

// Whether two X ids are of one client: the bits past the resource mask are the client's.
static int compositor_xwm_same_client(unsigned int a, unsigned int b)
{
        return a && b && (a & ~compositor_xwm_at->resource_mask) == (b & ~compositor_xwm_at->resource_mask);
}

static int compositor_xwm_same(const unsigned char *a, const unsigned char *b, unsigned long n)
{
        for (unsigned long at = 0; at < n; at++)
                if (a[at] != b[at])
                        return 0;
        return 1;
}

/* ---- the atoms and their names: a table that grows, the names in one that grows beside it ---- */

/*
        The atoms indexed by their numbers (open addressing, a power of two of buckets, each a record's slot and one), as the windows are: a server
        names atoms in lists (a selection's TARGETS, a drag's types), and a walk of the whole table for each was the square of the list on the loop.
        A record's atom is put in once it is known (at its making, or at its InternAtom's reply) and is never taken out: the table only grows, and
        is let go of with the connection. At half full the index is made again at twice the buckets.
*/
static void compositor_xwm_atom_put(struct wayland_table *index, long slot)
{
        unsigned int *bucket = (unsigned int *)index->at;
        unsigned long b = compositor_xwm_hash(compositor_xwm_at->atom[slot].atom, index->count);

        while (bucket[b])
                b = (b + 1) & (unsigned long)(index->count - 1);
        bucket[b] = (unsigned int)(slot + 1);
}

// The record at slot, its atom now known, put in the index (made again when half full); without the memory it is left out and found by the walk.
static void compositor_xwm_atom_indexed(long slot)
{
        struct compositor_xwm *x = compositor_xwm_at;

        if (!x->atom[slot].atom)
                return;
        if (2 * (x->atoms_indexed + 1) > x->atom_index.count)
        {
                struct wayland_table grown = {0, sizeof(unsigned int), 0, 0, 0, 0, 0, 0, 0, 0};

                wayland_tables_host(&compositor_wl_host);
                if (!wayland_table_reach(&grown, x->atom_index.count ? 2 * x->atom_index.count : 64))
                {
                        x->atoms_indexed = -1; // no index: every look is the walk (compositor_xwm_atom_index)
                        wayland_table_drop(&x->atom_index);
                        return;
                }
                for (long at = 0; at < x->atom_table.count; at++)
                        if (at != slot && x->atom[at].atom)
                                compositor_xwm_atom_put(&grown, at);
                wayland_table_drop(&x->atom_index);
                x->atom_index = grown;
        }
        compositor_xwm_atom_put(&x->atom_index, slot);
        x->atoms_indexed++;
}

static long compositor_xwm_atom_add(unsigned int atom, const unsigned char *name, unsigned long length, int known)
{
        struct compositor_xwm *x = compositor_xwm_at;
        long at;

        wayland_tables_host(&compositor_wl_host);
        x->name_table.unit = 1;
        if (length && !wayland_table_reach(&x->name_table, x->name_bytes + (long)length))
                return -1;
        x->atom_table.unit = sizeof(struct compositor_xwm_atom);
        at = wayland_table_take(&x->atom_table);
        if (at < 0)
                return -1;
        x->atom = (struct compositor_xwm_atom *)x->atom_table.at;
        x->atom[at].atom = atom;
        x->atom[at].at = (unsigned int)x->name_bytes;
        x->atom[at].length = (unsigned int)length;
        x->atom[at].known = (unsigned char)known;
        if (length)
                memory_copy(x->name_table.at + x->name_bytes, name, (positive)length);
        x->name_bytes += (long)length;
        if (x->atoms_indexed >= 0)
                compositor_xwm_atom_indexed(at);
        return at;
}

// The record of an atom, or -1: the first record that has it (an atom interned by name may also be one named by a list before), by the index's
// chain, which holds every record with it; by the walk where the index could not be made.
static long compositor_xwm_atom_index(unsigned int atom)
{
        struct compositor_xwm *x = compositor_xwm_at;
        long buckets = x->atom_index.count, found = -1;
        const unsigned int *bucket = (const unsigned int *)x->atom_index.at;

        if (!atom)
                return -1;
        if (x->atoms_indexed < 0)
        {
                for (long at = 0; at < x->atom_table.count; at++)
                        if (x->atom[at].atom == atom)
                                return at;
                return -1;
        }
        for (unsigned long b = buckets ? compositor_xwm_hash(atom, buckets) : 0, n = 0; buckets && n < (unsigned long)buckets && bucket[b];
             n++, b = (b + 1) & (unsigned long)(buckets - 1))
                if (x->atom[bucket[b] - 1].atom == atom && (found < 0 || (long)bucket[b] - 1 < found))
                        found = (long)bucket[b] - 1;
        return found;
}

static long compositor_xwm_atom_named(const unsigned char *name, unsigned long length)
{
        struct compositor_xwm *x = compositor_xwm_at;

        for (long at = 0; length && at < x->atom_table.count; at++)
                if (x->atom[at].length == length && compositor_xwm_same(x->name_table.at + x->atom[at].at, name, length))
                        return at;
        return -1;
}

// An atom's name, or 0 while it is not known.
static const unsigned char *compositor_xwm_atom_name(unsigned int atom, unsigned long *length)
{
        long at = compositor_xwm_atom_index(atom);

        if (at < 0 || !compositor_xwm_at->atom[at].known)
                return 0;
        *length = compositor_xwm_at->atom[at].length;
        return compositor_xwm_at->name_table.at + compositor_xwm_at->atom[at].at;
}

// A name's atom when it is known; else it is asked of the server (InternAtom), and 0 until the reply.
static unsigned int compositor_xwm_intern_name(const unsigned char *name, unsigned long length)
{
        unsigned char req[XWM_REQUEST_SHORT + XWM_ATOM_NAME_MAX];
        long at = compositor_xwm_atom_named(name, length);

        if (at >= 0)
                return compositor_xwm_at->atom[at].known ? compositor_xwm_at->atom[at].atom : 0;
        if (!length || length > XWM_ATOM_NAME_MAX || (at = compositor_xwm_atom_add(0, name, length, 0)) < 0)
                return 0;
        compositor_xwm_wait(req, xwm_enc_intern_atom(req, sizeof req, 0, name, length), 0, COMPOSITOR_XWM_PROP_INTERN, (unsigned int)at);
        return 0;
}

// An atom's name asked of the server (GetAtomName) when it is not known: the reply comes before any event that follows.
static void compositor_xwm_name_atom(unsigned int atom)
{
        unsigned char req[XWM_REQUEST_SHORT];
        long at;

        if (!atom || compositor_xwm_atom_index(atom) >= 0 || (at = compositor_xwm_atom_add(atom, 0, 0, 0)) < 0)
                return;
        compositor_xwm_wait(req, xwm_enc_get_atom_name(req, sizeof req, atom), 0, COMPOSITOR_XWM_PROP_ATOM_NAME, (unsigned int)at);
}

static void compositor_xwm_interned(const unsigned char *in, long size, const struct compositor_xwm_pending *p)
{
        struct compositor_xwm *x = compositor_xwm_at;
        struct xwm_atom_reply r;

        if (xwm_parse_atom_reply(in, (unsigned long)size, &r) != XWM_OK)
        {
                x->broken = 1;
                return;
        }
        if ((long)p->slot < x->atom_table.count && r.atom)
        {
                b32 had = x->atom[p->slot].atom != 0;

                x->atom[p->slot].atom = r.atom;
                x->atom[p->slot].known = 1;
                if (!had && x->atoms_indexed >= 0)
                        compositor_xwm_atom_indexed((long)p->slot);
        }
}

static void compositor_xwm_named(const unsigned char *in, long size, const struct compositor_xwm_pending *p)
{
        struct compositor_xwm *x = compositor_xwm_at;
        struct xwm_atom_name_reply r;

        if (xwm_parse_atom_name_reply(in, (unsigned long)size, &r) != XWM_OK)
        {
                x->broken = 1;
                return;
        }
        if ((long)p->slot >= x->atom_table.count)
                return;
        x->name_table.unit = 1;
        if (r.name_len && wayland_table_reach(&x->name_table, x->name_bytes + (long)r.name_len))
        {
                memory_copy(x->name_table.at + x->name_bytes, r.name, (positive)r.name_len);
                x->atom[p->slot].at = (unsigned int)x->name_bytes;
                x->atom[p->slot].length = (unsigned int)r.name_len;
                x->name_bytes += (long)r.name_len;
        }
        x->atom[p->slot].known = 1;
}

// The atoms the window manager interned at its start, known by name from the first: and the predefined ones a selection names.
static void compositor_xwm_atoms_seed(void)
{
        static const struct
        {
                unsigned int atom;
                const char *name;
        } predefined[] = {{COMPOSITOR_XWM_XA_PRIMARY, "PRIMARY"}, {COMPOSITOR_XWM_ATOM_TYPE, "ATOM"}, {COMPOSITOR_XWM_XA_INTEGER, "INTEGER"},
                          {COMPOSITOR_XWM_XA_STRING, "STRING"}};
        struct compositor_xwm *x = compositor_xwm_at;

        for (unsigned int at = 0; at < COMPOSITOR_XWM_ATOMS + sizeof predefined / sizeof predefined[0]; at++)
        {
                const char *name = at < COMPOSITOR_XWM_ATOMS ? compositor_xwm_names[at] : predefined[at - COMPOSITOR_XWM_ATOMS].name;
                unsigned int atom = at < COMPOSITOR_XWM_ATOMS ? x->atoms[at] : predefined[at - COMPOSITOR_XWM_ATOMS].atom;
                unsigned long length = 0;

                while (name[length])
                        length++;
                if (atom && compositor_xwm_atom_index(atom) < 0)
                        compositor_xwm_atom_add(atom, (const unsigned char *)name, length, 1);
        }
}

/* ---- the selections' requests ---- */

static int compositor_xwm_which(unsigned int selection)
{
        for (int which = 0; selection && which < 3; which++)
                if (compositor_xwm_at->sel[which].atom == selection)
                        return which;
        return -1;
}

static void compositor_xwm_own(int which, unsigned int owner)
{
        unsigned char req[16];
        long n = xwm_enc_set_selection_owner(req, sizeof req, owner, compositor_xwm_at->sel[which].atom, XWM_CURRENT_TIME);

        if (n > 0)
                compositor_xwm_queue(req, n);
        compositor_xwm_at->sel[which].owned = owner != 0;
}

// The owner's answer to a requestor: the property the value is in, or 0 for a refusal.
static void compositor_xwm_notify(unsigned int time, unsigned int requestor, unsigned int selection, unsigned int target, unsigned int property)
{
        unsigned char event[XWM_EVENT_SIZE], req[64];
        long n;

        if (xwm_enc_selection_notify(event, sizeof event, time, requestor, selection, target, property) != XWM_EVENT_SIZE)
                return;
        n = xwm_enc_send_event(req, sizeof req, 0, requestor, 0, event, XWM_EVENT_SIZE);
        if (n > 0)
                compositor_xwm_queue(req, n);
}

static unsigned char compositor_xwm_request_bytes[24 + XWM_PROPERTY_MAX];

static void compositor_xwm_put(unsigned int window, unsigned int property, unsigned int type, unsigned int format, const unsigned char *data, unsigned long length)
{
        long n = xwm_enc_change_property(compositor_xwm_request_bytes, sizeof compositor_xwm_request_bytes, XWM_PROP_REPLACE, window, property, type,
                                         format, data, length);

        if (n > 0)
                compositor_xwm_queue(compositor_xwm_request_bytes, n);
}

static void compositor_xwm_select(unsigned int window, unsigned int mask)
{
        unsigned char req[16];
        long n = xwm_enc_select_events(req, sizeof req, window, mask);

        if (n > 0)
                compositor_xwm_queue(req, n);
}

// A pipe, close-on-exec, its end at ours (0 or 1) not waiting, as large as COMPOSITOR_XWM_PIPE where the kernel allows: 0, or -1.
static int compositor_xwm_pipe(int fds[2], int ours)
{
        if (compositor_call(syscall(pipe2), (positive)fds, COMPOSITOR_XWM_O_CLOEXEC, 0, 0, 0, 0) < 0)
                return -1;
        compositor_call(syscall(fcntl), (positive)fds[ours], COMPOSITOR_XWM_F_SETFL, COMPOSITOR_XWM_O_NONBLOCK, 0, 0, 0);
        compositor_call(syscall(fcntl), (positive)fds[0], COMPOSITOR_XWM_F_SETPIPE_SZ, COMPOSITOR_XWM_PIPE, 0, 0, 0);
        return 0;
}

// What can be written to a pipe now without waiting: its size less what is in it, held back by the slack.
static long compositor_xwm_pipe_room(int fd)
{
        int used = 0;
        long size = compositor_call(syscall(fcntl), (positive)fd, COMPOSITOR_XWM_F_GETPIPE_SZ, 0, 0, 0, 0);

        if (size <= 0 || compositor_call(syscall(ioctl), (positive)fd, COMPOSITOR_XWM_FIONREAD, (positive)&used, 0, 0, 0) < 0)
                return 0;
        return size - used - COMPOSITOR_XWM_PIPE_SLACK > 0 ? size - used - COMPOSITOR_XWM_PIPE_SLACK : 0;
}

// Whether a type is in a list of types (each with its null).
static int compositor_xwm_listed(const unsigned char *list, long bytes, const unsigned char *name, unsigned long length)
{
        for (long at = 0; list && at < bytes;)
        {
                long n = 0;

                while (at + n < bytes && list[at + n])
                        n++;
                if ((unsigned long)n == length && compositor_xwm_same(list + at, name, length))
                        return 1;
                at += n + 1;
        }
        return 0;
}

/* ---- the person's rule: who may copy and paste ---- */

/* The row of an X client's window the person has just touched (a key or a button within COMPOSITOR_CLIP_GESTURE_NS), the one with
   the keys first: the owner of a copy that client makes. -1 when there is none, so an X client copies only under the person's hand. */
static int compositor_xwm_copy_row(unsigned int owner)
{
        struct compositor_xwm *x = compositor_xwm_at;
        positive now = clock_monotonic_nanoseconds();
        int best = -1;

        if (!compositor.table || !compositor.table->clip_on)
                return -1;
        for (long at = 0; at < x->window_table.count; at++)
        {
                struct compositor_xwm_window *w = &x->window[at];

                if (w->id && w->row >= 0 && w->row < COMPOSITOR_ROWS && compositor_xwm_same_client(w->id, owner) &&
                    now - compositor_clip.gesture[w->row] <= COMPOSITOR_CLIP_GESTURE_NS && (best < 0 || w->row == (int)compositor.focus))
                        best = w->row;
        }
        return best;
}

/* Whether an X client may be given the compositor's text: a window of its has the keys (or is a popup of the one that has) and has just
   had a key or a button of the person's, as a window's paste in the compositor (compositor_clip_door). */
static int compositor_xwm_may_paste(unsigned int requestor)
{
        struct compositor_xwm *x = compositor_xwm_at;
        positive now = clock_monotonic_nanoseconds();

        if (!compositor.table || !compositor.table->clip_on || compositor.focus < 0)
                return 0;
        for (long at = 0; at < x->window_table.count; at++)
        {
                struct compositor_xwm_window *w = &x->window[at];

                if (w->id && w->row >= 0 && w->row < COMPOSITOR_ROWS && compositor_xwm_same_client(w->id, requestor) &&
                    compositor_clip_root(w->row) == (int)compositor.focus && now - compositor_clip.gesture[w->row] <= COMPOSITOR_CLIP_GESTURE_NS)
                        return 1;
        }
        return 0;
}

/* ---- the conversions the window manager asks for ---- */

static struct compositor_xwm_in *compositor_xwm_in_at(long at)
{
        return at >= 0 && at < compositor_xwm_at->in_table.count && compositor_xwm_at->ins[at].window ? &compositor_xwm_at->ins[at] : 0;
}

// A conversion begun: its requestor window (its PropertyChange selected), and the selection asked for as target into its property.
static long compositor_xwm_in_new(int which, unsigned int target, unsigned int time, int eager, int fd, int row)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned char req[48];
        unsigned int values[1] = {XWM_MASK_PROPERTY_CHANGE};
        struct compositor_xwm_in *in;
        long at, n;

        wayland_tables_host(&compositor_wl_host);
        x->in_table.unit = sizeof(struct compositor_xwm_in);
        at = wayland_table_take(&x->in_table);
        if (at < 0)
                return -1;
        x->ins = (struct compositor_xwm_in *)x->in_table.at;
        in = &x->ins[at];
        memory_zero(in, sizeof *in);
        if (x->next_requestor < COMPOSITOR_XWM_FIRST_REQUESTOR || x->next_requestor > x->resource_mask)
                x->next_requestor = COMPOSITOR_XWM_FIRST_REQUESTOR;
        in->window = compositor_xwm_id(x->next_requestor++);
        in->which = (unsigned char)which;
        in->stage = target == x->atoms[COMPOSITOR_XWM_A_TARGETS] ? COMPOSITOR_XWM_IN_TARGETS : COMPOSITOR_XWM_IN_ASKED;
        in->eager = (unsigned char)eager;
        in->latin1 = target == COMPOSITOR_XWM_XA_STRING;
        in->selection = x->sel[which].atom;
        in->target = target;
        in->time = time;
        in->fd = fd;
        in->row = row;
        in->pump = -1;
        in->index = -1;
        in->next = -1;
        in->deadline = clock_monotonic_nanoseconds() + COMPOSITOR_XWM_TRANSFER_NS;
        in->targets.unit = sizeof(unsigned int);
        n = xwm_enc_create_window(req, sizeof req, 0, in->window, x->root, -1, -1, 1, 1, XWM_CLASS_INPUT_ONLY, 0);
        if (n > 0)
                compositor_xwm_queue(req, n);
        n = xwm_enc_change_window_attributes(req, sizeof req, in->window, XWM_CW_EVENT_MASK, values, 1);
        if (n > 0)
                compositor_xwm_queue(req, n);
        n = xwm_enc_convert_selection(req, sizeof req, in->window, in->selection, target, x->atoms[COMPOSITOR_XWM_A_SELECTION], time);
        if (n > 0)
                compositor_xwm_queue(req, n);
        return at;
}

static void compositor_xwm_saved(unsigned int which, int ok);

/* An X copy read to its end: every type it gave, the copy whole, checked and kept as any copy (owned by the window touched), so it
   outlives its X owner; one that gave no type leaves the selection as it was. */
static void compositor_xwm_copied(struct compositor_xwm_in *in)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned int which = in->which;
        unsigned long length = 0;
        int kept = -1;

        if (!compositor.table || which > 1 || !in->count || in->index < 0 || !windows_clip_seal(in->fd, in->index, in->total, in->count))
                return;
        kept = compositor_clip_open(compositor.table->clip_pid, in->fd, &length);
        if (kept < 0)
                return;
        x->setting = which + 1;
        compositor_clip_keep(which, kept, length, in->row);
        x->setting = 0;
        compositor_xwm_saved(which, 1);
}

// A conversion let go of, whatever it was in the middle of: an X copy's SAVE_TARGETS, if it was waited for, is answered None.
static void compositor_xwm_in_free(long at)
{
        struct compositor_xwm *x = compositor_xwm_at;
        struct compositor_xwm_in *in = compositor_xwm_in_at(at);

        if (!in)
                return;
        if (in->eager)
                compositor_xwm_saved(in->which, 0);
        if (in->fd >= 0)
                system_close(in->fd);
        if (in->index >= 0)
                system_close(in->index);
        compositor_xwm_window_op(XWM_OP_DESTROY_WINDOW, in->window);
        wayland_table_drop(&in->targets);
        for (int which = 0; which < 3; which++)
                if (x->sel[which].reading == at)
                        x->sel[which].reading = -1;
        in->window = 0;
        wayland_table_give(&x->in_table, at);
}

/* An eager copy's next target, after its text: each of its TARGETS that is a Wayland type (a name with a slash) and is no name text
   goes by, converted into the copy's memfd after the types before it. None left: the copy is made whole (compositor_xwm_copied). */
static void compositor_xwm_in_next(long at)
{
        struct compositor_xwm *x = compositor_xwm_at;
        struct compositor_xwm_in *in = compositor_xwm_in_at(at);
        const unsigned int *atoms = (const unsigned int *)in->targets.at;
        unsigned int target = 0;
        unsigned char req[32];
        long n;

        if (in->next < 0)
        {
                in->next = 0;
                target = in->text;
        }
        while (!target && atoms && in->next < in->targets.count)
        {
                unsigned long length = 0;
                unsigned int atom = atoms[in->next++];
                const unsigned char *name = compositor_xwm_atom_name(atom, &length);

                if (name && atom != in->text && xwm_target_kind(name, length) == XWM_TARGET_MIME && !windows_clip_alias((const char *)name, length))
                        target = atom;
        }
        if (!target)
        {
                compositor_xwm_copied(in);
                compositor_xwm_in_free(at);
                return;
        }
        in->target = target;
        in->latin1 = target == COMPOSITOR_XWM_XA_STRING;
        in->stage = COMPOSITOR_XWM_IN_ASKED;
        in->incr = 0, in->offset = 0, in->done = 0;
        in->deadline = clock_monotonic_nanoseconds() + COMPOSITOR_XWM_TRANSFER_NS;
        n = xwm_enc_convert_selection(req, sizeof req, in->window, in->selection, target, x->atoms[COMPOSITOR_XWM_A_SELECTION], in->time);
        if (n > 0)
                compositor_xwm_queue(req, n);
}

/* A conversion ended, well (ok) or not. A pipe's (a Wayland client's receive) is let go; an eager copy's type is kept in its index when
   it came whole (and, for its text, is UTF-8 with no NUL and not empty), else its bytes are cut off again, and the next is asked for. */
static void compositor_xwm_in_end(long at, int ok)
{
        struct compositor_xwm_in *in = compositor_xwm_in_at(at);
        unsigned long length = 0;
        const unsigned char *name;

        if (!in)
                return;
        if (!in->eager || in->stage == COMPOSITOR_XWM_IN_TARGETS)
        {
                compositor_xwm_in_free(at);
                return;
        }
        if (in->stage == COMPOSITOR_XWM_IN_NAMING)
        {
                // TARGETS read and named: the copy's types, its text first.
                in->index = ok ? (int)compositor_call(syscall(memfd_create), (positive) "moonwater-x-index", 1, 0, 0, 0, 0) : -1;
                if (in->index < 0)
                        compositor_xwm_in_free(at);
                else
                        compositor_xwm_in_next(at);
                return;
        }
        name = in->target == in->text ? (const unsigned char *)WINDOWS_CLIP_TEXT : compositor_xwm_atom_name(in->target, &length);
        length = in->target == in->text ? sizeof(WINDOWS_CLIP_TEXT) - 1 : length;
        if (ok && in->target == in->text)
        {
                long mapped = in->done ? compositor_call(syscall(mmap), 0, (positive)(in->total + in->done), 1, 1, (positive)in->fd, 0) : -22;

                ok = !(mapped < 0 && mapped > -4096) && windows_clip_text((const unsigned char *)mapped + in->total, in->done);
                if (!(mapped < 0 && mapped > -4096))
                        compositor_call(syscall(munmap), (positive)mapped, (positive)(in->total + in->done), 0, 0, 0, 0);
        }
        if (ok && name && windows_clip_record(in->index, in->total, in->done, (const char *)name, length))
                in->total += in->done, in->count++;
        else
        {
                compositor_call(syscall(ftruncate), (positive)in->fd, (positive)in->total, 0, 0, 0, 0);
                compositor_call(syscall(lseek), (positive)in->fd, (positive)in->total, 0, 0, 0, 0);
        }
        compositor_xwm_in_next(at);
}

/* The next part of the value asked for (GetProperty, deleting it once the last of it is read): as much as a reply holds, and for a pipe
   as much as the pipe has room for now (a value made UTF-8 from Latin-1 counted twice). None while there is no room: ready says a
   piece waits, and the pump is asked to knock when it has made some. */
static void compositor_xwm_in_ask(long at)
{
        struct compositor_xwm *x = compositor_xwm_at;
        struct compositor_xwm_in *in = compositor_xwm_in_at(at);
        unsigned char req[32];
        long units = COMPOSITOR_XWM_PIECE_UNITS;

        if (!in || in->asking)
                return;
        if (!in->eager)
        {
                long room = compositor_xwm_pipe_room(in->fd);

                // No room: the pump is asked to knock once it has made some, and the room looked at again (it may have made it meanwhile).
                if ((in->latin1 ? room / 2 : room) / XWM_UNIT < 1024 && in->pump >= 0)
                {
                        compositor_wl_pump_nudge(in->pump);
                        room = compositor_xwm_pipe_room(in->fd);
                }
                units = (in->latin1 ? room / 2 : room) / XWM_UNIT;
                units = units > COMPOSITOR_XWM_PIECE_UNITS ? COMPOSITOR_XWM_PIECE_UNITS : units;
                if (units < 1024)
                {
                        in->ready = 1;
                        return;
                }
        }
        in->ready = 0;
        if (compositor_xwm_wait(req, xwm_enc_get_property(req, sizeof req, 1, in->window, x->atoms[COMPOSITOR_XWM_A_SELECTION], 0, (unsigned int)in->offset,
                                                          (unsigned int)units),
                                in->window, COMPOSITOR_XWM_PROP_SEL, (unsigned int)at) == 0)
                in->asking = 1;
}

// Bytes of a value to where they go (STRING made UTF-8 on the way): 1, or 0 when they could not all go (the limit, or the reader gone).
static int compositor_xwm_in_write(struct compositor_xwm_in *in, const unsigned char *bytes, unsigned long count)
{
        unsigned char wide[8192];

        for (unsigned long at = 0; at < count;)
        {
                unsigned long take = in->latin1 ? (count - at > 4096 ? 4096 : count - at) : count - at, put = take;
                const unsigned char *from = bytes + at;
                long wrote = 0;

                if (in->latin1)
                {
                        put = xwm_latin1_utf8(bytes + at, take, wide);
                        from = wide;
                }
                for (unsigned long done = 0; done < put; done += (unsigned long)wrote)
                {
                        wrote = compositor_call(syscall(write), (positive)in->fd, (positive)(from + done), (positive)(put - done), 0, 0, 0);
                        if (wrote <= 0)
                                return 0;
                }
                in->done += put;
                at += take;
        }
        in->deadline = clock_monotonic_nanoseconds() + COMPOSITOR_XWM_TRANSFER_NS;
        return 1;
}

// The best text of an X copy's TARGETS: UTF8_STRING, the UTF-8 types, STRING (made UTF-8), then TEXT.
static unsigned int compositor_xwm_best_text(const unsigned int *atoms, long count)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned int order[5] = {x->atoms[COMPOSITOR_XWM_A_UTF8], x->atoms[COMPOSITOR_XWM_A_MIME_UTF8], x->atoms[COMPOSITOR_XWM_A_MIME_TEXT],
                                 COMPOSITOR_XWM_XA_STRING, x->atoms[COMPOSITOR_XWM_A_TEXT]};

        for (int rank = 0; rank < 5; rank++)
                for (long k = 0; order[rank] && k < count; k++)
                        if (atoms[k] == order[rank])
                                return order[rank];
        return 0;
}

/* A reply to a conversion's GetProperty: TARGETS listed (their names asked for), the INCR that starts a value in pieces, a part of the
   value, or the end: a value whole, a part past which nothing is left, or INCR's empty piece. */
static void compositor_xwm_in_reply(const unsigned char *msg, long size, const struct compositor_xwm_pending *p)
{
        struct compositor_xwm *x = compositor_xwm_at;
        long at = (long)p->slot;
        struct compositor_xwm_in *in = compositor_xwm_in_at(at);
        struct xwm_property_reply r;
        unsigned long incr = 0;
        int first;

        if (!in || in->window != p->window)
                return;
        in->asking = 0;
        if (xwm_parse_property_reply(msg, (unsigned long)size, &r) != XWM_OK)
        {
                x->broken = 1;
                return;
        }
        if (p->what == COMPOSITOR_XWM_PROP_SEL_SYNC)
        {
                compositor_xwm_in_end(at, 1);
                return;
        }
        if (in->stage == COMPOSITOR_XWM_IN_TARGETS)
        {
                unsigned char req[32];

                wayland_tables_host(&compositor_wl_host);
                for (unsigned long k = 0; r.format == XWM_FORMAT_32 && k + 4 <= r.value_len; k += 4)
                {
                        unsigned int atom = xwm_get32(r.value + k);

                        // The first COMPOSITOR_XWM_TARGETS_MOST (no toolkit offers more): each is named of the server, so a list of thousands filled
                        // the queue to the server and broke the connection.
                        if (in->targets.count >= COMPOSITOR_XWM_TARGETS_MOST)
                                break;
                        if (!atom || !wayland_table_reach(&in->targets, in->targets.count + 1))
                                continue;
                        ((unsigned int *)in->targets.at)[in->targets.count - 1] = atom;
                        compositor_xwm_name_atom(atom);
                }
                in->text = compositor_xwm_best_text((const unsigned int *)in->targets.at, in->targets.count);
                in->offset = 0;
                // The names asked for are waited for (a reply after them), then each type is read in turn, the text first.
                in->stage = COMPOSITOR_XWM_IN_NAMING;
                if (compositor_xwm_wait(req, xwm_enc_get_property(req, sizeof req, 0, in->window, x->atoms[COMPOSITOR_XWM_A_SELECTION], 0, 0, 0), in->window,
                                        COMPOSITOR_XWM_PROP_SEL_SYNC, (unsigned int)at) != 0)
                        compositor_xwm_in_end(at, 0);
                return;
        }
        first = in->offset == 0;
        if (in->stage == COMPOSITOR_XWM_IN_ASKED && first && xwm_property_incr(&r, x->atoms[COMPOSITOR_XWM_A_INCR], &incr))
        {
                // INCR: the delete told the owner to write its first piece, which comes as the property's new value.
                in->incr = 1;
                in->stage = COMPOSITOR_XWM_IN_PIECES;
                in->deadline = clock_monotonic_nanoseconds() + COMPOSITOR_XWM_TRANSFER_NS;
                return;
        }
        if (in->incr && first && !r.value_len && !r.bytes_after)
        {
                compositor_xwm_in_end(at, 1);
                return;
        }
        if (r.value_len && !compositor_xwm_in_write(in, r.value, r.value_len))
        {
                compositor_xwm_in_end(at, 0);
                return;
        }
        if (r.bytes_after)
        {
                in->offset += r.value_len / XWM_UNIT;
                compositor_xwm_in_ask(at);
                return;
        }
        in->offset = 0;
        if (!in->incr)
                compositor_xwm_in_end(at, 1);
        else
                in->stage = COMPOSITOR_XWM_IN_PIECES;
}

// A SelectionNotify for one of the window manager's conversions: refused (property 0) is its end, else its value is read.
static int compositor_xwm_in_notify(const struct xwm_event *e)
{
        struct compositor_xwm *x = compositor_xwm_at;

        for (long at = 0; at < x->in_table.count; at++)
                if (x->ins[at].window && x->ins[at].window == e->requestor)
                {
                        if (!e->property)
                                compositor_xwm_in_end(at, 0);
                        else if (x->ins[at].stage == COMPOSITOR_XWM_IN_TARGETS || x->ins[at].stage == COMPOSITOR_XWM_IN_ASKED)
                        {
                                x->ins[at].offset = 0;
                                compositor_xwm_in_ask(at);
                        }
                        return 1;
                }
        return 0;
}

// A property of a requestor window changed: the owner's next INCR piece is there to read.
static int compositor_xwm_in_property(const struct xwm_event *e)
{
        struct compositor_xwm *x = compositor_xwm_at;

        for (long at = 0; at < x->in_table.count; at++)
                if (x->ins[at].window && x->ins[at].window == e->window)
                {
                        if (e->atom == x->atoms[COMPOSITOR_XWM_A_SELECTION] && e->state == 0 && x->ins[at].stage == COMPOSITOR_XWM_IN_PIECES)
                                compositor_xwm_in_ask(at);
                        return 1;
                }
        return 0;
}

/* A Wayland client's receive of a type of an X drag (which 2): converted into a pipe whose other end a pump writes to the client's
   descriptor. Refused (the descriptor closed, the client reads an end) when the type has no atom or there is no X drag. */
static unsigned int compositor_xwm_drag_target(const char *mime, unsigned long length, unsigned int *time);

static void compositor_xwm_receive_one(long client, long which, const char *mime, long fd)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned long length = 0;
        unsigned int target = 0, time = XWM_CURRENT_TIME;
        int fds[2] = {-1, -1};
        long pump, at;

        (void)client;
        while (mime && mime[length])
                length++;
        if (which == 2 && x->fd >= 0 && x->ready)
                target = compositor_xwm_drag_target(mime, length, &time);
        if (!target || compositor_xwm_pipe(fds, 1) != 0)
        {
                system_close((int)fd);
                return;
        }
        pump = compositor_wl_pump(fds[0], (int)fd);
        if (pump < 0)
        {
                system_close(fds[1]);
                return;
        }
        // From here the pump holds the read end and the client's descriptor: closing the write end is the end it reads.
        at = compositor_xwm_in_new((int)which, target, time, 0, fds[1], -1);
        if (at < 0)
                system_close(fds[1]);
        else
                x->ins[at].pump = pump;
}

/* ---- the conversions the window manager answers ---- */

static struct compositor_xwm_out *compositor_xwm_out_at(long at)
{
        return at >= 0 && at < compositor_xwm_at->out_table.count && compositor_xwm_at->outs[at].requestor ? &compositor_xwm_at->outs[at] : 0;
}

static void compositor_xwm_out_free(long at)
{
        struct compositor_xwm_out *o = compositor_xwm_out_at(at);

        if (!o)
                return;
        if (o->fd >= 0)
                system_close(o->fd);
        o->requestor = 0;
        wayland_table_give(&compositor_xwm_at->out_table, at);
}

static unsigned char compositor_xwm_piece[XWM_INCR_CHUNK];

// count bytes of the compositor's text from where the transfer is, into the piece: the bytes read.
static unsigned long compositor_xwm_out_read(struct compositor_xwm_out *o, unsigned long count)
{
        unsigned long got = 0;

        while (got < count)
        {
                long n = compositor_call(syscall(pread64), (positive)o->fd, (positive)(compositor_xwm_piece + got), (positive)(count - got),
                                         (positive)(o->base + o->offset + got), 0, 0);

                if (n <= 0)
                        break;
                got += (unsigned long)n;
        }
        return got;
}

/* The next step of an answer, when the queue to the server has room for it: a value whole and its notify; INCR's start (PropertyChange
   selected on the requestor) and its notify; a piece, once the requestor deleted the last; and the empty piece that ends it. */
static void compositor_xwm_out_step(long at)
{
        struct compositor_xwm *x = compositor_xwm_at;
        struct compositor_xwm_out *o = compositor_xwm_out_at(at);
        long room = COMPOSITOR_XWM_OUT - COMPOSITOR_XWM_ROOM - x->out_len;

        if (!o || o->stage == COMPOSITOR_XWM_OUT_WAIT || o->stage == COMPOSITOR_XWM_OUT_MULTIPLE || room <= 256)
                return;
        if (o->stage == COMPOSITOR_XWM_OUT_START && o->from == COMPOSITOR_XWM_FROM_FILE && !xwm_incr_needed(o->size))
        {
                unsigned long got, put;

                if ((unsigned long)room < 32 + o->size)
                        return;
                got = compositor_xwm_out_read(o, o->size);
                put = o->latin1 ? xwm_utf8_latin1(&o->cut, compositor_xwm_piece, got, compositor_xwm_piece) : got;
                if (got == o->size)
                        compositor_xwm_put(o->requestor, o->property, o->type, XWM_FORMAT_8, compositor_xwm_piece, put);
                if (!o->quiet)
                        compositor_xwm_notify(o->time, o->requestor, o->selection, o->target, got == o->size ? o->property : 0);
                compositor_xwm_out_free(at);
                return;
        }
        if (o->stage == COMPOSITOR_XWM_OUT_START)
        {
                unsigned char req[32];
                long n = xwm_enc_incr(req, sizeof req, o->requestor, o->property, x->atoms[COMPOSITOR_XWM_A_INCR],
                                      o->size == XWM_INCR_SIZE_UNKNOWN ? XWM_INCR_CHUNK : o->size);

                compositor_xwm_select(o->requestor, XWM_MASK_PROPERTY_CHANGE);
                if (n > 0)
                        compositor_xwm_queue(req, n);
                if (!o->quiet)
                        compositor_xwm_notify(o->time, o->requestor, o->selection, o->target, o->property);
                o->stage = COMPOSITOR_XWM_OUT_WAIT;
                o->deadline = clock_monotonic_nanoseconds() + COMPOSITOR_XWM_TRANSFER_NS;
                return;
        }
        for (;;)
        {
                unsigned long most = xwm_incr_piece(o->from == COMPOSITOR_XWM_FROM_FILE ? o->size - o->offset : XWM_INCR_CHUNK, (unsigned long)room);
                unsigned long got, put;

                if (!most && (o->from == COMPOSITOR_XWM_FROM_PIPE || o->offset < o->size))
                        return;
                if (o->from == COMPOSITOR_XWM_FROM_FILE)
                        got = compositor_xwm_out_read(o, most);
                else
                {
                        long n = compositor_call(syscall(read), (positive)o->fd, (positive)compositor_xwm_piece, (positive)most, 0, 0, 0);

                        // Nothing in the pipe yet: the pump knocks once it has put some there, and the pipe is read again (it may have meanwhile).
                        if (n == COMPOSITOR_XWM_EAGAIN && o->pump >= 0)
                        {
                                compositor_wl_pump_nudge(o->pump);
                                n = compositor_call(syscall(read), (positive)o->fd, (positive)compositor_xwm_piece, (positive)most, 0, 0, 0);
                        }
                        if (n == COMPOSITOR_XWM_EAGAIN)
                                return;
                        got = n > 0 ? (unsigned long)n : 0;
                }
                put = o->latin1 ? xwm_utf8_latin1(&o->cut, compositor_xwm_piece, got, compositor_xwm_piece) : got;
                // Only the start of a character: more is read before a piece is written (an empty one would be the end).
                if (got && !put)
                        continue;
                compositor_xwm_put(o->requestor, o->property, o->type, XWM_FORMAT_8, compositor_xwm_piece, put);
                o->offset += got;
                if (!got)
                {
                        compositor_xwm_out_free(at);
                        return;
                }
                o->stage = COMPOSITOR_XWM_OUT_WAIT;
                o->deadline = clock_monotonic_nanoseconds() + COMPOSITOR_XWM_TRANSFER_NS;
                return;
        }
}

// The requestor deleted the piece it read: the next is written.
static void compositor_xwm_out_property(const struct xwm_event *e)
{
        struct compositor_xwm *x = compositor_xwm_at;

        if (e->state != 1)
                return;
        for (long at = 0; at < x->out_table.count; at++)
                if (x->outs[at].requestor && x->outs[at].requestor == e->window && x->outs[at].property == e->atom &&
                    x->outs[at].stage == COMPOSITOR_XWM_OUT_WAIT)
                {
                        x->outs[at].stage = COMPOSITOR_XWM_OUT_NEXT;
                        compositor_xwm_out_step(at);
                }
}

/* The types of the drag's Wayland source (2), each with its null; text says whether text is among them. */
static long compositor_xwm_drag_types(const char **types);

static long compositor_xwm_side_types(int which, const char **types, int *text)
{
        long bytes = which == 2 ? compositor_xwm_drag_types(types) : 0;

        *text = 0;
        for (long at = 0; at < bytes && !*text;)
        {
                long n = 0;

                while (at + n < bytes && (*types)[at + n])
                        n++;
                *text = xwm_target_kind((const unsigned char *)*types + at, (unsigned long)n) == XWM_TARGET_UTF8 ||
                        xwm_target_kind((const unsigned char *)*types + at, (unsigned long)n) == XWM_TARGET_LATIN1;
                at += n + 1;
        }
        return bytes;
}

// The atom of a name, when the window manager knows it (0 when it does not yet).
static unsigned int compositor_xwm_atom_of(const char *name, unsigned long length)
{
        long found = compositor_xwm_atom_named((const unsigned char *)name, length);

        return found >= 0 && compositor_xwm_at->atom[found].known ? compositor_xwm_at->atom[found].atom : 0;
}

/* TARGETS: TARGETS, TIMESTAMP and MULTIPLE, then each name the compositor's copy is offered under to X (its types, and the names text goes
   by when it has text) with a known atom; for a drag, the five text targets when its source has text and each other type of it. Past
   what one request holds, it goes by INCR as any value: its atoms written to a memfd of their own first. */
static void compositor_xwm_targets(const struct xwm_event *e, int which, unsigned int property, int quiet)
{
        struct compositor_xwm *x = compositor_xwm_at;
        static const unsigned int text_atoms[] = {COMPOSITOR_XWM_A_UTF8, COMPOSITOR_XWM_A_TEXT, COMPOSITOR_XWM_A_MIME_UTF8, COMPOSITOR_XWM_A_MIME_TEXT};
        unsigned char data[XWM_PROPERTY_MAX];
        const char *types = 0, *name;
        unsigned int count = 0, atom;
        unsigned long length;
        int text = 0;
        long bytes = compositor_xwm_side_types(which, &types, &text);

        xwm_put32(data + 4 * count++, x->atoms[COMPOSITOR_XWM_A_TARGETS]);
        xwm_put32(data + 4 * count++, x->atoms[COMPOSITOR_XWM_A_TIMESTAMP]);
        xwm_put32(data + 4 * count++, x->atoms[COMPOSITOR_XWM_A_MULTIPLE]);
        for (unsigned long nth = 0; which < 2 && count < sizeof data / 4 &&
                                    (name = windows_clip_offered(compositor_clip.map[which], compositor.table->clip_length[which], nth, 1, &length));
             nth++)
                if ((atom = windows_clip_same(name, length, "STRING") ? COMPOSITOR_XWM_XA_STRING : compositor_xwm_atom_of(name, length)))
                        xwm_put32(data + 4 * count++, atom);
        if (text)
        {
                for (unsigned int k = 0; k < sizeof text_atoms / sizeof text_atoms[0]; k++)
                        xwm_put32(data + 4 * count++, x->atoms[text_atoms[k]]);
                xwm_put32(data + 4 * count++, COMPOSITOR_XWM_XA_STRING);
        }
        for (long at = 0; at < bytes && count < sizeof data / 4;)
        {
                long n = 0;

                while (at + n < bytes && types[at + n])
                        n++;
                if (xwm_target_kind((const unsigned char *)types + at, (unsigned long)n) == XWM_TARGET_MIME &&
                    (atom = compositor_xwm_atom_of(types + at, (unsigned long)n)))
                        xwm_put32(data + 4 * count++, atom);
                at += n + 1;
        }
        compositor_xwm_put(e->requestor, property, COMPOSITOR_XWM_ATOM_TYPE, XWM_FORMAT_32, data, count * 4);
        if (!quiet)
                compositor_xwm_notify(e->time, e->requestor, e->selection, e->target, property);
}

// The Wayland type a target is asked of the window manager's side as: a text target the side's first text type, any other its own name.
static long compositor_xwm_side_mime(int which, int kind, const unsigned char *name, unsigned long length, const char **mime)
{
        const char *types = 0;
        int text = 0;
        long bytes = compositor_xwm_side_types(which, &types, &text);

        for (long at = 0; at < bytes;)
        {
                long n = 0;
                int is;

                while (at + n < bytes && types[at + n])
                        n++;
                is = xwm_target_kind((const unsigned char *)types + at, (unsigned long)n);
                if ((kind == XWM_TARGET_MIME && (unsigned long)n == length && compositor_xwm_same((const unsigned char *)types + at, name, length)) ||
                    (kind != XWM_TARGET_MIME && (is == XWM_TARGET_UTF8 || is == XWM_TARGET_LATIN1)))
                {
                        *mime = types + at;
                        return n;
                }
                at += n + 1;
        }
        return 0;
}

static int compositor_xwm_drag_send(const char *mime, long fd);

/* An answer begun: from the compositor's text (a descriptor of its own) or, for any other type and for a drag, from the Wayland source's
   send into a pipe a pump fills. 0 when it is refused. */
/* The answers to a requestor window that is gone (DestroyNotify), or to its property when it asks again into it: a requestor that was
   killed in the middle of an INCR, whose window number the next client is given, deletes nothing of the old one's. */
static void compositor_xwm_out_gone(unsigned int requestor, unsigned int property)
{
        struct compositor_xwm *x = compositor_xwm_at;

        for (long at = 0; requestor && at < x->out_table.count; at++)
                if (x->outs[at].requestor == requestor && (!property || x->outs[at].property == property))
                        compositor_xwm_out_free(at);
}

static long compositor_xwm_out_begin(const struct xwm_event *e, int which, unsigned int property, int quiet)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned long length = 0;
        const unsigned char *name = compositor_xwm_atom_name(e->target, &length);
        int kind = name ? xwm_target_kind(name, length) : XWM_TARGET_NONE, fd = -1, from;
        const char *mime = 0;
        struct windows_clip_type type = {0, 0, 0, 0};
        struct compositor_xwm_out *o;
        long at, pump_at = -1;

        compositor_xwm_out_gone(e->requestor, property);
        if (!name || (which < 2 && !compositor_xwm_may_paste(e->requestor)))
                return -1;
        // The compositor's copy: the type of the target's name (a name text goes by is its text), wherever it is in the copy.
        if (which < 2)
        {
                if (compositor_clip.fd[which] < 0 ||
                    !windows_clip_find(compositor_clip.map[which], compositor.table->clip_length[which], (const char *)name, length, &type))
                        return -1;
                if (!windows_clip_same(type.name, type.name_length, WINDOWS_CLIP_TEXT))
                        kind = XWM_TARGET_MIME;
                from = COMPOSITOR_XWM_FROM_FILE;
                fd = (int)compositor_call(syscall(fcntl), (positive)compositor_clip.fd[which], COMPOSITOR_F_DUPFD_CLOEXEC, 0, 0, 0, 0);
                if (fd < 0)
                        return -1;
        }
        else
        {
                int source[2], ours[2];
                long pump;

                if (kind == XWM_TARGET_NONE || !compositor_xwm_side_mime(which, kind, name, length, &mime) || compositor_xwm_pipe(source, 0) != 0)
                        return -1;
                if (compositor_xwm_pipe(ours, 0) != 0)
                {
                        system_close(source[0]);
                        system_close(source[1]);
                        return -1;
                }
                // The source's write end is the shim's once it is sent; then the pump reads the other into the window manager's pipe.
                if (!compositor_xwm_drag_send(mime, source[1]))
                {
                        system_close(source[0]);
                        system_close(source[1]);
                        system_close(ours[0]);
                        system_close(ours[1]);
                        return -1;
                }
                pump = compositor_wl_pump(source[0], ours[1]);
                if (pump < 0)
                {
                        system_close(ours[0]);
                        return -1;
                }
                pump_at = pump;
                from = COMPOSITOR_XWM_FROM_PIPE;
                fd = ours[0];
        }
        wayland_tables_host(&compositor_wl_host);
        x->out_table.unit = sizeof(struct compositor_xwm_out);
        at = wayland_table_take(&x->out_table);
        if (at < 0)
        {
                system_close(fd);
                return -1;
        }
        x->outs = (struct compositor_xwm_out *)x->out_table.at;
        o = &x->outs[at];
        memory_zero(o, sizeof *o);
        o->requestor = e->requestor;
        o->property = property;
        o->selection = e->selection;
        o->target = e->target;
        o->time = e->time;
        o->which = (unsigned char)which;
        o->from = (unsigned char)from;
        o->latin1 = kind == XWM_TARGET_LATIN1;
        o->type = kind == XWM_TARGET_LATIN1 ? COMPOSITOR_XWM_XA_STRING : e->target == x->atoms[COMPOSITOR_XWM_A_TEXT] ? x->atoms[COMPOSITOR_XWM_A_UTF8] : e->target;
        o->fd = fd;
        o->pump = pump_at;
        o->quiet = (unsigned char)quiet;
        o->base = type.offset;
        o->size = from == COMPOSITOR_XWM_FROM_FILE ? type.length : XWM_INCR_SIZE_UNKNOWN;
        o->stage = COMPOSITOR_XWM_OUT_START;
        o->deadline = clock_monotonic_nanoseconds() + COMPOSITOR_XWM_TRANSFER_NS;
        compositor_xwm_out_step(at);
        return at;
}

/* MULTIPLE (ICCCM 2.6.2): the pairs of targets and properties the requestor wrote in its property are read, each answered into its
   property as any request is (no notify of its own), a target that cannot be (or whose first step finds no room in the queue to the
   server) made None in the pairs, and the pairs written back with the one notify of the MULTIPLE. */
static void compositor_xwm_multiple(const struct xwm_event *e, int which, unsigned int property)
{
        struct compositor_xwm *x = compositor_xwm_at;
        struct compositor_xwm_out *o;
        unsigned char req[32];
        long at;

        wayland_tables_host(&compositor_wl_host);
        x->out_table.unit = sizeof(struct compositor_xwm_out);
        at = wayland_table_take(&x->out_table);
        if (at < 0)
        {
                compositor_xwm_notify(e->time, e->requestor, e->selection, e->target, 0);
                return;
        }
        x->outs = (struct compositor_xwm_out *)x->out_table.at;
        o = &x->outs[at];
        memory_zero(o, sizeof *o);
        o->requestor = e->requestor, o->property = property, o->selection = e->selection, o->target = e->target, o->time = e->time;
        o->which = (unsigned char)which, o->stage = COMPOSITOR_XWM_OUT_MULTIPLE, o->fd = -1, o->pump = -1;
        o->deadline = clock_monotonic_nanoseconds() + COMPOSITOR_XWM_TRANSFER_NS;
        if (compositor_xwm_wait(req, xwm_enc_get_property(req, sizeof req, 0, e->requestor, property, 0, 0, COMPOSITOR_XWM_PIECE_UNITS), e->requestor,
                                COMPOSITOR_XWM_PROP_MULTIPLE, (unsigned int)at) != 0)
        {
                compositor_xwm_notify(e->time, e->requestor, e->selection, e->target, 0);
                compositor_xwm_out_free(at);
        }
}

static void compositor_xwm_multiple_reply(const unsigned char *msg, long size, const struct compositor_xwm_pending *p)
{
        struct compositor_xwm *x = compositor_xwm_at;
        struct compositor_xwm_out *o = compositor_xwm_out_at((long)p->slot), multiple;
        struct xwm_property_reply r;
        unsigned char pairs[XWM_PROPERTY_MAX];
        unsigned long count;

        if (!o || o->requestor != p->window || o->stage != COMPOSITOR_XWM_OUT_MULTIPLE)
                return;
        multiple = *o;
        compositor_xwm_out_free((long)p->slot);
        if (!msg || xwm_parse_property_reply(msg, (unsigned long)size, &r) != XWM_OK || r.format != XWM_FORMAT_32 || r.value_len > sizeof pairs)
        {
                compositor_xwm_notify(multiple.time, multiple.requestor, multiple.selection, multiple.target, 0);
                return;
        }
        count = r.value_len / 8;
        memory_copy(pairs, r.value, (positive)(count * 8));
        for (unsigned long k = 0; k < count; k++)
        {
                struct xwm_event sub;
                long at = -1;
                int ok = 1;

                memory_zero(&sub, sizeof sub);
                sub.requestor = multiple.requestor, sub.selection = multiple.selection, sub.time = multiple.time;
                sub.target = xwm_get32(pairs + 8 * k), sub.property = xwm_get32(pairs + 8 * k + 4);
                if (!sub.target || !sub.property || sub.target == x->atoms[COMPOSITOR_XWM_A_MULTIPLE])
                        ok = 0;
                else if (sub.target == x->atoms[COMPOSITOR_XWM_A_TARGETS])
                        compositor_xwm_targets(&sub, multiple.which, sub.property, 1);
                else if (sub.target == x->atoms[COMPOSITOR_XWM_A_TIMESTAMP])
                {
                        unsigned char data[4];

                        xwm_put32(data, x->sel[multiple.which].owned_time);
                        compositor_xwm_put(sub.requestor, sub.property, COMPOSITOR_XWM_XA_INTEGER, XWM_FORMAT_32, data, 4);
                }
                else if ((at = compositor_xwm_out_begin(&sub, multiple.which, sub.property, 1)) < 0)
                        ok = 0;
                else if (compositor_xwm_out_at(at) && x->outs[at].stage == COMPOSITOR_XWM_OUT_START)
                {
                        compositor_xwm_out_free(at);
                        ok = 0;
                }
                if (!ok)
                        xwm_put32(pairs + 8 * k, 0);
        }
        compositor_xwm_put(multiple.requestor, multiple.property, x->atoms[COMPOSITOR_XWM_A_ATOM_PAIR], XWM_FORMAT_32, pairs, count * 8);
        compositor_xwm_notify(multiple.time, multiple.requestor, multiple.selection, multiple.target, multiple.property);
}

/* SAVE_TARGETS (the clipboard manager's, freedesktop's ClipboardManager): an X client that owns the clipboard asks the manager to keep
   its copy before it quits. The compositor reads every X copy whole when it is made, so the answer is the copy's own: at once when it
   is the compositor's, when its read ends while it is being read, and None when it is neither (a copy the person did not make). */
static void compositor_xwm_save(const struct xwm_event *e, unsigned int property)
{
        struct compositor_xwm *x = compositor_xwm_at;
        int row = compositor.table ? compositor.table->clip_owner[0] : -1;
        int ours = 0;

        if (e->target == x->atoms[COMPOSITOR_XWM_A_TARGETS])
        {
                unsigned char data[8];

                xwm_put32(data, x->atoms[COMPOSITOR_XWM_A_TARGETS]);
                xwm_put32(data + 4, x->atoms[COMPOSITOR_XWM_A_SAVE_TARGETS]);
                compositor_xwm_put(e->requestor, property, COMPOSITOR_XWM_ATOM_TYPE, XWM_FORMAT_32, data, 8);
                compositor_xwm_notify(e->time, e->requestor, e->selection, e->target, property);
                return;
        }
        if (e->target != x->atoms[COMPOSITOR_XWM_A_SAVE_TARGETS])
        {
                compositor_xwm_notify(e->time, e->requestor, e->selection, e->target, 0);
                return;
        }
        if (x->save.requestor)
                compositor_xwm_saved(0, 0);
        if (x->sel[0].reading >= 0 && compositor_xwm_same_client(x->sel[0].x_owner, e->requestor))
        {
                x->save.requestor = e->requestor, x->save.property = property, x->save.time = e->time;
                return;
        }
        for (long at = 0; row >= 0 && at < x->window_table.count; at++)
                ours |= x->window[at].id && x->window[at].row == row && compositor_xwm_same_client(x->window[at].id, e->requestor);
        compositor_xwm_notify(e->time, e->requestor, e->selection, e->target, ours && compositor_clip.fd[0] >= 0 ? property : 0);
}

// The clipboard's X copy is the compositor's (ok) or will not be: a SAVE_TARGETS that waited for it is answered.
static void compositor_xwm_saved(unsigned int which, int ok)
{
        struct compositor_xwm *x = compositor_xwm_at;

        if (which != 0 || !x->save.requestor)
                return;
        compositor_xwm_notify(x->save.time, x->save.requestor, x->atoms[COMPOSITOR_XWM_A_MANAGER], x->atoms[COMPOSITOR_XWM_A_SAVE_TARGETS],
                              ok ? x->save.property : 0);
        x->save.requestor = 0;
}

static int compositor_xwm_drag_ours(void);

// A SelectionRequest of the window manager's selection window: TARGETS, TIMESTAMP, or a value; any other is refused.
static void compositor_xwm_selection_request(const struct xwm_event *e)
{
        struct compositor_xwm *x = compositor_xwm_at;
        int which = compositor_xwm_which(e->selection);
        unsigned int property = e->property ? e->property : e->target;

        if (e->owner != compositor_xwm_id(COMPOSITOR_XWM_SEL_WINDOW) || !e->requestor)
                return;
        if (e->selection && e->selection == x->atoms[COMPOSITOR_XWM_A_MANAGER] && property)
        {
                compositor_xwm_save(e, property);
                return;
        }
        if (which < 0 || (which < 2 && !x->sel[which].owned) || (which == 2 && !compositor_xwm_drag_ours()) || !property)
        {
                compositor_xwm_notify(e->time, e->requestor, e->selection, e->target, 0);
                return;
        }
        if (e->target == x->atoms[COMPOSITOR_XWM_A_TARGETS])
                compositor_xwm_targets(e, which, property, 0);
        else if (e->target == x->atoms[COMPOSITOR_XWM_A_TIMESTAMP])
        {
                unsigned char data[4];

                xwm_put32(data, x->sel[which].owned_time);
                compositor_xwm_put(e->requestor, property, COMPOSITOR_XWM_XA_INTEGER, XWM_FORMAT_32, data, 4);
                compositor_xwm_notify(e->time, e->requestor, e->selection, e->target, property);
        }
        else if (e->target == x->atoms[COMPOSITOR_XWM_A_MULTIPLE] && which < 2)
                compositor_xwm_multiple(e, which, property);
        else if (compositor_xwm_out_begin(e, which, property, 0) < 0)
                compositor_xwm_notify(e->time, e->requestor, e->selection, e->target, 0);
}

/* ---- who owns a selection ---- */

static void compositor_xwm_xdnd_owner(unsigned int owner);

/* XFIXES: a selection's owner changed. The window manager's own is the time it owns it from. An X client's copy is read into the
   compositor's when the person has just touched that client, and one whose owner went is the compositor's still, owned in X again. */
static void compositor_xwm_owner(const struct xwm_event *e)
{
        struct compositor_xwm *x = compositor_xwm_at;
        int which = compositor_xwm_which(e->selection), row;
        unsigned int owner = e->detail == 0 ? e->owner : 0;
        long fd, at;

        if (which < 0)
                return;
        if (owner && owner == compositor_xwm_id(COMPOSITOR_XWM_SEL_WINDOW))
        {
                x->sel[which].owned_time = e->selection_time;
                return;
        }
        x->sel[which].x_owner = owner;
        if (which == 2)
        {
                compositor_xwm_xdnd_owner(owner);
                return;
        }
        x->sel[which].owned = 0;
        if (x->sel[which].reading >= 0)
                compositor_xwm_in_free(x->sel[which].reading);
        // Its owner gone (or one that is no X client's): the compositor's copy is owned in X again, by the window manager.
        if (!owner)
        {
                x->sel[which].dirty = 1;
                return;
        }
        row = compositor_xwm_copy_row(owner);
        if (row < 0)
                return;
        // MFD_CLOEXEC | MFD_ALLOW_SEALING
        fd = compositor_call(syscall(memfd_create), (positive) "moonwater-x-copy", 3, 0, 0, 0, 0);
        if (fd < 0)
                return;
        at = compositor_xwm_in_new(which, x->atoms[COMPOSITOR_XWM_A_TARGETS], e->selection_time, 1, (int)fd, row);
        if (at < 0)
                system_close((int)fd);
        else
                x->sel[which].reading = at;
}

/* The compositor's selection changed (compositor_wl_selection), by anyone but the window manager: an X copy still being read is let go
   (it would land over the newer one), and its X ownership is looked at in the next pass. */
static void compositor_xwm_selection_changed_one(unsigned int which)
{
        struct compositor_xwm *x = compositor_xwm_at;

        if (which > 1 || x->setting == which + 1)
                return;
        if (x->sel[which].reading >= 0)
                compositor_xwm_in_free(x->sel[which].reading);
        x->sel[which].dirty = 1;
}

/* The selections owned in X as the compositor holds them: owned by the selection window while there is a copy, the atoms of the names
   it is offered under interned first; let go when there is none. Then each transfer's step, and those that moved nothing in their time
   let go. */
static void compositor_xwm_selections_pass(void)
{
        struct compositor_xwm *x = compositor_xwm_at;
        positive now = clock_monotonic_nanoseconds();

        for (int which = 0; which < 2 && x->claimed && x->xfixes_event; which++)
        {
                if (!x->sel[which].dirty)
                        continue;
                x->sel[which].dirty = 0;
                if (compositor.table && compositor.table->clip_on && compositor_clip.fd[which] >= 0)
                {
                        const char *name;
                        unsigned long length;

                        for (unsigned long nth = 0; (name = windows_clip_offered(compositor_clip.map[which], compositor.table->clip_length[which], nth, 1, &length));
                             nth++)
                                if (!windows_clip_same(name, length, "STRING"))
                                        compositor_xwm_intern_name((const unsigned char *)name, length);
                        compositor_xwm_own(which, compositor_xwm_id(COMPOSITOR_XWM_SEL_WINDOW));
                }
                else if (x->sel[which].owned)
                        compositor_xwm_own(which, 0);
        }
        for (long at = 0; at < x->in_table.count && x->fd >= 0 && !x->broken; at++)
        {
                struct compositor_xwm_in *in = compositor_xwm_in_at(at);

                if (in && now > in->deadline)
                        compositor_xwm_in_end(at, 0);
                else if (in && in->ready && !in->asking)
                        compositor_xwm_in_ask(at);
        }
        for (long at = 0; at < x->out_table.count && x->fd >= 0 && !x->broken; at++)
        {
                struct compositor_xwm_out *o = compositor_xwm_out_at(at);

                if (o && now > o->deadline)
                        compositor_xwm_out_free(at);
                else if (o && o->stage != COMPOSITOR_XWM_OUT_WAIT)
                        compositor_xwm_out_step(at);
        }
}

/* XFIXES's reply: with it, the selection window is told who owns the three selections, and the clipboard and the primary selection are
   owned in X as the compositor holds them (the next pass). */
static void compositor_xwm_xfixes(const unsigned char *in, long size)
{
        struct compositor_xwm *x = compositor_xwm_at;
        struct xwm_extension_reply r;
        unsigned char req[32];
        long n;

        if (xwm_parse_extension_reply(in, (unsigned long)size, &r) != XWM_OK)
        {
                x->broken = 1;
                return;
        }
        if (!r.present || r.major_opcode < XWM_EXT_MAJOR_FIRST || r.first_event <= XWM_EVENT_LAST)
        {
                compositor_xwm_log("xwm: no XFIXES extension: nothing crosses between X and Wayland\n");
                return;
        }
        x->xfixes_opcode = r.major_opcode;
        x->xfixes_event = r.first_event;
        n = xwm_enc_xfixes_query_version(req, sizeof req, r.major_opcode, 5, 0);
        if (n > 0)
                compositor_xwm_queue(req, n);
        for (int which = 0; which < 3; which++)
        {
                n = xwm_enc_xfixes_select_selection_input(req, sizeof req, r.major_opcode, compositor_xwm_id(COMPOSITOR_XWM_SEL_WINDOW), x->sel[which].atom,
                                                          XWM_XFIXES_SELECTION_MASK);
                if (n > 0)
                        compositor_xwm_queue(req, n);
                x->sel[which].dirty = which < 2;
        }
        // The clipboard manager (SAVE_TARGETS) is the window manager's selection window.
        n = xwm_enc_set_selection_owner(req, sizeof req, compositor_xwm_id(COMPOSITOR_XWM_SEL_WINDOW), x->atoms[COMPOSITOR_XWM_A_MANAGER], XWM_CURRENT_TIME);
        if (n > 0)
                compositor_xwm_queue(req, n);
        compositor_xwm_log("xwm: the clipboard and drag and drop cross between X and Wayland (XFIXES)\n");
}

/*
        ---- drag and drop (DRAG AND DROP): XDND version 5 between the X windows and the Wayland ones ----

        A Wayland drag over an X window (the shim's xdrag): the window manager owns XdndSelection with its selection window, which is the
        XDND source; the X window's XdndAware is read (its types' atoms interned before, so they are known when the reply comes), then
        XdndEnter with the types (text as the five text targets, more than three in XdndTypeList), an XdndPosition at a time (the next
        once the status of the last has come), each XdndStatus told to the shim as the type taken and the action; the drop is XdndDrop,
        the conversion of XdndSelection is answered from the source's send (as a selection's is, by INCR through a pump), and
        XdndFinished is the drag's end. A leave, a cancel (Escape, the source gone) or the X window gone is XdndLeave, or the drop failed.

        An X drag over a Wayland window: while an X client owns XdndSelection and holds the left button on its window, and the pointer is
        over a Wayland client's window, the window manager's proxy (input only, XdndAware) is mapped over that window, on top, so the X
        source finds it there, and unmapped elsewhere (the X windows find their own). Its XdndEnter's types are named (their atoms'
        names asked for, the Wayland drag begun once they have come), its XdndPosition the Wayland drag's motion over the window under
        that place, answered by XdndStatus with what the Wayland target takes (and again when that changes), its XdndLeave the drag's
        leave, its XdndDrop the drop; the target's receive converts XdndSelection with the drop's time into the target's descriptor, and
        the drag's end is XdndFinished, accepted when the target finished.
*/

// A client message of window to, sent to dest (XDND's proxy: the message names the window under the pointer and goes to its proxy).
static void compositor_xwm_message_via(unsigned int dest, unsigned int to, unsigned int type, const unsigned int *data)
{
        unsigned char event[XWM_EVENT_SIZE], req[64];
        long n;

        if (!to || !dest || xwm_enc_client_message(event, sizeof event, to, type, data, XWM_DATA_LONGS) != XWM_EVENT_SIZE)
                return;
        n = xwm_enc_send_event(req, sizeof req, 0, dest, 0, event, XWM_EVENT_SIZE);
        if (n > 0)
                compositor_xwm_queue(req, n);
}

static void compositor_xwm_message32(unsigned int to, unsigned int type, const unsigned int *data)
{
        compositor_xwm_message_via(to, to, type, data);
}

// A message of a Wayland drag's to the X window it is over, by way of that window's proxy.
static void compositor_xwm_wdrag_say(unsigned int type, const unsigned int *data)
{
        struct compositor_xwm *x = compositor_xwm_at;

        compositor_xwm_message_via(x->wdrag.proxy ? x->wdrag.proxy : x->wdrag.target, x->wdrag.target, type, data);
}

// The X window of a row (a popup's too), or 0.
static struct compositor_xwm_window *compositor_xwm_of_row(long row)
{
        struct compositor_xwm *x = compositor_xwm_at;
        struct compositor_xwm_window *found = 0;

        for (long at = 0; row >= 0 && at < x->window_table.count; at++)
                if (x->window[at].id && x->window[at].row == (int)row && (!found || (found->state & COMPOSITOR_XWM_POPUPISH)))
                        found = &x->window[at];
        return found;
}

/* The X targets of the Wayland drag's types: the text ones as the five text targets first, then each other as the atom of its name
   (interned when it is not known, when intern says so). Their count. */
static unsigned int compositor_xwm_dnd_atoms(unsigned int *atoms, unsigned int room, int intern)
{
        struct compositor_xwm *x = compositor_xwm_at;
        const char *types = 0;
        unsigned int actions = 0, count = 0;
        long bytes = wayland_drag_x_types(&types, &actions);
        int text = 0;

        for (long at = 0; at < bytes; at++)
        {
                long n = 0;
                int kind;

                while (at + n < bytes && types[at + n])
                        n++;
                kind = xwm_target_kind((const unsigned char *)types + at, (unsigned long)n);
                text |= kind == XWM_TARGET_UTF8 || kind == XWM_TARGET_LATIN1;
                at += n;
        }
        if (text)
        {
                unsigned int list[5] = {x->atoms[COMPOSITOR_XWM_A_UTF8], x->atoms[COMPOSITOR_XWM_A_MIME_UTF8], x->atoms[COMPOSITOR_XWM_A_MIME_TEXT],
                                        COMPOSITOR_XWM_XA_STRING, x->atoms[COMPOSITOR_XWM_A_TEXT]};

                for (unsigned int k = 0; k < 5 && count < room; k++)
                        atoms[count++] = list[k];
        }
        for (long at = 0; at < bytes; at++)
        {
                long n = 0;
                unsigned int atom;

                while (at + n < bytes && types[at + n])
                        n++;
                if (xwm_target_kind((const unsigned char *)types + at, (unsigned long)n) == XWM_TARGET_MIME)
                {
                        long found = compositor_xwm_atom_named((const unsigned char *)types + at, (unsigned long)n);

                        atom = found >= 0 && x->atom[found].known ? x->atom[found].atom : 0;
                        if (!atom && intern)
                                compositor_xwm_intern_name((const unsigned char *)types + at, (unsigned long)n);
                        if (atom && count < room)
                                atoms[count++] = atom;
                }
                at += n;
        }
        return count;
}

static void compositor_xwm_wdrag_position(void)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned int data[5] = {compositor_xwm_id(COMPOSITOR_XWM_SEL_WINDOW), 0, x->wdrag.place, XWM_CURRENT_TIME, x->wdrag.action};

        compositor_xwm_wdrag_say(x->atoms[COMPOSITOR_XWM_A_XDND_POSITION], data);
        x->wdrag.waiting = 1;
        x->wdrag.moved = 0;
}

static void compositor_xwm_wdrag_leave(void)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned int data[5] = {compositor_xwm_id(COMPOSITOR_XWM_SEL_WINDOW), 0, 0, 0, 0};

        if (x->wdrag.entered)
                compositor_xwm_wdrag_say(x->atoms[COMPOSITOR_XWM_A_XDND_LEAVE], data);
        x->wdrag.target = x->wdrag.proxy = 0;
        x->wdrag.entered = x->wdrag.waiting = x->wdrag.moved = x->wdrag.aware_no = 0;
        x->wdrag.row = -1;
}

static void compositor_xwm_xdrag_reset(void)
{
        struct compositor_xwm *x = compositor_xwm_at;

        x->xdrag.source = x->xdrag.version = x->xdrag.action = x->xdrag.time = x->xdrag.told_action = x->xdrag.listed = x->xdrag.words = 0;
        x->xdrag.entered = x->xdrag.naming = x->xdrag.began = x->xdrag.dropped = x->xdrag.told_accept = x->xdrag.escaped = 0;
        x->xdrag.types.count = 0;
}

static void compositor_xwm_wdrag_actions(unsigned int actions);
static void compositor_xwm_xdrag_ask_actions(unsigned int source);

/* The shim's word of a Wayland drag over an X window (host->xdrag), or of the end of a drag of the X side's (py: its action). */
static long compositor_xwm_xdrag_one(long what, long row, long px, long py)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned char req[32];

        if (what == WAYLAND_XDRAG_END)
        {
                // An X drag's end: its source told it finished (the target took it) or not, once it dropped.
                if (x->xdrag.began || x->xdrag.dropped)
                {
                        // The action the drag came to at its end (py: the drag's bits), the one the target finished with.
                        unsigned int done = py ? xwm_dnd_action_atom((unsigned int)py, x->atoms[COMPOSITOR_XWM_A_XDND_COPY], x->atoms[COMPOSITOR_XWM_A_XDND_MOVE],
                                                                     x->atoms[COMPOSITOR_XWM_A_XDND_ASK])
                                              : x->xdrag.action;
                        unsigned int data[5] = {compositor_xwm_id(COMPOSITOR_XWM_PROXY_WINDOW), px ? 1u : 0u, px ? done : 0, 0, 0};

                        if (x->xdrag.dropped)
                        {
                                compositor_xwm_message32(x->xdrag.source, x->atoms[COMPOSITOR_XWM_A_XDND_FINISHED], data);
                                compositor_xwm_xdrag_reset();
                        }
                        else
                        {
                                x->xdrag.began = 0;
                                x->xdrag.cancelled = 1;
                        }
                }
                if (x->wdrag.live)
                {
                        compositor_xwm_wdrag_leave();
                        x->wdrag.live = x->wdrag.dropped = 0;
                        if (x->fd >= 0 && x->ready)
                                compositor_xwm_own(2, 0);
                }
                return 0;
        }
        if (x->fd < 0 || !x->ready || !x->claimed || !x->xfixes_event)
                return 0;
        if (what == WAYLAND_XDRAG_LEAVE)
        {
                compositor_xwm_wdrag_leave();
                return 0;
        }
        if (what == WAYLAND_XDRAG_DROP)
        {
                unsigned int data[5] = {compositor_xwm_id(COMPOSITOR_XWM_SEL_WINDOW), 0, XWM_CURRENT_TIME, 0, 0};

                if (!x->wdrag.entered)
                {
                        wayland_drag_x_finished(0);
                        return 0;
                }
                compositor_xwm_wdrag_say(x->atoms[COMPOSITOR_XWM_A_XDND_DROP], data);
                x->wdrag.dropped = 1;
                return 0;
        }
        if (what == WAYLAND_XDRAG_OVER)
        {
                struct compositor_xwm_window *w = compositor_xwm_of_row(row);
                unsigned int atoms[64], actions = 0;
                const char *types = 0;

                if (!w)
                        return 0;
                if (!x->wdrag.live)
                {
                        // The drag's first X window: its types' atoms asked for, XdndSelection owned for its conversions, and the actions it
                        // offers listed with their words where it offers more than one or asks (XdndActionList, XdndActionDescription).
                        x->wdrag.live = 1;
                        x->wdrag.dropped = 0;
                        compositor_xwm_dnd_atoms(atoms, 64, 1);
                        compositor_xwm_own(2, compositor_xwm_id(COMPOSITOR_XWM_SEL_WINDOW));
                        wayland_drag_x_types(&types, &actions);
                        compositor_xwm_wdrag_actions(actions);
                }
                wayland_drag_x_types(&types, &actions);
                x->wdrag.action = xwm_dnd_action_atom(actions, x->atoms[COMPOSITOR_XWM_A_XDND_COPY], x->atoms[COMPOSITOR_XWM_A_XDND_MOVE],
                                                      x->atoms[COMPOSITOR_XWM_A_XDND_ASK]);
                x->wdrag.place = xwm_dnd_place(w->x + px, w->y + py);
                x->wdrag.moved = 1;
                if (x->wdrag.target != w->id)
                {
                        // A new X window: its XdndProxy first (XDND's proxy), then the XdndAware of the window its messages go to.
                        compositor_xwm_wdrag_leave();
                        x->wdrag.target = w->id;
                        x->wdrag.row = (int)row;
                        compositor_xwm_wait(req, xwm_enc_get_property(req, sizeof req, 0, w->id, x->atoms[COMPOSITOR_XWM_A_XDND_PROXY], COMPOSITOR_XWM_XA_WINDOW_TYPE, 0, 1),
                                            w->id, COMPOSITOR_XWM_PROP_XDND_PROXY, 0);
                }
                else if (x->wdrag.entered && !x->wdrag.waiting)
                        compositor_xwm_wdrag_position();
                compositor_xwm_flush();
                return 1;
        }
        return 0;
}

/*
        The XdndProxy of the X window a Wayland drag is over: a proxy that names itself in its own XdndProxy is where the drag's messages
        go (asked for here, and its XdndAware after), anything else (none, a window gone, one that names another) is no proxy, and the
        window's own XdndAware is read.
*/
static void compositor_xwm_wdrag_proxy(const struct xwm_property_reply *r, const struct compositor_xwm_pending *p)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned int named = r && r->format == XWM_FORMAT_32 && r->value_len >= 4 ? xwm_get32(r->value) : 0, ask;
        unsigned char req[32];

        if (!x->wdrag.live || p->window != x->wdrag.target)
                return;
        if (p->what == COMPOSITOR_XWM_PROP_XDND_PROXY && named && named != p->window)
        {
                compositor_xwm_wait(req, xwm_enc_get_property(req, sizeof req, 0, named, x->atoms[COMPOSITOR_XWM_A_XDND_PROXY], COMPOSITOR_XWM_XA_WINDOW_TYPE, 0, 1),
                                    p->window, COMPOSITOR_XWM_PROP_XDND_PROXIED, named);
                return;
        }
        x->wdrag.proxy = p->what == COMPOSITOR_XWM_PROP_XDND_PROXIED && named == p->slot ? named : 0;
        ask = x->wdrag.proxy ? x->wdrag.proxy : p->window;
        compositor_xwm_wait(req, xwm_enc_get_property(req, sizeof req, 0, ask, x->atoms[COMPOSITOR_XWM_A_XDND_AWARE], COMPOSITOR_XWM_ATOM_TYPE, 0, 1), p->window,
                            COMPOSITOR_XWM_PROP_XDND_AWARE, ask);
}

/* The actions a Wayland drag's source offers, for an X target: XdndActionList and XdndActionDescription on the selection window, the
   window manager's, when there is a choice in them (more than one, or ask); none when there is not. */
static void compositor_xwm_wdrag_actions(unsigned int actions)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned int window = compositor_xwm_id(COMPOSITOR_XWM_SEL_WINDOW);
        unsigned char list[12], words[16];
        unsigned long count = 0, bytes = 0;

        if (!(actions & XWM_DND_ASK) && actions != (XWM_DND_COPY | XWM_DND_MOVE))
        {
                compositor_xwm_put(window, x->atoms[COMPOSITOR_XWM_A_XDND_ACTIONS], COMPOSITOR_XWM_ATOM_TYPE, XWM_FORMAT_32, list, 0);
                return;
        }
        if (actions & XWM_DND_COPY)
        {
                xwm_put32(list + 4 * count++, x->atoms[COMPOSITOR_XWM_A_XDND_COPY]);
                memory_copy(words + bytes, "Copy", 5), bytes += 5;
        }
        if (actions & XWM_DND_MOVE)
        {
                xwm_put32(list + 4 * count++, x->atoms[COMPOSITOR_XWM_A_XDND_MOVE]);
                memory_copy(words + bytes, "Move", 5), bytes += 5;
        }
        if (actions & XWM_DND_ASK)
        {
                xwm_put32(list + 4 * count++, x->atoms[COMPOSITOR_XWM_A_XDND_ASK]);
                memory_copy(words + bytes, "Ask", 4), bytes += 4;
        }
        compositor_xwm_put(window, x->atoms[COMPOSITOR_XWM_A_XDND_ACTIONS], COMPOSITOR_XWM_ATOM_TYPE, XWM_FORMAT_32, list, count * 4);
        compositor_xwm_put(window, x->atoms[COMPOSITOR_XWM_A_XDND_DESCRIPTIONS], COMPOSITOR_XWM_XA_STRING, XWM_FORMAT_8, words, bytes);
}

// The X window's XdndAware (its proxy's, through one): entered (version 3 and later, the types in its enter and in XdndTypeList), or no
// target there.
static void compositor_xwm_wdrag_aware(const struct xwm_property_reply *r, unsigned int window)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned int atoms[256], data[5], count, version;

        if (!x->wdrag.live || window != x->wdrag.target)
                return;
        version = r && r->format == XWM_FORMAT_32 && r->value_len >= 4 ? xwm_get32(r->value) : 0;
        if (version < 3)
        {
                x->wdrag.aware_no = 1;
                wayland_drag_x_status(0, 0);
                return;
        }
        x->wdrag.version = version > XWM_XDND_VERSION ? XWM_XDND_VERSION : version;
        count = compositor_xwm_dnd_atoms(atoms, 256, 0);
        if (count > 3)
        {
                unsigned char list[1024];

                for (unsigned int k = 0; k < count; k++)
                        xwm_put32(list + 4 * k, atoms[k]);
                compositor_xwm_put(compositor_xwm_id(COMPOSITOR_XWM_SEL_WINDOW), x->atoms[COMPOSITOR_XWM_A_XDND_TYPES], COMPOSITOR_XWM_ATOM_TYPE, XWM_FORMAT_32,
                                   list, count * 4);
        }
        xwm_dnd_enter(data, compositor_xwm_id(COMPOSITOR_XWM_SEL_WINDOW), x->wdrag.version, atoms, count);
        compositor_xwm_wdrag_say(x->atoms[COMPOSITOR_XWM_A_XDND_ENTER], data);
        x->wdrag.entered = 1;
        compositor_xwm_wdrag_position();
}

// XdndStatus and XdndFinished to the selection window: what the X window takes (told to the shim), and its end of the drop.
static void compositor_xwm_wdrag_message(const struct xwm_event *e)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned int copy = x->atoms[COMPOSITOR_XWM_A_XDND_COPY];

        if (!x->wdrag.live || !x->wdrag.target || e->data[0] != x->wdrag.target)
                return;
        if (e->atom == x->atoms[COMPOSITOR_XWM_A_XDND_STATUS])
        {
                unsigned int accepted = e->data[1] & 1u, action = xwm_dnd_action(e->data[4], copy, x->atoms[COMPOSITOR_XWM_A_XDND_MOVE], x->atoms[COMPOSITOR_XWM_A_XDND_ASK]);

                x->wdrag.waiting = 0;
                wayland_drag_x_status(accepted, accepted && !action ? XWM_DND_COPY : action);
                if (x->wdrag.moved && x->wdrag.entered && !x->wdrag.dropped)
                        compositor_xwm_wdrag_position();
        }
        else if (e->atom == x->atoms[COMPOSITOR_XWM_A_XDND_FINISHED] && x->wdrag.dropped)
                wayland_drag_x_finished(x->wdrag.version < 5 || (e->data[1] & 1u));
}

// The X window a Wayland drag is over is gone: its drop failed, or the drag is over no target.
static void compositor_xwm_wdrag_gone(unsigned int window)
{
        struct compositor_xwm *x = compositor_xwm_at;

        if (!x->wdrag.live || window != x->wdrag.target)
                return;
        x->wdrag.entered = 0;
        if (x->wdrag.dropped)
                wayland_drag_x_finished(0);
        else
        {
                x->wdrag.target = 0;
                wayland_drag_x_status(0, 0);
        }
}

static void compositor_xwm_xdrag_status(void)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned long action = 0;
        int accepted = x->xdrag.began && !x->xdrag.escaped && wayland_drag_x_answer(&action);
        unsigned int data[5] = {compositor_xwm_id(COMPOSITOR_XWM_PROXY_WINDOW), (accepted ? 1u : 0u) | 2u, 0, 0, 0};

        data[4] = accepted ? xwm_dnd_action_atom((unsigned int)action, x->atoms[COMPOSITOR_XWM_A_XDND_COPY], x->atoms[COMPOSITOR_XWM_A_XDND_MOVE],
                                                 x->atoms[COMPOSITOR_XWM_A_XDND_ASK])
                           : 0;
        compositor_xwm_message32(x->xdrag.source, x->atoms[COMPOSITOR_XWM_A_XDND_STATUS], data);
        x->xdrag.told_accept = (unsigned char)accepted;
        x->xdrag.told_action = data[4];
        if (accepted)
                x->xdrag.action = data[4];
}

/*
        The Wayland actions of what an X source asks: its action, and for XdndActionAsk the person's choice, which a Wayland target makes
        (ask), of the actions its XdndActionList names that Wayland has (copy and move); a source that lists none is asked for a copy
        too, the one action every XDND source has.
*/
static unsigned int compositor_xwm_xdrag_actions(void)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned int action = xwm_dnd_action(x->xdrag.action, x->atoms[COMPOSITOR_XWM_A_XDND_COPY], x->atoms[COMPOSITOR_XWM_A_XDND_MOVE],
                                             x->atoms[COMPOSITOR_XWM_A_XDND_ASK]);

        if (action == XWM_DND_ASK)
                return XWM_DND_ASK | ((x->xdrag.listed & (XWM_DND_COPY | XWM_DND_MOVE)) ? x->xdrag.listed & (XWM_DND_COPY | XWM_DND_MOVE) : XWM_DND_COPY);
        return action ? action : XWM_DND_COPY;
}

// The X drag's types, named, as Wayland types and actions: the Wayland drag of it begun.
static void compositor_xwm_xdrag_begin(void)
{
        struct compositor_xwm *x = compositor_xwm_at;
        const unsigned int *atoms = (const unsigned int *)x->xdrag.types.at;
        long bytes = 0;

        wayland_tables_host(&compositor_wl_host);
        x->type_table.unit = 1;
        for (long k = 0; atoms && k < x->xdrag.types.count; k++)
        {
                unsigned long length = 0;
                const unsigned char *name = compositor_xwm_atom_name(atoms[k], &length);
                unsigned char mime[XWM_ATOM_NAME_MAX + 1];
                long n = name ? xwm_target_mime(name, length, mime, sizeof mime) : 0;

                if (n <= 0 || compositor_xwm_listed(x->type_table.at, bytes, mime, (unsigned long)n) ||
                    !wayland_table_reach(&x->type_table, bytes + n + 1))
                        continue;
                memory_copy(x->type_table.at + bytes, mime, (positive)n + 1);
                bytes += n + 1;
        }
        if (bytes && wayland_drag_x_begin((const char *)x->type_table.at, bytes, compositor_xwm_xdrag_actions()))
                x->xdrag.began = 1;
}

// A client message to the proxy: the X source's enter, position, leave and drop.
static void compositor_xwm_proxy_message(const struct xwm_event *e)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned char req[32];

        if (e->atom == x->atoms[COMPOSITOR_XWM_A_XDND_ENTER])
        {
                if (x->xdrag.began)
                        wayland_drag_x_leave();
                compositor_xwm_xdrag_reset();
                x->xdrag.cancelled = 0;
                x->xdrag.source = e->data[0];
                x->xdrag.version = e->data[1] >> 24;
                x->xdrag.entered = x->xdrag.naming = 1;
                wayland_tables_host(&compositor_wl_host);
                x->xdrag.types.unit = sizeof(unsigned int);
                if (e->data[1] & 1u)
                {
                        compositor_xwm_wait(req, xwm_enc_get_property(req, sizeof req, 0, e->data[0], x->atoms[COMPOSITOR_XWM_A_XDND_TYPES], COMPOSITOR_XWM_ATOM_TYPE, 0, 1024),
                                            e->data[0], COMPOSITOR_XWM_PROP_XDND_TYPES, 0);
                        compositor_xwm_xdrag_ask_actions(e->data[0]);
                        return;
                }
                compositor_xwm_xdrag_ask_actions(e->data[0]);
                for (int k = 2; k < 5; k++)
                        if (e->data[k] && wayland_table_reach(&x->xdrag.types, x->xdrag.types.count + 1))
                        {
                                ((unsigned int *)x->xdrag.types.at)[x->xdrag.types.count - 1] = e->data[k];
                                compositor_xwm_name_atom(e->data[k]);
                        }
                compositor_xwm_wait(req, xwm_enc_get_property(req, sizeof req, 0, e->data[0], x->atoms[COMPOSITOR_XWM_A_XDND_TYPES], COMPOSITOR_XWM_ATOM_TYPE, 0, 0),
                                    e->data[0], COMPOSITOR_XWM_PROP_XDND_SYNC, 0);
                return;
        }
        if (!x->xdrag.entered || e->data[0] != x->xdrag.source)
                return;
        if (e->atom == x->atoms[COMPOSITOR_XWM_A_XDND_POSITION])
        {
                int rx = (int)(e->data[2] >> 16), ry = (int)(e->data[2] & 0xffffu), tab = 0, under;

                if (e->data[4] && e->data[4] != x->xdrag.action)
                {
                        // The person changed what the X source asks (a key held): the Wayland drag's actions follow.
                        x->xdrag.action = e->data[4];
                        if (x->xdrag.began)
                                wayland_drag_x_actions(compositor_xwm_xdrag_actions());
                }
                if (!x->xdrag.naming && !x->xdrag.began && !x->xdrag.cancelled && !x->xdrag.dropped && !x->xdrag.escaped)
                        compositor_xwm_xdrag_begin();
                rx = compositor_xwm_from_x(rx, 0), ry = compositor_xwm_from_x(ry, 1);
                under = compositor_under(rx, ry, &tab);
                if (x->xdrag.began)
                        wayland_drag_x_motion(under, under >= 0 ? rx - compositor.x[under] : 0, under >= 0 ? ry - compositor.y[under] : 0);
                compositor_xwm_xdrag_status();
        }
        else if (e->atom == x->atoms[COMPOSITOR_XWM_A_XDND_LEAVE])
        {
                if (x->xdrag.began)
                        wayland_drag_x_leave();
                compositor_xwm_xdrag_reset();
        }
        else if (e->atom == x->atoms[COMPOSITOR_XWM_A_XDND_DROP])
        {
                x->xdrag.time = e->data[2];
                if (x->xdrag.began && !x->xdrag.escaped)
                {
                        x->xdrag.dropped = 1;
                        wayland_drag_x_drop();
                }
                else
                {
                        unsigned int data[5] = {compositor_xwm_id(COMPOSITOR_XWM_PROXY_WINDOW), 0, 0, 0, 0};

                        compositor_xwm_message32(x->xdrag.source, x->atoms[COMPOSITOR_XWM_A_XDND_FINISHED], data);
                        compositor_xwm_xdrag_reset();
                }
        }
}

// The replies of drag and drop's requests: an X window's XdndAware, an X source's XdndTypeList, the names of its types come.
static void compositor_xwm_dnd_reply(const unsigned char *in, long size, const struct compositor_xwm_pending *p)
{
        struct compositor_xwm *x = compositor_xwm_at;
        struct xwm_property_reply r;

        if (in && xwm_parse_property_reply(in, (unsigned long)size, &r) != XWM_OK)
        {
                x->broken = 1;
                return;
        }
        if (p->what == COMPOSITOR_XWM_PROP_XDND_AWARE)
        {
                compositor_xwm_wdrag_aware(in ? &r : 0, p->window);
                return;
        }
        if (p->what == COMPOSITOR_XWM_PROP_XDND_PROXY || p->what == COMPOSITOR_XWM_PROP_XDND_PROXIED)
        {
                compositor_xwm_wdrag_proxy(in ? &r : 0, p);
                return;
        }
        if (!x->xdrag.entered || p->window != x->xdrag.source)
                return;
        if (p->what == COMPOSITOR_XWM_PROP_XDND_ACTIONS)
        {
                // XdndActionList: the actions the X source offers, as the drag's bits (link and private have no Wayland action).
                x->xdrag.listed = 0;
                for (unsigned long k = 0; in && r.format == XWM_FORMAT_32 && k + 4 <= r.value_len; k += 4)
                        x->xdrag.listed |= xwm_dnd_action(xwm_get32(r.value + k), x->atoms[COMPOSITOR_XWM_A_XDND_COPY], x->atoms[COMPOSITOR_XWM_A_XDND_MOVE],
                                                          x->atoms[COMPOSITOR_XWM_A_XDND_ASK]);
                return;
        }
        if (p->what == COMPOSITOR_XWM_PROP_XDND_WORDS)
        {
                // XdndActionDescription: a word for each action of the list, each with its NUL (the Wayland target asks in its own words).
                x->xdrag.words = 0;
                for (unsigned long k = 0; in && r.format == XWM_FORMAT_8 && k < r.value_len; k++)
                        x->xdrag.words += !r.value[k];
                return;
        }
        if (p->what == COMPOSITOR_XWM_PROP_XDND_TYPES)
        {
                unsigned char req[32];

                for (unsigned long k = 0; in && r.format == XWM_FORMAT_32 && k + 4 <= r.value_len && x->xdrag.types.count < COMPOSITOR_XWM_TARGETS_MOST; k += 4)
                        if (xwm_get32(r.value + k) && wayland_table_reach(&x->xdrag.types, x->xdrag.types.count + 1))
                        {
                                ((unsigned int *)x->xdrag.types.at)[x->xdrag.types.count - 1] = xwm_get32(r.value + k);
                                compositor_xwm_name_atom(xwm_get32(r.value + k));
                        }
                compositor_xwm_wait(req, xwm_enc_get_property(req, sizeof req, 0, p->window, x->atoms[COMPOSITOR_XWM_A_XDND_TYPES], COMPOSITOR_XWM_ATOM_TYPE, 0, 0),
                                    p->window, COMPOSITOR_XWM_PROP_XDND_SYNC, 0);
                return;
        }
        x->xdrag.naming = 0;
}

static int compositor_xwm_row_is_x(int row)
{
        return row >= 0 && compositor_xwm_slot_of_row(row) != 0;
}

/* An X source's XdndActionList and XdndActionDescription asked for at its enter (before the reply that says its types are named, so
   they have come by then and its Wayland drag begins with them). */
static void compositor_xwm_xdrag_ask_actions(unsigned int source)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned char req[32];

        compositor_xwm_wait(req, xwm_enc_get_property(req, sizeof req, 0, source, x->atoms[COMPOSITOR_XWM_A_XDND_ACTIONS], COMPOSITOR_XWM_ATOM_TYPE, 0, 64),
                            source, COMPOSITOR_XWM_PROP_XDND_ACTIONS, 0);
        compositor_xwm_wait(req, xwm_enc_get_property(req, sizeof req, 0, source, x->atoms[COMPOSITOR_XWM_A_XDND_DESCRIPTIONS], COMPOSITOR_XWM_XA_STRING, 0,
                                                      1024),
                            source, COMPOSITOR_XWM_PROP_XDND_WORDS, 0);
}

/*
        Where the hand that drives an X drag is: a button held whose press went to an X window (any button: an X client may drag with any),
        at the pointer; else a finger down on an X window (Xwayland's emulated pointer follows it), at the finger. 1 with the place.
*/
static int compositor_xwm_drag_hand(int *hx, int *hy)
{
        for (int button = 0; button < 8; button++)
                if ((compositor.button_down & (1u << button)) && compositor_xwm_row_is_x(compositor.button_row[button]))
                {
                        *hx = compositor.pointer_x, *hy = compositor.pointer_y;
                        return 1;
                }
        for (int at = 0; at < COMPOSITOR_INPUTS; at++)
                for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                        if (compositor.screen_touch[at].id[slot] >= 0 && compositor_xwm_row_is_x((int)wayland_touch_row(compositor.screen_touch[at].id[slot])))
                        {
                                compositor_touch_point((unsigned int)at, slot, hx, hy);
                                return 1;
                        }
        return 0;
}

/*
        Escape while an X client drags across the bridge (compositor_wl_drag_escape, the host's one Escape for every drag): the Wayland drag
        of it ends at once (its target left), the X source is told the proxy takes nothing and its drop finishes nothing, and it is given
        the Escape itself through XTEST, as the person's key, so it ends its own drag (its grab has the keyboard). True when there was one.
*/
static int compositor_xwm_drag_escape_one(void)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned char req[48];

        if (x->fd < 0 || !x->xdrag.entered || x->xdrag.dropped || x->xdrag.escaped)
                return 0;
        x->xdrag.escaped = 1;
        if (x->xdrag.began)
                wayland_drag_x_leave();
        x->xdrag.began = 0;
        compositor_xwm_xdrag_status();
        if (x->xtest && xwm_enc_xtest_key(req, sizeof req, x->xtest, XWM_XTEST_KEY_PRESS, COMPOSITOR_XWM_KEY_ESCAPE) > 0)
        {
                compositor_xwm_queue(req, 36);
                if (xwm_enc_xtest_key(req, sizeof req, x->xtest, XWM_XTEST_KEY_RELEASE, COMPOSITOR_XWM_KEY_ESCAPE) > 0)
                        compositor_xwm_queue(req, 36);
        }
        compositor_xwm_flush();
        return 1;
}

/* The proxy over the Wayland window under the pointer while an X client drags (it owns XdndSelection and holds the left button on its
   window), on top so the X source finds it; unmapped elsewhere. Then the X source is told again what the Wayland target takes when
   that changed since it was last told. */
static void compositor_xwm_dnd_pass(void)
{
        struct compositor_xwm *x = compositor_xwm_at;
        unsigned int proxy = compositor_xwm_id(COMPOSITOR_XWM_PROXY_WINDOW);
        int want = -1, tab = 0, hx = 0, hy = 0;

        if (!x->claimed || !x->xfixes_event)
                return;
        if (x->sel[2].x_owner && compositor_xwm_drag_hand(&hx, &hy))
        {
                int under = compositor_under(hx, hy, &tab);

                if (under >= 0 && under < COMPOSITOR_ROWS && !compositor_xwm_row_is_x(under) &&
                    (wayland_row_owner(under) >= 0 || (compositor.table && compositor.table->drop_ok[under])) && compositor.w[under] > 0 &&
                    compositor.h[under] > 0)
                        want = under;
        }
        if (want >= 0 && (!x->xdrag.proxy_mapped || x->xdrag.proxy_row != want || x->xdrag.proxy_x != compositor.x[want] ||
                          x->xdrag.proxy_y != compositor.y[want] || x->xdrag.proxy_w != (int)compositor.w[want] || x->xdrag.proxy_h != (int)compositor.h[want]))
        {
                struct xwm_configure c;

                c.mask = XWM_CONFIGURE_X | XWM_CONFIGURE_Y | XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT | XWM_CONFIGURE_STACK_MODE;
                c.x = compositor_xwm_to_x(compositor.x[want], 0);
                c.y = compositor_xwm_to_x(compositor.y[want], 1);
                c.width = (unsigned int)compositor.w[want];
                c.height = (unsigned int)compositor.h[want];
                c.stack_mode = XWM_STACK_ABOVE;
                compositor_xwm_configure(proxy, &c);
                if (!x->xdrag.proxy_mapped)
                        compositor_xwm_window_op(XWM_OP_MAP_WINDOW, proxy);
                x->xdrag.proxy_mapped = 1;
                x->xdrag.proxy_row = want;
                x->xdrag.proxy_x = compositor.x[want], x->xdrag.proxy_y = compositor.y[want], x->xdrag.proxy_w = (int)c.width, x->xdrag.proxy_h = (int)c.height;
        }
        else if (want < 0 && x->xdrag.proxy_mapped)
        {
                compositor_xwm_window_op(XWM_OP_UNMAP_WINDOW, proxy);
                x->xdrag.proxy_mapped = 0;
                x->xdrag.proxy_row = -1;
        }
        if (x->xdrag.began && !x->xdrag.dropped)
        {
                unsigned long action = 0;
                int accepted = wayland_drag_x_answer(&action);
                unsigned int atom = accepted ? xwm_dnd_action_atom((unsigned int)action, x->atoms[COMPOSITOR_XWM_A_XDND_COPY], x->atoms[COMPOSITOR_XWM_A_XDND_MOVE],
                                                                   x->atoms[COMPOSITOR_XWM_A_XDND_ASK])
                                             : 0;

                if (accepted != x->xdrag.told_accept || atom != x->xdrag.told_action)
                        compositor_xwm_xdrag_status();
        }
}

/* A Wayland target's receive of an X drag's type: the X target it is converted as (the drag's best text for text, else the atom of its
   name among the drag's types), with the drop's time; 0 when the drag has no such type. */
static unsigned int compositor_xwm_drag_target(const char *mime, unsigned long length, unsigned int *time)
{
        struct compositor_xwm *x = compositor_xwm_at;
        const unsigned int *atoms = (const unsigned int *)x->xdrag.types.at;
        int kind = xwm_target_kind((const unsigned char *)mime, length);

        if (!x->xdrag.entered || !x->xdrag.source || !atoms)
                return 0;
        *time = x->xdrag.time;
        if (kind == XWM_TARGET_UTF8 || kind == XWM_TARGET_LATIN1)
                return compositor_xwm_best_text(atoms, x->xdrag.types.count);
        for (long k = 0; kind == XWM_TARGET_MIME && k < x->xdrag.types.count; k++)
        {
                unsigned long n = 0;
                const unsigned char *name = compositor_xwm_atom_name(atoms[k], &n);

                if (name && n == length && compositor_xwm_same(name, (const unsigned char *)mime, length))
                        return atoms[k];
        }
        return 0;
}

static long compositor_xwm_drag_types(const char **types)
{
        unsigned int actions = 0;

        return wayland_drag_x_types(types, &actions);
}

// A conversion of the drag's type for an X target: a Wayland source's send, or a program's copy pasted (compositor_drag_push).
static int compositor_xwm_drag_send(const char *mime, long fd)
{
        return compositor_drag.source >= 0 ? compositor_drag_push(mime, fd) : wayland_drag_x_send(mime, fd);
}

static int compositor_xwm_drag_ours(void)
{
        return compositor_xwm_at->wdrag.live;
}

// XdndSelection's owner changed: an X drag's source that is gone ends the Wayland drag of it.
static void compositor_xwm_xdnd_owner(unsigned int owner)
{
        struct compositor_xwm *x = compositor_xwm_at;

        if (owner)
                return;
        if (x->xdrag.began && !x->xdrag.dropped)
                wayland_drag_x_leave();
        if (!x->xdrag.dropped)
                compositor_xwm_xdrag_reset();
}

/* ---- the events ---- */

static void compositor_xwm_event(const struct xwm_event *e)
{
        struct compositor_xwm_window *w;

        switch (e->kind)
        {
        case XWM_EV_MAP_REQUEST:
                w = compositor_xwm_find(e->window, 1);
                if (!w)
                {
                        compositor_xwm_counted[2]++; // the window is not mapped: said in moonwater debug, not dropped unseen
                        break;
                }
                w->state |= COMPOSITOR_XWM_MAPPED;
                // The window's title and protocols are asked for, and changes of its properties are asked to be told.
                {
                        unsigned char req[32];
                        long n = xwm_enc_select_events(req, sizeof req, e->window, XWM_MASK_PROPERTY_CHANGE);

                        if (n > 0)
                                compositor_xwm_queue(req, n);
                }
                compositor_xwm_ask_title(w);
                compositor_xwm_ask(e->window, COMPOSITOR_XWM_PROP_PROTOCOLS, compositor_xwm_at->atoms[COMPOSITOR_XWM_A_PROTOCOLS], COMPOSITOR_XWM_ATOM_TYPE);
                compositor_xwm_ask(e->window, COMPOSITOR_XWM_PROP_TRANSIENT, COMPOSITOR_XWM_XA_WM_TRANSIENT_FOR, COMPOSITOR_XWM_XA_WINDOW);
                compositor_xwm_ask(e->window, COMPOSITOR_XWM_PROP_WM_HINTS, COMPOSITOR_XWM_XA_WM_HINTS, 0);
                compositor_xwm_ask(e->window, COMPOSITOR_XWM_PROP_NORMAL_HINTS, COMPOSITOR_XWM_XA_WM_NORMAL_HINTS, 0);
                compositor_xwm_ask(e->window, COMPOSITOR_XWM_PROP_STATE, compositor_xwm_at->atoms[COMPOSITOR_XWM_A_NET_STATE], COMPOSITOR_XWM_ATOM_TYPE);
                compositor_xwm_ask(e->window, COMPOSITOR_XWM_PROP_CLASS, COMPOSITOR_XWM_XA_WM_CLASS, COMPOSITOR_XWM_XA_STRING);
                if (!w->owner)
                        compositor_xwm_ask_owner(e->window);
                // The type is asked last: its reply says the others have come.
                compositor_xwm_ask(e->window, COMPOSITOR_XWM_PROP_TYPE, compositor_xwm_at->atoms[COMPOSITOR_XWM_A_TYPE], COMPOSITOR_XWM_ATOM_TYPE);
                compositor_xwm_window_op(XWM_OP_MAP_WINDOW, e->window);
                compositor_xwm_allow_commits(e->window);
                compositor_xwm_wm_state(e->window);
                // The keys: the X focus follows the window that has them (the pass), and a window the compositor gives none is given none.
                break;
        case XWM_EV_CONFIGURE_REQUEST:
        {
                struct xwm_configure c;

                w = compositor_xwm_find(e->window, 0);
                // A window made fullscreen for covering its screen that asks for another size is a window of that size again (a game
                // gone from borderless to windowed in its settings): out of fullscreen, floating at the size it asks.
                if (w && (w->state & COMPOSITOR_XWM_COVERED) && (w->state & COMPOSITOR_XWM_FULLSCREEN) && w->row >= 0 &&
                    (e->value_mask & (XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT)) == (XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT) &&
                    (e->width != compositor.w[w->row] || e->height != compositor.h[w->row]))
                {
                        w->restore_w = e->width, w->restore_h = e->height;
                        compositor_xwm_fullscreen(w, 0);
                }
                // A fullscreen window keeps the output's place and size whatever it asks.
                c.mask = e->value_mask & (XWM_CONFIGURE_X | XWM_CONFIGURE_Y | XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT |
                                          XWM_CONFIGURE_BORDER | XWM_CONFIGURE_SIBLING | XWM_CONFIGURE_STACK_MODE);
                if (w && (w->state & COMPOSITOR_XWM_FULLSCREEN))
                        c.mask &= ~(XWM_CONFIGURE_X | XWM_CONFIGURE_Y | XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT);
                if (w && w->row >= 0 && (compositor.resize == w->row || compositor.arranged[w->row] != COMPOSITOR_FLOATING))
                        c.mask &= ~(XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT);
                // The compositor places and stacks a managed window (its place and stacking order are synced from compositor.stack):
                // client X/Y and stacking requests are not forwarded so X11 hit-testing never diverges from what Moonwater draws.
                if (!w || !(w->state & COMPOSITOR_XWM_POPUPTYPE))
                        c.mask &= ~(XWM_CONFIGURE_X | XWM_CONFIGURE_Y | XWM_CONFIGURE_SIBLING | XWM_CONFIGURE_STACK_MODE);
                c.x = e->x;
                c.y = e->y;
                // A size within the output and the window's WM_NORMAL_HINTS bounds.
                c.width = compositor.width > 0 && e->width > (unsigned int)compositor.width ? (unsigned int)compositor.width : e->width;
                c.height = compositor.height > 0 && e->height > (unsigned int)compositor.height ? (unsigned int)compositor.height : e->height;
                compositor_xwm_clamp_size(w, &c.width, &c.height);
                c.border = e->border;
                c.sibling = e->sibling;
                c.stack_mode = e->detail;
                if (c.mask)
                {
                        compositor_xwm_configure(e->window, &c);
                        if (w && (c.mask & XWM_CONFIGURE_WIDTH))
                                w->width = c.width, w->sent_w = c.width;
                        if (w && (c.mask & XWM_CONFIGURE_HEIGHT))
                                w->height = c.height, w->sent_h = c.height;
                }
                // ICCCM 4.1.5: send a synthetic ConfigureNotify ONLY when the ConfigureRequest was completely refused (!c.mask)
                // so X11/Wine never receives a stale old-size ConfigureNotify right after a real ConfigureWindow resize.
                if (w && !c.mask)
                {
                        unsigned char ev[XWM_EVENT_SIZE], req[64];
                        int nx = w->row >= 0 && compositor.showing[w->row] ? compositor_xwm_to_x(compositor.x[w->row], 0) : w->x;
                        int ny = w->row >= 0 && compositor.showing[w->row] ? compositor_xwm_to_x(compositor.y[w->row], 1) : w->y;
                        unsigned int nw = w->width ? w->width : (w->row >= 0 && compositor.showing[w->row] ? compositor.w[w->row] : 1u);
                        unsigned int nh = w->height ? w->height : (w->row >= 0 && compositor.showing[w->row] ? compositor.h[w->row] : 1u);
                        long n = xwm_enc_configure_notify(ev, sizeof ev, w->id, nx, ny, nw, nh, 0, (w->state & COMPOSITOR_XWM_OVERRIDE) != 0);

                        if (n > 0 && (n = xwm_enc_send_event(req, sizeof req, 0, w->id, XWM_MASK_STRUCTURE_NOTIFY, ev, (unsigned long)n)) > 0)
                                compositor_xwm_queue(req, n);
                }
                break;
        }
        case XWM_EV_CREATE_NOTIFY:
                w = compositor_xwm_find(e->window, 1);
                if (w && e->override)
                        w->state |= COMPOSITOR_XWM_OVERRIDE;
                // A window that is not mapped is still watched for _MOONWATER_STEAM_OVERLAY (its changes, and what it has now).
                if (w && !e->override && compositor_xwm_at->atoms[COMPOSITOR_XWM_A_STEAM])
                {
                        unsigned char req[16];
                        long n = xwm_enc_select_events(req, sizeof req, e->window, XWM_MASK_PROPERTY_CHANGE);

                        if (n > 0)
                                compositor_xwm_queue(req, n);
                        compositor_xwm_ask(e->window, COMPOSITOR_XWM_PROP_STEAM, compositor_xwm_at->atoms[COMPOSITOR_XWM_A_STEAM],
                                           COMPOSITOR_XWM_ALLOW_TYPE);
                }
                if (w)
                {
                        w->x = e->x;
                        w->y = e->y;
                        w->width = e->width;
                        w->height = e->height;
                }
                break;
        case XWM_EV_MAP_NOTIFY:
                // No MapRequest comes for an override-redirect window: once it is mapped Xwayland may commit its surface.
                w = compositor_xwm_find(e->window, 0);
                if (w && e->override)
                {
                        w->state |= COMPOSITOR_XWM_OVERRIDE;
                        compositor_xwm_allow_commits(e->window);
                }
                break;
        case XWM_EV_CONFIGURE_NOTIFY:
                w = compositor_xwm_find(e->window, 0);
                if (w)
                {
                        w->x = e->x;
                        w->y = e->y;
                        w->width = e->width;
                        w->height = e->height;
                        // Wine/Proton borderless fullscreen games often map a small override-redirect window first and then resize
                        // it to cover the whole output: promote it to managed fullscreen so it hides/joins the taskbar and takes focus.
                        if ((w->state & COMPOSITOR_XWM_ADOPTED) && w->row >= 0 && w->row < COMPOSITOR_ROWS &&
                            !(w->state & COMPOSITOR_XWM_FULLSCREEN))
                        {
                                int covers = compositor_xwm_covers(w);

                                if (covers >= 0)
                                {
                                        w->restore_w = w->width, w->restore_h = w->height;
                                        w->state |= COMPOSITOR_XWM_FULLSCREEN | COMPOSITOR_XWM_COVERED;
                                        compositor_xwm_net_state(w->id, (int)w->state);
                                        if (compositor.table)
                                        {
                                                compositor.table->glass[w->row] &= ~WINDOW_NO_TAB;
                                                compositor.table->popup_parent[w->row] = 0;
                                        }
                                        compositor.asks[w->row] &= ~WINDOW_NO_TAB;
                                        compositor_popup_parent[w->row] = -1;
                                        compositor_fullscreen_on(w->row, covers);
                                        compositor_raise(w->row);
                                        compositor_focus(w->row);
                                        compositor_tabs_changed();
                                        break;
                                }
                        }
                        // A popup Xwayland moves while it is shown is adopted again: its new place is sent to the host there.
                        if ((w->state & COMPOSITOR_XWM_ADOPTED) && compositor_xwm_is_popup(w) && w->row >= 0)
                                w->dirty = 1;
                }
                break;
        case XWM_EV_PROPERTY_NOTIFY:
                // A requestor window's property is a conversion's piece; another client's, a piece it took of the window manager's answer.
                if (compositor_xwm_in_property(e))
                        break;
                compositor_xwm_out_property(e);
                w = compositor_xwm_find(e->window, 0);
                if (!w)
                        break;
                // A window's _NET_WM_NAME is its title; its WM_NAME changes what the title is only while it has no _NET_WM_NAME.
                if (e->atom == compositor_xwm_at->atoms[COMPOSITOR_XWM_A_NET_NAME] ||
                    (e->atom == COMPOSITOR_XWM_XA_WM_NAME && !(w->state & COMPOSITOR_XWM_NET_TITLE)))
                        compositor_xwm_ask_title(w);
                else if (e->atom == compositor_xwm_at->atoms[COMPOSITOR_XWM_A_PROTOCOLS])
                        compositor_xwm_ask(w->id, COMPOSITOR_XWM_PROP_PROTOCOLS, e->atom, COMPOSITOR_XWM_ATOM_TYPE);
                else if (e->atom == COMPOSITOR_XWM_XA_WM_HINTS)
                        compositor_xwm_ask(w->id, COMPOSITOR_XWM_PROP_WM_HINTS, e->atom, 0);
                else if (e->atom == COMPOSITOR_XWM_XA_WM_NORMAL_HINTS)
                        compositor_xwm_ask(w->id, COMPOSITOR_XWM_PROP_NORMAL_HINTS, e->atom, 0);
                else if (e->atom && e->atom == compositor_xwm_at->atoms[COMPOSITOR_XWM_A_STEAM])
                        compositor_xwm_ask(w->id, COMPOSITOR_XWM_PROP_STEAM, e->atom, COMPOSITOR_XWM_ALLOW_TYPE);
                else if (e->atom && e->atom == compositor_xwm_at->atoms[COMPOSITOR_XWM_A_STEAM_OPEN])
                        compositor_xwm_ask(w->id, COMPOSITOR_XWM_PROP_STEAM_OPEN, e->atom, COMPOSITOR_XWM_ALLOW_TYPE);
                break;
        case XWM_EV_UNMAP_NOTIFY:
                w = compositor_xwm_find(e->window, 0);
                if (w)
                {
                        // The surface the serial named went with the mapping (Xwayland destroys it): the next one says its own.
                        w->state &= ~(COMPOSITOR_XWM_MAPPED | COMPOSITOR_XWM_SERIAL);
                        compositor_xwm_unmapped(w);
                }
                break;
        case XWM_EV_DESTROY_NOTIFY:
                compositor_xwm_wdrag_gone(e->window);
                compositor_xwm_out_gone(e->window, 0);
                compositor_xwm_forget(e->window);
                break;
        case XWM_EV_SELECTION_REQUEST:
                compositor_xwm_selection_request(e);
                break;
        case XWM_EV_SELECTION_NOTIFY:
                compositor_xwm_in_notify(e);
                break;
        case XWM_EV_SELECTION_CLEAR:
                if (e->owner == compositor_xwm_id(COMPOSITOR_XWM_SEL_WINDOW) && compositor_xwm_which(e->selection) >= 0 &&
                    compositor_xwm_which(e->selection) < 2)
                        compositor_xwm_at->sel[compositor_xwm_which(e->selection)].owned = 0;
                break;
        case XWM_EV_CLIENT_MESSAGE:
                // XDND: the X source's messages to the proxy, and the X target's to the selection window (DRAG AND DROP).
                if (e->format == XWM_FORMAT_32 && e->window == compositor_xwm_id(COMPOSITOR_XWM_PROXY_WINDOW) && compositor_xwm_at->claimed)
                {
                        compositor_xwm_proxy_message(e);
                        break;
                }
                if (e->format == XWM_FORMAT_32 && e->window == compositor_xwm_id(COMPOSITOR_XWM_SEL_WINDOW) && compositor_xwm_at->claimed)
                {
                        compositor_xwm_wdrag_message(e);
                        break;
                }
                if (e->atom == compositor_xwm_at->atoms[COMPOSITOR_XWM_A_SERIAL])
                {
                        w = compositor_xwm_find(e->window, 1);
                        if (!w)
                                break;
                        // Another serial is another surface, and so another mapping, whether or not its unmap was heard first.
                        if (!(w->state & COMPOSITOR_XWM_SERIAL) || w->serial != (((unsigned long long)e->data[1] << 32) | e->data[0]))
                                compositor_xwm_unmapped(w);
                        w->serial = ((unsigned long long)e->data[1] << 32) | e->data[0];
                        w->state |= COMPOSITOR_XWM_SERIAL;
                        compositor_xwm_resolve(w);
                }
                else if (e->atom == compositor_xwm_at->atoms[COMPOSITOR_XWM_A_NET_STATE])
                {
                        unsigned int fullscreen = compositor_xwm_at->atoms[COMPOSITOR_XWM_A_FULLSCREEN];
                        unsigned int max_v = compositor_xwm_at->atoms[COMPOSITOR_XWM_A_MAX_VERT];
                        unsigned int max_h = compositor_xwm_at->atoms[COMPOSITOR_XWM_A_MAX_HORZ];
                        unsigned int hidden = compositor_xwm_at->atoms[COMPOSITOR_XWM_A_HIDDEN];

                        w = compositor_xwm_find(e->window, 0);
                        if (!w)
                                break;
                        if (fullscreen && (e->data[1] == fullscreen || e->data[2] == fullscreen))
                        {
                                if (e->data[0] == COMPOSITOR_XWM_NET_ADD)
                                        compositor_xwm_fullscreen(w, 1);
                                else if (e->data[0] == COMPOSITOR_XWM_NET_REMOVE)
                                        compositor_xwm_fullscreen(w, 0);
                                else if (e->data[0] == COMPOSITOR_XWM_NET_TOGGLE)
                                        compositor_xwm_fullscreen(w, !(w->state & COMPOSITOR_XWM_FULLSCREEN));
                        }
                        if ((max_v && (e->data[1] == max_v || e->data[2] == max_v)) ||
                            (max_h && (e->data[1] == max_h || e->data[2] == max_h)))
                        {
                                int on = e->data[0] == COMPOSITOR_XWM_NET_ADD      ? 1
                                         : e->data[0] == COMPOSITOR_XWM_NET_REMOVE ? 0
                                                                                   : !(w->state & COMPOSITOR_XWM_MAXIMIZED);
                                if (on != !!(w->state & COMPOSITOR_XWM_MAXIMIZED))
                                {
                                        if (on)
                                        {
                                                if (!(w->state & (COMPOSITOR_XWM_FULLSCREEN | COMPOSITOR_XWM_MAXIMIZED)))
                                                {
                                                        w->restore_w = w->width;
                                                        w->restore_h = w->height;
                                                }
                                                w->state |= COMPOSITOR_XWM_MAXIMIZED;
                                                if (w->row >= 0 && !(w->state & COMPOSITOR_XWM_FULLSCREEN))
                                                        compositor_arrange(w->row, COMPOSITOR_ROOM);
                                                compositor_xwm_net_state(w->id, (int)w->state);
                                        }
                                        else
                                        {
                                                w->state &= ~COMPOSITOR_XWM_MAXIMIZED;
                                                compositor_xwm_net_state(w->id, (int)w->state);
                                                if (!(w->state & COMPOSITOR_XWM_FULLSCREEN))
                                                {
                                                        if (w->row >= 0)
                                                                compositor_float(w->row);
                                                        if (w->restore_w && w->restore_h)
                                                        {
                                                                struct xwm_configure c;

                                                                c.mask = XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT;
                                                                c.width = w->restore_w;
                                                                c.height = w->restore_h;
                                                                compositor_xwm_configure(w->id, &c);
                                                                w->sent_w = w->restore_w;
                                                                w->sent_h = w->restore_h;
                                                        }
                                                }
                                        }
                                }
                        }
                        if (hidden && (e->data[1] == hidden || e->data[2] == hidden) &&
                            w->row >= 0 && w->row < COMPOSITOR_ROWS && compositor.showing[w->row] && !(w->state & COMPOSITOR_XWM_POPUPISH))
                        {
                                int on = e->data[0] == COMPOSITOR_XWM_NET_ADD      ? 1
                                         : e->data[0] == COMPOSITOR_XWM_NET_REMOVE ? 0
                                                                                   : !(compositor.state[w->row] & WINDOW_MINIMIZED);
                                if (on)
                                        compositor_minimize_row(w->row);
                                else
                                        compositor_reveal(w->row);
                        }
                }
                else if (e->atom == compositor_xwm_at->atoms[COMPOSITOR_XWM_A_NET_MOVERESIZE] && e->format == XWM_FORMAT_32)
                        compositor_xwm_moveresize(compositor_xwm_find(e->window, 0), e->data[2]);
                else if (e->atom && e->atom == compositor_xwm_at->atoms[COMPOSITOR_XWM_A_NET_ACTIVE])
                {
                        w = compositor_xwm_find(e->window, 0);
                        if (w && w->row >= 0 && w->row < COMPOSITOR_ROWS && compositor.showing[w->row] && !(w->state & COMPOSITOR_XWM_POPUPISH))
                                compositor_reveal(w->row);
                }
                else if (e->atom && e->atom == compositor_xwm_at->atoms[COMPOSITOR_XWM_A_NET_CLOSE])
                {
                        w = compositor_xwm_find(e->window, 0);
                        if (w && w->row >= 0 && w->row < COMPOSITOR_ROWS && compositor.showing[w->row])
                                compositor_close(w->row);
                }
                else if (e->atom && e->atom == compositor_xwm_at->atoms[COMPOSITOR_XWM_A_CHANGE_STATE] &&
                         e->format == XWM_FORMAT_32 && e->data[0] == COMPOSITOR_XWM_STATE_ICONIC)
                {
                        w = compositor_xwm_find(e->window, 0);
                        if (w && w->row >= 0 && w->row < COMPOSITOR_ROWS && compositor.showing[w->row] && !(w->state & COMPOSITOR_XWM_POPUPISH))
                                compositor_minimize_row(w->row);
                }
                break;
        default:
                break;
        }
}

/* An error the server sent: its sequence says which request it answers. The first request is the root's selection, and a
   refusal of that is another window manager already running: the connection is dropped. Others are counted and go on. */
static void compositor_xwm_error(const unsigned char *in)
{
        struct xwm_error err;

        if (xwm_parse_error(in, XWM_HEADER, &err) != XWM_OK)
        {
                compositor_xwm_at->broken = 1;
                return;
        }
        compositor_xwm_at->errors++;
        if (err.sequence == 1)
        {
                compositor_xwm_drop("the server refused the root's events (another window manager runs)");
                return;
        }
        // A property request that is refused (its window is gone) is answered: a title it was asked for is no longer waited for.
        if (compositor_xwm_at->pending_count && compositor_xwm_at->pending[compositor_xwm_at->pending_at].seq == err.sequence)
        {
                struct compositor_xwm_pending failed = compositor_xwm_at->pending[compositor_xwm_at->pending_at];
                struct compositor_xwm_window *w;

                compositor_xwm_pending_done();
                w = compositor_xwm_find(failed.window, 0);
                if (w && (failed.what == COMPOSITOR_XWM_PROP_NET_NAME || failed.what == COMPOSITOR_XWM_PROP_NAME))
                        compositor_xwm_title_done(w);
                else if (w && failed.what == COMPOSITOR_XWM_PROP_PROTOCOLS)
                        w->state |= COMPOSITOR_XWM_PROTO_KNOWN;
                else if (w && failed.what == COMPOSITOR_XWM_PROP_TYPE)
                        w->state |= COMPOSITOR_XWM_PROPS_READ;
                // A name with no atom is never one again; an atom with no name has none (no type of it is offered).
                else if (failed.what == COMPOSITOR_XWM_PROP_INTERN && (long)failed.slot < compositor_xwm_at->atom_table.count)
                        compositor_xwm_at->atom[failed.slot].length = 0;
                else if (failed.what == COMPOSITOR_XWM_PROP_ATOM_NAME && (long)failed.slot < compositor_xwm_at->atom_table.count)
                        compositor_xwm_at->atom[failed.slot].known = 1;
                else if ((failed.what == COMPOSITOR_XWM_PROP_SEL || failed.what == COMPOSITOR_XWM_PROP_SEL_SYNC) &&
                         compositor_xwm_in_at((long)failed.slot) && compositor_xwm_at->ins[failed.slot].window == failed.window)
                        compositor_xwm_in_end((long)failed.slot, 0);
                // An X window gone under a drag has no XdndAware; an X source's types that cannot be read are the ones its enter named.
                else if (COMPOSITOR_XWM_DND_REPLY(failed.what))
                        compositor_xwm_dnd_reply(0, 0, &failed);
                else if (failed.what == COMPOSITOR_XWM_PROP_MULTIPLE)
                        compositor_xwm_multiple_reply(0, 0, &failed);
        }
}

/* One message the server sent, whole, at the start of the input: a reply, an error or an event. */
static void compositor_xwm_message(const unsigned char *in, long size, long type)
{
        struct compositor_xwm *x = compositor_xwm_at;

        if (type == XWM_ERROR)
        {
                compositor_xwm_error(in);
                return;
        }
        if (type == XWM_REPLY && x->pending_count && x->pending[x->pending_at].seq == xwm_get16(in + 2))
        {
                struct compositor_xwm_pending found = x->pending[x->pending_at];

                compositor_xwm_pending_done();
                if (found.what == COMPOSITOR_XWM_PROP_OWNER)
                {
                        struct compositor_xwm_window *w = compositor_xwm_find(found.window, 0);
                        unsigned int pid = 0;

                        if (xwm_parse_xres_client_pid(in, (unsigned long)size, &pid) != XWM_OK)
                        {
                                x->broken = 1;
                                return;
                        }
                        if (w && pid > 1 && pid != (unsigned int)x->peer_pid &&
                            pid != (unsigned int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0))
                        {
                                w->owner = pid;
                                compositor_xwm_owned(w);
                        }
                        return;
                }
                if (found.what == COMPOSITOR_XWM_PROP_INTERN)
                        compositor_xwm_interned(in, size, &found);
                else if (found.what == COMPOSITOR_XWM_PROP_ATOM_NAME)
                        compositor_xwm_named(in, size, &found);
                else if (found.what == COMPOSITOR_XWM_PROP_SEL || found.what == COMPOSITOR_XWM_PROP_SEL_SYNC)
                        compositor_xwm_in_reply(in, size, &found);
                else if (COMPOSITOR_XWM_DND_REPLY(found.what))
                        compositor_xwm_dnd_reply(in, size, &found);
                else if (found.what == COMPOSITOR_XWM_PROP_MULTIPLE)
                        compositor_xwm_multiple_reply(in, size, &found);
                else
                        compositor_xwm_property(in, size, &found);
                return;
        }
        if (type == XWM_REPLY && xwm_get16(in + 2) == COMPOSITOR_XWM_COMPOSITE_SEQ)
        {
                compositor_xwm_composite(in, size);
                return;
        }
        if (type == XWM_REPLY && xwm_get16(in + 2) == COMPOSITOR_XWM_XRES_SEQ)
        {
                compositor_xwm_xres(in, size);
                return;
        }
        if (type == XWM_REPLY && xwm_get16(in + 2) == COMPOSITOR_XWM_XFIXES_SEQ)
        {
                compositor_xwm_xfixes(in, size);
                return;
        }
        if (type == XWM_REPLY && xwm_get16(in + 2) == COMPOSITOR_XWM_XTEST_SEQ)
        {
                struct xwm_extension_reply r;

                if (xwm_parse_extension_reply(in, (unsigned long)size, &r) != XWM_OK)
                        x->broken = 1;
                else if (r.present && r.major_opcode >= XWM_EXT_MAJOR_FIRST)
                        x->xtest = r.major_opcode;
                return;
        }
        if (type == XWM_REPLY)
        {
                struct xwm_atom_reply atom;
                unsigned int at = (xwm_get16(in + 2) + 0x10000u - COMPOSITOR_XWM_ATOM_FIRST_SEQ) & 0xffffu;

                // Only while the atoms are awaited: a reply nothing waits for, once the sequence has gone round, is no atom's.
                if (at >= COMPOSITOR_XWM_ATOMS || x->atoms_seen >= COMPOSITOR_XWM_ATOMS)
                        return;
                if (xwm_parse_atom_reply(in, (unsigned long)size, &atom) != XWM_OK)
                {
                        x->broken = 1;
                        return;
                }
                if (!x->atoms[at])
                        x->atoms_seen++;
                x->atoms[at] = atom.atom;
                if (x->atoms_seen == COMPOSITOR_XWM_ATOMS)
                        compositor_xwm_claim();
                return;
        }
        if (x->xfixes_event && (in[0] & XWM_TYPE_MASK) == x->xfixes_event)
        {
                struct xwm_event e;

                if (xwm_parse_xfixes_selection(in, (unsigned long)size, x->xfixes_event, &e) != XWM_OK)
                {
                        x->broken = 1;
                        return;
                }
                compositor_xwm_owner(&e);
                return;
        }
        {
                struct xwm_event e;

                if (xwm_parse_event(in, (unsigned long)size, &e) != XWM_OK)
                {
                        x->broken = 1;
                        return;
                }
                compositor_xwm_event(&e);
        }
}

/* Consumes n bytes from the front of the input. */
static void compositor_xwm_consume(long n)
{
        struct compositor_xwm *x = compositor_xwm_at;

        if (n > 0 && x->in_len > n)
                memory_copy(x->in, x->in + n, (positive)(x->in_len - n));
        x->in_len -= n;
}

/* Every whole message in the input, in order: the setup reply first, then the messages. A message the codec refuses drops the
   connection, so nothing a server sends is read past its bounds. The messages answered are taken off the input once, at the end
   (a burst of them is one move of the input, not one move of the rest of it for each message). */
static void compositor_xwm_messages(void)
{
        struct compositor_xwm *x = compositor_xwm_at;
        long at = 0;

        while (x->fd >= 0 && !x->broken)
        {
                long size, type;

                if (!x->ready)
                {
                        struct xwm_setup setup;

                        size = xwm_setup_size(x->in + at, (unsigned long)(x->in_len - at));
                        if (size == XWM_ERR_SHORT)
                                break;
                        if (size < 0)
                        {
                                compositor_xwm_drop("the setup reply is too long");
                                return;
                        }
                        if (x->in_len - at < size)
                                break;
                        if (xwm_parse_setup(x->in + at, (unsigned long)size, &setup) != XWM_OK || setup.status != XWM_SETUP_SUCCESS)
                        {
                                string_format(log, (string_address)"xwm: the setup reply has status %p and %p bytes, and the codec says %p\n",
                                              (positive)x->in[at], (positive)size, (positive)xwm_parse_setup(x->in + at, (unsigned long)size, &setup));
                                log_flush();
                                compositor_xwm_drop("the setup was refused or is malformed");
                                return;
                        }
                        x->root = setup.root;
                        x->resource_base = setup.resource_base;
                        x->resource_mask = setup.resource_mask;
                        x->ready = 1;
                        at += size;
                        compositor_xwm_start_session();
                        continue;
                }
                type = xwm_message_type(x->in + at, (unsigned long)(x->in_len - at));
                if (type == XWM_ERR_SHORT)
                        break;
                size = xwm_server_message_size(x->in + at, (unsigned long)(x->in_len - at));
                if (size < 0)
                {
                        compositor_xwm_drop("a reply is longer than the window manager takes");
                        return;
                }
                if (x->in_len - at < size)
                        break;
                // A server that sends faster than the queue to it drains is taken a pass at a time: the rest waits in the input.
                if (x->out_len > COMPOSITOR_XWM_OUT - COMPOSITOR_XWM_ROOM)
                        break;
                compositor_xwm_message(x->in + at, size, type);
                if (x->fd < 0)
                        return;
                at += size;
        }
        compositor_xwm_consume(at);
}

/* ---- the connection ---- */

static void compositor_xwm_close_rows(void)
{
        struct compositor_xwm *x = compositor_xwm_at;

        // The windows the connection made are taken away, and their surfaces are shown no more (those of an Xwayland that has died
        // are gone with it, and the shim finds none).
        for (long at = 0; at < x->window_table.count; at++)
        {
                struct compositor_xwm_window *w = &x->window[at];

                if (w->id && (w->state & COMPOSITOR_XWM_SERIAL))
                        wayland_xwindow_gone(compositor_xwm_server(x), w->serial);
                compositor_xwm_release(w);
        }
        x->pending_at = x->pending_count = 0;
        x->focus_row = -2;
        // The conversions go with the connection (a pipe's pump reads its end), and what X offered with a selection.
        for (long at = 0; at < x->in_table.count; at++)
                if (x->ins[at].window)
                {
                        x->ins[at].eager = 0;
                        if (x->ins[at].fd >= 0)
                                system_close(x->ins[at].fd);
                        x->ins[at].fd = -1;
                        wayland_table_drop(&x->ins[at].targets);
                        x->ins[at].window = 0;
                }
        for (long at = 0; at < x->out_table.count; at++)
                if (x->outs[at].requestor && x->outs[at].fd >= 0)
                        system_close(x->outs[at].fd);
        wayland_table_drop(&x->in_table);
        wayland_table_drop(&x->out_table);
        wayland_table_drop(&x->atom_table);
        wayland_table_drop(&x->atom_index);
        wayland_table_drop(&x->name_table);
        wayland_table_drop(&x->type_table);
        x->ins = 0, x->outs = 0, x->atom = 0, x->name_bytes = 0, x->atoms_indexed = 0;
        for (int which = 0; which < 3; which++)
        {
                x->sel[which].atom = x->sel[which].owned_time = x->sel[which].x_owner = 0;
                x->sel[which].owned = x->sel[which].dirty = 0;
                x->sel[which].reading = -1;
        }
        x->xfixes_opcode = x->xfixes_event = x->setting = 0;
        x->save.requestor = 0;
        // A drag across the bridge ends with it.
        if (x->xdrag.began && !x->xdrag.dropped)
                wayland_drag_x_leave();
        if (x->wdrag.live && x->wdrag.dropped)
                wayland_drag_x_finished(0);
        else if (x->wdrag.live)
                wayland_drag_x_status(0, 0);
        x->wdrag.live = x->wdrag.entered = x->wdrag.dropped = 0, x->wdrag.target = 0;
        compositor_xwm_xdrag_reset();
        wayland_table_drop(&x->xdrag.types);
        x->xdrag.proxy_mapped = 0, x->xdrag.proxy_row = -1, x->xdrag.cancelled = 0;
}

static void compositor_xwm_drop(const char *why)
{
        struct compositor_xwm *x = compositor_xwm_at;

        if (x->fd < 0)
                return;
        compositor_xwm_counted[3]++;
        compositor_xwm_log("xwm: the window manager connection is closed: ");
        compositor_xwm_log(why);
        compositor_xwm_log("\n");
        log_flush();
        system_close(x->fd);
        x->fd = -1;
        compositor_xwm_close_rows();
        x->ready = 0;
        x->broken = 0;
        x->in_len = 0;
        x->out_len = 0;
        x->seq = 0;
        x->atoms_seen = 0;
        x->claimed = 0;
        x->composite = 0;
        for (unsigned int at = 0; at < COMPOSITOR_XWM_ATOMS; at++)
                x->atoms[at] = 0;
}

/* The setup request goes out first, before anything else is read: it is the only message the window manager sends unasked. */
static void compositor_xwm_accept(void)
{
        int credentials[3] = {0, 0, 0};
        unsigned int size = sizeof credentials;
        long fd;
        unsigned char setup[XWM_SETUP_REQUEST];
        long n;
        struct compositor_xwm *x = 0;

        // SOCK_NONBLOCK | SOCK_CLOEXEC
        fd = compositor_call(syscall(accept4), (positive)compositor_xwm_listen_fd, 0, 0, 0x800 | 0x80000, 0, 0);
        if (fd < 0)
                return;
        // The credentials first (E-h9-1): a peer that is refused never takes a live server's place, whatever pid it has.
        if (compositor_call(syscall(getsockopt), (positive)fd, 1, COMPOSITOR_XWM_SO_PEERCRED, (positive)credentials, (positive)&size, 0) < 0)
                credentials[0] = 0;
        if (credentials[0] > 0 && (credentials[1] == compositor_xwm_uid || (compositor_xwm_uid == 0 && credentials[1] == 1000)))
                // One window manager for each wrapper: its new connection (the next display tried) takes the old one's place.
                for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                        if (compositor_xwm_slots[at].fd >= 0 && compositor_xwm_slots[at].peer_pid == credentials[0])
                        {
                                compositor_xwm_at = &compositor_xwm_slots[at];
                                compositor_xwm_drop("its wrapper connected again");
                        }
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS && !x; at++)
                if (compositor_xwm_slots[at].fd < 0)
                        x = &compositor_xwm_slots[at];
        if (credentials[0] <= 0 || (credentials[1] != compositor_xwm_uid && !(compositor_xwm_uid == 0 && credentials[1] == 1000)) || !x)
        {
                // Every slot is an X server's already: the connection is refused (counted on the first slot).
                if (!x)
                        compositor_xwm_slots[0].refused++;
                compositor_xwm_counted[0]++;
                system_close((int)fd);
                return;
        }
        compositor_xwm_at = x;
        *x = compositor_xwm_fresh;
        x->fd = (int)fd;
        x->peer_pid = credentials[0];
        x->since = clock_monotonic_nanoseconds();
        n = xwm_enc_setup_request(setup, sizeof setup);
        if (n > 0)
        {
                memory_copy(x->out, setup, (positive)n);
                x->out_len = n;
        }
        compositor_xwm_log("xwm: the window manager of Xwayland is connected\n");
        log_flush();
}

static void compositor_xwm_flush(void)
{
        struct compositor_xwm *x = compositor_xwm_at;
        long got;

        if (x->fd < 0 || x->out_len == 0)
                return;
        // MSG_DONTWAIT | MSG_NOSIGNAL
        got = compositor_call(syscall(sendto), (positive)x->fd, (positive)x->out, (positive)x->out_len,
                              COMPOSITOR_XWM_MSG_DONTWAIT | COMPOSITOR_XWM_MSG_NOSIGNAL, 0, 0);
        if (got == COMPOSITOR_XWM_EAGAIN)
                return;
        if (got <= 0)
        {
                compositor_xwm_drop("the write failed");
                return;
        }
        if (x->out_len > got)
                memory_copy(x->out, x->out + got, (positive)(x->out_len - got));
        x->out_len -= got;
}

/* What the server has sent, a bounded amount of it: each read is taken whole into the input and its messages are answered. */
static void compositor_xwm_read(void)
{
        struct compositor_xwm *x = compositor_xwm_at;

        for (int reads = 0; reads < 4 && x->fd >= 0 && !x->broken && x->out_len <= COMPOSITOR_XWM_OUT - COMPOSITOR_XWM_ROOM; reads++)
        {
                long got;

                if (x->in_len >= COMPOSITOR_XWM_IN)
                {
                        compositor_xwm_drop("the input is full");
                        return;
                }
                // MSG_DONTWAIT
                got = compositor_call(syscall(recvfrom), (positive)x->fd, (positive)(x->in + x->in_len),
                                      (positive)(COMPOSITOR_XWM_IN - x->in_len), COMPOSITOR_XWM_MSG_DONTWAIT, 0, 0);
                if (got == COMPOSITOR_XWM_EAGAIN)
                        return;
                if (got <= 0)
                {
                        compositor_xwm_drop("the connection ended");
                        return;
                }
                x->in_len += got;
                compositor_xwm_messages();
        }
}

/* ---- the socket and the loop ---- */

static void compositor_xwm_start(void)
{
        unsigned char address[110];
        long length = 0, got;

        if (compositor_xwm_listen_fd >= 0)
                return;
        memory_zero(address, sizeof address);
        address[0] = 1; // AF_UNIX
        while (COMPOSITOR_XWM_PATH[length])
        {
                address[2 + length] = (unsigned char)COMPOSITOR_XWM_PATH[length];
                length++;
        }
        // Pre-create /tmp/.X11-unix under the compositor's identity (root on default Moonwater) so Xwayland does not warn about user ownership.
        if (compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)"/tmp", 01777, 0, 0, 0) >= 0)
                compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)"/tmp", 01777, 0, 0, 0);
        if (compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)"/tmp/.X11-unix", 01777, 0, 0, 0) >= 0)
                compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)"/tmp/.X11-unix", 01777, 0, 0, 0);
        compositor_xwm_uid = (int)compositor_call(syscall(getuid), 0, 0, 0, 0, 0, 0);
        // Directory 0711 stops other users listing it while letting uid/gid 1000 traverse to wm-0 (mode 0660, group 1000 when uid 0),
        // and an old socket at the path is taken away (a compositor that died left it).
        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_DIR, 0711, 0, 0, 0);
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_DIR, 0711, 0, 0, 0);
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_PATH, 0, 0, 0, 0);
        // SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC
        got = compositor_call(syscall(socket), 1, 1 | 0x800 | 0x80000, 0, 0, 0, 0);
        if (got < 0)
                return;
        compositor_xwm_listen_fd = (int)got;
        if (compositor_call(syscall(bind), (positive)got, (positive)address, (positive)(2 + length + 1), 0, 0, 0) < 0 ||
            compositor_call(syscall(listen), (positive)got, COMPOSITOR_XWM_BACKLOG, 0, 0, 0, 0) < 0)
        {
                system_close(compositor_xwm_listen_fd);
                compositor_xwm_listen_fd = -1;
                compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_PATH, 0, 0, 0, 0);
                return;
        }
        if (compositor_xwm_uid == 0)
                compositor_call(syscall(fchownat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_PATH,
                                (positive)(unsigned int)-1, (positive)1000u, 0, 0);
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_PATH, 0660, 0, 0, 0);
        compositor_xwm_log("xwm: listening on " COMPOSITOR_XWM_PATH "\n");
        log_flush();
}

static void compositor_xwm_stop(void)
{
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                if (compositor_xwm_slots[at].fd >= 0)
                {
                        compositor_xwm_at = &compositor_xwm_slots[at];
                        compositor_xwm_drop("the compositor stops");
                }
        compositor_xwm_at = &compositor_xwm_slots[0];
        if (compositor_xwm_listen_fd >= 0)
                system_close(compositor_xwm_listen_fd);
        compositor_xwm_listen_fd = -1;
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_PATH, 0, 0, 0, 0);
}

/* The two polls of the window manager: its listening socket and its connection, at the end of the wayland polls. */
static unsigned int compositor_xwm_poll(struct compositor_poll *wanted, unsigned int count, unsigned int base)
{
        compositor_xwm_listen_at = -1;
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                compositor_xwm_slots[at].poll_at = -1;
        if (compositor_xwm_listen_fd < 0)
                return count;
        compositor_xwm_listen_at = (int)(count - base);
        wanted[count++] = (struct compositor_poll){compositor_xwm_listen_fd, COMPOSITOR_POLLIN, 0};
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
        {
                struct compositor_xwm *x = &compositor_xwm_slots[at];

                if (x->fd < 0)
                        continue;
                x->poll_at = (int)(count - base);
                wanted[count++] = (struct compositor_poll){x->fd, (short)(COMPOSITOR_POLLIN | (x->out_len ? COMPOSITOR_POLLOUT : 0)), 0};
        }
        return count;
}

/* One pass: a connection accepted, what the server sent answered, what it asks of the rows, and the connection timed out. */
/* One connection's pass (compositor_xwm_pass runs it for each slot): what its server sent answered, what it asks of the rows. */
static void compositor_xwm_pass_one(const struct compositor_poll *wanted)
{
        struct compositor_xwm *x = compositor_xwm_at;
        short returned;

        if (x->poll_at < 0)
                return;
        returned = wanted[x->poll_at].returned;
        // The setup request, queued at the accept, goes out before anything is waited for.
        compositor_xwm_flush();
        if (x->fd >= 0 && (returned & COMPOSITOR_POLLIN))
                compositor_xwm_read();
        else if (x->fd >= 0 && (returned & (COMPOSITOR_POLLHUP | COMPOSITOR_POLLERR)))
                compositor_xwm_drop("the connection hung up");
        if (x->fd >= 0 && x->broken)
                compositor_xwm_drop("a message the codec refused, or a queue that overflowed");
        if (x->fd >= 0 && !x->ready && clock_monotonic_nanoseconds() - x->since > COMPOSITOR_XWM_SETUP_NS)
                compositor_xwm_drop("the setup did not finish in time");
        if (x->fd >= 0 && clock_monotonic_nanoseconds() - x->since > COMPOSITOR_XWM_BIND_NS && compositor_xwm_server(x) < 0)
                compositor_xwm_drop("no X server of its wrapper is a Wayland client");
        if (x->fd < 0 || !x->ready)
                return;
        // What a drained queue lets be taken of the input.
        compositor_xwm_flush();
        compositor_xwm_messages();
        if (x->fd >= 0 && x->broken)
        {
                compositor_xwm_drop("a message the codec refused, or a queue that overflowed");
                return;
        }
        compositor_xwm_selections_pass();
        compositor_xwm_dnd_pass();
        for (long at = 0; at < x->window_table.count && x->fd >= 0 && !x->broken; at++)
        {
                struct compositor_xwm_window *w = &x->window[at];

                if (!w->id)
                        continue;
                // The row is the serial's surface's, asked each pass and not remembered: a row whose surface went is not this window's.
                if (w->row >= 0 && (!(w->state & COMPOSITOR_XWM_SERIAL) || wayland_serial_row(compositor_xwm_server(x), w->serial) != w->row))
                        compositor_xwm_unmapped(w);
                compositor_xwm_resolve(w);
                if (w->row < 0 || w->row >= COMPOSITOR_ROWS)
                        continue;
                // The compositor asked this window to go: a popup is unmapped, a window is asked by WM_DELETE_WINDOW on the first ask
                // when its WM_PROTOCOLS lists it, and escalated to DestroyWindow (plus SIGTERM/SIGKILL to its owner PID if unresponsive)
                // on repeated Alt+F4 / close asks so hung Wine/Proton/X11 games can always be closed.
                if (compositor_xwm_close_owed(w))
                {
                        w->closes = compositor.close_asks[w->row];
                        if (w->state & COMPOSITOR_XWM_POPUPISH)
                                compositor_xwm_window_op(XWM_OP_UNMAP_WINDOW, w->id);
                        else if ((w->protocols & COMPOSITOR_XWM_PROTO_DELETE) && w->closes < 2)
                                compositor_xwm_protocol(w->id, x->atoms[COMPOSITOR_XWM_A_DELETE]);
                        else
                        {
                                compositor_xwm_window_op(XWM_OP_DESTROY_WINDOW, w->id);
                                if (w->closes >= 2 && w->owner > 1)
                                        compositor_call(syscall(kill), (positive)w->owner, w->closes >= 3 ? 9 : 15, 0, 0, 0, 0);
                        }
                }
                if (!compositor_xwm_is_popup(w))
                        compositor_xwm_sync(w);
                // A dialog that is transient for a window is over it when it is shown, and takes the keys from it if it has them.
                if (w->transient && !(w->state & COMPOSITOR_XWM_RAISED))
                {
                        struct compositor_xwm_window *parent = compositor_xwm_find(w->transient, 0);

                        if (parent && parent->row >= 0 && parent->row < COMPOSITOR_ROWS)
                        {
                                w->state |= COMPOSITOR_XWM_RAISED;
                                compositor_raise(w->row);
                                if ((int)compositor.focus == parent->row)
                                        compositor_focus(w->row);
                        }
                }
        }
        if (x->fd >= 0 && x->broken)
        {
                compositor_xwm_drop("a message the codec refused, or a queue that overflowed");
                return;
        }
        // The window that has the keys has the X focus (and is told, if it asked to be: WM_TAKE_FOCUS): when the row with the keys is
        // another, and when it is another X window's (a row let go of and taken again by a window mapped after, whose focus was never set).
        {
                unsigned int now_window = 0;

                for (long at = 0; at < x->window_table.count && x->fd >= 0; at++)
                        if (x->window[at].id && x->window[at].row >= 0 && x->window[at].row == (int)compositor.focus &&
                            !compositor_xwm_is_popup(&x->window[at]))
                                now_window = x->window[at].id;
                if (now_window != x->focus_window)
                        x->focus_row = -3;
                x->focus_window = now_window;
        }
        if (x->fd >= 0 && (int)compositor.focus != x->focus_row)
        {
                int focused = 0;

                x->focus_row = (int)compositor.focus;
                for (long at = 0; at < x->window_table.count; at++)
                {
                        struct compositor_xwm_window *w = &x->window[at];

                        if (w->id && w->row >= 0 && w->row == x->focus_row && !compositor_xwm_is_popup(w))
                        {
                                if (!w->no_input)
                                        compositor_xwm_focus(w->id);
                                else if (w->protocols & COMPOSITOR_XWM_PROTO_FOCUS)
                                        compositor_xwm_active(w->id);
                                if (w->protocols & COMPOSITOR_XWM_PROTO_FOCUS)
                                        compositor_xwm_protocol(w->id, x->atoms[COMPOSITOR_XWM_A_TAKE_FOCUS]);
                                if (!w->no_input || (w->protocols & COMPOSITOR_XWM_PROTO_FOCUS))
                                        focused = 1;
                        }
                }
                if (!focused)
                        compositor_xwm_focus(0);
                // The window that took the keys has its transient dialogs over it, as it had them.
                for (long at = 0; at < x->window_table.count; at++)
                {
                        struct compositor_xwm_window *w = &x->window[at], *parent;

                        if (w->id && w->transient && w->row >= 0 && (w->state & COMPOSITOR_XWM_RAISED) &&
                            (parent = compositor_xwm_find(w->transient, 0)) && parent->row >= 0 && parent->row == x->focus_row)
                                compositor_raise(w->row);
                }
        }
        // Keep X11 stacking order aligned with compositor.stack so XWayland hit-testing matches what Moonwater draws.
        if (x->fd >= 0 && x->last_stack_gen != compositor.stack_gen)
        {
                x->last_stack_gen = compositor.stack_gen;
                for (unsigned int s = 0; s < compositor.stacked; s++)
                {
                        unsigned int row = compositor.stack[s];

                        for (long at = 0; at < x->window_table.count; at++)
                        {
                                struct compositor_xwm_window *w = &x->window[at];

                                if (w->id && w->row == (int)row)
                                {
                                        struct xwm_configure sc = {0};

                                        sc.mask = XWM_CONFIGURE_STACK_MODE;
                                        sc.stack_mode = XWM_STACK_ABOVE;
                                        compositor_xwm_configure(w->id, &sc);
                                }
                        }
                }
        }
        compositor_xwm_flush();
}

/* The loop's pass: a connection accepted, then each live connection's pass on its own slot. */
static void compositor_xwm_pass(const struct compositor_poll *wanted)
{
        if (compositor_xwm_listen_at >= 0 && (wanted[compositor_xwm_listen_at].returned & COMPOSITOR_POLLIN))
                compositor_xwm_accept();
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
        {
                struct compositor_xwm *x = &compositor_xwm_slots[at];

                if (x->fd < 0 || x->poll_at < 0)
                        continue;
                compositor_xwm_at = x;
                compositor_xwm_pass_one(wanted);
        }
        compositor_xwm_at = &compositor_xwm_slots[0];
        if (compositor.table)
                for (unsigned int k = 0; k < 4; k++)
                        compositor.table->xwm_counts[k] = compositor_xwm_counted[k];
}

/* ---- which Wayland client is this X server ---- */

// The parent of a process (the pid after the command name's closing parenthesis, the field after the state), or -1.
static long compositor_xwm_ppid(long pid)
{
        char path[48], text[512];
        long at = 0, handle, got, digits, value = 0;
        static const char prefix[] = "/proc/";
        static const char suffix[] = "/stat";

        for (long k = 0; prefix[k]; k++)
                path[at++] = prefix[k];
        {
                char digit[24];
                long n = 0;

                if (pid <= 0)
                        return -1;
                for (long v = pid; v > 0; v /= 10)
                        digit[n++] = (char)('0' + v % 10);
                while (n > 0)
                        path[at++] = digit[--n];
        }
        for (long k = 0; suffix[k]; k++)
                path[at++] = suffix[k];
        path[at] = 0;
        handle = system_open_at(AT_FDCWD, path, FILE_READ | COMPOSITOR_XWM_O_CLOEXEC);
        if (handle < 0)
                return -1;
        got = compositor_call(syscall(read), (positive)handle, (positive)text, sizeof text - 1, 0, 0, 0);
        system_close((int)handle);
        if (got <= 0)
                return -1;
        text[got] = 0;
        digits = 0;
        // the command name is in parentheses and may hold anything: the fields are after the last one
        for (long k = got - 1; k >= 0; k--)
                if (text[k] == ')')
                {
                        digits = k + 1;
                        break;
                }
        if (digits == 0)
                return -1;
        // " state ppid": skip the state, then read the parent
        at = digits;
        while (text[at] == ' ')
                at++;
        while (text[at] && text[at] != ' ')
                at++;
        while (text[at] == ' ')
                at++;
        if (text[at] < '0' || text[at] > '9')
                return -1;
        while (text[at] >= '0' && text[at] <= '9')
                value = value * 10 + (text[at++] - '0');
        return value;
}

// The wayland client that is this X server: the one whose process is a child of the wrapper that connected (peer_pid). Found once, then
// kept while that client is the same process; -1 until a client's parent is the wrapper (the server's connection to Wayland comes first or second).
static long compositor_xwm_server(struct compositor_xwm *x)
{
        if (x->client >= 0 && wayland_client_pid(x->client) == x->client_pid && x->client_pid > 0)
                return x->client;
        x->client = -1;
        x->client_pid = -1;
        if (x->peer_pid <= 0)
                return -1;
        for (long at = 0; at < wayland_client_count(); at++)
        {
                long pid = wayland_client_pid(at);

                if (pid > 0 && compositor_xwm_ppid(pid) == x->peer_pid)
                {
                        x->client = at;
                        x->client_pid = pid;
                        break;
                }
        }
        return x->client;
}

/* ---- the slots: which server a row or a host's call is about ---- */

// The slot whose window the row is (a row is one X window of one server), or 0 for none.
static struct compositor_xwm *compositor_xwm_slot_of_row(long row)
{
        // No row is no window's: a record of row -1 is one not placed yet, of any server (B-h9-1).
        for (int at = 0; row >= 0 && at < COMPOSITOR_XWM_SLOTS; at++)
        {
                struct compositor_xwm *x = &compositor_xwm_slots[at];

                if (x->fd < 0)
                        continue;
                for (long w = 0; w < x->window_table.count; w++)
                        if (x->window[w].id && x->window[w].row == (int)row)
                                return x;
        }
        return 0;
}

// The first live slot that is ready (its setup is done), or the first slot when none is: where a call with no row goes.
static struct compositor_xwm *compositor_xwm_first_ready(void)
{
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                if (compositor_xwm_slots[at].fd >= 0 && compositor_xwm_slots[at].ready)
                        return &compositor_xwm_slots[at];
        return &compositor_xwm_slots[0];
}

/* ---- the host's entry points (wayland_host.c): the selections go to every server, a row's and a drag's to the server they are on ---- */

static void compositor_xwm_selection_changed(unsigned int which)
{
        // Only to a server that is one: its wrapper's child a Wayland client (a connection that is a socket and nothing else is offered nothing).
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                if (compositor_xwm_slots[at].fd >= 0 && compositor_xwm_slots[at].ready && compositor_xwm_server(&compositor_xwm_slots[at]) >= 0)
                {
                        compositor_xwm_at = &compositor_xwm_slots[at];
                        compositor_xwm_selection_changed_one(which);
                }
        compositor_xwm_at = &compositor_xwm_slots[0];
}

static void compositor_xwm_receive(long client, long which, const char *mime, long fd)
{
        struct compositor_xwm *x = compositor_xwm_first_ready();

        // A paste of an X selection goes to the server that owns it (x_owner: its window); a drag's to the first ready server.
        if (which != 2)
                for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                        if (compositor_xwm_slots[at].fd >= 0 && compositor_xwm_slots[at].ready && !compositor_xwm_slots[at].sel[which].owned &&
                            compositor_xwm_slots[at].sel[which].x_owner)
                        {
                                x = &compositor_xwm_slots[at];
                                break;
                        }
        compositor_xwm_at = x;
        compositor_xwm_receive_one(client, which, mime, fd);
        compositor_xwm_at = &compositor_xwm_slots[0];
}

static long compositor_xwm_xdrag(long what, long row, long px, long py)
{
        struct compositor_xwm *x = compositor_xwm_slot_of_row(row);
        long got = 0;

        // A drag's end (no row) goes to each server that holds a drag, an X source's or a Wayland one over its windows (B-h9-1): with two X servers
        // it went to the first one with a window not yet placed, and the one holding the drag kept XdndSelection and its drag live.
        if (what == WAYLAND_XDRAG_END)
        {
                for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                        if (compositor_xwm_slots[at].fd >= 0 &&
                            (compositor_xwm_slots[at].xdrag.began || compositor_xwm_slots[at].xdrag.dropped || compositor_xwm_slots[at].wdrag.live))
                                compositor_xwm_at = &compositor_xwm_slots[at], compositor_xwm_xdrag_one(what, row, px, py);
                compositor_xwm_at = &compositor_xwm_slots[0];
                return 0;
        }
        compositor_xwm_at = x ? x : compositor_xwm_first_ready();
        got = compositor_xwm_xdrag_one(what, row, px, py);
        compositor_xwm_at = &compositor_xwm_slots[0];
        return got;
}

/*
        STEAM'S OVERLAY IN A GAME ON WAYLAND

        Steam's overlay renderer reads its hot-key and the overlay's input only from X events. In a game that is a Wayland
        client, steam-overlay-gl.so (the steam profile's preload, test/steam_overlay_gl_shim.c) makes an unmapped X window the
        renderer takes for the game's, and names the game's pid on it (_MOONWATER_STEAM_OVERLAY), with
        _MOONWATER_STEAM_OVERLAY_OPEN saying when the overlay is open. While that game's window has the keys, its Shift+Tab is
        sent to the X window as the X server would send a key (Shift, Tab, their releases), and while the overlay is open every
        key, motion, button and wheel of the person's is sent there and none to the game, as Steam's overlay has them on X11.
        The window is believed only when X-Resource says its client is the program the pid names, so no other X client can
        ask for a program's keys. Events go by SendEvent with no mask, to the client that made the window.
        An open overlay is told again every second (the preload's pump): one not heard of for COMPOSITOR_XWM_STEAM_QUIET is
        taken as closed, so a game whose bridge stopped (its pump, its X server) gives the person's input back to the game.
*/
#define COMPOSITOR_XWM_STEAM_QUIET 3000u // milliseconds
static struct compositor_xwm_window *compositor_xwm_steam_window(unsigned int pid)
{
        if (!pid || pid == 1)
                return 0;
        for (int slot = 0; slot < COMPOSITOR_XWM_SLOTS; slot++)
        {
                struct compositor_xwm *x = &compositor_xwm_slots[slot];

                if (x->fd < 0)
                        continue;
                for (long at = 0; at < x->window_table.count; at++)
                        if (x->window[at].id && x->window[at].steam_pid == pid && x->window[at].owner == pid)
                        {
                                struct compositor_xwm_window *w = &x->window[at];

                                if (w->steam_open &&
                                    (unsigned int)(clock_monotonic_nanoseconds() / 1000000) - w->steam_told > COMPOSITOR_XWM_STEAM_QUIET)
                                        w->steam_open = 0;
                                compositor_xwm_at = x;
                                return w;
                        }
        }
        return 0;
}

// The modifiers as X's state mask: Shift, Control, Mod1 (Alt), Mod4 (the logo key).
static unsigned int compositor_xwm_steam_state(unsigned int modifiers)
{
        return ((modifiers & WINDOW_KEY_SHIFT) ? 1u : 0u) | ((modifiers & WINDOW_KEY_CONTROL) ? 4u : 0u) |
               ((modifiers & WINDOW_KEY_ALT) ? 8u : 0u) | ((modifiers & WINDOW_KEY_SUPER) ? 0x40u : 0u);
}

static void compositor_xwm_steam_send(struct compositor_xwm_window *w, unsigned int type, unsigned int detail, unsigned int state)
{
        static unsigned int last;
        unsigned int now = (unsigned int)(clock_monotonic_nanoseconds() / 1000000);
        unsigned char event[XWM_EVENT_SIZE], req[64];
        long n;

        // Each event a later time than the one before: an X client takes a release and a press of one time for a key's repeat.
        now = now > last ? now : last + 1;
        last = now;
        n = xwm_enc_input_event(event, sizeof event, type, detail, now, compositor_xwm_at->root, w->id, w->steam_x, w->steam_y, state);
        if (n == XWM_EVENT_SIZE && (n = xwm_enc_send_event(req, sizeof req, 0, w->id, 0, event, XWM_EVENT_SIZE)) > 0)
                compositor_xwm_queue(req, n);
}

// What the game of pid has: 0 no overlay window, 1 one (its Tab is the compositor's to read), 2 one whose overlay is open.
static int compositor_xwm_steam_wants(unsigned int pid)
{
        struct compositor_xwm_window *w = compositor_xwm_steam_window(pid);
        int wants = w ? (w->steam_open ? 2 : 1) : 0;

        compositor_xwm_at = &compositor_xwm_slots[0];
        return wants;
}

// A key while the game of pid has the keys: true when it went to the overlay (and is not the game's).
static int compositor_xwm_steam_key(unsigned int pid, unsigned int code, int value, unsigned int modifiers)
{
        struct compositor_xwm_window *w = compositor_xwm_steam_window(pid);
        unsigned int state = compositor_xwm_steam_state(modifiers);
        int taken = 0;

        if (!w)
        {
                compositor_xwm_at = &compositor_xwm_slots[0];
                return 0;
        }
        if (!w->steam_focused)
        {
                // FocusIn, mode NotifyNormal, detail NotifyNonlinear: the renderer reads the hot-key of the window with the focus.
                unsigned char event[XWM_EVENT_SIZE], req[64];
                long n = xwm_enc_input_event(event, sizeof event, XWM_EVENT_FOCUS_IN, 3, 0, 0, w->id, 0, 0, 0);

                if (n == XWM_EVENT_SIZE && (n = xwm_enc_send_event(req, sizeof req, 0, w->id, 0, event, XWM_EVENT_SIZE)) > 0)
                        compositor_xwm_queue(req, n);
                w->steam_focused = 1;
        }
        if (code == CK_KEY_TAB && w->steam_tab && value != 1)
        {
                // The Tab of the Shift+Tab sent: its repeats and its release are not the game's.
                if (!value)
                        w->steam_tab = 0;
                taken = 1;
        }
        else if (w->steam_open)
        {
                // The overlay is open: the key is its own, at the X keycode of the evdev code (Xwayland's keymap is the compositor's).
                if (value != 2 && code + 8 <= 255)
                        compositor_xwm_steam_send(w, value ? XWM_EVENT_KEY_PRESS : XWM_EVENT_KEY_RELEASE, code + 8, state);
                taken = 1;
        }
        else if (code == CK_KEY_TAB && value == 1 && (modifiers & WINDOW_KEY_SHIFT) &&
                 !(modifiers & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_SUPER)))
        {
                // Shift+Tab: Shift_L (X keycode 50) down, Tab (23) down and up, Shift up, as a person's keys reach an X window.
                compositor_xwm_steam_send(w, XWM_EVENT_KEY_PRESS, 50, 0);
                compositor_xwm_steam_send(w, XWM_EVENT_KEY_PRESS, 23, 1);
                compositor_xwm_steam_send(w, XWM_EVENT_KEY_RELEASE, 23, 1);
                compositor_xwm_steam_send(w, XWM_EVENT_KEY_RELEASE, 50, 1);
                w->steam_tab = 1;
                taken = 1;
        }
        if (taken)
                compositor_xwm_flush();
        compositor_xwm_at = &compositor_xwm_slots[0];
        return taken;
}

// One of the pointer's coordinates in the game's window (of size across) as the X window's pixels: the preload makes that window the
// size the game draws its frame at (an EGL surface's, a Vulkan swapchain's), which is the window's own unless it is scaled.
static int compositor_xwm_steam_scale(int at, int across, unsigned int x_across)
{
        if (across <= 0 || x_across <= 1 || x_across == (unsigned int)across)
                return at;
        return (int)((long long)at * (long long)x_across / (long long)across);
}

/*
        The pointer on the window of the game of pid (the window's own pixels; the window width by height): true when the overlay
        is open and it went there. A wheel's turn is the evdev sign (up, right positive) turned the other way for natural scrolling
        (flip), counted in notch units (a wheel's 120, a finger's pixels), and each whole notch is X's button 4 or 5 (up, down) or 6
        or 7 (left, right); a wheel's stop is the overlay's too.
*/
static int compositor_xwm_steam_pointer(unsigned int pid, unsigned int type, unsigned int flags, int a, int b, int width, int height,
                                        int flip, int notch)
{
        struct compositor_xwm_window *w = compositor_xwm_steam_window(pid);
        int taken = 0;

        if (w && w->steam_open)
        {
                taken = 1;
                if (type == WINDOW_EVENT_MOTION)
                {
                        w->steam_x = compositor_xwm_steam_scale(a, width, w->width);
                        w->steam_y = compositor_xwm_steam_scale(b, height, w->height);
                        compositor_xwm_steam_send(w, XWM_EVENT_MOTION_NOTIFY, 0, 0);
                }
                else if (type == WINDOW_EVENT_BUTTON)
                {
                        // The buttons as X numbers them: left 1, middle 2, right 3.
                        unsigned int button = a == 0x110 ? 1u : a == 0x112 ? 2u : a == 0x111 ? 3u : 0u;

                        if (button)
                                compositor_xwm_steam_send(w, (flags & WINDOW_KEY_DOWN) ? XWM_EVENT_BUTTON_PRESS : XWM_EVENT_BUTTON_RELEASE,
                                                          button, 0);
                }
                else if (type == WINDOW_EVENT_WHEEL && (flags & WINDOW_WHEEL_STOP))
                        w->steam_wheel[0] = w->steam_wheel[1] = 0;
                else if (type == WINDOW_EVENT_WHEEL && a)
                {
                        int axis = b != 0, turn = flip ? -a : a, *left = &w->steam_wheel[axis];

                        notch = notch > 0 ? notch : 120;
                        if ((*left > 0 && turn < 0) || (*left < 0 && turn > 0))
                                *left = 0;
                        *left += turn;
                        for (int sent = 0; sent < 16 && (*left >= notch || *left <= -notch); sent++)
                        {
                                unsigned int button = axis ? (*left > 0 ? 7u : 6u) : (*left > 0 ? 4u : 5u);

                                *left += *left > 0 ? -notch : notch;
                                compositor_xwm_steam_send(w, XWM_EVENT_BUTTON_PRESS, button, 0);
                                compositor_xwm_steam_send(w, XWM_EVENT_BUTTON_RELEASE, button, 0);
                        }
                        if (*left >= notch || *left <= -notch)
                                *left = 0;
                }
                compositor_xwm_flush();
        }
        compositor_xwm_at = &compositor_xwm_slots[0];
        return taken;
}

static int compositor_xwm_drag_escape(void)
{
        int done = 0;

        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                if (compositor_xwm_slots[at].fd >= 0)
                {
                        compositor_xwm_at = &compositor_xwm_slots[at];
                        done |= compositor_xwm_drag_escape_one();
                }
        compositor_xwm_at = &compositor_xwm_slots[0];
        return done;
}

#endif /* COMPOSITOR_XWM_INCLUDED */
