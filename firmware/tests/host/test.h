// Minimal test harness for host-side unit tests (no framework dependency).
#pragma once

#include <stdio.h>

extern int g_test_failed;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            g_test_failed = 1;                                                   \
            return;                                                              \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                                                    \
    do {                                                                                                  \
        const long long a_ = (long long)(a), b_ = (long long)(b);                                         \
        if (a_ != b_) {                                                                                   \
            printf("    FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, a_, b_);      \
            g_test_failed = 1;                                                                            \
            return;                                                                                       \
        }                                                                                                 \
    } while (0)

// Every test, in run order.
#define TEST_LIST                                    \
    X(dirty_clips_to_screen)                         \
    X(dirty_aligns_x_to_cache_lines)                 \
    X(dirty_merges_overlapping_and_flush_rects)      \
    X(dirty_merge_chains_stay_disjoint)              \
    X(dirty_overflow_keeps_coverage)                 \
    X(dirty_random_adds_keep_invariants)             \
    X(scene_renders_only_inside_dirty_rects)         \
    X(scene_transparent_pixels_show_background)      \
    X(scene_layers_follow_z_then_insertion_order)    \
    X(scene_integer_scaling)                         \
    X(scene_flip_x)                                  \
    X(scene_clips_sprites_at_screen_edges)           \
    X(scene_random_sprites_match_reference)          \
    X(scene_collect_reports_old_and_new_bounds)      \
    X(scene_collect_includes_invalidations)          \
    X(repair_alternating_buffers_match_full_redraw)  \
    X(repair_moving_sprite_restores_background)      \
    X(repair_redraws_only_recent_changes)            \
    X(anim_loops)                                    \
    X(anim_holds_last_frame)                         \
    X(buddy_art_decodes_within_palette)              \
    X(buddy_poke_runs_surprise_happy_idle)           \
    X(buddy_idle_blinks_and_breathes)                \
    X(buddy_survives_clock_wraparound)               \
    X(buddy_view_fits_stage_and_hits)                \
    X(monster_phases_run_in_order_then_idle)         \
    X(monster_ignores_events_until_finished)         \
    X(monster_tells_the_story)                       \
    X(monster_redraws_match_full_redraw)             \
    X(office_switch_finishes_a_transition_before_the_next) \
    X(office_time_validity)                          \
    X(office_font_covers_every_text)                 \
    X(office_clock_shows_time_date_and_fallback)     \
    X(office_clock_redraws_match_full_redraw)        \
    X(office_pauses_character_mid_monster)           \
    X(office_moon_button)                            \
    X(office_switch_redraws_fully_only_while_switching) \
    X(layout_splits_screen)                          \
    X(link_crc_matches_ccitt_false)                  \
    X(link_parses_commands)                          \
    X(link_rejects_noise_and_corruption)             \
    X(link_rejects_unknown_commands_and_bad_arguments) \
    X(link_formats_replies_the_companion_can_check)  \
    X(link_time_zone_switches_at_the_announced_moment) \
    X(link_parses_messages)                          \
    X(link_rejects_bad_messages)                     \
    X(marquee_text_policy)                           \
    X(marquee_stays_long_enough_then_hides)          \
    X(marquee_shows_the_text_scrolling_left)         \
    X(marquee_redraws_only_the_sign)

#define X(name) void test_##name(void);
TEST_LIST
#undef X
