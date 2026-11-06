#pragma once
#if __has_include ("ui/OniriqueTheme.h")
 #include "ui/OniriqueTheme.h"
#elif __has_include ("UI/OniriqueTheme.h")
 #include "UI/OniriqueTheme.h"
#elif __has_include ("GUI/OniriqueTheme.h")
 #include "GUI/OniriqueTheme.h"
#elif __has_include ("../License/OniriqueTheme.h")
 #include "../License/OniriqueTheme.h"
#else
 #include "OniriqueTheme.h"
#endif

#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace onirique::ui
{

//==============================================================================
//  ONIRIQUE VERB : un instrument de nuit. Une façade de métal anodisé couleur sang séché, des inscriptions
//  sérigraphiées, des potards usinés, des touches à témoin, et des fenêtres de verre fumé où la reverb
//  s'allume. La braise ne sert qu'à ce qui émet de la lumière : témoins, valeurs, affichages.
//  Toutes les couleurs de l'interface viennent d'ici.
//==============================================================================
namespace palette
{
    // la charte ONIRIQUE
    inline const juce::Colour night     { 0xff0d0809 };
    inline const juce::Colour& blood = onirique::look::litTop;
    inline const juce::Colour& bloodDeep = onirique::look::litBottom;
    inline const juce::Colour mist      { 0xfff6eeee };
    inline const juce::Colour haze      { 0xffa88f92 };
    inline const juce::Colour& ember = onirique::look::emberBar;
    inline const juce::Colour& emberHot = onirique::look::emberLit;
    inline const juce::Colour bone      { 0xffffecee };
    inline const juce::Colour rose      { 0xffffd2d6 };

    // la façade
    inline juce::Colour plateTop    { 0xff231615 };
    inline juce::Colour plateBottom { 0xff110a0a };
    inline const juce::Colour ink         { 0xffeadcd9 };   // sérigraphie
    inline juce::Colour inkDim      { 0xff98817f };
    inline juce::Colour metalHi     { 0xff352c2b };   // capuchons, jupes des potards
    inline juce::Colour metalLo     { 0xff0c0909 };
    inline juce::Colour glass       { 0xff080404 };   // fond des affichages
    inline juce::Colour ledOff      { 0xff2c1517 };

    /** La façade suit la couleur de l'interface (même luminosité) : rappelée quand la teinte est chargée ou change. */
    inline void refreshPalette()
    {
        plateTop = onirique::look::tint (0xff231615);
        plateBottom = onirique::look::tint (0xff110a0a);
        inkDim = onirique::look::tint (0xff98817f);
        metalHi = onirique::look::tint (0xff352c2b);
        metalLo = onirique::look::tint (0xff0c0909);
        glass = onirique::look::tint (0xff080404);
        ledOff = onirique::look::tint (0xff2c1517);
    }
}

/** Les quatre familles de la charte : Unbounded (la marque), Grenze Gotisch (« Verb »),
    Hanken Grotesk (la sérigraphie), JetBrains Mono (les valeurs dans les affichages). */
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

} // namespace onirique::ui
