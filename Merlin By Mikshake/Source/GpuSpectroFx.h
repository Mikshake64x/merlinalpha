#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include "GpuModule.h"

class ReleaseFader;   // PluginEditor.h
class NeuAnimButton;  // PluginEditor.h

//==============================================================================
// GpuSpectroFxDisplay — cinq spectrogrammes d'ambiance rendus par la carte
// graphique (v1.81.37, aperçus validés : preview/merlin_spectros_preview.html) :
//   Bars     barres 3D, une par bande (15 / 31 / 62 comme VIEW), reflet au sol
//   Led      colonnes de points façon afficheur LED, en miroir, traits de glitch
//   Ribbons  nappe de fils colorés ondulants, flou de profondeur (bokeh)
//   Matrix   relief en fil de fer vert, transparent
//   Splash   surface liquide qui suit le spectre, gouttes sur les attaques
//
// Mêmes commandes que les spectrogrammes GPU Terrain / Néon / Cristal
// (v1.81.38) : fader TIME (durée de l'historique affiché, 1 à 20 s), HOLD,
// orbite à la souris (aimant sur les vues droites), zoom molette ±25 %,
// double-clic = vue d'origine, lecture au survol (fréquence, niveau, âge),
// repères de fréquence et de temps ; vue et TIME mémorisés par style.
//
// Historique : anneau de 64 lignes (160 fréquences log, PSD 4096 points du
// processeur) dans une texture ; le relief est calculé dans les shaders.
// Lueur : scène rendue dans un tampon multi-échantillonné en virgule
// flottante, passe claire, flou à demi et quart de résolution, composition
// avec la courbe de tonalité des aperçus.
//==============================================================================
class GpuSpectroFxDisplay : public GpuModuleBase
{
public:
    enum class Style { Bars, Led, Ribbons, Matrix, Splash };

    GpuSpectroFxDisplay(LeakAudioProcessor&, Style);
    ~GpuSpectroFxDisplay() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void syncFromProcessor();   // preset rappelé : TIME et vue relus

    // caméra (orbite autour du centre de la scène), partagée par le rendu,
    // le survol et les repères du calque 2D
    struct Cam {
        float mvp[16];
        float eye[3], dir[3], right[3], up[3];
        float tanHalfY { 0.4f }, aspect { 1.f };
    };

private:
    static constexpr int NX = 160;    // fréquences (colonnes de la texture)
    static constexpr int NZ = 64;     // instants (lignes de l'anneau)
    static constexpr int NB = 62;     // bandes au plus (Bars)

    const Style style;
    const char* title() const;
    int   span() const;                                  // lignes affichées
    double captureMs() const;                            // TIME / lignes affichées
    Cam   makeCam(float yawOff, float pitchOff, float zoomAmount, float aspect) const;
    float& timeRef();
    float& yawRef();
    float& pitchRef();
    float& zoomRef();

    // ── thread GL ──
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void uiTick() override;
    // Précision (v1.81.40) : la ligne en cours garde la crête de toutes les images
    // de sa durée (aucun coup bref perdu en TIME long) et se remplit en direct au
    // fond ; à la fin de sa durée, elle est validée et une nouvelle commence.
    void accumulate(bool startNew);         // chaque image : crête dans la ligne en cours
    void commit();                          // fin de durée : la ligne en cours est validée
    int  liveRow() const { return (head+1)%NZ; }
    float rowDZ() const;                    // profondeur d'une ligne (Barres : selon TIME)
    bool ensureTargets(int pw, int ph);     // tampons de la lueur à la taille de la vue
    void releaseTargets();
    void buildGeometry();
    void drawScene(const Cam&, float t, float frac, float dt, int pw, int ph);
    void updateParticles(float dt);         // gouttes (Splash), traits de glitch (Led)
    float levelAt(float u, float age) const;            // historique, bilinéaire (thread GL)
    float surfaceY(float u, float age, float t) const;  // hauteur de la scène, comme le shader
    void pick(const Cam&, float frac, float t, int w, int h);

    PsdLogMap logMap;
    float liveDb[PsdLogMap::N] {};
    float rows[NZ][NX] {};
    float bandRows[NZ][NB] {};
    int   head { 0 }, seenAxis { -1 }, bandsShown { 0 };
    double lastCaptureMs { 0.0 }, lastFrameMs { 0.0 };
    float animT { 0.f };                    // horloge des animations (arrêtée par HOLD)
    float heldFrac { 0.f };                 // position du défilement au moment du HOLD
    std::atomic<int> viewW { 1 }, viewH { 1 }, titleBottom { 26 };

    // commandes (comme le Cristal)
    std::unique_ptr<ReleaseFader> timeFader;
    std::unique_ptr<NeuAnimButton> holdBtn;
    std::atomic<float> timeSecA { 2.f };
    std::atomic<bool>  frozen { false };
    std::atomic<float> yaw { 0.f }, pitch { 0.f }, zoom { 0.f };   // écarts à la vue d'origine du style
    float yawAtDrag { 0.f }, pitchAtDrag { 0.f };
    bool  dragFromTitle { false };

    // survol : position souris (interface) -> point touché (thread GL)
    std::atomic<bool>  hoverOn { false }, hitValid { false };
    std::atomic<float> hoverX { 0.f }, hoverY { 0.f };
    std::atomic<float> hitU { 0.f }, hitAge { 0.f }, hitLevel { 0.f };
    float hitPos[3] {};
    bool  shownValid { false };
    float shownU { -1.f }, shownAge { 0.f }, shownLevel { 0.f };

    // objets GL
    std::unique_ptr<juce::OpenGLShaderProgram> sceneShader, pointShader, brightShader, blurShader, compShader;
    unsigned int histTex { 0 }, bandTex { 0 };
    unsigned int vaoScene { 0 }, vboScene { 0 }, iboScene { 0 };
    unsigned int vaoQuad { 0 }, vboQuad { 0 }, vaoPts { 0 }, vboPts { 0 };
    int sceneVerts { 0 }, sceneIndices { 0 };
    // lueur : scène multi-échantillonnée, résolue, puis flous (0-1 demi, 2-3 quart)
    unsigned int msFbo { 0 }, msColor { 0 }, msDepth { 0 }, sceneFbo { 0 }, sceneTex { 0 };
    unsigned int blurFbo[4] {}, blurTex[4] {};
    int targetW { 0 }, targetH { 0 };

    // particules calculées sur le processeur (Splash : gouttes ; Led : glitch)
    struct Particle { float x, y, z, vx, vy, vz, r, g, b, life; };
    std::vector<Particle> parts;
    struct Streak { float x, y, z, vx, len, life, r, g, b; };
    std::vector<Streak> streaks;
    std::vector<float> ptVerts;             // x, y, z, r, g, b
    float prevBand[40] {};
    juce::Random rng { 7 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GpuSpectroFxDisplay)
};
