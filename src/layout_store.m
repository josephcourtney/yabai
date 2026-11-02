#include "layout_store.h"
#include "layout_match.h"

static NSString *layout_store_resolve_path(const char *name_or_path, uint64_t sid)
{
    NSString *home = NSHomeDirectory();
    NSString *dir = [home stringByAppendingPathComponent:@".config/yabai/layouts"];

    if (!name_or_path || !*name_or_path) {
        int index = space_manager_mission_control_index(sid);
        if (index > 0) {
            return [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"space_%d.json", index]];
        }
        return [dir stringByAppendingPathComponent:@"default.json"];
    }

    NSString *str = [NSString stringWithUTF8String:name_or_path];
    if ([str hasPrefix:@"~/"]) {
        return [home stringByAppendingPathComponent:[str substringFromIndex:2]];
    } else if ([str hasPrefix:@"/"] || [str hasPrefix:@"."]) {
        return str;
    } else {
        if (![str hasSuffix:@".json"]) {
            str = [str stringByAppendingString:@".json"];
        }
        return [dir stringByAppendingPathComponent:str];
    }
}

static NSString *layout_store_resolve_session_path(const char *name_or_path)
{
    NSString *home = NSHomeDirectory();
    NSString *dir = [home stringByAppendingPathComponent:@".config/yabai/layouts"];

    if (!name_or_path || !*name_or_path) {
        return [dir stringByAppendingPathComponent:@"session.json"];
    }

    NSString *str = [NSString stringWithUTF8String:name_or_path];
    if ([str hasPrefix:@"~/"]) {
        return [home stringByAppendingPathComponent:[str substringFromIndex:2]];
    } else if ([str hasPrefix:@"/"] || [str hasPrefix:@"."]) {
        return str;
    } else {
        if (![str hasSuffix:@".json"]) {
            str = [str stringByAppendingString:@".json"];
        }
        return [dir stringByAppendingPathComponent:str];
    }
}

static bool layout_store_ensure_dir(NSString *path)
{
    NSString *dir = [path stringByDeletingLastPathComponent];
    NSFileManager *fm = [NSFileManager defaultManager];
    BOOL isDir = NO;
    if (![fm fileExistsAtPath:dir isDirectory:&isDir] || !isDir) {
        return [fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    }
    return true;
}

static NSDictionary *layout_store_serialize_node(struct window_node *node)
{
    if (!node) return nil;
    NSMutableDictionary *dict = [NSMutableDictionary dictionary];

    if (window_node_is_leaf(node)) {
        [dict setObject:@"leaf" forKey:@"type"];
        NSMutableArray *apps = [NSMutableArray array];
        NSMutableArray *titles = [NSMutableArray array];
        for (int i = 0; i < node->window_count; ++i) {
            struct window *w = window_manager_find_window(&g_window_manager, node->window_list[i]);
            if (w && w->application && w->application->name) {
                [apps addObject:[NSString stringWithUTF8String:w->application->name]];
                char *title = window_title_ts(w);
                [titles addObject:title ? [NSString stringWithUTF8String:title] : @""];
            }
        }
        [dict setObject:apps forKey:@"apps"];
        [dict setObject:titles forKey:@"titles"];
    } else {
        [dict setObject:@"branch" forKey:@"type"];
        [dict setObject:(node->split == SPLIT_X ? @"horizontal" : @"vertical") forKey:@"split"];
        [dict setObject:@(node->ratio) forKey:@"ratio"];
        if (node->left) {
            NSDictionary *left_dict = layout_store_serialize_node(node->left);
            if (left_dict) [dict setObject:left_dict forKey:@"left"];
        }
        if (node->right) {
            NSDictionary *right_dict = layout_store_serialize_node(node->right);
            if (right_dict) [dict setObject:right_dict forKey:@"right"];
        }
    }

    return dict;
}

static void layout_store_collect_tree_apps(NSDictionary *node_dict, NSMutableArray *apps)
{
    if (!node_dict || ![node_dict isKindOfClass:[NSDictionary class]]) return;
    NSString *type = node_dict[@"type"];
    if ([type isEqualToString:@"leaf"]) {
        NSArray *node_apps = node_dict[@"apps"];
        if (node_apps && [node_apps isKindOfClass:[NSArray class]]) {
            for (NSString *a in node_apps) {
                [apps addObject:a];
            }
        }
    } else {
        layout_store_collect_tree_apps(node_dict[@"left"], apps);
        layout_store_collect_tree_apps(node_dict[@"right"], apps);
    }
}

bool layout_store_save(uint64_t sid, const char *name_or_path, char *out_path, size_t out_path_size, char *err_msg, size_t err_size)
{
    @autoreleasepool {
        struct view *view = space_manager_find_view(&g_space_manager, sid);
        if (!view) {
            snprintf(err_msg, err_size, "could not locate view for space %llu", sid);
            return false;
        }

        if (view->layout != VIEW_BSP) {
            snprintf(err_msg, err_size, "can only save layout for BSP spaces (current is %s)", view_type_str[view->layout]);
            return false;
        }

        if (!view->root || (window_node_is_leaf(view->root) && !window_node_is_occupied(view->root))) {
            snprintf(err_msg, err_size, "space has no tiled windows to save");
            return false;
        }

        NSString *path = layout_store_resolve_path(name_or_path, sid);
        if (!layout_store_ensure_dir(path)) {
            snprintf(err_msg, err_size, "could not create directory for '%s'", [path UTF8String]);
            return false;
        }

        NSDictionary *tree_dict = layout_store_serialize_node(view->root);
        if (!tree_dict) {
            snprintf(err_msg, err_size, "failed to serialize window tree");
            return false;
        }

        NSDictionary *root_dict = @{
            @"version": @1,
            @"layout": @"bsp",
            @"top_padding": @(view->top_padding),
            @"bottom_padding": @(view->bottom_padding),
            @"left_padding": @(view->left_padding),
            @"right_padding": @(view->right_padding),
            @"window_gap": @(view->window_gap),
            @"tree": tree_dict
        };

        NSError *err = nil;
        NSData *data = [NSJSONSerialization dataWithJSONObject:root_dict options:NSJSONWritingPrettyPrinted error:&err];
        if (!data || err) {
            snprintf(err_msg, err_size, "JSON serialization error: %s", err ? [[err localizedDescription] UTF8String] : "unknown");
            return false;
        }

        if (![data writeToFile:path atomically:YES]) {
            snprintf(err_msg, err_size, "could not write file to '%s'", [path UTF8String]);
            return false;
        }

        if (out_path && out_path_size > 0) {
            snprintf(out_path, out_path_size, "%s", [path UTF8String]);
        }

        return true;
    }
}

bool layout_store_dump(uint64_t sid, FILE *rsp, char *err_msg, size_t err_size)
{
    @autoreleasepool {
        struct view *view = space_manager_find_view(&g_space_manager, sid);
        if (!view) {
            snprintf(err_msg, err_size, "could not locate view for space %llu", sid);
            return false;
        }

        if (view->layout != VIEW_BSP) {
            snprintf(err_msg, err_size, "can only dump layout for BSP spaces");
            return false;
        }

        if (!view->root || (window_node_is_leaf(view->root) && !window_node_is_occupied(view->root))) {
            snprintf(err_msg, err_size, "space has no tiled windows to dump");
            return false;
        }

        NSDictionary *tree_dict = layout_store_serialize_node(view->root);
        if (!tree_dict) {
            snprintf(err_msg, err_size, "failed to serialize window tree");
            return false;
        }

        NSDictionary *root_dict = @{
            @"version": @1,
            @"layout": @"bsp",
            @"top_padding": @(view->top_padding),
            @"bottom_padding": @(view->bottom_padding),
            @"left_padding": @(view->left_padding),
            @"right_padding": @(view->right_padding),
            @"window_gap": @(view->window_gap),
            @"tree": tree_dict
        };

        NSError *err = nil;
        NSData *data = [NSJSONSerialization dataWithJSONObject:root_dict options:NSJSONWritingPrettyPrinted error:&err];
        if (!data || err) {
            snprintf(err_msg, err_size, "JSON serialization error");
            return false;
        }

        NSString *json_str = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
        if (json_str) {
            fprintf(rsp, "%s\n", [json_str UTF8String]);
            [json_str release];
        }

        return true;
    }
}

/* These nodes have not been registered with the window manager: the normal
 * window_node_destroy() would remove already-managed live windows. */
static void layout_store_free_detached_node(struct window_node *node)
{
    if (!node) return;
    layout_store_free_detached_node(node->left);
    layout_store_free_detached_node(node->right);
    free(node);
}

static void layout_store_snapshot_keys(struct window **windows,
                                       struct layout_match_key *keys, int count)
{
    for (int i = 0; i < count; ++i) {
        keys[i].app = windows[i]->application ? windows[i]->application->name : NULL;
        keys[i].title = window_title_ts(windows[i]);
    }
}

static struct window_node *layout_store_reconstruct_node(
    NSDictionary *dict, struct window **avail,
    const struct layout_match_key *keys, bool *used, int avail_count,
    struct window_node *parent, enum window_node_child child,
    char *err_msg, size_t err_size)
{
    if (!dict || ![dict isKindOfClass:[NSDictionary class]]) {
        snprintf(err_msg, err_size, "invalid or missing layout tree node");
        return NULL;
    }

    NSString *type = dict[@"type"];
    if ([type isEqualToString:@"leaf"]) {
        NSArray *apps = dict[@"apps"];
        NSArray *titles = dict[@"titles"];
        if (![apps isKindOfClass:[NSArray class]] ||
            ![titles isKindOfClass:[NSArray class]] ||
            [apps count] != 1 || [titles count] != 1 ||
            ![apps[0] isKindOfClass:[NSString class]] ||
            ![titles[0] isKindOfClass:[NSString class]]) {
            snprintf(err_msg, err_size, "layout leaf requires exactly one app/title pair; stacked or legacy leaves cannot be safely restored");
            return NULL;
        }

        const char *app = [apps[0] UTF8String];
        const char *title = [titles[0] UTF8String];
        size_t index = 0;
        enum layout_match_result status = layout_match_unique(
            app, title, keys, used, (size_t) avail_count, &index);
        if (status != LAYOUT_MATCH_OK) {
            snprintf(err_msg, err_size, "cannot match layout window app='%.80s' title='%.80s': %s",
                     app, title,
                     status == LAYOUT_MATCH_AMBIGUOUS ? "ambiguous duplicate windows" :
                     status == LAYOUT_MATCH_ALREADY_USED ? "window referenced more than once" :
                     status == LAYOUT_MATCH_MISSING ? "no exact app/title match" : "invalid key");
            return NULL;
        }

        used[index] = true;
        struct window_node *leaf = calloc(1, sizeof(struct window_node));
        if (!leaf) {
            snprintf(err_msg, err_size, "out of memory reconstructing layout leaf");
            return NULL;
        }
        leaf->parent = parent;
        leaf->child = child;
        leaf->ratio = 0.5f;
        leaf->window_list[0] = avail[index]->id;
        leaf->window_order[0] = avail[index]->id;
        leaf->window_count = 1;
        return leaf;
    }

    if ([type isEqualToString:@"branch"]) {
        NSString *split = dict[@"split"];
        if (![split isEqualToString:@"horizontal"] && ![split isEqualToString:@"vertical"]) {
            snprintf(err_msg, err_size, "invalid branch split");
            return NULL;
        }

        struct window_node *branch = calloc(1, sizeof(struct window_node));
        if (!branch) {
            snprintf(err_msg, err_size, "out of memory reconstructing layout branch");
            return NULL;
        }
        branch->parent = parent;
        branch->child = child;
        branch->split = [split isEqualToString:@"horizontal"] ? SPLIT_X : SPLIT_Y;
        branch->ratio = [dict[@"ratio"] floatValue];
        if (branch->ratio < 0.05f || branch->ratio > 0.95f) branch->ratio = 0.5f;

        branch->left = layout_store_reconstruct_node(
            dict[@"left"], avail, keys, used, avail_count, branch, CHILD_FIRST,
            err_msg, err_size);
        if (!branch->left) {
            layout_store_free_detached_node(branch);
            return NULL;
        }
        branch->right = layout_store_reconstruct_node(
            dict[@"right"], avail, keys, used, avail_count, branch, CHILD_SECOND,
            err_msg, err_size);
        if (!branch->right) {
            layout_store_free_detached_node(branch);
            return NULL;
        }
        return branch;
    }

    snprintf(err_msg, err_size, "unknown layout node type");
    return NULL;
}

bool layout_store_restore(uint64_t sid, const char *name_or_path, char *out_path, size_t out_path_size, char *err_msg, size_t err_size)
{
    @autoreleasepool {
        struct view *view = space_manager_find_view(&g_space_manager, sid);
        if (!view) {
            snprintf(err_msg, err_size, "could not locate view for space %llu", sid);
            return false;
        }

        NSString *path = layout_store_resolve_path(name_or_path, sid);
        NSData *data = [NSData dataWithContentsOfFile:path];
        if (!data) {
            snprintf(err_msg, err_size, "layout file not found or unreadable: '%s'", [path UTF8String]);
            return false;
        }

        NSError *err = nil;
        NSDictionary *root_dict = [NSJSONSerialization JSONObjectWithData:data options:0 error:&err];
        if (!root_dict || ![root_dict isKindOfClass:[NSDictionary class]] || err) {
            snprintf(err_msg, err_size, "invalid JSON format in '%s'", [path UTF8String]);
            return false;
        }

        if (root_dict[@"spaces"] && [root_dict[@"spaces"] isKindOfClass:[NSArray class]]) {
            return layout_store_restore_session(name_or_path, out_path, out_path_size, err_msg, err_size);
        }

        NSDictionary *tree_dict = root_dict[@"tree"];
        if (!tree_dict || ![tree_dict isKindOfClass:[NSDictionary class]]) {
            snprintf(err_msg, err_size, "missing 'tree' key in layout file '%s'", [path UTF8String]);
            return false;
        }

        int total_windows = 0;
        uint32_t *raw_list = space_window_list(sid, &total_windows, false);
        struct window *avail[256];
        bool used[256] = {false};
        struct layout_match_key keys[256];
        int avail_count = 0;

        for (int i = 0; i < total_windows && avail_count < 256; ++i) {
            struct window *w = window_manager_find_window(&g_window_manager, raw_list[i]);
            if (w && window_manager_is_window_eligible(w) && !window_check_flag(w, WINDOW_FLOAT)) {
                avail[avail_count++] = w;
            }
        }

        if (avail_count == 0) {
            snprintf(err_msg, err_size, "no eligible managed windows found on space %llu to apply layout", sid);
            return false;
        }

        layout_store_snapshot_keys(avail, keys, avail_count);
        struct window_node *new_root = layout_store_reconstruct_node(
            tree_dict, avail, keys, used, avail_count, NULL, CHILD_NONE, err_msg, err_size);
        if (!new_root) return false;

        view->layout = VIEW_BSP;

        if (view->root) {
            insert_feedback_destroy(view->root);
            window_node_destroy(view->root);
        }
        view->root = new_root;

        for (int i = 0; i < avail_count; ++i) {
            if (!used[i]) {
                view_add_window_node_with_insertion_point(view, avail[i], 0);
            }
            window_manager_add_managed_window(&g_window_manager, avail[i], view);
        }

        if (root_dict[@"top_padding"])    view->top_padding    = [root_dict[@"top_padding"] intValue];
        if (root_dict[@"bottom_padding"]) view->bottom_padding = [root_dict[@"bottom_padding"] intValue];
        if (root_dict[@"left_padding"])   view->left_padding   = [root_dict[@"left_padding"] intValue];
        if (root_dict[@"right_padding"])  view->right_padding  = [root_dict[@"right_padding"] intValue];
        if (root_dict[@"window_gap"])     view->window_gap     = [root_dict[@"window_gap"] intValue];

        view_update(view);
        view_flush(view);

        if (out_path && out_path_size > 0) {
            snprintf(out_path, out_path_size, "%s", [path UTF8String]);
        }

        return true;
    }
}

static NSDictionary *layout_store_build_session_dict(void)
{
    int display_count = 0;
    uint32_t *display_list = display_manager_active_display_list(&display_count);
    if (!display_list || display_count == 0) return nil;

    NSMutableArray *spaces_arr = [NSMutableArray array];

    for (int i = 0; i < display_count; ++i) {
        int space_count = 0;
        uint64_t *space_list = display_space_list(display_list[i], &space_count);
        if (!space_list) continue;

        for (int j = 0; j < space_count; ++j) {
            uint64_t sid = space_list[j];
            if (!space_is_user(sid)) continue;

            int mci = space_manager_mission_control_index(sid);
            struct view *view = space_manager_find_view(&g_space_manager, sid);
            if (!view) continue;

            NSMutableDictionary *s_dict = [NSMutableDictionary dictionary];
            [s_dict setObject:@(mci) forKey:@"index"];
            [s_dict setObject:@(i + 1) forKey:@"display"];
            [s_dict setObject:[NSString stringWithUTF8String:view_type_str[view->layout]] forKey:@"layout"];
            [s_dict setObject:@(view->top_padding) forKey:@"top_padding"];
            [s_dict setObject:@(view->bottom_padding) forKey:@"bottom_padding"];
            [s_dict setObject:@(view->left_padding) forKey:@"left_padding"];
            [s_dict setObject:@(view->right_padding) forKey:@"right_padding"];
            [s_dict setObject:@(view->window_gap) forKey:@"window_gap"];

            if (view->layout == VIEW_BSP && view->root && (!window_node_is_leaf(view->root) || window_node_is_occupied(view->root))) {
                NSDictionary *tree_dict = layout_store_serialize_node(view->root);
                if (tree_dict) [s_dict setObject:tree_dict forKey:@"tree"];
            }

            int w_count = 0;
            uint32_t *w_list = space_window_list(sid, &w_count, false);
            NSMutableArray *win_arr = [NSMutableArray array];
            for (int k = 0; k < w_count; ++k) {
                struct window *w = window_manager_find_window(&g_window_manager, w_list[k]);
                if (w && w->application && w->application->name) {
                    NSMutableDictionary *w_dict = [NSMutableDictionary dictionary];
                    [w_dict setObject:[NSString stringWithUTF8String:w->application->name] forKey:@"app"];
                    char *title = window_title_ts(w);
                    [w_dict setObject:(title ? [NSString stringWithUTF8String:title] : @"") forKey:@"title"];
                    [win_arr addObject:w_dict];
                }
            }
            [s_dict setObject:win_arr forKey:@"windows"];

            [spaces_arr addObject:s_dict];
        }
    }

    return @{
        @"version": @1,
        @"type": @"session",
        @"spaces": spaces_arr
    };
}

bool layout_store_save_session(const char *name_or_path, char *out_path, size_t out_path_size, char *err_msg, size_t err_size)
{
    @autoreleasepool {
        NSDictionary *root_dict = layout_store_build_session_dict();
        if (!root_dict) {
            snprintf(err_msg, err_size, "failed to collect space session state");
            return false;
        }

        NSString *path = layout_store_resolve_session_path(name_or_path);
        if (!layout_store_ensure_dir(path)) {
            snprintf(err_msg, err_size, "could not create directory for '%s'", [path UTF8String]);
            return false;
        }

        NSError *err = nil;
        NSData *data = [NSJSONSerialization dataWithJSONObject:root_dict options:NSJSONWritingPrettyPrinted error:&err];
        if (!data || err) {
            snprintf(err_msg, err_size, "JSON serialization error: %s", err ? [[err localizedDescription] UTF8String] : "unknown");
            return false;
        }

        if (![data writeToFile:path atomically:YES]) {
            snprintf(err_msg, err_size, "could not write session file to '%s'", [path UTF8String]);
            return false;
        }

        if (out_path && out_path_size > 0) {
            snprintf(out_path, out_path_size, "%s", [path UTF8String]);
        }

        return true;
    }
}

bool layout_store_dump_session(FILE *rsp, char *err_msg, size_t err_size)
{
    @autoreleasepool {
        NSDictionary *root_dict = layout_store_build_session_dict();
        if (!root_dict) {
            snprintf(err_msg, err_size, "failed to collect space session state");
            return false;
        }

        NSError *err = nil;
        NSData *data = [NSJSONSerialization dataWithJSONObject:root_dict options:NSJSONWritingPrettyPrinted error:&err];
        if (!data || err) {
            snprintf(err_msg, err_size, "JSON serialization error");
            return false;
        }

        NSString *json_str = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
        if (json_str) {
            fprintf(rsp, "%s\n", [json_str UTF8String]);
            [json_str release];
        }

        return true;
    }
}

bool layout_store_restore_session(const char *name_or_path, char *out_path, size_t out_path_size, char *err_msg, size_t err_size)
{
    @autoreleasepool {
        NSString *path = layout_store_resolve_session_path(name_or_path);
        NSData *data = [NSData dataWithContentsOfFile:path];
        if (!data) {
            snprintf(err_msg, err_size, "session file not found or unreadable: '%s'", [path UTF8String]);
            return false;
        }

        NSError *err = nil;
        NSDictionary *root_dict = [NSJSONSerialization JSONObjectWithData:data options:0 error:&err];
        if (!root_dict || ![root_dict isKindOfClass:[NSDictionary class]] || err) {
            snprintf(err_msg, err_size, "invalid JSON format in '%s'", [path UTF8String]);
            return false;
        }

        NSArray *spaces_arr = root_dict[@"spaces"];
        if (!spaces_arr || ![spaces_arr isKindOfClass:[NSArray class]]) {
            snprintf(err_msg, err_size, "missing 'spaces' array in session file '%s'", [path UTF8String]);
            return false;
        }

        int display_count = 0;
        uint32_t *display_list = display_manager_active_display_list(&display_count);
        struct window *all_windows[512];
        struct layout_match_key all_keys[512];
        bool globally_used[512] = {false};
        int all_count = 0;

        for (int i = 0; i < display_count && all_count < 512; ++i) {
            int space_count = 0;
            uint64_t *space_list = display_space_list(display_list[i], &space_count);
            if (!space_list) continue;

            for (int j = 0; j < space_count && all_count < 512; ++j) {
                uint64_t sid = space_list[j];
                if (!space_is_user(sid)) continue;

                int w_count = 0;
                uint32_t *w_list = space_window_list(sid, &w_count, false);
                for (int k = 0; k < w_count && all_count < 512; ++k) {
                    struct window *w = window_manager_find_window(&g_window_manager, w_list[k]);
                    if (w && window_manager_is_window_eligible(w) && !window_check_flag(w, WINDOW_FLOAT)) {
                        all_windows[all_count] = w;
                        all_count++;
                    }
                }
            }
        }

        /* Plan and validate EVERY space before moving a single window.
         * A duplicate app/title is not a stable identity and must fail closed.
         * Restoration remains nontransactional if the OS rejects a later move. */
        layout_store_snapshot_keys(all_windows, all_keys, all_count);
        NSMutableDictionary *planned_windows = [NSMutableDictionary dictionary];
        for (NSDictionary *s_dict in spaces_arr) {
            if (![s_dict isKindOfClass:[NSDictionary class]]) {
                snprintf(err_msg, err_size, "invalid space entry in session");
                return false;
            }
            int mci = [s_dict[@"index"] intValue];
            uint64_t sid = mci > 0 ? space_manager_mission_control_space(mci) : 0;
            if (!sid || !space_manager_find_view(&g_space_manager, sid) ||
                planned_windows[@(mci)]) {
                snprintf(err_msg, err_size, "missing or duplicate target Space index %d", mci);
                return false;
            }

            NSArray *descriptors = s_dict[@"windows"];
            if (![descriptors isKindOfClass:[NSArray class]] || [descriptors count] > 256) {
                snprintf(err_msg, err_size, "Space %d has invalid window descriptors", mci);
                return false;
            }

            NSString *layout_str = s_dict[@"layout"];
            if (![layout_str isEqualToString:@"bsp"] &&
                ![layout_str isEqualToString:@"stack"] &&
                ![layout_str isEqualToString:@"float"]) {
                snprintf(err_msg, err_size, "Space %d has unsupported layout", mci);
                return false;
            }

            NSMutableArray *indexes = [NSMutableArray array];
            for (NSDictionary *w_dict in descriptors) {
                if (![w_dict isKindOfClass:[NSDictionary class]] ||
                    ![w_dict[@"app"] isKindOfClass:[NSString class]] ||
                    ![w_dict[@"title"] isKindOfClass:[NSString class]]) {
                    snprintf(err_msg, err_size, "Space %d has invalid app/title descriptor", mci);
                    return false;
                }

                const char *app = [w_dict[@"app"] UTF8String];
                const char *title = [w_dict[@"title"] UTF8String];
                size_t index = 0;
                enum layout_match_result status = layout_match_unique(
                    app, title, all_keys, globally_used, (size_t) all_count, &index);
                if (status != LAYOUT_MATCH_OK) {
                    snprintf(err_msg, err_size,
                             "Space %d cannot match window app='%.80s' title='%.80s': %s",
                             mci, app, title,
                             status == LAYOUT_MATCH_AMBIGUOUS ? "ambiguous duplicates" :
                             status == LAYOUT_MATCH_ALREADY_USED ? "window reused" :
                             status == LAYOUT_MATCH_MISSING ? "no exact match" : "invalid key");
                    return false;
                }
                globally_used[index] = true;
                [indexes addObject:@((int) index)];
            }
            planned_windows[@(mci)] = indexes;

            /* Validate the saved BSP tree against the planned occupants,
             * including title identity and leaf cardinality, before changes. */
            NSDictionary *tree_dict = s_dict[@"tree"];
            if ([layout_str isEqualToString:@"bsp"] && tree_dict) {
                if (![tree_dict isKindOfClass:[NSDictionary class]]) {
                    snprintf(err_msg, err_size, "Space %d has invalid BSP tree", mci);
                    return false;
                }
                struct window *planned[256];
                struct layout_match_key keys[256];
                bool used[256] = {false};
                for (NSUInteger j = 0; j < [indexes count]; ++j) {
                    int k = [indexes[j] intValue];
                    planned[j] = all_windows[k];
                    keys[j] = all_keys[k];
                }
                struct window_node *check = layout_store_reconstruct_node(
                    tree_dict, planned, keys, used, (int) [indexes count],
                    NULL, CHILD_NONE, err_msg, err_size);
                if (!check) return false;
                layout_store_free_detached_node(check);
            }
        }

        /* Refuse a partial saved-window set: unplanned eligible windows must
         * not be silently moved or abandoned in an old layout tree. */
        for (int i = 0; i < all_count; ++i) {
            if (!globally_used[i]) {
                snprintf(err_msg, err_size, "session does not describe every eligible live window; refusing partial restore");
                return false;
            }
        }

        for (NSDictionary *s_dict in spaces_arr) {
            int mci = [s_dict[@"index"] intValue];
            uint64_t sid = space_manager_mission_control_space(mci);
            struct view *view = space_manager_find_view(&g_space_manager, sid);
            NSArray *indexes = planned_windows[@(mci)];

            struct window *space_windows[256];
            struct layout_match_key space_keys[256];
            bool space_used[256] = {false};
            int space_win_count = (int) [indexes count];
            for (int j = 0; j < space_win_count; ++j) {
                int k = [indexes[j] intValue];
                space_windows[j] = all_windows[k];
                space_keys[j] = all_keys[k];
            }

            for (int m = 0; m < space_win_count; ++m) {
                struct window *w = space_windows[m];
                struct view *src_view = window_manager_find_managed_window(&g_window_manager, w);
                if (src_view && src_view->sid != sid) {
                    space_manager_untile_window(src_view, w);
                    window_manager_remove_managed_window(&g_window_manager, w->id);
                    space_manager_move_window_to_space(sid, w);
                } else if (!src_view) {
                    space_manager_move_window_to_space(sid, w);
                }
            }

            NSString *layout_str = s_dict[@"layout"];
            enum view_type new_layout = VIEW_BSP;
            if ([layout_str isEqualToString:@"stack"]) new_layout = VIEW_STACK;
            else if ([layout_str isEqualToString:@"float"]) new_layout = VIEW_FLOAT;
            view->layout = new_layout;

            if (s_dict[@"top_padding"])    view->top_padding    = [s_dict[@"top_padding"] intValue];
            if (s_dict[@"bottom_padding"]) view->bottom_padding = [s_dict[@"bottom_padding"] intValue];
            if (s_dict[@"left_padding"])   view->left_padding   = [s_dict[@"left_padding"] intValue];
            if (s_dict[@"right_padding"])  view->right_padding  = [s_dict[@"right_padding"] intValue];
            if (s_dict[@"window_gap"])     view->window_gap     = [s_dict[@"window_gap"] intValue];

            if (view->layout == VIEW_BSP) {
                NSDictionary *tree_dict = s_dict[@"tree"];
                if (tree_dict && [tree_dict isKindOfClass:[NSDictionary class]] && space_win_count > 0) {
                    struct window_node *new_root = layout_store_reconstruct_node(
                        tree_dict, space_windows, space_keys, space_used, space_win_count,
                        NULL, CHILD_NONE, err_msg, err_size);
                    if (new_root) {
                        if (view->root) {
                            insert_feedback_destroy(view->root);
                            window_node_destroy(view->root);
                        }
                        view->root = new_root;

                        for (int k = 0; k < space_win_count; ++k) {
                            if (!space_used[k]) {
                                view_add_window_node_with_insertion_point(view, space_windows[k], 0);
                            }
                            window_manager_add_managed_window(&g_window_manager, space_windows[k], view);
                        }
                    }
                } else {
                    if (view->root) {
                        insert_feedback_destroy(view->root);
                        window_node_destroy(view->root);
                    }
                    view->root = malloc(sizeof(struct window_node));
                    memset(view->root, 0, sizeof(struct window_node));
                    for (int k = 0; k < space_win_count; ++k) {
                        view_add_window_node_with_insertion_point(view, space_windows[k], 0);
                        window_manager_add_managed_window(&g_window_manager, space_windows[k], view);
                    }
                }
                view_update(view);
                view_flush(view);
            } else if (view->layout == VIEW_STACK) {
                if (view->root) {
                    insert_feedback_destroy(view->root);
                    window_node_destroy(view->root);
                }
                view->root = malloc(sizeof(struct window_node));
                memset(view->root, 0, sizeof(struct window_node));
                for (int k = 0; k < space_win_count; ++k) {
                    view_add_window_node(view, space_windows[k]);
                    window_manager_adjust_layer(space_windows[k], LAYER_BELOW);
                    window_manager_add_managed_window(&g_window_manager, space_windows[k], view);
                }
                view_update(view);
                view_flush(view);
            } else if (view->layout == VIEW_FLOAT) {
                view_clear(view);
                view_update(view);
                view_flush(view);
            }
        }

        if (out_path && out_path_size > 0) {
            snprintf(out_path, out_path_size, "%s", [path UTF8String]);
        }

        return true;
    }
}
