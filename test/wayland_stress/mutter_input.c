/*
        Input for a headless Mutter, as Moonwater headless takes it: evdev records (struct input_event) read from a FIFO and
        given to Mutter's RemoteDesktop session on the session bus (org.gnome.Mutter.RemoteDesktop), so the stress bench's
        input rows (the pointer lock, the clipboard, drag and drop, the input edge cases) run on Mutter as a reference. A
        relative motion is sent at its SYN_REPORT, a button (BTN_* codes) as a pointer button and any other key as a keycode.
        The session lives while this does: Mutter ends a RemoteDesktop session when the connection that made it goes.

            mutter-input FIFO
*/
#include <fcntl.h>
#include <gio/gio.h>
#include <linux/input.h>
#include <stdio.h>
#include <unistd.h>

static GDBusConnection *bus;
static const char *session;

static void call(const char *method, GVariant *args)
{
        GError *error = NULL;
        GVariant *r = g_dbus_connection_call_sync(bus, "org.gnome.Mutter.RemoteDesktop", session, "org.gnome.Mutter.RemoteDesktop.Session", method, args,
                                                  NULL, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, &error);
        if (r)
                g_variant_unref(r);
        if (error)
        {
                fprintf(stderr, "mutter-input: %s: %s\n", method, error->message);
                g_error_free(error);
        }
}

int main(int argc, char **argv)
{
        GError *error = NULL;
        if (argc != 2)
        {
                fprintf(stderr, "usage: mutter-input FIFO\n");
                return 2;
        }
        bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
        if (!bus)
        {
                fprintf(stderr, "mutter-input: no session bus: %s\n", error->message);
                return 2;
        }
        // Mutter takes its name on the bus a moment after its socket is there: asked again for up to ten seconds.
        GVariant *r = NULL;
        for (int i = 0; i < 100 && !r; i++)
        {
                if (error)
                        g_clear_error(&error), usleep(100000);
                r = g_dbus_connection_call_sync(bus, "org.gnome.Mutter.RemoteDesktop", "/org/gnome/Mutter/RemoteDesktop", "org.gnome.Mutter.RemoteDesktop",
                                                "CreateSession", NULL, G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
        }
        if (!r)
        {
                fprintf(stderr, "mutter-input: no RemoteDesktop session: %s\n", error->message);
                return 2;
        }
        g_variant_get(r, "(o)", &session);
        call("Start", NULL);
        int fd = open(argv[1], O_RDONLY);
        if (fd < 0)
                return 2;
        usleep(300000); // the session's virtual devices reach the seat
        printf("ready %s\n", session);
        fflush(stdout);
        struct input_event e;
        double dx = 0, dy = 0;
        while (read(fd, &e, sizeof(e)) == (ssize_t)sizeof(e))
        {
                if (e.type == EV_REL && e.code == REL_X)
                        dx += e.value;
                else if (e.type == EV_REL && e.code == REL_Y)
                        dy += e.value;
                else if (e.type == EV_KEY && e.code >= BTN_MISC && e.code < KEY_OK)
                        call("NotifyPointerButton", g_variant_new("(ib)", (int)e.code, e.value != 0));
                else if (e.type == EV_KEY && e.value != 2)
                        call("NotifyKeyboardKeycode", g_variant_new("(ub)", (unsigned)e.code, e.value != 0));
                else if (e.type == EV_SYN && (dx || dy))
                {
                        call("NotifyPointerMotionRelative", g_variant_new("(dd)", dx, dy));
                        dx = dy = 0;
                }
        }
        return 0;
}
