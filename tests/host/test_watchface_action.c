#include "watchy/watchface_action.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

typedef struct {
    watchy_package_catalog_t snapshot;
    watchy_package_status_t select_status;
    watchy_watchface_run_result_t run_result;
    unsigned select_builtin_calls;
    unsigned select_watchface_calls;
    unsigned run_calls;
    unsigned snapshot_calls;
    unsigned save_calls;
    unsigned force_full_calls;
    bool run_started_after_full_refresh;
    watchy_package_status_t import_status;
    watchy_package_status_t recovery_status;
    watchy_package_status_t snapshot_status;
    watchy_status_t save_status;
    unsigned import_calls;
    unsigned boot_snapshot_calls;
    unsigned recovery_calls;
    char boot_order[4];
    size_t boot_order_length;
    char selected_ref[WATCHY_PACKAGE_REF_MAX + 1u];
    watchy_settings_t saved_settings;
} fixture_t;

static watchy_package_status_t import_factory_seed(void *context) {
    fixture_t *fixture = context;
    ++fixture->import_calls;
    fixture->boot_order[fixture->boot_order_length++] = 'I';
    fixture->boot_order[fixture->boot_order_length] = '\0';
    return fixture->import_status;
}

static watchy_package_status_t boot_snapshot(void *context,
                                             watchy_package_catalog_t *out_catalog) {
    fixture_t *fixture = context;
    ++fixture->boot_snapshot_calls;
    fixture->boot_order[fixture->boot_order_length++] = 'S';
    fixture->boot_order[fixture->boot_order_length] = '\0';
    *out_catalog = fixture->snapshot;
    return fixture->snapshot_status;
}

static watchy_package_status_t recover_stale_pending(void *context) {
    fixture_t *fixture = context;
    ++fixture->recovery_calls;
    fixture->boot_order[fixture->boot_order_length++] = 'R';
    fixture->boot_order[fixture->boot_order_length] = '\0';
    return fixture->recovery_status;
}

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

static watchy_watchface_run_result_t run_watchface(void *context, bool safe_mode) {
    fixture_t *fixture = context;
    ++fixture->run_calls;
    fixture->run_started_after_full_refresh = fixture->force_full_calls != 0u;
    return safe_mode ? (watchy_watchface_run_result_t){0} : fixture->run_result;
}

static void force_full_refresh(void *context) {
    fixture_t *fixture = context;
    ++fixture->force_full_calls;
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
    return fixture->save_status;
}

static watchy_watchface_action_ops_t operations(fixture_t *fixture) {
    return (watchy_watchface_action_ops_t){
        .select_builtin = select_builtin,
        .select_watchface = select_watchface,
        .run_watchface = run_watchface,
        .force_full_refresh = force_full_refresh,
        .snapshot = snapshot,
        .save_settings = save_settings,
        .context = fixture,
    };
}

static watchy_watchface_boot_ops_t boot_operations(fixture_t *fixture) {
    return (watchy_watchface_boot_ops_t){
        .import_factory_seed = import_factory_seed,
        .recover_stale_pending = recover_stale_pending,
        .snapshot = boot_snapshot,
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
    fixture_t fixture = {
        .select_status = WATCHY_PACKAGE_OK,
        .run_result = {.rendered = true},
    };
    watchy_package_catalog_t catalog;
    watchy_settings_t settings = {0};
    watchy_watchface_run_result_t result = {0};
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

    CHECK(watchy_watchface_action_apply(&request, false, false, &settings, &catalog,
                                        &ops, &result) == WATCHY_STATUS_OK);
    CHECK(result.rendered && result.cancelled_buttons == 0u);
    CHECK(fixture.force_full_calls == 1u);
    CHECK(fixture.run_started_after_full_refresh);
    CHECK(fixture.select_watchface_calls == 1u && fixture.run_calls == 1u);
    CHECK(fixture.snapshot_calls == 1u && fixture.save_calls == 1u);
    CHECK(strcmp(fixture.selected_ref, "face.new@1.0.0") == 0);
    CHECK(strcmp(settings.active_watchface, "face.new@1.0.0") == 0);
    CHECK(strcmp(fixture.saved_settings.active_watchface, "face.new@1.0.0") == 0);
    CHECK(catalog.packages[1].active);
    return 0;
}

static int test_failed_render_preserves_previous_watchface(void) {
    fixture_t fixture = {.select_status = WATCHY_PACKAGE_OK};
    watchy_package_catalog_t catalog;
    watchy_settings_t settings = {0};
    watchy_watchface_run_result_t result = {.rendered = true};
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

    CHECK(watchy_watchface_action_apply(&request, false, false, &settings, &catalog,
                                        &ops, &result) == WATCHY_STATUS_INVALID_STATE);
    CHECK(!result.rendered && result.cancelled_buttons == 0u);
    CHECK(fixture.force_full_calls == 1u);
    CHECK(fixture.snapshot_calls == 1u && fixture.save_calls == 0u);
    CHECK(strcmp(settings.active_watchface, "face.old@1.0.0") == 0);
    CHECK(catalog.packages[0].active);
    return 0;
}

static int test_builtin_selection_clears_persisted_package(void) {
    fixture_t fixture = {.select_status = WATCHY_PACKAGE_OK};
    watchy_package_catalog_t catalog;
    watchy_settings_t settings = {0};
    watchy_watchface_run_result_t result = {.rendered = true};
    catalog_with_active(&catalog, "face.old@1.0.0");
    catalog_with_active(&fixture.snapshot, "");
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.old@1.0.0");
    const watchy_shell_action_request_t request = {
        .action = WATCHY_SHELL_ACTION_SELECT_BUILTIN,
    };
    const watchy_watchface_action_ops_t ops = operations(&fixture);

    CHECK(watchy_watchface_action_apply(&request, false, false, &settings, &catalog,
                                        &ops, &result) == WATCHY_STATUS_OK);
    CHECK(!result.rendered && result.cancelled_buttons == 0u);
    CHECK(fixture.force_full_calls == 1u);
    CHECK(fixture.select_builtin_calls == 1u && fixture.run_calls == 0u);
    CHECK(fixture.snapshot_calls == 1u && fixture.save_calls == 1u);
    CHECK(settings.active_watchface[0] == '\0');
    return 0;
}

static int test_cancelled_activation_replays_the_button_without_package_failure(void) {
    fixture_t fixture = {
        .select_status = WATCHY_PACKAGE_OK,
        .run_result = {
            .rendered = true,
            .cancelled_buttons = WATCHY_BUTTON_MASK_MENU,
        },
    };
    watchy_package_catalog_t catalog;
    watchy_settings_t settings = {0};
    watchy_watchface_run_result_t result = {0};
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

    CHECK(watchy_watchface_action_apply(&request, false, false, &settings, &catalog,
                                        &ops, &result) == WATCHY_STATUS_CANCELLED);
    CHECK(result.cancelled_buttons == WATCHY_BUTTON_MASK_MENU);
    CHECK(strcmp(settings.active_watchface, "face.old@1.0.0") == 0);
    CHECK(fixture.save_calls == 0u);
    return 0;
}

static int test_active_package_return_forces_full_refresh_before_render(void) {
    fixture_t fixture = {
        .run_result = {.rendered = true},
    };
    watchy_package_catalog_t catalog;
    watchy_watchface_run_result_t result = {0};
    catalog_with_active(&catalog, "face.new@1.0.0");
    const watchy_watchface_action_ops_t ops = operations(&fixture);

    CHECK(watchy_watchface_run_active(false, false, true, &catalog, &ops, &result) ==
          WATCHY_STATUS_OK);
    CHECK(result.rendered);
    CHECK(fixture.force_full_calls == 1u);
    CHECK(fixture.run_calls == 1u);

    catalog_with_active(&catalog, "");
    CHECK(watchy_watchface_run_active(false, false, true, &catalog, &ops, &result) ==
          WATCHY_STATUS_UNSUPPORTED);
    CHECK(fixture.force_full_calls == 1u);
    CHECK(fixture.run_calls == 1u);
    return 0;
}

static int test_normal_boot_imports_seed_before_first_catalog_snapshot(void) {
    fixture_t fixture = {
        .import_status = WATCHY_PACKAGE_OK,
        .snapshot_status = WATCHY_PACKAGE_OK,
    };
    watchy_package_catalog_t catalog = {0};
    watchy_watchface_boot_result_t result = {0};
    catalog_with_active(&fixture.snapshot, "face.old@1.0.0");
    const watchy_watchface_boot_ops_t ops = boot_operations(&fixture);

    CHECK(watchy_watchface_boot_prepare(false, &catalog, &ops, &result) ==
          WATCHY_STATUS_OK);
    CHECK(strcmp(fixture.boot_order, "IRS") == 0);
    CHECK(fixture.import_calls == 1u && fixture.recovery_calls == 1u &&
          fixture.boot_snapshot_calls == 1u);
    CHECK(result.seed_import_attempted && !result.seed_import_failed);
    CHECK(result.catalog_readable && !result.package_warning);
    CHECK(catalog.packages[0].active);
    return 0;
}

static int test_safe_boot_skips_seed_import_but_keeps_recovery_catalog(void) {
    fixture_t fixture = {
        .import_status = WATCHY_PACKAGE_ERR_STATE,
        .snapshot_status = WATCHY_PACKAGE_OK,
    };
    watchy_package_catalog_t catalog = {0};
    watchy_watchface_boot_result_t result = {0};
    catalog_with_active(&fixture.snapshot, "face.old@1.0.0");
    const watchy_watchface_boot_ops_t ops = boot_operations(&fixture);

    CHECK(watchy_watchface_boot_prepare(true, &catalog, &ops, &result) ==
          WATCHY_STATUS_OK);
    CHECK(strcmp(fixture.boot_order, "S") == 0);
    CHECK(fixture.import_calls == 0u && fixture.recovery_calls == 0u &&
          fixture.boot_snapshot_calls == 1u);
    CHECK(!result.seed_import_attempted && !result.seed_import_failed);
    CHECK(result.catalog_readable && !result.package_warning);
    CHECK(catalog.packages[0].active);
    return 0;
}

static int test_seed_failure_is_visible_and_does_not_hide_readable_catalog(void) {
    fixture_t fixture = {
        .import_status = WATCHY_PACKAGE_ERR_DIGEST,
        .snapshot_status = WATCHY_PACKAGE_OK,
    };
    watchy_package_catalog_t catalog = {0};
    watchy_watchface_boot_result_t result = {0};
    catalog_with_active(&fixture.snapshot, "face.old@1.0.0");
    const watchy_watchface_boot_ops_t ops = boot_operations(&fixture);

    CHECK(watchy_watchface_boot_prepare(false, &catalog, &ops, &result) ==
          WATCHY_STATUS_OK);
    CHECK(strcmp(fixture.boot_order, "IRS") == 0);
    CHECK(result.seed_import_attempted && result.seed_import_failed);
    CHECK(result.catalog_readable && result.package_warning);
    CHECK(catalog.packages[0].active);
    return 0;
}

static int test_unreadable_boot_catalog_is_a_bounded_package_warning(void) {
    fixture_t fixture = {
        .import_status = WATCHY_PACKAGE_OK,
        .snapshot_status = WATCHY_PACKAGE_ERR_STORE,
    };
    watchy_package_catalog_t catalog = {0};
    watchy_watchface_boot_result_t result = {0};
    const watchy_watchface_boot_ops_t ops = boot_operations(&fixture);

    CHECK(watchy_watchface_boot_prepare(false, &catalog, &ops, &result) ==
          WATCHY_STATUS_OK);
    CHECK(strcmp(fixture.boot_order, "IRS") == 0);
    CHECK(!result.catalog_readable && result.package_warning);
    return 0;
}

static int test_active_removal_reconciles_to_hairline_setting(void) {
    fixture_t fixture = {0};
    watchy_package_catalog_t catalog = {0};
    watchy_settings_t settings = {0};
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.old@1.0.0");

    CHECK(watchy_watchface_reconcile_settings(
              &settings, &catalog, save_settings, &fixture) == WATCHY_STATUS_OK);
    CHECK(settings.active_watchface[0] == '\0');
    CHECK(fixture.saved_settings.active_watchface[0] == '\0');
    CHECK(fixture.save_calls == 1u);
    return 0;
}

static int test_non_active_removal_leaves_setting_unchanged(void) {
    fixture_t fixture = {0};
    watchy_package_catalog_t catalog = {0};
    watchy_settings_t settings = {0};
    catalog_with_active(&catalog, "face.old@1.0.0");
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.old@1.0.0");

    CHECK(watchy_watchface_reconcile_settings(
              &settings, &catalog, save_settings, &fixture) == WATCHY_STATUS_OK);
    CHECK(strcmp(settings.active_watchface, "face.old@1.0.0") == 0);
    CHECK(fixture.save_calls == 0u);
    return 0;
}

static int test_portal_pending_candidate_never_replaces_active_setting(void) {
    fixture_t fixture = {0};
    watchy_package_catalog_t catalog = {0};
    watchy_settings_t settings = {0};
    catalog_with_active(&catalog, "face.old@1.0.0");
    catalog.packages[1].pending = true;
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.old@1.0.0");

    CHECK(watchy_watchface_reconcile_settings(
              &settings, &catalog, save_settings, &fixture) == WATCHY_STATUS_OK);
    CHECK(strcmp(settings.active_watchface, "face.old@1.0.0") == 0);
    CHECK(fixture.save_calls == 0u);
    return 0;
}

static int test_stale_pending_recovery_failure_blocks_package_execution(void) {
    fixture_t fixture = {
        .import_status = WATCHY_PACKAGE_OK,
        .recovery_status = WATCHY_PACKAGE_ERR_STORE,
        .snapshot_status = WATCHY_PACKAGE_OK,
    };
    watchy_package_catalog_t catalog = {0};
    watchy_watchface_boot_result_t result = {0};
    catalog_with_active(&fixture.snapshot, "face.old@1.0.0");
    fixture.snapshot.packages[1].pending = true;
    const watchy_watchface_boot_ops_t ops = boot_operations(&fixture);

    CHECK(watchy_watchface_boot_prepare(false, &catalog, &ops, &result) ==
          WATCHY_STATUS_OK);
    CHECK(result.stale_pending_recovery_failed && result.package_warning);
    CHECK(!watchy_watchface_boot_allows_package_execution(false, &result));
    CHECK(strcmp(fixture.boot_order, "IRS") == 0);
    return 0;
}

static int test_blocked_wake_rejects_watchface_selection_without_callbacks(void) {
    fixture_t fixture = {
        .select_status = WATCHY_PACKAGE_OK,
        .run_result = {.rendered = true},
    };
    watchy_package_catalog_t catalog = {0};
    watchy_settings_t settings = {0};
    watchy_watchface_run_result_t result = {0};
    catalog_with_active(&catalog, "face.old@1.0.0");
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.old@1.0.0");
    const watchy_shell_action_request_t request = {
        .action = WATCHY_SHELL_ACTION_SELECT_WATCHFACE,
        .has_package = true,
        .package_ref = "face.new@1.0.0",
    };
    const watchy_watchface_action_ops_t ops = operations(&fixture);

    CHECK(watchy_watchface_action_apply(&request, false, true, &settings,
                                        &catalog, &ops, &result) ==
          WATCHY_STATUS_UNSUPPORTED);
    CHECK(fixture.select_watchface_calls == 0u && fixture.run_calls == 0u);
    CHECK(fixture.force_full_calls == 0u && fixture.snapshot_calls == 0u);
    CHECK(fixture.save_calls == 0u);
    CHECK(strcmp(settings.active_watchface, "face.old@1.0.0") == 0);
    return 0;
}

static int test_blocked_wake_still_allows_hairline_selection(void) {
    fixture_t fixture = {.select_status = WATCHY_PACKAGE_OK};
    watchy_package_catalog_t catalog = {0};
    watchy_settings_t settings = {0};
    watchy_watchface_run_result_t result = {0};
    catalog_with_active(&catalog, "face.old@1.0.0");
    catalog_with_active(&fixture.snapshot, "");
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.old@1.0.0");
    const watchy_shell_action_request_t request = {
        .action = WATCHY_SHELL_ACTION_SELECT_BUILTIN,
    };
    const watchy_watchface_action_ops_t ops = operations(&fixture);

    CHECK(watchy_watchface_action_apply(&request, false, true, &settings,
                                        &catalog, &ops, &result) == WATCHY_STATUS_OK);
    CHECK(fixture.select_builtin_calls == 1u && fixture.run_calls == 0u);
    CHECK(settings.active_watchface[0] == '\0');
    return 0;
}

static int test_blocked_wake_rejects_active_watchface_return_without_run(void) {
    fixture_t fixture = {.run_result = {.rendered = true}};
    watchy_package_catalog_t catalog = {0};
    watchy_watchface_run_result_t result = {0};
    catalog_with_active(&catalog, "face.old@1.0.0");
    const watchy_watchface_action_ops_t ops = operations(&fixture);

    CHECK(watchy_watchface_run_active(false, true, true, &catalog, &ops,
                                      &result) == WATCHY_STATUS_UNSUPPORTED);
    CHECK(fixture.run_calls == 0u && fixture.force_full_calls == 0u);
    return 0;
}

typedef struct {
    unsigned calls;
} app_fixture_t;

static bool run_app(void *context, const char *package_ref) {
    app_fixture_t *fixture = context;
    ++fixture->calls;
    return strcmp(package_ref, "app.demo@1.0.0") == 0;
}

static int test_blocked_wake_rejects_app_without_invoking_runner(void) {
    app_fixture_t fixture = {0};
    CHECK(watchy_package_app_action_apply(false, true, "app.demo@1.0.0",
                                          run_app, &fixture) ==
          WATCHY_STATUS_UNSUPPORTED);
    CHECK(fixture.calls == 0u);
    CHECK(watchy_package_app_action_apply(false, false, "app.demo@1.0.0",
                                          run_app, &fixture) == WATCHY_STATUS_OK);
    CHECK(fixture.calls == 1u);
    return 0;
}

typedef struct {
    watchy_status_t stop_status;
    watchy_status_t load_status;
    watchy_package_status_t snapshot_status;
    watchy_status_t save_status;
    watchy_settings_t loaded_settings;
    watchy_package_catalog_t snapshot;
    char order[5];
    size_t order_length;
    unsigned save_calls;
} portal_exit_fixture_t;

static void portal_record(portal_exit_fixture_t *fixture, char call) {
    fixture->order[fixture->order_length++] = call;
    fixture->order[fixture->order_length] = '\0';
}

static watchy_status_t portal_stop(void *context) {
    portal_exit_fixture_t *fixture = context;
    portal_record(fixture, 'T');
    return fixture->stop_status;
}

static watchy_status_t portal_load(void *context,
                                   watchy_settings_t *out_settings) {
    portal_exit_fixture_t *fixture = context;
    portal_record(fixture, 'L');
    if (fixture->load_status == WATCHY_STATUS_OK) {
        *out_settings = fixture->loaded_settings;
    }
    return fixture->load_status;
}

static watchy_package_status_t portal_snapshot(
    void *context,
    watchy_package_catalog_t *out_catalog) {
    portal_exit_fixture_t *fixture = context;
    portal_record(fixture, 'C');
    if (fixture->snapshot_status == WATCHY_PACKAGE_OK) {
        *out_catalog = fixture->snapshot;
    }
    return fixture->snapshot_status;
}

static watchy_status_t portal_save(void *context,
                                   const watchy_settings_t *settings) {
    portal_exit_fixture_t *fixture = context;
    portal_record(fixture, 'S');
    ++fixture->save_calls;
    (void)settings;
    return fixture->save_status;
}

static watchy_watchface_portal_exit_ops_t portal_operations(
    portal_exit_fixture_t *fixture) {
    return (watchy_watchface_portal_exit_ops_t){
        .stop_portal = portal_stop,
        .load_settings = portal_load,
        .snapshot = portal_snapshot,
        .save_settings = portal_save,
        .context = fixture,
    };
}

static int test_portal_stop_failure_still_reloads_snapshots_and_reconciles(void) {
    portal_exit_fixture_t fixture = {
        .stop_status = WATCHY_STATUS_INVALID_STATE,
        .load_status = WATCHY_STATUS_OK,
        .snapshot_status = WATCHY_PACKAGE_OK,
    };
    watchy_settings_t settings = {0};
    watchy_package_catalog_t catalog = {0};
    watchy_watchface_portal_exit_result_t result = {0};
    snprintf(fixture.loaded_settings.active_watchface,
             sizeof(fixture.loaded_settings.active_watchface),
             "%s", "face.removed@1.0.0");
    watchy_watchface_portal_exit_ops_t ops = portal_operations(&fixture);

    CHECK(watchy_watchface_portal_exit(&settings, &catalog, &ops, &result) ==
          WATCHY_STATUS_OK);
    CHECK(strcmp(fixture.order, "TLCS") == 0);
    CHECK(result.settings_reloaded && result.catalog_refreshed);
    CHECK(result.error == WATCHY_SHELL_ERROR_PORTAL);
    CHECK(settings.active_watchface[0] == '\0' && fixture.save_calls == 1u);
    return 0;
}

static int test_portal_reload_failure_still_snapshots_and_reconciles_active(void) {
    portal_exit_fixture_t fixture = {
        .load_status = WATCHY_STATUS_INVALID_STATE,
        .snapshot_status = WATCHY_PACKAGE_OK,
    };
    watchy_settings_t settings = {0};
    watchy_package_catalog_t catalog = {0};
    watchy_watchface_portal_exit_result_t result = {0};
    catalog_with_active(&fixture.snapshot, "face.old@1.0.0");
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.pending@1.0.0");
    fixture.snapshot.packages[1].pending = true;
    const watchy_watchface_portal_exit_ops_t ops = portal_operations(&fixture);

    CHECK(watchy_watchface_portal_exit(&settings, &catalog, &ops, &result) ==
          WATCHY_STATUS_OK);
    CHECK(strcmp(fixture.order, "TLCS") == 0);
    CHECK(!result.settings_reloaded && result.catalog_refreshed);
    CHECK(result.error == WATCHY_SHELL_ERROR_SETTINGS_LOAD);
    CHECK(strcmp(settings.active_watchface, "face.old@1.0.0") == 0);
    CHECK(fixture.save_calls == 1u);
    return 0;
}

static int test_portal_exit_error_precedence_keeps_reconciliation_attempt(void) {
    portal_exit_fixture_t fixture = {
        .stop_status = WATCHY_STATUS_INVALID_STATE,
        .load_status = WATCHY_STATUS_INVALID_STATE,
        .snapshot_status = WATCHY_PACKAGE_OK,
        .save_status = WATCHY_STATUS_INVALID_STATE,
    };
    watchy_settings_t settings = {0};
    watchy_package_catalog_t catalog = {0};
    watchy_watchface_portal_exit_result_t result = {0};
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.removed@1.0.0");
    watchy_watchface_portal_exit_ops_t ops = portal_operations(&fixture);

    CHECK(watchy_watchface_portal_exit(&settings, &catalog, &ops, &result) ==
          WATCHY_STATUS_OK);
    CHECK(strcmp(fixture.order, "TLCS") == 0 && fixture.save_calls == 1u);
    CHECK(result.error == WATCHY_SHELL_ERROR_SETTINGS_SAVE);

    fixture = (portal_exit_fixture_t){
        .stop_status = WATCHY_STATUS_INVALID_STATE,
        .load_status = WATCHY_STATUS_INVALID_STATE,
        .snapshot_status = WATCHY_PACKAGE_ERR_STORE,
    };
    settings = (watchy_settings_t){0};
    catalog = (watchy_package_catalog_t){0};
    ops = portal_operations(&fixture);
    CHECK(watchy_watchface_portal_exit(&settings, &catalog, &ops, &result) ==
          WATCHY_STATUS_OK);
    CHECK(strcmp(fixture.order, "TLC") == 0);
    CHECK(result.error == WATCHY_SHELL_ERROR_PACKAGE);
    return 0;
}

static watchy_package_status_t mutation_snapshot(void *context,
                                                 watchy_package_catalog_t *out_catalog) {
    return snapshot(context, out_catalog);
}

static int reconcile_mutation(fixture_t *fixture,
                              watchy_package_status_t status,
                              bool index_mutated,
                              watchy_settings_t *settings,
                              watchy_package_catalog_t *catalog,
                              watchy_watchface_catalog_mutation_result_t *result) {
    const watchy_package_mutation_result_t mutation = {
        .status = status,
        .index_mutated = index_mutated,
    };
    return watchy_watchface_reconcile_catalog_mutation(
               mutation, settings, catalog, mutation_snapshot, save_settings,
               fixture, result) == WATCHY_STATUS_OK ? 0 : 1;
}

static int test_partial_active_removal_failure_reconciles_empty_setting(void) {
    fixture_t fixture = {0};
    watchy_package_catalog_t catalog = {0};
    watchy_settings_t settings = {0};
    watchy_watchface_catalog_mutation_result_t result = {0};
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.old@1.0.0");

    CHECK(reconcile_mutation(&fixture, WATCHY_PACKAGE_ERR_FILESYSTEM, true,
                             &settings, &catalog, &result) == 0);
    CHECK(result.catalog_refreshed && result.package_failed);
    CHECK(!result.settings_save_failed);
    CHECK(settings.active_watchface[0] == '\0' && fixture.save_calls == 1u);
    return 0;
}

static int test_partial_purge_failure_reconciles_empty_setting(void) {
    fixture_t fixture = {0};
    watchy_package_catalog_t catalog = {0};
    watchy_settings_t settings = {0};
    watchy_watchface_catalog_mutation_result_t result = {0};
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.old@1.0.0");

    CHECK(reconcile_mutation(&fixture, WATCHY_PACKAGE_ERR_FILESYSTEM, true,
                             &settings, &catalog, &result) == 0);
    CHECK(result.package_failed && settings.active_watchface[0] == '\0');
    return 0;
}

static int test_partial_mutation_save_failure_has_explicit_precedence(void) {
    fixture_t fixture = {.save_status = WATCHY_STATUS_INVALID_STATE};
    watchy_package_catalog_t catalog = {0};
    watchy_settings_t settings = {0};
    watchy_watchface_catalog_mutation_result_t result = {0};
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.old@1.0.0");

    CHECK(reconcile_mutation(&fixture, WATCHY_PACKAGE_ERR_FILESYSTEM, true,
                             &settings, &catalog, &result) == 0);
    CHECK(result.package_failed && result.settings_save_failed);
    return 0;
}

static int test_non_active_removal_mutation_keeps_active_setting(void) {
    fixture_t fixture = {0};
    watchy_package_catalog_t catalog = {0};
    watchy_settings_t settings = {0};
    watchy_watchface_catalog_mutation_result_t result = {0};
    catalog_with_active(&fixture.snapshot, "face.old@1.0.0");
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.old@1.0.0");

    CHECK(reconcile_mutation(&fixture, WATCHY_PACKAGE_OK, true,
                             &settings, &catalog, &result) == 0);
    CHECK(result.catalog_refreshed && !result.package_failed);
    CHECK(fixture.save_calls == 0u);
    CHECK(strcmp(settings.active_watchface, "face.old@1.0.0") == 0);
    return 0;
}

static int test_rollback_reconciles_persisted_setting_to_prior_active(void) {
    fixture_t fixture = {0};
    watchy_package_catalog_t catalog = {0};
    watchy_settings_t settings = {0};
    catalog_with_active(&catalog, "face.old@1.0.0");
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.new@1.0.0");

    CHECK(watchy_watchface_reconcile_settings(
              &settings, &catalog, save_settings, &fixture) == WATCHY_STATUS_OK);
    CHECK(strcmp(settings.active_watchface, "face.old@1.0.0") == 0);
    CHECK(strcmp(fixture.saved_settings.active_watchface,
                 "face.old@1.0.0") == 0);
    CHECK(fixture.save_calls == 1u);
    return 0;
}

static int test_reconcile_surfaces_settings_save_failure(void) {
    fixture_t fixture = {.save_status = WATCHY_STATUS_INVALID_STATE};
    watchy_package_catalog_t catalog = {0};
    watchy_settings_t settings = {0};
    catalog_with_active(&catalog, "face.new@1.0.0");
    snprintf(settings.active_watchface, sizeof(settings.active_watchface),
             "%s", "face.old@1.0.0");

    CHECK(watchy_watchface_reconcile_settings(
              &settings, &catalog, save_settings, &fixture) ==
          WATCHY_STATUS_INVALID_STATE);
    CHECK(strcmp(settings.active_watchface, "face.new@1.0.0") == 0);
    CHECK(fixture.save_calls == 1u);
    return 0;
}

static int test_activation_identifies_settings_save_failure(void) {
    fixture_t fixture = {
        .select_status = WATCHY_PACKAGE_OK,
        .run_result = {.rendered = true},
        .save_status = WATCHY_STATUS_INVALID_STATE,
    };
    watchy_package_catalog_t catalog = {0};
    watchy_settings_t settings = {0};
    watchy_watchface_run_result_t result = {0};
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

    CHECK(watchy_watchface_action_apply(&request, false, false, &settings, &catalog,
                                        &ops, &result) ==
          WATCHY_STATUS_INVALID_STATE);
    CHECK(result.rendered && result.settings_save_failed);
    CHECK(fixture.save_calls == 1u);
    return 0;
}

int main(void) {
    CHECK(test_successful_watchface_selection_promotes_and_persists() == 0);
    CHECK(test_failed_render_preserves_previous_watchface() == 0);
    CHECK(test_builtin_selection_clears_persisted_package() == 0);
    CHECK(test_cancelled_activation_replays_the_button_without_package_failure() == 0);
    CHECK(test_active_package_return_forces_full_refresh_before_render() == 0);
    CHECK(test_normal_boot_imports_seed_before_first_catalog_snapshot() == 0);
    CHECK(test_safe_boot_skips_seed_import_but_keeps_recovery_catalog() == 0);
    CHECK(test_seed_failure_is_visible_and_does_not_hide_readable_catalog() == 0);
    CHECK(test_unreadable_boot_catalog_is_a_bounded_package_warning() == 0);
    CHECK(test_active_removal_reconciles_to_hairline_setting() == 0);
    CHECK(test_non_active_removal_leaves_setting_unchanged() == 0);
    CHECK(test_portal_pending_candidate_never_replaces_active_setting() == 0);
    CHECK(test_stale_pending_recovery_failure_blocks_package_execution() == 0);
    CHECK(test_blocked_wake_rejects_watchface_selection_without_callbacks() == 0);
    CHECK(test_blocked_wake_still_allows_hairline_selection() == 0);
    CHECK(test_blocked_wake_rejects_active_watchface_return_without_run() == 0);
    CHECK(test_blocked_wake_rejects_app_without_invoking_runner() == 0);
    CHECK(test_portal_stop_failure_still_reloads_snapshots_and_reconciles() == 0);
    CHECK(test_portal_reload_failure_still_snapshots_and_reconciles_active() == 0);
    CHECK(test_portal_exit_error_precedence_keeps_reconciliation_attempt() == 0);
    CHECK(test_partial_active_removal_failure_reconciles_empty_setting() == 0);
    CHECK(test_partial_purge_failure_reconciles_empty_setting() == 0);
    CHECK(test_partial_mutation_save_failure_has_explicit_precedence() == 0);
    CHECK(test_non_active_removal_mutation_keeps_active_setting() == 0);
    CHECK(test_rollback_reconciles_persisted_setting_to_prior_active() == 0);
    CHECK(test_reconcile_surfaces_settings_save_failure() == 0);
    CHECK(test_activation_identifies_settings_save_failure() == 0);
    puts("watchface action tests passed");
    return 0;
}
