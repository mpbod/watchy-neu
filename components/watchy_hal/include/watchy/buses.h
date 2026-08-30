#ifndef WATCHY_BUSES_H
#define WATCHY_BUSES_H

#include <stdbool.h>

#include "watchy/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

watchy_status_t watchy_buses_init(void);
bool watchy_buses_ready(void);
void watchy_buses_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
