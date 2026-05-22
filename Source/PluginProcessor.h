#pragma once
#include <JuceHeader.h>

// =============================================================================
//  OndesProcessor
//
//  Цифровой синтезатор по мотивам Волн Мартено:
//    • Колесо модуляции (CC1) напрямую управляет амплитудой — атака/рилиз
//      определяются тем, КАК вы двигаете колесо, а не параметрами ADSR.
//    • Hold ON  — нота живёт после отпускания клавиши; гасится колесом вниз.
//    • Hold OFF — «смычковый» режим: нота звучит только пока клавиша зажата
//                 И колесо поднято.
//    • Монофонический глайд; скорость глайда = velocity следующей ноты.
//    • Aftertouch → лёгкий детюн (вибрато ~5.5 Гц).
// =============================================================================
class OndesProcessor : public juce::AudioProcessor
{
public:
    OndesProcessor();
    ~OndesProcessor() override;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // ── Editor ────────────────────────────────────────────────────────────────
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // ── Info ──────────────────────────────────────────────────────────────────
    const juce::String getName() const override { return "Ondes Martinot"; }
    bool   acceptsMidi()    const override { return true;  }
    bool   producesMidi()   const override { return false; }
    bool   isMidiEffect()   const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    // ── Programs ──────────────────────────────────────────────────────────────
    int  getNumPrograms()   override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    // ── State ─────────────────────────────────────────────────────────────────
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ── Parameters (APVTS) ────────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState apvts;

    // ── Thread-safe display values (читает UI) ────────────────────────────────
    std::atomic<float> displayAmplitude { 0.0f };  // 0..1, текущая громкость
    std::atomic<float> displayModWheel  { 0.0f };  // 0..1, позиция колеса

private:
    // ── Parameter layout ─────────────────────────────────────────────────────
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ── Synth voice state (только аудио-поток) ────────────────────────────────
    int  currentNote  = -1;     // текущая MIDI-нота (-1 = нет)
    bool keyIsHeld    = false;  // клавиша физически зажата
    bool noteIsArmed  = false;  // нота «заряжена» (звуком управляет колесо)

    // Глайд (скользящая частота в пространстве полутонов)
    double currentSemitone = 69.0;  // A4
    double targetSemitone  = 69.0;
    double glideCoeff      = 1.0;   // 1.0 = мгновенно

    // Амплитуда со сглаживанием
    double ampSmoothed     = 0.0;
    double ampSmoothCoeff  = 0.99;  // вычисляется в prepareToPlay (~2 мс)

    // Осциллятор
    double oscPhase        = 0.0;

    // Вибрато (LFO, питается от aftertouch)
    double vibratoPhase    = 0.0;
    static constexpr double VIBRATO_RATE_HZ  = 5.5;
    static constexpr double VIBRATO_MAX_CENTS = 25.0;

    // MIDI-состояние (сырые CC-значения, 0–127)
    float modWheelCC   = 0.0f;
    float aftertouchCC = 0.0f;

    double sampleRate_ = 44100.0;

    // ── Вспомогательные функции ───────────────────────────────────────────────
    void   processMidiEvent (const juce::MidiMessage& msg,
                             bool holdMode, float ccMin, float ccMax,
                             float maxGlide);
    double getTargetAmplitude (bool holdMode, float ccMin, float ccMax) const;

    static double midiNoteToHz (double note) noexcept
    {
        return 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OndesProcessor)
};
