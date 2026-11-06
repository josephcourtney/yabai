// Experimental, opt-in BSP resize preview. No AX geometry writes occur until
// mouse-up; live AX resizing remains a separate mode.
extern int g_connection;

struct bsp_resize_preview_session {
    struct view shadow;
    struct window_node *baseline;
    uint64_t sid;
    uint32_t source_wid;
    struct area initial_area;
    int initial_gap;
    uint64_t initial_flags;
    enum window_node_split initial_split_type;
    uint32_t overlay_wid;
    CGContextRef overlay_context;
    bool active;
    bool changed;
};
static struct bsp_resize_preview_session g_bsp_resize_preview;

static void bsp_preview_free_tree(struct window_node *node)
{
    if (!node) return;
    bsp_preview_free_tree(node->left);
    bsp_preview_free_tree(node->right);
    free(node);
}

static bool bsp_preview_contains_zoom(const struct window_node *node)
{
    if (!node) return false;
    return node->zoom || bsp_preview_contains_zoom(node->left) ||
           bsp_preview_contains_zoom(node->right);
}

static struct window_node *bsp_preview_clone_tree(const struct window_node *source,
                                                  struct window_node *parent)
{
    if (!source) return NULL;
    struct window_node *copy = malloc(sizeof(*copy));
    if (!copy) return NULL;
    *copy = *source;
    copy->parent = parent;
    copy->left = NULL;
    copy->right = NULL;
    copy->zoom = NULL;
    copy->insert_dir = 0;
    memset(&copy->feedback_window, 0, sizeof(copy->feedback_window));
    if (source->left) {
        copy->left = bsp_preview_clone_tree(source->left, copy);
        if (!copy->left) goto fail;
    }
    if (source->right) {
        copy->right = bsp_preview_clone_tree(source->right, copy);
        if (!copy->right) goto fail;
    }
    return copy;
fail:
    bsp_preview_free_tree(copy);
    return NULL;
}

static bool bsp_preview_tree_matches(const struct window_node *current,
                                     const struct window_node *baseline)
{
    if (!current || !baseline) return current == baseline;
    if ((current->left != NULL) != (baseline->left != NULL) ||
        (current->right != NULL) != (baseline->right != NULL) ||
        current->split != baseline->split ||
        current->ratio != baseline->ratio ||
        current->window_count != baseline->window_count ||
        current->zoom != NULL) return false;
    for (int i = 0; i < current->window_count; ++i) {
        if (current->window_list[i] != baseline->window_list[i]) return false;
    }
    return bsp_preview_tree_matches(current->left, baseline->left) &&
           bsp_preview_tree_matches(current->right, baseline->right);
}

static void bsp_preview_copy_changed_ratios(struct window_node *destination,
                                            const struct window_node *proposed,
                                            const struct window_node *baseline)
{
    if (proposed->ratio != baseline->ratio) destination->ratio = proposed->ratio;
    if (destination->left) {
        bsp_preview_copy_changed_ratios(destination->left, proposed->left, baseline->left);
    }
    if (destination->right) {
        bsp_preview_copy_changed_ratios(destination->right, proposed->right, baseline->right);
    }
}

static bool bsp_preview_area_equal(struct area a, struct area b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static void bsp_preview_layout(struct view *view, struct window_node *node)
{
    if (!node->left || !node->right) return;
    area_make_pair_for_node(view, node);
    bsp_preview_layout(view, node->left);
    bsp_preview_layout(view, node->right);
}

// BSP tile coordinates use the global top-left, y-down display convention.
// SLWindowContextCreate uses a Quartz drawing context with y increasing up.
// Convert the rectangle within the overlay before drawing; do not alter the
// BSP tree or rotate based on CGDisplayRotation. A portrait display merely
// makes the mismatch easier to see when tiles are stacked top-to-bottom.
static CGRect bsp_preview_local_rect(struct area tile, struct area root)
{
    return CGRectMake(tile.x - root.x,
                      root.h - (tile.y - root.y) - tile.h,
                      tile.w, tile.h);
}

static void bsp_preview_draw_leaves(struct window_node *node, CGContextRef context,
                                    struct area root)
{
    if (!node->left || !node->right) {
        if (!node->window_count) return;
        CGRect rect = bsp_preview_local_rect(node->area, root);
        if (rect.size.width <= 3.0f || rect.size.height <= 3.0f) return;
        CGContextFillRect(context, rect);
        CGContextStrokeRect(context, CGRectInset(rect, 1.0f, 1.0f));
        return;
    }
    bsp_preview_draw_leaves(node->left, context, root);
    bsp_preview_draw_leaves(node->right, context, root);
}

static void bsp_preview_render(void)
{
    struct bsp_resize_preview_session *preview = &g_bsp_resize_preview;
    if (!preview->overlay_context || !preview->shadow.root) return;
    struct area root = preview->initial_area;
    CGContextRef context = preview->overlay_context;
    CGContextClearRect(context, CGRectMake(0, 0, root.w, root.h));
    CGContextSetLineWidth(context, 2.0f);
    CGContextSetRGBFillColor(context, 0.20f, 0.60f, 1.0f, 0.10f);
    CGContextSetRGBStrokeColor(context, 0.20f, 0.70f, 1.0f, 0.95f);
    bsp_preview_draw_leaves(preview->shadow.root, context, root);
    CGContextFlush(context);
}

static bool bsp_preview_create_overlay(void)
{
    struct bsp_resize_preview_session *preview = &g_bsp_resize_preview;
    struct area area = preview->initial_area;
    CGRect frame = CGRectMake(area.x, area.y, area.w, area.h);
    CFTypeRef region = NULL;
    if (CGSNewRegionWithRect(&frame, &region) != kCGErrorSuccess || !region) return false;
    CFTypeRef empty = CGRegionCreateEmptyRegion();
    if (!empty) {
        CFRelease(region);
        return false;
    }
    // Noninteractive tags also used by yabai's existing insertion feedback.
    uint64_t tags = (1ULL << 1) | (1ULL << 9);
    CGError err = SLSNewWindowWithOpaqueShapeAndContext(g_connection, 2, region,
                       empty, 13, &tags, 0, 0, 64, &preview->overlay_wid, NULL);
    CFRelease(empty);
    CFRelease(region);
    if (err != kCGErrorSuccess || !preview->overlay_wid) return false;
    sls_window_disable_shadow(preview->overlay_wid);
    SLSSetWindowResolution(g_connection, preview->overlay_wid, 1.0f);
    SLSSetWindowOpacity(g_connection, preview->overlay_wid, false);
    SLSSetWindowLevel(g_connection, preview->overlay_wid,
                      CGWindowLevelForKey(kCGOverlayWindowLevelKey));
    preview->overlay_context = SLWindowContextCreate(g_connection, preview->overlay_wid, NULL);
    if (!preview->overlay_context) return false;
    bsp_preview_render();
    return SLSOrderWindow(g_connection, preview->overlay_wid, 1, 0) == kCGErrorSuccess;
}

void bsp_resize_preview_cancel(void)
{
    struct bsp_resize_preview_session *preview = &g_bsp_resize_preview;
    if (preview->overlay_wid) {
        SLSOrderWindow(g_connection, preview->overlay_wid, 0, 0);
        if (preview->overlay_context) CGContextRelease(preview->overlay_context);
        SLSReleaseWindow(g_connection, preview->overlay_wid);
    }
    bsp_preview_free_tree(preview->shadow.root);
    bsp_preview_free_tree(preview->baseline);
    memset(preview, 0, sizeof(*preview));
}

void bsp_resize_preview_cancel_for_space(uint64_t sid)
{
    if (g_bsp_resize_preview.active && g_bsp_resize_preview.sid == sid) {
        bsp_resize_preview_cancel();
    }
}

bool bsp_resize_preview_begin(struct view *view, uint32_t source_wid)
{
    bsp_resize_preview_cancel();
    if (!view || view->layout != VIEW_BSP || !view->root ||
        !space_is_visible(view->sid) || view_is_invalid(view) ||
        bsp_preview_contains_zoom(view->root)) return false;
    struct bsp_resize_preview_session *preview = &g_bsp_resize_preview;
    preview->baseline = bsp_preview_clone_tree(view->root, NULL);
    preview->shadow = *view;
    preview->shadow.root = bsp_preview_clone_tree(view->root, NULL);
    if (!preview->baseline || !preview->shadow.root) goto fail;
    preview->sid = view->sid;
    preview->source_wid = source_wid;
    preview->initial_area = view->root->area;
    preview->initial_gap = view->window_gap;
    preview->initial_flags = view->flags & (VIEW_ENABLE_GAP | VIEW_ENABLE_PADDING);
    preview->initial_split_type = view->split_type;
    preview->active = true;
    if (!bsp_preview_create_overlay()) goto fail;
    // A preview commit must not inherit outstanding AX requests from an
    // earlier live gesture. Preserve learned minimum sizes, not old targets.
    view_clear_live_resize_requests(view);
    return true;
fail:
    bsp_resize_preview_cancel();
    return false;
}

bool bsp_resize_preview_adjust(struct view *view, int direction, float dx, float dy)
{
    struct bsp_resize_preview_session *preview = &g_bsp_resize_preview;
    if (!preview->active || !view || view->sid != preview->sid ||
        !space_is_visible(view->sid) ||
        !bsp_preview_tree_matches(view->root, preview->baseline) ||
        !bsp_preview_area_equal(view->root->area, preview->initial_area)) {
        bsp_resize_preview_cancel();
        return false;
    }
    struct window_node *node = view_find_window_node(&preview->shadow, preview->source_wid);
    if (!node) return false;
    struct window_node *x_fence = NULL;
    struct window_node *y_fence = NULL;
    if (direction & HANDLE_TOP) x_fence = window_node_fence(node, DIR_NORTH);
    if (direction & HANDLE_BOTTOM) x_fence = window_node_fence(node, DIR_SOUTH);
    if (direction & HANDLE_LEFT) y_fence = window_node_fence(node, DIR_WEST);
    if (direction & HANDLE_RIGHT) y_fence = window_node_fence(node, DIR_EAST);

    bool changed = false;
    if (y_fence && y_fence->area.w > 0) {
        float ratio = view_clamp_split_ratio(&preview->shadow, y_fence,
                          y_fence->ratio + dx / y_fence->area.w);
        changed |= ratio != y_fence->ratio;
        y_fence->ratio = ratio;
    }
    if (x_fence && x_fence->area.h > 0) {
        float ratio = view_clamp_split_ratio(&preview->shadow, x_fence,
                          x_fence->ratio + dy / x_fence->area.h);
        changed |= ratio != x_fence->ratio;
        x_fence->ratio = ratio;
    }
    if (changed) {
        bsp_preview_layout(&preview->shadow, preview->shadow.root);
        bsp_preview_render();
        preview->changed = true;
    }
    return changed;
}

// AX clients can reject a requested size while still accepting its position.
// Never move a window to its proposed tile before checking whether it can
// actually shrink to fit. A preview may initially be optimistic if the app's
// minimum size is unknown; a size-only preflight learns that minimum on release.
struct bsp_preview_window_snapshot {
    struct window *window;
    CGRect original;
    CGSize requested;
    CGSize observed;
    int stable;
    bool shrink_requested;
};

static bool bsp_preview_geometry_matches(CGRect actual, CGRect target)
{
    return !AX_DIFF(actual.origin.x, target.origin.x) &&
           !AX_DIFF(actual.origin.y, target.origin.y) &&
           !AX_DIFF(actual.size.width, target.size.width) &&
           !AX_DIFF(actual.size.height, target.size.height);
}

static CGSize bsp_preview_probe_size(CGSize current, CGSize target)
{
    return CGSizeMake(min(current.width, target.width),
                      min(current.height, target.height));
}

static CGSize bsp_preview_learn_minimum(CGSize requested, CGSize observed, CGSize known)
{
    if (observed.width > requested.width + 1.5f) {
        known.width = max(known.width, observed.width);
    }
    if (observed.height > requested.height + 1.5f) {
        known.height = max(known.height, observed.height);
    }
    return known;
}

// Snapshot *actual* AX frames before any preflight resize. The window->frame
// cache is asynchronous and must not be used as a rollback source.
static bool bsp_preview_capture_snapshots(struct view *view,
                                          struct bsp_preview_window_snapshot **out,
                                          int *out_count)
{
    struct window_capture *windows = NULL;
    window_node_capture_windows(view->root, &windows);
    int count = ts_buf_len(windows);
    if (!count) return false;

    struct bsp_preview_window_snapshot *snapshots = calloc(count, sizeof(*snapshots));
    if (!snapshots) return false;

    for (int i = 0; i < count; ++i) {
        struct window *window = windows[i].window;
        CGRect frame = window_ax_frame(window);
        if (frame.size.width <= 0.0f || frame.size.height <= 0.0f) {
            free(snapshots);
            return false;
        }
        snapshots[i].window = window;
        snapshots[i].original = frame;
    }
    *out = snapshots;
    *out_count = count;
    return true;
}

// Preflight may change real sizes. Its caller MUST either apply and verify
// the final geometry, or explicitly restore every original AX frame.
static bool bsp_preview_preflight_sizes(struct view *shadow,
                                        struct bsp_preview_window_snapshot *snapshots,
                                        int count)
{
    struct window_capture *targets = NULL;
    window_node_capture_windows(shadow->root, &targets);
    if (ts_buf_len(targets) != count) return false;

    bool has_shrink = false;
    for (int i = 0; i < count; ++i) {
        struct bsp_preview_window_snapshot *probe = &snapshots[i];
        if (targets[i].window != probe->window) return false;
        CGSize target = CGSizeMake(targets[i].w, targets[i].h);
        if (target.width <= 0.0f || target.height <= 0.0f) return false;
        probe->requested = bsp_preview_probe_size(probe->original.size, target);
        probe->observed = probe->original.size;
        probe->shrink_requested =
            probe->requested.width < probe->original.size.width - 1.5f ||
            probe->requested.height < probe->original.size.height - 1.5f;
        if (probe->shrink_requested) {
            has_shrink = true;
            window_manager_resize_window(probe->window, probe->requested.width,
                                          probe->requested.height);
        } else {
            probe->stable = 2;
        }
    }

    if (has_shrink) {
        // Observe all clients in one bounded interval, not 200ms per window.
        // Do not mistake an immediate, stale AX readback for a client minimum.
        for (int attempt = 0; attempt < 4; ++attempt) {
            usleep(50000);
            bool all_stable = true;
            for (int i = 0; i < count; ++i) {
                struct bsp_preview_window_snapshot *probe = &snapshots[i];
                if (!probe->shrink_requested) continue;
                CGRect frame = window_ax_frame(probe->window);
                if (frame.size.width <= 0.0f || frame.size.height <= 0.0f) return false;
                if (fabs(frame.size.width - probe->observed.width) < 1.5f &&
                    fabs(frame.size.height - probe->observed.height) < 1.5f) {
                    probe->stable++;
                } else {
                    probe->stable = 0;
                }
                probe->observed = frame.size;
                if (probe->stable < 2) all_stable = false;
            }
            if (attempt >= 2 && all_stable) break;
        }
    }

    for (int i = 0; i < count; ++i) {
        struct bsp_preview_window_snapshot *probe = &snapshots[i];
        if (probe->stable < 2) return false;
    }

    for (int i = 0; i < count; ++i) {
        struct bsp_preview_window_snapshot *probe = &snapshots[i];
        if (!probe->shrink_requested) continue;
        CGSize learned = bsp_preview_learn_minimum(probe->requested,
                                                   probe->observed,
                                                   probe->window->live_resize_minimum);
        if (!CGSizeEqualToSize(learned, probe->window->live_resize_minimum)) {
            debug("bsp_resize_preview: AX minimum app=%s wid=%u w=%.0f h=%.0f\n",
                  probe->window->application->name, probe->window->id,
                  learned.width, learned.height);
            probe->window->live_resize_minimum = learned;
        }
    }
    return true;
}

// Explicit AX restoration is required even when the BSP ratios never changed:
// a failed preflight may already have shrunk a client at its old position.
// Re-read AX rather than trusting the window->frame cache.
static bool bsp_preview_restore_snapshots(struct bsp_preview_window_snapshot *snapshots,
                                          int count)
{
    bool restored = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        restored = true;
        for (int i = 0; i < count; ++i) {
            struct bsp_preview_window_snapshot *snapshot = &snapshots[i];
            CGRect actual = window_ax_frame(snapshot->window);
            if (!bsp_preview_geometry_matches(actual, snapshot->original)) {
                restored = false;
                window_manager_set_window_frame(snapshot->window,
                    snapshot->original.origin.x, snapshot->original.origin.y,
                    snapshot->original.size.width, snapshot->original.size.height);
            }
        }
        if (restored) break;
        usleep(50000);
    }
    for (int i = 0; i < count; ++i) {
        struct bsp_preview_window_snapshot *snapshot = &snapshots[i];
        CGRect actual = window_ax_frame(snapshot->window);
        snapshot->window->frame = actual;
        if (!bsp_preview_geometry_matches(actual, snapshot->original)) restored = false;
    }
    if (!restored) debug("bsp_resize_preview: AX rollback not fully accepted\n");
    return restored;
}

// A successful size preflight is not a successful move. The release must
// explicitly write and verify *both* size and position. In particular, a
// left-edge drag must move the left edge right while holding the right edge.
static bool bsp_preview_apply_frames(struct view *view,
                                     struct bsp_preview_window_snapshot *snapshots,
                                     int count)
{
    struct window_capture *targets = NULL;
    window_node_capture_windows(view->root, &targets);
    if (ts_buf_len(targets) != count) return false;

    for (int i = 0; i < count; ++i) {
        if (targets[i].window != snapshots[i].window ||
            targets[i].w <= 0.0f || targets[i].h <= 0.0f) return false;
    }

    bool applied = false;
    for (int attempt = 0; attempt < 4; ++attempt) {
        applied = true;
        for (int i = 0; i < count; ++i) {
            struct window *window = snapshots[i].window;
            CGRect target = CGRectMake(targets[i].x, targets[i].y,
                                       targets[i].w, targets[i].h);
            CGRect actual = window_ax_frame(window);
            if (!bsp_preview_geometry_matches(actual, target)) {
                applied = false;
                // The generic setter writes size-position-size, and does not
                // skip the move based on an optimistic live-resize frame cache.
                if (attempt == 0 || attempt == 2) {
                    window_manager_set_window_frame(window, target.origin.x,
                        target.origin.y, target.size.width, target.size.height);
                }
            }
        }
        if (applied) break;
        usleep(50000);
    }

    for (int i = 0; i < count; ++i) {
        struct window *window = snapshots[i].window;
        CGRect target = CGRectMake(targets[i].x, targets[i].y,
                                   targets[i].w, targets[i].h);
        CGRect actual = window_ax_frame(window);
        window->frame = actual;
        if (!bsp_preview_geometry_matches(actual, target)) applied = false;
    }
    return applied;
}

static void bsp_preview_clamp_subtree(struct view *view, struct window_node *node)
{
    if (!node || !node->left || !node->right) return;
    node->ratio = view_clamp_split_ratio(view, node, node->ratio);
    area_make_pair_for_node(view, node);
    bsp_preview_clamp_subtree(view, node->left);
    bsp_preview_clamp_subtree(view, node->right);
}

static bool bsp_preview_subtree_fits(struct view *view, struct window_node *node)
{
    if (!node) return true;
    if (node->left && node->right) {
        return bsp_preview_subtree_fits(view, node->left) &&
               bsp_preview_subtree_fits(view, node->right);
    }
    struct bsp_minimum_size minimum = window_node_minimum_size(view, node);
    return node->area.w + 1.5f >= minimum.w &&
           node->area.h + 1.5f >= minimum.h;
}

static void bsp_preview_restore_ratios(struct window_node *node,
                                       const struct window_node *baseline)
{
    node->ratio = baseline->ratio;
    if (node->left) bsp_preview_restore_ratios(node->left, baseline->left);
    if (node->right) bsp_preview_restore_ratios(node->right, baseline->right);
}

bool bsp_resize_preview_commit(struct view *view)
{
    struct bsp_resize_preview_session *preview = &g_bsp_resize_preview;
    bool committed = false;
    if (preview->active && preview->changed && view &&
        view->layout == VIEW_BSP && view->sid == preview->sid &&
        space_is_visible(view->sid) && !view_is_invalid(view) &&
        view->window_gap == preview->initial_gap &&
        view->split_type == preview->initial_split_type &&
        (view->flags & (VIEW_ENABLE_GAP | VIEW_ENABLE_PADDING)) == preview->initial_flags &&
        bsp_preview_area_equal(view->root->area, preview->initial_area) &&
        bsp_preview_tree_matches(view->root, preview->baseline)) {
        struct bsp_preview_window_snapshot *snapshots = NULL;
        int count = 0;
        if (bsp_preview_capture_snapshots(view, &snapshots, &count)) {
            // Probe the *shadow* layout first. Do not modify authoritative
            // BSP ratios until the observed minimum sizes can be satisfied.
            if (bsp_preview_preflight_sizes(&preview->shadow, snapshots, count)) {
                bsp_preview_clamp_subtree(&preview->shadow, preview->shadow.root);
                bsp_preview_layout(&preview->shadow, preview->shadow.root);
                if (bsp_preview_subtree_fits(&preview->shadow, preview->shadow.root)) {
                    bsp_preview_copy_changed_ratios(view->root, preview->shadow.root,
                                                    preview->baseline);
                    view_update(view);
                    committed = bsp_preview_apply_frames(view, snapshots, count);
                    if (!committed) {
                        debug("bsp_resize_preview: AX rejected final position or size; rolling back\n");
                    }
                } else {
                    debug("bsp_resize_preview: no feasible layout for AX minimum; rolling back\n");
                }
            } else {
                debug("bsp_resize_preview: AX size preflight did not settle; rolling back\n");
            }
            if (!committed) {
                bsp_preview_restore_ratios(view->root, preview->baseline);
                view_update(view);
                bsp_preview_restore_snapshots(snapshots, count);
            }
            free(snapshots);
        }
    }
    bsp_resize_preview_cancel();
    return committed;
}
