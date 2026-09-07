# Download

Two APKs, both **arm64-v8a only**, both signed with the same key — so either can be
installed over the other. Android 12 or newer (`minSdk 31`), no root, no unlocked
bootloader.

> **This installs over the last two builds** — same key, instances and data intact. If you
> last installed something older than that, the paragraph below still applies to you once.
>
> Every build before those was signed with a key the *build machine* generated for itself,
> so two builds made in two sessions had two different keys — and Android's error for
> that is the unhelpful `App not installed`. The only way through it is to uninstall,
> which deletes the instances, their data, and any expansion file you spent an evening
> importing. That was a build-machine detail costing a tester their work, so the key now
> lives in the repository (`app/debug.keystore`, subject `CN=UNIQUE Test Key`) and every
> build from here on installs over this one with the instances intact.
>
> It is a **test** key and marked as one: its password is written in `app/build.gradle.kts`
> in the open, and anyone with the repository can build something that installs over your
> copy. That is exactly what it is for, and it is why an APK signed this way must not be
> distributed. `apksigner verify --print-certs` on the APK you have and the one you are
> about to install says which case you are in; `SHA256SUMS` says which artifact this is.
>
> So: if you have never installed one of the keyed builds, **uninstall UNIQUE once, install
> this, and re-import your apps.** After that, updates keep everything.

| File | Size | What it is |
|---|---|---|
| `unique-arm64-v8a.apk` | ~26 MB | **Install this one.** The engine exactly as the acceptance suite runs it — no R8, so no keep rule can be wrong — with Flutter's UI built ahead of time so the artifact is a quarter the size of a debug build |
| `unique-arm64-v8a-minified.apk` | ~18 MB | The same thing with R8 on. **Not device-verified** — see below |

Direct links, which work in a phone browser:

- https://github.com/erundltd/gavna/raw/claude/unique-app-virtualization-bn35b2/dist/unique-arm64-v8a.apk
- https://github.com/erundltd/gavna/raw/claude/unique-app-virtualization-bn35b2/dist/unique-arm64-v8a-minified.apk

Verify what you downloaded against `SHA256SUMS`.

## Which one, and why it matters

Install **`unique-arm64-v8a.apk`**. It is the build the acceptance suite ran against, and
it is not minified — a virtualization engine is nearly all reflection, so a wrong R8 keep
rule produces an app that works everywhere except the build people install.

`unique-arm64-v8a-minified.apk` assembles, signs, and its keep rules hold *structurally*:
the minified dex still carries the stub pool, the router and shared providers,
`UniqueNative` and the native entry points, and the manifest still declares all 281
components. That was checked on this artifact. It is not the same as running: the
instrumented suite cannot be pointed at it, because `androidx.tracing.Trace` reaches
`AndroidJUnitRunner.onCreate` from R8's *classpath* rather than from program input, so no
keep rule applies. Install it only to see whether it starts — and if it behaves
differently from the other one, that difference is the finding.

## Signing

Both are signed with the repository's **test key** (`CN=UNIQUE Test Key`, SHA-256
`6086840c82b590eb8b0e1b35578844f973f056fe08fc2577e110e739cb3ba6d3`), the same key as the
build before this one. It lives in
`app/debug.keystore` so that every build from here on installs over the last one with your
instances intact — see the warning at the top for why that changed, and for the one
uninstall it costs you now. It is a test-signing arrangement and is marked as such: these
must not be distributed as a release, and an app signed with a real key later will not
install over them.

## What changed since the last phone run

Хорошая новость: возврат сработал. Никаких падений драйвера, ничего не крашится, игра
запускается и показывает своё меню — на скриншоте это видно. Осталось два провала, и один
из них я наконец понял.

**`java.io.File` — это две библиотеки, а не одна.** Диагностика, которую я поставил два
прогона назад, напечатала ровно ту строку, ради которой ставилась:

```
io_redirect: hooked libjavacore.so=8
```

Восемь точек в `libjavacore.so` — это `open`, `stat`, `lstat`, `access`, `mkdir`, `rmdir`,
`unlink`, `rename`. То есть всё, чем приложение **пишет** через `java.io`. И при этом
проверка отказалась публиковать путь: «опубликованный путь к APK не открывается».

Оба факта верны одновременно, потому что чтение атрибутов файла — `isFile()`, `length()`,
`delete()`, `list()` — это **нативные** методы, и живут они в другой библиотеке
(`libopenjdk.so`, порт OpenJDK внутри Android), которой не было в списке. А написана она
через «большие» имена функций: она вызывает `stat64`, а не `stat`. На 64-битном телефоне
это одна и та же функция, но **разные символы** в таблице — перехват одного не видит
другого.

Поэтому все записи подменялись, а первая же проверка существования опубликованного пути —
нет. И поэтому проверка из позапрошлой сборки проходила: она писала через публичный путь, а
читала результат по *настоящему*, и ни разу не спрашивала `stat` про публичный путь. Эта
проверка была первой, кто спросил.

**Что сделано:** `libopenjdk.so` добавлена в список, и добавлены «большие» имена —
`stat64`, `lstat64`, `open64`, `openat64`, `fopen64`, `statfs64`, плюс `remove` и `creat`.

И ещё: сообщение об отказе теперь различает три разные причины, которые раньше выглядели
одинаково — «нет правила для пути», «правило есть, но файла по нему нет», «правило верное, а
Java всё равно не видит». Последнее — это то, что было, и раньше оно молчало.

### Про «Попытка входа отменена» на скриншоте

Это то же самое, что и раньше, и это ещё не та стена, о которой мы говорили. Запрос входа
уходит из пространства к Play services телефона, тот отвечает **как UNIQUE**, и игра
получает отмену. До сервера игры с отчётом об окружении дело пока не доходит.

Порядок от этого не меняется: сначала пути, потом вход. Но теперь ясно, что до таблички про
виртуальное пространство мы ещё не дошли — а значит и не проверили то, ради чего всё это.

## What changed one run ago

Прошлая сборка сделала хуже, и твой лог говорит чем — одной строкой.

```
io_redirect: hooked 9 new slot(s) after loading mapper.mediatek.so
E mali_config_interface_c_mapper: Failed to locate stable-C mapper library
E mali_config_interface_mapper: Failed to acquire IMapper service. Aborting.
E CRASH: signal 6 (SIGABRT) … name: RenderThread >>> com.axlebolt.standoff2 <<<
```

Я расширил перехват на **весь процесс** — 436 точек в 385 библиотеках — чтобы закрыть
дыру из предыдущего лога. В числе этих 385 оказался драйвер графики Mali. Он не нашёл свой
модуль памяти и **аварийно завершил поток отрисовки**. Игра перезапускалась три раза за
минуту.

Мой довод «это безопасно, потому что правила подмены не могут совпасть с папками UNIQUE»
был верен и остаётся верным — но он был не про то. **Библиотеку можно сломать самим фактом
перехвата, независимо от того, что потом решат правила.** Драйвер — ровно такая библиотека:
он не возвращает ошибку, он падает.

**Что теперь.** Список библиотек снова именной, но составлен не на глаз: прошлая сборка
впервые напечатала, сколько точек в каждой библиотеке, и я взял этот список, убрав из него
графику и вендорское. Плюс `/vendor/`, `/odm/`, `/system/vendor/` исключены наглухо, чтобы
случайное расширение больше не дотянулось до драйвера.

То же самое с той тонкостью, из-за которой не открывалась база SQLite: она нужна ровно
одной библиотеке (`libsqlite.so`), а применялась ко всем — теперь только к ней.

**Чего эта сборка не объясняет, и я не буду делать вид, что объясняет.** В прошлом логе
опубликованный путь к APK перестал открываться (`code=false`), хотя в позапрошлом, с
*более узким* перехватом, он открывался. Более широкий перехват, подменяющий *меньше*, —
это не то, что предсказывает рассуждение, и причину я пока не знаю. Диагностика для этого
уже стоит и в следующем логе напечатает по каждой библиотеке, что именно в ней подменено.

### Про вход — ты прав, и я был неправ дважды

Ты сказал: вход и есть та стена, потому что войти в игру просто не получается. Это верно, и
мои две поправки до этого были неточны. Как я теперь понимаю картину с твоих слов:

- выбираешь любой вход → игра грузится → пишет, что запущена в виртуальном пространстве;
- так во **всех** виртуалках, не только в нашей.

Значит вход доходит до сервера, и отказ приходит **от сервера игры**, а не от Google. Это
ровно то, что предсказывает разбор бинарника: отчёт `AppVerification` с путём к APK едет
*внутри* запроса на вход. Никто ещё эту стену не прошёл — и это же значит, что она и есть
то, ради чего стоит работать, потому что в отличие от аттестации она находится в
досягаемости.

## What changed two runs ago

Your log was the most useful one this project has had, and not because things worked. Both
of the safety checks I built into the last build fired, and one of them was right to.

**The safe half worked exactly as designed.** Before telling an app its data folder is
`/data/user/0/<app>`, UNIQUE writes a byte through that path and checks it arrives where it
should. It did not, so the data folder was left alone and nothing was lost:

```
GUEST_PATHS_PUBLISHED … code=true data=false
    detail=a database cannot be opened through the public path
```

The reason is a genuinely interesting bug and your log contained the whole diagnosis.
UNIQUE redirects an app's file access by rewriting a pointer in each library's jump table.
That catches every *call*. It does not catch a library that takes the *address* of a system
function once, at load, and stores it — and Android's SQLite does precisely that for `stat`
and `lstat` while going through the normal route for `open`. So inside one library half the
file calls were redirected and half were not, SQLite opened the database in one place and
looked for it in another, and said so. Fixed: those stored addresses are patched too, and
there is now a test that compiles the same construct and checks the compiler still produces
what the fix assumes.

**The unsafe half is the one that caused what you saw.** The "not enough memory" message in
Standoff 2 is almost certainly this line, which your log has three times:

```
E misc: isReadonlyFilesystem():
    statfs(/data/app/~~kx_uUO…/com.axlebolt.standoff2-…/base.apk) failed:
    No such file or directory
```

I told the game its APK is at an installed-looking path, and made that path work for the
libraries I had hooked — but not for the rest of the system. One library I had not thought
about read the path, could not open it, and the game reported it as a storage problem.

That was my mistake and the fix is the obvious one once it is stated plainly: **a path
handed to an app is handed to every library in that app's process**, so the redirect has to
cover all of them. It now does — every library except the linker, the C library itself and
UNIQUE's own. That is a big change, and the reason it is safe is not that I was careful: it
is that the redirect rules can only ever match the *guest's* own folder names or the shared
storage folders, never UNIQUE's, and there is a test that checks exactly that against
UNIQUE's own settings, database and logs by name.

I also added the gate that was missing: the app is not told about an installed-looking path
unless the redirect is actually in place and the path actually opens.

**And the log tool now reads all of this.** A new `paths` check reports a gate refusing as
information — that is the design working — and *fails* on a published path that some caller
could not open. That distinction is the whole lesson from your log.

### Will the game ever actually work? — the short version

Three different things have been getting mixed together, so, separately:

1. **Запускается ли игра.** Да, уже. Твой одиннадцатый лог: Unity на настоящем GPU, свои
   файлы, звук, экран входа на 2400×1080, твоё касание принято. Это не вопрос.
2. **Табличка «виртуальное пространство».** Решаемая, и я знаю чем — четыре геттера, ничего
   больше. Работа идёт; ни один лог ещё не мерил её под целой сборкой.
3. **Вход.** Вот здесь было моё собственное заблуждение, и твой же лог его исправил.

### Что нашлось в твоём логе про вход

Ты пробовал войти через Facebook, и SDK напечатал, за кого он считает приложение:

```
D com.facebook.unity.FB: KeyHash: lcG7acvUIg0k4FQSQmAbyw1tN0o=
```

Это отпечаток **подписи приложения**, посчитанный на месте. У UNIQUE отпечаток другой
(`ZNumr4jaGDnhpxdrLhZyrLkUR2s=`). Значит SDK получил **настоящую подпись Standoff 2** —
он спросил PackageManager самого приложения, а это PackageManager UNIQUE, и тот ответил
правильно.

**Это и есть разница между Google и всеми остальными.** Google спрашивает Play services —
другой процесс, который смотрит на идентификатор процесса ядра, и туда UNIQUE не дотянется
никогда. Facebook, VK и почти все остальные спрашивают само приложение — и там UNIQUE уже
отвечает верно, сегодня.

Дальше лог показывает, где именно вход останавливается. Три из четырёх экранов Facebook
SDK отработали **внутри** пространства, а четвёртый открыл Chrome — и оттуда возврат
(`fb752573801798020://authorize/…`) уходит в Android, который не знает такого приложения.
Вход завершается на стороне Facebook и приходит в никуда.

Починить это — конкретная, ограниченная работа: перехватить открытие браузера, показать
страницу входа в WebView **внутри процесса приложения**, дождаться возврата и отдать его
приложению напрямую, минуя Android. Ничем не заблокировано.

**Порядок важен и он не по вкусу, а по зависимости.** В бинарнике игры *каждый* вид входа —
Google, VK, Facebook, Game Center — несёт с собой один и тот же отчёт об окружении. Значит
сервер откажет и по Facebook, пока отчёт говорит, что APK лежит в папке `com.unique`.
Сначала табличка, потом браузер внутри пространства. В обратном порядке получится вход,
который дойдёт до сервера и будет отклонён, и из этого ничего не узнать.

### About Google sign-in, since you asked

Short answer: **not through your phone's own Play services. Ever. And there is exactly one
route that could work, which is not built yet — but per the section above, Google is not
the route this game needs.** The long answer is
[`docs/GOOGLE_SIGN_IN.md`](../docs/GOOGLE_SIGN_IN.md); the essentials:

Google identifies an app by **package name and signing certificate**, and it does not take
the app's word for either — it asks Android, using the process id of whoever called. Inside
UNIQUE that process is UNIQUE. That is why the "unknown calling package" errors were fixable
(UNIQUE now tells Play services the truth, that the caller is UNIQUE) and why sign-in is
not: telling the truth there means asking Google for a token for UNIQUE, which has no
account with the game's server.

There is no hook, no permission and no setting that changes this. It happens in another
process, against records Android keeps, on a phone with no root.

**The one route that would work** is to run Play services *inside* the virtual space, as
another virtual app. Then the process asking "who is calling?" is one whose Android is
UNIQUE's, and UNIQUE answers with the game's real package name and its real signing
certificate — which it has, because it holds the game's APK. This is designed
(`ARCHITECTURE.md` §9.3, "Mode A") and not implemented. It needs Play services, Google
Services Framework and the Play Store imported from your own phone, sign-in intents kept
inside the space instead of leaving to the host's copy, a device check-in that may or may
not succeed, and 150–250 MB of RAM per space. It is a large piece of work, not a setting.

**What it still would not fix:** Play Integrity and its equivalents, which attest the device
and the app *as Android sees them*. Standoff 2 ships Play Integrity. Your ChatGPT error,
`preauth_cookie_device_check_failed`, is OpenAI's own version of the same idea. UNIQUE is
not an attestation bypass and this does not change that.

**One thing that is fixable and is a separate job:** apps that sign in through the browser
(ChatGPT is one) fail for a different reason — the sign-in completes on Google's side and
the redirect back has nowhere to land, because the app the redirect names is not installed
on your phone. Keeping that whole exchange inside the app's own process, in a WebView
UNIQUE owns, would fix it without Play services at all. It would not help Standoff 2, which
uses the native Google API.

### What I need from the next log

1. **Open two or three apps that had data in them, before the game.** This build changes
   every file operation of every app. A mistake here does not crash — the app just looks
   empty. It is the only failure that would not announce itself.
2. `GUEST_PATHS_PUBLISHED … code=true data=true`, and no `paths` failure.
3. Whether the "virtual space" notice appears in Standoff 2 — and whether the
   "not enough memory" one is gone.

## What changed three runs ago

**Your last log was the best one yet: seventeen of eighteen checks passed.** The game
launched, ran, did not crash, and did not die on the sign-in button the way it did before.
The `/proc` cover that had a hole in it last time is now complete on your phone — the
engine's own self-check went from "two entries still name UNIQUE" to zero. So this build is
not another round of fixing what broke; it is the one large change I said I would not make
in the same build as anything else.

**The game is now told it is installed.**

Last time I read the game's own binary instead of guessing at it, and it turned out to
check exactly four things — where its APK is, where its libraries are, where its code is,
and where its data folder is. Inside UNIQUE, all four answered with a path containing
`com.unique`. No real installation of any app on Earth produces such a path, so the game
did not have to be clever; it only had to look.

Those four now answer the way an installed copy would:

| The game asks | It used to hear | It now hears |
|---|---|---|
| where is my APK | `…/com.unique/files/virtual/apk/…` | `/data/app/~~…/com.axlebolt.standoff2-…/base.apk` |
| where are my libraries | the same, plus `lib/arm64-v8a` | the same folder, `lib/arm64` — which is how a real install spells it |
| where is my data | `…/com.unique/files/virtual/users/1/…` | `/data/user/0/com.axlebolt.standoff2` |

The hard half is not telling it that. The hard half is that those paths have to **work** —
if the game opens the folder it was just told about and finds nothing there, that is worse
than the truth, because the game silently loses its settings and its downloads. Making them
work meant redirecting the game's file access at a level UNIQUE had never touched before:
not just the game's own code, but the Android libraries every app's `File`, save-file and
database goes through.

That is the largest single change this engine has had, and it touches **every file
operation of every app you run**, not just the game. Two things keep it honest:

- UNIQUE's own files are provably out of reach of the redirection — the rules can only ever
  match the *guest's* package name or the shared-storage folders, never UNIQUE's own — and
  there is now a test that checks precisely that, including UNIQUE's settings, database and
  logs by name.
- The data-folder half is **not applied on faith**. Before the app is told its new data
  path, UNIQUE writes a byte through it and checks the byte arrives where it should — twice,
  once as an ordinary file and once by opening a database, because those take different
  routes. If either fails, the data path stays as it was, the app keeps working, and the log
  says which probe refused and why.

**What I need from the next log, in order of importance:**

1. `GUEST_PATHS_PUBLISHED … code=true data=true`. If it says `data=false`, nothing is
   broken — the safety check refused, and the line says why.
2. **Open a few apps you already had, with data in them,** before you open the game. This
   change touches every file every app reads. An app that lost its settings is what a
   mistake here looks like — it will not crash, it will just look empty. That is the single
   most useful thing you can check.
3. Then the game: whether the "virtual space" notice still appears.

**What this does not fix, and I want to be exact about it.** Google sign-in will still be
refused. That is not the same problem. Google identifies an app by its package name *and*
its signing certificate, resolved from the kernel — inside UNIQUE the request genuinely
arrives from UNIQUE, and no amount of path work changes who is calling. The game's
"virtual space" verdict and the Google refusal have been failing together because the game
sends its environment report *inside* the sign-in request; this build addresses the report,
not the sign-in. If the notice disappears and sign-in still fails, that is the expected
outcome and it is progress, not a half-fix.

**One more thing fixed along the way, from reading my own output.** My log analyzer had
been reporting *"Google answered DEVELOPER_ERROR"* on runs where Google had answered no
such thing — the only line containing that word was UNIQUE's own explanation of what
*would* happen. A tool that cannot tell its own prediction from a real answer is worse than
no tool, because both read identically. Fixed, with a test.

## What changed four runs ago

**I read the game.** Two passes were spent guessing at what Standoff 2 checks; this time
the check itself was found, in the shipping build, and it is not what either of us assumed.
[`docs/STANDOFF2.md`](../docs/STANDOFF2.md) is the whole of it. The short version:

- There are **two** "virtual space" messages, not one. One is shown in-game by the game's
  own anti-cheat; the other is a **refusal from Axlebolt's server** at login.
- The server one is decided from a report the game builds and **sends as part of the Google
  sign-in request itself**. It contains the APK's path, its hash, its certificate, a file
  and library list, a process list, and it is signed with a key the game generates — so it
  cannot be edited on the way past. Signing in and being told this is a virtual space are
  the same event, which is why they have both been failing together.
- What the game reads to fill that report in is **four getters**: `sourceDir` (4 times),
  `getApplicationInfo` (3), `getPackageCodePath` (2), `nativeLibraryDir` (1). Inside UNIQUE
  all four answer with a path containing `com.unique`, and no installed copy of the game
  could produce any of them.
- It reads **no** `/proc/self/maps`, no process list, no emulator check. Which means the
  thing I built last time closes a real hole and closes nothing *this* game uses. I would
  rather say that here than let it look like progress it is not.

Closing it is one specific change — make those four values look like an installed app's,
and make the paths still work — and it is the biggest change this engine has left, because
the guest's Java file access has to be redirected too. It is not going into the same build
as three other fixes; a log that then went wrong would not say which one did it.

**Your previous log also had the best news this project has had.** Standoff 2 ran. Not "launched" —
*ran*: Unity came up on the real GPU, the game read its own expansion file out of its
copy's storage, sound loaded, Firebase and the analytics SDKs started, and it drew its
login screen at 2400×1080 and took your tap on it. Every version of the project's own
README before today said a virtual app had never reached a usable screen on a phone. That
sentence is now wrong and has been replaced.

Then you tapped *Sign in with Google* and it crashed. That exact crash was found and fixed
in the previous build from your ninth log — the one you tested is older than the fix — so
it should not happen again here. Nothing new was needed for it.

What was new is underneath, and it is worse than the crash:

- **A game ran under another app's identity.** MT Manager failed to start (see below),
  and the process it failed in was left half-set-up and marked as belonging to nobody.
  Sixteen seconds later a background job for Standoff 2 landed in that same process and
  took it over — so the game ran reporting **MT Manager's device id**. Two copies having
  separate identities is the one thing this whole project is for, and it was quietly
  broken by an app that was not even running. A process is now claimed the moment a copy
  starts using it, whether or not that copy then works, and a failed start hands the
  process back so the next attempt gets a clean one instead of a poisoned one.
- **The game was refused a permission no dialog can grant.** `ACCESS_ADSERVICES_ATTRIBUTION`,
  twice. It is granted at install or never; there is no screen anywhere on your phone that
  could have fixed it. UNIQUE now asks for it, and for the four others Standoff 2 wants
  that it did not have.
- **The game can see it is inside UNIQUE, and now sees less.** This is the one you asked
  about. An app does not have to work hard to find out where it is: it reads
  `/proc/self/maps`, a file listing everything its process has open, and inside a copy
  that file names UNIQUE's own app and UNIQUE's own folders in plain text. It costs one
  read and no permission. That file is now answered with a rewritten version — the game's
  own files appear where an installed game's would be, `/data/app/…/com.axlebolt.standoff2-…`
  and `/data/user/0/com.axlebolt.standoff2`, and so does UNIQUE's own code. Nothing is
  removed from the list, only renamed, because the game's crash reporter reads the same
  file and would break if lines went missing.

  **This covers reads made by the game's own native code, which is where this kind of
  check lives.** It does not cover four other ways of asking, and rather than let you find
  that out from a screenshot: a check written in Java, a check that asks the code loader
  instead of the filesystem, a check that calls the kernel directly, and the app's own
  data folder path, which is still UNIQUE's and has to be until a larger change is made.
  If the notice still appears, the log will now say whether this vector was closed —
  UNIQUE checks its own work at startup and records the result — and that narrows the
  next step to one of those four.
**And three things this build fixes, all from your newest log.**

- **The Google sign-in crash is fixed — properly this time.** The fix I shipped last time
  could not have worked, and your log proved it by crashing in exactly the same place. An
  intent's data is unpacked lazily, and the code loader that will be used is decided at the
  **first read**, not when you name it — and UNIQUE always reads first, on the line above.
  So naming the right one afterwards changed nothing. It is now a loader that is installed
  before anything reads, and asks who the app is at the moment it is needed.
- **The `/proc` cover had a hole and told me so itself.** `leaked=2` — thirteen of fifteen
  entries hidden and two not, because Android spells one directory two ways and I had only
  covered one. That whole mechanism exists so the log says this instead of me finding out
  from a screenshot, and it worked on the first phone that ran it.
- **Firebase analytics is still refused, and I stopped guessing at it.** Three passes, three
  real fixes, three logs with the same message on a different route — because the log could
  not tell "my fix did not fire" from "my fix fired and something else refused". It can now:
  the next log will name the exact interface, call and byte offset. I am not shipping a
  fourth guess at it.

- **MT Manager still does not start**, and I know one more thing about why. Its protection
  library `libmtprotect.so` **loads fine**, and 58 milliseconds later the thing it was
  supposed to provide is not there. It did not fail to load; it looked around and refused.
  And it looked around before any of UNIQUE's interception was in place, because that was
  set up after the app's first code ran rather than before it. That order is fixed. Whether
  the protector changes its mind is a measurement, not a promise — if it still fails, it
  fails for a reason worth reading.

## What changed five runs ago

The rewrite from the last build worked — your log shows 17 requests going out under a
name Google accepts, and the game-files message is gone. What it uncovered is three more
things that were hidden behind it.

- **Standoff 2 asked you to install Play services from a build where Google works.** Its
  copy still carried a note written when it crashed under the *older* build: "hide Play
  services from this app". That note outlived the build it described. Notes now record
  which build wrote them, and one from an older build is thrown away and deleted —
  Standoff 2 will see Play services again on its next launch, and you do not need to
  re-add it.
- **Firebase Analytics was still being refused.** It does not go through the connection
  the last build corrected; it talks to a different Google service directly. Every Play
  services connection is corrected now, not one kind of them.
- **Google sign-in got further than it ever has, and crashed in my code, not Google's.**
  The sign-in screen *launched inside the app* and died on its first line, reading a
  setting it had written itself one step earlier. The data was there; nothing could find
  the class that reads it, because the app's own code loader was not attached to the
  message carrying it. That is fixed, and it is not a Google fix — any app passing its
  own data through an app-to-app message hit the same thing.

**What I told you last time about sign-in was drawn from one path and stated as if it
covered all of them.** The `DEVELOPER_ERROR` is real for a request that reaches Google as
UNIQUE. But this crash never reached Google at all. Try signing in on this build and send
the log: what happens now is something nobody has measured, me included.

## What changed six runs ago

- **Google Play services actually works now.** There was one refusal behind every Google
  failure this project has ever had: Play services checks that the calling app's name
  belongs to the calling process's uid, and inside UNIQUE the uid is UNIQUE's while the
  name is the app's. Every Google API failed on that — Maps, Firebase, ads, the
  advertising ID, analytics, FCM, the module loader. The request now goes out under
  UNIQUE's own name, which is the truth about the uid, and Play services accepts it.
- **Google sign-in still does not work, and here is exactly why.** Sign-in is the one
  thing that asks *which app is this*. Google answers it from the app's package name **and
  signing certificate**, registered in the app developer's own Google Cloud project. The
  call now reaches Google — but it arrives as UNIQUE, which matches no such registration,
  and Google replies `DEVELOPER_ERROR`. That reply is already in your last log. Nothing
  UNIQUE can send changes it: the check is a signature check made inside Google's own
  process. It needs Play services running *inside* the virtual space, which is a
  reimplementation of GmsCore plus system-level signature spoofing — root or a modified
  ROM. I am not going to pretend that is a build away.
- **The file manager is on the home screen.** First tile, always there, no *Remove* — it
  is part of the virtual device, like a file manager on a phone. Open it and you get the
  list of apps in the space; open one and you get its folders under the paths the app
  itself uses, `/data/data/<package>` and `/sdcard`, with `Android/obb/<package>` where a
  game's instructions say it is. **Import** copies files in from anywhere the system
  picker reaches, several at once. It is still reachable from an app's own Storage
  section, which opens it directly inside that app.

## What changed seven runs ago

Six things were reported. Two of them were mistakes of mine, one was a request, and the
log had all of them.

- **Apps stopped crashing on Google Play services.** Making the stack visible had a cost
  and this is it: three apps died of `SecurityException: Unknown calling package name` on
  their own main thread. In the *same run*, ChatGPT hit the identical refusal ten times
  and carried on — because it ships a newer Google library, and the newer one catches it.
  UNIQUE now catches it too. One exception, from one API, is caught on the guest's main
  loop; everything else still ends the process as before. An old app ends up on the same
  path its own newer sibling takes: that one call fails, the app is told, and nothing else
  is lost. Play services stays visible for everything else.
- **"Why does it say OBB is needed for apps that don't even have it?"** Because I got it
  wrong. UNIQUE decided the expansion files were present-and-blocked whenever it could not
  *see* the directory — and since Android 11 it can never see it, so every app matched,
  including a chat app, a cleaner and a file manager that have never had one. Nothing is
  inferred any more; an invisible directory is now reported as nothing at all.
- **"I gave all-files access and nothing changed."** It could not have. **All-files access
  does not cover `Android/data` or `Android/obb`** — Android filters those two folders out
  below the permission check, so an app with the permission sees exactly what an app
  without it sees. Every message that told you to grant it is gone.
- **The banners are gone.** The warning banner on App Details and the ten-second message
  after every launch have been removed. They were asking for that same permission, for
  every app, whether or not it was true.
- **There is a file manager now, which is what was actually needed.** **App Details →
  Storage → Files.** It shows the app's own folders under the paths the app itself uses —
  `/data/data/<package>` and `/sdcard`, with `Android/obb/<package>` where the game's own
  instructions say it is — and **Import** copies files in from anywhere the system picker
  can reach. Multiple files at once. That is the way to give a game its `.obb`, and it is
  the only way Android leaves open.
- **Google sign-in is still not fixed**, and I am not going to claim otherwise. Play
  services resolves the caller to UNIQUE, so a token comes back for UNIQUE and not for the
  app. Only Play services running *inside* the space can answer that, and it is not built.

## What changed eight runs ago

That log was answered with two words — *"nothing changed"* — and a screenshot of a
notification asking to install Google Play services. That was fair. The build was
installed and its new code was running; the code was wrong.

- **Google Play services is visible to guests again.** The previous build decided
  visibility per app, from the `com.google.android.gms.version` number an app declares.
  That number is the *minimum* GmsCore version the app accepts, not the version of
  anything it ships — Google froze it years ago, and the log had it identical for 1Tap
  Cleaner, ChatGPT and a Unity game. So the rule hid Play services from everyone, on a
  phone carrying GmsCore 26.32.34, and every app that looked said it was missing. It is
  visible now. It is hidden from a single instance only after *that instance* has died of
  the one refusal an old client cannot survive, which the crash handler records before the
  process goes; the next launch of that one app takes the path its SDK has for a phone
  with no Google, and nothing else is affected.
- **The all-files request is on the app's own screen now, not in a snackbar.** Expansion
  files (`Android/obb`) have been out of reach since Android 11, and a game without them
  looks like a broken download. This build put a banner on the app's details screen asking
  for all-files access. **That access does not cover `Android/obb`** — see the section
  above — so the banner asked for something that could not have helped, and it appeared
  for apps with no expansion files at all.
- **The device-log analyzer has a check for exactly this mistake.** It pairs "the phone
  has Play services" with "a guest was told it does not" and fails the run. Sixteen checks
  passed on the log that produced that notification; this is the seventeenth, and it is
  asserted against that same log so the rule cannot come back quietly.

## What changed nine runs ago

Six apps launched in that run and three of them died seconds later, all of the same thing.
This build answers everything that log reported.

- **Play services was killing guests.** `GmsClient.getRemoteService` sends the app's own
  package name to `com.google.android.gms`, which resolves the *calling* uid — UNIQUE's —
  and answers `SecurityException: Unknown calling package name`. It arrives on a `Handler`,
  so it is fatal and no app can catch it: three of the six guests in that log died this way
  within seconds of reaching their own screen. That build hid the Google stack from every
  guest entirely — `getPackageInfo`, the uid lookup, provider resolution, the installed
  list and both intent resolvers — so an SDK that asked found nothing and took the path it
  has for a phone without Play services. **Two runs later that turned out to be the worse
  fault of the two**, and the section above is what replaced it. `com.android.vending` was
  never hidden, because Play's licence check is an ordinary bind that works and a protected
  app exits without it.
- **There was no keyboard.** `EditorInfo.packageName` carries the app's own name into
  `startInputOrWindowGainedFocus`, and the input-method service checks it against the
  calling uid: a mismatch is `INVALID_PACKAGE_NAME` and *no keyboard is ever bound*. The
  field takes focus, shows a caret, and accepts nothing. `input_method` is hooked now and
  the phone's own keyboard comes up, exactly as it does outside UNIQUE.
- **A guest came up with no `FileProvider`.** Publishing a provider runs the app's own
  `attachInfo`, and `FileProvider`'s reads external storage — which was still the host's at
  that moment. Providers are published after the identity hooks now.
- **"The screen does not respond."** Touch was never broken: `ACTION_DOWN` and `ACTION_UP`
  reached every guest that was alive. The unresponsive screens were the dead windows left
  by the crashes above, still on screen with nothing behind them. Fixing the crashes is the
  fix for this.
- **The interface was half in English.** Every failure the engine can report now carries a
  code the interface translates, an automatic profile name is written in the reader's own
  language, and `tools/check-translations.py` fails the build if a code has no sentence in
  both. 25 codes, both languages.
- **Some permissions could not be granted at all.** Draw-over-other-apps, exact alarms and
  unrestricted background work have no Android dialog — each is a Settings screen, and each
  is held by UNIQUE rather than by a copy. App Details lists all three with their real
  state, opens the right screen, and re-reads the answer on return.
- **The *Advanced* section is gone**, as asked: the device test, the checklist, the
  diagnostics screen and the export with it. Everything UNIQUE records still goes to
  `logcat` under one tag, so any log-recorder app on the phone captures a whole run.

## What changed in the run before that

That build answered the four things its log was actually reporting, and eighteen more
found on the way. The four:

- **Everything was drawing in software.** The `ActivityInfo` UNIQUE substitutes carried no
  `flags`, and `Activity.attach` reads exactly `FLAG_HARDWARE_ACCELERATED` out of it — so
  every app UNIQUE has ever run rendered on the CPU. That is the "screen lags" report, and
  for anything drawing through a `RenderNode` it was not slow but fatal:
  `IllegalArgumentException: Software rendering doesn't support drawRenderNode`. The window
  is now told twice — through the `ActivityInfo` and directly, before the app's `onCreate`,
  because on an Android 14 device the first was not enough.
- **A landscape game opened portrait.** The platform takes a window's orientation from the
  manifest entry it has *installed*, which under UNIQUE is a stub declaring `unspecified`.
  The app's own `android:screenOrientation` is applied before it reads the display size.
- **`ApplicationInfo.metaData` was empty**, so Google Play services threw on every app that
  declares `com.google.android.gms.version` — which is most of them. Meta-data is carried
  now in both shapes, and a `@integer` reference is resolved against the app's own
  resources.
- **Play's licence check could not bind**, because UNIQUE never declared
  `com.android.vending.CHECK_LICENSE`. A PAIRIP-protected app calls `System.exit(0)` when
  that bind is refused, which is an app vanishing with no crash and no message.

And, from putting all of that on the emulator:

- A **second tap on a running app did nothing**: the platform delivers the launch to the
  activity that is already there, and what arrived was UNIQUE's routing intent rather than
  the app's own — which `ActivityThread` then stores as `getIntent()` for the rest of that
  screen's life.
- **External storage was unusable.** `getExternalFilesDir()` named a scoped-storage
  directory UNIQUE may not create, so apps that keep downloads or caches there saw storage
  as unavailable. It now resolves inside the instance.
- **An app could not tell you who it was.** `/proc/self/cmdline` read `com.unique:vapp0`,
  the process list showed UNIQUE's processes, `getLaunchIntentForPackage` for its own
  package returned null, and `getInstallerPackageName` threw. All four are answers an app
  expects to be able to get about itself, and apps that check them concluded they were
  running somewhere they should not.
- **A permission screen the app opened about itself went nowhere**, because the package it
  named is not installed. It is retargeted to UNIQUE, which is the uid that actually holds
  the access.

Also: a guest gets its **own** network security policy instead of UNIQUE's (an app died in
Conscrypt closing a TLS socket, and pinning and cleartext rules were the host's either
way), and a `<provider>` with no `android:name` no longer produces six
`ClassNotFoundException`s per launch.

Seven real applications from F-Droid were then imported and launched with nothing written
for them — **Termux**, **Fossify Gallery**, **NewPipe**, **Shattered Pixel Dungeon**,
**AntennaPod**, **KeePassDX** and **Aegis**. Four of the seven failed, each differently,
and all seven now reach their own main screen:

- a guest was held to **UNIQUE's** target SDK rather than its own, by two separate compat
  mechanisms, so an app built against Android 9 threw on `registerReceiver` and again on
  `PendingIntent`;
- `startForeground(id, notification)` — every app written before Android 10 — asked for
  *every* foreground-service type UNIQUE declares at once, and was refused for the first
  one whose permission the phone had not granted;
- an app asking about its own home-screen widget died in `onCreate`, because the service
  that answers that names the caller in a `ComponentName` and nothing was rewriting it;
- an app enabling *its own* component — which is how a launcher icon is changed and how a
  keyboard is switched on — was refused by the platform, because the component belongs to a
  package it has never installed; UNIQUE keeps that state per instance now;
- and an app publishing a shortcut killed itself in `Application.onCreate`. That one is
  refused rather than fixed: it now gets the "not accepted" answer the API already defines,
  and shortcuts from a virtual app stay a thing UNIQUE does not do.

The on-device suite is **46 of 46** on an Android 14 x86_64 emulator, and all seven of
those applications still pass against everything in this build. None of it has been back on
a phone; `docs/COMPATIBILITY.md` says so per row. And nothing here looks at the screen:
"the app reached its own main activity and stayed up" is the whole claim.

## What changed since the first phone run

That run imported two apps and launched neither. Both causes are fixed here.

- **A guest could not read a setting**, so it could not open a database, could not attach
  an Activity, and never started. The settings provider was cached before the graft and
  UNIQUE's identity rewrite never got in front of it.
- **`getHistoricalProcessExitReasons` went out under the guest's own name**, which needs
  `DUMP` for any package but the caller's — so startup crash-reporting code took the app
  down with it.
- **Every imported app was called `@7f010000`** and had no icon. Names and icons are now
  read from the stored APK through the platform's own parser, in the phone's language.
- **The log was nearly empty of UNIQUE's own events.** This build writes the full
  structured trace to logcat, so a capture from a logcat app on the phone now contains
  everything about a run. That is a much better starting point than the log before it,
  which held only warnings and errors.
- **The interface speaks English and Russian**, following the phone by default and
  switchable in *Settings → Appearance → Language*.

Neither engine fix is re-proven on hardware: the emulator never reproduced them, so it
cannot confirm them either. `docs/COMPATIBILITY.md` still says `BROKEN` for both on ARM64.

## What to do next

`docs/PHYSICAL_DEVICE_TEST.md` is the twelve-step sequence. It needs no `adb`, no root and
no computer to run: start any log-recorder app on the phone, work down the twelve steps,
and send what it captured. UNIQUE writes everything it records to `logcat` under one tag,
so an ordinary capture carries the whole run — `tools/device-log/analyze.py` reads it.

## What is not claimed

Standoff 2 is recorded `PARTIAL` on ARM64 in `docs/COMPATIBILITY.md`, not `SUPPORTED`.
Google sign-in from it has never completed and is not expected to — that limit is
explained above and is not a bug waiting to be found. Whether the *"running in a virtual
space"* notice still appears has **not been measured under this build**: the change that
targets it is tested on the build machine only, and a phone is the only thing that can
answer it. Everything else — hardware Vulkan, WebView rendering, Play Integrity, Play
Billing, Play Games — is still `NOT_TESTED` or `UNSUPPORTED` and stays that way until a run
says otherwise.

The list of libraries whose file access is redirected has now been wrong in three different
directions — too narrow, then everything, then narrow again but missing the one that answers
`File.isFile()`. Each was corrected from a phone log rather than from reasoning, and the
current list may still be short by a name. What is different now is that the engine says
which name, instead of failing silently.

What would help most from the next log, in order:

1. **`GUEST_PATHS_PUBLISHED … code=true`**, and no `paths` failure. That is the whole point
   of this build. If it still refuses, the message now says which of three things is wrong
   instead of one word for all three.
2. **Whether apps you already had still have their data.** Open two or three with something
   saved in them before the game.
3. **Whether the "virtual space" notice appears**, and what it lets you do — continue
   playing, or back to the login screen. Those are two different messages in the game and
   knowing which one it is matters.
4. `io_redirect: hooked <library>=<n>`, still — it is the line that found this one.
