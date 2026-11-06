// Pure mouse gesture and resize-state regression tests. The native test
// executable includes the same yabai implementation via src/manifest.m.

TEST_FUNC(mouse_resize_defaults_to_live,
{
    struct mouse_state ms = {0};
    mouse_state_init(&ms);
    TEST_CHECK(ms.bsp_live_resize, true);
    TEST_CHECK(ms.action1, MOUSE_MODE_MOVE);
    TEST_CHECK(ms.action2, MOUSE_MODE_RESIZE);
});

TEST_FUNC(mouse_release_ignores_stationary_click,
{
    struct mouse_state ms = { .origin_location = { 100, 100 } };
    TEST_CHECK(mouse_gesture_moved(&ms, CGPointMake(100, 100)), false);
    TEST_CHECK(mouse_gesture_moved(&ms, CGPointMake(102.9, 102.9)), false);
});

TEST_FUNC(mouse_release_detects_threshold_crossing,
{
    struct mouse_state ms = { .origin_location = { 100, 100 } };
    TEST_CHECK(mouse_gesture_moved(&ms, CGPointMake(103, 100)), true);
    TEST_CHECK(mouse_gesture_moved(&ms, CGPointMake(100, 97)), true);
});

TEST_FUNC(mouse_release_detects_prior_resize_when_cursor_returns,
{
    struct mouse_state ms = {
        .origin_location = { 100, 100 },
        .down_location = { 140, 140 },
        .resize_applied = true
    };
    TEST_CHECK(mouse_gesture_moved(&ms, CGPointMake(100, 100)), true);
    TEST_CHECK(mouse_gesture_moved(&ms, CGPointMake(101, 101)), true);
});

TEST_FUNC(mouse_release_does_not_derive_motion_from_updated_down_location,
{
    struct mouse_state ms = {
        .origin_location = { 100, 100 },
        .down_location = { 140, 130 }
    };
    TEST_CHECK(mouse_gesture_moved(&ms, CGPointMake(140, 130)), true);
});

TEST_FUNC(mouse_frame_changes_are_classified_by_axis,
{
    struct window window = { .frame = {{ 120, 160 }, { 800, 600 }} };
    struct mouse_state ms = {
        .window = &window,
        .window_frame = {{ 100, 150 }, { 750, 600 }}
    };
    struct mouse_window_info info = {0};
    mouse_window_info_populate(&ms, &info);
    TEST_CHECK(info.dx == 20.0f, true);
    TEST_CHECK(info.dy == 10.0f, true);
    TEST_CHECK(info.dw == 50.0f, true);
    TEST_CHECK(info.dh == 0.0f, true);
    TEST_CHECK(info.changed_position, true);
    TEST_CHECK(info.changed_size, true);
    TEST_CHECK(info.changed_h, false);
});

TEST_FUNC(mouse_active_move_targets_only_the_dragged_window,
{
    struct window selected = { .id = 701 };
    struct window other = { .id = 702 };
    struct mouse_state ms = { .window = &selected, .current_action = MOUSE_MODE_MOVE };
    TEST_CHECK(mouse_is_active_move(&ms, &selected), true);
    TEST_CHECK(mouse_is_active_move(&ms, &other), false);
    TEST_CHECK(mouse_is_active_move(&ms, NULL), false);
    ms.current_action = MOUSE_MODE_RESIZE;
    TEST_CHECK(mouse_is_active_move(&ms, &selected), false);
    ms.current_action = MOUSE_MODE_NONE;
    TEST_CHECK(mouse_is_active_move(&ms, &selected), false);
});

TEST_FUNC(mouse_release_with_no_prior_resize_still_uses_gesture_origin,
{
    struct mouse_state ms = {
        .origin_location = { 300, 300 },
        .down_location = { 325, 325 },
        .resize_applied = false
    };
    TEST_CHECK(mouse_gesture_moved(&ms, CGPointMake(300, 300)), false);
    TEST_CHECK(mouse_gesture_moved(&ms, CGPointMake(325, 325)), true);
});

TEST_FUNC(bsp_resize_clamps_to_both_child_minimums,
{
    TEST_CHECK(fabsf(bsp_clamp_ratio(0.9f, 0.5f, 1000.0f, 0.0f, 250.0f, 400.0f) - 0.6f) < 0.0001f, true);
    TEST_CHECK(fabsf(bsp_clamp_ratio(0.1f, 0.5f, 1000.0f, 0.0f, 250.0f, 400.0f) - 0.25f) < 0.0001f, true);
});

TEST_FUNC(bsp_resize_accounts_for_gaps,
{
    float ratio = bsp_clamp_ratio(0.9f, 0.5f, 1000.0f, 20.0f, 250.0f, 400.0f);
    TEST_CHECK(fabsf(ratio - (1.0f - 400.0f / 980.0f)) < 0.0001f, true);
});

TEST_FUNC(bsp_resize_preserves_previous_ratio_if_infeasible,
{
    TEST_CHECK(bsp_clamp_ratio(0.8f, 0.5f, 600.0f, 20.0f, 350.0f, 350.0f) == 0.5f, true);
    TEST_CHECK(bsp_clamp_ratio(0.8f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f) == 0.5f, true);
});

TEST_FUNC(event_coalescing_keeps_mouse_button_boundaries,
{
    TEST_CHECK(event_loop_can_coalesce(MOUSE_DRAGGED, MOUSE_DRAGGED), true);
    TEST_CHECK(event_loop_can_coalesce(MOUSE_MOVED, MOUSE_MOVED), true);
    TEST_CHECK(event_loop_can_coalesce(MOUSE_DRAGGED, MOUSE_UP), false);
    TEST_CHECK(event_loop_can_coalesce(MOUSE_DRAGGED, MOUSE_DOWN), false);
    TEST_CHECK(event_loop_can_coalesce(MOUSE_MOVED, MOUSE_DRAGGED), false);
    TEST_CHECK(event_loop_can_coalesce(MOUSE_DRAGGED, MOUSE_MOVED), false);
    TEST_CHECK(event_loop_can_coalesce(MOUSE_UP, MOUSE_UP), false);
    TEST_CHECK(event_loop_can_coalesce(WINDOW_MOVED, WINDOW_MOVED), false);
});
