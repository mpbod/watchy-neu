#include "watchy/shell_render.h"

#include "watchy/ui_draw.h"

#include <stdio.h>
#include <string.h>

enum {
    PANEL_WIDTH = 200,
    PANEL_HEIGHT = 200,
    CONTENT_WIDTH = 187,
    HEADER_HEIGHT = 25,
    ROW_HEIGHT = 55,
    RAIL_X = 187,
    RAIL_WIDTH = 13,
    RAIL_TOP = 20,
    RAIL_BOTTOM = 180,
    RAIL_THUMB_HEIGHT = 32,
};

static watchy_text_style_t text_style(const watchy_font_t *font,
                                      int8_t tracking,
                                      bool black,
                                      bool outlined) {
    return (watchy_text_style_t){font, tracking, black, outlined};
}

#define STYLE(font, tracking, black, outlined) \
    (&(watchy_text_style_t){font, tracking, black, outlined})

static void draw_header(watchy_canvas_t *canvas,
                        const char *title,
                        const watchy_time_t *time) {
    char clock[6];
    const watchy_text_style_t white = text_style(&watchy_font_plex_9_semibold, 1, false, false);
    watchy_ui_rect(canvas, 0, 0, CONTENT_WIDTH, HEADER_HEIGHT, true);
    watchy_ui_draw_text_font(canvas, 7, 17, title, &white);
    snprintf(clock, sizeof(clock), "%02u:%02u",
             time == NULL ? 0u : (unsigned)(time->hour % 24u),
             time == NULL ? 0u : (unsigned)(time->minute % 60u));
    watchy_ui_draw_text_right(canvas, 183, 17, clock, &white);
}

static void triangle(watchy_canvas_t *canvas, int center_y, bool up) {
    for (int row = 0; row < 5; ++row) {
        const int width = row + 1;
        const int y = up ? center_y + 2 - row : center_y - 2 + row;
        for (int x = 193 - width / 2; x <= 193 + width / 2; ++x) {
            watchy_ui_pixel(canvas, (int16_t)x, (int16_t)y, true);
        }
    }
}

static void draw_rail(watchy_canvas_t *canvas, unsigned selection, unsigned total) {
    unsigned clamped_selection = selection;
    unsigned thumb_top = RAIL_TOP;
    const unsigned track_height = RAIL_BOTTOM - RAIL_TOP;
    const unsigned travel = track_height - RAIL_THUMB_HEIGHT;
    watchy_ui_rect(canvas, RAIL_X, 0, RAIL_WIDTH, PANEL_HEIGHT, false);
    watchy_ui_rule(canvas, RAIL_X, 0, 1, PANEL_HEIGHT, true);
    triangle(canvas, 10, true);
    triangle(canvas, 190, false);
    if (total == 0u) total = 1u;
    if (clamped_selection >= total) clamped_selection = total - 1u;
    if (total > 1u) {
        thumb_top += (clamped_selection * travel + (total - 1u) / 2u) / (total - 1u);
    }
    watchy_ui_rect(canvas, 190, (int16_t)thumb_top, 7, RAIL_THUMB_HEIGHT, true);
}

static void draw_face_icon(watchy_canvas_t *canvas, int16_t x, int16_t y, bool black) {
    watchy_ui_circle(canvas, (int16_t)(x + 9), (int16_t)(y + 11), 8, false, black);
    watchy_ui_rule(canvas, (int16_t)(x + 9), (int16_t)(y + 4), 1, 14, black);
    watchy_ui_rule(canvas, (int16_t)(x + 5), (int16_t)(y + 11), 9, 1, black);
}

static void draw_apps_icon(watchy_canvas_t *canvas, int16_t x, int16_t y, bool black) {
    watchy_ui_rect(canvas, x + 1, y + 3, 8, 8, black);
    watchy_ui_rect_outline(canvas, x + 12, y + 3, 8, 8, 1, black);
    watchy_ui_rect_outline(canvas, x + 1, y + 14, 8, 8, 1, black);
    watchy_ui_rect(canvas, x + 12, y + 14, 8, 8, black);
}

static void draw_settings_icon(watchy_canvas_t *canvas, int16_t x, int16_t y, bool black) {
    /* An angular gear/diamond remains legible at the 22 px icon size. */
    for (int i = 0; i < 8; ++i) {
        watchy_ui_pixel(canvas, (int16_t)(x + 10 - i), (int16_t)(y + 10 - i), black);
        watchy_ui_pixel(canvas, (int16_t)(x + 10 + i), (int16_t)(y + 10 - i), black);
        watchy_ui_pixel(canvas, (int16_t)(x + 10 - i), (int16_t)(y + 10 + i), black);
        watchy_ui_pixel(canvas, (int16_t)(x + 10 + i), (int16_t)(y + 10 + i), black);
    }
    watchy_ui_rect(canvas, x + 8, y + 8, 5, 5, !black);
}

static void draw_icon(watchy_canvas_t *canvas, int16_t x, int16_t y, unsigned kind, bool black) {
    if (kind == 0u) {
        draw_face_icon(canvas, x, y, black);
    } else if (kind == 1u) {
        draw_apps_icon(canvas, x, y, black);
    } else {
        draw_settings_icon(canvas, x, y, black);
    }
}

static void draw_primary_row(watchy_canvas_t *canvas,
                             unsigned slot,
                             bool selected,
                             const char *label,
                             const char *metadata,
                             unsigned icon_kind) {
    const int16_t top = (int16_t)(HEADER_HEIGHT + slot * ROW_HEIGHT);
    const bool ink = !selected;
    const watchy_text_style_t primary =
        text_style(&watchy_font_heros_17_bold, 0, ink, false);
    const watchy_text_style_t secondary =
        text_style(&watchy_font_plex_9_semibold, 1, ink, false);
    if (selected) {
        watchy_ui_rect(canvas, 0, top, CONTENT_WIDTH, ROW_HEIGHT, true);
    }
    draw_icon(canvas, 11, (int16_t)(top + 17), icon_kind, ink);
    watchy_ui_draw_text_font(canvas, 44, (int16_t)(top + 30), label, &primary);
    watchy_ui_draw_text_font(canvas, 44, (int16_t)(top + 46), metadata, &secondary);
}

static void render_menu(watchy_canvas_t *canvas,
                        const watchy_shell_t *shell,
                        const watchy_time_t *time) {
    static const char *const labels[] = {"Watchface", "Apps", "Settings"};
    static const char *const metadata[] = {"CHANGE FACE", "APP GRID", "SYSTEM"};
    draw_header(canvas, "MENU", time);
    for (unsigned row = 0u; row < 3u; ++row) {
        draw_primary_row(canvas, row, shell->selection == row, labels[row], metadata[row], row);
    }
    draw_rail(canvas, shell->selection, 3u);
}

static const char *selector_metadata(const watchy_package_info_t *package) {
    if (package->quarantined) return "QUARANTINED";
    if (package->pending) return "PENDING";
    if (package->active) return "ACTIVE";
    return package->version[0] == '\0' ? "UNKNOWN" : package->version;
}

static void render_selector(watchy_canvas_t *canvas,
                            const watchy_shell_t *shell,
                            const watchy_time_t *time,
                            const watchy_package_catalog_t *catalog) {
    const unsigned total = shell->face_count == 0u ? 1u : shell->face_count;
    const unsigned page_start = (shell->selection / WATCHY_SHELL_VISIBLE_ROWS) *
                                WATCHY_SHELL_VISIBLE_ROWS;
    draw_header(canvas, "WATCHFACE", time);
    for (unsigned slot = 0u; slot < WATCHY_SHELL_VISIBLE_ROWS; ++slot) {
        const unsigned position = page_start + slot;
        const char *label = "Hairline";
        const char *metadata = "BUILT-IN";
        unsigned icon_kind = 0u;
        if (position > 0u && position < total && catalog != NULL) {
            const unsigned face_position = position - 1u;
            if (face_position < shell->face_count - 1u) {
                const unsigned catalog_index = shell->face_indices[face_position];
                if (catalog_index < catalog->count && catalog_index < WATCHY_PACKAGE_INSTALLED_MAX) {
                    const watchy_package_info_t *package = &catalog->packages[catalog_index];
                    label = package->name[0] == '\0' ? package->package_ref : package->name;
                    metadata = selector_metadata(package);
                }
            }
        }
        if (position < total) {
            draw_primary_row(canvas, slot, shell->selection == position,
                             label, metadata, icon_kind);
        }
    }
    draw_rail(canvas, shell->selection, total);
}

static void render_settings_screen(watchy_canvas_t *canvas,
                                   const watchy_shell_t *shell,
                                   const watchy_settings_t *settings,
                                   const watchy_time_t *time) {
    static const char *const labels[] = {
        "Clock", "Motion Wake", "Display Motion", "Set Time", "NTP Sync",
        "Wi-Fi", "Portal", "Refresh", "Diagnostics", "About",
    };
    char metadata[10][20];
    const char *motion = settings->motion_wake ? "ON" : "OFF";
    const char *transition = settings->transition_level == WATCHY_TRANSITION_LEVEL_FULL
                                 ? "FULL"
                                 : settings->transition_level == WATCHY_TRANSITION_LEVEL_REDUCED
                                     ? "REDUCED" : "OFF";
    snprintf(metadata[0], sizeof(metadata[0]), "%s", settings->time_24h ? "24H" : "12H");
    snprintf(metadata[1], sizeof(metadata[1]), "%s", motion);
    snprintf(metadata[2], sizeof(metadata[2]), "%s", transition);
    snprintf(metadata[3], sizeof(metadata[3]), "%s", shell->editing ? "EDITING" : "EDIT");
    snprintf(metadata[4], sizeof(metadata[4]), "%s", "SYNC");
    snprintf(metadata[5], sizeof(metadata[5]), "%s", settings->wifi_ssid[0] ? "SAVED" : "NOT SET");
    snprintf(metadata[6], sizeof(metadata[6]), "%s", "AP READY");
    snprintf(metadata[7], sizeof(metadata[7]), "LIMIT %u", (unsigned)settings->partial_refresh_limit);
    snprintf(metadata[8], sizeof(metadata[8]), "%s", "REPORT");
    snprintf(metadata[9], sizeof(metadata[9]), "%s", "INFO");
    draw_header(canvas, "SETTINGS", time);
    const unsigned page_start = (shell->selection / WATCHY_SHELL_VISIBLE_ROWS) *
                                WATCHY_SHELL_VISIBLE_ROWS;
    for (unsigned slot = 0u; slot < WATCHY_SHELL_VISIBLE_ROWS; ++slot) {
        const unsigned index = page_start + slot;
        if (index < 10u) {
            draw_primary_row(canvas, slot, shell->selection == index,
                             labels[index], metadata[index], 2u);
        }
    }
    draw_rail(canvas, shell->selection, 10u);
}

static void render_hairline(watchy_canvas_t *canvas,
                            const watchy_shell_t *shell,
                            const watchy_settings_t *settings,
                            const watchy_time_t *time,
                            const watchy_battery_state_t *battery) {
    static const char *const months[] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
    };
    char value[16];
    unsigned hour = time == NULL ? 0u : (unsigned)(time->hour % 24u);
    unsigned minute = time == NULL ? 0u : (unsigned)(time->minute % 60u);
    unsigned day = time == NULL ? 0u : time->day;
    unsigned month = time == NULL ? 0u : time->month;
    if (!settings->time_24h) {
        hour %= 12u;
        if (hour == 0u) hour = 12u;
    }
    if (day > 31u) day = 0u;
    snprintf(value, sizeof(value), "%02u:%02u", hour, minute);
    watchy_ui_draw_text_centered(canvas, 100, 91, value,
                                 STYLE(&watchy_font_heros_40_regular, 0, true, false));
    if (month < 1u || month > 12u) month = 1u;
    snprintf(value, sizeof(value), "%02u %s", day, months[month - 1u]);
    watchy_ui_draw_text_centered(canvas, 100, 175, value,
                                 STYLE(&watchy_font_plex_9_semibold, 1, true, false));
    unsigned percent = battery == NULL ? 0u : battery->percent;
    if (percent > 100u) percent = 0u;
    unsigned width = (percent * PANEL_WIDTH + 50u) / 100u;
    if (width > PANEL_WIDTH) width = PANEL_WIDTH;
    watchy_ui_rect(canvas, 0, 197, (int16_t)width, 3, true);
    if (shell->safe_mode) {
        watchy_ui_draw_text_font(canvas, 5, 16, "SAFE",
                                 STYLE(&watchy_font_plex_9_semibold, 1, true, false));
    }
    if (shell->package_warning) {
        watchy_ui_draw_text_font(canvas, 181, 16, "!",
                                 STYLE(&watchy_font_plex_11_regular, 0, true, false));
    }
}

static void draw_compact_row(watchy_canvas_t *canvas,
                             unsigned slot,
                             const char *label,
                             const char *metadata,
                             bool selected) {
    const int16_t top = (int16_t)(31 + slot * 24u);
    const watchy_text_style_t primary = text_style(&watchy_font_heros_13_bold, 0, !selected, false);
    const watchy_text_style_t secondary = text_style(&watchy_font_plex_8_semibold, 0, !selected, false);
    if (selected) watchy_ui_rect(canvas, 4, top, CONTENT_WIDTH - 8, 20, true);
    watchy_ui_draw_text_font(canvas, 9, (int16_t)(top + 13), label, &primary);
    if (metadata != NULL && metadata[0] != '\0') {
        watchy_ui_draw_text_font(canvas, 130, (int16_t)(top + 13), metadata, &secondary);
    }
}

static void draw_detail_lines(watchy_canvas_t *canvas, int16_t y, const char *detail) {
    char line[48];
    while (detail != NULL && *detail != '\0' && y < PANEL_HEIGHT - 7) {
        const char *end = strchr(detail, '\n');
        size_t length = end == NULL ? strlen(detail) : (size_t)(end - detail);
        if (length >= sizeof(line)) length = sizeof(line) - 1u;
        memcpy(line, detail, length);
        line[length] = '\0';
        watchy_ui_draw_text_font(canvas, 8, y, line,
                                 STYLE(&watchy_font_plex_9_semibold, 0, true, false));
        y = (int16_t)(y + 14);
        detail = end == NULL ? NULL : end + 1u;
    }
}

static void render_apps(watchy_canvas_t *canvas,
                        const watchy_shell_t *shell,
                        const watchy_package_catalog_t *catalog,
                        const char *detail) {
    const bool recovery = shell->safe_mode;
    const unsigned count = recovery ? shell->recovery_count : shell->app_count;
    const uint8_t *indices = recovery ? shell->recovery_indices : shell->app_indices;
    draw_header(canvas, recovery ? "REMOVE" : "APPS", NULL);
    if (count == 0u || catalog == NULL || catalog->count == 0u) {
        draw_compact_row(canvas, 0u, recovery ? "NO PACKAGES" : "NO APPS", NULL, false);
    } else {
        const unsigned page_start = (shell->selection / WATCHY_SHELL_VISIBLE_ROWS) *
                                    WATCHY_SHELL_VISIBLE_ROWS;
        for (unsigned slot = 0u; slot < WATCHY_SHELL_VISIBLE_ROWS; ++slot) {
            const unsigned position = page_start + slot;
            if (position < count && indices[position] < catalog->count) {
                char label[WATCHY_SHELL_PACKAGE_LABEL_SIZE];
                if (watchy_shell_format_package_label(&catalog->packages[indices[position]],
                                                      position, count, label, sizeof(label))) {
                    draw_compact_row(canvas, slot, label,
                                     catalog->packages[indices[position]].version,
                                     shell->selection == position);
                }
            }
        }
    }
    draw_detail_lines(canvas, 184, detail);
}

static void render_diagnostics(watchy_canvas_t *canvas,
                               const watchy_shell_t *shell,
                               const watchy_diagnostic_report_t *report) {
    draw_header(canvas, "DIAGNOSTICS", NULL);
    const unsigned count = report == NULL || report->count > WATCHY_DIAGNOSTIC_ENTRY_COUNT
                               ? 0u : (unsigned)report->count;
    if (count == 0u) {
        draw_compact_row(canvas, 0u, "UNAVAILABLE", "N/A", false);
        return;
    }
    const unsigned page_start = (shell->selection / WATCHY_SHELL_VISIBLE_ROWS) *
                                WATCHY_SHELL_VISIBLE_ROWS;
    for (unsigned slot = 0u; slot < WATCHY_SHELL_VISIBLE_ROWS; ++slot) {
        const unsigned index = page_start + slot;
        if (index < count) {
            char label[WATCHY_SHELL_DIAGNOSTIC_LABEL_SIZE];
            if (watchy_shell_format_diagnostic_label(&report->entries[index], label, sizeof(label))) {
                draw_compact_row(canvas, slot, label, NULL, shell->selection == index);
            }
        }
    }
}

static void render_legacy_screen(watchy_canvas_t *canvas,
                                 const watchy_shell_t *shell,
                                 const watchy_settings_t *settings,
                                 const watchy_package_catalog_t *catalog,
                                 const watchy_diagnostic_report_t *report,
                                 const char *detail) {
    switch (shell->screen) {
    case WATCHY_SHELL_PACKAGE_APPS:
        render_apps(canvas, shell, catalog, detail);
        break;
    case WATCHY_SHELL_MANUAL_TIME:
        draw_header(canvas, "SET TIME", NULL);
        draw_detail_lines(canvas, 62, detail == NULL ? "EDIT CLOCK" : detail);
        break;
    case WATCHY_SHELL_NTP_SYNC:
        draw_header(canvas, "NTP SYNC", NULL);
        draw_compact_row(canvas, 2u, detail == NULL ? "READY" : detail, "STATUS", false);
        break;
    case WATCHY_SHELL_CONNECTIVITY:
        draw_header(canvas, "CONNECT", NULL);
        draw_compact_row(canvas, 1u, settings->wifi_ssid[0] == '\0' ? "NO WIFI SAVED" : "WIFI SAVED",
                         "CREDENTIALS", false);
        draw_compact_row(canvas, 3u, detail == NULL ? "RADIO OFF" : detail, "STATUS", false);
        break;
    case WATCHY_SHELL_PACKAGE_PORTAL:
        draw_header(canvas, "PORTAL", NULL);
        draw_compact_row(canvas, 1u, "CLIENT WIFI", "STA", shell->selection == 0u);
        draw_compact_row(canvas, 2u, "WATCHY AP", "AP", shell->selection == 1u);
        draw_detail_lines(canvas, 114, detail);
        break;
    case WATCHY_SHELL_DIAGNOSTICS:
        render_diagnostics(canvas, shell, report);
        break;
    case WATCHY_SHELL_ABOUT:
        draw_header(canvas, "ABOUT", NULL);
        draw_compact_row(canvas, 1u, "WATCHY 2.0", "FIRMWARE", false);
        draw_compact_row(canvas, 3u, "ESP-IDF WPK1", "ABI 1.2", false);
        draw_detail_lines(canvas, 135, detail);
        break;
    case WATCHY_SHELL_ERROR:
        draw_header(canvas, "ERROR", NULL);
        draw_compact_row(canvas, 2u, watchy_shell_error_message(shell), "FAILED", false);
        break;
    case WATCHY_SHELL_SAFE_MODE:
        draw_header(canvas, "SAFE MODE", NULL);
        draw_compact_row(canvas, 0u, detail == NULL ? "RECOVERY" : detail, "BOOT", false);
        draw_compact_row(canvas, 2u, "DIAGNOSTICS", "REPORT", shell->selection == 0u);
        draw_compact_row(canvas, 3u, shell->package_index_readable ? "REMOVE PACKAGE" : "REMOVE ALL",
                         "RECOVERY", shell->selection == 1u);
        draw_compact_row(canvas, 4u, "NORMAL REBOOT", "EXIT", shell->selection == 2u);
        break;
    default:
        draw_header(canvas, "ERROR", NULL);
        draw_compact_row(canvas, 0u, "UNKNOWN SCREEN", "INVALID", false);
        break;
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
    if (canvas == NULL || shell == NULL || settings == NULL) return;
    watchy_ui_fill(canvas, false);
    if (shell->screen == WATCHY_SHELL_WATCHFACE_SELECTOR) {
        render_selector(canvas, shell, time, catalog);
        return;
    }
    switch (shell->screen) {
    case WATCHY_SHELL_WATCHFACE:
        render_hairline(canvas, shell, settings, time, battery);
        break;
    case WATCHY_SHELL_LAUNCHER:
        render_menu(canvas, shell, time);
        break;
    case WATCHY_SHELL_SETTINGS:
        render_settings_screen(canvas, shell, settings, time);
        break;
    case WATCHY_SHELL_PACKAGE_APPS:
    case WATCHY_SHELL_MANUAL_TIME:
    case WATCHY_SHELL_NTP_SYNC:
    case WATCHY_SHELL_CONNECTIVITY:
    case WATCHY_SHELL_PACKAGE_PORTAL:
    case WATCHY_SHELL_DIAGNOSTICS:
    case WATCHY_SHELL_ABOUT:
    case WATCHY_SHELL_ERROR:
    case WATCHY_SHELL_SAFE_MODE:
        render_legacy_screen(canvas, shell, settings, catalog, diagnostics, detail);
        break;
    default:
        watchy_ui_fill(canvas, false);
        break;
    }
}
