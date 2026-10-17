// Buddy Missile Defense, the rules (md.h): waves, shots, blasts and chains, the score, his eyes' heat, a missile on
// his head, lost items and the end of the game, his lines; plus how a slow player fares in the first waves.
#include <stdio.h>
#include <string.h>

#include "md.h"
#include "test.h"

static void tick_to(md_t *g, uint32_t *t, uint32_t to)
{
    while ((int32_t)(to - *t) > 0) {
        *t += 16; // about a frame
        if ((int32_t)(*t - to) > 0) {
            *t = to;
        }
        md_tick(g, *t);
    }
}

static int count_state(const md_t *g, md_missile_state_t s)
{
    int n = 0;
    for (int i = 0; i < MD_MAX_MISSILES; i++) {
        n += g->missile[i].state == s;
    }
    return n;
}


// A missile placed by hand: from (x0, MD_SKY_TOP) to target, launched at t.
static md_missile_t *place(md_t *g, int slot, int x0, int target, uint16_t fall, uint32_t t)
{
    md_missile_t *m = &g->missile[slot];
    *m = (md_missile_t){0};
    m->state = MD_MISSILE_FLYING;
    m->target = (uint8_t)target;
    m->x0 = (int16_t)x0;
    m->y0 = MD_SKY_TOP;
    m->x1 = target == MD_TARGET_BUDDY ? MD_BUDDY_X : md_item_x[target];
    m->y1 = target == MD_TARGET_BUDDY ? MD_BUDDY_Y : MD_ITEM_HIT_Y;
    m->fall_ms = fall;
    m->born = t;
    return m;
}

// A game in its first wave with nothing launched on its own, at *t (the wave, still owed its missiles, never ends).
static void quiet_game(md_t *g, uint32_t *t)
{
    md_init(g, 5);
    md_start(g, *t);
    tick_to(g, t, *t + MD_READY_MS + 20);
    g->next_spawn = *t + 100000000u;
}

// The first waves are slow and few; later ones get faster and fuller, up to a limit; no wave ever needs more missiles
// at once than there are slots (a missile also keeps its slot while its trail fades).
void test_md_waves_start_gentle_then_get_harder(void)
{
    const md_wave_t first = md_wave(1);
    CHECK(first.fall_ms >= 10000);
    CHECK(first.at_once <= 2);
    CHECK(first.missiles <= 6);
    CHECK(first.gap_min_ms >= 2000);
    md_wave_t prev = first;
    for (int w = 2; w <= 40; w++) {
        const md_wave_t now = md_wave(w);
        CHECK(now.fall_ms <= prev.fall_ms);
        CHECK(now.missiles >= prev.missiles);
        CHECK(now.at_once >= prev.at_once);
        CHECK(now.fall_ms >= 5000); // never the 2.4 s first proposed
        CHECK(now.gap_min_ms <= now.gap_max_ms && now.gap_min_ms > MD_TRAIL_FADE_MS);
        CHECK(now.at_once + 2 <= MD_MAX_MISSILES);
        CHECK(now.missiles <= MD_WAVE_MISSILES_MAX);
        prev = now;
    }
    CHECK(md_wave(40).missiles == MD_WAVE_MISSILES_MAX);
}

// A shot: its blast grows where the finger was; a missile inside it explodes and scores, and its own blast catches the
// next one (a chain), which counts double for the same shot.
void test_md_a_shot_catches_missiles_and_chains(void)
{
    md_t g;
    uint32_t t = 1000;
    quiet_game(&g, &t);
    CHECK_EQ(g.phase, MD_PLAYING);
    md_missile_t *a = place(&g, 0, 300, 1, 10000, t - 5000);  // half way down, at (255, 277)
    md_missile_t *b = place(&g, 1, 330, 1, 10000, t - 5000);  // its neighbour, 15 pixels away
    md_missile_t *far = place(&g, 2, 900, 4, 10000, t - 5000); // far away: untouched
    int ax, ay;
    md_missile_head(a, t, &ax, &ay);
    CHECK_EQ(md_fire(&g, ax, ay, t), MD_FIRED);
    tick_to(&g, &t, t + 150); // a at once, b once the blast has grown to it
    CHECK_EQ(a->state, MD_MISSILE_FADING);
    CHECK(a->shot_down);
    CHECK_EQ(b->state, MD_MISSILE_FADING); // caught by the shot or by a's blast
    CHECK(b->shot_down);
    CHECK_EQ(far->state, MD_MISSILE_FLYING);
    CHECK_EQ(g.score, MD_POINTS_MISSILE + 2 * MD_POINTS_MISSILE);
    CHECK_EQ(g.pop_mult, 2);
    CHECK_EQ(g.best, g.score);
    CHECK_EQ(g.items, (1u << MD_ITEMS) - 1);
    // The trails shrink back, then the slots are free again.
    tick_to(&g, &t, t + MD_TRAIL_FADE_MS + 20);
    CHECK_EQ(a->state, MD_MISSILE_FREE);
    CHECK_EQ(b->state, MD_MISSILE_FREE);
}

// Three with one shot count 1x, 2x and 3x; a fourth stays at 3x.
void test_md_multi_kills_score_more(void)
{
    md_t g;
    uint32_t t = 1000;
    quiet_game(&g, &t);
    for (int i = 0; i < 4; i++) { // four close together, half way down
        place(&g, i, 400 + i * 6, 2, 10000, t - 5000);
    }
    int x, y;
    md_missile_head(&g.missile[1], t, &x, &y);
    CHECK_EQ(md_fire(&g, x, y, t), MD_FIRED);
    tick_to(&g, &t, t + 900);
    CHECK_EQ(count_state(&g, MD_MISSILE_FADING) + count_state(&g, MD_MISSILE_FREE), MD_MAX_MISSILES);
    CHECK_EQ(g.score, MD_POINTS_MISSILE * (1 + 2 + 3 + 3));
    CHECK_EQ(g.pop_mult, 3);
    CHECK(md_line(&g, t - 600) != NULL); // "TRIPLE!" (said at once, a big moment)
}

// A missile that gets through destroys its item (once: a second one on the rubble changes nothing); he is dismayed and
// names it; when the last item goes the game is over: RETRY / QUIT after a moment, and a long wait closes it. The best
// score stays for the next game.
void test_md_lost_items_end_the_game(void)
{
    md_t g;
    uint32_t t = 1000;
    quiet_game(&g, &t);
    g.score = 300;
    g.best = 300;
    place(&g, 0, 100, 0, 2000, t);
    tick_to(&g, &t, t + 2100);
    CHECK_EQ(g.items, (1u << MD_ITEMS) - 2);
    CHECK_EQ(g.lost_item, 0);
    CHECK_EQ(g.wave_lost, 1);
    CHECK(strcmp(md_line(&g, t), "MY COFFEE!!") == 0 || strcmp(md_line(&g, t), "NOT THE COFFEE!") == 0);
    CHECK(g.missile[0].state == MD_MISSILE_FADING && !g.missile[0].shot_down);
    place(&g, 1, 120, 0, 2000, t); // again on the mug's rubble
    tick_to(&g, &t, t + 2100);
    CHECK_EQ(md_items_left(&g), 5);
    for (int i = 1; i < MD_ITEMS; i++) {
        place(&g, i, 100 + 100 * i, i, 2000, t);
    }
    place(&g, 7, 600, MD_TARGET_BUDDY, 8000, t); // still in the air when the game is lost
    md_fire(&g, 900, 100, t + 1990);              // a blast still growing then
    tick_to(&g, &t, t + 2100);
    CHECK_EQ(md_items_left(&g), 0);
    CHECK_EQ(g.phase, MD_OVER);
    // From the moment it was lost the world stands still: nothing moves, grows, lands or scores, and nothing asks for
    // frames (only the buttons and his idle timeout are still ahead).
    md_t frozen = g;
    CHECK_EQ(g.missile[7].state, MD_MISSILE_FLYING);
    CHECK(md_tick(&g, t + 10) > MD_FRAME_MS);
    tick_to(&g, &t, t + 20000);
    CHECK(memcmp(g.missile, frozen.missile, sizeof(g.missile)) == 0);
    CHECK(memcmp(g.blast, frozen.blast, sizeof(g.blast)) == 0);
    CHECK_EQ(g.score, frozen.score);
    CHECK(!g.stunned);
    CHECK_EQ(md_world_ms(&g, t), g.phase_at);
    int fx, fy, lx, ly;
    md_missile_head(&g.missile[7], md_world_ms(&g, g.phase_at), &fx, &fy);
    md_missile_head(&g.missile[7], md_world_ms(&g, t), &lx, &ly);
    CHECK(fx == lx && fy == ly);
    CHECK(md_line(&g, t) && strchr(md_line(&g, t), '\n')); // two lines: "THEY HAD... / SO MANY..."
    CHECK_EQ(md_fire(&g, 500, 200, t), MD_IGNORED);
    CHECK(!md_buttons(&g, g.phase_at + MD_OVER_BUTTONS_MS - 1));
    CHECK(md_buttons(&g, g.phase_at + MD_OVER_BUTTONS_MS));
    CHECK(!md_bored(&g, g.phase_at + MD_OVER_BUTTONS_MS + MD_OVER_IDLE_MS - 1)); // counted from the buttons...
    CHECK(md_bored(&g, g.phase_at + MD_OVER_BUTTONS_MS + MD_OVER_IDLE_MS));
    md_touched(&g, g.phase_at + 5000); // ...or from a touch after them
    CHECK(!md_bored(&g, g.phase_at + 5000 + MD_OVER_IDLE_MS - 1));
    CHECK(md_bored(&g, g.phase_at + 5000 + MD_OVER_IDLE_MS));
    // RETRY: a fresh game, the best score kept; the frozen missile's trail shrinks back into where it stood.
    int hx, hy;
    md_missile_head(&g.missile[7], md_world_ms(&g, t), &hx, &hy);
    md_start(&g, t);
    CHECK_EQ(g.phase, MD_READY);
    CHECK_EQ(g.wave, 1);
    CHECK_EQ(g.score, 0);
    CHECK_EQ(g.best, 300);
    CHECK_EQ(md_items_left(&g), MD_ITEMS);
    CHECK_EQ(g.missile[7].state, MD_MISSILE_FADING);
    CHECK(!g.missile[7].shot_down && g.missile[7].ex == hx && g.missile[7].ey == hy && g.missile[7].end_at == t);
    CHECK_EQ(count_state(&g, MD_MISSILE_FLYING), 0);
    tick_to(&g, &t, t + MD_TRAIL_FADE_MS + 20);
    CHECK_EQ(count_state(&g, MD_MISSILE_FREE), MD_MAX_MISSILES);
    CHECK_EQ(g.score, 0); // nothing of the old game scores in the new one
}

// A missile on his head dazes him: no shots for MD_STUN_MS (he says so when you try), nothing on the desk lost.
void test_md_a_hit_on_buddy_only_stuns(void)
{
    md_t g;
    uint32_t t = 1000;
    quiet_game(&g, &t);
    place(&g, 0, 500, MD_TARGET_BUDDY, 2000, t);
    tick_to(&g, &t, t + 2010);
    CHECK(g.stunned);
    CHECK_EQ(md_items_left(&g), MD_ITEMS);
    CHECK_EQ(g.phase, MD_PLAYING);
    CHECK(md_line(&g, t) != NULL);
    const uint32_t hit = g.stun_at;
    CHECK_EQ(md_fire(&g, 500, 200, hit + MD_STUN_MS - 30), MD_REFUSED_STUNNED);
    t = hit + MD_STUN_MS - 30;
    tick_to(&g, &t, hit + MD_STUN_MS + 20);
    CHECK(!g.stunned);
    CHECK_EQ(md_fire(&g, 500, 200, t), MD_FIRED);
}

// Shots heat his eyes: fired too fast they overheat (no shots for MD_OVERHEAT_MS, he says so); they cool at
// MD_COOL_PER_S, so a steady pace never overheats.
void test_md_eyes_overheat_and_cool(void)
{
    md_t g;
    uint32_t t = 1000;
    quiet_game(&g, &t);
    const int to_overheat = (100 + MD_HEAT_PER_SHOT - 1) / MD_HEAT_PER_SHOT;
    for (int i = 0; i < to_overheat; i++) {
        CHECK_EQ(md_fire(&g, 200 + i * 100, 200, t), MD_FIRED);
        t += 20;
    }
    CHECK(g.hot);
    CHECK_EQ(md_heat(&g, g.hot_at), 100);
    CHECK_EQ(md_fire(&g, 500, 200, t), MD_REFUSED_HOT);
    tick_to(&g, &t, g.hot_at + MD_OVERHEAT_MS + 20);
    CHECK(!g.hot);
    CHECK(md_heat(&g, t) < 100 - MD_COOL_PER_S); // cooled during the lockout
    CHECK_EQ(md_fire(&g, 500, 200, t), MD_FIRED);
    // A steady pace: one shot every time the eyes have shed a shot's worth of heat.
    const uint32_t pace = 1000u * MD_HEAT_PER_SHOT / MD_COOL_PER_S + 20;
    for (int i = 0; i < 20; i++) {
        tick_to(&g, &t, t + pace);
        CHECK_EQ(md_fire(&g, 100 + i * 40, 150, t), MD_FIRED);
    }
    CHECK(!g.hot);
}

// A shot at empty sky is a miss: the streak is gone and he covers it up ("WARNING SHOT.").
void test_md_a_miss_breaks_the_streak(void)
{
    md_t g;
    uint32_t t = 1000;
    quiet_game(&g, &t);
    g.streak = 4;
    g.say = MD_SAY_NONE;
    CHECK_EQ(md_fire(&g, 800, 100, t), MD_FIRED);
    tick_to(&g, &t, t + MD_BLAST_GROW_MS + MD_BLAST_HOLD_MS + MD_BLAST_SHRINK_MS + 30);
    CHECK_EQ(g.streak, 0);
    CHECK_EQ(g.say, MD_SAY_MISS);
}

// Taps below the sky aim just above the desk; taps past the edges stay on screen.
void test_md_shots_stay_in_the_sky(void)
{
    md_t g;
    uint32_t t = 1000;
    quiet_game(&g, &t);
    CHECK_EQ(md_fire(&g, -40, 590, t), MD_FIRED);
    CHECK_EQ(g.shot_x, 0);
    CHECK_EQ(g.shot_y, MD_FIRE_MAX_Y);
    t += 600;
    CHECK_EQ(md_fire(&g, 2000, 0, t), MD_FIRED);
    CHECK_EQ(g.shot_x, MD_W - 1);
    CHECK_EQ(g.shot_y, MD_FIRE_MIN_Y);
}

// A frame that comes late is stepped through, not jumped: a fast missile crossing a blast between two frames is still
// caught.
void test_md_late_frames_never_skip_a_blast(void)
{
    md_t g;
    uint32_t t = 1000;
    quiet_game(&g, &t);
    md_missile_t *m = place(&g, 0, md_item_x[3], 3, 400, t); // straight down, fast (~1.15 px/ms)
    CHECK_EQ(md_fire(&g, md_item_x[3], 300, t), MD_FIRED);  // ahead of it
    // Two late frames: before the first the head is still above the blast, by the second it is through it (the blast
    // is still growing: checked only at the frames, it would never have caught it).
    t += 180;
    md_tick(&g, t);
    CHECK_EQ(m->state, MD_MISSILE_FLYING);
    t += 80;
    md_tick(&g, t);
    CHECK_EQ(m->state, MD_MISSILE_FADING);
    CHECK(m->shot_down);
}

// Shooting everything clears the wave: a bonus per item still standing, then the next wave, faster.
static void autoplay(md_t *g, uint32_t *t, uint32_t to, uint32_t reaction_ms, uint32_t min_gap_ms, int miss_px,
                     uint32_t *last_shot)
{
    while ((int32_t)(to - *t) > 0) {
        *t += 16;
        md_tick(g, *t);
        if (*t - *last_shot < min_gap_ms || g->hot || g->stunned) {
            continue;
        }
        // The lowest missile the player has seen for reaction_ms, aimed where it will be in reaction_ms.
        int best = -1, best_y = -1;
        for (int i = 0; i < MD_MAX_MISSILES; i++) {
            const md_missile_t *m = &g->missile[i];
            if (m->state != MD_MISSILE_FLYING || *t - m->born < reaction_ms) {
                continue;
            }
            int x, y;
            md_missile_head(m, *t, &x, &y);
            if (y > best_y) {
                best = i;
                best_y = y;
            }
        }
        if (best >= 0) {
            int x, y;
            md_missile_head(&g->missile[best], *t + reaction_ms / 2, &x, &y);
            const int err = (int)(g->rng % (uint32_t)(2 * miss_px + 1)) - miss_px; // aim is not perfect
            if (md_fire(g, x + err, y - err / 2, *t) == MD_FIRED) {
                *last_shot = *t;
            }
        }
    }
}

void test_md_cleared_waves_pay_and_the_next_is_harder(void)
{
    md_t g;
    md_init(&g, 21);
    uint32_t t = 5000, last = 0;
    md_start(&g, t);
    autoplay(&g, &t, t + 60000, 200, 300, 0, &last);
    CHECK(g.wave >= 2); // a quick, accurate player gets through the first wave
    // Back to a clear moment: run until the next CLEAR and check its bonus.
    uint32_t guard = t + 120000;
    while (g.phase != MD_CLEAR && (int32_t)(guard - t) > 0) {
        autoplay(&g, &t, t + 16, 200, 300, 0, &last);
    }
    CHECK_EQ(g.phase, MD_CLEAR);
    const uint32_t before = g.score;
    CHECK(g.saved == md_items_left(&g) && g.saved > 0);
    const int wave = g.wave;
    tick_to(&g, &t, g.phase_at + MD_CLEAR_MS + 20);
    CHECK_EQ(g.phase, MD_READY);
    CHECK_EQ(g.wave, wave + 1);
    CHECK_EQ(g.score, before); // the bonus was added when the wave was cleared
    CHECK(md_wave(g.wave).fall_ms <= md_wave(wave).fall_ms);
    tick_to(&g, &t, g.phase_at + MD_READY_MS + 400);
    CHECK_EQ(g.phase, MD_PLAYING);
    CHECK(count_state(&g, MD_MISSILE_FLYING) >= 1);
}

// Approachable: a slow, slightly inaccurate player (sees a missile after 0.9 s, one shot a second at most, aims up to
// 18 px off) survives the first three waves with most of the desk standing.
void test_md_first_waves_are_approachable(void)
{
    int worst = MD_ITEMS, total = 0;
    for (uint32_t seed = 1; seed <= 12; seed++) {
        md_t g;
        md_init(&g, seed * 7919u);
        uint32_t t = 1000, last = 0;
        md_start(&g, t);
        uint32_t guard = t + 600000;
        while (g.wave <= 3 && g.phase != MD_OVER && (int32_t)(guard - t) > 0) {
            autoplay(&g, &t, t + 16, 900, 1000, 18, &last);
        }
        CHECK(g.phase != MD_OVER);
        CHECK(g.wave == 4);
        worst = md_items_left(&g) < worst ? md_items_left(&g) : worst;
        total += md_items_left(&g);
    }
    printf("    slow player after wave 3: %d items left at worst, %.1f on average (of %d)\n", worst, total / 12.0,
           MD_ITEMS);
    CHECK(worst >= 3);
}

// Nobody playing: the missiles take the desk item by item and the game ends by itself (in a few minutes at most).
void test_md_unplayed_game_ends_by_itself(void)
{
    md_t g;
    md_init(&g, 77);
    uint32_t t = 1000;
    md_start(&g, t);
    const uint32_t start = t;
    while (g.phase != MD_OVER && t - start < 900000) {
        t += 16;
        md_tick(&g, t);
    }
    CHECK_EQ(g.phase, MD_OVER);
    printf("    unplayed: over after %u s in wave %d\n", (unsigned)((t - start) / 1000), g.wave);
    CHECK(t - start < 600000);
}

// The same seed and the same taps at the same times give the same game, also across the clock's wrap-around.
void test_md_is_deterministic_across_wraparound(void)
{
    md_t a, b;
    md_init(&a, 99);
    md_init(&b, 99);
    uint32_t ta = 0xFFFF0000u, tb = 0xFFFF0000u, la = 0, lb = 0;
    md_start(&a, ta);
    md_start(&b, tb);
    autoplay(&a, &ta, ta + 90000, 300, 400, 6, &la);
    autoplay(&b, &tb, tb + 90000, 300, 400, 6, &lb);
    CHECK(ta < 0x10000000u); // wrapped
    CHECK_EQ(a.score, b.score);
    CHECK_EQ(a.wave, b.wave);
    CHECK_EQ(a.items, b.items);
    CHECK(memcmp(a.missile, b.missile, sizeof(a.missile)) == 0);
    CHECK(a.wave >= 2);
}

// Between frames nothing changes unless md_tick says so: while anything moves it asks for frames; otherwise it names
// the next moment (the end of "WAVE n", the next launch, the buttons).
void test_md_tick_says_when_next(void)
{
    md_t g;
    md_init(&g, 3);
    CHECK_EQ(md_tick(&g, 100), UINT32_MAX);
    uint32_t t = 1000;
    md_start(&g, t);
    const uint32_t n = md_tick(&g, t + 100);
    CHECK(n >= 1 && n <= MD_READY_MS - 100);
    tick_to(&g, &t, t + MD_READY_MS + 600);
    CHECK_EQ(g.phase, MD_PLAYING);
    CHECK_EQ(md_tick(&g, t), MD_FRAME_MS); // a missile is up
    md_stop(&g);
    CHECK_EQ(md_tick(&g, t), UINT32_MAX);
    CHECK(md_line(&g, t) == NULL);
}

// Every line is short enough for his bubble (at most two lines of 17 characters).
void test_md_lines_are_short(void)
{
    for (int i = 0; i < md_line_count; i++) {
        const char *s = md_lines[i];
        int lines = 1, len = 0;
        for (; *s; s++) {
            if (*s == '\n') {
                lines++;
                len = 0;
                continue;
            }
            CHECK(++len <= 17);
        }
        CHECK(lines <= 2);
    }
}
