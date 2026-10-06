/* Machine voicings: the bit depth, sample rate, playback interpolation and
 * output filtering that give each classic sampler its character. */
#ifndef ARDKORE_MACHINE_H
#define ARDKORE_MACHINE_H

typedef enum {
    MACH_AMIGA,
    MACH_SP1200,
    MACH_MPC60,
    MACH_MPC3000,
    MACH_PS1,
    MACH_COUNT
} MachineId;

typedef struct {
    const char *name;
    int bits;        /* quantisation depth applied when the sample is baked */
    int native_rate; /* bake/playback rate in Hz; 0 = chosen from the ProTracker period table */
    int interpolate; /* INTERP_* */
    int nonlinear;   /* companded quantisation (MPC60-style 12-bit "non-linear") */
    int prefilter;   /* gentle anti-alias filter before resampling */
    int codec;       /* CODEC_* applied when baking */
} Machine;

enum { INTERP_NONE, INTERP_LINEAR, INTERP_GAUSS };
enum { CODEC_NONE, CODEC_PS1_ADPCM };

extern const Machine machines[MACH_COUNT];

/* ProTracker period table, finetune 0, C-1 .. B-3 plus C-4 (Amigo's 33 kHz top end). */
#define PT_NUM_PERIODS 37
#define PT_PAL_CLOCK 3546895.0
#define PT_DEFAULT_PERIOD 29 /* F-3, period 160 = 22168 Hz */

/* PS1 sample rates offered for baking (incl. the CD-XA rates). */
#define PS1_NUM_RATES 10
#define PS1_DEFAULT_RATE 5 /* 22050 Hz */
extern const int ps1_rates[PS1_NUM_RATES];

extern const int pt_periods[PT_NUM_PERIODS];
extern const char *const pt_note_names[PT_NUM_PERIODS];

double pt_rate(int period_index);

/* Playback rate in Hz for a sample baked at `period_index` (Amiga) or the
 * machine's native rate, transposed by `semis` (may be fractional). Amiga
 * transposition rounds to integer periods like the real hardware. */
double machine_play_rate(int machine, int period_index, double semis);

/* Rate at which a sample is baked for this machine. `rate_index` picks from
 * the machine's rate list (ProTracker periods, PS1 rates) and is ignored by
 * fixed-rate machines. */
double machine_bake_rate(int machine, int rate_index);

int machine_rate_count(int machine);
int machine_default_rate(int machine);

#endif
