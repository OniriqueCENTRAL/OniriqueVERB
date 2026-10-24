#pragma once

#include <juce_dsp/juce_dsp.h>
#include "License/LicenseOverlay.h"
#include "PluginProcessor.h"
#include "GUI/Theme.h"

// Le potard de VERB et les menus aux couleurs de l'instrument.
class ReverbLook : public juce::LookAndFeel_V4
{
public:
    ReverbLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    juce::Font getPopupMenuFont() override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
};

// Potard complet : nom sérigraphié au-dessus, valeur en dessous (un clic dessus pour la taper).
// Le MIX (hero) est plus grand, gradué de 0 à 100, avec sa valeur dans un affichage.
class Knob : public juce::Component
{
public:
    Knob (OniriqueReverbProcessor&, const char* paramId, const char* name, const char* tip, bool hero = false);   // textes en UTF-8
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> valueArea() const;
    void editValue();

    OniriqueReverbProcessor& proc;
    juce::RangedAudioParameter* param;
    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    juce::String name;
    bool hero;
    std::unique_ptr<juce::TextEditor> typing;
    onirique::ui::Fonts fonts;
};

// Une rangée de touches à témoin reliée à un paramètre à choix (les six espaces, le STUTTER).
class ModeSelector : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModeSelector (OniriqueReverbProcessor&, const char* paramId, const juce::StringArray& labels, const char* tip = "");
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick();

private:
    int indexAt (float x) const noexcept;
    juce::Rectangle<float> key (int i) const noexcept;

    juce::RangedAudioParameter* param;
    juce::ParameterAttachment attachment;
    juce::StringArray labels;
    std::vector<float> lit;
    int current = 1, hover = -1;
    onirique::ui::Fonts fonts;
};

// BPM : la touche SYNC (la queue et le pre-delay en valeurs de notes), le tempo dans son affichage (glisser,
// double-clic pour le taper), et sa source (DAW ou choisi).
class BpmControl : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit BpmControl (OniriqueReverbProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void tick();

private:
    int zoneAt (float x) const noexcept;
    void setManualBpm (float bpm);
    void askBpm();

    OniriqueReverbProcessor& proc;
    juce::RangedAudioParameter *syncParam, *sourceParam, *bpmParam;
    bool draggingValue = false;
    float dragStartBpm = 120.0f, shownBpm = 0.0f, lit = 0.0f;
    int shownSource = -1;
    onirique::ui::Fonts fonts;
};

// Le motif du STUTTER sur une mesure, avec la tête de lecture.
class StutterView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StutterView (OniriqueReverbProcessor&);
    void paint (juce::Graphics&) override;
    void tick();

private:
    OniriqueReverbProcessor& proc;
    double beat = 0.0;
    onirique::ui::Fonts fonts;
};

// L'affichage principal : l'espace de la reverb vu de face. Chaque anneau est un instant de la queue qui
// s'enfonce dans la profondeur ; la voix sèche passe devant en ruban. Glisser : SIZE (gauche / droite) et DECAY (haut / bas).
class Portal : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Portal (OniriqueReverbProcessor&);
    void paint (juce::Graphics&) override;
    void refresh (double seconds);
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    float historyAt (const std::vector<float>&, float ageSeconds) const noexcept;

    OniriqueReverbProcessor& proc;
    juce::RangedAudioParameter *sizeParam, *decayParam, *decayNoteParam, *dragDecay = nullptr;
    std::vector<float> wet, dry;   // niveaux, une valeur par image (30 par seconde), la plus récente en [head]
    int head = 0, lastWrite = 0;
    float wetPeak = 0.05f, dryPeak = 0.05f, wetNow = 0.0f, ringPhase = 0.0f, clock = 0.0f, frozen = 0.0f;
    struct Mote { float x, y, z; };
    std::vector<Mote> motes;
    juce::Random rng;
    juce::Image sigil;
    bool dragging = false, hovering = false;
    juce::Point<float> dragStart;
    float sizeStart = 0.0f, decayStart = 0.0f;
    onirique::ui::Fonts fonts;
};

// L'EQ de la reverb : IN (ce qui entre dans la reverb) et OUT (la queue), avec leur spectre en direct.
// Les points se tirent à la souris : coupes en largeur, grave / médium / aigu en largeur (fréquence) et en hauteur (gain).
// Double-clic : retour à zéro.
class EqView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit EqView (OniriqueReverbProcessor&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void tick (double seconds);

private:
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder;

    const onirique::EqIds& idsFor (bool out) const noexcept;
    onirique::reverb::Eq read (bool out) const noexcept;
    juce::RangedAudioParameter* freqParam (int band) const;
    juce::RangedAudioParameter* gainParam (int band) const;   // nullptr pour les coupes
    juce::Rectangle<float> plot() const noexcept;
    juce::Rectangle<float> tab (bool out) const noexcept;
    float xFor (float hz) const noexcept;
    float hzFor (float x) const noexcept;
    float yFor (float db) const noexcept;
    juce::Point<float> handle (int band) const;
    int bandAt (juce::Point<float>) const;

    OniriqueReverbProcessor& proc;
    bool editOut = true;
    int grabbed = -1, hover = -1;
    std::vector<float> seen;
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> scope, fftData, spectrum;   // spectrum : niveau lissé de chaque raie, en dB
    bool spectrumAlive = false;
    onirique::ui::Fonts fonts;
};

// Touches : INFINITE (grande touche à témoin), BYPASS (marche / arrêt), cadenas du MIX.
class GlowToggle : public juce::ToggleButton
{
public:
    enum class Style { infinite, power, lock };
    GlowToggle (Style s, const char* tip) : style (s) { setClickingTogglesState (true); setTooltip (juce::String::fromUTF8 (tip)); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick();

private:
    Style style;
    float lit = 0.0f, pulse = 0.0f;
    onirique::ui::Fonts fonts;
};

// Un choix dans un petit affichage (divisions de PULSE et du STUTTER) : un clic ouvre la liste.
class ChoicePill : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChoicePill (OniriqueReverbProcessor&, const char* paramId, const char* tip);
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::AudioParameterChoice* param;
    juce::ParameterAttachment attachment;
    onirique::ui::Fonts fonts;
};

// Les presets : touche précédent, nom dans son affichage (menu), touche suivant.
class PresetBar : public juce::Component
{
public:
    explicit PresetBar (OniriqueReverbProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick();

private:
    void openMenu();
    void saveAs();

    OniriqueReverbProcessor& proc;
    juce::String shownName;
    bool shownModified = false;
    onirique::ui::Fonts fonts;
};

//==============================================================================
class OniriqueReverbEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int baseWidth = 1180, baseHeight = 900;

    explicit OniriqueReverbEditor (OniriqueReverbProcessor&);
    ~OniriqueReverbEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshDisplays (double seconds);

private:
    struct Content;
    void timerCallback() override { refreshDisplays (1.0 / 30.0); }

    OniriqueReverbProcessor& proc;
    ReverbLook look;
    std::unique_ptr<Content> content;
    std::unique_ptr<onirique::license::Overlay> licenseOverlay;
    juce::TooltipWindow tooltips { this, 600 };
    bool sizeRestored = false;
};
