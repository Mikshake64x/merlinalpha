#include "GpuOscilloscope.h"
#include "PluginEditor.h"   // Col::, Ui::

using namespace juce::gl;

//==============================================================================
// Shaders (GLSL 1.50) — commentaires en ASCII uniquement
//==============================================================================
// Fond : quad plein ecran sans tampon, texture du fond (ligne 0 = haut)
static const char* kOscBgVS = R"(#version 150
out vec2 vUv;
void main()
{
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vUv = c;
    gl_Position = vec4(c*2.0-1.0, 0.0, 1.0);
}
)";

static const char* kOscBgFS = R"(#version 150
in vec2 vUv;
out vec4 fragColour;
uniform sampler2D tex;
void main() { fragColour = texture(tex, vec2(vUv.x, 1.0-vUv.y)); }
)";

// Trace : chaque segment devient un ruban anti-aliase (geometry shader)
static const char* kOscLineVS = R"(#version 150
in vec2 lv;                   // px logiques depuis le haut
uniform vec2 view;
void main() { gl_Position = vec4(lv.x/view.x*2.0-1.0, 1.0-lv.y/view.y*2.0, 0.0, 1.0); }
)";

static const char* kOscLineGS = R"(#version 150
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;
uniform vec2  view;
uniform float halfW;
out float vSide;
void main()
{
    vec2 ap = (gl_in[0].gl_Position.xy*0.5+0.5)*view;
    vec2 bp = (gl_in[1].gl_Position.xy*0.5+0.5)*view;
    vec2 d = bp-ap;
    float len = length(d);
    vec2 dir = len > 1e-4 ? d/len : vec2(1.0, 0.0);
    vec2 n = vec2(-dir.y, dir.x)*halfW;
    vec2 e = dir*halfW*0.5;
    vec2  q[4] = vec2[4](ap-e+n, ap-e-n, bp+e+n, bp+e-n);
    float s[4] = float[4](1.0, -1.0, 1.0, -1.0);
    for(int k = 0; k < 4; ++k){
        gl_Position = vec4(q[k]/view*2.0-1.0, 0.0, 1.0);
        vSide = s[k];
        EmitVertex();
    }
    EndPrimitive();
}
)";

static const char* kOscLineFS = R"(#version 150
in float vSide;
out vec4 fragColour;
uniform vec4  col;            // premultiplie
uniform float sharp;
void main() { fragColour = col*exp(-vSide*vSide*sharp); }
)";

// Aire sous la courbe (style deux voies)
static const char* kOscFillVS = R"(#version 150
in vec2 fv;
uniform vec2 view;
void main() { gl_Position = vec4(fv.x/view.x*2.0-1.0, 1.0-fv.y/view.y*2.0, 0.0, 1.0); }
)";

static const char* kOscFillFS = R"(#version 150
out vec4 fragColour;
uniform vec4 col;
void main() { fragColour = col; }
)";

//==============================================================================
GpuOscilloscopeDisplay::GpuOscilloscopeDisplay(LeakAudioProcessor& p) : GpuModuleBase(p)
{
    for(auto* v : { &rawL, &rawR, &trigSig }) v->assign((size_t)MAXN, 0.f);
    for(auto* v : { &bufL, &bufR, &bufM, &tmpM }) v->assign((size_t)kMaxWindow, 0.f);
    for(int i=0;i<kHist;++i){ histL[i].assign((size_t)kMaxWindow, 0.f); histR[i].assign((size_t)kMaxWindow, 0.f); }
    verts.reserve((size_t)kMaxWindow*4);

    // ST : traces L et R ; MONO : une trace (L+R)/2
    monoBtn.onClick=[this]{ processor.oscMono=!processor.oscMono; syncFromProcessor(); };
    addAndMakeVisible(monoBtn);

    // AMP (vertical, à gauche) et TIME (horizontal, en bas) : mêmes plages que
    // les boutons de la barre du bas
    ampFader=std::make_unique<ReleaseFader>();
    ampFader->configure("AMP", 0.5f, 4.f, 1.f, 1.f);
    ampFader->formatValue=[](float v){ return juce::String(v, 1)+"x"; };
    ampFader->onValueChange=[this](float v){
        processor.oscAmpVal=v; setAmplitude(v);
    };
    addAndMakeVisible(*ampFader);
    timeFader=std::make_unique<ReleaseFader>();
    timeFader->setHorizontal(true);
    timeFader->configure("TIME", 128.f, (float)kMaxWindow, 1024.f, 1200.f);
    timeFader->getDefault=[this]{ return (float)processor.oscDefaultWindow(); };   // double-clic : 25 ms
    timeFader->formatValue=[this](float v){
        const double sr=juce::jmax(1.0, processor.getCurrentSampleRate());
        return juce::String((double)v/sr*1000.0, 1)+" ms";
    };
    timeFader->onValueChange=[this](float v){
        const int n=juce::roundToInt(v);
        processor.oscTimeDiv=n; setTimeDiv(n);
    };
    addAndMakeVisible(*timeFader);
    syncFromProcessor();
}

void GpuOscilloscopeDisplay::refreshFaders()
{
    if(ampFader)  ampFader ->setValue(processor.oscAmpVal, juce::dontSendNotification);
    if(timeFader){ timeFader->setValue((float)processor.oscTimeDiv, juce::dontSendNotification); timeFader->repaint(); }
}

GpuOscilloscopeDisplay::~GpuOscilloscopeDisplay()
{
    detachGL();
}

int GpuOscilloscopeDisplay::maxFrameRate() const
{
    return juce::jlimit(10, 144, processor.oscFps);
}

void GpuOscilloscopeDisplay::syncFromProcessor()
{
    monoBtn.setButtonText(processor.oscMono ? "MONO" : "ST");
    styleA=juce::jlimit(0, 3, processor.oscStyle);
    monoA=processor.oscMono;
    persistA=processor.oscPersist;
    static const float widthMul[]={ 0.6f, 1.f, 1.6f, 2.4f };   // fin, normal, épais, très épais
    widthMulA=widthMul[juce::jlimit(0, 3, processor.oscLineWidth)];
    classicTraceA=processor.oscTraceMode==1;
    refreshFaders();
    rebuildBackgroundIfNeeded();
    refreshFrameRate();
    repaint();
}

void GpuOscilloscopeDisplay::resized()
{
    viewW=juce::jmax(1, getWidth());
    viewH=juce::jmax(1, getHeight());
    {   // bouton ST/MONO à l'échelle du texte (illisible en gros texte)
        const int bw=juce::roundToInt(34.f*Ui::textScale), bh=juce::roundToInt(12.f*Ui::textScale);
        monoBtn.setBounds(getWidth()-bw-4, 2, bw, bh);
    }
    // faders : AMP le long du bord gauche, TIME le long du bas
    if(ampFader)  ampFader ->setBounds(2, 22, (int)kFaderL-4, juce::jmax(10, getHeight()-22-(int)kFaderB-4));
    if(timeFader) timeFader->setBounds((int)kFaderL, getHeight()-(int)kFaderB, juce::jmax(60, getWidth()-(int)kFaderL-6), (int)kFaderB-2);
    rebuildBackgroundIfNeeded();
}

//==============================================================================
// Géométrie et fond (thread de messages)
//==============================================================================
// Les écrans laissent la place des faders : kFaderL à gauche, kFaderB en bas
juce::Rectangle<float> GpuOscilloscopeDisplay::tubeAreaFor(float W, float H)
{
    return { kFaderL, 22.f, W-kFaderL-6.f, H-27.f-kFaderB };
}

juce::Rectangle<float> GpuOscilloscopeDisplay::plotAreaFor(int style, float W, float H)
{
    switch(style){
        case 2:  return tubeAreaFor(W, H).reduced(14.f, 10.f);   // dans le tube
        case 3:  return { kFaderL+2.f, 24.f, W-kFaderL-10.f, H-30.f-kFaderB };
        default: return { kFaderL+2.f, 24.f, W-kFaderL-10.f, H-24.f-17.f-kFaderB };     // légendes en bas
    }
}

namespace
{
    void scopeGraticule(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool dotted, bool ticks, int rows=8)
    {
        g.setColour(c);
        for(int i=0;i<=10;++i){
            const float x=std::round(r.getX()+r.getWidth()*(float)i/10.f);
            if(dotted) for(float y=r.getY(); y<=r.getBottom(); y+=4.f) g.fillRect(x, y, 1.f, 1.f);
            else       g.fillRect(x, r.getY(), 1.f, r.getHeight());
        }
        for(int j=0;j<=rows;++j){
            const float y=std::round(r.getY()+r.getHeight()*(float)j/(float)rows);
            if(dotted) for(float x=r.getX(); x<=r.getRight(); x+=4.f) g.fillRect(x, y, 1.f, 1.f);
            else       g.fillRect(r.getX(), y, r.getWidth(), 1.f);
        }
        if(ticks){   // graduations fines sur les axes centraux (5 par division)
            const float cx=std::round(r.getCentreX()), cy=std::round(r.getCentreY());
            for(int k=0;k<=50;++k)     g.fillRect(std::round(r.getX()+r.getWidth()*(float)k/50.f), cy-2.f, 1.f, 5.f);
            for(int k=0;k<=rows*5;++k) g.fillRect(cx-2.f, std::round(r.getY()+r.getHeight()*(float)k/(float)(rows*5)), 5.f, 1.f);
        }
    }

    // zone d'une voie de "Deux voies" : grille légère et ligne du zéro
    void scopeLane(juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour(juce::Colour::fromRGBA(120,180,220,31));
        for(int i=0;i<=8;++i) g.fillRect(std::round(r.getX()+r.getWidth()*(float)i/8.f), r.getY(), 1.f, r.getHeight());
        g.setColour(juce::Colour::fromRGBA(120,200,240,77));
        g.fillRect(r.getX(), std::round(r.getCentreY()), r.getWidth(), 1.f);
    }

    juce::Rectangle<float> upperHalf(juce::Rectangle<float> r) { return r.withHeight(r.getHeight()*0.5f); }
    juce::Rectangle<float> lowerHalf(juce::Rectangle<float> r) { return r.withTrimmedTop(r.getHeight()*0.5f); }
    juce::Rectangle<float> upperLane(juce::Rectangle<float> r) { return r.withHeight(r.getHeight()*0.5f-3.f); }
    juce::Rectangle<float> lowerLane(juce::Rectangle<float> r) { return r.withTrimmedTop(r.getHeight()*0.5f+3.f); }
}

// fond, graticule et légendes fixes
void GpuOscilloscopeDisplay::paintBackground(juce::Graphics& g, int style, bool mono) const
{
    const float W=(float)getWidth(), H=(float)getHeight();
    const auto pr=plotAreaFor(style, W, H);
    g.setFont(Ui::iceland(9.f));
    auto label=[&](const juce::String& t, float x, float y, juce::Colour c){
        g.setColour(c); g.drawText(t, juce::Rectangle<float>(x, y, 60.f, 11.f), juce::Justification::left, false); };

    if(style==0){            // tube cathodique
        g.fillAll(juce::Colour(0xFF02121A));
        g.setGradientFill(juce::ColourGradient(juce::Colour::fromRGBA(20,90,110,89), W*0.5f, H*0.55f,
                                               juce::Colours::transparentBlack, W*1.2f, H*0.55f, true));
        g.fillAll();
        scopeGraticule(g, pr, juce::Colour::fromRGBA(90,220,255,41), false, true);
        // légendes placées selon leur largeur réelle ; la base de temps (« 2.50
        // ms/div », calque 2D, à droite) garde sa place : sinon « L » / « R »
        auto tw=[&](const juce::String& t){ return juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), t); };
        const float gap=6.f*Ui::textScale, perDivW=tw("00.00 ms/div")+8.f;
        const bool roomy=pr.getWidth() >= tw("CH1 L")+gap+tw("CH2 R")+perDivW;
        const juce::String l1=mono ? "L+R" : (roomy ? "CH1 L" : "L"), l2=roomy ? "CH2 R" : "R";
        label(l1, pr.getX(), pr.getBottom()+3.f, juce::Colour(0xFF8FF6FF));
        if(!mono) label(l2, pr.getX()+tw(l1)+gap, pr.getBottom()+3.f, juce::Colour(0xFF4AA3FF));
    }
    else if(style==1){       // numérique (DSO)
        g.fillAll(juce::Colour(0xFF050607));
        scopeGraticule(g, pr, juce::Colour::fromRGBA(200,200,200,72), true, false);
        g.setColour(juce::Colour::fromRGBA(200,200,200,90));
        g.fillRect(pr.getX(), std::round(pr.getCentreY()), pr.getWidth(), 1.f);
        auto chip=[&](const juce::String& t, float x, juce::Colour bg, juce::Colour fg){
            const juce::Rectangle<float> r(x, pr.getBottom()+4.f, 38.f, 11.f);
            g.setColour(bg); g.fillRoundedRectangle(r, 2.f);
            g.setColour(fg); g.drawText(t, r, juce::Justification::centred, false); };
        if(mono) chip("L+R", pr.getX(), juce::Colour(0xFF00E5FF), juce::Colour(0xFF03181C));
        else   { chip("CH1 L", pr.getX(), juce::Colour(0xFF00E5FF), juce::Colour(0xFF03181C));
                 chip("CH2 R", pr.getX()+42.f, juce::Colour(0xFFFF4FD8), juce::Colour(0xFF2A0622)); }
    }
    else if(style==2){       // vintage à lunette
        g.fillAll(juce::Colour(0xFF16181B));
        const auto tube=tubeAreaFor(W, H);
        juce::Path p; p.addRoundedRectangle(tube, 16.f);
        g.setColour(juce::Colour(0xFF030B09)); g.fillPath(p);
        {
            juce::Graphics::ScopedSaveState ss(g);
            g.reduceClipRegion(p);
            g.setGradientFill(juce::ColourGradient(juce::Colour::fromRGBA(40,140,110,72), tube.getCentreX(), tube.getCentreY(),
                                                   juce::Colour::fromRGBA(0,0,0,128), tube.getRight()+tube.getWidth()*0.1f, tube.getCentreY(), true));
            g.fillPath(p);
            scopeGraticule(g, pr, juce::Colour::fromRGBA(120,255,210,33), false, false);
            if(!mono){
                g.setColour(juce::Colour::fromRGBA(120,255,210,70));
                g.fillRect(pr.getX(), std::round(pr.getCentreY()), pr.getWidth(), 1.f);
                label("L", pr.getX()+3.f, pr.getY()+2.f, juce::Colour(0xFF7DFFD8));
                label("R", pr.getX()+3.f, pr.getCentreY()+3.f, juce::Colour(0xFF5FD3FF));
            }
            // reflet du verre bombé
            g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.07f), 0.f, tube.getY(),
                                                   juce::Colours::transparentWhite, 0.f, tube.getCentreY(), false));
            g.fillRect(tube.withHeight(tube.getHeight()*0.5f));
        }
        g.setColour(juce::Colour(0xFF2C3036)); g.strokePath(p, juce::PathStrokeType(3.f));
    }
    else {                   // deux voies
        g.fillAll(juce::Colour(0xFF04090D));
        if(mono){ scopeLane(g, pr); label("L+R", pr.getX()+4.f, pr.getY()+2.f, juce::Colour(0xFF22E0FF)); }
        else {
            scopeLane(g, upperLane(pr)); scopeLane(g, lowerLane(pr));
            g.setColour(juce::Colours::white.withAlpha(0.08f));
            g.fillRect(pr.getX(), std::round(pr.getCentreY()), pr.getWidth(), 1.f);
            label("L", pr.getX()+4.f, upperLane(pr).getY()+2.f, juce::Colour(0xFF22E0FF));
            label("R", pr.getX()+4.f, lowerLane(pr).getY()+2.f, juce::Colour(0xFF3D8BFF));
        }
    }
}

// Le fond est redessiné (image logicielle, résolution physique) quand la
// taille, l'échelle, le style ou le mode changent, puis envoyé en texture.
void GpuOscilloscopeDisplay::rebuildBackgroundIfNeeded()
{
    const int W=getWidth(), H=getHeight();
    if(W<10 || H<10) return;
    double scale=juce::Component::getApproximateScaleFactorForComponent(this);
    if(auto* d=juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds())) scale*=d->scale;
    scale=juce::jlimit(1.0, 4.0, scale);
    const int style=styleA.load();
    const bool mono=monoA.load();
    const int key=(((W*4099+H)*5+style)*2+(mono ? 1 : 0))*131+juce::roundToInt(scale*100.0);
    if(key==bgKey) return;
    bgKey=key;
    juce::Image img(juce::Image::ARGB, juce::jmax(1, juce::roundToInt((double)W*scale)), juce::jmax(1, juce::roundToInt((double)H*scale)),
                    true, juce::SoftwareImageType());
    {
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::scale((float)scale));
        paintBackground(g, style, mono);
    }
    {
        const juce::SpinLock::ScopedLockType sl(bgLock);
        bgImage=img;
    }
    ++bgVersion;
    repaint();
}

//==============================================================================
// Calque 2D : titre, repère de déclenchement, mesures
//==============================================================================
void GpuOscilloscopeDisplay::uiTick()
{
    rebuildBackgroundIfNeeded();   // changement d'écran / d'échelle
    if(styleA.load()==1){
        const float vpp=measVpp.load(), hz=measHz.load();
        const juce::String m="Vpp "+juce::String(vpp, 2)+"   f "+(hz>0.f ? juce::String(juce::roundToInt(hz))+" Hz" : juce::String("--"));
        if(m!=shownMeas){ shownMeas=m; repaint(); }
    }
}

void GpuOscilloscopeDisplay::paint(juce::Graphics& g)
{
    const int W=getWidth(), H=getHeight();
    if(W<40 || H<40) return;
    const bool gl=isGlAttached();
    if(!gl){   // rendu suspendu (panneau ouvert) : le fond seul
        juce::Image img;
        { const juce::SpinLock::ScopedLockType sl(bgLock); img=bgImage; }
        if(img.isValid()) g.drawImage(img, getLocalBounds().toFloat());
        else              g.fillAll(Col::bg);
    }

    const int  style=styleA.load();
    const bool mono=monoA.load();
    const auto pr=plotAreaFor(style, (float)W, (float)H);

    if(triggerMode.load() && style!=3){
        const juce::Colour c = style==1 ? juce::Colour(0xFFFF9F0A)
                             : style==2 ? juce::Colour(0xFF7DFFD8).withAlpha(0.8f)
                                        : juce::Colour(0xFF8FF6FF).withAlpha(0.8f);
        // en ST dans le tube, sur le zéro de la voie L
        const float cy=(style==2 && !mono) ? upperHalf(pr).getCentreY() : pr.getCentreY();
        juce::Path tri; tri.addTriangle(pr.getX(), cy-4.f, pr.getX()+6.f, cy, pr.getX(), cy+4.f);
        g.setColour(c); g.fillPath(tri);
    }

    const double sr=juce::jmax(1.0, processor.getCurrentSampleRate());
    const juce::String perDiv=juce::String((double)nPts.load()/sr*100.0, 2)+" ms/div";   // 10 divisions
    g.setFont(Ui::iceland(9.f));
    if(style==0){
        g.setColour(juce::Colour(0xB38FF6FF));
        g.drawText(perDiv, juce::Rectangle<float>(pr.getRight()-90.f, pr.getBottom()+3.f, 90.f, 11.f), juce::Justification::right, false);
    } else if(style==1){
        g.setColour(juce::Colour(0xD9DCDCDC));
        const float left=mono ? 44.f : 86.f;
        g.drawText(shownMeas+"   "+perDiv, juce::Rectangle<float>(pr.getX()+left, pr.getBottom()+3.f, pr.getWidth()-left, 12.f),
                   juce::Justification::right, false);
    }

    g.setFont(Ui::iceland(10.5f).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    g.drawFittedText(TR("OSCILLOSCOPE"), 6, 5, juce::jmax(20, monoBtn.getX()-10), 13, juce::Justification::left, 1, 0.6f);

    paintGlFailure(g);
    paintScreenFrame(g);
}

//==============================================================================
// Thread GL
//==============================================================================
void GpuOscilloscopeDisplay::newOpenGLContextCreated()
{
    glFailed=false;
    bgShader  =compileProgram(kOscBgVS, kOscBgFS);
    lineShader=compileProgram(kOscLineVS, kOscLineFS, kOscLineGS);
    fillShader=compileProgram(kOscFillVS, kOscFillFS);
    if(bgShader==nullptr || lineShader==nullptr || fillShader==nullptr) return;

    GLint prevVao=0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGenVertexArrays(1, &vaoQuad);
    auto makeStream=[&](unsigned int& vao, unsigned int& vbo, juce::OpenGLShaderProgram& prog, const char* attr){
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);
        const GLint loc=glGetAttribLocation(prog.getProgramID(), attr);
        if(loc>=0){ glEnableVertexAttribArray((GLuint)loc); glVertexAttribPointer((GLuint)loc, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), nullptr); }
    };
    makeStream(vaoLines, vboLines, *lineShader, "lv");
    makeStream(vaoFill,  vboFill,  *fillShader, "fv");
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    bgUploaded=-1;   // texture du fond à renvoyer dans ce contexte
    onContextCreated();
}

void GpuOscilloscopeDisplay::openGLContextClosing()
{
    for(auto* b : { &vboLines, &vboFill }) if(*b){ glDeleteBuffers(1, b); *b=0; }
    for(auto* a : { &vaoQuad, &vaoLines, &vaoFill }) if(*a){ glDeleteVertexArrays(1, a); *a=0; }
    if(bgTex){ glDeleteTextures(1, &bgTex); bgTex=0; }
    bgShader.reset(); lineShader.reset(); fillShader.reset();
}

void GpuOscilloscopeDisplay::uploadBackground()
{
    const int v=bgVersion.load();
    if(v==bgUploaded && bgTex!=0) return;
    juce::Image img;
    { const juce::SpinLock::ScopedLockType sl(bgLock); img=bgImage; }
    if(!img.isValid()) return;
    bgUploaded=v;
    const int w=img.getWidth(), h=img.getHeight();
    std::vector<juce::uint8> px((size_t)w*(size_t)h*4);
    {
        const juce::Image::BitmapData bd(img, juce::Image::BitmapData::readOnly);
        for(int y=0;y<h;++y) std::memcpy(px.data()+(size_t)y*(size_t)w*4, bd.getLinePointer(y), (size_t)w*4);   // BGRA prémultiplié
    }
    if(bgTex==0) glGenTextures(1, &bgTex);
    glBindTexture(GL_TEXTURE_2D, bgTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    bgTexW=w; bgTexH=h;
}

// rémanence : l'image affichée passe dans l'historique
void GpuOscilloscopeDisplay::pushHistory()
{
    if(curN<=0 || curN!=histN) return;
    std::memcpy(histL[histHead].data(), bufL.data(), (size_t)curN*sizeof(float));
    std::memcpy(histR[histHead].data(), bufR.data(), (size_t)curN*sizeof(float));
    histHead=(histHead+1)%kHist;
    histCount=juce::jmin(histCount+1, kHist);
}

// Nouvelle fenêtre : calée sur la phase de la fondamentale (période par
// autocorrélation du signal filtré à ~200 Hz), fondu entre les deux dernières
// périodes : image stable qui évolue en continu, au plus près du direct.
bool GpuOscilloscopeDisplay::capture()
{
    const int  n=nPts.load();
    const bool mono=monoA.load();
    if(n!=histN){ histN=n; histCount=0; }
    const double sr=juce::jmax(1.0, processor.getCurrentSampleRate());

    auto finish=[&]{
        curN=n;
        for(int i=0;i<n;++i) bufM[(size_t)i]=0.5f*(bufL[(size_t)i]+bufR[(size_t)i]);
        // mesures de la voie 1 (L, ou L+R en mono) : crête à crête, fréquence
        const float* c1=mono ? bufM.data() : bufL.data();
        float lo=1e9f, hi=-1e9f; int first=-1, last=-1, crossings=0;
        for(int i=0;i<n;++i){
            lo=juce::jmin(lo, c1[i]); hi=juce::jmax(hi, c1[i]);
            if(i>0 && c1[i-1]<0.f && c1[i]>=0.f){ if(first<0) first=i; last=i; ++crossings; }
        }
        measVpp=hi-lo;
        measHz=(crossings>=2 && last>first) ? (float)((double)(crossings-1)*sr/(double)(last-first)) : 0.f;
    };

    if(!triggerMode.load()){
        pushHistory();
        processor.getScopeData(bufL.data(), bufR.data(), n);
        finish();
        return true;
    }

    const int search=juce::jlimit(256, LeakAudioProcessor::SCOPE_MAX_READ-n, (int)(sr*0.12));   // 120 ms : basses jusqu'à ~10 Hz
    const int fetch=n+search;
    const juce::uint64 endAbs=processor.getScopeData(rawL.data(), rawR.data(), fetch);
    const juce::uint64 baseAbs=endAbs-(juce::uint64)fetch;   // indice absolu de rawL[0]

    // Silence (sous -80 dBFS sur la fenêtre la plus récente) : ligne plate
    // tout de suite. Avant, l'image restait figée ~150 ms puis sautait.
    {
        float rawPeak=0.f;
        for(int i=search;i<fetch;++i) rawPeak=juce::jmax(rawPeak, std::abs(rawL[(size_t)i]), std::abs(rawR[(size_t)i]));
        if(rawPeak<1.0e-4f){
            pushHistory();
            std::memcpy(bufL.data(), rawL.data()+search, (size_t)n*sizeof(float));
            std::memcpy(bufR.data(), rawR.data()+search, (size_t)n*sizeof(float));
            period=0.0;
            finish();
            return true;
        }
    }

    const float a=1.f-std::exp(-juce::MathConstants<float>::twoPi*200.f/(float)sr);
    float y1=0.f, y2=0.f;
    for(int i=0;i<fetch;++i){
        const float x=mono ? 0.5f*(rawL[(size_t)i]+rawR[(size_t)i]) : rawL[(size_t)i];
        y1+=a*(x-y1); y2+=a*(y1-y2);
        trigSig[(size_t)i]=y2;
    }
    const int warm=juce::jmin(search/4, (int)(sr*0.01));   // mise en route du filtre ignorée

    auto trigSrc=[&](int i){ return mono ? 0.5f*(rawL[(size_t)i]+rawR[(size_t)i]) : rawL[(size_t)i]; };
    period=estimatePeriod(fetch, warm, sr);   // 0 = pas de hauteur nette
    const double maxStart=(double)(baseAbs+(juce::uint64)search);
    double startPos=maxStart;
    if(period>1.0){
        // Calage sur la PHASE DE LA FONDAMENTALE (corrélation complexe à 1/P
        // sur les 4 dernières périodes, fenêtre de Hann) : la fenêtre commence
        // au passage montant de la fondamentale, le plus récent. Les fronts du
        // signal (v1.38.1-1.39.1) glissaient d'un point de l'onde à un autre
        // quand le timbre évolue (note tenue désaccordée) -> sauts.
        const int W=juce::jmin((int)(4.0*period), fetch-warm);
        const int a0=fetch-W;
        const double w0=juce::MathConstants<double>::twoPi/period;
        const double ph0=std::fmod((double)(baseAbs+(juce::uint64)a0), period)*w0;   // phase absolue de rawL[a0]
        double re=0.0, im=0.0;
        double c=std::cos(ph0), s=std::sin(ph0);
        const double dc=std::cos(w0), ds=std::sin(w0);
        for(int i=0;i<W;++i){
            const double h=0.5-0.5*std::cos(juce::MathConstants<double>::twoPi*(double)i/(double)W);
            const double x=(double)trigSrc(a0+i)*h;
            re+=x*c; im-=x*s;
            const double cn=c*dc-s*ds; s=s*dc+c*ds; c=cn;   // rotation d'un échantillon
        }
        const double phi=std::atan2(im, re);
        const double i0=(-juce::MathConstants<double>::halfPi-phi)/w0;   // passage montant (modulo P)
        startPos=i0+std::floor((maxStart-i0)/period)*period;
    } else {
        // pas de hauteur nette : front le plus récent (montée à 60 % de la crête)
        float rp=0.f;
        for(int i=juce::jmax(0, fetch-n-(int)(sr*0.04));i<fetch;++i) rp=juce::jmax(rp, trigSrc(i));
        bool armed=false; int last=-1;
        float xPrev=trigSrc(0);
        for(int i=1;i<=search;++i){
            const float x=trigSrc(i);
            if(x<0.2f*rp) armed=true;
            else if(armed && xPrev<0.6f*rp && x>=0.6f*rp){ last=i; armed=false; }
            xPrev=x;
        }
        if(last>=0) startPos=(double)(baseAbs+(juce::uint64)last);
    }
    const int trigPt=juce::jlimit(0, search, (int)std::llround(startPos-(double)baseAbs));
    pushHistory();
    std::memcpy(bufL.data(), rawL.data()+trigPt, (size_t)n*sizeof(float));
    std::memcpy(bufR.data(), rawR.data()+trigPt, (size_t)n*sizeof(float));
    // Fondu entre les deux dernières périodes calées, au prorata du temps
    // écoulé : la forme évolue en continu au lieu d'avancer par bonds d'une
    // période (la note et l'écran n'ont pas le même rythme).
    if(period>1.0){
        const int p0=(int)std::llround(startPos-period-(double)baseAbs);
        const float f=(float)juce::jlimit(0.0, 1.0, (maxStart-startPos)/period);   // poids de la plus récente
        if(p0>=0 && p0<trigPt && f<1.f){
            const float g=1.f-f;
            for(int i=0;i<n;++i){
                bufL[(size_t)i]=f*bufL[(size_t)i]+g*rawL[(size_t)(p0+i)];
                bufR[(size_t)i]=f*bufR[(size_t)i]+g*rawR[(size_t)(p0+i)];
            }
        }
    }
    finish();
    return true;
}

// Période de la fondamentale en échantillons (0 = pas de hauteur nette) :
// autocorrélation normalisée (méthode de McLeod) sur le signal de
// déclenchement décimé à ~6 kHz (100 ms), puis affinée à pleine résolution.
double GpuOscilloscopeDisplay::estimatePeriod(int fetch, int warm, double sr)
{
    const int D=juce::jmax(1, (int)std::lround(sr/6000.0));
    const double dsr=sr/(double)D;
    const int L=juce::jmin(kAcMax, (int)(0.1*dsr), (fetch-warm)/D);
    const int tMin=juce::jmax(2, (int)(dsr/1000.0));   // 1 kHz max
    const int tMax=juce::jmin(L/2, (int)(dsr/20.0));    // 20 Hz min
    if(tMax<=tMin+2) return 0.0;
    for(int j=0;j<L;++j) acBuf[j]=trigSig[(size_t)(fetch-(L-j)*D)];
    for(int t=0;t<=tMax;++t){
        double acf=0.0, m=0.0;
        for(int j=0;j+t<L;++j){ acf+=(double)acBuf[j]*acBuf[j+t]; m+=(double)acBuf[j]*acBuf[j]+(double)acBuf[j+t]*acBuf[j+t]; }
        nsdfBuf[t]=m>0.0 ? (float)(2.0*acf/m) : 0.f;
    }
    // maximum de chaque lobe positif ; on garde le PREMIER à 90 % du plus haut
    // = la période de la note. Le plus haut tombait souvent sur 2 périodes
    // (v1.38.1-1.39.0) : l'image restait en place une image sur deux puis
    // sautait de deux périodes.
    int keys[64]; int nk=0; float top=0.f;
    int t=1;
    while(t<=tMax && nsdfBuf[t]>0.f) ++t;   // lobe de l'origine
    while(t<=tMax && nk<64){
        while(t<=tMax && nsdfBuf[t]<=0.f) ++t;
        int best=-1;
        while(t<=tMax && nsdfBuf[t]>0.f){ if(best<0 || nsdfBuf[t]>nsdfBuf[best]) best=t; ++t; }
        if(best>=tMin && best<tMax){ keys[nk++]=best; top=juce::jmax(top, nsdfBuf[best]); }
    }
    if(nk==0 || top<0.5f) return 0.0;
    int tau=keys[0];
    for(int k=0;k<nk;++k) if(nsdfBuf[keys[k]]>=0.9f*top){ tau=keys[k]; break; }

    // affinage à pleine résolution autour de tau·D (précision < 1 échantillon)
    const int W=juce::jmin((int)(0.06*sr), fetch-warm-(tau+1)*D-1);
    if(W<64) return (double)(tau*D);
    auto nsdfAt=[&](int lag){
        double acf=0.0, m=0.0;
        const int s0=fetch-W-lag;
        for(int i=0;i<W;++i){
            const double x=trigSig[(size_t)(s0+i)], y=trigSig[(size_t)(s0+i+lag)];
            acf+=x*y; m+=x*x+y*y;
        }
        return m>0.0 ? 2.0*acf/m : 0.0;
    };
    int bestLag=tau*D; double bv=-2.0;
    for(int lag=juce::jmax(2, tau*D-D); lag<=tau*D+D; ++lag){ const double v=nsdfAt(lag); if(v>bv){ bv=v; bestLag=lag; } }
    const double a=nsdfAt(bestLag-1), c=nsdfAt(bestLag+1), den=a-2.0*bv+c;
    return (double)bestLag + (den<0.0 ? juce::jlimit(-0.5, 0.5, 0.5*(a-c)/den) : 0.0);
}

void GpuOscilloscopeDisplay::renderOpenGL()
{
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    if(bgShader==nullptr || lineShader==nullptr || fillShader==nullptr || vaoQuad==0) return;
    applyVsync();

    const float scale=renderingScale();
    const int w=viewW.load(), h=viewH.load();
    glViewport(0, 0, juce::jmax(1, juce::roundToInt(scale*(float)w)), juce::jmax(1, juce::roundToInt(scale*(float)h)));

    GLint prevVao=0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glDisable(GL_DEPTH_TEST);

    // 1) fond (dégradé, graticule, légendes)
    uploadBackground();
    if(bgTex!=0){
        glDisable(GL_BLEND);
        bgShader->use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, bgTex);
        bgShader->setUniform("tex", 0);
        glBindVertexArray(vaoQuad);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // 2) nouvelle fenêtre
    capture();
    const int n=curN;
    if(n>=2){
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        const int  style=styleA.load();
        const bool mono=monoA.load();
        const auto pr=plotAreaFor(style, (float)w, (float)h);
        const float gain=ampGain.load();

        // trace -> sommets (px logiques). Tous les échantillons sont reliés à
        // leur abscisse exacte (sous-pixel) tant qu'il y en a moins de 12 par
        // pixel réel : l'enveloppe min / max par colonne, utilisée dès 2 par
        // pixel, découpait la courbe en marches (aliasing visible à 25 ms).
        // Au-delà (très longues fenêtres) : min / max par colonne PHYSIQUE.
        // Tracé « classique » (Options) : l'enveloppe d'avant la v1.66.3, dès
        // 2 échantillons par pixel logique — trait plus net, moins de détail.
        const bool classic=classicTraceA.load();
        const float widthMul=widthMulA.load();
        auto build=[&](const float* buf, juce::Rectangle<float> r, bool area){
            verts.clear();
            const float cy=r.getCentreY(), amp=r.getHeight()*0.46f;
            auto add=[&](float x, float y){ verts.push_back(x); verts.push_back(y); if(area){ verts.push_back(x); verts.push_back(cy); } };
            const int cols=juce::jmax(1, classic ? (int)r.getWidth() : (int)(r.getWidth()*scale));
            if(n>cols*(classic ? 2 : 12)){
                float prevY=cy;
                for(int c=0;c<cols;++c){
                    const int i0=c*n/cols, i1=juce::jmax(i0+1, (c+1)*n/cols);
                    float mn=1.f, mx=-1.f;
                    for(int i=i0;i<i1 && i<n;++i){ const float v=juce::jlimit(-1.f, 1.f, buf[i]*gain); mn=juce::jmin(mn, v); mx=juce::jmax(mx, v); }
                    const float x=r.getX()+((float)c+0.5f)/(float)cols*r.getWidth();
                    const float yHi=cy-mx*amp, yLo=cy-mn*amp;
                    const bool hiFirst=std::abs(yHi-prevY) < std::abs(yLo-prevY);   // trait continu
                    add(x, hiFirst ? yHi : yLo);
                    add(x, hiFirst ? yLo : yHi);
                    prevY=hiFirst ? yLo : yHi;
                }
            } else {
                for(int i=0;i<n;++i) add(r.getX()+(float)i/(float)(n-1)*r.getWidth(), cy-juce::jlimit(-1.f, 1.f, buf[i]*gain)*amp);
            }
            return (int)(verts.size()/2);
        };
        auto colourUniform=[](juce::OpenGLShaderProgram& prog, juce::Colour c, float alpha){
            const float a=c.getFloatAlpha()*alpha;
            prog.setUniform("col", c.getFloatRed()*a, c.getFloatGreen()*a, c.getFloatBlue()*a, a);
        };
        auto line=[&](const float* buf, juce::Rectangle<float> r, juce::Colour c, float alpha, float width, float halo){
            width*=widthMul;   // Options : épaisseur du trait
            const int nv=build(buf, r, false);
            if(nv<2) return;
            lineShader->use();
            lineShader->setUniform("view", (float)w, (float)h);
            glBindVertexArray(vaoLines);
            glBindBuffer(GL_ARRAY_BUFFER, vboLines);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size()*sizeof(float)), verts.data(), GL_STREAM_DRAW);
            if(halo>0.f){   // halo serré (tube, vintage)
                lineShader->setUniform("halfW", width*0.5f+2.4f);
                lineShader->setUniform("sharp", 1.3f);
                colourUniform(*lineShader, c, 0.22f*halo*alpha);
                glDrawArrays(GL_LINE_STRIP, 0, nv);
            }
            lineShader->setUniform("halfW", width*0.5f+0.9f);
            lineShader->setUniform("sharp", 3.f);
            colourUniform(*lineShader, c, alpha);
            glDrawArrays(GL_LINE_STRIP, 0, nv);
        };
        auto fill=[&](const float* buf, juce::Rectangle<float> r, juce::Colour c, float alpha){
            const int nv=build(buf, r, true);
            if(nv<4) return;
            fillShader->use();
            fillShader->setUniform("view", (float)w, (float)h);
            colourUniform(*fillShader, c, alpha);
            glBindVertexArray(vaoFill);
            glBindBuffer(GL_ARRAY_BUFFER, vboFill);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size()*sizeof(float)), verts.data(), GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, nv);
        };
        // une image (courante : halo, aire ; rémanence : plus pâle, sans halo ni aire)
        auto frame=[&](const float* L, const float* R, const float* M, float alpha, bool current){
            const float halo=current ? 1.f : 0.f;
            if(style==0){
                if(!mono) line(R, pr, juce::Colour(0xFF4AA3FF), alpha, 1.4f, halo);
                line(M, pr, juce::Colour(0xFF8FF6FF), alpha, 1.6f, halo);
            } else if(style==1){
                line(M, pr, juce::Colour(0xFF00E5FF), alpha, 1.5f, 0.f);
                if(!mono) line(R, pr, juce::Colour(0xFFFF4FD8), alpha, 1.3f, 0.f);
            } else if(style==2){
                if(mono) line(M, pr, juce::Colour(0xFF7DFFD8), alpha, 1.7f, 1.3f*halo);
                else {
                    line(L, upperHalf(pr), juce::Colour(0xFF7DFFD8), alpha, 1.6f, 1.2f*halo);
                    line(R, lowerHalf(pr), juce::Colour(0xFF5FD3FF), alpha, 1.6f, 1.2f*halo);
                }
            } else {
                auto lane=[&](const float* buf, juce::Rectangle<float> r, juce::Colour c){
                    if(current) fill(buf, r, c, 0.13f*alpha);
                    line(buf, r, c, alpha, 1.5f, 0.f);
                };
                if(mono) lane(M, pr, juce::Colour(0xFF22E0FF));
                else { lane(L, upperLane(pr), juce::Colour(0xFF22E0FF)); lane(R, lowerLane(pr), juce::Colour(0xFF3D8BFF)); }
            }
        };

        // rémanence (écran à phosphore) : les deux images précédentes, de
        // plus en plus pâles, sous l'image courante
        if(persistA.load() && histN==n){
            for(int age=histCount; age>=1; --age){
                const int idx=(histHead-age+kHist*2)%kHist;
                if(mono) for(int i=0;i<n;++i) tmpM[(size_t)i]=0.5f*(histL[idx][(size_t)i]+histR[idx][(size_t)i]);
                frame(histL[idx].data(), histR[idx].data(), mono ? tmpM.data() : histL[idx].data(), age==1 ? 0.32f : 0.14f, false);
            }
        }
        frame(bufL.data(), bufR.data(), mono ? bufM.data() : bufL.data(), 1.f, true);
    }

    // état rendu à JUCE (composition du calque 2D)
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(0);
}
