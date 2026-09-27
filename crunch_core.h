/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CRUNCH_MAX_WORD 32U
#define CRUNCH_MAX_CHARSET 80U

typedef enum {
    CrunchCharsetNumeric,
    CrunchCharsetLowercase,
    CrunchCharsetUppercase,
    CrunchCharsetMixedCase,
    CrunchCharsetAlphanumeric,
    CrunchCharsetSymbols,
    CrunchCharsetCustom,
    CrunchCharsetCount,
} CrunchCharsetMode;

typedef enum {
    CrunchOk,
    CrunchErrorLength,
    CrunchErrorCharset,
    CrunchErrorPattern,
    CrunchErrorLiteralMask,
    CrunchErrorCharacter,
    CrunchErrorOverflow,
} CrunchError;

typedef struct {
    CrunchCharsetMode charset_mode;
    uint8_t minimum_length;
    uint8_t maximum_length;
    char custom_charset[CRUNCH_MAX_CHARSET + 1U];
    char pattern[CRUNCH_MAX_WORD + 1U];
    char literal_mask[CRUNCH_MAX_WORD + 1U];
} CrunchConfig;

typedef struct {
    const char* position_sets[CRUNCH_MAX_WORD];
    uint8_t position_lengths[CRUNCH_MAX_WORD];
    char fixed[CRUNCH_MAX_WORD];
    const char* main_charset;
    uint8_t main_charset_length;
    uint8_t minimum_length;
    uint8_t maximum_length;
    uint8_t pattern_length;
    bool pattern_mode;
    uint64_t total_lines;
    uint64_t total_bytes;
} CrunchPlan;

typedef enum {
    CrunchGenerateComplete,
    CrunchGenerateCancelled,
    CrunchGenerateWriteError,
} CrunchGenerateStatus;

typedef struct {
    CrunchGenerateStatus status;
    uint64_t generated_lines;
    uint64_t output_bytes;
} CrunchGenerateResult;

typedef bool (*CrunchEmitCallback)(void* context, const uint8_t* data, size_t length);
typedef bool (*CrunchCancelCallback)(void* context);
typedef void (*CrunchProgressCallback)(void* context, uint64_t lines, uint64_t bytes);

const char* crunch_charset_name(CrunchCharsetMode mode);
const char* crunch_charset_value(CrunchCharsetMode mode, const CrunchConfig* config);
const char* crunch_error_string(CrunchError error);
CrunchError crunch_plan_build(const CrunchConfig* config, CrunchPlan* plan);
CrunchGenerateResult crunch_generate(
    const CrunchPlan* plan,
    CrunchEmitCallback emit,
    CrunchCancelCallback cancel,
    CrunchProgressCallback progress,
    void* context);

