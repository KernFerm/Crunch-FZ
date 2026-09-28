/* SPDX-License-Identifier: GPL-2.0-only */
#include "crunch_core.h"
#include "crunch_external.h"

#include <furi.h>
#include <furi/core/memmgr.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <storage/storage.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CRUNCH_FZ_VERSION "1.0.4"
#define CRUNCH_OUTPUT_DIR_MAX 127U
#define CRUNCH_OUTPUT_NAME_MAX 63U
#define CRUNCH_OUTPUT_PATH_MAX 255U
#define CRUNCH_IO_BUFFER_SIZE 1024U
#define CRUNCH_TRANSACTION_SUFFIX_MAX 8U

typedef enum {
    CrunchViewMain,
    CrunchViewSettings,
    CrunchViewInput,
    CrunchViewText,
    CrunchViewExternal,
} CrunchViewId;

typedef enum {
    CrunchMenuSettings,
    CrunchMenuExternal,
    CrunchMenuCustom,
    CrunchMenuPattern,
    CrunchMenuClearPattern,
    CrunchMenuLiteral,
    CrunchMenuClearLiteral,
    CrunchMenuFilename,
    CrunchMenuDirectory,
    CrunchMenuPreflight,
    CrunchMenuAbout,
} CrunchMenuItem;

typedef enum {
    CrunchInputCustom,
    CrunchInputPattern,
    CrunchInputLiteral,
    CrunchInputFilename,
    CrunchInputDirectory,
} CrunchInputPurpose;

typedef enum {
    CrunchRunIdle,
    CrunchRunComplete,
    CrunchRunCancelled,
    CrunchRunDirectoryError,
    CrunchRunOpenError,
    CrunchRunWriteError,
} CrunchRunStatus;

typedef struct {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* dispatcher;
    Submenu* main_menu;
    VariableItemList* settings;
    TextInput* input;
    Widget* widget;
    View* external_view;
    FuriString* text;
    FuriThread* worker;
    FuriMutex* state_mutex;
    CrunchExternal* external;
    CrunchConfig config;
    CrunchPlan plan;
    CrunchGenerateResult result;
    CrunchRunStatus run_status;
    CrunchViewId current_view;
    CrunchViewId text_return;
    CrunchInputPurpose input_purpose;
    char output_filename[CRUNCH_OUTPUT_NAME_MAX + 1U];
    char output_directory[CRUNCH_OUTPUT_DIR_MAX + 1U];
    char output_path[CRUNCH_OUTPUT_PATH_MAX + 1U];
    volatile bool cancel;
    uint64_t progress_lines;
    uint64_t progress_bytes;
    uint32_t start_tick;
    uint32_t end_tick;
    size_t heap_before;
    size_t heap_after;
    size_t heap_minimum;
    uint32_t worker_stack_free;
    uint32_t external_baud;
    uint8_t external_baud_index;
    bool views_added;
} CrunchApp;

typedef struct {
    CrunchApp* app;
    uint32_t revision;
} CrunchExternalModel;

static const uint32_t crunch_external_bauds[] = {115200U, 230400U, 460800U};
static const char* const crunch_external_baud_names[] = {"115200", "230400", "460800"};

static void crunch_switch(CrunchApp* app, CrunchViewId view) {
    app->current_view = view;
    view_dispatcher_switch_to_view(app->dispatcher, view);
}

static void crunch_show_text(
    CrunchApp* app,
    const char* title,
    const char* body,
    CrunchViewId return_view) {
    widget_reset(app->widget);
    furi_string_printf(app->text, "\e#%s\n%s", title, body);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, furi_string_get_cstr(app->text));
    app->text_return = return_view;
    crunch_switch(app, CrunchViewText);
}

static size_t crunch_bounded_length(const char* value, size_t limit) {
    size_t length = 0U;
    while(length < limit && value[length]) length++;
    return length;
}

static bool crunch_filename_valid(const char* filename) {
    size_t length = crunch_bounded_length(filename, CRUNCH_OUTPUT_NAME_MAX + 1U);
    if(!length || length > CRUNCH_OUTPUT_NAME_MAX || !strcmp(filename, ".") ||
       !strcmp(filename, "..")) {
        return false;
    }
    for(size_t index = 0; index < length; index++) {
        unsigned char character = (unsigned char)filename[index];
        if(character < 0x20U || character > 0x7EU || strchr("/\\:*?\"<>|", character))
            return false;
    }
    return true;
}

static bool crunch_directory_valid(const char* directory) {
    size_t length = crunch_bounded_length(directory, CRUNCH_OUTPUT_DIR_MAX + 1U);
    if(!length || length > CRUNCH_OUTPUT_DIR_MAX || !strcmp(directory, ".") ||
       !strcmp(directory, "..") || strstr(directory, "..")) {
        return false;
    }
    for(size_t index = 0; index < length; index++) {
        unsigned char character = (unsigned char)directory[index];
        if(character < 0x20U || character > 0x7EU || strchr("/\\:*?\"<>|", character)) {
            return false;
        }
    }
    return true;
}

static bool crunch_build_output_path(CrunchApp* app) {
    if(!crunch_filename_valid(app->output_filename) ||
       !crunch_directory_valid(app->output_directory)) {
        return false;
    }
    int written = snprintf(
        app->output_path,
        sizeof(app->output_path),
        "/ext/%s/%s",
        app->output_directory,
        app->output_filename);
    return written > 0 && (size_t)written < sizeof(app->output_path);
}

static void crunch_external_draw(Canvas* canvas, void* model_context) {
    const CrunchExternalModel* model = model_context;
    CrunchExternalSnapshot snapshot;
    crunch_external_snapshot(model->app->external, &snapshot);
    char line[96];
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 1, 9, "External Crunch");
    canvas_set_font(canvas, FontKeyboard);
    if(!snapshot.connected) {
        snprintf(line, sizeof(line), "State: %s", snapshot.state);
        canvas_draw_str(canvas, 1, 20, line);
        canvas_draw_str(canvas, 1, 30, snapshot.error[0] ? snapshot.error : "Waiting for CWF1 bridge");
        canvas_draw_str(canvas, 1, 61, "Back: close UART");
        return;
    }
    snprintf(line, sizeof(line), "Crunch %s  %s", snapshot.crunch_version, snapshot.state);
    canvas_draw_str(canvas, 1, 19, line);
    snprintf(line, sizeof(line), "Lines %llu", (unsigned long long)snapshot.lines);
    canvas_draw_str(canvas, 1, 29, line);
    snprintf(line, sizeof(line), "Bytes %llu", (unsigned long long)snapshot.bytes);
    canvas_draw_str(canvas, 1, 39, line);
    snprintf(
        line,
        sizeof(line),
        "Time %llu.%01llus",
        (unsigned long long)(snapshot.elapsed_ms / 1000U),
        (unsigned long long)((snapshot.elapsed_ms % 1000U) / 100U));
    canvas_draw_str(canvas, 1, 49, line);
    snprintf(
        line,
        sizeof(line),
        "%s",
        snapshot.error[0] ? snapshot.error :
                            snapshot.output_name[0] ? snapshot.output_name : "No output yet");
    canvas_draw_str(canvas, 1, 59, line);
    canvas_draw_str(canvas, 94, 9, snapshot.running ? "OK Stop" : "OK Run");
}

static bool crunch_external_input(InputEvent* event, void* context) {
    CrunchApp* app = context;
    if(event->key != InputKeyOk || event->type != InputTypeShort) return false;
    CrunchExternalSnapshot snapshot;
    crunch_external_snapshot(app->external, &snapshot);
    if(!snapshot.connected) return false;
    if(snapshot.running)
        crunch_external_cancel(app->external);
    else
        crunch_external_run(app->external, &app->config, app->output_filename);
    CrunchExternalModel* model = view_get_model(app->external_view);
    model->revision++;
    view_commit_model(app->external_view, true);
    return true;
}

static void crunch_start_external(CrunchApp* app) {
    CrunchPlan plan;
    CrunchError error = crunch_plan_build(&app->config, &plan);
    if(error != CrunchOk) {
        crunch_show_text(app, "Invalid configuration", crunch_error_string(error), CrunchViewMain);
        return;
    }
    if(!crunch_filename_valid(app->output_filename)) {
        crunch_show_text(app, "Invalid output", "Set a valid output filename first.", CrunchViewMain);
        return;
    }
    crunch_external_start(app->external, app->external_baud);
    crunch_switch(app, CrunchViewExternal);
    CrunchExternalModel* model = view_get_model(app->external_view);
    model->revision++;
    view_commit_model(app->external_view, true);
}

static void crunch_join_worker(CrunchApp* app) {
    if(app->worker) {
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        app->worker = NULL;
    }
}

static uint64_t crunch_elapsed_milliseconds(uint32_t start, uint32_t end) {
    uint32_t frequency = furi_kernel_get_tick_frequency();
    return frequency ? ((uint64_t)(end - start) * 1000U) / frequency : 0U;
}

static uint64_t crunch_rate_tenths(uint64_t lines, uint64_t elapsed_ms) {
    if(!elapsed_ms) return 0U;
    return (lines / elapsed_ms) * 10000U +
           ((lines % elapsed_ms) * 10000U) / elapsed_ms;
}

static unsigned crunch_percent(uint64_t completed, uint64_t total) {
    if(!total) return 0U;
    if(completed >= total) return 100U;
    uint64_t quotient = total / 100U;
    uint64_t remainder = total % 100U;
    for(unsigned percent = 99U; percent > 0U; percent--) {
        uint64_t threshold = quotient * percent + (remainder * percent + 99U) / 100U;
        if(completed >= threshold) return percent;
    }
    return 0U;
}

static bool crunch_cancelled(void* context) {
    CrunchApp* app = context;
    return app->cancel;
}

typedef struct {
    File* file;
    uint8_t data[CRUNCH_IO_BUFFER_SIZE];
    size_t used;
} CrunchBufferedWriter;

static bool crunch_writer_flush(CrunchBufferedWriter* writer) {
    if(!writer->used) return true;
    if(storage_file_write(writer->file, writer->data, writer->used) != writer->used) return false;
    writer->used = 0U;
    return true;
}

static bool crunch_writer_append(CrunchBufferedWriter* writer, const uint8_t* data, size_t length) {
    while(length) {
        size_t available = sizeof(writer->data) - writer->used;
        if(!available && !crunch_writer_flush(writer)) return false;
        available = sizeof(writer->data) - writer->used;
        size_t chunk = length < available ? length : available;
        memcpy(writer->data + writer->used, data, chunk);
        writer->used += chunk;
        data += chunk;
        length -= chunk;
    }
    return true;
}

typedef struct {
    CrunchApp* app;
    CrunchBufferedWriter* writer;
} CrunchWorkerContext;

static bool crunch_worker_emit(void* context, const uint8_t* data, size_t length) {
    CrunchWorkerContext* worker = context;
    return crunch_writer_append(worker->writer, data, length);
}

static bool crunch_worker_cancelled(void* context) {
    CrunchWorkerContext* worker = context;
    return crunch_cancelled(worker->app);
}

static void crunch_worker_progress(void* context, uint64_t lines, uint64_t bytes) {
    CrunchWorkerContext* worker = context;
    if(furi_mutex_acquire(worker->app->state_mutex, FuriWaitForever) == FuriStatusOk) {
        worker->app->progress_lines = lines;
        worker->app->progress_bytes = bytes;
        furi_mutex_release(worker->app->state_mutex);
    }
}

static int32_t crunch_worker(void* context) {
    CrunchApp* app = context;
    app->heap_before = memmgr_get_free_heap();
    app->start_tick = furi_get_tick();
    app->run_status = CrunchRunIdle;
    memset(&app->result, 0, sizeof(app->result));

    char directory[CRUNCH_OUTPUT_PATH_MAX + 1U];
    char temporary[CRUNCH_OUTPUT_PATH_MAX + CRUNCH_TRANSACTION_SUFFIX_MAX + 1U];
    char backup[CRUNCH_OUTPUT_PATH_MAX + CRUNCH_TRANSACTION_SUFFIX_MAX + 1U];
    snprintf(directory, sizeof(directory), "/ext/%s", app->output_directory);
    snprintf(temporary, sizeof(temporary), "%s.partial", app->output_path);
    snprintf(backup, sizeof(backup), "%s.backup", app->output_path);

    if(storage_simply_mkdir(app->storage, directory)) {
        if(storage_file_exists(app->storage, backup)) {
            if(!storage_file_exists(app->storage, app->output_path)) {
                if(storage_common_rename(app->storage, backup, app->output_path) != FSE_OK) {
                    app->run_status = CrunchRunWriteError;
                    goto worker_done;
                }
            } else if(storage_common_remove(app->storage, backup) != FSE_OK) {
                app->run_status = CrunchRunWriteError;
                goto worker_done;
            }
        }
        if(storage_file_exists(app->storage, temporary) &&
           storage_common_remove(app->storage, temporary) != FSE_OK) {
            app->run_status = CrunchRunWriteError;
            goto worker_done;
        }
        File* file = storage_file_alloc(app->storage);
        if(file) {
            app->run_status = CrunchRunOpenError;
            if(storage_file_open(file, temporary, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
                CrunchBufferedWriter* writer = calloc(1U, sizeof(CrunchBufferedWriter));
                if(writer) {
                    writer->file = file;
                    CrunchWorkerContext worker_context = {.app = app, .writer = writer};
                    app->result = crunch_generate(
                        &app->plan,
                        crunch_worker_emit,
                        crunch_worker_cancelled,
                        crunch_worker_progress,
                        &worker_context);
                    if(app->result.status == CrunchGenerateComplete && crunch_writer_flush(writer))
                        app->run_status = CrunchRunComplete;
                    else if(app->result.status == CrunchGenerateCancelled)
                        app->run_status = CrunchRunCancelled;
                    else
                        app->run_status = CrunchRunWriteError;
                    free(writer);
                }
                if(!storage_file_sync(file) && app->run_status == CrunchRunComplete)
                    app->run_status = CrunchRunWriteError;
                storage_file_close(file);

                if(app->run_status == CrunchRunComplete) {
                    bool had_output = storage_file_exists(app->storage, app->output_path);
                    if(had_output &&
                       storage_common_rename(app->storage, app->output_path, backup) != FSE_OK) {
                        app->run_status = CrunchRunWriteError;
                    } else if(storage_common_rename(app->storage, temporary, app->output_path) !=
                              FSE_OK) {
                        if(had_output)
                            storage_common_rename(app->storage, backup, app->output_path);
                        app->run_status = CrunchRunWriteError;
                    } else if(had_output) {
                        storage_common_remove(app->storage, backup);
                    }
                }
                if(app->run_status != CrunchRunComplete)
                    storage_common_remove(app->storage, temporary);
            }
            storage_file_free(file);
        } else {
            app->run_status = CrunchRunOpenError;
        }
    } else {
        app->run_status = CrunchRunDirectoryError;
    }

worker_done:
    app->end_tick = furi_get_tick();
    app->heap_after = memmgr_get_free_heap();
    app->heap_minimum = memmgr_get_minimum_free_heap();
    app->worker_stack_free = furi_thread_get_stack_space(furi_thread_get_current_id());
    view_dispatcher_send_custom_event(app->dispatcher, 1U);
    return 0;
}

static void crunch_show_completion(CrunchApp* app) {
    uint64_t elapsed_ms = crunch_elapsed_milliseconds(app->start_tick, app->end_tick);
    uint64_t rate_tenths = crunch_rate_tenths(app->result.generated_lines, elapsed_ms);
    const char* title = "Generation failed";
    const char* status = "Unknown storage failure.";
    if(app->run_status == CrunchRunComplete) {
        title = "Generation complete";
        status = "Exact preflight total written.";
    } else if(app->run_status == CrunchRunCancelled) {
        title = "Generation cancelled";
        status = "Temporary output discarded; existing output preserved.";
    } else if(app->run_status == CrunchRunDirectoryError) {
        status = "Could not create the output directory.";
    } else if(app->run_status == CrunchRunOpenError) {
        status = "Could not open the output file.";
    } else if(app->run_status == CrunchRunWriteError) {
        status = "Write failed; temporary output discarded and existing output preserved.";
    }
    char body[512];
    snprintf(
        body,
        sizeof(body),
        "%s\n\n%s\nLines: %llu / %llu\nBytes: %llu / %llu\nElapsed: %llu.%02llu s\nMeasured: %llu.%01llu entries/s\n\nHeap: %lu -> %lu\nHeap min: %lu\nWorker stack free: %lu",
        status,
        app->output_path,
        (unsigned long long)app->result.generated_lines,
        (unsigned long long)app->plan.total_lines,
        (unsigned long long)app->result.output_bytes,
        (unsigned long long)app->plan.total_bytes,
        (unsigned long long)(elapsed_ms / 1000U),
        (unsigned long long)((elapsed_ms % 1000U) / 10U),
        (unsigned long long)(rate_tenths / 10U),
        (unsigned long long)(rate_tenths % 10U),
        (unsigned long)app->heap_before,
        (unsigned long)app->heap_after,
        (unsigned long)app->heap_minimum,
        (unsigned long)app->worker_stack_free);
    crunch_show_text(app, title, body, CrunchViewMain);
}

static void crunch_start_generation(CrunchApp* app) {
    crunch_join_worker(app);
    app->cancel = false;
    app->progress_lines = 0U;
    app->progress_bytes = 0U;
    app->run_status = CrunchRunIdle;
    widget_reset(app->widget);
    furi_string_printf(
        app->text,
        "\e#Generating\n0%%\n0 / %llu lines\n0 / %llu bytes\n\nBack requests cancellation.",
        (unsigned long long)app->plan.total_lines,
        (unsigned long long)app->plan.total_bytes);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, furi_string_get_cstr(app->text));
    app->text_return = CrunchViewMain;
    crunch_switch(app, CrunchViewText);
    app->worker = furi_thread_alloc_ex("CrunchWorker", 4096U, crunch_worker, app);
    if(app->worker) {
        furi_thread_start(app->worker);
    } else {
        app->run_status = CrunchRunOpenError;
        crunch_show_text(
            app,
            "Generation failed",
            "Could not allocate the worker thread. No output was generated.",
            CrunchViewMain);
    }
}

static void crunch_overwrite_button(GuiButtonType button, InputType type, void* context) {
    CrunchApp* app = context;
    if(button == GuiButtonTypeCenter && type == InputTypeShort) crunch_start_generation(app);
}

static void crunch_request_generation(GuiButtonType button, InputType type, void* context) {
    CrunchApp* app = context;
    if(button != GuiButtonTypeCenter || type != InputTypeShort) return;
    if(storage_file_exists(app->storage, app->output_path)) {
        widget_reset(app->widget);
        furi_string_printf(
            app->text,
            "\e#Overwrite output?\n%s\n\nThe existing file will be replaced.",
            app->output_path);
        widget_add_text_scroll_element(app->widget, 0, 0, 128, 51, furi_string_get_cstr(app->text));
        widget_add_button_element(
            app->widget, GuiButtonTypeCenter, "Overwrite", crunch_overwrite_button, app);
        app->text_return = CrunchViewMain;
    } else {
        crunch_start_generation(app);
    }
}

static void crunch_preflight(CrunchApp* app) {
    CrunchError error = crunch_plan_build(&app->config, &app->plan);
    if(error != CrunchOk) {
        crunch_show_text(app, "Invalid configuration", crunch_error_string(error), CrunchViewMain);
        return;
    }
    if(!crunch_build_output_path(app)) {
        crunch_show_text(
            app,
            "Invalid output",
            "Use printable names without / \\ : * ? \" < > |. The folder is created under /ext.",
            CrunchViewMain);
        return;
    }
    uint64_t total_space = 0U;
    uint64_t free_space = 0U;
    if(storage_common_fs_info(app->storage, "/ext", &total_space, &free_space) != FSE_OK) {
        crunch_show_text(
            app,
            "Storage unavailable",
            "Could not read microSD capacity. Check that the card is mounted and try again.",
            CrunchViewMain);
        return;
    }
    if(app->plan.total_bytes > free_space) {
        char message[192];
        snprintf(
            message,
            sizeof(message),
            "Required: %llu bytes\nAvailable: %llu bytes\n\nGeneration was not started.",
            (unsigned long long)app->plan.total_bytes,
            (unsigned long long)free_space);
        crunch_show_text(app, "Not enough space", message, CrunchViewMain);
        return;
    }
    widget_reset(app->widget);
    furi_string_printf(
        app->text,
        "\e#Preflight\nCharset: %s\nMode: %s\nLength: %u..%u\nLines: %llu\nBytes: %llu\nSD free: %llu\nOutput: %s%s",
        crunch_charset_name(app->config.charset_mode),
        app->plan.pattern_mode ? "pattern" : "range",
        app->config.minimum_length,
        app->config.maximum_length,
        (unsigned long long)app->plan.total_lines,
        (unsigned long long)app->plan.total_bytes,
        (unsigned long long)free_space,
        app->output_path,
        storage_file_exists(app->storage, app->output_path) ? "\nExisting file: overwrite confirmation required." : "");
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 51, furi_string_get_cstr(app->text));
    widget_add_button_element(
        app->widget, GuiButtonTypeCenter, "Generate", crunch_request_generation, app);
    app->text_return = CrunchViewMain;
    crunch_switch(app, CrunchViewText);
}

static void crunch_input_done(void* context) {
    CrunchApp* app = context;
    crunch_switch(app, CrunchViewMain);
}

static void crunch_open_input(CrunchApp* app, CrunchInputPurpose purpose) {
    app->input_purpose = purpose;
    char* buffer = NULL;
    size_t size = 0U;
    const char* header = NULL;
    if(purpose == CrunchInputCustom) {
        buffer = app->config.custom_charset;
        size = sizeof(app->config.custom_charset);
        header = "Custom character set";
    } else if(purpose == CrunchInputPattern) {
        buffer = app->config.pattern;
        size = sizeof(app->config.pattern);
        header = "Pattern @ , % ^";
    } else if(purpose == CrunchInputLiteral) {
        buffer = app->config.literal_mask;
        size = sizeof(app->config.literal_mask);
        header = "Literal mask (same length)";
    } else if(purpose == CrunchInputFilename) {
        buffer = app->output_filename;
        size = sizeof(app->output_filename);
        header = "Output filename";
    } else {
        buffer = app->output_directory;
        size = sizeof(app->output_directory);
        header = "Folder name under /ext";
    }
    text_input_reset(app->input);
    text_input_set_header_text(app->input, header);
    text_input_set_minimum_length(app->input, 1U);
    text_input_set_result_callback(app->input, crunch_input_done, app, buffer, size, true);
    crunch_switch(app, CrunchViewInput);
}

static void crunch_main_selected(void* context, uint32_t index) {
    CrunchApp* app = context;
    switch(index) {
    case CrunchMenuSettings: crunch_switch(app, CrunchViewSettings); break;
    case CrunchMenuExternal: crunch_start_external(app); break;
    case CrunchMenuCustom: crunch_open_input(app, CrunchInputCustom); break;
    case CrunchMenuPattern: crunch_open_input(app, CrunchInputPattern); break;
    case CrunchMenuClearPattern:
        app->config.pattern[0] = '\0';
        app->config.literal_mask[0] = '\0';
        crunch_show_text(app, "Pattern cleared", "Length range mode is active.", CrunchViewMain);
        break;
    case CrunchMenuLiteral: crunch_open_input(app, CrunchInputLiteral); break;
    case CrunchMenuClearLiteral:
        app->config.literal_mask[0] = '\0';
        crunch_show_text(app, "Literal mask cleared", "All pattern markers are active.", CrunchViewMain);
        break;
    case CrunchMenuFilename: crunch_open_input(app, CrunchInputFilename); break;
    case CrunchMenuDirectory: crunch_open_input(app, CrunchInputDirectory); break;
    case CrunchMenuPreflight: crunch_preflight(app); break;
    case CrunchMenuAbout:
        crunch_show_text(
            app,
            "About Crunch FZ",
            "Version " CRUNCH_FZ_VERSION
            "\n\nStreams genuine Crunch-style combinations directly to microSD. No complete wordlist is held in RAM."
             "\n\nPatterns: @ selected/lower set, , uppercase, % numbers, ^ symbols. Fixed text creates prefixes and suffixes. A same-length literal mask makes matching markers literal."
             "\n\nPreflight uses checked exact line and byte counts and verifies microSD free space. Buffered transactional output preserves an existing wordlist unless the replacement fully succeeds."
             "\n\nProgress and speed come from the real generation run."
            "\n\nExternal Crunch runs the genuine upstream executable on a Raspberry Pi or Linux computer. The Flipper sends the validated configuration over 3.3V UART and displays measured Pi output."
            "\n\nGNU GPL v2 only. Use only for authorized work.",
            CrunchViewMain);
        break;
    }
}

static void crunch_charset_changed(VariableItem* item) {
    CrunchApp* app = variable_item_get_context(item);
    app->config.charset_mode = (CrunchCharsetMode)variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, crunch_charset_name(app->config.charset_mode));
}

static void crunch_minimum_changed(VariableItem* item) {
    CrunchApp* app = variable_item_get_context(item);
    app->config.minimum_length = (uint8_t)(variable_item_get_current_value_index(item) + 1U);
    char value[4];
    snprintf(value, sizeof(value), "%u", app->config.minimum_length);
    variable_item_set_current_value_text(item, value);
}

static void crunch_maximum_changed(VariableItem* item) {
    CrunchApp* app = variable_item_get_context(item);
    app->config.maximum_length = (uint8_t)(variable_item_get_current_value_index(item) + 1U);
    char value[4];
    snprintf(value, sizeof(value), "%u", app->config.maximum_length);
    variable_item_set_current_value_text(item, value);
}

static void crunch_external_baud_changed(VariableItem* item) {
    CrunchApp* app = variable_item_get_context(item);
    app->external_baud_index = variable_item_get_current_value_index(item);
    app->external_baud = crunch_external_bauds[app->external_baud_index];
    variable_item_set_current_value_text(
        item, crunch_external_baud_names[app->external_baud_index]);
}

static bool crunch_custom_event(void* context, uint32_t event) {
    CrunchApp* app = context;
    if(event != 1U) return false;
    crunch_join_worker(app);
    crunch_show_completion(app);
    return true;
}

static bool crunch_back(void* context) {
    CrunchApp* app = context;
    if(app->current_view == CrunchViewMain) {
        view_dispatcher_stop(app->dispatcher);
    } else if(app->worker) {
        app->cancel = true;
        furi_string_set_str(
            app->text,
            "\e#Cancelling...\nWaiting for the current SD write to finish and the output file to close.");
        widget_reset(app->widget);
        widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, furi_string_get_cstr(app->text));
    } else if(app->current_view == CrunchViewExternal) {
        crunch_external_stop(app->external);
        crunch_switch(app, CrunchViewMain);
    } else if(app->current_view == CrunchViewText) {
        crunch_switch(app, app->text_return);
    } else {
        crunch_switch(app, CrunchViewMain);
    }
    return true;
}

static void crunch_tick(void* context) {
    CrunchApp* app = context;
    if(app->current_view == CrunchViewExternal) {
        CrunchExternalModel* model = view_get_model(app->external_view);
        model->revision++;
        view_commit_model(app->external_view, true);
        return;
    }
    if(!app->worker || app->run_status != CrunchRunIdle) return;
    uint64_t lines = 0U;
    uint64_t bytes = 0U;
    if(furi_mutex_acquire(app->state_mutex, 0U) == FuriStatusOk) {
        lines = app->progress_lines;
        bytes = app->progress_bytes;
        furi_mutex_release(app->state_mutex);
    }
    uint64_t elapsed_ms = crunch_elapsed_milliseconds(app->start_tick, furi_get_tick());
    uint64_t rate_tenths = crunch_rate_tenths(lines, elapsed_ms);
    unsigned percent = crunch_percent(lines, app->plan.total_lines);
    furi_string_printf(
        app->text,
        "\e#Generating\n%u%%\n%llu / %llu lines\n%llu / %llu bytes\n%llu.%02llu s; %llu.%01llu entries/s\n\nBack requests cancellation.",
        percent,
        (unsigned long long)lines,
        (unsigned long long)app->plan.total_lines,
        (unsigned long long)bytes,
        (unsigned long long)app->plan.total_bytes,
        (unsigned long long)(elapsed_ms / 1000U),
        (unsigned long long)((elapsed_ms % 1000U) / 10U),
        (unsigned long long)(rate_tenths / 10U),
        (unsigned long long)(rate_tenths % 10U));
    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, furi_string_get_cstr(app->text));
}

static CrunchApp* crunch_app_alloc(void) {
    CrunchApp* app = calloc(1U, sizeof(CrunchApp));
    if(!app) return NULL;
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dispatcher = view_dispatcher_alloc();
    app->main_menu = submenu_alloc();
    app->settings = variable_item_list_alloc();
    app->input = text_input_alloc();
    app->widget = widget_alloc();
    app->external_view = view_alloc();
    app->text = furi_string_alloc();
    app->state_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->external = crunch_external_alloc();
    if(!app->gui || !app->storage || !app->dispatcher || !app->main_menu || !app->settings ||
       !app->input || !app->widget || !app->external_view || !app->text || !app->state_mutex ||
       !app->external) {
        return app;
    }

    app->config.charset_mode = CrunchCharsetLowercase;
    app->config.minimum_length = 1U;
    app->config.maximum_length = 4U;
    memcpy(app->config.custom_charset, "abc123", 7U);
    memcpy(app->output_filename, "wordlist.txt", 13U);
    memcpy(app->output_directory, "crunch_fz", 10U);
    app->external_baud = 115200U;

    submenu_set_header(app->main_menu, "Crunch FZ v" CRUNCH_FZ_VERSION);
    submenu_add_item(app->main_menu, "Charset and lengths", CrunchMenuSettings, crunch_main_selected, app);
    submenu_add_item(app->main_menu, "External Crunch", CrunchMenuExternal, crunch_main_selected, app);
    submenu_add_item(app->main_menu, "Edit custom set", CrunchMenuCustom, crunch_main_selected, app);
    submenu_add_item(app->main_menu, "Edit pattern", CrunchMenuPattern, crunch_main_selected, app);
    submenu_add_item(app->main_menu, "Clear pattern", CrunchMenuClearPattern, crunch_main_selected, app);
    submenu_add_item(app->main_menu, "Edit literal mask", CrunchMenuLiteral, crunch_main_selected, app);
    submenu_add_item(app->main_menu, "Clear literal mask", CrunchMenuClearLiteral, crunch_main_selected, app);
    submenu_add_item(app->main_menu, "Output filename", CrunchMenuFilename, crunch_main_selected, app);
    submenu_add_item(app->main_menu, "Output directory", CrunchMenuDirectory, crunch_main_selected, app);
    submenu_add_item(app->main_menu, "Preflight / Generate", CrunchMenuPreflight, crunch_main_selected, app);
    submenu_add_item(app->main_menu, "About", CrunchMenuAbout, crunch_main_selected, app);

    VariableItem* charset = variable_item_list_add(
        app->settings, "Charset", CrunchCharsetCount, crunch_charset_changed, app);
    variable_item_set_current_value_index(charset, app->config.charset_mode);
    variable_item_set_current_value_text(charset, crunch_charset_name(app->config.charset_mode));
    VariableItem* minimum = variable_item_list_add(
        app->settings, "Minimum length", CRUNCH_MAX_WORD, crunch_minimum_changed, app);
    variable_item_set_current_value_index(minimum, app->config.minimum_length - 1U);
    variable_item_set_current_value_text(minimum, "1");
    VariableItem* maximum = variable_item_list_add(
        app->settings, "Maximum length", CRUNCH_MAX_WORD, crunch_maximum_changed, app);
    variable_item_set_current_value_index(maximum, app->config.maximum_length - 1U);
    variable_item_set_current_value_text(maximum, "4");
    VariableItem* baud = variable_item_list_add(
        app->settings,
        "External baud",
        COUNT_OF(crunch_external_bauds),
        crunch_external_baud_changed,
        app);
    variable_item_set_current_value_index(baud, 0U);
    variable_item_set_current_value_text(baud, crunch_external_baud_names[0]);
    VariableItem* version = variable_item_list_add(app->settings, "Version", 1U, NULL, app);
    variable_item_set_current_value_text(version, CRUNCH_FZ_VERSION);

    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, crunch_custom_event);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, crunch_back);
    view_dispatcher_set_tick_event_callback(app->dispatcher, crunch_tick, 250U);
    view_dispatcher_add_view(app->dispatcher, CrunchViewMain, submenu_get_view(app->main_menu));
    view_dispatcher_add_view(
        app->dispatcher, CrunchViewSettings, variable_item_list_get_view(app->settings));
    view_dispatcher_add_view(app->dispatcher, CrunchViewInput, text_input_get_view(app->input));
    view_dispatcher_add_view(app->dispatcher, CrunchViewText, widget_get_view(app->widget));
    view_set_context(app->external_view, app);
    view_set_draw_callback(app->external_view, crunch_external_draw);
    view_set_input_callback(app->external_view, crunch_external_input);
    view_allocate_model(app->external_view, ViewModelTypeLocking, sizeof(CrunchExternalModel));
    CrunchExternalModel* external_model = view_get_model(app->external_view);
    external_model->app = app;
    view_commit_model(app->external_view, false);
    view_dispatcher_add_view(app->dispatcher, CrunchViewExternal, app->external_view);
    app->views_added = true;
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    return app;
}

static bool crunch_app_valid(CrunchApp* app) {
    return app && app->gui && app->storage && app->dispatcher && app->main_menu && app->settings &&
           app->input && app->widget && app->external_view && app->text && app->state_mutex &&
           app->external;
}

static void crunch_app_free(CrunchApp* app) {
    if(!app) return;
    app->cancel = true;
    crunch_join_worker(app);
    if(app->external) crunch_external_free(app->external);
    if(app->dispatcher && app->views_added) {
        view_dispatcher_remove_view(app->dispatcher, CrunchViewExternal);
        view_dispatcher_remove_view(app->dispatcher, CrunchViewText);
        view_dispatcher_remove_view(app->dispatcher, CrunchViewInput);
        view_dispatcher_remove_view(app->dispatcher, CrunchViewSettings);
        view_dispatcher_remove_view(app->dispatcher, CrunchViewMain);
    }
    if(app->state_mutex) furi_mutex_free(app->state_mutex);
    if(app->external_view) view_free(app->external_view);
    if(app->text) furi_string_free(app->text);
    if(app->widget) widget_free(app->widget);
    if(app->input) text_input_free(app->input);
    if(app->settings) variable_item_list_free(app->settings);
    if(app->main_menu) submenu_free(app->main_menu);
    if(app->dispatcher) view_dispatcher_free(app->dispatcher);
    if(app->storage) furi_record_close(RECORD_STORAGE);
    if(app->gui) furi_record_close(RECORD_GUI);
    free(app);
}

int32_t crunch_fz_app(void* context) {
    UNUSED(context);
    CrunchApp* app = crunch_app_alloc();
    if(!crunch_app_valid(app)) {
        crunch_app_free(app);
        return -1;
    }
    crunch_switch(app, CrunchViewMain);
    view_dispatcher_run(app->dispatcher);
    crunch_app_free(app);
    return 0;
}
