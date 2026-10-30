#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

// après les bandes de base : bandes ultrasoniques (même pas), retenues
// seulement si la fréquence d'échantillonnage permet de les mesurer
const float LeakAudioProcessor::BAND_FREQS_15[N_BANDS_15+N_EXTRA_15] = {
    25.f,40.f,63.f,100.f,160.f,250.f,400.f,630.f,
    1000.f,1600.f,2500.f,4000.f,6300.f,10000.f,16000.f,
    25000.f,40000.f,63000.f
};
const float LeakAudioProcessor::BAND_FREQS_31[N_BANDS_31+N_EXTRA_31] = {
    20.f,25.f,31.5f,40.f,50.f,63.f,80.f,100.f,125.f,160.f,200.f,250.f,315.f,400.f,500.f,
    630.f,800.f,1000.f,1250.f,1600.f,2000.f,2500.f,3150.f,4000.f,5000.f,6300.f,8000.f,
    10000.f,12500.f,16000.f,20000.f,
    25000.f,31500.f,40000.f,50000.f,63000.f,80000.f
};
const float LeakAudioProcessor::BAND_FREQS_62[N_BANDS_62+N_EXTRA_62] = {
    20.f,22.4f,25.f,28.f,31.5f,35.5f,40.f,45.f,50.f,56.f,63.f,71.f,
    80.f,90.f,100.f,112.f,125.f,140.f,160.f,180.f,200.f,224.f,250.f,280.f,
    315.f,355.f,400.f,450.f,500.f,560.f,630.f,710.f,800.f,900.f,1000.f,1120.f,
    1250.f,1400.f,1600.f,1800.f,2000.f,2240.f,2500.f,2800.f,3150.f,3550.f,4000.f,4500.f,
    5000.f,5600.f,6300.f,7100.f,8000.f,9000.f,10000.f,11200.f,12500.f,14000.f,16000.f,18000.f,
    20000.f,22400.f,
    25000.f,28000.f,31500.f,35500.f,40000.f,45000.f,50000.f,56000.f,63000.f,71000.f,80000.f,90000.f
};

void LeakAudioProcessor::BandFilter::design(float fc, float fs, float Q)
{
    fc = juce::jlimit(10.f, fs*0.45f, fc);
    const float w0    = juce::MathConstants<float>::twoPi * fc / fs;
    const float sw    = std::sin(w0), cw = std::cos(w0);
    const float alpha = sw / (2.f*Q);
    const float a0i   = 1.f / (1.f+alpha);
    b0 = alpha*a0i; b1 = 0.f; b2 = -alpha*a0i;
    a1 = -2.f*cw*a0i; a2 = (1.f-alpha)*a0i;
    reset();
}
float LeakAudioProcessor::BandFilter::process(float x)
{
    float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
    x2=x1; x1=x; y2=y1; y1=y;
    return y;
}

// Crossover Butterworth 2nd ordre (RBJ cookbook, Q=1/sqrt(2)) — séparation
// grave/médium/aigu pour le vectorscope multi-bande. Purement analytique
// (n'affecte pas le signal de sortie), donc pas besoin d'une paire
// Linkwitz-Riley à sommation plate : un simple Butterworth par bande suffit.
void LeakAudioProcessor::XoverFilter::designLP(float fc, float fs)
{
    fc = juce::jlimit(10.f, fs*0.45f, fc);
    const float w0=juce::MathConstants<float>::twoPi*fc/fs;
    const float sw=std::sin(w0), cw=std::cos(w0);
    const float alpha=sw/(2.f*0.70710678f);
    const float a0i=1.f/(1.f+alpha);
    b0=(1.f-cw)*0.5f*a0i; b1=(1.f-cw)*a0i; b2=(1.f-cw)*0.5f*a0i;
    a1=-2.f*cw*a0i; a2=(1.f-alpha)*a0i;
    reset();
}
void LeakAudioProcessor::XoverFilter::designHP(float fc, float fs)
{
    fc = juce::jlimit(10.f, fs*0.45f, fc);
    const float w0=juce::MathConstants<float>::twoPi*fc/fs;
    const float sw=std::sin(w0), cw=std::cos(w0);
    const float alpha=sw/(2.f*0.70710678f);
    const float a0i=1.f/(1.f+alpha);
    b0=(1.f+cw)*0.5f*a0i; b1=-(1.f+cw)*a0i; b2=(1.f+cw)*0.5f*a0i;
    a1=-2.f*cw*a0i; a2=(1.f-alpha)*a0i;
    reset();
}
float LeakAudioProcessor::XoverFilter::process(float x)
{
    float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
    x2=x1; x1=x; y2=y1; y1=y;
    return y;
}

LeakAudioProcessor::LeakAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    for (int b=0;b<2;++b) {
        std::fill(std::begin(bandBufDB[b]),std::end(bandBufDB[b]),-120.f);
        std::fill(std::begin(sideBufDB[b]),std::end(sideBufDB[b]),-120.f);
        std::fill(std::begin(corrBuf[b]),  std::end(corrBuf[b]),   0.f);
        std::fill(std::begin(panBuf[b]),   std::end(panBuf[b]),    0.f);
    }
    std::fill(std::begin(momBuf),std::end(momBuf),-120.f);
    std::fill(std::begin(stBuf), std::end(stBuf), -120.f);
    std::fill(std::begin(corrAccLL),std::end(corrAccLL),0.f);
    std::fill(std::begin(corrAccRR),std::end(corrAccRR),0.f);
    std::fill(std::begin(corrAccLR),std::end(corrAccLR),0.f);
}
LeakAudioProcessor::~LeakAudioProcessor() {}

void LeakAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sampleRate;
    setNumActiveBands(bandMode);   // bandes jusqu'à la moitié de la nouvelle fréquence (+ filtres)
    rmsCoeff    = 1.f - std::exp(-1.f / (0.3f*(float)sampleRate));
    holdRelease = std::exp(-1.f        / (0.8f*(float)sampleRate));

    // Pondération K (BS.1770-4) — coefficients redessinés pour le sample rate réel
    designKWeighting((float)sampleRate);
    kBlockSumL=kBlockSumR=0.f;

    // Fenêtres momentanée (400ms) / court-terme (3000ms) en nombre de blocs
    // VU_EXPORT_INTERVAL — calculées pour correspondre à une durée réelle fixe
    // quel que soit le sample rate (corrige les fenêtres ~65ms/~480ms précédentes)
    const float frameSec=(float)VU_EXPORT_INTERVAL/(float)sampleRate;
    momFramesActive=juce::jlimit(1,MOM_FRAMES_MAX,(int)std::round(0.400f/frameSec));
    stFramesActive =juce::jlimit(1,ST_FRAMES_MAX, (int)std::round(3.000f/frameSec));

    // Blocs de gating : 100ms (intégré) et 1s (LRA), en échantillons audio
    integBlockSamples=juce::jmax(1,(int)std::round(0.100*sampleRate));
    lraBlockSamples  =juce::jmax(1,(int)std::round(1.000*sampleRate));

    resetLoudness();
    runPeakL=runPeakR=vuRmsAccL=vuRmsAccR=0.f;
    vuHoldL=vuHoldR=0.f; vuHoldFrameL=vuHoldFrameR=0;
    tpL=tpR=0.f;
    tpDetL.reset(); tpDetR.reset();
    for (int b=0;b<2;++b) {
        std::fill(std::begin(bandBufDB[b]),std::end(bandBufDB[b]),-120.f);
        std::fill(std::begin(sideBufDB[b]),std::end(sideBufDB[b]),-120.f);
        std::fill(std::begin(corrBuf[b]),  std::end(corrBuf[b]),   0.f);
        std::fill(std::begin(panBuf[b]),   std::end(panBuf[b]),    0.f);
    }
    // Reset accumulateurs corrélation/panorama
    std::fill(std::begin(corrAccLL),std::end(corrAccLL),0.f);
    std::fill(std::begin(corrAccRR),std::end(corrAccRR),0.f);
    std::fill(std::begin(corrAccLR),std::end(corrAccLR),0.f);
    decimAccL=decimAccR=0.f;
    for(int i=0;i<numActiveBands;++i){ corrL[i].reset(); corrR[i].reset(); }

    updateCounter=vuExportCounter=decimCounter=0;
    std::fill(std::begin(scopeBuf.L),std::end(scopeBuf.L),0.f);
    std::fill(std::begin(scopeBuf.R),std::end(scopeBuf.R),0.f);
    scopeBuf.writePos=0;
    scopeTotal.store(0, std::memory_order_release);
    std::fill(std::begin(vectorBuf),std::end(vectorBuf),VectorPoint{0.f,0.f});
    vectorWrite=vectorCount=0; vectorTotal=0;
    mbLowL.designLP(MB_XOVER_LO,(float)sampleRate);  mbLowR.designLP(MB_XOVER_LO,(float)sampleRate);
    mbHighL.designHP(MB_XOVER_HI,(float)sampleRate); mbHighR.designHP(MB_XOVER_HI,(float)sampleRate);
    mbMidHpL.designHP(MB_XOVER_LO,(float)sampleRate); mbMidHpR.designHP(MB_XOVER_LO,(float)sampleRate);
    mbMidLpL.designLP(MB_XOVER_HI,(float)sampleRate); mbMidLpR.designLP(MB_XOVER_HI,(float)sampleRate);
    for(int b=0;b<3;++b){
        std::fill(std::begin(mbVectorBuf[b]),std::end(mbVectorBuf[b]),VectorPoint{0.f,0.f});
        mbVectorWrite[b]=mbVectorCount[b]=0;
    }

    // ── PSD : reset du ring-buffer FFT et recalcul du coefficient de lissage ──
    std::fill(std::begin(psdRing),std::end(psdRing),0.f);
    psdRingWrite=0; psdSamplesAcc=0;
    std::fill(std::begin(psdAvgLin),std::end(psdAvgLin),0.f);
    for(int b=0;b<2;++b) std::fill(std::begin(psdBuf[b]),std::end(psdBuf[b]),0.f);
    {
        const float frameInterval=(float)PSD_HOP/(float)sampleRate; // intervalle entre deux trames FFT
        const float avgTau=0.3f;                                     // constante de lissage ~300ms (façon Welch)
        psdAlpha=1.f-std::exp(-frameInterval/avgTau);
        // son coupé : retombée rapide (~108 dB/s au lieu de ~14 dB/s, qui
        // laissait une traîne de 5 s sur les spectrogrammes)
        psdAlphaFast=1.f-std::exp(-frameInterval/0.04f);
    }
}
void LeakAudioProcessor::releaseResources() {}

// Filtre d'interpolation x4 de l'ITU-R BS.1770-4, annexe 2 (tableau 1) :
// 4 phases de 12 coefficients ; les phases 0/3 et 1/2 sont symétriques.
float LeakAudioProcessor::TruePeakDetector::process(float x) noexcept
{
    static constexpr float coef[4][12] = {
        {  0.0017089843750f,  0.0109863281250f, -0.0196533203125f,  0.0332031250000f, -0.0594482421875f,  0.1373291015625f,
           0.9721679687500f, -0.1022949218750f,  0.0476074218750f, -0.0266113281250f,  0.0148925781250f, -0.0083007812500f },
        { -0.0291748046875f,  0.0292968750000f, -0.0517578125000f,  0.0891113281250f, -0.1665039062500f,  0.4650878906250f,
           0.7797851562500f, -0.2003173828125f,  0.1015625000000f, -0.0582275390625f,  0.0330810546875f, -0.0189208984375f },
        { -0.0189208984375f,  0.0330810546875f, -0.0582275390625f,  0.1015625000000f, -0.2003173828125f,  0.7797851562500f,
           0.4650878906250f, -0.1665039062500f,  0.0891113281250f, -0.0517578125000f,  0.0292968750000f, -0.0291748046875f },
        { -0.0083007812500f,  0.0148925781250f, -0.0266113281250f,  0.0476074218750f, -0.1022949218750f,  0.9721679687500f,
           0.1373291015625f, -0.0594482421875f,  0.0332031250000f, -0.0196533203125f,  0.0109863281250f,  0.0017089843750f },
    };
    // fenêtre circulaire doublée : hist[pos+1 .. pos+12] = 12 derniers échantillons
    pos = (pos + 1) % 12;
    hist[pos] = hist[pos + 12] = x;
    const float* w = hist + pos + 1;
    float peak = std::abs(x);
    for (const auto& c : coef)
    {
        float s = 0.f;
        for (int k = 0; k < 12; ++k) s += c[k] * w[k];
        peak = juce::jmax(peak, std::abs(s));
    }
    return peak;
}

void LeakAudioProcessor::resetLoudness()
{
    loudnessResetCount.fetch_add(1);
    integratedLufs=-120.f; lraValue=0.f;
    momHead=stHead=0; momSum=stSum=0.f;
    std::fill(std::begin(momBuf),std::end(momBuf),0.f);
    std::fill(std::begin(stBuf), std::end(stBuf), 0.f);
    std::fill(std::begin(integHist),std::end(integHist),0.f);
    std::fill(std::begin(lraHist),  std::end(lraHist),  0.f);
    integBlockCounter=lraBlockCounter=0;
    kPreL.reset(); kPreR.reset(); kRlbL.reset(); kRlbR.reset();
    kBlockSumL=kBlockSumR=0.f;
    juce::SpinLock::ScopedTryLockType sl(vuLock);
    if(sl.isLocked()){
        loudExport.momentary=loudExport.shortTerm=loudExport.integrated=-120.f;
        loudExport.lra=0.f;
    }
}

void LeakAudioProcessor::setNumActiveBands(int mode)
{
    bandMode = mode==62 ? 62 : mode==15 ? 15 : 31;
    // bandes ultrasoniques tant que le filtre peut les concevoir (fc <= 0,45 fs) :
    // l'axe va alors jusqu'à la moitié de la fréquence d'échantillonnage
    const int extra = bandMode==62 ? N_EXTRA_62 : bandMode==15 ? N_EXTRA_15 : N_EXTRA_31;
    const float* F=getBandFreqs();
    int n=bandMode;
    while(n<bandMode+extra && F[n]<=0.45f*(float)currentSampleRate) ++n;
    numActiveBands=n;
    rebuildFilters(); decimCounter=0;
    for(int b=0;b<2;++b){
        std::fill(std::begin(bandBufDB[b]),std::end(bandBufDB[b]),-120.f);
        std::fill(std::begin(sideBufDB[b]),std::end(sideBufDB[b]),-120.f);
    }
    // rebuildFilters() a déjà redesigné et resetté corrL/R
    std::fill(std::begin(corrAccLL),std::end(corrAccLL),0.f);
    std::fill(std::begin(corrAccRR),std::end(corrAccRR),0.f);
    std::fill(std::begin(corrAccLR),std::end(corrAccLR),0.f);
    decimAccL=decimAccR=0.f;
}
void LeakAudioProcessor::setAnalysisMode(AnalysisMode m)
{
    analysisMode=m;
    for(int i=0;i<numActiveBands;++i){
        filtersA[i].reset(); filtersB[i].reset();
        corrL[i].reset();    corrR[i].reset();
    }
    for(int b=0;b<2;++b){
        std::fill(std::begin(bandBufDB[b]),std::end(bandBufDB[b]),-120.f);
        std::fill(std::begin(sideBufDB[b]),std::end(sideBufDB[b]),-120.f);
    }
    std::fill(std::begin(corrAccLL),std::end(corrAccLL),0.f);
    std::fill(std::begin(corrAccRR),std::end(corrAccRR),0.f);
    std::fill(std::begin(corrAccLR),std::end(corrAccLR),0.f);
}

void LeakAudioProcessor::rebuildFilters()
{
    float Q = (bandMode==62)?8.65f:(bandMode==15)?2.87f:4.32f;
    const float* F=getBandFreqs();
    // La décimation (1 échantillon sur N) a été retirée : elle décalait les
    // fréquences réelles mesurées (bande "400 Hz" répondant en réalité à du
    // 200 Hz) et, pire, rendait les bandes hautes en 62 bandes impossibles à
    // concevoir correctement (leur fréquence dépassait le Nyquist effectif
    // après décimation, d'où le plateau plat au-delà de ~5k6 Hz). Le coût CPU
    // réel d'un banc de 62 filtres biquad à pleine fréquence est négligeable,
    // donc on conçoit désormais tout à la fréquence d'échantillonnage réelle.
    for(int i=0;i<numActiveBands;++i){
        filtersA[i].design(F[i],(float)currentSampleRate,Q);
        filtersB[i].design(F[i],(float)currentSampleRate,Q);
        corrL[i].design   (F[i],(float)currentSampleRate,Q);  // design() appelle reset()
        corrR[i].design   (F[i],(float)currentSampleRate,Q);
    }
    // Coefficient EMA corrélation/panorama — constante de temps 150 ms
    const float updateRate = juce::jmax(1.f,(float)currentSampleRate);
    corrAlpha = 1.f - std::exp(-1.f / (0.150f * updateRate));
}

void LeakAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Sans ça, les états internes des filtres (bandpass du spectre, corrélation,
    // peakAcc) qui s'éteignent en silence tombent en dénormalisés — le CPU
    // ralentit alors radicalement (jusqu'à 100x sur certains x86) tant que ces
    // valeurs restent coincées entre le plancher normal et zéro exact.
    juce::ScopedNoDenormals noDenormals;
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const float* L = buffer.getReadPointer(0);
    const float* R = (numChannels>1)?buffer.getReadPointer(1):L;
    const int   nBands = numActiveBands;
    const auto  mode   = analysisMode;
    const float rc=rmsCoeff, hrl=holdRelease, rel=RELEASE_ALPHA;
    const bool isSide=(mode==AnalysisMode::Side), isMS=(mode==AnalysisMode::MS);

    // ── Oscilloscope : anneau sans verrou ─────────────────────────────────
    // (avec un verrou "try", un bloc entier était perdu quand l'interface
    // lisait au même moment : saut dans l'onde, très visible sur les basses)
    {
        int wp=scopeBuf.writePos;
        for(int s=0;s<numSamples;++s){
            scopeBuf.L[wp] = L[s];
            scopeBuf.R[wp] = (numChannels>1)?R[s]:L[s];
            wp = (wp+1) & (SCOPE_SIZE-1);
        }
        scopeBuf.writePos=wp;
        scopeTotal.store(scopeTotal.load(std::memory_order_relaxed)+(juce::uint64)numSamples, std::memory_order_release);   // anneau : position = total modulo SCOPE_SIZE
    }

    for(int s=0;s<numSamples;++s)
    {
        const float l=L[s], r=R[s];
        const float al=std::abs(l), ar=std::abs(r);

        if(al>runPeakL) runPeakL=al;
        if(ar>runPeakR) runPeakR=ar;
        vuRmsAccL += rc*(l*l-vuRmsAccL);
        vuRmsAccR += rc*(r*r-vuRmsAccR);

        // Pondération K (BS.1770-4) pour la mesure LUFS — chaîne séparée du
        // RMS brut ci-dessus, qui reste non pondéré pour le VU mètre
        const float klPre=kPreL.process(l), krPre=kPreR.process(r);
        const float klW  =kRlbL.process(klPre), krW=kRlbR.process(krPre);
        kBlockSumL += klW*klW;
        kBlockSumR += krW*krW;

        if(al>=vuHoldL){vuHoldL=al;vuHoldFrameL=VU_HOLD_SAMPLES;}
        else if(vuHoldFrameL>0) --vuHoldFrameL;
        else vuHoldL*=hrl;
        if(ar>=vuHoldR){vuHoldR=ar;vuHoldFrameR=VU_HOLD_SAMPLES;}
        else if(vuHoldFrameR>0) --vuHoldFrameR;
        else vuHoldR*=hrl;
        // vrai true peak (interpolation x4) et dépassements > 0 dBTP
        const float tpl=tpDetL.process(l), tpr=tpDetR.process(r);
        tpL=(tpl>tpL)?tpl:tpL*TP_DECAY;
        tpR=(tpr>tpR)?tpr:tpR*TP_DECAY;
        if(tpl>1.f) tpOverL.store(true,std::memory_order_relaxed);
        if(tpr>1.f) tpOverR.store(true,std::memory_order_relaxed);

        if(++vuExportCounter>=VU_EXPORT_INTERVAL){vuExportCounter=0;updateVuExport();}

        // Filterbank spectre et corrélation/panorama — traités à chaque
        // échantillon (plus de décimation, voir note dans rebuildFilters()).
        const float mid=(l+r)*0.5f, side=(l-r)*0.5f;
        const float inA=isSide?side:mid;

        for(int b=0;b<nBands;++b){
            const float yA=filtersA[b].process(inA), sqA=yA*yA;
            filtersA[b].peakAcc=(sqA>filtersA[b].peakAcc)?sqA:filtersA[b].peakAcc*rel;
            if(isMS){
                const float yB=filtersB[b].process(side), sqB=yB*yB;
                filtersB[b].peakAcc=(sqB>filtersB[b].peakAcc)?sqB:filtersB[b].peakAcc*rel;
            }
        }
        {
            const float ca=corrAlpha, cb=1.f-ca;
            for(int b=0;b<nBands;++b){
                const float yL=corrL[b].process(l);
                const float yR=corrR[b].process(r);
                corrAccLL[b] = corrAccLL[b]*cb + yL*yL*ca;
                corrAccRR[b] = corrAccRR[b]*cb + yR*yR*ca;
                corrAccLR[b] = corrAccLR[b]*cb + yL*yR*ca;
            }
        }
        if(++updateCounter>=UPDATE_INTERVAL){updateCounter=0;updateLevels();}
    }

    // ── Vectorscope : écriture batch (1 échantillon sur 4) ───────────────
    {
        juce::SpinLock::ScopedTryLockType sl(vectorLock);
        if(sl.isLocked()){
            for(int s=0;s<numSamples;s+=4){
                const float mv=(L[s]+(numChannels>1?R[s]:L[s]))*0.5f;
                const float sv=(L[s]-(numChannels>1?R[s]:L[s]))*0.5f;
                vectorBuf[vectorWrite]={mv,sv};
                vectorWrite=(vectorWrite+1)%VECTOR_SIZE;
                if(vectorCount<VECTOR_SIZE) ++vectorCount;
                ++vectorTotal;
            }
        }
    }

    // ── Vectorscope multi-bande : crossover grave/médium/aigu, écriture
    // batch (1 échantillon sur 4) — les filtres tournent sur tous les
    // échantillons (état continu), seule l'écriture dans le ring-buffer
    // est décimée.
    {
        juce::SpinLock::ScopedTryLockType sl(mbVectorLock);
        const bool locked=sl.isLocked();
        for(int s=0;s<numSamples;++s){
            const float l=L[s], r=(numChannels>1)?R[s]:L[s];
            const float lowL=mbLowL.process(l),   lowR=mbLowR.process(r);
            const float highL=mbHighL.process(l), highR=mbHighR.process(r);
            const float midL=mbMidLpL.process(mbMidHpL.process(l));
            const float midR=mbMidLpR.process(mbMidHpR.process(r));
            if(locked && (s&3)==0){
                auto write=[&](int band,float bl,float br){
                    mbVectorBuf[band][mbVectorWrite[band]]={(bl+br)*0.5f,(bl-br)*0.5f};
                    mbVectorWrite[band]=(mbVectorWrite[band]+1)%MB_VECTOR_SIZE;
                    if(mbVectorCount[band]<MB_VECTOR_SIZE) ++mbVectorCount[band];
                };
                write(0,lowL,lowR); write(1,midL,midR); write(2,highL,highR);
            }
        }
    }

    // ── Densité spectrale (PSD) : alimentation du ring-buffer FFT ─────────
    for(int s=0;s<numSamples;++s){
        psdRing[psdRingWrite] = (numChannels>1) ? (L[s]+R[s])*0.5f : L[s];
        psdRingWrite=(psdRingWrite+1)%PSD_FFT_SIZE;
        if(++psdSamplesAcc>=PSD_HOP){
            psdSamplesAcc=0;
            computePSDFrame();
        }
    }
}

void LeakAudioProcessor::updateLevels()
{
    auto toDb=[](float acc)->float{
        return (acc<1e-20f)?-120.f:juce::jlimit(-120.f,6.f,10.f*std::log10(acc));
    };
    const bool doSide=(analysisMode==AnalysisMode::MS);
    const int N=numActiveBands;

    // Spectre (identique à l'original)
    const int wBand=1-bandReadIdx.load(std::memory_order_relaxed);
    const int wSide=1-sideReadIdx.load(std::memory_order_relaxed);
    for(int b=0;b<N;++b){
        bandBufDB[wBand][b]=toDb(filtersA[b].peakAcc);
        sideBufDB[wSide][b]=doSide?toDb(filtersB[b].peakAcc):-120.f;
    }
    bandReadIdx.store(wBand,std::memory_order_release);
    sideReadIdx.store(wSide,std::memory_order_release);

    // Export corrélation et panorama par bande
    const int wCorr=1-corrReadIdx.load(std::memory_order_relaxed);
    for(int b=0;b<N;++b){
        const float ll=corrAccLL[b], rr=corrAccRR[b], lr=corrAccLR[b];
        // Corrélation normalisée [-1, +1]
        const float denom=std::sqrt(juce::jmax(1e-30f,ll*rr));
        corrBuf[wCorr][b]=(denom>1e-30f)?juce::jlimit(-1.f,1.f,lr/denom):0.f;
        // Pan énergie : -1=gauche, 0=centre, +1=droite
        const float sumE=ll+rr;
        panBuf[wCorr][b]=(sumE>1e-30f)?(rr-ll)/sumE:0.f;
    }
    for(int b=N;b<N_BANDS;++b){ corrBuf[wCorr][b]=0.f; panBuf[wCorr][b]=0.f; }
    corrReadIdx.store(wCorr,std::memory_order_release);
}

// ── Pondération K (ITU-R BS.1770-4) ─────────────────────────────────────────
// Étage 1 : shelf haute fréquence (~+4dB) — Étage 2 : passe-haut "RLB" (~38Hz)
// Coefficients redérivés depuis les prototypes analogiques de la norme, valides
// pour n'importe quel sample rate (méthode utilisée par les implémentations de
// référence telles que libebur128).
void LeakAudioProcessor::designKWeighting(float fs)
{
    {
        const double f0=1681.974450955533, G=3.999843853973347, Q=0.7071752369554196;
        const double K =std::tan(juce::MathConstants<double>::pi*f0/(double)fs);
        const double Vh=std::pow(10.0,G/20.0);
        const double Vb=std::pow(Vh,0.4996667741545416);
        const double a0=1.0+K/Q+K*K;
        const float b0=(float)((Vh+Vb*K/Q+K*K)/a0);
        const float b1=(float)(2.0*(K*K-Vh)/a0);
        const float b2=(float)((Vh-Vb*K/Q+K*K)/a0);
        const float a1=(float)(2.0*(K*K-1.0)/a0);
        const float a2=(float)((1.0-K/Q+K*K)/a0);
        kPreL.b0=b0; kPreL.b1=b1; kPreL.b2=b2; kPreL.a1=a1; kPreL.a2=a2; kPreL.reset();
        kPreR.b0=b0; kPreR.b1=b1; kPreR.b2=b2; kPreR.a1=a1; kPreR.a2=a2; kPreR.reset();
    }
    {
        const double f0=38.13547087602444, Q=0.5003270373238773;
        const double K =std::tan(juce::MathConstants<double>::pi*f0/(double)fs);
        const double a0=1.0+K/Q+K*K;
        const float b0=1.f, b1=-2.f, b2=1.f;
        const float a1=(float)(2.0*(K*K-1.0)/a0);
        const float a2=(float)((1.0-K/Q+K*K)/a0);
        kRlbL.b0=b0; kRlbL.b1=b1; kRlbL.b2=b2; kRlbL.a1=a1; kRlbL.a2=a2; kRlbL.reset();
        kRlbR.b0=b0; kRlbR.b1=b1; kRlbR.b2=b2; kRlbR.a1=a1; kRlbR.a2=a2; kRlbR.reset();
    }
}

// ── Gating BS.1770-4 (intégré : seuil relatif -10 LU) / EBU Tech 3342 (LRA : -20 LU) ──
// Histogramme à bins de 0.1 LU — méthode standard pour un gating à mémoire bornée
// en fonctionnement continu (identique en principe à libebur128 / Insight / Youlean).
void LeakAudioProcessor::addGateBlock(float* hist, float lufs)
{
    if(lufs<GATE_MIN_LUFS) return;
    int idx=(int)((lufs-GATE_MIN_LUFS)/GATE_BIN_W);
    idx=juce::jlimit(0,GATE_BINS-1,idx);
    hist[idx]+=1.f;
}
float LeakAudioProcessor::gateMeanLUFS(const float* hist, float thresholdLUFS) const
{
    double sumP=0.0, cnt=0.0;
    for(int i=0;i<GATE_BINS;++i){
        if(hist[i]<=0.f) continue;
        const float lufsCenter=GATE_MIN_LUFS+((float)i+0.5f)*GATE_BIN_W;
        if(lufsCenter<thresholdLUFS) continue;
        const double p=std::pow(10.0,(lufsCenter+0.691)/10.0);
        sumP+=p*(double)hist[i]; cnt+=(double)hist[i];
    }
    if(cnt<=0.0) return -120.f;
    return (float)(-0.691+10.0*std::log10(sumP/cnt));
}
float LeakAudioProcessor::gatePercentileLUFS(const float* hist, float thresholdLUFS, float fraction) const
{
    double total=0.0;
    for(int i=0;i<GATE_BINS;++i){
        const float lufsCenter=GATE_MIN_LUFS+((float)i+0.5f)*GATE_BIN_W;
        if(lufsCenter>=thresholdLUFS) total+=(double)hist[i];
    }
    if(total<=0.0) return GATE_MIN_LUFS;
    double cum=0.0;
    for(int i=0;i<GATE_BINS;++i){
        const float lufsCenter=GATE_MIN_LUFS+((float)i+0.5f)*GATE_BIN_W;
        if(lufsCenter<thresholdLUFS) continue;
        cum+=(double)hist[i];
        if(cum/total>=(double)fraction) return lufsCenter;
    }
    return GATE_MIN_LUFS;
}
void LeakAudioProcessor::recomputeIntegrated()
{
    const float ungated=gateMeanLUFS(integHist,GATE_MIN_LUFS);
    if(ungated<=-119.f){ integratedLufs=-120.f; return; }
    const float relGate=ungated-10.f;   // -10 LU sous la moyenne non-gatée
    integratedLufs=gateMeanLUFS(integHist,juce::jmax(GATE_MIN_LUFS,relGate));
}
void LeakAudioProcessor::recomputeLRA()
{
    const float ungated=gateMeanLUFS(lraHist,GATE_MIN_LUFS);
    if(ungated<=-119.f){ lraValue=0.f; return; }
    const float relGate=ungated-20.f;   // -20 LU pour la LRA (EBU Tech 3342)
    const float thr=juce::jmax(GATE_MIN_LUFS,relGate);
    const float p10=gatePercentileLUFS(lraHist,thr,0.10f);
    const float p95=gatePercentileLUFS(lraHist,thr,0.95f);
    lraValue=juce::jmax(0.f,p95-p10);
}

void LeakAudioProcessor::updateVuExport()
{
    auto lin2db=[](float v)->float{
        return (v<1e-10f)?-120.f:20.f*std::log10(v);
    };

    // ── Bloc K-pondéré (indépendant du verrou UI — état interne au thread audio) ──
    const float zL=kBlockSumL/(float)VU_EXPORT_INTERVAL;
    const float zR=kBlockSumR/(float)VU_EXPORT_INTERVAL;
    kBlockSumL=kBlockSumR=0.f;
    const float blockPower=zL+zR;   // somme stéréo en puissance (BS.1770 — pas de moyenne en dB)

    const float oldMom=momBuf[momHead];
    momSum-=oldMom; momBuf[momHead]=blockPower; momSum+=blockPower;
    momHead=(momHead+1)%momFramesActive;
    const float momPower=momSum/(float)momFramesActive;
    const float momVal=(momPower>1e-12f)?(-0.691f+10.f*std::log10(momPower)):-120.f;

    const float oldST=stBuf[stHead];
    stSum-=oldST; stBuf[stHead]=blockPower; stSum+=blockPower;
    stHead=(stHead+1)%stFramesActive;
    const float stPower=stSum/(float)stFramesActive;
    const float stVal=(stPower>1e-12f)?(-0.691f+10.f*std::log10(stPower)):-120.f;

    // ── Blocs de gating : intégré toutes les 100ms, LRA toutes les 1s ──────
    integBlockCounter+=VU_EXPORT_INTERVAL;
    if(integBlockCounter>=integBlockSamples){
        integBlockCounter=0;
        addGateBlock(integHist,momVal);
        recomputeIntegrated();
    }
    lraBlockCounter+=VU_EXPORT_INTERVAL;
    if(lraBlockCounter>=lraBlockSamples){
        lraBlockCounter=0;
        addGateBlock(lraHist,stVal);
        recomputeLRA();
    }

    juce::SpinLock::ScopedTryLockType sl(vuLock);
    if(!sl.isLocked()) return;
    vuExport.peakL=lin2db(runPeakL); vuExport.peakR=lin2db(runPeakR);
    runPeakL=runPeakR=0.f;
    vuExport.rmsL=lin2db(std::sqrt(juce::jmax(vuRmsAccL,0.f)));
    vuExport.rmsR=lin2db(std::sqrt(juce::jmax(vuRmsAccR,0.f)));
    vuExport.peakHoldL=lin2db(vuHoldL);
    vuExport.peakHoldR=lin2db(vuHoldR);

    loudExport.momentary=momVal; loudExport.shortTerm=stVal;
    loudExport.integrated=integratedLufs;
    loudExport.truePeakL=lin2db(tpL); loudExport.truePeakR=lin2db(tpR);
    loudExport.lra=lraValue;
}

bool LeakAudioProcessor::getBandLevels(float* dest,int numBands) const
{
    const int r=bandReadIdx.load(std::memory_order_acquire);
    std::memcpy(dest,bandBufDB[r],(size_t)juce::jmin(numBands,numActiveBands)*sizeof(float));
    return true;
}
bool LeakAudioProcessor::getSideLevels(float* dest,int numBands) const
{
    const int r=sideReadIdx.load(std::memory_order_acquire);
    std::memcpy(dest,sideBufDB[r],(size_t)juce::jmin(numBands,numActiveBands)*sizeof(float));
    return true;
}
bool LeakAudioProcessor::getBandCorr(float* dest,int numBands) const
{
    if(!dest||numBands<=0) return false;
    const int r=corrReadIdx.load(std::memory_order_acquire);
    std::memcpy(dest,corrBuf[r],(size_t)juce::jmin(numBands,numActiveBands)*sizeof(float));
    return true;
}
bool LeakAudioProcessor::getBandPan(float* dest,int numBands) const
{
    if(!dest||numBands<=0) return false;
    const int r=corrReadIdx.load(std::memory_order_acquire);
    std::memcpy(dest,panBuf[r],(size_t)juce::jmin(numBands,numActiveBands)*sizeof(float));
    return true;
}

// ── PSD : une trame FFT toutes les PSD_HOP échantillons (recouvrement 75%) ──
void LeakAudioProcessor::computePSDFrame()
{
    // Reconstitue les PSD_FFT_SIZE derniers échantillons dans l'ordre chronologique
    // (psdRingWrite pointe sur l'échantillon le plus ancien, sur le point d'être réécrit)
    for(int i=0;i<PSD_FFT_SIZE;++i)
        psdFFTBuf[i]=psdRing[(psdRingWrite+i)%PSD_FFT_SIZE];
    std::fill(psdFFTBuf+PSD_FFT_SIZE, psdFFTBuf+2*PSD_FFT_SIZE, 0.f);

    psdWindowFn.multiplyWithWindowingTable(psdFFTBuf,(size_t)PSD_FFT_SIZE);
    psdFFT.performFrequencyOnlyForwardTransform(psdFFTBuf);

    // ── Normalisation amplitude → repère ~dBFS ────────────────────────────
    // performFrequencyOnlyForwardTransform() de JUCE renvoie une magnitude
    // brute, NON normalisée (ni par la taille de FFT, ni par le gain du
    // fenêtrage). Sans correction, un signal large bande et fort (master
    // très limité) pousse une grande partie des bins au-dessus de l'échelle
    // dB attendue → ils s'écrasent tous en haut du graphe ("plateau plat").
    // PSD_NORM ramène un sinus plein échelle à ~0dB : 2/N pour repasser en
    // amplitude simple-face, /0.5 pour compenser le gain cohérent de la
    // fenêtre de Hann (sa moyenne vaut exactement 0.5).
    static constexpr float PSD_NORM = 2.f/((float)PSD_FFT_SIZE*0.5f);

    // Double-tampon façon bandBufDB/corrBuf : écrit dans le tampon non lu
    const int w = 1 - psdReadIdx.load(std::memory_order_relaxed);
    for(int b=0;b<PSD_NUM_BINS;++b){
        const float mag=psdFFTBuf[b]*PSD_NORM;
        const float power=mag*mag;
        // moyennage EMA façon Welch ; si la trame tombe à plus de 12 dB sous la
        // moyenne (son arrêté), la moyenne suit vite : pas de longue traîne
        const float a = power < psdAvgLin[b]*0.0631f ? psdAlphaFast : psdAlpha;
        psdAvgLin[b] += (power-psdAvgLin[b])*a;
        psdBuf[w][b]=psdAvgLin[b];
    }
    psdReadIdx.store(w,std::memory_order_release);
}

bool LeakAudioProcessor::getPSDData(float* destDb,int numBins) const
{
    if(!destDb||numBins<=0) return false;
    const int n=juce::jmin(numBins,PSD_NUM_BINS);
    const int r=psdReadIdx.load(std::memory_order_acquire);
    for(int b=0;b<n;++b){
        const float p=psdBuf[r][b];
        destDb[b]=(p<1e-12f)?-120.f:10.f*std::log10(p);
    }
    return true;
}
bool LeakAudioProcessor::getVuData(VuData& out) const
{
    juce::SpinLock::ScopedTryLockType sl(vuLock);
    if(!sl.isLocked()) return false;
    out=vuExport; return true;
}
bool LeakAudioProcessor::getLoudnessData(LoudnessData& out) const
{
    juce::SpinLock::ScopedTryLockType sl(vuLock);
    if(!sl.isLocked()) return false;
    out=loudExport; return true;
}
juce::uint64 LeakAudioProcessor::getScopeData(float* outL,float* outR,int n) const
{
    // jamais d'image vide : lecture sans verrou des n derniers échantillons
    // publiés (l'audio écrit au plus un bloc pendant la copie, loin derrière)
    n=juce::jlimit(0, SCOPE_MAX_READ, n);
    const juce::uint64 total=scopeTotal.load(std::memory_order_acquire);
    int rd=(int)((total-(juce::uint64)n) & (juce::uint64)(SCOPE_SIZE-1));
    for(int i=0;i<n;++i){
        outL[i]=scopeBuf.L[rd]; outR[i]=scopeBuf.R[rd];
        rd=(rd+1) & (SCOPE_SIZE-1);
    }
    return total;
}
void LeakAudioProcessor::getVectorData(VectorPoint* out,int& count) const
{
    juce::SpinLock::ScopedTryLockType sl(vectorLock);
    if(!sl.isLocked()){count=0;return;}
    const int n=juce::jmin(vectorCount,VECTOR_EXPORT);
    int rd=(vectorWrite-n+VECTOR_SIZE)%VECTOR_SIZE;
    for(int i=0;i<n;++i){out[i]=vectorBuf[rd];rd=(rd+1)%VECTOR_SIZE;}
    count=n;
}
int LeakAudioProcessor::readNewVectorData(juce::uint64& cursor,VectorPoint* out,int maxCount) const
{
    juce::SpinLock::ScopedTryLockType sl(vectorLock);
    if(!sl.isLocked()) return 0;
    const juce::uint64 total=vectorTotal;
    if(cursor>total) cursor=total;   // flux redémarré
    const int n=(int)juce::jmin<juce::uint64>(total-cursor, (juce::uint64)juce::jmin(maxCount, vectorCount));
    int rd=(vectorWrite-n+VECTOR_SIZE)%VECTOR_SIZE;
    for(int i=0;i<n;++i){ out[i]=vectorBuf[rd]; rd=(rd+1)%VECTOR_SIZE; }
    cursor=total;
    return n;
}
void LeakAudioProcessor::getMultibandVectorData(int band,VectorPoint* out,int& count) const
{
    band=juce::jlimit(0,2,band);
    juce::SpinLock::ScopedTryLockType sl(mbVectorLock);
    if(!sl.isLocked()){count=0;return;}
    const int n=juce::jmin(mbVectorCount[band],MB_VECTOR_EXPORT);
    int rd=(mbVectorWrite[band]-n+MB_VECTOR_SIZE)%MB_VECTOR_SIZE;
    for(int i=0;i<n;++i){out[i]=mbVectorBuf[band][rd];rd=(rd+1)%MB_VECTOR_SIZE;}
    count=n;
}

juce::AudioProcessorEditor* LeakAudioProcessor::createEditor(){return new LeakEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new LeakAudioProcessor();}

void LeakAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement xml("LeakState");
    xml.setAttribute("editorW",     editorWidth);
    xml.setAttribute("editorH",     editorHeight);
    xml.setAttribute("numBands",    bandMode);
    xml.setAttribute("analysisMode",(int)analysisMode);
    for(int i=0;i<NUM_SLOTS;++i)
        xml.setAttribute("slot"+juce::String(i), slotModule[i]);
    xml.setAttribute("customAccentColor", (int)customAccentColor);
    xml.setAttribute("customGridColor",   (int)customGridColor);
    xml.setAttribute("customBgColor",     (int)customBgColor);
    xml.setAttribute("customTextColor",   (int)customTextColor);
    xml.setAttribute("customSideColor",   (int)customSideColor);
    xml.setAttribute("lightCpuMode", lightCpuMode);
    xml.setAttribute("uiFps", uiFps);
    xml.setAttribute("gpuMsaa", gpuMsaa);
    xml.setAttribute("gpuVsync", gpuVsync);
    if(language>=0) xml.setAttribute("language", language);
    xml.setAttribute("lightTint", lightTint);
    xml.setAttribute("highContrast", highContrast);
    xml.setAttribute("modules3D", modules3D);
    xml.setAttribute("largeText", largeText);
    xml.setAttribute("uiScale", uiScale);
    xml.setAttribute("plateTint", plateTint);
    xml.setAttribute("lightScreens", lightScreens);
    xml.setAttribute("titleStyle", titleStyle);
    xml.setAttribute("corrStyle", corrStyle);
    xml.setAttribute("masterConsole", masterConsole);
    xml.setAttribute("peakHoldOn", peakHoldOn);
    xml.setAttribute("peakHoldIdx", peakHoldIdx);
    xml.setAttribute("dbRangeVal", (double)dbRangeVal);
    xml.setAttribute("spectrumReleaseSec", (double)spectrumReleaseSec);
    xml.setAttribute("dotChromaReleaseSec", (double)dotChromaReleaseSec);
    xml.setAttribute("dotNebulaReleaseSec", (double)dotNebulaReleaseSec);
    xml.setAttribute("corrReleaseSec", (double)corrReleaseSec);
    xml.setAttribute("panoReleaseSec", (double)panoReleaseSec);
    xml.setAttribute("oscTimeDiv", oscTimeDiv);
    xml.setAttribute("oscAmpVal", (double)oscAmpVal);
    xml.setAttribute("oscMono", oscMono);
    xml.setAttribute("oscStyle", oscStyle);
    xml.setAttribute("oscSplit", oscSplit);
    xml.setAttribute("oscFps", oscFps);
    xml.setAttribute("oscPersist", oscPersist);
    xml.setAttribute("oscLineW", oscLineWidth);
    xml.setAttribute("oscTraceMode", oscTraceMode);
    xml.setAttribute("vecReleaseAlpha", (double)vecReleaseAlpha);
    xml.setAttribute("mbVecReleaseAlpha", (double)mbVecReleaseAlpha);
    xml.setAttribute("vecLineMode", vecLineMode);
    xml.setAttribute("needleModeSel", needleModeSel);
    xml.setAttribute("needleCalibRef", (double)needleCalibRef);
    xml.setAttribute("needleCalibStep", needleCalibStep);
    xml.setAttribute("customSpectroLowColor",  (int)customSpectroLowColor);
    xml.setAttribute("customSpectroMidColor",  (int)customSpectroMidColor);
    xml.setAttribute("customSpectroHighColor", (int)customSpectroHighColor);
    xml.setAttribute("spectro3dPeaksReverse",   spectro3dPeaksReverse);
    xml.setAttribute("spectro3dSurfaceReverse", spectro3dSurfaceReverse);
    xml.setAttribute("spectro3dPeaksTilt",   (double)spectro3dPeaksTilt);
    xml.setAttribute("spectro3dPeaksSkew",   (double)spectro3dPeaksSkew);
    xml.setAttribute("spectro3dSurfaceTilt", (double)spectro3dSurfaceTilt);
    xml.setAttribute("spectro3dSurfaceSkew", (double)spectro3dSurfaceSkew);
    xml.setAttribute("spectro3dPeaksTimeSec",   (double)spectro3dPeaksTimeSec);
    xml.setAttribute("spectro3dSurfaceTimeSec", (double)spectro3dSurfaceTimeSec);
    xml.setAttribute("gpuSurfaceReverse", gpuSurfaceReverse);
    xml.setAttribute("gpuSurfaceTilt",    (double)gpuSurfaceTilt);
    xml.setAttribute("gpuSurfaceSkew",    (double)gpuSurfaceSkew);
    xml.setAttribute("gpuSurfaceTimeSec", (double)gpuSurfaceTimeSec);
    xml.setAttribute("gpuTerrainTimeSec", (double)gpuTerrainTimeSec);
    xml.setAttribute("gpuNeonTimeSec",    (double)gpuNeonTimeSec);
    xml.setAttribute("gpuCrystalTimeSec", (double)gpuCrystalTimeSec);
    xml.setAttribute("gpuTerrainYaw", (double)gpuTerrainYaw); xml.setAttribute("gpuTerrainPitch", (double)gpuTerrainPitch);
    xml.setAttribute("gpuNeonYaw",    (double)gpuNeonYaw);    xml.setAttribute("gpuNeonPitch",    (double)gpuNeonPitch);
    xml.setAttribute("gpuCrystalYaw", (double)gpuCrystalYaw); xml.setAttribute("gpuCrystalPitch", (double)gpuCrystalPitch);
    xml.setAttribute("gpuTerrainZoom", (double)gpuTerrainZoom); xml.setAttribute("gpuNeonZoom", (double)gpuNeonZoom);
    xml.setAttribute("gpuCrystalZoom", (double)gpuCrystalZoom);
    xml.setAttribute("presetName", currentPresetName);
    copyXmlToBinary(xml,destData);
}

//==============================================================================
// Presets utilisateur : un fichier XML par preset (même format que l'état),
// sans les réglages propres au poste (taille de fenêtre, cadence, réglages GPU)
//==============================================================================
namespace
{
    const char* const kMachineAttrs[] = { "editorW", "editorH", "lightCpuMode", "uiFps", "gpuMsaa", "gpuVsync", "language", "presetName" };
    bool isMachineAttr(const juce::String& a)
    {
        for(auto* m : kMachineAttrs) if(a==m) return true;
        return false;
    }
}

juce::File LeakAudioProcessor::getPresetFolder()
{
    const auto base=juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Side Key System");
    const auto dir=base.getChildFile("Merlin Alpha Presets");
    // v1.47 : presets d'un ancien nom ("Merlin By Mikshake", "Merlin Alpha by
    // Mikshake") repris une fois
    for(auto* oldName : { "Merlin Alpha by Mikshake Presets", "Merlin By Mikshake Presets" }){
        const auto old=base.getChildFile(oldName);
        if(!dir.isDirectory() && old.isDirectory()) old.copyDirectoryTo(dir);
    }
    return dir;
}

juce::File LeakAudioProcessor::presetFile(const juce::String& name)
{
    return getPresetFolder().getChildFile(juce::File::createLegalFileName(name.trim())+".xml");
}

juce::StringArray LeakAudioProcessor::getUserPresetNames() const
{
    juce::StringArray names;
    for(const auto& f : getPresetFolder().findChildFiles(juce::File::findFiles, false, "*.xml"))
        names.add(f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool LeakAudioProcessor::saveUserPreset(const juce::String& name)
{
    if(name.trim().isEmpty()) return false;
    juce::MemoryBlock mb;
    getStateInformation(mb);
    auto xml=getXmlFromBinary(mb.getData(), (int)mb.getSize());
    if(xml==nullptr) return false;
    for(auto* a : kMachineAttrs) xml->removeAttribute(a);
    if(!getPresetFolder().createDirectory()) return false;
    if(!xml->writeTo(presetFile(name))) return false;
    currentPresetName=presetFile(name).getFileNameWithoutExtension();
    return true;
}

// État courant + valeurs d'un preset (les réglages du poste restent)
bool LeakAudioProcessor::applyPresetXml(const juce::XmlElement& pre)
{
    if(!pre.hasTagName("LeakState")) return false;
    juce::MemoryBlock mb;
    getStateInformation(mb);
    auto cur=getXmlFromBinary(mb.getData(), (int)mb.getSize());
    if(cur==nullptr) return false;
    for(int i=0;i<pre.getNumAttributes();++i){
        const auto attr=pre.getAttributeName(i);
        if(!isMachineAttr(attr)) cur->setAttribute(attr, pre.getAttributeValue(i));
    }
    juce::MemoryBlock out;
    copyXmlToBinary(*cur, out);
    setStateInformation(out.getData(), (int)out.getSize());
    return true;
}

// Preset usine "TV" (validé par le user sur capture, 2026-10-20) : VU à
// aiguille (réf. -14), Multiband Correlator, Spectrogram 3D Surface, Loudness,
// Spectrum 62 bandes, VU master Console. Angles des spectrogrammes GPU non
// inclus (modules absents, vue de chacun gardée).
bool LeakAudioProcessor::loadFactoryTV()
{
    static const char* const kTV = R"(<LeakState
    numBands="62" analysisMode="0" transientThr="-18.0" slot0="12" slot1="7" slot2="15" slot3="4"
    slot4="6" slot5="5" customAccentColor="-16711758" customGridColor="-16043984"
    customBgColor="-16578038" customTextColor="-3604504" customSideColor="-5085441" uiTemplate="2"
    lightTint="0" highContrast="1" modules3D="0" largeText="1" masterConsole="1" peakHoldOn="1"
    peakHoldIdx="2" dbRangeVal="50.0" spectrumReleaseSec="1.011325240135193"
    dotChromaReleaseSec="0.1000986769795418" dotNebulaReleaseSec="0.1000000014901161"
    corrReleaseSec="0.1000080779194832" panoReleaseSec="0.1000230759382248" oscTimeDiv="1164"
    oscAmpVal="4.0" oscMono="1" oscStyle="2" oscFps="60" oscPersist="0"
    vecReleaseAlpha="0.7200000286102295" mbVecReleaseAlpha="0.8999999761581421" vecLineMode="1"
    needleModeSel="0" needleCalibRef="-14.0" customSpectroLowColor="-13421773"
    customSpectroMidColor="-5197648" customSpectroHighColor="-12124160" spectro3dPeaksReverse="0"
    spectro3dSurfaceReverse="0" spectro3dPeaksTilt="1.799999952316284" spectro3dPeaksSkew="-1.0"
    spectro3dSurfaceTilt="1.479999899864197" spectro3dSurfaceSkew="0.3840000927448273"
    spectro3dPeaksTimeSec="1.480317234992981" spectro3dSurfaceTimeSec="3.199969291687012"
    gpuTerrainTimeSec="11.89981174468994" gpuNeonTimeSec="20.0" gpuCrystalTimeSec="20.0"/>)";
    auto pre=juce::parseXML(juce::String(kTV));
    if(pre==nullptr || !applyPresetXml(*pre)) return false;
    currentPresetName.clear();
    return true;
}

bool LeakAudioProcessor::loadUserPreset(const juce::String& name)
{
    auto pre=juce::parseXML(presetFile(name));
    if(pre==nullptr || !applyPresetXml(*pre)) return false;
    currentPresetName=presetFile(name).getFileNameWithoutExtension();
    return true;
}

// Preset usine "Sound Design" (validé par le user sur capture, 2026-10-19) :
// VU à aiguille, oscilloscope Vintage ST, Spectrum 15 bandes, Panorama Chroma,
// Spectrogram Crystal 7 s vu de face, textes agrandis, contraste, modules 3D.
bool LeakAudioProcessor::loadFactorySoundDesign()
{
    static const char* const kSoundDesign = R"(<LeakState
    numBands="15" analysisMode="0" transientThr="-18.0" slot0="12" slot1="2" slot2="6" slot3="21"
    slot4="18" slot5="5" customAccentColor="-16711758" customGridColor="-16043984"
    customBgColor="-16578038" customTextColor="-3604504" customSideColor="-5085441" uiTemplate="2"
    lightTint="0" highContrast="1" modules3D="1" largeText="1" masterConsole="0" peakHoldOn="1"
    peakHoldIdx="2" dbRangeVal="50.0" spectrumReleaseSec="1.011325240135193"
    dotChromaReleaseSec="0.1000986769795418" dotNebulaReleaseSec="0.1000000014901161"
    corrReleaseSec="0.1000080779194832" panoReleaseSec="0.1000230759382248" oscTimeDiv="1212"
    oscAmpVal="3.372153520584106" oscMono="0" oscStyle="2" oscFps="60" oscPersist="0"
    vecReleaseAlpha="0.7200000286102295" mbVecReleaseAlpha="0.8999999761581421" vecLineMode="1"
    needleModeSel="0" needleCalibRef="-12.0" customSpectroLowColor="-13421773"
    customSpectroMidColor="-5197648" customSpectroHighColor="-12124160" spectro3dPeaksReverse="0"
    spectro3dSurfaceReverse="0" spectro3dPeaksTilt="1.080000042915344" spectro3dPeaksSkew="-1.0"
    spectro3dSurfaceTilt="1.799999952316284" spectro3dSurfaceSkew="-0.9959999918937683"
    spectro3dPeaksTimeSec="2.371251583099365" spectro3dSurfaceTimeSec="2.325513601303101"
    gpuTerrainTimeSec="11.75337791442871" gpuNeonTimeSec="20.0" gpuCrystalTimeSec="7.038753032684326"
    gpuTerrainYaw="0.0" gpuTerrainPitch="1.018" gpuNeonYaw="0.0" gpuNeonPitch="1.018"
    gpuCrystalYaw="0.0" gpuCrystalPitch="1.018"/>)";
    auto pre=juce::parseXML(juce::String(kSoundDesign));
    if(pre==nullptr || !applyPresetXml(*pre)) return false;
    currentPresetName.clear();
    return true;
}

bool LeakAudioProcessor::deleteUserPreset(const juce::String& name)
{
    const bool ok=presetFile(name).deleteFile();
    if(ok && name==currentPresetName) currentPresetName.clear();
    return ok;
}
void LeakAudioProcessor::setStateInformation(const void* data,int sizeInBytes)
{
    if(auto xml=getXmlFromBinary(data,sizeInBytes)){
        if(xml->hasTagName("LeakState")){
            editorWidth  =xml->getIntAttribute("editorW",1100);
            editorHeight =xml->getIntAttribute("editorH",620);
            setNumActiveBands(xml->getIntAttribute("numBands",31));
            setAnalysisMode((AnalysisMode)xml->getIntAttribute("analysisMode",0));
            static const int def[NUM_SLOTS]={1,7,12,4,6,5};
            for(int i=0;i<NUM_SLOTS;++i)
                slotModule[i]=xml->getIntAttribute("slot"+juce::String(i),def[i]);
            for(auto& m : slotModule) if(m==(int)ModuleID::Transient) m=0;   // module retiré : slot vide
            customAccentColor = (uint32_t)xml->getIntAttribute("customAccentColor", (int)0xFF00FFB2);
            customGridColor   = (uint32_t)xml->getIntAttribute("customGridColor",   (int)0xFF0B3030);
            customBgColor     = (uint32_t)xml->getIntAttribute("customBgColor",     (int)0xFF030A0A);
            customTextColor   = (uint32_t)xml->getIntAttribute("customTextColor",   (int)0xFFC8FFE8);
            customSideColor   = (uint32_t)xml->getIntAttribute("customSideColor",   (int)0xFFB266FF);
            lightCpuMode = xml->getBoolAttribute("lightCpuMode", false);
            uiFps    = juce::jlimit(30, 144, xml->getIntAttribute("uiFps", lightCpuMode ? 30 : 60));
            gpuMsaa  = juce::jlimit(0, 8, xml->getIntAttribute("gpuMsaa", 4));
            gpuVsync = xml->getBoolAttribute("gpuVsync", true);
            language = xml->getIntAttribute("language", -1);
            // (avant la v1.6, « Neumorphic Light » était un template à part : uiTemplate 3)
            lightTint = xml->getBoolAttribute("lightTint", xml->getIntAttribute("uiTemplate", 2) == 3);
            highContrast = true;   // mode contraste permanent (v1.51) : l'ancien réglage est ignoré
            modules3D = xml->getBoolAttribute("modules3D", false);
            largeText = true;   // gros texte toujours activé à l'ouverture (réglable ensuite dans Options › Affichage)
            uiScale = xml->getIntAttribute("uiScale", uiScale);   // absent (presets d'usine) : inchangé
            if(uiScale!=0) uiScale=juce::jlimit(100, 200, uiScale);
            plateTint = juce::jlimit(0, 1, xml->getIntAttribute("plateTint", plateTint));   // absent : inchangé
            lightScreens = juce::jlimit(0, 1, xml->getIntAttribute("lightScreens", lightScreens));
            titleStyle = juce::jlimit(0, 2, xml->getIntAttribute("titleStyle", titleStyle));
            corrStyle  = juce::jlimit(0, 2, xml->getIntAttribute("corrStyle", corrStyle));
            masterConsole = xml->getBoolAttribute("masterConsole", false);
            peakHoldOn  = xml->getBoolAttribute("peakHoldOn", true);
            peakHoldIdx = xml->getIntAttribute("peakHoldIdx", 2);
            dbRangeVal  = (float)xml->getDoubleAttribute("dbRangeVal", 50.0);
            spectrumReleaseSec = (float)xml->getDoubleAttribute("spectrumReleaseSec", 0.4);
            dotChromaReleaseSec = (float)xml->getDoubleAttribute("dotChromaReleaseSec", 0.4);
            dotNebulaReleaseSec = (float)xml->getDoubleAttribute("dotNebulaReleaseSec", 0.4);
            corrReleaseSec     = (float)xml->getDoubleAttribute("corrReleaseSec", 0.4);
            panoReleaseSec     = (float)xml->getDoubleAttribute("panoReleaseSec", 0.4);
            oscTimeDiv  = xml->getIntAttribute("oscTimeDiv", oscDefaultWindow());
            oscAmpVal   = (float)xml->getDoubleAttribute("oscAmpVal", 1.0);
            oscMono     = xml->getBoolAttribute("oscMono", false);
            oscStyle    = juce::jlimit(0, 3, xml->getIntAttribute("oscStyle", 0));
            oscSplit    = xml->getBoolAttribute("oscSplit", oscStyle>=2);   // état plus ancien : ce que faisait ce style
            oscFps      = juce::jlimit(10, 144, xml->getIntAttribute("oscFps", 30));
            oscPersist  = xml->getBoolAttribute("oscPersist", true);
            // curseur 0..100 (v1.69), 20 d'origine ; les anciens niveaux 0..3
            // (« oscLineWidth », v1.67-1.68) ne sont pas repris : retour à 20
            oscLineWidth= juce::jlimit(0, 100, xml->getIntAttribute("oscLineW", 20));
            oscTraceMode= juce::jlimit(0, 1, xml->getIntAttribute("oscTraceMode", 0));
            vecReleaseAlpha = (float)xml->getDoubleAttribute("vecReleaseAlpha", 0.72);
            mbVecReleaseAlpha = (float)xml->getDoubleAttribute("mbVecReleaseAlpha", 0.90);
            vecLineMode = xml->getBoolAttribute("vecLineMode", true);
            needleModeSel  = xml->getIntAttribute("needleModeSel", 0);
            needleCalibRef = (float)xml->getDoubleAttribute("needleCalibRef", -10.0);
            needleCalibStep = xml->getIntAttribute("needleCalibStep", 2)==4 ? 4 : 2;
            customSpectroLowColor  = (uint32_t)xml->getIntAttribute("customSpectroLowColor",  (int)0xFF333333);
            customSpectroMidColor  = (uint32_t)xml->getIntAttribute("customSpectroMidColor",  (int)0xFFB0B0B0);
            customSpectroHighColor = (uint32_t)xml->getIntAttribute("customSpectroHighColor", (int)0xFF470000);
            spectro3dPeaksReverse   = xml->getBoolAttribute("spectro3dPeaksReverse", false);
            spectro3dSurfaceReverse = xml->getBoolAttribute("spectro3dSurfaceReverse", false);
            spectro3dPeaksTilt   = (float)xml->getDoubleAttribute("spectro3dPeaksTilt", 0.3);
            spectro3dPeaksSkew   = (float)xml->getDoubleAttribute("spectro3dPeaksSkew", 0.0);
            spectro3dSurfaceTilt = (float)xml->getDoubleAttribute("spectro3dSurfaceTilt", 0.3);
            spectro3dSurfaceSkew = (float)xml->getDoubleAttribute("spectro3dSurfaceSkew", 0.0);
            spectro3dPeaksTimeSec   = (float)xml->getDoubleAttribute("spectro3dPeaksTimeSec",   2.1);
            spectro3dSurfaceTimeSec = (float)xml->getDoubleAttribute("spectro3dSurfaceTimeSec", 2.1);
            gpuSurfaceReverse = xml->getBoolAttribute("gpuSurfaceReverse", false);
            gpuSurfaceTilt    = (float)xml->getDoubleAttribute("gpuSurfaceTilt", 0.3);
            gpuSurfaceSkew    = (float)xml->getDoubleAttribute("gpuSurfaceSkew", 0.0);
            gpuSurfaceTimeSec = (float)xml->getDoubleAttribute("gpuSurfaceTimeSec", 2.1);
            gpuTerrainTimeSec = (float)xml->getDoubleAttribute("gpuTerrainTimeSec", 2.1);
            gpuNeonTimeSec    = (float)xml->getDoubleAttribute("gpuNeonTimeSec",    2.1);
            gpuCrystalTimeSec = (float)xml->getDoubleAttribute("gpuCrystalTimeSec", 2.1);
            gpuTerrainYaw   = (float)xml->getDoubleAttribute("gpuTerrainYaw",   0.0);
            gpuTerrainPitch = (float)xml->getDoubleAttribute("gpuTerrainPitch", 1.018);
            gpuNeonYaw      = (float)xml->getDoubleAttribute("gpuNeonYaw",      0.0);
            gpuNeonPitch    = (float)xml->getDoubleAttribute("gpuNeonPitch",    1.018);
            gpuCrystalYaw   = (float)xml->getDoubleAttribute("gpuCrystalYaw",   0.0);
            gpuCrystalPitch = (float)xml->getDoubleAttribute("gpuCrystalPitch", 1.018);
            gpuTerrainZoom  = (float)xml->getDoubleAttribute("gpuTerrainZoom",  0.0);
            gpuNeonZoom     = (float)xml->getDoubleAttribute("gpuNeonZoom",     0.0);
            gpuCrystalZoom  = (float)xml->getDoubleAttribute("gpuCrystalZoom",  0.0);
            currentPresetName = xml->getStringAttribute("presetName");
        }
    }
}
