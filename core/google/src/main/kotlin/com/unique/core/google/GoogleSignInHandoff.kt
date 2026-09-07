package com.unique.core.google

import java.lang.reflect.Field
import java.lang.reflect.Modifier

/**
 * The one field that decides whether a Google sign-in is even attempted.
 *
 * ## What the thirteenth phone run actually showed
 *
 * The failure everyone has been describing as "Google sign-in does not work in a virtual
 * space" is, on this device, not a token problem and not a certificate problem. It is a
 * refusal that happens **before the account picker is drawn**:
 *
 * ```
 * ACTIVITY_INTENT_ROUTED  activity=…signin.internal.SignInHubActivity   00:28:03.088
 * ACTIVITY_IMPLICIT_LEFT_GUEST action=com.google.android.gms.auth.GOOGLE_SIGN_IN
 *                              handledByHost=com.google.android.gms     00:28:03.194
 * D TokenPendingResult: Calling onResult … Status{statusCode=CANCELED}   00:28:03.816
 * ```
 *
 * Six hundred milliseconds, and nothing was shown to the user. The app's own
 * `SignInHubActivity` — which is *its* class, out of the `play-services-auth` it bundles —
 * hands Play services a `SignInConfiguration`, and the first field of that object is the
 * package name of the app asking:
 *
 * ```java
 * new SignInConfiguration(context.getPackageName(), googleSignInOptions)
 * ```
 *
 * Inside UNIQUE `context.getPackageName()` is the guest's, correctly, because that is the
 * whole point of the graft. Play services then compares it with the package that actually
 * started the activity — which is `com.unique`, because a `:vappN` process *is* UNIQUE —
 * and the two do not match. A caller claiming to be a package it is not is exactly what
 * that check is for, so Play services answers `RESULT_CANCELED` and the user is told
 * "Попытка входа отменена".
 *
 * ## Why the name is changed and not the check defeated
 *
 * `com.unique` is the truthful answer to "which package started this activity", and it is
 * the same answer `GmsBrokerBinder` already gives on every service bind for the same
 * reason — the uid is UNIQUE's and no rewriting changes that. Making the two halves of
 * the request agree is what turns an unconditional refusal into a real sign-in attempt.
 *
 * ## What this does not do, stated before anyone measures it
 *
 * It gets the account picker drawn. It does **not** conjure an OAuth client:
 *
 *  - Basic sign-in — id, email, display name — needs no registered client and should
 *    complete.
 *  - `requestIdToken(serverClientId)` and `requestServerAuthCode(...)` are validated
 *    against the Android OAuth client registered for the *calling* package and
 *    certificate. That is `com.unique` now, which no developer has registered, and the
 *    documented answer is `DEVELOPER_ERROR` (10).
 *
 * Which of those two a given app is in is a property of its own `GoogleSignInOptions` and
 * is not knowable from here, so [requestsServerToken] reports it — present or absent, never
 * the value — and the log says which answer to expect before Google gives it. Stripping
 * the token request to force a "success" is deliberately not done: an app handed an
 * account without the token it asked for fails later, on its own server, in a way nobody
 * can read.
 *
 * Everything is by shape and never by field name. `SignInConfiguration`'s fields are
 * `zba` and `zbb` in one release of `play-services-auth` and something else in the next,
 * while the *value* — a string equal to the guest's package name — is what it is.
 */
object GoogleSignInHandoff {

    /**
     * The activity actions that hand a sign-in over to the device's Play services.
     *
     * `GOOGLE_SIGN_IN` is the legacy `GoogleSignInClient` flow, `APPAUTH_SIGN_IN` the one
     * `SignInHubActivity` uses when the options carry an AppAuth-style request. Both
     * carry the same configuration object and both are refused the same way.
     */
    val ACTIONS: Set<String> = setOf(
        "com.google.android.gms.auth.GOOGLE_SIGN_IN",
        "com.google.android.gms.auth.APPAUTH_SIGN_IN",
    )

    /** The extra `SignInHubActivity` puts the configuration under, in both shapes. */
    const val CONFIG_KEY = "config"

    /** A Google OAuth client id always ends this way, whatever project issued it. */
    private const val CLIENT_ID_SUFFIX = ".apps.googleusercontent.com"

    fun isHandoff(action: String?): Boolean = action != null && action in ACTIONS

    /**
     * Instance fields of [config] whose value is exactly [guestPackage].
     *
     * Declared fields only, and only `String`s: the configuration is a flat SafeParcelable
     * whose consumer package is a top-level field, and searching deeper would risk
     * rewriting a package name that means something else.
     */
    fun consumerFields(config: Any, guestPackage: String): List<Field> =
        config.javaClass.declaredFields
            .filter { !Modifier.isStatic(it.modifiers) && it.type == String::class.java }
            .filter { field ->
                runCatching {
                    field.isAccessible = true
                    field.get(config) == guestPackage
                }.getOrDefault(false)
            }

    /**
     * Replaces every field naming the guest with [hostPackage]. Returns how many changed.
     *
     * Zero is a legitimate answer and the caller treats it as "leave the intent alone":
     * a configuration that does not name the guest is one this reasoning does not apply
     * to, and a rewrite made anyway would be a guess.
     */
    fun rewriteConsumer(config: Any, guestPackage: String, hostPackage: String): Int {
        if (guestPackage == hostPackage) return 0
        var changed = 0
        for (field in consumerFields(config, guestPackage)) {
            val ok = runCatching {
                field.isAccessible = true
                field.set(config, hostPackage)
                field.get(config) == hostPackage
            }.getOrDefault(false)
            if (ok) changed++
        }
        return changed
    }

    /**
     * Whether the request asks Google for a token issued to a server client.
     *
     * The presence of an OAuth client id anywhere in the configuration is the tell, and
     * the value is never read out or logged. This exists only so that a log carrying a
     * `DEVELOPER_ERROR` afterwards is explained by the line above it rather than
     * investigated from scratch.
     *
     * Bounded on purpose: three levels, two hundred fields, and only into Google's own
     * classes and plain collections. This runs on an activity start.
     */
    fun requestsServerToken(config: Any): Boolean {
        val seen = HashSet<Any>()
        var budget = 200
        fun walk(value: Any?, depth: Int): Boolean {
            if (value == null || depth > 3 || budget <= 0) return false
            budget--
            when (value) {
                is String -> return value.endsWith(CLIENT_ID_SUFFIX)
                is Iterable<*> -> return value.any { walk(it, depth + 1) }
                is Array<*> -> return value.any { walk(it, depth + 1) }
            }
            if (!value.javaClass.name.startsWith("com.google.")) return false
            if (!seen.add(value)) return false
            return value.javaClass.declaredFields
                .filter { !Modifier.isStatic(it.modifiers) }
                .any { field ->
                    runCatching {
                        field.isAccessible = true
                        walk(field.get(value), depth + 1)
                    }.getOrDefault(false)
                }
        }
        return runCatching { walk(config, 0) }.getOrDefault(false)
    }
}
