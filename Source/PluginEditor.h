#pragma once

#include "License/LicenseOverlay.h"
#include "PluginProcessor.h"
#include "GUI/Theme.h"

// Le potard ONIRIQUE et les menus aux couleurs de la marque.
class ReverbLook : public juce::LookAndFeel_V4
{
public:
    ReverbLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    juce::Font getPopupMenuFont() override;
};

// Potard complet : nom au-dessus, valeur en dessous. Le MIX (hero) a son anneau sec / mouillé et sa grande valeur.
class Knob : public juce::Component
{
public:
    Knob (OniriqueReverbProcessor&, const char* paramId, const char* name, const char* tip, bool hero = false);   // textes en UTF-8
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    OniriqueReverbProcessor& proc;
    juce::RangedAudioParameter* param;
    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    juce::String name;
    bool hero;
    onirique::ui::Fonts fonts;
};

// Sélecteur segmenté relié à un paramètre à choix (les six modes, le STUTTER) : la pastille braise glisse vers le choix.
class ModeSelector : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModeSelector (OniriqueReverbProcessor&, const char* paramId, const juce::StringArray& labels, const char* tip = "");
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick();

private:
    juce::RangedAudioParameter* param;
    juce::ParameterAttachment attachment;
    juce::StringArray labels;
    float shown = 1.0f;
    int current = 1;
    onirique::ui::Fonts fonts;
};

// BPM : allumé, la queue et le pre-delay se calculent au tempo. Le tempo vient du DAW, ou se choisit à la main
// (glisser sur le chiffre, double-clic pour le taper).
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

// LE PORTAIL : l'espace de la reverb vu de face. Chaque anneau est un instant de la queue qui s'enfonce dans la
// profondeur ; la voix sèche passe devant en ruban. Glisser : SIZE (gauche / droite) et DECAY (haut / bas).
class Portal : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Portal (OniriqueReverbProcessor&);
    void paint (juce::Graphics&) override;
    void refresh (double seconds);
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
    float wetPeak = 0.05f, dryPeak = 0.05f, wetNow = 0.0f, ringPhase = 0.0f, spin = 0.0f, clock = 0.0f, frozen = 0.0f;
    struct Mote { float x, y, z; };
    std::vector<Mote> motes;
    juce::Random rng;
    juce::Image sigil;
    bool dragging = false;
    juce::Point<float> dragStart;
    float sizeStart = 0.0f, decayStart = 0.0f;
    onirique::ui::Fonts fonts;
};

// L'EQ de la reverb : IN (ce qui entre dans la reverb) et OUT (la queue). Les points se tirent à la souris :
// coupes en largeur, grave / médium / aigu en largeur (fréquence) et en hauteur (gain). Double-clic : retour à zéro.
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
    void tick();

private:
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
    onirique::ui::Fonts fonts;
};

// Interrupteurs : INFINITE (grande carte), BYPASS (power), cadenas du MIX.
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

// Choix dans une pastille (division de PULSE) : un clic ouvre la liste.
class ChoicePill : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChoicePill (OniriqueReverbProcessor&, const char* paramId, const char* tip);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::AudioParameterChoice* param;
    juce::ParameterAttachment attachment;
    onirique::ui::Fonts fonts;
};

// Barre des presets : précédent / nom (menu) / suivant.
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
