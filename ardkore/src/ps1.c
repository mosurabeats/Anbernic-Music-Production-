#include "ps1.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Constants and formulas from the psx-spx hardware reference (nocash). */

/* SPU 4-point Gaussian interpolation table. */
const int16_t ps1_gauss[512] = {
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 2, 2, 2, 3, 3,
    3, 4, 4, 5, 5, 6, 7, 7,
    8, 9, 9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 21, 22, 24,
    25, 27, 28, 30, 32, 33, 35, 37,
    39, 41, 44, 46, 48, 51, 53, 56,
    58, 61, 64, 67, 70, 73, 77, 80,
    84, 87, 91, 95, 99, 103, 107, 111,
    116, 120, 125, 130, 135, 140, 145, 150,
    156, 161, 167, 173, 179, 186, 192, 199,
    205, 212, 219, 227, 234, 242, 250, 257,
    266, 274, 283, 291, 300, 309, 319, 328,
    338, 348, 358, 369, 379, 390, 401, 412,
    424, 436, 448, 460, 473, 485, 498, 512,
    525, 539, 553, 567, 582, 597, 612, 627,
    643, 659, 675, 692, 708, 726, 743, 761,
    779, 797, 816, 835, 854, 874, 894, 914,
    935, 956, 977, 999, 1020, 1043, 1066, 1089,
    1112, 1136, 1160, 1184, 1209, 1234, 1260, 1286,
    1312, 1339, 1366, 1394, 1422, 1450, 1479, 1508,
    1537, 1567, 1598, 1628, 1660, 1691, 1723, 1756,
    1789, 1822, 1856, 1890, 1924, 1959, 1995, 2031,
    2067, 2104, 2141, 2179, 2217, 2256, 2295, 2334,
    2374, 2415, 2456, 2497, 2539, 2582, 2624, 2668,
    2712, 2756, 2801, 2846, 2892, 2938, 2985, 3032,
    3079, 3128, 3176, 3225, 3275, 3325, 3376, 3427,
    3479, 3531, 3584, 3637, 3691, 3745, 3799, 3855,
    3910, 3967, 4023, 4081, 4138, 4197, 4255, 4315,
    4374, 4435, 4495, 4557, 4619, 4681, 4744, 4807,
    4871, 4935, 5000, 5065, 5131, 5197, 5264, 5332,
    5399, 5468, 5536, 5606, 5676, 5746, 5817, 5888,
    5959, 6032, 6104, 6177, 6251, 6325, 6400, 6475,
    6550, 6626, 6702, 6779, 6856, 6934, 7012, 7091,
    7170, 7249, 7329, 7409, 7490, 7571, 7653, 7735,
    7817, 7900, 7983, 8066, 8150, 8234, 8319, 8404,
    8489, 8575, 8661, 8748, 8834, 8922, 9009, 9097,
    9185, 9273, 9362, 9451, 9541, 9630, 9720, 9811,
    9901, 9992, 10083, 10174, 10266, 10358, 10450, 10542,
    10635, 10727, 10820, 10913, 11007, 11100, 11194, 11288,
    11382, 11476, 11571, 11665, 11760, 11855, 11950, 12045,
    12140, 12236, 12331, 12427, 12522, 12618, 12714, 12809,
    12905, 13001, 13097, 13193, 13289, 13385, 13481, 13577,
    13673, 13769, 13865, 13961, 14056, 14152, 14248, 14343,
    14439, 14534, 14630, 14725, 14820, 14915, 15010, 15104,
    15199, 15293, 15387, 15481, 15575, 15669, 15762, 15855,
    15948, 16041, 16133, 16226, 16317, 16409, 16500, 16592,
    16682, 16773, 16863, 16953, 17042, 17131, 17220, 17308,
    17396, 17484, 17571, 17658, 17744, 17830, 17916, 18001,
    18086, 18170, 18254, 18337, 18420, 18502, 18584, 18665,
    18746, 18826, 18905, 18985, 19063, 19141, 19219, 19295,
    19372, 19447, 19522, 19597, 19671, 19744, 19816, 19888,
    19959, 20030, 20100, 20169, 20238, 20306, 20373, 20439,
    20505, 20570, 20634, 20698, 20760, 20822, 20884, 20944,
    21004, 21063, 21121, 21178, 21235, 21290, 21345, 21399,
    21452, 21505, 21556, 21607, 21657, 21706, 21754, 21801,
    21848, 21893, 21938, 21982, 22025, 22066, 22107, 22148,
    22187, 22225, 22262, 22299, 22334, 22369, 22402, 22435,
    22467, 22498, 22527, 22556, 22584, 22611, 22637, 22662,
    22686, 22709, 22731, 22752, 22772, 22791, 22809, 22826,
    22842, 22857, 22872, 22885, 22897, 22908, 22918, 22927,
    22935, 22942, 22948, 22953, 22957, 22960, 22962, 22963,
};

/* ---- ADPCM ---------------------------------------------------------------- */

static const int pos_tab[5] = {0, 60, 115, 98, 122};
static const int neg_tab[5] = {0, 0, -52, -55, -60};

static int clamp16(int v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }

static int predict(int f, int old, int older) { return (old * pos_tab[f] + older * neg_tab[f] + 32) >> 6; }

size_t ps1_adpcm_size(int len) { return (size_t)((len + 27) / 28) * 16; }

/* Pick the filter and shift with the least error for 28 samples. Writes the
 * block and, if `recon` is given, the decoded samples. */
static void encode_block(const int16_t *x, int *old, int *older, uint8_t *blk, int16_t *recon)
{
    long long best_err = -1;
    int best_f = 0, best_sh = 12, best_old = 0, best_older = 0;
    uint8_t best_nib[28];
    int16_t best_rec[28];

    for (int f = 0; f < 5; f++) {
        int o = *old, oo = *older, maxr = 0;
        for (int j = 0; j < 28; j++) {
            int r = x[j] - predict(f, o, oo);
            if (r < 0) r = -r;
            if (r > maxr) maxr = r;
            oo = o;
            o = x[j];
        }
        int sh = 0;
        while (sh < 12 && (7 << sh) < maxr) sh++;
        for (int s = sh; s <= sh + 1 && s <= 12; s++) {
            uint8_t nib[28];
            int16_t rec[28];
            long long err = 0;
            o = *old;
            oo = *older;
            for (int j = 0; j < 28; j++) {
                int p = predict(f, o, oo);
                int t = (x[j] - p + ((1 << s) >> 1)) >> s;
                if (t < -8) t = -8;
                if (t > 7) t = 7;
                int v = clamp16(t * (1 << s) + p);
                long long d = x[j] - v;
                err += d * d;
                nib[j] = (uint8_t)(t & 15);
                rec[j] = (int16_t)v;
                oo = o;
                o = v;
            }
            if (best_err < 0 || err < best_err) {
                best_err = err;
                best_f = f;
                best_sh = s;
                best_old = o;
                best_older = oo;
                memcpy(best_nib, nib, sizeof nib);
                memcpy(best_rec, rec, sizeof rec);
            }
        }
    }
    memset(blk, 0, 16);
    blk[0] = (uint8_t)((best_f << 4) | (12 - best_sh));
    for (int j = 0; j < 28; j++) blk[2 + j / 2] |= (uint8_t)(best_nib[j] << (4 * (j & 1)));
    *old = best_old;
    *older = best_older;
    if (recon) memcpy(recon, best_rec, sizeof best_rec);
}

void ps1_adpcm_encode(const int16_t *pcm, int len, uint8_t *out, int loop_start)
{
    int nblocks = (len + 27) / 28, old = 0, older = 0;
    for (int b = 0; b < nblocks; b++) {
        int16_t x[28] = {0};
        for (int j = 0; j < 28 && b * 28 + j < len; j++) x[j] = pcm[b * 28 + j];
        uint8_t *blk = out + b * 16;
        encode_block(x, &old, &older, blk, NULL);
        if (loop_start >= 0 && b == loop_start / 28) blk[1] |= 4;
        if (b == nblocks - 1) blk[1] |= loop_start >= 0 ? 3 : 1;
    }
}

int ps1_adpcm_decode(const uint8_t *in, int nblocks, int16_t *out, int *loop_start)
{
    int old = 0, older = 0, n = 0;
    *loop_start = -1;
    for (int b = 0; b < nblocks; b++) {
        const uint8_t *blk = in + b * 16;
        int shift = blk[0] & 15, f = (blk[0] >> 4) & 7, flags = blk[1];
        if (shift > 12) shift = 9; /* hardware quirk */
        if (f > 4) f = 4;
        if ((flags & 4) && *loop_start < 0) *loop_start = n;
        for (int j = 0; j < 28; j++) {
            int t = (blk[2 + j / 2] >> (4 * (j & 1))) & 15;
            if (t >= 8) t -= 16;
            int s = clamp16(t * (1 << (12 - shift)) + predict(f, old, older));
            out[n++] = (int16_t)s;
            older = old;
            old = s;
        }
        if (flags & 1) {
            if (!(flags & 2)) *loop_start = -1; /* end + mute: one-shot */
            break;
        }
    }
    return n;
}

void ps1_adpcm_roundtrip(float *data, int len)
{
    int old = 0, older = 0;
    for (int b = 0; b * 28 < len; b++) {
        int16_t x[28] = {0}, rec[28];
        uint8_t blk[16];
        int count = len - b * 28 < 28 ? len - b * 28 : 28;
        for (int j = 0; j < count; j++) x[j] = (int16_t)clamp16((int)lrintf(data[b * 28 + j] * 32767.0f));
        encode_block(x, &old, &older, blk, rec);
        for (int j = 0; j < count; j++) data[b * 28 + j] = rec[j] / 32768.0f;
    }
}

double ps1_pitch_rate(double rate)
{
    long p = lrint(rate * 4096.0 / 44100.0);
    if (p < 1) p = 1;
    if (p > 0x3FFF) p = 0x3FFF;
    return p * 44100.0 / 4096.0;
}

/* ---- VAG files -------------------------------------------------------------- */

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

int ps1_vag_load(const char *path, float **out, int *len, int *rate, int *loop_start,
                 char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errlen, "CAN'T OPEN FILE");
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0x30 + 16 || size > 16L * 1024 * 1024) {
        fclose(f);
        snprintf(err, errlen, "BAD VAG SIZE");
        return -1;
    }
    uint8_t *buf = malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(buf);
        snprintf(err, errlen, "READ ERROR");
        return -1;
    }
    fclose(f);
    if (memcmp(buf, "VAGp", 4) != 0) {
        free(buf);
        snprintf(err, errlen, "NOT A VAG FILE");
        return -1;
    }
    long data = (long)be32(buf + 0x0C);
    if (data <= 0 || data > size - 0x30) data = size - 0x30;
    int r = (int)be32(buf + 0x10);
    int nblocks = (int)(data / 16);
    int16_t *pcm = malloc(sizeof(int16_t) * (size_t)nblocks * 28);
    float *mono = malloc(sizeof(float) * (size_t)nblocks * 28);
    if (!pcm || !mono || r < 1000 || r > 96000) {
        free(buf);
        free(pcm);
        free(mono);
        snprintf(err, errlen, r < 1000 || r > 96000 ? "BAD VAG RATE" : "OUT OF MEMORY");
        return -1;
    }
    int n = ps1_adpcm_decode(buf + 0x30, nblocks, pcm, loop_start);
    for (int i = 0; i < n; i++) mono[i] = pcm[i] / 32768.0f;
    free(buf);
    free(pcm);
    *out = mono;
    *len = n;
    *rate = r;
    return n > 0 ? 0 : -1;
}

int ps1_vag_save(const char *path, const int16_t *pcm, int len, int rate, int loop_start)
{
    size_t bytes = ps1_adpcm_size(len);
    uint8_t *data = calloc(1, bytes);
    if (!data) return -1;
    ps1_adpcm_encode(pcm, len, data, loop_start);
    uint8_t hdr[0x30] = {'V', 'A', 'G', 'p', 0, 0, 0, 0x20};
    uint32_t vals[2] = {(uint32_t)bytes, (uint32_t)rate};
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 4; i++) hdr[0x0C + k * 4 + i] = (uint8_t)(vals[k] >> (24 - 8 * i));
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(data);
        return -1;
    }
    fwrite(hdr, 1, sizeof hdr, f);
    fwrite(data, 1, bytes, f);
    free(data);
    return fclose(f) == 0 ? 0 : -1;
}

/* ---- Reverb ------------------------------------------------------------------ */

const char *const ps1_reverb_names[PS1_RVB_COUNT] = {
    "OFF", "ROOM", "STUDIO S", "STUDIO M", "STUDIO L", "HALL", "HALF ECHO", "SPACE ECHO", "CHAOS", "DELAY",
};

/* Work area sizes (bytes) and register sets rev00..rev1F, as published. */
static const int rvb_size[PS1_RVB_COUNT] = {
    0x10, 0x26C0, 0x1F40, 0x4840, 0x6FE0, 0xADE0, 0x3C00, 0xF6C0, 0x18040, 0x18040,
};

static const uint16_t rvb_regs[PS1_RVB_COUNT][32] = {
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
     0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
     0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
     0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0000, 0x0000},
    {0x007D, 0x005B, 0x6D80, 0x54B8, 0xBED0, 0x0000, 0x0000, 0xBA80,
     0x5800, 0x5300, 0x04D6, 0x0333, 0x03F0, 0x0227, 0x0374, 0x01EF,
     0x0334, 0x01B5, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
     0x0000, 0x0000, 0x01B4, 0x0136, 0x00B8, 0x005C, 0x8000, 0x8000},
    {0x0033, 0x0025, 0x70F0, 0x4FA8, 0xBCE0, 0x4410, 0xC0F0, 0x9C00,
     0x5280, 0x4EC0, 0x03E4, 0x031B, 0x03A4, 0x02AF, 0x0372, 0x0266,
     0x031C, 0x025D, 0x025C, 0x018E, 0x022F, 0x0135, 0x01D2, 0x00B7,
     0x018F, 0x00B5, 0x00B4, 0x0080, 0x004C, 0x0026, 0x8000, 0x8000},
    {0x00B1, 0x007F, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xB4C0,
     0x5280, 0x4EC0, 0x0904, 0x076B, 0x0824, 0x065F, 0x07A2, 0x0616,
     0x076C, 0x05ED, 0x05EC, 0x042E, 0x050F, 0x0305, 0x0462, 0x02B7,
     0x042F, 0x0265, 0x0264, 0x01B2, 0x0100, 0x0080, 0x8000, 0x8000},
    {0x00E3, 0x00A9, 0x6F60, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xA680,
     0x5680, 0x52C0, 0x0DFB, 0x0B58, 0x0D09, 0x0A3C, 0x0BD9, 0x0973,
     0x0B59, 0x08DA, 0x08D9, 0x05E9, 0x07EC, 0x04B0, 0x06EF, 0x03D2,
     0x05EA, 0x031D, 0x031C, 0x0238, 0x0154, 0x00AA, 0x8000, 0x8000},
    {0x01A5, 0x0139, 0x6000, 0x5000, 0x4C00, 0xB800, 0xBC00, 0xC000,
     0x6000, 0x5C00, 0x15BA, 0x11BB, 0x14C2, 0x10BD, 0x11BC, 0x0DC1,
     0x11C0, 0x0DC3, 0x0DC0, 0x09C1, 0x0BC4, 0x07C1, 0x0A00, 0x06CD,
     0x09C2, 0x05C1, 0x05C0, 0x041A, 0x0274, 0x013A, 0x8000, 0x8000},
    {0x0017, 0x0013, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0x8500,
     0x5F80, 0x54C0, 0x0371, 0x02AF, 0x02E5, 0x01DF, 0x02B0, 0x01D7,
     0x0358, 0x026A, 0x01D6, 0x011E, 0x012D, 0x00B1, 0x011F, 0x0059,
     0x01A0, 0x00E3, 0x0058, 0x0040, 0x0028, 0x0014, 0x8000, 0x8000},
    {0x033D, 0x0231, 0x7E00, 0x5000, 0xB400, 0xB000, 0x4C00, 0xB000,
     0x6000, 0x5400, 0x1ED6, 0x1A31, 0x1D14, 0x183B, 0x1BC2, 0x16B2,
     0x1A32, 0x15EF, 0x15EE, 0x1055, 0x1334, 0x0F2D, 0x11F6, 0x0C5D,
     0x1056, 0x0AE1, 0x0AE0, 0x07A2, 0x0464, 0x0232, 0x8000, 0x8000},
    {0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x8100,
     0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000,
     0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
     0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000},
    {0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x0000,
     0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000,
     0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
     0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000},
};

enum {
    dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL,
    vAPF1, vAPF2, mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2,
    dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4,
    dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN,
};

void ps1_reverb_init(Ps1Reverb *r, int preset)
{
    if (preset < 0 || preset >= PS1_RVB_COUNT) preset = PS1_RVB_OFF;
    r->preset = preset;
    for (int i = 0; i < 32; i++) r->regs[i] = (int16_t)rvb_regs[preset][i];
    r->n = rvb_size[preset] / 2;
    r->cur = 0;
    memset(r->buf, 0, sizeof r->buf);
    r->acc_l = r->acc_r = r->out_l = r->out_r = r->prev_l = r->prev_r = 0.0f;
    r->phase = 0;
}

/* Addresses are in 8-byte units (4 halfwords), relative to the moving
 * buffer position and wrapped within the work area. */
static int addr(const Ps1Reverb *r, int reg, int adjust)
{
    int a = (r->cur + (uint16_t)r->regs[reg] * 4 + adjust) % r->n;
    return a < 0 ? a + r->n : a;
}

static int rd(const Ps1Reverb *r, int reg, int adjust) { return r->buf[addr(r, reg, adjust)]; }
static void wr(Ps1Reverb *r, int reg, int v) { r->buf[addr(r, reg, 0)] = (int16_t)clamp16(v); }
static int mul(int a, int b) { return (int)(((long long)a * b) >> 15); }

static void reverb_step(Ps1Reverb *r, int in_l, int in_r, int *out_l, int *out_r)
{
    const int16_t *g = r->regs;
    int lin = mul(g[vLIN], in_l), rin = mul(g[vRIN], in_r);

    /* Same-side and cross reflections (IIR). */
    int ls = rd(r, mLSAME, -1), rs = rd(r, mRSAME, -1), ld = rd(r, mLDIFF, -1), rdf = rd(r, mRDIFF, -1);
    int nls = mul(lin + mul(rd(r, dLSAME, 0), g[vWALL]) - ls, g[vIIR]) + ls;
    int nrs = mul(rin + mul(rd(r, dRSAME, 0), g[vWALL]) - rs, g[vIIR]) + rs;
    int nld = mul(lin + mul(rd(r, dRDIFF, 0), g[vWALL]) - ld, g[vIIR]) + ld;
    int nrd = mul(rin + mul(rd(r, dLDIFF, 0), g[vWALL]) - rdf, g[vIIR]) + rdf;
    wr(r, mLSAME, nls);
    wr(r, mRSAME, nrs);
    wr(r, mLDIFF, nld);
    wr(r, mRDIFF, nrd);

    /* Early echo: four combs. */
    int lo = mul(g[vCOMB1], rd(r, mLCOMB1, 0)) + mul(g[vCOMB2], rd(r, mLCOMB2, 0)) +
             mul(g[vCOMB3], rd(r, mLCOMB3, 0)) + mul(g[vCOMB4], rd(r, mLCOMB4, 0));
    int ro = mul(g[vCOMB1], rd(r, mRCOMB1, 0)) + mul(g[vCOMB2], rd(r, mRCOMB2, 0)) +
             mul(g[vCOMB3], rd(r, mRCOMB3, 0)) + mul(g[vCOMB4], rd(r, mRCOMB4, 0));

    /* Late reverb: two all-pass stages. */
    int d1 = -(uint16_t)g[dAPF1] * 4, d2 = -(uint16_t)g[dAPF2] * 4;
    int t = rd(r, mLAPF1, d1);
    lo = clamp16(lo - mul(g[vAPF1], t));
    wr(r, mLAPF1, lo);
    lo = mul(lo, g[vAPF1]) + t;
    t = rd(r, mRAPF1, d1);
    ro = clamp16(ro - mul(g[vAPF1], t));
    wr(r, mRAPF1, ro);
    ro = mul(ro, g[vAPF1]) + t;
    t = rd(r, mLAPF2, d2);
    lo = clamp16(lo - mul(g[vAPF2], t));
    wr(r, mLAPF2, lo);
    lo = mul(lo, g[vAPF2]) + t;
    t = rd(r, mRAPF2, d2);
    ro = clamp16(ro - mul(g[vAPF2], t));
    wr(r, mRAPF2, ro);
    ro = mul(ro, g[vAPF2]) + t;

    *out_l = clamp16(lo);
    *out_r = clamp16(ro);
    r->cur = (r->cur + 1) % r->n;
}

void ps1_reverb_run(Ps1Reverb *r, float in_l, float in_r, float *out_l, float *out_r)
{
    if (r->preset == PS1_RVB_OFF) {
        *out_l = *out_r = 0.0f;
        return;
    }
    r->acc_l += in_l;
    r->acc_r += in_r;
    if (r->phase) {
        int l, rr;
        reverb_step(r, clamp16((int)lrintf(r->acc_l * 0.5f * 32767.0f)),
                    clamp16((int)lrintf(r->acc_r * 0.5f * 32767.0f)), &l, &rr);
        r->prev_l = r->out_l;
        r->prev_r = r->out_r;
        r->out_l = l / 32768.0f;
        r->out_r = rr / 32768.0f;
        r->acc_l = r->acc_r = 0.0f;
        *out_l = (r->prev_l + r->out_l) * 0.5f;
        *out_r = (r->prev_r + r->out_r) * 0.5f;
    } else {
        *out_l = r->out_l;
        *out_r = r->out_r;
    }
    r->phase ^= 1;
}
