#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include "GpuModule.h"

class ReleaseFader;   // PluginEditor.h
class NeuAnimButton;  // PluginEditor.h

//==============================================================================
// GpuSurfaceDisplay — "Spectrogram Surface GPU" : EXACTEMENT le dessin du
// Spectrogram 3D Surface (courbes de spectre remplies d'un dégradé inferno,
// empilées en perspective oblique, plus transparentes au fond, sol "plateau 0"
// quadrillé), mais tracé par la carte graphique à chaque rafraîchissement de
// l'écran : défilement continu et net même avec un TIME long (le module CPU
// repeint image par image). Le module CPU reste disponible tel quel.
//
// Le rendu GL reprend la géométrie du module CPU en pixels logiques ; le
// calque 2D (titre, repères de fréquence et de temps, bouton de sens, lecture
// au survol, fader TIME, HOLD) est peint par paint() par-dessus.
//
// (v1.49 : essai d'une analyse fine + 512 courbes, jugé moins lisible par le
// user -> retour à cette version.)
//==============================================================================
class GpuSurfaceDisplay : public GpuModuleBase
{
public:
    explicit GpuSurfaceDisplay(LeakAudioProcessor&);
    ~GpuSurfaceDisplay() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void syncFromProcessor();   // preset rappelé : TIME, sens et perspective relus

private:
    static constexpr int NPTS  = 160;   // points par courbe (log 20 Hz–20 kHz / Nyquist)
    static constexpr int NROWS = 96;    // courbes en mémoire (comme le module CPU)

    // géométrie commune (rendu GL et calque 2D), en pixels logiques
    struct Geo {
        float base, x0, pw, dir, totalDx, totalDy, peakH, labelH, L0;
    };
    Geo geometry(float W, float H) const;
    double captureMs() const { return juce::jmax(1.0, (double)timeSecA.load())*1000.0/(double)(NROWS-1); }

    // ── thread GL ──
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void uiTick() override;
    void drawDirectionButton(juce::Graphics&);

    std::unique_ptr<ReleaseFader> timeFader;
    std::unique_ptr<NeuAnimButton> holdBtn;
    std::atomic<bool>  frozen { false };
    // réglages lus par le thread GL (copies des champs du processeur)
    std::atomic<float> timeSecA { 2.1f }, tiltA { 0.3f }, skewA { 0.f };
    std::atomic<bool>  reverseA { false };
    std::atomic<int>   viewW { 1 }, viewH { 1 };
    std::atomic<int>   titleBottom { 26 };   // le relief s'arrête là : rien ne chevauche le titre
    float tiltAtDragStart { 0.3f }, skewAtDragStart { 0.f };
    bool  dragFromTitle { false };   // glisser parti du titre : pas de perspective (détachement du module)
    juce::Rectangle<float> dirBtnBounds;

    // survol : position souris (interface) -> courbe touchée (thread GL)
    std::atomic<bool>  hoverOn { false };
    std::atomic<float> hoverX { 0.f }, hoverY { 0.f };
    std::atomic<bool>  hitValid { false };
    std::atomic<float> hitTopY { 0.f }, hitBaseY { 0.f }, hitHz { 0.f }, hitDb { 0.f }, hitAgeSec { 0.f };
    bool  shownValid { false };
    float shownTop { 0.f }, shownDb { 0.f }, shownX { 0.f };

    // données (thread GL) : anneau de courbes, rows[head] = la plus récente
    float  rows[NROWS][NPTS] {};
    int    head { 0 }, rowCount { 0 };
    juce::uint32 captureTotal { 0 };
    double lastCaptureMs { 0.0 };
    float  glideFrac { 0.f };
    PsdLogMap logMap;
    int seenAxisVersion { 0 };
    float  liveDb[NPTS] {};
    // courbe "live" au fond, mise à jour à chaque image : le bord du fond bouge
    // en continu même avec un TIME long (sinon une courbe toutes les TIME/95 s)
    float  liveRow[NPTS] {};
    bool   haveLive { false };

    // objets GL
    std::unique_ptr<juce::OpenGLShaderProgram> shader;
    unsigned int vao { 0 }, vbo { 0 };
    std::vector<float> verts;   // x, y, g, alpha par sommet

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GpuSurfaceDisplay)
};
