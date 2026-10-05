/* Plain-text project files: tempo, per-track parameters, sample sources, steps. */
#ifndef ARDKORE_PROJECT_H
#define ARDKORE_PROJECT_H

#include "engine.h"

int project_save(const Engine *e, const char *path);

/* Replaces the engine's tracks. Returns 0 on success, 1 if the project loaded
 * but some samples were missing (see err), -1 if the file couldn't be read. */
int project_load(Engine *e, const char *path, char *err, int errlen);

/* The starter project: demo break on track 1, demo pad on track 2. */
void project_default(Engine *e);

#endif
