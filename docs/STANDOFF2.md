# Standoff 2, and how it knows

What this game checks, read out of the game itself, because two passes had been spent
guessing at it. `com.axlebolt.standoff2` 0.39.3, versionCode 203908, ARM64.

Everything below comes from the shipping build: the 105 MB APK and the 1.7 GB expansion
file. Nothing here is inferred from behaviour or from what similar games do, and where a
conclusion is a reading rather than a quotation it says so.

---

## Where the evidence is, and where it is not

The obvious place to look is not the right one. The game is Unity 6 on IL2CPP, and the
build reports its engine as `6000.3.13f1_patched` — a Unity fork of Axlebolt's own — so
the usual layout does not apply:

| Looked for | Found |
|---|---|
| `assets/bin/Data/Managed/Metadata/global-metadata.dat` in the APK | not there |
| the same, in the expansion file | not there — the OBB holds `data.unity3d`, DLC bundles and FMOD banks, and nothing else |
| `libil2cpp.so` | not there |

The metadata is **linked into `libunity.so`**, which is 236 MB in this build against the
20–40 MB an unmodified engine produces. That is also where it is *readable*: the IL2CPP
string, type, method and field name tables are in the binary in the clear. `strings` on it
yields 378,000 runs, and the game's own C# identifiers are among them.

The expansion file therefore answers nothing about detection and did not need to be
downloaded at all. Its index was read over HTTP range requests — 1,384 entries, none of
them metadata — which is recorded here because the next person to ask this question should
not spend 1.7 GB finding out.

---

## The two things that say "virtual space", and they are not the same thing

There are **two** separate mechanisms, with two separate messages, and conflating them
sends any fix to the wrong layer.

### 1. The in-game warning: `Anticheat/VirtualSpaceWarning`

A localization key, in the `Anticheat` section, beside the game's own alert strings:

```
Anticheat \0 get_VirtualSpaceWarning \0 VirtualSpaceWarning \0 ArmsRace \0 …
```

It is set from `Axlebolt.Standoff.Anitcheat.AntiCheatManager` — the namespace is spelled
that way in the binary. The class's names are obfuscated to fifteen-character garbage
(`FBGEDCEFHEBEGGF`), with four survivors that were not renamed because they are used
reflectively or as constants:

```
VirtualSpaceDetected      PackageName
<ScanDirectoryRecursive>b__25_0        <GetUnityPlayer>b__30_0
<UpdateCoroutine>d__4
```

`VirtualSpaceDetected` is the flag. `UpdateCoroutine` means it is re-evaluated on a timer
rather than once at startup. `GetUnityPlayer` means it reaches the Android side through
`com.unity3d.player.UnityPlayer`, and `ScanDirectoryRecursive` means it walks directories.

### 2. The login refusal: `AuthRestrictions/VirtualSpaceMessage`

A different key, in a family that is all about being refused a login:

```
AuthRestrictions
    get_FilesCheckFailedMessage      FilesCheckFailedMessage
    get_FilesNotAuthenticMessage     FilesNotAuthenticMessage
    get_HackingSoftFoundMessage      HackingSoftFoundMessage
    get_RootFoundMessage             RootFoundMessage
    get_UnofficialVersionMessage     UnofficialVersionMessage
    get_VirtualSpaceMessage          VirtualSpaceMessage
```

beside `YouAccountIsBanned`, `YouDeviceIsBanned`, `YourSessionIsKicked` and the
`GooglePlay*` sign-in error codes. These are **server verdicts**: the client shows the
message the server names.

---

## What the client sends, and to which request it attaches it

The report is a protobuf message, and its shape is in the metadata field-name table:

```protobuf
message AppVerification {
  bool            IsRooted;
  string          ApkHash;
  repeated string JsonForbiddenApps;
  string          Path;
  string          ContentHash;
  map<string, …>  AppSnapshot;
  …               N;              // RSA modulus
  …               E;              // RSA exponent
}
```

`N` and `E` are an RSA public key travelling with the report, so the payload is signed or
sealed client-side: it is not a field an intermediary edits.

And this is the part that matters most:

```
GoogleAuthRequest
    get_AuthGoogle  set_AuthGoogle
    get_AppVerification  set_AppVerification
    AuthGoogleFieldNumber  authGoogle_
    AppVerificationFieldNumber  appVerification_
```

**`AppVerification` is a field of `GoogleAuthRequest`.** The environment report is not a
background telemetry ping; it is *part of the sign-in call*. Every auth request shape in
the file — `TestAuthRequest`, `VkAuthRequest`, `FacebookAuthRequest`, `GameCenterAuthRequest`
— carries a `Verification` alongside the credential.

Which means: signing in and being told the game is running in a virtual space are the same
event, seen from two sides. An engine that fixes the crash on the way to the Google account
picker has not touched this at all.

The neighbouring names in the string table name the rest of what is collected:

```
ApkAndObb  ApkCertCompLen  ApkCertLen  ApkFiles  ApkHash  ApkLibs  ApkPath
AppEnvironment  AppProcs  AppSnapshot  AppVerification
```

— the APK's path, its hash, its certificate length, its file and library lists, the OBB
alongside it, and a list of processes.

---

## What it reads to decide, and this is the whole answer

The client's Java-side surface is small and completely explicit. Every JNI name the game
references, counted in `libunity.so`:

| Referenced | Times |
|---|---|
| `sourceDir` | 4 |
| `getApplicationInfo` | 3 |
| `getPackageCodePath` | 2 |
| `getPackageManager` | 2 |
| `currentActivity` | 2 |
| `getPackageName` | 2 |
| `nativeLibraryDir` | 1 |
| `getInstalledApplications` | 1 |
| `getFilesDir` | 1 |
| `getExternalFilesDir` | 1 |

That is the identity-path surface and nothing else. There is no `/proc/self/maps` in it, no
`dl_iterate_phdr`, no mount-table walk. `sourceDir` four times and `getPackageCodePath`
twice, from a class holding a flag called `VirtualSpaceDetected`.

Inside UNIQUE, those four values are:

```
sourceDir          /data/user/0/com.unique/files/virtual/apk/com.axlebolt.standoff2/203908/base.apk
publicSourceDir    …the same
nativeLibraryDir   /data/user/0/com.unique/files/virtual/apk/com.axlebolt.standoff2/203908/lib/arm64-v8a
getFilesDir()      /data/user/0/com.unique/files/virtual/users/0/data/com.axlebolt.standoff2/files
```

Every one of them names another package. An installed copy of this game cannot produce any
of them, no comparison is needed to see it, and the game sends the first of them to its own
server as `Path`.

**So this is the detection.** It is not `/proc`, it is not the process list, it is not an
emulator check. It is four getters.

### The hard-coded path list, which is *not* it

There is one literal list of paths in the binary, pipe-separated:

```
data/user/0/com.kittenware.skillz|data/data/user/0/com.kittenware.skillz|
data/user/0/com.skillz.kitso2hider|data/data/user/0/com.skillz.kitso2hider|
data/user/0/io.va.exposed/virtual/data/user/0/com.kittenware.skillz|
data/user/0/io.va.exposed/virtual/data/user/0/com.skillz.kitso2hider|
/data/data/com.topjohnwu.magisk
```

Two cheat packages, each looked for directly **and** inside VirtualXposed's virtual tree
(`io.va.exposed/virtual/…`), plus Magisk. This is the `CheatDirectories` list, and it is a
*cheat* detector that happens to know one virtualization engine's layout — not a
virtualization detector. UNIQUE's tree is not in it, and adding UNIQUE to it would take
someone at Axlebolt writing it down. It is recorded here so that it is not mistaken for the
mechanism above.

### Play Integrity is in the build, and what the phone can and cannot say about it

`Google.Play.Integrity`, `IntegrityManager`, `RequestIntegrityToken`, `environmentIntegrity`
and `GooglePlayIntegrityCheckRpcException` are all in the build. Play Integrity attests the
*calling package and certificate*, which inside UNIQUE is UNIQUE's. Nothing in this engine
changes that, and nothing here should be read as suggesting otherwise — `README.md` says
UNIQUE is not an attestation bypass, and that stands.

**A measurement was made here and it was overstated. The correction, first.**

What was written was "the game never binds the Play Integrity service at all", from three
phone runs. What those runs actually contain:

| Run | Length | How far the game got |
|---|---|---|
| 11 | 93 s | launch → login screen → sign-in attempt failed |
| 13 | 59 s | launch → sign-in attempt failed |
| 14 | 59 s | launch → sign-in attempt failed |

**No session has ever completed a login.** Every one ends at or before the sign-in refusal,
and the longest is a minute and a half. Play Integrity in a game is most naturally called
*after* authentication — with the session, on a match, or on a timer — so a minute of
pre-login activity says nothing about it. "Never called" was not measured; "not called in
the first minute of a failed login" was.

**What the evidence does support, stated at its real width.** Across those three runs:

| Bound by the game | Times |
|---|---|
| `…expressintegrityservice.BIND_EXPRESS_INTEGRITY_SERVICE` | 0 |
| `com.google.android.gms.safetynet.service.START` | 3 — once per run |

and the single SafetyNet bind arrives inside the ordinary GMS-common initialisation,
milliseconds before `com.google.android.gms.usagereporting.service.START`, which is what
Firebase and Crashlytics do on every start.

In the same fourteenth log, at the same stage of its own failed login, `com.openai.chatgpt`
binds express-integrity **six** times — because its device check is a *pre-auth* one, which
is what `preauth_cookie_device_check_failed` says. So the narrow, supported claim is:

> **Standoff 2 does not gate the sign-in attempt itself on Play Integrity.** Whether it
> calls Play Integrity after a successful login is unknown and unmeasured, because there
> has never been a successful login.

**And one argument that was made for this and is wrong.** "A game that hard-required Play
Integrity would be dead on phones without Google, therefore Standoff 2 cannot require it" —
Axlebolt ships a **separate build for Huawei AppGallery**. A separate build is exactly how a
developer hard-requires Google services in the Google Play build without losing that market.
The argument does not hold and should not be relied on.

### When the notice actually appears, from someone who has seen it

Written here from the user's own account of the game's behaviour, because it is the only
observation of the notice that exists — no session inside UNIQUE has ever reached it:

> the notice appears **after** signing in — a Google account is chosen, or a Facebook login
> completes, the game loads, and then the notice comes up.

Two consequences, and a retraction.

**The retraction.** A previous version of this section proposed testing the path work by
launching the game and *not* signing in, on the theory that
`Anticheat/VirtualSpaceWarning` is client-side and runs on a timer. That test is worthless:
the notice does not appear before a login, so a run without one proves nothing either way.

**The notice is post-authentication**, which is what `AppVerification` being a field of
`GoogleAuthRequest` predicts. The report travels *with* the credential; the server reads
`Path`, `ApkHash`, `ApkFiles`, `AppSnapshot` and the rest, and answers. Whether what the
player sees is the server's `AuthRestrictions/VirtualSpaceMessage` or the client's
`Anticheat/VirtualSpaceWarning` firing once the main menu exists cannot be told apart from
the outside — the distinguishing question is whether the game can still be played
afterwards. Either way the path work is aimed at the right thing, because the client flag
and the server report read the same four getters.

**And the login itself evidently completes elsewhere.** The account picker returns, the
game loads. So a virtual space *can* get through a Google or Facebook sign-in on this game;
what it then hits is this notice. That is the wall worth spending on, and it is the one
UNIQUE is already closing.

---

## What this means for UNIQUE, stated as a decision and not as a plan

The `/proc` view shipped one pass earlier closes a real vector and closes nothing this game
uses. That is worth saying plainly rather than quietly leaving it as an implied win: it was
built from reasoning about what a check *would* read, and the check reads something else.
It stays, because the reasoning was right about the class of app and wrong only about this
one, and because the leak it closes is real.

The change this game needs is the one already recorded as the next step in `STATUS.md`, now
with evidence behind it instead of a guess:

**A guest's Java-visible paths have to be shaped like an installed app's, and those paths
have to resolve.**

The second half is what makes it work rather than a cosmetic change, and it is the hard
half. There is no directory on the device that UNIQUE can create and that does not name
`com.unique`: `/data/app/…` and `/data/user/0/<guest>` both belong to the platform. So the
public path can only be made real by redirection — and the redirection UNIQUE has is a PLT
patch in the *guest's own* libraries, which does not cover the framework's own opens. The
class loader (`DexPathList`), `AssetManager.addAssetPath` and every `java.io.File` in the
guest go through `libjavacore.so` and `libandroidfw.so`, which are the platform's.

So it needs the interception widened to those libraries, with UNIQUE's own file operations
in the same process exempted. That is the VirtualApp architecture and it is a large change
with the largest possible blast radius — every file operation of every guest. It is not
one to make in the same build as three other fixes, because a log that then goes wrong
would not say which change did it.

What it is *not* is speculative any more. The surfaces are the four getters above, the
report field is `Path`, and the request it rides on is `GoogleAuthRequest`.

---

## The network stack, as far as the APK shows it

Asked directly: is the game's networking — matches and the rest — visible in the APK?

**The shape of it is completely visible. None of the logic is.** The same property that
made the detection readable does it here: IL2CPP metadata is linked into `libunity.so` in
the clear, so every C# type, method and field *name* is in the binary, and protobuf's C#
backend additionally embeds each `.proto` file's descriptor as base64 — which means field
*numbers and types* survive too. What does not survive is anything that was compiled:
method bodies are ARM64 machine code, and the class names around them are the same
fifteen-character garbage the anti-cheat's are.

There are three layers and they are not the same technology.

### 1. The meta backend — protobuf, package `com.axlebolt.bolt`

Everything that is not the shooting: auth, lobby, matchmaking tickets, inventory,
marketplace, clans, chat, stats, battle pass. **1,081** distinct `*Request` / `*Response`
type names are in the string table, and 889 field definitions — a name and a field number each —
were decoded out of the embedded descriptors. Named packages seen in them: `com.axlebolt.bolt`,
`.bolt.matches`, `.bolt.matchmaking`, `.bolt.stats`, `.bolt.stats.gameserver`, `.bolt.Store`.

Two readings worth stating, because both were checked rather than assumed:

- **No gRPC service definitions are in it.** Zero `service` blocks and zero rpc methods
  survive in the descriptor bytes — the scan below finds them wherever they exist, and the
  only `ServiceOptions` in the binary belong to `descriptor.proto` itself. The reading, and
  it is a reading: dispatch is by request type through the client's own `BoltApi` /
  `BoltClient`, not through generated stubs.
- **The dedicated server's own API ships in the client build.** `GSGetPlayersStatsRequest`,
  `GSIncrementPlayersStatsRequest`, `ConsumeItemsByServerRequest`, `ExecuteRecipeByServerRequest`,
  `FinishMatchRequest`, `ConfirmMatchRequest`, `AbandonMatchRequest`, `BanGamePlayerRequest`,
  `CheckBanGamePlayerRequest` and — the one that says most about how the game is policed —
  `AccusationByServerRequest`. Bans and accusations are things the *server* sends about a
  player, and the client carries the definitions because the game server is built from the
  same assemblies.

The recovery is partial by construction: IL2CPP does not keep a descriptor's base64 chunks
contiguous, so whole `.proto` files cannot be reassembled — 889 fields is a floor, not the
schema.

### 2. The room layer — Photon (Exit Games)

`Photon3Unity3D.dll`, `PhotonPeer`, name server → master server → game server, regions,
`JoinRoom` / `JoinRandomRoom` / `ReconnectAndRejoin`, and a
`com.axlebolt.bolt.PhotonGame` message with custom properties on the protobuf side.
The PUN-era types (`PhotonView`, `PhotonNetwork`, `PhotonSerializeView`, `PhotonStreamQueue`,
`PhotonInstantiate`) are still in the build alongside the framework below, which is what a
migration in progress looks like from the outside.

### 3. The match itself — `Axlebolt.NetCode` over LiteNetLib (UDP)

Their own netcode framework, and its vocabulary is the standard server-authoritative one:

```
Axlebolt.NetCode.Framework.Snapshots     Axlebolt.NetCode.Framework.TimeSync
Axlebolt.NetCode.Framework.Network.Client   …Network.Transport
Axlebolt.NetCode.Transport.LiteNetLibAdapter   LiteNetLibClientTransport / …ServerTransport
Axlebolt.Standoff.NetCode.CustomConverters.Quantisation.Vectors
ClientWorld.ReceiveWorld  ClientWorld.ProcessPrediction  ClientWorld.InterpolateWorld
ClientWorld.ProcessCommands  ClientWorld.TransportSend   ClientWorldOptions / ServerWorldOptions
WorldSnapshot   "Could not find world snapshot for tick id"   CascadeRollback
```

Snapshot names name the replicated state: `TransformSnapshot`, `WeaponStateSnapshot`,
`DamageSnapshot`, `CollisionSnapshot`, `SurfaceHitSnapshot`, `StunSnapshot`, `BlindSnapshot`,
`FlamePositionSnapshot`, `GraffitiPlayerStateSnapshot`, `ChatMessageSnapshot`. Twenty-two
names end in `Rpc`, of which fifteen are the game's: `SelectTeamRpc`, `SelectTeamAutoRpc`,
`WeaponShopBuyRequestRpc`, `WeaponShopBuyRequestCancelRpc`, `WeaponShopAutoBuyRequestRpc`,
`WeaponShopChargebackRpc`, `PositionMarkerRequestRpc`, `GraffitiDrawRpc`, `ChatMessageRpc`,
`SetVoteRpc`, `StartVotingRpc`, `SpectateRpc`, `SetRandomWeaponsStateRpc`, `ConsoleCommandRpc`,
`SendConsoleCommandRpc`.

That list is the whole *named* rpc surface of a round, and what is not in it is the
interesting half: there is no "I hit him" and no "my position is". Everything the client
sends about the actual play is an input command against a tick the server owns
(`ClientWorld.ProcessCommands`, `ClientWorld.TransportSend`), and even buying is a request
the server can reverse afterwards — `WeaponShopChargebackRpc` exists because the server,
not the client, decides whether the purchase happened.

The wire format is **not** protobuf here. `Axlebolt.NetCode.Serialization` generates binary
serializers (`BinarySerializerGeneratedAttribute`, `BinarySerializerPrimitiveAttribute`) and
the vector converters quantise, so a match packet is bit-packed by code that only exists
compiled. Names tell you what a packet contains; they do not tell you where the bits are.

### Hosts in the binary

`dev-matchmaking.bolt-api.com`, `metrics.standoff2.io`, `fra01.metrics.ms.boltgaming.io:9111`,
`avatars.cdn.boltgaming.io`, `link.standoff2.com`, `install.standoff2.com`, `help.standoff2.com`,
plus Photon's `ns.exitgamescloud.com`. Voice is Vivox ("Connected to Vivox").

### One correction, because the name invites the opposite guess

`lib/arm64-v8a/libsigner.so` (1.1 MB) is **not** the game's anti-cheat or its request
signer. Its only exported entry point is `Java_com_adjust_sdk_sig_NativeLibHelper_nSign` —
it is the Adjust attribution SDK's signature library. The game's own verification is the
`AppVerification` message documented above, built in C# and carried on the auth call.

### What this changes for UNIQUE: nothing

The wall is where the section above says it is — `AppVerification` riding on
`GoogleAuthRequest`, read by a server. Nothing in the match protocol is on UNIQUE's path,
and nothing in this section is a step toward touching it: UNIQUE runs a guest unmodified,
and reading a protocol's shape out of a binary is not the same activity as speaking it.
This is recorded because "is the networking visible?" is a question that will be asked
again, and answering it a second time costs the same day it cost the first time.

---

## How to reproduce every claim here

```bash
# 1. The APK. 105 MB.
#    (the OBB is not needed; its index says why — see the table at the top)

# 2. The metadata is in libunity.so, in the clear.
unzip -o app.apk 'lib/arm64-v8a/libunity.so' -d x
strings -n 5 x/lib/arm64-v8a/libunity.so > s.txt
wc -l s.txt                       # ~378,000

# 3. The two messages.
grep -aoE '[A-Z][A-Za-z0-9]+/[A-Z][A-Za-z0-9]+' s.txt | grep -i virtualspace
#   Anticheat/VirtualSpaceWarning
#   AuthRestrictions/VirtualSpaceMessage   (as ...get_VirtualSpaceMessage)

# 4. The detector's surviving member names.
python3 - <<'PY'
import re
d = open('x/lib/arm64-v8a/libunity.so','rb').read()
i = d.find(b'AntiCheatManager')
print(d[i-200:i+400])          # …AntiCheatManager Axlebolt.Standoff.Anitcheat…
j = d.find(b'VirtualSpaceDetected')
print(d[j-80:j+200])           # VirtualSpaceDetected PackageName ScanDirectoryRecursive
PY

# 5. The report, and the request it belongs to.
grep -ao 'AppVerification' s.txt | head
python3 - <<'PY'
import re
d = open('x/lib/arm64-v8a/libunity.so','rb').read()
i = d.find(b'\x00AppVerification\x00')
print(b' '.join(p for p in d[i:i+1200].split(b'\x00') if p).decode('latin-1'))
PY

# 6. The Java surface it reads, and the counts in the table above.
for n in sourceDir getPackageCodePath nativeLibraryDir getApplicationInfo \
         getInstalledApplications getFilesDir currentActivity; do
  printf '%s\t%s\n' "$(grep -ac "$n" s.txt)" "$n"
done

# 7. The hard-coded cheat-path list.
grep -ao 'io.va.exposed[^ ]*' s.txt | head -1

# 8. The three network layers, by the names they leave behind.
grep -ao 'Axlebolt.NetCode[A-Za-z0-9_.]*' s.txt | sort -u
grep -ao 'ClientWorld[A-Za-z0-9_.]*' s.txt | sort -u
grep -aoE '[A-Za-z0-9_]{3,50}Rpc\b' s.txt | sort -u            # 22, fifteen of them the game's
grep -aoE '[A-Za-z0-9_]{2,40}(Request|Response)\b' s.txt | sort -u | wc -l   # 1081

# 9. The protobuf schema, as far as IL2CPP leaves it recoverable.
#    protoc's C# backend embeds each .proto file's descriptor as base64, split into
#    60-character literals. IL2CPP keeps the literals but not their order, so whole files
#    cannot be reassembled — individual chunks still decode, and a field definition or an
#    rpc method is recognisable in the wire encoding on its own.
python3 - <<'SCAN'
import base64, re
d = open('x/lib/arm64-v8a/libunity.so', 'rb').read()
pile = []
for m in re.finditer(rb'[A-Za-z0-9+/]{40,}={0,2}', d):
    r = m.group(0)
    for ph in range(4):                       # the run's start is not chunk-aligned
        c = r[ph:]; c = c[:len(c) // 4 * 4]
        if len(c) >= 40:
            try: pile.append(base64.b64decode(c))
            except Exception: pass
b = b'\xff\xff\xff\xff'.join(pile)
def lp(i):                                    # length-prefixed identifier at i
    n = b[i] if i < len(b) else 0
    s = b[i+1:i+1+n]
    return (s.decode(), i+1+n) if 1 <= n <= 120 and re.fullmatch(rb'[A-Za-z0-9_.]+', s) else None
fields, rpcs = set(), set()
for m in re.finditer(rb'\x0a', b):
    a = lp(m.start() + 1)
    if not a: continue
    name, j = a
    if j + 6 <= len(b) and b[j] == 0x18 and b[j+2] == 0x20 and b[j+4] == 0x28:
        fields.add((name, b[j+1]))            # field name, field number
    if j < len(b) and b[j] == 0x12:           # rpc: name, input_type, output_type
        i2 = lp(j + 1)
        if i2 and i2[0].startswith('.') and i2[1] < len(b) and b[i2[1]] == 0x1a:
            o = lp(i2[1] + 1)
            if o and o[0].startswith('.'): rpcs.add((name, i2[0], o[0]))
print(len(fields), 'field definitions,', len(rpcs), 'rpc methods')   # 889, 0
SCAN

# 10. libsigner.so is Adjust's, not the game's.
unzip -o app.apk 'lib/arm64-v8a/libsigner.so' -d x
llvm-readelf --dyn-syms x/lib/arm64-v8a/libsigner.so | grep -o 'Java_[A-Za-z0-9_]*'
#   Java_com_adjust_sdk_sig_NativeLibHelper_nSign
```
