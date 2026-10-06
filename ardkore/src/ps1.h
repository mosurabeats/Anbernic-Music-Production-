/* PlayStation SPU pieces: ADPCM codec, Gaussian interpolation table, pitch
 * register quantisation, the hardware reverb with its factory presets, and
 * .VAG sample files. Constants and formulas follow the psx-spx reference. */
#ifndef ARDKORE_PS1_H
#define ARDKORE_PS1_H

#include <stddef.h>
#include <stdint.h>

extern const int16_t ps1_gauss[512];

/* Bytes needed to ADPCM-encode `len` samples (16 bytes per 28 samples). */
size_t ps1_adpcm_size(int len);

/* Encode 16-bit PCM to SPU-ADPCM blocks. loop_start < 0 makes a one-shot. */
void ps1_adpcm_encode(const int16_t *pcm, int len, uint8_t *out, int loop_start);

/* Decode until an end flag or `nblocks`. Returns samples written to `out`
 * (room for nblocks * 28 needed); *loop_start gets the loop point or -1. */
int ps1_adpcm_decode(const uint8_t *in, int nblocks, int16_t *out, int *loop_start);

/* Run audio through encode + decode in place, so it carries the 4-bit
 * ADPCM character. Values are clamped to 16 bits. */
void ps1_adpcm_roundtrip(float *data, int len);

/* The rate the SPU actually plays when asked for `rate` Hz: its 4.12 pitch
 * register (1000h = 44100 Hz) is integer and capped at 3FFFh. */
double ps1_pitch_rate(double rate);

int ps1_vag_load(const char *path, float **out, int *len, int *rate, int *loop_start,
                 char *err, size_t errlen);
int ps1_vag_save(const char *path, const int16_t *pcm, int len, int rate, int loop_start);

enum {
    PS1_RVB_OFF, PS1_RVB_ROOM, PS1_RVB_STUDIO_S, PS1_RVB_STUDIO_M, PS1_RVB_STUDIO_L,
    PS1_RVB_HALL, PS1_RVB_HALF_ECHO, PS1_RVB_SPACE_ECHO, PS1_RVB_CHAOS, PS1_RVB_DELAY,
    PS1_RVB_COUNT
};
extern const char *const ps1_reverb_names[PS1_RVB_COUNT];

#define PS1_RVB_MAX_HALFWORDS (0x18040 / 2)

typedef struct {
    int preset;
    int16_t regs[32];
    int n, cur; /* work area size and position, in halfwords */
    int16_t buf[PS1_RVB_MAX_HALFWORDS];
    float acc_l, acc_r, out_l, out_r, prev_l, prev_r;
    int phase;
} Ps1Reverb;

void ps1_reverb_init(Ps1Reverb *r, int preset);

/* Feed one full-rate stereo frame; the reverb itself runs at half rate like
 * the hardware (22050 Hz at 44.1 kHz). Output is the wet signal only. */
void ps1_reverb_run(Ps1Reverb *r, float in_l, float in_r, float *out_l, float *out_r);

#endif
