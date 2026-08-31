#ifndef WATCHY_TRANSITION_GOLDEN_H
#define WATCHY_TRANSITION_GOLDEN_H

#include <stdint.h>

/* Filled only after the independently asserted edge and pixel behavior passes. */
static const uint32_t watchy_transition_golden_hashes[10][5] = {
    {UINT32_C(0xdecc0e24), UINT32_C(0), UINT32_C(0), UINT32_C(0), UINT32_C(0)},
    {UINT32_C(0x834934bb), UINT32_C(0xdecc0e24), UINT32_C(0), UINT32_C(0), UINT32_C(0)},
    {UINT32_C(0xaee8d724), UINT32_C(0x838ba1ab), UINT32_C(0xdecc0e24),
     UINT32_C(0), UINT32_C(0)},
    {UINT32_C(0x38b3f92b), UINT32_C(0xdecc0e24), UINT32_C(0), UINT32_C(0),
     UINT32_C(0)},
    {UINT32_C(0x43a36757), UINT32_C(0xdecc0e24), UINT32_C(0), UINT32_C(0), UINT32_C(0)},
    {UINT32_C(0x11600563), UINT32_C(0xdecc0e24), UINT32_C(0), UINT32_C(0),
     UINT32_C(0)},
    {UINT32_C(0x54d0e75c), UINT32_C(0xdecc0e24), UINT32_C(0), UINT32_C(0),
     UINT32_C(0)},
    {UINT32_C(0x3fb64cf7), UINT32_C(0xdecc0e24), UINT32_C(0), UINT32_C(0),
     UINT32_C(0)},
    {UINT32_C(0x5b901c41), UINT32_C(0x9900396d), UINT32_C(0xdecc0e24),
     UINT32_C(0), UINT32_C(0)},
    {UINT32_C(0x50d79ecf), UINT32_C(0x03738e47), UINT32_C(0xdecc0e24),
     UINT32_C(0), UINT32_C(0)},
};

#endif
