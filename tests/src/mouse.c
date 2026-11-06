// Pure mouse gesture and resize-state regression tests. The native test
// executable includes the same yabai implementation via src/manifest.m.

TEST_FUNC(mouse_resize_defaults_to_live,
{
    struct mouse_state ms = {0};
    mouse_state_init(&ms);
    TEST_CHECK(ms.bsp_live_resize, true);
    TEST_CHECK(ms.bsp_resize_preview, false);
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

TEST_FUNC(bsp_resize_nested_subtree_minimums,
{
    struct window_node a = {0}, b = {0}, c = {0};
    struct window_node nested = { .left = &a, .right = &b, .split = SPLIT_Y };
    struct window_node root = { .left = &nested, .right = &c, .split = SPLIT_Y };
    struct view view = { .window_gap = 10, .flags = VIEW_ENABLE_GAP };
    struct bsp_minimum_size minimum = window_node_minimum_size(&view, &root);
    TEST_CHECK(minimum.w == 23.0f, true);
    TEST_CHECK(minimum.h == 1.0f, true);

    nested.split = SPLIT_X;
    minimum = window_node_minimum_size(&view, &root);
    TEST_CHECK(minimum.w == 12.0f, true);
    TEST_CHECK(minimum.h == 12.0f, true);
});

TEST_FUNC(bsp_live_minimum_ignores_delayed_ax_readback,
{
    struct window window = {
        .live_resize_requested = { 400, 300 },
        .live_resize_has_request = true
    };
    TEST_CHECK(bsp_observe_live_resize_minimum(&window, CGSizeMake(500, 300)), false);
    TEST_CHECK(bsp_observe_live_resize_minimum(&window, CGSizeMake(500, 300)), false);
    TEST_CHECK(bsp_observe_live_resize_minimum(&window, CGSizeMake(500, 300)), false);
    // AX eventually applies the requested size; no minimum was learned.
    TEST_CHECK(bsp_observe_live_resize_minimum(&window, CGSizeMake(400, 300)), false);
    TEST_CHECK(window.live_resize_minimum.width == 0.0f, true);
    TEST_CHECK(window.live_resize_width_stall == 0, true);
});

TEST_FUNC(bsp_live_minimum_learns_only_stable_rejection,
{
    struct window window = {
        .live_resize_requested = { 400, 300 },
        .live_resize_has_request = true
    };
    TEST_CHECK(bsp_observe_live_resize_minimum(&window, CGSizeMake(500, 300)), false);
    TEST_CHECK(bsp_observe_live_resize_minimum(&window, CGSizeMake(500, 300)), false);
    TEST_CHECK(bsp_observe_live_resize_minimum(&window, CGSizeMake(500, 300)), false);
    TEST_CHECK(bsp_observe_live_resize_minimum(&window, CGSizeMake(500, 300)), true);
    TEST_CHECK(window.live_resize_minimum.width == 500.0f, true);
    TEST_CHECK(window.live_resize_minimum.height == 0.0f, true);
    TEST_CHECK(bsp_observe_live_resize_minimum(&window, CGSizeMake(500, 300)), false);
});

TEST_FUNC(bsp_live_minimum_requires_stationary_observed_size,
{
    struct window window = {
        .live_resize_requested = { 400, 300 },
        .live_resize_has_request = true
    };
    TEST_CHECK(bsp_observe_live_resize_minimum(&window, CGSizeMake(520, 300)), false);
    TEST_CHECK(bsp_observe_live_resize_minimum(&window, CGSizeMake(510, 300)), false);
    TEST_CHECK(bsp_observe_live_resize_minimum(&window, CGSizeMake(500, 300)), false);
    TEST_CHECK(bsp_observe_live_resize_minimum(&window, CGSizeMake(490, 300)), false);
    TEST_CHECK(window.live_resize_minimum.width == 0.0f, true);
});

TEST_FUNC(bsp_live_avoids_duplicate_ax_writes_after_stale_notification,
{
    struct window window = { .frame = {{10, 20}, {400, 300}} };
    struct window_capture capture = { .window = &window, .x = 10, .y = 20, .w = 420, .h = 300 };
    TEST_CHECK(bsp_live_frame_needs_write(&window, capture), true);
    window.live_resize_target_frame = (CGRect) {{10, 20}, {420, 300}};
    window.live_resize_has_request = true;
    // A delayed AX notification can restore the observed (old) frame.
    window.frame = (CGRect) {{10, 20}, {400, 300}};
    TEST_CHECK(bsp_live_frame_needs_write(&window, capture), false);
    capture.w = 430;
    TEST_CHECK(bsp_live_frame_needs_write(&window, capture), true);
    window.live_resize_has_request = false;
    capture.w = 420;
    TEST_CHECK(bsp_live_frame_needs_write(&window, capture), true);
});

TEST_FUNC(bsp_split_retains_gap_and_outer_edge,
{
    struct area parent = { .x = 20, .y = 30, .w = 1000, .h = 650 };
    struct area first, second;
    area_make_pair(SPLIT_Y, 12, 0.5f, &parent, &first, &second);
    TEST_CHECK(first.x == 20.0f && first.w == 494.0f, true);
    TEST_CHECK(second.x == 526.0f && second.w == 494.0f, true);
    TEST_CHECK(second.x - (first.x + first.w) == 12.0f, true);
    TEST_CHECK(second.x + second.w == parent.x + parent.w, true);
    TEST_CHECK(first.y == parent.y && first.h == parent.h, true);

    area_make_pair(SPLIT_X, 12, 0.5f, &parent, &first, &second);
    TEST_CHECK(second.y - (first.y + first.h) == 12.0f, true);
    TEST_CHECK(second.y + second.h == parent.y + parent.h, true);
});

TEST_FUNC(bsp_live_frame_plan_minimizes_ax_writes,
{
    CGRect previous = {{10, 20}, {400, 300}};
    struct bsp_live_frame_plan plan = bsp_plan_live_frame_write(previous, previous);
    TEST_CHECK(plan.move, false);
    TEST_CHECK(plan.resize, false);

    plan = bsp_plan_live_frame_write(previous, (CGRect) {{10, 20}, {420, 300}});
    TEST_CHECK(plan.move, false);
    TEST_CHECK(plan.resize, true);

    plan = bsp_plan_live_frame_write(previous, (CGRect) {{20, 30}, {400, 300}});
    TEST_CHECK(plan.move, true);
    TEST_CHECK(plan.resize, false);
});

TEST_FUNC(bsp_live_top_edge_resize_avoids_bottom_overshoot,
{
    CGRect previous = {{10, 20}, {400, 300}};
    // Drag the top edge down: the opposite edge stays at y=320.
    CGRect shrink = {{10, 50}, {400, 270}};
    struct bsp_live_frame_plan plan = bsp_plan_live_frame_write(previous, shrink);
    TEST_CHECK(plan.move, true);
    TEST_CHECK(plan.resize, true);
    TEST_CHECK(plan.move_first, false);
    // Shrinking before moving does not temporarily extend below y=320.
    TEST_CHECK(previous.origin.y + shrink.size.height <= 320, true);

    // Drag the top edge up: moving first avoids an intermediate bottom overshoot.
    CGRect grow = {{10, 0}, {400, 320}};
    plan = bsp_plan_live_frame_write(previous, grow);
    TEST_CHECK(plan.move_first, true);
    TEST_CHECK(grow.origin.y + previous.size.height <= 320, true);
});

TEST_FUNC(bsp_live_ax_observation_is_rate_limited,
{
    TEST_CHECK(bsp_live_observation_due(1000000000ULL, 950000000ULL), false);
    TEST_CHECK(bsp_live_observation_due(1050000000ULL, 950000000ULL), true);
    TEST_CHECK(bsp_live_observation_due(1200000000ULL, 1050000000ULL), true);
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


TEST_FUNC(bsp_preview_shadow_does_not_mutate_live_tree,
{
    struct window_node real = { .split = SPLIT_X, .ratio = 0.5f };
    struct window_node left = { .window_count = 1, .window_list = {11}, .parent = &real };
    struct window_node right = { .window_count = 1, .window_list = {12}, .parent = &real };
    real.left = &left;
    real.right = &right;
    struct window_node *shadow = bsp_preview_clone_tree(&real, NULL);
    TEST_CHECK(shadow != NULL, true);
    if (shadow) {
        shadow->ratio = 0.7f;
        TEST_CHECK(real.ratio == 0.5f, true);
        TEST_CHECK(shadow->left->parent == shadow, true);
        bsp_preview_free_tree(shadow);
    }
});

TEST_FUNC(bsp_preview_rejects_concurrent_topology_or_ratio_changes,
{
    struct window_node real = { .split = SPLIT_X, .ratio = 0.5f };
    struct window_node left = { .window_count = 1, .window_list = {11}, .parent = &real };
    struct window_node right = { .window_count = 1, .window_list = {12}, .parent = &real };
    real.left = &left;
    real.right = &right;
    struct window_node *baseline = bsp_preview_clone_tree(&real, NULL);
    TEST_CHECK(bsp_preview_tree_matches(&real, baseline), true);
    right.window_list[0] = 99;
    TEST_CHECK(bsp_preview_tree_matches(&real, baseline), false);
    right.window_list[0] = 12;
    real.ratio = 0.6f;
    TEST_CHECK(bsp_preview_tree_matches(&real, baseline), false);
    bsp_preview_free_tree(baseline);
});

TEST_FUNC(bsp_preview_commit_copies_only_changed_ratios,
{
    struct window_node real = { .split = SPLIT_X, .ratio = 0.5f };
    struct window_node left = { .ratio = 0.0f, .parent = &real };
    struct window_node right = { .ratio = 0.0f, .parent = &real };
    real.left = &left;
    real.right = &right;
    struct window_node *baseline = bsp_preview_clone_tree(&real, NULL);
    struct window_node *proposed = bsp_preview_clone_tree(&real, NULL);
    proposed->ratio = 0.7f;
    proposed->left->ratio = 0.5f;
    bsp_preview_copy_changed_ratios(&real, proposed, baseline);
    TEST_CHECK(real.ratio == 0.7f, true);
    TEST_CHECK(left.ratio == 0.5f, true);
    TEST_CHECK(right.ratio == 0.0f, true);
    bsp_preview_free_tree(baseline);
    bsp_preview_free_tree(proposed);
});

TEST_FUNC(bsp_preview_split_conserves_stationary_outer_edges,
{
    struct area parent = { 10.0f, 20.0f, 1001.0f, 801.0f };
    struct area first, second;
    for (int i = 10; i <= 90; ++i) {
        float ratio = (float)i / 100.0f;
        area_make_pair(SPLIT_X, 8, ratio, &parent, &first, &second);
        TEST_CHECK(first.y == parent.y, true);
        TEST_CHECK(second.y + second.h == parent.y + parent.h, true);
        TEST_CHECK(first.y + first.h + 8 == second.y, true);
        area_make_pair(SPLIT_Y, 8, ratio, &parent, &first, &second);
        TEST_CHECK(first.x == parent.x, true);
        TEST_CHECK(second.x + second.w == parent.x + parent.w, true);
        TEST_CHECK(first.x + first.w + 8 == second.x, true);
    }
});


TEST_FUNC(bsp_preview_tiny_parent_never_creates_negative_tile,
{
    struct area parent = { 10.0f, 20.0f, 4.0f, 3.0f };
    struct area first, second;
    area_make_pair(SPLIT_X, 8, 0.5f, &parent, &first, &second);
    TEST_CHECK(first.h >= 0.0f, true);
    TEST_CHECK(second.h >= 0.0f, true);
    TEST_CHECK(second.y + second.h == parent.y + parent.h, true);
    area_make_pair(SPLIT_Y, 8, 0.5f, &parent, &first, &second);
    TEST_CHECK(first.w >= 0.0f, true);
    TEST_CHECK(second.w >= 0.0f, true);
    TEST_CHECK(second.x + second.w == parent.x + parent.w, true);
});


TEST_FUNC(bsp_preview_maps_screen_y_down_to_quartz_y_up,
{
    // Use a nonzero display origin to catch accidental global/local mixing.
    struct area root = { .x = 1200.0f, .y = 180.0f, .w = 900.0f, .h = 1600.0f };
    struct area top = { .x = 1200.0f, .y = 180.0f, .w = 900.0f, .h = 700.0f };
    struct area bottom = { .x = 1200.0f, .y = 900.0f, .w = 900.0f, .h = 880.0f };
    CGRect top_rect = bsp_preview_local_rect(top, root);
    CGRect bottom_rect = bsp_preview_local_rect(bottom, root);
    TEST_CHECK(top_rect.origin.y == 900.0f, true);
    TEST_CHECK(bottom_rect.origin.y == 0.0f, true);
    TEST_CHECK(top_rect.origin.x == 0.0f, true);
    TEST_CHECK(bottom_rect.origin.x == 0.0f, true);
    TEST_CHECK(top_rect.size.height == 700.0f, true);
    TEST_CHECK(bottom_rect.size.height == 880.0f, true);

    // A left/right split should preserve horizontal positions while applying
    // the same coordinate convention on landscape and portrait displays.
    struct area left = { .x = 1200.0f, .y = 180.0f, .w = 440.0f, .h = 1600.0f };
    struct area right = { .x = 1660.0f, .y = 180.0f, .w = 440.0f, .h = 1600.0f };
    CGRect left_rect = bsp_preview_local_rect(left, root);
    CGRect right_rect = bsp_preview_local_rect(right, root);
    TEST_CHECK(left_rect.origin.x == 0.0f, true);
    TEST_CHECK(right_rect.origin.x == 460.0f, true);
    TEST_CHECK(left_rect.origin.y == 0.0f, true);
    TEST_CHECK(right_rect.origin.y == 0.0f, true);
});

TEST_FUNC(bsp_preview_maps_nested_rects_without_reversing_split_order,
{
    struct area root = { .x = -900.0f, .y = -100.0f, .w = 900.0f, .h = 1200.0f };
    struct area top_left = { .x = -900.0f, .y = -100.0f, .w = 440.0f, .h = 500.0f };
    struct area bottom_right = { .x = -440.0f, .y = 420.0f, .w = 440.0f, .h = 680.0f };
    CGRect first = bsp_preview_local_rect(top_left, root);
    CGRect second = bsp_preview_local_rect(bottom_right, root);
    TEST_CHECK(first.origin.x == 0.0f, true);
    TEST_CHECK(first.origin.y == 700.0f, true);
    TEST_CHECK(second.origin.x == 460.0f, true);
    TEST_CHECK(second.origin.y == 0.0f, true);
    TEST_CHECK(first.origin.y > second.origin.y, true);
    TEST_CHECK(first.origin.x < second.origin.x, true);
});


TEST_FUNC(bsp_preview_abort_clears_resize_gesture_state,
{
    struct window window = { .id = 501 };
    struct mouse_state state = {
        .preview_resize_active = true,
        .bsp_live_resize = true,
        .current_action = MOUSE_MODE_RESIZE,
        .window = &window,
        .live_resized = true,
        .resize_applied = true
    };
    mouse_abort_bsp_preview_gesture(&state);
    TEST_CHECK(state.preview_resize_active, false);
    TEST_CHECK(state.current_action, MOUSE_MODE_NONE);
    TEST_CHECK(state.window == NULL, true);
    TEST_CHECK(state.live_resized, false);
    TEST_CHECK(state.resize_applied, false);
    // Repeated cancellation is harmless.
    mouse_abort_bsp_preview_gesture(&state);
    TEST_CHECK(state.current_action, MOUSE_MODE_NONE);
});

TEST_FUNC(bsp_preview_does_not_claim_live_ax_notification_ownership,
{
    struct mouse_state saved = g_mouse_state;
    struct window window = { .id = 502 };
    struct view candidate = { .layout = VIEW_BSP };
    g_mouse_state = (struct mouse_state) {
        .preview_resize_active = true,
        .bsp_live_resize = true,
        .current_action = MOUSE_MODE_RESIZE,
        .window = &window
    };
    TEST_CHECK(mouse_live_resize_controls_view(&candidate), false);
    g_mouse_state = saved;
});
