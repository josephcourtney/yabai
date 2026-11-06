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
        bsp_preview_copy_changed_ratios(view->root, preview->shadow.root, preview->baseline);
        view_update(view);
        // One real-window flush at release, no AX calls during pointer motion.
        view_flush_direct(view);
        committed = true;
    }
    bsp_resize_preview_cancel();
    return committed;
}
