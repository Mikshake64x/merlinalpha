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
// 2 vintage à lunette, 3 deux voies. Le fond (dégradé, graticule, légendes
// fixes) est dessiné en 2D puis envoyé en texture ; les traces sont des
// rubans anti-aliasés (geometry shader). Rémanence : les deux images
// précédentes, plus pâles. Déclenchement sur la fondamentale + calage sur le
// motif (voir capture()).
//==============================================================================
class GpuOscilloscopeDisplay : public GpuModuleBase
{
public:
    explicit GpuOscilloscopeDisplay(LeakAudioProcessor&);
    ~GpuOscilloscopeDisplay() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // réglages de la barre du bas (TIME / AMP)
    static constexpr int kMaxWindow = 8192;   // fenêtre affichée max (le reste de l'anneau sert au déclenchement)
    void setTimeDiv(int samples) { nPts=juce::jlimit(64, kMaxWindow, samples); repaint(); }   // repaint : ms/div
    void setAmplitude(float g)   { ampGain=juce::jlimit(0.25f, 6.f, g); }
    // style, MONO/ST, rémanence, cadence relus (Options, preset rappelé)
    void syncFromProcessor();
    int  maxFrameRate() const override;   // Options > cadence de l'oscilloscope
    // Faders du module (AMP vertical à gauche, TIME horizontal en bas), seuls
    // réglages de l'oscilloscope : refreshFaders() relit les valeurs du processor
    void refreshFaders();
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
    void paintBackground(juce::Graphics&, int style, bool mono) const;
    void rebuildBackgroundIfNeeded();

    juce::TextButton monoBtn { "ST" };   // (bouton TRIG retiré : déclenchement toujours actif)
    std::unique_ptr<ReleaseFader> ampFader, timeFader;

    // réglages lus par le thread GL
    std::atomic<int>   nPts { 512 };
    std::atomic<float> ampGain { 1.f };
    std::atomic<bool>  triggerMode { true };
    std::atomic<int>   styleA { 0 };
    std::atomic<bool>  monoA { false }, persistA { true };
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

    // mesures (style numérique), affichées par le calque
    std::atomic<float> measVpp { 0.f }, measHz { 0.f };
    juce::String shownMeas;

    // objets GL
    std::unique_ptr<juce::OpenGLShaderProgram> bgShader, lineShader, fillShader;
    unsigned int vaoQuad { 0 }, vaoLines { 0 }, vboLines { 0 }, vaoFill { 0 }, vboFill { 0 };
    std::vector<float> verts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GpuOscilloscopeDisplay)
};
