#pragma once
// Finding an exported function in a library that is loaded and cannot be `dlopen`ed.
//
// An app's linker namespace publishes the NDK list and nothing else, so `dlopen`ing
// `libsqlite.so` — a platform library already mapped into every process that has ever
// opened a database — answers `library "libsqlite.so" not found`, and `dlsym(RTLD_DEFAULT)`
// does not search it either. The library is nonetheless *there*, its dynamic section is
// readable, and an exported symbol is one linear walk of `.dynsym` away.
//
// Two things make that walk non-obvious, and both are why this is a file with a test
// rather than six lines inside its one caller.
//
// **`.dynsym` carries no length.** Both hash tables encode one and which one a library has
// depends on how it was linked, so both are read: `DT_HASH` states it outright — `nchain`
// is the number of symbols — while `DT_GNU_HASH` has to have it recovered, by finding the
// largest bucket and walking its chain to the entry whose low bit is set. A library with
// neither is refused rather than guessed at.
//
// **`d_un.d_ptr` is not always a link-time address.** The ELF specification says it is one,
// and bionic leaves it that way, so on Android the address is `load_bias + d_ptr`. glibc
// relocates the dynamic section in place, so the same arithmetic there adds the bias
// twice and reads a page that is not mapped. That is not a portability nicety — it is the
// difference between this file having a host-side test and not having one, and a walk
// nobody can run off a device is a walk nobody checks. The `PT_LOAD` headers say which
// case a library is in exactly: a `d_ptr` inside the link-time span needs the bias, and
// one outside it has already had the bias applied.
//
// Everything here is read-only and every failure returns "not found", because the caller's
// fallback — leaving SQLite's syscalls alone — is the behaviour that was already shipping.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <elf.h>
#include <link.h>

namespace unique::elf {

/// A loaded library's dynamic symbol table, with the bound that makes it walkable.
struct DynamicSymbols {
    const ElfW(Sym)* symtab = nullptr;
    const char* strtab = nullptr;
    size_t count = 0;
    ElfW(Addr) base = 0;

    bool valid() const { return symtab != nullptr && strtab != nullptr && count > 0; }
};

/// The number of `.dynsym` entries a `DT_GNU_HASH` table implies.
///
/// The layout is `nbuckets, symoffset, bloom_size, bloom_shift`, then `bloom_size` words
/// of Bloom filter, then `nbuckets` bucket heads, then the chain — one word per symbol
/// from `symoffset` onward, with the low bit set on the last of each chain. Symbols below
/// `symoffset` are the undefined ones and are in no bucket, which is why a table with no
/// exported symbols at all still accounts for `symoffset` entries.
inline size_t gnu_hash_symbol_count(const uint32_t* table) {
    if (table == nullptr) return 0;
    const uint32_t nbuckets = table[0];
    const uint32_t symoffset = table[1];
    const uint32_t bloom_size = table[2];
    if (nbuckets == 0) return symoffset;

    const auto* bloom = reinterpret_cast<const ElfW(Addr)*>(&table[4]);
    const auto* buckets = reinterpret_cast<const uint32_t*>(&bloom[bloom_size]);
    const uint32_t* chain = buckets + nbuckets;

    uint32_t last = 0;
    for (uint32_t i = 0; i < nbuckets; ++i) {
        if (buckets[i] > last) last = buckets[i];
    }
    if (last < symoffset) return symoffset;

    // Walk to the end of the chain the largest bucket starts. Bounded so a table that is
    // not the shape this expects stops rather than running off the mapping.
    uint32_t index = last;
    for (uint32_t guard = 0; guard < (1u << 20); ++guard) {
        if ((chain[index - symoffset] & 1u) != 0) return index + 1;
        ++index;
    }
    return 0;
}

/// The link-time address span of a library's `PT_LOAD` segments.
struct LoadSpan {
    ElfW(Addr) low = 0;
    ElfW(Addr) high = 0;

    bool contains(ElfW(Addr) address) const {
        return high > low && address >= low && address < high;
    }
};

inline LoadSpan load_span(const ElfW(Phdr)* phdr, int count) {
    LoadSpan span;
    bool first = true;
    for (int i = 0; i < count; ++i) {
        if (phdr[i].p_type != PT_LOAD) continue;
        const ElfW(Addr) start = phdr[i].p_vaddr;
        const ElfW(Addr) end = phdr[i].p_vaddr + phdr[i].p_memsz;
        if (first || start < span.low) span.low = start;
        if (first || end > span.high) span.high = end;
        first = false;
    }
    return span;
}

/// Turns a `d_un.d_ptr` into an address in this process. See the header comment.
inline ElfW(Addr) resolve(ElfW(Addr) pointer, ElfW(Addr) bias, const LoadSpan& span) {
    return span.contains(pointer) ? bias + pointer : pointer;
}

/// Reads the symbol table out of one loaded library, or an invalid result.
inline DynamicSymbols read_symbols(const ElfW(Dyn)* dyn, ElfW(Addr) bias,
                                   const LoadSpan& span) {
    DynamicSymbols out;
    out.base = bias;
    if (dyn == nullptr) return out;

    const uint32_t* sysv_hash = nullptr;
    const uint32_t* gnu_hash = nullptr;
    for (; dyn->d_tag != DT_NULL; ++dyn) {
        const ElfW(Addr) at = resolve(dyn->d_un.d_ptr, bias, span);
        switch (dyn->d_tag) {
            case DT_SYMTAB:
                out.symtab = reinterpret_cast<const ElfW(Sym)*>(at);
                break;
            case DT_STRTAB:
                out.strtab = reinterpret_cast<const char*>(at);
                break;
            case DT_HASH:
                sysv_hash = reinterpret_cast<const uint32_t*>(at);
                break;
            case DT_GNU_HASH:
                gnu_hash = reinterpret_cast<const uint32_t*>(at);
                break;
            default:
                break;
        }
    }

    // `DT_HASH` first: it states the count rather than implying it, so when a library
    // carries both there is nothing to reconstruct.
    if (sysv_hash != nullptr) {
        out.count = sysv_hash[1];
    } else if (gnu_hash != nullptr) {
        out.count = gnu_hash_symbol_count(gnu_hash);
    }
    return out;
}

/// Reads the symbol table of the library `dl_iterate_phdr` is reporting.
inline DynamicSymbols read_symbols(const struct dl_phdr_info* info) {
    if (info == nullptr || info->dlpi_phdr == nullptr) return {};
    const LoadSpan span = load_span(info->dlpi_phdr, info->dlpi_phnum);
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        if (info->dlpi_phdr[i].p_type != PT_DYNAMIC) continue;
        const auto* dyn = reinterpret_cast<const ElfW(Dyn)*>(
                info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
        return read_symbols(dyn, info->dlpi_addr, span);
    }
    return {};
}

/// The address of an exported, defined symbol named [name], or null.
///
/// Undefined entries are skipped: a library that *imports* `sqlite3_vfs_find` has it in
/// its `.dynsym` with `st_shndx == SHN_UNDEF`, and returning the library's own base for
/// that would be a pointer to its ELF header.
inline void* find_symbol(const DynamicSymbols& symbols, const char* name) {
    if (!symbols.valid() || name == nullptr) return nullptr;
    for (size_t i = 0; i < symbols.count; ++i) {
        const ElfW(Sym)& sym = symbols.symtab[i];
        if (sym.st_shndx == SHN_UNDEF || sym.st_value == 0) continue;
        const char* candidate = symbols.strtab + sym.st_name;
        if (std::strcmp(candidate, name) != 0) continue;
        return reinterpret_cast<void*>(symbols.base + sym.st_value);
    }
    return nullptr;
}

}  // namespace unique::elf
