// Host-side checks for elf_symbols.h — finding an exported symbol in a loaded library.
//
// The reason this needs testing off a device is that its one production caller cannot be
// tested on one: `sqlite_vfs.cpp` reads `sqlite3_vfs_find` out of a platform library that
// an app is not allowed to `dlopen`, and the only way to know the walk is right is to run
// the same walk against a library whose answer is already known. `dlsym` provides that
// answer; the walk has to agree with it.

#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <link.h>
#include <string>

#include "../../core/native/src/main/cpp/elf_symbols.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (condition) return;
    ++g_failures;
    std::printf("FAIL  %s\n", what.c_str());
}

struct Found {
    bool seen = false;
    unique::elf::DynamicSymbols symbols;
};

int look(struct dl_phdr_info* info, size_t, void* data) {
    auto* found = static_cast<Found*>(data);
    if (info->dlpi_name == nullptr) return 0;
    if (std::strstr(info->dlpi_name, "elf_probe.so") == nullptr) return 0;
    found->seen = true;
    found->symbols = unique::elf::read_symbols(info);
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "./elf_probe.so";
    void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        std::printf("FAIL  could not dlopen %s: %s\n", path, dlerror());
        return 1;
    }

    void* expected = dlsym(handle, "unique_elf_probe_symbol");
    check(expected != nullptr, "dlsym found the exported symbol");

    Found found;
    dl_iterate_phdr(look, &found);
    check(found.seen, "dl_iterate_phdr reported the loaded probe library");
    check(found.symbols.valid(),
          "the dynamic symbol table was readable and its length recoverable");
    check(found.symbols.count > 0, "the symbol count came out of a hash table");

    void* walked = unique::elf::find_symbol(found.symbols, "unique_elf_probe_symbol");
    check(walked == expected, "the walk found the same address dlsym did");

    check(unique::elf::find_symbol(found.symbols, "unique_elf_probe_absent") == nullptr,
          "a name the library does not define is not found");

    // The imported one. It is in `.dynsym` as undefined, and a walk that ignored
    // `st_shndx` would answer with the library's own base address.
    void* imported = unique::elf::find_symbol(found.symbols, "strlen");
    check(imported == nullptr || imported == reinterpret_cast<void*>(&strlen),
          "an undefined symbol is refused rather than answered with the load address");

    unique::elf::DynamicSymbols empty;
    check(unique::elf::find_symbol(empty, "anything") == nullptr,
          "an unreadable table finds nothing rather than reading through null");
    check(unique::elf::read_symbols(nullptr).valid() == false,
          "a library with no dynamic section is refused");
    check(unique::elf::gnu_hash_symbol_count(nullptr) == 0,
          "a missing GNU hash table implies no symbols rather than crashing");

    std::printf("%d checks, %d failures  (elf_symbols)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
