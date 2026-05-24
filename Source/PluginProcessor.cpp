#include "PluginProcessor.h"
#include "PluginEditor.h"

OndesProcessor::OndesProcessor()
    : AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{}

OndesProcessor::~OndesProcessor() {}

// ─── Parameters ───────────────────────────────────────────────────────────────
juce::AudioProcessorValueTreeState::ParameterLayout OndesProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Режим управления амплитудой
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "bowMode", "Mode",
        juce::StringArray { "Hold", "Bow", "Insane" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "ccMin", "CC Min",
        juce::NormalisableRange<float> (0.0f, 120.0f, 1.0f), 20.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "ccMax", "CC Max",
        juce::NormalisableRange<float> (7.0f, 127.0f, 1.0f), 120.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "glideMax", "Max Glide",
        juce::NormalisableRange<float> (0.0f, 3.0f, 0.01f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "volume", "Volume",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "waveform", "Waveform",
        juce::StringArray { "Sine", "Sawtooth", "Square", "Triangle" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "bowSens", "Bow Sens",
        juce::NormalisableRange<float> (0.02f, 0.8f, 0.01f), 0.12f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "release", "Release",
        juce::NormalisableRange<float> (0.0f, 3.0f, 0.01f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "warmth", "Warmth",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.3f));

    return { params.begin(), params.end() };
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────
void OndesProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRate_ = sampleRate;

    // Амплитуда: сглаживание ~8 мс (при 2 мс 127 шагов CC давали слышимые ступени)
    ampSmoothCoeff = std::exp (-1.0 / (0.008 * sampleRate));

    // Тень CC: τ=25 мс — за ней следует «скорость» движения колеса
    shadowCoeff = std::exp (-1.0 / (0.025 * sampleRate));

    // Выходной LPF для bow speed: τ=15 мс — убирает CC-спайки
    bowOutCoeff = std::exp (-1.0 / (0.015 * sampleRate));

    // Insane: затухание ~50 мс (полужизнь)
    insaneDecayCoeff = std::exp (-std::log (2.0) / (0.05 * sampleRate));

    // ≈100мс "тишины" для детекции первого касания колеса
    ccSilenceThreshold = static_cast<int> (sampleRate * 0.1);
    ccSilenceCounter   = ccSilenceThreshold + 1; // стартуем как будто давно тишина

    // Ламповый преамп: динамический биас τ≈150мс (ток сетки 12AX7)
    tubeBiasCoeff = std::exp (-1.0 / (0.15 * sampleRate));


    // Сброс
    currentNote    = -1;    keyIsHeld   = false;  noteIsArmed = false;
    ampSmoothed    = 0.0;   oscPhase    = 0.0;    prevRawOsc  = 0.0;
    modWheelCC     = 0.0f;  aftertouchCC   = 0.0f;
    shadowCC       = 0.0f;  bowSpeedOut    = 0.0f;  insaneBowSpeed = 0.0f;
    currentSemitone = 69.0; targetSemitone = 69.0;  glideCoeff = 1.0;
    dcX1 = 0.0; dcY1 = 0.0;
    tubeXPrev = 0.0; tubeBias = 0.0;
    computeLossFilters (sampleRate);
    jaM = 0.0; jaHPrev = 0.0; jaInPrev = 0.0;
    hbZ1 = 0.0; hbZ2 = 0.0; lfZ1 = 0.0; lfZ2 = 0.0;
    wowPhase   = 0.0;   flutterPhase = 0.0;
}

void OndesProcessor::releaseResources() {}

// ─── MIDI ─────────────────────────────────────────────────────────────────────
void OndesProcessor::processMidiEvent (const juce::MidiMessage& msg,
                                        int bowMode,
                                        float ccMin, float ccMax,
                                        float maxGlide)
{
    juce::ignoreUnused (ccMin, ccMax);

    if (msg.isNoteOn())
    {
        const int   newNote  = msg.getNoteNumber();
        const float velocity = msg.getFloatVelocity();

        targetSemitone = static_cast<double> (newNote);

        if (!noteIsArmed || currentNote == -1)
        {
            currentSemitone = targetSemitone;
            glideCoeff      = 1.0;
        }
        else
        {
            const float t = maxGlide * (1.0f - velocity);
            glideCoeff = (t < 0.001f) ? 1.0
                                      : 1.0 - std::exp (-1.0 / (t * sampleRate_));
        }

        if (ampSmoothed < 0.001)
        {
            // «Холодный» старт: сбрасываем все состояния сатурации и фильтров,
            // чтобы не было артефактов из предыдущей ноты.
            oscPhase   = 0.0;
            tubeXPrev  = 0.0;  tubeBias   = 0.0;
            jaM = 0.0; jaHPrev = 0.0; jaInPrev = 0.0;
            hbZ1 = 0.0; hbZ2 = 0.0; lfZ1 = 0.0; lfZ2 = 0.0;
            dcX1       = 0.0;  dcY1       = 0.0;
        }

        // В режиме смычка сбрасываем тень и фильтр — без стартового всплеска
        if (bowMode != Hold)
        {
            shadowCC    = modWheelCC;
            bowSpeedOut = 0.0f;
        }

        currentNote    = newNote;
        keyIsHeld      = true;
        noteIsArmed    = true;
        aftertouchCC   = 0.0f;
    }
    else if (msg.isNoteOff())
    {
        if (msg.getNoteNumber() == currentNote)
        {
            keyIsHeld = false;
            // Hold:        нота живёт до опускания колеса ниже ccMin.
            // Bow/Insane:  нота живёт до конца release-хвоста (снимается в processBlock).
        }
    }
    else if (msg.isController() && msg.getControllerNumber() == 1)
    {
        const float newCC = static_cast<float> (msg.getControllerValue());

        if (bowMode == Insane)
        {
            // Дельта = разница между новым значением и ТЕКУЩИМ modWheelCC.
            // modWheelCC здесь ещё не обновлён → это и есть «предыдущее» значение.
            const float delta = std::abs (newCC - modWheelCC);
            insaneBowSpeed = std::min (1.0f, delta * INSANE_SENSITIVITY);
        }

        // BOW: если CC не двигался >100мс — рука только что коснулась колеса.
        // Снапаем тень, чтобы не было спайка от скачка позиции.
        // После снапа счётчик сбрасывается → следующие CC идут без снапа → нормальный BOW.
        if (bowMode == Bow && ccSilenceCounter > ccSilenceThreshold)
            shadowCC = newCC;

        ccSilenceCounter = 0; // сбрасываем на любом CC

        modWheelCC = newCC;

        // Hold: если колесо упало ниже ccMin после отпускания — снять ноту
        if (bowMode == Hold && !keyIsHeld && modWheelCC <= ccMin)
            noteIsArmed = false;
    }
    else if (msg.isAftertouch() && msg.getNoteNumber() == currentNote)
        aftertouchCC = static_cast<float> (msg.getAfterTouchValue());
    else if (msg.isChannelPressure())
        aftertouchCC = static_cast<float> (msg.getChannelPressureValue());
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
    {
        currentNote = -1;  keyIsHeld = false;  noteIsArmed = false;
        modWheelCC = 0.0f; insaneBowSpeed = 0.0f; aftertouchCC = 0.0f;
    }
}

// ─── Process Block ────────────────────────────────────────────────────────────
void OndesProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int   bowMode  = static_cast<int> (apvts.getRawParameterValue ("bowMode")->load());
    const float ccMin    = apvts.getRawParameterValue ("ccMin")->load();
    const float ccMax    = apvts.getRawParameterValue ("ccMax")->load();
    const float maxGlide = apvts.getRawParameterValue ("glideMax")->load();
    const float volume   = apvts.getRawParameterValue ("volume")->load();
    const int   waveform = static_cast<int> (apvts.getRawParameterValue ("waveform")->load());
    const float bowSens     = apvts.getRawParameterValue ("bowSens")->load();
    const float releaseTime = apvts.getRawParameterValue ("release")->load();
    const float warmth      = apvts.getRawParameterValue ("warmth")->load();

    // Release-коэффициент для Bow/Insane: считается раз на блок, не per-sample
    const double relCoeff = (releaseTime > 0.005f)
        ? std::exp (-1.0 / (static_cast<double> (releaseTime) * sampleRate_))
        : ampSmoothCoeff;   // 0 с = мгновенно (≈ 2 мс)

    const int numSamples = buffer.getNumSamples();
    float* leftCh  = buffer.getWritePointer (0);
    float* rightCh = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    auto midiIt = midiMessages.begin();

    for (int i = 0; i < numSamples; ++i)
    {
        // ── MIDI ─────────────────────────────────────────────────────────────
        while (midiIt != midiMessages.end())
        {
            auto meta = *midiIt;
            if (meta.samplePosition > i) break;
            processMidiEvent (meta.getMessage(), bowMode, ccMin, ccMax, maxGlide);
            ++midiIt;
        }

        // ── Счётчик тишины по CC (для детекции первого касания) ─────────────
        if (ccSilenceCounter <= ccSilenceThreshold)
            ++ccSilenceCounter;

        // ── Обновление тени CC (каждый сэмпл, для плавного BOW) ─────────────
        // shadowCC — LPF(modWheelCC, τ=25мс). Разница = скорость движения руки.
        shadowCC = static_cast<float> (
            static_cast<double> (shadowCC) * shadowCoeff
            + static_cast<double> (modWheelCC) * (1.0 - shadowCoeff));

        // ── Затухание insaneBowSpeed (каждый сэмпл) ───────────────────────────
        insaneBowSpeed = static_cast<float> (
            static_cast<double> (insaneBowSpeed) * insaneDecayCoeff);

        // ── Целевая амплитуда ─────────────────────────────────────────────────
        double targetAmp = 0.0;
        if (noteIsArmed)
        {
            switch (bowMode)
            {
                case Hold:
                {
                    // Позиция колеса → амплитуда
                    const float norm = juce::jlimit (0.0f, 1.0f,
                                                     (modWheelCC - ccMin) / (ccMax - ccMin));
                    targetAmp = static_cast<double> (norm);
                    break;
                }
                case Bow:
                {

                    const float rawSpeed = juce::jmin (1.0f,
                        std::abs (modWheelCC - shadowCC) * bowSens);
                    bowSpeedOut = static_cast<float> (
                        static_cast<double> (bowSpeedOut) * bowOutCoeff
                        + static_cast<double> (rawSpeed) * (1.0 - bowOutCoeff));
                    targetAmp = keyIsHeld ? static_cast<double> (bowSpeedOut) : 0.0;
                    break;
                }
                case Insane:
                {
                    // Сырые дельты → хаос → это и есть Insane
                    targetAmp = keyIsHeld ? static_cast<double> (insaneBowSpeed) : 0.0;
                    break;
                }
                default: break;
            }
        }

        // ── Сглаживание ───────────────────────────────────────────────────────
        // В стадии release (Bow/Insane, клавиша отпущена) — медленный коэффициент.
        const bool inRelease = !keyIsHeld && noteIsArmed && (bowMode == Bow || bowMode == Insane);
        const double useCoeff = inRelease ? relCoeff : ampSmoothCoeff;
        ampSmoothed = ampSmoothed * useCoeff + targetAmp * (1.0 - useCoeff);

        // Конец release: снимаем ноту когда хвост затих
        if (inRelease && ampSmoothed < 1e-5)
            noteIsArmed = false;

        if (ampSmoothed < 1e-5 && !noteIsArmed)
        {
            leftCh[i] = 0.0f;
            if (rightCh) rightCh[i] = 0.0f;
            continue;
        }

        // ── Глайд ────────────────────────────────────────────────────────────
        currentSemitone += glideCoeff * (targetSemitone - currentSemitone);
        double freq = midiNoteToHz (currentSemitone);

        // ── Aftertouch → прямой детюн ─────────────────────────────────────────
        freq *= std::pow (2.0, (aftertouchCC / 127.0) * AFTERTOUCH_MAX_CENTS / 1200.0);

        // ── Вау и флаттер (pitch modulation) ─────────────────────────────────
        // Два LFO с разными частотами имитируют нестабильность кассетной ленты.
        // Вау (~0.8 Гц, ±0.15%) — медленные колебания высоты тона.
        // Флаттер (~8 Гц, ±0.05%) — быстрое дрожание, как у реального мотора.
        wowPhase     += juce::MathConstants<double>::twoPi * 0.8  / sampleRate_;
        flutterPhase += juce::MathConstants<double>::twoPi * 8.0  / sampleRate_;
        if (wowPhase     >= juce::MathConstants<double>::twoPi) wowPhase     -= juce::MathConstants<double>::twoPi;
        if (flutterPhase >= juce::MathConstants<double>::twoPi) flutterPhase -= juce::MathConstants<double>::twoPi;

        freq *= 1.0 + 0.0015 * std::sin (wowPhase)
                    + 0.0005 * std::sin (flutterPhase);

        // ── Осциллятор (polyBLEP) ─────────────────────────────────────────────
        const double dt        = freq / sampleRate_;
        const double rawSample = generateOscSample (oscPhase, waveform, dt);

        oscPhase += juce::MathConstants<double>::twoPi * dt;
        if (oscPhase >= juce::MathConstants<double>::twoPi)
            oscPhase -= juce::MathConstants<double>::twoPi;

        const double w = static_cast<double> (warmth);

        // ── Ламповый преамп 12AX7 (ADAA, asymmetric, gain-dependent) ─────────
        // При малом сигнале — почти линейный проход.
        // При нарастании амплитуды — мягкое насыщение с 2-й гармоникой.
        // Dynamic bias shift имитирует ток через сетку: транзиенты насыщаются сильнее.
        const double tubeOut = tubeSaturate12AX7 (rawSample, tubeXPrev, tubeBias,
                                                  tubeBiasCoeff, w);

        // ── Jiles-Atherton tape model (2x oversampled Euler) ────────────────
        // Полная физическая модель магнитного гистерезиса кассетной ленты.
        // H = входное поле (tubeOut × drive), M = намагниченность (состояние).
        // Два шага Эйлера на сэмпл (midpoint interpolation) для стабильности.
        {
            const double jaDrive = 0.5 + w * 0.8;
            const double H_mid = (jaInPrev + tubeOut) * 0.5 * jaDrive;
            const double H_cur = tubeOut * jaDrive;

            // Шаг 1: от jaHPrev до H_mid
            {
                const double dH = H_mid - jaHPrev;
                jaM += dH * jA_dMdH (jaM, jaHPrev, dH, JA_Ms, JA_a, JA_alpha, JA_k, JA_c);
                jaM = juce::jlimit (-JA_Ms, JA_Ms, jaM);
                jaHPrev = H_mid;
            }
            // Шаг 2: от H_mid до H_cur
            {
                const double dH = H_cur - jaHPrev;
                jaM += dH * jA_dMdH (jaM, jaHPrev, dH, JA_Ms, JA_a, JA_alpha, JA_k, JA_c);
                jaM = juce::jlimit (-JA_Ms, JA_Ms, jaM);
                jaHPrev = H_cur;
            }
            jaInPrev = tubeOut;
        }

        // Wet/dry: warmth=0 → tubeOut чистый; warmth=1 → полный гистерезис
        const double jaOut = tubeOut + w * (jaM / JA_Ms - tubeOut);

        // ── Loss model: head bump + HF rolloff ───────────────────────────────
        // Имитирует АЧХ воспроизводящей головки кассетного деки.
        const double hbOut  = processBiquad (jaOut, hbB0, hbB1, hbB2, hbA1, hbA2, hbZ1, hbZ2);
        const double lfOut  = processBiquad (hbOut, lfB0, lfB1, lfB2, lfA1, lfA2, lfZ1, lfZ2);
        // Blend loss model только при warmth > 0
        const double tapeOut = jaOut + w * (lfOut - jaOut);

        prevRawOsc = rawSample;

        // ── DC-блокер ─────────────────────────────────────────────────────────
        const double dcBlocked = tapeOut - dcX1 + DC_BLOCKER_R * dcY1;
        dcX1 = tapeOut;
        dcY1 = dcBlocked;

        const double sample = dcBlocked * ampSmoothed * static_cast<double> (volume);

        leftCh[i] = static_cast<float> (sample);
        if (rightCh) rightCh[i] = leftCh[i];
    }

    // ── Обновляем UI ──────────────────────────────────────────────────────────
    displayAmplitude.store (static_cast<float> (ampSmoothed));

    float dispSrc = 0.0f;
    switch (bowMode)
    {
        case Hold:   dispSrc = juce::jlimit (0.0f, 1.0f, (modWheelCC - ccMin) / (ccMax - ccMin)); break;
        case Bow:    dispSrc = bowSpeedOut; break;
        case Insane: dispSrc = insaneBowSpeed; break;
        default:     break;
    }
    displayModWheel.store (dispSrc);
}

// ─── PolyBLEP ─────────────────────────────────────────────────────────────────
// Сглаживает разрыв наивного осциллятора полиномиальной коррекцией.
// t  = phase / (2π) ∈ [0, 1)
// dt = freq / sampleRate — ширина окна коррекции
double OndesProcessor::polyBlep (double t, double dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

// ─── Waveform ─────────────────────────────────────────────────────────────────
double OndesProcessor::generateOscSample (double phase, int waveform, double dt) noexcept
{
    const double pi    = juce::MathConstants<double>::pi;
    const double twoPi = juce::MathConstants<double>::twoPi;
    const double t     = phase / twoPi;   // нормированная фаза [0, 1)

    switch (waveform)
    {
        case Sine:
            return std::sin (phase);   // синус без разрывов — polyBLEP не нужен

        case Sawtooth:
        {
            // Наивная пила [-1, +1] с разрывом в t=0
            double saw = 2.0 * t - 1.0;
            saw -= polyBlep (t, dt);   // сглаживаем разрыв
            return saw;
        }

        case Square:
        {
            // Наивный квадрат с разрывами в t=0 и t=0.5
            double sq = (phase < pi) ? 1.0 : -1.0;
            sq += polyBlep (t, dt);                           // разрыв при t=0
            sq -= polyBlep (std::fmod (t + 0.5, 1.0), dt);   // разрыв при t=0.5
            return sq;
        }

        case Triangle:
            // asin(sin) — уже гладкий, разрывов нет
            return (2.0 / pi) * std::asin (std::sin (phase));

        default:
            return std::sin (phase);
    }
}


// ─── Jiles-Atherton tape model ────────────────────────────────────────────────

// Функция Ланжевена: L(x) = coth(x) - 1/x
// Тейлор-разложение для x→0 (избегаем деления на 0 и потери точности)
double OndesProcessor::langevin (double x) noexcept
{
    if (std::abs (x) < 1.0e-4)
        return x / 3.0;
    return 1.0 / std::tanh (x) - 1.0 / x;
}

// Производная L'(x) = 1/x² - 1/sinh²(x)
double OndesProcessor::langevinDeriv (double x) noexcept
{
    if (std::abs (x) < 1.0e-4)
        return 1.0 / 3.0;
    const double sh = std::sinh (x);
    return 1.0 / (x * x) - 1.0 / (sh * sh);
}

// Полное уравнение J-A: dM/dH
// Описывает скорость изменения намагниченности по полю.
// delta = направление обхода; deltaM = физическое ограничение (петля гистерезиса).
// c = доля обратимых доменов; alpha = межмолекулярное взаимодействие.
double OndesProcessor::jA_dMdH (double M, double H, double dH,
                                  double Ms, double a, double alpha,
                                  double k,  double c) noexcept
{
    const double Q       = (H + alpha * M) / a;
    const double Man     = Ms * langevin (Q);
    const double ManDeriv = (Ms / a) * langevinDeriv (Q);

    const double delta  = (dH >= 0.0) ? 1.0 : -1.0;
    const double deltaM = ((Man - M) * delta > 0.0) ? 1.0 : 0.0;

    // Знаменатель необратимой части
    const double denom1 = (1.0 - c) * delta * k - alpha * (Man - M);
    const double chi    = c * ManDeriv;

    // Защита от нуля
    if (std::abs (denom1) < 1.0e-10)
        return chi / std::max (1.0 - chi * alpha, 1.0e-10);

    const double A      = (1.0 - c) * deltaM * (Man - M) / denom1;
    const double denom2 = 1.0 - chi * alpha;
    return (A + chi) / std::max (denom2, 1.0e-10);
}

// Transposed Direct Form II biquad (численно стабилен)
double OndesProcessor::processBiquad (double x,
                                       double b0, double b1, double b2,
                                       double a1, double a2,
                                       double& z1, double& z2) noexcept
{
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

// Вычисляет коэффициенты двух biquad-фильтров loss model:
//   head bump:  пик ~150 Гц, +3.5 дБ, Q=0.8  (резонанс воспроизводящей головки)
//   HF rolloff: Butterworth LP ~10 кГц        (потери высоких частот на ленте)
void OndesProcessor::computeLossFilters (double sr)
{
    const double pi = juce::MathConstants<double>::pi;

    // ── Head bump: peaking EQ 150 Hz, +3.5 dB, Q=0.8 ────────────────────────
    {
        const double A    = std::pow (10.0, 3.5 / 40.0);  // sqrt(amplitude), для peaking
        const double w0   = 2.0 * pi * 150.0 / sr;
        const double s    = std::sin (w0);
        const double cosW = std::cos (w0);
        const double alp  = s / (2.0 * 0.8);
        const double a0   = 1.0 + alp / A;
        hbB0 = (1.0 + alp * A) / a0;
        hbB1 = (-2.0 * cosW)    / a0;
        hbB2 = (1.0 - alp * A) / a0;
        hbA1 = (-2.0 * cosW)    / a0;
        hbA2 = (1.0 - alp / A) / a0;
    }

    // ── HF rolloff: Butterworth LP 10 kHz ────────────────────────────────────
    {
        const double w1  = 2.0 * pi * 10000.0 / sr;
        const double s1  = std::sin (w1);
        const double c1  = std::cos (w1);
        const double alp1 = s1 / std::sqrt (2.0);
        const double a01  = 1.0 + alp1;
        lfB0 = ((1.0 - c1) * 0.5) / a01;
        lfB1 = (1.0 - c1)          / a01;
        lfB2 = ((1.0 - c1) * 0.5) / a01;
        lfA1 = (-2.0 * c1)         / a01;
        lfA2 = (1.0 - alp1)        / a01;
    }
}


// ─── 12AX7 Tube Preamp — ADAA + Asymmetric Bias ──────────────────────────────

// F₁(x) = ln(cosh(x)) — первообразная tanh.
// Численно стабильная форма: при больших |x| cosh(x) ≈ e^|x|/2,
// поэтому ln(cosh(x)) ≈ |x| - ln(2), что не даёт переполнения.
double OndesProcessor::adaaTanhF1 (double x) noexcept
{
    const double ax = std::abs (x);
    // ln(cosh x) = |x| + ln(1 + e^{-2|x|}) - ln2
    return ax + std::log1p (std::exp (-2.0 * ax)) - 0.6931471805599453;
}

// ADAA 1st-order для f(x) = tanh(x).
// y[n] = (F₁(x[n]) − F₁(x[n−1])) / (x[n] − x[n−1])
// При |diff| < eps — midpoint fallback (избегаем деления на нуль).
// Результат: подавление алиасинга ≈ как 4–8× оверсэмплинг, но без CPU-цены.
double OndesProcessor::adaaTanh (double x, double xPrev) noexcept
{
    constexpr double eps = 1.0e-8;
    const double diff = x - xPrev;
    if (std::abs (diff) < eps)
        return std::tanh ((x + xPrev) * 0.5);   // midpoint
    return (adaaTanhF1 (x) - adaaTanhF1 (xPrev)) / diff;
}

// Полная модель лампового каскада.
// tubeBiasCoeffArg передаётся параметром (а не берётся как член), чтобы
// функция оставалась noexcept-pure и не требовала доступа к this.
double OndesProcessor::tubeSaturate12AX7 (double in,
                                           double& xPrev,
                                           double& biasState,
                                           double tubeBiasCoeffArg,
                                           double warmth) noexcept
{
    if (warmth < 0.001) return in;

    // Drive: мягкий режим 1→3.5 (не перегруз, а ламповая теплота)
    const double drive = 1.0 + warmth * 2.5;

    // ── Ток сетки (grid current): динамическое смещение рабочей точки ────────
    // biasState — LPF(in, τ≈150мс). Отстаёт от сигнала при атаках →
    // рабочая точка временно смещается сильнее → атака насыщается ярче (2-я гарм.).
    biasState = biasState * tubeBiasCoeffArg + in * (1.0 - tubeBiasCoeffArg);
    // staticBias смещает рабочую точку → 2-я гармоника доминирует (ламповое тепло).
    // Повышен с 0.10 до 0.22 — теперь слышен даже при warmth=0.2.
    const double staticBias  = warmth * 0.22;
    // dynamicBias = ток сетки: атаки насыщаются сильнее, потом точка возвращается.
    const double dynamicBias = biasState * warmth * 0.28;

    // Полный вход в нелинейность
    const double x = in * drive + staticBias + dynamicBias;

    // ADAA 1st-order tanh — alias-free
    const double y = adaaTanh (x, xPrev);
    xPrev = x;

    // Нормировка: делим на (drive*0.70) вместо drive → сохраняем часть компрессии,
    // которая и даёт «ламповое приближение» вместо просто waveshaper.
    const double normalized = y / (drive * 0.70);

    // Wet/dry
    return in + warmth * (normalized - in);
}

// ─── State ────────────────────────────────────────────────────────────────────
void OndesProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void OndesProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* OndesProcessor::createEditor() { return new OndesEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()    { return new OndesProcessor(); }
