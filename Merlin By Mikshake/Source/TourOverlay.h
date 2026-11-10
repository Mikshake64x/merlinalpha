#pragma once

#include <JuceHeader.h>
#include "Lang.h"
#include "AppMode.h"
#include "OptionsOverlay.h"   // OptionsUi : thème et police des Options

/*
    TourOverlay -- visite guidée d'accueil (v1.81.43)
    -------------------------------------------------
    Demande du user : « comme sur Sevenpack, un tour du logiciel à
    l'ouverture et réactivable via les Options », même style que Sevenpack
    (bulle, sans numéro d'étape), dans la langue de Merlin.

    - Voile gris sur tout l'éditeur, découpé autour des éléments expliqués
      (contour bleu + anneau qui pulse), bulle fléchée à côté.
    - Pour les étapes de la barre du bas et du Needle VU, l'éditeur place
      lui-même les modules concernés dans les emplacements le temps de
      l'étape (onStep) ; la disposition d'origine revient à la fin.
    - Bloque les clics sur Merlin pendant le tour. Échap = passer (va à la
      dernière étape, qui dit comment le relancer).
    - Vu une seule fois par ordinateur et par utilisateur, appli et plugin
      confondus (TourSettings). Options › À propos › Visite guidée le relance.
*/

// Zones à encadrer, en coordonnées de l'éditeur (vides si absentes)
struct TourTargets
{
    juce::Rectangle<float> moduleTitle, moduleSlot, reset, power, pin, keyboard, options;
    juce::Rectangle<float> bottomBar, spectrum, correlator, panoLed, needleHeader;
};

// « Déjà vu » : un petit fichier commun à l'appli et au plugin
namespace TourSettings
{
    inline juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Side Key System").getChildFile ("Merlin Alpha tour.settings");
    }
    inline bool hasBeenSeen()
    {
        juce::PropertiesFile::Options o;
        juce::PropertiesFile pf (file(), o);
        return pf.getBoolValue ("tourSeen", false);
    }
    inline void setSeen (bool seen)
    {
        file().getParentDirectory().createDirectory();
        juce::PropertiesFile::Options o;
        juce::PropertiesFile pf (file(), o);
        pf.setValue ("tourSeen", seen);
        pf.saveIfNeeded();
    }
}

class TourOverlay : public juce::Component, private juce::Timer
{
public:
    enum class Id { welcome, detach, addOther, menu, reset, power, pin, keyboard, bottomBar, needle, end };

    std::function<TourTargets()> getTargets;
    std::function<void (Id)>     onStep;       // l'éditeur prépare l'étape (modules à afficher)
    std::function<void()>        onFinished;   // disposition d'origine, rendu GPU

    TourOverlay()
    {
        setInterceptsMouseClicks (true, true);
        setWantsKeyboardFocus (true);
        for (auto* b : { &skipButton, &backButton, &nextButton })
            addAndMakeVisible (*b);
        skipButton.onClick = [this] { goTo (lastStep()); };
        backButton.onClick = [this] { goTo (step - 1); };
        nextButton.onClick = [this] { if (step >= lastStep()) finish(); else goTo (step + 1); };
        setVisible (false);
    }

    ~TourOverlay() override { stopTimer(); }

    void start()
    {
        setVisible (true);
        toFront (true);
        startTimerHz (30);
        goTo (0);
        grabKeyboardFocus();
    }

    bool isRunning() const noexcept { return isVisible(); }

    // à rappeler après une mise en page de l'éditeur (taille, langue, thème)
    void refreshLayout() { if (isVisible()) layoutStep(); }
    void resized() override { refreshLayout(); }

    void paint (juce::Graphics& g) override
    {
        const auto th = OptionsUi::Theme::current();
        const bool light = ! th.dark;

        // voile gris, découpé autour des éléments expliqués
        juce::Path veil;
        veil.addRectangle (getLocalBounds().toFloat());
        for (const auto& h : holes)
            veil.addRoundedRectangle (h, kHoleCorner);
        veil.setUsingNonZeroWinding (false);
        g.setColour (light ? juce::Colour (0xa3343a42) : juce::Colour (0xa80a0e13));
        g.fillPath (veil);

        // contours + anneau qui pulse (bleu de Sevenpack)
        const auto ring = ringColour();
        const float t = pulsePhase;
        for (const auto& h : holes)
        {
            const float grow = 6.0f * t;
            g.setColour (ring.withAlpha (0.55f * (1.0f - t)));
            g.drawRoundedRectangle (h.expanded (grow), kHoleCorner + grow, 2.0f + 6.0f * t);
            g.setColour (ring);
            g.drawRoundedRectangle (h, kHoleCorner, 2.0f);
        }

        // bulle : ombre (image en cache), feuille, flèche, textes
        g.drawImage (shadowImage, shadowArea, juce::RectanglePlacement::stretchToFit);
        g.setColour (th.bg);
        g.fillRoundedRectangle (card, kCardCorner);
        g.setColour (th.sep);
        g.drawRoundedRectangle (card.reduced (0.5f), kCardCorner, 1.0f);
        if (arrowSide != Side::none)
            drawArrow (g, th.bg, th.sep);

        const auto& st = steps()[(size_t) step];
        g.setColour (th.fg);
        g.setFont (titleFont());
        const juce::String upAlpha (juce::CharPointer_UTF8 ("\xce\x91")), alpha (juce::CharPointer_UTF8 ("\xce\xb1"));
        g.drawText (TR (st.title).toUpperCase().replace (upAlpha, alpha), titleArea, juce::Justification::centredLeft, true);
        for (int i = 0; i < (int) st.body.size() && i < (int) paragraphAreas.size(); ++i)
            makeParagraph (TR (st.body[(size_t) i]), th.fg.withAlpha (0.86f)).draw (g, paragraphAreas[(size_t) i]);
    }

    void mouseDown (const juce::MouseEvent&) override {}   // clics sur le voile : rien (Merlin est bloqué)

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)  { if (step < lastStep()) goTo (lastStep()); else finish(); return true; }
        if (key == juce::KeyPress::rightKey || key == juce::KeyPress::returnKey) { nextButton.triggerClick(); return true; }
        if (key == juce::KeyPress::leftKey && step > 0) { goTo (step - 1); return true; }
        return false;
    }

private:
    enum class Side { none, left, right, top, bottom };

    struct Step
    {
        Id id;
        const char* title;
        std::vector<const char*> body;
    };

    static constexpr float kCardW = 318.0f, kGap = 18.0f, kMargin = 16.0f;
    static constexpr float kHoleCorner = 9.0f, kCardCorner = 12.0f;

    static juce::Colour ringColour() { return OptionsUi::lightTheme() ? juce::Colour (0xff2c95ef) : juce::Colour (0xff4fb6ff); }

    // textes validés sur l'aperçu (français, traduits par TR) ; l'épingle et
    // le clavier n'existent que dans l'appli
    static const std::vector<Step>& steps()
    {
        static const std::vector<Step> s = []
        {
            std::vector<Step> v {
                { Id::welcome, "Bienvenue dans MERLIN α",
                  { "Ce tour présente les gestes utiles : déplacer et changer les modules, remettre à zéro ou arrêter les mesures, garder Merlin devant votre DAW. Il prend une minute." } },
                { Id::detach, "Détacher un module",
                  { "Attrapez le titre d'un module et tirez-le hors de son emplacement : il part dans sa propre fenêtre, sur un autre écran si vous voulez.",
                    "Ramenez-le sur son emplacement ou fermez sa fenêtre pour le remettre." } },
                { Id::addOther, "Un autre module à sa place",
                  { "L'emplacement libéré se partage en deux : une moitié ramène le module, l'autre ouvre la liste pour en mettre un autre, pendant que le premier reste dans sa fenêtre." } },
                { Id::menu, "Changer de module",
                  { "Clic droit sur un module : la liste de tous les modules, rangés par famille.",
                    "L'étoile met un module dans vos favoris, en haut de la liste." } },
                { Id::reset, "RESET",
                  { "Remet à zéro le loudness, les crêtes, les VU et les captures de l'oscilloscope." } },
                { Id::power, "Arrêter les mesures",
                  { "Le bouton marche / arrêt suspend toutes les mesures et libère le processeur et la carte graphique. Un clic les relance." } },
                { Id::pin, "Toujours devant",
                  { "L'épingle garde Merlin au-dessus des autres fenêtres, même pendant que vous travaillez dans votre DAW." } },
                { Id::keyboard, "Clavier",
                  { "Allumée, cette touche donne le clavier à Merlin. Éteinte, il reste à votre DAW : raccourcis et clavier MIDI continuent de marcher quand vous cliquez dans Merlin." } },
                { Id::bottomBar, "La barre du bas",
                  { "VIEW choisit 15, 31 ou 62 bandes pour le Spectrum, le Multiband Correlator et le Panorama LED.",
                    "MID et SIDE choisissent l'analyse ; RANGE et PEAK HOLD règlent l'échelle et le maintien des crêtes du Spectrum." } },
                { Id::needle, "Needle VU",
                  { "Les flèches ▲ ▼ règlent le niveau en dBFS qui met l'aiguille sur 0, de -4 à -30 dBFS. Un clic sur le pas le fait passer de 2 à 4 dB.",
                    "LR choisit la voie : L+R, L ou R." } },
                { Id::end, "Fin de la visite",
                  { "Le tour ne s'ouvrira plus tout seul. Pour le revoir : Options › À propos › Visite guidée." } },
            };
            if (AppMode::isPlugin)
                v.erase (std::remove_if (v.begin(), v.end(), [] (const Step& st) { return st.id == Id::pin || st.id == Id::keyboard; }), v.end());
            return v;
        }();
        return s;
    }

    int lastStep() const { return (int) steps().size() - 1; }

    static juce::Font titleFont() { return OptionsUi::font (15.0f, 1).withExtraKerningFactor (0.1f); }
    static juce::Font bodyFont()  { return OptionsUi::font (13.5f); }

    static juce::AttributedString makeParagraph (const juce::String& text, juce::Colour colour)
    {
        juce::AttributedString as;
        as.setText (text);
        as.setFont (bodyFont());
        as.setColour (colour);
        as.setLineSpacing (2.0f);
        as.setJustification (juce::Justification::topLeft);
        as.setWordWrap (juce::AttributedString::byWord);
        return as;
    }

    static juce::Rectangle<float> pad (juce::Rectangle<float> r, float all) { return r.isEmpty() ? r : r.expanded (all); }

    static juce::Rectangle<float> unionOf (const std::vector<juce::Rectangle<float>>& rs)
    {
        juce::Rectangle<float> u;
        for (const auto& r : rs)
            if (! r.isEmpty()) u = u.isEmpty() ? r : u.getUnion (r);
        return u;
    }

    // sécurité : deux découpes qui se chevauchent sont fusionnées (voile en pair-impair)
    static void mergeOverlaps (std::vector<juce::Rectangle<float>>& rs)
    {
        for (bool merged = true; merged;)
        {
            merged = false;
            for (size_t i = 0; i < rs.size() && ! merged; ++i)
                for (size_t j = i + 1; j < rs.size() && ! merged; ++j)
                    if (rs[i].intersects (rs[j]))
                    {
                        rs[i] = rs[i].getUnion (rs[j]);
                        rs.erase (rs.begin() + (std::ptrdiff_t) j);
                        merged = true;
                    }
        }
    }

    // découpes (holes) et éléments qui placent la bulle (anchor) de l'étape
    void computeTargets (const TourTargets& t, std::vector<juce::Rectangle<float>>& outHoles,
                         std::vector<juce::Rectangle<float>>& outAnchor) const
    {
        outHoles.clear(); outAnchor.clear();
        auto add = [&outHoles] (juce::Rectangle<float> r, float p) { if (! r.isEmpty()) outHoles.push_back (pad (r, p)); };
        switch (steps()[(size_t) step].id)
        {
            case Id::detach:    add (t.moduleTitle, 4.0f); break;
            case Id::addOther:
            case Id::menu:      add (t.moduleSlot, 4.0f); break;
            case Id::reset:     add (t.reset, 5.0f); break;
            case Id::power:     add (t.power, 5.0f); break;
            case Id::pin:       add (t.pin, 5.0f); break;
            case Id::keyboard:  add (t.keyboard, 5.0f); break;
            case Id::bottomBar: add (t.bottomBar, 4.0f);
                                outAnchor = outHoles;   // la bulle se place sur la barre
                                add (t.spectrum, 3.0f); add (t.correlator, 3.0f); add (t.panoLed, 3.0f); break;
            case Id::needle:    add (t.needleHeader, 5.0f); break;
            case Id::end:       add (t.options, 6.0f); break;
            case Id::welcome:   break;   // accueil : sans découpe
        }
        if (outAnchor.empty())
            outAnchor = outHoles;
        mergeOverlaps (outHoles);
    }

    void goTo (int newStep)
    {
        step = juce::jlimit (0, lastStep(), newStep);
        if (onStep)
            onStep (steps()[(size_t) step].id);
        if (step == lastStep())
            TourSettings::setSeen (true);   // vu jusqu'au bout (ou passé) : ne revient plus
        layoutStep();
        grabKeyboardFocus();
    }

    void finish()
    {
        TourSettings::setSeen (true);
        stopTimer();
        setVisible (false);
        if (onFinished)
            onFinished();
    }

    void layoutStep()
    {
        const auto& st = steps()[(size_t) step];
        std::vector<juce::Rectangle<float>> anchorRects;
        holes.clear();
        if (getTargets)
            computeTargets (getTargets(), holes, anchorRects);

        // textes
        const float innerW = kCardW - 36.0f;
        paragraphs.clear();
        float textH = 0.0f;
        for (const auto* p : st.body)
        {
            juce::TextLayout tl;
            tl.createLayout (makeParagraph (TR (p), juce::Colours::black), innerW);
            textH += tl.getHeight() + (paragraphs.empty() ? 0.0f : 7.0f);
            paragraphs.push_back (std::move (tl));
        }
        const float cardH = 18.0f + 22.0f + 8.0f + textH + 14.0f + 30.0f + 14.0f;

        // bulle : à droite, sous, au-dessus ou à gauche de la zone, là où elle tient
        const float W = (float) getWidth(), H = (float) getHeight();
        auto clampX = [&] (float v) { return juce::jlimit (kMargin, juce::jmax (kMargin, W - kCardW - kMargin), v); };
        auto clampY = [&] (float v) { return juce::jlimit (kMargin, juce::jmax (kMargin, H - cardH - kMargin), v); };
        arrowSide = Side::none;
        const auto a = unionOf (anchorRects);
        if (a.isEmpty())
            card = { (W - kCardW) * 0.5f, (H - cardH) * 0.5f, kCardW, cardH };
        else if (H - a.getBottom() >= cardH + kGap + kMargin && a.getHeight() < H * 0.45f)
        {
            card = { clampX (a.getCentreX() - kCardW * 0.5f), a.getBottom() + kGap, kCardW, cardH };
            arrowSide = Side::top; arrowPos = a.getCentreX();
        }
        else if (W - a.getRight() >= kCardW + kGap + kMargin)
        {
            const float focusY = a.getY() + juce::jmin (a.getHeight(), 120.0f) * 0.5f;
            card = { a.getRight() + kGap, clampY (focusY - 40.0f), kCardW, cardH };
            arrowSide = Side::left; arrowPos = focusY;
        }
        else if (a.getX() >= kCardW + kGap + kMargin)
        {
            const float focusY = a.getY() + juce::jmin (a.getHeight(), 120.0f) * 0.5f;
            card = { a.getX() - kGap - kCardW, clampY (focusY - 40.0f), kCardW, cardH };
            arrowSide = Side::right; arrowPos = focusY;
        }
        else if (a.getY() >= cardH + kGap + kMargin)
        {
            card = { clampX (a.getCentreX() - kCardW * 0.5f), a.getY() - cardH - kGap, kCardW, cardH };
            arrowSide = Side::bottom; arrowPos = a.getCentreX();
        }
        else
            card = { (W - kCardW) * 0.5f, (H - cardH) * 0.5f, kCardW, cardH };   // pas de place : au centre
        if (arrowSide == Side::left || arrowSide == Side::right)
            arrowPos = juce::jlimit (card.getY() + 22.0f, card.getBottom() - 22.0f, arrowPos);
        else if (arrowSide != Side::none)
            arrowPos = juce::jlimit (card.getX() + 24.0f, card.getRight() - 24.0f, arrowPos);

        auto in = card.reduced (18.0f, 0.0f).withTrimmedTop (18.0f).withTrimmedBottom (14.0f);
        titleArea = in.removeFromTop (22.0f);
        in.removeFromTop (8.0f);
        paragraphAreas.clear();
        for (size_t i = 0; i < paragraphs.size(); ++i)
        {
            if (i > 0) in.removeFromTop (7.0f);
            paragraphAreas.push_back (in.removeFromTop (paragraphs[i].getHeight()));
        }

        // boutons
        const bool first = step == 0, last = step == lastStep();
        skipButton.setButtonText (TR ("Passer le tour"));
        backButton.setButtonText (TR ("Retour"));
        skipButton.setVisible (! last);
        backButton.setVisible (! first);
        nextButton.setButtonText (first ? TR ("Commencer") : last ? TR ("Terminer") : TR ("Suivant"));
        auto row = card.reduced (18.0f, 0.0f).removeFromBottom (14.0f + 30.0f).withTrimmedBottom (14.0f);
        nextButton.setBounds (row.removeFromRight ((float) nextButton.getIdealWidth()).toNearestInt());
        row.removeFromRight (8.0f);
        if (! first)
            backButton.setBounds (row.removeFromRight ((float) backButton.getIdealWidth()).toNearestInt());
        if (! last)
            skipButton.setBounds (row.removeFromLeft ((float) skipButton.getIdealWidth()).toNearestInt());

        rebuildShadow();
        repaint();
    }

    void drawArrow (juce::Graphics& g, juce::Colour fill, juce::Colour edge) const
    {
        juce::Path tri, sides;
        const float s = 9.0f;
        if (arrowSide == Side::left || arrowSide == Side::right)
        {
            const bool l = arrowSide == Side::left;
            const float x = l ? card.getX() : card.getRight(), d = l ? 1.0f : -1.0f;
            tri.addTriangle (x + 1.5f * d, arrowPos - 11.0f, x - s * d, arrowPos, x + 1.5f * d, arrowPos + 11.0f);
            sides.startNewSubPath (x + 0.5f * d, arrowPos - 10.5f); sides.lineTo (x - s * d, arrowPos); sides.lineTo (x + 0.5f * d, arrowPos + 10.5f);
        }
        else
        {
            const bool t = arrowSide == Side::top;
            const float y = t ? card.getY() : card.getBottom(), d = t ? 1.0f : -1.0f;
            tri.addTriangle (arrowPos - 11.0f, y + 1.5f * d, arrowPos, y - s * d, arrowPos + 11.0f, y + 1.5f * d);
            sides.startNewSubPath (arrowPos - 10.5f, y + 0.5f * d); sides.lineTo (arrowPos, y - s * d); sides.lineTo (arrowPos + 10.5f, y + 0.5f * d);
        }
        g.setColour (fill);
        g.fillPath (tri);
        g.setColour (edge);
        g.strokePath (sides, juce::PathStrokeType (1.0f));
    }

    // ombre de la bulle calculée une fois par étape (pas à chaque image de l'anneau)
    void rebuildShadow()
    {
        shadowArea = card.expanded (34.0f);
        shadowImage = juce::Image (juce::Image::ARGB, juce::jmax (1, (int) shadowArea.getWidth()), juce::jmax (1, (int) shadowArea.getHeight()), true);
        juce::Graphics sg (shadowImage);
        juce::Path p;
        p.addRoundedRectangle (card.translated (-shadowArea.getX(), -shadowArea.getY()), kCardCorner);
        juce::DropShadow (juce::Colours::black.withAlpha (OptionsUi::lightTheme() ? 0.30f : 0.55f), 24, { 0, 8 }).drawForPath (sg, p);
    }

    void timerCallback() override
    {
        pulsePhase = (float) std::fmod (juce::Time::getMillisecondCounterHiRes() / 1800.0, 1.0);
        for (const auto& h : holes)
            repaint (h.expanded (12.0f).getSmallestIntegerContainer());
    }

    // boutons de la bulle : lien discret, contour, ou bleu plein (comme Sevenpack)
    class TourButton : public juce::Button
    {
    public:
        enum class Kind { link, plain, primary };
        TourButton (const juce::String& text, Kind k) : juce::Button (text), kind (k)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setWantsKeyboardFocus (false);
        }

        int getIdealWidth() const
        {
            const float p = kind == Kind::link ? 4.0f : 30.0f;
            return (int) std::ceil (juce::GlyphArrangement::getStringWidth (OptionsUi::font (13.0f, 1), getButtonText()) + p);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto th = OptionsUi::Theme::current();
            const auto b = getLocalBounds().toFloat();
            if (kind == Kind::primary)
            {
                auto bg = ringColour();
                if (down) bg = bg.darker (0.15f); else if (over) bg = bg.brighter (0.08f);
                g.setColour (bg);
                g.fillRoundedRectangle (b, 7.0f);
                g.setColour (th.dark ? juce::Colour (0xff0b1520) : juce::Colours::white);
            }
            else if (kind == Kind::plain)
            {
                g.setColour (th.fg.withAlpha (down ? 0.12f : over ? 0.07f : 0.0f));
                g.fillRoundedRectangle (b, 7.0f);
                g.setColour (th.sep);
                g.drawRoundedRectangle (b.reduced (0.5f), 7.0f, 1.0f);
                g.setColour (th.fg);
            }
            else
                g.setColour (over ? th.fg : th.muted);
            g.setFont (OptionsUi::font (13.0f, kind == Kind::link ? 0 : 1));
            g.drawText (getButtonText(), getLocalBounds(), kind == Kind::link ? juce::Justification::centredLeft
                                                                                : juce::Justification::centred, false);
        }

    private:
        Kind kind;
    };

    TourButton skipButton { "Skip", TourButton::Kind::link };
    TourButton backButton { "Back", TourButton::Kind::plain };
    TourButton nextButton { "Next", TourButton::Kind::primary };

    int step = 0;
    float pulsePhase = 0.0f, arrowPos = 0.0f;
    Side arrowSide = Side::none;
    std::vector<juce::Rectangle<float>> holes;
    juce::Rectangle<float> card, titleArea, shadowArea;
    std::vector<juce::TextLayout> paragraphs;
    std::vector<juce::Rectangle<float>> paragraphAreas;
    juce::Image shadowImage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TourOverlay)
};
