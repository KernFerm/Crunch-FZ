/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include "crunch_core.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct CrunchExternal CrunchExternal;

typedef struct {
    bool active;
    bool connected;
    bool running;
    uint32_t protocol_version;
    uint32_t serial_errors;
    uint64_t lines;
    uint64_t bytes;
    uint64_t elapsed_ms;
    int32_t exit_code;
    char bridge_version[24];
    char crunch_version[32];
    char state[16];
    char output_name[64];
    char error[64];
} CrunchExternalSnapshot;

CrunchExternal* crunch_external_alloc(void);
void crunch_external_free(CrunchExternal* external);
bool crunch_external_start(CrunchExternal* external, uint32_t baudrate);
void crunch_external_stop(CrunchExternal* external);
bool crunch_external_run(
    CrunchExternal* external,
    const CrunchConfig* config,
    const char* output_filename);
bool crunch_external_cancel(CrunchExternal* external);
void crunch_external_snapshot(CrunchExternal* external, CrunchExternalSnapshot* snapshot);

