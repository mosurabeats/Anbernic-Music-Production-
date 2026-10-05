/* Track parameter table shared by the UI and project files. */
#ifndef ARDKORE_PARAMS_H
#define ARDKORE_PARAMS_H

#include <stddef.h>

#include "engine.h"

enum {
    PF_REBAKE = 1,  /* changing it re-bakes the sample */
    PF_RESLICE = 2, /* changing it re-slices the sample */
};

typedef struct {
    const char *name; /* short label shown on screen */
    const char *key;  /* stable name used in project files */
    size_t offset;    /* into TrackParams */
    int min, max, def;
    int step, coarse;
    int flags;
    const char *const *labels; /* enum names, or NULL for numbers */
} ParamDef;

extern const ParamDef param_defs[];
extern const int param_count;

int *param_ptr(TrackParams *p, int index);
int param_get(const TrackParams *p, int index);

/* Write a display string for the parameter's current value (max 8 chars). */
void param_format(const TrackParams *p, int index, char *buf, size_t len);

int param_find(const char *key);

#endif
