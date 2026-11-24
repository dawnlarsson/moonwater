/*
        libVkLayer_MOONWATER_canvas.so: a game's Vulkan swapchain shown straight by Canvas (an implicit layer, i386 and x86_64)

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        A Vulkan game on an X window presents through Xwayland: Mesa's X11 swapchain hands each image to the X server with
        Present, the X server flips it onto its own surface of the window (or copies it there when it cannot), and Canvas shows
        that. The frame's timing is the X server's (its msc moves on Canvas's frame callbacks, a lost one a second's freeze), a
        lower resolution is the X server's emulated mode (a scaling copy), and nothing of Canvas's pacing reaches the game.
        winewayland's Vulkan draws to a subsurface of its window, which Canvas composes every frame.

        This layer keeps every surface the game makes as the game made it, and moves only the swapchain: at vkCreateSwapchainKHR
        it gives the swapchain a surface of its own on Canvas (a stand-in, canvas_swapchain_v1 in src/canvas/wayland.c), which
        Canvas shows as the window's picture: in place of the X window's (Canvas's X window manager vouches that this process is
        the X client that made it) or of winewayland's client subsurface (the same client, when the subsurface is the whole of its
        window). The swapchain is then Mesa's own Wayland swapchain on that surface: its dma-bufs are read in place or scanned out
        straight, its present modes are Wayland's (tearing is wp_tearing_control), its frames are paced by Canvas's frame callbacks
        (the compositor's JIT time, before its vblank), and a swapchain smaller than the window is scaled to it by the display's
        plane. The window itself stays the window: its input, focus, title and place are the X window's, untouched. Everything a
        stand-in cannot be (Canvas not there, another X server, a window not this process's, a part of a window) is left as it
        was: the swapchain goes to the game's own surface, and a stand-in Canvas refuses after it was taken (an answer that
        changed) makes the swapchain out of date once, so the game makes it again where it would have.

        From the one place every frame passes, it also gives:
          pacing: a FIFO swapchain waits in vkAcquireNextImageKHR for its last frame's callback (Canvas answers a fullscreen
            game's at its JIT time, the vblank less its render time), so the game starts each frame as late as still makes it;
            MOONWATER_WSI_PACE=0 turns it off. MOONWATER_WSI_FPS=N caps the frames a second, slept to a grid of 1/N s.
          render size: MOONWATER_WSI_SCALE=PERCENT (25 to 100) or MOONWATER_WSI_SIZE=WxH reports an X window's surface that much
            smaller (its current extent), for a game that sizes its swapchain by it; Canvas scales the picture up on the plane.
          present to photon: MOONWATER_WSI_LOG=PATH writes a line a present (P: CLOCK_MONOTONIC ns of the present, ns of the
            vblank it was shown at, the refresh ns, the feedback's flags, 8 being zero copy; D: discarded) and one an answer
            (A: ns, answer, window width, height) and a swapchain (C: ns, width, height, present mode, on the stand-in or not).

        Off: MOONWATER_WSI=0 (the manifest's enable_environment wants 1, which the steam session sets behind `wa canvas-wsi`),
        or MOONWATER_WSI_OFF=1, or /root/workarounds saying "canvas-wsi off" or "APP:canvas-wsi off" for the game's executable
        (its file's name, its comm, or its first argument's last part, a Windows path's included). Nothing runs at load.

        Built into the byte arrays in src/bowl.c (i386 and x86_64) with:
          clang -target {i386,x86_64}-linux-gnu -std=gnu11 -O2 -fPIC -shared -nostdlib -ffreestanding -fno-stack-protector \
                -fvisibility=hidden -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident -fuse-ld=lld \
                -Wl,-soname,libVkLayer_MOONWATER_canvas.so,-z,lazy,-z,undefs,--allow-shlib-undefined,-Bsymbolic,--hash-style=both,--build-id=none,-s \
                -o libVkLayer_MOONWATER_canvas.so src/shim/wsi.c
*/

#define WSI_EXPORT __attribute__((visibility("default")))

/* ---- Vulkan's words: the handles, structures and numbers this layer reads (vulkan_core.h, vk_layer.h; the same on both machines) ---- */

typedef int VkResult;
typedef unsigned int VkFlags, VkBool32, VkStructureType;
typedef unsigned long long VkSurfaceKHR, VkSwapchainKHR, VkSemaphore, VkFence;
typedef void *VkInstance, *VkPhysicalDevice, *VkDevice, *VkQueue;
typedef void (*PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction (*PFN_vkGetInstanceProcAddr)(VkInstance, const char *);
typedef PFN_vkVoidFunction (*PFN_vkGetDeviceProcAddr)(VkDevice, const char *);

#define VK_SUCCESS 0
#define VK_TIMEOUT 2
#define VK_SUBOPTIMAL_KHR 1000001003
#define VK_ERROR_INITIALIZATION_FAILED (-3)
#define VK_ERROR_EXTENSION_NOT_PRESENT (-7)
#define VK_ERROR_OUT_OF_DATE_KHR (-1000001004)
#define VK_ERROR_OUT_OF_HOST_MEMORY (-1)

#define VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO 47u
#define VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO 48u
#define VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR 1000006000u
#define VK_LAYER_LINK_INFO 0
#define VK_PRESENT_MODE_IMMEDIATE_KHR 0u
#define VK_PRESENT_MODE_MAILBOX_KHR 1u
#define VK_PRESENT_MODE_FIFO_KHR 2u
#define VK_PRESENT_MODE_FIFO_RELAXED_KHR 3u
#define VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR 1u
#define VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR 8u

struct VkBase { VkStructureType sType; const void *pNext; };
struct VkExtent2D { unsigned int width, height; };

struct VkInstanceCreateInfo
{
        VkStructureType sType;
        const void *pNext;
        VkFlags flags;
        const void *pApplicationInfo;
        unsigned int enabledLayerCount;
        const char *const *ppEnabledLayerNames;
        unsigned int enabledExtensionCount;
        const char *const *ppEnabledExtensionNames;
};

struct VkLayerInstanceLink
{
        struct VkLayerInstanceLink *pNext;
        PFN_vkGetInstanceProcAddr pfnNextGetInstanceProcAddr;
        PFN_vkVoidFunction pfnNextGetPhysicalDeviceProcAddr;
};

struct VkLayerInstanceCreateInfo
{
        VkStructureType sType;
        const void *pNext;
        int function;
        union { struct VkLayerInstanceLink *pLayerInfo; void *other; } u;
};

struct VkLayerDeviceLink
{
        struct VkLayerDeviceLink *pNext;
        PFN_vkGetInstanceProcAddr pfnNextGetInstanceProcAddr;
        PFN_vkGetDeviceProcAddr pfnNextGetDeviceProcAddr;
};

struct VkLayerDeviceCreateInfo
{
        VkStructureType sType;
        const void *pNext;
        int function;
        union { struct VkLayerDeviceLink *pLayerInfo; void *other; } u;
};

struct VkNegotiateLayerInterface
{
        int sType;
        void *pNext;
        unsigned int loaderLayerInterfaceVersion;
        PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr;
        PFN_vkGetDeviceProcAddr pfnGetDeviceProcAddr;
        PFN_vkVoidFunction pfnGetPhysicalDeviceProcAddr;
};

struct VkXlibSurfaceCreateInfoKHR { VkStructureType sType; const void *pNext; VkFlags flags; void *dpy; unsigned long window; };
struct VkXcbSurfaceCreateInfoKHR { VkStructureType sType; const void *pNext; VkFlags flags; void *connection; unsigned int window; };
struct VkWaylandSurfaceCreateInfoKHR { VkStructureType sType; const void *pNext; VkFlags flags; void *display; void *surface; };

struct VkSurfaceCapabilitiesKHR
{
        unsigned int minImageCount, maxImageCount;
        struct VkExtent2D currentExtent, minImageExtent, maxImageExtent;
        unsigned int maxImageArrayLayers;
        VkFlags supportedTransforms, currentTransform, supportedCompositeAlpha, supportedUsageFlags;
};
struct VkPhysicalDeviceSurfaceInfo2KHR { VkStructureType sType; const void *pNext; VkSurfaceKHR surface; };
struct VkSurfaceCapabilities2KHR { VkStructureType sType; void *pNext; struct VkSurfaceCapabilitiesKHR surfaceCapabilities; };
struct VkSurfaceFormatKHR { int format, colorSpace; };

struct VkSwapchainCreateInfoKHR
{
        VkStructureType sType;
        const void *pNext;
        VkFlags flags;
        VkSurfaceKHR surface;
        unsigned int minImageCount;
        int imageFormat, imageColorSpace;
        struct VkExtent2D imageExtent;
        unsigned int imageArrayLayers;
        VkFlags imageUsage;
        int imageSharingMode;
        unsigned int queueFamilyIndexCount;
        const unsigned int *pQueueFamilyIndices;
        VkFlags preTransform, compositeAlpha;
        unsigned int presentMode;
        VkBool32 clipped;
        VkSwapchainKHR oldSwapchain;
};

struct VkPresentInfoKHR
{
        VkStructureType sType;
        const void *pNext;
        unsigned int waitSemaphoreCount;
        const VkSemaphore *pWaitSemaphores;
        unsigned int swapchainCount;
        const VkSwapchainKHR *pSwapchains;
        const unsigned int *pImageIndices;
        VkResult *pResults;
};

struct VkAcquireNextImageInfoKHR
{
        VkStructureType sType;
        const void *pNext;
        VkSwapchainKHR swapchain;
        unsigned long long timeout;
        VkSemaphore semaphore;
        VkFence fence;
        unsigned int deviceMask;
};

typedef VkResult (*PFN_vkCreateInstance)(const struct VkInstanceCreateInfo *, const void *, VkInstance *);
typedef void (*PFN_vkDestroyInstance)(VkInstance, const void *);
typedef VkResult (*PFN_vkCreateDevice)(VkPhysicalDevice, const struct VkBase *, const void *, VkDevice *);
typedef void (*PFN_vkDestroyDevice)(VkDevice, const void *);
typedef VkResult (*PFN_vkCreateXlibSurfaceKHR)(VkInstance, const struct VkXlibSurfaceCreateInfoKHR *, const void *, VkSurfaceKHR *);
typedef VkResult (*PFN_vkCreateXcbSurfaceKHR)(VkInstance, const struct VkXcbSurfaceCreateInfoKHR *, const void *, VkSurfaceKHR *);
typedef VkResult (*PFN_vkCreateWaylandSurfaceKHR)(VkInstance, const struct VkWaylandSurfaceCreateInfoKHR *, const void *, VkSurfaceKHR *);
typedef void (*PFN_vkDestroySurfaceKHR)(VkInstance, VkSurfaceKHR, const void *);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(VkPhysicalDevice, VkSurfaceKHR, struct VkSurfaceCapabilitiesKHR *);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR)(VkPhysicalDevice, const struct VkPhysicalDeviceSurfaceInfo2KHR *,
                                                                   struct VkSurfaceCapabilities2KHR *);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)(VkPhysicalDevice, VkSurfaceKHR, unsigned int *, unsigned int *);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)(VkPhysicalDevice, VkSurfaceKHR, unsigned int *, struct VkSurfaceFormatKHR *);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceSupportKHR)(VkPhysicalDevice, unsigned int, VkSurfaceKHR, VkBool32 *);
typedef VkResult (*PFN_vkCreateSwapchainKHR)(VkDevice, const struct VkSwapchainCreateInfoKHR *, const void *, VkSwapchainKHR *);
typedef void (*PFN_vkDestroySwapchainKHR)(VkDevice, VkSwapchainKHR, const void *);
typedef VkResult (*PFN_vkAcquireNextImageKHR)(VkDevice, VkSwapchainKHR, unsigned long long, VkSemaphore, VkFence, unsigned int *);
typedef VkResult (*PFN_vkAcquireNextImage2KHR)(VkDevice, const struct VkAcquireNextImageInfoKHR *, unsigned int *);
typedef VkResult (*PFN_vkQueuePresentKHR)(VkQueue, const struct VkPresentInfoKHR *);

/* ---- libwayland-client's, by dlsym from the copy the driver loaded (the interfaces' layout is the library's ABI) ---- */

struct wl_message { const char *name, *signature; const struct wl_interface **types; };
struct wl_interface { const char *name; int version, method_count; const struct wl_message *methods; int event_count; const struct wl_message *events; };
struct wl_proxy;
struct wl_display;
struct wl_event_queue;

static struct
{
        struct wl_display *(*display_connect)(const char *);
        struct wl_event_queue *(*display_create_queue)(struct wl_display *);
        int (*display_roundtrip_queue)(struct wl_display *, struct wl_event_queue *);
        int (*display_dispatch_queue_pending)(struct wl_display *, struct wl_event_queue *);
        int (*display_prepare_read_queue)(struct wl_display *, struct wl_event_queue *);
        int (*display_read_events)(struct wl_display *);
        void (*display_cancel_read)(struct wl_display *);
        int (*display_flush)(struct wl_display *);
        int (*display_get_fd)(struct wl_display *);
        void *(*proxy_create_wrapper)(void *);
        void (*proxy_wrapper_destroy)(void *);
        void (*proxy_set_queue)(struct wl_proxy *, struct wl_event_queue *);
        struct wl_proxy *(*proxy_marshal_constructor)(struct wl_proxy *, unsigned int, const struct wl_interface *, ...);
        struct wl_proxy *(*proxy_marshal_constructor_versioned)(struct wl_proxy *, unsigned int, const struct wl_interface *, unsigned int, ...);
        void (*proxy_marshal)(struct wl_proxy *, unsigned int, ...);
        int (*proxy_add_listener)(struct wl_proxy *, void (**)(void), void *);
        void (*proxy_destroy)(struct wl_proxy *);
        const struct wl_interface *registry_interface, *compositor_interface, *surface_interface, *callback_interface;
        int looked, ready;
} wl;

/* ---- the C library's, resolved when the game's process loads this one ---- */

struct wsi_timespec { long seconds, nanoseconds; };
struct wsi_pollfd { int fd; short events, revents; };

extern char *getenv(const char *);
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern void *malloc(unsigned long);
extern void *calloc(unsigned long, unsigned long);
extern void *realloc(void *, unsigned long);
extern void free(void *);
extern int open(const char *, int, ...);
extern long read(int, void *, unsigned long);
extern long write(int, const void *, unsigned long);
extern int close(int);
extern int clock_gettime(int, struct wsi_timespec *);
extern int clock_nanosleep(int, int, const struct wsi_timespec *, struct wsi_timespec *);
extern int poll(struct wsi_pollfd *, unsigned long, int);
extern int sched_yield(void);
extern long readlink(const char *, char *, unsigned long);
extern int __cxa_atexit(void (*)(void *), void *, void *);

#define WSI_RTLD_NOW 2
#define WSI_RTLD_NOLOAD 4
#define WSI_CLOCK_MONOTONIC 1
#define WSI_TIMER_ABSTIME 1

/* ---- small things done here rather than asked of a library ---- */

static unsigned long long wsi_now(void)
{
        struct wsi_timespec now = {0, 0};

        clock_gettime(WSI_CLOCK_MONOTONIC, &now);
        return (unsigned long long)now.seconds * 1000000000ull + (unsigned long long)now.nanoseconds;
}

// A 64-bit number over a 32-bit one by shifts: a 64-bit division on i386 is libgcc's __udivdi3, which no C library exports.
static unsigned long long wsi_divide(unsigned long long value, unsigned int by)
{
        unsigned long long quotient = 0, rest = 0;

        for (int bit = 63; bit >= 0; bit--)
        {
                rest = rest << 1 | (value >> bit & 1);
                if (rest >= by)
                        rest -= by, quotient |= 1ull << bit;
        }
        return quotient;
}

static int wsi_same(const char *a, const char *b)
{
        while (*a && *a == *b)
                a++, b++;
        return *a == *b;
}

static unsigned int wsi_length(const char *text)
{
        unsigned int length = 0;

        while (text[length])
                length++;
        return length;
}

// A decimal of the environment's (or the default when it is unset or not a number).
static long wsi_env_number(const char *name, long fallback)
{
        const char *text = getenv(name);
        long value = 0;

        if (!text || !*text)
                return fallback;
        for (; *text >= '0' && *text <= '9'; text++)
                value = value * 10 + (*text - '0');
        return *text && *text != 'x' && *text != '%' ? fallback : value;
}

// The spin lock of the tables: held for a few loads and stores, or the rare round trip of a surface's making.
static int wsi_held;
static void wsi_lock(void)
{
        while (__atomic_exchange_n(&wsi_held, 1, __ATOMIC_ACQUIRE))
                sched_yield();
}
static void wsi_unlock(void) { __atomic_store_n(&wsi_held, 0, __ATOMIC_RELEASE); }

/* ---- the log (MOONWATER_WSI_LOG) ---- */

#define WSI_LOG_LINES 256u

static struct
{
        int fd;      // the log's descriptor plus one: 0 for none (the table is zero, so it takes no room in the file)
        unsigned int count;
        struct { unsigned long long a, b, c; long d, e; char what; } line[WSI_LOG_LINES];
} wsi_log;

static unsigned int wsi_decimal(char *into, unsigned long long value)
{
        static const unsigned long long powers[20] = {
                10000000000000000000ull, 1000000000000000000ull, 100000000000000000ull, 10000000000000000ull, 1000000000000000ull,
                100000000000000ull, 10000000000000ull, 1000000000000ull, 100000000000ull, 10000000000ull, 1000000000ull, 100000000ull,
                10000000ull, 1000000ull, 100000ull, 10000ull, 1000ull, 100ull, 10ull, 1ull};
        unsigned int at = 0, started = 0;

        for (unsigned int place = 0; place < 20; place++)
        {
                char digit = '0';

                while (value >= powers[place])
                        value -= powers[place], digit++;
                if (digit != '0' || started || place == 19)
                        into[at++] = digit, started = 1;
        }
        return at;
}

static void wsi_log_flush(void)
{
        char text[4096];
        unsigned int length = 0;

        for (unsigned int at = 0; at < wsi_log.count; at++)
        {
                unsigned long long value[5] = {wsi_log.line[at].a, wsi_log.line[at].b, wsi_log.line[at].c, (unsigned long long)wsi_log.line[at].d,
                                               (unsigned long long)wsi_log.line[at].e};

                if (length > sizeof text - 128)
                {
                        if (write(wsi_log.fd - 1, text, length) < 0)
                                break;
                        length = 0;
                }
                text[length++] = wsi_log.line[at].what;
                for (unsigned int k = 0; k < 5; k++)
                        text[length++] = ' ', length += wsi_decimal(text + length, value[k]);
                text[length++] = '\n';
        }
        if (length && wsi_log.fd)
                write(wsi_log.fd - 1, text, length);
        wsi_log.count = 0;
}

static void wsi_log_end(void *unused)
{
        (void)unused;
        wsi_lock(), wsi_log_flush(), wsi_unlock();
}

static void wsi_log_open(void)
{
        const char *path = getenv("MOONWATER_WSI_LOG");
        int fd;

        // O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC
        if (path && *path && (fd = open(path, 1 | 0x40 | 0x400 | 0x80000, 0644)) >= 0)
                wsi_log.fd = fd + 1, __cxa_atexit(wsi_log_end, 0, 0);
}

// One line (under the lock); nothing when no log is asked for.
static void wsi_note(char what, unsigned long long a, unsigned long long b, unsigned long long c, long d, long e)
{
        if (!wsi_log.fd)
                return;
        if (wsi_log.count == WSI_LOG_LINES)
                wsi_log_flush();
        wsi_log.line[wsi_log.count].what = what, wsi_log.line[wsi_log.count].a = a, wsi_log.line[wsi_log.count].b = b;
        wsi_log.line[wsi_log.count].c = c, wsi_log.line[wsi_log.count].d = d, wsi_log.line[wsi_log.count].e = e;
        wsi_log.count++;
        // An answer or a swapchain is written at once (a game ended by a signal runs no exit handler); a present's line waits for more.
        if (what != 'P' && what != 'D')
                wsi_log_flush();
}

/* ---- the tables: an instance, a device, a surface and a swapchain each a row, found by key ---- */

struct wsi_instance
{
        void *key;
        VkInstance instance;
        PFN_vkGetInstanceProcAddr next;
        int active;
        PFN_vkDestroyInstance destroy;
        PFN_vkCreateDevice create_device;
        PFN_vkCreateXlibSurfaceKHR create_xlib;
        PFN_vkCreateXcbSurfaceKHR create_xcb;
        PFN_vkCreateWaylandSurfaceKHR create_wayland;
        PFN_vkDestroySurfaceKHR destroy_surface;
        PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR caps;
        PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR caps2;
        PFN_vkGetPhysicalDeviceSurfacePresentModesKHR modes;
        PFN_vkGetPhysicalDeviceSurfaceFormatsKHR formats;
        PFN_vkGetPhysicalDeviceSurfaceSupportKHR support;
};

struct wsi_device
{
        void *key;
        VkDevice device;
        VkPhysicalDevice physical;
        struct wsi_instance *instance;
        PFN_vkGetDeviceProcAddr next;
        PFN_vkDestroyDevice destroy;
        PFN_vkCreateSwapchainKHR create_swapchain;
        PFN_vkDestroySwapchainKHR destroy_swapchain;
        PFN_vkAcquireNextImageKHR acquire;
        PFN_vkAcquireNextImage2KHR acquire2;
        PFN_vkQueuePresentKHR present;
};

// A connection to Canvas the layer speaks on: its own (an X window's stand-ins), or the game's (winewayland's), with a queue of its own.
struct wsi_link
{
        struct wl_display *display;
        struct wl_event_queue *queue;
        struct wl_proxy *wrapper, *registry, *compositor, *manager, *presentation;
        unsigned int compositor_name, compositor_version, manager_name, presentation_name;
};

enum { WSI_X11 = 1, WSI_WAYLAND = 2 };
enum { WSI_SHOWN = 0, WSI_WAITING = 1, WSI_UNKNOWN = 2, WSI_NOT_OWNER = 3, WSI_PARTIAL = 4, WSI_OWN = 5, WSI_UNASKED = 255 };

struct wsi_surface
{
        VkSurfaceKHR surface;          // the game's, as the driver made it
        struct wsi_instance *instance;
        int kind;
        void *xcb;                     // the X connection (an Xlib display's, for an Xlib surface)
        unsigned int window, top;      // the X window the surface is, and its toplevel (a child of the root)
        void *wl_display, *wl_surface; // a Wayland surface's
        struct wsi_link *link;
        struct wl_proxy *stand;        // the stand-in, on link
        VkSurfaceKHR stand_surface;    // the driver's surface of the stand-in
        unsigned int answer, refused;  // Canvas's last answer; refused: a stand-in is not to be asked for again
        int width, height;             // the window's size, as the answer said
};

struct wsi_swapchain
{
        VkSwapchainKHR swapchain;
        struct wsi_device *device;
        struct wsi_surface *surface;
        int stood, pace, stale;
        unsigned int mode, width, height;
        struct wl_event_queue *queue;  // the swapchain's own queue: its callbacks and feedback, dispatched by the thread that presents it
        void *target;                  // the surface's wrapper on that queue (callbacks and feedback are asked through it)
        void *presentation;            // wp_presentation's wrapper on that queue
        struct wl_proxy *callback;
        unsigned long long cap_next, refresh;
        struct wsi_feedback *feedback; // the feedback asked and not yet answered
};

struct wsi_feedback { struct wsi_feedback *next; struct wsi_swapchain *swapchain; struct wl_proxy *proxy; unsigned long long at; };

static struct
{
        struct wsi_instance **instances;
        struct wsi_device **devices;
        struct wsi_surface **surfaces;
        struct wsi_swapchain **swapchains;
        unsigned int instance_count, device_count, surface_count, swapchain_count;
        unsigned int instance_room, device_room, surface_room, swapchain_room;
        struct wsi_link *own;          // the layer's own connection to Canvas, made once
        struct wsi_link **links;       // the game's connections the layer spoke on
        unsigned int link_count, link_room;
        int looked, off, own_looked;
        long pace, fps, scale, size_w, size_h;
} wsi;

// A row added to a growing table (no fixed count): 0 when there is no memory.
static int wsi_append(void ***table, unsigned int *count, unsigned int *room, void *row)
{
        if (*count == *room)
        {
                unsigned int grown = *room ? *room * 2 : 8;
                void **moved = (void **)realloc(*table, grown * sizeof(void *));

                if (!moved)
                        return 0;
                *table = moved, *room = grown;
        }
        (*table)[(*count)++] = row;
        return 1;
}

static void wsi_remove(void **table, unsigned int *count, void *row)
{
        for (unsigned int at = 0; at < *count; at++)
                if (table[at] == row)
                {
                        table[at] = table[--*count];
                        return;
                }
}

static void *wsi_key(const void *handle) { return handle ? *(void *const *)handle : 0; }

static struct wsi_instance *wsi_instance_of(const void *handle)
{
        void *key = wsi_key(handle);
        struct wsi_instance *found = 0;

        wsi_lock();
        for (unsigned int at = 0; at < wsi.instance_count && !found; at++)
                if (wsi.instances[at]->key == key)
                        found = wsi.instances[at];
        wsi_unlock();
        return found;
}

static struct wsi_device *wsi_device_of(const void *handle)
{
        void *key = wsi_key(handle);
        struct wsi_device *found = 0;

        wsi_lock();
        for (unsigned int at = 0; at < wsi.device_count && !found; at++)
                if (wsi.devices[at]->key == key)
                        found = wsi.devices[at];
        wsi_unlock();
        return found;
}

static struct wsi_surface *wsi_surface_of(VkSurfaceKHR surface)
{
        struct wsi_surface *found = 0;

        wsi_lock();
        for (unsigned int at = 0; at < wsi.surface_count && !found; at++)
                if (wsi.surfaces[at]->surface == surface)
                        found = wsi.surfaces[at];
        wsi_unlock();
        return found;
}

static struct wsi_swapchain *wsi_swapchain_of(VkSwapchainKHR swapchain)
{
        struct wsi_swapchain *found = 0;

        wsi_lock();
        for (unsigned int at = 0; at < wsi.swapchain_count && !found; at++)
                if (wsi.swapchains[at]->swapchain == swapchain)
                        found = wsi.swapchains[at];
        wsi_unlock();
        return found;
}

/* ---- whether this process wants the layer at all ---- */

// The last part of a path, a Windows path's too.
static const char *wsi_base(const char *path)
{
        const char *base = path;

        for (const char *at = path; *at; at++)
                if (*at == '/' || *at == '\\')
                        base = at + 1;
        return base;
}

// Whether text (length bytes, lines) says "NAME off", or "APP:NAME off" for one of the apps.
static int wsi_said_off(const char *text, long length, const char *const *apps, unsigned int app_count)
{
        static const char name[] = "canvas-wsi";

        for (long at = 0; at < length;)
        {
                long end = at, colon = -1;

                while (end < length && text[end] != '\n')
                        end++;
                for (long k = at; k < end; k++)
                        if (text[k] == ':' && colon < 0)
                                colon = k;
                if (end - at >= 4 && text[end - 4] == ' ' && text[end - 3] == 'o' && text[end - 2] == 'f' && text[end - 1] == 'f')
                {
                        long key = colon >= 0 ? colon + 1 : at, key_length = end - 4 - key;

                        if (key_length == (long)sizeof name - 1)
                        {
                                int same = 1;

                                for (long k = 0; k < key_length && same; k++)
                                        same = text[key + k] == name[k];
                                if (same && colon < 0)
                                        return 1;
                                for (unsigned int app = 0; same && app < app_count; app++)
                                {
                                        unsigned int app_length = wsi_length(apps[app]);
                                        int match = app_length && (long)app_length == colon - at;

                                        for (unsigned int k = 0; match && k < app_length; k++)
                                                match = text[at + k] == apps[app][k];
                                        if (match)
                                                return 1;
                                }
                        }
                }
                at = end + 1;
        }
        return 0;
}

static void wsi_look(void)
{
        char text[4096], exe[512], comm[64], first[512];
        const char *apps[3];
        unsigned int app_count = 0;
        long got;
        int fd;
        const char *on = getenv("MOONWATER_WSI");

        wsi.looked = 1;
        wsi_log_open();
        wsi.pace = wsi_env_number("MOONWATER_WSI_PACE", 1), wsi.fps = wsi_env_number("MOONWATER_WSI_FPS", 0);
        wsi.scale = wsi_env_number("MOONWATER_WSI_SCALE", 100);
        wsi.scale = wsi.scale < 25 ? 25 : wsi.scale > 100 ? 100 : wsi.scale;
        {
                const char *size = getenv("MOONWATER_WSI_SIZE");
                long w = 0, h = 0;

                for (; size && *size >= '0' && *size <= '9'; size++)
                        w = w * 10 + (*size - '0');
                if (size && *size == 'x')
                        for (size++; *size >= '0' && *size <= '9'; size++)
                                h = h * 10 + (*size - '0');
                if (w >= 64 && h >= 64 && w <= 16384 && h <= 16384)
                        wsi.size_w = w, wsi.size_h = h;
        }
        if ((on && wsi_same(on, "0")) || getenv("MOONWATER_WSI_OFF"))
        {
                wsi.off = 1;
                return;
        }
        if ((got = readlink("/proc/self/exe", exe, sizeof exe - 1)) > 0)
                exe[got] = 0, apps[app_count++] = wsi_base(exe);
        if ((fd = open("/proc/self/comm", 0 | 0x80000)) >= 0)
        {
                got = read(fd, comm, sizeof comm - 1);
                close(fd);
                if (got > 0)
                {
                        comm[got] = 0;
                        if (comm[got - 1] == '\n')
                                comm[got - 1] = 0;
                        apps[app_count++] = comm;
                }
        }
        if ((fd = open("/proc/self/cmdline", 0 | 0x80000)) >= 0)
        {
                got = read(fd, first, sizeof first - 1);
                close(fd);
                if (got > 0)
                        first[got] = 0, apps[app_count++] = wsi_base(first);
        }
        // O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC
        if ((fd = open("/root/workarounds", 0 | 0x20000 | 0x800 | 0x80000)) >= 0)
        {
                got = read(fd, text, sizeof text);
                close(fd);
                if (got > 0 && wsi_said_off(text, got, apps, app_count))
                        wsi.off = 1;
        }
}

/* ---- Canvas's protocol, and the two of presentation-time this layer asks for (their tables as wayland-scanner makes them) ---- */

static const struct wl_interface *wsi_types_surface[4], *wsi_types_two_surfaces[2], *wsi_types_feedback[2];
static const struct wl_interface *wsi_types_none[8];

static const struct wl_message wsi_manager_requests[] = {
        {"destroy", "", wsi_types_none}, {"override_window", "ou", wsi_types_surface}, {"override_surface", "oo", wsi_types_two_surfaces}};
static const struct wl_message wsi_manager_events[] = {{"answer", "ouii", wsi_types_surface}};
static const struct wl_interface wsi_manager_interface = {"canvas_swapchain_v1", 1, 3, wsi_manager_requests, 1, wsi_manager_events};

static const struct wl_message wsi_feedback_events[] = {
        {"sync_output", "o", wsi_types_none}, {"presented", "uuuuuuu", wsi_types_none}, {"discarded", "", wsi_types_none}};
static const struct wl_interface wsi_feedback_interface = {"wp_presentation_feedback", 1, 0, 0, 3, wsi_feedback_events};
static const struct wl_message wsi_presentation_requests[] = {{"destroy", "", wsi_types_none}, {"feedback", "on", wsi_types_feedback}};
static const struct wl_message wsi_presentation_events[] = {{"clock_id", "u", wsi_types_none}};
static const struct wl_interface wsi_presentation_interface = {"wp_presentation", 1, 2, wsi_presentation_requests, 1, wsi_presentation_events};

#define WSI_DISPLAY_GET_REGISTRY 1u
#define WSI_REGISTRY_BIND 0u
#define WSI_COMPOSITOR_CREATE_SURFACE 0u
#define WSI_SURFACE_DESTROY 0u
#define WSI_SURFACE_FRAME 3u
#define WSI_MANAGER_OVERRIDE_WINDOW 1u
#define WSI_MANAGER_OVERRIDE_SURFACE 2u
#define WSI_PRESENTATION_FEEDBACK 1u
#define WSI_FEEDBACK_KIND_ZERO_COPY 8u

// libwayland-client, the copy in the process (the driver's), or loaded now; 0 when there is none.
static int wsi_wayland(void)
{
        void *library;

        if (wl.looked)
                return wl.ready;
        wl.looked = 1;
        if (!(library = dlopen("libwayland-client.so.0", WSI_RTLD_NOW | WSI_RTLD_NOLOAD)) && !(library = dlopen("libwayland-client.so.0", WSI_RTLD_NOW)))
                return 0;
#define WSI_WL(field, symbol) *(void **)&wl.field = dlsym(library, symbol)
        WSI_WL(display_connect, "wl_display_connect");
        WSI_WL(display_create_queue, "wl_display_create_queue");
        WSI_WL(display_roundtrip_queue, "wl_display_roundtrip_queue");
        WSI_WL(display_dispatch_queue_pending, "wl_display_dispatch_queue_pending");
        WSI_WL(display_prepare_read_queue, "wl_display_prepare_read_queue");
        WSI_WL(display_read_events, "wl_display_read_events");
        WSI_WL(display_cancel_read, "wl_display_cancel_read");
        WSI_WL(display_flush, "wl_display_flush");
        WSI_WL(display_get_fd, "wl_display_get_fd");
        WSI_WL(proxy_create_wrapper, "wl_proxy_create_wrapper");
        WSI_WL(proxy_wrapper_destroy, "wl_proxy_wrapper_destroy");
        WSI_WL(proxy_set_queue, "wl_proxy_set_queue");
        WSI_WL(proxy_marshal_constructor, "wl_proxy_marshal_constructor");
        WSI_WL(proxy_marshal_constructor_versioned, "wl_proxy_marshal_constructor_versioned");
        WSI_WL(proxy_marshal, "wl_proxy_marshal");
        WSI_WL(proxy_add_listener, "wl_proxy_add_listener");
        WSI_WL(proxy_destroy, "wl_proxy_destroy");
        WSI_WL(registry_interface, "wl_registry_interface");
        WSI_WL(compositor_interface, "wl_compositor_interface");
        WSI_WL(surface_interface, "wl_surface_interface");
        WSI_WL(callback_interface, "wl_callback_interface");
#undef WSI_WL
        if (!wl.display_connect || !wl.display_create_queue || !wl.display_roundtrip_queue || !wl.display_dispatch_queue_pending ||
            !wl.display_prepare_read_queue || !wl.display_read_events || !wl.display_cancel_read || !wl.display_flush || !wl.display_get_fd ||
            !wl.proxy_create_wrapper || !wl.proxy_wrapper_destroy || !wl.proxy_set_queue || !wl.proxy_marshal_constructor ||
            !wl.proxy_marshal_constructor_versioned || !wl.proxy_marshal || !wl.proxy_add_listener || !wl.proxy_destroy ||
            !wl.registry_interface || !wl.compositor_interface || !wl.surface_interface || !wl.callback_interface)
                return 0;
        wsi_types_surface[0] = wl.surface_interface;
        wsi_types_two_surfaces[0] = wsi_types_two_surfaces[1] = wl.surface_interface;
        wsi_types_feedback[0] = wl.surface_interface, wsi_types_feedback[1] = &wsi_feedback_interface;
        return wl.ready = 1;
}

static void wsi_registry_global(void *data, struct wl_proxy *registry, unsigned int name, const char *interface, unsigned int version)
{
        struct wsi_link *link = (struct wsi_link *)data;

        (void)registry;
        if (wsi_same(interface, "wl_compositor"))
                link->compositor_name = name, link->compositor_version = version;
        else if (wsi_same(interface, "canvas_swapchain_v1"))
                link->manager_name = name;
        else if (wsi_same(interface, "wp_presentation"))
                link->presentation_name = name;
}

static void wsi_registry_remove(void *data, struct wl_proxy *registry, unsigned int name) { (void)data, (void)registry, (void)name; }

static void (*const wsi_registry_listener[])(void) = {(void (*)(void))wsi_registry_global, (void (*)(void))wsi_registry_remove};

static void wsi_answer(void *data, struct wl_proxy *manager, struct wl_proxy *surface, unsigned int answer, int width, int height);
static void (*const wsi_manager_listener[])(void) = {(void (*)(void))wsi_answer};

static struct wl_proxy *wsi_bind(struct wsi_link *link, unsigned int name, const struct wl_interface *interface, unsigned int version)
{
        return wl.proxy_marshal_constructor_versioned(link->registry, WSI_REGISTRY_BIND, interface, version, name, interface->name, version, (void *)0);
}

// A connection's globals, on a queue of the layer's own: 0 when it is not Canvas's (no canvas_swapchain_v1).
static struct wsi_link *wsi_link_make(struct wl_display *display)
{
        struct wsi_link *link = (struct wsi_link *)calloc(1, sizeof *link);

        if (!link || !(link->queue = wl.display_create_queue(display)) || !(link->wrapper = wl.proxy_create_wrapper(display)))
                return free(link), (struct wsi_link *)0;
        link->display = display;
        wl.proxy_set_queue((struct wl_proxy *)link->wrapper, link->queue);
        if (!(link->registry = wl.proxy_marshal_constructor((struct wl_proxy *)link->wrapper, WSI_DISPLAY_GET_REGISTRY, wl.registry_interface, (void *)0)))
                return link;
        wl.proxy_add_listener(link->registry, (void (**)(void))wsi_registry_listener, link);
        if (wl.display_roundtrip_queue(display, link->queue) < 0 || !link->manager_name || !link->compositor_name)
                return link;
        link->compositor = wsi_bind(link, link->compositor_name, wl.compositor_interface, link->compositor_version < 4 ? link->compositor_version : 4);
        link->manager = wsi_bind(link, link->manager_name, &wsi_manager_interface, 1);
        if (link->presentation_name)
                link->presentation = wsi_bind(link, link->presentation_name, &wsi_presentation_interface, 1);
        if (link->manager)
                wl.proxy_add_listener(link->manager, (void (**)(void))wsi_manager_listener, link);
        wl.display_roundtrip_queue(display, link->queue);
        return link;
}

// The layer's own connection to Canvas, for an X window's stand-ins: WAYLAND_DISPLAY's, or Canvas's socket.
static struct wsi_link *wsi_own(void)
{
        struct wl_display *display;

        if (wsi.own_looked)
                return wsi.own;
        wsi.own_looked = 1;
        if (!wsi_wayland())
                return 0;
        display = wl.display_connect(0);
        if (!display && !getenv("WAYLAND_DISPLAY"))
                display = wl.display_connect("/run/moonwater/wayland-0");
        if (display)
                wsi.own = wsi_link_make(display);
        return wsi.own;
}

// The layer's speaking on a game's own connection (winewayland's), once a connection.
static struct wsi_link *wsi_link_of(void *display)
{
        struct wsi_link *link = 0;

        for (unsigned int at = 0; at < wsi.link_count && !link; at++)
                if (wsi.links[at]->display == display)
                        link = wsi.links[at];
        if (!link && wsi_wayland() && (link = wsi_link_make((struct wl_display *)display)))
                wsi_append((void ***)&wsi.links, &wsi.link_count, &wsi.link_room, link);
        return link;
}

// Canvas's answer to a stand-in: the surface's whose stand-in it is (any thread, the queue's dispatcher holds the lock).
static void wsi_answer(void *data, struct wl_proxy *manager, struct wl_proxy *surface, unsigned int answer, int width, int height)
{
        (void)data, (void)manager;
        for (unsigned int at = 0; at < wsi.surface_count; at++)
        {
                struct wsi_surface *s = wsi.surfaces[at];

                if (s->stand != surface)
                        continue;
                s->answer = answer, s->width = width, s->height = height;
                if (answer != WSI_SHOWN && answer != WSI_WAITING)
                        s->refused = 1;
                for (unsigned int k = 0; k < wsi.swapchain_count; k++)
                        if (wsi.swapchains[k]->surface == s && wsi.swapchains[k]->stood && s->refused)
                                wsi.swapchains[k]->stale = 1;
                wsi_note('A', wsi_now(), answer, 0, width, height);
        }
}

// Events of a link's queue now, without waiting (under the lock).
static void wsi_link_pump(struct wsi_link *link)
{
        struct wsi_pollfd watch;

        if (!link || !link->manager)
                return;
        while (wl.display_prepare_read_queue(link->display, link->queue) != 0)
                wl.display_dispatch_queue_pending(link->display, link->queue);
        wl.display_flush(link->display);
        watch.fd = wl.display_get_fd(link->display), watch.events = 1, watch.revents = 0;
        if (poll(&watch, 1, 0) > 0)
                wl.display_read_events(link->display);
        else
                wl.display_cancel_read(link->display);
        wl.display_dispatch_queue_pending(link->display, link->queue);
}

/* ---- the X side: the toplevel of the game's X window, and its size (the game's own X connection, through its libxcb) ---- */

struct wsi_xcb_cookie { unsigned int sequence; };
struct wsi_xcb_tree { unsigned char response_type, pad0; unsigned short sequence; unsigned int length, root, parent; unsigned short children_len; };
struct wsi_xcb_geometry { unsigned char response_type, depth; unsigned short sequence; unsigned int length, root; short x, y; unsigned short width, height, border; };

static struct
{
        struct wsi_xcb_cookie (*query_tree)(void *, unsigned int);
        struct wsi_xcb_tree *(*query_tree_reply)(void *, struct wsi_xcb_cookie, void **);
        struct wsi_xcb_cookie (*get_geometry)(void *, unsigned int);
        struct wsi_xcb_geometry *(*get_geometry_reply)(void *, struct wsi_xcb_cookie, void **);
        void *(*xlib_connection)(void *);
        int looked, ready;
} xcb;

static int wsi_xcb(void)
{
        void *library, *xlib;

        if (xcb.looked)
                return xcb.ready;
        xcb.looked = 1;
        if (!(library = dlopen("libxcb.so.1", WSI_RTLD_NOW | WSI_RTLD_NOLOAD)) && !(library = dlopen("libxcb.so.1", WSI_RTLD_NOW)))
                return 0;
        *(void **)&xcb.query_tree = dlsym(library, "xcb_query_tree");
        *(void **)&xcb.query_tree_reply = dlsym(library, "xcb_query_tree_reply");
        *(void **)&xcb.get_geometry = dlsym(library, "xcb_get_geometry");
        *(void **)&xcb.get_geometry_reply = dlsym(library, "xcb_get_geometry_reply");
        if ((xlib = dlopen("libX11-xcb.so.1", WSI_RTLD_NOW | WSI_RTLD_NOLOAD)) || (xlib = dlopen("libX11-xcb.so.1", WSI_RTLD_NOW)))
                *(void **)&xcb.xlib_connection = dlsym(xlib, "XGetXCBConnection");
        return xcb.ready = xcb.query_tree && xcb.query_tree_reply && xcb.get_geometry && xcb.get_geometry_reply;
}

static int wsi_x_geometry(void *connection, unsigned int window, int *x, int *y, int *width, int *height)
{
        void *error = 0;
        struct wsi_xcb_geometry *reply = xcb.get_geometry_reply(connection, xcb.get_geometry(connection, window), &error);

        free(error);
        if (!reply)
                return 0;
        *x = reply->x, *y = reply->y, *width = reply->width, *height = reply->height;
        free(reply);
        return 1;
}

// The window's toplevel, the child of the root it is under (Canvas's X window manager reparents none), when the window is all of it:
// 0 when the window is a part of a bigger toplevel, or the tree cannot be read.
static unsigned int wsi_x_top(void *connection, unsigned int window)
{
        unsigned int at = window;
        int x, y, width, height, tx, ty, top_width, top_height;

        if (!wsi_x_geometry(connection, window, &x, &y, &width, &height))
                return 0;
        for (int hops = 0; hops < 32; hops++)
        {
                void *error = 0;
                struct wsi_xcb_tree *reply = xcb.query_tree_reply(connection, xcb.query_tree(connection, at), &error);
                unsigned int parent, root;

                free(error);
                if (!reply)
                        return 0;
                parent = reply->parent, root = reply->root;
                free(reply);
                if (!parent || parent == root)
                        break;
                at = parent;
        }
        if (at == window)
                return window;
        if (!wsi_x_geometry(connection, at, &tx, &ty, &top_width, &top_height))
                return 0;
        // a child at the toplevel's corner and of its size (winex11's client window of a window with no frame of its own)
        return x == 0 && y == 0 && width == top_width && height == top_height ? at : 0;
}

/* ---- the stand-in ---- */

// Asks Canvas for the surface's stand-in when it has none or its answer was a wait (under the lock): the answer, WSI_UNKNOWN when it
// cannot be asked. An X window not known yet (its window manager reads its creation a turn after the game made it) is asked again a few
// times, a few milliseconds apart.
static unsigned int wsi_stand(struct wsi_surface *s)
{
        struct wsi_link *link;

        if (s->refused || (s->stand && s->answer == WSI_SHOWN))
                return s->refused ? WSI_UNKNOWN : s->answer;
        if (s->kind == WSI_X11)
        {
                if (!s->top && wsi_xcb())
                        s->top = wsi_x_top(s->xcb, s->window);
                link = s->top ? wsi_own() : 0;
        }
        else
                link = wsi_link_of(s->wl_display);
        if (!link || !link->manager || !link->compositor)
                return s->refused = 1, WSI_UNKNOWN;
        s->link = link;
        if (!s->stand && !(s->stand = wl.proxy_marshal_constructor(link->compositor, WSI_COMPOSITOR_CREATE_SURFACE, wl.surface_interface, (void *)0)))
                return s->refused = 1, WSI_UNKNOWN;
        for (int tries = 0; tries < (s->kind == WSI_X11 ? 8 : 1); tries++)
        {
                struct wsi_timespec pause = {0, 4000000};

                s->answer = WSI_UNASKED;
                if (s->kind == WSI_X11)
                        wl.proxy_marshal(link->manager, WSI_MANAGER_OVERRIDE_WINDOW, s->stand, s->top);
                else
                        wl.proxy_marshal(link->manager, WSI_MANAGER_OVERRIDE_SURFACE, s->stand, s->wl_surface);
                if (wl.display_roundtrip_queue(link->display, link->queue) < 0 || s->answer != WSI_UNKNOWN)
                        break;
                s->refused = 0;
                clock_nanosleep(WSI_CLOCK_MONOTONIC, 0, &pause, 0);
        }
        if (s->answer == WSI_UNASKED)
                s->answer = WSI_UNKNOWN, s->refused = 1;
        return s->answer;
}

// The driver's surface of the stand-in, made once.
static VkSurfaceKHR wsi_stand_surface(struct wsi_surface *s)
{
        struct VkWaylandSurfaceCreateInfoKHR info = {VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR, 0, 0, 0, 0};

        if (s->stand_surface || !s->stand || !s->instance->create_wayland)
                return s->stand_surface;
        info.display = s->link->display, info.surface = s->stand;
        if (s->instance->create_wayland(s->instance->instance, &info, 0, &s->stand_surface) != VK_SUCCESS)
                s->stand_surface = 0, s->refused = 1;
        return s->stand_surface;
}

static void wsi_stand_drop(struct wsi_surface *s)
{
        if (s->stand_surface && s->instance->destroy_surface)
                s->instance->destroy_surface(s->instance->instance, s->stand_surface, 0);
        if (s->stand)
        {
                wl.proxy_marshal(s->stand, WSI_SURFACE_DESTROY);
                wl.proxy_destroy(s->stand);
                if (s->link)
                        wl.display_flush(s->link->display);
        }
        s->stand = 0, s->stand_surface = 0;
}

/* ---- the instance ---- */

static PFN_vkVoidFunction wsi_instance_proc(VkInstance instance, const char *name);
static PFN_vkVoidFunction wsi_device_proc(VkDevice device, const char *name);

static VkResult wsi_create_instance(const struct VkInstanceCreateInfo *info, const void *allocator, VkInstance *made)
{
        struct VkLayerInstanceCreateInfo *chain = (struct VkLayerInstanceCreateInfo *)info->pNext;
        struct VkInstanceCreateInfo asked = *info;
        PFN_vkGetInstanceProcAddr next;
        PFN_vkCreateInstance create;
        struct wsi_instance *row;
        const char **names = 0;
        int x11 = 0, wayland = 0;
        VkResult result;

        while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO))
                chain = (struct VkLayerInstanceCreateInfo *)chain->pNext;
        if (!chain || !chain->u.pLayerInfo)
                return VK_ERROR_INITIALIZATION_FAILED;
        next = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
        if (!(create = (PFN_vkCreateInstance)next(0, "vkCreateInstance")))
                return VK_ERROR_INITIALIZATION_FAILED;
        wsi_lock();
        if (!wsi.looked)
                wsi_look();
        wsi_unlock();
        for (unsigned int at = 0; at < info->enabledExtensionCount; at++)
        {
                x11 |= wsi_same(info->ppEnabledExtensionNames[at], "VK_KHR_xlib_surface") || wsi_same(info->ppEnabledExtensionNames[at], "VK_KHR_xcb_surface");
                wayland |= wsi_same(info->ppEnabledExtensionNames[at], "VK_KHR_wayland_surface");
        }
        // An X surface's stand-in is a Wayland surface of the same instance: the extension is asked for beside the game's.
        if (!wsi.off && x11 && !wayland && (names = (const char **)malloc((info->enabledExtensionCount + 1) * sizeof(char *))))
        {
                for (unsigned int at = 0; at < info->enabledExtensionCount; at++)
                        names[at] = info->ppEnabledExtensionNames[at];
                names[info->enabledExtensionCount] = "VK_KHR_wayland_surface";
                asked.ppEnabledExtensionNames = names, asked.enabledExtensionCount = info->enabledExtensionCount + 1, wayland = 1;
        }
        result = create(&asked, allocator, made);
        if (result == VK_ERROR_EXTENSION_NOT_PRESENT && names)
                result = create(info, allocator, made), wayland = 0;
        free(names);
        if (result != VK_SUCCESS)
                return result;
        if (!(row = (struct wsi_instance *)calloc(1, sizeof *row)))
                return result;
        row->key = wsi_key(*made), row->instance = *made, row->next = next, row->active = !wsi.off && wayland && (x11 || wayland);
#define WSI_GET(field, type, symbol) row->field = (type)next(*made, symbol)
        WSI_GET(destroy, PFN_vkDestroyInstance, "vkDestroyInstance");
        WSI_GET(create_device, PFN_vkCreateDevice, "vkCreateDevice");
        WSI_GET(create_xlib, PFN_vkCreateXlibSurfaceKHR, "vkCreateXlibSurfaceKHR");
        WSI_GET(create_xcb, PFN_vkCreateXcbSurfaceKHR, "vkCreateXcbSurfaceKHR");
        WSI_GET(create_wayland, PFN_vkCreateWaylandSurfaceKHR, "vkCreateWaylandSurfaceKHR");
        WSI_GET(destroy_surface, PFN_vkDestroySurfaceKHR, "vkDestroySurfaceKHR");
        WSI_GET(caps, PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        WSI_GET(caps2, PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR, "vkGetPhysicalDeviceSurfaceCapabilities2KHR");
        WSI_GET(modes, PFN_vkGetPhysicalDeviceSurfacePresentModesKHR, "vkGetPhysicalDeviceSurfacePresentModesKHR");
        WSI_GET(formats, PFN_vkGetPhysicalDeviceSurfaceFormatsKHR, "vkGetPhysicalDeviceSurfaceFormatsKHR");
        WSI_GET(support, PFN_vkGetPhysicalDeviceSurfaceSupportKHR, "vkGetPhysicalDeviceSurfaceSupportKHR");
#undef WSI_GET
        row->active &= row->create_wayland && row->destroy_surface && row->caps && row->modes && row->formats;
        wsi_lock();
        if (!wsi_append((void ***)&wsi.instances, &wsi.instance_count, &wsi.instance_room, row))
                free(row);
        wsi_unlock();
        return result;
}

static void wsi_destroy_instance(VkInstance instance, const void *allocator)
{
        struct wsi_instance *row = wsi_instance_of(instance);
        PFN_vkDestroyInstance destroy = row ? row->destroy : 0;

        if (!row)
                return;
        wsi_lock();
        for (unsigned int at = wsi.surface_count; at-- > 0;)
                if (wsi.surfaces[at]->instance == row)
                {
                        struct wsi_surface *s = wsi.surfaces[at];

                        wsi_remove((void **)wsi.surfaces, &wsi.surface_count, s);
                        wsi_stand_drop(s), free(s);
                }
        wsi_remove((void **)wsi.instances, &wsi.instance_count, row);
        wsi_unlock();
        free(row);
        if (destroy)
                destroy(instance, allocator);
}

/* ---- surfaces: the game's, as it made them, kept with what a stand-in needs ---- */

static void wsi_surface_keep(struct wsi_instance *row, VkSurfaceKHR surface, int kind, void *xcb_connection, unsigned int window, void *display, void *wl_surface)
{
        struct wsi_surface *s;

        if (!row->active || !(s = (struct wsi_surface *)calloc(1, sizeof *s)))
                return;
        s->surface = surface, s->instance = row, s->kind = kind, s->xcb = xcb_connection, s->window = window;
        s->wl_display = display, s->wl_surface = wl_surface, s->answer = WSI_UNASKED;
        wsi_lock();
        if (!wsi_append((void ***)&wsi.surfaces, &wsi.surface_count, &wsi.surface_room, s))
                free(s);
        wsi_unlock();
}

static VkResult wsi_create_xlib(VkInstance instance, const struct VkXlibSurfaceCreateInfoKHR *info, const void *allocator, VkSurfaceKHR *made)
{
        struct wsi_instance *row = wsi_instance_of(instance);
        VkResult result = row && row->create_xlib ? row->create_xlib(instance, info, allocator, made) : VK_ERROR_INITIALIZATION_FAILED;

        if (result == VK_SUCCESS && wsi_xcb() && xcb.xlib_connection)
                wsi_surface_keep(row, *made, WSI_X11, xcb.xlib_connection(info->dpy), (unsigned int)info->window, 0, 0);
        return result;
}

static VkResult wsi_create_xcb(VkInstance instance, const struct VkXcbSurfaceCreateInfoKHR *info, const void *allocator, VkSurfaceKHR *made)
{
        struct wsi_instance *row = wsi_instance_of(instance);
        VkResult result = row && row->create_xcb ? row->create_xcb(instance, info, allocator, made) : VK_ERROR_INITIALIZATION_FAILED;

        if (result == VK_SUCCESS)
                wsi_surface_keep(row, *made, WSI_X11, info->connection, info->window, 0, 0);
        return result;
}

static VkResult wsi_create_wayland(VkInstance instance, const struct VkWaylandSurfaceCreateInfoKHR *info, const void *allocator, VkSurfaceKHR *made)
{
        struct wsi_instance *row = wsi_instance_of(instance);
        VkResult result = row && row->create_wayland ? row->create_wayland(instance, info, allocator, made) : VK_ERROR_INITIALIZATION_FAILED;

        if (result == VK_SUCCESS)
                wsi_surface_keep(row, *made, WSI_WAYLAND, 0, 0, info->display, info->surface);
        return result;
}

static void wsi_destroy_surface(VkInstance instance, VkSurfaceKHR surface, const void *allocator)
{
        struct wsi_instance *row = wsi_instance_of(instance);
        struct wsi_surface *s = surface ? wsi_surface_of(surface) : 0;

        if (s)
        {
                wsi_lock();
                wsi_remove((void **)wsi.surfaces, &wsi.surface_count, s);
                wsi_stand_drop(s);
                wsi_unlock();
                free(s);
        }
        if (row && row->destroy_surface)
                row->destroy_surface(instance, surface, allocator);
}

// The render size an X window's surface is told (MOONWATER_WSI_SIZE, MOONWATER_WSI_SCALE): its own when neither is asked for.
static void wsi_render_size(unsigned int *width, unsigned int *height)
{
        if (wsi.size_w && wsi.size_h && (unsigned long)wsi.size_w <= *width && (unsigned long)wsi.size_h <= *height)
                *width = (unsigned int)wsi.size_w, *height = (unsigned int)wsi.size_h;
        else if (wsi.scale < 100)
                *width = (unsigned int)wsi_divide((unsigned long long)*width * (unsigned long long)wsi.scale + 50, 100),
                *height = (unsigned int)wsi_divide((unsigned long long)*height * (unsigned long long)wsi.scale + 50, 100);
        *width = *width ? *width : 1, *height = *height ? *height : 1;
}

// An X window's surface's capabilities with its stand-in: the render size its current extent (and its only one), and opaque its
// composite alpha, as an X window of depth 24 is.
static void wsi_caps_fix(struct wsi_surface *s, struct VkSurfaceCapabilitiesKHR *caps)
{
        unsigned int width = caps->currentExtent.width, height = caps->currentExtent.height;

        if (!s || s->kind != WSI_X11 || width == 0xffffffffu || (wsi.scale >= 100 && !wsi.size_w))
                return;
        wsi_lock();
        if (wsi_stand(s) > WSI_WAITING)
                width = 0;
        wsi_unlock();
        if (!width)
                return;
        wsi_render_size(&width, &height);
        caps->currentExtent.width = caps->minImageExtent.width = caps->maxImageExtent.width = width;
        caps->currentExtent.height = caps->minImageExtent.height = caps->maxImageExtent.height = height;
}

static VkResult wsi_caps(VkPhysicalDevice physical, VkSurfaceKHR surface, struct VkSurfaceCapabilitiesKHR *caps)
{
        struct wsi_instance *row = wsi_instance_of(physical);
        VkResult result = row && row->caps ? row->caps(physical, surface, caps) : VK_ERROR_INITIALIZATION_FAILED;

        if (result == VK_SUCCESS)
                wsi_caps_fix(wsi_surface_of(surface), caps);
        return result;
}

static VkResult wsi_caps2(VkPhysicalDevice physical, const struct VkPhysicalDeviceSurfaceInfo2KHR *info, struct VkSurfaceCapabilities2KHR *caps)
{
        struct wsi_instance *row = wsi_instance_of(physical);
        VkResult result = row && row->caps2 ? row->caps2(physical, info, caps) : VK_ERROR_INITIALIZATION_FAILED;

        if (result == VK_SUCCESS)
                wsi_caps_fix(wsi_surface_of(info->surface), &caps->surfaceCapabilities);
        return result;
}

/* ---- the device ---- */

static VkResult wsi_create_device(VkPhysicalDevice physical, const struct VkBase *info, const void *allocator, VkDevice *made)
{
        struct VkLayerDeviceCreateInfo *chain = (struct VkLayerDeviceCreateInfo *)info->pNext;
        struct wsi_instance *instance = wsi_instance_of(physical);
        PFN_vkGetInstanceProcAddr next_instance;
        PFN_vkGetDeviceProcAddr next;
        PFN_vkCreateDevice create;
        struct wsi_device *row;
        VkResult result;

        while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO))
                chain = (struct VkLayerDeviceCreateInfo *)chain->pNext;
        if (!chain || !chain->u.pLayerInfo || !instance)
                return VK_ERROR_INITIALIZATION_FAILED;
        next_instance = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr, next = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
        chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
        if (!(create = (PFN_vkCreateDevice)next_instance(instance->instance, "vkCreateDevice")))
                return VK_ERROR_INITIALIZATION_FAILED;
        if ((result = create(physical, info, allocator, made)) != VK_SUCCESS || !(row = (struct wsi_device *)calloc(1, sizeof *row)))
                return result;
        row->key = wsi_key(*made), row->device = *made, row->physical = physical, row->instance = instance, row->next = next;
#define WSI_GET(field, type, symbol) row->field = (type)next(*made, symbol)
        WSI_GET(destroy, PFN_vkDestroyDevice, "vkDestroyDevice");
        WSI_GET(create_swapchain, PFN_vkCreateSwapchainKHR, "vkCreateSwapchainKHR");
        WSI_GET(destroy_swapchain, PFN_vkDestroySwapchainKHR, "vkDestroySwapchainKHR");
        WSI_GET(acquire, PFN_vkAcquireNextImageKHR, "vkAcquireNextImageKHR");
        WSI_GET(acquire2, PFN_vkAcquireNextImage2KHR, "vkAcquireNextImage2KHR");
        WSI_GET(present, PFN_vkQueuePresentKHR, "vkQueuePresentKHR");
#undef WSI_GET
        wsi_lock();
        if (!wsi_append((void ***)&wsi.devices, &wsi.device_count, &wsi.device_room, row))
                free(row);
        wsi_unlock();
        return result;
}

static void wsi_swapchain_drop(struct wsi_swapchain *chain);

static void wsi_destroy_device(VkDevice device, const void *allocator)
{
        struct wsi_device *row = wsi_device_of(device);
        PFN_vkDestroyDevice destroy = row ? row->destroy : 0;

        if (!row)
                return;
        wsi_lock();
        for (unsigned int at = wsi.swapchain_count; at-- > 0;)
                if (wsi.swapchains[at]->device == row)
                        wsi_swapchain_drop(wsi.swapchains[at]);
        wsi_remove((void **)wsi.devices, &wsi.device_count, row);
        wsi_unlock();
        free(row);
        if (destroy)
                destroy(device, allocator);
}

/* ---- swapchains: on the stand-in when Canvas shows it, else where the game asked ---- */

static int wsi_mode_has(struct wsi_instance *instance, VkPhysicalDevice physical, VkSurfaceKHR surface, unsigned int mode)
{
        unsigned int modes[16], count = 16;

        if (instance->modes(physical, surface, &count, modes) < 0)
                return 0;
        for (unsigned int at = 0; at < count && at < 16; at++)
                if (modes[at] == mode)
                        return 1;
        return 0;
}

static int wsi_format_has(struct wsi_instance *instance, VkPhysicalDevice physical, VkSurfaceKHR surface, int format, int space)
{
        unsigned int count = 0;
        struct wsi_formats { struct VkSurfaceFormatKHR format[64]; } *list;
        int found = 0;

        if (instance->formats(physical, surface, &count, 0) < 0 || !count || !(list = (struct wsi_formats *)malloc(sizeof *list)))
                return 0;
        count = count > 64 ? 64 : count;
        if (instance->formats(physical, surface, &count, list->format) >= 0)
                for (unsigned int at = 0; at < count && !found; at++)
                        found = list->format[at].format == format && list->format[at].colorSpace == space;
        free(list);
        return found;
}

// The swapchain's info for the stand-in's surface, or 0 when the stand-in cannot take what the game asked (a format or a usage it
// does not have): opaque, a present mode it has, an image count it allows.
static int wsi_stand_info(struct wsi_device *device, struct wsi_surface *s, VkSurfaceKHR stand, struct VkSwapchainCreateInfoKHR *info)
{
        struct wsi_instance *instance = device->instance;
        struct VkSurfaceCapabilitiesKHR caps;

        if (instance->caps(device->physical, stand, &caps) != VK_SUCCESS || (info->imageUsage & ~caps.supportedUsageFlags) ||
            !wsi_format_has(instance, device->physical, stand, info->imageFormat, info->imageColorSpace))
                return 0;
        info->surface = stand, info->compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, info->preTransform = 1;
        if (!wsi_mode_has(instance, device->physical, stand, info->presentMode))
                info->presentMode = info->presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR && wsi_mode_has(instance, device->physical, stand, VK_PRESENT_MODE_MAILBOX_KHR)
                                        ? VK_PRESENT_MODE_MAILBOX_KHR
                                        : VK_PRESENT_MODE_FIFO_KHR;
        if (info->minImageCount < caps.minImageCount)
                info->minImageCount = caps.minImageCount;
        if (caps.maxImageCount && info->minImageCount > caps.maxImageCount)
                info->minImageCount = caps.maxImageCount;
        (void)s;
        return 1;
}

static VkResult wsi_create_swapchain(VkDevice device, const struct VkSwapchainCreateInfoKHR *info, const void *allocator, VkSwapchainKHR *made)
{
        struct wsi_device *row = wsi_device_of(device);
        struct wsi_surface *s = wsi_surface_of(info->surface);
        struct wsi_swapchain *old = info->oldSwapchain ? wsi_swapchain_of(info->oldSwapchain) : 0, *chain;
        struct VkSwapchainCreateInfoKHR asked = *info;
        VkSurfaceKHR stand = 0;
        unsigned int answer = WSI_UNKNOWN;
        VkResult result;
        int stood = 0;

        if (!row || !row->create_swapchain)
                return VK_ERROR_INITIALIZATION_FAILED;
        if (s && row->instance->active)
        {
                wsi_lock();
                answer = wsi_stand(s);
                if (answer <= WSI_WAITING)
                        stand = wsi_stand_surface(s);
                wsi_unlock();
                stood = stand && wsi_stand_info(row, s, stand, &asked);
                if (!stood)
                        asked = *info;
        }
        // An old swapchain of the other surface is no old one of this: the game destroys it itself.
        if (old && (old->stood != stood || (!old->stood && old->surface != s)))
                asked.oldSwapchain = 0;
        result = row->create_swapchain(device, &asked, allocator, made);
        if (result != VK_SUCCESS && stood)
                stood = 0, asked = *info, asked.oldSwapchain = old && old->stood ? 0 : info->oldSwapchain,
                result = row->create_swapchain(device, &asked, allocator, made);
        if (result != VK_SUCCESS || !s || !(chain = (struct wsi_swapchain *)calloc(1, sizeof *chain)))
                return result;
        chain->swapchain = *made, chain->device = row, chain->surface = s, chain->stood = stood, chain->mode = asked.presentMode;
        chain->width = asked.imageExtent.width, chain->height = asked.imageExtent.height;
        chain->pace = wsi.pace && (asked.presentMode == VK_PRESENT_MODE_FIFO_KHR || asked.presentMode == VK_PRESENT_MODE_FIFO_RELAXED_KHR);
        chain->refresh = 6944444ull;
        wsi_lock();
        // The swapchain's own queue, and the surface its frame callbacks and feedback are asked through: the stand-in's, or a Wayland
        // surface of the game's own (where pacing and feedback come through a wrapper on that queue).
        if (stood || s->kind == WSI_WAYLAND)
        {
                struct wsi_link *link = s->link ? s->link : wsi_link_of(s->wl_display);
                void *surface = stood ? (void *)s->stand : s->wl_surface;

                if (link && link->manager && (chain->queue = wl.display_create_queue(link->display)) && (chain->target = wl.proxy_create_wrapper(surface)))
                {
                        wl.proxy_set_queue((struct wl_proxy *)chain->target, chain->queue), s->link = link;
                        if (link->presentation && (chain->presentation = wl.proxy_create_wrapper(link->presentation)))
                                wl.proxy_set_queue((struct wl_proxy *)chain->presentation, chain->queue);
                }
        }
        if (!wsi_append((void ***)&wsi.swapchains, &wsi.swapchain_count, &wsi.swapchain_room, chain))
                free(chain), chain = 0;
        if (chain)
                wsi_note('C', wsi_now(), chain->width, chain->height, (long)chain->mode, (long)stood + 2 * (long)answer);
        wsi_unlock();
        return result;
}

static void wsi_frame_done(void *data, struct wl_proxy *callback, unsigned int time);
static void (*const wsi_callback_listener[])(void) = {(void (*)(void))wsi_frame_done};

static void wsi_feedback_output(void *data, struct wl_proxy *feedback, struct wl_proxy *output) { (void)data, (void)feedback, (void)output; }
static void wsi_feedback_presented(void *data, struct wl_proxy *proxy, unsigned int sec_hi, unsigned int sec_lo, unsigned int nsec,
                                   unsigned int refresh, unsigned int seq_hi, unsigned int seq_lo, unsigned int flags);
static void wsi_feedback_discarded(void *data, struct wl_proxy *proxy);
static void (*const wsi_feedback_listener[])(void) = {(void (*)(void))wsi_feedback_output, (void (*)(void))wsi_feedback_presented,
                                                      (void (*)(void))wsi_feedback_discarded};

static void wsi_feedback_end(struct wsi_feedback *f)
{
        struct wsi_feedback **at = &f->swapchain->feedback;

        while (*at && *at != f)
                at = &(*at)->next;
        if (*at)
                *at = f->next;
        wl.proxy_destroy(f->proxy);
        free(f);
}

static void wsi_frame_done(void *data, struct wl_proxy *callback, unsigned int time)
{
        struct wsi_swapchain *chain = (struct wsi_swapchain *)data;

        (void)time;
        if (chain->callback == callback)
                chain->callback = 0;
        wl.proxy_destroy(callback);
}

static void wsi_feedback_presented(void *data, struct wl_proxy *proxy, unsigned int sec_hi, unsigned int sec_lo, unsigned int nsec,
                                   unsigned int refresh, unsigned int seq_hi, unsigned int seq_lo, unsigned int flags)
{
        struct wsi_feedback *f = (struct wsi_feedback *)data;
        unsigned long long at = (((unsigned long long)sec_hi << 32) | sec_lo) * 1000000000ull + nsec;

        (void)proxy, (void)seq_hi, (void)seq_lo;
        if (refresh)
                f->swapchain->refresh = refresh;
        wsi_lock(), wsi_note('P', f->at, at, refresh, (long)flags, (long)f->swapchain->stood), wsi_unlock();
        wsi_feedback_end(f);
}

static void wsi_feedback_discarded(void *data, struct wl_proxy *proxy)
{
        struct wsi_feedback *f = (struct wsi_feedback *)data;

        (void)proxy;
        wsi_lock(), wsi_note('D', f->at, 0, 0, 0, (long)f->swapchain->stood), wsi_unlock();
        wsi_feedback_end(f);
}

// The swapchain's queue's events, waiting at most until `until` (CLOCK_MONOTONIC ns; 0 for no wait) for its frame callback.
static void wsi_chain_pump(struct wsi_swapchain *chain, unsigned long long until)
{
        struct wl_display *display = chain->surface->link->display;

        for (;;)
        {
                struct wsi_pollfd watch;
                unsigned long long now = wsi_now();
                int wait = until > now && chain->callback ? (int)((until - now) >> 20) + 1 : 0;

                while (wl.display_prepare_read_queue(display, chain->queue) != 0)
                        wl.display_dispatch_queue_pending(display, chain->queue);
                wl.display_flush(display);
                watch.fd = wl.display_get_fd(display), watch.events = 1, watch.revents = 0;
                if (poll(&watch, 1, wait) > 0)
                        wl.display_read_events(display);
                else
                        wl.display_cancel_read(display);
                wl.display_dispatch_queue_pending(display, chain->queue);
                if (!chain->callback || !wait || wsi_now() >= until)
                        return;
        }
}

static void wsi_swapchain_drop(struct wsi_swapchain *chain)
{
        wsi_remove((void **)wsi.swapchains, &wsi.swapchain_count, chain);
        while (chain->feedback)
                wsi_feedback_end(chain->feedback);
        if (chain->callback)
                wl.proxy_destroy(chain->callback);
        if (chain->target)
                wl.proxy_wrapper_destroy(chain->target);
        if (chain->presentation)
                wl.proxy_wrapper_destroy(chain->presentation);
        free(chain);
}

static void wsi_destroy_swapchain(VkDevice device, VkSwapchainKHR swapchain, const void *allocator)
{
        struct wsi_device *row = wsi_device_of(device);
        struct wsi_swapchain *chain = swapchain ? wsi_swapchain_of(swapchain) : 0;

        if (row && row->destroy_swapchain)
                row->destroy_swapchain(device, swapchain, allocator);
        if (chain)
                wsi_lock(), wsi_swapchain_drop(chain), wsi_unlock();
}

// Before the game's next image: its last frame's callback waited for (pacing), the frame cap slept to, and a swapchain Canvas no
// longer shows the stand-in of made out of date.
static VkResult wsi_before_acquire(VkSwapchainKHR swapchain)
{
        struct wsi_swapchain *chain = wsi_swapchain_of(swapchain);

        if (!chain)
                return VK_SUCCESS;
        if (chain->target)
                wsi_chain_pump(chain, chain->pace && chain->callback ? wsi_now() + 2 * chain->refresh + 1000000ull : 0);
        if (wsi.fps > 0)
        {
                unsigned long long now = wsi_now(), step = wsi_divide(1000000000ull, (unsigned int)wsi.fps);

                if (chain->cap_next > now && chain->cap_next - now <= step)
                {
                        struct wsi_timespec at = {(long)wsi_divide(chain->cap_next, 1000000000u), 0};

                        at.nanoseconds = (long)(chain->cap_next - (unsigned long long)at.seconds * 1000000000ull);
                        clock_nanosleep(WSI_CLOCK_MONOTONIC, WSI_TIMER_ABSTIME, &at, 0);
                        chain->cap_next += step;
                }
                else
                        chain->cap_next = now + step;
        }
        if (chain->stood && chain->surface->answer == WSI_WAITING && !chain->stale)
                wsi_lock(), wsi_link_pump(chain->surface->link), wsi_unlock();
        return chain->stale ? VK_ERROR_OUT_OF_DATE_KHR : VK_SUCCESS;
}

static VkResult wsi_acquire(VkDevice device, VkSwapchainKHR swapchain, unsigned long long timeout, VkSemaphore semaphore, VkFence fence, unsigned int *index)
{
        struct wsi_device *row = wsi_device_of(device);
        VkResult result = wsi_before_acquire(swapchain);

        if (result != VK_SUCCESS)
                return result;
        return row && row->acquire ? row->acquire(device, swapchain, timeout, semaphore, fence, index) : VK_ERROR_INITIALIZATION_FAILED;
}

static VkResult wsi_acquire2(VkDevice device, const struct VkAcquireNextImageInfoKHR *info, unsigned int *index)
{
        struct wsi_device *row = wsi_device_of(device);
        VkResult result = wsi_before_acquire(info->swapchain);

        if (result != VK_SUCCESS)
                return result;
        return row && row->acquire2 ? row->acquire2(device, info, index) : VK_ERROR_INITIALIZATION_FAILED;
}

// A present: each swapchain's next commit asked for its frame callback (pacing) and its feedback (the log), before the driver commits it.
static VkResult wsi_present(VkQueue queue, const struct VkPresentInfoKHR *info)
{
        struct wsi_device *row = wsi_device_of(queue);
        unsigned long long now = wsi_now();
        VkResult result;
        int stale = 0;

        if (!row || !row->present)
                return VK_ERROR_INITIALIZATION_FAILED;
        for (unsigned int at = 0; at < info->swapchainCount; at++)
        {
                struct wsi_swapchain *chain = wsi_swapchain_of(info->pSwapchains[at]);
                struct wsi_feedback *f;

                if (!chain || !chain->target)
                        continue;
                stale |= chain->stale;
                if (chain->pace && !chain->callback &&
                    (chain->callback = wl.proxy_marshal_constructor((struct wl_proxy *)chain->target, WSI_SURFACE_FRAME, wl.callback_interface, (void *)0)))
                        wl.proxy_add_listener(chain->callback, (void (**)(void))wsi_callback_listener, chain);
                // The feedback carries the refresh the pacing waits by, and the log's vblanks; at most a few wait at once.
                if (chain->presentation && (!chain->feedback || !chain->feedback->next || !chain->feedback->next->next) &&
                    (f = (struct wsi_feedback *)calloc(1, sizeof *f)))
                {
                        f->proxy = wl.proxy_marshal_constructor((struct wl_proxy *)chain->presentation, WSI_PRESENTATION_FEEDBACK, &wsi_feedback_interface,
                                                                 chain->target, (void *)0);
                        if (f->proxy)
                        {
                                f->swapchain = chain, f->at = now, f->next = chain->feedback, chain->feedback = f;
                                wl.proxy_add_listener(f->proxy, (void (**)(void))wsi_feedback_listener, f);
                        }
                        else
                                free(f);
                }
        }
        result = row->present(queue, info);
        return result == VK_SUCCESS && stale ? VK_SUBOPTIMAL_KHR : result;
}

/* ---- the loader's way in ---- */

static PFN_vkVoidFunction wsi_device_hook(const char *name)
{
        static const struct { const char *name; PFN_vkVoidFunction function; } hooks[] = {
                {"vkGetDeviceProcAddr", (PFN_vkVoidFunction)wsi_device_proc},
                {"vkDestroyDevice", (PFN_vkVoidFunction)wsi_destroy_device},
                {"vkCreateSwapchainKHR", (PFN_vkVoidFunction)wsi_create_swapchain},
                {"vkDestroySwapchainKHR", (PFN_vkVoidFunction)wsi_destroy_swapchain},
                {"vkAcquireNextImageKHR", (PFN_vkVoidFunction)wsi_acquire},
                {"vkAcquireNextImage2KHR", (PFN_vkVoidFunction)wsi_acquire2},
                {"vkQueuePresentKHR", (PFN_vkVoidFunction)wsi_present},
        };

        for (unsigned int at = 0; at < sizeof hooks / sizeof hooks[0]; at++)
                if (wsi_same(name, hooks[at].name))
                        return hooks[at].function;
        return 0;
}

static PFN_vkVoidFunction wsi_instance_hook(const char *name)
{
        static const struct { const char *name; PFN_vkVoidFunction function; } hooks[] = {
                {"vkGetInstanceProcAddr", (PFN_vkVoidFunction)wsi_instance_proc},
                {"vkCreateInstance", (PFN_vkVoidFunction)wsi_create_instance},
                {"vkDestroyInstance", (PFN_vkVoidFunction)wsi_destroy_instance},
                {"vkCreateDevice", (PFN_vkVoidFunction)wsi_create_device},
                {"vkCreateXlibSurfaceKHR", (PFN_vkVoidFunction)wsi_create_xlib},
                {"vkCreateXcbSurfaceKHR", (PFN_vkVoidFunction)wsi_create_xcb},
                {"vkCreateWaylandSurfaceKHR", (PFN_vkVoidFunction)wsi_create_wayland},
                {"vkDestroySurfaceKHR", (PFN_vkVoidFunction)wsi_destroy_surface},
                {"vkGetPhysicalDeviceSurfaceCapabilitiesKHR", (PFN_vkVoidFunction)wsi_caps},
                {"vkGetPhysicalDeviceSurfaceCapabilities2KHR", (PFN_vkVoidFunction)wsi_caps2},
        };

        for (unsigned int at = 0; at < sizeof hooks / sizeof hooks[0]; at++)
                if (wsi_same(name, hooks[at].name))
                        return hooks[at].function;
        return 0;
}

static PFN_vkVoidFunction wsi_instance_proc(VkInstance instance, const char *name)
{
        struct wsi_instance *row;
        PFN_vkVoidFunction hook = wsi_instance_hook(name), next;

        if (!instance)
                return wsi_same(name, "vkCreateInstance") || wsi_same(name, "vkGetInstanceProcAddr") ? hook : 0;
        if (!(row = wsi_instance_of(instance)))
                return hook;
        next = row->next(instance, name);
        if (!next)
                return 0;
        if (hook && (row->active || wsi_same(name, "vkGetInstanceProcAddr") || wsi_same(name, "vkDestroyInstance") || wsi_same(name, "vkCreateDevice")))
                return hook;
        if (row->active && (hook = wsi_device_hook(name)))
                return hook;
        return next;
}

static PFN_vkVoidFunction wsi_device_proc(VkDevice device, const char *name)
{
        struct wsi_device *row = wsi_device_of(device);
        PFN_vkVoidFunction hook = wsi_device_hook(name), next;

        if (!row)
                return 0;
        next = row->next(device, name);
        if (!next)
                return 0;
        if (hook && (row->instance->active || wsi_same(name, "vkGetDeviceProcAddr") || wsi_same(name, "vkDestroyDevice")))
                return hook;
        return next;
}

WSI_EXPORT VkResult vkNegotiateLoaderLayerInterfaceVersion(struct VkNegotiateLayerInterface *interface)
{
        if (!interface || interface->loaderLayerInterfaceVersion < 2)
                return VK_ERROR_INITIALIZATION_FAILED;
        interface->loaderLayerInterfaceVersion = 2;
        interface->pfnGetInstanceProcAddr = wsi_instance_proc;
        interface->pfnGetDeviceProcAddr = wsi_device_proc;
        interface->pfnGetPhysicalDeviceProcAddr = 0;
        return VK_SUCCESS;
}
