/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "cheat_menu_view.h"

#include <stdio.h>
#include <string.h>

#include "cheat_menu.h"     /* package / feature ids */
#include "psx_menu_host.h"  /* PsxMenuHost (struct only) */
#include "ts2_cheat_engine.h"
#include "ts2_cheat_external.h"

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

int cheat_menu_view_row_index(const cheat_menu_view *v, const RecompRuntimeUiItem *item)
{
    size_t i;
    if (!v || !item) return -1;
    /* recomp-ui hands back pointers into the items array it was given. */
    if (item >= &v->items[1] && item < &v->items[1 + v->cheat_count])
        return (int)(item - &v->items[1]);
    /* Anything else (a copied item): match the key exactly. */
    if (!item->key) return -1;
    for (i = 0; i < v->cheat_count; ++i)
        if (v->items[1 + i].key && strcmp(item->key, v->items[1 + i].key) == 0)
            return (int)i;
    return -1;
}

/* Row i may be shown as a live cheat and take requests. */
static int row_available(const cheat_menu_view *v, size_t i)
{
    return i < v->cheat_count && v->bound[i] &&
           v->state.snapshots[i].state != TS2_CHEAT_UNAVAILABLE;
}

/* Row description for the row's snapshot. Static strings, the table's
 * hints and the core's static status name only. */
static void refresh_text(cheat_menu_view *v, size_t i)
{
    const ts2_cheat_def *d = &v->defs[i];
    char *out = v->row_text[i];
    const size_t n = sizeof v->row_text[i];
    const cheat_menu_snapshot s = v->state.snapshots[i];
    switch (v->bound[i] ? s.state : TS2_CHEAT_UNAVAILABLE) {
    case TS2_CHEAT_OFF:
        if (d->off_hint) snprintf(out, n, "Off - %s", d->off_hint);
        else snprintf(out, n, "Off");
        break;
    case TS2_CHEAT_ARMED:
        snprintf(out, n, "Armed - applies once the game is running");
        break;
    case TS2_CHEAT_ACTIVE:
        snprintf(out, n, "Active - %s", d->active_hint ? d->active_hint : "applied every frame");
        break;
    case TS2_CHEAT_ERROR:
        snprintf(out, n, "Error (%s) - stopped, toggle to retry",
                 s.last_status_name ? s.last_status_name : "unknown");
        break;
    case TS2_CHEAT_UNAVAILABLE:
    default:
        snprintf(out, n, "Unavailable - cheat engine not set up");
        break;
    }
}

void cheat_menu_view_init_table(cheat_menu_view *v, const ts2_cheat_def *defs, size_t n)
{
    size_t i;
    if (!v) return;
    memset(v, 0, sizeof *v);
    cheat_menu_state_init(&v->state);
    set_item(&v->items[0], CHEAT_MENU_KEY_RESUME, "Resume Game",
             "Close the menu and continue playing.", RECOMP_RUNTIME_UI_ACTION);
    /* Resume only unless every effective key is valid and unique. */
    if (!defs || n > CHEAT_MENU_MAX_CHEATS ||
        ts2_cheat_validate_menu_keys(defs, n) != TS2_CHEAT_TABLE_OK)
        return;
    v->defs = defs;
    v->cheat_count = n;
    for (i = 0; i < n; ++i) {
        const ts2_cheat_def *d = &defs[i];
        (void)ts2_cheat_menu_key(d, v->row_key[i], sizeof v->row_key[i]);  /* validated above */
        set_item(&v->items[1 + i], v->row_key[i],
                 d->label ? d->label : (d->id ? d->id : "Cheat"),
                 v->row_text[i], RECOMP_RUNTIME_UI_BOOL);
        refresh_text(v, i);
    }
}

void cheat_menu_view_init(cheat_menu_view *v)
{
    size_t n = 0;
    const ts2_cheat_def *defs = ts2_cheat_active_table(&n);
    cheat_menu_view_init_table(v, defs, n);
}

const RecompRuntimeUiItem *cheat_menu_view_items(const cheat_menu_view *v, size_t *count)
{
    if (count) *count = v ? 1u + v->cheat_count : 0u;
    return v ? v->items : NULL;
}

int cheat_menu_view_get_value(void *ctx, const RecompRuntimeUiItem *item, int *value)
{
    const cheat_menu_view *v = (const cheat_menu_view *)ctx;
    const int i = cheat_menu_view_row_index(v, item);
    if (i < 0 || !value) return 0;
    /* 0 makes recomp-ui show "Unavailable" instead of On/Off. */
    if (!row_available(v, (size_t)i)) return 0;
    *value = cheat_menu_state_display_enabled_at(&v->state, (size_t)i);
    return 1;
}

int cheat_menu_view_set_value(void *ctx, const RecompRuntimeUiItem *item, int value)
{
    cheat_menu_view *v = (cheat_menu_view *)ctx;
    const int i = cheat_menu_view_row_index(v, item);
    if (i < 0 || !row_available(v, (size_t)i)) return 0;
    /* Mailbox only: the engine sees it at the next main-thread sync. */
    return cheat_menu_state_request_at(&v->state, (size_t)i, value != 0);
}

int cheat_menu_view_is_enabled(void *ctx, const RecompRuntimeUiItem *item)
{
    const cheat_menu_view *v = (const cheat_menu_view *)ctx;
    int i;
    if (!v) return 0;
    i = cheat_menu_view_row_index(v, item);
    return i < 0 ? 1 : row_available(v, (size_t)i);
}

void cheat_menu_view_callbacks(cheat_menu_view *v, RecompRuntimeUiCallbacks *cb)
{
    if (!cb) return;
    cb->context = v;
    cb->get_value = cheat_menu_view_get_value;
    cb->set_value = cheat_menu_view_set_value;
    cb->is_enabled = cheat_menu_view_is_enabled;
}

int cheat_menu_persist_via_host(void *ctx, const char *option_id, const char *value)
{
    const PsxMenuHost *host = (const PsxMenuHost *)ctx;
    if (!option_id || !value || !host || !host->mod_set) return 0;
    if (host->netplay_active && host->netplay_active()) return 0;
    (void)host->mod_set(TS2_CHEAT_MENU_PACKAGE_ID, TS2_CHEAT_MENU_FEATURE_ID,
                        option_id, value);
    return 1;
}

void cheat_menu_view_set_persist(cheat_menu_view *v, int remember,
                                 cheat_menu_persist_fn fn, void *ctx)
{
    if (!v) return;
    v->remember = remember != 0;
    v->persist = fn;
    v->persist_ctx = ctx;
}

int cheat_menu_view_sync(cheat_menu_view *v)
{
    size_t i;
    uint32_t asked = 0;
    int applied, failed = 0, sent = 0, session = 0;
    if (!v) return 0;

    /* Bind rows to engine slots. A request on an unbound row never reaches
     * the engine (that slot could hold a different cheat); it still counts
     * as requested, so the footer reports the row as unavailable. */
    for (i = 0; i < v->cheat_count; ++i) {
        v->bound[i] = i < ts2_cheat_engine_count() &&
                      ts2_cheat_engine_def(i) == &v->defs[i];
        if (cheat_menu_state_pending_at(&v->state, i, NULL)) {
            asked |= 1u << i;
            if (!v->bound[i]) cheat_menu_state_discard_at(&v->state, i);
        }
    }
    applied = cheat_menu_state_sync(&v->state);
    v->state.requested_mask |= asked;
    for (i = 0; i < v->cheat_count; ++i) refresh_text(v, i);

    v->last_persist = CHEAT_MENU_PERSIST_NONE;
    if (!applied || !v->remember) return applied;
    /* Only explicit requests the engine accepted, each under its own
     * option id; the value is the engine's resulting state, so the latest
     * request per cheat wins. */
    for (i = 0; i < v->cheat_count; ++i) {
        const char *value;
        if (!(v->state.applied_mask & (1u << i)) || !v->bound[i]) continue;
        if (ts2_cheat_def_is_external(&v->defs[i])) {   /* session-only */
            session = 1;
            continue;
        }
        value = v->state.snapshots[i].enabled ? "true" : "false";
        ++v->persist_attempts;
        ++sent;
        if (!(v->persist && v->defs[i].id && v->persist(v->persist_ctx, v->defs[i].id, value)))
            failed = 1;
    }
    if (sent)
        v->last_persist = failed ? CHEAT_MENU_PERSIST_FAILED : CHEAT_MENU_PERSIST_QUEUED;
    else if (session)
        v->last_persist = CHEAT_MENU_PERSIST_SESSION;
    return applied;
}

const char *cheat_menu_view_status(cheat_menu_view *v)
{
    const char *what;
    const char *label = "Cheats";
    size_t i, target = 0, more = 0;
    int found = 0;
    ts2_cheat_state st = TS2_CHEAT_UNAVAILABLE;
    char extra[24] = "";

    if (!v) return "";
    for (i = 0; i < v->cheat_count; ++i) {
        if (!(v->state.requested_mask & (1u << i))) continue;
        if (!found) { target = i; found = 1; }
        else ++more;
    }
    if (v->cheat_count > 0u) {
        label = v->items[1 + target].label;
        if (v->bound[target]) st = v->state.snapshots[target].state;
    }
    switch (st) {
    case TS2_CHEAT_OFF: what = "off"; break;
    case TS2_CHEAT_ARMED: what = "armed, applies after resume"; break;
    case TS2_CHEAT_ACTIVE: what = "active"; break;
    case TS2_CHEAT_ERROR: what = "error"; break;
    case TS2_CHEAT_UNAVAILABLE:
    default: what = "unavailable"; break;
    }
    if (more) snprintf(extra, sizeof extra, " (+%u more)", (unsigned)more);
    snprintf(v->status_text, sizeof v->status_text, "%s: %s%s%s", label, what, extra,
             v->last_persist == CHEAT_MENU_PERSIST_QUEUED ? " - saving for next start" :
             v->last_persist == CHEAT_MENU_PERSIST_FAILED ? " - not saved" :
             v->last_persist == CHEAT_MENU_PERSIST_SESSION ? " - session only" : "");
    return v->status_text;
}
