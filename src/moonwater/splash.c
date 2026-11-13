/*
        The boot splash: the picture Moonwater's loader draws before the kernel starts, the file that keeps
        it, and where on a screen it goes.

        The picture is a file beside the boot image on the system partition, SPLASH_FILE under the
        partition's root. moonwater splash (src/sh/host.c) checks a picture with splash_parse and writes it
        there; the EFI stub (kernel/patch/splash-stub.c, which kernel/patch/apply puts in the kernel's
        libstub) reads it from the partition the image was loaded from, checks it with the same parser,
        and draws it with the firmware's Graphics Output Protocol. So one parser says what a usable file
        is, in the shell, in the loader and in the checks (CHECK_storage_format), and this file includes
        nothing and calls nothing: it builds in all three.

        Nothing draws a picture that does not check. A file that is missing, cut short, damaged, longer
        than SPLASH_FILE_MOST or of another shape is no picture, the screen is left as the firmware had it,
        and the machine starts as it does with no file there.

        A file is a binary PPM (P6), the one format that needs no decoder: the magic P6, then width,
        height and maxval as decimal numbers with white space and # comments between them, one white
        space byte, and width x height x 3 bytes of RGB, rows from the top. maxval must be 255. The whole
        file is that and nothing after it: a file that is longer, shorter or says any other size is
        refused, with the reason.

        The most a file may be is SPLASH_FILE_MOST bytes, and each side is at most SPLASH_SIDE_MOST
        pixels. The picture is drawn at its own size, a pixel of it a pixel of the screen, centred, on
        black; what of it a smaller screen has no room for is cut off evenly at both sides. It is never
        scaled.
*/
#ifndef MOONWATER_SPLASH_INCLUDED
#define MOONWATER_SPLASH_INCLUDED
#define SPLASH_DIRECTORY "/EFI/moonwater"           // beside the image kept for a rollback
#define SPLASH_FILE SPLASH_DIRECTORY "/splash.ppm"  // the picture the loader draws; absent is no picture
#define SPLASH_FILE_NEXT SPLASH_DIRECTORY "/splash.new" // what a write makes beside it, and renames over it
#define SPLASH_FILE_MOST (1u << 20)                 // the most bytes a splash file may be: 1 MiB
#define SPLASH_SIDE_MOST 1024                       // the most pixels across, and the most down
#define SPLASH_HEADER_MOST 32                       // room for the header splash_header writes

struct splash_image
{
        unsigned int width, height;
        const unsigned char *rgb; // width x height x 3 bytes, in the file
};

// Where a picture goes on a screen: the part of it drawn, and the screen pixel its first one lands on.
struct splash_place
{
        unsigned int source_x, source_y; // the first pixel of the picture that is drawn
        unsigned int x, y;               // where on the screen it lands
        unsigned int width, height;      // how much is drawn; 0 by 0 on a screen with no pixels
};

static inline int splash_space(unsigned char byte)
{
        return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\v' || byte == '\f';
}

// The next decimal number of the header, with the white space and comments before it. 0 when there is none, or it is too large.
static inline int splash_field(const unsigned char *bytes, unsigned long length, unsigned long *at, unsigned long *into)
{
        unsigned long value = 0;
        unsigned long digits = 0;

        while (*at < length && (splash_space(bytes[*at]) || bytes[*at] == '#'))
        {
                if (bytes[*at] == '#')
                        while (*at < length && bytes[*at] != '\n')
                                (*at)++;
                else
                        (*at)++;
        }
        while (*at < length && bytes[*at] >= '0' && bytes[*at] <= '9')
        {
                value = value * 10 + (unsigned long)(bytes[*at] - '0');
                if (value > 65535)
                        return 0;
                digits++;
                (*at)++;
        }
        *into = value;
        return digits != 0;
}

/*
        The image in bytes, or the reason it is not one: null on success, with image set. The rgb pointer is into
        bytes, which the caller keeps for as long as it reads the image. No byte past length is read, and length
        is the caller's count of what it holds: more than SPLASH_FILE_MOST is refused before a byte is looked at.
*/
static inline const char *splash_parse(const unsigned char *bytes, unsigned long length, struct splash_image *image)
{
        unsigned long at = 2, width = 0, height = 0, maxval = 0;

        if (length > SPLASH_FILE_MOST)
                return "it is larger than 1 MiB";
        if (length < 2 || bytes[0] != 'P' || bytes[1] != '6')
                return "not a binary PPM (P6) image";
        if (!splash_field(bytes, length, &at, &width) || !splash_field(bytes, length, &at, &height) ||
            !splash_field(bytes, length, &at, &maxval))
                return "the header is cut short or has no number in it";
        if (!width || !height || width > SPLASH_SIDE_MOST || height > SPLASH_SIDE_MOST)
                return "the size is not 1 to 1024 pixels a side";
        if (maxval != 255)
                return "the maximum value is not 255";
        if (at >= length || !splash_space(bytes[at]))
                return "the pixels do not follow the header";
        at++;
        if (length - at != width * height * 3)
                return "the pixels are not exactly width x height x 3 bytes";
        image->width = (unsigned int)width;
        image->height = (unsigned int)height;
        image->rgb = bytes + at;
        return 0;
}

/*
        The header of a file of width by height pixels, as moonwater splash writes it: "P6\nW H\n255\n", into
        room for SPLASH_HEADER_MOST bytes. Its length, which the pixels follow. What is kept on the system
        partition is this header and the checked pixels, never the header a person's file came with.
*/
static inline unsigned long splash_header(unsigned char *into, unsigned int width, unsigned int height)
{
        unsigned int numbers[2] = {width, height};
        unsigned long at = 0;

        into[at++] = 'P';
        into[at++] = '6';
        into[at++] = '\n';
        for (unsigned int which = 0; which < 2; which++)
        {
                unsigned char digits[10];
                unsigned int count = 0, value = numbers[which];

                do
                        digits[count++] = (unsigned char)('0' + value % 10);
                while ((value /= 10) && count < sizeof(digits));
                while (count)
                        into[at++] = digits[--count];
                into[at++] = which ? '\n' : ' ';
        }
        into[at++] = '2';
        into[at++] = '5';
        into[at++] = '5';
        into[at++] = '\n';
        return at;
}

// Where image goes on a screen of width by height pixels: centred, and cut evenly where the screen is the smaller.
static inline void splash_place(const struct splash_image *image, unsigned int width, unsigned int height, struct splash_place *place)
{
        place->width = image->width < width ? image->width : width;
        place->height = image->height < height ? image->height : height;
        place->source_x = (image->width - place->width) / 2;
        place->source_y = (image->height - place->height) / 2;
        place->x = (width - place->width) / 2;
        place->y = (height - place->height) / 2;
}

/*
        The picture as the firmware's block transfer takes it: a pixel is four bytes, blue, green, red and one
        that is nought, rows from the top with none between them. into has room for width x height x 4 bytes.
*/
static inline void splash_pixels(const struct splash_image *image, unsigned char *into)
{
        unsigned long count = (unsigned long)image->width * image->height;

        for (unsigned long at = 0; at < count; at++)
        {
                into[at * 4] = image->rgb[at * 3 + 2];
                into[at * 4 + 1] = image->rgb[at * 3 + 1];
                into[at * 4 + 2] = image->rgb[at * 3];
                into[at * 4 + 3] = 0;
        }
}

#endif
