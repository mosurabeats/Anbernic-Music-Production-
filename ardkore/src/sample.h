/* Sample storage, baking (resample + quantise) and slicing. */
#ifndef ARDKORE_SAMPLE_H
#define ARDKORE_SAMPLE_H

#define MAX_SLICES 32
#define SAMPLE_NAME_LEN 64

typedef struct {
    char name[SAMPLE_NAME_LEN];

    /* Original audio, mono float. */
    float *src;
    int src_len;
    int src_rate;
    int src_loop; /* loop start in source samples, -1 = none (loops whole region) */

    /* Baked audio at the machine's rate and bit depth; what actually plays. */
    float *data;
    int len;
    double rate;
    int loop_start; /* in baked samples, -1 = none */

    /* Slice i spans [slice[i], slice[i + 1]). slice[nslices] == len. */
    int slice[MAX_SLICES + 1];
    int nslices;
} Sample;

void sample_free(Sample *s);

/* Takes ownership of `mono`. `loop` is the loop start in source samples or -1. */
void sample_set_source(Sample *s, float *mono, int len, int rate, int loop, const char *name);

/* Resample the source to `rate`, quantise to `bits` and run it through
 * `codec` (a CODEC_* value). Returns 0 on success. */
int sample_bake(Sample *s, double rate, int bits, int nonlinear, int prefilter, int codec);

void sample_slice_equal(Sample *s, int n, int zero_cross);
void sample_slice_auto(Sample *s, int thresh, int zero_cross);

/* Quantise one value the way sample_bake does. Exposed for tests. */
float sample_quantise(float x, int bits, int nonlinear);

#endif
