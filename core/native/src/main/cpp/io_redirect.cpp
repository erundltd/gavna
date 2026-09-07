#include <cstdarg>
#include <cstdio>
#include <cerrno>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <climits>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <vector>

#include "elf_symbols.h"
#include "plt_hook.h"
#include "proc_view.h"
#include "sqlite_vfs.h"
#include "syscall_paths.h"
#include "redirect_table.h"
#include "unique_native.h"

namespace unique::io_redirect {
namespace {

RedirectTable g_table;
ProcView g_proc_view;
std::mutex g_mutex;
bool g_installed = false;

}  // namespace

void set_rules(const char** from, const char** to, int count) {
    std::vector<RedirectRule> rules;
    rules.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (from[i] == nullptr || to[i] == nullptr) continue;
        rules.push_back(RedirectRule{from[i], to[i]});
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_table.set(std::move(rules));
    ULOGI("redirect table set: %zu rule(s)", g_table.size());
}

void clear_rules() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_table.clear();
}

int rule_count() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return static_cast<int>(g_table.size());
}

std::string redirect(const char* path) {
    std::string out;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_table.redirect(path, out)) return out;
    return {};
}

void set_proc_view(const char** from, const char** to, int count) {
    std::vector<RedirectRule> rules;
    rules.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (from[i] == nullptr || to[i] == nullptr) continue;
        rules.push_back(RedirectRule{from[i], to[i]});
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_proc_view.set(std::move(rules));
    ULOGI("proc view set: %zu rule(s)", g_proc_view.size());
}

void clear_proc_view() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_proc_view.clear();
}

int proc_view_rule_count() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return static_cast<int>(g_proc_view.size());
}

std::string proc_view_rewrite(const char* text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_proc_view.rewrite(text == nullptr ? std::string() : std::string(text));
}

bool installed() { return g_installed; }

// ---------------------------------------------------------------------------------
// The interception.
//
// Every trampoline has the same three-line shape: rewrite the path if a rule matches,
// call through to the original, done. The uniformity is the point - a trampoline that
// does anything else is a place for a bug to hide in code that runs on every file
// operation a virtual app performs.
//
// Only the *outermost* call matters. A guest calling fopen() reaches open() inside libc
// without crossing a PLT, and that is correct: the path was already rewritten on the way
// in, and rewriting it twice would be wrong.
// ---------------------------------------------------------------------------------

// Declared at namespace scope, deliberately: inside the anonymous namespace below this
// would declare a *different* symbol from the definition further down, and the call from
// the trampolines becomes ambiguous rather than resolving to the real one.
InstallStatus install_locked();

/// Installs (or extends) the library-load watch. Idempotent; see watch_library_loads().
InstallStatus watch_locked();

namespace {

// Originals, filled by the PLT hook. Null until install() runs, and every trampoline
// falls back to the libc symbol if it is - so a partially installed hook degrades to
// "no redirection" rather than a null call.
int (*o_open)(const char*, int, ...) = nullptr;
int (*o_openat)(int, const char*, int, ...) = nullptr;
int (*o_stat)(const char*, struct stat*) = nullptr;
int (*o_lstat)(const char*, struct stat*) = nullptr;
int (*o_access)(const char*, int) = nullptr;
int (*o_mkdir)(const char*, mode_t) = nullptr;
int (*o_rmdir)(const char*) = nullptr;
int (*o_unlink)(const char*) = nullptr;
int (*o_rename)(const char*, const char*) = nullptr;
int (*o_chmod)(const char*, mode_t) = nullptr;
DIR* (*o_opendir)(const char*) = nullptr;
FILE* (*o_fopen)(const char*, const char*) = nullptr;
ssize_t (*o_readlink)(const char*, char*, size_t) = nullptr;
int (*o_statfs)(const char*, struct statfs*) = nullptr;

// The `*at` family, which is what the platform's own libraries call.
//
// The plain names above are enough for a guest's own `.so` files, compiled against NDK
// headers where `stat`, `mkdir` and the rest are real exported functions. They are *not*
// enough for `libjavacore.so`, which is where every `java.io.File`, `FileInputStream` and
// `SharedPreferences` write in the process ends up: `libcore.io.Linux` is written against
// the directory-relative calls, so a guest's Java code reaches `openat` and `fstatat64`
// and never touches `open` or `stat` at all.
//
// A relative path with a real `dirfd` is left alone by construction: the redirect table
// only matches paths beginning with `/`.
int (*o_fstatat)(int, const char*, struct stat*, int) = nullptr;

// The fortified spellings. Separate originals, because they are separate symbols with
// separate addresses and calling one through the other's saved pointer would pass the
// wrong number of arguments.
int (*o_open_2)(const char*, int) = nullptr;
int (*o_openat_2)(int, const char*, int) = nullptr;
ssize_t (*o_readlink_chk)(const char*, char*, size_t, size_t) = nullptr;
ssize_t (*o_readlinkat_chk)(int, const char*, char*, size_t, size_t) = nullptr;

// Three more that take a path and were simply absent from the table. `freopen` opens one
// outright; `statx` is the modern `stat` a recent libc++ `std::filesystem` reaches for.
FILE* (*o_freopen)(const char*, const char*, FILE*) = nullptr;
int (*o_statx)(int, const char*, int, unsigned int, void*) = nullptr;

// The one an engine reaches for when it does not want libc at all. See syscall_paths.h.
long (*o_syscall)(long, ...) = nullptr;
int (*o_faccessat)(int, const char*, int, int) = nullptr;
int (*o_mkdirat)(int, const char*, mode_t) = nullptr;
int (*o_unlinkat)(int, const char*, int) = nullptr;
int (*o_renameat)(int, const char*, int, const char*) = nullptr;
ssize_t (*o_readlinkat)(int, const char*, char*, size_t) = nullptr;
int (*o_fchmodat)(int, const char*, mode_t, int) = nullptr;
char* (*o_realpath)(const char*, char*) = nullptr;
int (*o_statvfs)(const char*, struct statvfs*) = nullptr;
int (*o_remove)(const char*) = nullptr;
int (*o_creat)(const char*, mode_t) = nullptr;

/// Rewrites `path` when a rule matches, otherwise hands back the original pointer.
///
/// The `holder` keeps the rewritten string alive for the duration of the call. Returning
/// a std::string by value and taking .c_str() of a temporary is the obvious way to write
/// this and is a dangling pointer.
const char* rewrite(const char* path, std::string& holder) {
    if (path == nullptr) return nullptr;
    std::string out;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_table.redirect(path, out)) return path;
    }
    holder = std::move(out);
    return holder.c_str();
}

/// Reads a `/proc` pseudo-file whole, without going back through the hooks.
///
/// `st_size` is zero for everything under `/proc`, so the only way to know how much there
/// is, is to read until there is not any more. A Unity game's maps runs to a few hundred
/// kilobytes; the growth below reaches that in five reads.
std::string read_all(const char* path) {
    const int fd = o_open != nullptr ? o_open(path, O_RDONLY | O_CLOEXEC, 0)
                                     : ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    std::string out;
    char buffer[16384];
    for (;;) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            out.append(buffer, static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    ::close(fd);
    return out;
}

/// A read-only file descriptor holding [text], or -1.
///
/// `memfd_create` rather than a file on disk: a temporary file would be one more thing in
/// the guest's own directory for the guest to find, would need cleaning up after a crash,
/// and would appear in the very list this is rewriting.
int fd_holding(const std::string& text, bool cloexec) {
    const int fd = ::memfd_create("maps", cloexec ? MFD_CLOEXEC : 0u);
    if (fd < 0) return -1;
    size_t written = 0;
    while (written < text.size()) {
        const ssize_t n = ::write(fd, text.data() + written, text.size() - written);
        if (n > 0) {
            written += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        ::close(fd);
        return -1;
    }
    if (::lseek(fd, 0, SEEK_SET) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

/// Serves [path] from the guest's view of it, or -1 to let the real open proceed.
///
/// Every failure falls through to the real file. A guest that can read its own maps and
/// sees UNIQUE in them is a guest that may refuse to run; a guest that cannot read them
/// at all is one that crashes in its own crash handler.
int serve_proc(const char* path, bool cloexec) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_proc_view.empty()) return -1;
        if (!ProcView::covers(path, ::getpid())) return -1;
    }
    const std::string real = read_all(path);
    if (real.empty()) return -1;
    std::string shown;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        shown = g_proc_view.rewrite(real);
    }
    return fd_holding(shown, cloexec);
}

int h_open(const char* path, int flags, ...) {
    const int served = serve_proc(path, (flags & O_CLOEXEC) != 0);
    if (served >= 0) return served;
    std::string holder;
    const char* target = rewrite(path, holder);
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, unsigned));
        va_end(args);
    }
    return o_open != nullptr ? o_open(target, flags, mode) : ::open(target, flags, mode);
}

int h_openat(int dirfd, const char* path, int flags, ...) {
    const int served = serve_proc(path, (flags & O_CLOEXEC) != 0);
    if (served >= 0) return served;
    std::string holder;
    const char* target = rewrite(path, holder);
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, unsigned));
        va_end(args);
    }
    return o_openat != nullptr ? o_openat(dirfd, target, flags, mode)
                               : ::openat(dirfd, target, flags, mode);
}

/// `open` with SQLite's signature rather than libc's.
///
/// `aSyscall[]` calls its entries through a fixed prototype —
/// `int (*)(const char*, int, int)` — and the varargs trampoline above is not that type.
/// Calling one through the other happens to work on AAPCS64 and is undefined by the
/// language; a three-argument wrapper costs one jump and removes the question.
int h_open_fixed(const char* path, int flags, int mode) {
    return h_open(path, flags, mode);
}

#define UNIQUE_TRAMPOLINE_1(name, ret, sig_type)                       \
    ret h_##name(const char* path) {                                   \
        std::string holder;                                            \
        const char* target = rewrite(path, holder);                    \
        return o_##name != nullptr ? o_##name(target) : ::name(target); \
    }

UNIQUE_TRAMPOLINE_1(rmdir, int, int)
UNIQUE_TRAMPOLINE_1(unlink, int, int)
UNIQUE_TRAMPOLINE_1(opendir, DIR*, DIR*)

int h_stat(const char* path, struct stat* out) {
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_stat != nullptr ? o_stat(target, out) : ::stat(target, out);
}

int h_lstat(const char* path, struct stat* out) {
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_lstat != nullptr ? o_lstat(target, out) : ::lstat(target, out);
}

int h_access(const char* path, int mode) {
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_access != nullptr ? o_access(target, mode) : ::access(target, mode);
}

int h_mkdir(const char* path, mode_t mode) {
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_mkdir != nullptr ? o_mkdir(target, mode) : ::mkdir(target, mode);
}

int h_chmod(const char* path, mode_t mode) {
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_chmod != nullptr ? o_chmod(target, mode) : ::chmod(target, mode);
}

int h_rename(const char* from, const char* to) {
    std::string from_holder, to_holder;
    const char* from_target = rewrite(from, from_holder);
    const char* to_target = rewrite(to, to_holder);
    return o_rename != nullptr ? o_rename(from_target, to_target)
                               : ::rename(from_target, to_target);
}

FILE* h_fopen(const char* path, const char* mode) {
    // `fopen("/proc/self/maps", "r")` is the commonest spelling of the check by a long
    // way, and it does not reach h_open: libc calls its own open internally, without
    // crossing a PLT. Serving it here is what makes the view actually cover anything.
    const int served = serve_proc(path, mode != nullptr && std::strchr(mode, 'e') != nullptr);
    if (served >= 0) {
        FILE* stream = ::fdopen(served, "r");
        if (stream != nullptr) return stream;
        ::close(served);
    }
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_fopen != nullptr ? o_fopen(target, mode) : ::fopen(target, mode);
}

ssize_t h_readlink(const char* path, char* buf, size_t size) {
    std::string holder;
    const char* target = rewrite(path, holder);
    const ssize_t n = o_readlink != nullptr ? o_readlink(target, buf, size)
                                            : ::readlink(target, buf, size);
    if (n <= 0 || buf == nullptr) return n;

    // The answer is a path, and under `/proc/self/fd` it is a path into UNIQUE. An app
    // that walks its own open descriptors — which a protector does, looking for exactly
    // this — reads the APK it was loaded from by name.
    //
    // `readlink` does not terminate the buffer and the caller is not entitled to a byte
    // past what is returned, so a longer answer is truncated rather than written past the
    // end. Truncation is what the real call does when the buffer is short.
    std::string shown;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_proc_view.empty()) return n;
        if (!g_proc_view.rewrite_path(buf, static_cast<size_t>(n), shown)) return n;
    }
    const size_t copied = shown.size() < size ? shown.size() : size;
    std::memcpy(buf, shown.data(), copied);
    return static_cast<ssize_t>(copied);
}

FILE* h_freopen(const char* path, const char* mode, FILE* stream) {
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_freopen != nullptr ? o_freopen(target, mode, stream)
                                : ::freopen(target, mode, stream);
}

/// `statx(dirfd, path, flags, mask, buf)` — the path is the second argument.
///
/// The buffer type is opaque here on purpose: `struct statx` needs a kernel header this
/// file does not include, and nothing in the trampoline looks inside it.
int h_statx(int dirfd, const char* path, int flags, unsigned int mask, void* out) {
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_statx != nullptr ? o_statx(dirfd, target, flags, mask, out) : -1;
}

/// Rewrites a `readlink`-style answer through the outward view. Defined further down,
/// beside the other `*at` trampolines; declared here because the syscall path reaches it
/// first.
ssize_t view_readlink_result(char* buf, ssize_t n, size_t size);

/// `syscall()`, with the path argument rewritten for the numbers that have one.
///
/// Standoff 2's engine imports two of the forty-one libc names in the table — `realpath`
/// and `readlink` — and neither of them opens a file. It issues its file operations as raw
/// syscalls, which is why every path hook redirected nothing for it and why the game could
/// not open the APK path UNIQUE had published to it. `syscall_paths.h` has the run that
/// established this and the table.
///
/// Six arguments are always read and always forwarded. On AAPCS64 the variadic arguments
/// live in registers, so reading one the caller did not pass yields a register value that
/// is then handed on unchanged — the kernel takes only as many as the number needs. What
/// must not happen is *work* on a number that carries no path: `futex` is most of what a
/// game's threads issue, and it is answered here by one comparison and a forward, without
/// touching the redirect table's lock. That is also what keeps this out of a deadlock with
/// the lock's own futex.
long h_syscall(long number, ...) {
    va_list args;
    va_start(args, number);
    long a[6];
    for (long& value : a) value = va_arg(args, long);
    va_end(args);

    const auto path_args = syscall_paths::path_arguments(number);
    if (!path_args.any()) {
        return o_syscall != nullptr ? o_syscall(number, a[0], a[1], a[2], a[3], a[4], a[5])
                                    : -1;
    }

    std::string first_holder, second_holder;
    a[path_args.first] = reinterpret_cast<long>(
            rewrite(reinterpret_cast<const char*>(a[path_args.first]), first_holder));
    if (path_args.second != syscall_paths::PathArguments::kNone) {
        a[path_args.second] = reinterpret_cast<long>(
                rewrite(reinterpret_cast<const char*>(a[path_args.second]), second_holder));
    }
    const long result = o_syscall != nullptr
            ? o_syscall(number, a[0], a[1], a[2], a[3], a[4], a[5])
            : -1;
    // `readlinkat` answers with a path, and under `/proc/self/fd` that path names UNIQUE.
    if (result > 0 && syscall_paths::answers_with_a_path(number)) {
        return view_readlink_result(reinterpret_cast<char*>(a[2]), result,
                                    static_cast<size_t>(a[3]));
    }
    return result;
}

int h_statfs(const char* path, struct statfs* out) {
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_statfs != nullptr ? o_statfs(target, out) : ::statfs(target, out);
}

// ---------------------------------------------------------------------------------
// The `*at` family. Same three lines each, with the path in second position.
// ---------------------------------------------------------------------------------

int h_fstatat(int dirfd, const char* path, struct stat* out, int flags) {
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_fstatat != nullptr ? o_fstatat(dirfd, target, out, flags)
                                : ::fstatat(dirfd, target, out, flags);
}

// The fortified spellings, which are what an app compiled with `_FORTIFY_SOURCE` calls
// and are *different symbols* from the ones they fortify.
//
// `open(path, O_RDONLY)` in a release build does not emit a call to `open`. Bionic's
// `bits/fortify/fcntl.h` turns a two-argument open into `__open_2(path, flags)`, and
// `openat` into `__openat_2`. The NDK enables `_FORTIFY_SOURCE` at every optimisation
// level, so this is not an edge case — it is what most third-party native code does.
//
// The eighteenth phone run is what this cost. Standoff 2's `libunity.so` was hooked, in
// the same process, 1.7 seconds before it tried and failed to open the APK path UNIQUE
// had just published to it:
//
//   io_redirect: hooked libunity.so=2
//   E Unity: ApkAddCentralDirectory : Unable to open '/data/app/~~kx_uUO…/base.apk'
//   E Unity: Failed to read assets/bin/Data/unity_app_guid
//
// and the game told its player "Not enough storage space to install required resources",
// because from its point of view its own APK was gone. Two patched slots in a 236 MB
// library was the whole tell, and the table's `__openat` — a name bionic does not export
// at all, so it could never match anything — was the other.
//
// `__open_2` cannot carry a mode: bionic rejects a two-argument open with `O_CREAT` at
// compile time, which is the entire reason the fortified form exists. So these take the
// arguments they are actually called with rather than being varargs.
int h_open_2(const char* path, int flags) {
    const int served = serve_proc(path, (flags & O_CLOEXEC) != 0);
    if (served >= 0) return served;
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_open_2 != nullptr ? o_open_2(target, flags) : ::open(target, flags);
}

int h_openat_2(int dirfd, const char* path, int flags) {
    const int served = serve_proc(path, (flags & O_CLOEXEC) != 0);
    if (served >= 0) return served;
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_openat_2 != nullptr ? o_openat_2(dirfd, target, flags)
                                 : ::openat(dirfd, target, flags);
}

/// Rewrites a `readlink` answer through the outward view. Shared by all four spellings.
ssize_t view_readlink_result(char* buf, ssize_t n, size_t size) {
    if (n <= 0 || buf == nullptr) return n;
    std::string shown;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_proc_view.empty()) return n;
        if (!g_proc_view.rewrite_path(buf, static_cast<size_t>(n), shown)) return n;
    }
    const size_t copied = shown.size() < size ? shown.size() : size;
    std::memcpy(buf, shown.data(), copied);
    return static_cast<ssize_t>(copied);
}

ssize_t h_readlink_chk(const char* path, char* buf, size_t size, size_t buf_size) {
    std::string holder;
    const char* target = rewrite(path, holder);
    const ssize_t n = o_readlink_chk != nullptr
            ? o_readlink_chk(target, buf, size, buf_size)
            : (o_readlink != nullptr ? o_readlink(target, buf, size)
                                     : ::readlink(target, buf, size));
    return view_readlink_result(buf, n, size);
}

ssize_t h_readlinkat_chk(int dirfd, const char* path, char* buf, size_t size,
                         size_t buf_size) {
    std::string holder;
    const char* target = rewrite(path, holder);
    const ssize_t n = o_readlinkat_chk != nullptr
            ? o_readlinkat_chk(dirfd, target, buf, size, buf_size)
            : (o_readlinkat != nullptr ? o_readlinkat(dirfd, target, buf, size)
                                       : ::readlinkat(dirfd, target, buf, size));
    return view_readlink_result(buf, n, size);
}

int h_faccessat(int dirfd, const char* path, int mode, int flags) {
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_faccessat != nullptr ? o_faccessat(dirfd, target, mode, flags)
                                  : ::faccessat(dirfd, target, mode, flags);
}

int h_mkdirat(int dirfd, const char* path, mode_t mode) {
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_mkdirat != nullptr ? o_mkdirat(dirfd, target, mode)
                                : ::mkdirat(dirfd, target, mode);
}

int h_unlinkat(int dirfd, const char* path, int flags) {
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_unlinkat != nullptr ? o_unlinkat(dirfd, target, flags)
                                 : ::unlinkat(dirfd, target, flags);
}

int h_renameat(int from_dirfd, const char* from, int to_dirfd, const char* to) {
    std::string from_holder, to_holder;
    const char* from_target = rewrite(from, from_holder);
    const char* to_target = rewrite(to, to_holder);
    return o_renameat != nullptr ? o_renameat(from_dirfd, from_target, to_dirfd, to_target)
                                 : ::renameat(from_dirfd, from_target, to_dirfd, to_target);
}

ssize_t h_readlinkat(int dirfd, const char* path, char* buf, size_t size) {
    std::string holder;
    const char* target = rewrite(path, holder);
    const ssize_t n = o_readlinkat != nullptr ? o_readlinkat(dirfd, target, buf, size)
                                              : ::readlinkat(dirfd, target, buf, size);
    if (n <= 0 || buf == nullptr) return n;
    std::string shown;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_proc_view.empty()) return n;
        if (!g_proc_view.rewrite_path(buf, static_cast<size_t>(n), shown)) return n;
    }
    const size_t copied = shown.size() < size ? shown.size() : size;
    std::memcpy(buf, shown.data(), copied);
    return static_cast<ssize_t>(copied);
}

int h_fchmodat(int dirfd, const char* path, mode_t mode, int flags) {
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_fchmodat != nullptr ? o_fchmodat(dirfd, target, mode, flags)
                                 : ::fchmodat(dirfd, target, mode, flags);
}

/// `File.getCanonicalPath()`, which resolves the path *and hands it back*.
///
/// Both halves matter and they pull opposite ways: the argument is redirected inward so
/// the call succeeds, and the answer is rewritten outward so what comes back is the path
/// the guest asked about rather than the one it really resolved to. Without the second
/// half, `getCanonicalPath()` would be the one call that hands a guest UNIQUE's directory
/// after everything else stopped doing so.
char* h_realpath(const char* path, char* resolved) {
    std::string holder;
    const char* target = rewrite(path, holder);
    char* answer = o_realpath != nullptr ? o_realpath(target, resolved)
                                         : ::realpath(target, resolved);
    if (answer == nullptr) return answer;
    std::string shown;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_proc_view.empty()) return answer;
        if (!g_proc_view.rewrite_path(answer, std::strlen(answer), shown)) return answer;
    }
    // A caller-supplied buffer is PATH_MAX by contract; one realpath allocated is at
    // least as long as what it wrote. Either way a longer answer is refused rather than
    // written past the end, because a buffer overrun here would be UNIQUE's worst bug.
    const size_t room = resolved != nullptr ? PATH_MAX : std::strlen(answer) + 1;
    if (shown.size() + 1 > room) return answer;
    std::memcpy(answer, shown.c_str(), shown.size() + 1);
    return answer;
}

int h_remove(const char* path) {
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_remove != nullptr ? o_remove(target) : ::remove(target);
}

int h_creat(const char* path, mode_t mode) {
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_creat != nullptr ? o_creat(target, mode) : ::creat(target, mode);
}

int h_statvfs(const char* path, struct statvfs* out) {
    std::string holder;
    const char* target = rewrite(path, holder);
    return o_statvfs != nullptr ? o_statvfs(target, out) : ::statvfs(target, out);
}

std::vector<std::string> g_filters;
std::vector<std::string> g_excludes;

/// Every PLT slot this process has patched, since the process started.
///
/// Cumulative, not "what the last scan found". A re-scan after a late dlopen legitimately
/// finds zero *new* slots — the ones already patched no longer point at the original libc
/// symbols, so they do not match again — and overwriting the total with that zero produced
/// a diagnostic that read as though the hooks had been lost:
///
///   io_redirect: rehooked after loading libprobevulkan.so (1 -> 0 slots)
///
/// They had not. But a number that only looks like a regression is worse than no number,
/// because it is the number someone will believe while looking for a bug that is not there.
int g_slots_patched = 0;
bool g_watching = false;

/// Whether SQLite's own syscall table has been replaced in this process.
///
/// Retried on every pass until it takes: `libsqlite.so` is mapped when the first database
/// is opened, which for a guest that opens one from `Application.onCreate` is after the
/// graft and for one that never opens a database is never.
bool g_sqlite_replaced = false;

// The loader entry points, captured when the watch is installed.
void* (*o_android_dlopen_ext)(const char*, int, const void*, const void*) = nullptr;

/// True while this thread is already re-scanning, so a load triggered from inside the
/// scan cannot recurse into it.
thread_local bool t_rescanning = false;

/// Re-hooks after a library has finished loading.
///
/// Called *after* the original returns, which matters: at that point the dynamic linker
/// has released its own lock, and `dl_iterate_phdr` — which install() uses and which
/// takes the same lock — can run without deadlocking. Doing this from inside the linker
/// would hang the process on a non-recursive mutex.
void rescan_after_load(const char* what) {
    if (t_rescanning) return;
    t_rescanning = true;
    const int before = g_slots_patched;
    install_locked();
    // And the *watch* itself, on the library that has just arrived.
    //
    // Without this the watch covers only what was loaded when it was installed. A library
    // loaded through `System.loadLibrary` is seen, because that goes through
    // `libnativeloader.so`, which was — but a library that library then `dlopen`s itself
    // is not, because its own `dlopen` slot was never patched.
    //
    // Unity is exactly that shape: `libmain.so` arrives through the loader and pulls in
    // `libunity.so` itself. The eighteenth phone run has no rescan for `libunity.so` at
    // all, and the game could not open the APK path UNIQUE had published to it.
    watch_locked();
    const int added = g_slots_patched - before;
    if (added > 0) {
        ULOGI("io_redirect: hooked %d new slot(s) after loading %s (%d total)",
              added, what == nullptr ? "?" : what, g_slots_patched);
    }
    t_rescanning = false;
}

/// `System.loadLibrary`'s route into the linker, and the only load this may intercept.
///
/// The path is redirected like any other, because a guest that has been told its
/// libraries are at `/data/app/~~…/lib/arm64` will ask for one by that name.
///
/// **`extinfo` is what makes this safe and its absence is what made the plain `dlopen`
/// hook unsafe.** `libnativeloader` passes an `android_dlextinfo` naming the class
/// loader's namespace explicitly, so the linker does not have to work out which namespace
/// to resolve in — see the comment on the watch's request list for the run that
/// established what happens when it does.
void* h_android_dlopen_ext(const char* path, int flags, const void* extinfo,
                           const void* caller) {
    std::string holder;
    const char* target = rewrite(path, holder);
    void* handle = o_android_dlopen_ext != nullptr
            ? o_android_dlopen_ext(target, flags, extinfo, caller)
            : nullptr;
    if (handle != nullptr) rescan_after_load(target);
    return handle;
}

/// The linker's own `dlopen`, which takes the caller's address instead of guessing it.
///
/// `libdl.so`'s `dlopen` is one line — `__loader_dlopen(name, flags,
/// __builtin_return_address(0))` — and that address decides which linker namespace a bare
/// soname is resolved in. A GOT hook that forwards through `libunique_native.so` replaces
/// the guest's namespace with UNIQUE's, and the twentieth phone run is what that costs:
///
///     Abort message: 'JNI FatalError called: Unable to load library: …/libunity.so
///         [dlopen failed: library "libunity.so" not found]'
///       at com.unity3d.player.UnityPlayer.loadNative
///
/// Calling `__loader_dlopen` directly with *our caller's* return address puts the
/// namespace back where it was, so the hook can redirect the path without moving the
/// resolution. Removing the hook instead — which the build after run 20 did — is not a
/// fix either: `libmain.so` loads `libunity.so` through plain `dlopen`, so nothing then
/// notices the load, `libunity.so` is never hooked, and the twenty-first run went
/// straight back to `ApkAddCentralDirectory : Unable to open`.
void* (*o_loader_dlopen)(const char*, int, const void*) = nullptr;

/// True once the linker's entry point has been located. The hook is installed only then:
/// a `dlopen` hook that cannot preserve the caller is worse than no hook at all.
bool loader_dlopen_available() { return o_loader_dlopen != nullptr; }

void* h_dlopen(const char* path, int flags) {
    // The address inside whichever library called us — `libmain.so`, not this one.
    const void* caller = __builtin_return_address(0);
    std::string holder;
    const char* target = rewrite(path, holder);
    void* handle = o_loader_dlopen != nullptr ? o_loader_dlopen(target, flags, caller)
                                              : nullptr;
    if (handle != nullptr) rescan_after_load(target);
    return handle;
}

/// Finds `__loader_dlopen`, which only the dynamic linker exports.
///
/// `dlsym` will not answer for it: it is not in any namespace an app can reach, and
/// `libdl.so` gets it through a relocation rather than a lookup. So the linker's own
/// dynamic symbol table is read directly, the same way `sqlite_vfs.cpp` reaches SQLite.
struct LoaderScan {
    void* found = nullptr;
};

int look_for_loader(struct dl_phdr_info* info, size_t, void* data) {
    auto* scan = static_cast<LoaderScan*>(data);
    const char* name = info->dlpi_name;
    if (name == nullptr) return 0;
    // The linker reports itself under one of these two names on every release that has
    // `__loader_*` at all. Restricting to them keeps this from walking four hundred
    // symbol tables to find a symbol only one object can have.
    if (std::strstr(name, "linker") == nullptr &&
        std::strstr(name, "ld-android") == nullptr) {
        return 0;
    }
    scan->found = elf::find_symbol(elf::read_symbols(info), "__loader_dlopen");
    return scan->found != nullptr ? 1 : 0;
}

void resolve_loader_dlopen() {
    if (o_loader_dlopen != nullptr) return;
    LoaderScan scan;
    dl_iterate_phdr(look_for_loader, &scan);
    o_loader_dlopen =
            reinterpret_cast<void* (*)(const char*, int, const void*)>(scan.found);
    ULOGI("io_redirect: linker dlopen %s",
          o_loader_dlopen != nullptr ? "located; dlopen is hooked with the caller preserved"
                                     : "not found; dlopen is left alone");
}

}  // namespace

/// Reads a file the way it is on the kernel's side of every hook this process has.
///
/// The `/proc` view is served through the same trampolines that now cover the platform's
/// own libraries, which means Java reads of `/proc/self/maps` are covered too — a gain,
/// and a problem for exactly one caller: the graft's own check that the view leaks
/// nothing. Reading through the view would make that check report success unconditionally,
/// which is the failure it exists to prevent. This is the second opinion it needs.
std::string read_unviewed(const char* path) {
    return path == nullptr ? std::string() : read_all(path);
}

/// Which libraries the interception covers, as path substrings.
///
/// `"*"` matches every loaded library. It is supported and **is not what UNIQUE uses**:
/// the fifteenth phone run tried it and the Mali driver aborted the render thread when
/// its gralloc mapper was hooked. `AppBootstrap.PLATFORM_IO_LIBRARIES` carries the list
/// that ships, with the run behind each entry.
///
/// Two things the scope is not:
///
/// - It is not the redirect. Every rule in `VirtualPathModel.redirectionRules` names the
///   guest's package or a shared-storage alias, so none can match a path under
///   `/data/user/0/com.unique`, and a hooked library that touches UNIQUE's own files is
///   unaffected. `round_trip_test.cpp` asserts that by name.
/// - It is not a safety argument on its own. That was the mistake the fifteenth run
///   corrected: a library can be broken by being hooked at all, whatever the table then
///   decides, and a driver is exactly such a library.
void set_scope(const char** paths, int count) {
    std::vector<std::string> filters;
    for (int i = 0; i < count; ++i) {
        if (paths[i] != nullptr) filters.emplace_back(paths[i]);
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_filters = std::move(filters);
}

void set_exclusions(const char** paths, int count) {
    std::vector<std::string> excludes;
    for (int i = 0; i < count; ++i) {
        if (paths[i] != nullptr) excludes.emplace_back(paths[i]);
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_excludes = std::move(excludes);
    ULOGI("io_redirect: %zu exclusion(s) set", g_excludes.size());
}

int exclusion_count() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return static_cast<int>(g_excludes.size());
}

int slots_patched() { return g_slots_patched; }

int sqlite_calls_replaced() { return sqlite_vfs::replaced(); }

/// Installs the interception into the libraries named by set_scope().
///
/// Idempotent, and meant to be repeated: a library the guest loads later has its own GOT
/// and is not covered by an earlier pass. Callers re-run this after System.loadLibrary.
/// The scan itself. Split out so the loader trampolines can re-run it without recursing
/// through the public entry point's scope checks.
InstallStatus install_locked() {
    // Static because the scan is repeated and each pass only walks libraries it has not
    // seen. `matched` and the saved originals have to survive from one pass to the next,
    // or a re-scan would report every symbol as one nothing in the process imports.
    static plt::HookRequest requests[] = {
        {"open",     reinterpret_cast<void*>(h_open),     reinterpret_cast<void**>(&o_open)},
        {"openat",   reinterpret_cast<void*>(h_openat),   reinterpret_cast<void**>(&o_openat)},
        {"stat",     reinterpret_cast<void*>(h_stat),     reinterpret_cast<void**>(&o_stat)},
        {"lstat",    reinterpret_cast<void*>(h_lstat),    reinterpret_cast<void**>(&o_lstat)},
        {"access",   reinterpret_cast<void*>(h_access),   reinterpret_cast<void**>(&o_access)},
        {"mkdir",    reinterpret_cast<void*>(h_mkdir),    reinterpret_cast<void**>(&o_mkdir)},
        {"rmdir",    reinterpret_cast<void*>(h_rmdir),    reinterpret_cast<void**>(&o_rmdir)},
        {"unlink",   reinterpret_cast<void*>(h_unlink),   reinterpret_cast<void**>(&o_unlink)},
        {"rename",   reinterpret_cast<void*>(h_rename),   reinterpret_cast<void**>(&o_rename)},
        {"chmod",    reinterpret_cast<void*>(h_chmod),    reinterpret_cast<void**>(&o_chmod)},
        {"opendir",  reinterpret_cast<void*>(h_opendir),  reinterpret_cast<void**>(&o_opendir)},
        {"fopen",    reinterpret_cast<void*>(h_fopen),    reinterpret_cast<void**>(&o_fopen)},
        {"readlink", reinterpret_cast<void*>(h_readlink), reinterpret_cast<void**>(&o_readlink)},
        {"statfs",   reinterpret_cast<void*>(h_statfs),   reinterpret_cast<void**>(&o_statfs)},

        // The directory-relative spellings, which is what `libjavacore.so` calls. Both
        // `fstatat` names are requested because bionic exports the LFS alias and which
        // one a library imports depends on how it was built.
        {"fstatat",     reinterpret_cast<void*>(h_fstatat),     reinterpret_cast<void**>(&o_fstatat)},
        {"fstatat64",   reinterpret_cast<void*>(h_fstatat),     reinterpret_cast<void**>(&o_fstatat)},
        {"faccessat",   reinterpret_cast<void*>(h_faccessat),   reinterpret_cast<void**>(&o_faccessat)},
        {"mkdirat",     reinterpret_cast<void*>(h_mkdirat),     reinterpret_cast<void**>(&o_mkdirat)},
        {"unlinkat",    reinterpret_cast<void*>(h_unlinkat),    reinterpret_cast<void**>(&o_unlinkat)},
        {"renameat",    reinterpret_cast<void*>(h_renameat),    reinterpret_cast<void**>(&o_renameat)},
        {"readlinkat",  reinterpret_cast<void*>(h_readlinkat),  reinterpret_cast<void**>(&o_readlinkat)},
        {"fchmodat",    reinterpret_cast<void*>(h_fchmodat),    reinterpret_cast<void**>(&o_fchmodat)},
        {"realpath",    reinterpret_cast<void*>(h_realpath),    reinterpret_cast<void**>(&o_realpath)},
        {"statvfs",     reinterpret_cast<void*>(h_statvfs),     reinterpret_cast<void**>(&o_statvfs)},
        {"statvfs64",   reinterpret_cast<void*>(h_statvfs),     reinterpret_cast<void**>(&o_statvfs)},

        // Bionic's fortified spellings, emitted when a library is built with
        // `_FORTIFY_SOURCE` — which the NDK turns on at every optimisation level, so most
        // third-party native code is. These are *different symbols*, not aliases.
        //
        // `__openat` used to stand here and does not exist: bionic exports `__open_2` and
        // `__openat_2`, and nothing in any process has ever imported the name that was
        // asked for. It was invisible because "nothing imports this" and "this name is
        // not a symbol" print the same line. `tools/native-test/check_libc_symbols.py`
        // now checks every name in this table against the NDK's own `libc.so`.
        {"__open_2",    reinterpret_cast<void*>(h_open_2),       reinterpret_cast<void**>(&o_open_2)},
        {"__openat_2",  reinterpret_cast<void*>(h_openat_2),     reinterpret_cast<void**>(&o_openat_2)},
        {"__readlink_chk",   reinterpret_cast<void*>(h_readlink_chk),   reinterpret_cast<void**>(&o_readlink_chk)},
        {"__readlinkat_chk", reinterpret_cast<void*>(h_readlinkat_chk), reinterpret_cast<void**>(&o_readlinkat_chk)},

        // The large-file spellings, which are a different *symbol* and were the whole of
        // the sixteenth run's failure.
        //
        // `java.io.File.isFile()` does not reach `libjavacore.so` at all. It reaches
        // `UnixFileSystem.getBooleanAttributes0`, which is native code in
        // `libopenjdk.so` — Android's OpenJDK port — and that file is written against
        // the LFS API:
        //
        //     static jboolean statMode(const char *path, int *mode) {
        //         struct stat64 sb;
        //         if (stat64(path, &sb) == 0) { ... }
        //
        // `stat64` and `stat` are the same function on a 64-bit device and *different
        // symbols* in a relocation table, so a hook that asks for one never sees the
        // other. Every write went through `libjavacore.so` and was redirected; the first
        // thing to ask `stat` about a published path through `libopenjdk.so` was the
        // code gate, and it was told the file does not exist.
        //
        // On LP64 `struct stat` and `struct stat64` are the same layout, so the same
        // trampolines serve both spellings.
        {"stat64",      reinterpret_cast<void*>(h_stat),         reinterpret_cast<void**>(&o_stat)},
        {"lstat64",     reinterpret_cast<void*>(h_lstat),        reinterpret_cast<void**>(&o_lstat)},
        {"open64",      reinterpret_cast<void*>(h_open),         reinterpret_cast<void**>(&o_open)},
        {"openat64",    reinterpret_cast<void*>(h_openat),       reinterpret_cast<void**>(&o_openat)},
        {"fopen64",     reinterpret_cast<void*>(h_fopen),        reinterpret_cast<void**>(&o_fopen)},
        {"statfs64",    reinterpret_cast<void*>(h_statfs),       reinterpret_cast<void**>(&o_statfs)},

        // And two more the same file uses: `remove` for delete and `creat` for a
        // truncating create. Neither has an `at` spelling in that code.
        {"remove",      reinterpret_cast<void*>(h_remove),       reinterpret_cast<void**>(&o_remove)},
        {"freopen",     reinterpret_cast<void*>(h_freopen),      reinterpret_cast<void**>(&o_freopen)},
        {"freopen64",   reinterpret_cast<void*>(h_freopen),      reinterpret_cast<void**>(&o_freopen)},
        {"statx",       reinterpret_cast<void*>(h_statx),        reinterpret_cast<void**>(&o_statx)},
        {"syscall",     reinterpret_cast<void*>(h_syscall),      reinterpret_cast<void**>(&o_syscall)},
        {"creat",       reinterpret_cast<void*>(h_creat),        reinterpret_cast<void**>(&o_creat)},
        {"creat64",     reinterpret_cast<void*>(h_creat),        reinterpret_cast<void**>(&o_creat)},
    };

    std::vector<std::string> filters;
    std::vector<std::string> excludes;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        filters = g_filters;
        excludes = g_excludes;
    }
    if (filters.empty()) {
        // Refused rather than applied everywhere. The scope is process-wide by design
        // now, but it is asked for with an explicit `"*"`: a scope that was never
        // published and one that was widened on purpose must not behave the same way.
        ULOGW("io_redirect::install() refused: no scope set");
        g_installed = false;
        return InstallStatus::kFailed;
    }

    // One memo per request set: the load watch below hooks a different symbol and must
    // not have its scan mark libraries as done for this one.
    static std::vector<std::string> seen;

    // Where an absolute data relocation is patched as well as a call.
    //
    // One entry, with one reason. SQLite wraps `open` in a local function, which reaches
    // the PLT, and stores `stat`, `lstat`, `access`, `unlink`, `mkdir`, `rmdir` and
    // `readlink` *by address* in a static table the linker fills in at load:
    //
    //     { "open",  (sqlite3_syscall_ptr)posixOpen, 0 },   /* a call     */
    //     { "stat",  (sqlite3_syscall_ptr)stat,      0 },   /* a pointer  */
    //
    // so a hook that patches only calls redirects half of one library's file operations.
    // The fourteenth run is that half: SQLite opened the probe database in the instance,
    // looked for it at the path it was given, and reported `file renamed while open`.
    //
    // The first attempt at this patched absolute slots everywhere, and the fifteenth run
    // is what that costs: `mali_config_interface_mapper: Failed to acquire IMapper
    // service. Aborting.` and a SIGABRT on the render thread. A PLT slot is by
    // construction a call into an imported function. An absolute slot is a pointer in a
    // library's own data, and a symbol-name match is a much weaker warrant for
    // overwriting one. So this list stays one library long until another one earns a
    // place on it.
    static const std::vector<std::string> abs64{"libsqlite.so"};

    auto report = plt::hook_all(filters, excludes, abs64, requests,
                                sizeof(requests) / sizeof(requests[0]), seen);
    g_slots_patched += report.slots_patched;
    for (const auto& failure : report.failures) {
        ULOGE("io_redirect: %s", failure.c_str());
    }
    for (const auto& name : report.excluded) {
        ULOGI("io_redirect: excluded (not hooked, on purpose): %s", name.c_str());
    }
    ULOGI("io_redirect installed: %d slot(s) in %d/%d libraries (%d excluded, %d seen "
          "before, %d total), %d rule(s), page size %ld",
          report.slots_patched, report.libraries_matched, report.libraries_scanned,
          report.libraries_excluded, report.libraries_already_scanned, g_slots_patched,
          rule_count(), sysconf(_SC_PAGESIZE));
    // Which library, not just how many. A redirect that reached `libjavacore.so` and not
    // `libsqlite.so` is a different engine from one that reached both, and the two used
    // to print the same line.
    for (const auto& entry : report.per_library) {
        ULOGI("io_redirect: hooked %s", entry.c_str());
    }
    // The names behind the numbers, for the guest's own libraries. See plt_hook.h.
    for (const auto& entry : report.per_library_symbols) {
        ULOGI("io_redirect: symbols %s", entry.c_str());
    }
    // And which of the names in the table nothing in this process spells that way. This
    // is the line that would have named the fourteenth run's bug on sight: `stat` was
    // requested, was imported by libraries that were in scope, and was patched nowhere,
    // because SQLite holds its address in a data relocation rather than calling it
    // through the PLT.
    {
        std::string missing;
        for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); ++i) {
            if (!requests[i].matched) {
                if (!missing.empty()) missing += ",";
                missing += requests[i].symbol;
            }
        }
        if (!missing.empty()) {
            ULOGI("io_redirect: nothing in this process imports: %s", missing.c_str());
        }
    }
    // SQLite, through its own interface rather than through its relocations.
    //
    // Repeated until it takes and then left alone. Reported either way and in one line,
    // because "SQLite was not redirected" and "SQLite has not been loaded yet" produce
    // the same behaviour and only one of them is a fault. See sqlite_vfs.h.
    if (!g_sqlite_replaced) {
        static const sqlite_vfs::Override kSqliteCalls[] = {
                {"open", reinterpret_cast<void*>(h_open_fixed)},
                {"access", reinterpret_cast<void*>(h_access)},
                {"stat", reinterpret_cast<void*>(h_stat)},
                {"lstat", reinterpret_cast<void*>(h_lstat)},
                {"unlink", reinterpret_cast<void*>(h_unlink)},
                {"mkdir", reinterpret_cast<void*>(h_mkdir)},
                {"rmdir", reinterpret_cast<void*>(h_rmdir)},
                {"readlink", reinterpret_cast<void*>(h_readlink)},
        };
        const auto sqlite = sqlite_vfs::install(
                kSqliteCalls, sizeof(kSqliteCalls) / sizeof(kSqliteCalls[0]));
        g_sqlite_replaced = sqlite.replaced > 0;
        ULOGI("io_redirect: sqlite %d/%d system call(s) replaced (library=%s api=%s "
              "vfs=v%d) %s",
              sqlite.replaced, sqlite.attempted, sqlite.library_found ? "yes" : "no",
              sqlite.api_found ? "yes" : "no", sqlite.vfs_version, sqlite.detail.c_str());
    }

    if (report.libraries_matched == 0) {
        for (const auto& filter : filters) {
            ULOGW("io_redirect: filter did not match: %s", filter.c_str());
        }
        for (const auto& name : report.sample) {
            ULOGW("io_redirect: app-private library seen: %s", name.c_str());
        }
    }

    // Zero patched slots is not a failure on its own - a guest with no native code has
    // nothing to hook, and one that loads its libraries later has nothing to hook *yet* -
    // but it must not be reported as a working interception either. kNothingToHook says
    // both, where the not-implemented status used to say neither.
    //
    // The *cumulative* count decides, not this pass's. Every pass after the first walks
    // only libraries it has not seen, so a healthy re-scan patches nothing and used to
    // report the interception as absent - which is the opposite of what it means.
    g_installed = g_slots_patched > 0;
    return g_slots_patched > 0 ? InstallStatus::kOk : InstallStatus::kNothingToHook;
}

InstallStatus install() { return install_locked(); }

/**
 * Notices libraries loaded *after* the initial scan.
 *
 * The initial hook walks what is loaded at that moment, so a library the guest loads
 * later - `System.loadLibrary` from an Activity, or one native library `dlopen`ing
 * another - has its own untouched GOT and none of its file operations are redirected.
 * That was recorded as broken rather than papered over; this closes it.
 *
 * `System.loadLibrary` reaches the linker through `libnativeloader`, so the notification
 * point is that library's call to `android_dlopen_ext` - one symbol, in one system
 * library, and the trampoline only calls the original and then re-scans. It rewrites
 * nothing and redirects nothing, which is what makes hooking outside the guest's own code
 * acceptable here when redirecting IO there would not be.
 *
 * The guest's own libraries are hooked too, so a native plugin loader is covered as well.
 */
InstallStatus watch_library_loads() { return watch_locked(); }

InstallStatus watch_locked() {
    // Both loader entry points, and `dlopen` only when its caller can be preserved.
    //
    // Two runs, two failures, and they are opposite ends of the same fact. `dlopen` in
    // `libdl.so` is
    //
    //     void* dlopen(const char* name, int flags) {
    //       return __loader_dlopen(name, flags, __builtin_return_address(0));
    //     }
    //
    // so the *caller's address* decides which namespace a bare soname resolves in.
    // Forwarding the call from `libunique_native.so` moved that to UNIQUE's namespace and
    // Unity's `libmain.so` could no longer find `libunity.so` by name — run 20, a
    // `JNI FatalError` in the game's own `onCreate`. Dropping the hook instead was no
    // better: `libmain.so` loads `libunity.so` through plain `dlopen`, so nothing noticed
    // the load, `libunity.so` was never hooked, and run 21 went straight back to
    // `ApkAddCentralDirectory : Unable to open` and a game telling its player the device
    // was out of storage.
    //
    // So the hook stays and calls `__loader_dlopen` with its own caller's return address.
    // If that symbol cannot be found, `dlopen` is left alone — the run-20 failure is
    // fatal and the run-21 one is not, so the safe side is the one that loads.
    //
    // `android_dlopen_ext` never had the problem: `libnativeloader` passes an
    // `android_dlextinfo` naming the namespace outright, so the caller is not consulted.
    resolve_loader_dlopen();
    static plt::HookRequest with_dlopen[] = {
        {"android_dlopen_ext", reinterpret_cast<void*>(h_android_dlopen_ext),
         reinterpret_cast<void**>(&o_android_dlopen_ext)},
        {"dlopen", reinterpret_cast<void*>(h_dlopen), nullptr},
    };
    static plt::HookRequest without_dlopen[] = {
        {"android_dlopen_ext", reinterpret_cast<void*>(h_android_dlopen_ext),
         reinterpret_cast<void**>(&o_android_dlopen_ext)},
    };
    plt::HookRequest* requests = loader_dlopen_available() ? with_dlopen : without_dlopen;
    const size_t request_count = loader_dlopen_available() ? 2 : 1;

    // Narrow and explicit: the loader plumbing, plus the guest's own code.
    std::vector<std::string> scope;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        scope = g_filters;
    }
    scope.emplace_back("libnativeloader.so");
    scope.emplace_back("libart.so");

    // No exclusions here, deliberately: this hooks `dlopen` so that install_locked can
    // run again afterwards, and install_locked is where the exclusions apply. Excluding a
    // protector from the *watch* would mean a library it loads is never redirected at
    // all, which is a different and larger loss than not redirecting the protector.
    static std::vector<std::string> seen;
    static const std::vector<std::string> none;
    auto report = plt::hook_all(scope, none, none, requests, request_count, seen);
    // Sticky, and it has to be: this runs again after every library load, and a pass that
    // walks only libraries it has already seen patches nothing. Reading that as "the watch
    // is not installed" would be the same mistake `kNothingToHook` was added to stop.
    const bool armed_now = report.slots_patched > 0;
    g_watching = g_watching || armed_now;
    // One line per pass that changed something, so a hundred quiet re-arms cost nothing
    // and the first one — or a later one that reaches a library the first could not — is
    // still in the log.
    if (armed_now || !g_watching) {
        ULOGI("io_redirect: library-load watch %s (%d slot(s) in %d libraries)",
              g_watching ? "installed" : "found nothing to hook",
              report.slots_patched, report.libraries_matched);
        for (const auto& name : report.sample) {
            ULOGW("io_redirect: watch saw but did not match: %s", name.c_str());
        }
    }
    // Not kNothingToHook: the scope here is libnativeloader.so and libart.so, which are
    // loaded in every process there has ever been. Matching nothing means the hook did
    // not work, and the consequence is specific - a library the guest loads after
    // bootstrap is never redirected - so it is reported as a failure and not as an empty
    // scan.
    return g_watching ? InstallStatus::kOk : InstallStatus::kFailed;
}

bool watching() { return g_watching; }

}  // namespace unique::io_redirect
