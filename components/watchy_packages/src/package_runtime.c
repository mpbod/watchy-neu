#include "watchy/packages.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static atomic_flag s_runtime_lock = ATOMIC_FLAG_INIT;
static watchy_package_session_t *s_owner;

static bool runtime_lock(void) {
    return !atomic_flag_test_and_set_explicit(&s_runtime_lock, memory_order_acquire);
}

static void runtime_unlock(void) {
    atomic_flag_clear_explicit(&s_runtime_lock, memory_order_release);
}

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

static bool checked_range(const watchy_package_loader_api_t *loader,
                          const void *address,
                          size_t size,
                          bool executable) {
    const uintptr_t start = (uintptr_t)address;
    if (address == NULL || size == 0u || start > UINTPTR_MAX - (size - 1u)) {
        return false;
    }
    if (executable) {
        return loader->executable == NULL ||
               loader->executable(loader->context, address, size);
    }
    return loader->readable == NULL || loader->readable(loader->context, address, size);
}

static bool function_valid(const watchy_package_loader_api_t *loader,
                           const void *function_bytes,
                           size_t function_size) {
    void *address = NULL;
    if (function_size != sizeof(address)) {
        return false;
    }
    memcpy(&address, function_bytes, sizeof(address));
    return checked_range(loader, address, 1u, true);
}

static bool copy_metadata_string(const watchy_package_loader_api_t *loader,
                                 const char *source,
                                 char *destination,
                                 size_t maximum) {
    if (source == NULL) {
        return false;
    }
    for (size_t index = 0u; index <= maximum; ++index) {
        if (!checked_range(loader, source + index, 1u, false)) {
            return false;
        }
        destination[index] = source[index];
        if (source[index] == '\0') {
            return true;
        }
    }
    return false;
}

static bool descriptor_copy_valid(watchy_package_session_t *session,
                                  const watchy_package_descriptor_v1_t *source,
                                  const watchy_package_manifest_t *manifest) {
    uint32_t descriptor_size;
    if (!checked_range(&session->loader, source, sizeof(descriptor_size), false)) {
        return false;
    }
    memcpy(&descriptor_size, source, sizeof(descriptor_size));
    if (descriptor_size < sizeof(watchy_package_descriptor_v1_t) ||
        !checked_range(&session->loader, source, sizeof(watchy_package_descriptor_v1_t), false)) {
        return false;
    }
    memcpy(&session->descriptor, source, sizeof(session->descriptor));
    if (!copy_metadata_string(&session->loader, session->descriptor.metadata.identifier,
                              session->identifier, WATCHY_PACKAGE_ID_MAX) ||
        !copy_metadata_string(&session->loader, session->descriptor.metadata.name,
                              session->name, WATCHY_PACKAGE_NAME_MAX) ||
        !copy_metadata_string(&session->loader, session->descriptor.metadata.version,
                              session->version, WATCHY_PACKAGE_VERSION_MAX) ||
        strcmp(session->identifier, manifest->id) != 0 ||
        strcmp(session->name, manifest->name) != 0 ||
        strcmp(session->version, manifest->version) != 0 ||
        session->descriptor.metadata.abi.major != manifest->abi_major ||
        session->descriptor.metadata.abi.minor != manifest->abi_minor ||
        !watchy_abi_compatible(session->descriptor.metadata.abi.major,
                              session->descriptor.metadata.abi.minor,
                              WATCHY_ABI_V1_MAJOR,
                              WATCHY_ABI_V1_MINOR) ||
        !function_valid(&session->loader, &session->descriptor.callbacks.on_load,
                        sizeof(session->descriptor.callbacks.on_load)) ||
        !function_valid(&session->loader, &session->descriptor.callbacks.on_unload,
                        sizeof(session->descriptor.callbacks.on_unload)) ||
        !function_valid(&session->loader, &session->descriptor.callbacks.on_start,
                        sizeof(session->descriptor.callbacks.on_start)) ||
        !function_valid(&session->loader, &session->descriptor.callbacks.on_stop,
                        sizeof(session->descriptor.callbacks.on_stop)) ||
        !function_valid(&session->loader, &session->descriptor.callbacks.on_event,
                        sizeof(session->descriptor.callbacks.on_event)) ||
        !function_valid(&session->loader, &session->descriptor.callbacks.on_render,
                        sizeof(session->descriptor.callbacks.on_render))) {
        memset(&session->descriptor, 0, sizeof(session->descriptor));
        return false;
    }
    session->descriptor.metadata.identifier = session->identifier;
    session->descriptor.metadata.name = session->name;
    session->descriptor.metadata.version = session->version;
    return true;
}

static watchy_package_status_t close_session_locked(watchy_package_session_t *session,
                                                    bool call_stop) {
    watchy_status_t transition_status = WATCHY_STATUS_OK;
    int close_status;

    if (session->handle == NULL) {
        return WATCHY_PACKAGE_OK;
    }
    if (session->poisoned) {
        return WATCHY_PACKAGE_ERR_LOADER;
    }
    if (call_stop && session->runtime.state == WATCHY_RUNTIME_STARTED) {
        callback_before(session);
        session->descriptor.callbacks.on_stop(session->user_data);
        callback_after(session);
        transition_status = watchy_runtime_transition(&session->runtime, WATCHY_RUNTIME_STOPPED);
    }
    if (session->on_load_completed) {
        session->on_load_completed = false;
        callback_before(session);
        session->descriptor.callbacks.on_unload(session->user_data);
        callback_after(session);
    }
    session->user_data = NULL;
    close_status = session->loader.close(session->loader.context, session->handle);
    if (close_status != 0) {
        session->poisoned = true;
        return WATCHY_PACKAGE_ERR_LOADER;
    }
    session->handle = NULL;
    memset(&session->descriptor, 0, sizeof(session->descriptor));
    memset(session->identifier, 0, sizeof(session->identifier));
    memset(session->name, 0, sizeof(session->name));
    memset(session->version, 0, sizeof(session->version));
    watchy_runtime_reset(&session->runtime);
    if (s_owner == session) {
        s_owner = NULL;
    }
    return transition_status == WATCHY_STATUS_OK ? WATCHY_PACKAGE_OK : WATCHY_PACKAGE_ERR_STATE;
}

watchy_package_status_t watchy_package_session_init(
    watchy_package_session_t *session,
    const watchy_package_loader_api_t *loader,
    const watchy_package_watchdog_api_t *watchdog) {
    if (session == NULL || loader == NULL || loader->open == NULL || loader->symbol == NULL ||
        loader->close == NULL || !runtime_lock()) {
        return session == NULL || loader == NULL ? WATCHY_PACKAGE_ERR_ARGUMENT
                                                  : WATCHY_PACKAGE_ERR_STATE;
    }
    /* s_owner is the authoritative live/poisoned-session registry. Do not
     * inspect caller storage before initialization; it need not be zeroed. */
    if (s_owner == session) {
        runtime_unlock();
        return WATCHY_PACKAGE_ERR_STATE;
    }
    memset(session, 0, sizeof(*session));
    session->loader = *loader;
    if (watchdog != NULL) {
        session->watchdog = *watchdog;
    }
    watchy_runtime_reset(&session->runtime);
    session->initialized = true;
    runtime_unlock();
    return WATCHY_PACKAGE_OK;
}

watchy_package_status_t watchy_package_session_load(watchy_package_session_t *session,
                                                    const char *elf_path,
                                                    const watchy_package_manifest_t *manifest,
                                                    const watchy_host_caps_v1_t *host,
                                                    bool safe_mode,
                                                    bool quarantined) {
    watchy_package_entry_fn_t entry = NULL;
    const watchy_package_descriptor_v1_t *descriptor;
    void *symbol;
    watchy_status_t callback_status;
    watchy_package_status_t result = WATCHY_PACKAGE_OK;

    if (session == NULL || !session->initialized || elf_path == NULL || manifest == NULL ||
        host == NULL || host->size < sizeof(*host) ||
        !watchy_abi_compatible(WATCHY_ABI_V1_MAJOR, WATCHY_ABI_V1_MINOR,
                              host->abi.major, host->abi.minor)) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (safe_mode) {
        return WATCHY_PACKAGE_ERR_SAFE_MODE;
    }
    if (quarantined) {
        return WATCHY_PACKAGE_ERR_QUARANTINED;
    }
    if (!runtime_lock()) {
        return WATCHY_PACKAGE_ERR_STATE;
    }
    if (s_owner != NULL || session->handle != NULL || session->poisoned ||
        session->runtime.state != WATCHY_RUNTIME_EMPTY) {
        runtime_unlock();
        return WATCHY_PACKAGE_ERR_STATE;
    }
    session->handle = session->loader.open(session->loader.context,
                                           elf_path,
                                           WATCHY_PACKAGE_RTLD_NOW);
    if (session->handle == NULL) {
        runtime_unlock();
        return WATCHY_PACKAGE_ERR_LOADER;
    }
    s_owner = session;
    symbol = session->loader.symbol(session->loader.context,
                                    session->handle,
                                    "watchy_package_entry");
    if (symbol == NULL || sizeof(entry) != sizeof(symbol) ||
        !checked_range(&session->loader, symbol, 1u, true)) {
        result = WATCHY_PACKAGE_ERR_LOADER;
        goto cleanup;
    }
    memcpy(&entry, &symbol, sizeof(entry));
    callback_before(session);
    descriptor = entry();
    callback_after(session);
    if (!descriptor_copy_valid(session, descriptor, manifest)) {
        result = WATCHY_PACKAGE_ERR_DESCRIPTOR;
        goto cleanup;
    }
    callback_before(session);
    callback_status = session->descriptor.callbacks.on_load(host, &session->user_data);
    callback_after(session);
    if (callback_status != WATCHY_STATUS_OK) {
        if (session->user_data != NULL) {
            callback_before(session);
            session->descriptor.callbacks.on_unload(session->user_data);
            callback_after(session);
            session->user_data = NULL;
        }
        result = WATCHY_PACKAGE_ERR_CALLBACK;
        goto cleanup;
    }
    session->on_load_completed = true;
    if (watchy_runtime_transition(&session->runtime, WATCHY_RUNTIME_LOADED) != WATCHY_STATUS_OK) {
        result = WATCHY_PACKAGE_ERR_STATE;
        goto cleanup;
    }
    session->runtime.package_abi = session->descriptor.metadata.abi;
    session->runtime.host_abi = host->abi;
    runtime_unlock();
    return WATCHY_PACKAGE_OK;

cleanup:
    (void)close_session_locked(session, false);
    runtime_unlock();
    return result;
}

watchy_package_status_t watchy_package_session_start(watchy_package_session_t *session) {
    watchy_status_t status;
    watchy_package_status_t result;
    if (session == NULL || !session->initialized || !runtime_lock()) {
        return session == NULL ? WATCHY_PACKAGE_ERR_ARGUMENT : WATCHY_PACKAGE_ERR_STATE;
    }
    if (s_owner != session || session->runtime.state != WATCHY_RUNTIME_LOADED || session->poisoned) {
        runtime_unlock();
        return WATCHY_PACKAGE_ERR_STATE;
    }
    callback_before(session);
    status = session->descriptor.callbacks.on_start(session->user_data);
    callback_after(session);
    if (status != WATCHY_STATUS_OK) {
        (void)close_session_locked(session, false);
        runtime_unlock();
        return WATCHY_PACKAGE_ERR_CALLBACK;
    }
    result = watchy_runtime_transition(&session->runtime, WATCHY_RUNTIME_STARTED) == WATCHY_STATUS_OK
                 ? WATCHY_PACKAGE_OK : WATCHY_PACKAGE_ERR_STATE;
    if (result != WATCHY_PACKAGE_OK) {
        (void)close_session_locked(session, true);
    }
    runtime_unlock();
    return result;
}

watchy_package_status_t watchy_package_session_event(watchy_package_session_t *session,
                                                     const watchy_event_t *event) {
    watchy_status_t status;
    if (session == NULL || event == NULL || event->type < WATCHY_EVENT_NONE ||
        event->type > WATCHY_EVENT_SYSTEM || !runtime_lock()) {
        return session == NULL || event == NULL ? WATCHY_PACKAGE_ERR_ARGUMENT
                                                 : WATCHY_PACKAGE_ERR_STATE;
    }
    if (s_owner != session || session->runtime.state != WATCHY_RUNTIME_STARTED ||
        session->poisoned) {
        runtime_unlock();
        return WATCHY_PACKAGE_ERR_STATE;
    }
    callback_before(session);
    status = session->descriptor.callbacks.on_event(session->user_data, event);
    callback_after(session);
    if (status != WATCHY_STATUS_OK) {
        (void)close_session_locked(session, true);
        runtime_unlock();
        return WATCHY_PACKAGE_ERR_CALLBACK;
    }
    runtime_unlock();
    return WATCHY_PACKAGE_OK;
}

watchy_package_status_t watchy_package_session_render(watchy_package_session_t *session,
                                                      watchy_canvas_t *canvas,
                                                      watchy_refresh_mode_t *mode) {
    watchy_canvas_t bound_canvas;
    size_t canvas_bytes;
    watchy_status_t status;
    if (session == NULL || canvas == NULL || mode == NULL || canvas->pixels == NULL ||
        canvas->width == 0u || canvas->height == 0u || canvas->stride == 0u ||
        canvas->rotation > 3u ||
        (canvas->format != WATCHY_PIXEL_MONO && canvas->format != WATCHY_PIXEL_GRAY4) ||
        canvas->stride < (canvas->format == WATCHY_PIXEL_MONO
                              ? (uint16_t)((canvas->width + 7u) / 8u)
                              : (uint16_t)((canvas->width + 1u) / 2u)) ||
        (size_t)canvas->stride > SIZE_MAX / canvas->height || !runtime_lock()) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    canvas_bytes = (size_t)canvas->stride * canvas->height;
    if (s_owner != session || session->runtime.state != WATCHY_RUNTIME_STARTED ||
        session->poisoned ||
        (session->loader.writable != NULL &&
         !session->loader.writable(session->loader.context, canvas->pixels, canvas_bytes))) {
        runtime_unlock();
        return WATCHY_PACKAGE_ERR_STATE;
    }
    bound_canvas = *canvas;
    callback_before(session);
    status = session->descriptor.callbacks.on_render(session->user_data, canvas, mode);
    callback_after(session);
    if (canvas->pixels != bound_canvas.pixels || canvas->width != bound_canvas.width ||
        canvas->height != bound_canvas.height || canvas->stride != bound_canvas.stride ||
        canvas->rotation != bound_canvas.rotation || canvas->format != bound_canvas.format) {
        *canvas = bound_canvas;
        status = WATCHY_STATUS_INVALID_ARGUMENT;
    }
    if (status != WATCHY_STATUS_OK ||
        (*mode != WATCHY_REFRESH_PARTIAL && *mode != WATCHY_REFRESH_FULL)) {
        (void)close_session_locked(session, true);
        runtime_unlock();
        return WATCHY_PACKAGE_ERR_CALLBACK;
    }
    runtime_unlock();
    return WATCHY_PACKAGE_OK;
}

watchy_package_status_t watchy_package_session_stop(watchy_package_session_t *session) {
    watchy_package_status_t status;
    if (session == NULL || !runtime_lock()) {
        return session == NULL ? WATCHY_PACKAGE_ERR_ARGUMENT : WATCHY_PACKAGE_ERR_STATE;
    }
    if (s_owner != session || session->handle == NULL) {
        runtime_unlock();
        return WATCHY_PACKAGE_ERR_STATE;
    }
    status = close_session_locked(session, true);
    runtime_unlock();
    return status;
}

bool watchy_package_session_loaded(const watchy_package_session_t *session) {
    return session != NULL && session->handle != NULL;
}
