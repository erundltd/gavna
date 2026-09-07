#pragma once
// Which argument of a raw `syscall()` is a path, for the numbers where one is.
//
// ## Why this exists
//
// The twenty-third phone run answered a question four rounds had been guessing at. The
// scan reported, for the first time, the names Standoff 2's engine actually imports:
//
//     io_redirect: symbols libunity.so patched=realpath,readlink
//                  unhooked=pthread_setspecific,…,syscall,…
//
// Two names out of forty-one, and neither of them opens anything. There is no `open`, no
// `openat`, no `stat`, no `fopen` in `libunity.so` at all — **Unity issues its file
// operations as raw `syscall()` calls**. That is why a GOT hook on every libc path
// function redirected nothing for it, and why the game was handed an installed-shaped APK
// path it could not open:
//
//     E Unity: ApkAddCentralDirectory : Unable to open '/data/app/~~…/base.apk'
//
// So `syscall` is hooked too, and the path argument is rewritten before the number is
// passed on. Everything else goes through untouched, which is most of them: `futex`,
// `gettid` and the rest are the overwhelming majority of what a game's engine issues.
//
// ## Why the table is small and why that is deliberate
//
// arm64 uses the generic syscall ABI, where the path-taking calls are the `*at` family —
// there is no `SYS_open`, `SYS_stat` or `SYS_access` at all. So the list is short, and
// every entry is a number whose path position is fixed by the kernel ABI and cannot
// change. Numbers absent from it cost one comparison and nothing else, which is the
// property that matters when the hook sits in front of every `futex` a thread takes.
//
// `renameat` and `renameat2` carry two paths; nothing else here carries more than one.
//
// The values are the asm-generic numbers (`arch/arm64/include/asm/unistd.h`), stated here
// rather than taken from a header so the table can be read and checked without one.

#include <cstddef>

namespace unique::syscall_paths {

/// Where a syscall's path arguments sit, counting from the first argument *after* the
/// number. `kNone` means the call carries none and must be passed through untouched.
struct PathArguments {
    static constexpr int kNone = -1;
    int first = kNone;
    int second = kNone;

    bool any() const { return first != kNone; }
};

/// The generic (arm64) syscall numbers that take a path.
enum Number : long {
    kMkdirat = 34,
    kUnlinkat = 35,
    kRenameat = 38,
    kStatfs = 43,
    kTruncate = 45,
    kFaccessat = 48,
    kChdir = 49,
    kFchmodat = 53,
    kFchownat = 54,
    kOpenat = 56,
    kReadlinkat = 78,
    kNewfstatat = 79,
    kUtimensat = 88,
    kRenameat2 = 276,
    kStatx = 291,
    kFaccessat2 = 439,
};

inline PathArguments path_arguments(long number) {
    switch (number) {
        // `(dirfd, path, …)` — the whole `*at` family, and the reason arm64 has no
        // path-taking syscall with the path first except the three below.
        case kMkdirat:
        case kUnlinkat:
        case kFaccessat:
        case kFaccessat2:
        case kFchmodat:
        case kFchownat:
        case kOpenat:
        case kReadlinkat:
        case kNewfstatat:
        case kUtimensat:
        case kStatx:
            return PathArguments{1, PathArguments::kNone};
        // `(olddirfd, oldpath, newdirfd, newpath[, flags])`.
        case kRenameat:
        case kRenameat2:
            return PathArguments{1, 3};
        // The three that take a path first and no directory descriptor.
        case kStatfs:
        case kTruncate:
        case kChdir:
            return PathArguments{0, PathArguments::kNone};
        default:
            return PathArguments{};
    }
}

/// True for the one number whose *answer* is a path and has to be shown the guest's view.
inline bool answers_with_a_path(long number) { return number == kReadlinkat; }

}  // namespace unique::syscall_paths
