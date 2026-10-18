#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace onirique;

OniriqueReverbProcessor::OniriqueReverbProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

reverb::Params OniriqueReverbProcessor::readParams() const noexcept
{
    reverb::Params p;
    p.mode       = juce::roundToInt (value (ids::mode));
    p.mix        = value (ids::mix) * 0.01f;
    p.preDelayMs = effectivePreMs();
    p.decay      = effectiveDecay();
    p.size       = value (ids::size) * 0.01f;
    p.diffusion  = value (ids::diffusion) * 0.01f;
    p.modRate    = value (ids::modRate);
    p.modDepth   = value (ids::modDepth) * 0.01f;
    p.width      = value (ids::width) * 0.01f;
    auto eq = [this] (const EqIds& e)
    {
        reverb::Eq q;
        q.lowCut = value (e.lowCut);   q.highCut = value (e.highCut);
        q.lowFreq = value (e.lowFreq); q.lowGain = value (e.lowGain);
        q.midFreq = value (e.midFreq); q.midGain = value (e.midGain);
        q.highFreq = value (e.highFreq); q.highGain = value (e.highGain);
        return q;
    };
    p.eqIn  = eq (eqInIds);
    p.eqOut = eq (eqOutIds);
    p.ducking    = value (ids::ducking) * 0.01f;
    p.clean      = value (ids::clean) * 0.01f;
    p.pulse      = value (ids::pulse) * 0.01f;
    p.pulseBeats = pulseRateBeats (juce::roundToInt (value (ids::pulseRate)));
    p.freeze     = value (ids::freeze) > 0.5f;
    p.bypass     = value (ids::bypass) > 0.5f;
    p.outputDb   = value (ids::output);
    p.stutterMode  = juce::roundToInt (value (ids::stutterMode));
    p.stutterBeats = stutterRateBeats (juce::roundToInt (value (ids::stutterRate)));
    p.stutterMix   = value (ids::stutterMix) * 0.01f;
    return p;
}

float OniriqueReverbProcessor::effectiveDecay() const noexcept
{
    return isSynced() ? (float) (decayNoteBeats (juce::roundToInt (value (ids::decayNote))) * 60.0 / getBpm()) : value (ids::decay);
}

float OniriqueReverbProcessor::effectivePreMs() const noexcept
{
    return isSynced() ? (float) (preNoteBeats (juce::roundToInt (value (ids::preNote))) * 60000.0 / getBpm()) : value (ids::preDelay);
}

bool OniriqueReverbProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& in = l.getMainInputChannelSet();
    const auto& out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());   // voix mono -> reverb stéréo
}

double OniriqueReverbProcessor::getTailLengthSeconds() const
{
    return effectivePreMs() * 0.001 + 1.2 * effectiveDecay();
}

void OniriqueReverbProcessor::prepareToPlay (double sampleRate, int)
{
   #if ! ONIRIQUE_TEST_BUILD
    license.refresh();   // beaucoup de DAW rappellent prepareToPlay juste avant un export
   #endif
    if (juce::roundToInt (value (ids::bpmSource)) == 1) effectiveBpm.store (value (ids::bpm));   // la queue au tempo choisi dès le départ
    engine.prepare (sampleRate, readParams());   // toute l'allocation se fait ici ; latence nulle
    setLatencySamples (0);
}

void OniriqueReverbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = juce::jmin (2, getTotalNumOutputChannels());
    if (numOut < 1 || n < 1)
        return;
    if (numIn == 1 && numOut == 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);   // voix mono : la même voix des deux côtés, la reverb s'ouvre en stéréo
    else
        for (int c = numIn; c < getTotalNumOutputChannels(); ++c)
            buffer.clear (c, 0, n);

    // tempo et position du DAW (PULSE, STUTTER, queue au tempo) ; BPM manuel : le tempo choisi, sans caler sur la mesure
    double bpm = 120.0, ppq = -1.0;
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
        {
            if (const auto b = pos->getBpm()) bpm = *b;
            if (pos->getIsPlaying())
                if (const auto q = pos->getPpqPosition()) ppq = *q;
        }
    if (juce::roundToInt (value (ids::bpmSource)) == 1)
    {
        bpm = value (ids::bpm);
        ppq = -1.0;
    }
    effectiveBpm.store ((float) juce::jlimit (20.0, 400.0, bpm), std::memory_order_relaxed);

    engine.set (readParams());
    engine.process (buffer.getArrayOfWritePointers(), numOut, n, bpm, ppq);

   #if ! ONIRIQUE_TEST_BUILD   // version d'essai locale (ONIRIQUE VERB TEST) : pas de licence
    license.process (buffer, getSampleRate(), isNonRealtime());   // en dernier, sur toutes les sorties
   #endif
}

juce::AudioProcessorEditor* OniriqueReverbProcessor::createEditor()
{
    return new OniriqueReverbEditor (*this);
}

void OniriqueReverbProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void OniriqueReverbProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            presets.takeSnapshot();   // l'état rechargé sert de référence pour l'indicateur "modifié"
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OniriqueReverbProcessor();
}
