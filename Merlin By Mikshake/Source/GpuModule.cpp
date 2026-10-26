#include "GpuModule.h"
#include "PluginEditor.h"     // Col::, Neu::, Ui::
#include "SystemLoopback.h"   // WindowsDisplay::waitForNextFrame

using namespace juce::gl;

namespace { juce::SpinLock rendererLock; juce::String rendererInfo; }

//==============================================================================
// GpuFramePacer — horloge d'images commune à tous les modules GPU : un fil
// dédié attend chaque composition de l'écran par Windows (DwmFlush) puis
// déclenche les images dues. Le signal de synchro de JUCE passait par le
// thread de messages : quand l'interface était chargée il arrivait en retard
// et une image sautait (saccades de l'oscilloscope).
//==============================================================================
struct GpuFramePacer : private juce::Thread
{
    GpuFramePacer() : juce::Thread("Merlin GPU frame pacer") { startThread(juce::Thread::Priority::high); }
    ~GpuFramePacer() override { stopThread(2000); }

    void add(GpuModuleBase* m)    { const juce::ScopedLock sl(lock); modules.addIfNotAlreadyThere(m); }
    void remove(GpuModuleBase* m) { const juce::ScopedLock sl(lock); modules.removeFirstMatchingValue(m); }
    void barrier()                { const juce::ScopedLock sl(lock); }   // attend la fin d'un passage en cours

    static std::shared_ptr<GpuFramePacer> acquire()
    {
        static std::mutex m;
        static std::weak_ptr<GpuFramePacer> shared;
        const std::lock_guard<std::mutex> g(m);
        auto p=shared.lock();
        if(p==nullptr){ p=std::make_shared<GpuFramePacer>(); shared=p; }
        return p;
    }

private:
    void run() override
    {
        while(!threadShouldExit()){
            if(!WindowsDisplay::waitForNextFrame()) juce::Thread::sleep(16);   // pas de composition : ~60 Hz
            const double ts=juce::Time::getMillisecondCounterHiRes()*0.001;
            const juce::ScopedLock sl(lock);
            for(auto* m : modules) m->onFrameTick(ts);
        }
    }

    juce::CriticalSection lock;
    juce::Array<GpuModuleBase*> modules;
};

//==============================================================================
GpuModuleBase::GpuModuleBase(LeakAudioProcessor& p) : processor(p)
{
    setOpaque(false);
    applyPixelFormat();
    context.setPreferredVersion({ 3, 2 });        // profil core 3.2
    context.setComponentPaintingEnabled(true);    // calque 2D composé par-dessus
    // Pas de rendu continu : sur certains pilotes (NVIDIA), le thread GL
    // attend la synchro écran en tournant à vide (~1 cœur). Les images sont
    // déclenchées par le fil de cadence (V-Sync) ou par le timer si la V-Sync
    // est coupée, et SwapBuffers ne bloque plus.
    context.setContinuousRepainting(false);
    context.setRenderer(this);
    pacer=GpuFramePacer::acquire();
    pacer->add(this);
    // l'attache se fait à l'arrivée dans l'éditeur (parentHierarchyChanged),
    // une fois la classe dérivée entièrement construite ; le timer démarre
    // quand le module devient visible (updateUiTimer)
}

GpuModuleBase::~GpuModuleBase()
{
    if(pacer!=nullptr) pacer->remove(this);
    stopTimer();
    context.detach();   // déjà fait par detachGL() dans la classe dérivée
}

void GpuModuleBase::detachGL()
{
    stopTimer();
    quiescePacer();
    if(pacer!=nullptr) pacer->remove(this);   // plus aucun appel du fil de cadence
    context.detach();
}

void GpuModuleBase::applyPixelFormat()
{
    juce::OpenGLPixelFormat pf;
    pf.multisamplingLevel=(juce::uint8)msaa;
    context.setPixelFormat(pf);
    context.setMultisamplingEnabled(msaa>0);
}

void GpuModuleBase::setGraphics(int msaaSamples, bool vsync, int fps)
{
    // V-Sync : images calées sur l'écran (au plus frameHz par seconde) ;
    // sans V-Sync : cadence fixe du timer
    requestedFps=fps;
    const int hz=juce::jmax(1, juce::jmin(fps, maxFrameRate()));
    const bool pacingChanged=vsync!=vsyncWanted.load() || hz!=frameHz;
    vsyncWanted=vsync;
    frameHz=hz;
    frameHzA=hz;
    if(pacingChanged) updateUiTimer();
    if(msaaSamples!=msaa){
        // le format de pixels ne change qu'à la création du contexte
        msaa=msaaSamples;
        const bool wasAttached=context.isAttached();
        if(wasAttached){ quiescePacer(); context.detach(); }
        applyPixelFormat();
        if(wasAttached) context.attachTo(*this);
        updateRenderActive();
    }
}

juce::String GpuModuleBase::getRendererInfo()
{
    const juce::SpinLock::ScopedLockType sl(rendererLock);
    return rendererInfo;
}

void GpuModuleBase::updateAttachment()
{
    // JUCE crée / libère lui-même la fenêtre GL selon la visibilité ; on ne
    // détache que pour laisser passer un panneau de l'éditeur.
    if(suspended){
        if(context.isAttached()){ quiescePacer(); context.detach(); repaint(); }
    } else if(!context.isAttached() && getParentComponent()!=nullptr){
        context.attachTo(*this);
        repaint();
    }
    updateRenderActive();
}

// le fil de cadence ne doit plus toucher au contexte : drapeau baissé, puis
// attente d'un éventuel passage en cours (avant tout détachement)
void GpuModuleBase::quiescePacer()
{
    renderActive=false;
    if(pacer!=nullptr) pacer->barrier();
}

void GpuModuleBase::updateRenderActive()
{
    renderActive = isVisible() && !suspended && context.isAttached();
}

void GpuModuleBase::updateUiTimer()
{
    updateRenderActive();
    // module caché : ni images ni lecture au survol à entretenir
    if(!isVisible())          stopTimer();
    else if(vsyncWanted.load()) startTimerHz(30);        // calque 2D seulement
    else                        startTimerHz(frameHz);   // déclenche aussi les images
}

void GpuModuleBase::timerCallback()
{
    // Filet de sécurité : le drapeau de rendu pouvait rester baissé alors que
    // le module est visible et attaché (spectrogramme figé jusqu'à ce qu'on le
    // touche). Le thread de messages ne détache jamais entre quiescePacer() et
    // detach(), donc le relever ici est sans risque.
    if(!renderActive.load() && isVisible() && !suspended && context.isAttached()){
        updateRenderActive();
    }
    const bool vs=vsyncWanted.load();
    if(!vs && context.isAttached()) context.triggerRepaint();
    if(vs || ++uiTickDiv>=juce::jmax(1, frameHz/30)){ uiTickDiv=0; uiTick(); }
}

// Fil de cadence (une fois par composition de l'écran) : une image par
// rafraîchissement, sans dépasser frameHz, à pas fixe d'une image à l'autre
// (30 i/s sur un écran 60 Hz = exactement une composition sur deux).
void GpuModuleBase::onFrameTick(double ts)
{
    if(lastTickTs>0.0){
        const double dt=ts-lastTickTs;
        if(dt>0.002 && dt<0.1) framePeriod+=(dt-framePeriod)*0.05;   // période de l'écran (estimée)
    }
    lastTickTs=ts;
    if(!vsyncWanted.load() || !renderActive.load()) return;
    const double period=1.0/(double)juce::jmax(1, frameHzA.load());
    if(ts+framePeriod*0.5 < nextFrameTs) return;   // pas encore l'heure de la prochaine image
    // pas fixe ; recalage seulement après un vrai retard
    nextFrameTs = (ts-nextFrameTs > period*2.0 || nextFrameTs<=0.0) ? ts+period : nextFrameTs+period;
    context.triggerRepaint();
}

//==============================================================================
// Thread GL
//==============================================================================
std::unique_ptr<juce::OpenGLShaderProgram> GpuModuleBase::compileProgram(const char* vs, const char* fs, const char* gs)
{
    auto prog=std::make_unique<juce::OpenGLShaderProgram>(context);
    if(prog->addVertexShader(vs)
       && (gs==nullptr || prog->addShader(gs, GL_GEOMETRY_SHADER))
       && prog->addFragmentShader(fs) && prog->link()) return prog;
    DBG("GPU shader : " << prog->getLastError());
    glFailed=true;
    juce::MessageManager::callAsync([sp=juce::Component::SafePointer<juce::Component>(this)]{ if(sp) sp->repaint(); });
    return {};
}


void GpuModuleBase::onContextCreated()
{
    vsyncApplied=-1;   // synchro verticale appliquée à la 1re image
    auto str=[](GLenum e){ auto* s=(const char*)glGetString(e); return juce::String(s!=nullptr ? s : "?"); };
    {
        const juce::SpinLock::ScopedLockType sl(rendererLock);
        rendererInfo=str(GL_RENDERER)+"  -  OpenGL "+str(GL_VERSION).upToFirstOccurrenceOf(" ", false, false);
    }
    // Anticrénelage : le multi-échantillonnage seul ne lisse que les contours
    // des triangles ; l'intérieur des reliefs (reflets, grille, détails fins
    // calculés dans les shaders) restait crénelé, d'où un réglage sans effet
    // visible. On demande en plus un ombrage par échantillon quand la carte
    // le permet (vrai suréchantillonnage).
    // (JUCE ne charge pas les fonctions d'extension : on va chercher celle-ci)
    GLint sb=0, ns=0;
    glGetIntegerv(GL_SAMPLE_BUFFERS, &sb);
    glGetIntegerv(GL_SAMPLES, &ns);
    minSampleShadingFn=nullptr;
    if(sb>0 && ns>1 && juce::OpenGLHelpers::isExtensionSupported("GL_ARB_sample_shading"))
        minSampleShadingFn=juce::OpenGLHelpers::getExtensionFunction("glMinSampleShadingARB");
    sampleShadingOk = minSampleShadingFn!=nullptr;
}

void GpuModuleBase::beginSampleShading()
{
    if(!sampleShadingOk) return;
    glEnable(GL_SAMPLE_SHADING_ARB);
    using MinSampleShading=void (KHRONOS_APIENTRY*)(GLfloat);
    reinterpret_cast<MinSampleShading>(minSampleShadingFn)(1.f);   // chaque échantillon ombré
}

void GpuModuleBase::endSampleShading()
{
    if(sampleShadingOk) glDisable(GL_SAMPLE_SHADING_ARB);
}

void GpuModuleBase::applyVsync()
{
    // la cadence est donnée par le fil de cadence / le timer : SwapBuffers ne doit
    // jamais attendre (attente active de certains pilotes)
    if(vsyncApplied!=0){ context.setSwapInterval(0); vsyncApplied=0; }
}

//==============================================================================
// Calque 2D
//==============================================================================
void GpuModuleBase::paintScreenFrame(juce::Graphics& g)
{
    // coins arrondis + bord du verre, identiques au calque du châssis (qui ne
    // peut pas passer devant une fenêtre GL)
    Neu::drawScreenGlass(g, getLocalBounds().toFloat());
}

void GpuModuleBase::paintGlFailure(juce::Graphics& g)
{
    if(!glFailed.load()) return;
    g.setFont(Ui::iceland(12.f));
    g.setColour(Col::textMid);
    g.drawFittedText(TR("OpenGL 3.2 indisponible sur cette carte graphique"), getLocalBounds().reduced(10), juce::Justification::centred, 2);
}

//==============================================================================
// PsdLogMap
//==============================================================================
void PsdLogMap::rebuild(double sr)
{
    mappedSampleRate=sr;
    const float binHz=(float)sr/(float)LeakAudioProcessor::PSD_FFT_SIZE;
    // 160 points log de 20 Hz au haut de l'axe (20 kHz, ou Nyquist si étendu)
    const float ratio=(extendedAxis ? topHz(sr) : 20000.f)/20.f;
    const float stepOct=std::log2(ratio)/(float)(N-1);
    for(int p=0;p<N;++p){
        const float f=20.f*std::pow(ratio,(float)p/(float)(N-1));
        const float lo=f*std::pow(2.f,-stepOct*0.5f)/binHz, hi=f*std::pow(2.f,stepOct*0.5f)/binHz;
        if(hi-lo>1.5f){                      // plusieurs bins : max de la plage
            binLo[p]=juce::jlimit(1,LeakAudioProcessor::PSD_NUM_BINS-2,(int)std::floor(lo));
            binHi[p]=juce::jlimit(binLo[p]+1,LeakAudioProcessor::PSD_NUM_BINS-1,(int)std::ceil(hi));
            binFrac[p]=-1.f;
        } else {                             // graves : interpolation entre deux bins
            const float fb=juce::jlimit(1.f,(float)LeakAudioProcessor::PSD_NUM_BINS-2.f,f/binHz);
            binLo[p]=(int)std::floor(fb); binHi[p]=binLo[p]+1;
            binFrac[p]=fb-(float)binLo[p];
        }
    }
}

bool PsdLogMap::read(LeakAudioProcessor& proc, float* outDb)
{
    const double sr=proc.getCurrentSampleRate();
    if(sr>0.0 && sr!=mappedSampleRate) rebuild(sr);
    if(mappedSampleRate<=0.0) return false;
    proc.getPSDData(psd, LeakAudioProcessor::PSD_NUM_BINS);
    for(int p=0;p<N;++p){
        if(binFrac[p]<0.f){
            float db=-120.f;
            for(int b=binLo[p];b<binHi[p];++b) db=juce::jmax(db,psd[b]);
            outDb[p]=db;
        } else {
            outDb[p]=psd[binLo[p]]+(psd[binHi[p]]-psd[binLo[p]])*binFrac[p];
        }
    }
    return true;
}
