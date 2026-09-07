#!/usr/bin/env bash
# Compiles and runs the host-side native tests. No device or NDK required.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${TMPDIR:-/tmp}/unique-native-test"
mkdir -p "$out"
for test in redirect_table proc_view round_trip syscall_paths; do
    ${CXX:-g++} -std=c++20 -O1 -Wall -Wextra -o "$out/${test}_test" "$here/${test}_test.cpp"
    "$out/${test}_test"
done

# The relocation shape, asserted against a compiler rather than against a memory of one.
# See reloc_shape.c: this is the construct that made SQLite's `stat` invisible to a GOT
# hook, and the check is that it still produces an absolute data relocation with a zero
# addend — the case `plt_hook.cpp::is_address_slot` was taught to accept.
# Every name the redirect asks libc for is a name libc actually has. See the script: the
# one that was not cost the eighteenth phone run, and printed the same line as the ones
# that were merely unused.
python3 "$here/check_libc_symbols.py"

# The symbol walk `sqlite_vfs.cpp` reaches SQLite's own VFS interface through, checked
# against a real linker's output and against dlsym's answer for the same name.
${CC:-gcc} -O1 -fPIC -shared -o "$out/elf_probe.so" "$here/elf_probe.c"
${CXX:-g++} -std=c++20 -O1 -Wall -Wextra -o "$out/elf_symbols_test" "$here/elf_symbols_test.cpp" -ldl
"$out/elf_symbols_test" "$out/elf_probe.so"

${CC:-gcc} -O1 -fPIC -shared -o "$out/reloc_shape.so" "$here/reloc_shape.c"
relocs="$(readelf -r "$out/reloc_shape.so")"
fail=0
for sym in stat lstat; do
    line="$(printf '%s\n' "$relocs" | grep -E "R_(X86_64_64|AARCH64_ABS64)\s" | grep -w "$sym" || true)"
    if [ -z "$line" ]; then
        echo "FAIL  taking the address of $sym did not produce an absolute data relocation"
        printf '%s\n' "$relocs" | grep -w "$sym" || echo "      (no relocation names $sym at all)"
        fail=1
    elif ! printf '%s\n' "$line" | grep -qE '\+ 0$'; then
        echo "FAIL  the relocation against $sym has a non-zero addend: $line"
        fail=1
    fi
done
if [ "$fail" -eq 0 ]; then
    echo "2 checks, 0 failures  (address-of-libc relocations are absolute and zero-addend)"
else
    exit 1
fi
