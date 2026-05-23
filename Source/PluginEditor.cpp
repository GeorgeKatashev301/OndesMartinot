#include "PluginEditor.h"

// =============================================================================
//  LookAndFeel
// =============================================================================
OndesLookAndFeel::OndesLookAndFeel()
{
    namespace C = OndePalette;
    setColour (juce::Slider::rotarySliderFillColourId,    C::accent());
    setColour (juce::Slider::rotarySliderOutlineColourId, C::border());
    setColour (juce::Slider::thumbColourId,               C::accentHi());
    setColour (juce::Slider::backgroundColourId,          C::panel());
    setColour (juce::Slider::trackColourId,               C::accent());
    setColour (juce::Slider::textBoxTextColourId,         C::textBri());
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colour (0xff111111));
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId,                 C::textBri());
}

void OndesLookAndFeel::drawRotarySlider (juce::Graphics& g,
    int x, int y, int w, int h,
    float sliderPos, float startAngle, float endAngle, juce::Slider&)
{
    using namespace juce;
    const float cx     = static_cast<float>(x) + static_cast<float>(w) * 0.5f;
    const float cy     = static_cast<float>(y) + static_cast<float>(h) * 0.5f;
    const float radius = jmin (static_cast<float>(w), static_cast<float>(h)) * 0.38f;
    const float angle  = startAngle + sliderPos * (endAngle - startAngle);

    g.setColour (OndePalette::panel());
    g.fillEllipse (cx-radius-4, cy-radius-4, (radius+4)*2, (radius+4)*2);

    Path track; track.addCentredArc (cx, cy, radius, radius, 0, startAngle, endAngle, true);
    g.setColour (OndePalette::border());
    g.strokePath (track, PathStrokeType (3, PathStrokeType::curved, PathStrokeType::rounded));

    if (sliderPos > 0)
    {
        Path val; val.addCentredArc (cx, cy, radius, radius, 0, startAngle, angle, true);
        g.setColour (OndePalette::accent());
        g.strokePath (val, PathStrokeType (3, PathStrokeType::curved, PathStrokeType::rounded));
    }

    Path ptr;
    ptr.addRectangle (-1.5f, -radius*0.85f, 3.0f, radius*0.45f);
    ptr.applyTransform (AffineTransform::rotation (angle).translated (cx, cy));
    g.setColour (OndePalette::accentHi());
    g.fillPath (ptr);

    g.setColour (juce::Colour (0xff111111));
    g.fillEllipse (cx-4.5f, cy-4.5f, 9, 9);
    g.setColour (OndePalette::border());
    g.drawEllipse (cx-4.5f, cy-4.5f, 9, 9, 1);
}

void OndesLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& btn,
    bool, bool)
{
    const bool on = btn.getToggleState();
    auto b = btn.getLocalBounds().toFloat().reduced (2);
    g.setColour (on ? OndePalette::accent() : OndePalette::panel());
    g.fillRoundedRectangle (b, 8);
    g.setColour (on ? OndePalette::accentHi() : OndePalette::border());
    g.drawRoundedRectangle (b.reduced(0.5f), 8, 1.5f);
    g.setFont (juce::Font (juce::FontOptions().withHeight(13).withStyle("Bold")));
    g.setColour (on ? juce::Colour(0xff111111) : OndePalette::textBri());
    g.drawText (btn.getButtonText(), b, juce::Justification::centred);
}

juce::Label* OndesLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::Font (juce::FontOptions().withHeight(11)));
    return l;
}

// =============================================================================
//  Meter
// =============================================================================
AmplitudeMeter::AmplitudeMeter (OndesProcessor& p) : proc(p) { startTimerHz(30); }

void AmplitudeMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float amp = proc.displayAmplitude.load();
    const float src = proc.displayModWheel.load();

    g.setColour (OndePalette::panel());
    g.fillRoundedRectangle (b, 5);
    g.setColour (OndePalette::accent().withAlpha(0.22f));
    g.fillRoundedRectangle (b.withWidth (b.getWidth() * src), 5);
    g.setColour (OndePalette::accent());
    g.fillRoundedRectangle (b.withWidth (b.getWidth() * amp), 5);
    g.setColour (OndePalette::border());
    g.drawRoundedRectangle (b.reduced(0.5f), 5, 1);
    g.setColour (OndePalette::textDim());
    g.setFont (juce::Font (juce::FontOptions().withHeight(10)));
    g.drawText ("OUTPUT", b, juce::Justification::centred);
}

// =============================================================================
//  ThreeWaySwitch — физический вид тумблера: три позиции
// =============================================================================
ThreeWaySwitch::ThreeWaySwitch (OndesProcessor& p) : proc(p) { startTimerHz(20); }

int ThreeWaySwitch::getMode() const
{
    return static_cast<int> (proc.apvts.getRawParameterValue ("bowMode")->load());
}

void ThreeWaySwitch::setMode (int m)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("bowMode")))
        p->setValueNotifyingHost (p->convertTo0to1 (m));
}

void ThreeWaySwitch::paint (juce::Graphics& g)
{
    const int mode  = getMode();
    auto bounds     = getLocalBounds().toFloat();
    const float W   = bounds.getWidth();
    const float H   = bounds.getHeight();

    // Тройка секций: HOLD | BOW | INSANE
    struct Section { const char* label; juce::Colour activeCol; };
    const Section secs[3] = {
        { "HOLD",   OndePalette::accent() },
        { "BOW",    OndePalette::accent() },
        { "INSANE", OndePalette::insane() },
    };

    const float secW = W / 3.0f;

    for (int i = 0; i < 3; ++i)
    {
        const bool active = (mode == i);
        juce::Rectangle<float> sec (secW * i, 0, secW, H);

        // Фон
        g.setColour (active ? secs[i].activeCol : OndePalette::panel());
        if (i == 0)
            g.fillRoundedRectangle (sec, 8);
        else if (i == 2)
            g.fillRoundedRectangle (sec, 8);
        else
            g.fillRect (sec);

        // Рамка
        g.setColour (active ? secs[i].activeCol.brighter(0.3f) : OndePalette::border());
        g.drawRect (sec, 1.0f);

        // Метка
        g.setFont (juce::Font (juce::FontOptions().withHeight(12).withStyle("Bold")));
        g.setColour (active ? juce::Colour(0xff111111) : OndePalette::textDim());
        g.drawText (secs[i].label, sec, juce::Justification::centred);
    }

    // Общая рамка поверх
    g.setColour (OndePalette::border());
    g.drawRoundedRectangle (bounds.reduced(0.5f), 8, 1.5f);
}

void ThreeWaySwitch::mouseDown (const juce::MouseEvent& e)
{
    const int section = static_cast<int> (e.x / (getWidth() / 3.0f));
    setMode (juce::jlimit (0, 2, section));
}

// =============================================================================
//  OndesEditor
// =============================================================================
OndesEditor::OndesEditor (OndesProcessor& p)
    : AudioProcessorEditor(&p), proc(p), modeSwitch(p), meter(p)
{
    setLookAndFeel (&laf);
    setSize (460, 350);

    addAndMakeVisible (modeSwitch);

    // Ноббы
    setupKnob (ccMinSlider,   ccMinLabel,   "CC MIN",   0,    120,  1,    "ccMin");
    setupKnob (ccMaxSlider,   ccMaxLabel,   "CC MAX",   7,    127,  1,    "ccMax");
    setupKnob (glideSlider,   glideLabel,   "GLIDE",    0.0,  3.0,  0.01, "glideMax");
    setupKnob (volumeSlider,  volumeLabel,  "VOLUME",   0.0,  1.0,  0.001,"volume");
    setupKnob (bowSensSlider, bowSensLabel, "BOW SENS", 0.02, 0.8,  0.01, "bowSens");
    setupKnob (releaseSlider, releaseLabel, "RELEASE",  0.0,  3.0,  0.01, "release");

    // Звук
    setupKnob (warmthSlider, warmthLabel, "WARMTH", 0.0, 1.0, 0.01, "warmth");

    // Кнопки волны
    auto* waveParam     = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("waveform"));
    const int waveGroup = 2001;

    auto setupWaveBtn = [&] (juce::TextButton& btn, int idx)
    {
        btn.setRadioGroupId (waveGroup);
        btn.setClickingTogglesState (true);
        btn.setToggleState (waveParam && waveParam->getIndex() == idx, juce::dontSendNotification);
        btn.setColour (juce::TextButton::buttonColourId,   OndePalette::panel());
        btn.setColour (juce::TextButton::buttonOnColourId, OndePalette::accent());
        btn.setColour (juce::TextButton::textColourOffId,  OndePalette::textDim());
        btn.setColour (juce::TextButton::textColourOnId,   juce::Colour(0xff111111));
        btn.onClick = [this, idx]
        {
            if (auto* wp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("waveform")))
                wp->setValueNotifyingHost (wp->convertTo0to1 (idx));
        };
        addAndMakeVisible (btn);
    };

    setupWaveBtn (waveSine,     0);
    setupWaveBtn (waveSaw,      1);
    setupWaveBtn (waveSquare,   2);
    setupWaveBtn (waveTriangle, 3);

    waveLabel.setText ("WAVE", juce::dontSendNotification);
    waveLabel.setJustificationType (juce::Justification::centredLeft);
    waveLabel.setFont (juce::Font (juce::FontOptions().withHeight(10)));
    waveLabel.setColour (juce::Label::textColourId, OndePalette::textDim());
    addAndMakeVisible (waveLabel);

    addAndMakeVisible (meter);
}

OndesEditor::~OndesEditor() { setLookAndFeel (nullptr); }

void OndesEditor::setupKnob (juce::Slider& s, juce::Label& l,
    const juce::String& text, double lo, double hi, double step,
    const juce::String& paramId)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
    s.setRange (lo, hi, step);
    addAndMakeVisible (s);

    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::Font (juce::FontOptions().withHeight(10.5f)));
    l.setColour (juce::Label::textColourId, OndePalette::textDim());
    addAndMakeVisible (l);

    if      (paramId == "ccMin")    ccMinAttach    = std::make_unique<SldAttach> (proc.apvts, paramId, s);
    else if (paramId == "ccMax")    ccMaxAttach    = std::make_unique<SldAttach> (proc.apvts, paramId, s);
    else if (paramId == "glideMax") glideAttach    = std::make_unique<SldAttach> (proc.apvts, paramId, s);
    else if (paramId == "volume")   volAttach      = std::make_unique<SldAttach> (proc.apvts, paramId, s);
    else if (paramId == "bowSens")  bowSensAttach  = std::make_unique<SldAttach> (proc.apvts, paramId, s);
    else if (paramId == "release")  releaseAttach  = std::make_unique<SldAttach> (proc.apvts, paramId, s);
    else if (paramId == "warmth")   warmthAttach   = std::make_unique<SldAttach> (proc.apvts, paramId, s);
}

// ─── Paint ────────────────────────────────────────────────────────────────────
void OndesEditor::paint (juce::Graphics& g)
{
    const float W = static_cast<float> (getWidth());

    g.fillAll (OndePalette::bg());

    // Шапка
    g.setColour (OndePalette::panel());
    g.fillRect (0, 0, getWidth(), 54);
    g.setColour (OndePalette::border());
    g.drawHorizontalLine (54, 0, W);

    g.setFont (juce::Font (juce::FontOptions().withName("Georgia").withHeight(21).withStyle("Italic")));
    g.setColour (OndePalette::accent());
    g.drawText ("Ondes Martinot", 20, 9, 260, 26, juce::Justification::centredLeft);

    g.setFont (juce::Font (juce::FontOptions().withHeight(10)));
    g.setColour (OndePalette::textDim());
    g.drawText ("mod wheel synthesizer", 20, 33, 220, 14, juce::Justification::centredLeft);
    g.drawText ("v0.5", static_cast<int>(W)-38, 36, 30, 12, juce::Justification::centredRight);

    // Панель (расширена для второго ряда нобов)
    g.setColour (OndePalette::panel());
    g.fillRoundedRectangle (10, 62, W-20, 246, 6);
    g.setColour (OndePalette::border());
    g.drawRoundedRectangle (10, 62, W-20, 246, 6, 1);

    // Разделитель 1: между MODE/WAVE и ��ервым рядом нобов
    g.setColour (OndePalette::border());
    g.drawHorizontalLine (118, 18, W-18);

    // Разделитель 2: между рядами нобов
    g.drawHorizontalLine (218, 18, W-18);

    // Подписи секций
    g.setFont (juce::Font (juce::FontOptions().withHeight(9)));
    g.setColour (OndePalette::textDim());
    g.drawText ("MODE",                       20,  64, 60,  11, juce::Justification::centredLeft);
    g.drawText ("OSCILLATOR",                300,  64, 100, 11, juce::Justification::centredLeft);
    g.drawText ("WHEEL RANGE + EXPRESSION",   20, 120, 240, 11, juce::Justification::centredLeft);
    g.drawText ("SOUND",                      20, 220, 100, 11, juce::Justification::centredLeft);
}

// ─── Layout ───────────────────────────────────────────────────────────────────
void OndesEditor::resized()
{
    const int W = getWidth();

    // ── Ряд 1 (y=77, h=32): тумблер слева | кнопки волны справа ─────────────
    {
        // Тумблер HOLD/BOW/INSANE занимает 55% ширины строки
        modeSwitch.setBounds (18, 77, 240, 32);

        // Кнопки волны — оставшееся место
        const int waveX   = 268;
        const int waveEnd = W - 18;
        const int waveW   = waveEnd - waveX;
        const int gap     = 3;
        const int btnW    = (waveW - gap * 3) / 4;

        waveLabel.setBounds (waveX, 64, 80, 11);
        waveSine    .setBounds (waveX,                    77, btnW, 32);
        waveSaw     .setBounds (waveX + (btnW+gap),       77, btnW, 32);
        waveSquare  .setBounds (waveX + (btnW+gap)*2,     77, btnW, 32);
        waveTriangle.setBounds (waveX + (btnW+gap)*3,     77, btnW, 32);
    }

    // ── Ряд 2 (y=131, h=86): ноббы, центрированные — 6 штук ─────────────────
    {
        const int knobW  = 66;
        const int labelH = 14;
        const int knobH  = 72;
        const int knobY  = 131;
        const int gap    = 5;
        const int total  = knobW*6 + gap*5;
        int x            = (W - total) / 2;

        auto place = [&] (juce::Slider& s, juce::Label& l)
        {
            l.setBounds (x, knobY, knobW, labelH);
            s.setBounds (x, knobY + labelH, knobW, knobH);
            x += knobW + gap;
        };

        place (ccMinSlider,   ccMinLabel);
        place (ccMaxSlider,   ccMaxLabel);
        place (glideSlider,   glideLabel);
        place (volumeSlider,  volumeLabel);
        place (bowSensSlider, bowSensLabel);
        place (releaseSlider, releaseLabel);
    }

    // ── Ряд 3 (y=231, h=86): звуковые параметры ──────────────────────────────
    {
        const int knobW  = 76;
        const int labelH = 14;
        const int knobH  = 72;
        const int knobY  = 231;

        warmthLabel .setBounds ((W - knobW) / 2, knobY, knobW, labelH);
        warmthSlider.setBounds ((W - knobW) / 2, knobY + labelH, knobW, knobH);
    }

    // ── Метр ─────────────────────────────────────────────────────────────────
    meter.setBounds (20, 317, W-40, 22);
}
