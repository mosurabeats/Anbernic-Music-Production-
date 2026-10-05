/* Synthesised demo sounds so ARDKORE makes noise before any samples are added. */
#ifndef ARDKORE_DEMO_H
#define ARDKORE_DEMO_H

#define DEMO_RATE 44100
#define DEMO_BREAK_BPM 170

/* One bar of a 16-step drum break at DEMO_BREAK_BPM; 16 equal slices land on the steps. */
float *demo_break(int *len);

/* A sustained minor-9th chord pad. */
float *demo_pad(int *len);

/* Load a source by id ("demo:break", "demo:pad" or a WAV path). Returns 0 on success. */
int demo_load_source(const char *src_id, float **out, int *len, int *rate, char *name, int name_len,
                     char *err, int errlen);

#endif
