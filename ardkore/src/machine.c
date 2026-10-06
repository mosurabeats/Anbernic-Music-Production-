#include "machine.h"

#include <math.h>

#include "ps1.h"

/* Values are approximations of each machine's sampling path, meant to be
 * tuned by ear rather than circuit-accurate models. */
const Machine machines[MACH_COUNT] = {
    /* name       bits  rate   interp          nonlin prefilter codec */
    {"AMIGA",      8,    0,     INTERP_NONE,    0,     0,        CODEC_NONE},
    {"SP1200",    12,   26040,  INTERP_NONE,    0,     0,        CODEC_NONE},
    {"MPC60",     12,   40000,  INTERP_LINEAR,  1,     1,        CODEC_NONE},
    {"MPC3000",   16,   44100,  INTERP_LINEAR,  0,     1,        CODEC_NONE},
    {"PS1",       16,    0,     INTERP_GAUSS,   0,     1,        CODEC_PS1_ADPCM},
};

const int ps1_rates[PS1_NUM_RATES] = {5512, 8000, 11025, 16000, 18900, 22050, 24000, 32000, 37800, 44100};

const int pt_periods[PT_NUM_PERIODS] = {
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113,
    107,
};

const char *const pt_note_names[PT_NUM_PERIODS] = {
    "C-1", "C#1", "D-1", "D#1", "E-1", "F-1", "F#1", "G-1", "G#1", "A-1", "A#1", "B-1",
    "C-2", "C#2", "D-2", "D#2", "E-2", "F-2", "F#2", "G-2", "G#2", "A-2", "A#2", "B-2",
    "C-3", "C#3", "D-3", "D#3", "E-3", "F-3", "F#3", "G-3", "G#3", "A-3", "A#3", "B-3",
    "C-4",
};

double pt_rate(int period_index)
{
    if (period_index < 0) period_index = 0;
    if (period_index >= PT_NUM_PERIODS) period_index = PT_NUM_PERIODS - 1;
    return PT_PAL_CLOCK / pt_periods[period_index];
}

int machine_rate_count(int machine)
{
    if (machine == MACH_AMIGA) return PT_NUM_PERIODS;
    if (machine == MACH_PS1) return PS1_NUM_RATES;
    return 1;
}

int machine_default_rate(int machine)
{
    if (machine == MACH_AMIGA) return PT_DEFAULT_PERIOD;
    if (machine == MACH_PS1) return PS1_DEFAULT_RATE;
    return 0;
}

static int clamp_rate(int machine, int index)
{
    int n = machine_rate_count(machine);
    return index < 0 ? 0 : index >= n ? n - 1 : index;
}

double machine_bake_rate(int machine, int rate_index)
{
    if (machine == MACH_AMIGA) return pt_rate(rate_index);
    if (machine == MACH_PS1) return ps1_rates[clamp_rate(machine, rate_index)];
    return machines[machine].native_rate;
}

double machine_play_rate(int machine, int period_index, double semis)
{
    if (machine == MACH_AMIGA) {
        if (period_index < 0) period_index = 0;
        if (period_index >= PT_NUM_PERIODS) period_index = PT_NUM_PERIODS - 1;
        double period = floor(pt_periods[period_index] * pow(2.0, -semis / 12.0) + 0.5);
        if (period < 28) period = 28; /* keep extreme transpositions sane */
        return PT_PAL_CLOCK / period;
    }
    if (machine == MACH_PS1)
        return ps1_pitch_rate(ps1_rates[clamp_rate(machine, period_index)] * pow(2.0, semis / 12.0));
    return machines[machine].native_rate * pow(2.0, semis / 12.0);
}
