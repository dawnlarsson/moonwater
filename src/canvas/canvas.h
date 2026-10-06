/*
        What Moonwater's core asks of Canvas.

        Canvas is its own object, so the core reaches it through the names
        here and no others. With Canvas off every one is an empty inline and
        the core compiles to what it would be without a compositor at all;
        there is no table to fill in, no pointer to test and no weak symbol,
        because both are linked into one image and the call is direct. The
        other direction is src/moonwater/seam.h.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef CANVAS_INCLUDED
#define CANVAS_INCLUDED

#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/mm_types.h>
#include <linux/errno.h>

#ifdef CONFIG_MOONWATER_CANVAS

// The device is registered and the machine is listening: look for a card.
void canvas_boot(void);

// The core is going away. Before anything of its own is.
void canvas_unload(void);

// Whether the desktop holds a card.
_Bool canvas_is_on(void);

// The requests /dev/spark takes that are not the core's; -ENOTTY for the rest.
long canvas_client_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
int canvas_client_mmap(struct file *file, struct vm_area_struct *vma);
__poll_t canvas_client_poll(struct file *file, poll_table *wait);
void canvas_client_release(struct file *file);

#define CANVAS_FILE_OPERATIONS .mmap = canvas_client_mmap, .poll = canvas_client_poll,

#else

static inline void canvas_boot(void) {}
static inline void canvas_unload(void) {}
static inline _Bool canvas_is_on(void) { return false; }

static inline long canvas_client_ioctl(struct file *file, unsigned int cmd,
                                       unsigned long arg)
{
        return -ENOTTY;
}

static inline void canvas_client_release(struct file *file) {}

#define CANVAS_FILE_OPERATIONS

#endif // CONFIG_MOONWATER_CANVAS

#endif // CANVAS_INCLUDED
