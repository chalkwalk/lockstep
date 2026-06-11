#pragma once

// Oklab colour-space utilities (Björn Ottosson, 2020).
//
// JUCE-free, header-only, so it is shared by the runtime Push surface and the
// offline tools/palette_capture image tool. Oklab is perceptually uniform:
// Euclidean distance in (L,a,b) approximates perceived colour difference, which
// is exactly what we want for nearest-palette matching. LCh is the cylindrical
// form (L, chroma, hue) used for hue-preserving gamut stretching.

#include <cmath>
#include <cstdint>

namespace lockstep::oklab
{
    struct Lab
    {
        float L = 0.0f, a = 0.0f, b = 0.0f;
    };
    struct LCh
    {
        float L = 0.0f, C = 0.0f, h = 0.0f;
    };   // h in radians
    struct Rgb
    {
        float r = 0.0f, g = 0.0f, b = 0.0f;
    };   // linear, 0..1

    // sRGB transfer (gamma) — per channel, 0..1.
    inline float srgbToLinear(float s) noexcept
    {
        return (s <= 0.04045f) ? (s / 12.92f)
                               : std::pow((s + 0.055f) / 1.055f, 2.4f);
    }
    inline float linearToSrgb(float c) noexcept
    {
        c = (c < 0.0f) ? 0.0f : (c > 1.0f ? 1.0f : c);
        return (c <= 0.0031308f) ? (c * 12.92f)
                                 : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
    }

    // Linear sRGB (0..1) -> Oklab.
    inline Lab linearToOklab(Rgb c) noexcept
    {
        const float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
        const float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
        const float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;

        const float l_ = std::cbrt(l);
        const float m_ = std::cbrt(m);
        const float s_ = std::cbrt(s);

        return {
            0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
            1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
            0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_,
        };
    }

    // Oklab -> linear sRGB (0..1, may exceed gamut; caller clamps).
    inline Rgb oklabToLinear(Lab c) noexcept
    {
        const float l_ = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b;
        const float m_ = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b;
        const float s_ = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;

        const float l = l_ * l_ * l_;
        const float m = m_ * m_ * m_;
        const float s = s_ * s_ * s_;

        return {
            +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
            -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
            -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s,
        };
    }

    inline LCh labToLCh(Lab c) noexcept
    {
        return { c.L, std::hypot(c.a, c.b), std::atan2(c.b, c.a) };
    }
    inline Lab lChToLab(LCh c) noexcept
    {
        return { c.L, c.C * std::cos(c.h), c.C * std::sin(c.h) };
    }

    // 8-bit sRGB channels -> Oklab (applies the sRGB transfer).
    inline Lab srgb8ToOklab(int r, int g, int b) noexcept
    {
        return linearToOklab({ srgbToLinear(static_cast<float>(r) / 255.0f),
                               srgbToLinear(static_cast<float>(g) / 255.0f),
                               srgbToLinear(static_cast<float>(b) / 255.0f) });
    }

    // Packed 0xRRGGBB (or low 24 bits of ARGB) -> Oklab. Alpha is ignored;
    // flatten over a background beforehand if needed.
    inline Lab packedRgbToOklab(std::uint32_t rgb) noexcept
    {
        return srgb8ToOklab(static_cast<int>((rgb >> 16) & 0xFF),
                            static_cast<int>((rgb >> 8) & 0xFF),
                            static_cast<int>(rgb & 0xFF));
    }

    inline float distanceSq(Lab x, Lab y) noexcept
    {
        const float dL = x.L - y.L, da = x.a - y.a, db = x.b - y.b;
        return dL * dL + da * da + db * db;
    }
}
