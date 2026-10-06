#include "sample.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dsp.h"
#include "machine.h"
#include "ps1.h"

void sample_free(Sample *s)
{
    free(s->src);
    free(s->data);
    memset(s, 0, sizeof(*s));
}

void sample_set_source(Sample *s, float *mono, int len, int rate, int loop, const char *name)
{
    sample_free(s);
    s->src = mono;
    s->src_len = len;
    s->src_rate = rate;
    s->src_loop = loop >= 0 && loop < len ? loop : -1;
    s->loop_start = -1;
    strncpy(s->name, name ? name : "", SAMPLE_NAME_LEN - 1);
}

float sample_quantise(float x, int bits, int nonlinear)
{
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    float levels = (float)((1 << (bits - 1)) - 1);
    if (!nonlinear) return roundf(x * levels) / levels;

    /* Compand, quantise, expand: finer steps near silence, coarser when loud. */
    const float mu = 31.0f;
    float sign = x < 0 ? -1.0f : 1.0f;
    float y = logf(1.0f + mu * fabsf(x)) / logf(1.0f + mu);
    y = roundf(y * levels) / levels;
    return sign * (powf(1.0f + mu, y) - 1.0f) / mu;
}

int sample_bake(Sample *s, double rate, int bits, int nonlinear, int prefilter, int codec)
{
    if (!s->src || s->src_len <= 0) return -1;

    int len = (int)((double)s->src_len * rate / s->src_rate);
    if (len < 1) len = 1;
    float *out = malloc(sizeof(float) * (size_t)len);
    if (!out) return -1;

    const float *src = s->src;
    float *filtered = NULL;
    if (prefilter && rate < s->src_rate) {
        filtered = malloc(sizeof(float) * (size_t)s->src_len);
        if (filtered) {
            Biquad f = {0};
            biquad_lowpass(&f, (float)rate * 0.45f, 0.707f, (float)s->src_rate);
            for (int i = 0; i < s->src_len; i++) filtered[i] = biquad_run(&f, s->src[i]);
            src = filtered;
        }
    }

    /* Linear-interpolated resample with no anti-alias filter by default:
     * the aliasing is part of the sound. */
    double step = (double)s->src_rate / rate;
    for (int i = 0; i < len; i++) {
        double t = i * step;
        int j = (int)t;
        float frac = (float)(t - j);
        float a = src[j < s->src_len ? j : s->src_len - 1];
        float b = src[j + 1 < s->src_len ? j + 1 : s->src_len - 1];
        out[i] = sample_quantise(a + (b - a) * frac, bits, nonlinear);
    }
    free(filtered);
    if (codec == CODEC_PS1_ADPCM) ps1_adpcm_roundtrip(out, len);

    free(s->data);
    s->data = out;
    s->len = len;
    s->rate = rate;
    s->loop_start = s->src_loop >= 0 ? (int)((double)s->src_loop * rate / s->src_rate) : -1;
    if (s->loop_start >= len) s->loop_start = -1;
    s->nslices = 1;
    s->slice[0] = 0;
    s->slice[1] = len;
    return 0;
}

static int snap_zero_cross(const Sample *s, int pos)
{
    int window = (int)(s->rate / 500.0); /* +-2 ms */
    if (window < 4) window = 4;
    for (int d = 0; d <= window; d++) {
        int cand[2] = {pos - d, pos + d};
        for (int c = 0; c < 2; c++) {
            int j = cand[c];
            if (j <= 0 || j >= s->len) continue;
            if ((s->data[j - 1] <= 0.0f) != (s->data[j] <= 0.0f)) return j;
        }
    }
    return pos;
}

static void finish_slices(Sample *s, int *bounds, int n, int zero_cross)
{
    /* bounds[0..n-1] are slice starts; bounds[0] is always 0. */
    int k = 0;
    for (int i = 0; i < n && k < MAX_SLICES; i++) {
        int b = bounds[i];
        if (i > 0 && zero_cross) b = snap_zero_cross(s, b);
        if (k > 0 && b <= s->slice[k - 1]) continue;
        if (b >= s->len) break;
        s->slice[k++] = b;
    }
    if (k == 0) s->slice[k++] = 0;
    s->slice[0] = 0;
    s->slice[k] = s->len;
    s->nslices = k;
}

void sample_slice_equal(Sample *s, int n, int zero_cross)
{
    if (!s->data) return;
    if (n < 1) n = 1;
    if (n > MAX_SLICES) n = MAX_SLICES;
    int bounds[MAX_SLICES];
    for (int i = 0; i < n; i++) bounds[i] = (int)((long long)s->len * i / n);
    finish_slices(s, bounds, n, zero_cross);
}

void sample_slice_auto(Sample *s, int thresh, int zero_cross)
{
    if (!s->data) return;
    int hop = (int)(s->rate / 200.0); /* 5 ms analysis frames */
    if (hop < 16) hop = 16;
    int frames = s->len / hop;
    if (frames < 3) {
        sample_slice_equal(s, 1, zero_cross);
        return;
    }

    float *env = malloc(sizeof(float) * (size_t)frames);
    if (!env) return;
    float max_env = 0.0f;
    for (int k = 0; k < frames; k++) {
        float sum = 0.0f;
        for (int i = 0; i < hop; i++) sum += fabsf(s->data[k * hop + i]);
        env[k] = sum / hop;
        if (env[k] > max_env) max_env = env[k];
    }

    /* Onset strength = rise in energy between frames. */
    float max_flux = 0.0f;
    for (int k = 1; k < frames; k++) {
        float d = env[k] - env[k - 1];
        if (d > max_flux) max_flux = d;
    }

    if (thresh < 1) thresh = 1;
    if (thresh > 32) thresh = 32;
    float t = 1.0f - thresh / 33.0f;
    float flux_thresh = max_flux * t * t;
    int min_gap = (int)(0.06 * s->rate / hop); /* at least 60 ms between slices */

    int bounds[MAX_SLICES];
    int n = 0;
    bounds[n++] = 0;
    int last = -min_gap;
    for (int k = 1; k + 1 < frames && n < MAX_SLICES; k++) {
        float d = env[k] - env[k - 1];
        float prev = k > 1 ? env[k - 1] - env[k - 2] : 0.0f;
        float next = env[k + 1] - env[k];
        if (d <= flux_thresh || d < prev || d < next) continue;
        if (env[k] < max_env * 0.02f) continue;
        if (k - last < min_gap) continue;
        int pos = (k - 1) * hop; /* back off one frame to keep the attack */
        if (pos <= bounds[n - 1]) continue;
        bounds[n++] = pos;
        last = k;
    }
    free(env);
    finish_slices(s, bounds, n, zero_cross);
}
