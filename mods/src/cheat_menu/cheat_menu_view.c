/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "cheat_menu_view.h"

#include <stdio.h>
#include <string.h>

static int is_key(const RecompRuntimeUiItem *item, const char *key)
{
    return item && item->key && strcmp(item->key, key) == 0;
}

static void set_item(RecompRuntimeUiItem *it, const char *key, const char *label,
                     const char *description, RecompRuntimeUiItemType type)
{
    memset(it, 0, sizeof *it);
    it->key = key;
    it->section = CHEAT_MENU_SECTION;
    it->label = label;
    it->description = description;
    it->type = type;
    if (type == RECOMP_RUNTIME_UI_BOOL) {
        it->minimum = 0;
        it->maximum = 1;
        it->step = 1;
    }
}

/* Row description for the snapshot's state. Static strings plus the core's
 * static status name only. */
static void refresh_text(cheat_menu_view *v)
{
    const cheat_menu_snapshot s = v->state.snapshot;
    switch (s.state) {
    case TS2_CHEAT_OFF:
        snprintf(v->coins_text, sizeof v->coins_text,
                 "Off - code 800B2226 0063 holds coins at 99");
        break;
    case TS2_CHEAT_ARMED:
        snprintf(v->coins_text, sizeof v->coins_text,
                 "Armed - applies once the game is running");
        break;
    case TS2_CHEAT_ACTIVE:
        snprintf(v->coins_text, sizeof v->coins_text,
                 "Active - coins held at 99");
        break;
    case TS2_CHEAT_ERROR:
        snprintf(v->coins_text, sizeof v->coins_text,
                 "Error (%s) - stopped, toggle to retry",
                 s.last_status_name ? s.last_status_name : "unknown");
        break;
    case TS2_CHEAT_UNAVAILABLE:
    default:
        snprintf(v->coins_text, sizeof v->coins_text,
                 "Unavailable - cheat engine not set up");
        break;
    }
}

void cheat_menu_view_init(cheat_menu_view *v)
{
    if (!v) return;
    memset(v, 0, sizeof *v);
    cheat_menu_state_init(&v->state);
    set_item(&v->items[0], CHEAT_MENU_KEY_RESUME, "Resume Game",
             "Close the menu and continue playing.", RECOMP_RUNTIME_UI_ACTION);
    set_item(&v->items[1], CHEAT_MENU_KEY_MAX_COINS, "Max Coins",
             v->coins_text, RECOMP_RUNTIME_UI_BOOL);
    refresh_text(v);
}

const RecompRuntimeUiItem *cheat_menu_view_items(const cheat_menu_view *v, size_t *count)
{
    if (count) *count = v ? CHEAT_MENU_ROW_COUNT : 0u;
    return v ? v->items : NULL;
}

int cheat_menu_view_get_value(void *ctx, const RecompRuntimeUiItem *item, int *value)
{
    const cheat_menu_view *v = (const cheat_menu_view *)ctx;
    if (!v || !value || !is_key(item, CHEAT_MENU_KEY_MAX_COINS)) return 0;
    /* 0 makes recomp-ui show "Unavailable" instead of On/Off. */
    if (v->state.snapshot.state == TS2_CHEAT_UNAVAILABLE) return 0;
    *value = cheat_menu_state_display_enabled(&v->state);
    return 1;
}

int cheat_menu_view_set_value(void *ctx, const RecompRuntimeUiItem *item, int value)
{
    cheat_menu_view *v = (cheat_menu_view *)ctx;
    if (!v || !is_key(item, CHEAT_MENU_KEY_MAX_COINS)) return 0;
    if (v->state.snapshot.state == TS2_CHEAT_UNAVAILABLE) return 0;
    /* Mailbox only: the engine sees it at the next main-thread sync. */
    cheat_menu_state_request(&v->state, value != 0);
    return 1;
}

int cheat_menu_view_is_enabled(void *ctx, const RecompRuntimeUiItem *item)
{
    const cheat_menu_view *v = (const cheat_menu_view *)ctx;
    if (!v) return 0;
    if (is_key(item, CHEAT_MENU_KEY_MAX_COINS))
        return v->state.snapshot.state != TS2_CHEAT_UNAVAILABLE;
    return 1;
}

void cheat_menu_view_callbacks(cheat_menu_view *v, RecompRuntimeUiCallbacks *cb)
{
    if (!cb) return;
    cb->context = v;
    cb->get_value = cheat_menu_view_get_value;
    cb->set_value = cheat_menu_view_set_value;
    cb->is_enabled = cheat_menu_view_is_enabled;
}

int cheat_menu_view_sync(cheat_menu_view *v)
{
    int applied;
    if (!v) return 0;
    applied = cheat_menu_state_sync(&v->state);
    refresh_text(v);
    return applied;
}

const char *cheat_menu_view_status(cheat_menu_view *v)
{
    const char *what;
    if (!v) return "";
    switch (v->state.snapshot.state) {
    case TS2_CHEAT_OFF: what = "off"; break;
    case TS2_CHEAT_ARMED: what = "armed, applies after resume"; break;
    case TS2_CHEAT_ACTIVE: what = "active"; break;
    case TS2_CHEAT_ERROR: what = "error"; break;
    case TS2_CHEAT_UNAVAILABLE:
    default: what = "unavailable"; break;
    }
    snprintf(v->status_text, sizeof v->status_text, "Max Coins: %s", what);
    return v->status_text;
}
