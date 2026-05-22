#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// =============================================================================
//  Цветовая палитра (янтарно-тёплая, под Волны Мартено)
// =============================================================================
namespace OndePalette
{
    inline juce::Colour bg()       { return juce::Colour (0xff171717); }
    inline juce::Colour panel()    { return juce::Colour (0xff1f1f1f); }
    inline juce::Colour accent()   { return juce::Colour (0xffE8943A); }
    inline juce::Colour accentHi() { return juce::Colour (0xffF5B060); }
    inline juce::Colour border()   { return juce::Colour (0xff333333); }
    inline juce::Colour textDim()  { return juce::Colour (0xff888888); }
    inline juce::Colour textBri()  { return juce::Colour (0xffCCCCCC); }
}

// =============================================================================
//  Кастомный LookAndFeel
// =============================================================================
class OndesLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OndesLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool highlighted, bool down) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
};

// =============================================================================
//  VU-метр (обновляется 30 Гц)
// =============================================================================
class AmplitudeMeter : public juce::Component, private juce::Timer
{
public:
    explicit AmplitudeMeter (OndesProcessor& p);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }
    OndesProcessor& proc;
};

// =============================================================================
//  Главный редактор
// =============================================================================
class OndesEditor : public juce::AudioProcessorEditor
{
public:
    explicit OndesEditor (OndesProcessor&);
    ~OndesEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    OndesProcessor&    proc;
    OndesLookAndFeel   laf;

    // ── Виджеты ───────────────────────────────────────────────────────────────
    juce::ToggleButton holdBtn { "HOLD" };

    juce::Slider ccMinSlider, ccMaxSlider, glideSlider, volumeSlider;
    juce::Label  ccMinLabel, ccMaxLabel, glideLabel, volumeLabel;

    AmplitudeMeter meter;

    // ── APVTS-привязки ────────────────────────────────────────────────────────
    using BtnAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SldAttach = juce::AudioProcessorValueTreeState::SliderAttachment;

    std::unique_ptr<BtnAttach> holdAttach;
    std::unique_ptr<SldAttach> ccMinAttach, ccMaxAttach, glideAttach, volAttach;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void setupKnob (juce::Slider& s, juce::Label& l, const juce::String& text,
                    double min, double max, double step,
                    const juce::String& paramId);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OndesEditor)
};
