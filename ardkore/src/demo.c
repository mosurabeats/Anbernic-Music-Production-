#include "demo.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsp.h"
#include "wav.h"

static unsigned rng_state = 0x1234567u;

static float noise(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (rng_state & 0xFFFF) / 32768.0f - 1.0f;
}

static void add_kick(float *buf, int len, int at, float vel)
{
    double phase = 0.0;
    int dur = (int)(0.35 * DEMO_RATE);
    for (int i = 0; i < dur && at + i < len; i++) {
        float t = (float)i / DEMO_RATE;
        float freq = 48.0f + 120.0f * expf(-t * 28.0f);
        phase += 2.0 * M_PI * freq / DEMO_RATE;
        float body = sinf((float)phase) * expf(-t * 8.0f);
        float click = i < 90 ? noise() * (1.0f - i / 90.0f) * 0.4f : 0.0f;
        buf[at + i] += (body + click) * vel;
    }
}

static void add_snare(float *buf, int len, int at, float vel)
{
    double phase = 0.0;
    float prev = 0.0f;
    int dur = (int)(0.28 * DEMO_RATE);
    for (int i = 0; i < dur && at + i < len; i++) {
        float t = (float)i / DEMO_RATE;
        phase += 2.0 * M_PI * 190.0 / DEMO_RATE;
        float tone = sinf((float)phase) * expf(-t * 22.0f) * 0.55f;
        float n = noise();
        float hp = n - prev; /* crude high-pass for the rattle */
        prev = n;
        float rattle = (0.6f * n + 0.4f * hp) * expf(-t * 15.0f) * 0.75f;
        buf[at + i] += (tone + rattle) * vel;
    }
}

static void add_hat(float *buf, int len, int at, float vel, float decay)
{
    float p1 = 0.0f, p2 = 0.0f;
    int dur = (int)(0.12 * DEMO_RATE);
    for (int i = 0; i < dur && at + i < len; i++) {
        float t = (float)i / DEMO_RATE;
        float n = noise();
        float h1 = n - p1;
        p1 = n;
        float h2 = h1 - p2;
        p2 = h1;
        buf[at + i] += h2 * expf(-t * decay) * 0.25f * vel;
    }
}

static void normalise(float *buf, int len, float target)
{
    float peak = 0.0f;
    for (int i = 0; i < len; i++)
        if (fabsf(buf[i]) > peak) peak = fabsf(buf[i]);
    if (peak <= 0.0f) return;
    for (int i = 0; i < len; i++) buf[i] *= target / peak;
}

float *demo_break(int *len)
{
    int step = (int)(DEMO_RATE * 60.0 / (DEMO_BREAK_BPM * 4.0));
    int n = step * 16;
    float *buf = calloc((size_t)n, sizeof(float));
    if (!buf) return NULL;
    rng_state = 0x1234567u;

    /* K = kick, S = snare, g = ghost snare, per 16th step. */
    const char *kit = "K..K S.Kg .KK. S.gS";
    int s = 0;
    for (const char *c = kit; *c; c++) {
        if (*c == ' ') continue;
        int at = s * step;
        if (*c == 'K') add_kick(buf, n, at, 1.0f);
        if (*c == 'S') add_snare(buf, n, at, 0.9f);
        if (*c == 'g') add_snare(buf, n, at, 0.35f);
        add_hat(buf, n, at, (s & 1) ? 0.55f : 1.0f, (s % 4 == 2) ? 18.0f : 55.0f);
        s++;
    }
    normalise(buf, n, 0.9f);
    *len = n;
    return buf;
}

float *demo_pad(int *len)
{
    int n = (int)(3.0 * DEMO_RATE);
    float *buf = calloc((size_t)n, sizeof(float));
    if (!buf) return NULL;

    const int notes[] = {48, 55, 63, 70, 74}; /* C3 G3 Eb4 Bb4 D5 */
    const float detune[] = {-0.07f, 0.07f};
    for (int k = 0; k < 5; k++) {
        for (int d = 0; d < 2; d++) {
            double freq = 440.0 * pow(2.0, (notes[k] - 69 + detune[d]) / 12.0);
            double ph = (k * 0.13 + d * 0.41);
            for (int i = 0; i < n; i++) {
                ph += freq / DEMO_RATE;
                ph -= floor(ph);
                buf[i] += (float)(2.0 * ph - 1.0) * 0.1f;
            }
        }
    }
    OnePole lp1, lp2;
    onepole_set(&lp1, 1800.0f, DEMO_RATE);
    onepole_set(&lp2, 1800.0f, DEMO_RATE);
    lp1.y = lp2.y = 0.0f;
    int att = (int)(0.4 * DEMO_RATE), rel = (int)(0.9 * DEMO_RATE);
    for (int i = 0; i < n; i++) {
        float env = 1.0f;
        if (i < att) env = (float)i / att;
        if (i > n - rel) env = (float)(n - i) / rel;
        buf[i] = onepole_lp(&lp2, onepole_lp(&lp1, buf[i])) * env;
    }
    normalise(buf, n, 0.8f);
    *len = n;
    return buf;
}

int demo_load_source(const char *src_id, float **out, int *len, int *rate, char *name, int name_len,
                     char *err, int errlen)
{
    if (!strcmp(src_id, "demo:break")) {
        *out = demo_break(len);
        *rate = DEMO_RATE;
        snprintf(name, (size_t)name_len, "DEMO_BREAK");
        return *out ? 0 : -1;
    }
    if (!strcmp(src_id, "demo:pad")) {
        *out = demo_pad(len);
        *rate = DEMO_RATE;
        snprintf(name, (size_t)name_len, "DEMO_PAD");
        return *out ? 0 : -1;
    }
    if (wav_load_mono(src_id, out, len, rate, err, (size_t)errlen) != 0) return -1;
    const char *base = strrchr(src_id, '/');
    base = base ? base + 1 : src_id;
    snprintf(name, (size_t)name_len, "%s", base);
    char *dot = strrchr(name, '.');
    if (dot) *dot = 0;
    return 0;
}
