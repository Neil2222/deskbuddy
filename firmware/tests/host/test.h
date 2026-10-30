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
    X(mood_names_round_trip)                         \
    X(mood_switches_cleanly)                         \
    X(mood_resumes_after_reactions)                  \
    X(mood_timed_returns_to_idle)                    \
    X(mood_survives_a_pause)                         \
    X(mood_loops_show_their_character)               \
    X(mood_redraws_match_full_redraw)                \
    X(office_switch_finishes_a_transition_before_the_next) \
    X(office_time_validity)                          \
    X(office_font_covers_every_text)                 \
    X(office_clock_shows_time_date_and_fallback)     \
    X(office_clock_redraws_match_full_redraw)        \
    X(office_pauses_character_mid_monster)           \
    X(office_switch_redraws_fully_only_while_switching) \
    X(office_switch_redraws_a_new_face)              \
    X(office_skeleton_assets_match_the_drawing)      \
    X(office_skeleton_keeps_the_time_on_top)         \
    X(office_skeleton_moves_like_clockwork)          \
    X(office_skeleton_redraws_match_full_redraw)     \
    X(layout_splits_screen)                          \
    X(link_crc_matches_ccitt_false)                  \
    X(link_parses_commands)                          \
    X(link_rejects_noise_and_corruption)             \
    X(link_rejects_unknown_commands_and_bad_arguments) \
    X(link_formats_replies_the_companion_can_check)  \
    X(link_time_zone_switches_at_the_announced_moment) \
    X(link_parses_messages)                          \
    X(link_rejects_bad_messages)                     \
    X(link_parses_moods)                             \
    X(link_parses_log_since)                         \
    X(marquee_text_policy)                           \
    X(marquee_stays_long_enough_then_hides)          \
    X(marquee_shows_the_text_scrolling_left)         \
    X(marquee_redraws_only_the_sign)                 \
    X(menu_definitions_fit_and_connect)              \
    X(menu_my_day_reports_what_you_do)               \
    X(menu_navigates_and_goes_back)                  \
    X(menu_selects_every_mood)                       \
    X(menu_activities_and_availability)              \
    X(menu_head_tap_enters_the_clock_from_every_page) \
    X(menu_head_long_press_opens_clock_faces)        \
    X(menu_head_touch_cancels_and_keeps_clear)       \
    X(menu_touch_needs_a_clean_tap)                  \
    X(menu_pages_and_swipes)                         \
    X(menu_view_follows_the_state)                   \
    X(menu_redraws_match_full_redraw)                \
    X(zoom_face_fills_the_screen_in_the_clock_colour) \
    X(zoom_camera_grows_from_his_head_and_back)      \
    X(zoom_enters_and_leaves_through_the_scene_switch) \
    X(zoom_survives_rapid_and_contrary_requests)     \
    X(zoom_whole_loop_into_the_clock_and_back)       \
    X(zoom_whole_loop_into_the_skeleton_and_back)    \
    X(activity_plays_to_the_end_then_the_mood_resumes) \
    X(activity_water_short_circuits_him)             \
    X(activity_sneeze_blows_his_head_off)            \
    X(activity_stare_pushes_in_and_he_loses)         \
    X(activity_meditation_floats_until_a_notification) \
    X(activity_redraws_match_full_redraw_on_the_stage) \
    X(ask_yes_is_happy_hops_a_smoke_break_then_his_mood) \
    X(ask_no_or_no_answer_makes_him_briefly_sad)     \
    X(ask_buttons_beside_him_answer_on_release)      \
    X(ask_redraws_match_full_redraw)                 \
    X(pdc_protocol_lines)                            \
    X(pdc_lease_and_one_request_per_tap)             \
    X(pdc_sign_lines)                                \
    X(pdc_menu_only_while_offered)                   \
    X(pdc_confirmation_and_reaction)                 \
    X(focus_tells_the_comic_in_order)                \
    X(focus_thoughts_redraw_on_the_stage)            \
    X(kitt_turns_in_sweeps_and_turns_back_to_his_mood) \
    X(kitt_a_mood_choice_turns_him_back_into_that_mood) \
    X(kitt_scanner_sweeps_with_a_tail)               \
    X(kitt_redraws_match_full_redraw)                \
    X(app_hello_offers_tracking_and_the_question)    \
    X(app_info_reports_the_build_and_the_board)      \
    X(app_hello_fits_with_a_long_version)            \
    X(app_question_by_command_reports_yes_no_and_timeout) \
    X(app_an_interrupted_question_is_never_an_answer) \
    X(app_shows_and_touches_report_their_own_ids)    \
    X(app_my_day_and_mood_sources)                   \
    X(app_events_carry_identity_and_time)            \
    X(app_events_replay_after_a_reconnect)           \
    X(app_events_replay_reports_what_was_lost)       \
    X(app_clock_faces_by_touch_and_command)          \
    X(app_kitt_until_a_tap_a_mood_or_the_clock)      \
    X(app_tic_tac_toe_on_the_touchscreen)            \
    X(app_tic_tac_toe_pauses_in_clock_mode)          \
    X(ttt_judge_finds_every_line_and_draws)          \
    X(ttt_best_move_wins_blocks_and_knows_positions) \
    X(ttt_perfect_play_draws)                        \
    X(ttt_ai_plays_only_free_cells)                  \
    X(ttt_buddy_is_good_but_beatable)                \
    X(ttt_attitude_changes_his_strength)             \
    X(ttt_buddy_usually_takes_a_win)                 \
    X(ttt_session_turns_and_thinking)                \
    X(ttt_session_starters_alternate_and_he_opens_late) \
    X(ttt_session_reaction_rematch_and_score)        \
    X(ttt_session_streaks_attitude_and_lines)        \
    X(ttt_session_gets_bored_without_touches)        \
    X(ttt_session_survives_clock_wraparound)         \
    X(ttt_lines_are_known_strings)                   \
    X(ttt_pause_freezes_and_resumes_exactly)         \
    X(ttt_pause_expires_and_keeps_the_score)         \
    X(game_faces_hold_until_the_next_round)          \
    X(game_brain_plays_over_his_mood)                \
    X(game_touch_takes_only_the_board)               \
    X(game_lines_fit_his_bubble)                     \
    X(game_redraws_match_full_redraw)                \
    X(md_waves_start_gentle_then_get_harder)         \
    X(md_a_shot_catches_missiles_and_chains)         \
    X(md_multi_kills_score_more)                     \
    X(md_lost_items_end_the_game)                    \
    X(md_a_hit_on_buddy_only_stuns)                  \
    X(md_eyes_overheat_and_cool)                     \
    X(md_a_miss_breaks_the_streak)                   \
    X(md_shots_stay_in_the_sky)                      \
    X(md_late_frames_never_skip_a_blast)             \
    X(md_cleared_waves_pay_and_the_next_is_harder)   \
    X(md_first_waves_are_approachable)               \
    X(md_unplayed_game_ends_by_itself)               \
    X(md_is_deterministic_across_wraparound)         \
    X(md_tick_says_when_next)                        \
    X(md_lines_are_short)                            \
    X(md_view_lines_fit_his_bubble)                  \
    X(md_buddy_faces_follow_the_game)                \
    X(md_view_touch_shoots_at_once_buttons_on_release) \
    X(md_view_redraws_match_full_redraw)             \
    X(app_missile_defense_fullscreen_and_back)       \
    X(usage_protocol_lines)                          \
    X(usage_lease_and_hello)                         \
    X(usage_countdown_boundaries)                    \
    X(usage_colours_follow_the_pct)                  \
    X(usage_strip_fits_and_redraws_only_on_change)   \
    X(app_usage_strip_follows_the_lease_and_the_mode) \
    X(sysinfo_protocol_lines)                        \
    X(sysinfo_lease_and_hello)                       \
    X(sysinfo_strip_fits_stacks_and_redraws_only_on_change) \
    X(app_sys_strip_stacks_with_usage)

#define X(name) void test_##name(void);
TEST_LIST
#undef X
