#ifndef WATCHY_PACKAGE_CRYPTO_H
#define WATCHY_PACKAGE_CRYPTO_H

#include "watchy/packages.h"

#ifdef __cplusplus
extern "C" {
#endif

bool watchy_package_sha256_regions(void *context,
                                   const watchy_byte_region_t *regions,
                                   size_t region_count,
                                   uint8_t out_digest[WATCHY_PACKAGE_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif
#endif
