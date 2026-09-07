// A library with one exported function and one imported one, for elf_symbols_test.
//
// It exists so the symbol walk is checked against a real linker's output rather than
// against a hand-built table: `elf_symbols.h` has to find the exported name, has to
// refuse the imported one — whose `.dynsym` entry is `SHN_UNDEF`, and returning the
// library's base address for it would be a pointer to its own ELF header — and has to
// bound the walk from whichever hash table the linker chose to emit.
#include <string.h>

int unique_elf_probe_symbol(int value) { return value + 1; }

// Imported, never defined here. Present in `.dynsym` as undefined.
size_t unique_elf_probe_calls_strlen(const char* text) { return strlen(text); }
