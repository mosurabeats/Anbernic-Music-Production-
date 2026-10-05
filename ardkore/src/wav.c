#include "wav.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WAV_BYTES (256L * 1024 * 1024)
#define MAX_SECONDS 60

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void set_err(char *err, size_t errlen, const char *msg)
{
    if (err && errlen) snprintf(err, errlen, "%s", msg);
}

int wav_load_mono(const char *path, float **out, int *len, int *rate, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        set_err(err, errlen, "CAN'T OPEN FILE");
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 44 || size > MAX_WAV_BYTES) {
        fclose(f);
        set_err(err, errlen, "BAD FILE SIZE");
        return -1;
    }
    uint8_t *buf = malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(buf);
        set_err(err, errlen, "READ ERROR");
        return -1;
    }
    fclose(f);

    int ret = -1;
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        set_err(err, errlen, "NOT A WAV FILE");
        goto done;
    }

    int format = 0, channels = 0, bits = 0, srate = 0;
    const uint8_t *data = NULL;
    uint32_t data_size = 0;
    long pos = 12;
    while (pos + 8 <= size) {
        const uint8_t *chunk = buf + pos;
        uint32_t csize = rd32(chunk + 4);
        long body = pos + 8;
        if (csize > (uint32_t)(size - body)) csize = (uint32_t)(size - body);
        if (!memcmp(chunk, "fmt ", 4) && csize >= 16) {
            format = rd16(buf + body);
            channels = rd16(buf + body + 2);
            srate = (int)rd32(buf + body + 4);
            bits = rd16(buf + body + 14);
            if (format == 0xFFFE && csize >= 26) format = rd16(buf + body + 24);
        } else if (!memcmp(chunk, "data", 4)) {
            data = buf + body;
            data_size = csize;
        }
        pos = body + csize + (csize & 1);
    }

    if (!data || channels < 1 || srate < 1000 || srate > 384000) {
        set_err(err, errlen, "MISSING FMT/DATA");
        goto done;
    }
    int is_float = format == 3 && bits == 32;
    if (!(format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) && !is_float) {
        set_err(err, errlen, "UNSUPPORTED WAV FORMAT");
        goto done;
    }

    int bytes = bits / 8;
    int frames = (int)(data_size / (uint32_t)(bytes * channels));
    if (frames > srate * MAX_SECONDS) frames = srate * MAX_SECONDS;
    if (frames < 1) {
        set_err(err, errlen, "EMPTY WAV");
        goto done;
    }
    float *mono = malloc(sizeof(float) * (size_t)frames);
    if (!mono) {
        set_err(err, errlen, "OUT OF MEMORY");
        goto done;
    }
    for (int i = 0; i < frames; i++) {
        float sum = 0.0f;
        for (int c = 0; c < channels; c++) {
            const uint8_t *p = data + ((size_t)i * channels + c) * bytes;
            float v;
            if (is_float) {
                uint32_t u = rd32(p);
                memcpy(&v, &u, sizeof v);
            } else if (bits == 8) {
                v = (p[0] - 128) / 128.0f;
            } else if (bits == 16) {
                v = (int16_t)rd16(p) / 32768.0f;
            } else if (bits == 24) {
                int32_t s = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8;
                v = s / 8388608.0f;
            } else {
                v = (int32_t)rd32(p) / 2147483648.0f;
            }
            sum += v;
        }
        mono[i] = sum / channels;
    }
    *out = mono;
    *len = frames;
    *rate = srate;
    ret = 0;
done:
    free(buf);
    return ret;
}

static void wr32(FILE *f, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, f);
}

static void wr16(FILE *f, uint16_t v)
{
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    fwrite(b, 1, 2, f);
}

int wav_save_stereo16(const char *path, const float *lr, int frames, int rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t data_size = (uint32_t)frames * 4;
    fwrite("RIFF", 1, 4, f);
    wr32(f, 36 + data_size);
    fwrite("WAVEfmt ", 1, 8, f);
    wr32(f, 16);
    wr16(f, 1);
    wr16(f, 2);
    wr32(f, (uint32_t)rate);
    wr32(f, (uint32_t)rate * 4);
    wr16(f, 4);
    wr16(f, 16);
    fwrite("data", 1, 4, f);
    wr32(f, data_size);
    for (int i = 0; i < frames * 2; i++) {
        float v = lr[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        wr16(f, (uint16_t)(int16_t)(v * 32767.0f));
    }
    int ok = ferror(f) == 0;
    fclose(f);
    return ok ? 0 : -1;
}
