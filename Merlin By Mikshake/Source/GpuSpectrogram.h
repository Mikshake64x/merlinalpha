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
    enum class Style { Terrain, Neon, Crystal, Crystal2 };   // Crystal2 : Crystal lissé (v1.81.52)

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
    static constexpr int    NZMAX = 192;       // instants : 64 (Crystal 2 : 192, v1.81.52 « plus de lignes »)
    // TIME : durée de défilement fond -> avant (fader à gauche, 1–20 s)
    double captureMs() const { return juce::jmax(1.0, (double)timeSecA.load())*1000.0/(double)(nz-3); }
    float& timeRef();   // valeur sauvegardée (processeur) selon le style

    // ── OpenGLRenderer (thread GL) ──
    void newOpenGLContextCreated() override;
    void renderGL() override;
    void closingGL() override;

    void captureLive();         // thread GL : spectre courant -> ligne live
    void captureAt(float* row, double tMs);   // thread GL : spectre à l'instant tMs (< 0 : dernier) -> ligne
    const char* title() const;
    void uiTick() override;   // rafraîchissement de la lecture au survol
    // Bulle de lecture au survol (v1.81.57) : préparée en image sur le thread de
    // l'interface, collée par le thread de rendu. Avant, chaque changement
    // repeignait le calque 2D : le thread de rendu de JUCE (partagé par tous les
    // modules GPU) attendait alors le thread de l'interface, d'où des images en
    // retard (micro-saccades), surtout en plein écran.
    void updateBubble();
    void drawBubble(float scale);
    juce::SpinLock bubbleLock;
    juce::Image bubbleImg;                       // sous bubbleLock
    juce::Rectangle<float> bubbleBox;            // sous bubbleLock (points, repère du composant)
    int bubbleCompW { 1 }, bubbleCompH { 1 };    // sous bubbleLock
    std::atomic<int> bubbleVersion { 0 };
    std::atomic<bool> bubbleShown { false };
    int bubbleUploaded { -1 };                   // thread GL
    unsigned int bubbleTex { 0 };                // thread GL
    std::unique_ptr<juce::OpenGLShaderProgram> bubbleShader;   // thread GL
    float sampleH(float u, float age) const;                   // thread GL, 0..1

    // caméra : partagée entre le thread GL et l'interface
    struct Camera {
        float mvp[16];
        float eye[3], dir[3], right[3], up[3];   // repère de la caméra (tracé de rayon du survol)
        float tanHalfY, aspect;
        float shiftY { 0.f };                    // décalage vertical de l'objectif (NDC) : bord avant au bas du cadre
        float fitA { 1.f }, fitB { 0.f };        // y de l'image -> fitA*y+fitB : le relief entier tient dans le cadre
        float zoom { 1.f }, zoomY { 1.f };       // agrandissement de l'image (molette), en largeur et en hauteur
        float zoomAnchorY { 0.f };               // ligne de l'image (NDC) que le zoom ne bouge pas
        float halfX;                             // demi-largeur du relief (fréquences)
        float halfZ { 3.f };                     // demi-profondeur du relief (temps)
    };
    static Camera makeCamera(float yaw, float pitch, float aspect, float zoomAmount, float topLimit);
    float topLimit(int h) const;   // ligne du titre dans l'image (NDC)
    void pick(const Camera&, float frac, int w, int h);   // thread GL : rayon souris -> relief

    const Style style;
    const int   nz;   // lignes d'historique : 64, Crystal 2 : 192
    std::unique_ptr<ReleaseFader> timeFader;
    std::atomic<float> timeSecA { 2.1f };
    // HOLD : image figée (plus de capture ni de défilement ; le survol reste actif)
    std::unique_ptr<NeuAnimButton> holdBtn;
    std::atomic<bool> frozen { false };
    float heldFrac { 0.f };   // thread GL : position du défilement au moment du HOLD

    // vue 3D (orbite souris), mémorisée dans le processeur ; défaut un peu
    // plus incliné qu'à l'origine
    static constexpr float kDefaultPitch = 1.018f;   // vue de face, inclinée (position validée par le user)
    static constexpr float kMinPitch = 0.12f, kMaxPitch = 1.5f;   // jusqu'à la vue de dessus (86° : à 90° la caméra n'a plus de haut)
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
    float  rows[NZMAX][NX] {};   // seules les nz premières servent
    int    head { 0 };
    juce::uint32 captureCount { 0 };
    int rowsAdvanced { 0 };   // lignes validées depuis le dernier envoi à la texture (thread GL)
    double lastCaptureMs { 0.0 };
    PsdLogMap logMap;
    int seenAxisVersion { 0 };
    float  liveDb[NX] {};
    float  prevRow[NX] {}, curRow[NX] {};   // spectre de l'image d'avant / courant (lignes datées)
    double prevRowMs { 0.0 };

    // objets GL
    std::unique_ptr<juce::OpenGLShaderProgram> meshShader;
    float  ageSpan() const { return (float)(nz-3); }
    unsigned int vao { 0 }, vboGrid { 0 }, iboTris { 0 }, heightTex { 0 };
    int numTriIndices { 0 };
    // HD (Crystal 2, v1.81.54, aperçu merlin_crystal_hd_preview.html « HD 2 ») :
    // ombres portées (carte d'ombre depuis la lumière de contre-jour), occlusion
    // d'horizon, reflet d'environnement, lueur HDR des crêtes fortes
    std::unique_ptr<NeuAnimButton> hdBtn;
    std::atomic<bool> hdOn { false };
    std::unique_ptr<juce::OpenGLShaderProgram> depthShader, brightShader, blurShader, compShader;
    unsigned int shadowFbo { 0 }, shadowTex { 0 }, vaoQuad { 0 };
    unsigned int msFbo { 0 }, msColor { 0 }, msDepth { 0 }, sceneFbo { 0 }, sceneTex { 0 };
    unsigned int blurFbo[4] {}, blurTex[4] {};
    int targetW { 0 }, targetH { 0 };
    static constexpr int kShadowSize = 2048;
    bool ensureTargets(int pw, int ph);   // thread GL : tampons de la scène HDR et des flous
    void releaseTargets();
    void lightMvp(const Camera&, float* out) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GpuSpectrogramDisplay)
};
