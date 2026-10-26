#pragma once
#include <JuceHeader.h>
#include "Lang.h"
#include "AppMode.h"

/*
    OptionsOverlay — menu Options de Merlin Alpha
    ----------------------------------------------------
    Repris de Sevenpack Alpha (OptionsOverlay.h, style "Réglages" d'Apple que
    le user a validé) : mêmes contrôles (OptionsUi : sélecteur segmenté,
    interrupteur, curseur, boutons, barre latérale de catégories, pages en
    cartes), catégories propres à Merlin : Audio, Image, Affichage,
    Apparence, Presets, À propos.
*/
namespace OptionsUi
{
    //==============================================================================
    // Couleurs "Reglages" Apple (clair / sombre), reprises de l'apercu.
    // Teinte de Merlin (interrupteur lune) : relue a chaque ouverture
    inline bool& lightTheme() { static bool light = false; return light; }

    struct Theme
    {
        bool dark = false;
        juce::Colour bg, side, group, fg, muted, sep, edge, field, hover, accent, segOn, segOnFg, popupBg;

        static Theme current()
        {
            Theme t;
            t.dark = ! lightTheme();
            t.accent = juce::Colour (0xff0a84ff);
            if (t.dark)
            {
                t.bg      = juce::Colour (0xff1e1e20);
                t.side    = juce::Colour (0xff262628);
                t.group   = juce::Colour (0xff2c2c2e);
                t.fg      = juce::Colour (0xfff5f5f7);
                t.muted   = juce::Colour (0xff98989d);
                t.sep     = juce::Colours::white.withAlpha (0.09f);
                t.edge    = juce::Colours::white.withAlpha (0.12f);
                t.field   = juce::Colour (0xff3a3a3c);
                t.hover   = juce::Colours::white.withAlpha (0.06f);
                t.segOn   = juce::Colour (0xff636366);
                t.segOnFg = juce::Colours::white;
                t.popupBg = juce::Colour (0xff2c2c2e);
            }
            else
            {
                t.bg      = juce::Colour (0xfff5f5f7);
                t.side    = juce::Colour (0xffececee);
                t.group   = juce::Colours::white;
                t.fg      = juce::Colour (0xff1d1d1f);
                t.muted   = juce::Colour (0xff86868b);
                t.sep     = juce::Colours::black.withAlpha (0.09f);
                t.edge    = juce::Colours::black.withAlpha (0.20f);
                t.field   = juce::Colour (0xffe3e3e8);
                t.hover   = juce::Colours::black.withAlpha (0.05f);
                t.segOn   = juce::Colours::white;
                t.segOnFg = juce::Colour (0xff1d1d1f);
                t.popupBg = juce::Colours::white;
            }
            return t;
        }
    };

    // weight : 0 regular, 1 semibold, 2 bold. Segoe UI sous Windows (police
    // systeme, la plus proche de SF), police sans-serif par defaut ailleurs.
    inline juce::Font font (float pointHeight, int weight = 0)
    {
        static const char* const styles[] = { "Regular", "Semibold", "Bold" };
       #if JUCE_WINDOWS
        const juce::String name ("Segoe UI");
       #else
        const juce::String name (juce::Font::getDefaultSansSerifFontName());
       #endif
        return juce::Font (Lang::withScriptFallback (juce::FontOptions().withName (name)
                                                                       .withStyle (styles[juce::jlimit (0, 2, weight)])
                                                                       .withPointHeight (pointHeight)));
    }

    //==============================================================================
    enum class Icon
    {
        gear, brush, gauge, split, spark, key, window, theme, cpu, finish, palette, fader,
        spring, back, glass, needle, gonio, phase, out, target, comp, timer, stack, scope,
        music, fidelity, expand, amount, info, close, check, spectrum, mic, text, grid, folder, layers, globe, count
    };

    // Pictogrammes de l'apercu (traits simples, viewBox 24), construits une
    // fois et partages par tous les composants (SharedResourcePointer).
    struct IconSet
    {
        struct Glyph { juce::Path stroke, fill; };

        IconSet()
        {
            for (int i = 0; i < (int) Icon::count; ++i)
                glyphs[(size_t) i] = make ((Icon) i);
        }

        const Glyph& operator[] (Icon icon) const   { return glyphs[(size_t) icon]; }

    private:
        static Glyph make (Icon icon)
        {
            Glyph g;
            const auto d = [&g] (const char* svg) { g.stroke.addPath (juce::Drawable::parseSVGPath (svg)); };
            const auto circle = [&g] (float cx, float cy, float r) { g.stroke.addEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f); };
            const auto rect = [&g] (float x, float y, float w, float h, float rx)
            {
                if (rx > 0.0f) g.stroke.addRoundedRectangle (x, y, w, h, rx);
                else           g.stroke.addRectangle (x, y, w, h);
            };

            switch (icon)
            {
                case Icon::gear:     circle (12, 12, 3.2f); circle (12, 12, 6.6f);
                                     d ("M12 2.8v2.4M12 18.8v2.4M2.8 12h2.4M18.8 12h2.4M5.5 5.5l1.7 1.7M16.8 16.8l1.7 1.7M5.5 18.5l1.7-1.7M16.8 7.2l1.7-1.7"); break;
                case Icon::brush:    d ("M14.5 4.5l5 5-8 8-5-5z"); d ("M6.5 12.5c-2 .5-3 2-3 4 0 1.5-.8 2.5-1.5 3 3 .5 6-.5 7-3"); break;
                case Icon::gauge:    d ("M4 16a8 8 0 1 1 16 0"); d ("M12 16l4-5"); circle (12, 16, 1.4f); break;
                case Icon::split:    d ("M4 12h6l4-5h6M14 17h6M10 12l4 5"); d ("M17 4l3 3-3 3M17 14l3 3-3 3"); break;
                case Icon::spark:    d ("M12 3l1.8 5.2L19 10l-5.2 1.8L12 17l-1.8-5.2L5 10l5.2-1.8z");
                                     d ("M19 15l.8 2.2L22 18l-2.2.8L19 21l-.8-2.2L16 18l2.2-.8z"); break;
                case Icon::key:      circle (8, 12, 3.5f); d ("M11.5 12H21M17 12v3M20 12v2.5"); break;
                case Icon::window:   rect (3, 5, 18, 14, 2); d ("M3 9h18M14 13l3 3M17 13v3h-3"); break;
                case Icon::theme:    circle (12, 12, 8); g.fill = juce::Drawable::parseSVGPath ("M12 4a8 8 0 0 0 0 16z"); break;
                case Icon::cpu:      rect (6.5f, 6.5f, 11, 11, 1.5f);
                                     d ("M9.5 3v3.5M14.5 3v3.5M9.5 17.5V21M14.5 17.5V21M3 9.5h3.5M3 14.5h3.5M17.5 9.5H21M17.5 14.5H21"); break;
                case Icon::finish:   d ("M12 3s6 6.5 6 11a6 6 0 0 1-12 0c0-4.5 6-11 6-11z"); d ("M9 14.5a3 3 0 0 0 3 3"); break;
                case Icon::palette:  d ("M12 3a9 9 0 1 0 0 18c1 0 1.6-.8 1.6-1.6 0-1-.8-1.5-.8-2.4 0-.9.8-1.6 1.7-1.6H17a4 4 0 0 0 4-4C21 6.8 17 3 12 3z");
                                     circle (7.5f, 11, 1); circle (10, 7, 1); circle (14.5f, 7, 1); break;
                case Icon::fader:    d ("M8 3v18M16 3v18"); rect (5, 7, 6, 4, 1); rect (13, 13, 6, 4, 1); break;
                case Icon::spring:   d ("M6 4h12M6 20h12M8 6.5l8 2.5-8 2.5 8 2.5-8 2.5"); break;
                case Icon::back:     d ("M9 7l-5 5 5 5"); d ("M4 12h9a6 6 0 0 1 6 6"); break;
                case Icon::glass:    rect (3, 6, 18, 12, 3); d ("M6 10.5c3-2 9-2 12 0"); break;
                case Icon::needle:   d ("M4 17a8 8 0 0 1 16 0"); d ("M12 17l5-8"); break;
                case Icon::gonio:    d ("M3 18a9 9 0 0 1 18 0z"); d ("M12 18l-6.3-6.3M12 18l6.3-6.3M12 18V9"); break;
                case Icon::phase:    d ("M2 12c2.5-6 5-6 7.5 0s5 6 7.5 0 3.5-4 5-3"); break;
                case Icon::out:      d ("M4 9h4l5-4v14l-5-4H4z"); d ("M16 9a4 4 0 0 1 0 6M18.5 6.5a7.5 7.5 0 0 1 0 11"); break;
                case Icon::target:   circle (12, 12, 8.5f); circle (12, 12, 4.5f); circle (12, 12, 1); break;
                case Icon::comp:     d ("M3 17l6-6 4 4 8-8"); d ("M15 7h6v6"); break;
                case Icon::timer:    circle (12, 13, 7.5f); d ("M12 9v4l2.5 2M9.5 3h5"); break;
                case Icon::stack:    d ("M4 7h16M7 12h10M10 17h4"); break;
                case Icon::scope:    rect (4, 4, 16, 16, 3); d ("M9 12l2 2 4-4"); break;
                case Icon::music:    d ("M9 18V6l11-2v12"); circle (6.5f, 18, 2.5f); circle (17.5f, 16, 2.5f); break;
                case Icon::fidelity: d ("M12 3l7 3v6c0 4.5-3 7.5-7 9-4-1.5-7-4.5-7-9V6z"); d ("M9 12l2 2 4-4"); break;
                case Icon::expand:   d ("M4 12h16M8 8l-4 4 4 4M16 8l4 4-4 4"); break;
                case Icon::amount:   d ("M4 18h16"); rect (6, 11, 3, 7, 0); rect (11, 7, 3, 11, 0); rect (16, 4, 3, 14, 0); break;
                case Icon::info:     circle (12, 12, 8.5f); d ("M12 11v5.5M12 7.6v.2"); break;
                case Icon::close:    d ("M6 6l12 12M18 6L6 18"); break;
                case Icon::check:    d ("M5 12.5l4.5 4.5L19 7.5"); break;
                case Icon::spectrum: d ("M3 19h18"); d ("M3 16l3-6 3 3 3-8 3 6 3-3 3 4"); break;
                case Icon::mic:      d ("M12 3a3 3 0 0 1 3 3v6a3 3 0 0 1-6 0V6a3 3 0 0 1 3-3z"); d ("M5.5 11a6.5 6.5 0 0 0 13 0M12 17.5V21"); break;
                case Icon::text:     d ("M5 6V4.5h14V6M12 4.5V19.5M9 19.5h6"); break;
                case Icon::grid:     rect (3.5f, 3.5f, 7.5f, 7.5f, 1.5f); rect (13, 3.5f, 7.5f, 7.5f, 1.5f); rect (3.5f, 13, 7.5f, 7.5f, 1.5f); rect (13, 13, 7.5f, 7.5f, 1.5f); break;
                case Icon::folder:   d ("M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"); break;
                case Icon::layers:   d ("M12 4l9 5-9 5-9-5z"); d ("M3 14l9 5 9-5"); break;
                case Icon::globe:    circle (12, 12, 8.5f);
                                     d ("M3.5 12h17M12 3.5c2.4 2.6 3.6 5.4 3.6 8.5s-1.2 5.9-3.6 8.5M12 3.5c-2.4 2.6-3.6 5.4-3.6 8.5s1.2 5.9 3.6 8.5"); break;
                case Icon::count:    break;
            }
            return g;
        }

        std::array<Glyph, (size_t) Icon::count> glyphs;
    };

    // Pictogramme centre dans `area` ; strokeUnits = epaisseur du trait dans
    // la grille 24 (1.9 comme l'apercu).
    inline void drawGlyph (juce::Graphics& g, const IconSet& set, Icon icon, juce::Rectangle<float> area,
                           juce::Colour colour, float strokeUnits = 1.9f)
    {
        const auto& glyph = set[icon];
        const float s = juce::jmin (area.getWidth(), area.getHeight()) / 24.0f;
        const auto t = juce::AffineTransform::scale (s).translated (area.getCentreX() - 12.0f * s,
                                                                   area.getCentreY() - 12.0f * s);
        g.setColour (colour);
        if (! glyph.fill.isEmpty())
            g.fillPath (glyph.fill, t);
        g.strokePath (glyph.stroke, juce::PathStrokeType (strokeUnits * s, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded), t);
    }

    // Pastille arrondie de couleur + pictogramme blanc (Reglages macOS).
    inline void drawBadge (juce::Graphics& g, const IconSet& set, Icon icon, juce::Rectangle<float> area, juce::Colour colour)
    {
        g.setColour (colour);
        g.fillRoundedRectangle (area, area.getWidth() * 0.25f);
        drawGlyph (g, set, icon, area.withSizeKeepingCentre (area.getWidth() * 0.62f, area.getHeight() * 0.62f),
                   juce::Colours::white);
    }

    //==============================================================================
    // LookAndFeel des menus deroulants, curseurs et ascenseur de la page.
    // Les autres controles (segments, interrupteurs, boutons) se dessinent
    // eux-memes.
    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel() { refresh(); }

        // Relit le theme -- appele a chaque ouverture / changement de theme.
        void refresh()
        {
            const auto th = Theme::current();
            // Fond de ComboBox TRANSPARENT : sinon JUCE rend le composant
            // opaque et les coins arrondis dessines ici laisseraient des
            // residus.
            setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
            setColour (juce::ComboBox::textColourId, th.fg);
            setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
            setColour (juce::ComboBox::arrowColourId, th.muted);
            setColour (juce::PopupMenu::backgroundColourId, th.popupBg);
            setColour (juce::PopupMenu::textColourId, th.fg);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, th.accent);
            setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
            setColour (juce::Label::textColourId, th.fg);
        }

        void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                           int, int, int, int, juce::ComboBox& box) override
        {
            const auto th = Theme::current();
            auto bg = th.field;
            if (isButtonDown)               bg = bg.contrasting (0.08f);
            else if (box.isMouseOver (true)) bg = bg.contrasting (0.04f);
            g.setColour (bg);
            g.fillRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height), 7.0f);

            // Double chevron des menus pop-up macOS, cale a droite.
            const float cx = (float) width - 15.0f, cy = (float) height * 0.5f;
            juce::Path p;
            p.startNewSubPath (cx - 3.5f, cy - 2.0f); p.lineTo (cx, cy - 5.5f); p.lineTo (cx + 3.5f, cy - 2.0f);
            p.startNewSubPath (cx - 3.5f, cy + 2.0f); p.lineTo (cx, cy + 5.5f); p.lineTo (cx + 3.5f, cy + 2.0f);
            g.setColour (th.muted);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        juce::Font getComboBoxFont (juce::ComboBox&) override   { return font (13.0f); }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (4, 0, box.getWidth() - 30, box.getHeight());
            label.setFont (getComboBoxFont (box));
        }

        juce::Font getPopupMenuFont() override   { return font (13.0f); }
        int getPopupMenuBorderSize() override    { return 5; }

        void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
        {
            const auto th = Theme::current();
            g.fillAll (th.popupBg);
            g.setColour (th.edge);
            g.drawRect (0, 0, width, height, 1);
        }

        void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int,
                                        int& idealWidth, int& idealHeight) override
        {
            if (isSeparator) { idealWidth = 50; idealHeight = 9; return; }
            idealHeight = 26;
            idealWidth = (int) juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), text) + 52;
        }

        void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool,
                                const juce::String& text, const juce::String&, const juce::Drawable*,
                                const juce::Colour*) override
        {
            const auto th = Theme::current();
            if (isSeparator)
            {
                g.setColour (th.sep);
                g.fillRect (area.reduced (10, 0).withSizeKeepingCentre (area.getWidth() - 20, 1));
                return;
            }

            const auto r = area.toFloat().reduced (5.0f, 1.0f);
            const bool hot = isHighlighted && isActive;
            if (hot)
            {
                g.setColour (th.accent);
                g.fillRoundedRectangle (r, 5.0f);
            }
            const auto ink = hot ? juce::Colours::white : th.fg.withAlpha (isActive ? 1.0f : 0.4f);
            if (isTicked)
                drawGlyph (g, *icons, Icon::check, r.withWidth (26.0f).withSizeKeepingCentre (13.0f, 13.0f), ink, 2.6f);
            g.setColour (ink);
            g.setFont (getPopupMenuFont());
            g.drawText (text, r.withTrimmedLeft (26.0f).withTrimmedRight (8.0f), juce::Justification::centredLeft, true);
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                               float, float, juce::Slider::SliderStyle, juce::Slider&) override
        {
            const auto th = Theme::current();
            const float cy = (float) y + (float) height * 0.5f;
            const auto track = juce::Rectangle<float> ((float) x, cy - 2.0f, (float) width, 4.0f);
            g.setColour (th.field);
            g.fillRoundedRectangle (track, 2.0f);
            g.setColour (th.accent);
            g.fillRoundedRectangle (track.withRight (sliderPos), 2.0f);

            juce::Path thumb;
            thumb.addEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ sliderPos, cy }));
            juce::DropShadow (juce::Colours::black.withAlpha (0.28f), 4, { 0, 1 }).drawForPath (g, thumb);
            g.setColour (juce::Colours::white);
            g.fillPath (thumb);
            g.setColour (juce::Colours::black.withAlpha (0.08f));
            g.strokePath (thumb, juce::PathStrokeType (0.5f));
        }

        int getSliderThumbRadius (juce::Slider&) override   { return 10; }

        void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                            bool isVertical, int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown) override
        {
            const auto th = Theme::current();
            auto thumb = isVertical ? juce::Rectangle<float> ((float) x, (float) thumbStart, (float) width, (float) thumbSize)
                                    : juce::Rectangle<float> ((float) thumbStart, (float) y, (float) thumbSize, (float) height);
            thumb = thumb.reduced (isVertical ? 1.5f : 2.0f, isVertical ? 2.0f : 1.5f);
            g.setColour (th.fg.withAlpha (isMouseOver || isMouseDown ? 0.35f : 0.2f));
            g.fillRoundedRectangle (thumb, juce::jmin (thumb.getWidth(), thumb.getHeight()) * 0.5f);
        }

        int getDefaultScrollbarWidth() override   { return 8; }

    private:
        juce::SharedResourcePointer<IconSet> icons;
    };

    //==============================================================================
    // Selecteur segmente (NSSegmentedControl) : piste grise, pastille blanche
    // sous le choix courant (gris clair en sombre).
    class Segmented : public juce::Component
    {
    public:
        explicit Segmented (juce::StringArray labelsIn)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setLabels (std::move (labelsIn));
        }

        // libellés traduits (même nombre de segments)
        void setLabels (juce::StringArray labelsIn)
        {
            labels = std::move (labelsIn);
            idealWidths.clear();
            for (auto& l : labels)
                idealWidths.push_back (juce::GlyphArrangement::getStringWidth (font (12.5f, 1), l) + 22.0f);
            repaint();
        }

        std::function<void (int)> onChange;

        // Teinte du segment choisi (transparente = pastille Apple). Sert a
        // garder le violet de l'expandeur.
        void setTint (juce::Colour c)   { tint = c; repaint(); }

        void setSelected (int index)
        {
            if (index != selected)
            {
                selected = index;
                repaint();
            }
        }

        int getIdealWidth() const
        {
            float w = 4.0f;
            for (auto iw : idealWidths)
                w += iw;
            return (int) std::ceil (w);
        }

        void paint (juce::Graphics& g) override
        {
            const auto th = Theme::current();
            g.setColour (th.field);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

            const auto segs = segmentBounds();
            for (int i = 0; i < labels.size(); ++i)
            {
                const auto& s = segs[(size_t) i];
                const bool on = (i == selected);
                if (on)
                {
                    juce::Path p;
                    p.addRoundedRectangle (s, 6.0f);
                    juce::DropShadow (juce::Colours::black.withAlpha (th.dark ? 0.4f : 0.18f), 3, { 0, 1 }).drawForPath (g, p);
                    g.setColour (tint.isTransparent() ? th.segOn : tint);
                    g.fillPath (p);
                }
                else
                {
                    if (i == hovered)
                    {
                        g.setColour (th.hover);
                        g.fillRoundedRectangle (s, 6.0f);
                    }
                    // Fin separateur entre deux segments non choisis.
                    if (i > 0 && i - 1 != selected && i != hovered && i - 1 != hovered)
                    {
                        g.setColour (th.fg.withAlpha (0.14f));
                        g.fillRect (juce::Rectangle<float> (s.getX() - 0.5f, s.getY() + s.getHeight() * 0.25f,
                                                            1.0f, s.getHeight() * 0.5f));
                    }
                }
                g.setColour (on ? (tint.isTransparent() ? th.segOnFg : juce::Colours::white) : th.fg);
                g.setFont (font (12.5f, on ? 1 : 0));
                g.drawText (labels[i], s, juce::Justification::centred, false);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override   { setHovered (indexAt (e.position.x)); }
        void mouseExit (const juce::MouseEvent&) override      { setHovered (-1); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = indexAt (e.position.x);
            if (i >= 0 && i != selected)
            {
                selected = i;
                repaint();
                if (onChange)
                    onChange (i);
            }
        }

    private:
        std::vector<juce::Rectangle<float>> segmentBounds() const
        {
            const auto inner = getLocalBounds().toFloat().reduced (2.0f);
            float total = 0.0f;
            for (auto iw : idealWidths)
                total += iw;
            const float k = total > 0.0f ? inner.getWidth() / total : 1.0f;

            std::vector<juce::Rectangle<float>> out;
            float x = inner.getX();
            for (auto iw : idealWidths)
            {
                out.push_back ({ x, inner.getY(), iw * k, inner.getHeight() });
                x += iw * k;
            }
            return out;
        }

        int indexAt (float x) const
        {
            const auto segs = segmentBounds();
            for (int i = 0; i < (int) segs.size(); ++i)
                if (x >= segs[(size_t) i].getX() && x < segs[(size_t) i].getRight())
                    return i;
            return -1;
        }

        void setHovered (int i)
        {
            if (i != hovered)
            {
                hovered = i;
                repaint();
            }
        }

        juce::StringArray labels;
        std::vector<float> idealWidths;
        int selected = -1, hovered = -1;
        juce::Colour tint; // transparente par defaut

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Segmented)
    };

    //==============================================================================
    // Interrupteur iOS 38x22, vert quand actif, pastille animee.
    class Switch : public juce::Component, private juce::Timer
    {
    public:
        Switch()   { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        std::function<void (bool)> onChange;

        void setOn (bool shouldBeOn)
        {
            on = shouldBeOn;
            pos = on ? 1.0f : 0.0f;
            stopTimer();
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto th = Theme::current();
            const auto track = getLocalBounds().toFloat().withSizeKeepingCentre (38.0f, 22.0f);
            g.setColour (th.field.interpolatedWith (juce::Colour (0xff34c759), pos));
            g.fillRoundedRectangle (track, 11.0f);

            const float k = 18.0f;
            const float cx = track.getX() + 2.0f + k * 0.5f + pos * (track.getWidth() - 4.0f - k);
            juce::Path knob;
            knob.addEllipse (juce::Rectangle<float> (k, k).withCentre ({ cx, track.getCentreY() }));
            juce::DropShadow (juce::Colours::black.withAlpha (0.3f), 3, { 0, 1 }).drawForPath (g, knob);
            g.setColour (juce::Colours::white);
            g.fillPath (knob);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! e.mouseWasClicked() || ! getLocalBounds().contains (e.getPosition()))
                return;
            on = ! on;
            startTimerHz (60);
            if (onChange)
                onChange (on);
        }

    private:
        void timerCallback() override
        {
            const float target = on ? 1.0f : 0.0f;
            pos += (target - pos) * 0.35f;
            if (std::abs (target - pos) < 0.02f)
            {
                pos = target;
                stopTimer();
            }
            repaint();
        }

        bool on = false;
        float pos = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Switch)
    };

    //==============================================================================
    // Curseur 0..100 + valeur affichee a droite.
    class ValueSlider : public juce::Component
    {
    public:
        ValueSlider()
        {
            addAndMakeVisible (slider);
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRange (0.0, 100.0, 1.0);
            // La molette fait defiler la page, pas le curseur.
            slider.setScrollWheelEnabled (false);
            slider.setMouseCursor (juce::MouseCursor::PointingHandCursor);
            slider.onValueChange = [this]
            {
                repaint();
                if (onValueChange)
                    onValueChange();
            };
        }

        std::function<void()> onValueChange;

        double getValue() const        { return slider.getValue(); }
        void setValue (double v)       { slider.setValue (v, juce::dontSendNotification); repaint(); }
        void setDefault (double v)     { slider.setDoubleClickReturnValue (true, v); }

        void resized() override
        {
            auto r = getLocalBounds();
            r.removeFromRight (40);
            slider.setBounds (r);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (Theme::current().muted);
            g.setFont (font (13.0f));
            g.drawText (juce::String (juce::roundToInt (slider.getValue())), getLocalBounds().removeFromRight (32),
                        juce::Justification::centredRight, false);
        }

    private:
        juce::Slider slider;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValueSlider)
    };

    //==============================================================================
    // Bouton gris a texte bleu ("Open").
    class PillButton : public juce::Button
    {
    public:
        explicit PillButton (const juce::String& text) : juce::Button (text)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto th = Theme::current();
            g.setColour (down ? th.field.contrasting (0.1f) : over ? th.field.contrasting (0.05f) : th.field);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
            g.setColour (th.accent);
            g.setFont (font (13.0f, 1));
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
        }
    };

    // Croix de fermeture ronde.
    class CloseButton : public juce::Button
    {
    public:
        CloseButton() : juce::Button ("Close")
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto th = Theme::current();
            const auto r = getLocalBounds().toFloat();
            g.setColour (down ? th.field.contrasting (0.1f) : over ? th.field.contrasting (0.05f) : th.field);
            g.fillEllipse (r);
            drawGlyph (g, *icons, Icon::close, r.withSizeKeepingCentre (14.0f, 14.0f), th.fg, 2.4f);
        }

    private:
        juce::SharedResourcePointer<IconSet> icons;
    };

    //==============================================================================
    // Ligne de la barre laterale : pastille + nom + nombre de reglages.
    class CategoryItem : public juce::Button
    {
    public:
        CategoryItem (const juce::String& name, Icon iconIn, juce::Colour colourIn)
            : juce::Button (name), icon (iconIn), colour (colourIn)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void setCount (int n)   { count = n; repaint(); }

        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            const auto th = Theme::current();
            const auto r = getLocalBounds().toFloat();
            const bool sel = getToggleState();
            if (sel || over)
            {
                g.setColour (sel ? th.accent : th.hover);
                g.fillRoundedRectangle (r, 8.0f);
            }

            const auto badge = juce::Rectangle<float> (24.0f, 24.0f).withPosition (r.getX() + 8.0f, r.getCentreY() - 12.0f);
            drawBadge (g, *icons, icon, badge, colour);

            const auto textArea = r.withLeft (badge.getRight() + 10.0f).withTrimmedRight (10.0f);
            g.setColour (sel ? juce::Colours::white.withAlpha (0.8f) : th.muted);
            g.setFont (font (12.0f));
            g.drawText (juce::String (count), textArea, juce::Justification::centredRight, false);
            g.setColour (sel ? juce::Colours::white : th.fg);
            g.setFont (font (14.0f));
            g.drawText (getButtonText(), textArea.withTrimmedRight (24.0f), juce::Justification::centredLeft, true);
        }

    private:
        Icon icon;
        juce::Colour colour;
        int count = 0;
        juce::SharedResourcePointer<IconSet> icons;
    };

    //==============================================================================
    // Page d'une categorie : groupes (titre, carte de lignes, note de bas de
    // groupe). Calcule sa propre hauteur via layout(width), le Viewport
    // parent s'en sert pour savoir jusqu'ou defiler.
    class Page : public juce::Component
    {
    public:
        void addGroup (const juce::String& title, const juce::String& footer = {})
        {
            Group grp;
            grp.title = title;
            grp.footer = footer;
            groups.push_back (grp);
        }

        // control peut etre nul (ligne d'information seule).
        void addRow (Icon icon, juce::Colour colour, const juce::String& title, const juce::String& subtitle,
                     juce::Component* control = nullptr, int controlWidth = 0, int controlHeight = 28)
        {
            Row r;
            r.icon = icon;
            r.colour = colour;
            r.title = title;
            r.subtitle = subtitle;
            r.control = control;
            r.controlWidth = controlWidth;
            r.controlHeight = controlHeight;
            groups.back().rows.push_back (r);
            if (control != nullptr)
                addAndMakeVisible (*control);
        }

        // Ligne-choix (page Langue) : pastille à texte ("FR"), toute la ligne
        // est cliquable, coche à droite sur le choix courant.
        void addChoiceRow (const juce::String& badgeText, juce::Colour colour, const juce::String& title,
                           const juce::String& subtitle, bool checked, std::function<void()> onClick)
        {
            Row r;
            r.badgeText = badgeText;
            r.colour = colour;
            r.title = title;
            r.subtitle = subtitle;
            r.checked = checked;
            r.onClick = std::move (onClick);
            groups.back().rows.push_back (r);
        }

        // coche sur la n-ième ligne-choix de la page
        void setChoice (int index)
        {
            int n = 0;
            for (auto& grp : groups)
                for (auto& r : grp.rows)
                    if (r.onClick != nullptr)
                        r.checked = (n++ == index);
            repaint();
        }

        // vide la page avant de la reconstruire (changement de langue) ; les
        // contrôles restent des enfants de la page et sont repris par addRow
        void clear()
        {
            groups.clear();
            hoveredGroup = hoveredRow = -1;
        }

        void setRowVisible (juce::Component& control, bool shouldBeVisible)
        {
            for (auto& grp : groups)
                for (auto& r : grp.rows)
                    if (r.control == &control)
                        r.visible = shouldBeVisible;
            control.setVisible (shouldBeVisible);
        }

        // sous-titre d'une ligne reperee par son titre (infos qui changent)
        void setRowSubtitle (const juce::String& title, const juce::String& subtitle)
        {
            for (auto& grp : groups)
                for (auto& r : grp.rows)
                    if (r.title == title)
                        r.subtitle = subtitle;
            repaint();
        }

        void setFooter (size_t groupIndex, const juce::String& text)
        {
            if (groupIndex < groups.size())
                groups[groupIndex].footer = text;
        }

        int getNumVisibleRows() const
        {
            int n = 0;
            for (auto& grp : groups)
                for (auto& r : grp.rows)
                    n += r.visible ? 1 : 0;
            return n;
        }

        void layout (int width)
        {
            if (width <= 0)
                return;

            constexpr float left = 28.0f, right = 20.0f, padX = 14.0f;
            const float cardW = (float) width - left - right;
            float y = 2.0f;

            for (auto& grp : groups)
            {
                grp.card = {};
                grp.titleArea = {};
                grp.footerArea = {};
                if (std::none_of (grp.rows.begin(), grp.rows.end(), [] (const Row& r) { return r.visible; }))
                    continue;

                if (grp.title.isNotEmpty())
                {
                    grp.titleArea = { left + 12.0f, y + 12.0f, cardW - 12.0f, 18.0f };
                    y += 36.0f;
                }
                else
                {
                    y += 10.0f;
                }

                const float top = y;
                for (auto& r : grp.rows)
                {
                    if (! r.visible)
                        continue;
                    const float h = r.subtitle.isNotEmpty() ? 54.0f : 44.0f;
                    r.bounds = { left, y, cardW, h };
                    if (r.control != nullptr)
                    {
                        const float cw = juce::jmin ((float) r.controlWidth, cardW - 260.0f);
                        r.control->setBounds (juce::Rectangle<float> (left + cardW - padX - cw,
                                                                      y + (h - (float) r.controlHeight) * 0.5f,
                                                                      cw, (float) r.controlHeight).toNearestInt());
                    }
                    y += h;
                }
                grp.card = { left, top, cardW, y - top };

                if (grp.footer.isNotEmpty())
                {
                    grp.footerArea = { left + 12.0f, y + 6.0f, cardW - 24.0f, 32.0f };
                    y += 40.0f;
                }
                y += 8.0f;
            }

            setSize (width, juce::roundToInt (y + 16.0f));
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto th = Theme::current();
            for (auto& grp : groups)
            {
                if (grp.card.isEmpty())
                    continue;

                if (! grp.titleArea.isEmpty())
                {
                    g.setColour (th.muted);
                    g.setFont (font (12.0f, 1));
                    g.drawText (grp.title, grp.titleArea, juce::Justification::centredLeft, true);
                }

                g.setColour (th.group);
                g.fillRoundedRectangle (grp.card, 10.0f);
                g.setColour (th.sep);
                g.drawRoundedRectangle (grp.card.reduced (0.25f), 10.0f, 0.5f);

                bool first = true;
                for (size_t ri = 0; ri < grp.rows.size(); ++ri)
                {
                    auto& r = grp.rows[ri];
                    if (! r.visible)
                        continue;

                    if (r.onClick != nullptr && (int) (&grp - groups.data()) == hoveredGroup && (int) ri == hoveredRow)
                    {
                        // survol d'une ligne-choix, découpé aux coins arrondis de la carte
                        juce::Graphics::ScopedSaveState state (g);
                        juce::Path card;
                        card.addRoundedRectangle (grp.card, 10.0f);
                        g.reduceClipRegion (card);
                        g.setColour (th.hover);
                        g.fillRect (r.bounds);
                    }

                    const auto badge = juce::Rectangle<float> (28.0f, 28.0f).withPosition (r.bounds.getX() + 14.0f,
                                                                                         r.bounds.getCentreY() - 14.0f);
                    const float textX = badge.getRight() + 12.0f;
                    if (! first)
                    {
                        // Separateur en retrait, aligne sur le texte (macOS).
                        g.setColour (th.sep);
                        g.fillRect (juce::Rectangle<float> (textX, r.bounds.getY(), r.bounds.getRight() - textX, 1.0f));
                    }
                    first = false;

                    if (r.badgeText.isNotEmpty())
                    {
                        g.setColour (r.colour);
                        g.fillRoundedRectangle (badge, 7.0f);
                        g.setColour (juce::Colours::white);
                        g.setFont (font (11.0f, 2));
                        g.drawText (r.badgeText, badge, juce::Justification::centred, false);
                    }
                    else
                    {
                        drawBadge (g, *icons, r.icon, badge, r.colour);
                    }

                    if (r.checked)
                        drawGlyph (g, *icons, Icon::check,
                                   juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ r.bounds.getRight() - 26.0f, r.bounds.getCentreY() }),
                                   th.accent, 2.6f);

                    const float textRight = r.control != nullptr ? (float) r.control->getX() - 12.0f
                                          : r.onClick != nullptr ? r.bounds.getRight() - 48.0f
                                                                 : r.bounds.getRight() - 14.0f;
                    const float textW = juce::jmax (10.0f, textRight - textX);
                    g.setColour (th.fg);
                    g.setFont (font (14.0f));
                    if (r.subtitle.isEmpty())
                    {
                        g.drawText (r.title, juce::Rectangle<float> (textX, r.bounds.getY(), textW, r.bounds.getHeight()),
                                    juce::Justification::centredLeft, true);
                    }
                    else
                    {
                        g.drawText (r.title, juce::Rectangle<float> (textX, r.bounds.getY() + 8.0f, textW, 20.0f),
                                    juce::Justification::centredLeft, true);
                        g.setColour (th.muted);
                        g.setFont (font (12.0f));
                        g.drawText (r.subtitle, juce::Rectangle<float> (textX, r.bounds.getY() + 28.0f, textW, 17.0f),
                                    juce::Justification::centredLeft, true);
                    }
                }

                if (! grp.footerArea.isEmpty())
                {
                    g.setColour (th.muted);
                    g.setFont (font (12.0f));
                    g.drawFittedText (grp.footer, grp.footerArea.toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
                }
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            int gi = -1, ri = -1;
            choiceAt (e.position, gi, ri);
            setMouseCursor (gi >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            if (gi != hoveredGroup || ri != hoveredRow)
            {
                hoveredGroup = gi;
                hoveredRow = ri;
                repaint();
            }
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            if (hoveredGroup >= 0)
            {
                hoveredGroup = hoveredRow = -1;
                repaint();
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            int gi = -1, ri = -1;
            if (! e.mouseWasClicked() || ! choiceAt (e.position, gi, ri))
                return;
            // copie : le rappel peut reconstruire la page (changement de langue)
            const auto fn = groups[(size_t) gi].rows[(size_t) ri].onClick;
            fn();
        }

    private:
        struct Row
        {
            Icon icon = Icon::gear;
            juce::Colour colour;
            juce::String title, subtitle, badgeText;
            juce::Component* control = nullptr;
            int controlWidth = 0, controlHeight = 28;
            bool visible = true, checked = false;
            std::function<void()> onClick;
            juce::Rectangle<float> bounds;
        };

        bool choiceAt (juce::Point<float> p, int& gi, int& ri) const
        {
            for (size_t g = 0; g < groups.size(); ++g)
                for (size_t r = 0; r < groups[g].rows.size(); ++r)
                {
                    const auto& row = groups[g].rows[r];
                    if (row.visible && row.onClick != nullptr && row.bounds.contains (p))
                    {
                        gi = (int) g;
                        ri = (int) r;
                        return true;
                    }
                }
            gi = ri = -1;
            return false;
        }

        int hoveredGroup = -1, hoveredRow = -1;

        struct Group
        {
            juce::String title, footer;
            std::vector<Row> rows;
            juce::Rectangle<float> titleArea, card, footerArea;
        };

        std::vector<Group> groups;
        juce::SharedResourcePointer<IconSet> icons;
    };
}

//==============================================================================
// Pastilles de couleur d'accent (palettes de Merlin), la choisie cerclée.
//==============================================================================
class AccentSwatches : public juce::Component
{
public:
    std::function<void (int)> onSelect;

    void setPalette (const juce::Array<juce::Colour>& colours, int selectedIndex)
    {
        palette = colours;
        selected = selectedIndex;
        repaint();
    }

    int getIdealWidth() const   { return palette.size() * 26 + 4; }

    void paint (juce::Graphics& g) override
    {
        const auto th = OptionsUi::Theme::current();
        for (int i = 0; i < palette.size(); ++i)
        {
            const auto r = dotBounds (i);
            g.setColour (palette[i]);
            g.fillEllipse (r.reduced (3.0f));
            if (i == selected || i == hovered)
            {
                g.setColour (i == selected ? th.accent : th.muted);
                g.drawEllipse (r.reduced (0.75f), 1.5f);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override   { const int i = indexAt (e.position); if (i != hovered) { hovered = i; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override      { hovered = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = indexAt (e.position);
        if (i >= 0)
        {
            selected = i;
            repaint();
            if (onSelect)
                onSelect (i);
        }
    }

private:
    juce::Rectangle<float> dotBounds (int i) const
    {
        return juce::Rectangle<float> (24.0f, 24.0f).withPosition (2.0f + (float) i * 26.0f, (float) getHeight() * 0.5f - 12.0f);
    }
    int indexAt (juce::Point<float> p) const
    {
        for (int i = 0; i < palette.size(); ++i)
            if (dotBounds (i).contains (p))
                return i;
        return -1;
    }

    juce::Array<juce::Colour> palette;
    int selected = -1, hovered = -1;
};

//==============================================================================
// Pastille d'une couleur réglable : clic = sélecteur de couleur dans une bulle.
//==============================================================================
class ColourChip : public juce::Button
{
public:
    ColourChip() : juce::Button ("colour")   { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    std::function<void (juce::Colour)> onPicked;
    void setColour (juce::Colour c)   { colour = c; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto th = OptionsUi::Theme::current();
        const auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (over ? th.accent : th.edge);
        g.drawRoundedRectangle (r, 6.0f, over ? 1.5f : 1.0f);
    }

    void clicked() override
    {
        struct Picker : public juce::ColourSelector, private juce::ChangeListener
        {
            Picker (juce::Colour c, std::function<void (juce::Colour)> fn)
                : juce::ColourSelector (juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
                                        | juce::ColourSelector::showColourspace),
                  callback (std::move (fn))
            {
                setCurrentColour (c, juce::dontSendNotification);
                addChangeListener (this);
                setSize (300, 280);
            }
            ~Picker() override   { removeChangeListener (this); }
            void changeListenerCallback (juce::ChangeBroadcaster*) override   { if (callback) callback (getCurrentColour()); }
            std::function<void (juce::Colour)> callback;
        };

        juce::Component::SafePointer<ColourChip> sp (this);
        juce::CallOutBox::launchAsynchronously (std::make_unique<Picker> (colour, [sp] (juce::Colour c)
                                                {
                                                    if (sp == nullptr) return;
                                                    sp->setColour (c);
                                                    if (sp->onPicked) sp->onPicked (c);
                                                }),
                                                getScreenBounds(), nullptr);
    }

private:
    juce::Colour colour { juce::Colours::grey };
};

//==============================================================================
// OptionsOverlay — menu Options de Merlin (trois points de la barre de titre),
// style "Réglages" d'Apple repris de Sevenpack Alpha : catégories à gauche
// avec pictogrammes, réglages en cartes à droite. Réunit ce qui était éparpillé
// (menus Audio / Graphique / Affichage de la fenêtre, panneau Settings).
// Se ferme au clic hors du panneau, avec la croix ou Échap.
//==============================================================================
class OptionsOverlay : public juce::Component
{
public:
    // État affiché, relu à chaque ouverture (setState ne déclenche aucun rappel)
    struct State
    {
        int audioSource = 0;                // 0 automatique, 1 carte son, 2 sortie Windows
        juce::String sourceName;
        float windowsVolume = -1.0f;        // 0..1, < 0 si indisponible
        int fps = 60, msaa = 4;
        bool vsync = true;
        juce::String gpuInfo;
        bool fullScreen = false, alwaysOnTop = false;
        bool dark = true, contrast = false, modules3D = false, largeText = false, masterConsole = false;
        int oscStyle = 0;                   // 0 tube, 1 numérique, 2 vintage, 3 deux voies
        int oscFps = 30;                    // 24, 30 ou 60
        bool oscPersist = true;
        int uiTemplate = 0;
        juce::Array<juce::Colour> accentPalette;
        int accentIndex = -1;
        juce::Colour roles[8];
        juce::String presetName;
        juce::String licenceText;           // « Activée », « Démo : 12 jours restants »…
        int language = 0;                   // Lang::Id
    };

    OptionsOverlay()
    {
        setLookAndFeel (&laf);
        setWantsKeyboardFocus (true);

        for (int i = 0; i < kNumCategories; ++i)
        {
            auto* item = categoryItems.add (new OptionsUi::CategoryItem (TR (categories()[i].name), categories()[i].icon,
                                                                         juce::Colour (categories()[i].colour)));
            addAndMakeVisible (item);
            item->onClick = [this, i] { showCategory (i); };
        }

        addAndMakeVisible (closeButton);
        closeButton.onClick = [this] { if (onDismiss) onDismiss(); };

        addAndMakeVisible (viewport);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);

        buildPages();
        // en plugin, la source est la piste : pas de page Audio
        categoryItems[kAudioPage]->setVisible (! AppMode::isPlugin);
        showCategory (AppMode::isPlugin ? kImagePage : kAudioPage);
    }

    // Langue changée : textes des catégories, pages reconstruites dans la
    // langue courante (les contrôles et leur état sont gardés), puis relayout.
    // L'éditeur rappelle setState juste après.
    void retranslate()
    {
        for (int i = 0; i < kNumCategories; ++i)
            categoryItems[i]->setButtonText (TR (categories()[i].name));
        for (auto& p : pages)
            p.clear();
        buildPages();
        resized();
        showCategory (currentCategory);
        repaint();
    }

    ~OptionsOverlay() override   { setLookAndFeel (nullptr); }

    // teinte claire / sombre de Merlin
    void refreshTheme (bool light)
    {
        OptionsUi::lightTheme() = light;
        laf.refresh();
        sendLookAndFeelChange();
        for (auto& p : pages)
            p.sendLookAndFeelChange();
        repaint();
    }

    void setState (const State& s)
    {
       #if JUCE_WINDOWS
        sourceSeg.setSelected (juce::jlimit (0, 2, s.audioSource));
       #else
        sourceSeg.setSelected (juce::jlimit (0, 1, s.audioSource));   // pas de « Sortie Windows »
       #endif
        pages[kAudioPage].setRowSubtitle (TR ("En cours"), s.sourceName);
        pages[kLanguagePage].setChoice (s.language);
        setWindowsVolume (s.windowsVolume);

        static const int fpsValues[] = { 30, 60, 90, 120, 144 };
        for (int i = 0; i < 5; ++i)
            if (fpsValues[i] == s.fps) fpsSeg.setSelected (i);
        msaaSeg.setSelected (s.msaa >= 8 ? 3 : s.msaa >= 4 ? 2 : s.msaa >= 2 ? 1 : 0);
        vsyncSwitch.setOn (s.vsync);
        pages[kImagePage].setRowSubtitle (TR ("Carte graphique"),
                                          s.gpuInfo.isNotEmpty() ? s.gpuInfo : TR ("Détectée à l'ouverture d'un module GPU"));

        fullScreenSwitch.setOn (s.fullScreen);
        alwaysOnTopSwitch.setOn (s.alwaysOnTop);
        darkSwitch.setOn (s.dark);
        contrastSwitch.setOn (s.contrast);
        modules3DSwitch.setOn (s.modules3D);
        largeTextSwitch.setOn (s.largeText);
        masterSeg.setSelected (s.masterConsole ? 1 : 0);
        oscStyleSeg.setSelected (juce::jlimit (0, 3, s.oscStyle));
        oscFpsSeg.setSelected (s.oscFps <= 24 ? 0 : s.oscFps <= 30 ? 1 : 2);
        oscPersistSwitch.setOn (s.oscPersist);

        templateSeg.setSelected (s.uiTemplate);
        accentSwatches.setPalette (s.accentPalette, s.accentIndex);
        for (int i = 0; i < 8; ++i)
            roleChips[i].setColour (s.roles[i]);

        pages[kAboutPage].setRowSubtitle (TR ("Licence"), s.licenceText);
        pages[kPresetsPage].setRowSubtitle (TR ("Enregistrer le preset"),
                                            s.presetName.isNotEmpty() ? TR ("Preset en cours : {0}").replace ("{0}", s.presetName)
                                                                      : TR ("Disposition, réglages des modules et apparence"));
    }

    void setWindowsVolume (float v)
    {
        if (v >= 0.0f && ! volumeSlider.isMouseButtonDown (true))
            volumeSlider.setValue (std::round (v * 100.0f));
    }

    void showCategory (int index)
    {
        currentCategory = juce::jlimit (0, kNumCategories - 1, index);
        for (int i = 0; i < kNumCategories; ++i)
            categoryItems[i]->setToggleState (i == currentCategory, juce::dontSendNotification);
        viewport.setViewedComponent (&pages[currentCategory], false);
        pages[currentCategory].sendLookAndFeelChange();   // une page détachée ne suit pas le LookAndFeel
        viewport.setViewPosition (0, 0);
        repaint (titleArea);
    }

    void resized() override
    {
        const auto bounds = getLocalBounds().toFloat();
        sheetBounds = bounds.withSizeKeepingCentre (juce::jmin (1000.0f, bounds.getWidth() - 40.0f),
                                                    juce::jmin (640.0f, bounds.getHeight() - 40.0f));
        auto sheet = sheetBounds;
        sidebarBounds = sheet.removeFromLeft (kSidebarWidth);

        auto side = sidebarBounds.reduced (10.0f, 14.0f);
        brandBounds = side.removeFromTop (52.0f);
        side.removeFromTop (8.0f);
        for (auto* item : categoryItems)
        {
            if (! item->isVisible())
                continue;
            item->setBounds (side.removeFromTop (36.0f).toNearestInt());
            side.removeFromTop (2.0f);
        }

        titleArea = sheet.removeFromTop (64.0f).toNearestInt();
        closeButton.setBounds (juce::Rectangle<int> (30, 30).withCentre ({ titleArea.getRight() - 32, titleArea.getCentreY() + 2 }));
        sheet.removeFromBottom (12.0f);
        viewport.setBounds (sheet.toNearestInt());
        for (auto& p : pages)
            p.layout (viewport.getWidth() - viewport.getScrollBarThickness());

        rebuildShadow();
    }

    void paint (juce::Graphics& g) override
    {
        const auto th = OptionsUi::Theme::current();
        g.fillAll (juce::Colours::black.withAlpha (0.5f));   // voile
        g.drawImageAt (shadowImage, shadowOrigin.x, shadowOrigin.y);

        juce::Path sheet;
        sheet.addRoundedRectangle (sheetBounds, 14.0f);
        g.setColour (th.bg);
        g.fillPath (sheet);
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (sheet);
            g.setColour (th.side);
            g.fillRect (sidebarBounds);
            g.setColour (th.sep);
            g.fillRect (sidebarBounds.withLeft (sidebarBounds.getRight() - 1.0f));
        }
        g.setColour (th.edge);
        g.strokePath (sheet, juce::PathStrokeType (1.0f));

        // Marque : pastille engrenage, "Options", version
        auto brand = brandBounds.reduced (8.0f, 6.0f);
        const auto badge = brand.removeFromLeft (34.0f).withSizeKeepingCentre (34.0f, 34.0f);
        OptionsUi::drawBadge (g, *icons, OptionsUi::Icon::gear, badge, juce::Colour (0xff3fa9f5));
        brand.removeFromLeft (10.0f);
        g.setColour (th.fg);
        g.setFont (OptionsUi::font (15.0f, 2));
        g.drawText (TR ("Options"), brand.removeFromTop (brand.getHeight() * 0.5f + 1.0f), juce::Justification::bottomLeft, true);
        g.setColour (th.muted);
        g.setFont (OptionsUi::font (11.5f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("Merlin \xce\xb1 ")) + ProjectInfo::versionString, brand, juce::Justification::topLeft, true);

        g.setColour (th.fg);
        g.setFont (OptionsUi::font (22.0f, 2));
        g.drawText (categoryItems[currentCategory]->getButtonText(), titleArea.withTrimmedLeft (28).withTrimmedRight (70),
                    juce::Justification::centredLeft, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! sheetBounds.contains (e.position) && onDismiss)
            onDismiss();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey && onDismiss)
        {
            onDismiss();
            return true;
        }
        return false;
    }

    std::function<void()> onDismiss;
    // Audio
    std::function<void (int)>   onAudioSourceSelected;   // 0 auto, 1 carte, 2 sortie Windows
    std::function<void()>       onOpenAudioSettings;
    std::function<void (float)> onWindowsVolumeChanged;  // 0..1
    // Image
    std::function<void (int)>   onFpsSelected;
    std::function<void (int)>   onMsaaSelected;          // 0, 2, 4, 8
    std::function<void (bool)>  onVsyncChanged;
    // Affichage
    std::function<void (bool)>  onFullScreenChanged, onAlwaysOnTopChanged, onDarkChanged, onContrastChanged,
                                onModules3DChanged, onLargeTextChanged, onMasterConsoleChanged;
    std::function<void (int)>   onOscStyleSelected;      // 0 tube, 1 numérique, 2 vintage, 3 deux voies
    std::function<void (int)>   onOscFpsSelected;        // 24, 30, 60
    std::function<void (bool)>  onOscPersistChanged;
    // Apparence
    std::function<void (int)>   onTemplateSelected;
    std::function<void()>       onOpenTemplateGallery;
    std::function<void (int)>   onAccentSelected;        // indice de palette
    std::function<void (int, juce::Colour)> onColourRoleChanged;
    std::function<void()>       onColoursReset;
    // Presets / à propos
    std::function<void()>       onSavePreset, onOpenPresetFolder, onResetLayout, onOpenAbout, onOpenLicence;
    // Langue
    std::function<void (int)>   onLanguageSelected;      // Lang::Id

private:
    static constexpr int kNumCategories = 7;
    enum { kAudioPage, kImagePage, kDisplayPage, kAppearancePage, kLanguagePage, kPresetsPage, kAboutPage };
    static constexpr float kSidebarWidth = 230.0f;

    struct CategoryDef { const char* name; OptionsUi::Icon icon; juce::uint32 colour; };
    static const CategoryDef* categories()
    {
        using OptionsUi::Icon;
        static const CategoryDef cats[kNumCategories] =
        {
            { "Audio",     Icon::mic,    0xffff453a },
            { "Image",     Icon::gauge,  0xff0a84ff },
            { "Affichage", Icon::window, 0xff5e5ce6 },
            { "Apparence", Icon::brush,  0xffff375f },
            { "Langue",    Icon::globe,  0xff30b0c7 },
            { "Presets",   Icon::stack,  0xff30b158 },
            { "\xc3\x80 propos", Icon::info, 0xff8e8e93 },
        };
        return cats;
    }

    void buildPages()
    {
        buildAudioPage();
        buildImagePage();
        buildDisplayPage();
        buildAppearancePage();
        buildLanguagePage();
        buildPresetsPage();
        buildAboutPage();
        for (int i = 0; i < kNumCategories; ++i)
            categoryItems[i]->setCount (pages[i].getNumVisibleRows());
    }

    void rebuildShadow()
    {
        const auto area = sheetBounds.expanded (64.0f).getSmallestIntegerContainer();
        shadowOrigin = area.getPosition();
        shadowImage = juce::Image (juce::Image::ARGB, juce::jmax (1, area.getWidth()), juce::jmax (1, area.getHeight()), true);
        juce::Graphics sg (shadowImage);
        juce::Path p;
        p.addRoundedRectangle (sheetBounds.translated ((float) -area.getX(), (float) -area.getY()), 14.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 48, { 0, 18 }).drawForPath (sg, p);
    }

    void buildAudioPage()
    {
        using OptionsUi::Icon;
        auto& p = pages[kAudioPage];
        audioSettingsButton.setButtonText (TR ("Réglages…"));
       #if JUCE_WINDOWS
        sourceSeg.setLabels ({ TR ("Automatique"), TR ("Carte son"), TR ("Sortie Windows") });
        p.addGroup (TR ("Source analysée"),
                    TR ("Automatique : la carte son si elle a une entrée Loopback, sinon ce qui joue sur le PC. "
                        "La sortie Windows n'entend pas l'ASIO d'Ableton."));
       #else
        // Mac : pas encore de capture de la sortie système
        sourceSeg.setLabels ({ TR ("Automatique"), TR ("Carte son") });
        p.addGroup (TR ("Source analysée"),
                    TR ("Automatique : l'entrée Loopback de la carte son si elle existe, sinon ses entrées 1-2."));
       #endif
        p.addRow (Icon::mic, juce::Colour (0xffff453a), TR ("Source"), TR ("Ce que Merlin mesure"), &sourceSeg, sourceSeg.getIdealWidth());
        p.addRow (Icon::out, juce::Colour (0xffff9f0a), TR ("En cours"), {}, &audioSettingsButton, 104);
        p.addGroup (TR ("Volume de Windows"), TR ("Le même réglage que le curseur de volume de la barre des tâches."));
        p.addRow (Icon::out, juce::Colour (0xff30b158), TR ("Volume"), TR ("Sortie Windows par défaut"), &volumeSlider, 260);

        sourceSeg.onChange = [this] (int i) { if (onAudioSourceSelected) onAudioSourceSelected (i); };
        audioSettingsButton.onClick = [this] { if (onOpenAudioSettings) onOpenAudioSettings(); };
        volumeSlider.onValueChange = [this] { if (onWindowsVolumeChanged) onWindowsVolumeChanged ((float) volumeSlider.getValue() * 0.01f); };
    }

    void buildImagePage()
    {
        using OptionsUi::Icon;
        auto& p = pages[kImagePage];
        msaaSeg.setLabels ({ TR ("Off"), "2x", "4x", "8x" });
        p.addGroup (TR ("Analyses"));
        p.addRow (Icon::cpu, juce::Colour (0xff8e8e93), TR ("Images par seconde"), TR ("30 = CPU léger"), &fpsSeg, fpsSeg.getIdealWidth());
        p.addGroup (TR ("Modules GPU"), TR ("L'anticrénelage lisse les contours et l'intérieur des reliefs ; plus il est élevé, "
                                           "plus la carte graphique travaille."));
        p.addRow (Icon::glass, juce::Colour (0xff64d2ff), TR ("Anticrénelage"), TR ("Spectrogrammes et modules OpenGL"), &msaaSeg, msaaSeg.getIdealWidth());
        p.addRow (Icon::timer, juce::Colour (0xffff9f0a), TR ("Synchro verticale"), TR ("Images calées sur l'écran (V-Sync)"), &vsyncSwitch, 44, 26);
        p.addRow (Icon::info, juce::Colour (0xff8e8e93), TR ("Carte graphique"), {});

        static const int fpsValues[] = { 30, 60, 90, 120, 144 };
        fpsSeg.onChange   = [this] (int i) { if (onFpsSelected) onFpsSelected (fpsValues[i]); };
        static const int msaaValues[] = { 0, 2, 4, 8 };
        msaaSeg.onChange  = [this] (int i) { if (onMsaaSelected) onMsaaSelected (msaaValues[i]); };
        vsyncSwitch.onChange = [this] (bool on) { if (onVsyncChanged) onVsyncChanged (on); };
    }

    void buildDisplayPage()
    {
        using OptionsUi::Icon;
        auto& p = pages[kDisplayPage];
        masterSeg.setLabels ({ TR ("Instrument"), TR ("Console") });
        oscStyleSeg.setLabels ({ TR ("Tube"), TR ("Numérique"), TR ("Vintage"), TR ("Deux voies") });
        if (! AppMode::isPlugin)   // en plugin, la fenêtre appartient à la DAW
        {
            p.addGroup (TR ("Fenêtre"));
            p.addRow (Icon::window, juce::Colour (0xff5e5ce6), TR ("Plein écran"), TR ("F11, Échap pour sortir"), &fullScreenSwitch, 44, 26);
            p.addRow (Icon::layers, juce::Colour (0xff8e8e93), TR ("Toujours au premier plan"), {}, &alwaysOnTopSwitch, 44, 26);
        }
        p.addGroup (TR ("Interface"));
        p.addRow (Icon::theme, juce::Colour (0xff48484a), TR ("Thème sombre"), {}, &darkSwitch, 44, 26);
        p.addRow (Icon::grid, juce::Colour (0xff30b0c7), TR ("Modules 3D"), TR ("Spectrum en blocs 3D"), &modules3DSwitch, 44, 26);
        p.addRow (Icon::text, juce::Colour (0xff0a84ff), TR ("Gros texte"), TR ("Lecture à distance"), &largeTextSwitch, 44, 26);
        p.addRow (Icon::gauge, juce::Colour (0xffff9f0a), TR ("Section master"), TR ("Style du VU master (aussi sous la colonne)"),
                  &masterSeg, masterSeg.getIdealWidth());
        p.addGroup (TR ("Modules"));
        p.addRow (Icon::phase, juce::Colour (0xff30b0c7), TR ("Oscilloscope"), TR ("Style de l'écran"), &oscStyleSeg, oscStyleSeg.getIdealWidth());
        p.addRow (Icon::timer, juce::Colour (0xff30b0c7), TR ("Cadence de l'oscilloscope"), TR ("Images par seconde : moins vite = plus lisible"),
                  &oscFpsSeg, oscFpsSeg.getIdealWidth());
        p.addRow (Icon::glass, juce::Colour (0xff30b0c7), TR ("Rémanence"), TR ("Les dernières images s'estompent, comme un écran à phosphore"),
                  &oscPersistSwitch, 44, 26);
        oscStyleSeg.onChange = [this] (int i) { if (onOscStyleSelected) onOscStyleSelected (i); };
        static const int oscFpsValues[] = { 24, 30, 60 };
        oscFpsSeg.onChange = [this] (int i) { if (onOscFpsSelected) onOscFpsSelected (oscFpsValues[i]); };
        oscPersistSwitch.onChange = [this] (bool on) { if (onOscPersistChanged) onOscPersistChanged (on); };

        fullScreenSwitch.onChange  = [this] (bool on) { if (onFullScreenChanged)  onFullScreenChanged (on); };
        alwaysOnTopSwitch.onChange = [this] (bool on) { if (onAlwaysOnTopChanged) onAlwaysOnTopChanged (on); };
        darkSwitch.onChange        = [this] (bool on) { if (onDarkChanged)        onDarkChanged (on); };
        contrastSwitch.onChange    = [this] (bool on) { if (onContrastChanged)    onContrastChanged (on); };
        modules3DSwitch.onChange   = [this] (bool on) { if (onModules3DChanged)   onModules3DChanged (on); };
        largeTextSwitch.onChange   = [this] (bool on) { if (onLargeTextChanged)   onLargeTextChanged (on); };
        masterSeg.onChange         = [this] (int i)   { if (onMasterConsoleChanged) onMasterConsoleChanged (i == 1); };
    }

    void buildAppearancePage()
    {
        using OptionsUi::Icon;
        auto& p = pages[kAppearancePage];
        resetColoursButton.setButtonText (TR ("Réinitialiser"));
        // Templates retirés (demande du user) : Merlin garde uniquement le
        // Neumorphic ; plus de choix de template ni de galerie.
        p.addGroup (TR ("Couleurs"), TR ("Les écrans des modules gardent leur fond sombre, en thème clair comme en sombre."));
        p.addRow (Icon::palette, juce::Colour (0xff0a84ff), TR ("Couleur d'accent"), TR ("Palettes prêtes à l'emploi"), &accentSwatches, accentSwatches.getIdealWidth() + 140);
        static const char* const roleNames[8] = { "Accent", "Grille", "Fond des écrans", "Texte", "Canal Side",
                                                  "Spectrogramme : creux", "Spectrogramme : milieu", "Spectrogramme : crêtes" };
        for (int i = 0; i < 8; ++i)
        {
            p.addRow (Icon::palette, juce::Colour (0xff636366), TR (roleNames[i]), {}, &roleChips[i], 52, 28);
            roleChips[i].onPicked = [this, i] (juce::Colour c) { if (onColourRoleChanged) onColourRoleChanged (i, c); };
        }
        p.addRow (Icon::back, juce::Colour (0xff8e8e93), TR ("Couleurs d'origine"), {}, &resetColoursButton, 110);

        accentSwatches.onSelect = [this] (int i) { if (onAccentSelected) onAccentSelected (i); };
        resetColoursButton.onClick = [this] { if (onColoursReset) onColoursReset(); };
    }

    void buildPresetsPage()
    {
        using OptionsUi::Icon;
        auto& p = pages[kPresetsPage];
        savePresetButton.setButtonText (TR ("Enregistrer…"));
        presetFolderButton.setButtonText (TR ("Ouvrir"));
        resetLayoutButton.setButtonText (TR ("Réinitialiser"));
        p.addGroup (TR ("Presets"), TR ("Un preset garde la disposition, les réglages des modules et l'apparence "
                                        "(pas la cadence ni les réglages GPU). Rappel par le menu PRESET."));
        p.addRow (Icon::stack, juce::Colour (0xff30b158), TR ("Enregistrer le preset"), {}, &savePresetButton, 110);
        p.addRow (Icon::folder, juce::Colour (0xff0a84ff), TR ("Dossier des presets"), TR ("Copier, renommer ou partager des presets"), &presetFolderButton, 90);
        p.addGroup (TR ("Disposition"));
        p.addRow (Icon::grid, juce::Colour (0xff8e8e93), TR ("Disposition des slots"), TR ("Revenir à la disposition d'origine"), &resetLayoutButton, 110);

        savePresetButton.onClick   = [this] { if (onSavePreset) onSavePreset(); };
        presetFolderButton.onClick = [this] { if (onOpenPresetFolder) onOpenPresetFolder(); };
        resetLayoutButton.onClick  = [this] { if (onResetLayout) onResetLayout(); };
    }

    void buildAboutPage()
    {
        using OptionsUi::Icon;
        auto& p = pages[kAboutPage];
        aboutButton.setButtonText (TR ("À propos"));
        p.addGroup ({});
        p.addRow (Icon::info, juce::Colour (0xff8e8e93), juce::String (juce::CharPointer_UTF8 ("Merlin \xce\xb1")), TR ("Version") + " " + ProjectInfo::versionString);
        p.addRow (Icon::key, juce::Colour (0xffffcc00), "Side Key System", "sidekey.fr", &aboutButton, 90);
        licenceButton.setButtonText (TR ("Gérer"));
        p.addRow (Icon::check, juce::Colour (0xff30d158), TR ("Licence"), {}, &licenceButton, 90);
        aboutButton.onClick = [this] { if (onOpenAbout) onOpenAbout(); };
        licenceButton.onClick = [this] { if (onOpenLicence) onOpenLicence(); };
    }

    // Langue : une ligne par langue (nom natif, nom dans la langue courante),
    // clic = bascule immédiate, coche sur la langue en cours
    void buildLanguagePage()
    {
        auto& p = pages[kLanguagePage];
        p.addGroup (TR ("Langue de l'interface"),
                    TR ("Appliquée tout de suite. Les termes techniques (LUFS, True Peak, RMS, dB, Hz…) restent en anglais."));
        static const char* const names[Lang::Count] = { "Français", "Anglais", "Allemand", "Italien", "Espagnol",
                                                        "Portugais (Brésil)", "Russe", "Japonais", "Chinois simplifié", "Coréen" };
        static const juce::uint32 colours[Lang::Count] = { 0xff0a84ff, 0xffff453a, 0xffff9f0a, 0xff30b158, 0xffd4a20a,
                                                           0xff34c759, 0xff5e5ce6, 0xffff375f, 0xffff6961, 0xff30b0c7 };
        for (int i = 0; i < Lang::Count; ++i)
            p.addChoiceRow (Lang::code (i), juce::Colour (colours[i]), Lang::nativeName (i), TR (names[i]),
                            i == Lang::current(), [this, i] { if (onLanguageSelected) onLanguageSelected (i); });
    }


    // Déclaré en premier : détruit en dernier, après tous les contrôles
    OptionsUi::LookAndFeel laf;
    juce::SharedResourcePointer<OptionsUi::IconSet> icons;

    // Audio
    OptionsUi::Segmented sourceSeg { { "Automatique", "Carte son", "Sortie Windows" } };
    OptionsUi::PillButton audioSettingsButton { juce::CharPointer_UTF8 ("Réglages…") };
    OptionsUi::ValueSlider volumeSlider;
    // Image
    OptionsUi::Segmented fpsSeg  { { "30", "60", "90", "120", "144" } };
    OptionsUi::Segmented msaaSeg { { "Off", "2x", "4x", "8x" } };
    OptionsUi::Switch vsyncSwitch;
    // Affichage
    OptionsUi::Switch fullScreenSwitch, alwaysOnTopSwitch, darkSwitch, contrastSwitch, modules3DSwitch, largeTextSwitch;
    OptionsUi::Segmented masterSeg { { "Instrument", "Console" } };
    OptionsUi::Segmented oscStyleSeg { juce::StringArray { juce::String ("Tube"), TR ("Numérique"), juce::String ("Vintage"), juce::String ("Deux voies") } };
    OptionsUi::Segmented oscFpsSeg { { "24", "30", "60" } };
    OptionsUi::Switch oscPersistSwitch;
    // Apparence
    OptionsUi::Segmented templateSeg { { "Stadium", "Forest", "Neumorphic" } };
    OptionsUi::PillButton galleryButton { "Ouvrir" };
    AccentSwatches accentSwatches;
    ColourChip roleChips[8];
    OptionsUi::PillButton resetColoursButton { juce::CharPointer_UTF8 ("Réinitialiser") };
    // Presets
    OptionsUi::PillButton savePresetButton   { juce::CharPointer_UTF8 ("Enregistrer…") };
    OptionsUi::PillButton presetFolderButton { "Ouvrir" };
    OptionsUi::PillButton resetLayoutButton  { juce::CharPointer_UTF8 ("Réinitialiser") };
    // À propos
    OptionsUi::PillButton aboutButton { juce::CharPointer_UTF8 ("À propos") };
    OptionsUi::PillButton licenceButton { "Licence" };

    OptionsUi::Page pages[kNumCategories];
    juce::OwnedArray<OptionsUi::CategoryItem> categoryItems;
    OptionsUi::CloseButton closeButton;
    juce::Viewport viewport;
    int currentCategory = 0;

    juce::Rectangle<float> sheetBounds, sidebarBounds, brandBounds;
    juce::Rectangle<int> titleArea;
    juce::Image shadowImage;
    juce::Point<int> shadowOrigin;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptionsOverlay)
};
