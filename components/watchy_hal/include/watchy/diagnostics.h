#ifndef WATCHY_DIAGNOSTICS_H
#define WATCHY_DIAGNOSTICS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WATCHY_DIAGNOSTIC_PASS = 0,
    WATCHY_DIAGNOSTIC_FAIL,
    WATCHY_DIAGNOSTIC_UNAVAILABLE,
    WATCHY_DIAGNOSTIC_STOPPED,
} watchy_diagnostic_state_t;

typedef enum {
    WATCHY_DIAGNOSTIC_PASSIVE = 0,
    WATCHY_DIAGNOSTIC_ACTIVE_ACCEPTANCE,
} watchy_diagnostic_scope_t;

typedef struct {
    const char *service;
    watchy_diagnostic_state_t state;
    watchy_diagnostic_scope_t scope;
    int32_t status_code;
    const char *detail;
} watchy_diagnostic_entry_t;

#define WATCHY_DIAGNOSTIC_ENTRY_COUNT 11u

typedef struct {
    watchy_diagnostic_entry_t entries[WATCHY_DIAGNOSTIC_ENTRY_COUNT];
    size_t count;
} watchy_diagnostic_report_t;

void watchy_diagnostics_collect(watchy_diagnostic_report_t *out_report);

#ifdef __cplusplus
}
#endif

#endif
