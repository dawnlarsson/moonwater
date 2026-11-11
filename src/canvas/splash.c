/*
        The splash: the picture Canvas shows on the desktop's background while the desktop loads, and the
        parser that reads a file of one. It is a binary PPM (P6), the one format that needs no decoder.

        moonwater splash (src/sh/host.c) checks a file with splash_parse before it keeps a copy, and the
        compositor (compositor.c) checks the copy it reads at each start the same way, so one parser says
        what a usable file is. Both include this file, as shell.c includes window.c.

        A file is the magic P6, then width, height and maxval as decimal numbers with white space and #
        comments between them, one white space byte, and width x height x 3 bytes of RGB, rows from the top.
        maxval must be 255. The whole file is that and nothing after it: a file that is longer, shorter or
        says any other size is refused, with the reason.

        The most a splash file may be is SPLASH_FILE_MOST bytes, and each side is at most SPLASH_SIDE_MOST
        pixels. Canvas draws the picture at its own size, in device pixels, centred; it is never scaled.
*/
#define SPLASH_PATH_IMAGE "/root/canvas.splash.ppm" // the copy moonwater splash keeps; absent is the built-in picture
#define SPLASH_PATH_STATE "/root/canvas.splash"     // "off" when the splash is off; absent otherwise
#define SPLASH_FILE_MOST (1u << 20)                 // the most bytes a splash file may be: 1 MiB
#define SPLASH_SIDE_MOST 1024                       // the most pixels across, and the most down

struct splash_image
{
        unsigned int width, height;
        const unsigned char *rgb; // width x height x 3 bytes, in the file
};

static int splash_space(unsigned char byte)
{
        return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\v' || byte == '\f';
}

// The next decimal number of the header, with the white space and comments before it. 0 when there is none, or it is too large.
static int splash_field(const unsigned char *bytes, unsigned long length, unsigned long *at, unsigned long *into)
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
        bytes, which the caller keeps for as long as it reads the image.
*/
static const char *splash_parse(const unsigned char *bytes, unsigned long length, struct splash_image *image)
{
        unsigned long at = 2, width = 0, height = 0, maxval = 0;

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
