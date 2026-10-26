#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include "GpuModule.h"

class ReleaseFader;   // PluginEditor.h

//==============================================================================
// GpuDotSpectrumDisplay — spectre en nuage de points façon ChromaVerb, rendu
// par la carte graphique. Couleur selon la fréquence (jaune dans les graves ->
// vert -> cyan -> bleu -> violet dans les aigus), 160 points log 20 Hz–20 kHz
// lus dans la PSD du processeur.
//   - brume colorée : calculée pixel par pixel dans un shader (flou gaussien
//     horizontal de 9 prises dans la texture des niveaux, bord haut adouci)
//   - particules : naissent sous la courbe en proportion de l'énergie,
//     montent et s'éteignent ; dessinées en point sprites additifs
// Deux rendus validés sur preview :
//   Chroma : points fins et nets sur la brume (fidèle à la ChromaVerb)
//   Nebula : particules lumineuses qui montent + enveloppe lumineuse nette
//==============================================================================
class GpuDotSpectrumDisplay : public GpuModuleBase
{
public:
    enum class Style { Chroma, Nebula };

    GpuDotSpectrumDisplay(LeakAudioProcessor&, Style);
    ~GpuDotSpectrumDisplay() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void syncFromProcessor();   // preset rappelé : REL relu

private:
    static constexpr int   NB = PsdLogMap::N;
    static constexpr float kDbMin = -90.f, kDbMax = -10.f;   // échelle verticale

    // ── thread GL ──
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void uiTick() override;

    void updateLevels(float dt);
    void updateParticles(float dt, float plotW, float plotH);
    const char* title() const;
    float& releaseRef();   // valeur sauvegardée du fader REL (processeur)

    const Style style;

    // fader REL, même réglage que celui du Spectrum (0.1–5 s, défaut 0.4 s)
    std::unique_ptr<ReleaseFader> relFader;
    std::atomic<float> releaseSec { 0.4f };

    // zone de tracé (px logiques), lue par le thread GL
    std::atomic<float> plotX { 0.f }, plotY { 0.f }, plotW { 1.f }, plotH { 1.f };
    std::atomic<int>   viewW { 1 }, viewH { 1 };

    // niveaux (thread GL) : brut et lissé, normalisés 0..1 sur kDbMin..kDbMax
    PsdLogMap logMap;
    float rawDb[NB] {}, raw[NB] {}, smooth[NB] {}, smoothDb[NB] {};
    double lastFrameMs { 0.0 };

    struct Particle { float x, y, u, life, maxLife, vy, size; };
    std::vector<Particle> parts;
    std::vector<float>    partVerts;     // x, y, u, alpha, taille
    std::vector<float>    ribbonVerts;   // x, y, u, côté
    juce::Random rng;

    // survol : niveaux lissés copiés pour l'interface
    juce::SpinLock uiLock;
    float uiDb[NB] {};
    std::atomic<bool>  hoverOn { false };
    std::atomic<float> hoverX { 0.f };

    // objets GL
    std::unique_ptr<juce::OpenGLShaderProgram> hazeShader, dotShader, ribbonShader;
    unsigned int vaoQuad { 0 }, vboQuad { 0 }, vaoDots { 0 }, vboDots { 0 },
                 vaoRibbon { 0 }, vboRibbon { 0 }, levelTex { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GpuDotSpectrumDisplay)
};

//==============================================================================
// GpuDotPanoramaDisplay — panorama par bande au style Dot Spectrum Chroma :
// fréquence en X (log), position stéréo en Y (L en haut, R en bas, comme le
// Panorama classique). Chaque bande du banc de filtres envoie des points à sa
// position stéréo, d'autant plus dispersés verticalement que la bande est
// large (corrélation basse) ; couleur selon la fréquence, lueur calculée
// dans un shader (gaussienne par bande).
//==============================================================================
class GpuDotPanoramaDisplay : public GpuModuleBase
{
public:
    explicit GpuDotPanoramaDisplay(LeakAudioProcessor&);
    ~GpuDotPanoramaDisplay() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    static constexpr int   NMAX = LeakAudioProcessor::N_BANDS;
    static constexpr float kDbMin = -66.f;   // niveau de bande -> 0..1 sur kDbMin..0 dB

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void uiTick() override;

    void updateBands(float dt);
    void updateParticles(float dt, float pw, float ph);

    std::atomic<float> plotX { 0.f }, plotY { 0.f }, plotW { 1.f }, plotH { 1.f };
    std::atomic<int>   viewW { 1 }, viewH { 1 };

    // bandes (thread GL) : position u (log), panoramique, niveau, dispersion
    int   nBands { 0 };
    float bandU[NMAX] {}, panS[NMAX] {}, levS[NMAX] {}, rawLev[NMAX] {}, dbS[NMAX] {}, spreadS[NMAX] {};
    float inPan[NMAX] {}, inDb[NMAX] {}, inCorr[NMAX] {};
    float bandUniforms[NMAX*4] {};
    double lastFrameMs { 0.0 };

    struct Particle { float x, y, u, life, maxLife, vy, size; };   // x, y : 0..1 dans la zone (y depuis le haut)
    std::vector<Particle> parts;
    std::vector<float>    partVerts;
    juce::Random rng;

    // survol : copie des bandes pour l'interface
    juce::SpinLock uiLock;
    int   uiN { 0 };
    float uiFreq[NMAX] {}, uiPan[NMAX] {}, uiDb[NMAX] {};
    std::atomic<bool>  hoverOn { false };
    std::atomic<float> hoverX { 0.f };

    std::unique_ptr<juce::OpenGLShaderProgram> hazeShader, dotShader;
    unsigned int vaoQuad { 0 }, vaoDots { 0 }, vboDots { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GpuDotPanoramaDisplay)
};
