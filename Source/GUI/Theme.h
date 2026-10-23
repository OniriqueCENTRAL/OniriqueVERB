#pragma once

#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace onirique::ui
{

//==============================================================================
//  Charte ONIRIQUE, reprise du site : nuit et sang, braise, sigil incandescent,
//  étoiles à quatre branches. Toutes les couleurs de l'interface viennent d'ici.
//==============================================================================
namespace palette
{
    inline const juce::Colour night     { 0xff0d0809 };
    inline const juce::Colour blood     { 0xff5c0008 };
    inline const juce::Colour bloodDeep { 0xff2e0306 };
    inline const juce::Colour deep      { 0xff1a080a };
    inline const juce::Colour deep2     { 0xff2a0a0e };
    inline const juce::Colour mist      { 0xfff6eeee };
    inline const juce::Colour haze      { 0xffa88f92 };
    inline const juce::Colour ember     { 0xffe23444 };
    inline const juce::Colour emberHot  { 0xffff5a68 };
    inline const juce::Colour bone      { 0xffffecee };
    inline const juce::Colour rose      { 0xffffd2d6 };

    inline juce::Colour line()    { return rose.withAlpha (0.13f); }
    inline juce::Colour ledOff()  { return rose.withAlpha (0.2f); }
    inline juce::Colour well()    { return juce::Colours::black.withAlpha (0.35f); }
}

/** Les quatre familles de la charte : Unbounded (titres), Grenze Gotisch
    (l'accent "rêve"), Hanken Grotesk (texte), JetBrains Mono (étiquettes). */
struct FontSet
{
    FontSet()
    {
        display = juce::Typeface::createSystemTypefaceFor (BinaryData::Unbounded800_ttf, BinaryData::Unbounded800_ttfSize);
        dream = juce::Typeface::createSystemTypefaceFor (BinaryData::GrenzeGotisch600_ttf, BinaryData::GrenzeGotisch600_ttfSize);
        body = juce::Typeface::createSystemTypefaceFor (BinaryData::HankenGrotesk600_ttf, BinaryData::HankenGrotesk600_ttfSize);
        mono = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMono500_ttf, BinaryData::JetBrainsMono500_ttfSize);
        monoBold = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMono600_ttf, BinaryData::JetBrainsMono600_ttfSize);
    }

    static juce::Font make (const juce::Typeface::Ptr& face, float height, float tracking)
    {
        return juce::Font (juce::FontOptions (face).withHeight (height)).withExtraKerningFactor (tracking);
    }

    juce::Font displayFont (float height, float tracking = 0.0f) const  { return make (display, height, tracking); }
    juce::Font dreamFont (float height) const                           { return make (dream, height, 0.0f); }
    juce::Font bodyFont (float height, float tracking = 0.0f) const     { return make (body, height, tracking); }
    juce::Font monoFont (float height, float tracking = 0.0f) const     { return make (mono, height, tracking); }
    juce::Font monoBoldFont (float height, float tracking = 0.0f) const { return make (monoBold, height, tracking); }

    juce::Typeface::Ptr display, dream, body, mono, monoBold;
};

using Fonts = juce::SharedResourcePointer<FontSet>;

//==============================================================================
/** Étoile à quatre branches du logo (même tracé que le clip-path du site). */
inline juce::Path starPath (juce::Point<float> centre, float size)
{
    static constexpr float points[8][2] = {
        { 50, 0 }, { 56, 44 }, { 100, 50 }, { 56, 56 }, { 50, 100 }, { 44, 56 }, { 0, 50 }, { 44, 44 }
    };

    juce::Path p;
    for (int i = 0; i < 8; ++i)
    {
        const juce::Point<float> pt (centre.x + (points[i][0] - 50.0f) * size / 100.0f,
                                     centre.y + (points[i][1] - 50.0f) * size / 100.0f);
        if (i == 0)
            p.startNewSubPath (pt);
        else
            p.lineTo (pt);
    }

    p.closeSubPath();
    return p;
}

inline void drawStar (juce::Graphics& g, juce::Point<float> centre, float size, float alpha = 1.0f)
{
    const auto star = starPath (centre, size);
    g.setColour (palette::ember.withAlpha (0.55f * alpha));
    g.fillPath (starPath (centre, size * 1.5f));
    g.setColour (palette::mist.withAlpha (alpha));
    g.fillPath (star);
}

/** Halo : le tracé est épaissi en couches de plus en plus larges et de plus
    en plus transparentes. Bien moins coûteux qu'un flou, donc utilisable dans
    les vues rafraîchies trente fois par seconde. */
inline void strokeGlow (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float width, float spread)
{
    for (int layer = 3; layer >= 1; --layer)
    {
        g.setColour (colour.withMultipliedAlpha (0.11f * (float) (4 - layer)));
        g.strokePath (path, juce::PathStrokeType (width + spread * (float) layer, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

/** Petite étiquette en capitales espacées, précédée de l'étoile. */
inline void drawEyebrow (juce::Graphics& g, const FontSet& fonts, const juce::String& text, float x, float y)
{
    drawStar (g, { x + 5.0f, y }, 10.0f);
    g.setColour (palette::haze);
    g.setFont (fonts.monoFont (10.5f, 0.16f));
    g.drawText (text.toUpperCase(), juce::Rectangle<float> (x + 17.0f, y - 8.0f, 240.0f, 16.0f), juce::Justification::centredLeft, false);
}

} // namespace onirique::ui
