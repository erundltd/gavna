# Handoff: everything a new agent needs to continue UNIQUE

Written for whoever picks this project up next, human or otherwise. It assumes nothing
about what you have read, and it is the only file you must read before touching code.
Everything in it is either checked-in fact or a measurement from a real phone, with the run
that produced it named. Where something is a guess, it says so.

Read in this order:

1. this file, all of it;
2. `docs/ARCHITECTURE.md` §7 (storage and paths) and §9 (Google);
3. `docs/STANDOFF2.md` — the target application, read out of its own binary;
4. `docs/STATUS.md` from the bottom of "phone runs" upward — each run's section is what
   that run *proved*, and several correct earlier claims.

---

## 1. What this project is

**UNIQUE** is an Android app-level virtualization engine. It runs an APK the device has
**not installed**, inside UNIQUE's own process, in a way that makes the app believe it is
itself: its own package name, its own paths, its own data, its own certificate. No root, no
unlocked bootloader, no Xposed, one APK.

- Repository: `erundltd/gavna`
- Working branch: `claude/unique-app-virtualization-bn35b2` (60 commits at the time of
  writing; **never push anywhere else**)
- Host package: `com.unique`, `minSdk 31`, `compileSdk`/`targetSdk 36`, **arm64-v8a only**
- UI: Flutter 3.35.5 in `ui/`, engine: Kotlin + C++ in `core/*`, host app in `app/`

**The goal, in the owner's words**: make apps that refuse to run in a virtual space run.
The specific target is **Standoff 2** (`com.axlebolt.standoff2` 0.39.3, versionCode
203908) — it must launch, run stably, and not show the "running in a virtual space" notice.

### Rules this project runs on

From `ARCHITECTURE.md` §18, and they are not decoration — several sections of `STATUS.md`
exist because a claim was made without evidence and a later phone log contradicted it.

- Nothing is described as working because it ought to. Every claim names the run, the log
  line or the test that supports it.
- `NOT_TESTED` is a legitimate and common answer. `SUPPORTED` requires evidence.
- A correction is written down beside the thing it corrects, not silently applied. If you
  find a claim in these documents that a log disproves, **retract it in the document** and
  say which log.
- Diagnostics are how this project sees. Almost every fix below came from a line somebody
  added *because* the previous round could not tell two failures apart.

---

## 2. Where the project stands, honestly

### Works, measured on a real phone

- An APK not installed on the device is imported, given an instance, and launched into a
  `:vappN` process where it runs as itself. Standoff 2 launches, renders on the real GPU at
  2400×1080, reads its 1.7 GB expansion file out of the instance's own storage, and shows
  its own menu.
- Play services works for everything except sign-in: Maps, Firebase, ads, Dynamite, FCM.
- `/proc/self/maps` inside a guest no longer names UNIQUE — `PROC_VIEW_INSTALLED …
  named=16 leaked=0` on the phone, and the graft checks its own work.
- Two instances of one app have separate identities, storage and `ANDROID_ID`.
- 302 JVM tests, 142 host-side native checks, 125 device-log tests, 17 APK-survey tests, 15
  Dart tests. All passing.

### Does not work

| | Where it stands |
|---|---|
| **The "virtual space" notice in Standoff 2** | Both halves are live on hardware as of run 18 (`code=true data=true sqlite=8`) — and publishing them broke the game, which showed *"Not enough storage space to install required resources"* because its own engine could not open the APK path it had been handed. Two causes found and fixed (§6, "the two the eighteenth run found"); **untested**. The notice itself has still never been observed with the paths intact. See §4. |
| **Google sign-in** | Fails as "Попытка входа отменена". Run 17 changed what is known about *why*: the refusal is timed at under half a second with no account picker drawn, which is not the OAuth-client wall this project had been describing. A fix is in this build and **has not been run on a phone**. §5 and `docs/GOOGLE_SIGN_IN.md` §0. |
| **Sign-in through a browser** (Facebook, VK, most OAuth) | The identity half already works; the *return* leg does not. This is the highest-value unbuilt piece. §5. |
| **Attestation** (Play Integrity, vendor device checks) | Out of reach and stated as such. UNIQUE is not an attestation bypass. |
| A packed app (`bin.mt.plus`) | Its protector declines to register natives. Unsolved. |

---

## 3. Building, and the signing key

### Environment

```bash
export ANDROID_HOME=/opt/android-sdk        # cmdline-tools, platform 36, build-tools 36
export PATH=/opt/flutter/bin:$PATH          # Flutter 3.35.5 — a newer Dart breaks ui/pubspec.lock
```

The NDK is installed by Gradle on first native build. AGP 8.13.0, Kotlin 2.2.20.

### Commands

```bash
./gradlew test                    # 302 JVM tests
./tools/native-test/run.sh        # 142 native checks; 38 need an NDK and skip without one
(cd ui && flutter test)           # 15 Dart tests
./tools/device-log/self_test.py   # 125 tests for the log analyzer, no toolchain
./tools/apk-survey/self_test.py   # 17 tests
./tools/check-translations.py     # every engine failure has both languages
./tools/report-unimplemented.sh   # every deliberately unimplemented surface

./gradlew :app:assembleVerify :app:assembleRelease
./tools/check-abi.sh dist/unique-arm64-v8a.apk       # ARM64-only + 16 KB alignment
```

`--tests` is not supported on the Android unit-test task; use
`:core:vam:testDebugUnitTest` for one module.

### Publishing a build to the tester

```bash
cp app/build/outputs/apk/verify/app-verify.apk    dist/unique-arm64-v8a.apk
cp app/build/outputs/apk/release/app-release.apk  dist/unique-arm64-v8a-minified.apk
(cd dist && sha256sum unique-arm64-v8a.apk unique-arm64-v8a-minified.apk > SHA256SUMS)
```

Then update `dist/README.md` — the tester reads that file, in Russian where it explains what
changed, and it is the only place the *user-facing* account of a build lives. Its "What
changed since the last phone run" section is shifted down one slot each time (`one run ago`,
`two runs ago`, …).

### The signing key

**It is already in the repository** and it is meant to be. `app/debug.keystore`:

| | |
|---|---|
| path | `app/debug.keystore` (committed) |
| store password | `android` |
| key alias | `androiddebugkey` |
| key password | `android` |
| subject | `CN=UNIQUE Test Key, OU=UNIQUE, O=UNIQUE` |
| valid | 2026-09-06 → 2056-08-29 |
| SHA-1 | `64:DB:A6:AF:88:DA:18:39:E1:A7:17:6B:2E:16:72:AC:B9:14:47:6B` |
| SHA-256 | `60:86:84:0C:82:B5:90:EB:8B:0E:1B:35:57:88:44:F9:73:F0:56:FE:08:FC:25:77:E1:10:E7:39:CB:3B:A6:D3` |
| Facebook-style key hash | `ZNumr4jaGDnhpxdrLhZyrLkUR2s=` |

Wired up as the `testSigned` signing config in `app/build.gradle.kts`, used by both the
`verify` and `release` builds. **Every build made from this repository installs over the tester's existing one and
keeps their instances and imported apps.** That is the entire reason the key is committed:
before it existed each build machine generated its own key, two builds had two keys, and
Android's answer is the unhelpful `App not installed` — whose only cure is an uninstall that
destroys the tester's instances and their expansion files.

Verify what you are about to ship matches what they have:

```bash
keytool -list -v -keystore app/debug.keystore -storepass android
apksigner verify --print-certs dist/unique-arm64-v8a.apk
```

To sign with a different key, pass the Gradle properties `uniqueKeystore` (path),
`uniqueKeystorePassword`, `uniqueKeyAlias` and `uniqueKeyPassword`. The committed keystore
is the fallback used when `uniqueKeystore` is absent.

> **This is a test key and must be treated as one.** Its password is in the build file in
> the open, so anyone with the repository can build something that installs over the
> tester's copy. Do not distribute an APK signed with it beyond the person testing, and do
> not use it for a store release. The SHA-256 above is what tells the two cases apart.

---

## 4. The Standoff 2 story, which is the main thread

### What the game actually checks — read, not guessed

`docs/STANDOFF2.md` has the full reverse engineering and how to reproduce every claim. The
essentials:

- The game is Unity 6 / IL2CPP, and its metadata is **linked into `libunity.so`** (236 MB),
  in the clear. There is no `global-metadata.dat` and the expansion file contains nothing
  relevant — its index was read over HTTP range requests to establish that.
- There are **two** "virtual space" messages, not one:
  - `Anticheat/VirtualSpaceWarning` — client-side, from a flag literally called
    `VirtualSpaceDetected` in `Axlebolt.Standoff.Anitcheat.AntiCheatManager`, re-evaluated
    on a timer;
  - `AuthRestrictions/VirtualSpaceMessage` — a **server verdict**, in a family with
    `RootFoundMessage` and `UnofficialVersionMessage`.
- The verdict is computed from an `AppVerification` protobuf (`IsRooted`, `ApkHash`,
  `JsonForbiddenApps`, **`Path`**, `ContentHash`, `AppSnapshot`, RSA key) which is a
  **field of `GoogleAuthRequest`** — and of `VkAuthRequest`, `FacebookAuthRequest`,
  `GameCenterAuthRequest` and `TestAuthRequest` too. **Signing in and being told this is a
  virtual space are the same event, whichever login you use.**
- What the client reads to fill it in is **four getters**, counted in the binary:
  `sourceDir` ×4, `getApplicationInfo` ×3, `getPackageCodePath` ×2, `nativeLibraryDir` ×1.
  No `/proc`, no `dl_iterate_phdr`, no process list, no emulator check.

Inside UNIQUE all four answered with a path containing `com.unique`, which no installed copy
of any app can produce. **That is the detection.**

### The fix, and where it is stuck

`core/vam/…/vam/GuestIdentityPaths.kt` rewrites those four *after* the class loader and the
`AssetManager` have been built from the real paths, and the published paths are made to
resolve by the redirect (`VirtualPathModel.redirectionRules` + the native hook).

Neither half is applied until it is measured:

- **code half** — needs the redirect installed and the published `base.apk` to open;
- **data half** — needs a byte written through the public path to be found at the real one,
  twice: once through `java.io.File`, once by opening a SQLite database, because those
  reach the filesystem through different hooks.

`GUEST_PATHS_PUBLISHED package=… code=… data=… slots=… apk=… detail=…` reports both.

**Current state: `code=true`, `data=true`** — measured on the phone, run 18, with
`sqlite=8`. Both halves of the identity a guest reports for itself are published and both
round-trip.

**And that is not the same as working.** In the same run the game showed *"Not enough
storage space to install required resources"*, because `libunity.so` could not open the
APK path UNIQUE had just published to it. The gate that decides whether to publish asks
`java.io.File`, which the redirect covers; the caller that failed was the guest's own,
which it did not. Two causes, both fixed and both **untested**: `__open_2` (§6) and a
library-load watch that was never re-armed (§6). Until a run says otherwise, treat
`code=true data=true` as *published*, not as *safe*.

The virtual-space notice has still never been observed with the paths intact, and it only
appears after a login, so a run without one proves nothing.

---

## 5. Signing in — what is settled and what is not

Read `docs/GOOGLE_SIGN_IN.md` in full; it contains two retractions and both matter.

**The distinction that governs everything**: it depends on *who is asked to vouch for the
app's identity*.

| The SDK asks | Example | UNIQUE |
|---|---|---|
| Play services — another process, resolving the caller by kernel uid | Google Sign-In | two walls, and the first one is fixed in this build — see below |
| the app's own `PackageManager` | Facebook, VK, most SDKs | **already answers correctly** |

### The Google refusal was never the one this project was describing

Re-read the thirteenth and sixteenth logs for *timing* and they say something the outcome
alone cannot:

```
ACTIVITY_IMPLICIT_LEFT_GUEST action=…auth.GOOGLE_SIGN_IN   …460.307
D TokenPendingResult: … Status{statusCode=CANCELED}        …460.686
```

Five attempts in run 16 at 0.23–0.47 s, four more in run 13. **No account picker was ever
drawn**, and `DEVELOPER_ERROR` appears in neither log — nothing got far enough to ask for a
token. The refusal is earlier: the client library builds
`new SignInConfiguration(context.getPackageName(), options)`, which inside UNIQUE names the
guest, while the package that *started* the activity is `com.unique`. Play services compares
them and refuses.

`GoogleSignInHandoff` now copies the configuration and replaces that field with
`com.unique`, reporting `GOOGLE_SIGN_IN_RETARGETED … serverToken=requested|no`. It is
expected to get the picker drawn and then to meet the OAuth-client wall for an app that
asks for an ID token. **No phone has run it.** The `signin` check in the analyzer is what
settles it, and it reads the gap: under two seconds is this refusal again, longer is a
person looking at a list of accounts.

The `PackageManager` row is **proven on the phone**, in the thirteenth run. The Facebook SDK printed
the key hash it computed from `getPackageInfo(getPackageName(), GET_SIGNATURES)`:

```
D com.facebook.unity.FB: KeyHash: lcG7acvUIg0k4FQSQmAbyw1tN0o=      → 95:C1:BB:…:37:4A
UNIQUE's own signing certificate                                    → 64:DB:A6:…:47:6B
```

Different — so the SDK was handed **Standoff 2's** certificate by UNIQUE's virtual
PackageManager.

**What is missing is the return leg.** In that same run, three of the Facebook SDK's four
activities ran inside the space (`FBUnityLoginActivity`, `FacebookActivity`,
`CustomTabMainActivity`) and the fourth opened Chrome. From there the redirect
`fb752573801798020://authorize/…` goes to `PackageManagerService`, which has never installed
a package declaring that scheme, so the sign-in completes on Facebook's side and arrives
nowhere.

**The work**: intercept the authorize `ACTION_VIEW` instead of letting it leave, run it in a
`WebView` in an activity UNIQUE owns inside the guest's process with the guest's cookie jar,
watch navigation for the redirect scheme the guest's manifest declares, and deliver it to the
guest's own activity as an `Intent` — never through `PackageManagerService`. Nothing blocks
it.

**It comes second, and that is a dependency.** Every auth request in the game carries the
same `AppVerification` report, so a completed Facebook login would meet the same
virtual-space verdict. Closing the verdict first is what makes the browser work worth doing.

### Claims that were made here and are wrong — do not re-make them

- ~~"Google sign-in from a virtual space cannot work, ever."~~ That describes the route
  UNIQUE takes. It is not a proof that no route exists. How another engine gets one through
  is the most valuable unknown in that document.
- ~~"Google sign-in fails because the OAuth client is registered against the app's package
  and certificate."~~ True of a wall this project has never reached. Runs 13 and 16 were
  refused in under half a second with no picker drawn, which is a *different* refusal, and
  reasoning from the documented one is what kept it hidden for four runs. Read the timing,
  not just the status code.
- ~~"The game never calls Play Integrity."~~ Measured over three runs of 59–93 seconds
  that all end at a failed login. What is supported is only: *the game does not gate the
  sign-in attempt on Play Integrity* (ChatGPT, in the same log, binds it six times at the
  same stage). What it does after a successful login is unknown — there has never been one.
- ~~"A game that hard-required Play Integrity would be dead on phones without Google."~~
  Axlebolt ships a separate build for Huawei AppGallery, which is exactly how a developer
  hard-requires Google services in the Play build.
- ~~"Launch the game and don't sign in — if the notice is gone, it's fixed."~~ The notice
  appears only *after* a login. A run without one proves nothing.

---

## 6. The path redirection subsystem, and the three ways its scope has been wrong

This is where most of the recent work happened and where the next bug will probably be.

### How it works

- `core/common/…/common/path/VirtualPathModel.kt` owns the path contract as data. Two tables, exact
  inverses of each other:
  - `redirectionRules` — **inward**: a path the guest hands out → where the file really is.
  - `procViewRules` — **outward**: what the kernel says → what the guest is shown, used for
    `/proc/self/maps`, `readlink` answers and `realpath` results.
- `core/native/src/main/cpp/plt_hook.cpp` writes one pointer into a library's GOT for each libc symbol in
  the table. `core/native/src/main/cpp/io_redirect.cpp` holds the trampolines and the tables.
- `tools/native-test/round_trip_test.cpp` asserts the two tables are inverses on exactly the
  paths a guest is handed, and that **no rule can match UNIQUE's own files** — by name,
  against UNIQUE's own preferences, database, diagnostics and installed APK.

### The scope has been wrong three times, each corrected by a log

| Scope | Run | What it cost |
|---|---|---|
| guest's own `.so` files only | ≤13 | covers native code, none of the guest's Java |
| + `libjavacore.so`, `libsqlite.so`, `libandroid_runtime.so` | 14 | a system library outside it could not `statfs` the published APK path — the game told its player the device was out of space |
| **everything** (`scope=*`, 436 slots in 385 libraries) | 15 | the Mali driver could not find its gralloc mapper and **aborted the render thread**: `mali_config_interface_mapper: Failed to acquire IMapper service. Aborting.` + `SIGABRT` |
| named list derived from run 15's own per-library output | 16 | the code gate still refused — see below |
| + `libopenjdk.so` and the large-file symbol spellings | 17 | **`code=true`.** The scope is right now; what was left was not a scope problem at all |
| SQLite through `xSetSystemCall`, not through relocations | 18 | **`data=true`, `sqlite=8`** — and the game could not open the path it had been given, for two reasons that are not about scope either |

**The lesson from run 15, which is the important one**: the safety argument ("no rule can
match `/data/user/0/com.unique`, so a hooked library touching UNIQUE's files is unaffected")
is true and is *not the whole argument*. **A library can be broken by being hooked at all**,
whatever the table then decides. A driver is exactly such a library. `/vendor/`, `/odm/` and
`/system/vendor/` are now excluded outright in `GuestNativeExclusions`.

### Two symbol-level traps, both found the hard way

- **SQLite stores libc addresses in data, not calls.** `{"open", (void*)posixOpen}` is a
  local wrapper and reaches the PLT; `{"stat", (void*)stat}` is the address and does not. A
  GOT hook redirects half of one library. See below: the relocation patch written for this
  never ran, and SQLite is redirected through its own VFS interface instead.
- **`java.io.File` is two libraries.** Writes go through libcore's `Os` API in
  `libjavacore.so`. `isFile()`, `length()`, `lastModified()`, `delete()`, `list()` are
  `UnixFileSystem`'s *native* methods, and those live in **`libopenjdk.so`**, written
  against the large-file API — it calls **`stat64`**, a different *symbol* from `stat` even
  though it is the same function on a 64-bit device. Every write was redirected and the
  first `stat` of a published path was not. Fixed in the build after run 16;
  **unverified on a phone.**

### SQLite was never redirected at all, and the log said so in four characters

The fix written after run 14 was to patch `R_AARCH64_ABS64` relocations in `libsqlite.so`.
Run 17 shows it reaching nothing:

```
io_redirect: hooked libsqlite.so=2+abs
```

Two is the PLT count on its own, and `+abs` says absolute patching was *enabled* for this
library — not that a single absolute slot was found. None were, because **Android links its
platform libraries with `--pack-dyn-relocs`**: `.rela.dyn` becomes an APS2 blob under
`DT_ANDROID_RELA`, and `read_dynamic` reads `DT_RELA`. Such a library reports as having no
data relocations at all, which is exactly what a library with none reports.

SQLite is now redirected through the interface it publishes for the purpose —
`sqlite3_vfs.xSetSystemCall`, version 3 — which replaces an entry of `aSyscall` by name,
covers every use of it, and is indifferent to the link format.
`core/native/…/elf_symbols.h` finds `sqlite3_vfs_find` inside a platform library that an
app's linker namespace refuses to `dlopen`, by walking the loaded library's own `.dynsym`;
`tools/native-test/elf_symbols_test.cpp` checks that walk against `dlsym`'s answer.

Read `sqlite=<n>` on `IO_REDIRECT_INSTALLED` and on `GUEST_PATHS_PUBLISHED`. Zero with a
database refusal is now a `paths` failure rather than a note, and `+packed` on the
per-library line confirms or retracts the premise above directly.

### The two the eighteenth run found, and how both hid

Both are in the redirect, both were verified against bionic's own `libc.so` rather than
reasoned about, and both are the same kind of mistake as the packed relocations: a
mechanism that reported success while reaching nothing.

**`__open_2`, and the `__openat` that never existed.** The NDK turns on `_FORTIFY_SOURCE`
at every optimisation level, so a release build's `open(path, O_RDONLY)` compiles to a
call to **`__open_2`** — a different symbol. The table did not have it. What it had was
`__openat`, which bionic does not export at all, so it matched nothing and printed as a
symbol nothing imports, in the same line as `open64` and `creat64`, which are real and
merely unused. `llvm-readelf --dyn-syms` on the NDK's `libc.so` settles it in one command;
`tools/native-test/check_libc_symbols.py` now runs that check over every name in the table
and skips with a message where there is no NDK.

Added: `__open_2`, `__openat_2`, `__readlink_chk`, `__readlinkat_chk`. Removed:
`__openat`.

**The load watch was armed once.** It hooks `dlopen` in the libraries loaded at the moment
it is installed. `System.loadLibrary` goes through `libnativeloader.so`, which is one of
them; a library that then `dlopen`s another itself is not. Unity is exactly that shape —
`libmain.so` pulls in `libunity.so` — and run 18 contains no rescan for `libunity.so` at
all. `rescan_after_load` re-arms the watch as well as the redirect now, and `g_watching`
is sticky so a quiet re-arm is not read as a failed one.

Run 17 is what proves these are two faults and not one: it *does* contain
`hooked libunity.so=2`, 1.7 seconds before Unity failed anyway.

### If a published path still does not resolve

The gate now separates its three failure modes and says which:

1. `no redirect rule maps the published APK path` — the model and the plan disagree;
2. `the rule maps the published APK path to a file that is not there` — the instance is wrong;
3. `the rule is right and Java cannot see it` — **the hook is missing from the library doing
   the asking**, which is what happened in run 16.

And the native layer prints, on every install:

```
io_redirect: hooked <library>=<n>[+abs]        one line per library that got slots
io_redirect: nothing in this process imports: <symbols>
```

Those two lines are what turned run 16 from a mystery into a diagnosis. **Read them first.**

---

## 7. The phone-log loop, which is how this project learns anything

The owner tests on one device: **Redmi Note 12 (`23030RAC7Y`), Android 15, arm64**. They run
a log-recorder app on the phone, work through the app, and send the capture. No `adb`, no
computer.

```bash
python3 tools/device-log/analyze.py <recorded.log> --device <device.txt>
```

20 checks (`analyze.CHECKS`); exit status 0 when all pass. `tools/device-log/README.md` explains each. Every
phone run is checked in as a fixture under `tools/device-log/fixtures/` with assertions in
`self_test.py`, so **a check that stops reporting a fault a real phone produced is a
regression in the tool** rather than progress in the engine.

Sixteen captures are checked in — the first run, then runs 4 through 18; runs 2 and 3
predate the analyzer and were never kept. When a new log arrives:

1. run the analyzer;
2. read the failures against the log itself — the analyzer has been wrong (it once reported
   UNIQUE's own forecast of `DEVELOPER_ERROR` as Google's answer);
3. add the log as `redmi-android15-run<N>.{log,device.txt}` with a test class whose
   docstring says what that run *proved*;
4. write the run's section into `docs/STATUS.md`;
5. fix; rebuild; refresh `dist/` and its README; commit; push.

---

## 8. What to do next, in order

1. **The nineteenth run**, and the first question is whether the game starts at all.
   - **No "Not enough storage space" dialog**, and no `E Unity: ApkAddCentralDirectory`
     in the log. That is the whole of what the two fixes above are for. If it is still
     there, the log now names the library that patched nothing —
     `io_redirect: hooked libunity.so=0+packed` — and the answer is a symbol the table
     still does not have, not a rescan that did not run.
   - **`io_redirect: hooked … after loading …/libunity.so`.** Its presence is the watch
     re-arm working. Its absence with a working game means Unity was covered by the
     initial scan instead.
   - **Google sign-in.** Tap the Google button in Standoff 2. Watch for
     `GOOGLE_SIGN_IN_RETARGETED … to=com.unique serverToken=…` and then for whether an
     account picker appears at all. The `signin` check times the answer: under two seconds
     is the identity refusal again; longer means the picker was drawn and whatever came
     back is a different answer. `DEVELOPER_ERROR` after an account is chosen would be the
     OAuth-client wall, reached for the first time.
   - **Apps that had data still having it** — a redirect gone wrong shows as an app that
     looks empty, not one that crashes. **Ask the tester to open two or three before the
     game.**
   - Whether the virtual-space notice appears, **and whether it lets play continue**: the
     client warning and the server verdict are different messages, only the second ends the
     session, and neither appears before a login.
2. **The in-space OAuth browser** (§5). The largest unbuilt piece and the one that makes a
   Facebook or VK login able to complete.
3. **The last Google route.** `GMS_PACKAGE_NOT_REWRITTEN descriptor=android.os.IMessenger
   code=1 bareAt=144 size=308` — Firebase Analytics sends the guest's package as a bare
   string inside a Bundle, not as a SafeParcel field. Deliberately not rewritten: changing a
   bare string's length would need the enclosing container's header fixed up, and a corrupt
   request to Play services is worse than a refused one.
4. `bin.mt.plus` and its protector. Unsolved since run 11.

---

## 9. Map of the code

Kotlin sources sit under `<module>/src/main/kotlin/com/unique/…`; the table elides that
middle for readability and `…` stands for `src/main/kotlin/com/unique`.

| Path | What lives there |
|---|---|
| `app/` | The host application, stub component pool, router, Flutter embedding |
| `core/vam/…/vam/AppBootstrap.kt` | The graft: `LoadedApk` construction, hook installation, the whole launch path. Start here. |
| `core/vam/…/vam/GuestIdentityPaths.kt` | The four getters and both gates |
| `core/vam/…/vam/GuestParcelables.kt` | The forwarding class loader for `Intent` extras |
| `core/vam/…/vam/HostPaths.kt` | UNIQUE's own files root, captured once — see its comment for the bug it prevents |
| `core/common/…/common/path/VirtualPathModel.kt` | The path contract, both tables |
| `core/common/…/common/nativelib/GuestNativeExclusions.kt` | Libraries never hooked, each with its run |
| `core/native/src/main/cpp/` | `plt_hook`, `io_redirect`, `proc_view`, `redirect_table`, crash handler, property virtualization |
| `core/google/` | Routing table, `GmsBrokerBinder` calling-package rewrite, `GoogleSignInHandoff` |
| `core/native/src/main/cpp/sqlite_vfs.cpp`, `elf_symbols.h` | SQLite through its own VFS interface, and the symbol walk that reaches it |
| `core/vpm/`, `core/vprocess/`, `core/vstorage/`, `core/vpermission/`, `core/vprofile/` | Virtual PackageManager, process pool, storage, permissions, per-instance device identity |
| `tools/device-log/` | The log analyzer, its tests and every phone fixture |
| `tools/native-test/` | Host-side C++ checks — no device, no NDK |
| `dist/` | The APKs the tester installs, `SHA256SUMS`, and the Russian account of each build |

---

## 10. Things that will waste your time if nobody tells you

- `tools/device-log/analyze.py` and its fixtures are the memory of this project. Before
  concluding anything about the engine's behaviour, check whether a fixture already says
  otherwise.
- The owner writes in Russian and reads `dist/README.md`. Answer in Russian; keep code,
  commits and the other documents in English.
- Do not stack two large changes into one build. Run 14 and run 15 each broke something and
  the only reason either was diagnosable is that the rest of the build was unchanged.
- Commit messages in this repository are long and explain the *evidence*, not the diff. Keep
  that; they are how the next round finds out why something is the way it is.
- Never put a model name or identifier into a commit, a PR or the code.
- `git push -u origin claude/unique-app-virtualization-bn35b2` and nowhere else.
