#pragma once

#include "config.h"

// Runs the full streaming cycle (blocking until exit).
// Returns 0 if the stream closed cleanly.
int stream_run(app_config_t *cfg, const char *config_dir);

int stream_take_catalog_update(void);
