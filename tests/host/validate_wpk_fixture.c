#include "watchy/package_crypto.h"
#include "watchy/packages.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    FILE *stream;
    long length;
    uint8_t *bytes;
    watchy_validated_package_t package;
    const watchy_crypto_api_t crypto = {.sha256 = watchy_package_sha256_regions};
    watchy_package_status_t status;
    if (argc != 2) {
        fprintf(stderr, "usage: %s PACKAGE.wpk\n", argv[0]);
        return 2;
    }
    stream = fopen(argv[1], "rb");
    if (stream == NULL || fseek(stream, 0, SEEK_END) != 0 || (length = ftell(stream)) <= 0 ||
        fseek(stream, 0, SEEK_SET) != 0) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        if (stream != NULL) fclose(stream);
        return 2;
    }
    bytes = malloc((size_t)length);
    if (bytes == NULL || fread(bytes, 1u, (size_t)length, stream) != (size_t)length) {
        fprintf(stderr, "cannot load %s\n", argv[1]);
        free(bytes);
        fclose(stream);
        return 2;
    }
    fclose(stream);
    status = watchy_package_validate(bytes, (size_t)length, &crypto, &package);
    if (status != WATCHY_PACKAGE_OK) {
        fprintf(stderr, "WPK validation status=%d\n", (int)status);
        free(bytes);
        return 1;
    }
    printf("PASS WPK %s@%s runtime_bytes=%lu assets=%lu\n", package.manifest.id,
           package.manifest.version, (unsigned long)package.runtime_bytes,
           (unsigned long)package.assets_size);
    free(bytes);
    return 0;
}
