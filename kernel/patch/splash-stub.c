// SPDX-License-Identifier: GPL-2.0
/*
 * Moonwater's boot splash, drawn by the EFI stub before the kernel starts.
 *
 * kernel/patch/apply copies this file into drivers/firmware/efi/libstub as
 * moonwater-splash.c, adds its object to the stub's list and calls
 * efi_moonwater_splash() from the stub's entry, before the kernel is
 * decompressed on x86 and as soon as the display is known on arm64 and
 * riscv64. The format, its parser and the placing are src/moonwater/splash.c,
 * which the shell and the checks build as well; this file is only the
 * firmware calls round them.
 *
 * The picture is \EFI\moonwater\splash.ppm on the volume the image was loaded
 * from. It is read through the firmware's own file protocol, the one the stub
 * reads initrd= files with, checked whole, and drawn with the Graphics Output
 * Protocol's block transfer on the display the kernel is handed: the screen is
 * made black and the picture put in the middle of it. Nothing is cleared after
 * that by this stub, so it stays until something in the kernel or the desktop
 * sets a mode or draws.
 *
 * Every failure is silence. No file, a volume with no file system protocol, a
 * file that is too long or does not parse, no display, no memory: the screen
 * is left as the firmware had it and the stub goes on. Nothing here writes to
 * a disk or to a variable, and nothing here can return an error to the entry
 * that called it. Firmware entered in mixed mode (32-bit firmware under a
 * 64-bit kernel) is not drawn on: the block transfer has no thunk.
 */

#include <linux/efi.h>
#include <asm/efi.h>

#include "efistub.h"

#include "../../../../kernel/moonwater/splash.c"

#define MOONWATER_SPLASH_FILL	0	/* EfiBltVideoFill */
#define MOONWATER_SPLASH_DRAW	2	/* EfiBltBufferToVideo */
#define MOONWATER_SPLASH_CHUNK	0x10000	/* a read, as file.c reads in pieces */

typedef efi_status_t (__efiapi *moonwater_splash_blt_t)(
				efi_graphics_output_protocol_t *, void *pixels,
				int operation,
				unsigned long source_x, unsigned long source_y,
				unsigned long x, unsigned long y,
				unsigned long width, unsigned long height,
				unsigned long delta);

/*
 * The display the kernel is handed, as gop.c picks it: the first with a
 * console on it, else the first there is, and never one that only transfers
 * blocks, which the kernel is not handed at all.
 */
static efi_graphics_output_protocol_t *moonwater_splash_display(void)
{
	efi_guid_t gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	efi_guid_t console_guid = EFI_CONSOLE_OUT_DEVICE_GUID;
	efi_graphics_output_protocol_t *first = NULL;
	efi_handle_t *handles = NULL;
	unsigned long count = 0;
	unsigned long at;

	if (efi_bs_call(locate_handle_buffer, EFI_LOCATE_BY_PROTOCOL, &gop_guid,
			NULL, &count, &handles) != EFI_SUCCESS || !handles)
		return NULL;

	for (at = 0; at < count; at++) {
		efi_graphics_output_protocol_t *gop = NULL;
		efi_graphics_output_protocol_mode_t *mode;
		efi_graphics_output_mode_info_t *info;
		void *console = NULL;

		if (efi_bs_call(handle_protocol, handles[at], &gop_guid,
				(void **)&gop) != EFI_SUCCESS || !gop)
			continue;

		mode = efi_table_attr(gop, mode);
		info = mode ? efi_table_attr(mode, info) : NULL;
		if (!info || info->pixel_format == PIXEL_BLT_ONLY ||
		    info->pixel_format >= PIXEL_FORMAT_MAX)
			continue;

		if (efi_bs_call(handle_protocol, handles[at], &console_guid,
				&console) == EFI_SUCCESS) {
			first = gop;
			break;
		}
		if (!first)
			first = gop;
	}

	efi_bs_call(free_pool, handles);
	return first;
}

/*
 * The file's bytes into room bytes: how many there were, and room when there
 * were that many or more, which is a file too long to be a picture. 0 when
 * there is no file, or it could not be read.
 */
static unsigned long moonwater_splash_read(efi_handle_t handle, u8 *into,
					   unsigned long room)
{
	static const efi_char16_t name[] = L"\\EFI\\moonwater\\splash.ppm";
	efi_guid_t image_guid = LOADED_IMAGE_PROTOCOL_GUID;
	efi_guid_t fs_guid = EFI_FILE_SYSTEM_GUID;
	efi_simple_file_system_protocol_t *fs = NULL;
	efi_loaded_image_t *image = NULL;
	efi_file_protocol_t *volume = NULL;
	efi_file_protocol_t *file = NULL;
	unsigned long length = 0;
	efi_handle_t device;

	if (efi_bs_call(handle_protocol, handle, &image_guid,
			(void **)&image) != EFI_SUCCESS || !image)
		return 0;

	device = efi_table_attr(image, device_handle);
	if (!device ||
	    efi_bs_call(handle_protocol, device, &fs_guid,
			(void **)&fs) != EFI_SUCCESS || !fs)
		return 0;

	if (efi_call_proto(fs, open_volume, &volume) != EFI_SUCCESS || !volume)
		return 0;

	if (efi_call_proto(volume, open, &file, (efi_char16_t *)name,
			   EFI_FILE_MODE_READ, 0) == EFI_SUCCESS && file) {
		while (length < room) {
			unsigned long got = room - length;

			if (got > MOONWATER_SPLASH_CHUNK)
				got = MOONWATER_SPLASH_CHUNK;
			if (efi_call_proto(file, read, &got,
					   into + length) != EFI_SUCCESS ||
			    got > room - length) {
				length = 0;
				break;
			}
			if (!got)
				break;
			length += got;
		}
		efi_call_proto(file, close);
	}

	efi_call_proto(volume, close);
	return length;
}

void efi_moonwater_splash(efi_handle_t handle)
{
	efi_graphics_output_protocol_mode_t *mode;
	efi_graphics_output_mode_info_t *info;
	efi_graphics_output_protocol_t *gop;
	moonwater_splash_blt_t blt;
	struct splash_image image;
	struct splash_place place;
	unsigned long length;
	u8 black[4] = { 0, 0, 0, 0 };
	u8 *pixels = NULL;
	u8 *bytes = NULL;

	if (!efi_is_native())
		return;

	gop = moonwater_splash_display();
	if (!gop || !gop->blt)
		return;

	mode = efi_table_attr(gop, mode);
	info = efi_table_attr(mode, info);
	blt = (moonwater_splash_blt_t)gop->blt;

	if (efi_bs_call(allocate_pool, EFI_LOADER_DATA, SPLASH_FILE_MOST + 1,
			(void **)&bytes) != EFI_SUCCESS || !bytes)
		return;

	length = moonwater_splash_read(handle, bytes, SPLASH_FILE_MOST + 1);
	if (!length || splash_parse(bytes, length, &image))
		goto done;

	splash_place(&image, info->horizontal_resolution,
		     info->vertical_resolution, &place);
	if (!place.width || !place.height)
		goto done;

	if (efi_bs_call(allocate_pool, EFI_LOADER_DATA,
			(unsigned long)image.width * image.height * 4,
			(void **)&pixels) != EFI_SUCCESS || !pixels)
		goto done;

	splash_pixels(&image, pixels);

	blt(gop, black, MOONWATER_SPLASH_FILL, 0, 0, 0, 0,
	    info->horizontal_resolution, info->vertical_resolution, 0);
	blt(gop, pixels, MOONWATER_SPLASH_DRAW, place.source_x, place.source_y,
	    place.x, place.y, place.width, place.height,
	    (unsigned long)image.width * 4);

	efi_bs_call(free_pool, pixels);
done:
	efi_bs_call(free_pool, bytes);
}
