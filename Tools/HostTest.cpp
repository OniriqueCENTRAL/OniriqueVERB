// Outil de développement : charge le fichier .vst3 comme le ferait un DAW (hôte VST3 de JUCE),
// puis vérifie ce qu'un hôte attend de lui.
//
//   OniriqueReverbHostTest "chemin/vers/ONIRIQUE VERB.vst3"

#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok     " : "  ECHEC  ") << what << std::endl;
    if (! ok) ++failures;
}

juce::AudioProcessorParameter* find (juce::AudioPluginInstance& plugin, const juce::String& name)
{
    for (auto* p : plugin.getParameters())
        if (p->getName (64) == name) return p;
    return nullptr;
}

// bruit dans le plugin, renvoie la plus grande différence entre entrée et sortie (canal gauche) et si tout est fini
float runNoise (juce::AudioPluginInstance& plugin, int channels, int blocks, bool& finite)
{
    juce::Random r (3);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    float diff = 0.0f;
    finite = true;
    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < 512; ++i) buffer.setSample (c, i, 0.2f * (r.nextFloat() * 2.0f - 1.0f));
        juce::AudioBuffer<float> in;
        in.makeCopyOf (buffer);
        plugin.processBlock (buffer, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i) finite &= std::isfinite (buffer.getSample (c, i));
        for (int i = 0; i < 512; ++i) diff = juce::jmax (diff, std::abs (buffer.getSample (0, i) - in.getSample (0, i)));
    }
    return diff;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2) { std::cout << "usage : OniriqueReverbHostTest <fichier.vst3>" << std::endl; return 1; }
    juce::ScopedJuceInitialiser_GUI gui;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, juce::String::fromUTF8 (argv[1]));
    check (found.size() == 1, "le fichier contient un plugin");
    if (found.isEmpty()) return 1;
    const auto& d = *found[0];
    check (d.name.startsWith ("ONIRIQUE VERB"), "il s'appelle " + d.name);
    check (! d.isInstrument, "c'est un effet");

    juce::String error;
    auto plugin = format.createInstanceFromDescription (d, 48000.0, 512, error);
    check (plugin != nullptr, "il se charge" + (error.isEmpty() ? juce::String() : " (" + error + ")"));
    if (plugin == nullptr) return 1;

    check (find (*plugin, "Mix") != nullptr && find (*plugin, "Decay") != nullptr && find (*plugin, "Infinite") != nullptr,
           juce::String (plugin->getParameters().size()) + " parametres, dont Mix, Decay et Infinite");
    check (plugin->getBypassParameter() != nullptr, "le bypass du DAW pilote son bypass");

    plugin->setPlayConfigDetails (2, 2, 48000.0, 512);
    plugin->prepareToPlay (48000.0, 512);
    check (plugin->getLatencySamples() == 0, "latence nulle");
    check (plugin->getTailLengthSeconds() > 1.0, "il annonce sa queue au DAW (" + juce::String (plugin->getTailLengthSeconds(), 2) + " s)");

    bool finite = false;
    const float wetDiff = runNoise (*plugin, 2, 200, finite);
    check (finite && wetDiff > 0.01f, "en stereo, la reverb s'entend et le son reste propre");

    find (*plugin, "Mix")->setValueNotifyingHost (0.0f);
    runNoise (*plugin, 2, 20, finite);    // le temps que le MIX glisse à 0
    const float dryDiff = runNoise (*plugin, 2, 20, finite);
    check (dryDiff < 1.0e-5f, "MIX 0 % : la voix ressort intacte");

    // sauvegarde dans le projet, puis rechargement
    find (*plugin, "Mix")->setValueNotifyingHost (0.42f);
    find (*plugin, "Decay")->setValueNotifyingHost (0.7f);
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    find (*plugin, "Mix")->setValueNotifyingHost (0.9f);
    find (*plugin, "Decay")->setValueNotifyingHost (0.1f);
    plugin->setStateInformation (state.getData(), (int) state.getSize());
    check (std::abs (find (*plugin, "Mix")->getValue() - 0.42f) < 1.0e-3f && std::abs (find (*plugin, "Decay")->getValue() - 0.7f) < 1.0e-3f,
           "le projet sauvegarde et recharge ses reglages");

    // voix mono sur piste stéréo
    plugin->releaseResources();
    auto layout = plugin->getBusesLayout();
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
    const bool monoOk = plugin->setBusesLayout (layout);
    check (monoOk, "accepte une entree mono vers une sortie stereo");
    if (monoOk)
    {
        plugin->prepareToPlay (48000.0, 512);
        runNoise (*plugin, 1, 100, finite);
        check (finite, "en mono vers stereo, le son reste propre");
    }

    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorIfNeeded());
        check (editor != nullptr && editor->getWidth() > 300, "la fenetre s'ouvre (" + (editor ? juce::String (editor->getWidth()) + " x " + juce::String (editor->getHeight()) : juce::String()) + ")");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
        if (editor) plugin->editorBeingDeleted (editor.get());
    }
    plugin->releaseResources();
    plugin.reset();

    std::cout << (failures == 0 ? "\nTout est bon." : "\nDes essais ont echoue.") << std::endl;
    return failures == 0 ? 0 : 1;
}
