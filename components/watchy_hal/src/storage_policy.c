#include "watchy/storage.h"

bool watchy_storage_region_is_erased(const uint8_t *data, size_t size) {
    if (data == NULL) {
        return false;
    }
    for (size_t index = 0; index < size; ++index) {
        if (data[index] != 0xffu) {
            return false;
        }
    }
    return true;
}
