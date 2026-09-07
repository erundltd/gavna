#pragma once
// PLT/GOT hooking.
//
// This is deliberately *not* inline hooking. An inline hook rewrites the first
// instructions of a function, which on ARM64 has to reason about BTI landing pads, PAC
// signing, literal pools and the instruction cache, and gets a partially-patched libc
// wrong in ways that corrupt data silently. A PLT hook writes one pointer into a
// library's Global Offset Table: nothing executable is modified, the failure mode is
// "this library was not hooked" rather than "this library is now broken", and the scope
// is explicit — exactly the libraries asked for, and no others.
//
// What that buys and what it costs:
//
//   - It catches calls *out of* a hooked library, which is what UNIQUE needs: a guest's
//     own code calling fopen("/data/data/<pkg>/…").
//   - It does not catch calls libc makes to itself. fopen() reaching open() internally is
//     invisible here, and correctly so: the path was already redirected on the way in.
//   - It does not catch a library that is loaded later. Rehooking is the caller's job,
//     which is why install() is idempotent and cheap to repeat.

#include <cstddef>
#include <string>
#include <vector>

namespace unique::plt {

struct HookRequest {
    const char* symbol;
    void* replacement;
    void** original;   // filled with the previous GOT value, may be null
    /// Set by hook_all when any library in the process referenced this symbol at all.
    /// A symbol nothing imports is not an error - most libraries want a handful of these
    /// - but "hooked and never called" and "never hooked because nothing in this process
    /// spells it that way" are different problems, and only the report can tell them
    /// apart. The fourteenth phone log is the argument: `stat` was in the table, was
    /// never patched anywhere, and looked exactly like a symbol nobody used.
    bool matched = false;
};

struct HookReport {
    int libraries_scanned = 0;
    int libraries_matched = 0;
    int libraries_excluded = 0;
    int slots_patched = 0;
    std::vector<std::string> failures;
    /// Library names seen, filled only when nothing matched. "No library matched" and
    /// "the filter is not the shape dl_iterate_phdr reports" are indistinguishable
    /// without it, and the second is the one that actually happens.
    std::vector<std::string> sample;
    /// Libraries in scope that an exclusion kept out, so a library that is *not* hooked
    /// on purpose never looks like one the scan failed to find.
    std::vector<std::string> excluded;
    /// Libraries skipped because a previous scan already walked them. The scan is
    /// re-run on every library load, and with a process-wide scope re-walking four
    /// hundred libraries' relocations each time is not free.
    int libraries_already_scanned = 0;
    /// "<library>=<slots>" for the libraries this pass actually patched something in,
    /// newest first and bounded. Which library a redirect reached is otherwise a number
    /// with no name attached to it.
    std::vector<std::string> per_library;
};

// Patches every address slot in the loaded libraries whose path contains one of
// `path_filters` for each requested symbol. A single filter of "*" means every library,
// which is the scope UNIQUE runs with: see io_redirect::set_scope for why a narrower one
// cannot work, and what makes a wide one safe.
//
// A library whose path contains one of `path_excludes` is skipped even when it is in
// scope, and named in the report. Exclusions exist because a PLT hook is not universally
// safe: a code-virtualization protector verifies its own GOT and answers a patched slot
// by jumping into its own generated code with a corrupt dispatch value. See
// io_redirect::set_exclusions for the run that established this.
//
// Idempotent: a slot already pointing at the replacement is left alone and not counted.
//
// `seen` is the caller's memo of libraries a previous call with *these* requests already
// walked, and is appended to. Each set of requests needs its own: two hook sets sharing
// one memo would have the first one's scan mark libraries as done for the second, which
// would then patch nothing in them and never say so. Pass an empty vector to walk
// everything.
HookReport hook_all(const std::vector<std::string>& path_filters,
                    const std::vector<std::string>& path_excludes,
                    HookRequest* requests, size_t request_count,
                    std::vector<std::string>& seen);

}  // namespace unique::plt
