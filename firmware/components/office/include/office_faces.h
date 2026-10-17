// Clock Mode's faces: which one is selected, and the calls that go to it.
//
// Classic is office_clock.h, unchanged; Mechanical Skeleton is office_skeleton.h. Both are set up at start (their
// art is rendered once), each with its own sprites in the scene, hidden while the other one shows. The selection
// lives in RAM only: after a restart the face is Classic again (the companion may send the one it remembers).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "gfx_scene.h"
#include "office_clock.h"
#include "office_skeleton.h"

#ifdef __cplusplus
extern "C" {
#endif

// In display order; the wire names (office_face_name) never change.
typedef enum {
    OFFICE_FACE_CLASSIC,
    OFFICE_FACE_SKELETON,
    OFFICE_FACE_COUNT,
} office_face_t;

// "classic", "skeleton"; NULL for anything else.
const char *office_face_name(office_face_t face);

// The face called `name`; false if this firmware has none by that name.
bool office_face_parse(const char *name, office_face_t *out);

typedef struct {
    office_clock_t classic;
    office_skeleton_t skeleton;
    bool has[OFFICE_FACE_COUNT]; // set up and selectable
    office_face_t face;          // selected
    bool visible;
} office_faces_t;

// Sets up Classic (classic_mem: OFFICE_CLOCK_MEMORY bytes) and Mechanical Skeleton (skeleton_mem:
// OFFICE_SKELETON_MEMORY bytes; NULL leaves it out), Classic selected. Returns false only if Classic could not be set
// up (then Clock Mode is unavailable).
bool office_faces_init(office_faces_t *f, gfx_scene_t *scene, gfx_rect_t screen, uint8_t *classic_mem,
                       uint8_t *skeleton_mem, int z);

// Selects a face. If the clock is showing, its sprites change at once and the caller must redraw the whole screen
// (the background changes too: office_faces_background). false: not available (nothing changes).
bool office_faces_select(office_faces_t *f, office_face_t face);

void office_faces_show(office_faces_t *f, bool visible);

// As office_clock_apply(), for the selected face.
uint32_t office_faces_apply(office_faces_t *f, const struct tm *tm, uint32_t ms);

// The selected face's background, for gfx_scene_t.background / background_ctx.
gfx_background_fn office_faces_background(office_faces_t *f, void **ctx);

#ifdef __cplusplus
}
#endif
