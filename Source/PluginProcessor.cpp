#include "PluginProcessor.h"
#include "PluginEditor.h"

// =============================================================================
OndesProcessor::OndesProcessor()
    : AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
}

OndesProcessor::~OndesProcessor() {}

// ─── Parameter Layout ─────────────────────────────────────────────────────────
juce::AudioProcessorValueTreeState::ParameterLayout OndesProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Hold режим: ON — нота живёт после отпускания клавиши
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "holdMode", "Hold Mode", false));

    // Нижний порог CC (значения ≤ ccMin считаются нулём)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "ccMin", "CC Min",
        juce::NormalisableRange<float> (0.0f, 120.0f, 1.0f), 20.0f));

    // Верхний порог CC (значения ≥ ccMax считаются максимумом)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "ccMax", "CC Max",
        juce::NormalisableRange<float> (7.0f, 127.0f, 1.0f), 120.0f));

    // Максимальное время глайда (сек); velocity=127 → мгновенный глайд
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "glideMax", "Max Glide",
        juce::NormalisableRange<float> (0.0f, 3.0f, 0.01f), 0.5f));

    // Общая громкость
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "volume", "Volume",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f));

    return { params.begin(), params.end() };
}

// ─── Lifecycle ─────────────────────────────────────────────────────────────────
void OndesProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    sampleRate_ = sampleRate;

    // Сглаживание амплитуды ~2 мс — достаточно быстро, чтобы не «обрезать»
    // резкие движения колеса, и достаточно медленно, чтобы избежать щелчков.
    const double smoothMs = 2.0;
    ampSmoothCoeff = std::exp (-1.0 / (smoothMs * 0.001 * sampleRate));

    // Сброс состояния при перезапуске
    currentNote    = -1;
    keyIsHeld      = false;
    noteIsArmed    = false;
    ampSmoothed    = 0.0;
    oscPhase       = 0.0;
    vibratoPhase   = 0.0;
    modWheelCC     = 0.0f;
    aftertouchCC   = 0.0f;
    currentSemitone = 69.0;
    targetSemitone  = 69.0;
    glideCoeff      = 1.0;
}

void OndesProcessor::releaseResources() {}

// ─── MIDI обработчик (аудио-поток) ────────────────────────────────────────────
void OndesProcessor::processMidiEvent (const juce::MidiMessage& msg,
                                        bool holdMode,
                                        float ccMin, float ccMax,
                                        float maxGlide)
{
    if (msg.isNoteOn())
    {
        const int   newNote   = msg.getNoteNumber();
        const float velocity  = msg.getFloatVelocity(); // 0.0 (тихо) … 1.0 (форте)

        targetSemitone = static_cast<double> (newNote);

        if (!noteIsArmed || currentNote == -1)
        {
            // Первая нота или звук молчал — снэп без глайда
            currentSemitone = targetSemitone;
            glideCoeff      = 1.0;
        }
        else
        {
            // Высокий velocity = быстрый глайд; velocity=1.0 → мгновенно
            const float glideTimeSec = maxGlide * (1.0f - velocity);
            if (glideTimeSec < 0.001f)
            {
                glideCoeff = 1.0;
            }
            else
            {
                glideCoeff = 1.0 - std::exp (-1.0 / (glideTimeSec * sampleRate_));
            }
        }

        // Если приходим из тишины — сбрасываем фазу, чтобы атака была чистой
        if (ampSmoothed < 0.001)
            oscPhase = 0.0;

        currentNote  = newNote;
        keyIsHeld    = true;
        noteIsArmed  = true;

        // При новой ноте сбрасываем aftertouch
        aftertouchCC = 0.0f;
    }
    else if (msg.isNoteOff())
    {
        if (msg.getNoteNumber() == currentNote)
        {
            keyIsHeld = false;

            if (!holdMode)
            {
                // Смычковый режим: отпустил клавишу → нота снимается
                noteIsArmed = false;
            }
            // Hold режим: нота продолжает жить, колесо управляет громкостью
        }
    }
    else if (msg.isController() && msg.getControllerNumber() == 1)
    {
        // Колесо модуляции (CC1)
        modWheelCC = static_cast<float> (msg.getControllerValue());

        // В Hold режиме: если колесо опущено ниже ccMin И клавиша не зажата,
        // снимаем нотный контекст (повторный подъём колеса = тишина)
        if (holdMode && !keyIsHeld && modWheelCC <= ccMin)
            noteIsArmed = false;
    }
    else if (msg.isAftertouch() && msg.getNoteNumber() == currentNote)
    {
        aftertouchCC = static_cast<float> (msg.getAfterTouchValue());
    }
    else if (msg.isChannelPressure())
    {
        aftertouchCC = static_cast<float> (msg.getChannelPressureValue());
    }
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
    {
        currentNote  = -1;
        keyIsHeld    = false;
        noteIsArmed  = false;
        modWheelCC   = 0.0f;
        aftertouchCC = 0.0f;
    }
}

// ─── Целевая амплитуда ─────────────────────────────────────────────────────────
double OndesProcessor::getTargetAmplitude (bool holdMode,
                                            float ccMin,
                                            float ccMax) const
{
    if (!noteIsArmed)
        return 0.0;

    // Нормируем CC → 0.0 … 1.0, зажимая по порогам
    const float norm = juce::jlimit (0.0f, 1.0f,
                                     (modWheelCC - ccMin) / (ccMax - ccMin));

    if (holdMode)
    {
        // Hold: клавиша уже не влияет, только колесо
        return static_cast<double> (norm);
    }
    else
    {
        // Смычок: нужны ОБОИХ — клавиша И колесо вверх
        return keyIsHeld ? static_cast<double> (norm) : 0.0;
    }
}

// ─── Главный цикл ─────────────────────────────────────────────────────────────
void OndesProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Кэшируем параметры на блок (атомарные reads дёшевы, но так чище)
    const bool  holdMode = apvts.getRawParameterValue ("holdMode")->load() > 0.5f;
    const float ccMin    = apvts.getRawParameterValue ("ccMin")->load();
    const float ccMax    = apvts.getRawParameterValue ("ccMax")->load();
    const float maxGlide = apvts.getRawParameterValue ("glideMax")->load();
    const float volume   = apvts.getRawParameterValue ("volume")->load();

    const int numSamples = buffer.getNumSamples();
    float* leftCh  = buffer.getWritePointer (0);
    float* rightCh = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    auto midiIt = midiMessages.begin();

    for (int i = 0; i < numSamples; ++i)
    {
        // ── Sample-accurate MIDI ─────────────────────────────────────────────
        while (midiIt != midiMessages.end())
        {
            auto meta = *midiIt;
            if (meta.samplePosition > i) break;
            processMidiEvent (meta.getMessage(), holdMode, ccMin, ccMax, maxGlide);
            ++midiIt;
        }

        // ── Целевая амплитуда → сглаженная ──────────────────────────────────
        const double targetAmp = getTargetAmplitude (holdMode, ccMin, ccMax);
        ampSmoothed = ampSmoothed * ampSmoothCoeff + targetAmp * (1.0 - ampSmoothCoeff);

        // Пропускаем генерацию, если тихо и нет активной ноты
        if (ampSmoothed < 1e-5 && !noteIsArmed)
        {
            leftCh[i] = 0.0f;
            if (rightCh) rightCh[i] = 0.0f;
            continue;
        }

        // ── Глайд: скользим по полутонам к цели ─────────────────────────────
        currentSemitone += glideCoeff * (targetSemitone - currentSemitone);
        double freq = midiNoteToHz (currentSemitone);

        // ── Вибрато от aftertouch ────────────────────────────────────────────
        // Максимальная глубина = VIBRATO_MAX_CENTS при aftertouch=127
        const double vibratoDepthCents = (aftertouchCC / 127.0) * VIBRATO_MAX_CENTS;
        const double vibrato = std::sin (vibratoPhase) * vibratoDepthCents / 1200.0; // в октавах
        freq *= std::pow (2.0, vibrato);

        vibratoPhase += juce::MathConstants<double>::twoPi * VIBRATO_RATE_HZ / sampleRate_;
        if (vibratoPhase >= juce::MathConstants<double>::twoPi)
            vibratoPhase -= juce::MathConstants<double>::twoPi;

        // ── Синус-осциллятор ─────────────────────────────────────────────────
        const double sample = std::sin (oscPhase) * ampSmoothed * static_cast<double> (volume);

        oscPhase += juce::MathConstants<double>::twoPi * freq / sampleRate_;
        if (oscPhase >= juce::MathConstants<double>::twoPi)
            oscPhase -= juce::MathConstants<double>::twoPi;

        leftCh[i] = static_cast<float> (sample);
        if (rightCh) rightCh[i] = leftCh[i];
    }

    // Обновляем значения для UI (читает таймер в Editor, не критично по времени)
    displayAmplitude.store (static_cast<float> (ampSmoothed));
    displayModWheel .store (juce::jlimit (0.0f, 1.0f,
                                          (modWheelCC - ccMin) / (ccMax - ccMin)));
}

// ─── State persistence ────────────────────────────────────────────────────────
void OndesProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void OndesProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// ─── Editor factory ───────────────────────────────────────────────────────────
juce::AudioProcessorEditor* OndesProcessor::createEditor()
{
    return new OndesEditor (*this);
}

// ─── JUCE plugin entry point ──────────────────────────────────────────────────
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OndesProcessor();
}
