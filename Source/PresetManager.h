#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"

namespace onirique
{
/*  Presets : usine (définis ici) + utilisateur (fichiers .orvpreset dans
    %APPDATA%/Onirique/Reverb/Presets). Thread de l'interface uniquement.

    - Un preset contient tous les paramètres SAUF Bypass, INFINITE et le tempo (BPM du DAW ou choisi) : réglages de session.
    - MIX verrouillé (cadenas sous le MIX) : changer de preset garde le dosage choisi.
    - Un paramètre absent d'un fichier (preset d'une version plus ancienne) reprend sa valeur par défaut.
    - Le nom du preset courant est rangé dans l'état du plugin : il revient avec le projet.
    - "Modifié" : comparaison des valeurs actuelles avec celles du chargement.
*/
class PresetManager
{
public:
    struct Entry { juce::String name; bool factory = false; juce::File file; int factoryIndex = -1; };

    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s)
    {
        folder().createDirectory();
        takeSnapshot();
    }

    static juce::File folder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Onirique").getChildFile ("Reverb").getChildFile ("Presets");
    }

    std::vector<Entry> list() const
    {
        std::vector<Entry> v;
        for (int i = 0; i < (int) factory().size(); ++i)
            v.push_back ({ factory()[(size_t) i].name, true, {}, i });
        auto files = folder().findChildFiles (juce::File::findFiles, false, "*.orvpreset");
        files.sort();
        for (auto& f : files) v.push_back ({ f.getFileNameWithoutExtension(), false, f, -1 });
        return v;
    }

    juce::String getCurrentName() const { return apvts.state.getProperty ("presetName", "Init").toString(); }

    bool isMixLocked() const { return (bool) apvts.state.getProperty ("mixLock", false); }
    void setMixLocked (bool locked) { apvts.state.setProperty ("mixLock", locked, nullptr); }

    bool isModified() const
    {
        if (snapshot.empty()) return false;
        size_t i = 0;
        for (auto* p : params())
            if (i < snapshot.size() && std::abs (p->getValue() - snapshot[i++]) > 1.0e-4f) return true;
        return false;
    }

    void load (const Entry& e)
    {
        std::map<juce::String, float> values;
        if (e.factory)
            values = factory()[(size_t) e.factoryIndex].values;
        else if (auto xml = juce::XmlDocument::parse (e.file))
            for (auto* p : xml->getChildWithTagNameIterator ("PARAM"))
                values[p->getStringAttribute ("id")] = (float) p->getDoubleAttribute ("value");

        for (auto* p : params())
        {
            if (isMixLocked() && p->getParameterID() == ids::mix) continue;
            const auto it = values.find (p->getParameterID());
            const float norm = it != values.end() ? p->convertTo0to1 (it->second) : p->getDefaultValue();
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        }
        apvts.state.setProperty ("presetName", e.name, nullptr);
        takeSnapshot();
    }

    // renvoie false si le nom est vide ; un nom d'usine est suffixé pour ne jamais l'écraser
    bool save (juce::String name)
    {
        name = juce::File::createLegalFileName (name.trim());
        if (name.isEmpty()) return false;
        for (const auto& f : factory()) if (f.name.equalsIgnoreCase (name)) name << " (user)";

        juce::XmlElement xml ("OniriqueReverbPreset");
        xml.setAttribute ("name", name);
        xml.setAttribute ("version", 1);
        for (auto* p : params())
        {
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", p->getParameterID());
            e->setAttribute ("value", (double) p->convertFrom0to1 (p->getValue()));
        }
        if (! xml.writeTo (folder().getChildFile (name + ".orvpreset"))) return false;
        apvts.state.setProperty ("presetName", name, nullptr);
        takeSnapshot();
        return true;
    }

    void remove (const Entry& e)
    {
        if (! e.factory) e.file.deleteFile();
    }

    // charge le preset précédent / suivant dans la liste (boucle)
    void step (int delta)
    {
        const auto v = list();
        if (v.empty()) return;
        int idx = 0;
        for (int i = 0; i < (int) v.size(); ++i) if (v[(size_t) i].name == getCurrentName()) idx = i;
        load (v[(size_t) ((idx + delta + (int) v.size()) % (int) v.size())]);
    }

    void takeSnapshot()
    {
        snapshot.clear();
        for (auto* p : params()) snapshot.push_back (p->getValue());
    }

private:
    struct Factory { juce::String name; std::map<juce::String, float> values; };

    std::vector<juce::RangedAudioParameter*> params() const
    {
        std::vector<juce::RangedAudioParameter*> v;
        for (auto* p : apvts.processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (const auto id = r->getParameterID(); id != ids::bypass && id != ids::freeze && id != ids::bpmSource && id != ids::bpm)
                    v.push_back (r);
        return v;
    }

    // valeurs en unités réelles (%, ms, s, Hz, index de choix)
    // mode 0 ROOM / 1 PLATE I / 2 PLATE II / 3 HALL / 4 SHIMMER / 5 ONIRIQUE ; pulseRate, decayNote, preNote, stutterMode,
    // stutterRate : index dans les listes de Parameters.h
    static const std::vector<Factory>& factory()
    {
        static const std::vector<Factory> f {
            { "Init", {} },
            { "Rap Vocal Plate",    { { ids::mode, 1.0f }, { ids::mix, 18.0f }, { ids::preDelay, 25.0f }, { ids::decay, 1.4f },
                                      { ids::size, 45.0f }, { ids::lowCut, 250.0f }, { ids::highCut, 7500.0f },
                                      { ids::ducking, 40.0f }, { ids::clean, 50.0f } } },
            { "Trap Ducking",       { { ids::mode, 1.0f }, { ids::mix, 30.0f }, { ids::preDelay, 30.0f }, { ids::decay, 2.5f },
                                      { ids::lowCut, 300.0f }, { ids::highCut, 8000.0f }, { ids::ducking, 80.0f }, { ids::clean, 60.0f } } },
            { "Small Room",         { { ids::mode, 0.0f }, { ids::mix, 15.0f }, { ids::preDelay, 5.0f }, { ids::decay, 0.7f },
                                      { ids::size, 35.0f }, { ids::lowCut, 180.0f }, { ids::highCut, 8000.0f } } },
            { "Dark Plate",         { { ids::mode, 2.0f }, { ids::mix, 25.0f }, { ids::preDelay, 20.0f }, { ids::decay, 2.2f },
                                      { ids::lowCut, 220.0f }, { ids::highCut, 5500.0f }, { ids::width, 130.0f } } },
            { "Lead Pop Hall",      { { ids::mode, 3.0f }, { ids::mix, 22.0f }, { ids::preDelay, 40.0f }, { ids::decay, 2.8f },
                                      { ids::size, 70.0f }, { ids::lowCut, 200.0f }, { ids::width, 120.0f },
                                      { ids::ducking, 30.0f }, { ids::clean, 40.0f } } },
            { "Breathing Ad-libs",  { { ids::mode, 3.0f }, { ids::mix, 35.0f }, { ids::preDelay, 20.0f }, { ids::decay, 3.5f },
                                      { ids::lowCut, 250.0f }, { ids::pulse, 70.0f }, { ids::pulseRate, 2.0f }, { ids::ducking, 20.0f } } },
            { "Shimmer Sky",        { { ids::mode, 4.0f }, { ids::mix, 35.0f }, { ids::preDelay, 60.0f }, { ids::decay, 6.0f },
                                      { ids::size, 85.0f }, { ids::lowCut, 300.0f }, { ids::highCut, 11000.0f },
                                      { ids::width, 150.0f }, { ids::ducking, 35.0f } } },
            { "Onirique Dream",     { { ids::mode, 5.0f }, { ids::mix, 40.0f }, { ids::preDelay, 80.0f }, { ids::decay, 9.0f },
                                      { ids::size, 90.0f }, { ids::modDepth, 55.0f }, { ids::lowCut, 250.0f }, { ids::highCut, 6500.0f },
                                      { ids::width, 160.0f }, { ids::ducking, 45.0f }, { ids::clean, 30.0f } } },
            { "Tempo Plate",        { { ids::mode, 1.0f }, { ids::mix, 25.0f }, { ids::sync, 1.0f }, { ids::decayNote, 3.0f },
                                      { ids::preNote, 4.0f }, { ids::lowCut, 250.0f }, { ids::highCut, 8000.0f }, { ids::ducking, 30.0f } } },
            { "Stutter Tail",       { { ids::mode, 3.0f }, { ids::mix, 35.0f }, { ids::sync, 1.0f }, { ids::decayNote, 5.0f },
                                      { ids::lowCut, 250.0f }, { ids::ducking, 40.0f }, { ids::stutterMode, 1.0f }, { ids::stutterRate, 2.0f } } },
            { "Repeat Dream",       { { ids::mode, 5.0f }, { ids::mix, 40.0f }, { ids::sync, 1.0f }, { ids::decayNote, 6.0f },
                                      { ids::preNote, 7.0f }, { ids::modDepth, 50.0f }, { ids::lowCut, 250.0f }, { ids::highCut, 7000.0f },
                                      { ids::width, 160.0f }, { ids::ducking, 50.0f }, { ids::stutterMode, 2.0f }, { ids::stutterRate, 1.0f } } },
            { "Ramp Riser",         { { ids::mode, 4.0f }, { ids::mix, 40.0f }, { ids::sync, 1.0f }, { ids::decayNote, 6.0f },
                                      { ids::lowCut, 300.0f }, { ids::width, 150.0f }, { ids::ducking, 35.0f },
                                      { ids::stutterMode, 3.0f }, { ids::stutterRate, 1.0f } } },
            { "Infinite Abyss",     { { ids::mode, 5.0f }, { ids::mix, 50.0f }, { ids::preDelay, 0.0f }, { ids::decay, 20.0f },
                                      { ids::size, 100.0f }, { ids::modDepth, 45.0f }, { ids::lowCut, 300.0f }, { ids::highCut, 5000.0f },
                                      { ids::width, 180.0f } } },
        };
        return f;
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<float> snapshot;
};
} // namespace onirique
