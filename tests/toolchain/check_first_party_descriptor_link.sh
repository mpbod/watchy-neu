#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
xtensa_prefix=${XTENSA_ESP32_PREFIX:-}
if [ -z "$xtensa_prefix" ]; then
    xtensa_prefix=$(command -v xtensa-esp32-elf-gcc | sed 's/-gcc$//' || true)
fi
if [ -z "$xtensa_prefix" ] || [ ! -x "${xtensa_prefix}-gcc" ]; then
    echo "SKIP: xtensa-esp32-elf-gcc is unavailable" >&2
    exit 77
fi

objdump=${xtensa_prefix}-objdump
readelf=${xtensa_prefix}-readelf
tmp=$(mktemp -d "${TMPDIR:-/tmp}/watchy-descriptor-audit.XXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM

"${xtensa_prefix}-g++" -c -fno-exceptions -fno-rtti -fno-threadsafe-statics \
    -fno-builtin -fvisibility=hidden -mlongcalls \
    -fdata-sections -ffunction-sections \
    -I"$root/sdk/include" -I"$root/first_party/watchfaces/common/include" \
    "$root/tests/toolchain/first_party_descriptor_fixture.cpp" \
    -o "$tmp/fixture.o"
"${xtensa_prefix}-gcc" -c -fPIC -DCONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT \
    "$root/tests/toolchain/project_so_bridge_fixture.c" -o "$tmp/bridge.o"

descriptor_sections=$($readelf -SW "$tmp/fixture.o" | grep 'descriptor' || true)
if [ -z "$descriptor_sections" ]; then
    echo "FAIL: descriptor section was not emitted" >&2
    exit 1
fi
if ! $readelf -Ws "$tmp/fixture.o" | grep -E 'GLOBAL[[:space:]]+DEFAULT.*watchy_package_entry' >/dev/null; then
    echo "FAIL: public package entry is not default-visible" >&2
    exit 1
fi
echo "$descriptor_sections"
case "$descriptor_sections" in
    *READONLY*)
        echo "FAIL: first-party descriptor remains in read-only storage" >&2
        exit 1
        ;;
esac
case "$descriptor_sections" in
    *" WA "*|*WRITE*) : ;;
    *)
        echo "FAIL: first-party descriptor section is not writable" >&2
        exit 1
        ;;
esac

if $objdump -r "$tmp/fixture.o" | awk '
    /^RELOCATION RECORDS FOR \[\.rodata/ { bad = 1 }
    END { exit bad ? 0 : 1 }
'; then
    echo "FAIL: dynamic relocation remains in a read-only rodata section" >&2
    exit 1
fi

if ! "${xtensa_prefix}-gcc" -shared -fPIC -static-libgcc -nostdlib -nostartfiles \
    -fdata-sections -ffunction-sections -Wl,--gc-sections -fvisibility=hidden \
    -o "$tmp/fixture.so" "$tmp/bridge.o" "$tmp/fixture.o" \
    -Wl,-T,"$root/sdk/ld/watchy_package_linker.o" \
    -Wl,--allow-shlib-undefined >"$tmp/link.log" 2>&1; then
    cat "$tmp/link.log" >&2
    echo "FAIL: first-party descriptor fixture does not link as a shared object" >&2
    exit 1
fi

echo "PASS: first-party descriptor uses writable storage with no rodata relocations"
