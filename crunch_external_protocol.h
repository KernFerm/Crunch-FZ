/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CRUNCH_EXTERNAL_PROTOCOL_VERSION 1U
#define CRUNCH_EXTERNAL_LINE_MAX 512U

typedef enum {
    CrunchExternalMessageNone,
    CrunchExternalMessageInfo,
    CrunchExternalMessageStatus,
    CrunchExternalMessageError,
} CrunchExternalMessageType;

typedef struct {
    CrunchExternalMessageType type;
    uint32_t protocol_version;
    uint64_t lines;
    uint64_t bytes;
    uint64_t elapsed_ms;
    int32_t exit_code;
    char bridge_version[24];
    char crunch_version[32];
    char state[16];
    char output_name[64];
    char error[64];
} CrunchExternalMessage;

typedef struct {
    char line[CRUNCH_EXTERNAL_LINE_MAX];
    size_t length;
    bool overflow;
} CrunchExternalDecoder;

typedef void (*CrunchExternalMessageCallback)(
    const CrunchExternalMessage* message,
    void* context);

void crunch_external_decoder_reset(CrunchExternalDecoder* decoder);
void crunch_external_decoder_feed(
    CrunchExternalDecoder* decoder,
    const uint8_t* data,
    size_t length,
    CrunchExternalMessageCallback callback,
    void* context);
bool crunch_external_parse_line(const char* line, CrunchExternalMessage* message);

