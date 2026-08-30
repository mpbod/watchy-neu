#include "watchy/shell_render.h"

#include "watchy/ui.h"

#include <stdio.h>
#include <string.h>

static void title(watchy_canvas_t *canvas, const char *text) {
    watchy_ui_draw_text(canvas, 7, 6, text, 2u, true);
    watchy_ui_rect(canvas, 6, 23, 188, 2, true);
}

static void row(watchy_canvas_t *canvas, int index, const char *text, bool selected) {
    const int16_t y = (int16_t)(31 + index * 24);
    if (selected) {
        watchy_ui_rect(canvas, 4, (int16_t)(y - 4), 192, 19, true);
    }
    watchy_ui_draw_text(canvas, 9, y, text, 2u, !selected);
}

static void compact_row(watchy_canvas_t *canvas, int index, const char *text, bool selected) {
    const int16_t y = (int16_t)(33 + index * 21);
    if (selected) {
        watchy_ui_rect(canvas, 4, (int16_t)(y - 3), 192, 15, true);
    }
    watchy_ui_draw_text(canvas, 8, y, text, 1u, !selected);
}

static void detail_lines(watchy_canvas_t *canvas, int16_t y, const char *detail) {
    char line[48];
    while (detail != NULL && *detail != '\0' && y < 193) {
        const char *end = strchr(detail, '\n');
        size_t length = end == NULL ? strlen(detail) : (size_t)(end - detail);
        if (length >= sizeof(line)) length = sizeof(line) - 1u;
        memcpy(line, detail, length);
        line[length] = '\0';
        watchy_ui_draw_text(canvas, 8, y, line, 1u, true);
        y = (int16_t)(y + 14);
        detail = end == NULL ? NULL : end + 1u;
    }
}

static void render_watchface(watchy_canvas_t *canvas,
                             const watchy_shell_t *shell,
                             const watchy_settings_t *settings,
                             const watchy_time_t *time,
                             const watchy_battery_state_t *battery) {
    char text[32];
    unsigned hour = time == NULL ? 0u : time->hour;
    const char *suffix = "";
    if (!settings->time_24h) {
        suffix = hour >= 12u ? " PM" : " AM";
        hour %= 12u;
        if (hour == 0u) hour = 12u;
    }
    snprintf(text, sizeof(text), "%02u:%02u%s", hour,
             time == NULL ? 0u : time->minute, suffix);
    watchy_ui_draw_text(canvas, settings->time_24h ? 28 : 10, 55, text, 4u, true);
    if (time != NULL) {
        snprintf(text, sizeof(text), "%04d-%02u-%02u", time->year, time->month, time->day);
        watchy_ui_draw_text(canvas, 39, 103, text, 2u, true);
    }
    if (battery != NULL) {
        snprintf(text, sizeof(text), "BAT %u%%", battery->percent);
        watchy_ui_draw_text(canvas, 60, 151, text, 2u, true);
    }
    if (shell->package_warning) {
        watchy_ui_rect(canvas, 174, 6, 20, 20, true);
        watchy_ui_draw_text(canvas, 181, 9, "!", 2u, false);
    }
    if (shell->safe_mode) {
        watchy_ui_draw_text(canvas, 5, 181, "SAFE", 1u, true);
    }
}

static void render_launcher(watchy_canvas_t *canvas, const watchy_shell_t *shell) {
    static const char *const labels[] = {
        "WATCHFACE", "PACKAGES", "SETTINGS", "CONNECT", "DIAGNOSTICS", "ABOUT",
    };
    title(canvas, "MENU");
    for (size_t index = 0u; index < WATCHY_SHELL_LAUNCHER_ITEMS; ++index) {
        row(canvas, (int)index, labels[index], shell->selection == index);
    }
}

static void render_settings(watchy_canvas_t *canvas,
                            const watchy_shell_t *shell,
                            const watchy_settings_t *settings) {
    char label[32];
    title(canvas, "SETTINGS");
    snprintf(label, sizeof(label), "CLOCK %s", settings->time_24h ? "24H" : "12H");
    row(canvas, 0, label, shell->selection == 0u);
    snprintf(label, sizeof(label), "MOTION %s", settings->motion_wake ? "ON" : "OFF");
    row(canvas, 1, label, shell->selection == 1u);
    row(canvas, 2, "SET TIME", shell->selection == 2u);
    row(canvas, 3, "NTP SYNC", shell->selection == 3u);
    row(canvas, 4, "WIFI", shell->selection == 4u);
    row(canvas, 5, "PORTAL", shell->selection == 5u);
    snprintf(label, sizeof(label), "REFRESH %u", settings->partial_refresh_limit);
    row(canvas, 6, label, shell->selection == 6u);
}

static void render_packages(watchy_canvas_t *canvas,
                            const watchy_shell_t *shell,
                            const watchy_package_catalog_t *catalog,
                            const char *detail) {
    title(canvas, shell->safe_mode ? "REMOVE" : "PACKAGES");
    if (catalog == NULL || catalog->count == 0u || shell->package_count == 0u) {
        row(canvas, 0, "NO PACKAGES", true);
    } else {
        const size_t page_start = watchy_shell_package_page_start(shell);
        for (size_t position = page_start; position < shell->package_count &&
                                             position < page_start + WATCHY_SHELL_PACKAGE_PAGE_ITEMS;
             ++position) {
            char label[WATCHY_SHELL_PACKAGE_LABEL_SIZE];
            const size_t catalog_index = shell->package_indices[position];
            if (catalog_index >= catalog->count ||
                !watchy_shell_format_package_label(&catalog->packages[catalog_index], position,
                                                   shell->package_count, label, sizeof(label))) {
                continue;
            }
            compact_row(canvas, (int)(position - page_start), label, shell->selection == position);
        }
    }
    if (detail != NULL) watchy_ui_draw_text(canvas, 8, 184, detail, 1u, true);
}

static void render_diagnostics(watchy_canvas_t *canvas,
                               const watchy_shell_t *shell,
                               const watchy_diagnostic_report_t *diagnostics) {
    title(canvas, "DIAGNOSTICS");
    if (diagnostics == NULL || diagnostics->count == 0u) {
        row(canvas, 2, "UNAVAILABLE", false);
        return;
    }
    const size_t page_start = watchy_shell_diagnostic_page_start(shell);
    for (size_t index = page_start; index < diagnostics->count &&
                                      index < page_start + WATCHY_SHELL_PACKAGE_PAGE_ITEMS;
         ++index) {
        char label[WATCHY_SHELL_DIAGNOSTIC_LABEL_SIZE];
        if (watchy_shell_format_diagnostic_label(&diagnostics->entries[index], label,
                                                 sizeof(label))) {
            compact_row(canvas, (int)(index - page_start), label, shell->selection == index);
        }
    }
}

void watchy_shell_render(watchy_canvas_t *canvas,
                         const watchy_shell_t *shell,
                         const watchy_settings_t *settings,
                         const watchy_time_t *time,
                         const watchy_battery_state_t *battery,
                         const watchy_package_catalog_t *catalog,
                         const watchy_diagnostic_report_t *diagnostics,
                         const char *detail) {
    if (canvas == NULL || shell == NULL || settings == NULL) {
        return;
    }
    watchy_ui_fill(canvas, false);
    switch (shell->screen) {
    case WATCHY_SHELL_WATCHFACE:
        render_watchface(canvas, shell, settings, time, battery);
        break;
    case WATCHY_SHELL_LAUNCHER:
        render_launcher(canvas, shell);
        break;
    case WATCHY_SHELL_PACKAGE_APPS:
        render_packages(canvas, shell, catalog, detail);
        break;
    case WATCHY_SHELL_SETTINGS:
        render_settings(canvas, shell, settings);
        break;
    case WATCHY_SHELL_MANUAL_TIME:
        title(canvas, "SET TIME");
        detail_lines(canvas, 62, detail == NULL ? "EDIT CLOCK" : detail);
        break;
    case WATCHY_SHELL_NTP_SYNC:
        title(canvas, "NTP SYNC");
        row(canvas, 2, detail == NULL ? "READY" : detail, false);
        break;
    case WATCHY_SHELL_CONNECTIVITY:
        title(canvas, "CONNECT");
        row(canvas, 1, settings->wifi_ssid[0] == '\0' ? "NO WIFI SAVED" : "WIFI SAVED", false);
        row(canvas, 3, detail == NULL ? "RADIO OFF" : detail, false);
        break;
    case WATCHY_SHELL_PACKAGE_PORTAL:
        title(canvas, "PORTAL");
        row(canvas, 1, "CLIENT WIFI", shell->selection == 0u);
        row(canvas, 2, "WATCHY AP", shell->selection == 1u);
        detail_lines(canvas, 114, detail);
        break;
    case WATCHY_SHELL_DIAGNOSTICS:
        render_diagnostics(canvas, shell, diagnostics);
        break;
    case WATCHY_SHELL_ABOUT:
        title(canvas, "ABOUT");
        row(canvas, 1, "WATCHY 2.0", false);
        row(canvas, 3, "ESP-IDF WPK1", false);
        break;
    case WATCHY_SHELL_ERROR:
        title(canvas, "ERROR");
        compact_row(canvas, 3, watchy_shell_error_message(shell), false);
        break;
    case WATCHY_SHELL_SAFE_MODE:
        title(canvas, "SAFE MODE");
        row(canvas, 0, detail == NULL ? "RECOVERY" : detail, false);
        row(canvas, 2, "DIAGNOSTICS", shell->selection == 0u);
        row(canvas, 3, shell->package_index_readable ? "REMOVE PACKAGE" : "REMOVE ALL",
            shell->selection == 1u);
        row(canvas, 4, "NORMAL REBOOT", shell->selection == 2u);
        break;
    }
}
