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
#define ICHECK_W 900
#define ICHECK_H 660

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
        c.mode = INSTALLER_OVERWRITE, c.bytes = 1000;
        c.profiles = INSTALLER_CHROME | INSTALLER_STEAM | INSTALLER_SSH;
        count = installer_plan(&c, true, plan, sizeof(plan));
        check("installer plan: offline, the install alone: no profile, and nothing the person did not change",
              count == 1 && string_equals((string_address)plan, (string_address) "/moonwater install sda overwrite yes 1000\n"));
        c.online = true, c.language_changed = true, c.mode = INSTALLER_UPDATE;
        count = installer_plan(&c, true, plan, sizeof(plan));
        check("installer plan: an update's words, the language changed reaching it and the keyboard not",
              icheck_has(plan, "/moonwater install sda update yes 1000\n") && icheck_has(plan, "/moonwater language de\n") &&
                  !icheck_has(plan, "keyboard"));
        check("installer plan: online, every profile chosen, SSH turned on after its profile",
              count == 6 && icheck_has(plan, "/bowl profile chrome\n") && icheck_has(plan, "/bowl profile steam\n") &&
                  icheck_has(plan, "/bowl profile ssh\n/moonwater ssh on\n"));
        c.keyboard_changed = true;
        count = installer_plan(&c, false, plan, sizeof(plan));
        check("installer plan: the keyboard changed reaches it; Steam is not run off x86-64",
              icheck_has(plan, "/moonwater keyboard de\n") && !icheck_has(plan, "steam") && count == 6);
        c.mode = INSTALLER_USE;
        installer_plan(&c, false, plan, sizeof(plan));
        check("installer plan: Use is the verb's use, the third answer of the terminal's question",
              icheck_has(plan, "/moonwater install sda use yes 1000\n") && installer_mode_at(2) == INSTALLER_USE && installer_mode_at(1) == INSTALLER_UPDATE &&
                  installer_mode_at(0) == INSTALLER_OVERWRITE);
        check("installer skipped: mapped and RAID volumes and eMMC boot areas are no drive; a card's whole device is",
              installer_skipped("dm-0") && installer_skipped("md127") && installer_skipped("mmcblk0boot0") && installer_skipped("sr0") &&
                  !installer_skipped("mmcblk0") && !installer_skipped("sda") && !installer_skipped("nvme0n1"));
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
        // Online: Network shows done among the steps, live, and Next from Keyboard goes past it; a press on it still goes to it; offline again
        // it is a step to come and in the way.
        installer.choice.online = true;
        check("installer steps: online, Network shows done while another step is shown, and is not done while it is shown",
              !installer_step_done(INSTALLER_STEP_NETWORK) && (installer_act(0), installer.step == INSTALLER_STEP_KEYBOARD) &&
                  installer_step_done(INSTALLER_STEP_NETWORK) && !installer_step_done(INSTALLER_STEP_DISK));
        installer_act(1);
        check("installer steps: online, Next from Keyboard goes past Network to Disk", installer.step == INSTALLER_STEP_DISK);
        check("installer steps: online, a press on Network goes to it from Disk; a step to come is not pressed",
              installer_step_reachable(INSTALLER_STEP_NETWORK) && installer_step_reachable(INSTALLER_STEP_LANGUAGE) && !installer_step_reachable(INSTALLER_STEP_APPS) &&
                  !installer_step_reachable(INSTALLER_STEP_DISK));
        installer.choice.online = false, installer.network_seen = false;
        check("installer steps: the link gone, Network is no longer done, and Next from Keyboard stops on it",
              !installer_step_done(INSTALLER_STEP_NETWORK) &&
                  (installer_go(INSTALLER_STEP_KEYBOARD), installer_act(1), installer.step == INSTALLER_STEP_NETWORK));
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
              icheck_has(installer.big->plan, "/moonwater install sda update yes ") &&
                  icheck_has(installer.big->plan, "\n/moonwater language de\n/moonwater keyboard uk\n/bowl profile chrome\n") &&
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
/*
        Each step as the desktop shows it: the moon wallpaper (ui_moon_night, the light theme's paper or the dark one's with its stars),
        the window's glass over it as the compositor makes it (what is under the window blurred, then tinted toward the theme's side by
        compositor_glass_tint, whose mean is the ground the window's inks are worked out from, ui_inks), the window's picture laid over
        that, its corners rounded and a hairline round it, and a soft shadow under it. The grain the compositor lays over glass is left
        out. Written as PPMs, light and dark for every step.
*/
#define ICHECK_SCREEN_W 1280
#define ICHECK_SCREEN_H 820
#define ICHECK_X ((ICHECK_SCREEN_W - ICHECK_W) / 2)
#define ICHECK_Y ((ICHECK_SCREEN_H - ICHECK_H) / 2)
#define ICHECK_RADIUS 16

static unsigned int icheck_pixels[ICHECK_W * ICHECK_H], icheck_scratch[1400 * UI_SCRATCH_ROWS];
static unsigned int icheck_wall[2][ICHECK_SCREEN_W * ICHECK_SCREEN_H], icheck_glass[2][ICHECK_W * ICHECK_H], icheck_ground[2];
static unsigned int icheck_blur[ICHECK_SCREEN_W * ICHECK_SCREEN_H];
static p8 icheck_atlas[1024 * 1024];
static font_face icheck_faces[2];
static int icheck_dark;

// A box blur of a picture's channels, radius r, across then down, in place.
static fn icheck_box_blur(unsigned int address_to p, int w, int h, int r)
{
        static unsigned int line[ICHECK_SCREEN_W > ICHECK_SCREEN_H ? ICHECK_SCREEN_W : ICHECK_SCREEN_H];

        for (int pass = 0; pass < 2; pass++)
        {
                int outer = pass ? w : h, inner = pass ? h : w;

                for (int o = 0; o < outer; o++)
                {
                        unsigned int sum[3] = {0, 0, 0};

                        for (int i = -r; i <= r; i++)
                        {
                                int at = i < 0 ? 0 : i >= inner ? inner - 1 : i;
                                unsigned int v = pass ? p[at * w + o] : p[o * w + at];

                                for (int c = 0; c < 3; c++)
                                        sum[c] += v >> (16 - 8 * c) & 255;
                        }
                        for (int i = 0; i < inner; i++)
                        {
                                int add = i + r + 1 >= inner ? inner - 1 : i + r + 1, drop = i - r < 0 ? 0 : i - r;
                                unsigned int a = pass ? p[add * w + o] : p[o * w + add], d = pass ? p[drop * w + o] : p[o * w + drop];

                                line[i] = 0xff000000u | (sum[0] / (2 * r + 1)) << 16 | (sum[1] / (2 * r + 1)) << 8 | sum[2] / (2 * r + 1);
                                for (int c = 0; c < 3; c++)
                                        sum[c] += (a >> (16 - 8 * c) & 255) - (d >> (16 - 8 * c) & 255);
                        }
                        for (int i = 0; i < inner; i++)
                                if (pass)
                                        p[i * w + o] = line[i];
                                else
                                        p[o * w + i] = line[i];
                }
        }
}

// The desktop and the glass under the window, for one theme: the wallpaper, and the window's rectangle of it blurred and tinted.
static fn icheck_backdrop(int dark)
{
        struct ui wall;
        unsigned long sums[3] = {0, 0, 0};

        memory_zero(&wall, sizeof(wall));
        wall.pixels = icheck_wall[dark], wall.pitch = ICHECK_SCREEN_W, wall.width = ICHECK_SCREEN_W, wall.height = ICHECK_SCREEN_H;
        wall.bottom = ICHECK_SCREEN_H, wall.scale = 100;
        ui_moon_night(&wall, 0, dark ? UI_MOON_PAPER_DARK | UI_MOON_PAPER_STARS : 0, UI_ACCENT_DAWN, 1, 0);
        memory_copy(icheck_blur, icheck_wall[dark], sizeof(icheck_blur));
        for (int k = 0; k < 3; k++)
                icheck_box_blur(icheck_blur, ICHECK_SCREEN_W, ICHECK_SCREEN_H, 18);
        for (int y = 0; y < ICHECK_H; y++)
                for (int x = 0; x < ICHECK_W; x++)
                {
                        unsigned int under = icheck_blur[(y + ICHECK_Y) * ICHECK_SCREEN_W + x + ICHECK_X];

                        icheck_glass[dark][y * ICHECK_W + x] = compositor_glass_tint(under & 0xffffffu, dark ? 0 : 1);
                        sums[0] += under >> 16 & 255, sums[1] += under >> 8 & 255, sums[2] += under & 255;
                }
        icheck_ground[dark] = compositor_glass_tint((unsigned int)(sums[0] / (ICHECK_W * ICHECK_H) << 16 | sums[1] / (ICHECK_W * ICHECK_H) << 8 |
                                                                   sums[2] / (ICHECK_W * ICHECK_H)),
                                                    dark ? 0 : 1);
}

// Whether a point of the window is inside its rounded corners, and how much (0 to 255) at the edge.
static unsigned int icheck_inside(int x, int y)
{
        int cx = x < ICHECK_RADIUS ? ICHECK_RADIUS : x >= ICHECK_W - ICHECK_RADIUS ? ICHECK_W - ICHECK_RADIUS - 1 : x;
        int cy = y < ICHECK_RADIUS ? ICHECK_RADIUS : y >= ICHECK_H - ICHECK_RADIUS ? ICHECK_H - ICHECK_RADIUS - 1 : y;
        int dx = (x - cx) * 16, dy = (y - cy) * 16, d2 = dx * dx + dy * dy, r = ICHECK_RADIUS * 16;

        if (d2 <= (r - 8) * (r - 8))
                return 255;
        if (d2 >= (r + 8) * (r + 8))
                return 0;
        // A pixel's width of edge, linear in the distance.
        for (int k = 0; k < 16; k++)
                if (d2 <= (r - 8 + k) * (r - 8 + k))
                        return (unsigned int)(255 - k * 16);
        return 0;
}

static unsigned int icheck_over(unsigned int under, unsigned int over, unsigned int alpha)
{
        unsigned int out = 0;

        for (int c = 0; c < 3; c++)
        {
                unsigned int u = under >> (16 - 8 * c) & 255, o = over >> (16 - 8 * c) & 255;

                out |= ((o * alpha + u * (255 - alpha) + 127) / 255) << (16 - 8 * c);
        }
        return out;
}

static fn icheck_ppm(string_address dir, string_address name)
{
        p8 path[256], head[64];
        static p8 row[3 * ICHECK_SCREEN_W];
        positive length;
        bipolar handle;
        int dark = icheck_dark;

        string_copy_bounded(path, dir, sizeof(path));
        string_append_bounded(path, (string_address) "/", sizeof(path));
        string_append_bounded(path, name, sizeof(path));
        string_append_bounded(path, (string_address)(dark ? "-dark.ppm" : "-light.ppm"), sizeof(path));
        handle = system_open_at_mode(AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (handle < 0)
                return;
        string_copy(head, (string_address) "P6\n");
        length = 3;
        length += positive_into_string(head + length, ICHECK_SCREEN_W);
        head[length++] = ' ';
        length += positive_into_string(head + length, ICHECK_SCREEN_H);
        string_copy(head + length, (string_address) "\n255\n");
        length += 5;
        system_write_once((b32)handle, head, length);
        for (int y = 0; y < ICHECK_SCREEN_H; y++)
        {
                for (int x = 0; x < ICHECK_SCREEN_W; x++)
                {
                        unsigned int pixel = icheck_wall[dark][y * ICHECK_SCREEN_W + x];
                        int wx = x - ICHECK_X, wy = y - ICHECK_Y;

                        // The shadow: a dark veil that fades over 24 pixels out from the window, a little lower than it.
                        {
                                // How far from the window's rounded outline, moved 8 down: the corners' arcs, not a square's.
                                int sy = wy - 8, cx = wx < ICHECK_RADIUS ? ICHECK_RADIUS : wx >= ICHECK_W - ICHECK_RADIUS ? ICHECK_W - ICHECK_RADIUS - 1 : wx;
                                int cy = sy < ICHECK_RADIUS ? ICHECK_RADIUS : sy >= ICHECK_H - ICHECK_RADIUS ? ICHECK_H - ICHECK_RADIUS - 1 : sy;
                                int dx = wx - cx, dy = sy - cy, far = 0;

                                while ((far + ICHECK_RADIUS) * (far + ICHECK_RADIUS) < dx * dx + dy * dy && far < 24)
                                        far++;

                                if (far < 24)
                                        pixel = icheck_over(pixel, 0, (unsigned int)((24 - far) * (dark ? 5 : 3)));
                        }
                        if (wx >= 0 && wy >= 0 && wx < ICHECK_W && wy < ICHECK_H)
                        {
                                unsigned int inside = icheck_inside(wx, wy), picture = icheck_pixels[wy * ICHECK_W + wx], alpha = picture >> 24;
                                unsigned int glass = icheck_glass[dark][wy * ICHECK_W + wx], laid = 0;

                                // The picture is premultiplied: its colour plus what of the glass its alpha leaves.
                                for (int c = 0; c < 3; c++)
                                {
                                        unsigned int g = glass >> (16 - 8 * c) & 255, o = picture >> (16 - 8 * c) & 255, v = o + (g * (255 - alpha) + 127) / 255;

                                        laid |= (v > 255 ? 255 : v) << (16 - 8 * c);
                                }
                                // The frame's hairline just inside the corners, as the compositor gives a floating window.
                                if (inside && inside < 255)
                                        laid = icheck_over(laid, dark ? 0x5a5a62u : 0xb4b4bcu, 160);
                                pixel = icheck_over(pixel, laid, inside);
                        }
                        row[x * 3] = (p8)(pixel >> 16), row[x * 3 + 1] = (p8)(pixel >> 8), row[x * 3 + 2] = (p8)pixel;
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

// The networks the Network step lists, as `moonwater wifi` says them.
static const char icheck_wifi_said[] = "[Moonwater] wifi on\n"
                                       "[Moonwater] * Moonbase            -48 dBm ####  wpa3    5 GHz ch 36\n"
                                       "[Moonwater] + Tranquility         -61 dBm ###.  wpa2    2.4 GHz ch 6\n"
                                       "[Moonwater]   Library guest       -72 dBm ##..  open    2.4 GHz ch 11\n"
                                       "[Moonwater]   Apollo 5G           -80 dBm #...  wpa2    5 GHz ch 149\n";

// Every step, in one theme.
static fn icheck_steps(string_address dir, int dark)
{
        struct installer_big address_to big;

        icheck_dark = dark;
        big = installer.big;
        // The window is the kit's surface with its steps a glass pane (ui_app.side), as installer_app asks.
        installer.ui.glass = 0, installer.ui.opaque = 1, installer.ui.sided = 1, installer.ui.side_ground = icheck_ground[dark];
        installer.width = ICHECK_W, installer.height = ICHECK_H;
        installer.focused = true;
        installer.x86 = true;
        ui_kit_theme(dark ? UI_THEME_DARK : UI_THEME_LIGHT, UI_ACCENT_DAWN, 0);
        ui_kit_inks(&installer.ui);

        icheck_draw(dir, (string_address) "1-language");
        // The same window reading from the right: the steps' glass pane at the right edge, the circles at its right.
        ui_kit.rtl = 1;
        icheck_draw(dir, (string_address) "1-language-rtl");
        ui_kit.rtl = 0;
        installer_act(1);
        icheck_draw(dir, (string_address) "2-keyboard");
        installer_act(1);
        installer.next_look = installer.next_wifi = 1ll << 62; // the tick is not run here: the networks are the fixture's
        memory_copy(big->said, icheck_wifi_said, sizeof(icheck_wifi_said) - 1);
        big->said_length = sizeof(icheck_wifi_said) - 1;
        installer_heard();
        installer.choice.online = false;
        icheck_draw(dir, (string_address) "3-network-offline");
        installer_pick_network(3);
        icheck_draw(dir, (string_address) "3-network-password");
        big->net_chosen = -1;
        installer.choice.online = true;
        icheck_draw(dir, (string_address) "3-network-online");
        installer_act(1);
        icheck_draw(dir, (string_address) "4-disk-none-chosen");
        installer_choose_drive(0);
        installer.choice.mode = INSTALLER_UPDATE;
        icheck_draw(dir, (string_address) "4-disk-update");
        installer.choice.mode = INSTALLER_USE;
        icheck_draw(dir, (string_address) "4-disk-use");
        installer.choice.mode = INSTALLER_UPDATE;
        installer_act(1);
        installer.choice.online = false;
        icheck_draw(dir, (string_address) "5-apps-offline");
        installer.choice.online = true;
        icheck_draw(dir, (string_address) "5-apps-none");
        installer_act(10);
        installer_act(12);
        big->hover_card = 1, big->focus_card = -1;
        icheck_draw(dir, (string_address) "5-apps-chosen");
        big->hover_card = -1;
        installer_act(1);
        icheck_draw(dir, (string_address) "6-review-update");
        installer_act(0);
        installer_act(0);
        installer_choose_drive(1);
        installer_act(1);
        installer_act(1);
        icheck_draw(dir, (string_address) "6-review-overwrite");
        installer.step = INSTALLER_STEP_RUN, installer.commands = 4, installer.at = 1;
        string_copy((string_address)big->last, (string_address) "writing this build to nvme0n1p1, from sdb1");
        icheck_draw(dir, (string_address) "7-progress");
        installer.step = INSTALLER_STEP_DONE;
        icheck_draw(dir, (string_address) "8-done");
}

static fn icheck_pictures(string_address regular, string_address bold, string_address dir)
{
        positive size = 0, bytes;
        long engine = compositor_call(syscall(mmap), 0, sizeof(font_engine), 3, 0x4022, (positive)-1, 0);
        b32 loaded = true;

        font_start((font_engine address_to)engine, icheck_atlas, 1024, 1024);
        for (int k = 0; k < 2; k++)
        {
                bytes = compositor_map_file(k ? bold : regular, &size);
                loaded = loaded && bytes && font_load(&icheck_faces[k], (const p8 address_to)bytes, size, 0) == 0;
        }
        check("installer pictures: the faces load", loaded);
        if (!loaded)
                return;
        icheck_backdrop(0);
        icheck_backdrop(1);
        for (int dark = 0; dark < 2; dark++)
        {
                memory_zero(&installer.ui, sizeof(installer.ui));
                installer.ui.engine = (font_engine address_to)engine;
                installer.ui.regular = &icheck_faces[0];
                installer.ui.bold = &icheck_faces[1];
                installer.ui.scratch = icheck_scratch;
                installer.ui.scratch_width = 1400;
                installer.ui.scale = 100;
                ui_kit.now = 1000000000ll;
                {
                        struct ui saved = installer.ui;

                        icheck_window_start(); // zeroes the state; the ui is put back
                        installer.ui = saved;
                }
                icheck_steps(dir, dark);
        }
}

// The cards' own hit target: a press and its release on a card choose it, a press that leaves it does not; the Wi-Fi list read.
static fn icheck_cards(void)
{
        struct installer_big address_to big = installer.big;
        struct window_event down = {WINDOW_EVENT_BUTTON, WINDOW_KEY_DOWN, UI_POINTER_LEFT, 0, 0}, up = {WINDOW_EVENT_BUTTON, 0, UI_POINTER_LEFT, 0, 0};
        b32 on;
        struct installer_network nets[INSTALLER_NETWORKS];
        int count;

        installer.step = INSTALLER_STEP_APPS, installer.choice.online = true, installer.choice.profiles = 0;
        installer_draw(icheck_pixels);
        check("installer cards: each card has its box", big->card_box[0].w > 0 && big->card_box[2].h >= INSTALLER_CARD_H - 1);
        ui_kit.pointer_x = big->card_box[2].x + 20, ui_kit.pointer_y = big->card_box[2].y + big->card_box[2].h / 2;
        installer_cards_event(&down);
        installer_cards_event(&up);
        on = (installer.choice.profiles & INSTALLER_SSH) != 0;
        check("installer cards: a press and its release anywhere on a card choose it", on);
        installer_cards_event(&down);
        ui_kit.pointer_x = 1, ui_kit.pointer_y = 1;
        installer_cards_event(&up);
        check("installer cards: a press let go off the card leaves it as it was", (installer.choice.profiles & INSTALLER_SSH) != 0);
        installer.choice.online = false;
        ui_kit.pointer_x = big->card_box[0].x + 20, ui_kit.pointer_y = big->card_box[0].y + 20;
        installer_cards_event(&down);
        installer_cards_event(&up);
        check("installer cards: offline, a press on a card chooses nothing", !(installer.choice.profiles & INSTALLER_CHROME));

        count = installer_wifi_parse(icheck_wifi_said, sizeof(icheck_wifi_said) - 1, nets, INSTALLER_NETWORKS, &on);
        check("installer wifi: the networks Settings' list reads are read, the label passed over", count == 4 && on &&
                                                                                                     string_equals((string_address)nets[0].name, (string_address) "Moonbase") &&
                                                                                                     nets[0].joined && nets[0].bars == 4 &&
                                                                                                     string_equals((string_address)nets[3].name, (string_address) "Apollo 5G"));
        check("installer wifi: saved, open and the bars", nets[1].saved && !nets[1].joined && nets[2].open && !nets[2].saved && nets[2].bars == 2);
        count = installer_wifi_parse("[Moonwater] wifi off\n", 21, nets, INSTALLER_NETWORKS, &on);
        check("installer wifi: a radio that is off says so", !count && !on);
}

b32 main()
{
        icheck_listing();
        icheck_plan();
        icheck_window();
        icheck_write_path();
        if (program_argument_count() > 3)
        {
                icheck_pictures(program_argument(1), program_argument(2), program_argument(3));
                icheck_cards();
        }
        return test_report(null);
}
#endif
