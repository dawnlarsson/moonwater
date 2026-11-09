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
#define GALLERY_MOST 72

static struct
{
        b32 on;
        struct ui_control control[GALLERY_MOST];
        int count;
        struct ui_scroll scroll; // the page, device pixels: the wheel, the keys and its scrollbar move it
        struct ui_text field[2]; // the fields' engines: a node's name, which takes a name's characters, and a region
} gallery;

static struct ui_control address_to gallery_put(int kind, int variant, int x, int y, int w, int h, const char address_to label)
{
        struct ui_control address_to c = &gallery.control[gallery.count < GALLERY_MOST - 1 ? gallery.count++ : gallery.count];

        memory_zero(c, sizeof(*c));
        c->kind = (unsigned char)kind, c->variant = (unsigned char)variant;
        c->x = x, c->y = y, c->w = w, c->h = h;
        c->label = label;
        return c;
}

static fn gallery_slider(struct ui_control address_to c, int low, int high, int step, int value)
{
        c->low = ui_q(low), c->high = ui_q(high), c->step = ui_q(step), c->value = ui_q(value);
}

// The page, from the demo's layout in Chrome (page pixels, the first card's top left at 24, 24).
static fn gallery_build(struct window_row address_to own, const char address_to (*accept)(p32 code), int most)
{
        gallery.count = 0;
        gallery_put(UI_WINDOW, 0, 24, 24, 332, 606, "Provision node");
        gallery_put(UI_CARD, 0, 380, 24, 332, 606, null);
        gallery_put(UI_CARD, 0, 736, 24, 332, 606, null);
        gallery_put(UI_CARD, 0, 24, 654, 332, 507, null);
        gallery_put(UI_CARD, 0, 380, 654, 332, 507, null);
        gallery_put(UI_CARD, 0, 24, 1185, 1044, 329, null);
        gallery_put(UI_RULE, 0, 48, 537, 284, 1, null);
        gallery_put(UI_RULE, 0, 405, 233, 282, 1, null);
        gallery_put(UI_RULE, 0, 49, 927, 282, 1, null);
        gallery_put(UI_RULE, 0, 49, 1420, 994, 1, null);
        gallery_put(UI_HEADING, UI_TITLE_MEDIUM, 405, 49, 282, ui_types[UI_TITLE_MEDIUM].line, "System preferences")->role = UI_ON_SURFACE;
        gallery_put(UI_HEADING, UI_TITLE_MEDIUM, 761, 49, 282, ui_types[UI_TITLE_MEDIUM].line, "Navigation")->role = UI_ON_SURFACE;
        gallery_put(UI_HEADING, UI_TITLE_MEDIUM, 49, 679, 282, ui_types[UI_TITLE_MEDIUM].line, "Build target")->role = UI_ON_SURFACE;
        gallery_put(UI_HEADING, UI_TITLE_MEDIUM, 405, 679, 282, ui_types[UI_TITLE_MEDIUM].line, "Actions")->role = UI_ON_SURFACE;
        gallery_put(UI_HEADING, UI_TITLE_MEDIUM, 49, 1210, 994, ui_types[UI_TITLE_MEDIUM].line, "System upgrades")->role = UI_ON_SURFACE;
        gallery_put(UI_HEADING, UI_BODY_MEDIUM, 49, 1250, 994, ui_types[UI_BODY_MEDIUM].line, "Firmware DB-9 patching in progress\xe2\x80\xa6")->role = UI_ON_SURFACE_VARIANT;
        gallery_put(UI_HEADING, UI_BODY_MEDIUM, 49, 1338, 994, ui_types[UI_BODY_MEDIUM].line, "Node replication (80%)")->role = UI_ON_SURFACE_VARIANT;
        gallery_put(UI_HEADING, UI_BODY_MEDIUM, 49, 1461, 221, ui_types[UI_BODY_MEDIUM].line, "3 nodes pending a kernel rebuild.")->role = UI_ON_SURFACE_VARIANT;
        // The fields are the kit's, typed in as any is: the node's name takes what a machine's name takes and says so.
        for (int k = 0; k < 2; k++)
        {
                struct ui_control address_to field = gallery_put(UI_INPUT, 0, UI_FIELD_H, k ? 203 : 117, 284, 78, k ? "Region" : "Node name");

                if (!gallery.field[k].doc.selections)
                        ui_field_open(&gallery.field[k], own, "");
                ui_field_focus(&gallery.field[k], false);
                gallery.field[k].most = 63;
                gallery.field[k].keeps_vertical = true; // Up and Down scroll the page
                field->items[0] = k ? "moon-1" : "my-kernel";
                field->edit = &gallery.field[k];
        }
        gallery.field[0].accept = accept;
        gallery.field[0].most = most, gallery.field[0].most_said = "a name is under 32 characters";
        gallery_slider(gallery_put(UI_SLIDER, 0, 48, 289, 284, 88, "vCPU allocation"), 1, 64, 1, 8);
        gallery_slider(gallery_put(UI_SLIDER, 0, 405, 266, 282, 88, "Log verbosity"), 0, 100, 1, 70);
        gallery_slider(gallery_put(UI_SLIDER, UI_SLIDER_STEPS, 405, 370, 282, 88, "Replication limit"), 0, 100, 25, 50);
        gallery_put(UI_SWITCH, 0, 48, 393, 284, 48, "Pin to bare metal")->on = 1;
        gallery_put(UI_SWITCH, 0, 48, 457, 284, 48, "Auto-deploy on push")->on = 0;
        gallery_put(UI_SWITCH, 0, 405, 89, 282, 48, "Telemetry")->on = 1;
        gallery_put(UI_SWITCH, 0, 405, 153, 282, 48, "Offline cache")->on = 0;
        gallery_put(UI_CHECKBOX, 0, 49, 960, 282, 48, "Enable WASM target")->on = 1;
        gallery_put(UI_CHECKBOX, 0, 49, 1024, 282, 48, "Build as no_std")->on = 1;
        gallery_put(UI_CHECKBOX, 0, 49, 1088, 282, 48, "Strip symbols")->on = 0;
        {
                struct ui_control address_to radios = gallery_put(UI_RADIOS, 0, 49, 719, 282, 48, null);

                radios->items[0] = "x86-64";
                radios->items[1] = "ARM64";
                radios->items[2] = "RISC-V (experimental)";
                radios->count = 3;
                radios->at = 0;
        }
        gallery_put(UI_BUTTON, UI_BUTTON_TEXT, 48, 570, 67, 36, "Cancel")->disabled = 0;
        gallery_put(UI_BUTTON, UI_BUTTON_PRIMARY, 204, 570, 129, 36, "Provision node")->disabled = 0;
        gallery_put(UI_BUTTON, UI_BUTTON_PRIMARY, 405, 719, 75, 36, "Deploy")->disabled = 0;
        gallery_put(UI_BUTTON, UI_BUTTON_DEFAULT, 492, 719, 62, 36, "Build")->disabled = 0;
        gallery_put(UI_BUTTON, UI_BUTTON_OUTLINED, 566, 719, 63, 36, "Logs")->disabled = 0;
        gallery_put(UI_BUTTON, UI_BUTTON_TEXT, 405, 767, 67, 36, "Cancel")->disabled = 0;
        gallery_put(UI_BUTTON, UI_BUTTON_DEFAULT, 405, 819, 87, 36, "Disabled")->disabled = 1;
        gallery_put(UI_BUTTON, UI_BUTTON_OUTLINED, 504, 819, 87, 36, "Disabled")->disabled = 1;
        gallery_put(UI_BUTTON, UI_BUTTON_PRIMARY, 916, 1453, 127, 36, "Verify updates")->disabled = 0;
        {
                struct ui_control address_to tabs = gallery_put(UI_TABS, UI_TABS_SEGMENTED, 761, 89, 223, 32, null);

                tabs->items[0] = "All";
                tabs->items[1] = "Running";
                tabs->items[2] = "Stopped";
                tabs->count = 3;
        }
        gallery_put(UI_PROGRESS, 0, 49, 1294, 994, 6, null)->value = -1;
        gallery_put(UI_PROGRESS, 0, 49, 1382, 994, 6, null)->value = UI_ONE / 100 * 80;
        // The pieces, in the grid's cell the demo leaves empty: a heading, then rows that flow left to right (x -1).
        gallery_put(UI_CARD, 0, 736, 654, 332, 507, null);
        gallery_put(UI_HEADING, UI_TITLE_MEDIUM, 761, 679, 282, ui_types[UI_TITLE_MEDIUM].line, "Pieces")->role = UI_ON_SURFACE;
        gallery_put(UI_BADGE, 1, 761, 719, 0, 22, "Accent")->role = UI_PRIMARY;
        gallery_put(UI_BADGE, 1, -1, 719, 0, 22, "Healthy")->role = UI_SUCCESS;
        gallery_put(UI_BADGE, 0, -1, 719, 0, 22, "Degraded")->role = UI_WARNING;
        gallery_put(UI_BADGE, 0, 761, 749, 0, 22, "Failed")->role = UI_ERROR;
        gallery_put(UI_BADGE, 0, -1, 749, 0, 22, "Info")->role = UI_INFO;
        gallery_put(UI_BADGE, 0, -1, 749, 0, 22, "Idle")->role = UI_SURFACE_HIGHEST;
        gallery_put(UI_BADGE, 0, -1, 749, 0, 22, "Draft")->role = -1;
        gallery_put(UI_TAG, 0, 761, 787, 0, 26, "riscv64");
        gallery_put(UI_TAG, 0, -1, 787, 0, 26, "kernel")->on = 1;
        gallery_put(UI_KBD, 0, -1, 790, 0, 20, "Ctrl");
        gallery_put(UI_KBD, 0, -1, 790, 0, 20, "K");
        gallery_put(UI_AVATAR, 0, 761, 829, 32, 32, "AL")->on = 1;
        gallery_put(UI_AVATAR, 0, 805, 833, 24, 24, "DL");
        gallery_put(UI_AVATAR, 1, 841, 829, 32, 32, "MW");
        gallery_put(UI_SPINNER, 0, 905, 838, 14, 14, null);
        gallery_put(UI_SPINNER, 0, 931, 835, 20, 20, null);
        gallery_put(UI_SPINNER, 0, 963, 831, 28, 28, null);
        {
                struct ui_control address_to callout = gallery_put(UI_CALLOUT, 0, 761, 877, 282, 70, "Kernel rebuild pending");

                callout->role = UI_WARNING;
                string_copy_bounded((string_address)callout->text, (string_address) "Three nodes wait for it.", sizeof(callout->text));
        }
        {
                struct ui_control address_to code = gallery_put(UI_CODE_WELL, 0, 761, 963, 282, 172, "build.sh");

                code->items[0] = "$ sh build.sh arch/x64";
                code->items[1] = "  kernel   ok";
                code->items[2] = "  shell    ok";
                code->items[3] = "  canvas   ok";
                code->count = 4;
        }
        for (int at = 0; at < gallery.count; at++)
                ui_control_start(&gallery.control[at]);
}

static fn gallery_place(struct ui address_to ui, int width, int height);

// The page drawn into the rows of the band: the paper, then every control, the scroll taken off their places.
static fn gallery_draw(struct ui address_to ui)
{
        int right = 0, row_y = -1, scroll, drawn, rest;

        ui_scroll_frame(ui, &gallery.scroll);
        // The page is drawn at rest, inside its ends, and a pull past one is the picture stretched (ui_stretch)
        drawn = gallery.scroll.scroll;
        rest = drawn < 0 ? 0 : drawn > ui_scroll_most(&gallery.scroll) ? ui_scroll_most(&gallery.scroll) : drawn;
        scroll = rest * 100 / ui->scale;
        ui->top = 0;
        ui->bottom = ui->height;
        ui_fill(ui, 0, 0, ui->width, ui->height, ui_role_surface());
        for (int at = 0; at < gallery.count; at++)
        {
                struct ui_control address_to c = &gallery.control[at];
                int y = c->y;

                // A piece that flows: after the last on its row, 8 apart.
                if (c->x < 0 || (c->kind == UI_BADGE || c->kind == UI_TAG || c->kind == UI_KBD))
                {
                        if (c->x < 0 && (row_y == c->y || row_y == c->y - 3 || row_y == c->y + 3))
                                c->x = right + 8;
                        else if (c->x < 0)
                                c->x = 761;
                }
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
        if (drawn != rest)
                ui_stretch(ui, 0, 0, ui->width, ui->height, drawn - rest);
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

                if (event->type == WINDOW_EVENT_BUTTON && event->a != UI_POINTER_LEFT)
                        return true;
                bar = ui_scroll_pointer(ui, s, (int)event->type, x, y, (event->flags & WINDOW_KEY_DOWN) != 0);
                if (bar >= 0)
                        moved = ui_scroll_set(s, bar) || true;
                else
                {
                        // The controls are drawn with the scroll taken off; the pointer is put on the page. A press off
                        // the fields takes their keys away.
                        for (int at = 0; at < gallery.count; at++)
                                gallery.control[at].y -= offset;
                        ui_kit_pointer(ui, gallery.control, gallery.count, (int)event->type, x, y,
                                       (event->flags & WINDOW_KEY_DOWN) != 0, event->flags);
                        for (int at = 0; at < gallery.count; at++)
                                gallery.control[at].y += offset;
                        for (int at = 0; at < gallery.count; at++)
                                if (gallery.control[at].edit)
                                        ui_field_focus(gallery.control[at].edit, gallery.control[at].focused);
                        moved = true;
                }
        }
        *redraw = moved;
        return true;
}

