/* ARDKORE audio engine: 8 sample tracks with small voice pools, a 16-step
 * sequencer, machine voicings and a PS1-style reverb bus. No SDL in here so
 * it can be rendered and tested headless. */
#ifndef ARDKORE_ENGINE_H
#define ARDKORE_ENGINE_H

#include <stdint.h>

#include "dsp.h"
#include "ps1.h"
#include "sample.h"

#define NUM_TRACKS 8
#define NUM_STEPS 16
#define MAX_VOICES 8
#define SRC_ID_LEN 256

enum { MODE_SLICE, MODE_SAMPLE };
enum { CHOP_EQUAL, CHOP_AUTO };
enum { TRIG_GATE, TRIG_THRU };
enum { AFILT_A1200, AFILT_A500, AFILT_LED };

/* In SAMPLE mode a step's value is a transposition: val - SAMPLE_NOTE_CENTER semitones. */
#define SAMPLE_NOTE_CENTER 12
#define STEP_VAL_MAX 31
#define ROLL_COUNT 6
extern const int roll_divs[ROLL_COUNT]; /* 1 (off), 2, 3, 4, 6, 8 */

#define SPEED_COUNT 4
extern const int speed_divs[SPEED_COUNT]; /* 1, 2, 4, 8 sixteenths per track step */

enum {
    CHORD_OFF, CHORD_MAJ, CHORD_MIN, CHORD_MAJ7, CHORD_MIN7, CHORD_DOM7, CHORD_MAJ9,
    CHORD_MIN9, CHORD_MIN11, CHORD_SUS2, CHORD_SUS4, CHORD_FIFTH, CHORD_OCT, CHORD_COUNT
};

typedef struct {
    uint8_t on;
    uint8_t val;  /* slice index (SLICE) or note (SAMPLE) */
    uint8_t roll; /* index into roll_divs */
    uint8_t vel;  /* 1..64 */
} Step;

/* Every field is an int so the parameter table can address them generically. */
typedef struct {
    int machine, mode, srate, afilt;
    int pitch, fine, vol, pan;
    int attack, release, trig, rev;
    int chop, slices, thresh, zc;
    int lpf, stretch, cycle, loop;
    int chord, hold, speed, rvb;
    int vib;
} TrackParams;

typedef struct {
    int active;
    double pos;    /* read head, in baked samples */
    double anchor; /* stretch anchor: where the next grain restarts */
    double grain;  /* samples read in the current grain */
    double inc;    /* baked samples per output sample */
    int lo, hi;    /* playable region [lo, hi) */
    int loop_lo;   /* loop restart point, or -1 for a one-shot */
    int dir;
    int age;
    int attack_len, release_len;
    int gate;      /* output samples until note-off, -1 = none */
    int releasing;
    float rel_env, rel_mul; /* release: level and per-sample decay */
    float fade, fade_step;  /* fast fade when the voice is cut */
    float gain;
} Voice;

typedef struct {
    TrackParams p;
    Sample smp;
    char src_id[SRC_ID_LEN]; /* "demo:break", "synth:choir", a file path, ... */
    Step steps[NUM_STEPS];
    Voice voice[MAX_VOICES];
    int newest; /* most recently started voice, for the UI playhead */
    Svf lp[2];
    OnePole a500;
    Biquad fixed;
    int fixed_on;
    int filter_lpf, filter_machine, filter_afilt; /* settings the filters were built for */
    float lfo_s, lfo_c;                           /* vibrato oscillator */
    int last_slice;                               /* most recently triggered slice, for the UI */
} Track;

typedef struct {
    int sr;
    int bpm, swing, master;
    int rvb_preset, rvb_level; /* PS1 reverb bus */
    int playing;
    int step;      /* global sixteenth within the bar */
    uint32_t tick; /* sixteenths since play started */
    double step_pos, step_len;
    int roll_next[NUM_TRACKS];
    Track tr[NUM_TRACKS];
    Ps1Reverb rvb;
    int rvb_applied; /* preset the reverb was initialised with */
    float peak;
} Engine;

void engine_init(Engine *e, int sr);
void engine_free(Engine *e);
void engine_track_defaults(Track *t);

/* Install a new source on a track (takes ownership of `mono`), then bake and
 * slice it. `loop` is the loop start in source samples, or -1. */
void engine_track_load(Engine *e, int track, float *mono, int len, int rate, int loop,
                       const char *name, const char *src_id);
void engine_track_clear(Engine *e, int track);
void engine_track_rebake(Engine *e, int track);
void engine_track_reslice(Engine *e, int track);

void engine_trigger(Engine *e, int track, int val, int vel);
void engine_play(Engine *e, int on);

/* Render interleaved stereo float. */
void engine_render(Engine *e, float *lr, int frames);

/* Default step value for a new step at `step`: plays the matching slice. */
int engine_default_val(const Engine *e, int track, int step);

/* Step the track is on (tracks can run slower than the global clock). */
int engine_track_step(const Engine *e, int track);

int engine_active_voices(const Engine *e, int track);
const Voice *engine_newest_voice(const Engine *e, int track);

/* Semitone offsets of a chord; returns the count. */
int engine_chord_tones(int chord, int *tones);

#endif
