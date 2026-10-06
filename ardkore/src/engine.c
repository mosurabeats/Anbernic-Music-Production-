#include "engine.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "params.h"

const int roll_divs[ROLL_COUNT] = {1, 2, 3, 4, 6, 8};
const int speed_divs[SPEED_COUNT] = {1, 2, 4, 8};

#define CUT_FADE_S 0.004f
#define VIB_RATE_HZ 5.5f
#define RELEASE_FLOOR 1e-3f

static const int chord_table[CHORD_COUNT][7] = {
    /* count, tones... */
    {1, 0},
    {3, 0, 4, 7},
    {3, 0, 3, 7},
    {4, 0, 4, 7, 11},
    {4, 0, 3, 7, 10},
    {4, 0, 4, 7, 10},
    {5, 0, 4, 7, 11, 14},
    {5, 0, 3, 7, 10, 14},
    {6, 0, 3, 7, 10, 14, 17},
    {3, 0, 2, 7},
    {3, 0, 5, 7},
    {2, 0, 7},
    {2, 0, 12},
};

int engine_chord_tones(int chord, int *tones)
{
    if (chord < 0 || chord >= CHORD_COUNT) chord = CHORD_OFF;
    int n = chord_table[chord][0];
    for (int i = 0; i < n; i++) tones[i] = chord_table[chord][1 + i];
    return n;
}

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
    t->lfo_s = 0.0f;
    t->lfo_c = 1.0f;
}

void engine_init(Engine *e, int sr)
{
    memset(e, 0, sizeof(*e));
    e->sr = sr;
    e->bpm = 170;
    e->swing = 50;
    e->master = 80;
    e->rvb_preset = PS1_RVB_OFF;
    e->rvb_level = 60;
    ps1_reverb_init(&e->rvb, e->rvb_preset);
    e->rvb_applied = e->rvb_preset;
    for (int t = 0; t < NUM_TRACKS; t++) engine_track_defaults(&e->tr[t]);
}

void engine_free(Engine *e)
{
    for (int t = 0; t < NUM_TRACKS; t++) sample_free(&e->tr[t].smp);
}

static void stop_voices(Track *t)
{
    for (int i = 0; i < MAX_VOICES; i++) t->voice[i].active = 0;
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
    sample_bake(&t->smp, machine_bake_rate(t->p.machine, t->p.srate), m->bits, m->nonlinear, m->prefilter,
                m->codec);
    engine_track_reslice(e, track);
}

void engine_track_load(Engine *e, int track, float *mono, int len, int rate, int loop,
                       const char *name, const char *src_id)
{
    Track *t = &e->tr[track];
    stop_voices(t);
    sample_set_source(&t->smp, mono, len, rate, loop, name);
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

int engine_track_step(const Engine *e, int track)
{
    int div = speed_divs[e->tr[track].p.speed];
    return (int)((e->tick / (uint32_t)div) % NUM_STEPS);
}

int engine_active_voices(const Engine *e, int track)
{
    int n = 0;
    for (int i = 0; i < MAX_VOICES; i++) n += e->tr[track].voice[i].active;
    return n;
}

const Voice *engine_newest_voice(const Engine *e, int track)
{
    const Track *t = &e->tr[track];
    return &t->voice[t->newest];
}

static void start_release(Voice *v, int release_len)
{
    float level = v->attack_len > 0 && v->age < v->attack_len ? (float)v->age / v->attack_len : 1.0f;
    if (release_len < 1) release_len = 1;
    v->releasing = 1;
    v->rel_env = level;
    v->rel_mul = expf(logf(RELEASE_FLOOR) / release_len);
}

static void cut_voice(Voice *v, int sr)
{
    if (v->fade_step > 0.0f) return;
    v->fade = 1.0f;
    v->fade_step = 1.0f / (CUT_FADE_S * sr);
}

static Voice *alloc_voice(Track *t)
{
    int best = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!t->voice[i].active) {
            t->newest = i;
            return &t->voice[i];
        }
        if (t->voice[i].age > t->voice[best].age) best = i;
    }
    t->newest = best; /* steal the oldest */
    return &t->voice[best];
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
    int loop_lo = -1;
    if (p->loop) loop_lo = s->loop_start > lo && s->loop_start < hi - 1 ? s->loop_start : lo;

    /* Earlier notes: slices are cut, sampled/pad notes fall into their release. */
    int release_len = p->release * e->sr / 1000;
    int min_release = (int)(CUT_FADE_S * e->sr);
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &t->voice[i];
        if (!v->active) continue;
        if (p->mode == MODE_SLICE) cut_voice(v, e->sr);
        else if (!v->releasing) start_release(v, release_len > min_release ? release_len : min_release);
    }

    int tones[7];
    int n = engine_chord_tones(p->chord, tones);
    float gain = (vel / 64.0f) * (p->vol / 64.0f) / sqrtf((float)n);
    int gate = -1;
    if (p->hold > 0) gate = (int)(p->hold * speed_divs[p->speed] * (e->sr * 60.0 / (e->bpm * 4.0)));

    for (int k = 0; k < n; k++) {
        Voice *v = alloc_voice(t);
        memset(v, 0, sizeof(*v));
        v->dir = p->rev ? -1 : 1;
        v->lo = lo;
        v->hi = hi;
        v->loop_lo = loop_lo;
        v->pos = p->rev ? hi - 1 : lo;
        v->anchor = v->pos;
        v->inc = machine_play_rate(p->machine, p->srate, semis + tones[k]) / e->sr;
        v->attack_len = p->attack * e->sr / 1000;
        v->release_len = release_len;
        v->gate = gate;
        v->gain = gain;
        v->active = 1;
    }
}

static float gauss_f[512];
static int gauss_ready;

static float sample_at(const Sample *s, int i) { return i >= 0 && i < s->len ? s->data[i] : 0.0f; }

static float read_sample(const Sample *s, const Voice *v, int interp)
{
    int k = (int)v->pos;
    if (interp == INTERP_GAUSS) {
        /* SPU 4-point Gaussian over the newest four samples in play order. */
        double frac = v->pos - k;
        if (v->dir < 0) frac = 1.0 - frac;
        int f = (int)(frac * 256.0);
        if (f > 255) f = 255;
        int d = v->dir;
        return gauss_f[255 - f] * sample_at(s, k - 3 * d) + gauss_f[511 - f] * sample_at(s, k - 2 * d) +
               gauss_f[256 + f] * sample_at(s, k - d) + gauss_f[f] * sample_at(s, k);
    }
    if (k < v->lo) k = v->lo;
    if (k >= v->hi) k = v->hi - 1;
    float x = s->data[k];
    if (interp == INTERP_LINEAR) {
        int j = k + 1 < s->len ? k + 1 : k;
        float frac = (float)(v->pos - k);
        if (frac < 0.0f) frac = 0.0f;
        x += (s->data[j] - x) * frac;
    }
    return x;
}

static float voice_tick(Track *t, Voice *v, float pitch_mul)
{
    const Sample *s = &t->smp;
    const TrackParams *p = &t->p;
    float x = read_sample(s, v, machines[p->machine].interpolate);

    /* Envelope. */
    float env;
    if (v->releasing) {
        env = v->rel_env;
        v->rel_env *= v->rel_mul;
        if (v->rel_env < RELEASE_FLOOR) v->active = 0;
    } else {
        env = v->attack_len > 0 && v->age < v->attack_len ? (float)v->age / v->attack_len : 1.0f;
        if (v->gate >= 0 && v->age >= v->gate) start_release(v, v->release_len);
    }
    double ratio = p->stretch / 100.0;
    double step = v->inc * pitch_mul;
    if (v->loop_lo < 0 && v->gate < 0 && !v->releasing && v->release_len > 0) {
        /* One-shots fade out into the end of their region. */
        double head = p->stretch == 100 ? v->pos : v->anchor;
        double speed = step / (p->stretch == 100 ? 1.0 : ratio);
        double remain = (v->dir > 0 ? v->hi - head : head - v->lo) / speed;
        if (remain < v->release_len) env *= (float)(remain / v->release_len);
    }
    if (v->fade_step > 0.0f) {
        env *= v->fade;
        v->fade -= v->fade_step;
        if (v->fade <= 0.0f) v->active = 0;
    }
    float out = x * env * v->gain;

    /* Advance. Cyclic stretch (S950 style): the read head plays grains at the
     * pitched rate and jumps back to an anchor that moves at the stretched rate. */
    v->age++;
    v->pos += step * v->dir;
    double head;
    if (p->stretch != 100) {
        v->anchor += step * v->dir / ratio;
        v->grain += step;
        double cycle = p->cycle * s->rate / 1000.0;
        if (v->grain >= cycle) {
            v->pos = v->anchor;
            v->grain = 0.0;
        }
        head = v->anchor;
    } else {
        head = v->pos;
    }
    int past = v->dir > 0 ? head >= v->hi : head < v->lo;
    if (past && v->loop_lo >= 0) {
        double span = v->dir > 0 ? v->hi - v->loop_lo : v->hi - v->lo;
        v->pos -= span * v->dir;
        v->anchor -= span * v->dir;
    } else if (past) {
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
    case MACH_PS1:
        t->fixed_on = 0; /* the Gaussian interpolation already does the smoothing */
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
        e->roll_next[t] = NUM_STEPS; /* nothing pending */
        int div = speed_divs[e->tr[t].p.speed];
        if (e->tick % (uint32_t)div) continue;
        const Step *st = &e->tr[t].steps[engine_track_step(e, t)];
        if (!st->on) continue;
        engine_trigger(e, t, st->val, st->vel);
        if (roll_divs[st->roll] > 1) e->roll_next[t] = 1;
    }
}

void engine_play(Engine *e, int on)
{
    e->playing = on;
    if (!on) return;
    e->tick = 0;
    e->step = 0;
    e->step_pos = 0.0;
    e->step_len = step_length(e, 0);
    fire_step(e);
}

static void sequencer_tick(Engine *e)
{
    if (e->step_pos >= e->step_len) {
        e->step_pos -= e->step_len;
        e->tick++;
        e->step = (int)(e->tick % NUM_STEPS);
        e->step_len = step_length(e, e->step);
        fire_step(e);
    }
    for (int t = 0; t < NUM_TRACKS; t++) {
        int k = e->roll_next[t];
        if (k >= NUM_STEPS) continue;
        const Step *st = &e->tr[t].steps[engine_track_step(e, t)];
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
    if (!gauss_ready) {
        for (int i = 0; i < 512; i++) gauss_f[i] = ps1_gauss[i] / 32768.0f;
        gauss_ready = 1;
    }
    if (e->rvb_applied != e->rvb_preset) {
        ps1_reverb_init(&e->rvb, e->rvb_preset);
        e->rvb_applied = e->rvb_preset;
    }

    float pan_l[NUM_TRACKS], pan_r[NUM_TRACKS], send[NUM_TRACKS];
    for (int t = 0; t < NUM_TRACKS; t++) {
        update_filters(e, t);
        float a = (e->tr[t].p.pan + 32) / 64.0f * (float)M_PI * 0.5f;
        pan_l[t] = cosf(a);
        pan_r[t] = sinf(a);
        send[t] = e->tr[t].p.rvb / 64.0f;
    }
    float master = e->master / 100.0f;
    float wet = e->rvb_level / 100.0f;
    float lfo_w = 2.0f * (float)M_PI * VIB_RATE_HZ / e->sr;
    float peak = 0.0f;

    for (int f = 0; f < frames; f++) {
        if (e->playing) sequencer_tick(e);
        float l = 0.0f, r = 0.0f, sl = 0.0f, sr = 0.0f;
        for (int ti = 0; ti < NUM_TRACKS; ti++) {
            Track *t = &e->tr[ti];
            const TrackParams *p = &t->p;
            float pitch_mul = 1.0f;
            if (p->vib > 0) {
                t->lfo_s += lfo_w * t->lfo_c;
                t->lfo_c -= lfo_w * t->lfo_s;
                pitch_mul = 1.0f + p->vib * t->lfo_s * (0.6931472f / 1200.0f);
            }
            float x = 0.0f;
            for (int i = 0; i < MAX_VOICES; i++)
                if (t->voice[i].active) x += voice_tick(t, &t->voice[i], pitch_mul);
            if (p->lpf < 127) {
                x = svf_lp(&t->lp[0], x);
                if (p->machine == MACH_SP1200 && ti < 2) x = svf_lp(&t->lp[1], x);
            }
            if (p->machine == MACH_AMIGA && p->afilt != AFILT_A1200) x = onepole_lp(&t->a500, x);
            if (t->fixed_on) x = biquad_run(&t->fixed, x);
            if (p->machine == MACH_MPC3000) x = tanhf(x * 1.2f) / 1.2f;
            float xl = x * pan_l[ti], xr = x * pan_r[ti];
            l += xl;
            r += xr;
            sl += xl * send[ti];
            sr += xr * send[ti];
        }
        float wl, wr;
        ps1_reverb_run(&e->rvb, sl, sr, &wl, &wr);
        l = tanhf((l + wl * wet) * master);
        r = tanhf((r + wr * wet) * master);
        lr[2 * f] = l;
        lr[2 * f + 1] = r;
        float a = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
        if (a > peak) peak = a;
    }
    e->peak = peak;
}
