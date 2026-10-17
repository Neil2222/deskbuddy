#include "office_faces.h"

#include <string.h>

static const char *const k_names[OFFICE_FACE_COUNT] = {
    [OFFICE_FACE_CLASSIC] = "classic",
    [OFFICE_FACE_SKELETON] = "skeleton",
};

const char *office_face_name(office_face_t face)
{
    return (unsigned)face < OFFICE_FACE_COUNT ? k_names[face] : NULL;
}

bool office_face_parse(const char *name, office_face_t *out)
{
    for (int i = 0; name && i < OFFICE_FACE_COUNT; i++) {
        if (strcmp(name, k_names[i]) == 0) {
            *out = (office_face_t)i;
            return true;
        }
    }
    return false;
}

bool office_faces_init(office_faces_t *f, gfx_scene_t *scene, gfx_rect_t screen, uint8_t *classic_mem,
                       uint8_t *skeleton_mem, uint8_t *skeleton_frame_mem, office_skeleton_flush_fn flush, int z)
{
    memset(f, 0, sizeof(*f));
    f->face = OFFICE_FACE_CLASSIC;
    f->has[OFFICE_FACE_CLASSIC] =
        office_clock_init(&f->classic, scene, screen, classic_mem, OFFICE_CLOCK_MEMORY, z);
    if (!f->has[OFFICE_FACE_CLASSIC]) {
        return false;
    }
    f->has[OFFICE_FACE_SKELETON] =
        skeleton_mem && office_skeleton_init(&f->skeleton, scene, screen, skeleton_mem, OFFICE_SKELETON_MEMORY,
                                             skeleton_frame_mem, office_skeleton_frame_memory(), flush, z);
    return true;
}

static void show_face(office_faces_t *f, office_face_t face, bool visible)
{
    if (face == OFFICE_FACE_SKELETON) {
        office_skeleton_show(&f->skeleton, visible);
    } else {
        office_clock_show(&f->classic, visible);
    }
}

bool office_faces_select(office_faces_t *f, office_face_t face)
{
    if ((unsigned)face >= OFFICE_FACE_COUNT || !f->has[face]) {
        return false;
    }
    if (face != f->face && f->visible) {
        show_face(f, f->face, false);
        show_face(f, face, true);
    }
    f->face = face;
    return true;
}

void office_faces_show(office_faces_t *f, bool visible)
{
    f->visible = visible;
    show_face(f, f->face, visible);
}

uint32_t office_faces_apply(office_faces_t *f, const struct tm *tm, uint32_t ms)
{
    if (f->face == OFFICE_FACE_SKELETON) {
        return office_skeleton_apply(&f->skeleton, tm, ms);
    }
    return office_clock_apply(&f->classic, tm, ms);
}

gfx_background_fn office_faces_background(office_faces_t *f, void **ctx)
{
    if (f->face == OFFICE_FACE_SKELETON) {
        *ctx = &f->skeleton;
        return office_skeleton_background;
    }
    *ctx = NULL;
    return office_background;
}
