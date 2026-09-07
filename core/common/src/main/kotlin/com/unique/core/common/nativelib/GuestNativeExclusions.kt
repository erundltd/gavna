package com.unique.core.common.nativelib

/**
 * Which of a guest's native libraries the path redirector must leave alone.
 *
 * ## The failure this exists for
 *
 * UNIQUE redirects a guest's file operations by writing one pointer into each of its
 * libraries' GOT slots. That is deliberately the *gentle* kind of hook — nothing
 * executable is modified, and the failure mode is meant to be "this library was not
 * hooked" rather than "this library is now broken" (`plt_hook.h`).
 *
 * There is one class of library for which that is not true, and a Redmi running Android
 * 15 produced it. A Unity game loaded `libgrave.so`, a code-virtualization protector:
 *
 * ```
 * io_redirect: hooked 22 new slot(s) after loading
 *   /data/user/0/com.unique/files/virtual/apk/…/lib/arm64-v8a/libgrave.so (22 total)
 * …
 * E CRASH: signal 7 (SIGBUS), code 1 (BUS_ADRALN), fault addr 0x7dd33219f7
 * E CRASH:   #00 pc 00000000000009f7  <anonymous:0000007dd3321000>
 * E CRASH: Forwarding signal 11
 * F libc  : Fatal signal 11 (SIGSEGV) … in tid 12385 (dey.standarling)
 * ```
 *
 * ## What the next run said about that, and why the entry stays anyway
 *
 * The pairing looked conclusive and it was not. With `libgrave.so` excluded, the same
 * game died the same way — the same signal, at the same offset into the page:
 *
 * ```
 * run 6  #00 pc …9f7  <anonymous:0000007dd3321000>
 * run 7  #00 pc …9f7  /memfd:gralloc_shared_memory (deleted)     (libgrave.so excluded)
 * ```
 *
 * So the hook was not the cause, and the honest reading of both is narrower: a pointer
 * that was already wrong, followed into whatever page the allocator had got to. That
 * correction is recorded rather than quietly dropped, because "we fixed it" and "it kept
 * happening" have to be distinguishable later.
 *
 * The entry stays for a different and smaller reason: a library that executes generated
 * code and verifies its own relocations is one whose GOT UNIQUE has no business writing
 * to, and the cost of leaving it alone is bounded and known. It is hardening, not a
 * fix — and this comment says so rather than letting the list read as a list of solved
 * crashes.
 *
 * ## What an exclusion costs, stated plainly
 *
 * An excluded library's hard-coded `/data/data/<pkg>/…` paths are **not** rewritten. If
 * such a library writes to one, it writes outside the instance and the write fails —
 * scoped storage will not let UNIQUE create another package's data directory. That is a
 * real loss and it is bounded to one library; the alternative for the libraries below is
 * that the app does not run at all.
 *
 * The native layer names every exclusion that actually applied, so a library that is not
 * hooked *on purpose* is never confused with one the scan failed to find.
 *
 * ## Why a name list and not a heuristic
 *
 * Two heuristics were considered and rejected. "Skip libraries with RELRO" excludes
 * almost everything, since the linker applies RELRO to nearly every modern `.so`.
 * "Scan the library for a hard-coded `/data/data` string" means reading tens of
 * megabytes of a game's engine at launch and still says nothing about whether the
 * library inspects its own GOT. A short list of protectors, each with the run that put
 * it there, is smaller, cheaper, and honest about being incomplete.
 */
object GuestNativeExclusions {

    /**
     * Library name fragments never hooked, whatever the guest is.
     *
     * Matched as substrings of the full path, so `libgrave` covers `libgrave.so` and any
     * versioned spelling of it. Each entry names what it is and how it got here; an
     * entry with no evidence behind it does not belong in this list.
     */
    val BUILT_IN: Set<String> = setOf(
        // Code-virtualization protector shipped inside Unity games. Excluded as
        // hardening, not as a fix: the crash that first drew attention to it happened
        // again with the exclusion in place. See the class comment.
        "libgrave.so",

        // UNIQUE's own. The scope is the whole process now, so this is no longer
        // hypothetical: without the entry, the redirector's own libc calls would go
        // through the redirector.
        "libunique_native.so",

        // The dynamic linker and the three libraries it is built out of.
        //
        // These are excluded for a different reason from the protector above, and it is
        // not caution. A GOT slot in `libc.so` is one libc reads to call *itself*, and a
        // trampoline that runs inside libc's own implementation of `fopen` on its way to
        // `open` would redirect a path that was already redirected on the way in — twice
        // is not idempotent for a prefix rewrite. The linker is worse: it resolves the
        // symbols the trampoline itself needs.
        //
        // Nothing is lost by it. A guest never calls into libc's internals with a path of
        // its own; it calls libc's exported entry points, and those are hooked in the
        // caller's library, which is where the path actually comes from.
        "/linker64",
        "/libc.so",
        "/libdl.so",
        "/libm.so",

        // Vendor and driver code, which is never in scope and is excluded anyway so that
        // widening the scope by accident cannot reach it.
        //
        // The fifteenth phone run is the entry's evidence. With the whole process hooked,
        // the Mali driver could not find its gralloc mapper and did not degrade:
        //
        //     io_redirect: hooked 9 new slot(s) after loading mapper.mediatek.so
        //     E mali_config_interface_mapper: Failed to acquire IMapper service. Aborting.
        //     E CRASH: signal 6 (SIGABRT) … name: RenderThread
        //
        // A driver loaded into the render thread aborts where ordinary code returns an
        // error, so the cost of being wrong about it is the whole app rather than one
        // file operation. Nothing UNIQUE wants from a redirect lives behind one.
        "/vendor/",
        "/odm/",
        "/system/vendor/",
    )

    /**
     * Everything to exclude for one guest: the built-in list plus this instance's own.
     *
     * @param perPackage names from the compatibility profile or an instance override.
     *   Blank entries are dropped rather than becoming a substring that matches every
     *   path — an empty exclusion would silently switch redirection off for the whole
     *   process, which is the one mistake this list must not be able to make.
     */
    fun forGuest(perPackage: Collection<String> = emptyList()): List<String> =
        (BUILT_IN + perPackage.map { it.trim() }.filter { it.isNotEmpty() })
            .distinct()
            .sorted()
}
