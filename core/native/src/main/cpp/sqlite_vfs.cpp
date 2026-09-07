#include "sqlite_vfs.h"

#include <cstring>
#include <dlfcn.h>
#include <link.h>
#include <mutex>

#include "elf_symbols.h"
#include "unique_native.h"

namespace unique::sqlite_vfs {
namespace {

/// SQLite's own VFS structure, copied from `sqlite3.h`.
///
/// Declared here rather than depending on a header UNIQUE does not ship, and safe to
/// declare: this layout has not changed since SQLite 3.7.6 in 2011, `iVersion` is the
/// first field precisely so a caller can refuse an older one, and every field past the
/// version check is only ever *read* through a pointer the library itself handed back.
using sqlite3_syscall_ptr = void (*)(void);
struct sqlite3_file;
struct sqlite3_vfs {
    int iVersion;
    int szOsFile;
    int mxPathname;
    sqlite3_vfs* pNext;
    const char* zName;
    void* pAppData;
    int (*xOpen)(sqlite3_vfs*, const char*, sqlite3_file*, int, int*);
    int (*xDelete)(sqlite3_vfs*, const char*, int);
    int (*xAccess)(sqlite3_vfs*, const char*, int, int*);
    int (*xFullPathname)(sqlite3_vfs*, const char*, int, char*);
    void* (*xDlOpen)(sqlite3_vfs*, const char*);
    void (*xDlError)(sqlite3_vfs*, int, char*);
    void (*(*xDlSym)(sqlite3_vfs*, void*, const char*))(void);
    void (*xDlClose)(sqlite3_vfs*, void*);
    int (*xRandomness)(sqlite3_vfs*, int, char*);
    int (*xSleep)(sqlite3_vfs*, int);
    int (*xCurrentTime)(sqlite3_vfs*, double*);
    int (*xGetLastError)(sqlite3_vfs*, int, char*);
    // Version 2.
    int (*xCurrentTimeInt64)(sqlite3_vfs*, int64_t*);
    // Version 3 — the three this file exists for.
    int (*xSetSystemCall)(sqlite3_vfs*, const char*, sqlite3_syscall_ptr);
    sqlite3_syscall_ptr (*xGetSystemCall)(sqlite3_vfs*, const char*);
    const char* (*xNextSystemCall)(sqlite3_vfs*, const char*);
};

constexpr int kSqliteOk = 0;

std::mutex g_mutex;
int g_replaced = 0;

struct FindContext {
    void* vfs_find = nullptr;
    bool library_seen = false;
};

/// True when this library is the platform's SQLite and not something that merely
/// contains the word: an app bundling its own `libsqlite3x.so` has its own syscall table
/// and is none of UNIQUE's business.
bool is_platform_sqlite(const char* path) {
    if (path == nullptr) return false;
    const char* slash = std::strrchr(path, '/');
    const char* name = slash != nullptr ? slash + 1 : path;
    return std::strcmp(name, "libsqlite.so") == 0;
}

int look_for_sqlite(struct dl_phdr_info* info, size_t, void* data) {
    auto* ctx = static_cast<FindContext*>(data);
    if (!is_platform_sqlite(info->dlpi_name)) return 0;
    ctx->library_seen = true;
    ctx->vfs_find = elf::find_symbol(elf::read_symbols(info), "sqlite3_vfs_find");
    return 1;
}

}  // namespace

Report install(const Override* overrides, size_t count) {
    std::lock_guard<std::mutex> lock(g_mutex);
    Report report;
    if (overrides == nullptr || count == 0) {
        report.detail = "nothing to replace";
        return report;
    }

    FindContext ctx;
    dl_iterate_phdr(look_for_sqlite, &ctx);
    report.library_found = ctx.library_seen;
    if (!ctx.library_seen) {
        report.detail = "libsqlite.so is not mapped in this process yet";
        return report;
    }
    if (ctx.vfs_find == nullptr) {
        report.detail = "libsqlite.so does not export sqlite3_vfs_find";
        return report;
    }
    report.api_found = true;

    auto vfs_find = reinterpret_cast<sqlite3_vfs* (*)(const char*)>(ctx.vfs_find);
    sqlite3_vfs* vfs = vfs_find(nullptr);
    if (vfs == nullptr) {
        report.detail = "sqlite3_vfs_find(0) answered no default VFS";
        return report;
    }
    report.vfs_version = vfs->iVersion;
    if (vfs->iVersion < 3 || vfs->xSetSystemCall == nullptr) {
        report.detail = "the default VFS is older than version 3 and has no xSetSystemCall";
        return report;
    }

    for (size_t i = 0; i < count; ++i) {
        if (overrides[i].name == nullptr || overrides[i].function == nullptr) continue;
        report.attempted++;
        const int rc = vfs->xSetSystemCall(
                vfs, overrides[i].name,
                reinterpret_cast<sqlite3_syscall_ptr>(overrides[i].function));
        if (rc == kSqliteOk) {
            report.replaced++;
        } else if (report.detail.empty()) {
            // One name, not all of them: a build without `lstat` in its table is normal
            // and the first refusal is enough to say which shape of table this is.
            report.detail = std::string("this build has no ") + overrides[i].name;
        }
    }
    if (report.replaced > 0) g_replaced = report.replaced;
    if (report.detail.empty()) report.detail = "every name in the table was replaced";
    return report;
}

int replaced() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_replaced;
}

}  // namespace unique::sqlite_vfs
