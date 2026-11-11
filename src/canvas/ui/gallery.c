/*
        The kit's gallery, the settings window on -g (moonwater settings gallery): every control of the UI kit on a page of its
        own, with its fields and its scroll, to see them move. It is a window of the settings program, included before the
        settings (shell.c and checks.c), and takes the window and the ui it draws in from the settings; nothing else uses it.
*/

/* ---- the gallery: the kit's showcase (moonwater settings -g) ---- */

/*
        The kit's gallery (moonwater settings gallery, -g): every control of
        ui.c's kit, laid out as Dawning Native's own demo page lays them out in
        Chrome at 1200 pixels (the provision dialog, the preferences, the tabs,
        the build target's radios and checkboxes, the actions and the upgrades'
        progress), and in the grid's empty cell the pieces its demo does not
        show (badges, tags, keys, avatars, spinners, a callout and a code well).
        The page is the theme's paper and not glass, so it is the web page's
        picture to compare, and the wheel scrolls it. Nothing in it runs a
        verb: it is there to be pressed.
*/
#define GALLERY_W 1092
#define GALLERY_H 1538

/*
        The page, one list: every control once, in the order it is made, drawn and hit, in page pixels (the first card's top left
        at 24, 24), several to a line where they are short. A heading's height 0 is its type's line; an x of -1 flows, after the last
        piece on its row (gallery_draw). The list sizes the page (GALLERY_MOST) and is the rows gallery_build makes the controls of.
*/
#define GALLERY_PAGE(X) \
        X(UI_WINDOW, 0, 24, 24, 332, 606, "Provision node") X(UI_CARD, 0, 380, 24, 332, 606, null) X(UI_CARD, 0, 736, 24, 332, 606, null) \
        X(UI_CARD, 0, 24, 654, 332, 507, null) X(UI_CARD, 0, 380, 654, 332, 507, null) X(UI_CARD, 0, 24, 1185, 1044, 329, null) \
        X(UI_RULE, 0, 48, 537, 284, 1, null) X(UI_RULE, 0, 405, 233, 282, 1, null) X(UI_RULE, 0, 49, 927, 282, 1, null) X(UI_RULE, 0, 49, 1420, 994, 1, null) \
        X(UI_HEADING, UI_TITLE_MEDIUM, 405, 49, 282, 0, "System preferences", .role = UI_ON_SURFACE) \
        X(UI_HEADING, UI_TITLE_MEDIUM, 761, 49, 282, 0, "Navigation", .role = UI_ON_SURFACE) \
        X(UI_HEADING, UI_TITLE_MEDIUM, 49, 679, 282, 0, "Build target", .role = UI_ON_SURFACE) \
        X(UI_HEADING, UI_TITLE_MEDIUM, 405, 679, 282, 0, "Actions", .role = UI_ON_SURFACE) \
        X(UI_HEADING, UI_TITLE_MEDIUM, 49, 1210, 994, 0, "System upgrades", .role = UI_ON_SURFACE) \
        X(UI_HEADING, UI_BODY_MEDIUM, 49, 1250, 994, 0, "Firmware DB-9 patching in progress\xe2\x80\xa6", .role = UI_ON_SURFACE_VARIANT) \
        X(UI_HEADING, UI_BODY_MEDIUM, 49, 1338, 994, 0, "Node replication (80%)", .role = UI_ON_SURFACE_VARIANT) \
        X(UI_HEADING, UI_BODY_MEDIUM, 49, 1461, 221, 0, "3 nodes pending a kernel rebuild.", .role = UI_ON_SURFACE_VARIANT) \
        X(UI_INPUT, 0, UI_FIELD_H, 117, 284, 78, "Node name", .items = {"my-kernel"}) \
        X(UI_INPUT, 0, UI_FIELD_H, 203, 284, 78, "Region", .items = {"moon-1"}) \
        X(UI_SLIDER, 0, 48, 289, 284, 88, "vCPU allocation", .low = 1, .high = 64, .step = 1, .value = 8) \
        X(UI_SLIDER, 0, 405, 266, 282, 88, "Log verbosity", .high = 100, .step = 1, .value = 70) \
        X(UI_SLIDER, UI_SLIDER_STEPS, 405, 370, 282, 88, "Replication limit", .high = 100, .step = 25, .value = 50) \
        X(UI_SWITCH, 0, 48, 393, 284, 48, "Pin to bare metal", .on = 1) X(UI_SWITCH, 0, 48, 457, 284, 48, "Auto-deploy on push") \
        X(UI_SWITCH, 0, 405, 89, 282, 48, "Telemetry", .on = 1) X(UI_SWITCH, 0, 405, 153, 282, 48, "Offline cache") \
        X(UI_CHECKBOX, 0, 49, 960, 282, 48, "Enable WASM target", .on = 1) X(UI_CHECKBOX, 0, 49, 1024, 282, 48, "Build as no_std", .on = 1) \
        X(UI_CHECKBOX, 0, 49, 1088, 282, 48, "Strip symbols") \
        X(UI_RADIOS, 0, 49, 719, 282, 48, null, .count = 3, .items = {"x86-64", "ARM64", "RISC-V (experimental)"}) \
        X(UI_BUTTON, UI_BUTTON_TEXT, 48, 570, 67, 36, "Cancel") X(UI_BUTTON, UI_BUTTON_PRIMARY, 204, 570, 129, 36, "Provision node") \
        X(UI_BUTTON, UI_BUTTON_PRIMARY, 405, 719, 75, 36, "Deploy") X(UI_BUTTON, UI_BUTTON_DEFAULT, 492, 719, 62, 36, "Build") \
        X(UI_BUTTON, UI_BUTTON_OUTLINED, 566, 719, 63, 36, "Logs") X(UI_BUTTON, UI_BUTTON_TEXT, 405, 767, 67, 36, "Cancel") \
        X(UI_BUTTON, UI_BUTTON_DEFAULT, 405, 819, 87, 36, "Disabled", .disabled = 1) X(UI_BUTTON, UI_BUTTON_OUTLINED, 504, 819, 87, 36, "Disabled", .disabled = 1) \
        X(UI_BUTTON, UI_BUTTON_PRIMARY, 916, 1453, 127, 36, "Verify updates") \
        X(UI_TABS, UI_TABS_SEGMENTED, 761, 89, 223, 32, null, .count = 3, .items = {"All", "Running", "Stopped"}) \
        X(UI_PROGRESS, 0, 49, 1294, 994, 6, null, .value = -1) X(UI_PROGRESS, 0, 49, 1382, 994, 6, null, .value = UI_ONE / 100 * 80) \
        X(UI_CARD, 0, 736, 654, 332, 507, null) X(UI_HEADING, UI_TITLE_MEDIUM, 761, 679, 282, 0, "Pieces", .role = UI_ON_SURFACE) \
        X(UI_BADGE, 1, 761, 719, 0, 22, "Accent", .role = UI_PRIMARY) X(UI_BADGE, 1, -1, 719, 0, 22, "Healthy", .role = UI_SUCCESS) \
        X(UI_BADGE, 0, -1, 719, 0, 22, "Degraded", .role = UI_WARNING) X(UI_BADGE, 0, 761, 749, 0, 22, "Failed", .role = UI_ERROR) \
        X(UI_BADGE, 0, -1, 749, 0, 22, "Info", .role = UI_INFO) X(UI_BADGE, 0, -1, 749, 0, 22, "Idle", .role = UI_SURFACE_HIGHEST) \
        X(UI_BADGE, 0, -1, 749, 0, 22, "Draft", .role = -1) \
        X(UI_TAG, 0, 761, 787, 0, 26, "riscv64") X(UI_TAG, 0, -1, 787, 0, 26, "kernel", .on = 1) \
        X(UI_KBD, 0, -1, 790, 0, 20, "Ctrl") X(UI_KBD, 0, -1, 790, 0, 20, "K") \
        X(UI_AVATAR, 0, 761, 829, 32, 32, "AL", .on = 1) X(UI_AVATAR, 0, 805, 833, 24, 24, "DL") X(UI_AVATAR, 1, 841, 829, 32, 32, "MW") \
        X(UI_SPINNER, 0, 905, 838, 14, 14, null) X(UI_SPINNER, 0, 931, 835, 20, 20, null) X(UI_SPINNER, 0, 963, 831, 28, 28, null) \
        X(UI_CALLOUT, 0, 761, 877, 282, 70, "Kernel rebuild pending", .role = UI_WARNING, .text = "Three nodes wait for it.") \
        X(UI_CODE_WELL, 0, 761, 963, 282, 172, "build.sh", .count = 4, \
          .items = {"$ sh build.sh arch/x64", "  kernel   ok", "  shell    ok", "  canvas   ok"})

struct gallery_row
{
        unsigned char kind, variant;
        short x, y, w, h;
        const char address_to label, address_to text; // a callout's second line is the text
        int role, count;
        b32 on, disabled;
        const char address_to items[4]; // a radios', tabs' or code well's lines; a field's hint in the first
        int low, high, step, value;     // a slider's range and value, whole numbers; a progress's as the control has it
};

#define GALLERY_COUNT(...) +1
enum { GALLERY_MOST = 0 GALLERY_PAGE(GALLERY_COUNT) };
#define GALLERY_ROW(kind, variant, x, y, w, h, ...) {kind, variant, x, y, w, h, __VA_ARGS__},
static const struct gallery_row gallery_page[GALLERY_MOST] = {GALLERY_PAGE(GALLERY_ROW)};
#undef GALLERY_COUNT
#undef GALLERY_ROW
#undef GALLERY_PAGE

static struct
{
        b32 on;
        struct ui_control control[GALLERY_MOST];
        int count;
        struct ui_scroll scroll; // the page, device pixels: the wheel, the keys and its scrollbar move it
        struct ui_text field[2]; // the fields' engines: a node's name, which takes a name's characters, and a region
        int band_top, band_bottom; // the rows a motion that moved only a slider's ghost draws again (none: the whole page)
} gallery;

// A control's pointer state as a motion finds it (gallery_event): its hover, what it is on, and a slider's hover_x (its ghost).
struct gallery_mark
{
        b32 hover;
        int item, hover_x;
};

// The page, from the demo's layout in Chrome: each row of the list made a control, and the fields given their engines.
static fn gallery_build(struct window_row address_to own, const char address_to (*accept)(p32 code), int most)
{
        int field = 0;

        gallery.count = GALLERY_MOST;
        for (int at = 0; at < GALLERY_MOST; at++)
        {
                const struct gallery_row address_to r = &gallery_page[at];
                struct ui_control address_to c = &gallery.control[at];

                memory_zero(c, sizeof(*c));
                c->kind = r->kind, c->variant = r->variant;
                c->x = r->x, c->y = r->y, c->w = r->w, c->h = r->kind == UI_HEADING ? ui_types[r->variant].line : r->h;
                c->label = r->label, c->role = r->role, c->on = r->on, c->disabled = r->disabled, c->count = r->count;
                for (int k = 0; k < 4; k++)
                        c->items[k] = r->items[k];
                if (r->text)
                        string_copy_bounded((string_address)c->text, (string_address)r->text, sizeof(c->text));
                if (r->kind == UI_SLIDER)
                        c->low = ui_q(r->low), c->high = ui_q(r->high), c->step = ui_q(r->step), c->value = ui_q(r->value);
                else
                        c->value = r->value;
                // The fields are the kit's, typed in as any is: the node's name takes what a machine's name takes and says so.
                if (r->kind == UI_INPUT)
                {
                        struct ui_text address_to t = &gallery.field[field++];

                        if (!t->doc.selections)
                                ui_field_open(t, own, "");
                        ui_field_focus(t, false);
                        t->most = 63;
                        t->keeps_vertical = true; // Up and Down scroll the page
                        c->edit = t;
                }
                ui_control_start(c);
        }
        gallery.field[0].accept = accept;
        gallery.field[0].most = most, gallery.field[0].most_said = "a name is under 32 characters";
}

static fn gallery_place(struct ui address_to ui, int width, int height);

// The page drawn into the rows of the band: the paper, then every control, the scroll taken off their places.
static fn gallery_draw(struct ui address_to ui)
{
        int right = 0, row_y = -1, scroll, drawn, rest;

        ui_scroll_frame(ui, &gallery.scroll);
        // The page is drawn at rest, inside its ends, and a pull past one is the picture stretched (ui_scroll_pull)
        drawn = gallery.scroll.scroll;
        rest = ui_scroll_rest(&gallery.scroll, drawn, ui->height);
        scroll = rest * 100 / ui->scale;
        ui->top = 0;
        ui->bottom = ui->height;
        ui_fill(ui, 0, 0, ui->width, ui->height, ui_role_surface());
        for (int at = 0; at < gallery.count; at++)
        {
                struct ui_control address_to c = &gallery.control[at];
                int y = c->y;

                // A piece that flows (x -1): after the last on its row, 8 apart.
                if (c->x < 0)
                        c->x = row_y == c->y || row_y == c->y - 3 || row_y == c->y + 3 ? right + 8 : 761;
                c->y = y - scroll;
                if (c->y + c->h + 120 > 0 && c->y - 120 < ui->height * 100 / ui->scale)
                {
                        // The page is laid out left to right and turned over its width, as a window is, for a window that reads from the right.
                        int placed = c->x;

                        ui_control_size(ui, c);
                        c->x = ui_mirror(c->x, c->w, 0, GALLERY_W);
                        ui_control_draw(ui, c);
                        c->x = placed;
                }
                c->y = y;
                if (c->kind == UI_BADGE || c->kind == UI_TAG || c->kind == UI_KBD)
                        right = c->x + c->w, row_y = c->y;
        }
        gallery_place(ui, ui->width, ui->height);
        ui_scroll_pull(ui, &gallery.scroll, drawn, 0, 0, ui->width, ui->height);
        ui_scroll_draw(ui, &gallery.scroll);
        // A finger's selection in either field: its handles and actions bar, over the page.
        ui_text_overlay(ui, &gallery.field[0]);
        ui_text_overlay(ui, &gallery.field[1]);
}

// The page's region: the window, over the whole page's height.
static fn gallery_place(struct ui address_to ui, int width, int height)
{
        struct ui_scroll address_to s = &gallery.scroll;

        s->x = ui_rtl() ? 0 : width - ui_px(ui, UI_SCROLL_WIDTH), s->y = 0, s->height = s->view = height;
        s->inset = UI_CLOSE_BOTTOM; // the window's close button is at its top right: the bar starts below it
        s->content = ui_px(ui, GALLERY_H);
        ui_scroll_clamp(s);
}

// The pointer, the wheel and the keys on the page: the kit's controls take the pointer, in page pixels; the region the rest.
static b32 gallery_event(struct ui address_to ui, int width, int height, struct ui_popup address_to popup, const struct window_event address_to event, b32 address_to redraw)
{
        struct ui_scroll address_to s = &gallery.scroll;
        int offset = s->scroll * 100 / ui->scale;
        b32 moved = false;

        gallery.band_top = gallery.band_bottom = 0;
        gallery_place(ui, width, height);
        if (event->type == WINDOW_EVENT_MOTION)
                ui_kit.pointer_x = event->a, ui_kit.pointer_y = event->b;
        // The fields are the kit's: the one a press focused has the keys first (Escape gives them back to the page),
        // and each takes the pointer on its well, the drag it began and the clipboard's answer to its paste.
        if (event->type == WINDOW_EVENT_BUTTON && (event->flags & WINDOW_KEY_DOWN))
                for (int k = 0; k < 2; k++)
                        ui_field_focus(&gallery.field[k], ui_text_inside(&gallery.field[k], ui_kit.pointer_x, ui_kit.pointer_y));
        for (int k = 0; k < 2; k++)
        {
                struct ui_text address_to t = &gallery.field[k];
                p32 asks = t->doc.selections && (event->type != WINDOW_EVENT_KEY || t->focused)
                               ? ui_field_event(ui, t, popup, event, ui_kit.pointer_x, ui_kit.pointer_y)
                               : 0;

                // A press outside a gallery field is the page's: the field's answer to it (commit or cancel) changes nothing here.
                if (event->type == WINDOW_EVENT_BUTTON && (asks == UI_TEXT_COMMIT || asks == UI_TEXT_CANCEL))
                        asks = 0;
                if (asks & (UI_TEXT_CANCEL | UI_TEXT_SUBMIT))
                        ui_field_focus(t, false);
                if (asks)
                {
                        for (int at = 0; at < gallery.count; at++)
                                if (gallery.control[at].edit)
                                        gallery.control[at].focused = gallery.control[at].edit->focused;
                        *redraw = true;
                        return true;
                }
        }
        if (event->type == WINDOW_EVENT_KEY)
        {
                if ((event->flags & WINDOW_KEY_DOWN) && event->a == UI_KEY_ESCAPE)
                        return false;
                if ((event->flags & WINDOW_KEY_DOWN) && (event->a == UI_KEY_DOWN || event->a == UI_KEY_UP))
                        moved = ui_scroll_set(s, s->scroll + (event->a == UI_KEY_DOWN ? 1 : -1) * ui_px(ui, UI_ROW));
        }
        else if (event->type == WINDOW_EVENT_WHEEL)
                moved = ui_wheel_turn(ui, s, event);
        else if (event->type == WINDOW_EVENT_MOTION || event->type == WINDOW_EVENT_BUTTON)
        {
                int x = ui_kit.pointer_x, y = ui_kit.pointer_y, bar;
                b32 hover_scroll = s->hover;
                struct gallery_mark was[GALLERY_MOST];
                b32 quiet;

                if (event->type == WINDOW_EVENT_BUTTON && event->a != UI_POINTER_LEFT)
                        return true;
                bar = ui_scroll_pointer(ui, s, (int)event->type, x, y, (event->flags & WINDOW_KEY_DOWN) != 0);
                if (bar >= 0)
                        moved = ui_scroll_set(s, bar) || true;
                else
                {
                        /*
                                A motion with nothing held, that moves no hover (a control's or the scroll bar's) and drags no field,
                                can change only the ghost of the slider it is over (ui_kit_pointer's hover_x): then only that
                                slider's rows are drawn again (gallery.band_top, band_bottom). A body that moves or stretches
                                draws the whole page, as every motion did.
                        */
                        quiet = event->type == WINDOW_EVENT_MOTION && !ui_kit_held && !s->held && s->hover == hover_scroll &&
                                !(event->flags & WINDOW_KEY_TOUCH) && !ui_body_going(&s->body) && s->scroll >= 0 && s->scroll <= ui_scroll_most(s) &&
                                !gallery.field[0].sel.dragging && !gallery.field[1].sel.dragging;
                        for (int at = 0; at < gallery.count; at++)
                        {
                                was[at].hover = gallery.control[at].hover;
                                was[at].item = gallery.control[at].item;
                                was[at].hover_x = gallery.control[at].hover_x;
                        }
                        // The controls are drawn with the scroll taken off; the pointer is put on the page. A press off
                        // the fields takes their keys away.
                        // gallery_draw turns the page over its width when the window reads from the right, so the controls are
                        // hit where they are drawn: mirrored, the same as the draw's, and back again after.
                        for (int at = 0; at < gallery.count; at++)
                                gallery.control[at].y -= offset, gallery.control[at].x = ui_mirror(gallery.control[at].x, gallery.control[at].w, 0, GALLERY_W);
                        ui_kit_pointer(ui, gallery.control, gallery.count, (int)event->type, x, y,
                                       (event->flags & WINDOW_KEY_DOWN) != 0, event->flags);
                        for (int at = 0; at < gallery.count; at++)
                                gallery.control[at].y += offset, gallery.control[at].x = ui_mirror(gallery.control[at].x, gallery.control[at].w, 0, GALLERY_W);
                        for (int at = 0; at < gallery.count; at++)
                                if (gallery.control[at].edit)
                                        ui_field_focus(gallery.control[at].edit, gallery.control[at].focused);
                        moved = true;
                        for (int at = 0; quiet && at < gallery.count; at++)
                                if (gallery.control[at].hover != was[at].hover || gallery.control[at].item != was[at].item)
                                        quiet = false;
                        if (quiet)
                        {
                                int top = 0, bottom = 0;
                                b32 ghost = false;

                                // A slider's rows: its box and a row each way (the thumb and its ring lie in the box), in the window's device rows.
                                for (int at = 0; at < gallery.count; at++)
                                {
                                        struct ui_control address_to c = &gallery.control[at];
                                        int from, to;

                                        if (c->kind != UI_SLIDER || c->hover_x == was[at].hover_x)
                                                continue;
                                        from = ui_dev(ui, c->y - offset - 8), to = ui_dev(ui, c->y - offset + c->h + 8);
                                        top = ghost && top < from ? top : from;
                                        bottom = ghost && bottom > to ? bottom : to;
                                        ghost = true;
                                }
                                if (ghost)
                                        gallery.band_top = top < 0 ? 0 : top, gallery.band_bottom = bottom > height ? height : bottom;
                        }
                }
        }
        *redraw = moved;
        return true;
}

