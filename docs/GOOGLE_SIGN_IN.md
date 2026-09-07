# Google sign-in from inside UNIQUE: what blocks it, and what would not

The question this answers is the one that keeps being asked, and it deserves a document
rather than a sentence in a release note: **can a virtual app sign in with Google the way
the same app does when it is installed normally?**

The answer is not "no". It is: **not through the phone's own Play services, ever, and
there is exactly one route that could work.** Both halves matter, and this file is the
evidence for each.

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
on an unrooted device.

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
  and `GooglePlayIntegrityCheckRpcException` (`docs/STANDOFF2.md`). Whether the server
  *requires* a valid token or only records it is unknown and cannot be known from the
  client.
- **ChatGPT** answers `error_code: preauth_cookie_device_check_failed`, which is OpenAI's
  own device check, not Google's. Same class of problem, same ceiling, different vendor.

An honest summary: Mode A would let a guest present the right identity. It would not make
the guest indistinguishable from an installed app to a server that asks the platform.

---

## 3. The other route, which fixes a different set of apps

Apps that sign in through the **browser** — AppAuth, Custom Tabs, an `ACTION_VIEW` to an
authorize URL — fail for a completely different reason, and that one is fixable without
GMS at all.

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

It does **not** help Standoff 2, which uses the native Google Sign-In API rather than a
browser flow, and it does not help ChatGPT's device check.

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

---

## 5. Where this leaves the engine

`core/google` reports `UNSUPPORTED` for `SIGN_IN` and `OAUTH_WEB`, with the reason named in
the rationale string rather than a mode that reads as working. That is deliberate: a sentence
saying what to do instead is worth more than a code path that fails at the end.

The path work in `GuestIdentityPaths` closes the **virtual-space verdict** — the
`AppVerification` report that rides on `GoogleAuthRequest` and names the APK's path. It does
not close the sign-in the report rides on. Those are different problems with different
ceilings, and a build that fixed one is not a build that half-fixed the other.
