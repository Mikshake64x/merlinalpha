#include "GpuSpectroFx.h"
#include "PluginEditor.h"   // Col::, Ui::

using namespace juce::gl;

//==============================================================================
// Shaders (GLSL 150, profil core 3.2). Les réglages (tailles, couleurs,
// caméras, lueur) reprennent ceux des aperçus WebGL validés.
//==============================================================================
namespace
{
    // historique commun : anneau de lignes, la plus récente en headRow
    const char* kHistGlsl = R"(
uniform sampler2D hist;
uniform float headRow, rowsTex, frac, span, time, pxScale;
uniform mat4 mvp;
float lvl(float u, float age){
    float v=(mod(headRow-age, rowsTex)+0.5)/rowsTex;
    return texture(hist, vec2(clamp(u,0.0,1.0)*(159.0/160.0)+0.5/160.0, v)).r;
}
)";

    // palette des barres (stops sRGB des aperçus, mélangés en linéaire)
    const char* kPaletteGlsl = R"(
vec3 lin(vec3 c){ return pow(c, vec3(2.2)); }
vec3 pal(float u){
    vec3 c0=lin(vec3(32.0,216.0,200.0)/255.0), c1=lin(vec3(90.0,64.0,255.0)/255.0), c2=lin(vec3(195.0,60.0,255.0)/255.0),
         c3=lin(vec3(255.0,74.0,154.0)/255.0), c4=lin(vec3(255.0,154.0,60.0)/255.0), c5=lin(vec3(138.0,60.0,255.0)/255.0),
         c6=lin(vec3(34.0,208.0,224.0)/255.0);
    if(u<0.2)  return mix(c0,c1,u/0.2);
    if(u<0.4)  return mix(c1,c2,(u-0.2)/0.2);
    if(u<0.55) return mix(c2,c3,(u-0.4)/0.15);
    if(u<0.68) return mix(c3,c4,(u-0.55)/0.13);
    if(u<0.84) return mix(c4,c5,(u-0.68)/0.16);
    return mix(c5,c6,(u-0.84)/0.16);
}
)";

    // ── 1 · barres 3D (une instance par barre, 4 passes : barre, reflet, sommet, sol) ──
    const char* kBarsVS = R"(
in vec3 pos; in vec3 nrm;
uniform sampler2D bands;
uniform float nBands, part, barDZ;
out vec3 vCol; out vec3 vN; out vec3 vW; out float vFog;
void main(){
    const float W=17.0; float DZ=barDZ;   // profondeur d'une ligne : selon TIME
    vec3 wp; vec3 col=vec3(0.0);
    vN=nrm;
    if(part>2.5){ wp=vec3(pos.x*80.0, -0.002, pos.z*80.0); }
    else {
        float id=float(gl_InstanceID);
        float k=mod(id, nBands), r=floor(id/nBands);
        float v=(mod(headRow-r, rowsTex)+0.5)/rowsTex;
        float lv=texture(bands, vec2((k+0.5)/62.0, v)).r;
        float sx=W/nBands, h=0.05+pow(lv,1.35)*5.4;
        vec3 o=vec3(-W*0.5+(k+0.5)*sx, 0.0, 2.0-(span-r-frac)*DZ);   // la plus récente au fond
        vec3 s=vec3(sx*0.76, h, DZ*0.76);
        if(part>0.5 && part<1.5) s.y=-h*0.8;
        if(part>1.5){ s=vec3(sx*0.78, 0.05, DZ*0.78); o.y=h; }
        wp=o+pos*s;
        vec3 base=pal(k/max(1.0,nBands-1.0));
        col= part>1.5 ? mix(base, vec3(1.0), smoothstep(0.45,0.9,lv))*(0.35+1.6*lv*lv) : base*(0.28+0.72*lv);
    }
    vCol=col; vW=wp;
    vec4 clip=mvp*vec4(wp,1.0); gl_Position=clip;
    vFog=clamp((clip.w-30.0)/20.0, 0.0, 1.0);   // brouillard lointain seulement : tout l'historique reste lisible
}
)";
    const char* kBarsFS = R"(
in vec3 vCol; in vec3 vN; in vec3 vW; in float vFog;
uniform float part; uniform vec3 eye, fogCol;
out vec4 o;
void main(){
    vec3 c=vCol; float a=1.0;
    if(part<0.5){
        vec3 n=normalize(vN), V=normalize(eye-vW), Ld=normalize(vec3(-6.0,12.0,9.0));
        vec3 pl=vec3(5.0,6.0,3.0)-vW; float dl=length(pl); vec3 Lp=pl/dl;
        vec3 amb=pow(vec3(0.604,0.541,1.0),vec3(2.2))*0.6;
        vec3 pc=pow(vec3(1.0,0.31,0.847),vec3(2.2))*40.0/(dl*dl)*pow(clamp(1.0-dl/40.0,0.0,1.0),2.0);
        vec3 irr=amb+vec3(1.7)*max(dot(n,Ld),0.0)+pc*max(dot(n,Lp),0.0);
        vec3 H=normalize(Ld+V);
        c=vCol*irr*(0.75/3.14159)+vec3(pow(max(dot(n,H),0.0),40.0)*0.3);
    } else if(part<1.5) a=0.17;
    else if(part>2.5){ c=fogCol; a=0.45; }
    o=vec4(mix(c, fogCol, vFog), a);
}
)";

    // ── 2 · cyberpunk LED (points carrés, en miroir) ──
    const char* kLedVS = R"(
in vec4 g;
out vec3 gC; out float gA; out float gB; out float gS;
void main(){
    const float L=28.0, H=4.4, W=16.0, DZ=0.7;
    float u=g.x, age=g.y, k=g.z, sg=g.w;
    float lv=lvl(u,age), h=(0.07+pow(lv,2.4))*H*1.25, y=(k+0.5)/L*H;
    float on=step(y,h);
    vec4 clip=mvp*vec4((u-0.5)*W, sg*y*0.92, -(span-age-frac)*DZ, 1.0);
    gl_Position=clip;
    float top=smoothstep(h-2.2*H/L, h, y)*step(0.42,lv);
    vec3 c=mix(vec3(0.25,0.95,1.0), vec3(1.0,0.82,0.22), top);
    c=mix(vec3(0.12,0.3,1.0), c, smoothstep(0.0,0.22,y/H));
    gA=on*(sg>0.0?1.0:0.7)*(age<0.5?1.0:0.3);   // tout l'historique visible, la ligne la plus récente plus vive
    gC=c*1.2; gB=0.0;
    gS=on*pxScale*2.6*(10.0/clip.w);
}
)";
    const char* kLedFS = R"(
in vec3 vC; in float vA; in float vB; in vec2 vQ; out vec4 o;
void main(){ vec2 d=abs(vQ)*0.5; if(max(d.x,d.y)>0.42) discard; o=vec4(vC*vA, vA); }
)";

    // ── 3 · cyberpunk rubans (points ronds, flou de profondeur) ──
    const char* kRibbonVS = R"(
in vec3 g;
out vec3 gC; out float gA; out float gB; out float gS;
vec3 pal5(float h){ h=fract(h)*5.0;
    vec3 c0=vec3(1.0,0.45,0.1), c1=vec3(1.0,0.85,0.2), c2=vec3(1.0,0.25,0.6), c3=vec3(0.25,0.35,1.0), c4=vec3(0.1,0.85,1.0);
    if(h<1.0) return mix(c0,c1,h); if(h<2.0) return mix(c1,c2,h-1.0); if(h<3.0) return mix(c2,c3,h-2.0);
    if(h<4.0) return mix(c3,c4,h-3.0); return mix(c4,c0,h-4.0); }
void main(){
    float u=g.x, age=g.y, s=g.z;
    float lv=lvl(u,age);
    float wave=0.6*sin(u*6.0+time*0.9+age*0.3+s*0.4)+0.3*sin(u*13.0-time*1.3+age*0.2);
    vec3 p=vec3((u-0.5)*22.0+s*0.05, pow(lv,1.2)*3.4+wave-1.6, -(span-age-frac)*0.42+s*0.11);
    vec4 clip=mvp*vec4(p,1.0); gl_Position=clip;
    float d=clip.w, blur=abs(d-8.0)/3.0;
    gB=clamp(blur,0.0,1.5);
    gS=pxScale*(1.5+7.0*gB*gB)*(9.0/d);
    gA=0.5/(1.0+5.0*blur*blur);   // sans fondu de l'historique
    gC=pal5(u*1.5+age*0.035+time*0.03+s*0.08)*(0.6+1.6*lv);
}
)";
    const char* kRibbonFS = R"(
in vec3 vC; in float vA; in float vB; in vec2 vQ; out vec4 o;
void main(){
    float r=length(vQ)*0.5;
    float a=(1.0-smoothstep(0.5-0.45*(0.25+vB), 0.5, r))*vA;
    if(a<0.003) discard;
    o=vec4(vC*a, a);
}
)";

    // ── 4 · matrix (segments verts) ──
    const char* kMatrixVS = R"(
in vec2 g;
out float vA;
void main(){
    float u=g.x, age=g.y, lv=lvl(u,age);
    gl_Position=mvp*vec4((u-0.5)*22.0, pow(lv,1.25)*3.6, -(span-age-frac)*0.3, 1.0);   // pleine largeur, sans enveloppe : niveaux exacts
    vA=(0.22+0.78*lv)*0.55;   // sans fondu de l'historique
}
)";
    const char* kMatrixFS = R"(
in float vA; out vec4 o;
void main(){ o=vec4(vec3(0.18,1.0,0.55)*vA, vA); }
)";

    // ── 5 · splash (surface liquide) ──
    const char* kSplashVS = R"(
in vec2 g;
out vec3 vN; out vec3 vW; out float vH; out float vF;
const float SX=16.0, DZ=0.36, HH=2.6, DU=1.0/128.0;
float hgt(float u, float age){
    float l=(2.0*lvl(u,age)+lvl(u-DU,age)+lvl(u+DU,age))*0.25;
    return pow(l,1.4)*HH;
}
void main(){
    float u=g.x, age=g.y;
    float y=hgt(u,age);
    float hx=hgt(u+DU,age)-hgt(u-DU,age);
    float hz=hgt(u,max(age-1.0,0.0))-hgt(u,min(age+1.0,span-1.0));
    vN=normalize(vec3(-hx/(2.0*DU*SX), 1.0, hz/(2.0*DZ)));
    vW=vec3((u-0.5)*SX, y, -(span-age-frac)*DZ);
    vH=y/HH; vF=(age+frac)/span;
    gl_Position=mvp*vec4(vW,1.0);
}
)";
    const char* kSplashFS = R"(
in vec3 vN; in vec3 vW; in float vH; in float vF;
uniform vec3 eye;
out vec4 o;
void main(){
    vec3 n=normalize(vN), V=normalize(eye-vW), Ld=normalize(vec3(-0.4,0.85,0.45));
    if(dot(n,V)<0.0) n=-n;   // dessous du relief
    vec3 col=mix(vec3(0.03,0.13,0.32), vec3(0.02,0.55,1.0), smoothstep(0.03,0.45,vH));
    col=mix(col, vec3(0.75,0.97,1.0), smoothstep(0.5,0.95,vH));
    float diff=max(dot(n,Ld),0.0), spec=pow(max(dot(reflect(-Ld,n),V),0.0),48.0), fr=pow(1.0-max(dot(n,V),0.0),2.5);
    vec3 Rf=reflect(-V,n); float hz=1.0-smoothstep(0.0,0.7,Rf.y);
    vec3 env=mix(vec3(0.01,0.03,0.09), vec3(0.12,0.32,0.6), hz);
    vec3 c=col*(0.35+0.6*diff)+env*(0.12+0.45*fr)+vec3(1.0)*spec*1.2;
    float ripple=0.5+0.5*sin(vW.x*3.0+vW.z*2.0-time*2.0);
    c+=vec3(0.02,0.05,0.09)*ripple*(1.0-vH);
    c=mix(c, vec3(0.012,0.03,0.07), smoothstep(30.0,44.0,length(eye-vW)));   // brouillard lointain seulement
    o=vec4(c,1.0);
}
)";

    // particules calculées sur le processeur (gouttes, traits de glitch)
    const char* kPointVS = R"(#version 150
in vec3 pos; in vec3 col;
uniform mat4 mvp; uniform float sizePx, worldSize, halfH;
out vec3 gC; out float gA; out float gB; out float gS;
void main(){
    vec4 clip=mvp*vec4(pos,1.0); gl_Position=clip;
    gC=col; gA=1.0; gB=0.0;
    gS= worldSize>0.0 ? worldSize*halfH/clip.w : sizePx;
}
)";
    const char* kPointFS = R"(#version 150
in vec3 vC; in float vA; in float vB; in vec2 vQ; uniform float roundPt; out vec4 o;
void main(){
    float a=1.0;
    if(roundPt>0.5){
        float r=length(vQ);
        a= r<0.35 ? mix(1.0,0.75,r/0.35) : mix(0.75,0.0,clamp((r-0.35)/0.65,0.0,1.0));
    }
    o=vec4(vC, a);
}
)";

    // Points : chaque point devient un carré dans un geometry shader, comme le
    // Dot Spectrum (gl_PointCoord n'est pas fourni par tous les pilotes).
    // gS : diamètre en pixels ; vQ : -1..1 dans le carré.
    const char* kSpriteGS = R"(#version 150
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
in vec3 gC[]; in float gA[]; in float gB[]; in float gS[];
uniform vec2 vpPx;
out vec3 vC; out float vA; out float vB; out vec2 vQ;
void main(){
    if(gS[0]<=0.0 || gA[0]<=0.0) return;
    vec4 c=gl_in[0].gl_Position;
    vec2 h=vec2(gS[0]/vpPx.x, gS[0]/vpPx.y)*c.w;
    for(int k=0;k<4;++k){
        vec2 q=vec2(float(k&1)*2.0-1.0, float(k>>1)*2.0-1.0);
        gl_Position=vec4(c.xy+q*h, c.z, c.w);
        vC=gC[0]; vA=gA[0]; vB=gB[0]; vQ=q;
        EmitVertex();
    }
    EndPrimitive();
}
)";

    // post-traitement : triangle plein écran sans attribut
    const char* kQuadVS = R"(#version 150
out vec2 uv;
void main(){ vec2 p=vec2(float((gl_VertexID<<1)&2), float(gl_VertexID&2)); uv=p; gl_Position=vec4(p*2.0-1.0,0.0,1.0); }
)";
    const char* kBrightFS = R"(#version 150
in vec2 uv; uniform sampler2D src; uniform float threshold; out vec4 o;
void main(){ vec3 c=texture(src,uv).rgb; float l=dot(c,vec3(0.299,0.587,0.114)); o=vec4(c*smoothstep(threshold,threshold+0.01,l),1.0); }
)";
    const char* kBlurFS = R"(#version 150
in vec2 uv; uniform sampler2D src; uniform vec2 dir; out vec4 o;
void main(){
    vec3 c=texture(src,uv).rgb*0.227027;
    c+=(texture(src,uv+dir).rgb+texture(src,uv-dir).rgb)*0.1945946;
    c+=(texture(src,uv+2.0*dir).rgb+texture(src,uv-2.0*dir).rgb)*0.1216216;
    c+=(texture(src,uv+3.0*dir).rgb+texture(src,uv-3.0*dir).rgb)*0.054054;
    c+=(texture(src,uv+4.0*dir).rgb+texture(src,uv-4.0*dir).rgb)*0.016216;
    o=vec4(c,1.0);
}
)";
    const char* kCompFS = R"(#version 150
in vec2 uv; uniform sampler2D scene, bloomH, bloomQ; uniform float strength, radius, exposure; out vec4 o;
vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0); }
vec3 srgb(vec3 c){ return mix(c*12.92, 1.055*pow(c,vec3(1.0/2.4))-0.055, step(vec3(0.0031308),c)); }
void main(){
    vec3 b=texture(bloomH,uv).rgb*(1.0-0.4*radius)+texture(bloomQ,uv).rgb*(0.6+0.8*radius);
    vec3 c=texture(scene,uv).rgb+strength*1.6*b;
    o=vec4(srgb(aces(c*exposure)),1.0);
}
)";

    std::string withHeader(const char* body, bool palette=false)
    {
        std::string s="#version 150\n";
        s+=kHistGlsl;
        if(palette) s+=kPaletteGlsl;
        return s+body;
    }

    // ── matrices (colonnes, comme OpenGL) ──
    struct Mat4 { float m[16] {}; };
    Mat4 mul(const Mat4& a, const Mat4& b)
    {
        Mat4 r;
        for(int c=0;c<4;++c)
            for(int rr=0;rr<4;++rr){
                float s=0.f;
                for(int k=0;k<4;++k) s+=a.m[k*4+rr]*b.m[c*4+k];
                r.m[c*4+rr]=s;
            }
        return r;
    }
    using V3=juce::Vector3D<float>;

    // palette des barres (même stops), linéaire : couleur des gouttes
    void paletteLin(float u, float& r, float& g, float& b)
    {
        static const float P[7][4]={ {0.f,32,216,200}, {0.2f,90,64,255}, {0.4f,195,60,255}, {0.55f,255,74,154},
                                     {0.68f,255,154,60}, {0.84f,138,60,255}, {1.f,34,208,224} };
        auto lin=[](float c){ return std::pow(c/255.f, 2.2f); };
        u=juce::jlimit(0.f, 1.f, u);
        int i=1; while(i<6 && u>P[i][0]) ++i;
        const float t=(u-P[i-1][0])/juce::jmax(1e-4f, P[i][0]-P[i-1][0]);
        r=lin(P[i-1][1]+(P[i][1]-P[i-1][1])*t);
        g=lin(P[i-1][2]+(P[i][2]-P[i-1][2])*t);
        b=lin(P[i-1][3]+(P[i][3]-P[i-1][3])*t);
    }

    float srgbToLin(int v) { return std::pow((float)v/255.f, 2.2f); }

    // réglages par style : lueur (force, rayon, seuil), exposition, fond (sRGB)
    struct FxLook { float strength, radius, threshold, exposure; int bg[3]; };
    const FxLook kLooks[5]={
        { 0.9f,  0.55f, 0.6f,  1.0f,  { 12, 6, 20 } },   // Bars
        { 0.95f, 0.35f, 0.3f,  1.0f,  { 2, 3, 6 } },     // Led
        { 1.25f, 0.8f,  0.15f, 1.2f,  { 5, 3, 10 } },    // Ribbons
        { 0.75f, 0.35f, 0.05f, 1.0f,  { 0, 0, 0 } },     // Matrix
        { 0.55f, 0.4f,  0.92f, 0.85f, { 3, 8, 18 } } };  // Splash

    // nombre de lignes affichées par style
    constexpr int kSpan[5]={ 26, 24, 46, 60, 60 };

    // caméra d'origine de chaque style (celle des aperçus) : l'orbite tourne
    // autour de la cible, à la même distance
    struct StyleCam { float ex, ey, ez, tx, ty, tz, fov; };
    const StyleCam kCams[5]={
        {  0.f,  6.6f, 13.5f,  0.f,  1.3f, -4.5f, 36.f },   // Bars
        {  0.f,  2.2f,  9.5f,  0.f,  0.1f, -5.f,  34.f },   // Led
        { -1.5f, 1.1f,  4.2f,  1.f, -0.4f, -8.f,  50.f },   // Ribbons
        {  0.f,  3.2f,  9.5f,  0.f,  0.8f, -7.f,  38.f },   // Matrix (de face, comme le Cristal)
        {  0.f,  4.6f, 10.f,   0.f,  0.9f, -6.f,  40.f } }; // Splash
    // géométrie (comme les shaders) : x = (u-0,5)*W, z = z0-(span-âge-frac)*DZ (la plus
    // récente au fond, z0 = bord avant) ; baseY : ligne des repères
    struct StyleGeo { float W, z0, DZ, baseY; };
    const StyleGeo kGeo[5]={ { 17.f, 2.f, 0.6f, 0.f }, { 16.f, 0.f, 0.7f, 0.f }, { 22.f, 0.f, 0.42f, -1.6f },
                             { 22.f, 0.f, 0.3f, 0.f }, { 16.f, 0.f, 0.36f, 0.f } };
    // TIME d'origine (durée de l'historique affiché), proche du défilement des aperçus
    constexpr float kDefaultTime[5]={ 1.f, 1.f, 1.5f, 2.f, 2.f };
    // mêmes butées que le Cristal (inclinaison absolue, zoom en écart)
    constexpr float kMinPitch=0.04f, kMaxPitch=1.5f, kMinZoom=-0.25f, kMaxZoom=0.25f;

    float basePitch(int s)
    {
        const auto& c=kCams[s];
        const V3 d(c.ex-c.tx, c.ey-c.ty, c.ez-c.tz);
        return std::asin(d.y/d.length());
    }

    // vues droites : un angle tout près d'un multiple de 90° y est ramené pile (comme le Cristal)
    float snapYaw(float y, float tolRad)
    {
        constexpr float twoPi=juce::MathConstants<float>::twoPi, quarter=juce::MathConstants<float>::halfPi;
        y=std::remainder(y, twoPi);
        const float q=quarter*std::round(y/quarter);
        return std::abs(y-q)<tolRad ? std::remainder(q, twoPi) : y;
    }
    const float kSnapAtLoad=juce::degreesToRadians(5.f), kSnapDrag=juce::degreesToRadians(2.5f);
}

//==============================================================================
GpuSpectroFxDisplay::GpuSpectroFxDisplay(LeakAudioProcessor& p, Style s) : GpuModuleBase(p), style(s)
{
    logMap.setExtendedAxis(true);   // axe jusqu'à Nyquist au-delà de 48 kHz
    parts.reserve(2600);
    if(style==Style::Led)
        for(int i=0;i<34;++i) streaks.push_back({ 0.f, 0.f, 0.f, 0.f, 0.f, -1.f, 1.f, 1.f, 1.f });

    // fader TIME (1 à 20 s, milieu de course 4 s) et HOLD : comme le Cristal
    timeFader=std::make_unique<ReleaseFader>();
    timeFader->configure("TIME", 1.f, 20.f, 4.f, kDefaultTime[(int)style]);
    timeFader->onValueChange=[this](float v){ timeRef()=v; timeSecA=v; repaint(); };
    addAndMakeVisible(*timeFader);
    holdBtn=std::make_unique<NeuAnimButton>("HOLD");
    holdBtn->setClickingTogglesState(true);
    holdBtn->onClick=[this]{ frozen=holdBtn->getToggleState(); };
    addAndMakeVisible(*holdBtn);
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    syncFromProcessor();
    // fenêtre du plugin rouverte : HOLD et image figée retrouvés (v1.81.46)
    if(auto* sn=processor.heldSnapshot(26+(int)style); sn!=nullptr && sn->rows.size()==(size_t)(NZ*NX) && sn->rows2.size()==(size_t)(NZ*NB)){
        std::copy(sn->rows.begin(), sn->rows.end(), &rows[0][0]);
        std::copy(sn->rows2.begin(), sn->rows2.end(), &bandRows[0][0]);
        head=sn->head; bandsShown=sn->count2; heldFrac=sn->frac; animT=sn->value;
        frozen=true;
        holdBtn->setToggleState(true, juce::dontSendNotification);
    }
}

GpuSpectroFxDisplay::~GpuSpectroFxDisplay()
{
    detachGL();
    // fenêtre du plugin fermée : HOLD et image figée gardés pour la réouverture
    auto& sn=processor.moduleSnapshots[26+(int)style];
    sn.hold=frozen.load();
    if(sn.hold){
        sn.rows.assign(&rows[0][0], &rows[0][0]+NZ*NX);
        sn.rows2.assign(&bandRows[0][0], &bandRows[0][0]+NZ*NB);
        sn.head=head; sn.count2=bandsShown; sn.frac=heldFrac; sn.value=animT;
    } else { sn.rows.clear(); sn.rows2.clear(); }
}

const char* GpuSpectroFxDisplay::title() const
{
    switch(style){
        case Style::Bars:    return "SPECTROGRAM GPU - BARS";
        case Style::Led:     return "SPECTROGRAM GPU - CYBERPUNK LED";
        case Style::Ribbons: return "SPECTROGRAM GPU - CYBERPUNK RIBBONS";
        case Style::Matrix:  return "SPECTROGRAM GPU - MATRIX";
        case Style::Splash:  return "SPECTROGRAM GPU - SPLASH";
    }
    return "";
}

// Barres : plus TIME est long, plus il y a de lignes (plus fines en profondeur,
// même profondeur totale) : la précision en temps reste lisible
int GpuSpectroFxDisplay::span() const
{
    if(style==Style::Bars)
        return juce::jlimit(kSpan[0], NZ-3, juce::roundToInt((float)kSpan[0]*std::sqrt(juce::jmax(1.f, timeSecA.load()))));
    return kSpan[(int)style];
}
float GpuSpectroFxDisplay::rowDZ() const
{
    const auto& geo=kGeo[(int)style];
    return style==Style::Bars ? geo.DZ*(float)kSpan[0]/(float)span() : geo.DZ;
}
double GpuSpectroFxDisplay::captureMs() const { return juce::jmax(1.0, (double)timeSecA.load())*1000.0/(double)span(); }
float& GpuSpectroFxDisplay::timeRef()  { return processor.fxTimeSec[(int)style]; }
float& GpuSpectroFxDisplay::yawRef()   { return processor.fxYaw[(int)style]; }
float& GpuSpectroFxDisplay::pitchRef() { return processor.fxPitch[(int)style]; }
float& GpuSpectroFxDisplay::zoomRef()  { return processor.fxZoom[(int)style]; }

void GpuSpectroFxDisplay::syncFromProcessor()
{
    timeSecA=timeRef();
    timeFader->setValue(timeRef(), juce::dontSendNotification);
    yawRef()=snapYaw(yawRef(), kSnapAtLoad);
    yaw=yawRef();
    const float p0=basePitch((int)style);
    pitch=juce::jlimit(kMinPitch-p0, kMaxPitch-p0, pitchRef());
    zoom=juce::jlimit(kMinZoom, kMaxZoom, zoomRef());
    repaint();
}

// Orbite autour de la cible du style ; vue plus étroite que les aperçus
// (16/9) : on garde leur champ horizontal ; zoom : image agrandie / resserrée
GpuSpectroFxDisplay::Cam GpuSpectroFxDisplay::makeCam(float yawOff, float pitchOff, float zoomAmount, float aspect) const
{
    const auto& sc=kCams[(int)style];
    const V3 target(sc.tx, sc.ty, sc.tz), d0(sc.ex-sc.tx, sc.ey-sc.ty, sc.ez-sc.tz);
    const float r=d0.length();
    const float yw=std::atan2(d0.x, d0.z)+yawOff;
    const float pt=juce::jlimit(kMinPitch, kMaxPitch, std::asin(d0.y/r)+pitchOff);
    const V3 eye=target+V3(std::sin(yw)*std::cos(pt), std::sin(pt), std::cos(yw)*std::cos(pt))*r;
    aspect=juce::jmax(0.2f, aspect);
    float fov=sc.fov;
    if(aspect<16.f/9.f)
        fov=juce::radiansToDegrees(2.f*std::atan(std::tan(juce::degreesToRadians(fov)*0.5f)*(16.f/9.f)/aspect));
    fov=juce::jmin(fov, 110.f);
    const float tanHalf=std::tan(juce::degreesToRadians(fov)*0.5f)/(1.f+zoomAmount);

    const V3 f=(target-eye).normalised(), sv=(f^V3(0.f,1.f,0.f)).normalised(), u=sv^f;
    Mat4 view;
    view.m[0]=sv.x; view.m[4]=sv.y; view.m[8]=sv.z;
    view.m[1]=u.x;  view.m[5]=u.y;  view.m[9]=u.z;
    view.m[2]=-f.x; view.m[6]=-f.y; view.m[10]=-f.z;
    view.m[12]=-(sv*eye); view.m[13]=-(u*eye); view.m[14]=f*eye; view.m[15]=1.f;
    Mat4 proj;
    const float n=0.1f, fa=200.f;
    proj.m[0]=1.f/(tanHalf*aspect); proj.m[5]=1.f/tanHalf; proj.m[10]=(fa+n)/(n-fa); proj.m[11]=-1.f; proj.m[14]=2.f*fa*n/(n-fa);
    const Mat4 vp=mul(proj, view);
    Cam c;
    std::copy(std::begin(vp.m), std::end(vp.m), c.mvp);
    c.eye[0]=eye.x; c.eye[1]=eye.y; c.eye[2]=eye.z;
    c.dir[0]=f.x;   c.dir[1]=f.y;   c.dir[2]=f.z;
    c.right[0]=sv.x; c.right[1]=sv.y; c.right[2]=sv.z;
    c.up[0]=u.x;    c.up[1]=u.y;    c.up[2]=u.z;
    c.tanHalfY=tanHalf; c.aspect=aspect;
    return c;
}

void GpuSpectroFxDisplay::resized()
{
    // la scène occupe la zone à droite du fader TIME (comme le Cristal)
    viewW=juce::jmax(1, getWidth()-(int)kReleaseFaderW);
    viewH=juce::jmax(1, getHeight());
    const int top=juce::roundToInt(24.f*Ui::textScale);
    timeFader->setBounds(2, top, (int)kReleaseFaderW-4, juce::jmax(10, getHeight()-top-8));
    const int hh=juce::jlimit(16, 22, juce::roundToInt(19.f*Ui::textScale));
    const int hw=juce::roundToInt(46.f*Ui::textScale);
    holdBtn->setBounds(getWidth()-hw-8, 2, hw, hh);
    titleBottom=holdBtn->getBottom()+3;
}

void GpuSpectroFxDisplay::uiTick()
{
    // lecture au survol : repeint le calque 2D seulement si le point a changé
    const bool v=hoverOn.load() && hitValid.load();
    const float u=hitU.load(), a=hitAge.load(), l=hitLevel.load();
    if(v!=shownValid || (v && (std::abs(u-shownU)>1e-4f || std::abs(a-shownAge)>0.05f || std::abs(l-shownLevel)>1e-3f))){
        shownValid=v; shownU=u; shownAge=a; shownLevel=l;
        repaint();
    }
}

void GpuSpectroFxDisplay::mouseMove(const juce::MouseEvent& e)  { hoverX=e.position.x; hoverY=e.position.y; hoverOn=true; }
void GpuSpectroFxDisplay::mouseExit(const juce::MouseEvent&)    { hoverOn=false; }

// Orbite à la souris (double-clic = vue d'origine), comme le Cristal
void GpuSpectroFxDisplay::mouseDown(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown()) return;   // laisse remonter pour le menu de slot
    yawAtDrag=yaw.load();
    pitchAtDrag=pitch.load();
    dragFromTitle=e.position.y<Ui::titleStripHeight();   // titre : détachement, pas d'orbite
}
void GpuSpectroFxDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown() || dragFromTitle) return;
    const float p0=basePitch((int)style);
    yaw   = snapYaw(yawAtDrag + (float)e.getDistanceFromDragStartX()*0.006f, kSnapDrag);
    pitch = juce::jlimit(kMinPitch-p0, kMaxPitch-p0, pitchAtDrag + (float)e.getDistanceFromDragStartY()*0.004f);
    yawRef()=yaw.load(); pitchRef()=pitch.load();
    hoverX=e.position.x; hoverY=e.position.y;
    repaint();   // repères
}
void GpuSpectroFxDisplay::mouseDoubleClick(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown()) return;
    yaw=0.f; pitch=0.f; zoom=0.f;
    yawRef()=0.f; pitchRef()=0.f; zoomRef()=0.f;
    repaint();
}
void GpuSpectroFxDisplay::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    float z=juce::jlimit(kMinZoom, kMaxZoom, zoom.load()+w.deltaY*0.053f);   // ~2,5 % par cran
    if(std::abs(z)<0.008f) z=0.f;   // aimant sur le cadrage d'origine
    zoom=z; zoomRef()=z;
    repaint();
}

// Calque 2D (comme le Cristal) : repères de fréquence le long de la ligne la
// plus récente (au fond), de temps le long du bord droit, titre, zoom, HOLD,
// lecture au survol
void GpuSpectroFxDisplay::paint(juce::Graphics& g)
{
    const int W=getWidth(), H=getHeight();
    if(W<40 || H<40) return;
    const bool gl=isGlAttached();
    if(!gl) g.fillAll(Col::bg);

    const float off=kReleaseFaderW, W3=juce::jmax(1.f, (float)W-off);
    const auto cam=makeCam(yaw.load(), pitch.load(), zoom.load(), W3/(float)H);
    const auto& geo=kGeo[(int)style];
    auto project=[&](float x, float y, float z, juce::Point<float>& out){
        const float* m=cam.mvp;
        const float cx=m[0]*x+m[4]*y+m[8]*z+m[12], cy=m[1]*x+m[5]*y+m[9]*z+m[13], cw=m[3]*x+m[7]*y+m[11]*z+m[15];
        if(cw<=0.01f) return false;
        out={ off+(cx/cw*0.5f+0.5f)*W3, (1.f-(cy/cw*0.5f+0.5f))*(float)H };
        return true;
    };
    const float fTop=PsdLogMap::topHz(processor.getCurrentSampleRate());
    const float logSpan=std::log(fTop/20.f);
    g.setFont(Ui::iceland(9.f));
    SpectroReadout::drawFreqMarks(g, fTop, [&](float hz, juce::Point<float>& at){
            return project((std::log(hz/20.f)/logSpan-0.5f)*geo.W, geo.baseY, geo.z0-(float)span()*rowDZ()-0.3f, at); },   // le long de la ligne la plus récente
        12.f*Ui::textScale, { off+4.f, (float)titleBottom.load()+1.f, W3-8.f, (float)(H-titleBottom.load())-5.f });
    {
        const float T=juce::jmax(0.5f, timeSecA.load()), step=SpectroReadout::timeTickStep(T);
        g.setFont(Ui::iceland(8.5f));
        for(float t=0.f; t<=T+1e-3f; t+=step){
            juce::Point<float> p;
            if(!project(geo.W*0.5f, geo.baseY, geo.z0-(1.f-t/T)*(float)span()*rowDZ(), p)) continue;   // 0 s au fond (la plus récente)
            if(p.y<(float)titleBottom.load()+6.f || p.y>(float)H-6.f || p.x<off || p.x>(float)W-4.f) continue;
            const auto lbl=SpectroReadout::formatTimeTick(t, step);
            const float w=juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), lbl)+2.f;
            g.setColour(Col::textMid.withAlpha(0.6f));
            g.drawLine(p.x+2.f, p.y, p.x+6.f, p.y, 1.f);
            const float lx=juce::jmin(p.x+8.f, (float)W-w-4.f);
            SpectroReadout::drawShadowedText(g, lbl, { lx, p.y-6.f, w, 12.f }, juce::Justification::centredLeft, Col::textMid.withAlpha(0.85f));
        }
    }

    Ui::drawTitleDecor(g, (float)W, (float)holdBtn->getBottom()+2.f);   // sous HOLD
    const juce::String zoomText="ZOOM "+juce::String(juce::roundToInt(zoom.load()*100.f))+" %";
    const auto zoomFont=Ui::iceland(9.f);
    const int zoomW=juce::roundToInt(juce::GlyphArrangement::getStringWidth(zoomFont, zoomText))+2;
    g.setFont(zoomFont);
    g.setColour(Col::textMid.withAlpha(0.8f));
    g.drawText(zoomText, holdBtn->getX()-zoomW-8, 5, zoomW, 13, juce::Justification::centredRight, false);
    g.setFont(Ui::iceland(10.5f).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    Ui::drawTitleFit(g, juce::String(title()), { 6.f, 5.f, (float)juce::jmax(20, holdBtn->getX()-zoomW-20), 13.f }, g.getCurrentFont());

    if(gl && shownValid)
        SpectroReadout::draw(g, getLocalBounds().toFloat(), { hoverX.load(), hoverY.load() },
                             20.f*std::pow(fTop/20.f, shownU), shownLevel*90.f-100.f,
                             shownAge*(float)captureMs()*0.001f);
    paintGlFailure(g);
    paintScreenFrame(g);
}

//==============================================================================
// Thread GL
//==============================================================================
void GpuSpectroFxDisplay::newOpenGLContextCreated()
{
    glFailed=false;
    switch(style){
        case Style::Bars:    sceneShader=compileProgram(withHeader(kBarsVS, true).c_str(), withHeader(kBarsFS).c_str()); break;
        case Style::Led:     sceneShader=compileProgram(withHeader(kLedVS).c_str(), withHeader(kLedFS).c_str(), kSpriteGS); break;
        case Style::Ribbons: sceneShader=compileProgram(withHeader(kRibbonVS).c_str(), withHeader(kRibbonFS).c_str(), kSpriteGS); break;
        case Style::Matrix:  sceneShader=compileProgram(withHeader(kMatrixVS).c_str(), withHeader(kMatrixFS).c_str()); break;
        case Style::Splash:  sceneShader=compileProgram(withHeader(kSplashVS).c_str(), withHeader(kSplashFS).c_str()); break;
    }
    pointShader =compileProgram(kPointVS, kPointFS, kSpriteGS);
    brightShader=compileProgram(kQuadVS, kBrightFS);
    blurShader  =compileProgram(kQuadVS, kBlurFS);
    compShader  =compileProgram(kQuadVS, kCompFS);
    if(sceneShader==nullptr || pointShader==nullptr || brightShader==nullptr || blurShader==nullptr || compShader==nullptr) return;

    buildGeometry();

    auto makeTex=[](unsigned int& tex, int w, int h, const float* data){
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    };
    makeTex(histTex, NX, NZ, &rows[0][0]);
    makeTex(bandTex, NB, NZ, &bandRows[0][0]);
    glBindTexture(GL_TEXTURE_2D, 0);
    onContextCreated();
}

void GpuSpectroFxDisplay::buildGeometry()
{
    GLint prevVao=0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    std::vector<float> v;
    std::vector<GLuint> idx;
    int comps=0;
    const int R=kSpan[(int)style];

    if(style==Style::Bars){
        // cube : x, z dans ±0,5 ; y dans 0..1 (pied au sol)
        const float F[6][3]={ {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
        for(auto& n : F){
            // quatre coins de la face
            V3 nn(n[0], n[1], n[2]);
            V3 a = std::abs(n[1])>0.5f ? V3(1.f,0.f,0.f) : V3(0.f,1.f,0.f);
            V3 b = nn^a;
            V3 c[4];
            const float sa[4]={ -1.f, 1.f, 1.f, -1.f }, sb[4]={ -1.f, -1.f, 1.f, 1.f };
            for(int k=0;k<4;++k) c[k]=nn*0.5f+a*(0.5f*sa[k])+b*(0.5f*sb[k]);
            const int tri[6]={ 0, 1, 2, 0, 2, 3 };
            for(int t : tri) v.insert(v.end(), { c[t].x, c[t].y+0.5f, c[t].z, n[0], n[1], n[2] });
        }
        comps=6;
    } else if(style==Style::Led){
        constexpr int C=120, L=28;
        for(int j=0;j<R;++j) for(int i=0;i<C;++i) for(int k=0;k<L;++k) for(float sg : { 1.f, -1.f })
            v.insert(v.end(), { (float)i/(float)(C-1), (float)j, (float)k, sg });
        comps=4;
    } else if(style==Style::Ribbons){
        constexpr int C=260, S=3;
        for(int j=0;j<R;++j) for(int s=0;s<S;++s) for(int i=0;i<C;++i)
            v.insert(v.end(), { (float)i/(float)(C-1), (float)j, (float)s });
        comps=3;
    } else if(style==Style::Matrix){
        constexpr int C=180;
        for(int j=0;j<R;++j) for(int i=0;i<C-1;++i)
            v.insert(v.end(), { (float)i/(float)(C-1), (float)j, (float)(i+1)/(float)(C-1), (float)j });
        comps=2;
    } else {
        constexpr int C=128;
        for(int j=0;j<R;++j) for(int i=0;i<C;++i) v.insert(v.end(), { (float)i/(float)(C-1), (float)j });
        for(int j=0;j<R-1;++j) for(int i=0;i<C-1;++i){
            const GLuint a=(GLuint)(j*C+i), b=a+1, c=a+(GLuint)C, d=c+1;
            idx.insert(idx.end(), { a, c, b, b, c, d });
        }
        comps=2;
    }
    sceneVerts=(int)(v.size()/(size_t)comps);
    sceneIndices=(int)idx.size();

    glGenVertexArrays(1, &vaoScene);
    glBindVertexArray(vaoScene);
    glGenBuffers(1, &vboScene);
    glBindBuffer(GL_ARRAY_BUFFER, vboScene);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size()*sizeof(float)), v.data(), GL_STATIC_DRAW);
    if(!idx.empty()){
        glGenBuffers(1, &iboScene);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboScene);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idx.size()*sizeof(GLuint)), idx.data(), GL_STATIC_DRAW);
    }
    const GLuint prog=sceneShader->getProgramID();
    auto attrib=[&](const char* name, int size, int offset){
        const GLint loc=glGetAttribLocation(prog, name);
        if(loc<0) return;
        glEnableVertexAttribArray((GLuint)loc);
        glVertexAttribPointer((GLuint)loc, size, GL_FLOAT, GL_FALSE, comps*(GLsizei)sizeof(float), (const void*)(size_t)(offset*(int)sizeof(float)));
    };
    if(style==Style::Bars){ attrib("pos", 3, 0); attrib("nrm", 3, 3); }
    else attrib("g", comps, 0);

    // particules (dynamiques) : x, y, z, r, g, b
    glGenVertexArrays(1, &vaoPts);
    glBindVertexArray(vaoPts);
    glGenBuffers(1, &vboPts);
    glBindBuffer(GL_ARRAY_BUFFER, vboPts);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);
    const GLuint pp=pointShader->getProgramID();
    for(auto [name, off] : { std::pair<const char*, int> { "pos", 0 }, { "col", 3 } }){
        const GLint loc=glGetAttribLocation(pp, name);
        if(loc<0) continue;
        glEnableVertexAttribArray((GLuint)loc);
        glVertexAttribPointer((GLuint)loc, 3, GL_FLOAT, GL_FALSE, 6*(GLsizei)sizeof(float), (const void*)(size_t)(off*(int)sizeof(float)));
    }
    glGenVertexArrays(1, &vaoQuad);   // vide : le triangle plein écran vient de gl_VertexID
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GpuSpectroFxDisplay::openGLContextClosing()
{
    releaseTargets();
    for(auto* b : { &vboScene, &iboScene, &vboPts }) if(*b){ glDeleteBuffers(1, b); *b=0; }
    for(auto* a : { &vaoScene, &vaoPts, &vaoQuad }) if(*a){ glDeleteVertexArrays(1, a); *a=0; }
    for(auto* t : { &histTex, &bandTex }) if(*t){ glDeleteTextures(1, t); *t=0; }
    sceneShader.reset(); pointShader.reset(); brightShader.reset(); blurShader.reset(); compShader.reset();
}

void GpuSpectroFxDisplay::releaseTargets()
{
    for(auto* f : { &msFbo, &sceneFbo }) if(*f){ glDeleteFramebuffers(1, f); *f=0; }
    for(auto* r : { &msColor, &msDepth }) if(*r){ glDeleteRenderbuffers(1, r); *r=0; }
    if(sceneTex){ glDeleteTextures(1, &sceneTex); sceneTex=0; }
    for(int i=0;i<4;++i){
        if(blurFbo[i]){ glDeleteFramebuffers(1, &blurFbo[i]); blurFbo[i]=0; }
        if(blurTex[i]){ glDeleteTextures(1, &blurTex[i]); blurTex[i]=0; }
    }
    targetW=targetH=0;
}

bool GpuSpectroFxDisplay::ensureTargets(int pw, int ph)
{
    if(sceneFbo!=0 && pw==targetW && ph==targetH) return true;
    releaseTargets();
    GLint prevFbo=0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    auto colorTex=[](unsigned int& tex, unsigned int& fbo, int w, int h){
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        return glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    };

    // scène multi-échantillonnée (4 au plus), sinon simple
    GLint maxSamples=0; glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    bool ok=false;
    for(int samples : { juce::jmin(4, (int)maxSamples), 0 }){
        if(msFbo){ glDeleteFramebuffers(1, &msFbo); msFbo=0; }
        for(auto* r : { &msColor, &msDepth }) if(*r){ glDeleteRenderbuffers(1, r); *r=0; }
        glGenRenderbuffers(1, &msColor);
        glBindRenderbuffer(GL_RENDERBUFFER, msColor);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, pw, ph);
        glGenRenderbuffers(1, &msDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, msDepth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, pw, ph);
        glGenFramebuffers(1, &msFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);
        ok=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
        if(ok) break;
    }
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    ok=ok && colorTex(sceneTex, sceneFbo, pw, ph);
    const int hw=juce::jmax(1, pw/2), hh=juce::jmax(1, ph/2), qw=juce::jmax(1, pw/4), qh=juce::jmax(1, ph/4);
    ok=ok && colorTex(blurTex[0], blurFbo[0], hw, hh) && colorTex(blurTex[1], blurFbo[1], hw, hh)
          && colorTex(blurTex[2], blurFbo[2], qw, qh) && colorTex(blurTex[3], blurFbo[3], qw, qh);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    if(!ok){ releaseTargets(); return false; }
    targetW=pw; targetH=ph;
    return true;
}

// Ligne en cours : crête (par fréquence) de toutes les images depuis le début
// de sa durée, envoyée à chaque image ; les barres en sont tirées directement
// (crête des fréquences de chaque bande, sans retombée : valeurs mesurées)
void GpuSpectroFxDisplay::accumulate(bool startNew)
{
    if(!logMap.read(processor, liveDb)) return;
    glActiveTexture(GL_TEXTURE0);
    if(logMap.axisVersion()!=seenAxis){   // nouvel axe : l'historique suivait l'ancien
        seenAxis=logMap.axisVersion();
        std::memset(rows, 0, sizeof(rows));
        std::memset(bandRows, 0, sizeof(bandRows));
        glBindTexture(GL_TEXTURE_2D, histTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, NX, NZ, GL_RED, GL_FLOAT, rows);
        glBindTexture(GL_TEXTURE_2D, bandTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, NB, NZ, GL_RED, GL_FLOAT, bandRows);
        startNew=true;
    }
    const int live=liveRow();
    float* r=rows[live];
    for(int p=0;p<NX;++p){
        const float v=juce::jlimit(0.f, 1.f, (liveDb[p]+100.f)/90.f);   // -100 dB -> 0, -10 dB -> 1
        r[p]=startNew ? v : juce::jmax(r[p], v);
    }
    glBindTexture(GL_TEXTURE_2D, histTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, live, NX, 1, GL_RED, GL_FLOAT, r);

    if(style==Style::Bars){
        const int n=juce::jlimit(1, NB, processor.getNumActiveBands());
        if(n!=bandsShown){   // VIEW changé : les bandes d'avant n'ont plus de sens
            std::memset(bandRows, 0, sizeof(bandRows)); bandsShown=n;
            glBindTexture(GL_TEXTURE_2D, bandTex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, NB, NZ, GL_RED, GL_FLOAT, bandRows);
        }
        for(int k=0;k<n;++k){
            const int a=k*NX/n, e=juce::jmax(a+1, (k+1)*NX/n);
            float m=0.f; for(int i=a;i<e;++i) m=juce::jmax(m, r[i]);
            bandRows[live][k]=m;
        }
        glBindTexture(GL_TEXTURE_2D, bandTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, live, NB, 1, GL_RED, GL_FLOAT, bandRows[live]);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Fin de durée : la ligne en cours devient la plus récente validée ; la
// suivante repart de la même valeur (deux validations dans une même image ne
// laissent pas de ligne périmée), accumulate() la remplace aussitôt
void GpuSpectroFxDisplay::commit()
{
    const int done=liveRow();
    head=done;
    const int next=liveRow();
    std::memcpy(rows[next], rows[done], sizeof(rows[0]));
    std::memcpy(bandRows[next], bandRows[done], sizeof(bandRows[0]));

    if(style==Style::Splash){
        // attaque dans une bande : des gouttes jaillissent de la ligne la plus récente (au fond)
        const float* r=rows[done];
        constexpr int B=40;
        for(int b=0;b<B;++b){
            const int a=b*NX/B, e=(b+1)*NX/B;
            float m=0.f; for(int i=a;i<e;++i) m=juce::jmax(m, r[i]);
            const float jump=m-prevBand[b]; prevBand[b]=m;
            if(jump>0.09f && m>0.4f){
                const float u=((float)b+0.5f)/(float)B, y0=std::pow(m, 1.4f)*2.6f;
                float cr, cg, cb; paletteLin(u, cr, cg, cb);
                const int cnt=juce::jmin(26, (int)(jump*70.f));
                for(int k=0;k<cnt;++k){
                    Particle q { (u-0.5f)*16.f+(rng.nextFloat()-0.5f)*0.35f, y0, -(float)span()*0.36f+rng.nextFloat()*0.8f,
                                 (rng.nextFloat()-0.5f)*1.8f, (2.5f+6.f*m)*(0.55f+0.6f*rng.nextFloat()), (rng.nextFloat()-0.2f)*1.4f,
                                 cr*1.4f, cg*1.4f, cb*1.4f, 3.f };
                    if(parts.size()<2600) parts.push_back(q);
                }
            }
        }
    }
}

void GpuSpectroFxDisplay::updateParticles(float dt)
{
    ptVerts.clear();
    if(style==Style::Splash){
        std::vector<Particle> spray;
        for(auto& q : parts){
            q.vy-=9.f*dt;
            q.x+=q.vx*dt; q.y+=q.vy*dt; q.z+=q.vz*dt;
            q.life-=dt;
            if(q.y<0.05f && q.vy<0.f){   // retombe : petite gerbe
                q.life=0.f;
                if(rng.nextFloat()<0.5f)
                    for(int s=0;s<2;++s)
                        spray.push_back({ q.x+(rng.nextFloat()-0.5f)*0.2f, 0.06f, q.z, (rng.nextFloat()-0.5f)*1.5f, 1.f+rng.nextFloat()*1.4f,
                                          (rng.nextFloat()-0.5f)*1.5f, q.r*0.6f, q.g*0.6f, q.b*0.6f, 0.6f });
            }
        }
        parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& q){ return q.life<=0.f; }), parts.end());
        for(auto& q : spray) if(parts.size()<2600) parts.push_back(q);
        for(auto& q : parts) ptVerts.insert(ptVerts.end(), { q.x, q.y, q.z, q.r, q.g, q.b });
    } else if(style==Style::Led){
        static const float kCols[5][3]={ {1.f,0.54f,0.24f}, {0.24f,0.42f,1.f}, {0.87f,0.91f,1.f}, {1.f,0.82f,0.24f}, {1.f,0.54f,0.24f} };
        for(auto& s : streaks){
            s.life-=dt; s.x+=s.vx*dt;
            if(s.life<0.f){
                const auto* c=kCols[rng.nextInt(5)];
                s={ -9.f+rng.nextFloat()*14.f, (rng.nextFloat()-0.5f)*6.f, -rng.nextFloat()*8.f, (rng.nextFloat()-0.4f)*3.f,
                    0.2f+rng.nextFloat()*0.8f, 0.5f+rng.nextFloat()*2.5f, c[0], c[1], c[2] };
            }
            const float flick=rng.nextFloat()<0.1f ? 0.2f : 1.f;
            constexpr int SP=90;
            for(int p=0;p<SP;++p){
                if((float)p/(float)SP>=s.len || p%5==4) continue;
                ptVerts.insert(ptVerts.end(), { s.x+(float)p*0.09f, s.y, s.z, s.r*0.45f*flick, s.g*0.45f*flick, s.b*0.45f*flick });
            }
        }
    }
}

// Historique échantillonné comme le shader : u 0..1 = fréquence, âge en lignes (0 = la plus récente)
float GpuSpectroFxDisplay::levelAt(float u, float age) const
{
    const float sx=juce::jlimit(0.f, (float)(NX-1), u*(float)(NX-1));
    const int   x0=(int)sx, x1=juce::jmin(x0+1, NX-1);
    const float fx=sx-(float)x0;
    const float rp=(float)liveRow()-juce::jlimit(0.f, (float)(span()-1), age);
    const int   r0=(int)std::floor(rp);
    const float fr=rp-(float)r0;
    const int   i0=((r0%NZ)+NZ)%NZ, i1=(i0+1)%NZ;
    const float a=rows[i0][x0]+(rows[i0][x1]-rows[i0][x0])*fx;
    const float b=rows[i1][x0]+(rows[i1][x1]-rows[i1][x0])*fx;
    return a+(b-a)*fr;
}

// Hauteur de la scène sous (u, âge), mêmes formules que les shaders
float GpuSpectroFxDisplay::surfaceY(float u, float age, float t) const
{
    switch(style){
        case Style::Bars: {
            const int n=juce::jmax(1, bandsShown);
            const int k=juce::jlimit(0, n-1, (int)(u*(float)n));
            const int r=juce::jlimit(0, span()-1, juce::roundToInt(age));
            return 0.05f+std::pow(bandRows[((liveRow()-r)%NZ+NZ)%NZ][k], 1.35f)*5.4f;
        }
        case Style::Led: {
            const float H=4.4f, h=(0.07f+std::pow(levelAt(u, age), 2.4f))*H*1.25f;
            return juce::jmin(h, H)*0.92f;   // haut de la colonne de points
        }
        case Style::Ribbons: {
            const float wave=0.6f*std::sin(u*6.f+t*0.9f+age*0.3f)+0.3f*std::sin(u*13.f-t*1.3f+age*0.2f);
            return std::pow(levelAt(u, age), 1.2f)*3.4f+wave-1.6f;
        }
        case Style::Matrix:
            return std::pow(levelAt(u, age), 1.25f)*3.6f;
        case Style::Splash: {
            constexpr float du=1.f/128.f;
            const float l=(2.f*levelAt(u, age)+levelAt(u-du, age)+levelAt(u+du, age))*0.25f;
            return std::pow(l, 1.4f)*2.6f;
        }
    }
    return 0.f;
}

// Survol : rayon partant de la caméra à travers le pixel de la souris, avancé
// par pas jusqu'à passer sous la scène, puis affiné par dichotomie (comme le Cristal)
void GpuSpectroFxDisplay::pick(const Cam& cam, float frac, float t, int w, int h)
{
    if(!hoverOn.load()){ hitValid=false; return; }
    const float nx=(hoverX.load()-kReleaseFaderW)/(float)juce::jmax(1, w)*2.f-1.f, ny=1.f-hoverY.load()/(float)juce::jmax(1, h)*2.f;
    if(hoverY.load()<(float)titleBottom.load()){ hitValid=false; return; }
    V3 d(cam.dir[0]+cam.right[0]*nx*cam.tanHalfY*cam.aspect+cam.up[0]*ny*cam.tanHalfY,
         cam.dir[1]+cam.right[1]*nx*cam.tanHalfY*cam.aspect+cam.up[1]*ny*cam.tanHalfY,
         cam.dir[2]+cam.right[2]*nx*cam.tanHalfY*cam.aspect+cam.up[2]*ny*cam.tanHalfY);
    d=d.normalised();
    const V3 eye(cam.eye[0], cam.eye[1], cam.eye[2]);
    const auto& geo=kGeo[(int)style];
    const float last=(float)(span()-1);
    auto toGrid=[&](const V3& p, float& u, float& age){
        u=p.x/geo.W+0.5f; age=(float)span()-frac-(geo.z0-p.z)/rowDZ();
        return u>=0.f && u<=1.f && age>=0.f && age<=last;
    };
    auto above=[&](float s, float& u, float& age, bool& in){
        const V3 p=eye+d*s;
        in=toGrid(p, u, age);
        return !in || p.y>surfaceY(u, age, t);
    };
    float u=0.f, age=0.f, sPrev=0.f;
    bool in=false;
    for(float s=0.05f; s<90.f; s+=0.04f){
        if(!above(s, u, age, in)){
            float lo=sPrev, hi=s;
            for(int it=0; it<12; ++it){
                const float m=(lo+hi)*0.5f;
                bool inM=false; float um=0.f, am=0.f;
                if(above(m, um, am, inM)) lo=m; else hi=m;
            }
            const V3 p=eye+d*hi;
            toGrid(p, u, age);
            float lv;
            if(style==Style::Bars){
                const int n=juce::jmax(1, bandsShown), k=juce::jlimit(0, n-1, (int)(u*(float)n));
                const int r=juce::jlimit(0, span()-1, juce::roundToInt(age));
                lv=bandRows[((liveRow()-r)%NZ+NZ)%NZ][k];
                u=((float)(k*NX/n)+(float)(juce::jmax(k*NX/n+1, (k+1)*NX/n)-1))*0.5f/(float)(NX-1);   // centre de la bande
            } else lv=levelAt(u, age);
            hitU=u; hitAge=age; hitLevel=lv;
            hitPos[0]=p.x; hitPos[1]=p.y; hitPos[2]=p.z;
            hitValid=true;
            return;
        }
        sPrev=s;
    }
    hitValid=false;
}

void GpuSpectroFxDisplay::drawScene(const Cam& cam, float t, float frac, float dt, int pw, int ph)
{
    const int si=(int)style;
    Mat4 vp; std::copy(std::begin(cam.mvp), std::end(cam.mvp), vp.m);
    const V3 eye(cam.eye[0], cam.eye[1], cam.eye[2]);
    const float pxScale=(float)ph/330.f;   // tailles de points des aperçus (vue de ~330 px de haut)

    auto& sh=*sceneShader;
    sh.use();
    sh.setUniform("hist", 0);
    sh.setUniform("headRow", (float)liveRow());   // âge 0 = ligne en cours
    sh.setUniform("rowsTex", (float)NZ);
    sh.setUniform("frac", frac);
    sh.setUniform("span", (float)span());
    sh.setUniform("barDZ", rowDZ());
    sh.setUniform("time", t);
    sh.setUniform("pxScale", pxScale);
    sh.setUniformMat4("mvp", vp.m, 1, GL_FALSE);
    sh.setUniform("eye", eye.x, eye.y, eye.z);
    sh.setUniform("vpPx", (float)pw, (float)ph);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, histTex);
    glBindVertexArray(vaoScene);
    glDisable(GL_CULL_FACE);

    if(style==Style::Bars){
        const auto& lk=kLooks[si];
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, bandTex);
        sh.setUniform("bands", 1);
        sh.setUniform("nBands", (float)juce::jmax(1, bandsShown));
        sh.setUniform("fogCol", srgbToLin(lk.bg[0]), srgbToLin(lk.bg[1]), srgbToLin(lk.bg[2]));
        const int inst=juce::jmax(1, bandsShown)*span();
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
        // reflet puis sol voilé (sans écrire la profondeur), puis barres et sommets
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
        sh.setUniform("part", 1.f); glDrawArraysInstanced(GL_TRIANGLES, 0, sceneVerts, inst);
        sh.setUniform("part", 3.f); glDrawArrays(GL_TRIANGLES, 0, sceneVerts);
        glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        sh.setUniform("part", 0.f); glDrawArraysInstanced(GL_TRIANGLES, 0, sceneVerts, inst);
        sh.setUniform("part", 2.f); glDrawArraysInstanced(GL_TRIANGLES, 0, sceneVerts, inst);
        glActiveTexture(GL_TEXTURE0);
    } else if(style==Style::Splash){
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_BLEND);
        glDrawElements(GL_TRIANGLES, sceneIndices, GL_UNSIGNED_INT, nullptr);
    } else {
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);   // additif, comme les aperçus
        glDrawArrays(style==Style::Matrix ? GL_LINES : GL_POINTS, 0, sceneVerts);
    }

    // particules
    updateParticles(dt);
    if(!ptVerts.empty()){
        auto& ps=*pointShader;
        ps.use();
        ps.setUniformMat4("mvp", vp.m, 1, GL_FALSE);
        ps.setUniform("sizePx", 1.8f*pxScale);
        ps.setUniform("worldSize", style==Style::Splash ? 0.16f : 0.f);
        ps.setUniform("halfH", (float)ph*0.5f);
        ps.setUniform("roundPt", style==Style::Splash ? 1.f : 0.f);
        ps.setUniform("vpPx", (float)pw, (float)ph);
        glBindVertexArray(vaoPts);
        glBindBuffer(GL_ARRAY_BUFFER, vboPts);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(ptVerts.size()*sizeof(float)), ptVerts.data(), GL_STREAM_DRAW);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glDrawArrays(GL_POINTS, 0, (GLsizei)(ptVerts.size()/6));
        glDepthMask(GL_TRUE);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    // point survolé (lu dans le calque 2D), comme le Cristal
    if(hoverOn.load() && hitValid.load()){
        const auto acc=Col::accent;
        const float mark[6]={ hitPos[0], hitPos[1], hitPos[2],
                              std::pow(acc.getFloatRed(), 2.2f)*2.f, std::pow(acc.getFloatGreen(), 2.2f)*2.f, std::pow(acc.getFloatBlue(), 2.2f)*2.f };
        auto& ps=*pointShader;
        ps.use();
        ps.setUniformMat4("mvp", vp.m, 1, GL_FALSE);
        ps.setUniform("sizePx", 9.f*pxScale);
        ps.setUniform("worldSize", 0.f);
        ps.setUniform("roundPt", 1.f);
        ps.setUniform("vpPx", (float)pw, (float)ph);
        glBindVertexArray(vaoPts);
        glBindBuffer(GL_ARRAY_BUFFER, vboPts);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(mark), mark, GL_STREAM_DRAW);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDrawArrays(GL_POINTS, 0, 1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

void GpuSpectroFxDisplay::renderOpenGL()
{
    GLint prevVao=0, prevFbo=0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    const auto bg=Col::bg;
    glClearColor(bg.getFloatRed(), bg.getFloatGreen(), bg.getFloatBlue(), 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if(sceneShader==nullptr || compShader==nullptr || vaoScene==0) return;
    applyVsync();

    // horloge : TIME = durée de l'historique affiché ; HOLD : rien ne bouge,
    // le défilement reprendra au même point (comme le Cristal)
    const double now=juce::Time::getMillisecondCounterHiRes();
    if(lastFrameMs<=0.0) lastCaptureMs=lastFrameMs=now;
    const float dt=(float)juce::jlimit(0.0, 0.1, (now-lastFrameMs)*0.001);
    lastFrameMs=now;
    const double capMs=captureMs();
    const bool hold=frozen.load();
    if(hold) lastCaptureMs=now-(double)heldFrac*capMs;
    else {
        if(now-lastCaptureMs>4.0*capMs) lastCaptureMs=now-capMs;
        bool startNew=false;
        while(now-lastCaptureMs>=capMs){ lastCaptureMs+=capMs; commit(); startNew=true; }
        accumulate(startNew);
    }
    const float frac=(float)juce::jlimit(0.0, 1.0, (now-lastCaptureMs)/capMs);
    if(!hold) heldFrac=frac;
    const float dtAnim=hold ? 0.f : dt;
    animT+=dtAnim;
    const float t=animT;

    const float scale=renderingScale();
    const int pw=juce::jmax(1, juce::roundToInt(scale*(float)viewW.load())), ph=juce::jmax(1, juce::roundToInt(scale*(float)viewH.load()));
    const int offX=juce::roundToInt(scale*kReleaseFaderW);   // à droite du fader TIME
    if(!ensureTargets(pw, ph)) return;
    const auto cam=makeCam(yaw.load(), pitch.load(), zoom.load(), (float)pw/(float)ph);
    pick(cam, frac, t, viewW.load(), viewH.load());
    const auto& lk=kLooks[(int)style];

    // 1) scène, dans le tampon multi-échantillonné
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
    glViewport(0, 0, pw, ph);
    glClearColor(srgbToLin(lk.bg[0]), srgbToLin(lk.bg[1]), srgbToLin(lk.bg[2]), 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawScene(cam, t, frac, dtAnim, pw, ph);

    // 2) résolution de l'anticrénelage
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFbo);
    glBlitFramebuffer(0, 0, pw, ph, 0, 0, pw, ph, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // 3) lueur : passe claire à demi-résolution, flous à demi puis quart
    glBindVertexArray(vaoQuad);
    glActiveTexture(GL_TEXTURE0);
    const int hw=juce::jmax(1, pw/2), hh=juce::jmax(1, ph/2), qw=juce::jmax(1, pw/4), qh=juce::jmax(1, ph/4);
    auto pass=[&](unsigned int fbo, int w, int h, unsigned int srcTex){
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w, h);
        glBindTexture(GL_TEXTURE_2D, srcTex);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };
    brightShader->use();
    brightShader->setUniform("src", 0);
    brightShader->setUniform("threshold", lk.threshold);
    pass(blurFbo[0], hw, hh, sceneTex);
    blurShader->use();
    blurShader->setUniform("src", 0);
    blurShader->setUniform("dir", 1.f/(float)hw, 0.f);  pass(blurFbo[1], hw, hh, blurTex[0]);
    blurShader->setUniform("dir", 0.f, 1.f/(float)hh);  pass(blurFbo[0], hw, hh, blurTex[1]);
    for(int it=0; it<2; ++it){
        blurShader->setUniform("dir", 1.f/(float)hw, 0.f);  pass(blurFbo[2], qw, qh, it==0 ? blurTex[0] : blurTex[3]);
        blurShader->setUniform("dir", 0.f, 1.f/(float)qh);  pass(blurFbo[3], qw, qh, blurTex[2]);
    }

    // 4) composition dans la vue de JUCE, sous la ligne du titre
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glViewport(offX, 0, pw, ph);
    glEnable(GL_SCISSOR_TEST);
    glScissor(offX, 0, pw, juce::jmax(0, ph-juce::roundToInt(scale*(float)titleBottom.load())));
    compShader->use();
    compShader->setUniform("scene", 0);
    compShader->setUniform("bloomH", 1);
    compShader->setUniform("bloomQ", 2);
    compShader->setUniform("strength", lk.strength);
    compShader->setUniform("radius", lk.radius);
    compShader->setUniform("exposure", lk.exposure);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, sceneTex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, blurTex[0]);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, blurTex[3]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    for(int u : { 2, 1, 0 }){ glActiveTexture(GL_TEXTURE0+(GLenum)u); glBindTexture(GL_TEXTURE_2D, 0); }
    glDisable(GL_SCISSOR_TEST);

    // état rendu à JUCE (composition du calque 2D)
    glBindVertexArray((GLuint)prevVao);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(0);
}
