#include "watchy/packages.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    ELF32_HEADER_SIZE = 52u,
    ELF32_PROGRAM_HEADER_SIZE = 32u,
    ELF32_SECTION_HEADER_SIZE = 40u,
    ELFCLASS32 = 1u,
    ELFDATA2LSB = 1u,
    EV_CURRENT = 1u,
    ET_DYN = 3u,
    EM_XTENSA = 94u,
    PT_LOAD = 1u,
    PF_X = 1u,
    SHT_NOBITS = 8u,
    SHF_ALLOC = 2u,
    SHF_EXECINSTR = 4u,
};

static uint16_t read_u16le(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_u32le(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static bool table_fits(size_t file_size, uint32_t offset, uint16_t count, uint16_t entry_size) {
    const size_t table_offset = (size_t)offset;
    const size_t table_size = (size_t)count * (size_t)entry_size;
    return table_offset <= file_size && table_size <= file_size - table_offset;
}

static bool range_fits(size_t file_size, uint32_t offset, uint32_t size) {
    return (size_t)offset <= file_size && (size_t)size <= file_size - (size_t)offset;
}

static bool power_of_two(uint32_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

watchy_package_status_t watchy_package_elf_validate(const uint8_t *elf,
                                                    size_t elf_size,
                                                    uint32_t declared_runtime_bytes,
                                                    uint32_t *out_runtime_bytes) {
    uint32_t program_offset;
    uint32_t section_offset;
    uint16_t program_count;
    uint16_t section_count;
    uint16_t section_string_index;
    uint32_t runtime_bytes = 0u;
    bool executable_load = false;
    bool executable_section = false;

    if (elf == NULL) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (elf_size > WATCHY_PACKAGE_ELF_BYTES_MAX ||
        declared_runtime_bytes == 0u || declared_runtime_bytes > WATCHY_PACKAGE_RUNTIME_BYTES_MAX) {
        return WATCHY_PACKAGE_ERR_LIMIT;
    }
    if (elf_size < ELF32_HEADER_SIZE ||
        elf[0] != 0x7fu || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F' ||
        elf[4] != ELFCLASS32 || elf[5] != ELFDATA2LSB || elf[6] != EV_CURRENT ||
        read_u16le(elf + 16u) != ET_DYN || read_u16le(elf + 18u) != EM_XTENSA ||
        read_u32le(elf + 20u) != EV_CURRENT || read_u16le(elf + 40u) != ELF32_HEADER_SIZE ||
        read_u16le(elf + 42u) != ELF32_PROGRAM_HEADER_SIZE ||
        read_u16le(elf + 46u) != ELF32_SECTION_HEADER_SIZE) {
        return WATCHY_PACKAGE_ERR_ELF;
    }

    program_offset = read_u32le(elf + 28u);
    section_offset = read_u32le(elf + 32u);
    program_count = read_u16le(elf + 44u);
    section_count = read_u16le(elf + 48u);
    section_string_index = read_u16le(elf + 50u);
    if (program_count == 0u || section_count < 2u || program_offset < ELF32_HEADER_SIZE ||
        section_offset < ELF32_HEADER_SIZE ||
        !table_fits(elf_size, program_offset, program_count, ELF32_PROGRAM_HEADER_SIZE) ||
        !table_fits(elf_size, section_offset, section_count, ELF32_SECTION_HEADER_SIZE) ||
        (section_string_index != 0u && section_string_index >= section_count)) {
        return WATCHY_PACKAGE_ERR_ELF;
    }

    for (uint16_t index = 0u; index < program_count; ++index) {
        const uint8_t *program = elf + (size_t)program_offset +
                                 (size_t)index * ELF32_PROGRAM_HEADER_SIZE;
        const uint32_t type = read_u32le(program);
        const uint32_t file_offset = read_u32le(program + 4u);
        const uint32_t virtual_address = read_u32le(program + 8u);
        const uint32_t file_bytes = read_u32le(program + 16u);
        const uint32_t memory_bytes = read_u32le(program + 20u);
        const uint32_t flags = read_u32le(program + 24u);
        const uint32_t alignment = read_u32le(program + 28u);

        if (type != PT_LOAD) {
            continue;
        }
        if (file_bytes > memory_bytes || !range_fits(elf_size, file_offset, file_bytes) ||
            (alignment > 1u && (!power_of_two(alignment) ||
                                file_offset % alignment != virtual_address % alignment)) ||
            memory_bytes > UINT32_MAX - runtime_bytes) {
            return WATCHY_PACKAGE_ERR_ELF;
        }
        runtime_bytes += memory_bytes;
        if ((flags & PF_X) != 0u && file_bytes != 0u) {
            executable_load = true;
        }
    }

    if (read_u32le(elf + section_offset + 4u) != 0u ||
        read_u32le(elf + section_offset + 8u) != 0u) {
        return WATCHY_PACKAGE_ERR_ELF;
    }
    for (uint16_t index = 1u; index < section_count; ++index) {
        const uint8_t *section = elf + (size_t)section_offset +
                                 (size_t)index * ELF32_SECTION_HEADER_SIZE;
        const uint32_t type = read_u32le(section + 4u);
        const uint32_t flags = read_u32le(section + 8u);
        const uint32_t file_offset = read_u32le(section + 16u);
        const uint32_t size = read_u32le(section + 20u);
        const uint32_t alignment = read_u32le(section + 32u);

        if ((type != SHT_NOBITS && !range_fits(elf_size, file_offset, size)) ||
            (alignment > 1u && !power_of_two(alignment))) {
            return WATCHY_PACKAGE_ERR_ELF;
        }
        if ((flags & (SHF_ALLOC | SHF_EXECINSTR)) == (SHF_ALLOC | SHF_EXECINSTR) &&
            type != SHT_NOBITS && size != 0u) {
            executable_section = true;
        }
    }

    if (!executable_load || !executable_section) {
        return WATCHY_PACKAGE_ERR_ELF;
    }
    if (runtime_bytes == 0u || runtime_bytes > declared_runtime_bytes) {
        return WATCHY_PACKAGE_ERR_LIMIT;
    }
    if (out_runtime_bytes != NULL) {
        *out_runtime_bytes = runtime_bytes;
    }
    return WATCHY_PACKAGE_OK;
}
