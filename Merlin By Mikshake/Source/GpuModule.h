#pragma once
#include <JuceHeader.h>
#include <atomic>
#include "PluginProcessor.h"

struct GpuFramePacer;   // GpuModule.cpp : horloge d'images commune (fil dédié)

//==============================================================================
// GpuModuleBase — socle des modules rendus par la carte graphique (OpenGL 3.2
// core) : contexte attaché au composant, réglages d'Options > Image
// (anticrénelage, V-Sync, images/s), suspension quand un panneau de l'éditeur
// doit passer devant, cadre de l'écran et message d'erreur.
//
// Contraintes Windows : la zone OpenGL est une fenêtre native, rien ne peut
// être peint par-dessus. Le calque 2D (titre, repères, coins arrondis, bord
// du verre) est donc peint par paint() et composé par JUCE au-dessus du rendu
// GL, et l'éditeur suspend le rendu quand About / Settings est ouvert.
//
// IMPORTANT : chaque classe dérivée doit appeler detachGL() dans son
// destructeur (le thread GL ne doit plus appeler ses fonctions virtuelles une
// fois sa destruction commencée).
//==============================================================================
class GpuModuleBase : public juce::Component, protected juce::OpenGLRenderer, protected juce::Timer
{
public:
    explicit GpuModuleBase(LeakAudioProcessor&);
    ~GpuModuleBase() override;

    // caché : le fil de cadence s'arrête AVANT que JUCE ne libère la fenêtre GL
    void visibilityChanged() override      { if(!isVisible()) quiescePacer(); updateAttachment(); updateUiTimer(); }
    void parentHierarchyChanged() override { updateAttachment(); updateRenderActive(); }

    // Options > Image : anticrénelage (0/2/4/8), synchro verticale, images/s
    // (>= 60 : calé sur l'écran, sinon cadence fixe)
    void setGraphics(int msaaSamples, bool vsync, int fps);
    // réapplique la cadence (un module qui la plafonne a changé de plafond)
    void refreshFrameRate() { setGraphics(msaa, vsyncWanted.load(), requestedFps); }
    // plafond propre au module (ex. oscilloscope : Options > cadence)
    virtual int maxFrameRate() const { return 1000; }
    // carte graphique + version OpenGL du dernier contexte créé (vide avant)
    static juce::String getRendererInfo();

    // vrai quand un panneau de l'éditeur doit passer devant le rendu GL
    void setSuspended(bool shouldSuspend) { if(shouldSuspend!=suspended){ suspended=shouldSuspend; updateAttachment(); } }

protected:
    void detachGL();
    bool isGlAttached() const { return context.isAttached(); }

    // ── à appeler depuis le thread GL ──
    // compile + lie un programme (nullptr en cas d'échec : glFailed est levé)
    std::unique_ptr<juce::OpenGLShaderProgram> compileProgram(const char* vs, const char* fs, const char* gs=nullptr);
    void onContextCreated();     // mémorise la carte, réarme la V-Sync
    void applyVsync();           // en tête de renderOpenGL()
    float renderingScale() const { return (float)context.getRenderingScale(); }
    // anticrénelage étendu à l'intérieur des surfaces (ombrage par
    // échantillon) : à placer autour du dessin des reliefs
    void beginSampleShading();
    void endSampleShading();

    // ── calque 2D ──
    void paintScreenFrame(juce::Graphics&);   // coins arrondis + bord du verre
    void paintGlFailure(juce::Graphics&);     // message si OpenGL indisponible

    // cadence fixe (< 60 i/s) puis uiTick() ~30 fois par seconde
    void timerCallback() override;
    virtual void uiTick() {}

    LeakAudioProcessor& processor;
    juce::OpenGLContext context;
    std::atomic<bool> glFailed { false };

private:
    friend struct GpuFramePacer;
    void updateAttachment();
    void updateUiTimer();        // timer actif seulement à l'écran
    void updateRenderActive();   // à l'écran et attaché : le fil de cadence peut déclencher des images
    void quiescePacer();         // plus d'image déclenchée par le fil de cadence (avant un détachement)
    // fil de cadence : une image par rafraîchissement de l'écran (V-Sync),
    // sans dépasser frameHz, à pas fixe sur l'horloge des images
    void onFrameTick(double timestampSec);
    void applyPixelFormat();

    bool suspended { false };
    int  msaa { 4 }, frameHz { 60 }, requestedFps { 60 };
    std::atomic<int>  frameHzA { 60 };
    std::atomic<bool> vsyncWanted { true };
    std::atomic<bool> renderActive { false };
    int  vsyncApplied { -1 };   // thread GL
    bool sampleShadingOk { false };   // thread GL : contexte multi-échantillonné + ombrage par échantillon dispo
    void* minSampleShadingFn { nullptr };   // glMinSampleShadingARB
    std::shared_ptr<GpuFramePacer> pacer;
    double lastTickTs { 0.0 }, framePeriod { 1.0/60.0 }, nextFrameTs { 0.0 };   // fil de cadence uniquement
    int    uiTickDiv { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GpuModuleBase)
};

//==============================================================================
// PsdLogMap — 160 points log 20 Hz–20 kHz lus dans la PSD 4096 points du
// processeur (max des bins couverts dans les aigus, interpolation dans les
// graves). Même correspondance que les spectrogrammes.
//==============================================================================
struct PsdLogMap
{
    static constexpr int N = 160;
    // remplit outDb[N] ; false tant que la fréquence d'échantillonnage est inconnue
    bool read(LeakAudioProcessor&, float* outDb);
    static float freqAt(float u) { return 20.f*std::pow(1000.f, u); }   // u 0..1 (axe 20 Hz–20 kHz)

    // Axe étendu (spectrogrammes, Dot Spectrum) : jusqu'à la moitié de la
    // fréquence d'échantillonnage (44,1 kHz -> 22,05 kHz, 48 kHz -> 24 kHz,
    // 96 kHz -> 48 kHz), 20 kHz tant qu'elle est inconnue. Sans axe étendu :
    // toujours 20 Hz–20 kHz.
    static float topHz(double sampleRate) { return sampleRate>0.0 ? (float)(sampleRate*0.5) : 20000.f; }
    void setExtendedAxis(bool b) { if(b!=extendedAxis){ extendedAxis=b; mappedSampleRate=0.0; } }

private:
    void rebuild(double sampleRate);
    int    binLo[N] {}, binHi[N] {};
    float  binFrac[N] {};
    bool   extendedAxis { false };
    double mappedSampleRate { 0.0 };
    float  psd[LeakAudioProcessor::PSD_NUM_BINS] {};
};
