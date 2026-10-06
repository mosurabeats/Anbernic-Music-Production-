/* Gamepad-driven UI rendered into a 320x240 framebuffer (shown 2x on the
 * RG35XX SP's 640x480 panel). No SDL: input arrives as abstract buttons. */
#ifndef ARDKORE_UI_H
#define ARDKORE_UI_H

#include <stdint.h>

#include "engine.h"

#define SCREEN_W 320
#define SCREEN_H 240
#define UI_PATH_LEN 512
#define UI_MAX_FILES 512

enum {
    BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
    BTN_A, BTN_B, BTN_X, BTN_Y,
    BTN_L1, BTN_R1, BTN_L2, BTN_R2,
    BTN_START, BTN_SELECT, BTN_MENU,
    BTN_COUNT
};

enum { PAGE_SEQ, PAGE_SAMPLE, PAGE_FILES, PAGE_PROJECT, PAGE_COUNT };

typedef struct {
    char name[64];
    int kind; /* 0 = wav, 1 = directory, 2 = parent, 3 = demo */
} FileEntry;

typedef struct {
    const float *data; /* sample the cache was built from */
    int len, width;
    int8_t lo[SCREEN_W], hi[SCREEN_W];
} WaveCache;

typedef struct {
    Engine *eng;
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
    void *lock_ctx;

    char browse_dir[UI_PATH_LEN];
    char project_path[UI_PATH_LEN];

    int page, track;
    int seq_step;
    int prm;
    int sel_slice[NUM_TRACKS];
    int last_note[NUM_TRACKS];

    FileEntry files[UI_MAX_FILES];
    int nfiles, file_cur, file_top;
    int auto_preview, preview_wait_ms; /* preview the file under the cursor once it rests */
    char preview_info[40];

    int proj_cur;
    int confirm_item, confirm_ms;

    int held[BTN_COUNT];
    int repeat_ms[BTN_COUNT];
    int a_edited, a_step_was_on;

    char status[64];
    int status_ms;
    char info[3][56]; /* device diagnostics shown on the PRJ page, filled by the platform layer */
    int quit;

    WaveCache wave[NUM_TRACKS];
    uint32_t fb[SCREEN_W * SCREEN_H];
} Ui;

extern const char *const ui_button_names[BTN_COUNT];

void ui_init(Ui *ui, Engine *eng, const char *samples_dir, const char *project_path);
void ui_set_lock(Ui *ui, void (*lock)(void *), void (*unlock)(void *), void *ctx);
void ui_button(Ui *ui, int btn, int down);
void ui_tick(Ui *ui, int dt_ms);
void ui_draw(Ui *ui);
void ui_status(Ui *ui, const char *msg);
int ui_save_ppm(const Ui *ui, const char *path);

#endif
