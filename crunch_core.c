/* SPDX-License-Identifier: GPL-2.0-only */
#include "crunch_core.h"

#include <limits.h>
#include <string.h>

/* Ordering is retained from upstream crunch.c at revision 3bdc4a8. */
static const char crunch_numeric[] = "0123456789";
static const char crunch_lowercase[] = "abcdefghijklmnopqrstuvwxyz";
static const char crunch_uppercase[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char crunch_mixed[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char crunch_alphanumeric[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const char crunch_symbols[] = "!@#$%^&*()-_+=~`[]{}|\\:;\"'<>,.?/ ";

static size_t crunch_bounded_length(const char* value, size_t limit) {
    size_t length = 0U;
    while(length < limit && value[length]) length++;
    return length;
}

static bool crunch_add_u64(uint64_t left, uint64_t right, uint64_t* output) {
    if(UINT64_MAX - left < right) return false;
    *output = left + right;
    return true;
}

static bool crunch_multiply_u64(uint64_t left, uint64_t right, uint64_t* output) {
    if(left && right > UINT64_MAX / left) return false;
    *output = left * right;
    return true;
}

static bool crunch_printable_ascii(const char* value, size_t maximum) {
    size_t length = crunch_bounded_length(value, maximum + 1U);
    if(length > maximum) return false;
    for(size_t index = 0; index < length; index++) {
        unsigned char character = (unsigned char)value[index];
        if(character < 0x20U || character > 0x7EU) return false;
    }
    return true;
}

const char* crunch_charset_name(CrunchCharsetMode mode) {
    switch(mode) {
    case CrunchCharsetNumeric: return "Numeric";
    case CrunchCharsetLowercase: return "Lowercase";
    case CrunchCharsetUppercase: return "Uppercase";
    case CrunchCharsetMixedCase: return "Mixed case";
    case CrunchCharsetAlphanumeric: return "Alphanumeric";
    case CrunchCharsetSymbols: return "Symbols";
    case CrunchCharsetCustom: return "Custom";
    default: return "Invalid";
    }
}

const char* crunch_charset_value(CrunchCharsetMode mode, const CrunchConfig* config) {
    switch(mode) {
    case CrunchCharsetNumeric: return crunch_numeric;
    case CrunchCharsetLowercase: return crunch_lowercase;
    case CrunchCharsetUppercase: return crunch_uppercase;
    case CrunchCharsetMixedCase: return crunch_mixed;
    case CrunchCharsetAlphanumeric: return crunch_alphanumeric;
    case CrunchCharsetSymbols: return crunch_symbols;
    case CrunchCharsetCustom: return config ? config->custom_charset : "";
    default: return "";
    }
}

const char* crunch_error_string(CrunchError error) {
    switch(error) {
    case CrunchOk: return "Configuration valid";
    case CrunchErrorLength: return "Lengths must be 1..32 and minimum <= maximum";
    case CrunchErrorCharset: return "Selected character set is empty or too long";
    case CrunchErrorPattern: return "Pattern must match both minimum and maximum length";
    case CrunchErrorLiteralMask: return "Literal mask must be blank or exactly pattern length";
    case CrunchErrorCharacter: return "Only printable ASCII is supported";
    case CrunchErrorOverflow: return "Exact line or byte count exceeds 64-bit range";
    default: return "Unknown configuration error";
    }
}

CrunchError crunch_plan_build(const CrunchConfig* config, CrunchPlan* plan) {
    if(!config || !plan) return CrunchErrorLength;
    memset(plan, 0, sizeof(*plan));
    if(config->minimum_length < 1U || config->maximum_length < config->minimum_length ||
       config->maximum_length > CRUNCH_MAX_WORD) {
        return CrunchErrorLength;
    }
    if(config->charset_mode >= CrunchCharsetCount) return CrunchErrorCharset;
    if(!crunch_printable_ascii(config->custom_charset, CRUNCH_MAX_CHARSET) ||
       !crunch_printable_ascii(config->pattern, CRUNCH_MAX_WORD) ||
       !crunch_printable_ascii(config->literal_mask, CRUNCH_MAX_WORD)) {
        return CrunchErrorCharacter;
    }

    const char* main_charset = crunch_charset_value(config->charset_mode, config);
    size_t main_length = crunch_bounded_length(main_charset, CRUNCH_MAX_CHARSET + 1U);
    if(main_length == 0U || main_length > CRUNCH_MAX_CHARSET) return CrunchErrorCharset;
    plan->main_charset = main_charset;
    plan->main_charset_length = (uint8_t)main_length;
    plan->minimum_length = config->minimum_length;
    plan->maximum_length = config->maximum_length;

    size_t pattern_length = crunch_bounded_length(config->pattern, CRUNCH_MAX_WORD + 1U);
    size_t mask_length = crunch_bounded_length(config->literal_mask, CRUNCH_MAX_WORD + 1U);
    if(pattern_length) {
        if(pattern_length > CRUNCH_MAX_WORD || pattern_length != config->minimum_length ||
           pattern_length != config->maximum_length) {
            return CrunchErrorPattern;
        }
        if(mask_length && mask_length != pattern_length) return CrunchErrorLiteralMask;
        plan->pattern_mode = true;
        plan->pattern_length = (uint8_t)pattern_length;
        for(size_t index = 0; index < pattern_length; index++) {
            char marker = config->pattern[index];
            bool literal = mask_length && config->literal_mask[index] == marker;
            const char* set = NULL;
            if(!literal) {
                if(marker == '@') set = main_charset;
                else if(marker == ',') set = crunch_uppercase;
                else if(marker == '%') set = crunch_numeric;
                else if(marker == '^') set = crunch_symbols;
            }
            if(set) {
                plan->position_sets[index] = set;
                plan->position_lengths[index] = (uint8_t)strlen(set);
            } else {
                plan->fixed[index] = marker;
            }
        }
    } else if(mask_length) {
        return CrunchErrorLiteralMask;
    }

    uint64_t lines = 0U;
    uint64_t bytes = 0U;
    if(plan->pattern_mode) {
        uint64_t pattern_lines = 1U;
        for(uint8_t index = 0; index < plan->pattern_length; index++) {
            uint64_t choices = plan->position_lengths[index] ? plan->position_lengths[index] : 1U;
            if(!crunch_multiply_u64(pattern_lines, choices, &pattern_lines)) return CrunchErrorOverflow;
        }
        uint64_t pattern_bytes;
        if(!crunch_multiply_u64(pattern_lines, (uint64_t)plan->pattern_length + 1U, &pattern_bytes))
            return CrunchErrorOverflow;
        lines = pattern_lines;
        bytes = pattern_bytes;
    } else {
        uint64_t length_lines = 1U;
        for(uint8_t length = 1U; length <= plan->maximum_length; length++) {
            if(!crunch_multiply_u64(length_lines, plan->main_charset_length, &length_lines))
                return CrunchErrorOverflow;
            if(length >= plan->minimum_length) {
                uint64_t length_bytes;
                if(!crunch_multiply_u64(length_lines, (uint64_t)length + 1U, &length_bytes) ||
                   !crunch_add_u64(lines, length_lines, &lines) ||
                   !crunch_add_u64(bytes, length_bytes, &bytes)) {
                    return CrunchErrorOverflow;
                }
            }
        }
    }
    plan->total_lines = lines;
    plan->total_bytes = bytes;
    return CrunchOk;
}

static void crunch_prepare_word(
    const CrunchPlan* plan,
    uint8_t length,
    const uint8_t* indexes,
    char* word) {
    for(uint8_t position = 0; position < length; position++) {
        const char* set = plan->pattern_mode ? plan->position_sets[position] : plan->main_charset;
        word[position] = set ? set[indexes[position]] : plan->fixed[position];
    }
    word[length] = '\n';
}

static bool crunch_increment(const CrunchPlan* plan, uint8_t length, uint8_t* indexes) {
    for(uint8_t reverse = 0; reverse < length; reverse++) {
        uint8_t position = (uint8_t)(length - reverse - 1U);
        uint8_t choices = plan->pattern_mode ? plan->position_lengths[position] : plan->main_charset_length;
        if(!choices) continue;
        indexes[position]++;
        if(indexes[position] < choices) return true;
        indexes[position] = 0U;
    }
    return false;
}

CrunchGenerateResult crunch_generate(
    const CrunchPlan* plan,
    CrunchEmitCallback emit,
    CrunchCancelCallback cancel,
    CrunchProgressCallback progress,
    void* context) {
    CrunchGenerateResult result = {.status = CrunchGenerateComplete};
    if(!plan || !emit) {
        result.status = CrunchGenerateWriteError;
        return result;
    }

    uint8_t first_length = plan->pattern_mode ? plan->pattern_length : plan->minimum_length;
    uint8_t last_length = plan->pattern_mode ? plan->pattern_length : plan->maximum_length;
    char line[CRUNCH_MAX_WORD + 1U];
    uint8_t indexes[CRUNCH_MAX_WORD] = {0};

    for(uint8_t length = first_length; length <= last_length; length++) {
        memset(indexes, 0, sizeof(indexes));
        bool more = true;
        while(more) {
            if(cancel && cancel(context)) {
                result.status = CrunchGenerateCancelled;
                if(progress) progress(context, result.generated_lines, result.output_bytes);
                return result;
            }
            crunch_prepare_word(plan, length, indexes, line);
            size_t line_length = (size_t)length + 1U;
            if(!emit(context, (const uint8_t*)line, line_length)) {
                result.status = CrunchGenerateWriteError;
                if(progress) progress(context, result.generated_lines, result.output_bytes);
                return result;
            }
            result.generated_lines++;
            result.output_bytes += line_length;
            if(progress && ((result.generated_lines & 0xFFU) == 0U ||
                            result.generated_lines == plan->total_lines)) {
                progress(context, result.generated_lines, result.output_bytes);
            }
            more = crunch_increment(plan, length, indexes);
        }
    }
    return result;
}
