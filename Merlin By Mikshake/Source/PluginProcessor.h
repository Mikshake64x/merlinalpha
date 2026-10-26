#pragma once
#include <JuceHeader.h>
#include "AppMode.h"
#include <atomic>
#include <cstdint>

class LeakAudioProcessor : public juce::AudioProcessor
{
public:
    // résolution 15/31/62 bandes (20 Hz–20 kHz), plus des bandes ultrasoniques
    // ajoutées tant que la moitié de la fréquence d'échantillonnage le permet
    static constexpr int N_BANDS_15 = 15;
    static constexpr int N_BANDS_31 = 31;
    static constexpr int N_BANDS_62 = 62;
    static constexpr int N_EXTRA_15 = 3, N_EXTRA_31 = 6, N_EXTRA_62 = 12;
    static constexpr int N_BANDS    = N_BANDS_62 + N_EXTRA_62;
    // bit d'une bande dans les masques de sélection / d'audition (64 bandes au plus)
    static constexpr uint64_t bandBit(int b) { return b>=0 && b<64 ? (1ULL<<(unsigned)b) : 0ULL; }

    enum class AnalysisMode { Stereo, Mid, Side, MS };

    // ── Système de slots : chaque slot peut accueillir n'importe quel module ──
    enum class ModuleID : int {
        Empty           = 0,
        Vectorscope     = 1,
        Oscilloscope    = 2,
        Transient       = 3,
        Loudness        = 4,
        VuMeter         = 5,
        Spectrum        = 6,
        MultibandCorr   = 7,
        Multipanorama   = 8,
        Resonance       = 9,
        SpectralDensity = 10,
        Masking         = 11,
        NeedleVU        = 12,
        MultibandVectorscope = 13,
        Spectrogram3DPeaks   = 14,
        Spectrogram3DSurface = 15,
        // spectrogrammes 3D rendus par la carte graphique (OpenGL)
        GpuSpectroTerrain    = 16,
        GpuSpectroNeon       = 17,
        GpuSpectroCrystal    = 18,
        // spectre en nuage de points (GPU), façon ChromaVerb
        DotSpectrumChroma    = 19,
        DotSpectrumNebula    = 20,
        DotPanoramaChroma    = 21,
        // vectorscopes GPU
        VectorscopePhosphor  = 22,
        VectorscopeDensity   = 23,
        PanoramaLed          = 24,
        // même rendu que le Spectrogram 3D Surface, tracé par la carte graphique
        GpuSpectroSurface    = 25
    };
    static constexpr int NUM_SLOTS = 6;
    // Assignation par défaut : Vectorscope, Multiband Corr, VU à aiguille, Loudness, Spectrum, VuMeter
    int slotModule[NUM_SLOTS] { 1, 7, 12, 4, 6, 5 };

    // Taille des ring-buffers oscilloscope / vectorscope
    // anneau de l'oscilloscope : 16384 échantillons (170 ms à 96 kHz, de quoi
    // trouver une période de basse et afficher jusqu'à 8192 échantillons)
    static constexpr int SCOPE_SIZE   = 16384;   // puissance de 2
    static constexpr int SCOPE_MAX_READ = SCOPE_SIZE - 4096;   // marge : l'audio écrit pendant la lecture
    static constexpr int VECTOR_SIZE  = 4096;
    static constexpr int TRANSIENT_W  = 512;

    LeakAudioProcessor();
    ~LeakAudioProcessor() override;

    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock   (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // mono ou stéréo, autant de sorties que d'entrées (le signal ressort intact)
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto in=l.getMainInputChannelSet();
        return (in==juce::AudioChannelSet::mono() || in==juce::AudioChannelSet::stereo())
            && in==l.getMainOutputChannelSet();
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Merlin Alpha"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram  (int) override {}
    const juce::String getProgramName  (int) override { return {}; }
    void changeProgramName  (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    int editorWidth  { 1100 };
    int editorHeight { 620  };
    // Couleurs de l'interface — 5 rôles éditables (menu Settings), les autres
    // teintes (gridCtr/border/panel/textMid/knobDim) en sont dérivées par
    // Theme::applyCustom(). Défauts = palette TEAL d'origine.
    uint32_t customAccentColor { 0xFF00FFB2 };
    uint32_t customGridColor   { 0xFF0B3030 };
    uint32_t customBgColor     { 0xFF030A0A };
    uint32_t customTextColor   { 0xFFC8FFE8 };
    uint32_t customSideColor   { 0xFFB266FF };
    uint32_t customSpectroLowColor  { 0xFF333333 };  // dégradé spectrogramme 3D — creux
    uint32_t customSpectroMidColor  { 0xFFB0B0B0 };  // dégradé spectrogramme 3D — milieu (indépendant de l'accent général)
    uint32_t customSpectroHighColor { 0xFF470000 };  // dégradé spectrogramme 3D — pics
    bool lightCpuMode { false };   // 30fps sur toutes les analyses au lieu de 60fps
    // Options > Image : cadence des analyses (30 = mode CPU léger), anticrénelage
    // et synchro verticale des spectrogrammes GPU
    int  uiFps    { 60 };
    int  gpuMsaa  { 4 };      // 0, 2, 4 ou 8 échantillons
    bool gpuVsync { true };
    // Options > Langue : indice Lang::Id (propre au poste, hors presets) ;
    // -1 = pas encore choisi -> langue de Windows au premier lancement
    int  language { -1 };

    // Template d'interface (décor autour du châssis neumorphique), choisi dans
    // la galerie du panneau Settings ou Options > Apparence. La
    // teinte (clair/sombre) et le contraste sont indépendants du template :
    // interrupteurs lune / contraste en haut à gauche.
    enum class UiTemplate : int { Stadium = 0, Forest, Neumorphic, Count };
    UiTemplate uiTemplate { UiTemplate::Neumorphic };   // seul template gardé (v1.44)
    bool lightTint    { false };
    bool highContrast { true };   // toujours actif depuis la v1.51 (demande du user)
    bool modules3D    { false };   // modules en 3D (interrupteur cube) — Spectrum : Blocks 3D
    bool largeText    { false };   // textes agrandis (interrupteur "Aa") — lecture à distance
    bool masterConsole{ false };   // section master : false = A "Instrument", true = C "Console mastering"

    // Sens de défilement du spectrogramme 3D (indépendant par module) —
    // false = ancré à droite, défile vers la gauche (défaut) ; true = inverse.
    bool spectro3dPeaksReverse   { false };
    bool spectro3dSurfaceReverse { false };
    // Perspective 3D réglable au clic-glisser (indépendante par module) —
    // tilt=intensité du recul/rétrécissement, skew=inclinaison latérale.
    float spectro3dPeaksTilt   { 0.3f };
    float spectro3dPeaksSkew   { 0.f };
    float spectro3dSurfaceTilt { 0.3f };
    float spectro3dSurfaceSkew { 0.f };
    // TIME des spectrogrammes : durée de défilement fond -> avant (secondes)
    float spectro3dPeaksTimeSec { 2.1f }, spectro3dSurfaceTimeSec { 2.1f };
    // Spectrogram Surface GPU : mêmes réglages que le 3D Surface, gardés à part
    bool  gpuSurfaceReverse { false };
    float gpuSurfaceTilt { 0.3f }, gpuSurfaceSkew { 0.f }, gpuSurfaceTimeSec { 2.1f };
    float gpuTerrainTimeSec { 2.1f }, gpuNeonTimeSec { 2.1f }, gpuCrystalTimeSec { 2.1f };
    // vue 3D des spectrogrammes GPU (orbite souris), mémorisée
    float gpuTerrainYaw { 0.f }, gpuTerrainPitch { 1.018f };
    float gpuNeonYaw    { 0.f }, gpuNeonPitch    { 1.018f };
    float gpuCrystalYaw { 0.f }, gpuCrystalPitch { 1.018f };

    // ── Presets utilisateur (menu PRESET) ───────────────────────────────────
    static juce::File getPresetFolder();   // %APPDATA%\Side Key System\Merlin Alpha Presets
    static juce::File presetFile(const juce::String& name);
    juce::StringArray getUserPresetNames() const;
    bool saveUserPreset  (const juce::String& name);   // disposition + réglages des modules + apparence
    bool loadUserPreset  (const juce::String& name);   // l'éditeur relit ensuite l'état (reloadFromProcessor)
    bool deleteUserPreset(const juce::String& name);
    bool loadFactorySoundDesign();                      // preset usine complet "Sound Design" (idem)
    bool loadFactoryTV();                               // preset usine complet "TV" (idem)
    bool applyPresetXml(const juce::XmlElement&);       // état courant + attributs du preset (hors réglages du poste)
    juce::String currentPresetName;   // dernier preset utilisateur chargé / enregistré

    // ── État persisté de la barre de contrôles et de quelques modules ──────────
    bool  peakHoldOn      { true };
    int   peakHoldIdx     { 2 };      // index dans {0.5,1,2,3,5,inf} → "2 s" par défaut
    float dbRangeVal      { 50.f };   // plage affichée du Spectrum, en dB (défaut -50dB)
    float spectrumReleaseSec { 0.4f };   // RELEASE local — module Spectrum
    float dotChromaReleaseSec { 0.4f };  // RELEASE — Dot Spectrum Chroma (même réglage que le Spectrum)
    float dotNebulaReleaseSec { 0.4f };  // RELEASE — Dot Spectrum Nebula
    float corrReleaseSec     { 0.4f };   // RELEASE local — module Multiband Corr
    float panoReleaseSec     { 0.4f };   // RELEASE local — module Panorama
    int   oscTimeDiv      { 1200 };   // fenêtre de l'oscilloscope, en échantillons (défaut 25 ms à 48 kHz)
    // fenêtre par défaut de l'oscilloscope : 25 ms au taux courant (48 kHz tant qu'il est inconnu)
    int   oscDefaultWindow() const { const double sr=getCurrentSampleRate(); return juce::roundToInt(0.025*(sr>0.0 ? sr : 48000.0)); }
    float oscAmpVal       { 1.0f };   // amplitude verticale de l'oscilloscope
    bool  oscMono         { false };  // oscilloscope : une trace (L+R)/2 au lieu de L et R
    int   oscStyle        { 0 };      // oscilloscope : 0 tube cathodique, 1 numérique, 2 vintage, 3 deux voies
    int   oscFps          { 30 };     // oscilloscope : images/s (24, 30 ou 60) — plus lisible que 60
    bool  oscPersist      { true };   // oscilloscope : rémanence des deux images précédentes
    float vecReleaseAlpha { 0.72f };  // persistance du trail du Vectorscope
    float mbVecReleaseAlpha { 0.90f };  // persistance du trail du Vectorscope multi-bande
    bool  vecLineMode     { true };   // Vectorscope : LINE (true) ou POINT (false)
    int   needleModeSel   { 0 };      // VU-mètre à aiguille : 0=LR 1=L 2=R
    float needleCalibRef  { -10.f };  // VU-mètre à aiguille : calibration 0VU en dBFS (-4 à -30)
    int   needleCalibStep { 2 };      // VU-mètre à aiguille : pas des flèches de calibration (2 ou 4 dB)

    // ── Spectre (identique Keystone) ─────────────────────────────────────
    bool getBandLevels (float* dest, int numBands) const;
    bool getSideLevels (float* dest, int numBands) const;

    struct VuData {
        float peakL{-120.f},peakR{-120.f};
        float rmsL {-120.f},rmsR {-120.f};
        float peakHoldL{-120.f},peakHoldR{-120.f};
    };
    struct LoudnessData {
        float momentary {-120.f},shortTerm {-120.f};
        float integrated{-120.f};
        float truePeakL {-120.f},truePeakR {-120.f};
        float lra       {  0.f };
    };
    bool getVuData       (VuData&)       const;
    bool getLoudnessData (LoudnessData&) const;
    void resetLoudness();
    std::atomic<int> loudnessResetCount { 0 };   // incrémenté à chaque remise à zéro (affichages TP max)
    // Dépassements true peak (> 0 dBTP) survenus depuis le dernier appel —
    // lus et remis à zéro par l'UI (voyants TP du VU-mètre).
    void consumeTruePeakOvers(bool& overL, bool& overR)
    {
        overL = tpOverL.exchange(false, std::memory_order_relaxed);
        overR = tpOverR.exchange(false, std::memory_order_relaxed);
    }

    double getCurrentSampleRate() const { return currentSampleRate; }
    // taille de la table de fréquences d'un mode (base + toutes les ultrasoniques)
    static int bandTableSize(int mode) { return mode==62 ? N_BANDS_62+N_EXTRA_62 : mode==15 ? N_BANDS_15+N_EXTRA_15 : N_BANDS_31+N_EXTRA_31; }
    // bandes affichées (base + ultrasoniques), jamais plus que la table du mode
    // courant (pendant un changement de mode, les deux valeurs bougent l'une après l'autre)
    int    getNumActiveBands()    const { return juce::jmin(numActiveBands, bandTableSize(bandMode)); }
    int    getBandMode()          const { return bandMode; }         // résolution choisie : 15, 31 ou 62
    void   setNumActiveBands (int mode);
    AnalysisMode getAnalysisMode() const { return analysisMode; }
    void         setAnalysisMode (AnalysisMode m);

    static const float BAND_FREQS_15[N_BANDS_15+N_EXTRA_15];
    static const float BAND_FREQS_31[N_BANDS_31+N_EXTRA_31];
    static const float BAND_FREQS_62[N_BANDS_62+N_EXTRA_62];
    const float* getBandFreqs() const {
        if (bandMode==62) return BAND_FREQS_62;
        if (bandMode==15) return BAND_FREQS_15;
        return BAND_FREQS_31;
    }

    // ── Oscilloscope ─────────────────────────────────────────────────────
    struct ScopeData {
        float L[SCOPE_SIZE] {};
        float R[SCOPE_SIZE] {};
        int   writePos { 0 };
    };
    // les n derniers échantillons (n <= SCOPE_MAX_READ), sans verrou ; renvoie
    juce::uint64 getScopeData (float* outL, float* outR, int n) const;

    // ── Vectorscope ──────────────────────────────────────────────────────
    struct VectorPoint { float m; float s; };
    void getVectorData (VectorPoint* out, int& count) const;
    // points arrivés depuis le dernier appel (cursor : compteur propre à
    // l'appelant) — au plus maxCount, les plus récents ; 0 si verrou occupé
    int  readNewVectorData(juce::uint64& cursor, VectorPoint* out, int maxCount) const;
    static constexpr int VECTOR_EXPORT = 512;

    // ── Vectorscope multi-bande (grave/médium/aigu) ───────────────────────
    // band : 0=grave (<250Hz) 1=médium (250Hz-3kHz) 2=aigu (>3kHz)
    static constexpr int MB_VECTOR_SIZE   = 2048;
    static constexpr int MB_VECTOR_EXPORT = 256;
    static constexpr float MB_XOVER_LO = 250.f, MB_XOVER_HI = 3000.f;
    void getMultibandVectorData(int band, VectorPoint* out, int& count) const;

    // ── Transitoires ─────────────────────────────────────────────────────
    void getTransientData (float* outPeak, int n) const;
    float transientThreshold { -18.f };

    // ── Multiband Corrélation & Panorama ─────────────────────────────────
    // corrélation normalisée [-1,+1] par bande
    bool getBandCorr (float* dest, int numBands) const;
    // position stéréo énergie [-1=gauche, 0=centre, +1=droite] par bande
    bool getBandPan  (float* dest, int numBands) const;

    // ── Densité spectrale de puissance (PSD, moyennage façon Welch) ────────
    // FFT réelle sur le signal mono (L+R)/2, indépendante de la résolution
    // 15/31/62 bandes utilisée par le filterbank du Spectrum.
    static constexpr int PSD_FFT_ORDER = 12;                 // 4096 points
    static constexpr int PSD_FFT_SIZE  = 1 << PSD_FFT_ORDER;
    static constexpr int PSD_NUM_BINS  = PSD_FFT_SIZE/2 + 1;  // DC..Nyquist inclus
    bool  getPSDData    (float* destDb, int numBins) const;
    float getPSDBinFreq (int bin) const { return (float)bin*(float)currentSampleRate/(float)PSD_FFT_SIZE; }

    // ── Audition (monitoring solo) — mono-bande ou multi-bande (bitmask) ──
    void setAuditionBand     (int b)  { auditionMask.store(bandBit(b), std::memory_order_relaxed); }
    void setAuditionBands    (uint64_t mask) { auditionMask.store(mask,std::memory_order_relaxed); }
    void toggleAuditionBand  (int b)  { if(b<0) return;
                                        uint64_t m=auditionMask.load(std::memory_order_relaxed);
                                        m ^= bandBit(b);
                                        auditionMask.store(m,std::memory_order_relaxed); }
    uint64_t getAuditionMask() const  { return auditionMask.load(std::memory_order_relaxed); }
    void setAuditionTransient(bool e) { auditionTransient.store(e,std::memory_order_relaxed); }
    void clearAudition()              { auditionMask.store(0ULL,std::memory_order_relaxed);
                                        auditionTransient.store(false,std::memory_order_relaxed); }

private:
    // ── Filtres spectre (copie exacte Keystone) ───────────────────────────
    struct BandFilter {
        float x1=0,x2=0,y1=0,y2=0;
        float b0=0,b1=0,b2=0,a1=0,a2=0;
        float peakAcc = 0.f;
        void  reset()  { x1=x2=y1=y2=peakAcc=0.f; }
        void  design   (float fc, float fs, float Q);
        float process  (float x);
    };
    BandFilter filtersA[N_BANDS];
    BandFilter filtersB[N_BANDS];

    // ── Filterbanks corrélation/panorama (L et R séparés, toujours actifs) ──
    BandFilter corrL[N_BANDS];
    BandFilter corrR[N_BANDS];

    // ── Crossover Butterworth 2nd ordre (vectorscope multi-bande) ─────────
    struct XoverFilter {
        float x1=0,x2=0,y1=0,y2=0;
        float b0=0,b1=0,b2=0,a1=0,a2=0;
        void  reset()  { x1=x2=y1=y2=0.f; }
        void  designLP(float fc, float fs);
        void  designHP(float fc, float fs);
        float process (float x);
    };
    // Décimation L/R séparée (peak-hold identique à decimAccA/S)
    float decimAccL { 0.f };
    float decimAccR { 0.f };
    // Accumulateurs EMA par bande
    float corrAccLL[N_BANDS] {};   // E[L²]  par bande
    float corrAccRR[N_BANDS] {};   // E[R²]  par bande
    float corrAccLR[N_BANDS] {};   // E[L×R] par bande
    // Buffers double-tampon (pattern identique à bandBufDB)
    float corrBuf[2][N_BANDS] {};  // corrélation normalisée
    float panBuf [2][N_BANDS] {};  // position stéréo
    std::atomic<int> corrReadIdx { 0 };
    float corrAlpha { 0.f };       // coefficient EMA, calculé dans rebuildFilters()

    float bandBufDB[2][N_BANDS] {};
    float sideBufDB[2][N_BANDS] {};
    std::atomic<int> bandReadIdx { 0 };
    std::atomic<int> sideReadIdx { 0 };

    // ── PSD : FFT temps réel + moyennage façon Welch ──────────────────────
    // NB : nécessite le module juce_dsp dans le projet Projucer.
    juce::dsp::FFT psdFFT { PSD_FFT_ORDER };
    juce::dsp::WindowingFunction<float> psdWindowFn { (size_t)PSD_FFT_SIZE, juce::dsp::WindowingFunction<float>::hann };
    static constexpr int PSD_HOP = PSD_FFT_SIZE/4;   // 75% de recouvrement
    float psdRing[PSD_FFT_SIZE] {};
    int   psdRingWrite  { 0 };
    int   psdSamplesAcc { 0 };
    float psdFFTBuf[2*PSD_FFT_SIZE] {};
    float psdAvgLin[PSD_NUM_BINS] {};     // puissance moyenne (EMA), domaine linéaire
    float psdAlpha { 0.f }, psdAlphaFast { 0.f };   // lissage normal / retombée rapide (son coupé)
    float psdBuf[2][PSD_NUM_BINS] {};
    std::atomic<int> psdReadIdx { 0 };
    void  computePSDFrame();

    float runPeakL  { 0.f }, runPeakR  { 0.f };
    float vuRmsAccL { 0.f }, vuRmsAccR { 0.f };
    float vuHoldL   { 0.f }, vuHoldR   { 0.f };
    int   vuHoldFrameL { 0 }, vuHoldFrameR { 0 };
    float tpL { 0.f }, tpR { 0.f };

    // Vrai true peak ITU-R BS.1770-4 (annexe 2) : interpolation x4 par filtre
    // polyphase 48 coefficients (4 phases x 12), max des valeurs absolues —
    // détecte les crêtes inter-échantillons que la crête échantillon ignore.
    struct TruePeakDetector {
        float hist[24] {};   // fenêtre de 12 échantillons, doublée (pas de modulo)
        int   pos { 0 };
        void  reset() { std::fill(std::begin(hist), std::end(hist), 0.f); pos = 0; }
        float process(float x) noexcept;
    };
    TruePeakDetector tpDetL, tpDetR;
    std::atomic<bool> tpOverL { false }, tpOverR { false };

    static constexpr int   VU_HOLD_SAMPLES    = 24000;
    static constexpr float TP_DECAY           = 0.99998f;
    static constexpr float RELEASE_ALPHA      = 0.9990f;
    static constexpr int   UPDATE_INTERVAL    = 64;
    static constexpr int   VU_EXPORT_INTERVAL = 128;

    float rmsCoeff    { 0.f };
    float holdRelease { 0.f };

    // ── Loudness LUFS (ITU-R BS.1770-4 / EBU R128) ─────────────────────────
    // Filtres de pondération K — étage 1 (shelf HF) puis étage 2 (passe-haut "RLB")
    BandFilter kPreL, kPreR;
    BandFilter kRlbL, kRlbR;
    float kBlockSumL { 0.f }, kBlockSumR { 0.f };   // somme des carrés K-pondérés du bloc courant

    // Fenêtres glissantes momentanée (400ms) et court-terme (3000ms), en nombre
    // de blocs VU_EXPORT_INTERVAL — le nombre de blocs actifs est recalculé dans
    // prepareToPlay() selon le sample rate réel (tableaux dimensionnés au max).
    static constexpr int MOM_FRAMES_MAX = 700;   // couvre jusqu'à ~192kHz
    static constexpr int ST_FRAMES_MAX  = 5000;
    float momBuf[MOM_FRAMES_MAX] {};   // puissance stéréo linéaire (z_L+z_R) par bloc
    float stBuf [ST_FRAMES_MAX]  {};
    int   momFramesActive { 24 }, stFramesActive { 180 };
    int   momHead { 0 }, stHead { 0 };
    float momSum  { 0.f }, stSum { 0.f };

    // Histogrammes de gating (BS.1770-4 §intégré / EBU Tech 3342 §LRA)
    // Bins de 0.1 LU de -70 à +5 LUFS
    static constexpr int   GATE_BINS     = 750;
    static constexpr float GATE_MIN_LUFS = -70.f;
    static constexpr float GATE_BIN_W    = 0.1f;
    float integHist[GATE_BINS] {};   // blocs de 100ms (= valeur momentanée échantillonnée)
    float lraHist  [GATE_BINS] {};   // blocs de 1s    (= valeur court-terme échantillonnée)
    int   integBlockSamples { 4800 }, integBlockCounter { 0 };
    int   lraBlockSamples   { 48000 },lraBlockCounter   { 0 };
    float integratedLufs { -120.f };
    float lraValue        { 0.f };

    void  designKWeighting(float fs);
    void  addGateBlock(float* hist, float lufs);
    float gateMeanLUFS(const float* hist, float thresholdLUFS) const;
    float gatePercentileLUFS(const float* hist, float thresholdLUFS, float fraction) const;
    void  recomputeIntegrated();
    void  recomputeLRA();

    mutable juce::SpinLock vuLock;
    VuData       vuExport;
    LoudnessData loudExport;

    double       currentSampleRate { 44100.0 };
    int          numActiveBands    { N_BANDS_31 };
    int          bandMode          { N_BANDS_31 };
    AnalysisMode analysisMode      { AnalysisMode::Stereo };

    int   updateCounter   { 0 };
    int   vuExportCounter { 0 };
    int   decimCounter    { 0 };
    float decimAccA       { 0.f };
    float decimAccS       { 0.f };

    void rebuildFilters();
    void updateLevels();
    void updateVuExport();

    // ── Oscilloscope ─────────────────────────────────────────────────────
    ScopeData scopeBuf;
    std::atomic<juce::uint64> scopeTotal { 0 };   // échantillons écrits depuis prepareToPlay, publié après l'écriture (sans verrou)

    // ── Vectorscope ──────────────────────────────────────────────────────
    mutable juce::SpinLock vectorLock;
    VectorPoint vectorBuf[VECTOR_SIZE] {};
    int         vectorWrite { 0 };
    int         vectorCount { 0 };
    juce::uint64 vectorTotal { 0 };   // points écrits depuis le prepare

    // ── Vectorscope multi-bande ────────────────────────────────────────────
    XoverFilter mbLowL, mbLowR, mbHighL, mbHighR;
    XoverFilter mbMidHpL, mbMidHpR, mbMidLpL, mbMidLpR;
    mutable juce::SpinLock mbVectorLock;
    VectorPoint mbVectorBuf[3][MB_VECTOR_SIZE] {};
    int         mbVectorWrite[3] {};
    int         mbVectorCount[3] {};

    // ── Transitoires ─────────────────────────────────────────────────────
    mutable juce::SpinLock transientLock;
    float transientCols[TRANSIENT_W] {};
    int   transientWrite    { 0 };
    float transientAccum    { 0.f };
    int   transientSampPerCol { 256 };
    int   transientSampCount  { 0 };

    // ── Audition ─────────────────────────────────────────────────────────
    std::atomic<uint64_t> auditionMask  { 0 };
    std::atomic<bool>     auditionTransient { false };
    // Un état de filtre SVF passe-bande par bande (jusqu'à N_BANDS=62) —
    // permet d'auditionner plusieurs bandes simultanément en sommant leurs sorties.
    struct AuditionBandState {
        float a1=0,a2=0,a3=0,g=0,makeup=1.f;
        float ic1L=0,ic2L=0,ic1R=0,ic2R=0;
        bool  designed=false;
    };
    AuditionBandState auditionStates[N_BANDS];
    int   auditionStatesConfigBands { -1 }; // numActiveBands utilisé lors du dernier design des filtres
    float trAtkFast{0.f},trRelFast{0.f},trAtkSlow{0.f},trRelSlow{0.f};
    float trEnvFastL{0.f},trEnvSlowL{0.f},trEnvFastR{0.f},trEnvSlowR{0.f};
    float trGainL{0.f},trGainR{0.f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LeakAudioProcessor)
};
