/*
        Moonwater window: shared compositor table (struct windows), terminal cell ring (struct window),
        and client helpers. Includes canvas.c ABI and ui/ui.c list format; expects lib.c + lib.util.c.
*/

#ifndef WINDOW_INCLUDED
#define WINDOW_INCLUDED

#define CANVAS_ABI_ONLY
#include "canvas.c"
#undef CANVAS_ABI_ONLY

#define UI_LIST_ONLY
#include "ui/ui.c"
#undef UI_LIST_ONLY

// O_RDWR | O_CLOEXEC
#define WINDOW_OPEN_FLAGS 0x80002u
#define WINDOW_PIXELS 4096
#define WINDOW_PRIVATE 0xffffffffu
#define WINDOW_MINIMIZED 4u
#define WINDOW_Z_MAX (1 << 24)
#define WINDOW_COORD_MAX (1 << 24)
#define WINDOW_CLOSING 2u
#define WINDOW_KEYS 64

enum
{
        WINDOW_KEY_DOWN = 1u, WINDOW_KEY_SHIFT = 2u, WINDOW_KEY_CONTROL = 4u, WINDOW_KEY_ALT = 8u,
        WINDOW_KEY_POINTER = 16u, WINDOW_KEY_POINTER_MOVE = 32u, WINDOW_KEY_ALTGR = 64u, WINDOW_KEY_REPEAT = 128u,
        WINDOW_KEY_SUPER = 256u, WINDOW_KEY_TOUCH = 512u, WINDOW_KEY_CANCEL = 1024u, WINDOW_KEY_HOLD = 2048u,
        WINDOW_KEY_SETTLE = 4096u, WINDOW_EVENT_VIRTUAL = 0x8000u
};

struct window_key
{
        unsigned int code, character, flags, reserved;
};

enum
{
        WINDOW_MIN_WIDTH = 96, WINDOW_MIN_HEIGHT = 48, WINDOW_LEAST_MOST = 16384, WINDOW_TITLE_MAX = 128,
        WINDOW_LAYER_BACKGROUND = -1, WINDOW_LAYER_NORMAL = 0, WINDOW_LAYER_ABOVE = 1, WINDOW_LAYER_OVERLAY = 2
};

struct window
{
        int x, y, z;
        unsigned int width, height, region, display, style, edge, sequence;
        char title[WINDOW_TITLE_MAX];
        unsigned int state, pitch, max_width, max_height, display_width, display_height, awake, handle;
        unsigned int stride, history, head, lines, key_head, key_tail;
        struct window_key keys[WINDOW_KEYS];
        unsigned int columns, rows, damage_row, damage_rows, grid_columns, grid_rows, max_columns, max_rows, mapping, want;
};

static inline void window_damage(struct window *window, unsigned int row, unsigned int rows)
{
        unsigned int last = row + rows, previous;

        if (!rows)
                return;
        if (last < row)
                last = ~0u;
        if (!window->damage_rows)
        {
                window->damage_row = row, window->damage_rows = last - row;
                return;
        }
        previous = window->damage_row + window->damage_rows;
        if (row < window->damage_row)
                window->damage_row = row;
        if (previous > last)
                last = previous;
        window->damage_rows = last - window->damage_row;
}

struct window_cell
{
        unsigned int character;
        unsigned char ink, paper;
        unsigned short flags;
};

static inline unsigned int window_cell_colour(unsigned int index)
{
        static const unsigned int sixteen[16] = {
            0x000000, 0xcd0000, 0x00cd00, 0xcdcd00, 0x0000ee, 0xcd00cd, 0x00cdcd, 0xe5e5e5,
            0x7f7f7f, 0xff0000, 0x00ff00, 0xffff00, 0x5c5cff, 0xff00ff, 0x00ffff, 0xffffff,
        };
        static const unsigned char level[6] = {0, 95, 135, 175, 215, 255};

        if (index < 16)
                return sixteen[index];
        if (index < 232)
        {
                index -= 16;
                return (unsigned int)level[index / 36] << 16 | (unsigned int)level[index / 6 % 6] << 8 | level[index % 6];
        }
        return (8 + (index - 232) % 24 * 10) * 0x010101u;
}

enum
{
        WINDOW_CELL_BOLD = 1u, WINDOW_CELL_DIM = 2u, WINDOW_CELL_ITALIC = 4u, WINDOW_CELL_UNDERLINE = 8u,
        WINDOW_CELL_BLINK = 16u, WINDOW_CELL_HIDDEN = 32u, WINDOW_CELL_STRIKE = 64u, WINDOW_CELL_WIDE = 128u,
        WINDOW_CELL_WIDE_RIGHT = 256u, WINDOW_CELL_BAR = 512u, WINDOW_CELL_SELECTED = 1024u,
        WINDOW_CELL_INK_SET = 8192u, WINDOW_CELL_PAPER_SET = 16384u, WINDOW_CELL_SWAP = 32768u,
        WINDOW_WANT_POINTER = 1u, WINDOW_CELL_W = 8, WINDOW_CELL_H = 16
};

// from, moved amount 256ths of the way to to.
static inline unsigned int window_mix(unsigned int from, unsigned int to, unsigned int amount)
{
        unsigned int mixed = 0;

        for (unsigned int shift = 0; shift < 24; shift += 8)
                mixed |= (((from >> shift & 255) * (256 - amount) + (to >> shift & 255) * amount + 128) >> 8) << shift;
        return mixed;
}

static inline int window_light(unsigned int ground)
{
        unsigned int r = ground >> 16 & 255, g = ground >> 8 & 255, b = ground & 255;

        return 2126u * r * r + 7152u * g * g + 722u * b * b > 116394750u;
}

static inline unsigned int window_contrast(unsigned int ground)
{
        return window_mix(ground, window_light(ground) ? 0x000000u : 0xffffffu, 216);
}

#define window_at(window, type, offset) ((type *)((char *)(window) + (offset)))
#define window_cells(window) window_at((window), struct window_cell, WINDOW_PIXELS)
#define window_lengths(window) window_at((window), unsigned int, (window)->lines)

static inline struct window_cell *window_line(struct window *window, unsigned int index)
{
        return window_cells(window) + (index % window->history) * window->stride;
}

static inline unsigned int *window_length(struct window *window, unsigned int index)
{
        return window_lengths(window) + index % window->history;
}

static inline void window_scroll(struct window *window)
{
        *window_length(window, window->head) = 0;
        __atomic_store_n(&window->head, window->head + 1, __ATOMIC_RELEASE);
}

static inline void window_grid(struct window *window, unsigned int columns, unsigned int rows)
{
        window->grid_columns = columns, window->grid_rows = rows;
}

static inline int window_key(struct window *window, struct window_key *key)
{
        unsigned int head = __atomic_load_n(&window->key_head, __ATOMIC_ACQUIRE);

        if (window->key_tail == head)
                return 0;
        *key = window->keys[window->key_tail % WINDOW_KEYS];
        __atomic_store_n(&window->key_tail, window->key_tail + 1, __ATOMIC_RELEASE);
        return 1;
}

/* Shared userspace compositor table (/run/moonwater/windows) and per-row doors. */
#define WINDOWS_TABLE "/run/moonwater/windows"
#define WINDOWS_GPU_REASONS 24
static const string_address windows_gpu_reasons[WINDOWS_GPU_REASONS] = {
    "drawn", "canvas gpu is off", "headless, and not asked for the GPU (-G)", "no card the GPU's kernels are for", "the self-test drew wrong",
    "cooling down after hangs", "a scanout pitch not 16 bytes", "a job given up on may still run",
    "the scanout is not one the card can draw into", "a window's picture is not mapped for the card", "a landing (scaled)", "a snap preview or a shared edge",
    "the records or the cover tables are full", "a close button not wholly in its picture", "the scene did not finish", "a turned screen (the processor turned it)",
    "other frames, or empty damage", "a window with alpha and round corners (the file gpu-round-off is there)",
    "a client's own pointer picture drawn into the scene"};

#define WINDOWS_GPU_REQS 10
static const string_address windows_gpu_req_names[WINDOWS_GPU_REQS] = {"none", "render node", "driver", "family", "userptr", "context", "ring", "self-test",
                                                                     "udmabuf", "PRIME import"};
static const string_address windows_gpu_req_fixes[WINDOWS_GPU_REQS] = {
    "", "no render node of the card opened: check its mode and owner, and that the driver is loaded (the render group, or mode 0666, lets a user open it)",
    "the card's driver has no GPU path: Canvas reads amdgpu and i915 cards (virtio_gpu, nouveau and xe stay on the processor)",
    "the chip has no GPU kernels yet (so far AMD RDNA2 only: families 144, 146, 149, 151; none for i915 yet): the processor draws everything",
    "the kernel would not map a page for the card: rebuild it with CONFIG_DRM_AMDGPU_USERPTR=y (error 19 is that kernel: it selects HMM_MIRROR and MMU_NOTIFIER)",
    "the card would not give a buffer, its VM map or a compute context: the amdgpu compute ring or its firmware is not up (the kernel lines say which)",
    "the compute ring did not finish the self-test job: firmware, clocks or a ring reset (the kernel lines say which)",
    "the card drew the self-test picture wrong: its kernels do not match this family's ISA; the card stays on the processor",
    "/dev/udmabuf would not make a dma-buf of a memfd page: CONFIG_UDMABUF=y, and /dev/udmabuf must open (the card still composes; a refused userptr then has no fallback)",
    "a dma-buf would not import into the render node: the card cannot read a window's memfd (CONFIG_DRM_AMDGPU, with PRIME; the card still composes)"};

enum { WINDOWS_ISSUES = 32, WINDOWS_ISSUE_KEY = 24, WINDOWS_ISSUE_DETAIL = 192, WINDOWS_ISSUE_ANY = 0u, WINDOWS_ISSUE_WARNING = 1u, WINDOWS_ISSUE_ERROR = 2u };
#define WINDOWS_KEY_GPU "canvas gpu"
#define WINDOWS_KEY_NOTICES "system notices"
#define WINDOWS_KERNEL_CLASSES(X)                                          \
        X(GPU, WINDOWS_KEY_GPU, "a kernel notice about the GPU")           \
        X(USB, "usb", "a kernel notice about USB")                         \
        X(WIFI, "wifi", "a kernel notice about Wi-Fi")                     \
        X(BLUETOOTH, "bluetooth", "a kernel notice about Bluetooth")       \
        X(AUDIO, "audio device", "a kernel notice about audio")            \
        X(STORAGE, "storage", "a kernel notice about storage")             \
        X(SYSTEM, WINDOWS_KEY_NOTICES, "a kernel notice about the system") \
        X(PROGRAM, WINDOWS_KEY_NOTICES, "a program crashed")

enum windows_issue_code
{
        WINDOWS_ISSUE_NONE, WINDOWS_ISSUE_GPU_MAP, WINDOWS_ISSUE_GPU_REFUSED, WINDOWS_ISSUE_GPU_LISTS,
        WINDOWS_ISSUE_GPU_COOLING, WINDOWS_ISSUE_GPU_UNTRUSTED, WINDOWS_ISSUE_GPU_FAMILY, WINDOWS_ISSUE_GPU_LOST,
        WINDOWS_ISSUE_GPU_SCANOUT, WINDOWS_ISSUE_GPU_USERPTR, WINDOWS_ISSUE_AUDIO_OPEN, WINDOWS_ISSUE_AUDIO_RATE,
        WINDOWS_ISSUE_GPU_REQUIRES, WINDOWS_ISSUE_GPU_FAULT, WINDOWS_ISSUE_GPU_HUNG,
#define WINDOWS_KERNEL_CODE(name, key, text) WINDOWS_ISSUE_KERNEL_##name,
        WINDOWS_KERNEL_CLASSES(WINDOWS_KERNEL_CODE)
#undef WINDOWS_KERNEL_CODE
        WINDOWS_ISSUE_RADIO, WINDOWS_ISSUE_NOTIFICATION, WINDOWS_ISSUE_COUNT
};
typedef char windows_issue_codes_fit[WINDOWS_ISSUE_COUNT <= 256 ? 1 : -1];
#define KERNEL_NOTICE_CLASSES (WINDOWS_ISSUE_RADIO - WINDOWS_ISSUE_KERNEL_GPU)
#define KERNEL_NOTICE_ALL ((1u << KERNEL_NOTICE_CLASSES) - 1u)
#define windows_issue_kernel(code) ((unsigned int)(code) - WINDOWS_ISSUE_KERNEL_GPU < KERNEL_NOTICE_CLASSES)
static const string_address windows_issue_names[WINDOWS_ISSUE_COUNT] = {
        "none", "the kernel refused to map window pictures", "the card refused presents", "the lists are off", "the card is cooling down",
        "the card's self-test drew wrong", "the GPU family is not supported", "apps lost frames to the processor", "the card refused direct scan-out",
        "the kernel has no userptr for the GPU (CONFIG_DRM_AMDGPU_USERPTR)", "the audio sink did not open", "the audio sink refused the rate",
        "a prerequisite of the GPU path failed", "the card faulted reading GPU memory", "the card hung twice within a minute",
#define WINDOWS_KERNEL_NAME(name, key, text) [WINDOWS_ISSUE_KERNEL_##name] = text,
        WINDOWS_KERNEL_CLASSES(WINDOWS_KERNEL_NAME)
#undef WINDOWS_KERNEL_NAME
        [WINDOWS_ISSUE_RADIO] = "a radio is blocked (rfkill)", [WINDOWS_ISSUE_NOTIFICATION] = "a program's notification"};

struct windows_issue
{
        char key[WINDOWS_ISSUE_KEY];
        unsigned int severity, code, a, b, used;
        char detail[WINDOWS_ISSUE_DETAIL];
};

#define WINDOWS_DOOR "/run/moonwater/window."
#define WINDOWS (1u << 20)
#define WINDOWS_STARTING_NS 30000000000ull
#define WINDOW_WHEEL_SPAN_NS 5000000000ull
enum
{
        WINDOWS_EVENTS = 64, WINDOWS_TITLE = 32, WINDOWS_APP_ID = 64, WINDOWS_STARTING = 4,
        WINDOWS_ICON = 32, WINDOWS_ICON_MOST = 1024u, WINDOWS_OUTPUTS = 32, WINDOWS_OUTPUT_NAME = 16,
        WINDOWS_OUTPUT_MODEL = 16, WINDOWS_OUTPUT_HZ = 8, WINDOWS_OUTPUT_SIZES = 48,
        WINDOWS_INPUTS = 1024, WINDOW_WHEEL_RING = 1024, WINDOWS_INPUT_NAME = 48,
        WINDOWS_INPUT_KEYBOARD = 1u, WINDOWS_INPUT_MOUSE = 2u, WINDOWS_INPUT_TABLET = 4u, WINDOWS_INPUT_WAKES = 8u,
        WINDOWS_INPUT_WHEEL = 16u, WINDOWS_INPUT_WHEEL_HI = 32u, WINDOWS_INPUT_HWHEEL = 64u, WINDOWS_INPUT_HWHEEL_HI = 128u,
        WINDOWS_INPUT_BUTTONS = 256u, WINDOWS_INPUT_TOUCH = 512u, WINDOWS_INPUT_FINGERS = 1024u, WINDOWS_INPUT_SLOTS = 2048u,
        WINDOWS_INPUT_POINTER = 4096u, WINDOWS_INPUT_DIRECT = 8192u
};

/* Gamepad/joystick/accelerometer check: excluded from compositor pointer input, passed to bowls. */
static inline int window_input_game(const unsigned char *keys, unsigned int properties)
{
        int buttons = 0;

        if (properties & 0x40)
                return 1;
        if (properties & 3)
                return 0;
        for (unsigned int code = 0x120; code <= 0x13e; code++)
                buttons |= keys[code / 8] >> (code % 8) & 1;
        for (unsigned int code = 0x2c0; code <= 0x2e7; code++)
                buttons |= keys[code / 8] >> (code % 8) & 1;
        return buttons && !(keys[0x110 / 8] >> (0x110 % 8) & 1) && !(keys[0x14a / 8] >> (0x14a % 8) & 1) &&
               !(keys[0x140 / 8] >> (0x140 % 8) & 1);
}

#define WINDOWS_MODE 0600
#define WINDOWS_DOOR_MODE WINDOWS_MODE

enum
{
        WINDOW_EVENT_KEY = 1u, WINDOW_EVENT_MOTION = 2u, WINDOW_EVENT_BUTTON = 3u, WINDOW_EVENT_WHEEL = 4u,
        WINDOW_EVENT_CLIP = 5u, WINDOW_EVENT_PINCH = 6u, WINDOW_EVENT_DRAG = 7u,
        WINDOW_WHEEL_PRECISE = 4096u, WINDOW_WHEEL_STOP = 8192u, WINDOW_WHEEL_STREAM = 16384u,
        WINDOW_SCROLL_FLIP = 1u, WINDOW_SCROLL_STOPS = 2u, WINDOW_SCROLL_STILL = 4u, WINDOW_SCROLL_DRAG = 8u,
        WINDOW_PINCH_BEGIN = 16384u, WINDOW_PINCH_END = 32768u, WINDOW_PINCH_ONE = 1024,
        WINDOWS_DRAG_ENTER = 1, WINDOWS_DRAG_MOTION, WINDOWS_DRAG_LEAVE, WINDOWS_DRAG_DROP, WINDOWS_DRAG_READY,
        WINDOWS_DRAG_DONE, WINDOWS_DRAG_ACTION, WINDOWS_DRAG_DECLINED, WINDOWS_DRAG_ENDED,
        WINDOWS_DRAG_COPY = 1u, WINDOWS_DRAG_MOVE = 2u, WINDOWS_DRAG_ASK = 4u,
        WINDOWS_DRAG_KIND_SHIFT = 8, WINDOWS_DRAG_ITEMS_SHIFT = 16
};

static inline int window_pinch_pack(int x, int y) { return (int)((unsigned int)x << 16 | ((unsigned int)y & 0xffffu)); }
static inline int window_pinch_x(int b) { return (int)(((unsigned int)b >> 16) & 0xffffu); }
static inline int window_pinch_y(int b) { return (int)((unsigned int)b & 0xffffu); }
static inline int window_drag_x(int b) { return (int)(short)((unsigned int)b & 0xffffu); }
static inline int window_drag_y(int b) { return (int)(short)(((unsigned int)b >> 16) & 0xffffu); }

enum
{
        WINDOW_PLACE_AUTO = 0u, WINDOW_PLACE_CENTER = 1u, WINDOW_PLACE_AT = 2u, WINDOW_PLACE_MAXIMIZED = 3u,
        WINDOW_PLACE_FULLSCREEN = 4u, WINDOW_PLACE_HOW = 7u, WINDOW_PLACE_SCREEN = 8u, WINDOW_PLACE_SIZE = 16u,
        WINDOW_PLACE_FREE = 32u, WINDOW_PLACE_MOST = 16384, WINDOWS_NAME = 16,
        WINDOW_PLACE_WORDS = 14u, WINDOW_PLACE_TEXT = 112u, WINDOW_ASKED_ENVIRONMENT = 256
};
#define WINDOW_PLACE_SCREENS ((unsigned int)WINDOWS_OUTPUTS)
#define WINDOW_PLACE_VARIABLE "MOONWATER_PLACE"
#define WINDOW_ASKED_VARIABLE "MOONWATER_ASKED"
#define WINDOWS_ASKED_DIR "/run/moonwater/asked"

struct window_place
{
        unsigned int how;
        int x, y;
        unsigned int width, height, screen;
        char name[WINDOWS_NAME];
};

enum
{
        WINDOW_GLASS = 1u, WINDOW_NO_CLOSE = 2u, WINDOW_DISMISS = 4u, WINDOW_NO_TAB = 8u,
        WINDOW_NO_GRAB = 16u, WINDOW_NO_KEYS = 32u, WINDOW_KEYBOARD = 64u, WINDOWS_OSK = 16,
        WINDOW_POINTER_ARROW = 0u, WINDOW_POINTER_RESIZE = 1u, WINDOW_POINTER_TEXT = 5u, WINDOW_POINTER_HAND = 8u,
        WINDOWS_REFUSED_RATE = 1u, WINDOWS_REFUSED_TURN = 2u, WINDOWS_REFUSED_PLACE = 4u, WINDOWS_REFUSED_SIZE = 8u,
        WINDOWS_LOOP_SOURCES = 10,
        WINDOW_LIST_VALID = 0u, WINDOW_LIST_NONE, WINDOW_LIST_PUBLISHING, WINDOW_LIST_HEADER, WINDOW_LIST_TORN,
        WINDOW_LIST_ROOM_OTHER, WINDOW_LIST_SIZE, WINDOW_LIST_PITCH, WINDOW_LIST_DAMAGE, WINDOW_LIST_REASONS,
        WINDOWS_COPY_TABLE = 0, WINDOWS_COPY_CARD, WINDOWS_COPY_CPU, WINDOWS_COPY_WHOLE
};

struct windows
{
        unsigned int output_width, output_height, scale, period, stride, focus, scanned, refused, refused_error, unscanned;
        char driver[16];
        unsigned int pointer_plane;
        int overlay;
        unsigned long pixels, buffer;

        /* Outputs (index 0 is primary). */
        unsigned int outputs, layout;
        int output_x[WINDOWS_OUTPUTS], output_y[WINDOWS_OUTPUTS];
        unsigned int output_w[WINDOWS_OUTPUTS], output_h[WINDOWS_OUTPUTS];
        char output_name[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_NAME], output_model[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_MODEL];
        unsigned int output_rates[WINDOWS_OUTPUTS], output_now[WINDOWS_OUTPUTS];
        unsigned int output_hz[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_HZ];
        unsigned int output_turn[WINDOWS_OUTPUTS], output_software[WINDOWS_OUTPUTS];
        unsigned int output_asked_hz[WINDOWS_OUTPUTS], output_asked_turn[WINDOWS_OUTPUTS], output_refused[WINDOWS_OUTPUTS];
        unsigned int output_asked_scale[WINDOWS_OUTPUTS], output_sizes[WINDOWS_OUTPUTS], output_size_preferred[WINDOWS_OUTPUTS];
        unsigned short output_size[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_SIZES][2];
        unsigned int output_asked_w[WINDOWS_OUTPUTS], output_asked_h[WINDOWS_OUTPUTS], output_mode_w[WINDOWS_OUTPUTS], output_mode_h[WINDOWS_OUTPUTS];

        /* Input devices and wheel diagnostics. */
        int input_number[WINDOWS_INPUTS];
        unsigned short input_id[WINDOWS_INPUTS][4];
        unsigned int input_kinds[WINDOWS_INPUTS], input_wheel_read[WINDOWS_INPUTS], input_wheel_used[WINDOWS_INPUTS], input_dropped[WINDOWS_INPUTS];
        unsigned long input_wheel_last[WINDOWS_INPUTS];
        unsigned long long wheel_when[WINDOWS_INPUTS][WINDOW_WHEEL_RING];
        unsigned char wheel_stream_flag[WINDOWS_INPUTS][WINDOW_WHEEL_RING];
        unsigned int wheel_turns[WINDOWS_INPUTS], wheel_streams[WINDOWS_INPUTS];
        char input_name[WINDOWS_INPUTS][WINDOWS_INPUT_NAME];
        unsigned int wheel_sent, wheel_nowhere, wheel_full;
        int wheel_row, wheel_x, wheel_y;

        /* Theme, scroll, caret, language, timing preferences. */
        unsigned int theme, accent, motion, themed, wheel_off, scroll_off, scroll_speed, caret_speed, caret_thick;
        unsigned int language, text_scale, motion_pace, click_ms, hold_ms, wheel_lines;

        /* Per-window columns. */
        unsigned int pid[WINDOWS], state[WINDOWS];
        int x[WINDOWS], y[WINDOWS];
        unsigned int width[WINDOWS], height[WINDOWS], want_width[WINDOWS], want_height[WINDOWS];
        unsigned int buffer_index[WINDOWS], frame[WINDOWS], shown[WINDOWS];
        int damage_x[WINDOWS], damage_y[WINDOWS], damage_width[WINDOWS], damage_height[WINDOWS];
        char title[WINDOWS][WINDOWS_TITLE];
        unsigned int glass[WINDOWS], pointer_shape[WINDOWS], asked[WINDOWS], ground[WINDOWS], window_scale[WINDOWS];
        unsigned int lift[WINDOWS], field[WINDOWS], body_state[WINDOWS];
        int body_raw[WINDOWS], body_drawn[WINDOWS], body_speed[WINDOWS];
        unsigned int osk_setting, osk_shown, osk_input, osk_physical, osk_start, osk_layout, osk_purpose, osk_caret;
        /* The on-screen keyboard's own process, written when it is started. The compositor grants keyboard keys to a row only
           when that row's recorded owner is this process (windows_owner_ok). A global, not a per-row column: 0 is none. */
        unsigned int osk_pid;
        unsigned int osk_head[WINDOWS], osk_tail[WINDOWS];
        int osk_code[WINDOWS][WINDOWS_OSK], osk_character[WINDOWS][WINDOWS_OSK];
        unsigned int osk_flags[WINDOWS][WINDOWS_OSK], hover_off[WINDOWS], pinch_wanted[WINDOWS], pinned[WINDOWS];
        int spoiled_x[WINDOWS][2], spoiled_y[WINDOWS][2], spoiled_width[WINDOWS][2], spoiled_height[WINDOWS][2];
        unsigned int place[WINDOWS];
        int place_x[WINDOWS], place_y[WINDOWS];
        unsigned int place_width[WINDOWS], place_height[WINDOWS], place_screen[WINDOWS];
        char name[WINDOWS][WINDOWS_NAME];
        unsigned int moved[WINDOWS], page[WINDOWS];
        int button_x, button_y;
        unsigned int button_w, button_h, button_edge, button_screen;
        unsigned int event_head[WINDOWS], event_tail[WINDOWS];
        unsigned short event_type[WINDOWS][WINDOWS_EVENTS], event_flags[WINDOWS][WINDOWS_EVENTS];
        int event_a[WINDOWS][WINDOWS_EVENTS], event_b[WINDOWS][WINDOWS_EVENTS];
        unsigned long long event_when[WINDOWS][WINDOWS_EVENTS];

        /* Clipboard, drag-and-drop, popups. */
        unsigned int clip_pid, clip_on, clip_serial[3];
        unsigned long clip_length[3];
        int clip_owner[2];
        unsigned int cli_pid, cli_fd, cli_which, cli_serial, cli_answer, cli_clear, clip_watch[WINDOWS];
        int clip_offer_fd[WINDOWS];
        unsigned int clip_offer_which[WINDOWS], clip_offer_serial[WINDOWS], clip_ask_which[WINDOWS], clip_ask_serial[WINDOWS];
        unsigned int clip_offer_answer[WINDOWS], clip_given_serial[WINDOWS];
        unsigned long clip_given_length[WINDOWS];
        int drag_offer_fd[WINDOWS];
        unsigned int drag_offer_actions[WINDOWS], drag_offer_serial[WINDOWS], drop_ok[WINDOWS];
        int drop_take[WINDOWS];
        unsigned int drop_take_actions[WINDOWS], drop_take_preferred[WINDOWS], drop_take_serial[WINDOWS];
        unsigned int drop_done[WINDOWS], drop_done_serial[WINDOWS], popup_parent[WINDOWS];
        int popup_x[WINDOWS], popup_y[WINDOWS], popup_w[WINDOWS], popup_h[WINDOWS];
        unsigned int popup_flags[WINDOWS];

        /* GPU, VRR, draw lists, Wayland/XWM metrics, issue board. */
        unsigned int gpu_drawn, gpu_processor, gpu_refused[WINDOWS_GPU_REASONS], gpu_map_why, gpu_family, gpu_device, gpu_map_probe;
        char gpu_said[256];
        unsigned int gpu_faults;
        char gpu_fault[320];
        unsigned int gpu_map_user, gpu_map_dma, gpu_recovered, gpu_priority, gpu_priority_error;
        unsigned int wakes, wake_how, wake_sets, wake_refused, period_seen, vrr, vrr_kicks, vrr_set_us, flips_async, gpu_node_pick;
        char gpu_node_bus[32], gpu_display_bus[32];
        unsigned int gpu_req_checked, gpu_req_first, gpu_req_error, gpu_req_mask, gpu_req_mode, gpu_req_owner, gpu_req_route, gpu_req_route_error;
        char gpu_req_driver[8], gpu_req_node[20];
        unsigned long lists, list_bytes;
        unsigned int list_mode, list_state[WINDOWS], list_seen[WINDOWS], list_refused, list_why[16];
        unsigned int list_invalid[WINDOW_LIST_REASONS], list_size_list[2], list_size_taken[2];
        unsigned int list_tried[WINDOWS], list_lost[WINDOWS][UI_LOST_COUNT];
        unsigned long wl_input_ms;
        unsigned int wl_inhibitors, wl_idled, wl_bells, wl_icons, wl_shortcuts;
        unsigned char wl_content[WINDOWS], wl_modal[WINDOWS], wl_icon[WINDOWS];
        unsigned int issue_seq, issue_lock, issue_count, issue_refused;
        struct windows_issue issues[WINDOWS_ISSUES];
        unsigned int gpu_on, gpu_able, gpu_state, gpu_steps, client_in_place, client_copied, client_card, client_scan_fourcc;
        unsigned long long client_scan_modifier;
        unsigned int client_scan_pitch, client_scan_offset, client_scan_width, client_scan_height;
        int client_scan_errno;
        unsigned int client_scan_refused, xwm_counts[4], client_card_refused[8], client_lend_refused[12];
        unsigned int client_lend_format, client_lend_fourcc, client_lend_dmabuf;
        unsigned long long client_lend_modifier;
        unsigned int client_wc_seeded, client_wc_presents;
        unsigned int metric_card_mean, metric_card_worst, metric_processor_mean, metric_processor_worst;
        unsigned int metric_maps, metric_map_hits, metric_map_misses, metric_wakes, metric_jobs, metric_job_mean;
        unsigned int metric_presents, metric_lists, metric_loops, metric_loop_idle, metric_loop_by[WINDOWS_LOOP_SOURCES];
        unsigned int metric_late, metric_missed, metric_lead, metric_present_most, metric_flip_jobs, metric_flip_jobs_most;
        unsigned int metric_waited, metric_maps_own_user, metric_maps_own_errno;
        unsigned int metric_gpu_jobs_scene;
        unsigned long long metric_gpu_job_ns_scene;
        unsigned int metric_gpu_jobs_copy;
        unsigned long long metric_gpu_job_ns_copy;
        unsigned int metric_gpu_jobs_scale;
        unsigned long long metric_gpu_job_ns_scale;
        unsigned int metric_jobs_waited, metric_jobs_unwaited;
        unsigned long long metric_job_submit_ns, metric_job_wait_ns;
        unsigned int kernel_dismiss, client_rects_card, client_rects_cpu, client_layers_refused[12], client_layers_laid, client_layers_gathered[2];
        unsigned int metric_card_hand_mean, metric_card_hand_worst, metric_card_job_mean, metric_card_job_worst, metric_card_seen_mean, metric_card_seen_worst;
        unsigned int metric_card_split, metric_clocks_level, metric_clocks_held, metric_clocks_asks, metric_clocks_took, metric_clocks_error;
        unsigned int client_layers_laid_card, client_seeded[2];
        unsigned int least_width[WINDOWS], least_height[WINDOWS], most_width[WINDOWS], most_height[WINDOWS];
        signed char layer[WINDOWS];
        unsigned int close_asks[WINDOWS];
        /* A close request from this row's program: close_target is the target row plus one (0 is none), and close_target_serial
           counts the requests. The compositor reads the serial once per take and checks the request itself. */
        unsigned int close_target[WINDOWS], close_target_serial[WINDOWS];
        char app_id[WINDOWS][WINDOWS_APP_ID];
        unsigned int client_row_copies[WINDOWS][4], icon_seq[WINDOWS], icon[WINDOWS][WINDOWS_ICON * WINDOWS_ICON];
        char starting[WINDOWS_STARTING][WINDOWS_APP_ID];
        unsigned long long starting_at[WINDOWS_STARTING];
        unsigned int abi, rows, capacity;
        /* Counts the programs that found every row taken and asked for more. The clipboard door is only the doorbell. */
        unsigned int rows_wanted;
};

#define WINDOWS_ABI 14u
#define WINDOWS_CLIP_NO 0x80000000u
#define WINDOWS_CLIP_MOST (64ul << 20)
#define WINDOWS_CLIP_DOOR "/run/moonwater/clip"
enum
{
        WINDOWS_VRR_CAPABLE = 1u, WINDOWS_VRR_PROPERTY = 2u, WINDOWS_VRR_ON = 4u, WINDOWS_VRR_REFUSED = 8u,
        WINDOWS_CLIP_CLIPBOARD = 0u, WINDOWS_CLIP_PRIMARY = 1u, WINDOWS_CLIP_DRAG = 2u,
        WINDOWS_CLIP_CHANGED = 1, WINDOWS_CLIP_READY, WINDOWS_CLIP_REFUSED, WINDOWS_CLIP_TAKEN, WINDOWS_CLIP_DECLINED,
        WINDOWS_CLIP_FD = 900, WINDOW_POPUP_POINT = 1u, WINDOW_POPUP_RIGHT = 2u, WINDOW_POPUP_EXACT = 4u
};

static inline int windows_abi_ok(unsigned int abi) { return abi == WINDOWS_ABI; }

static inline unsigned int windows_room(const struct windows *table)
{
        unsigned int room = table ? __atomic_load_n(&table->capacity, __ATOMIC_ACQUIRE) : 0;

        return room < WINDOWS ? room : WINDOWS;
}

static inline unsigned int windows_rows(const struct windows *table)
{
        unsigned int rows = table ? __atomic_load_n(&table->rows, __ATOMIC_ACQUIRE) : 0, room = windows_room(table);

        return rows < room ? rows : room;
}

/* Wheel readout ring & draw-list validation. */
static void window_wheel_note(struct windows address_to table, unsigned int at, unsigned long long when, int stream)
{
        unsigned int slot = table->wheel_turns[at]++ % WINDOW_WHEEL_RING;

        table->wheel_when[at][slot] = when, table->wheel_stream_flag[at][slot] = stream != 0;
        table->wheel_streams[at] += stream != 0;
}

struct window_wheel_report
{
        unsigned int notches, streams, peak, gaps;
        unsigned long long least, median;
};

static void window_wheel_report(const struct windows address_to table, unsigned int at, unsigned long long now, struct window_wheel_report address_to r)
{
        static unsigned long long kept[WINDOW_WHEEL_RING], gap[WINDOW_WHEEL_RING];
        unsigned int turns = table->wheel_turns[at], first = turns > WINDOW_WHEEL_RING ? turns - WINDOW_WHEEL_RING : 0, n = 0, second[5] = {0};

        memory_zero(r, sizeof(*r));
        for (unsigned int k = first; k < turns; k++)
        {
                unsigned int slot = k % WINDOW_WHEEL_RING;
                unsigned long long when = table->wheel_when[at][slot];

                if (when > now || now - when >= WINDOW_WHEEL_SPAN_NS)
                        continue;
                kept[n++] = when, r->notches++, r->streams += table->wheel_stream_flag[at][slot] != 0;
                second[(now - when) / 1000000000ull]++;
        }
        for (unsigned int k = 0; k < 5; k++)
                r->peak = second[k] > r->peak ? second[k] : r->peak;
        for (unsigned int k = 1; k < n; k++)
                if (kept[k] >= kept[k - 1])
                        gap[r->gaps++] = kept[k] - kept[k - 1];
        for (unsigned int k = 1; k < r->gaps; k++)
        {
                unsigned long long v = gap[k];
                unsigned int j = k;

                while (j > 0 && gap[j - 1] > v)
                        gap[j] = gap[j - 1], j--;
                gap[j] = v;
        }
        if (r->gaps)
                r->least = gap[0], r->median = gap[r->gaps / 2];
}

#define WINDOWS_HEADER ((sizeof(struct windows) + 4095) & ~4095ul)
#define WINDOWS_LISTS_AT(buffer, rows) (WINDOWS_HEADER + (unsigned long)(buffer) * 2 * (unsigned long)(rows))
#define WINDOW_LIST_BYTES (4ul << 20)
#define WINDOW_LIST_ROOM 16384u

static inline unsigned char *window_list_of(const struct windows *table, int row)
{
        if (!table || !table->lists || table->list_bytes != WINDOW_LIST_BYTES || row < 0 || (unsigned int)row >= windows_room(table))
                return 0;
        return (unsigned char *)table + table->lists + (unsigned long)row * table->list_bytes;
}

static inline unsigned int window_list_check(unsigned char *list, int width, int height, struct ui_draw_view *view)
{
        int opened;

        if (!list || width <= 0 || height <= 0)
                return WINDOW_LIST_NONE;
        opened = ui_draw_list_open(list, WINDOW_LIST_BYTES, view);
        if (opened <= 0)
                return opened == 0 ? WINDOW_LIST_PUBLISHING : WINDOW_LIST_HEADER;
        if (!ui_draw_list_still(list, view))
                return WINDOW_LIST_TORN;
        if (view->room != WINDOW_LIST_ROOM)
                return WINDOW_LIST_ROOM_OTHER;
        if (view->width != (unsigned int)width || view->height != (unsigned int)height)
                return WINDOW_LIST_SIZE;
        if (view->pitch < (unsigned long)width)
                return WINDOW_LIST_PITCH;
        if (view->x1 > view->x0 && view->y1 > view->y0 && (view->x0 < 0 || view->y0 < 0 || view->x1 > width || view->y1 > height))
                return WINDOW_LIST_DAMAGE;
        return WINDOW_LIST_VALID;
}

static inline int window_list_valid(unsigned char *list, int width, int height)
{
        struct ui_draw_view view;

        return window_list_check(list, width, height, &view) == WINDOW_LIST_VALID;
}

/* Issue board (seqlock + bounded spinlock) & GPU/kernel notice diagnostics. */
enum { WINDOWS_ISSUE_SPINS = 100000u, WINDOWS_ISSUE_TRIES = 16, WINDOWS_METRIC_RUN = 128 };

struct windows_issues
{
        unsigned int count, refused;
        struct windows_issue issue[WINDOWS_ISSUES];
};

static int windows_issue_same(const char *a, const char *b)
{
        for (unsigned int i = 0; i < WINDOWS_ISSUE_KEY; i++)
                if (a[i] != b[i] || !a[i])
                        return a[i] == b[i];
        return 1;
}

static unsigned int windows_issue_lock(struct windows *table)
{
        for (unsigned int spin = 0; spin < WINDOWS_ISSUE_SPINS; spin++)
        {
                unsigned int empty = 0;

                if (__atomic_compare_exchange_n(&table->issue_lock, &empty, 1u, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
                {
                        unsigned int seq = __atomic_load_n(&table->issue_seq, __ATOMIC_RELAXED) | 1u;

                        __atomic_store_n(&table->issue_seq, seq, __ATOMIC_RELAXED);
                        __atomic_thread_fence(__ATOMIC_RELEASE);
                        return seq;
                }
        }
        return 0;
}

static void windows_issue_unlock(struct windows *table, unsigned int seq)
{
        __atomic_store_n(&table->issue_seq, seq + 1, __ATOMIC_RELEASE);
        __atomic_store_n(&table->issue_lock, 0u, __ATOMIC_RELEASE);
}

static int windows_issue_set(struct windows *table, const char *key, unsigned int severity, unsigned int code, unsigned int a, unsigned int b,
                             const char *detail)
{
        int at = -1, empty = -1;
        unsigned int seq;

        if (!table || !key || !key[0] || (severity != WINDOWS_ISSUE_WARNING && severity != WINDOWS_ISSUE_ERROR) ||
            !code || code >= WINDOWS_ISSUE_COUNT || !(seq = windows_issue_lock(table)))
                return 0;
        for (int i = 0; i < WINDOWS_ISSUES; i++)
                if (table->issues[i].used && table->issues[i].code == code && windows_issue_same(table->issues[i].key, key))
                {
                        at = i;
                        break;
                }
                else if (!table->issues[i].used && empty < 0)
                        empty = i;
        if (at < 0 && empty < 0)
        {
                table->issue_refused++;
                windows_issue_unlock(table, seq);
                return 0;
        }
        struct windows_issue *issue = &table->issues[at >= 0 ? at : empty];

        if (at < 0)
                issue->used = 1, table->issue_count++;
        string_copy_bounded((string_address)issue->key, (string_address)key, WINDOWS_ISSUE_KEY);
        issue->severity = severity, issue->code = code, issue->a = a, issue->b = b;
        string_copy_bounded((string_address)issue->detail, (string_address)(detail ? detail : ""), WINDOWS_ISSUE_DETAIL);
        windows_issue_unlock(table, seq);
        return 1;
}

static unsigned int windows_issue_clear(struct windows *table, const char *key, unsigned int code)
{
        unsigned int cleared = 0, seq;

        if (!table || !key || !key[0] || !(seq = windows_issue_lock(table)))
                return 0;
        for (int i = 0; i < WINDOWS_ISSUES; i++)
                if (table->issues[i].used && (code == WINDOWS_ISSUE_ANY || table->issues[i].code == code) && windows_issue_same(table->issues[i].key, key))
                        memory_zero(&table->issues[i], sizeof(table->issues[i])), table->issue_count -= table->issue_count != 0, cleared++;
        if (table->issue_count < WINDOWS_ISSUES)
                table->issue_refused = 0;
        windows_issue_unlock(table, seq);
        return cleared;
}

static int windows_issues_read(const struct windows *table, struct windows_issues *into)
{
        for (int tries = 0; table && into && tries < WINDOWS_ISSUE_TRIES; tries++)
        {
                unsigned int before = __atomic_load_n(&table->issue_seq, __ATOMIC_ACQUIRE), count = 0;

                if (before & 1u)
                        continue;
                memory_copy(into->issue, table->issues, sizeof(table->issues));
                into->refused = table->issue_refused;
                __atomic_thread_fence(__ATOMIC_ACQUIRE);
                if (before != __atomic_load_n(&table->issue_seq, __ATOMIC_RELAXED))
                        continue;
                for (int i = 0; i < WINDOWS_ISSUES; i++)
                {
                        struct windows_issue *issue = &into->issue[i];

                        issue->key[WINDOWS_ISSUE_KEY - 1] = 0, issue->detail[WINDOWS_ISSUE_DETAIL - 1] = 0;
                        if (!issue->used || !issue->key[0] || (issue->severity != WINDOWS_ISSUE_WARNING && issue->severity != WINDOWS_ISSUE_ERROR))
                                memory_zero(issue, sizeof(*issue));
                        else
                                count++;
                }
                into->count = count;
                return 1;
        }
        if (into)
                memory_zero(into, sizeof(*into));
        return 0;
}

static unsigned int windows_issue_lines(const struct windows_issues *all, const char *key, unsigned int open)
{
        unsigned int lines = 0;

        for (int i = 0; open && all && key && i < WINDOWS_ISSUES; i++)
                if (all->issue[i].used && windows_issue_same(all->issue[i].key, key))
                        lines += 1 + (all->issue[i].detail[0] && all->issue[i].code != WINDOWS_ISSUE_GPU_REQUIRES);
        return lines;
}

static unsigned int windows_issues_scan(const struct windows_issues *all, const char *key, unsigned int *count)
{
        unsigned int worst = 0;

        if (count)
                *count = 0;
        for (int i = 0; all && i < WINDOWS_ISSUES; i++)
                if (all->issue[i].used && (key ? windows_issue_same(all->issue[i].key, key) : all->issue[i].key[0] != 0))
                {
                        worst = all->issue[i].severity > worst ? all->issue[i].severity : worst;
                        if (count)
                                (*count)++;
                }
        return worst;
}

static unsigned int windows_issues_worst(const struct windows_issues *all, const char *key, unsigned int *count)
{
        return key ? windows_issues_scan(all, key, count) : (count ? (*count = 0) : 0u);
}

static unsigned int windows_issues_summary(const struct windows_issues *all, unsigned int *count)
{
        return windows_issues_scan(all, 0, count);
}

static void windows_metric_stats(const unsigned long long *ring, unsigned int count, unsigned int *mean_us, unsigned int *worst_us)
{
        unsigned long long sum = 0, worst = 0;

        count = count > WINDOWS_METRIC_RUN ? WINDOWS_METRIC_RUN : count;
        for (unsigned int i = 0; i < count; i++)
                sum += ring[i], worst = ring[i] > worst ? ring[i] : worst;
        *mean_us = count ? (unsigned int)(sum / count / 1000) : 0, *worst_us = (unsigned int)(worst / 1000);
}

static unsigned int windows_metric_rate(unsigned long long count, unsigned long long nanoseconds)
{
        return nanoseconds ? (unsigned int)(count * 1000000000ull / nanoseconds) : 0;
}

static const unsigned int windows_gpu_codes[] = {
        WINDOWS_ISSUE_GPU_USERPTR, WINDOWS_ISSUE_GPU_MAP, WINDOWS_ISSUE_GPU_UNTRUSTED, WINDOWS_ISSUE_GPU_FAMILY, WINDOWS_ISSUE_GPU_COOLING,
        WINDOWS_ISSUE_GPU_REFUSED, WINDOWS_ISSUE_GPU_LISTS, WINDOWS_ISSUE_GPU_LOST, WINDOWS_ISSUE_GPU_SCANOUT,
        WINDOWS_ISSUE_GPU_HUNG, WINDOWS_ISSUE_GPU_FAULT};
#define WINDOWS_GPU_CODES (sizeof(windows_gpu_codes) / sizeof(windows_gpu_codes[0]))
enum
{
        WINDOWS_GPU_UP = 0u, WINDOWS_GPU_UNTRUSTED, WINDOWS_GPU_NO_CARD, WINDOWS_GPU_COOLING, WINDOWS_GPU_HUNG,
        WINDOWS_GPU_STATUS_OFF = 0u, WINDOWS_GPU_STATUS_NONE, WINDOWS_GPU_STATUS_IN_USE, WINDOWS_GPU_STATUS_PARTLY, WINDOWS_GPU_STATUS_NOT,
        WINDOWS_GPU_REFUSAL_MIN = 8u, WINDOWS_GPU_REFUSAL_SHARE = 20u, WINDOWS_GPU_LOST_MIN = 8u
};

static int windows_has_word(const char *text, const char *word)
{
        for (unsigned int at = 0; text[at]; at++)
        {
                unsigned int k = 0;

                while (word[k] && text[at + k] == word[k])
                        k++;
                if (!word[k])
                        return 1;
        }
        return 0;
}

static void windows_want_push(struct windows_issue *want, unsigned int *count, unsigned int room, unsigned int sev, unsigned int cod, unsigned int x,
                              unsigned int y, const char *text)
{
        if (*count >= room)
                return;
        struct windows_issue *w = &want[(*count)++];

        memory_zero(w, sizeof(*w));
        w->severity = sev, w->code = cod, w->a = x, w->b = y;
        string_copy_bounded((string_address)w->detail, (string_address)text, WINDOWS_ISSUE_DETAIL);
}

static unsigned int windows_gpu_wanted(const struct windows *table, struct windows_issue *want, unsigned int room)
{
        unsigned int count = 0, step, error, total, refused_top = 0, refused_why = 0, tried = 0, lost = 0;

        if (!table || !want || !table->gpu_on)
                return 0;
        step = table->gpu_map_why & 255u, error = table->gpu_map_why >> 8;
        if (step && step != 9u)
                windows_want_push(want, &count, room, WINDOWS_ISSUE_ERROR,
                                  windows_has_word(table->gpu_said, "HMM_MIRROR") || (step == 3 && error == 19)
                                      ? WINDOWS_ISSUE_GPU_USERPTR : WINDOWS_ISSUE_GPU_MAP,
                                  step, error, table->gpu_said);
        if (table->gpu_state == WINDOWS_GPU_UNTRUSTED)
                windows_want_push(want, &count, room, WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_UNTRUSTED, table->gpu_family, 0, "");
        if (table->gpu_state == WINDOWS_GPU_NO_CARD && table->gpu_family)
        {
                char driver[sizeof(table->gpu_req_driver) + 1];

                memory_copy(driver, table->gpu_req_driver, sizeof(table->gpu_req_driver)), driver[sizeof(table->gpu_req_driver)] = 0;
                windows_want_push(want, &count, room, WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_FAMILY, table->gpu_family, 0, driver);
        }
        if (table->gpu_state == WINDOWS_GPU_COOLING)
                windows_want_push(want, &count, room, WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_COOLING, table->gpu_steps, 0, "");
        if (table->gpu_state == WINDOWS_GPU_HUNG)
                windows_want_push(want, &count, room, WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_HUNG, 0, 0, "");
        if (table->gpu_fault[0])
                windows_want_push(want, &count, room, WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_FAULT, table->gpu_faults, 0, table->gpu_fault);
        for (unsigned int why = 5; why < WINDOWS_GPU_REASONS; why++)
                if (table->gpu_refused[why] > refused_top)
                        refused_top = table->gpu_refused[why], refused_why = why;
        total = table->gpu_drawn + table->gpu_processor;
        if (refused_top >= WINDOWS_GPU_REFUSAL_MIN && refused_top * WINDOWS_GPU_REFUSAL_SHARE >= total && refused_why)
                windows_want_push(want, &count, room, WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_REFUSED, refused_top, refused_why, windows_gpu_reasons[refused_why]);
        if (!table->gpu_able && table->gpu_state == WINDOWS_GPU_UP && total > 0)
                windows_want_push(want, &count, room, WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_LISTS, 0, 0, "");
        for (unsigned int row = 0; row < windows_rows(table); row++)
                if (table->pid[row] && table->list_tried[row])
                {
                        tried += table->list_tried[row];
                        for (unsigned int why = 0; why < UI_LOST_COUNT; why++)
                                lost += table->list_lost[row][why];
                }
        if (lost >= WINDOWS_GPU_LOST_MIN && lost * 10 >= tried)
                windows_want_push(want, &count, room, WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_LOST, lost, tried, "");
        if (table->refused && table->refused_error)
                windows_want_push(want, &count, room, WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_SCANOUT, table->refused_error, 0, "");
        return count;
}

static unsigned int windows_gpu_judge(const struct windows *table, unsigned int previous, unsigned int drawn, unsigned int processor)
{
        unsigned int total = drawn + processor, share, hi = previous == WINDOWS_GPU_STATUS_IN_USE ? 65u : 85u;

        if (!table->gpu_on)
                return WINDOWS_GPU_STATUS_OFF;
        if (table->gpu_state == WINDOWS_GPU_NO_CARD)
                return WINDOWS_GPU_STATUS_NONE;
        if (total < 16)
                return previous == WINDOWS_GPU_STATUS_OFF || previous == WINDOWS_GPU_STATUS_NONE ? WINDOWS_GPU_STATUS_NOT : previous;
        share = drawn * 100 / total;
        return share >= hi ? WINDOWS_GPU_STATUS_IN_USE : share <= 15u ? WINDOWS_GPU_STATUS_NOT : WINDOWS_GPU_STATUS_PARTLY;
}

/* Kernel /dev/kmsg notice feeder. */
#define KMSG_FILE "/dev/kmsg"
#define KERNEL_NOTICE_AGE_NS (600ull * 1000000000ull)
enum { KERNEL_NOTICE_MOST = 256, KERNEL_NOTICE_LINE = 1024 };
static const char *const kernel_notice_key[KERNEL_NOTICE_CLASSES] = {
#define WINDOWS_KERNEL_KEY(name, key, text) key,
    WINDOWS_KERNEL_CLASSES(WINDOWS_KERNEL_KEY)
#undef WINDOWS_KERNEL_KEY
};
static const char *const kernel_notice_quiet[] = {
    "LTTPR count is nonzero", "ACPI BIOS Error", "due to previous error", "Direct firmware load", "[Firmware Bug]",
    "no codecs found", "RDSEED32 is broken", "kauditd_printk_skb", "Moonwater starting", "Spark format unregistered", 0};

struct kernel_notices
{
        int fd;
        unsigned int count[KERNEL_NOTICE_CLASSES], severity[KERNEL_NOTICE_CLASSES];
        unsigned long long seen[KERNEL_NOTICE_CLASSES];
        char last[KERNEL_NOTICE_CLASSES][WINDOWS_ISSUE_DETAIL];
};
static struct kernel_notices kernel_notice_state = {.fd = -1};

static int kernel_notice_has(const char *text, unsigned int length, const char *words)
{
        for (unsigned int from = 0, to = 0, k; words[from]; from = to + (words[to] == '|'))
        {
                for (to = from; words[to] && words[to] != '|'; to++)
                        ;
                for (unsigned int at = 0; at + to - from <= length; at++)
                {
                        if (at && (text[at - 1] | 32) >= 'a' && (text[at - 1] | 32) <= 'z')
                                continue;
                        for (k = 0; k < to - from && text[at + k] == words[from + k]; k++)
                                ;
                        if (k == to - from)
                                return 1;
                }
        }
        return 0;
}

static const struct { unsigned int code, severity, level; const char *words, *needs; } kernel_notice_rules[] = {
    {WINDOWS_ISSUE_KERNEL_SYSTEM, WINDOWS_ISSUE_ERROR, 4, "Out of memory|oom_kill_process|Killed process|BUG:|Oops|Kernel panic|WARNING: CPU", null},
    {WINDOWS_ISSUE_KERNEL_PROGRAM, WINDOWS_ISSUE_WARNING, 7, "segfault at|traps:", null},
    {WINDOWS_ISSUE_KERNEL_GPU, 0, 4, "amdgpu|drm|i915", "timeout|reset|fault|failed"},
    {WINDOWS_ISSUE_KERNEL_GPU, WINDOWS_ISSUE_WARNING, 7, "i915", "disabling eDP"},
    {WINDOWS_ISSUE_KERNEL_USB, 0, 4, "xhci|usb", "overrun|failed"},
    {WINDOWS_ISSUE_KERNEL_WIFI, 0, 4, "iwlwifi|ath1|ath9|mt76|mt79|brcm|cfg80211", "firmware|timeout|error"},
    {WINDOWS_ISSUE_KERNEL_BLUETOOTH, 0, 4, "Bluetooth:", "failed|error"},
    {WINDOWS_ISSUE_KERNEL_AUDIO, 0, 4, "snd_|hda", "no codecs|error|failed"},
    {WINDOWS_ISSUE_KERNEL_STORAGE, 0, 4, "nvme|ext4|btrfs|xfs|mmc|FAT-fs|I/O error|Buffer I/O", "I/O error|Buffer I/O|not properly unmounted|error|timeout"},
    {WINDOWS_ISSUE_KERNEL_SYSTEM, 0, 4, "thermal|throttl|Hardware Error|mce:|EDAC", null}};

static unsigned int kernel_notice_class(const char *text, unsigned int length, unsigned int level, unsigned int *severity)
{
        *severity = 0;
        for (int q = 0; kernel_notice_quiet[q]; q++)
                if (kernel_notice_has(text, length, kernel_notice_quiet[q]))
                        return 0;
        for (unsigned int r = 0; r < sizeof(kernel_notice_rules) / sizeof(kernel_notice_rules[0]); r++)
                if (level <= kernel_notice_rules[r].level && kernel_notice_has(text, length, kernel_notice_rules[r].words) &&
                    (!kernel_notice_rules[r].needs || level <= 3 || kernel_notice_has(text, length, kernel_notice_rules[r].needs)))
                {
                        *severity = kernel_notice_rules[r].severity ? kernel_notice_rules[r].severity : level <= 3 ? WINDOWS_ISSUE_ERROR : WINDOWS_ISSUE_WARNING;
                        return kernel_notice_rules[r].code;
                }
        return level <= 3 ? (*severity = WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_KERNEL_SYSTEM) : 0;
}

static int kernel_notice_parse(const char *record, unsigned int length, unsigned int *level, const char **message, unsigned int *message_length)
{
        unsigned int at = 0, priority = 0, start, stop;

        while (at < length && record[at] >= '0' && record[at] <= '9')
        {
                if ((priority = priority * 10 + (unsigned int)(record[at++] - '0')) > 100000u)
                        return 0;
        }
        if (!at || at >= length || record[at] != ',')
                return 0;
        while (at < length && record[at] != ';' && record[at] != '\n')
                at++;
        if (at >= length || record[at] != ';')
                return 0;
        start = ++at;
        for (stop = start; stop < length && record[stop] != '\n'; stop++)
                ;
        *level = priority & 7u, *message = record + start, *message_length = stop - start;
        return 1;
}

static void kernel_notice_sanitize(char *into, unsigned int room, const char *text, unsigned int length)
{
        unsigned int at = 0;

        for (unsigned int i = 0; i < length && at + 1 < room; i++)
        {
                unsigned char c = (unsigned char)text[i];

                into[at++] = c < 0x20 || c == 0x7f ? ' ' : (char)c;
        }
        into[at] = 0;
}

static int kernel_notices_record(struct kernel_notices *k, const char *record, unsigned int length, unsigned long long now)
{
        unsigned int level = 0, severity = 0, message_length = 0, code, idx;
        const char *message = null;

        if (!kernel_notice_parse(record, length, &level, &message, &message_length) ||
            !(code = kernel_notice_class(message, message_length, level, &severity)))
                return 0;
        idx = code - WINDOWS_ISSUE_KERNEL_GPU;
        if (k->count[idx] < 0xffffffffu)
                k->count[idx]++;
        if (severity > k->severity[idx])
                k->severity[idx] = severity;
        k->seen[idx] = now;
        kernel_notice_sanitize(k->last[idx], sizeof(k->last[idx]), message, message_length);
        return 1;
}

static void kernel_notices_publish(struct windows *table, struct kernel_notices *k, unsigned long long now)
{
        for (unsigned int idx = 0; idx < KERNEL_NOTICE_CLASSES; idx++)
        {
                unsigned int code = WINDOWS_ISSUE_KERNEL_GPU + idx;

                if (table && (__atomic_load_n(&table->kernel_dismiss, __ATOMIC_ACQUIRE) >> idx & 1u))
                        k->count[idx] = k->severity[idx] = 0, __atomic_and_fetch(&table->kernel_dismiss, ~(1u << idx), __ATOMIC_RELEASE);
                if (!k->count[idx])
                        continue;
                if (k->severity[idx] == WINDOWS_ISSUE_WARNING && k->seen[idx] && now - k->seen[idx] > KERNEL_NOTICE_AGE_NS)
                {
                        if (table)
                                windows_issue_clear(table, kernel_notice_key[idx], code);
                        k->count[idx] = k->severity[idx] = 0;
                        continue;
                }
                if (table)
                        windows_issue_set(table, kernel_notice_key[idx], k->severity[idx], code, k->count[idx] > 65535u ? 65535u : k->count[idx],
                                          (unsigned int)((now - k->seen[idx]) / 1000000000ull), k->last[idx]);
        }
}


static unsigned int windows_language_code(const char *word, unsigned int length)
{
        unsigned int code = 0;

        if (length < 2 || length > 3)
                return 0;
        for (unsigned int at = 0; at < length; at++)
        {
                if (word[at] < 'a' || word[at] > 'z')
                        return 0;
                code |= (unsigned int)word[at] << 8 * at;
        }
        return code;
}

/* Output arrangement, displays file, and software rotation. */
enum { WINDOWS_PLACE_FLOW = 0, WINDOWS_PLACE_AT = 1, WINDOWS_PLACE_OFF = 2, WINDOWS_PLACE_MOST = 32768 };

static int windows_buf_put(char *into, unsigned int room, unsigned int *at, const char *text, unsigned int length)
{
        if (room < *at || room - *at <= length)
                return 0;
        if (length)
                memory_copy(into + *at, text, length);
        *at += length, into[*at] = 0;
        return 1;
}

static int windows_buf_str(char *into, unsigned int room, unsigned int *at, const char *text)
{
        return windows_buf_put(into, room, at, text, (unsigned int)string_length((const_string)text));
}

static int windows_buf_num(char *into, unsigned int room, unsigned int *at, long value)
{
        char digits[24];
        unsigned int count = 0;
        unsigned long magnitude = value < 0 ? 0ul - (unsigned long)value : (unsigned long)value;

        do
                digits[count++] = (char)('0' + magnitude % 10), magnitude /= 10;
        while (magnitude);
        if (value < 0 && !windows_buf_put(into, room, at, "-", 1))
                return 0;
        while (count)
                if (!windows_buf_put(into, room, at, digits + --count, 1))
                        return 0;
        return 1;
}

static int windows_buf_kv(char *into, unsigned int room, unsigned int *at, const char *key, long value)
{
        return windows_buf_str(into, room, at, key) && windows_buf_num(into, room, at, value);
}

static void windows_num_path(char *out, unsigned int room, const char *pre, unsigned long num, const char *post)
{
        unsigned int at = 0;

        if (!room)
                return;
        out[0] = 0;
        if (pre && *pre)
                windows_buf_str(out, room, &at, pre);
        windows_buf_num(out, room, &at, (long)num);
        if (post && *post)
                windows_buf_str(out, room, &at, post);
}

static void windows_num2_path(char *out, unsigned int room, const char *pre, unsigned long a, const char *mid, unsigned long b)
{
        unsigned int at = 0;

        if (!room)
                return;
        out[0] = 0;
        if (pre && *pre)
                windows_buf_str(out, room, &at, pre);
        windows_buf_num(out, room, &at, (long)a);
        if (mid && *mid)
                windows_buf_str(out, room, &at, mid);
        windows_buf_num(out, room, &at, (long)b);
}

static int windows_scan_int(const char *word, unsigned int length, int low, int high, unsigned int max_digits, int strict_zero, int *into)
{
        unsigned int at = 0, digits = 0;
        int negative = 0;
        long value = 0;

        if (low < 0 && at < length && word[at] == '-')
                negative = 1, at++;
        if (strict_zero && at + 1 < length && word[at] == '0' && word[at + 1] >= '0' && word[at + 1] <= '9')
                return 0;
        for (; at < length; at++, digits++)
        {
                if (word[at] < '0' || word[at] > '9' || digits == max_digits)
                        return 0;
                value = value * 10 + (word[at] - '0');
        }
        if (!digits || (strict_zero && negative && !value))
                return 0;
        value = negative ? -value : value;
        if (value < low || value > high)
                return 0;
        *into = (int)value;
        return 1;
}

static int windows_arrange_word(const char *word, unsigned int length, unsigned int *name, int *how, int *x, int *y)
{
        unsigned int at = 0, comma;
        const char *hit;

        while (at < length && ((unsigned int)((word[at] | 32) - 'a') < 26u || (word[at] >= '0' && word[at] <= '9') || word[at] == '-'))
                at++;
        *name = at, *how = WINDOWS_PLACE_FLOW, *x = *y = 0;
        if (!at || at >= WINDOWS_OUTPUT_NAME)
                return 0;
        if (at == length)
                return 1;
        if (word[at++] != '@')
                return 0;
        if (length - at == 3 && !memory_compare(word + at, "off", 3))
                return *how = WINDOWS_PLACE_OFF, 1;
        hit = (const char *)memory_first_of(word + at, ',', length - at);
        comma = hit ? (unsigned int)(hit - word) : length;
        if (comma == length || !windows_scan_int(word + at, comma - at, -WINDOWS_PLACE_MOST, WINDOWS_PLACE_MOST, 6, 0, x) ||
            !windows_scan_int(word + comma + 1, length - comma - 1, -WINDOWS_PLACE_MOST, WINDOWS_PLACE_MOST, 6, 0, y))
                return 0;
        return *how = WINDOWS_PLACE_AT, 1;
}

static int windows_word_next(const char *text, unsigned int *start, unsigned int *stop)
{
        unsigned int at = *start;

        while (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')
                at++;
        if (!text[at])
                return 0;
        *start = at;
        while (text[at] && text[at] != ' ' && text[at] != '\t' && text[at] != '\n' && text[at] != '\r')
                at++;
        *stop = at;
        return 1;
}

static int windows_arrange_valid(const char *text)
{
        unsigned int start = 0, stop, words = 0, starts[WINDOWS_OUTPUTS * 2], names[WINDOWS_OUTPUTS * 2];

        for (; windows_word_next(text, &start, &stop); start = stop)
        {
                unsigned int name;
                int how, x, y;

                if (stop - start == 7 && !memory_compare(text + start, "default", 7))
                {
                        unsigned int next_s = stop, next_e;

                        return !words && !windows_word_next(text, &next_s, &next_e);
                }
                if (words == WINDOWS_OUTPUTS * 2 || !windows_arrange_word(text + start, stop - start, &name, &how, &x, &y))
                        return 0;
                for (unsigned int other = 0; other < words; other++)
                        if (names[other] == name && !memory_compare(text + starts[other], text + start, name))
                                return 0;
                starts[words] = start, names[words++] = name;
        }
        return 1;
}

static unsigned int windows_arrange_lay(const char *text, const char (*names)[WINDOWS_OUTPUT_NAME], const int (*sizes)[2],
                                        unsigned int count, int (*rects)[4], unsigned char *order)
{
        unsigned char laid[WINDOWS_OUTPUTS] = {0}, placed[WINDOWS_OUTPUTS] = {0}, dark[WINDOWS_OUTPUTS] = {0};
        int place[WINDOWS_OUTPUTS][2] = {{0}}, valid = windows_arrange_valid(text), right = 0, low_x = 0, low_y = 0;
        unsigned int lit = 0, start = 0, stop, used = 0;

        count = count < WINDOWS_OUTPUTS ? count : WINDOWS_OUTPUTS;
        for (; valid && windows_word_next(text, &start, &stop); start = stop)
        {
                unsigned int name;
                int how, x, y;

                if (!windows_arrange_word(text + start, stop - start, &name, &how, &x, &y))
                        continue;
                for (unsigned int s = 0; s < count; s++)
                        if (!laid[s] && !names[s][name] && !memory_compare(names[s], text + start, name))
                                laid[s] = 1, dark[s] = how == WINDOWS_PLACE_OFF, placed[s] = how == WINDOWS_PLACE_AT,
                                place[s][0] = x, place[s][1] = y, order[used++] = (unsigned char)s;
        }
        for (unsigned int s = 0; s < count; s++)
        {
                if (!laid[s])
                        order[used++] = (unsigned char)s;
                lit += !dark[s];
        }
        if (!lit && count)
                dark[0] = 0;
        lit = 0;
        for (unsigned int each = 0; each < used; each++)
        {
                unsigned int s = order[each];

                if (dark[s])
                        continue;
                rects[s][0] = placed[s] ? place[s][0] : right, rects[s][1] = placed[s] ? place[s][1] : 0;
                rects[s][2] = sizes[s][0], rects[s][3] = sizes[s][1];
                right = !lit || rects[s][0] + rects[s][2] > right ? rects[s][0] + rects[s][2] : right;
                low_x = !lit || rects[s][0] < low_x ? rects[s][0] : low_x;
                low_y = !lit || rects[s][1] < low_y ? rects[s][1] : low_y;
                order[lit++] = (unsigned char)s;
        }
        for (unsigned int s = 0; s < count; s++)
                if (dark[s])
                        rects[s][0] = rects[s][1] = rects[s][2] = rects[s][3] = 0;
                else
                        rects[s][0] -= low_x, rects[s][1] -= low_y;
        return lit;
}

enum
{
        WINDOWS_DISPLAYS = 32, WINDOWS_DISPLAY_NAME = 1u, WINDOWS_DISPLAY_ROTATION = 2u, WINDOWS_DISPLAY_REFRESH = 4u,
        WINDOWS_DISPLAY_X = 8u, WINDOWS_DISPLAY_Y = 16u, WINDOWS_DISPLAY_OTHER = 32u, WINDOWS_DISPLAY_SCALE = 64u,
        WINDOWS_DISPLAY_MODE = 128u, WINDOWS_DISPLAY_PRIMARY = 256u, WINDOWS_DISPLAY_SIDE = 16384u,
        WINDOWS_DISPLAY_HZ = 1000u, WINDOWS_SCALE_DEFAULT = 100u
};
static const unsigned int windows_scale_presets[] = {50, 75, 100, 125, 150, 200};

static int windows_scale_ok(int percent)
{
        for (unsigned int at = 0; at < sizeof(windows_scale_presets) / sizeof(windows_scale_presets[0]); at++)
                if (percent == (int)windows_scale_presets[at])
                        return 1;
        return 0;
}

struct windows_display
{
        char name[WINDOWS_OUTPUT_NAME];
        unsigned int rotation, refresh, width, height, primary, scale;
        int x, y;
        unsigned int placed, bad;
};

static int windows_display_name(const char *word, unsigned int length, char *name)
{
        if (!length || length >= WINDOWS_OUTPUT_NAME)
                return 0;
        for (unsigned int at = 0; at < length; at++)
                if (!((unsigned int)((word[at] | 32) - 'a') < 26u || (word[at] >= '0' && word[at] <= '9') || word[at] == '-'))
                        return 0;
        memory_copy(name, word, length), name[length] = 0;
        return 1;
}

static int windows_name_is(const char *name, const char *word, unsigned int length)
{
        return length < WINDOWS_OUTPUT_NAME && !name[length] && !memory_compare(name, word, length);
}

static int windows_display_same_name(const char *a, const char *b)
{
        unsigned int at = 0;

        while (at < WINDOWS_OUTPUT_NAME - 1 && a[at] && a[at] == b[at])
                at++;
        return a[at] == b[at];
}

static int windows_display_size(const char *word, unsigned int length, unsigned int *width, unsigned int *height)
{
        unsigned int cross = 0;
        int w, h;

        if (length == 9 && windows_name_is("preferred", word, 9))
                return *width = *height = 0, 1;
        while (cross < length && word[cross] != 'x')
                cross++;
        if (cross == length || !windows_scan_int(word, cross, 1, (int)WINDOWS_DISPLAY_SIDE, 5, 0, &w) ||
            !windows_scan_int(word + cross + 1, length - cross - 1, 1, (int)WINDOWS_DISPLAY_SIDE, 5, 0, &h))
                return 0;
        return *width = (unsigned int)w, *height = (unsigned int)h, 1;
}

static int windows_display_value(unsigned int field, const char *word, unsigned int length, int *into)
{
        int value = 0;

        if (field == WINDOWS_DISPLAY_REFRESH && length == 9 && windows_name_is("preferred", word, 9))
                return *into = 0, 1;
        if (field == WINDOWS_DISPLAY_ROTATION)
        {
                if (!windows_scan_int(word, length, 0, 270, 3, 0, &value) || value % 90)
                        return 0;
        }
        else if (field == WINDOWS_DISPLAY_SCALE)
        {
                if (!windows_scan_int(word, length, 0, 999, 3, 0, &value) || !windows_scale_ok(value))
                        return 0;
        }
        else if (field == WINDOWS_DISPLAY_REFRESH)
        {
                if (!windows_scan_int(word, length, 1, (int)WINDOWS_DISPLAY_HZ, 4, 0, &value))
                        return 0;
        }
        else if (!windows_scan_int(word, length, -WINDOWS_PLACE_MOST, WINDOWS_PLACE_MOST, 6, 0, &value))
                return 0;
        return *into = value, 1;
}

static void windows_display_line(const char *line, unsigned int length, struct windows_display *display)
{
        static const struct { const char *name; unsigned char len; unsigned short bit; } fields[] = {
            {"rotation", 8, WINDOWS_DISPLAY_ROTATION}, {"refresh", 7, WINDOWS_DISPLAY_REFRESH}, {"scale", 5, WINDOWS_DISPLAY_SCALE},
            {"mode", 4, WINDOWS_DISPLAY_MODE}, {"primary", 7, WINDOWS_DISPLAY_PRIMARY}, {"x", 1, WINDOWS_DISPLAY_X}, {"y", 1, WINDOWS_DISPLAY_Y},
        };
        unsigned int at = 0, start, seen = 0, have_x = 0, have_y = 0;
        int x = 0, y = 0;

        memory_zero(display, sizeof(*display));
        display->scale = WINDOWS_SCALE_DEFAULT;
        while (at < length && (line[at] == ' ' || line[at] == '\t'))
                at++;
        start = at;
        while (at < length && line[at] != ' ' && line[at] != '\t')
                at++;
        if (!windows_display_name(line + start, at - start, display->name))
        {
                display->bad = WINDOWS_DISPLAY_NAME;
                return;
        }
        for (;;)
        {
                unsigned int word, equals, key = 0, val_len;
                const char *val;
                int value;

                while (at < length && (line[at] == ' ' || line[at] == '\t'))
                        at++;
                if (at == length)
                        break;
                for (word = at; at < length && line[at] != ' ' && line[at] != '\t'; at++)
                        ;
                for (equals = word; equals < at && line[equals] != '='; equals++)
                        ;
                for (unsigned int f = 0; equals > word && equals < at && f < sizeof(fields) / sizeof(fields[0]); f++)
                        if (equals - word == fields[f].len && windows_name_is(fields[f].name, line + word, fields[f].len))
                        {
                                key = fields[f].bit;
                                break;
                        }
                if (!key || (seen & key))
                {
                        display->bad |= WINDOWS_DISPLAY_OTHER;
                        continue;
                }
                seen |= key, val = line + equals + 1, val_len = at - equals - 1;
                if (key == WINDOWS_DISPLAY_PRIMARY)
                {
                        if (val_len == 3 && windows_name_is("yes", val, 3))
                                display->primary = 1;
                        else if (!(val_len == 2 && windows_name_is("no", val, 2)))
                                display->bad |= key;
                }
                else if (key == WINDOWS_DISPLAY_MODE)
                        display->bad |= windows_display_size(val, val_len, &display->width, &display->height) ? 0u : key;
                else if (!windows_display_value(key, val, val_len, &value))
                        display->bad |= key;
                else if (key == WINDOWS_DISPLAY_ROTATION) display->rotation = (unsigned int)value;
                else if (key == WINDOWS_DISPLAY_SCALE) display->scale = (unsigned int)value;
                else if (key == WINDOWS_DISPLAY_REFRESH) display->refresh = (unsigned int)value;
                else if (key == WINDOWS_DISPLAY_X) x = value, have_x = 1;
                else y = value, have_y = 1;
        }
        if (have_x && have_y)
                display->placed = 1, display->x = x, display->y = y;
        else if (have_x || have_y)
                display->bad |= have_x ? WINDOWS_DISPLAY_Y : WINDOWS_DISPLAY_X;
}

static unsigned int windows_displays_read(const char *text, struct windows_display *list, unsigned int room)
{
        unsigned int count = 0, at = 0;

        while (text[at] && count < room)
        {
                unsigned int start = at, blank = 1;

                while (text[at] && text[at] != '\n')
                        blank &= text[at] == ' ' || text[at] == '\t' || text[at] == '\r', at++;
                if (!blank)
                {
                        windows_display_line(text + start, at - start, &list[count]);
                        for (unsigned int earlier = 0; earlier < count && list[count].name[0]; earlier++)
                                if (list[earlier].name[0] && windows_display_same_name(list[earlier].name, list[count].name))
                                        list[count].bad |= WINDOWS_DISPLAY_OTHER;
                        count++;
                }
                at += text[at] == '\n';
        }
        return count;
}

static const struct windows_display *windows_display_of(const struct windows_display *list, unsigned int count,
                                                        const char *word, unsigned int length)
{
        for (unsigned int at = 0; at < count; at++)
                if (list[at].name[0] && windows_name_is(list[at].name, word, length))
                        return &list[at];
        return 0;
}

static unsigned int windows_mode_at(const unsigned int (*modes)[3], unsigned int count, unsigned int width,
                                    unsigned int height, unsigned int hz)
{
        for (unsigned int at = 0; at < count; at++)
                if (modes[at][0] == width && modes[at][1] == height && (!hz || modes[at][2] == hz))
                        return at;
        return count;
}

static unsigned long windows_rotation_bit(const char *const *names, const unsigned long *values, unsigned int count, unsigned int degrees)
{
        static const char *const wanted[] = {"rotate-0", "rotate-90", "rotate-180", "rotate-270"};

        if (degrees % 90 || degrees > 270)
                return 0;
        for (unsigned int at = 0; at < count; at++)
                if (windows_display_same_name(names[at], wanted[degrees / 90]) && values[at] < 32)
                        return 1ul << values[at];
        return 0;
}

static void windows_rotate_at(unsigned int lw, unsigned int lh, unsigned int degrees, unsigned int lx, unsigned int ly,
                              unsigned int *px, unsigned int *py)
{
        if (degrees == 90) *px = lh - 1 - ly, *py = lx;
        else if (degrees == 180) *px = lw - 1 - lx, *py = lh - 1 - ly;
        else if (degrees == 270) *px = ly, *py = lw - 1 - lx;
        else *px = lx, *py = ly;
}

#define WINDOWS_TURN_TILE 16u

static void windows_rotate_u32(unsigned int *into, unsigned long into_pitch, const unsigned int *from, unsigned long from_pitch,
                               unsigned int lw, unsigned int lh, unsigned int degrees, unsigned int x0, unsigned int y0,
                               unsigned int x1, unsigned int y1)
{
        if (degrees == 90 || degrees == 270)
        {
                int cw = degrees == 90;
                unsigned int pa = cw ? lh - y1 : y0, pb = cw ? lh - y0 : y1, qa = cw ? x0 : lw - x1, qb = cw ? x1 : lw - x0;
                long step = cw ? -(long)from_pitch : (long)from_pitch;

                for (unsigned int p = pa, pe; p < pb; p = pe)
                {
                        pe = (p / WINDOWS_TURN_TILE + 1) * WINDOWS_TURN_TILE;
                        pe = pe < pb ? pe : pb;
                        for (unsigned int q = qa; q < qb; q += WINDOWS_TURN_TILE)
                        {
                                unsigned int qe = q + WINDOWS_TURN_TILE < qb ? q + WINDOWS_TURN_TILE : qb;
                                const unsigned int *base = cw ? from + (unsigned long)(lh - 1 - p) * from_pitch
                                                              : from + (unsigned long)p * from_pitch;

                                if (qe - q >= 4 && pe - p == WINDOWS_TURN_TILE)
                                {
                                        memory_transpose_u32(into + (unsigned long)q * into_pitch + p, into_pitch,
                                                             base + (cw ? q : lw - 1 - q), step, cw ? 1 : -1, qe - q, pe - p);
                                        continue;
                                }
                                for (unsigned int row = q; row < qe; row++)
                                {
                                        unsigned int *to = into + (unsigned long)row * into_pitch;
                                        const unsigned int *src = base + (cw ? row : lw - 1 - row);

                                        for (unsigned int px = p; px < pe; px++, src += step)
                                                to[px] = *src;
                                }
                        }
                }
                return;
        }
        if (degrees == 180)
        {
                memory_copy_rect_reverse_u32(into + (unsigned long)(lh - 1 - y0) * into_pitch + (lw - x1), -(long)into_pitch,
                                             from + (unsigned long)y0 * from_pitch + x0, (long)from_pitch, x1 - x0, y1 - y0);
                return;
        }
        for (unsigned int ly = y0; ly < y1; ly++)
                memory_copy(into + (unsigned long)ly * into_pitch + x0, from + (unsigned long)ly * from_pitch + x0, (positive)(x1 - x0) * 4);
}

static void windows_rotate_panel(unsigned int lw, unsigned int lh, unsigned int degrees, unsigned int x0, unsigned int y0,
                                 unsigned int x1, unsigned int y1, unsigned int *panel)
{
        unsigned int corner[4][2] = {{x0, y0}, {x1 - 1, y0}, {x0, y1 - 1}, {x1 - 1, y1 - 1}};

        panel[0] = panel[1] = ~0u, panel[2] = panel[3] = 0;
        for (unsigned int each = 0; each < 4; each++)
        {
                unsigned int px, py;

                windows_rotate_at(lw, lh, degrees, corner[each][0], corner[each][1], &px, &py);
                panel[0] = px < panel[0] ? px : panel[0], panel[1] = py < panel[1] ? py : panel[1];
                panel[2] = px + 1 > panel[2] ? px + 1 : panel[2], panel[3] = py + 1 > panel[3] ? py + 1 : panel[3];
        }
}

static void windows_line_first_word(const char *text, unsigned int start, unsigned int *stop, unsigned int *word, unsigned int *word_end)
{
        unsigned int s = (unsigned int)(string_first_of_or_end((string_address)text + start, '\n') - (string_address)text), w = start, e;

        while (w < s && (text[w] == ' ' || text[w] == '\t'))
                w++;
        for (e = w; e < s && text[e] != ' ' && text[e] != '\t'; e++)
                ;
        *stop = s, *word = w, *word_end = e;
}

static unsigned int windows_displays_write(char *into, unsigned int room, const struct windows_display *list, unsigned int count)
{
        unsigned int at = 0;

        if (!room)
                return 0;
        into[0] = 0;
        for (unsigned int each = 0; each < count; each++)
        {
                const struct windows_display *d = &list[each];

                if (!d->name[0] || (!d->rotation && !d->refresh && !d->placed && !d->width && !d->primary && d->scale == WINDOWS_SCALE_DEFAULT))
                        continue;
                if (!windows_buf_str(into, room, &at, d->name) || !windows_buf_kv(into, room, &at, " rotation=", (long)d->rotation) ||
                    !(d->refresh ? windows_buf_kv(into, room, &at, " refresh=", (long)d->refresh) : windows_buf_str(into, room, &at, " refresh=preferred")) ||
                    (d->width && (!windows_buf_kv(into, room, &at, " mode=", (long)d->width) || !windows_buf_kv(into, room, &at, "x", (long)d->height))) ||
                    (d->primary && !windows_buf_str(into, room, &at, " primary=yes")) ||
                    (d->scale != WINDOWS_SCALE_DEFAULT && !windows_buf_kv(into, room, &at, " scale=", (long)d->scale)) ||
                    (d->placed && (!windows_buf_kv(into, room, &at, " x=", d->x) || !windows_buf_kv(into, room, &at, " y=", d->y))) ||
                    !windows_buf_put(into, room, &at, "\n", 1))
                        return 0;
        }
        return at;
}

static int windows_displays_rewrite(char *into, unsigned int room, const char *text, const struct windows_display *list, unsigned int count,
                                    const unsigned char *dirty, const struct windows_display *added)
{
        unsigned char emitted[WINDOWS_DISPLAYS] = {0};
        unsigned int at = 0, start = 0, made;
        char line[192];

        into[0] = 0;
        while (text[start])
        {
                unsigned int stop, word, word_end;
                const struct windows_display *found;

                windows_line_first_word(text, start, &stop, &word, &word_end);
                found = word_end > word ? windows_display_of(list, count, text + word, word_end - word) : 0;
                if (found && dirty[found - list])
                {
                        unsigned int idx = (unsigned int)(found - list);

                        if (!emitted[idx])
                        {
                                emitted[idx] = 1, made = windows_displays_write(line, sizeof(line), found, 1);
                                if (made && !windows_buf_put(into, room, &at, line, made))
                                        return -1;
                        }
                }
                else if (!windows_buf_put(into, room, &at, text + start, stop - start) || !windows_buf_put(into, room, &at, "\n", 1))
                        return -1;
                start = stop + (text[stop] == '\n');
        }
        if (added && (made = windows_displays_write(line, sizeof(line), added, 1)) && !windows_buf_put(into, room, &at, line, made))
                return -1;
        return (int)at;
}

static int windows_displays_set(char *into, unsigned int room, const char *text, const char *name, unsigned int field,
                                const struct windows_display *change)
{
        struct windows_display list[WINDOWS_DISPLAYS], screen = {0};
        unsigned char dirty[WINDOWS_DISPLAYS] = {0};
        const struct windows_display *found;
        unsigned int name_len = 0, count;

        if (!room)
                return -1;
        into[0] = 0, screen.scale = WINDOWS_SCALE_DEFAULT;
        name_len = (unsigned int)string_length_max((string_address)name, WINDOWS_OUTPUT_NAME);
        if (!windows_display_name(name, name_len, screen.name))
                return -1;
        count = windows_displays_read(text, list, WINDOWS_DISPLAYS);
        found = windows_display_of(list, count, name, name_len);
        if (!found && count == WINDOWS_DISPLAYS)
                return -1;
        if (found)
                screen = *found;
        screen.bad = 0;
        if (field & WINDOWS_DISPLAY_ROTATION) screen.rotation = change->rotation;
        if (field & WINDOWS_DISPLAY_REFRESH) screen.refresh = change->refresh;
        if (field & WINDOWS_DISPLAY_SCALE) screen.scale = change->scale;
        if (field & WINDOWS_DISPLAY_MODE) screen.width = change->width, screen.height = change->height;
        if (field & WINDOWS_DISPLAY_PRIMARY) screen.primary = change->primary;
        if (field & (WINDOWS_DISPLAY_X | WINDOWS_DISPLAY_Y)) screen.placed = 1, screen.x = change->x, screen.y = change->y;
        if (found)
                list[found - list] = screen, dirty[found - list] = 1;
        return windows_displays_rewrite(into, room, text, list, count, dirty, found ? 0 : &screen);
}

static int windows_displays_primary(char *into, char *work, unsigned int room, const char *text, const char *name)
{
        struct windows_display list[WINDOWS_DISPLAYS], added = {0};
        unsigned char dirty[WINDOWS_DISPLAYS] = {0};
        unsigned int count = windows_displays_read(text, list, WINDOWS_DISPLAYS), name_len;
        int target = -1;

        (void)work;
        if (!room)
                return -1;
        into[0] = 0;
        if (name)
        {
                name_len = (unsigned int)string_length_max((string_address)name, WINDOWS_OUTPUT_NAME);
                if (!windows_display_name(name, name_len, added.name))
                        return -1;
        }
        for (unsigned int i = 0; i < count; i++)
        {
                if (!list[i].name[0])
                        continue;
                if (name && target < 0 && windows_display_same_name(list[i].name, name))
                        target = (int)i, list[i].primary = 1, list[i].bad = 0, dirty[i] = 1;
                else if (list[i].primary)
                        list[i].primary = 0, list[i].bad = 0, dirty[i] = 1;
        }
        if (name && target < 0 && count == WINDOWS_DISPLAYS)
                return -1;
        added.scale = WINDOWS_SCALE_DEFAULT, added.primary = 1;
        return windows_displays_rewrite(into, room, text, list, count, dirty, name && target < 0 ? &added : 0);
}

static int windows_displays_lead(char *into, unsigned int room, const char *text, const char (*names)[WINDOWS_OUTPUT_NAME], unsigned int count)
{
        unsigned int at = 0;

        if (!room)
                return -1;
        into[0] = 0;
        for (unsigned int pass = 0; pass <= count; pass++)
        {
                unsigned int start = 0, taken = 0;

                while (text[start])
                {
                        unsigned int stop, word, word_end, which = 0;

                        windows_line_first_word(text, start, &stop, &word, &word_end);
                        while (which < count && !(word_end > word && windows_name_is(names[which], text + word, word_end - word)))
                                which++;
                        if (pass < count ? which == pass && !taken : which == count)
                        {
                                if (!windows_buf_put(into, room, &at, text + start, stop - start) || !windows_buf_put(into, room, &at, "\n", 1))
                                        return -1;
                                taken = 1;
                        }
                        start = stop + (text[stop] == '\n');
                }
        }
        return (int)at;
}

static unsigned int windows_name_length(const char *name)
{
        unsigned int at = 0;

        while (at < WINDOWS_OUTPUT_NAME - 1 && name[at])
                at++;
        return at;
}

static unsigned int windows_screen_of(const char (*names)[WINDOWS_OUTPUT_NAME], unsigned int count, const char *word, unsigned int length)
{
        for (unsigned int s = 0; s < count; s++)
                if (windows_name_is(names[s], word, length))
                        return s + 1;
        return 0;
}

static int windows_buf_at_xy(char *into, unsigned int room, unsigned int *at, const char *name, unsigned int len, int x, int y)
{
        return windows_buf_put(into, room, at, name, len) && windows_buf_kv(into, room, at, "@", x) &&
               windows_buf_kv(into, room, at, ",", y) && windows_buf_put(into, room, at, " ", 1);
}

static unsigned int windows_arrange_pinned(const char *arrangement, const char (*names)[WINDOWS_OUTPUT_NAME],
                                           unsigned int count, const struct windows_display *list, unsigned int listed,
                                           char *into, unsigned int room)
{
        unsigned char named[WINDOWS_OUTPUTS] = {0}, used[WINDOWS_DISPLAYS] = {0}, extra[WINDOWS_DISPLAYS] = {0};
        const char *base = windows_arrange_valid(arrangement) ? arrangement : "";
        unsigned int at = 0, start, stop, name, screen;
        int how, x, y;

        if (!room || count > WINDOWS_OUTPUTS || listed > WINDOWS_DISPLAYS)
                return 0;
        into[0] = 0;
        for (start = 0; windows_word_next(base, &start, &stop); start = stop)
        {
                const struct windows_display *d;

                if (stop - start == 7 && windows_name_is("default", base + start, 7))
                        continue;
                windows_arrange_word(base + start, stop - start, &name, &how, &x, &y);
                if ((screen = windows_screen_of(names, count, base + start, name)))
                        named[screen - 1] = 1;
                if ((d = windows_display_of(list, listed, base + start, name)))
                        used[d - list] = 1;
                d = how == WINDOWS_PLACE_OFF ? 0 : d;
                if (d && d->placed ? !windows_buf_at_xy(into, room, &at, d->name, name, d->x, d->y)
                                   : !windows_buf_put(into, room, &at, base + start, stop - start) || !windows_buf_put(into, room, &at, " ", 1))
                        return 0;
        }
        for (unsigned int each = 0; each < listed; each++)
        {
                const struct windows_display *d = &list[each];
                unsigned int length = windows_name_length(d->name);

                if (d->placed && !used[each] && length && windows_display_of(list, listed, d->name, length) == d &&
                    (screen = windows_screen_of(names, count, d->name, length)))
                        named[screen - 1] = 1, extra[each] = 1;
        }
        for (unsigned int s = 0; s < count; s++)
                if (!named[s] && (!windows_buf_put(into, room, &at, names[s], windows_name_length(names[s])) || !windows_buf_put(into, room, &at, " ", 1)))
                        return 0;
        for (unsigned int each = 0; each < listed; each++)
                if (extra[each] && !windows_buf_at_xy(into, room, &at, list[each].name, windows_name_length(list[each].name), list[each].x, list[each].y))
                        return 0;
        while (at && into[at - 1] == ' ')
                into[--at] = 0;
        return windows_arrange_valid(into) ? at + 1 : 0;
}

// Row row's buffer which
#define windows_buffer(table, row, which)                                                              \
        ((unsigned int *)((char *)(table) + WINDOWS_HEADER +                                           \
                          ((unsigned long)(row) * 2 + ((which) & 1u)) * (table)->buffer))

struct window_row
{
        struct windows *table;
        unsigned int row;
        long door, wake;
        unsigned long bytes, buffer;
        unsigned int stride, output_width, output_height, closes;
};

static inline unsigned int *windows_pixels(const struct window_row *own, unsigned int which)
{
        return (unsigned int *)((char *)own->table + WINDOWS_HEADER + ((unsigned long)own->row * 2 + (which & 1u)) * own->buffer);
}

struct window_event
{
        unsigned int type, flags;
        int a, b;
        unsigned long long when;
};

_Static_assert(sizeof(struct window) == 1288, "window page ABI");
_Static_assert(sizeof(struct window_key) == 16, "window key ABI");
_Static_assert(sizeof(struct window_cell) == 8, "window cell ABI");
#define WINDOW_AT(field, offset) _Static_assert(offset_of(struct window, field) == (offset), "window page ABI " #field)
WINDOW_AT(x, 0); WINDOW_AT(y, 4); WINDOW_AT(z, 8); WINDOW_AT(width, 12);
WINDOW_AT(height, 16); WINDOW_AT(region, 20); WINDOW_AT(display, 24); WINDOW_AT(style, 28);
WINDOW_AT(edge, 32); WINDOW_AT(sequence, 36); WINDOW_AT(title, 40); WINDOW_AT(state, 168);
WINDOW_AT(pitch, 172); WINDOW_AT(max_width, 176); WINDOW_AT(max_height, 180); WINDOW_AT(display_width, 184);
WINDOW_AT(display_height, 188); WINDOW_AT(awake, 192); WINDOW_AT(handle, 196); WINDOW_AT(stride, 200);
WINDOW_AT(history, 204); WINDOW_AT(head, 208); WINDOW_AT(lines, 212); WINDOW_AT(key_head, 216);
WINDOW_AT(key_tail, 220); WINDOW_AT(keys, 224); WINDOW_AT(columns, 1248); WINDOW_AT(rows, 1252);
WINDOW_AT(damage_row, 1256); WINDOW_AT(damage_rows, 1260); WINDOW_AT(grid_columns, 1264); WINDOW_AT(grid_rows, 1268);
WINDOW_AT(max_columns, 1272); WINDOW_AT(max_rows, 1276); WINDOW_AT(mapping, 1280); WINDOW_AT(want, 1284);
_Static_assert(offset_of(struct window_key, character) == 4, "window key ABI");
_Static_assert(offset_of(struct window_cell, ink) == 4, "window cell ABI");
_Static_assert(offset_of(struct window_cell, paper) == 5, "window cell ABI");
_Static_assert(offset_of(struct window_cell, flags) == 6, "window cell ABI");

#if defined(__x86_64__)
enum
{
        WINDOW_SYS_READ = 0, WINDOW_SYS_WRITE = 1, WINDOW_SYS_CLOSE = 3, WINDOW_SYS_FSTAT = 5,
        WINDOW_SYS_LSEEK = 8, WINDOW_SYS_MMAP = 9, WINDOW_SYS_MUNMAP = 11, WINDOW_SYS_IOCTL = 16,
        WINDOW_SYS_PREAD = 17, WINDOW_SYS_MADVISE = 28, WINDOW_SYS_GETPID = 39, WINDOW_SYS_FCNTL = 72,
        WINDOW_SYS_FTRUNCATE = 77, WINDOW_SYS_OPENAT = 257, WINDOW_SYS_MKDIRAT = 258, WINDOW_SYS_PPOLL = 271,
        WINDOW_SYS_MEMFD = 319
};
#else
enum
{
        WINDOW_SYS_FCNTL = 25, WINDOW_SYS_IOCTL = 29, WINDOW_SYS_MKDIRAT = 34, WINDOW_SYS_FTRUNCATE = 46,
        WINDOW_SYS_OPENAT = 56, WINDOW_SYS_CLOSE = 57, WINDOW_SYS_LSEEK = 62, WINDOW_SYS_READ = 63,
        WINDOW_SYS_WRITE = 64, WINDOW_SYS_PREAD = 67, WINDOW_SYS_PPOLL = 73, WINDOW_SYS_FSTAT = 80,
        WINDOW_SYS_GETPID = 172, WINDOW_SYS_MUNMAP = 215, WINDOW_SYS_MMAP = 222, WINDOW_SYS_MADVISE = 233,
        WINDOW_SYS_MEMFD = 279
};
#endif

#define WINDOWS_MAP_ERR(m) ((m) < 0 && (m) > -4096)

#ifdef STANDARD_MODERN_C
#define window_call(number, a, b, c, d, e, f)                                \
        ((long)system_call_6((positive)(number), (positive)(a),              \
                             (positive)(b), (positive)(c), (positive)(d),    \
                             (positive)(e), (positive)(f)))
#elif defined(__x86_64__)
static long window_call(long number, long a, long b, long c, long d, long e, long f)
{
        register long r10 __asm__("r10") = d, r8 __asm__("r8") = e, r9 __asm__("r9") = f;
        long result;

        __asm__ volatile("syscall" : "=a"(result) : "a"(number), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
        return result;
}
#elif defined(__aarch64__)
static long window_call(long number, long a, long b, long c, long d, long e, long f)
{
        register long x8 __asm__("x8") = number, x0 __asm__("x0") = a, x1 __asm__("x1") = b, x2 __asm__("x2") = c, x3 __asm__("x3") = d, x4 __asm__("x4") = e, x5 __asm__("x5") = f;

        __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
        return x0;
}
#else
static long window_call(long number, long a, long b, long c, long d, long e, long f)
{
        register long a7 __asm__("a7") = number, a0 __asm__("a0") = a, a1 __asm__("a1") = b, a2 __asm__("a2") = c, a3 __asm__("a3") = d, a4 __asm__("a4") = e, a5 __asm__("a5") = f;

        __asm__ volatile("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5) : "memory");
        return a0;
}
#endif

static long windows_read_line(const char *path, char *buf, unsigned int room, long flags)
{
        long fd, got;

        if (!room)
                return -1;
        buf[0] = 0;
        if ((fd = window_call(WINDOW_SYS_OPENAT, -100, (long)path, flags, 0, 0, 0)) < 0)
                return -1;
        got = window_call(WINDOW_SYS_READ, fd, (long)buf, (long)(room - 1), 0, 0, 0);
        window_call(WINDOW_SYS_CLOSE, fd, 0, 0, 0, 0, 0);
        if (got < 0)
                return buf[0] = 0, -1;
        buf[got] = 0;
        for (long i = 0; i < got; i++)
                if (buf[i] == '\n' || buf[i] == '\r')
                        return buf[i] = 0, i;
        return got;
}

static struct windows *windows_scratch_table(struct windows **at)
{
        if (!*at)
        {
                long got = window_call(WINDOW_SYS_MMAP, 0, (long)sizeof(struct windows), 3, 0x4022, -1, 0);

                *at = WINDOWS_MAP_ERR(got) ? 0 : (struct windows *)got;
        }
        return *at;
}

static void windows_scratch_zero(struct windows *table)
{
        if (table)
                window_call(WINDOW_SYS_MADVISE, (long)table, (long)((sizeof(struct windows) + 4095) & ~4095ul), 4, 0, 0, 0);
}

static struct windows *windows_scratch_fresh(struct windows **at)
{
        struct windows *table = windows_scratch_table(at);

        windows_scratch_zero(table);
        return table;
}

/* Touch trace file switch (/run/moonwater/touch-trace -> /run/moonwater/touch-trace.log). */
#define WINDOW_TRACE_SWITCH "/run/moonwater/touch-trace"
#define WINDOW_TRACE_LOG "/run/moonwater/touch-trace.log"
#define WINDOW_TRACE_CAP 1048576l
enum
{
        WINDOW_TRACE_TEXT = 320, WINDOW_TRACE_APPEND = 0x1 | 0x400 | 0x40 | 0x80000,
        WINDOW_TRACE_NEW = 0x1 | 0x200 | 0x40 | 0x80000, WINDOW_TRACE_READ = 0x80000
};

static b32 window_trace_live;
static char window_trace_text[WINDOW_TRACE_TEXT];
static positive window_trace_length;

static b32 window_trace_room(long size, long length)
{
        return size >= 0 && length > 0 && size + length <= WINDOW_TRACE_CAP;
}

static b32 window_trace_write(const char address_to line, positive length)
{
        long handle = window_call(WINDOW_SYS_OPENAT, -100, (long)WINDOW_TRACE_LOG, WINDOW_TRACE_APPEND, 0600, 0, 0), size;
        b32 wrote = false;

        if (handle < 0)
                return false;
        size = window_call(WINDOW_SYS_LSEEK, handle, 0, 2, 0, 0, 0);
        if (window_trace_room(size, (long)length))
                wrote = window_call(WINDOW_SYS_WRITE, handle, (long)line, (long)length, 0, 0, 0) == (long)length;
        window_call(WINDOW_SYS_CLOSE, handle, 0, 0, 0, 0, 0);
        return wrote;
}

static b32 (*window_trace_out)(const char address_to line, positive length) = window_trace_write;

static void window_trace_touch(void)
{
        long handle = window_call(WINDOW_SYS_OPENAT, -100, (long)WINDOW_TRACE_SWITCH, WINDOW_TRACE_READ, 0, 0, 0);
        b32 was = window_trace_live;

        window_trace_live = handle >= 0;
        if (handle >= 0)
                window_call(WINDOW_SYS_CLOSE, handle, 0, 0, 0, 0, 0);
        if (window_trace_live && !was)
        {
                long fresh = window_call(WINDOW_SYS_OPENAT, -100, (long)WINDOW_TRACE_LOG, WINDOW_TRACE_NEW, 0600, 0, 0);

                if (fresh >= 0)
                        window_call(WINDOW_SYS_CLOSE, fresh, 0, 0, 0, 0, 0);
        }
}

static void window_trace_take(address_any data, positive length)
{
        const char address_to bytes = (const char address_to)data;

        for (positive at = 0; at < length && window_trace_length < WINDOW_TRACE_TEXT - 1; at++)
                window_trace_text[window_trace_length++] = bytes[at];
}

static void window_trace_flush(void)
{
        if (window_trace_length < WINDOW_TRACE_TEXT - 1 && (!window_trace_length || window_trace_text[window_trace_length - 1] != '\n'))
                window_trace_text[window_trace_length++] = '\n';
        if (window_trace_out)
                window_trace_out(window_trace_text, window_trace_length);
        window_trace_length = 0;
}

static void window_trace_word(const char address_to word)
{
        if (!window_trace_live || !word)
                return;
        for (positive at = 0; word[at] && window_trace_length < WINDOW_TRACE_TEXT - 1; at++)
                window_trace_text[window_trace_length++] = word[at];
}

#define WINDOW_TRACE_START() do { if (window_trace_live) window_trace_length = 0; } while (0)
#define WINDOW_TRACE_PART(...) do { if (window_trace_live) string_format(window_trace_take, __VA_ARGS__); } while (0)
#define WINDOW_TRACE_END() do { if (window_trace_live) window_trace_flush(); } while (0)
#define WINDOW_TRACE(...) do { if (window_trace_live) { window_trace_length = 0; string_format(window_trace_take, __VA_ARGS__); window_trace_flush(); } } while (0)

struct window_poll { int handle; short events, returned; };

#define WINDOW_STRIDE_STEP 32u

static struct window *window_grid_open(unsigned int columns, unsigned int rows, unsigned int max_columns, unsigned int history)
{
        unsigned long cells, lines, bytes, ring = 1;
        unsigned int cut;
        long mapped;
        struct window *window;

        if (!history || !max_columns || !columns || history > 0x80000000u)
                return 0;
        columns = columns < max_columns ? columns : max_columns;
        rows = rows < history ? rows : history;
        while (ring < history)
                ring <<= 1;
        history = (unsigned int)ring, cells = (unsigned long)history * max_columns;
        if (cells > 0xffffffffu / sizeof(struct window_cell))
                return 0;
        lines = WINDOW_PIXELS + cells * sizeof(struct window_cell);
        bytes = (lines + (unsigned long)history * sizeof(unsigned int) + 4095) & ~4095ul;
        if (bytes > 0xffffffffu)
                return 0;
        cut = (columns + WINDOW_STRIDE_STEP - 1) / WINDOW_STRIDE_STEP * WINDOW_STRIDE_STEP;
        mapped = window_call(WINDOW_SYS_MMAP, 0, (long)bytes, 3, 0x22, -1, 0);
        if (WINDOWS_MAP_ERR(mapped))
                return 0;
        window = (struct window *)mapped;
        window->handle = WINDOW_PRIVATE, window->stride = cut < max_columns ? cut : max_columns;
        window->history = window->max_rows = history, window->head = history + rows, window->lines = (unsigned int)lines;
        window->columns = window->grid_columns = columns, window->rows = window->grid_rows = rows;
        window->max_columns = max_columns, window->mapping = (unsigned int)bytes;
        return window;
}

static void window_widen(struct window *window, unsigned int columns)
{
        unsigned int was = window->stride, stride;

        if (columns <= was)
                return;
        stride = (columns + WINDOW_STRIDE_STEP - 1) / WINDOW_STRIDE_STEP * WINDOW_STRIDE_STEP;
        stride = stride < window->max_columns ? stride : window->max_columns;
        if (stride <= was)
                return;
        for (unsigned int slot = window->history; slot--;)
        {
                unsigned int *length = window_lengths(window) + slot, kept = *length < was ? *length : was;

                if (kept)
                        memory_copy(window_cells(window) + (unsigned long)slot * stride,
                                    window_cells(window) + (unsigned long)slot * was, (unsigned long)kept * sizeof(struct window_cell));
                *length = kept;
        }
        window->stride = stride;
}

/* Window doors, identity/icons, placement, launch-ask, and screen geometry. */
#define WINDOWS_O_READ 0x80800L
#define WINDOWS_O_WRITE 0x80801L
#define WINDOWS_O_BOTH 0x80802L

static void windows_door_name(char *into, unsigned int row, int wake)
{
        windows_num_path(into, 48, WINDOWS_DOOR, row, wake ? ".wake" : "");
}

static void windows_drain(long handle)
{
        char bytes[64];

        while (window_call(WINDOW_SYS_READ, handle, (long)bytes, sizeof(bytes), 0, 0, 0) == (long)sizeof(bytes))
                ;
}

static int windows_lists_fit(const struct windows *table, unsigned long size, unsigned long buffer)
{
        unsigned long lists = table->lists, room = windows_room(table);

        return !lists || (table->list_bytes == WINDOW_LIST_BYTES && lists == WINDOWS_LISTS_AT(buffer, room) && lists <= size &&
                          (size - lists) / WINDOW_LIST_BYTES >= room);
}

static unsigned int windows_copy_word_clean(char *into, const volatile char *src)
{
        unsigned int length = 0;

        for (; length < WINDOWS_APP_ID - 1 && src[length]; length++)
        {
                char byte = src[length];

                into[length] = (unsigned char)byte <= ' ' || byte == 127 ? '?' : byte;
        }
        return into[length] = 0, length;
}

static void windows_store_word(volatile char *dst, const char *src, unsigned int length)
{
        for (unsigned int at = WINDOWS_APP_ID; at-- > length;)
                dst[at] = 0;
        for (unsigned int at = length; at-- > 0;)
                dst[at] = src[at];
}

static unsigned int windows_valid_word_len(const char *text)
{
        unsigned int length = 0;

        for (; text && length < WINDOWS_APP_ID && text[length]; length++)
                if ((unsigned char)text[length] <= ' ' || text[length] == 127)
                        return 0;
        return text && length < WINDOWS_APP_ID ? length : 0;
}

static void windows_identity_clear(struct windows *table, unsigned int row)
{
        if (!table || row >= WINDOWS)
                return;
        windows_store_word(table->app_id[row], "", 0);
        __atomic_store_n(&table->icon_seq[row], 0u, __ATOMIC_RELEASE);
}

static int windows_app_id_set(struct windows *table, unsigned int row, const char *text)
{
        unsigned int length = table && row < WINDOWS ? windows_valid_word_len(text) : 0;

        if (!length)
                return 0;
        return windows_store_word(table->app_id[row], text, length), 1;
}

static unsigned int windows_app_id_read(const struct windows *table, unsigned int row, char *into)
{
        return into[0] = 0, table && row < WINDOWS ? windows_copy_word_clean(into, table->app_id[row]) : 0;
}

static unsigned long long windows_starting_read(const struct windows *table, unsigned int slot, char *into)
{
        unsigned long long stamp;
        unsigned int length;

        into[0] = 0;
        if (!table || slot >= WINDOWS_STARTING || !(stamp = __atomic_load_n(&table->starting_at[slot], __ATOMIC_ACQUIRE)))
                return 0;
        length = windows_copy_word_clean(into, table->starting[slot]);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        return length && __atomic_load_n(&table->starting_at[slot], __ATOMIC_ACQUIRE) == stamp ? stamp : 0;
}

static void windows_starting_set(struct windows *table, const char *id, unsigned long long now)
{
        unsigned int length = table && now ? windows_valid_word_len(id) : 0, slot = 0;
        unsigned long long oldest = ~0ull;
        char held[WINDOWS_APP_ID];

        if (!length)
                return;
        for (unsigned int at = 0; at < WINDOWS_STARTING; at++)
        {
                unsigned long long stamp = windows_starting_read(table, at, held);

                if (stamp && string_equals((string_address)held, (string_address)id))
                {
                        slot = at;
                        break;
                }
                if (stamp < oldest)
                        oldest = stamp, slot = at;
        }
        __atomic_store_n(&table->starting_at[slot], 0ull, __ATOMIC_RELEASE);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        windows_store_word(table->starting[slot], id, length);
        __atomic_store_n(&table->starting_at[slot], now, __ATOMIC_RELEASE);
}

static void windows_starting_done(struct windows *table, unsigned int slot)
{
        if (table && slot < WINDOWS_STARTING)
                __atomic_store_n(&table->starting_at[slot], 0ull, __ATOMIC_RELEASE);
}

static inline unsigned int windows_icon_pixel(unsigned int pixel)
{
        unsigned int a = pixel >> 24, r = pixel >> 16 & 255, g = pixel >> 8 & 255, b = pixel & 255;

        return a << 24 | (r > a ? a : r) << 16 | (g > a ? a : g) << 8 | (b > a ? a : b);
}

static int windows_icon_set(struct windows *table, unsigned int row, const unsigned int *pixels, unsigned int width, unsigned int height,
                            unsigned int stride)
{
        unsigned int side, left, top, seq;

        if (!table || row >= WINDOWS || !pixels || !width || !height || width > WINDOWS_ICON_MOST || height > WINDOWS_ICON_MOST || stride < width)
                return 0;
        side = width > height ? width : height, left = (side - width) / 2, top = (side - height) / 2;
        seq = __atomic_load_n(&table->icon_seq[row], __ATOMIC_ACQUIRE) | 1u;
        __atomic_store_n(&table->icon_seq[row], seq, __ATOMIC_RELEASE);
        for (unsigned int y = 0; y < WINDOWS_ICON; y++)
        {
                unsigned int y0 = y * side / WINDOWS_ICON, y1 = (y + 1) * side / WINDOWS_ICON;

                y1 = y1 > y0 ? y1 : y0 + 1;
                for (unsigned int x = 0; x < WINDOWS_ICON; x++)
                {
                        unsigned int x0 = x * side / WINDOWS_ICON, x1 = (x + 1) * side / WINDOWS_ICON, sum[4] = {0}, count;

                        x1 = x1 > x0 ? x1 : x0 + 1, count = (x1 - x0) * (y1 - y0);
                        for (unsigned int sy = y0; sy < y1; sy++)
                                for (unsigned int sx = x0; sx < x1; sx++)
                                        if (sx >= left && sx - left < width && sy >= top && sy - top < height)
                                        {
                                                unsigned int p = windows_icon_pixel(pixels[(unsigned long)(sy - top) * stride + (sx - left)]);

                                                sum[0] += p >> 24, sum[1] += p >> 16 & 255, sum[2] += p >> 8 & 255, sum[3] += p & 255;
                                        }
                        ((volatile unsigned int *)table->icon[row])[y * WINDOWS_ICON + x] =
                            (sum[0] + count / 2) / count << 24 | (sum[1] + count / 2) / count << 16 | (sum[2] + count / 2) / count << 8 | (sum[3] + count / 2) / count;
                }
        }
        __atomic_store_n(&table->icon_seq[row], seq + 1 ? seq + 1 : 2u, __ATOMIC_RELEASE);
        return 1;
}

static unsigned int windows_icon_read(const struct windows *table, unsigned int row, unsigned int *into)
{
        for (int tries = 0; table && row < WINDOWS && tries < 4; tries++)
        {
                unsigned int seq = __atomic_load_n(&table->icon_seq[row], __ATOMIC_ACQUIRE);

                if (!seq)
                        return 0;
                if (seq & 1u)
                        continue;
                for (unsigned int at = 0; at < WINDOWS_ICON * WINDOWS_ICON; at++)
                        into[at] = windows_icon_pixel(((const volatile unsigned int *)table->icon[row])[at]);
                __atomic_thread_fence(__ATOMIC_ACQUIRE);
                if (__atomic_load_n(&table->icon_seq[row], __ATOMIC_ACQUIRE) == seq)
                        return seq;
        }
        return 0;
}

static int windows_fits(unsigned long size, unsigned long pixels, unsigned long buffer, unsigned int stride,
                        unsigned int width, unsigned int height, unsigned int room)
{
        return pixels == WINDOWS_HEADER && size >= WINDOWS_HEADER && buffer && !(buffer % 4096) && room && room <= WINDOWS &&
               buffer <= (size - WINDOWS_HEADER) / (2 * (unsigned long)room) && width && height && stride >= width && buffer / 4 / stride >= height;
}

static void windows_reset_row(struct windows *table, unsigned int row)
{
        unsigned char *list = window_list_of(table, (int)row);
        #define Z(f) __atomic_store_n(&table->f, 0, __ATOMIC_RELEASE)

        table->state[row] = table->buffer_index[row] = table->glass[row] = table->asked[row] = table->ground[row] = 0;
        table->x[row] = table->y[row] = table->place_x[row] = table->place_y[row] = 0;
        table->width[row] = table->height[row] = table->want_width[row] = table->want_height[row] = 0;
        table->damage_x[row] = table->damage_y[row] = table->damage_width[row] = table->damage_height[row] = 0;
        table->place[row] = table->place_screen[row] = table->place_width[row] = table->place_height[row] = 0;
        table->least_width[row] = table->least_height[row] = table->most_width[row] = table->most_height[row] = 0;
        table->body_state[row] = table->body_raw[row] = table->body_drawn[row] = table->body_speed[row] = 0;
        table->popup_parent[row] = table->popup_flags[row] = 0;
        table->popup_x[row] = table->popup_y[row] = table->popup_w[row] = table->popup_h[row] = 0;
        table->layer[row] = WINDOW_LAYER_NORMAL;
        memory_zero(table->title[row], WINDOWS_TITLE);
        memory_zero(table->name[row], WINDOWS_NAME);
        windows_identity_clear(table, row);
        table->event_tail[row] = __atomic_load_n(&table->event_head[row], __ATOMIC_ACQUIRE);
        __atomic_store_n(&table->osk_tail[row], __atomic_load_n(&table->osk_head[row], __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
        Z(close_asks[row]); Z(close_target[row]); Z(close_target_serial[row]); Z(window_scale[row]); Z(field[row]); Z(pointer_shape[row]);
        Z(lift[row]); Z(page[row]); Z(hover_off[row]); Z(pinch_wanted[row]);
        Z(spoiled_height[row][0]); Z(spoiled_height[row][1]); Z(moved[row]); Z(clip_watch[row]);
        Z(drop_ok[row]); Z(shown[row]); Z(frame[row]);
        if (list)
                memory_zero(list, UI_DRAW_LIST_HEADER);
        Z(list_state[row]); Z(list_seen[row]);
        #undef Z
}

static int window_name_valid(const char *name)
{
        unsigned int at = 0;

        if (!((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= '0' && name[0] <= '9')))
                return 0;
        for (; at < WINDOWS_NAME && name[at]; at++)
                if (!((name[at] >= 'a' && name[at] <= 'z') || (name[at] >= '0' && name[at] <= '9') || name[at] == '-'))
                        return 0;
        return at < WINDOWS_NAME;
}

static const char *window_place_number(const char *text, char stop, int low, int high, int *value)
{
        unsigned int len = 0;

        while (text[len] && text[len] != stop)
                len++;
        return text[len] == stop && windows_scan_int(text, len, low, high, 6, 1, value) ? text + len : 0;
}

static int window_place_parse(struct window_place *place, const char *const *words, unsigned int count)
{
        static const struct { const char *word; unsigned char how, bit; } spec[] = {
            {"center", WINDOW_PLACE_CENTER, WINDOW_PLACE_HOW}, {"maximized", WINDOW_PLACE_MAXIMIZED, WINDOW_PLACE_HOW},
            {"fullscreen", WINDOW_PLACE_FULLSCREEN, WINDOW_PLACE_HOW}, {"at", WINDOW_PLACE_AT, WINDOW_PLACE_HOW},
            {"screen", 0, WINDOW_PLACE_SCREEN}, {"size", 0, WINDOW_PLACE_SIZE}, {"name", 0, 32u},
        };
        unsigned int at = 0, seen = 0;

        memory_zero(place, sizeof(*place));
        while (at < count)
        {
                const char *val = at + 1 < count ? words[at + 1] : 0, *after;
                unsigned int how = 0, bit = 0;
                int a, b;

                for (unsigned int i = 0; i < sizeof(spec) / sizeof(spec[0]); i++)
                        if (string_equals((string_address)words[at], (string_address)spec[i].word))
                        {
                                how = spec[i].how, bit = spec[i].bit;
                                break;
                        }
                if (!bit)
                        break;
                if (seen & bit)
                        return -1;
                seen |= bit, at++;
                if (how && how != WINDOW_PLACE_AT)
                {
                        place->how |= how;
                        continue;
                }
                if (!val)
                        return -1;
                at++;
                if (how == WINDOW_PLACE_AT)
                {
                        if (!(after = window_place_number(val, ',', -WINDOW_PLACE_MOST, WINDOW_PLACE_MOST, &place->x)) ||
                            !window_place_number(after + 1, 0, -WINDOW_PLACE_MOST, WINDOW_PLACE_MOST, &place->y))
                                return -1;
                        place->how |= how;
                }
                else if (bit == WINDOW_PLACE_SCREEN)
                {
                        if (!window_place_number(val, 0, 1, (int)WINDOW_PLACE_SCREENS, &a))
                                return -1;
                        place->screen = (unsigned int)a - 1, place->how |= bit;
                }
                else if (bit == WINDOW_PLACE_SIZE)
                {
                        if (!(after = window_place_number(val, 'x', 1, WINDOW_PLACE_MOST, &a)) ||
                            !window_place_number(after + 1, 0, 1, WINDOW_PLACE_MOST, &b))
                                return -1;
                        place->width = (unsigned int)a, place->height = (unsigned int)b, place->how |= bit;
                }
                else if (!window_name_valid(val))
                        return -1;
                else
                        string_copy_bounded((string_address)place->name, (string_address)val, WINDOWS_NAME);
        }
        return (place->how & WINDOW_PLACE_SIZE) && ((place->how & WINDOW_PLACE_HOW) == WINDOW_PLACE_MAXIMIZED ||
                                                    (place->how & WINDOW_PLACE_HOW) == WINDOW_PLACE_FULLSCREEN)
                   ? -1 : (int)at;
}

static unsigned int window_place_say(const struct window_place *place, char *into, unsigned int room)
{
        static const char *const modes[] = {"", "center", "at", "maximized", "fullscreen"};
        char text[WINDOW_PLACE_TEXT] = {0};
        unsigned int at = 0, how = place->how & WINDOW_PLACE_HOW, length;

        if (how && how < 5)
                windows_buf_str(text, sizeof(text), &at, modes[how]);
        if (how == WINDOW_PLACE_AT)
                windows_buf_kv(text, sizeof(text), &at, " ", place->x), windows_buf_kv(text, sizeof(text), &at, ",", place->y);
        if (place->how & WINDOW_PLACE_SCREEN)
                windows_buf_kv(text, sizeof(text), &at, at ? " screen " : "screen ", (long)place->screen + 1);
        if (place->how & WINDOW_PLACE_SIZE)
                windows_buf_kv(text, sizeof(text), &at, at ? " size " : "size ", (long)place->width),
                    windows_buf_kv(text, sizeof(text), &at, "x", (long)place->height);
        if (place->name[0])
                windows_buf_str(text, sizeof(text), &at, at ? " name " : "name "), windows_buf_str(text, sizeof(text), &at, place->name);
        length = at < room ? at : room ? room - 1 : 0;
        if (length)
                memory_copy(into, text, length);
        if (room)
                into[length] = 0;
        return length;
}

static void windows_place_set(struct windows *table, unsigned int row, const struct window_place *place)
{
        table->place_x[row] = place->x, table->place_y[row] = place->y;
        table->place_width[row] = place->width, table->place_height[row] = place->height, table->place_screen[row] = place->screen;
        if (place->name[0])
                memory_copy(table->name[row], place->name, WINDOWS_NAME);
        __atomic_store_n(&table->place[row], place->how, __ATOMIC_RELEASE);
}

static void windows_place_free(struct windows *table, unsigned int row)
{
        __atomic_or_fetch(&table->place[row], WINDOW_PLACE_FREE, __ATOMIC_RELEASE);
}

static int window_place_from_text(struct window_place *place, const char *text)
{
        char words[WINDOW_PLACE_TEXT];
        const char *word[WINDOW_PLACE_WORDS];
        unsigned int length = (unsigned int)string_length_max((string_address)text, WINDOW_PLACE_TEXT), count = 0;

        if (!length || length == WINDOW_PLACE_TEXT)
                return 0;
        for (unsigned int at = 0; at <= length; at++)
        {
                words[at] = text[at] == ' ' ? 0 : text[at];
                if (at < length && text[at] != ' ' && (!at || text[at - 1] == ' '))
                {
                        if (count == WINDOW_PLACE_WORDS)
                                return 0;
                        word[count++] = words + at;
                }
                if (text[at] == ' ' && (!at || at + 1 == length || text[at + 1] == ' '))
                        return 0;
        }
        return window_place_parse(place, word, count) == (int)count;
}

static char *windows_env_value(char *entry, const char *name, unsigned int len)
{
        return !memory_compare(entry, name, len) && entry[len] == '=' ? entry + len + 1 : 0;
}

static void windows_place_take(struct windows *table, unsigned int row)
{
        string_address address_to env = program_environment_list();
        int placed = 0;
        struct window_place place;

        for (unsigned int at = 0; env && env[at]; at++)
        {
                char *val = windows_env_value((char *)env[at], WINDOW_PLACE_VARIABLE, sizeof(WINDOW_PLACE_VARIABLE) - 1);

                if (val)
                {
                        if (!placed && *val && window_place_from_text(&place, val))
                                windows_place_set(table, row, &place), placed = 1;
                        *val = 0;
                }
        }
}

static void windows_asked_take(struct windows *table, unsigned int row, string_address address_to env)
{
        for (unsigned int at = 0; env && env[at]; at++)
        {
                char *val = windows_env_value((char *)env[at], WINDOW_ASKED_VARIABLE, sizeof(WINDOW_ASKED_VARIABLE) - 1);

                if (val)
                {
                        if (val[0] == '1' && !val[1])
                                __atomic_store_n(&table->asked[row], 1u, __ATOMIC_RELEASE);
                        *val = 0;
                }
        }
}

static unsigned long windows_proc_stat(unsigned int pid, unsigned int *parent)
{
        char path[32], text[512];
        unsigned int field = 3;
        unsigned long value = 0, start = 0;
        long handle, got, from = -1;

        *parent = 0;
        windows_num_path(path, sizeof(path), "/proc/", pid, "/stat");
        if (!pid || (handle = window_call(WINDOW_SYS_OPENAT, -100, (long)path, 0x80000, 0, 0, 0)) < 0)
                return 0;
        got = window_call(WINDOW_SYS_READ, handle, (long)text, (long)sizeof text - 1, 0, 0, 0);
        window_call(WINDOW_SYS_CLOSE, handle, 0, 0, 0, 0, 0);
        for (long k = 0; k < got; k++)
                from = text[k] == ')' ? k : from;
        for (long k = from >= 0 ? from + 2 : got; k < got && field <= 22; k++)
                if (text[k] == ' ')
                        field++, value = 0;
                else if (text[k] >= '0' && text[k] <= '9')
                {
                        value = value * 10 + (unsigned long)(text[k] - '0');
                        if (field == 4) *parent = (unsigned int)value;
                        if (field == 22) start = value;
                }
        return start;
}

/* A row's owner as the compositor saw it: the pid a client claimed and that process's start time (/proc stat, field 22).
   The compositor keeps these in its own memory, one per row, never in the shared table, so a client cannot rewrite them.
   The table's pid is only a claim: every use of it goes through windows_owner_ok, which refuses a pid that changed or that
   was reused (a new process has a new start time). */
struct windows_owner
{
        unsigned int pid;
        unsigned long long start;
};

/* Compositor: record the owner of a row when it is claimed (the table's pid is read once, here). */
static int windows_owner_take(struct windows_owner *held, unsigned int pid)
{
        unsigned int parent = 0;
        unsigned long long start = pid ? windows_proc_stat(pid, &parent) : 0;

        held->pid = start ? pid : 0, held->start = start;
        return start != 0;
}

/* Compositor: may the row's claimed pid be used? True only for the process taken for that row and still the same one. */
static int windows_owner_ok(const struct windows_owner *held, unsigned int pid)
{
        unsigned int parent = 0;

        return held->pid && held->start && pid == held->pid && windows_proc_stat(pid, &parent) == held->start;
}

static const char *windows_asked_dir = WINDOWS_ASKED_DIR;

static void windows_asked_path(char *path, unsigned int pid)
{
        unsigned int at = 0;

        windows_buf_str(path, 64, &at, windows_asked_dir);
        windows_buf_kv(path, 64, &at, "/", (long)pid);
}

static void windows_asked_record(void)
{
        char path[64], line[24];
        unsigned int parent, pid = (unsigned int)window_call(WINDOW_SYS_GETPID, 0, 0, 0, 0, 0, 0), length = 0;
        unsigned long start = windows_proc_stat(pid, &parent);
        long handle;

        if (!start)
                return;
        window_call(WINDOW_SYS_MKDIRAT, -100, (long)windows_asked_dir, 0700, 0, 0, 0);
        windows_asked_path(path, pid);
        windows_buf_num(line, sizeof(line), &length, (long)start);
        windows_buf_put(line, sizeof(line), &length, "\n", 1);
        handle = window_call(WINDOW_SYS_OPENAT, -100, (long)path, 01 | 0100 | 01000 | O_NOFOLLOW | 02000000, 0600, 0, 0);
        if (handle >= 0)
                window_call(WINDOW_SYS_WRITE, handle, (long)line, (long)length, 0, 0, 0), window_call(WINDOW_SYS_CLOSE, handle, 0, 0, 0, 0, 0);
}

static string_address address_to window_asked_environment(void)
{
        static string_address environment[WINDOW_ASKED_ENVIRONMENT];
        static char asked[] = WINDOW_ASKED_VARIABLE "=1";
        string_address address_to inherited = program_environment_list();
        unsigned int used = 1;

        windows_asked_record();
        environment[0] = asked;
        for (unsigned int at = 0; inherited && inherited[at]; at++)
                if (!string_has_prefix(inherited[at], WINDOW_ASKED_VARIABLE "="))
                {
                        if (used + 1 >= WINDOW_ASKED_ENVIRONMENT)
                                return inherited;
                        environment[used++] = inherited[at];
                }
        environment[used] = null;
        return environment;
}

static void windows_name(const struct window_row *own, const char *name)
{
        if (name && !own->table->name[own->row][0] && window_name_valid(name))
                string_copy_bounded((string_address)own->table->name[own->row], (string_address)name, WINDOWS_NAME);
}

static unsigned int windows_output(const struct windows *table, unsigned int index, int *x, int *y, int *width, int *height)
{
        unsigned int count = __atomic_load_n(&table->outputs, __ATOMIC_ACQUIRE);
        long v[4];

        if (!count || count > WINDOWS_OUTPUTS)
                index = 0, v[0] = v[1] = 0, v[2] = (long)table->output_width, v[3] = (long)table->output_height;
        else
                index = index < count ? index : 0, v[0] = table->output_x[index], v[1] = table->output_y[index],
                v[2] = (long)table->output_w[index], v[3] = (long)table->output_h[index];
        for (unsigned int at = 0; at < 4; at++)
        {
                long low = at < 2 ? -WINDOW_COORD_MAX : 1;

                v[at] = v[at] < low ? low : v[at] > WINDOW_COORD_MAX ? WINDOW_COORD_MAX : v[at];
        }
        *x = (int)v[0], *y = (int)v[1], *width = (int)v[2], *height = (int)v[3];
        return index;
}

static unsigned int windows_output_at(const struct windows *table, int px, int py, int *x, int *y, int *width, int *height)
{
        unsigned int count = __atomic_load_n(&table->outputs, __ATOMIC_ACQUIRE);

        for (unsigned int at = 0; count <= WINDOWS_OUTPUTS && at < count; at++)
                if (windows_output(table, at, x, y, width, height) == at && px >= *x && px - *x < *width && py >= *y && py - *y < *height)
                        return at;
        return windows_output(table, 0, x, y, width, height);
}

static unsigned int windows_screen_numbers(const struct windows *table, unsigned int number[WINDOWS_OUTPUTS])
{
        unsigned int count = __atomic_load_n(&table->outputs, __ATOMIC_ACQUIRE);
        int left[WINDOWS_OUTPUTS], top[WINDOWS_OUTPUTS];

        if (!count || count > WINDOWS_OUTPUTS)
                return number[0] = 1, 1;
        for (unsigned int at = 0; at < count; at++)
                left[at] = table->output_x[at], top[at] = table->output_y[at];
        for (unsigned int at = 0; at < count; at++)
        {
                unsigned int rank = 1;

                for (unsigned int o = 0; o < count; o++)
                        rank += top[o] < top[at] || (top[o] == top[at] && (left[o] < left[at] || (left[o] == left[at] && o < at)));
                number[at] = rank;
        }
        return count;
}

/* Remembered window places, rfkill radio state, kmsg tick, and table mapping/row claiming. */
enum { PLACES_MOST = 32, PLACES_LINE = 96, PLACES_BYTES = PLACES_MOST * PLACES_LINE };

struct place_kept { char name[WINDOWS_NAME], screen[WINDOWS_NAME]; int origin_x, origin_y, x, y, width, height; };
struct places { unsigned int count; struct place_kept kept[PLACES_MOST]; };

static int place_screen_name_valid(const char *name)
{
        unsigned int at = 0;

        for (; at < WINDOWS_NAME && name[at]; at++)
                if (!((unsigned int)((name[at] | 32) - 'a') < 26u || (name[at] >= '0' && name[at] <= '9') || name[at] == '-'))
                        return 0;
        return at && at < WINDOWS_NAME;
}

static int places_find(const struct places *places, const char *name)
{
        for (unsigned int at = 0; at < places->count; at++)
        {
                unsigned int k = 0;

                while (k < WINDOWS_NAME && places->kept[at].name[k] == name[k] && name[k])
                        k++;
                if (k < WINDOWS_NAME && places->kept[at].name[k] == name[k])
                        return (int)at;
        }
        return -1;
}

static int places_line(char *line, struct place_kept *kept)
{
        char *field[8];
        unsigned int fields = 0;
        int v[6];

        for (unsigned int at = 0;; at++)
        {
                if (line[at] && line[at] != ' ')
                {
                        if (!at || !line[at - 1])
                        {
                                if (fields == 8)
                                        return 0;
                                field[fields++] = line + at;
                        }
                        continue;
                }
                if (line[at] == ' ' && (!at || !line[at - 1] || !line[at + 1]))
                        return 0;
                if (!line[at])
                        break;
                line[at] = 0;
        }
        if (fields != 8 || !window_name_valid(field[0]) || !place_screen_name_valid(field[1]))
                return 0;
        for (unsigned int at = 0; at < 6; at++)
                if (!window_place_number(field[2 + at], 0, at < 4 ? -WINDOW_PLACE_MOST : 1, WINDOW_PLACE_MOST, &v[at]))
                        return 0;
        memory_zero(kept, sizeof(*kept));
        string_copy_bounded((string_address)kept->name, (string_address)field[0], WINDOWS_NAME);
        string_copy_bounded((string_address)kept->screen, (string_address)field[1], WINDOWS_NAME);
        kept->origin_x = v[0], kept->origin_y = v[1], kept->x = v[2], kept->y = v[3], kept->width = v[4], kept->height = v[5];
        return 1;
}

static void places_parse(struct places *places, const char *text, unsigned long length)
{
        unsigned long at = 0;

        places->count = 0;
        while (at < length && places->count < PLACES_MOST)
        {
                char line[PLACES_LINE];
                unsigned long size = 0;
                struct place_kept kept;

                for (; at + size < length && text[at + size] != '\n'; size++)
                        if (size < PLACES_LINE)
                                line[size] = text[at + size];
                if (at + size < length && size < PLACES_LINE)
                {
                        line[size] = 0;
                        if (places_line(line, &kept) && places_find(places, kept.name) < 0)
                                places->kept[places->count++] = kept;
                }
                at += size + 1;
        }
}

static unsigned long places_say(const struct places *places, char *into, unsigned long room)
{
        unsigned long length = 0;

        for (unsigned int at = 0; at < places->count; at++)
        {
                const struct place_kept *k = &places->kept[at];
                const int nums[6] = {k->origin_x, k->origin_y, k->x, k->y, k->width, k->height};
                char line[PLACES_LINE * 2];
                unsigned int pos = 0;

                windows_buf_str(line, sizeof(line), &pos, k->name);
                windows_buf_str(line, sizeof(line), &pos, " ");
                windows_buf_str(line, sizeof(line), &pos, k->screen);
                for (unsigned int i = 0; i < 6; i++)
                        windows_buf_kv(line, sizeof(line), &pos, " ", nums[i]);
                windows_buf_put(line, sizeof(line), &pos, "\n", 1);
                if (length + pos > room)
                        break;
                memory_copy(into + length, line, pos), length += pos;
        }
        return length;
}

static void places_note(struct places *places, const struct place_kept *kept)
{
        int found = places_find(places, kept->name);
        unsigned int last = found >= 0 ? (unsigned int)found : places->count < PLACES_MOST ? places->count : PLACES_MOST - 1;

        for (unsigned int at = last; at > 0; at--)
                places->kept[at] = places->kept[at - 1];
        places->kept[0] = *kept;
        places->count += found < 0 && places->count < PLACES_MOST;
}

#define WINDOWS_TAKING 0x80000000u

static const char *radio_key_of_type(const char *type)
{
        if (!type)
                return null;
        return string_equals((string_address)type, (string_address)"wlan") ? "wifi"
               : string_equals((string_address)type, (string_address)"bluetooth") ? "bluetooth" : null;
}

static const char *radio_blocked_detail(const char *type, int soft, int hard, int asked)
{
        const char *key = radio_key_of_type(type);

        if (!key || (!soft && !hard))
                return null;
        if (hard)
                return "blocked by its hardware switch: turn the radio on at the switch";
        if (!asked)
                return null;
        return key[0] == 'w' ? "blocked in software (rfkill) though it is switched on: run moonwater wifi on"
                             : "blocked in software (rfkill) though it is switched on: run moonwater bluetooth on";
}

static int radio_asked(const char *word, long list)
{
        return word ? string_equals((string_address)word, (string_address)"on") : list > 0;
}

static unsigned char radio_shown[2];

static void radio_state_tick(struct windows *table)
{
        static const struct { const char *key, *power, *list; } radios[2] = {
            {"wifi", "/root/wifi.power", "/root/wifi"}, {"bluetooth", "/root/bluetooth.power", "/root/bluetooth"}};
        const char *found[2] = {null, null};

        if (!table)
                return;
        for (unsigned int n = 0; n < 8; n++)
        {
                char type[16], soft[4], hard[4], path[64], word[8], list[4];
                const char *key, *detail;
                unsigned int r;

                windows_num_path(path, sizeof(path), "/sys/class/rfkill/rfkill", n, "/type");
                if (windows_read_line(path, type, sizeof(type), 0x80000) < 0 || !(key = radio_key_of_type(type)))
                        continue;
                windows_num_path(path, sizeof(path), "/sys/class/rfkill/rfkill", n, "/soft");
                windows_read_line(path, soft, sizeof(soft), 0x80000);
                windows_num_path(path, sizeof(path), "/sys/class/rfkill/rfkill", n, "/hard");
                windows_read_line(path, hard, sizeof(hard), 0x80000);
                r = key[0] == 'b';
                detail = radio_blocked_detail(type, soft[0] == '1', hard[0] == '1',
                                              soft[0] == '1' && (windows_read_line(radios[r].power, word, sizeof(word), 0x80000) >= 0
                                                                     ? radio_asked(word, 0)
                                                                     : radio_asked(null, windows_read_line(radios[r].list, list, sizeof(list), 0x80000))));
                if (detail && !found[r])
                        found[r] = detail;
        }
        for (unsigned int i = 0; i < 2; i++)
                if (found[i])
                        windows_issue_set(table, radios[i].key, WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_RADIO, 0, 0, found[i]), radio_shown[i] = 1;
                else if (radio_shown[i])
                        windows_issue_clear(table, radios[i].key, WINDOWS_ISSUE_RADIO), radio_shown[i] = 0;
}

static unsigned int windows_kernel_dismiss(struct windows *table, unsigned int mask)
{
        unsigned int cleared = 0;

        for (unsigned int idx = 0; table && idx < KERNEL_NOTICE_CLASSES; idx++)
                if (mask >> idx & 1u)
                {
                        cleared += windows_issue_clear(table, kernel_notice_key[idx], WINDOWS_ISSUE_KERNEL_GPU + idx);
                        __atomic_or_fetch(&table->kernel_dismiss, 1u << idx, __ATOMIC_RELEASE);
                }
        return cleared;
}

static void kernel_notices_tick(struct windows *table, unsigned long long now)
{
        struct kernel_notices *k = &kernel_notice_state;
        char record[KERNEL_NOTICE_LINE];

        if (k->fd == -1)
        {
                long file = window_call(WINDOW_SYS_OPENAT, -100, (long)KMSG_FILE, 0x800 | 0x80000, 0, 0, 0);

                k->fd = file >= 0 ? (int)file : -2;
        }
        for (unsigned int n = 0; k->fd >= 0 && n < KERNEL_NOTICE_MOST; n++)
        {
                long got = window_call(WINDOW_SYS_READ, k->fd, (long)record, sizeof(record), 0, 0, 0);

                if (got <= 0)
                        break;
                kernel_notices_record(k, record, (unsigned int)got, now);
        }
        kernel_notices_publish(table, k, now);
        radio_state_tick(table);
}

#define WINDOWS_ROWS_WAIT_MS 30000
static int windows_rows_ask(struct windows *table, unsigned int had)
{
        unsigned int seen = windows_rows(table);
        long door;

        if (had >= windows_room(table))
                return 0;
        __atomic_add_fetch(&table->rows_wanted, 1u, __ATOMIC_RELEASE);
        if ((door = window_call(WINDOW_SYS_OPENAT, -100, (long)WINDOWS_CLIP_DOOR, WINDOWS_O_WRITE, 0, 0, 0)) < 0)
                return 0;
        for (unsigned int waited = 0; waited < WINDOWS_ROWS_WAIT_MS && windows_rows(table) <= had; waited++)
        {
                long nap[2] = {0, 1000000};
                char byte = 0;

                if (windows_rows(table) != seen)
                        seen = windows_rows(table), waited = 0;
                if (waited % 100 == 0 && window_call(WINDOW_SYS_WRITE, door, (long)&byte, 1, 0, 0, 0) == -32)
                        break;
                window_call(WINDOW_SYS_PPOLL, 0, 0, (long)nap, 0, 8, 0);
        }
        window_call(WINDOW_SYS_CLOSE, door, 0, 0, 0, 0, 0);
        return windows_rows(table) > had;
}

static long windows_table_map(long file, long *size)
{
        long mapped;

        *size = window_call(WINDOW_SYS_LSEEK, file, 0, 2, 0, 0, 0);
        mapped = *size >= (long)sizeof(struct windows) ? window_call(WINDOW_SYS_MMAP, 0, *size, 3, 1, file, 0) : -22;
        window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
        return mapped;
}

static struct windows *windows_board_cache;
static unsigned long windows_board_inode;
static long windows_board_bytes;

static void windows_board_forget(void)
{
        if (windows_board_cache)
                window_call(WINDOW_SYS_MUNMAP, (long)windows_board_cache, windows_board_bytes, 0, 0, 0, 0);
        windows_board_cache = null, windows_board_inode = 0, windows_board_bytes = 0;
}

static struct windows *windows_board_at(const char *path)
{
        unsigned long st[18];
        long file = window_call(WINDOW_SYS_OPENAT, -100, (long)path, WINDOW_OPEN_FLAGS, 0, 0, 0), size, mapped;
        struct windows *table;

        if (file < 0)
                goto fail;
        if (window_call(WINDOW_SYS_FSTAT, file, (long)st, 0, 0, 0, 0) < 0)
        {
                window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
                goto fail;
        }
        if (windows_board_cache && st[1] == windows_board_inode && (long)st[6] == windows_board_bytes)
                return window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0), windows_board_cache;
        mapped = windows_table_map(file, &size);
        if (WINDOWS_MAP_ERR(mapped))
                goto fail;
        table = (struct windows *)mapped;
        if (!windows_abi_ok(__atomic_load_n(&table->abi, __ATOMIC_ACQUIRE)))
        {
                window_call(WINDOW_SYS_MUNMAP, mapped, size, 0, 0, 0, 0);
                goto fail;
        }
        windows_board_forget();
        windows_board_cache = table, windows_board_inode = st[1], windows_board_bytes = size;
        return table;
fail:
        windows_board_forget();
        return null;
}

static struct windows *windows_board_map(void) { return windows_board_at(WINDOWS_TABLE); }

static long windows_take(struct window_row *own)
{
        long spark = window_call(WINDOW_SYS_OPENAT, -100, (long)"/dev/spark", WINDOW_OPEN_FLAGS, 0, 0, 0);
        long file = spark >= 0 ? window_call(WINDOW_SYS_IOCTL, spark, WINDOWS_IOCTL_TABLE_GET, 0, 0, 0, 0) : -2, size, mapped, probe, failed = 0, err;
        unsigned int pid = (unsigned int)window_call(WINDOW_SYS_GETPID, 0, 0, 0, 0, 0, 0), stride, width, height;
        unsigned long pixels, buffer;
        struct windows *table;
        char name[48];

        if (spark >= 0)
                window_call(WINDOW_SYS_CLOSE, spark, 0, 0, 0, 0, 0);
        if (file < 0 && (file = window_call(WINDOW_SYS_OPENAT, -100, (long)WINDOWS_TABLE, WINDOW_OPEN_FLAGS, 0, 0, 0)) < 0)
                return file;
        mapped = windows_table_map(file, &size);
        if (WINDOWS_MAP_ERR(mapped))
                return mapped;
        table = (struct windows *)mapped;
        if (!windows_abi_ok(__atomic_load_n(&table->abi, __ATOMIC_ACQUIRE)))
                return err = -71, window_call(WINDOW_SYS_MUNMAP, mapped, size, 0, 0, 0, 0), err;
        pixels = __atomic_load_n(&table->pixels, __ATOMIC_ACQUIRE);
        buffer = __atomic_load_n(&table->buffer, __ATOMIC_ACQUIRE);
        stride = __atomic_load_n(&table->stride, __ATOMIC_ACQUIRE);
        width = __atomic_load_n(&table->output_width, __ATOMIC_ACQUIRE);
        height = __atomic_load_n(&table->output_height, __ATOMIC_ACQUIRE);
        if (!windows_fits((unsigned long)size, pixels, buffer, stride, width, height, windows_room(table)) ||
            !windows_lists_fit(table, (unsigned long)size, buffer))
                return window_call(WINDOW_SYS_MUNMAP, mapped, size, 0, 0, 0, 0), -22;
        *own = (struct window_row){.table = table, .bytes = (unsigned long)size, .buffer = buffer,
                                   .stride = stride, .output_width = width, .output_height = height};
        for (unsigned int from = 0;; from = own->row + 1)
        {
                for (own->row = from; own->row < windows_rows(table); own->row++)
                {
                        unsigned int none = 0;

                        if (__atomic_compare_exchange_n(&table->pid[own->row], &none, WINDOWS_TAKING, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
                                break;
                }
                if (own->row >= windows_rows(table))
                {
                        unsigned int had = own->row;

                        if (had && windows_rows_ask(table, had))
                        {
                                own->row = had - 1;
                                continue;
                        }
                        return window_call(WINDOW_SYS_MUNMAP, mapped, size, 0, 0, 0, 0), failed ? failed : -28;
                }
                windows_reset_row(table, own->row);
                windows_place_take(table, own->row);
                windows_asked_take(table, own->row, program_environment_list());
                windows_door_name(name, own->row, 0);
                probe = window_call(WINDOW_SYS_OPENAT, -100, (long)name, WINDOWS_O_WRITE, 0, 0, 0);
                own->door = probe >= 0 ? window_call(WINDOW_SYS_OPENAT, -100, (long)name, WINDOWS_O_BOTH, 0, 0, 0) : probe;
                if (probe >= 0)
                        window_call(WINDOW_SYS_CLOSE, probe, 0, 0, 0, 0, 0);
                windows_door_name(name, own->row, 1);
                own->wake = own->door >= 0 ? window_call(WINDOW_SYS_OPENAT, -100, (long)name, WINDOWS_O_READ, 0, 0, 0) : -1;
                if (own->door >= 0 && own->wake >= 0)
                        break;
                failed = own->door < 0 ? own->door : own->wake;
                if (own->door >= 0)
                        window_call(WINDOW_SYS_CLOSE, own->door, 0, 0, 0, 0, 0);
                __atomic_store_n(&table->pid[own->row], 0, __ATOMIC_RELEASE);
        }
        windows_drain(own->wake);
        __atomic_store_n(&table->pid[own->row], pid, __ATOMIC_RELEASE);
        return 0;
}

/* Window row operations, atomic file write, clipboard/drag-and-drop codec, and colour parsing. */
static int windows_close_asked(struct window_row *own)
{
        unsigned int asks = __atomic_load_n(&own->table->close_asks[own->row], __ATOMIC_ACQUIRE);

        return asks == own->closes ? 0 : (own->closes = asks, asks != 0);
}

static void windows_knock(const struct window_row *own)
{
        char byte = 0;

        window_call(WINDOW_SYS_WRITE, own->door, (long)&byte, 1, 0, 0, 0);
}

static void windows_knock_bump(const struct window_row *own, unsigned int *serial)
{
        __atomic_add_fetch(serial, 1, __ATOMIC_RELEASE);
        windows_knock(own);
}

static void windows_title_set(const struct window_row *own, const char *title)
{
        char *dst = own->table->title[own->row];
        unsigned int len = 0, changed = 0;

        while (title && title[len] && len < WINDOWS_TITLE - 1)
                len++;
        while (len && (unsigned char)title[len] >= 0x80 && (unsigned char)title[len] < 0xc0)
                len--;
        for (unsigned int i = 0; i < WINDOWS_TITLE; i++)
        {
                char ch = i < len ? title[i] : 0;

                if (dst[i] != ch)
                        dst[i] = ch, changed = 1;
        }
        if (changed && own->table->frame[own->row])
                windows_knock(own);
}

static int windows_keyboard_send(const struct window_row *own, int code, int character, unsigned int flags)
{
        struct windows *table = own->table;
        unsigned int head = table->osk_head[own->row], at;

        if (head - __atomic_load_n(&table->osk_tail[own->row], __ATOMIC_ACQUIRE) >= WINDOWS_OSK)
                return 0;
        at = head % WINDOWS_OSK;
        table->osk_code[own->row][at] = code, table->osk_character[own->row][at] = character, table->osk_flags[own->row][at] = flags;
        __atomic_store_n(&table->osk_head[own->row], head + 1, __ATOMIC_RELEASE);
        return windows_knock(own), 1;
}

static void windows_show(const struct window_row *own, unsigned int which)
{
        own->table->buffer_index[own->row] = which & 1u;
        windows_knock_bump(own, &own->table->frame[own->row]);
}

static int windows_ready(const struct window_row *own)
{
        return __atomic_load_n(&own->table->shown[own->row], __ATOMIC_ACQUIRE) == own->table->frame[own->row];
}

static int windows_event(const struct window_row *own, struct window_event *event)
{
        struct windows *table = own->table;
        unsigned int row = own->row, tail = table->event_tail[row], at = tail % WINDOWS_EVENTS;

        if (__atomic_load_n(&table->event_head[row], __ATOMIC_ACQUIRE) == tail)
                return 0;
        event->type = table->event_type[row][at], event->flags = table->event_flags[row][at];
        event->a = __atomic_exchange_n(&table->event_a[row][at], 0, __ATOMIC_ACQ_REL);
        event->b = table->event_b[row][at], event->when = table->event_when[row][at];
        __atomic_store_n(&table->event_tail[row], tail + 1, __ATOMIC_RELEASE);
        return 1;
}

static long windows_wait_also(const struct window_row *own, long also, long other, long nanoseconds)
{
        struct window_poll wanted[3];
        long timeout[2] = {nanoseconds / 1000000000, nanoseconds % 1000000000}, ready;
        unsigned int count = 0, also_at = 3;

        wanted[count++] = (struct window_poll){(int)own->wake, 1, 0};
        if (also >= 0)
                also_at = count, wanted[count++] = (struct window_poll){(int)also, 1, 0};
        if (other >= 0)
                wanted[count++] = (struct window_poll){(int)other, 1, 0};
        ready = window_call(WINDOW_SYS_PPOLL, (long)wanted, count, nanoseconds < 0 ? 0 : (long)timeout, 0, 0, 0);
        if (ready > 0 && wanted[0].returned)
        {
                windows_drain(own->wake);
                if (wanted[0].returned & 16)
                        return -1;
        }
        if (ready > 0 && also_at < 3 && wanted[also_at].returned)
                windows_drain(also);
        return ready;
}

static long windows_wait(const struct window_row *own, long other, long nanoseconds)
{
        return windows_wait_also(own, -1, other, nanoseconds);
}

static int windows_spoiled(const struct window_row *own, unsigned int which, int *x, int *y, int *width, int *height)
{
        struct windows *table = own->table;
        unsigned int row = own->row;

        which &= 1u;
        if (__atomic_load_n(&table->spoiled_height[row][which], __ATOMIC_ACQUIRE) <= 0)
                return 0;
        *x = table->spoiled_x[row][which], *y = table->spoiled_y[row][which];
        *width = table->spoiled_width[row][which], *height = table->spoiled_height[row][which];
        __atomic_store_n(&table->spoiled_height[row][which], 0, __ATOMIC_RELEASE);
        return *width > 0 && *height > 0;
}

static void windows_discard(const struct window_row *own, unsigned int which)
{
        if (!__atomic_load_n(&own->table->pinned[own->row], __ATOMIC_ACQUIRE))
                window_call(WINDOW_SYS_MADVISE, (long)windows_pixels(own, which), (long)own->buffer, 9, 0, 0, 0);
}

static void windows_leave(struct window_row *own)
{
        window_call(WINDOW_SYS_CLOSE, own->door, 0, 0, 0, 0, 0);
        window_call(WINDOW_SYS_CLOSE, own->wake, 0, 0, 0, 0, 0);
        window_call(WINDOW_SYS_MUNMAP, (long)own->table, (long)own->bytes, 0, 0, 0, 0);
        own->table = 0;
}

#ifdef STANDARD_MODERN_C
static b32 file_write_atomic_at(b32 dir, string_address path, const void *bytes, positive length, positive mode)
{
        char stage[512];
        positive path_len = string_length(path);
        bipolar handle;
        b32 ok;

        if (path_len + 5 > sizeof(stage))
                return false;
        memory_copy(stage, path, path_len), memory_copy(stage + path_len, ".new", 5);
        if ((handle = system_open_at_mode(dir, (string_address)stage, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, mode)) < 0)
                return false;
        ok = !length || system_write_all((positive)handle, bytes, length) == (bipolar)length;
        system_close(handle);
        if (!ok || system_rename_at(dir, (string_address)stage, dir, path, 0) < 0)
                return system_remove_at(dir, (string_address)stage, 0), false;
        return true;
}
#endif

enum { WINDOWS_MFD_CLOEXEC = 1u, WINDOWS_MFD_SEALING = 2u, WINDOWS_F_ADD_SEALS = 1033, WINDOWS_F_GET_SEALS = 1034, WINDOWS_SEALS = 15, WINDOWS_CLIP_SEALS = 14, WINDOWS_CLIP_TRAILER = 20 };
#define WINDOWS_CLIP_MAGIC 0x317650494c43574dull
#define WINDOWS_CLIP_TEXT "text/plain;charset=utf-8"
#define WINDOWS_CLIP_URIS "text/uri-list"
#define WINDOWS_CLIP_PNG "image/png"

struct windows_clip_type { unsigned long offset, length; const char *name; unsigned long name_length; };
struct windows_clip_part { const char *name; const void *bytes; unsigned long length; };

static long windows_clip_held[2] = {-1, -1}, windows_drag_held = -1;

static unsigned long windows_clip_word(const unsigned char *at, unsigned int bytes)
{
        unsigned long value = 0;

        while (bytes--)
                value = value << 8 | at[bytes];
        return value;
}

static void windows_clip_le(unsigned char *at, unsigned long value, unsigned int bytes)
{
        for (unsigned int i = 0; i < bytes; i++)
                at[i] = (unsigned char)(value >> 8 * i);
}

static int windows_clip_same(const char *a, unsigned long a_length, const char *b)
{
#ifdef STANDARD_MODERN_C
        return string_length((string_address)b) == a_length && !memory_compare_ascii_case(a, b, a_length);
#else
        unsigned long at = 0;

        for (; at < a_length && b[at]; at++)
                if (((unsigned char)a[at] | ((unsigned char)a[at] >= 'A' && (unsigned char)a[at] <= 'Z' ? 32 : 0)) !=
                    ((unsigned char)b[at] | ((unsigned char)b[at] >= 'A' && (unsigned char)b[at] <= 'Z' ? 32 : 0)))
                        return 0;
        return at == a_length && !b[at];
#endif
}

static int windows_clip_text_rank(const char *mime, unsigned long len)
{
        static const char *const names[5] = {WINDOWS_CLIP_TEXT, "UTF8_STRING", "text/plain", "STRING", "TEXT"};

        for (int at = 0; mime && at < 5; at++)
                if (windows_clip_same(mime, len, names[at]))
                        return at + 1;
        return 0;
}

static int windows_clip_alias(const char *name, unsigned long length)
{
        int rank = windows_clip_text_rank(name, length);

        return !rank ? 0 : rank <= 3 ? 1 : 2;
}

static int windows_clip_text(const unsigned char *bytes, unsigned long length)
{
        return !length || (!memory_first_of((address_any)bytes, 0, (positive)length) &&
                           memory_utf8_valid_span((address_any)bytes, (positive)length).x == (positive)length);
}

static int windows_clip_next(const unsigned char *file, unsigned long size, unsigned long *cursor, struct windows_clip_type *type)
{
        unsigned long index, until, at;

        if (!file || size < WINDOWS_CLIP_TRAILER || windows_clip_word(file + size - 8, 8) != WINDOWS_CLIP_MAGIC)
                return 0;
        index = windows_clip_word(file + size - WINDOWS_CLIP_TRAILER, 8), until = size - WINDOWS_CLIP_TRAILER, at = *cursor ? *cursor : index;
        if (index > until || at < index || at + 19 > until)
                return 0;
        type->offset = windows_clip_word(file + at, 8), type->length = windows_clip_word(file + at + 8, 8);
        type->name_length = windows_clip_word(file + at + 16, 2), type->name = (const char *)file + at + 18;
        if (!type->name_length || type->offset > index || type->length > index - type->offset || type->name_length > until - at - 19 ||
            type->name[type->name_length])
                return 0;
        for (unsigned long k = 0; k < type->name_length; k++)
                if ((unsigned char)type->name[k] < 0x20 || (unsigned char)type->name[k] > 0x7e)
                        return 0;
        return *cursor = at + 19 + type->name_length, 1;
}

static int windows_clip_valid(const unsigned char *file, unsigned long size)
{
        struct windows_clip_type type;
        unsigned long cursor = 0, count = 0;

        if (!file || size < WINDOWS_CLIP_TRAILER || windows_clip_word(file + size - 8, 8) != WINDOWS_CLIP_MAGIC)
                return 0;
        while (windows_clip_next(file, size, &cursor, &type))
        {
                count++;
                if (windows_clip_same(type.name, type.name_length, WINDOWS_CLIP_TEXT) && !windows_clip_text(file + type.offset, type.length))
                        return 0;
        }
        return count && count == windows_clip_word(file + size - 12, 4) && cursor == size - WINDOWS_CLIP_TRAILER;
}

static int windows_clip_find(const unsigned char *file, unsigned long size, const char *name, unsigned long length, struct windows_clip_type *type)
{
        const char *want = windows_clip_alias(name, length) ? WINDOWS_CLIP_TEXT : 0;

        for (int pass = 0; pass < (want ? 2 : 1); pass++)
                for (unsigned long cursor = 0; windows_clip_next(file, size, &cursor, type);)
                        if (!pass && want ? windows_clip_same(type->name, type->name_length, want)
                                          : type->name_length == length && !memory_compare(type->name, name, (positive)length))
                                return 1;
        return 0;
}

static const char *windows_clip_offered(const unsigned char *file, unsigned long size, unsigned long nth, int x, unsigned long *length)
{
        static const char *const names[4] = {"UTF8_STRING", "text/plain", "TEXT", "STRING"};
        struct windows_clip_type type;
        unsigned long cursor = 0;
        int text = 0;

        while (windows_clip_next(file, size, &cursor, &type))
        {
                text |= windows_clip_same(type.name, type.name_length, WINDOWS_CLIP_TEXT);
                if (!nth--)
                        return *length = type.name_length, type.name;
        }
        for (int at = 0; text && at < (x ? 4 : 2); at++)
        {
                unsigned long n = string_length((string_address)names[at]);
                int held = 0;

                for (cursor = 0; !held && windows_clip_next(file, size, &cursor, &type);)
                        held = type.name_length == n && !memory_compare(type.name, names[at], (positive)n);
                if (!held && !nth--)
                        return *length = n, names[at];
        }
        return 0;
}

static int windows_clip_put(long fd, const void *bytes, unsigned long count)
{
        for (unsigned long done = 0; done < count;)
        {
                long wrote = window_call(WINDOW_SYS_WRITE, fd, (long)((const char *)bytes + done), (long)(count - done), 0, 0, 0);

                if (wrote <= 0)
                        return 0;
                done += (unsigned long)wrote;
        }
        return 1;
}

static int windows_clip_record(long index, unsigned long offset, unsigned long length, const char *name, unsigned long name_length)
{
        unsigned char head[18];

        if (!name_length || name_length > 0xffff)
                return 0;
        windows_clip_le(head, offset, 8), windows_clip_le(head + 8, length, 8), windows_clip_le(head + 16, name_length, 2);
        return windows_clip_put(index, head, 18) && windows_clip_put(index, name, name_length) && windows_clip_put(index, "", 1);
}

static int windows_clip_seal(long keep, long index, unsigned long until, unsigned long count)
{
        unsigned char bytes[4096], trailer[WINDOWS_CLIP_TRAILER];
        unsigned long size = (unsigned long)window_call(WINDOW_SYS_LSEEK, index, 0, 2, 0, 0, 0);

        if (window_call(WINDOW_SYS_LSEEK, keep, (long)until, 0, 0, 0, 0) != (long)until || (long)size < 0)
                return 0;
        for (unsigned long at = 0; at < size;)
        {
                long got = window_call(WINDOW_SYS_PREAD, index, (long)bytes, (long)(size - at > sizeof bytes ? sizeof bytes : size - at), (long)at, 0, 0);

                if (got <= 0 || !windows_clip_put(keep, bytes, (unsigned long)got))
                        return 0;
                at += (unsigned long)got;
        }
        windows_clip_le(trailer, until, 8), windows_clip_le(trailer + 8, count, 4), windows_clip_le(trailer + 12, WINDOWS_CLIP_MAGIC, 8);
        return windows_clip_put(keep, trailer, sizeof trailer) && window_call(WINDOW_SYS_FCNTL, keep, WINDOWS_F_ADD_SEALS, WINDOWS_SEALS, 0, 0, 0) >= 0;
}

static long windows_clip_build(const struct windows_clip_part *parts, unsigned long count)
{
        long fd = window_call(WINDOW_SYS_MEMFD, (long)"moonwater-copy", WINDOWS_MFD_CLOEXEC | WINDOWS_MFD_SEALING, 0, 0, 0, 0);
        long index = fd < 0 ? fd : window_call(WINDOW_SYS_MEMFD, (long)"moonwater-index", WINDOWS_MFD_CLOEXEC, 0, 0, 0, 0);
        unsigned long until = 0;
        int ok = index >= 0 && count;

        for (unsigned long at = 0; ok && at < count; at++)
                ok = windows_clip_put(fd, parts[at].bytes, parts[at].length) &&
                     windows_clip_record(index, until, parts[at].length, parts[at].name, string_length((string_address)parts[at].name)),
                until += parts[at].length;
        ok = ok && windows_clip_seal(fd, index, until, count);
        if (index >= 0)
                window_call(WINDOW_SYS_CLOSE, index, 0, 0, 0, 0, 0);
        if (!ok && fd >= 0)
                return window_call(WINDOW_SYS_CLOSE, fd, 0, 0, 0, 0, 0), -5;
        return fd;
}

static long windows_clip_memfd(const void *bytes, unsigned long length)
{
        struct windows_clip_part part = {WINDOWS_CLIP_TEXT, bytes, length};

        return windows_clip_build(&part, 1);
}

static void windows_fd_replace(long *slot, long fd)
{
        if (*slot >= 0)
                window_call(WINDOW_SYS_CLOSE, *slot, 0, 0, 0, 0, 0);
        *slot = fd;
}

static long windows_clip_offer_parts(const struct window_row *own, unsigned int which, const struct windows_clip_part *parts, unsigned long count)
{
        long fd = count ? windows_clip_build(parts, count) : -22;

        which &= 1u;
        if (fd < 0)
                return fd;
        windows_fd_replace(&windows_clip_held[which], fd);
        own->table->clip_offer_fd[own->row] = (int)fd, own->table->clip_offer_which[own->row] = which;
        windows_knock_bump(own, &own->table->clip_offer_serial[own->row]);
        return 0;
}

static long windows_clip_offer(const struct window_row *own, unsigned int which, const void *bytes, unsigned long length)
{
        struct windows_clip_part part = {WINDOWS_CLIP_TEXT, bytes, length};

        return length ? windows_clip_offer_parts(own, which, &part, 1) : -22;
}

static void windows_clip_done(unsigned int which) { windows_fd_replace(&windows_clip_held[which & 1u], -1); }

static void windows_clip_ask(const struct window_row *own, unsigned int which)
{
        own->table->clip_ask_which[own->row] = which & 1u;
        windows_knock_bump(own, &own->table->clip_ask_serial[own->row]);
}

static void windows_proc_fd(char *path, unsigned int pid, unsigned int number)
{
        windows_num2_path(path, 48, "/proc/", pid, "/fd/", number);
}

/* A clip file is read only when it is sealed against write, grow and shrink (the compositor's own rule, manage.c). */
static int windows_clip_sealed(long fd)
{
        long seals = window_call(WINDOW_SYS_FCNTL, fd, WINDOWS_F_GET_SEALS, 0, 0, 0, 0);

        return seals >= 0 && ((unsigned long)seals & WINDOWS_CLIP_SEALS) == WINDOWS_CLIP_SEALS;
}

static const unsigned char *windows_clip_map(unsigned int pid, unsigned int which, unsigned long want)
{
        char path[48];
        long fd, mapped;

        if (!pid || !want || want > WINDOWS_CLIP_MOST)
                return 0;
        windows_proc_fd(path, pid, WINDOWS_CLIP_FD + (which < 3 ? which : 0));
        if ((fd = window_call(WINDOW_SYS_OPENAT, -100, (long)path, 0x80000, 0, 0, 0)) < 0)
                return 0;
        /* Only a file sealed against write, grow and shrink is read: its bytes cannot change after this check,
           so the validation and every later read of the mapping see the same clip. */
        if (!windows_clip_sealed(fd))
        {
                window_call(WINDOW_SYS_CLOSE, fd, 0, 0, 0, 0, 0);
                return 0;
        }
        mapped = window_call(WINDOW_SYS_LSEEK, fd, 0, 2, 0, 0, 0) == (long)want ? window_call(WINDOW_SYS_MMAP, 0, (long)want, 1, 1, fd, 0) : -22;
        window_call(WINDOW_SYS_CLOSE, fd, 0, 0, 0, 0, 0);
        if (WINDOWS_MAP_ERR(mapped))
                return 0;
        if (!windows_clip_valid((const unsigned char *)mapped, want))
                return window_call(WINDOW_SYS_MUNMAP, mapped, (long)want, 0, 0, 0, 0), (const unsigned char *)0;
        return (const unsigned char *)mapped;
}

static long windows_clip_fetch_type(unsigned int pid, unsigned int which, unsigned long want, const char *name, unsigned char **bytes,
                                    unsigned long *length)
{
        const unsigned char *file = windows_clip_map(pid, which, want);
        struct windows_clip_type type;
        long mapped;

        *bytes = 0, *length = 0;
        if (!file)
                return -2;
        name = name ? name : WINDOWS_CLIP_TEXT;
        if (!windows_clip_find(file, want, name, string_length((string_address)name), &type))
                return window_call(WINDOW_SYS_MUNMAP, (long)file, (long)want, 0, 0, 0, 0), -61;
        mapped = window_call(WINDOW_SYS_MMAP, 0, (long)type.length + 1, 3, 0x22, -1, 0);
        if (!WINDOWS_MAP_ERR(mapped))
                memory_copy((address_any)mapped, (address_any)(file + type.offset), (positive)type.length);
        window_call(WINDOW_SYS_MUNMAP, (long)file, (long)want, 0, 0, 0, 0);
        if (WINDOWS_MAP_ERR(mapped))
                return mapped;
        return *bytes = (unsigned char *)mapped, *length = type.length, (long)type.length;
}

static long windows_clip_fetch(unsigned int pid, unsigned int which, unsigned long want, unsigned char **bytes, unsigned long *length)
{
        return windows_clip_fetch_type(pid, which, want, 0, bytes, length);
}

static long windows_clip_read_type(const struct window_row *own, unsigned int which, const char *name, unsigned char **bytes, unsigned long *length)
{
        return windows_clip_fetch_type(__atomic_load_n(&own->table->clip_pid, __ATOMIC_ACQUIRE), which,
                                       __atomic_load_n(&own->table->clip_given_length[own->row], __ATOMIC_ACQUIRE), name, bytes, length);
}

static long windows_clip_read(const struct window_row *own, unsigned int which, unsigned char **bytes, unsigned long *length)
{
        return windows_clip_read_type(own, which, 0, bytes, length);
}

static long windows_drag_offer(const struct window_row *own, const struct windows_clip_part *parts, unsigned long count, unsigned int actions)
{
        long fd = count && (actions & 7u) ? windows_clip_build(parts, count) : -22;

        if (fd < 0)
                return fd;
        windows_fd_replace(&windows_drag_held, fd);
        own->table->drag_offer_fd[own->row] = (int)fd;
        own->table->drag_offer_actions[own->row] = actions & (7u | 0xffu << WINDOWS_DRAG_KIND_SHIFT | 0xffffu << WINDOWS_DRAG_ITEMS_SHIFT);
        windows_knock_bump(own, &own->table->drag_offer_serial[own->row]);
        return 0;
}

static void windows_drag_over(void) { windows_fd_replace(&windows_drag_held, -1); }

static const unsigned char *windows_drag_types(const struct window_row *own, unsigned long *size)
{
        *size = __atomic_load_n(&own->table->clip_length[WINDOWS_CLIP_DRAG], __ATOMIC_ACQUIRE);
        return windows_clip_map(__atomic_load_n(&own->table->clip_pid, __ATOMIC_ACQUIRE), WINDOWS_CLIP_DRAG, *size);
}

static void windows_drop_take(const struct window_row *own, long nth, unsigned int actions, unsigned int preferred)
{
        own->table->drop_take[own->row] = nth < 0 ? -1 : (int)nth;
        own->table->drop_take_actions[own->row] = nth < 0 ? 0 : actions & 7u;
        own->table->drop_take_preferred[own->row] = preferred & 7u;
        windows_knock_bump(own, &own->table->drop_take_serial[own->row]);
}

static long windows_drag_read(const struct window_row *own, const char *name, unsigned char **bytes, unsigned long *length)
{
        return windows_clip_read_type(own, WINDOWS_CLIP_DRAG, name, bytes, length);
}

static void windows_drop_done(const struct window_row *own, int took)
{
        own->table->drop_done[own->row] = took ? 1u : 0u;
        windows_knock_bump(own, &own->table->drop_done_serial[own->row]);
}

static void windows_clip_free(unsigned char *bytes, unsigned long length)
{
        if (bytes)
                window_call(WINDOW_SYS_MUNMAP, (long)bytes, (long)length + 1, 0, 0, 0, 0);
}

static long window_colour_parse(string_address text)
{
        p8 digits[6], bytes[3];

        if (*text == '#')
                text++;
        if (string_length(text) == 3)
        {
                for (positive at = 0; at < 3; at++)
                        digits[at * 2] = digits[at * 2 + 1] = text[at];
                text = digits;
        }
        else if (string_length(text) != 6)
                return -1;
        return memory_from_hex(bytes, text, 3) ? (long)(bytes[0] << 16 | bytes[1] << 8 | bytes[2]) : -1;
}

#endif // WINDOW_INCLUDED
