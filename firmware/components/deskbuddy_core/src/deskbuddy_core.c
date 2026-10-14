#include "deskbuddy_core.h"

#include <string.h>

#define CHARACTER_Z 10

// Wrap-around safe "a is at or after b".
static bool at_or_after(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) >= 0;
}

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

void core_config_defaults(core_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->screen_w = CORE_SCREEN_W;
    cfg->screen_h = CORE_SCREEN_H;
    cfg->stage_percent = 65;
    cfg->panel_left = false;
    cfg->seed = 1;
    cfg->demo_poke_ms = 0;
}

bool core_init(core_t *c, const core_config_t *cfg, gfx_color_t *strip, int strip_px, uint32_t now_ms)
{
    memset(c, 0, sizeof(*c));
    c->cfg = *cfg;
    const int w = cfg->screen_w, h = cfg->screen_h;
    c->layout = layout_compute(w, h, cfg->stage_percent, cfg->panel_left, CORE_X_ALIGN);
    if (!gfx_scene_init(&c->scene, w, h, layout_background, &c->layout, strip, strip_px, CORE_X_ALIGN)) {
        return false;
    }
    gfx_dirty_init(&c->changes, w, h, CORE_X_ALIGN);
    gfx_dirty_init(&c->redraw, w, h, CORE_X_ALIGN);
    gfx_repair_init(&c->repair, w, h, CORE_X_ALIGN);
    if (!buddy_view_init(&c->view, &c->scene, c->layout.stage, CHARACTER_Z, now_ms)) {
        return false;
    }
    buddy_brain_init(&c->brain, now_ms, cfg->seed);
    gfx_scene_invalidate(&c->scene, c->layout.screen); // both buffers start undefined: the first frame is full
    c->now = now_ms;
    c->next_demo = now_ms + cfg->demo_poke_ms;
    return true;
}

static void poke(core_t *c, uint32_t t)
{
    buddy_brain_poke(&c->brain, t);
    c->count.reactions++;
}

bool core_handle_event(core_t *c, const core_event_t *ev)
{
    c->count.events++;
    if (!at_or_after(c->now, ev->time_ms)) {
        c->now = ev->time_ms; // keep the clock monotonic for the next update
    }
    switch (ev->type) {
    case CORE_EVENT_TOUCH_DOWN:
        c->touch.x = ev->touch.x;
        c->touch.y = ev->touch.y;
        c->touch.down = true;
        if (buddy_view_hit(&c->view, ev->touch.x, ev->touch.y)) {
            poke(c, ev->time_ms);
            return true;
        }
        return false;
    case CORE_EVENT_TOUCH_MOVE:
        c->touch.x = ev->touch.x;
        c->touch.y = ev->touch.y;
        return false;
    case CORE_EVENT_TOUCH_UP:
        c->touch.down = false;
        return false;
    case CORE_EVENT_SIGNAL:
        if (ev->signal.id == CORE_SIGNAL_POKE) {
            poke(c, ev->time_ms);
            return true;
        }
        break;
    default:
        break;
    }
    c->count.ignored++;
    return false;
}

uint32_t core_update(core_t *c, uint32_t now_ms)
{
    if (!at_or_after(now_ms, c->now)) {
        now_ms = c->now; // an event was stamped later than this update: never go back in time
    }
    c->now = now_ms;
    if (c->cfg.demo_poke_ms && at_or_after(now_ms, c->next_demo)) {
        poke(c, now_ms);
        c->next_demo = now_ms + c->cfg.demo_poke_ms;
    }
    uint32_t next = buddy_brain_tick(&c->brain, now_ms, &c->pose);
    next = min_u32(next, buddy_view_apply(&c->view, &c->pose, now_ms));
    if (c->cfg.demo_poke_ms) {
        next = min_u32(next, c->next_demo - now_ms);
    }
    return c->second_frame_pending ? 0 : next;
}

const gfx_dirty_t *core_collect(core_t *c)
{
    if (c->second_frame_pending) {
        // The first frame was a full redraw and is in the repair history, so any change makes the other buffer
        // redraw fully too: touch one pixel to get there now rather than at the next animation step.
        gfx_scene_invalidate(&c->scene, (gfx_rect_t){0, 0, 1, 1});
        c->second_frame_pending = false;
    }
    gfx_dirty_clear(&c->changes);
    gfx_scene_collect(&c->scene, &c->changes);
    if (c->changes.count == 0) {
        return NULL;
    }
    gfx_repair_next(&c->repair, &c->changes, &c->redraw);
    if (++c->count.frames == 1) {
        c->second_frame_pending = true;
    }
    return &c->redraw;
}

void core_render(const core_t *c, const gfx_dirty_t *redraw, const gfx_surface_t *back)
{
    gfx_scene_render(&c->scene, redraw, back);
}

uint32_t core_verify(const core_t *c, const gfx_surface_t *frame, const gfx_surface_t *scratch, int *first_x,
                     int *first_y)
{
    gfx_dirty_t all;
    gfx_dirty_init(&all, c->scene.w, c->scene.h, CORE_X_ALIGN);
    gfx_dirty_add(&all, c->layout.screen);
    gfx_scene_render(&c->scene, &all, scratch);
    uint32_t bad = 0;
    for (int y = 0; y < c->scene.h; y++) {
        const gfx_color_t *a = frame->px + (size_t)y * frame->stride;
        const gfx_color_t *b = scratch->px + (size_t)y * scratch->stride;
        if (memcmp(a, b, (size_t)c->scene.w * sizeof(gfx_color_t)) == 0) {
            continue;
        }
        for (int x = 0; x < c->scene.w; x++) {
            if (a[x] != b[x]) {
                if (bad++ == 0) {
                    if (first_x) {
                        *first_x = x;
                    }
                    if (first_y) {
                        *first_y = y;
                    }
                }
            }
        }
    }
    return bad;
}

uint32_t core_frame_crc32(const gfx_surface_t *s)
{
    static const uint32_t k_nibble[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
    };
    uint32_t crc = 0xFFFFFFFFu;
    for (int y = 0; y < s->h; y++) {
        const gfx_color_t *row = s->px + (size_t)y * s->stride;
        for (int x = 0; x < s->w; x++) {
            const uint8_t bytes[2] = {(uint8_t)(row[x] & 0xFF), (uint8_t)(row[x] >> 8)};
            for (int i = 0; i < 2; i++) {
                crc ^= bytes[i];
                crc = (crc >> 4) ^ k_nibble[crc & 0x0F];
                crc = (crc >> 4) ^ k_nibble[crc & 0x0F];
            }
        }
    }
    return ~crc;
}
