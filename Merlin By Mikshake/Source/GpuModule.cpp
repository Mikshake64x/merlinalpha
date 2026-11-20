#include "GpuModule.h"
#include "PluginEditor.h"     // Col::, Neu::, Ui::
#include "SystemLoopback.h"   // WindowsDisplay::waitForNextFrame
#include <map>

using namespace juce::gl;

namespace { juce::SpinLock rendererLock; juce::String rendererInfo; }

//==============================================================================
// GpuFramePacer — horloge d'images commune à tous les modules GPU : un fil
// dédié attend chaque composition de l'écran par Windows (DwmFlush) puis
// déclenche les images dues. Le signal de synchro de JUCE passait par le
// thread de messages : quand l'interface était chargée il arrivait en retard
// et une image sautait (saccades de l'oscilloscope).
//==============================================================================
// v1.81.57 : un fil par écran, calé sur SON rafraîchissement (WaitForVBlank) ;
// chaque module suit l'écran de sa fenêtre (voir followMonitor()).
struct GpuFramePacer : private juce::Thread
{
    explicit GpuFramePacer(void* mon) : juce::Thread("Merlin GPU frame pacer"), monitor(mon) { startThread(juce::Thread::Priority::high); }
    ~GpuFramePacer() override { stopThread(2000); }

    void add(GpuModuleBase* m)    { const juce::ScopedLock sl(lock); modules.addIfNotAlreadyThere(m); }
    void remove(GpuModuleBase* m) { const juce::ScopedLock sl(lock); modules.removeFirstMatchingValue(m); }
    void barrier()                { const juce::ScopedLock sl(lock); }   // attend la fin d'un passage en cours

    static std::shared_ptr<GpuFramePacer> acquire(void* mon)
    {
        static std::mutex m;
        static std::map<void*, std::weak_ptr<GpuFramePacer>> shared;
        const std::lock_guard<std::mutex> g(m);
        auto p=shared[mon].lock();
        if(p==nullptr){ p=std::make_shared<GpuFramePacer>(mon); shared[mon]=p; }
        return p;
    }
    void* const monitor;

private:
    void run() override
    {
        // Grille des rafraîchissements de l'écran (v1.81.57) : chaque signal est
        // rattaché à SON rafraîchissement. Avec deux écrans sur deux cartes,
        // WaitForVBlank / DwmFlush rendaient parfois la main deux fois par période
        // (doublon : ignoré) ; un réveil en retard compte quand même pour son
        // rafraîchissement (avant : image sautée, trou de 33 ms).
        double hz=WindowsDisplay::refreshHz(monitor), anchor=0.0, pMeas=0.0, prevTs=0.0;
        juce::int64 lastIdx=-1;
        juce::uint32 hzReadMs=juce::Time::getMillisecondCounter();
        while(!threadShouldExit()){
            if(!WindowsDisplay::waitForVBlank(monitor) && !WindowsDisplay::waitForNextFrame())
                juce::Thread::sleep(16);   // ni écran ni composition : ~60 Hz
            const double ts=juce::Time::getMillisecondCounterHiRes()*0.001;
            if(juce::Time::getMillisecondCounter()-hzReadMs>2000){ hz=WindowsDisplay::refreshHz(monitor); hzReadMs=juce::Time::getMillisecondCounter(); }
            const double pNom=hz>1.0 ? 1.0/hz : 1.0/60.0;
            // période réelle mesurée (Windows annonce 60 Hz pour un écran à 59,94...)
            if(prevTs>0.0){ const double dt=ts-prevTs; if(dt>0.8*pNom && dt<1.2*pNom) pMeas=pMeas>0.0 ? pMeas+(dt-pMeas)*0.01 : dt; }
            prevTs=ts;
            if(pMeas>0.0 && std::abs(pMeas-pNom)>0.1*pNom) pMeas=pNom;   // fréquence changée
            const double p=pMeas>0.0 ? pMeas : pNom;
            if(anchor<=0.0 || ts-anchor>(double)(lastIdx+4)*p+1.0){ anchor=ts; lastIdx=-1; }   // départ, ou longue pause
            const juce::int64 idx=(juce::int64)std::floor((ts-anchor)/p+0.25);
            const double off=ts-(anchor+(double)idx*p);   // retard sur le rafraîchissement supposé
            if(off<0.0)        anchor+=off;        // rafraîchissement plus tôt que prévu : la grille recule
            else if(off<0.5*p) anchor+=off*0.02;   // suit lentement (les réveils en retard ne pèsent presque pas)
            if(idx<=lastIdx) continue;              // doublon dans la même période
            lastIdx=idx;
            const double vblank=anchor+(double)idx*p;   // heure du rafraîchissement : horloge régulière des images
            const juce::ScopedLock sl(lock);
            for(auto* m : modules) m->onFrameTick(vblank);
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
    pacer=GpuFramePacer::acquire(nullptr);   // écran inconnu tant que la fenêtre n'existe pas
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
    followMonitor();
    const bool vs=vsyncWanted.load();
    if(!vs && context.isAttached()) context.triggerRepaint();
    if(vs || ++uiTickDiv>=juce::jmax(1, frameHz/30)){
        uiTickDiv=0;
        // le calque 2D (graduations) n'est repeint que sur demande : il doit
        // suivre un changement de fréquence d'échantillonnage
        const double sr=processor.getCurrentSampleRate();
        if(sr!=shownSampleRate){ shownSampleRate=sr; repaint(); }
        uiTick();
    }
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
    // cadence >= écran : une image à chaque rafraîchissement. Avant la v1.81.57,
    // le pas fixe (16,667 ms) dérivait face à un écran à 60,0x Hz et sautait un
    // rafraîchissement toutes les ~10 s (micro-freeze).
    if(period<=framePeriod*1.04){ nextFrameTs=ts; frameTickMs=ts*1000.0; context.triggerRepaint(); return; }
    if(ts+framePeriod*0.5 < nextFrameTs) return;   // pas encore l'heure de la prochaine image
    // pas fixe ; recalage seulement après un vrai retard
    nextFrameTs = (ts-nextFrameTs > period*2.0 || nextFrameTs<=0.0) ? ts+period : nextFrameTs+period;
    frameTickMs=ts*1000.0;
    context.triggerRepaint();
}

// fenêtre passée sur un autre écran : le module change de fil de cadence
void GpuModuleBase::followMonitor()
{
    auto* peer=getPeer();
    void* mon=peer!=nullptr ? WindowsDisplay::monitorOf(peer->getNativeHandle()) : nullptr;
    if(mon==nullptr || (pacer!=nullptr && pacer->monitor==mon)) return;
    auto next=GpuFramePacer::acquire(mon);
    if(pacer!=nullptr) pacer->remove(this);   // attend la fin d'un passage en cours (verrou)
    lastTickTs=0.0; nextFrameTs=0.0;          // horloge de l'autre écran
    pacer=next;
    pacer->add(this);
}

//==============================================================================
// Thread GL
//==============================================================================

// Intensité des couleurs : saturation + contraste des seules couleurs (même
// formule que VividEffect, PluginEditor.cpp). Gris, noir et blanc intacts.
static const char* kVividVS = R"(#version 150
out vec2 uv;
void main(){ vec2 p=vec2(float((gl_VertexID<<1)&2), float(gl_VertexID&2)); uv=p; gl_Position=vec4(p*2.0-1.0,0.0,1.0); }
)";
static const char* kVividFS = R"(#version 150
in vec2 uv; uniform sampler2D tex; uniform float sat, con; out vec4 o;
void main(){
    vec3 c=texture(tex,uv).rgb;
    float mx=max(c.r,max(c.g,c.b)), mn=min(c.r,min(c.g,c.b));
    float L=dot(c, vec3(0.2126,0.7152,0.0722));
    vec3 s=L+(c-L)*sat;
    float w=clamp((mx-mn)*4.0, 0.0, 1.0);
    vec3 k=(s-0.5)*con+0.5;
    o=vec4(clamp(mix(s,k,w), 0.0, 1.0), 1.0);
}
)";

// Intensité des couleurs : le module dessine dans une image hors écran, puis la
// passe la recopie dans la fenêtre. (1re version : relecture de la fenêtre
// après coup -> attente du pilote à chaque module, cadence tombée vers 24 i/s.)
void GpuModuleBase::renderOpenGL()
{
    GLint vp[4]={0,0,0,0};
    glGetIntegerv(GL_VIEWPORT, vp);   // tout le composant (posé par JUCE)
    const int w=vp[2], h=vp[3];
    const bool viv=Col::vivid.load()>0 && w>0 && h>0 && ensureVividTargets(w, h);
    GLint target=0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &target);
    if(!viv){ renderGL(); return; }

    glBindFramebuffer(GL_FRAMEBUFFER, vividMsFbo);
    glViewport(0, 0, w, h);
    renderGL();   // le module voit cette image comme sa cible (il lit GL_FRAMEBUFFER_BINDING)

    float sat=1.f, con=1.f;
    Col::vividParams(Col::vivid.load(), sat, con);
    GLint prevVao=0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, vividMsFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, vividFbo);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)target);
    glViewport(vp[0], vp[1], w, h);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    vividProg->use();
    vividProg->setUniform("tex", 0);
    vividProg->setUniform("sat", sat);
    vividProg->setUniform("con", con);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, vividTex);
    glBindVertexArray(vividVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    // état rendu à JUCE (composition du calque 2D)
    glBindVertexArray((GLuint)prevVao);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void GpuModuleBase::openGLContextClosing()
{
    closingGL();
    releaseVivid();
    vividProg.reset();
    if(vividVao){ glDeleteVertexArrays(1, &vividVao); vividVao=0; }
}

void GpuModuleBase::releaseVivid()
{
    if(vividMsFbo)  { glDeleteFramebuffers(1, &vividMsFbo); vividMsFbo=0; }
    if(vividMsColor){ glDeleteRenderbuffers(1, &vividMsColor); vividMsColor=0; }
    if(vividMsDepth){ glDeleteRenderbuffers(1, &vividMsDepth); vividMsDepth=0; }
    if(vividFbo)    { glDeleteFramebuffers(1, &vividFbo); vividFbo=0; }
    if(vividTex)    { glDeleteTextures(1, &vividTex); vividTex=0; }
    vividW=vividH=0;
}

// cible hors écran à la taille du composant, même anticrénelage que la fenêtre
bool GpuModuleBase::ensureVividTargets(int w, int h)
{
    if(vividProg==nullptr){ vividProg=compileProgram(kVividVS, kVividFS); if(vividProg==nullptr) return false; }
    if(vividVao==0) glGenVertexArrays(1, &vividVao);
    GLint samples=0;
    {
        GLint cur=0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &cur);
        if(cur!=(GLint)vividMsFbo) glGetIntegerv(GL_SAMPLES, &samples);   // celui de la cible de JUCE
        else samples=vividSamples;
    }
    GLint maxSamples=0; glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples=juce::jlimit(0, juce::jmax(0, (int)maxSamples), (int)samples);
    if(vividMsFbo!=0 && w==vividW && h==vividH && samples==vividSamples) return true;
    releaseVivid();
    GLint prevFbo=0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGenRenderbuffers(1, &vividMsColor);
    glBindRenderbuffer(GL_RENDERBUFFER, vividMsColor);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);
    glGenRenderbuffers(1, &vividMsDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, vividMsDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, w, h);
    glGenFramebuffers(1, &vividMsFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, vividMsFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, vividMsColor);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, vividMsDepth);
    const bool okMs=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    glGenTextures(1, &vividTex);
    glBindTexture(GL_TEXTURE_2D, vividTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &vividFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, vividFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vividTex, 0);
    const bool ok=okMs && glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    if(!ok){ releaseVivid(); return false; }
    vividW=w; vividH=h; vividSamples=samples;
    return true;
}
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
    ++axisVersionCount;
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

bool PsdLogMap::readAt(LeakAudioProcessor& proc, double timeMs, float* outDb)
{
    const double sr=proc.getCurrentSampleRate();
    if(sr>0.0 && sr!=mappedSampleRate) rebuild(sr);
    if(mappedSampleRate<=0.0) return false;
    if(!proc.getPSDAt(timeMs, psd, LeakAudioProcessor::PSD_NUM_BINS)) proc.getPSDData(psd, LeakAudioProcessor::PSD_NUM_BINS);
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
