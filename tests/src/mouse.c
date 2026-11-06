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
    TEST_CHECK(info.dx, 20.0f);
    TEST_CHECK(info.dy, 10.0f);
    TEST_CHECK(info.dw, 50.0f);
    TEST_CHECK(info.dh, 0.0f);
    TEST_CHECK(info.changed_position, true);
    TEST_CHECK(info.changed_size, true);
    TEST_CHECK(info.changed_h, false);
});
