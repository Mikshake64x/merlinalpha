#pragma once
#include <JuceHeader.h>
#include <atomic>
#include "GpuModule.h"

class ReleaseFader;   // PluginEditor.h
class NeuAnimButton;  // PluginEditor.h

//==============================================================================
// GpuSpectrogramDisplay — spectrogramme 3D rendu par la carte graphique
// (OpenGL + shaders). Maillage 160 fréquences (log 20 Hz–20 kHz, d'après la
// FFT 4096 points du processeur) x 64 instants, une ligne capturée toutes les
// ~33 ms et défilement continu entre deux captures. Trois rendus (previews
// WebGL) : Terrain éclairé, Néon, Cristal.
//
// Contexte, réglages graphiques, suspension et cadre : voir GpuModuleBase.
//==============================================================================
class GpuSpectrogramDisplay : public GpuModuleBase
{
public:
    enum class Style { Terrain, Neon, Crystal };

    GpuSpectrogramDisplay(LeakAudioProcessor&, Style);
    ~GpuSpectrogramDisplay() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void syncFromProcessor();   // preset rappelé : TIME et vue 3D relus

private:
    static constexpr int    NX = 160;          // fréquences
    static constexpr int    NZ = 64;           // instants (~2 s)
    // TIME : durée de défilement fond -> avant (fader à gauche, 1–20 s)
    double captureMs() const { return juce::jmax(1.0, (double)timeSecA.load())*1000.0/(double)(NZ-3); }
    float& timeRef();   // valeur sauvegardée (processeur) selon le style

    // ── OpenGLRenderer (thread GL) ──
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void captureLive();         // thread GL : spectre courant -> ligne live
    const char* title() const;
    void uiTick() override;   // rafraîchissement de la lecture au survol
    float sampleH(float u, float age) const;                   // thread GL, 0..1

    // caméra : partagée entre le thread GL et l'interface
    struct Camera {
        float mvp[16];
        float eye[3], dir[3], right[3], up[3];   // repère de la caméra (tracé de rayon du survol)
        float tanHalfY, aspect;
        float shiftY { 0.f };                    // décalage vertical de l'objectif (NDC) : bord avant au bas du cadre
        float fitY { 1.f };                      // tassement vertical : le relief entier tient sous le titre
        float zoom { 1.f };                      // agrandissement de l'image (molette)
        float zoomAnchorY { 0.f };               // ligne de l'image (NDC) que le zoom ne bouge pas
        float halfX;                             // demi-largeur du relief (fréquences)
        float halfZ { 3.f };                     // demi-profondeur du relief (temps)
    };
    static Camera makeCamera(float yaw, float pitch, float aspect, float zoomAmount, float topLimit);
    float topLimit(int h) const;   // ligne du titre dans l'image (NDC)
    void pick(const Camera&, float frac, int w, int h);   // thread GL : rayon souris -> relief

    const Style style;
    std::unique_ptr<ReleaseFader> timeFader;
    std::atomic<float> timeSecA { 2.1f };
    // HOLD : image figée (plus de capture ni de défilement ; le survol reste actif)
    std::unique_ptr<NeuAnimButton> holdBtn;
    std::atomic<bool> frozen { false };
    float heldFrac { 0.f };   // thread GL : position du défilement au moment du HOLD

    // vue 3D (orbite souris), mémorisée dans le processeur ; défaut un peu
    // plus incliné qu'à l'origine
    static constexpr float kDefaultPitch = 1.018f;   // vue de face, inclinée (position validée par le user)
    std::atomic<float> yaw { 0.f }, pitch { kDefaultPitch };
    // zoom molette : -25 à +25 %, 0 = cadrage d'origine (butée aimantée), mémorisé avec la vue
    static constexpr float kMinZoom = -0.25f, kMaxZoom = 0.25f;
    std::atomic<float> zoom { 0.f };
    float& yawRef();
    float& pitchRef();
    float& zoomRef();
    std::atomic<int>   viewW { 1 }, viewH { 1 };   // zone 3D (à droite du fader TIME)
    std::atomic<int>   titleBottom { 26 };         // bas de la ligne du titre : le relief tient en dessous
    float yawAtDrag { 0.f }, pitchAtDrag { kDefaultPitch };
    bool  dragFromTitle { false };   // glisser parti du titre : pas d'orbite (détachement du module)

    // survol : position souris (interface) -> point touché (thread GL)
    std::atomic<bool>  hoverOn { false };
    std::atomic<float> hoverX { 0.f }, hoverY { 0.f };
    std::atomic<bool>  hitValid { false };
    std::atomic<float> hitU { 0.f }, hitAge { 0.f }, hitH { 0.f };
    bool  shownValid { false };
    float shownU { -1.f }, shownAge { 0.f }, shownH { 0.f };

    // données (thread GL) : anneau de captures, rows[head] = la plus récente,
    // rows[head+1] = ligne live mise à jour à chaque image
    float  rows[NZ][NX] {};
    int    head { 0 };
    juce::uint32 captureCount { 0 };
    double lastCaptureMs { 0.0 };
    PsdLogMap logMap;
    int seenAxisVersion { 0 };
    float  liveDb[NX] {};

    // objets GL
    std::unique_ptr<juce::OpenGLShaderProgram> meshShader;
    float  ageSpan() const { return (float)(NZ-3); }
    unsigned int vao { 0 }, vboGrid { 0 }, iboTris { 0 }, heightTex { 0 };
    int numTriIndices { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GpuSpectrogramDisplay)
};
