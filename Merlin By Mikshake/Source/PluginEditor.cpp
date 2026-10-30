#include "PluginEditor.h"
#include "SystemLoopback.h"   // WindowsVolume (réglage de la barre du bas)
#include <cmath>

//==============================================================================
// Band labels
//==============================================================================
// (+ bandes ultrasoniques, affichées selon la fréquence d'échantillonnage)
const char* SpectrumDisplayLeak::BAND_LABELS_15[LeakAudioProcessor::N_BANDS_15+LeakAudioProcessor::N_EXTRA_15]={
    "25","40","63","100","160","250","400","630",
    "1k","1k6","2k5","4k","6k3","10k","16k",
    "25k","40k","63k"
};
const char* SpectrumDisplayLeak::BAND_LABELS_31[LeakAudioProcessor::N_BANDS_31+LeakAudioProcessor::N_EXTRA_31]={
    "20","25","31.5","40","50","63","80","100","125","160","200","250","315","400","500",
    "630","800","1k","1k25","1k6","2k","2k5","3k15","4k","5k","6k3","8k","10k","12k5","16k","20k",
    "25k","31k5","40k","50k","63k","80k"
};
const char* SpectrumDisplayLeak::BAND_LABELS_62[LeakAudioProcessor::N_BANDS_62+LeakAudioProcessor::N_EXTRA_62]={
    "20","22.4","25","28","31.5","35.5","40","45","50","56","63","71",
    "80","90","100","112","125","140","160","180","200","224","250","280",
    "315","355","400","450","500","560","630","710","800","900","1k","1k12",
    "1k25","1k4","1k6","1k8","2k","2k24","2k5","2k8","3k15","3k55","4k","4k5",
    "5k","5k6","6k3","7k1","8k","9k","10k","11k2","12k5","14k","16k","18k","20k","22k",
    "25k","28k","31k5","35k5","40k","45k","50k","56k","63k","71k","80k","90k"
};

//==============================================================================
// Theme — palettes d'accent sélectionnables depuis le panneau Settings
//==============================================================================
namespace Theme {
    const Palette palettes[NUM_PALETTES] = {
        { 0xFF00FFB2, 0xFF0B3030, "TEAL"   },
        { 0xFFFFA000, 0xFF2A200A, "AMBER"  },
        { 0xFF00B2FF, 0xFF0A2030, "BLUE"   },
        { 0xFFB266FF, 0xFF201030, "PURPLE" },
        { 0xFFFF3355, 0xFF300A10, "RED"    },
    };
    void applyCustom(const CustomColors& c)
    {
        Col::accent  = juce::Colour(c.accent);
        Col::midCol  = juce::Colour(c.accent);
        Col::grid    = juce::Colour(c.grid);
        Col::gridCtr = Col::grid.brighter(0.55f);
        Col::border  = Col::grid.brighter(0.25f);
        Col::bg      = juce::Colour(c.bg).withAlpha(1.0f);   // opaque : les modules sont déclarés opaques
        Col::panel   = Col::bg.brighter(0.03f);
        Col::knobDim = Col::bg.brighter(0.10f);
        Col::textHi  = juce::Colour(c.text);
        Col::textMid = Col::textHi.interpolatedWith(Col::bg, 0.35f);
        Col::sideCol = juce::Colour(c.side);
        Col::spectroLow  = juce::Colour(c.spectroLow);
        Col::spectroMid  = juce::Colour(c.spectroMid);
        Col::spectroHigh = juce::Colour(c.spectroHigh);
        if(Col::paper){
            // écrans Papier (aperçu validé, variante A) : blanc cassé, grille
            // gris-bleu fine, encre ardoise ; accent et Side tirés des couleurs choisies
            Col::bg      = juce::Colour(0xFFF8FAFC);
            Col::panel   = juce::Colour(0xFFF0F3F7);
            Col::grid    = juce::Colour(0xFFE0E5EB);
            Col::gridCtr = juce::Colour(0xFFC6CFD9);
            Col::border  = juce::Colour(0xFFCBD3DD);
            Col::knobDim = juce::Colour(0xFFE4E9EF);
            Col::textHi  = juce::Colour(0xFF26303B);
            Col::textMid = juce::Colour(0xFF7C8898);
            Col::accent  = Col::ink(juce::Colour(c.accent));
            Col::midCol  = Col::accent;
            Col::sideCol = Col::ink(juce::Colour(c.side));
        }
        ++Col::generation;
    }
}

juce::Colour Col::ink(juce::Colour c)
{
    if(!paper) return c;
    float h, s, b; c.getHSB(h, s, b);
    if(s<0.15f)   // gris / blanc : la lumière devient de l'encre
        return juce::Colour::fromHSV(h, s, juce::jlimit(0.16f, 0.55f, 1.f-b*0.82f), c.getFloatAlpha());
    // teinte gardée, assez sombre pour se lire sur du blanc (rouges, oranges
    // et jaunes un peu moins, sinon ils tournent au brun)
    const float maxB=(h<0.20f || h>0.95f) ? 0.86f : 0.66f;
    return juce::Colour::fromHSV(h, juce::jmax(s, 0.72f), juce::jmin(b, maxB), c.getFloatAlpha());
}

//==============================================================================
void draw3DButtonL(juce::Graphics& g,juce::Rectangle<float> b,bool isDown,bool isOn,
                   juce::Colour face,juce::Colour lit)
{
    const float r=4.f;
    g.setColour(juce::Colour(0xFF000000).withAlpha(Col::paper ? 0.16f : 0.5f));
    g.fillRoundedRectangle(b.translated(0.f,isDown?0.5f:1.5f),r);
    juce::ColourGradient grad=isDown
        ?juce::ColourGradient(face.darker(0.55f),b.getX(),b.getY(),face.darker(0.2f),b.getX(),b.getBottom(),false)
        :juce::ColourGradient(face.brighter(0.22f),b.getX(),b.getY(),face.darker(0.28f),b.getX(),b.getBottom(),false);
    g.setGradientFill(grad);g.fillRoundedRectangle(b,r);
    if(!isDown){g.setColour(juce::Colours::white.withAlpha(Col::paper ? 0.55f : 0.09f));g.fillRoundedRectangle(b.withHeight(b.getHeight()*0.42f),r);}
    if(isOn){g.setColour(lit.withAlpha(0.85f));g.drawRoundedRectangle(b.reduced(0.5f),r,1.2f);}
    else{g.setColour(Col::border);g.drawRoundedRectangle(b.reduced(0.5f),r,0.8f);}
}

// Cartes neumorphiques : saillie autour de chaque écran (module), lunette
// sombre (Neu::bezelW) dans la carte, rayon des coins de l'écran.
static constexpr float kCardPad      = 7.f;
static constexpr float kScreenRadius = Neu::screenRadius;

//==============================================================================
// SpectroReadout — bulle de lecture au survol des spectrogrammes
//==============================================================================
namespace SpectroReadout {
float timeTickStep(float span)
{
    return span<=2.5f ? 0.5f : span<=6.f ? 1.f : span<=12.f ? 2.f : 5.f;
}

juce::String formatTimeTick(float sec, float step)
{
    if(sec<=0.001f) return "0 s";
    return "-"+(step<1.f ? juce::String(sec,1) : juce::String(juce::roundToInt(sec)))+" s";
}

}   // namespace SpectroReadout (rouvert juste après)

void Ui::drawTitleDecor(juce::Graphics& g, float W, float y, float endX)
{
    if(titleStyle==0) return;
    const float x0=8.f, x1=endX>0.f ? endX : W-8.f;
    if(titleStyle==1){
        juce::ColourGradient line(Col::accent, x0, 0.f, Col::accent.withAlpha(0.f), x1, 0.f, false);
        line.addColour(0.45, Col::accent.withAlpha(0.33f));
        g.setGradientFill(line);
        g.fillRect(x0, y, x1-x0, 1.f);
    } else {
        g.setColour(Col::light(0.05f));
        g.fillRect(0.f, 0.f, W, y);
        g.setColour(Col::light(Col::paper ? 0.22f : 0.16f));
        g.fillRect(0.f, y, endX>0.f ? endX : W, 1.f);
    }
}

float Ui::titleEndBeforeRing(float cx, float cy, float r)
{
    const float dy=cy-kTitleLineY;
    if(dy>=r+2.f) return 0.f;
    return juce::jmax(40.f, cx-std::sqrt(juce::jmax(0.f, r*r-dy*dy))-6.f);
}

void Ui::drawCorrRing(juce::Graphics& g, float cx, float cy, float r, float corr, float height)
{
    const float pi=juce::MathConstants<float>::pi;
    const auto angle=[pi](float c){ return pi-juce::jlimit(-1.f, 1.f, c)*pi*0.5f; };   // 0 en haut, sens horaire : -1 à gauche, 0 en bas, +1 à droite
    auto stroke=[&](float from, float to, float width){
        juce::Path arc; arc.addCentredArc(cx, cy, r, r, 0.f, from, to, true);
        g.strokePath(arc, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    g.setColour(Col::light(Col::paper ? 0.18f : 0.14f));
    stroke(pi*0.5f, pi*1.5f, 3.2f);

    if(corrStyle==2){
        g.setColour(Col::textMid);
        for(int k=-4; k<=4; ++k){
            const float a=angle((float)k*0.25f);
            const bool big=k%4==0;
            const float r0=r-(big ? 5.6f : 3.2f), r1=r+(big ? 5.6f : 3.2f);
            g.drawLine(cx+std::sin(a)*r0, cy-std::cos(a)*r0, cx+std::sin(a)*r1, cy-std::cos(a)*r1, big ? 1.3f : 0.8f);
        }
        g.setFont(Ui::iceland(7.5f));
        const juce::Rectangle<float> tag(0.f, 0.f, 22.f, 12.f);
        if(cy+r+20.f<height) g.drawText("0", tag.withCentre({ cx, cy+r+13.6f }), juce::Justification::centred, false);
        g.drawText("-1", tag.withPosition(cx-r+2.f, cy+7.f), juce::Justification::centredLeft, false);
        g.drawText("+1", tag.withPosition(cx+r-24.f, cy+7.f), juce::Justification::centredRight, false);
    }

    const auto col=corr>=0.f ? Col::accent.interpolatedWith(Col::ink(juce::Colour(0xFFFFD200)), 1.f-corr) : Col::ink(juce::Colour(0xFFFF5530));
    const float a=angle(corr);
    g.setColour(col);
    stroke(a-0.05f, a+0.05f, 6.4f);
}

void Ui::drawAxisLabels(juce::Graphics& g, const std::vector<AxisLabel>& labels, float y, float h, juce::Range<float> clipX, float gap)
{
    const auto font=g.getCurrentFont();
    std::vector<juce::Range<float>> used;
    for(int pass=0; pass<2; ++pass)
        for(const auto& l : labels){
            if(l.priority!=(pass==0)) continue;
            const float w=juce::GlyphArrangement::getStringWidth(font, l.text)+2.f;
            const juce::Range<float> r(l.x-w*0.5f, l.x+w*0.5f);
            if(r.getStart()<clipX.getStart() || r.getEnd()>clipX.getEnd()) continue;
            bool free=true;
            for(const auto& u : used) if(r.getStart()<u.getEnd()+gap && u.getStart()<r.getEnd()+gap){ free=false; break; }
            if(!free) continue;
            used.push_back(r);
            g.drawText(l.text, juce::Rectangle<float>(r.getStart(), y, w, h), juce::Justification::centredTop, false);
        }
}

namespace SpectroReadout {

void drawShadowedText(juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, juce::Colour c)
{
    g.setColour(Col::shade(0.65f));
    g.drawText(t, r.translated(0.8f, 0.8f), j, false);
    g.setColour(c);
    g.drawText(t, r, j, false);
}

void drawFreqMarks(juce::Graphics& g, float topHz, std::function<bool(float, juce::Point<float>&)> posAt,
                   float labelH, juce::Rectangle<float> clip)
{
    struct Mark { float hz; const char* label; bool decade; };
    static const Mark marks[] = { {50.f,"50",false},{100.f,"100",true},{200.f,"200",false},{500.f,"500",false},
                                  {1000.f,"1k",true},{2000.f,"2k",false},{5000.f,"5k",false},{10000.f,"10k",true},
                                  {20000.f,"20k",false},{40000.f,"40k",false},{80000.f,"80k",false},{160000.f,"160k",false} };
    const auto font=g.getCurrentFont();
    std::vector<juce::Range<float>> used;
    for(int pass=0; pass<2; ++pass)
        for(const auto& m : marks){
            if(m.decade!=(pass==0) || m.hz>topHz*0.95f) continue;   // au-delà de 20 kHz seulement si l'axe y va
            juce::Point<float> at;
            if(!posAt(m.hz, at)) continue;
            const float x=at.x, y=at.y;
            const float w=juce::GlyphArrangement::getStringWidth(font, m.label)+2.f;
            const juce::Range<float> r(x-w*0.5f, x+w*0.5f);
            if(r.getStart()<clip.getX() || r.getEnd()>clip.getRight() || y<clip.getY() || y+labelH>clip.getBottom()) continue;
            bool free=true;
            for(const auto& u : used) if(r.getStart()<u.getEnd()+5.f && u.getStart()<r.getEnd()+5.f){ free=false; break; }
            if(!free) continue;
            used.push_back(r);
            drawShadowedText(g, m.label, { r.getStart()-1.f, y, w+2.f, labelH }, juce::Justification::centredTop, Col::textMid.withAlpha(0.9f));
        }
}

juce::String formatHz(float hz)
{
    return hz<100.f   ? juce::String(hz,1)+" Hz"
         : hz<1000.f  ? juce::String(juce::roundToInt(hz))+" Hz"
         : hz<10000.f ? juce::String(hz*0.001f,2)+" kHz"
                      : juce::String(hz*0.001f,1)+" kHz";
}

void draw(juce::Graphics& g, juce::Rectangle<float> area, juce::Point<float> at,
          float hz, float db, float ageSec)
{
    const juce::String a = db<=-99.9f ? juce::String(juce::CharPointer_UTF8("\xe2\x89\xa4 -100 dB"))
                                      : juce::String(db,1)+" dB";
    const juce::String t = ageSec<=0.02f ? juce::String("LIVE") : "-"+juce::String(ageSec,2)+" s";
    drawLines(g, area, at, formatHz(hz), a, t);
}

void drawLines(juce::Graphics& g, juce::Rectangle<float> area, juce::Point<float> at,
               const juce::String& f, const juce::String& a, const juce::String& t)
{
    const auto fBig=Ui::iceland(13.f), fSmall=Ui::iceland(10.5f);
    const float lineBig=fBig.getHeight(), lineSmall=fSmall.getHeight();
    const float tw=juce::jmax(juce::GlyphArrangement::getStringWidth(fBig, f),
                              juce::GlyphArrangement::getStringWidth(fBig, a),
                              juce::GlyphArrangement::getStringWidth(fSmall, t));
    const float bw=tw+16.f, bh=lineBig*2.f+lineSmall+10.f;

    // à droite et au-dessus du point, retournée près des bords
    float x=at.x+14.f, y=at.y-bh-10.f;
    if(x+bw>area.getRight()-4.f)  x=at.x-14.f-bw;
    if(y<area.getY()+20.f)        y=at.y+14.f;
    x=juce::jlimit(area.getX()+4.f, juce::jmax(area.getX()+4.f, area.getRight()-bw-4.f), x);
    y=juce::jlimit(area.getY()+4.f, juce::jmax(area.getY()+4.f, area.getBottom()-bh-4.f), y);
    const juce::Rectangle<float> box(x, y, bw, bh);

    g.setColour(Col::pick(juce::Colours::black.withAlpha(0.72f), juce::Colours::white.withAlpha(0.92f)));
    g.fillRoundedRectangle(box, 5.f);
    g.setColour(Col::accent.withAlpha(0.7f));
    g.drawRoundedRectangle(box.reduced(0.5f), 5.f, 1.f);

    auto r=box.reduced(8.f, 5.f);
    g.setFont(fBig);   g.setColour(Col::textHi);
    g.drawText(f, r.removeFromTop(lineBig), juce::Justification::left, false);
    g.setColour(Col::accent);
    g.drawText(a, r.removeFromTop(lineBig), juce::Justification::left, false);
    g.setFont(fSmall); g.setColour(Col::textMid);
    g.drawText(t, r.removeFromTop(lineSmall), juce::Justification::left, false);
}
}

//==============================================================================
// Neu — châssis neumorphique
//==============================================================================
namespace Neu {
void apply(bool lightTint, bool contrast, bool slate, bool paperScreens)
{
    light = lightTint;
    highContrast = contrast;
    Col::paper = light && paperScreens;   // palette relue par Theme::applyCustom()
    if(!light && slate){
        // ardoise bleutée de la maquette de Sunny (plaque, cartes, creux)
        surface     = juce::Colour(0xFF2A3240);
        frame       = juce::Colour(0xFF20262F);
        well        = juce::Colour(0xFF141A22);
        bezel       = juce::Colour(0xFF111417);
        shadowDark  = juce::Colours::black.withAlpha(0.62f);
        shadowLight = juce::Colours::white.withAlpha(0.075f);
        text        = juce::Colour(0xFFDDE3EA);
        textDim     = juce::Colour(0xFF8D99A8);
    } else if(light){
        surface     = juce::Colour(0xFFE2E7EE);
        frame       = juce::Colour(0xFFD4DBE4);
        well        = juce::Colour(0xFFC8D0DB);
        bezel       = juce::Colour(0xFF2A3136);
        shadowDark  = juce::Colour(0xFFA3B1C6).withAlpha(0.90f);
        shadowLight = juce::Colours::white.withAlpha(0.95f);
        text        = juce::Colour(0xFF353E4B);
        textDim     = juce::Colour(0xFF6F7A8B);
    } else {
        surface     = juce::Colour(0xFF262A30);
        frame       = juce::Colour(0xFF1D2025);
        well        = juce::Colour(0xFF15181C);
        bezel       = juce::Colour(0xFF111417);
        shadowDark  = juce::Colours::black.withAlpha(0.62f);
        shadowLight = juce::Colours::white.withAlpha(0.075f);
        text        = juce::Colour(0xFFD9DFE7);
        textDim     = juce::Colour(0xFF8B95A3);
    }
    if(highContrast){
        // reliefs plus marqués, textes plus francs
        shadowDark  = shadowDark.withAlpha(1.0f);
        shadowLight = shadowLight.withAlpha(light ? 1.0f : 0.16f);
        text        = light ? juce::Colour(0xFF111720) : juce::Colours::white;
        textDim     = light ? juce::Colour(0xFF3E4857) : juce::Colour(0xFFC3CAD4);
    }
}

juce::Colour accentInk()
{
    return light ? Col::accent.darker(0.9f) : Col::accent;
}

void drawRaised(juce::Graphics& g, juce::Rectangle<float> r, float radius, float depth)
{
    juce::Path p; p.addRoundedRectangle(r, radius);
    const int blur=juce::jmax(2, juce::roundToInt(depth*2.4f));
    const int off =juce::jmax(1, juce::roundToInt(depth));
    juce::DropShadow(shadowDark,  blur, { off,  off}).drawForPath(g, p);
    juce::DropShadow(shadowLight, blur, {-off, -off}).drawForPath(g, p);
    // face : à peine plus claire côté lumière (haut-gauche)
    juce::ColourGradient face(surface.brighter(light ? 0.015f : 0.05f), r.getX(), r.getY(),
                              surface.darker  (light ? 0.025f : 0.07f), r.getRight(), r.getBottom(), false);
    g.setGradientFill(face); g.fillPath(p);
    if(highContrast){   // contour net autour de chaque relief
        g.setColour(light ? juce::Colours::black.withAlpha(0.22f) : juce::Colours::white.withAlpha(0.14f));
        g.strokePath(p, juce::PathStrokeType(1.f));
    }
}

void drawInset(juce::Graphics& g, juce::Rectangle<float> r, float radius, float depth)
{
    juce::Path p; p.addRoundedRectangle(r, radius);
    g.setColour(well); g.fillPath(p);
    // ombres internes : celles d'un cadre qui entoure le creux, limitées au creux
    juce::Path ring; ring.addRectangle(r.expanded(depth*6.f)); ring.addRoundedRectangle(r, radius);
    ring.setUsingNonZeroWinding(false);
    const int blur=juce::jmax(2, juce::roundToInt(depth*2.4f));
    const int off =juce::jmax(1, juce::roundToInt(depth));
    g.saveState();
    g.reduceClipRegion(p);
    juce::DropShadow(shadowDark,  blur, { off,  off}).drawForPath(g, ring);
    juce::DropShadow(shadowLight, blur, {-off, -off}).drawForPath(g, ring);
    g.restoreState();
}

// Rayon extérieur de la lunette : les modules sont des rectangles (coins arrondis
// par-dessus via drawScreenGlass) ; avec screenRadius+bezelW (16 px), le coin du
// rectangle sortait d'un pixel hors de la courbe extérieure. Le coin reste
// dedans tant que (R - bezelW)·√2 < R, soit R < 3,41·bezelW : on garde une marge.
static float bezelOuterRadius() { return juce::jmin(screenRadius+bezelW, bezelW*3.1f); }
// anneau autour d'un écran Papier : la carte, un ton plus bas
static juce::Colour paperBezel() { return surface.darker(0.05f); }

void drawBezel(juce::Graphics& g, juce::Rectangle<float> s)
{
    const auto o=s.expanded(bezelW);
    const float R=bezelOuterRadius();
    juce::Path p; p.addRoundedRectangle(o, R);
    if(Col::paper){
        // écran Papier : pas de lunette sombre, l'écran est creusé dans la carte
        // (anneau à peine plus foncé que la carte, filet gris-bleu net)
        g.setColour(paperBezel()); g.fillPath(p);
        g.setColour(juce::Colour(0xFFB4BFCC));
        g.drawRoundedRectangle(o.reduced(0.5f), R-0.5f, 1.f);
        return;
    }
    g.setColour(bezel); g.fillPath(p);
    // la lunette s'enfonce dans la carte : filet sombre net sur son bord extérieur
    // (une ombre floue assombrissait tout l'anneau, et les coins de l'écran,
    // peints à la couleur pure par drawScreenGlass, ressortaient en petits angles clairs)
    g.setColour(juce::Colours::black.withAlpha(light ? 0.45f : 0.6f));
    g.drawRoundedRectangle(o.reduced(0.5f), R-0.5f, 1.f);
    juce::ColourGradient lip(juce::Colours::transparentWhite, o.getX(), o.getY(),
                             shadowLight.withMultipliedAlpha(light ? 0.9f : 1.f), o.getX(), o.getBottom(), false);
    g.setGradientFill(lip);
    g.drawRoundedRectangle(o.expanded(0.5f), R+0.5f, 1.f);
}

void drawScreenGlass(juce::Graphics& g, juce::Rectangle<float> s)
{
    // coins : les modules peignent des rectangles, la lunette les arrondit
    juce::Path caps; caps.addRectangle(s); caps.addRoundedRectangle(s, screenRadius);
    caps.setUsingNonZeroWinding(false);
    juce::Path glass; glass.addRoundedRectangle(s, screenRadius);
    if(Col::paper){
        // écran Papier creusé : ombre interne en haut à gauche, lumière en bas
        // à droite, filet net ; pas de reflet
        g.setColour(paperBezel()); g.fillPath(caps);
        juce::Path ring; ring.addRectangle(s.expanded(24.f)); ring.addRoundedRectangle(s, screenRadius);
        ring.setUsingNonZeroWinding(false);
        g.saveState();
        g.reduceClipRegion(glass);
        juce::DropShadow(juce::Colour(0xFF283C5A).withAlpha(0.20f), 9, { 3,  3}).drawForPath(g, ring);
        juce::DropShadow(juce::Colours::white.withAlpha(0.85f),     7, {-2, -2}).drawForPath(g, ring);
        g.restoreState();
        g.setColour(juce::Colour(0xFFC3CCD7));
        g.drawRoundedRectangle(s.reduced(0.5f), screenRadius, 1.f);
        return;
    }
    g.setColour(bezel); g.fillPath(caps);
    // bord de la vitre : filet sombre tout autour + léger reflet en haut
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.drawRoundedRectangle(s.reduced(0.5f), screenRadius, 1.f);
    g.saveState();
    g.reduceClipRegion(glass);
    juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.07f), s.getX(), s.getY(),
                               juce::Colours::transparentWhite, s.getX(), s.getY()+10.f, false);
    g.setGradientFill(sheen);
    g.fillRect(s.withHeight(10.f));
    g.restoreState();
}
}

//==============================================================================
// PillToggle — port de AlphaPillToggle (Sevenpack Alpha), palette "Sunny" :
// fond bleu quand actif, ardoise sombre sinon ; pastille blanche bombée.
//==============================================================================
float PillToggle::Tween::valueAt(double now, bool backEase) const noexcept
{
    const float t=(float)juce::jlimit(0.0, 1.0, (now-startMs)/durMs);
    float e;
    if(backEase){
        // ~ cubic-bezier(.5, 1.6, .4, 1) : dépasse ~10 % puis revient
        constexpr float c1=1.70158f, c3=c1+1.0f;
        e=1.0f+c3*std::pow(t-1.0f, 3.0f)+c1*std::pow(t-1.0f, 2.0f);
    } else {
        e=t*t*(3.0f-2.0f*t);
    }
    return from+(to-from)*e;
}

void PillToggle::syncAnimationTargets(bool down)
{
    const bool on=getToggleState();
    const double now=juce::Time::getMillisecondCounterHiRes();
    if(!initialised){
        initialised=true;
        animatedOn=on;
        knobPos=colourMix=iconProgress=on ? 1.0f : 0.0f;
        knobTween=colourTween=iconTween={ knobPos, knobPos, now, 1.0 };
    }
    if(on!=animatedOn){
        animatedOn=on;
        const float target=on ? 1.0f : 0.0f;
        knobTween  ={ knobPos,      target, now, 380.0 };
        colourTween={ colourMix,    target, now, 350.0 };
        iconTween  ={ iconProgress, target, now, 400.0 };
        startTimerHz(60);
    }
    if(down!=pressedTarget){
        pressedTarget=down;
        startTimerHz(60);
    }
}

void PillToggle::timerCallback()
{
    const double now=juce::Time::getMillisecondCounterHiRes();
    knobPos     =knobTween.valueAt(now, true);
    colourMix   =juce::jlimit(0.0f, 1.0f, colourTween.valueAt(now, false));
    iconProgress=iconTween.valueAt(now, true);
    pressT+=((pressedTarget ? 1.0f : 0.0f)-pressT)*0.35f;   // ~180 ms

    const bool pressSettled=std::abs(pressT-(pressedTarget ? 1.0f : 0.0f))<0.01f;
    if(knobTween.done(now) && colourTween.done(now) && iconTween.done(now) && pressSettled){
        pressT=pressedTarget ? 1.0f : 0.0f;
        stopTimer();
    }
    repaint();
}

void PillToggle::paintButton(juce::Graphics& g, bool, bool down)
{
    syncAnimationTargets(down);

    auto r=getLocalBounds().toFloat().reduced(1.0f);
    r=r.withSizeKeepingCentre(r.getWidth(), juce::jmin(r.getHeight(), 26.0f));
    const float rad=r.getHeight()*0.5f;
    const float k=r.getHeight()/88.0f;   // échelle de la preview d'origine (88 px de haut)

    // Fond : off -> on en fondu. Lune : jour = ambre soleil, nuit = bleu.
    const juce::Colour blue(0xff3da0ec);
    const juce::Colour offCol = icon==Icon::Moon ? juce::Colour(0xfff0a93a)
                                                 : (Neu::light ? juce::Colour(0xffaab6c4) : juce::Colour(0xff212c38));
    const auto bg=offCol.interpolatedWith(blue, colourMix);
    g.setColour(bg);
    g.fillRoundedRectangle(r, rad);
    juce::ColourGradient inner(juce::Colours::black.withAlpha(0.22f), r.getX(), r.getY(),
                               juce::Colours::transparentBlack, r.getX(), r.getY()+r.getHeight()*0.45f, false);
    g.setGradientFill(inner);
    g.fillRoundedRectangle(r, rad);
    g.setColour(juce::Colours::white.withAlpha(0.10f));   // reflet
    g.fillRoundedRectangle(r.withTrimmedTop(r.getHeight()*0.55f).reduced(rad*0.5f, 0.f), rad*0.4f);

    // Icônes (sous la pastille, dans la moitié libre)
    const float iconW=r.getHeight()*(112.0f/88.0f);
    const auto leftZone =juce::Rectangle<float>(r.getX(), r.getY(), iconW, r.getHeight());
    const auto rightZone=juce::Rectangle<float>(r.getRight()-iconW, r.getY(), iconW, r.getHeight());
    const float iconSize=r.getHeight()*0.55f;

    if(icon==Icon::Moon){
        // Nuit (on) : lune à gauche — croissant = disque "effacé" par le fond
        drawAnimatedGlyph(g, leftZone.getCentre(), iconSize, iconProgress, -30.0f*k, -120.0f, 1.0f,
                          [bg](juce::Graphics& gg, juce::Rectangle<float> a){
                              auto disc=a.reduced(a.getWidth()*0.08f);
                              gg.setColour(juce::Colours::white); gg.fillEllipse(disc);
                              gg.setColour(bg); gg.fillEllipse(disc.translated(disc.getWidth()*0.34f, -disc.getHeight()*0.22f));
                          });
        // Jour (off) : soleil à droite
        drawAnimatedGlyph(g, rightZone.getCentre(), iconSize, 1.0f-iconProgress, 30.0f*k, 120.0f, 1.0f,
                          [](juce::Graphics& gg, juce::Rectangle<float> a){
                              gg.setColour(juce::Colours::white);
                              const auto c=a.getCentre();
                              const float rr=a.getWidth()*0.22f;
                              gg.fillEllipse(juce::Rectangle<float>(rr*2.0f, rr*2.0f).withCentre(c));
                              const float sw=juce::jmax(1.2f, a.getWidth()*0.09f);
                              for(int i=0;i<8;++i){
                                  const float ang=juce::MathConstants<float>::twoPi*(float)i/8.0f;
                                  const float r0=rr*1.5f, r1=a.getWidth()*0.5f;
                                  gg.drawLine(c.x+std::sin(ang)*r0, c.y-std::cos(ang)*r0, c.x+std::sin(ang)*r1, c.y-std::cos(ang)*r1, sw);
                              }
                          });
    } else {
        // Contraste (demi-disque) ou 3D (cube) : glyphe du côté libre
        const bool on=getToggleState();
        const float popT=juce::jlimit(0.0f, 1.0f, on ? iconProgress : 1.0f-iconProgress);
        const juce::Colour ink = on ? juce::Colours::white
                                    : (Neu::light ? juce::Colour(0xff3e4857) : juce::Colour(0xffaab4c0));
        const bool cube=icon==Icon::Cube, isText=icon==Icon::Text, meter=icon==Icon::Meter, pin=icon==Icon::Pin;
        const bool kbd=icon==Icon::Keyboard;
        drawAnimatedGlyph(g, (on ? leftZone : rightZone).getCentre(), iconSize, popT, 0.0f, on ? -90.0f : 90.0f, 0.4f,
                          [ink, cube, isText, meter, pin, kbd](juce::Graphics& gg, juce::Rectangle<float> a){
                              if(kbd){
                                  // clavier : boîtier arrondi, deux rangées de touches, barre d'espace
                                  gg.setColour(ink);
                                  const float s=a.getWidth(), lw=juce::jmax(1.1f, s*0.08f);
                                  const auto body=juce::Rectangle<float>(a.getX(), a.getCentreY()-s*0.34f, s, s*0.68f);
                                  gg.drawRoundedRectangle(body.reduced(lw*0.5f), s*0.10f, lw);
                                  const float k=s*0.11f;
                                  for(int row=0; row<2; ++row)
                                      for(int i=0; i<4; ++i)
                                          gg.fillRect(body.getX()+s*0.16f+(float)i*s*0.19f, body.getY()+s*0.14f+(float)row*s*0.17f, k, k);
                                  gg.fillRect(body.getX()+s*0.28f, body.getBottom()-s*0.20f, s*0.44f, k);
                                  return;
                              }
                              if(pin){
                                  // punaise : tête, collerette, pointe (toujours au premier plan)
                                  gg.setColour(ink);
                                  const float s=a.getWidth(), cx=a.getCentreX(), y0=a.getY()+s*0.04f;
                                  gg.fillRoundedRectangle(cx-s*0.17f, y0, s*0.34f, s*0.36f, s*0.07f);
                                  gg.fillRoundedRectangle(cx-s*0.31f, y0+s*0.34f, s*0.62f, s*0.11f, s*0.05f);
                                  gg.drawLine(cx, y0+s*0.45f, cx, y0+s*0.92f, juce::jmax(1.2f, s*0.09f));
                                  return;
                              }
                              if(meter){
                                  // deux barres de VU de hauteurs différentes sur une ligne de base
                                  gg.setColour(ink);
                                  const float bw=a.getWidth()*0.24f, base=a.getBottom()-a.getHeight()*0.08f;
                                  gg.fillRoundedRectangle(a.getX()+a.getWidth()*0.18f, base-a.getHeight()*0.80f, bw, a.getHeight()*0.80f, bw*0.3f);
                                  gg.fillRoundedRectangle(a.getRight()-a.getWidth()*0.18f-bw, base-a.getHeight()*0.52f, bw, a.getHeight()*0.52f, bw*0.3f);
                                  return;
                              }
                              if(isText){
                                  // "Aa" : taille de texte (glyphe fixe, hors échelle de texte)
                                  gg.setColour(ink);
                                  gg.setFont(Ui::icelandAbs(a.getHeight()*1.15f));
                                  gg.drawText("Aa", a.expanded(4.f), juce::Justification::centred, false);
                                  return;
                              }
                              if(cube){
                                  // cube isométrique : dessus plein, faces en contour
                                  const auto o=a.getCentre();
                                  const float s=a.getWidth()*0.46f, hx=s*0.866f, hy=s*0.5f;
                                  juce::Path topF, outline;
                                  topF.startNewSubPath(o.x, o.y-s); topF.lineTo(o.x+hx, o.y-hy);
                                  topF.lineTo(o.x, o.y); topF.lineTo(o.x-hx, o.y-hy); topF.closeSubPath();
                                  outline.startNewSubPath(o.x, o.y-s); outline.lineTo(o.x+hx, o.y-hy);
                                  outline.lineTo(o.x+hx, o.y+hy); outline.lineTo(o.x, o.y+s);
                                  outline.lineTo(o.x-hx, o.y+hy); outline.lineTo(o.x-hx, o.y-hy); outline.closeSubPath();
                                  outline.startNewSubPath(o.x, o.y); outline.lineTo(o.x, o.y+s);
                                  gg.setColour(ink);
                                  gg.fillPath(topF);
                                  gg.strokePath(outline, juce::PathStrokeType(juce::jmax(1.2f, a.getWidth()*0.09f),
                                                                              juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                                  return;
                              }
                              auto disc=a.reduced(a.getWidth()*0.06f);
                              juce::Path half;
                              half.addPieSegment(disc, juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi, 0.0f);
                              gg.setColour(ink);
                              gg.fillPath(half);
                              gg.drawEllipse(disc, juce::jmax(1.2f, a.getWidth()*0.1f));
                          });
    }

    // Pastille (s'élargit pendant l'appui)
    const float d=r.getHeight()-2.0f;
    const float knobW=d*(1.0f+0.19f*pressT);
    const float travel=r.getWidth()-2.0f-knobW;
    const auto knob=juce::Rectangle<float>(r.getX()+1.0f+knobPos*travel, r.getY()+1.0f, knobW, d);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(knob.translated(0.0f, 1.2f), d*0.5f);
    juce::ColourGradient kg(juce::Colours::white, knob.getX()+d*0.35f, knob.getY()+d*0.3f,
                            juce::Colour(0xffe4e8ec), knob.getRight(), knob.getBottom(), true);
    g.setGradientFill(kg);
    g.fillRoundedRectangle(knob, d*0.5f);
}

//==============================================================================
// KebabButton — port de AlphaKebab (Sevenpack Alpha)
//==============================================================================
void KebabButton::paintButton(juce::Graphics& g, bool over, bool down)
{
    const auto c=getLocalBounds().toFloat().getCentre();
    const float h=(float)getHeight();
    const float ds=juce::jlimit(4.f, 7.f, h*0.2f), step=ds*1.45f;
    const auto acc=Neu::accentInk();
    for(int i=-1;i<=1;++i){
        const auto dot=juce::Rectangle<float>(ds, ds).withCentre({c.x, c.y+(float)i*step+(down ? 1.f : 0.f)});
        juce::Path p; p.addEllipse(dot);
        juce::DropShadow(acc.withAlpha(over ? 0.8f : 0.5f), 4, {}).drawForPath(g, p);
        g.setColour(acc);
        g.fillEllipse(dot);
    }
}

void FullScreenButton::paintButton(juce::Graphics& g, bool over, bool down)
{
    if(over!=overNow){ overNow=over; startTimerHz(60); }
    const auto c=getLocalBounds().toFloat().getCentre();
    const float s=juce::jmin((float)getWidth(), (float)getHeight())*0.56f;   // carré des équerres
    const float L=s*0.34f;                                                   // longueur d'une branche
    const float shift=(full ? -1.f : 1.f)*hoverK*1.6f + (down ? (full ? 0.8f : -0.8f) : 0.f);
    const auto acc=Neu::accentInk();
    juce::Path p;
    for(int i=0;i<4;++i){
        const float sx=(i&1) ? 1.f : -1.f, sy=(i&2) ? 1.f : -1.f;
        const float hx=c.x+sx*(s*0.5f+shift), hy=c.y+sy*(s*0.5f+shift);   // coin
        if(!full){
            // vers l'extérieur : l'angle est au coin
            p.startNewSubPath(hx, hy-sy*L); p.lineTo(hx, hy); p.lineTo(hx-sx*L, hy);
        } else {
            // vers l'intérieur : l'angle est rentré d'une branche
            const float ix=hx-sx*L, iy=hy-sy*L;
            p.startNewSubPath(hx, iy); p.lineTo(ix, iy); p.lineTo(ix, hy);
        }
    }
    const juce::PathStrokeType st(juce::jlimit(1.6f, 2.4f, s*0.12f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path stroked; st.createStrokedPath(stroked, p);
    juce::DropShadow(acc.withAlpha(0.5f+0.3f*hoverK), 4, {}).drawForPath(g, stroked);
    g.setColour(acc);
    g.fillPath(stroked);
}

void FullScreenButton::timerCallback()
{
    const float target=overNow ? 1.f : 0.f;
    hoverK+=(target-hoverK)*0.25f;
    if(std::abs(target-hoverK)<0.01f){ hoverK=target; stopTimer(); }
    repaint();
}

void KeyboardButton::paintButton(juce::Graphics& g, bool over, bool down)
{
    if(over!=overNow){ overNow=over; startTimerHz(60); }
    const bool on=getToggleState();
    const auto r=getLocalBounds().toFloat();
    const float w=juce::jmin(r.getWidth()*0.74f, r.getHeight()*0.9f), h=w*0.6f;
    const auto kb=juce::Rectangle<float>(w, h).withCentre(r.getCentre()).translated(0.f, down ? 0.6f : 0.f);
    const float sw=juce::jlimit(1.4f, 2.2f, w*0.075f);
    juce::Path outline; outline.addRoundedRectangle(kb.reduced(sw*0.5f), h*0.2f);
    juce::Path shape; juce::PathStrokeType(sw).createStrokedPath(shape, outline);
    // deux rangées de touches + barre d'espace
    const float pad=w*0.2f, k=w*0.09f;
    for(int row=0;row<2;++row)
        for(int i=0;i<5;++i){
            const float cx=kb.getX()+pad+(w-2.f*pad)*(float)i/4.f;
            const float cy=kb.getY()+h*(0.33f+0.2f*(float)row);
            shape.addRectangle(cx-k*0.5f, cy-k*0.5f, k, k);
        }
    shape.addRoundedRectangle(kb.getCentreX()-w*0.22f, kb.getY()+h*0.73f-k*0.5f, w*0.44f, k, k*0.4f);
    const auto acc=Neu::accentInk();
    if(on) juce::DropShadow(acc.withAlpha(0.5f+0.3f*hoverK), 4, {}).drawForPath(g, shape);
    g.setColour(on ? acc : acc.withMultipliedAlpha(0.42f+0.3f*hoverK));
    g.fillPath(shape);
}

void KeyboardButton::timerCallback()
{
    const float target=overNow ? 1.f : 0.f;
    hoverK+=(target-hoverK)*0.25f;
    if(std::abs(target-hoverK)<0.01f){ hoverK=target; stopTimer(); }
    repaint();
}

//==============================================================================
// BandAxis — axe des bandes du Corrélateur et du Panorama (même principe que
// le Spectrum) : log calé sur les BORDS des bandes extrêmes (elles ne sont
// plus coupées par le cadre), colonnes de largeur égale calées au pixel.
//==============================================================================
namespace {
struct BandAxis
{
    BandAxis(const float* F, int N, float xStart, float width)
        : x0(xStart), w(width), n(juce::jmax(1, N))
    {
        const float half=N>1 ? (std::log10(F[N-1])-std::log10(F[0]))/(float)(N-1)*0.5f : 0.15f;
        logMin=std::log10(F[0])-half;
        logMax=std::log10(F[juce::jmax(0, N-1)])+half;
    }
    float freqToX(float f) const { return x0+(std::log10(juce::jmax(1.f, f))-logMin)/(logMax-logMin)*w; }
    float colX(int i) const      { return std::round(x0+(float)i*w/(float)n); }
    float colW(int i) const      { const float cw=w/(float)n; return juce::jmax(1.f, colX(i+1)-colX(i)-(cw>=10.f ? 2.f : 1.f)); }
    int   bandAt(float px) const { return juce::jlimit(0, n-1, (int)std::floor((px-x0)/w*(float)n)); }
    float x0, w, logMin { 0.f }, logMax { 1.f };
    int   n;
};
}

//==============================================================================
// Spec3D — style "Blocks 3D" du Spectrum
//==============================================================================
namespace Spec3D {
float depthFor(float slotW) { return juce::jlimit(2.f, 10.f, slotW*0.32f); }

juce::ColourGradient levelGradient(float plotTop, float plotBottom)
{
    // mêmes couleurs que le Classic, mais opaques en bas : les blocs restent
    // pleins même à bas niveau
    juce::ColourGradient g(Col::ink(juce::Colour(0xFFFF1500)).withAlpha(0.95f), 0.f, plotTop,
                           juce::Colour(0xFF00C88C).withAlpha(0.60f), 0.f, plotBottom, false);
    g.addColour(0.12, Col::ink(juce::Colour(0xFFFF6600)).withAlpha(0.93f));
    g.addColour(0.28, Col::ink(juce::Colour(0xFFFFD200)).withAlpha(0.90f));
    g.addColour(0.52, Col::ink(juce::Colour(0xFF00FF55)).withAlpha(0.86f));
    g.addColour(0.78, Col::ink(juce::Colour(0xFF00FFB2)).withAlpha(0.80f));
    return g;
}

void drawSocle(juce::Graphics& g, juce::Rectangle<float> slot)
{
    const float d=depthFor(slot.getWidth()), dy=d*0.7f;
    const float fw=juce::jmax(1.f, slot.getWidth()-d), x=slot.getX(), b=slot.getBottom();
    juce::Path top;
    top.startNewSubPath(x, b); top.lineTo(x+d, b-dy); top.lineTo(x+fw+d, b-dy); top.lineTo(x+fw, b); top.closeSubPath();
    g.setColour(Col::grid.withAlpha(0.8f)); g.fillPath(top);
}

void drawBlock(juce::Graphics& g, juce::Rectangle<float> slot, float top, juce::Colour topCol,
               const juce::ColourGradient& front, const juce::ColourGradient& side,
               float peakY, juce::Colour peakCol)
{
    const float d=depthFor(slot.getWidth()), dy=d*0.7f;
    const float fw=juce::jmax(1.f, slot.getWidth()-d), x=slot.getX(), bottom=slot.getBottom();
    const float h=bottom-top;
    if(h>0.5f){
        // face avant : dégradé de niveau + lumière sur le tiers gauche
        g.setGradientFill(front); g.fillRect(x, top, fw, h);
        g.setColour(juce::Colours::white.withAlpha(0.12f)); g.fillRect(x, top, fw*0.35f, h);
        // flanc : même dégradé (décalé), assombri
        juce::Path sideP;
        sideP.startNewSubPath(x+fw, top); sideP.lineTo(x+fw+d, top-dy); sideP.lineTo(x+fw+d, bottom-dy);
        sideP.lineTo(x+fw, bottom); sideP.closeSubPath();
        g.setGradientFill(side); g.fillPath(sideP);
        g.setColour(juce::Colours::black.withAlpha(0.5f)); g.fillPath(sideP);
        // dessus : couleur du niveau atteint, éclairée
        juce::Path topP;
        topP.startNewSubPath(x, top); topP.lineTo(x+d, top-dy); topP.lineTo(x+fw+d, top-dy); topP.lineTo(x+fw, top);
        topP.closeSubPath();
        g.setColour(topCol.interpolatedWith(juce::Colours::white, 0.35f)); g.fillPath(topP);
        // arête avant gauche
        g.setColour(juce::Colours::white.withAlpha(0.35f)); g.fillRect(x, top, 1.f, h);
    }
    if(peakY>=0.f){
        // crête : fine dalle flottante avec son dessus
        const float py=peakY-1.5f;
        g.setColour(peakCol); g.fillRect(x, py, fw, 2.5f);
        juce::Path pt;
        pt.startNewSubPath(x, py); pt.lineTo(x+d, py-dy); pt.lineTo(x+fw+d, py-dy); pt.lineTo(x+fw, py); pt.closeSubPath();
        g.setColour(peakCol.interpolatedWith(juce::Colours::white, 0.4f)); g.fillPath(pt);
    }
}
}

//==============================================================================
// NeuAnimButton — bouton animé de la barre de contrôles
//==============================================================================
void NeuAnimButton::mouseDown(const juce::MouseEvent& e)
{
    ripplePos=e.position; ripple=0.f;   // onde au point de clic
    kick();
    juce::TextButton::mouseDown(e);
}

void NeuAnimButton::timerCallback()
{
    constexpr float dt=1.f/60.f;
    const bool on=getToggleState(), down=isDown(), over=isOver();
    // ressort légèrement sous-amorti : petit rebond en fin de course
    const float target=(on||down) ? 1.f : 0.f;
    constexpr float k=520.f, c=26.f;
    pressVel+=((target-press)*k-pressVel*c)*dt;
    press+=pressVel*dt;
    hover+=((over ? 1.f : 0.f)-hover)*0.22f;
    glow +=((on   ? 1.f : 0.f)-glow )*0.16f;
    if(ripple>=0.f){ ripple+=dt/0.45f; if(ripple>=1.f) ripple=-1.f; }

    const bool settled=std::abs(target-press)<0.002f && std::abs(pressVel)<0.01f
                    && std::abs((over ? 1.f : 0.f)-hover)<0.01f && std::abs((on ? 1.f : 0.f)-glow)<0.01f
                    && ripple<0.f;
    if(settled){
        press=target; pressVel=0.f; hover=over ? 1.f : 0.f; glow=on ? 1.f : 0.f;
        stopTimer();
    }
    repaint();
}

void NeuAnimButton::paintButton(juce::Graphics& g, bool over, bool down)
{
    const bool on=getToggleState();
    if(!initialised){
        initialised=true;
        press=(on||down) ? 1.f : 0.f; glow=on ? 1.f : 0.f; hover=over ? 1.f : 0.f;
    }
    // tout changement d'état (clic, survol, bascule par programme) relance l'animation
    if(std::abs(((on||down) ? 1.f : 0.f)-press)>0.002f || std::abs((on ? 1.f : 0.f)-glow)>0.01f
       || std::abs((over ? 1.f : 0.f)-hover)>0.01f)
        kick();

    // marge réservée dans les bounds pour le relief (pas de dessin hors limites)
    const auto r=getLocalBounds().toFloat().reduced(reliefMargin>=0.f ? reliefMargin : arrowDir!=0 ? 1.5f : 2.5f);
    const float rad=juce::jmin(r.getHeight()*0.5f, 9.f);
    juce::Path shape; shape.addRoundedRectangle(r, rad);
    const float raised=juce::jlimit(0.f, 1.f, 1.f-press);
    const float sunk  =juce::jlimit(0.f, 1.f, press);

    // relief extérieur : s'efface en s'enfonçant, se soulève au survol
    if(raised>0.01f){
        const float depth=1.6f+hover*0.9f;
        const int blur=juce::roundToInt(depth*2.4f), off=juce::jmax(1, juce::roundToInt(depth));
        juce::DropShadow(Neu::shadowDark .withMultipliedAlpha(raised), blur, { off,  off}).drawForPath(g, shape);
        juce::DropShadow(Neu::shadowLight.withMultipliedAlpha(raised), blur, {-off, -off}).drawForPath(g, shape);
    }
    g.setColour(Neu::surface.interpolatedWith(Neu::well, sunk*0.75f));
    g.fillPath(shape);

    // ombres internes quand il est enfoncé
    if(sunk>0.01f){
        juce::Path ring; ring.addRectangle(r.expanded(10.f)); ring.addRoundedRectangle(r, rad);
        ring.setUsingNonZeroWinding(false);
        g.saveState(); g.reduceClipRegion(shape);
        juce::DropShadow(Neu::shadowDark .withMultipliedAlpha(sunk), 4, { 2,  2}).drawForPath(g, ring);
        juce::DropShadow(Neu::shadowLight.withMultipliedAlpha(sunk), 4, {-2, -2}).drawForPath(g, ring);
        g.restoreState();
    }

    const auto acc=Neu::accentInk();
    if(glow>0.01f){
        // lueur d'accent qui monte du bas + barre lumineuse ouverte depuis le centre
        g.saveState(); g.reduceClipRegion(shape);
        juce::ColourGradient lg(acc.withAlpha(0.22f*glow), r.getCentreX(), r.getBottom(),
                                acc.withAlpha(0.f), r.getCentreX(), r.getY(), false);
        g.setGradientFill(lg); g.fillRect(r);
        g.restoreState();
        const auto bar=juce::Rectangle<float>(r.getWidth()*0.46f*glow, 2.f).withCentre({r.getCentreX(), r.getBottom()-4.f});
        g.setColour(acc.withAlpha(0.35f*glow)); g.fillRoundedRectangle(bar.expanded(2.f, 1.5f), 2.f);
        g.setColour(acc.withAlpha(glow));       g.fillRoundedRectangle(bar, 1.f);
    }
    if(ripple>=0.f){
        // onde qui s'étend depuis le point de clic et s'éteint
        const float rr=std::hypot(r.getWidth(), r.getHeight())*(0.15f+0.85f*ripple);
        g.saveState(); g.reduceClipRegion(shape);
        g.setColour(acc.withAlpha(0.35f*(1.f-ripple)));
        g.fillEllipse(ripplePos.x-rr, ripplePos.y-rr, rr*2.f, rr*2.f);
        g.restoreState();
    }

    // texte : descend d'un pixel en s'enfonçant, vire à l'accent quand actif
    g.setColour(Neu::text.withAlpha(0.85f).interpolatedWith(acc, glow));
    if(arrowDir!=0){
        // flèche pleine centrée, à la place du texte (suit l'enfoncement)
        const float h=juce::jmin(r.getHeight()*0.48f, r.getWidth()*0.32f), w=h*1.7f;
        const auto c=r.getCentre().translated(0.f, sunk*0.6f);
        juce::Path tri;
        if(arrowDir>0) tri.addTriangle(c.x-w*0.5f, c.y+h*0.5f, c.x+w*0.5f, c.y+h*0.5f, c.x, c.y-h*0.5f);
        else           tri.addTriangle(c.x-w*0.5f, c.y-h*0.5f, c.x+w*0.5f, c.y-h*0.5f, c.x, c.y+h*0.5f);
        g.fillPath(tri);
        return;
    }
    const int textLen=juce::jmax(1, getButtonText().length());
    const float maxFsByW=(r.getWidth()-1.f)/((float)textLen*0.62f);
    const float fs=juce::jlimit(7.f, 15.f*Ui::textScale, juce::jmin((r.getHeight()+5.f)*0.5f*Ui::textScale, maxFsByW));
    g.setFont(Ui::icelandAbs(fs));   // taille ajustée à la largeur, déjà à l'échelle
    g.drawText(getButtonText(), getLocalBounds().toFloat().translated(0.f, sunk-glow), juce::Justification::centred);
}

//==============================================================================
namespace
{
    constexpr int kWindowBarH = 22;

    struct WindowBarButton final : public juce::Button
    {
        explicit WindowBarButton(int t) : juce::Button("window"), type(t) {}

        void paintButton(juce::Graphics& g, bool over, bool down) override
        {
            const auto r=getLocalBounds().toFloat();
            const bool close=type==juce::DocumentWindow::closeButton;
            if(over || down){
                g.setColour(close ? juce::Colour(0xffd9443f).withAlpha(down ? 0.8f : 1.f)
                                  : Neu::text.withAlpha(down ? 0.2f : 0.1f));
                g.fillRoundedRectangle(r.reduced(1.f), 4.f);
            }
            g.setColour(close && (over || down) ? juce::Colours::white : Neu::text.withAlpha(0.8f));
            const auto c=r.getCentre();
            const float a=4.f, t=1.2f;
            if(type==juce::DocumentWindow::minimiseButton)
                g.drawLine(c.x-a, c.y, c.x+a, c.y, t);
            else if(close){
                g.drawLine(c.x-a, c.y-a, c.x+a, c.y+a, t);
                g.drawLine(c.x-a, c.y+a, c.x+a, c.y-a, t);
            } else if(getToggleState()){   // fenêtre agrandie : restaurer
                g.drawRect(juce::Rectangle<float>(a*1.6f, a*1.6f).withCentre({ c.x-1.5f, c.y+1.5f }), t);
                g.drawLine(c.x-a+2.5f, c.y-a, c.x+a, c.y-a, t);
                g.drawLine(c.x+a, c.y-a, c.x+a, c.y+a-2.5f, t);
            } else
                g.drawRect(juce::Rectangle<float>(a*2.f, a*2.f).withCentre(c), t);
        }

        int type;
    };
}

juce::ColourGradient Neu::plateFace(juce::Rectangle<float> r)
{
    return juce::ColourGradient(surface.brighter(light ? 0.02f : 0.04f), r.getX(), r.getY(),
                                surface.darker  (light ? 0.03f : 0.06f), r.getRight(), r.getBottom(), false);
}

void WindowBarLook::install(juce::DocumentWindow& w)
{
   #if JUCE_WINDOWS
    w.setLookAndFeel(this);
    w.setTitleBarHeight(kWindowBarH);
   #else
    w.setUsingNativeTitleBar(true);
   #endif
}

void WindowBarLook::drawDocumentWindowTitleBar(juce::DocumentWindow&, juce::Graphics& g, int, int h, int, int, const juce::Image*, bool)
{
    g.setColour(Neu::accentInk());
    g.setFont(juce::Font(juce::FontOptions(16.f, juce::Font::bold)));
    g.drawText(juce::String(juce::CharPointer_UTF8("\xce\xb1")), 8, 0, 20, h, juce::Justification::centredLeft, false);
}

juce::Button* WindowBarLook::createDocumentWindowButton(int buttonType)
{
    return new WindowBarButton(buttonType);
}

void WindowBarLook::positionDocumentWindowButtons(juce::DocumentWindow&, int x, int y, int w, int h,
                                                  juce::Button* minimise, juce::Button* maximise, juce::Button* close, bool)
{
    const int bw=28;
    int bx=x+w-bw-2;
    for(auto* b : { close, maximise, minimise })
        if(b!=nullptr){ b->setBounds(bx, y, bw, h); bx-=bw; }
}

void WindowBarLook::fillResizableWindowBackground(juce::Graphics& g, int w, int h, const juce::BorderSize<int>&, juce::ResizableWindow&)
{
    g.setGradientFill(Neu::plateFace({ (float)w, (float)h }));   // sans couture avec la plaque de l'éditeur
    g.fillAll();
}

//==============================================================================
LeakLookAndFeel::LeakLookAndFeel()
{
    setColour(juce::Label::textColourId,                      Col::textHi);
    setColour(juce::ComboBox::backgroundColourId,             Col::knobDim);
    setColour(juce::ComboBox::textColourId,                   Col::textMid);
    setColour(juce::ComboBox::outlineColourId,                Col::border);
    setColour(juce::ComboBox::arrowColourId,                  Col::textMid);
    setColour(juce::PopupMenu::backgroundColourId,            Col::panel);
    setColour(juce::PopupMenu::textColourId,                  Col::textMid);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Col::knobDim);
    setColour(juce::PopupMenu::highlightedTextColourId,       Col::accent);
    setColour(juce::TextButton::buttonColourId,               Col::knobDim);
    setColour(juce::TextButton::textColourOffId,              Col::textMid);
    setColour(juce::TextButton::textColourOnId,               Col::accent);
}
void LeakLookAndFeel::drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool isOver,bool isDown)
{ draw3DButtonL(g,b.getLocalBounds().toFloat().reduced(1.f),isDown,b.getToggleState(),Col::knobDim.brighter(isOver?0.12f:0.f),Col::accent); }
void LeakLookAndFeel::drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool isDown)
{
    g.setColour(b.getToggleState()?Col::accent:Col::textMid);
    const float bw=(float)b.getWidth();
    const int textLen=juce::jmax(1,b.getButtonText().length());
    const float maxFsByW=bw/((float)textLen*0.62f);
    const float fs=juce::jlimit(6.f,13.f*Ui::textScale,juce::jmin((float)b.getHeight()*0.42f*Ui::textScale,maxFsByW));
    g.setFont(Ui::icelandAbs(fs));
    g.drawText(b.getButtonText(),b.getLocalBounds().translated(0,isDown?1:0),juce::Justification::centred);
}
void LeakLookAndFeel::drawComboBox(juce::Graphics& g,int w,int h,bool,int,int,int,int,juce::ComboBox&)
{
    juce::Rectangle<float> b(0.f,0.f,(float)w,(float)h);
    g.setColour(Col::knobDim);g.fillRoundedRectangle(b,3.f);
    g.setColour(Col::border);g.drawRoundedRectangle(b.reduced(0.5f),3.f,1.f);
    juce::Path arr;arr.addTriangle((float)w-14.f,(float)h*.5f-3.f,(float)w-8.f,(float)h*.5f-3.f,(float)w-11.f,(float)h*.5f+3.f);
    g.setColour(Col::textMid);g.fillPath(arr);
}
void LeakLookAndFeel::drawPopupMenuBackground(juce::Graphics& g,int w,int h)
{ g.setColour(Col::panel);g.fillRoundedRectangle(0.f,0.f,(float)w,(float)h,3.f);
  g.setColour(Col::border);g.drawRoundedRectangle(0.5f,0.5f,(float)w-1.f,(float)h-1.f,3.f,1.f); }
void LeakLookAndFeel::drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,
                                       float pos,float startAng,float endAng,juce::Slider&)
{
    auto b=juce::Rectangle<float>((float)x,(float)y,(float)w,(float)h).reduced(3.f);
    const float r =juce::jmin(b.getWidth(),b.getHeight())*0.5f;
    const float cx=b.getCentreX(), cy=b.getCentreY();
    const float ang=startAng+pos*(endAng-startAng);

    // ── Corps du knob (dégradé métal brossé), légèrement rétréci pour
    //    laisser la place à l'anneau de LEDs autour ───────────────────────
    const float br=r*0.74f;
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillEllipse(cx-br,cy-br+2.f,br*2,br*2);
    juce::ColourGradient body(Col::knobDim.brighter(0.42f),cx,cy-br,Col::knobDim.darker(0.45f),cx,cy+br,false);
    g.setGradientFill(body); g.fillEllipse(cx-br,cy-br,br*2,br*2);
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.fillEllipse(cx-br*0.72f,cy-br*0.86f,br*1.44f,br*0.82f);
    g.setColour(Col::border); g.drawEllipse(cx-br,cy-br,br*2,br*2,1.f);

    // Petit indicateur central — repère d'orientation rapide
    juce::Point<float> tip(cx+std::sin(ang)*br*0.62f, cy-std::cos(ang)*br*0.62f);
    g.setColour(Col::textHi.withAlpha(0.85f));
    g.drawLine(juce::Line<float>(juce::Point<float>(cx,cy),tip).withShortenedStart(br*0.22f),1.8f);

    // ── Anneau de mini LEDs ─────────────────────────────────────────────
    const int   numLeds = 15;
    const float ringR   = r*0.96f;
    const float ledR    = juce::jmax(1.0f, r*0.085f);
    for(int i=0;i<numLeds;++i){
        const float t=(float)i/(float)(numLeds-1);
        const float ledAng=startAng+t*(endAng-startAng);
        const float lx=cx+std::sin(ledAng)*ringR;
        const float ly=cy-std::cos(ledAng)*ringR;
        const bool  lit=(t<=pos+1e-4f);
        if(lit){
            g.setColour(Col::accent.withAlpha(0.30f));
            g.fillEllipse(lx-ledR*2.0f,ly-ledR*2.0f,ledR*4.0f,ledR*4.0f);   // halo
            g.setColour(Col::accent);
            g.fillEllipse(lx-ledR,ly-ledR,ledR*2.f,ledR*2.f);
        } else {
            g.setColour(Col::knobDim.darker(0.65f));
            g.fillEllipse(lx-ledR,ly-ledR,ledR*2.f,ledR*2.f);
            g.setColour(Col::border.withAlpha(0.55f));
            g.drawEllipse(lx-ledR,ly-ledR,ledR*2.f,ledR*2.f,0.6f);
        }
    }
}

//==============================================================================
// NeuLookAndFeel — contrôles du châssis neumorphique
//==============================================================================
void NeuLookAndFeel::drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool isOver,bool isDown)
{
    // marge réservée dans les bounds pour le relief (pas de dessin hors limites)
    auto r=b.getLocalBounds().toFloat().reduced(2.5f);
    const float rad=juce::jmin(r.getHeight()*0.5f, 9.f);
    if(b.getToggleState() || isDown) Neu::drawInset (g, r, rad, 1.5f);
    else                             Neu::drawRaised(g, r, rad, isOver ? 2.2f : 1.6f);
}
void NeuLookAndFeel::drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool isDown)
{
    g.setColour(b.getToggleState() ? Neu::accentInk() : Neu::text.withAlpha(0.85f));
    const float bw=(float)b.getWidth();
    const int textLen=juce::jmax(1,b.getButtonText().length());
    const float maxFsByW=(bw-6.f)/((float)textLen*0.62f);
    const float fs=juce::jlimit(7.f,15.f*Ui::textScale,juce::jmin((float)b.getHeight()*0.5f*Ui::textScale,maxFsByW));
    g.setFont(Ui::icelandAbs(fs));
    g.drawText(b.getButtonText(),b.getLocalBounds().translated(0,isDown?1:0),juce::Justification::centred);
}
void NeuLookAndFeel::drawComboBox(juce::Graphics& g,int w,int h,bool,int,int,int,int,juce::ComboBox&)
{
    auto r=juce::Rectangle<float>(0.f,0.f,(float)w,(float)h).reduced(1.5f);
    Neu::drawInset(g, r, juce::jmin(r.getHeight()*0.5f, 8.f), 1.4f);
    juce::Path arr;arr.addTriangle((float)w-15.f,(float)h*.5f-2.5f,(float)w-8.f,(float)h*.5f-2.5f,(float)w-11.5f,(float)h*.5f+2.5f);
    g.setColour(Neu::textDim);g.fillPath(arr);
}
void NeuLookAndFeel::drawComboBoxTextWhenNothingSelected(juce::Graphics& g,juce::ComboBox& box,juce::Label& l)
{
    // texte d'invite ("PRESET") : teinte du châssis, lisible en clair comme en sombre
    g.setColour(Neu::textDim);
    const auto font=getLabelFont(l);
    g.setFont(font);
    const auto area=getLabelBorderSize(l).subtractedFrom(l.getBounds());
    g.drawFittedText(box.getTextWhenNothingSelected(), area, l.getJustificationType(),
                     juce::jmax(1,(int)((float)area.getHeight()/font.getHeight())), l.getMinimumHorizontalScale());
}
void NeuLookAndFeel::drawLabel(juce::Graphics& g,juce::Label& l)
{
    if(l.isBeingEdited()) return;
    const bool inCombo=dynamic_cast<juce::ComboBox*>(l.getParentComponent())!=nullptr;
    g.setColour(inCombo ? Neu::text : Neu::textDim);
    const auto font=getLabelFont(l);
    g.setFont(font);
    g.drawFittedText(l.getText(), getLabelBorderSize(l).subtractedFrom(l.getLocalBounds()),
                     l.getJustificationType(), juce::jmax(1,(int)((float)l.getHeight()/font.getHeight())),
                     l.getMinimumHorizontalScale());
}
void NeuLookAndFeel::drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,
                                      float pos,float startAng,float endAng,juce::Slider&)
{
    auto b=juce::Rectangle<float>((float)x,(float)y,(float)w,(float)h).reduced(2.f);
    const float r =juce::jmin(b.getWidth(),b.getHeight())*0.5f;
    const float cx=b.getCentreX(), cy=b.getCentreY();
    const float ang=startAng+pos*(endAng-startAng);

    // piste creusée + arc de valeur (accent)
    const float trackR=r*0.90f;
    juce::Path track; track.addCentredArc(cx,cy,trackR,trackR,0.f,startAng,endAng,true);
    g.setColour(Neu::well);
    g.strokePath(track, juce::PathStrokeType(r*0.16f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc(cx,cy,trackR,trackR,0.f,startAng,ang,true);
    g.setColour(Neu::accentInk());
    g.strokePath(val, juce::PathStrokeType(r*0.10f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // corps en relief, face plate
    const float kr=r*0.62f;
    Neu::drawRaised(g, {cx-kr,cy-kr,kr*2.f,kr*2.f}, kr, juce::jmax(1.2f, kr*0.16f));

    // repère : point creusé
    const float dr=juce::jmax(1.5f, kr*0.14f);
    const juce::Point<float> dot(cx+std::sin(ang)*kr*0.62f, cy-std::cos(ang)*kr*0.62f);
    g.setColour(Neu::accentInk());
    g.fillEllipse(dot.x-dr, dot.y-dr, dr*2.f, dr*2.f);
}

//==============================================================================
// VectorscopeDisplay
//==============================================================================
VectorscopeDisplay::VectorscopeDisplay(LeakAudioProcessor& p) : processor(p)
{
    lineBtn.setClickingTogglesState(true);
    lineMode = processor.vecLineMode;
    lineBtn.setToggleState(lineMode, juce::dontSendNotification);
    releaseAlpha = processor.vecReleaseAlpha;
    lineBtn.onClick=[this]{ lineMode=lineBtn.getToggleState(); processor.vecLineMode=lineMode; trailValid=false; };
    addAndMakeVisible(lineBtn);
    startTimerHz(60);
}
void VectorscopeDisplay::resized()
{
    trailValid=false;
    lineBtn.setBounds(getWidth()-32, 2, 30, 12);
}
void VectorscopeDisplay::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    releaseAlpha = juce::jlimit(0.50f, 0.98f, releaseAlpha + w.deltaY * 0.04f);
    processor.vecReleaseAlpha = releaseAlpha;
    repaint();
}
void VectorscopeDisplay::syncFromProcessor()
{
    lineMode = processor.vecLineMode;
    lineBtn.setToggleState(lineMode, juce::dontSendNotification);
    releaseAlpha = processor.vecReleaseAlpha;
    trailValid = false;
    repaint();
}
void VectorscopeDisplay::timerCallback()
{
    processor.getVectorData(pts,ptCount);
    double sLR=0,sLL=0,sRR=0;
    bool anyActive=false;
    for(int i=0;i<ptCount;++i){
        const float L=pts[i].m+pts[i].s, R=pts[i].m-pts[i].s;
        sLR+=L*R; sLL+=L*L; sRR+=R*R;
        if(std::abs(pts[i].m)>1e-4f || std::abs(pts[i].s)>1e-4f) anyActive=true;
    }
    const float c=(sLL>1e-9&&sRR>1e-9)?(float)(sLR/std::sqrt(sLL*sRR)):0.f;
    corr+=(c-corr)*0.20f;

    if(anyActive) silentFrames=0;
    else if(silentFrames<SILENT_FRAMES_MAX) ++silentFrames;

    // Optimisation CPU/GPU : une fois le trail bien décru en silence,
    // on arrête de retoucher l'image entière à chaque frame.
    if(anyActive || silentFrames<SILENT_FRAMES_MAX){
        renderTrail();
        repaint();
    }
}
void VectorscopeDisplay::renderTrail()
{
    const int W=getWidth(),H=getHeight();
    if(W<10||H<10) return;
    if(!trailValid || trailImage.getWidth()!=W || trailImage.getHeight()!=H){
        // image logicielle : retouchée pixel à pixel à chaque image
        // (multiplyAllAlphas) — une image Direct2D ferait un aller-retour GPU
        trailImage=juce::Image(juce::Image::ARGB,W,H,true,juce::SoftwareImageType());
        trailValid=true;
    }
    trailImage.multiplyAllAlphas(releaseAlpha);
    juce::Graphics g(trailImage);
    const float cx=W*0.5f, cy=H*0.5f, r=juce::jmin(cx,cy)*0.86f;
    if(lineMode){
        for(int i=1;i<ptCount;++i){
            const float m0=pts[i-1].m, s0=pts[i-1].s;
            const float n0=std::sqrt(m0*m0+s0*s0);
            if(n0<1e-5f) continue;
            const float sc0=(n0>1.f)?1.f/n0:1.f;
            const float m1=pts[i].m, s1=pts[i].s;
            const float n1=std::sqrt(m1*m1+s1*s1);
            if(n1<1e-5f) continue;
            const float sc1=(n1>1.f)?1.f/n1:1.f;
            const float px0=cx-s0*sc0*r, py0=cy-m0*sc0*r;
            const float px1=cx-s1*sc1*r, py1=cy-m1*sc1*r;
            const float a=juce::jlimit(0.08f,0.88f,(n0+n1)*1.0f);
            const float k=juce::jlimit(0.f,1.f,(n0+n1)*0.8f);
            g.setColour(Col::hot(Col::accent,k).withAlpha(a));
            g.drawLine(px0,py0,px1,py1,0.9f);
        }
    } else {
        for(int i=0;i<ptCount;++i){
            const float m=pts[i].m, s=pts[i].s;
            const float norm=std::sqrt(m*m+s*s);
            if(norm<1e-5f) continue;
            const float scale=(norm>1.f)?1.f/norm:1.f;
            const float px=cx-s*scale*r, py=cy-m*scale*r;
            const float k=juce::jlimit(0.f,1.f,norm*1.6f);
            const float a=juce::jlimit(0.10f,0.95f,norm*2.4f);
            g.setColour(Col::hot(Col::accent,k).withAlpha(a));
            g.fillEllipse(px-1.1f,py-1.1f,2.2f,2.2f);
        }
    }
}
void VectorscopeDisplay::paint(juce::Graphics& g)
{
    const int W=getWidth(),H=getHeight();
    if(W<10||H<10) return;
    const float cx=W*0.5f, cy=H*0.5f;
    const float r=juce::jmin(cx,cy)*0.86f;

    // Fond fixe (dégradé, cercles, axes, repères) rendu une fois dans une image
    // native à la résolution physique ; refait si taille / échelle / thème change.
    const float pxScale=g.getInternalContext().getPhysicalPixelScaleFactor();
    const juce::int64 key=(((juce::int64)W*8191+H)*131+juce::roundToInt(pxScale*100.f))*31
                          +juce::roundToInt(Ui::textScale*100.f)
                          +(juce::int64)Col::grid.getARGB()*3+(juce::int64)Col::gridCtr.getARGB()*5
                          +(juce::int64)Col::generation*7919;
    if(!bgCache.isValid() || key!=bgKey){
        bgKey=key;
        bgCache=juce::Image(juce::Image::RGB, juce::jmax(1,juce::roundToInt((float)W*pxScale)), juce::jmax(1,juce::roundToInt((float)H*pxScale)), false);
        juce::Graphics bg(bgCache);
        bg.addTransform(juce::AffineTransform::scale(pxScale));
        paintBackground(bg, cx, cy, r);
    }
    g.drawImage(bgCache, getLocalBounds().toFloat());
    paintForeground(g, cx, cy, r);
}

void VectorscopeDisplay::paintBackground(juce::Graphics& g, float cx, float cy, float r)
{
    juce::ColourGradient bgGrad(Col::pick(juce::Colour(0xFF04130F), Col::bg),cx,cy,Col::pick(juce::Colour(0xFF010706), Col::panel),cx,cy+r*1.3f,true);
    g.setGradientFill(bgGrad); g.fillRect(getLocalBounds());
    g.setColour(Col::accent.withAlpha(0.04f));
    g.fillEllipse(cx-r*1.02f,cy-r*1.02f,r*2.04f,r*2.04f);
    g.setColour(Col::grid.withAlpha(0.9f));
    for(float k : {0.66f,0.33f}) g.drawEllipse(cx-r*k,cy-r*k,r*2*k,r*2*k,0.6f);
    // Anneau extérieur en blanc — plus stylé, se détache nettement (comme la maquette)
    g.setColour(Col::light(0.35f));
    g.drawEllipse(cx-r,cy-r,r*2,r*2,1.1f);
    g.setColour(Col::gridCtr.withAlpha(0.8f));
    g.drawLine(cx,cy-r,cx,cy+r,0.6f);
    g.drawLine(cx-r,cy,cx+r,cy,0.6f);
    g.setColour(Col::grid.withAlpha(0.6f));
    g.drawLine(cx-r*0.707f,cy-r*0.707f,cx+r*0.707f,cy+r*0.707f,0.6f);
    g.drawLine(cx+r*0.707f,cy-r*0.707f,cx-r*0.707f,cy+r*0.707f,0.6f);
    g.setFont(Ui::iceland((9.5f)));
    g.setColour(Col::light(0.7f));
    // "M" au-dessus du cercle, sauf s'il tombe sur le titre (gros texte,
    // module large et bas) : il passe alors juste sous le haut de l'anneau
    {
        const float titleRight=6.f+juce::GlyphArrangement::getStringWidth(Ui::iceland(10.5f).withExtraKerningFactor(0.08f), TR("VECTORSCOPE"));
        const bool mInside=cy-r-13.f < 5.f+13.f*Ui::textScale && (cx-8.f < titleRight+4.f || Ui::textScale>1.f);
        g.drawText("M",(int)(cx-8),(int)(mInside ? cy-r+3.f : cy-r-13.f),16,11,juce::Justification::centred);
    }
    // L à gauche, R à droite — sens standard d'un vectorscope
    g.drawText("R",(int)(cx+r*0.707f-2),(int)(cy-r*0.707f-13),14,11,juce::Justification::centred);
    g.drawText("L",(int)(cx-r*0.707f-12),(int)(cy-r*0.707f-13),14,11,juce::Justification::centred);
}

// Partie animée : traînée, jauge de corrélation, titre (même ordre qu'avant)
void VectorscopeDisplay::paintForeground(juce::Graphics& g, float cx, float cy, float r)
{
    const int H=getHeight();
    if(trailValid) g.drawImageAt(trailImage,0,0);
    if(Ui::corrStyle!=0) Ui::drawCorrRing(g, cx, cy, r, corr, (float)H);
    else {
        // Jauge de corrélation : indicateur purement graphique, sans aucun texte.
        const float mh=2.5f, my=(float)H-mh-8.f, mx=cx-r*0.80f, mw=r*1.60f;
        g.setColour(Col::knobDim.darker(0.3f));
        g.fillRoundedRectangle(mx,my,mw,mh,1.f);
        const float t=(corr*0.5f+0.5f);
        juce::Colour cc=(corr>=0.f)?Col::accent.interpolatedWith(Col::ink(juce::Colour(0xFFFFD200)),1.f-corr):Col::ink(juce::Colour(0xFFFF5530));
        g.setColour(cc.withAlpha(0.92f));
        g.fillRect(mx+t*mw-1.5f,my-0.5f,3.f,mh+1.f);
        g.setColour(Col::gridCtr.withAlpha(0.45f));
        g.drawLine(mx+mw*0.5f,my,mx+mw*0.5f,my+mh,0.5f);
    }
    Ui::drawTitleDecor(g, (float)getWidth(), Ui::kTitleLineY, Ui::titleEndBeforeRing(cx, cy, r));
    g.setFont(Ui::iceland((10.5f)).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    g.drawFittedText(TR("VECTORSCOPE"),6,5,getWidth()-12,13,juce::Justification::left,1,0.7f);
}

//==============================================================================
// MultibandVectorscopeDisplay
//==============================================================================
MultibandVectorscopeDisplay::MultibandVectorscopeDisplay(LeakAudioProcessor& p) : processor(p)
{
    releaseAlpha = processor.mbVecReleaseAlpha;
    startTimerHz(60);
}
void MultibandVectorscopeDisplay::resized()
{
    for(int b=0;b<3;++b) trailValid[b]=false;
}
void MultibandVectorscopeDisplay::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    releaseAlpha = juce::jlimit(0.50f, 0.98f, releaseAlpha + w.deltaY*0.04f);
    processor.mbVecReleaseAlpha = releaseAlpha;
}
void MultibandVectorscopeDisplay::getBandCircle(int band, float& cx, float& cy, float& r) const
{
    const int W=getWidth(), H=getHeight();
    const float titleH=16.f, labelH=14.f, gap=6.f;
    const float colW=((float)W-2.f*gap)/3.f;
    const float x0=(float)band*(colW+gap);
    cx = x0 + colW*0.5f;
    const float availH = (float)H - titleH - labelH;
    cy = titleH + availH*0.5f;
    r = juce::jmax(4.f, juce::jmin(colW*0.46f, availH*0.46f));
}
void MultibandVectorscopeDisplay::timerCallback()
{
    bool needsRepaint=false;
    for(int b=0;b<3;++b){
        processor.getMultibandVectorData(b,pts[b],ptCount[b]);
        bool active=false;
        for(int i=0;i<ptCount[b];++i)
            if(std::abs(pts[b][i].m)>1e-4f || std::abs(pts[b][i].s)>1e-4f){active=true;break;}
        if(active) silentFrames[b]=0;
        else if(silentFrames[b]<SILENT_FRAMES_MAX) ++silentFrames[b];
        if(active || silentFrames[b]<SILENT_FRAMES_MAX){
            renderTrail(b);
            needsRepaint=true;
        }
    }
    if(needsRepaint) repaint();
}
void MultibandVectorscopeDisplay::renderTrail(int band)
{
    const int W=getWidth(),H=getHeight();
    if(W<30||H<30) return;
    if(!trailValid[band] || trailImage[band].getWidth()!=W || trailImage[band].getHeight()!=H){
        trailImage[band]=juce::Image(juce::Image::ARGB,W,H,true,juce::SoftwareImageType());   // logicielle (cf. Vectorscope)
        trailValid[band]=true;
    }
    trailImage[band].multiplyAllAlphas(releaseAlpha);
    juce::Graphics g(trailImage[band]);
    float cx,cy,r; getBandCircle(band,cx,cy,r);
    for(int i=0;i<ptCount[band];++i){
        const float m=pts[band][i].m, s=pts[band][i].s;
        const float norm=std::sqrt(m*m+s*s);
        if(norm<1e-5f) continue;
        const float scale=(norm>1.f)?1.f/norm:1.f;
        const float px=cx-s*scale*r, py=cy-m*scale*r;
        const float k=juce::jlimit(0.f,1.f,norm*1.6f);
        const float a=juce::jlimit(0.10f,0.95f,norm*2.4f);
        g.setColour(Col::hot(Col::accent,k).withAlpha(a));
        g.fillEllipse(px-1.0f,py-1.0f,2.0f,2.0f);
    }
}
void MultibandVectorscopeDisplay::paint(juce::Graphics& g)
{
    const int W=getWidth(),H=getHeight();
    if(W<30||H<30) return;
    g.setColour(Col::bg); g.fillRect(getLocalBounds());
    static const char* labels[3]={"LOW","MID|band","HIGH"};
    for(int b=0;b<3;++b){
        float cx,cy,r; getBandCircle(b,cx,cy,r);
        juce::ColourGradient bgGrad(Col::pick(juce::Colour(0xFF04130F), Col::bg),cx,cy,Col::pick(juce::Colour(0xFF010706), Col::panel),cx,cy+r*1.3f,true);
        g.setGradientFill(bgGrad); g.fillEllipse(cx-r,cy-r,r*2,r*2);
        g.setColour(Col::grid.withAlpha(0.9f));
        for(float k : {0.66f,0.33f}) g.drawEllipse(cx-r*k,cy-r*k,r*2*k,r*2*k,0.5f);
        g.setColour(Col::light(0.30f));
        g.drawEllipse(cx-r,cy-r,r*2,r*2,1.0f);
        g.setColour(Col::gridCtr.withAlpha(0.7f));
        g.drawLine(cx,cy-r,cx,cy+r,0.5f);
        g.drawLine(cx-r,cy,cx+r,cy,0.5f);
        if(trailValid[b]) g.drawImageAt(trailImage[b],0,0);
        g.setFont(Ui::iceland((9.f)));
        g.setColour(Col::textMid.withAlpha(0.85f));
        g.drawFittedText(TR(labels[b]),(int)(cx-r),(int)(cy+r+2),(int)(r*2),12,juce::Justification::centred,1,0.7f);
    }
    Ui::drawTitleDecor(g, (float)W);
    g.setFont(Ui::iceland((10.5f)).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    g.drawFittedText(TR("MULTIBAND VECTORSCOPE"), 6,5,W-12,13,juce::Justification::left,1,0.7f);
}

//==============================================================================
// Spectrogram3DBase
//==============================================================================
Spectrogram3DBase::Spectrogram3DBase(LeakAudioProcessor& p, bool& reverseFlag, float& tiltRef, float& skewRef, float& timeRef)
    : processor(p), reverseScroll(reverseFlag), tiltAmount(tiltRef), skewAmount(skewRef), timeSec(timeRef)
{
    // TIME : 1 à 20 s (milieu de course 4 s), 2.1 s par défaut (double-clic)
    timeFader.configure("TIME", 1.f, 20.f, 4.f, 2.1f);
    timeFader.setValue(timeSec, juce::dontSendNotification);
    timeFader.onValueChange=[this](float v){ timeSec=v; repaint(); };   // échelle de temps
    addAndMakeVisible(timeFader);
    // HOLD : fige l'image pour l'examiner (le survol reste actif)
    holdBtn.setClickingTogglesState(true);
    holdBtn.onClick=[this]{ frozen=holdBtn.getToggleState(); };
    addAndMakeVisible(holdBtn);
    lastCaptureMs = juce::Time::getMillisecondCounterHiRes();
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    startTimerHz(60);
}

juce::Colour Col::paperHeat(float t)
{
    static const juce::uint8 stops[8][3] = {
        {236,241,245},{170,222,205},{45,179,138},{140,198,63},{241,194,50},{240,138,44},{224,65,47},{150,24,44} };
    t=juce::jlimit(0.f,1.f,t)*7.f;
    const int i=juce::jmin(6,(int)t);
    const float f=t-(float)i;
    auto mix=[&](int c){ return (juce::uint8)juce::roundToInt((float)stops[i][c]+((float)stops[i+1][c]-(float)stops[i][c])*f); };
    return juce::Colour(mix(0),mix(1),mix(2));
}

juce::Colour Spectrogram3DBase::inferno(float t)
{
    if(Col::paper) return Col::paperHeat(t);
    static const juce::uint8 stops[8][3] = {
        {0,0,4},{40,11,84},{101,21,110},{159,42,99},{212,72,66},{245,125,21},{250,193,39},{252,255,164} };
    t=juce::jlimit(0.f,1.f,t)*7.f;
    const int i=juce::jmin(6,(int)t);
    const float f=t-(float)i;
    auto mix=[&](int c){ return (juce::uint8)juce::roundToInt((float)stops[i][c]+((float)stops[i+1][c]-(float)stops[i][c])*f); };
    return juce::Colour(mix(0),mix(1),mix(2));
}

void Spectrogram3DBase::rebuildBinMap(double sr)
{
    mappedSampleRate=sr;
    const float binHz=(float)sr/(float)LeakAudioProcessor::PSD_FFT_SIZE;
    // 20 Hz -> 20 kHz, ou jusqu'à la moitié de la fréquence d'échantillonnage au-delà de 48 kHz
    const float ratio=PsdLogMap::topHz(sr)/20.f;
    const float stepOct=std::log2(ratio)/(float)(NPTS-1);
    for(int p=0;p<NPTS;++p){
        const float f=20.f*std::pow(ratio,(float)p/(float)(NPTS-1));
        const float lo=f*std::pow(2.f,-stepOct*0.5f)/binHz, hi=f*std::pow(2.f,stepOct*0.5f)/binHz;
        if(hi-lo>1.5f){                      // plusieurs bins : on prend le max de la plage
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

void Spectrogram3DBase::drawDirectionButton(juce::Graphics& g)
{
    const int W=getWidth();
    const float bs=16.f;
    dirBtnBounds = juce::Rectangle<float>((float)W-bs-6.f, 4.f, bs, bs);
    g.setColour(Col::light(0.06f));
    g.fillRoundedRectangle(dirBtnBounds,3.f);
    g.setColour(Col::border);
    g.drawRoundedRectangle(dirBtnBounds.reduced(0.5f),3.f,1.f);
    juce::Path arrow;
    const float cx=dirBtnBounds.getCentreX(), cy=dirBtnBounds.getCentreY(), s=4.5f;
    if(reverseScroll) arrow.addTriangle(cx-s,cy, cx+s*0.4f,cy-s, cx+s*0.4f,cy+s);
    else               arrow.addTriangle(cx+s,cy, cx-s*0.4f,cy-s, cx-s*0.4f,cy+s);
    g.setColour(Col::accent.withAlpha(0.85f));
    g.fillPath(arrow);
}
void Spectrogram3DBase::mouseDown(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown()) return;  // laisse remonter pour le menu de slot
    tiltAtDragStart = tiltAmount;
    skewAtDragStart = skewAmount;
    dragFromTitle = e.position.y < Ui::titleStripHeight();   // titre : détachement, pas de perspective
    if(dirBtnBounds.contains(e.position)){
        reverseScroll = !reverseScroll;
        repaint();
    }
}
void Spectrogram3DBase::mouseDrag(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown() || dragFromTitle) return;
    // glisser verticalement : hauteur de la perspective ; horizontalement :
    // décalage latéral des courbes anciennes
    tiltAmount = juce::jlimit(0.f, 1.8f, tiltAtDragStart - (float)e.getDistanceFromDragStartY()*0.004f);
    skewAmount = juce::jlimit(-1.f, 1.f, skewAtDragStart + (float)e.getDistanceFromDragStartX()*0.004f);
    repaint();
}

void Spectrogram3DBase::timerCallback()
{
    if(frozen) return;   // HOLD : image figée (glideFrac gardé)
    const double now=juce::Time::getMillisecondCounterHiRes();
    const double cap=captureMs();
    // Entre deux courbes, l'image glisse en continu (repeinte à chaque tic) ;
    // avant, elle n'avançait qu'à chaque nouvelle courbe, par à-coups.
    const bool due=now-lastCaptureMs >= cap;
    if(!due){
        glideFrac=(float)juce::jlimit(0.0, 1.0, (now-lastCaptureMs)/cap);
        if(silentFrames<SILENT_FRAMES_MAX) repaint();
        return;
    }
    // pas fixe (défilement régulier) ; recalage seulement après un vrai retard
    lastCaptureMs = now-lastCaptureMs > 4.0*cap ? now : lastCaptureMs+cap;
    glideFrac=(float)juce::jlimit(0.0, 1.0, (now-lastCaptureMs)/cap);

    const double sr=processor.getCurrentSampleRate();
    if(sr>0.0 && sr!=mappedSampleRate){ rebuildBinMap(sr); rowCount=0; }   // l'historique suivait l'ancien axe
    if(mappedSampleRate<=0.0) return;

    processor.getPSDData(psd, LeakAudioProcessor::PSD_NUM_BINS);
    head=(head+1)%NROWS;
    ++captureTotal;
    float* r=rows[head];
    bool active=false;
    for(int p=0;p<NPTS;++p){
        float db;
        if(binFrac[p]<0.f){
            db=-120.f;
            for(int b=binLo[p];b<binHi[p];++b) db=juce::jmax(db,psd[b]);
        } else {
            db=psd[binLo[p]]+(psd[binHi[p]]-psd[binLo[p]])*binFrac[p];
        }
        if(db>-90.f) active=true;
        r[p]=juce::jlimit(0.f,1.f,(db+100.f)/90.f);   // -100 dB -> 0, -10 dB -> 1
    }
    rowCount=juce::jmin(rowCount+1,NROWS);

    if(active) silentFrames=0;
    else if(silentFrames<SILENT_FRAMES_MAX) ++silentFrames;
    if(active || silentFrames<SILENT_FRAMES_MAX) repaint();
}

void Spectrogram3DBase::resized()
{
    // fader TIME sur le bord gauche, de la ligne du haut à la base des courbes
    const float labelH=14.f*Ui::textScale;
    const int top=26, base=juce::roundToInt((float)getHeight()-labelH-6.f);
    timeFader.setBounds(2, top, (int)kReleaseFaderW-4, juce::jmax(10, base-top));
    // HOLD dans l'en-tête, à gauche du bouton de sens de défilement
    const int hh=juce::jlimit(16, 22, juce::roundToInt(19.f*Ui::textScale));
    const int hw=juce::roundToInt(46.f*Ui::textScale);
    holdBtn.setBounds(getWidth()-6-16-6-hw, 2, hw, hh);
}

void Spectrogram3DBase::paintCurves(juce::Graphics& g, const char* title, bool filled)
{
    const int W=getWidth(), H=getHeight();
    if(W<40||H<40) return;
    g.setColour(Col::bg); g.fillRect(getLocalBounds());

    // géométrie : profondeur 0 = devant (en bas), 1 = fond (en haut, décalé
    // sur le côté) ; la zone commence après le fader TIME à gauche
    const float labelH=14.f*Ui::textScale;
    const float top=26.f, base=(float)H-labelH-6.f;
    const float L0=kReleaseFaderW, aw=(float)W-L0;
    const float pw=aw*0.86f;   // plus large (0.80 avant) : le relief remplit mieux le module
    const float spare=aw-20.f-pw;
    const float dir=reverseScroll ? -1.f : 1.f;
    const float x0=reverseScroll ? (float)W-10.f-pw : L0+10.f;
    const float totalDx=spare*(0.6f+0.4f*skewAmount);
    const float totalDy=(base-top)*(0.22f+0.30f*juce::jmin(1.f,tiltAmount));
    const float peakH=juce::jmax(10.f,(base-top)-totalDy-4.f);
    const int   n=rowCount;

    // plateau 0 : sol plat bleuté quadrillé sous les courbes (visible là où le
    // niveau est au plus bas), plus clair vers l'avant
    {
        const juce::Point<float> fl(x0, base), fr(x0+pw, base),
                                 bl(x0+dir*totalDx, base-totalDy), br(x0+dir*totalDx+pw, base-totalDy);
        juce::Path floor;
        floor.startNewSubPath(fl); floor.lineTo(fr); floor.lineTo(br); floor.lineTo(bl); floor.closeSubPath();
        g.setGradientFill(juce::ColourGradient(Col::pick(juce::Colour(0xFF2E2B66), juce::Colour(0xFFE4E9F0)), 0.f, base-totalDy,
                                               Col::pick(juce::Colour(0xFF6A6FC8), juce::Colour(0xFFCBD4E1)), 0.f, base, false));
        g.setOpacity(0.85f);
        g.fillPath(floor);
        g.setOpacity(1.f);
        g.setColour(Col::pick(juce::Colour(0xFFC8D0FF).withAlpha(0.13f), Col::textHi.withAlpha(0.10f)));
        for(int k=0;k<=16;++k){
            const float u=(float)k/16.f;
            g.drawLine(juce::Line<float>(fl+(fr-fl)*u, bl+(br-bl)*u), 0.6f);
        }
        // lignes de temps : une toutes les 4 courbes, attachée à sa courbe
        // (elle avance avec elle, comme sur les spectrogrammes GPU)
        for(int r=0;r<NROWS;++r){
            if(((captureTotal-(juce::uint32)r) & 3u)!=0u) continue;
            const float d=((float)r+glideFrac)/(float)(NROWS-1);   // 0 = au fond (la plus récente)
            if(d>1.f) continue;
            g.drawLine(juce::Line<float>(bl+(fl-bl)*d, br+(fr-br)*d), 0.6f);
        }
    }

    juce::Path p;
    // la plus récente naît au fond (profondeur 1) et l'historique avance vers
    // l'avant ; dessin du fond vers l'avant (chaque courbe masque les précédentes)
    for(int r=0;r<n;++r){
        const float* row=rows[(head-r+NROWS)%NROWS];
        const float depth=1.f-((float)r+glideFrac)/(float)(NROWS-1);   // glisse en continu vers l'avant
        if(depth<0.f) continue;                                           // sortie par l'avant
        const float ox=x0+dir*totalDx*depth, oy=base-totalDy*depth;
        const float a=1.f-(float)r/(float)NROWS;   // 1 = récente, 0 = ancienne

        p.clear();
        p.startNewSubPath(ox, oy);
        for(int i=0;i<NPTS;++i)
            p.lineTo(ox+(float)i/(float)(NPTS-1)*pw, oy-row[i]*peakH);
        p.lineTo(ox+pw, oy);
        p.closeSubPath();

        if(filled){
            // surface : dégradé d'amplitude (bas sombre -> haut chaud), plus
            // transparente au fond
            juce::ColourGradient sg(inferno(Col::paper ? 0.28f : 0.12f).withAlpha(0.95f), 0.f, oy,   // papier : pied coloré, pas blanc
                                    inferno(0.95f), 0.f, oy-peakH, false);
            sg.addColour(0.35, inferno(0.45f));
            sg.addColour(0.65, inferno(0.72f));
            g.setGradientFill(sg);
            g.setOpacity(Col::paper ? 0.80f+0.20f*a : 0.35f+0.65f*a);   // papier : sans voile blanc
            g.fillPath(p);
            g.setOpacity(1.f);
            g.setColour(Col::pick(juce::Colours::black.withAlpha(0.35f), Col::textHi.withAlpha(0.30f)));
            g.strokePath(p, juce::PathStrokeType(0.8f));
        } else {
            // lignes : fond masquant (occultation) + trait coloré par ancienneté
            g.setColour(Col::bg.withAlpha(0.88f));
            g.fillPath(p);
            if(Col::paper) g.setColour(inferno(0.40f+0.60f*a).withAlpha(0.45f+0.55f*a));
            else           g.setColour(inferno(0.25f+0.75f*a).withAlpha(0.25f+0.75f*a));
            g.strokePath(p, juce::PathStrokeType(r==0 ? 1.8f : 1.f));
        }
    }

    // repères de fréquence sous la courbe de devant (sans chevauchement)
    g.setFont(Ui::iceland(9.f));
    SpectroReadout::drawFreqMarks(g, axisTopHz(), [&](float f, juce::Point<float>& at){
                                      at={ x0+std::log(f/20.f)/std::log(axisTopHz()/20.f)*pw, base+3.f }; return true; },
                                  labelH, { L0, 0.f, (float)W-L0, (float)H });

    // échelle de temps le long du bord droit : 0 s au fond (la plus récente),
    // jusqu'à -TIME devant
    {
        const float T=juce::jmax(0.5f, timeSec), step=SpectroReadout::timeTickStep(T);
        g.setFont(Ui::iceland(8.5f));
        for(float t=0.f; t<=T+1e-3f; t+=step){
            const float d=1.f-t/T;
            const float ex=x0+dir*totalDx*d+pw, ey=base-totalDy*d;
            g.setColour(Col::textMid.withAlpha(0.6f));
            g.drawLine(ex+2.f, ey, ex+6.f, ey, 1.f);
            const auto lbl=SpectroReadout::formatTimeTick(t, step);
            const float w=juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), lbl)+2.f;
            const float lx=juce::jmin(ex+8.f, (float)W-w-2.f);
            SpectroReadout::drawShadowedText(g, lbl, { lx, ey-6.f, w, 12.f }, juce::Justification::centredLeft, Col::textMid.withAlpha(0.85f));
        }
    }

    drawDirectionButton(g);
    Ui::drawTitleDecor(g, (float)getWidth(), (float)holdBtn.getBottom()+2.f);   // sous HOLD
    g.setFont(Ui::iceland(10.5f).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    Ui::drawTitleFit(g, TR(title), { 6.f, 5.f, (float)juce::jmax(20, holdBtn.getX()-12), 13.f }, g.getCurrentFont());   // plutôt que "…"

    // Survol : la courbe visible sous la souris est la plus en avant qui couvre
    // le pixel (chaque courbe masque celles de derrière).
    if(hovering && n>0 && !dirBtnBounds.contains(hoverPos)){
        for(int r=n-1;r>=0;--r){   // de l'avant (la plus ancienne) vers le fond
            const float* row=rows[(head-r+NROWS)%NROWS];
            const float depth=1.f-((float)r+glideFrac)/(float)(NROWS-1);
            if(depth<0.f) continue;
            const float ox=x0+dir*totalDx*depth, oy=base-totalDy*depth;
            const float fi=(hoverPos.x-ox)/pw*(float)(NPTS-1);
            if(fi<0.f || fi>(float)(NPTS-1)) continue;
            const int   i0=juce::jmin(NPTS-2,(int)fi);
            const float v=row[i0]+(row[i0+1]-row[i0])*(fi-(float)i0);
            const float topY=oy-v*peakH;
            if(hoverPos.y<topY-2.f || hoverPos.y>oy) continue;

            // repère : trait vertical jusqu'à la base de la courbe + point
            g.setColour(Col::accent.withAlpha(0.55f));
            g.drawLine(hoverPos.x, topY, hoverPos.x, oy, 1.f);
            g.setColour(juce::Colours::white);
            g.fillEllipse(juce::Rectangle<float>(7.f,7.f).withCentre({hoverPos.x, topY}));
            g.setColour(Col::accent);
            g.drawEllipse(juce::Rectangle<float>(9.f,9.f).withCentre({hoverPos.x, topY}), 1.2f);

            SpectroReadout::draw(g, getLocalBounds().toFloat(), { hoverPos.x, topY },
                                 20.f*std::pow(axisTopHz()/20.f, fi/(float)(NPTS-1)), v*90.f-100.f,
                                 (float)r*(float)captureMs()*0.001f);
            break;
        }
    }
}

//==============================================================================
// Spectrogram3DPeaksDisplay / Spectrogram3DSurfaceDisplay
//==============================================================================
void Spectrogram3DPeaksDisplay::paint(juce::Graphics& g)   { paintCurves(g, "SPECTROGRAM 3D - LINES",   false); }
void Spectrogram3DSurfaceDisplay::paint(juce::Graphics& g) { paintCurves(g, "SPECTROGRAM 3D - SURFACE", true);  }


//==============================================================================
//==============================================================================
// VuMeterLeak
//==============================================================================
VuMeterLeak::VuMeterLeak(LeakAudioProcessor& p) : processor(p) {}

float VuMeterLeak::dbToY(float db,float gy0,float gh) const
{ return gy0+juce::jlimit(0.f,1.f,(db-DB_MAX)/(DB_MIN-DB_MAX))*gh; }

void VuMeterLeak::update()
{
    LeakAudioProcessor::VuData d;
    if(!processor.getVuData(d)) return;
    if(d.peakL>dispPeakL) dispPeakL=d.peakL; else dispPeakL+=(d.peakL-dispPeakL)*RC;
    if(d.peakR>dispPeakR) dispPeakR=d.peakR; else dispPeakR+=(d.peakR-dispPeakR)*RC;
    dispRmsL+=(d.rmsL-dispRmsL)*RC;
    dispRmsR+=(d.rmsR-dispRmsR)*RC;
    if(dispPeakL>=holdL){holdL=dispPeakL;holdFrL=HOLD_FR;}
    else if(holdFrL>0) --holdFrL; else holdL-=0.06f;
    if(dispPeakR>=holdR){holdR=dispPeakR;holdFrR=HOLD_FR;}
    else if(holdFrR>0) --holdFrR; else holdR-=0.06f;
    if(d.peakL>=-0.1f) clipHoldL=2.f; else clipHoldL=juce::jmax(0.f,clipHoldL-DT);
    if(d.peakR>=-0.1f) clipHoldR=2.f; else clipHoldR=juce::jmax(0.f,clipHoldR-DT);
    {
        bool overL=false, overR=false;
        processor.consumeTruePeakOvers(overL, overR);
        tpLatchL = tpLatchL || overL;
        tpLatchR = tpLatchR || overR;
    }
    if(dispPeakL>numPeakL) numPeakL=dispPeakL; else numPeakL+=(dispPeakL-numPeakL)*NUM_RC;
    if(dispPeakR>numPeakR) numPeakR=dispPeakR; else numPeakR+=(dispPeakR-numPeakR)*NUM_RC;
    numRmsL += (dispRmsL-numRmsL)*NUM_RC*3.f;
    numRmsR += (dispRmsR-numRmsR)*NUM_RC*3.f;
    LeakAudioProcessor::LoudnessData ld;
    if(processor.getLoudnessData(ld)){
        const float lra=juce::jmax(0.f,ld.lra);
        dispLRA+=(lra-dispLRA)*0.15f;
        dispIntegrated=ld.integrated;
        dispTPL=ld.truePeakL; dispTPR=ld.truePeakR;
        tpMaxL=juce::jmax(tpMaxL, ld.truePeakL);
        tpMaxR=juce::jmax(tpMaxR, ld.truePeakR);
    }
    // Dirty-check : on ne repaint que si quelque chose a visiblement bougé —
    // évite de solliciter le GPU en continu quand le signal est stable/silencieux.
    const bool clipL=clipHoldL>0.f, clipR=clipHoldR>0.f;
    const bool changed =
        std::abs(dispPeakL-lastPaintedPeakL)>0.02f || std::abs(dispPeakR-lastPaintedPeakR)>0.02f ||
        std::abs(dispRmsL -lastPaintedRmsL )>0.02f || std::abs(dispRmsR -lastPaintedRmsR )>0.02f ||
        std::abs(holdL-lastPaintedHoldL)>0.02f || std::abs(holdR-lastPaintedHoldR)>0.02f ||
        clipL!=lastPaintedClipL || clipR!=lastPaintedClipR ||
        tpLatchL!=lastPaintedTpL || tpLatchR!=lastPaintedTpR ||
        std::abs(tpMaxL-lastPaintedTpMaxL)>0.05f || std::abs(tpMaxR-lastPaintedTpMaxR)>0.05f;
    if(changed){
        lastPaintedPeakL=dispPeakL; lastPaintedPeakR=dispPeakR;
        lastPaintedRmsL=dispRmsL;   lastPaintedRmsR=dispRmsR;
        lastPaintedHoldL=holdL;     lastPaintedHoldR=holdR;
        lastPaintedClipL=clipL;     lastPaintedClipR=clipR;
        lastPaintedTpL=tpLatchL;    lastPaintedTpR=tpLatchR;
        lastPaintedTpMaxL=tpMaxL;   lastPaintedTpMaxR=tpMaxR;
        repaint();
    }
}
void VuMeterLeak::mouseDown(const juce::MouseEvent& e)
{
    // clic sur les pastilles TP : remet à zéro TP et CLIP du côté cliqué
    // (moitié gauche = L, moitié droite = R, dans les deux designs)
    if((float)e.y < PAD_T){
        if(e.x < getWidth()/2){ clipHoldL = 0.f; tpLatchL = false; tpMaxL = -120.f; }
        else                { clipHoldR = 0.f; tpLatchR = false; tpMaxR = -120.f; }
        repaint();
    }
}
void VuMeterLeak::resetAll()
{
    dispPeakL=dispPeakR=-120.f;
    dispRmsL=dispRmsR=-120.f;
    holdL=holdR=-120.f; holdFrL=holdFrR=0;
    dispLRA=0.f; dispIntegrated=-120.f;
    clipHoldL=clipHoldR=0.f;
    tpLatchL=tpLatchR=false;
    tpMaxL=tpMaxR=-120.f;
    dispTPL=dispTPR=-120.f;
    numPeakL=numPeakR=-120.f;
    numRmsL=numRmsR=-120.f;
    repaint();
}

void VuMeterLeak::paint(juce::Graphics& g)
{
    // Section master — deux designs (interrupteur en haut à gauche) :
    //   A "Instrument"         : barres crête avec RMS intégré, échelle des deux
    //                            côtés, tableau PEAK / RMS / CREST en bas ;
    //   C "Console mastering"  : échelle centrale, barres galbées, grands
    //                            afficheurs LCD et facteur de crête.
    // Communs : pastilles TP en haut (max en dBTP, rouges > 0 dBTP), maintien
    // de crête, bande rouge en haut de barre quand l'échantillon clippe.
    const float W=(float)getWidth(), H=(float)getHeight();
    g.setColour(Col::bg); g.fillRect(getLocalBounds());
    if(W<40.f || H<120.f) return;

    const float gy0=PAD_T;
    const float panelH=juce::jlimit(110.f, 168.f, H*0.27f)*juce::jmin(1.15f, Ui::textScale);
    const float botY=H-panelH-(consoleStyle ? 8.f : 22.f);
    const float gh=juce::jmax(20.f, botY-gy0);
    auto yOf=[&](float db){ return dbToY(db, gy0, gh); };

    const juce::ColourGradient levelGrad=[&]{
        juce::ColourGradient lg(Col::ink(juce::Colour(0xFFFF1500)), 0.f, gy0, Col::ink(juce::Colour(0xFF00FFB2)).withAlpha(0.25f), 0.f, botY, false);
        lg.addColour(0.10, Col::ink(juce::Colour(0xFFFF6600)));
        lg.addColour(0.24, Col::ink(juce::Colour(0xFFFFD200)));
        lg.addColour(0.50, Col::ink(juce::Colour(0xFF00FF55)));
        lg.addColour(0.80, Col::ink(juce::Colour(0xFF00FFB2)));
        return lg; }();

    auto fmt=[](float db, bool sign){
        if(db<=-59.f) return juce::String("--");
        return (sign && db>0.f ? "+" : "")+juce::String(db, 1); };

    // pastille TP (design validé)
    auto drawTpPill=[&](float cx, float w, float tpMax, bool over){
        const auto pill=juce::Rectangle<float>(w, 24.f).withCentre({cx, 4.f+12.f});
        juce::Path p; p.addRoundedRectangle(pill, 6.f);
        if(over){
            juce::DropShadow(Col::ink(juce::Colour(0xFFFF2A2A)).withAlpha(Col::paper ? 0.35f : 0.8f), 8, {}).drawForPath(g, p);
            g.setColour(Col::pick(juce::Colour(0xE6A00A0A), juce::Colour(0xFFFDE6E3))); g.fillPath(p);
            g.setColour(Col::ink(juce::Colour(0xFFFF4A4A))); g.strokePath(p, juce::PathStrokeType(1.2f));
        } else {
            g.setColour(Col::pick(juce::Colour(0xFF0D1416), Col::knobDim)); g.fillPath(p);
            g.setColour(Col::light(Col::paper ? 0.14f : 0.08f)); g.strokePath(p, juce::PathStrokeType(1.f));
        }
        g.setFont(Ui::iceland(8.5f));
        g.setColour(over ? Col::pick(juce::Colour(0xFFFFD0D0), Col::ink(juce::Colour(0xFFFF4A4A))) : Col::textHi.withAlpha(0.55f));
        g.drawFittedText("TP", pill.withHeight(11.f).translated(0.f, 1.f).toNearestInt(), juce::Justification::centred, 1);
        g.setFont(Ui::iceland(11.5f));
        g.setColour(over ? Col::pick(juce::Colours::white, Col::ink(juce::Colour(0xFFFF2A2A))) : Col::textHi.withAlpha(0.92f));
        g.drawFittedText(fmt(tpMax, true), pill.withTrimmedTop(10.f).toNearestInt(), juce::Justification::centred, 1);
    };

    // barre crête : piste, dégradé de niveau, bande de clip, maintien
    auto drawBar=[&](juce::Rectangle<float> r, float peakDb, float holdDb, bool clip, bool rounded){
        juce::Path track; track.addRoundedRectangle(r, rounded ? 4.f : 1.f);
        g.setColour(Col::pick(juce::Colour(0xFF0A1717), juce::Colour(0xFFE6EBF0))); g.fillPath(track);
        g.saveState(); g.reduceClipRegion(track);
        const float y=juce::jmax(yOf(peakDb), gy0);
        g.setGradientFill(levelGrad); g.fillRect(r.withTop(y));
        if(rounded){   // galbe : reflet à gauche, ombre à droite
            juce::ColourGradient sh(juce::Colours::white.withAlpha(Col::paper ? 0.35f : 0.22f), r.getX(), 0.f,
                                    juce::Colours::black.withAlpha(Col::paper ? 0.12f : 0.35f), r.getRight(), 0.f, false);
            sh.addColour(0.4, juce::Colours::transparentWhite);
            g.setGradientFill(sh); g.fillRect(r);
        }
        if(clip){ g.setColour(Col::ink(juce::Colour(0xFFFF1010))); g.fillRect(r.withHeight(4.f)); }
        g.restoreState();
        if(holdDb>DB_MIN){
            g.setColour(Col::pick(juce::Colour(0xFFE8FFF6), Col::textHi));
            g.fillRect(r.getX(), juce::jmax(yOf(holdDb), gy0)-1.f, r.getWidth(), 2.f);
        }
    };

    // panneau creusé des afficheurs
    auto drawPanel=[&](juce::Rectangle<float> r){
        g.setColour(Col::pick(juce::Colour(0xFF071012), Col::panel)); g.fillRoundedRectangle(r, 6.f);
        g.setColour(Col::pick(juce::Colours::white.withAlpha(0.07f), Col::border)); g.drawRoundedRectangle(r.reduced(0.5f), 6.f, 1.f);
    };

    const bool overL=tpLatchL || tpMaxL>0.f, overR=tpLatchR || tpMaxR>0.f;
    const float crestL=numPeakL-numRmsL, crestR=numPeakR-numRmsR;
    static const int marks[] = { 6, 3, 0, -3, -6, -9, -12, -18, -24, -30, -40, -50, -60 };

    if(!consoleStyle){
        // ── A — Instrument ───────────────────────────────────────────────
        const float sc=juce::jlimit(20.f, 30.f, W*0.15f);
        const float gap=6.f, bw=(W-2.f*sc-gap-8.f)*0.5f;
        const float xL=sc+4.f, xR=xL+bw+gap;

        drawTpPill(xL+bw*0.5f, bw-2.f, tpMaxL, overL);
        drawTpPill(xR+bw*0.5f, bw-2.f, tpMaxR, overR);

        g.setFont(Ui::iceland(9.5f));
        for(int db : marks){
            const float y=yOf((float)db);
            g.setColour(db==0 ? juce::Colour(0x73FF5050) : Col::grid);
            g.drawLine(sc, y, W-sc, y, db==0 ? 1.f : 0.6f);
            g.setColour(db>0 ? Col::ink(juce::Colour(0xFFFF6060)) : Col::light(0.75f));
            const juce::String t=db>0 ? "+"+juce::String(db) : juce::String(db);
            g.drawFittedText(t, 0, (int)y-6, (int)sc-4, 12, juce::Justification::centredRight, 1);
            g.drawFittedText(t, (int)(W-sc)+4, (int)y-6, (int)sc-4, 12, juce::Justification::centredLeft, 1);
        }

        for(int c=0;c<2;++c){
            const float x=c==0 ? xL : xR;
            const auto r=juce::Rectangle<float>(x, gy0, bw, gh);
            drawBar(r, c==0 ? dispPeakL : dispPeakR, c==0 ? holdL : holdR, c==0 ? clipHoldL>0.f : clipHoldR>0.f, false);
            // RMS intégré : bande centrale assombrie + repère blanc
            const float ry=juce::jmax(yOf(c==0 ? dispRmsL : dispRmsR), gy0);
            g.setColour(Col::pick(juce::Colour(0x8C030A0A), juce::Colours::white.withAlpha(0.45f))); g.fillRect(x+bw*0.3f, ry, bw*0.4f, botY-ry);
            g.setColour(Col::light(0.85f)); g.fillRect(x+bw*0.3f, ry-1.f, bw*0.4f, 2.f);
        }

        g.setFont(Ui::iceland(11.f));
        g.setColour(Col::textMid.withAlpha(0.85f));
        g.drawText("L", juce::Rectangle<float>(xL, botY+3.f, bw, 14.f), juce::Justification::centred);
        g.drawText("R", juce::Rectangle<float>(xR, botY+3.f, bw, 14.f), juce::Justification::centred);

        // tableau PEAK / RMS / CREST
        const auto panel=juce::Rectangle<float>(6.f, botY+20.f, W-12.f, H-botY-26.f);
        drawPanel(panel);
        const float rowH=panel.getHeight()/3.f;
        struct Row { const char* label; juce::String l, r; juce::Colour col; float size; };
        const Row rows[3] = {
            { "PEAK",  fmt(numPeakL, false), fmt(numPeakR, false), Col::light(1.f),          17.f },
            { "RMS",   fmt(numRmsL,  false), fmt(numRmsR,  false), Col::accent,              14.f },
            { "CREST", numPeakL<=-59.f ? juce::String("--") : juce::String(crestL, 1),
                       numPeakR<=-59.f ? juce::String("--") : juce::String(crestR, 1), Col::ink(juce::Colour(0xFFFFD200)), 12.5f },
        };
        for(int i=0;i<3;++i){
            const auto row=panel.withTop(panel.getY()+rowH*(float)i).withHeight(rowH);
            g.setFont(Ui::iceland(8.5f));
            g.setColour(Col::textHi.withAlpha(0.5f));
            g.drawText(rows[i].label, row.reduced(8.f, 3.f), juce::Justification::topLeft);
            g.setFont(Ui::iceland(rows[i].size));
            g.setColour(rows[i].col);
            const auto valRow=row.withTrimmedTop(rowH*0.3f);
            g.drawFittedText(rows[i].l, juce::Rectangle<float>(xL-4.f, valRow.getY(), bw+8.f, valRow.getHeight()).toNearestInt(), juce::Justification::centred, 1);
            g.drawFittedText(rows[i].r, juce::Rectangle<float>(xR-4.f, valRow.getY(), bw+8.f, valRow.getHeight()).toNearestInt(), juce::Justification::centred, 1);
            if(i<2){
                g.setColour(Col::light(Col::paper ? 0.10f : 0.06f));
                g.fillRect(panel.getX()+6.f, row.getBottom(), panel.getWidth()-12.f, 1.f);
            }
        }
    } else {
        // ── C — Console mastering ────────────────────────────────────────
        const float scW=juce::jlimit(30.f, 44.f, W*0.24f);
        const float bw=(W-scW-24.f)*0.5f;
        const float xL=10.f, xR=W-10.f-bw, mid=W*0.5f;

        drawTpPill(xL+bw*0.5f, bw-2.f, tpMaxL, overL);
        drawTpPill(xR+bw*0.5f, bw-2.f, tpMaxR, overR);

        g.setFont(Ui::iceland(10.f));
        for(int db : marks){
            const float y=yOf((float)db);
            g.setColour(db>0 ? Col::ink(juce::Colour(0xFFFF6060)) : Col::light(0.8f));
            g.drawFittedText(db>0 ? "+"+juce::String(db) : juce::String(db),
                             juce::Rectangle<float>(mid-scW*0.5f+6.f, y-6.f, scW-12.f, 12.f).toNearestInt(), juce::Justification::centred, 1);
            g.setColour(Col::light(0.25f));
            g.drawLine(xL+bw+2.f, y, xL+bw+7.f, y, 1.f);
            g.drawLine(xR-7.f,    y, xR-2.f,    y, 1.f);
        }

        for(int c=0;c<2;++c){
            const float x=c==0 ? xL : xR;
            const auto r=juce::Rectangle<float>(x, gy0, bw, gh);
            drawBar(r, c==0 ? dispPeakL : dispPeakR, c==0 ? holdL : holdR, c==0 ? clipHoldL>0.f : clipHoldR>0.f, true);
            const float ry=juce::jmax(yOf(c==0 ? dispRmsL : dispRmsR), gy0);   // repère RMS
            g.setColour(Col::light(0.9f)); g.fillRect(x, ry-1.f, bw, 2.f);
        }

        // grands afficheurs LCD
        const auto panel=juce::Rectangle<float>(6.f, botY+6.f, W-12.f, H-botY-12.f);
        drawPanel(panel);
        const float lx=panel.getX()+panel.getWidth()*0.28f, rx=panel.getX()+panel.getWidth()*0.72f;
        const float colW=panel.getWidth()*0.44f;
        auto colText=[&](const juce::String& t, float cx, float y, float h){
            g.drawFittedText(t, juce::Rectangle<float>(cx-colW*0.5f, y, colW, h).toNearestInt(), juce::Justification::centred, 1); };
        const float u=panel.getHeight()/8.f;   // 8 unités : L/R, PEAK x2, RMS x2, CREST x2 + marge
        g.setFont(Ui::iceland(10.f)); g.setColour(Col::textMid.withAlpha(0.85f));
        colText("L", lx, panel.getY()+u*0.15f, u*0.8f); colText("R", rx, panel.getY()+u*0.15f, u*0.8f);
        auto label=[&](const juce::String& t, float y){
            g.setFont(Ui::iceland(8.5f)); g.setColour(Col::textHi.withAlpha(0.5f));
            g.drawText(t, juce::Rectangle<float>(panel.getX(), y, panel.getWidth(), u*0.7f), juce::Justification::centred); };
        label("PEAK", panel.getY()+u*0.9f);
        g.setFont(Ui::iceland(19.f)); g.setColour(Col::light(1.f));
        colText(fmt(numPeakL, false), lx, panel.getY()+u*1.5f, u*1.4f); colText(fmt(numPeakR, false), rx, panel.getY()+u*1.5f, u*1.4f);
        label("RMS", panel.getY()+u*3.0f);
        g.setFont(Ui::iceland(16.f)); g.setColour(Col::accent);
        colText(fmt(numRmsL, false), lx, panel.getY()+u*3.6f, u*1.3f); colText(fmt(numRmsR, false), rx, panel.getY()+u*3.6f, u*1.3f);
        label(TR("CREST FACTOR"), panel.getY()+u*5.1f);
        g.setFont(Ui::iceland(15.f)); g.setColour(Col::ink(juce::Colour(0xFFFFD200)));
        const float crest=(crestL+crestR)*0.5f;
        g.drawFittedText(numPeakL<=-59.f ? juce::String("--") : juce::String(crest, 1)+" dB",
                         juce::Rectangle<float>(panel.getX(), panel.getY()+u*5.7f, panel.getWidth(), u*1.4f).toNearestInt(),
                         juce::Justification::centred, 1);
    }
}

//==============================================================================
// LoudnessPanel
//==============================================================================
LoudnessPanel::LoudnessPanel(LeakAudioProcessor& p) : processor(p)
{
    // RESET propre au module : intégré, LRA et true peak max (le RESET de la
    // barre du bas remet aussi le Spectrum et le VU à zéro)
    resetBtn.onClick=[this]{ processor.resetLoudness(); };
    addAndMakeVisible(resetBtn);
}
void LoudnessPanel::resized()
{
    const float s=Ui::textScale;
    const int bh=juce::jlimit(18, 30, juce::roundToInt(22.f*s));
    const int bw=juce::jmin(getWidth()-12, juce::roundToInt(78.f*s));
    resetBtn.setBounds((getWidth()-bw)/2, getHeight()-bh-5, bw, bh);
    btnZoneH=bh+8;
}
void LoudnessPanel::update()
{
    LeakAudioProcessor::LoudnessData ld;
    if(!processor.getLoudnessData(ld)) return;
    dispM=ld.momentary; dispS=ld.shortTerm; dispI=ld.integrated;
    dispLRA=juce::jmax(0.f,ld.lra);
    // TRUE PEAK : maximum maintenu (le pic du moteur retombe lentement et
    // faisait bouger l'affichage) ; remis à zéro avec le Loudness (RESET)
    const int rc=processor.loudnessResetCount.load();
    if(rc!=seenResetCount){ seenResetCount=rc; tpMaxL=tpMaxR=-120.f; }
    tpMaxL=juce::jmax(tpMaxL, ld.truePeakL); tpMaxR=juce::jmax(tpMaxR, ld.truePeakR);
    dispTPL=tpMaxL; dispTPR=tpMaxR;
    if(ld.integrated>numI) numI=ld.integrated; else numI+=(ld.integrated-numI)*0.002f;
    numM += (ld.momentary - numM) * 0.015f;
    numS += (ld.shortTerm - numS) * 0.006f;
    const bool changed =
        std::abs(numI-lastPaintedI)>0.02f || std::abs(numM-lastPaintedM)>0.02f ||
        std::abs(numS-lastPaintedS)>0.02f || std::abs(dispLRA-lastPaintedLRA)>0.02f ||
        std::abs(dispTPL-lastPaintedTPL)>0.02f || std::abs(dispTPR-lastPaintedTPR)>0.02f;
    if(changed){
        lastPaintedI=numI; lastPaintedM=numM; lastPaintedS=numS;
        lastPaintedLRA=dispLRA; lastPaintedTPL=dispTPL; lastPaintedTPR=dispTPR;
        repaint();
    }
}
void LoudnessPanel::paint(juce::Graphics& g)
{
    const int W=getWidth(), H=getHeight();
    // en-tête à l'échelle du texte : en gros texte, "EBU R128" passait sous le cadre INTEGRATED
    const float s=Ui::textScale;
    const int hdrH=juce::roundToInt(26.f*s), titleH=juce::roundToInt(13.f*s);
    g.setColour(Col::bg); g.fillRect(getLocalBounds());
    if(Ui::titleStyle==0){
        g.setColour(Col::panel); g.fillRect(0,0,W,hdrH);
        g.setColour(Col::border); g.drawLine(0.f,(float)hdrH+0.5f,(float)W,(float)hdrH+0.5f,0.6f);
    } else {
        Ui::drawTitleDecor(g, (float)W, (float)hdrH);
    }
    g.setFont(Ui::iceland((10.5f)).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    g.drawFittedText(TR("LOUDNESS"),6,3,W-12,titleH,juce::Justification::left,1,0.7f);
    g.setFont(Ui::iceland((7.5f)).withExtraKerningFactor(0.12f));
    g.setColour(Col::textMid.withAlpha(0.5f));
    g.drawText("EBU R128",6,2+titleH,W-12,hdrH-3-titleH,juce::Justification::left);
    const float bodyH=(float)(H-hdrH-4-btnZoneH);
    auto fmtVal=[](float v,int dec)->juce::String{
        return (v<=-119.f)?juce::String("---"):juce::String(v,dec);
    };
    auto lufsCol=[](float v)->juce::Colour{
        if(v>=-9.f)  return Col::ink(juce::Colour(0xFFFF1500));
        if(v>=-14.f) return Col::ink(juce::Colour(0xFFFFD200));
        if(v>=-23.f) return Col::accent;
        return Col::textMid;
    };
    auto tpCol=[](float v)->juce::Colour{
        if(v>=-1.f) return Col::ink(juce::Colour(0xFFFF1500));
        if(v>=-3.f) return Col::ink(juce::Colour(0xFFFFD200));
        return Col::accent;
    };
    const float bigH=juce::jlimit(28.f,52.f*s,bodyH*0.36f);
    const float bigY=(float)hdrH+4.f;
    {
        const juce::Colour ic=lufsCol(numI);
        g.setColour(ic.withAlpha(0.06f)); g.fillRect(0.f,bigY,(float)W,bigH);
        g.setColour(Col::border.withAlpha(0.4f));
        g.drawLine(0.f,bigY+bigH,(float)W,bigY+bigH,0.5f);
        // Trois rangées bien séparées — label, grand nombre, unité — pour ne
        // jamais se chevaucher, même quand le panneau est bas (bigH minimal).
        const float lblH=juce::jmin(11.f,bigH*0.24f);
        const float unitH=juce::jmin(11.f,bigH*0.22f);
        const float numH=juce::jmax(14.f,bigH-lblH-unitH);
        g.setFont(Ui::iceland((juce::jlimit(8.f,11.f,lblH))));
        g.setColour(Col::textMid.withAlpha(0.55f));
        g.drawFittedText(TR("INTEGRATED"),2,(int)bigY+2,W-4,(int)lblH,juce::Justification::left,1,0.7f);
        const float vfs=juce::jlimit(16.f,30.f,numH*0.78f);
        g.setColour(lufsCol(numI));
        Ui::drawFit(g, fmtVal(numI,1), juce::Rectangle<float>(0.f,bigY+lblH,(float)W-3.f,numH), juce::Justification::centredRight, Ui::iceland(vfs));
        g.setFont(Ui::iceland((juce::jlimit(7.f,9.f,unitH))));
        g.setColour(ic.withAlpha(0.6f));
        g.drawText("LUFS",0,(int)(bigY+lblH+numH),W-3,(int)unitH,juce::Justification::centredRight);
    }
    float y=bigY+bigH+4.f;
    const float remH=(float)H-y-4.f-(float)btnZoneH;   // bouton RESET en bas
    const float sepH=3.f;
    const float rowH=juce::jmax(9.f,(remH-2.f*sepH)/5.f);
    const float fs2=juce::jlimit(9.f,13.f,rowH*0.82f);
    auto row=[&](const juce::String& lbl,const juce::String& val,juce::Colour vc){
        // valeur d'abord, à sa largeur réelle (jamais tronquée, même en gros
        // texte) ; le libellé prend le reste et se resserre au besoin
        const auto valFont=Ui::iceland(fs2);
        juce::GlyphArrangement ga; ga.addLineOfText(valFont, val, 0.f, 0.f);
        const float vw=juce::jmin((float)W*0.62f, ga.getBoundingBox(0, -1, true).getWidth()+4.f);
        g.setFont(valFont);
        g.setColour(vc);
        g.drawText(val,(int)((float)W-6.f-vw),(int)y,(int)vw,(int)rowH,juce::Justification::centredRight,false);
        g.setFont(Ui::iceland((juce::jlimit(7.f,9.f,rowH*0.5f))).withExtraKerningFactor(0.03f));
        g.setColour(Col::textMid.withAlpha(0.6f));
        g.drawFittedText(lbl,6,(int)y,juce::jmax(1,(int)((float)W-vw-18.f)),(int)rowH,juce::Justification::centredLeft,1,0.6f);
        y+=rowH;
    };
    auto sep=[&]{ g.setColour(Col::border.withAlpha(0.4f));
                  g.drawLine(4.f,y+1.f,(float)W-4.f,y+1.f,0.5f); y+=sepH; };
    row(TR("SHORT TERM"),   fmtVal(numS,1)+" LUFS", lufsCol(numS));
    row(TR("MOMENTARY"),    fmtVal(numM,1)+" LUFS", lufsCol(numM));
    sep();
    row(TR("LOUDNESS RANGE"), dispLRA<0.05f?"---":juce::String(dispLRA,1)+" LU", Col::accent);
    sep();
    row("TRUE PEAK L",fmtVal(dispTPL,1)+" dB",tpCol(dispTPL));
    row("TRUE PEAK R",fmtVal(dispTPR,1)+" dB",tpCol(dispTPR));
}

//==============================================================================
// ReleaseFader
//==============================================================================
void ReleaseFader::paint(juce::Graphics& g)
{
    // Style A "rail intégré à l'écran" (preview validée) : pas de support, un
    // rail fin translucide directement sur l'écran du module, remplissage
    // d'accent lumineux depuis le bas, petit curseur clair (plus grand au
    // survol, lueur renforcée pendant le glisser).
    const int W=getWidth(), H=getHeight();
    const auto acc=Col::accent;
    if(horizontal){
        // même rail, couché : étiquette à gauche, valeur à droite
        if(W<60||H<8) return;
        const float lw=30.f*Ui::textScale, vw=46.f*Ui::textScale, cy=(float)H*0.5f;   // gros texte : "TIME" ne mord plus sur le rail
        const float x0=lw, x1=(float)W-vw;
        g.setFont(Ui::iceland(9.f));
        g.setColour(Col::textMid.withAlpha(0.8f));
        g.drawFittedText(TR(label),0,0,(int)lw-2,H,juce::Justification::centredRight,1,0.6f);
        juce::Path rail; rail.startNewSubPath(x0, cy); rail.lineTo(x1, cy);
        const juce::PathStrokeType railStroke(4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour(acc.withAlpha(0.12f)); g.strokePath(rail, railStroke);
        const float xv=x0+(x1-x0)*range.convertTo0to1(value);
        juce::Path fill; fill.startNewSubPath(x0, cy); fill.lineTo(xv, cy);
        g.setColour(acc.withAlpha(dragging ? 0.35f : 0.18f));
        g.strokePath(fill, juce::PathStrokeType(9.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(acc); g.strokePath(fill, railStroke);
        const float tw=hovered||dragging ? 7.f : 6.f, th=hovered||dragging ? 16.f : 14.f;
        const auto thumb=juce::Rectangle<float>(tw, juce::jmin(th,(float)H)).withCentre({xv, cy});
        g.setColour(juce::Colours::black.withAlpha(Col::paper ? 0.22f : 0.5f)); g.fillRoundedRectangle(thumb.translated(1.f, 0.f), tw*0.5f);
        g.setColour(Col::pick(juce::Colour(0xFFE8FFF6), juce::Colours::white)); g.fillRoundedRectangle(thumb, tw*0.5f);
        g.setFont(Ui::iceland(9.5f));
        g.setColour(acc);
        g.drawFittedText(valueText(),(int)x1+6,0,(int)vw-6,H,juce::Justification::centredLeft,1,0.6f);
        return;
    }
    if(W<6||H<20) return;
    const float s=Ui::textScale;   // gros texte : rangées du libellé et de la valeur plus hautes
    const float trackY0=4.f+11.f*s, trackY1=(float)H-5.f-11.f*s;
    const float cx=(float)W*0.5f;

    g.setFont(Ui::iceland(9.f));
    g.setColour(Col::textMid.withAlpha(0.8f));
    g.drawFittedText(TR(label),0,1,W,juce::roundToInt(11.f*s),juce::Justification::centred,1,0.45f);   // "TIME" tient dans le rail

    // rail
    juce::Path rail; rail.startNewSubPath(cx, trackY0); rail.lineTo(cx, trackY1);
    const juce::PathStrokeType railStroke(4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour(acc.withAlpha(0.12f));
    g.strokePath(rail, railStroke);

    // remplissage avec lueur
    const float frac=range.convertTo0to1(value);
    const float yv=trackY1-(trackY1-trackY0)*frac;
    juce::Path fill; fill.startNewSubPath(cx, yv); fill.lineTo(cx, trackY1);
    g.setColour(acc.withAlpha(dragging ? 0.35f : 0.18f));
    g.strokePath(fill, juce::PathStrokeType(9.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(acc);
    g.strokePath(fill, railStroke);

    // curseur
    const float tw=hovered||dragging ? 16.f : 14.f, th=hovered||dragging ? 7.f : 6.f;
    const auto thumb=juce::Rectangle<float>(tw, th).withCentre({cx, yv});
    g.setColour(juce::Colours::black.withAlpha(Col::paper ? 0.22f : 0.5f)); g.fillRoundedRectangle(thumb.translated(0.f, 1.f), th*0.5f);
    g.setColour(Col::pick(juce::Colour(0xFFE8FFF6), juce::Colours::white)); g.fillRoundedRectangle(thumb, th*0.5f);

    g.setFont(Ui::iceland(9.5f));
    g.setColour(acc);
    g.drawFittedText(valueText(),0,H-2-juce::roundToInt(11.f*s),W,juce::roundToInt(11.f*s),juce::Justification::centred,1,0.6f);   // resserré plutôt que "2…"
}
void ReleaseFader::setFromPos(juce::Point<int> p)
{
    float frac;
    if(horizontal){
        // mêmes bornes que paint()
        const float x0=30.f*Ui::textScale, x1=(float)getWidth()-46.f*Ui::textScale;
        frac=juce::jlimit(0.f,1.f,((float)p.x-x0)/juce::jmax(4.f,x1-x0));
    } else {
        const float trackY0=4.f+11.f*Ui::textScale, trackY1=(float)getHeight()-5.f-11.f*Ui::textScale;
        frac=juce::jlimit(0.f,1.f,1.f-((float)p.y-trackY0)/juce::jmax(4.f,trackY1-trackY0));
    }
    setValue(range.convertFrom0to1(frac));
}
void ReleaseFader::mouseDown(const juce::MouseEvent& e){ if(!e.mods.isLeftButtonDown()) return; dragging=true; setFromPos(e.getPosition()); repaint(); }
void ReleaseFader::mouseDrag(const juce::MouseEvent& e){ if(!e.mods.isLeftButtonDown()) return; setFromPos(e.getPosition()); }

//==============================================================================
// SpectrumDisplayLeak
//==============================================================================
SpectrumDisplayLeak::SpectrumDisplayLeak(LeakAudioProcessor& p) : processor(p)
{
    for(int i=0;i<NMAX;++i){
        levels[i]=sideLevel[i]=peaks[i]=sidePeaks[i]=-120.f;
        prevLevels[i]=prevSideLevel[i]=-120.f;
        prevPeaks[i]=prevSidePeaks[i]=-120.f;
        peakTimers[i]=sidePeakTimers[i]=0.f;
        clipLatch[i]=false;clipHoldF[i]=0;
        bandBuf[i]=sideBuf[i]=-120.f;
    }
    lastHoveredBand=-1;cachedTipW=0.f;
    addAndMakeVisible(relFader);
    relFader.setValue(processor.spectrumReleaseSec,juce::dontSendNotification);
    setReleaseSec(processor.spectrumReleaseSec);
    relFader.onValueChange=[this](float v){ processor.spectrumReleaseSec=v; setReleaseSec(v); };
    startTimerHz(60);  // fluide 60Hz — le dirty-check évite les repaints inutiles, pas la fréquence elle-même
}
SpectrumDisplayLeak::~SpectrumDisplayLeak(){stopTimer();}

void SpectrumDisplayLeak::setActiveBands(int n)
{
    activeBands=n;
    for(int i=0;i<NMAX;++i){
        levels[i]=sideLevel[i]=-120.f;
        prevLevels[i]=prevSideLevel[i]=-120.f;
        bandBuf[i]=sideBuf[i]=-120.f;
    }
    resetPeaks();
    // Les indices de bandes changent de sens entre 15/31/62 — on efface
    // toute sélection de bandes (elle désignerait d'autres fréquences).
    if(soloBand!=-1 || multiSelectMask!=0ULL){
        soloBand=-1; multiSelectMask=0; lastDragBand=-1;
    }
    cacheValid=false;gridImageValid=false;
    if(getWidth()>10&&getHeight()>10) rebuildCache();
    repaint();
}
void SpectrumDisplayLeak::setReleaseSec(float s)
{
    releaseSec=juce::jmax(0.01f,s);
}
void SpectrumDisplayLeak::resetPeaks()
{ for(int i=0;i<NMAX;++i){peaks[i]=sidePeaks[i]=prevPeaks[i]=prevSidePeaks[i]=-120.f;peakTimers[i]=sidePeakTimers[i]=0.f;} }
void SpectrumDisplayLeak::resetAll()
{
    resetPeaks();
    for(int i=0;i<NMAX;++i){
        levels[i]=sideLevel[i]=-120.f;
        prevLevels[i]=prevSideLevel[i]=-120.f;
        bandBuf[i]=sideBuf[i]=-120.f;
        clipLatch[i]=false;clipHoldF[i]=0;
    }
    globalClipLatch=false;
    // RESET efface aussi toute sélection de bandes.
    if(soloBand!=-1 || multiSelectMask!=0ULL){
        soloBand=-1; multiSelectMask=0; lastDragBand=-1;
    }
    repaint();
}
void SpectrumDisplayLeak::invalidateCache(){cacheValid=false;gridImageValid=false;}
void SpectrumDisplayLeak::resized(){
    invalidateCache();rebuildCache();
    relFader.setBounds(2,(int)padT(),(int)kReleaseFaderW-4,juce::jmax(10,(int)cachedGH));
}

// Axe log calé sur les BORDS des bandes extrêmes (et non sur leurs centres) :
// la première et la dernière bande sont entières, pas coupées par le cadre.
float SpectrumDisplayLeak::freqToX(float f) const
{ return cachedPadL+(std::log10(f)-cachedLogMin)/(cachedLogMax-cachedLogMin)*cachedGW; }
float SpectrumDisplayLeak::dbToY(float db) const
{ return padT()+juce::jlimit(0.f,1.f,db/-dbRange)*cachedGH; }
int SpectrumDisplayLeak::xToBand(float px) const
{ int best=0;float bd=1e9f;
  for(int i=0;i<activeBands;++i){
      const auto& c=barCache[i];float cx=c.x+c.bw*.5f;
      if(px>=c.x&&px<=c.x+c.bw)return i;
      float d=std::abs(cx-px);if(d<bd){bd=d;best=i;}}return best; }

void SpectrumDisplayLeak::rebuildCache()
{
    const int W=getWidth(),H=getHeight();if(W<10||H<10)return;
    // la résolution a pu changer côté moteur (preset rappelé, fréquence
    // d'échantillonnage) avant setActiveBands() : le nombre de bandes doit
    // suivre, sinon étiquettes et fréquences du nouveau mode sont lues
    // au-delà de leur table (plantage au rappel d'un preset, v1.52.0)
    if(activeBands!=processor.getNumActiveBands()){ setActiveBands(processor.getNumActiveBands()); return; }
    cachedPadL=kReleaseFaderW+juce::jlimit(22.f,34.f,W*.042f)*Ui::textScale; // + fader RELEASE local, échelle dB
    cachedGW=(float)W-cachedPadL-4.f;
    const float* F=processor.getBandFreqs();
    const int mode=processor.getBandMode();
    const int N=juce::jmin(activeBands, LeakAudioProcessor::bandTableSize(mode));
    cacheMode=mode;
    float divisor=((mode==62)?130.f:(mode==15)?35.f:70.f)*(float)N/(float)mode;   // bandes ultrasoniques : même place par étiquette
    const float wScale=cachedGW/divisor;
    const float hScale=juce::jlimit(7.f,14.f,((float)H-padT()-34.f*Ui::textScale)*0.05f);
    labelFontSize=juce::jlimit(6.f,14.f,juce::jmin(wScale,hScale))*Ui::textScale;
    labelFont=Ui::icelandAbs(labelFontSize);
    // marge du bas = juste les deux rangées d'étiquettes (taille réelle du
    // texte) : plus de bande vide sous les fréquences dans un petit slot
    cachedPadB=juce::jmin(34.f*Ui::textScale, labelFontSize*2.1f+labelFontSize*0.4f+3.f);
    cachedGH=(float)H-padT()-padB();
    // Bandes espacées régulièrement en log : axe étendu d'un demi-pas de part
    // et d'autre, colonnes de largeur égale calées au pixel, écart constant.
    {
        const float halfStep=N>1 ? (std::log10(F[N-1])-std::log10(F[0]))/(float)(N-1)*0.5f : 0.15f;
        cachedLogMin=std::log10(F[0])  -halfStep;
        cachedLogMax=std::log10(F[N-1])+halfStep;
        const float colW=cachedGW/(float)juce::jmax(1,N);
        const float gap=colW>=10.f ? 2.f : 1.f;
        for(int i=0;i<N;++i){
            const float x0=std::round(cachedPadL+(float)i*colW);
            const float x1=std::round(cachedPadL+(float)(i+1)*colW);
            barCache[i].x=x0;
            barCache[i].bw=juce::jmax(1.f,x1-x0-gap);
        }
    }
    const char** labels=(mode==62)?BAND_LABELS_62:(mode==15)?BAND_LABELS_15:BAND_LABELS_31;
    const float gy0=padT(),gh=cachedGH;
    const float rowA=gy0+gh+labelFontSize+1.f,rowB=gy0+gh+labelFontSize*2.1f+1.f;
    float lastA=-99.f,lastB=-99.f;
    juce::GlyphArrangement ga;
    for(int i=0;i<N;++i){
        const auto& bc=barCache[i];float cx=bc.x+bc.bw*.5f;
        ga.clear();ga.addLineOfText(labelFont,labels[i],0.f,0.f);
        float tw=ga.getNumGlyphs()>0?ga.getBoundingBox(0,ga.getNumGlyphs(),true).getWidth():0.f;
        float row=(i%2==0)?rowA:rowB;float& last=(i%2==0)?lastA:lastB;
        if(cx-tw*.5f>last+2.f){barCache[i].labelX=cx-tw*.5f;barCache[i].labelY=row-labelFontSize;
            barCache[i].showLabel=true;last=cx+tw*.5f;}else barCache[i].showLabel=false;
    }
    // Grid image
    gridImage=juce::Image(juce::Image::ARGB,(int)(cachedGW+cachedPadL+4.f),(int)(cachedGH+padT()+padB()),true);
    {
        juce::Graphics gi(gridImage);
        gi.setColour(Col::bg);gi.fillAll();
        for(int db=-(int)dbRange;db<=0;db+=10){
            float y=std::round(dbToY((float)db))+.5f;
            gi.setColour(db==0?Col::gridCtr:Col::grid);gi.drawLine(cachedPadL,y,cachedPadL+cachedGW,y,1.f);
            float fs=juce::jlimit(8.f,12.f,cachedGH*.065f);
            gi.setFont(Ui::iceland((fs)));
            gi.setColour(db==0?Col::textHi:Col::textMid);
            // (« +0 » un peu plus bas : la ligne du titre passe juste au-dessus)
            gi.drawText(db==0?"+0":juce::String(db),0,(int)y-7+(db==0 ? 4 : 0),(int)cachedPadL-4,14,juce::Justification::centredRight);
        }
        static const float vRef[]={20,50,100,200,500,1000,2000,5000,10000,20000};
        gi.setColour(Col::grid);
        for(float f:vRef){float x=std::round(freqToX(f))+.5f;gi.drawLine(x,padT(),x,padT()+cachedGH,1.f);}
    }
    // Gradient images
    const int gradH=juce::jmax(1,(int)cachedGH);
    barGradImage  = juce::Image(juce::Image::ARGB,1,gradH,true);
    {
        juce::Graphics gg(barGradImage);
        juce::ColourGradient grad(
            Col::ink(juce::Colour(0xFFFF1500)).withAlpha(0.95f),0.f,0.f,
            Col::ink(juce::Colour(0xFF00FFB2)).withAlpha(0.10f),0.f,(float)gradH,false);
        grad.addColour(0.12,Col::ink(juce::Colour(0xFFFF6600)).withAlpha(0.92f));
        grad.addColour(0.28,Col::ink(juce::Colour(0xFFFFD200)).withAlpha(0.88f));
        grad.addColour(0.52,Col::ink(juce::Colour(0xFF00FF55)).withAlpha(0.82f));
        grad.addColour(0.78,Col::ink(juce::Colour(0xFF00FFB2)).withAlpha(0.75f));
        gg.setGradientFill(grad);gg.fillRect(0,0,1,gradH);
    }
    lastHoveredBand=-1;
    cacheValid=true;gridImageValid=true;cacheGen=Col::generation;
}

void SpectrumDisplayLeak::timerCallback()
{
    if(!cacheValid) rebuildCache();
    // pas réel depuis l'image précédente : le timer prend du retard quand
    // l'interface est chargée, et compter 1/60 s par image allongeait le peak
    // hold (2 s affichées en ~3 s) ; durée du hold et lissages en vrai temps
    const double nowMs=juce::Time::getMillisecondCounterHiRes();
    const float dt=lastTickMs>0.0 ? (float)juce::jlimit(0.001, 0.1, (nowMs-lastTickMs)*0.001) : 1.f/(float)currentHz;
    lastTickMs=nowMs;
    // fréquence d'échantillonnage changée : bandes ultrasoniques ajoutées / retirées
    if(processor.getNumActiveBands()!=activeBands){ setActiveBands(processor.getNumActiveBands()); return; }
    processor.getBandLevels(bandBuf,activeBands);
    const bool isMSMode=(analysisMode==LeakAudioProcessor::AnalysisMode::MS);
    if(isMSMode) processor.getSideLevels(sideBuf,activeBands);
    const float attackK =1.f-std::exp(-dt/.005f);
    const float releaseK=1.f-std::exp(-dt/releaseSec);
    const float peakRelK=1.f-std::exp(-dt/juce::jmax(releaseSec*2.f,1.f));
    bool anyChanged=false;
    for(int i=0;i<activeBands;++i){
        float target=juce::jlimit(-120.f,6.f,bandBuf[i]);
        levels[i]+=(target>levels[i])?(target-levels[i])*attackK:(target-levels[i])*releaseK;
        if(isMSMode){
            float st=juce::jlimit(-120.f,6.f,sideBuf[i]);
            sideLevel[i]+=(st>sideLevel[i])?(st-sideLevel[i])*attackK:(st-sideLevel[i])*releaseK;
        }
        if(peakOn){
            if(levels[i]>=peaks[i])     {peaks[i]=levels[i];peakTimers[i]=peakHoldSec;}
            else if(peakHoldSec>9000.f) {}
            else if(peakTimers[i]>0.f)  peakTimers[i]-=dt;
            else peaks[i]+=(-120.f-peaks[i])*peakRelK;
            if(isMSMode){
                if(sideLevel[i]>=sidePeaks[i])  {sidePeaks[i]=sideLevel[i];sidePeakTimers[i]=peakHoldSec;}
                else if(peakHoldSec>9000.f)      {}
                else if(sidePeakTimers[i]>0.f)   sidePeakTimers[i]-=dt;
                else sidePeaks[i]+=(-120.f-sidePeaks[i])*peakRelK;
            }
        }
        if(levels[i]>-.3f)     {clipLatch[i]=true;clipHoldF[i]=55;}
        else if(clipHoldF[i]>0) --clipHoldF[i];
        else                    clipLatch[i]=false;
        if(std::abs(levels[i]-prevLevels[i])>0.2f){anyChanged=true;prevLevels[i]=levels[i];}
        if(isMSMode&&std::abs(sideLevel[i]-prevSideLevel[i])>0.2f){anyChanged=true;prevSideLevel[i]=sideLevel[i];}
        // Le hold-peak évolue indépendamment de levels[] (décroissance propre,
        // pas pilotée par le RELEASE du module) : sans son propre dirty-check,
        // un RELEASE très court fait se stabiliser levels[] avant que le peak
        // ait fini de redescendre — les repaint() s'arrêtent et le peak se fige.
        if(peakOn && std::abs(peaks[i]-prevPeaks[i])>0.2f){anyChanged=true;prevPeaks[i]=peaks[i];}
        if(peakOn && isMSMode && std::abs(sidePeaks[i]-prevSidePeaks[i])>0.2f){anyChanged=true;prevSidePeaks[i]=sidePeaks[i];}
    }
    globalClipLatch=false;
    for(int i=0;i<activeBands;++i) if(clipLatch[i]){globalClipLatch=true;break;}
    if(anyChanged) repaint();

    // Rafraîchissement périodique du tooltip de survol (toutes les ~0.5s)
    // même si la souris reste immobile sur la même bande
    if(hoveredBand>=0){
        if(++hoverRefreshCounter>=HOVER_REFRESH_FRAMES){
            hoverRefreshCounter=0;
            forceTipRefresh=true;
            repaint();
        }
    } else {
        hoverRefreshCounter=0;
    }
}


void SpectrumDisplayLeak::mouseMove(const juce::MouseEvent& e)
{
    hoverPixX=(float)e.x;
    float gx0=cachedPadL,gx1=(float)getWidth()-4.f,gy0=padT(),gy1=padT()+cachedGH;
    int nb=(e.x>=(int)gx0&&e.x<=(int)gx1&&e.y>=(int)gy0&&e.y<=(int)gy1)?xToBand((float)e.x):-1;
    if(nb!=hoveredBand){hoveredBand=nb;hoverRefreshCounter=0;repaint();}
    else if(hoveredBand>=0){
        const int tipW=(int)cachedTipW+24;
        const int tipX=juce::jlimit(0,(int)(getWidth()-tipW),e.x+10);
        repaint(tipX-2,(int)gy0,tipW+(int)hoverPixX-tipX+14,(int)cachedGH);
    }
}
void SpectrumDisplayLeak::mouseExit(const juce::MouseEvent&)
{
    if(hoveredBand!=-1){hoveredBand=-1;repaint();}
    // Le clic-maintenu mono-bande s'arrête si la souris sort du composant,
    // mais la sélection multi-bandes (Ctrl) reste active.
    if(soloBand!=-1){
        soloBand=-1;
        repaint();
    }
}
void SpectrumDisplayLeak::mouseDown(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown()) return;  // clic droit = menu slot
    const float gx0=cachedPadL,gx1=(float)getWidth()-4.f;
    const float gy0=padT(),gy1=padT()+cachedGH;
    if(e.x>=(int)gx0&&e.x<=(int)gx1&&e.y>=(int)gy0&&e.y<=(int)gy1){
        const int band=xToBand((float)e.x);
        if(e.mods.isCtrlDown()||e.mods.isCommandDown()){
            // Ctrl+clic : bascule la bande dans la sélection multi-bandes persistante
            if(band>=0){
                multiSelectMask ^= LeakAudioProcessor::bandBit(band);
                lastDragBand=band;
            }
        } else {
            // Clic simple : bande isolée tant que le bouton est enfoncé, efface la sélection multi
            multiSelectMask=0;
            soloBand=band;
        }
        if(onSelectionChanged) onSelectionChanged();
        repaint();
    }
}
void SpectrumDisplayLeak::mouseDrag(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown()) return;
    if(!(e.mods.isCtrlDown()||e.mods.isCommandDown())) return; // le drag sans Ctrl ne fait rien (mono-bande reste figée sur le clic initial)
    const float gx0=cachedPadL,gx1=(float)getWidth()-4.f;
    const float gy0=padT(),gy1=padT()+cachedGH;
    if(e.x>=(int)gx0&&e.x<=(int)gx1&&e.y>=(int)gy0&&e.y<=(int)gy1){
        const int band=xToBand((float)e.x);
        if(band>=0 && band!=lastDragBand){
            // En glissant avec Ctrl maintenu, chaque nouvelle bande survolée
            // est ajoutée à la sélection (mode "peinture", pas de toggle ici
            // pour éviter de retirer une bande qu'on vient de traverser).
            multiSelectMask |= LeakAudioProcessor::bandBit(band);
            lastDragBand=band;
            repaint();
        }
    }
}
void SpectrumDisplayLeak::mouseUp(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown()) return;
    lastDragBand=-1;
    if(soloBand!=-1){
        soloBand=-1;
        // Relâcher le clic mono-bande retombe sur la sélection multi-bandes
        // (vide si aucune n'est active).
        repaint();
    }
}

void SpectrumDisplayLeak::paint(juce::Graphics& g)
{
    const int W=getWidth(),H=getHeight();if(W<10||H<10)return;
    if(!cacheValid || cacheGen!=Col::generation) rebuildCache();
    const float gy0=padT(),gh=cachedGH,gw=cachedGW,gx0=cachedPadL;
    const float minY=dbToY(-dbRange),zeroY=dbToY(0.f);
    const int N=activeBands;
    const bool isMSMode=(analysisMode==LeakAudioProcessor::AnalysisMode::MS);

    if(gridImageValid){
        g.setColour(Col::bg);g.fillRect(getLocalBounds());
        g.drawImageAt(gridImage,0,0);
    } else {g.setColour(Col::bg);g.fillRect(getLocalBounds());}

    {using AM=LeakAudioProcessor::AnalysisMode;
     float lfs=juce::jlimit(8.f,11.f,gh*.06f);
     g.setFont(Ui::iceland((lfs)));
     if(analysisMode==AM::Mid){g.setColour(Col::midCol.withAlpha(.75f));g.drawText("MID",(int)gx0+2,(int)gy0+2,36,(int)lfs+2,juce::Justification::left);}
     else if(analysisMode==AM::Side){g.setColour(Col::sideCol.withAlpha(.75f));g.drawText("SIDE",(int)gx0+2,(int)gy0+2,36,(int)lfs+2,juce::Justification::left);}
     else if(isMSMode){g.setColour(Col::midCol.withAlpha(.7f));g.drawText("MID",(int)gx0+2,(int)gy0+2,30,(int)lfs+2,juce::Justification::left);
         g.setColour(Col::light(.8f));g.drawText("SIDE",(int)gx0+34,(int)gy0+2,36,(int)lfs+2,juce::Justification::left);}}

    clipOn.clear();clipOff.clear();
    bool anyClipOn=false,anyClipOff=false;
    static const juce::Colour kRefMark=juce::Colour(0xCCFFD200);

    // couleurs de pointe (niveau) et de crête — communes Classic / Blocks 3D
    auto tipColour=[](float d){
        if(d>=-3.f)  return Col::ink(juce::Colour(0xFFFF1500));
        if(d>=-6.f)  return Col::ink(juce::Colour(0xFFFF6600));
        if(d>=-14.f) return Col::ink(juce::Colour(0xFFFFD200));
        if(d>=-26.f) return Col::ink(juce::Colour(0xFF00FF55));
        return Col::ink(juce::Colour(0xFF00FFB2)); };
    auto peakColour=[](float d){
        if(d>=-3.f)  return Col::ink(juce::Colour(0xFFFF1500));
        if(d>=-6.f)  return Col::ink(juce::Colour(0xFFFF6600));
        if(d>=-12.f) return Col::ink(juce::Colour(0xFFFFD200));
        if(d>=-24.f) return Col::ink(juce::Colour(0xFF00FF55));
        return Col::ink(juce::Colour(0xFF00FFB2)); };

    // Blocks 3D : sol éclairé + dalles, dégradé de niveau calé sur le tracé
    juce::ColourGradient front3D, side3D;
    if(blocks3D){
        juce::ColourGradient floor(Col::accent.withAlpha(0.08f), gx0+gw*0.5f, minY,
                                   juce::Colours::transparentBlack, gx0+gw*0.5f, minY-gh*0.45f, false);
        g.setGradientFill(floor); g.fillRect(gx0, gy0, gw, gh);
        for(int i=0;i<N;++i) Spec3D::drawSocle(g, {barCache[i].x, gy0, barCache[i].bw, minY-gy0});
        const float dy=N>0 ? Spec3D::depthFor(barCache[0].bw)*0.7f : 0.f;
        front3D=Spec3D::levelGradient(gy0, minY);
        side3D =Spec3D::levelGradient(gy0-dy, minY-dy);   // le flanc est décalé vers le haut
    }

    for(int i=0;i<N;++i){
        const float bx=barCache[i].x,bw=barCache[i].bw;
        const float db=juce::jmax(levels[i],-dbRange);
        const float barT=juce::jmax(dbToY(db),gy0);
        const float barB=minY,barH=barB-barT;
        if(blocks3D){
            const float peakY=(peakOn&&peaks[i]>-dbRange) ? juce::jmax(dbToY(peaks[i]),gy0) : -1.f;
            Spec3D::drawBlock(g, {bx, gy0, bw, barB-gy0}, barH>0.5f ? barT : barB, tipColour(db),
                              front3D, side3D, peakY, peakColour(peaks[i]));
        }
        else if(barH>0.5f){
            g.drawImage(barGradImage,(int)bx,(int)barT,(int)bw,(int)barH,0,(int)(barT-gy0),1,(int)barH);
            const juce::Colour tipCol=tipColour(db);
            g.setColour(tipCol);
            g.fillRect(bx,barT,bw,1.5f);
            if(i!=soloBand && (multiSelectMask & LeakAudioProcessor::bandBit(i))==0ULL){
                for(int rd=-5;rd>=-(int)dbRange;rd-=5){
                    if(levels[i]>(float)rd){
                        const float ry=std::round(dbToY((float)rd));
                        if(rd%10==0){g.setColour(kRefMark);g.fillRect(bx,ry-1.f,bw,1.5f);}
                        else{g.setColour(Col::pick(juce::Colour(0x44AAFFDD), Col::accent.withAlpha(0.30f)));g.fillRect(bx,ry-0.5f,bw,1.0f);}
                    }
                }
            }
        }
        if(isMSMode){
            const float sDb=juce::jmax(sideLevel[i],-dbRange);
            const float sTop=juce::jmax(dbToY(sDb),gy0),sH=barB-sTop;
            // Side en "verre dépoli" (preview validée) : barre blanche
            // translucide posée sur le Mid — voile sombre léger, contour,
            // reflet vertical et cap blanc. Couleur neutre : elle ne se
            // mélange jamais au dégradé de niveau du Mid.
            const float inset=bw>=8.f ? 2.f : 1.f;
            const float sx=bx+inset, sw=juce::jmax(2.f,bw-2.f*inset);
            if(sH>0.5f){
                const juce::Rectangle<float> glass(sx,sTop,sw,sH);
                g.setColour(Col::bg.withAlpha(0.35f)); g.fillRect(glass);
                g.setGradientFill(juce::ColourGradient(Col::light(0.32f),0.f,sTop,
                                                       Col::light(0.06f),0.f,barB,false));
                g.fillRect(glass);
                g.setColour(Col::light(0.75f)); g.drawRect(glass,1.f);
                if(sw>4.f){
                    g.setColour(Col::light(0.18f));
                    g.fillRect(glass.reduced(1.f).withWidth(juce::jmax(1.f,(sw-2.f)*0.3f)));
                }
                g.setColour(Col::light(1.f)); g.fillRect(sx,sTop,sw,1.5f);
            }
            if(peakOn&&sidePeaks[i]>-dbRange){
                const float spy=juce::jmax(dbToY(sidePeaks[i]),gy0);
                g.setColour(Col::light(0.8f)); g.fillRect(sx,spy-0.75f,sw,1.5f);
            }
        }
        if(!blocks3D && peakOn&&peaks[i]>-dbRange){   // (Blocks 3D : crête dessinée avec le bloc)
            const float py=juce::jmax(dbToY(peaks[i]),gy0);
            const juce::Colour peakCol=peakColour(peaks[i]);
            g.setColour(peakCol.withAlpha(0.28f));g.fillRect(bx,py-3.f,bw,5.f);
            g.setColour(peakCol);g.fillRect(bx,py-1.5f,bw,2.f);
        }
        const float ledY=gy0-16.f;
        if(clipLatch[i]){clipOn.add(bx,ledY,bw,8.f);anyClipOn=true;}
        else            {clipOff.add(bx,ledY,bw,8.f);anyClipOff=true;}
    }

    if(anyClipOff){g.setColour(Col::pick(juce::Colour(0xFF0A1A1A), Col::knobDim));g.fillRectList(clipOff);
                   g.setColour(Col::border);for(auto&r:clipOff)g.drawRect(r,0.5f);}
    if(anyClipOn) {g.setColour(Col::ink(juce::Colour(0xFFFF1010)));g.fillRectList(clipOn);
                   g.setColour(Col::ink(juce::Colour(0xFFFF5555)));for(auto&r:clipOn)g.drawRect(r,0.5f);}

    // Solo visuel : assombrit les bandes non sélectionnées, encadre les bandes actives
    // (soloBand = clic mono-bande momentané, multiSelectMask = sélection Ctrl persistante)
    if(soloBand>=0 || multiSelectMask!=0ULL){
        g.setColour(Col::pick(juce::Colour(0xBB000000), Col::bg.withAlpha(0.73f)));
        for(int i=0;i<N;++i){
            const bool sel = (i==soloBand) || ((multiSelectMask & LeakAudioProcessor::bandBit(i))!=0ULL);
            if(!sel) g.fillRect(barCache[i].x,gy0,barCache[i].bw,gh);
        }
        // cadre tracé À L'INTÉRIEUR de la colonne : il ne doit pas combler
        // l'écart avec les bandes voisines (sinon elles semblent se rejoindre)
        g.setColour(Col::accent);
        for(int i=0;i<N;++i){
            const bool sel = (i==soloBand) || ((multiSelectMask & LeakAudioProcessor::bandBit(i))!=0ULL);
            if(sel) g.drawRect(juce::Rectangle<float>(barCache[i].x,gy0,barCache[i].bw,gh),
                               juce::jlimit(1.f,1.5f,barCache[i].bw*0.2f));
        }
    }

    g.setColour(Col::gridCtr.brighter(.3f));g.drawLine(gx0,zeroY,gx0+gw,zeroY,1.f);
    g.setColour(Col::border);g.drawRect(gx0,gy0,gw,gh,1.f);
    g.setFont(labelFont);g.setColour(Col::textMid);
    // étiquettes du mode avec lequel le cache a été construit (même nombre de bandes)
    const char** labels=(cacheMode==62)?BAND_LABELS_62:(cacheMode==15)?BAND_LABELS_15:BAND_LABELS_31;
    for(int i=0;i<juce::jmin(N, LeakAudioProcessor::bandTableSize(cacheMode));++i) if(barCache[i].showLabel) g.drawSingleLineText(labels[i],(int)barCache[i].labelX,(int)(barCache[i].labelY+labelFontSize));

    if(hoveredBand>=0){
        g.setColour(Col::accent.withAlpha(.18f));g.drawLine(hoverPixX,gy0,hoverPixX,gy0+gh,1.f);
        if(hoveredBand!=lastHoveredBand || forceTipRefresh){
            const float* F=processor.getBandFreqs();float f=F[juce::jmin(hoveredBand, processor.getNumActiveBands()-1)];
            juce::String freqStr=(f>=1000.f)?(juce::String(f/1000.f,(std::fmod(f,1000.f)==0.f)?0:2)+" kHz"):(juce::String((int)f)+" Hz");
            cachedTipStr=isMSMode?freqStr+"  M:"+juce::String(levels[hoveredBand],1)+"  S:"+juce::String(sideLevel[hoveredBand],1)+" dB"
                                 :freqStr+"  |  "+juce::String(levels[hoveredBand],1)+" dB";
            float tfs=juce::jlimit(9.f,12.f,gh*.07f);
            juce::Font tipFont=Ui::iceland(tfs);
            juce::GlyphArrangement ga;ga.addLineOfText(tipFont,cachedTipStr,0.f,0.f);
            cachedTipW=ga.getNumGlyphs()>0?ga.getBoundingBox(0,ga.getNumGlyphs(),true).getWidth()+14.f:80.f;
            lastHoveredBand=hoveredBand;
            forceTipRefresh=false;
        }
        float tfs=juce::jlimit(9.f,12.f,gh*.07f);
        float th=tfs+8.f,tx=hoverPixX+10.f;
        if(tx+cachedTipW>(float)W-4.f) tx=hoverPixX-cachedTipW-10.f;
        g.setColour(Col::panel);g.fillRoundedRectangle(tx,gy0+6.f,cachedTipW,th,3.f);
        g.setColour(Col::border);g.drawRoundedRectangle(tx,gy0+6.f,cachedTipW,th,3.f,.8f);
        g.setFont(Ui::iceland((tfs)));
        g.setColour(Col::accent);g.drawText(cachedTipStr,(int)(tx+7.f),(int)(gy0+6.f),(int)(cachedTipW-8.f),(int)th,juce::Justification::centredLeft);
    }

    Ui::drawTitleDecor(g, (float)getWidth());
    g.setFont(Ui::iceland((10.5f)).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    g.drawFittedText(TR("SPECTRUM"),8,4,juce::jmax(40,getWidth()/2),13,juce::Justification::left,1,0.7f);
}

//==============================================================================
// NeedleVuMeter
//==============================================================================
// Référence 0VU en dBFS : -4 à -30, réglée aux flèches par pas de 2 ou 4 dB
static float clampNeedleCalib(float v) { return juce::jlimit(-30.f, -4.f, (float)juce::roundToInt(v)); }

NeedleVuMeter::NeedleVuMeter(LeakAudioProcessor& p) : processor(p)
{
    skImage = juce::ImageCache::getFromMemory(BinaryData::sidekey_monogram_png, BinaryData::sidekey_monogram_pngSize);
    {
        // le PNG a des marges inégales : on centre sur la partie réellement dessinée
        const juce::Image::BitmapData bd(skImage, juce::Image::BitmapData::readOnly);
        int x0=skImage.getWidth(), y0=skImage.getHeight(), x1=-1, y1=-1;
        for(int y=0;y<skImage.getHeight();++y)
            for(int x=0;x<skImage.getWidth();++x)
                if(bd.getPixelColour(x,y).getAlpha()>24){ x0=juce::jmin(x0,x); y0=juce::jmin(y0,y); x1=juce::jmax(x1,x); y1=juce::jmax(y1,y); }
        skCrop = x1>=x0 ? juce::Rectangle<int>(x0,y0,x1-x0+1,y1-y0+1) : skImage.getBounds();
        skLogo = skImage.getClippedImage(skCrop);
    }

    modeSel  = juce::jlimit(0,2,processor.needleModeSel);
    calibRef = clampNeedleCalib(processor.needleCalibRef);
    modeBtn.onClick=[this]{ modeSel=(modeSel+1)%3; processor.needleModeSel=modeSel; updateHeaderTexts(); repaint(); };
    calibDown.onClick=[this]{ setCalibRef(calibRef-(float)processor.needleCalibStep); };
    calibUp  .onClick=[this]{ setCalibRef(calibRef+(float)processor.needleCalibStep); };
    stepBtn.onClick=[this]{ processor.needleCalibStep = processor.needleCalibStep==2 ? 4 : 2; updateHeaderTexts(); };
    modeBtn  .setTooltip(TR("Channels: LR (average), L or R"));
    calibDown.setTooltip(TR("0 VU reference: lower"));
    calibUp  .setTooltip(TR("0 VU reference: higher"));
    stepBtn  .setTooltip(TR("Arrow step: 2 or 4 dB"));
    calibUp.setArrow(1); calibDown.setArrow(-1);
    for(auto* b : { &modeBtn, &calibDown, &calibUp, &stepBtn }) addAndMakeVisible(*b);
    updateHeaderTexts();

    holdBtn.setClickingTogglesState(true);
    holdBtn.onClick=[this]{ holding=holdBtn.getToggleState(); if(holding) holdPeakDb=needleDb; };
    addAndMakeVisible(holdBtn);

    startTimerHz(60);
}

// pied PEAK / RMS : plus haut en gros texte (trois rangées de texte)
static int needleStatH(int H)
{
    const float s=Ui::textScale;
    return juce::jlimit(juce::roundToInt(42.f*s), juce::roundToInt(58.f*s), juce::roundToInt((float)H*0.15f*s));
}

void NeedleVuMeter::resized()
{
    const int W=getWidth(), H=getHeight();
    // en-tête et menus LR / dBFS à l'échelle du texte : même taille de police
    // que les menus de la barre du bas
    // (un peu plus haut qu'avant : les flèches ▲ / ▼ y sont empilées)
    headerH=juce::roundToInt(30.f*Ui::textScale);
    int cbH=headerH-8;
    // commandes, en hauteurs de bouton : LR, valeur, ▲/▼, pas. Le titre
    // "dBFS METER" doit rester entier : dans un module étroit, elles rétrécissent
    constexpr float kMode=1.7f, kValue=2.7f, kArrows=1.1f, kStep=2.1f;
    constexpr float kSum=kMode+kValue+kArrows+kStep;
    {
        const float titleW=juce::GlyphArrangement::getStringWidth(Ui::iceland(10.5f).withExtraKerningFactor(0.08f), TR("dBFS METER"));
        // 8 bord, titre, 22 d'écart (le relief des boutons déborde), 8 bord, 6 + 2 + 6 entre groupes
        const float room=(float)W-8.f-titleW-22.f-8.f-14.f;
        if((float)cbH*kSum>room) cbH=juce::jmax(12, (int)(room/kSum));
    }
    const int y=(headerH-cbH)/2;
    int x=W-8;
    auto place=[&](juce::Component& c, float k){ const int w=juce::roundToInt((float)cbH*k); x-=w; c.setBounds(x, y, w, cbH); };
    place(stepBtn, kStep);  x-=6;
    {   // ▲ au-dessus de ▼, sur toute la hauteur de l'en-tête
        const int w=juce::roundToInt((float)cbH*kArrows), colH=headerH-4, bh=colH/2;
        x-=w;
        calibUp  .setBounds(x, 2,      w, bh);
        calibDown.setBounds(x, 2+bh,   w, colH-bh);
        x-=2;
    }
    { const int w=juce::roundToInt((float)cbH*kValue); x-=w; calibValueArea={ x, y, w, cbH }; }
    x-=6;
    place(modeBtn, kMode);

    const int statH=needleStatH(H);
    gaugeArea = { 0, headerH, W, H-headerH-statH };

    const int cardW=(W-8)/4;
    holdBtn.setBounds(W-cardW+6, H-statH+6, cardW-12, statH-12);
}

// Mapping non linéaire dB -> angle (degrés, 0=vertical, + à droite) — reproduit
// l'espacement resserré autour de 0 et étiré aux extrêmes d'un VU-mètre classique.
float NeedleVuMeter::angleForDb(float db)
{
    static const float dbs   [] = {-20.f,-10.f,-7.f,-5.f,-3.f,0.f,1.f,2.f,3.f};
    static const float angles[] = {-50.f,-34.f,-27.f,-21.f,-13.f,0.f,15.f,30.f,46.f};
    constexpr int N = 9;
    db = juce::jlimit(dbs[0], dbs[N-1], db);
    for(int i=0;i<N-1;++i)
        if(db<=dbs[i+1]){
            const float t=(db-dbs[i])/(dbs[i+1]-dbs[i]);
            return angles[i]+t*(angles[i+1]-angles[i]);
        }
    return angles[N-1];
}

void NeedleVuMeter::syncFromProcessor()
{
    modeSel  = juce::jlimit(0,2,processor.needleModeSel);
    calibRef = clampNeedleCalib(processor.needleCalibRef);
    updateHeaderTexts();
    repaint();
}
void NeedleVuMeter::setCalibRef(float v)
{
    calibRef = clampNeedleCalib(v);
    processor.needleCalibRef = calibRef;
    repaint();   // la valeur fait partie du cadran en cache (clé)
}
void NeedleVuMeter::updateHeaderTexts()
{
    modeBtn.setButtonText(modeSel==1 ? "L" : modeSel==2 ? "R" : "LR");
    stepBtn.setButtonText(juce::String(processor.needleCalibStep)+" dB");
}
void NeedleVuMeter::timerCallback()
{
    LeakAudioProcessor::VuData d;
    if(!processor.getVuData(d)) return;

    // Lissage léger, uniquement pour les LECTURES NUMÉRIQUES (Peak/RMS) —
    // l'aiguille elle-même part directement des valeurs brutes du processeur
    // pour ne pas cumuler deux étages de lissage (c'était la cause de la lenteur).
    dispPeakL+=(d.peakL-dispPeakL)*0.5f; dispPeakR+=(d.peakR-dispPeakR)*0.5f;
    dispRmsL +=(d.rmsL -dispRmsL )*0.3f; dispRmsR +=(d.rmsR -dispRmsR )*0.3f;

    if(dispPeakL>numPeak || dispPeakR>numPeak) numPeak=juce::jmax(dispPeakL,dispPeakR);
    else numPeak+=(juce::jmax(dispPeakL,dispPeakR)-numPeak)*0.02f;
    const float rmsAvg=(dispRmsL+dispRmsR)*0.5f;
    numRms+=(rmsAvg-numRms)*0.05f;

    // Aiguille : part des valeurs brutes du processeur (un seul étage de lissage,
    // la ballistique elle-même ci-dessous), pour un ressenti "hardware" réactif.
    const float rawPeakL=d.peakL, rawPeakR=d.peakR, rawRmsL=d.rmsL, rawRmsR=d.rmsR;
    const float pL = ppmType?rawPeakL:rawRmsL, pR = ppmType?rawPeakR:rawRmsR;
    const float raw = (modeSel==1)?pL : (modeSel==2)?pR : (pL+pR)*0.5f;
    const float targetDb = raw - calibRef;

    const float dt=1.f/(float)currentHz;
    // Aiguille modélisée comme une masse ressort-amorti (comportement mécanique
    // réaliste : rapide, fluide, avec un très léger dépassement contrôlé — au
    // lieu d'un simple lissage exponentiel qui donnait un ressenti mou).
    const float spring = ppmType ? 620.f : 260.f;   // raideur (rapidité)
    const float damping= ppmType ?  46.f :  30.f;   // amortissement (légèrement sous-critique = petit rebond réaliste)
    const float accel = (targetDb-needleDb)*spring - needleVel*damping;
    needleVel += accel*dt;
    needleDb  += needleVel*dt;

    if(holding) holdPeakDb=juce::jmax(holdPeakDb,needleDb);

    // repeinte seulement si l'aiguille bouge visiblement ou si une lecture
    // affichée (au dixième) change
    const float ang=angleForDb(holding?holdPeakDb:needleDb);
    auto tenth=[](float v){ return juce::roundToInt(v*10.f); };
    if(std::abs(ang-lastPaintedAng)>0.03f || holding!=lastPaintedHold
       || tenth(numPeak)!=tenth(lastPaintedPeak) || tenth(numRms)!=tenth(lastPaintedRms)){
        lastPaintedAng=ang; lastPaintedHold=holding;
        lastPaintedPeak=numPeak; lastPaintedRms=numRms;
        repaint();
    }
}

void NeedleVuMeter::paint(juce::Graphics& g)
{
    const int W=getWidth(), H=getHeight();
    if(W<10||H<10) return;

    const auto ga = gaugeArea.toFloat();
    const float cx=ga.getCentreX();
    const float cy=ga.getBottom()-ga.getHeight()*0.12f;
    const float radius=juce::jmin(ga.getWidth()*0.44f, ga.getHeight()*0.70f);
    auto pointOnArc=[&](float angDeg,float r){
        const float rad=juce::degreesToRadians(angDeg);
        return juce::Point<float>(cx+std::sin(rad)*r, cy-std::cos(rad)*r);
    };

    // ── Partie fixe (fond, en-tête, cadran gradué, textes) : rendue une fois
    // dans une image native (même moteur de rendu que la fenêtre, résolution
    // physique), refaite seulement si la taille, l'échelle ou le thème change.
    const float pxScale=g.getInternalContext().getPhysicalPixelScaleFactor();
    const juce::int64 key=(((juce::int64)W*8191+H)*131+juce::roundToInt(pxScale*100.f))*31
                          +juce::roundToInt(Ui::textScale*100.f)+(juce::int64)headerH*7
                          +(juce::int64)Col::bg.getARGB()*3+(juce::int64)Col::accent.getARGB()*5
                          +(juce::int64)Col::textHi.getARGB()*11+(juce::int64)Col::textMid.getARGB()*13
                          +(juce::int64)Col::border.getARGB()*17+(juce::int64)modeBtn.getX()*19
                          +(juce::int64)juce::roundToInt(calibRef*10.f)*23+(juce::int64)Col::generation*29;
    if(!dialCache.isValid() || key!=dialKey){
        dialKey=key;
        dialCache=juce::Image(juce::Image::RGB, juce::jmax(1,juce::roundToInt((float)W*pxScale)), juce::jmax(1,juce::roundToInt((float)H*pxScale)), false);
        juce::Graphics dg(dialCache);
        dg.addTransform(juce::AffineTransform::scale(pxScale));
        paintDialStatic(dg, ga, cx, cy, radius);
    }
    g.drawImage(dialCache, getLocalBounds().toFloat());
    paintDialDynamic(g, ga, cx, cy, radius);
}

void NeedleVuMeter::paintDialStatic(juce::Graphics& g, juce::Rectangle<float> ga, float cx, float cy, float radius)
{
    const int W=getWidth();
    auto pointOnArc=[&](float angDeg,float r){
        const float rad=juce::degreesToRadians(angDeg);
        return juce::Point<float>(cx+std::sin(rad)*r, cy-std::cos(rad)*r);
    };

    g.setColour(Col::bg); g.fillRect(getLocalBounds());

    // ── En-tête ──────────────────────────────────────────────────────────
    g.setFont(Ui::iceland((10.5f)).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    g.drawFittedText(TR("dBFS METER"),8,0,juce::jmax(40,modeBtn.getX()-22),headerH,juce::Justification::centredLeft,1,0.7f);
    if(Ui::titleStyle==0){ g.setColour(Col::border.withAlpha(0.5f)); g.drawLine(0.f,(float)headerH,(float)W,(float)headerH,0.6f); }
    else Ui::drawTitleDecor(g, (float)W, (float)headerH);
    // référence 0VU entre les flèches, dans un petit écran creux
    if(!calibValueArea.isEmpty()){
        const auto va=calibValueArea.toFloat().reduced(1.f, 0.f);
        g.setColour(Col::pick(juce::Colours::black.withAlpha(0.35f), Col::panel)); g.fillRoundedRectangle(va, 4.f);
        g.setColour(Col::border.withAlpha(0.6f));          g.drawRoundedRectangle(va.reduced(0.5f), 4.f, 0.8f);
        g.setFont(Ui::iceland(juce::jmax(8.f, (float)calibValueArea.getHeight()*0.68f)));
        g.setColour(Col::textHi.withAlpha(0.92f));
        g.drawFittedText(juce::String((int)calibRef)+" dB", calibValueArea, juce::Justification::centred, 1, 0.7f);
    }

    // ── Cadran ───────────────────────────────────────────────────────────
    // Halo doux
    juce::ColourGradient halo(Col::accent.withAlpha(0.05f),cx,cy-radius*0.4f,
                               juce::Colours::transparentBlack,cx,cy-radius*1.1f,false);
    g.setGradientFill(halo); g.fillRect(ga);

    // Graduations + étiquettes
    static const float majDbs[]={-20.f,-10.f,-7.f,-5.f,-3.f,0.f,1.f,2.f,3.f};
    // graduations à l'échelle du cadran (pas du gros texte : -10/-7/-5/-3 sont
    // trop serrés pour grossir sans se chevaucher)
    g.setFont(Ui::icelandAbs(juce::jlimit(9.f,13.f,radius*0.12f)));
    for(float db : majDbs){
        const float ang=angleForDb(db);
        const bool hot=db>0.5f;
        const auto p0=pointOnArc(ang,radius*0.86f);
        const auto p1=pointOnArc(ang,radius*1.0f);
        g.setColour(hot?Col::ink(juce::Colour(0xFFFF3B30)):(db>=-0.01f?Col::light(1.f):Col::accent));
        g.drawLine(p0.x,p0.y,p1.x,p1.y, db==0.f?2.2f:1.6f);
        const auto lp=pointOnArc(ang,radius*1.16f);
        juce::String lbl = (db>0.f? "+" : "") + juce::String((int)db);
        g.drawText(lbl,(int)lp.x-18,(int)lp.y-7,36,14,juce::Justification::centred);
    }
    // Petites graduations intermédiaires non chiffrées (entre -10 et 0, comme la maquette)
    g.setColour(Col::accent.withAlpha(0.45f));
    for(int i=1;i<10;++i){
        const float db=-10.f+(float)i;
        if(db>=-0.4f) continue;
        const float ang=angleForDb(db);
        const auto p0=pointOnArc(ang,radius*0.90f);
        const auto p1=pointOnArc(ang,radius*0.97f);
        g.drawLine(p0.x,p0.y,p1.x,p1.y,1.f);
    }
    // Arc rouge (zone chaude, au-delà de +1)
    {
        juce::Path redArc;
        const int steps=24;
        for(int i=0;i<=steps;++i){
            const float db=1.f+(3.f-1.f)*(float)i/(float)steps;
            const auto pt=pointOnArc(angleForDb(db),radius*1.0f);
            if(i==0) redArc.startNewSubPath(pt); else redArc.lineTo(pt);
        }
        g.setColour(Col::ink(juce::Colour(0xFFFF3B30)).withAlpha(0.85f));
        g.strokePath(redArc, juce::PathStrokeType(2.2f));
    }

    // Texte central "VU" / "dB" / signes — rangées bien séparées, aucun chevauchement
    g.setColour(Col::accent.withAlpha(0.9f));
    // (inscriptions du cadran à l'échelle du cadran, comme les graduations)
    g.setFont(Ui::icelandAbs(radius*0.30f).withExtraKerningFactor(0.1f));
    // ce que lit l'aiguille : des dBFS (0 = référence de l'en-tête) — "VU" et
    // "dB" ne sont plus affichés
    g.drawText("dBFS",juce::Rectangle<float>(cx-radius*0.6f,cy-radius*0.58f,radius*1.2f,radius*0.26f).toNearestInt(),juce::Justification::centred,false);
    g.setFont(Ui::icelandAbs(radius*0.14f));
    g.setColour(Col::textMid.withAlpha(0.5f));
    g.drawText("-",juce::Rectangle<float>(cx-radius*0.44f,cy-radius*0.16f,radius*0.16f,radius*0.16f).toNearestInt(),juce::Justification::centred);
    g.drawText("+",juce::Rectangle<float>(cx+radius*0.28f,cy-radius*0.16f,radius*0.16f,radius*0.16f).toNearestInt(),juce::Justification::centred);

}

// Partie animée : indicateur LIVE/HOLD, aiguille, monogramme, lectures du pied
void NeedleVuMeter::paintDialDynamic(juce::Graphics& g, juce::Rectangle<float> ga, float cx, float cy, float radius)
{
    const int W=getWidth(), H=getHeight();
    auto pointOnArc=[&](float angDeg,float r){
        const float rad=juce::degreesToRadians(angDeg);
        return juce::Point<float>(cx+std::sin(rad)*r, cy-std::cos(rad)*r);
    };

    // Indicateur LIVE
    {
        const bool live = !holding;
        g.setColour((live?Col::accent:Col::textMid).withAlpha(live?0.95f:0.4f));
        g.fillEllipse(ga.getX()+10.f,ga.getY()+8.f,8.f,8.f);
        g.setFont(Ui::iceland((10.f)));
        g.drawText(live?"LIVE":"HOLD",(int)ga.getX()+22,(int)ga.getY()+3,60,16,juce::Justification::left);
    }

    // Aiguille
    const float pivotR = juce::jlimit(9.f,13.f,radius*0.11f);
    const float needleAng = angleForDb(holding?holdPeakDb:needleDb);
    {
        const float ang=needleAng;
        const auto tip=pointOnArc(ang,radius*0.92f);
        // Couleur bien distincte du "VU" en arrière-plan (qui est dans l'accent) —
        // blanc chaud, pour que l'aiguille se détache nettement en toute circonstance.
        // (sur écran Papier : aiguille graphite)
        const juce::Colour needleCol=Col::pick(juce::Colour(0xFFF4F7F5), juce::Colour(0xFF2A323C));
        const float baseW=3.4f;
        const float dx=tip.x-cx, dy=tip.y-cy;
        const float len=std::sqrt(dx*dx+dy*dy);
        const float px=(len>0.0001f)?(-dy/len*baseW):0.f;
        const float py=(len>0.0001f)?( dx/len*baseW):0.f;

        // Ombre portée
        juce::Path shadow;
        shadow.startNewSubPath(cx-px,cy-py);
        shadow.lineTo(cx+px,cy+py);
        shadow.lineTo(tip.x,tip.y);
        shadow.closeSubPath();
        g.setColour(juce::Colours::black.withAlpha(Col::paper ? 0.22f : 0.35f));
        g.fillPath(shadow, juce::AffineTransform::translation(Col::paper ? 1.2f : 0.7f, Col::paper ? 1.2f : 0.7f));

        // Deux facettes (gauche/droite) façon tige métallique arrondie —
        // l'une plus sombre, l'autre plus claire, pour donner du relief 3D.
        juce::Path faceL, faceR;
        faceL.startNewSubPath(cx-px,cy-py); faceL.lineTo(cx,cy); faceL.lineTo(tip.x,tip.y); faceL.closeSubPath();
        faceR.startNewSubPath(cx,cy);       faceR.lineTo(cx+px,cy+py); faceR.lineTo(tip.x,tip.y); faceR.closeSubPath();

        juce::ColourGradient ngDark(needleCol.darker(0.55f).withAlpha(0.9f),cx,cy,
                                     needleCol.darker(0.15f),tip.x,tip.y,false);
        g.setGradientFill(ngDark); g.fillPath(faceL);

        juce::ColourGradient ngLight(needleCol.withAlpha(0.95f),cx,cy,
                                      needleCol.brighter(0.4f),tip.x,tip.y,false);
        g.setGradientFill(ngLight); g.fillPath(faceR);

        // Liseré de contour pour détacher l'aiguille du fond
        juce::Path outline;
        outline.startNewSubPath(cx-px,cy-py); outline.lineTo(tip.x,tip.y); outline.lineTo(cx+px,cy+py);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.strokePath(outline, juce::PathStrokeType(0.8f));

        // Reflet spéculaire central — fine ligne claire le long de l'arête,
        // c'est ce qui vend vraiment l'effet 3D (tige ronde qui capte la lumière).
        g.setColour(juce::Colours::white.withAlpha(Col::paper ? 0.45f : 0.85f));
        g.drawLine(cx,cy,tip.x,tip.y,0.9f);

        g.setColour(Col::knobDim.darker(0.4f));
        g.fillEllipse(cx-pivotR,cy-pivotR,pivotR*2.f,pivotR*2.f);
        g.setColour(needleCol.withAlpha(0.6f));
        g.drawEllipse(cx-pivotR,cy-pivotR,pivotR*2.f,pivotR*2.f,1.2f);
    }

    // Monogramme Side Key — badge sur la charnière : centré sur le pivot (partie
    // opaque du PNG) et solidaire de l'aiguille (tourne avec elle)
    if(skLogo.isValid()){
        const float fit=pivotR*1.55f/(float)juce::jmax(skCrop.getWidth(), skCrop.getHeight());
        const float imgW=(float)skCrop.getWidth()*fit, imgH=(float)skCrop.getHeight()*fit;
        juce::Graphics::ScopedSaveState ss(g);
        g.addTransform(juce::AffineTransform::rotation(juce::degreesToRadians(needleAng), cx, cy));
        g.setOpacity(0.9f);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        // zone en virgule flottante : la version à entiers de drawImage tronquait
        // position et taille (logo décalé d'~1,5 px vers le haut-gauche du pivot)
        if(Col::paper){   // monogramme chromé illisible sur le pivot clair : en silhouette encre
            g.setColour(Col::textHi.withAlpha(0.85f));
            g.drawImage(skLogo, juce::Rectangle<float>(cx-imgW*0.5f, cy-imgH*0.5f, imgW, imgH), juce::RectanglePlacement::stretchToFit, true);
        } else
            g.drawImage(skLogo, juce::Rectangle<float>(cx-imgW*0.5f, cy-imgH*0.5f, imgW, imgH));
    }

    // ── Pied : lectures Peak / RMS (plus de LUFS-I : les deux cartes se
    // partagent la place à gauche du HOLD) ───────────────────────────────
    const int statH=needleStatH(H);
    const int statY=H-statH;
    g.setColour(Col::border.withAlpha(0.5f)); g.drawLine(0.f,(float)statY,(float)W,(float)statY,0.6f);
    const int cardW=((W-8)-(W-8)/4)/2;

    auto fmtDb=[](float v)->juce::String{ return v<=-119.f?juce::String("--"):juce::String(v,1); };
    auto statCard=[&](int idx,const char* label,const juce::String& val,const char* unit,juce::Colour vc){
        const int x=4+idx*cardW;
        // Rangées séquentielles à hauteur fixe — jamais de chevauchement, quelle
        // que soit la hauteur du module (label / valeur / unité bien séparées,
        // marges resserrées pour tenir dans la carte sans déborder du canvas).
        // gros texte : rangées plus hautes, et la valeur se resserre pour tenir
        // dans la carte au lieu de finir en "-1…"
        const int labelH=juce::roundToInt(9.f*Ui::textScale), unitH=labelH, gap=1;
        const int valueH=juce::jmax(14, statH-labelH-unitH-2*gap-2);
        const float tw=(float)(cardW-6);
        int y=statY+2;
        g.setColour(Col::textMid.withAlpha(0.6f));
        Ui::drawFit(g, label, juce::Rectangle<float>((float)x+4.f,(float)y,tw,(float)labelH), juce::Justification::left,
                    Ui::iceland(8.5f).withExtraKerningFactor(0.06f));
        y+=labelH+gap;
        g.setColour(vc);
        Ui::drawFit(g, val, juce::Rectangle<float>((float)x+4.f,(float)y,tw,(float)valueH), juce::Justification::left,
                    Ui::icelandAbs(juce::jlimit(13.f,19.f*Ui::textScale,(float)valueH*0.82f)));
        y+=valueH+gap;
        g.setColour(Col::textMid.withAlpha(0.5f));
        Ui::drawFit(g, unit, juce::Rectangle<float>((float)x+4.f,(float)y,tw,(float)unitH), juce::Justification::left, Ui::iceland(7.5f));
    };
    statCard(0,"PEAK",fmtDb(numPeak),"dBFS", numPeak>=-1.f?Col::ink(juce::Colour(0xFFFF3B30)):Col::accent);
    statCard(1,"RMS", fmtDb(numRms), "dBFS", Col::accent);

}

//==============================================================================
// TopBar
//==============================================================================
TopBar::TopBar()
{
    logoImage = juce::ImageCache::getFromMemory(BinaryData::sidekey_monogram_png,
                                                 BinaryData::sidekey_monogram_pngSize);

    setLookAndFeel(&laf);   // RESET + options : contrôles du châssis neumorphique

    retranslate();   // infobulles
    themeToggle   .onClick=[this]{ if(onThemeToggled)    onThemeToggled   (themeToggle.getToggleState()); };
    contrastToggle.onClick=[this]{ if(onContrastToggled) onContrastToggled(contrastToggle.getToggleState()); };
    modules3DToggle.onClick=[this]{ if(onModules3DToggled) onModules3DToggled(modules3DToggle.getToggleState()); };
    largeTextToggle.onClick=[this]{ if(onLargeTextToggled) onLargeTextToggled(largeTextToggle.getToggleState()); };
    onTopToggle.onClick=[this]{ if(onAlwaysOnTopToggled) onAlwaysOnTopToggled(onTopToggle.getToggleState()); };
    addAndMakeVisible(themeToggle);
    addChildComponent(contrastToggle);   // contraste permanent (v1.51) : interrupteur masqué
    addAndMakeVisible(modules3DToggle);
    addChildComponent(largeTextToggle);   // retiré de l'en-tête (v1.65) : réglage dans Options › Affichage
    // premier plan, clavier, plein écran, volume : propres à l'appli (en plugin,
    // la fenêtre et l'écoute sont à la DAW)
    addChildComponent(onTopToggle);     onTopToggle   .setVisible(! AppMode::isPlugin);
    addChildComponent(keyboardToggle);  keyboardToggle.setVisible(! AppMode::isPlugin);
    setKeyboardState(false);
    keyboardToggle.onClick = [this]{
        const bool on=keyboardToggle.getToggleState();
        setKeyboardState(on);   // infobulle
        if(onKeyboardToggled) onKeyboardToggled(on);
    };

    addAndMakeVisible(gearBtn);
    gearBtn.onClick = [this]{ if(onSettingsClicked) onSettingsClicked(); };
    addChildComponent(fullScreenBtn);  fullScreenBtn.setVisible(! AppMode::isPlugin);
    fullScreenBtn.onClick = [this]{ if(onFullScreenClicked) onFullScreenClicked(); };
    addAndMakeVisible(resetBtn);
    resetBtn.onClick = [this]{ if(onResetClicked) onResetClicked(); };

    // Volume général de Windows (comme le curseur de la barre des tâches) : on
    // écoute plus fort ou moins fort sans quitter Merlin
    volKnob.onValueChange=[this]{ WindowsVolume::set((float)volKnob.getValue()*0.01f); };
    addChildComponent(volKnob);
    if(! AppMode::isPlugin){
        volKnob.setVisible(true);
        showWindowsVolume(WindowsVolume::get());
        startTimerHz(2);   // suit les changements faits dans Windows
    }
}

void TopBar::showWindowsVolume(float v)
{
    volKnob.setEnabled(v>=0.f);
    if(v<0.f) return;
    const double pct=std::round(v*100.0);
    if(std::abs(volKnob.getValue()-pct)>=0.5) volKnob.setValue(pct, juce::dontSendNotification);
}

void TopBar::timerCallback()
{
    if(!isShowing() || volKnob.isMouseButtonDown()) return;
    showWindowsVolume(WindowsVolume::get());
}

// Knob neumorphique du thème clair de Sevenpack Alpha, contraste renforcé,
// dans tous les thèmes (v1.73.4, aperçu preview/merlin_knob_volume_preview.html
// validé) : corps plat de la couleur de la plaque, relief sur le bord,
// couronne de 72 traits, traits bleus allumés jusqu'à la valeur, point bleu
// creusé. « VOL » et la valeur à gauche du knob.
void VolumeRing::paint(juce::Graphics& g)
{
    const auto all=getLocalBounds().toFloat();
    const auto area=all.withSizeKeepingCentre(juce::jmin(all.getWidth(), all.getHeight()), juce::jmin(all.getWidth(), all.getHeight()));
    const auto rp=getRotaryParameters();
    const float startAngle=rp.startAngleRadians, endAngle=rp.endAngleRadians;
    const float pos=(float)valueToProportionOfLength(getValue());
    const bool lively=isEnabled();
    const auto accent=Neu::accentInk();   // turquoise de Merlin (v1.74 : le bleu de Sevenpack jurait)
    const float R=juce::jmin(area.getWidth(), area.getHeight())*0.5f/AlphaKnob::kExtent;
    const auto c=area.getCentre();
    constexpr bool contrast=true;   // contraste renforcé (choisi sur l'aperçu)

    {
        const float scale=juce::jmax(0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        const auto& body=knobCache->body(R*scale, Neu::light, contrast);
        const float half=(float)body.getWidth()*0.5f/scale;
        g.drawImage(body, juce::Rectangle<float>(c.x-half, c.y-half, half*2.0f, half*2.0f));

        // traits de la couronne allumés du départ jusqu'à la valeur (le plus proche toujours allumé)
        const float startDeg=juce::radiansToDegrees(startAngle)-360.0f;   // -135 (rotary 1.25 pi .. 2.75 pi)
        const float spanDeg=juce::radiansToDegrees(endAngle-startAngle);
        const float valueDeg=startDeg+pos*spanDeg;
        const float nearest=std::round(valueDeg/5.0f)*5.0f;
        const float r0=1.08f*R, r1=1.30f*R;
        juce::Path ticks;
        for(float d=startDeg; d<=startDeg+spanDeg+0.01f; d+=5.0f){
            if(!((d<=valueDeg+0.01f) || std::abs(d-nearest)<0.01f)) continue;
            const float a=juce::degreesToRadians(d);
            ticks.startNewSubPath(c.x+std::sin(a)*r0, c.y-std::cos(a)*r0);
            ticks.lineTo(c.x+std::sin(a)*r1, c.y-std::cos(a)*r1);
        }
        const float w=juce::jmax(0.9f/scale, 0.0497f*R)+0.6f/scale;
        const auto lit=lively ? accent : Neu::textDim.withAlpha(0.55f);
        g.setColour(lit.withMultipliedAlpha(0.28f));   // halo sans flou
        g.strokePath(ticks, juce::PathStrokeType(w*2.6f));
        g.setColour(lit);
        g.strokePath(ticks, juce::PathStrokeType(w));

        const float ang=startAngle+pos*(endAngle-startAngle);
        const auto& dot=knobCache->dot(juce::jmax(2.0f, AlphaKnob::kDotSize*R)*scale*(contrast ? 1.45f : 1.0f), lively, accent);
        const float dr=AlphaKnob::kDotRadius*R, dh=(float)dot.getWidth()*0.5f/scale;
        const juce::Point<float> p(c.x+std::sin(ang)*dr, c.y-std::cos(ang)*dr);
        g.drawImage(dot, juce::Rectangle<float>(p.x-dh, p.y-dh, dh*2.0f, dh*2.0f));
    }

    // valeur au centre du knob (v1.76.3, choisie sur l'aperçu) : à l'intérieur du
    // tracé du point (0,67 R), qui ne la recouvre jamais
    const auto val=lively ? juce::String(juce::roundToInt(getValue())) : juce::String("--");
    g.setColour(Neu::text);
    g.setFont(Ui::fitFont(Ui::icelandAbs(R*0.66f), val, R*0.95f));
    g.drawText(val, juce::Rectangle<float>(R*1.2f, R*0.8f).withCentre(c.translated(0.f, R*0.04f)), juce::Justification::centred, false);
}

void StepArrow::paintButton(juce::Graphics& g, bool over, bool down)
{
    const auto r=getLocalBounds().toFloat();
    const float s=juce::jmin(r.getWidth(), r.getHeight())*0.42f;
    const auto c=r.getCentre().translated(0.f, down ? 0.5f : 0.f);
    juce::Path t;
    if(right) t.addTriangle(c.x-s*0.45f, c.y-s*0.6f, c.x-s*0.45f, c.y+s*0.6f, c.x+s*0.6f, c.y);
    else      t.addTriangle(c.x+s*0.45f, c.y-s*0.6f, c.x+s*0.45f, c.y+s*0.6f, c.x-s*0.6f, c.y);
    g.setColour(over ? Neu::accentInk() : Neu::textDim);
    g.fillPath(t);
}

//==============================================================================
// PresetBar — au centre de la bande du bas (v1.73)
//==============================================================================
PresetBar::PresetBar()
{
    addAndMakeVisible(presetCombo);
    addAndMakeVisible(prevBtn);
    addAndMakeVisible(nextBtn);
    prevBtn.onClick=[this]{ step(-1); };
    nextBtn.onClick=[this]{ step(1); };
    retranslate();
    presetCombo.onChange = [this]{
        const int id = presetCombo.getSelectedId();
        if(id >= 1 && id <= 4){                       // presets d'usine
            shownId=id; shownPreset.clear();
            if(onPresetSelected) onPresetSelected(id - 1);
            return;
        }
        if(id >= kUserPresetBaseId && id < kUserPresetBaseId+userPresets.size()){
            if(onUserPresetSelected) onUserPresetSelected(userPresets[id-kUserPresetBaseId]);
            return;
        }
        // actions : l'affichage revient sur le preset en cours
        presetCombo.setSelectedId(shownId, juce::dontSendNotification);
        if(id==kSavePresetId   && onSavePresetRequested)  onSavePresetRequested();
        if(id==kDeletePresetId && onDeletePresetRequested) onDeletePresetRequested(shownPreset);
        if(id==kPresetFolderId && onOpenPresetFolder)     onOpenPresetFolder();
    };
}

void TopBar::retranslate()
{
    themeToggle    .setTooltip(TR("Dark / light theme"));
    contrastToggle .setTooltip(TR("High contrast"));
    modules3DToggle.setTooltip(TR("3D modules"));
    largeTextToggle.setTooltip(TR("Large text"));
    onTopToggle.setTooltip(TR("Always on top"));
    fullScreenBtn  .setTooltip(TR("Full screen (F11)"));
    volKnob.setTooltip(TR("PC VOLUME"));
    setKeyboardState(keyboardToggle.getToggleState());
    repaint();
}

void PresetBar::retranslate()
{
    prevBtn.setTooltip(TR("Preset précédent"));
    nextBtn.setTooltip(TR("Preset suivant"));
    presetCombo.setTextWhenNothingSelected(TR("PRESET"));
    const auto names=userPresets;
    const auto shown=shownPreset;
    setPresetList(names, shown);
}

// flèches : preset suivant / précédent, presets d'usine puis utilisateur, en boucle
void PresetBar::step(int dir)
{
    juce::Array<int> ids { 1, 2, 3, 4 };
    for(int i=0;i<userPresets.size();++i) ids.add(kUserPresetBaseId+i);
    const int n=ids.size();
    const int cur=ids.indexOf(presetCombo.getSelectedId());
    const int next = cur<0 ? (dir>0 ? 0 : n-1) : ((cur+dir)%n+n)%n;
    presetCombo.setSelectedId(ids[next], juce::sendNotificationSync);
}

void PresetBar::resized()
{
    const int H=getHeight(), aw=juce::jmin(H, juce::roundToInt(22.f*Ui::textScale));
    prevBtn.setBounds(0, 0, aw, H);
    nextBtn.setBounds(getWidth()-aw, 0, aw, H);
    presetCombo.setBounds(aw+4, 0, juce::jmax(20, getWidth()-2*aw-8), H);
}

void PresetBar::setPresetList(const juce::StringArray& names, const juce::String& current)
{
    userPresets=names;
    presetCombo.clear(juce::dontSendNotification);
    presetCombo.addSectionHeading(TR("FACTORY"));
    presetCombo.addItem(TR("MIXING"),      1);
    presetCombo.addItem(TR("MASTERING"),   2);
    presetCombo.addItem(TR("SOUND DESIGN"),3);
    presetCombo.addItem(TR("TV"),          4);
    presetCombo.addSectionHeading(TR("MY PRESETS"));
    for(int i=0;i<names.size();++i) presetCombo.addItem(names[i], kUserPresetBaseId+i);
    presetCombo.addSeparator();
    presetCombo.addItem(TR("Enregistrer le preset…"), kSavePresetId);
    const int cur=names.indexOf(current);
    if(cur>=0) presetCombo.addItem(TR("Supprimer « {0} »").replace("{0}", current), kDeletePresetId);
    presetCombo.addItem(TR("Ouvrir le dossier des presets"), kPresetFolderId);
    shownPreset = cur>=0 ? current : juce::String();
    shownId     = cur>=0 ? kUserPresetBaseId+cur : 0;
    presetCombo.setSelectedId(shownId, juce::dontSendNotification);
}
void TopBar::resized()
{
    const int H=getHeight();
    const float s=Ui::textScale, hk=headerScale();
    auto cap=[hk](float v){ return juce::roundToInt(v*hk); };
    // ── à droite : monogramme (réservé), options, plein écran, RESET, volume ──
    // (tout est centré sur la hauteur de l'en-tête)
    const int gearW=juce::jlimit(26,cap(32.f),H*54/100);
    const int rightReserve=14+juce::jlimit(18,cap(34.f),(int)((float)H*0.84f))+cap(12.f); // place réservée au logo Side Key
    int x=getWidth()-rightReserve-gearW;
    const int gy=(H-gearW)/2;
    gearBtn.setBounds(x,gy,gearW,gearW);
    if(! AppMode::isPlugin){   // plein écran, avant le menu d'options (appli seulement)
        x -= (gearW+4);
        fullScreenBtn.setBounds(x,gy,gearW,gearW);
    }
    // carré : le texte du bouton s'ajuste tout seul à la largeur ; les bounds
    // débordent autour pour que son relief ne soit pas coupé
    const int rh=juce::jlimit(20, juce::roundToInt(26.f*s*hk), juce::jmax(20, H-12));
    constexpr int reliefBleed=7;
    x -= rh+cap(12.f);
    resetBtn.setReliefMargin(2.5f+(float)reliefBleed);
    resetBtn.setBounds(juce::Rectangle<int>(x,(H-rh)/2,rh,rh).expanded(reliefBleed));
    if(! AppMode::isPlugin){
        // knob de Sevenpack Alpha (sa couronne déborde jusqu'à 1,3 R), valeur au centre
        const int vd=juce::jlimit(30, cap(50.f), H-2);
        x -= vd+cap(10.f);
        volKnob.setBounds(x,(H-vd)/2,vd,vd);
    }

    // ── à gauche : interrupteurs en 2×2, alignés sur le bord des écrans ──
    // en plugin : thème et 3D seulement, sur une rangée (contrastToggle masqué : contraste permanent)
    const bool twoRows=! AppMode::isPlugin;
    const int th=twoRows ? juce::jlimit(14,cap(22.f),(H-10)/2) : juce::jlimit(20,cap(26.f),H*55/100);
    const int tw=juce::roundToInt((float)th*2.3f);
    const int tx=18, tx2=tx+tw+cap(8.f);
    if(twoRows){
        const int rg=cap(4.f), y0=(H-2*th-rg)/2, y1=y0+th+rg;
        themeToggle.setBounds(tx, y0,tw,th);  modules3DToggle.setBounds(tx2,y0,tw,th);
        onTopToggle.setBounds(tx, y1,tw,th);  keyboardToggle .setBounds(tx2,y1,tw,th);
    } else {
        const int ty=(H-th)/2;
        themeToggle.setBounds(tx,ty,tw,th);   modules3DToggle.setBounds(tx2,ty,tw,th);
    }
}

// Logotype de l'en-tête (v1.60) : MERLIN en Audiowide (police OFL embarquée,
// licence Source/Audiowide-OFL.txt), gravé dans la plaque, puis le α tracé
// d'un trait dans une pastille bombée
static const juce::Typeface::Ptr& audiowideTypeface()
{
    static const juce::Typeface::Ptr tf=juce::Typeface::createSystemTypefaceFor(BinaryData::AudiowideRegular_ttf,
                                                                                  (size_t)BinaryData::AudiowideRegular_ttfSize);
    return tf;
}

// α d'un seul trait (boîte 100×100, ligne de base y=82, centre de l'encre 53,55)
static juce::Path alphaGlyph()
{
    juce::Path p;
    p.startNewSubPath(77.f,27.f);
    p.cubicTo(73.f,40.f, 70.f,53.f, 64.f,65.f);
    p.cubicTo(58.f,76.f, 50.f,83.f, 40.f,83.f);
    p.cubicTo(24.f,83.f, 14.f,71.f, 14.f,57.f);
    p.cubicTo(14.f,42.f, 26.f,31.f, 40.f,31.f);
    p.cubicTo(55.f,31.f, 63.f,45.f, 68.f,60.f);
    p.cubicTo(72.f,72.f, 76.f,82.f, 85.f,82.f);
    p.cubicTo(88.f,82.f, 90.f,81.f, 92.f,79.f);
    return p;
}

// Pastille α bombée (même relief que les boutons), centre c, rayon r
static void drawAlphaBadge(juce::Graphics& g, juce::Point<float> c, float r)
{
    const float k=r/15.f;   // proportions validées sur la preview (rayon 15 px)
    juce::Path disc; disc.addEllipse(c.x-r, c.y-r, 2.f*r, 2.f*r);
    const int blur=juce::jmax(2, juce::roundToInt(4.f*k));
    const int dk=juce::jmax(1, juce::roundToInt(2.f*k)), lt=juce::jmax(1, juce::roundToInt(1.5f*k));
    juce::DropShadow(Neu::shadowDark,  blur, {  dk,  dk }).drawForPath(g, disc);
    juce::DropShadow(Neu::shadowLight, blur, { -lt, -lt }).drawForPath(g, disc);
    g.setColour(juce::Colour(Neu::light ? 0xFFE8EDF3 : 0xFF2B3037));
    g.fillPath(disc);
    static const juce::Path alpha=alphaGlyph();
    auto a=alpha;
    a.applyTransform(juce::AffineTransform::translation(-53.f,-55.f).scaled(16.f/60.f*k).translated(c.x, c.y));
    g.setColour(Neu::light ? Col::accent.darker(0.45f) : Col::accent);
    g.strokePath(a, juce::PathStrokeType(2.2f*k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void TopBar::paint(juce::Graphics& g)
{
    // Pas de fond : la barre de titre est posée sur la plaque neumorphique
    // (dessinée par l'éditeur), seuls textes, logos et contrôles sont peints.
    const int W=getWidth(),H=getHeight();
    titleBounds = juce::Rectangle<float>(0.f,0.f,(float)W,(float)H);

    // Titre principal + sous-titre ("ADVANCED AUDIO ANALYZER") — le nom du
    // plugin est MERLIN α (MERLIN MTR 2 avant la v1.47), sous-titre en dessous.
    // (v1.73.1 : le logotype d'origine est gardé avec le design de Sunny)
    const bool hasSubtitle = H >= 34;
    const float hk = headerScale();
    const float fs = hasSubtitle ? juce::jlimit(14.f,22.f*hk,(float)H*.42f) : juce::jlimit(10.f,18.f,(float)H*.58f);
    const int titleY = hasSubtitle ? (int)((float)H*0.10f) : 0;
    const int titleH = hasSubtitle ? (int)((float)H*0.62f) : H;

    // Logotype MERLIN + pastille α, centré ; capitales à la hauteur de l'ancien titre
    const float cap=0.68f*fs*Ui::textScale;
    juce::Font wf(juce::FontOptions(audiowideTypeface()).withHeight(100.f));
    {
        juce::GlyphArrangement m; m.addLineOfText(wf, "M", 0.f, 0.f);
        const float cap100=juce::jmax(1.f, -m.getBoundingBox(0,-1,false).getY());   // encre au-dessus de la ligne de base
        wf=wf.withHeight(100.f*cap/cap100).withExtraKerningFactor(0.08f);
    }
    juce::GlyphArrangement word; word.addLineOfText(wf, "MERLIN", 0.f, 0.f);
    const auto ink=word.getBoundingBox(0,-1,false);
    const float r=0.79f*cap, gap=0.47f*cap;
    const float x0=((float)W-(ink.getWidth()+gap+2.f*r))*0.5f-ink.getX();
    // sous-titre + LCD sous le logotype ; le bloc entier est centré sur la hauteur (v1.74)
    const auto sf=Ui::iceland((juce::jlimit(7.f,9.5f*hk,(float)H*0.16f))).withExtraKerningFactor(0.22f);
    const juce::Font lf(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), sf.getHeight()*1.05f, juce::Font::bold));
    const float lcdH=lf.getHeight()+4.f, lineH=(float)H*0.28f;
    float base=(float)titleY+(float)titleH*0.5f+cap*0.5f, lineY=(float)H*0.68f;
    {
        const float top=base-cap*0.5f-r;
        const float bottom=hasSubtitle ? lineY+lineH*0.5f+lcdH*0.5f : base;
        const float dy=std::round((float)H*0.5f-(top+bottom)*0.5f);
        base+=dy; lineY+=dy;
    }
    auto drawWord=[&](juce::Colour c, float dx, float dy){
        g.setColour(c);
        word.draw(g, juce::AffineTransform::translation(x0+dx, base+dy));
    };
    if(glitching){
        drawWord(juce::Colour(0xFFFF0033).withAlpha(0.55f), (float)(rng.nextInt(7)-3), (float)(rng.nextInt(4)-2));
        drawWord(juce::Colour(0xFF00FFCC).withAlpha(0.50f), (float)(rng.nextInt(7)-3), (float)(rng.nextInt(4)-2));
        const int gy=rng.nextInt(H);
        g.setColour(juce::Colour(0xFF00FFB2).withAlpha(0.25f));
        g.fillRect(0,gy,W,juce::jlimit(1,3,rng.nextInt(4)));
        drawWord(Neu::text, (float)(rng.nextInt(3)-1), 0.f);
    } else {
        // gravé dans la plaque : reflet clair dessous, ombre dessus
        drawWord(Neu::shadowLight, 0.f, 1.f);
        drawWord(Neu::light ? juce::Colour(0x8C7887A0) : juce::Colours::black.withAlpha(0.55f), 0.f, -1.f);
        drawWord(juce::Colour(Neu::light ? 0xFF4A5462 : 0xFFC9D0D9), 0.f, 0.f);
    }
    drawAlphaBadge(g, { x0+ink.getRight()+gap+r, base-cap*0.5f }, r);

    if(hasSubtitle){
        // sous-titre + petit écran LCD de la fréquence d'échantillonnage (affichage seul)
        const auto sub=TR("ADVANCED AUDIO ANALYZER");
        const float subW=juce::GlyphArrangement::getStringWidth(sf, sub);
        const juce::String srText=sampleRate>0.0 ? juce::String(sampleRate/1000.0, 1)+" kHz" : juce::String("-- kHz");
        const float lcdW=juce::GlyphArrangement::getStringWidth(lf, srText)+4.f;
        const float sgap=10.f, sx0=((float)W-(subW+sgap+lcdW))*0.5f;
        g.setFont(sf);
        g.setColour(Neu::textDim);
        g.drawText(sub, juce::Rectangle<float>(sx0, lineY, subW+6.f, lineH), juce::Justification::centredLeft, false);

        // fréquence d'échantillonnage, sans cadre (v1.76.2, demande du user) :
        // chiffres verts à léger halo sur la plaque sombre, vert foncé sur la claire
        const juce::Rectangle<float> lcd(sx0+subW+sgap, lineY+lineH*0.5f-lcdH*0.5f, lcdW, lcdH);
        g.setFont(lf);
        if(! Neu::light){
            g.setColour(juce::Colour(0xff2fe0a8).withAlpha(0.22f));
            for(float d : { -0.8f, 0.8f })
                g.drawText(srText, lcd.translated(d, 0.f), juce::Justification::centred, false);
        }
        g.setColour(Neu::light ? juce::Colour(0xff0a8a64) : juce::Colour(0xff2fe0a8));
        g.drawText(srText, lcd, juce::Justification::centred, false);
    }

    // démo en cours : pastille ambre à droite de la pastille α (clic = licence)
    demoBounds={};
    if(demoText.isNotEmpty()){
        const auto f=juce::Font(Lang::withScriptFallback(juce::FontOptions(juce::jmax(9.f, cap*0.62f)))).boldened();
        const float tw=juce::GlyphArrangement::getStringWidth(f, demoText);
        demoBounds={ x0+ink.getRight()+gap+2.f*r+cap*0.7f, base-cap*0.5f-cap*0.62f, tw+cap*1.2f, cap*1.24f };
        g.setColour(juce::Colour(0xff3a2e0e));
        g.fillRoundedRectangle(demoBounds, demoBounds.getHeight()*0.5f);
        g.setColour(juce::Colour(0xff6b5418));
        g.drawRoundedRectangle(demoBounds.reduced(0.5f), demoBounds.getHeight()*0.5f, 1.f);
        g.setColour(juce::Colour(0xffffcc4d));
        g.setFont(f);
        g.drawText(demoText, demoBounds, juce::Justification::centred, false);
    }

    // Logo Side Key (monogramme) — coin haut-droit (clic → About)
    if(logoImage.isValid()){
        const float logoH=juce::jlimit(18.f,34.f*hk,(float)H*0.84f);
        const float aspect=(float)logoImage.getWidth()/(float)juce::jmax(1,logoImage.getHeight());
        const float logoW=logoH*aspect;
        const float lx=(float)W-logoW-14.f;
        const float ly=((float)H-logoH)*0.5f;
        // monogramme chromé (clair) : en silhouette teinte texte sur châssis clair
        if(Neu::light){
            g.setColour(Neu::text.withAlpha(0.85f));
            g.drawImage(logoImage, (int)lx, (int)ly, (int)logoW, (int)logoH,
                        0, 0, logoImage.getWidth(), logoImage.getHeight(), true);
        } else {
            g.setOpacity(0.92f);
            g.drawImage(logoImage, (int)lx, (int)ly, (int)logoW, (int)logoH,
                        0, 0, logoImage.getWidth(), logoImage.getHeight());
            g.setOpacity(1.0f);
        }
        logoBounds = juce::Rectangle<float>(lx,ly,logoW,logoH).expanded(4.f);
    }
}

//==============================================================================
// ControlsBarLeak
//==============================================================================
ControlsBarLeak::ControlsBarLeak(SpectrumDisplayLeak& d, LeakAudioProcessor& p, VuMeterLeak& vu, GpuOscilloscopeDisplay& osc)
    : display(d), processor(p), vuMeter(vu), oscillo(osc)
{
    setLookAndFeel(&laf);
    peakBtn.setClickingTogglesState(true);peakBtn.setToggleState(true,juce::dontSendNotification);
    peakBtn.onClick=[this]{ processor.peakHoldOn=peakBtn.getToggleState(); display.setPeakHoldEnabled(peakBtn.getToggleState());};
    addAndMakeVisible(peakBtn);
    peakSel.addItem("0.5 s",1);peakSel.addItem("1 s",2);peakSel.addItem("2 s",3);
    peakSel.addItem("3 s",4);peakSel.addItem("5 s",5);peakSel.addItem("inf",6);
    peakSel.setSelectedId(3,juce::dontSendNotification);
    peakSel.onChange=[this]{static const float v[]={.5f,1.f,2.f,3.f,5.f,99999.f};
        int idx=peakSel.getSelectedId()-1;if(idx>=0&&idx<6){ processor.peakHoldIdx=idx; display.setPeakHoldSec(v[idx]); }};
    addAndMakeVisible(peakSel);
    rangeSel.addItem("-30 dB",1);rangeSel.addItem("-40 dB",2);rangeSel.addItem("-50 dB",3);
    rangeSel.addItem("-60 dB",4);rangeSel.addItem("-70 dB",5);rangeSel.setSelectedId(3,juce::dontSendNotification);
    rangeSel.onChange=[this]{static const float rv[]={30.f,40.f,50.f,60.f,70.f};
        int idx=rangeSel.getSelectedId()-1;if(idx>=0&&idx<5){ processor.dbRangeVal=rv[idx]; display.setDbRange(rv[idx]); }};
    addAndMakeVisible(rangeSel);
    midBtn.setClickingTogglesState(true);sideBtn.setClickingTogglesState(true);
    updateMSMode=[this]{
        bool mOn=midBtn.getToggleState(),sOn=sideBtn.getToggleState();
        LeakAudioProcessor::AnalysisMode mode;
        if(mOn&&sOn)  mode=LeakAudioProcessor::AnalysisMode::MS;
        else if(mOn)  mode=LeakAudioProcessor::AnalysisMode::Mid;
        else if(sOn)  mode=LeakAudioProcessor::AnalysisMode::Side;
        else          mode=LeakAudioProcessor::AnalysisMode::Stereo;
        processor.setAnalysisMode(mode);display.setAnalysisMode(mode);};
    midBtn.onClick=updateMSMode;sideBtn.onClick=updateMSMode;
    addAndMakeVisible(midBtn);addAndMakeVisible(sideBtn);
    bands15Btn.setClickingTogglesState(true);
    bands31Btn.setClickingTogglesState(true);
    bands62Btn.setClickingTogglesState(true);
    bands31Btn.setToggleState(true,juce::dontSendNotification);
    auto setBands=[this](int n){
        bands15Btn.setToggleState(n==15,juce::dontSendNotification);
        bands31Btn.setToggleState(n==31,juce::dontSendNotification);
        bands62Btn.setToggleState(n==62,juce::dontSendNotification);
        processor.setNumActiveBands(n);
        display.setActiveBands(processor.getNumActiveBands());
    };
    bands15Btn.onClick=[this,setBands]{setBands(15);};
    bands31Btn.onClick=[this,setBands]{setBands(31);};
    bands62Btn.onClick=[this,setBands]{setBands(62);};
    addAndMakeVisible(bands15Btn);addAndMakeVisible(bands31Btn);addAndMakeVisible(bands62Btn);
    addAndMakeVisible(presets);   // RESET et volume Windows : dans l'en-tête (v1.73)
}
void ControlsBarLeak::syncFromProcessor()
{
    const int n=processor.getBandMode();
    bands15Btn.setToggleState(n==15,juce::dontSendNotification);
    bands31Btn.setToggleState(n==31,juce::dontSendNotification);
    bands62Btn.setToggleState(n==62,juce::dontSendNotification);
    using AM=LeakAudioProcessor::AnalysisMode;
    const auto mode=processor.getAnalysisMode();
    midBtn .setToggleState(mode==AM::Mid||mode==AM::MS,juce::dontSendNotification);
    sideBtn.setToggleState(mode==AM::Side||mode==AM::MS,juce::dontSendNotification);

    // Peak hold (on/off + durée)
    peakBtn.setToggleState(processor.peakHoldOn,juce::dontSendNotification);
    display.setPeakHoldEnabled(processor.peakHoldOn);
    {
        static const float v[]={.5f,1.f,2.f,3.f,5.f,99999.f};
        const int idx=juce::jlimit(0,5,processor.peakHoldIdx);
        peakSel.setSelectedId(idx+1,juce::dontSendNotification);
        display.setPeakHoldSec(v[idx]);
    }
    // Range
    {
        static const float rv[]={30.f,40.f,50.f,60.f,70.f};
        int idx=2;
        for(int i=0;i<5;++i) if(std::abs(rv[i]-processor.dbRangeVal)<0.5f) idx=i;
        rangeSel.setSelectedId(idx+1,juce::dontSendNotification);
        display.setDbRange(rv[idx]);
    }
    syncOscFromProcessor();
}

// Oscilloscope : durée de fenêtre + amplitude (réglées par les faders du module)
void ControlsBarLeak::syncOscFromProcessor()
{
    oscillo.setTimeDiv(processor.oscTimeDiv);
    oscillo.setAmplitude(processor.oscAmpVal);
    oscillo.refreshFaders();
}
void ControlsBarLeak::paint(juce::Graphics& g)
{
    // Pas de fond : la bande est posée sur la plaque (design de Sunny)
    g.setFont(Ui::iceland(11.f).withExtraKerningFactor(0.08f));
    g.setColour(Neu::textDim);
    const juce::String titles[4]={ TR("PEAK HOLD"), TR("RANGE"), "MS", TR("VIEW") };
    for(int i=0;i<4;++i)
        g.drawText(titles[i], groupLabel[i], juce::Justification::centredLeft, false);

    // séparateurs gravés entre groupes : filet sombre + filet clair
    for(float dx : groupDividerX){
        g.setColour(Neu::shadowDark.withMultipliedAlpha(0.6f));
        g.drawLine(dx,6.f,dx,(float)getHeight()-6.f,1.0f);
        g.setColour(Neu::shadowLight);
        g.drawLine(dx+1.f,6.f,dx+1.f,(float)getHeight()-6.f,1.0f);
    }
}
void ControlsBarLeak::resized()
{
    // Une rangée, titres en ligne : PEAK HOLD, RANGE, MS à gauche, presets au
    // centre, VIEW à droite. Les bounds des boutons/champs incluent 2,5 px de
    // marge pour le relief neumorphique (voir NeuLookAndFeel) ; les dimensions
    // suivent la taille de texte.
    const float s=Ui::textScale;
    auto px=[s](float v){ return juce::roundToInt(v*s); };
    const int H=getHeight();
    const int bh=juce::jlimit(20,px(28.f),juce::jmax(20,H-4));
    const int by=(H-bh)/2;
    const auto lf=Ui::iceland(11.f).withExtraKerningFactor(0.08f);
    const juce::String titles[4]={ TR("PEAK HOLD"), TR("RANGE"), "MS", TR("VIEW") };
    auto label=[&](int& x, int i){
        const int w=juce::roundToInt(juce::GlyphArrangement::getStringWidth(lf, titles[i]))+px(4.f);
        groupLabel[i]={x,0,w,H};
        x+=w+px(5.f);
    };
    const int groupGap=px(10.f);
    int x=px(4.f);

    label(x,0);
    peakBtn.setBounds(x,by,px(42.f),bh); x+=px(44.f);
    peakSel.setBounds(x,by,px(70.f),bh); x+=px(70.f);
    x+=groupGap; groupDividerX[0]=(float)x; x+=groupGap;
    label(x,1);
    rangeSel.setBounds(x,by,px(72.f),bh); x+=px(72.f);
    x+=groupGap; groupDividerX[1]=(float)x; x+=groupGap;
    label(x,2);
    midBtn .setBounds(x,by,px(44.f),bh); x+=px(46.f);
    sideBtn.setBounds(x,by,px(44.f),bh); x+=px(44.f);
    const int leftEnd=x;

    // VIEW, calé à droite
    const int bbw=px(34.f), bgap=px(2.f);
    int xr=getWidth()-px(4.f)-(bbw*3+bgap*2);
    bands15Btn.setBounds(xr,             by,bbw,bh);
    bands31Btn.setBounds(xr+bbw+bgap,    by,bbw,bh);
    bands62Btn.setBounds(xr+(bbw+bgap)*2,by,bbw,bh);
    const int vw=juce::roundToInt(juce::GlyphArrangement::getStringWidth(lf, titles[3]))+px(4.f);
    groupLabel[3]={xr-px(5.f)-vw,0,vw,H};
    const int rightStart=groupLabel[3].getX();

    // presets au centre de l'espace restant
    const int room=rightStart-leftEnd-px(24.f);
    const int pw=juce::jlimit(juce::jmin(room, px(110.f)), px(320.f), room);
    presets.setBounds(leftEnd+(rightStart-leftEnd-pw)/2, by, juce::jmax(0, pw), bh);
}

//==============================================================================
// AboutOverlay (v1.70) — page À propos dans l'esprit de Sevenpack Alpha, aux
// couleurs de la plaque neumorphique (voir PluginEditor.h)
//==============================================================================
namespace
{
    juce::String utf8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }

    // police des Options (Segoe UI / sans-serif système), tailles en points
    // comme l'aperçu validé (13 = corps de texte)
    juce::Font aboutFont(float pt, int weight=0, bool italic=false)
    {
        if(!italic) return OptionsUi::font(pt, weight);
       #if JUCE_WINDOWS
        const juce::String name("Segoe UI");
       #else
        const juce::String name(juce::Font::getDefaultSansSerifFontName());
       #endif
        return juce::Font(juce::FontOptions().withName(name).withStyle(weight>0 ? "Bold Italic" : "Italic").withPointHeight(pt));
    }
    // titres, liens, tampon : accent (assombri sur le thème clair, comme la pastille α)
    juce::Colour aboutInk()  { return Neu::light ? Col::accent.darker(0.45f) : Col::accent; }
    juce::Colour aboutLine() { return Neu::light ? juce::Colours::black.withAlpha(0.10f) : juce::Colours::white.withAlpha(0.085f); }

    // réduction par paliers x2 puis ajustement exact : une seule réduction à
    // fort rapport crénelle les traits fins (signatures, monogramme)
    juce::Image smoothRescale(juce::Image img, int w, int h)
    {
        w=juce::jmax(1, w); h=juce::jmax(1, h);
        while(img.getWidth()>w*2 && img.getHeight()>h*2)
            img=img.rescaled(img.getWidth()/2, img.getHeight()/2, juce::Graphics::highResamplingQuality);
        return img.rescaled(w, h, juce::Graphics::highResamplingQuality);
    }

    // signature (encre seule, dans l'alpha) à la couleur du thème
    juce::Image inked(const juce::Image& src, juce::Colour ink)
    {
        juce::Image out(juce::Image::ARGB, src.getWidth(), src.getHeight(), true);
        const juce::Image::BitmapData s(src, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData d(out, juce::Image::BitmapData::writeOnly);
        for(int y=0;y<s.height;++y)
            for(int x=0;x<s.width;++x)
                d.setPixelColour(x, y, ink.withAlpha(s.getPixelColour(x, y).getFloatAlpha()));
        return out;
    }

    // monogramme Side Key (argenté) : assombri pour rester lisible sur le thème clair
    void darken(juce::Image& img, float k)
    {
        juce::Image::BitmapData d(img, juce::Image::BitmapData::readWrite);
        for(int y=0;y<d.height;++y)
            for(int x=0;x<d.width;++x)
                d.setPixelColour(x, y, d.getPixelColour(x, y).withMultipliedBrightness(k));
    }

    constexpr float kTeamH=250.f, kCardW=200.f;
}

//------------------------------------------------------------------------------
// Colonne : mise en page mot à mot, paragraphes justifiés (la dernière ligne
// reste à gauche), titres et puces alignés à gauche
float AboutOverlay::Column::drawItem(juce::Graphics* g, const ColumnItem& it, float y, float width) const
{
    using K=ColumnItem::Kind;
    const bool head=it.kind==K::Header, bullet=it.kind==K::Bullet;
    const auto font=head ? aboutFont(15.f, 2) : aboutFont(13.f);
    const float x0=bullet ? 12.f : 0.f, maxW=juce::jmax(10.f, width-x0);
    const float lineH=font.getHeight();

    juce::StringArray words;
    words.addTokens(it.text, " ", "");
    words.removeEmptyStrings();
    std::vector<float> wordW;
    for(const auto& w : words) wordW.push_back(juce::GlyphArrangement::getStringWidth(font, w));
    const float space=juce::GlyphArrangement::getStringWidth(font, " ");

    std::vector<std::pair<int,int>> lines;   // [début, fin) dans words
    int start=0; float cur=0.f;
    for(int i=0;i<words.size();++i){
        const float add=(i==start ? 0.f : space)+wordW[(size_t)i];
        if(i>start && cur+add>maxW){ lines.push_back({ start, i }); start=i; cur=wordW[(size_t)i]; }
        else cur+=add;
    }
    if(start<words.size()) lines.push_back({ start, words.size() });

    if(g!=nullptr){
        if(bullet){
            g->setColour(aboutInk());
            g->fillEllipse(0.f, y+lineH*0.5f-2.5f, 5.f, 5.f);
        }
        juce::GlyphArrangement ga;
        float baseline=y+font.getAscent();
        for(size_t l=0;l<lines.size();++l){
            const auto [a, b]=lines[l];
            float total=0.f;
            for(int i=a;i<b;++i) total+=wordW[(size_t)i];
            const bool justify=it.kind==K::Paragraph && l+1<lines.size() && b-a>1;
            const float gap=justify ? (maxW-total)/(float)(b-a-1) : space;
            float x=x0;
            for(int i=a;i<b;++i){
                ga.addLineOfText(font, words[i], x, baseline);
                x+=wordW[(size_t)i]+gap;
            }
            baseline+=lineH;
        }
        g->setColour(head ? aboutInk() : Neu::text);
        ga.draw(*g);
    }
    return (float)lines.size()*lineH;
}

float AboutOverlay::Column::itemHeight(const ColumnItem& it, float width) const
{
    return drawItem(nullptr, it, 0.f, width);
}

// écart sous un élément ; un titre qui suit une liste de puces s'en détache davantage
static float aboutItemGap(const AboutOverlay::ColumnItem& it, const AboutOverlay::ColumnItem* next)
{
    using K=AboutOverlay::ColumnItem::Kind;
    const float base=it.kind==K::Header ? 6.f : it.kind==K::Bullet ? 4.f : 10.f;
    return base+((it.kind==K::Bullet && next!=nullptr && next->kind==K::Header) ? 10.f : 0.f);
}

void AboutOverlay::Column::layout(int width)
{
    float y=0.f;
    for(size_t i=0;i<items.size();++i)
        y+=itemHeight(items[i], (float)width)+aboutItemGap(items[i], i+1<items.size() ? &items[i+1] : nullptr);
    setSize(width, juce::roundToInt(y)+4);
}

void AboutOverlay::Column::paint(juce::Graphics& g)
{
    float y=0.f;
    const float w=(float)getWidth();
    for(size_t i=0;i<items.size();++i){
        y+=drawItem(&g, items[i], y, w);
        y+=aboutItemGap(items[i], i+1<items.size() ? &items[i+1] : nullptr);
    }
}

//------------------------------------------------------------------------------
AboutOverlay::AboutOverlay()
{
    monogram=juce::ImageCache::getFromMemory(BinaryData::sidekey_monogram_png, BinaryData::sidekey_monogram_pngSize);
    // portraits du duo en noir et blanc (copies : desaturate() modifie l'image,
    // celles de l'ImageCache sont partagées)
    const auto blackAndWhite=[](const void* data, int size){
        auto img=juce::ImageCache::getFromMemory(data, size).createCopy();
        img.desaturate();
        return img;
    };
    portrait[0]=blackAndWhite(BinaryData::mikshake_about_crop_jpg, BinaryData::mikshake_about_crop_jpgSize);
    portrait[1]=blackAndWhite(BinaryData::sunny_crop_jpg,          BinaryData::sunny_crop_jpgSize);
    signature[0]=juce::ImageCache::getFromMemory(BinaryData::signature_mikshake_png, BinaryData::signature_mikshake_pngSize);
    signature[1]=juce::ImageCache::getFromMemory(BinaryData::signature_sunny_png,    BinaryData::signature_sunny_pngSize);

    using K=ColumnItem::Kind;
    const auto H=[](const char* s){ return ColumnItem{ utf8(s), K::Header }; };
    const auto P=[](const char* s){ return ColumnItem{ utf8(s), K::Paragraph }; };
    const auto B=[](const char* s){ return ColumnItem{ utf8(s), K::Bullet }; };

    column[0].setItems({
        H("MERLIN \xce\xb1: Every Measurement, One Window"),
        P("I built MERLIN \xce\xb1 out of a simple frustration. Every session, I ended up juggling several metering plugins "
          "scattered across the mix, each in its own little window, each with its own idea of where it should live on the screen."),
        P("What I wanted was one window that stays open next to the DAW, listens to everything that reaches the speakers, "
          "and shows every measurement I care about, arranged exactly the way I work."),
        P("So MERLIN \xce\xb1 runs as a standalone application as well as a plugin. It listens to the loopback of your audio "
          "interface or, on Windows, directly to the system output: no extra hardware, no routing gymnastics."),
        P("Five slots and one master meter. Any module goes into any slot with a right click, and any module can leave the "
          "main window to live in its own, on another screen if you like."),
        P("It stays on top when you want it to, and it can leave the keyboard to your DAW, so your shortcuts keep working "
          "while MERLIN \xce\xb1 keeps watching."),
        P("Why Merlin? Because a good analyzer should feel a little like magic, and then turn out to be nothing but careful math."),
    });

    column[1].setItems({
        H("Measurements, Not Guesses"),
        P("MERLIN \xce\xb1 is a measuring instrument first. No probabilities, no scores, no estimates dressed up as readings: "
          "every value on screen is computed from the signal itself."),
        P("Loudness follows ITU-R BS.1770-4 and EBU R128: Momentary, Short Term, gated Integrated loudness, and Loudness "
          "Range as defined by EBU Tech 3342."),
        P("True Peak uses the 4x oversampling filter from annex 2 of BS.1770-4, so the inter-sample peaks that a sample "
          "meter misses are caught."),
        P("The spectral density runs on a 4096 point FFT with 75% overlap and Welch style averaging. The band spectrum "
          "offers 15, 31 or 62 bands, extends above 20 kHz up to half the sample rate, and analyzes Stereo, Mid/Side or Side."),
        P("The oscilloscope locks onto the fundamental and, at its default 25 ms window, draws every single sample."),
        P("The 3D spectrograms, phosphor vectorscopes and particle spectrums are rendered by the graphics card in OpenGL, "
          "leaving the processor to your session."),
        P("In the plugin, MERLIN \xce\xb1 only listens: the audio passes through untouched."),
    });

    column[2].setItems({
        H("Features"),
        B("21 Modules in 4 Families: Spectrum, Spectrogram, Stereo, Level and Time"),
        B("5 Freely Assignable Slots plus a Fixed Master VU"),
        B("Standalone App for Windows and macOS, VST3, and AU on macOS"),
        B("Audio Interface Loopback Capture, plus Direct System Output Capture on Windows"),
        B("EBU R128 / ITU-R BS.1770-4 Loudness: Momentary, Short Term, Integrated, LRA"),
        B("True Peak with 4x Oversampling (BS.1770-4 Annex 2)"),
        B("15 / 31 / 62 Band Spectrum, Stereo, Mid/Side or Side Analysis"),
        B("4096 Point Spectral Density with Welch Averaging"),
        B("3D Spectrograms: Lines, Surface, Terrain, Neon, Crystal"),
        B("Vectorscopes: Classic, Phosphor, Density, Multiband"),
        B("Multiband Correlator, Panorama and Dot Panorama"),
        B("Triggered Oscilloscope with Adjustable Trace Width"),
        B("GPU Modules Rendered in OpenGL, with Anti-Aliasing and V-Sync Options"),
        B("Detachable Modules: Any Module in its Own Window"),
        B("Always on Top, Keyboard Left to Your DAW"),
        B("Live Sample Rate Display"),
        B("Light and Dark Themes, Accent Palettes"),
        B("10 Interface Languages"),
        B("One License for 5 Computers, 15 Day Free Trial"),
        H("The Side Key System Philosophy"),
        P("At Side Key System, calibration is not a final adjustment. It is part of the engineering process itself."),
        P("Every tool is carefully designed, measured, tested and calibrated with meticulous attention to precision, "
          "stability and measurement consistency. Every reading is meant to be trusted, whether working on a single "
          "track or a full mastering session."),
        P("The objective is simple: give engineers instruments they can rely on, where every reading has a purpose and "
          "every decision serves the music."),
    });

    for(int i=0;i<3;++i){
        viewport[i].setScrollBarsShown(true, false);
        viewport[i].setScrollBarThickness(8);
        viewport[i].setViewedComponent(&column[i], false);
        column[i].onAnyClick=[this]{ hideAnimated(); };
        addAndMakeVisible(viewport[i]);
    }
    setWantsKeyboardFocus(true);   // Échap ferme la page
}

juce::Rectangle<float> AboutOverlay::panelBounds() const
{
    const auto b=getLocalBounds().toFloat();
    return b.reduced(juce::jmax(20.f, b.getWidth()*0.015f), juce::jmax(12.f, b.getHeight()*0.04f));
}

// Une seule mise en page pour le décor (cache) et les colonnes (Viewport)
AboutOverlay::Layout AboutOverlay::computeLayout() const
{
    Layout L;
    auto c=panelBounds().reduced(20.f, 12.f);
    L.lineX0=c.getX(); L.lineX1=c.getRight();

    L.header=c.removeFromTop(30.f);
    c.removeFromTop(8.f);
    L.headerLineY=c.getY();

    L.footer=c.removeFromBottom(54.f);
    c.removeFromBottom(10.f);
    L.footerLineY=c.getBottom();
    c.removeFromBottom(10.f);

    // bandeau du duo : 250 px, réduit sur une petite fenêtre pour laisser lire les colonnes
    c.removeFromTop(14.f);
    L.team=c.removeFromTop(juce::jlimit(120.f, kTeamH, c.getHeight()*0.42f));
    c.removeFromTop(14.f);
    L.teamLineY=c.getY();
    c.removeFromTop(14.f);

    const float gap=18.f, colW=(c.getWidth()-2.f*gap)/3.f;
    L.colTop=c.getY(); L.colBottom=c.getBottom();
    const auto a=c.removeFromLeft(colW); c.removeFromLeft(gap);
    const auto b=c.removeFromLeft(colW); c.removeFromLeft(gap);
    L.colA=a.toNearestInt(); L.colB=b.toNearestInt(); L.colC=c.toNearestInt();
    L.dividerX1=a.getRight()+gap*0.5f;
    L.dividerX2=b.getRight()+gap*0.5f;
    return L;
}

void AboutOverlay::resized()
{
    const auto L=computeLayout();
    const juce::Rectangle<int> cols[3]={ L.colA, L.colB, L.colC };
    for(int i=0;i<3;++i){
        viewport[i].setBounds(cols[i]);
        // largeur utile = colonne moins la barre de défilement (pas de double retour à la ligne)
        column[i].layout(juce::jmax(10, cols[i].getWidth()-viewport[i].getScrollBarThickness()-6));
    }
    chromeCache={};
}

void AboutOverlay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.60f*animOpacity));
    if(!chromeCache.isValid()) rebuildChromeCache();
    if(!chromeCache.isValid()) return;
    juce::Graphics::ScopedSaveState state(g);
    g.setOpacity(animOpacity);
    g.addTransform(panelTransform());
    g.drawImageTransformed(chromeCache, juce::AffineTransform::scale(1.f/chromeScale));
}

// Décor statique (feuille, en-tête, duo, filets, pied de page) rendu une fois
// à la résolution de l'écran ; paint() ne fait que le recopier (défilement
// des colonnes et animation sans redessiner les ombres et les photos)
void AboutOverlay::rebuildChromeCache()
{
    const int w=getWidth(), h=getHeight();
    if(w<4 || h<4) return;
    double scale=juce::Component::getApproximateScaleFactorForComponent(this);
    if(auto* d=juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds())) scale*=d->scale;
    chromeScale=(float)juce::jlimit(1.0, 4.0, scale);
    chromeCache=juce::Image(juce::Image::ARGB, juce::roundToInt((float)w*chromeScale), juce::roundToInt((float)h*chromeScale), true);
    juce::Graphics g(chromeCache);
    g.addTransform(juce::AffineTransform::scale(chromeScale));
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);

    // feuille en relief, comme la plaque
    juce::Path sheet;
    sheet.addRoundedRectangle(panelBounds(), 14.f);
    juce::DropShadow(Neu::shadowDark,  30, {  10,  10 }).drawForPath(g, sheet);
    juce::DropShadow(Neu::shadowLight, 20, {  -6,  -6 }).drawForPath(g, sheet);
    g.setColour(Neu::surface);
    g.fillPath(sheet);
    g.setColour(aboutLine());
    g.strokePath(sheet, juce::PathStrokeType(1.f));

    const auto L=computeLayout();
    drawHeader(g, L.header);
    drawTeam(g, L.team, chromeScale);
    drawFooter(g, L.footer, chromeScale);

    g.setColour(aboutLine());
    for(const float y : { L.headerLineY, L.teamLineY, L.footerLineY })
        g.fillRect(L.lineX0, y, L.lineX1-L.lineX0, 1.f);
    g.fillRect(L.dividerX1-0.5f, L.colTop, 1.f, L.colBottom-L.colTop);
    g.fillRect(L.dividerX2-0.5f, L.colTop, 1.f, L.colBottom-L.colTop);
}

// En-tête : monogramme, ABOUT + logotype MERLIN (Audiowide) + pastille α,
// accroche, version à droite
void AboutOverlay::drawHeader(juce::Graphics& g, juce::Rectangle<float> row) const
{
    const auto ink=aboutInk();

    g.setColour(Neu::textDim);
    g.setFont(aboutFont(11.f, 2).withExtraKerningFactor(0.08f));
    g.drawText("v"+juce::String(ProjectInfo::versionString), row.removeFromRight(70.f), juce::Justification::centredRight, false);

    if(monogram.isValid()){
        const float mh=28.f, mw=mh*(float)monogram.getWidth()/(float)monogram.getHeight();
        auto img=smoothRescale(monogram, juce::roundToInt(mw*chromeScale), juce::roundToInt(mh*chromeScale));
        if(Neu::light) darken(img, 0.3f);
        g.drawImage(img, juce::Rectangle<float>(row.getX(), row.getCentreY()-mh*0.5f, mw, mh));
        row.removeFromLeft(mw+10.f);
    }

    const float cap=16.f, base=row.getCentreY()+cap*0.5f;   // ligne de base commune
    g.setColour(ink);
    juce::GlyphArrangement about;
    about.addLineOfText(aboutFont(22.f, 2).withExtraKerningFactor(0.11f), "ABOUT", row.getX(), base);
    about.draw(g);
    float x=about.getBoundingBox(0, -1, false).getRight()+11.f;

    juce::Font wf(juce::FontOptions(audiowideTypeface()).withHeight(100.f));
    {
        juce::GlyphArrangement m; m.addLineOfText(wf, "M", 0.f, 0.f);
        wf=wf.withHeight(100.f*cap/juce::jmax(1.f, -m.getBoundingBox(0, -1, false).getY())).withExtraKerningFactor(0.08f);
    }
    juce::GlyphArrangement word;
    word.addLineOfText(wf, "MERLIN", x, base);
    word.draw(g);
    x=word.getBoundingBox(0, -1, false).getRight()+8.f;
    drawAlphaBadge(g, { x+13.f, row.getCentreY() }, 13.f);
    x+=26.f+14.f;

    g.setColour(ink.withAlpha(0.85f));
    g.setFont(aboutFont(14.f, 0, true).withExtraKerningFactor(0.10f));
    g.drawText("Every frequency, revealed.", juce::Rectangle<float>(x, row.getY(), juce::jmax(0.f, row.getRight()-x-8.f), row.getHeight()),
               juce::Justification::centredLeft, true);
}

// Bandeau du duo : deux portraits (légende nom + rôle sur un dégradé sombre),
// puis la présentation, centrée verticalement
void AboutOverlay::drawTeam(juce::Graphics& g, juce::Rectangle<float> row, float scale) const
{
    const float cardW=row.getHeight()*kCardW/kTeamH;
    static const char* const names[]={ "MIKSHAKE", "SUNNY" };
    static const char* const roles[]={ "DEVELOPMENT", "DESIGN" };
    for(int i=0;i<2;++i){
        const auto card=row.removeFromLeft(cardW);
        row.removeFromLeft(16.f);
        {
            juce::Graphics::ScopedSaveState state(g);
            juce::Path clip; clip.addRoundedRectangle(card, 10.f);
            g.reduceClipRegion(clip);
            g.setColour(juce::Colour(0xff111111));
            g.fillRect(card);
            if(const auto& img=portrait[i]; img.isValid()){
                // remplissage "cover", cadré un peu au-dessus du centre (visages)
                const float s=juce::jmax(card.getWidth()/(float)img.getWidth(), card.getHeight()/(float)img.getHeight());
                const float w=(float)img.getWidth()*s, h=(float)img.getHeight()*s;
                g.drawImage(smoothRescale(img, juce::roundToInt(w*scale), juce::roundToInt(h*scale)),
                            { card.getX()+(card.getWidth()-w)*0.5f, card.getY()+(card.getHeight()-h)*0.3f, w, h });
            }
            // légende à l'échelle de la carte (plus petite sur une fenêtre basse),
            // le nom rétrécissant au besoin pour tenir en entier
            const float k=juce::jlimit(0.6f, 1.f, card.getHeight()/kTeamH);
            const auto cap=card.withTrimmedTop(card.getHeight()-72.f*k);
            g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.f), 0.f, cap.getY(),
                                                   juce::Colours::black.withAlpha(0.72f), 0.f, cap.getBottom(), false));
            g.fillRect(cap);
            auto text=card.reduced(14.f*k, 12.f*k);
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.setFont(Ui::fitFont(aboutFont(12.f*k).withExtraKerningFactor(0.12f), roles[i], text.getWidth()));
            g.drawText(roles[i], text.removeFromBottom(15.f*k), juce::Justification::bottomLeft, false);
            g.setColour(juce::Colours::white);
            g.setFont(Ui::fitFont(aboutFont(19.f*k, 2).withExtraKerningFactor(0.14f), names[i], text.getWidth()));
            g.drawText(names[i], text.removeFromBottom(24.f*k), juce::Justification::bottomLeft, false);
        }
        g.setColour(aboutLine());
        g.drawRoundedRectangle(card, 10.f, 1.f);
    }

    // présentation : titre, une phrase, puis la devise (empattements, italique)
    auto intro=row.withTrimmedLeft(10.f).withTrimmedRight(10.f);
    intro.setWidth(juce::jmin(intro.getWidth(), 620.f));
    struct Part { juce::String text; juce::Font font; juce::Colour colour; float gapAfter; };
    const Part parts[]={
        { "Two people, one window", aboutFont(15.f, 2), aboutInk(), 6.f },
        { utf8("MERLIN \xce\xb1 is made by two people: Mikshake writes the code, Sunny designs everything you see."),
          aboutFont(13.f), Neu::text, 10.f },
        { "One window to rule them all, and in the mix reveal them.",
          juce::Font(juce::FontOptions().withName("Georgia").withStyle("Italic").withPointHeight(16.f)).withExtraKerningFactor(0.025f),
          aboutInk(), 0.f },
    };
    juce::GlyphArrangement ga[3];
    float heights[3], total=0.f;
    for(int i=0;i<3;++i){
        ga[i].addJustifiedText(parts[i].font, parts[i].text, 0.f, parts[i].font.getAscent(), intro.getWidth(), juce::Justification::left);
        heights[i]=ga[i].getNumGlyphs()>0 ? ga[i].getBoundingBox(0, -1, true).getBottom() : 0.f;
        total+=heights[i]+parts[i].gapAfter;
    }
    float y=intro.getCentreY()-total*0.5f;
    for(int i=0;i<3;++i){
        g.setColour(parts[i].colour);
        ga[i].draw(g, juce::AffineTransform::translation(intro.getX(), y));
        y+=heights[i]+parts[i].gapAfter;
    }
}

// Pied de page : les deux signatures, liens (site | licence) et copyright, tampon à droite
void AboutOverlay::drawFooter(juce::Graphics& g, juce::Rectangle<float> area, float scale)
{
    const auto ink=Neu::light ? juce::Colours::black : juce::Colours::white;
    const float sigH[2]={ 50.f, 40.f };   // Sunny Jim, plus large : même poids visuel
    for(int i=0;i<2;++i){
        if(!signature[i].isValid()) continue;
        const float h=sigH[i], w=h*(float)signature[i].getWidth()/(float)signature[i].getHeight();
        g.drawImage(inked(smoothRescale(signature[i], juce::roundToInt(w*scale), juce::roundToInt(h*scale)), ink),
                    { area.getX(), area.getCentreY()-h*0.5f, w, h });
        area.removeFromLeft(w+(i==0 ? 10.f : 16.f));
    }

    drawStamp(g, area.removeFromRight(58.f));
    area.removeFromRight(14.f);

    // liens, puis copyright, centrés ensemble verticalement
    const float top=area.getCentreY()-(16.f+4.f+13.f)*0.5f;
    const juce::Rectangle<float> linkRow(area.getX(), top, area.getWidth(), 16.f);
    const juce::Rectangle<float> copyRow(area.getX(), top+20.f, area.getWidth(), 13.f);

    const auto linkFont=aboutFont(13.f, 2).withExtraKerningFactor(0.04f);
    g.setFont(linkFont);
    const juce::String site("sidekeysystem.com");
    const float siteW=juce::GlyphArrangement::getStringWidth(linkFont, site);
    websiteBounds=linkRow.withWidth(siteW);
    g.setColour(aboutInk());
    g.drawText(site, websiteBounds, juce::Justification::centredLeft, false);
    g.fillRect(websiteBounds.getX(), websiteBounds.getBottom()-2.f, siteW, 1.f);

    const float sepW=18.f;
    g.setColour(Neu::textDim.withAlpha(0.6f));
    g.drawText("|", juce::Rectangle<float>(websiteBounds.getRight(), linkRow.getY(), sepW, linkRow.getHeight()), juce::Justification::centred, false);

    // état de la licence (clé active ou jours de démo) : ouvre l'écran de licence
    const auto lic=licenceText();
    licenceBounds=linkRow.withX(websiteBounds.getRight()+sepW).withWidth(juce::GlyphArrangement::getStringWidth(linkFont, lic));
    g.setColour(aboutInk());
    g.drawText(lic, licenceBounds, juce::Justification::centredLeft, false);

    g.setColour(Neu::textDim.withAlpha(0.8f));
    g.setFont(aboutFont(9.5f).withExtraKerningFactor(0.04f));
    g.drawText(utf8("\xc2\xa9 2026 Side Key System. All rights reserved.   \xe2\x80\xa2   Made in France"),
               copyRow, juce::Justification::centredLeft, true);
}

juce::String AboutOverlay::licenceText() const
{
    if(license==nullptr) return "License";
    if(const auto key=license->getLicenseKey(); key.isNotEmpty()) return key;
    if(license->getStatus()==LicenseManager::Status::trial)
        return "Demo - "+juce::String(license->getTrialDaysLeft())+" day(s) left";
    return "License";
}

// Tampon "Calibrated" : double anneau tourné, texte, grain d'encre déterministe
void AboutOverlay::drawStamp(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto centre=area.getCentre();
    const float r=juce::jmin(area.getWidth(), area.getHeight())*0.5f;
    const auto ink=aboutInk().withAlpha(0.82f);

    juce::Graphics::ScopedSaveState state(g);
    g.addTransform(juce::AffineTransform::rotation(juce::degreesToRadians(-8.f), centre.x, centre.y));

    g.setColour(ink);
    g.drawEllipse(centre.x-r, centre.y-r, r*2.f, r*2.f, 1.8f);
    g.drawEllipse(centre.x-r*0.82f, centre.y-r*0.82f, r*1.64f, r*1.64f, 0.9f);

    g.setFont(juce::Font(juce::FontOptions(r*0.30f, juce::Font::bold)).withExtraKerningFactor(0.08f));
    g.drawFittedText("CALIBRATED", juce::Rectangle<float>(centre.x-r*0.78f, centre.y-r*0.52f, r*1.56f, r*0.4f).toNearestInt(),
                     juce::Justification::centred, 1);
    g.setFont(juce::Font(juce::FontOptions(r*0.20f, juce::Font::bold)).withExtraKerningFactor(0.04f));
    g.drawFittedText("SIDE KEY", juce::Rectangle<float>(centre.x-r*0.7f, centre.y+r*0.06f, r*1.4f, r*0.32f).toNearestInt(),
                     juce::Justification::centred, 1);
    g.drawFittedText("SYSTEM", juce::Rectangle<float>(centre.x-r*0.7f, centre.y+r*0.36f, r*1.4f, r*0.3f).toNearestInt(),
                     juce::Justification::centred, 1);

    for(int i=0;i<46;++i){
        const float ang=(float)i*0.151f;
        const float rr=r*(0.32f+0.66f*std::abs(std::sin((float)i*2.3f)));
        g.setColour(ink.withAlpha(0.16f*std::abs(std::sin((float)i*1.7f))));
        g.fillEllipse(centre.x+std::cos(ang)*rr-0.6f, centre.y+std::sin(ang)*rr-0.6f, 1.2f, 1.2f);
    }
}

//------------------------------------------------------------------------------
void AboutOverlay::mouseDown(const juce::MouseEvent& e)
{
    if(animating && !animatingIn) return;   // déjà en train de se fermer
    if(websiteBounds.contains(e.position)){ juce::URL("https://sidekeysystem.com/").launchInDefaultBrowser(); return; }
    if(licenceBounds.contains(e.position)){ if(onOpenLicence) onOpenLicence(); return; }
    hideAnimated();
}

void AboutOverlay::mouseMove(const juce::MouseEvent& e)
{
    const bool overLink=websiteBounds.contains(e.position) || licenceBounds.contains(e.position);
    setMouseCursor(overLink ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

bool AboutOverlay::keyPressed(const juce::KeyPress& key)
{
    if(key!=juce::KeyPress::escapeKey) return false;
    hideAnimated();
    return true;
}

void AboutOverlay::visibilityChanged()
{
    if(isVisible()){ grabKeyboardFocus(); return; }
    // fermée (fin d'animation, ou cachée d'un coup par l'éditeur) : état de repos
    stopTimer();
    animating=false;
    animOpacity=animScale=1.f;
    for(auto& v : viewport){ v.setTransform({}); v.setAlpha(1.f); }
}

// Apparition : voile fondu, panneau qui grandit de 92 % à 100 % en montant de
// 16 px avec un léger rebond (280 ms) ; disparition inverse (170 ms)
void AboutOverlay::showAnimated()
{
    chromeCache={};   // thème, accent et licence relus
    for(int i=0;i<3;++i){
        viewport[i].getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, Neu::text.withAlpha(0.28f));
        viewport[i].setViewPosition(0, 0);
        column[i].repaint();
    }
    animOpacity=0.f; animScale=0.f;
    for(auto& v : viewport){ v.setTransform(panelTransform()); v.setAlpha(0.f); }
    setVisible(true);
    startAnimation(true);
}

void AboutOverlay::hideAnimated()
{
    if(isVisible() && !(animating && !animatingIn))
        startAnimation(false);
}

juce::AffineTransform AboutOverlay::panelTransform() const
{
    const auto c=panelBounds().getCentre();
    const float s=0.92f+0.08f*animScale;
    return juce::AffineTransform::scale(s, s, c.x, c.y).translated(0.f, (1.f-animScale)*16.f);
}

void AboutOverlay::startAnimation(bool in)
{
    animatingIn=in;
    animating=true;
    animStartMs=juce::Time::getMillisecondCounterHiRes();
    animFrom=animOpacity;
    startTimerHz(60);
    timerCallback();
}

void AboutOverlay::timerCallback()
{
    const double dur=animatingIn ? 280.0 : 170.0;
    const float t=(float)juce::jlimit(0.0, 1.0, (juce::Time::getMillisecondCounterHiRes()-animStartMs)/dur);
    if(animatingIn){
        constexpr float c1=1.2f, c3=c1+1.f;   // rebond discret
        animScale=1.f+c3*std::pow(t-1.f, 3.f)+c1*std::pow(t-1.f, 2.f);
        animOpacity=animFrom+(1.f-animFrom)*(1.f-(1.f-t)*(1.f-t));
    } else {
        const float e=t*t;   // accélère vers la sortie
        animOpacity=animFrom*(1.f-e);
        animScale=1.f-e;
    }
    const auto tr=panelTransform();
    for(auto& v : viewport){ v.setTransform(tr); v.setAlpha(animOpacity); }
    repaint();

    if(t<1.f) return;
    stopTimer();
    animating=false;
    if(animatingIn){
        animOpacity=animScale=1.f;
        for(auto& v : viewport){ v.setTransform({}); v.setAlpha(1.f); }
    } else {
        setVisible(false);   // visibilityChanged() remet l'état de repos
    }
}

//==============================================================================
// MultibandCorrDisplay
// Barres de corrélation L/R par bande fréquentielle
// Cyan = en phase (+1)  ·  Rouge = en opposition (-1)
//==============================================================================
MultibandCorrDisplay::MultibandCorrDisplay(LeakAudioProcessor& p) : processor(p)
{
    std::fill(std::begin(rawCorr),std::end(rawCorr),0.f);
    std::fill(std::begin(corr),   std::end(corr),   0.f);
    std::fill(std::begin(prevCorr),std::end(prevCorr),0.f);
    addAndMakeVisible(relFader);
    relFader.setValue(processor.corrReleaseSec,juce::dontSendNotification);
    setReleaseSec(processor.corrReleaseSec);
    relFader.onValueChange=[this](float v){ processor.corrReleaseSec=v; setReleaseSec(v); };
    startTimerHz(60);  // fluide
}
void MultibandCorrDisplay::resized()
{
    const float padT=22.f, padB=8.f;
    relFader.setBounds(2,(int)padT,(int)kReleaseFaderW-4,juce::jmax(10,(int)((float)getHeight()-padT-padB)));
}
void MultibandCorrDisplay::setReleaseSec(float s)
{
    releaseSec=juce::jmax(0.02f,s);
}
void MultibandCorrDisplay::timerCallback()
{
    processor.getBandCorr(rawCorr, NMAX);
    const float dt=1.f/(float)currentHz;                 // cadence réelle du timer
    const float k=1.f-std::exp(-dt/releaseSec);
    const int N=processor.getNumActiveBands();
    bool changed=false;
    for(int b=0;b<N;++b){
        const float target=rawCorr[b];
        corr[b]+=(target-corr[b])*k;
        // Comparé à la dernière valeur repeinte (pas à la frame précédente) :
        // à un RELEASE long, le pas par frame est minuscule mais s'accumule
        // jusqu'à dépasser le seuil — sans ça l'affichage se figeait tant que
        // le signal n'avait pas fini de converger (jamais, en pratique).
        if(std::abs(corr[b]-prevCorr[b])>0.004f){changed=true;prevCorr[b]=corr[b];}
    }
    if(changed) repaint();  // dirty-check : pas de repaint si déjà stabilisé (silence/signal stable)
}
int MultibandCorrDisplay::xToBand(float px) const
{
    const int W=getWidth();
    const float padL=kReleaseFaderW+juce::jlimit(18.f,32.f,W*.042f)*Ui::textScale, padR=4.f;
    const float gw=(float)W-padL-padR;
    return BandAxis(processor.getBandFreqs(), processor.getNumActiveBands(), padL, gw).bandAt(px);
}
void MultibandCorrDisplay::mouseDown(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown()) return;  // clic droit = menu slot
    const int band=xToBand((float)e.x);
    if(e.mods.isCtrlDown()||e.mods.isCommandDown()){
        // Ctrl+clic : bascule la bande dans la sélection multi-bandes persistante
        if(band>=0){
            multiSelectMask ^= LeakAudioProcessor::bandBit(band);
            lastDragBand=band;
        }
    } else {
        // Clic simple : bande isolée tant que le bouton est enfoncé, efface la sélection multi
        multiSelectMask=0;
        soloBand=band;
    }
    if(onSelectionChanged) onSelectionChanged();
    repaint();
}
void MultibandCorrDisplay::mouseUp(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown()) return;
    lastDragBand=-1;
    if(soloBand!=-1){
        soloBand=-1;
        // Relâcher le clic mono-bande retombe sur la sélection multi-bandes (vide si aucune)
        repaint();
    }
}
void MultibandCorrDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown()) return;
    if(!(e.mods.isCtrlDown()||e.mods.isCommandDown())) return;
    const int band=xToBand((float)e.x);
    if(band>=0 && band!=lastDragBand){
        multiSelectMask |= LeakAudioProcessor::bandBit(band);
        lastDragBand=band;
        repaint();
    }
}
void MultibandCorrDisplay::mouseMove(const juce::MouseEvent& e)
{
    hoverPixX=(float)e.x;
    const int nb=xToBand(hoverPixX);
    if(nb!=hoveredBand){hoveredBand=nb;repaint();}
}
void MultibandCorrDisplay::mouseExit(const juce::MouseEvent&)
{
    bool needsRepaint=false;
    if(soloBand!=-1){soloBand=-1;needsRepaint=true;}
    if(hoveredBand!=-1){hoveredBand=-1;needsRepaint=true;}
    if(needsRepaint) repaint();
}
void MultibandCorrDisplay::paint(juce::Graphics& g)
{
    const int W=getWidth(), H=getHeight();
    if(W<10||H<10) return;

    g.setColour(Col::bg); g.fillRect(getLocalBounds());

    const float padL=kReleaseFaderW+juce::jlimit(18.f,32.f,W*.042f)*Ui::textScale;
    const float padR=4.f, padT=22.f, padB=8.f;
    const float gw=(float)W-padL-padR;
    const float gh=(float)H-padT-padB;
    const float cy=padT+gh*0.5f;     // y de la ligne corr=0
    const BandAxis axis(processor.getBandFreqs(), processor.getNumActiveBands(), padL, gw);
    const float ghalf=gh*0.5f;

    // ── Grille horizontale ────────────────────────────────────────────────
    auto hline=[&](float corrVal, bool center){
        const float y=cy-corrVal*ghalf;
        g.setColour(center?Col::gridCtr.withAlpha(0.9f):Col::grid.withAlpha(0.45f));
        g.drawLine(padL,y,padL+gw,y,center?0.8f:0.5f);
    };
    hline( 0.f,true);
    hline( 0.5f,false); hline(-0.5f,false);
    hline( 1.f,false);  hline(-1.f,false);

    // Labels corrélation (gauche)
    g.setFont(Ui::iceland((7.f)));
    for(auto [cv,lbl] : std::initializer_list<std::pair<float,const char*>>{
            {1.f,"+1"},{0.5f,"+.5"},{0.f,"0"},{-0.5f,"-.5"},{-1.f,"-1"}})
    {
        g.setColour(Col::textMid.withAlpha(cv==0.f?0.7f:0.45f));
        // (« +1 » un peu plus bas : la ligne du titre passe juste au-dessus)
        g.drawText(lbl, 0,(int)(cy-cv*ghalf)-4+(cv==1.f ? 3 : 0),(int)padL-2,9,juce::Justification::centredRight);
    }

    // ── Grille verticale fréquence ───────────────────────────────────────
    auto freqToX=[&](float f)->float{ return axis.freqToX(f); };
    // Police des chiffres de fréquence — gardée pour le calcul de la grille verticale
    // (les nombres ne sont plus affichés en permanence : ils apparaissent au survol)
    for(auto [f,lbl] : std::initializer_list<std::pair<float,const char*>>{
            {50.f,"50"},{100.f,"100"},{200.f,"200"},{500.f,"500"},
            {1000.f,"1k"},{2000.f,"2k"},{5000.f,"5k"},{10000.f,"10k"}})
    {
        (void)lbl;
        const float fx=freqToX(f);
        g.setColour(Col::grid.withAlpha(0.28f));
        g.drawLine(fx,padT,fx,padT+gh,0.5f);
    }

    // ── Barres de corrélation ─────────────────────────────────────────────
    const int N=processor.getNumActiveBands();

    // Bornes par bande (réutilisées pour le rendu et le surlignage solo) :
    // colonnes égales, bandes extrêmes entières
    float bandX0[NMAX] {}, bandBW[NMAX] {};
    for(int b=0;b<N;++b){ bandX0[b]=axis.colX(b); bandBW[b]=axis.colW(b); }

    // Surlignage persistant des bandes multi-sélectionnées (Ctrl+clic/glisser)
    if(multiSelectMask!=0ULL){
        for(int b=0;b<N;++b){
            if(multiSelectMask & LeakAudioProcessor::bandBit(b)){
                g.setColour(Col::accent.withAlpha(0.16f));
                g.fillRect(bandX0[b]-2.f, padT, bandBW[b]+4.f, gh);
                g.setColour(Col::accent.withAlpha(0.5f));
                g.drawRect(bandX0[b]-1.f, padT, bandBW[b]+2.f, gh, 1.0f);
            }
        }
    }

    // Surlignage de la bande soloée — colonne complète, sous les barres
    if(soloBand>=0 && soloBand<N){
        g.setColour(Col::accent.withAlpha(0.10f));
        g.fillRect(bandX0[soloBand]-2.f, padT, bandBW[soloBand]+4.f, gh);
    }

    for(int b=0;b<N;++b){
        const float c=juce::jlimit(-1.f,1.f,corr[b]);
        const float barH=std::abs(c)*ghalf;
        if(barH<0.5f) continue;
        const float x0=bandX0[b], bw=bandBW[b];

        if(c >= 0.f){
            // Corrélation positive : cyan → blanc selon intensité
            const juce::Colour col=Col::hot(Col::accent,c*0.35f);
            g.setColour(col.withAlpha(0.78f));
            g.fillRect(x0+0.5f, cy-barH, bw, barH);
            g.setColour(col.brighter(0.2f));
            g.fillRect(x0+0.5f, cy-barH, bw, 1.5f);   // cap haut
        } else {
            // Corrélation négative : rouge-orange selon intensité
            const float nc=-c;
            const juce::Colour col=Col::ink(juce::Colour(0xFFFF2200)).interpolatedWith(Col::ink(juce::Colour(0xFFFF7700)),nc*0.5f);
            g.setColour(col.withAlpha(0.78f));
            g.fillRect(x0+0.5f, cy, bw, barH);
            g.setColour(col.brighter(0.2f));
            g.fillRect(x0+0.5f, cy+barH-1.5f, bw, 1.5f);  // cap bas
        }
    }

    // Cadre de la bande soloée (par-dessus les barres)
    if(soloBand>=0 && soloBand<N){
        g.setColour(Col::accent.withAlpha(0.65f));
        g.drawRect(bandX0[soloBand]-1.f, padT, bandBW[soloBand]+2.f, gh, 1.2f);
    }

    // Axes
    g.setColour(Col::gridCtr.withAlpha(0.5f));
    g.drawLine(padL, padT, padL, padT+gh, 0.7f);
    g.drawLine(padL, padT+gh, padL+gw, padT+gh, 0.7f);

    // ── Tooltip de survol : fréquence uniquement ───────────────────────────
    if(hoveredBand>=0 && hoveredBand<N){
        const float fx=bandX0[hoveredBand]+bandBW[hoveredBand]*0.5f;   // centre de la colonne
        g.setColour(Col::accent.withAlpha(0.16f));
        g.drawLine(fx,padT,fx,padT+gh,1.f);

        const float f=processor.getBandFreqs()[hoveredBand];
        const juce::String freqStr=(f>=1000.f)
            ?(juce::String(f/1000.f,(std::fmod(f,1000.f)==0.f)?0:2)+" kHz")
            :(juce::String((int)f)+" Hz");
        const float tfs=juce::jlimit(8.f,11.f,gh*.06f);
        juce::Font tipFont=Ui::iceland(tfs);
        juce::GlyphArrangement ga; ga.addLineOfText(tipFont,freqStr,0.f,0.f);
        const float tipW=ga.getNumGlyphs()>0?ga.getBoundingBox(0,ga.getNumGlyphs(),true).getWidth()+12.f:60.f;
        const float th=tfs+8.f;
        float tx=hoverPixX+8.f;
        if(tx+tipW>(float)W-4.f) tx=hoverPixX-tipW-8.f;
        const float ty=padT+4.f;
        g.setColour(Col::panel); g.fillRoundedRectangle(tx,ty,tipW,th,3.f);
        g.setColour(Col::border); g.drawRoundedRectangle(tx,ty,tipW,th,3.f,0.7f);
        g.setFont(tipFont); g.setColour(Col::accent);
        g.drawText(freqStr,(int)(tx+6.f),(int)ty,(int)(tipW-7.f),(int)th,juce::Justification::centredLeft);
    }

    Ui::drawTitleDecor(g, (float)W);
    g.setFont(Ui::iceland((10.5f)).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    g.drawFittedText(TR("MULTIBAND CORRELATOR"), 6,5,W-12,13,juce::Justification::left,1,0.7f);
    g.setColour(Col::accent.withAlpha(0.4f));
    g.drawText("+",(int)(W-14),4,10,9,juce::Justification::centred);
    g.setColour(Col::ink(juce::Colour(0xFFFF3300)).withAlpha(0.4f));
    g.drawText("-",(int)(W-14),(int)(H-14),10,9,juce::Justification::centred);
}

//==============================================================================
// MultipanoramaDisplay
// Nuage de points : X = fréquence (log), Y = position stéréo (L haut → R bas)
// Trail phosphore — décroissance pilotée par le RELEASE général
//==============================================================================
MultipanoramaDisplay::MultipanoramaDisplay(LeakAudioProcessor& p) : processor(p)
{
    std::fill(std::begin(pan),   std::end(pan),   0.f);
    std::fill(std::begin(energy),std::end(energy),-120.f);
    std::fill(std::begin(corr),  std::end(corr),  1.f);
    addAndMakeVisible(relFader);
    relFader.setValue(processor.panoReleaseSec,juce::dontSendNotification);
    setReleaseSec(processor.panoReleaseSec);
    relFader.onValueChange=[this](float v){ processor.panoReleaseSec=v; setReleaseSec(v); };
    startTimerHz(60);  // fluide
}
void MultipanoramaDisplay::resized()
{
    trailValid=false;
    const float padT=22.f, padB=6.f;
    relFader.setBounds(2,(int)padT,(int)kReleaseFaderW-4,juce::jmax(10,(int)((float)getHeight()-padT-padB)));
}
void MultipanoramaDisplay::setReleaseSec(float s)
{
    releaseSec=juce::jmax(0.02f,s);
    const float dt=1.f/(float)currentHz;                  // cadence réelle du timer
    releaseAlpha=std::exp(-dt/releaseSec);
}
void MultipanoramaDisplay::timerCallback()
{
    const int N=processor.getNumActiveBands();
    processor.getBandPan   (pan,    N);
    processor.getBandLevels(energy, N);
    processor.getBandCorr  (corr,   N);

    bool anyActive=false;
    for(int b=0;b<N;++b) if(energy[b] > -54.f) { anyActive=true; break; }

    if(anyActive) silentFrames=0;
    else if(silentFrames<SILENT_FRAMES_MAX) ++silentFrames;

    // Optimisation CPU/GPU : une fois le trail totalement décru en silence,
    // on arrête de retoucher l'image entière à chaque frame.
    if(anyActive || silentFrames<SILENT_FRAMES_MAX){
        renderTrail();
        repaint();
    }
}
int MultipanoramaDisplay::xToBand(float px) const
{
    const int W=getWidth();
    const float padL=kReleaseFaderW+juce::jlimit(16.f,28.f,W*.038f), padR=24.f;
    const float gw=(float)W-padL-padR;
    const BandAxis axis(processor.getBandFreqs(), processor.getNumActiveBands(), padL, gw);   // bandes extrêmes entières
    auto freqToX=[&](float f)->float{
        return axis.freqToX(f);
    };
    const int N=processor.getNumActiveBands();
    const float* F=processor.getBandFreqs();
    int best=0; float bestD=1e9f;
    for(int b=0;b<N;++b){ const float d=std::abs(freqToX(F[b])-px); if(d<bestD){bestD=d;best=b;} }
    return best;
}
void MultipanoramaDisplay::mouseDown(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown()) return;  // clic droit = menu slot
    const int band=xToBand((float)e.x);
    if(e.mods.isCtrlDown()||e.mods.isCommandDown()){
        if(band>=0){
            multiSelectMask ^= LeakAudioProcessor::bandBit(band);
            lastDragBand=band;
        }
    } else {
        multiSelectMask=0;
        soloBand=band;
    }
    if(onSelectionChanged) onSelectionChanged();
    repaint();
}
void MultipanoramaDisplay::mouseUp(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown()) return;
    lastDragBand=-1;
    if(soloBand!=-1){
        soloBand=-1;
        repaint();
    }
}
void MultipanoramaDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if(e.mods.isRightButtonDown()) return;
    if(!(e.mods.isCtrlDown()||e.mods.isCommandDown())) return;
    const int band=xToBand((float)e.x);
    if(band>=0 && band!=lastDragBand){
        multiSelectMask |= LeakAudioProcessor::bandBit(band);
        lastDragBand=band;
        repaint();
    }
}
void MultipanoramaDisplay::mouseMove(const juce::MouseEvent& e)
{
    hoverPixX=(float)e.x;
    const int nb=xToBand(hoverPixX);
    if(nb!=hoveredBand){hoveredBand=nb;repaint();}
}
void MultipanoramaDisplay::mouseExit(const juce::MouseEvent&)
{
    bool needsRepaint=false;
    if(soloBand!=-1){soloBand=-1;needsRepaint=true;}
    if(hoveredBand!=-1){hoveredBand=-1;needsRepaint=true;}
    if(needsRepaint) repaint();
}
// Dégradé thermique par déviation de pan : cyan (centre/mono) → vert → jaune → orange → rouge (hard L ou R)
juce::Colour MultipanoramaDisplay::heatColor(float amp01)
{
    amp01=juce::jlimit(0.f,1.f,amp01);
    const juce::Colour stops[5] = {   // (pas static : suit la palette, écrans Papier compris)
        Col::accent,                 // cyan — centré / mono
        Col::ink(juce::Colour(0xFF30FF40)),    // vert
        Col::ink(juce::Colour(0xFFFFE030)),    // jaune
        Col::ink(juce::Colour(0xFFFF8800)),    // orange
        Col::ink(juce::Colour(0xFFFF2020))     // rouge — hard L/R
    };
    const float seg=amp01*4.f;
    const int   i=juce::jlimit(0,3,(int)seg);
    const float f=seg-(float)i;
    return stops[i].interpolatedWith(stops[i+1],f);
}
void MultipanoramaDisplay::renderTrail()
{
    const int W=getWidth(), H=getHeight();
    if(W<10||H<10) return;

    // Crée ou recycle l'image de trail
    if(!trailValid || trailImage.getWidth()!=W || trailImage.getHeight()!=H){
        trailImage = juce::Image(juce::Image::ARGB,W,H,true,juce::SoftwareImageType());   // logicielle (cf. Vectorscope)
        trailValid = true;
    }

    // Décroissance phosphore — pilotée par le RELEASE général
    trailImage.multiplyAllAlphas(releaseAlpha);

    juce::Graphics tg(trailImage);
    const float padL=kReleaseFaderW+juce::jlimit(16.f,28.f,W*.038f);
    const float padR=24.f, padT=22.f, padB=6.f;
    const float gw=(float)W-padL-padR;
    const float gh=(float)H-padT-padB;
    const float cy=padT+gh*0.5f;

    const BandAxis axis(processor.getBandFreqs(), processor.getNumActiveBands(), padL, gw);   // bandes extrêmes entières
    auto freqToX=[&](float f)->float{
        return axis.freqToX(f);
    };

    const int N=processor.getNumActiveBands();
    const float* F=processor.getBandFreqs();

    // Les points doivent rester visuellement cohérents quelle que soit la
    // taille du panneau (slot petit ou grand) — on les met à l'échelle.
    const float sizeScale=juce::jlimit(0.7f,2.4f,gh/140.f);

    for(int b=0;b<N;++b){
        const float db=energy[b];
        if(db < -54.f) continue;                                   // sous le bruit
        const float amp=juce::jlimit(0.f,1.f,(db+60.f)/55.f);    // 0 = -60dB, 1 = -5dB
        const float p=pan[b];                                       // -1=gauche, +1=droite

        const float fx=freqToX(F[b]);
        // Y : L en haut (–1 → cy-...) / R en bas (+1 → cy+...)
        const float fy=cy + p*(gh*0.46f);

        const float sz=6.5f * sizeScale;
        const float al=juce::jlimit(0.25f, 0.95f, amp*0.9f);

        // Couleur thermique par déviation de pan (cyan centré → rouge hard L/R)
        const juce::Colour col=heatColor(std::abs(p));

        // Largeur stéréo : un contenu décorrélé (corr faible/négatif) porte de
        // l'énergie indépendante à la fois à gauche ET à droite, même quand la
        // balance nette tombe au centre. On étire alors le marqueur dans les
        // deux sens plutôt que de l'écraser en un seul point. Quand le signal
        // est quasi tout à gauche ou tout à droite, l'autre canal est quasi
        // silencieux et la corrélation devient indéfinie/nulle — il ne faut
        // alors PAS étirer le marqueur, donc on pondère par l'équilibre L/R.
        const float balance=1.f-std::abs(p);   // 1 au centre, 0 en hard L/R
        const float spread=juce::jlimit(0.f,1.f,(1.f-corr[b])*0.5f)*balance;
        if(spread>0.12f){
            const float reach=spread*gh*0.40f;
            const float yTop=juce::jlimit(padT,padT+gh,fy-reach);
            const float yBot=juce::jlimit(padT,padT+gh,fy+reach);
            tg.setColour(col.withAlpha(al*0.55f));
            tg.fillRoundedRectangle(fx-sz*0.32f,yTop,sz*0.64f,yBot-yTop,sz*0.3f);
        }
        tg.setColour(col.withAlpha(al));
        tg.fillEllipse(fx-sz*0.5f, fy-sz*0.5f, sz, sz);
    }
}
void MultipanoramaDisplay::paint(juce::Graphics& g)
{
    const int W=getWidth(), H=getHeight();
    if(W<10||H<10) return;

    // Fond noir — cohérent avec les autres modules
    g.setColour(Col::bg); g.fillRect(getLocalBounds());

    const float padL=kReleaseFaderW+juce::jlimit(16.f,28.f,W*.038f);
    const float padR=24.f, padT=22.f, padB=6.f;
    const float gw=(float)W-padL-padR;
    const float gh=(float)H-padT-padB;
    const float cy=padT+gh*0.5f;

    const BandAxis axis(processor.getBandFreqs(), processor.getNumActiveBands(), padL, gw);   // bandes extrêmes entières
    auto freqToX=[&](float f)->float{
        return axis.freqToX(f);
    };

    const int N=processor.getNumActiveBands();
    const float* F=processor.getBandFreqs();

    // ── Lignes L/C/R ─────────────────────────────────────────────────────
    g.setColour(Col::accent.withAlpha(0.12f));
    g.drawLine(padL, cy, padL+gw, cy, 2.4f);              // halo doux derrière le centre
    g.setColour(Col::gridCtr.withAlpha(0.85f));
    g.drawLine(padL, cy, padL+gw, cy, 0.8f);              // Centre
    g.setColour(Col::grid.withAlpha(0.5f));
    g.drawLine(padL, padT,    padL+gw, padT,    0.6f);    // L (haut)
    g.drawLine(padL, padT+gh, padL+gw, padT+gh, 0.6f);   // R (bas)
    g.setColour(Col::grid.withAlpha(0.42f));
    g.drawLine(padL, cy-gh*0.25f, padL+gw, cy-gh*0.25f, 0.5f);
    g.drawLine(padL, cy+gh*0.25f, padL+gw, cy+gh*0.25f, 0.5f);

    // Repères chiffrés de position (droite) — lecture rapide du %  panoramique
    g.setFont(Ui::iceland((7.5f)));
    g.setColour(Col::textMid.withAlpha(0.45f));
    g.drawText("100",(int)(padL+gw)+2,(int)padT-1,30,9,juce::Justification::left);   // sous la ligne du titre
    g.drawText("50", (int)(padL+gw)+2,(int)(cy-gh*0.25f)-5,30,9,juce::Justification::left);
    g.drawText("0",  (int)(padL+gw)+2,(int)cy-5,30,9,juce::Justification::left);
    g.drawText("50", (int)(padL+gw)+2,(int)(cy+gh*0.25f)-5,30,9,juce::Justification::left);
    g.drawText("100",(int)(padL+gw)+2,(int)(padT+gh)-5,30,9,juce::Justification::left);

    // ── Surlignage persistant des bandes multi-sélectionnées (Ctrl+clic/glisser) ──
    if(multiSelectMask!=0ULL){
        for(int b=0;b<N;++b){
            if(multiSelectMask & LeakAudioProcessor::bandBit(b)){
                const float fx=freqToX(F[b]);
                g.setColour(Col::accent.withAlpha(0.14f));
                g.fillRect(fx-7.f,padT,14.f,gh);
                g.setColour(Col::accent.withAlpha(0.45f));
                g.drawLine(fx,padT,fx,padT+gh,1.0f);
            }
        }
    }

    // ── Surlignage de la bande soloée (sous le trail) ──────────────────────
    if(soloBand>=0 && soloBand<N){
        const float fx=freqToX(F[soloBand]);
        g.setColour(Col::accent.withAlpha(0.12f));
        g.fillRect(fx-7.f,padT,14.f,gh);
        g.setColour(Col::accent.withAlpha(0.55f));
        g.drawLine(fx,padT,fx,padT+gh,1.2f);
    }

    // ── Labels L / C / R (gauche) ─────────────────────────────────────────
    g.setFont(Ui::iceland((8.f)));
    g.setColour(Col::ink(juce::Colour(0xFF3090FF)).withAlpha(0.7f));
    g.drawText("L", 0,(int)padT-2,(int)padL,10,juce::Justification::centredRight);   // sous la ligne du titre
    g.setColour(Col::textMid.withAlpha(0.6f));
    g.drawText("C", 0,(int)cy-5, (int)padL,10,juce::Justification::centredRight);
    g.setColour(Col::sideCol.withAlpha(0.7f));
    g.drawText("R", 0,(int)(padT+gh)-6,(int)padL,10,juce::Justification::centredRight);

    // ── Trail phosphore ───────────────────────────────────────────────────
    if(trailValid) g.drawImageAt(trailImage,0,0);

    // ── Tooltip de survol : fréquence uniquement ───────────────────────────
    if(hoveredBand>=0 && hoveredBand<N){
        const float f=F[hoveredBand];
        const juce::String freqStr=(f>=1000.f)
            ?(juce::String(f/1000.f,(std::fmod(f,1000.f)==0.f)?0:2)+" kHz")
            :(juce::String((int)f)+" Hz");
        const float tfs=juce::jlimit(8.f,11.f,gh*.06f);
        juce::Font tipFont=Ui::iceland(tfs);
        juce::GlyphArrangement ga; ga.addLineOfText(tipFont,freqStr,0.f,0.f);
        const float tipW=ga.getNumGlyphs()>0?ga.getBoundingBox(0,ga.getNumGlyphs(),true).getWidth()+12.f:60.f;
        const float th=tfs+8.f;
        float tx=hoverPixX+8.f;
        if(tx+tipW>(float)W-4.f) tx=hoverPixX-tipW-8.f;
        const float ty=padT+4.f;
        g.setColour(Col::panel); g.fillRoundedRectangle(tx,ty,tipW,th,3.f);
        g.setColour(Col::border); g.drawRoundedRectangle(tx,ty,tipW,th,3.f,0.7f);
        g.setFont(tipFont); g.setColour(Col::accent);
        g.drawText(freqStr,(int)(tx+6.f),(int)ty,(int)(tipW-7.f),(int)th,juce::Justification::centredLeft);
    }

    Ui::drawTitleDecor(g, (float)W);
    g.setFont(Ui::iceland((10.5f)).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    g.drawFittedText(TR("PANORAMA"), 6,5,W-12,13,juce::Justification::left,1,0.7f);
}

//==============================================================================
// PanoramaLedDisplay
//==============================================================================
PanoramaLedDisplay::PanoramaLedDisplay(LeakAudioProcessor& p) : processor(p)
{
    std::fill(std::begin(inCorr), std::end(inCorr), 1.f);
    std::fill(std::begin(dbS),    std::end(dbS),    -120.f);
    addAndMakeVisible(relFader);
    relFader.setValue(processor.panoReleaseSec, juce::dontSendNotification);
    relFader.onValueChange=[this](float v){ processor.panoReleaseSec=v; };   // même release que le Panorama
    startTimerHz(60);
}

void PanoramaLedDisplay::resized()
{
    const float padT=22.f, padB=6.f;
    relFader.setBounds(2,(int)padT,(int)kReleaseFaderW-4,juce::jmax(10,(int)((float)getHeight()-padT-padB)));
    offKey=0;   // grille éteinte à refaire
}

// mêmes couleurs que le Panorama (heatColor) : écart au centre
juce::Colour PanoramaLedDisplay::ledColour(float dev)
{
    dev=juce::jlimit(0.f, 1.f, std::abs(dev));
    const juce::Colour stops[5] = { Col::accent, Col::ink(juce::Colour(0xFF30FF40)), Col::ink(juce::Colour(0xFFFFE030)),
                                    Col::ink(juce::Colour(0xFFFF8800)), Col::ink(juce::Colour(0xFFFF2020)) };
    const float seg=dev*4.f;
    const int   i=juce::jlimit(0,3,(int)seg);
    return stops[i].interpolatedWith(stops[i+1], seg-(float)i);
}

void PanoramaLedDisplay::timerCallback()
{
    const int N=juce::jlimit(0, NMAX, processor.getNumActiveBands());
    if(N<=0) return;
    processor.getBandPan   (inPan,  N);
    processor.getBandLevels(inDb,   N);
    processor.getBandCorr  (inCorr, N);
    // nombre de bandes connu (lancement) ou changé : grille à (re)dessiner même
    // sans son — sinon le module restait vide jusqu'au premier signal
    const bool layoutChanged=N!=nBands;
    if(layoutChanged){
        nBands=N; offKey=0;
        for(int b=0;b<N;++b){ panS[b]=inPan[b]; levS[b]=0.f; widS[b]=0.f; dbS[b]=inDb[b]; }
    }
    // position et largeur lissées ; niveau : montée rapide, retombée selon la
    // release du Panorama
    const float dt=1.f/(float)currentHz;
    const float kPan=1.f-std::exp(-dt/0.06f), kWid=1.f-std::exp(-dt/0.15f);
    const float kUp=1.f-std::exp(-dt/0.02f), kDown=1.f-std::exp(-dt/juce::jmax(0.02f, processor.panoReleaseSec));
    bool active=false;
    for(int b=0;b<N;++b){
        const float e=juce::jlimit(0.f, 1.f, (inDb[b]+60.f)/54.f);
        levS[b]+=(e-levS[b])*(e>levS[b] ? kUp : kDown);
        dbS[b] +=(inDb[b]-dbS[b])*(inDb[b]>dbS[b] ? kUp : kDown);
        panS[b]+=(juce::jlimit(-1.f, 1.f, inPan[b])-panS[b])*kPan;
        widS[b]+=(juce::jlimit(0.f, 1.f, (1.f-inCorr[b])*0.5f)-widS[b])*kWid;
        if(levS[b]>0.01f) active=true;
    }
    // une dernière image quand tout s'éteint (sinon les dernières LED restaient figées)
    if(active || wasActive || layoutChanged || hovering) repaint();
    wasActive=active;
}

void PanoramaLedDisplay::paint(juce::Graphics& g)
{
    const int W=getWidth(), H=getHeight();
    if(W<10||H<10) return;
    g.setColour(Col::bg); g.fillRect(getLocalBounds());

    // ── cadre identique au Panorama ─────────────────────────────────────
    const float padL=kReleaseFaderW+juce::jlimit(16.f,28.f,W*.038f);
    const float padR=24.f, padT=22.f, padB=6.f;
    const float gw=(float)W-padL-padR, gh=(float)H-padT-padB;
    const float cy=padT+gh*0.5f;
    const int   N=nBands;
    const BandAxis axis(processor.getBandFreqs(), juce::jmax(1, N), padL, gw);

    g.setFont(Ui::iceland((7.5f)));
    g.setColour(Col::textMid.withAlpha(0.45f));
    g.drawText("100",(int)(padL+gw)+2,(int)padT-1,30,9,juce::Justification::left);   // sous la ligne du titre
    g.drawText("50", (int)(padL+gw)+2,(int)(cy-gh*0.25f)-5,30,9,juce::Justification::left);
    g.drawText("0",  (int)(padL+gw)+2,(int)cy-5,30,9,juce::Justification::left);
    g.drawText("50", (int)(padL+gw)+2,(int)(cy+gh*0.25f)-5,30,9,juce::Justification::left);
    g.drawText("100",(int)(padL+gw)+2,(int)(padT+gh)-5,30,9,juce::Justification::left);
    g.setFont(Ui::iceland((8.f)));
    g.setColour(Col::ink(juce::Colour(0xFF3090FF)).withAlpha(0.7f));
    g.drawText("L", 0,(int)padT-2,(int)padL,10,juce::Justification::centredRight);   // sous la ligne du titre
    g.setColour(Col::textMid.withAlpha(0.6f));
    g.drawText("C", 0,(int)cy-5, (int)padL,10,juce::Justification::centredRight);
    g.setColour(Col::sideCol.withAlpha(0.7f));
    g.drawText("R", 0,(int)(padT+gh)-6,(int)padL,10,juce::Justification::centredRight);

    Ui::drawTitleDecor(g, (float)W);
    g.setFont(Ui::iceland((10.5f)).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    g.drawFittedText(TR("PANORAMA LED"),6,5,W-12,13,juce::Justification::left,1,0.7f);

    if(N<=0 || gw<10.f || gh<10.f) return;

    // ── LED : nombre impair (une LED pile au centre), pas vertical régulier ──
    int L=juce::jlimit(9, 25, (int)(gh/9.f));
    if((L&1)==0) --L;
    const float dy=gh/(float)(L-1);
    const float ledH=juce::jmax(2.f, dy*0.64f);
    auto ledRect=[&](int b, int k){
        const float bw=juce::jmax(2.f, axis.colW(b)*0.62f);
        const float x=axis.colX(b)+(axis.colW(b)-bw)*0.5f;
        return juce::Rectangle<float>(x, padT+(float)k*dy-ledH*0.5f, bw, ledH);
    };

    // grille éteinte en cache
    const float scale=g.getInternalContext().getPhysicalPixelScaleFactor();
    const int key=W*131+H*7+N*3+L+juce::roundToInt(scale*100.f)*17+Col::generation*7919;
    if(key!=offKey || !offGrid.isValid()){
        offKey=key;
        offGrid=juce::Image(juce::Image::ARGB, juce::jmax(1,juce::roundToInt((float)W*scale)), juce::jmax(1,juce::roundToInt((float)H*scale)), true);
        juce::Graphics og(offGrid);
        og.addTransform(juce::AffineTransform::scale(scale));
        og.setColour(Col::light(Col::paper ? 0.07f : 0.05f));
        for(int b=0;b<N;++b)
            for(int k=0;k<L;++k) og.fillRoundedRectangle(ledRect(b,k), 1.5f);
        // repère du centre
        og.setColour(Col::accent.withAlpha(0.10f));
        og.fillRect(padL, cy-0.5f, gw, 1.f);
    }
    g.drawImage(offGrid, getLocalBounds().toFloat());

    // LED allumées : position + voisines selon la largeur, éclat selon le niveau
    for(int b=0;b<N;++b){
        const float lev=levS[b];
        if(lev<0.01f) continue;
        const float pos=(panS[b]+1.f)*0.5f*(float)(L-1);
        const float reach=0.6f+widS[b]*(float)L*0.5f;
        const int k0=juce::jmax(0, (int)std::floor(pos-reach)), k1=juce::jmin(L-1, (int)std::ceil(pos+reach));
        for(int k=k0;k<=k1;++k){
            const float on=juce::jmax(0.f, 1.f-std::abs((float)k-pos)/reach)*(0.35f+0.65f*lev);
            if(on<0.02f) continue;
            const auto col=ledColour((float)k/(float)(L-1)*2.f-1.f);
            const auto r=ledRect(b,k);
            if(on>0.55f){   // halo des LED franchement allumées
                g.setColour(col.withAlpha(0.22f*on));
                g.fillRoundedRectangle(r.expanded(2.f, 1.5f), 3.f);
            }
            g.setColour(col.withAlpha(0.2f+0.8f*on));
            g.fillRoundedRectangle(r, 1.5f);
        }
    }

    // survol : bande la plus proche, position et niveau
    if(hovering && hoverX>=padL && hoverX<=padL+gw){
        const int b=axis.bandAt(hoverX);
        const float* F=processor.getBandFreqs();
        if(b>=0 && b<N && F!=nullptr){
            const float bx=axis.colX(b)+axis.colW(b)*0.5f;
            const float by=padT+(panS[b]+1.f)*0.5f*gh;
            g.setColour(Col::accent.withAlpha(0.35f));
            g.drawVerticalLine((int)bx, padT, padT+gh);
            const float p=panS[b];
            const juce::String panTxt = std::abs(p)<0.03f ? juce::String("C")
                                      : juce::String(p<0.f ? "L " : "R ")+juce::String(juce::roundToInt(std::abs(p)*100.f))+" %";
            SpectroReadout::drawLines(g, getLocalBounds().toFloat(), { bx, by }, SpectroReadout::formatHz(F[b]), panTxt,
                                      dbS[b]<=-119.f ? juce::String("--") : juce::String(dbS[b],1)+" dB");
        }
    }
}

//==============================================================================
// SpectralDensityDisplay (PSD)
//==============================================================================
SpectralDensityDisplay::SpectralDensityDisplay(LeakAudioProcessor& p) : processor(p)
{
    std::fill(std::begin(psdDb), std::end(psdDb), -120.f);
    std::fill(std::begin(peakDb),std::end(peakDb),-120.f);
    startTimerHz(60);
}
float SpectralDensityDisplay::freqToX(float f) const
{
    const int W=getWidth();
    const float padL=juce::jlimit(18.f,32.f,W*.042f)*Ui::textScale, padR=4.f;
    const float gw=(float)W-padL-padR;
    const float lMin=std::log10(20.f), lMax=std::log10(20000.f);
    return padL+(std::log10(juce::jlimit(20.f,20000.f,f))-lMin)/(lMax-lMin)*gw;
}
float SpectralDensityDisplay::dbToY(float db) const
{
    const int H=getHeight();
    const float padT=22.f, padB=16.f;
    const float gh=(float)H-padT-padB;
    const float dbMin=-120.f, dbMax=6.f;
    const float t=juce::jlimit(0.f,1.f,(juce::jlimit(dbMin,dbMax,db)-dbMin)/(dbMax-dbMin));
    return padT+gh*(1.f-t);
}
void SpectralDensityDisplay::timerCallback()
{
    processor.getPSDData(psdDb, NB);
    bool changed=false, anyActive=false;
    for(int b=0;b<NB;++b){
        if(psdDb[b]>peakDb[b]) peakDb[b]=psdDb[b];
        else                   peakDb[b]+=(psdDb[b]-peakDb[b])*0.01f; // décroissance lente du peak-hold
        if(std::abs(psdDb[b]-peakDb[b])>0.3f) changed=true;
        if(psdDb[b]>-119.f) anyActive=true;
    }
    // La courbe PSD bouge en permanence sur du programme réel — pas de dirty-check
    // fin par bin, mais on évite au moins de repaint en silence complet prolongé.
    if(anyActive) silentFrames=0; else if(silentFrames<180) ++silentFrames;
    if(anyActive || silentFrames<180) repaint();
    juce::ignoreUnused(changed);
}
void SpectralDensityDisplay::mouseMove(const juce::MouseEvent& e)
{
    hoverPixX=(float)e.x; hovering=true;
    const int W=getWidth();
    const float padL=juce::jlimit(18.f,32.f,W*.042f)*Ui::textScale, padR=4.f;
    const float gw=(float)W-padL-padR;
    const float lMin=std::log10(20.f), lMax=std::log10(20000.f);
    const float t=juce::jlimit(0.f,1.f,(hoverPixX-padL)/gw);
    hoverFreq=std::pow(10.f,lMin+t*(lMax-lMin));
    const int bin=juce::jlimit(0,NB-1,(int)std::round(hoverFreq*(float)LeakAudioProcessor::PSD_FFT_SIZE/(float)juce::jmax(1.0,processor.getCurrentSampleRate())));
    hoverDb=psdDb[bin];
    repaint();
}
void SpectralDensityDisplay::mouseExit(const juce::MouseEvent&)
{
    hovering=false; repaint();
}
void SpectralDensityDisplay::paint(juce::Graphics& g)
{
    const int W=getWidth(), H=getHeight();
    if(W<10||H<10) return;
    g.setColour(Col::bg); g.fillRect(getLocalBounds());

    const float padL=juce::jlimit(18.f,32.f,W*.042f)*Ui::textScale, padR=4.f, padT=22.f, padB=16.f;
    const float gw=(float)W-padL-padR;
    const float gh=(float)H-padT-padB;

    // Grille verticale fréquence
    for(auto f : {50.f,100.f,200.f,500.f,1000.f,2000.f,5000.f,10000.f,20000.f}){
        const float fx=freqToX(f);
        g.setColour(Col::grid.withAlpha(0.28f));
        g.drawLine(fx,padT,fx,padT+gh,0.5f);
    }
    // Grille horizontale dB
    g.setFont(Ui::iceland((7.f)));
    for(float db : {0.f,-20.f,-40.f,-60.f,-80.f,-100.f,-120.f}){
        const float y=dbToY(db);
        g.setColour(db==0.f?Col::gridCtr.withAlpha(0.7f):Col::grid.withAlpha(0.35f));
        g.drawLine(padL,y,padL+gw,y,0.5f);
        g.setColour(Col::textMid.withAlpha(0.5f));
        const float lh=9.f*Ui::textScale;   // "-120" entier, même en gros texte (plus de "-1…")
        Ui::drawFit(g, juce::String((int)db), { 0.f, y-lh*0.5f, padL-2.f, lh }, juce::Justification::centredRight, Ui::iceland(7.f));
    }

    const double sr=juce::jmax(1.0,processor.getCurrentSampleRate());
    const float binHz=(float)(sr/(double)LeakAudioProcessor::PSD_FFT_SIZE);
    // N'affiche que les bins au-dessus de 20Hz (ignore le DC)
    const int binLo=juce::jmax(1,(int)(20.f/binHz));
    const int binHi=NB-1;

    // ── Aire remplie sous la courbe PSD ───────────────────────────────────
    juce::Path fillPath, linePath;
    bool started=false;
    for(int b=binLo;b<=binHi;++b){
        const float f=(float)b*binHz;
        const float x=freqToX(f), y=dbToY(psdDb[b]);
        if(!started){ fillPath.startNewSubPath(x,padT+gh); fillPath.lineTo(x,y); linePath.startNewSubPath(x,y); started=true; }
        else { fillPath.lineTo(x,y); linePath.lineTo(x,y); }
    }
    if(started){
        fillPath.lineTo(freqToX((float)binHi*binHz),padT+gh);
        fillPath.closeSubPath();
        g.setColour(Col::accent.withAlpha(0.16f));
        g.fillPath(fillPath);
        g.setColour(Col::accent.withAlpha(0.85f));
        g.strokePath(linePath, juce::PathStrokeType(1.3f));
    }

    // ── Peak-hold (trace plus discrète) ───────────────────────────────────
    juce::Path peakPath; bool pStarted=false;
    for(int b=binLo;b<=binHi;++b){
        const float f=(float)b*binHz;
        const float x=freqToX(f), y=dbToY(peakDb[b]);
        if(!pStarted){ peakPath.startNewSubPath(x,y); pStarted=true; } else peakPath.lineTo(x,y);
    }
    if(pStarted){
        g.setColour(Col::ink(juce::Colour(0xFFFFD200)).withAlpha(0.45f));
        g.strokePath(peakPath, juce::PathStrokeType(0.9f));
    }

    if(hovering){
        g.setColour(Col::accent.withAlpha(0.18f));
        g.drawLine(hoverPixX,padT,hoverPixX,padT+gh,1.f);
        const juce::String tip = (hoverFreq>=1000.f
            ? juce::String(hoverFreq/1000.f,1)+" kHz"
            : juce::String((int)hoverFreq)+" Hz")
            + "  •  " + juce::String(hoverDb,1)+" dB";
        const float tfs=juce::jlimit(8.f,11.f,gh*.05f);
        juce::Font tipFont=Ui::iceland(tfs);
        juce::GlyphArrangement ga; ga.addLineOfText(tipFont,tip,0.f,0.f);
        const float tipW=ga.getNumGlyphs()>0?ga.getBoundingBox(0,ga.getNumGlyphs(),true).getWidth()+12.f:120.f;
        const float th=tfs+8.f;
        float tx=hoverPixX+8.f; if(tx+tipW>(float)W-4.f) tx=hoverPixX-tipW-8.f;
        const float ty=padT+4.f;
        g.setColour(Col::panel); g.fillRoundedRectangle(tx,ty,tipW,th,3.f);
        g.setColour(Col::border); g.drawRoundedRectangle(tx,ty,tipW,th,3.f,0.7f);
        g.setFont(tipFont); g.setColour(Col::accent);
        g.drawText(tip,(int)(tx+6.f),(int)ty,(int)(tipW-7.f),(int)th,juce::Justification::centredLeft);
    }

    Ui::drawTitleDecor(g, (float)W);
    g.setFont(Ui::iceland((10.5f)).withExtraKerningFactor(0.08f));
    g.setColour(Col::textHi.withAlpha(0.85f));
    g.drawFittedText(TR("SPECTRAL DENSITY (PSD)"), 6,5,W-12,13,juce::Justification::left,1,0.7f);
}

//==============================================================================
// LeakEditor — système de 6 slots configurable par clic droit
//==============================================================================
LeakEditor::LeakEditor(LeakAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
      vectorscope(p), oscilloscope(p),
      loudnessPanel(p), vuMeter(p),
      spectrum(p), corrDisplay(p), panoDisplay(p), panoLed(p),
      spectralDensity(p), needleVu(p), mbVectorscope(p),
      spectro3dPeaks(p, p.spectro3dPeaksReverse, p.spectro3dPeaksTilt, p.spectro3dPeaksSkew, p.spectro3dPeaksTimeSec),
      spectro3dSurface(p, p.spectro3dSurfaceReverse, p.spectro3dSurfaceTilt, p.spectro3dSurfaceSkew, p.spectro3dSurfaceTimeSec),
      gpuTerrain(p, GpuSpectrogramDisplay::Style::Terrain),
      gpuNeon(p, GpuSpectrogramDisplay::Style::Neon),
      gpuCrystal(p, GpuSpectrogramDisplay::Style::Crystal),
      gpuSurface(p),
      gpuDotChroma(p, GpuDotSpectrumDisplay::Style::Chroma),
      gpuDotNebula(p, GpuDotSpectrumDisplay::Style::Nebula),
      gpuDotPano(p),
      gpuVecPhosphor(p, GpuVectorscopeDisplay::Style::Phosphor),
      gpuVecDensity(p, GpuVectorscopeDisplay::Style::Density),
      controls(spectrum,p,vuMeter,oscilloscope)
{
    setLookAndFeel(&laf);
    // langue : choix mémorisé, sinon celle de Windows au premier lancement
    if(audioProcessor.language<0) audioProcessor.language=Lang::fromSystem();
    Lang::set(audioProcessor.language);
    Neu::apply(audioProcessor.lightTint, audioProcessor.highContrast, audioProcessor.plateTint==1, audioProcessor.lightScreens!=0);
    Ui::textScale = audioProcessor.largeText ? Ui::kLargeTextScale : 1.0f;
    Ui::titleStyle = audioProcessor.titleStyle;
    Ui::corrStyle  = audioProcessor.corrStyle;
    applyColours();

    addAndMakeVisible(topBar);
    addAndMakeVisible(controls);

    // Tous les modules passent par addChildComponent — c'est updateLayout()
    // qui décide lesquels sont visibles et où ils se placent
    juce::Component* const allModules[] = {
        &vectorscope, &oscilloscope,
        &loudnessPanel, &vuMeter, &spectrum,
        &corrDisplay, &panoDisplay, &panoLed,
        &spectralDensity, &needleVu, &mbVectorscope,
        &spectro3dPeaks, &spectro3dSurface,
        &gpuTerrain, &gpuNeon, &gpuCrystal, &gpuSurface, &gpuDotChroma, &gpuDotNebula, &gpuDotPano,
        &gpuVecPhosphor, &gpuVecDensity
    };
    for(auto* c : allModules)
    {
        addChildComponent(c);
        // les modules 2D peignent tout leur rectangle : opaques, JUCE ne
        // redessine plus le châssis sous eux (les modules OpenGL restent à part)
        if(dynamic_cast<GpuModuleBase*>(c)==nullptr) c->setOpaque(true);
        // Écouter les clics droits pour afficher le menu de slot
        c->addMouseListener(this, true);
    }
    addAndMakeVisible(masterStyleToggle);   // sous le VU master (posé sur la plaque)
    // calque décor + bords des écrans : au-dessus des modules, sous les panneaux
    overlayLayer.painter = [this](juce::Graphics& g){ paintChassisOverlay(g); };
    addAndMakeVisible(overlayLayer);
    addChildComponent(aboutOverlay);
    addChildComponent(optionsOverlay);
    addChildComponent(licenseOverlay);   // au-dessus de tout
    aboutOverlay.addComponentListener(this);
    optionsOverlay.addComponentListener(this);
    licenseOverlay.addComponentListener(this);

    // Licence : logotype de la feuille, pastille DÉMO de l'en-tête
    licenseOverlay.drawBrand = [](juce::Graphics& g, juce::Rectangle<float> r){
        const float cap=r.getHeight()*0.55f;
        juce::Font wf(juce::FontOptions(audiowideTypeface()).withHeight(100.f));
        juce::GlyphArrangement m; m.addLineOfText(wf, "M", 0.f, 0.f);
        wf=wf.withHeight(100.f*cap/juce::jmax(1.f, -m.getBoundingBox(0,-1,false).getY())).withExtraKerningFactor(0.08f);
        juce::GlyphArrangement word; word.addLineOfText(wf, "MERLIN", 0.f, 0.f);
        const auto ink=word.getBoundingBox(0,-1,false);
        const float x0=r.getX()-ink.getX(), base=r.getCentreY()+cap*0.5f;
        g.setColour(juce::Colour(OptionsUi::lightTheme() ? 0xFF4A5462 : 0xFFC9D0D9));
        word.draw(g, juce::AffineTransform::translation(x0, base));
        drawAlphaBadge(g, { x0+ink.getRight()+0.47f*cap+0.79f*cap, base-cap*0.5f }, 0.79f*cap);
    };
    licenseOverlay.onDismiss = [this]{ updateLicenseUi(); };
    aboutOverlay.setLicenseManager(*license);   // pied de page de l'À propos : clé ou jours de démo
    aboutOverlay.onOpenLicence = [this]{ openLicence(); };
    topBar.onDemoClicked = [this]{ openLicence(); };
    license->addChangeListener(this);
    updateLicenseUi();

    topBar.onAboutClicked    = [this]{
        closeOptions();
        if(aboutOverlay.isVisible()) aboutOverlay.hideAnimated(); else aboutOverlay.showAnimated();
    };
    // RESET de l'en-tête (v1.73) — un clic sur le titre fait la même chose
    topBar.onResetClicked    = [this]{
        audioProcessor.resetLoudness();
        spectrum.resetAll();
        vuMeter.resetAll();
    };
    topBar.onTitleClicked    = [this]{ if(topBar.onResetClicked) topBar.onResetClicked(); };
    topBar.onFullScreenClicked = [this]{ if(onToggleFullScreen) onToggleFullScreen(); };
    topBar.onKeyboardToggled   = [this](bool on){ if(host.setKeyboardInMerlin) host.setKeyboardInMerlin(on); };
    topBar.onSettingsClicked = [this]{ openOptions(); };   // menu Options (trois points)
    // presets : au centre de la bande du bas (v1.73)
    auto& pb=controls.presets;
    pb.onPresetSelected = [this](int idx){ applyPreset(idx); };
    refreshPresetList();
    pb.onUserPresetSelected    = [this](const juce::String& n){ loadUserPreset(n); };
    pb.onSavePresetRequested   = [this]{ askSavePreset(); };
    pb.onDeletePresetRequested = [this](const juce::String& n){ askDeletePreset(n); };
    pb.onOpenPresetFolder      = []{ auto f=LeakAudioProcessor::getPresetFolder(); f.createDirectory(); f.startAsProcess(); };

    // Interrupteurs thème (lune) / contraste, indépendants du template
    topBar.setLookState(!audioProcessor.lightTint, audioProcessor.highContrast, audioProcessor.modules3D,
                        audioProcessor.largeText);
    topBar.onLargeTextToggled = [this](bool on){ audioProcessor.largeText=on; applyTextScale(); };
    topBar.onAlwaysOnTopToggled = [this](bool on){ if(host.setAlwaysOnTop) host.setAlwaysOnTop(on); };

    // Section master : design A "Instrument" / C "Console mastering" —
    // interrupteur sous la colonne du VU master
    vuMeter.setConsoleStyle(audioProcessor.masterConsole);
    masterStyleToggle.setTooltip(TR("Master meter: Instrument / Console"));
    masterStyleToggle.setToggleState(audioProcessor.masterConsole, juce::dontSendNotification);
    masterStyleToggle.onClick=[this]{ audioProcessor.masterConsole=masterStyleToggle.getToggleState(); vuMeter.setConsoleStyle(audioProcessor.masterConsole); };
    topBar.onThemeToggled    = [this](bool dark){ audioProcessor.lightTint=!dark;     applyLook(); };
    topBar.onContrastToggled = [this](bool on)  { audioProcessor.highContrast=on;     applyLook(); };

    // Interrupteur cube : styles 3D des modules (Spectrum : Blocks 3D)
    spectrum.set3D(audioProcessor.modules3D);
    topBar.onModules3DToggled = [this](bool on){ audioProcessor.modules3D=on; spectrum.set3D(on); };

    // Sélection de bandes exclusive : un seul module audité à la fois —
    // toute nouvelle sélection efface celle des deux autres modules.
    spectrum.onSelectionChanged    = [this]{ corrDisplay.clearBandSelection(); panoDisplay.clearBandSelection(); };
    corrDisplay.onSelectionChanged = [this]{ spectrum.clearBandSelection();    panoDisplay.clearBandSelection(); };
    panoDisplay.onSelectionChanged = [this]{ spectrum.clearBandSelection();    corrDisplay.clearBandSelection(); };


    setupOptions();
    applyGraphics();
    applyLanguage();   // textes posés à la construction -> langue choisie

    setInterceptsMouseClicks(true,true);

    // taille mini 1024×576 (même limite que la fenêtre, cf. Main.cpp)
    const int w=juce::jlimit(1024,2400,audioProcessor.editorWidth);
    const int h=juce::jlimit(576,1400,audioProcessor.editorHeight);
    setSize(w,h);
    setResizable(true,true);
    setResizeLimits(1024,576,2400,1400);
    applyUiScale();   // plugin : taille de l'interface (écran principal tant que l'éditeur n'est pas affiché)
    startTimerHz(60);
}

void LeakEditor::setScaleFactor(float newScale)
{
    hostScale=newScale;
    appliedUiScale=0.f;   // à réappliquer, combinée à la taille choisie
    applyUiScale();
}

void LeakEditor::applyUiScale()
{
    if(!AppMode::isPlugin){ if(host.uiScaleChanged) host.uiScaleChanged(); return; }
    const auto& displays=juce::Desktop::getInstance().getDisplays();
    const auto* d=isShowing() ? displays.getDisplayForRect(getScreenBounds()) : displays.getPrimaryDisplay();
    const float s=Ui::uiScaleFor(audioProcessor.uiScale, d);
    if(std::abs(s-appliedUiScale)<0.001f) return;
    appliedUiScale=s;
    juce::AudioProcessorEditor::setScaleFactor(hostScale*s);   // la DAW redimensionne sa fenêtre
}
LeakEditor::~LeakEditor()
{
    stopTimer();
    floating.clear();                    // fenêtres détachées fermées (les modules restent à l'éditeur)
    optionsWindow.reset();               // idem pour la fenêtre des Options
    license->removeChangeListener(this);
    aboutOverlay.removeComponentListener(this);
    optionsOverlay.removeComponentListener(this);
    licenseOverlay.removeComponentListener(this);
    setLookAndFeel(nullptr);
}

//==============================================================================
// Menu Options
//==============================================================================
void LeakEditor::syncTopBarLook()
{
    topBar.setLookState(!audioProcessor.lightTint, audioProcessor.highContrast, audioProcessor.modules3D, audioProcessor.largeText);
}

OptionsOverlay::State LeakEditor::optionsState() const
{
    OptionsOverlay::State s;
    s.audioSource   = host.getAudioSource ? host.getAudioSource() : 0;
    s.sourceName    = host.getSourceName  ? host.getSourceName()  : juce::String();
    s.windowsVolume = WindowsVolume::get();
    s.fps   = audioProcessor.uiFps;
    s.msaa  = audioProcessor.gpuMsaa;
    s.vsync = audioProcessor.gpuVsync;
    s.gpuInfo = GpuModuleBase::getRendererInfo();
    s.fullScreen  = host.isFullScreen  ? host.isFullScreen()  : false;
    s.alwaysOnTop = host.isAlwaysOnTop ? host.isAlwaysOnTop() : false;
    s.dark      = !audioProcessor.lightTint;
    s.contrast  = audioProcessor.highContrast;
    s.modules3D = audioProcessor.modules3D;
    s.largeText = audioProcessor.largeText;
    s.masterConsole = audioProcessor.masterConsole;
    s.oscStyle   = audioProcessor.oscStyle;
    s.oscFps     = audioProcessor.oscFps;
    s.oscPersist = audioProcessor.oscPersist;
    s.oscSplit   = audioProcessor.oscSplit;
    s.oscLineWidth = audioProcessor.oscLineWidth;
    s.oscTraceMode = audioProcessor.oscTraceMode;
    s.uiScale = audioProcessor.uiScale;
    s.plateTint = audioProcessor.plateTint;
    s.lightScreens = audioProcessor.lightScreens;
    s.titleStyle = audioProcessor.titleStyle;
    s.corrStyle = audioProcessor.corrStyle;
    for(int i=0;i<Theme::NUM_PALETTES;++i){
        s.accentPalette.add(juce::Colour(Theme::palettes[i].accent));
        if(Theme::palettes[i].accent==audioProcessor.customAccentColor) s.accentIndex=i;
    }
    const uint32_t roles[8] = { audioProcessor.customAccentColor, audioProcessor.customGridColor, audioProcessor.customBgColor,
                                audioProcessor.customTextColor, audioProcessor.customSideColor, audioProcessor.customSpectroLowColor,
                                audioProcessor.customSpectroMidColor, audioProcessor.customSpectroHighColor };
    for(int i=0;i<8;++i) s.roles[i]=juce::Colour(roles[i]);
    s.presetName = audioProcessor.currentPresetName;
    {
        using S=LicenseManager::Status;
        const auto ls=license->getStatus();
        s.licenceText = ls==S::licensed ? TR("Activée sur cet ordinateur")
                      : ls==S::trial    ? TR("Démo : {0} jours restants").replace("{0}", juce::String(license->getTrialDaysLeft()))
                                        : TR("Non activée");
    }
    s.language   = audioProcessor.language;
    return s;
}

void LeakEditor::openOptions()
{
    if(optionsOverlay.isVisible()){ closeOptions(); return; }
    aboutOverlay.setVisible(false);
    optionsOverlay.refreshTheme(audioProcessor.lightTint);
    optionsOverlay.setState(optionsState());

    if(!AppMode::isPlugin){
        // appli : fenêtre à part, déplaçable (autre écran possible) ; Merlin et
        // ses modules GPU restent visibles et actifs derrière
        // la fenêtre existe AVANT que le panneau devienne visible : sinon le
        // changement de visibilité suspendait les modules GPU (panneau cru
        // posé sur l'éditeur) et rien ne les relançait — oscillo figé
        removeChildComponent(&optionsOverlay);
        optionsOverlay.setWindowed(true);
        optionsOverlay.setSize(1000, 640);
        optionsWindow=std::make_unique<DetachedWindow>(juce::String(juce::CharPointer_UTF8("Merlin \xce\xb1 \xc2\xb7 "))+TR("Options"), optionsOverlay,
            [sp=juce::Component::SafePointer<LeakEditor>(this)]{
                juce::MessageManager::callAsync([sp]{ if(sp!=nullptr) sp->closeOptions(); });
            });
        auto& w=*optionsWindow;
        w.setResizeLimits(760, 480, 2400, 1600);
        w.centreAroundComponent(this, w.getWidth(), w.getHeight());
        w.addToDesktop();
        w.setAlwaysOnTop(alwaysOnTopState);   // comme la fenêtre principale
        if(auto* peer=w.getPeer())
            WindowsDisplay::setNoActivate(peer->getNativeHandle(), !keyboardState);
        optionsOverlay.setVisible(true);
        w.setVisible(true);
        w.toFront(true);
        updateGpuSuspension();   // modules GPU actifs derrière la fenêtre des Options
        return;
    }
    optionsOverlay.setVisible(true);
    optionsOverlay.toFront(true);
}

// Ferme les Options (fenêtre de l'appli ou panneau du plugin) ; le panneau
// revient, masqué, dans l'éditeur pour la prochaine ouverture
void LeakEditor::closeOptions()
{
    optionsOverlay.setVisible(false);
    if(optionsWindow!=nullptr){
        optionsWindow.reset();   // ne détruit pas le panneau (contenu non possédé)
        optionsOverlay.setWindowed(false);
        addChildComponent(&optionsOverlay, getIndexOfChildComponent(&licenseOverlay));   // sous l'écran de licence
        optionsOverlay.setBounds(getLocalBounds());
    }
    updateGpuSuspension();
}

void LeakEditor::setupOptions()
{
    auto& o=optionsOverlay;
    juce::Component::SafePointer<LeakEditor> sp(this);
    auto refreshSoon=[sp]{   // nom de la source connu une fois la capture ouverte
        juce::Timer::callAfterDelay(700, [sp]{ if(sp!=nullptr && sp->optionsOverlay.isVisible()) sp->optionsOverlay.setState(sp->optionsState()); });
    };
    o.onDismiss = [this]{ closeOptions(); };
    // Audio
    o.onAudioSourceSelected  = [this, refreshSoon](int i){ if(host.setAudioSource) host.setAudioSource(i); optionsOverlay.setState(optionsState()); refreshSoon(); };
    o.onOpenAudioSettings    = [this]{ if(host.openAudioSettings) host.openAudioSettings(); };
    o.onWindowsVolumeChanged = [](float v){ WindowsVolume::set(v); };
    // Image
    o.onFpsSelected  = [this](int hz){ audioProcessor.uiFps=hz;   applyGraphics(); };
    o.onMsaaSelected = [this](int n) { audioProcessor.gpuMsaa=n;  applyGraphics(); };
    o.onVsyncChanged = [this](bool on){ audioProcessor.gpuVsync=on; applyGraphics(); };
    // Affichage
    o.onFullScreenChanged  = [this](bool on){ if(host.isFullScreen && host.isFullScreen()!=on && onToggleFullScreen) onToggleFullScreen(); };
    o.onAlwaysOnTopChanged = [this](bool on){ if(host.setAlwaysOnTop) host.setAlwaysOnTop(on); };
    o.onDarkChanged      = [this](bool dark){ audioProcessor.lightTint=!dark; applyLook(); syncTopBarLook(); optionsOverlay.refreshTheme(!dark); };
    o.onContrastChanged  = [this](bool on){ audioProcessor.highContrast=on; applyLook(); syncTopBarLook(); };
    o.onModules3DChanged = [this](bool on){ audioProcessor.modules3D=on; spectrum.set3D(on); syncTopBarLook(); };
    o.onLargeTextChanged = [this](bool on){ audioProcessor.largeText=on; applyTextScale(); syncTopBarLook(); };
    o.onOscStyleSelected = [this](int i){ audioProcessor.oscStyle=juce::jlimit(0, 3, i); oscilloscope.syncFromProcessor(); };
    o.onOscSplitSelected = [this](bool b){ audioProcessor.oscSplit=b; oscilloscope.syncFromProcessor(); };
    oscilloscope.onSplitChanged=[this]{ if(optionsOverlay.isVisible()) optionsOverlay.setState(optionsState()); };
    o.onOscFpsSelected   = [this](int fps){ audioProcessor.oscFps=fps; oscilloscope.syncFromProcessor(); };
    o.onOscLineWidthChanged  = [this](int w){ audioProcessor.oscLineWidth=w; oscilloscope.syncFromProcessor(); };   // curseur, en direct
    o.onOscTraceModeSelected = [this](int m){ audioProcessor.oscTraceMode=m; oscilloscope.syncFromProcessor(); };
    o.onUiScaleSelected      = [this](int pct){ audioProcessor.uiScale=pct; appliedUiScale=0.f; applyUiScale(); };
    o.onPlateTintSelected    = [this](int i){ audioProcessor.plateTint=i; applyLook(); };
    o.onLightScreensSelected = [this](bool on){ audioProcessor.lightScreens=on ? 1 : 0; applyLook(); };
    o.onTitleStyleSelected   = [this](int i){ audioProcessor.titleStyle=i; applyLook(); };
    o.onCorrStyleSelected    = [this](int i){ audioProcessor.corrStyle=i; applyLook(); };
    o.onOscPersistChanged= [this](bool on){ audioProcessor.oscPersist=on; oscilloscope.syncFromProcessor(); };
    o.onMasterConsoleChanged = [this](bool on){
        audioProcessor.masterConsole=on; vuMeter.setConsoleStyle(on);
        masterStyleToggle.setToggleState(on, juce::dontSendNotification);
    };
    // Apparence : couleurs des modules
    o.onAccentSelected    = [this](int i){ selectAccentPalette(i); optionsOverlay.setState(optionsState()); };
    o.onColourRoleChanged = [this](int r, juce::Colour c){ setColourRole(r, c); };
    o.onColoursReset      = [this]{ resetColours(); optionsOverlay.setState(optionsState()); };
    // Presets / à propos
    o.onSavePreset       = [this]{ askSavePreset(); };
    o.onOpenPresetFolder = []{ auto f=LeakAudioProcessor::getPresetFolder(); f.createDirectory(); f.startAsProcess(); };
    o.onResetLayout      = [this]{ resetLayout(); closeOptions(); };
    o.onOpenAbout        = [this]{ closeOptions(); aboutOverlay.showAnimated(); };
    o.onOpenLicence      = [this]{ openLicence(); };
    // ligne Version : pastille α de Merlin au format des pictogrammes (carré arrondi 28 px)
    o.drawAppBadge = [](juce::Graphics& g, juce::Rectangle<float> r){
        g.setColour(juce::Colour(0xFF2B3037));
        g.fillRoundedRectangle(r, 7.f);
        static const juce::Path alpha=alphaGlyph();
        auto a=alpha;
        a.applyTransform(juce::AffineTransform::translation(-53.f,-55.f).scaled(r.getWidth()*0.0068f).translated(r.getCentreX(), r.getCentreY()));
        g.setColour(juce::Colour(0xFF2FE0A8));
        g.strokePath(a, juce::PathStrokeType(r.getWidth()*0.085f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    // Langue : appliquée en place (recréer l'éditeur ferait planter les modules OpenGL)
    o.onLanguageSelected = [this](int id){
        if(id==audioProcessor.language) return;
        audioProcessor.language=id;
        applyLanguage();
    };
}

//==============================================================================
// Presets utilisateur
//==============================================================================
void LeakEditor::refreshPresetList()
{
    controls.presets.setPresetList(audioProcessor.getUserPresetNames(), audioProcessor.currentPresetName);
}

void LeakEditor::loadUserPreset(const juce::String& name)
{
    if(audioProcessor.loadUserPreset(name)) reloadFromProcessor();
    refreshPresetList();
}

// Preset rappelé : l'interface en place relit tout l'état du processeur
// (recréer l'éditeur détruisait puis recréait les contextes OpenGL des modules
// GPU et faisait planter l'appli)
void LeakEditor::reloadFromProcessor()
{
    reattachAll();   // preset rappelé : disposition d'origine, modules dans l'éditeur
    // apparence
    Ui::titleStyle = audioProcessor.titleStyle;
    Ui::corrStyle  = audioProcessor.corrStyle;
    applyColours();
    syncTopBarLook();
    masterStyleToggle.setToggleState(audioProcessor.masterConsole, juce::dontSendNotification);
    vuMeter.setConsoleStyle(audioProcessor.masterConsole);
    spectrum.set3D(audioProcessor.modules3D);

    // modules : la barre du bas, le Spectrum, le Corrélateur, le Panorama, le
    // Vectorscope et le VU à aiguille sont resynchronisés au prochain tick
    initialSyncDone=false;
    panoLed.syncReleaseFromProcessor();
    oscilloscope.syncFromProcessor();
    spectro3dPeaks.syncFromProcessor();
    spectro3dSurface.syncFromProcessor();
    for(auto* g : { &gpuTerrain, &gpuNeon, &gpuCrystal }) g->syncFromProcessor();
    gpuSurface.syncFromProcessor();
    for(auto* d : { &gpuDotChroma, &gpuDotNebula }) d->syncFromProcessor();

    // teinte, template, taille du texte et disposition des slots
    applyLook();
    refreshChassis();
    applyTextScale();
    updateLayout();
    repaint();
}

void LeakEditor::askSavePreset()
{
    auto* aw=new juce::AlertWindow(TR("Enregistrer le preset"),
                                   TR("Disposition des modules, leurs réglages et l'apparence.\nUn nom existant est remplacé."),
                                   juce::MessageBoxIconType::NoIcon, this);
    aw->addTextEditor("name", audioProcessor.currentPresetName.isNotEmpty() ? audioProcessor.currentPresetName
                                                                            : TR("Mon preset"), TR("Nom"));
    aw->addButton(TR("Enregistrer"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton(TR("Annuler"),     0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<LeakEditor> sp(this);
    aw->enterModalState(true, juce::ModalCallbackFunction::create([sp, aw](int result){
        if(result!=1 || sp==nullptr) return;
        const auto name=aw->getTextEditorContents("name").trim();
        if(name.isEmpty()) return;
        sp->audioProcessor.saveUserPreset(name);
        sp->refreshPresetList();
        if(sp->optionsOverlay.isVisible()) sp->optionsOverlay.setState(sp->optionsState());
    }), true);
}

void LeakEditor::askDeletePreset(const juce::String& name)
{
    if(name.isEmpty()) return;
    juce::Component::SafePointer<LeakEditor> sp(this);
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                     .withTitle(TR("Supprimer le preset"))
                                     .withMessage(TR("Supprimer « {0} » ?").replace("{0}", name))
                                     .withButton(TR("Supprimer"))
                                     .withButton(TR("Annuler"))
                                     .withAssociatedComponent(this),
                                 [sp, name](int result){
                                     if(result!=1 || sp==nullptr) return;
                                     sp->audioProcessor.deleteUserPreset(name);
                                     sp->refreshPresetList();
                                 });
}

void LeakEditor::timerCallback()
{
    if(!initialSyncDone){
        initialSyncDone=true;
        controls.syncFromProcessor();
        spectrum.setActiveBands(audioProcessor.getNumActiveBands());
        spectrum.setAnalysisMode(audioProcessor.getAnalysisMode());
        spectrum.syncReleaseFromProcessor();
        corrDisplay.syncReleaseFromProcessor();
        panoDisplay.syncReleaseFromProcessor();
        vectorscope.syncFromProcessor();
        needleVu.syncFromProcessor();
    }
    vuMeter.update();
    loudnessPanel.update();
    topBar.setSampleRate(audioProcessor.getCurrentSampleRate());   // LCD sous le titre
    updateDockMagnet();
    followDetachDrag();   // module qu'on est en train de détacher : la fenêtre suit la souris
    // plugin en taille Auto : l'écran (donc l'échelle) peut changer quand la DAW
    // déplace sa fenêtre — vérifié une fois par seconde
    if(AppMode::isPlugin && audioProcessor.uiScale==0 && ++uiScaleCheckTicks>=60){ uiScaleCheckTicks=0; applyUiScale(); }
    // le Spectrum caché ne calcule plus : seul son état visible compte
    topBar.setGlitch(vuMeter.isClipping() || (spectrum.isVisible() && spectrum.isGlobalClipping()));
}

// ── Slot management ───────────────────────────────────────────────────────────
juce::Component* LeakEditor::getComponentForModule(int id)
{
    using M = LeakAudioProcessor::ModuleID;
    switch((M)id){
        case M::Vectorscope:   return &vectorscope;
        case M::Oscilloscope:  return &oscilloscope;
        case M::Loudness:      return &loudnessPanel;
        case M::VuMeter:       return &vuMeter;
        case M::Spectrum:      return &spectrum;
        case M::MultibandCorr: return &corrDisplay;
        case M::Multipanorama: return &panoDisplay;
        case M::PanoramaLed:   return &panoLed;
        case M::SpectralDensity: return &spectralDensity;
        case M::NeedleVU:      return &needleVu;
        case M::MultibandVectorscope: return &mbVectorscope;
        case M::Spectrogram3DPeaks:   return &spectro3dPeaks;
        case M::Spectrogram3DSurface: return &spectro3dSurface;
        case M::GpuSpectroTerrain:    return &gpuTerrain;
        case M::GpuSpectroNeon:       return &gpuNeon;
        case M::GpuSpectroCrystal:    return &gpuCrystal;
        case M::GpuSpectroSurface:    return &gpuSurface;
        case M::DotSpectrumChroma:    return &gpuDotChroma;
        case M::DotSpectrumNebula:    return &gpuDotNebula;
        case M::DotPanoramaChroma:    return &gpuDotPano;
        case M::VectorscopePhosphor:  return &gpuVecPhosphor;
        case M::VectorscopeDensity:   return &gpuVecDensity;
        default:               return nullptr;
    }
}

int LeakEditor::hitTestSlot(juce::Point<int> pt) const
{
    for(int s=0;s<LeakAudioProcessor::NUM_SLOTS;++s)
        if(rcSlot[s].contains(pt)) return s;
    return -1;
}

void LeakEditor::mouseDown(const juce::MouseEvent& e)
{
    if(!isOwnMouseEvent(e)) return;   // module détaché : sa fenêtre n'est plus dans l'éditeur
    const auto pt = e.getEventRelativeTo(this).getPosition();
    if(!e.mods.isRightButtonDown()){
        const int slot = hitTestSlot(pt);
        // emplacement d'un module détaché, partagé en deux : le ramener, ou en
        // mettre un autre (la fenêtre reste ouverte)
        if(isDetached(slot)){
            if(detachedHalf(slot, true).contains(pt)) showModuleMenu(slot, e.getScreenPosition());
            else                                       reattachSlot(slot);
            return;
        }
        // titre d'un module (bande du haut) : candidat au détachement, si on tire
        if(!AppMode::isPlugin && slot>=0 && slot!=5 && e.eventComponent!=this
           && getComponentForModule(audioProcessor.slotModule[slot])!=nullptr
           && pt.y < rcSlot[slot].getY()+juce::roundToInt(Ui::titleStripHeight())){
            dragSlot=slot;
            dragStart=e.getScreenPosition();
            dragGrab=pt-rcSlot[slot].getPosition();
        }
        // slot vide : le clic gauche ouvre aussi le menu des modules
        if(slot >= 0 && slot != 5 && getComponentForModule(audioProcessor.slotModule[slot]) == nullptr)
            showModuleMenu(slot, e.getScreenPosition());
        return;
    }
    const int slot = hitTestSlot(pt);
    // Slot F (VU master) fixe ; les autres, Loudness compris, se changent au clic droit
    if(slot >= 0 && slot != 5) showModuleMenu(slot, e.getScreenPosition());
}

//==============================================================================
// Modules détachés (appli seulement)
//==============================================================================
namespace { juce::String slotModuleName(int id); }   // défini plus bas, avec le menu des modules

void LeakEditor::mouseDrag(const juce::MouseEvent& e)
{
    if(dragSlot<0 || dragFollowing || !isOwnMouseEvent(e)) return;
    if(e.getScreenPosition().getDistanceFrom(dragStart) < 14) return;
    // hors de l'événement en cours : le module quitte l'éditeur pendant son propre clic
    dragFollowing=true;
    juce::MessageManager::callAsync([sp=juce::Component::SafePointer<LeakEditor>(this), s=dragSlot]{
        if(sp!=nullptr) sp->detachSlot(s);
    });
}

void LeakEditor::mouseUp(const juce::MouseEvent&)
{
    if(!dragFollowing) dragSlot=-1;
}

void LeakEditor::detachSlot(int s)
{
    const int module=audioProcessor.slotModule[s];
    auto* comp=getComponentForModule(module);
    if(comp==nullptr || isDetached(s) || s==5 || floatingFor(module)!=nullptr){ dragFollowing=false; dragSlot=-1; return; }

    beginWindowSwap();   // contextes OpenGL lâchés pendant le transfert, comme pour le plein écran
    removeChildComponent(comp);
    const auto title=juce::String(juce::CharPointer_UTF8("Merlin \xce\xb1 \xc2\xb7 "))+slotModuleName(module);
    // fermeture : le module revient à l'éditeur, dans son emplacement s'il l'attend
    // encore, sinon il disparaît (hors du rappel de la fenêtre qui va disparaître)
    floating.push_back(FloatingModule{ module, s, std::make_unique<DetachedWindow>(title, *comp,
        [sp=juce::Component::SafePointer<LeakEditor>(this), module]{
            juce::MessageManager::callAsync([sp, module]{ if(sp!=nullptr) sp->dockFloating(module); });
        }) });
    auto& w=*floating.back().window;
    w.setTopLeftPosition(juce::Desktop::getMousePosition()-dragGrab-w.contentOffset());
    comp->setVisible(true);
    w.addToDesktop();
    w.setAlwaysOnTop(alwaysOnTopState);   // même règle que la fenêtre principale
    if(auto* peer=w.getPeer())
        WindowsDisplay::setNoActivate(peer->getNativeHandle(), !keyboardState);   // ne prend pas le clavier à Ableton
    w.setVisible(true);
    w.toFront(true);

    layoutDirty=true;   // l'écran laisse place au creux
    updateLayout();
    juce::Timer::callAfterDelay(150, [sp=juce::Component::SafePointer<LeakEditor>(this)]{
        if(sp!=nullptr) sp->endWindowSwap();
    });
}

void LeakEditor::raiseDetachedWindows()
{
    auto raise=[](DetachedWindow* w){
        if(w!=nullptr && w->isVisible() && !w->isMinimised()) w->toFront(false);   // (toFront la rétablirait)
    };
    for(auto& f : floating) raise(f.window.get());
    raise(optionsWindow.get());
}

// la fenêtre détachée suit la souris tant que le bouton reste enfoncé (timer de l'éditeur)
void LeakEditor::followDetachDrag()
{
    if(!dragFollowing) return;
    auto* f=dragSlot>=0 ? floatingFor(audioProcessor.slotModule[dragSlot]) : nullptr;
    auto* w=f!=nullptr ? f->window.get() : nullptr;
    if(!juce::ModifierKeys::getCurrentModifiersRealtime().isLeftButtonDown()){
        dragFollowing=false;
        dragSlot=-1;
        return;
    }
    if(w==nullptr) return;
    if(magnetSlot==f->homeSlot){
        // aimant : le module se cale sur son emplacement tant que la souris y reste
        const auto* content=w->getContentComponent();
        const auto offset=content!=nullptr ? content->getScreenPosition()-w->getScreenPosition() : juce::Point<int>();
        w->setTopLeftPosition(localPointToGlobal(rcSlot[magnetSlot].getTopLeft())-offset);
    } else {
        w->setTopLeftPosition(juce::Desktop::getMousePosition()-dragGrab-w->contentOffset());
    }
}

// Timer de l'éditeur : suit aussi le déplacement par la barre de titre de la
// fenêtre (la souris y est prise par Windows, seul son état réel est lisible).
void LeakEditor::updateDockMagnet()
{
    if(floating.empty() && magnetSlot<0) return;
    constexpr int kReach=36;   // écart maximal entre la fenêtre et sa place exacte dans l'emplacement
    const bool down=juce::ModifierKeys::getCurrentModifiersRealtime().isLeftButtonDown();
    const auto mouse=juce::Desktop::getMousePosition();
    int over=-1, dock=-1;
    for(auto& f : floating){
        const auto b=f.window->getScreenBounds();
        const bool resized=b.getWidth()!=f.lastBounds.getWidth() || b.getHeight()!=f.lastBounds.getHeight();
        if(down && !f.lastBounds.isEmpty()){
            if(resized)                                            f.moving=false;   // bord tiré : ce n'est pas un déplacement
            else if(b.getPosition()!=f.lastBounds.getPosition()){
                // par sa barre, une fenêtre sans activation ne passe pas devant toute seule
                if(!f.moving) f.window->toFront(false);
                f.moving=true;
            }
        }
        f.lastBounds=b;
        if(!f.moving) continue;
        bool inSlot=false;
        if(isDetached(f.homeSlot)){
            const auto* content=f.window->getContentComponent();
            const auto inset=content!=nullptr ? content->getScreenPosition()-b.getPosition() : juce::Point<int>();
            // pendant le détachement la fenêtre peut être calée : c'est la souris qui dit où elle va
            const bool followed=dragFollowing && dragSlot==f.homeSlot;
            const auto at=followed ? mouse-dragGrab-inset : b.getPosition();
            inSlot=at.getDistanceFrom(localPointToGlobal(rcSlot[f.homeSlot].getTopLeft())-inset)<=kReach;
        }
        if(down){ if(inSlot) over=f.homeSlot; continue; }
        f.moving=false;
        if(inSlot) dock=f.module;
    }
    if(over!=magnetSlot){
        if(magnetSlot>=0) repaint(rcSlot[magnetSlot]);
        if(over>=0)       repaint(rcSlot[over]);
        magnetSlot=over;
    }
    if(dock>=0) dockFloating(dock);   // hors de la boucle : retire le module de floating
}

void LeakEditor::drawDockMagnet(juce::Graphics& g, int s)
{
    const auto r=rcSlot[s].toFloat().reduced(3.f);
    const auto ink=Neu::accentInk();
    g.setColour(Neu::well);                 // couvre les deux moitiés du creux
    g.fillRoundedRectangle(r, kScreenRadius);
    g.setColour(ink.withAlpha(0.14f));
    g.fillRoundedRectangle(r, kScreenRadius);
    g.setColour(ink);
    g.drawRoundedRectangle(r, kScreenRadius, 2.5f);
    g.setFont(Ui::iceland(13.f).withExtraKerningFactor(0.08f));
    g.drawText(TR("ramener").toUpperCase(), r, juce::Justification::centred, false);
}

LeakEditor::FloatingModule* LeakEditor::floatingFor(int module)
{
    for(auto& f : floating)
        if(f.module==module) return &f;
    return nullptr;
}

bool LeakEditor::isDetached(int s) const
{
    if(s<0 || s>=LeakAudioProcessor::NUM_SLOTS) return false;
    for(const auto& f : floating)
        if(f.homeSlot==s && f.module==audioProcessor.slotModule[s]) return true;
    return false;
}

void LeakEditor::reattachSlot(int s)
{
    if(isDetached(s)) dockFloating(audioProcessor.slotModule[s]);
}

void LeakEditor::dockFloating(int module)
{
    const auto it=std::find_if(floating.begin(), floating.end(), [module](const FloatingModule& f){ return f.module==module; });
    if(it==floating.end()) return;
    if(dragSlot>=0 && audioProcessor.slotModule[dragSlot]==module){ dragFollowing=false; dragSlot=-1; }
    beginWindowSwap();
    it->window->clearContentComponent();
    floating.erase(it);
    if(auto* comp=getComponentForModule(module))
        addChildComponent(comp, getIndexOfChildComponent(&overlayLayer));   // sous le calque des bords et les panneaux
    if(hoverDetachedSlot>=0){ hoverDetachedSlot=-1; setMouseCursor(juce::MouseCursor::NormalCursor); }
    layoutDirty=true;
    updateLayout();   // affiché s'il a un emplacement, caché sinon
    repaint();
    juce::Timer::callAfterDelay(150, [sp=juce::Component::SafePointer<LeakEditor>(this)]{
        if(sp!=nullptr) sp->endWindowSwap();
    });
}

void LeakEditor::setKeyboardState(bool b)
{
    topBar.setKeyboardState(b);
    keyboardState=b;
    for(auto& f : floating)
        if(f.window!=nullptr)
            if(auto* peer=f.window->getPeer())
                WindowsDisplay::setNoActivate(peer->getNativeHandle(), !b);
    if(optionsWindow!=nullptr)
        if(auto* peer=optionsWindow->getPeer())
            WindowsDisplay::setNoActivate(peer->getNativeHandle(), !b);
}

void LeakEditor::reattachAll()
{
    while(!floating.empty())
        dockFloating(floating.front().module);
}

// creux partagé : côte à côte dans un emplacement large, l'une au-dessus de
// l'autre dans un étroit ; « ramener » à gauche / en haut
juce::Rectangle<int> LeakEditor::detachedHalf(int s, bool addHalf) const
{
    const auto r=rcSlot[s];
    if(r.getWidth()>=r.getHeight())
        return addHalf ? r.withTrimmedLeft(r.getWidth()/2) : r.withWidth(r.getWidth()/2);
    return addHalf ? r.withTrimmedTop(r.getHeight()/2) : r.withHeight(r.getHeight()/2);
}

// emplacement d'un module détaché : un seul creux, partagé par un filet
void LeakEditor::drawDetachedSlot(juce::Graphics& g, int s)
{
    const auto r=rcSlot[s].toFloat();
    Neu::drawInset(g, r, kScreenRadius, 3.f);
    g.setColour(Neu::light ? juce::Colours::black.withAlpha(0.12f) : juce::Colours::white.withAlpha(0.10f));
    if(r.getWidth()>=r.getHeight()) g.fillRect(r.getCentreX()-0.5f, r.getY()+r.getHeight()*0.18f, 1.f, r.getHeight()*0.64f);
    else                            g.fillRect(r.getX()+r.getWidth()*0.14f, r.getCentreY()-0.5f, r.getWidth()*0.72f, 1.f);
    drawDetachedHalf(g, s, false, false);
    drawDetachedHalf(g, s, true,  false);
}

// une moitié : pictogramme, titre, légende (courte si la moitié est étroite) ;
// survolée : teinte claire et accent, peinte par-dessus le châssis en cache
void LeakEditor::drawDetachedHalf(juce::Graphics& g, int s, bool addHalf, bool hover)
{
    const auto r=detachedHalf(s, addHalf).toFloat();
    const float sc=Ui::textScale;
    if(hover){
        const auto tint=r.reduced(8.f);
        g.setColour(Neu::well);
        g.fillRoundedRectangle(tint, 10.f);
        g.setColour(juce::Colours::white.withAlpha(Neu::light ? 0.35f : 0.035f));
        g.fillRoundedRectangle(tint, 10.f);
    }
    const auto ink=hover ? Neu::accentInk() : Neu::textDim;
    const auto c=r.getCentre();
    // pictogrammes viewBox 24 : fenêtre (deux fenêtres + flèche vers l'extérieur) / carré « + »
    static const juce::Path windowIcon=[]{
        juce::Path p=juce::Drawable::parseSVGPath("M8 7V5a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2h-3M14 10l5-5M15 5h4v4");
        p.addRoundedRectangle(3.f, 7.f, 13.f, 12.f, 2.f);
        return p;
    }();
    static const juce::Path addIcon=[]{
        juce::Path p=juce::Drawable::parseSVGPath("M12 8v8M8 12h8");
        p.addRoundedRectangle(3.f, 3.f, 18.f, 18.f, 4.f);
        return p;
    }();
    const float k=30.f*sc/24.f;
    auto ic=addHalf ? addIcon : windowIcon;
    ic.applyTransform(juce::AffineTransform::scale(k).translated(c.x-12.f*k, c.y-34.f*sc));
    g.setColour(ink);
    g.strokePath(ic, juce::PathStrokeType(1.6f*k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool wide=r.getWidth()>=230.f*sc;
    const juce::String title=addHalf ? TR(wide ? "Ajouter un module" : "Ajouter").toUpperCase()
                                     : slotModuleName(audioProcessor.slotModule[s]).toUpperCase();
    const juce::String caption=addHalf ? TR(wide ? "la fenêtre reste ouverte" : "un autre module")
                                       : TR(wide ? "dans une fenêtre · cliquer pour le ramener" : "ramener");
    const float tw=r.getWidth()-16.f;
    g.setColour(hover ? Neu::accentInk() : Neu::text);
    g.setFont(Ui::fitFont(Ui::iceland(13.f).withExtraKerningFactor(0.12f), title, tw));
    g.drawText(title, juce::Rectangle<float>(r.getX()+8.f, c.y+2.f*sc, tw, 18.f*sc), juce::Justification::centred, false);
    g.setColour(Neu::textDim);
    g.setFont(Ui::fitFont(Ui::plain(11.f), caption, tw));
    g.drawText(caption, juce::Rectangle<float>(r.getX()+8.f, c.y+22.f*sc, tw, 16.f*sc), juce::Justification::centred, false);
}

void LeakEditor::mouseMove(const juce::MouseEvent& e)
{
    int slot=-1; bool add=false;
    if(isOwnMouseEvent(e)){
        const auto pt=e.getEventRelativeTo(this).getPosition();
        const int s=hitTestSlot(pt);
        if(isDetached(s)){ slot=s; add=detachedHalf(s, true).contains(pt); }
    }
    if(slot==hoverDetachedSlot && (slot<0 || add==hoverDetachedAdd)) return;
    if(hoverDetachedSlot>=0) repaint(rcSlot[hoverDetachedSlot]);
    hoverDetachedSlot=slot;
    hoverDetachedAdd=add;
    if(slot>=0) repaint(rcSlot[slot]);
    setMouseCursor(slot>=0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void LeakEditor::mouseExit(const juce::MouseEvent& e)
{
    if(e.eventComponent!=this || hoverDetachedSlot<0) return;   // sortie d'un module : rien à effacer
    repaint(rcSlot[hoverDetachedSlot]);
    hoverDetachedSlot=-1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

//==============================================================================
// SlotMenuLookAndFeel — menu clic droit des slots
//==============================================================================
SlotMenuLookAndFeel::SlotMenuLookAndFeel()
{
    // fond légèrement translucide : la fenêtre du menu n'est plus opaque, ses
    // coins peuvent être arrondis
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(kBg));
}

void SlotMenuLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    const auto r=juce::Rectangle<float>(0.f, 0.f, (float)width, (float)height);
    g.setColour(juce::Colour(kBg).withAlpha(1.f));
    g.fillRoundedRectangle(r, 10.f);
    g.setColour(juce::Colour(kLine));
    g.drawRoundedRectangle(r.reduced(0.5f), 10.f, 1.f);
}

juce::Font SlotMenuLookAndFeel::getPopupMenuFont()
{
    return Ui::iceland(16.f);
}

void SlotMenuLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int,
                                                    int& idealWidth, int& idealHeight)
{
    if(isSeparator){ idealWidth=50; idealHeight=juce::roundToInt(9.f*Ui::textScale); return; }
    idealHeight=juce::roundToInt(25.f*Ui::textScale);
    // icône + texte + badge + coche
    idealWidth=juce::roundToInt(juce::GlyphArrangement::getStringWidth(getPopupMenuFont(), text)+(36.f+70.f)*Ui::textScale);
}

void SlotMenuLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area,
                                                     const juce::String& name)
{
    g.setFont(Ui::iceland(12.f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(kHeader));
    g.drawText(name.toUpperCase(), area.withTrimmedLeft(10).withTrimmedTop(juce::roundToInt(4.f*Ui::textScale)),
               juce::Justification::centredLeft, false);
}

void SlotMenuLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                            bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool,
                                            const juce::String& text, const juce::String& badge,
                                            const juce::Drawable* icon, const juce::Colour* textColour)
{
    if(isSeparator){
        g.setColour(juce::Colour(kLine));
        g.fillRect(area.toFloat().withSizeKeepingCentre((float)area.getWidth()-12.f, 1.f));
        return;
    }
    const auto row=area.toFloat().reduced(4.f, 1.f);
    if((isHighlighted && isActive) || isTicked){
        g.setColour(juce::Colour(kHover));
        g.fillRoundedRectangle(row, 6.f);
    }

    const float s=Ui::textScale;
    auto r=row.reduced(8.f*s, 0.f);
    // icône du module
    const auto iconBox=r.removeFromLeft(16.f*s).withSizeKeepingCentre(15.f*s, 15.f*s);
    if(icon!=nullptr)
        icon->drawWithin(g, iconBox, juce::RectanglePlacement::centred, 1.f);
    r.removeFromLeft(9.f*s);

    // coche du module actif, puis badge GPU / 3D à droite
    if(isTicked){
        const auto tick=r.removeFromRight(14.f*s).withSizeKeepingCentre(10.f*s, 8.f*s);
        juce::Path p;
        p.startNewSubPath(tick.getX(), tick.getCentreY());
        p.lineTo(tick.getX()+tick.getWidth()*0.38f, tick.getBottom());
        p.lineTo(tick.getRight(), tick.getY());
        g.setColour(Col::accent);
        g.strokePath(p, juce::PathStrokeType(1.6f*s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        r.removeFromRight(6.f*s);
    }
    if(badge.isNotEmpty()){
        const auto bf=Ui::iceland(11.f).withExtraKerningFactor(0.08f);
        const float bw=juce::GlyphArrangement::getStringWidth(bf, badge)+12.f*s;
        const auto chip=r.removeFromRight(bw).withSizeKeepingCentre(bw, 15.f*s);
        const bool gpu=badge=="GPU";
        const bool win=badge==TR("Fenêtre").toUpperCase();   // module détaché
        g.setColour(win ? Col::accent.withAlpha(0.16f) : juce::Colour(gpu ? 0xFF2D2550 : 0xFF173A35));
        g.fillRoundedRectangle(chip, 4.f);
        g.setColour(win ? Col::accent : juce::Colour(gpu ? 0xFFC4B5FF : 0xFF7DFFD9));
        g.setFont(bf);
        g.drawText(badge, chip, juce::Justification::centred, false);
        r.removeFromRight(6.f*s);
    }

    juce::Colour tc = textColour!=nullptr ? *textColour : juce::Colour(kText);
    if(isTicked) tc=Col::accent;
    if(!isActive) tc=tc.withMultipliedAlpha(0.4f);
    g.setColour(tc);
    g.setFont(getPopupMenuFont());
    g.drawFittedText(text, r.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
}

namespace
{
    // Modules proposés dans les slots, rangés par famille (ordre du menu)
    struct SlotModuleEntry { int id; const char* name; const char* badge; };
    struct SlotModuleFamily { const char* title; std::vector<SlotModuleEntry> modules; };
    const SlotModuleFamily kSlotFamilies[] = {
        { "Spectre", {
            { 6,  "Spectrum",             ""    },
            { 10, "Spectral Density",     ""    },
            { 19, "Dot Spectrum Chroma",  "GPU" },
            { 20, "Dot Spectrum Nebula",  "GPU" } } },
        { "Spectrogramme", {
            { 14, "Spectrogram Lines",    "3D"  },
            { 15, "Spectrogram Surface",  "3D"  },
            { 25, "Spectrogram Surface GPU", "GPU" },
            { 16, "Spectrogram Terrain",  "GPU" },
            { 17, "Spectrogram Neon",     "GPU" },
            { 18, "Spectrogram Crystal",  "GPU" } } },
        { "St\xc3\xa9r\xc3\xa9o", {
            { 1,  "Vectorscope",          ""    },
            { 22, "Vectorscope Phosphor", "GPU" },
            { 23, "Vectorscope Density",  "GPU" },
            { 13, "Multiband Vectorscope",""    },
            { 8,  "Panorama",             ""    },
            { 24, "Panorama LED",         ""    },
            { 21, "Dot Panorama Chroma",  "GPU" },
            { 7,  "Multiband Correlator", ""    } } },
        { "Niveau & temps", {
            { 4,  "Loudness",             ""    },
            { 12, "Needle VU Meter",      ""    },
            { 2,  "Oscilloscope",         ""    } } },   // Transient retiré (demande du user)
    };

    juce::String slotModuleName(int id)
    {
        for(const auto& f : kSlotFamilies)
            for(const auto& e : f.modules)
                if(e.id==id) return TR(e.name);
        return TR("Vide");
    }

    // En-tête du menu : numéro du slot et module en place
    struct SlotMenuHeader : public juce::PopupMenu::CustomComponent
    {
        SlotMenuHeader(int slotNum, juce::String current)
            : juce::PopupMenu::CustomComponent(false), num(slotNum), cur(std::move(current)) {}
        void getIdealSize(int& w, int& h) override
        {
            w=juce::roundToInt(290.f*Ui::textScale);
            h=juce::roundToInt(32.f*Ui::textScale);
        }
        void paint(juce::Graphics& g) override
        {
            const auto r=getLocalBounds().toFloat().reduced(10.f, 0.f);
            g.setFont(Ui::iceland(13.f).withExtraKerningFactor(0.14f));
            g.setColour(juce::Colour(0xFF8B95A3));
            g.drawText(TR("SLOT {0}").replace("{0}", juce::String(num)), r, juce::Justification::centredLeft, false);
            g.setFont(Ui::iceland(13.f).withExtraKerningFactor(0.04f));
            g.setColour(Col::accent);
            g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x97\x8f "))+cur, r, juce::Justification::centredRight, false);
            g.setColour(juce::Colour(SlotMenuLookAndFeel::kLine));
            g.fillRect(4.f, (float)getHeight()-1.f, (float)getWidth()-8.f, 1.f);
        }
        int num;
        juce::String cur;
    };
}

// ── Icônes du menu de sélection de module (clic droit sur un slot) ──────────
// Petits pictogrammes vectoriels, un par type de module, pour identifier le
// module en un coup d'œil dans le menu (sinusoïde pour l'oscilloscope, barres
// pour le spectre, etc.)
static std::unique_ptr<juce::Drawable> makeModuleIcon(int moduleId)
{
    using M = LeakAudioProcessor::ModuleID;
    constexpr float S = 15.f;
    juce::Path p;
    bool filled = false;

    switch((M)moduleId)
    {
        case M::Vectorscope:
            p.addEllipse(1.5f,1.5f,S-3.f,S-3.f);
            p.startNewSubPath(S*0.5f,1.5f);   p.lineTo(S*0.5f,S-1.5f);
            p.startNewSubPath(1.5f,S*0.5f);   p.lineTo(S-1.5f,S*0.5f);
            break;

        case M::Oscilloscope:
            p.startNewSubPath(0.f,S*0.5f);
            p.cubicTo(S*0.18f,0.f,   S*0.32f,0.f,   S*0.5f,S*0.5f);
            p.cubicTo(S*0.68f,S,     S*0.82f,S,     S,S*0.5f);
            break;

        case M::Spectrum:
        {
            filled = true;
            const float bw=2.0f, gap=1.3f;
            const float heights[5] = {0.42f,0.7f,1.0f,0.6f,0.85f};
            float x=0.5f;
            for(float h9 : heights){
                const float h=S*h9;
                p.addRectangle(x, S-h, bw, h);
                x+=bw+gap;
            }
            break;
        }

        case M::MultibandCorr:
        {
            filled = true;
            const float bw=2.0f, gap=1.3f;
            const float amt[5] = {0.3f,-0.55f,0.7f,-0.35f,0.5f};
            const float mid=S*0.5f;
            float x=0.5f;
            for(float a : amt){
                const float h=S*0.42f*a;
                if(h>=0.f) p.addRectangle(x, mid-h, bw, h);
                else       p.addRectangle(x, mid,   bw, -h);
                x+=bw+gap;
            }
            break;
        }

        case M::Multipanorama:
        {
            filled = true;
            const float xs[5]={1.5f,4.8f,7.5f,10.2f,13.5f};
            const float ys[5]={8.5f,5.f, 7.5f, 10.5f,8.f};
            for(int i=0;i<5;++i) p.addEllipse(xs[i]-1.1f,ys[i]-1.1f,2.2f,2.2f);
            break;
        }

        case M::SpectralDensity:
            p.startNewSubPath(0.f,S*0.8f);
            p.quadraticTo(S*0.5f,-S*0.15f, S,S*0.8f);
            break;

        case M::NeedleVU:
            p.addCentredArc(S*0.5f,S*0.62f,S*0.46f,S*0.46f,0.f,
                             juce::degreesToRadians(200.f),juce::degreesToRadians(340.f),true);
            p.startNewSubPath(S*0.5f,S*0.62f); p.lineTo(S*0.8f,S*0.18f);
            break;

        case M::MultibandVectorscope:
        {
            const float cys[3]={S*0.5f,S*0.5f,S*0.5f};
            const float cxs[3]={S*0.18f,S*0.5f,S*0.82f};
            const float rr=S*0.15f;
            for(int i=0;i<3;++i){
                p.addEllipse(cxs[i]-rr,cys[i]-rr,rr*2,rr*2);
                p.startNewSubPath(cxs[i],cys[i]-rr); p.lineTo(cxs[i],cys[i]+rr);
                p.startNewSubPath(cxs[i]-rr,cys[i]); p.lineTo(cxs[i]+rr,cys[i]);
            }
            break;
        }

        case M::Spectrogram3DPeaks:
            p.startNewSubPath(0.f,S*0.85f);
            p.lineTo(S*0.28f,S*0.35f); p.lineTo(S*0.5f,S*0.6f);
            p.lineTo(S*0.72f,S*0.2f);  p.lineTo(S,S*0.85f);
            p.startNewSubPath(0.f,S*0.7f);
            p.lineTo(S*0.28f,S*0.5f); p.lineTo(S*0.5f,S*0.68f);
            p.lineTo(S*0.72f,S*0.42f);  p.lineTo(S,S*0.7f);
            break;

        case M::Spectrogram3DSurface:
            filled = true;
            p.startNewSubPath(0.f,S*0.85f);
            p.lineTo(S*0.28f,S*0.35f); p.lineTo(S*0.5f,S*0.6f);
            p.lineTo(S*0.72f,S*0.2f);  p.lineTo(S,S*0.85f);
            p.lineTo(S,S); p.lineTo(0.f,S); p.closeSubPath();
            break;

        // GPU : relief en perspective (losange de sol + crêtes)
        case M::GpuSpectroTerrain:
            filled = true;
            p.startNewSubPath(0.f,S*0.78f);
            p.lineTo(S*0.22f,S*0.42f); p.lineTo(S*0.38f,S*0.58f);
            p.lineTo(S*0.6f,S*0.12f);  p.lineTo(S*0.8f,S*0.5f);
            p.lineTo(S,S*0.62f); p.lineTo(S*0.72f,S); p.lineTo(S*0.12f,S);
            p.closeSubPath();
            break;

        case M::GpuSpectroNeon:
            for(int k=0;k<3;++k){
                const float o=(float)k*S*0.2f;
                p.startNewSubPath(o*0.6f,S*0.92f-o);
                p.lineTo(o*0.6f+S*0.25f,S*0.5f-o*0.5f); p.lineTo(o*0.6f+S*0.42f,S*0.72f-o);
                p.lineTo(o*0.6f+S*0.6f,S*0.3f-o*0.4f);  p.lineTo(S-o*0.2f,S*0.92f-o);
            }
            break;

        case M::GpuSpectroCrystal:
            p.startNewSubPath(0.f,S*0.75f);
            p.lineTo(S*0.3f,S*0.25f); p.lineTo(S*0.5f,S*0.55f);
            p.lineTo(S*0.72f,S*0.1f); p.lineTo(S,S*0.75f);
            p.startNewSubPath(S*0.3f,S*0.25f); p.lineTo(S*0.3f,S*0.75f);
            p.startNewSubPath(S*0.5f,S*0.55f); p.lineTo(S*0.5f,S*0.75f);
            p.startNewSubPath(S*0.72f,S*0.1f); p.lineTo(S*0.72f,S*0.75f);
            p.startNewSubPath(0.f,S*0.75f);    p.lineTo(S,S*0.75f);
            p.startNewSubPath(S*0.12f,S*0.95f); p.lineTo(S*0.88f,S*0.95f);   // reflet
            break;

        // Dot Spectrum : nuage de points en dôme (Nebula : points plus gros + enveloppe)
        case M::DotSpectrumChroma:
        case M::DotSpectrumNebula:
        {
            filled = true;
            const bool neb = (M)moduleId==M::DotSpectrumNebula;
            juce::Random r(7);
            for(int k=0;k<(neb ? 9 : 16);++k){
                const float x=r.nextFloat();
                const float top=S*(0.95f-0.75f*std::sin(x*juce::MathConstants<float>::pi));
                const float y=top+(S*0.95f-top)*r.nextFloat();
                const float d=neb ? S*0.15f : S*0.09f;
                p.addEllipse(x*S*0.9f+S*0.05f-d*0.5f, y-d*0.5f, d, d);
            }
            if(neb)
                for(int k=0;k<8;++k){
                    const float x0=(float)k/8.f, x1=(float)(k+1)/8.f;
                    p.addLineSegment({ x0*S, S*(0.85f-0.75f*std::sin(x0*juce::MathConstants<float>::pi)),
                                       x1*S, S*(0.85f-0.75f*std::sin(x1*juce::MathConstants<float>::pi)) }, 1.2f);
                }
            break;
        }

        // Vectorscopes GPU : cercle + trace (Phosphor) ou tache pleine (Density)
        case M::VectorscopePhosphor:
            p.addEllipse(0.5f, 0.5f, S-1.f, S-1.f);
            p.startNewSubPath(S*0.5f, S*0.18f);
            p.cubicTo(S*0.95f, S*0.35f, S*0.05f, S*0.62f, S*0.5f, S*0.85f);
            break;

        case M::VectorscopeDensity:
            filled = true;
            p.addEllipse(S*0.3f, S*0.12f, S*0.4f, S*0.76f);
            p.addEllipse(0.f, 0.f, S, S);
            p.addEllipse(1.2f, 1.2f, S-2.4f, S-2.4f);   // anneau (règle pair-impair)
            p.setUsingNonZeroWinding(false);
            break;

        // Panorama LED : colonnes de petites LED, une allumée par colonne
        case M::PanoramaLed:
        {
            filled = true;
            const float lit[4] = { 0.5f, 0.3f, 0.62f, 0.42f };
            for(int c=0;c<4;++c)
                for(int k=0;k<5;++k){
                    const float x=(float)c*S*0.26f+S*0.04f, y=(float)k*S*0.2f+S*0.04f;
                    const bool on=std::abs((float)k*0.25f-lit[c])<0.13f;
                    p.addRoundedRectangle(x, y, S*0.18f, on ? S*0.14f : S*0.05f, 0.6f);
                }
            break;
        }

        // Dot Panorama : points répartis autour de la ligne centrale (L / R)
        case M::DotPanoramaChroma:
        {
            filled = true;
            p.addRectangle(0.f, S*0.5f-0.4f, S, 0.8f);
            juce::Random r(11);
            for(int k=0;k<14;++k){
                const float x=r.nextFloat()*S*0.9f+S*0.05f;
                const float y=S*0.5f+(r.nextFloat()+r.nextFloat()-1.f)*S*0.42f;
                p.addEllipse(x-S*0.045f, y-S*0.045f, S*0.09f, S*0.09f);
            }
            break;
        }

        default:
            return nullptr;
    }

    auto dp = std::make_unique<juce::DrawablePath>();
    dp->setPath(p);
    if(filled){
        dp->setFill(juce::FillType(Col::accent.withAlpha(0.85f)));
    } else {
        dp->setFill(juce::FillType(juce::Colours::transparentBlack));
        dp->setStrokeFill(juce::FillType(Col::accent.withAlpha(0.85f)));
        dp->setStrokeThickness(1.4f);
    }
    return dp;
}

void LeakEditor::showModuleMenu(int slot, juce::Point<int> screenPos)
{
    // Design A "liste sectionnée" : en-tête (slot + module en place), modules
    // rangés par famille avec badges GPU / 3D (ou FENÊTRE pour un module
    // détaché, que le choisir ramène ici), "Vider le slot" à part en bas.
    juce::PopupMenu m;
    m.setLookAndFeel(&slotMenuLaf);
    // Numérotation séquentielle des slots configurables (1 à 5) — le slot fixe F
    // (VU master) ne compte pas dans la numérotation.
    static const int configurableSlots[] = {0,1,2,3,4};
    int displayNum = slot+1;
    for(int i=0;i<5;++i) if(configurableSlots[i]==slot) { displayNum=i+1; break; }
    // emplacement quitté par son module (dans une fenêtre) : vide pour le menu
    const int cur = isDetached(slot) ? 0 : audioProcessor.slotModule[slot];

    {
        juce::PopupMenu::Item header;
        header.customComponent = new SlotMenuHeader(displayNum, slotModuleName(cur));
        header.isEnabled = false;
        m.addItem(std::move(header));
    }
    for(const auto& family : kSlotFamilies){
        m.addSectionHeader(TR(family.title));
        for(const auto& e : family.modules){
            juce::PopupMenu::Item item;
            item.text = TR(e.name);
            item.itemID = e.id+1;
            item.isTicked = (cur==e.id);
            item.image = makeModuleIcon(e.id);
            // dessiné en badge par SlotMenuLookAndFeel
            item.shortcutKeyDescription = floatingFor(e.id)!=nullptr ? TR("Fenêtre").toUpperCase() : juce::String(e.badge);
            m.addItem(std::move(item));
        }
    }
    m.addSeparator();
    {
        juce::PopupMenu::Item empty;
        empty.text = TR("Vider le slot");
        empty.itemID = 1;   // module 0 = vide
        empty.isEnabled = cur!=0;
        empty.colour = juce::Colour(SlotMenuLookAndFeel::kDanger);
        juce::Path x;
        x.startNewSubPath(3.f, 3.f); x.lineTo(12.f, 12.f);
        x.startNewSubPath(12.f, 3.f); x.lineTo(3.f, 12.f);
        auto icon = std::make_unique<juce::DrawablePath>();
        icon->setPath(x);
        icon->setFill(juce::FillType(juce::Colours::transparentBlack));
        icon->setStrokeFill(juce::FillType(juce::Colour(SlotMenuLookAndFeel::kDanger)));
        icon->setStrokeThickness(1.6f);
        empty.image = std::move(icon);
        m.addItem(std::move(empty));
    }

    PopupFocus::prepare();   // clavier laissé à Ableton : sinon le menu se referme
    m.showMenuAsync(
        juce::PopupMenu::Options()
            .withTargetScreenArea({screenPos.x, screenPos.y, 1, 1}),
        [this, slot](int result){
            if(result <= 0) return;
            const int newId = result - 1;
            // module dans une fenêtre : sa fenêtre se ferme, il vient ici
            if(newId != 0) dockFloating(newId);
            // Si le module demandé est déjà dans un autre slot → le vider
            for(int s=0;s<LeakAudioProcessor::NUM_SLOTS;++s)
                if(s!=slot && audioProcessor.slotModule[s]==newId)
                    audioProcessor.slotModule[s]=0;
            audioProcessor.slotModule[slot]=newId;
            updateLayout();
            repaint();
        });
}

// ── Layout ────────────────────────────────────────────────────────────────────
void LeakEditor::updateLayout()
{
    // Toute la mise en page se fait dans la plaque, en retrait du cadre nature.
    // Marges et écarts laissent la place aux cartes neumorphiques (saillie de
    // kCardPad + ombres) autour de chaque écran.
    const auto plate=getLocalBounds();
    const int W=plate.getWidth(), H=plate.getHeight();
    // v1.74 : la bande du bas rend 16 px de marges vides à l'en-tête ; les
    // modules gardent la même hauteur
    constexpr int kBandToHeader=16;
    const int titleH=juce::jlimit(36,52,H*8/100)+kBandToHeader;
    // bande du bas fixe (design de Sunny, v1.73) : une rangée de contrôles,
    // plus de tiroir repliable ; suit la taille de texte
    const int ctrlH =juce::roundToInt((float)juce::jlimit(30,36,H*5/100)*Ui::textScale);
    const int bandSpace=ctrlH+26-kBandToHeader;   // du bas de l'écran E au bas de la plaque
    const int M=18, G=20;
    const int topH  =juce::jlimit(140,300,H*34/100);
    const int innerW=W-2*M;
    const int topY  =titleH+M;

    // Le VU Meter (slot F) est maintenant une colonne pleine hauteur à droite,
    // à droite du LUFS (slot D) — les slots A B C D (haut) et E (bas)
    // partagent donc une largeur réduite (topInnerW) pour lui laisser la place.
    const int vuW       =juce::jlimit(120, 190, W*12/100);   // section master élargie (afficheurs lisibles)
    const int topInnerW =innerW-vuW-G;

    const int vecW  =juce::jmin(topH, topInnerW*17/100);
    const int loudW =juce::jlimit(150, 240, topInnerW*16/100);
    const int remW  =topInnerW-vecW-loudW-3*G;
    const int oscW  =remW/2;                 // slots B et C de largeur égale (comme la maquette)
    const int trnW  =remW-oscW;

    int x=M;
    rcSlot[0]={x,topY,vecW, topH}; x+=vecW+G;   // slot A
    rcSlot[1]={x,topY,oscW, topH}; x+=oscW+G;   // slot B
    rcSlot[2]={x,topY,trnW, topH}; x+=trnW+G;   // slot C
    rcSlot[3]={x,topY,loudW,topH};               // slot D (LUFS)

    const int specY   =topY+topH+G;
    const int specH   =juce::jmax(40, H-bandSpace-specY);
    // bande centrée sur la hauteur entre la carte de l'écran E (qui déborde de
    // kCardPad) et le bas de la plaque
    const int ctrlYtop=juce::roundToInt(((float)(specY+specH)+kCardPad+(float)H-(float)ctrlH)*0.5f);
    rcSlot[4]={M,            specY, topInnerW,        specH};          // slot E (configurable, prend toute la largeur en bas)
    // slot F (VU Meter, pleine hauteur, à droite du LUFS, aussi bas que le slot E) ;
    // l'interrupteur de style de la section master est au bout de la bande,
    // sous la colonne du VU : le VU garde toute la hauteur
    const int mtH=juce::jlimit(20,26,(titleH-kBandToHeader)*55/100), mtW=juce::roundToInt((float)mtH*2.3f);
    rcSlot[5]={M+innerW-vuW, topY,  vuW, topH+G+specH};
    juce::Rectangle<int> rcMasterToggle(rcSlot[5].getCentreX()-mtW/2, ctrlYtop+(ctrlH-mtH)/2, mtW, mtH);

    rcTitle={0,0,W,titleH};
    rcCtrl ={M,ctrlYtop,juce::jmax(100, rcMasterToggle.getX()-14-M),ctrlH};

    for(auto& r : rcSlot) r.translate(plate.getX(),plate.getY());
    rcMasterToggle  .translate(plate.getX(),plate.getY());
    masterStyleToggle.setBounds(rcMasterToggle);
    rcTitle         .translate(plate.getX(),plate.getY());
    rcCtrl          .translate(plate.getX(),plate.getY());

    // Masquer tous les modules configurables
    juce::Component* const allModules[] = {
        &vectorscope, &oscilloscope,
        &spectrum, &corrDisplay, &panoDisplay, &panoLed,
        &spectralDensity, &needleVu, &mbVectorscope,
        &spectro3dPeaks, &spectro3dSurface,
        &gpuTerrain, &gpuNeon, &gpuCrystal, &gpuSurface, &gpuDotChroma, &gpuDotNebula, &gpuDotPano,
        &gpuVecPhosphor, &gpuVecDensity, &loudnessPanel
    };
    // Slots configurables (clic droit) : A, B, C, D, E → indices 0 à 4
    static const int configurableSlots[] = {0,1,2,3,4};
    // seuls les modules non utilisés sont masqués : un module en place ne doit
    // pas être caché/réaffiché à chaque mise en page (animation de repli,
    // contexte OpenGL des spectrogrammes GPU)
    for(auto* c : allModules){
        bool used=false;
        for(int s : configurableSlots) used = used || getComponentForModule(audioProcessor.slotModule[s])==c;
        for(const auto& f : floating)  used = used || getComponentForModule(f.module)==c;   // vit dans sa fenêtre
        if(!used) c->setVisible(false);
    }
    for(int s : configurableSlots){
        if(isDetached(s)) continue;   // le module vit dans sa propre fenêtre
        if(auto* comp=getComponentForModule(audioProcessor.slotModule[s])){
            comp->setBounds(rcSlot[s]);
            comp->setVisible(true);
        }
    }

    // Slot fixe : F = VU Meter (section master)
    vuMeter      .setBounds(rcSlot[5]); vuMeter      .setVisible(true);

    topBar      .setBounds(rcTitle);
    controls       .setBounds(rcCtrl);   // bande du bas (fixe)
    overlayLayer   .setBounds(getLocalBounds());
    aboutOverlay   .setBounds(getLocalBounds());
    if(optionsWindow==nullptr)   // sinon le panneau suit sa propre fenêtre
        optionsOverlay.setBounds(getLocalBounds());
    licenseOverlay .setBounds(getLocalBounds());

    applyFrameRate(audioProcessor.uiFps);   // modules cachés à l'arrêt

    layoutDirty=true;   // cartes et creux suivent les slots : caches à refaire
    repaint();
}

void LeakEditor::applyPreset(int idx)
{
    // Sound Design : preset usine complet (disposition, réglages des modules,
    // apparence), relu en place comme un preset utilisateur
    if(idx==2 || idx==3){
        if(idx==2 ? audioProcessor.loadFactorySoundDesign() : audioProcessor.loadFactoryTV()) reloadFromProcessor();
        refreshPresetList();
        return;
    }
    using M = LeakAudioProcessor::ModuleID;
    // Templates usine : A, B, C, E varient ; D reprend le Loudness, F (VU) est fixe.
    static const int mixing[4]      = { (int)M::Vectorscope, (int)M::MultibandCorr,   (int)M::Multipanorama,  (int)M::Spectrum };
    static const int mastering[4]   = { (int)M::Vectorscope, (int)M::MultibandCorr,   (int)M::NeedleVU,       (int)M::Spectrum };
    static const int slotIdx[4]     = { 0, 1, 2, 4 };

    const int* preset = idx==0 ? mixing : mastering;
    for(int i=0;i<4;++i)
        audioProcessor.slotModule[slotIdx[i]] = preset[i];
    audioProcessor.slotModule[3] = (int)M::Loudness;

    audioProcessor.setNumActiveBands(idx==1 ? 62 : 31);
    spectrum.setActiveBands(audioProcessor.getNumActiveBands());

    updateLayout();
    repaint();
}

void LeakEditor::applyFrameRate(int hz)
{
    // Modules à l'écran : cadence demandée ; modules cachés : timer arrêté
    // (rien à calculer ni à peindre). Seuls les changements sont appliqués :
    // relancer un timer remet son compte à zéro (la mise en page est refaite
    // à chaque image pendant l'animation de repli).
    auto rate=[&](auto& m){
        const int want=m.isVisible() ? hz : 0;
        const auto it=appliedFrameRate.find(&m);
        if(it!=appliedFrameRate.end() && it->second==want) return;
        appliedFrameRate[&m]=want;
        m.setFrameRateHz(want);
    };
    rate(vectorscope);   // oscilloscope : GPU, cadence via applyGraphics
    rate(spectrum);     rate(corrDisplay);  rate(panoDisplay);  rate(panoLed);
    rate(spectralDensity); rate(needleVu);  rate(mbVectorscope);
    rate(spectro3dPeaks);  rate(spectro3dSurface);
}

// Options > Image / mode CPU léger : cadence des analyses + réglages GPU
void LeakEditor::applyGraphics()
{
    audioProcessor.lightCpuMode = audioProcessor.uiFps<=30;
    applyFrameRate(audioProcessor.uiFps);
    for(auto* g : gpuModules())
        g->setGraphics(audioProcessor.gpuMsaa, audioProcessor.gpuVsync, audioProcessor.uiFps);
}

// Licence : pastille DÉMO, feuille imposée tant que Merlin n'est ni activé ni en démo
void LeakEditor::updateLicenseUi()
{
    using S=LicenseManager::Status;
    const auto s=license->getStatus();
    topBar.setDemoBadge(s==S::trial ? TR("DÉMO · {0} JOURS").replace("{0}", juce::String(license->getTrialDaysLeft()))
                                    : juce::String());
    if(! license->isUsable() && s!=S::activating && ! licenseOverlay.isVisible())
        openLicence();
}

void LeakEditor::openLicence()
{
    aboutOverlay.setVisible(false);
    closeOptions();
    OptionsUi::lightTheme()=audioProcessor.lightTint;   // feuille au thème de Merlin
    licenseOverlay.open();
}

void LeakEditor::updateGpuSuspension()
{
    // Options dans leur propre fenêtre (appli) : rien ne recouvre les modules
    const bool optionsOver=optionsOverlay.isVisible() && optionsWindow==nullptr;
    const bool covered=aboutOverlay.isVisible() || optionsOver
                       || licenseOverlay.isVisible() || windowSwaps>0;
    for(auto* g : gpuModules())
        g->setSuspended(covered);
}

void LeakEditor::resized()
{
    // pas pendant la construction : applyTextScale() relance la mise en page
    // alors que l'éditeur fait encore 0×0, ce qui effaçait la taille mémorisée
    // (l'éditeur rouvrait toujours en 1024×576)
    if(getWidth()>0 && getHeight()>0){
        audioProcessor.editorWidth  = getWidth();
        audioProcessor.editorHeight = getHeight();
    }
    updateLayout();
}

//==============================================================================
// Templates : châssis neumorphique (sombre / clair) + décor nature éventuel
//==============================================================================
void LeakEditor::refreshChassis()
{
    layoutDirty=true;   // force la reconstruction des caches du châssis
    updateLayout();
    repaint();
}

// Textes agrandis : les polices sont recréées à chaque paint avec Ui::textScale ;
// on relance resized() partout pour que les caches qui en dépendent (grilles,
// étiquettes du Spectrum, labels…) se reconstruisent, puis on repeint.
void LeakEditor::applyTextScale()
{
    Ui::textScale = audioProcessor.largeText ? Ui::kLargeTextScale : 1.0f;
    std::function<void(juce::Component&)> relayout = [&](juce::Component& c){
        for(auto* child : c.getChildren()) relayout(*child);
        c.resized();
    };
    relayout(*this);
    layoutDirty=true;
    repaint();
}

// Langue : tous les textes passent par TR() au moment de peindre ; il suffit de
// reposer ceux qui sont fixés une fois (infobulles, menus, boutons, pages des
// Options), de relancer la mise en page (caches de libellés, châssis) puis de
// tout repeindre — modules OpenGL compris, sans les recréer.
void LeakEditor::applyLanguage()
{
    Lang::set(audioProcessor.language);
    topBar.retranslate();
    controls.presets.retranslate();
    licenseOverlay.refresh();
    updateLicenseUi();   // pastille DÉMO
    masterStyleToggle.setTooltip(TR("Master meter: Instrument / Console"));
    Lang::retranslateTree(*this);   // boutons à texte fixe ("RESET", "LINE"…)
    optionsOverlay.retranslate();
    if(optionsOverlay.isVisible()) optionsOverlay.setState(optionsState());
    applyTextScale();
    std::function<void(juce::Component&)> repaintAll = [&](juce::Component& c){
        c.repaint();
        for(auto* child : c.getChildren()) repaintAll(*child);
    };
    repaintAll(*this);
}

// palette des modules depuis les couleurs mémorisées (écrans Papier compris)
void LeakEditor::applyColours()
{
    Theme::applyCustom({audioProcessor.customAccentColor, audioProcessor.customGridColor,
                         audioProcessor.customBgColor, audioProcessor.customTextColor,
                         audioProcessor.customSideColor, audioProcessor.customSpectroLowColor,
                         audioProcessor.customSpectroMidColor, audioProcessor.customSpectroHighColor});
}

void LeakEditor::selectAccentPalette(int idx)
{
    idx = juce::jlimit(0, Theme::NUM_PALETTES-1, idx);
    audioProcessor.customAccentColor = Theme::palettes[idx].accent;
    audioProcessor.customGridColor   = Theme::palettes[idx].grid;
    applyColours();
    repaint();
}

void LeakEditor::setColourRole(int role, juce::Colour c)
{
    switch(role){
        case 0: audioProcessor.customAccentColor = c.getARGB(); break;
        case 1: audioProcessor.customGridColor   = c.getARGB(); break;
        case 2: audioProcessor.customBgColor     = c.getARGB(); break;
        case 3: audioProcessor.customTextColor   = c.getARGB(); break;
        case 4: audioProcessor.customSideColor   = c.getARGB(); break;
        case 5: audioProcessor.customSpectroLowColor  = c.getARGB(); break;
        case 6: audioProcessor.customSpectroMidColor  = c.getARGB(); break;
        case 7: audioProcessor.customSpectroHighColor = c.getARGB(); break;
        default: return;
    }
    applyColours();
    repaint();
}

void LeakEditor::resetColours()
{
    audioProcessor.customAccentColor = 0xFF00FFB2;
    audioProcessor.customGridColor   = 0xFF0B3030;
    audioProcessor.customBgColor     = 0xFF030A0A;
    audioProcessor.customTextColor   = 0xFFC8FFE8;
    audioProcessor.customSideColor   = 0xFFB266FF;
    audioProcessor.customSpectroLowColor  = 0xFF333333;
    audioProcessor.customSpectroMidColor  = 0xFFB0B0B0;
    audioProcessor.customSpectroHighColor = 0xFF470000;
    applyColours();
    repaint();
}

void LeakEditor::resetLayout()
{
    static const int def[LeakAudioProcessor::NUM_SLOTS] = {1,7,12,4,6,5};
    for(int s=0;s<LeakAudioProcessor::NUM_SLOTS;++s) audioProcessor.slotModule[s]=def[s];
    updateLayout();
    repaint();
}

void LeakEditor::applyLook()
{
    Neu::apply(audioProcessor.lightTint, audioProcessor.highContrast, audioProcessor.plateTint==1, audioProcessor.lightScreens!=0);
    // écrans Papier ou noirs : la palette des modules suit le thème
    Ui::titleStyle = audioProcessor.titleStyle;
    Ui::corrStyle  = audioProcessor.corrStyle;
    applyColours();
    layoutDirty=true;
    updateLayout();
    repaint();
    // modules OpenGL : leur calque 2D (coins de la lunette) n'est refait que
    // s'ils sont eux-mêmes invalidés
    for(auto* gm : gpuModules()) gm->repaint();
    // barres de fenêtre : couleur de la plaque
    if(auto* top=getTopLevelComponent()) top->repaint();
    for(auto& f : floating) f.window->repaint();
    if(optionsWindow!=nullptr) optionsWindow->repaint();
    if(licenseOverlay.isVisible()){ OptionsUi::lightTheme()=audioProcessor.lightTint; licenseOverlay.refresh(); }
}

// Châssis mis en cache à la résolution physique : rien de ce qui suit n'est
// redessiné à chaque image des modules.
// carte en saillie + lunette sombre autour d'un écran
static void drawScreenCard(juce::Graphics& g, juce::Rectangle<float> r)
{
    Neu::drawRaised(g, r.expanded(kCardPad), kScreenRadius+kCardPad, 4.f);
    Neu::drawBezel(g, r);
}

// coins arrondis + bord de la vitre d'un écran (au-dessus du module)
static void drawScreenCaps(juce::Graphics& g, juce::Rectangle<float> r)
{
    Neu::drawScreenGlass(g, r);
}

void LeakEditor::rebuildChassisCaches(float pxScale)
{
    const int W=getWidth(), H=getHeight();
    const int pw=juce::jmax(1,juce::roundToInt((float)W*pxScale));
    const int ph=juce::jmax(1,juce::roundToInt((float)H*pxScale));
    cacheScale=pxScale; cacheW=W; cacheH=H; layoutDirty=false;

    const auto plateR=getLocalBounds().toFloat();
    juce::Path plateP; plateP.addRectangle(plateR);

    juce::Array<juce::Rectangle<float>> screens;   // écrans des modules
    for(int s=0;s<LeakAudioProcessor::NUM_SLOTS;++s)
        if(!rcSlot[s].isEmpty() && !isDetached(s)) screens.add(rcSlot[s].toFloat());

    // ── 1) Châssis opaque : plaque, cartes en saillie, creux ───────────────
    chassisCache=juce::Image(juce::Image::RGB,pw,ph,false);
    {
        juce::Graphics g(chassisCache);
        g.addTransform(juce::AffineTransform::scale(pxScale));
        // plaque : surface neumorphique, lumière venant du haut-gauche
        g.setGradientFill(Neu::plateFace(plateR)); g.fillPath(plateP);

        // filet gravé sous la barre de titre
        {
            const float ly=(float)rcTitle.getBottom()-1.f;
            const float lx=plateR.getX()+20.f, lw=plateR.getWidth()-40.f;
            g.setColour(Neu::shadowDark.withMultipliedAlpha(0.45f)); g.fillRect(lx,ly,lw,1.f);
            g.setColour(Neu::shadowLight);                            g.fillRect(lx,ly+1.f,lw,1.f);
        }

        // une carte en saillie par écran, l'écran étant posé dans un creux
        for(int s=0;s<LeakAudioProcessor::NUM_SLOTS;++s)
            if(!rcSlot[s].isEmpty()){
                if(isDetached(s)) drawDetachedSlot(g, s);   // module dans sa fenêtre : creux cliquable
                else              drawScreenCard(g, rcSlot[s].toFloat());
            }

        // slots configurables vides : un "+" en relief au centre (clic = menu des modules)
        for(int s : { 0, 1, 2, 3, 4 })
            drawEmptySlotPlus(g, s);
    }

    // ── 2) Calque au-dessus des modules : bords des écrans ──────────────────
    overlayCache=juce::Image(juce::Image::ARGB,pw,ph,true);
    {
        juce::Graphics og(overlayCache);
        og.addTransform(juce::AffineTransform::scale(pxScale));
        for(const auto& r : screens)
            drawScreenCaps(og, r);   // coins arrondis (les modules peignent des rectangles)
    }

    // zones utiles du calque : pourtour des écrans
    overlayRegions.clear();
    const int e=(int)kScreenRadius+2;
    for(const auto& r : screens){
        const auto ri=r.getSmallestIntegerContainer().expanded(1);
        overlayRegions.add(ri.withHeight(e));  overlayRegions.add(ri.withTop(ri.getBottom()-e));
        overlayRegions.add(ri.withWidth(e));   overlayRegions.add(ri.withLeft(ri.getRight()-e));
    }
}

void LeakEditor::paint(juce::Graphics& g)
{
    const float pxScale=g.getInternalContext().getPhysicalPixelScaleFactor();
    if(layoutDirty || !chassisCache.isValid() || cacheScale!=pxScale
       || cacheW!=getWidth() || cacheH!=getHeight())
        rebuildChassisCaches(pxScale);
    g.drawImage(chassisCache,getLocalBounds().toFloat());
    // moitié survolée d'un creux partagé, par-dessus le châssis en cache
    if(isDetached(hoverDetachedSlot)) drawDetachedHalf(g, hoverDetachedSlot, hoverDetachedAdd, true);
    if(isDetached(magnetSlot))        drawDockMagnet(g, magnetSlot);
}

// Calque au-dessus des modules (OverlayLayer) : seules les zones utiles sont
// recomposées, le centre des écrans n'est jamais touché.
void LeakEditor::paintChassisOverlay(juce::Graphics& g)
{
    if(!overlayCache.isValid()) return;
    g.reduceClipRegion(overlayRegions);
    g.drawImage(overlayCache,getLocalBounds().toFloat());
}

// slot configurable vide : "+" en relief au centre
void LeakEditor::drawEmptySlotPlus(juce::Graphics& g, int s)
{
    if(rcSlot[s].isEmpty() || getComponentForModule(audioProcessor.slotModule[s]) != nullptr) return;
    const auto r=rcSlot[s].toFloat();
    const float d=juce::jlimit(22.f, 40.f, juce::jmin(r.getWidth(), r.getHeight())*0.22f);
    const auto plus=juce::Rectangle<float>(d, d).withCentre(r.getCentre());
    Neu::drawRaised(g, plus, d*0.5f, 2.f);
    g.setColour(Neu::accentInk());
    const float bar=d*0.42f, th=juce::jmax(2.f, d*0.08f);
    g.fillRoundedRectangle(juce::Rectangle<float>(bar, th).withCentre(plus.getCentre()), th*0.5f);
    g.fillRoundedRectangle(juce::Rectangle<float>(th, bar).withCentre(plus.getCentre()), th*0.5f);
}

