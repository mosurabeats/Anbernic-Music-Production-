/* Headless tests for the ARDKORE engine, project files and UI logic. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "demo.h"
#include "engine.h"
#include "machine.h"
#include "params.h"
#include "project.h"
#include "sample.h"
#include "ui.h"
#include "wav.h"

static int failures, checks;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        checks++;                                           \
        if (!(cond)) {                                      \
            failures++;                                     \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

static Engine eng;
static Ui ui;

static float *sine(int len, int rate, double freq)
{
    float *b = malloc(sizeof(float) * (size_t)len);
    for (int i = 0; i < len; i++) b[i] = (float)(0.8 * sin(2 * M_PI * freq * i / rate));
    return b;
}

static void test_protracker_rates(void)
{
    CHECK((int)(pt_rate(PT_DEFAULT_PERIOD) + 0.5) == 22168, "F-3 should be 22168 Hz, got %f", pt_rate(PT_DEFAULT_PERIOD));
    CHECK((int)(pt_rate(0) + 0.5) == 4144, "C-1 should be 4144 Hz, got %f", pt_rate(0));
    CHECK(fabs(pt_rate(PT_NUM_PERIODS - 1) - 33148.6) < 1.0, "C-4 should be ~33 kHz");
    /* An octave up from period 160 is period 80. */
    double up = machine_play_rate(MACH_AMIGA, PT_DEFAULT_PERIOD, 12);
    CHECK(fabs(up - PT_PAL_CLOCK / 80) < 0.01, "Amiga octave transposition %f", up);
    double sp = machine_play_rate(MACH_SP1200, 0, 0);
    CHECK(fabs(sp - 26040) < 0.01, "SP-1200 native rate %f", sp);
}

static void test_bake_and_quantise(void)
{
    Sample s = {0};
    sample_set_source(&s, sine(44100, 44100, 220), 44100, 44100, "SINE");
    CHECK(sample_bake(&s, pt_rate(PT_DEFAULT_PERIOD), 8, 0, 0) == 0, "bake failed");
    CHECK(abs(s.len - 22168) <= 1, "baked length %d", s.len);

    int levels_seen[256] = {0}, distinct = 0;
    int on_grid = 1;
    for (int i = 0; i < s.len; i++) {
        float q = s.data[i] * 127.0f;
        if (fabsf(q - roundf(q)) > 1e-3f) on_grid = 0;
        int l = (int)roundf(q) + 128;
        if (l >= 0 && l < 256 && !levels_seen[l]++) distinct++;
    }
    CHECK(on_grid, "8-bit bake should land on 1/127 steps");
    CHECK(distinct <= 255 && distinct > 150, "distinct 8-bit levels %d", distinct);

    float a = sample_quantise(0.001f, 12, 1), b = sample_quantise(0.001f, 12, 0);
    CHECK(fabsf(a - 0.001f) <= fabsf(b - 0.001f) + 1e-6f, "companding should be finer near zero");
    sample_free(&s);
}

static void test_slicing(void)
{
    Sample s = {0};
    sample_set_source(&s, sine(44100, 44100, 110), 44100, 44100, "SINE");
    sample_bake(&s, 44100, 16, 0, 0);
    sample_slice_equal(&s, 16, 1);
    CHECK(s.nslices == 16, "equal slices %d", s.nslices);
    int mono = 1;
    for (int i = 0; i < s.nslices; i++)
        if (s.slice[i + 1] <= s.slice[i]) mono = 0;
    CHECK(mono && s.slice[0] == 0 && s.slice[s.nslices] == s.len, "slice bounds must increase and cover the sample");
    sample_free(&s);

    /* Eight bursts in silence should give eight slices. */
    int len = 44100 * 2;
    float *b = calloc((size_t)len, sizeof(float));
    for (int k = 0; k < 8; k++)
        for (int i = 0; i < 2000; i++)
            b[k * len / 8 + i] = (float)(0.9 * exp(-i / 400.0) * sin(i * 0.3));
    sample_set_source(&s, b, len, 44100, "CLICKS");
    sample_bake(&s, 44100, 16, 0, 0);
    sample_slice_auto(&s, 16, 0);
    CHECK(s.nslices == 8, "auto slices on 8 bursts: %d", s.nslices);
    sample_free(&s);

    int blen;
    float *brk = demo_break(&blen);
    sample_set_source(&s, brk, blen, DEMO_RATE, "BREAK");
    sample_bake(&s, 22168, 8, 0, 0);
    sample_slice_auto(&s, 16, 1);
    CHECK(s.nslices >= 6 && s.nslices <= 16, "auto slices on demo break: %d", s.nslices);
    sample_free(&s);
}

static void test_wav_roundtrip(void)
{
    const char *path = "/tmp/ardkore_test.wav";
    float lr[2000];
    for (int i = 0; i < 1000; i++) lr[2 * i] = lr[2 * i + 1] = (float)sin(i * 0.05) * 0.5f;
    CHECK(wav_save_stereo16(path, lr, 1000, 22050) == 0, "wav save");
    float *mono;
    int len, rate;
    char err[64];
    CHECK(wav_load_mono(path, &mono, &len, &rate, err, sizeof err) == 0, "wav load: %s", err);
    CHECK(len == 1000 && rate == 22050, "wav header len %d rate %d", len, rate);
    float maxerr = 0;
    for (int i = 0; i < len; i++) maxerr = fmaxf(maxerr, fabsf(mono[i] - lr[2 * i]));
    CHECK(maxerr < 1e-3f, "wav roundtrip error %f", maxerr);
    free(mono);
    CHECK(wav_load_mono("/nonexistent.wav", &mono, &len, &rate, err, sizeof err) != 0, "missing file must fail");
    unlink(path);
}

static int frames_until_silent(Engine *e, int track, int max)
{
    float buf[2 * 64];
    int n = 0;
    while (e->tr[track].v.active && n < max) {
        engine_render(e, buf, 64);
        n += 64;
    }
    return n;
}

static void test_voice_and_stretch(void)
{
    engine_init(&eng, 44100);
    engine_track_load(&eng, 0, sine(44100, 44100, 220), 44100, 44100, "SINE", "test");
    eng.tr[0].p.slices = 4;
    engine_track_reslice(&eng, 0);

    engine_trigger(&eng, 0, 1, 64);
    int plain = frames_until_silent(&eng, 0, 44100 * 4);
    CHECK(abs(plain - 11025) < 200, "quarter-second slice played %d frames", plain);

    eng.tr[0].p.stretch = 200;
    engine_trigger(&eng, 0, 1, 64);
    int stretched = frames_until_silent(&eng, 0, 44100 * 4);
    CHECK(fabs((double)stretched / plain - 2.0) < 0.05, "200%% stretch ratio %f", (double)stretched / plain);

    eng.tr[0].p.stretch = 50;
    engine_trigger(&eng, 0, 1, 64);
    int squashed = frames_until_silent(&eng, 0, 44100 * 4);
    CHECK(fabs((double)squashed / plain - 0.5) < 0.05, "50%% stretch ratio %f", (double)squashed / plain);

    eng.tr[0].p.stretch = 100;
    eng.tr[0].p.rev = 1;
    engine_trigger(&eng, 0, 1, 64);
    int rev = frames_until_silent(&eng, 0, 44100 * 4);
    CHECK(abs(rev - plain) < 200, "reverse slice length %d vs %d", rev, plain);

    eng.tr[0].p.rev = 0;
    eng.tr[0].p.trig = TRIG_THRU;
    engine_trigger(&eng, 0, 1, 64);
    int thru = frames_until_silent(&eng, 0, 44100 * 4);
    CHECK(abs(thru - 3 * plain) < 300, "THRU plays to the end: %d", thru);

    engine_track_clear(&eng, 0);
    float buf[128];
    engine_trigger(&eng, 0, 0, 64); /* no sample: must be a no-op */
    engine_render(&eng, buf, 64);
    CHECK(!eng.tr[0].v.active, "empty track should not play");
    engine_free(&eng);
}

static void test_sequencer_timing(void)
{
    engine_init(&eng, 44100);
    eng.bpm = 120; /* 16th = 5512.5 frames */
    engine_play(&eng, 1);
    float *buf = malloc(sizeof(float) * 2 * 22051);
    engine_render(&eng, buf, 22051); /* just past four steps */
    CHECK(eng.step == 4, "after 4 steps at 120 BPM, step=%d", eng.step);

    eng.swing = 66;
    engine_play(&eng, 1);
    double l0 = eng.step_len;
    engine_render(&eng, buf, (int)l0 + 2);
    double l1 = eng.step_len;
    CHECK(fabs(l0 + l1 - 11025.0) < 1e-6 && l0 > l1, "swing keeps the pair length: %f + %f", l0, l1);
    free(buf);
    engine_free(&eng);
}

static void test_demo_render(void)
{
    engine_init(&eng, 44100);
    project_default(&eng);
    CHECK(eng.tr[0].smp.nslices == 16, "demo break slices %d", eng.tr[0].smp.nslices);
    int frames = 44100 * 3;
    float *buf = malloc(sizeof(float) * 2 * (size_t)frames);
    engine_play(&eng, 1);
    engine_render(&eng, buf, frames);
    double energy = 0;
    int bad = 0;
    for (int i = 0; i < frames * 2; i++) {
        if (!isfinite(buf[i]) || fabsf(buf[i]) > 1.0f) bad++;
        energy += buf[i] * buf[i];
    }
    CHECK(bad == 0, "%d invalid output samples", bad);
    CHECK(energy / (frames * 2) > 1e-3, "demo should be audible, rms^2=%g", energy / (frames * 2));

    for (int m = 0; m < MACH_COUNT; m++) {
        eng.tr[0].p.machine = m;
        engine_track_rebake(&eng, 0);
        engine_render(&eng, buf, 4096);
        bad = 0;
        for (int i = 0; i < 4096 * 2; i++) if (!isfinite(buf[i])) bad++;
        CHECK(bad == 0 && eng.tr[0].smp.data, "machine %s renders", machines[m].name);
    }
    free(buf);
    engine_free(&eng);
}

static void test_project_roundtrip(void)
{
    const char *path = "/tmp/ardkore_test.prj";
    engine_init(&eng, 44100);
    project_default(&eng);
    eng.bpm = 133;
    eng.tr[0].p.lpf = 77;
    eng.tr[0].steps[3].roll = 2;
    eng.tr[2].p.machine = MACH_SP1200;
    snprintf(eng.tr[3].src_id, SRC_ID_LEN, "/missing/file.wav");
    CHECK(project_save(&eng, path) == 0, "project save");

    Engine *e2 = malloc(sizeof(Engine));
    engine_init(e2, 44100);
    char err[64];
    int r = project_load(e2, path, err, sizeof err);
    CHECK(r == 1, "missing sample should be reported, got %d", r);
    CHECK(!strcmp(e2->tr[3].src_id, "/missing/file.wav"), "missing sample path kept");
    CHECK(e2->bpm == 133 && e2->tr[0].p.lpf == 77 && e2->tr[0].steps[3].roll == 2, "values restored");
    CHECK(e2->tr[2].p.machine == MACH_SP1200, "machine restored");
    CHECK(e2->tr[0].smp.data && e2->tr[1].smp.data, "demo samples reloaded");
    CHECK(!memcmp(e2->tr[0].steps, eng.tr[0].steps, sizeof eng.tr[0].steps), "steps restored");
    engine_free(e2);
    free(e2);
    engine_free(&eng);
    unlink(path);
    CHECK(project_load(&eng, "/nonexistent.prj", err, sizeof err) == -1, "missing project fails");
}

static void press(int b)
{
    ui_button(&ui, b, 1);
    ui_button(&ui, b, 0);
}

static void test_ui(void)
{
    engine_init(&eng, 44100);
    project_default(&eng);
    ui_init(&ui, &eng, "/tmp", "/tmp/ardkore_ui_test.prj");

    /* Toggle a step on track 3 (empty) and back off. */
    press(BTN_DOWN);
    press(BTN_DOWN);
    CHECK(ui.track == 2, "dpad down moves track, at %d", ui.track);
    press(BTN_A);
    CHECK(eng.tr[2].steps[0].on, "A turns a step on");
    press(BTN_A);
    CHECK(!eng.tr[2].steps[0].on, "A again turns it off");

    /* A + right edits the slice of an existing step instead of toggling. */
    ui.track = 0;
    ui.seq_step = 2;
    ui_button(&ui, BTN_A, 1);
    press(BTN_RIGHT);
    ui_button(&ui, BTN_A, 0);
    CHECK(eng.tr[0].steps[2].on && eng.tr[0].steps[2].val == 3, "A+right: val %d", eng.tr[0].steps[2].val);
    press(BTN_X);
    CHECK(eng.tr[0].steps[2].roll == 1, "X cycles roll");

    /* SAMPLE page: change slice count, cycle an enum. */
    press(BTN_R1);
    CHECK(ui.page == PAGE_SAMPLE, "R1 goes to the sample page");
    ui.prm = param_find("slices");
    ui_button(&ui, BTN_A, 1);
    press(BTN_DOWN); /* coarse -4 */
    ui_button(&ui, BTN_A, 0);
    CHECK(eng.tr[0].p.slices == 12 && eng.tr[0].smp.nslices == 12, "slices now %d", eng.tr[0].smp.nslices);
    ui.prm = param_find("machine");
    press(BTN_A);
    CHECK(eng.tr[0].p.machine == MACH_SP1200 && fabs(eng.tr[0].smp.rate - 26040) < 1, "tap A cycles machine and rebakes");
    press(BTN_Y);
    CHECK(ui.sel_slice[0] == 1, "Y selects the next slice");

    /* Every page draws without crashing. */
    for (int p = 0; p < PAGE_COUNT; p++) {
        ui.page = p;
        ui_draw(&ui);
    }
    ui.page = PAGE_FILES;
    CHECK(ui.nfiles >= 2 && ui.files[0].kind == 3, "file list starts with demos");

    /* Held direction repeats. */
    ui.page = PAGE_SEQ;
    ui.seq_step = 0;
    ui_button(&ui, BTN_RIGHT, 1);
    ui_tick(&ui, 280 + 55 * 3);
    ui_button(&ui, BTN_RIGHT, 0);
    CHECK(ui.seq_step == 4, "key repeat moved to step %d", ui.seq_step);
    engine_free(&eng);
}

int main(void)
{
    test_protracker_rates();
    test_bake_and_quantise();
    test_slicing();
    test_wav_roundtrip();
    test_voice_and_stretch();
    test_sequencer_timing();
    test_demo_render();
    test_project_roundtrip();
    test_ui();
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
