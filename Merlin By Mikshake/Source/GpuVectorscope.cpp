#include "GpuVectorscope.h"
#include "PluginEditor.h"   // Col::, Ui::

using namespace juce::gl;

//==============================================================================
// Shaders (GLSL 1.50) — commentaires en ASCII uniquement
//==============================================================================
// Quad plein écran sans tampon (gl_VertexID) ; vUv (0,0) en bas à gauche
static const char* kFullVS = R"(#version 150
out vec2 vUv;
void main()
{
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vUv = c;
    gl_Position = vec4(c*2.0-1.0, 0.0, 1.0);
}
)";

// Rémanence : dst *= k (blend GL_ZERO, GL_SRC_COLOR)
static const char* kFadeFS = R"(#version 150
uniform float k;
out vec4 fragColour;
void main() { fragColour = vec4(k); }
)";

// Trace du faisceau : chaque segment devient un ruban (geometry shader),
// d'autant plus lumineux que le faisceau est lent (segment court)
static const char* kLineVS = R"(#version 150
in vec3 lv;                   // x, y (px logiques depuis le haut), intensite
uniform vec2 view;
out float gI;
void main()
{
    gl_Position = vec4(lv.x/view.x*2.0-1.0, 1.0-lv.y/view.y*2.0, 0.0, 1.0);
    gI = lv.z;
}
)";

static const char* kLineGS = R"(#version 150
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;
in float gI[];
uniform vec2  view;
uniform float halfW;
out float vI;
out float vSide;
void main()
{
    vec2 ap = (gl_in[0].gl_Position.xy*0.5+0.5)*view;
    vec2 bp = (gl_in[1].gl_Position.xy*0.5+0.5)*view;
    vec2 d = bp-ap;
    float len = length(d);
    vec2 dir = len > 1e-4 ? d/len : vec2(1.0, 0.0);
    vec2 n = vec2(-dir.y, dir.x)*halfW;
    vec2 e = dir*halfW*0.7;
    float I = min(gI[0], gI[1])*clamp(1.6/(len+0.8), 0.04, 1.0);
    vec2  q[4] = vec2[4](ap-e+n, ap-e-n, bp+e+n, bp+e-n);
    float s[4] = float[4](1.0, -1.0, 1.0, -1.0);
    for(int k = 0; k < 4; ++k){
        gl_Position = vec4(q[k]/view*2.0-1.0, 0.0, 1.0);
        vI = I; vSide = s[k];
        EmitVertex();
    }
    EndPrimitive();
}
)";

static const char* kLineFS = R"(#version 150
in float vI;
in float vSide;
out vec4 fragColour;
void main() { fragColour = vec4(vI*exp(-vSide*vSide*2.5)); }
)";

// Composition du phosphore : halo (anneaux de prises) + coeur blanc
static const char* kPhosphorFS = R"(#version 150
in vec2 vUv;
out vec4 fragColour;
uniform sampler2D acc;
uniform vec2 texel;
uniform vec3 phos;
void main()
{
    float v = texture(acc, vUv).r;
    float bloom = 0.0;
    for(int i = 0; i < 12; ++i){
        float a = 6.2831853*float(i)/12.0;
        vec2  d = vec2(cos(a), sin(a));
        bloom += texture(acc, vUv+d*texel*3.0).r*0.6 + texture(acc, vUv+d*texel*7.0).r*0.4;
    }
    bloom /= 12.0;
    vec3 c = phos*(1.0-exp(-v*1.4))
           + phos*(1.0-exp(-bloom*0.9))*0.45
           + vec3(1.0)*(1.0-exp(-max(v-0.8, 0.0)*0.8))*0.75;
    fragColour = vec4(c, 1.0);
}
)";

// Carte de densité : quad sur le carré du scope (vUv.y = 0 en haut)
static const char* kBoxVS = R"(#version 150
uniform vec2 view;
uniform vec4 box;             // x, y (haut), largeur, hauteur en px
out vec2 vUv;
void main()
{
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vUv = c;
    vec2 p = box.xy + c*box.zw;
    gl_Position = vec4(p.x/view.x*2.0-1.0, 1.0-p.y/view.y*2.0, 0.0, 1.0);
}
)";

static const char* kDensityFS = R"(#version 150
in vec2 vUv;
out vec4 fragColour;
uniform sampler2D dens;
uniform float invMax;
uniform float texel;
const vec3 st[8] = vec3[8](vec3(0.,0.,4.)/255.,     vec3(40.,11.,84.)/255.,  vec3(101.,21.,110.)/255.,
                           vec3(159.,42.,99.)/255., vec3(212.,72.,66.)/255., vec3(245.,125.,21.)/255.,
                           vec3(250.,193.,39.)/255.,vec3(252.,255.,164.)/255.);
vec3 inferno(float t)
{
    t = clamp(t, 0.0, 1.0)*7.0;
    int i = min(6, int(t));
    return mix(st[i], st[i+1], t-float(i));
}
void main()
{
    vec2 d = vUv*2.0-1.0;
    if(dot(d, d) > 1.02) discard;
    float s = 0.0, ws = 0.0;
    for(int y = -2; y <= 2; ++y)
        for(int x = -2; x <= 2; ++x){
            float w = exp(-float(x*x+y*y)/3.0);
            s  += w*texture(dens, vUv+vec2(float(x), float(y))*texel).r;
            ws += w;
        }
    float v = pow(clamp(s/ws*invMax, 0.0, 1.0), 0.35);
    fragColour = vec4(inferno(0.12+0.88*v)*smoothstep(0.01, 0.08, v), 1.0);
}
)";

//==============================================================================
GpuVectorscopeDisplay::GpuVectorscopeDisplay(LeakAudioProcessor& p, Style s)
    : GpuModuleBase(p), style(s)
{
    pts.resize(LeakAudioProcessor::VECTOR_SIZE);
    lineVerts.reserve(LeakAudioProcessor::VECTOR_SIZE*3+3);
    density.assign((size_t)NG*NG, 0.f);
}

GpuVectorscopeDisplay::~GpuVectorscopeDisplay()
{
    detachGL();
}

const char* GpuVectorscopeDisplay::title() const
{
    return style==Style::Phosphor ? "VECTORSCOPE - PHOSPHOR" : "VECTORSCOPE - DENSITY";
}

void GpuVectorscopeDisplay::resized()
{
    const int W=getWidth(), H=getHeight();
    viewW=juce::jmax(1, W);
    viewH=juce::jmax(1, H);
    // exactement la géométrie du Vectorscope d'origine : cercle centré dans
    // le module, rayon 0.86 x la demi-dimension la plus petite
    const float cx=(float)W*0.5f, cy=(float)H*0.5f;
    scopeCx=cx;
    scopeCy=cy;
    scopeR=juce::jmax(4.f, juce::jmin(cx, cy)*0.86f);
}

//==============================================================================
// Calque 2D : graticule, repères, titre, gain automatique, corrélation
//==============================================================================
void GpuVectorscopeDisplay::paint(juce::Graphics& g)
{
    const int W=getWidth(), H=getHeight();
    if(W<40||H<40) return;
    const bool gl=isGlAttached();
    if(!gl) g.fillAll(Col::bg);

    const float cx=scopeCx.load(), cy=scopeCy.load(), r=scopeR.load();
    const auto grid=juce::Colour(0xFF78C8BE);

    // graticule : cercles plein / moitié, axes M et S, diagonales L / R
    // anneau extérieur blanc, comme l'original : même cercle de référence
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.drawEllipse(cx-r, cy-r, 2.f*r, 2.f*r, 1.1f);
    g.setColour(grid.withAlpha(0.16f));
    g.drawEllipse(cx-r*0.5f, cy-r*0.5f, r, r, 1.f);
    g.drawLine(cx, cy-r, cx, cy+r, 1.f);
    g.drawLine(cx-r, cy, cx+r, cy, 1.f);
    {
        const float dashes[] = { 3.f, 4.f };
        const float k=r*0.7071f;
        g.drawDashedLine({ cx-k, cy-k, cx+k, cy+k }, dashes, 2, 1.f);
        g.drawDashedLine({ cx+k, cy-k, cx-k, cy+k }, dashes, 2, 1.f);
    }
    // repères aux mêmes places que le Vectorscope d'origine
    g.setFont(Ui::iceland(9.5f));
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    // "M" au-dessus du cercle, sauf s'il tombe sur le titre (gros texte,
    // module large et bas) : il passe alors juste sous le haut de l'anneau
    {
        const float titleW=juce::GlyphArrangement::getStringWidth(Ui::iceland(10.5f).withExtraKerningFactor(0.08f), TR(title()));
        const float titleRight=6.f+juce::jmin(titleW, (float)W-60.f);
        const bool mInside=cy-r-13.f < 5.f+13.f*Ui::textScale && (cx-8.f < titleRight+4.f || Ui::textScale>1.f);
        g.drawText("M",(int)(cx-8),(int)(mInside ? cy-r+3.f : cy-r-13.f),16,11,juce::Justification::centred);
    }
    g.drawText("R",(int)(cx+r*0.707f-2),(int)(cy-r*0.707f-13),14,11,juce::Justification::centred);
    g.drawText("L",(int)(cx-r*0.707f-12),(int)(cy-r*0.707f-13),14,11,juce::Justification::centred);

    g.setFont(Ui::iceland(10.5f).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    // gain automatique appliqué à l'affichage, à droite ; le titre prend le reste
    const juce::String gainTxt="+"+juce::String(juce::roundToInt(gainDbUi.load()))+" dB";
    const auto gainFont=Ui::iceland(10.f);
    const int gainW=juce::roundToInt(juce::GlyphArrangement::getStringWidth(gainFont, gainTxt))+4;
    g.drawFittedText(TR(title()), 6, 5, W-gainW-18, 13, juce::Justification::left, 1, 0.6f);
    g.setFont(gainFont);
    g.setColour(Col::textMid.withAlpha(0.8f));
    g.drawText(gainTxt, W-gainW-6, 5, gainW, 13, juce::Justification::right, false);

    // barre de corrélation (-1 .. +1), à la place et à la largeur de la jauge
    // du Vectorscope d'origine (sous le cercle, 1.6 x le rayon)
    {
        const float co=juce::jlimit(-1.f, 1.f, corrUi.load());
        const float mh=3.f, y=(float)H-mh*0.5f-8.f;
        const float x0=cx-r*0.80f, x1=cx+r*0.80f, mid=cx;
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillRoundedRectangle(x0, y-mh*0.5f, x1-x0, mh, 1.5f);
        const float v=mid+co*(x1-x0)*0.5f;
        g.setColour(co<0.f ? juce::Colour(0xFFFF5A4A) : Col::accent);
        g.fillRoundedRectangle(juce::jmin(mid, v), y-mh*0.5f, std::abs(v-mid), mh, 1.5f);
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.fillRect(mid-0.5f, y-3.5f, 1.f, 7.f);
    }

    paintGlFailure(g);
    paintScreenFrame(g);
}

//==============================================================================
// Thread GL
//==============================================================================
void GpuVectorscopeDisplay::newOpenGLContextCreated()
{
    glFailed=false;
    if(style==Style::Phosphor){
        fadeShader    =compileProgram(kFullVS, kFadeFS);
        lineShader    =compileProgram(kLineVS, kLineFS, kLineGS);
        phosphorShader=compileProgram(kFullVS, kPhosphorFS);
        if(fadeShader==nullptr || lineShader==nullptr || phosphorShader==nullptr) return;
    } else {
        densityShader =compileProgram(kBoxVS, kDensityFS);
        if(densityShader==nullptr) return;
    }

    GLint prevVao=0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGenVertexArrays(1, &vaoQuad);
    if(style==Style::Phosphor){
        glGenVertexArrays(1, &vaoLines);
        glBindVertexArray(vaoLines);
        glGenBuffers(1, &vboLines);
        glBindBuffer(GL_ARRAY_BUFFER, vboLines);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);
        const GLint lv=glGetAttribLocation(lineShader->getProgramID(), "lv");
        if(lv>=0){ glEnableVertexAttribArray((GLuint)lv); glVertexAttribPointer((GLuint)lv, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), nullptr); }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    } else {
        glGenTextures(1, &densityTex);
        glBindTexture(GL_TEXTURE_2D, densityTex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, NG, NG, 0, GL_RED, GL_FLOAT, density.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glBindVertexArray((GLuint)prevVao);

    accumW=accumH=0;
    haveLast=false;
    lastFrameMs=juce::Time::getMillisecondCounterHiRes();
    onContextCreated();
}

void GpuVectorscopeDisplay::openGLContextClosing()
{
    if(vboLines){ glDeleteBuffers(1, &vboLines); vboLines=0; }
    for(auto* a : { &vaoQuad, &vaoLines }) if(*a){ glDeleteVertexArrays(1, a); *a=0; }
    for(auto* t : { &accumTex, &densityTex }) if(*t){ glDeleteTextures(1, t); *t=0; }
    if(accumFbo){ glDeleteFramebuffers(1, &accumFbo); accumFbo=0; }
    accumW=accumH=0;
    fadeShader.reset(); lineShader.reset(); phosphorShader.reset(); densityShader.reset();
}

// Texture de rémanence (R16F) à la taille physique de la vue
bool GpuVectorscopeDisplay::ensureAccumTarget(int pw, int ph)
{
    if(accumFbo!=0 && pw==accumW && ph==accumH) return true;
    if(accumTex==0) glGenTextures(1, &accumTex);
    glBindTexture(GL_TEXTURE_2D, accumTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, pw, ph, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    if(accumFbo==0) glGenFramebuffers(1, &accumFbo);
    GLint prevFbo=0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, accumFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accumTex, 0);
    const bool ok=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    if(ok){ glClearColor(0.f, 0.f, 0.f, 0.f); glClear(GL_COLOR_BUFFER_BIT); }
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    accumW=pw; accumH=ph;
    return ok;
}

// Nouveaux points : gain automatique (enveloppe de crête, montée immédiate,
// retombée lente), corrélation glissante, puis trace (Phosphor) ou grille
// de densité (Density).
void GpuVectorscopeDisplay::readPoints(float dt)
{
    numPts=processor.readNewVectorData(cursor, pts.data(), (int)pts.size());

    float peak=0.f;
    const double cd=std::exp(-(double)dt/0.3);
    sLR*=cd; sLL*=cd; sRR*=cd;
    for(int i=0;i<numPts;++i){
        const float m=pts[(size_t)i].m, s=pts[(size_t)i].s;
        peak=juce::jmax(peak, std::sqrt(m*m+s*s));
        const double L=m+s, R=m-s;
        sLR+=L*R; sLL+=L*L; sRR+=R*R;
    }
    env=juce::jmax(1e-4f, juce::jmax(peak, env*std::exp(-dt/1.2f)));
    const float target=juce::jlimit(1.f, 16.f, 0.85f/env);
    gain+=(target-gain)*(1.f-std::exp(-dt/(target<gain ? 0.05f : 0.4f)));
    gainDbUi=20.f*std::log10(gain);
    if(sLL>1e-9 && sRR>1e-9) corrUi=(float)(sLR/std::sqrt(sLL*sRR));

    const float cx=scopeCx.load(), cy=scopeCy.load(), r=scopeR.load();
    auto toScope=[&](float m, float s, float& x, float& y){
        m*=gain; s*=gain;
        const float n=std::sqrt(m*m+s*s);
        if(n>1.f){ m/=n; s/=n; }
        x=cx-s*r; y=cy-m*r;
    };

    if(style==Style::Phosphor){
        lineVerts.clear();
        float x, y;
        if(haveLast && numPts>0){ toScope(lastM, lastS, x, y); lineVerts.insert(lineVerts.end(), { x, y, 1.f }); }
        for(int i=0;i<numPts;++i){
            const float m=pts[(size_t)i].m, s=pts[(size_t)i].s;
            toScope(m, s, x, y);
            const bool live=std::abs(m)>1e-5f || std::abs(s)>1e-5f;   // le silence ne grave pas le centre
            lineVerts.insert(lineVerts.end(), { x, y, live ? 1.f : 0.f });
        }
        if(numPts>0){ lastM=pts[(size_t)numPts-1].m; lastS=pts[(size_t)numPts-1].s; haveLast=true; }
    } else {
        const float k=std::exp(-dt/0.35f);
        for(auto& v : density) v*=k;
        for(int i=0;i<numPts;++i){
            const float m=pts[(size_t)i].m, s=pts[(size_t)i].s;
            if(std::abs(m)<1e-5f && std::abs(s)<1e-5f) continue;
            float x, y; toScope(m, s, x, y);
            // position dans la grille (0..NG-1), dépôt bilinéaire
            const float gx=((x-(cx-r))/(2.f*r))*(float)(NG-1), gy=((y-(cy-r))/(2.f*r))*(float)(NG-1);
            const int ix=(int)std::floor(gx), iy=(int)std::floor(gy);
            const float fx=gx-(float)ix, fy=gy-(float)iy;
            auto add=[&](int xx, int yy, float w){ if(xx>=0 && yy>=0 && xx<NG && yy<NG) density[(size_t)(yy*NG+xx)]+=w; };
            add(ix, iy, (1.f-fx)*(1.f-fy)); add(ix+1, iy, fx*(1.f-fy));
            add(ix, iy+1, (1.f-fx)*fy);     add(ix+1, iy+1, fx*fy);
        }
        float mx=0.f;
        for(auto v : density) mx=juce::jmax(mx, v);
        densityMax=juce::jmax(1e-3f, juce::jmax(mx, densityMax*std::exp(-dt/0.8f)));
    }
}

void GpuVectorscopeDisplay::renderOpenGL()
{
    const auto bg=Col::bg;
    glClearColor(bg.getFloatRed(), bg.getFloatGreen(), bg.getFloatBlue(), 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    if(vaoQuad==0) return;
    if(style==Style::Phosphor ? (phosphorShader==nullptr || lineShader==nullptr || fadeShader==nullptr) : densityShader==nullptr) return;
    applyVsync();

    const double now=juce::Time::getMillisecondCounterHiRes();
    const float dt=(float)juce::jlimit(0.0, 0.1, (now-lastFrameMs)*0.001);
    lastFrameMs=now;
    readPoints(dt);

    const float scale=renderingScale();
    const int w=viewW.load(), h=viewH.load();
    const int pw=juce::jmax(1, juce::roundToInt(scale*(float)w)), ph=juce::jmax(1, juce::roundToInt(scale*(float)h));

    GLint prevVao=0, prevFbo=0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);

    if(style==Style::Phosphor){
        if(ensureAccumTarget(pw, ph)){
            // 1) rémanence : la texture s'estompe, puis la trace s'y ajoute
            glBindFramebuffer(GL_FRAMEBUFFER, accumFbo);
            glViewport(0, 0, pw, ph);
            fadeShader->use();
            fadeShader->setUniform("k", std::exp(-dt/0.09f));
            glBlendFunc(GL_ZERO, GL_SRC_COLOR);
            glBindVertexArray(vaoQuad);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            const int nv=(int)(lineVerts.size()/3);
            if(nv>=2){
                lineShader->use();
                lineShader->setUniform("view", (float)w, (float)h);
                lineShader->setUniform("halfW", 1.4f);
                glBlendFunc(GL_ONE, GL_ONE);
                glBindVertexArray(vaoLines);
                glBindBuffer(GL_ARRAY_BUFFER, vboLines);
                glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(lineVerts.size()*sizeof(float)), lineVerts.data(), GL_STREAM_DRAW);
                glDrawArrays(GL_LINE_STRIP, 0, nv);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);

            // 2) composition : phosphore + halo + coeur
            glViewport(0, 0, pw, ph);
            phosphorShader->use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, accumTex);
            phosphorShader->setUniform("acc", 0);
            phosphorShader->setUniform("texel", 1.f/(float)pw, 1.f/(float)ph);
            const auto acc=Col::accent;
            phosphorShader->setUniform("phos", acc.getFloatRed(), acc.getFloatGreen(), acc.getFloatBlue());
            glBlendFunc(GL_ONE, GL_ONE);
            glBindVertexArray(vaoQuad);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    } else {
        glViewport(0, 0, pw, ph);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, densityTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, NG, NG, GL_RED, GL_FLOAT, density.data());
        const float cx=scopeCx.load(), cy=scopeCy.load(), r=scopeR.load();
        densityShader->use();
        densityShader->setUniform("dens", 0);
        densityShader->setUniform("view", (float)w, (float)h);
        densityShader->setUniform("box", cx-r, cy-r, 2.f*r, 2.f*r);
        densityShader->setUniform("invMax", 1.f/densityMax);
        densityShader->setUniform("texel", 1.f/(float)NG);
        glBlendFunc(GL_ONE, GL_ONE);
        glBindVertexArray(vaoQuad);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(0);
}
