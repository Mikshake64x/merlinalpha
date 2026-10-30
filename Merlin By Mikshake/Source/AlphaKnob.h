#pragma once
#include <JuceHeader.h>
#include <map>

/**
    AlphaKnob -- knob neumorphique de Sevenpack Alpha, repris tel quel (v1.73.3,
    demande du user : « reprendre les knobs du Sevenpack Alpha »).
    Corps PLAT de la couleur de la carte, relief sur le bord (chanfrein clair en
    haut a gauche, sombre en bas a droite), halo clair, couronne de 72 traits
    autour, point bleu creuse. Le corps est calcule par pixel (sur-echantillonne
    3x3) une fois par taille et par theme (cache partage), seul le point bouge.
    Distances en fractions du rayon R ; le dessin deborde jusqu'a kExtent * R.
*/
namespace AlphaKnob
{
    constexpr float kExtent = 1.30f;   // couronne de traits jusqu'a ~1.29 R
    constexpr float kDotRadius = 0.672f;
    constexpr float kDotSize = 0.0778f;

    inline float smooth (float a, float b, float x) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a));
        return t * t * (3.0f - 2.0f * t);
    }
    inline float bump (float in0, float peak, float out1, float x) noexcept
    {
        return x < peak ? smooth (in0, peak, x) : 1.0f - smooth (peak, out1, x);
    }

    struct Theme
    {
        float lightScale, darkScale, tickAlpha;
        juce::Colour lightColour, shadowColour;
        // Contraste renforce (variante C "silhouette" choisie sur l'apercu) :
        // filet au bord, face teintee. Le CORPS du knob seulement -- les
        // traits de la couronne restent ceux du mode normal (demande
        // explicite : "le mode contraste c'est juste les knobs, pas les
        // marqueurs autour des knobs").
        float edge = 0.0f, face = 0.0f;
        bool contrastOnShadow = false; // filet/face dans le calque sombre (theme clair) ou clair (theme sombre)
    };
    inline Theme themeFor (bool light, bool highContrast = false) noexcept
    {
        Theme t = light ? Theme { 1.0f, 1.0f, 0.509f, juce::Colours::white, juce::Colour (0xff738c9e) }
                        // Theme sombre : meme geometrie, lumiere tres retenue, ombre noire.
                        : Theme { 0.18f, 1.25f, 0.13f, juce::Colours::white, juce::Colours::black };
        if (highContrast)
        {
            if (light) { t.darkScale = 2.0f; t.edge = 0.7f; t.face = 0.2f; t.contrastOnShadow = true; }
            else       { t.lightScale = 0.3f; t.darkScale = 1.8f; t.edge = 0.4f; t.face = 0.1f; }
        }
        return t;
    }

    // Calques au point (dx, dy) du centre, R en pixels : kL clair, kD sombre.
    inline void layers (float dx, float dy, float R, const Theme& t, float& kL, float& kD) noexcept
    {
        using MC = juce::MathConstants<float>;
        const float r = std::sqrt (dx * dx + dy * dy), rho = r / R;
        const float th = std::atan2 (dy, dx);
        const float s = std::cos (th - MC::pi * 0.25f);       // +1 en bas a droite, -1 en haut a gauche
        const float sp = juce::jmax (0.0f, s), sn = juce::jmax (0.0f, -s);

        kD = (0.505f * std::pow (sp, 1.62f) + 0.021f) * bump (0.851f, 0.992f, 1.265f, rho);
        const float kR = 0.946f * std::pow (sn, 1.46f) * bump (0.868f, 0.989f, 1.239f, rho);
        const float kG = 0.225f * std::pow (sn, 0.789f) * bump (0.747f, 1.102f, 1.315f, rho);

        float kT = 0.0f;
        if (rho > 1.038f && rho < 1.343f)
        {
            const float step = MC::twoPi / 72.0f;                 // un trait a midi
            float a = std::fmod (th + MC::halfPi, step);
            if (a < 0.0f) a += step;
            if (a > step * 0.5f) a -= step;
            const float cov = juce::jlimit (0.0f, 1.0f, 0.5f + (juce::jmax (0.9f, 0.0497f * R) * 0.5f - std::abs (a) * r) * 1.5f);
            kT = t.tickAlpha * cov * smooth (1.068f, 1.108f, rho) * (1.0f - smooth (1.273f, 1.313f, rho));
        }

        // Contraste renforce : filet au bord (tout le tour) + face teintee.
        const float kE = t.edge * std::exp (-std::pow ((rho - 0.99f) / 0.035f, 2.0f));
        const float kF = t.face * (1.0f - smooth (0.95f, 1.0f, rho));
        const float extra = 1.0f - (1.0f - kE) * (1.0f - kF);
        const float tickL = juce::jmin (1.0f, kT); // traits : toujours dans le calque clair, comme en mode normal

        kL = 1.0f - (1.0f - kR * t.lightScale) * (1.0f - kG * t.lightScale) * (1.0f - tickL) * (1.0f - (t.contrastOnShadow ? 0.0f : extra));
        kD = 1.0f - (1.0f - juce::jmin (1.0f, kD * t.darkScale)) * (1.0f - (t.contrastOnShadow ? extra : 0.0f));
    }

    // Corps du knob (sans le point). R en pixels PHYSIQUES ; centre au
    // milieu de l'image.
    inline juce::Image renderBody (float R, bool lightTheme, bool highContrast = false)
    {
        const auto t = themeFor (lightTheme, highContrast);
        const int size = 2 * (int) std::ceil (R * kExtent) + 2;
        const float c = (float) size * 0.5f;
        juce::Image img (juce::Image::ARGB, size, size, true);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);

        const float lr = t.lightColour.getFloatRed(), lg = t.lightColour.getFloatGreen(), lb = t.lightColour.getFloatBlue();
        const float sr = t.shadowColour.getFloatRed(), sg = t.shadowColour.getFloatGreen(), sb = t.shadowColour.getFloatBlue();
        constexpr int N = 3;

        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                float A = 0.0f, pr = 0.0f, pg = 0.0f, pb = 0.0f;   // premultiplie
                for (int sy = 0; sy < N; ++sy)
                    for (int sx = 0; sx < N; ++sx)
                    {
                        float kL, kD;
                        layers ((float) x + ((float) sx + 0.5f) / N - c, (float) y + ((float) sy + 0.5f) / N - c, R, t, kL, kD);
                        // clair puis sombre par-dessus = un seul pixel premultiplie
                        const float wl = kL * (1.0f - kD);
                        A  += wl + kD;
                        pr += lr * wl + sr * kD;
                        pg += lg * wl + sg * kD;
                        pb += lb * wl + sb * kD;
                    }
                constexpr float inv = 255.0f / (float) (N * N);
                if (A * inv < 0.5f)
                    continue;
                reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y))
                    ->setARGB ((juce::uint8) juce::roundToInt (A * inv), (juce::uint8) juce::roundToInt (pr * inv),
                               (juce::uint8) juce::roundToInt (pg * inv), (juce::uint8) juce::roundToInt (pb * inv));
            }
        return img;
    }

    // Point creuse + lueur, rayon ds en pixels PHYSIQUES ; centre au milieu de
    // l'image. Merlin : teinte = accent de l'interface (le bleu de Sevenpack
    // Alpha jurait avec le turquoise), nuances derivees de cette couleur.
    inline juce::Image renderDot (float ds, bool powered, juce::Colour tint)
    {
        const int e = (int) std::ceil (ds * 2.6f) + 1;
        const int size = 2 * e;
        const float c = (float) e;
        juce::Image img (juce::Image::ARGB, size, size, true);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);

        // sombre (haut gauche) / milieu / clair (bas droite) ; gris si eteint
        const float t[3] = { (float) tint.getRed(), (float) tint.getGreen(), (float) tint.getBlue() };
        float c0[3], c1[3], c2[3], glow[3];
        for (int k = 0; k < 3; ++k)
        {
            c0[k] = t[k] * 0.42f + 20.0f;
            c1[k] = t[k] * 0.78f;
            c2[k] = t[k] + (255.0f - t[k]) * 0.40f;
            glow[k] = t[k] + (255.0f - t[k]) * 0.45f;
        }
        const float g0[3] = { 120.0f, 130.0f, 140.0f }, g1[3] = { 150.0f, 160.0f, 170.0f }, g2[3] = { 190.0f, 198.0f, 205.0f };
        const float* k0 = powered ? c0 : g0;
        const float* k1 = powered ? c1 : g1;
        const float* k2 = powered ? c2 : g2;
        const float glA = powered ? 0.565f : 0.0f, glR = 1.04f * ds, gOff = 0.238f * ds * 0.7071f;
        constexpr int N = 4;

        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                float A = 0.0f, p[3] = { 0.0f, 0.0f, 0.0f };
                for (int sy = 0; sy < N; ++sy)
                    for (int sx = 0; sx < N; ++sx)
                    {
                        const float u = (float) x + ((float) sx + 0.5f) / N - c, v = (float) y + ((float) sy + 0.5f) / N - c;
                        const float kGl = glA * std::exp (-((u - gOff) * (u - gOff) + (v - gOff) * (v - gOff)) / (glR * glR));
                        const float cov = juce::jlimit (0.0f, 1.0f, (ds - std::sqrt (u * u + v * v)) * 3.0f + 0.5f);
                        const float w = juce::jlimit (-1.0f, 1.0f, (u + v) / (1.4142f * ds));
                        const float wg = kGl * (1.0f - cov);
                        A += wg + cov;
                        for (int k = 0; k < 3; ++k)
                        {
                            const float dc = w < 0.0f ? k1[k] + (k0[k] - k1[k]) * -w : k1[k] + (k2[k] - k1[k]) * w;
                            p[k] += glow[k] * wg + dc * cov;
                        }
                    }
                constexpr float inv = 1.0f / (float) (N * N);
                if (A * inv * 255.0f < 0.5f)
                    continue;
                reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y))
                    ->setARGB ((juce::uint8) juce::roundToInt (A * inv * 255.0f), (juce::uint8) juce::roundToInt (p[0] * inv),
                               (juce::uint8) juce::roundToInt (p[1] * inv), (juce::uint8) juce::roundToInt (p[2] * inv));
            }
        return img;
    }

    // Cache partage par toutes les tranches (SharedResourcePointer : libere
    // avec le dernier editeur, pas d'image statique a la fermeture du
    // plugin). Cle = taille au quart de pixel physique + theme/etat.
    class Cache
    {
    public:
        const juce::Image& body (float R, bool lightTheme, bool highContrast)
        {
            const int q = juce::jmax (4, juce::roundToInt (R * 4.0f));
            return get (bodies, q * 4 + (lightTheme ? 1 : 0) + (highContrast ? 2 : 0),
                        [&] { return renderBody ((float) q / 4.0f, lightTheme, highContrast); });
        }
        const juce::Image& dot (float ds, bool powered, juce::Colour tint)
        {
            const int q = juce::jmax (4, juce::roundToInt (ds * 4.0f));
            const auto key = ((juce::int64) tint.getARGB() << 24) | (juce::int64) (q * 2 + (powered ? 1 : 0));
            return get (dots, key, [&] { return renderDot ((float) q / 4.0f, powered, tint); });
        }

    private:
        template <typename Fn>
        static const juce::Image& get (std::map<juce::int64, juce::Image>& m, juce::int64 key, Fn&& make)
        {
            auto it = m.find (key);
            if (it == m.end())
            {
                if (m.size() > 24)
                    m.clear();
                it = m.emplace (key, make()).first;
            }
            return it->second;
        }

        std::map<juce::int64, juce::Image> bodies, dots;
    };
}
