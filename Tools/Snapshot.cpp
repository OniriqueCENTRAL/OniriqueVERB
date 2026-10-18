// Outil de développement : instancie le plugin sans DAW, lui fait chanter une voix de synthèse
// (phrases et silences), puis enregistre l'interface dans un fichier PNG.
//
//   OniriqueReverbSnapshot <sortie.png> [echelle] [mode 0-5] [infinite] [dry] [sync] [chop|repeat|ramp]

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "GUI/OniriqueWindowSize.h"

#include <cmath>
#include <iostream>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "usage : OniriqueReverbSnapshot <sortie.png> [echelle] [mode 0-5] [infinite] [dry]" << std::endl;
        return 1;
    }
    const float scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;
    const int mode = argc > 3 ? juce::String (argv[3]).getIntValue() : 3;
    juce::StringArray options;
    for (int i = 4; i < argc; ++i) options.add (argv[i]);

    juce::ScopedJuceInitialiser_GUI gui;
    const double sampleRate = 48000.0;
    const int blockSize = 256;

    std::unique_ptr<juce::AudioProcessor> plugin (createPluginFilter());
    auto* reverb = dynamic_cast<OniriqueReverbProcessor*> (plugin.get());
    if (reverb == nullptr) return 2;

    auto set = [&] (const char* id, float v)
    {
        if (auto* p = reverb->apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    set (onirique::ids::mode, (float) mode);
    set (onirique::ids::mix, 35.0f);
    set (onirique::ids::decay, 3.5f);
    set (onirique::ids::modDepth, 45.0f);
    set (onirique::ids::width, 140.0f);
    set (onirique::ids::ducking, 25.0f);
    set (onirique::ids::clean, 40.0f);
    set (onirique::ids::sync, options.contains ("sync") ? 1.0f : 0.0f);
    set (onirique::ids::stutterMode, options.contains ("chop") ? 1.0f : options.contains ("repeat") ? 2.0f : options.contains ("ramp") ? 3.0f : 0.0f);

    plugin->setPlayConfigDetails (2, 2, sampleRate, blockSize);
    plugin->prepareToPlay (sampleRate, blockSize);

    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditor());
    auto* view = dynamic_cast<OniriqueReverbEditor*> (editor.get());
    if (view == nullptr) return 3;

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    double phase = 0.0;
    const double melody[] = { 57.0, 60.0, 62.0, 64.0, 62.0, 60.0, 59.0, 57.0 };
    const double seconds = 6.0;
    const int totalBlocks = (int) (seconds * sampleRate / blockSize);
    const bool dryOnly = options.contains ("dry");
    int untilRefresh = 0;

    for (int block = 0; block < totalBlocks; ++block)
    {
        if (options.contains ("infinite") && block == totalBlocks * 2 / 3) set (onirique::ids::freeze, 1.0f);
        for (int i = 0; i < blockSize; ++i)
        {
            const double t = ((double) block * blockSize + i) / sampleRate;
            const int step = (int) (t / 0.45) % 8;
            const double local = std::fmod (t, 0.45);
            const double note = melody[step] + 0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t);
            const double f = 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
            phase += 2.0 * juce::MathConstants<double>::pi * f / sampleRate;
            double x = 0.0;
            for (int h = 1; h <= 20; ++h)
                x += std::sin ((double) h * phase) / (double) h * (1.0 + 2.0 * std::exp (-0.5 * std::pow (((double) h * f - 750.0) / 280.0, 2.0)));
            // phrases de 4 notes puis un silence : la queue se voit entre les phrases
            const bool silent = (step % 4 == 3 && local > 0.1) || t > seconds - 0.9;
            const float env = (float) std::min (1.0, local / 0.02);
            const float s = silent ? 0.0f : (float) (0.1 * x) * env;
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, s);
        }
        if (dryOnly) buffer.clear();
        plugin->processBlock (buffer, midi);
        if (++untilRefresh >= 6)
        {
            untilRefresh = 0;
            view->refreshDisplays (6.0 * blockSize / sampleRate);
        }
    }

    editor->setSize (juce::roundToInt (OniriqueReverbEditor::baseWidth * scale), juce::roundToInt (OniriqueReverbEditor::baseHeight * scale));
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

    juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    file.deleteFile();
    juce::FileOutputStream stream (file);
    if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
    {
        std::cout << "impossible d'ecrire " << file.getFullPathName() << std::endl;
        return 4;
    }
    std::cout << "ecrit : " << file.getFullPathName() << " (" << image.getWidth() << " x " << image.getHeight() << ")" << std::endl;
    editor.reset();
    // la fermeture de la fenêtre a retenu la taille de la capture : ce n'est pas un choix de l'utilisateur
    juce::PropertiesFile (onirique::window::settingsFile (JucePlugin_Name)).getFile().deleteFile();
    return 0;
}
