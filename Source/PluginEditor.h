#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace OndePalette
{
    inline juce::Colour bg()       { return juce::Colour (0xff171717); }
    inline juce::Colour panel()    { return juce::Colour (0xff1f1f1f); }
    inline juce::Colour accent()   { return juce::Colour (0xffE8943A); }
    inline juce::Colour accentHi() { return juce::Colour (0xffF5B060); }
    inline juce::Colour insane()   { return juce::Colour (0xffC0392B); } // красный для Insane
    inline juce::Colour border()   { return juce::Colour (0xff333333); }
    inline juce::Colour textDim()  { return juce::Colour (0xff888888); }
    inline juce::Colour textBri()  { return juce::Colour (0xffCCCCCC); }
}

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
//  Трёхпозиционный тумблер (HOLD / BOW / INSANE)
// =============================================================================
class ThreeWaySwitch : public juce::Component, private juce::Timer
{
public:
    ThreeWaySwitch (OndesProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    OndesProcessor& proc;
    int getMode() const;
    void setMode (int m);
};

// =============================================================================
class OndesEditor : public juce::AudioProcessorEditor
{
public:
    explicit OndesEditor (OndesProcessor&);
    ~OndesEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    OndesProcessor&  proc;
    OndesLookAndFeel laf;

    ThreeWaySwitch modeSwitch;

    // Expression controls
    juce::Slider ccMinSlider, ccMaxSlider, glideSlider, volumeSlider, bowSensSlider, releaseSlider;
    juce::Label  ccMinLabel,  ccMaxLabel,  glideLabel,  volumeLabel,  bowSensLabel,  releaseLabel;

    // Sound controls
    juce::Slider warmthSlider;
    juce::Label  warmthLabel;

    juce::TextButton waveSine { "SIN" }, waveSaw { "SAW" },
                     waveSquare { "SQR" }, waveTriangle { "TRI" };
    juce::Label waveLabel;

    AmplitudeMeter meter;

    using SldAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SldAttach> ccMinAttach, ccMaxAttach, glideAttach, volAttach,
                               bowSensAttach, releaseAttach, warmthAttach;

    void setupKnob (juce::Slider& s, juce::Label& l, const juce::String& text,
                    double lo, double hi, double step, const juce::String& paramId);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OndesEditor)
};
