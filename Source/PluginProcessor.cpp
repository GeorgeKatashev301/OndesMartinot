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

    return { params.begin(), params.end() };
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────
void OndesProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRate_ = sampleRate;

    // Амплитуда: сглаживание ~2 мс
    ampSmoothCoeff = std::exp (-1.0 / (0.002 * sampleRate));

    // Тень CC: τ=25 мс — за ней следует «скорость» движения колеса
    shadowCoeff = std::exp (-1.0 / (0.025 * sampleRate));

    // Выходной LPF для bow speed: τ=15 мс — убирает CC-спайки
    bowOutCoeff = std::exp (-1.0 / (0.015 * sampleRate));

    // Insane: затухание ~50 мс (полужизнь)
    insaneDecayCoeff = std::exp (-std::log (2.0) / (0.05 * sampleRate));

    // ≈100мс "тишины" для детекции первого касания колеса
    ccSilenceThreshold = static_cast<int> (sampleRate * 0.1);
    ccSilenceCounter   = ccSilenceThreshold + 1; // стартуем как будто давно тишина

    // Сброс
    currentNote    = -1;    keyIsHeld   = false;  noteIsArmed = false;
    ampSmoothed    = 0.0;   oscPhase    = 0.0;
    modWheelCC     = 0.0f;  aftertouchCC   = 0.0f;
    shadowCC       = 0.0f;  bowSpeedOut    = 0.0f;  insaneBowSpeed = 0.0f;
    currentSemitone = 69.0; targetSemitone = 69.0;  glideCoeff = 1.0;
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

        if (ampSmoothed < 0.001) oscPhase = 0.0;

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

        // ── Осциллятор ────────────────────────────────────────────────────────
        const double sample = generateOscSample (oscPhase, waveform)
                              * ampSmoothed
                              * static_cast<double> (volume);

        oscPhase += juce::MathConstants<double>::twoPi * freq / sampleRate_;
        if (oscPhase >= juce::MathConstants<double>::twoPi)
            oscPhase -= juce::MathConstants<double>::twoPi;

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

// ─── Waveform ─────────────────────────────────────────────────────────────────
double OndesProcessor::generateOscSample (double phase, int waveform) noexcept
{
    const double pi = juce::MathConstants<double>::pi;
    switch (waveform)
    {
        case Sine:     return std::sin (phase);
        case Sawtooth: return 1.0 - (phase / pi);
        case Square:   return phase < pi ? 1.0 : -1.0;
        case Triangle: return (2.0 / pi) * std::asin (std::sin (phase));
        default:       return std::sin (phase);
    }
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
