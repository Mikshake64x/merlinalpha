#pragma once

#include <JuceHeader.h>
#include "Lang.h"
#include "OptionsOverlay.h"   // OptionsUi : thème et police des Options
#include "LicenseManager.h"

//==============================================================================
// Écran de licence de Merlin α (aperçu validé : preview/merlin_licence_preview.html).
// Feuille au style des Options posée sur un voile. Tant que Merlin n'est ni
// activé ni en démo, elle ne se ferme pas ; sinon Échap / OK la referme.
// Le logotype (MERLIN + pastille α) est dessiné par l'éditeur via drawBrand.
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
            e->onReturnKey = [this] { activate(); };
        }
        keyEdit.setInputRestrictions (64);

        for (auto* b : { &buyButton, &demoButton, &activateButton, &okButton, &deactivateButton })
        {
            addAndMakeVisible (*b);
            b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        buyButton.onClick      = [] { juce::URL (buyUrl).launchInDefaultBrowser(); };
        demoButton.onClick     = [this] { manager.startDemo(); };
        activateButton.onClick = [this] { activate(); };
        okButton.onClick       = [this] { dismiss(); };
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
            e->setTextToShowWhenEmpty (e == &keyEdit ? juce::String ("XXXX-XXXX-XXXX-XXXX") : TR ("facultatif"), th.muted);
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

        for (auto* e : { &keyEdit, &firstEdit, &lastEdit, &emailEdit })
            e->setVisible (! licensed);
        buyButton.setVisible (! licensed);
        activateButton.setVisible (! licensed);
        demoButton.setVisible (! licensed && manager.canStartDemo());
        okButton.setVisible (licensed || manager.isUsable());

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
        g.drawText (title(), titleArea, juce::Justification::centredLeft, true);
        g.setColour (th.muted);
        g.setFont (OptionsUi::font (12.0f));
        g.drawFittedText (subtitle(), textArea.toNearestInt(), juce::Justification::topLeft, 3, 1.0f);

        // groupe de lignes (champs, ou clé + ordinateur)
        g.setColour (th.group);
        g.fillRoundedRectangle (groupArea, 10.0f);
        const auto labels = licensed ? juce::StringArray { TR ("Clé"), TR ("Ordinateur") }
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

        const auto err = errorText();
        if (err.isNotEmpty())
        {
            g.setColour (juce::Colour (0xffff6961));
            g.setFont (OptionsUi::font (12.0f));
            g.drawText (err, errorArea, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        const auto b = getLocalBounds().toFloat();
        const float w = juce::jmin (460.0f, b.getWidth() - 40.0f);
        const int rows = licensed ? 2 : 4;
        const float h = 22.0f + 30.0f + 26.0f + 40.0f + rows * rowH + 12.0f + 22.0f + 34.0f + 18.0f;
        sheet = b.withSizeKeepingCentre (w, h);

        auto r = sheet.reduced (22.0f, 18.0f);
        brandArea = r.removeFromTop (30.0f);
        r.removeFromTop (4.0f);
        titleArea = r.removeFromTop (22.0f);
        textArea  = r.removeFromTop (40.0f);
        groupArea = r.removeFromTop (rows * rowH);
        r.removeFromTop (4.0f);
        errorArea = r.removeFromTop (22.0f);
        r.removeFromTop (8.0f);
        auto buttons = r.removeFromTop (32.0f);

        if (! licensed)
        {
            juce::TextEditor* eds[] = { &keyEdit, &firstEdit, &lastEdit, &emailEdit };
            for (int i = 0; i < 4; ++i)
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
        place (activateButton, 96.0f);
        place (demoButton, 128.0f);
        buyButton.setBounds (buttons.withWidth (juce::jmin (buttons.getWidth(), 150.0f)).toNearestInt());
        deactivateButton.setBounds (buttons.withWidth (juce::jmin (buttons.getWidth(), 220.0f)).toNearestInt());
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
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

    LicenseManager& manager;
    OptionsUi::LookAndFeel laf;
    juce::TextEditor keyEdit, firstEdit, lastEdit, emailEdit;
    SheetButton buyButton { SheetButton::link }, demoButton { SheetButton::secondary },
                activateButton { SheetButton::primary }, okButton { SheetButton::primary },
                deactivateButton { SheetButton::secondary };
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
            case S::revoked:      return TR ("Licence révoquée");
            default:              return TR ("Activer Merlin α");
        }
    }

    juce::String subtitle() const
    {
        using S = LicenseManager::Status;
        switch (manager.getStatus())
        {
            case S::licensed:     return TR ("Merlin α est activé sur cet ordinateur et fonctionne hors ligne. "
                                             "Pour le passer sur un autre ordinateur, désactivez-le ici.");
            case S::trialExpired: return TR ("Merci d'avoir essayé Merlin α. Entrez votre clé de licence pour continuer à l'utiliser.");
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
