/* ARDKORE: an 8-bit slice sampler groovebox for Anbernic handhelds. */
#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "project.h"
#include "ui.h"
#include "wav.h"

#define SAMPLE_RATE 44100
#define DEFAULT_AUDIO_FRAMES 1024
#define MAX_RAW_BUTTONS 32
#define MAX_RAW_STICKS 4
#define PANIC_BUTTONS 3
#define PANIC_MS 2000

static Engine engine;
static Ui ui;

static void usage(void)
{
    printf("usage: ardkore [options]\n"
           "  --samples DIR       folder to browse for .wav files (default ./samples)\n"
           "  --project FILE      project file (default ./ardkore.prj)\n"
           "  --fullscreen        fill the screen (use on the handheld)\n"
           "  --render OUT.wav    render the project offline and exit\n"
           "  --seconds N         length for --render (default 8)\n"
           "  --screenshot OUT    write the UI as a PPM image and exit\n"
           "  --page N            page for --screenshot (0 SEQ, 1 SMP, 2 FIL, 3 PRJ)\n"
           "environment:\n"
           "  ARDKORE_SWAP_AB=1        use positional (Xbox) face buttons\n"
           "  ARDKORE_AUDIO_FRAMES=N   audio buffer size (default %d)\n",
           DEFAULT_AUDIO_FRAMES);
}

static void load_project(const char *path)
{
    char err[64];
    int r = project_load(&engine, path, err, sizeof err);
    if (r < 0) project_default(&engine);
    else if (r > 0) fprintf(stderr, "ardkore: %s\n", err);
}

static int render_offline(const char *out, double seconds)
{
    int frames = (int)(seconds * engine.sr);
    float *buf = malloc(sizeof(float) * 2 * (size_t)frames);
    if (!buf) return 1;
    engine_play(&engine, 1);
    for (int done = 0; done < frames; done += 512) {
        int n = frames - done < 512 ? frames - done : 512;
        engine_render(&engine, buf + 2 * done, n);
    }
    int r = wav_save_stereo16(out, buf, frames, engine.sr);
    free(buf);
    if (r != 0) {
        fprintf(stderr, "ardkore: can't write %s\n", out);
        return 1;
    }
    printf("rendered %.1fs to %s\n", seconds, out);
    return 0;
}

static int screenshot(const char *out, int page)
{
    float buf[2 * 512];
    engine_play(&engine, 1);
    for (int i = 0; i < 20; i++) engine_render(&engine, buf, 512); /* move the playhead */
    ui.page = page >= 0 && page < PAGE_COUNT ? page : 0;
    ui_draw(&ui);
    if (ui_save_ppm(&ui, out) != 0) {
        fprintf(stderr, "ardkore: can't write %s\n", out);
        return 1;
    }
    return 0;
}

/* ---- SDL glue ----------------------------------------------------------- */

static void audio_cb(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    engine_render(&engine, (float *)stream, len / (int)(2 * sizeof(float)));
}

static void lock_audio(void *ctx) { SDL_LockAudioDevice(*(SDL_AudioDeviceID *)ctx); }
static void unlock_audio(void *ctx) { SDL_UnlockAudioDevice(*(SDL_AudioDeviceID *)ctx); }

static int map_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP: return BTN_UP;
    case SDLK_DOWN: return BTN_DOWN;
    case SDLK_LEFT: return BTN_LEFT;
    case SDLK_RIGHT: return BTN_RIGHT;
    case SDLK_x: return BTN_A;
    case SDLK_z: return BTN_B;
    case SDLK_s: return BTN_X;
    case SDLK_a: return BTN_Y;
    case SDLK_q: return BTN_L1;
    case SDLK_w: return BTN_R1;
    case SDLK_1: return BTN_L2;
    case SDLK_2: return BTN_R2;
    case SDLK_RETURN: return BTN_START;
    case SDLK_RSHIFT:
    case SDLK_BACKSPACE: return BTN_SELECT;
    case SDLK_ESCAPE: return BTN_MENU;
    default: return -1;
    }
}

/* Anbernic pads are Nintendo-labelled (A on the right), while SDL names
 * buttons by Xbox position (A at the bottom). By default we follow the
 * printed labels; ARDKORE_SWAP_AB=1 flips to positional. */
static int map_pad_button(int b, int swap)
{
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return BTN_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return BTN_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return BTN_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return BTN_RIGHT;
    case SDL_CONTROLLER_BUTTON_A: return swap ? BTN_A : BTN_B;
    case SDL_CONTROLLER_BUTTON_B: return swap ? BTN_B : BTN_A;
    case SDL_CONTROLLER_BUTTON_X: return swap ? BTN_X : BTN_Y;
    case SDL_CONTROLLER_BUTTON_Y: return swap ? BTN_Y : BTN_X;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return BTN_L1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return BTN_R1;
    case SDL_CONTROLLER_BUTTON_START: return BTN_START;
    case SDL_CONTROLLER_BUTTON_BACK: return BTN_SELECT;
    case SDL_CONTROLLER_BUTTON_GUIDE: return BTN_MENU;
    default: return -1;
    }
}

/* Joysticks SDL doesn't recognise as game controllers are read raw. The
 * default layout assumes evdev order (south, east, north, west, L1, R1, L2,
 * R2, select, start, menu); controls.txt ("a=1" per line) overrides it. */
static int raw_map[MAX_RAW_BUTTONS] = {
    BTN_B, BTN_A, BTN_X, BTN_Y, BTN_L1, BTN_R1, BTN_L2, BTN_R2, BTN_SELECT, BTN_START, BTN_MENU,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};
static SDL_JoystickID raw_ids[MAX_RAW_STICKS];
static int raw_count;
static int raw_axis_state[2]; /* -1, 0, +1 for the x and y axes */

static void load_raw_map(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char name[32];
        int idx;
        if (line[0] == '#' || sscanf(line, " %31[^= ] = %d", name, &idx) != 2) continue;
        if (idx < 0 || idx >= MAX_RAW_BUTTONS) continue;
        for (char *c = name; *c; c++) *c = (char)toupper((unsigned char)*c);
        for (int b = 0; b < BTN_COUNT; b++)
            if (!strcmp(name, ui_button_names[b])) {
                for (int i = 0; i < MAX_RAW_BUTTONS; i++)
                    if (raw_map[i] == b) raw_map[i] = -1;
                raw_map[idx] = b;
            }
    }
    fclose(f);
    printf("ardkore: loaded raw button map from %s\n", path);
}

static int is_raw(SDL_JoystickID id)
{
    for (int i = 0; i < raw_count; i++)
        if (raw_ids[i] == id) return 1;
    return 0;
}

static void open_joystick(int index)
{
    if (SDL_IsGameController(index)) {
        SDL_GameController *gc = SDL_GameControllerOpen(index);
        const char *name = gc ? SDL_GameControllerName(gc) : "?";
        printf("ardkore: pad %d '%s' (game controller)\n", index, name ? name : "?");
        snprintf(ui.info[1], sizeof ui.info[1], "PAD  %.40s", name ? name : "?");
        return;
    }
    SDL_Joystick *js = SDL_JoystickOpen(index);
    if (!js || raw_count >= MAX_RAW_STICKS) return;
    char guid[64];
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(js), guid, sizeof guid);
    printf("ardkore: pad %d '%s' (raw: %d buttons, %d hats, %d axes, guid %s)\n", index,
           SDL_JoystickName(js), SDL_JoystickNumButtons(js), SDL_JoystickNumHats(js),
           SDL_JoystickNumAxes(js), guid);
    snprintf(ui.info[1], sizeof ui.info[1], "RAW  %.40s", SDL_JoystickName(js));
    raw_ids[raw_count++] = SDL_JoystickInstanceID(js);
}

static void report_input(const char *source, int btn)
{
    snprintf(ui.info[0], sizeof ui.info[0], "INPUT  %s -> %s", source, btn >= 0 ? ui_button_names[btn] : "(UNMAPPED)");
}

static void raw_axis(int axis, int value)
{
    if (axis > 1) return;
    int dir = value > 16000 ? 1 : value < -16000 ? -1 : 0;
    int old = raw_axis_state[axis];
    if (dir == old) return;
    int neg = axis == 0 ? BTN_LEFT : BTN_UP, pos = axis == 0 ? BTN_RIGHT : BTN_DOWN;
    if (old) ui_button(&ui, old < 0 ? neg : pos, 0);
    if (dir) {
        ui_button(&ui, dir < 0 ? neg : pos, 1);
        char src[24];
        snprintf(src, sizeof src, "AXIS %d%c", axis, dir < 0 ? '-' : '+');
        report_input(src, dir < 0 ? neg : pos);
    }
    raw_axis_state[axis] = dir;
}

static void raw_hat(int value)
{
    static int prev;
    const struct { int mask, btn; } dirs[4] = {
        {SDL_HAT_UP, BTN_UP}, {SDL_HAT_DOWN, BTN_DOWN}, {SDL_HAT_LEFT, BTN_LEFT}, {SDL_HAT_RIGHT, BTN_RIGHT}};
    for (int i = 0; i < 4; i++) {
        int now = (value & dirs[i].mask) != 0, was = (prev & dirs[i].mask) != 0;
        if (now != was) ui_button(&ui, dirs[i].btn, now);
        if (now && !was) report_input("HAT", dirs[i].btn);
    }
    prev = value;
}

static void log_versions(void)
{
    SDL_version c, l;
    SDL_VERSION(&c);
    SDL_GetVersion(&l);
    printf("ardkore: SDL compiled %d.%d.%d, running %d.%d.%d\n", c.major, c.minor, c.patch, l.major, l.minor, l.patch);
}

int main(int argc, char **argv)
{
    const char *samples = "samples", *project = "ardkore.prj";
    const char *render_out = NULL, *shot_out = NULL;
    double seconds = 8.0;
    int fullscreen = 0, page = 0;

    setvbuf(stdout, NULL, _IOLBF, 0); /* keep log.txt useful if we crash */

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (!strcmp(a, "--samples") && more) samples = argv[++i];
        else if (!strcmp(a, "--project") && more) project = argv[++i];
        else if (!strcmp(a, "--render") && more) render_out = argv[++i];
        else if (!strcmp(a, "--seconds") && more) seconds = atof(argv[++i]);
        else if (!strcmp(a, "--screenshot") && more) shot_out = argv[++i];
        else if (!strcmp(a, "--page") && more) page = atoi(argv[++i]);
        else if (!strcmp(a, "--fullscreen")) fullscreen = 1;
        else {
            usage();
            return !strcmp(a, "--help") || !strcmp(a, "-h") ? 0 : 1;
        }
    }

    engine_init(&engine, SAMPLE_RATE);
    load_project(project);
    ui_init(&ui, &engine, samples, project);

    if (render_out) return render_offline(render_out, seconds);
    if (shot_out) return screenshot(shot_out, page);

    log_versions();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "ardkore: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
    load_raw_map("controls.txt");
    int swap = getenv("ARDKORE_SWAP_AB") && atoi(getenv("ARDKORE_SWAP_AB"));

    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(0, &mode) == 0)
        printf("ardkore: video driver %s, display %dx%d@%d\n", SDL_GetCurrentVideoDriver(), mode.w, mode.h, mode.refresh_rate);

    SDL_Window *win = SDL_CreateWindow("ARDKORE", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       SCREEN_W * 2, SCREEN_H * 2,
                                       fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (!win) {
        fprintf(stderr, "ardkore: window: %s\n", SDL_GetError());
        return 1;
    }
    if (fullscreen) SDL_ShowCursor(SDL_DISABLE);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        fprintf(stderr, "ardkore: renderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RendererInfo rinfo;
    if (SDL_GetRendererInfo(ren, &rinfo) == 0) printf("ardkore: renderer %s\n", rinfo.name);
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
    SDL_RenderSetIntegerScale(ren, SDL_TRUE);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                         SCREEN_W, SCREEN_H);

    static SDL_AudioDeviceID dev;
    SDL_AudioSpec want = {0}, have;
    int frames = getenv("ARDKORE_AUDIO_FRAMES") ? atoi(getenv("ARDKORE_AUDIO_FRAMES")) : DEFAULT_AUDIO_FRAMES;
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = (Uint16)(frames >= 128 && frames <= 8192 ? frames : DEFAULT_AUDIO_FRAMES);
    want.callback = audio_cb;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (dev) {
        engine.sr = have.freq;
        ui_set_lock(&ui, lock_audio, unlock_audio, &dev);
        SDL_PauseAudioDevice(dev, 0);
        printf("ardkore: audio driver %s, %d Hz, %d frames\n", SDL_GetCurrentAudioDriver(), have.freq, have.samples);
        snprintf(ui.info[2], sizeof ui.info[2], "%.10s %.10s  %dHZ/%d", SDL_GetCurrentVideoDriver(),
                 SDL_GetCurrentAudioDriver(), have.freq, have.samples);
    } else {
        fprintf(stderr, "ardkore: no audio: %s\n", SDL_GetError());
        snprintf(ui.info[2], sizeof ui.info[2], "NO AUDIO DEVICE");
    }

    snprintf(ui.info[0], sizeof ui.info[0], "INPUT  (PRESS A BUTTON)");
    snprintf(ui.info[1], sizeof ui.info[1], "PAD  NONE FOUND");
    printf("ardkore: %d joystick(s)\n", SDL_NumJoysticks()); /* opened via SDL_JOYDEVICEADDED */

    int l2 = 0, r2 = 0, panic_ms = 0;
    Uint32 last = SDL_GetTicks();
    while (!ui.quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            char src[32];
            int b;
            switch (ev.type) {
            case SDL_QUIT:
                ui.quit = 1;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                if (ev.key.repeat) break;
                b = map_key(ev.key.keysym.sym);
                if (ev.type == SDL_KEYDOWN) {
                    snprintf(src, sizeof src, "KEY %.16s", SDL_GetKeyName(ev.key.keysym.sym));
                    report_input(src, b);
                }
                ui_button(&ui, b, ev.type == SDL_KEYDOWN);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                b = map_pad_button(ev.cbutton.button, swap);
                if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
                    const char *n = SDL_GameControllerGetStringForButton(ev.cbutton.button);
                    snprintf(src, sizeof src, "PAD %.16s", n ? n : "?");
                    for (char *c = src; *c; c++) *c = (char)toupper((unsigned char)*c);
                    report_input(src, b);
                }
                ui_button(&ui, b, ev.type == SDL_CONTROLLERBUTTONDOWN);
                break;
            case SDL_CONTROLLERAXISMOTION: {
                int on = ev.caxis.value > 16000;
                if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT && on != l2) {
                    l2 = on;
                    if (on) report_input("PAD LEFTTRIGGER", BTN_L2);
                    ui_button(&ui, BTN_L2, on);
                } else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT && on != r2) {
                    r2 = on;
                    if (on) report_input("PAD RIGHTTRIGGER", BTN_R2);
                    ui_button(&ui, BTN_R2, on);
                }
                break;
            }
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
                if (!is_raw(ev.jbutton.which)) break;
                b = ev.jbutton.button < MAX_RAW_BUTTONS ? raw_map[ev.jbutton.button] : -1;
                if (ev.type == SDL_JOYBUTTONDOWN) {
                    snprintf(src, sizeof src, "JOY %d", ev.jbutton.button);
                    report_input(src, b);
                }
                ui_button(&ui, b, ev.type == SDL_JOYBUTTONDOWN);
                break;
            case SDL_JOYHATMOTION:
                if (is_raw(ev.jhat.which) && ev.jhat.hat == 0) raw_hat(ev.jhat.value);
                break;
            case SDL_JOYAXISMOTION:
                if (is_raw(ev.jaxis.which)) raw_axis(ev.jaxis.axis, ev.jaxis.value);
                break;
            case SDL_JOYDEVICEADDED: /* also sent for devices present at startup */
                open_joystick(ev.jdevice.which);
                break;
            }
        }

        Uint32 now = SDL_GetTicks();
        int dt = (int)(now - last);
        last = now;
        ui_tick(&ui, dt);

        /* Emergency exit, whatever the button mapping: hold any 3 buttons for 2 s. */
        int held = 0;
        for (int i = 0; i < BTN_COUNT; i++) held += ui.held[i];
        panic_ms = held >= PANIC_BUTTONS ? panic_ms + dt : 0;
        if (panic_ms >= PANIC_MS) ui.quit = 1;

        ui_draw(&ui);
        SDL_UpdateTexture(tex, NULL, ui.fb, SCREEN_W * (int)sizeof(uint32_t));
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        Uint32 spent = SDL_GetTicks() - now;
        if (spent < 16) SDL_Delay(16 - spent);
    }

    printf("ardkore: quitting, saving %s\n", project);
    if (project_save(&engine, project) != 0) fprintf(stderr, "ardkore: couldn't save %s\n", project);
    if (dev) SDL_CloseAudioDevice(dev);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    /* Release the display and audio, but skip SDL_Quit: SDL 2.0.14 (what
     * these firmwares ship) crashes in its joystick/udev teardown when udev
     * isn't fully available. Process exit closes the joystick devices. */
    SDL_QuitSubSystem(SDL_INIT_AUDIO | SDL_INIT_VIDEO);
    engine_free(&engine);
    return 0;
}
