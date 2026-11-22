/* The Scenes overview's drag API (C8, comp-manage). The taskbar's globe button (TASKBAR_LAUNCHER) posts the drag:

        compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_BEGIN, screen, 0, 0)     the press became a drag on this screen
        compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_MOVE, screen, progress, velocity)
        compositor_scenes_drag(COMPOSITOR_SCENES_DRAG_END, screen, progress, velocity)

   progress is 0..1024 and is measured from the band's edge toward the screen's middle, so it works on a bottom, top or side
   band; dragging back toward the band lowers it. velocity is px/s, positive away from the band's edge.
   END commits (the overview opens on the screen) when velocity is over COMPOSITOR_SCENES_SWIPE away from the band; it cancels
   when velocity is over that toward the band; otherwise it commits when progress is over half the way. A tap without a drag
   is not the API's: the globe's tap keeps opening the launcher. */
#ifndef MOONWATER_CANVAS_COMPOSITOR_SCENES_H
#define MOONWATER_CANVAS_COMPOSITOR_SCENES_H

enum
{
        COMPOSITOR_SCENES_DRAG_BEGIN = 0,
        COMPOSITOR_SCENES_DRAG_MOVE = 1,
        COMPOSITOR_SCENES_DRAG_END = 2,
        COMPOSITOR_SCENES_SWIPE = 600,  // px/s
        COMPOSITOR_SCENES_PROGRESS_ONE = 1024
};

void compositor_scenes_drag(unsigned int phase, int screen, int progress, int velocity);

#endif
