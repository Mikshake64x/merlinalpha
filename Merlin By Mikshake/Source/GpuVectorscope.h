#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include "GpuModule.h"

//==============================================================================
// GpuVectorscopeDisplay — vectorscope (M/S, L en haut à gauche, R en haut à
// droite) rendu par la carte graphique, deux rendus validés sur preview :
//   Phosphor : trace analogique — chaque segment est ajouté dans une texture
//              flottante qui s'estompe (rémanence), plus lumineux là où le
//              faisceau va lentement, puis composé avec halo et coeur blanc
//   Density  : carte de densité — grille accumulée (avec oubli) lissée et
//              colorée (inferno) par le shader
// Gain automatique (le nuage occupe l'écran même à bas niveau) et barre de
// corrélation sous le scope.
//==============================================================================
class GpuVectorscopeDisplay : public GpuModuleBase
{
public:
    enum class Style { Phosphor, Density };

    GpuVectorscopeDisplay(LeakAudioProcessor&, Style);
    ~GpuVectorscopeDisplay() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int NG = 192;   // grille de densité

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    // corrélation + gain : calque repeint seulement si l'affichage change
    void uiTick() override
    {
        const float c=corrUi.load(), gdb=gainDbUi.load();
        if(std::abs(c-shownCorr)>0.005f || juce::roundToInt(gdb)!=juce::roundToInt(shownGainDb))
        { shownCorr=c; shownGainDb=gdb; repaint(); }
    }
    float shownCorr { 2.f }, shownGainDb { -100.f };

    void readPoints(float dt);
    bool ensureAccumTarget(int pw, int ph);   // Phosphor : texture de rémanence
    const char* title() const;

    const Style style;

    // géométrie du scope (px logiques), lue par le thread GL
    std::atomic<float> scopeCx { 0.f }, scopeCy { 0.f }, scopeR { 1.f };
    std::atomic<int>   viewW { 1 }, viewH { 1 };

    // affichage
    std::atomic<float> corrUi { 0.f }, gainDbUi { 0.f };

    // données (thread GL)
    juce::uint64 cursor { 0 };
    std::vector<LeakAudioProcessor::VectorPoint> pts;
    int    numPts { 0 };
    float  lastS { 0.f }, lastM { 0.f };   // dernier point de l'image précédente (trace continue)
    bool   haveLast { false };
    float  env { 0.05f }, gain { 1.f };
    double sLR { 0.0 }, sLL { 0.0 }, sRR { 0.0 };
    double lastFrameMs { 0.0 };
    std::vector<float> lineVerts;          // x, y, intensité
    std::vector<float> density;            // NG x NG
    float  densityMax { 1.f };

    // objets GL
    std::unique_ptr<juce::OpenGLShaderProgram> fadeShader, lineShader, phosphorShader, densityShader;
    unsigned int vaoQuad { 0 }, vaoLines { 0 }, vboLines { 0 };
    unsigned int accumFbo { 0 }, accumTex { 0 }, densityTex { 0 };
    int accumW { 0 }, accumH { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GpuVectorscopeDisplay)
};
