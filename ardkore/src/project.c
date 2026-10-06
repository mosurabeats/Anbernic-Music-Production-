#include "project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demo.h"
#include "machine.h"
#include "params.h"
#include "ps1.h"

#define PROJECT_MAGIC "ARDKORE 1"

int project_save(const Engine *e, const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    fprintf(f, "%s\nbpm %d\nswing %d\nmaster %d\nreverb %d\nrvblevel %d\n", PROJECT_MAGIC, e->bpm, e->swing,
            e->master, e->rvb_preset, e->rvb_level);
    for (int t = 0; t < NUM_TRACKS; t++) {
        const Track *tr = &e->tr[t];
        fprintf(f, "track %d\n", t);
        if (tr->src_id[0]) fprintf(f, "src %s\n", tr->src_id);
        for (int i = 0; i < param_count; i++)
            fprintf(f, "p %s %d\n", param_defs[i].key, param_get(&tr->p, i));
        for (int s = 0; s < NUM_STEPS; s++) {
            const Step *st = &tr->steps[s];
            fprintf(f, "s %d %d %d %d %d\n", s, st->on, st->val, st->roll, st->vel);
        }
    }
    int ok = ferror(f) == 0;
    if (fclose(f) != 0) ok = 0;
    if (!ok || rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int project_load(Engine *e, const char *path, char *err, int errlen)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(err, (size_t)errlen, "NO PROJECT FILE");
        return -1;
    }
    char line[1024];
    if (!fgets(line, sizeof line, f) || strncmp(line, PROJECT_MAGIC, strlen(PROJECT_MAGIC)) != 0) {
        fclose(f);
        snprintf(err, (size_t)errlen, "NOT AN ARDKORE PROJECT");
        return -1;
    }

    engine_play(e, 0);
    for (int t = 0; t < NUM_TRACKS; t++) engine_track_clear(e, t);
    e->rvb_preset = PS1_RVB_OFF; /* projects saved before the reverb existed */
    e->rvb_level = 60;

    char srcs[NUM_TRACKS][SRC_ID_LEN] = {{0}};
    int cur = -1;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char key[32];
        int a, b, c, d, g;
        if (sscanf(line, "bpm %d", &a) == 1) e->bpm = clampi(a, 40, 300);
        else if (sscanf(line, "swing %d", &a) == 1) e->swing = clampi(a, 50, 75);
        else if (sscanf(line, "master %d", &a) == 1) e->master = clampi(a, 0, 100);
        else if (sscanf(line, "reverb %d", &a) == 1) e->rvb_preset = clampi(a, 0, PS1_RVB_COUNT - 1);
        else if (sscanf(line, "rvblevel %d", &a) == 1) e->rvb_level = clampi(a, 0, 100);
        else if (sscanf(line, "track %d", &a) == 1) cur = (a >= 0 && a < NUM_TRACKS) ? a : -1;
        else if (cur < 0) continue;
        else if (!strncmp(line, "src ", 4)) snprintf(srcs[cur], SRC_ID_LEN, "%s", line + 4);
        else if (sscanf(line, "p %31s %d", key, &a) == 2) {
            int i = param_find(key);
            if (i >= 0) *param_ptr(&e->tr[cur].p, i) = clampi(a, param_defs[i].min, param_defs[i].max);
        } else if (sscanf(line, "s %d %d %d %d %d", &a, &b, &c, &d, &g) == 5 && a >= 0 && a < NUM_STEPS) {
            Step *st = &e->tr[cur].steps[a];
            st->on = b != 0;
            st->val = (uint8_t)clampi(c, 0, STEP_VAL_MAX);
            st->roll = (uint8_t)clampi(d, 0, ROLL_COUNT - 1);
            st->vel = (uint8_t)clampi(g, 1, 64);
        }
    }
    fclose(f);

    int missing = 0;
    err[0] = 0;
    for (int t = 0; t < NUM_TRACKS; t++) {
        if (!srcs[t][0]) continue;
        float *mono;
        int len, rate, loop;
        char name[SAMPLE_NAME_LEN], why[64];
        if (demo_load_source(srcs[t], &mono, &len, &rate, &loop, name, sizeof name, why, sizeof why) == 0) {
            engine_track_load(e, t, mono, len, rate, loop, name, srcs[t]);
        } else {
            /* Keep the reference so saving again doesn't lose it. */
            snprintf(e->tr[t].src_id, SRC_ID_LEN, "%s", srcs[t]);
            if (!missing) snprintf(err, (size_t)errlen, "T%d SAMPLE MISSING", t + 1);
            missing = 1;
        }
    }
    return missing;
}

static void load_builtin(Engine *e, int t, const char *id)
{
    float *mono;
    int len, rate, loop;
    char name[SAMPLE_NAME_LEN], why[64];
    if (demo_load_source(id, &mono, &len, &rate, &loop, name, sizeof name, why, sizeof why) == 0)
        engine_track_load(e, t, mono, len, rate, loop, name, id);
}

static void set_steps(Track *t, const int *steps, const int *vals, int n)
{
    for (int i = 0; i < n; i++) {
        t->steps[steps[i]].on = 1;
        t->steps[steps[i]].val = (uint8_t)vals[i];
    }
}

void project_default(Engine *e)
{
    engine_play(e, 0);
    for (int t = 0; t < NUM_TRACKS; t++) engine_track_clear(e, t);
    e->bpm = DEMO_BREAK_BPM;
    e->swing = 50;
    e->rvb_preset = PS1_RVB_HALL;
    e->rvb_level = 55;

    /* Track 1: the break through the Amiga, sliced 16 ways and re-arranged a little. */
    load_builtin(e, 0, "demo:break");
    static const int order[NUM_STEPS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 4, 13, 12, 12};
    for (int s = 0; s < NUM_STEPS; s++) {
        e->tr[0].steps[s].on = 1;
        e->tr[0].steps[s].val = (uint8_t)order[s];
    }
    e->tr[0].steps[15].roll = 3; /* x4 roll into the loop point */
    e->tr[0].p.rvb = 6;

    /* Track 2: PS1 choir pad, minor 9ths moving C -> Bb -> Ab -> Bb, one per bar. */
    Track *pad = &e->tr[1];
    pad->p.machine = MACH_PS1;
    pad->p.srate = PS1_DEFAULT_RATE;
    pad->p.mode = MODE_SAMPLE;
    pad->p.chord = CHORD_MIN9;
    pad->p.loop = 1;
    pad->p.speed = 2; /* 1/4: one step per beat, 16 steps = 4 bars */
    pad->p.attack = 350;
    pad->p.release = 1400;
    pad->p.vol = 46;
    pad->p.vib = 6;
    pad->p.rvb = 56;
    load_builtin(e, 1, "synth:choir");
    static const int pad_steps[] = {0, 4, 8, 12}, pad_vals[] = {12, 10, 8, 10};
    set_steps(pad, pad_steps, pad_vals, 4);

    /* Track 3: sub bass following the roots, also at quarter speed. */
    Track *sub = &e->tr[2];
    sub->p.machine = MACH_PS1;
    sub->p.srate = PS1_DEFAULT_RATE;
    sub->p.mode = MODE_SAMPLE;
    sub->p.loop = 1;
    sub->p.speed = 2;
    sub->p.hold = 2;
    sub->p.attack = 5;
    sub->p.release = 120;
    sub->p.vol = 50;
    load_builtin(e, 2, "synth:sub");
    static const int sub_steps[] = {0, 3, 4, 7, 8, 11, 12, 14}, sub_vals[] = {12, 12, 10, 10, 8, 8, 10, 10};
    set_steps(sub, sub_steps, sub_vals, 8);
}
