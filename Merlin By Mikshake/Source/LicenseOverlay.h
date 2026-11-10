#pragma once

#include <JuceHeader.h>
#include "Lang.h"
#include "OptionsOverlay.h"   // OptionsUi : thème et police des Options
#include "LicenseManager.h"
#include "DiscordLogo.h"

//==============================================================================
// Écran de licence de Merlin α (aperçu validé : preview/merlin_licence_preview.html).
// Feuille au style des Options posée sur un voile. Tant que Merlin n'est ni
// activé ni en démo, elle ne se ferme pas ; sinon Échap / OK la referme.
// Le logotype (MERLIN + pastille α) est dessiné par l'éditeur via drawBrand.
//
// Programme bêta (v1.81.34, même système que Sevenpack Alpha) : le bouton
// « Rejoindre le programme bêta » ouvre une vue à part, sans champ de clé :
// conditions du programme, prénom / nom / email (obligatoire), Retour |
// Rejoindre la bêta, lien Discord. Voir LicenseManager::joinBeta().
//==============================================================================
class LicenseOverlay : public juce::Component,
                       private juce::ChangeListener
{
public:
    std::function<void (juce::Graphics&, juce::Rectangle<float>)> drawBrand;
    std::function<void()> onDismiss;

    explicit LicenseOverlay (LicenseManager& m) : manager (m)
    {
        setLookAndFeel (&laf);
        setWantsKeyboardFocus (true);

        for (auto* e : { &keyEdit, &firstEdit, &lastEdit, &emailEdit })
        {
            addAndMakeVisible (*e);
            e->setJustification (juce::Justification::centredLeft);
            e->setIndents (0, 0);
            e->setBorder ({});
            e->onReturnKey = [this] { submit(); };
        }
        keyEdit.setInputRestrictions (64);
        emailEdit.onTextChange = [this] { if (betaEmailMissing) { betaEmailMissing = false; repaint(); } };

        for (auto* b : std::initializer_list<juce::Button*> { &buyButton, &demoButton, &activateButton, &okButton,
                                                              &deactivateButton, &betaButton, &joinButton, &backButton,
                                                              &discordButton })
        {
            addAndMakeVisible (*b);
            b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        buyButton.onClick      = [] { juce::URL (buyUrl).launchInDefaultBrowser(); };
        demoButton.onClick     = [this] { manager.clearBetaJoinError(); manager.startDemo(); };
        activateButton.onClick = [this] { activate(); };
        okButton.onClick       = [this] { dismiss(); };
        betaButton.onClick     = [this] { setBetaMode (true); };
        joinButton.onClick     = [this] { joinBeta(); };
        backButton.onClick     = [this] { setBetaMode (false); };
        discordButton.onClick  = [] { juce::URL (discordUrl).launchInDefaultBrowser(); };
        // deux temps : le premier clic demande confirmation
        deactivateButton.onClick = [this]
        {
            if (! confirmDeactivate) { confirmDeactivate = true; refresh(); return; }
            confirmDeactivate = false;
            showError = true;
            manager.deactivate();
        };

        manager.addChangeListener (this);
        refresh();
    }

    ~LicenseOverlay() override
    {
        manager.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    // Ouverte par la pastille DÉMO ou par Options › À propos
    void open()
    {
        refresh();
        setVisible (true);
        toFront (true);
        if (keyEdit.isShowing()) keyEdit.grabKeyboardFocus();
    }

    // Langue ou thème changés
    void refresh()
    {
        laf.refresh();
        if (! canJoinBeta())
            betaMode = false;   // inscription réussie (licence bêta) ou plus proposable
        const auto th = OptionsUi::Theme::current();
        for (auto* e : { &keyEdit, &firstEdit, &lastEdit, &emailEdit })
        {
            e->setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
            e->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            e->setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
            e->setColour (juce::TextEditor::textColourId, th.fg);
            e->setColour (juce::TextEditor::highlightColourId, th.accent.withAlpha (0.35f));
            e->setColour (juce::CaretComponent::caretColourId, th.accent);
            e->setFont (OptionsUi::font (13.0f));
            e->setTextToShowWhenEmpty (e == &keyEdit ? juce::String ("XXXX-XXXX-XXXX-XXXX")
                                       : (betaMode && e == &emailEdit) ? TR ("obligatoire") : TR ("facultatif"), th.muted);
        }
        buyButton.setButtonText (TR ("Acheter une licence"));
        demoButton.setButtonText (TR ("Essayer 15 jours"));
        okButton.setButtonText ("OK");

        const auto s = manager.getStatus();
        licensed = s == LicenseManager::Status::licensed;
        activateButton.setButtonText (s == LicenseManager::Status::activating ? TR ("Activation…") : TR ("Activer"));
        activateButton.setEnabled (s != LicenseManager::Status::activating);

        if (! licensed) confirmDeactivate = false;
        deactivateButton.setButtonText (manager.isDeactivating() ? TR ("Désactivation…")
                                        : confirmDeactivate ? TR ("Confirmer la désactivation")
                                                            : TR ("Désactiver sur cet ordinateur"));
        deactivateButton.setEnabled (! manager.isDeactivating());
        deactivateButton.setVisible (licensed);

        keyEdit.setVisible (! licensed && ! betaMode);
        for (auto* e : { &firstEdit, &lastEdit, &emailEdit })
            e->setVisible (! licensed);
        buyButton.setVisible (! licensed && ! betaMode);
        activateButton.setVisible (! licensed && ! betaMode);
        demoButton.setVisible (! licensed && ! betaMode && manager.canStartDemo() && s != LicenseManager::Status::betaExpired);
        okButton.setVisible (! betaMode && (licensed || manager.isUsable()));

        // programme bêta : proposé tant qu'il n'y a pas de licence (démo comprise)
        betaButton.setButtonText (TR ("Rejoindre le programme bêta"));
        betaButton.setVisible (! betaMode && canJoinBeta());
        joinButton.setButtonText (TR ("Rejoindre la bêta"));
        joinButton.setEnabled (! manager.isJoiningBeta());
        backButton.setButtonText (TR ("Retour"));
        discordButton.setButtonText (TR ("Discord de la bêta"));
        for (auto* b : std::initializer_list<juce::Button*> { &joinButton, &backButton, &discordButton })
            b->setVisible (betaMode);

        resized();
        repaint();
    }

    bool isBlocking() const { return ! manager.isUsable(); }

    void paint (juce::Graphics& g) override
    {
        const auto th = OptionsUi::Theme::current();
        g.fillAll (juce::Colours::black.withAlpha (isBlocking() ? 0.62f : 0.42f));

        juce::Path p; p.addRoundedRectangle (sheet, 14.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 40, { 0, 16 }).drawForPath (g, p);
        g.setColour (th.bg);
        g.fillPath (p);

        if (drawBrand) drawBrand (g, brandArea);

        g.setColour (licensed ? juce::Colour (0xff30d158) : th.fg);
        g.setFont (OptionsUi::font (15.0f, 1));
        g.drawText (betaMode ? TR ("Rejoindre le programme bêta") : title(), titleArea, juce::Justification::centredLeft, true);
        if (betaMode)
        {
            // conditions du programme bêta (mêmes termes que Sevenpack Alpha)
            auto t = textArea;
            const std::pair<juce::String, bool> paras[] = {
                { TR ("Accès gratuit jusqu'à la fin de la bêta, sans clé de licence."), true },
                { TR ("Le programme bêta ne donne pas droit à une licence de la version commerciale. "
                      "Side Key System se réserve le droit de modifier la durée du programme bêta."), false } };
            for (auto& [text, strong] : paras)
            {
                juce::AttributedString as;
                as.setText (text);
                as.setFont (OptionsUi::font (12.0f, strong ? 1 : 0));
                as.setColour (strong ? th.fg : th.muted);
                as.setJustification (juce::Justification::topLeft);
                juce::TextLayout tl;
                tl.createLayout (as, t.getWidth());
                tl.draw (g, t.removeFromTop (tl.getHeight()));
                t.removeFromTop (6.0f);
            }
        }
        else
        {
            g.setColour (th.muted);
            g.setFont (OptionsUi::font (12.0f));
            g.drawFittedText (subtitle(), textArea.toNearestInt(), juce::Justification::topLeft, 3, 1.0f);
        }

        // groupe de lignes (champs, ou clé + ordinateur)
        g.setColour (th.group);
        g.fillRoundedRectangle (groupArea, 10.0f);
        const auto labels = licensed ? juce::StringArray { TR ("Clé"), TR ("Ordinateur") }
                          : betaMode ? juce::StringArray { TR ("Prénom"), TR ("Nom de famille"), TR ("Email") }
                                     : juce::StringArray { TR ("Clé de licence"), TR ("Prénom"), TR ("Nom de famille"), TR ("Email") };
        for (int i = 0; i < labels.size(); ++i)
        {
            const auto r = rowArea (i);
            if (i > 0)
            {
                g.setColour (th.sep);
                g.fillRect (r.withHeight (1.0f).withTrimmedLeft (12.0f));
            }
            g.setColour (th.fg);
            g.setFont (OptionsUi::font (13.0f));
            g.drawText (labels[i], r.withTrimmedLeft (12.0f).withWidth (labelW), juce::Justification::centredLeft, true);
        }
        if (licensed)
        {
            g.setColour (th.muted);
            g.drawText (maskedKey(), rowArea (0).withTrimmedLeft (12.0f + labelW), juce::Justification::centredLeft, true);
            g.drawText (juce::SystemStats::getComputerName(), rowArea (1).withTrimmedLeft (12.0f + labelW),
                        juce::Justification::centredLeft, true);
        }

        if (betaMode)
        {
            // inscription en cours (gris) ou erreur (rouge)
            const auto [msg, isError] = betaMessage();
            if (msg.isNotEmpty())
            {
                g.setColour (isError ? juce::Colour (0xffff6961) : th.muted);
                g.setFont (OptionsUi::font (12.0f));
                g.drawFittedText (msg, errorArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
            }
        }
        else
        {
            const auto err = errorText();
            if (err.isNotEmpty())
            {
                g.setColour (juce::Colour (0xffff6961));
                g.setFont (OptionsUi::font (12.0f));
                g.drawText (err, errorArea, juce::Justification::centredLeft, true);
            }
        }
    }

    void resized() override
    {
        const auto b = getLocalBounds().toFloat();
        const float w = juce::jmin (460.0f, b.getWidth() - 40.0f);
        const int rows = licensed ? 2 : betaMode ? 3 : 4;
        const float textH = betaMode ? 84.0f : 40.0f;                       // conditions du programme bêta
        const bool betaRow = ! betaMode && betaButton.isVisible();           // ligne « Rejoindre le programme bêta »
        const float h = 22.0f + 30.0f + 26.0f + textH + rows * rowH + 12.0f + 22.0f + 34.0f + 18.0f + (betaRow ? 38.0f : 0.0f);
        sheet = b.withSizeKeepingCentre (w, h);

        auto r = sheet.reduced (22.0f, 18.0f);
        brandArea = r.removeFromTop (30.0f);
        r.removeFromTop (4.0f);
        titleArea = r.removeFromTop (22.0f);
        textArea  = r.removeFromTop (textH);
        groupArea = r.removeFromTop (rows * rowH);
        r.removeFromTop (4.0f);
        errorArea = r.removeFromTop (22.0f);
        r.removeFromTop (8.0f);
        auto buttons = r.removeFromTop (32.0f);
        if (betaRow)
        {
            r.removeFromTop (8.0f);
            betaButton.setBounds (r.removeFromTop (30.0f).toNearestInt());
        }

        if (! licensed)
        {
            juce::TextEditor* all[] = { &keyEdit, &firstEdit, &lastEdit, &emailEdit };
            juce::TextEditor* const* eds = betaMode ? all + 1 : all;   // vue bêta : pas de champ de clé
            for (int i = 0; i < rows; ++i)
                eds[i]->setBounds (rowArea (i).withTrimmedLeft (12.0f + labelW).withTrimmedRight (10.0f)
                                              .withSizeKeepingCentre (rowArea (i).getWidth() - 22.0f - labelW, 22.0f).toNearestInt());
        }

        auto place = [&buttons] (juce::Button& btn, float width)
        {
            if (! btn.isVisible()) return;
            btn.setBounds (buttons.removeFromRight (width).toNearestInt());
            buttons.removeFromRight (8.0f);
        };
        place (okButton, 70.0f);
        place (joinButton, 150.0f);
        place (backButton, 84.0f);
        place (activateButton, 96.0f);
        place (demoButton, 128.0f);
        buyButton.setBounds (buttons.withWidth (juce::jmin (buttons.getWidth(), 150.0f)).toNearestInt());
        deactivateButton.setBounds (buttons.withWidth (juce::jmin (buttons.getWidth(), 220.0f)).toNearestInt());
        discordButton.setBounds (buttons.withWidth (juce::jmin (buttons.getWidth(), discordButton.idealWidth())).toNearestInt());
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey && betaMode) { setBetaMode (false); return true; }
        if (k == juce::KeyPress::escapeKey && ! isBlocking()) { dismiss(); return true; }
        return false;
    }

    void mouseDown (const juce::MouseEvent&) override {}   // le voile capte les clics

private:
    static constexpr const char* buyUrl = "https://sidekeysystem.com/";
    static constexpr float rowH = 38.0f, labelW = 110.0f;

    // Boutons de la feuille : principal (bleu), secondaire (gris), lien
    struct SheetButton : public juce::Button
    {
        enum Kind { primary, secondary, link };
        SheetButton (Kind k) : juce::Button ({}), kind (k) {}
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto th = OptionsUi::Theme::current();
            const auto r = getLocalBounds().toFloat();
            const float a = isEnabled() ? 1.0f : 0.5f;
            if (kind != link)
            {
                auto c = kind == primary ? th.accent : th.field;
                if (down) c = c.contrasting (0.12f); else if (over) c = c.contrasting (0.06f);
                g.setColour (c.withMultipliedAlpha (a));
                g.fillRoundedRectangle (r, 8.0f);
            }
            g.setColour ((kind == primary ? juce::Colours::white : kind == link ? th.accent : th.fg).withMultipliedAlpha (a));
            g.setFont (OptionsUi::font (12.5f, kind == primary ? 1 : 0));
            g.drawText (getButtonText(), r, kind == link ? juce::Justification::centredLeft : juce::Justification::centred, true);
        }
        Kind kind;
    };

    static constexpr const char* discordUrl = "https://discord.gg/pHHWfqdEE";   // même serveur que Sevenpack Alpha

    // Lien Discord du programme bêta : logo officiel + texte en couleur de lien
    struct DiscordButton : public juce::Button
    {
        DiscordButton() : juce::Button ("Discord")
        {
            setTooltip (discordUrl);
            logo = juce::ImageCache::getFromMemory (DiscordLogo::png, DiscordLogo::pngSize);
        }
        float idealWidth() const
        {
            return 22.0f + 8.0f + juce::GlyphArrangement::getStringWidth (OptionsUi::font (12.5f), getButtonText()) + 4.0f;
        }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto th = OptionsUi::Theme::current();
            auto r = getLocalBounds().toFloat();
            g.setOpacity (down ? 0.8f : 1.0f);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);   // 256 px -> 22 px, net
            g.drawImage (logo, r.removeFromLeft (22.0f).withSizeKeepingCentre (22.0f, 22.0f), juce::RectanglePlacement::centred);
            g.setOpacity (1.0f);
            r.removeFromLeft (8.0f);
            g.setColour (over ? juce::Colour (0xff5865f2) : th.accent);
            g.setFont (OptionsUi::font (12.5f));
            g.drawFittedText (getButtonText(), r.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        }
        juce::Image logo;
    };

    LicenseManager& manager;
    OptionsUi::LookAndFeel laf;
    juce::TextEditor keyEdit, firstEdit, lastEdit, emailEdit;
    SheetButton buyButton { SheetButton::link }, demoButton { SheetButton::secondary },
                activateButton { SheetButton::primary }, okButton { SheetButton::primary },
                deactivateButton { SheetButton::secondary }, betaButton { SheetButton::secondary },
                joinButton { SheetButton::primary }, backButton { SheetButton::secondary };
    DiscordButton discordButton;
    bool betaMode = false;           // vue « Rejoindre le programme bêta »
    bool betaEmailMissing = false;   // « Rejoindre la bêta » sans email valide
    bool confirmDeactivate = false;
    juce::Rectangle<float> sheet, brandArea, titleArea, textArea, groupArea, errorArea;
    bool licensed = false;
    bool showError = false;   // erreur affichée seulement après une tentative

    juce::Rectangle<float> rowArea (int i) const { return groupArea.withHeight (rowH).translated (0.0f, i * rowH); }

    void activate()
    {
        if (keyEdit.getText().trim().isEmpty()) { keyEdit.grabKeyboardFocus(); return; }
        showError = true;
        manager.activate (keyEdit.getText(), firstEdit.getText(), lastEdit.getText(), emailEdit.getText());
    }

    void submit() { if (betaMode) joinBeta(); else activate(); }

    // Programme bêta : proposé tant qu'il n'y a pas de licence (démo comprise)
    bool canJoinBeta() const
    {
        using S = LicenseManager::Status;
        const auto s = manager.getStatus();
        return s == S::unlicensed || s == S::invalid || s == S::trial || s == S::trialExpired;
    }

    void setBetaMode (bool on)
    {
        betaMode = on;
        betaEmailMissing = false;
        manager.clearBetaJoinError();
        refresh();
        repaint();
        if (betaMode && firstEdit.isShowing())       firstEdit.grabKeyboardFocus();
        else if (! betaMode && keyEdit.isShowing())  keyEdit.grabKeyboardFocus();
    }

    void joinBeta()
    {
        const auto email = emailEdit.getText().trim();
        if (email.isEmpty() || ! email.containsChar ('@'))
        {
            betaEmailMissing = true;
            emailEdit.grabKeyboardFocus();
            repaint();
            return;
        }
        betaEmailMissing = false;
        manager.joinBeta (firstEdit.getText(), lastEdit.getText(), email);
    }

    // Ligne sous les champs de la vue bêta : { texte, erreur ? }
    std::pair<juce::String, bool> betaMessage() const
    {
        if (manager.isJoiningBeta()) return { TR ("Inscription au programme bêta…"), false };
        if (betaEmailMissing)        return { TR ("Entrez votre email pour rejoindre la bêta."), true };
        const auto e = manager.getBetaJoinError();
        if (e.isEmpty())                          return {};
        if (e == "beta_closed" || e == "expired") return { TR ("Le programme bêta est terminé."), true };
        if (e == "revoked")                       return { TR ("Votre accès à la bêta a été révoqué."), true };
        if (e == "no_beta_program")               return { TR ("Aucun programme bêta n'est ouvert pour Merlin α."), true };
        if (e == "network_error")                 return { TR ("Impossible de joindre le serveur. Vérifiez votre connexion internet."), true };
        return { TR ("Inscription impossible pour le moment. Réessayez plus tard."), true };
    }

    void dismiss()
    {
        if (isBlocking()) return;
        setVisible (false);
        if (onDismiss) onDismiss();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        const bool wasBlocking = isVisible() && ! licensed;
        refresh();
        // activation réussie ou démo lancée depuis la feuille : elle se referme
        // d'elle-même si on venait d'un écran bloquant
        if (wasBlocking && manager.getStatus() == LicenseManager::Status::trial)
            dismiss();
    }

    juce::String title() const
    {
        using S = LicenseManager::Status;
        switch (manager.getStatus())
        {
            case S::licensed:     return TR ("Licence activée");
            case S::trialExpired: return TR ("Votre démo de 15 jours est terminée");
            case S::betaExpired:  return TR ("La bêta de Merlin α est terminée");
            case S::revoked:      return TR ("Licence révoquée");
            default:              return TR ("Activer Merlin α");
        }
    }

    // fin de bêta en heure locale, chiffres seuls (lisible dans toutes les langues)
    juce::String betaEndText() const { return manager.getBetaEnd().formatted ("%Y-%m-%d %H:%M"); }

    juce::String subtitle() const
    {
        using S = LicenseManager::Status;
        switch (manager.getStatus())
        {
            case S::licensed:     return manager.isBetaKey()
                                       ? TR ("Clé bêta : Merlin α fonctionne sur cet ordinateur jusqu'au {0}, même hors ligne.")
                                             .replace ("{0}", betaEndText())
                                       : TR ("Merlin α est activé sur cet ordinateur et fonctionne hors ligne. "
                                             "Pour le passer sur un autre ordinateur, désactivez-le ici.");
            case S::trialExpired: return TR ("Merci d'avoir essayé Merlin α. Entrez votre clé de licence pour continuer à l'utiliser.");
            case S::betaExpired:  return TR ("Merci d'avoir testé Merlin α. Entrez une clé de licence pour continuer à l'utiliser.");
            case S::revoked:      return TR ("Cette licence a été révoquée. Contactez Side Key System ou entrez une autre clé.");
            case S::trial:        return TR ("Démo : {0} jours restants. Entrez votre clé de licence pour activer Merlin α.")
                                        .replace ("{0}", juce::String (manager.getTrialDaysLeft()));
            default:              return TR ("Entrez votre clé de licence. Merlin se connecte une seule fois pour l'activer, puis fonctionne hors ligne.");
        }
    }

    juce::String errorText() const
    {
        const auto st = manager.getStatus();
        if (! showError || ! (st == LicenseManager::Status::invalid || st == LicenseManager::Status::licensed))
            return {};
        const auto e = manager.getLastError();
        if (e.isEmpty())
            return {};
        if (st == LicenseManager::Status::licensed && e == "network_error")   // désactivation hors ligne
            return TR ("Connexion internet nécessaire pour désactiver Merlin α.");
        if (e == "invalid_license" || e == "wrong_product") return TR ("Cette clé n'est pas valide pour Merlin α.");
        if (e == "revoked")                                return TR ("Cette licence a été révoquée.");
        if (e == "expired")                                return TR ("Cette clé bêta a expiré.");
        if (e == "machine_limit_reached")                  return TR ("Cette licence est déjà activée sur {0} ordinateurs. Désactivez-la sur l'un d'eux.")
                                                                      .replace ("{0}", juce::String (manager.getMaxMachines()));
        if (e == "network_error")                          return TR ("Impossible de joindre le serveur. Vérifiez votre connexion internet.");
        return TR ("Activation impossible pour le moment. Réessayez plus tard.");
    }

    juce::String maskedKey() const
    {
        const auto k = manager.getLicenseKey();
        return k.length() > 9 ? k.substring (0, 4) + juce::String (juce::CharPointer_UTF8 ("-\xe2\x80\xa6-")) + k.getLastCharacters (4) : k;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseOverlay)
};
