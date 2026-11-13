unsigned char __src_osax_payload[1];
unsigned int __src_osax_payload_len;
unsigned char __src_osax_loader[1];
unsigned int __src_osax_loader_len;

#include "../../src/manifest.m"

#define TEST_SIG(name) bool test_##name(void)
typedef TEST_SIG(function);

#define TEST_FUNC(name, ...) static TEST_SIG(name) { char *test_name = #name; bool result = true; __VA_ARGS__ return result; }
#define TEST_CHECK(r, e) if ((r) != (e)) { printf("                   \e[1;33m%s\e[m\e[1;31m#%d %s == %s\e[m \e[1;31m(%d == %d)\e[m\n", test_name, __LINE__, #r, #e, r, e); result = false; }

#include "area.c"
#include "mouse.c"
#include "service.c"

#define TEST_ENTRY(name) { #name, test_##name },
#define TEST_LIST                                              \
    TEST_ENTRY(display_area_is_in_direction)                   \
    TEST_ENTRY(closest_display_in_direction)                  \
    TEST_ENTRY(mouse_resize_defaults_to_live)                \
    TEST_ENTRY(mouse_release_ignores_stationary_click)       \
    TEST_ENTRY(mouse_release_detects_threshold_crossing)     \
    TEST_ENTRY(mouse_release_detects_prior_resize_when_cursor_returns) \
    TEST_ENTRY(mouse_release_does_not_derive_motion_from_updated_down_location) \
    TEST_ENTRY(mouse_frame_changes_are_classified_by_axis)        \
    TEST_ENTRY(mouse_active_move_targets_only_the_dragged_window) \
    TEST_ENTRY(mouse_release_with_no_prior_resize_still_uses_gesture_origin) \
    TEST_ENTRY(event_coalescing_keeps_mouse_button_boundaries) \
    TEST_ENTRY(bsp_resize_clamps_to_both_child_minimums) \
    TEST_ENTRY(bsp_resize_accounts_for_gaps) \
    TEST_ENTRY(bsp_resize_preserves_previous_ratio_if_infeasible) \
    TEST_ENTRY(bsp_resize_nested_subtree_minimums) \
    TEST_ENTRY(bsp_live_minimum_ignores_delayed_ax_readback) \
    TEST_ENTRY(bsp_live_minimum_learns_only_stable_rejection) \
    TEST_ENTRY(bsp_live_minimum_requires_stationary_observed_size) \
    TEST_ENTRY(bsp_live_avoids_duplicate_ax_writes_after_stale_notification) \
    TEST_ENTRY(bsp_split_retains_gap_and_outer_edge) \
    TEST_ENTRY(bsp_live_frame_plan_minimizes_ax_writes) \
    TEST_ENTRY(bsp_live_top_edge_resize_avoids_bottom_overshoot) \
    TEST_ENTRY(bsp_live_ax_observation_is_rate_limited) \
    TEST_ENTRY(bsp_preview_shadow_does_not_mutate_live_tree) \
    TEST_ENTRY(bsp_preview_rejects_concurrent_topology_or_ratio_changes) \
    TEST_ENTRY(bsp_preview_commit_copies_only_changed_ratios) \
    TEST_ENTRY(bsp_preview_split_conserves_stationary_outer_edges) \
    TEST_ENTRY(bsp_preview_tiny_parent_never_creates_negative_tile) \
    TEST_ENTRY(bsp_preview_maps_screen_y_down_to_quartz_y_up) \
    TEST_ENTRY(bsp_preview_maps_nested_rects_without_reversing_split_order) \
    TEST_ENTRY(bsp_preview_abort_clears_resize_gesture_state) \
    TEST_ENTRY(bsp_preview_does_not_claim_live_ax_notification_ownership) \
    TEST_ENTRY(bsp_preview_probe_never_grows_before_position_is_verified) \
    TEST_ENTRY(bsp_preview_learns_rejected_minimum_from_actual_size) \
    TEST_ENTRY(bsp_preview_geometry_requires_position_after_size_preflight) \
    TEST_ENTRY(bsp_preview_minimum_clamp_preserves_right_edge) \
    TEST_ENTRY(mission_control_reconcile_only_for_space_expose_modes) \
    TEST_ENTRY(mission_control_reconcile_rejects_stale_and_active_retries) \
    TEST_ENTRY(service_install_policy_is_idempotent_and_refuses_unsafe_replacement) \
    TEST_ENTRY(service_readiness_requires_running_matching_daemon_and_socket) \
    TEST_ENTRY(service_xml_escaping_is_lossless) \
    TEST_ENTRY(service_executable_identity_uses_resolved_paths) \
    TEST_ENTRY(service_plist_rejects_other_labels_and_symlinks)

static struct {
    char *name;
    test_function *func;
} tests[] = {
    TEST_LIST
};

int main(int argc, char **argv)
{
    int succeeded = 0;
    int failed = 0;
    int total = array_count(tests);
    printf("\e[1;34m -- Running %d tests\e[m\n\n", total);

    uint64_t cpu_freq  = read_cpu_freq();
    uint64_t begin_tsc = read_cpu_timer();

    for (int i = 0; i < total; ++i) {
        uint64_t tsc = read_cpu_timer();
        bool result = tests[i].func();
        double ms_elapsed = 1000.0 * (double)(read_cpu_timer() - tsc) / (double)cpu_freq;

        printf("(%0.4fms) %s \e[1;33m%s\e[m\n", ms_elapsed, result ? "\e[1;32msuccess\e[m" : " \e[1;31mfailed\e[m", tests[i].name);
        if (result) ++succeeded; else ++failed;
    }

    double ms_elapsed = 1000.0 * (double)(read_cpu_timer() - begin_tsc) / (double)cpu_freq;
    printf("\n\e[1;34m -- Completed (%0.4fms)\e[m\n", ms_elapsed);
    printf("\t%d \e[1;32msucceeded\e[m\n", succeeded);
    printf("\t%d \e[1;31mfailed\e[m\n", failed);
    printf("\t%d \e[1;33mtotal\e[m\n", total);

    return total == succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
