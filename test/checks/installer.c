/*
        test/checks/installer.c: the installer, its model and its verb's write path.

        A run of test/checks.c, which includes this file where the run stood: compiled only as part of that file. Built as a
        freestanding section from the repository root:

            gcc -O2 -static -nostdlib -nostartfiles -march=x86-64 -fno-stack-protector -fno-builtin -w -T src/build/spark.ld \
                -Wl,-e,_start -DCHECK_installer -fwhole-program -o OUT test/checks.c
            OUT fonts/inter/Inter-Regular.ttf fonts/inter/Inter-Bold.ttf [PICTURES]

        The rows: the drive listing read from a sysfs tree made under /tmp (sizes, models, removable, Moonwater by its GPT names,
        loop, ram and zram left out), its lines written and read back, the boot drive left out of the targets, the plan (the
        install's words for each mode, the language and the keyboard only where changed, the profiles only online, Steam only on
        x86-64), the default route, the window's steps (Back and Next, Disk held until a drive is chosen, Update only for a
        drive with Moonwater, the cards off while offline, the profiles chosen reaching the plan, the language and the keyboard
        chosen reaching it), Install refused for virtual input, and the write path: the install's layout written to a file of
        3 GiB (no block device), its GPT read back as an install's, and an image's SHA-256 read back equal, and unequal after one
        byte changed. With PICTURES, each step is drawn on the light theme and written there as a PPM.
*/

#ifdef CHECK_installer
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

#define ICHECK_ROOT "/tmp/moonwater-installer-check"
#define ICHECK_W 760
#define ICHECK_H 600

static fn icheck_write(string_address path, string_address text)
{
        bipolar handle = system_open_at_mode(AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

        if (handle < 0)
                return;
        system_write_once((b32)handle, text, string_length(text));
        system_close(handle);
}

static fn icheck_dir(string_address path)
{
        system_make_directory_exact_at(AT_FDCWD, path, 0755);
}

// A block device of the tree: its size in sectors, its flag and its model, and partitions by their GPT names.
static fn icheck_disk(string_address name, string_address sectors, string_address removable, string_address model,
                      string_address part1, string_address part2)
{
        p8 path[256], sub[256];

        string_copy_bounded(path, (string_address)ICHECK_ROOT "/block/", sizeof(path));
        string_append_bounded(path, name, sizeof(path));
        icheck_dir(path);
        string_copy_bounded(sub, path, sizeof(sub)), string_append_bounded(sub, (string_address) "/size", sizeof(sub)), icheck_write(sub, sectors);
        string_copy_bounded(sub, path, sizeof(sub)), string_append_bounded(sub, (string_address) "/removable", sizeof(sub)), icheck_write(sub, removable);
        string_copy_bounded(sub, path, sizeof(sub)), string_append_bounded(sub, (string_address) "/device", sizeof(sub)), icheck_dir(sub);
        if (model)
                string_append_bounded(sub, (string_address) "/model", sizeof(sub)), icheck_write(sub, model);
        for (int k = 0; k < 2; k++)
        {
                string_address part = k ? part2 : part1;
                p8 dir[256], file[256], text[96];

                if (!part)
                        continue;
                string_copy_bounded(dir, path, sizeof(dir));
                string_append_bounded(dir, (string_address) "/", sizeof(dir));
                string_append_bounded(dir, name, sizeof(dir));
                string_append_bounded(dir, (string_address)(k ? "2" : "1"), sizeof(dir));
                icheck_dir(dir);
                string_copy_bounded(file, dir, sizeof(file)), string_append_bounded(file, (string_address) "/partition", sizeof(file));
                icheck_write(file, (string_address)(k ? "2\n" : "1\n"));
                string_copy_bounded(file, dir, sizeof(file)), string_append_bounded(file, (string_address) "/uevent", sizeof(file));
                string_copy_bounded(text, (string_address) "MAJOR=8\nPARTN=1\nPARTNAME=", sizeof(text));
                string_append_bounded(text, part, sizeof(text));
                string_append_bounded(text, (string_address) "\n", sizeof(text));
                icheck_write(file, text);
        }
}

static const struct installer_drive address_to icheck_find(const struct installer_drive address_to drives, int count, const char address_to name)
{
        for (int at = 0; at < count; at++)
                if (string_equals((string_address)drives[at].name, (string_address)name))
                        return &drives[at];
        return null;
}

static fn icheck_listing(void)
{
        struct installer_drive drives[INSTALLER_DRIVES], back[INSTALLER_DRIVES];
        const struct installer_drive address_to d;
        char text[2048], line[256];
        positive length = 0;
        int count, again;

        bowl_reset_walk((string_address)ICHECK_ROOT, 0, null);
        icheck_dir((string_address)ICHECK_ROOT);
        icheck_dir((string_address)ICHECK_ROOT "/block");
        icheck_disk((string_address) "sda", (string_address) "976773168\n", (string_address) "0\n", (string_address) "Samsung SSD 860   \n",
                    (string_address)INSTALLER_BOOT_PART, (string_address)INSTALLER_DATA_PART);
        icheck_disk((string_address) "sdb", (string_address) "60437492\n", (string_address) "1\n", (string_address) "Cruzer Blade\n",
                    (string_address) "EFI", null);
        icheck_disk((string_address) "nvme0n1", (string_address) "2000409264\n", (string_address) "0\n", (string_address) "WD Blue SN570\n",
                    (string_address)INSTALLER_BOOT_PART, null);
        icheck_disk((string_address) "loop0", (string_address) "1024\n", (string_address) "0\n", null, null, null);
        icheck_disk((string_address) "zram0", (string_address) "1024\n", (string_address) "0\n", null, null, null);
        icheck_disk((string_address) "ram0", (string_address) "1024\n", (string_address) "0\n", null, null, null);
        icheck_disk((string_address) "sdc", (string_address) "0\n", (string_address) "1\n", (string_address) "Card reader\n", null, null);

        count = installer_drives_read(ICHECK_ROOT, "sdb", drives, INSTALLER_DRIVES);
        check("installer listing: three drives read from the tree (loop, zram, ram and an empty reader left out)", count == 3);
        d = icheck_find(drives, count, "sda");
        check("installer listing: a drive's size is its sectors times 512", d && d->bytes == 976773168ull * 512);
        check("installer listing: its model, trailing blanks off", d && string_equals((string_address)d->model, (string_address) "Samsung SSD 860"));
        check("installer listing: both of an install's GPT names are Moonwater", d && d->moonwater && !d->removable && !d->boot);
        d = icheck_find(drives, count, "nvme0n1");
        check("installer listing: one of them alone is not", d && !d->moonwater);
        d = icheck_find(drives, count, "sdb");
        check("installer listing: the removable flag and the boot drive are read", d && d->removable && d->boot && !d->moonwater);
        check("installer listing: loop, ram and zram are no drives", !icheck_find(drives, count, "loop0") && !icheck_find(drives, count, "zram0") &&
                                                                         !icheck_find(drives, count, "ram0"));

        for (int at = 0; at < count; at++)
        {
                installer_line(&drives[at], line, sizeof(line));
                memory_copy(text + length, line, string_length((string_address)line));
                length += string_length((string_address)line);
        }
        memory_copy(text + length, "loop1\t100\t0\t0\t0\tx\nbad line\nsdz\tnot\t0\t0\t0\tm\n", 43);
        length += 43;
        again = installer_parse(text, length, back, INSTALLER_DRIVES);
        check("installer listing: the lines read back as the drives, a malformed line and a loop device passed over", again == count);
        d = icheck_find(back, again, "sda");
        check("installer listing: a drive read back is the drive written", d && d->bytes == 976773168ull * 512 && d->moonwater &&
                                                                            string_equals((string_address)d->model, (string_address) "Samsung SSD 860"));
        again = installer_targets(back, again);
        check("installer boot drive: the one this booted from is no target", again == 2 && !icheck_find(back, again, "sdb"));
        check("installer boot drive: excluded by name, as loop, ram and zram are", installer_excluded("sdb", "sdb") && installer_excluded("loop3", null) &&
                                                                                       installer_excluded("zram0", "") && installer_excluded("ram1", "sdb") &&
                                                                                       !installer_excluded("sda", "sdb"));
        bowl_reset_walk((string_address)ICHECK_ROOT, 0, null);
        system_remove_at(AT_FDCWD, (string_address)ICHECK_ROOT, AT_REMOVEDIR);
}

static b32 icheck_has(const char address_to plan, const char address_to line)
{
        return string_find((string_address)plan, (string_address)line) != null;
}

static fn icheck_plan(void)
{
        struct installer_choice c;
        char plan[INSTALLER_PLAN];
        int count;

        memory_zero(&c, sizeof(c));
        string_copy((string_address)c.drive, (string_address) "sda");
        string_copy((string_address)c.language, (string_address) "de");
        string_copy((string_address)c.keyboard, (string_address) "de");
        c.mode = INSTALLER_OVERWRITE;
        c.profiles = INSTALLER_CHROME | INSTALLER_STEAM | INSTALLER_SSH;
        count = installer_plan(&c, true, plan, sizeof(plan));
        check("installer plan: offline, the install alone: no profile, and nothing the person did not change",
              count == 1 && string_equals((string_address)plan, (string_address) "/moonwater install sda overwrite yes\n"));
        c.online = true, c.language_changed = true, c.mode = INSTALLER_UPDATE;
        count = installer_plan(&c, true, plan, sizeof(plan));
        check("installer plan: an update's words, the language changed reaching it and the keyboard not",
              icheck_has(plan, "/moonwater install sda update yes\n") && icheck_has(plan, "/moonwater language de\n") &&
                  !icheck_has(plan, "keyboard"));
        check("installer plan: online, every profile chosen, SSH turned on after its profile",
              count == 6 && icheck_has(plan, "/bowl profile chrome\n") && icheck_has(plan, "/bowl profile steam\n") &&
                  icheck_has(plan, "/bowl profile ssh\n/moonwater ssh on\n"));
        c.keyboard_changed = true;
        count = installer_plan(&c, false, plan, sizeof(plan));
        check("installer plan: the keyboard changed reaches it; Steam is not run off x86-64",
              icheck_has(plan, "/moonwater keyboard de\n") && !icheck_has(plan, "steam") && count == 6);
        string_copy((string_address)c.drive, (string_address) "loop0");
        check("installer plan: no plan for a loop device", installer_plan(&c, true, plan, sizeof(plan)) == 0 && !plan[0]);
        {
                static const char wan_route[] = "Iface\tDestination\tGateway\nwlan0\t0010A8C0\t00000000\nwlan0\t00000000\t0110A8C0\n";
                static const char lan_only[] = "Iface\tDestination\tGateway\neth0\t0010A8C0\t00000000\n";

                check("installer network: a default route is online, a lan_only one alone is not",
                      installer_route_default(wan_route, sizeof(wan_route) - 1) && !installer_route_default(lan_only, sizeof(lan_only) - 1));
        }
}

// The window's state, with a drive list given it as the verb would have said it.
static fn icheck_window_start(void)
{
        static struct installer_big big;
        static const char said[] = "sda\t500107862016\t0\t1\t0\tSamsung SSD 860\nsdb\t30943995904\t1\t0\t1\tCruzer Blade\nnvme0n1\t1024209543168\t0\t0\t0\tWD Blue SN570\n";

        memory_zero(&installer, sizeof(installer));
        memory_zero(&big, sizeof(big));
        installer.big = &big;
        installer.moonwater = (string_address) "/nonexistent/moonwater";
        installer_begin();
        memory_copy(big.said, said, sizeof(said) - 1);
        big.said_length = sizeof(said) - 1;
        installer_listed();
}

static fn icheck_window(void)
{
        struct window_event key = {WINDOW_EVENT_KEY, WINDOW_KEY_DOWN, UI_KEY_ENTER, 0, 0};

        icheck_window_start();
        check("installer steps: it opens on Language", installer.step == INSTALLER_STEP_LANGUAGE);
        installer_act(1);
        check("installer steps: Next goes to Keyboard", installer.step == INSTALLER_STEP_KEYBOARD);
        installer_act(0);
        check("installer steps: Back goes to Language again", installer.step == INSTALLER_STEP_LANGUAGE);
        installer_pick_language(3); // de
        check("installer language: a language chosen is the choice, changed",
              string_equals((string_address)installer.choice.language, (string_address) "de") && installer.choice.language_changed);
        installer_act(1);
        installer_pick_layout(1); // uk
        check("installer keyboard: a layout chosen is the choice, changed",
              string_equals((string_address)installer.choice.keyboard, (string_address) "uk") && installer.choice.keyboard_changed);
        installer_act(1);
        check("installer steps: Network is passed offline (Skip)", installer.step == INSTALLER_STEP_NETWORK && installer_can_next());
        installer_act(1);
        check("installer disk: the boot drive is not listed", installer.big->drive_count == 2 &&
                                                                 !string_equals((string_address)installer.big->drives[0].name, (string_address) "sdb") &&
                                                                 !string_equals((string_address)installer.big->drives[1].name, (string_address) "sdb"));
        check("installer disk: Next waits for a drive", installer.step == INSTALLER_STEP_DISK && !installer_can_next());
        installer_act(1);
        check("installer disk: and stays", installer.step == INSTALLER_STEP_DISK);
        installer_choose_drive(1); // nvme0n1, no Moonwater
        installer.choice.mode = INSTALLER_UPDATE;
        installer_choose_drive(1);
        check("installer disk: a drive with no Moonwater is overwritten, never updated", installer.choice.mode == INSTALLER_OVERWRITE);
        installer_choose_drive(0); // sda, Moonwater
        installer.choice.mode = INSTALLER_UPDATE;
        check("installer disk: a drive with Moonwater can be updated", string_equals((string_address)installer.choice.drive, (string_address) "sda") &&
                                                                          installer.big->drives[0].moonwater && installer_can_next());
        installer_act(1);
        installer.choice.online = false;
        installer_act(10);
        installer_act(12);
        check("installer apps: offline, a card is no choice", installer.step == INSTALLER_STEP_APPS && installer.choice.profiles == 0);
        installer.choice.online = true;
        installer_act(10);
        installer_act(12);
        check("installer apps: online, Chrome and SSH chosen", installer.choice.profiles == (INSTALLER_CHROME | INSTALLER_SSH));
        installer_act(1);
        check("installer steps: Apps goes to the review", installer.step == INSTALLER_STEP_REVIEW && !installer_can_next());
        check("installer confirm: a key the machine's own keyboard sent is a hand's", installer_hand_of(&key));
        key.flags |= WINDOW_EVENT_VIRTUAL;
        check("installer confirm: one a virtual device sent is not", !installer_hand_of(&key));
        installer.hand = false;
        check("installer confirm: Install from virtual input is refused, and says how it is done",
              !installer_install() && installer.step == INSTALLER_STEP_REVIEW && installer.big->note[0] && installer.child == 0);
        installer.commands = installer_plan(&installer.choice, installer.x86, installer.big->plan, sizeof(installer.big->plan));
        check("installer plan: what the window chose is what it runs: the update, the language, the keyboard, the profiles",
              icheck_has(installer.big->plan, "/moonwater install sda update yes\n/moonwater language de\n/moonwater keyboard uk\n/bowl profile chrome\n") &&
                  icheck_has(installer.big->plan, "/bowl profile ssh\n/moonwater ssh on\n") && !icheck_has(installer.big->plan, "steam"));
}

// The write path on a file: the install's layout, read back as an install's; an image's sum read back equal, and not after a byte changed.
static fn icheck_write_path(void)
{
        string_address disk = (string_address) "/tmp/moonwater-installer-check.img";
        string_address image = (string_address) "/tmp/moonwater-installer-check.efi", copy = (string_address) "/tmp/moonwater-installer-check.copy";
        storage_format_partition parts[2];
        p8 random[80], sums[2][32], block[4096];
        bipolar handle;
        p64 bytes = 3ull << 30;
        bipolar failed;

        handle = system_open_at_mode(AT_FDCWD, disk, FILE_READ_WRITE | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        check("installer write: a file image is made", handle >= 0);
        if (handle < 0)
                return;
        system_call_2(syscall(ftruncate), (positive)handle, (positive)bytes);
        memory_zero(parts, sizeof(parts));
        for (positive at = 0; at < sizeof(random); at++)
                random[at] = (p8)(at * 37 + 11);
        check("installer write: the layout is placed on 3 GiB of 512-byte sectors", host_install_place(bytes, 512, parts));
        failed = host_install_lay(handle, bytes, 512, parts, random);
        check("installer write: the install's layout is written to the file", failed == 0);
        check("installer write: its GPT reads back as an install's", host_install_layout_is(handle, 512));
        storage_format_zero(handle, 512, 512);
        check("installer write: and with the table gone, as none", !host_install_layout_is(handle, 512));
        system_close(handle);
        system_remove_at(AT_FDCWD, disk, 0);

        for (positive at = 0; at < sizeof(block); at++)
                block[at] = (p8)(at * 131 + 7);
        handle = system_open_at_mode(AT_FDCWD, image, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        for (int k = 0; handle >= 0 && k < 64; k++)
                system_write_once((b32)handle, block, sizeof(block));
        if (handle >= 0)
                system_close(handle);
        check("installer verify: the image is copied whole", host_copy_file(image, copy) == 0);
        check("installer verify: the copy, synced and read back, sums as the image",
              host_image_sum(image, true, sums[0]) && host_image_sum(copy, true, sums[1]) && !memory_compare(sums[0], sums[1], 32));
        handle = system_open_at(AT_FDCWD, copy, FILE_READ_WRITE | O_CLOEXEC);
        if (handle >= 0)
        {
                p8 one = 0x5a;

                storage_format_write(handle, &one, 1, 100000);
                system_close(handle);
        }
        check("installer verify: one byte changed does not", host_image_sum(copy, true, sums[1]) && memory_compare(sums[0], sums[1], 32));
        system_remove_at(AT_FDCWD, image, 0);
        system_remove_at(AT_FDCWD, copy, 0);
}

/* ---- the pictures ---- */

static unsigned int icheck_pixels[ICHECK_W * ICHECK_H], icheck_scratch[1400 * UI_SCRATCH_ROWS];
static p8 icheck_atlas[1024 * 1024];
static font_face icheck_faces[2];
static unsigned int icheck_ground = 0xc7d0dcu; // under the glass: the light desktop's tone; the dark one's for the dark pictures

static fn icheck_ppm(string_address dir, string_address name)
{
        p8 path[256], head[64];
        static p8 row[3 * ICHECK_W];
        positive length;
        bipolar handle;

        string_copy_bounded(path, dir, sizeof(path));
        string_append_bounded(path, (string_address) "/", sizeof(path));
        string_append_bounded(path, name, sizeof(path));
        handle = system_open_at_mode(AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (handle < 0)
                return;
        string_copy(head, (string_address) "P6\n");
        length = 3;
        length += positive_into_string(head + length, ICHECK_W);
        head[length++] = ' ';
        length += positive_into_string(head + length, ICHECK_H);
        string_copy(head + length, (string_address) "\n255\n");
        length += 5;
        system_write_once((b32)handle, head, length);
        for (int y = 0; y < ICHECK_H; y++)
        {
                for (int x = 0; x < ICHECK_W; x++)
                {
                        // The window is glass: its premultiplied picture over a ground like the desktop's, as the compositor lays it.
                        unsigned int pixel = icheck_pixels[y * ICHECK_W + x], alpha = pixel >> 24, rest = 255 - alpha;

                        for (int c = 0; c < 3; c++)
                        {
                                unsigned int under = (icheck_ground >> (16 - 8 * c)) & 255, over = (pixel >> (16 - 8 * c)) & 255;
                                unsigned int mixed = over + (under * rest + 127) / 255;

                                row[x * 3 + c] = (p8)(mixed > 255 ? 255 : mixed);
                        }
                }
                system_write_once((b32)handle, row, sizeof(row));
        }
        system_close(handle);
}

static fn icheck_draw(string_address dir, string_address name)
{
        // The kit's motion run to rest: a frame a second apart, three times.
        for (int k = 0; k < 3; k++)
        {
                ui_kit.now += 1000000000ll;
                ui_kit.busy = 0;
                ui_kit_turn();
                installer_draw(icheck_pixels);
        }
        icheck_ppm(dir, name);
}

static fn icheck_pictures(string_address regular, string_address bold, string_address dir)
{
        positive size = 0, bytes;
        long engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x4022, (positive)-1, 0);
        b32 loaded = true;

        installer.ui.engine = (font_engine address_to)engine;
        font_start(installer.ui.engine, icheck_atlas, 1024, 1024);
        for (int k = 0; k < 2; k++)
        {
                bytes = compositor_map_file(k ? bold : regular, &size);
                loaded = loaded && bytes && font_load(&icheck_faces[k], (const p8 address_to)bytes, size, 0) == 0;
        }
        check("installer pictures: the faces load", loaded);
        if (!loaded)
                return;
        icheck_window_start();
        installer.ui.engine = (font_engine address_to)engine;
        installer.ui.regular = &icheck_faces[0];
        installer.ui.bold = &icheck_faces[1];
        installer.ui.scratch = icheck_scratch;
        installer.ui.scratch_width = 1400;
        installer.ui.scale = 100;
        installer.width = ICHECK_W, installer.height = ICHECK_H;
        installer.focused = true;
        installer.x86 = true;
        ui_kit_theme(UI_THEME_LIGHT, UI_ACCENT_DAWN, 0);
        ui_kit.now = 1000000000ll;

        icheck_draw(dir, (string_address) "1-language.ppm");
        installer_act(1);
        icheck_draw(dir, (string_address) "2-keyboard.ppm");
        installer_act(1);
        icheck_draw(dir, (string_address) "3-network-offline.ppm");
        installer.choice.online = true;
        icheck_draw(dir, (string_address) "3-network-online.ppm");
        installer_act(1);
        icheck_draw(dir, (string_address) "4-disk-none-chosen.ppm");
        installer_choose_drive(0);
        installer.choice.mode = INSTALLER_UPDATE;
        icheck_draw(dir, (string_address) "4-disk-update.ppm");
        installer_act(1);
        installer.choice.online = false;
        icheck_draw(dir, (string_address) "5-apps-offline.ppm");
        installer.choice.online = true;
        installer_act(10);
        installer_act(12);
        icheck_draw(dir, (string_address) "5-apps-online.ppm");
        installer_act(1);
        icheck_draw(dir, (string_address) "6-review-update.ppm");
        installer_act(0);
        installer_act(0);
        installer_choose_drive(1);
        installer_act(1);
        installer_act(1);
        icheck_draw(dir, (string_address) "6-review-overwrite.ppm");
        installer.step = INSTALLER_STEP_RUN, installer.commands = 4, installer.at = 1;
        string_copy((string_address)installer.big->last, (string_address) "writing this build to nvme0n1p1, from sdb1");
        icheck_draw(dir, (string_address) "7-progress.ppm");
        installer.step = INSTALLER_STEP_DONE;
        icheck_draw(dir, (string_address) "8-done.ppm");
        ui_kit_theme(UI_THEME_DARK, UI_ACCENT_DAWN, 0);
        icheck_ground = 0x1a2030u;
        installer.step = INSTALLER_STEP_APPS;
        icheck_draw(dir, (string_address) "5-apps-online-dark.ppm");
}

b32 main()
{
        icheck_listing();
        icheck_plan();
        icheck_window();
        icheck_write_path();
        if (program_argument_count() > 3)
                icheck_pictures(program_argument(1), program_argument(2), program_argument(3));
        return test_report(null);
}
#endif
