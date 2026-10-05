#ifndef ARDKORE_WAV_H
#define ARDKORE_WAV_H

#include <stddef.h>

/* Load a WAV file (PCM 8/16/24/32-bit or 32-bit float, any channel count)
 * and mix it down to mono float. Returns 0 on success; on failure writes a
 * short reason into `err`. */
int wav_load_mono(const char *path, float **out, int *len, int *rate, char *err, size_t errlen);

/* Write interleaved stereo float as 16-bit PCM. Returns 0 on success. */
int wav_save_stereo16(const char *path, const float *lr, int frames, int rate);

#endif
