#include "touch_gesture.h"

#include <string.h>

void tg_reset(touch_gesture_t *g) {
    memset(g, 0, sizeof(*g));
}

/* Returns the index of the tracked finger with this id, or -1. */
static int slot_of(const touch_gesture_t *g, int id) {
    if (id == 0)
        return -1;
    for (int i = 0; i < TG_FINGERS; i++)
        if (g->id[i] == id)
            return i;
    return -1;
}

static int first_free(const touch_gesture_t *g) {
    for (int i = 0; i < TG_FINGERS; i++)
        if (g->id[i] == 0)
            return i;
    return -1;
}

static void flush_move(touch_gesture_t *g, tg_action_t *out, int max_out,
                      int *n, int *dx, int *dy) {
    int ix = (int)g->acc_x, iy = (int)g->acc_y;
    if (ix == 0 && iy == 0)
        return;
    if (ix > TG_POINTER_CLAMP) ix = TG_POINTER_CLAMP;
    if (ix < -TG_POINTER_CLAMP) ix = -TG_POINTER_CLAMP;
    if (iy > TG_POINTER_CLAMP) iy = TG_POINTER_CLAMP;
    if (iy < -TG_POINTER_CLAMP) iy = -TG_POINTER_CLAMP;
    g->acc_x -= ix;
    g->acc_y -= iy;
    if (*n < max_out) {
        out[*n] = TG_ACT_MOVE;
        dx[*n] = ix;
        dy[*n] = iy;
        (*n)++;
    }
}

static void button(touch_gesture_t *g, int right, int down,
                   tg_action_t *out, int max_out, int *n) {
    int *cur = right ? &g->right : &g->left;
    if (*cur == down)
        return;
    *cur = down;
    if (*n < max_out) {
        out[*n] = right ? (down ? TG_ACT_R_DOWN : TG_ACT_R_UP)
                        : (down ? TG_ACT_L_DOWN : TG_ACT_L_UP);
        (*n)++;
    }
}

/* Flush pending scroll travel into wheel actions. Two-finger drags average
 * the two contacts so a slightly loose second finger does not jitter the
 * notch rate; vertical is emitted first, then horizontal. */
static void flush_scroll(touch_gesture_t *g, tg_action_t *out, int max_out,
                         int *n, int *dx, int *dy) {
    int vy = (int)(g->acc_sy / TG_SCROLL_PX_PER_TICK * TG_WHEEL_UNIT);
    int vx = (int)(g->acc_sx / TG_SCROLL_PX_PER_TICK * TG_WHEEL_UNIT);
    if (vy) {
        g->acc_sy -= (float)vy * TG_SCROLL_PX_PER_TICK / TG_WHEEL_UNIT;
        if (*n < max_out) {
            out[*n] = TG_ACT_SCROLL;
            dx[*n] = 0;
            dy[*n] = vy;
            (*n)++;
        }
    }
    if (vx) {
        g->acc_sx -= (float)vx * TG_SCROLL_PX_PER_TICK / TG_WHEEL_UNIT;
        if (*n < max_out) {
            out[*n] = TG_ACT_SCROLL;
            dx[*n] = vx;
            dy[*n] = 0;
            (*n)++;
        }
    }
}

/* Release BOTH tracked buttons at once (mode transitions never leave a
 * stuck press on the host). */
static void release_buttons(touch_gesture_t *g, tg_action_t *out,
                            int max_out, int *n) {
    if (g->left)  button(g, 0, 0, out, max_out, n);
    if (g->right) button(g, 1, 0, out, max_out, n);
}

int tg_feed(touch_gesture_t *g, const touch_report_t *r,
            tg_action_t *out, int max_out, int *dx, int *dy) {
    int n = 0;

    /* --- 1. latch the touch slots ------------------------------------ */
    int seen[TG_FINGERS];
    int count = r->fingers < TG_FINGERS ? r->fingers : TG_FINGERS;

    for (int i = 0; i < TG_FINGERS; i++)
        seen[i] = 0;

    /* Update tracked fingers; note which report entries were consumed. */
    for (int i = 0; i < count; i++) {
        int id = r->id[i];
        if (id == 0)
            continue;
        int slot = slot_of(g, id);
        if (slot < 0)
            continue;
        seen[i] = 1;
        int dxp = r->x[i] - g->x[slot];
        int dyp = r->y[i] - g->y[slot];
        g->x[slot] = r->x[i];
        g->y[slot] = r->y[i];
        if (dxp == 0 && dyp == 0)
            continue;

        if (g->mode == TG_POINTER) {
            /* Single-finger pointer: only slot 0 is ever tracked in this
             * mode, so this delta is cursor movement. */
            g->acc_x += (float)dxp;
            g->acc_y += (float)dyp;
        } else if (g->mode == TG_PENDING || g->mode == TG_SCROLL) {
            g->acc_sx += (float)dxp * 0.5f;  /* average of the pair */
            g->acc_sy += (float)dyp * 0.5f;
            if (g->mode == TG_PENDING) {
                if (dxp < 0) dxp = -dxp;
                if (dyp < 0) dyp = -dyp;
                g->travel += dxp > dyp ? dxp : dyp;
            }
        }
    }

    /* New contacts anchor without jumping. */
    for (int i = 0; i < count; i++) {
        if (seen[i] || r->id[i] == 0)
            continue;
        int slot = first_free(g);
        if (slot < 0)
            continue;
        g->id[slot] = r->id[i];
        g->x[slot] = r->x[i];
        g->y[slot] = r->y[i];
    }

    /* Drop contacts that disappeared from the report. */
    int tracked = 0;
    for (int i = 0; i < TG_FINGERS; i++) {
        int live = 0;
        if (g->id[i] != 0) {
            for (int k = 0; k < count; k++)
                if (r->id[k] == g->id[i])
                    live = 1;
            if (!live)
                g->id[i] = 0;
            else
                tracked++;
        }
    }

    /* --- 2. mode transitions ----------------------------------------- */
    if (tracked == 0) {
        release_buttons(g, out, max_out, &n);
        g->mode = TG_NONE;
        g->travel = 0;
        g->acc_sx = g->acc_sy = 0;
    } else if (tracked == 1) {
        if (g->mode == TG_PENDING || g->mode == TG_SCROLL) {
            /* Scroll lost a finger: flush what accumulated, then fall
             * back to pointer mode anchored at the survivor (no jump: the
             * survivor keeps its tracked position). */
            flush_scroll(g, out, max_out, &n, dx, dy);
            release_buttons(g, out, max_out, &n);
            g->acc_x = g->acc_y = 0;
        }
        if (g->mode == TG_NONE) {
            g->acc_x = g->acc_y = 0;
        }
        g->mode = TG_POINTER;
        g->travel = 0;
    } else {
        /* Two tracked fingers. */
        if (g->mode == TG_POINTER || g->mode == TG_NONE) {
            /* The pair just formed. Flush whatever pointer motion the
             * first finger accumulated up to this report (it was real),
             * release buttons, then start pending: cursor accumulation is
             * dropped so the pair cannot creep the pointer. */
            if (g->mode == TG_POINTER)
                flush_move(g, out, max_out, &n, dx, dy);
            release_buttons(g, out, max_out, &n);
            g->mode = TG_PENDING;
            g->travel = 0;
            g->acc_sx = g->acc_sy = 0;
            g->acc_x = g->acc_y = 0;
        }
        if (g->mode == TG_PENDING && g->travel >= TG_SCROLL_LOCK_PX) {
            g->mode = TG_SCROLL;
            release_buttons(g, out, max_out, &n); /* click guard while scrolling */
            /* The movement that crossed the threshold starts the scroll. */
        }
    }

    /* --- 3. emit ------------------------------------------------------ */
    if (g->mode == TG_POINTER)
        flush_move(g, out, max_out, &n, dx, dy);
    if (g->mode == TG_SCROLL)
        flush_scroll(g, out, max_out, &n, dx, dy);

    /* --- 4. physical click ------------------------------------------- */
    if (g->mode == TG_POINTER) {
        button(g, 0, r->click, out, max_out, &n);
    } else if (g->mode == TG_PENDING) {
        /* Two fingers resting, no drag: classic right-click. */
        button(g, 1, r->click, out, max_out, &n);
    } else if (g->mode == TG_SCROLL) {
        /* A click during a scroll is noise (the pad gets pressed while
         * flicking); buttons are force-released instead. */
        if (r->click)
            release_buttons(g, out, max_out, &n);
    } else { /* TG_NONE */
        if (g->left)  button(g, 0, 0, out, max_out, &n);
        if (g->right) button(g, 1, 0, out, max_out, &n);
    }

    return n;
}
