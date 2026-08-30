#include <stddef.h>
#include <stdint.h>

// Packages cannot rely on the firmware's libc resolver. Keep compiler-generated
// POD initialization and copies inside the ELF with hidden bytewise primitives.
extern "C" __attribute__((visibility("hidden"), noinline))
void *memset(void *destination, int value, size_t count) noexcept {
    auto *bytes = static_cast<volatile unsigned char *>(destination);
    const auto byte = static_cast<unsigned char>(value);
    for (size_t index = 0; index < count; ++index) bytes[index] = byte;
    return destination;
}

extern "C" __attribute__((visibility("hidden"), noinline))
void *memcpy(void *destination, const void *source, size_t count) noexcept {
    auto *output = static_cast<volatile unsigned char *>(destination);
    const auto *input = static_cast<const volatile unsigned char *>(source);
    for (size_t index = 0; index < count; ++index) output[index] = input[index];
    return destination;
}

extern "C" __attribute__((visibility("hidden"), noinline))
void *memmove(void *destination, const void *source, size_t count) noexcept {
    auto *output = static_cast<volatile unsigned char *>(destination);
    const auto *input = static_cast<const volatile unsigned char *>(source);
    const uintptr_t output_address = reinterpret_cast<uintptr_t>(destination);
    const uintptr_t input_address = reinterpret_cast<uintptr_t>(source);
    if (output_address < input_address) {
        for (size_t index = 0; index < count; ++index) output[index] = input[index];
    } else if (output_address > input_address) {
        for (size_t index = count; index != 0; --index) output[index - 1] = input[index - 1];
    }
    return destination;
}

extern "C" __attribute__((visibility("hidden"), noinline))
int memcmp(const void *left, const void *right, size_t count) noexcept {
    const auto *lhs = static_cast<const volatile unsigned char *>(left);
    const auto *rhs = static_cast<const volatile unsigned char *>(right);
    for (size_t index = 0; index < count; ++index) {
        if (lhs[index] != rhs[index]) return lhs[index] < rhs[index] ? -1 : 1;
    }
    return 0;
}
