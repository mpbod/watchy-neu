#include "watchy/packages.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static void callback_before(watchy_package_session_t *session) {
    if (session->watchdog.before_callback != NULL) {
        session->watchdog.before_callback(session->watchdog.context);
    }
}

static void callback_after(watchy_package_session_t *session) {
    if (session->watchdog.after_callback != NULL) {
        session->watchdog.after_callback(session->watchdog.context);
    }
}

static bool metadata_string_matches(const char *actual, const char *expected, size_t maximum) {
    size_t actual_length;
    size_t expected_length;
    if (actual == NULL || expected == NULL) {
        return false;
    }
    actual_length = strnlen(actual, maximum + 1u);
    expected_length = strnlen(expected, maximum + 1u);
    return actual_length <= maximum && actual_length == expected_length &&
           memcmp(actual, expected, actual_length) == 0;
}

static bool descriptor_valid(const watchy_package_descriptor_v1_t *descriptor,
                             const watchy_package_manifest_t *manifest) {
    if (descriptor == NULL || descriptor->size < sizeof(*descriptor) ||
        !metadata_string_matches(descriptor->metadata.identifier,
                                 manifest->id,
                                 WATCHY_PACKAGE_ID_MAX) ||
        !metadata_string_matches(descriptor->metadata.name,
                                 manifest->name,
                                 WATCHY_PACKAGE_NAME_MAX) ||
        !metadata_string_matches(descriptor->metadata.version,
                                 manifest->version,
                                 WATCHY_PACKAGE_VERSION_MAX) ||
        descriptor->metadata.abi.major != manifest->abi_major ||
        descriptor->metadata.abi.minor != manifest->abi_minor ||
        !watchy_abi_compatible(descriptor->metadata.abi.major,
                              descriptor->metadata.abi.minor,
                              WATCHY_ABI_V1_MAJOR,
                              WATCHY_ABI_V1_MINOR)) {
        return false;
    }
    return descriptor->callbacks.on_load != NULL && descriptor->callbacks.on_unload != NULL &&
           descriptor->callbacks.on_start != NULL && descriptor->callbacks.on_stop != NULL &&
           descriptor->callbacks.on_event != NULL && descriptor->callbacks.on_render != NULL;
}

static watchy_package_status_t close_session(watchy_package_session_t *session, bool call_stop) {
    int close_status = 0;
    watchy_status_t transition_status = WATCHY_STATUS_OK;

    if (session->handle == NULL) {
        watchy_runtime_reset(&session->runtime);
        return WATCHY_PACKAGE_OK;
    }
    if (call_stop && session->runtime.state == WATCHY_RUNTIME_STARTED &&
        session->descriptor != NULL) {
        callback_before(session);
        session->descriptor->callbacks.on_stop(session->user_data);
        callback_after(session);
        transition_status = watchy_runtime_transition(&session->runtime, WATCHY_RUNTIME_STOPPED);
    }
    if (session->on_load_completed && session->descriptor != NULL) {
        callback_before(session);
        session->descriptor->callbacks.on_unload(session->user_data);
        callback_after(session);
    }
    close_status = session->loader.close(session->loader.context, session->handle);
    session->handle = NULL;
    session->descriptor = NULL;
    session->user_data = NULL;
    session->on_load_completed = false;
    if (session->runtime.state == WATCHY_RUNTIME_STOPPED &&
        watchy_runtime_transition(&session->runtime, WATCHY_RUNTIME_EMPTY) != WATCHY_STATUS_OK) {
        transition_status = WATCHY_STATUS_INVALID_STATE;
    }
    watchy_runtime_reset(&session->runtime);
    if (close_status != 0) {
        return WATCHY_PACKAGE_ERR_LOADER;
    }
    return transition_status == WATCHY_STATUS_OK ? WATCHY_PACKAGE_OK : WATCHY_PACKAGE_ERR_STATE;
}

watchy_package_status_t watchy_package_session_init(
    watchy_package_session_t *session,
    const watchy_package_loader_api_t *loader,
    const watchy_package_watchdog_api_t *watchdog) {
    if (session == NULL || loader == NULL || loader->open == NULL || loader->symbol == NULL ||
        loader->close == NULL) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    memset(session, 0, sizeof(*session));
    session->loader = *loader;
    if (watchdog != NULL) {
        session->watchdog = *watchdog;
    }
    watchy_runtime_reset(&session->runtime);
    return WATCHY_PACKAGE_OK;
}

watchy_package_status_t watchy_package_session_load(watchy_package_session_t *session,
                                                    const char *elf_path,
                                                    const watchy_package_manifest_t *manifest,
                                                    const watchy_host_caps_v1_t *host,
                                                    bool safe_mode,
                                                    bool quarantined) {
    watchy_package_entry_fn_t entry = NULL;
    void *symbol;
    watchy_status_t callback_status;

    if (session == NULL || elf_path == NULL || manifest == NULL || host == NULL ||
        host->size < sizeof(*host) ||
        !watchy_abi_compatible(WATCHY_ABI_V1_MAJOR,
                              WATCHY_ABI_V1_MINOR,
                              host->abi.major,
                              host->abi.minor)) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (safe_mode) {
        return WATCHY_PACKAGE_ERR_SAFE_MODE;
    }
    if (quarantined) {
        return WATCHY_PACKAGE_ERR_QUARANTINED;
    }
    if (session->handle != NULL || session->runtime.state != WATCHY_RUNTIME_EMPTY) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    session->handle = session->loader.open(session->loader.context,
                                           elf_path,
                                           WATCHY_PACKAGE_RTLD_NOW);
    if (session->handle == NULL) {
        return WATCHY_PACKAGE_ERR_LOADER;
    }
    symbol = session->loader.symbol(session->loader.context,
                                    session->handle,
                                    "watchy_package_entry");
    if (symbol == NULL || sizeof(entry) != sizeof(symbol)) {
        (void)close_session(session, false);
        return WATCHY_PACKAGE_ERR_LOADER;
    }
    memcpy(&entry, &symbol, sizeof(entry));
    callback_before(session);
    session->descriptor = entry();
    callback_after(session);
    if (!descriptor_valid(session->descriptor, manifest)) {
        (void)close_session(session, false);
        return WATCHY_PACKAGE_ERR_DESCRIPTOR;
    }
    callback_before(session);
    callback_status = session->descriptor->callbacks.on_load(host, &session->user_data);
    callback_after(session);
    if (callback_status != WATCHY_STATUS_OK) {
        if (session->user_data != NULL) {
            callback_before(session);
            session->descriptor->callbacks.on_unload(session->user_data);
            callback_after(session);
        }
        session->user_data = NULL;
        (void)close_session(session, false);
        return WATCHY_PACKAGE_ERR_CALLBACK;
    }
    session->on_load_completed = true;
    if (watchy_runtime_transition(&session->runtime, WATCHY_RUNTIME_LOADED) != WATCHY_STATUS_OK) {
        (void)close_session(session, false);
        return WATCHY_PACKAGE_ERR_STATE;
    }
    session->runtime.package_abi = session->descriptor->metadata.abi;
    session->runtime.host_abi = host->abi;
    return WATCHY_PACKAGE_OK;
}

watchy_package_status_t watchy_package_session_start(watchy_package_session_t *session) {
    watchy_status_t status;
    if (session == NULL || session->runtime.state != WATCHY_RUNTIME_LOADED ||
        session->descriptor == NULL) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    callback_before(session);
    status = session->descriptor->callbacks.on_start(session->user_data);
    callback_after(session);
    if (status != WATCHY_STATUS_OK) {
        (void)close_session(session, false);
        return WATCHY_PACKAGE_ERR_CALLBACK;
    }
    if (watchy_runtime_transition(&session->runtime, WATCHY_RUNTIME_STARTED) != WATCHY_STATUS_OK) {
        (void)close_session(session, true);
        return WATCHY_PACKAGE_ERR_STATE;
    }
    return WATCHY_PACKAGE_OK;
}

watchy_package_status_t watchy_package_session_event(watchy_package_session_t *session,
                                                     const watchy_event_t *event) {
    watchy_status_t status;
    if (session == NULL || event == NULL || session->runtime.state != WATCHY_RUNTIME_STARTED ||
        session->descriptor == NULL || event->type < WATCHY_EVENT_NONE ||
        event->type > WATCHY_EVENT_SYSTEM) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    callback_before(session);
    status = session->descriptor->callbacks.on_event(session->user_data, event);
    callback_after(session);
    if (status != WATCHY_STATUS_OK) {
        (void)close_session(session, true);
        return WATCHY_PACKAGE_ERR_CALLBACK;
    }
    return WATCHY_PACKAGE_OK;
}

watchy_package_status_t watchy_package_session_render(watchy_package_session_t *session,
                                                      watchy_canvas_t *canvas,
                                                      watchy_refresh_mode_t *mode) {
    watchy_status_t status;
    if (session == NULL || canvas == NULL || mode == NULL || canvas->pixels == NULL ||
        canvas->width == 0u || canvas->height == 0u || canvas->stride == 0u ||
        canvas->rotation > 3u ||
        (canvas->format != WATCHY_PIXEL_MONO && canvas->format != WATCHY_PIXEL_GRAY4) ||
        session->runtime.state != WATCHY_RUNTIME_STARTED || session->descriptor == NULL) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    callback_before(session);
    status = session->descriptor->callbacks.on_render(session->user_data, canvas, mode);
    callback_after(session);
    if (status != WATCHY_STATUS_OK || (*mode != WATCHY_REFRESH_PARTIAL && *mode != WATCHY_REFRESH_FULL)) {
        (void)close_session(session, true);
        return WATCHY_PACKAGE_ERR_CALLBACK;
    }
    return WATCHY_PACKAGE_OK;
}

watchy_package_status_t watchy_package_session_stop(watchy_package_session_t *session) {
    if (session == NULL || session->handle == NULL) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    return close_session(session, true);
}

bool watchy_package_session_loaded(const watchy_package_session_t *session) {
    return session != NULL && session->handle != NULL;
}
