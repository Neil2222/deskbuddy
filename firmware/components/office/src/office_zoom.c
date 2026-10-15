#include "office_zoom.h"

#include <math.h>

#include "buddy_art.h"

// --- State --------------------------------------------------------------------------------------------------------

void office_zoom_init(office_zoom_t *z)
{
    *z = (office_zoom_t){0};
}

void office_zoom_request(office_zoom_t *z, bool office)
{
    z->want_office = office;
}

bool office_zoom_busy(const office_zoom_t *z)
{
    return z->state == OFFICE_ZOOM_ENTERING || z->state == OFFICE_ZOOM_LEAVING;
}

office_zoom_step_t office_zoom_step(office_zoom_t *z, office_switch_t *sw, uint32_t now)
{
    office_zoom_step_t out = {0};
    switch (z->state) {
    case OFFICE_ZOOM_NORMAL:
        if (!z->want_office) {
            break;
        }
        z->state = OFFICE_ZOOM_ENTERING;
        z->start = now;
        z->asked = false;
        out.menu_hide = true;
        // fall through
    case OFFICE_ZOOM_ENTERING:
        if (!z->asked && now - z->start >= OFFICE_ENTER_MS) {
            office_switch_request(sw, true); // his dark face becomes the clock
            z->asked = true;
        }
        if (z->asked && sw->office && !office_switch_busy(sw)) {
            z->state = OFFICE_ZOOM_CLOCK; // the clock is drawn
            break;
        }
        out.animate = true;
        break;
    case OFFICE_ZOOM_CLOCK:
        if (z->want_office) {
            break;
        }
        office_switch_request(sw, false); // the clock gives way to his dark face
        z->state = OFFICE_ZOOM_LEAVING;
        z->running = false;
        z->menu_back = false;
        // fall through
    case OFFICE_ZOOM_LEAVING:
        out.animate = true;
        if (!z->running) {
            if (sw->office || office_switch_busy(sw)) {
                break; // his face is still being drawn (it is drawn at the start of the timeline)
            }
            z->running = true;
            z->start = now;
        }
        if (!z->menu_back && now - z->start >= OFFICE_MENU_BACK_AT) {
            z->menu_back = true;
            out.menu_show = true;
        }
        if (now - z->start >= OFFICE_LEAVE_MS) {
            z->state = OFFICE_ZOOM_NORMAL;
            out.animate = false;
            out.back = true;
            out.menu_show |= !z->menu_back;
            z->menu_back = true;
        }
        break;
    }
    return out;
}

// --- Camera -------------------------------------------------------------------------------------------------------

// His face screen in art pixels (from the artwork: the head's screen-coloured pixels).
static gfx_rect_t face_screen(void)
{
    static gfx_rect_t r;
    if (r.w > 0) {
        return r;
    }
    const buddy_art_t *a = buddy_art_get();
    int x0 = a->head.w, y0 = a->head.h, x1 = -1, y1 = -1;
    for (int y = 0; y < a->head.h; y++) {
        for (int x = 0; x < a->head.w; x++) {
            if (a->head.px[y * a->head.w + x] == BUDDY_PX_SCREEN) {
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0;
                y1 = y > y1 ? y : y1;
            }
        }
    }
    r = (gfx_rect_t){a->head_at.x + x0, a->head_at.y + y0, x1 - x0 + 1, y1 - y0 + 1};
    return r;
}

office_camera_t office_zoom_face_camera(gfx_rect_t screen)
{
    const gfx_rect_t f = face_screen();
    const int sx = (screen.w + f.w - 1) / f.w, sy = (screen.h + f.h - 1) / f.h;
    const int s = sx > sy ? sx : sy;
    // The face screen's centre (in half art pixels, so odd sizes centre exactly) on the screen's centre.
    return (office_camera_t){s, screen.x + screen.w / 2 - (2 * f.x + f.w) * s / 2,
                             screen.y + screen.h / 2 - (2 * f.y + f.h) * s / 2};
}

// Gentle start, a satisfying acceleration, a smooth arrival.
static float ease(float p)
{
    if (p <= 0) {
        return 0;
    }
    if (p >= 1) {
        return 1;
    }
    return p < 0.5f ? 4 * p * p * p : 1 - powf(-2 * p + 2, 3) / 2;
}

// The camera e of the way (0..1) from home to the face. The scale grows exponentially, so the approach feels even;
// the face's centre glides from where it is on the stage to the middle of the screen.
static office_camera_t camera_at(float e, office_camera_t home, gfx_rect_t screen)
{
    if (e <= 0) {
        return home;
    }
    const office_camera_t face = office_zoom_face_camera(screen);
    if (e >= 1) {
        return face;
    }
    const gfx_rect_t f = face_screen();
    const float fx = f.x + f.w / 2.0f, fy = f.y + f.h / 2.0f; // art point that is zoomed into
    const float s = home.scale * powf((float)face.scale / home.scale, e);
    const int si = (int)lroundf(s);
    const float px = home.x0 + fx * home.scale, py = home.y0 + fy * home.scale; // its screen point at home...
    const float qx = face.x0 + fx * face.scale, qy = face.y0 + fy * face.scale; // ...and at the end
    return (office_camera_t){si, (int)lroundf(px + (qx - px) * e - fx * si),
                             (int)lroundf(py + (qy - py) * e - fy * si)};
}

// --- Pose ---------------------------------------------------------------------------------------------------------

typedef struct {
    uint16_t until; // ms on the timeline
    uint8_t eyes, mouth, antenna, tint, lift;
} beat_t;

static const beat_t k_enter[] = {
    {100, BUDDY_EYES_WIDE, BUDDY_MOUTH_OPEN, BUDDY_ANTENNA_EXCITED, BUDDY_TINT_NORMAL, 1}, // oh! you want me?
    {160, BUDDY_EYES_OPEN, BUDDY_MOUTH_SMILE, BUDDY_ANTENNA_CALM, BUDDY_TINT_NORMAL, 0},
    {240, BUDDY_EYES_CLOSED, BUDDY_MOUTH_SMILE, BUDDY_ANTENNA_CALM, BUDDY_TINT_NORMAL, 0}, // blink
    {940, BUDDY_EYES_OPEN, BUDDY_MOUTH_SMILE, BUDDY_ANTENNA_CALM, BUDDY_TINT_NORMAL, 0},   // here I come
    {1020, BUDDY_EYES_HALF, BUDDY_MOUTH_SMILE, BUDDY_ANTENNA_CALM, BUDDY_TINT_NORMAL, 0},
    {1120, BUDDY_EYES_CLOSED, BUDDY_MOUTH_NEUTRAL, BUDDY_ANTENNA_CALM, BUDDY_TINT_NORMAL, 0},
    {1190, BUDDY_EYES_CLOSED, BUDDY_MOUTH_NEUTRAL, BUDDY_ANTENNA_CALM, BUDDY_TINT_DROWSY, 0},
    {UINT16_MAX, BUDDY_EYES_CLOSED, BUDDY_MOUTH_NEUTRAL, BUDDY_ANTENNA_CALM, BUDDY_TINT_DARK, 0}, // lights out
};
static const beat_t k_leave[] = {
    {80, BUDDY_EYES_CLOSED, BUDDY_MOUTH_NEUTRAL, BUDDY_ANTENNA_CALM, BUDDY_TINT_DARK, 0},
    {160, BUDDY_EYES_CLOSED, BUDDY_MOUTH_NEUTRAL, BUDDY_ANTENNA_CALM, BUDDY_TINT_DROWSY, 0}, // the screen glows
    {240, BUDDY_EYES_HALF, BUDDY_MOUTH_NEUTRAL, BUDDY_ANTENNA_CALM, BUDDY_TINT_NORMAL, 0},
    {320, BUDDY_EYES_OPEN, BUDDY_MOUTH_SMILE, BUDDY_ANTENNA_CALM, BUDDY_TINT_NORMAL, 0},
    {400, BUDDY_EYES_CLOSED, BUDDY_MOUTH_SMILE, BUDDY_ANTENNA_CALM, BUDDY_TINT_NORMAL, 0}, // blink
    {1080, BUDDY_EYES_OPEN, BUDDY_MOUTH_SMILE, BUDDY_ANTENNA_CALM, BUDDY_TINT_NORMAL, 0},  // back to his place
    {1140, BUDDY_EYES_SQUEEZE, BUDDY_MOUTH_OPEN, BUDDY_ANTENNA_EXCITED, BUDDY_TINT_NORMAL, 1}, // a big stretch...
    {1260, BUDDY_EYES_SQUEEZE, BUDDY_MOUTH_OPEN, BUDDY_ANTENNA_EXCITED, BUDDY_TINT_NORMAL, 3},
    {1320, BUDDY_EYES_SQUEEZE, BUDDY_MOUTH_SMILE, BUDDY_ANTENNA_CALM, BUDDY_TINT_NORMAL, 1},
    {1380, BUDDY_EYES_OPEN, BUDDY_MOUTH_SMILE, BUDDY_ANTENNA_CALM, BUDDY_TINT_NORMAL, 0},
    {1440, BUDDY_EYES_CLOSED, BUDDY_MOUTH_SMILE, BUDDY_ANTENNA_CALM, BUDDY_TINT_NORMAL, 0}, // ...and a blink
    {UINT16_MAX, BUDDY_EYES_OPEN, BUDDY_MOUTH_SMILE, BUDDY_ANTENNA_CALM, BUDDY_TINT_NORMAL, 0},
};

uint32_t office_zoom_frame(const office_zoom_t *z, uint32_t now, office_camera_t home, gfx_rect_t screen,
                           office_camera_t *cam, buddy_pose_t *pose)
{
    const bool entering = z->state != OFFICE_ZOOM_LEAVING;
    const uint32_t t = entering || z->running ? now - z->start : 0;
    const beat_t *keys = entering ? k_enter : k_leave;
    while (keys->until != UINT16_MAX && t >= keys->until) { // the last beat lasts
        keys++;
    }
    *pose = (buddy_pose_t){.state = BUDDY_STATE_IDLE, .eyes = keys->eyes, .mouth = keys->mouth,
                           .antenna = keys->antenna, .tint = keys->tint, .lift = keys->lift};
    uint32_t next = keys->until == UINT16_MAX ? UINT32_MAX : keys->until - t;

    const uint32_t at = entering ? OFFICE_ZOOM_IN_AT : OFFICE_ZOOM_OUT_AT;
    const uint32_t ms = entering ? OFFICE_ZOOM_IN_MS : OFFICE_ZOOM_OUT_MS;
    const float p = t < at ? 0 : (float)(t - at) / ms;
    const float e = entering ? ease(p) : 1 - ease(p);
    *cam = camera_at(e, home, screen);
    if (t < at) {
        next = next < at - t ? next : at - t;
    } else if (t < at + ms) {
        next = next < OFFICE_ZOOM_FRAME_MS ? next : OFFICE_ZOOM_FRAME_MS;
    }
    if (entering && !z->asked && t < OFFICE_ENTER_MS) {
        next = next < OFFICE_ENTER_MS - t ? next : OFFICE_ENTER_MS - t;
    }
    if (!entering && t < OFFICE_MENU_BACK_AT) {
        next = next < OFFICE_MENU_BACK_AT - t ? next : OFFICE_MENU_BACK_AT - t;
    }
    if (!entering && t < OFFICE_LEAVE_MS) {
        next = next < OFFICE_LEAVE_MS - t ? next : OFFICE_LEAVE_MS - t;
    }
    return next > 0 ? next : 1;
}
