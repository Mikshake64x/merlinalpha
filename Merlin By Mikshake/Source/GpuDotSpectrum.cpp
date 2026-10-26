#include "GpuDotSpectrum.h"
#include "PluginEditor.h"   // Col::, Ui::, SpectroReadout

using namespace juce::gl;

// Grille et graduations de fréquence sous la zone de tracé (police courante) :
// lignes à toutes les graduations, libellés sans chevauchement (100, 1k, 10k
// d'abord) — en gros texte ou dans un slot étroit, "30501002005001k" ne colle plus
static void drawFreqGrid(juce::Graphics& g, juce::Rectangle<float> pr, float topHz, const std::function<float(float)>& xForHz)
{
    std::vector<Ui::AxisLabel> labels;
    for(auto [hz, lbl] : std::initializer_list<std::pair<float,const char*>>{
            {30.f,"30"},{50.f,"50"},{100.f,"100"},{200.f,"200"},{500.f,"500"},{1000.f,"1k"},
            {2000.f,"2k"},{5000.f,"5k"},{10000.f,"10k"},{20000.f,"20k"},{40000.f,"40k"},{80000.f,"80k"}}){
        if(hz>topHz*0.95f) continue;   // au-delà de 20 kHz seulement si l'axe y va
        const float x=std::round(xForHz(hz))+0.5f;
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawVerticalLine((int)x, pr.getY(), pr.getBottom());
        labels.push_back({ x, lbl, hz==100.f || hz==1000.f || hz==10000.f });
    }
    g.setColour(Col::textMid.withAlpha(0.75f));
    Ui::drawAxisLabels(g, labels, pr.getBottom()+2.f, 13.f*Ui::textScale, { pr.getX()-12.f, pr.getRight()+8.f });
}

//==============================================================================
// Shaders (GLSL 1.50, profil 3.2 core) — commentaires en ASCII uniquement :
// certains pilotes refusent les accents dans les sources GLSL.
//==============================================================================
#define DOT_GLSL_COMMON \
    "vec3 hsl2rgb(vec3 c){ vec3 rgb=clamp(abs(mod(c.x*6.0+vec3(0.0,4.0,2.0),6.0)-3.0)-1.0,0.0,1.0);\n" \
    "    return c.z+c.y*(rgb-0.5)*(1.0-abs(2.0*c.z-1.0)); }\n" \
    "float hueAt(float u){ return (52.0+u*218.0)/360.0; }\n"     /* jaune -> violet */

// Brume : quad de la zone de tracé généré sans tampon (gl_VertexID)
static const char* kHazeVS = R"(#version 150
uniform vec2 view;
uniform vec4 plot;            // x, y (haut), largeur, hauteur en px
out vec2 vPx;
void main()
{
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vec2 p = plot.xy + c*plot.zw;
    vPx = p;
    gl_Position = vec4(p.x/view.x*2.0-1.0, 1.0-p.y/view.y*2.0, 0.0, 1.0);
}
)";

static const char* kHazeFS = "#version 150\n"
    "in vec2 vPx;\n"
    "out vec4 fragColour;\n"
    "uniform sampler2D levels;\n"
    "uniform vec4 plot;\n"
    "uniform float strength;\n"
    "const float NB = 160.0;\n"
    DOT_GLSL_COMMON
    R"(
float lv(float u){ return texture(levels, vec2(clamp(u,0.0,1.0)*(NB-1.0)/NB+0.5/NB, 0.5)).r; }
void main()
{
    float u = (vPx.x-plot.x)/plot.z;
    float y = (plot.y+plot.w-vPx.y)/plot.w;        // 0 bas, 1 haut
    // flou gaussien horizontal (9 prises) du masque "sous la courbe" : lueur
    // concentree pres du sommet (la ou naissent les points), qui s'estompe
    // vers le bas et reste faible pour les niveaux bas
    float acc = 0.0, ws = 0.0;
    for(int k=-4; k<=4; ++k){
        float w = exp(-float(k*k)/7.0);
        float l = lv(u + float(k)*2.0/NB);
        float under = smoothstep(l+0.08, l-0.10, y);
        float nearTop = exp(-max(l-y, 0.0)*4.5);
        acc += w*under*(0.18+0.82*nearTop)*(0.1+0.9*l*l);
        ws  += w;
    }
    float h = acc/ws;
    vec3  c = hsl2rgb(vec3(hueAt(u), 0.95, 0.55));
    fragColour = vec4(c*h*strength, 1.0);
}
)";

// Particules : chaque point devient un petit quad dans un geometry shader
// (pas de point sprites : gl_PointCoord n'est pas fourni partout)
static const char* kDotVS = R"(#version 150
in vec4  pa;                  // x, y (px dans la zone, y depuis le bas), u, alpha
in float psize;               // diametre en px logiques
uniform vec2  view;
uniform vec4  plot;
out float gU;
out float gA;
out float gS;
void main()
{
    vec2 p = vec2(plot.x+pa.x, plot.y+plot.w-pa.y);
    gl_Position = vec4(p.x/view.x*2.0-1.0, 1.0-p.y/view.y*2.0, 0.0, 1.0);
    gU = pa.z; gA = pa.w; gS = psize;
}
)";

static const char* kDotGS = R"(#version 150
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
in float gU[];
in float gA[];
in float gS[];
uniform vec2 view;
out float vU;
out float vA;
out vec2  vC;                 // -1..1 dans le quad
void main()
{
    vec4 c = gl_in[0].gl_Position;
    vec2 halfNdc = vec2(gS[0]/view.x, gS[0]/view.y);   // (diametre/2) px -> NDC
    for(int k = 0; k < 4; ++k){
        vec2 o = vec2(float(k & 1)*2.0-1.0, float(k >> 1)*2.0-1.0);
        gl_Position = vec4(c.xy + o*halfNdc, 0.0, 1.0);
        vU = gU[0]; vA = gA[0]; vC = o;
        EmitVertex();
    }
    EndPrimitive();
}
)";

static const char* kDotFS = "#version 150\n"
    "in float vU;\n"
    "in float vA;\n"
    "in vec2  vC;\n"
    "out vec4 fragColour;\n"
    "uniform int style;\n"
    DOT_GLSL_COMMON
    R"(
void main()
{
    vec2  d  = vC;
    float r2 = dot(d, d);
    if(r2 > 1.0) discard;
    vec3  c = hsl2rgb(vec3(hueAt(vU), 0.95, 0.70));
    float a;
    if(style == 0){           // Chroma : point net, coeur un peu plus clair
        a = 1.0-smoothstep(0.35, 1.0, sqrt(r2));
        c = mix(c, vec3(1.0), 0.3*(1.0-r2));
    } else {                  // Nebula : coeur lumineux + halo
        float core = exp(-r2*9.0);
        a = core + exp(-r2*2.6)*0.5;
        c = mix(c, vec3(1.0), core*0.55);
    }
    fragColour = vec4(c*a*vA, 1.0);
}
)";

// Enveloppe (Nebula) : ruban de triangles, épaisseur donnée par halfW
static const char* kRibbonVS = R"(#version 150
in vec4 rb;                   // x, y (px), u, cote (-1 / +1)
in vec2 nrm;                  // normale a la courbe
uniform vec2  view;
uniform float halfW;
out float vU;
out float vSide;
void main()
{
    vec2 p = rb.xy + nrm*rb.w*halfW;
    gl_Position = vec4(p.x/view.x*2.0-1.0, 1.0-p.y/view.y*2.0, 0.0, 1.0);
    vU = rb.z; vSide = rb.w;
}
)";

static const char* kRibbonFS = "#version 150\n"
    "in float vU;\n"
    "in float vSide;\n"
    "out vec4 fragColour;\n"
    "uniform float glow;\n"
    DOT_GLSL_COMMON
    R"(
void main()
{
    float s = abs(vSide);
    float a = glow > 0.5 ? exp(-s*s*3.5)*0.5 : 1.0-smoothstep(0.45, 1.0, s);
    vec3  c = hsl2rgb(vec3(hueAt(vU), 0.95, glow > 0.5 ? 0.58 : 0.80));
    fragColour = vec4(c*a, 1.0);
}
)";

//==============================================================================
GpuDotSpectrumDisplay::GpuDotSpectrumDisplay(LeakAudioProcessor& p, Style s)
    : GpuModuleBase(p), style(s)
{
    relFader=std::make_unique<ReleaseFader>();
    addAndMakeVisible(*relFader);
    releaseSec=releaseRef();
    relFader->setValue(releaseRef(), juce::dontSendNotification);
    relFader->onValueChange=[this](float v){ releaseRef()=v; releaseSec=v; };
    for(auto& v : uiDb) v=kDbMin-10.f;
    for(auto& v : smoothDb) v=kDbMin-10.f;
    parts.reserve(4096);
    logMap.setExtendedAxis(true);   // axe jusqu'à la moitié de la fréquence d'échantillonnage
}

GpuDotSpectrumDisplay::~GpuDotSpectrumDisplay()
{
    detachGL();
}

void GpuDotSpectrumDisplay::syncFromProcessor()
{
    releaseSec=releaseRef();
    relFader->setValue(releaseRef(), juce::dontSendNotification);
}

float& GpuDotSpectrumDisplay::releaseRef()
{
    return style==Style::Chroma ? processor.dotChromaReleaseSec : processor.dotNebulaReleaseSec;
}

const char* GpuDotSpectrumDisplay::title() const
{
    return style==Style::Chroma ? "DOT SPECTRUM - CHROMA" : "DOT SPECTRUM - NEBULA";
}

void GpuDotSpectrumDisplay::resized()
{
    viewW=juce::jmax(1, getWidth());
    viewH=juce::jmax(1, getHeight());
    // marges : titre en haut, fader REL puis dB à gauche, fréquences en bas
    const float padT=24.f*Ui::textScale, padL=kReleaseFaderW+30.f*Ui::textScale, padB=17.f*Ui::textScale, padR=10.f;
    plotX=padL; plotY=padT;
    plotW=juce::jmax(10.f, (float)getWidth()-padL-padR);
    plotH=juce::jmax(10.f, (float)getHeight()-padT-padB);
    relFader->setBounds(2, (int)padT, (int)kReleaseFaderW-4, juce::jmax(10, (int)plotH.load()));
}

void GpuDotSpectrumDisplay::mouseMove(const juce::MouseEvent& e)
{
    hoverX=e.position.x; hoverOn=true;
    repaint();
}
void GpuDotSpectrumDisplay::mouseExit(const juce::MouseEvent&)
{
    hoverOn=false;
    repaint();
}

void GpuDotSpectrumDisplay::uiTick()
{
    if(hoverOn.load()) repaint();   // la lecture suit le niveau en direct
}

//==============================================================================
// Calque 2D : grille, graduations, titre, lecture au survol, cadre
//==============================================================================
void GpuDotSpectrumDisplay::paint(juce::Graphics& g)
{
    const int W=getWidth(), H=getHeight();
    if(W<40||H<40) return;
    const bool gl=isGlAttached();
    if(!gl) g.fillAll(Col::bg);

    const juce::Rectangle<float> pr(plotX.load(), plotY.load(), plotW.load(), plotH.load());
    const float topHz=PsdLogMap::topHz(processor.getCurrentSampleRate());
    auto xForHz=[&](float hz){ return pr.getX()+std::log(hz/20.f)/std::log(topHz/20.f)*pr.getWidth(); };
    auto yForDb=[&](float db){ return pr.getBottom()-(db-kDbMin)/(kDbMax-kDbMin)*pr.getHeight(); };

    // grille discrète (le nuage reste la vedette) + graduations
    g.setFont(Ui::iceland(9.5f));
    drawFreqGrid(g, pr, topHz, xForHz);
    for(float db : { -80.f, -60.f, -40.f, -20.f }){
        const float y=std::round(yForDb(db))+0.5f;
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawHorizontalLine((int)y, pr.getX(), pr.getRight());
        g.setColour(Col::textMid.withAlpha(0.75f));
        g.drawText(juce::String((int)db), juce::Rectangle<float>(kReleaseFaderW, y-7.f, pr.getX()-kReleaseFaderW-5.f, 14.f), juce::Justification::centredRight, false);
    }

    g.setFont(Ui::iceland(10.5f).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    g.drawFittedText(TR(title()), 6, 5, W-12, 13, juce::Justification::left, 1, 0.7f);

    // lecture au survol : fréquence sous la souris + niveau lissé en direct
    const float mx=hoverX.load();
    if(gl && hoverOn.load() && mx>=pr.getX() && mx<=pr.getRight()){
        const float u=(mx-pr.getX())/pr.getWidth();
        const float fi=u*(float)(NB-1);
        const int   i0=juce::jlimit(0, NB-2, (int)fi);
        float db;
        {
            const juce::SpinLock::ScopedLockType sl(uiLock);
            db=uiDb[i0]+(uiDb[i0+1]-uiDb[i0])*(fi-(float)i0);
        }
        const float ly=juce::jlimit(pr.getY(), pr.getBottom(), yForDb(db));
        g.setColour(Col::accent.withAlpha(0.45f));
        g.drawVerticalLine((int)mx, pr.getY(), pr.getBottom());
        g.setColour(juce::Colours::white);
        g.fillEllipse(juce::Rectangle<float>(7.f, 7.f).withCentre({ mx, ly }));
        g.setColour(Col::accent);
        g.drawEllipse(juce::Rectangle<float>(9.f, 9.f).withCentre({ mx, ly }), 1.2f);
        SpectroReadout::draw(g, getLocalBounds().toFloat(), { mx, ly }, 20.f*std::pow(topHz/20.f, u), db, 0.f);
    }

    paintGlFailure(g);
    paintScreenFrame(g);
}

//==============================================================================
// Thread GL
//==============================================================================
void GpuDotSpectrumDisplay::newOpenGLContextCreated()
{
    glFailed=false;
    hazeShader  =compileProgram(kHazeVS,   kHazeFS);
    dotShader   =compileProgram(kDotVS,    kDotFS, kDotGS);
    ribbonShader=compileProgram(kRibbonVS, kRibbonFS);
    if(hazeShader==nullptr || dotShader==nullptr || ribbonShader==nullptr) return;

    GLint prevVao=0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);

    glGenVertexArrays(1, &vaoQuad);   // quad sans tampon (gl_VertexID)

    glGenVertexArrays(1, &vaoDots);
    glBindVertexArray(vaoDots);
    glGenBuffers(1, &vboDots);
    glBindBuffer(GL_ARRAY_BUFFER, vboDots);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);
    {
        const GLint pa=glGetAttribLocation(dotShader->getProgramID(), "pa");
        const GLint ps=glGetAttribLocation(dotShader->getProgramID(), "psize");
        if(pa>=0){ glEnableVertexAttribArray((GLuint)pa); glVertexAttribPointer((GLuint)pa, 4, GL_FLOAT, GL_FALSE, 5*sizeof(float), nullptr); }
        if(ps>=0){ glEnableVertexAttribArray((GLuint)ps); glVertexAttribPointer((GLuint)ps, 1, GL_FLOAT, GL_FALSE, 5*sizeof(float), (const void*)(4*sizeof(float))); }
    }

    glGenVertexArrays(1, &vaoRibbon);
    glBindVertexArray(vaoRibbon);
    glGenBuffers(1, &vboRibbon);
    glBindBuffer(GL_ARRAY_BUFFER, vboRibbon);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);
    {
        const GLint rb=glGetAttribLocation(ribbonShader->getProgramID(), "rb");
        const GLint nr=glGetAttribLocation(ribbonShader->getProgramID(), "nrm");
        if(rb>=0){ glEnableVertexAttribArray((GLuint)rb); glVertexAttribPointer((GLuint)rb, 4, GL_FLOAT, GL_FALSE, 6*sizeof(float), nullptr); }
        if(nr>=0){ glEnableVertexAttribArray((GLuint)nr); glVertexAttribPointer((GLuint)nr, 2, GL_FLOAT, GL_FALSE, 6*sizeof(float), (const void*)(4*sizeof(float))); }
    }
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // niveaux lissés : texture 160 x 1 filtrée (la brume interpole entre bandes)
    glGenTextures(1, &levelTex);
    glBindTexture(GL_TEXTURE_2D, levelTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, NB, 1, 0, GL_RED, GL_FLOAT, smooth);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    lastFrameMs=juce::Time::getMillisecondCounterHiRes();
    onContextCreated();
}

void GpuDotSpectrumDisplay::openGLContextClosing()
{
    for(auto* b : { &vboDots, &vboRibbon }) if(*b){ glDeleteBuffers(1, b); *b=0; }
    for(auto* a : { &vaoQuad, &vaoDots, &vaoRibbon }) if(*a){ glDeleteVertexArrays(1, a); *a=0; }
    if(levelTex){ glDeleteTextures(1, &levelTex); levelTex=0; }
    hazeShader.reset(); dotShader.reset(); ribbonShader.reset();
}

// Niveaux : brut (naissance des particules) et lissé (brume, hauteur, survol) —
// montée rapide, retombée douce, indépendants de la cadence d'images.
void GpuDotSpectrumDisplay::updateLevels(float dt)
{
    if(!logMap.read(processor, rawDb)) return;
    // même balistique que le Spectrum : montée 5 ms, retombée = fader REL
    const float kUp=1.f-std::exp(-dt/0.005f), kDown=1.f-std::exp(-dt/juce::jmax(0.01f, releaseSec.load()));
    for(int i=0;i<NB;++i){
        raw[i]=juce::jlimit(0.f, 1.f, (rawDb[i]-kDbMin)/(kDbMax-kDbMin));
        smooth[i]  +=(raw[i]-smooth[i])    *(raw[i]>smooth[i]     ? kUp : kDown);
        smoothDb[i]+=(rawDb[i]-smoothDb[i])*(rawDb[i]>smoothDb[i] ? kUp : kDown);
    }
    const juce::SpinLock::ScopedLockType sl(uiLock);
    std::copy(std::begin(smoothDb), std::end(smoothDb), std::begin(uiDb));
}

// Particules : naissent sous la courbe à un rythme proportionnel à l'énergie
// (au cube : les crêtes scintillent, le bruit de fond reste clairsemé), montent
// et s'éteignent en douceur. Densité calée sur la largeur (même rendu qu'en preview).
void GpuDotSpectrumDisplay::updateParticles(float dt, float pw, float ph)
{
    const bool  chroma=style==Style::Chroma;
    const float widthScale=pw/600.f;
    // persistance (preview validée) : la release fait aussi durer les points —
    // longue = nuage qui s'attarde et s'éteint lentement, courte = scintillement
    const float lifeScale=juce::jlimit(0.35f, 6.f, std::pow(releaseSec.load()/0.4f, 0.75f));
    const float capScale=juce::jmin(3.f, 0.6f+lifeScale*0.6f);
    const size_t maxP=(size_t)juce::jlimit(400.f, 24000.f, (chroma ? 2600.f : 1300.f)*widthScale*capScale);
    const float binW=pw/(float)NB;
    const float baseRate=(chroma ? 130.f : 65.f)*widthScale;   // par seconde et par bande, à pleine échelle
    // halos Nebula proportionnés à la largeur (un petit slot garde des grains fins)
    const float glowScale=juce::jlimit(0.45f, 1.2f, pw/700.f);

    // près du plafond, toutes les bandes ralentissent ensemble (sinon les
    // aigus, servis en dernier, n'auraient plus de points)
    float total=0.f;
    // émission selon le niveau lissé : le nuage retombe au rythme du fader REL
    for(int i=0;i<NB;++i) total+=baseRate*std::pow(smooth[i], 2.2f)*dt;
    const float room=(float)(maxP-juce::jmin(maxP, parts.size()));
    const float scaleDown=total>room ? room/juce::jmax(1e-3f, total) : 1.f;
    for(int i=0;i<NB && parts.size()<maxP;++i){
        const float e=smooth[i];
        const float expected=baseRate*std::pow(e, 2.2f)*dt*scaleDown;
        int k=(int)expected+(rng.nextFloat()<expected-std::floor(expected) ? 1 : 0);
        while(k-->0 && parts.size()<maxP){
            Particle p;
            p.x=((float)i+0.5f)*binW+(rng.nextFloat()-0.5f)*binW*1.6f;
            p.y=std::pow(rng.nextFloat(), chroma ? 0.75f : 0.55f)*smooth[i]*ph;
            p.u=(float)i/(float)(NB-1);
            p.life=0.f;
            if(chroma){ p.maxLife=0.5f+rng.nextFloat()*0.7f; p.vy=4.f+rng.nextFloat()*10.f;  p.size=1.4f+rng.nextFloat()*2.2f+1.2f; }
            else      { p.maxLife=0.8f+rng.nextFloat()*0.9f; p.vy=14.f+rng.nextFloat()*30.f; p.size=(2.f+rng.nextFloat()*4.f)*3.6f*glowScale; }
            p.maxLife*=lifeScale;                 // vit plus longtemps…
            p.vy/=std::sqrt(lifeScale);           // …et monte plus lentement
            parts.push_back(p);
        }
    }

    partVerts.clear();
    partVerts.reserve(parts.size()*5);
    const float alphaMax=chroma ? 0.9f : 0.75f;
    for(size_t n=0;n<parts.size();){
        auto& p=parts[n];
        p.life+=dt;
        if(p.life>=p.maxLife || p.y>ph+12.f){ p=parts.back(); parts.pop_back(); continue; }
        p.y+=p.vy*dt;
        // apparition rapide puis extinction douce sur toute la durée de vie
        const float x=p.life/p.maxLife;
        const float a=juce::jmin(1.f, x*8.f)*std::pow(1.f-x, 1.3f)*alphaMax;
        partVerts.insert(partVerts.end(), { p.x, p.y, p.u, a, p.size });
        ++n;
    }
}

void GpuDotSpectrumDisplay::renderOpenGL()
{
    const auto bg=Col::bg;
    glClearColor(bg.getFloatRed(), bg.getFloatGreen(), bg.getFloatBlue(), 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    if(hazeShader==nullptr || dotShader==nullptr || ribbonShader==nullptr || vaoQuad==0) return;
    applyVsync();

    const double now=juce::Time::getMillisecondCounterHiRes();
    const float dt=(float)juce::jlimit(0.0, 0.1, (now-lastFrameMs)*0.001);
    lastFrameMs=now;

    const float px=plotX.load(), py=plotY.load(), pw=plotW.load(), ph=plotH.load();
    const int w=viewW.load(), h=viewH.load();
    updateLevels(dt);
    updateParticles(dt, pw, ph);

    const float scale=renderingScale();
    glViewport(0, 0, juce::roundToInt(scale*(float)w), juce::roundToInt(scale*(float)h));
    GLint prevVao=0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);   // tout s'additionne : lumière sur fond sombre

    // 1) brume colorée
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, levelTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, NB, 1, GL_RED, GL_FLOAT, smooth);
    hazeShader->use();
    hazeShader->setUniform("levels", 0);
    hazeShader->setUniform("view", (float)w, (float)h);
    hazeShader->setUniform("plot", px, py, pw, ph);
    hazeShader->setUniform("strength", style==Style::Chroma ? 0.9f : 0.55f);
    glBindVertexArray(vaoQuad);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // 2) particules
    if(!partVerts.empty()){
        dotShader->use();
        dotShader->setUniform("view", (float)w, (float)h);
        dotShader->setUniform("plot", px, py, pw, ph);
        dotShader->setUniform("style", (GLint)(style==Style::Chroma ? 0 : 1));
        glBindVertexArray(vaoDots);
        glBindBuffer(GL_ARRAY_BUFFER, vboDots);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(partVerts.size()*sizeof(float)), partVerts.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_POINTS, 0, (GLsizei)(partVerts.size()/5));
    }

    // 3) Nebula : enveloppe lumineuse (halo large puis trait net)
    if(style==Style::Nebula){
        ribbonVerts.clear();
        ribbonVerts.reserve((size_t)NB*12);
        // enveloppe lissée sur 5 bandes (1-2-3-2-1) : trait net sans dents
        // dans les aigus, où les bandes voisines varient beaucoup
        float env[NB];
        for(int i=0;i<NB;++i){
            float s=0.f, ws=0.f;
            for(int k=-2;k<=2;++k){
                const float w=(float)(3-std::abs(k));
                s+=w*smooth[juce::jlimit(0, NB-1, i+k)]; ws+=w;
            }
            env[i]=s/ws;
        }
        auto pt=[&](int i){ return juce::Point<float>(px+(float)i/(float)(NB-1)*pw, py+ph-env[i]*ph); };
        for(int i=0;i<NB;++i){
            const auto a=pt(juce::jmax(0,i-1)), b=pt(juce::jmin(NB-1,i+1)), p=pt(i);
            float nx=-(b.y-a.y), ny=b.x-a.x;
            const float len=std::sqrt(nx*nx+ny*ny); if(len>1e-4f){ nx/=len; ny/=len; } else { nx=0.f; ny=-1.f; }
            const float u=(float)i/(float)(NB-1);
            ribbonVerts.insert(ribbonVerts.end(), { p.x, p.y, u, -1.f, nx, ny,  p.x, p.y, u, 1.f, nx, ny });
        }
        ribbonShader->use();
        ribbonShader->setUniform("view", (float)w, (float)h);
        glBindVertexArray(vaoRibbon);
        glBindBuffer(GL_ARRAY_BUFFER, vboRibbon);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(ribbonVerts.size()*sizeof(float)), ribbonVerts.data(), GL_STREAM_DRAW);
        ribbonShader->setUniform("glow", 1.f);  ribbonShader->setUniform("halfW", 7.f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, NB*2);
        ribbonShader->setUniform("glow", 0.f);  ribbonShader->setUniform("halfW", 1.3f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, NB*2);
    }

    // état rendu à JUCE (composition du calque 2D)
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(0);
}

//==============================================================================
//==============================================================================
// GpuDotPanoramaDisplay
//==============================================================================
//==============================================================================

// Lueur : une gaussienne par bande, centrée sur (fréquence, panoramique),
// étirée verticalement selon la largeur stéréo de la bande
static_assert(LeakAudioProcessor::N_BANDS==74, "bands[74] du shader kPanHazeFS = N_BANDS");
static const char* kPanHazeFS = "#version 150\n"
    "in vec2 vPx;\n"
    "out vec4 fragColour;\n"
    "uniform vec4  plot;\n"
    "uniform float strength;\n"
    "uniform float bandW;\n"
    "uniform int   nBands;\n"
    "uniform vec4  bands[74];\n"      /* u, pan (-1 L .. +1 R), niveau 0..1, dispersion (74 = N_BANDS) */
    DOT_GLSL_COMMON
    R"(
void main()
{
    float u = (vPx.x-plot.x)/plot.z;
    float p = (vPx.y-plot.y)/plot.w*2.0-1.0;        // -1 at top (L), +1 at bottom (R)
    vec3 acc = vec3(0.0);
    for(int i=0; i<74; ++i){
        if(i >= nBands) break;
        vec4  b  = bands[i];
        float dx = (u-b.x)/(bandW*0.8);
        float dy = (p-b.y)/(b.w*1.3+0.08);
        acc += hsl2rgb(vec3(hueAt(b.x), 0.95, 0.55))*exp(-dx*dx-dy*dy)*b.z*b.z;
    }
    fragColour = vec4(acc*strength, 1.0);
}
)";

GpuDotPanoramaDisplay::GpuDotPanoramaDisplay(LeakAudioProcessor& p) : GpuModuleBase(p)
{
    parts.reserve(4096);
}

GpuDotPanoramaDisplay::~GpuDotPanoramaDisplay()
{
    detachGL();
}

void GpuDotPanoramaDisplay::resized()
{
    viewW=juce::jmax(1, getWidth());
    viewH=juce::jmax(1, getHeight());
    // marges : titre en haut, L/C/R à gauche, % à droite, fréquences en bas
    const float padT=24.f*Ui::textScale, padL=20.f*Ui::textScale, padR=28.f*Ui::textScale, padB=17.f*Ui::textScale;
    plotX=padL; plotY=padT;
    plotW=juce::jmax(10.f, (float)getWidth()-padL-padR);
    plotH=juce::jmax(10.f, (float)getHeight()-padT-padB);
}

void GpuDotPanoramaDisplay::mouseMove(const juce::MouseEvent& e)
{
    hoverX=e.position.x; hoverOn=true;
    repaint();
}
void GpuDotPanoramaDisplay::mouseExit(const juce::MouseEvent&)
{
    hoverOn=false;
    repaint();
}

void GpuDotPanoramaDisplay::uiTick()
{
    if(hoverOn.load()) repaint();
}

void GpuDotPanoramaDisplay::paint(juce::Graphics& g)
{
    const int W=getWidth(), H=getHeight();
    if(W<40||H<40) return;
    const bool gl=isGlAttached();
    if(!gl) g.fillAll(Col::bg);

    const juce::Rectangle<float> pr(plotX.load(), plotY.load(), plotW.load(), plotH.load());
    const float topHz=PsdLogMap::topHz(processor.getCurrentSampleRate());
    auto xForHz=[&](float hz){ return pr.getX()+std::log(hz/20.f)/std::log(topHz/20.f)*pr.getWidth(); };
    auto yForPan=[&](float p){ return pr.getY()+(p+1.f)*0.5f*pr.getHeight(); };

    g.setFont(Ui::iceland(9.5f));
    drawFreqGrid(g, pr, topHz, xForHz);
    // axe stéréo : L en haut, centre, R en bas ; échelle en % à droite
    for(auto [p, pct] : std::initializer_list<std::pair<float,const char*>>{
            {-1.f,"100"},{-0.5f,"50"},{0.f,"0"},{0.5f,"50"},{1.f,"100"}}){
        const float y=std::round(yForPan(p))+0.5f;
        if(p==0.f){
            // centre : même trait que le Panorama (halo d'accent + filet)
            g.setColour(Col::accent.withAlpha(0.12f));
            g.drawLine(pr.getX(), y, pr.getRight(), y, 2.4f);
            g.setColour(Col::gridCtr.withAlpha(0.85f));
            g.drawLine(pr.getX(), y, pr.getRight(), y, 0.8f);
        } else {
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.drawHorizontalLine((int)y, pr.getX(), pr.getRight());
        }
        g.setColour(Col::textMid.withAlpha(0.7f));
        g.drawText(pct, juce::Rectangle<float>(pr.getRight()+3.f, y-7.f, 30.f, 14.f), juce::Justification::centredLeft, false);
    }
    g.setFont(Ui::iceland(11.f));
    g.setColour(Col::textHi.withAlpha(0.8f));
    g.drawText("L", juce::Rectangle<float>(2.f, pr.getY()-1.f,       pr.getX()-4.f, 14.f), juce::Justification::centredRight, false);
    g.drawText("C", juce::Rectangle<float>(2.f, yForPan(0.f)-7.f,    pr.getX()-4.f, 14.f), juce::Justification::centredRight, false);
    g.drawText("R", juce::Rectangle<float>(2.f, pr.getBottom()-13.f, pr.getX()-4.f, 14.f), juce::Justification::centredRight, false);

    g.setFont(Ui::iceland(10.5f).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    g.drawFittedText(TR("PANORAMA - CHROMA"), 6, 5, W-12, 13, juce::Justification::left, 1, 0.7f);

    // survol : bande la plus proche de la souris
    const float mx=hoverX.load();
    if(gl && hoverOn.load() && mx>=pr.getX() && mx<=pr.getRight()){
        int n=0; float f=0.f, p=0.f, db=-120.f;
        {
            const juce::SpinLock::ScopedLockType sl(uiLock);
            n=uiN;
            float best=1e9f;
            for(int b=0;b<n;++b){
                const float d=std::abs(xForHz(uiFreq[b])-mx);
                if(d<best){ best=d; f=uiFreq[b]; p=uiPan[b]; db=uiDb[b]; }
            }
        }
        if(n>0){
            const float bx=xForHz(f), by=yForPan(juce::jlimit(-1.f, 1.f, p));
            g.setColour(Col::accent.withAlpha(0.45f));
            g.drawVerticalLine((int)bx, pr.getY(), pr.getBottom());
            g.setColour(juce::Colours::white);
            g.fillEllipse(juce::Rectangle<float>(7.f, 7.f).withCentre({ bx, by }));
            g.setColour(Col::accent);
            g.drawEllipse(juce::Rectangle<float>(9.f, 9.f).withCentre({ bx, by }), 1.2f);
            const juce::String panTxt = std::abs(p)<0.03f ? juce::String("C")
                                      : juce::String(p<0.f ? "L " : "R ")+juce::String(juce::roundToInt(std::abs(p)*100.f))+" %";
            SpectroReadout::drawLines(g, getLocalBounds().toFloat(), { bx, by }, SpectroReadout::formatHz(f), panTxt,
                                      db<=-119.f ? juce::String("--") : juce::String(db,1)+" dB");
        }
    }

    paintGlFailure(g);
    paintScreenFrame(g);
}

//==============================================================================
// Thread GL
//==============================================================================
void GpuDotPanoramaDisplay::newOpenGLContextCreated()
{
    glFailed=false;
    hazeShader=compileProgram(kHazeVS, kPanHazeFS);
    dotShader =compileProgram(kDotVS,  kDotFS, kDotGS);
    if(hazeShader==nullptr || dotShader==nullptr) return;

    GLint prevVao=0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGenVertexArrays(1, &vaoQuad);
    glGenVertexArrays(1, &vaoDots);
    glBindVertexArray(vaoDots);
    glGenBuffers(1, &vboDots);
    glBindBuffer(GL_ARRAY_BUFFER, vboDots);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);
    {
        const GLint pa=glGetAttribLocation(dotShader->getProgramID(), "pa");
        const GLint ps=glGetAttribLocation(dotShader->getProgramID(), "psize");
        if(pa>=0){ glEnableVertexAttribArray((GLuint)pa); glVertexAttribPointer((GLuint)pa, 4, GL_FLOAT, GL_FALSE, 5*sizeof(float), nullptr); }
        if(ps>=0){ glEnableVertexAttribArray((GLuint)ps); glVertexAttribPointer((GLuint)ps, 1, GL_FLOAT, GL_FALSE, 5*sizeof(float), (const void*)(4*sizeof(float))); }
    }
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    lastFrameMs=juce::Time::getMillisecondCounterHiRes();
    onContextCreated();
}

void GpuDotPanoramaDisplay::openGLContextClosing()
{
    if(vboDots){ glDeleteBuffers(1, &vboDots); vboDots=0; }
    for(auto* a : { &vaoQuad, &vaoDots }) if(*a){ glDeleteVertexArrays(1, a); *a=0; }
    hazeShader.reset(); dotShader.reset();
}

// Bandes : position stéréo, niveau et largeur (corrélation) lissés — montée
// rapide, retombée douce ; la résolution (15/31/62 bandes) peut changer.
void GpuDotPanoramaDisplay::updateBands(float dt)
{
    const int N=juce::jlimit(0, NMAX, processor.getNumActiveBands());
    const float* F=processor.getBandFreqs();
    if(N<=0 || F==nullptr){ nBands=0; return; }
    processor.getBandPan   (inPan,  N);
    processor.getBandLevels(inDb,   N);
    processor.getBandCorr  (inCorr, N);

    const bool resChanged=N!=nBands;
    nBands=N;
    const float kUp=1.f-std::exp(-dt/0.02f), kDown=1.f-std::exp(-dt/0.15f);
    const float kPan=1.f-std::exp(-dt/0.06f), kSpread=1.f-std::exp(-dt/0.15f);
    const float logTop=std::log(PsdLogMap::topHz(processor.getCurrentSampleRate())/20.f);   // axe jusqu'à la moitié de la fréquence d'échantillonnage
    for(int b=0;b<N;++b){
        bandU[b]=juce::jlimit(0.f, 1.f, std::log(F[b]/20.f)/logTop);
        const float e=juce::jlimit(0.f, 1.f, (inDb[b]-kDbMin)/(-kDbMin));
        const float p=juce::jlimit(-1.f, 1.f, inPan[b]);
        const float sp=0.045f+0.33f*juce::jlimit(0.f, 1.5f, 1.f-inCorr[b]);   // mono -> serré, large -> dispersé
        if(resChanged){ levS[b]=0.f; panS[b]=p; spreadS[b]=sp; dbS[b]=inDb[b]; }
        rawLev[b]=e;
        levS[b]   +=(e-levS[b])*(e>levS[b] ? kUp : kDown);
        dbS[b]    +=(inDb[b]-dbS[b])*(inDb[b]>dbS[b] ? kUp : kDown);
        panS[b]   +=(p-panS[b])*kPan;
        spreadS[b]+=(sp-spreadS[b])*kSpread;
        bandUniforms[b*4+0]=bandU[b];  bandUniforms[b*4+1]=panS[b];
        bandUniforms[b*4+2]=levS[b];   bandUniforms[b*4+3]=spreadS[b];
    }
    const juce::SpinLock::ScopedLockType sl(uiLock);
    uiN=N;
    for(int b=0;b<N;++b){ uiFreq[b]=F[b]; uiPan[b]=panS[b]; uiDb[b]=dbS[b]; }
}

// Points : naissent autour de la position stéréo de chaque bande (dispersion
// gaussienne selon sa largeur), dérivent un peu puis s'éteignent. Positions
// relatives à la zone de tracé (0..1) : quand le module change de slot ou de
// taille, les points déjà en vie restent à leur place au lieu de déborder.
void GpuDotPanoramaDisplay::updateParticles(float dt, float pw, float ph)
{
    const int N=nBands;
    if(N>0){
        const float widthScale=pw/600.f;
        const size_t maxP=(size_t)juce::jlimit(400.f, 12000.f, 2400.f*widthScale);
        const float baseRate=130.f*widthScale*31.f/(float)N;   // même densité quelle que soit la résolution
        const float bandWU=N>1 ? (bandU[N-1]-bandU[0])/(float)(N-1) : 0.05f;
        float total=0.f;
        for(int b=0;b<N;++b) total+=baseRate*std::pow(rawLev[b], 1.6f)*dt;
        const float room=(float)(maxP-juce::jmin(maxP, parts.size()));
        const float scaleDown=total>room ? room/juce::jmax(1e-3f, total) : 1.f;
        for(int b=0;b<N && parts.size()<maxP;++b){
            const float expected=baseRate*std::pow(rawLev[b], 1.6f)*dt*scaleDown;
            int k=(int)expected+(rng.nextFloat()<expected-std::floor(expected) ? 1 : 0);
            while(k-->0 && parts.size()<maxP){
                const float gauss=(rng.nextFloat()+rng.nextFloat()+rng.nextFloat()-1.5f)*0.8f;
                const float pan=juce::jlimit(-1.f, 1.f, panS[b]+gauss*spreadS[b]);
                Particle p;
                p.x=bandU[b]+(rng.nextFloat()-0.5f)*bandWU*1.2f;
                p.y=(pan+1.f)*0.5f;
                p.u=bandU[b];
                p.life=0.f;
                p.maxLife=0.5f+rng.nextFloat()*0.7f;
                p.vy=(rng.nextFloat()-0.5f)*10.f/juce::jmax(1.f, ph);   // ~±5 px/s
                p.size=2.6f+rng.nextFloat()*2.2f;
                parts.push_back(p);
            }
        }
    }

    partVerts.clear();
    partVerts.reserve(parts.size()*5);
    for(size_t n=0;n<parts.size();){
        auto& p=parts[n];
        p.life+=dt;
        if(p.life>=p.maxLife){ p=parts.back(); parts.pop_back(); continue; }
        p.y=juce::jlimit(0.f, 1.f, p.y+p.vy*dt);
        const float a=std::sin(juce::MathConstants<float>::pi*p.life/p.maxLife)*0.9f;
        partVerts.insert(partVerts.end(), { p.x*pw, (1.f-p.y)*ph, p.u, a, p.size });   // le shader compte y depuis le bas
        ++n;
    }
}

void GpuDotPanoramaDisplay::renderOpenGL()
{
    const auto bg=Col::bg;
    glClearColor(bg.getFloatRed(), bg.getFloatGreen(), bg.getFloatBlue(), 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    if(hazeShader==nullptr || dotShader==nullptr || vaoQuad==0) return;
    applyVsync();

    const double now=juce::Time::getMillisecondCounterHiRes();
    const float dt=(float)juce::jlimit(0.0, 0.1, (now-lastFrameMs)*0.001);
    lastFrameMs=now;

    const float px=plotX.load(), py=plotY.load(), pw=plotW.load(), ph=plotH.load();
    const int w=viewW.load(), h=viewH.load();
    updateBands(dt);
    updateParticles(dt, pw, ph);

    const float scale=renderingScale();
    glViewport(0, 0, juce::roundToInt(scale*(float)w), juce::roundToInt(scale*(float)h));
    GLint prevVao=0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    // 1) lueur par bande
    if(nBands>0){
        hazeShader->use();
        hazeShader->setUniform("view", (float)w, (float)h);
        hazeShader->setUniform("plot", px, py, pw, ph);
        hazeShader->setUniform("strength", 0.55f);
        hazeShader->setUniform("bandW", nBands>1 ? (bandU[nBands-1]-bandU[0])/(float)(nBands-1) : 0.05f);
        hazeShader->setUniform("nBands", (GLint)juce::jmin(nBands, NMAX));
        const GLint loc=glGetUniformLocation(hazeShader->getProgramID(), "bands");
        if(loc>=0) glUniform4fv(loc, juce::jmin(nBands, NMAX), bandUniforms);
        glBindVertexArray(vaoQuad);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // 2) points
    if(!partVerts.empty()){
        dotShader->use();
        dotShader->setUniform("view", (float)w, (float)h);
        dotShader->setUniform("plot", px, py, pw, ph);
        dotShader->setUniform("style", (GLint)0);   // points nets, comme Chroma
        glBindVertexArray(vaoDots);
        glBindBuffer(GL_ARRAY_BUFFER, vboDots);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(partVerts.size()*sizeof(float)), partVerts.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_POINTS, 0, (GLsizei)(partVerts.size()/5));
    }

    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(0);
}
