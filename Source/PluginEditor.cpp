#include "PluginEditor.h"

// =============================================================================
//  OndesLookAndFeel
// =============================================================================
OndesLookAndFeel::OndesLookAndFeel()
{
    using C = OndePalette;

    // Слайдеры
    setColour (juce::Slider::rotarySliderFillColourId,    C::accent());
    setColour (juce::Slider::rotarySliderOutlineColourId, C::border());
    setColour (juce::Slider::thumbColourId,               C::accentHi());
    setColour (juce::Slider::backgroundColourId,          C::panel());
    setColour (juce::Slider::trackColourId,               C::accent());
    setColour (juce::Slider::textBoxTextColourId,         C::textBri());
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colour (0xff111111));
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);

    // Лейблы
    setColour (juce::Label::textColourId, C::textBri());
}

void OndesLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                          int x, int y, int w, int h,
                                          float sliderPos,
                                          float startAngle, float endAngle,
                                          juce::Slider&)
{
    using namespace juce;
    const float cx     = x + w * 0.5f;
    const float cy     = y + h * 0.5f;
    const float radius = jmin (w, h) * 0.38f;
    const float angle  = startAngle + sliderPos * (endAngle - startAngle);

    // ── Подложка ──────────────────────────────────────────────────────────────
    g.setColour (OndePalette::panel());
    g.fillEllipse (cx - radius - 4, cy - radius - 4, (radius + 4) * 2, (radius + 4) * 2);

    // ── Треклайн (пустой) ─────────────────────────────────────────────────────
    Path trackPath;
    trackPath.addCentredArc (cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (OndePalette::border());
    g.strokePath (trackPath, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

    // ── Треклайн (заполнение) ─────────────────────────────────────────────────
    if (sliderPos > 0.0f)
    {
        Path valuePath;
        valuePath.addCentredArc (cx, cy, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour (OndePalette::accent());
        g.strokePath (valuePath, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // ── Указатель ─────────────────────────────────────────────────────────────
    Path ptr;
    ptr.addRectangle (-1.5f, -radius * 0.85f, 3.0f, radius * 0.45f);
    ptr.applyTransform (AffineTransform::rotation (angle).translated (cx, cy));
    g.setColour (OndePalette::accentHi());
    g.fillPath (ptr);

    // ── Центральная точка ─────────────────────────────────────────────────────
    g.setColour (juce::Colour (0xff111111));
    g.fillEllipse (cx - 4.5f, cy - 4.5f, 9.0f, 9.0f);
    g.setColour (OndePalette::border());
    g.drawEllipse (cx - 4.5f, cy - 4.5f, 9.0f, 9.0f, 1.0f);
}

void OndesLookAndFeel::drawToggleButton (juce::Graphics& g,
                                          juce::ToggleButton& btn,
                                          bool /*highlighted*/,
                                          bool /*down*/)
{
    const bool on     = btn.getToggleState();
    auto bounds = btn.getLocalBounds().toFloat().reduced (2.0f);

    // Фон
    g.setColour (on ? OndePalette::accent() : OndePalette::panel());
    g.fillRoundedRectangle (bounds, 10.0f);

    // Рамка
    g.setColour (on ? OndePalette::accentHi() : OndePalette::border());
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.5f);

    // Текст
    g.setFont (juce::Font (juce::FontOptions().withHeight (13.0f)
                                               .withStyle ("Bold")));
    g.setColour (on ? juce::Colour (0xff111111) : OndePalette::textBri());
    g.drawText (btn.getButtonText(), bounds, juce::Justification::centred);
}

juce::Label* OndesLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (s);
    label->setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
    return label;
}

// =============================================================================
//  AmplitudeMeter
// =============================================================================
AmplitudeMeter::AmplitudeMeter (OndesProcessor& p) : proc (p)
{
    startTimerHz (30);
}

void AmplitudeMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    const float amp  = proc.displayAmplitude.load();
    const float mod  = proc.displayModWheel .load();

    // Подложка
    g.setColour (OndePalette::panel());
    g.fillRoundedRectangle (b, 5.0f);

    // Слой: позиция колеса (слегка светлее, как «тень» амплитуды)
    g.setColour (OndePalette::accent().withAlpha (0.25f));
    g.fillRoundedRectangle (b.withWidth (b.getWidth() * mod), 5.0f);

    // Слой: реальная амплитуда звука
    g.setColour (OndePalette::accent());
    g.fillRoundedRectangle (b.withWidth (b.getWidth() * amp), 5.0f);

    // Рамка
    g.setColour (OndePalette::border());
    g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);

    // Текст
    g.setColour (OndePalette::textDim());
    g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
    g.drawText ("OUTPUT", b, juce::Justification::centred);
}

// =============================================================================
//  OndesEditor
// =============================================================================
OndesEditor::OndesEditor (OndesProcessor& p)
    : AudioProcessorEditor (&p), proc (p), meter (p)
{
    setLookAndFeel (&laf);
    setSize (460, 300);

    // HOLD кнопка
    addAndMakeVisible (holdBtn);
    holdAttach = std::make_unique<BtnAttach> (p.apvts, "holdMode", holdBtn);

    // Ноббы
    setupKnob (ccMinSlider, ccMinLabel, "CC MIN",  0,   120,  1,    "ccMin");
    setupKnob (ccMaxSlider, ccMaxLabel, "CC MAX",  7,   127,  1,    "ccMax");
    setupKnob (glideSlider, glideLabel, "GLIDE",   0.0, 3.0,  0.01, "glideMax");
    setupKnob (volumeSlider, volumeLabel, "VOLUME", 0.0, 1.0,  0.001,"volume");

    // Метр
    addAndMakeVisible (meter);
}

OndesEditor::~OndesEditor()
{
    setLookAndFeel (nullptr);
}

void OndesEditor::setupKnob (juce::Slider& s, juce::Label& l,
                               const juce::String& text,
                               double lo, double hi, double step,
                               const juce::String& paramId)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
    s.setRange (lo, hi, step);
    addAndMakeVisible (s);

    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::Font (juce::FontOptions().withHeight (10.5f)));
    l.setColour (juce::Label::textColourId, OndePalette::textDim());
    addAndMakeVisible (l);

    // Привязываем к APVTS
    if      (paramId == "ccMin")    ccMinAttach  = std::make_unique<SldAttach> (proc.apvts, paramId, s);
    else if (paramId == "ccMax")    ccMaxAttach  = std::make_unique<SldAttach> (proc.apvts, paramId, s);
    else if (paramId == "glideMax") glideAttach  = std::make_unique<SldAttach> (proc.apvts, paramId, s);
    else if (paramId == "volume")   volAttach    = std::make_unique<SldAttach> (proc.apvts, paramId, s);
}

// ─── Paint ────────────────────────────────────────────────────────────────────
void OndesEditor::paint (juce::Graphics& g)
{
    // Фон
    g.fillAll (OndePalette::bg());

    // Шапка
    {
        juce::Rectangle<float> header (0.0f, 0.0f, (float)getWidth(), 56.0f);
        g.setColour (OndePalette::panel());
        g.fillRect (header);
        g.setColour (OndePalette::border());
        g.drawHorizontalLine (56, 0, (float)getWidth());

        // Название
        g.setFont (juce::Font (juce::FontOptions()
                        .withName ("Georgia")
                        .withHeight (22.0f)
                        .withStyle ("Italic")));
        g.setColour (OndePalette::accent());
        g.drawText ("Ondes Martinot", 20, 10, 240, 28, juce::Justification::centredLeft);

        // Подзаголовок
        g.setFont (juce::Font (juce::FontOptions().withHeight (10.0f)));
        g.setColour (OndePalette::textDim());
        g.drawText ("mod wheel  |  monophonic synthesizer", 20, 34, 280, 16,
                    juce::Justification::centredLeft);

        // Версия
        g.drawText ("v0.1", getWidth() - 40, 38, 32, 12,
                    juce::Justification::centredRight);
    }

    // Панель элементов управления
    {
        juce::Rectangle<float> panel (10.0f, 66.0f, getWidth() - 20.0f, 158.0f);
        g.setColour (OndePalette::panel());
        g.fillRoundedRectangle (panel, 6.0f);
        g.setColour (OndePalette::border());
        g.drawRoundedRectangle (panel.reduced (0.5f), 6.0f, 1.0f);
    }

    // Подписи секций
    g.setFont (juce::Font (juce::FontOptions().withHeight (9.0f)));
    g.setColour (OndePalette::textDim());
    g.drawText ("WHEEL RANGE", 130, 68, 160, 12, juce::Justification::centredLeft);
}

// ─── Layout ───────────────────────────────────────────────────────────────────
void OndesEditor::resized()
{
    const int W = getWidth();

    // HOLD — крупная кнопка слева
    holdBtn.setBounds (20, 80, 92, 52);

    // Ноббы — правее кнопки
    const int knobW   = 76;
    const int labelH  = 14;
    const int knobH   = 72;
    const int knobTop = 72;
    const int gap     = 4;

    int x = 124;

    auto placeKnob = [&] (juce::Slider& s, juce::Label& l)
    {
        l.setBounds (x, knobTop, knobW, labelH);
        s.setBounds (x, knobTop + labelH, knobW, knobH);
        x += knobW + gap;
    };

    placeKnob (ccMinSlider, ccMinLabel);
    placeKnob (ccMaxSlider, ccMaxLabel);

    // Небольшой разделитель перед Glide/Volume
    x += 6;
    placeKnob (glideSlider, glideLabel);
    placeKnob (volumeSlider, volumeLabel);

    // Метр — в нижней части
    meter.setBounds (20, 240, W - 40, 22);
}
