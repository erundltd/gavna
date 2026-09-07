// Host-side checks for syscall_paths.h — which argument of a raw syscall is a path.
//
// This table decides what a hook on `syscall()` is allowed to touch, and both kinds of
// mistake in it are expensive. Naming an argument that is not a path rewrites an integer
// the kernel then reads as a pointer. Missing one leaves a guest's file operation
// unredirected — which is the fault this file exists for: Standoff 2's engine imports two
// libc names, neither of them opens anything, and it reaches the filesystem through
// `syscall()` alone.
//
// The numbers are the asm-generic ones arm64 uses. They are checked here rather than on a
// phone because they are a property of the kernel ABI, not of the device.

#include <cstdio>
#include <string>

#include "../../core/native/src/main/cpp/syscall_paths.h"

namespace {

using unique::syscall_paths::PathArguments;
using unique::syscall_paths::path_arguments;

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (condition) return;
    ++g_failures;
    std::printf("FAIL  %s\n", what.c_str());
}

void expect(long number, int first, int second, const std::string& name) {
    const PathArguments args = path_arguments(number);
    check(args.first == first && args.second == second,
          name + ": expected (" + std::to_string(first) + "," + std::to_string(second) +
                  "), got (" + std::to_string(args.first) + "," +
                  std::to_string(args.second) + ")");
}

}  // namespace

int main() {
    constexpr int kNone = PathArguments::kNone;

    // The one that matters most: the only way arm64 opens a file at all.
    expect(56, 1, kNone, "openat");

    // The rest of the `*at` family, all `(dirfd, path, …)`.
    expect(34, 1, kNone, "mkdirat");
    expect(35, 1, kNone, "unlinkat");
    expect(48, 1, kNone, "faccessat");
    expect(439, 1, kNone, "faccessat2");
    expect(53, 1, kNone, "fchmodat");
    expect(54, 1, kNone, "fchownat");
    expect(78, 1, kNone, "readlinkat");
    expect(79, 1, kNone, "newfstatat");
    expect(88, 1, kNone, "utimensat");
    expect(291, 1, kNone, "statx");

    // Two paths, and the second is the one a rename would corrupt if it were missed.
    expect(38, 1, 3, "renameat");
    expect(276, 1, 3, "renameat2");

    // The three that take a path first.
    expect(43, 0, kNone, "statfs");
    expect(45, 0, kNone, "truncate");
    expect(49, 0, kNone, "chdir");

    // And the ones that must be passed through untouched. `futex` is the important one:
    // it is most of what a game's threads issue, and doing any work for it — including
    // taking the redirect table's lock, whose own implementation uses `futex` — is how a
    // hook in this position deadlocks a process.
    for (long number : {98L /* futex */, 178L /* gettid */, 63L /* read */, 64L /* write */,
                        57L /* close */, 222L /* mmap */, 172L /* getpid */,
                        80L /* fstat */, 46L /* ftruncate */}) {
        const PathArguments args = path_arguments(number);
        check(!args.any(), "syscall " + std::to_string(number) + " carries no path");
    }

    // `fstat` and `ftruncate` take a *descriptor* where their `*at` siblings take a path.
    // Confusing one for the other is the mistake this pair is here to catch.
    check(!path_arguments(80).any(), "fstat takes a descriptor, not a path");
    check(path_arguments(79).any(), "newfstatat takes a path");

    check(unique::syscall_paths::answers_with_a_path(78), "readlinkat answers with a path");
    check(!unique::syscall_paths::answers_with_a_path(56), "openat answers with a descriptor");

    std::printf("%d checks, %d failures  (syscall_paths)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
