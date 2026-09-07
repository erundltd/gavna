# Signing in from inside UNIQUE: what blocks it, and what does not

The question this answers keeps being asked, and it deserves a document rather than a
sentence in a release note: **can a virtual app sign in the way the same app does when it
is installed normally?**

The answer depends entirely on *who is asked to vouch for the app's identity*, and there
are two answers, not one:

| The SDK asks | Example | Where UNIQUE stands |
|---|---|---|
| **Play services** — another process, resolving the caller by kernel uid | Google Sign-In | Not through the route UNIQUE takes today. **Not proven impossible** — see the note below |
| **the app's own `PackageManager`** | Facebook, VK, most SDKs | **Already correct** — proven on the phone in §3 |

> **A claim made here was too strong, and is withdrawn.** This document said Google sign-in
> from a virtual space could not work "ever, on an unrooted phone". The user reports that
> in other virtual spaces on this game the account picker returns, the login completes and
> the game loads — what appears afterwards is the virtual-space notice, not a sign-in
> failure. One person's account of an app is not a measurement, but it is evidence, and it
> is more evidence than the reasoning it contradicts. §1 describes accurately what UNIQUE's
> own route does and why it fails; it does not establish that no route exists, and the
> difference matters. **How another engine gets a Google sign-in through is now the most
> valuable unknown in this file**, and it is answerable: one log from such an engine, doing
> a sign-in that works, would show the route.

So "you cannot sign in inside a virtual space" is false as a general statement, and may be
false even for Google. For Standoff 2 the shortest route UNIQUE can build today is Facebook
or VK (§3) — but the thing that actually stops the game being playable is what comes
*after* the login, not the login.

---

## 1. What actually happens today

Play services identifies its caller by **kernel uid**, not by what the caller says it is.
The ninth phone run is where that stopped being theory:

```
GmsClient.getRemoteService(callingPackage="com.axlebolt.standoff2")
  → GMS: getPackagesForUid(Binder.getCallingUid())
  → ["com.unique"]
  → SecurityException: Unknown calling package name 'com.axlebolt.standoff2'
```

thrown on a `Handler`, where no app can catch it, which is why it killed three apps in one
run. UNIQUE's answer is `GmsBrokerBinder`: the calling package in the request is rewritten
to `com.unique`, which is *the truth about the calling uid*, and the bind then succeeds.
Maps, Firebase, ads, Dynamite and FCM work because of it — the tenth run onward.

**Sign-in is the one call that rewrite cannot fix.** An OAuth client is registered by its
developer as a pair:

```
package name         com.axlebolt.standoff2
signing certificate  SHA-1 of Axlebolt's release key
```

and Google checks both, resolved from the *system's* PackageManager, in the Play services
process. Rewriting the request to say `com.unique` makes the bind legal and the sign-in
wrong: Google is now being asked for a token for UNIQUE, which has no OAuth client for the
game's backend. The thirteenth run shows exactly this shape — sign-in reaches Play
services, four attempts, and comes back cancelled in under a second.

So there are two different walls, and they are often confused:

| Wall | What it checks | Can UNIQUE change it? |
|---|---|---|
| "Unknown calling package name" | the uid may claim that package | **Yes** — done, since run 10 |
| sign-in identity | package **and certificate**, from the system PackageManager | **No**, not for the host's Play services |

The second one is not a check UNIQUE participates in. It happens in another process, against
records in `system_server`. There is no hook, no rewrite and no permission that reaches it
on an unrooted device — **by this route**. What is described above is what UNIQUE does:
the sign-in intent leaves the space (`ACTIVITY_IMPLICIT_LEFT_GUEST … handledByHost=
com.google.android.gms` in every run), the host's Play services answers it as UNIQUE, and a
token for UNIQUE is no use to the game's server.

That is a description of one route failing, not a proof that every route fails, and the
note at the top of this file says why the distinction now matters. Two things that would be
worth knowing and are not known: whether the game asks for an ID token at all or only for
the account, and what an engine that does get through actually does differently.

---

## 2. The one route that would work, and why

**Run Play services inside the virtual space.** Then the process that asks "who is
calling?" is one whose `PackageManager` is *UNIQUE's*, and UNIQUE answers with the guest's
own package name and the guest's own signing certificate — which it has, because it holds
the guest's real APK.

This is Mode A in `ARCHITECTURE.md` §9.3, it is designed, and it is **not implemented**.

What it requires, honestly listed:

- **GMS, GSF and Play Store imported from the device's own installation.** UNIQUE never
  bundles or redistributes Google binaries. The user's phone already has them.
- **`getPackagesForUid` and `getPackageInfo(GET_SIGNATURES)` answered by the virtual
  PackageManager** for the in-space GMS. UNIQUE already virtualizes both; what is missing
  is a GMS that asks *it* rather than the system.
- **Routing**: the guest's `com.google.android.gms.auth.GOOGLE_SIGN_IN` intent must stay
  inside the space. Today it leaves — `ACTIVITY_IMPLICIT_LEFT_GUEST … handledByHost=
  com.google.android.gms` in every run — and is answered by the host's copy.
- **Device check-in.** In-space GMS has to register with Google as a device. This fails on
  some phones and there is no way to know which without running it.
- **Cost**: 150–250 MB of RAM for the GMS process set, per space. Mode A is therefore
  opt-in per instance in the design, not the default.

### What Mode A still would not fix

**Attestation.** Play Integrity, SafetyNet, DroidGuard and every vendor equivalent attest
the *device and the app as the platform sees them*. An in-space GMS cannot produce a
hardware-backed attestation for a package the platform has never installed. `README.md`
says UNIQUE is not an attestation bypass; that stands, and Mode A does not change it.

This matters for the two apps that have been tested:

- **Standoff 2** ships `Google.Play.Integrity`, `IntegrityManager`, `RequestIntegrityToken`
  and `GooglePlayIntegrityCheckRpcException` (`docs/STANDOFF2.md`). It does not bind the
  Play Integrity service *before or during* a sign-in attempt — ChatGPT, at the same stage
  of its own failed login in the same log, binds it six times — so the game does not gate
  the sign-in attempt on it. **What it does after a successful login is unknown**: no
  session has ever completed one, and the three runs are 59–93 seconds each. Do not read
  the absence as more than that; see `docs/STANDOFF2.md` for the correction and for the
  Huawei argument that was made for it and does not hold.
- **ChatGPT** answers `error_code: preauth_cookie_device_check_failed`, which is OpenAI's
  own device check, not Google's. Same class of problem, same ceiling, different vendor.

An honest summary: Mode A would let a guest present the right identity. It would not make
the guest indistinguishable from an installed app to a server that asks the platform.

---

## 3. The route that is already half-working, and the phone proved it

There is a second identity mechanism, it is the one most SDKs use, and **UNIQUE already
satisfies it**. The thirteenth phone run contains the proof, in a line nobody was looking
for.

Standoff 2 also offers Facebook login. The user tried it, and the Facebook SDK announced
the app it thought it was running inside:

```
V com.facebook.unity.FB: Init({"appId":"752573801798020", …})
D com.facebook.unity.FB: KeyHash: lcG7acvUIg0k4FQSQmAbyw1tN0o=
```

A Facebook key hash is `base64(SHA-1(signing certificate))`, computed at run time from
`getPackageInfo(getPackageName(), GET_SIGNATURES)`. Decode both and compare:

| | SHA-1 |
|---|---|
| UNIQUE's own signing key (`app/debug.keystore`) | `64:DB:A6:…:47:6B` |
| what the SDK computed, on the phone, inside UNIQUE | `95:C1:BB:…:37:4A` |

They are different, which means the SDK was handed **Standoff 2's own certificate**. It
asked the app's own `PackageManager`, that is UNIQUE's virtual one, and UNIQUE answered
with the guest's real signature — because it holds the guest's real APK.

**That is the whole difference between Google and everyone else.** Google asks Play
services, which is another process and resolves the caller by kernel uid, and UNIQUE has no
part in that conversation. Facebook, VK, and every SDK that identifies its host app through
the app's own `PackageManager` ask a question UNIQUE answers correctly today.

So for this game the identity half of a Facebook login is already solved. What is not is the
**return leg**, and the run shows precisely where it breaks:

```
FB.LoginWithReadPermissions({"scope":"public_profile,email"})
  → FBUnityLoginActivity     routed onto a stub, launched
  → FacebookActivity         routed, launched
  → CustomTabMainActivity    routed, launched
  → ACTIVITY_IMPLICIT_LEFT_GUEST action=VIEW data=https handledByHost=com.android.chrome
```

Three of the SDK's four activities ran inside the space. The fourth opened Chrome, and from
there the redirect — `fb752573801798020://authorize/…` — is handed to
`PackageManagerService`, which has never installed a package declaring that scheme. The
sign-in completes on Facebook's side and arrives nowhere.

### What closing it takes

Keep the browser leg inside the guest's own process:

1. Intercept the `ACTION_VIEW` for the authorize URL instead of letting it leave.
2. Open it in a `WebView` in an activity UNIQUE owns, running in the guest's process, with
   the guest's own cookie jar.
3. Watch navigation for the redirect scheme the guest's manifest declares.
4. Deliver it to the guest's own activity **directly**, as an `Intent`, never through
   `PackageManagerService`.

Nothing in that needs the host's PackageManager to know the guest exists, and nothing in it
is blocked. It is the highest-value unbuilt piece in this engine: it is what a working login
for Standoff 2 actually depends on, and it fixes the same wall for every app that signs in
through a browser.

It does **not** help Google sign-in, which never opens a browser.

### The other half, which comes first — and is the real blocker

Every auth request shape in Standoff 2's binary — `GoogleAuthRequest`, `VkAuthRequest`,
`FacebookAuthRequest`, `GameCenterAuthRequest`, `TestAuthRequest` — carries the same
`AppVerification` report (`docs/STANDOFF2.md`). So the virtual-space verdict is **not**
specific to Google, and a Facebook login would meet it for the same reason.

The user's account of the game settles which of the two walls actually decides
playability: **the sign-in completes, the game loads, and then the notice appears.** So in
a virtual space that gets a login through, the login is not what stops the game — this is.

The order is therefore a dependency and not a preference:

1. Close the virtual-space verdict — `GuestIdentityPaths`, written, waiting on a phone.
2. Then the login route, whichever proves cheapest to build.

Doing the second first produces a login that completes and a game that then refuses, which
is exactly the state other engines are already in, and nothing is learned from reaching it
a second time.

---

## 3b. The general shape of the browser problem

Apps that sign in through the **browser** — AppAuth, Custom Tabs, an `ACTION_VIEW` to an
authorize URL — all fail the same way, and that way is fixable without GMS at all.

The sixth run settled the diagnosis: the outbound half works, and the **return** half
cannot happen. The redirect is an `ACTION_VIEW` for `myapp://callback`, Chrome hands it to
`PackageManagerService`, and the activity declaring that scheme belongs to a package the
platform has never installed. Nothing resolves it. The user is left on a browser page with
a sign-in that completed on Google's side and arrived nowhere.

The fix is to keep the whole exchange inside the guest's own process:

1. Intercept the `ACTION_VIEW` for the authorize URL instead of letting it leave.
2. Open it in a `WebView` in an activity UNIQUE owns, running in the guest's process.
3. Watch navigation for the redirect the guest's manifest declares.
4. Deliver it to the guest's own activity **directly**, as an `Intent`, never through
   `PackageManagerService`.

Nothing in that needs the host's PackageManager to know the guest exists. It is a real
piece of work and it is not blocked on anything.

It does not help ChatGPT's device check, which is attestation rather than identity. It does
not help Google sign-in from any app. It **does** help Standoff 2, through Facebook — see
§3 — which is the finding that reorders this whole document.

---

## 4. What is not a route

Recorded so that each is not re-proposed:

- **Signature spoofing on the host's Play services.** Needs root or an Xposed-class module
  in `system_server`. UNIQUE is explicitly an unrooted, single-APK engine.
- **Sharing the host's account through `AccountManager`.** Auth tokens are issued per
  calling uid and per OAuth client. The uid is UNIQUE's; the client is the guest's. The
  mismatch is the same wall in a different place.
- **Rewriting the certificate in the request.** The certificate is never in the request.
  Play services reads it from the system, from the uid.
- **Patching the guest's APK to use a web OAuth client.** It changes `ApkHash`, which
  Standoff 2 sends in its `AppVerification` report, and the server has a message for
  exactly that: `FilesNotAuthenticMessage`.
- **Assuming the Facebook route generalises to Google.** It does not, and the reason is
  the table at the top: the two SDKs ask different processes. Nothing about §3 working
  makes §2 any closer.

---

## 5. Where this leaves the engine

`core/google` reports `UNSUPPORTED` for `SIGN_IN` and `OAUTH_WEB`, with the reason named in
the rationale string rather than a mode that reads as working. That is deliberate: a sentence
saying what to do instead is worth more than a code path that fails at the end.

The path work in `GuestIdentityPaths` closes the **virtual-space verdict** — the
`AppVerification` report that rides on `GoogleAuthRequest` and names the APK's path. It does
not close the sign-in the report rides on. Those are different problems with different
ceilings, and a build that fixed one is not a build that half-fixed the other.
