#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace onirique
{
namespace ids
{
    inline constexpr const char* mode      = "mode";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* preDelay  = "preDelay";
    inline constexpr const char* decay     = "decay";
    inline constexpr const char* size      = "size";
    inline constexpr const char* diffusion = "diffusion";
    inline constexpr const char* modRate   = "modRate";
    inline constexpr const char* modDepth  = "modDepth";
    inline constexpr const char* width     = "width";
    inline constexpr const char* lowCut    = "lowCut";
    inline constexpr const char* highCut   = "highCut";
    inline constexpr const char* ducking   = "ducking";
    inline constexpr const char* clean     = "clean";
    inline constexpr const char* pulse     = "pulse";
    inline constexpr const char* pulseRate = "pulseRate";
    inline constexpr const char* freeze    = "freeze";
    inline constexpr const char* output    = "output";
    inline constexpr const char* bypass    = "bypass";
    // tempo
    inline constexpr const char* sync      = "sync";
    inline constexpr const char* bpmSource = "bpmSource";
    inline constexpr const char* bpm       = "bpm";
    inline constexpr const char* decayNote = "decayNote";
    inline constexpr const char* preNote   = "preNote";
    // STUTTER
    inline constexpr const char* stutterMode = "stutterMode";
    inline constexpr const char* stutterRate = "stutterRate";
    inline constexpr const char* stutterMix  = "stutterMix";
    // EQ : IN (ce qui entre dans la reverb) ; OUT (la queue), dont les coupes sont lowCut / highCut
    inline constexpr const char* inLowCut    = "inLowCut";
    inline constexpr const char* inHighCut   = "inHighCut";
    inline constexpr const char* inLowFreq   = "inLowFreq";
    inline constexpr const char* inLowGain   = "inLowGain";
    inline constexpr const char* inMidFreq   = "inMidFreq";
    inline constexpr const char* inMidGain   = "inMidGain";
    inline constexpr const char* inHighFreq  = "inHighFreq";
    inline constexpr const char* inHighGain  = "inHighGain";
    inline constexpr const char* outLowFreq  = "outLowFreq";
    inline constexpr const char* outLowGain  = "outLowGain";
    inline constexpr const char* outMidFreq  = "outMidFreq";
    inline constexpr const char* outMidGain  = "outMidGain";
    inline constexpr const char* outHighFreq = "outHighFreq";
    inline constexpr const char* outHighGain = "outHighGain";
}

// les identifiants d'un EQ, bande par bande : coupe-bas, coupe-haut, grave, médium, aigu (fréquence, gain)
struct EqIds { const char *lowCut, *highCut, *lowFreq, *lowGain, *midFreq, *midGain, *highFreq, *highGain; };
inline constexpr EqIds eqInIds  { ids::inLowCut, ids::inHighCut, ids::inLowFreq, ids::inLowGain, ids::inMidFreq, ids::inMidGain, ids::inHighFreq, ids::inHighGain };
inline constexpr EqIds eqOutIds { ids::lowCut, ids::highCut, ids::outLowFreq, ids::outLowGain, ids::outMidFreq, ids::outMidGain, ids::outHighFreq, ids::outHighGain };

inline const juce::StringArray modeNames { "ROOM", "PLATE I", "PLATE II", "HALL", "SHIMMER", "ONIRIQUE" };

// PULSE : nom affiché et durée en noires
inline const juce::StringArray pulseRateNames { "1/2", "1/4", "1/8", "1/16", "1/4 T", "1/8 T", "1/8 D" };
inline double pulseRateBeats (int i)
{
    static constexpr double beats[] { 2.0, 1.0, 0.5, 0.25, 2.0 / 3.0, 1.0 / 3.0, 0.75 };
    return beats[juce::jlimit (0, 6, i)];
}

// Queue au tempo (bouton BPM) : DECAY et PRE-DELAY en valeurs de notes (durée en noires)
inline const juce::StringArray decayNoteNames { "1 TEMPS", "2 TEMPS", "3 TEMPS", "1 MESURE", "1,5 MESURE", "2 MESURES", "4 MESURES", "8 MESURES" };
inline double decayNoteBeats (int i)
{
    static constexpr double beats[] { 1.0, 2.0, 3.0, 4.0, 6.0, 8.0, 16.0, 32.0 };
    return beats[juce::jlimit (0, 7, i)];
}
inline const juce::StringArray preNoteNames { "0", "1/64", "1/32", "1/16 T", "1/16", "1/8 T", "1/16 D", "1/8", "1/8 D", "1/4" };
inline double preNoteBeats (int i)
{
    static constexpr double beats[] { 0.0, 1.0 / 16.0, 1.0 / 8.0, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 0.75, 1.0 };
    return beats[juce::jlimit (0, 9, i)];
}

// STUTTER : découpe de la queue au tempo
inline const juce::StringArray stutterModeNames { "OFF", "CHOP", "REPEAT", "RAMP" };
inline const juce::StringArray stutterRateNames { "1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T" };
inline double stutterRateBeats (int i)
{
    static constexpr double beats[] { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
    return beats[juce::jlimit (0, 5, i)];
}

inline juce::String hzText (float v)
{
    return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    using Attr = AudioParameterFloatAttributes;

    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto pctText = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto pct = [&] (const char* id, const char* name, float hi, float def)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, hi, 0.1f), def,
                                                            Attr().withLabel ("%").withStringFromValueFunction (pctText)));
    };
    auto skewed = [&] (const char* id, const char* name, float lo, float hi, float centre, float def, const char* label,
                       std::function<String (float, int)> text)
    {
        NormalisableRange<float> r (lo, hi, 0.0f);
        r.setSkewForCentre (centre);
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                            Attr().withLabel (label).withStringFromValueFunction (std::move (text))));
    };

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::mode, 1 }, "Mode", modeNames, 1));
    pct (ids::mix, "Mix", 100.0f, 30.0f);
    skewed (ids::preDelay, "Pre-Delay", 0.0f, 500.0f, 60.0f, 20.0f, "ms", [] (float v, int) { return String (roundToInt (v)) + " ms"; });
    skewed (ids::decay, "Decay", 0.2f, 20.0f, 3.0f, 2.5f, "s", [] (float v, int) { return String (v, v < 10.0f ? 2 : 1) + " s"; });
    pct (ids::size, "Size", 100.0f, 60.0f);
    pct (ids::diffusion, "Diffusion", 100.0f, 80.0f);
    skewed (ids::modRate, "Mod Rate", 0.05f, 5.0f, 0.8f, 0.6f, "Hz", [] (float v, int) { return String (v, 2) + " Hz"; });
    pct (ids::modDepth, "Mod Depth", 100.0f, 30.0f);
    pct (ids::width, "Width", 200.0f, 100.0f);
    // ---- EQ IN et EQ OUT
    {
        auto hz = [] (float v, int) { return hzText (v); };
        auto db = [] (float v, int) { v = std::round (v * 10.0f) / 10.0f + 0.0f; return String (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; };
        auto gain = [&] (const char* id, const char* name)
        {
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                                Attr().withLabel ("dB").withStringFromValueFunction (db)));
        };
        for (int s = 0; s < 2; ++s)
        {
            const auto& e = s == 0 ? eqOutIds : eqInIds;
            const String tag = s == 0 ? "Out " : "In ";
            skewed (e.lowCut, (tag + "Low Cut").toRawUTF8(), 20.0f, 1000.0f, 200.0f, s == 0 ? 150.0f : 20.0f, "Hz", hz);
            skewed (e.highCut, (tag + "High Cut").toRawUTF8(), 1000.0f, 20000.0f, 6000.0f, s == 0 ? 9000.0f : 20000.0f, "Hz", hz);
            skewed (e.lowFreq, (tag + "Low Freq").toRawUTF8(), 40.0f, 1000.0f, 200.0f, 200.0f, "Hz", hz);
            gain (e.lowGain, (tag + "Low Gain").toRawUTF8());
            skewed (e.midFreq, (tag + "Mid Freq").toRawUTF8(), 200.0f, 8000.0f, 1200.0f, 1000.0f, "Hz", hz);
            gain (e.midGain, (tag + "Mid Gain").toRawUTF8());
            skewed (e.highFreq, (tag + "High Freq").toRawUTF8(), 1500.0f, 16000.0f, 6000.0f, 6000.0f, "Hz", hz);
            gain (e.highGain, (tag + "High Gain").toRawUTF8());
        }
    }
    pct (ids::ducking, "Ducking", 100.0f, 0.0f);
    pct (ids::clean, "Clean", 100.0f, 0.0f);
    pct (ids::pulse, "Pulse", 100.0f, 0.0f);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::pulseRate, 1 }, "Pulse Rate", pulseRateNames, 2));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::freeze, 1 }, "Infinite", false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::output, 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
        {
            v = std::round (v * 10.0f) / 10.0f + 0.0f;   // évite "-0.0"
            return String (v > 0.0f ? "+" : "") + String (v, 1) + " dB";
        })));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));

    // ---- tempo : BPM du DAW ou choisi à la main ; bouton BPM = DECAY et PRE-DELAY en valeurs de notes
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::sync, 1 }, "Tempo Sync", false));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::bpmSource, 1 }, "BPM Source", StringArray { "DAW", "Manuel" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::bpm, 1 }, "BPM", NormalisableRange<float> (40.0f, 240.0f, 0.1f), 120.0f,
        Attr().withLabel ("BPM").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " BPM"; })));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::decayNote, 1 }, "Decay Note", decayNoteNames, 3));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::preNote, 1 }, "Pre-Delay Note", preNoteNames, 2));

    // ---- STUTTER
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::stutterMode, 1 }, "Stutter", stutterModeNames, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::stutterRate, 1 }, "Stutter Rate", stutterRateNames, 2));
    pct (ids::stutterMix, "Stutter Mix", 100.0f, 100.0f);

    return { p.begin(), p.end() };
}
} // namespace onirique
