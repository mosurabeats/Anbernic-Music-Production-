/* ARDKORE: an 8-bit slice sampler groovebox for Anbernic handhelds. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "project.h"
#include "ui.h"
#include "wav.h"

#define SAMPLE_RATE 44100

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
           "  --page N            page for --screenshot (0 SEQ, 1 SMP, 2 FIL, 3 PRJ)\n");
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

int main(int argc, char **argv)
{
    const char *samples = "samples", *project = "ardkore.prj";
    const char *render_out = NULL, *shot_out = NULL;
    double seconds = 8.0;
    int fullscreen = 0, page = 0;

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

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "ardkore: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
    int swap = getenv("ARDKORE_SWAP_AB") && atoi(getenv("ARDKORE_SWAP_AB"));

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
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
    SDL_RenderSetIntegerScale(ren, SDL_TRUE);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                         SCREEN_W, SCREEN_H);

    static SDL_AudioDeviceID dev;
    SDL_AudioSpec want = {0}, have;
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = audio_cb;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (dev) {
        engine.sr = have.freq;
        ui_set_lock(&ui, lock_audio, unlock_audio, &dev);
        SDL_PauseAudioDevice(dev, 0);
    } else {
        fprintf(stderr, "ardkore: no audio: %s\n", SDL_GetError());
    }

    for (int j = 0; j < SDL_NumJoysticks(); j++)
        if (SDL_IsGameController(j)) SDL_GameControllerOpen(j);

    int l2 = 0, r2 = 0;
    Uint32 last = SDL_GetTicks();
    while (!ui.quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                ui.quit = 1;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                if (!ev.key.repeat) ui_button(&ui, map_key(ev.key.keysym.sym), ev.type == SDL_KEYDOWN);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                ui_button(&ui, map_pad_button(ev.cbutton.button, swap), ev.type == SDL_CONTROLLERBUTTONDOWN);
                break;
            case SDL_CONTROLLERAXISMOTION: {
                int on = ev.caxis.value > 16000;
                if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT && on != l2) {
                    l2 = on;
                    ui_button(&ui, BTN_L2, on);
                } else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT && on != r2) {
                    r2 = on;
                    ui_button(&ui, BTN_R2, on);
                }
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
                SDL_GameControllerOpen(ev.cdevice.which);
                break;
            }
        }

        Uint32 now = SDL_GetTicks();
        ui_tick(&ui, (int)(now - last));
        last = now;
        ui_draw(&ui);
        SDL_UpdateTexture(tex, NULL, ui.fb, SCREEN_W * (int)sizeof(uint32_t));
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        Uint32 spent = SDL_GetTicks() - now;
        if (spent < 16) SDL_Delay(16 - spent);
    }

    if (project_save(&engine, project) != 0) fprintf(stderr, "ardkore: couldn't save %s\n", project);
    if (dev) SDL_CloseAudioDevice(dev);
    SDL_Quit();
    engine_free(&engine);
    return 0;
}
