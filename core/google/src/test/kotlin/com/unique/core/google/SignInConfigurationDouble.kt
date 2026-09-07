// Test doubles for the two objects a Google sign-in request is made of.
//
// They are in Google's own package on purpose. `GoogleSignInHandoff.requestsServerToken`
// only descends into classes named `com.google.…`, which is what keeps a walk that runs on
// every activity start from wandering into an app's whole object graph — so a double in
// any other package would be testing a code path the real one never takes. The field
// *names* are Google's obfuscated ones for the same reason: nothing here may match on them.
package com.google.android.gms.auth.api.signin.internal

/** Stands in for `GoogleSignInOptions`. */
class GoogleSignInOptionsDouble(
    @JvmField val zba: List<String>,
    @JvmField val zbf: String?,
)

/** Stands in for `SignInConfiguration`, whose first field is the consumer package name. */
class SignInConfigurationDouble(
    @JvmField val zba: String,
    @JvmField val zbb: GoogleSignInOptionsDouble,
) {
    @JvmField val unrelated: String = "com.example.other"

    @Suppress("unused")
    private val notAString: Int = 7

    companion object {
        /** Static, and must not be considered even when its value matches. */
        @JvmStatic
        val staticName: String = "com.example.guest"
    }
}
