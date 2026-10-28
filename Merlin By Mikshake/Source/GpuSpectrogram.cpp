#include "GpuSpectrogram.h"
#include "PluginEditor.h"   // Col::, Neu::, Ui::

using namespace juce::gl;

//==============================================================================
// Shaders (GLSL 1.50 — profil OpenGL 3.2 core)
//
// Les hauteurs vivent dans une texture flottante NX x NZ utilisée comme anneau
// (une ligne = une capture) : le processeur graphique interpole lui-même entre
// les fréquences et entre les instants (filtrage linéaire), calcule les
// normales au pixel près et tout l'éclairage. Le CPU n'envoie qu'une ligne de
// 160 valeurs par image.
//
// age = nombre de captures écoulées (0 = la plus récente, -1 = ligne "live" en
// cours). Chaque rangée de sommets reste attachée à sa capture (une rangée par
// demi-ligne de données) et avance avec elle d'une fraction de ligne entre deux
// captures : le défilement est continu et une crête déjà dessinée ne change
// plus (rééchantillonner l'historique à chaque image la faisait onduler).
//==============================================================================
static const char* kMeshVS = R"(#version 150
in vec2 grid;                 // x : frequence 0..1, y : rang dans l'historique 0 (live) .. 1 (plus ancien)
uniform mat4  mvp;
uniform sampler2D heights;
uniform float headT, invNZ, ageSpan, frac, sScale, sOff, halfX, halfZ;
out vec3  vPos;
out float vAge, vU, vD, vH;

void main()
{
    // age fixe du sommet (pas d'une demi-capture) et place dans la profondeur,
    // en captures ; un sommet qui sort par un bord y reste, a l'age que ce
    // bord coupe a cet instant
    float a   = grid.y*(ageSpan + 1.0) - 1.0;
    float P   = a + frac;
    float Pc  = clamp(P, 0.0, ageSpan);
    float age = (P == Pc) ? a : Pc - frac;
    float h   = texture(heights, vec2(grid.x*sScale + sOff, headT - age*invNZ)).r;
    float d   = Pc/ageSpan;
    // temps : la ligne la plus recente nait au fond (z = -halfZ) et
    // l'historique avance vers la camera (z = +halfZ)
    vec3  p   = vec3((grid.x*2.0 - 1.0)*halfX, h*2.2, (d*2.0 - 1.0)*halfZ);
    vPos = p; vAge = age; vU = grid.x; vD = d; vH = h;
    gl_Position = mvp*vec4(p, 1.0);
}
)";

static const char* kMeshFS = R"(#version 150
in vec3  vPos;
in float vAge, vU, vD, vH;
out vec4 fragColour;
uniform sampler2D heights;
uniform float headT, invNZ, ageSpan, sScale, sOff, parity, gridPhase, halfX, halfZ;
uniform int   style;          // 0 terrain, 1 neon, 2 cristal
uniform vec3  eye, viewDir, bg, accent;
uniform vec3  hover;         // survol : u, age, actif (0/1)

const vec3 stops[8] = vec3[8](vec3(0.,0.,4.)/255.,     vec3(40.,11.,84.)/255.,  vec3(101.,21.,110.)/255.,
                              vec3(159.,42.,99.)/255., vec3(212.,72.,66.)/255., vec3(245.,125.,21.)/255.,
                              vec3(250.,193.,39.)/255.,vec3(252.,255.,164.)/255.);
vec3 inferno(float t)
{
    t = clamp(t, 0.0, 1.0)*7.0;
    int i = min(6, int(t));
    return mix(stops[i], stops[i+1], t - float(i));
}
float H(float u, float age) { return texture(heights, vec2(u*sScale + sOff, headT - age*invNZ)).r; }

// Cristal : vert (niveau le plus faible) -> jaune -> orange -> rouge, violet
// seulement tout en haut (-10 dB = 1.0)
vec3 crystalRamp(float t)
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

// trait anti-aliase tous les 'period' : coeur ~1.4 px + halo
float lineMask(float coord, out float halo)
{
    float w  = max(fwidth(coord), 1e-5);
    float px = abs(fract(coord + 0.5) - 0.5)/w;
    float dens = clamp(1.0 - (w - 0.18)*4.0, 0.0, 1.0);    // fondu quand les traits se serrent (fond)
    halo = exp(-px*px/24.0)*dens;
    return (1.0 - smoothstep(0.55, 1.45, px))*dens;
}

void main()
{
    float fade = 1.0 - clamp(vAge/64.0, 0.0, 1.0)*0.35;
    float fogK = clamp((dot(vPos - eye, viewDir) - 7.0)/8.0, 0.0, 1.0);   // brouillard en profondeur de vue

    // normale au pixel (differences finies dans la texture)
    float du = 1.0/159.0;
    float hx = (H(vU + du, vAge) - H(vU - du, vAge))*2.2/(4.0*halfX*du);
    float hz = (H(vU, vAge + 1.0) - H(vU, vAge - 1.0))*2.2/(2.0*(2.0*halfZ/ageSpan));
    vec3  n  = normalize(vec3(-hx, 1.0, -hz));
    vec3  V  = normalize(eye - vPos);
    if(dot(n, V) < 0.0) n = -n;

    vec3 col;
    if(style == 0){
        // A - terrain eclaire : cle blanche haut-gauche, contre-jour turquoise
        vec3  base = inferno(vH)*fade;
        vec3  L1 = normalize(vec3(-4.0, 6.0, 5.0)), L2 = normalize(vec3(5.0, 2.0, -6.0));
        float d1 = max(dot(n, L1), 0.0), d2 = max(dot(n, L2), 0.0);
        float sp = pow(max(dot(n, normalize(L1 + V)), 0.0), 28.0)*0.22;
        col = base*(0.35 + 1.1*d1) + base*vec3(0.40, 1.0, 0.87)*0.5*d2 + vec3(sp);
    } else if(style == 1){
        // B - neon : surface noire qui masque, quadrillage lumineux (choix du
        // user) : une ligne toutes les 2 captures (liee aux donnees, elle
        // recule avec elles) ET une ligne par frequence (64) + front (live)
        float halo, haloF, hT, hF;
        float m  = max(lineMask((vAge - parity)*0.5, hT), lineMask(vU*63.0, hF));
        halo = max(hT, hF);
        float wf = max(fwidth(vD), 1e-5);
        float pf = vD/wf;
        float mf = 1.0 - smoothstep(0.8, 1.8, pf);
        haloF = exp(-pf*pf/30.0);
        vec3 lc = inferno(0.3 + 0.7*vH)*(1.0 - clamp(vAge/64.0, 0.0, 1.0));
        vec3 fc = inferno(0.35 + 0.65*vH);
        col = vec3(0.008, 0.024, 0.024)
            + lc*(m*1.25 + halo*0.45)
            + fc*(mf*1.4 + haloF*0.6);
    } else {
        // C - cristal : Phong brillant, lumiere d'accent, grille fine. Lumieres
        // placees derriere le relief (pivotees d'un demi-tour) : contre-jour,
        // reflets sur les cretes (rendu de la vue retournee, en vue normale)
        vec3  base = crystalRamp(vH)*fade;   // degrade vert -> violet (demande du user)
        vec3  L1 = normalize(vec3(3.0, 7.0, -6.0));
        vec3  Lp = vec3(-3.0, 3.0, -4.0) - vPos;
        float dp = length(Lp); Lp /= dp;
        float att = clamp(1.0 - dp/20.0, 0.0, 1.0);
        float d1 = max(dot(n, L1), 0.0), d2 = max(dot(n, Lp), 0.0);
        float s1 = pow(max(dot(n, normalize(L1 + V)), 0.0), 90.0);
        float s2 = pow(max(dot(n, normalize(Lp + V)), 0.0), 90.0)*att;
        col = base*(0.4 + d1) + base*accent*0.8*att*d2
            + vec3(0.60, 1.0, 0.93)*(s1*0.75 + s2*0.6);
        float hu, hr;
        float g = max(lineMask(vU*79.0, hu), lineMask(vAge - parity, hr));
        col *= 1.0 - 0.22*g;
    }

    // plateau 0 : le bas du relief (niveau au plus bas) devient un sol plat
    // bleute et quadrille, plus clair vers l'avant ; une ligne de temps toutes
    // les 4 captures, attachee a sa capture (gridPhase suit le compteur de
    // captures) : elle avance avec le relief sans jamais sauter en arriere
    float pl = 1.0 - smoothstep(0.0, 0.05, vH);
    if(pl > 0.0){
        float gu, ga;
        float gp = max(lineMask(vU*32.0, gu), lineMask((vAge - gridPhase)*0.25, ga));
        vec3 plc = mix(vec3(0.18, 0.17, 0.40), vec3(0.42, 0.44, 0.80), vD);
        plc = mix(plc, vec3(0.78, 0.82, 1.0), gp*0.35);
        col = mix(col, plc, pl);
    }

    // curseur 3D : frequence (de l'avant au fond) + instant survoles, point au croisement
    if(hover.z > 0.5){
        float pu = abs(vU - hover.x)/max(fwidth(vU), 1e-6);
        float pa = abs(vAge - hover.y)/max(fwidth(vAge), 1e-6);
        float ln = max(1.0 - smoothstep(0.6, 1.6, pu), 1.0 - smoothstep(0.6, 1.6, pa));
        col = mix(col, accent, ln*0.8);
        col = mix(col, vec3(1.0), 1.0 - smoothstep(2.5, 4.0, length(vec2(pu, pa))));
    }
    fragColour = vec4(mix(col, bg, fogK), 1.0);
}
)";

//==============================================================================
// petites matrices (colonne majeure, comme OpenGL)
//==============================================================================
namespace
{
    struct V3 { float x, y, z; };
    V3 sub(V3 a, V3 b)   { return { a.x-b.x, a.y-b.y, a.z-b.z }; }
    V3 cross(V3 a, V3 b) { return { a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x }; }
    float dot(V3 a, V3 b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
    V3 norm(V3 a)        { const float l=std::sqrt(dot(a,a)); return { a.x/l, a.y/l, a.z/l }; }

    void mul(const float* a, const float* b, float* out)   // out = a*b
    {
        for(int c=0;c<4;++c)
            for(int r=0;r<4;++r){
                float s=0.f;
                for(int k=0;k<4;++k) s+=a[k*4+r]*b[c*4+k];
                out[c*4+r]=s;
            }
    }

    constexpr int kGX = 319;   // sommets : 2 par colonne de données
}

GpuSpectrogramDisplay::Camera GpuSpectrogramDisplay::makeCamera(float yawRad, float pitchRad, float aspect)
{
    // orbite autour du centre du relief (comme la preview)
    const float r=9.5f;
    const V3 eyeP { std::sin(yawRad)*r*std::cos(pitchRad), r*std::sin(pitchRad)+0.6f, std::cos(yawRad)*r*std::cos(pitchRad) };
    const V3 centre { 0.f, 0.6f, 0.f }, up { 0.f, 1.f, 0.f };
    const V3 f=norm(sub(centre, eyeP)), s=norm(cross(f, up)), u=cross(s, f);
    const float view[16] = {  s.x,  u.x, -f.x, 0.f,
                              s.y,  u.y, -f.y, 0.f,
                              s.z,  u.z, -f.z, 0.f,
                             -dot(s,eyeP), -dot(u,eyeP), dot(f,eyeP), 1.f };

    // Cadrage selon le format du slot : le relief s'élargit avec la vue pour
    // que le bord avant (à ~7 unités de la caméra) en occupe ~92 % ; un slot
    // étroit garde une largeur minimale et élargit plutôt le champ (34° de
    // champ vertical sinon, comme la preview).
    aspect=juce::jmax(0.2f, aspect);
    const float t17=std::tan(juce::degreesToRadians(17.f));
    const float fit=0.92f*7.f;
    const float wFit=juce::jlimit(3.f, 14.f, fit*t17*aspect);   // longueur à l'horizontale de l'écran
    const float tanHalfY=juce::jmax(t17, wFit/(fit*aspect));
    // Vue tournée (demande du user) : l'axe qui passe à l'horizontale de
    // l'écran prend la longueur du cadre, l'autre garde la profondeur d'origine
    // (3). De face rien ne change ; à un quart de tour, le temps remplit la
    // largeur au lieu d'une bande étroite au milieu.
    const float ca=std::abs(std::cos(yawRad)), sa=std::abs(std::sin(yawRad));
    const float halfX=ca*wFit + sa*3.f;
    const float halfZ=ca*3.f  + sa*wFit;
    const float zn=0.1f, zf=60.f, fy=1.f/tanHalfY;
    const float proj[16] = { fy/aspect, 0.f, 0.f, 0.f,
                             0.f, fy, 0.f, 0.f,
                             0.f, 0.f, (zf+zn)/(zn-zf), -1.f,
                             0.f, 0.f, 2.f*zf*zn/(zn-zf), 0.f };
    Camera cam;
    mul(proj, view, cam.mvp);

    // Le relief va jusqu'au bas du cadre : le bord le plus proche du bas de
    // l'image (bord avant de face, bord latéral en vue tournée) passe juste
    // sous le bas grâce à un décalage vertical de l'objectif. Avant, une bande
    // noire restait sous le relief qui défile.
    float ys[4]; int nys=0;
    for(const float x : { -halfX, halfX })
        for(const float z : { -halfZ, halfZ }){
            const float* m=cam.mvp;
            const float cy=m[1]*x + m[9]*z + m[13];   // y clip du coin (x, 0, z)
            const float cw=m[3]*x + m[11]*z + m[15];
            ys[nys++] = cw>0.01f ? cy/cw : -2.f;
        }
    std::sort(ys, ys+4);
    cam.shiftY=juce::jlimit(0.f, 0.6f, ys[1]+1.02f);   // le plus haut des deux coins du bas
    if(cam.shiftY>0.f)
        for(int c=0;c<4;++c) cam.mvp[c*4+1]-=cam.shiftY*cam.mvp[c*4+3];
    cam.eye[0]=eyeP.x; cam.eye[1]=eyeP.y; cam.eye[2]=eyeP.z;
    cam.halfX=halfX; cam.halfZ=halfZ;
    cam.dir[0]=f.x; cam.dir[1]=f.y; cam.dir[2]=f.z;
    cam.right[0]=s.x; cam.right[1]=s.y; cam.right[2]=s.z;
    cam.up[0]=u.x;    cam.up[1]=u.y;    cam.up[2]=u.z;
    cam.tanHalfY=tanHalfY; cam.aspect=aspect;
    return cam;
}

//==============================================================================
static_assert(PsdLogMap::N==160, "le maillage suit les 160 points de PsdLogMap");

// Vues « droites » (de face, quart de tour…) : un angle à moins de tolRad d'un
// multiple de 90° y est ramené pile — plan bien parallèle à l'écran, sans viser
// au pixel près (le user voyait le sol légèrement de biais, 0,75° de trop)
static float snapYaw(float y, float tolRad)
{
    constexpr float twoPi=juce::MathConstants<float>::twoPi, quarter=juce::MathConstants<float>::halfPi;
    y=std::remainder(y, twoPi);
    const float q=quarter*std::round(y/quarter);
    return std::abs(y-q)<tolRad ? std::remainder(q, twoPi) : y;
}
static constexpr float kSnapAtLoad=juce::degreesToRadians(5.f), kSnapDrag=juce::degreesToRadians(2.5f);

GpuSpectrogramDisplay::GpuSpectrogramDisplay(LeakAudioProcessor& p, Style s)
    : GpuModuleBase(p), style(s)
{
    // fader TIME : 1 à 20 s (milieu de course 4 s), 2.1 s par défaut
    timeFader=std::make_unique<ReleaseFader>();
    timeFader->configure("TIME", 1.f, 20.f, 4.f, 2.1f);
    timeSecA=timeRef();
    timeFader->setValue(timeRef(), juce::dontSendNotification);
    timeFader->onValueChange=[this](float v){ timeRef()=v; timeSecA=v; repaint(); };   // échelle de temps
    addAndMakeVisible(*timeFader);
    holdBtn=std::make_unique<NeuAnimButton>("HOLD");
    holdBtn->setClickingTogglesState(true);
    holdBtn->onClick=[this]{ frozen=holdBtn->getToggleState(); };
    addAndMakeVisible(*holdBtn);
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    lastCaptureMs=juce::Time::getMillisecondCounterHiRes();
    logMap.setExtendedAxis(true);   // axe jusqu'à Nyquist au-delà de 48 kHz
    syncFromProcessor();            // vue mémorisée (ramenée pile sur une vue droite si elle en est tout près)
}

GpuSpectrogramDisplay::~GpuSpectrogramDisplay()
{
    detachGL();
}

void GpuSpectrogramDisplay::syncFromProcessor()
{
    timeSecA=timeRef();
    timeFader->setValue(timeRef(), juce::dontSendNotification);
    yawRef()=snapYaw(yawRef(), kSnapAtLoad);
    yaw=yawRef();
    pitch=juce::jlimit(0.12f, 1.1f, pitchRef());
    repaint();
}

void GpuSpectrogramDisplay::uiTick()
{
    // lecture au survol : repeint le calque 2D seulement si le point a changé
    const bool v=hoverOn.load() && hitValid.load();
    const float u=hitU.load(), a=hitAge.load(), h=hitH.load();
    if(v!=shownValid || (v && (std::abs(u-shownU)>1e-4f || std::abs(a-shownAge)>0.05f || std::abs(h-shownH)>1e-3f))){
        shownValid=v; shownU=u; shownAge=a; shownH=h;
        repaint();
    }
}

void GpuSpectrogramDisplay::mouseMove(const juce::MouseEvent& e)
{
    hoverX=e.position.x; hoverY=e.position.y;
    hoverOn=true;
}
void GpuSpectrogramDisplay::mouseExit(const juce::MouseEvent&)
{
    hoverOn=false;
}

float& GpuSpectrogramDisplay::yawRef()
{
    return style==Style::Terrain ? processor.gpuTerrainYaw : style==Style::Neon ? processor.gpuNeonYaw : processor.gpuCrystalYaw;
}
float& GpuSpectrogramDisplay::pitchRef()
{
    return style==Style::Terrain ? processor.gpuTerrainPitch : style==Style::Neon ? processor.gpuNeonPitch : processor.gpuCrystalPitch;
}

float& GpuSpectrogramDisplay::timeRef()
{
    switch(style){
        case Style::Terrain: return processor.gpuTerrainTimeSec;
        case Style::Neon:    return processor.gpuNeonTimeSec;
        case Style::Crystal: break;
    }
    return processor.gpuCrystalTimeSec;
}

const char* GpuSpectrogramDisplay::title() const
{
    switch(style){
        case Style::Terrain: return "SPECTROGRAM GPU - TERRAIN";
        case Style::Neon:    return "SPECTROGRAM GPU - NEON";
        case Style::Crystal: return "SPECTROGRAM GPU - CRYSTAL";
    }
    return "";
}

void GpuSpectrogramDisplay::resized()
{
    // la scène 3D occupe la zone à droite du fader TIME
    viewW=juce::jmax(1, getWidth()-(int)kReleaseFaderW);
    viewH=juce::jmax(1, getHeight());
    const int top=juce::roundToInt(24.f*Ui::textScale);
    timeFader->setBounds(2, top, (int)kReleaseFaderW-4, juce::jmax(10, getHeight()-top-8));
    // HOLD dans l'en-tête, à droite
    const int hh=juce::jlimit(16, 22, juce::roundToInt(19.f*Ui::textScale));
    const int hw=juce::roundToInt(46.f*Ui::textScale);
    holdBtn->setBounds(getWidth()-hw-8, 2, hw, hh);
}

//==============================================================================
// Interface : orbite à la souris (double-clic = vue d'origine)
//==============================================================================
void GpuSpectrogramDisplay::mouseDown(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown()) return;   // laisse remonter pour le menu de slot
    yawAtDrag=yaw.load();
    pitchAtDrag=pitch.load();
    dragFromTitle=e.position.y<Ui::titleStripHeight();   // titre : détachement, pas d'orbite
}
void GpuSpectrogramDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown() || dragFromTitle) return;
    yaw   = snapYaw(yawAtDrag + (float)e.getDistanceFromDragStartX()*0.006f, kSnapDrag);   // aimant sur les vues droites
    pitch = juce::jlimit(0.12f, 1.1f, pitchAtDrag + (float)e.getDistanceFromDragStartY()*0.004f);
    yawRef()=yaw.load(); pitchRef()=pitch.load();   // vue retenue
    hoverX=e.position.x; hoverY=e.position.y;
    repaint();   // repères de fréquence
}
void GpuSpectrogramDisplay::mouseDoubleClick(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown()) return;
    yaw=0.f; pitch=kDefaultPitch;
    yawRef()=0.f; pitchRef()=kDefaultPitch;
    repaint();
}

// Calque 2D : composé par JUCE au-dessus du rendu GL (ou peint seul quand le
// rendu est suspendu). Les coins arrondis et le bord du verre de l'écran sont
// redessinés ici : le calque du châssis ne peut pas passer devant une fenêtre GL.
void GpuSpectrogramDisplay::paint(juce::Graphics& g)
{
    const int W=getWidth(), H=getHeight();
    if(W<40||H<40) return;
    const bool gl=isGlAttached();
    if(!gl) g.fillAll(Col::bg);

    // repères projetés sur le relief : fréquences le long du bord du fond,
    // temps le long du bord droit
    const float off=kReleaseFaderW, W3=juce::jmax(1.f, (float)W-off);   // zone 3D
    const auto cam=makeCamera(yaw.load(), pitch.load(), W3/(float)H);
    auto project=[&](float x, float y, float z, juce::Point<float>& out){
        const float* m=cam.mvp;
        const float cx=m[0]*x+m[4]*y+m[8]*z+m[12], cy=m[1]*x+m[5]*y+m[9]*z+m[13], cw=m[3]*x+m[7]*y+m[11]*z+m[15];
        if(cw<=0.01f) return false;
        out={ off+(cx/cw*0.5f+0.5f)*W3, (1.f-(cy/cw*0.5f+0.5f))*(float)H };
        return true;
    };
    // haut de l'axe : 20 kHz, ou la moitié de la fréquence d'échantillonnage au-delà de 48 kHz
    const float fTop=PsdLogMap::topHz(processor.getCurrentSampleRate());
    const float logSpan=std::log(fTop/20.f);
    g.setFont(Ui::iceland(9.f));
    SpectroReadout::drawFreqMarks(g, fTop, [&](float hz, juce::Point<float>& at){
            return project((std::log(hz/20.f)/logSpan*2.f-1.f)*cam.halfX, 0.f, -cam.halfZ-0.25f, at); },   // le long de la ligne la plus récente
        12.f*Ui::textScale, { off+4.f, 20.f, W3-8.f, (float)H-24.f });
    {
        const float T=juce::jmax(0.5f, timeSecA.load()), step=SpectroReadout::timeTickStep(T);
        g.setFont(Ui::iceland(8.5f));
        for(float t=0.f; t<=T+1e-3f; t+=step){
            juce::Point<float> p;
            if(!project(cam.halfX, 0.f, -cam.halfZ+t/T*2.f*cam.halfZ, p)) continue;   // 0 s au fond (la plus récente)
            if(p.y<22.f || p.y>(float)H-6.f || p.x<off) continue;
            const auto lbl=SpectroReadout::formatTimeTick(t, step);
            const float w=juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), lbl)+2.f;
            g.setColour(Col::textMid.withAlpha(0.6f));
            g.drawLine(p.x+2.f, p.y, p.x+6.f, p.y, 1.f);
            const float lx=juce::jmin(p.x+8.f, (float)W-w-4.f);
            SpectroReadout::drawShadowedText(g, lbl, { lx, p.y-6.f, w, 12.f }, juce::Justification::centredLeft, Col::textMid.withAlpha(0.85f));
        }
    }

    g.setFont(Ui::iceland(10.5f).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    Ui::drawTitleFit(g, TR(title()), { 6.f, 5.f, (float)juce::jmax(20, holdBtn->getX()-12), 13.f }, g.getCurrentFont());   // plutôt que "…"

    // lecture au survol (le point est marqué en 3D par le shader)
    if(gl && shownValid)
        SpectroReadout::draw(g, getLocalBounds().toFloat(), { hoverX.load(), hoverY.load() },
                             20.f*std::pow(PsdLogMap::topHz(processor.getCurrentSampleRate())/20.f, shownU), shownH*90.f-100.f,
                             shownAge*(float)captureMs()*0.001f);

    paintGlFailure(g);
    paintScreenFrame(g);
}

//==============================================================================
// Thread GL
//==============================================================================
void GpuSpectrogramDisplay::newOpenGLContextCreated()
{
    glFailed=false;
    meshShader=compileProgram(kMeshVS, kMeshFS);
    if(meshShader==nullptr) return;

    // grille statique (u, rang dans l'historique) : 2 rangées par capture, de
    // la ligne live (âge -1) à la plus ancienne affichée (âge NZ-3)
    const int kGZ=2*(NZ-2)+1;
    std::vector<float> verts; verts.reserve((size_t)kGX*kGZ*2);
    for(int z=0;z<kGZ;++z)
        for(int x=0;x<kGX;++x){ verts.push_back((float)x/(float)(kGX-1)); verts.push_back((float)z/(float)(kGZ-1)); }
    std::vector<GLuint> idx; idx.reserve((size_t)(kGX-1)*(kGZ-1)*6);
    for(int z=0;z<kGZ-1;++z)
        for(int x=0;x<kGX-1;++x){
            const GLuint a=(GLuint)(z*kGX+x), b=a+1, c=a+(GLuint)kGX, d=c+1;
            idx.insert(idx.end(), { a, c, b, b, c, d });
        }
    numTriIndices=(int)idx.size();

    GLint prevVao=0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vboGrid);
    glBindBuffer(GL_ARRAY_BUFFER, vboGrid);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size()*sizeof(float)), verts.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &iboTris);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTris);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idx.size()*sizeof(GLuint)), idx.data(), GL_STATIC_DRAW);
    const GLint loc=glGetAttribLocation(meshShader->getProgramID(), "grid");
    if(loc>=0){
        glEnableVertexAttribArray((GLuint)loc);
        glVertexAttribPointer((GLuint)loc, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), nullptr);
    }
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // texture des hauteurs (anneau) — reprend l'historique déjà capturé
    glGenTextures(1, &heightTex);
    glBindTexture(GL_TEXTURE_2D, heightTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, NX, NZ, 0, GL_RED, GL_FLOAT, rows);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    onContextCreated();
}

void GpuSpectrogramDisplay::openGLContextClosing()
{
    if(vboGrid)  { glDeleteBuffers(1, &vboGrid);  vboGrid=0; }
    if(iboTris)  { glDeleteBuffers(1, &iboTris);  iboTris=0; }
    if(vao)      { glDeleteVertexArrays(1, &vao); vao=0; }
    if(heightTex){ glDeleteTextures(1, &heightTex); heightTex=0; }
    meshShader.reset();
}

void GpuSpectrogramDisplay::captureLive()
{
    if(!logMap.read(processor, liveDb)) return;
    float* r=rows[(head+1)%NZ];   // ligne "live", validée à la prochaine capture
    for(int p=0;p<NX;++p)
        r[p]=juce::jlimit(0.f,1.f,(liveDb[p]+100.f)/90.f);   // -100 dB -> 0, -10 dB -> 1
}

// Même échantillonnage que le shader (texture filtrée en bilinéaire, anneau
// en T) : u 0..1 = fréquence, age en captures (-1 = ligne live).
float GpuSpectrogramDisplay::sampleH(float u, float age) const
{
    const float sx=juce::jlimit(0.f, (float)(NX-1), u*(float)(NX-1));
    const int   x0=(int)sx, x1=juce::jmin(x0+1, NX-1);
    const float fx=sx-(float)x0;
    const float rp=(float)head-juce::jlimit(-1.f, ageSpan(), age);
    const int   r0=(int)std::floor(rp);
    const float fr=rp-(float)r0;
    const int   i0=((r0%NZ)+NZ)%NZ, i1=(i0+1)%NZ;
    const float a=rows[i0][x0]+(rows[i0][x1]-rows[i0][x0])*fx;
    const float b=rows[i1][x0]+(rows[i1][x1]-rows[i1][x0])*fx;
    return a+(b-a)*fr;
}

// Survol : rayon partant de la caméra à travers le pixel de la souris, avancé
// par pas jusqu'à passer sous le relief, puis affiné par dichotomie.
void GpuSpectrogramDisplay::pick(const Camera& cam, float frac, int w, int h)
{
    if(!hoverOn.load()){ hitValid=false; return; }
    const float nx=(hoverX.load()-kReleaseFaderW)/(float)w*2.f-1.f, ny=1.f-hoverY.load()/(float)h*2.f;
    const float sx=nx*cam.tanHalfY*cam.aspect, sy=(ny+cam.shiftY)*cam.tanHalfY;   // + décalage de l'objectif
    float d[3];
    for(int k=0;k<3;++k) d[k]=cam.dir[k]+cam.right[k]*sx+cam.up[k]*sy;
    const float len=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    for(auto& c : d) c/=len;

    // hauteur du relief sous un point (x, z) ; < 0 hors du relief
    auto surface=[&](float x, float z, float& u, float& age){
        if(std::abs(x)>cam.halfX || std::abs(z)>cam.halfZ) return -1.f;
        u=(x/cam.halfX+1.f)*0.5f;
        age=(z+cam.halfZ)/(2.f*cam.halfZ)*ageSpan()-frac;   // la plus récente au fond (z = -halfZ)
        return sampleH(u, age)*2.2f;
    };
    float u=0.f, age=0.f;
    const float step=0.03f;
    float tPrev=0.f;
    for(float t=step; t<60.f; t+=step){
        const float px=cam.eye[0]+d[0]*t, py=cam.eye[1]+d[1]*t, pz=cam.eye[2]+d[2]*t;
        if(py<-0.01f) break;   // sous le sol sans rien toucher
        const float hs=surface(px, pz, u, age);
        if(hs>=0.f && py<=hs){
            float lo=tPrev, hi=t;
            for(int it=0; it<10; ++it){
                const float m=(lo+hi)*0.5f;
                const float hm=surface(cam.eye[0]+d[0]*m, cam.eye[2]+d[2]*m, u, age);
                if(hm>=0.f && cam.eye[1]+d[1]*m<=hm) hi=m; else lo=m;
            }
            surface(cam.eye[0]+d[0]*hi, cam.eye[2]+d[2]*hi, u, age);
            hitU=u; hitAge=age; hitH=sampleH(u, age);
            hitValid=true;
            return;
        }
        tPrev=t;
    }
    hitValid=false;
}

void GpuSpectrogramDisplay::renderOpenGL()
{
    const auto bg=Col::bg;
    glClearColor(bg.getFloatRed(), bg.getFloatGreen(), bg.getFloatBlue(), 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if(meshShader==nullptr || vao==0) return;

    applyVsync();

    // capture : la ligne live devient la plus récente, une nouvelle ligne live
    // prend la place de la plus ancienne
    const double now=juce::Time::getMillisecondCounterHiRes();
    const double capMs=captureMs();
    const bool hold=frozen.load();
    if(hold){
        // HOLD : rien ne bouge ; le défilement reprendra au même point
        lastCaptureMs=now-(double)heldFrac*capMs;
    } else {
        if(now-lastCaptureMs>=capMs){
            lastCaptureMs = now-lastCaptureMs > 4.0*capMs ? now : lastCaptureMs+capMs;
            head=(head+1)%NZ;
            ++captureCount;
        }
        captureLive();
    }
    const float frac=(float)juce::jlimit(0.0, 1.0, (now-lastCaptureMs)/capMs);
    if(!hold) heldFrac=frac;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, heightTex);
    const int liveRow=(head+1)%NZ;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, liveRow, NX, 1, GL_RED, GL_FLOAT, rows[liveRow]);

    const float scale=(float)context.getRenderingScale();
    const int w=viewW.load(), h=viewH.load();
    glViewport(juce::roundToInt(scale*kReleaseFaderW), 0, juce::roundToInt(scale*(float)w), juce::roundToInt(scale*(float)h));   // à droite du fader

    const auto cam=makeCamera(yaw.load(), pitch.load(), (float)w/(float)h);
    const auto acc=Col::accent;
    pick(cam, frac, w, h);
    const bool showHover=hoverOn.load() && hitValid.load();

    meshShader->use();
    meshShader->setUniform("heights", 0);
    meshShader->setUniform("headT", ((float)head+0.5f)/(float)NZ);
    meshShader->setUniform("invNZ", 1.f/(float)NZ);
    meshShader->setUniform("ageSpan", (float)(NZ-3));
    meshShader->setUniform("frac", frac);
    meshShader->setUniform("sScale", (float)(NX-1)/(float)NX);
    meshShader->setUniform("sOff", 0.5f/(float)NX);
    meshShader->setUniform("parity", (float)(captureCount & 1));
    meshShader->setUniform("gridPhase", (float)(captureCount & 3));   // lignes du plateau liées aux captures
    meshShader->setUniform("halfX", cam.halfX);
    meshShader->setUniform("halfZ", cam.halfZ);
    meshShader->setUniform("style", (GLint)style);
    meshShader->setUniform("eye", cam.eye[0], cam.eye[1], cam.eye[2]);
    meshShader->setUniform("viewDir", cam.dir[0], cam.dir[1], cam.dir[2]);
    meshShader->setUniform("bg", bg.getFloatRed(), bg.getFloatGreen(), bg.getFloatBlue());
    meshShader->setUniform("accent", acc.getFloatRed(), acc.getFloatGreen(), acc.getFloatBlue());
    meshShader->setUniformMat4("mvp", cam.mvp, 1, GL_FALSE);
    meshShader->setUniform("hover", hitU.load(), hitAge.load(), showHover ? 1.f : 0.f);

    GLint prevVao=0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glBindVertexArray(vao);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);

    // (plus de reflet au sol sous le Cristal : ce relief inversé pouvait se
    // lire comme un second spectre)
    glDisable(GL_BLEND);
    beginSampleShading();
    glDrawElements(GL_TRIANGLES, numTriIndices, GL_UNSIGNED_INT, nullptr);
    endSampleShading();

    // état rendu à JUCE (composition du calque 2D)
    glBindVertexArray((GLuint)prevVao);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}
