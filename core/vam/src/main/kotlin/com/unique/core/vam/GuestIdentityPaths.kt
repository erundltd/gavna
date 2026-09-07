package com.unique.core.vam

import android.content.Context
import android.content.pm.ApplicationInfo
import com.unique.core.common.diag.DiagChannel
import com.unique.core.common.path.VirtualPathModel
import com.unique.core.diagnostics.Diagnostics
import com.unique.core.hook.Reflect
import com.unique.core.nativebridge.UniqueNative
import java.io.File

/**
 * Gives a guest the paths an installed app would report for itself.
 *
 * ## Why, and how it stopped being a guess
 *
 * Standoff 2 was read out of its own binary (`docs/STANDOFF2.md`). It does not look at
 * `/proc`, it does not enumerate processes and it does not check for an emulator. It calls
 * four getters through JNI —
 *
 * ```
 * sourceDir           x4
 * getApplicationInfo  x3
 * getPackageCodePath  x2
 * nativeLibraryDir    x1
 * ```
 *
 * — puts the APK path into an `AppVerification` protobuf alongside the APK's hash and
 * certificate, and sends it **as a field of `GoogleAuthRequest`**. The server answers with
 * `AuthRestrictions/VirtualSpaceMessage`, and the game keeps a client-side flag called
 * `VirtualSpaceDetected` for the same conclusion.
 *
 * Inside UNIQUE all four answer with a path containing `com.unique`. No installed copy of
 * any app can produce one, so no comparison is even required to see it. This is the whole
 * detection, and closing it is closing the thing rather than a thing.
 *
 * ## Why it can be done at all, which is not obvious
 *
 * The paths cannot simply be changed: the class loader is built from `sourceDir`, the
 * resources from `publicSourceDir`, and the guest's files are really on disk under
 * UNIQUE's directory because there is nowhere else on an unrooted device to put them.
 *
 * Two things make it work anyway.
 *
 * 1. **Order.** The class loader and the `AssetManager` are built once, from the real
 *    paths, before this runs. What is changed afterwards is only what is *reported*:
 *    `LoadedApk.mAppDir`, which `getPackageCodePath()` returns and nothing else reads, and
 *    the `ApplicationInfo` object, which by then has already done its work. `mResDir`,
 *    `mSplitResDirs`, `mLibDir` and `mClassLoader` are deliberately left alone.
 * 2. **The redirect goes both ways, in every library.** `VirtualPathModel.redirectionRules`
 *    maps the public APK directory back to the real one and `/data/user/0/<guest>` back to
 *    the instance, and the interception covers the whole process — so a path handed out
 *    here is a path that opens, from Java, from the guest's native code, and from whatever
 *    system library the guest hands it to.
 *
 * The change is also *additive*, which is what makes it safe to do part-way through a
 * process's life: the real paths are matched by no rule and keep working, so anything
 * already holding one — an open `SharedPreferences`, a `File` captured earlier — is
 * unaffected. Both spellings name the same bytes.
 *
 * ## Neither half is applied until it is measured
 *
 * A published path that does not resolve does not fail visibly. It fails as a guest that
 * silently cannot read its own saved games, or — as the fourteenth phone run showed — as a
 * game telling its player the device is out of space, because a system library outside the
 * hook's reach could not `statfs` the APK path it had just been given. So:
 *
 * - The **code** half needs the redirect to be installed (slots patched) and the published
 *   `base.apk` to open. With no redirect there is no path, only a string.
 * - The **data** half needs a round trip: a byte written through the public path and found
 *   at the real one, twice — once through `java.io.File` and once by opening a SQLite
 *   database, because those reach the filesystem through different hooks.
 *
 * Either refusal leaves the real paths in place and says so in one line, with the reason.
 * `GUEST_PATHS_PUBLISHED … code=… data=… detail=…`.
 */
internal object GuestIdentityPaths {

    /** What was actually applied. Both halves are reported; neither is assumed. */
    data class Result(
        val code: Boolean,
        val data: Boolean,
        val publicApkDir: String,
        val publicDataDir: String,
        val detail: String,
    )

    /**
     * The paths a guest will be told it has, computed from the model and nothing else.
     *
     * Separated from [apply] because the interesting property is arithmetic on strings and
     * the rest is reflection into framework objects that a unit test cannot construct.
     * What has to hold is that none of these four names UNIQUE, and that is checkable here.
     */
    data class Plan(
        val publicApkDir: String,
        val publicDataDir: String,
        val baseApk: String,
        val nativeLibraryDir: String,
    )

    fun plan(
        model: VirtualPathModel,
        packageName: String,
        vuid: Int,
        versionCode: Long,
        abiDirName: String,
        realHostUserId: Int = 0,
    ): Plan {
        val token = VirtualPathModel.installTokenFor(vuid, packageName, versionCode)
        val apkDir = model.installedApkDir(packageName, token)
        return Plan(
            publicApkDir = apkDir,
            publicDataDir = "/data/user/$realHostUserId/$packageName",
            baseApk = "$apkDir/base.apk",
            // `lib/arm64`, not `lib/arm64-v8a`: the ABI directory inside an APK and the
            // one the installer extracts to are spelled differently, and an app that knows
            // the difference is exactly the kind that is looking.
            nativeLibraryDir = "$apkDir/lib/${VirtualPathModel.installedAbiDirName(abiDirName)}",
        )
    }

    /**
     * Splits, moved to [publicApkDir] by name.
     *
     * Rewritten entry for entry rather than dropped: an app that counts its own splits is
     * not unusual, and a guest with three splits reporting none is its own kind of tell.
     */
    fun rebaseSplits(splits: Array<String>?, publicApkDir: String): Array<String>? =
        splits?.map { "$publicApkDir/${it.substringAfterLast('/')}" }?.toTypedArray()

    fun apply(
        hostContext: Context,
        model: VirtualPathModel,
        params: VirtualLaunchParams,
        appInfo: ApplicationInfo,
        loadedApk: Any?,
        realHostUserId: Int = 0,
    ): Result {
        // The `LoadedApk` is built from this object and on every release seen so far keeps
        // the same instance — but "so far" is not a guarantee, and a release that copied it
        // would leave `Context.getApplicationInfo()` reporting the real paths while
        // `PackageManager.getApplicationInfo()` reported the public ones. Two answers to
        // one question is worse than either. Both are written when they differ.
        val infos = buildList {
            add(appInfo)
            loadedApk?.let { apk ->
                (Reflect.get(apk.javaClass, "mApplicationInfo", apk) as? ApplicationInfo)
                    ?.takeIf { it !== appInfo }?.let(::add)
            }
        }
        val abiDirName = appInfo.nativeLibraryDir?.substringAfterLast('/') ?: "arm64-v8a"
        val plan = plan(
            model = model,
            packageName = params.packageName,
            vuid = params.vuid,
            versionCode = params.versionCode,
            abiDirName = abiDirName,
            realHostUserId = realHostUserId,
        )
        val publicApkDir = plan.publicApkDir
        val publicDataDir = plan.publicDataDir

        // The code half is gated too, and the fourteenth phone run is why.
        //
        // It used to be unconditional, on the argument that a wrong code path could not
        // lose anything because the class loader and resources were already built. That
        // argument was about *UNIQUE*. It said nothing about the rest of the process:
        //
        //     E misc: isReadonlyFilesystem():
        //         statfs(/data/app/~~kx_uUO…/com.axlebolt.standoff2-kROpHz…/base.apk)
        //         failed: No such file or directory
        //
        // A published path is only a path if the redirect is in. With no slots patched
        // there is no redirect, and publishing would hand every library in the process a
        // path that resolves nowhere — which surfaced as a game telling its player the
        // device was out of space.
        val slots = runCatching { UniqueNative.redirectSlotsPatched() }.getOrDefault(0)
        // Three different faults produce the same "it does not open", and the sixteenth
        // run cost a round because the message could not tell them apart. Asked in order,
        // each answer rules out one:
        //
        //   1. no rule maps the path       — `VirtualPathModel` and the plan disagree
        //   2. the rule maps it nowhere    — the instance's APK is not where it should be
        //   3. the rule is right and Java still cannot see it
        //                                  — the hook is not in the library doing the
        //                                    asking, which is what actually happened
        val mapped = runCatching { UniqueNative.redirect(plan.baseApk) }.getOrNull()
        val codeRefusal = when {
            slots <= 0 ->
                "the redirect patched no slots, so a published path would resolve nowhere"
            mapped == null ->
                "no redirect rule maps the published APK path: ${plan.baseApk}"
            !File(mapped).isFile ->
                "the rule maps the published APK path to a file that is not there: $mapped"
            !File(plan.baseApk).isFile ->
                "the rule is right and Java cannot see it: the redirect is missing from " +
                    "the library that answers File.isFile — ${plan.baseApk} -> $mapped"
            else -> null
        }
        val code = if (codeRefusal != null) false else runCatching {
            infos.forEach { applyCodePaths(it, plan) }
            if (loadedApk != null) {
                // `LoadedApk.mAppDir` is what `Context.getPackageCodePath()` returns, and
                // it is the only thing that reads it. `mResDir` beside it is *not* touched:
                // the resources are re-created from it on a configuration change.
                Reflect.set(loadedApk.javaClass, "mAppDir", loadedApk, plan.baseApk)
            }
            true
        }.onFailure {
            Diagnostics.warn(
                DiagChannel.LAUNCH, "GUEST_PATHS_CODE_FAILED",
                mapOf("package" to params.packageName, "error" to it.toString()),
            )
        }.getOrDefault(false)

        val realDataDir = model.dataDir(params.vuid, params.packageName)
        val refusal = codeRefusal ?: probe(publicDataDir, realDataDir)
        val data = if (refusal == null) {
            runCatching {
                infos.forEach { applyDataPaths(it, publicDataDir) }
                applyDataDirs(hostContext, loadedApk, publicDataDir)
                true
            }.onFailure {
                Diagnostics.warn(
                    DiagChannel.LAUNCH, "GUEST_PATHS_DATA_FAILED",
                    mapOf("package" to params.packageName, "error" to it.toString()),
                )
            }.getOrDefault(false)
        } else {
            false
        }

        val result = Result(
            code = code,
            data = data,
            publicApkDir = publicApkDir,
            publicDataDir = publicDataDir,
            detail = refusal ?: "both paths round-tripped into the instance",
        )
        Diagnostics.event(
            DiagChannel.LAUNCH,
            if (code && data) com.unique.core.common.diag.DiagLevel.INFO
            else com.unique.core.common.diag.DiagLevel.WARN,
            "GUEST_PATHS_PUBLISHED",
            mapOf(
                "package" to params.packageName,
                "code" to code.toString(),
                // How much interception was actually in when this decided. A published
                // path with no redirect behind it is the failure mode this gates.
                "slots" to slots.toString(),
                // And how much of SQLite. The data half fails on its own when a database
                // opened through the public path does not land in the instance, and this
                // is the one number that says why: SQLite's syscall table is replaced
                // through its own VFS interface, not by patching relocations, and zero
                // means that did not happen. See `UniqueNative.sqliteCallsReplaced`.
                "sqlite" to runCatching { UniqueNative.sqliteCallsReplaced() }
                    .getOrDefault(0).toString(),
                "data" to data.toString(),
                "apk" to publicApkDir,
                "detail" to result.detail,
            ),
        )
        return result
    }

    // ---------------------------------------------------------------------------------

    /** The APK and library paths. Safe unconditionally; see the class comment. */
    private fun applyCodePaths(appInfo: ApplicationInfo, plan: Plan) {
        appInfo.sourceDir = plan.baseApk
        appInfo.publicSourceDir = plan.baseApk
        appInfo.splitSourceDirs = rebaseSplits(appInfo.splitSourceDirs, plan.publicApkDir)
        appInfo.splitPublicSourceDirs =
            rebaseSplits(appInfo.splitPublicSourceDirs, plan.publicApkDir)
        appInfo.nativeLibraryDir = plan.nativeLibraryDir
    }

    /**
     * The data directory, once [probe] has shown it resolves.
     *
     * All three spellings, because `ContextImpl.getDataDir()` picks between them by the
     * context's storage mode and a guest gets the credential-protected one.
     */
    private fun applyDataPaths(appInfo: ApplicationInfo, publicDataDir: String) {
        appInfo.dataDir = publicDataDir
        appInfo.deviceProtectedDataDir = publicDataDir
        runCatching {
            Reflect.set(
                ApplicationInfo::class.java, "credentialProtectedDataDir", appInfo, publicDataDir,
            )
        }
    }

    private fun applyDataDirs(hostContext: Context, loadedApk: Any?, publicDataDir: String) {
        if (loadedApk != null) {
            val dir = File(publicDataDir)
            Reflect.set(loadedApk.javaClass, "mDataDir", loadedApk, publicDataDir)
            Reflect.set(loadedApk.javaClass, "mDataDirFile", loadedApk, dir)
            Reflect.set(loadedApk.javaClass, "mDeviceProtectedDataDirFile", loadedApk, dir)
            Reflect.set(loadedApk.javaClass, "mCredentialProtectedDataDirFile", loadedApk, dir)
        }

        // A `ContextImpl` caches each directory the first time it is asked for, and UNIQUE
        // asks for some of them during the graft. Left alone, the guest would be handed
        // the real path by a cache and the public one by everything else — which is worse
        // than either, because the two disagree.
        clearContextCaches(hostContext)
    }

    private val CONTEXT_DIR_CACHES = listOf(
        "mFilesDir", "mCacheDir", "mCodeCacheDir", "mNoBackupFilesDir",
        "mDatabasesDir", "mPreferencesDir", "mSharedPrefsPaths",
    )

    private fun clearContextCaches(context: Context) {
        var target: Context = context
        repeat(4) {
            val impl = target
            if (impl.javaClass.name == "android.app.ContextImpl") {
                for (field in CONTEXT_DIR_CACHES) {
                    runCatching { Reflect.set(impl.javaClass, field, impl, null) }
                }
                return
            }
            val next = runCatching {
                (impl as? android.content.ContextWrapper)?.baseContext
            }.getOrNull() ?: return
            target = next
        }
    }

    /**
     * Writes a byte through the public path and looks for it at the real one.
     *
     * Returns null when it arrived, or the reason it did not. Everything is done through
     * `java.io.File`, deliberately: that is the path the guest's own code takes, so this
     * measures the thing being relied on rather than something adjacent to it.
     *
     * The probe file is removed by both spellings. If the redirect is working they are one
     * file and the second delete is a no-op; if it is not, the first delete removed
     * nothing and the second removes whatever was created.
     */
    private fun probe(publicDataDir: String, realDataDir: String): String? =
        probeFile(publicDataDir, realDataDir) ?: probeDatabase(publicDataDir, realDataDir)

    /** `java.io.File`, which is `libjavacore.so` — every stream and preference file. */
    private fun probeFile(publicDataDir: String, realDataDir: String): String? {
        val marker = "unique-probe-${System.nanoTime()}"
        val viaPublic = File("$publicDataDir/files/.unique-path-probe")
        val viaReal = File("$realDataDir/files/.unique-path-probe")
        return runCatching {
            File("$publicDataDir/files").mkdirs()
            viaPublic.writeText(marker)
            val seen = runCatching { viaReal.takeIf { it.isFile }?.readText() }.getOrNull()
            runCatching { viaPublic.delete() }
            runCatching { viaReal.delete() }
            when (seen) {
                marker -> null
                null -> "a file written through $publicDataDir did not reach the instance"
                else -> "the public path resolved somewhere unexpected"
            }
        }.getOrElse { "the public data path is not writable: $it" }
    }

    /**
     * SQLite, which is neither `libjavacore.so` nor the guest's own code.
     *
     * A database is opened by absolute path from `libandroid_runtime.so` into
     * `libsqlite.so`, so it is covered by a different hook from everything the file probe
     * exercises — and it is where a guest keeps the state whose loss it would notice
     * most. One library out of scope and the file probe would still pass while every
     * database the guest opened went somewhere UNIQUE cannot see.
     */
    private fun probeDatabase(publicDataDir: String, realDataDir: String): String? {
        val name = ".unique-path-probe.db"
        val viaReal = File("$realDataDir/databases/$name")
        return runCatching {
            File("$publicDataDir/databases").mkdirs()
            val db = android.database.sqlite.SQLiteDatabase.openOrCreateDatabase(
                "$publicDataDir/databases/$name", null,
            )
            db.execSQL("CREATE TABLE IF NOT EXISTS probe (n INTEGER)")
            db.close()
            val arrived = viaReal.isFile
            for (suffix in listOf("", "-journal", "-wal", "-shm")) {
                runCatching { File("$publicDataDir/databases/$name$suffix").delete() }
                runCatching { File("$realDataDir/databases/$name$suffix").delete() }
            }
            if (arrived) null
            else "a database opened through $publicDataDir did not reach the instance"
        }.getOrElse { "a database cannot be opened through the public path: $it" }
    }
}
