/* SPDX-License-Identifier: GPL-2.0-only */
#include "crunch_external.h"
#include "crunch_external_protocol.h"

#include <expansion/expansion.h>
#include <furi.h>
#include <furi_hal.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CrunchExternalFlagStop = 1U << 0,
    CrunchExternalFlagRx = 1U << 1,
    CrunchExternalFlagError = 1U << 2,
};

struct CrunchExternal {
    FuriMutex* data_mutex;
    FuriMutex* tx_mutex;
    FuriStreamBuffer* rx_stream;
    FuriThread* worker;
    bool worker_started;
    FuriHalSerialHandle* serial;
    Expansion* expansion;
    CrunchExternalDecoder decoder;
    CrunchExternalSnapshot snapshot;
};

static void crunch_external_set_error(CrunchExternal* external, const char* text) {
    furi_mutex_acquire(external->data_mutex, FuriWaitForever);
    snprintf(external->snapshot.error, sizeof(external->snapshot.error), "%s", text);
    furi_mutex_release(external->data_mutex);
}

static bool crunch_external_send(CrunchExternal* external, const char* command) {
    if(!external->serial) return false;
    furi_mutex_acquire(external->tx_mutex, FuriWaitForever);
    furi_hal_serial_tx(external->serial, (const uint8_t*)command, strlen(command));
    furi_hal_serial_tx_wait_complete(external->serial);
    furi_mutex_release(external->tx_mutex);
    return true;
}

static void crunch_external_message(const CrunchExternalMessage* message, void* context) {
    CrunchExternal* external = context;
    furi_mutex_acquire(external->data_mutex, FuriWaitForever);
    CrunchExternalSnapshot* snapshot = &external->snapshot;
    if(message->type == CrunchExternalMessageInfo) {
        snapshot->protocol_version = message->protocol_version;
        if(message->protocol_version == CRUNCH_EXTERNAL_PROTOCOL_VERSION) {
            snapshot->connected = true;
            snapshot->error[0] = '\0';
            snprintf(snapshot->bridge_version, sizeof(snapshot->bridge_version), "%s", message->bridge_version);
            snprintf(snapshot->crunch_version, sizeof(snapshot->crunch_version), "%s", message->crunch_version);
        } else {
            snapshot->connected = false;
            snprintf(snapshot->error, sizeof(snapshot->error), "Protocol mismatch: Pi=%" PRIu32, message->protocol_version);
        }
    } else if(message->type == CrunchExternalMessageStatus) {
        snprintf(snapshot->state, sizeof(snapshot->state), "%s", message->state);
        snapshot->running = !strcmp(message->state, "STARTING") ||
                            !strcmp(message->state, "RUNNING") ||
                            !strcmp(message->state, "STOPPING");
        snapshot->lines = message->lines;
        snapshot->bytes = message->bytes;
        snapshot->elapsed_ms = message->elapsed_ms;
        snapshot->exit_code = message->exit_code;
        snprintf(snapshot->output_name, sizeof(snapshot->output_name), "%s", message->output_name);
    } else if(message->type == CrunchExternalMessageError) {
        snprintf(snapshot->error, sizeof(snapshot->error), "%s", message->error);
        snapshot->running = false;
    }
    furi_mutex_release(external->data_mutex);
}

static void crunch_external_irq(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    CrunchExternal* external = context;
    uint32_t flags = 0U;
    if(event & FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        if(furi_stream_buffer_send(external->rx_stream, &byte, 1U, 0U) == 1U)
            flags |= CrunchExternalFlagRx;
        else
            flags |= CrunchExternalFlagError;
    }
    if(event &
       (FuriHalSerialRxEventFrameError | FuriHalSerialRxEventNoiseError |
        FuriHalSerialRxEventOverrunError | FuriHalSerialRxEventParityError))
        flags |= CrunchExternalFlagError;
    if(flags && external->worker)
        furi_thread_flags_set(furi_thread_get_id(external->worker), flags);
}

static int32_t crunch_external_worker(void* context) {
    CrunchExternal* external = context;
    while(true) {
        uint32_t flags = furi_thread_flags_wait(
            CrunchExternalFlagStop | CrunchExternalFlagRx | CrunchExternalFlagError,
            FuriFlagWaitAny,
            1000U);
        if(flags & FuriFlagError) {
            if(flags == (uint32_t)FuriFlagErrorTimeout)
                crunch_external_send(external, "CWF1 STATUS\n");
            else
                crunch_external_set_error(external, "UART worker failure");
            continue;
        }
        if(flags & CrunchExternalFlagStop) break;
        if(flags & CrunchExternalFlagError) {
            furi_mutex_acquire(external->data_mutex, FuriWaitForever);
            external->snapshot.serial_errors++;
            snprintf(
                external->snapshot.error,
                sizeof(external->snapshot.error),
                "UART receive error (%" PRIu32 ")",
                external->snapshot.serial_errors);
            furi_mutex_release(external->data_mutex);
        }
        if(flags & CrunchExternalFlagRx) {
            uint8_t buffer[64];
            size_t received;
            do {
                received = furi_stream_buffer_receive(external->rx_stream, buffer, sizeof(buffer), 0U);
                if(received)
                    crunch_external_decoder_feed(
                        &external->decoder,
                        buffer,
                        received,
                        crunch_external_message,
                        external);
            } while(received);
        }
    }
    return 0;
}

CrunchExternal* crunch_external_alloc(void) {
    CrunchExternal* external = calloc(1U, sizeof(CrunchExternal));
    if(!external) return NULL;
    external->data_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    external->tx_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!external->data_mutex || !external->tx_mutex) {
        if(external->tx_mutex) furi_mutex_free(external->tx_mutex);
        if(external->data_mutex) furi_mutex_free(external->data_mutex);
        free(external);
        return NULL;
    }
    snprintf(external->snapshot.state, sizeof(external->snapshot.state), "DISCONNECTED");
    return external;
}

bool crunch_external_start(CrunchExternal* external, uint32_t baudrate) {
    if(!external || external->serial || baudrate < 9600U) return false;
    memset(&external->snapshot, 0, sizeof(external->snapshot));
    snprintf(external->snapshot.state, sizeof(external->snapshot.state), "CONNECTING");
    external->snapshot.active = true;
    crunch_external_decoder_reset(&external->decoder);
    external->rx_stream = furi_stream_buffer_alloc(1024U, 1U);
    external->worker = furi_thread_alloc_ex("CrunchExternal", 2048U, crunch_external_worker, external);
    if(!external->rx_stream || !external->worker) {
        crunch_external_set_error(external, "Allocation failed");
        crunch_external_stop(external);
        return false;
    }
    external->expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(external->expansion);
    external->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!external->serial) {
        crunch_external_set_error(external, "USART busy");
        crunch_external_stop(external);
        return false;
    }
    furi_thread_start(external->worker);
    external->worker_started = true;
    furi_hal_serial_init(external->serial, baudrate);
    furi_hal_serial_async_rx_start(external->serial, crunch_external_irq, external, true);
    crunch_external_send(external, "CWF1 HELLO\n");
    crunch_external_send(external, "CWF1 STATUS\n");
    return true;
}

void crunch_external_stop(CrunchExternal* external) {
    if(!external) return;
    if(external->serial) {
        crunch_external_send(external, "CWF1 STOP\n");
        furi_hal_serial_async_rx_stop(external->serial);
    }
    if(external->worker) {
        if(external->worker_started) {
            furi_thread_flags_set(furi_thread_get_id(external->worker), CrunchExternalFlagStop);
            furi_thread_join(external->worker);
        }
        furi_thread_free(external->worker);
        external->worker = NULL;
        external->worker_started = false;
    }
    if(external->serial) {
        furi_hal_serial_deinit(external->serial);
        furi_hal_serial_control_release(external->serial);
        external->serial = NULL;
    }
    if(external->expansion) {
        expansion_enable(external->expansion);
        furi_record_close(RECORD_EXPANSION);
        external->expansion = NULL;
    }
    if(external->rx_stream) {
        furi_stream_buffer_free(external->rx_stream);
        external->rx_stream = NULL;
    }
    furi_mutex_acquire(external->data_mutex, FuriWaitForever);
    external->snapshot.active = false;
    external->snapshot.connected = false;
    external->snapshot.running = false;
    snprintf(external->snapshot.state, sizeof(external->snapshot.state), "DISCONNECTED");
    furi_mutex_release(external->data_mutex);
}

static bool crunch_external_hex(const char* input, char* output, size_t capacity) {
    static const char digits[] = "0123456789ABCDEF";
    size_t length = strlen(input);
    if(!length) {
        if(capacity < 2U) return false;
        output[0] = '-';
        output[1] = '\0';
        return true;
    }
    if(length > (capacity - 1U) / 2U) return false;
    for(size_t index = 0; index < length; index++) {
        uint8_t byte = (uint8_t)input[index];
        output[index * 2U] = digits[byte >> 4U];
        output[index * 2U + 1U] = digits[byte & 0x0FU];
    }
    output[length * 2U] = '\0';
    return true;
}

bool crunch_external_run(
    CrunchExternal* external,
    const CrunchConfig* config,
    const char* output_filename) {
    if(!external || !config || !output_filename) return false;
    CrunchExternalSnapshot snapshot;
    crunch_external_snapshot(external, &snapshot);
    if(!snapshot.connected || snapshot.running) return false;
    CrunchPlan plan;
    if(crunch_plan_build(config, &plan) != CrunchOk) return false;
    char custom[CRUNCH_MAX_CHARSET * 2U + 1U];
    char pattern[CRUNCH_MAX_WORD * 2U + 1U];
    char literal[CRUNCH_MAX_WORD * 2U + 1U];
    char filename[129];
    if(!crunch_external_hex(config->custom_charset, custom, sizeof(custom)) ||
       !crunch_external_hex(config->pattern, pattern, sizeof(pattern)) ||
       !crunch_external_hex(config->literal_mask, literal, sizeof(literal)) ||
       !crunch_external_hex(output_filename, filename, sizeof(filename)))
        return false;
    char command[CRUNCH_EXTERNAL_LINE_MAX];
    int written = snprintf(
        command,
        sizeof(command),
        "CWF1 RUN %u %u %u %s %s %s %s\n",
        config->minimum_length,
        config->maximum_length,
        (unsigned)config->charset_mode,
        custom,
        pattern,
        literal,
        filename);
    if(written <= 0 || (size_t)written >= sizeof(command) || !crunch_external_send(external, command))
        return false;
    furi_mutex_acquire(external->data_mutex, FuriWaitForever);
    external->snapshot.running = true;
    external->snapshot.lines = 0U;
    external->snapshot.bytes = 0U;
    external->snapshot.elapsed_ms = 0U;
    snprintf(external->snapshot.state, sizeof(external->snapshot.state), "STARTING");
    furi_mutex_release(external->data_mutex);
    return true;
}

bool crunch_external_cancel(CrunchExternal* external) {
    if(!external) return false;
    CrunchExternalSnapshot snapshot;
    crunch_external_snapshot(external, &snapshot);
    return snapshot.connected && crunch_external_send(external, "CWF1 STOP\n");
}

void crunch_external_snapshot(CrunchExternal* external, CrunchExternalSnapshot* snapshot) {
    if(!external || !snapshot) return;
    furi_mutex_acquire(external->data_mutex, FuriWaitForever);
    *snapshot = external->snapshot;
    furi_mutex_release(external->data_mutex);
}

void crunch_external_free(CrunchExternal* external) {
    if(!external) return;
    crunch_external_stop(external);
    furi_mutex_free(external->tx_mutex);
    furi_mutex_free(external->data_mutex);
    free(external);
}
