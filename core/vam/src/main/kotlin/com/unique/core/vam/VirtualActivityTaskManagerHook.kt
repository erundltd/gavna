package com.unique.core.vam

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.os.Parcel
import android.os.Parcelable
import com.unique.core.common.apk.ComponentEntry
import com.unique.core.common.apk.ComponentKind
import com.unique.core.common.diag.DiagChannel
import com.unique.core.common.diag.DiagLevel
import com.unique.core.common.shim.MethodShim
import com.unique.core.common.shim.shim
import com.unique.core.diagnostics.Diagnostics
import com.unique.core.google.GoogleSignInHandoff
import com.unique.core.hook.SystemServiceHook
import java.lang.reflect.Method

/**
 * Routes a guest's *own* activity starts onto stubs.
 *
 * Launching the first activity of an instance is UNIQUE's job and goes through
 * [VirtualLaunchIntent]. Every activity the guest starts after that is the guest's own
 * call, and it names a component of a package the system has never installed:
 *
 * ```
 * ActivityTaskManager: Unable to find app for caller … / ActivityNotFoundException
 * ```
 *
 * So the same wrapping applied on the way in is applied here on the way out, and the
 * inbound rewrite in [LaunchInterceptor] unwraps it exactly as it does for the first
 * launch. No second mechanism, no second contract.
 *
 * ## Why a second hook and not the `activity` one
 *
 * `Activity.startActivity` has gone to **`IActivityTaskManager`**, not
 * `IActivityManager`, since Android 10 — `Instrumentation.execStartActivity` calls
 * `ActivityTaskManager.getService().startActivity(…)`. `IActivityManager` still declares
 * a `startActivity`, so a shim placed there binds cleanly and is never called: the same
 * shape of failure as the `bindServiceInstance` rename (§6.2.1). Both interfaces are
 * hooked, and both report the concrete methods they matched.
 */
object VirtualActivityTaskManagerHook {

    @Volatile private var installedFor: String? = null

    val boundPackage: String? get() = installedFor

    /**
     * Every method that starts an activity from an `Intent`.
     *
     * Matched structurally for the reason above. `startActivities` takes an `Intent[]`
     * rather than an `Intent`, so both element and array rules are declared and whichever
     * resolves against the real signature is the one that applies.
     */
    internal fun startsActivity(method: Method): Boolean {
        if (!method.name.startsWith("startActivit")) return false
        return method.parameterTypes.any {
            it == Intent::class.java || it == Array<Intent>::class.java
        }
    }

    @Synchronized
    fun install(virtualPackage: String, hostContext: Context): Boolean {
        if (installedFor == virtualPackage) return true

        val hostPackage = hostContext.packageName
        val target = SystemServiceHook.TARGETS.firstOrNull { it.serviceName == "activity_task" }
            ?: run {
                Diagnostics.error(
                    DiagChannel.LAUNCH, "ATM_HOOK_FAILED",
                    mapOf("package" to virtualPackage, "reason" to "no activity_task target"),
                )
                return false
            }
        val report = SystemServiceHook.install(target, shims(virtualPackage, hostPackage))
        if (!report.installed) {
            Diagnostics.error(
                DiagChannel.LAUNCH, "ATM_HOOK_FAILED",
                mapOf("package" to virtualPackage, "reason" to (report.reason ?: "?")),
            )
            return false
        }
        installedFor = virtualPackage
        Diagnostics.info(
            DiagChannel.LAUNCH, "ATM_HOOK_INSTALLED",
            mapOf(
                "package" to virtualPackage,
                "host" to hostPackage,
                "matched" to (report.bind?.describeMatches()?.take(400) ?: "-"),
            ),
        )
        return true
    }

    private fun shims(virtualPackage: String, hostPackage: String): List<MethodShim> = listOf(
        shim("activityStart") {
            matchMethods { method -> startsActivity(method) }
            // The caller's own package travels outward on these calls and is checked
            // against the real uid, exactly as on IActivityManager.
            rewriteAll<String>(matching = { it == virtualPackage }) { hostPackage }
            rewriteAll<Intent> { intent -> routeActivity(hostPackage, intent) }
            rewriteAll<Array<Intent>> { intents ->
                Array(intents.size) { routeActivity(hostPackage, intents[it]) }
            }
        },
    )

    /**
     * Rewrites an activity intent onto a stub, or returns it unchanged.
     *
     * Unchanged is right for anything that is not a virtual activity: UNIQUE's own
     * components share this process, and a guest may legitimately start a *host* activity
     * — a share sheet, a browser — which must reach the real one.
     */
    internal fun routeActivity(hostPackage: String, rawIntent: Intent): Intent {
        val ready = AppBootstrap.current ?: return rawIntent
        // Before anything else: a `content://` URI belonging to this guest is unusable to
        // anyone outside UNIQUE, and this is the last point at which it can be made usable.
        // See VirtualUriGrants.
        val granted = VirtualUriGrants.rewriteOutgoing(hostPackage, rawIntent, ready)
        val intent = retargetGoogleSignInIntent(
            hostPackage, retargetSettingsIntent(hostPackage, granted, ready), ready,
        )
        val component = intent.component
        if (component == null) return routeImplicit(hostPackage, intent, ready)
        if (component.packageName != ready.params.packageName) return intent

        val entry = resolveTarget(ready, component.className) ?: run {
            Diagnostics.warn(
                DiagChannel.LAUNCH, "ACTIVITY_NOT_DECLARED",
                mapOf("activity" to component.className, "package" to ready.params.packageName),
            )
            return intent
        }

        val stubIntent = routeExplicit(hostPackage, intent, ready, entry)
        Diagnostics.event(
            DiagChannel.LAUNCH, DiagLevel.DEBUG, "ACTIVITY_INTENT_ROUTED",
            mapOf(
                "activity" to entry.className,
                "stub" to (stubIntent.component?.className ?: "-"),
                "launchMode" to entry.launchMode.toString(),
            ),
        )
        return stubIntent
    }

    /**
     * Makes the two halves of a Google sign-in request agree about who is asking.
     *
     * The thirteenth phone run timed this failure precisely: the guest's own
     * `SignInHubActivity` starts `com.google.android.gms.auth.GOOGLE_SIGN_IN`, the intent
     * leaves the space, and six hundred milliseconds later the app is told the attempt was
     * cancelled — with no account picker ever drawn. Play services compares the package
     * inside the request's `SignInConfiguration`, which is the guest's, against the package
     * that started the activity, which is `com.unique` because a `:vappN` process is
     * UNIQUE. They disagree, and disagreeing is exactly what that check refuses.
     *
     * So the configuration is rewritten to say `com.unique`, which is the truth about the
     * caller and the same correction [com.unique.core.google.GmsBrokerBinder] already makes
     * on every service bind. See `GoogleSignInHandoff` for what this does and does not buy
     * — it gets the picker drawn; it does not register an OAuth client.
     *
     * The app's own object is never touched. A parcel round trip makes an independent copy
     * first, so a configuration the guest is still holding — `SignInHubActivity` keeps the
     * one it was started with — reads exactly as it did.
     *
     * Every failure returns the intent unchanged and says why, because the outcome of not
     * rewriting is the refusal that was already happening.
     */
    private fun retargetGoogleSignInIntent(
        hostPackage: String,
        intent: Intent,
        ready: AppBootstrap.Result.Ready,
    ): Intent {
        val action = intent.action ?: return intent
        if (!GoogleSignInHandoff.isHandoff(action)) return intent
        val guest = ready.params.packageName
        if (guest == hostPackage) return intent

        val located = locateSignInConfig(intent)
        if (located == null) {
            notRetargeted(action, guest, "the request carries no configuration object")
            return intent
        }
        val copy = copyParcelable(located.config)
        if (copy == null) {
            notRetargeted(action, guest, "the configuration could not be copied")
            return intent
        }
        val fields = GoogleSignInHandoff.rewriteConsumer(copy, guest, hostPackage)
        if (fields == 0) {
            notRetargeted(action, guest, "no field of the configuration names this guest")
            return intent
        }

        val out = Intent(intent)
        if (located.bundle != null) {
            val holder = Bundle(located.bundle)
            holder.putParcelable(GoogleSignInHandoff.CONFIG_KEY, copy)
            out.putExtra(GoogleSignInHandoff.CONFIG_KEY, holder)
        } else {
            out.putExtra(located.key, copy)
        }
        Diagnostics.info(
            DiagChannel.LAUNCH, "GOOGLE_SIGN_IN_RETARGETED",
            mapOf(
                "action" to action,
                "package" to guest,
                "to" to hostPackage,
                "fields" to fields.toString(),
                "shape" to if (located.bundle != null) "bundle" else "extra",
                // Present or absent, never the value. This is what says in advance whether
                // to expect an account or a DEVELOPER_ERROR, so the next log explains
                // itself instead of starting another investigation.
                "serverToken" to
                    if (GoogleSignInHandoff.requestsServerToken(copy)) "requested" else "no",
                "detail" to "Play services refuses a request whose configuration names a " +
                    "package other than the one that started the activity",
            ),
        )
        return out
    }

    private fun notRetargeted(action: String, guestPackage: String, reason: String) {
        Diagnostics.warn(
            DiagChannel.LAUNCH, "GOOGLE_SIGN_IN_NOT_RETARGETED",
            mapOf("action" to action, "package" to guestPackage, "reason" to reason),
        )
    }

    /** The configuration object, the bundle it was in if it was in one, and its key. */
    private class SignInConfig(
        val config: Parcelable,
        val bundle: Bundle?,
        val key: String = GoogleSignInHandoff.CONFIG_KEY,
    )

    /**
     * Finds the configuration in either shape the client library writes it.
     *
     * `SignInHubActivity` reads its own through `getBundleExtra("config")` and writes the
     * onward request with the object directly; which of the two a given release of
     * `play-services-auth` produces is not worth depending on, and both are two lines.
     */
    private fun locateSignInConfig(intent: Intent): SignInConfig? = runCatching {
        val bundle = intent.getBundleExtra(GoogleSignInHandoff.CONFIG_KEY)
        if (bundle != null) {
            @Suppress("DEPRECATION")
            val inner = bundle.getParcelable<Parcelable>(GoogleSignInHandoff.CONFIG_KEY)
            if (inner != null) return@runCatching SignInConfig(inner, bundle)
        }
        @Suppress("DEPRECATION")
        val direct = intent.getParcelableExtra<Parcelable>(GoogleSignInHandoff.CONFIG_KEY)
        if (direct != null) return@runCatching SignInConfig(direct, null)

        // The key is not what this depends on. If a release of the client library ever
        // spells it differently, the *type* is still what it is, and the extras of an
        // intent the guest built in this process are already objects — so one pass over
        // them costs nothing and removes a name from the contract.
        val extras = intent.extras ?: return@runCatching null
        for (key in extras.keySet()) {
            @Suppress("DEPRECATION")
            val value = runCatching { extras.get(key) }.getOrNull()
            if (value is Parcelable &&
                GoogleSignInHandoff.looksLikeConfiguration(value.javaClass.name)
            ) {
                return@runCatching SignInConfig(value, null, key)
            }
        }
        null
    }.getOrNull()

    /**
     * An independent copy of a `Parcelable`, made the way the platform would make one.
     *
     * Reflection on the original would be shorter and would mutate an object the guest
     * still holds. A parcel round trip through the class's own `CREATOR` costs a few
     * hundred bytes and leaves the app's copy alone.
     */
    private fun copyParcelable(value: Parcelable): Parcelable? {
        val parcel = Parcel.obtain()
        return try {
            value.writeToParcel(parcel, 0)
            parcel.setDataPosition(0)
            @Suppress("UNCHECKED_CAST")
            val creator = value.javaClass.getField("CREATOR")
                .get(null) as Parcelable.Creator<Parcelable>
            creator.createFromParcel(parcel)
        } catch (error: Throwable) {
            null
        } finally {
            parcel.recycle()
        }
    }

    /** Settings screens an app opens *about itself*, all of them naming it in the data URI. */
    private const val SETTINGS_ACTION_PREFIX = "android.settings."

    /** `Settings.EXTRA_APP_PACKAGE` — the notification screens name the app in an extra. */
    private const val EXTRA_APP_PACKAGE = "android.provider.extra.APP_PACKAGE"

    /**
     * Points a guest's "open my settings page" intent at UNIQUE's, which is the real one.
     *
     * Every special access an app cannot request with a dialog — all-files access, usage
     * access, overlay, exact alarms, unknown sources, and the notification screens — is
     * asked for by starting a Settings activity that names the app in a `package:` URI:
     *
     * ```java
     * startActivity(new Intent(ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
     *                          Uri.parse("package:" + getPackageName())));
     * ```
     *
     * On a device that opens the app's own page. Under UNIQUE the package named is not
     * installed, so Settings has nothing to open — in the fourth device log a cleaner app
     * sent exactly this and it left the guest for `com.android.settings`, which could only
     * fail. The user is left with a dead end in the middle of the app's own onboarding.
     *
     * The uid that would actually hold the access is UNIQUE's — a guest runs inside it —
     * so UNIQUE's page is not a substitute for the guest's page, it *is* the page where
     * this decision is made. Retargeting the URI takes the user to the switch that works.
     *
     * Narrow on purpose: only `android.settings.*` actions, only when the package named is
     * this guest's. A guest opening some other app's settings page, or a Settings screen
     * that is not about a package at all, is left exactly as it was.
     */
    private fun retargetSettingsIntent(
        hostPackage: String,
        intent: Intent,
        ready: AppBootstrap.Result.Ready,
    ): Intent {
        // The action is checked before anything else so an ordinary start costs one
        // string comparison: reading an extra unparcels the whole bundle, and an intent
        // that came from another app can carry a class this process cannot load.
        val action = intent.action ?: return intent
        if (!action.startsWith(SETTINGS_ACTION_PREFIX)) return intent
        val guest = ready.params.packageName
        val uri = intent.data
        val decision = settingsRetarget(
            action = action,
            dataScheme = uri?.scheme,
            dataPackage = uri?.schemeSpecificPart,
            extraPackage = runCatching { intent.getStringExtra(EXTRA_APP_PACKAGE) }.getOrNull(),
            guestPackage = guest,
        )
        if (!decision.needed) return intent

        val retargeted = Intent(intent).apply {
            if (decision.rewriteData) {
                setData(android.net.Uri.fromParts("package", hostPackage, null))
            }
            if (decision.rewriteExtra) putExtra(EXTRA_APP_PACKAGE, hostPackage)
        }
        Diagnostics.info(
            DiagChannel.LAUNCH, "SETTINGS_INTENT_RETARGETED",
            mapOf(
                "action" to action,
                "package" to guest,
                "to" to hostPackage,
                "detail" to "the guest's package is not installed, and the access this " +
                    "screen grants is held by UNIQUE's uid",
            ),
        )
        return retargeted
    }

    /** Which halves of a Settings intent name the guest. */
    internal data class SettingsRetarget(val rewriteData: Boolean, val rewriteExtra: Boolean) {
        val needed: Boolean get() = rewriteData || rewriteExtra
    }

    /**
     * The decision behind [retargetSettingsIntent], as plain strings so it can be tested.
     *
     * Both halves are checked independently because an app may set either: the all-files
     * and overlay screens read the `package:` URI, the notification screens read
     * `Settings.EXTRA_APP_PACKAGE`, and `APP_NOTIFICATION_SETTINGS` is sometimes sent with
     * both.
     */
    internal fun settingsRetarget(
        action: String?,
        dataScheme: String?,
        dataPackage: String?,
        extraPackage: String?,
        guestPackage: String,
    ): SettingsRetarget {
        if (action == null || !action.startsWith(SETTINGS_ACTION_PREFIX)) {
            return SettingsRetarget(rewriteData = false, rewriteExtra = false)
        }
        return SettingsRetarget(
            rewriteData = dataScheme == "package" && dataPackage == guestPackage,
            rewriteExtra = extraPackage == guestPackage,
        )
    }

    /** Builds the stub intent for a guest activity that has already been chosen. */
    private fun routeExplicit(
        hostPackage: String,
        intent: Intent,
        ready: AppBootstrap.Result.Ready,
        entry: ComponentEntry,
    ): Intent {
        val stubParams = ready.params.copy(
            targetComponent = entry.className,
            kind = VirtualComponentKind.ACTIVITY,
        )
        return Intent(intent).apply {
            setPackage(null)
            stubParams.writeTo(this)
            VirtualLaunchIntent.stampIdentity(this, guest = intent, params = stubParams)
            component = android.content.ComponentName(
                hostPackage,
                StubRouter.stubActivity(ready.params.slot, entry.launchMode.coerceIn(0, 3), 0),
            )
        }
    }

    /**
     * Resolves an implicit start against the guest's own filters, or lets it go.
     *
     * See [VirtualIntentResolver] for which side wins and why. Every outcome is recorded
     * with the rule that produced it, because "it opened the wrong thing" is a question
     * that needs an answer and not a shrug.
     */
    private fun routeImplicit(
        hostPackage: String,
        intent: Intent,
        ready: AppBootstrap.Result.Ready,
    ): Intent {
        val guestPackage = ready.params.packageName
        val matches = VirtualIntentResolver.matchingActivities(ready.manifest, intent)
        if (matches.isEmpty()) {
            // Left to the platform, which is right — an app opening a browser or a share
            // sheet does exactly this on a real device, and trapping it inside the guest
            // would break behaviour that works.
            //
            // But it is the one path by which a guest's intent reaches an *installed* app,
            // with that app's data, so it is reported at INFO with the packages that could
            // answer it. Gemini's shell activity fires an implicit ACTION_VIEW within fifty
            // milliseconds of starting; the host's Google app answers it, and the user sees
            // their real account in what they launched as a fresh instance. That is faithful
            // to the app and confusing to the person, and the only thing that makes it
            // legible afterwards is this line naming where the intent went.
            val handlers = AppBootstrap.hostContext?.let {
                VirtualIntentResolver.hostHandlersFor(it, intent, hostPackage)
            }.orEmpty()
            Diagnostics.info(
                DiagChannel.LAUNCH, "ACTIVITY_IMPLICIT_LEFT_GUEST",
                mapOf(
                    "action" to (intent.action ?: "-"),
                    "data" to (intent.data?.scheme ?: "-"),
                    "package" to guestPackage,
                    "handledByHost" to handlers.joinToString(",").ifEmpty { "nothing" },
                    "detail" to "no activity of the guest matches; the host's own apps " +
                        "answer this intent, with the host's data",
                ),
            )
            return intent
        }

        val scopedToGuest = intent.`package` == guestPackage ||
            intent.selector?.`package` == guestPackage
        if (!scopedToGuest) {
            val context = AppBootstrap.hostContext
            if (context != null &&
                VirtualIntentResolver.hostCanHandle(context, intent, hostPackage)
            ) {
                // An https VIEW belongs in a browser and a SEND belongs in the chooser.
                // Pulling either into the guest would break behaviour that works today.
                Diagnostics.info(
                    DiagChannel.LAUNCH, "ACTIVITY_IMPLICIT_HOST_PREFERRED",
                    mapOf(
                        "action" to (intent.action ?: "-"),
                        "package" to guestPackage,
                        "guestMatches" to matches.size.toString(),
                        "guestBest" to matches.first().entry.className,
                    ),
                )
                return intent
            }
        }

        val best = matches.first()
        val routed = routeExplicit(hostPackage, intent, ready, best.entry)
        Diagnostics.info(
            DiagChannel.LAUNCH, "ACTIVITY_IMPLICIT_ROUTED",
            mapOf(
                "action" to (intent.action ?: "-"),
                "data" to (intent.data?.scheme ?: "-"),
                "activity" to best.entry.className,
                "package" to guestPackage,
                "reason" to if (scopedToGuest) "scoped" else "onlyHandler",
                "candidates" to matches.size.toString(),
            ),
        )
        return routed
    }

    /** An activity or the alias's target activity, as the platform resolves it. */
    private fun resolveTarget(ready: AppBootstrap.Result.Ready, className: String): ComponentEntry? {
        val direct = ready.manifest.components.firstOrNull {
            it.kind == ComponentKind.ACTIVITY && it.className == className
        }
        if (direct != null) return direct
        // <activity-alias name="X" targetActivity="Y"> is started as X and instantiated
        // as Y; the stub has to carry the class the platform will actually construct.
        val alias = ready.manifest.components.firstOrNull {
            it.kind == ComponentKind.ACTIVITY_ALIAS && it.className == className
        } ?: return null
        val targetName = alias.targetActivity ?: return null
        return ready.manifest.components.firstOrNull {
            it.kind == ComponentKind.ACTIVITY && it.className == targetName
        }
    }
}
