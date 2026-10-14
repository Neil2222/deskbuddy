#include "buddy_view.h"

#include <math.h>
#include <string.h>

static void init_sprite(const buddy_view_t *v, gfx_sprite_t *sp, const gfx_image_t *image)
{
    memset(sp, 0, sizeof(*sp));
    sp->image = image;
    sp->palette = &v->art->palette;
    sp->scale = (uint8_t)v->scale;
}

// Puts a sprite's top-left corner at art point (x, y) plus a screen-pixel offset.
static void put(const buddy_view_t *v, gfx_sprite_t *sp, int x, int y, int dx_px, int dy_px)
{
    sp->x = v->x0 + x * v->scale + dx_px;
    sp->y = v->y0 + y * v->scale + dy_px;
}

static void place(const buddy_view_t *v, gfx_sprite_t *sp, buddy_point_t at, int dx_px, int dy_px)
{
    put(v, sp, at.x, at.y, dx_px, dy_px);
}

static void show(gfx_sprite_t *sp, const gfx_image_t *image)
{
    sp->image = image;
    sp->flags = (uint8_t)(sp->flags & ~GFX_SPRITE_HIDDEN);
}

static void hide(gfx_sprite_t *sp)
{
    sp->flags |= GFX_SPRITE_HIDDEN;
}

// Art-pixel extent of everything the character may draw, relative to the art origin at lift 0.
static gfx_rect_t art_extent(const buddy_art_t *a)
{
    const buddy_props_art_t *p = &a->props;
    int x0 = 0, y0 = -BUDDY_MAX_LIFT, x1 = a->w, y1 = a->h;
    x0 = p->battery_at.x < x0 ? p->battery_at.x : x0;
    x1 = p->can_at.x + p->can[0].w + 1 > x1 ? p->can_at.x + p->can[0].w + 1 : x1;
    y0 = p->alert_at.y - BUDDY_MAX_LIFT < y0 ? p->alert_at.y - BUDDY_MAX_LIFT : y0;
    for (int i = 0; i < BUDDY_BOLT_SLOTS; i++) {
        x0 = p->bolt_at[i].x - 1 < x0 ? p->bolt_at[i].x - 1 : x0;
        y0 = p->bolt_at[i].y - 2 < y0 ? p->bolt_at[i].y - 2 : y0; // lift and shake
    }
    return (gfx_rect_t){x0, y0, x1 - x0, y1 - y0};
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
    // The props must fit beside and above the centred robot too.
    const gfx_rect_t ext = art_extent(a);
    for (; v->scale >= 1; v->scale--) {
        const int x0 = stage.x + (stage.w - a->w * v->scale) / 2;
        const int y0 = stage.y + (stage.h - full_h * v->scale) / 2 + BUDDY_MAX_LIFT * v->scale;
        const gfx_rect_t r = {x0 + ext.x * v->scale, y0 + ext.y * v->scale, ext.w * v->scale, ext.h * v->scale};
        if (gfx_rect_contains_rect(stage, r)) {
            v->x0 = x0;
            v->y0 = y0;
            break;
        }
    }
    if (v->scale < 1 || v->scale > 127) {
        return false;
    }
    v->anim_start = now_ms;
    v->rig_px.part[0].cur = v->rig_px.part[1].cur = -1;

    const buddy_props_art_t *p = &a->props;
    init_sprite(v, &v->shadow, &a->shadow);
    init_sprite(v, &v->torso, &a->torso);
    init_sprite(v, &v->head, &a->head);
    init_sprite(v, &v->antenna, &a->antenna_on);
    init_sprite(v, &v->eye[0], &a->eyes[BUDDY_EYES_OPEN]);
    init_sprite(v, &v->eye[1], &a->eyes[BUDDY_EYES_OPEN]);
    init_sprite(v, &v->mouth, &a->mouth[BUDDY_MOUTH_NEUTRAL]);
    init_sprite(v, &v->can_shadow, &p->can_shadow);
    init_sprite(v, &v->can, &p->can[BUDDY_CAN_LEVELS - 1]);
    init_sprite(v, &v->port, &p->port);
    init_sprite(v, &v->rig[0], NULL);
    init_sprite(v, &v->rig[1], NULL);
    init_sprite(v, &v->battery, &p->battery[0]);
    init_sprite(v, &v->alert, &p->alert);
    for (int i = 0; i < BUDDY_MAX_BOLTS; i++) {
        init_sprite(v, &v->bolt[i], &p->bolt[0]);
    }
    for (int i = 0; i < BUDDY_FX_COUNT; i++) {
        init_sprite(v, &v->fx[i], &p->spark[0]);
    }
    for (int i = 0; i < BUDDY_PARTICLES; i++) {
        init_sprite(v, &v->part[i], NULL);
    }
    const buddy_pose_t rest = {.eyes = BUDDY_EYES_OPEN, .mouth = BUDDY_MOUTH_NEUTRAL, .antenna = BUDDY_ANTENNA_CALM};
    buddy_view_apply(v, &rest, now_ms);

    bool ok = gfx_scene_add(scene, &v->shadow, z) && gfx_scene_add(scene, &v->can_shadow, z) &&
              gfx_scene_add(scene, &v->torso, z + 1) && gfx_scene_add(scene, &v->head, z + 1) &&
              gfx_scene_add(scene, &v->antenna, z + 1) &&
              gfx_scene_add(scene, &v->can, z + 1) && gfx_scene_add(scene, &v->port, z + 1) &&
              gfx_scene_add(scene, &v->eye[0], z + 2) && gfx_scene_add(scene, &v->eye[1], z + 2) &&
              gfx_scene_add(scene, &v->mouth, z + 2) && gfx_scene_add(scene, &v->rig[0], z + 3) &&
              gfx_scene_add(scene, &v->rig[1], z + 3) && gfx_scene_add(scene, &v->battery, z + 3) &&
              gfx_scene_add(scene, &v->alert, z + 3);
    for (int i = 0; ok && i < BUDDY_MAX_BOLTS; i++) {
        ok = gfx_scene_add(scene, &v->bolt[i], z + 4);
    }
    for (int i = 0; ok && i < BUDDY_FX_COUNT; i++) {
        ok = gfx_scene_add(scene, &v->fx[i], z + 4);
    }
    for (int i = 0; ok && i < BUDDY_PARTICLES; i++) {
        ok = gfx_scene_add(scene, &v->part[i], z + 4);
    }
    return ok;
}

// --- Rig: telescopic arm and cable, drawn into an indexed image ------------------------------------------------

typedef struct {
    float x, y;
} fpoint_t;

static void plot(buddy_rig_t *r, int x, int y, uint8_t c)
{
    x -= BUDDY_RIG_X;
    y -= BUDDY_RIG_Y;
    if (x >= 0 && y >= 0 && x < BUDDY_RIG_W && y < BUDDY_RIG_H) {
        r->work[y * BUDDY_RIG_W + x] = c;
    }
}

static void stamp(buddy_rig_t *r, const gfx_image_t *img, int x, int y)
{
    for (int iy = 0; iy < img->h; iy++) {
        for (int ix = 0; ix < img->w; ix++) {
            const uint8_t c = img->px[iy * img->w + ix];
            if (c) {
                plot(r, x + ix, y + iy, c);
            }
        }
    }
}

static int round_i(float f)
{
    return (int)floorf(f + 0.5f);
}

// Energy pulses travel from the can (index 0) towards the far end as `flow` grows.
static uint8_t cable_color(int i, bool energized, uint16_t flow)
{
    if (!energized) {
        return BUDDY_PX_CABLE;
    }
    const int phase = (i - flow % 6 + 6) % 6;
    return phase == 0 ? BUDDY_PX_WHITE : phase == 1 ? BUDDY_PX_YELLOW : BUDDY_PX_NEON;
}

// A hanging cable of fixed length from a to b: a quadratic curve that sags more the closer the ends are.
static void draw_cable(buddy_rig_t *r, fpoint_t a, fpoint_t b, float len, int ground, bool energized, uint16_t flow)
{
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float d = sqrtf(dx * dx + dy * dy);
    float sag = d >= len ? 0.5f : d > len / 3 ? sqrtf(3 * d * (len - d) / 8) : (len - d) / 2;
    const fpoint_t c = {(a.x + b.x) / 2, (a.y + b.y) / 2 + 2 * sag};
    const int steps = (int)(2 * (fabsf(dx) + fabsf(dy) + 4 * sag)) + 8;
    int px = -1000, py = -1000, i = 0;
    for (int s = 0; s <= steps; s++) {
        const float t = (float)s / steps, u = 1 - t;
        const int x = round_i(u * u * a.x + 2 * u * t * c.x + t * t * b.x);
        int y = round_i(u * u * a.y + 2 * u * t * c.y + t * t * b.y);
        y = y > ground ? ground : y;
        if (x == px && y == py) {
            continue;
        }
        plot(r, x, y, cable_color(i++, energized, flow));
        px = x;
        py = y;
    }
}

// The arm extends from the root like a telescope: two pixels thick, with a dark seam between sections.
static void draw_rod(buddy_rig_t *r, int x0, int y0, int x1, int y1)
{
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    const int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    const bool flat = dx >= -dy; // thicken across the direction of travel
    int err = dx + dy, i = 0;
    for (;;) {
        const bool seam = i++ % 4 == 3;
        plot(r, x0 + !flat, y0 + flat, seam ? BUDDY_PX_OUTLINE : BUDDY_PX_BODY_SHADE);
        plot(r, x0, y0, seam ? BUDDY_PX_OUTLINE : BUDDY_PX_BODY);
        if (x0 == x1 && y0 == y1) {
            return;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static buddy_point_t offset(buddy_point_t p, int dx, int dy)
{
    return (buddy_point_t){p.x + dx, p.y + dy};
}

// Hand position in art pixels; bx/by is the body offset (shake, lift), hx/hy the head's (plus wobble and nod).
static fpoint_t hand_spot(const buddy_props_art_t *p, buddy_hand_t h, int bx, int by, int hx, int hy)
{
    buddy_point_t s;
    switch (h) {
    case BUDDY_HAND_SHOW:
        s = offset(p->hand_show, bx, by);
        break;
    case BUDDY_HAND_CAN:
        s = p->hand_can;
        break;
    case BUDDY_HAND_PORT:
        s = offset(p->hand_port, hx, hy);
        break;
    case BUDDY_HAND_MOUTH:
        s = offset(p->hand_mouth, hx, hy);
        break;
    case BUDDY_HAND_REST:
        s = offset(p->hand_rest, bx, by);
        break;
    case BUDDY_HAND_HOME:
    default:
        s = offset(p->arm_root, bx, by);
        break;
    }
    return (fpoint_t){(float)s.x, (float)s.y};
}

static bool hand_out(const buddy_props_t *pr)
{
    const bool at_from = pr->hand_mix == 0, at_to = pr->hand_mix == 255;
    return !((pr->hand_from == BUDDY_HAND_HOME && (at_from || pr->hand_to == BUDDY_HAND_HOME)) ||
             (pr->hand_to == BUDDY_HAND_HOME && at_to));
}

static buddy_point_t hand_now(const buddy_props_art_t *p, const buddy_props_t *pr, int bx, int by, int hx, int hy)
{
    const fpoint_t a = hand_spot(p, pr->hand_from, bx, by, hx, hy), b = hand_spot(p, pr->hand_to, bx, by, hx, hy);
    const float m = pr->hand_mix / 255.0f;
    return (buddy_point_t){round_i(a.x + (b.x - a.x) * m), round_i(a.y + (b.y - a.y) * m)};
}

static void draw_rig(buddy_view_t *v, const buddy_props_t *pr, int bx, int by, int hx, int hy)
{
    const buddy_props_art_t *p = &v->art->props;
    buddy_rig_t *r = &v->rig_px;
    memset(r->work, 0, sizeof(r->work));

    const bool out = hand_out(pr);
    const buddy_point_t h = hand_now(p, pr, bx, by, hx, hy);
    const buddy_point_t held_plug = {h.x, h.y - p->plug.h}; // the plug sits in the claw
    const buddy_point_t port_plug = offset(p->plug_port, hx, hy);
    const buddy_point_t ground_plug = {p->can_at.x - 3, p->ground_y + 1 - p->plug.h};
    // The plug stays in the can's lid, which sinks when the can is crushed.
    const int sink = pr->can == BUDDY_CAN_CRUSHED ? p->can[0].h - p->can_crushed.h : 0;
    const buddy_point_t can_plug = {p->plug_can.x, p->plug_can.y + sink};
    const fpoint_t can_end = {(float)can_plug.x, (float)can_plug.y};
    const fpoint_t hand_end = {(float)held_plug.x, (float)held_plug.y + 1};
    const float len = (float)p->cable_len;

    switch (pr->cable) {
    case BUDDY_CABLE_HELD: { // both ends in the hand: a loop hangs from the hand, draped over the arm
        const buddy_point_t root = offset(p->arm_root, bx, by);
        const fpoint_t drape = {(float)root.x + (float)(h.x - root.x) * 0.4f,
                                (float)root.y + (float)(h.y - root.y) * 0.4f};
        draw_cable(r, hand_end, drape, len / 2, p->ground_y, false, 0);
        break;
    }
    case BUDDY_CABLE_CAN_TO_HAND:
        draw_cable(r, can_end, hand_end, len, p->ground_y, false, 0);
        break;
    case BUDDY_CABLE_CAN_TO_PORT:
        draw_cable(r, can_end, (fpoint_t){(float)port_plug.x + 1, (float)port_plug.y + 1}, len, p->ground_y,
                   pr->energized, pr->flow);
        break;
    case BUDDY_CABLE_DROPPED:
        draw_cable(r, can_end, (fpoint_t){(float)ground_plug.x, (float)ground_plug.y}, len, p->ground_y, false, 0);
        break;
    default:
        break;
    }

    if (out) {
        draw_rod(r, p->arm_root.x + bx + 1, p->arm_root.y + by, h.x, h.y + 1);
        if (pr->cig) { // between the claw's prongs, drawn over by them
            stamp(r, &p->cig[pr->cig_left % BUDDY_CIG_LENGTHS][pr->ember % BUDDY_EMBERS], h.x - BUDDY_CIG_GRIP,
                  h.y - 1);
        }
        stamp(r, &p->hand, h.x - 1, h.y - 1);
    }
    switch (pr->cable) {
    case BUDDY_CABLE_COILED:
        stamp(r, &p->coil, h.x - 2, h.y - 4);
        stamp(r, &p->plug, held_plug.x, held_plug.y);
        break;
    case BUDDY_CABLE_HELD:
        stamp(r, &p->plug, held_plug.x, held_plug.y);
        break;
    case BUDDY_CABLE_CAN_TO_HAND:
        stamp(r, &p->plug, can_plug.x, can_plug.y);
        stamp(r, &p->plug, held_plug.x, held_plug.y);
        break;
    case BUDDY_CABLE_CAN_TO_PORT:
        stamp(r, &p->plug, can_plug.x, can_plug.y);
        stamp(r, &p->plug, port_plug.x, port_plug.y);
        break;
    case BUDDY_CABLE_DROPPED:
        stamp(r, &p->plug, can_plug.x, can_plug.y);
        stamp(r, &p->plug, ground_plug.x, ground_plug.y);
        break;
    default:
        break;
    }
}

// Crops columns [c0, c1) of the drawn rig and publishes them on `sp`, switching to the part's other output image
// whenever the content changed.
static void commit_part(buddy_view_t *v, buddy_rig_part_t *r, gfx_sprite_t *sp, int c0, int c1)
{
    const uint8_t *work = v->rig_px.work;
    int x0 = c1, y0 = BUDDY_RIG_H, x1 = -1, y1 = -1;
    for (int y = 0; y < BUDDY_RIG_H; y++) {
        for (int x = c0; x < c1; x++) {
            if (work[y * BUDDY_RIG_W + x]) {
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0;
                y1 = y > y1 ? y : y1;
            }
        }
    }
    if (x1 < 0) {
        r->cur = -1;
        sp->image = NULL;
        return;
    }
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (r->cur >= 0) {
        const gfx_image_t *img = &r->image[r->cur];
        bool same = img->w == w && img->h == h && r->x == BUDDY_RIG_X + x0 && r->y == BUDDY_RIG_Y + y0;
        for (int y = 0; same && y < h; y++) {
            same = memcmp(img->px + y * w, work + (y0 + y) * BUDDY_RIG_W + x0, (size_t)w) == 0;
        }
        if (same) {
            return;
        }
    }
    // Never rewrite the image the screen was last drawn from: its pointer is how the scene notices a change.
    const int next = sp->shown.image == &r->image[0] ? 1 : 0;
    for (int y = 0; y < h; y++) {
        memcpy(r->px[next] + y * w, work + (y0 + y) * BUDDY_RIG_W + x0, (size_t)w);
    }
    r->image[next] = (gfx_image_t){(uint16_t)w, (uint16_t)h, r->px[next]};
    r->cur = next;
    r->x = BUDDY_RIG_X + x0;
    r->y = BUDDY_RIG_Y + y0;
    sp->image = &r->image[next];
    put(v, sp, r->x, r->y, 0, 0);
}

static void commit_rig(buddy_view_t *v)
{
    const int split = BUDDY_RIG_SPLIT - BUDDY_RIG_X;
    commit_part(v, &v->rig_px.part[0], &v->rig[0], 0, split);
    commit_part(v, &v->rig_px.part[1], &v->rig[1], split, BUDDY_RIG_W);
}

// --- Props -----------------------------------------------------------------------------------------------------

static uint32_t hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static void apply_bolts(buddy_view_t *v, const buddy_props_t *pr, int dx_px, int dy_px)
{
    const buddy_props_art_t *p = &v->art->props;
    const uint32_t h = hash(pr->strike);
    const unsigned base = (h >> 8) % BUDDY_BOLT_SLOTS;
    for (int i = 0; i < BUDDY_MAX_BOLTS; i++) {
        gfx_sprite_t *sp = &v->bolt[i];
        if (i >= pr->bolts) {
            hide(sp);
            continue;
        }
        show(sp, &p->bolt[(h >> (16 + i)) & 1u]);
        sp->flags = (uint8_t)((sp->flags & ~GFX_SPRITE_FLIP_X) | ((h >> (20 + i)) & 1u ? GFX_SPRITE_FLIP_X : 0));
        place(v, sp, p->bolt_at[(base + (unsigned)i) % BUDDY_BOLT_SLOTS], dx_px, dy_px);
    }
}

static void apply_fx(buddy_view_t *v, const buddy_props_t *pr, int hx, int by, buddy_point_t hand)
{
    const buddy_props_art_t *p = &v->art->props;
    for (int i = 0; i < BUDDY_FX_COUNT; i++) {
        const buddy_fx_t *f = &pr->fx[i];
        gfx_sprite_t *sp = &v->fx[i];
        const gfx_image_t *img = f->kind == BUDDY_FX_SPARK  ? &p->spark[f->frame % BUDDY_SPARK_FRAMES]
                                 : f->kind == BUDDY_FX_POOF ? &p->poof[f->frame % BUDDY_POOF_FRAMES]
                                                            : NULL;
        if (!img) {
            hide(sp);
            continue;
        }
        buddy_point_t c;
        switch (f->at) {
        case BUDDY_SPOT_CAN_TOP:
            c = (buddy_point_t){p->plug_can.x + 1, p->plug_can.y};
            break;
        case BUDDY_SPOT_CAN_BASE:
            c = (buddy_point_t){p->can_at.x + p->can[0].w / 2, p->ground_y};
            break;
        case BUDDY_SPOT_CAN:
            c = (buddy_point_t){p->can_at.x + p->can[0].w / 2, p->can_at.y + p->can[0].h * 2 / 3};
            break;
        case BUDDY_SPOT_PORT:
            c = offset(p->plug_port, hx, by + 1); // where plug meets port
            break;
        case BUDDY_SPOT_HAND:
        default:
            c = (buddy_point_t){hand.x, hand.y - p->plug.h};
            break;
        }
        // A cloud over the whole can is drawn with chunkier pixels.
        const int scale = f->at == BUDDY_SPOT_CAN ? 2 * v->scale : v->scale;
        show(sp, img);
        sp->scale = (uint8_t)scale;
        sp->x = v->x0 + c.x * v->scale + v->scale / 2 - img->w * scale / 2;
        sp->y = v->y0 + c.y * v->scale + v->scale / 2 - img->h * scale / 2;
    }
}

// Floating effects. Head start points follow the head; the cigarette's tip follows the resting hand.
static void apply_particles(buddy_view_t *v, const buddy_props_t *pr, int bx, int by, int hx, int hy)
{
    const buddy_props_art_t *p = &v->art->props;
    for (int i = 0; i < BUDDY_PARTICLES; i++) {
        const buddy_particle_t *q = &pr->particle[i];
        gfx_sprite_t *sp = &v->part[i];
        if (q->kind == BUDDY_PART_NONE || q->kind >= BUDDY_PART_COUNT || q->from >= BUDDY_FROM_COUNT) {
            hide(sp);
            continue;
        }
        buddy_point_t c;
        if (q->from == BUDDY_FROM_EMBER) {
            const gfx_image_t *cig = &p->cig[pr->cig_left % BUDDY_CIG_LENGTHS][0];
            c = offset(p->hand_rest, bx - BUDDY_CIG_GRIP + cig->w - 1, by - 1);
        } else {
            c = offset(p->from[q->from], hx, hy);
        }
        const gfx_image_t *img = &p->part[q->kind][q->frame % p->part_frames[q->kind]];
        show(sp, img);
        sp->x = v->x0 + (c.x + q->dx) * v->scale + v->scale / 2 - img->w * v->scale / 2;
        sp->y = v->y0 + (c.y + q->dy) * v->scale + v->scale / 2 - img->h * v->scale / 2;
    }
}

static void apply_props(buddy_view_t *v, const buddy_props_t *pr, int bx, int by, int hx, int hy)
{
    const buddy_props_art_t *p = &v->art->props;
    const int dx_px = bx * v->scale, dy_px = by * v->scale;

    if (pr->can == BUDDY_CAN_NONE) {
        hide(&v->can);
        hide(&v->can_shadow);
    } else {
        const bool crushed = pr->can == BUDDY_CAN_CRUSHED;
        const gfx_image_t *img = crushed ? &p->can_crushed : &p->can[pr->can_level % BUDDY_CAN_LEVELS];
        show(&v->can, img);
        put(v, &v->can, p->can_at.x, p->ground_y + 1 - img->h - pr->can_drop, 0, 0);
        if (pr->can_drop < 8) {
            show(&v->can_shadow, &p->can_shadow);
            put(v, &v->can_shadow, p->can_at.x - 1, p->ground_y + 1, 0, 0);
        } else {
            hide(&v->can_shadow);
        }
    }

    if (pr->port) {
        show(&v->port, &p->port);
        place(v, &v->port, p->port_at, hx * v->scale, dy_px);
    } else {
        hide(&v->port);
    }
    if (pr->battery >= 1 && pr->battery <= BUDDY_BATTERY_FRAMES) {
        show(&v->battery, &p->battery[pr->battery - 1]);
        place(v, &v->battery, p->battery_at, 0, 0);
    } else {
        hide(&v->battery);
    }
    if (pr->alert) {
        show(&v->alert, &p->alert);
        place(v, &v->alert, p->alert_at, dx_px, dy_px);
    } else {
        hide(&v->alert);
    }
    apply_bolts(v, pr, dx_px, dy_px);

    draw_rig(v, pr, bx, by, hx, hy);
    commit_rig(v);
    apply_fx(v, pr, hx, hy, hand_now(p, pr, bx, by, hx, hy));
    apply_particles(v, pr, bx, by, hx, hy);
}

uint32_t buddy_view_apply(buddy_view_t *v, const buddy_pose_t *pose, uint32_t now_ms)
{
    const buddy_art_t *a = v->art;
    const int lift = pose->lift > BUDDY_MAX_LIFT ? BUDDY_MAX_LIFT : pose->lift;
    const int shake = pose->shake < -1 ? -1 : pose->shake > 1 ? 1 : pose->shake;
    const int wobble = pose->wobble < -1 ? -1 : pose->wobble > 1 ? 1 : pose->wobble;
    const int look = pose->look < -1 ? -1 : pose->look > 1 ? 1 : pose->look;
    const int dx = shake * v->scale, dy = -lift * v->scale, head_dx = (shake + wobble) * v->scale;
    const int nod = pose->nod > 1 ? 1 : pose->nod, head_dy = dy + nod * v->scale;
    const gfx_palette_t *tint = &a->tint[pose->tint < BUDDY_TINT_COUNT ? pose->tint : BUDDY_TINT_NORMAL];
    uint32_t next = UINT32_MAX;

    // Undo buddy_view_hide(): the robot and the rig are always shown; the props show or hide per pose below.
    gfx_sprite_t *always[] = {&v->shadow, &v->torso, &v->head, &v->antenna, &v->eye[0],
                              &v->eye[1], &v->mouth,  &v->rig[0], &v->rig[1]};
    for (unsigned i = 0; i < sizeof(always) / sizeof(always[0]); i++) {
        always[i]->flags = (uint8_t)(always[i]->flags & ~GFX_SPRITE_HIDDEN);
    }

    const buddy_antenna_t ant = pose->antenna < BUDDY_ANTENNA_COUNT ? pose->antenna : BUDDY_ANTENNA_CALM;
    v->antenna.image = gfx_anim_image(&a->antenna[ant], now_ms - v->anim_start, &next);
    v->eye[0].image = v->eye[1].image = &a->eyes[pose->eyes];
    const uint8_t mirror = a->eye_mirror[pose->eyes] ? GFX_SPRITE_FLIP_X : 0; // asymmetric expressions
    v->eye[1].flags = (uint8_t)((v->eye[1].flags & ~GFX_SPRITE_FLIP_X) | mirror);
    v->mouth.image = &a->mouth[pose->mouth];
    v->shadow.image = lift >= 2 ? &a->shadow_small : &a->shadow;
    v->head.palette = v->torso.palette = v->antenna.palette = tint;
    v->eye[0].palette = v->eye[1].palette = v->mouth.palette = tint;

    place(v, &v->torso, a->torso_at, dx, dy);
    place(v, &v->head, a->head_at, head_dx, head_dy);
    place(v, &v->antenna, a->antenna_at, head_dx, head_dy);
    place(v, &v->eye[0], a->eye_at[0], head_dx + look * v->scale, head_dy);
    place(v, &v->eye[1], a->eye_at[1], head_dx + look * v->scale, head_dy);
    place(v, &v->mouth, a->mouth_at, head_dx, head_dy);
    place(v, &v->shadow, a->shadow_at, 0, 0);
    apply_props(v, &pose->props, shake, -lift, shake + wobble, nod - lift);
    return next;
}

void buddy_view_hide(buddy_view_t *v)
{
    gfx_sprite_t *all[] = {&v->shadow, &v->torso, &v->head, &v->antenna, &v->eye[0], &v->eye[1], &v->mouth,
                           &v->can_shadow, &v->can, &v->port, &v->rig[0], &v->rig[1], &v->battery, &v->alert,
                           &v->bolt[0], &v->bolt[1], &v->bolt[2], &v->bolt[3], &v->fx[0], &v->fx[1],
                           &v->part[0], &v->part[1], &v->part[2], &v->part[3], &v->part[4]};
    _Static_assert(BUDDY_MAX_BOLTS == 4 && BUDDY_FX_COUNT == 2 && BUDDY_PARTICLES == 5,
                   "list every bolt, effect and particle sprite above");
    for (unsigned i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        hide(all[i]);
    }
}

bool buddy_view_hit(const buddy_view_t *v, int x, int y)
{
    gfx_rect_t r = gfx_rect_union(gfx_sprite_bounds(&v->head), gfx_sprite_bounds(&v->torso));
    r = gfx_rect_union(r, gfx_sprite_bounds(&v->antenna));
    if (gfx_rect_is_empty(r)) {
        return false; // hidden
    }
    const int margin = 2 * v->scale; // fingers are wider than outlines
    r.x -= margin;
    r.y -= margin;
    r.w += 2 * margin;
    r.h += 2 * margin;
    return gfx_rect_contains_point(r, x, y);
}

// --- Trigger button ----------------------------------------------------------------------------------------------

bool buddy_button_init(buddy_button_t *b, gfx_scene_t *scene, gfx_rect_t area, int scale, int z)
{
    memset(b, 0, sizeof(*b));
    const buddy_art_t *a = buddy_art_get();
    if (!a) {
        return false;
    }
    const gfx_image_t *plate = &a->props.plate, *can = &a->props.can[BUDDY_CAN_LEVELS - 1];
    const int fit_w = area.w * 80 / 100 / plate->w, fit_h = area.h * 60 / 100 / plate->h;
    scale = scale < fit_w ? scale : fit_w;
    scale = scale < fit_h ? scale : fit_h;
    if (scale < 1 || scale > 255) {
        return false;
    }
    b->plate = (gfx_sprite_t){.image = plate, .palette = &a->palette, .scale = (uint8_t)scale};
    b->plate.x = area.x + (area.w - plate->w * scale) / 2;
    b->plate.y = area.y + (area.h - plate->h * scale) / 2;
    b->can = (gfx_sprite_t){.image = can, .palette = &a->palette, .scale = (uint8_t)scale};
    b->can.x = b->plate.x + (plate->w - can->w) / 2 * scale;
    b->can.y = b->plate.y + (plate->h - can->h) / 2 * scale;
    return gfx_scene_add(scene, &b->plate, z) && gfx_scene_add(scene, &b->can, z + 1);
}

void buddy_button_apply(buddy_button_t *b, bool visible, bool available)
{
    b->plate.flags = (uint8_t)(visible ? b->plate.flags & ~GFX_SPRITE_HIDDEN : b->plate.flags | GFX_SPRITE_HIDDEN);
    b->can.flags = (uint8_t)(visible && available ? b->can.flags & ~GFX_SPRITE_HIDDEN
                                                  : b->can.flags | GFX_SPRITE_HIDDEN);
}

bool buddy_button_hit(const buddy_button_t *b, int x, int y)
{
    return gfx_rect_contains_point(gfx_sprite_bounds(&b->plate), x, y);
}
