#include "PluginEditor.h"
#include "GUI/OniriqueWindowSize.h"

using namespace onirique;
using namespace onirique::ui;
namespace pal = onirique::ui::palette;

namespace
{
constexpr float pi = juce::MathConstants<float>::pi, twoPi = juce::MathConstants<float>::twoPi;

bool approach (float& v, float target, float k) noexcept
{
    if (std::abs (target - v) < 1.0e-3f) { const bool moved = v != target; v = target; return moved; }
    v += k * (target - v);
    return true;
}

juce::Point<float> polar (juce::Point<float> c, float angle, float r) noexcept
{
    return { c.x + r * std::sin (angle), c.y - r * std::cos (angle) };   // angles de JUCE : 0 en haut, sens horaire
}

juce::Rectangle<float> circle (juce::Point<float> c, float r) noexcept { return juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c); }

// texte sérigraphié sur la façade
void ink (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j, float size = 10.5f, float alpha = 0.72f)
{
    g.setColour (pal::ink.withAlpha (alpha));
    g.setFont (Fonts()->bodyFont (size, 0.18f));
    g.drawText (text, r, j, false);
}

// rainure gravée dans le métal : un trait d'ombre, puis la lèvre éclairée juste dessous (ou à droite)
void groove (juce::Graphics& g, float x1, float y1, float x2, float y2)
{
    const bool horizontal = std::abs (y2 - y1) < 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawLine (x1, y1, x2, y2, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.065f));
    g.drawLine (x1 + (horizontal ? 0.0f : 1.0f), y1 + (horizontal ? 1.0f : 0.0f), x2 + (horizontal ? 0.0f : 1.0f), y2 + (horizontal ? 1.0f : 0.0f), 1.0f);
}

// titre de section : le nom, puis une rainure jusqu'au bout de la section
void sectionTitle (juce::Graphics& g, const juce::String& text, float x, float y, float right)
{
    const auto font = Fonts()->bodyFont (10.5f, 0.26f);
    ink (g, text, { x, y - 8.0f, 300.0f, 16.0f }, juce::Justification::centredLeft, 10.5f, 0.62f);
    groove (g, x + juce::GlyphArrangement::getStringWidth (font, text) + 12.0f, y, right, y);
}

// fenêtre d'affichage : verre fumé enchâssé (le fond ; le contenu se dessine ensuite, puis glassFront)
void glassBack (juce::Graphics& g, juce::Rectangle<float> r, float radius = 5.0f)
{
    g.setColour (pal::glass);
    g.fillRoundedRectangle (r, radius);
}

// par-dessus le contenu : l'ombre du bord haut, un reflet en biais, le cadre (ombre dedans, lèvre éclairée dehors)
void glassFront (juce::Graphics& g, juce::Rectangle<float> r, float radius = 5.0f)
{
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, radius);
        g.reduceClipRegion (clip);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), 0.0f, r.getY(),
                                                 juce::Colours::transparentBlack, 0.0f, r.getY() + juce::jmin (16.0f, r.getHeight() * 0.3f), false));
        g.fillRect (r);
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY(),
                                    juce::Colours::white.withAlpha (0.0f), r.getX() + r.getHeight() * 1.4f, r.getBottom(), false);
        sheen.addColour (0.42, juce::Colours::white.withAlpha (0.0f));
        sheen.addColour (0.5, juce::Colours::white.withAlpha (0.028f));
        sheen.addColour (0.62, juce::Colours::white.withAlpha (0.0f));
        g.setGradientFill (sheen);
        g.fillRect (r);
    }
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    juce::Path lip;
    lip.addRoundedRectangle (r.expanded (1.0f), radius + 1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), 0.0f, r.getY(),
                                             juce::Colours::white.withAlpha (0.1f), 0.0f, r.getBottom() + 1.0f, false));
    g.strokePath (lip, juce::PathStrokeType (1.0f));
}

// touche : son logement creusé dans la façade, puis le capuchon de métal sombre éclairé d'en haut ;
// enfoncée, la lumière s'inverse (le capuchon est sous le bord du logement)
void keyCap (juce::Graphics& g, juce::Rectangle<float> r, bool down, bool hover, float radius = 4.0f)
{
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRoundedRectangle (r.expanded (2.0f), radius + 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (r.expanded (2.5f), radius + 2.5f, 0.8f);
    const auto cap = down ? r.reduced (0.5f).translated (0.0f, 0.5f) : r;
    const auto top = juce::Colour (0xff2c2423).brighter (hover ? 0.12f : 0.0f), bottom = juce::Colour (0xff141010).brighter (hover ? 0.12f : 0.0f);
    g.setGradientFill (juce::ColourGradient (down ? bottom : top, 0.0f, cap.getY(), down ? top.darker (0.2f) : bottom, 0.0f, cap.getBottom(), false));
    g.fillRoundedRectangle (cap, radius);
    if (! down)
    {
        g.setColour (juce::Colours::white.withAlpha (0.11f));
        g.drawLine (cap.getX() + radius, cap.getY() + 0.8f, cap.getRight() - radius, cap.getY() + 0.8f, 1.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (cap, radius, 1.0f);
}

// témoin : la lentille éteinte, la braise qui s'allume et sa lueur, le reflet
void led (juce::Graphics& g, juce::Point<float> c, float level, float r = 2.8f)
{
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillEllipse (circle (c, r + 1.0f));
    if (level > 0.02f)
    {
        g.setGradientFill (juce::ColourGradient (pal::ember.withAlpha (0.4f * level), c.x, c.y, pal::ember.withAlpha (0.0f), c.x + r * 4.5f, c.y, true));
        g.fillEllipse (circle (c, r * 4.5f));
    }
    g.setColour (pal::ledOff.interpolatedWith (pal::emberHot, level));
    g.fillEllipse (circle (c, r));
    g.setColour (juce::Colours::white.withAlpha (0.18f + 0.5f * level));
    g.fillEllipse (circle (c.translated (-r * 0.35f, -r * 0.35f), r * 0.32f));
}

// petit triangle de menu
void menuArrow (juce::Graphics& g, float x, float y, juce::Colour c)
{
    juce::Path tri;
    tri.addTriangle (x, y - 2.0f, x + 7.0f, y - 2.0f, x + 3.5f, y + 2.5f);
    g.setColour (c);
    g.fillPath (tri);
}

/*  Le potard de VERB : un bouton d'aluminium noir usiné posé sur la façade.
    - une échelle sérigraphiée, et dans la piste gravée la braise qui va de l'origine à la valeur ;
    - une jupe moletée, éclairée d'en haut ;
    - un chapeau tourné (reflets en croix du métal usiné), et l'index gravé qui va jusque sur la jupe. */
void drawVerbKnob (juce::Graphics& g, juce::Rectangle<float> area, float pos, float start, float end, bool hot)
{
    const float R = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 1.0f;
    if (R < 8.0f) return;
    const auto c = area.getCentre();
    const float angle = start + juce::jlimit (0.0f, 1.0f, pos) * (end - start);
    const bool big = R > 55.0f;

    // l'échelle
    const int count = big ? 40 : 20;
    for (int i = 0; i <= count; ++i)
    {
        const float a = start + (float) i / (float) count * (end - start);
        const bool major = i % 5 == 0;
        g.setColour (pal::ink.withAlpha (major ? 0.5f : 0.2f));
        g.drawLine (juce::Line<float> (polar (c, a, R - R * (major ? 0.11f : 0.07f)), polar (c, a, R)), major ? 1.3f : 1.0f);
    }

    // la piste et la valeur
    const float rt = R * 0.81f, tw = juce::jmax (2.2f, R * 0.045f);
    juce::Path track, value;
    track.addCentredArc (c.x, c.y, rt, rt, 0.0f, start, end, true);
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.strokePath (track, juce::PathStrokeType (tw + 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (angle > start + 1.0e-3f)
    {
        value.addCentredArc (c.x, c.y, rt, rt, 0.0f, start, angle, true);
        g.setColour (pal::ember.withAlpha (hot ? 0.3f : 0.16f));
        g.strokePath (value, juce::PathStrokeType (tw + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (hot ? pal::emberHot : pal::ember);
        g.strokePath (value, juce::PathStrokeType (tw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // l'ombre portée du bouton
    const float rk = R * 0.66f;
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.7f), c.x, c.y + rk * 0.2f,
                                             juce::Colours::transparentBlack, c.x + rk * 1.35f, c.y + rk * 0.2f, true));
    g.fillEllipse (circle (c.translated (0.0f, rk * 0.2f), rk * 1.35f));

    // la jupe moletée
    g.setGradientFill (juce::ColourGradient (pal::metalHi.brighter (0.15f), c.x, c.y - rk, pal::metalLo, c.x, c.y + rk, false));
    g.fillEllipse (circle (c, rk));
    const int ridges = big ? 90 : 56;
    for (int j = 0; j < ridges; ++j)
    {
        const float a = (float) j / (float) ridges * twoPi;
        const float light = juce::jmax (0.0f, -std::cos (a - 0.35f));
        g.setColour (juce::Colours::white.withAlpha (0.02f + 0.11f * light * light));
        g.drawLine (juce::Line<float> (polar (c, a, rk * 0.86f), polar (c, a, rk - 0.7f)), 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawLine (juce::Line<float> (polar (c, a + pi / (float) ridges, rk * 0.86f), polar (c, a + pi / (float) ridges, rk - 0.7f)), 1.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawEllipse (circle (c, rk), 1.0f);

    // le chapeau tourné
    const float rc = rk * 0.8f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff120e0e), c.x, c.y - rc, juce::Colour (0xff2c2422), c.x, c.y + rc, false));
    g.fillEllipse (circle (c, rc));
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addEllipse (circle (c, rc));
        g.reduceClipRegion (clip);
        const int wedges = 72;
        for (int s = 0; s < wedges; ++s)
        {
            const float a0 = (float) s / (float) wedges * twoPi, a1 = a0 + twoPi / (float) wedges + 0.01f;
            const float sheen = std::pow (std::abs (std::cos (a0 + 0.5f * twoPi / (float) wedges - 0.75f)), 7.0f);
            if (sheen < 0.02f) continue;
            juce::Path wedge;
            wedge.addPieSegment (circle (c, rc), a0, a1, 0.0f);
            g.setColour (juce::Colours::white.withAlpha (0.11f * sheen));
            g.fillPath (wedge);
        }
        for (float rr = rc * 0.12f; rr < rc; rr += juce::jmax (1.6f, rc / 20.0f))
        {
            g.setColour (juce::Colours::black.withAlpha (0.08f));
            g.drawEllipse (circle (c, rr), 0.7f);
        }
    }
    juce::Path rim;
    rim.addEllipse (circle (c, rc));
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.24f), c.x, c.y - rc, juce::Colours::black.withAlpha (0.4f), c.x, c.y + rc, false));
    g.strokePath (rim, juce::PathStrokeType (1.1f));

    // l'index gravé, prolongé sur la jupe
    const float iw = juce::jmax (1.7f, R * 0.034f);
    const auto across = polar ({}, angle + pi * 0.5f, 1.0f);
    const auto p0 = polar (c, angle, rc * 0.2f), p1 = polar (c, angle, rc * 0.9f);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawLine (juce::Line<float> (p0 + across, p1 + across), iw);
    g.setColour (pal::bone.withAlpha (0.95f));
    g.drawLine (juce::Line<float> (p0, p1), iw);
    g.setColour (pal::bone.withAlpha (0.6f));
    g.drawLine (juce::Line<float> (polar (c, angle, rk * 0.88f), polar (c, angle, rk * 0.99f)), juce::jmax (1.3f, iw * 0.8f));
}
} // namespace

//==============================================================================
ReverbLook::ReverbLook()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff160f0f));
    setColour (juce::PopupMenu::textColourId, pal::ink);
    setColour (juce::PopupMenu::headerTextColourId, pal::inkDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, pal::ember.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId, pal::bone);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff160f0f));
    setColour (juce::TooltipWindow::textColourId, pal::ink);
    setColour (juce::TooltipWindow::outlineColourId, juce::Colours::black);
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff160f0f));
    setColour (juce::AlertWindow::textColourId, pal::ink);
    setColour (juce::AlertWindow::outlineColourId, juce::Colours::black);
    setColour (juce::TextEditor::backgroundColourId, pal::glass);
    setColour (juce::TextEditor::textColourId, pal::emberHot);
    setColour (juce::TextEditor::highlightColourId, pal::ember.withAlpha (0.35f));
    setColour (juce::TextEditor::outlineColourId, juce::Colours::black);
    setColour (juce::TextEditor::focusedOutlineColourId, pal::ember.withAlpha (0.6f));
    setColour (juce::CaretComponent::caretColourId, pal::emberHot);
    setColour (juce::TextButton::buttonColourId, pal::metalHi);
    setColour (juce::TextButton::textColourOffId, pal::ink);
}

void ReverbLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    drawVerbKnob (g, juce::Rectangle<int> (x, y, w, h).toFloat(), pos, start, end, s.isMouseOverOrDragging());
}

juce::Font ReverbLook::getPopupMenuFont()
{
    return Fonts()->bodyFont (15.0f);
}

// le coin pour agrandir la fenêtre : trois traits gravés
void ReverbLook::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    for (int i = 1; i <= 3; ++i)
    {
        const float d = (float) i * (float) juce::jmin (w, h) / 4.0f;
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawLine ((float) w - d, (float) h, (float) w, (float) h - d, 1.0f);
        g.setColour ((isMouseOver || isMouseDragging ? pal::emberHot : pal::ink).withAlpha (isMouseOver || isMouseDragging ? 0.8f : 0.25f));
        g.drawLine ((float) w - d + 1.0f, (float) h, (float) w, (float) h - d + 1.0f, 1.0f);
    }
}

//==============================================================================
Knob::Knob (OniriqueReverbProcessor& p, const char* id, const char* n, const char* tip, bool h)
    : proc (p), param (p.apvts.getParameter (id)), attachment (p.apvts, id, slider), name (juce::String::fromUTF8 (n)), hero (h)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (pi * 1.25f, pi * 2.75f, true);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.setMouseDragSensitivity (hero ? 320 : 220);
    slider.setVelocityModeParameters (0.25, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // Maj : réglage fin
    slider.setTooltip (juce::String::fromUTF8 (tip) + juce::String::fromUTF8 ("\n\nMaj : réglage fin. Double-clic : valeur d'origine. Clic sur la valeur : la taper."));
    slider.setTitle (name);
    slider.onValueChange = [this] { repaint(); };
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (hero ? 20 : 16);
    r.removeFromBottom (hero ? 40 : 20);
    const int d = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (d, d));
    if (typing != nullptr) typing->setBounds (valueArea().toNearestInt());
}

juce::Rectangle<float> Knob::valueArea() const
{
    const auto b = getLocalBounds().toFloat();
    if (hero) return juce::Rectangle<float> (112.0f, 30.0f).withCentre ({ b.getCentreX(), b.getBottom() - 18.0f });
    return b.withTop (b.getBottom() - 18.0f).reduced (6.0f, 0.0f);
}

void Knob::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == this && valueArea().contains (e.position) && ! e.mouseWasDraggedSinceMouseDown())
        editValue();
}

// la valeur tapée au clavier : « 120 », « 2,5 », « 1.2k », ou un nom de note (« 1/8 ») pour les réglages au tempo
void Knob::editValue()
{
    typing = std::make_unique<juce::TextEditor>();
    typing->setFont (fonts->monoBoldFont (hero ? 17.0f : 11.5f));
    typing->setJustification (juce::Justification::centred);
    typing->setIndents (2, 0);
    typing->setText (param->getCurrentValueAsText().upToFirstOccurrenceOf (" ", false, false), false);
    typing->selectAll();
    addAndMakeVisible (*typing);
    typing->setBounds (valueArea().toNearestInt());
    typing->grabKeyboardFocus();

    auto close = [safe = juce::Component::SafePointer<Knob> (this)] (bool apply)
    {
        if (safe == nullptr || safe->typing == nullptr) return;
        if (apply)
        {
            auto* p = safe->param;
            const auto text = safe->typing->getText().trim().replaceCharacter (',', '.');
            float norm;
            if (dynamic_cast<juce::AudioParameterChoice*> (p) != nullptr)
                norm = p->getValueForText (text);
            else
            {
                float v = text.getFloatValue();
                if (text.containsIgnoreCase ("k")) v *= 1000.0f;
                norm = p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (v));
            }
            if (text.isNotEmpty())
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (norm);
                p->endChangeGesture();
            }
        }
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->typing.reset(); safe->repaint(); } });
    };
    typing->onReturnKey = [close] { close (true); };
    typing->onEscapeKey = [close] { close (false); };
    typing->onFocusLost = [close] { close (true); };
    repaint();
}

void Knob::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const bool hot = slider.isMouseOverOrDragging() || isMouseOver (true);
    ink (g, name, b.withHeight (hero ? 18.0f : 15.0f), juce::Justification::centred, hero ? 12.0f : 10.5f, hot ? 0.95f : 0.7f);

    if (! hero)
    {
        if (typing == nullptr)
        {
            g.setColour (hot ? pal::emberHot : pal::mist.withAlpha (0.82f));
            g.setFont (fonts->monoFont (11.0f));
            g.drawText (param->getCurrentValueAsText(), valueArea(), juce::Justification::centred, false);
        }
        return;
    }

    // MIX : les chiffres de l'échelle, puis la valeur dans son affichage, avec la part de voix et de reverb
    const auto sb = slider.getBounds().toFloat();
    const auto c = sb.getCentre();
    const float R = sb.getWidth() * 0.5f + 11.0f;
    g.setFont (fonts->monoFont (9.5f));
    g.setColour (pal::ink.withAlpha (0.5f));
    for (auto [a, t] : { std::pair { pi * 1.25f, "0" }, std::pair { pi * 2.75f, "100" } })
        g.drawText (t, circle (polar (c, a, R), 12.0f), juce::Justification::centred, false);

    const auto win = valueArea();
    glassBack (g, win);
    if (typing == nullptr)
    {
        g.setColour (hot ? pal::emberHot : pal::ember.brighter (0.15f));
        g.setFont (fonts->monoBoldFont (17.0f));
        g.drawText (juce::String (juce::roundToInt (proc.value (ids::mix))) + " %", win.withTrimmedBottom (4.0f), juce::Justification::centred, false);
    }
    reverb::Params rp;
    rp.mix = proc.value (ids::mix) * 0.01f;
    const auto gains = reverb::Engine::mixGains (rp);
    const float half = win.getWidth() * 0.5f - 8.0f, y = win.getBottom() - 5.0f;
    g.setColour (pal::bone.withAlpha (0.75f));
    g.fillRect (juce::Rectangle<float> (win.getCentreX() - 1.0f - half * gains[0], y, half * gains[0], 2.0f));
    g.setColour (pal::emberHot);
    g.fillRect (juce::Rectangle<float> (win.getCentreX() + 1.0f, y, half * gains[1], 2.0f));
    glassFront (g, win);

    ink (g, "SEC", { win.getX() - 64.0f, win.getY(), 56.0f, win.getHeight() }, juce::Justification::centredRight, 9.5f, 0.6f);
    ink (g, "REVERB", { win.getRight() + 8.0f, win.getY(), 70.0f, win.getHeight() }, juce::Justification::centredLeft, 9.5f, 0.6f);
}

//==============================================================================
ModeSelector::ModeSelector (OniriqueReverbProcessor& p, const char* id, const juce::StringArray& l, const char* tip)
    : param (p.apvts.getParameter (id)),
      attachment (*param, [this] (float v) { current = juce::roundToInt (v); repaint(); }),
      labels (l)
{
    setTooltip (juce::String::fromUTF8 (tip));
    attachment.sendInitialUpdate();
    lit.assign ((size_t) labels.size(), 0.0f);
    if (juce::isPositiveAndBelow (current, labels.size())) lit[(size_t) current] = 1.0f;
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> ModeSelector::key (int i) const noexcept
{
    constexpr float gap = 8.0f;
    const float w = ((float) getWidth() - gap * (float) (labels.size() - 1)) / (float) labels.size();
    return juce::Rectangle<float> ((float) i * (w + gap), 0.0f, w, (float) getHeight()).reduced (2.0f);
}

int ModeSelector::indexAt (float x) const noexcept
{
    return juce::jlimit (0, labels.size() - 1, (int) (x / ((float) getWidth() / (float) labels.size())));
}

void ModeSelector::tick()
{
    bool moving = false;
    for (int i = 0; i < labels.size(); ++i)
        moving |= approach (lit[(size_t) i], i == current ? 1.0f : 0.0f, 0.3f);
    if (moving) repaint();
}

void ModeSelector::mouseMove (const juce::MouseEvent& e)
{
    if (const int h = indexAt (e.position.x); h != hover) { hover = h; repaint(); }
}

void ModeSelector::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void ModeSelector::mouseDown (const juce::MouseEvent& e)
{
    attachment.setValueAsCompleteGesture ((float) indexAt (e.position.x));
}

void ModeSelector::paint (juce::Graphics& g)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto k = key (i);
        const float l = lit[(size_t) i];
        keyCap (g, k, i == current, i == hover);
        led (g, { k.getX() + 13.0f, k.getCentreY() }, l);
        g.setFont (fonts->bodyFont (10.5f, 0.2f));
        g.setColour (pal::inkDim.interpolatedWith (pal::bone, juce::jmax (l, i == hover ? 0.55f : 0.0f)));
        g.drawText (labels[i], k.withTrimmedLeft (20.0f).withTrimmedRight (4.0f), juce::Justification::centred, false);
    }
}

//==============================================================================
Portal::Portal (OniriqueReverbProcessor& p)
    : proc (p), sizeParam (p.apvts.getParameter (ids::size)), decayParam (p.apvts.getParameter (ids::decay)),
      decayNoteParam (p.apvts.getParameter (ids::decayNote))
{
    wet.assign (1024, 0.0f);
    dry.assign (1024, 0.0f);
    for (int i = 0; i < 60; ++i)
        motes.push_back ({ rng.nextFloat() * 2.0f - 1.0f, rng.nextFloat() * 2.0f - 1.0f, 0.1f + 0.9f * rng.nextFloat() });
    sigil = juce::ImageCache::getFromMemory (BinaryData::sigil_png, BinaryData::sigil_pngSize);
    lastWrite = p.getEngine().getWritePos();
    setOpaque (true);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setTooltip (juce::String::fromUTF8 ("L'espace de la reverb vu de face. Chaque anneau est un instant de la queue qui s'enfonce dans la "
                "profondeur ; la voix sèche passe devant. Glisser à gauche / à droite : SIZE. Glisser vers le haut / le bas : DECAY "
                "(Maj : réglage fin). Double-clic : valeurs d'origine."));
}

float Portal::historyAt (const std::vector<float>& h, float age) const noexcept
{
    const float frames = juce::jmax (0.0f, age * 30.0f);
    const int i = (int) frames;
    const float f = frames - (float) i;
    const int n = (int) h.size();
    if (i >= n - 1) return 0.0f;
    const float a = h[(size_t) ((head - i + n) % n)], b = h[(size_t) ((head - i - 1 + n) % n)];
    return a + f * (b - a);
}

void Portal::refresh (double seconds)
{
    const float dt = (float) seconds;
    const auto& e = proc.getEngine();
    const int w = e.getWritePos();
    int count = (w - lastWrite + reverb::Engine::kHistory) % reverb::Engine::kHistory;
    float dw = 0.0f, dd = 0.0f;
    for (int k = 0; k < count; ++k)
    {
        dw = juce::jmax (dw, e.getWet (lastWrite + k));
        dd = juce::jmax (dd, e.getDry (lastWrite + k));
    }
    lastWrite = w;

    head = (head + 1) % (int) wet.size();
    wet[(size_t) head] = dw;
    dry[(size_t) head] = dd;
    // échelle automatique : les anneaux restent lisibles, voix chuchotée ou criée
    wetPeak = juce::jmax (0.03f, dw, wetPeak * std::pow (0.5f, dt / 3.0f));
    dryPeak = juce::jmax (0.03f, dd, dryPeak * std::pow (0.5f, dt / 3.0f));
    wetNow += 0.25f * (dw / wetPeak - wetNow);

    const bool freeze = proc.value (ids::freeze) > 0.5f;
    approach (frozen, freeze ? 1.0f : 0.0f, 0.08f);
    const float window = juce::jlimit (0.5f, 12.0f, proc.effectiveDecay());
    if (! freeze)
    {
        ringPhase += dt / (window / 26.0f);
        ringPhase -= std::floor (ringPhase);
    }
    clock += dt;

    const int mode = juce::roundToInt (proc.value (ids::mode));
    const bool sparkle = mode == reverb::shimmer || mode == reverb::dream;
    for (auto& m : motes)
    {
        m.z -= dt * (freeze ? 0.03f : 0.09f + 0.25f * wetNow);
        if (sparkle) m.y -= dt * 0.12f;
        if (m.z < 0.08f || m.y < -1.5f)
            m = { rng.nextFloat() * 2.0f - 1.0f, rng.nextFloat() * 2.0f - 1.0f, 0.9f + 0.1f * rng.nextFloat() };
    }
    repaint();
}

void Portal::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (pal::plateTop, 0.0f, -74.0f, pal::plateBottom, 0.0f, 826.0f, false));   // la façade autour du verre
    g.fillAll();
    const auto b = all.reduced (2.0f);
    glassBack (g, b, 7.0f);

    g.saveState();
    juce::Path frame;
    frame.addRoundedRectangle (b, 7.0f);
    g.reduceClipRegion (frame);

    const auto c = b.getCentre();
    const float glow = juce::jlimit (0.0f, 1.0f, wetNow);
    const int mode = juce::roundToInt (proc.value (ids::mode));
    const float size = proc.value (ids::size) * 0.01f, width = proc.value (ids::width) * 0.01f;
    const float window = juce::jlimit (0.5f, 12.0f, proc.effectiveDecay());
    const float pre = proc.effectivePreMs() * 0.001f;
    const float diffusion = proc.value (ids::diffusion) * 0.01f;
    const float wobble = 0.006f + 0.05f * proc.value (ids::modDepth) * 0.01f * (mode == reverb::dream ? 2.0f : 1.0f);
    const float modPhase = clock * twoPi * juce::jmin (2.0f, proc.value (ids::modRate)) * 0.5f;
    const float bright = std::log (proc.value (ids::highCut) / 1000.0f) / std::log (20.0f);

    // la lueur du fond : la queue éclaire le verre de l'intérieur
    {
        juce::ColourGradient bg (pal::bloodDeep.withMultipliedBrightness (0.7f + 1.1f * glow), c.x, c.y,
                                 pal::glass, c.x + b.getWidth() * 0.5f, c.y, true);
        g.setGradientFill (bg);
        g.fillRect (b);
    }

    // les anneaux : du plus lointain au plus proche
    constexpr int numRings = 26;
    const float depth = 1.5f + 9.0f * size;
    const float near = b.getHeight() * 0.95f;
    const float aspect = 1.5f + 0.9f * width;
    for (int j = numRings - 1; j >= 0; --j)
    {
        const float slot = (float) j + ringPhase;
        const float z = slot / (float) numRings;
        const float age = pre + z * window;
        const float level = juce::jlimit (0.0f, 1.0f, std::sqrt (historyAt (wet, age) / wetPeak));
        const float persp = 1.0f / (1.0f + depth * z);
        const float ry = near * persp * 0.5f, rx = ry * aspect;

        float alpha = (0.06f + 0.94f * level) * std::pow (1.0f - z, 0.7f);
        if (z < 0.06f) alpha *= z / 0.06f;
        alpha = juce::jmin (1.0f, alpha * (1.0f + 0.6f * frozen * (0.5f + 0.5f * std::sin (clock * 2.0f + (float) j * 0.4f))));
        if (alpha < 0.01f) continue;

        auto col = pal::bone.interpolatedWith (pal::ember, juce::jlimit (0.0f, 1.0f, z * 1.8f - 0.35f * bright + 0.2f));
        col = col.interpolatedWith (pal::blood.brighter (0.25f), juce::jlimit (0.0f, 1.0f, (z - 0.55f) * 2.2f));
        col = col.interpolatedWith (pal::bone, 0.7f * frozen);

        juce::Path ring;
        const float ph = modPhase + (float) j * 0.7f;
        for (int i = 0; i <= 72; ++i)
        {
            const float th = (float) i / 72.0f * twoPi;
            const float k = 1.0f + wobble * (std::sin (3.0f * th + ph) + 0.5f * std::sin (5.0f * th - ph * 1.3f));
            const juce::Point<float> pt (c.x + rx * k * std::cos (th), c.y + ry * k * std::sin (th));
            if (i == 0) ring.startNewSubPath (pt); else ring.lineTo (pt);
        }
        ring.closeSubPath();

        const float strokeW = (0.6f + 2.0f * level) * (0.4f + 0.6f * persp);
        if (diffusion < 0.55f)   // peu de diffusion : on devine les échos séparés
        {
            juce::Path dashed;
            const float dash[] { 2.0f + 14.0f * diffusion * persp, 4.0f + 10.0f * (1.0f - diffusion) * persp };
            juce::PathStrokeType (strokeW).createDashedStroke (dashed, ring, dash, 2);
            g.setColour (col.withAlpha (alpha));
            g.fillPath (dashed);
            continue;
        }
        if (level > 0.35f && persp > 0.35f)   // le phosphore bave un peu autour des anneaux les plus vifs
        {
            g.setColour (col.withAlpha (alpha * 0.14f));
            g.strokePath (ring, juce::PathStrokeType (strokeW + 5.0f * level * persp));
        }
        g.setColour (col.withAlpha (alpha));
        g.strokePath (ring, juce::PathStrokeType (strokeW));
    }

    // la poussière : elle vient vers nous (et monte en SHIMMER / ONIRIQUE)
    for (const auto& m : motes)
    {
        const juce::Point<float> pt (c.x + m.x / m.z * b.getWidth() * 0.09f, c.y + m.y / m.z * b.getHeight() * 0.11f);
        const float s = juce::jmin (4.0f, 0.8f / m.z);
        const float a = juce::jlimit (0.0f, 1.0f, (1.0f - m.z) * 1.3f) * (0.3f + 0.7f * glow);
        g.setColour (pal::rose.withAlpha (a * 0.75f));
        g.fillEllipse (circle (pt, s * 0.5f));
    }

    // la voix sèche : un ruban de fils qui part du centre, la voix la plus récente au cœur
    {
        const float span = b.getWidth() * 0.5f - 12.0f;
        for (int s = 0; s < 5; ++s)
        {
            juce::Path ribbon;
            for (int side = -1; side <= 1; side += 2)
                for (int i = 0; i <= 90; ++i)
                {
                    const int k = side < 0 ? 90 - i : i;
                    const float u = (float) k / 90.0f;
                    const float amp = juce::jlimit (0.0f, 1.0f, historyAt (dry, u * 1.6f) / dryPeak) * b.getHeight() * 0.30f * (1.0f - 0.6f * u);
                    const float y = c.y + amp * std::sin (u * 13.0f - clock * 3.1f + (float) s * 0.6f) * (1.0f - 0.13f * (float) s);
                    const juce::Point<float> pt (c.x + (float) side * u * span, y);
                    if (side < 0 && i == 0) ribbon.startNewSubPath (pt); else ribbon.lineTo (pt);
                }
            const bool main = s == 2;
            if (main)
            {
                g.setColour (pal::rose.withAlpha (0.1f));
                g.strokePath (ribbon, juce::PathStrokeType (4.0f));
            }
            g.setColour (pal::bone.withAlpha (main ? 0.85f : 0.22f));
            g.strokePath (ribbon, juce::PathStrokeType (main ? 1.3f : 0.8f));
        }
    }

    // le sigil au point de fuite
    {
        const float halo = 60.0f + 50.0f * glow;
        g.setGradientFill (juce::ColourGradient (pal::ember.withAlpha (0.35f * (0.3f + glow)), c.x, c.y, pal::ember.withAlpha (0.0f), c.x + halo, c.y, true));
        g.fillEllipse (circle (c, halo));
        const float rr = 26.0f;
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillEllipse (circle (c, rr));
        g.setColour (pal::rose.withAlpha (0.22f));
        g.drawEllipse (circle (c, rr), 1.0f);
        const float ss = 34.0f * (1.0f + 0.1f * glow);
        g.setOpacity (0.7f + 0.3f * glow);
        g.drawImage (sigil, juce::Rectangle<float> (ss, ss).withCentre (c), juce::RectanglePlacement::centred);
        g.setOpacity (1.0f);
    }

    // les inscriptions de l'affichage
    {
        static const char* modeLabels[] { "ROOM", "PLATE I", "PLATE II", "HALL", "SHIMMER", "ONIRIQUE" };
        const auto top = b.reduced (16.0f, 10.0f).withHeight (18.0f);
        g.setFont (fonts->monoBoldFont (11.0f, 0.18f));
        g.setColour (pal::emberHot.withAlpha (0.85f));
        g.drawText (modeLabels[juce::jlimit (0, 5, mode)], top, juce::Justification::centredLeft, false);

        const auto tag = [&] (const char* label, const char* id) { return juce::String (label) + " " + proc.apvts.getParameter (id)->getCurrentValueAsText(); };
        const bool sync = proc.isSynced();
        g.setFont (fonts->monoFont (11.0f, 0.06f));
        g.setColour (dragging ? pal::emberHot : pal::rose.withAlpha (0.55f));
        g.drawText ((sync ? "BPM " + juce::String (proc.getBpm(), 1) + "   " : juce::String())
                        + tag ("PRE", sync ? ids::preNote : ids::preDelay) + "   " + tag ("DECAY", sync ? ids::decayNote : ids::decay)
                        + "   " + tag ("SIZE", ids::size),
                    top, juce::Justification::centredRight, false);
        if (hovering || dragging)
        {
            g.setFont (fonts->monoFont (9.5f, 0.1f));
            g.setColour (pal::rose.withAlpha (0.4f));
            g.drawText (juce::CharPointer_UTF8 ("\xe2\x86\x94 SIZE   \xe2\x86\x95 DECAY"), b.reduced (16.0f, 10.0f).withTop (b.getBottom() - 28.0f),
                        juce::Justification::centredRight, false);
        }
        if (frozen > 0.01f)
        {
            g.setFont (fonts->monoBoldFont (11.0f, 0.3f));
            g.setColour (pal::emberHot.withAlpha (frozen * (0.7f + 0.3f * std::sin (clock * 2.5f))));
            g.drawText (juce::CharPointer_UTF8 ("\xe2\x88\x9e INFINITE"), top, juce::Justification::centred, false);
        }
    }
    g.restoreState();
    glassFront (g, b, 7.0f);
}

void Portal::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    dragStart = e.position;
    dragDecay = proc.isSynced() ? decayNoteParam : decayParam;   // BPM allumé : DECAY en valeurs de notes
    sizeStart = sizeParam->getValue();
    decayStart = dragDecay->getValue();
    sizeParam->beginChangeGesture();
    dragDecay->beginChangeGesture();
}

void Portal::mouseDrag (const juce::MouseEvent& e)
{
    const auto d = e.position - dragStart;
    const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
    sizeParam->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, sizeStart + fine * d.x / ((float) getWidth() * 0.7f)));
    dragDecay->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, decayStart - fine * d.y / ((float) getHeight() * 1.2f)));
}

void Portal::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    sizeParam->endChangeGesture();
    dragDecay->endChangeGesture();
}

void Portal::mouseDoubleClick (const juce::MouseEvent&)
{
    for (auto* p : { sizeParam, proc.isSynced() ? decayNoteParam : decayParam })
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
}

//==============================================================================
namespace
{
const char* const bandNames[] { "LOW CUT", "GRAVE", "MEDIUM", "AIGU", "HIGH CUT" };
constexpr float eqTop = 15.0f, eqBottom = -27.0f;
constexpr float specTop = -18.0f, specBottom = -96.0f;   // échelle du spectre (dBFS, penché de +3 dB par octave autour de 1 kHz)
}

EqView::EqView (OniriqueReverbProcessor& p) : proc (p)
{
    scope.assign ((size_t) reverb::Engine::kScope, 0.0f);
    fftData.assign ((size_t) fftSize * 2, 0.0f);
    spectrum.assign ((size_t) fftSize / 2, specBottom);
    setOpaque (true);
    setTooltip (juce::String::fromUTF8 ("L'EQ de la reverb, avec le spectre en direct. IN : ce qui entre dans la reverb (la voix sèche n'est "
                                        "pas touchée). OUT : la queue qui en sort. Tirer les points : les coupes en largeur ; grave, médium "
                                        "et aigu en largeur (fréquence) et en hauteur (gain). Double-clic sur un point : retour à zéro. "
                                        "CLEAN creuse aussi les bas-médiums de la queue."));
}

const EqIds& EqView::idsFor (bool out) const noexcept { return out ? eqOutIds : eqInIds; }

reverb::Eq EqView::read (bool out) const noexcept
{
    const auto& e = idsFor (out);
    reverb::Eq q;
    q.lowCut = proc.value (e.lowCut);   q.highCut = proc.value (e.highCut);
    q.lowFreq = proc.value (e.lowFreq); q.lowGain = proc.value (e.lowGain);
    q.midFreq = proc.value (e.midFreq); q.midGain = proc.value (e.midGain);
    q.highFreq = proc.value (e.highFreq); q.highGain = proc.value (e.highGain);
    return q;
}

juce::RangedAudioParameter* EqView::freqParam (int band) const
{
    const auto& e = idsFor (editOut);
    const char* id[] { e.lowCut, e.lowFreq, e.midFreq, e.highFreq, e.highCut };
    return proc.apvts.getParameter (id[band]);
}

juce::RangedAudioParameter* EqView::gainParam (int band) const
{
    const auto& e = idsFor (editOut);
    const char* id[] { nullptr, e.lowGain, e.midGain, e.highGain, nullptr };
    return id[band] != nullptr ? proc.apvts.getParameter (id[band]) : nullptr;
}

juce::Rectangle<float> EqView::plot() const noexcept { return getLocalBounds().toFloat().reduced (12.0f, 8.0f).withTrimmedTop (18.0f).withTrimmedBottom (12.0f); }
juce::Rectangle<float> EqView::tab (bool out) const noexcept { return { out ? 46.0f : 12.0f, 7.0f, 32.0f, 16.0f }; }

float EqView::xFor (float hz) const noexcept
{
    const auto a = plot();
    return a.getX() + std::log (hz / 20.0f) / std::log (1000.0f) * a.getWidth();
}

float EqView::hzFor (float x) const noexcept
{
    const auto a = plot();
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth()));
}

float EqView::yFor (float db) const noexcept
{
    const auto a = plot();
    return juce::jmap (juce::jlimit (eqBottom, eqTop, db), eqTop, eqBottom, a.getY(), a.getBottom());
}

juce::Point<float> EqView::handle (int band) const
{
    const auto* g = gainParam (band);
    const float db = g != nullptr ? g->convertFrom0to1 (g->getValue()) : -3.0f;
    const auto* f = freqParam (band);
    return { xFor (f->convertFrom0to1 (f->getValue())), yFor (db) };
}

int EqView::bandAt (juce::Point<float> pos) const
{
    int best = -1;
    float bestD = 1.0e9f;
    for (int b = 0; b < reverb::Eq::kBands; ++b)
        if (const float d = handle (b).getDistanceFrom (pos); d < bestD) { bestD = d; best = b; }
    return best;
}

void EqView::tick (double seconds)
{
    // le spectre : les fftSize derniers échantillons (IN ou OUT), fenêtre de Hann, montée rapide et descente douce
    proc.getEngine().copyScope (editOut, scope.data());
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    const int offset = reverb::Engine::kScope - fftSize;
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = scope[(size_t) (offset + i)] * (0.5f - 0.5f * std::cos (twoPi * (float) i / (float) fftSize));
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
    const float fall = (float) (40.0 * seconds);   // dB perdus par image
    bool alive = false;
    for (int k = 1; k < fftSize / 2; ++k)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[(size_t) k] * 4.0f / (float) fftSize, -140.0f);
        auto& s = spectrum[(size_t) k];
        s = db > s ? s + 0.6f * (db - s) : juce::jmax (db, s - fall);
        alive |= s > specBottom - 6.0f;
    }

    std::vector<float> now;
    for (bool out : { false, true })
    {
        const auto& e = idsFor (out);
        for (const char* id : { e.lowCut, e.highCut, e.lowFreq, e.lowGain, e.midFreq, e.midGain, e.highFreq, e.highGain })
            now.push_back (proc.value (id));
    }
    now.push_back (proc.value (ids::clean));
    if (now != seen || alive || spectrumAlive) { seen = std::move (now); repaint(); }
    spectrumAlive = alive;
}

void EqView::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (pal::plateTop, 0.0f, -646.0f, pal::plateBottom, 0.0f, 254.0f, false));
    g.fillAll();
    const auto b = all.reduced (2.0f);
    glassBack (g, b);

    const auto a = plot();
    g.setFont (fonts->monoFont (8.5f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        g.setColour (pal::rose.withAlpha (f == 100.0f || f == 1000.0f || f == 10000.0f ? 0.08f : 0.04f));
        g.drawVerticalLine ((int) xFor (f), a.getY(), a.getBottom());
        g.setColour (pal::rose.withAlpha (0.35f));
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (xFor (f) - 15.0f, a.getBottom() + 1.0f, 30.0f, 11.0f), juce::Justification::centred, false);
    }
    for (float db : { 12.0f, -12.0f, -24.0f })
    {
        g.setColour (pal::rose.withAlpha (0.04f));
        g.drawHorizontalLine ((int) yFor (db), a.getX(), a.getRight());
    }
    g.setColour (pal::rose.withAlpha (0.12f));
    g.drawHorizontalLine ((int) yFor (0.0f), a.getX(), a.getRight());

    // le spectre en direct, derrière les courbes
    {
        const double fs = proc.getEngine().getSampleRate();
        juce::Path spec;
        for (float x = a.getX(); x <= a.getRight() + 0.5f; x += 2.0f)
        {
            const float hz = hzFor (x), bin = (float) (hz / fs * fftSize);
            const int i = juce::jlimit (1, fftSize / 2 - 2, (int) bin);
            const float f = juce::jlimit (0.0f, 1.0f, bin - (float) i);
            float db = spectrum[(size_t) i] + f * (spectrum[(size_t) i + 1] - spectrum[(size_t) i]);
            db += 3.0f * std::log2 (hz / 1000.0f);
            const float y = juce::jmap (juce::jlimit (specBottom, specTop, db), specTop, specBottom, a.getY(), a.getBottom());
            if (spec.isEmpty()) spec.startNewSubPath (x, a.getBottom());
            spec.lineTo (x, y);
        }
        spec.lineTo (a.getRight(), a.getBottom());
        spec.closeSubPath();
        g.setGradientFill (juce::ColourGradient (pal::rose.withAlpha (0.16f), 0.0f, a.getY(), pal::rose.withAlpha (0.03f), 0.0f, a.getBottom(), false));
        g.fillPath (spec);
    }

    // les deux courbes : celle qu'on règle en braise, l'autre en filigrane
    const double fs = 48000.0;
    auto curveFor = [&] (bool out, juce::Path& curve, juce::Path& fill)
    {
        BiquadCoeffs c[reverb::Eq::kBands];
        bool on[reverb::Eq::kBands];
        read (out).design (fs, c, on);
        const auto mud = BiquadCoeffs::peaking (fs, 350.0, 0.9, -0.06 * proc.value (ids::clean));   // CLEAN en % : jusqu'à -6 dB
        for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
        {
            const double f = hzFor (x);
            double mag = out ? std::abs (mud.response (fs, f)) : 1.0;
            for (int k = 0; k < reverb::Eq::kBands; ++k)
                if (on[k]) mag *= std::abs (c[k].response (fs, f));
            const float y = yFor ((float) (20.0 * std::log10 (mag + 1.0e-9)));
            if (curve.isEmpty()) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, a.getBottom()); }
            curve.lineTo (x, y);
            fill.lineTo (x, y);
        }
        fill.lineTo (a.getRight(), a.getBottom());
        fill.closeSubPath();
    };
    {
        juce::Path other, unused;
        curveFor (! editOut, other, unused);
        g.setColour (pal::bone.withAlpha (0.28f));
        g.strokePath (other, juce::PathStrokeType (1.0f));
    }
    juce::Path curve, fill;
    curveFor (editOut, curve, fill);
    g.setGradientFill (juce::ColourGradient (pal::ember.withAlpha (0.22f), 0.0f, a.getY(), pal::ember.withAlpha (0.0f), 0.0f, a.getBottom(), false));
    g.fillPath (fill);
    g.setColour (pal::ember.withAlpha (0.2f));
    g.strokePath (curve, juce::PathStrokeType (4.0f));
    g.setColour (pal::emberHot);
    g.strokePath (curve, juce::PathStrokeType (1.5f));

    for (int band = 0; band < reverb::Eq::kBands; ++band)
    {
        const auto pt = handle (band);
        const bool hot = band == grabbed || band == hover;
        const float r = hot ? 6.0f : 4.5f;
        if (hot)
        {
            g.setColour (pal::ember.withAlpha (0.25f));
            g.fillEllipse (circle (pt, r * 2.0f));
        }
        g.setColour (pal::glass);
        g.fillEllipse (circle (pt, r));
        g.setColour (gainParam (band) != nullptr ? pal::emberHot : pal::bone);
        g.drawEllipse (circle (pt, r), 1.5f);
    }

    // IN / OUT, puis la bande touchée ou le rôle de l'EQ choisi
    for (bool out : { false, true })
    {
        const auto t = tab (out);
        const bool sel = out == editOut;
        g.setFont (fonts->monoBoldFont (10.0f, 0.12f));
        g.setColour (sel ? pal::emberHot : pal::rose.withAlpha (0.35f));
        g.drawText (out ? "OUT" : "IN", t, juce::Justification::centred, false);
        if (sel) g.fillRect (t.getCentreX() - 8.0f, t.getBottom() - 1.0f, 16.0f, 1.5f);
    }
    g.setFont (fonts->monoFont (9.5f, 0.06f));
    const int shown = grabbed >= 0 ? grabbed : hover;
    const auto info = juce::Rectangle<float> (90.0f, 7.0f, b.getWidth() - 100.0f, 16.0f);
    if (shown >= 0)
    {
        auto text = juce::String (editOut ? "OUT  " : "IN  ") + bandNames[shown] + "  " + freqParam (shown)->getCurrentValueAsText();
        if (auto* gp = gainParam (shown)) text << "  " << gp->getCurrentValueAsText();
        g.setColour (pal::bone);
        g.drawText (text, info, juce::Justification::centredRight, false);
    }
    else
    {
        g.setColour (pal::rose.withAlpha (0.45f));
        g.drawText (editOut ? "LA QUEUE QUI SORT DE LA REVERB" : "CE QUI ENTRE DANS LA REVERB", info, juce::Justification::centredRight, false);
    }
    glassFront (g, b);
}

void EqView::mouseMove (const juce::MouseEvent& e)
{
    const int h = tab (false).contains (e.position) || tab (true).contains (e.position) ? -1 : bandAt (e.position);
    if (h != hover) { hover = h; repaint(); }
}

void EqView::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void EqView::mouseDown (const juce::MouseEvent& e)
{
    for (bool out : { false, true })
        if (tab (out).contains (e.position))
        {
            if (editOut != out) std::fill (spectrum.begin(), spectrum.end(), specBottom);
            editOut = out;
            grabbed = -1;
            repaint();
            return;
        }
    grabbed = bandAt (e.position);
    freqParam (grabbed)->beginChangeGesture();
    if (auto* gp = gainParam (grabbed)) gp->beginChangeGesture();
    mouseDrag (e);
}

void EqView::mouseDrag (const juce::MouseEvent& e)
{
    if (grabbed < 0) return;
    auto set = [] (juce::RangedAudioParameter* p, float v)
    {
        p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (v)));
    };
    set (freqParam (grabbed), hzFor (e.position.x));
    if (auto* gp = gainParam (grabbed))
        set (gp, juce::jmap (e.position.y, plot().getY(), plot().getBottom(), eqTop, eqBottom));
    repaint();
}

void EqView::mouseUp (const juce::MouseEvent&)
{
    if (grabbed >= 0)
    {
        freqParam (grabbed)->endChangeGesture();
        if (auto* gp = gainParam (grabbed)) gp->endChangeGesture();
    }
    grabbed = -1;
    repaint();
}

void EqView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int band = bandAt (e.position);
    if (band < 0 || tab (false).contains (e.position) || tab (true).contains (e.position)) return;
    auto* p = gainParam (band) != nullptr ? gainParam (band) : freqParam (band);   // grave / médium / aigu : gain à 0 ; coupe : sa valeur d'origine
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->getDefaultValue());
    p->endChangeGesture();
}

//==============================================================================
void GlowToggle::tick()
{
    const bool on = style == Style::power ? ! getToggleState() : getToggleState();
    bool moving = approach (lit, on ? 1.0f : 0.0f, 0.2f);
    if (style == Style::infinite && lit > 0.0f) { pulse += 0.06f; moving = true; }
    if (moving) repaint();
}

void GlowToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();

    if (style == Style::power)
    {
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 3.0f;
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillEllipse (circle (c, r + 2.0f));
        g.setGradientFill (juce::ColourGradient (pal::metalHi.brighter (highlighted ? 0.08f : 0.0f), c.x, c.y - r, pal::metalLo.brighter (0.3f), c.x, c.y + r, false));
        g.fillEllipse (circle (down ? c.translated (0.0f, 0.6f) : c, r));
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.drawEllipse (circle (c, r), 1.0f);
        if (lit > 0.02f)
        {
            g.setGradientFill (juce::ColourGradient (pal::ember.withAlpha (0.35f * lit), c.x, c.y, pal::ember.withAlpha (0.0f), c.x + r, c.y, true));
            g.fillEllipse (circle (c, r));
        }
        juce::Path icon;
        icon.addCentredArc (c.x, c.y, r * 0.42f, r * 0.42f, 0.0f, 0.75f, twoPi - 0.75f, true);
        icon.startNewSubPath (c.x, c.y - r * 0.55f);
        icon.lineTo (c.x, c.y - r * 0.1f);
        g.setColour (pal::inkDim.interpolatedWith (pal::emberHot, lit));
        g.strokePath (icon, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }

    if (style == Style::lock)
    {
        const auto col = pal::ink.withAlpha (highlighted ? 0.85f : 0.5f).interpolatedWith (pal::emberHot, lit);
        const auto body = juce::Rectangle<float> (b.getWidth() * 0.6f, b.getHeight() * 0.42f).withCentre (c.translated (0.0f, b.getHeight() * 0.17f));
        juce::Path shackle;
        const float sw = body.getWidth() * 0.32f;
        shackle.startNewSubPath (c.x - sw, body.getY());
        shackle.lineTo (c.x - sw, body.getY() - b.getHeight() * 0.12f);
        shackle.addCentredArc (c.x, body.getY() - b.getHeight() * 0.12f, sw, sw, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
        shackle.lineTo (c.x + sw, body.getY() - (1.0f - lit) * b.getHeight() * 0.1f);
        g.setColour (col);
        g.strokePath (shackle, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillRoundedRectangle (body, 1.5f);
        return;
    }

    // INFINITE : une grande touche carrée à verrou, le signe gravé dessus qui s'allume quand la queue est figée,
    // son témoin, et son nom sérigraphié à côté
    const float side = juce::jmin (b.getHeight() - 16.0f, 92.0f);
    const auto cap = juce::Rectangle<float> (b.getX() + 10.0f, c.y - side * 0.5f, side, side);
    const float breath = 0.5f + 0.5f * std::sin (pulse);
    keyCap (g, cap, down || getToggleState(), highlighted, 7.0f);
    if (lit > 0.02f)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (cap.toNearestInt());
        g.setGradientFill (juce::ColourGradient (pal::ember.withAlpha (0.28f * lit * (0.75f + 0.25f * breath)), cap.getCentreX(), cap.getCentreY(),
                                                 pal::ember.withAlpha (0.0f), cap.getRight(), cap.getCentreY(), true));
        g.fillRect (cap);
    }
    const auto symText = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e"));
    g.setFont (fonts->monoBoldFont (44.0f));
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawText (symText, cap.translated (0.0f, -1.0f), juce::Justification::centred, false);
    g.setColour (pal::inkDim.withAlpha (0.8f).interpolatedWith (pal::emberHot, lit));
    g.drawText (symText, cap, juce::Justification::centred, false);

    const float tx = cap.getRight() + 18.0f;
    led (g, { tx + 4.0f, c.y - 22.0f }, lit * (0.8f + 0.2f * breath), 3.0f);
    g.setFont (fonts->bodyFont (14.0f, 0.32f));
    g.setColour (pal::ink.withAlpha (highlighted ? 0.95f : 0.8f));
    g.drawText ("INFINITE", juce::Rectangle<float> (tx, c.y - 10.0f, b.getRight() - tx, 20.0f), juce::Justification::centredLeft, false);
    g.setFont (fonts->monoFont (9.5f, 0.12f));
    g.setColour (pal::inkDim.interpolatedWith (pal::emberHot, lit));
    g.drawText (lit > 0.5f ? juce::String::fromUTF8 ("QUEUE FIGÉE") : juce::String ("FIGER LA QUEUE"),
                juce::Rectangle<float> (tx, c.y + 12.0f, b.getRight() - tx, 16.0f), juce::Justification::centredLeft, false);
}

//==============================================================================
ChoicePill::ChoicePill (OniriqueReverbProcessor& p, const char* id, const char* tip)
    : param (dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id))),
      attachment (*param, [this] (float) { repaint(); })
{
    setTooltip (juce::String::fromUTF8 (tip));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ChoicePill::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (2.0f);
    glassBack (g, b, 4.0f);
    g.setColour (isMouseOver() ? pal::emberHot : pal::ember.brighter (0.15f));
    g.setFont (fonts->monoBoldFont (11.5f));
    g.drawText (param->getCurrentChoiceName(), b.withTrimmedRight (12.0f), juce::Justification::centred, false);
    menuArrow (g, b.getRight() - 15.0f, b.getCentreY(), pal::rose.withAlpha (isMouseOver() ? 0.8f : 0.45f));
    glassFront (g, b, 4.0f);
}

void ChoicePill::mouseDown (const juce::MouseEvent&)
{
    juce::PopupMenu m;
    for (int i = 0; i < param->choices.size(); ++i)
        m.addItem (i + 1, param->choices[i], true, i == param->getIndex());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                     [safe = juce::Component::SafePointer<ChoicePill> (this)] (int r)
                     {
                         if (safe != nullptr && r > 0) safe->attachment.setValueAsCompleteGesture ((float) (r - 1));
                     });
}

//==============================================================================
PresetBar::PresetBar (OniriqueReverbProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle ("Presets");
}

void PresetBar::tick()
{
    const auto name = proc.presets.getCurrentName();
    const bool modified = proc.presets.isModified();
    if (name != shownName || modified != shownModified)
    {
        shownName = name;
        shownModified = modified;
        repaint();
    }
}

void PresetBar::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    for (int side : { -1, 1 })
    {
        const auto k = (side < 0 ? b.withWidth (32.0f) : b.withLeft (b.getRight() - 32.0f)).reduced (2.0f);
        keyCap (g, k, false, false);
        juce::Path chevron;
        const float x = k.getCentreX(), y = k.getCentreY();
        chevron.startNewSubPath (x + 2.0f * (float) side * -1.0f, y - 4.5f);
        chevron.lineTo (x + 2.0f * (float) side, y);
        chevron.lineTo (x + 2.0f * (float) side * -1.0f, y + 4.5f);
        g.setColour (pal::ink.withAlpha (0.7f));
        g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }

    const auto win = b.reduced (40.0f, 2.0f);
    glassBack (g, win);
    g.setFont (fonts->monoFont (8.5f, 0.2f));
    g.setColour (pal::rose.withAlpha (0.4f));
    g.drawText ("PRESET", win.reduced (10.0f, 0.0f).withWidth (50.0f), juce::Justification::centredLeft, false);
    g.setFont (fonts->bodyFont (14.0f, 0.02f));
    g.setColour (pal::bone.withAlpha (0.92f));
    g.drawText (shownName, win.reduced (10.0f, 0.0f).withTrimmedLeft (52.0f).withTrimmedRight (16.0f), juce::Justification::centredLeft, true);
    if (shownModified)
    {
        g.setColour (pal::emberHot);
        g.fillEllipse (circle ({ win.getRight() - 12.0f, win.getCentreY() }, 2.5f));
    }
    glassFront (g, win);
}

void PresetBar::mouseDown (const juce::MouseEvent& e)
{
    if (e.position.x < 36.0f) proc.presets.step (-1);
    else if (e.position.x > (float) getWidth() - 36.0f) proc.presets.step (1);
    else openMenu();
}

void PresetBar::openMenu()
{
    const auto entries = proc.presets.list();
    juce::PopupMenu m;
    m.addSectionHeader ("PRESETS ONIRIQUE");
    bool userHeader = false, currentIsUser = false;
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        if (! e.factory && ! userHeader) { m.addSectionHeader ("MES PRESETS"); userHeader = true; }
        const bool current = e.name == proc.presets.getCurrentName();
        currentIsUser |= current && ! e.factory;
        m.addItem (i + 1, e.name, true, current);
    }
    m.addSeparator();
    m.addItem (1000, juce::String (juce::CharPointer_UTF8 ("Enregistrer le preset\xe2\x80\xa6")));
    if (currentIsUser) m.addItem (1001, juce::String (juce::CharPointer_UTF8 ("Supprimer \xc2\xab ")) + proc.presets.getCurrentName()
                                         + juce::String (juce::CharPointer_UTF8 (" \xc2\xbb")));
    m.addItem (1002, "Ouvrir le dossier des presets");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                     [safe = juce::Component::SafePointer<PresetBar> (this), entries] (int r)
                     {
                         if (safe == nullptr || r <= 0) return;
                         auto& presets = safe->proc.presets;
                         if (r == 1000) safe->saveAs();
                         else if (r == 1001)
                         {
                             for (const auto& e : entries)
                                 if (e.name == presets.getCurrentName() && ! e.factory) presets.remove (e);
                         }
                         else if (r == 1002) PresetManager::folder().startAsProcess();
                         else if (r <= (int) entries.size()) presets.load (entries[(size_t) (r - 1)]);
                     });
}

void PresetBar::saveAs()
{
    auto* w = new juce::AlertWindow ("ENREGISTRER LE PRESET", "Nom du preset :", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.presets.getCurrentName());
    w->addButton ("ENREGISTRER", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("ANNULER", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, safe = juce::Component::SafePointer<PresetBar> (this)] (int r)
    {
        if (safe != nullptr && r == 1) safe->proc.presets.save (w->getTextEditorContents ("name"));
    }), true);
}

//==============================================================================
BpmControl::BpmControl (OniriqueReverbProcessor& p)
    : proc (p), syncParam (p.apvts.getParameter (ids::sync)), sourceParam (p.apvts.getParameter (ids::bpmSource)),
      bpmParam (p.apvts.getParameter (ids::bpm))
{
    setTooltip (juce::String::fromUTF8 ("SYNC : allumé, la longueur de la reverb (DECAY) et le PRE-DELAY se règlent en valeurs de notes, "
                                        "calculées au tempo. Le tempo suit le DAW, ou se choisit : glisser sur le chiffre (Maj : réglage fin), "
                                        "double-clic pour le taper, touche de droite pour revenir au tempo du DAW. PULSE et STUTTER suivent ce tempo."));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle ("Tempo");
}

int BpmControl::zoneAt (float x) const noexcept
{
    return x < 66.0f ? 0 : (x > (float) getWidth() - 60.0f ? 2 : 1);
}

void BpmControl::tick()
{
    const bool manual = juce::roundToInt (proc.value (ids::bpmSource)) == 1;
    const float bpm = manual ? proc.value (ids::bpm) : proc.getBpm();
    bool moving = approach (lit, proc.isSynced() ? 1.0f : 0.0f, 0.25f);
    if (std::abs (bpm - shownBpm) > 0.01f || shownSource != (int) manual) { shownBpm = bpm; shownSource = (int) manual; moving = true; }
    if (moving) repaint();
}

void BpmControl::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    // la touche SYNC
    const auto sync = b.withWidth (62.0f).reduced (2.0f);
    keyCap (g, sync, lit > 0.5f, false);
    led (g, { sync.getX() + 12.0f, sync.getCentreY() }, lit);
    g.setFont (fonts->bodyFont (10.0f, 0.2f));
    g.setColour (pal::inkDim.interpolatedWith (pal::bone, lit));
    g.drawText ("SYNC", sync.withTrimmedLeft (20.0f), juce::Justification::centred, false);

    // le tempo
    const auto win = juce::Rectangle<float> (68.0f, 2.0f, b.getWidth() - 128.0f, b.getHeight() - 4.0f);
    glassBack (g, win);
    g.setFont (fonts->monoBoldFont (15.0f));
    g.setColour (draggingValue ? pal::emberHot : pal::ember.brighter (0.15f).interpolatedWith (pal::emberHot, lit));
    g.drawText (juce::String (shownBpm, 1), win.withTrimmedRight (22.0f), juce::Justification::centred, false);
    g.setFont (fonts->monoFont (8.5f, 0.15f));
    g.setColour (pal::rose.withAlpha (0.4f));
    g.drawText ("BPM", win.withTrimmedRight (8.0f), juce::Justification::centredRight, false);
    glassFront (g, win);

    // d'où il vient
    const auto src = b.withLeft (b.getRight() - 58.0f).reduced (2.0f);
    keyCap (g, src, false, false);
    g.setFont (fonts->monoBoldFont (10.0f, 0.12f));
    g.setColour (shownSource == 1 ? pal::emberHot : pal::ink.withAlpha (0.7f));
    g.drawText (shownSource == 1 ? "MAN" : "DAW", src.withTrimmedRight (14.0f), juce::Justification::centred, false);
    menuArrow (g, src.getRight() - 16.0f, src.getCentreY(), pal::ink.withAlpha (0.5f));
}

void BpmControl::setManualBpm (float bpm)
{
    for (auto [param, value] : { std::pair { sourceParam, 1.0f }, std::pair { bpmParam, bpm } })
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (param->getNormalisableRange().snapToLegalValue (value)));
        param->endChangeGesture();
    }
}

void BpmControl::mouseDown (const juce::MouseEvent& e)
{
    const int zone = zoneAt (e.position.x);
    if (zone == 0)
    {
        syncParam->beginChangeGesture();
        syncParam->setValueNotifyingHost (proc.isSynced() ? 0.0f : 1.0f);
        syncParam->endChangeGesture();
    }
    else if (zone == 1)
    {
        draggingValue = true;
        dragStartBpm = shownBpm;
    }
    else
    {
        juce::PopupMenu m;
        m.addItem (1, "Suivre le tempo du DAW", true, shownSource == 0);
        m.addItem (2, "Tempo choisi", true, shownSource == 1);
        m.addSeparator();
        m.addItem (3, juce::String::fromUTF8 ("Taper le tempo…"));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe = juce::Component::SafePointer<BpmControl> (this)] (int r)
                         {
                             if (safe == nullptr || r <= 0) return;
                             if (r == 1)
                             {
                                 safe->sourceParam->beginChangeGesture();
                                 safe->sourceParam->setValueNotifyingHost (0.0f);
                                 safe->sourceParam->endChangeGesture();
                             }
                             else if (r == 2) safe->setManualBpm (safe->shownBpm);
                             else safe->askBpm();
                         });
    }
}

void BpmControl::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingValue || e.getDistanceFromDragStart() < 2) return;
    const float fine = e.mods.isShiftDown() ? 0.02f : 0.25f;
    setManualBpm (dragStartBpm - fine * (float) e.getDistanceFromDragStartY());
}

void BpmControl::mouseUp (const juce::MouseEvent&)
{
    draggingValue = false;
    repaint();
}

void BpmControl::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (zoneAt (e.position.x) == 1) askBpm();
}

void BpmControl::askBpm()
{
    auto* w = new juce::AlertWindow ("TEMPO", juce::String::fromUTF8 ("BPM (40 à 240) :"), juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("bpm", juce::String (shownBpm, 1));
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("ANNULER", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, safe = juce::Component::SafePointer<BpmControl> (this)] (int r)
    {
        const float bpm = w->getTextEditorContents ("bpm").replaceCharacter (',', '.').getFloatValue();
        if (safe != nullptr && r == 1 && bpm >= 40.0f && bpm <= 240.0f) safe->setManualBpm (bpm);
    }), true);
}

//==============================================================================
StutterView::StutterView (OniriqueReverbProcessor& p) : proc (p)
{
    setOpaque (true);
    setTooltip (juce::String::fromUTF8 ("Le motif du STUTTER sur une mesure. CHOP : la queue hachée net. REPEAT : le premier morceau de "
                                        "chaque groupe de quatre est rejoué trois fois. RAMP : les coupes accélèrent à chaque temps, "
                                        "jusqu'à la relance. La voix sèche n'est jamais découpée, seulement la reverb."));
}

void StutterView::tick()
{
    const double now = proc.getEngine().getBeat();
    if (now != beat || juce::roundToInt (proc.value (ids::stutterMode)) != 0) { beat = now; repaint(); }
}

void StutterView::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (pal::plateTop, 0.0f, -788.0f, pal::plateBottom, 0.0f, 112.0f, false));
    g.fillAll();
    const auto b = all.reduced (2.0f);
    glassBack (g, b);

    const auto area = b.reduced (12.0f, 10.0f).withTrimmedTop (12.0f);
    const int mode = juce::roundToInt (proc.value (ids::stutterMode));
    const double period = stutterRateBeats (juce::roundToInt (proc.value (ids::stutterRate)));
    const float amount = 0.3f + 0.7f * proc.value (ids::stutterMix) * 0.01f;
    const auto xAt = [&] (double t) { return area.getX() + (float) (t / 4.0) * area.getWidth(); };

    g.setFont (fonts->monoFont (8.5f, 0.1f));
    for (int k = 0; k < 4; ++k)
    {
        g.setColour (pal::rose.withAlpha (0.07f));
        g.drawVerticalLine ((int) xAt (k), area.getY(), area.getBottom());
        g.setColour (pal::rose.withAlpha (0.4f));
        g.drawText (juce::String (k + 1), juce::Rectangle<float> (xAt (k) + 3.0f, b.getY() + 5.0f, 20.0f, 12.0f), juce::Justification::centredLeft, false);
    }

    if (mode == 0)
    {
        g.setFont (fonts->monoFont (9.5f, 0.14f));
        g.setColour (pal::rose.withAlpha (0.45f));
        g.drawText (juce::String::fromUTF8 ("STUTTER COUPÉ · CHOISIR CHOP, REPEAT OU RAMP"), area, juce::Justification::centred, false);
        glassFront (g, b);
        return;
    }

    if (mode == 2)   // REPEAT : les groupes de quatre découpes
    {
        const int count = juce::roundToInt (4.0 / period);
        for (int k = 0; k < count; ++k)
        {
            const auto slice = juce::Rectangle<float> (xAt (k * period), area.getY(), xAt ((k + 1) * period) - xAt (k * period), area.getHeight()).reduced (1.5f, 0.0f);
            const bool rec = k % 4 == 0;
            g.setColour ((rec ? pal::ember : pal::bone).withAlpha ((rec ? 0.4f : 0.12f) * amount));
            g.fillRoundedRectangle (slice, 2.0f);
            g.setColour (pal::ember.withAlpha (0.75f * amount));
            g.drawRoundedRectangle (slice, 2.0f, 1.0f);
            if (slice.getWidth() > 24.0f)
            {
                g.setFont (fonts->monoBoldFont (9.0f, 0.1f));
                g.setColour (pal::bone.withAlpha (0.8f));
                g.drawText (rec ? "REC" : juce::String::fromUTF8 ("×") + juce::String (k % 4), slice, juce::Justification::centred, false);
            }
        }
    }
    else             // CHOP, RAMP : la porte
    {
        juce::Path gate, fill;
        for (float x = area.getX(); x <= area.getRight(); x += 1.0f)
        {
            const double t = 4.0 * (x - area.getX()) / area.getWidth();
            const float y = area.getBottom() - reverb::Engine::stutterGate (mode, t, period, 0.012) * area.getHeight();
            if (gate.isEmpty()) { gate.startNewSubPath (x, y); fill.startNewSubPath (x, area.getBottom()); }
            gate.lineTo (x, y);
            fill.lineTo (x, y);
        }
        fill.lineTo (area.getRight(), area.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (pal::ember.withAlpha (0.35f * amount), 0.0f, area.getY(), pal::ember.withAlpha (0.04f * amount), 0.0f, area.getBottom(), false));
        g.fillPath (fill);
        g.setColour (pal::emberHot.withAlpha (amount));
        g.strokePath (gate, juce::PathStrokeType (1.2f));
    }

    // la tête de lecture
    const float px = xAt (std::fmod (std::max (0.0, beat), 4.0));
    g.setColour (pal::bone.withAlpha (0.15f));
    g.fillRect (juce::Rectangle<float> (px - 2.5f, area.getY() - 4.0f, 5.0f, area.getHeight() + 6.0f));
    g.setColour (pal::bone);
    g.fillRect (juce::Rectangle<float> (px - 0.75f, area.getY() - 4.0f, 1.5f, area.getHeight() + 6.0f));
    glassFront (g, b);
}

//==============================================================================
struct OniriqueReverbEditor::Content : public juce::Component
{
    explicit Content (OniriqueReverbProcessor& p)
        : proc (p), portal (p),
          modes (p, ids::mode, modeNames, "L'espace de la reverb : ROOM (une pièce), PLATE I (plaque brillante), PLATE II (plaque sombre et "
                                          "large), HALL (une salle), SHIMMER (octave aérienne), ONIRIQUE (salle immense et sombre)."),
          stutterModes (p, ids::stutterMode, stutterModeNames,
                        "STUTTER : découpe la queue de la reverb au tempo. CHOP : hachée net. REPEAT : un morceau rejoué "
                        "en boucle. RAMP : les coupes accélèrent sur la mesure. OFF : la queue entière."),
          curve (p), presetBar (p), bpm (p), stutterView (p),
          preDelay (p, ids::preDelay, "PRE-DELAY", "Le silence entre la voix et le début de la reverb. Un peu de pre-delay (20 à 80 ms) "
                                                     "garde la voix nette devant, la reverb arrive juste derrière."),
          preNote (p, ids::preNote, "PRE-DELAY", "Le pre-delay au tempo (SYNC allumé) : de 0 à une noire. T : triolet, D : pointé."),
          decay (p, ids::decay, "DECAY", "La durée de la queue : le temps qu'il faut à la reverb pour s'éteindre."),
          decayNote (p, ids::decayNote, "DECAY", "La durée de la queue au tempo (SYNC allumé) : d'un temps à huit mesures."),
          size (p, ids::size, "SIZE", "La taille de la pièce : petite et serrée, ou immense."),
          diffusion (p, ids::diffusion, "DIFFUSION", "Bas : on entend des échos séparés. Haut : une nappe lisse et dense."),
          modRate (p, ids::modRate, "MOD RATE", "La vitesse à laquelle la queue ondule."),
          modDepth (p, ids::modDepth, "MOD DEPTH", "L'ampleur de l'ondulation : de la reverb fixe à une queue qui chorusse et rêve."),
          width (p, ids::width, "WIDTH", "La largeur stéréo de la reverb : 0 % au centre, 100 % naturelle, 200 % très large."),
          output (p, ids::output, "OUTPUT", "Le volume de sortie du plugin."),
          mix (p, ids::mix, "MIX", "Le dosage de la reverb. Jusqu'à 50 % la voix reste entière et la reverb monte ; "
                                   "au-delà, la voix s'efface jusqu'à la reverb seule.", true),
          ducking (p, ids::ducking, "DUCKING", "La reverb s'efface quand la voix chante et revient dans les silences : "
                                               "la voix reste devant, l'espace s'ouvre entre les phrases."),
          clean (p, ids::clean, "CLEAN", "Nettoie la queue : les sifflantes (s, ch) n'entrent plus dans la reverb, les "
                                         "bas-médiums qui embouent sont creusés, et chaque nouvelle phrase efface la queue de la précédente."),
          pulse (p, ids::pulse, "PULSE", "La queue respire au tempo. Choisir la division à droite."),
          stutterMix (p, ids::stutterMix, "STUTTER", "La force du STUTTER : à 100 % la queue est entièrement découpée, plus bas "
                                                     "la queue entière reste dessous."),
          pulseRate (p, ids::pulseRate, "La division de PULSE, au tempo (T : triolet, D : pointé)."),
          stutterRate (p, ids::stutterRate, "La longueur d'une découpe du STUTTER, au tempo (T : triolet)."),
          infinite (GlowToggle::Style::infinite, "INFINITE : fige la queue de la reverb, elle tient indéfiniment ; la voix "
                                                 "passe sans entrer dans la reverb. Le relâcher laisse la queue s'éteindre."),
          bypass (GlowToggle::Style::power, "Allumer / couper la reverb (la queue continue de vivre)."),
          lock (GlowToggle::Style::lock, "Verrouiller le MIX : changer de preset garde le dosage choisi."),
          infiniteAttach (p.apvts, ids::freeze, infinite),
          bypassAttach (p.apvts, ids::bypass, bypass)
    {
        for (juce::Component* c : std::initializer_list<juce::Component*> { &portal, &modes, &stutterModes, &curve, &presetBar, &bpm,
                                         &stutterView, &preDelay, &preNote, &decay, &decayNote, &size, &diffusion, &modRate, &modDepth,
                                         &width, &output, &mix, &ducking, &clean, &pulse, &stutterMix, &pulseRate, &stutterRate,
                                         &infinite, &bypass, &lock })
            addAndMakeVisible (c);

        lock.setToggleState (p.presets.isMixLocked(), juce::dontSendNotification);
        lock.onClick = [this] { proc.presets.setMixLocked (lock.getToggleState()); };

        sigil = juce::ImageCache::getFromMemory (BinaryData::sigil_png, BinaryData::sigil_pngSize);

        // le métal brossé : des stries horizontales, chaque rangée un peu plus claire ou plus sombre
        brushed = juce::Image (juce::Image::ARGB, 256, 256, true);
        juce::Random r (11);
        for (int y = 0; y < brushed.getHeight(); ++y)
        {
            const float row = r.nextFloat() * 2.0f - 1.0f;
            for (int x = 0; x < brushed.getWidth(); ++x)
            {
                const float v = 0.75f * row + 0.25f * (r.nextFloat() * 2.0f - 1.0f);
                brushed.setPixelAt (x, y, (v > 0.0f ? juce::Colours::white : juce::Colours::black).withAlpha (std::abs (v) * 0.03f));
            }
        }
        showSync();
    }

    void resized() override
    {
        presetBar.setBounds (392, 14, 330, 34);
        bpm.setBounds (736, 14, 196, 34);
        bypass.setBounds (1118, 13, 36, 36);
        portal.setBounds (24, 74, 1132, 276);
        modes.setBounds (232, 362, 716, 36);

        const int knobY = 432, knobH = 156;
        Knob* left[] { &preDelay, &decay, &size, &diffusion };
        Knob* right[] { &modRate, &modDepth, &width, &output };
        for (int i = 0; i < 4; ++i)
        {
            left[i]->setBounds (34 + i * 98, knobY, 94, knobH);
            right[i]->setBounds (762 + i * 98, knobY, 94, knobH);
        }
        preNote.setBounds (preDelay.getBounds());
        decayNote.setBounds (decay.getBounds());
        mix.setBounds (466, 414, 248, 194);
        lock.setBounds (694, 418, 18, 22);

        // EQ, DYNAMIQUE, INFINITE
        curve.setBounds (34, 646, 640, 112);
        ducking.setBounds (692, 642, 86, 116);
        clean.setBounds (782, 642, 86, 116);
        infinite.setBounds (892, 646, 252, 112);

        // RYTHME : STUTTER puis PULSE
        stutterModes.setBounds (34, 798, 306, 34);
        stutterRate.setBounds (118, 842, 90, 28);
        stutterMix.setBounds (352, 780, 88, 112);
        stutterView.setBounds (452, 788, 466, 98);
        pulse.setBounds (944, 780, 88, 112);
        pulseRate.setBounds (1042, 828, 102, 28);
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();

        // la façade : métal anodisé, éclairé d'en haut, brossé, assombri sur les bords
        g.setGradientFill (juce::ColourGradient (pal::plateTop, 0.0f, 0.0f, pal::plateBottom, 0.0f, b.getBottom(), false));
        g.fillRect (b);
        g.setTiledImageFill (brushed, 0, 0, 1.0f);
        g.fillRect (b);
        {
            juce::ColourGradient v (juce::Colours::transparentBlack, b.getCentreX(), 420.0f, juce::Colours::black.withAlpha (0.32f), 0.0f, 0.0f, true);
            v.addColour (0.55, juce::Colours::transparentBlack);
            g.setGradientFill (v);
            g.fillRect (b);
        }
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine (0, 0.0f, b.getRight());

        // la marque : le sigil, ONIRIQUE sérigraphié, « Verb » en gothique couleur braise
        g.setOpacity (1.0f);
        g.drawImage (sigil, juce::Rectangle<float> (22.0f, 16.0f, 30.0f, 30.0f), juce::RectanglePlacement::centred);
        const auto brand = fonts->displayFont (15.0f, 0.14f);
        g.setFont (brand);
        g.setColour (pal::ink.withAlpha (0.92f));
        g.drawText ("ONIRIQUE", juce::Rectangle<float> (62.0f, 16.0f, 200.0f, 30.0f), juce::Justification::centredLeft, false);
        const float bw = juce::GlyphArrangement::getStringWidth (brand, "ONIRIQUE");
        g.setFont (fonts->dreamFont (33.0f));
        g.setColour (pal::ember);
        g.drawText ("Verb", juce::Rectangle<float> (62.0f + bw + 10.0f, 8.0f, 120.0f, 40.0f), juce::Justification::centredLeft, false);

        groove (g, 0.0f, 62.0f, b.getRight(), 62.0f);

        // sous l'affichage : les espaces, entre deux rainures
        groove (g, 24.0f, 380.0f, (float) modes.getX() - 12.0f, 380.0f);
        groove (g, (float) modes.getRight() + 12.0f, 380.0f, 1156.0f, 380.0f);

        sectionTitle (g, "ESPACE", 34.0f, 420.0f, 436.0f);
        sectionTitle (g, "MOUVEMENT", 762.0f, 420.0f, 1146.0f);
        groove (g, 450.0f, 414.0f, 450.0f, 604.0f);
        groove (g, 730.0f, 414.0f, 730.0f, 604.0f);

        groove (g, 24.0f, 618.0f, 1156.0f, 618.0f);
        sectionTitle (g, "EQ", 34.0f, 634.0f, 674.0f);
        sectionTitle (g, "DYNAMIQUE", 692.0f, 634.0f, 868.0f);
        groove (g, 880.0f, 630.0f, 880.0f, 758.0f);

        groove (g, 24.0f, 770.0f, 1156.0f, 770.0f);
        sectionTitle (g, "RYTHME", 34.0f, 786.0f, 340.0f);
        ink (g, "DIVISION", { 34.0f, 842.0f, 80.0f, 28.0f }, juce::Justification::centredLeft, 9.5f, 0.55f);
        groove (g, 930.0f, 784.0f, 930.0f, 884.0f);
        ink (g, "DIVISION", { 1042.0f, 806.0f, 102.0f, 16.0f }, juce::Justification::centred, 9.5f, 0.55f);
    }

    // SYNC allumé : DECAY et PRE-DELAY se règlent en valeurs de notes
    void showSync()
    {
        const bool sync = proc.isSynced();
        preDelay.setVisible (! sync);
        decay.setVisible (! sync);
        preNote.setVisible (sync);
        decayNote.setVisible (sync);
    }

    void refresh (double seconds)
    {
        portal.refresh (seconds);
        modes.tick();
        stutterModes.tick();
        stutterView.tick();
        curve.tick (seconds);
        presetBar.tick();
        bpm.tick();
        infinite.tick();
        bypass.tick();
        lock.tick();
        showSync();
        if (lock.getToggleState() != proc.presets.isMixLocked())
            lock.setToggleState (proc.presets.isMixLocked(), juce::dontSendNotification);
        if (lastMix != proc.value (ids::mix))
        {
            lastMix = proc.value (ids::mix);
            mix.repaint();
        }
    }

    OniriqueReverbProcessor& proc;
    Portal portal;
    ModeSelector modes, stutterModes;
    EqView curve;
    PresetBar presetBar;
    BpmControl bpm;
    StutterView stutterView;
    Knob preDelay, preNote, decay, decayNote, size, diffusion, modRate, modDepth, width, output, mix, ducking, clean, pulse, stutterMix;
    ChoicePill pulseRate, stutterRate;
    GlowToggle infinite, bypass, lock;
    juce::AudioProcessorValueTreeState::ButtonAttachment infiniteAttach, bypassAttach;
    juce::Image sigil, brushed;
    float lastMix = -1.0f;
    Fonts fonts;
};

//==============================================================================
OniriqueReverbEditor::OniriqueReverbEditor (OniriqueReverbProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    content = std::make_unique<Content> (p);
    addAndMakeVisible (*content);

   #if ! ONIRIQUE_TEST_BUILD   // la version d'essai locale n'a pas de licence
    licenseOverlay = std::make_unique<onirique::license::Overlay> (JucePlugin_Name, p.license);
    addAndMakeVisible (*licenseOverlay);
   #endif

    // taille d'ouverture : la règle commune des plugins ONIRIQUE (GUI/OniriqueWindowSize.h)
    const int w = onirique::window::openingSize (p.apvts.state, JucePlugin_Name, { baseWidth, baseHeight },
                                                 { baseWidth * 6 / 10, baseHeight * 6 / 10 }, { baseWidth * 2, baseHeight * 2 }).x;
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setResizeLimits (baseWidth * 6 / 10, baseHeight * 6 / 10, baseWidth * 2, baseHeight * 2);
    setSize (w, juce::roundToInt ((float) w * baseHeight / baseWidth));
    sizeRestored = true;

   #if ONIRIQUE_UI_TEST
    // essais locaux uniquement : ONIRIQUE_UI_TEST=static fige les animations (visuel de démo : chaque bouton ne redessine que lui-même)
    if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("static")) { refreshDisplays (0.0); return; }
   #endif
    startTimerHz (30);
}

OniriqueReverbEditor::~OniriqueReverbEditor()
{
    stopTimer();
    onirique::window::rememberOnThisComputer (JucePlugin_Name, getWidth(), getHeight());
    licenseOverlay.reset();
    content.reset();
    setLookAndFeel (nullptr);
}

void OniriqueReverbEditor::paint (juce::Graphics& g)
{
    g.fillAll (pal::plateBottom);
}

void OniriqueReverbEditor::resized()
{
    if (content == nullptr)
        return;

    const float scale = (float) getWidth() / (float) baseWidth;
    content->setTransform (juce::AffineTransform::scale (scale));
    content->setBounds (0, 0, baseWidth, baseHeight);

    if (licenseOverlay != nullptr)
    {
        const int width = licenseOverlay->pillWidth();
        licenseOverlay->fit (getLocalBounds(), scale, { 1106 - width, 16, width, 30 });
    }
    if (sizeRestored) onirique::window::remember (proc.apvts.state, getWidth(), getHeight());   // taille gardée avec le projet
}

void OniriqueReverbEditor::refreshDisplays (double seconds)
{
    if (content != nullptr)
        content->refresh (seconds);
}
