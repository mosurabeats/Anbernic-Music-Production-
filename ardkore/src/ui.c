#include "ui.h"

#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "demo.h"
#include "font.h"
#include "machine.h"
#include "params.h"
#include "project.h"

/* LCD-style palette. */
#define COL_BG 0xFFA9C9D2u
#define COL_INK 0xFF101C22u
#define COL_DIM 0xFF5E7A84u
#define COL_SHADE 0xFF8FB2BDu
#define COL_HOT 0xFFD9481Cu

#define CW 6 /* character cell */
#define CH 8

#define REPEAT_DELAY 280
#define REPEAT_RATE 55

const char *const ui_button_names[BTN_COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y",
    "L1", "R1", "L2", "R2", "START", "SELECT", "MENU",
};

static const char *const page_names[PAGE_COUNT] = {"SEQ", "SMP", "FIL", "PRJ"};

enum { PRJ_BPM, PRJ_SWING, PRJ_MASTER, PRJ_REVERB, PRJ_RVB_LEVEL, PRJ_SAVE, PRJ_RELOAD, PRJ_NEW, PRJ_CLEAR, PRJ_COUNT };

/* ---- drawing ---------------------------------------------------------- */

static void fill(Ui *ui, int x, int y, int w, int h, uint32_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    for (int j = 0; j < h; j++) {
        uint32_t *row = ui->fb + (y + j) * SCREEN_W + x;
        for (int i = 0; i < w; i++) row[i] = c;
    }
}

static void pixel(Ui *ui, int x, int y, uint32_t c)
{
    if (x >= 0 && y >= 0 && x < SCREEN_W && y < SCREEN_H) ui->fb[y * SCREEN_W + x] = c;
}

static void vline(Ui *ui, int x, int y0, int y1, uint32_t c)
{
    for (int y = y0; y <= y1; y++) pixel(ui, x, y, c);
}

static void frame(Ui *ui, int x, int y, int w, int h, uint32_t c)
{
    fill(ui, x, y, w, 1, c);
    fill(ui, x, y + h - 1, w, 1, c);
    fill(ui, x, y, 1, h, c);
    fill(ui, x + w - 1, y, 1, h, c);
}

static int text(Ui *ui, int x, int y, const char *s, uint32_t fg)
{
    for (; *s; s++, x += CW) {
        const unsigned char *g = font_glyph((unsigned char)*s);
        for (int r = 0; r < FONT_H; r++)
            for (int c = 0; c < FONT_W; c++)
                if (g[r] & (1 << (4 - c))) pixel(ui, x + c, y + r, fg);
    }
    return x;
}

static int textf(Ui *ui, int x, int y, uint32_t fg, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return text(ui, x, y, buf, fg);
}

static void text_right(Ui *ui, int right, int y, const char *s, uint32_t fg)
{
    text(ui, right - (int)strlen(s) * CW, y, s, fg);
}

/* ---- helpers ---------------------------------------------------------- */

static void lock(Ui *ui) { if (ui->lock) ui->lock(ui->lock_ctx); }
static void unlock(Ui *ui) { if (ui->unlock) ui->unlock(ui->lock_ctx); }

void ui_status(Ui *ui, const char *msg)
{
    snprintf(ui->status, sizeof ui->status, "%s", msg);
    ui->status_ms = 2200;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static Track *cur_track(Ui *ui) { return &ui->eng->tr[ui->track]; }

static void audition(Ui *ui, int val)
{
    lock(ui);
    engine_trigger(ui->eng, ui->track, val, 64);
    unlock(ui);
}

/* ---- file browser ----------------------------------------------------- */

static int has_sample_ext(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && (!strcasecmp(name + n - 4, ".wav") || !strcasecmp(name + n - 4, ".vag"));
}

static int entry_cmp(const void *a, const void *b)
{
    const FileEntry *x = a, *y = b;
    if (x->kind != y->kind) return y->kind - x->kind; /* dirs, then samples */
    return strcasecmp(x->name, y->name);
}

static void scan_dir(Ui *ui)
{
    ui->nfiles = 0;
    FileEntry *f = ui->files;
    for (int i = 0; i < BUILTIN_COUNT; i++) {
        snprintf(f[ui->nfiles].name, sizeof f->name, "%s", builtin_ids[i]);
        f[ui->nfiles++].kind = 3;
    }
    if (strcmp(ui->browse_dir, "/") != 0) {
        snprintf(f[ui->nfiles].name, sizeof f->name, "..");
        f[ui->nfiles++].kind = 2;
    }

    DIR *d = opendir(ui->browse_dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) && ui->nfiles < UI_MAX_FILES) {
            if (de->d_name[0] == '.') continue;
            if (strlen(de->d_name) >= sizeof f->name) continue;
            char path[UI_PATH_LEN * 2];
            snprintf(path, sizeof path, "%s/%s", ui->browse_dir, de->d_name);
            struct stat st;
            if (stat(path, &st) != 0) continue;
            int kind;
            if (S_ISDIR(st.st_mode)) kind = 1;
            else if (has_sample_ext(de->d_name)) kind = 0;
            else continue;
            snprintf(f[ui->nfiles].name, sizeof f->name, "%s", de->d_name);
            f[ui->nfiles++].kind = kind;
        }
        closedir(d);
    }
    /* Built-ins and ".." stay first, in order; sort the folder's contents. */
    int fixed = BUILTIN_COUNT + (strcmp(ui->browse_dir, "/") != 0);
    qsort(ui->files + fixed, (size_t)(ui->nfiles - fixed), sizeof(FileEntry), entry_cmp);
    ui->file_cur = clampi(ui->file_cur, 0, ui->nfiles - 1);
}

static void enter_dir(Ui *ui, const char *name)
{
    if (!strcmp(name, "..")) {
        char *slash = strrchr(ui->browse_dir, '/');
        if (slash == ui->browse_dir) ui->browse_dir[1] = 0;
        else if (slash) *slash = 0;
    } else {
        size_t n = strlen(ui->browse_dir);
        if (n + strlen(name) + 2 >= UI_PATH_LEN) return;
        if (n > 1) strcat(ui->browse_dir, "/");
        strcat(ui->browse_dir, name);
    }
    ui->file_cur = 0;
    ui->file_top = 0;
    ui->preview_info[0] = 0;
    ui->preview_wait_ms = 0;
    scan_dir(ui);
}

#define PREVIEW_DELAY_MS 180

static int entry_path(const Ui *ui, const FileEntry *fe, char *out, size_t len)
{
    if (strlen(ui->browse_dir) + strlen(fe->name) + 2 > len) return -1;
    snprintf(out, len, "%s/%s", strcmp(ui->browse_dir, "/") ? ui->browse_dir : "", fe->name);
    return 0;
}

static void preview_entry(Ui *ui)
{
    ui->preview_wait_ms = 0;
    if (ui->file_cur < 0 || ui->file_cur >= ui->nfiles) return;
    const FileEntry *fe = &ui->files[ui->file_cur];
    char path[SRC_ID_LEN], name[SAMPLE_NAME_LEN], err[64];
    float *mono;
    int len, rate, loop;
    if (fe->kind != 0 || entry_path(ui, fe, path, sizeof path) != 0) return;
    if (demo_load_source(path, &mono, &len, &rate, &loop, name, sizeof name, err, sizeof err) != 0) {
        snprintf(ui->preview_info, sizeof ui->preview_info, "%s", err);
        return;
    }
    snprintf(ui->preview_info, sizeof ui->preview_info, "%dHZ  %.2fS%s", rate, (double)len / rate,
             loop >= 0 ? "  LOOPED" : "");
    lock(ui);
    engine_preview(ui->eng, mono, len, rate);
    unlock(ui);
}

static void stop_preview(Ui *ui)
{
    ui->preview_wait_ms = 0;
    lock(ui);
    engine_preview(ui->eng, NULL, 0, 0);
    unlock(ui);
}

static void load_entry(Ui *ui)
{
    const FileEntry *fe = &ui->files[ui->file_cur];
    if (fe->kind == 1 || fe->kind == 2) {
        enter_dir(ui, fe->name);
        return;
    }
    char id[SRC_ID_LEN];
    if (fe->kind == 3) snprintf(id, sizeof id, "%s", fe->name);
    else if (entry_path(ui, fe, id, sizeof id) != 0) {
        ui_status(ui, "PATH TOO LONG");
        return;
    }
    stop_preview(ui);

    float *mono;
    int len, rate, loop;
    char name[SAMPLE_NAME_LEN], err[64];
    if (demo_load_source(id, &mono, &len, &rate, &loop, name, sizeof name, err, sizeof err) != 0) {
        ui_status(ui, err);
        return;
    }
    lock(ui);
    engine_track_load(ui->eng, ui->track, mono, len, rate, loop, name, id);
    if (!ui->eng->playing) engine_trigger(ui->eng, ui->track, 0, 64);
    unlock(ui);
    ui->sel_slice[ui->track] = 0;
    char msg[64];
    snprintf(msg, sizeof msg, "T%d < %.40s", ui->track + 1, name);
    ui_status(ui, msg);
}

/* ---- editing ---------------------------------------------------------- */

static void set_param(Ui *ui, int idx, int value)
{
    Track *t = cur_track(ui);
    const ParamDef *d = &param_defs[idx];
    value = clampi(value, d->min, d->max);
    if (d->offset == offsetof(TrackParams, srate)) value = clampi(value, 0, machine_rate_count(t->p.machine) - 1);
    int *ptr = param_ptr(&t->p, idx);
    if (*ptr == value) return;
    lock(ui);
    *ptr = value;
    if (d->offset == offsetof(TrackParams, machine)) t->p.srate = machine_default_rate(value);
    if (d->flags & PF_REBAKE) engine_track_rebake(ui->eng, ui->track);
    else if (d->flags & PF_RESLICE) engine_track_reslice(ui->eng, ui->track);
    unlock(ui);
    int n = t->smp.nslices > 0 ? t->smp.nslices : 1;
    ui->sel_slice[ui->track] = clampi(ui->sel_slice[ui->track], 0, n - 1);
}

static void edit_step(Ui *ui, int dir_btn)
{
    Track *t = cur_track(ui);
    Step *st = &t->steps[ui->seq_step];
    if (!st->on) return;
    int big = t->p.mode == MODE_SAMPLE ? 12 : 4;
    int delta = dir_btn == BTN_LEFT ? -1 : dir_btn == BTN_RIGHT ? 1 : dir_btn == BTN_UP ? big : -big;
    int max = t->p.mode == MODE_SAMPLE ? 2 * SAMPLE_NOTE_CENTER
                                       : (t->smp.nslices > 0 ? t->smp.nslices - 1 : STEP_VAL_MAX);
    st->val = (uint8_t)clampi(st->val + delta, 0, max);
    if (t->p.mode == MODE_SAMPLE) ui->last_note[ui->track] = st->val;
    if (!ui->eng->playing) audition(ui, st->val);
}

static void project_action(Ui *ui, int item)
{
    Engine *e = ui->eng;
    char err[64];
    if (item == PRJ_SAVE) {
        ui_status(ui, project_save(e, ui->project_path) == 0 ? "PROJECT SAVED" : "SAVE FAILED");
        return;
    }
    if (item == PRJ_NEW || item == PRJ_CLEAR || item == PRJ_RELOAD) {
        if (ui->confirm_item != item) {
            ui->confirm_item = item;
            ui->confirm_ms = 2500;
            ui_status(ui, "PRESS A AGAIN TO CONFIRM");
            return;
        }
        ui->confirm_item = -1;
    }
    lock(ui);
    if (item == PRJ_NEW) {
        project_default(e);
        ui_status(ui, "DEMO PROJECT LOADED");
    } else if (item == PRJ_CLEAR) {
        engine_track_clear(e, ui->track);
        ui_status(ui, "TRACK CLEARED");
    } else if (item == PRJ_RELOAD) {
        int r = project_load(e, ui->project_path, err, sizeof err);
        ui_status(ui, r == 0 ? "PROJECT LOADED" : err);
    }
    unlock(ui);
}

static void project_edit(Ui *ui, int delta)
{
    Engine *e = ui->eng;
    lock(ui);
    if (ui->proj_cur == PRJ_BPM) e->bpm = clampi(e->bpm + delta, 40, 300);
    if (ui->proj_cur == PRJ_SWING) e->swing = clampi(e->swing + delta, 50, 75);
    if (ui->proj_cur == PRJ_MASTER) e->master = clampi(e->master + delta, 0, 100);
    if (ui->proj_cur == PRJ_REVERB && delta) e->rvb_preset = (e->rvb_preset + (delta > 0 ? 1 : PS1_RVB_COUNT - 1)) % PS1_RVB_COUNT;
    if (ui->proj_cur == PRJ_RVB_LEVEL) e->rvb_level = clampi(e->rvb_level + delta, 0, 100);
    unlock(ui);
}

/* ---- input ------------------------------------------------------------ */

static int is_dir(int b) { return b == BTN_UP || b == BTN_DOWN || b == BTN_LEFT || b == BTN_RIGHT; }

static void handle_dir(Ui *ui, int b)
{
    Track *t = cur_track(ui);
    int dx = b == BTN_LEFT ? -1 : b == BTN_RIGHT ? 1 : 0;
    int dy = b == BTN_UP ? -1 : b == BTN_DOWN ? 1 : 0;

    switch (ui->page) {
    case PAGE_SEQ:
        if (ui->held[BTN_A]) {
            ui->a_edited = 1;
            edit_step(ui, b);
        } else if (ui->held[BTN_Y]) {
            Step *st = &t->steps[ui->seq_step];
            if (st->on && dy) st->vel = (uint8_t)clampi(st->vel - dy * 8, 1, 64);
            if (st->on && dx) st->vel = (uint8_t)clampi(st->vel + dx, 1, 64);
        } else {
            ui->seq_step = (ui->seq_step + dx + NUM_STEPS) % NUM_STEPS;
            if (dy) ui->track = (ui->track + dy + NUM_TRACKS) % NUM_TRACKS;
        }
        break;
    case PAGE_SAMPLE:
        if (ui->held[BTN_A]) {
            const ParamDef *d = &param_defs[ui->prm];
            int delta = dx ? dx * d->step : -dy * d->coarse;
            ui->a_edited = 1;
            set_param(ui, ui->prm, param_get(&t->p, ui->prm) + delta);
        } else {
            int col = ui->prm % 4, row = ui->prm / 4;
            int rows = (param_count + 3) / 4;
            col = (col + dx + 4) % 4;
            row = (row + dy + rows) % rows;
            int idx = row * 4 + col;
            if (idx < param_count) ui->prm = idx;
        }
        break;
    case PAGE_FILES: {
        int before = ui->file_cur;
        if (dy) ui->file_cur = clampi(ui->file_cur + dy, 0, ui->nfiles - 1);
        if (dx) ui->file_cur = clampi(ui->file_cur + dx * 10, 0, ui->nfiles - 1);
        if (ui->file_cur != before) {
            ui->preview_info[0] = 0;
            if (ui->auto_preview) ui->preview_wait_ms = PREVIEW_DELAY_MS;
        }
        break;
    }
    case PAGE_PROJECT:
        if (ui->held[BTN_A]) {
            ui->a_edited = 1;
            project_edit(ui, dx ? dx : -dy * 10);
        } else if (dy) {
            ui->proj_cur = (ui->proj_cur + dy + PRJ_COUNT) % PRJ_COUNT;
        }
        break;
    }
}

static void a_pressed(Ui *ui)
{
    Track *t = cur_track(ui);
    ui->a_edited = 0;
    if (ui->page != PAGE_SEQ) return;
    Step *st = &t->steps[ui->seq_step];
    ui->a_step_was_on = st->on;
    if (!st->on) {
        if (t->p.mode == MODE_SAMPLE) st->val = (uint8_t)ui->last_note[ui->track];
        else st->val = (uint8_t)engine_default_val(ui->eng, ui->track, ui->seq_step);
        st->on = 1;
        if (!ui->eng->playing) audition(ui, st->val);
    }
}

static void a_released(Ui *ui)
{
    Track *t = cur_track(ui);
    if (ui->a_edited) return;
    switch (ui->page) {
    case PAGE_SEQ:
        if (ui->a_step_was_on) t->steps[ui->seq_step].on = 0;
        break;
    case PAGE_SAMPLE: {
        const ParamDef *d = &param_defs[ui->prm];
        if (d->labels) {
            int v = param_get(&t->p, ui->prm) + 1;
            set_param(ui, ui->prm, v > d->max ? d->min : v);
        }
        break;
    }
    case PAGE_FILES:
        load_entry(ui);
        break;
    case PAGE_PROJECT:
        if (ui->proj_cur >= PRJ_SAVE) project_action(ui, ui->proj_cur);
        break;
    }
}

void ui_button(Ui *ui, int b, int down)
{
    if (b < 0 || b >= BTN_COUNT) return;
    int was = ui->held[b];
    ui->held[b] = down;
    if (!down) {
        if (b == BTN_A && was) a_released(ui);
        return;
    }
    if (was) return;
    ui->repeat_ms[b] = -REPEAT_DELAY;

    Track *t = cur_track(ui);
    int nsl = t->smp.nslices > 0 ? t->smp.nslices : 1;

    if (is_dir(b)) {
        handle_dir(ui, b);
        return;
    }
    switch (b) {
    case BTN_A:
        a_pressed(ui);
        break;
    case BTN_B:
        if (ui->page == PAGE_FILES) {
            enter_dir(ui, "..");
        } else if (ui->page == PAGE_SEQ) {
            const Step *st = &t->steps[ui->seq_step];
            audition(ui, st->on ? st->val : engine_default_val(ui->eng, ui->track, ui->seq_step));
        } else {
            audition(ui, t->p.mode == MODE_SAMPLE ? SAMPLE_NOTE_CENTER : ui->sel_slice[ui->track]);
        }
        break;
    case BTN_X:
        if (ui->page == PAGE_FILES) {
            preview_entry(ui);
        } else if (ui->page == PAGE_SEQ) {
            Step *st = &t->steps[ui->seq_step];
            if (st->on) st->roll = (uint8_t)((st->roll + 1) % ROLL_COUNT);
        } else if (ui->page == PAGE_SAMPLE) {
            ui->sel_slice[ui->track] = (ui->sel_slice[ui->track] + nsl - 1) % nsl;
            audition(ui, ui->sel_slice[ui->track]);
        }
        break;
    case BTN_Y:
        if (ui->page == PAGE_FILES) {
            ui->auto_preview = !ui->auto_preview;
            ui_status(ui, ui->auto_preview ? "AUTO PREVIEW ON" : "AUTO PREVIEW OFF");
        } else if (ui->page == PAGE_SAMPLE) {
            ui->sel_slice[ui->track] = (ui->sel_slice[ui->track] + 1) % nsl;
            audition(ui, ui->sel_slice[ui->track]);
        }
        break;
    case BTN_L1:
    case BTN_R1:
        ui->page = (ui->page + (b == BTN_R1 ? 1 : PAGE_COUNT - 1)) % PAGE_COUNT;
        if (ui->page == PAGE_FILES) scan_dir(ui);
        break;
    case BTN_L2:
    case BTN_R2:
        ui->track = (ui->track + (b == BTN_R2 ? 1 : NUM_TRACKS - 1)) % NUM_TRACKS;
        break;
    case BTN_START:
        if (ui->held[BTN_SELECT]) {
            if (project_save(ui->eng, ui->project_path) != 0) fprintf(stderr, "ardkore: autosave failed\n");
            ui->quit = 1;
            break;
        }
        lock(ui);
        engine_play(ui->eng, !ui->eng->playing);
        unlock(ui);
        break;
    case BTN_MENU:
        ui->quit = 1;
        break;
    }
}

void ui_tick(Ui *ui, int dt_ms)
{
    for (int b = BTN_UP; b <= BTN_RIGHT; b++) {
        if (!ui->held[b]) continue;
        ui->repeat_ms[b] += dt_ms;
        while (ui->repeat_ms[b] >= REPEAT_RATE) {
            ui->repeat_ms[b] -= REPEAT_RATE;
            handle_dir(ui, b);
        }
    }
    if (ui->preview_wait_ms > 0 && (ui->preview_wait_ms -= dt_ms) <= 0) {
        if (ui->page == PAGE_FILES) preview_entry(ui);
        ui->preview_wait_ms = 0;
    }
    if (ui->status_ms > 0) ui->status_ms -= dt_ms;
    if (ui->confirm_ms > 0 && (ui->confirm_ms -= dt_ms) <= 0) ui->confirm_item = -1;
}

/* ---- pages ------------------------------------------------------------ */

static void build_wave(WaveCache *wc, const Sample *s, int width)
{
    wc->data = s->data;
    wc->len = s->len;
    wc->width = width;
    for (int c = 0; c < width; c++) {
        int a = (int)((long long)s->len * c / width);
        int b = (int)((long long)s->len * (c + 1) / width);
        if (b <= a) b = a + 1;
        float lo = 0.0f, hi = 0.0f;
        for (int i = a; i < b && i < s->len; i++) {
            if (s->data[i] < lo) lo = s->data[i];
            if (s->data[i] > hi) hi = s->data[i];
        }
        wc->lo[c] = (int8_t)(lo * 127.0f);
        wc->hi[c] = (int8_t)(hi * 127.0f);
    }
}

static void draw_wave(Ui *ui, int x, int y, int w, int h, int ti, int sel)
{
    Track *t = &ui->eng->tr[ti];
    const Sample *s = &t->smp;
    frame(ui, x - 1, y - 1, w + 2, h + 2, COL_INK);
    if (!s->data) {
        const char *msg = "NO SAMPLE: LOAD ONE ON THE FIL PAGE";
        text(ui, x + (w - (int)strlen(msg) * CW) / 2, y + h / 2 - 3, msg, COL_DIM);
        return;
    }
    WaveCache *wc = &ui->wave[ti];
    if (wc->data != s->data || wc->len != s->len || wc->width != w) build_wave(wc, s, w);

    if (sel >= 0 && sel < s->nslices) {
        int a = (int)((long long)s->slice[sel] * w / s->len);
        int b = (int)((long long)s->slice[sel + 1] * w / s->len);
        fill(ui, x + a, y, b - a > 0 ? b - a : 1, h, COL_SHADE);
    }
    int mid = y + h / 2;
    for (int c = 0; c < w; c++) {
        int top = mid - wc->hi[c] * (h / 2 - 1) / 127;
        int bot = mid - wc->lo[c] * (h / 2 - 1) / 127;
        vline(ui, x + c, top, bot, COL_INK);
    }
    for (int i = 1; i < s->nslices; i++) {
        int px = x + (int)((long long)s->slice[i] * w / s->len);
        for (int yy = y; yy < y + h; yy += 3) pixel(ui, px, yy, COL_INK);
    }
    const Voice *v = engine_newest_voice(ui->eng, ti);
    if (v->active) {
        int px = x + (int)(v->pos * w / s->len);
        vline(ui, px, y, y + h - 1, COL_HOT);
    }
}

static void step_label(const Track *t, const Step *st, char *buf)
{
    if (t->p.mode == MODE_SAMPLE) {
        int semis = st->val - SAMPLE_NOTE_CENTER;
        if (semis == 0) snprintf(buf, 3, " 0");
        else snprintf(buf, 3, "%c%X", semis < 0 ? '-' : '+', semis < 0 ? -semis : semis);
    } else {
        snprintf(buf, 3, "%02X", st->val);
    }
}

static void draw_seq(Ui *ui)
{
    Engine *e = ui->eng;
    int x0 = 4 + 3 * CW, y0 = 22;
    for (int s = 0; s < NUM_STEPS; s++) {
        int x = x0 + s * 3 * CW;
        uint32_t c = (s % 4 == 0) ? COL_INK : COL_DIM;
        if (e->playing && s == engine_track_step(e, ui->track)) {
            fill(ui, x - 1, y0 - 1, 2 * CW + 1, CH + 1, COL_HOT);
            c = COL_BG;
        }
        textf(ui, x + 3, y0, c, "%X", s);
    }
    for (int ti = 0; ti < NUM_TRACKS; ti++) {
        Track *t = &e->tr[ti];
        int y = y0 + 12 + ti * 14;
        uint32_t lc = t->smp.data ? COL_INK : COL_DIM;
        if (ti == ui->track) {
            fill(ui, 2, y - 2, 2 * CW + 2, CH + 3, COL_INK);
            lc = COL_BG;
        }
        textf(ui, 4, y, lc, "%d", ti + 1);
        for (int s = 0; s < NUM_STEPS; s++) {
            const Step *st = &t->steps[s];
            int x = x0 + s * 3 * CW;
            int cursor = ti == ui->track && s == ui->seq_step;
            int playing = e->playing && s == engine_track_step(e, ti) && st->on;
            uint32_t fg = st->on ? COL_INK : COL_DIM;
            if (cursor) {
                fill(ui, x - 1, y - 2, 2 * CW + 1, CH + 3, COL_INK);
                fg = COL_BG;
            } else if (playing) {
                fill(ui, x - 1, y - 2, 2 * CW + 1, CH + 3, COL_HOT);
                fg = COL_BG;
            } else if (s % 4 == 0) {
                fill(ui, x - 1, y - 2, 2 * CW + 1, CH + 3, COL_SHADE);
            }
            if (st->on) {
                char buf[4];
                step_label(t, st, buf);
                text(ui, x, y, buf, fg);
                if (st->roll) fill(ui, x, y + CH, 2 * CW - 1, 1, cursor ? COL_HOT : COL_INK);
            } else {
                char dots[3] = {FONT_DOT, FONT_DOT, 0};
                text(ui, x, y, dots, fg);
            }
        }
    }

    Track *t = cur_track(ui);
    const Step *st = &t->steps[ui->seq_step];
    int y = 146;
    if (st->on) {
        char buf[4];
        step_label(t, st, buf);
        textf(ui, 4, y, COL_INK, "STEP %X  %s %s  ROLL X%d  VEL %d", ui->seq_step,
              t->p.mode == MODE_SAMPLE ? "NOTE" : "SLICE", buf, roll_divs[st->roll], st->vel);
    } else {
        textf(ui, 4, y, COL_DIM, "STEP %X  OFF", ui->seq_step);
    }
    draw_wave(ui, 4, 160, SCREEN_W - 8, 50, ui->track, st->on && t->p.mode == MODE_SLICE ? st->val % (t->smp.nslices ? t->smp.nslices : 1) : -1);
}

static void draw_sample(Ui *ui)
{
    Track *t = cur_track(ui);
    int sel = t->p.mode == MODE_SLICE ? ui->sel_slice[ui->track] : -1;
    draw_wave(ui, 4, 22, SCREEN_W - 8, 56, ui->track, sel);

    int gy = 82, cw = 79, chh = 14;
    for (int i = 0; i < param_count; i++) {
        int col = i % 4, row = i / 4;
        int x = 2 + col * cw, y = gy + row * chh;
        uint32_t fg = COL_INK;
        if (i == ui->prm) {
            fill(ui, x, y, cw - 1, chh - 1, COL_INK);
            fg = COL_BG;
        } else {
            frame(ui, x, y, cw - 1, chh - 1, COL_DIM);
        }
        char val[16];
        param_format(&t->p, i, val, sizeof val);
        text(ui, x + 3, y + 3, param_defs[i].name, fg);
        text_right(ui, x + cw - 4, y + 3, val, fg);
    }

    int y = gy + ((param_count + 3) / 4) * chh + 4;
    const Sample *s = &t->smp;
    if (s->data) {
        if (t->p.machine == MACH_AMIGA)
            textf(ui, 4, y, COL_INK, "NOTE %s  %d SLICES  %.2fS", pt_note_names[t->p.srate], s->nslices, s->len / s->rate);
        else
            textf(ui, 4, y, COL_INK, "%d-BIT  %d SLICES  %.2fS", machines[t->p.machine].bits, s->nslices, s->len / s->rate);
        if (sel >= 0) {
            int a = s->slice[sel], b = s->slice[sel + 1];
            textf(ui, 4, y + 10, COL_DIM, "SLICE %02d  %.3fS", sel, (b - a) / s->rate);
        }
    }
}

static void draw_files(Ui *ui)
{
    char dir[60];
    size_t n = strlen(ui->browse_dir);
    snprintf(dir, sizeof dir, "%s", n > 50 ? ui->browse_dir + n - 50 : ui->browse_dir);
    textf(ui, 4, 22, COL_DIM, "%s", dir);
    int rows = 20, y0 = 34;
    if (ui->file_cur < ui->file_top) ui->file_top = ui->file_cur;
    if (ui->file_cur >= ui->file_top + rows) ui->file_top = ui->file_cur - rows + 1;
    for (int i = 0; i < rows && ui->file_top + i < ui->nfiles; i++) {
        const FileEntry *fe = &ui->files[ui->file_top + i];
        int y = y0 + i * 9;
        uint32_t fg = COL_INK;
        if (ui->file_top + i == ui->file_cur) {
            fill(ui, 2, y - 1, SCREEN_W - 4, CH + 1, COL_INK);
            fg = COL_BG;
        }
        char name[52];
        snprintf(name, sizeof name, "%s", fe->name);
        switch (fe->kind) {
        case 3: {
            char kind[16];
            const char *colon = strchr(name, ':');
            snprintf(kind, sizeof kind, "%.*s", colon ? (int)(colon - name) : 0, name);
            for (char *c = kind; *c; c++) *c = (char)(*c - 32 * (*c >= 'a' && *c <= 'z'));
            textf(ui, 4, y, fg, "[%s] %s", kind, colon ? colon + 1 : name);
            break;
        }
        case 2: text(ui, 4, y, "../", fg); break;
        case 1: textf(ui, 4, y, fg, "%s/", name); break;
        default: text(ui, 4, y, name, fg); break;
        }
    }
    if (ui->preview_info[0]) text(ui, 4, 218, ui->preview_info, COL_DIM);
    if (ui->nfiles <= BUILTIN_COUNT + 1)
        text(ui, 4, y0 + (BUILTIN_COUNT + 2) * 9, "(PUT .WAV/.VAG FILES IN THE SAMPLES FOLDER)", COL_DIM);
}

static void draw_project(Ui *ui)
{
    Engine *e = ui->eng;
    const char *labels[PRJ_COUNT] = {"BPM", "SWING", "MASTER", "PS1 REVERB", "REVERB LEVEL", "SAVE PROJECT", "RELOAD PROJECT",
                                     "NEW DEMO PROJECT", "CLEAR TRACK"};
    for (int i = 0; i < PRJ_COUNT; i++) {
        int y = 22 + i * 10;
        uint32_t fg = COL_INK;
        if (i == ui->proj_cur) {
            fill(ui, 2, y - 1, 160, CH + 1, COL_INK);
            fg = COL_BG;
        }
        text(ui, 6, y, labels[i], fg);
        char val[16] = "";
        if (i == PRJ_BPM) snprintf(val, sizeof val, "%d", e->bpm);
        if (i == PRJ_SWING) snprintf(val, sizeof val, "%d%%", e->swing);
        if (i == PRJ_MASTER) snprintf(val, sizeof val, "%d", e->master);
        if (i == PRJ_REVERB) snprintf(val, sizeof val, "%s", ps1_reverb_names[e->rvb_preset]);
        if (i == PRJ_RVB_LEVEL) snprintf(val, sizeof val, "%d", e->rvb_level);
        if (i == PRJ_CLEAR) snprintf(val, sizeof val, "T%d", ui->track + 1);
        text_right(ui, 158, y, val, fg);
    }
    static const char *const help[] = {
        "L1/R1     PAGE        L2/R2   TRACK",
        "START     PLAY/STOP   SEL+START SAVE+QUIT",
        "A         TOGGLE/PICK A+DPAD  EDIT VALUE",
        "B         HEAR        X       ROLL (SEQ)",
        "X/Y       PREV/NEXT SLICE (SMP PAGE)",
        "Y+DPAD    STEP VELOCITY (SEQ PAGE)",
    };
    for (int i = 0; i < 6; i++) text(ui, 4, 120 + i * 9, help[i], COL_DIM);
    for (int i = 0; i < 3; i++) text(ui, 4, 182 + i * 10, ui->info[i], COL_INK);
}

static const char *page_hint(int page)
{
    switch (page) {
    case PAGE_SEQ: return "A:STEP  A+PAD:VAL  X:ROLL  B:HEAR  START:PLAY";
    case PAGE_SAMPLE: return "A+PAD:EDIT  X/Y:SLICE  B:HEAR  START:PLAY";
    case PAGE_FILES: return "A:LOAD  X:HEAR  Y:AUTO  B:UP  L2/R2:TRACK";
    default: return "A:SELECT  A+PAD:EDIT  L1/R1:PAGE";
    }
}

void ui_draw(Ui *ui)
{
    Engine *e = ui->eng;
    fill(ui, 0, 0, SCREEN_W, SCREEN_H, COL_BG);

    /* Header bar: name, page tabs, track/tempo/transport. */
    fill(ui, 0, 0, SCREEN_W, 11, COL_INK);
    text(ui, 4, 2, "ARDKORE", COL_BG);
    for (int p = 0; p < PAGE_COUNT; p++) {
        int x = 60 + p * 24;
        if (p == ui->page) {
            fill(ui, x - 2, 1, 3 * CW + 3, 9, COL_BG);
            text(ui, x, 2, page_names[p], COL_INK);
        } else {
            text(ui, x, 2, page_names[p], COL_DIM);
        }
    }
    char play[2] = {e->playing ? FONT_PLAY : FONT_STOP, 0};
    textf(ui, 166, 2, COL_BG, "T%d", ui->track + 1);
    textf(ui, 190, 2, COL_BG, "%dBPM", e->bpm);
    text(ui, 236, 2, play, e->playing ? COL_HOT : COL_BG);
    /* Output level meter. */
    int meter = (int)(e->peak * 60.0f);
    frame(ui, 252, 3, 64, 5, COL_DIM);
    fill(ui, 254, 4, meter > 60 ? 60 : meter, 3, e->peak > 0.95f ? COL_HOT : COL_BG);

    Track *t = cur_track(ui);
    textf(ui, 4, 13, COL_INK, "T%d %.24s", ui->track + 1, t->smp.data ? t->smp.name : "(EMPTY)");
    text_right(ui, SCREEN_W - 4, 13, machines[t->p.machine].name, COL_INK);

    switch (ui->page) {
    case PAGE_SEQ: draw_seq(ui); break;
    case PAGE_SAMPLE: draw_sample(ui); break;
    case PAGE_FILES: draw_files(ui); break;
    default: draw_project(ui); break;
    }

    fill(ui, 0, SCREEN_H - 11, SCREEN_W, 11, COL_INK);
    if (ui->status_ms > 0) text(ui, 4, SCREEN_H - 9, ui->status, COL_HOT);
    else text(ui, 4, SCREEN_H - 9, page_hint(ui->page), COL_SHADE);
}

/* ---- setup ------------------------------------------------------------ */

void ui_init(Ui *ui, Engine *eng, const char *samples_dir, const char *project_path)
{
    memset(ui, 0, sizeof(*ui));
    ui->eng = eng;
    ui->confirm_item = -1;
    ui->auto_preview = 1;
    for (int t = 0; t < NUM_TRACKS; t++) ui->last_note[t] = SAMPLE_NOTE_CENTER;
    char resolved[PATH_MAX];
    snprintf(ui->browse_dir, sizeof ui->browse_dir, "%s", realpath(samples_dir, resolved) ? resolved : samples_dir);
    size_t n = strlen(ui->browse_dir);
    while (n > 1 && ui->browse_dir[n - 1] == '/') ui->browse_dir[--n] = 0;
    snprintf(ui->project_path, sizeof ui->project_path, "%s", project_path);
    scan_dir(ui);
}

void ui_set_lock(Ui *ui, void (*lockfn)(void *), void (*unlockfn)(void *), void *ctx)
{
    ui->lock = lockfn;
    ui->unlock = unlockfn;
    ui->lock_ctx = ctx;
}

int ui_save_ppm(const Ui *ui, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint32_t c = ui->fb[i];
        unsigned char rgb[3] = {(unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c};
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f) == 0 ? 0 : -1;
}
