#pragma once
#include <JuceHeader.h>
#include <cstdint>
#include <map>
#include "PluginProcessor.h"
#include "GpuSpectrogram.h"
#include "GpuDotSpectrum.h"
#include "GpuVectorscope.h"
#include "GpuOscilloscope.h"
#include "GpuSurface.h"
#include "Lang.h"
#include "OptionsOverlay.h"
#include "LicenseOverlay.h"
#include "AlphaKnob.h"   // knob de Sevenpack Alpha (volume de l'en-tête)

// NB : toutes les couleurs ci-dessous sont des variables 'inline' (C++17, une
// seule instance partagée entre PluginEditor.cpp et PluginProcessor.cpp),
// modifiées à l'exécution par Theme::apply() (palettes rapides) ou
// Theme::applyCustom() (menu de couleurs entièrement personnalisable, panneau
// Settings) — bg/grid/border/knobDim et textHi/textMid sont dérivées les unes
// des autres pour rester cohérentes sans exposer 9 sélecteurs à l'utilisateur.
namespace Col {
    inline juce::Colour bg      { 0xFF030A0A };
    inline juce::Colour panel   { 0xFF070F0E };
    inline juce::Colour grid    { 0xFF0B3030 };
    inline juce::Colour gridCtr { 0xFF0F4040 };
    inline juce::Colour border  { 0xFF1A3535 };
    inline juce::Colour textHi  { 0xFFC8FFE8 };
    inline juce::Colour textMid { 0xFF7AB8A8 };
    inline juce::Colour accent  { 0xFF00FFB2 };
    inline juce::Colour knobDim { 0xFF0A2828 };
    inline juce::Colour midCol  { 0xFF00FFB2 };
    inline juce::Colour sideCol { 0xFFB266FF };
    // Dégradé du spectrogramme 3D (creux → milieu → pic) — entièrement
    // indépendant de Col::accent (qui reste l'accent général de l'interface).
    inline juce::Colour spectroLow  { 0xFF030A0F };
    inline juce::Colour spectroMid  { 0xFFB0B0B0 };
    inline juce::Colour spectroHigh { 0xFFFFFFFF };

    // Écrans « Papier » (v1.75) : en thème clair, les écrans des modules
    // passent en clair (option lightScreens) et Theme::applyCustom() remplace
    // la palette par sa version encre sur papier. generation change à chaque
    // nouvelle palette : un module qui garde un fond en cache le refait
    // quand elle a bougé.
    inline bool paper      { false };
    inline int  generation { 0 };
    // couleur vive pensée pour un fond noir → version lisible sur papier
    // (inchangée sur écran sombre)
    juce::Colour ink(juce::Colour c);
    // lumière (blanc) sur écran sombre → encre (texte foncé) sur papier
    inline juce::Colour light(float alpha) { return (paper ? textHi : juce::Colours::white).withAlpha(alpha); }
    // ombre portée sous un texte ou un trait : noire sur écran sombre, halo blanc sur papier
    inline juce::Colour shade(float alpha) { return (paper ? juce::Colours::white : juce::Colours::black).withAlpha(alpha); }
    // couleur propre à chaque fond (fonds, creux, pistes des écrans)
    inline juce::Colour pick(juce::Colour onDark, juce::Colour onPaper) { return paper ? onPaper : onDark; }
    // trace plus forte quand le signal monte : vers le blanc sur écran sombre,
    // vers l'encre sur papier
    // dégradé de niveau des spectrogrammes sur papier (bas pâle → haut rouge,
    // comme le spectre), à la place d'inferno qui part du noir
    juce::Colour paperHeat(float t);
    inline juce::Colour hot(juce::Colour c, float k) { return paper ? c.interpolatedWith(textHi, k*0.45f) : c.interpolatedWith(juce::Colours::white, k); }
}

// ── Thème (couleurs) ─────────────────────────────────────────────────────────
namespace Theme {
    // Palettes rapides : accent + grille assortie (le reste — gridCtr/border/
    // panel/textMid/knobDim — est dérivé automatiquement par applyCustom()).
    struct Palette { uint32_t accent; uint32_t grid; const char* name; };
    static constexpr int NUM_PALETTES = 5;
    extern const Palette palettes[NUM_PALETTES];
    // Menu couleurs entièrement personnalisable : un rôle par élément clé,
    // les teintes dérivées (gridCtr/border/panel/textMid/knobDim) sont
    // recalculées à partir d'eux pour garder l'interface cohérente. Les
    // palettes rapides passent aussi par cette même fonction.
    struct CustomColors { uint32_t accent, grid, bg, text, side, spectroLow, spectroMid, spectroHigh; };
    void applyCustom(const CustomColors&);
}

// ── Châssis neumorphique ─────────────────────────────────────────────────────
// Couleurs du "corps" de l'appli (plaque, cartes, barre de titre, barre de
// contrôles) — les écrans des modules gardent les couleurs Col::. Fixées par
// Neu::apply() selon le template (sombre ou clair).
namespace Neu {
    inline bool         light       { false };
    inline bool         highContrast{ false };        // reliefs, contours et textes renforcés
    inline juce::Colour surface     { 0xFF262A30 };   // plaque et cartes
    inline juce::Colour frame       { 0xFF1D2025 };   // pourtour sans décor
    inline juce::Colour well        { 0xFF181B1F };   // creux autour des écrans
    inline juce::Colour bezel       { 0xFF111417 };   // lunette sombre autour des écrans (clair comme sombre)
    inline juce::Colour shadowDark  { 0xA0000000 };
    inline juce::Colour shadowLight { 0x14FFFFFF };
    inline juce::Colour text        { 0xFFD9DFE7 };
    inline juce::Colour textDim     { 0xFF8B95A3 };
    // slate : thème sombre en ardoise (design de Sunny, v1.73), sinon gris d'origine
    // paperScreens : écrans des modules en clair quand le thème est clair (Col::paper)
    void apply(bool lightTint, bool contrast, bool slate=false, bool paperScreens=false);
    juce::Colour accentInk();   // accent lisible sur le châssis (assombri en clair)
    juce::ColourGradient plateFace(juce::Rectangle<float>);   // face de la plaque, en diagonale

    // Relief : carte en saillie (deux ombres) ou creux (ombres internes).
    void drawRaised(juce::Graphics&, juce::Rectangle<float>, float radius, float depth);
    void drawInset (juce::Graphics&, juce::Rectangle<float>, float radius, float depth);

    // Écrans des modules « à lunette » (v1.55) : coins de 12 px dans un cadre
    // sombre de 4 px — en mode clair, l'écran noir n'a plus l'air collé sur le gris
    inline constexpr float screenRadius = 12.f;
    inline constexpr float bezelW       = 4.f;
    void drawBezel(juce::Graphics&, juce::Rectangle<float> screen);        // cadre, sous le module
    void drawScreenGlass(juce::Graphics&, juce::Rectangle<float> screen);  // coins + bord de la vitre, au-dessus
}

// Lisibilité : facteur appliqué aux tailles de texte de l'interface
// (interrupteur "Aa" en haut à gauche — lecture à distance).
namespace Ui {
    inline float textScale { 1.0f };
    constexpr float kLargeTextScale = 1.3f;
    // repli de police selon la langue (Lang::withScriptFallback : japonais, chinois, coréen, russe)
    inline juce::Font iceland(float h) { return juce::Font(Lang::withScriptFallback(juce::FontOptions().withName("Iceland").withHeight(h*textScale))); }
    inline juce::Font plain  (float h) { return juce::Font(Lang::withScriptFallback(juce::FontOptions(h*textScale))); }
    // Taille de l'interface : l'éditeur est conçu pour un écran de 1080 lignes
    // (logiques) ; en Auto, au-delà (4K à 100 %, 1440p…) tout est agrandi
    // d'autant, par quarts entiers (1152 lignes : ×1, 1440 : ×1,25, 2160 : ×2),
    // au plus ×3. Réglage 100..200 : pourcentage fixe.
    inline float uiScaleFor(int setting, const juce::Displays::Display* d)
    {
        if(setting>0) return (float)setting/100.f;
        if(d==nullptr) return 1.f;
        return juce::jlimit(1.f, 3.f, std::floor(d->logicalBounds.getHeight()/1080.f*4.f)/4.f);
    }
    // taille déjà mise à l'échelle par l'appelant (ex. texte ajusté à une largeur)
    inline juce::Font icelandAbs(float h) { return juce::Font(Lang::withScriptFallback(juce::FontOptions().withName("Iceland").withHeight(h))); }

    // Bande de titre des modules : un glisser qui y commence détache le module
    // (appli) et ne fait jamais tourner / basculer les vues 3D
    inline float titleStripHeight() { return 22.f*textScale; }
    // Titre d'un module (deux styles à l'essai, Options › Affichage) : 1 filet
    // d'accent qui s'efface vers la droite, 2 bandeau fermé par une ligne. La
    // ligne tient dans la place du titre, les mesures ne bougent pas : à 19 px,
    // sous le texte et au-dessus du contenu (22 px) ; plus bas si un bouton descend.
    inline int titleStyle { 1 };
    inline constexpr float kTitleLineY = 19.f;
    // endX : fin de la ligne si elle doit s'arrêter avant un obstacle (0 = bord)
    void drawTitleDecor(juce::Graphics&, float width, float lineY=kTitleLineY, float endX=0.f);
    // fin de la ligne avant l'anneau d'un vectorscope, s'il monte jusque-là
    float titleEndBeforeRing(float cx, float cy, float r);
    // Corrélation des vectorscopes (à l'essai, Options › Affichage) : 0 barre
    // sous le cercle, 1 curseur sur le demi-cercle bas du cerclage, 2 idem gradué
    inline int corrStyle { 1 };
    void drawCorrRing(juce::Graphics&, float cx, float cy, float r, float corr, float height);

    // Gros texte (Aa) : une ligne dont la police rétrécit juste assez pour tenir
    // dans sa largeur — jamais de « … », les mesures restent entières
    inline juce::Font fitFont(juce::Font f, const juce::String& t, float maxW, float minH=6.f)
    {
        const float w=juce::GlyphArrangement::getStringWidth(f, t);
        return (w>maxW && w>0.f) ? f.withHeight(juce::jmax(minH, f.getHeight()*maxW/w)) : f;
    }
    inline void drawFit(juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, juce::Font f)
    {
        g.setFont(fitFont(f, t, r.getWidth()));
        g.drawText(t, r, j, false);
    }
    // titre de module à côté de boutons : s'il devait trop rétrécir, le premier
    // mot ("SPECTROGRAM") saute — "GPU - SURFACE" plutôt qu'un titre minuscule
    inline void drawTitleFit(juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Font f)
    {
        juce::String s=t;
        if(fitFont(f, s, r.getWidth()).getHeight()<f.getHeight()*0.8f && t.containsChar(' '))
            s=t.fromFirstOccurrenceOf(" ", false, false);
        drawFit(g, s, r, juce::Justification::left, f);
    }
    // libellés d'axe centrés sur x, sur une rangée, sans chevauchement : les
    // prioritaires d'abord (100, 1k, 10k…), les autres là où il reste de la place
    struct AxisLabel { float x; juce::String text; bool priority; };
    void drawAxisLabels(juce::Graphics&, const std::vector<AxisLabel>&, float y, float h, juce::Range<float> clipX, float gap=4.f);
}

// Lecture au survol des spectrogrammes : bulle fréquence / amplitude / instant
// placée près du point survolé, sans sortir de la zone.
namespace SpectroReadout {
    void draw(juce::Graphics&, juce::Rectangle<float> area, juce::Point<float> at,
              float hz, float db, float ageSec);
    // même bulle, trois lignes libres (deux grandes, une petite)
    void drawLines(juce::Graphics&, juce::Rectangle<float> area, juce::Point<float> at,
                   const juce::String& line1, const juce::String& line2, const juce::String& line3);
    juce::String formatHz(float hz);

    // Repères des spectrogrammes (CPU et GPU)
    // échelle de temps : pas lisible selon la durée affichée, libellé "0 s" / "-1.5 s"
    float timeTickStep(float spanSec);
    juce::String formatTimeTick(float sec, float step);
    // fréquences sous le relief, sans chevauchement : les décades (100, 1k,
    // 10k…) d'abord, les autres seulement s'il reste de la place.
    // posAt(hz, at) donne le haut-centre du libellé (false = hors champ).
    void drawFreqMarks(juce::Graphics&, float topHz, std::function<bool(float, juce::Point<float>&)> posAt,
                       float labelH, juce::Rectangle<float> clip);
    // texte lisible sur un relief clair ou sombre (ombre portée discrète)
    void drawShadowedText(juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, juce::Colour);
}

//==============================================================================
// Spec3D — style "Blocks 3D" du module Spectrum (interrupteur cube en haut à
// gauche) : chaque bande devient un bloc 3D — face avant, flanc, dessus — en
// gardant le dégradé de couleur calé sur l'échelle des dB (comme le Classic).
//==============================================================================
namespace Spec3D {
    float depthFor(float slotW);   // profondeur du bloc selon la largeur de bande
    // dégradé de niveau (rouge en haut -> turquoise en bas), calé sur le tracé
    juce::ColourGradient levelGradient(float plotTop, float plotBottom);
    // dalle au pied d'une bande
    void drawSocle(juce::Graphics&, juce::Rectangle<float> slot);
    // bloc d'une bande : slot = colonne entière, top = y du niveau ; front et
    // side = dégradé de niveau (side décalé de la profondeur) ; peakY < 0 si
    // pas de crête
    void drawBlock(juce::Graphics&, juce::Rectangle<float> slot, float top, juce::Colour topCol,
                   const juce::ColourGradient& front, const juce::ColourGradient& side,
                   float peakY, juce::Colour peakCol);
}

//==============================================================================
// NeuAnimButton — bouton neumorphique animé de la barre de contrôles :
// passage en relief / enfoncé avec un petit rebond, onde à l'endroit du clic,
// lueur d'accent et barre lumineuse qui s'ouvre sous le texte quand il est actif.
//==============================================================================
class NeuAnimButton : public juce::TextButton, private juce::Timer
{
public:
    using juce::TextButton::TextButton;
    ~NeuAnimButton() override { stopTimer(); }
    void paintButton(juce::Graphics&, bool over, bool down) override;
    void mouseDown(const juce::MouseEvent&) override;
    // flèche (1 = haut, -1 = bas) dessinée à la place du texte ; marge de relief
    // réduite pour les petits boutons empilés
    void setArrow(int dir) { arrowDir=dir; repaint(); }
    // marge plus large pour un bouton isolé : tout le relief tient dans les bounds
    // (sinon l'ombre est coupée en carré), seul le bouton lui-même reste cliquable
    void setReliefMargin(float m) { reliefMargin=m; repaint(); }
    bool hitTest(int x, int y) override { return reliefMargin<0.f || getLocalBounds().toFloat().reduced(reliefMargin).contains((float)x, (float)y); }
private:
    int   arrowDir { 0 };
    float reliefMargin { -1.f };   // < 0 : marge standard
    void timerCallback() override;
    void kick() { if(!isTimerRunning()) startTimerHz(60); }
    float press { 0.f }, pressVel { 0.f };   // 0 = en relief, 1 = enfoncé (ressort)
    float hover { 0.f }, glow { 0.f };       // survol, état actif
    float ripple { -1.f };                   // progression de l'onde (-1 = aucune)
    juce::Point<float> ripplePos;
    bool  initialised { false };
};

//==============================================================================
// KebabButton — trois points verticaux (menu d'options), repris de Sevenpack
// Alpha (AlphaKebab) : pastilles à l'accent avec halo, plus vif au survol.
//==============================================================================
class KebabButton : public juce::Button
{
public:
    KebabButton() : juce::Button("Options") {}
    void paintButton(juce::Graphics&, bool over, bool down) override;
};

//==============================================================================
// PopupFocus — clavier laissé à Ableton : Merlin n'est pas au premier plan et
// JUCE referme un menu dès que la souris en sort. Juste avant d'ouvrir un menu,
// prepare() fait passer Merlin au premier plan (branché par la fenêtre,
// Main.cpp, qui rend la main à la fenêtre d'avant une fois le menu refermé).
//==============================================================================
struct PopupFocus
{
    static std::function<void()>& beforePopup() { static std::function<void()> f; return f; }
    static void prepare() { auto& f=beforePopup(); if(f) f(); }
};

// Liste déroulante qui prévient PopupFocus avant d'ouvrir son menu
class FocusComboBox : public juce::ComboBox
{
public:
    using juce::ComboBox::ComboBox;
    void showPopup() override { PopupFocus::prepare(); juce::ComboBox::showPopup(); }
};

//==============================================================================
// FullScreenButton — entrer / sortir du plein écran (à côté de PRESET) : quatre
// équerres à l'accent avec halo (même famille que KebabButton), tournées vers
// l'extérieur en fenêtré, vers l'intérieur en plein écran ; elles s'écartent
// (ou se resserrent) en douceur au survol.
//==============================================================================
class FullScreenButton : public juce::Button, private juce::Timer
{
public:
    FullScreenButton() : juce::Button("Full screen") { setTooltip(TR("Full screen (F11)")); }
    ~FullScreenButton() override { stopTimer(); }
    void setFullScreen(bool b) { if(b!=full){ full=b; repaint(); } }
    void paintButton(juce::Graphics&, bool over, bool down) override;
private:
    void timerCallback() override;
    float hoverK { 0.f };
    bool  full { false }, overNow { false };
};

//==============================================================================
// KeyboardButton — allumé : Merlin prend le clavier (F11, Échap, saisie) ;
// éteint (défaut) : un clic dans Merlin laisse le clavier à l'application
// active, ex. Ableton joué au clavier du PC. Même famille que FullScreenButton
// (tracé à l'accent avec halo), estompé quand il est éteint.
//==============================================================================
class KeyboardButton : public juce::Button, private juce::Timer
{
public:
    KeyboardButton() : juce::Button("Keyboard") { setClickingTogglesState(true); }
    ~KeyboardButton() override { stopTimer(); }
    void paintButton(juce::Graphics&, bool over, bool down) override;
private:
    void timerCallback() override;
    float hoverK { 0.f };
    bool  overNow { false };
};

//==============================================================================
// PillToggle — interrupteur à pilule animé repris de Sevenpack Alpha
// (AlphaPillToggle) : la pastille glisse avec un léger dépassement, le fond
// change de couleur en fondu, l'icône tourne en entrant.
//   Moon     : on = nuit (thème sombre, lune) / off = jour (soleil)
//   Contrast : on = contraste renforcé (demi-disque)
//   Cube     : on = modules en 3D (cube isométrique)
//==============================================================================
class PillToggle : public juce::Button, private juce::Timer
{
public:
    enum class Icon { Moon, Contrast, Cube, Text, Meter, Pin, Keyboard };   // Pin : toujours au premier plan ; Keyboard : clavier à Merlin
    explicit PillToggle(Icon i) : juce::Button("pill"), icon(i) { setClickingTogglesState(true); }
    ~PillToggle() override { stopTimer(); }
    void paintButton(juce::Graphics&, bool, bool down) override;

private:
    struct Tween
    {
        float from = 0.0f, to = 0.0f;
        double startMs = 0.0, durMs = 1.0;
        float valueAt(double now, bool backEase) const noexcept;
        bool done(double now) const noexcept { return now - startMs >= durMs; }
    };
    void syncAnimationTargets(bool down);
    void timerCallback() override;

    // Glyphe avec la transformation de l'animation : opacité, échelle
    // (minScale -> 1), rotation (rotDeg -> 0) et décalage (dx -> 0).
    template <typename DrawFn>
    static void drawAnimatedGlyph(juce::Graphics& g, juce::Point<float> centre, float size, float progress,
                                  float dx, float rotDeg, float minScale, DrawFn&& draw)
    {
        const float opacity = juce::jlimit(0.0f, 1.0f, progress);
        if (opacity <= 0.01f) return;
        const float inv = 1.0f - progress;
        const float scale = minScale + (1.0f - minScale) * juce::jlimit(0.0f, 1.2f, progress);
        g.saveState();
        g.setOpacity(opacity);
        g.addTransform(juce::AffineTransform::rotation(juce::degreesToRadians(rotDeg * inv), centre.x, centre.y)
                           .scaled(scale, scale, centre.x, centre.y)
                           .translated(dx * inv, 0.0f));
        draw(g, juce::Rectangle<float>(size, size).withCentre(centre));
        g.restoreState();
    }

    Icon icon;
    bool initialised = false, animatedOn = false, pressedTarget = false;
    float knobPos = 0.0f, colourMix = 0.0f, iconProgress = 0.0f, pressT = 0.0f;
    Tween knobTween, colourTween, iconTween;
};

void draw3DButtonL(juce::Graphics&,juce::Rectangle<float>,bool,bool,juce::Colour,juce::Colour);

//==============================================================================
class LeakLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LeakLookAndFeel();
    void drawButtonBackground(juce::Graphics&,juce::Button&,const juce::Colour&,bool,bool) override;
    void drawButtonText      (juce::Graphics&,juce::TextButton&,bool,bool) override;
    void drawComboBox        (juce::Graphics&,int,int,bool,int,int,int,int,juce::ComboBox&) override;
    void drawPopupMenuBackground(juce::Graphics&,int,int) override;
    void drawRotarySlider    (juce::Graphics&,int,int,int,int,float,float,float,juce::Slider&) override;
};

// LookAndFeel du châssis (barre de titre, barre de contrôles) : boutons en
// pilule saillante / enfoncée quand actifs, champs creusés, knobs en relief.
// Les écrans des modules gardent LeakLookAndFeel.
class NeuLookAndFeel : public LeakLookAndFeel
{
public:
    void drawButtonBackground(juce::Graphics&,juce::Button&,const juce::Colour&,bool,bool) override;
    void drawButtonText      (juce::Graphics&,juce::TextButton&,bool,bool) override;
    void drawComboBox        (juce::Graphics&,int,int,bool,int,int,int,int,juce::ComboBox&) override;
    void drawComboBoxTextWhenNothingSelected(juce::Graphics&,juce::ComboBox&,juce::Label&) override;
    void drawLabel           (juce::Graphics&,juce::Label&) override;
    void drawRotarySlider    (juce::Graphics&,int,int,int,int,float,float,float,juce::Slider&) override;
};

// Barre de fenêtre Merlin (Windows, à la place de la barre du système) : fine,
// couleur de la plaque, α et boutons discrets. Déplacement, aimantation aux bords
// et redimensionnement restent ceux de Windows (JUCE répond à WM_NCHITTEST par
// findControlAtPoint). Ailleurs, barre du système.
class WindowBarLook : public juce::LookAndFeel_V4
{
public:
    void install(juce::DocumentWindow&);   // à la construction ; setLookAndFeel(nullptr) dans le destructeur
    void drawDocumentWindowTitleBar(juce::DocumentWindow&,juce::Graphics&,int,int,int,int,const juce::Image*,bool) override;
    juce::Button* createDocumentWindowButton(int) override;
    void positionDocumentWindowButtons(juce::DocumentWindow&,int,int,int,int,juce::Button*,juce::Button*,juce::Button*,bool) override;
    void fillResizableWindowBackground(juce::Graphics&,int,int,const juce::BorderSize<int>&,juce::ResizableWindow&) override;
    void drawResizableWindowBorder(juce::Graphics&,int,int,const juce::BorderSize<int>&,juce::ResizableWindow&) override {}
};

// Menu clic droit des slots (design A "liste sectionnée", preview validée) :
// fond sombre arrondi, titres de famille, icône du module, badge GPU / 3D
// (passé dans le texte de raccourci de l'item), module actif coché,
// "Vider le slot" en rouge doux.
class SlotMenuLookAndFeel : public LeakLookAndFeel
{
public:
    SlotMenuLookAndFeel();
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                   int& idealWidth, int& idealHeight) override;
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSize() override { return 4; }

    static constexpr juce::uint32 kBg = 0xF21D2126, kLine = 0xFF2C333B, kHover = 0xFF26303A,
                                  kText = 0xFFD9DFE7, kHeader = 0xFF6F8A8A, kDanger = 0xFFFF8A7A;
};

//==============================================================================
// VectorscopeDisplay
//==============================================================================
class VectorscopeDisplay : public juce::Component, private juce::Timer
{
public:
    explicit VectorscopeDisplay(LeakAudioProcessor&);
    ~VectorscopeDisplay() override { stopTimer(); }
    void paint  (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void setFrameRateHz(int hz) { if(hz<=0){ stopTimer(); return; } startTimerHz(hz); }
    void syncFromProcessor();
private:
    void timerCallback() override;
    void renderTrail();
    LeakAudioProcessor& processor;
    static constexpr int VPTS = LeakAudioProcessor::VECTOR_EXPORT;
    LeakAudioProcessor::VectorPoint pts[VPTS];
    int ptCount { 0 };
    float corr { 0.f };
    juce::Image trailImage;
    // fond fixe en cache (dégradé, cercles, axes, repères)
    juce::Image bgCache;
    juce::int64 bgKey { 0 };
    void paintBackground(juce::Graphics&, float cx, float cy, float r);
    void paintForeground(juce::Graphics&, float cx, float cy, float r);
    bool trailValid { false };
    bool lineMode   { true };
    juce::TextButton lineBtn { "LINE" };
    float releaseAlpha { 0.72f };
    // Optimisation CPU/GPU : suspend le rendu du trail une fois le silence bien établi
    int silentFrames { 0 };
    static constexpr int SILENT_FRAMES_MAX = 480; // ~8s à 60Hz
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VectorscopeDisplay)
};

//==============================================================================
// MultibandVectorscopeDisplay — 3 mini-goniomètres (grave/médium/aigu) côte
// à côte, chacun avec son propre trail phosphore. Complète le Vectorscope
// classique (image stéréo instantanée globale) en montrant comment la
// largeur stéréo varie selon la plage de fréquence.
//==============================================================================
class MultibandVectorscopeDisplay : public juce::Component, private juce::Timer
{
public:
    explicit MultibandVectorscopeDisplay(LeakAudioProcessor&);
    ~MultibandVectorscopeDisplay() override { stopTimer(); }
    void paint  (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void setFrameRateHz(int hz) { if(hz<=0){ stopTimer(); return; } startTimerHz(hz); }
private:
    void timerCallback() override;
    void renderTrail(int band);
    void getBandCircle(int band, float& cx, float& cy, float& r) const;
    LeakAudioProcessor& processor;
    static constexpr int MPTS = LeakAudioProcessor::MB_VECTOR_EXPORT;
    LeakAudioProcessor::VectorPoint pts[3][MPTS];
    int ptCount[3] {};
    juce::Image trailImage[3];
    bool trailValid[3] {};
    float releaseAlpha { 0.90f };
    int silentFrames[3] {};
    static constexpr int SILENT_FRAMES_MAX = 480; // ~8s à 60Hz
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultibandVectorscopeDisplay)
};

//==============================================================================
// ReleaseFader — fader vertical compact intégré au bord gauche d'un module ;
// pilote le RELEASE (temps de lissage/décroissance) local à CE module,
// indépendamment des autres — remplace l'ancien RELEASE global de la barre
// de contrôles.
//==============================================================================
constexpr float kReleaseFaderW = 26.f;

class ReleaseFader : public juce::Component
{
public:
    ReleaseFader() { range.setSkewForCentre(1.0f); }
    void paint(juce::Graphics&) override;
    void resized() override {}
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override { dragging=false; repaint(); }
    void mouseEnter(const juce::MouseEvent&) override { hovered=true;   repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovered=false;  repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { setValue(getDefault ? getDefault() : defaultValue); }
    std::function<float()> getDefault;   // valeur du double-clic calculée à la volée (ex. 25 ms au taux courant)
    void setValue(float v, juce::NotificationType nt = juce::sendNotification)
    {
        v = range.snapToLegalValue(v);
        if(std::abs(v-value)<0.0001f) return;
        value=v; repaint();
        if(nt!=juce::dontSendNotification && onValueChange) onValueChange(value);
    }
    float getValue() const { return value; }
    std::function<void(float)> onValueChange;
    std::function<juce::String(float)> formatValue;   // affichage de la valeur (défaut : secondes, "1.0s")
    void setHorizontal(bool h) { horizontal=h; repaint(); }   // rail horizontal : étiquette à gauche, valeur à droite
    // autre usage que le RELEASE (ex. TIME des spectrogrammes) : étiquette,
    // plage (milieu de course = centre), valeur du double-clic
    void configure(const juce::String& newLabel, float lo, float hi, float centre, float def)
    {
        label=newLabel;
        range=juce::NormalisableRange<float>(lo, hi);
        range.setSkewForCentre(centre);
        defaultValue=def;
        value=range.snapToLegalValue(value);
        repaint();
    }
private:
    void setFromPos(juce::Point<int> p);
    juce::String valueText() const { return formatValue ? formatValue(value) : juce::String(value,1)+"s"; }
    juce::NormalisableRange<float> range { 0.1f, 5.0f };
    float value        { 0.4f };
    float defaultValue { 0.4f };
    juce::String label { "REL" };
    bool  hovered { false }, dragging { false }, horizontal { false };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReleaseFader)
};

//==============================================================================
// Spectrogram3DBase — commun aux deux spectrogrammes "courbes 3D en
// perspective" (style C validé sur preview) : une courbe de spectre capturée
// régulièrement, empilée en profondeur (la plus récente au fond). Source :
// la FFT 4096 points du processeur (PSD), ramenée à 160 points répartis en
// log de 20 Hz à 20 kHz — courbes fines, détaillées et fluides. Fader TIME à
// gauche : durée de défilement (fond -> avant). Repeint à chaque nouvelle courbe.
//==============================================================================
class Spectrogram3DBase : public juce::Component, protected juce::Timer
{
public:
    // reverseFlag/tiltRef/skewRef : références aux champs persistés du
    // processor propres à CE module (sens de défilement et perspective).
    Spectrogram3DBase(LeakAudioProcessor&, bool& reverseFlag, float& tiltRef, float& skewRef, float& timeRef);
    ~Spectrogram3DBase() override { stopTimer(); }
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent& e) override { hoverPos=e.position; hovering=true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override   { hovering=false; repaint(); }
    void setFrameRateHz(int hz) { if(hz<=0){ stopTimer(); return; } startTimerHz(hz); }
    // preset rappelé : fader TIME relu (sens, perspective : références directes)
    void syncFromProcessor() { timeFader.setValue(timeSec, juce::dontSendNotification); repaint(); }
protected:
    static constexpr int    NPTS = 160;            // points par courbe (log 20 Hz–20 kHz)
    static constexpr int    NROWS = 96;            // courbes en mémoire (64 avant : historique jugé court)
    static constexpr double CAPTURE_MS = 33.0;     // une courbe toutes les ~33 ms
    static constexpr int    SILENT_FRAMES_MAX = 240;

    void timerCallback() override;
    void drawDirectionButton(juce::Graphics&);
    // filled=false : lignes (fond masquant + trait coloré par ancienneté) ;
    // filled=true  : surfaces remplies d'un dégradé selon l'amplitude
    void paintCurves(juce::Graphics&, const char* title, bool filled);
    static juce::Colour inferno(float t);   // palette "inferno" (violet sombre -> jaune pâle)
    void rebuildBinMap(double sampleRate);

    LeakAudioProcessor& processor;
    bool& reverseScroll;
    float& tiltAmount;
    float& skewAmount;
    float& timeSec;             // TIME : durée de défilement fond -> avant (s)
    ReleaseFader timeFader;      // fader TIME à gauche (même style que REL)
    double captureMs() const { return juce::jmax(1.0, (double)timeSec)*1000.0/(double)(NROWS-1); }
    // haut de l'axe de fréquence de la correspondance en cours (20 kHz ou Nyquist)
    float axisTopHz() const { return PsdLogMap::topHz(mappedSampleRate>0.0 ? mappedSampleRate : 48000.0); }
    float tiltAtDragStart { 0.3f };
    bool  dragFromTitle { false };   // glisser parti du titre : pas de perspective (détachement du module)
    float skewAtDragStart { 0.f };
    juce::Rectangle<float> dirBtnBounds;
    juce::Point<float> hoverPos;   // lecture fréquence/amplitude au survol
    bool hovering { false };
    NeuAnimButton holdBtn { "HOLD" };   // fige l'image (plus de nouvelles courbes)
    bool frozen { false };

    float  rows[NROWS][NPTS] {};   // anneau de courbes, valeurs 0..1
    int    head { 0 }, rowCount { 0 };
    juce::uint32 captureTotal { 0 };   // courbes capturées depuis l'ouverture (lignes du plateau liées aux courbes)
    double lastCaptureMs { 0.0 };
    float  glideFrac { 0.f };   // avance (0..1) vers la courbe suivante : défilement continu
    int    silentFrames { 0 };
    // correspondance points log -> bins FFT (refaite si la fréquence change)
    int    binLo[NPTS] {}, binHi[NPTS] {};
    float  binFrac[NPTS] {};
    double mappedSampleRate { 0.0 };
    float  psd[LeakAudioProcessor::PSD_NUM_BINS] {};
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Spectrogram3DBase)
};

//==============================================================================
// Spectrogram3DPeaksDisplay — courbes en lignes : chaque courbe masque celles
// de derrière, trait coloré par ancienneté (palette inferno).
//==============================================================================
class Spectrogram3DPeaksDisplay : public Spectrogram3DBase
{
public:
    Spectrogram3DPeaksDisplay(LeakAudioProcessor& p, bool& reverseFlag, float& tiltRef, float& skewRef, float& timeRef)
        : Spectrogram3DBase(p,reverseFlag,tiltRef,skewRef,timeRef) {}
    void paint(juce::Graphics&) override;
};

//==============================================================================
// Spectrogram3DSurfaceDisplay — mêmes courbes, remplies d'un dégradé selon
// l'amplitude, plus transparentes au fond.
//==============================================================================
class Spectrogram3DSurfaceDisplay : public Spectrogram3DBase
{
public:
    Spectrogram3DSurfaceDisplay(LeakAudioProcessor& p, bool& reverseFlag, float& tiltRef, float& skewRef, float& timeRef)
        : Spectrogram3DBase(p,reverseFlag,tiltRef,skewRef,timeRef) {}
    void paint(juce::Graphics&) override;
};

//==============================================================================
// VuMeterLeak
//==============================================================================
class VuMeterLeak : public juce::Component
{
public:
    explicit VuMeterLeak(LeakAudioProcessor&);
    void update();
    void paint  (juce::Graphics&) override;
    void resized() override {}
    void mouseDown(const juce::MouseEvent& e) override;
    bool isClipping() const { return clipHoldL>0.f || clipHoldR>0.f; }
    void resetAll();
    // Design de la section master : false = A "Instrument", true = C "Console mastering"
    void setConsoleStyle(bool b) { if(b!=consoleStyle){ consoleStyle=b; repaint(); } }
private:
    float dbToY(float db, float gy0, float gh) const;
    LeakAudioProcessor& processor;
    float dispPeakL{-120.f},dispPeakR{-120.f};
    float dispRmsL {-120.f},dispRmsR {-120.f};
    float holdL    {-120.f},holdR    {-120.f};
    int   holdFrL  {0},     holdFrR  {0};
    float dispLRA  {0.f};
    float dispIntegrated {-120.f};
    float clipHoldL {0.f}, clipHoldR {0.f};
    float dispTPL   {-120.f}, dispTPR {-120.f};
    float numPeakL  {-120.f}, numPeakR {-120.f};
    float numRmsL   {-120.f}, numRmsR  {-120.f};
    // Dirty-check : évite tout repaint quand rien n'a visiblement bougé
    // (silence prolongé, valeurs déjà stabilisées) — optimisation CPU/GPU.
    float lastPaintedPeakL{-999.f}, lastPaintedPeakR{-999.f};
    float lastPaintedRmsL {-999.f}, lastPaintedRmsR {-999.f};
    float lastPaintedHoldL{-999.f}, lastPaintedHoldR{-999.f};
    bool  lastPaintedClipL{false},  lastPaintedClipR{false};
    // Pastilles TP (design A validé) : true peak max en dBTP depuis la remise à
    // zéro, rouges après un dépassement > 0 dBTP ; un clic sur la zone des
    // voyants les remet à zéro (avec CLIP).
    bool  tpLatchL{false}, tpLatchR{false};
    float tpMaxL{-120.f}, tpMaxR{-120.f};
    bool  lastPaintedTpL{false}, lastPaintedTpR{false};
    float lastPaintedTpMaxL{-999.f}, lastPaintedTpMaxR{-999.f};
    static constexpr float PAD_T = 34.f;   // pastilles TP au-dessus des barres
    bool  consoleStyle { false };
    static constexpr float NUM_RC = 0.003f;
    static constexpr float DB_MIN=-60.f, DB_MAX=6.f;
    static constexpr float RC=0.04f;
    static constexpr float HOLD_SEC=2.f, DT=1.f/60.f;
    static constexpr int   HOLD_FR=120;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VuMeterLeak)
};

//==============================================================================
// SpectrumDisplayLeak
//==============================================================================
class SpectrumDisplayLeak : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumDisplayLeak(LeakAudioProcessor&);
    ~SpectrumDisplayLeak() override;
    void paint  (juce::Graphics&) override;
    void resized() override;

    void setPeakHoldEnabled(bool b)  { peakOn=b; if(!b)resetPeaks(); }
    void setPeakHoldSec    (float s) { peakHoldSec=s; }
    void setReleaseSec     (float s);
    // Recharge le fader RELEASE local depuis l'état persisté du processor
    // (appelé après ouverture d'un projet/preset).
    void syncReleaseFromProcessor() { relFader.setValue(processor.spectrumReleaseSec,juce::dontSendNotification); setReleaseSec(processor.spectrumReleaseSec); }
    void setDbRange        (float r) { dbRange=r; resetPeaks(); invalidateCache(); }
    void setActiveBands    (int n);
    void setAnalysisMode   (LeakAudioProcessor::AnalysisMode m){ analysisMode=m; resetPeaks(); }
    void resetAll();
    void setFrameRateHz(int hz) { if(hz<=0){ stopTimer(); return; } currentHz=hz; startTimerHz(hz); setReleaseSec(releaseSec); }
    int  getActiveBands()   const { return activeBands; }
    bool isGlobalClipping() const { return globalClipLatch; }
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp  (const juce::MouseEvent&) override;
    // Efface la sélection de bandes locale (appelé quand un AUTRE module
    // efface celle des autres — un seul module sélectionné à la fois).
    void clearBandSelection() { multiSelectMask=0; soloBand=-1; lastDragBand=-1; repaint(); }
    // Notifié quand CE module vient de modifier sa propre sélection (clic/glisser) —
    // câblé par LeakEditor pour effacer la sélection des autres modules.
    std::function<void()> onSelectionChanged;
    // Style Blocks 3D (interrupteur cube) — false = Classic
    void set3D(bool b) { if(b!=blocks3D){ blocks3D=b; repaint(); } }

private:
    static constexpr int NMAX = LeakAudioProcessor::N_BANDS;
    static const char* BAND_LABELS_15[LeakAudioProcessor::N_BANDS_15+LeakAudioProcessor::N_EXTRA_15];
    static const char* BAND_LABELS_31[LeakAudioProcessor::N_BANDS_31+LeakAudioProcessor::N_EXTRA_31];
    static const char* BAND_LABELS_62[LeakAudioProcessor::N_BANDS_62+LeakAudioProcessor::N_EXTRA_62];

    void timerCallback() override;
    void resetPeaks();
    void invalidateCache();
    void rebuildCache();

    float padL() const { return cachedPadL; }
    float padR() const { return 4.f; }
    float padT() const { return 22.f; }
    float padB() const { return cachedPadB; }   // étiquettes de fréquence (2 rangées), calculée dans rebuildCache
    float cachedPadB { 34.f };
    float freqToX(float f) const;
    float dbToY  (float db) const;
    int   xToBand(float px) const;

    ReleaseFader relFader;

    struct BandCache { float x{0},bw{0},labelX{0},labelY{0}; bool showLabel{false}; };
    BandCache barCache[NMAX];
    bool  cacheValid    { false };
    int   cacheGen      { -1 };   // Col::generation du cache (palette écrans Papier / sombres)
    bool  gridImageValid{ false };
    float cachedPadL{ 38.f }, cachedGW{ 600.f }, cachedGH{ 250.f };
    float cachedLogMin{ 1.30103f }, cachedLogMax{ 4.30103f };   // axe log (bords des bandes extrêmes)
    juce::Image gridImage;
    juce::Image barGradImage;
    juce::Font  labelFont { juce::FontOptions().withName("Iceland").withHeight(8.f) };
    float       labelFontSize { 0.f };

    LeakAudioProcessor& processor;
    int  activeBands { 31 };
    int  cacheMode   { 31 };   // résolution (15/31/62) du dernier rebuildCache : étiquettes
    LeakAudioProcessor::AnalysisMode analysisMode { LeakAudioProcessor::AnalysisMode::Stereo };

    float levels    [NMAX] {};
    float sideLevel [NMAX] {};
    float peaks     [NMAX] {};
    float sidePeaks [NMAX] {};
    float prevPeaks    [NMAX] {};  // dernière valeur repeinte — référence du dirty-check
    float prevSidePeaks[NMAX] {};
    float peakTimers    [NMAX] {};
    float sidePeakTimers[NMAX] {};
    bool  clipLatch [NMAX] {};
    int   clipHoldF [NMAX] {};
    bool  globalClipLatch { false };
    float bandBuf[NMAX] {}, sideBuf[NMAX] {};
    float prevLevels[NMAX] {}, prevSideLevel[NMAX] {};

    bool  peakOn { true };
    int   currentHz { 60 };   // fréquence réelle du timer — pilote le dt des lissages
    float peakHoldSec{2.f}, releaseSec{1.f}, dbRange{50.f};
    double lastTickMs { 0.0 };   // instant de l'image précédente (pas réel des lissages / du hold)

    int          hoveredBand    { -1 };
    int          soloBand       { -1 };
    // ── Sélection multi-bandes (Ctrl+clic / Ctrl+glisser) ──
    uint64_t     multiSelectMask{ 0 };
    int          lastDragBand   { -1 };
    float        hoverPixX      { 0.f };
    int          lastHoveredBand{ -1 };
    juce::String cachedTipStr;
    float        cachedTipW     { 0.f };
    // Rafraîchissement périodique du tooltip (les dB doivent bouger même
    // si le curseur reste immobile sur la même bande)
    int          hoverRefreshCounter { 0 };
    bool         forceTipRefresh    { false };
    static constexpr int HOVER_REFRESH_FRAMES = 30; // ~0.5s à 60Hz

    juce::RectangleList<float> barRects[5], peakRects[5];
    juce::RectangleList<float> sideRects, refRects, clipOn, clipOff;

    bool  blocks3D { false };   // style Blocks 3D (sinon Classic)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumDisplayLeak)
};

//==============================================================================
// LoudnessPanel
//==============================================================================
class LoudnessPanel : public juce::Component
{
public:
    explicit LoudnessPanel(LeakAudioProcessor& p);
    void update();
    void paint(juce::Graphics&) override;
    void resized() override;
    void resetLoudness() { processor.resetLoudness(); }
private:
    LeakAudioProcessor& processor;
    NeuAnimButton resetBtn { "RESET" };   // remise à zéro du Loudness seul (I, LRA, true peak)
    int   btnZoneH { 0 };                 // hauteur réservée au bouton en bas
    float dispM   { -120.f };
    float dispS   { -120.f };
    float dispI   { -120.f };
    float dispLRA { 0.f };
    float dispTPL { -120.f };
    float dispTPR { -120.f };
    // Dirty-check (optimisation CPU/GPU) — cf. VuMeterLeak
    float lastPaintedI{-999.f}, lastPaintedM{-999.f}, lastPaintedS{-999.f};
    float lastPaintedLRA{-999.f}, lastPaintedTPL{-999.f}, lastPaintedTPR{-999.f};
    float numI {-120.f}, numM {-120.f}, numS {-120.f};
    // TRUE PEAK L/R : maximum maintenu depuis la dernière remise à zéro
    float tpMaxL {-120.f}, tpMaxR {-120.f};
    int   seenResetCount { -1 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudnessPanel)
};

//==============================================================================
// MultibandCorrDisplay — corrélation L/R par bande fréquentielle
// Barres : cyan = en phase (+1), rouge = en opposition (-1)
//==============================================================================
class MultibandCorrDisplay : public juce::Component, private juce::Timer
{
public:
    explicit MultibandCorrDisplay(LeakAudioProcessor&);
    ~MultibandCorrDisplay() override { stopTimer(); }
    void paint  (juce::Graphics&) override;
    void resized() override;
    void setReleaseSec(float s);
    void syncReleaseFromProcessor() { relFader.setValue(processor.corrReleaseSec,juce::dontSendNotification); setReleaseSec(processor.corrReleaseSec); }
    void setFrameRateHz(int hz) { if(hz<=0){ stopTimer(); return; } currentHz=hz; startTimerHz(hz); setReleaseSec(releaseSec); }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp  (const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void clearBandSelection() { multiSelectMask=0; soloBand=-1; lastDragBand=-1; repaint(); }
    std::function<void()> onSelectionChanged;
private:
    void timerCallback() override;
    int  xToBand(float px) const;
    LeakAudioProcessor& processor;
    ReleaseFader relFader;
    int   currentHz { 60 };   // fréquence réelle du timer — pilote le dt du lissage
    static constexpr int NMAX = LeakAudioProcessor::N_BANDS;
    float rawCorr[NMAX] {};   // valeur brute lue depuis le processor
    float corr[NMAX] {};      // valeur lissée affichée (pilotée par RELEASE)
    float prevCorr[NMAX] {};  // dernière valeur repeinte — référence du dirty-check
    float releaseSec { 0.4f };
    int   soloBand    { -1 };
    // ── Sélection multi-bandes (Ctrl+clic / Ctrl+glisser) ──
    uint64_t multiSelectMask { 0 };
    int      lastDragBand    { -1 };
    int   hoveredBand { -1 };
    float hoverPixX   { 0.f };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultibandCorrDisplay)
};

//==============================================================================
// MultipanoramaDisplay — position stéréo par bande (X=fréq, Y=L→C→R)
// Trail phosphore accumulant l'historique de la scène stéréo
//==============================================================================
class MultipanoramaDisplay : public juce::Component, private juce::Timer
{
public:
    explicit MultipanoramaDisplay(LeakAudioProcessor&);
    ~MultipanoramaDisplay() override { stopTimer(); }
    void paint  (juce::Graphics&) override;
    void resized() override;
    void setReleaseSec(float s);
    void syncReleaseFromProcessor() { relFader.setValue(processor.panoReleaseSec,juce::dontSendNotification); setReleaseSec(processor.panoReleaseSec); }
    void setFrameRateHz(int hz) { if(hz<=0){ stopTimer(); return; } currentHz=hz; startTimerHz(hz); setReleaseSec(releaseSec); }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp  (const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void clearBandSelection() { multiSelectMask=0; soloBand=-1; lastDragBand=-1; repaint(); }
    std::function<void()> onSelectionChanged;
private:
    void timerCallback() override;
    void renderTrail();
    int  xToBand(float px) const;
    static juce::Colour heatColor(float amp01);
    LeakAudioProcessor& processor;
    ReleaseFader relFader;
    int   currentHz { 60 };   // fréquence réelle du timer — pilote le dt du lissage
    static constexpr int NMAX = LeakAudioProcessor::N_BANDS;
    float pan   [NMAX] {};   // -1=gauche, 0=centre, +1=droite
    float energy[NMAX] {};   // dB
    float corr  [NMAX] {};   // -1..+1 : correlation L/R (1=mono, 0=large/decorrele)
    juce::Image trailImage;
    bool  trailValid   { false };
    float releaseAlpha { 0.88f };   // décroissance phosphore par frame — pilotée par RELEASE
    float releaseSec   { 0.4f };
    int   soloBand     { -1 };
    // ── Sélection multi-bandes (Ctrl+clic / Ctrl+glisser) ──
    uint64_t multiSelectMask { 0 };
    int      lastDragBand    { -1 };
    int   hoveredBand  { -1 };
    float hoverPixX    { 0.f };
    // Optimisation : suspend le rendu du trail une fois le silence bien établi
    int   silentFrames { 0 };
    static constexpr int SILENT_FRAMES_MAX = 240; // ~4s à 60Hz
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultipanoramaDisplay)
};

//==============================================================================
// PanoramaLedDisplay — "Panorama LED" (design D validé sur preview) : une
// colonne de LED par bande, de L (haut) à R (bas). La LED de la position
// stéréo s'allume, ses voisines selon la largeur (1 - corrélation) ; l'éclat
// suit le niveau. Couleur de chaque LED selon sa place, comme le Panorama :
// turquoise au centre -> vert -> jaune -> orange -> rouge aux bords.
// Même cadre que le Panorama (rail REL, L/C/R, échelle %), même release.
//==============================================================================
class PanoramaLedDisplay : public juce::Component, private juce::Timer
{
public:
    explicit PanoramaLedDisplay(LeakAudioProcessor&);
    ~PanoramaLedDisplay() override { stopTimer(); }
    void paint  (juce::Graphics&) override;
    void resized() override;
    void setFrameRateHz(int hz) { if(hz<=0){ stopTimer(); return; } currentHz=hz; startTimerHz(hz); }
    void syncReleaseFromProcessor() { relFader.setValue(processor.panoReleaseSec,juce::dontSendNotification); }
    void mouseMove(const juce::MouseEvent& e) override { hoverX=e.position.x; hovering=true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override   { hovering=false; repaint(); }
private:
    void timerCallback() override;
    static juce::Colour ledColour(float dev);   // dev -1..+1 (écart au centre)
    LeakAudioProcessor& processor;
    ReleaseFader relFader;
    int   currentHz { 60 };
    static constexpr int NMAX = LeakAudioProcessor::N_BANDS;
    float inPan[NMAX] {}, inDb[NMAX] {}, inCorr[NMAX] {};
    float panS[NMAX] {}, levS[NMAX] {}, widS[NMAX] {}, dbS[NMAX] {};
    int   nBands { 0 };
    bool  wasActive { false };      // au moins une LED allumée à la dernière image
    juce::Image offGrid;            // LED éteintes (mise en cache par taille / résolution)
    int   offKey { 0 };
    float hoverX { 0.f };
    bool  hovering { false };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanoramaLedDisplay)
};

//==============================================================================
// SpectralDensityDisplay — PSD (densité spectrale de puissance), FFT réelle
// moyennée façon Welch (EMA ~300ms), 4096 points, recouvrement 75%.
//==============================================================================
class SpectralDensityDisplay : public juce::Component, private juce::Timer
{
public:
    explicit SpectralDensityDisplay(LeakAudioProcessor&);
    ~SpectralDensityDisplay() override { stopTimer(); }
    void paint  (juce::Graphics&) override;
    void resized() override {}
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void setFrameRateHz(int hz) { if(hz<=0){ stopTimer(); return; } startTimerHz(hz); }
private:
    void timerCallback() override;
    float freqToX(float f) const;
    float dbToY  (float db) const;
    LeakAudioProcessor& processor;
    static constexpr int NB = LeakAudioProcessor::PSD_NUM_BINS;
    float psdDb [NB] {};
    float peakDb[NB] {};   // peak-hold lent (façon enveloppe max), aide à repérer les pics fugaces
    bool  hovering   { false };
    float hoverPixX  { 0.f };
    float hoverFreq  { 0.f };
    float hoverDb    { -120.f };
    int   silentFrames { 0 };   // optimisation CPU : suspend le repaint en silence prolongé
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralDensityDisplay)
};

//==============================================================================
// NeedleVuMeter — VU-mètre analogique à aiguille (façon boîtier hardware).
// Échelle -20 → +3 non linéaire, aiguille avec ballistique VU (~300ms) ou PPM
// (attaque rapide / relâche lente) selon TYPE, calibration dBFS réglable,
// mode L/R/L+R, lectures Peak/RMS/LUFS-I et Hold en pied de module.
//==============================================================================
class NeedleVuMeter : public juce::Component, private juce::Timer
{
public:
    explicit NeedleVuMeter(LeakAudioProcessor&);
    ~NeedleVuMeter() override { stopTimer(); }
    void paint  (juce::Graphics&) override;
    void resized() override;
    void setFrameRateHz(int hz) { if(hz<=0){ stopTimer(); return; } currentHz=hz; startTimerHz(hz); }
    void syncFromProcessor();
private:
    void timerCallback() override;
    static float angleForDb(float db);   // mapping non linéaire dB -> angle (degrés)
    LeakAudioProcessor& processor;
    int   currentHz { 60 };

    float needleDb   { -20.f };   // valeur affichée par l'aiguille (après ballistique)
    float needleVel  { 0.f };     // vitesse angulaire (physique ressort-amorti)
    float dispPeakL{-120.f}, dispPeakR{-120.f};
    float dispRmsL {-120.f}, dispRmsR {-120.f};
    float numPeak{-120.f}, numRms{-120.f};
    bool  holding    { false };
    float holdPeakDb { -20.f };

    // En-tête : LR/L/R (clic = suivant), référence (0 du cadran, en dBFS) réglée
    // aux flèches ▲ / ▼ empilées, par pas de 2 ou 4 dB (-4 à -30 dBFS).
    NeuAnimButton     modeBtn   { "LR" };
    NeuAnimButton     calibDown, calibUp;   // flèches (setArrow)
    NeuAnimButton     stepBtn   { "2 dB" };
    juce::Rectangle<int> calibValueArea;   // valeur affichée entre les flèches
    void setCalibRef(float v);
    void updateHeaderTexts();
    NeuAnimButton     holdBtn { "HOLD" };   // même bouton que la barre du bas

    int   modeSel  { 0 };      // 0=LR 1=L 2=R
    float calibRef { -10.f };   // natif : -10 dBFS
    bool  ppmType  { false };

    juce::Rectangle<int> gaugeArea;
    int headerH { 24 };   // suit la taille de texte (Aa)
    juce::Image skImage;
    juce::Rectangle<int> skCrop;   // partie opaque du monogramme (centrage sur le pivot)
    juce::Image skLogo;            // = skImage limitée à skCrop
    // cadran fixe en cache + dernier état peint (repeinte seulement si ça bouge)
    juce::Image dialCache;
    juce::int64 dialKey { 0 };
    float lastPaintedAng { -999.f }, lastPaintedPeak { 0.f }, lastPaintedRms { 0.f };
    bool  lastPaintedHold { false };
    void paintDialStatic (juce::Graphics&, juce::Rectangle<float> ga, float cx, float cy, float radius);
    void paintDialDynamic(juce::Graphics&, juce::Rectangle<float> ga, float cx, float cy, float radius);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeedleVuMeter)
};

//==============================================================================
// TopBar — titre + sous-titre, logo, sélecteur de presets, bouton settings
//==============================================================================
//==============================================================================
// VolumeRing — volume de la sortie Windows dans l'en-tête. v1.73.4 : knob
// neumorphique du thème clair de Sevenpack Alpha, contraste renforcé, dans
// tous les thèmes ; « VOL » et la valeur à gauche ; glisser ou molette.
//==============================================================================
class VolumeRing : public juce::Slider
{
public:
    VolumeRing()
    {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        setRotaryParameters(juce::MathConstants<float>::pi*1.25f, juce::MathConstants<float>::pi*2.75f, true);
        setRange(0.0, 100.0, 1.0);
    }
    void paint(juce::Graphics&) override;
private:
    juce::SharedResourcePointer<AlphaKnob::Cache> knobCache;
};

//==============================================================================
// StepArrow — petite flèche ◀ / ▶ (preset précédent / suivant)
//==============================================================================
class StepArrow : public juce::Button
{
public:
    explicit StepArrow(bool pointsRight) : juce::Button(pointsRight ? "next" : "prev"), right(pointsRight)
    { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void paintButton(juce::Graphics&, bool over, bool down) override;
private:
    bool right;
};

//==============================================================================
// PresetBar (v1.73) — sélecteur de presets au centre de la bande du bas (design
// de Sunny) : flèches précédent / suivant autour du menu PRESET (presets
// d'usine, presets utilisateur, enregistrer / supprimer / dossier). Repris de
// l'en-tête, où il se trouvait avant.
//==============================================================================
class PresetBar : public juce::Component
{
public:
    PresetBar();
    void resized() override;
    void retranslate();
    void setPresetList(const juce::StringArray& userNames, const juce::String& current);
    std::function<void(int)> onPresetSelected;   // 0=Mixing 1=Mastering 2=Sound Design 3=TV
    std::function<void(const juce::String&)> onUserPresetSelected;
    std::function<void()>                    onSavePresetRequested;
    std::function<void(const juce::String&)> onDeletePresetRequested;
    std::function<void()>                    onOpenPresetFolder;
private:
    void step(int dir);   // flèches : preset suivant / précédent (usine puis utilisateur, en boucle)
    FocusComboBox     presetCombo;
    StepArrow         prevBtn { false }, nextBtn { true };
    juce::StringArray userPresets;   // noms des presets utilisateur (ids 100+)
    juce::String      shownPreset;   // preset utilisateur affiché (vide = usine ou aucun)
    int               shownId { 0 }; // élément affiché avant une action (enregistrer…)
    enum { kSavePresetId=900, kDeletePresetId, kPresetFolderId, kUserPresetBaseId=100 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBar)
};

// En-tête (v1.73, design de Sunny, hauteur d'origine) : merle, interrupteurs en
// 2×2 (thème, 3D / premier plan, clavier), logotype MERLIN α + fréquence
// d'échantillonnage dans un cadre d'accent, volume Windows, RESET, plein écran,
// options, monogramme Side Key.
class TopBar : public juce::Component, private juce::Timer
{
public:
    explicit TopBar();
    ~TopBar() override { stopTimer(); setLookAndFeel(nullptr); }
    void paint  (juce::Graphics&) override;
    void resized() override;
    // v1.74 : l'en-tête a pris 16 px à la bande du bas ; les tailles plafond
    // de ses éléments grandissent d'autant (x1 jusqu'à 52 px de haut)
    float headerScale() const { return juce::jlimit(1.f, 1.24f, (float)getHeight()/52.f); }
    // repeint seulement pendant l'effet (il s'anime) et à son extinction
    void setGlitch(bool b) { if(b || glitching){ glitching=b; repaint(); } }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if(demoBounds.contains(e.position) && onDemoClicked)
            onDemoClicked();
        else if(logoBounds.contains(e.position) && onAboutClicked)
            onAboutClicked();
        else if(titleBounds.contains(e.position) && onTitleClicked)
            onTitleClicked();
    }
    // pastille « DÉMO · X JOURS » à droite du logotype (vide = masquée)
    void setDemoBadge(const juce::String& t) { if(t!=demoText){ demoText=t; repaint(); } }
    // fréquence d'échantillonnage affichée sous le titre (0 = pas d'audio)
    void setSampleRate(double sr) { if(sr!=sampleRate){ sampleRate=sr; repaint(); } }
    std::function<void()>    onDemoClicked;
    std::function<void()>    onAboutClicked;
    std::function<void()>    onTitleClicked;
    std::function<void()>    onSettingsClicked;
    std::function<void()>    onResetClicked;     // RESET (loudness, crêtes, VU)
    std::function<void(bool)> onThemeToggled;    // true = sombre (nuit)
    std::function<void(bool)> onContrastToggled; // true = contraste renforcé
    std::function<void(bool)> onModules3DToggled; // true = modules en 3D
    std::function<void(bool)> onLargeTextToggled; // true = textes agrandis
    std::function<void(bool)> onAlwaysOnTopToggled; // true = fenêtre toujours au premier plan
    void setAlwaysOnTopState(bool b) { onTopToggle.setToggleState(b, juce::dontSendNotification); }
    std::function<void()>     onFullScreenClicked;  // bouton plein écran (à côté de PRESET)
    void setFullScreenState(bool b) { fullScreenBtn.setFullScreen(b); }
    std::function<void(bool)> onKeyboardToggled;    // true = le clavier va à Merlin
    void retranslate();   // langue changée : infobulles
    void setKeyboardState(bool b)
    {
        keyboardToggle.setToggleState(b, juce::dontSendNotification);
        keyboardToggle.setTooltip(TR(b ? "Clavier : Merlin (cliquer pour le rendre \xc3\xa0 Ableton)"
                                       : "Clavier : Ableton (cliquer pour le donner \xc3\xa0 Merlin)"));
    }
    void setLookState(bool dark, bool contrast, bool modules3D, bool largeText)
    {
        themeToggle    .setToggleState(dark,          juce::dontSendNotification);
        contrastToggle .setToggleState(contrast,      juce::dontSendNotification);
        modules3DToggle.setToggleState(modules3D,     juce::dontSendNotification);
        largeTextToggle.setToggleState(largeText,     juce::dontSendNotification);
    }
private:
    void timerCallback() override;    // suit le volume Windows changé ailleurs
    void showWindowsVolume(float v);  // v 0..1, < 0 si indisponible
    // Interrupteurs de Sevenpack, en 2×2 après le merle : thème, 3D / premier
    // plan, clavier (le style de la section master est dans la bande du bas)
    PillToggle themeToggle    { PillToggle::Icon::Moon };
    PillToggle contrastToggle { PillToggle::Icon::Contrast };
    PillToggle modules3DToggle { PillToggle::Icon::Cube };   // modules en 3D
    PillToggle largeTextToggle { PillToggle::Icon::Text };   // textes agrandis
    PillToggle onTopToggle     { PillToggle::Icon::Pin };    // toujours au premier plan
    PillToggle keyboardToggle  { PillToggle::Icon::Keyboard };   // clavier à Merlin (sinon à Ableton)
    bool          glitching { false };
    juce::Random  rng;
    juce::Image   logoImage;
    juce::Rectangle<float> logoBounds, titleBounds, demoBounds;
    juce::String     demoText;
    double           sampleRate { 0.0 };
    NeuLookAndFeel   laf;
    KebabButton      gearBtn;   // menu d'options (trois points, comme Sevenpack Alpha)
    FullScreenButton fullScreenBtn;
    NeuAnimButton    resetBtn { "RESET" };
    VolumeRing       volKnob;   // volume général de la sortie Windows (appli)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TopBar)
};

//==============================================================================
// ControlsBarLeak
//==============================================================================
// Bande du bas (v1.73, design de Sunny) : une seule rangée fixe, titres en
// ligne — PEAK HOLD, RANGE, MS à gauche, presets au centre, VIEW à droite.
// RESET et le volume Windows sont passés dans l'en-tête.
class ControlsBarLeak : public juce::Component
{
public:
    ControlsBarLeak(SpectrumDisplayLeak&, LeakAudioProcessor&, VuMeterLeak&, GpuOscilloscopeDisplay&);
    ~ControlsBarLeak() override { setLookAndFeel(nullptr); }
    void resized() override;
    void paint  (juce::Graphics&) override;
    void syncFromProcessor();
    void syncOscFromProcessor();   // oscilloscope (et ses faders) relu dans le processor
    PresetBar presets;             // sélecteur de presets, au centre
private:
    SpectrumDisplayLeak&  display;
    LeakAudioProcessor&   processor;
    VuMeterLeak&          vuMeter;
    GpuOscilloscopeDisplay& oscillo;
    NeuLookAndFeel       laf;

    NeuAnimButton    peakBtn   { "ON" };
    FocusComboBox    peakSel;
    FocusComboBox    rangeSel;
    NeuAnimButton    midBtn    { "MID" };
    NeuAnimButton    sideBtn   { "SIDE" };
    NeuAnimButton    bands15Btn{ "15" };
    NeuAnimButton    bands31Btn{ "31" };
    NeuAnimButton    bands62Btn{ "62" };

    // titres en ligne (PEAK HOLD, RANGE, MS, VIEW) et séparateurs gravés entre
    // groupes, placés dans resized()
    juce::Rectangle<int> groupLabel[4];
    float groupDividerX[2] {};

    std::function<void()> updateMSMode;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlsBarLeak)
};

//==============================================================================
// AboutOverlay (v1.70) — page À propos dans l'esprit de Sevenpack Alpha (aperçu
// preview/merlin_about_preview.html validé) : en-tête (monogramme, ABOUT
// MERLIN α, accroche, version), bandeau du duo (portraits noir et blanc
// Mikshake · CODE / Sunny · DESIGN + présentation), 3 colonnes de texte
// défilantes, pied de page (deux signatures, liens site | licence, tampon
// "Calibrated"). Apparition / disparition animées ; se ferme au clic n'importe
// où ou à Échap, sauf sur les liens.
//==============================================================================
class AboutOverlay : public juce::Component, private juce::Timer
{
public:
    // Élément de colonne : titre, paragraphe justifié ou puce
    struct ColumnItem
    {
        enum class Kind { Paragraph, Header, Bullet };
        juce::String text;
        Kind kind = Kind::Paragraph;
    };

    // Colonne de texte : calcule sa propre hauteur (le Viewport parent sait
    // jusqu'où défiler) ; un clic dessus ferme aussi la page
    class Column : public juce::Component
    {
    public:
        std::function<void()> onAnyClick;
        void mouseDown(const juce::MouseEvent&) override { if(onAnyClick) onAnyClick(); }
        void setItems(std::vector<ColumnItem> v) { items=std::move(v); }
        void layout(int width);   // appelé par le parent une fois la largeur connue
        void paint(juce::Graphics&) override;
    private:
        float itemHeight(const ColumnItem&, float width) const;
        float drawItem(juce::Graphics*, const ColumnItem&, float y, float width) const;   // renvoie la hauteur
        std::vector<ColumnItem> items;
    };

    AboutOverlay();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;   // main sur les liens
    bool keyPressed(const juce::KeyPress&) override;
    void visibilityChanged() override;

    void showAnimated();   // thème relu, voile fondu, panneau qui grandit avec un léger rebond
    void hideAnimated();
    void setLicenseManager(LicenseManager& lm) { license=&lm; }
    std::function<void()> onOpenLicence;   // lien licence du pied de page

private:
    struct Layout
    {
        juce::Rectangle<float> header, team, footer;
        float headerLineY=0.f, teamLineY=0.f, footerLineY=0.f, lineX0=0.f, lineX1=0.f;
        juce::Rectangle<int> colA, colB, colC;
        float dividerX1=0.f, dividerX2=0.f, colTop=0.f, colBottom=0.f;
    };
    Layout computeLayout() const;
    juce::Rectangle<float> panelBounds() const;
    juce::AffineTransform panelTransform() const;
    void startAnimation(bool in);
    void timerCallback() override;
    void rebuildChromeCache();
    void drawHeader(juce::Graphics&, juce::Rectangle<float>) const;
    void drawTeam  (juce::Graphics&, juce::Rectangle<float>, float scale) const;
    void drawFooter(juce::Graphics&, juce::Rectangle<float>, float scale);
    void drawStamp (juce::Graphics&, juce::Rectangle<float>) const;
    juce::String licenceText() const;

    juce::Image monogram, portrait[2], signature[2];   // signatures : encre seule (alpha), teintée au thème
    juce::Image chromeCache;                              // décor statique, à l'échelle de l'écran
    float chromeScale=1.f;
    juce::Rectangle<float> websiteBounds, licenceBounds;
    LicenseManager* license=nullptr;

    juce::Viewport viewport[3];
    Column column[3];

    bool animating=false, animatingIn=false;
    double animStartMs=0.0;
    float animOpacity=1.f, animScale=1.f, animFrom=0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutOverlay)
};

//==============================================================================
// LeakEditor — système de 6 slots configurables par clic droit
//==============================================================================
class LeakEditor : public juce::AudioProcessorEditor,
                   private juce::Timer,
                   private juce::ComponentListener,
                   private juce::ChangeListener   // état de la licence
{
public:
    explicit LeakEditor(LeakAudioProcessor&);
    ~LeakEditor() override;
    void paint  (juce::Graphics&) override;
    void resized() override;

    // Applique les réglages d'Options > Image (images/s, anticrénelage, V-Sync)
    void applyGraphics();
    // Bouton plein écran de la barre de titre : la fenêtre (Main.cpp) bascule
    std::function<void()> onToggleFullScreen;
    void setFullScreenState(bool b) { topBar.setFullScreenState(b); }
    // passage plein écran <-> fenêtré : rendu GPU lâché pendant le transfert
    // de l'éditeur, repris une fois la nouvelle fenêtre peinte
    void beginWindowSwap() { ++windowSwaps; updateGpuSuspension(); }
    void endWindowSwap()   { if(windowSwaps>0) --windowSwaps; updateGpuSuspension(); }
    void setKeyboardState(bool b);   // bouton clavier : aussi appliqué aux modules détachés
    // fenêtres détachées (modules, Options) : elles repassent devant la fenêtre
    // principale quand celle-ci vient d'être remontée
    void raiseDetachedWindows();
    void setAlwaysOnTopState(bool b)
    {
        topBar.setAlwaysOnTopState(b);
        alwaysOnTopState=b;
        for(auto& f : floating)   // les modules détachés suivent la fenêtre principale
            if(f.window!=nullptr) f.window->setAlwaysOnTop(b);
        if(optionsWindow!=nullptr) optionsWindow->setAlwaysOnTop(b);   // la fenêtre des Options aussi
    }
    void mouseDown(const juce::MouseEvent&) override;

    // Fonctions de la fenêtre (Main.cpp) reprises par le menu Options
    struct HostHooks
    {
        std::function<int()>          getAudioSource;    // 0 automatique, 1 carte son, 2 sortie Windows
        std::function<void(int)>      setAudioSource;
        std::function<juce::String()> getSourceName;
        std::function<void()>         openAudioSettings;
        std::function<bool()>         isFullScreen;
        std::function<bool()>         isAlwaysOnTop;
        std::function<void(bool)>     setAlwaysOnTop;
        std::function<void(bool)>     setKeyboardInMerlin;   // bouton clavier de l'en-tête
        std::function<void(bool)>     holdKeyboard;          // écran de licence : clavier à Merlin le temps de la saisie
        std::function<void()>         uiScaleChanged;        // Options › Taille de l'interface : la fenêtre remet l'éditeur à l'échelle
    };
    HostHooks host;
    bool isLicenceOpen() const { return licenseOverlay.isVisible(); }   // écran de licence affiché

private:
    void timerCallback() override;
    void updateLayout();
    void applyPreset(int idx);   // 0=Mixing 1=Mastering 2=Sound Design 3=TV
    void applyFrameRate(int hz);   // cadence des modules à l'écran (cachés : arrêtés)
    std::map<const juce::Component*, int> appliedFrameRate;   // dernière cadence appliquée par module
    void refreshChassis();   // reconstruit les caches du châssis
    void applyLook();   // teinte + contraste du processeur -> châssis, caches, vignettes
    void applyTextScale();   // taille de texte du processeur -> toute l'interface
    // Taille de l'interface (plugin) : échelle de l'hôte × celle choisie, appliquée
    // par AudioProcessorEditor::setScaleFactor (la DAW suit la taille de la fenêtre).
    // Dans l'appli, c'est la fenêtre qui met l'éditeur à l'échelle (Main.cpp).
    void setScaleFactor(float newScale) override;
    void applyUiScale();
    float hostScale { 1.f }, appliedUiScale { 0.f };
    int   uiScaleCheckTicks { 0 };
    void applyLanguage();    // langue du processeur -> toute l'interface, en place
    void paintChassisOverlay(juce::Graphics&);
    void rebuildChassisCaches(float pxScale);
    void drawEmptySlotPlus(juce::Graphics&, int slot);
    // palette des modules relue depuis les couleurs du processeur (écrans
    // Papier compris) ; actions de l'onglet Couleurs des Options
    void applyColours();
    void selectAccentPalette(int idx);
    void setColourRole(int role, juce::Colour c);
    void resetColours();
    void resetLayout();

    // Slot management
    juce::Component* getComponentForModule(int moduleId);
    int  hitTestSlot(juce::Point<int> pt) const;
    void showModuleMenu(int slot, juce::Point<int> screenPos);

    bool initialSyncDone { false };

    LeakAudioProcessor&  audioProcessor;
    LeakLookAndFeel      laf;
    SlotMenuLookAndFeel  slotMenuLaf;   // menu clic droit des slots
    TopBar               topBar;
    VectorscopeDisplay   vectorscope;
    GpuOscilloscopeDisplay oscilloscope;   // rendu par la carte graphique
    LoudnessPanel        loudnessPanel;
    VuMeterLeak          vuMeter;
    SpectrumDisplayLeak  spectrum;
    MultibandCorrDisplay corrDisplay;
    MultipanoramaDisplay panoDisplay;
    PanoramaLedDisplay   panoLed;
    SpectralDensityDisplay  spectralDensity;
    NeedleVuMeter        needleVu;
    MultibandVectorscopeDisplay mbVectorscope;
    Spectrogram3DPeaksDisplay   spectro3dPeaks;
    Spectrogram3DSurfaceDisplay spectro3dSurface;
    GpuSpectrogramDisplay gpuTerrain, gpuNeon, gpuCrystal;
    GpuSurfaceDisplay     gpuSurface;   // Spectrogram Surface GPU
    GpuDotSpectrumDisplay gpuDotChroma, gpuDotNebula;
    GpuDotPanoramaDisplay gpuDotPano;
    GpuVectorscopeDisplay gpuVecPhosphor, gpuVecDensity;
    // tous les modules OpenGL (suspension sous les panneaux, Options > Image)
    std::array<GpuModuleBase*, 10> gpuModules() { return { &gpuTerrain, &gpuNeon, &gpuCrystal, &gpuSurface, &gpuDotChroma, &gpuDotNebula, &gpuDotPano, &gpuVecPhosphor, &gpuVecDensity, &oscilloscope }; }
    ControlsBarLeak      controls;

    // Les modules GPU sont des fenêtres OpenGL natives : rien ne peut être
    // peint par-dessus, leur rendu est donc suspendu tant qu'About ou
    // Settings est ouvert.
    void componentVisibilityChanged(juce::Component&) override
    {
        updateGpuSuspension();
        if(host.holdKeyboard) host.holdKeyboard(licenseOverlay.isVisible());   // saisie de la clé (appli)
    }
    void updateGpuSuspension();
    int  windowSwaps { 0 };   // transferts de fenêtre en cours (plein écran)

    // Calque transparent AU-DESSUS des modules mais SOUS les panneaux About /
    // Settings : décor nature qui déborde sur la plaque + bords creusés des
    // écrans. Pur dessin, les clics traversent.
    struct OverlayLayer : public juce::Component
    {
        OverlayLayer() { setInterceptsMouseClicks(false,false); }
        std::function<void(juce::Graphics&)> painter;
        void paint(juce::Graphics& g) override { if(painter) painter(g); }
    };
    OverlayLayer         overlayLayer;
    AboutOverlay         aboutOverlay;
    OptionsOverlay       optionsOverlay;    // menu Options (trois points)
    // licence (DRM SideKey) : un gestionnaire partagé par l'appli et toutes les instances du plugin
    juce::SharedResourcePointer<LicenseManager> license;
    LicenseOverlay       licenseOverlay { *license };
    void changeListenerCallback(juce::ChangeBroadcaster*) override { updateLicenseUi(); }
    void updateLicenseUi();
    void openLicence();

    // Modules détachés (appli) : titre tiré hors de l'emplacement → fenêtre à
    // part qui suit la souris. L'emplacement devient un creux partagé en deux
    // (v1.71) : une moitié ramène le module, l'autre ouvre le menu pour en
    // mettre un autre, la fenêtre restant ouverte. Fermer la fenêtre ramène le
    // module si son emplacement l'attend encore, sinon il disparaît (il reste
    // dans le menu ; le choisir le ramène). Pas mémorisé d'un lancement à l'autre.
    struct DetachedWindow : public juce::DocumentWindow
    {
        // fenêtre native créée une seule fois, une fois taillée (sinon sa partie
        // droite pouvait ne pas être dessinée)
        DetachedWindow(const juce::String& name, juce::Component& content, std::function<void()> close)
            : juce::DocumentWindow(name, Col::bg, juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton, false),
              onClose(std::move(close))
        {
            barLook->install(*this);
            setContentNonOwned(&content, true);
            setResizable(true, false);
            setResizeLimits(220, 140, 6000, 4000);
        }
        ~DetachedWindow() override { clearContentComponent(); setLookAndFeel(nullptr); }   // le module appartient à l'éditeur
        void closeButtonPressed() override { if(onClose) onClose(); }
        // écart entre le coin de la fenêtre et son contenu (barre Merlin, bord)
        juce::Point<int> contentOffset() const { const auto b=getContentComponentBorder(); return { b.getLeft(), b.getTop() }; }
        std::function<void()> onClose;
        juce::SharedResourcePointer<WindowBarLook> barLook;
    };
    // fenêtre d'un module détaché, et l'emplacement qu'il a quitté
    // (lastBounds, moving : la fenêtre est-elle en train d'être déplacée ?)
    struct FloatingModule { int module=0, homeSlot=-1; std::unique_ptr<DetachedWindow> window;
                            juce::Rectangle<int> lastBounds; bool moving=false; };
    std::vector<FloatingModule> floating;
    // Options (appli) : fenêtre à part, déplaçable ; Merlin reste visible et actif
    std::unique_ptr<DetachedWindow> optionsWindow;
    void closeOptions();
    FloatingModule* floatingFor(int module);   // nullptr si le module n'est pas dans une fenêtre
    // emplacement quitté par son module, qui est dans une fenêtre (creux partagé)
    bool isDetached(int s) const;
    void detachSlot(int s);
    void reattachSlot(int s);       // ramène dans s le module qui l'a quitté
    void dockFloating(int module);  // ferme la fenêtre : le module revient à l'éditeur (affiché seulement s'il a un emplacement)
    void reattachAll();
    void followDetachDrag();
    // fenêtre ramenée sur son emplacement d'origine : il s'allume, et le module
    // y revient au relâchement
    void updateDockMagnet();
    void drawDockMagnet(juce::Graphics&, int s);
    int  magnetSlot { -1 };
    void drawDetachedSlot(juce::Graphics&, int s);
    void drawDetachedHalf(juce::Graphics&, int s, bool addHalf, bool hover);
    // moitiés du creux partagé : ramener le module (gauche / haut), en ajouter un autre (droite / bas)
    juce::Rectangle<int> detachedHalf(int s, bool addHalf) const;
    int  hoverDetachedSlot { -1 };
    bool hoverDetachedAdd { false };
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    int  dragSlot { -1 };                 // titre de module cliqué : candidat au détachement
    juce::Point<int> dragStart, dragGrab; // départ (écran) / prise dans le module
    bool dragFollowing { false };         // fenêtre détachée qui suit la souris
    bool alwaysOnTopState { false };      // « Toujours au premier plan » de l'appli, repris par les fenêtres détachées
    bool keyboardState { false };         // clavier à Merlin (sinon un clic ne l'enlève pas à Ableton)
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool isOwnMouseEvent(const juce::MouseEvent& e) const { return e.eventComponent==this || isParentOf(e.eventComponent); }

    void setupOptions();
    void openOptions();
    OptionsOverlay::State optionsState() const;
    void syncTopBarLook();
    // presets utilisateur
    void refreshPresetList();
    void loadUserPreset(const juce::String& name);
    void reloadFromProcessor();   // preset rappelé : toute l'interface relit l'état
    void askSavePreset();
    void askDeletePreset(const juce::String& name);

    // 6 slots remplaçant rcVec/rcOsc/rcTrn/rcLoud/rcSpec/rcVu
    juce::Rectangle<int> rcSlot[LeakAudioProcessor::NUM_SLOTS];
    juce::Rectangle<int> rcTitle, rcCtrl;
    // design de la section master (A Instrument / C Console), sous le VU master
    PillToggle masterStyleToggle { PillToggle::Icon::Meter };

    // Caches du châssis, à la résolution physique — reconstruits si la taille,
    // l'échelle, le thème ou la disposition des slots changent.
    juce::Image chassisCache;          // plaque + cartes en relief (opaque)
    juce::Image overlayCache;          // bords creusés des écrans (transparent)
    juce::RectangleList<int> overlayRegions;   // zones utiles de overlayCache
    float cacheScale { 0.f };
    int   cacheW { 0 }, cacheH { 0 };
    bool  layoutDirty { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LeakEditor)
};
