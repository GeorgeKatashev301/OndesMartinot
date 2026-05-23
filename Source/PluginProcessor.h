#pragma once
#include <JuceHeader.h>

// =============================================================================
//  OndesProcessor  —  три режима управления амплитудой:
//
//  HOLD   — позиция колеса = амплитуда; нота держится после отпускания клавиши.
//  BOW    — амплитуда = скорость движения колеса (через тень-фильтр, плавно).
//           Остановил руку → тишина. Чем быстрее — тем громче.
//  INSANE — то же самое, но дельта считается по сырым MIDI-сообщениям.
//           Хаотичные всплески, нестабильность — это фича.
// =============================================================================
class OndesProcessor : public juce::AudioProcessor
{
public:
    OndesProcessor();
    ~OndesProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ondes Martinot"; }
    bool   acceptsMidi()    const override { return true;  }
    bool   producesMidi()   const override { return false; }
    bool   isMidiEffect()   const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Для UI (читается в таймере, 30 Гц)
    std::atomic<float> displayAmplitude { 0.0f };
    std::atomic<float> displayModWheel  { 0.0f };

    // Режимы (соответствуют параметру "bowMode")
    enum BowMode { Hold = 0, Bow = 1, Insane = 2 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Голос
    int  currentNote  = -1;
    bool keyIsHeld    = false;
    bool noteIsArmed  = false;

    // Глайд
    double currentSemitone = 69.0;
    double targetSemitone  = 69.0;
    double glideCoeff      = 1.0;

    // Амплитуда
    double ampSmoothed    = 0.0;
    double ampSmoothCoeff = 0.99;

    // Осциллятор
    double oscPhase    = 0.0;
    double prevRawOsc  = 0.0;   // предыдущий raw-семпл (для 2× oversampling сатурации)
    enum Waveform { Sine = 0, Sawtooth, Square, Triangle };

    // dt = freq / sampleRate — нужен для polyBLEP-коррекции разрывов
    static double generateOscSample (double phase, int waveform, double dt) noexcept;

    // PolyBLEP: сглаживает разрывы в точке t (фаза / 2π), dt = freq/sampleRate
    static double polyBlep (double t, double dt) noexcept;

    // Ламповое насыщение: асимметричный вейвшейпер, акцент на 2-й гармонике
    static double tubeSaturate (double x, double warmth) noexcept;

    // DC-блокер (убирает постоянный сдвиг от асимметричной сатурации)
    double dcX1 = 0.0, dcY1 = 0.0;
    static constexpr double DC_BLOCKER_R = 0.9999;

    // MIDI
    float modWheelCC   = 0.0f;
    float aftertouchCC = 0.0f;

    // ── BOW (плавный) ─────────────────────────────────────────────────────────
    // shadowCC = LPF(modWheelCC, τ=25мс); разница = скорость движения.
    // bowSpeedOut = дополнительный LPF на выходе (τ=15мс) — убирает CC-спайки.
    float  shadowCC         = 0.0f;
    float  bowSpeedOut      = 0.0f;  // сглаженный выходной bow speed
    double shadowCoeff      = 0.999;
    double bowOutCoeff      = 0.999; // τ≈15мс, считается в prepareToPlay
    static constexpr float BOW_SMOOTH_SENSITIVITY = 0.25f;

    // Счётчик "тишины" по CC: сколько сэмплов прошло с последнего CC-сообщения.
    // Если > порога (≈100мс) → рука только что коснулась колеса → снапим тень.
    int ccSilenceCounter   = 0;
    int ccSilenceThreshold = 4410; // пересчитывается в prepareToPlay

    // ── INSANE (сырые дельты) ─────────────────────────────────────────────────
    // Фикс: дельта = |newCC - modWheelCC| (modWheelCC = предыдущее значение).
    // prevModWheelCC убран — modWheelCC сам является «предыдущим» до обновления.
    float  insaneBowSpeed   = 0.0f;
    double insaneDecayCoeff = 0.999;
    static constexpr float INSANE_SENSITIVITY = 0.5f; // повышена: delta=2 → 100%

    // Aftertouch → прямой детюн
    static constexpr double AFTERTOUCH_MAX_CENTS = 40.0;

    double sampleRate_ = 44100.0;

    void processMidiEvent (const juce::MidiMessage& msg,
                           int bowMode, float ccMin, float ccMax, float maxGlide);

    static double midiNoteToHz (double note) noexcept
    {
        return 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OndesProcessor)
};
