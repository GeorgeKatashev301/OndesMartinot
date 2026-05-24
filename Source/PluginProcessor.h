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

    // ── Ламповый преамп (12AX7) ─────────────────────────────────────────────────
    //
    // Модель: ADAA-tanh с асимметричным смещением рабочей точки.
    //
    // Три механизма ламповой теплоты:
    //  1. Gain-dependent saturation — при малом сигнале линейный проход,
    //     при большом — мягкое ограничение (естественно из tanh).
    //  2. Asymmetric bias (статика) — смещение +bias → доминирует 2-я гармоника,
    //     как у реального анода 12AX7. В отличие от транзистора (нечётные),
    //     это и есть «ламповая теплота».
    //  3. Dynamic bias shift (ток сетки) — biasState = LPF(in, τ≈150мс).
    //     Атака нарастает быстрее, чем LPF успевает отследить → рабочая точка
    //     временно смещается сильнее → больше 2-й гармоники на транзиентах.
    //     Потом медленно возвращается. Это создаёт «живой» отклик на динамику.
    //
    // ADAA (Antiderivative Anti-Aliasing):
    //   F₁(x) = ln(cosh(x)) — первообразная tanh.
    //   y[n] = (F₁(x[n]) − F₁(x[n−1])) / (x[n] − x[n−1])
    //   Подавляет алиасинг без дорогого оверсэмплинга.
    static double adaaTanhF1 (double x) noexcept;                         // ln(cosh(x)), стабильная форма
    static double adaaTanh   (double x, double xPrev) noexcept;           // ADAA 1st-order
    static double tubeSaturate12AX7 (double in,
                                     double& xPrev,
                                     double& biasState,
                                     double tubeBiasCoeffArg,
                                     double warmth) noexcept;

    double tubeXPrev     = 0.0;  // предыдущий вход ADAA (x[n-1])
    double tubeBias      = 0.0;  // динамическое смещение рабочей точки
    double tubeBiasCoeff = 0.999; // τ≈150мс, пересчитывается в prepareToPlay

    // ── Полная модель Джайлса-Атертона (магнитный гистерез��с) ─────────────────
    //
    // Уравнение J-A (Chowdhury, DAFx-19 / Stanford CCRMA):
    //   dM/dH = [(1-c)·δM·(Man-M)] / [(1-c)·δ·k - α·(Man-M)]  +  c·dMan/dH
    //
    //   Man = Ms · L((H + α·M)/a)   — равновесная намагниченность
    //   L(x) = coth(x) - 1/x        — функция Ланжевена (не tanh!)
    //   δ   = sign(dH/dt)           — направление обхода петли
    //   δM  = 1 если (Man-M)·δ > 0  — физическое ограничение
    //
    // Интегрируется методом Эйлера при 2× оверсэмплинге (midpoint interpolation).
    // После — loss model (head bump + HF rolloff): тембр воспроизводящей головки.
    //
    // Параметры подобраны под ферроксидную (Type I) кассетную ленту.

    static double langevin      (double x) noexcept;           // coth(x) - 1/x
    static double langevinDeriv (double x) noexcept;           // L'(x)
    static double jA_dMdH       (double M, double H, double dH,
                                  double Ms, double a, double alpha,
                                  double k,  double c) noexcept;

    // Biquad фильтр (transposed df2t) — для loss model
    static double processBiquad (double x,
                                  double b0, double b1, double b2,
                                  double a1, double a2,
                                  double& z1, double& z2) noexcept;

    void computeLossFilters (double sr);  // вычисляет коэффициенты в prepareToPlay

    // Физические параметры ленты (нормализованы, Ms = 1.0):
    static constexpr double JA_Ms    = 1.0;     // намагниченность насыщения
    static constexpr double JA_a     = 0.22;    // форма кривой Ланжевена
    static constexpr double JA_alpha = 0.0016;  // параметр среднего поля
    static constexpr double JA_k     = 0.22;    // ширина петли (коэрцитивность)
    static constexpr double JA_c     = 0.14;    // доля обратимой намагниченности

    // J-A состояние
    double jaM      = 0.0;  // текущая на��агниченность M
    double jaHPrev  = 0.0;  // H на предыдущем шаге (для вычисления dH)
    double jaInPrev = 0.0;  // вход на предыдущем шаге (2× OS интерполяция)

    // Loss model: два biquad (transposed df2t)
    // Head bump: пик ~150 Гц, +3.5 дБ — резонанс воспроизводящей головки
    double hbB0=1.0, hbB1=0.0, hbB2=0.0, hbA1=0.0, hbA2=0.0;
    double hbZ1=0.0, hbZ2=0.0;
    // HF rolloff: LP ~10 кГц, Butterworth — потери высоких частот
    double lfB0=1.0, lfB1=0.0, lfB2=0.0, lfA1=0.0, lfA2=0.0;
    double lfZ1=0.0, lfZ2=0.0;

    // Вау и флаттер: два медленных LFO модулируют высоту тона.
    // Бейкд-ин — без параметра, характер инструмента.
    double wowPhase     = 0.0;  // ~0.8 Гц, ±0.15% pitch
    double flutterPhase = 0.0;  // ~8 Гц,   ±0.05% pitch

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
