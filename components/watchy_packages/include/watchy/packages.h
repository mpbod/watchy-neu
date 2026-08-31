#ifndef WATCHY_PACKAGES_H
#define WATCHY_PACKAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "watchy/runtime.h"
#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WATCHY_PACKAGE_ID_MAX 48u
#define WATCHY_PACKAGE_DIGEST_SIZE 32u

typedef enum {
    WATCHY_PACKAGE_OK = 0,
    WATCHY_PACKAGE_ERR_ARGUMENT = -1,
    WATCHY_PACKAGE_ERR_DIGEST = -2,
    WATCHY_PACKAGE_ERR_CRYPTO = -3,
    WATCHY_PACKAGE_ERR_MANIFEST = -4,
    WATCHY_PACKAGE_ERR_UTF8 = -5,
    WATCHY_PACKAGE_ERR_CAPABILITY = -6,
    WATCHY_PACKAGE_ERR_PATH = -7,
    WATCHY_PACKAGE_ERR_COLLISION = -8,
    WATCHY_PACKAGE_ERR_LIMIT = -9,
    WATCHY_PACKAGE_ERR_ELF = -10,
    WATCHY_PACKAGE_ERR_WPK = -11,
    WATCHY_PACKAGE_ERR_ASSETS = -12,
    WATCHY_PACKAGE_ERR_ABI = -13,
    WATCHY_PACKAGE_ERR_STORE = -14,
    WATCHY_PACKAGE_ERR_STATE = -15,
    WATCHY_PACKAGE_ERR_QUARANTINED = -16,
    WATCHY_PACKAGE_ERR_SAFE_MODE = -17,
    WATCHY_PACKAGE_ERR_LOADER = -18,
    WATCHY_PACKAGE_ERR_DESCRIPTOR = -19,
    WATCHY_PACKAGE_ERR_CALLBACK = -20,
    WATCHY_PACKAGE_ERR_FILESYSTEM = -21,
} watchy_package_status_t;

#define WATCHY_PACKAGE_NAME_MAX 64u
#define WATCHY_PACKAGE_VERSION_MAX 32u
#define WATCHY_PACKAGE_ASSET_PATH_MAX 96u
#define WATCHY_PACKAGE_ASSET_COUNT_MAX 64u
#define WATCHY_PACKAGE_ASSETS_BYTES_MAX (32u * 1024u)
#define WATCHY_PACKAGE_RUNTIME_BYTES_MAX (80u * 1024u)
#define WATCHY_PACKAGE_ELF_BYTES_MAX (64u * 1024u)
#define WATCHY_PACKAGE_MANIFEST_BYTES_MAX (16u * 1024u)
#define WATCHY_PACKAGE_WPK_BYTES_MAX (80u * 1024u)
#define WATCHY_PACKAGE_REF_MAX (WATCHY_PACKAGE_ID_MAX + 1u + WATCHY_PACKAGE_VERSION_MAX)
#define WATCHY_PACKAGE_HEALTH_RECORD_MAX 16u
#define WATCHY_PACKAGE_INSTALLED_MAX 16u
#define WATCHY_PACKAGE_INDEX_MAGIC UINT32_C(0x57504b49)
#define WATCHY_PACKAGE_INDEX_VERSION 1u
#define WATCHY_PACKAGE_INDEX_WIRE_MAX 4096u
#define WATCHY_PACKAGE_TRANSACTION_PREFIX ".watchy-txn-"

typedef enum {
    WATCHY_PACKAGE_TYPE_WATCHFACE = 0,
    WATCHY_PACKAGE_TYPE_APP = 1,
} watchy_package_type_t;

enum {
    WATCHY_CAP_CANVAS = 1u << 0,
    WATCHY_CAP_CLOCK = 1u << 1,
    WATCHY_CAP_INPUT = 1u << 2,
    WATCHY_CAP_MOTION = 1u << 3,
    WATCHY_CAP_BATTERY = 1u << 4,
    WATCHY_CAP_HAPTICS = 1u << 5,
    WATCHY_CAP_STORAGE = 1u << 6,
    WATCHY_CAP_NETWORK = 1u << 7,
    WATCHY_CAP_BLUETOOTH = 1u << 8,
    WATCHY_CAP_SYSTEM = 1u << 9,
    WATCHY_CAP_KNOWN_MASK = (1u << 10) - 1u,
};

typedef struct {
    char path[WATCHY_PACKAGE_ASSET_PATH_MAX + 1u];
    uint32_t size;
} watchy_package_asset_t;

typedef struct {
    char id[WATCHY_PACKAGE_ID_MAX + 1u];
    char name[WATCHY_PACKAGE_NAME_MAX + 1u];
    char version[WATCHY_PACKAGE_VERSION_MAX + 1u];
    watchy_package_type_t type;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t capabilities;
    uint32_t max_runtime_bytes;
    watchy_package_asset_t assets[WATCHY_PACKAGE_ASSET_COUNT_MAX];
    size_t asset_count;
} watchy_package_manifest_t;

typedef struct {
    watchy_package_manifest_t manifest;
    const uint8_t *elf;
    uint32_t elf_size;
    const uint8_t *assets;
    uint32_t assets_size;
    uint32_t runtime_bytes;
} watchy_validated_package_t;

typedef struct {
    char package_ref[WATCHY_PACKAGE_REF_MAX + 1u];
    uint8_t incomplete_attempts;
    bool quarantined;
} watchy_package_health_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    char active_watchface[WATCHY_PACKAGE_REF_MAX + 1u];
    char pending_watchface[WATCHY_PACKAGE_REF_MAX + 1u];
    char prior_watchface[WATCHY_PACKAGE_REF_MAX + 1u];
    char installed[WATCHY_PACKAGE_INSTALLED_MAX][WATCHY_PACKAGE_REF_MAX + 1u];
    watchy_package_type_t installed_types[WATCHY_PACKAGE_INSTALLED_MAX];
    size_t installed_count;
    watchy_package_health_t health[WATCHY_PACKAGE_HEALTH_RECORD_MAX];
    size_t health_count;
} watchy_package_index_t;

typedef enum {
    WATCHY_INDEX_STORE_OK = 0,
    WATCHY_INDEX_STORE_NOT_FOUND = 1,
    WATCHY_INDEX_STORE_ERROR = -1,
} watchy_index_store_result_t;

typedef struct {
    watchy_index_store_result_t (*load)(void *context, watchy_package_index_t *out_index);
    bool (*save)(void *context, const watchy_package_index_t *index);
    void *context;
} watchy_package_index_store_t;

typedef struct {
    watchy_package_index_t index;
    watchy_package_index_t scratch;
    watchy_package_index_store_t store;
    bool initialized;
} watchy_package_index_manager_t;

#define WATCHY_PACKAGE_RTLD_NOW 2

typedef struct {
    void *(*open)(void *context, const char *path, int mode);
    void *(*symbol)(void *context, void *handle, const char *name);
    int (*close)(void *context, void *handle);
    bool (*readable)(void *context, const void *address, size_t size);
    bool (*writable)(void *context, void *address, size_t size);
    bool (*executable)(void *context, const void *address, size_t size);
    void *context;
} watchy_package_loader_api_t;

typedef struct {
    void (*before_callback)(void *context);
    bool (*after_callback)(void *context);
    bool (*ensure_current)(void *context);
    void *context;
} watchy_package_watchdog_api_t;

typedef enum {
    WATCHY_PACKAGE_RECONCILE_KEEP = 0,
    WATCHY_PACKAGE_RECONCILE_REMOVE_TRANSACTION = 1,
    WATCHY_PACKAGE_RECONCILE_REMOVE_UNINDEXED = 2,
    WATCHY_PACKAGE_RECONCILE_REMOVE_INVALID = 3,
} watchy_package_reconcile_action_t;

typedef struct {
    watchy_package_loader_api_t loader;
    watchy_package_watchdog_api_t watchdog;
    watchy_runtime_t runtime;
    void *handle;
    watchy_package_descriptor_v1_t descriptor;
    char identifier[WATCHY_PACKAGE_ID_MAX + 1u];
    char name[WATCHY_PACKAGE_NAME_MAX + 1u];
    char version[WATCHY_PACKAGE_VERSION_MAX + 1u];
    void *user_data;
    bool on_load_completed;
    bool initialized;
    bool poisoned;
} watchy_package_session_t;

typedef struct {
    bool (*write_file)(void *context,
                       const char *path,
                       const uint8_t *bytes,
                       size_t size,
                       bool read_only);
    bool (*write_file_exclusive)(void *context,
                                 const char *path,
                                 const uint8_t *bytes,
                                 size_t size);
    bool (*read_file)(void *context,
                      const char *path,
                      uint8_t *bytes,
                      size_t capacity,
                      size_t *out_size);
    bool (*mkdirs)(void *context, const char *path);
    bool (*mkdir_exclusive)(void *context, const char *path);
    bool (*sync_tree)(void *context, const char *path);
    bool (*rename_noreplace)(void *context, const char *source, const char *destination);
    bool (*path_exists)(void *context, const char *path);
    bool (*remove_tree)(void *context, const char *path);
    uint32_t (*unique_id)(void *context);
    void *context;
} watchy_package_fs_api_t;

/* Caller-owned install storage. One workspace may service one transaction at a
 * time; target code serializes it with the install/state mutex. */
typedef struct {
    watchy_validated_package_t package;
    uint8_t *stage_bytes;
    size_t stage_capacity;
    bool in_use;
} watchy_package_install_workspace_t;

typedef struct {
    const uint8_t *bytes;
    size_t size;
} watchy_byte_region_t;

typedef bool (*watchy_sha256_fn_t)(void *context,
                                   const watchy_byte_region_t *regions,
                                   size_t region_count,
                                   uint8_t out_digest[WATCHY_PACKAGE_DIGEST_SIZE]);

typedef struct {
    watchy_sha256_fn_t sha256;
    void *context;
} watchy_crypto_api_t;

bool watchy_package_id_valid(const char *identifier);
bool watchy_package_version_valid(const char *version);
bool watchy_package_relative_path_valid(const char *path);
bool watchy_package_transaction_name_valid(const char *name);
watchy_package_reconcile_action_t watchy_package_reconcile_version(
    const char *name,
    bool indexed);
watchy_package_status_t watchy_package_watchdog_ensure_current(
    const watchy_package_watchdog_api_t *watchdog);
watchy_package_status_t watchy_package_manifest_parse(const uint8_t *json,
                                                      size_t json_size,
                                                      watchy_package_manifest_t *out_manifest);
watchy_package_status_t watchy_package_elf_validate(const uint8_t *elf,
                                                    size_t elf_size,
                                                    uint32_t declared_runtime_bytes,
                                                    uint32_t *out_runtime_bytes);
watchy_package_status_t watchy_package_validate(const uint8_t *wpk,
                                                size_t wpk_size,
                                                const watchy_crypto_api_t *crypto,
                                                watchy_validated_package_t *out_package);
watchy_package_status_t watchy_package_index_init(watchy_package_index_manager_t *manager,
                                                  const watchy_package_index_store_t *store);
const watchy_package_index_t *watchy_package_index_snapshot(
    const watchy_package_index_manager_t *manager);
watchy_package_status_t watchy_package_select_watchface(watchy_package_index_manager_t *manager,
                                                        const char *package_ref);
watchy_package_status_t watchy_package_select_builtin(watchy_package_index_manager_t *manager);
watchy_package_status_t watchy_package_promote_pending(watchy_package_index_manager_t *manager,
                                                       const char *package_ref);
watchy_package_status_t watchy_package_rollback_pending(watchy_package_index_manager_t *manager,
                                                        const char *package_ref);
watchy_package_status_t watchy_package_begin_attempt(watchy_package_index_manager_t *manager,
                                                     const char *package_ref,
                                                     bool safe_mode);
watchy_package_status_t watchy_package_finish_attempt(watchy_package_index_manager_t *manager,
                                                      const char *package_ref,
                                                      bool clean_stop);
watchy_package_status_t watchy_package_finalize_watchface_attempt(
    watchy_package_index_manager_t *manager,
    const char *package_ref,
    bool pending,
    bool rendered,
    watchy_package_status_t lifecycle_status,
    watchy_package_status_t stop_status);
bool watchy_package_is_quarantined(const watchy_package_index_manager_t *manager,
                                   const char *package_ref);
watchy_package_status_t watchy_package_register_installed(
    watchy_package_index_manager_t *manager,
    const char *package_ref);
watchy_package_status_t watchy_package_unregister(watchy_package_index_manager_t *manager,
                                                  const char *package_ref);
watchy_package_status_t watchy_package_index_clear(watchy_package_index_manager_t *manager);
bool watchy_package_index_has_id(const watchy_package_index_manager_t *manager,
                                 const char *identifier);
watchy_package_status_t watchy_package_register_installed_typed(
    watchy_package_index_manager_t *manager,
    const char *package_ref,
    watchy_package_type_t type);
bool watchy_package_is_installed(const watchy_package_index_manager_t *manager,
                                 const char *package_ref,
                                 watchy_package_type_t *out_type);
watchy_package_status_t watchy_package_index_encode(
    const watchy_package_index_t *index,
    uint8_t *wire,
    size_t capacity,
    size_t *out_size);
watchy_package_status_t watchy_package_index_decode(const uint8_t *wire,
                                                    size_t size,
                                                    watchy_package_index_t *out_index);
watchy_package_status_t watchy_package_session_init(
    watchy_package_session_t *session,
    const watchy_package_loader_api_t *loader,
    const watchy_package_watchdog_api_t *watchdog);
watchy_package_status_t watchy_package_session_load(watchy_package_session_t *session,
                                                    const char *elf_path,
                                                    const watchy_package_manifest_t *manifest,
                                                    const watchy_host_caps_v1_t *host,
                                                    bool safe_mode,
                                                    bool quarantined);
watchy_package_status_t watchy_package_session_start(watchy_package_session_t *session);
watchy_package_status_t watchy_package_session_event(watchy_package_session_t *session,
                                                     const watchy_event_t *event);
watchy_package_status_t watchy_package_session_render(watchy_package_session_t *session,
                                                      watchy_canvas_t *canvas,
                                                      watchy_refresh_mode_t *mode);
watchy_package_status_t watchy_package_session_stop(watchy_package_session_t *session);
bool watchy_package_session_loaded(const watchy_package_session_t *session);
watchy_package_status_t watchy_package_install(watchy_package_index_manager_t *manager,
                                               const watchy_package_fs_api_t *filesystem,
                                               watchy_package_install_workspace_t *workspace,
                                               const uint8_t *wpk,
                                               size_t wpk_size,
                                               const watchy_crypto_api_t *crypto,
                                               char out_package_ref[WATCHY_PACKAGE_REF_MAX + 1u]);
watchy_package_status_t watchy_package_calculate_digest(
    const uint8_t *wpk,
    size_t wpk_size,
    const watchy_crypto_api_t *crypto,
    uint8_t out_digest[WATCHY_PACKAGE_DIGEST_SIZE]);
watchy_package_status_t watchy_package_digest_matches(const uint8_t *wpk,
                                                      size_t wpk_size,
                                                      const watchy_crypto_api_t *crypto);

#ifdef __cplusplus
}
#endif

#endif
