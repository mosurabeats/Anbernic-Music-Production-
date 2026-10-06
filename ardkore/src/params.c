#include "params.h"

#include <stdio.h>
#include <string.h>

#include "machine.h"

static const char *const machine_labels[] = {"AMIGA", "SP1200", "MPC60", "MPC3000", "PS1"};
static const char *const mode_labels[] = {"SLICE", "SAMPLE"};
static const char *const afilt_labels[] = {"A1200", "A500", "LED"};
static const char *const trig_labels[] = {"GATE", "THRU"};
static const char *const onoff_labels[] = {"OFF", "ON"};
static const char *const chop_labels[] = {"EQUAL", "AUTO"};
static const char *const chord_labels[] = {"OFF", "MAJ", "MIN", "MAJ7", "MIN7", "DOM7", "MAJ9",
                                           "MIN9", "MIN11", "SUS2", "SUS4", "5TH", "OCT"};
static const char *const speed_labels[] = {"1X", "1/2", "1/4", "1/8"};

#define P(name, key, field, min, max, def, step, coarse, flags, labels) \
    {name, key, offsetof(TrackParams, field), min, max, def, step, coarse, flags, labels}

/* Laid out as a 4-column grid on the SAMPLE page, row by row. */
const ParamDef param_defs[] = {
    P("MACH", "machine", machine, 0, MACH_COUNT - 1, MACH_AMIGA, 1, 1, PF_REBAKE, machine_labels),
    P("MODE", "mode", mode, 0, 1, MODE_SLICE, 1, 1, 0, mode_labels),
    P("S-RATE", "srate", srate, 0, PT_NUM_PERIODS - 1, PT_DEFAULT_PERIOD, 1, 12, PF_REBAKE, NULL),
    P("AFILT", "afilt", afilt, 0, 2, AFILT_A1200, 1, 1, 0, afilt_labels),

    P("PITCH", "pitch", pitch, -24, 24, 0, 1, 12, 0, NULL),
    P("FINE", "fine", fine, -8, 7, 0, 1, 4, 0, NULL),
    P("VOL", "vol", vol, 0, 64, 64, 1, 8, 0, NULL),
    P("PAN", "pan", pan, -32, 32, 0, 1, 8, 0, NULL),

    P("ATTACK", "attack", attack, 0, 2000, 0, 5, 50, 0, NULL),
    P("RELEAS", "release", release, 0, 4000, 10, 5, 100, 0, NULL),
    P("HOLD", "hold", hold, 0, 16, 0, 1, 4, 0, NULL),
    P("TRIG", "trig", trig, 0, 1, TRIG_GATE, 1, 1, 0, trig_labels),

    P("CHORD", "chord", chord, 0, CHORD_COUNT - 1, CHORD_OFF, 1, 1, 0, chord_labels),
    P("LOOP", "loop", loop, 0, 1, 0, 1, 1, 0, onoff_labels),
    P("SPEED", "speed", speed, 0, SPEED_COUNT - 1, 0, 1, 1, 0, speed_labels),
    P("REV", "rev", rev, 0, 1, 0, 1, 1, 0, onoff_labels),

    P("CHOP", "chop", chop, 0, 1, CHOP_EQUAL, 1, 1, PF_RESLICE, chop_labels),
    P("SLICES", "slices", slices, 1, MAX_SLICES, 16, 1, 4, PF_RESLICE, NULL),
    P("THRESH", "thresh", thresh, 1, 32, 16, 1, 4, PF_RESLICE, NULL),
    P("ZC", "zc", zc, 0, 1, 1, 1, 1, PF_RESLICE, onoff_labels),

    P("LPF", "lpf", lpf, 0, 127, 127, 1, 8, 0, NULL),
    P("STRTCH", "stretch", stretch, 25, 400, 100, 5, 25, 0, NULL),
    P("CYCLE", "cycle", cycle, 5, 250, 40, 1, 10, 0, NULL),
    P("VIB", "vib", vib, 0, 50, 0, 1, 5, 0, NULL),

    P("RVB", "rvb", rvb, 0, 64, 0, 1, 8, 0, NULL),
};

const int param_count = (int)(sizeof(param_defs) / sizeof(param_defs[0]));

int *param_ptr(TrackParams *p, int index)
{
    return (int *)((char *)p + param_defs[index].offset);
}

int param_get(const TrackParams *p, int index)
{
    return *(const int *)((const char *)p + param_defs[index].offset);
}

int param_find(const char *key)
{
    for (int i = 0; i < param_count; i++)
        if (!strcmp(param_defs[i].key, key)) return i;
    return -1;
}

void param_format(const TrackParams *p, int index, char *buf, size_t len)
{
    const ParamDef *d = &param_defs[index];
    int v = param_get(p, index);
    if (d->labels) {
        snprintf(buf, len, "%s", d->labels[v]);
        return;
    }
    switch (d->offset) {
    case offsetof(TrackParams, srate):
        snprintf(buf, len, "%d", (int)(machine_bake_rate(p->machine, v) + 0.5));
        break;
    case offsetof(TrackParams, pitch):
    case offsetof(TrackParams, fine):
    case offsetof(TrackParams, pan):
        snprintf(buf, len, "%+d", v);
        break;
    case offsetof(TrackParams, lpf):
        if (v >= 127) snprintf(buf, len, "OPEN");
        else snprintf(buf, len, "%d", v);
        break;
    case offsetof(TrackParams, stretch):
        snprintf(buf, len, "%d%%", v);
        break;
    case offsetof(TrackParams, hold):
        if (v == 0) snprintf(buf, len, "NEXT");
        else snprintf(buf, len, "%d ST", v);
        break;
    case offsetof(TrackParams, attack):
    case offsetof(TrackParams, release):
    case offsetof(TrackParams, cycle):
        snprintf(buf, len, "%dMS", v);
        break;
    default:
        snprintf(buf, len, "%d", v);
        break;
    }
}
