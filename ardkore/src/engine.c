#include "engine.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "params.h"

const int roll_divs[ROLL_COUNT] = {1, 2, 3, 4, 6, 8};

void engine_track_defaults(Track *t)
{
    for (int i = 0; i < param_count; i++) *param_ptr(&t->p, i) = param_defs[i].def;
    for (int i = 0; i < NUM_STEPS; i++) {
        t->steps[i].on = 0;
        t->steps[i].val = (uint8_t)i;
        t->steps[i].roll = 0;
        t->steps[i].vel = 64;
    }
    t->filter_lpf = -1;
}

void engine_init(Engine *e, int sr)
{
    memset(e, 0, sizeof(*e));
    e->sr = sr;
    e->bpm = 170;
    e->swing = 50;
    e->master = 80;
    for (int t = 0; t < NUM_TRACKS; t++) engine_track_defaults(&e->tr[t]);
}

void engine_free(Engine *e)
{
    for (int t = 0; t < NUM_TRACKS; t++) sample_free(&e->tr[t].smp);
}

static void stop_voices(Track *t)
{
    t->v.active = 0;
    t->tail.active = 0;
}

void engine_track_reslice(Engine *e, int track)
{
    Track *t = &e->tr[track];
    if (!t->smp.data) return;
    if (t->p.chop == CHOP_AUTO)
        sample_slice_auto(&t->smp, t->p.thresh, t->p.zc);
    else
        sample_slice_equal(&t->smp, t->p.slices, t->p.zc);
    stop_voices(t);
}

void engine_track_rebake(Engine *e, int track)
{
    Track *t = &e->tr[track];
    if (!t->smp.src) return;
    const Machine *m = &machines[t->p.machine];
    sample_bake(&t->smp, machine_bake_rate(t->p.machine, t->p.srate), m->bits, m->nonlinear, m->prefilter);
    engine_track_reslice(e, track);
}

void engine_track_load(Engine *e, int track, float *mono, int len, int rate,
                       const char *name, const char *src_id)
{
    Track *t = &e->tr[track];
    stop_voices(t);
    sample_set_source(&t->smp, mono, len, rate, name);
    strncpy(t->src_id, src_id ? src_id : "", SRC_ID_LEN - 1);
    t->src_id[SRC_ID_LEN - 1] = 0;
    engine_track_rebake(e, track);
}

void engine_track_clear(Engine *e, int track)
{
    Track *t = &e->tr[track];
    stop_voices(t);
    sample_free(&t->smp);
    t->src_id[0] = 0;
    engine_track_defaults(t);
}

int engine_default_val(const Engine *e, int track, int step)
{
    const Track *t = &e->tr[track];
    if (t->p.mode == MODE_SAMPLE) return SAMPLE_NOTE_CENTER;
    int n = t->smp.nslices > 0 ? t->smp.nslices : 1;
    return step % n;
}

void engine_trigger(Engine *e, int track, int val, int vel)
{
    Track *t = &e->tr[track];
    Sample *s = &t->smp;
    const TrackParams *p = &t->p;
    if (!s->data || s->len < 2) return;

    double semis = p->pitch + p->fine / 8.0;
    int lo, hi;
    if (p->mode == MODE_SLICE) {
        int idx = val % s->nslices;
        lo = s->slice[idx];
        hi = s->slice[idx + 1];
        t->last_slice = idx;
    } else {
        lo = 0;
        hi = s->len;
        semis += val - SAMPLE_NOTE_CENTER;
    }
    if (p->trig == TRIG_THRU) {
        if (p->rev) lo = 0;
        else hi = s->len;
    }
    if (hi - lo < 2) return;

    Voice *v = &t->v;
    if (v->active) {
        t->tail = *v;
        t->tail_fade = 1.0f;
    }
    v->dir = p->rev ? -1 : 1;
    v->lo = lo;
    v->hi = hi;
    v->pos = p->rev ? hi - 1 : lo;
    v->anchor = v->pos;
    v->grain = 0.0;
    v->inc = machine_play_rate(p->machine, p->srate, semis) / e->sr;
    v->age = 0;
    v->attack_len = p->attack * e->sr / 1000;
    v->release_len = p->release * e->sr / 1000;
    v->gain = (vel / 64.0f) * (p->vol / 64.0f);
    v->active = 1;
}

static float voice_tick(Track *t, Voice *v)
{
    if (!v->active) return 0.0f;
    const Sample *s = &t->smp;
    const TrackParams *p = &t->p;

    int i = (int)v->pos;
    if (i < v->lo) i = v->lo;
    if (i >= v->hi) i = v->hi - 1;
    float x = s->data[i];
    if (machines[p->machine].interpolate) {
        int j = i + 1 < s->len ? i + 1 : i;
        float frac = (float)(v->pos - i);
        if (frac < 0.0f) frac = 0.0f;
        x += (s->data[j] - x) * frac;
    }

    /* Envelope: linear attack, release fades into the end of the region. */
    float env = 1.0f;
    if (v->attack_len > 0 && v->age < v->attack_len) env = (float)v->age / v->attack_len;
    double ratio = p->stretch / 100.0;
    double head = p->stretch == 100 ? v->pos : v->anchor;
    double speed = v->inc / (p->stretch == 100 ? 1.0 : ratio);
    double remain = (v->dir > 0 ? v->hi - head : head - v->lo) / speed;
    if (v->release_len > 0 && remain < v->release_len) env *= (float)(remain / v->release_len);
    float out = x * env * v->gain;

    /* Advance. Cyclic stretch (S950 style): the read head plays grains at the
     * pitched rate and jumps back to an anchor that moves at the stretched rate. */
    v->age++;
    v->pos += v->inc * v->dir;
    if (p->stretch != 100) {
        v->anchor += v->inc * v->dir / ratio;
        v->grain += v->inc;
        double cycle = p->cycle * s->rate / 1000.0;
        if (v->grain >= cycle) {
            v->pos = v->anchor;
            v->grain = 0.0;
        }
        head = v->anchor;
    } else {
        head = v->pos;
    }
    if (v->dir > 0 ? head >= v->hi : head < v->lo) {
        v->active = 0;
    } else if (v->dir > 0 ? v->pos >= v->hi : v->pos < v->lo) {
        v->pos = v->anchor;
        v->grain = 0.0;
    }
    return out;
}

static float lpf_cutoff(int lpf) { return 80.0f * powf(250.0f, lpf / 127.0f); }

static void update_filters(Engine *e, int ti)
{
    Track *t = &e->tr[ti];
    const TrackParams *p = &t->p;
    if (t->filter_lpf == p->lpf && t->filter_machine == p->machine && t->filter_afilt == p->afilt) return;

    float sr = (float)e->sr;
    if (p->lpf < 127) {
        float fc = lpf_cutoff(p->lpf);
        svf_set(&t->lp[0], fc, 0.8f, sr);
        svf_set(&t->lp[1], fc, 0.8f, sr);
    }

    /* Output voicing. SP-1200 channel layout: 1-2 four-pole (via the LPF),
     * 3-6 fixed low-pass, 7-8 unfiltered. */
    t->fixed_on = 1;
    switch (p->machine) {
    case MACH_AMIGA:
        onepole_set(&t->a500, 4420.97f, sr);
        t->fixed_on = p->afilt == AFILT_LED;
        biquad_lowpass(&t->fixed, 3090.5f, 0.660f, sr);
        break;
    case MACH_SP1200:
        t->fixed_on = ti >= 2 && ti <= 5;
        biquad_lowpass(&t->fixed, 9000.0f, 0.707f, sr);
        break;
    case MACH_MPC60:
        biquad_lowpass(&t->fixed, 15000.0f, 0.707f, sr);
        break;
    default:
        biquad_lowpass(&t->fixed, 18000.0f, 0.707f, sr);
        break;
    }
    t->filter_lpf = p->lpf;
    t->filter_machine = p->machine;
    t->filter_afilt = p->afilt;
}

static double step_length(const Engine *e, int step)
{
    double base = e->sr * 60.0 / (e->bpm * 4.0);
    double first = 2.0 * base * e->swing / 100.0;
    return (step & 1) ? 2.0 * base - first : first;
}

static void fire_step(Engine *e)
{
    for (int t = 0; t < NUM_TRACKS; t++) {
        const Step *st = &e->tr[t].steps[e->step];
        e->roll_next[t] = NUM_STEPS; /* nothing pending */
        if (!st->on) continue;
        engine_trigger(e, t, st->val, st->vel);
        if (roll_divs[st->roll] > 1) e->roll_next[t] = 1;
    }
}

void engine_play(Engine *e, int on)
{
    e->playing = on;
    if (!on) return;
    e->step = 0;
    e->step_pos = 0.0;
    e->step_len = step_length(e, 0);
    fire_step(e);
}

static void sequencer_tick(Engine *e)
{
    if (e->step_pos >= e->step_len) {
        e->step_pos -= e->step_len;
        e->step = (e->step + 1) % NUM_STEPS;
        e->step_len = step_length(e, e->step);
        fire_step(e);
    }
    for (int t = 0; t < NUM_TRACKS; t++) {
        int k = e->roll_next[t];
        if (k >= NUM_STEPS) continue;
        const Step *st = &e->tr[t].steps[e->step];
        int div = roll_divs[st->roll];
        if (k >= div) continue;
        if (e->step_pos >= e->step_len * k / div) {
            engine_trigger(e, t, st->val, st->vel);
            e->roll_next[t] = k + 1;
        }
    }
    e->step_pos += 1.0;
}

void engine_render(Engine *e, float *lr, int frames)
{
    float pan_l[NUM_TRACKS], pan_r[NUM_TRACKS];
    for (int t = 0; t < NUM_TRACKS; t++) {
        update_filters(e, t);
        float a = (e->tr[t].p.pan + 32) / 64.0f * (float)M_PI * 0.5f;
        pan_l[t] = cosf(a);
        pan_r[t] = sinf(a);
    }
    float master = e->master / 100.0f;
    float tail_step = 1.0f / (0.004f * e->sr);
    float peak = 0.0f;

    for (int f = 0; f < frames; f++) {
        if (e->playing) sequencer_tick(e);
        float l = 0.0f, r = 0.0f;
        for (int ti = 0; ti < NUM_TRACKS; ti++) {
            Track *t = &e->tr[ti];
            const TrackParams *p = &t->p;
            float x = voice_tick(t, &t->v);
            if (t->tail.active) {
                x += voice_tick(t, &t->tail) * t->tail_fade;
                t->tail_fade -= tail_step;
                if (t->tail_fade <= 0.0f) t->tail.active = 0;
            }
            if (p->lpf < 127) {
                x = svf_lp(&t->lp[0], x);
                if (p->machine == MACH_SP1200 && ti < 2) x = svf_lp(&t->lp[1], x);
            }
            if (p->machine == MACH_AMIGA && p->afilt != AFILT_A1200) x = onepole_lp(&t->a500, x);
            if (t->fixed_on) x = biquad_run(&t->fixed, x);
            if (p->machine == MACH_MPC3000) x = tanhf(x * 1.2f) / 1.2f;
            l += x * pan_l[ti];
            r += x * pan_r[ti];
        }
        l = tanhf(l * master);
        r = tanhf(r * master);
        lr[2 * f] = l;
        lr[2 * f + 1] = r;
        float a = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
        if (a > peak) peak = a;
    }
    e->peak = peak;
}
