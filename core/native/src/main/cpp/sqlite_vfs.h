#pragma once
// Redirecting SQLite's file operations through SQLite's own front door.
//
// ## Why a GOT hook is not enough here, twice over
//
// SQLite reaches the filesystem through a table of function pointers rather than through
// calls, and it publishes an interface for replacing them. Both facts matter and the
// second one is the fix:
//
// ```c
// static struct unix_syscall {
//   const char *zName; sqlite3_syscall_ptr pCurrent; ...
// } aSyscall[] = {
//   { "open",  (sqlite3_syscall_ptr)posixOpen, 0 },   /* a local wrapper -> the PLT   */
//   { "stat",  (sqlite3_syscall_ptr)stat,      0 },   /* the address     -> not a call */
// ```
//
// The fourteenth phone run is what the first line of that costs: `open` was redirected and
// `stat` was not, in one library, so SQLite created the probe database inside the instance
// and then could not find it at the path it had been handed —
// `file renamed while open`, then `SQLITE_IOERR_FSTAT`.
//
// The answer written then was to patch `R_AARCH64_ABS64` relocations in `libsqlite.so`.
// The seventeenth run says that did not happen: `io_redirect: hooked libsqlite.so=2+abs`,
// two slots, and the identical SQLite message underneath it. Two is the PLT count alone.
// Android's platform libraries are linked with `--pack-dyn-relocs`, so `.rela.dyn` is not
// an array of `ElfW(Rela)` at all — it is an APS2 blob under `DT_ANDROID_RELA`, which the
// scan does not parse and therefore reports as "this library has no data relocations".
// The mechanism was correct about SQLite and wrong about the file format.
//
// ## What this does instead
//
// `sqlite3_vfs` version 3 carries `xSetSystemCall`, which exists for exactly this purpose:
// it replaces an entry of `aSyscall` by name. It reaches every use, including the ones a
// relocation never produced, and it does not depend on how the library was linked.
//
// Getting to it needs one thing the app's linker namespace will not give: the address of
// `sqlite3_vfs_find` in a library that cannot be `dlopen`ed. `elf_symbols.h` reads it out
// of the loaded library's own dynamic symbol table.
//
// Every failure — library not loaded yet, symbol absent, a VFS older than version 3, a
// name this build does not have — leaves SQLite exactly as it was and is reported. The
// result is the behaviour that shipped before, never a half-replaced syscall table.

#include <cstddef>
#include <string>

namespace unique::sqlite_vfs {

/// One replacement: SQLite's name for the call, and the trampoline to put there.
struct Override {
    const char* name;
    void* function;
};

/// What one attempt found, for the log line that has to answer this next time.
struct Report {
    bool library_found = false;
    bool api_found = false;
    int vfs_version = 0;
    int attempted = 0;
    int replaced = 0;
    std::string detail;
};

/// Replaces the named system calls in the default VFS. Safe to call repeatedly.
///
/// Idempotent by construction: setting a call to the value it already holds is what the
/// second pass does, and SQLite accepts it. Callers repeat it because `libsqlite.so` is
/// mapped when the first database opens, which for many guests is after the graft.
Report install(const Override* overrides, size_t count);

/// How many calls the last successful [install] replaced. Zero until one succeeds.
int replaced();

}  // namespace unique::sqlite_vfs
