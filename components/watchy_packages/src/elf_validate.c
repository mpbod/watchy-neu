#include "watchy/packages.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    EH_SIZE = 52u, PH_SIZE = 32u, SH_SIZE = 40u, SYM_SIZE = 16u, RELA_SIZE = 12u,
    ELFCLASS32 = 1u, ELFDATA2LSB = 1u, EV_CURRENT = 1u, ET_DYN = 3u, EM_XTENSA = 94u,
    PT_LOAD = 1u, PF_X = 1u, PF_W = 2u, PF_R = 4u,
    SHT_NULL = 0u, SHT_PROGBITS = 1u, SHT_SYMTAB = 2u, SHT_STRTAB = 3u,
    SHT_RELA = 4u, SHT_HASH = 5u, SHT_DYNAMIC = 6u, SHT_NOBITS = 8u, SHT_DYNSYM = 11u,
    SHF_WRITE = 1u, SHF_ALLOC = 2u, SHF_EXECINSTR = 4u,
    SHN_UNDEF = 0u, STB_GLOBAL = 1u, STT_FUNC = 2u,
    R_XTENSA_RTLD = 2u, R_XTENSA_GLOB_DAT = 3u, R_XTENSA_JMP_SLOT = 4u,
    R_XTENSA_RELATIVE = 5u,
};

static uint16_t u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}

static bool add_ok(uint32_t a, uint32_t b, uint32_t *out) {
    if (a > UINT32_MAX - b) {
        return false;
    }
    *out = a + b;
    return true;
}

static bool range_fits(size_t file_size, uint32_t offset, uint32_t size) {
    return (size_t)offset <= file_size && (size_t)size <= file_size - (size_t)offset;
}

static bool table_fits(size_t file_size, uint32_t offset, uint16_t count, uint16_t entry_size) {
    return (size_t)offset <= file_size &&
           (size_t)count <= (file_size - (size_t)offset) / entry_size;
}

static bool power_two(uint32_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

static bool ranges_overlap(uint32_t a, uint32_t as, uint32_t b, uint32_t bs) {
    uint32_t ae;
    uint32_t be;
    return as != 0u && bs != 0u && add_ok(a, as, &ae) && add_ok(b, bs, &be) &&
           a < be && b < ae;
}

static const uint8_t *section(const uint8_t *elf, uint32_t shoff, uint16_t index) {
    return elf + (size_t)shoff + (size_t)index * SH_SIZE;
}

static const uint8_t *program(const uint8_t *elf, uint32_t phoff, uint16_t index) {
    return elf + (size_t)phoff + (size_t)index * PH_SIZE;
}

static bool nul_string(const uint8_t *table, uint32_t table_size, uint32_t offset) {
    return offset < table_size && memchr(table + offset, '\0', table_size - offset) != NULL;
}

static bool section_in_load(const uint8_t *elf,
                            uint32_t phoff,
                            uint16_t phnum,
                            uint32_t sh_type,
                            uint32_t sh_flags,
                            uint32_t sh_addr,
                            uint32_t sh_offset,
                            uint32_t sh_size) {
    uint32_t sh_end;
    if (!add_ok(sh_addr, sh_size, &sh_end)) {
        return false;
    }
    for (uint16_t i = 0u; i < phnum; ++i) {
        const uint8_t *ph = program(elf, phoff, i);
        uint32_t mem_end;
        uint32_t file_end;
        const uint32_t flags = u32(ph + 24u);
        if (u32(ph) != PT_LOAD || !add_ok(u32(ph + 8u), u32(ph + 20u), &mem_end) ||
            sh_addr < u32(ph + 8u) || sh_end > mem_end ||
            ((sh_flags & SHF_EXECINSTR) != 0u && (flags & PF_X) == 0u) ||
            ((sh_flags & SHF_WRITE) != 0u && (flags & PF_W) == 0u) ||
            (flags & PF_R) == 0u) {
            continue;
        }
        if (sh_type == SHT_NOBITS) {
            return true;
        }
        if (add_ok(u32(ph + 4u), u32(ph + 16u), &file_end) &&
            sh_offset >= u32(ph + 4u) && sh_size <= file_end - sh_offset) {
            return true;
        }
    }
    return false;
}

static int loader_section_kind(const char *name,
                               uint32_t type,
                               uint32_t flags) {
    if (strcmp(name, ".text") == 0 && type == SHT_PROGBITS &&
        (flags & (SHF_ALLOC | SHF_EXECINSTR)) == (SHF_ALLOC | SHF_EXECINSTR) &&
        (flags & SHF_WRITE) == 0u) {
        return 1;
    }
    if (strcmp(name, ".data") == 0 && type == SHT_PROGBITS &&
        (flags & (SHF_ALLOC | SHF_WRITE)) == (SHF_ALLOC | SHF_WRITE) &&
        (flags & SHF_EXECINSTR) == 0u) {
        return 2;
    }
    if (strcmp(name, ".rodata") == 0 && type == SHT_PROGBITS &&
        (flags & SHF_ALLOC) != 0u && (flags & (SHF_WRITE | SHF_EXECINSTR)) == 0u) {
        return 3;
    }
    if (strcmp(name, ".data.rel.ro") == 0 && type == SHT_PROGBITS &&
        (flags & SHF_ALLOC) != 0u && (flags & SHF_EXECINSTR) == 0u) {
        return 4;
    }
    if (strcmp(name, ".bss") == 0 && type == SHT_NOBITS &&
        (flags & (SHF_ALLOC | SHF_WRITE)) == (SHF_ALLOC | SHF_WRITE) &&
        (flags & SHF_EXECINSTR) == 0u) {
        return 5;
    }
    return 0;
}

static bool loader_metadata_section(const char *name, uint32_t type) {
    return (type == SHT_DYNSYM && strcmp(name, ".dynsym") == 0) ||
           (type == SHT_STRTAB && strcmp(name, ".dynstr") == 0) ||
           (type == SHT_HASH && strcmp(name, ".hash") == 0) ||
           (type == SHT_DYNAMIC && strcmp(name, ".dynamic") == 0) ||
           (type == SHT_RELA && strncmp(name, ".rela", 5u) == 0);
}

watchy_package_status_t watchy_package_elf_validate(const uint8_t *elf,
                                                    size_t elf_size,
                                                    uint32_t declared_runtime_bytes,
                                                    uint32_t *out_runtime_bytes) {
    uint32_t phoff;
    uint32_t shoff;
    uint32_t entry;
    uint16_t phnum;
    uint16_t shnum;
    uint16_t shstrndx;
    const uint8_t *shstr;
    uint32_t shstr_size;
    uint32_t runtime = 0u;
    uint32_t text_addr = 0u;
    uint32_t text_size = 0u;
    uint8_t seen_loader_sections = 0u;
    uint16_t dynsym_index = 0u;
    uint16_t dynstr_index = 0u;

    if (elf == NULL) {
        return WATCHY_PACKAGE_ERR_ARGUMENT;
    }
    if (elf_size > WATCHY_PACKAGE_ELF_BYTES_MAX || declared_runtime_bytes == 0u ||
        declared_runtime_bytes > WATCHY_PACKAGE_RUNTIME_BYTES_MAX) {
        return WATCHY_PACKAGE_ERR_LIMIT;
    }
    if (elf_size < EH_SIZE || memcmp(elf, "\177ELF", 4u) != 0 ||
        elf[4] != ELFCLASS32 || elf[5] != ELFDATA2LSB || elf[6] != EV_CURRENT ||
        u16(elf + 16u) != ET_DYN || u16(elf + 18u) != EM_XTENSA ||
        u32(elf + 20u) != EV_CURRENT || u16(elf + 40u) != EH_SIZE ||
        u16(elf + 42u) != PH_SIZE || u16(elf + 46u) != SH_SIZE) {
        return WATCHY_PACKAGE_ERR_ELF;
    }
    entry = u32(elf + 24u);
    phoff = u32(elf + 28u);
    shoff = u32(elf + 32u);
    phnum = u16(elf + 44u);
    shnum = u16(elf + 48u);
    shstrndx = u16(elf + 50u);
    if (phnum == 0u || shnum < 2u || shstrndx == 0u || shstrndx >= shnum ||
        phoff < EH_SIZE || shoff < EH_SIZE ||
        !table_fits(elf_size, phoff, phnum, PH_SIZE) ||
        !table_fits(elf_size, shoff, shnum, SH_SIZE)) {
        return WATCHY_PACKAGE_ERR_ELF;
    }
    for (size_t byte = 0u; byte < SH_SIZE; ++byte) {
        if (section(elf, shoff, 0u)[byte] != 0u) {
            return WATCHY_PACKAGE_ERR_ELF;
        }
    }
    {
        const uint8_t *header = section(elf, shoff, shstrndx);
        if (u32(header + 4u) != SHT_STRTAB || (u32(header + 8u) & SHF_ALLOC) != 0u ||
            u32(header + 20u) < 2u ||
            !range_fits(elf_size, u32(header + 16u), u32(header + 20u))) {
            return WATCHY_PACKAGE_ERR_ELF;
        }
        shstr = elf + u32(header + 16u);
        shstr_size = u32(header + 20u);
        if (shstr[0] != '\0' || shstr[shstr_size - 1u] != '\0') {
            return WATCHY_PACKAGE_ERR_ELF;
        }
    }
    for (uint16_t i = 1u; i < shnum; ++i) {
        const uint8_t *sh = section(elf, shoff, i);
        const uint32_t name_offset = u32(sh);
        const char *name;
        if (!nul_string(shstr, shstr_size, name_offset)) {
            return WATCHY_PACKAGE_ERR_ELF;
        }
        name = (const char *)shstr + name_offset;
        if (strcmp(name, ".dynsym") == 0) {
            if (dynsym_index != 0u || u32(sh + 4u) != SHT_DYNSYM) {
                return WATCHY_PACKAGE_ERR_ELF;
            }
            dynsym_index = i;
        } else if (u32(sh + 4u) == SHT_DYNSYM) {
            return WATCHY_PACKAGE_ERR_ELF;
        }
        if (strcmp(name, ".dynstr") == 0) {
            if (dynstr_index != 0u || u32(sh + 4u) != SHT_STRTAB) {
                return WATCHY_PACKAGE_ERR_ELF;
            }
            dynstr_index = i;
        }
    }
    if (dynsym_index == 0u || dynstr_index == 0u ||
        u32(section(elf, shoff, dynsym_index) + 24u) != dynstr_index) {
        return WATCHY_PACKAGE_ERR_ELF;
    }

    for (uint16_t i = 0u; i < phnum; ++i) {
        const uint8_t *ph = program(elf, phoff, i);
        uint32_t mem_end;
        const uint32_t type = u32(ph);
        const uint32_t align = u32(ph + 28u);
        if (type != PT_LOAD) {
            continue;
        }
        if (u32(ph + 16u) > u32(ph + 20u) ||
            !range_fits(elf_size, u32(ph + 4u), u32(ph + 16u)) ||
            !add_ok(u32(ph + 8u), u32(ph + 20u), &mem_end) ||
            (align > 1u && (!power_two(align) ||
             u32(ph + 4u) % align != u32(ph + 8u) % align))) {
            return WATCHY_PACKAGE_ERR_ELF;
        }
        for (uint16_t prior = 0u; prior < i; ++prior) {
            const uint8_t *other = program(elf, phoff, prior);
            if (u32(other) == PT_LOAD && ranges_overlap(u32(ph + 8u), u32(ph + 20u),
                                                       u32(other + 8u), u32(other + 20u))) {
                return WATCHY_PACKAGE_ERR_ELF;
            }
        }
    }

    for (uint16_t i = 1u; i < shnum; ++i) {
        const uint8_t *sh = section(elf, shoff, i);
        const uint32_t name_offset = u32(sh);
        const uint32_t type = u32(sh + 4u);
        const uint32_t flags = u32(sh + 8u);
        const uint32_t addr = u32(sh + 12u);
        const uint32_t offset = u32(sh + 16u);
        const uint32_t size = u32(sh + 20u);
        const uint32_t align = u32(sh + 32u);
        const char *name;
        int kind;
        uint32_t end;

        if (!nul_string(shstr, shstr_size, name_offset) ||
            (type != SHT_NOBITS && !range_fits(elf_size, offset, size)) ||
            !add_ok(addr, size, &end) ||
            (align > 1u && (!power_two(align) || (addr != 0u && addr % align != 0u)))) {
            return WATCHY_PACKAGE_ERR_ELF;
        }
        name = (const char *)shstr + name_offset;
        kind = loader_section_kind(name, type, flags);
        if ((flags & SHF_ALLOC) != 0u) {
            uint32_t allocation = size;
            if ((kind == 0 && !loader_metadata_section(name, type)) || size == 0u ||
                !section_in_load(elf, phoff, phnum, type, flags, addr, offset, size)) {
                return WATCHY_PACKAGE_ERR_ELF;
            }
            if (kind == 0) {
                allocation = 0u;
            }
            if (kind == 1) {
                if (size > UINT32_MAX - 3u) {
                    return WATCHY_PACKAGE_ERR_ELF;
                }
                allocation = (size + 3u) & ~UINT32_C(3);
                if (!range_fits(elf_size, offset, allocation)) {
                    return WATCHY_PACKAGE_ERR_ELF;
                }
                text_addr = addr;
                text_size = size;
            }
            if (kind != 0) {
                if ((seen_loader_sections & (uint8_t)(1u << (kind - 1))) != 0u ||
                    allocation > UINT32_MAX - runtime) {
                    return WATCHY_PACKAGE_ERR_ELF;
                }
                seen_loader_sections |= (uint8_t)(1u << (kind - 1));
                runtime += allocation;
                for (uint16_t prior = 1u; prior < i; ++prior) {
                    const uint8_t *other = section(elf, shoff, prior);
                    const char *other_name = (const char *)shstr + u32(other);
                    if (loader_section_kind(other_name, u32(other + 4u), u32(other + 8u)) != 0 &&
                        ranges_overlap(addr, size, u32(other + 12u), u32(other + 20u))) {
                        return WATCHY_PACKAGE_ERR_ELF;
                    }
                }
            }
        }
        if (type != SHT_NOBITS && size != 0u) {
            for (uint16_t prior = 1u; prior < i; ++prior) {
                const uint8_t *other = section(elf, shoff, prior);
                if (u32(other + 4u) != SHT_NOBITS && u32(other + 20u) != 0u &&
                    ranges_overlap(offset, size, u32(other + 16u), u32(other + 20u))) {
                    return WATCHY_PACKAGE_ERR_ELF;
                }
            }
        }
        if (type == SHT_STRTAB &&
            (size == 0u || elf[offset] != '\0' || elf[offset + size - 1u] != '\0')) {
            return WATCHY_PACKAGE_ERR_ELF;
        }
        if (type == SHT_SYMTAB || type == SHT_DYNSYM) {
            const uint32_t entries = size / SYM_SIZE;
            const uint32_t link = u32(sh + 24u);
            if (u32(sh + 36u) != SYM_SIZE || size % SYM_SIZE != 0u || link >= shnum ||
                u32(sh + 28u) > entries || u32(section(elf, shoff, (uint16_t)link) + 4u) != SHT_STRTAB) {
                return WATCHY_PACKAGE_ERR_ELF;
            }
            const uint8_t *str_sh = section(elf, shoff, (uint16_t)link);
            const uint8_t *str = elf + u32(str_sh + 16u);
            const uint32_t str_size = u32(str_sh + 20u);
            for (uint32_t symbol_index = 0u; symbol_index < entries; ++symbol_index) {
                const uint8_t *sym = elf + offset + (size_t)symbol_index * SYM_SIZE;
                const uint16_t sym_section = u16(sym + 14u);
                uint32_t sym_end;
                if (!nul_string(str, str_size, u32(sym)) ||
                    (sym_section != SHN_UNDEF && sym_section < 0xff00u && sym_section >= shnum) ||
                    !add_ok(u32(sym + 4u), u32(sym + 8u), &sym_end)) {
                    return WATCHY_PACKAGE_ERR_ELF;
                }
                if (sym_section != SHN_UNDEF && sym_section < 0xff00u) {
                    const uint8_t *target = section(elf, shoff, sym_section);
                    const char *target_name = (const char *)shstr + u32(target);
                    uint32_t target_end;
                    if ((u32(target + 8u) & SHF_ALLOC) == 0u ||
                        !add_ok(u32(target + 12u), u32(target + 20u), &target_end) ||
                        u32(sym + 4u) < u32(target + 12u) || sym_end > target_end) {
                        return WATCHY_PACKAGE_ERR_ELF;
                    }
                    if (type == SHT_DYNSYM && (sym[12u] >> 4u) == STB_GLOBAL &&
                        (sym[12u] & 0x0fu) == STT_FUNC &&
                        loader_section_kind(target_name, u32(target + 4u),
                                            u32(target + 8u)) != 1) {
                        return WATCHY_PACKAGE_ERR_ELF;
                    }
                } else if (type == SHT_DYNSYM && (sym[12u] >> 4u) == STB_GLOBAL &&
                           (sym[12u] & 0x0fu) == STT_FUNC) {
                    /* elf_loader exports every global function by subtracting
                     * the .text base, including undefined functions. Reject
                     * those before it can perform that invalid mapping. */
                    return WATCHY_PACKAGE_ERR_ELF;
                }
                if (type == SHT_DYNSYM && (sym[12u] >> 4u) == STB_GLOBAL &&
                    (sym[12u] & 0x0fu) == STT_FUNC) {
                    const uint8_t *terminator = memchr(str + u32(sym), '\0',
                                                       str_size - u32(sym));
                    const uint32_t name_bytes = (uint32_t)(terminator - (str + u32(sym))) + 1u;
                    uint32_t aligned_name;
                    /* ESP32 esp_symtab_t is two 32-bit pointers. elf_loader
                     * allocates a table entry and a separate name for every
                     * exported global function. */
                    if (name_bytes > UINT32_MAX - 3u) {
                        return WATCHY_PACKAGE_ERR_LIMIT;
                    }
                    aligned_name = (name_bytes + 3u) & ~UINT32_C(3);
                    if (runtime > UINT32_MAX - 8u ||
                        runtime + 8u > UINT32_MAX - aligned_name) {
                        return WATCHY_PACKAGE_ERR_LIMIT;
                    }
                    runtime += 8u + aligned_name;
                }
            }
        } else if (type == SHT_RELA) {
            const uint32_t link = u32(sh + 24u);
            const uint32_t info = u32(sh + 28u);
            if (u32(sh + 36u) != RELA_SIZE || size % RELA_SIZE != 0u ||
                link >= shnum || info >= shnum ||
                ((info == 0u) && strcmp(name, ".rela.dyn") != 0) ||
                (u32(section(elf, shoff, (uint16_t)link) + 4u) != SHT_SYMTAB &&
                 u32(section(elf, shoff, (uint16_t)link) + 4u) != SHT_DYNSYM) ||
                (info != 0u &&
                 (u32(section(elf, shoff, (uint16_t)info) + 8u) & SHF_ALLOC) == 0u)) {
                return WATCHY_PACKAGE_ERR_ELF;
            }
            const uint32_t symbol_count = u32(section(elf, shoff, (uint16_t)link) + 20u) / SYM_SIZE;
            for (uint32_t relocation = 0u; relocation < size / RELA_SIZE; ++relocation) {
                const uint8_t *rela = elf + offset + (size_t)relocation * RELA_SIZE;
                const uint8_t relocation_type = (uint8_t)u32(rela + 4u);
                bool target_found = false;
                if ((u32(rela + 4u) >> 8u) >= symbol_count ||
                    relocation_type < R_XTENSA_RTLD ||
                    relocation_type > R_XTENSA_RELATIVE) {
                    return WATCHY_PACKAGE_ERR_ELF;
                }
                for (uint16_t target_index = 1u; target_index < shnum; ++target_index) {
                    const uint8_t *target = section(elf, shoff, target_index);
                    const char *target_name = (const char *)shstr + u32(target);
                    uint32_t target_end;
                    if ((info == 0u || info == target_index) &&
                        loader_section_kind(target_name, u32(target + 4u),
                                            u32(target + 8u)) != 0 &&
                        u32(target + 20u) >= 4u &&
                        add_ok(u32(target + 12u), u32(target + 20u), &target_end) &&
                        u32(rela) >= u32(target + 12u) && u32(rela) <= target_end - 4u) {
                        target_found = true;
                        break;
                    }
                }
                if (!target_found) {
                    return WATCHY_PACKAGE_ERR_ELF;
                }
            }
        } else if (type == SHT_HASH) {
            const uint32_t link = u32(sh + 24u);
            if (size < 8u || size % 4u != 0u || u32(sh + 36u) != 4u || link >= shnum ||
                u32(section(elf, shoff, (uint16_t)link) + 4u) != SHT_DYNSYM) {
                return WATCHY_PACKAGE_ERR_ELF;
            }
        } else if (type == SHT_DYNAMIC) {
            const uint32_t link = u32(sh + 24u);
            if (size % 8u != 0u || u32(sh + 36u) != 8u || link >= shnum ||
                u32(section(elf, shoff, (uint16_t)link) + 4u) != SHT_STRTAB) {
                return WATCHY_PACKAGE_ERR_ELF;
            }
        }
    }
    if ((seen_loader_sections & 1u) == 0u || text_size == 0u || entry < text_addr ||
        entry >= text_addr + text_size || runtime == 0u || runtime > declared_runtime_bytes) {
        return runtime > declared_runtime_bytes ? WATCHY_PACKAGE_ERR_LIMIT : WATCHY_PACKAGE_ERR_ELF;
    }
    if (out_runtime_bytes != NULL) {
        *out_runtime_bytes = runtime;
    }
    return WATCHY_PACKAGE_OK;
}
