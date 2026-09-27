/* SPDX-License-Identifier: GPL-2.0-only */
#include "crunch_external_protocol.h"

#include <limits.h>
#include <string.h>

static bool crunch_external_u64(const char* text, uint64_t* value) {
    if(!text || !*text) return false;
    uint64_t result = 0U;
    while(*text) {
        if(*text < '0' || *text > '9') return false;
        uint8_t digit = (uint8_t)(*text - '0');
        if(result > (UINT64_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
        text++;
    }
    *value = result;
    return true;
}

static bool crunch_external_u32(const char* text, uint32_t* value) {
    uint64_t parsed;
    if(!crunch_external_u64(text, &parsed) || parsed > UINT32_MAX) return false;
    *value = (uint32_t)parsed;
    return true;
}

static bool crunch_external_i32(const char* text, int32_t* value) {
    if(!text || !*text) return false;
    bool negative = *text == '-';
    if(negative) text++;
    uint64_t parsed;
    if(!crunch_external_u64(text, &parsed)) return false;
    if(parsed > INT32_MAX)
        return false;
    *value = negative ? -(int32_t)parsed : (int32_t)parsed;
    return true;
}

static bool crunch_external_copy(char* output, size_t size, const char* token) {
    size_t length = strlen(token);
    if(!length || length >= size) return false;
    memcpy(output, token, length + 1U);
    return true;
}

static size_t crunch_external_tokens(char* line, char** tokens, size_t capacity) {
    size_t count = 0U;
    char* position = line;
    while(*position && count < capacity) {
        while(*position == ' ') position++;
        if(!*position) break;
        tokens[count++] = position;
        while(*position && *position != ' ') position++;
        if(*position) *position++ = '\0';
    }
    return count;
}

bool crunch_external_parse_line(const char* line, CrunchExternalMessage* message) {
    if(!line || !message) return false;
    size_t length = strlen(line);
    if(!length || length >= CRUNCH_EXTERNAL_LINE_MAX) return false;
    char copy[CRUNCH_EXTERNAL_LINE_MAX];
    memcpy(copy, line, length + 1U);
    char* tokens[10];
    size_t count = crunch_external_tokens(copy, tokens, 10U);
    if(count < 3U || strcmp(tokens[0], "CWF1")) return false;
    memset(message, 0, sizeof(*message));
    if(!strcmp(tokens[1], "INFO")) {
        if(count != 5U || !crunch_external_u32(tokens[2], &message->protocol_version) ||
           !crunch_external_copy(message->bridge_version, sizeof(message->bridge_version), tokens[3]) ||
           !crunch_external_copy(message->crunch_version, sizeof(message->crunch_version), tokens[4]))
            return false;
        message->type = CrunchExternalMessageInfo;
        return true;
    }
    if(!strcmp(tokens[1], "STATUS")) {
        if(count != 8U ||
           !crunch_external_copy(message->state, sizeof(message->state), tokens[2]) ||
           !crunch_external_u64(tokens[3], &message->lines) ||
           !crunch_external_u64(tokens[4], &message->bytes) ||
           !crunch_external_u64(tokens[5], &message->elapsed_ms) ||
           !crunch_external_i32(tokens[6], &message->exit_code) ||
           !crunch_external_copy(message->output_name, sizeof(message->output_name), tokens[7]))
            return false;
        message->type = CrunchExternalMessageStatus;
        return true;
    }
    if(!strcmp(tokens[1], "ERROR")) {
        if(count != 3U || !crunch_external_copy(message->error, sizeof(message->error), tokens[2]))
            return false;
        message->type = CrunchExternalMessageError;
        return true;
    }
    return false;
}

void crunch_external_decoder_reset(CrunchExternalDecoder* decoder) {
    if(decoder) memset(decoder, 0, sizeof(*decoder));
}

void crunch_external_decoder_feed(
    CrunchExternalDecoder* decoder,
    const uint8_t* data,
    size_t length,
    CrunchExternalMessageCallback callback,
    void* context) {
    if(!decoder || (!data && length)) return;
    for(size_t index = 0; index < length; index++) {
        uint8_t byte = data[index];
        if(byte == '\n') {
            if(!decoder->overflow && decoder->length) {
                if(decoder->line[decoder->length - 1U] == '\r') decoder->length--;
                decoder->line[decoder->length] = '\0';
                CrunchExternalMessage message;
                if(crunch_external_parse_line(decoder->line, &message) && callback)
                    callback(&message, context);
            }
            decoder->length = 0U;
            decoder->overflow = false;
        } else if(!decoder->overflow) {
            if(decoder->length + 1U < sizeof(decoder->line))
                decoder->line[decoder->length++] = (char)byte;
            else
                decoder->overflow = true;
        }
    }
}
