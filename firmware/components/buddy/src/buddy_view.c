#include "buddy_view.h"

#include <string.h>

static void place(const buddy_view_t *v, gfx_sprite_t *sp, buddy_point_t at, int lift_px)
{
    sp->x = v->x0 + at.x * v->scale;
    sp->y = v->y0 + at.y * v->scale - lift_px;
}

static void init_sprite(const buddy_view_t *v, gfx_sprite_t *sp, const gfx_image_t *image)
{
    memset(sp, 0, sizeof(*sp));
    sp->image = image;
    sp->palette = &v->art->palette;
    sp->scale = (uint8_t)v->scale;
}

bool buddy_view_init(buddy_view_t *v, gfx_scene_t *scene, gfx_rect_t stage, int z, uint32_t now_ms)
{
    memset(v, 0, sizeof(*v));
    v->art = buddy_art_get();
    if (!v->art) {
        return false;
    }
    const buddy_art_t *a = v->art;
    const int full_h = a->h + BUDDY_MAX_LIFT; // headroom for hops
    const int sx = stage.w * 45 / 100 / a->w;
    const int sy = stage.h * 60 / 100 / full_h;
    v->scale = sx < sy ? sx : sy;
    if (v->scale < 1 || v->scale > 255) {
        return false;
    }
    v->x0 = stage.x + (stage.w - a->w * v->scale) / 2;
    v->y0 = stage.y + (stage.h - full_h * v->scale) / 2 + BUDDY_MAX_LIFT * v->scale;
    v->anim_start = now_ms;

    init_sprite(v, &v->shadow, &a->shadow);
    init_sprite(v, &v->body, &a->body);
    init_sprite(v, &v->antenna, &a->antenna_on);
    init_sprite(v, &v->eye[0], &a->eyes[BUDDY_EYES_OPEN]);
    init_sprite(v, &v->eye[1], &a->eyes[BUDDY_EYES_OPEN]);
    init_sprite(v, &v->mouth, &a->mouth[BUDDY_MOUTH_NEUTRAL]);
    const buddy_pose_t rest = {BUDDY_STATE_IDLE, BUDDY_EYES_OPEN, BUDDY_MOUTH_NEUTRAL, BUDDY_ANTENNA_CALM, 0};
    buddy_view_apply(v, &rest, now_ms);

    return gfx_scene_add(scene, &v->shadow, z) && gfx_scene_add(scene, &v->body, z + 1) &&
           gfx_scene_add(scene, &v->antenna, z + 1) && gfx_scene_add(scene, &v->eye[0], z + 2) &&
           gfx_scene_add(scene, &v->eye[1], z + 2) && gfx_scene_add(scene, &v->mouth, z + 2);
}

uint32_t buddy_view_apply(buddy_view_t *v, const buddy_pose_t *pose, uint32_t now_ms)
{
    const buddy_art_t *a = v->art;
    const int lift = (pose->lift > BUDDY_MAX_LIFT ? BUDDY_MAX_LIFT : pose->lift) * v->scale;
    uint32_t next = UINT32_MAX;

    v->antenna.image = gfx_anim_image(&a->antenna[pose->antenna], now_ms - v->anim_start, &next);
    v->eye[0].image = v->eye[1].image = &a->eyes[pose->eyes];
    v->mouth.image = &a->mouth[pose->mouth];
    v->shadow.image = pose->lift >= 2 ? &a->shadow_small : &a->shadow;

    place(v, &v->body, a->body_at, lift);
    place(v, &v->antenna, a->antenna_at, lift);
    place(v, &v->eye[0], a->eye_at[0], lift);
    place(v, &v->eye[1], a->eye_at[1], lift);
    place(v, &v->mouth, a->mouth_at, lift);
    place(v, &v->shadow, a->shadow_at, 0);
    return next;
}

bool buddy_view_hit(const buddy_view_t *v, int x, int y)
{
    gfx_rect_t r = gfx_rect_union(gfx_sprite_bounds(&v->body), gfx_sprite_bounds(&v->antenna));
    const int margin = 2 * v->scale; // fingers are wider than outlines
    r.x -= margin;
    r.y -= margin;
    r.w += 2 * margin;
    r.h += 2 * margin;
    return gfx_rect_contains_point(r, x, y);
}
