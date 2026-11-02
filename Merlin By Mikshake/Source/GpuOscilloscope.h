#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include "GpuModule.h"

class ReleaseFader;   // PluginEditor.h

//==============================================================================
// GpuOscilloscopeDisplay — oscilloscope rendu par la carte graphique.
//
// Les images sont cadencées par la synchro de l'écran (une sur deux à 30 i/s
// sur un écran 60 Hz) et la capture + le tracé se font sur le thread OpenGL :
// plus d'à-coups quand l'interface est chargée (l'ancien oscilloscope 2D
// dépendait du timer du thread de messages, irrégulier).
//
// Quatre styles (Options > Affichage) : 0 tube cathodique, 1 numérique (DSO),
// 2 vintage à lunette, 3 rempli. Le fond (dégradé, graticule, légendes
// fixes) est dessiné en 2D puis envoyé en texture ; les traces sont des
// rubans anti-aliasés (geometry shader). Rémanence : les deux images
// précédentes, plus pâles. Chaque image se cale sur la forme des précédentes ;
// la fondamentale sert de secours quand il n'y a rien à quoi se comparer
// (voir capture()).
//==============================================================================
class GpuOscilloscopeDisplay : public GpuModuleBase
{
public:
    explicit GpuOscilloscopeDisplay(LeakAudioProcessor&);
    ~GpuOscilloscopeDisplay() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    // coup gardé : molette = zoom en temps autour du pointeur, clic gauche = déplacement, double-clic = vue entière
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    // réglages de la barre du bas (TIME / AMP)
    static constexpr int kMaxWindow = 65536;  // fenêtre affichée max, 1,37 s à 48 kHz (le reste de l'anneau sert au déclenchement)
    void setTimeDiv(int samples) { nPts=juce::jlimit(64, kMaxWindow, samples); repaint(); }   // repaint : ms/div
    void setAmplitude(float g)   { ampGain=juce::jlimit(0.25f, 6.f, g); }
    // style, MONO/ST, rémanence, cadence relus (Options, preset rappelé)
    void syncFromProcessor();
    int  maxFrameRate() const override;   // Options > cadence de l'oscilloscope
    // Faders du module (AMP vertical à gauche, TIME horizontal en bas), seuls
    // réglages de l'oscilloscope : refreshFaders() relit les valeurs du processor
    void refreshFaders();
    std::function<void()> onSplitChanged;   // bouton du module : les Options suivent
    static constexpr float kFaderL = 26.f, kFaderB = 20.f;   // place réservée (gauche, bas)

private:
    static constexpr int MAXN = LeakAudioProcessor::SCOPE_SIZE;

    // ── thread GL ──
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    bool capture();              // nouvelle fenêtre déclenchée (false : image gardée)
    void pushHistory();
    void uploadBackground();

    // ── calque 2D / fond (thread de messages) ──
    void uiTick() override;
    static juce::Rectangle<float> plotAreaFor(int style, float W, float H);
    static juce::Rectangle<float> tubeAreaFor(float W, float H);
    void paintBackground(juce::Graphics&, int style, bool mono, bool split) const;
    void rebuildBackgroundIfNeeded();

    juce::TextButton monoBtn { "ST" };   // (bouton TRIG retiré : déclenchement toujours actif)
    juce::TextButton splitBtn { "OVER" };   // en stéréo : voies superposées (OVER) ou séparées (SPLIT)
    juce::TextButton precisionBtn { "PRECISION" };   // allumé : calage sur la fondamentale plutôt que sur la forme
    juce::TextButton captureBtn { "CAPTURE" };       // garde le prochain coup à l'écran
    juce::TextButton superposeBtn { "SUPERPOSE" };   // la mesure gardée reste en référence sous les coups suivants
    std::unique_ptr<ReleaseFader> ampFader, timeFader;
    // SUPERPOSE : pastilles 1 (référence) et 2 (dernier coup), légende et choix de l'affichage
    class TraceChip final : public juce::Button
    {
    public:
        TraceChip(const juce::String& name, juce::Colour c) : juce::Button(name), colour(c) { setMouseClickGrabsKeyboardFocus(false); }
        void paintButton(juce::Graphics&, bool over, bool down) override;
    private:
        juce::Colour colour;
    };
    static constexpr juce::uint32 kRefColour = 0xFFFFB454;
    TraceChip chip1 { "1", juce::Colour(kRefColour) }, chip2 { "2", juce::Colour(0xFF8FF6FF) };
    TraceChip resetChip { "RESET", juce::Colour(0xFFB8C4CC) };   // efface les captures, retour au direct
    void layoutChips();

    // réglages lus par le thread GL
    std::atomic<int>   nPts { 512 };
    std::atomic<float> ampGain { 1.f };
    std::atomic<bool>  triggerMode { true };
    std::atomic<int>   styleA { 0 };
    std::atomic<bool>  monoA { false }, persistA { true };
    std::atomic<bool>  splitA { false };   // en stéréo : L et R l'une au-dessus de l'autre
    std::atomic<bool>  precisionA { false };
    enum { kCapLive, kCapArmed, kCapHeld };          // direct, attente du coup, coup gardé
    std::atomic<int>   captureA { kCapLive };        // le thread GL passe d'attente à gardé
    std::atomic<bool>  superposeA { false }, capValidA { false };   // capValidA : un coup est dans capL/capR
    std::atomic<bool>  refReadyA { false }, showFirstA { true }, showLastA { true };   // référence prête, captures affichées
    static constexpr int kMinView = 32;              // zoom maximal, en échantillons
    std::atomic<int>   viewStartA { 0 }, viewLenA { 0 };   // portion du coup affichée, viewLenA 0 = fenêtre entière
    int  dragStartView { 0 };
    bool handCursor { false };
    std::atomic<float> widthMulA { 1.f };      // Options : épaisseur du trait
    std::atomic<bool>  classicTraceA { false }; // Options : tracé classique (enveloppe dès 2 échantillons / pixel)
    std::atomic<int>   viewW { 1 }, viewH { 1 };

    // fond : image (résolution physique) envoyée en texture
    juce::SpinLock   bgLock;
    juce::Image      bgImage;
    std::atomic<int> bgVersion { 0 };
    int bgKey { 0 }, bgUploaded { -1 };
    unsigned int bgTex { 0 };
    int bgTexW { 0 }, bgTexH { 0 };

    // données (thread GL)
    std::vector<float> rawL, rawR, trigSig, bufL, bufR, bufM, tmpM;
    static constexpr int kHist = 2;
    std::vector<float> histL[kHist], histR[kHist];
    int   histHead { 0 }, histCount { 0 }, histN { 0 };
    int   curN { 0 };            // fenêtre de l'image courante
    // calage sur la phase de la fondamentale (indices absolus dans le flux de l'oscilloscope)
    double period { 0.0 };       // période de la fondamentale (échantillons), 0 = inconnue
    static constexpr int kAcMax = 1024;
    float acBuf[kAcMax] {}, nsdfBuf[kAcMax] {};
    double estimatePeriod(int fetch, int warm, double sr);
    // calage sur la forme : moyenne glissante des dernières fenêtres affichées
    struct ShapeMatch { int start { -1 }; int repeat { 0 }; };   // début dans rawL, écart avec la répétition d'avant
    ShapeMatch matchShape(int n, int search, double sr);
    std::vector<float> lockSig, lockRef, lockDec, lockRefDec, lockScore;
    int  lockN { 0 };            // fenêtre de la référence, 0 = pas de référence
    int  lockMiss { 0 };         // images de suite sans ressemblance
    bool lockMono { false };
    // CAPTURE : coup copié ici, avec 5 ms avant lui (indices absolus ; capHit 0 = pas encore de coup)
    static constexpr int kCapLen = kMaxWindow;
    bool watchForHit(int fetch, juce::uint64 endAbs, int n, double sr);
    void readHit(juce::uint64 start, std::vector<float>& l, std::vector<float>& r, bool& pending);
    std::vector<float> capL, capR, refL, refR;
    juce::uint64 capSeen { 0 }, capHit { 0 }, capStart { 0 }, refStart { 0 };
    float capFloor { 0.f };      // niveau de fond
    bool  capWatching { false }, capFresh { false };
    bool  capPending { false };  // gardé dès que la fenêtre affichée est pleine, le reste arrive ensuite
    bool  refValid { false }, refPending { false };
    int   heldStart { 0 };       // début de la portion affichée, dans capL et refL

    // mesures (style numérique), affichées par le calque
    std::atomic<float> measVpp { 0.f }, measHz { 0.f };
    juce::String shownMeas;

    // objets GL
    std::unique_ptr<juce::OpenGLShaderProgram> bgShader, lineShader, fillShader;
    unsigned int vaoQuad { 0 }, vaoLines { 0 }, vboLines { 0 }, vaoFill { 0 }, vboFill { 0 };
    std::vector<float> verts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GpuOscilloscopeDisplay)
};
