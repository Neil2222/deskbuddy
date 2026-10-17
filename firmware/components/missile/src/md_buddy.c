#include "md_buddy.h"

#define AIM_SIDE_PX 80 // a target this far to either side of him: his eyes go that way
#define AIM_UP_Y 380   // a target above this: his eyes go up

// Where he looks: at the shot just fired, else at the missile closest to the desk, else up at the sky.
static void aim(const md_t *g, uint32_t now, bool shot, buddy_game_t *f)
{
    int tx = MD_BUDDY_X, ty = MD_SKY_TOP + 150;
    if (shot) {
        tx = g->shot_x;
        ty = g->shot_y;
    } else {
        int best = -1;
        for (int i = 0; i < MD_MAX_MISSILES; i++) {
            const md_missile_t *m = &g->missile[i];
            if (m->state != MD_MISSILE_FLYING) {
                continue;
            }
            int x, y;
            md_missile_head(m, md_world_ms(g, now), &x, &y);
            if (y > best) {
                best = y;
                tx = x;
                ty = y;
            }
        }
    }
    const int dx = tx - MD_BUDDY_X;
    f->aim_x = (int8_t)(dx < -AIM_SIDE_PX ? -1 : dx > AIM_SIDE_PX ? 1 : 0);
    f->aim_y = (int8_t)(ty < AIM_UP_Y ? -1 : 0);
}

buddy_game_t md_buddy_face(const md_t *g, uint32_t now)
{
    buddy_game_t f = {BUDDY_GAME_MD_AIM, BUDDY_GAME_EVEN, 0, g->phase_at, 0, 0};
    const bool firing = g->shots > 0 && now - g->shot_at < MD_FIRE_FACE_MS;
    aim(g, now, firing, &f);
    if (g->phase == MD_OVER) {
        f.face = BUDDY_GAME_MD_OVER;
        f.since = g->phase_at;
    } else if (g->stunned) {
        f.face = BUDDY_GAME_MD_HIT;
        f.since = g->stun_at;
    } else if (g->hot) {
        f.face = BUDDY_GAME_MD_HOT;
        f.since = g->hot_at;
    } else if (g->lost_at && now - g->lost_at < MD_LOST_FACE_MS) {
        f.face = BUDDY_GAME_MD_LOST;
        f.since = g->lost_at;
        f.aim_x = (int8_t)(md_item_x[g->lost_item % MD_ITEMS] < MD_BUDDY_X ? -1 : 1); // at what is left of it
    } else if (g->phase == MD_CLEAR) {
        f.face = BUDDY_GAME_MD_CHEER;
        f.since = g->phase_at;
    } else if (firing) {
        f.face = BUDDY_GAME_MD_FIRE;
        f.since = g->shot_at;
    } else if (g->panic) {
        f.face = BUDDY_GAME_MD_PANIC;
        f.since = g->panic_at;
    } else if (g->phase == MD_READY) {
        f.face = BUDDY_GAME_MD_READY;
        f.since = g->phase_at;
    } else if (g->smug_at && g->streak >= MD_SMUG_STREAK) {
        f.face = BUDDY_GAME_MD_SMUG;
        f.since = g->smug_at;
    }
    return f;
}
