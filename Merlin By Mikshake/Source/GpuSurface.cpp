#include "GpuSurface.h"
#include "PluginEditor.h"   // Col::, Ui::, ReleaseFader, NeuAnimButton, SpectroReadout

using namespace juce::gl;

//==============================================================================
// Shaders (GLSL 1.50) — commentaires en ASCII uniquement
//
// Tout est dessine en 2D, en pixels logiques du module (meme repere que le
// module CPU) : le vertex shader ne fait que passer en coordonnees normalisees.
// Chaque sommet porte g = hauteur relative (position dans le degrade) et
// l'opacite de sa courbe.
//==============================================================================
static const char* kSurfVS = R"(#version 150
in vec4 v;              // x, y (pixels logiques), g (0..1), alpha
uniform vec2 view;      // taille logique du module
out float vG;
out float vA;
void main()
{
    vG = v.z; vA = v.w;
    gl_Position = vec4(v.x/view.x*2.0 - 1.0, 1.0 - v.y/view.y*2.0, 0.0, 1.0);
}
)";

static const char* kSurfFS = R"(#version 150
in float vG;
in float vA;
out vec4 fragColour;
uniform int  mode;      // 0 courbe (degrade inferno), 1 couleur unie, 2 degrade col -> col2
uniform vec4 col;
uniform vec4 col2;
const vec3 stops[8] = vec3[8](vec3(0.,0.,4.)/255.,     vec3(40.,11.,84.)/255.,  vec3(101.,21.,110.)/255.,
                              vec3(159.,42.,99.)/255., vec3(212.,72.,66.)/255., vec3(245.,125.,21.)/255.,
                              vec3(250.,193.,39.)/255.,vec3(252.,255.,164.)/255.);
vec3 inferno(float t)
{
    t = clamp(t, 0.0, 1.0)*7.0;
    int i = min(6, int(t));
    return mix(stops[i], stops[i+1], t - float(i));
}
// meme degrade que le Crystal (v1.50) : vert (niveau le plus faible) ->
// jaune -> orange -> rouge, violet seulement tout en haut (-10 dB = 1.0)
vec3 levelRamp(float t)
{
    t = clamp(t, 0.0, 1.0);
    const vec3 green  = vec3(0.10, 0.60, 0.22);
    const vec3 green2 = vec3(0.36, 0.86, 0.20);
    const vec3 yellow = vec3(1.00, 0.90, 0.15);
    const vec3 orange = vec3(1.00, 0.52, 0.06);
    const vec3 red    = vec3(0.92, 0.12, 0.10);
    const vec3 violet = vec3(0.62, 0.18, 0.92);
    if(t < 0.35) return mix(green,  green2, t/0.35);
    if(t < 0.55) return mix(green2, yellow, (t - 0.35)/0.20);
    if(t < 0.72) return mix(yellow, orange, (t - 0.55)/0.17);
    if(t < 0.92) return mix(orange, red,    (t - 0.72)/0.20);
    return mix(red, violet, (t - 0.92)/0.08);
}
void main()
{
    if(mode == 0){
        // degrade vert -> violet selon le niveau (essai demande par le user ;
        // avant : inferno du module CPU), base a 95 % d'opacite comme avant
        float t = clamp(vG, 0.0, 1.0);
        vec4 c = vec4(levelRamp(t), mix(0.95, 1.0, clamp(t/0.35, 0.0, 1.0)));
        fragColour = vec4(c.rgb, c.a*vA);
    } else if(mode == 2){
        vec4 c = mix(col, col2, clamp(vG, 0.0, 1.0));
        fragColour = vec4(c.rgb, c.a*vA);
    } else {
        fragColour = vec4(col.rgb, col.a*vA);
    }
}
)";

static_assert(PsdLogMap::N==160, "les courbes suivent les 160 points de PsdLogMap");

//==============================================================================
GpuSurfaceDisplay::GpuSurfaceDisplay(LeakAudioProcessor& p) : GpuModuleBase(p)
{
    // TIME : 1 à 20 s (milieu de course 4 s), 2.1 s par défaut — comme le module CPU
    timeFader=std::make_unique<ReleaseFader>();
    timeFader->configure("TIME", 1.f, 20.f, 4.f, 2.1f);
    timeFader->onValueChange=[this](float v){ processor.gpuSurfaceTimeSec=v; timeSecA=v; repaint(); };   // échelle de temps
    addAndMakeVisible(*timeFader);
    // HOLD : fige l'image pour l'examiner (le survol reste actif)
    holdBtn=std::make_unique<NeuAnimButton>("HOLD");
    holdBtn->setClickingTogglesState(true);
    holdBtn->onClick=[this]{ frozen=holdBtn->getToggleState(); };
    addAndMakeVisible(*holdBtn);
    lastCaptureMs=juce::Time::getMillisecondCounterHiRes();
    logMap.setExtendedAxis(true);   // axe jusqu'à Nyquist au-delà de 48 kHz (comme le module CPU)
    verts.reserve(200000);
    syncFromProcessor();
}

GpuSurfaceDisplay::~GpuSurfaceDisplay()
{
    detachGL();
}

void GpuSurfaceDisplay::syncFromProcessor()
{
    timeSecA=processor.gpuSurfaceTimeSec;
    timeFader->setValue(processor.gpuSurfaceTimeSec, juce::dontSendNotification);
    tiltA=processor.gpuSurfaceTilt;
    skewA=processor.gpuSurfaceSkew;
    reverseA=processor.gpuSurfaceReverse;
    repaint();
}

// Géométrie du module CPU (Spectrogram3DBase::paintCurves), en pixels logiques :
// profondeur 0 = devant (en bas), 1 = fond (en haut, décalé sur le côté).
GpuSurfaceDisplay::Geo GpuSurfaceDisplay::geometry(float W, float H) const
{
    Geo g;
    g.labelH=14.f*Ui::textScale;
    const float top=26.f;
    g.base=H-g.labelH-6.f;
    g.L0=kReleaseFaderW;
    const float aw=W-g.L0;
    g.pw=aw*0.86f;
    const float spare=aw-20.f-g.pw;
    const bool rev=reverseA.load();
    g.dir=rev ? -1.f : 1.f;
    g.x0=rev ? W-10.f-g.pw : g.L0+10.f;
    g.totalDx=spare*(0.6f+0.4f*skewA.load());
    g.totalDy=(g.base-top)*(0.22f+0.30f*juce::jmin(1.f, tiltA.load()));
    g.peakH=juce::jmax(10.f, (g.base-top)-g.totalDy-4.f);
    return g;
}

void GpuSurfaceDisplay::resized()
{
    viewW=juce::jmax(1, getWidth());
    viewH=juce::jmax(1, getHeight());
    // fader TIME sur le bord gauche, de la ligne du haut à la base des courbes
    const float labelH=14.f*Ui::textScale;
    const int top=26, base=juce::roundToInt((float)getHeight()-labelH-6.f);
    timeFader->setBounds(2, top, (int)kReleaseFaderW-4, juce::jmax(10, base-top));
    // HOLD dans l'en-tête, à gauche du bouton de sens de défilement
    const int hh=juce::jlimit(16, 22, juce::roundToInt(19.f*Ui::textScale));
    const int hw=juce::roundToInt(46.f*Ui::textScale);
    holdBtn->setBounds(getWidth()-6-16-6-hw, 2, hw, hh);
}

//==============================================================================
// Interface (comme le module CPU) : bouton de sens, glisser = perspective
//==============================================================================
void GpuSurfaceDisplay::mouseDown(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown()) return;   // laisse remonter pour le menu de slot
    tiltAtDragStart=processor.gpuSurfaceTilt;
    skewAtDragStart=processor.gpuSurfaceSkew;
    if(dirBtnBounds.contains(e.position)){
        processor.gpuSurfaceReverse=!processor.gpuSurfaceReverse;
        reverseA=processor.gpuSurfaceReverse;
        repaint();
    }
}
void GpuSurfaceDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown()) return;
    // glisser verticalement : hauteur de la perspective ; horizontalement :
    // décalage latéral des courbes anciennes
    processor.gpuSurfaceTilt=juce::jlimit(0.f, 1.8f, tiltAtDragStart-(float)e.getDistanceFromDragStartY()*0.004f);
    processor.gpuSurfaceSkew=juce::jlimit(-1.f, 1.f, skewAtDragStart+(float)e.getDistanceFromDragStartX()*0.004f);
    tiltA=processor.gpuSurfaceTilt;
    skewA=processor.gpuSurfaceSkew;
    hoverX=e.position.x; hoverY=e.position.y;
    repaint();   // repères de fréquence et de temps
}
void GpuSurfaceDisplay::mouseMove(const juce::MouseEvent& e)
{
    hoverX=e.position.x; hoverY=e.position.y;
    hoverOn=true;
}
void GpuSurfaceDisplay::mouseExit(const juce::MouseEvent&)
{
    hoverOn=false;
}

void GpuSurfaceDisplay::uiTick()
{
    // lecture au survol : le calque 2D n'est repeint que si le point a changé
    const bool v=hoverOn.load() && hitValid.load();
    const float t=hitTopY.load(), db=hitDb.load(), x=hoverX.load();
    if(v!=shownValid || (v && (std::abs(t-shownTop)>0.5f || std::abs(db-shownDb)>0.05f || std::abs(x-shownX)>0.5f))){
        shownValid=v; shownTop=t; shownDb=db; shownX=x;
        repaint();
    }
}

void GpuSurfaceDisplay::drawDirectionButton(juce::Graphics& g)
{
    const int W=getWidth();
    const float bs=16.f;
    dirBtnBounds=juce::Rectangle<float>((float)W-bs-6.f, 4.f, bs, bs);
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.fillRoundedRectangle(dirBtnBounds, 3.f);
    g.setColour(Col::border);
    g.drawRoundedRectangle(dirBtnBounds.reduced(0.5f), 3.f, 1.f);
    juce::Path arrow;
    const float cx=dirBtnBounds.getCentreX(), cy=dirBtnBounds.getCentreY(), s=4.5f;
    if(reverseA.load()) arrow.addTriangle(cx-s,cy, cx+s*0.4f,cy-s, cx+s*0.4f,cy+s);
    else                arrow.addTriangle(cx+s,cy, cx-s*0.4f,cy-s, cx-s*0.4f,cy+s);
    g.setColour(Col::accent.withAlpha(0.85f));
    g.fillPath(arrow);
}

// Calque 2D : composé par JUCE au-dessus du rendu GL (ou peint seul quand le
// rendu est suspendu) — mêmes repères que le module CPU.
void GpuSurfaceDisplay::paint(juce::Graphics& g)
{
    const int W=getWidth(), H=getHeight();
    if(W<40||H<40) return;
    const bool gl=isGlAttached();
    if(!gl) g.fillAll(Col::bg);

    const auto geo=geometry((float)W, (float)H);
    const float fTop=PsdLogMap::topHz(processor.getCurrentSampleRate());

    // repères de fréquence sous la courbe de devant (sans chevauchement)
    g.setFont(Ui::iceland(9.f));
    SpectroReadout::drawFreqMarks(g, fTop, [&](float f, juce::Point<float>& at){
                                      at={ geo.x0+std::log(f/20.f)/std::log(fTop/20.f)*geo.pw, geo.base+3.f }; return true; },
                                  geo.labelH, { geo.L0, 0.f, (float)W-geo.L0, (float)H });

    // échelle de temps le long du bord droit : 0 s au fond (la plus récente),
    // jusqu'à -TIME devant
    {
        const float T=juce::jmax(0.5f, timeSecA.load()), step=SpectroReadout::timeTickStep(T);
        g.setFont(Ui::iceland(8.5f));
        for(float t=0.f; t<=T+1e-3f; t+=step){
            const float d=1.f-t/T;
            const float ex=geo.x0+geo.dir*geo.totalDx*d+geo.pw, ey=geo.base-geo.totalDy*d;
            g.setColour(Col::textMid.withAlpha(0.6f));
            g.drawLine(ex+2.f, ey, ex+6.f, ey, 1.f);
            const auto lbl=SpectroReadout::formatTimeTick(t, step);
            const float w=juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), lbl)+2.f;
            const float lx=juce::jmin(ex+8.f, (float)W-w-2.f);
            SpectroReadout::drawShadowedText(g, lbl, { lx, ey-6.f, w, 12.f }, juce::Justification::centredLeft, Col::textMid.withAlpha(0.85f));
        }
    }

    drawDirectionButton(g);
    g.setFont(Ui::iceland(10.5f).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    Ui::drawTitleFit(g, TR("SPECTROGRAM GPU - SURFACE"), { 6.f, 5.f, (float)juce::jmax(20, holdBtn->getX()-12), 13.f }, g.getCurrentFont());   // plutôt que "GP…"

    // survol : trait vertical jusqu'à la base de la courbe + point + bulle
    if(gl && shownValid){
        const float hx=shownX, topY=shownTop, oy=hitBaseY.load();
        g.setColour(Col::accent.withAlpha(0.55f));
        g.drawLine(hx, topY, hx, oy, 1.f);
        g.setColour(juce::Colours::white);
        g.fillEllipse(juce::Rectangle<float>(7.f,7.f).withCentre({hx, topY}));
        g.setColour(Col::accent);
        g.drawEllipse(juce::Rectangle<float>(9.f,9.f).withCentre({hx, topY}), 1.2f);
        SpectroReadout::draw(g, getLocalBounds().toFloat(), { hx, topY }, hitHz.load(), shownDb, hitAgeSec.load());
    }

    paintGlFailure(g);
    paintScreenFrame(g);
}

//==============================================================================
// Thread GL
//==============================================================================
void GpuSurfaceDisplay::newOpenGLContextCreated()
{
    glFailed=false;
    shader=compileProgram(kSurfVS, kSurfFS);
    if(shader==nullptr) return;

    GLint prevVao=0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    const GLint loc=glGetAttribLocation(shader->getProgramID(), "v");
    if(loc>=0){
        glEnableVertexAttribArray((GLuint)loc);
        glVertexAttribPointer((GLuint)loc, 4, GL_FLOAT, GL_FALSE, 4*sizeof(float), nullptr);
    }
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    onContextCreated();
}

void GpuSurfaceDisplay::openGLContextClosing()
{
    if(vbo){ glDeleteBuffers(1, &vbo); vbo=0; }
    if(vao){ glDeleteVertexArrays(1, &vao); vao=0; }
    shader.reset();
}

void GpuSurfaceDisplay::renderOpenGL()
{
    const auto bg=Col::bg;
    glClearColor(bg.getFloatRed(), bg.getFloatGreen(), bg.getFloatBlue(), 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    if(shader==nullptr || vao==0) return;
    applyVsync();

    // Captures à pas fixe (une courbe toutes les TIME/95 s) ; entre deux, les
    // courbes glissent en continu vers l'avant, à chaque image.
    const double now=juce::Time::getMillisecondCounterHiRes();
    const double cap=captureMs();
    if(frozen.load()){
        lastCaptureMs=now-(double)glideFrac*cap;   // HOLD : rien ne bouge, reprise au même point
    } else {
        if(now-lastCaptureMs>=cap){
            lastCaptureMs = now-lastCaptureMs > 4.0*cap ? now : lastCaptureMs+cap;
            if(logMap.read(processor, liveDb)){
                head=(head+1)%NROWS;
                ++captureTotal;
                for(int p=0;p<NPTS;++p) rows[head][p]=juce::jlimit(0.f,1.f,(liveDb[p]+100.f)/90.f);   // -100 dB -> 0, -10 dB -> 1
                rowCount=juce::jmin(rowCount+1, NROWS);
            }
        }
        // ligne live : spectre courant à chaque image (elle se fige dans
        // l'historique à la capture suivante, au même endroit : pas de saut)
        if(logMap.read(processor, liveDb)){
            for(int p=0;p<NPTS;++p) liveRow[p]=juce::jlimit(0.f,1.f,(liveDb[p]+100.f)/90.f);
            haveLive=true;
        }
        glideFrac=(float)juce::jlimit(0.0, 1.0, (now-lastCaptureMs)/cap);
    }

    const float W=(float)viewW.load(), H=(float)viewH.load();
    if(W<40.f || H<40.f) return;
    const auto geo=geometry(W, H);
    const float scale=renderingScale();
    glViewport(0, 0, juce::roundToInt(scale*W), juce::roundToInt(scale*H));

    // ── sommets de l'image (pixels logiques) ──
    struct Draw { GLenum prim; int first, count, mode; juce::Colour c, c2; float lineAlpha; };
    std::vector<Draw> draws; draws.reserve(2*NROWS+6);
    verts.clear();
    auto vtx=[this](float x, float y, float gg, float a){ verts.push_back(x); verts.push_back(y); verts.push_back(gg); verts.push_back(a); };
    auto mark=[this]{ return (int)(verts.size()/4); };
    // trait JUCE de largeur w (px logiques) -> ligne GL d'1 px physique de même couverture
    auto cover=[scale](float w){ return juce::jlimit(0.f, 1.f, w*scale); };

    // plateau 0 : sol plat bleuté quadrillé sous les courbes, plus clair vers l'avant
    const juce::Point<float> fl(geo.x0, geo.base), fr(geo.x0+geo.pw, geo.base),
                             bl(geo.x0+geo.dir*geo.totalDx, geo.base-geo.totalDy), br(bl.x+geo.pw, bl.y);
    {
        const int s=mark();
        vtx(bl.x, bl.y, 0.f, 0.85f); vtx(br.x, br.y, 0.f, 0.85f);
        vtx(fl.x, fl.y, 1.f, 0.85f); vtx(fr.x, fr.y, 1.f, 0.85f);
        draws.push_back({ GL_TRIANGLE_STRIP, s, 4, 2, juce::Colour(0xFF2E2B66), juce::Colour(0xFF6A6FC8), 1.f });
    }
    {
        const int s=mark();
        for(int k=0;k<=16;++k){
            const float u=(float)k/16.f;
            const auto a=fl+(fr-fl)*u, b=bl+(br-bl)*u;
            vtx(a.x, a.y, 0.f, 1.f); vtx(b.x, b.y, 0.f, 1.f);
        }
        // lignes de temps : une toutes les 4 courbes, attachée à sa courbe
        for(int r=0;r<NROWS;++r){
            if(((captureTotal-(juce::uint32)r) & 3u)!=0u) continue;
            const float d=((float)r+glideFrac)/(float)(NROWS-1);   // 0 = au fond (la plus récente)
            if(d>1.f) continue;
            const auto a=bl+(fl-bl)*d, b=br+(fr-br)*d;
            vtx(a.x, a.y, 0.f, 1.f); vtx(b.x, b.y, 0.f, 1.f);
        }
        draws.push_back({ GL_LINES, s, mark()-s, 1, juce::Colour(0xFFC8D0FF).withAlpha(0.13f), {}, cover(0.6f) });
    }

    // courbes : la plus récente naît au fond, dessin du fond vers l'avant
    // (chaque courbe masque les précédentes) — remplissage puis contour ; la
    // ligne live (r = -1) reste au fond, devant le bord arrière du sol
    const int n=rowCount;
    for(int r=(haveLive ? -1 : 0);r<n;++r){
        const float* row = r<0 ? liveRow : rows[(head-r+NROWS)%NROWS];
        const float depth = r<0 ? 1.f : 1.f-((float)r+glideFrac)/(float)(NROWS-1);
        if(depth<0.f) continue;                                   // sortie par l'avant
        const float ox=geo.x0+geo.dir*geo.totalDx*depth, oy=geo.base-geo.totalDy*depth;
        const float a=juce::jmin(1.f, 1.f-(float)r/(float)NROWS); // 1 = récente, 0 = ancienne
        const float fa=0.35f+0.65f*a;                             // plus transparente au fond
        // (hauteur < 1,5 px : remplissage estompé, sinon des éclats d'un
        // pixel scintillent sur le sol)
        auto edgeA=[&](float v){ return juce::jlimit(0.f, 1.f, v*geo.peakH/1.5f); };
        int s=mark();
        for(int i=0;i<NPTS;++i){
            const float x=ox+(float)i/(float)(NPTS-1)*geo.pw;
            vtx(x, oy, 0.f, fa*edgeA(row[i]));
            vtx(x, oy-row[i]*geo.peakH, row[i], fa*edgeA(row[i]));
        }
        draws.push_back({ GL_TRIANGLE_STRIP, s, 2*NPTS, 0, {}, {}, 1.f });
        // contour : bord supérieur + côtés, sans la ligne de base, et estompé
        // là où la courbe est à plat sur le sol. Empilées sur le sol (aigus
        // vides, silences), ces lignes formaient des rayures serrées qui
        // moiraient en glissant : scintillement avec l'anticrénelage.
        s=mark();
        vtx(ox, oy, 0.f, edgeA(row[0]));
        for(int i=0;i<NPTS;++i) vtx(ox+(float)i/(float)(NPTS-1)*geo.pw, oy-row[i]*geo.peakH, 0.f, edgeA(row[i]));
        vtx(ox+geo.pw, oy, 0.f, edgeA(row[NPTS-1]));
        draws.push_back({ GL_LINE_STRIP, s, NPTS+2, 1, juce::Colours::black.withAlpha(0.35f), {}, cover(0.8f) });
    }

    // ── survol : la courbe visible sous la souris est la plus en avant qui
    // couvre le pixel (même règle que le module CPU) ──
    {
        bool hit=false;
        const float hx=hoverX.load(), hy=hoverY.load();
        const juce::Rectangle<float> dirBtn(W-16.f-6.f, 4.f, 16.f, 16.f);
        if(hoverOn.load() && (n>0 || haveLive) && !dirBtn.contains(hx, hy)){
            for(int r=n-1;r>=(haveLive ? -1 : 0);--r){   // de l'avant (la plus ancienne) vers le fond (ligne live)
                const float* row = r<0 ? liveRow : rows[(head-r+NROWS)%NROWS];
                const float depth = r<0 ? 1.f : 1.f-((float)r+glideFrac)/(float)(NROWS-1);
                if(depth<0.f) continue;
                const float ox=geo.x0+geo.dir*geo.totalDx*depth, oy=geo.base-geo.totalDy*depth;
                const float fi=(hx-ox)/geo.pw*(float)(NPTS-1);
                if(fi<0.f || fi>(float)(NPTS-1)) continue;
                const int   i0=juce::jmin(NPTS-2, (int)fi);
                const float v=row[i0]+(row[i0+1]-row[i0])*(fi-(float)i0);
                const float topY=oy-v*geo.peakH;
                if(hy<topY-2.f || hy>oy) continue;
                const float fTop=PsdLogMap::topHz(processor.getCurrentSampleRate());
                hitTopY=topY; hitBaseY=oy;
                hitHz=20.f*std::pow(fTop/20.f, fi/(float)(NPTS-1));
                hitDb=v*90.f-100.f;
                hitAgeSec=(float)juce::jmax(0, r)*(float)captureMs()*0.001f;   // ligne live : 0 s
                hit=true;
                break;
            }
        }
        hitValid=hit;
    }

    // ── tracé ──
    GLint prevVao=0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size()*sizeof(float)), verts.data(), GL_STREAM_DRAW);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    shader->use();
    shader->setUniform("view", W, H);
    int curMode=-1;
    for(const auto& d : draws){
        if(d.mode!=curMode){ shader->setUniform("mode", (GLint)d.mode); curMode=d.mode; }
        if(d.mode!=0){
            shader->setUniform("col", d.c.getFloatRed(), d.c.getFloatGreen(), d.c.getFloatBlue(), d.c.getFloatAlpha()*d.lineAlpha);
            if(d.mode==2) shader->setUniform("col2", d.c2.getFloatRed(), d.c2.getFloatGreen(), d.c2.getFloatBlue(), d.c2.getFloatAlpha());
        }
        glDrawArrays(d.prim, d.first, d.count);
    }

    // état rendu à JUCE (composition du calque 2D)
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}
