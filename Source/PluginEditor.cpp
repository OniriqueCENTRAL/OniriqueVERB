#include "PluginEditor.h"
#include "GUI/OniriqueKnob.h"
#include "GUI/OniriqueWindowSize.h"

using namespace onirique;
using namespace onirique::ui;
namespace pal = onirique::ui::palette;

namespace
{
constexpr float twoPi = juce::MathConstants<float>::twoPi;

bool approach (float& v, float target, float k) noexcept
{
    if (std::abs (target - v) < 1.0e-3f) { const bool moved = v != target; v = target; return moved; }
    v += k * (target - v);
    return true;
}

// texte incandescent : le tracé des lettres, avec son halo
void glowText (juce::Graphics& g, const juce::String& text, const juce::Font& font, float x, float baseline, juce::Colour colour, float spread)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, x, baseline);
    juce::Path path;
    glyphs.createPath (path);
    strokeGlow (g, path, colour.withAlpha (0.55f), 0.5f, spread);
    g.setColour (colour);
    g.fillPath (path);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const FontSet& fonts, const juce::String& title)
{
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 16.0f);
    g.setGradientFill (juce::ColourGradient (pal::deep2.withAlpha (0.78f), r.getX(), r.getY(), pal::deep.withAlpha (0.62f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 16.0f);
    g.setColour (pal::line());
    g.drawRoundedRectangle (r.reduced (0.5f), 16.0f, 1.0f);
    g.setColour (pal::rose.withAlpha (0.07f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 18.0f, r.getRight() - 18.0f);
    drawEyebrow (g, fonts, title, r.getX() + 16.0f, r.getY() + 17.0f);
}

juce::String valueText (juce::RangedAudioParameter* p) { return p->getCurrentValueAsText(); }
} // namespace

//==============================================================================
ReverbLook::ReverbLook()
{
    setColour (juce::PopupMenu::backgroundColourId, pal::deep);
    setColour (juce::PopupMenu::textColourId, pal::mist);
    setColour (juce::PopupMenu::headerTextColourId, pal::haze);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, pal::ember.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, pal::bone);
    setColour (juce::TooltipWindow::backgroundColourId, pal::deep);
    setColour (juce::TooltipWindow::textColourId, pal::mist);
    setColour (juce::TooltipWindow::outlineColourId, pal::ember.withAlpha (0.4f));
    setColour (juce::AlertWindow::backgroundColourId, pal::deep);
    setColour (juce::AlertWindow::textColourId, pal::mist);
    setColour (juce::AlertWindow::outlineColourId, pal::ember.withAlpha (0.4f));
    setColour (juce::TextEditor::backgroundColourId, pal::night);
    setColour (juce::TextEditor::textColourId, pal::mist);
    setColour (juce::TextEditor::outlineColourId, pal::line());
    setColour (juce::TextEditor::focusedOutlineColourId, pal::ember);
    setColour (juce::TextButton::buttonColourId, pal::deep2);
    setColour (juce::TextButton::textColourOffId, pal::mist);
}

void ReverbLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    drawKnob (g, juce::Rectangle<int> (x, y, w, h).toFloat(), pos, start, end, pal::ember, false, s.isMouseOverOrDragging());
}

juce::Font ReverbLook::getPopupMenuFont()
{
    return Fonts()->bodyFont (15.0f);
}

//==============================================================================
Knob::Knob (OniriqueReverbProcessor& p, const char* id, const char* n, const char* tip, bool h)
    : proc (p), param (p.apvts.getParameter (id)), attachment (p.apvts, id, slider), name (juce::String::fromUTF8 (n)), hero (h)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.setMouseDragSensitivity (hero ? 320 : 220);
    slider.setTooltip (juce::String::fromUTF8 (tip));
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (hero ? 22 : 18);
    r.removeFromBottom (hero ? 44 : 20);
    const int d = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (d, d));
}

void Knob::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const bool hot = slider.isMouseOverOrDragging();
    g.setColour (hot ? pal::bone : pal::haze);
    g.setFont (fonts->monoBoldFont (hero ? 12.0f : 10.5f, 0.16f));
    g.drawText (name, b.withHeight (hero ? 20.0f : 16.0f), juce::Justification::centred, false);

    if (! hero)
    {
        g.setColour (hot ? pal::ember.brighter (0.3f) : pal::mist);
        g.setFont (fonts->monoFont (11.5f));
        g.drawText (valueText (param), b.withTop (b.getBottom() - 18.0f), juce::Justification::centred, false);
        return;
    }

    // MIX : l'anneau sec (os, à gauche) / mouillé (braise, à droite), qui montent du bas selon les volumes réels
    const auto sb = slider.getBounds().toFloat();
    const auto c = sb.getCentre();
    const float R = sb.getWidth() * 0.5f + 9.0f;
    reverb::Params rp;
    rp.mix = proc.value (ids::mix) * 0.01f;
    const auto gains = reverb::Engine::mixGains (rp);
    const float pi = juce::MathConstants<float>::pi;
    auto arc = [&] (float from, float to, juce::Colour col, float width, float alpha)
    {
        juce::Path p;
        p.addCentredArc (c.x, c.y, R, R, 0.0f, from, to, true);
        if (alpha > 0.3f) strokeGlow (g, p, col.withAlpha (0.5f * alpha), width, 4.0f);
        g.setColour (col.withAlpha (alpha));
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    arc (pi * 1.08f, pi * 1.92f, pal::rose, 3.0f, 0.10f);
    arc (pi * 0.08f, pi * 0.92f, pal::rose, 3.0f, 0.10f);
    arc (pi * 0.92f - gains[1] * pi * 0.84f, pi * 0.92f, pal::ember, 3.0f, 0.95f);   // mouillé : à droite, depuis le bas
    arc (pi * 1.08f, pi * 1.08f + gains[0] * pi * 0.84f, pal::bone, 3.0f, 0.8f);    // sec : à gauche, depuis le bas

    g.setFont (fonts->monoFont (9.5f, 0.18f));
    g.setColour (pal::haze);
    g.drawText ("SEC", juce::Rectangle<float> (c.x - R - 30.0f, c.y + R - 14.0f, 40.0f, 14.0f), juce::Justification::centredRight, false);
    g.setColour (pal::ember.brighter (0.2f));
    g.drawText ("REVERB", juce::Rectangle<float> (c.x + R - 10.0f, c.y + R - 14.0f, 60.0f, 14.0f), juce::Justification::centredLeft, false);

    const auto text = juce::String (juce::roundToInt (proc.value (ids::mix))) + "%";
    const auto font = fonts->displayFont (28.0f, 0.02f);
    const float w = juce::GlyphArrangement::getStringWidth (font, text);
    glowText (g, text, font, b.getCentreX() - w * 0.5f, b.getBottom() - 10.0f, hot ? pal::bone : pal::mist, hot ? 5.0f : 2.5f);
}

//==============================================================================
ModeSelector::ModeSelector (OniriqueReverbProcessor& p, const char* id, const juce::StringArray& l, const char* tip)
    : param (p.apvts.getParameter (id)),
      attachment (*param, [this] (float v) { current = juce::roundToInt (v); repaint(); }),
      labels (l)
{
    setTooltip (juce::String::fromUTF8 (tip));
    attachment.sendInitialUpdate();
    shown = (float) current;
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ModeSelector::tick()
{
    if (approach (shown, (float) current, 0.28f)) repaint();
}

void ModeSelector::mouseDown (const juce::MouseEvent& e)
{
    const int i = juce::jlimit (0, labels.size() - 1, (int) (e.position.x / ((float) getWidth() / (float) labels.size())));
    attachment.setValueAsCompleteGesture ((float) i);
}

void ModeSelector::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float r = b.getHeight() * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (b, r);
    g.setColour (pal::line());
    g.drawRoundedRectangle (b, r, 1.0f);

    const float seg = b.getWidth() / (float) labels.size();
    const auto pill = juce::Rectangle<float> (b.getX() + shown * seg, b.getY(), seg, b.getHeight()).reduced (3.0f);
    juce::Path pp;
    pp.addRoundedRectangle (pill, pill.getHeight() * 0.5f);
    strokeGlow (g, pp, pal::ember.withAlpha (0.55f), 0.0f, 5.0f);
    g.setGradientFill (juce::ColourGradient (pal::ember, pill.getX(), pill.getY(), pal::blood, pill.getX(), pill.getBottom(), false));
    g.fillPath (pp);
    g.setColour (pal::bone.withAlpha (0.25f));
    g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, 1.0f);

    g.setFont (fonts->monoBoldFont (11.0f, 0.14f));
    for (int i = 0; i < labels.size(); ++i)
    {
        const float near = juce::jmax (0.0f, 1.0f - std::abs (shown - (float) i));
        g.setColour (pal::haze.interpolatedWith (pal::bone, near));
        g.drawText (labels[i], juce::Rectangle<float> (b.getX() + (float) i * seg, b.getY(), seg, b.getHeight()), juce::Justification::centred, false);
    }
}

//==============================================================================
Portal::Portal (OniriqueReverbProcessor& p)
    : proc (p), sizeParam (p.apvts.getParameter (ids::size)), decayParam (p.apvts.getParameter (ids::decay)),
      decayNoteParam (p.apvts.getParameter (ids::decayNote))
{
    wet.assign (1024, 0.0f);
    dry.assign (1024, 0.0f);
    for (int i = 0; i < 70; ++i)
        motes.push_back ({ rng.nextFloat() * 2.0f - 1.0f, rng.nextFloat() * 2.0f - 1.0f, 0.1f + 0.9f * rng.nextFloat() });
    sigil = juce::ImageCache::getFromMemory (BinaryData::sigil_png, BinaryData::sigil_pngSize);
    lastWrite = p.getEngine().getWritePos();
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setTooltip (juce::String::fromUTF8 ("LE PORTAIL : l'espace de la reverb vu de face. Chaque anneau est un instant de la queue qui s'enfonce dans la "
                "profondeur. Glisser à gauche / à droite : SIZE. Glisser vers le haut / le bas : DECAY. Double-clic : valeurs d'origine."));
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
    spin += dt * 0.12f;
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
    const auto b = getLocalBounds().toFloat();
    juce::Path frame;
    frame.addRoundedRectangle (b, 18.0f);
    juce::Graphics::ScopedSaveState save (g);
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

    // le fond : un cœur de sang qui s'embrase avec la queue
    {
        juce::ColourGradient bg (pal::blood.withMultipliedBrightness (0.55f + 0.7f * glow), c.x, c.y,
                                 pal::night, c.x + b.getWidth() * 0.55f, c.y, true);
        bg.addColour (0.35, pal::bloodDeep.interpolatedWith (pal::blood, 0.25f * glow));
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

        float alpha = (0.07f + 0.93f * level) * std::pow (1.0f - z, 0.7f);
        if (z < 0.06f) alpha *= z / 0.06f;
        alpha = juce::jmin (1.0f, alpha * (1.0f + 0.6f * frozen * (0.5f + 0.5f * std::sin (clock * 2.0f + (float) j * 0.4f))));
        if (alpha < 0.01f) continue;

        auto col = pal::bone.interpolatedWith (pal::ember, juce::jlimit (0.0f, 1.0f, z * 1.8f - 0.35f * bright + 0.2f));
        col = col.interpolatedWith (pal::blood.brighter (0.2f), juce::jlimit (0.0f, 1.0f, (z - 0.55f) * 2.2f));
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

        const float strokeW = (0.7f + 2.4f * level) * (0.4f + 0.6f * persp);
        if (diffusion < 0.55f)   // peu de diffusion : on devine les échos séparés
        {
            juce::Path dashed;
            const float dash[] { 2.0f + 14.0f * diffusion * persp, 4.0f + 10.0f * (1.0f - diffusion) * persp };
            juce::PathStrokeType (strokeW).createDashedStroke (dashed, ring, dash, 2);
            ring = dashed;
            g.setColour (col.withAlpha (alpha * 0.35f));
            g.fillPath (ring);
            g.setColour (col.withAlpha (alpha));
            g.fillPath (ring);
            continue;
        }
        if (level > 0.15f) strokeGlow (g, ring, col.withAlpha (alpha * 0.6f), strokeW, 2.0f + 5.0f * level * persp);
        g.setColour (col.withAlpha (alpha));
        g.strokePath (ring, juce::PathStrokeType (strokeW));
    }

    // la poussière d'étoiles : elle vient vers nous (et monte en SHIMMER / ONIRIQUE)
    for (const auto& m : motes)
    {
        const juce::Point<float> pt (c.x + m.x / m.z * b.getWidth() * 0.09f, c.y + m.y / m.z * b.getHeight() * 0.11f);
        const float s = juce::jmin (9.0f, 0.9f / m.z);
        const float a = juce::jlimit (0.0f, 1.0f, (1.0f - m.z) * 1.3f) * (0.35f + 0.65f * glow);
        if (s > 3.2f)
            drawStar (g, pt, s * 1.6f, a * 0.8f);
        else
        {
            g.setColour (pal::rose.withAlpha (a * 0.7f));
            g.fillEllipse (juce::Rectangle<float> (s, s).withCentre (pt));
        }
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
            const float a = s == 2 ? 0.85f : 0.28f;
            strokeGlow (g, ribbon, pal::rose.withAlpha (0.25f * a), 1.0f, 3.0f);
            g.setColour (pal::bone.withAlpha (a));
            g.strokePath (ribbon, juce::PathStrokeType (s == 2 ? 1.4f : 0.9f));
        }
    }

    // le sigil au point de fuite, dans son réticule
    {
        const float halo = 70.0f + 60.0f * glow;
        g.setGradientFill (juce::ColourGradient (pal::ember.withAlpha (0.45f * (0.3f + glow)), c.x, c.y, pal::ember.withAlpha (0.0f), c.x + halo, c.y, true));
        g.fillEllipse (juce::Rectangle<float> (2.0f * halo, 2.0f * halo).withCentre (c));

        const float rr = 30.0f;
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (juce::Rectangle<float> (2.0f * rr, 2.0f * rr).withCentre (c));
        g.setColour (pal::rose.withAlpha (0.3f));
        g.drawEllipse (juce::Rectangle<float> (2.0f * rr, 2.0f * rr).withCentre (c), 1.0f);
        for (int t = 0; t < 4; ++t)
        {
            const float a = spin + (float) t * twoPi * 0.25f;
            const juce::Point<float> d (std::cos (a), std::sin (a));
            g.setColour (pal::ember.withAlpha (0.8f));
            g.drawLine (juce::Line<float> (c + d * (rr + 3.0f), c + d * (rr + 10.0f)), 1.6f);
        }
        juce::Path arcs;
        arcs.addCentredArc (c.x, c.y, rr + 6.0f, rr + 6.0f, -spin * 1.7f, 0.2f, 1.3f, true);
        arcs.addCentredArc (c.x, c.y, rr + 6.0f, rr + 6.0f, -spin * 1.7f, 3.34f, 4.44f, true);
        g.setColour (pal::rose.withAlpha (0.35f));
        g.strokePath (arcs, juce::PathStrokeType (1.0f));
        const float ss = 38.0f * (1.0f + 0.12f * glow);
        g.setOpacity (0.75f + 0.25f * glow);
        g.drawImage (sigil, juce::Rectangle<float> (ss, ss).withCentre (c), juce::RectanglePlacement::centred);
        g.setOpacity (1.0f);
    }

    // voile sur les bords
    {
        juce::ColourGradient v (juce::Colours::transparentBlack, c.x, c.y, juce::Colours::black.withAlpha (0.7f), c.x + b.getWidth() * 0.52f, c.y, true);
        v.addColour (0.6, juce::Colours::transparentBlack);
        g.setGradientFill (v);
        g.fillRect (b);
    }

    // les inscriptions
    drawEyebrow (g, *fonts, "LE PORTAIL", 18.0f, 22.0f);
    {
        const auto tag = [&] (const char* label, const char* id) { return juce::String (label) + " " + valueText (proc.apvts.getParameter (id)); };
        const bool sync = proc.isSynced();
        g.setFont (fonts->monoFont (11.0f, 0.08f));
        g.setColour (dragging ? pal::ember.brighter (0.3f) : pal::haze);
        g.drawText ((sync ? "BPM " + juce::String (proc.getBpm(), 1) + "   " : juce::String())
                        + tag ("PRE", sync ? ids::preNote : ids::preDelay) + "   " + tag ("DECAY", sync ? ids::decayNote : ids::decay)
                        + "   " + tag ("SIZE", ids::size),
                    b.reduced (18.0f, 12.0f).withHeight (20.0f), juce::Justification::centredRight, false);
    }
    {
        static const char* dreamNames[] { "Room", "Plate I", "Plate II", "Hall", "Shimmer", "Onirique" };
        g.setColour (pal::bone.withAlpha (0.16f));
        g.setFont (fonts->dreamFont (58.0f));
        g.drawText (dreamNames[juce::jlimit (0, 5, mode)], juce::Rectangle<float> (20.0f, b.getBottom() - 72.0f, 400.0f, 64.0f),
                    juce::Justification::bottomLeft, false);
    }
    g.setColour (pal::haze.withAlpha (0.55f));
    g.setFont (fonts->monoFont (9.5f, 0.12f));
    g.drawText (juce::CharPointer_UTF8 ("GLISSER   \xe2\x86\x94 SIZE   \xe2\x86\x95 DECAY"), b.reduced (18.0f, 12.0f).withTop (b.getBottom() - 32.0f),
                juce::Justification::centredRight, false);
    if (frozen > 0.01f)
    {
        g.setFont (fonts->monoBoldFont (12.0f, 0.3f));
        g.setColour (pal::ember.brighter (0.4f).withAlpha (frozen * (0.65f + 0.35f * std::sin (clock * 3.0f))));
        g.drawText (juce::CharPointer_UTF8 ("\xe2\x88\x9e  INFINITE"), b.withHeight (46.0f), juce::Justification::centred, false);
    }

    g.setColour (pal::line());
    g.drawRoundedRectangle (b.reduced (0.5f), 18.0f, 1.0f);
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
}

EqView::EqView (OniriqueReverbProcessor& p) : proc (p)
{
    setTooltip (juce::String::fromUTF8 ("L'EQ de la reverb. IN : ce qui entre dans la reverb (la voix sèche n'est pas touchée). "
                                        "OUT : la queue qui en sort. Tirer les points : les coupes en largeur ; grave, médium et aigu "
                                        "en largeur (fréquence) et en hauteur (gain). Double-clic sur un point : retour à zéro. "
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

juce::Rectangle<float> EqView::plot() const noexcept { return getLocalBounds().toFloat().reduced (8.0f, 4.0f).withTrimmedTop (20.0f); }
juce::Rectangle<float> EqView::tab (bool out) const noexcept { return { out ? 50.0f : 8.0f, 4.0f, 40.0f, 17.0f }; }

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

void EqView::tick()
{
    std::vector<float> now;
    for (bool out : { false, true })
    {
        const auto& e = idsFor (out);
        for (const char* id : { e.lowCut, e.highCut, e.lowFreq, e.lowGain, e.midFreq, e.midGain, e.highFreq, e.highGain })
            now.push_back (proc.value (id));
    }
    now.push_back (proc.value (ids::clean));
    if (now != seen) { seen = std::move (now); repaint(); }
}

void EqView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (pal::line());
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

    const auto a = plot();
    g.setFont (fonts->monoFont (8.5f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        g.setColour (pal::rose.withAlpha (f == 100.0f || f == 1000.0f || f == 10000.0f ? 0.1f : 0.05f));
        g.drawVerticalLine ((int) xFor (f), a.getY(), a.getBottom());
    }
    g.setColour (pal::rose.withAlpha (0.12f));
    g.drawHorizontalLine ((int) yFor (0.0f), a.getX(), a.getRight());
    g.setColour (pal::haze.withAlpha (0.6f));
    for (auto [f, t] : { std::pair { 100.0f, "100" }, std::pair { 1000.0f, "1k" }, std::pair { 10000.0f, "10k" } })
        g.drawText (t, juce::Rectangle<float> (xFor (f) + 3.0f, a.getBottom() - 12.0f, 30.0f, 12.0f), juce::Justification::centredLeft, false);

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
        g.setColour (pal::bone.withAlpha (0.35f));
        g.strokePath (other, juce::PathStrokeType (1.0f));
    }
    juce::Path curve, fill;
    curveFor (editOut, curve, fill);
    g.setGradientFill (juce::ColourGradient (pal::ember.withAlpha (0.38f), 0.0f, a.getY(), pal::ember.withAlpha (0.0f), 0.0f, a.getBottom(), false));
    g.fillPath (fill);
    strokeGlow (g, curve, pal::ember, 1.4f, 3.0f);
    g.setColour (pal::ember.brighter (0.25f));
    g.strokePath (curve, juce::PathStrokeType (1.6f));

    for (int band = 0; band < reverb::Eq::kBands; ++band)
    {
        const auto pt = handle (band);
        const bool hot = band == grabbed || band == hover;
        const float r = hot ? 7.0f : 5.5f;
        g.setColour (pal::ember.withAlpha (hot ? 0.45f : 0.3f));
        g.fillEllipse (juce::Rectangle<float> (r * 3.0f, r * 3.0f).withCentre (pt));
        g.setColour (gainParam (band) != nullptr ? pal::deep2 : pal::bone);
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (pt));
        g.setColour (pal::ember.brighter (gainParam (band) != nullptr ? 0.3f : 0.0f));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (pt), 1.5f);
    }

    // les onglets IN / OUT, puis la bande touchée ou le rôle de l'EQ choisi
    for (bool out : { false, true })
    {
        const auto t = tab (out);
        const bool sel = out == editOut;
        g.setColour (sel ? pal::ember : juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (t, 8.0f);
        g.setColour (sel ? pal::bone.withAlpha (0.3f) : pal::line());
        g.drawRoundedRectangle (t.reduced (0.5f), 8.0f, 1.0f);
        g.setFont (fonts->monoBoldFont (9.5f, 0.12f));
        g.setColour (sel ? pal::bone : pal::haze);
        g.drawText (out ? "OUT" : "IN", t, juce::Justification::centred, false);
    }
    g.setFont (fonts->monoBoldFont (9.5f, 0.1f));
    const int shown = grabbed >= 0 ? grabbed : hover;
    const auto info = juce::Rectangle<float> (98.0f, 4.0f, b.getWidth() - 106.0f, 17.0f);
    if (shown >= 0)
    {
        auto text = juce::String (editOut ? "OUT  " : "IN  ") + bandNames[shown] + "  " + freqParam (shown)->getCurrentValueAsText();
        if (auto* gp = gainParam (shown)) text << "  " << gp->getCurrentValueAsText();
        g.setColour (pal::bone);
        g.drawText (text, info, juce::Justification::centredRight, false);
    }
    else
    {
        g.setColour (pal::haze);
        g.drawText (editOut ? "LA QUEUE QUI SORT DE LA REVERB" : "CE QUI ENTRE DANS LA REVERB", info, juce::Justification::centredRight, false);
    }
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
        if (tab (out).contains (e.position)) { editOut = out; grabbed = -1; repaint(); return; }
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
    if (style == Style::infinite && lit > 0.0f) { pulse += 0.09f; moving = true; }
    if (moving) repaint();
}

void GlowToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    const auto c = b.getCentre();

    if (style == Style::power)
    {
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 1.0f;
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c));
        juce::Path ringP;
        ringP.addEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c));
        if (lit > 0.05f) strokeGlow (g, ringP, pal::ember.withAlpha (lit), 1.2f, 4.0f);
        g.setColour (pal::rose.withAlpha (0.2f).interpolatedWith (pal::ember, lit));
        g.strokePath (ringP, juce::PathStrokeType (1.2f));
        juce::Path icon;
        icon.addCentredArc (c.x, c.y, r * 0.42f, r * 0.42f, 0.0f, 0.75f, twoPi - 0.75f, true);
        icon.startNewSubPath (c.x, c.y - r * 0.55f);
        icon.lineTo (c.x, c.y - r * 0.1f);
        g.setColour (pal::haze.interpolatedWith (pal::bone, highlighted ? 1.0f : lit));
        g.strokePath (icon, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }

    if (style == Style::lock)
    {
        const auto col = pal::haze.withAlpha (highlighted ? 0.9f : 0.55f).interpolatedWith (pal::ember.brighter (0.3f), lit);
        const auto body = juce::Rectangle<float> (b.getWidth() * 0.62f, b.getHeight() * 0.45f).withCentre (c.translated (0.0f, b.getHeight() * 0.17f));
        juce::Path shackle;
        const float sw = body.getWidth() * 0.32f;
        shackle.startNewSubPath (c.x - sw, body.getY());
        shackle.lineTo (c.x - sw, body.getY() - b.getHeight() * 0.12f);
        shackle.addCentredArc (c.x, body.getY() - b.getHeight() * 0.12f, sw, sw, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
        shackle.lineTo (c.x + sw, body.getY() - (1.0f - lit) * b.getHeight() * 0.1f);
        g.setColour (col);
        g.strokePath (shackle, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillRoundedRectangle (body, 2.0f);
        return;
    }

    // INFINITE : une carte qui s'embrase et respire
    const float breath = 0.5f + 0.5f * std::sin (pulse);
    juce::Path card;
    card.addRoundedRectangle (b, 14.0f);
    if (lit > 0.02f) strokeGlow (g, card, pal::ember.withAlpha (lit * (0.5f + 0.5f * breath)), 1.0f, 7.0f);
    g.setGradientFill (juce::ColourGradient (pal::deep2.interpolatedWith (pal::blood, lit * (0.6f + 0.3f * breath)), b.getX(), b.getY(),
                                             pal::night.interpolatedWith (pal::bloodDeep, lit), b.getX(), b.getBottom(), false));
    g.fillPath (card);
    g.setColour (pal::line().interpolatedWith (pal::ember, lit * 0.8f));
    g.strokePath (card, juce::PathStrokeType (highlighted ? 1.6f : 1.0f));

    const auto sym = fonts->monoBoldFont (54.0f);
    const auto symText = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e"));
    const float symW = juce::GlyphArrangement::getStringWidth (sym, symText);
    glowText (g, symText, sym, b.getX() + 26.0f, c.y + 18.0f, pal::haze.interpolatedWith (pal::bone, lit), 2.0f + 6.0f * lit);

    const float tx = b.getX() + 40.0f + symW;
    glowText (g, "INFINITE", fonts->displayFont (17.0f, 0.06f), tx, c.y - 2.0f, pal::mist.interpolatedWith (pal::bone, lit), 1.5f + 3.0f * lit);
    g.setFont (fonts->monoFont (9.5f, 0.12f));
    g.setColour (pal::haze.interpolatedWith (pal::ember.brighter (0.4f), lit));
    g.drawText (lit > 0.5f ? juce::String::fromUTF8 ("FEEDBACK FIGÉ") : juce::String ("FIGER LE FEEDBACK"),
                juce::Rectangle<float> (tx, c.y + 6.0f, b.getRight() - tx - 8.0f, 16.0f), juce::Justification::centredLeft, false);
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
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
    g.setColour (isMouseOver() ? pal::ember : pal::line());
    g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.0f);
    g.setColour (pal::mist);
    g.setFont (fonts->monoBoldFont (11.5f));
    g.drawText (param->getCurrentChoiceName(), b.withTrimmedRight (12.0f), juce::Justification::centred, false);
    juce::Path tri;
    const float x = b.getRight() - 15.0f, y = b.getCentreY();
    tri.addTriangle (x, y - 2.0f, x + 7.0f, y - 2.0f, x + 3.5f, y + 2.5f);
    g.setColour (pal::ember);
    g.fillPath (tri);
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
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
    g.setColour (pal::line());
    g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.0f);

    for (int side : { -1, 1 })
    {
        const float x = side < 0 ? b.getX() + 17.0f : b.getRight() - 17.0f, y = b.getCentreY();
        juce::Path chevron;
        chevron.startNewSubPath (x - 2.5f * (float) side, y - 5.0f);
        chevron.lineTo (x + 2.5f * (float) side, y);
        chevron.lineTo (x - 2.5f * (float) side, y + 5.0f);
        g.setColour (pal::haze);
        g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }
    g.setColour (pal::line());
    g.drawVerticalLine ((int) b.getX() + 34, b.getY() + 7.0f, b.getBottom() - 7.0f);
    g.drawVerticalLine ((int) b.getRight() - 34, b.getY() + 7.0f, b.getBottom() - 7.0f);

    const auto inner = b.reduced (40.0f, 0.0f);
    g.setFont (fonts->monoFont (8.5f, 0.2f));
    g.setColour (pal::haze.withAlpha (0.7f));
    g.drawText ("PRESET", inner.withWidth (52.0f), juce::Justification::centredLeft, false);
    g.setFont (fonts->bodyFont (14.5f));
    g.setColour (pal::mist);
    g.drawText (shownName + (shownModified ? " *" : ""), inner.withTrimmedLeft (52.0f), juce::Justification::centredLeft, true);
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
    setTooltip (juce::String::fromUTF8 ("BPM : allumé, la longueur de la reverb (DECAY) et le PRE-DELAY se règlent en valeurs de notes, "
                                        "calculées au tempo. Le tempo suit le DAW, ou se choisit : glisser sur le chiffre (Maj : réglage fin), "
                                        "double-clic pour le taper, menu à droite pour revenir au tempo du DAW. PULSE et STUTTER suivent ce tempo."));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int BpmControl::zoneAt (float x) const noexcept
{
    return x < 66.0f ? 0 : (x > (float) getWidth() - 58.0f ? 2 : 1);
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
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float r = b.getHeight() * 0.5f;
    juce::Path pill;
    pill.addRoundedRectangle (b, r);
    if (lit > 0.05f) strokeGlow (g, pill, pal::ember.withAlpha (0.6f * lit), 1.0f, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillPath (pill);
    g.setColour (pal::line().interpolatedWith (pal::ember, 0.8f * lit));
    g.strokePath (pill, juce::PathStrokeType (1.0f));

    // le bouton BPM : une LED et son nom
    const juce::Point<float> led (b.getX() + 17.0f, b.getCentreY());
    if (lit > 0.05f)
    {
        g.setColour (pal::ember.withAlpha (0.35f * lit));
        g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (led));
    }
    g.setColour (pal::rose.withAlpha (0.2f).interpolatedWith (pal::ember.brighter (0.3f), lit));
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (led));
    g.setFont (fonts->monoBoldFont (12.0f, 0.12f));
    g.setColour (pal::haze.interpolatedWith (pal::bone, lit));
    g.drawText ("BPM", juce::Rectangle<float> (b.getX() + 27.0f, b.getY(), 40.0f, b.getHeight()), juce::Justification::centredLeft, false);

    g.setColour (pal::line());
    g.drawVerticalLine (66, b.getY() + 7.0f, b.getBottom() - 7.0f);
    g.drawVerticalLine (getWidth() - 58, b.getY() + 7.0f, b.getBottom() - 7.0f);

    // le tempo
    g.setFont (fonts->monoBoldFont (15.0f));
    g.setColour (draggingValue ? pal::ember.brighter (0.4f) : pal::mist);
    g.drawText (juce::String (shownBpm, 1), juce::Rectangle<float> (66.0f, b.getY(), (float) getWidth() - 124.0f, b.getHeight()),
                juce::Justification::centred, false);

    // d'où il vient
    g.setFont (fonts->monoBoldFont (10.0f, 0.12f));
    g.setColour (shownSource == 1 ? pal::ember.brighter (0.3f) : pal::haze);
    g.drawText (shownSource == 1 ? "MAN" : "DAW", juce::Rectangle<float> ((float) getWidth() - 56.0f, b.getY(), 36.0f, b.getHeight()),
                juce::Justification::centred, false);
    juce::Path tri;
    const float x = b.getRight() - 17.0f, y = b.getCentreY();
    tri.addTriangle (x, y - 2.0f, x + 7.0f, y - 2.0f, x + 3.5f, y + 2.5f);
    g.setColour (pal::ember);
    g.fillPath (tri);
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
    setTooltip (juce::String::fromUTF8 ("Le motif du STUTTER sur une mesure. CHOP : la queue hachée net. REPEAT : le premier morceau de "
                                        "chaque groupe de quatre est rejoué trois fois. RAMP : les coupes accélèrent à chaque temps, "
                                        "jusqu'à la relance. La voix sèche n'est jamais découpée, seulement la reverb."));
}

void StutterView::tick()
{
    beat = proc.getEngine().getBeat();
    repaint();
}

void StutterView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (pal::line());
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

    const auto area = b.reduced (12.0f, 10.0f).withTrimmedTop (12.0f);
    const int mode = juce::roundToInt (proc.value (ids::stutterMode));
    const double period = stutterRateBeats (juce::roundToInt (proc.value (ids::stutterRate)));
    const float amount = 0.3f + 0.7f * proc.value (ids::stutterMix) * 0.01f;
    const auto xAt = [&] (double t) { return area.getX() + (float) (t / 4.0) * area.getWidth(); };

    g.setFont (fonts->monoFont (8.5f, 0.1f));
    for (int k = 0; k < 4; ++k)
    {
        g.setColour (pal::rose.withAlpha (0.1f));
        g.drawVerticalLine ((int) xAt (k), area.getY(), area.getBottom());
        g.setColour (pal::haze.withAlpha (0.6f));
        g.drawText (juce::String (k + 1), juce::Rectangle<float> (xAt (k) + 3.0f, b.getY() + 4.0f, 20.0f, 12.0f), juce::Justification::centredLeft, false);
    }

    if (mode == 0)
    {
        g.setFont (fonts->monoBoldFont (10.0f, 0.14f));
        g.setColour (pal::haze.withAlpha (0.7f));
        g.drawText (juce::String::fromUTF8 ("STUTTER COUPÉ · CHOISIR CHOP, REPEAT OU RAMP"), area, juce::Justification::centred, false);
        return;
    }

    if (mode == 2)   // REPEAT : les groupes de quatre découpes
    {
        const int count = juce::roundToInt (4.0 / period);
        for (int k = 0; k < count; ++k)
        {
            const auto slice = juce::Rectangle<float> (xAt (k * period), area.getY(), xAt ((k + 1) * period) - xAt (k * period), area.getHeight()).reduced (1.5f, 0.0f);
            const bool rec = k % 4 == 0;
            g.setColour ((rec ? pal::ember : pal::bone).withAlpha ((rec ? 0.45f : 0.16f) * amount));
            g.fillRoundedRectangle (slice, 3.0f);
            g.setColour (pal::ember.withAlpha (0.8f * amount));
            g.drawRoundedRectangle (slice, 3.0f, 1.0f);
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
        g.setGradientFill (juce::ColourGradient (pal::ember.withAlpha (0.5f * amount), 0.0f, area.getY(), pal::blood.withAlpha (0.15f * amount), 0.0f, area.getBottom(), false));
        g.fillPath (fill);
        g.setColour (pal::ember.brighter (0.2f).withAlpha (amount));
        g.strokePath (gate, juce::PathStrokeType (1.2f));
    }

    // la tête de lecture
    const float px = xAt (std::fmod (std::max (0.0, beat), 4.0));
    juce::Path head;
    head.startNewSubPath (px, area.getY() - 4.0f);
    head.lineTo (px, area.getBottom() + 2.0f);
    strokeGlow (g, head, pal::bone, 1.0f, 3.0f);
    g.setColour (pal::bone);
    g.strokePath (head, juce::PathStrokeType (1.4f));
}

//==============================================================================
struct OniriqueReverbEditor::Content : public juce::Component
{
    explicit Content (OniriqueReverbProcessor& p)
        : proc (p), portal (p),
          modes (p, ids::mode, modeNames),
          stutterModes (p, ids::stutterMode, stutterModeNames,
                        "STUTTER : découpe la queue de la reverb au tempo. CHOP : hachée net. REPEAT : un morceau rejoué "
                        "en boucle. RAMP : les coupes accélèrent sur la mesure. OFF : la queue entière."),
          curve (p), presetBar (p), bpm (p), stutterView (p),
          preDelay (p, ids::preDelay, "PRE-DELAY", "Le silence entre la voix et le début de la reverb. Un peu de pre-delay (20 à 80 ms) "
                                                     "garde la voix nette devant, la reverb arrive juste derrière."),
          preNote (p, ids::preNote, "PRE · BPM", "Le pre-delay au tempo (bouton BPM allumé) : de 0 à une noire. T : triolet, D : pointé."),
          decay (p, ids::decay, "DECAY", "La durée de la queue : le temps qu'il faut à la reverb pour s'éteindre."),
          decayNote (p, ids::decayNote, "DECAY · BPM", "La durée de la queue au tempo (bouton BPM allumé) : d'un temps à huit mesures."),
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
          infinite (GlowToggle::Style::infinite, "INFINITE : fige le feedback de la reverb, la queue tient indéfiniment ; la voix "
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
        grain = juce::Image (juce::Image::ARGB, 160, 160, true);
        juce::Random r (7);
        for (int y = 0; y < grain.getHeight(); ++y)
            for (int x = 0; x < grain.getWidth(); ++x)
                grain.setPixelAt (x, y, (r.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (r.nextFloat() * 0.045f));
        showSync();
    }

    void resized() override
    {
        presetBar.setBounds (392, 13, 320, 34);
        bpm.setBounds (726, 13, 186, 34);
        bypass.setBounds (1120, 12, 36, 36);
        portal.setBounds (24, 64, 1132, 296);
        modes.setBounds (290, 372, 600, 34);

        const int knobY = 448, knobH = 144;
        Knob* left[] { &preDelay, &decay, &size, &diffusion };
        Knob* right[] { &modRate, &modDepth, &width, &output };
        for (int i = 0; i < 4; ++i)
        {
            left[i]->setBounds (34 + i * 98, knobY, 94, knobH);
            right[i]->setBounds (762 + i * 98, knobY, 94, knobH);
        }
        preNote.setBounds (preDelay.getBounds());
        decayNote.setBounds (decay.getBounds());
        mix.setBounds (466, 418, 248, 190);
        lock.setBounds (590 + 44, 418 + 190 - 34, 20, 20);

        // EQ
        curve.setBounds (36, 640, 640, 100);
        ducking.setBounds (690, 634, 86, 108);
        clean.setBounds (780, 634, 86, 108);
        infinite.setBounds (878, 630, 266, 106);

        // RYTHME : STUTTER puis PULSE
        stutterModes.setBounds (40, 796, 300, 32);
        stutterRate.setBounds (122, 840, 78, 26);
        stutterMix.setBounds (352, 774, 88, 108);
        stutterView.setBounds (452, 782, 466, 96);
        pulse.setBounds (938, 774, 88, 108);
        pulseRate.setBounds (1042, 820, 98, 26);
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.fillAll (pal::night);
        {
            juce::ColourGradient top (pal::blood.withAlpha (0.55f), b.getCentreX(), 200.0f, pal::blood.withAlpha (0.0f), b.getCentreX() + 720.0f, 200.0f, true);
            g.setGradientFill (top);
            g.fillRect (b);
            g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0.0f, 380.0f, juce::Colours::black.withAlpha (0.5f), 0.0f, b.getBottom(), false));
            g.fillRect (b);
        }
        g.setTiledImageFill (grain, 0, 0, 1.0f);
        g.fillRect (b);

        // en-tête : sigil, ONIRIQUE en capitales, « Verb » en gothique incandescent
        g.drawImage (sigil, juce::Rectangle<float> (22.0f, 12.0f, 36.0f, 36.0f), juce::RectanglePlacement::centred);
        const auto brand = fonts->displayFont (17.0f, 0.1f);
        glowText (g, "ONIRIQUE", brand, 68.0f, 37.0f, pal::mist, 1.5f);
        const float bw = juce::GlyphArrangement::getStringWidth (brand, "ONIRIQUE");
        glowText (g, "Verb", fonts->dreamFont (38.0f), 68.0f + bw + 10.0f, 40.0f, pal::ember.brighter (0.15f), 6.0f);

        // la scène sous le PORTAIL : le sélecteur flotte dans un trait de lumière
        {
            juce::ColourGradient beam (pal::ember.withAlpha (0.0f), 60.0f, 389.0f, pal::ember.withAlpha (0.0f), 1120.0f, 389.0f, false);
            beam.addColour (0.5, pal::ember.withAlpha (0.45f));
            g.setGradientFill (beam);
            g.fillRect (60.0f, 388.5f, 1060.0f, 1.0f);
        }

        drawPanel (g, { 24.0f, 418.0f, 412.0f, 184.0f }, *fonts, "ESPACE");
        drawPanel (g, { 744.0f, 418.0f, 412.0f, 184.0f }, *fonts, "MOUVEMENT");
        drawPanel (g, { 24.0f, 614.0f, 1132.0f, 134.0f }, *fonts, "EQ");
        drawPanel (g, { 24.0f, 760.0f, 1132.0f, 128.0f }, *fonts, juce::String::fromUTF8 ("RYTHME  ·  STUTTER"));

        // halo derrière le MIX
        const auto mc = mix.getBounds().toFloat().getCentre().translated (0.0f, -12.0f);
        g.setGradientFill (juce::ColourGradient (pal::ember.withAlpha (0.18f), mc.x, mc.y, pal::ember.withAlpha (0.0f), mc.x + 150.0f, mc.y, true));
        g.fillEllipse (juce::Rectangle<float> (300.0f, 300.0f).withCentre (mc));

        g.setFont (fonts->monoBoldFont (9.5f, 0.16f));
        g.setColour (pal::haze);
        g.drawText ("DIVISION", juce::Rectangle<float> (40.0f, 840.0f, 76.0f, 26.0f), juce::Justification::centredLeft, false);
        g.drawText ("DIVISION", juce::Rectangle<float> (1042.0f, 800.0f, 98.0f, 16.0f), juce::Justification::centred, false);
        g.setColour (pal::line());
        g.drawVerticalLine (928, 782.0f, 878.0f);
    }

    // BPM allumé : DECAY et PRE-DELAY se règlent en valeurs de notes
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
        curve.tick();
        presetBar.tick();
        bpm.tick();
        infinite.tick();
        bypass.tick();
        lock.tick();
        showSync();
        if (lock.getToggleState() != proc.presets.isMixLocked())
            lock.setToggleState (proc.presets.isMixLocked(), juce::dontSendNotification);
        if (mix.isMouseOverOrDragging (true) || lastMix != proc.value (ids::mix))
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
    juce::Image sigil, grain;
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
    g.fillAll (pal::night);
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
        licenseOverlay->fit (getLocalBounds(), scale, { 1108 - width, 15, width, 30 });
    }
    if (sizeRestored) onirique::window::remember (proc.apvts.state, getWidth(), getHeight());   // taille gardée avec le projet
}

void OniriqueReverbEditor::refreshDisplays (double seconds)
{
    if (content != nullptr)
        content->refresh (seconds);
}
