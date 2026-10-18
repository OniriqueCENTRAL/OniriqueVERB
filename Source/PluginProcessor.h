#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "License/LicenseGuard.h"
#include "Parameters.h"
#include "PresetManager.h"
#include "DSP/ReverbEngine.h"

class OniriqueReverbProcessor : public juce::AudioProcessor
{
public:
    OniriqueReverbProcessor();
    ~OniriqueReverbProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // Le bypass du DAW pilote directement le paramètre "Bypass" (fondu propre, la queue continue de vivre)
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (onirique::ids::bypass); }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    onirique::PresetManager presets { apvts };

    // ---- lus par l'interface (thread GUI)
    const onirique::reverb::Engine& getEngine() const noexcept { return engine; }
    float value (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    float getBpm() const noexcept { return effectiveBpm.load (std::memory_order_relaxed); }   // tempo utilisé (DAW ou choisi)
    bool isSynced() const noexcept { return value (onirique::ids::sync) > 0.5f; }
    /** DECAY et PRE-DELAY réellement appliqués : en secondes / ms, ou calculés au tempo quand BPM est allumé. */
    float effectiveDecay() const noexcept;
    float effectivePreMs() const noexcept;

    /** Licence ONIRIQUE (acheté, essai, démo) : voir License/LicenseGuard.h. */
    onirique::license::Guard license { "onirique-verb" };

private:
    onirique::reverb::Params readParams() const noexcept;

    onirique::reverb::Engine engine;
    std::atomic<float> effectiveBpm { 120.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueReverbProcessor)
};
