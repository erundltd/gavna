package com.unique.core.google

import com.google.android.gms.auth.api.signin.internal.GoogleSignInOptionsDouble
import com.google.android.gms.auth.api.signin.internal.SignInConfigurationDouble
import com.google.common.truth.Truth.assertThat
import org.junit.Test

/**
 * The rewrite that decides whether a Google sign-in gets as far as an account picker.
 *
 * These are pinned off a device because the failure they fix is a *silent* one: Play
 * services answers `RESULT_CANCELED` in under a second and nothing distinguishes "the
 * rewrite did not happen" from "the user changed their mind". The thirteenth phone run
 * cost a round to that ambiguity.
 */
class GoogleSignInHandoffTest {

    private val guest = "com.axlebolt.standoff2"
    private val host = "com.unique"

    private fun config(
        consumer: String = guest,
        serverClientId: String? = null,
    ) = SignInConfigurationDouble(
        consumer,
        GoogleSignInOptionsDouble(listOf("email", "profile"), serverClientId),
    )

    @Test
    fun `both sign-in handoff actions are recognised`() {
        assertThat(GoogleSignInHandoff.isHandoff("com.google.android.gms.auth.GOOGLE_SIGN_IN"))
            .isTrue()
        assertThat(GoogleSignInHandoff.isHandoff("com.google.android.gms.auth.APPAUTH_SIGN_IN"))
            .isTrue()
    }

    @Test
    fun `anything else is left alone`() {
        assertThat(GoogleSignInHandoff.isHandoff(null)).isFalse()
        assertThat(GoogleSignInHandoff.isHandoff("android.intent.action.VIEW")).isFalse()
        // Neighbouring Google actions that are not a sign-in handoff. Widening this set
        // would rewrite requests whose package field means something else.
        assertThat(GoogleSignInHandoff.isHandoff("com.google.android.gms.auth.NO_IMPL"))
            .isFalse()
    }

    @Test
    fun `the consumer package is replaced with the host's`() {
        val subject = config()
        assertThat(GoogleSignInHandoff.rewriteConsumer(subject, guest, host)).isEqualTo(1)
        assertThat(subject.zba).isEqualTo(host)
    }

    @Test
    fun `a field naming some other package is not touched`() {
        val subject = config()
        GoogleSignInHandoff.rewriteConsumer(subject, guest, host)
        assertThat(subject.unrelated).isEqualTo("com.example.other")
    }

    @Test
    fun `a static field is never considered, even when its value matches`() {
        val subject = config(consumer = "com.example.guest")
        val fields = GoogleSignInHandoff.consumerFields(subject, "com.example.guest")
        assertThat(fields.map { it.name }).containsExactly("zba")
    }

    @Test
    fun `a configuration that does not name the guest changes nothing`() {
        val subject = config(consumer = "com.example.somethingelse")
        assertThat(GoogleSignInHandoff.rewriteConsumer(subject, guest, host)).isEqualTo(0)
    }

    @Test
    fun `a guest running as the host is not rewritten`() {
        val subject = config(consumer = host)
        assertThat(GoogleSignInHandoff.rewriteConsumer(subject, host, host)).isEqualTo(0)
    }

    @Test
    fun `an inherited field naming the guest is found too`() {
        // `declaredFields` alone would miss it. The configuration's fields are its own
        // today; a walk that assumes that would fail silently if it ever stopped being so.
        val subject = InheritingConfiguration(guest)
        assertThat(GoogleSignInHandoff.rewriteConsumer(subject, guest, host)).isEqualTo(1)
        assertThat(subject.consumer).isEqualTo(host)
    }

    @Test
    fun `the configuration is recognised by type, not only by the extra it arrives under`() {
        assertThat(GoogleSignInHandoff.looksLikeConfiguration(
            "com.google.android.gms.auth.api.signin.internal.SignInConfiguration")).isTrue()
        assertThat(GoogleSignInHandoff.looksLikeConfiguration(null)).isFalse()
        assertThat(GoogleSignInHandoff.looksLikeConfiguration(
            "com.google.android.gms.auth.api.signin.GoogleSignInOptions")).isFalse()
        // A package that merely ends with the word is not the class.
        assertThat(GoogleSignInHandoff.looksLikeConfiguration("SignInConfiguration.Holder"))
            .isFalse()
    }

    @Test
    fun `a request for a server token is reported`() {
        val subject = config(serverClientId = "123456-abc.apps.googleusercontent.com")
        assertThat(GoogleSignInHandoff.requestsServerToken(subject)).isTrue()
    }

    @Test
    fun `a request for the account alone is reported as such`() {
        assertThat(GoogleSignInHandoff.requestsServerToken(config())).isFalse()
    }

    @Test
    fun `the walk does not follow an app's own classes`() {
        // The budget and the `com.google.` gate are what keep this off an activity start's
        // critical path. A non-Google object is not descended into at all.
        assertThat(GoogleSignInHandoff.requestsServerToken(NotGoogles())).isFalse()
    }

    private open class HasConsumer(@JvmField val consumer: String)

    private class InheritingConfiguration(consumer: String) : HasConsumer(consumer)

    private class NotGoogles {
        @Suppress("unused")
        val clientId: String = "123456-abc.apps.googleusercontent.com"
    }
}
