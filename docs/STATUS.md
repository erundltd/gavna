# UNIQUE — status

Regenerate the "not implemented" section with `tools/report-unimplemented.sh`. This file
exists because ARCHITECTURE.md §18 rule 1 forbids describing unfinished work as done.

**Phases 0-5 complete. Phases 6 and 7 begun: signatures, per-instance device identity, splits and updates, cross-process components, and a compatibility pass driven by five runs on a real phone.**

A real APK — not installed on the device — is imported, registered, given an instance, and
launched into a `:vappN` process where it believes it is itself. Its Activity, Service,
manifest BroadcastReceiver and ContentProvider all run as the guest, in the guest's
storage, in the guest's process. Evidence is checked in under `docs/evidence/`.

For a single-file account of the whole project — build, signing key, the target
application, every open item and the claims already retracted — see
[`HANDOFF.md`](HANDOFF.md).

## What each environment can prove

| Environment | Proves | Cannot prove |
|---|---|---|
| Build machine (JVM + host C++) | Parsers, path contract, ELF checks, shim engine, Google routing table, redactor | Anything about a running Android system |
| **Android 14 x86_64 emulator** (`aosp_atd`, software rendering, no KVM) | The engine graft — it is pure Java and architecture-independent — and, through `tools/real-app-smoke.sh`, applications the project did not write | ARM64 native code, real GPU paths, OEM framework forks, Android 15/16 behaviour, 16 KB pages, **anything involving Play services or an IME** — the image has neither |
| ARM64 Android 15 phone | Everything above, for real | **Five runs so far**, each a capture read by `tools/device-log/analyze.py`; three are checked in as fixtures. See `docs/PHYSICAL_DEVICE_TEST.md` |

Every device claim below names the environment. Nothing is marked working on reasoning.

## Off-device: done and tested

| Area | Tests | Notes |
|---|---|---|
| Binary XML / manifest decoding | 16 | Against real `aapt2` (build-tools 36) output |
| APK bundle + split selection | 8 | Found a real de-duplication bug |
| ELF: ARM64 + 16 KB alignment | 7 | Against real NDK r27 `.so`, 4 KB and 16 KB |
| Virtual path contract | 12 | Every accessor and every alias pinned |
| Native redirect table (C++) | 34 checks | Host-side binary, no device needed |
| The two path tables are inverses (C++) | 27 checks | Every path a guest is handed round-trips; no rule can match UNIQUE's own files |
| The relocation a GOT hook must patch | 2 checks | Taking the address of a libc function compiles to an absolute, zero-addend data relocation — the case SQLite uses. The seventeenth run showed the patch never reaching it on a platform library; see `sqlite_vfs.h` |
| Finding a symbol in a library that cannot be `dlopen`ed (C++) | 10 checks | Against a real linker's output and against `dlsym`'s answer for the same name — the walk that reaches SQLite's own VFS interface |
| The Google sign-in handoff | 12 | Which field is rewritten and which is left alone, that a static field is never one, that an inherited one is still found, that the configuration is recognised by type rather than by the key it arrives under, and that a request for a server token is reported before Google answers |
| Signature-agnostic shim engine | 12 | Includes one shim bound to two different signatures, and conditional `proceed()` |
| Settings screens a guest opens about itself | 6 | Which half of the intent names the app, and every case that must be left alone |
| Device profile model | 9 | Shape, stability, regeneration, RFC 4122 |
| Compatibility resolver | 5 | Local-override merging |
| Diagnostics redactor | 7 | JWTs, `ya29.`, bearer headers, emails, key names |
| Google routing table | 10 | Every flow's decision pinned |
| Stub / job / channel namespacing | 8 | Two instances cannot collide |
| Flutter UI | 15 | Includes both shapes the engine's Google status arrives in, and that the reason an app cannot be added is a key both languages carry |
| Runtime vs install-time permissions | 7 | Every dangerous group enumerated; the three a device run found denied are install-time |
| Process slot pool | 12 | Release ends the process; a dead slot is reclaimed; a full pool refuses rather than evicts |
| Per-instance permission store | 12 | Undecided install-time is granted, undecided runtime is not, and neither can exceed the host |
| Device-log analyzer | 35 | Against a real Android 15 run, plus a synthetic healthy one |
| APK survey (DEX reader, service map) | 17 | The reader is checked against a real checked-in APK |
| Which packages a guest may see | 6 | The Google stack hidden, `com.android.vending` not, a prefix match not enough, and both shapes intent resolution answers in — the emulator has no Play services, so this is where the decision is pinned |
| Window and task attributes | 9 | `hardwareAccelerated` at both levels including the `targetSdk >= 14` default, orientation, config changes, the task flags, typed meta-data, and a provider's own grant flag — against real `aapt2` output |

**302 JVM tests, 15 Dart tests, 145 native checks (41 of them need an NDK and skip without one), 156 off-device tool tests — all passing.**

## On device (EMU34): verified working

| Step | Evidence |
|---|---|
| APK import from a file, package **not installed** on the host | `PACKAGE_IMPORTED package=com.unique.probe versionCode=7 bytes=12709` |
| Imported APKs are read-only (W^X) | Asserted by the suite: `baseApk.canWrite()` is false |
| Virtual package registered in the state database | `t01` passes |
| Instance created with directories and a device profile | `INSTANCE_PREPARED created=13`, `INSTANCE_CREATED androidId=…` |
| `:vappN` process starts and is identified | `PROCESS_START process=com.unique:vapp0 kind=VAPP` |
| Hidden-API access obtained | `HIDDEN_API_GRANTED via=HiddenApiBypass` |
| Launch interceptor installs on `ActivityThread.mH` | `INTERCEPTOR_INSTALLED executeTransaction=159` |
| Launch transaction reaches the interceptor | `CALLBACK_ALIVE what=159 obj=…ClientTransaction` |
| `LaunchActivityItem` located and its shape understood | No `LAUNCH_ITEM_SHAPE_UNKNOWN`; sibling transactions correctly ignored |
| Virtual `PackageManager` installed | `SERVICE_HOOKED service=package cache=true singletons=1` — 7 methods bound |
| Native library loads in the virtual process | `libunique_native loaded (page size 4096)` |
| `LoadedApk` graft completes | `BOOTSTRAP_OK … applicationClass=com.unique.probe.ProbeApplication` |
| Launch transaction rewritten to the guest's own component | `TRANSACTION_REWRITTEN activity=com.unique.probe.ProbeActivity` |
| **The guest's own `Application` subclass runs** | `UniqueProbe: Application.onCreate package=com.unique.probe class=com.unique.probe.ProbeApplication filesDir=…/virtual/users/0/…` |
| The platform launches the guest's real Activity | `Unable to start activity ComponentInfo{com.unique.probe/com.unique.probe.ProbeActivity}` — the component is the guest's, not a stub |
| Outbound identity accepted by system_server | `VAM_HOOK_INSTALLED package=com.unique.probe host=com.unique` |
| A crashing virtual process leaves a diagnostic | `CRASH UNCAUGHT_EXCEPTION` recorded by `CrashGuard` |

## On device (EMU34): the acceptance suite

Android 14 x86_64, probe **not installed on the device**. The suite is **46 tests**, and
the newest run — `RUN_ID=ru-pass-1`, the `debug` build — passes all of them.

It was 47 until this pass: `t37` covered the diagnostics export, the device report and the
checklist, and all three were removed with the UI that reached them.

The last run of the `verify` build a tester is actually handed, `20260905-125829-4200`,
was **38 of 38** against the suite as it stood then; everything after `t38` was added later
and has only been run on `debug`. Full output of that run in
`docs/evidence/phase3-4-instrumentation.txt`.

| Test | Result |
|---|---|
| `t01` import, register, create an instance | **PASS** |
| `t02` launch, and the app sees its own identity | **PASS** |
| `t03` the app writes nothing into UNIQUE's own directories | **PASS** |
| `t04` data survives a full process kill and relaunch | **PASS** |
| `t05` a second instance is fully independent | **PASS** |
| `t06` a crashing instance kills neither UNIQUE nor its sibling | **PASS** |
| `t07` the guest's own Service runs, started *and* bound | **PASS** |
| `t08` the guest's manifest BroadcastReceiver gets broadcasts | **PASS** |
| `t09` the guest's ContentProvider answers its own authority | **PASS** |
| `t10` the guest starts its own second Activity | **PASS** |
| `t11` a `PendingIntent` the guest built fires into the guest | **PASS** |
| `t12` a runtime permission belongs to the instance, not to UNIQUE | **PASS** |
| `t13` a grant survives the virtual process being killed | **PASS** |
| `t14` app ops accept the guest's identity | **PASS** |
| `t15` the guest's notification is posted, and two instances do not collide | **PASS** |
| `t16` the guest's foreground service starts | **PASS** |
| `t17` the guest loads and runs its own native library | **PASS** |
| `t18` the guest's job is scheduled, and the system runs it | **PASS** |
| `t19` the guest sets alarms and uses the clipboard | **PASS** |
| `t20` the guest renders with OpenGL, and the pixel reads back | **PASS** |
| `t21` the guest reads its own signature, and the truth about the Google stack | **PASS** |
| `t22` two instances have different device identities | **PASS** |
| `t23` an update keeps the instance's data | **PASS** |
| `t24` an update signed by someone else is refused | **PASS** |
| `t25` a *dead* guest is woken by a broadcast, and its receiver runs | **PASS** |
| `t26` UNIQUE itself reads a guest's provider, across the process boundary | **PASS** |
| `t27` a guest reaches its own provider in another of its processes | **PASS** |
| `t28` the guest creates a Vulkan instance, device and graphics queue | **PASS** |
| `t29` cold start, warm start and memory are measured and recorded | **PASS** |
| `t30` the guest runs a WebView, in its own data directory | **PASS** |
| `t31` the guest starts its own activity by an implicit intent | **PASS** |
| `t32` killing a guest's provider process leaves UNIQUE alive and still able to read another | **PASS** |
| `t33` a native crash leaves a diagnostic record UNIQUE wrote | **PASS** |
| `t34` a guest shares one of its own files with something outside it | **PASS** |
| `t35` the Google routing decision is real, and follows this device | **PASS** |
| `t36` a guest reaching another app's provider gets a well-formed answer | **PASS** |
| `t38` a guest reads a setting, and is called by its own name | **PASS** |
| `t39` the guest's window is hardware-accelerated, read after it was attached | **PASS** |
| `t40` the guest's declared `screenOrientation` reaches the platform | **PASS** |
| `t41` the guest reads its own meta-data, with a resource reference resolved | **PASS** |
| `t42` the guest resolves its own components through its own PackageManager | **PASS** |
| `t43` the guest sees its own process name, and not UNIQUE's | **PASS** |
| `t44` the guest's external storage is its own, and can be written to | **PASS** |
| `t45` a launch redelivered to a running activity arrives as the guest's own intent | **PASS** |
| `t46` the guest starts its own service by action, with no class named | **PASS** |
| `t47` the guest turns one of its own components off, and it stops matching intents | **PASS** |

### What it costs, on this emulator

Recorded by `t29` on every run. **Software-emulated x86_64 with no hardware acceleration**,
so these are not device numbers and no budget is asserted against them (§17.1) — they are
a baseline a physical-device run is compared against.

| Measurement | Run `20260904-221502-8465` | Run `20260905-093631-18438` | Run `ru-pass-1` |
|---|---|---|---|
| Cold start, fork → the app's first screen ready | 37.5 s | 12.1 s | 23.6 s |
| ... of which fork → guest `Application.onCreate` (the graft) | 27.6 s | 8.2 s | 21.7 s |
| Warm start, request → ready, into a live process | 5.4 s | 2.3 s | 3.6 s |
| Virtual process memory, total PSS | 30.2 MB | 33.1 MB | 37.9 MB |

The three columns are the same engine on the same emulator, three times apart in every
timing, and the difference is not UNIQUE: the first was measured with Gradle's and Kotlin's
daemons resident, holding about five gigabytes between them on a machine sized for one
emulator. That is what a wall-clock number on this environment is worth, and why none is
asserted against (§17.1). `tools/verify-device.sh` now stops the daemons before it
instruments, because at load 10 the platform was killing processes before they could
attach — `Killing …:com.unique:vapp2 (adj -10000): start timeout`, and
`com.android.bluetooth` in the same second, which is how the machine was finally
distinguished from the engine.

The graft dominates cold start, which is what a JIT-only process loading UNIQUE's
interception layer before any guest code looks like. PSS rather than RSS: a virtual process
shares the whole framework and UNIQUE's own code, and RSS would count those pages in full
in every process at once.

What the guest's non-Activity components reported
(`docs/evidence/phase4-native-engine.log`):

```
Service.onCreate       package=com.unique.probe process=13624
Service.onStartCommand startId=1 count=1
Service.onBind         count=1 component=com.unique.probe/.ProbeService
onServiceConnected     com.unique/.stub.ServiceStub_p0_s0
Receiver.onReceive     action=com.unique.probe.PING package=com.unique.probe
Provider.onCreate      package=com.unique.probe
ACTIVITY_INTENT_ROUTED activity=…ProbeSecondActivity stub=…ActivityStub_p0_m0_a0
TRANSACTION_REWRITTEN  activity=com.unique.probe.ProbeSecondActivity
PERMISSION_CHECK       permission=android.permission.CAMERA result=DENIED
PERMISSION_RESULT_RECORDED permission=…CAMERA granted=true blockedByHost=false
cameraAfter            = GRANTED   (the host held it all along; the instance did not)
rowCount               = 3
provider.packageName   = com.unique.probe
provider.filesDir      = …/virtual/users/0/data/com.unique.probe/files
provider.pid           = 13624   (the same process as the Activity)

PACKAGE_IMPORTED       package=com.unique.probe versionCode=22 libs=1 abi=x86_64
loaded                 = true
arch                   = x86_64      (the device's own instruction set; nothing emulated)
nativePid              = 17583       (= the Activity's pid)
echo                   = native:hello
nativeLibraryDir       = …/virtual/apk/com.unique.probe/22/lib/x86_64
libcWrite              = ok:…/virtual/users/0/data/com.unique.probe/files/probe-libc.txt

io_redirect installed  1 slot(s) in 1/318 libraries, 8 rule(s), page size 4096
libcRawWrite           = ok:/data/data/com.unique.probe/files/probe-libc-raw.txt
libcRawLandedInInstance= true    (a path hard-coded in native code, redirected by libc)
```

What the guest itself reported (`docs/evidence/phase2-first-launch-engine.log`):

```
packageName            = com.unique.probe
applicationClass       = com.unique.probe.ProbeApplication
applicationOnCreateRan = true
applicationBeforeActivity = true
activityClass          = com.unique.probe.ProbeActivity
componentName          = com.unique.probe/com.unique.probe.ProbeActivity
dataDir                = …/virtual/users/0/data/com.unique.probe
filesDir               = …/virtual/users/0/data/com.unique.probe/files
codeCacheDir           = …/virtual/users/0/data/com.unique.probe/code_cache
packageCodePath        = …/virtual/apk/com.unique.probe/7/base.apk
appInfoNativeLibraryDir= …/virtual/apk/com.unique.probe/7/lib/arm64-v8a
targetSdk              = 34
uid                    = 10109   (UNIQUE's own — see below)
```

Three things are worth reading carefully:

- **`applicationBeforeActivity = true`.** The guest's `Application.onCreate` ran before its
  `Activity`, which is the ordering apps depend on and the reason the graft happens at the
  launch transaction rather than at process start.
- **`packageCodePath` is the shared read-only APK** while every writable path is under
  `users/0/`. That split is what makes a second instance cheap.
- **`uid` is UNIQUE's.** This is correct and permanent: UNIQUE is not a privilege
  boundary. The guest's *package identity* is virtual; its *Linux identity* is the host's.


## The first physical-device run

**Xiaomi Redmi 23030RAC7Y, Android 15 (API 35), HyperOS, arm64-v8a, 4 KB pages.** Run by
the phone's owner on 2026-09-05 and captured with a logcat app — no `adb`, no root, no
computer, which is what the artifact was built for.

**No app launched.** Two faults, both invisible to the verification emulator, both now
fixed and neither re-proven on hardware.

**A settings read from inside a guest was refused.**

```
SecurityException: Package com.gordey.standarling does not belong to 10300
    at android.provider.Settings$NameValueCache.getStringForUser
    at android.database.sqlite.SQLiteCompatibilityWalFlags.initIfNeeded
    at android.database.sqlite.SQLiteDatabase.<init>
```

A settings read is a content-provider `call` carrying an `AttributionSource`, and the
provider checks it against the uid. UNIQUE wraps the provider precisely to substitute the
host's — but nothing acquires the settings provider *after* the graft. The framework reads
one during `handleBindApplication`, before a line of guest code exists (`GraphicsEnvironment:
Global.Settings values are invalid` is that read, in the log, at the top of the process's
life), and the raw binder it gets is cached in `ActivityThread.mProviderMap` and in the
static `NameValueCache` inside each of `Settings.Secure`, `Global` and `System`. The
wrapper never got in front of them. Since `SQLiteDatabase` reads a setting before it opens
anything, the guest could not open a database, could not attach an Activity, and never
started. Fixed by evicting both caches at the end of the graft, before the guest's
`Application.onCreate`.

**`getHistoricalProcessExitReasons` went out under the guest's own name.**

```
SecurityException: Permission Denial: getHistoricalProcessExitReasons
    from pid=22773, uid=10300 requires android.permission.DUMP
```

It needs `DUMP` only for a package that is not the caller's own, so a guest asking why it
died last time was asking about a stranger — and Crashlytics-style startup code took the
application down with it. The identity rewrite's allowlist did not name it. It now has a
structural rule for the interrogative half of `IActivityManager` (`get`, `is`, `check`,
`has`, `query`, `report`), with an explicit exclusion for verbs that *act* on a package,
because rewriting `forceStopPackage` would turn "stop me" into "stop UNIQUE".

**A guest could not read its own name either**, for the same reason one level deeper:
`ApplicationInfo.labelRes` was never set, because the manifest reader kept `android:label`
as text and not as a resource id, so `getApplicationLabel` fell back to the package name.
An app's own about screen, notification title or share sheet would have shown
`com.example.app`. Both halves are carried now.

**Two things that were not launch failures but were plainly wrong.** Every imported app
was listed as `@7f010000` — `android:label` is a reference into the APK's resource table
and UNIQUE's binary-XML reader has none — and none of them had an icon, because
`getApplicationIcon(packageName)` answers only for packages the device has installed.
Both are now read from the stored APK through the platform's own parser, which also gets
the label in the phone's language for free.

**And the log was nearly empty of UNIQUE's own events.** The build a tester installs is not
debuggable, `Diagnostics.verbose` was `BuildConfig.DEBUG`, and so a logcat capture from the
phone held only warnings and errors: no `PROCESS_START`, no hook report, none of the trace
before the failure. That is now a build-type flag of its own, on for the `verify` build.

`t38` covers the settings read and the label, and the probe was given a real resource table
and a localized name so both are exercised rather than asserted.

### The second run: further in, still not launching

Same phone, same day, with the fixes above installed and full logging on. The graft now
**completes**: `BOOTSTRAP_OK`, fifteen of ChatGPT's providers published, the launch
transaction rewritten to `com.openai.chatgpt.MainActivity`. Then the guest crashes, and it
crashes reading a setting again:

```
SecurityException: Package com.openai.chatgpt does not belong to 10300
  at android.content.ContentProviderProxy.call        ← the raw binder, not UNIQUE's wrapper
  at android.provider.Settings$NameValueCache.getStringForUser
  at android.database.sqlite.SQLiteCompatibilityWalFlags.initIfNeeded
```

Emptying the caches was not enough, and the log says why by what is *missing* from the
stack: no wrapper frame. `ActivityThread.installProvider` does this with whatever
`getContentProvider` hands back:

```java
IBinder jBinder = provider.asBinder();
ProviderRefCount prc = mProviderRefCountMap.get(jBinder);
if (prc != null) {
    provider = prc.holder.provider;   // the wrapper is dropped here
}
```

UNIQUE's wrapper answers `asBinder()` with the *raw* binder — it has to, that is what makes
it a usable `IContentProvider` — so a provider this process acquired before the graft is
found in that second map by its own binder and the wrapper is discarded in favour of the
record already there. `mProviderRefCountMap` is now evicted alongside `mProviderMap`, and
the wrapper is additionally installed into each `Settings` holder **by hand** rather than
arranged for, so nothing depends on which path a later acquisition takes.

The log also settled a question that had been guesswork: `hostSource=com.unique
hostSourceUid=10108`. The identity being substituted was right all along; it was simply
never reaching the call.

Two more faults from the same log:

- **`FATAL EXCEPTION: nfz Dispatcher`** — Conscrypt dereferencing a null
  `NetworkSecurityPolicy` while closing a TLS socket. A `:vappN` inherits the policy the
  platform installed for *UNIQUE* during `handleBindApplication`, because the guest did not
  exist yet. That is wrong even when nothing crashes: cleartext rules and certificate
  pinning are the guest's security decisions, and running its traffic under the host's
  either relaxes them or breaks them. The guest's own config is now installed after its
  Application is created.
- **`ClassNotFoundException: Invalid name: com.openai.chatgpt.`**, six times per launch. A
  `<provider>` with no `android:name` — real APKs contain them, left behind by a manifest
  merge — and qualifying an empty name against the package produces `<pkg>.`. Components
  with a blank name are now dropped at parse time, as the platform drops them.

**Still not confirmed working on hardware.** None of this is re-marked in
`docs/COMPATIBILITY.md`: an emulator that never reproduced these cannot confirm them.

### The third run: what a whole session on the phone looks like

The first two runs each read one crash. This one is a whole session — seven apps imported
and launched on a Redmi Note 12 (Android 15, `sea_ru`, HyperOS), 26,950 lines of log, of
which 545 are UNIQUE's own. It is checked in at
`tools/device-log/fixtures/redmi-android15.log`, and
[`tools/device-log/analyze.py`](../tools/device-log/README.md) is what reads it, because
finding thirty lines in twenty-seven thousand by eye is not a method.

The reported symptom was **"Gemini started, and after that nothing launches at all."**
Both halves have a cause and neither is the app's.

**Nothing launched after Gemini.** `ProcessPool.release()` freed a slot's record and
killed nothing. Removing an instance called it, the `:vapp0` process kept running with
Gemini's graft still installed, and the next three apps were each assigned slot 0:

```
BOOTSTRAP_FAILED package=clear.una        code=SLOT_ALREADY_BOUND
    message=Slot 0 already serves com.google.android.apps.bard (u0)
BOOTSTRAP_FAILED package=com.f0x1d.logfox code=SLOT_ALREADY_BOUND
BOOTSTRAP_FAILED package=bin.mt.plus      code=SLOT_ALREADY_BOUND
```

Slot 0 is the first free slot, so it was picked every time and every launch from then on
failed identically. The pool now ends a slot's process when it releases it, checks
liveness before handing a slot to a new occupant, and reclaims slots whose process has
died — the death observation the doc comment claimed and nothing performed. A `:vappN`
that still finds itself double-booked ends itself rather than refusing forever, which
costs one tap instead of every launch after it.

**ChatGPT crashed on its first screen**, twice, and a third refusal killed its network
thread:

```
SecurityException: Only system may: get application restrictions for other user/app com.openai.chatgpt
    at android.content.RestrictionsManager.getApplicationRestrictions
    at com.openai.chatgpt.MainActivity.onCreate
SecurityException: getApplicationLocales: Neither user 10300 nor current process has READ_APP_SPECIFIC_LOCALES.
SecurityException: Package com.openai.chatgpt does not belong to 10300
    at android.net.ConnectivityManager.getNetworkCapabilities
```

Three spellings of one fault: a call carrying a package name that does not belong to
UNIQUE's uid, to a service that was not proxied. `restrictions`, `locale` and
`connectivity` are now in `SystemServiceHook.TARGETS`, along with the rest of the set an
ordinary app touches while starting — each listed with what a guest loses without it in
`VirtualIdentityHooks.CALLER_PACKAGE_SERVICES`. A guest asking to set its *own* app locale
is refused rather than aimed at UNIQUE's, which is the only other place that call could
land.

**And every guest was running with no network permission.** Not from any of the crashes —
from the permission model:

```
PERMISSION_CHECK permission=android.permission.ACCESS_NETWORK_STATE result=DENIED   (x36)
PERMISSION_CHECK permission=android.permission.INTERNET              result=DENIED   (x10)
PERMISSION_CHECK permission=android.permission.WAKE_LOCK             result=DENIED   (x4)
```

`PermissionStore` treated every undecided permission as denied. That is the right rule for
a runtime permission and meaningless for an install-time one: `INTERNET` has no dialog,
no app requests it, and there was no action any user could have taken to grant it. Install-
time permissions are now granted because the manifest asked for them, as at install, and
only dangerous ones are the user's decision. A permission the guest defines itself is
granted too — the host cannot hold one only the guest declares.

Two more, from the same log:

- **UNIQUE's own App Details screen was dead.** `GoogleStatus.fromMap` threw
  `type 'String' is not a subtype of type 'bool?' in type cast` on the first field it read,
  fifteen times. The engine sends `Map<String, String>` because the same map goes into
  Diagnostics; the Dart was written to tolerate that and `as bool?` *throws* on a `String`
  rather than evaluating to null, so the `??` fallback meant to catch it never ran.
- **`IO_REDIRECT_INSTALLED status=NOT_IMPLEMENTED`** on the Unity app and on Gemini, which
  read as a missing subsystem and was not one. It means zero PLT slots were patched
  because the guest had not loaded its libraries yet — Unity loads them from its own
  initialiser, long after `Application.onCreate` — and `watch=OK` says the dlopen watch
  will catch them. That state is now `NOTHING_TO_HOOK`, and the watch matching nothing is
  `kFailed`, which is what it always was.

**Still not confirmed working on hardware.** Every fix above is reasoned from this log and
none has been back on the phone. `docs/COMPATIBILITY.md` is unchanged for the same reason
it was unchanged after the second run.

### The fourth run: the fixes hold, and the ordering was wrong all along

The third run's fixes, back on the same Redmi. What they were meant to do, they did:

| | Run 3 | Run 4 |
|---|---|---|
| Launches reaching the guest's Activity | 7 of 10 | **8 of 8** |
| `SLOT_ALREADY_BOUND` | 3 | **0** |
| `INTERNET` / `ACCESS_NETWORK_STATE` denied | 46 times | **0** |

`PERMISSIONS_BOUND` now says what it grants: ChatGPT, 31 declared — 24 at install, 7 for
the user. The Play Store, 145 declared — 129 at install, 16 for the user.

And ChatGPT still died, on a call to a service that **was** proxied:

```
SecurityException: Caller not system or systemui or same package: uid 10302 does not have
    android.permission.STATUS_BAR_SERVICE
  at NotificationManager.areNotificationsEnabled
SecurityException: Package com.openai.chatgpt does not belong to 10302
  at ConnectivityManager.getNetworkCapabilities                    (statsig, on a worker)
```

Both services are in `TARGETS` and both hooks install. The hooks just arrived too late.
`makeApplication` ran the guest's `Application.onCreate` at line 349 of `AppBootstrap`;
the identity hooks land at 416 and the notification hook at 434. A guest's `onCreate` is
not a quiet moment — it starts analytics, opens a network stack, asks whether
notifications are enabled — and `NotificationManager.sService` is a **static** field, so
the raw interface it captured there outlived the hook that came sixty lines later.

The same ordering explains a provider failure nobody had connected to it:

```
PROVIDER_PUBLISH_FAILED provider=androidx.startup.InitializationProvider
    error=IllegalStateException: WorkManager is already initialized.
```

`androidx.startup` is built on the platform's guarantee that providers are published
*before* `Application.onCreate`. UNIQUE was publishing them after.

Both are one fix. `makeApplicationInner` uses its `Instrumentation` argument for exactly
one thing — `callApplicationOnCreate` — so it is passed null, and `onCreate` is called
explicitly at the end of the graft. That is `handleBindApplication`'s own order: make the
Application, install the providers, then start it.

Three more faults from the same log, all now closed:

- **`IStorageManager.getVolumeList`** — `callingPackage does not match UID`, reached from
  `Environment.isExternalStorageManager`, killed `clear.una` on its first frame. `mount`
  is proxied.
- **`IAccountManager`** — declared in `TARGETS` since forever and installed by nothing, so
  the Play Store died asking about accounts. Now installed.
- **`search`** — the caller-package shim bound to *nothing*, nine times per run:
  `ISearchManager` on API 35 declares seven methods and not one takes a String. It carries
  no caller identity, so it is removed rather than left looking installed.

**"The screen is laggy"**, reported by the tester, is a launch with both of the things
that normally cover a launch switched off. `Theme.Unique.Stub` set `windowDisablePreview`
to true and `windowAnimationStyle` to `@null`, neither ever explained — so a tap produced
no starting window, then content snapped in with no transition, after however long a
JIT-only cold start takes. Nothing was dropping frames: that run has **one** skipped-frame
event in 37,775 lines. Both overrides are gone; the platform's own behaviour is back.
Unconfirmed on hardware, like everything else here.

### Reading the fourth run's log again: what was still wrong with every launch

The fourth run's log was read once for the two crashes it names above. Read end to end —
39,958 lines, four applications, ten launches — it says four more things, and the first of
them was true of **every application UNIQUE has ever run**.

**Every guest was rendering in software.** `AppBootstrap.buildActivityInfo` builds the
`ActivityInfo` the platform launches a guest with and never set `flags`. That field has
exactly one consumer at attach time:

```java
// Activity.attach
mWindow.setWindowManager(…, mToken, mComponent.flattenToString(),
        (info.flags & ActivityInfo.FLAG_HARDWARE_ACCELERATED) != 0);
```

so every virtual activity was created with the hardware renderer switched off. `android:
hardwareAccelerated` was not read by the manifest parser at all — neither the activity
attribute nor the `<application>` default of `targetSdk >= 14` that every modern app
inherits. Two symptoms, one cause:

- Everything drew slowly. That is the second half of "the screen is laggy"; the first half
  was the stub theme, and both were real.
- Anything drawing through a `RenderNode` — Compose, any hardware layer, most modern view
  code — died on its first frame:

```
CRASH u1 clear.una UNCAUGHT_EXCEPTION thread=main
  reason=IllegalArgumentException: Software rendering doesn't support drawRenderNode
    at android.view.RecordingCanvas.drawRenderNode
    at …DecorView.draw / ViewRootImpl.drawSoftware
```

`drawSoftware` in that stack is the platform saying so in as many words.

**A Play-protected app killed itself before drawing anything.** `1Tap Cleaner` is signed by
Google's PAIRIP, and PAIRIP checks the licence on startup:

```
E LicenseClient: Not allowed to bind with the licensing service:
    com.android.vending.licensing.ILicensingService
  Caused by: java.lang.SecurityException: Not allowed to bind to service
      Intent { act=com.android.vending.licensing.ILicensingService pkg=com.android.vending }
I System.exit called, status: 0
```

The service is exported and Play was installed; what was missing is that Play guards it
with `com.android.vending.CHECK_LICENSE`, and a permission is checked against the *calling
uid*, which is UNIQUE's. The guest declared it and UNIQUE did not. Every application
Google re-signs — which is every paid app and a large share of free ones — performs this
check, treats a bind failure as tampering, and exits. The log line UNIQUE printed beside it
pointed the other way: `SERVICE_INTENT_IMPLICIT`, thirty times, for an intent that was
scoped to `com.android.vending` and perfectly ordinary.

**Google Play services was told the app had no manifest.** `ApplicationInfo.metaData` was
never populated, so:

```
E FA: Task exception on worker thread: java.lang.IllegalStateException: A required
  meta-data tag in your app's AndroidManifest.xml does not exist. You must have the
  following declaration within the <application> element:
  <meta-data android:name="com.google.android.gms.version" …/>
```

on both the Unity game and ChatGPT. The apps declare it. `android:value` there is a
*reference* into the app's resource table, which is why carrying the manifest's text was
not enough: the type and the compiled datum have to travel together and be resolved
against the guest's own resources, which only a virtual process has.

**A landscape game opened portrait.** `screenOrientation` was parsed and put on the
substituted `ActivityInfo`, where nothing reads it: the platform takes a window's
orientation from the `ActivityRecord` it built in `system_server` from the *stub's*
manifest entry, and the stub says `unspecified`. `vri size:1080x2400` on a Unity game that
declares landscape is the whole of "игры запускаются вертикально".

Two smaller things from the same log:

- **`service download was not proxied: service not available`, ten times.**
  `Context.DOWNLOAD_SERVICE` is the string `"download"` and there is no binder service of
  that name — `DownloadManager` is built from a `ContentResolver`. The APK survey counted
  the *manager*. Removed, which is rule 8.
- **A 1.6 GB APK stalled the launch for 2.5 seconds on the main thread**, inside
  `getPackageArchiveInfo`, which digests every byte of the APK to verify its signing block.
  The OEM's hang watchdog saw it before the user could:
  `MIUIScout App: Enter APP_SCOUT_WARNING State (duration=2501ms … w=159)`. Nothing in the
  graft needs the signature, so it is loaded on a thread of its own and waited for only by
  a caller that asked for it.

### What that pass changed, and one bug it found on the way

Everything above is fixed, and six things around them that the same reading turned up:

| Found | Fixed by |
|---|---|
| The `ActivityInfo` carried no `flags`, `softInputMode`, `uiOptions`, resize mode or aspect-ratio clamp — the manifest parser read none of them | `WindowAttributes` on every component entry, composed into `ActivityInfo` with the platform's own constants. `WindowAttributesTest` holds the two-level `hardwareAccelerated` default against real `aapt2` output |
| `ApplicationInfo` claimed none of the screen-support flags, so `CompatibilityInfo` was entitled to put every guest in screen-compatibility mode — a scaled, letterboxed window with the density lied about | The flags `PackageParser` sets for a modern target SDK, plus `FLAG_HARDWARE_ACCELERATED`, `FLAG_LARGE_HEAP`, `FLAG_SUPPORTS_RTL` and legacy-storage |
| `getPackageInfo` ignored its `flags`, so `GET_ACTIVITIES`, `GET_SERVICES`, `GET_PROVIDERS` and `GET_META_DATA` all came back null | `GuestComponents`, which also answers `getServiceInfo`, `getReceiverInfo` and `getProviderInfo` — three siblings of `getActivityInfo` that were never hooked |
| `getLaunchIntentForPackage(getPackageName())` returned null, and so did `resolveActivity` for the guest's own component: the platform has never installed the package being asked about | `GuestIntentResolution`, scoped strictly to intents that name the guest — an `https` VIEW still belongs to the device's browser |
| A guest enumerating installed packages did not find *itself* and did find `com.unique` | Both corrected in `getInstalledPackages`/`getInstalledApplications`; nothing else in the list is touched |
| `getExternalFilesDir()` returned a path scoped storage will not let UNIQUE create, because it is named for the guest's package and UNIQUE's is `com.unique` | The `mount` proxy rewrites `getVolumeList`, which is the one place `Environment` builds every external path from |
| `/proc/self/cmdline` read `com.unique:vapp0`, and `getRunningAppProcesses` showed UNIQUE's processes and sibling instances | `Process.setArgV0`, as `handleBindApplication` does it, and a process-table rewrite that also stops one instance seeing another |
| A permission the *host* lacks could never be granted to an instance: the switch in App Details was disabled, and the row explained a problem with nothing to press | The switch asks Android for the permission on UNIQUE's behalf, and offers the settings page when the platform has stopped asking |

The bug found on the way is the one worth naming, because it is a *launch* failure and it
was reachable on any slow device:

```
PROVIDER_READY_NOT_SEEN slot=0 vuid=0 waitedMillis=45147 rewarms=2
ANR in com.unique:vapp0   Reason: executing service com.unique/.stub.ServiceStub_p0_s6
Killing 5384:com.unique:vapp0/u0a108 (adj 0): bg anr
```

A caller waiting for a slot re-warms it only when the slot is *not* already grafting —
there is a whole announcement, `slotStarting`, whose stated purpose is to prevent exactly
this. It never fired. `AppBootstrap.announce` begins `val host = hostPackage ?: return`,
and `hostPackage` is set part-way through the graft, after the announcement is made. On a
cold process it was always null, so the announcement was a no-op precisely in the case it
exists for, the caller re-warmed a busy process twice, and ActivityManager killed the
process it was waiting for. Before the graft the context still is UNIQUE's, so its own
package name is both available and correct.

### Putting it on the emulator: five more faults the reading could not have found

The pass above was reasoned from a phone log. Running it produced five faults that no
amount of re-reading would have:

| Found on the emulator | Fixed by |
|---|---|
| `windowHardwareAccelerated=false` **with** `activityInfoFlags=512`. The `ActivityInfo` carried the bit, `Activity.attach` is where the platform reads it, and the window still came up without it | The window is told directly, with `Window.setFlags` from `onActivityPreCreated` — before `setContentView` builds the decor, and recorded as a *forced* flag so `generateLayout` cannot clear it. The `ActivityInfo` keeps the bit as well |
| `metaDataNumber` came back as the resource id rather than `240508`: `LoadedApk.getResources()` is null that early, so a `@integer` reference had nothing to resolve against | The `metaData` bundle is rebuilt from the `Application`'s own resources once `makeApplication` has run |
| `getExternalFilesDir()` threw `SecurityException: callingPackage does not match UID` — the `mount` guard shadows the generic caller-package rewrite, because the first shim that binds to a method wins | The guard carries the rewrite itself, and says why at [`VirtualExternalStorage.shims`] |
| With that fixed, external storage still pointed at `/storage/emulated/0`: `StorageVolume.mPath` is a `File`, and writing a `String` into it is an `IllegalArgumentException` that the surrounding `runCatching` turned into a quiet false | The field's declared type is read and the value built to match; the repoint is now reported (`EXTERNAL_VOLUME_REPOINTED`) rather than only its failure |
| A second launch of a running app produced nothing at all. `FLAG_ACTIVITY_NEW_TASK` onto a task already running the component is `START_DELIVERED_TO_TOP`, and the intent that arrives is UNIQUE's *stub* intent — which `ActivityThread` then assigns to `Activity.mIntent`, so `getIntent()` answers with it for the rest of that activity's life | `NewIntentItem` is rewritten in place, keeping the `ReferrerIntent` the platform reads the referrer from (`t45`) |

Two more came from reading the fourth log once more with the emulator's answers in hand:

| Found | Fixed by |
|---|---|
| An implicit service start scoped to the guest — `new Intent(ACTION).setPackage(getPackageName())`, how a great many SDKs reach their own worker — resolved to nothing, because the platform has no filters for a package it never installed | Resolved against the guest's own manifest, and only when exactly one service matches; more than one is refused rather than guessed, as `bindService` refuses it (`t46`) |
| A Settings screen an app opens about itself named the guest in a `package:` URI, so the device's Settings had nothing to open. In the fourth log a cleaner app sent `MANAGE_APP_ALL_FILES_ACCESS_PERMISSION` and it left the guest for `com.android.settings`, which could only fail | The URI is retargeted to UNIQUE, whose uid is the one that would actually hold the access — so the switch the user is sent to is the switch that works |

And one thing that was not UNIQUE at all, but was making every run after `t36` unreadable:
this emulator's Bluetooth stack cannot finish its own handshake under load
(`AdapterState TURNING_ON : BREDR_START_TIMEOUT`), dies, is restarted by
`BluetoothManagerService`, and from then on `com.android.bluetooth` restarts every twenty
seconds. One run cost 45 process starts and four tests timed out waiting for an app that
was simply not being scheduled. `tools/verify-device.sh` now turns Bluetooth off **on an
emulator only** — a physical device's is the owner's to decide.

### Seven real applications, and the seven faults only they could find

Everything above was found with `tools/testapp`, which is deliberately ordinary and
deliberately *cooperative*: it writes down what it observed, so every assertion about it is
a fact the app itself reported. That is what makes those tests precise, and it is also
their limit — the probe was written after the engine, and it only does what the engine
already supports.

Seven apps from F-Droid were then imported and launched with nothing written for them:
**Termux** (0.119.0-beta.3, 115 MB, native, a foreground service), **Fossify Gallery**
(1.13.1, twelve native libraries, a home-screen widget), **NewPipe** (0.29.1),
**Shattered Pixel Dungeon** (3.3.8, a real game), **AntennaPod** (3.12.0), **KeePassDX**
(4.5.2, native crypto and its own keyboard) and **Aegis** (3.4.2). Four of the seven
failed, each in a different way, and none of the seven causes was reachable from the
probe:

| Found | Fixed by |
|---|---|
| **Termux died in `TermuxActivity.onStart`**: `SecurityException: com.unique: One of RECEIVER_EXPORTED or RECEIVER_NOT_EXPORTED should be specified`. That rule is a compat change and `ActivityManagerService` evaluates it against the **calling uid**, which is UNIQUE's — so an app built against Android 9, using the two-argument `registerReceiver` it has always used, is held to UNIQUE's target SDK | `registerReceiver*` supplies `RECEIVER_EXPORTED` when the guest passed neither flag and the guest's own target SDK predates the rule. Exported rather than not-exported, because that is what the platform itself does for a pre-34 app |
| **Fossify Gallery died in `MainActivity.onCreate`**: `SecurityException: Package org.fossify.gallery does not belong to 10108`, from `AppWidgetManager.getAppWidgetIds`. `appwidget` was not proxied — and proxying it alone changed nothing, because the package it checks is inside a **`ComponentName`**, which the generic caller rewrite does not touch | `appwidget` added to the proxied services, with a guard that rewrites `ComponentName` packages as well as strings. The guest then gets an empty array, which is the truth: its widget provider is not installed, so no home screen can be showing one |
| **Termux died again, deeper**: `IllegalArgumentException: com.termux: Targeting S+ … requires that one of FLAG_IMMUTABLE or FLAG_MUTABLE be specified`. Same shape, different enforcement: this one is checked **inside the app's own process**, by the compat config `ActivityThread` installs at bind time — which is UNIQUE's, because the process was bound as UNIQUE | `GuestCompatChanges` disables, for that process, the changes the guest's own target SDK predates. The change *ids* are read from the platform classes that declare them rather than transcribed; what UNIQUE states is the SDK each is gated at, which `@EnabledAfter` does not keep at runtime |
| **Termux died a third time**: `SecurityException: Starting FGS with type microphone`. `startForeground(id, notification)` — every app written before Android 10 — sends `FOREGROUND_SERVICE_TYPE_MANIFEST`, meaning "the type on this service's manifest entry". The entry `ActivityManagerService` reads is the **stub's**, which declares every type UNIQUE can host, so the two-argument call asked for all of them at once: `requested=0xffffffff … granted=0x400008ff` | The manifest type is resolved against the *guest's* own entry for the service that is starting. A guest that declares none — an app older than the attribute — gets `specialUse`, which is the type that exists for work the taxonomy does not name and the one the host manifest already carries a `PROPERTY_SPECIAL_USE_FGS_SUBTYPE` for |
| **KeePassDX died in `FileDatabaseSelectActivity.onCreate`**: `SecurityException: Attempt to change component state`, from `setComponentEnabledSetting` on its own keyboard service. An app turning one of *its own* components on or off is ordinary — an `<activity-alias>` enabled and its sibling disabled is how a launcher icon is changed — and `PackageManagerService` checks the component against the calling uid, so there was nothing for it to store and no way for it to agree | `GuestComponentState`, a properties file beside the instance's permission record. `get*EnabledSetting` answers from it, `ComponentInfo.enabled` carries it, and both intent resolvers filter on it — so a disabled alias really does stop matching |
| **Aegis's graft failed with `message=java.lang.reflect.InvocationTargetException`** and nothing else. The guest's `Application.onCreate` is called reflectively, so anything it throws arrives wrapped, and that one line ended the investigation instead of starting it | The chain is unwrapped to its root and the first frames carried, and the stack goes to `logcat` under the same tag a device capture is filtered on. The next run said `SecurityException: Shortcut package name mismatch` and named the call |
| **Which was the real fault**: `ShortcutInfo` carries its own package name and `ShortcutService` checks each one against the caller, so the caller-package rewrite was not enough. Rewriting it would then fail the next check — the shortcut's activity must be a *main* activity of that package, and UNIQUE's stubs are neither exported nor launcher-filtered | Publishing is refused, with `false` — a value `setDynamicShortcuts` already returns when the caller is rate-limited, so the app sees a documented outcome rather than an exception. Reading is left alone and answers with UNIQUE's own, which is empty and true. Making them work means routing each shortcut's intents onto stubs the way `PendingIntent`s are; that is a feature, not a fix |

After those seven, **all seven run**: each reaches its own main activity, on the hardware
renderer, and stays up. `tools/real-app-smoke.sh` is what does this, and it is deliberately
*not* part of the acceptance suite — an APK downloaded at run time can change under the
test, and a failure would then be a fact about F-Droid rather than about UNIQUE. It is a
report a person reads:

```
== real-app smoke ==
  PASS  com.beemdevelopment.aegis                  ACTIVITY_HARDWARE_ACCELERATED … applied=true
  PASS  com.kunzisoft.keepass.libre                ACTIVITY_HARDWARE_ACCELERATED … applied=true
  PASS  com.shatteredpixel.shatteredpixeldungeon   ACTIVITY_HARDWARE_ACCELERATED … applied=true
  PASS  com.termux                                 ACTIVITY_HARDWARE_ACCELERATED … applied=true
  PASS  de.danoeh.antennapod                       ACTIVITY_HARDWARE_ACCELERATED … applied=true
  PASS  org.fossify.gallery                        ACTIVITY_HARDWARE_ACCELERATED … applied=true
  PASS  org.schabi.newpipe                         ACTIVITY_HARDWARE_ACCELERATED … applied=true
```

Re-run against everything in this pass — the hidden Google stack, the `input_method` hook
and the provider reordering included — and all seven still reach their own main activity on
the hardware renderer.

What it does *not* say is that the apps are usable: nothing here looks at the screen, and
"reached its own main activity and stayed up for five minutes" is the whole claim.

### The fifth run: everything launched, and Play services killed three of them

The log that came back from the phone after the pass above is the first one in which the
engine is not the story. Eight launches, six of which reached the guest's own Activity, and
the report was: *"some apps seem to launch, some don't, in some the screen does not
respond, games crash and there is no keyboard."*

`tools/device-log/analyze.py` on that capture, which is checked in as
`tools/device-log/fixtures/redmi-android15-run5.log`:

```
[FAIL] launch       6/8 launches reached the guest's Activity
[FAIL] crash        com.gordey.standarling crashed on main: SecurityException: Unknown calling package name
                    com.a0soft.gphone.acc.free crashed on main: SecurityException: Unknown calling package name
                    com.Chillow.CustomRise crashed on main: SecurityException: Unknown calling package name
[FAIL] providers    androidx.core.content.FileProvider did not publish
[FAIL] permissions  android.permission.SYSTEM_ALERT_WINDOW denied 3x, but it is an install-time permission
[ok  ] render       (the fourth run's fault, and it stayed fixed)
```

| Found | Fixed by |
|---|---|
| **Three guests died of one thing.** `GmsClient.getRemoteService` sends `context.getPackageName()` to `com.google.android.gms`, which resolves the *calling* uid — UNIQUE's — and answers `SecurityException: Unknown calling package name '<the guest>'`. It arrives on a `Handler`, so it is fatal and uncatchable, and it fired seconds after each app reached its own screen. Every one of the three stacks is Firebase's `dynamite_measurementdynamite` module | `com.google.android.gms` and `com.google.android.gsf` are hidden from a guest: absent from `getPackageInfo`, `getApplicationInfo`, `getPackageUid`, `resolveContentProvider`, the installed list and both intent resolvers. An SDK that asks finds nothing and takes the path it already has for a phone without Play services. `com.android.vending` stays visible — Play's licence check is an ordinary bind that works, and hiding it would break the PAIRIP case fixed one run earlier |
| **No guest has ever had a keyboard.** `EditorInfo.packageName` is the app's own, and `InputMethodManagerService.startInputOrWindowGainedFocus` checks it against the calling uid: a mismatch is `InputBindResult.INVALID_PACKAGE_NAME`, and *no IME is bound*. Nothing is logged in the app's process — the field takes focus, shows a caret, and accepts nothing | `input_method` proxied, with a guard that rewrites the `EditorInfo`'s own field rather than an argument. Two `$Stub` spellings, because the interface moved from `com.android.internal.view` to `com.android.internal.inputmethod` in Android 14, and two singleton caches for the same reason |
| **A guest came up with no `FileProvider`.** `ContentProvider.attachInfo` runs the app's own code, and `FileProvider`'s reads external storage while parsing its `<paths>`. The registry installed providers *before* the identity hooks, so that call went out under the guest's name: `PROVIDER_PUBLISH_FAILED … SecurityException: callingPackage does not match UID`. `FileProvider` is how an app shares a file with anything outside itself | Providers are published after `VirtualIdentityHooks`. They still precede every *guest* component and its `Application.onCreate`, which is the ordering apps rely on; what changed is that they no longer precede UNIQUE's own plumbing |
| **"In some apps the screen does not respond."** Touch was never broken. `ACTION_DOWN` and `ACTION_UP` reach every guest that is alive, in the log, for every app. The unresponsive screens are the windows left behind by the three crashes above — still drawn, with no process behind them | Nothing, directly. Fixing the crashes is the fix |
| **`SYSTEM_ALERT_WINDOW` denied three times**, and it is an install-time permission: no dialog exists for it, so no user could have refused it. UNIQUE did not declare it, and Android's overlay screen does not list an app that does not | Declared — which grants nothing by itself; `SYSTEM_ALERT_WINDOW` is granted on the Settings screen, and the declaration is what makes UNIQUE appear on it. App Details now lists it, with exact alarms and unrestricted background work, each with its real state and the screen that grants it |
| **`bin.mt.plus` never reached its `Application`**: `UnsatisfiedLinkError: No implementation found for void l.ۢ.<clinit>()`, thrown by its own packer's static initialiser before UNIQUE's graft could finish. Unexplained | Nothing. It is reported as `NO_APPLICATION` and the analyzer's fifth-run tests assert that it keeps being reported. A commercial packer that decrypts its own classes in a `<clinit>` is a class of app UNIQUE does not claim |

And what the same message asked for that was not a fault:

- **The *Advanced* section is gone** — the device test, the checklist, the diagnostics
  screen, the export, and the engine code behind all four. §14.2 of ARCHITECTURE.md says
  what went and what survives it.
- **Everything the interface shows is translated.** Engine failures carry a code the
  interface translates, with the engine's English prose as the fallback for a code it does
  not know; an automatic profile name is written in the reader's own language rather than
  stored in English. `tools/check-translations.py` fails when a code has no sentence in
  both languages, or a sentence has no code.

Two of the three engine fixes cannot be *observed* on the verification emulator: it is an
AOSP image with no Play services, so the crash they prevent cannot be reproduced there, and
it has no IME of its own to bind. What the emulator does say about the keyboard is that the
proxy installs and the guard binds to exactly the method that matters:

```
HOOK SERVICE_HOOKED service=input_method cache=true singletons=1
     bound=callerPackage,editorIdentity
     matched=… editorIdentity=startInputOrWindowGainedFocus
HOOK IDENTITY_HOOKS_INSTALLED package=com.unique.probe
     installed=…,appwidget,input_method skipped=
```

The `com.android.internal.inputmethod.IInputMethodManagerGlobalInvoker` singleton is
reported skipped on this image and the `android.view.inputmethod` one patched, which is
what the two spellings are there for. The hiding decision is unit-tested instead
(`VirtualPackageManagerHookTest`, six tests), and both rows say `NOT_TESTED` on ARM64 in
`docs/COMPATIBILITY.md` until a phone says otherwise.

### The sixth run: everything launched, and the games had no assets

The log from the phone after that pass is `tools/device-log/fixtures/redmi-android15-run6.log`.
Seven launches, six of which reached the guest's own Activity, and the report was that
apps start and then behave as though they had been installed wrong — a game that loads
into a menu with nothing in it, another that dies a few seconds in.

The analyzer, with the two checks this run added:

```
[FAIL] launch       6/7 launches reached the guest's Activity
                    bin.mt.plus u0 -> bin.mt.plus.MainNoBgIcon: NO_APPLICATION
[FAIL] crash        com.a0soft.gphone.acc.free crashed on GoogleApiHandler:
                    SecurityException: Package com.a0soft.gphone.acc.free is not owned by uid 10303
[FAIL] platform     NotificationManager.getNotificationChannel was refused
[FAIL] permissions  READ_EXTERNAL_STORAGE was refused to the guest 12x because UNIQUE
                    does not hold it — not a decision any user could have made or can undo
[FAIL] storage      com.axlebolt.standoff2 skipped its own expansion files for lack of
                    READ_EXTERNAL_STORAGE
[FAIL] native       com.gordey.standarling died on a native signal after UNIQUE hooked
                    libgrave.so
```

| Found | Fixed by |
|---|---|
| **A game cannot see its own OBB.** `HOST_PERMISSION_REFUSED group=FILES permissions=READ_EXTERNAL_STORAGE,WRITE_EXTERNAL_STORAGE permanent=true`, and then `I Unity: No permission to read external storage. Skipping OBB loading.` 156 times. `permanent=true` is not a phone that needs its settings changed: since Android 13 the platform *auto-denies* both permissions to any app targeting 33 or later — no dialog, no toggle — and UNIQUE targets 36. So `blockedByHost` could never clear, and every Unity game asked once a frame and skipped its own assets every time | `PlatformPermissions.SELF_SERVED`. A guest's external storage is a directory inside UNIQUE's own `filesDir`; reading it needs no permission from anyone, so the host intersection does not apply to `READ_EXTERNAL_STORAGE`, `WRITE_EXTERNAL_STORAGE` or `MANAGE_EXTERNAL_STORAGE`. They answer GRANTED unless the *user* denied this instance its files, and a request the platform refuses is corrected to GRANTED before the guest's callback sees it. Media permissions are deliberately *not* in the set: they reach the device's real `MediaStore`, which is the user's photo library and not the instance's directory |
| **And then the directory was empty.** The permission was only half of it. `getObbDir()` correctly names `users/<vuid>/sdcard/Android/obb/<pkg>`, and nothing had ever put the game's 3 GB of expansion files there | `GuestAssetImport`, run after every import and clone and available from the UI. `Android/obb/<pkg>` is copied from the device's own external storage; `Android/data/<pkg>` only when asked for, because it is live app state and copying it makes two saves that immediately diverge. `.obb` files picked beside the APK are separated out of the import and go straight to the instance. Every outcome is one of four named results, so "could not read the source" never looks like "there was nothing to copy". **The all-files access this row went on to ask for cannot work — see the ninth run.** |
| **UNIQUE's own PLT hook killed a game.** `io_redirect: hooked 22 new slot(s) after loading …/libgrave.so`, and six seconds later `SIGBUS`, `BUS_ADRALN`, at `pc 0x…9f7` inside an anonymous page — a jump to a value that was never a function. `libgrave.so` is a code-virtualization protector: it checks its own relocations and executes generated code out of anonymous pages. The tombstone names the game's `libunity.so` and never names UNIQUE | `plt::hook_all` takes an exclusion list, `GuestNativeExclusions` holds the built-in one with the run that put each entry there, and `runtime/native/<vuid>/<package>.exclude` lets a protector UNIQUE has not met yet be named without a new build. Every exclusion that applied is printed by the native layer, so "not hooked on purpose" is never confused with "the scan missed it". The cost is stated rather than hidden: an excluded library's hard-coded `/data/data/<pkg>` paths are not rewritten |
| **A fatal `SecurityException` nine milliseconds after the hook that would have prevented it.** `PROVIDERS_PUBLISHED` at `14:41:34.450`, `SERVICE_HOOKED service=notification` at `.459`, `FATAL EXCEPTION: GoogleApiHandler … Package com.a0soft.gphone.acc.free is not owned by uid 10303 at INotificationManager$Stub$Proxy.getNotificationChannel` at `.471`. A provider's `attachInfo` runs real app code; it started a thread, and the thread asked for a notification channel under the guest's own name | The job, notification, receiver and activity-lifecycle hooks are installed **before** `VirtualProviderRegistry`. The rule the fifth run established — providers after the identity hooks — was right and did not go far enough: the first line of *guest* code is a provider's `attachInfo`, not `Application.onCreate`, and a hook installed after guest code has run is a hook that was not installed |
| **WorkManager and Play services could not schedule a job.** `IllegalArgumentException: uid 10303 cannot schedule job in com.openai.chatgpt`, from `JobSchedulerImpl.schedule` — with `HOOK SERVICE_HOOKED service=jobscheduler cache=true singletons=0` in the same log. `singletons=0` is the tell: `JobScheduler` keeps its binder in an ordinary *instance* field, so no named static could reach it, and WorkManager and Firebase both initialise from a `ContentProvider` — which then ran before the hook | Ordering, as above, and a third step in `SystemServiceHook`: every manager object this process has already built is re-pointed at the shim, matched by declared field *type* so a manager for another service can never be touched. `StaticServiceFetcher`'s process-wide instance and every `ContextImpl.mServiceCache` are both walked, because which of the two a service uses has changed between releases |
| **Push notifications could not bind.** `SERVICE_INTENT_IMPLICIT action=com.google.firebase.MESSAGING_EVENT matches=2`, then `SecurityException: Not allowed to bind to service Intent { act=com.google.firebase.MESSAGING_EVENT pkg=com.axlebolt.standoff2 }`. Two matches is the *normal* shape for FCM — the SDK's own service and the app's subclass — and refusing to choose sent the bind out to a platform that has never installed the guest | An intent the caller scoped to the guest with `setPackage()` resolves to the best match, which is what `PackageManagerService.resolveService` does: highest filter priority, then first declared. A *bare* implicit intent with several matches is still refused, because there the platform refuses too |
| **26 broadcasts that could never arrive.** `PENDING_INTENT_RECEIVER_UNSUPPORTED receiver=com.google.android.gms.measurement.AppMeasurementReceiver`, once for each time an SDK re-armed a timer. A guest's manifest receiver exists only as a dynamic registration inside the live process, and a dynamic receiver is matched by filter, so an explicit broadcast had nothing to be pointed at | `com.unique.app.runtime.BroadcastStub`, in UNIQUE's **main** process — which is the point, because a `PendingIntent` fires when there may be no `:vappN` at all. The guest's receiver class and the whole original intent travel as extras, and delivery is the cold-broadcast path that already leases the right slot and retries until the receiver acknowledges |
| **All-files access is not offered.** The manifest said it "would not help: a guest's external storage is redirected into its own instance directory" | **The manifest was right and this row was wrong.** `MANAGE_EXTERNAL_STORAGE` does not cover `Android/data` or `Android/obb`, so it cannot reach a game's expansion files whatever it is granted for. It stays declared and listed, because it does widen the *rest* of shared storage, but nothing in UNIQUE asks for it on an app's behalf any more and no message tells a user it will fix their game. The ninth run is where a user granted it and reported that nothing changed |
| **A warning that was always wrong.** `SINGLETON_PATCH_SKIPPED service=input_method field=com.android.internal.inputmethod.IInputMethodManagerGlobalInvoker.sServiceCache`, eight times, next to `singletons=1` saying the keyboard hook had worked. The two entries are alternative spellings of one singleton — the interface moved package in Android 14 — so exactly one resolving is the healthy outcome | Warned only when *none* of a target's spellings resolved, which is the case that means something |

Two things this run found are **not** fixed, and are recorded rather than worked around:

- **`bin.mt.plus` still never reaches its `Application`.** `UnsatisfiedLinkError: No
  implementation found for void l.ۢ.<clinit>()`. Its packer loads `libmtprotect.so`
  successfully — the load is in the log — and then the natives it should have registered
  are not there. A commercial packer that decrypts its own classes through native
  `<clinit>` methods and inspects its environment while doing it is a class of app UNIQUE
  does not claim, and the analyzer asserts that it keeps being reported as
  `NO_APPLICATION` rather than passing quietly.
- **A browser OAuth redirect cannot come back into a guest.** See below.

### The seventh run: nothing broke a launch, and Google was the whole story

`tools/device-log/fixtures/redmi-android15-run7.log`. Three launches, three into the
guest's own Activity, no refused platform call, no poisoned slot — the first log in which
the engine is not the story at all. What the user reported instead:

> the games still do not see their resources, UNIQUE asks me to install Google services
> when they are already installed, one game says it cannot see them, and signing into
> ChatGPT with Google fails after the account picker

Every one of those is in the log, and three of the four are one decision.

| Found | Fixed by |
|---|---|
| **"UNIQUE asks me to install Google services."** `GOOGLE_STACK_HIDDEN` four times, then `W GooglePlayServicesUtil: com.axlebolt.standoff2 requires Google Play services, but they are missing` — on a phone with Play services 26.32.34 installed and enabled, which UNIQUE's own `GOOGLE_ENVIRONMENT` line records two seconds earlier. The hiding was unconditional, and it is a lie about the device that costs more than a message: `DynamiteModule` loads Google's own code through a provider, `AdvertisingIdClient` binds a service that never checks the caller, and `emoji2` disables itself when the package is absent | The hiding is decided per guest, from the guest's own manifest. What it was protecting against is one call — `GmsClient.getRemoteService` — and two logs say exactly who survives it. `play-services-basement 17.4.0` let the refusal reach the app's main looper and died; the 18.x line catches it, logs `Failed to get service from broker` and reports a `ConnectionResult`, which is the same path the SDK takes on a phone with no Google stack. The first attempt read which one an app links from its `com.google.android.gms.version` meta-data — **and that was wrong; the eighth run below is the log that says so.** What replaced it is measured rather than guessed: visible to everyone, hidden from an instance only after that instance has died of the refusal. `GoogleStackVisibility`, nine tests |
| **The refusal was never prevented by hiding anyway.** With Play services hidden, `E GoogleApiManager: Unknown calling package name 'com.axlebolt.standoff2'` still appears: the SDK binds by explicit intent and never asks the package manager first. So the hiding bought silence, not safety | Nothing to fix — recorded because it is the fact that makes the row above safe. Hiding changes what an app is *told*, not what it does |
| **"The games still do not see their resources."** `GUEST_OBB_IMPORT outcome=SOURCE_UNREADABLE … /storage/emulated/0/Android/obb is not readable by UNIQUE`, for all three apps. The import worked and the directory is guarded: since Android 11 `Android/obb` needs all-files access, which had not been granted — and nothing had asked for it, because the import only ran at *import* time and its failure went to a log file | The import runs on every launch, and it is cheap to repeat: a file whose name and length already match is skipped. The banner and snackbar this row added asked for all-files access, **which cannot reach `Android/obb` and was shown for every app whether or not it had any**; both were removed in the ninth run and replaced by the instance's file browser. Instances created by an earlier build get whatever is visible on their next launch instead of needing to be deleted and re-imported |
| **Signing into ChatGPT with Google.** The log shows what happened: `SERVICE_INTENT_CROSS_APP action=android.support.customtabs.action.CustomTabsService`, then `ACTIVITY_IMPLICIT_LEFT_GUEST action=VIEW data=https handledByHost=com.android.chrome`. Play services was hidden, so ChatGPT fell back to the browser — and the account picker the user saw was Google's *web* chooser in Chrome. The redirect back cannot arrive, for the reason the sixth run settled | Partly. With Play services visible ChatGPT takes its native path instead of the browser, which is the flow the user asked for. What that path returns is a separate and unsolved thing: Play services resolves the caller to UNIQUE, so a token comes back for UNIQUE's identity and not the app's. Only Play services *inside the space* can answer that, and this build does not have it |

And one correction, which matters more than any of the rows above.

**The sixth run's native finding was wrong.** It read: UNIQUE patched 22 GOT slots in a
Unity game's `libgrave.so`, and six seconds later the process died — therefore the hook
killed it. The seventh run excluded `libgrave.so`, and the same game died the same way:

```
run 6   #00 pc …9f7  <anonymous:0000007dd3321000>
run 7   #00 pc …9f7  /memfd:gralloc_shared_memory (deleted)      libgrave.so excluded
```

Same signal, same code, the same offset into the page. The pairing was an ordering
coincidence. What both logs actually support is narrower and is what the analyzer says
now: a jump to an address that is not instruction-aligned, into a page that is not a
library — a pointer that was already wrong before the jump, landing wherever the
allocator had got to. The exclusion stays as hardening, and `GuestNativeExclusions` says
in as many words that it is not a fix.

Two things came out of taking that seriously:

- `plt_hook` no longer takes the original function out of the GOT slot it overwrites. A
  lazily-bound entry holds the resolver stub rather than the function, and a trampoline
  that calls a resolver stub with the wrong arguments jumps somewhere that is not a
  function at all. `dlsym(RTLD_DEFAULT, symbol)` asks the loader the question the
  relocation asked and cannot answer with a stub. The NDK links with `-z now`, so the
  case is rare — which is what makes it the shape of bug that surfaces once, on one
  device, minutes into a game.
- The analyzer reports a wild jump as itself instead of attributing it to whichever
  library loaded last, because that attribution sent one investigation the wrong way and
  would have sent the next one the same way.

### The eighth run: the version rule was wrong, and the log said so at once

`tools/device-log/fixtures/redmi-android15-run8.log`. The user's whole report was two
words — *"nothing changed"* — with a screenshot of a notification:

> **Установите сервисы Google Play** — Чтобы и дальше пользоваться приложением
> «1Tap Cleaner», установите сервисы Google Play

The build was installed and running; the new code was in the log. It was simply wrong:

```
GOOGLE_ENVIRONMENT gmsPresent=true gmsEnabled=true gmsVersionCode=263234035
                   gmsVersionName=26.32.34
GOOGLE_STACK_HIDDEN hidden=true reason=SDK_TOO_OLD gmsVersion=12451000     (x3)
W GooglePlayServicesUtil: com.openai.chatgpt requires Google Play services, but they
    are missing.
```

`12451000`, three times, for 1Tap Cleaner, ChatGPT and a Unity game. Three unrelated apps
with three unrelated release cadences do not ship the same client library, and ChatGPT
certainly does not ship a 2018 one. `com.google.android.gms.version` is the **minimum
GmsCore version the client requires**, not the version of the client — Google stopped
moving it years ago, so it is the same constant in nearly every APK on the phone. Nothing
built on it could ever have distinguished a 17.x client from an 18.x one, and the rule I
shipped therefore hid Play services from *every* guest. The previous build hid it from
every guest unconditionally; this one hid it from every guest with a reason attached. From
the user's side those are the same build, which is exactly what they said.

**What replaced it: measure instead of predict.**

Play services is visible to every guest. There is one call that kills an old client —
`GmsClient.getRemoteService`, refused with `Unknown calling package name` — and if a guest
dies of it, `CrashGuard`'s observer writes one word into that instance's own file before
the process goes, and the next launch of *that instance* hides the stack. The cost is one
crash, once, for an app whose client library is old enough to die of it. The alternative,
which is what shipped, was every app on a fully Googled phone being told the phone has no
Google.

| Found | Fixed by |
|---|---|
| **Play services hidden from all three guests on a phone running GmsCore 26.32.34.** The rule read a constant and treated it as a version | Visible by default; hidden per instance only after that instance has died of the refusal, recorded by the crash handler because a dying process is the last thing that can write the file. `HIDE`/`SHOW` in `runtime/google/<vuid>/<pkg>.visibility` override both, so an instance can always be tried again. `GoogleStackVisibility`, nine tests |
| **Nothing in sixteen checks noticed a build that lied to every app about the device.** Every launch passed; the fault was in what the apps were told afterwards | A `google` check that pairs `gmsPresent=true` with a guest being told otherwise — and reads the app's own `requires Google Play services, but they are missing` as well as UNIQUE's event, so a build that stops logging the event is still caught. Asserted against this fixture and against three synthetic runs, including the two hides that are *legitimate* |
| **The expansion files were still unreadable**, for the third run running: `GUEST_OBB_IMPORT outcome=SOURCE_UNREADABLE` at every launch. The import runs, the directory is closed. All-files access had still not been granted, and the request for it was a snackbar on a screen the user had already left | A `NoticeBanner` on the app's own details screen, present for as long as the condition is, with an **Allow** button that opens the all-files settings page directly. A snackbar is for something that just happened; this is a thing that stays true until someone acts on it |

Worth stating plainly, because it is the second correction in three runs: both wrong
answers had the same shape. A number that looked like a version, and a library that
loaded just before a crash — each was a plausible story told from one coincidence, and
each survived until a physical device contradicted it. The analyzer now has a check for
both, which is the only part of this that generalises.

### The ninth run: the bill for making Google visible, and a message that was never true

`tools/device-log/fixtures/redmi-android15-run9.log`. The user's report, in their own
order: stop adding a screen for every fault, Google sign-in still does not work, apps
still do not see Play services, some apps still crash, **all-files access was granted and
nothing changed**, and — the one that gave the game away — *"why does it say OBB is needed
for every app, including ones that do not even have it"*.

Every one of those is in the log, and two of them are my mistakes rather than the
platform's.

| Found | Fixed by |
|---|---|
| **Three apps killed by the refusal that hiding used to prevent.** `SecurityException: Unknown calling package name` on `Looper.loop`, for a Unity game, Standoff 2 and 1Tap Cleaner. In the same run ChatGPT hit the *same refusal ten times* and lived: `E GoogleApiManager: Failed to get service from broker`. The difference is which `play-services-basement` the app ships — 17.4.0 delivers it to the app's own looper, the 18.x line catches it and reports a `ConnectionResult` | `GuestLooperGuard`: the guest's main loop is wrapped, and one exception is caught — that sentence, from that API — while everything else goes to the uncaught handler unchanged. It reproduces what the newer library already does, so an old client ends up on the path its own newer sibling takes: the bind fails, the app is told, nothing else is lost. Hiding stays as the fallback for an app that hits the refusal in a loop, past 24 in one launch. Six tests, most of them about what it refuses to catch |
| **"It says OBB is needed for apps that do not have OBB."** Correct, and the fault is mine. `GuestAssetImport` decided the files were present-and-blocked whenever the OBB directory could not be *seen*, by asking whether its parent was readable. Since Android 11 that parent is never readable, so every package matched: the log carries the message for `com.openai.chatgpt`, `bin.mt.plus` and `com.a0soft.gphone.acc.free` — a chat app, a file manager and a cleaner | Nothing is inferred any more. An invisible source is `NOTHING_TO_IMPORT` and is silent, because there is no test from inside an app that separates "no expansion files" from "expansion files behind the platform's filter". `GuestAssetImport.invisibleSourceOutcome`, named and tested precisely because it is one line and the wrong line was also one line |
| **"I granted all-files access and nothing changed."** It could not have. **`MANAGE_EXTERNAL_STORAGE` explicitly does not cover `Android/data` or `Android/obb`** — the platform filters those two subtrees out below the permission check, so an app holding All-files access sees exactly what an app holding nothing sees. I told the user to grant it. That advice was wrong in the engine, in the interface, in the analyzer and in `dist/README.md` | The advice is gone from all four. What replaces it is the route that works: the user hands UNIQUE the file |
| **"Stop adding a new screen for every error."** Fair. The last pass answered an unreadable directory with a banner on App Details and a ten-second snackbar after every launch — for a condition that, per the row above, was reported for every app whether or not it was true | Both removed, and `EngineOutcome.warning` with them. What is left in their place is one row in the Storage section, reading **Files**, which is not a warning about anything |
| **A file manager, which is what was actually needed.** The user asked for it directly: the virtual space's own folders and files, like on a normal device, with an import button for putting a file into a game's `data` or `obb` | `FilesScreen` over `FileBrowser`. It shows the two trees a guest has, under the guest's own names — `/data/data/<pkg>` and `/sdcard` — so a user following a game's instructions finds `Android/obb/<package>` where the game says it is. **Import** opens the system picker and copies in, multi-select, through a `content://` stream so a file in Drive or on a removable volume works the same as one in Downloads. Sixteen tests, and most of them are about the boundary: `..`, an absolute path, another package's directory and a symlink planted in the instance all resolve to null |

**Google sign-in is still not solved, and this run does not change that.** With the stack
visible ChatGPT takes its native path, Play services resolves the caller to UNIQUE, and a
token comes back for UNIQUE's identity rather than the app's. In-space Play services is
still the only architecture that answers it, and it is still not built.

### Making Play services actually work, and the exact point where it stops

Two asks: *"сделай нормальный полноценный гугл сервисы и гугл вход"*, and put the file
manager on the home screen rather than inside an app's settings. The second is done. The
first splits cleanly in two, and the split is not a matter of effort.

**The one refusal, and how it is answered now.** Every Play services client, for every
API, starts the same way:

```java
GetServiceRequest request = new GetServiceRequest(...);
request.callingPackage = context.getPackageName();
broker.getService(callbacks, request);
```

`com.google.android.gms` takes `Binder.getCallingUid()`, asks the package manager which
packages that uid owns, and refuses if the request's calling package is not among them.
Inside UNIQUE the uid is always UNIQUE's, so this failed for **every** guest and every
Google API — and every Google-shaped failure in this project was behind it.

The uid cannot be changed; it is the kernel's, checked in another process. The *name* in
the request can be, and `com.unique` genuinely is a package that uid owns. So the request
is rewritten in flight: `GmsBrokerBinder` wraps the broker at the moment the guest first
holds it, and rewrites the calling package inside the marshalled `GetServiceRequest`.

Rebuilding rather than patching, because `Parcel.marshall()` refuses any parcel holding a
binder and this one holds the client's callbacks. `Parcel.appendFrom` carries binder
references across, so every byte except the replaced string is copied verbatim. The
`SafeParcelWriter` layout is walked without knowing one field number — the field is found
**by value**, as the one whose contents decode to exactly the guest's package name,
because the index is Google's private business and changes between versions while the
value never does. Any inconsistency abandons the rewrite and forwards what the app wrote,
so the worst case is the refusal that was happening anyway.

**Where it stops, and why no amount of work moves it.** Play services now believes the
caller is `com.unique`, because that is the truth about the uid. Anything that is *about
which app this is* is therefore answered for UNIQUE. Google Sign-In identifies an app by
package name **and signing certificate**, checked against the OAuth client in the app
developer's own Google Cloud project. A request arriving as `com.unique` matches no such
client, and Google answers `DEVELOPER_ERROR`.

That is not a prediction. It is in the ninth run's log, from the user's own phone:

```
W GoogleApiManager: connectionResult is not user-facing:
    ConnectionResult{statusCode=DEVELOPER_ERROR, resolution=null}
```

To present the app's own identity, the call has to *originate* from a uid that owns the
app's package and be signed by the app's certificate. Inside a virtual space that means
implementing Play services in the space (`GoogleMode.VIRTUAL_GMS`), which is a GmsCore
reimplementation — and one that still needs system-level signature spoofing to satisfy
the certificate half. Root or a patched ROM. It is a real project and it is not this one.

| Found | Fixed by |
|---|---|
| **Play services refused every guest, for every API.** One call, `GmsClient.getRemoteService`, and behind it Maps, Firebase, ads, the advertising ID, Dynamite modules, analytics and FCM | `GmsBrokerBinder` + `SafeParcelRewrite`. The broker is wrapped only when the bind is to `com.google.android.gms` — checked before the interface descriptor, because reading a descriptor from a remote binder is a synchronous transaction on the app's main thread. Eleven tests over the parcel walk, all but one about what it *refuses* to touch |
| **Google sign-in.** `DEVELOPER_ERROR` in the ninth run, on the user's own device | Not fixed, and now stated exactly rather than as "requires in-space GMS": the bind succeeds, the identity check does not, and the reason is a signature check made by another process against a record UNIQUE has no part in. The analyzer names it when it sees it, so it stops looking like a bug in the app |
| **The file manager was inside an app's settings.** It is the virtual device's file manager, not one app's | It is a built-in app on the home screen, first in the grid, with no *Remove* — deleting the only thing that can put a file into the space would lock the user out of it. Opened there it starts at the list of apps; opened from an app it starts inside that app. Same screen, same tree, two heights |

### The tenth run: the rewrite works, and it uncovered three things behind it

The calling-package rewrite went in and did its job: **17 binds wrapped, 17 requests sent
under UNIQUE's own name**, and the expansion-file check green for the first time in five
runs. The user's report — *"Google sign-in still does not work in apps and games, and
Standoff 2 asks me to install Play services; you did not finish"* — is fair, and the log
says exactly why. Three separate things were behind the one that was fixed.

| Found | Fixed by |
|---|---|
| **Firebase Analytics was still refused.** The wrap allowed only `IGmsServiceBroker`, on the reasoning that it is the interface `getRemoteService` uses. Analytics does not use it: it binds `AppMeasurementService` directly and sends the package inside an `AppMetadata`. So the broker was wrapped seventeen times and `E FA: Task exception while flushing queue: SecurityException: Unknown calling package name` appeared anyway | The gate is now the actual condition — *is this a bind to Play services* — rather than a list of interfaces to be extended once per discovery. The rewrite decides for itself whether there is anything to do: it only replaces a field whose value is exactly the guest's package, and leaves the parcel untouched otherwise, so a request carrying no calling package passes through whatever interface it belongs to |
| **Google Sign-In got further than it ever had, and died in UNIQUE's own plumbing.** Not at Google — `SignInHubActivity` launched *inside the guest* and crashed on its first line: `BadParcelableException: ClassNotFoundException when unmarshalling: com.google.android.gms.auth.api.signin.internal.SignInConfiguration`, from `Bundle.getParcelable` in `onCreate`. The class is in the guest's **own APK**. An `Intent` crosses a process boundary as a parcelled `Bundle` that is not read until something asks, and whatever loader it carries then has to find the class — inside a `:vappN` that was UNIQUE's, which knows nothing about the guest | `LaunchInterceptor.adoptGuestClassLoader`, on every intent UNIQUE hands a guest and on every activity result. Not a Google fix: **any** app passing its own `Parcelable` through an `Intent` hits this, and this is the first flow that happened to prove it |
| **Standoff 2 asked for Play services to be installed — from a build in which Google worked.** Its instance still carried the automatic-hide mark written when it crashed under the *previous* build. A recorded hide is a statement about what happened under a particular mechanism, and it outlived the mechanism | Marks carry the generation that wrote them (`GoogleStackVisibility.IDENTITY_GENERATION`). A mark from an older one is discarded *and deleted* — ignoring it alone would mean re-reading and re-discarding it on every launch for the life of the instance. A hand-written `hide` or `show` is a person's decision, not evidence about a build, so it never goes stale |
| **`Attempt to read from protected data in Parcel` ×6 before every rewrite.** UNIQUE's own noise: probing each field for the package name sometimes read at a position holding a binder or a descriptor. Harmless — the read returns nothing and the walk carries on — and six of them per rewrite in a log a person has to read | A size check first. `Parcel.writeString` uses a length that is exactly predictable, so a field of the wrong size cannot hold the name and is never probed |

And one fix to the analyzer that the fixture forced. The `platform` check looked for the
stack frame naming a refused interface within 30 lines of the exception — but logcat
interleaves every thread into one file, so that was really a window of *how busy the
device was*. It now walks only lines from the thread that raised the exception, which is
what the window was always meant to mean.

**Where Google sign-in stands after this.** The `DEVELOPER_ERROR` in the ninth run is
real and it is the answer for a request that reaches Google as UNIQUE. But the tenth run
shows a flow that never reached Google at all — it died in UNIQUE — so the earlier
conclusion was drawn from one path and stated as though it covered every path. With the
loader fixed, sign-in gets further than it has, and what it does next is a measurement
that has not been taken yet. Which is the answer this project keeps having to re-learn:
the log decides, not the reasoning.

### The eleventh run: a game ran, and a slot was stolen out from under it

The first log in which a virtual app is *used*. Standoff 2 — Unity 6000.3.13f1, IL2CPP,
ARM64, a 105 MB APK and a separate expansion file — launched into its own Activity on the
hardware renderer and stayed there:

```
I Unity: SystemInfo CPU = ARM64 FP ASIMD AES, Cores = 8, Memory = 7707mb
I Unity: Device Model 'Xiaomi 23030RAC7Y', OS 'Android OS 15 (API 35)'
W Unity: [FMOD] loadFromWeb. Path = jar:file:///data/user/0/com.unique/files/virtual/
         users/0/sdcard/Android/obb/com.axlebolt.standoff2/main.203908.…obb!/assets/Master…
D BLASTBufferQueue: [SurfaceView[com.axlebolt.standoff2/…MessagingUnityPlayerActivity]#1]
         acquireNextBufferLocked size=2400x1080
I MIUIInput: [MotionEvent] ViewRootImpl windowName
         'com.axlebolt.standoff2/com.google.firebase.MessagingUnityPlayerActivity',
         { action=ACTION_DOWN … }
I Unity: Firebase Cloud Messaging API Initialized
```

Its expansion file was read out of the instance's own storage, FMOD loaded its banks,
Firebase Analytics, FCM and AppMetrica all initialised, and the game drew its login screen
at full resolution and took a touch on it. **That is a virtual app running to a usable
screen on physical hardware**, which every previous version of this file said had never
happened. It is the single largest change in this project's status and it is recorded here
before anything that went wrong, because the temptation is to lead with the faults.

Then the user tapped *Sign in with Google*, and `SignInHubActivity` died on the class
loader — the tenth run's fault exactly, in the build that predates its fix. Nothing new,
and it is fixed at HEAD.

What *is* new is underneath, and is worse than anything the crash check reported.

| Found | Fixed by |
|---|---|
| **A game ran under another app's device identity.** `bin.mt.plus` failed to construct its `Application`, and the graft stopped there — *after* the process had been renamed to it, its device profile bound and its identity hooks installed. `AppBootstrap` records the *result* of a graft, so a graft that failed recorded nothing and the process read as claimed by nobody. Sixteen seconds later a `JobStub_p0` job for Standoff 2 fired into that same process: `PROCESS_RENAMED argv0=com.axlebolt.standoff2` followed by `PROFILE_REBIND_IGNORED current=028dca45-… requested=6c904a63-…`. A device profile binds once per process, so the game ran reporting **the file manager's `ANDROID_ID`** — the one value this engine exists to keep separate — and every later launch of the file manager was refused `SLOT_ALREADY_BOUND` | The claim is written **before** the graft, not after it succeeds (`AppBootstrap.Claim`). A process belongs to whoever asked for it first, whatever happened next, and a second instance arriving is refused and the process surrenders the slot. The decision is a pure function with every case pinned (`AppBootstrapClaimTest`). A failed graft also *says so* now — `ROUTER_METHOD_SLOT_FAILED` → `ProcessPool.releaseIf` — so the allocator ends the process and the next attempt gets a clean one instead of the third identical failure in a poisoned one |
| **Nothing in the analyzer would have said a word about it.** The one line that names the fault, `PROFILE_REBIND_IGNORED`, read as a debug note | `slots` fails on it, with the sentence that says what it means: *a second instance was grafted into a process that already had a device profile; it is running under the first one's identity*. Pinned against this log and against a synthetic line, so the check cannot start passing by never firing |
| **`ACCESS_ADSERVICES_ATTRIBUTION` denied twice**, and it is install-time: no dialog exists for it anywhere on the device, so a guest denied it cannot be helped afterwards | Declared, with the five others the same game asks for that UNIQUE did not have: the three `ACCESS_ADSERVICES_*`, `com.google.android.gms.permission.AD_ID`, `com.android.vending.BILLING` and `CHECK_LICENSE`. `REQUEST_DELETE_PACKAGES` is deliberately still refused — it lets a guest put a system uninstall dialog in front of the user for a package on the real device |
| **`bin.mt.plus` still does not launch**, and the log now says more about why than "unexplained". `libmtprotect.so` **loaded cleanly** and 58 ms later `l.ۢ.<clinit>` had no implementation. The library is there and its natives are not registered: the protector ran and declined. And it declined having looked around with nothing of UNIQUE's interception in place — `installIoRedirection` ran *after* `makeApplication`, which is where a packed app loads its protector | The tables and the load-watch are armed **before** `makeApplication` (`armIoRedirection`). ART calls `JNI_OnLoad` *after* `android_dlopen_ext` returns, so a watch installed first patches the protector's own PLT in the gap between the two, and its checks then go through the redirect table and the `/proc` view like any other library's. Whether that changes its mind is a measurement, not a claim: `NO_APPLICATION` is still what the analyzer asserts for this app |

### What a guest can read about UNIQUE, and what it can now not

The eleventh run also settled what to do next, by running the app this project is being
built for. Standoff 2 does not need to defeat anything to know where it is:

```
$ cat /proc/self/maps          # inside :vapp0
… r--p … /data/app/~~eOlB8_…/com.unique-LcSgGP…/base.apk
… r-xp … /data/user/0/com.unique/files/virtual/apk/com.axlebolt.standoff2/…/libunity.so
```

An installed app's maps names its own package and nothing else. Two lines like those are
all a check needs, they cost one `fopen` and no permission, and reading that file is
something every native crash handler does anyway — so the code that would find them is
already in the app for an innocent reason.

`core/native/proc_view.h` answers that file, and `smaps`, and the per-thread spellings of
both, from a rewritten view: the guest's APK directory and UNIQUE's own install directory
both read as `/data/app/~~<a>/<pkg>-<b>`, the instance's data as `/data/user/0/<pkg>`, its
storage as `/storage/emulated/0`. `readlink` answers are put through the same table, which
covers a walk of `/proc/self/fd`.

Three properties it was built to have:

- **It renames, it never deletes.** Every mapping keeps its address, its permissions and
  its place in the file. Unity's crash handler, Crashlytics and every native unwinder read
  this file to turn addresses into symbols, and a view with lines missing would break them
  in ways that look like the app's own fault.
- **It is the exact inverse of the redirect table**, and there is a test that says so: a
  path the view produces, put back through redirection, is the path it came from. A view
  that answered with a path nothing resolves would be a stranger fault than the one being
  fixed — a library that is mapped and cannot be opened is a state no real device produces.
- **It checks its own work and reports the count.** A table with a wrong prefix in it and
  no table at all produce the same "installed" line and opposite behaviour, which is the
  failure this project keeps meeting. So the graft reads the real `/proc/self/maps`, puts
  it through the same code that serves it, and counts what still names UNIQUE:
  `PROC_VIEW_INSTALLED … named=17 leaked=0`. The analyzer's new `detection` check fails on
  anything but zero and names the directory the missing rule is for.

**What it did not cover, as written at the time.** The hook was a PLT patch in the
*guest's own* libraries, which is where anti-cheat code lives and was the reason the scope
was drawn there. It did not cover:

- **Java.** `new FileInputStream("/proc/self/maps")` goes through `libjavacore.so`, a
  system library UNIQUE did not patch — patching it would redirect UNIQUE's own file
  operations in the same process, which is a much larger change than this one.
- **`dl_iterate_phdr` and `dladdr`**, which read the linker's own tables and never open a
  file.
- **A raw `syscall(SYS_openat, …)`**, which crosses no PLT.
- **`ApplicationInfo.dataDir`,** which was the virtual path and had to be: the guest's Java
  code writes there through those same unpatched system libraries. An app comparing it
  with `/data/user/0/<its own package>` sees the difference. Closing that means redirecting
  Java file IO too, which means patching the platform's libraries and exempting UNIQUE's
  own reads — a real design and the obvious next step, and not one to make on reasoning
  alone the week a build is going onto a phone.

The first, third-from-last and last of those are no longer true: the thirteenth run's
section below is that change, made once the game had been read and the scope of it was a
fact rather than a guess. `dl_iterate_phdr`, `dladdr` and a raw syscall remain outside it.

So this is one vector closed, measured, with the rest named. Whether it is *the* vector
Standoff 2 uses is what the next log says.

### The twelfth run, and reading the game instead of guessing at it

The first log from a build with the `/proc` view in it, and the check that was built to
distrust itself did its job on the first phone that ran it:

```
PROC_VIEW_INSTALLED package=com.axlebolt.standoff2 rules=6 named=15 leaked=2
    first=/data/data/com.unique/files/virtual/apk/com.axlebolt.standoff2/203908
```

Thirteen of fifteen mappings renamed and two not — which for this purpose is the same as
none, because a check only has to find one. `/data/data/<pkg>` is a symlink to
`/data/user/0/<pkg>` and the view was built from `Context.getFilesDir()`, which is the
second spelling. `redirectionRules` has always covered both for the *guest*; nothing had
needed it for the host until the view existed. Fixed, with the other two spellings derived
rather than listed.

| Found | Fixed by |
|---|---|
| **Google Sign-In crashed identically to the run before, with the fix for it in the build.** `setExtrasClassLoader(guestLoader)` cannot work and could never have worked: `BaseBundle` reads `mClassLoader` in exactly one place, `unparcel()`, and `unparcel` builds the map lazily — each value becomes a `Parcel.LazyValue` holding the loader *as it was at that moment*. The loader that decides is the one in place at the **first read of any key**, and UNIQUE always reads first: `VirtualLaunchParams.from(intent)` is the first line of every rewrite | `GuestParcelables.loader`, a single forwarding `ClassLoader` that resolves nothing itself and asks the current graft when it is *asked*. It can therefore be installed before the guest is known, which is the whole point — it goes on before the first read instead of after it. Installed on every launch intent, new intent, service intent, activity result, cold-broadcast payload and stub intent, each time on the line above the first read |
| **A relaunch counted as a failed launch.** A launch into a process already grafted for that instance does not graft again, so it has a rewritten transaction and no `BOOTSTRAP_OK` — and the analyzer failed it while the game was plainly running | Both, not either. A rewrite cannot happen without a graft, since the interceptor only reaches it after `bootstrap` returns Ready |
| **Firebase Analytics refused for the third pass running.** Each of the three fixes was real and each was followed by a log with the same message on a different route, because "the rewrite did not fire" and "the rewrite fired and something else refused" have looked identical in the log | Not a fourth guess. `GMS_PACKAGE_NOT_REWRITTEN` names the interface, the transaction code and the byte offset when the guest's package is in a request as a *bare* string rather than a `SafeParcel` field. Deliberately not rewritten there: replacing a bare string changes the length of everything after it, and a parcel gives no way to know whether it sits inside a length-prefixed container whose header would then be wrong. A corrupted request to Play services is a worse failure than a refused one |

And then the game was read, which is what should have happened two passes earlier.
`docs/STANDOFF2.md` has the whole of it; the part that changes this project's plan:

- The message is **two** messages. `Anticheat/VirtualSpaceWarning` is set in-game by
  `Axlebolt.Standoff.Anitcheat.AntiCheatManager`, which keeps a flag literally called
  `VirtualSpaceDetected`. `AuthRestrictions/VirtualSpaceMessage` is a **server verdict**,
  in a family with `RootFoundMessage`, `HackingSoftFoundMessage` and
  `UnofficialVersionMessage`.
- The verdict is decided from an `AppVerification` protobuf carrying `IsRooted`, `ApkHash`,
  `JsonForbiddenApps`, **`Path`**, `ContentHash`, an `AppSnapshot` map and an RSA key —
  and it is a **field of `GoogleAuthRequest`**. Signing in and being told this is a virtual
  space are the same event.
- What the client reads to fill it in is four getters, and the counts are in the binary:
  `sourceDir` ×4, `getApplicationInfo` ×3, `getPackageCodePath` ×2, `nativeLibraryDir` ×1.
  No `/proc`, no `dl_iterate_phdr`, no mount table.

Which means the `/proc` view, built one pass earlier from reasoning about what such a check
*would* read, closes a real vector and closes nothing **this** game uses. That is worth
writing down rather than leaving as an implied win. It stays — the leak it closes is real
and the reasoning was wrong only about which app — but the next step is now known instead
of guessed: a guest's Java-visible paths have to be shaped like an installed app's, and
they have to resolve, which needs the interception widened to `libjavacore.so` and
`libandroidfw.so` with UNIQUE's own operations exempted. That is the largest change this
engine has left and it is not one to make in the same build as three other fixes.

### The thirteenth run, and the change the game's own binary asked for

The first log in which nothing except Google passes or fails by surprise. Eighteen checks,
seventeen green, on the same phone and the same game:

```
PROC_VIEW_INSTALLED package=com.axlebolt.standoff2 rules=16 named=16 leaked=0
detection  ok    16 mapping(s) named UNIQUE, none readable through the view
launch     ok    1/1 launches reached the guest's Activity
crash      ok
```

`named=15 leaked=2` one run earlier, `leaked=0` here — the alias was the whole of it. The
forwarding class loader also held: no `ClassNotFoundException`, no crash, sign-in reaches
Play services and is refused there instead of dying on the way. And the one failing check
names its own route with a byte offset rather than a suspicion:

```
GMS_PACKAGE_NOT_REWRITTEN descriptor=android.os.IMessenger code=1
    package=com.axlebolt.standoff2 bareAt=144 size=308
```

The analyzer had one bug of its own, found by reading its output against the log rather
than trusting it: it reported *"Google answered `DEVELOPER_ERROR`"* on a run where Play
services answered no such thing. The only line containing the word was UNIQUE's own
`GOOGLE_ROUTE` explanation of what **would** happen if a guest signed in. A tool that
cannot tell a forecast from an observation is worse than no tool, because its verdict reads
identically in both cases. It now reads Google's side of the conversation only, and says
so explicitly when the prediction is all there is.

**Then the change the previous run's reading had named.** `docs/STANDOFF2.md` said the
detection is four getters and nothing else. So:

| Was | Is |
|---|---|
| `sourceDir` → `…/com.unique/files/virtual/apk/com.axlebolt.standoff2/203908/base.apk` | `/data/app/~~<a>/com.axlebolt.standoff2-<b>/base.apk` |
| `nativeLibraryDir` → `…/203908/lib/arm64-v8a` | `/data/app/~~<a>/com.axlebolt.standoff2-<b>/lib/arm64` |
| `getPackageCodePath()` → the same virtual path | the installed one |
| `dataDir` → `…/virtual/users/1/data/com.axlebolt.standoff2` | `/data/user/0/com.axlebolt.standoff2` |

Two halves, and the second is what makes it more than cosmetic:

1. **`GuestIdentityPaths`** writes those values *after* the class loader and the
   `AssetManager` have been built from the real ones. `mResDir`, `mSplitResDirs`, `mLibDir`
   and `mClassLoader` are left alone deliberately — resources are rebuilt from `mResDir` on
   a configuration change.
2. **The published paths resolve**, because the interception was widened to the three
   platform libraries a guest's Java file operations pass through: `libjavacore.so`
   (`java.io.File`, every stream, every `SharedPreferences`), `libsqlite.so` and
   `libandroid_runtime.so` (a database opened by absolute path). Android's libcore is
   written against the directory-relative calls, so `openat`, `fstatat`, `fstatat64`,
   `faccessat`, `mkdirat`, `unlinkat`, `renameat`, `readlinkat`, `fchmodat`, `realpath`
   and `statvfs` are hooked alongside the plain names.

This is the change the eleventh run's notes called *"the largest change this engine has
left"*, and the reason it can be made without redirecting UNIQUE out of its own files is a
property of the **table**, not of the scope. Every rule in `redirectionRules` names either
the guest's package or a shared-storage alias; none can match `/data/user/0/com.unique`.
That claim is now a test (`tools/native-test/round_trip_test.cpp`, 27 checks) rather than a
paragraph: UNIQUE's own prefs, database, diagnostics and installed APK all pass through
untouched, and every path a guest is handed round-trips back to itself.

`realpath` is the one call that runs both directions at once — argument inward so it
succeeds, answer outward through the `/proc` view — because otherwise
`File.getCanonicalPath()` would be the single remaining accessor handing a guest UNIQUE's
directory.

**Three things this pass refused to assume.**

- **The data directory is measured, not reasoned about.** A published path that does not
  resolve fails silently, as a guest that cannot read its own saved games. So the data half
  applies only after a probe writes a byte through the published path and finds it at the
  real one — twice, once through `java.io.File` and once by opening a SQLite database,
  because those are different hooks and one library out of scope would let the first pass
  while every database the guest opened went somewhere UNIQUE cannot see. The code half is
  not gated: nothing can be lost by it. Both are reported:
  `GUEST_PATHS_PUBLISHED … code=… data=… detail=…`.
- **The graft's self-check had to stop reading through the thing it tests.**
  `PROC_VIEW_INSTALLED` counted mappings that still name UNIQUE by reading
  `/proc/self/maps` from Kotlin — a genuine second opinion for exactly as long as Java file
  IO crossed no patched PLT. It now does. The check reads through a new
  `UniqueNative.readUnviewed`, which steps around the view; otherwise it would have been
  answered by its own output and could never have failed again.
- **One answer to "where are UNIQUE's own files".** `VirtualPathModel` is constructed from
  `context.filesDir` in a dozen places, correct for as long as `getFilesDir()` in a
  `:vappN` could only mean UNIQUE's directory. `GuestIdentityPaths` ends that. `HostPaths`
  captures the real root once, at the start of the graft, before anything is swapped. The
  call this would have broken first is `VirtualJobDispatcher.stop` — which turned out to
  have been reading the routing store from the *guest's* context all along, finding no
  record, and stopping jobs with the host's own job id. Fixed in passing, with the reason
  written next to it.

**What the next log has to show.** `GUEST_PATHS_PUBLISHED … code=true data=true`, the
`detection` check still at `leaked=0`, every app that ran before still running — the blast
radius here is every file operation of every guest, and a regression would show as an app
that cannot read its own data rather than as a crash — and the game reaching its own menu.
The Google sign-in refusal is *not* expected to move: `AppVerification` rides on
`GoogleAuthRequest`, and that request arrives at Play services as UNIQUE by kernel uid.
This closes the virtual-space verdict, not the Google identity, and those are different
problems with different ceilings.

### The fourteenth run: both gates fired, and one of them was right to

The first log from a build that tells a guest it is installed. It is the most useful log
this project has had, and not because things worked.

**The data gate refused, and named itself.**

```
GUEST_PATHS_PUBLISHED package=com.axlebolt.standoff2 code=true data=false
    detail=a database cannot be opened through the public path:
           SQLiteDiskIOException: disk I/O error (code 1802 SQLITE_IOERR_FSTAT)
```

with SQLite's own diagnosis two lines above it:

```
W SQLiteLog: (28) file renamed while open:
    /data/data/com.axlebolt.standoff2/databases/.unique-path-probe.db
```

The file probe passed and the database probe did not, on the same directory, in the same
millisecond — which is exactly the split the second probe was added for and the reason it
was not enough to write a byte and read it back.

The cause is in `plt_hook.cpp` and it is worth stating precisely, because it is a whole
class of gap rather than one symbol. A GOT hook patches calls: `R_AARCH64_JUMP_SLOT` for a
PLT call, `GLOB_DAT` for a GOT read. **Taking the address of a libc function in a static
initialiser is neither.** It is an absolute data relocation the linker resolves once at
load time into the library's own `.data.rel.ro`, and every later call reads it from there.
SQLite is built exactly that way:

```c
static struct unix_syscall { const char *zName; sqlite3_syscall_ptr pCurrent; } aSyscall[] = {
  { "open",  (sqlite3_syscall_ptr)posixOpen, 0 },   /* a local wrapper -> reaches the PLT */
  { "stat",  (sqlite3_syscall_ptr)stat,      0 },   /* the address     -> does not        */
  { "lstat", (sqlite3_syscall_ptr)lstat,     0 },
```

So `open` was redirected and `stat` was not, inside one library. SQLite opened the database
in the instance, looked for it at the path it had been given, did not find it, and said so.
Its `lstat` reaching the real filesystem is also why `/data/user/0` appears resolved to
`/data/data` in that message — a rewriting no rule of UNIQUE's produces, and the detail
that turned a guess into a diagnosis. `R_AARCH64_ABS64` with a zero addend is patched now,
and `tools/native-test/run.sh` compiles that construct and asserts the relocation it
produces, so the premise is checked rather than remembered.

**The code gate did not exist, and should have.**

```
E misc: isReadonlyFilesystem():
    statfs(/data/app/~~kx_uUO…/com.axlebolt.standoff2-kROpHz…/base.apk) failed:
    No such file or directory
```

The code half was published unconditionally, on the argument that a wrong value there could
lose nothing because the class loader and resources were already built. That argument was
about UNIQUE and said nothing about the rest of the process. A system library nobody had
listed read the published `sourceDir` and could not open it, because it was outside a scope
of three libraries — and the user's report of what the game then showed was that the device
was out of space.

**A path handed to a guest is handed to every library in the guest's process, and any of
them may be the one that opens it.** Publishing such a path while hooking part of the
process is worse than publishing nothing: it works for some callers and not others, and the
failure arrives as the guest's own bug. So the scope is the whole process now, minus the
linker, `libc`/`libdl`/`libm`, UNIQUE's own native library and the per-app exclusions. What
makes that safe is unchanged and is a property of the table rather than of the scope — no
rule can match `/data/user/0/com.unique` — and `round_trip_test.cpp` asserts it by name.

The code half is gated too now: no slots patched, or a published `base.apk` that does not
open, and it is not published.

Three smaller things from the same log:

- **The scan is memoised.** With the whole process in scope, re-walking four hundred
  libraries' relocation tables on every `dlopen` is not free. Each set of hook requests
  owns its own memo — sharing one between the redirect and the load watch would have the
  first mark libraries as done for the second, which would then patch nothing and never
  say so.
- **`NOTHING_TO_HOOK` was reported on every re-scan**, because the status read that pass's
  slot count rather than the cumulative one. A healthy re-scan patches nothing. It reads
  the total now.
- **The analyzer gained a `paths` check**, written against this log: it reports the gates
  refusing as notes, and fails on a published path that some caller could not open. The
  distinction is the point — the first is the design working, the second is the bug.

**What the next log has to show.** `GUEST_PATHS_PUBLISHED … code=true data=true`, no
`paths` failure, `detection` still at `leaked=0`, and every app that had data still having
it — the scope change touches every file operation of every guest, and a regression there
looks like an app that is empty rather than one that crashes. The new
`io_redirect: hooked <lib>=<n>` lines say which libraries the redirect actually reached, and
`io_redirect: nothing in this process imports: …` names any symbol in the table that nothing
spells that way. Either line would have named this run's bug on sight.

### The fifteenth run: hooking everything is not the answer either

The fourteenth run said three libraries was too narrow — a system library outside the scope
could not `statfs` the published APK path, and the game told its player the device was out
of space. The answer tried here was the whole process. This log is what that costs:

```
io_redirect installed: 436 slot(s) in 385/389 libraries, scope=*
io_redirect: hooked 9 new slot(s) after loading mapper.mediatek.so
E mali_config_interface_c_mapper: Failed to locate stable-C mapper library
E mali_config_interface_mapper: Failed to acquire IMapper service. Aborting.
E CRASH: signal 6 (SIGABRT) … name: RenderThread >>> com.axlebolt.standoff2 <<<
```

A vendor gralloc mapper is not an ordinary consumer of a path. It is a driver, it runs on
the render thread, and it aborts where ordinary code returns an error. The table argument —
no rule in `redirectionRules` can match `/data/user/0/com.unique`, so a hooked library
touching UNIQUE's own files is unaffected — was never wrong, and was never the whole
argument. **A library can be broken by being hooked at all, whatever the table then
decides.** That sentence is the finding.

Two more things came with it, and the third is the reason this run reads worse than the one
before rather than better:

- The published APK path stopped resolving, so the code gate refused:
  `GUEST_PATHS_PUBLISHED code=false slots=436 … the published APK path does not open`.
  The fourteenth run, with a *narrower* hook, published it. A wider hook redirecting less
  is not something reasoning predicts, and it is not yet explained; the per-library
  diagnostic added for it is capped and did not name `libjavacore.so` either way.
- `androidx.datastore` threw `Unable to create parent directories of
  /data/user/0/com.axlebolt.standoff2/files/datastore/…` on a Firebase background thread,
  twice. The game restarted three times in a 60-second run.

**So the scope goes back to a named list, and this time it is derived from a measurement
rather than from a guess about which libraries matter.** The fifteenth run printed, per
library, how many slots each had for these symbols — the diagnostic added after the
fourteenth — and the shipped list is that output with the graphics and vendor libraries
taken back out: `libjavacore.so`, `libsqlite.so`, `libandroid_runtime.so`,
`libandroidfw.so`, `libbase.so`, `libcutils.so`, `libutils.so`, `libnativeloader.so`,
`libincfs.so`, `libdataloader.so`, `libbinder.so`, `libz.so`. `/vendor/`, `/odm/` and
`/system/vendor/` are excluded outright so that widening the scope by accident cannot reach
a driver again.

**Absolute data relocations are narrowed the same way, and for the same reason.** Patching
them is what the fourteenth run's SQLite failure needed — SQLite stores `stat` and `lstat`
by address in a static table and reaches `open` through the PLT, so a call-only hook
redirects half of one library. The first attempt patched them process-wide. A PLT slot is
by construction a call into an imported function; an absolute slot is a pointer in a
library's own data, and a symbol-name match is a much weaker warrant for overwriting one.
They are now patched in `libsqlite.so` and nowhere else, and the per-library diagnostic
marks which libraries got them.

**What this run does not settle, stated so it is not counted as progress.** The
`code=false` cause is unexplained. If the next run still refuses, the per-library line —
now uncapped enough to list everything, and marked with which relocation kinds were used —
says whether `libjavacore.so` got its `stat` patched, and that is the next thing to read.

### The sixteenth run: `java.io.File` is two libraries, not one

The scope revert held. No native abort, no datastore failure, the game launches and shows
its own menu, `leaked=0`. Two checks fail, `google` and `paths`, and only the second is new
work.

What makes this log the useful one is a single line, printed by the diagnostic added two
runs earlier for exactly this:

```
io_redirect installed: 81 slot(s) in 15/389 libraries
io_redirect: hooked libjavacore.so=8
io_redirect: hooked libsqlite.so=2+abs
```

Eight slots in `libjavacore.so` — `open`, `stat`, `lstat`, `access`, `mkdir`, `rmdir`,
`unlink`, `rename`, which is every path a guest's `java.io` **writes** through. And the code
gate still refused: *the published APK path does not open*.

Both are true at once because **`java.io.File` is two libraries.** Writes go through
`libcore`'s `Os` API in `libjavacore.so`. But `File.isFile()`, `length()`,
`lastModified()`, `delete()` and `list()` are `UnixFileSystem`'s *native* methods, and
those live in Android's OpenJDK port — `libopenjdk.so` — which was not in scope. That file
is written against the large-file API:

```c
static jboolean statMode(const char *path, int *mode) {
    struct stat64 sb;
    if (stat64(path, &sb) == 0) { ... }
```

and `stat64` is a **different symbol** from `stat` in a relocation table, even though on a
64-bit device it is the same function.

So every write was redirected and the first `stat` of a published path was not. It also
explains why the fourteenth run's file probe passed while this gate refuses: the probe wrote
through the public path and read the result back through the *real* one, and never asked
`stat` about a public path at all. The gate is the first thing that ever did.

Three changes:

- **`libopenjdk.so` is in the scope**, and the large-file spellings are in the symbol table:
  `stat64`, `lstat64`, `open64`, `openat64`, `fopen64`, `statfs64`, plus `remove` and
  `creat`/`creat64`, which the same file uses for delete and truncating create.
- **The gate distinguishes its three failure modes**, because they produced one message and
  cost a round. It now asks the native table directly: no rule maps the path; the rule maps
  it somewhere the file is not; or the rule is right and Java still cannot see it — which
  names the fault as a missing hook rather than a missing rule, and is what happened here.
- The fixture and its assertions are checked in, including that the fifteenth run's native
  abort and datastore failure stay gone.

### The seventeenth run: the code paths held, and the SQLite fix had never run

The first log in which a guest was handed installed-shaped paths for its own APK and they
resolved:

```
GUEST_PATHS_PUBLISHED package=com.axlebolt.standoff2 code=true slots=102 data=false
    apk=/data/app/~~kx_uUO_2M6FP3W-o_gj5uw/com.axlebolt.standoff2-kROpHz2y_eD6hTS9Y0RvGQ
```

`code=true` is what the sixteenth run's `libopenjdk.so` and `stat64` work was for, and it
held on hardware. Three launches, three activities, no crash, `leaked=0` for both guests,
the game to its own menu. Nothing the fifteenth or sixteenth run fixed regressed.

**Then the data half refused with the fourteenth run's message, in a build that was
supposed to have closed it.**

```
I UniqueNative: io_redirect: hooked libsqlite.so=2+abs
W SQLiteLog: (28) file renamed while open: /data/data/…/.unique-path-probe.db
E SQLiteLog: (1802) statement aborts at 29: [CREATE TABLE …] disk I/O error
W LAUNCH GUEST_PATHS_PUBLISHED … data=false
    detail=a database cannot be opened through the public path: SQLITE_IOERR_FSTAT
```

**A claim made after the fourteenth run is retracted.** That run's section says
`R_AARCH64_ABS64` with a zero addend "is patched now", and the sixteenth run's per-library
line was read as confirming it. It was not. `libsqlite.so=2+abs` is two patched slots and
`+abs` names an *attempt*: it says absolute relocations were enabled for this library, not
that any were found. Two is the PLT count on its own — `posixOpen` reaching `open`, and
nothing else — and every one of SQLite's other file operations is a pointer in
`aSyscall[]` that the scan never saw.

It never saw them because **Android links its platform libraries with packed relocations.**
`--pack-dyn-relocs` replaces `DT_RELA` with an APS2 blob under `DT_ANDROID_RELA`, and
`read_dynamic` reads `DT_RELA`. A library linked that way reports, in every number the scan
produces, as a library with no data relocations at all. The mechanism was right about
SQLite and wrong about the file format, and no log before this one could have said so,
because "patched nothing" and "there was nothing to patch" printed the same number.

The fix does not argue with the format. SQLite publishes an interface for exactly this
problem — `sqlite3_vfs.xSetSystemCall`, version 3 — which replaces an entry of `aSyscall`
by name and reaches every use of it, however the library was linked. Reaching that
interface needs the address of `sqlite3_vfs_find` in a library an app is not allowed to
`dlopen`, which `core/native/…/elf_symbols.h` reads out of the loaded library's own
`.dynsym`; both hash-table shapes are handled, because the table carries no length of its
own and which one a library has depends on how it was linked. `tools/native-test/run.sh`
checks that walk against a real linker's output and against `dlsym`'s answer for the same
name.

Two numbers now say which mechanism did what, and they are in different events on purpose:

- `IO_REDIRECT_INSTALLED … slots=102 sqlite=8` — GOT slots, and SQLite's own table.
- `GUEST_PATHS_PUBLISHED … slots=102 sqlite=8 data=…` — the same pair beside the gate that
  reads them.

`sqlite=0` with a database refusal is now a failure of the `paths` check rather than a
note, and a build that does not report the field at all says so rather than being read as
zero. The `+packed` marker on the per-library line names the format directly, so the next
log states the premise instead of implying it.

**No Google sign-in was attempted in this run**, and the `signin` check says exactly that
rather than passing quietly.

### The eighteenth run: SQLite landed, and publishing the path broke the game

Two things in one log, and they point in opposite directions.

**The SQLite fix works, on the first attempt, on hardware.**

```
io_redirect: sqlite 8/8 system call(s) replaced (library=yes api=yes vfs=v3)
GUEST_PATHS_PUBLISHED … code=true slots=102 sqlite=8 data=true
    detail=both paths round-tripped into the instance
```

`data=true` for the first time in this project's history. Both halves of the identity a
guest reports for itself are now published *and* they round-trip — a byte written through
the public path arrives in the instance through `java.io`, and a database opened through it
does too.

The same log confirms the seventeenth run's diagnosis outright, in the marker added for
exactly that purpose:

```
io_redirect: hooked libsqlite.so=2+abs+packed
io_redirect: hooked libjavacore.so=11+packed
io_redirect: hooked libopenjdk.so=14+packed
```

Every platform library in scope is `+packed`. The absolute-relocation patch was enabled for
`libsqlite.so` and there was no relocation array for it to walk. It could never have
worked, and now nothing depends on it.

**And the tester's screen said this:**

> **Error** — Not enough storage space to install required resources.

with, in the log:

```
E Unity: ApkAddCentralDirectory : Unable to open '/data/app/~~kx_uUO…/base.apk'
E Unity: Failed to read assets/bin/Data/unity_app_guid
```

The game was handed the installed-shaped path for its own APK and **its own engine could
not open it**, so it concluded its resources were not installed. The sixteenth run — the
last with `code=false` — has no Unity error at all. The regression arrived with the
publication, and it was in the seventeenth log too, four times, unread.

**Two corrections, and both of them are the analyzer's as much as the engine's.**

*The check passed a run the user was looking at an error dialog in.* `paths` matched
`statfs(<path>) failed: …` — a system library reporting an errno — and nothing else. An
app's own engine does not say that; Unity says `Unable to open '<path>'`. Both spellings
are read now, runs 17 and 18 fail the check, and `docs/STATUS.md`'s claim that run 17
passed everything but `google` is retracted here.

*And the gate that decides whether to publish asks a library that is not the one at risk.*
`File.isFile()` reaches `libopenjdk.so`, which the redirect covers. The caller that failed
was the guest's own, which it did not.

**Why it did not, checked against bionic rather than reasoned about.**

1. **`__open_2`.** A release build's two-argument `open(path, O_RDONLY)` does not call
   `open`. The NDK enables `_FORTIFY_SOURCE` at every optimisation level, and bionic's
   `bits/fortify/fcntl.h` turns that call into `__open_2` — a *different symbol*, which the
   redirect table did not have. What it did have was `__openat`, which bionic does not
   export at all, so it could never match anything and reported as a symbol nothing
   imports. `llvm-readelf --dyn-syms` on the NDK's own `libc.so` settles both:
   `__open_2`, `__openat_2`, `__readlink_chk` and `__readlinkat_chk` exist; `__openat`
   does not. All four are in the table now, with their own trampolines, and
   `tools/native-test/check_libc_symbols.py` checks every name in it against that same
   `libc.so` — the check that would have caught this, and which skips with a message on a
   machine with no NDK.

2. **The library-load watch was never re-armed.** It hooks `dlopen` in the libraries
   loaded when it is installed. `System.loadLibrary` goes through `libnativeloader.so`,
   which was — but `libmain.so` pulls in `libunity.so` by `dlopen`ing it *itself*, and
   `libmain.so`'s own `dlopen` slot was never patched, so no rescan ran. The eighteenth log
   contains no rescan for `libunity.so` at all; the seventeenth contains one, 1.7 seconds
   before Unity failed anyway, which is what proves the two faults are separate.
   `rescan_after_load` re-arms the watch as well as the redirect now.

The per-library line also names a guest's own library that patched **nothing**
(`libunity.so=0+packed`), because "never scanned" and "scanned and matched no symbol" are
a missing rescan and a missing symbol respectively, and they printed the same absence.

### The nineteenth run: the data half is published for two apps, and WebView traps

Read for what it settles and what it does not, because the second is the part that is easy
to overstate.

**Settled.** Two apps, both `code=true slots=120 sqlite=8 data=true`. The per-library line
now names a guest's own library that patched *nothing* — `libmain.so=0`,
`libcrashlytics.so=0`, `libdatastore_shared_counter.so=0` — so "never scanned" and
"scanned and matched no symbol" are finally two different answers rather than one absence.
And `libsentry.so=10`, `libsentry-android.so=1` and `libconscrypt_jni.so=2` are hooked for
the first time in any log, through the rescan the re-armed load watch fires:

```
io_redirect: hooked 10 new slot(s) after loading …/libsentry.so
```

**Not settled, and this matters.** `libunity.so` never loaded — there is no `I Unity:` line
in this log at all. The game was launched, reached its own activity, and was closed eight
seconds later. So the absence of `ApkAddCentralDirectory : Unable to open` here is **not**
evidence that the `__open_2` fix worked. The eighteenth run's fault is still unanswered.

**New, and located to the line.** With the data path published, ChatGPT died:

```
E chromium: [ERROR:crashpad/util/file/filesystem_posix.cc:63]
    mkdir /data/user/0/com.openai.chatgpt/cache/webview_vapp0/Crashpad:
    No such file or directory (2)
F libc: Fatal signal 5 (SIGTRAP), code 1 (TRAP_BRKPT) … (.openai.chatgpt)
```

This is the data half's version of the fault the code half had one run earlier.
`libwebviewchromium.so` runs **in the guest's process** — the in-process WebView — was
handed the guest's published cache directory, and is not in `PLATFORM_IO_LIBRARIES`. The
directory it wanted to create has a parent chain that exists only inside the instance, so
the `mkdir` reached the real filesystem and found nothing. Chromium does not degrade when
its crash handler cannot be set up: it fails a `CHECK` and traps, and the app goes with it.
The same app ran without this in the twelfth and fourteenth runs, both of which had
`data=false`.

`libwebviewchromium.so` is in the scope now, with `libmonochrome.so` and
`libmonochrome_64.so` — the same code under the names the Trichrome and Chrome WebView
providers use on other phones.

**Two things the analyzer had to learn from this.**

*A `mkdir` under a published path failing with `ENOENT` is never legitimate.* It means the
parent chain exists only in the instance, so the caller is outside the redirect. `paths`
reads that shape now, and it reads the *data* half's published prefixes as well as the
APK's — which the event does not carry, because they are arithmetic on the package name.

*And the `native` check pointed at the wrong library.* It pairs a native crash with
whatever was hooked most recently, which here was Sentry and Conscrypt; acting on that
would have excluded a library that was not the cause and cost a round. It cross-references
`paths` now and says so.

### The twentieth run: the game dies in its own `onCreate`, and the cause is UNIQUE's

> "захожу в standoff 2 вылетает и все"

```
Abort message: 'JNI FatalError called: Unable to load library:
    /data/app/~~qDQo4EJx…/com.axlebolt.standoff2-ZoMeAB09…/lib/arm64/libunity.so
    [dlopen failed: library "libunity.so" not found]'
  #05 …/lib/arm64-v8a/libmain.so
  #08 com.unity3d.player.UnityPlayer.loadNative
  #16 com.unity3d.player.UnityPlayerActivity.onCreate
```

**Read what the linker actually complained about.** Not the path in the message —
`library "libunity.so" not found`, the bare soname. Unity's `libmain.so` asks for it by
name, and a name is resolved in **the calling library's linker namespace**: `dlopen` in
`libdl.so` is one line that hands `__builtin_return_address(0)` to the loader for exactly
that purpose. The library-load watch hooks `dlopen`, so the call reached the loader from
`libunique_native.so`, and UNIQUE's namespace does not contain the guest's libraries.

**This is a regression, and it is mine.** The `dlopen` hook is old; re-arming the watch on
every library load — added after run 18, and correct in itself — is what first put it in
front of `libmain.so`. Runs 17 and 18 were spared only because the watch had been armed
once and had never reached that library.

The hook is gone. `android_dlopen_ext` keeps its own, because `libnativeloader` passes an
`android_dlextinfo` that names the namespace outright and the caller's address is never
consulted — and that is the route `System.loadLibrary` takes, which is how the libraries
the watch exists for arrive. It now redirects the path as well, which it did not. The cost
is stated rather than hidden: a library a guest `dlopen`s *itself* is not redirected and
does not trigger a rescan until the next `System.loadLibrary`.

**And the nineteenth run had this too.** Two of the same tombstones, in the log that was
read as "the data half reaches two apps". It was missed because the analyzer's `crash`
check reads `AndroidRuntime: FATAL EXCEPTION`, which is an uncaught *Java* exception and
nothing else. An `abort()` — a `JNI FatalError`, a failed `CHECK`, a native assertion —
leaves a `DEBUG` tombstone with an `Abort message:` line instead, and nothing read it. So
a run in which the game died twice reported no crash at all.

That is the third time in four runs that a check passed a log the tester was watching a
failure in, and the pattern is the same each time: the tool reads one spelling of a fault
and the app uses another. The `crash` check reads the tombstone now, folds both launches
into one finding, and keeps the abort message long enough that the linker's own reason
survives — the part that says *which library and why* is at the end of it.

### The twenty-first run: the two ends of one fact, and the shape that closes both

Removing the `dlopen` hook did exactly what it was for. The game loads `libunity.so`,
starts its engine, and there is no tombstone in this log at all. Then it showed *"Not
enough storage space to install required resources"* again:

```
E Unity: ApkAddCentralDirectory : Unable to open '/data/app/~~qDQo4EJx…/base.apk'
E Unity: Failed to read assets/bin/Data/unity_app_guid
```

**The reason is in what the per-library line does not say.** `libmain.so=0` is there.
`libunity.so` is not, and there is no `hooked … after loading` line in the whole log.
`libmain.so` loads `libunity.so` through plain `dlopen`, and with that hook removed nothing
noticed the load — so `libunity.so` was never scanned and its own `open` was never
redirected.

So runs 20 and 21 are not two faults. They are the two ends of one:

| | |
|---|---|
| hook `dlopen` | the call reaches the linker from `libunique_native.so`, the namespace moves, and `libunity.so` cannot be found *by name* — run 20, a `JNI FatalError` in the game's own `onCreate` |
| do not hook `dlopen` | nothing sees the load, `libunity.so` is never redirected, and the engine cannot open its own APK — run 21 |

**The shape that closes both** is the one `dlopen` itself has. `libdl.so`'s is a single
line, `__loader_dlopen(name, flags, __builtin_return_address(0))`, and the only thing a GOT
hook destroys is that third argument. So the hook stays and calls `__loader_dlopen`
directly with *its own caller's* return address — `libmain.so`'s, not UNIQUE's — which
leaves the namespace exactly where it was while still redirecting the path and triggering
the rescan.

`__loader_dlopen` is exported by the dynamic linker and by nothing else, and `dlsym` will
not answer for it: it is not in a namespace an app can reach. It is read out of the
linker's own dynamic symbol table, by the walk `elf_symbols.h` already does for SQLite.
**If it cannot be found, `dlopen` is left alone** — run 20's failure is fatal and run 21's
is not, so the fallback is the side that still loads.

What the next run has to show, in order: no tombstone, then
`io_redirect: hooked … after loading …/libunity.so` and a `libunity.so=<n>` line, then
whether `ApkAddCentralDirectory` is gone. If it is not, that line and the
`nothing in this process imports` line beside it name the missing symbol outright, which is
the first time this fault will have been answerable without another guess.

### The twenty-second run: the loader fix holds, and the engine is still not redirected

Everything runs 20 and 21 were missing is in this log:

```
io_redirect: linker dlopen located; dlopen is hooked with the caller preserved
io_redirect: hooked 1 new slot(s) after loading …/lib/arm64-v8a/libunity.so
io_redirect: hooked libunity.so=2
```

No tombstone, so the namespace is preserved and the game loads its engine. A rescan fired
on the load, so the hook sees it. `libunity.so` was scanned. Both ends of the `dlopen`
problem are closed and the shape that closed them — calling `__loader_dlopen` with the
caller's own return address — is the one `dlopen` itself has.

**And the engine still cannot open the APK.** `libunity.so=2`: out of thirty-eight names in
the redirect table, Unity's engine imports two, and the log does not say which two or what
else it asks libc for. That has been the state of this fault for four runs, and it is the
reason each round has ended in a guess about how Unity opens a file.

So the scan now reports the names, for the guest's own libraries only:

```
io_redirect: symbols libunity.so patched=<names> unhooked=<file operations it imports
             that the table does not have>
```

Both halves come out of a walk that was already happening — the relocation being examined
carries the symbol name — and the second half is the one that matters: a file operation
`libunity.so` imports and UNIQUE does not hook is, by elimination, the call that opens the
APK. The next log answers it outright instead of narrowing it.

Three names were added on the way, from a diff of the table against bionic's own exported
path-taking functions rather than from a guess about Unity: `freopen`, `freopen64` — which
open a path outright — and `statx`, the modern `stat` a recent libc++ `std::filesystem`
reaches for. `tools/native-test/check_libc_symbols.py` covers all forty-one.

### Signing in: the refusal happens before the account picker, and that is measurable

The thirteenth and sixteenth logs both contain sign-in attempts, and re-reading them for
the *timing* rather than for the outcome answers a question this project has been treating
as unanswerable.

```
ACTIVITY_IMPLICIT_LEFT_GUEST action=com.google.android.gms.auth.GOOGLE_SIGN_IN   …460.307
D TokenPendingResult:  … Status{statusCode=CANCELED, resolution=null}            …460.686
```

Five attempts in the sixteenth run: 0.38 s, 0.47 s, 0.31 s, 0.23 s, 0.25 s. Four in the
thirteenth, the same shape. **No account picker was ever drawn.** Nobody chose anything and
nobody cancelled anything; Play services refused the request and returned.

That is not the wall this project has been describing. The documented one — an OAuth client
registered against a package and a signing certificate — is reached *after* an account is
chosen and answers `DEVELOPER_ERROR`. What happens here is earlier and simpler:

```java
// play-services-auth, in the guest's own APK
new SignInConfiguration(context.getPackageName(), googleSignInOptions)
```

`context.getPackageName()` is the guest's, correctly, because the graft works. The package
that actually *started* the activity is `com.unique`, because a `:vappN` process is UNIQUE.
Play services compares the two, and a caller claiming to be a package it is not is exactly
what that comparison is for.

So the request is now made self-consistent on the way out. `GoogleSignInHandoff` copies the
configuration — the app keeps its own object untouched — replaces the field whose value is
the guest's package name with `com.unique`, and reports what it did:

```
GOOGLE_SIGN_IN_RETARGETED action=…GOOGLE_SIGN_IN package=com.axlebolt.standoff2
    to=com.unique fields=1 shape=bundle serverToken=requested
```

The field is found by value and never by name — `SignInConfiguration`'s fields are `zba`
and `zbb` in one release of the client library and something else in the next — which is
ARCHITECTURE §18 rule 8 applied to a data structure instead of a method.

**What this is expected to buy, and what it is not.** It should get the account picker
drawn, which is the state the user reports other engines reaching. It does not register an
OAuth client: a sign-in asking only for id, email and profile needs none and should
complete, while `requestIdToken(serverClientId)` and `requestServerAuthCode(...)` are
validated against the client registered for the *calling* package and certificate — now
`com.unique`, which no developer has registered — and the documented answer for that is
`DEVELOPER_ERROR` (10). Which of the two a given app is in is a property of its own
`GoogleSignInOptions`, so `serverToken=requested|no` is reported *before* Google answers,
and the next log explains itself rather than starting another investigation.

Stripping the token request to force a "success" was considered and deliberately not done.
An app handed an account without the token it asked for fails later, on its own server, in
a way nobody holding the phone can read.

The analyzer has a `signin` check now, separate from `google`, and it reads the gap between
the handoff and the answer. Under two seconds is a refusal before anything was drawn;
longer is a person who saw the picker, which is a different event and must not be reported
as this one. Runs 13 and 16 now fail it, which is the point: the tool reports a fault a
real phone produced four runs ago and nobody read.

### What the thirteenth run settled about signing in, found late

Re-read for a different question, the thirteenth log answers one this project had been
answering wrongly. The user tried Facebook login in Standoff 2, and the SDK printed the
identity it had been given:

```
V com.facebook.unity.FB: Init({"appId":"752573801798020", …})
D com.facebook.unity.FB: KeyHash: lcG7acvUIg0k4FQSQmAbyw1tN0o=
```

A Facebook key hash is `base64(SHA-1(signing certificate))`, computed at run time from
`getPackageInfo(getPackageName(), GET_SIGNATURES)`. Decoded, that is
`95:C1:BB:…:37:4A`. UNIQUE's own signing certificate is `64:DB:A6:…:47:6B`. They are
different, so the SDK was handed **the guest's** certificate: it asked the app's own
`PackageManager`, which is UNIQUE's virtual one, and got the right answer.

**That is the distinction this project had been missing.** "A guest cannot prove who it is"
was written as a property of virtualization. It is a property of *who is asked*:

| The SDK asks | Resolves identity by | UNIQUE |
|---|---|---|
| Play services (another process) | kernel uid | cannot reach it |
| the app's own `PackageManager` | the virtual one | **already correct** |

Google is the first row. Facebook, VK and most SDKs are the second, and for them the
identity half is solved today.

What is not solved is the return leg, and the same log shows exactly where it stops. Three
of the Facebook SDK's four activities ran inside the space — `FBUnityLoginActivity`,
`FacebookActivity`, `CustomTabMainActivity`, each routed onto a stub and launched — and the
fourth opened Chrome:

```
ACTIVITY_IMPLICIT_LEFT_GUEST action=android.intent.action.VIEW data=https
    package=com.axlebolt.standoff2 handledByHost=com.android.chrome
```

From there the redirect `fb752573801798020://authorize/…` goes to
`PackageManagerService`, which has never installed a package declaring that scheme. The
sign-in completes on Facebook's side and arrives nowhere — the same wall the sixth run
found for OAuth generally, now with a named app behind it.

So the highest-value unbuilt piece in this engine is the in-space browser: intercept the
authorize `ACTION_VIEW`, run it in a `WebView` in the guest's own process, watch for the
guest's declared redirect scheme, and deliver it as an `Intent` directly. It is not blocked
on anything.

**It comes second, and that is a dependency rather than a preference.** Every auth request
shape in Standoff 2's binary carries the same `AppVerification` report, so the virtual-space
verdict refuses a Facebook login for exactly the reason it refuses a Google one. Closing the
verdict first is what makes the browser work worth doing; doing it in the other order
produces a login that reaches the server and is refused, and teaches nothing.

### What the sixth run settled about Google sign-in

The Google layer used to answer `PASSTHROUGH` for `SIGN_IN` whenever an app declared an
OAuth redirect scheme, with the rationale *"the browser flow can be used and is the most
reliable path"*. The outbound half of that is true. The return half cannot happen: the
redirect is an `ACTION_VIEW` for `myapp://callback`, Chrome hands it to
`PackageManagerService`, and the activity declaring that scheme belongs to a package the
platform has never installed. Nothing resolves it, and the user is left on a browser page
with a sign-in that completed on Google's side and arrived nowhere.

UNIQUE cannot close this. An intent filter is fixed in a manifest at build time, the
scheme is the *guest's* and is only known at import, and the one runtime lever the
platform offers — enabling and disabling a pre-declared `<activity-alias>` — still needs
the scheme to have been declared in advance. The interception UNIQUE has runs inside the
guest's own process; Chrome's process is not UNIQUE's to hook.

So `SIGN_IN` and `OAUTH_WEB` now report `UNSUPPORTED` with the scheme named in the
rationale, and in-space Google Play services stays the only route that can answer them —
because there the whole exchange stays inside the space. A mode that reads as working is
worth less than a sentence that says what to do instead.

## Previously blocking, now fixed

Each device run moved the failure further down the launch path. None of these were
visible to unit tests:

| Blocker | Status |
|---|---|
| `hostContext` null during `attachBaseContext` → silent return | fixed |
| `makeApplication` failure reason swallowed | fixed (chain now reported) |
| `SecurityException: Writable dex file … is not allowed` (W^X on the APK) | fixed |
| `SecurityException: Given calling package … does not match caller's uid` | fixed (outbound identity rewrite) |
| `SecurityException: Package … does not belong to <uid>` — the `AttributionSource` on provider calls | fixed |
| **Re-importing a package deleted every instance of it.** `@Insert(onConflict = REPLACE)` is DELETE-then-INSERT in SQLite, and `instances` cascades on `packages.packageName` | fixed (`@Upsert`, which never issues a DELETE) |
| An update deleted the old APK while a task still named that version, so an updated app could not be reopened from Recents | fixed (the launch falls back to the version on disk; superseded APKs are reclaimed at launch instead) |
| The permission layer answered once and then stopped: since Android 12 both routes answer from a `PropertyInvalidatedCache` **inside the app's own process**, so later checks never crossed Binder and never reached the shim | fixed (the platform's own per-process cache disables, called at bind) |
| `awaitFile` returned the first non-empty version of a file the probe writes several times per launch, so a test could assert on state that was not finished yet | fixed (`awaitFileWhere`, which waits for a named stage) |
| `t06` passed vacuously: `/proc` is `hidepid=invisible`, so the pid check saw nothing | fixed (`ActivityManager.getRunningAppProcesses`) |
| `t06`: the crash extra never arrived — the task was recreated from its *stored* intent | fixed (`FLAG_ACTIVITY_CLEAR_TASK`) |
| Every bind reached AMS unrewritten: `ContextImpl` calls `bindServiceInstance` from Android 12 on, and the shim was registered for `bindService`, which still exists and so bound cleanly | fixed (structural service matcher; the bind report now names the concrete methods) |
| The same shape again: `activity_task` was declared as a hook target and never installed, so a guest could not open a second screen | fixed (`VirtualActivityTaskManagerHook`) |
| All of a guest's activities share one stub pool, so with `FLAG_ACTIVITY_NEW_TASK` the task system saw the *same component* and answered `START_DELIVERED_TO_TOP` instead of starting the requested screen — and two `PendingIntent`s for two screens compared equal | fixed (`Intent.setIdentifier` on the stub intent; the guest's own identifier is restored on unwrap) |
| `stageProbeApk` declared no inputs, so the suite ran an old probe against a new engine while reporting green | fixed (probe sources are inputs; the APK is rebuilt, not reused) |
| `verify-device.sh` piped `adb install` into `tail`, discarding its exit status — a failed install ran the suite against whatever was installed before | fixed |
| **A released slot's process was never ended, so every app launched after it got `SLOT_ALREADY_BOUND` and never started.** Three apps in a row on a real phone | fixed (the pool ends the process, checks liveness before reassigning, and reclaims dead slots) |
| Nothing ever called `ProcessPool.release` on a crash or a low-memory kill, despite the doc saying so, so a crashed guest kept its slot for as long as UNIQUE stayed up | fixed (liveness is re-checked at allocation instead of relying on a callback) |
| **Install-time permissions were denied to every guest** — `INTERNET`, `ACCESS_NETWORK_STATE`, `WAKE_LOCK` — with no user action that could have granted them | fixed (`PlatformPermissions` splits runtime from install-time; only dangerous ones are the user's decision) |
| A permission the guest's own manifest defines was denied, because the host does not hold a permission only the guest declares | fixed (self-defined non-dangerous permissions are granted, as at install) |
| `RestrictionsManager.getApplicationRestrictions` refused, killing ChatGPT in `Activity.onCreate` | fixed (`restrictions` proxied) |
| `LocaleManager.getApplicationLocales` refused, killing ChatGPT's network thread | fixed (`locale` proxied; `setApplicationLocales` refused rather than applied to UNIQUE) |
| `ConnectivityManager.getNetworkCapabilities` refused, so a guest saw no network | fixed (`connectivity` proxied, with eleven more services that validate the same way) |
| UNIQUE's own App Details screen threw on every open: `as bool?` throws on a `String` in Dart, so the tolerant fallback beside it never ran | fixed (type-test instead of cast, with tests for both shapes) |
| A guest with no libraries loaded yet reported `IO_REDIRECT_INSTALLED status=NOT_IMPLEMENTED`, which reads as a missing subsystem | fixed (`NOTHING_TO_HOOK`; a failed dlopen watch is `kFailed`) |
| The 131 JVM tests could not be run without a 1 GB Flutter SDK, because `settings.gradle.kts` refused to configure without it | fixed (a build without Flutter configures `core/` and says loudly that `:app` is not in it) |
| **The guest's `Application.onCreate` ran in the middle of the graft**, before the identity hooks and before its own providers, so anything it touched cached an unproxied interface — which is why `connectivity` and `notification` were still refused after both were proxied | fixed (null `Instrumentation` into `makeApplicationInner`; `callApplicationOnCreate` last, as `handleBindApplication` does it) |
| `androidx.startup`'s `InitializationProvider` failed with "WorkManager is already initialized", because providers were published after `onCreate` rather than before | fixed by the same reordering |
| `IStorageManager.getVolumeList` refused, killing a guest through `Environment.isExternalStorageManager` | fixed (`mount` proxied) |
| `IAccountManager` was declared in `TARGETS` and installed by nothing, so the Play Store died asking about accounts — and a guest saw the host's real Google accounts | fixed (installed; the accounts a guest *should* see is a separate open question) |
| The `search` hook bound to nothing nine times a run: `ISearchManager` takes no String on API 35 | fixed (removed, and recorded as a deliberate omission the survey no longer nags about) |
| A guest's launch had no starting window and no transition animation, from two undocumented overrides on the stub theme | fixed (both removed; the platform's own behaviour) |
| **Every virtual activity rendered in software**: the substituted `ActivityInfo` carried no `flags`, and `Activity.attach` reads exactly that bit to decide. Slow for everything, fatal for anything drawing through a `RenderNode` | fixed (`android:hardwareAccelerated` parsed with the platform's two-level default; `WindowAttributesTest`, `t39`) |
| A landscape game opened portrait: the platform takes the orientation from the *stub's* manifest entry | fixed (`setRequestedOrientation` before the guest's `onCreate`; `t40`) |
| PAIRIP-signed apps killed themselves at startup: Play's licensing service is guarded by `com.android.vending.CHECK_LICENSE`, which the guest declared and UNIQUE did not | fixed (declared, with `READ_GSERVICES`, the FCM receive permission and the install-referrer bind) |
| `ApplicationInfo.metaData` was always null, so Play services threw `A required meta-data tag … does not exist` on an app that declares it | fixed (typed meta-data entries, resolved against the guest's own resources; `t41`) |
| `getPackageInfo` ignored its flags — `GET_ACTIVITIES`, `GET_SERVICES`, `GET_META_DATA` all returned null | fixed (`GuestComponents`; `t42`) |
| `getLaunchIntentForPackage` and `resolveActivity` for the guest's own components returned nothing | fixed (`GuestIntentResolution`, scoped to intents that name the guest; `t42`) |
| `getExternalFilesDir()` named a scoped-storage directory UNIQUE may not create, so external storage read as unavailable | fixed (the `mount` proxy rewrites `getVolumeList`, which `Environment` builds every external path from; `t44`) |
| `/proc/self/cmdline` said `com.unique:vapp0`, and the process table showed UNIQUE's own processes and sibling instances | fixed (`Process.setArgV0` as `handleBindApplication` does it, plus a process-table rewrite; `t43`) |
| A permission UNIQUE does not hold could never be granted to an instance — the switch was disabled and the row explained a problem the user could not act on | fixed (the switch asks Android for it, and offers the settings page when the platform has stopped asking) |
| **The `slotStarting` announcement was a no-op on a cold process** (`hostPackage` is set later in the graft), so a waiting caller re-warmed a process that was mid-graft and ActivityManager killed it: `bg anr`, forty-five seconds into a launch | fixed (the announcement falls back to the context's own package, which before the graft is UNIQUE's) |
| A 1.6 GB APK's signature was verified on the main thread during the graft, 2.5 s, tripping the OEM hang watchdog | fixed (loaded on its own thread, waited for only by a caller that asked for signatures) |
| `service download was not proxied: service not available`, ten times a run — there is no binder service called `download` | fixed (removed; `DownloadManager` is a `ContentResolver` client) |
| The hardware-acceleration bit was on the `ActivityInfo` and the window still came up without it on an Android 14 device | fixed (`Window.setFlags` before the guest's `onCreate`, which also records it as forced so `generateLayout` cannot clear it; `t39`) |
| A `@integer` reference in the guest's meta-data resolved to its resource id, because `LoadedApk.getResources()` is null during the graft | fixed (the bundle is rebuilt from the `Application`'s own resources once `makeApplication` has run; `t41`) |
| `getVolumeList` was refused with `callingPackage does not match UID` **after** `mount` was proxied: the external-storage guard shadows the generic caller-package rewrite, because the first shim that binds to a method wins | fixed (the guard carries the rewrite itself, and says so where a reader will look) |
| With that fixed, external storage still pointed at `/storage/emulated/0`: `StorageVolume.mPath` is a `File`, and writing a `String` into it is an `IllegalArgumentException` the surrounding `runCatching` turned into a quiet false | fixed (the field's declared type is read and the value built to match; the repoint is reported, not only its failure; `t44`) |
| **A second launch of an app that was already running produced nothing.** `FLAG_ACTIVITY_NEW_TASK` onto a task already running the component is `START_DELIVERED_TO_TOP`, and the intent delivered was UNIQUE's *stub* intent — which `ActivityThread` assigns to `Activity.mIntent`, so `getIntent()` answered with it from then on | fixed (`NewIntentItem` rewritten in place, keeping the `ReferrerIntent` the platform reads the referrer from; `t45`) |
| An implicit service start scoped to the guest resolved to nothing, because the platform has no filters for a package it never installed — the shape a great many SDKs use to reach their own worker | fixed (resolved against the guest's own manifest, and only when exactly one service matches; `t46`) |
| A Settings screen an app opens about itself named the guest in a `package:` URI, so the device's Settings had nothing to open and a cleaner app's onboarding dead-ended | fixed (retargeted to UNIQUE, whose uid is the one that would hold the access) |
| `getInstallerPackageName` threw `IllegalArgumentException: Unknown package` for the guest — unchecked, and called by nearly every analytics and update SDK on launch | fixed (answered as null, which is what a sideloaded app gets and what UNIQUE is; `t42`) |
| A Chromium renderer crash from the WebView test arrived two seconds into the *next* test and killed the process it had just started, reporting a Chromium fault as `t31` failing to resolve an implicit intent | fixed in the suite (`t30` retires the process that hosted the WebView, so the abort has nothing left to take with it) |
| A guest was held to **UNIQUE's** target SDK by two different mechanisms: `ActivityManagerService` compat checks against the calling uid, and the compat config `ActivityThread` installs in the process at bind time. An app built against Android 9 threw on `registerReceiver` and on `PendingIntent` | fixed (the export flag is supplied at its call site; `GuestCompatChanges` disables in-process changes the guest's target SDK predates, with the ids read from the platform) |
| `startForeground(id, notification)` asked for **every** foreground-service type UNIQUE declares, because `FOREGROUND_SERVICE_TYPE_MANIFEST` resolves against the stub's manifest entry | fixed (resolved against the guest's own entry; a guest that declares none gets `specialUse`) |
| `AppWidgetManager.getAppWidgetIds` was refused: `appwidget` was unproxied, and the package it checks is inside a `ComponentName` rather than a string | fixed (proxied, with a `ComponentName` rewrite in the guard) |
| An app enabling or disabling one of its *own* components — how a launcher icon is changed, and how a keyboard is turned on — was refused with `Attempt to change component state`, because the component belongs to a package the platform never installed | fixed (`GuestComponentState`, per instance and on disk; both intent resolvers and `ComponentInfo.enabled` read it) |
| A failed graft reported `message=java.lang.reflect.InvocationTargetException` and nothing else, which names UNIQUE's calling convention and nothing about the app | fixed (the chain is unwrapped to its root with the first frames, and the stack goes to `logcat` under the tag a device capture is filtered on) |
| A guest publishing a shortcut killed its own `Application.onCreate`: a `ShortcutInfo` carries its own package name and the platform checks each against the caller | refused honestly, not fixed (`SHORTCUT_PUBLISH_UNSUPPORTED`, returning the `false` the API already defines for a rate-limited caller). Routing a published shortcut back into the guest is a feature and is listed as not implemented |
| Three of the four client-side framework caches UNIQUE thought it was disabling did not exist under those names, and the one that mattered was not on the list at all — `ApplicationPackageManager` answers `getComponentEnabledSetting`, `getPackageInfo` and `getApplicationInfo` from the process, where no Binder shim can see them | fixed (the *classes* are named and every static no-argument `disable…Cache()` on each is discovered and called; three more were found this way, and the list can no longer go stale when a release renames one) |
| **Play services killed three guests seconds after they launched.** `GmsClient.getRemoteService` sends the guest's own package name to `com.google.android.gms`, which resolves the calling uid to UNIQUE's packages and answers `SecurityException: Unknown calling package name`, on a `Handler`, fatally | fixed (`com.google.android.gms` and `com.google.android.gsf` are hidden from everything a guest can ask the package manager, so an SDK takes its own "no Play services" path; `com.android.vending` stays visible. `VirtualPackageManagerHookTest`) |
| **No guest could type.** `EditorInfo.packageName` is checked against the calling uid before an IME is bound, and a mismatch is `INVALID_PACKAGE_NAME` — silently, with nothing logged in the app's process | fixed (`input_method` proxied under either of the two `$Stub` package names the interface has had, with a guard that rewrites the `EditorInfo` field rather than an argument) |
| **A guest's own `FileProvider` never published.** Providers were installed before the identity hooks, and `FileProvider.attachInfo` reads external storage while parsing its `<paths>` — under the guest's name, which the platform refuses | fixed (the registry installs after `VirtualIdentityHooks`, still before every guest component) |
| `SYSTEM_ALERT_WINDOW` was denied to every guest that asked, and it is install-time: no dialog exists, so no user refused it. UNIQUE did not declare it, and Android's overlay screen does not offer an app that does not | fixed (declared, which is what puts UNIQUE on that screen; App Details lists overlay, exact alarms and unrestricted background work with their real state and opens each one's screen) |

**Caveat on rendering.** The suite asserts the activity ran and produced its observations;
it does not look at the screen. Confirming that pixels appear is a two-minute manual step
in `docs/PHYSICAL_DEVICE_TEST.md`.

## Deliberately not implemented

| Surface | Phase | What it does instead |
|---|---|---|
| A broadcast arriving while UNIQUE itself is not running | 3 | `VirtualBroadcastRouter` holds the registrations in UNIQUE's main process, so it must be alive. Static registrations in the host manifest would close it, and need the actions known at build time |
| Implicit system broadcasts to a guest receiver | 3 | Android 14's `IMPLICIT_INTENTS_ONLY_MATCH_EXPORTED_COMPONENTS` matches implicit intents only against exported filters, and UNIQUE's own registrations are `RECEIVER_NOT_EXPORTED`. A sender inside UNIQUE must scope its intent |
| Hidden-API native fallback | 3 | `HiddenApi.nativeFallbackAvailable` is a constant `false` |
| Native property virtualization | 6/7 | `InstallStatus.NOT_IMPLEMENTED` |
| Every Google bridge body | 6 | Interfaces, routing and host-environment detection exist and are tested; no bridge has an implementation. `docs/GOOGLE_DEVICE_TEST.md` is the procedure that would settle each flow |
| Publishing a shortcut from a guest | 10 | A `ShortcutInfo` carries its own package name and the platform checks it against the caller; rewriting it fails the next check, which is that the shortcut's activity is a *main* activity of that package. Making them work means routing each shortcut's intents onto stubs, the way `PendingIntent`s are, and surviving UNIQUE's own updates. Refused with `false` — which `setDynamicShortcuts` already returns for a rate-limited caller — and reported as `SHORTCUT_PUBLISH_UNSUPPORTED` |
| Device profile regenerate | 7 | UI row says it is not available yet |
| WebView rendering on this environment | 6 | A WebView is created correctly in the guest with the instance's own data directory, but Chromium's renderer crashes on this emulator *outside* virtualization too, so rendering is `NOT_TESTED` rather than attributed to UNIQUE (`t30`) |
| URI permission grants between virtual processes | 3 | `grantUriPermission` across `:vappN` is not implemented |
| VirtualCore server interface | 3 | `onBind` returns null; the UI process owns the database for now |
| Play services, to a guest | 6 | Hidden entirely rather than half-served. Every SDK path that reaches `com.google.android.gms` ends in a uid check UNIQUE cannot satisfy, and the failure is a fatal `SecurityException` on a `Handler` — so an app that asks now finds no Play services and takes the path it already has for a phone without them. Making this work means implementing the bridges (row above), not hiding less |
| A diagnostics UI inside the app | 8 | Removed at the request of the person it was written for. Everything UNIQUE records goes to `logcat` under one tag, and `tools/device-log/analyze.py` reads a capture from any recorder app |

## Known limits that are not bugs

- **Any UNIQUE process started to publish a provider must finish inside the platform's
  publish timeout**, and that now includes UNIQUE's own `:core`, which publishes the
  router and shared authorities. On a badly loaded emulator `system_server` has killed it:
  `Killing …:com.unique (adj 0): timeout publishing content providers`. The window is far
  larger on hardware, but the ceiling belongs to the platform and grows with every
  provider added to a process.
- **A `:vappN` started to publish a provider must finish inside the same timeout.** On the verification emulator a cold virtual process can take tens of seconds
  to reach the graft, and `system_server` has been seen to give up on it:
  `Killing …:com.unique:vapp2 (adj 0): timeout publishing content providers`. On hardware
  the margin is far larger, but the ceiling is real and belongs to the platform.
- **Background ANRs on a loaded machine.** `Killing …:com.unique:vapp0 (adj 0): bg anr`
  appears when the device is thrashing: a cold wake is fork, load UNIQUE's code, install
  the interception layer, graft the guest, *then* run the component, and all of it inside
  what ActivityManager considers a background service start. It is the reason no test
  asserts a wall-clock budget (§17.1).

  What was a bug rather than a limit is what happened next: the stub is started
  `START_NOT_STICKY`, so nothing brought it back and the broadcast was **lost silently**.
  A cold delivery is now tracked in UNIQUE's main process and re-tried until the guest's
  receiver acknowledges it, up to three attempts ninety seconds apart
  (`COLD_BROADCAST_RETRY`, `COLD_BROADCAST_ACKNOWLEDGED`, `COLD_BROADCAST_GIVEN_UP`). The
  ANR itself remains a property of a loaded device; losing the broadcast to it does not.
  OEM background management on a real phone produces the same kill, which is why this is
  worth carrying to hardware.


- **A guest is never relaunched for a config change it did not declare.**
  `ActivityRecord.shouldRelaunchLocked` reads the *stub's* `android:configChanges`, and the
  stub declares every change it can, so a guest that did not declare `orientation` keeps
  the layout it loaded in the other one after a rotation. Calling `Activity.recreate()` for
  an undeclared change is the obvious answer and was tried: in the acceptance suite it
  resurrected an activity from an earlier test, which re-applied its landscape orientation
  and rotated the display around a permission request, destroying the activity that was
  waiting for the result. A stale layout is a cosmetic fault; a lost activity result is
  not. It stays a limit until there is a way to do it that cannot touch an activity with
  work in flight.
- **An implicit service intent with no package at all cannot be helped.**
  `ContextImpl.validateServiceIntent` throws in the *app's* own process, before any call
  reaches ActivityManager, so UNIQUE never sees it. What UNIQUE can resolve — and now
  does — is the far commoner shape, `setPackage(getPackageName())` with no component.
- **No AOT.** Since Android 10 an app cannot invoke `dex2oat`, so virtual apps are
  JIT-only and cold start is slower than an installed app. This is a platform property.
- **Attestation.** Play Integrity, Play Games and Play Billing are expected not to work
  and are recorded `UNSUPPORTED_FOR_NOW` — expected, not measured.
- **32-bit-only apps** on a 64-bit-only device.
- The Credential Manager route is the Google layer's **central hypothesis**, unverified.

## What a survey of real apps says is missing

`tools/apk-survey/survey.py` reads the `method_ids` table out of real APKs and reports
which system services they call. Across 63 apps from F-Droid, nine services real apps use
are not proxied — and three of those are the worse kind, named in
`SystemServiceHook.TARGETS` with nothing installing them, so they read as done:

| Service | Manager | Apps | State |
|---|---|---|---|
| `window` | `WindowManager` | 60/63 | declared, never installed |
| `phone` | `TelephonyManager` | 34/63 | not in `TARGETS` |
| `download` | `DownloadManager` | 5/63 | not in `TARGETS` |
| `account` | `AccountManager` | 3/63 | declared, never installed |
| `media_session` | `MediaSessionManager` | 3/63 | declared, never installed |
| `device_policy` | `DevicePolicyManager` | 3/63 | not in `TARGETS` |
| `media.camera` | `CameraManager` | 3/63 | not in `TARGETS` |
| `telecom` | `TelecomManager` | 1/63 | not in `TARGETS` |
| `media_router` | `MediaRouter` | 1/63 | not in `TARGETS` |

`account` matters beyond compatibility: until it is installed, `AccountManager.getAccounts()`
from inside a guest returns the **host's real Google accounts**, which is a per-instance
identity leak rather than a missing feature.

None of this is a claim that these apps fail — a DEX reference is not a call. It is a
ranking of what to proxy before a phone finds it, and it is the check that would have
caught `restrictions`, `locale` and `connectivity` before one did.

## Next steps, in order

1. **The eighteenth phone run.** Two changes, in two subsystems, and the log separates
   them. In order of what would matter most if it were wrong:
   - **Google sign-in.** Tap the Google button in Standoff 2 and watch for
     `GOOGLE_SIGN_IN_RETARGETED … to=com.unique serverToken=…`, then whether an account
     picker appears at all. The `signin` check reads the gap between the handoff and the
     answer: under two seconds is the identity refusal again, longer means the picker was
     drawn and whatever came back is a *different* answer. If Google says
     `DEVELOPER_ERROR` after an account is chosen, that is the OAuth-client wall and the
     first time this project has actually reached it.
   - **A guest's own data still being there.** `sqlite=8` replaces SQLite's file
     operations for every database in the process, and a redirect gone wrong shows as an
     app that looks empty rather than one that crashes. **Open two or three apps with
     state before the game** — the file manager, something signed in — and say whether
     their data survived.
   - `GUEST_PATHS_PUBLISHED … code=true sqlite=8 data=true`, and the `paths` check green.
     `data=false` with `sqlite=0` means the VFS interface was not reached and the log says
     which of the four reasons; `data=false` with `sqlite=8` means the fault is downstream
     of the redirect and is new.
   - `io_redirect: hooked libsqlite.so=2+abs+packed` — the `+packed` confirms the
     seventeenth run's diagnosis directly. Its absence retracts it.
   - Then the game: whether `Anticheat/VirtualSpaceWarning` still appears, and — only
     after a login that completes — whether `AuthRestrictions/VirtualSpaceMessage` does.
     The notice comes *after* the login, so a run without one proves nothing.
2. **The fifteenth phone run's open items.** This pass hooks every library in every guest's
   process and changed the identity a guest reports for itself; neither can be observed
   anywhere but on a phone. In order of what would matter most if it were wrong:
   - **Every app that ran before still running, and still holding its own data.** A guest
     that cannot find its saved games is what a redirect gone wrong looks like; it does not
     announce itself. Open something with state — a launcher, a browser with a session, an
     app that was signed in — before looking at the game.
   - `GUEST_PATHS_PUBLISHED … code=true data=true`, and the `paths` check green. `data=false`
     is not a crash and not a regression — it means a gate refused, and `detail=` says which
     one and why.
   - `io_redirect: hooked <lib>=<n>` for the libraries the redirect reached, and
     `io_redirect: nothing in this process imports: …` for any symbol in the table nothing
     spells that way. The fourteenth run's bug was invisible for want of those two lines.
   - `PROC_VIEW_INSTALLED … leaked=0`, still, now that the check reads around the view
     rather than through it.
   - The game: whether `Anticheat/VirtualSpaceWarning` still appears. That is the flag this
     pass is aimed at. `AuthRestrictions/VirtualSpaceMessage` on sign-in is a *different*
     ceiling and is not expected to move — see `docs/GOOGLE_SIGN_IN.md`.
3. **The seventh phone run's open items**, which the verification emulator still cannot
   answer: it has no Play services, no IME, no `Android/obb` to import from and no
   code-virtualization protector to break. What to watch for, in the order the sixth run
   failed:
   - `GUEST_OBB_IMPORT outcome=IMPORTED files=… bytes=…` at import, and **no**
     `Skipping OBB loading` from Unity afterwards. `SOURCE_UNREADABLE` means all-files
     access has not been granted — App Details → Special access → *Access to all files*.
   - `IO_REDIRECT_INSTALLED … excluded=libgrave.so,libunique_native.so`, and the Unity
     game that died of `SIGBUS` staying up. A *different* protector shows as the `native`
     check naming a library; write it into
     `runtime/native/<vuid>/<package>.exclude` and launch again.
   - No `SecurityException` from `getNotificationChannel`, no
     `cannot schedule job in <guest>`, no `Not allowed to bind to service … MESSAGING_EVENT`.
   - `PENDING_INTENT_RECEIVER_ROUTED` where `PENDING_INTENT_RECEIVER_UNSUPPORTED` used to
     be, and `PENDING_BROADCAST_DELIVERED started=true` when one fires.
   - A guest with a text field brings up the phone's own keyboard, which no run has yet
     shown either way.

   `docs/PHYSICAL_DEVICE_TEST.md` is the sequence — start a log recorder, work the twelve
   steps, send the capture. `tools/device-log/analyze.py` reads it with no toolchain at
   all, and the two checks added for the sixth run mean an asset or protector fault names
   itself now instead of looking like the app's own bug.
4. ARM64 native code, a real GPU driver, a hardware Vulkan ICD, WebView rendering and a
   real engine app — five things only a phone can answer, and every one of them is
   `NOT_TESTED` until it does.
5. A temporary URI grant handed *into* a guest — the case a photo picker actually uses.
   Sharing outward works (`t34`) and the inbound request is at least well-formed (`t36`),
   but arranging a real grant needs a third APK: instrumentation runs under the target
   app's uid and can neither write another app's files nor grant for its authority.
5. Google, which needs a device with a Google stack. `docs/GOOGLE_DEVICE_TEST.md` is the
   procedure, in the order that makes one failure explain the next.
6. A real engine sample (Unity/Unreal). None is available in this environment, and no
   claim will be made without one.
7. **Re-running the `verify` build.** Everything after `t38` has only been run on
   `debug`, and the `verify` build is what a tester installs — the difference is not
   cosmetic: it is unminified but non-debuggable, so ActivityManager holds it to the
   ordinary ten-second process-start budget.
8. **Verifying the minified release build.** The artifact a tester is actually given is
   already covered: `BUILD_TYPE=verify ./tools/verify-device.sh` runs the whole suite
   against the exact APK in `dist/` — not a near neighbour of it — and passed 38 of 38
   against the suite as it stood then.
   What is left is R8. The minified build assembles and signs, and its keep rules hold
   structurally: the stub pool, the router and shared providers, `UniqueNative` and the
   native entry points all survive, checked on the artifact. But the suite cannot be
   pointed at it — `androidx.tracing.Trace` reaches `AndroidJUnitRunner.onCreate` from
   R8's *classpath* rather than from program input, so no `-keep` rule applies — and a
   class surviving is not the same as a reflective lookup succeeding.

See `docs/COMPATIBILITY.md` for the per-application matrix and
`docs/PHYSICAL_DEVICE_TEST.md` for the physical-device checklist.
