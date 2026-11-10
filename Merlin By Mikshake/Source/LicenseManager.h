#pragma once

#include <JuceHeader.h>
#include <map>
#include "AppMode.h"   // merlinVersionLabel()

//==============================================================================
// Licence Merlin α — DRM SideKey (https://license.sidekey.fr, D:/vst/LicenseServer).
//
// Différent des autres produits : la licence est validée UNE fois en ligne, puis
// Merlin marche hors ligne pour toujours. À l'activation, le serveur renvoie un
// certificat (signature RSA PKCS#1 v1.5 / SHA-256 de "produit|clé|machine"),
// vérifié ici avec la clé publique : un license.json fabriqué à la main est
// refusé. À chaque lancement, un signal part en arrière-plan (/v1/validate) pour
// que les connexions apparaissent sur le DRM ; il ne bloque jamais, sauf si le
// serveur répond explicitement « revoked » (remboursement, litige).
// Démo de 15 jours : départ local, recalé sur la date de premier signal connue
// du serveur (effacer le fichier ne relance pas la démo).
//
// Clés BÊTA (v1.81.33) : une clé créée avec une date de fin (expires_at dans
// /admin) reçoit un certificat signé sur "produit|clé|machine|<date>", la date
// étant renvoyée telle quelle (UTC "AAAA-MM-JJ HH:MM:SS"). Merlin se bloque à
// cette date même hors ligne (statut betaExpired, écran « bêta terminée ») ;
// retirer la date du fichier casse la signature. Après la date, le serveur
// répond « expired ». Si la date est retirée dans /admin, le signal de
// lancement ou une réactivation rapporte un certificat sans date : la clé
// devient définitive. Les clés vendues (sans date) ne changent pas.
//
// Une seule instance par processus (SharedResourcePointer) : l'appli et toutes
// les instances du plugin partagent le même état et le même fichier.
// Les requêtes tournent sur des threads à part ; leurs réponses reviennent sur
// le thread de messages via une référence faible (le gestionnaire peut avoir
// été détruit entre-temps, DAW qui ferme le plugin).
//==============================================================================
class LicenseManager : public juce::ChangeBroadcaster,
                       private juce::Timer
{
public:
    enum class Status { unlicensed, activating, licensed, invalid, revoked, trial, trialExpired, betaExpired };

    static constexpr const char* productId = "merlin-alpha";
    static constexpr int trialDays = 15;

    LicenseManager()
    {
        machineId = juce::SystemStats::getUniqueDeviceID();
        load();

        if (storedKey.isNotEmpty() && verifyCertificate (storedKey, certificate, expiresAt))
        {
            status = betaHasEnded() ? Status::betaExpired : Status::licensed;
            if (status.load() == Status::licensed)
                noteClock();
            pingLaunch();   // aussi en fin de bêta : une clé devenue définitive se débloque
        }
        else if (trialStart != juce::Time())
        {
            recomputeTrial();
            pingTrial();
        }

        sessionStart = juce::Time::getMillisecondCounterHiRes();
        startTimer (usagePingMs);
    }

    ~LicenseManager() override { stopTimer(); }

    // Fenêtre d'activation : clé obligatoire, nom et email facultatifs
    void activate (const juce::String& key, const juce::String& firstName = {},
                   const juce::String& lastName = {}, const juce::String& email = {})
    {
        const auto k = key.trim();
        if (k.isEmpty() || status.load() == Status::activating)
            return;

        status = Status::activating;
        notify();

        juce::Thread::launch ([self = juce::WeakReference<LicenseManager> (this), mid = machineId, k,
                               f = firstName.trim(), l = lastName.trim(), e = email.trim()]
        {
            auto r = postJson ("/v1/activate",
                { { "license_key", k }, { "machine_id", mid },
                  { "machine_name", juce::SystemStats::getComputerName() },
                  { "product_id", productId }, { "plugin_version", merlinVersionLabel() },
                  { "first_name", f }, { "last_name", l }, { "email", e } });

            const auto cert = r.json.getProperty ("certificate", "").toString();
            const auto expires = expiresField (r.json);

            juce::MessageManager::callAsync ([self, k, r, cert, expires]
            {
                auto* m = self.get();
                if (m == nullptr)
                    return;

                if (r.ok && m->verifyCertificate (k, cert, expires))
                {
                    m->storedKey = k;
                    m->certificate = cert;
                    m->expiresAt = expires;
                    m->betaEndedByServer = false;
                    m->status = m->betaHasEnded() ? Status::betaExpired : Status::licensed;
                    m->lastError = {};
                    m->save();
                }
                else
                {
                    m->status = Status::invalid;
                    m->lastError = r.ok ? juce::String ("bad_certificate")
                                        : (r.networkError ? juce::String ("network_error") : r.error);
                    if (m->lastError == "machine_limit_reached")
                        m->maxMachines = (int) r.json.getProperty ("max", m->maxMachines);
                }
                m->notify();
            });
        });
    }

    // « Désactiver sur cet ordinateur » : libère la place sur le DRM (en ligne)
    // pour réactiver la licence ailleurs ; la licence locale est effacée.
    void deactivate()
    {
        if (status.load() != Status::licensed || deactivating)
            return;
        deactivating = true;
        notify();

        juce::Thread::launch ([self = juce::WeakReference<LicenseManager> (this), key = storedKey, mid = machineId]
        {
            auto r = postJson ("/v1/deactivate", { { "license_key", key }, { "machine_id", mid } });

            juce::MessageManager::callAsync ([self, r]
            {
                auto* m = self.get();
                if (m == nullptr)
                    return;
                m->deactivating = false;
                if (r.ok)
                {
                    m->storedKey = {};
                    m->certificate = {};
                    m->expiresAt = {};
                    m->betaEndedByServer = false;
                    m->status = Status::unlicensed;
                    m->lastError = {};
                    if (m->trialStart != juce::Time())
                        m->recomputeTrial();
                    m->save();
                }
                else
                {
                    m->lastError = r.networkError ? juce::String ("network_error") : r.error;   // reste activée
                }
                m->notify();
            });
        });
    }

    // « Rejoindre la bêta » (même système que Sevenpack Alpha) : nom + email ->
    // /v1/beta-join rend une clé bêta pour CETTE machine, signée avec la date de
    // fin du programme. Elle vit ensuite comme une clé bêta tapée (date de fin
    // tenue hors ligne, contrôle au lancement). Rejoindre deux fois depuis la même
    // machine rend la même clé. En cas d'échec le statut ne change pas (une démo
    // en cours continue) : seul getBetaJoinError() le dit.
    void joinBeta (const juce::String& firstName, const juce::String& lastName, const juce::String& email)
    {
        const auto s = status.load();
        if (joiningBeta || s == Status::activating || s == Status::licensed)
            return;
        joiningBeta = true;
        betaJoinError = {};
        notify();

        juce::Thread::launch ([self = juce::WeakReference<LicenseManager> (this), mid = machineId, e = email.trim(),
                               name = (firstName.trim() + " " + lastName.trim()).trim()]
        {
            auto r = postJson ("/v1/beta-join",
                { { "machine_id", mid }, { "product_id", productId }, { "email", e }, { "customer_name", name },
                  { "machine_name", juce::SystemStats::getComputerName() },
                  { "plugin_version", merlinVersionLabel() } });

            const auto key = r.json.getProperty ("license_key", "").toString();
            const auto cert = r.json.getProperty ("certificate", "").toString();
            const auto expires = expiresField (r.json);

            juce::MessageManager::callAsync ([self, r, key, cert, expires]
            {
                auto* m = self.get();
                if (m == nullptr)
                    return;
                m->joiningBeta = false;
                if (r.ok && key.isNotEmpty() && m->verifyCertificate (key, cert, expires))
                {
                    m->storedKey = key;
                    m->certificate = cert;
                    m->expiresAt = expires;
                    m->betaEndedByServer = false;
                    m->status = m->betaHasEnded() ? Status::betaExpired : Status::licensed;
                    m->lastError = {};
                    m->save();
                }
                else
                {
                    m->betaJoinError = r.ok ? juce::String ("bad_certificate")
                                     : r.networkError ? juce::String ("network_error")
                                     : r.error.isNotEmpty() ? r.error : juce::String ("unknown");
                }
                m->notify();
            });
        });
    }

    bool isJoiningBeta() const          { return joiningBeta; }
    juce::String getBetaJoinError() const { return betaJoinError; }
    void clearBetaJoinError()           { betaJoinError = {}; }

    bool isDeactivating() const { return deactivating; }
    int getMaxMachines() const  { return maxMachines; }   // renvoyé par le serveur quand la limite est atteinte

    // « Essayer 15 jours » : une seule fois par machine
    void startDemo()
    {
        if (trialStart == juce::Time())
        {
            trialStart = juce::Time::getCurrentTime();
            save();
        }
        recomputeTrial();
        pingTrial();
        notify();
    }

    Status getStatus() const           { return status.load(); }
    juce::String getLastError() const  { return lastError; }
    juce::String getLicenseKey() const { return status.load() == Status::licensed ? storedKey : juce::String(); }
    bool canStartDemo() const          { return trialStart == juce::Time(); }

    // Merlin mesure sans restriction
    bool isUsable() const
    {
        const auto s = status.load();
        return s == Status::licensed || s == Status::trial;
    }

    int getTrialDaysLeft() const
    {
        const int total = trialDays + bonusDays;
        if (trialStart == juce::Time())
            return total;
        const auto elapsed = (int) (juce::Time::getCurrentTime() - trialStart).inDays();
        return juce::jlimit (0, total, total - elapsed);
    }

    // Clé bêta (avec date de fin signée), active ou terminée
    bool isBetaKey() const
    {
        const auto s = status.load();
        return expiresAt.isNotEmpty() && (s == Status::licensed || s == Status::betaExpired);
    }

    // Fin de la bêta, en heure locale (Time() si la clé n'a pas de date)
    juce::Time getBetaEnd() const { return expiresAt.isNotEmpty() ? parseUtc (expiresAt) : juce::Time(); }

    // Jours entamés restants avant la fin de la bêta (0 = terminée)
    int getBetaDaysLeft() const
    {
        if (expiresAt.isEmpty() || betaHasEnded())
            return 0;
        const auto ms = getBetaEnd().toMilliseconds() - juce::Time::getCurrentTime().toMilliseconds();
        return juce::jmax (1, (int) ((ms + 86399999) / 86400000));
    }

private:
    static constexpr int usagePingMs = 15 * 60 * 1000;   // temps d'utilisation remonté par tranches

    // Clé publique RSA 2048 (e,n en hexadécimal) — la clé privée reste sur le serveur
    static constexpr const char* publicKey =
        "10001,b4ddbf320aa9a622c41bf52e0f7111c30204b4ffbfcaf3754b389fc1f214ed7fe1fb740ad3f4a69e79aaa54aeb923a93"
        "41dd475bb22ade63fb75415795c85dcefb8994b0d2fabcffbae1d69450eb1b017dfff6bf3c93f08451f8ccc626a7ca0800fbdfc8"
        "7408e1c0d015f0a8ee7c1fcd77ef935acf0b0e9aa46718244bd63d046b60c4334c85f2ba41b7e2faa0d839da5c5428862a5ae3ae"
        "37b090bacecb6ccbe4705e7a7c29deb59ebcec7127f0582ecd6f4d2d372df09fd273c85a18fc28b336f16b7c0b677ccc9b3c09ff"
        "41152d292548d5011b6a58d623485e6e5b9499afd784aa1ddcc7f7bb5cfa98648340252b13bbce1d086f675d25cfda797ff802d5";

    static constexpr const char* serverUrl = "https://license.sidekey.fr";

    juce::String machineId, storedKey, certificate, lastError;
    juce::String expiresAt;            // date de fin signée d'une clé bêta, telle que renvoyée par le serveur
    juce::int64 lastSeenMs = 0;        // horloge la plus avancée vue (heure reculée = bêta terminée)
    bool betaEndedByServer = false;    // le serveur a répondu « expired » (date avancée dans /admin)
    std::atomic<Status> status { Status::unlicensed };
    juce::Time trialStart;
    int bonusDays = 0;
    int maxMachines = 5;
    bool deactivating = false;
    bool joiningBeta = false;          // « Rejoindre la bêta » en cours
    juce::String betaJoinError;        // dernière erreur de l'inscription bêta (vide = aucune)
    double sessionStart = 0.0;

    // les éditeurs (appli, instances du plugin) écoutent en ChangeListener
    void notify() { sendChangeMessage(); }

    //==========================================================================
    // Certificat : s^e mod n doit redonner l'encodage PKCS#1 v1.5 du SHA-256.
    // Clé bêta : la date de fin fait partie de la charge signée.
    bool verifyCertificate (const juce::String& key, const juce::String& certHex, const juce::String& expires) const
    {
        if (key.isEmpty() || certHex.length() < 64)
            return false;
        juce::BigInteger v;
        v.parseString (certHex, 16);
        if (v.isZero())
            return false;
        juce::RSAKey (publicKey).applyToValue (v);

        auto payload = juce::String (productId) + "|" + key + "|" + machineId;
        if (expires.isNotEmpty())
            payload << "|" << expires;
        const auto hash = juce::SHA256 (payload.toUTF8()).toHexString();
        const auto expected = "1" + juce::String::repeatedString ("ff", 202) + "00"
                            + "3031300d060960864801650304020105000420" + hash;
        return v.toString (16) == expected;
    }

    // "expires_at" d'une réponse du serveur : chaîne non vide, sinon rien (clé sans date)
    static juce::String expiresField (const juce::var& json)
    {
        const auto v = json.getProperty ("expires_at", juce::var());
        return v.isString() ? v.toString().trim() : juce::String();
    }

    // Date UTC du serveur ("AAAA-MM-JJ HH:MM:SS", ou "AAAA-MM-JJ" = début du jour)
    static juce::Time parseUtc (const juce::String& s)
    {
        auto iso = s.trim().replaceCharacter (' ', 'T');
        if (! iso.containsChar ('T'))
            iso << "T00:00:00";
        if (! iso.endsWithChar ('Z') && ! iso.fromFirstOccurrenceOf ("T", false, false).containsAnyOf ("+-"))
            iso << "Z";
        return juce::Time::fromISO8601 (iso);
    }

    // Fin de bêta : date passée, date illisible, horloge reculée de plus de 2 jours
    // sous la plus avancée déjà vue, ou « expired » renvoyé par le serveur
    bool betaHasEnded() const
    {
        if (expiresAt.isEmpty())
            return false;
        if (betaEndedByServer)
            return true;
        const auto end = parseUtc (expiresAt).toMilliseconds();
        const auto now = juce::Time::getCurrentTime().toMilliseconds();
        if (end <= 0 || now >= end)
            return true;
        return lastSeenMs > 0 && now < lastSeenMs - 2LL * 86400000;
    }

    // Retient l'horloge la plus avancée (écrite au plus une fois par heure)
    void noteClock()
    {
        if (expiresAt.isEmpty())
            return;
        const auto now = juce::Time::getCurrentTime().toMilliseconds();
        if (now > lastSeenMs + 3600000LL)
        {
            lastSeenMs = now;
            save();
        }
    }

    //==========================================================================
    static juce::File licenseFile()
    {
       #if JUCE_MAC
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("Application Support");
       #else
        auto base = juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory);
       #endif
        return base.getChildFile ("Side Key System").getChildFile ("Merlin Alpha").getChildFile ("license.json");
    }

    void load()
    {
        const auto f = licenseFile();
        if (! f.existsAsFile())
            return;
        const auto j = juce::JSON::parse (f);
        storedKey   = j.getProperty ("license_key", "").toString();
        certificate = j.getProperty ("certificate", "").toString();
        expiresAt   = j.getProperty ("expires_at", "").toString();
        lastSeenMs  = (juce::int64) j.getProperty ("last_seen_ms", 0);
        betaEndedByServer = (bool) j.getProperty ("beta_ended", false);
        const auto t = (juce::int64) j.getProperty ("trial_start_ms", 0);
        if (t > 0)
            trialStart = juce::Time (t);
        if ((bool) j.getProperty ("revoked", false))
            status = Status::revoked;
    }

    void save()
    {
        const auto f = licenseFile();
        f.getParentDirectory().createDirectory();
        auto* o = new juce::DynamicObject();
        o->setProperty ("license_key", storedKey);
        o->setProperty ("certificate", certificate);
        if (expiresAt.isNotEmpty())
        {
            o->setProperty ("expires_at", expiresAt);
            o->setProperty ("last_seen_ms", lastSeenMs);
            o->setProperty ("beta_ended", betaEndedByServer);
        }
        o->setProperty ("trial_start_ms", trialStart.toMilliseconds());
        o->setProperty ("revoked", status.load() == Status::revoked);
        f.replaceWithText (juce::JSON::toString (juce::var (o)));
    }

    void recomputeTrial()
    {
        const auto s = status.load();
        if (s == Status::revoked || s == Status::licensed || s == Status::betaExpired)
            return;
        status = getTrialDaysLeft() > 0 ? Status::trial : Status::trialExpired;
    }

    //==========================================================================
    // Signal de lancement : visible sur le DRM. Ne bloque que sur « revoked », ou
    // « expired » pour une clé bêta. Un certificat joint à la réponse (date de
    // bêta changée ou retirée dans /admin) remplace celui du fichier.
    void pingLaunch()
    {
        juce::Thread::launch ([self = juce::WeakReference<LicenseManager> (this), key = storedKey, mid = machineId]
        {
            auto r = postJson ("/v1/validate",
                { { "license_key", key }, { "machine_id", mid },
                  { "plugin_version", merlinVersionLabel() } });

            const auto cert = r.json.getProperty ("certificate", "").toString();
            const auto expires = expiresField (r.json);
            if (! (r.error == "revoked" || r.error == "expired" || (r.ok && cert.isNotEmpty())))
                return;

            juce::MessageManager::callAsync ([self, r, key, cert, expires]
            {
                auto* m = self.get();
                if (m == nullptr || m->storedKey != key)
                    return;

                if (r.error == "revoked")
                {
                    m->status = Status::revoked;
                    m->certificate = {};
                }
                else if (r.error == "expired")
                {
                    if (m->expiresAt.isEmpty())
                        return;   // clé vendue (sans date) : jamais bloquée par « expired »
                    m->betaEndedByServer = true;
                    m->status = Status::betaExpired;
                }
                else if (m->verifyCertificate (key, cert, expires))
                {
                    m->certificate = cert;
                    m->expiresAt = expires;
                    m->betaEndedByServer = false;
                    m->status = m->betaHasEnded() ? Status::betaExpired : Status::licensed;
                }
                else
                {
                    return;
                }
                m->save();
                m->notify();
            });
        });
    }

    void pingTrial()
    {
        juce::Thread::launch ([self = juce::WeakReference<LicenseManager> (this), mid = machineId]
        {
            auto r = postJson ("/v1/trial-ping",
                { { "machine_id", mid }, { "product_id", productId },
                  { "machine_name", juce::SystemStats::getComputerName() },
                  { "plugin_version", merlinVersionLabel() } });

            juce::MessageManager::callAsync ([self, r]
            {
                auto* m = self.get();
                if (m == nullptr)
                    return;

                if (r.error == "revoked")
                {
                    m->status = Status::trialExpired;
                    m->notify();
                    return;
                }
                if (! r.ok)
                    return;   // serveur injoignable : la démo locale continue

                m->bonusDays = juce::jmax (0, (int) r.json.getProperty ("bonus_days", 0));

                // date du premier signal de cette machine, en UTC "AAAA-MM-JJ HH:MM:SS"
                const auto startedUtc = r.json.getProperty ("started_at", "").toString();
                const auto serverStart = juce::Time::fromISO8601 (startedUtc.replaceCharacter (' ', 'T') + "Z");
                if (startedUtc.isNotEmpty() && serverStart.toMilliseconds() > 0 && serverStart < m->trialStart)
                {
                    m->trialStart = serverStart;   // démo déjà commencée sur cette machine
                    m->save();
                }
                m->recomputeTrial();
                m->notify();
            });
        });
    }

    // Temps d'utilisation, par tranches ; vérifie aussi la fin de démo en cours de session
    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const int seconds = (int) ((now - sessionStart) / 1000.0);
        sessionStart = now;

        if (status.load() == Status::trial)
        {
            recomputeTrial();
            notify();
        }
        else if (status.load() == Status::licensed && expiresAt.isNotEmpty())
        {
            noteClock();
            if (betaHasEnded())   // fin de bêta pendant la session
            {
                status = Status::betaExpired;
                notify();
            }
        }

        const auto s = status.load();
        if (seconds <= 0 || ! (s == Status::licensed || s == Status::trial))
            return;

        juce::Thread::launch ([mid = machineId, key = (s == Status::licensed ? storedKey : juce::String()), seconds]
        {
            postJson ("/v1/usage-ping",
                { { "machine_id", mid }, { "product_id", productId },
                  { "license_key", key }, { "seconds", juce::String (seconds) } });
        });
    }

    //==========================================================================
    struct ApiResult { bool ok = false; bool networkError = false; juce::String error; juce::var json; };

    static ApiResult postJson (const juce::String& path, const std::map<juce::String, juce::String>& fields)
    {
        auto* o = new juce::DynamicObject();
        for (auto& f : fields)
            o->setProperty (f.first, f.first == "seconds" ? juce::var (f.second.getIntValue()) : juce::var (f.second));

        auto url = juce::URL (juce::String (serverUrl) + path).withPOSTData (juce::JSON::toString (juce::var (o), true));
        int code = 0;
        auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                                 .withExtraHeaders ("Content-Type: application/json")
                                                 .withConnectionTimeoutMs (8000)
                                                 .withStatusCode (&code));
        if (stream == nullptr)
            return { false, true, "network_error", {} };

        ApiResult r;
        r.json  = juce::JSON::parse (stream->readEntireStreamAsString());
        r.ok    = code >= 200 && code < 300 && (bool) r.json.getProperty ("ok", false);
        r.error = r.json.getProperty ("error", "").toString();
        return r;
    }

    JUCE_DECLARE_WEAK_REFERENCEABLE (LicenseManager)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseManager)
};
