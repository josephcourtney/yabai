#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "layout_match.h"

static void test_unique_titles(void)
{
    const struct layout_match_key keys[] = {
        {"kitty", "editor"},
        {"kitty", "shell"},
        {"Zen", "Inspect yabai"}
    };
    bool used[] = {false, false, false};
    size_t index = 99;

    assert(layout_match_unique("kitty", "shell", keys, used, 3, &index) == LAYOUT_MATCH_OK);
    assert(index == 1);
    used[index] = true;
    assert(layout_match_unique("kitty", "shell", keys, used, 3, &index) == LAYOUT_MATCH_ALREADY_USED);
    assert(layout_match_unique("kitty", "editor", keys, used, 3, &index) == LAYOUT_MATCH_OK);
    assert(index == 0);
    assert(layout_match_unique("kitty", "missing", keys, used, 3, &index) == LAYOUT_MATCH_MISSING);
    assert(layout_match_unique("Zen", "Inspect yabai", keys, used, 3, &index) == LAYOUT_MATCH_OK);
    assert(index == 2);
}

static void test_duplicate_titles_fail_closed(void)
{
    const struct layout_match_key keys[] = {
        {"kitty", "~"},
        {"kitty", "~"},
        {"kitty", "nvim"}
    };
    const bool used[] = {false, true, false};
    size_t index = 77;

    assert(layout_match_unique("kitty", "~", keys, used, 3, &index) == LAYOUT_MATCH_AMBIGUOUS);
    assert(index == 77);
    assert(layout_match_unique("kitty", "nvim", keys, used, 3, &index) == LAYOUT_MATCH_OK);
    assert(index == 2);
}

static void test_invalid_and_empty_title(void)
{
    const struct layout_match_key keys[] = {
        {"kitty", ""},
        {"kitty", "work"}
    };
    const bool used[] = {false, false};
    size_t index = 77;
    assert(layout_match_unique("kitty", "", keys, used, 2, &index) == LAYOUT_MATCH_OK);
    assert(index == 0);
    assert(layout_match_unique("kitty", NULL, keys, used, 2, &index) == LAYOUT_MATCH_INVALID);
    assert(layout_match_unique(NULL, "work", keys, used, 2, &index) == LAYOUT_MATCH_INVALID);
}

int main(void)
{
    test_unique_titles();
    test_duplicate_titles_fail_closed();
    test_invalid_and_empty_title();
    return 0;
}
