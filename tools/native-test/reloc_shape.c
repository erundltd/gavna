/* The relocation shape the fourteenth phone run turned on, built and inspected.
 *
 * SQLite does not call `stat` through the PLT. It stores the *address* in a static
 * table, once, and calls it through that pointer forever after:
 *
 *     static struct unix_syscall {
 *       const char *zName; void *pCurrent;
 *     } aSyscall[] = {
 *       { "open",  (void*)posixOpen },   // a local wrapper -> a PLT call
 *       { "stat",  (void*)stat      },   // the address     -> a data relocation
 *     };
 *
 * A GOT hook patches the first and never sees the second, so `open` was redirected and
 * `stat` was not, inside one library, and SQLite reported the database as having been
 * renamed out from under it.
 *
 * This file is that construct and nothing else. `run.sh` builds it as a shared object
 * and asserts the relocation the compiler actually emits: an absolute 64-bit data
 * relocation against `stat`, with a zero addend — which is exactly what
 * `plt_hook.cpp::is_address_slot` now accepts, and what it used to skip.
 */
#include <sys/stat.h>

static int local_open(const char* path, int flags) { (void)path; (void)flags; return -1; }

struct syscall_entry {
    const char* name;
    void* current;
};

struct syscall_entry table[] = {
    {"open", (void*)local_open},
    {"stat", (void*)stat},
    {"lstat", (void*)lstat},
};

void* entry(int i) { return table[i].current; }
