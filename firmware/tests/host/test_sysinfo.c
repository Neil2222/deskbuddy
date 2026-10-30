// The PC's load on the device (sysinfo.h, sysinfo_view.h): the protocol line, the lease, and the strip (where it sits
// next to the Claude usage strip, that the widest it can say fits, what it says about the free space, and that it
// redraws only when what it says or where it sits changes). The application's side (stacked with the usage strip,
// giving way to Buddy's buttons and the sign) is in test_app.c.
#include <stdio.h>
#include <string.h>

#include "gfx_repair.h"
#include "gfx_scene.h"
#include "layout.h"
#include "link.h"
#include "sysinfo.h"
#include "sysinfo_view.h"
#include "test.h"
#include "usage_view.h"

enum { W = 1024, H = 600, ALIGN = 32 };

static link_result_t parse(const char *content, link_command_t *c)
{
    char line[LINK_LINE_MAX];
    snprintf(line, sizeof(line), "%s #%04X", content, link_crc16(content, strlen(content)));
    return link_parse_command(line, strlen(line), c);
}

void test_sysinfo_protocol_lines(void)
{
    link_command_t c;
    CHECK_EQ(parse(">DB1 5 sys.set state=on cpu=12 ram=45 disk=85 disk_free=120", &c), LINK_OK);
    CHECK(c.cmd == LINK_CMD_SYS_SET && c.u.sys_set.on);
    const link_sys_t *s = &c.u.sys_set.sys;
    CHECK(s->cpu == 12 && s->ram == 45 && s->disk == 85 && s->disk_free == 120);

    // Any stat on its own; the limits; unknown keys ignored.
    CHECK_EQ(parse(">DB1 5 sys.set state=on ram=100 later=1", &c), LINK_OK);
    CHECK(s->cpu == -1 && s->ram == 100 && s->disk == -1 && s->disk_free == -1);
    CHECK_EQ(parse(">DB1 5 sys.set state=on cpu=0 disk=0 disk_free=999999", &c), LINK_OK);
    CHECK(s->cpu == 0 && s->ram == -1 && s->disk == 0 && s->disk_free == 999999);
    CHECK_EQ(parse(">DB1 5 sys.set state=on disk=100", &c), LINK_OK);
    CHECK(s->disk == 100 && s->disk_free == -1);
    CHECK_EQ(parse(">DB1 5 sys.set state=on", &c), LINK_OK); // nothing to show, but well-formed
    CHECK(c.u.sys_set.on && !sysinfo_has_stats(s));
    CHECK_EQ(parse(">DB1 5 sys.set state=off", &c), LINK_OK);
    CHECK(!c.u.sys_set.on);

    static const char *const k_bad[] = {
        ">DB1 6 sys.set",                              // no state
        ">DB1 6 sys.set state=maybe cpu=1",            // not on or off
        ">DB1 6 sys.set state=on cpu=101",             // out of range
        ">DB1 6 sys.set state=on ram=-1",
        ">DB1 6 sys.set state=on disk=1 disk_free=1000000",
        ">DB1 6 sys.set state=on disk=1 disk_free=-1",
        ">DB1 6 sys.set state=on disk_free=120",       // the free space without its disk
        ">DB1 6 sys.set state=on cpu=1 disk_free=120",
        ">DB1 6 sys.set state=on cpu=12.5",            // not a whole number
        ">DB1 6 sys.set state=on ram=lots",
        ">DB1 6 sys.set state=on disk=85 disk_free=",
    };
    for (unsigned i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++) {
        if (parse(k_bad[i], &c) != LINK_ERR_ARGS) {
            printf("    accepted: %s\n", k_bad[i]);
        }
        CHECK_EQ(parse(k_bad[i], &c), LINK_ERR_ARGS);
        CHECK_EQ(c.seq, 6); // answered: err bad_args
    }
}

// The lease: 30 s from the last sys.set; off, nothing to show, and a hello end it at once.
void test_sysinfo_lease_and_hello(void)
{
    sysinfo_t s;
    sysinfo_init(&s);
    CHECK(!sysinfo_available(&s, 0));
    CHECK_EQ(sysinfo_next(&s, 0), UINT32_MAX);
    const link_sys_t sys = {12, 45, 85, 120};
    const uint32_t t0 = 0xFFFFF000u; // across the ms counter's wrap
    sysinfo_set(&s, true, &sys, t0);
    CHECK(sysinfo_available(&s, t0 + SYSINFO_LEASE_MS - 1));
    CHECK_EQ(sysinfo_next(&s, t0 + 1000), SYSINFO_LEASE_MS - 1000);
    CHECK(!sysinfo_available(&s, t0 + SYSINFO_LEASE_MS));
    CHECK_EQ(sysinfo_next(&s, t0 + SYSINFO_LEASE_MS), UINT32_MAX);

    for (uint32_t t = t0; t != t0 + 60000u; t += 5000u) { // renewed every 5 s: it holds
        sysinfo_set(&s, true, &sys, t);
        CHECK(sysinfo_available(&s, t + 4999));
    }
    sysinfo_reset(&s); // hello
    CHECK(!sysinfo_available(&s, t0 + 60000u));

    sysinfo_set(&s, true, &sys, 100);
    sysinfo_set(&s, false, &sys, 200); // switched off: the numbers go too
    CHECK(!sysinfo_available(&s, 200));
    CHECK(!sysinfo_has_stats(&s.sys));
    const link_sys_t none = {-1, -1, -1, -1};
    sysinfo_set(&s, true, &none, 300); // no stat: nothing to show
    CHECK(!sysinfo_available(&s, 300));
}

static gfx_color_t s_strip[16 * 1024];
static gfx_color_t s_buf[2][W * H], s_ref[W * H];
static uint8_t s_mem[SYSINFO_VIEW_MEMORY];

static const char *shown(sysinfo_view_t *v, link_sys_t sys)
{
    sysinfo_t s;
    sysinfo_init(&s);
    sysinfo_set(&s, true, &sys, 0);
    sysinfo_view_apply(v, &s, false);
    return v->shown;
}

// The strip: in the usage strip's place, or directly above it (both layouts); the widest it can say fits; the free
// space in GB, from 1000 GB in TB; and over a sequence of samples, row changes and the usage strip coming and going it
// redraws only itself, only on a change, every frame equal to a full redraw.
void test_sysinfo_strip_fits_stacks_and_redraws_only_on_change(void)
{
    static gfx_scene_t scene;
    static layout_t layout;
    static sysinfo_view_t v;
    for (int left = 0; left < 2; left++) {
        layout = layout_compute(W, H, 65, left, ALIGN);
        CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
        const gfx_rect_t low = sysinfo_view_area(layout.stage, false), high = sysinfo_view_area(layout.stage, true);
        CHECK(gfx_rect_equal(low, usage_view_area(layout.stage)));
        CHECK(gfx_rect_contains_rect(layout.stage, high));
        CHECK_EQ(high.x, low.x);
        CHECK_EQ(high.y + high.h + SYSINFO_VIEW_GAP, low.y);
        CHECK(!sysinfo_view_init(&v, &scene, layout.stage, NULL, 28));
        CHECK(sysinfo_view_init(&v, &scene, layout.stage, s_mem, 28));
        CHECK(gfx_rect_equal(gfx_sprite_bounds(&v.sprite), (gfx_rect_t){0, 0, 0, 0})); // hidden until there is data

        // The widest: everything full, the longest free space in either unit.
        for (int i = 0; i < 2; i++) {
            CHECK(strcmp(shown(&v, (link_sys_t){100, 100, 100, i ? 999 : LINK_SYS_FREE_MAX}),
                         i ? "100:100:100:999 GB vrij" : "100:100:100:976.6 TB vrij") == 0);
            CHECK(gfx_rect_equal(gfx_sprite_bounds(&v.sprite), low));
            CHECK(v.plate_x > 0 && v.plate_x + v.plate_w < v.img_w); // nothing cut off
        }
        sysinfo_t s;
        sysinfo_init(&s);
        sysinfo_set(&s, true, &(link_sys_t){100, 100, 100, 999}, 0);
        sysinfo_view_apply(&v, &s, true);
        CHECK(gfx_rect_equal(gfx_sprite_bounds(&v.sprite), high));
        sysinfo_view_apply(&v, NULL, true);
        CHECK(v.sprite.flags & GFX_SPRITE_HIDDEN);
    }

    // What it says: GB below 1000, then TB with one decimal (1024 GB to the TB); a stat left out is not shown.
    CHECK(strcmp(shown(&v, (link_sys_t){12, 45, 85, 120}), "12:45:85:120 GB vrij") == 0);
    const int all = v.plate_w;
    CHECK(strcmp(shown(&v, (link_sys_t){12, 45, 85, 1000}), "12:45:85:1.0 TB vrij") == 0);
    CHECK(strcmp(shown(&v, (link_sys_t){12, 45, 85, 1843}), "12:45:85:1.8 TB vrij") == 0);
    CHECK(strcmp(shown(&v, (link_sys_t){12, 45, 85, -1}), "12:45:85:") == 0);
    CHECK(v.plate_w < all);
    CHECK(strcmp(shown(&v, (link_sys_t){7, -1, -1, -1}), "7:-1:-1:") == 0);
    CHECK(v.plate_w < all / 2);

    // A minute of samples every 5 s, the usage strip appearing (raised) and going (down again), the render loop's way.
    static gfx_dirty_t changes, redraw, every;
    static gfx_repair_t repair;
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
    CHECK(sysinfo_view_init(&v, &scene, layout.stage, s_mem, 28));
    const gfx_rect_t low = sysinfo_view_area(layout.stage, false), high = sysinfo_view_area(layout.stage, true);
    gfx_dirty_init(&changes, W, H, ALIGN);
    gfx_dirty_init(&redraw, W, H, ALIGN);
    gfx_dirty_init(&every, W, H, ALIGN);
    gfx_dirty_add(&every, (gfx_rect_t){0, 0, W, H});
    gfx_repair_init(&repair, W, H, ALIGN);
    const gfx_surface_t ref = {s_ref, W, H, W};
    memset(s_buf, 0, sizeof(s_buf));
    gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});
    sysinfo_t s;
    sysinfo_init(&s);
    const int both_rows = (low.w + 2 * ALIGN) * (low.y + low.h - high.y);
    int back = 0, frames = 0, bad = 0, too_big = 0, strip_frames = 0;
    for (uint32_t t = 0; t <= 60000; t += 100) {
        if (t >= 1000 && t % 5000 == 1000) { // a sample every 5 s; the cpu changes every other one, the rest stays
            sysinfo_set(&s, true, &(link_sys_t){(int8_t)(10 + (t - 1000) / 10000), 45, 85, 120}, t);
        }
        const bool raised = t >= 20000 && t < 40000; // the usage strip shows meanwhile
        sysinfo_view_apply(&v, sysinfo_available(&s, t) ? &s : NULL, raised);
        gfx_dirty_clear(&changes);
        gfx_scene_collect(&scene, &changes);
        if (changes.count == 0) {
            continue;
        }
        gfx_repair_next(&repair, &changes, &redraw);
        const gfx_surface_t dst = {s_buf[back], W, H, W};
        gfx_scene_render(&scene, &redraw, &dst);
        gfx_scene_render(&scene, &every, &ref);
        bad += memcmp(s_buf[back], s_ref, sizeof(s_ref)) != 0;
        back ^= 1;
        if (++frames > 2) { // after the two boot frames
            too_big += gfx_dirty_area(&redraw) > both_rows;
            strip_frames++;
        }
    }
    CHECK_EQ(bad, 0);
    CHECK_EQ(too_big, 0);
    // After the boot frames (the second shows it): the cpu at 11, 21, 31, 41 and 51 s, the row at 20 s (up) and 40 s
    // (down).
    CHECK_EQ(strip_frames, 7);
    CHECK(strcmp(v.shown, "15:45:85:120 GB vrij") == 0);
}
