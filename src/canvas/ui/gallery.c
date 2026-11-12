/*
        The kit's gallery, the settings window on -g (moonwater settings gallery): every control of the UI kit on a page of its
        own, to see them move. It is a window of the settings program, included before the settings (shell.c and checks.c), and
        takes the window and the ui it draws in from the settings; nothing else uses it.
*/

/* ---- the gallery: the kit's showcase (moonwater settings -g) ---- */

/*
        The page is a view (ui.c): a column that scrolls, of rows of cards, each card a surface of the kit's with its pieces in a
        column on it, at the sizes and the gaps Dawning Native's own demo page has in Chrome at 1200 pixels (the provision dialog,
        the preferences, the tabs, the build target's radios and checkboxes, the actions and the upgrades' progress), and in the
        grid's empty cell the pieces its demo does not show (badges, tags, keys, avatars, spinners, a callout and a code well).
        The page is the theme's paper and not glass, so it is the web page's picture to compare. Nothing in it runs a verb: it is
        there to be pressed. What is kept here is the controls themselves and the text of the two fields, which is handed its
        events here (the view has no element for a field's text yet); where each control is, the scroll, the mirror of a window
        that reads from the right, the picture and the pointer are the view's.
*/
#define GALLERY_W 1092 // the page whole, logical pixels: the window it opens in
#define GALLERY_H 1538
#define GALLERY_MOST 64

// What is said of a piece where the page declares it: its kind, its size where the kit has no rule for one, and what it holds.
struct gallery_row
{
        unsigned char kind, variant;
        short w, h;
        const char address_to label, address_to class, address_to text; // a callout's second line is the text
        int role, count;
        b32 on, disabled;
        const char address_to items[4]; // a radios', tabs' or code well's lines; a field's hint in the first
        int low, high, step, value;     // a slider's range and value, whole numbers; a progress's as the control has it
        struct ui_text address_to edit; // a field's engine
};

static struct
{
        b32 on;
        struct ui_view view;
        struct ui_control control[GALLERY_MOST];
        int count;                 // the controls the page declared, in the order it declared them
        struct ui_text field[2];   // the fields' engines: a node's name, which takes a name's characters, and a region
        int band_top, band_bottom; // the rows a motion that moved only a slider's ghost draws again (none: the whole page)
} gallery;

// The next control of the page, made the first time the page is declared, from what is said of it there.
static struct ui_control address_to gallery_made(const struct gallery_row address_to r)
{
        struct ui_control address_to c = &gallery.control[gallery.count < GALLERY_MOST - 1 ? gallery.count++ : GALLERY_MOST - 1];

        if (c->kind)
                return c;
        c->kind = r->kind, c->variant = r->variant, c->label = r->label, c->role = r->role, c->on = r->on, c->disabled = r->disabled;
        c->count = r->count, c->edit = r->edit;
        for (int k = 0; k < 4; k++)
                c->items[k] = r->items[k];
        if (r->text)
                string_copy_bounded((string_address)c->text, (string_address)r->text, sizeof(c->text));
        if (r->kind == UI_SLIDER)
                c->low = ui_q(r->low), c->high = ui_q(r->high), c->step = ui_q(r->step), c->value = ui_q(r->value);
        else
                c->value = r->value;
        ui_control_start(c);
        return c;
}

// A piece of the page: its control, an element of the size and the class said.
static fn gallery_piece(const struct gallery_row address_to r)
{
        ui_control(gallery_made(r), .w = r->w, .h = r->h, .class = r->class);
}

#define surface(...) gallery_made(&(struct gallery_row){__VA_ARGS__})
#define piece(...) gallery_piece(&(struct gallery_row){__VA_ARGS__})

// The page's rows of cards: what it shows, in the order it is drawn and pressed, the pieces of one line side by side. A row is the
// demo page's width, so a window of another width shows the same page.
static fn gallery_page(void)
{
        ui_row(.class = "cards", .w = GALLERY_W - 48)
        {
                ui_col(.control = surface(UI_WINDOW, .label = "Provision node"), .class = "window", .grow = 1)
                {
                        ui_col(.class = "gap2")
                        {
                                piece(UI_INPUT, 0, 0, 78, "Node name", .items = {"my-kernel"}, .edit = &gallery.field[0]);
                                piece(UI_INPUT, 0, 0, 78, "Region", .items = {"moon-1"}, .edit = &gallery.field[1]);
                                piece(UI_SLIDER, 0, 0, 88, "vCPU allocation", .low = 1, .high = 64, .step = 1, .value = 8);
                        }
                        piece(UI_SWITCH, 0, 0, 48, "Pin to bare metal", .on = 1);
                        piece(UI_SWITCH, 0, 0, 48, "Auto-deploy on push");
                        piece(UI_RULE, .class = "rule");
                        ui_row(.class = "spread")
                                piece(UI_BUTTON, UI_BUTTON_TEXT, 67, 36, "Cancel"), piece(UI_BUTTON, UI_BUTTON_PRIMARY, 129, 36, "Provision node");
                }
                ui_col(.control = surface(UI_CARD), .class = "panel", .grow = 1)
                {
                        ui_text("System preferences", .class = "title");
                        piece(UI_SWITCH, 0, 0, 48, "Telemetry", .on = 1);
                        piece(UI_SWITCH, 0, 0, 48, "Offline cache");
                        piece(UI_RULE, .class = "rule");
                        piece(UI_SLIDER, 0, 0, 88, "Log verbosity", .high = 100, .step = 1, .value = 70);
                        piece(UI_SLIDER, UI_SLIDER_STEPS, 0, 88, "Replication limit", .high = 100, .step = 25, .value = 50);
                }
                ui_col(.control = surface(UI_CARD), .class = "panel", .grow = 1)
                {
                        ui_text("Navigation", .class = "title");
                        piece(UI_TABS, UI_TABS_SEGMENTED, 0, UI_TABS_H, .count = 3, .items = {"All", "Running", "Stopped"});
                }
        }
        ui_row(.class = "cards", .w = GALLERY_W - 48)
        {
                ui_col(.control = surface(UI_CARD), .class = "panel", .grow = 1)
                {
                        ui_text("Build target", .class = "title");
                        piece(UI_RADIOS, 0, 0, 2 * UI_RADIO_PITCH + 48, .count = 3, .items = {"x86-64", "ARM64", "RISC-V (experimental)"});
                        piece(UI_RULE, .class = "rule");
                        piece(UI_CHECKBOX, 0, 0, 48, "Enable WASM target", .on = 1);
                        piece(UI_CHECKBOX, 0, 0, 48, "Build as no_std", .on = 1);
                        piece(UI_CHECKBOX, 0, 0, 48, "Strip symbols");
                }
                ui_col(.control = surface(UI_CARD), .class = "panel", .grow = 1)
                {
                        ui_text("Actions", .class = "title");
                        ui_col(.class = "gap3")
                        {
                                ui_row(.class = "gap3")
                                        piece(UI_BUTTON, UI_BUTTON_PRIMARY, 75, 36, "Deploy"), piece(UI_BUTTON, UI_BUTTON_DEFAULT, 62, 36, "Build"), piece(UI_BUTTON, UI_BUTTON_OUTLINED, 63, 36, "Logs");
                                piece(UI_BUTTON, UI_BUTTON_TEXT, 67, 36, "Cancel");
                        }
                        ui_row(.class = "gap3")
                                piece(UI_BUTTON, UI_BUTTON_DEFAULT, 87, 36, "Disabled", .disabled = 1), piece(UI_BUTTON, UI_BUTTON_OUTLINED, 87, 36, "Disabled", .disabled = 1);
                }
                ui_col(.control = surface(UI_CARD), .class = "panel", .grow = 1)
                {
                        ui_text("Pieces", .class = "title");
                        ui_col(.class = "gap2")
                        {
                                ui_row(.class = "gap2")
                                        piece(UI_BADGE, 1, .label = "Accent", .role = UI_PRIMARY), piece(UI_BADGE, 1, .label = "Healthy", .role = UI_SUCCESS), piece(UI_BADGE, .label = "Degraded", .role = UI_WARNING);
                                ui_row(.class = "gap2")
                                        piece(UI_BADGE, .label = "Failed", .role = UI_ERROR), piece(UI_BADGE, .label = "Info", .role = UI_INFO), piece(UI_BADGE, .label = "Idle", .role = UI_SURFACE_HIGHEST), piece(UI_BADGE, .label = "Draft", .role = -1);
                        }
                        ui_row(.class = "gap2.level")
                                piece(UI_TAG, .label = "riscv64"), piece(UI_TAG, .label = "kernel", .on = 1), piece(UI_KBD, 0, 0, 20, "Ctrl"), piece(UI_KBD, 0, 0, 20, "K");
                        ui_row(.class = "gap3.level")
                        {
                                piece(UI_AVATAR, 0, 32, 32, "AL", .on = 1), piece(UI_AVATAR, 0, 24, 24, "DL"), piece(UI_AVATAR, 1, 32, 32, "MW");
                                ui_host(null, .w = 8);
                                piece(UI_SPINNER, 0, 14, 14), piece(UI_SPINNER, 0, 20, 20), piece(UI_SPINNER, 0, 28, 28);
                        }
                        piece(UI_CALLOUT, 0, 0, 70, "Kernel rebuild pending", .role = UI_WARNING, .text = "Three nodes wait for it.");
                        piece(UI_CODE_WELL, 0, 0, 172, "build.sh", .count = 4, .items = {"$ sh build.sh arch/x64", "  kernel   ok", "  shell    ok", "  canvas   ok"});
                }
        }
        ui_row(.class = "cards", .w = GALLERY_W - 48)
                ui_col(.control = surface(UI_CARD), .class = "panel", .grow = 1)
                {
                        ui_text("System upgrades", .class = "title");
                        ui_text("Firmware DB-9 patching in progress\xe2\x80\xa6", .class = "quiet");
                        piece(UI_PROGRESS, .class = "meter", .value = -1);
                        ui_host(null, .h = 6); // the demo page's two upgrades are 38 apart
                        ui_text("Node replication (80%)", .class = "quiet");
                        piece(UI_PROGRESS, .class = "meter", .value = UI_ONE / 100 * 80);
                        piece(UI_RULE, .class = "rule");
                        ui_row(.class = "spread")
                                ui_text("3 nodes pending a kernel rebuild.", .class = "quiet"), piece(UI_BUTTON, UI_BUTTON_PRIMARY, 127, 36, "Verify updates");
                }
}

#undef surface
#undef piece

// The page as new: no control made yet (the next layout makes them, gallery_made), the page at its top, and the fields given their
// engines; the node's name takes what a machine's name takes and says so.
static fn gallery_build(struct window_row address_to own, const char address_to (*accept)(p32 code), int most)
{
        memory_zero(gallery.control, sizeof(gallery.control));
        memory_zero(&gallery.view, sizeof(gallery.view));
        gallery.count = 0;
        for (int k = 0; k < 2; k++)
        {
                struct ui_text address_to t = &gallery.field[k];

                if (!t->doc.selections)
                        ui_field_open(t, own, "");
                ui_field_focus(t, false);
                t->most = 63;
                t->keeps_vertical = true; // Up and Down scroll the page
        }
        gallery.field[0].accept = accept;
        gallery.field[0].most = most, gallery.field[0].most_said = "a name is under 32 characters";
}

// The page declared and laid out in a window, which is where every control is: the paper, a column that scrolls, and on it the rows of
// cards once the page is built (gallery_build opens the fields; till then the window is its paper).
static fn gallery_layout(struct ui address_to ui, int width, int height)
{
        gallery.count = 0;
        ui_view_begin(&gallery.view, ui, width, -height);
        ui_col(.class = "page.scroll", .grow = 1)
                if (gallery.field[0].doc.selections)
                        gallery_page();
        ui_view_end(&gallery.view);
}

// The page drawn into the rows of the band, and a finger's selection in either field over it: its handles and actions bar.
static fn gallery_draw(struct ui address_to ui)
{
        gallery_layout(ui, ui->width, ui->height);
        ui_view_draw(ui, &gallery.view, false);
        ui_text_overlay(ui, &gallery.field[0]);
        ui_text_overlay(ui, &gallery.field[1]);
}

// An event on the page: the fields' text has it first, then the view (the controls, the scroll). Escape closes the window (false).
static b32 gallery_event(struct ui address_to ui, int width, int height, struct ui_popup address_to popup, const struct window_event address_to event, b32 address_to redraw)
{
        p32 asks;

        gallery.band_top = gallery.band_bottom = 0;
        gallery_layout(ui, width, height);
        // The fields are the kit's: the one a press focused has the keys first (Escape gives them back to the page),
        // and each takes the pointer on its well, the drag it began and the clipboard's answer to its paste.
        if (event->type == WINDOW_EVENT_BUTTON && (event->flags & WINDOW_KEY_DOWN))
                for (int k = 0; k < 2; k++)
                        ui_field_focus(&gallery.field[k], ui_text_inside(&gallery.field[k], ui_kit.pointer_x, ui_kit.pointer_y));
        for (int k = 0; k < 2; k++)
        {
                struct ui_text address_to t = &gallery.field[k];

                asks = t->doc.selections && (event->type != WINDOW_EVENT_KEY || t->focused) ? ui_field_event(ui, t, popup, event, ui_kit.pointer_x, ui_kit.pointer_y) : 0;
                // A press outside a gallery field is the page's: the field's answer to it (commit or cancel) changes nothing here.
                if (event->type == WINDOW_EVENT_BUTTON && (asks == UI_TEXT_COMMIT || asks == UI_TEXT_CANCEL))
                        asks = 0;
                if (asks & (UI_TEXT_CANCEL | UI_TEXT_SUBMIT))
                        ui_field_focus(t, false);
                for (int at = 0; asks && at < gallery.count; at++)
                        if (gallery.control[at].edit)
                                gallery.control[at].focused = gallery.control[at].edit->focused;
                if (asks)
                        return *redraw = true;
        }
        if (event->type == WINDOW_EVENT_KEY && (event->flags & WINDOW_KEY_DOWN) && event->a == UI_KEY_ESCAPE)
                return false;
        asks = ui_view_event(ui, &gallery.view, event);
        // The pointer is the view's: the field a press focused has the keys, and a press off the fields takes their keys away.
        for (int at = 0; (event->type == WINDOW_EVENT_MOTION || (event->type == WINDOW_EVENT_BUTTON && event->a == UI_POINTER_LEFT)) && at < gallery.count; at++)
                if (gallery.control[at].edit)
                        ui_field_focus(gallery.control[at].edit, gallery.control[at].focused);
        // The whole page is due, or only the rows of a slider whose ghost moved.
        *redraw = ui_view_moved(&gallery.view, &gallery.band_top, &gallery.band_bottom) || (asks & UI_VIEW_DRAW);
        if (asks & UI_VIEW_DRAW)
                gallery.band_top = gallery.band_bottom = 0;
        return true;
}
