/* Small filter building blocks used by the engine. */
#ifndef ARDKORE_DSP_H
#define ARDKORE_DSP_H

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct { float a, y; } OnePole;

static inline void onepole_set(OnePole *f, float cutoff, float sr)
{
    f->a = 1.0f - expf(-2.0f * (float)M_PI * cutoff / sr);
}

static inline float onepole_lp(OnePole *f, float x)
{
    f->y += f->a * (x - f->y);
    return f->y;
}

/* RBJ biquad low-pass. */
typedef struct { float b0, b1, b2, a1, a2, z1, z2; } Biquad;

static inline void biquad_lowpass(Biquad *f, float cutoff, float q, float sr)
{
    float w = 2.0f * (float)M_PI * cutoff / sr;
    float c = cosf(w), s = sinf(w);
    float alpha = s / (2.0f * q);
    float a0 = 1.0f + alpha;
    f->b0 = (1.0f - c) * 0.5f / a0;
    f->b1 = (1.0f - c) / a0;
    f->b2 = f->b0;
    f->a1 = -2.0f * c / a0;
    f->a2 = (1.0f - alpha) / a0;
}

static inline float biquad_run(Biquad *f, float x)
{
    float y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}

/* Topology-preserving state variable filter (low-pass output), stable under
 * cutoff modulation. */
typedef struct { float a1, a2, a3, k, ic1, ic2; } Svf;

static inline void svf_set(Svf *f, float cutoff, float q, float sr)
{
    if (cutoff > sr * 0.45f) cutoff = sr * 0.45f;
    float g = tanf((float)M_PI * cutoff / sr);
    f->k = 1.0f / q;
    f->a1 = 1.0f / (1.0f + g * (g + f->k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}

static inline float svf_lp(Svf *f, float x)
{
    float v3 = x - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    return v2;
}

#endif
