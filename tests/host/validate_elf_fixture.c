#include <stdio.h>
#include <stdlib.h>

#include "watchy/packages.h"

int main(int argc, char **argv) {
    FILE *file;
    long length;
    uint8_t *bytes;
    uint32_t runtime_bytes = 0u;
    watchy_package_status_t status;
    if (argc != 2 || (file = fopen(argv[1], "rb")) == NULL ||
        fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        return 2;
    }
    bytes = (uint8_t *)malloc((size_t)length);
    if (bytes == NULL || fread(bytes, 1u, (size_t)length, file) != (size_t)length ||
        fclose(file) != 0) {
        free(bytes);
        return 2;
    }
    status = watchy_package_elf_validate(bytes, (size_t)length,
                                         WATCHY_PACKAGE_RUNTIME_BYTES_MAX, &runtime_bytes);
    free(bytes);
    if (status != WATCHY_PACKAGE_OK) {
        fprintf(stderr, "fixture validation status=%d\n", status);
        return 1;
    }
    printf("PASS Xtensa fixture runtime_bytes=%lu\n", (unsigned long)runtime_bytes);
    return 0;
}
