#include "watchy/watchface_action.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

typedef struct {
    watchy_package_catalog_t snapshot;
    watchy_package_status_t select_status;
    bool run_result;
    unsigned select_builtin_calls;
    unsigned select_watchface_calls;
    unsigned run_calls;
    unsigned snapshot_calls;
    unsigned save_calls;
    char selected_ref[WATCHY_PACKAGE_REF_MAX + 1u];
    watchy_settings_t saved_settings;
} fixture_t;

static watchy_package_status_t select_builtin(void *context) {
    fixture_t *fixture = context;
    ++fixture->select_builtin_calls;
    return fixture->select_status;
}

static watchy_package_status_t select_watchface(void *context, const char *package_ref) {
    fixture_t *fixture = context;
    ++fixture->select_watchface_calls;
    snprintf(fixture->selected_ref, sizeof(fixture->selected_ref), "%s", package_ref);
    return fixture->select_status;
}

static bool run_watchface(void *context, bool safe_mode) {
    fixture_t *fixture = context;
    ++fixture->run_calls;
    return !safe_mode && fixture->run_result;
}

static watchy_package_status_t snapshot(void *context,
                                        watchy_package_catalog_t *out_catalog) {
    fixture_t *fixture = context;
    ++fixture->snapshot_calls;
    *out_catalog = fixture->snapshot;
    return WATCHY_PACKAGE_OK;
}

static watchy_status_t save_settings(void *context,
                                     const watchy_settings_t *settings) {
    fixture_t *fixture = context;
    ++fixture->save_calls;
    fixture->saved_settings = *settings;
    return WATCHY_STATUS_OK;
}

static watchy_watchface_action_ops_t operations(fixture_t *fixture) {
    return (watchy_watchface_action_ops_t){
        .select_builtin = select_builtin,
        .select_watchface = select_watchface,
        .run_watchface = run_watchface,
        .snapshot = snapshot,
        .save_settings = save_settings,
        .context = fixture,
    };
}

static void catalog_with_active(watchy_package_catalog_t *catalog,
                                const char *active_ref) {
    memset(catalog, 0, sizeof(*catalog));
    catalog->count = 2u;
    snprintf(catalog->packages[0].package_ref,
             sizeof(catalog->packages[0].package_ref), "%s", "face.old@1.0.0");
    snprintf(catalog->packages[1].package_ref,
             sizeof(catalog->packages[1].package_ref), "%s", "face.new@1.0.0");
    catalog->packages[0].active = strcmp(active_ref, "face.old@1.0.0") == 0;
    catalog->packages[1].active = strcmp(active_ref, "face.new@1.0.0") == 0;
}

static int test_successful_watchface_selection_promotes_and_persists(void) {
    fixture_t fixture = {.select_status = WATCHY_PACKAGE_OK, .run_result = true};
    watchy_package_catalog_t catalog;
    watchy_settings_t settings = {0};
    bool rendered = false;
    catalog_with_active(&catalog, "face.old@1.0.0");
    catalog_with_active(&fixture.snapshot, "face.new@1.0.0");
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.old@1.0.0");
    const watchy_shell_action_request_t request = {
        .action = WATCHY_SHELL_ACTION_SELECT_WATCHFACE,
        .has_package = true,
        .package_ref = "face.new@1.0.0",
    };
    const watchy_watchface_action_ops_t ops = operations(&fixture);

    CHECK(watchy_watchface_action_apply(&request, false, &settings, &catalog,
                                        &ops, &rendered) == WATCHY_STATUS_OK);
    CHECK(rendered);
    CHECK(fixture.select_watchface_calls == 1u && fixture.run_calls == 1u);
    CHECK(fixture.snapshot_calls == 1u && fixture.save_calls == 1u);
    CHECK(strcmp(fixture.selected_ref, "face.new@1.0.0") == 0);
    CHECK(strcmp(settings.active_watchface, "face.new@1.0.0") == 0);
    CHECK(strcmp(fixture.saved_settings.active_watchface, "face.new@1.0.0") == 0);
    CHECK(catalog.packages[1].active);
    return 0;
}

static int test_failed_render_preserves_previous_watchface(void) {
    fixture_t fixture = {.select_status = WATCHY_PACKAGE_OK, .run_result = false};
    watchy_package_catalog_t catalog;
    watchy_settings_t settings = {0};
    bool rendered = true;
    catalog_with_active(&catalog, "face.old@1.0.0");
    catalog_with_active(&fixture.snapshot, "face.old@1.0.0");
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.old@1.0.0");
    const watchy_shell_action_request_t request = {
        .action = WATCHY_SHELL_ACTION_SELECT_WATCHFACE,
        .has_package = true,
        .package_ref = "face.new@1.0.0",
    };
    const watchy_watchface_action_ops_t ops = operations(&fixture);

    CHECK(watchy_watchface_action_apply(&request, false, &settings, &catalog,
                                        &ops, &rendered) == WATCHY_STATUS_INVALID_STATE);
    CHECK(!rendered);
    CHECK(fixture.snapshot_calls == 1u && fixture.save_calls == 0u);
    CHECK(strcmp(settings.active_watchface, "face.old@1.0.0") == 0);
    CHECK(catalog.packages[0].active);
    return 0;
}

static int test_builtin_selection_clears_persisted_package(void) {
    fixture_t fixture = {.select_status = WATCHY_PACKAGE_OK};
    watchy_package_catalog_t catalog;
    watchy_settings_t settings = {0};
    bool rendered = true;
    catalog_with_active(&catalog, "face.old@1.0.0");
    catalog_with_active(&fixture.snapshot, "");
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.old@1.0.0");
    const watchy_shell_action_request_t request = {
        .action = WATCHY_SHELL_ACTION_SELECT_BUILTIN,
    };
    const watchy_watchface_action_ops_t ops = operations(&fixture);

    CHECK(watchy_watchface_action_apply(&request, false, &settings, &catalog,
                                        &ops, &rendered) == WATCHY_STATUS_OK);
    CHECK(!rendered);
    CHECK(fixture.select_builtin_calls == 1u && fixture.run_calls == 0u);
    CHECK(fixture.snapshot_calls == 1u && fixture.save_calls == 1u);
    CHECK(settings.active_watchface[0] == '\0');
    return 0;
}

int main(void) {
    CHECK(test_successful_watchface_selection_promotes_and_persists() == 0);
    CHECK(test_failed_render_preserves_previous_watchface() == 0);
    CHECK(test_builtin_selection_clears_persisted_package() == 0);
    puts("watchface action tests passed");
    return 0;
}
