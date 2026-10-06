/* Built-in sounds (a demo break and pad, plus the pad synth's sources) and
 * loading of sample sources by id. */
#ifndef ARDKORE_DEMO_H
#define ARDKORE_DEMO_H

#define DEMO_RATE 44100
#define DEMO_BREAK_BPM 170

/* Built-in source ids, in the order the file browser lists them. */
#define BUILTIN_COUNT 7
extern const char *const builtin_ids[BUILTIN_COUNT];

/* One bar of a 16-step drum break at DEMO_BREAK_BPM; 16 equal slices land on the steps. */
float *demo_break(int *len);

/* A sustained minor-9th chord pad (one-shot). */
float *demo_pad(int *len);

enum { SYNTH_CHOIR, SYNTH_STRINGS, SYNTH_GLASS, SYNTH_SAW, SYNTH_SUB, SYNTH_COUNT };

/* A seamlessly looping 2-second pad source rooted on C3 (SUB: C2), built
 * additively so every partial completes whole cycles within the loop. */
float *synth_source(int kind, int *len);

/* Load a source by id: "demo:break", "demo:pad", "synth:<kind>", or a .wav /
 * .vag path. *loop gets the loop start in samples, or -1. Returns 0 on success. */
int demo_load_source(const char *src_id, float **out, int *len, int *rate, int *loop, char *name,
                     int name_len, char *err, int errlen);

#endif
