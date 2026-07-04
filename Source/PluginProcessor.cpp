#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <algorithm>

namespace
{
struct HubRegistryEntry
{
    int instanceId = 0;
    mixteacher::HubTrackSummary track;
    double updatedMs = 0.0;
};

juce::CriticalSection hubRegistryLock;
std::vector<HubRegistryEntry> hubRegistry;
std::vector<MixTeacherAudioProcessor*> hubProcessors;
std::atomic<int> nextHubInstanceId { 1 };

float linearToDb(float value)
{
    return juce::Decibels::gainToDecibels(juce::jmax(value, 1.0e-6f), -120.0f);
}

float normaliseDb(float db, float floorDb = -72.0f, float ceilingDb = 0.0f)
{
    return juce::jlimit(0.0f, 1.0f, (db - floorDb) / (ceilingDb - floorDb));
}

float dbToPower(float db)
{
    if (db <= -119.0f)
        return 0.0f;

    return std::pow(10.0f, db / 10.0f);
}

float powerToDb(float power)
{
    return juce::Decibels::gainToDecibels(std::sqrt(juce::jmax(power, 1.0e-12f)), -120.0f);
}

float percentile(std::vector<float> values, float p)
{
    if (values.empty())
        return -120.0f;

    std::sort(values.begin(), values.end());
    const auto index = juce::jlimit(0, static_cast<int>(values.size()) - 1, static_cast<int>(std::round((values.size() - 1) * p)));
    return values[static_cast<size_t>(index)];
}

juce::String normaliseTrackName(juce::String text)
{
    return text.toLowerCase().replace(" ", "_");
}

juce::String hubText(bool russian, const char* ru, const char* en)
{
    return russian ? juce::String::fromUTF8(ru) : juce::String(en);
}

bool isType(const mixteacher::HubTrackSummary& track, const juce::String& type)
{
    return track.effectiveType == type || track.manualType == type || track.detectedType == type;
}

float maxLowDb(const mixteacher::HubTrackSummary& track)
{
    return juce::jmax(track.bandDb.lowFoundation, track.bandDb.lowBody);
}

juce::String trackName(const mixteacher::HubTrackSummary& track)
{
    if (track.effectiveType.isNotEmpty() && track.effectiveType != "unknown")
        return track.effectiveType;

    return juce::String("track ") + juce::String(track.instanceId);
}

float bandValueDb(const mixteacher::HubTrackSummary& track, int bandIndex)
{
    switch (bandIndex)
    {
        case 0: return track.bandDb.lowFoundation;
        case 1: return track.bandDb.lowBody;
        case 2: return track.bandDb.mud;
        case 3: return track.bandDb.boxiness;
        case 4: return track.bandDb.tone;
        case 5: return track.bandDb.presence;
        case 6: return track.bandDb.sibilance;
        case 7: return track.bandDb.air;
        default: return -120.0f;
    }
}

juce::String bandLabel(bool russian, int bandIndex)
{
    switch (bandIndex)
    {
        case 0: return hubText(russian, "40-80 Hz фундамент", "40-80 Hz foundation");
        case 1: return hubText(russian, "80-150 Hz тело", "80-150 Hz body");
        case 2: return hubText(russian, "150-350 Hz муть", "150-350 Hz mud");
        case 3: return hubText(russian, "350-800 Hz коробка", "350-800 Hz boxiness");
        case 4: return hubText(russian, "800 Hz-2 kHz тон", "800 Hz-2 kHz tone");
        case 5: return hubText(russian, "2-5 kHz атака/читаемость", "2-5 kHz attack/readability");
        case 6: return hubText(russian, "5-9 kHz резкость", "5-9 kHz sharpness");
        case 7: return hubText(russian, "9-16 kHz воздух", "9-16 kHz air");
        default: return "band";
    }
}

juce::String bandAction(bool russian, int bandIndex)
{
    switch (bandIndex)
    {
        case 0:
            return hubText(russian,
                           "Выбери главный источник саба. Обычно kick или bass. На остальных дорожках попробуй high-pass или dynamic EQ, не режь всё сразу.",
                           "Choose the main sub source, usually kick or bass. Try high-pass or dynamic EQ on the others, not broad cuts everywhere.");
        case 1:
            return hubText(russian,
                           "Раздели тело кика, баса и томов: один источник оставь главным, у второго попробуй узкий cut 1-3 dB или sidechain.",
                           "Separate kick, bass, and tom body: keep one source dominant, try a narrow 1-3 dB cut or sidechain on the second.");
        case 2:
            return hubText(russian,
                           "Сначала убери муть у второстепенных дорожек: high-pass где можно и EQ cut 1-3 dB в 180-300 Hz. Вокал/бас не режь вслепую.",
                           "Clean secondary tracks first: high-pass where possible and try a 1-3 dB cut around 180-300 Hz. Do not blindly cut vocal/bass.");
        case 3:
            return hubText(russian,
                           "Если микс картонный, проверь 400-700 Hz на снейре, гитарах, пиано и комнатах. Режь только там, где после bypass стало лучше.",
                           "If the mix feels cardboard-like, check 400-700 Hz on snare, guitars, piano, and rooms. Cut only where bypass proves it helps.");
        case 4:
            return hubText(russian,
                           "Эта зона быстро забивает центр. Выбери, кто несёт тон, а у подложек попробуй лёгкий cut 1-2 dB.",
                           "This range crowds the center quickly. Choose who carries the tone and try a light 1-2 dB cut on supporting layers.");
        case 5:
            return hubText(russian,
                           "2-5 kHz отвечает за читаемость. Если вокал важен, освободи место в гитарах/синтах/снейре dynamic EQ или cut 1-3 dB.",
                           "2-5 kHz carries readability. If vocal matters, make room in guitars/synths/snare with dynamic EQ or a 1-3 dB cut.");
        case 6:
            return hubText(russian,
                           "Не глуши весь верх. Найди самый раздражающий источник: hats, sibilance, synth или cymbals. Используй de-esser/dynamic EQ 2-4 dB.",
                           "Do not dull the whole top. Find the harsh source: hats, sibilance, synth, or cymbals. Use de-esser/dynamic EQ by 2-4 dB.");
        case 7:
            return hubText(russian,
                           "Если слишком много air, микс может стать песочным. Оставь воздух главному элементу, остальным чуть убери shelf 1-2 dB.",
                           "Too much air can make the mix sandy. Keep air for the main element and slightly shelf down supporting tracks by 1-2 dB.");
        default:
            return {};
    }
}

float maxCurveValue(const std::array<float, mixteacher::dynamicsBins>& values)
{
    auto result = 0.0f;
    for (const auto value : values)
        result = juce::jmax(result, value);

    return result;
}

mixteacher::HubLowEndTiming analyseLowEndTiming(const mixteacher::HubTrackSummary& a,
                                                const mixteacher::HubTrackSummary& b)
{
    mixteacher::HubLowEndTiming timing;
    timing.available = false;
    timing.trackAId = a.instanceId;
    timing.trackBId = b.instanceId;
    timing.trackAType = trackName(a);
    timing.trackBType = trackName(b);

    const auto maxA = maxCurveValue(a.lowEndCurve);
    const auto maxB = maxCurveValue(b.lowEndCurve);

    if (maxA < 0.08f || maxB < 0.08f)
        return timing;

    const auto thresholdA = juce::jmax(0.07f, maxA * 0.34f);
    const auto thresholdB = juce::jmax(0.07f, maxB * 0.34f);
    float weightedOverlap = 0.0f;
    float weightedEither = 0.0f;

    for (int i = 0; i < mixteacher::dynamicsBins; ++i)
    {
        const auto aValue = a.lowEndCurve[static_cast<size_t>(i)];
        const auto bValue = b.lowEndCurve[static_cast<size_t>(i)];
        const auto aActive = aValue >= thresholdA;
        const auto bActive = bValue >= thresholdB;

        if (aActive || bActive)
        {
            ++timing.activeBins;
            weightedEither += juce::jmax(aValue, bValue);
        }

        if (aActive && bActive)
        {
            ++timing.overlapBins;
            const auto overlap = std::sqrt(juce::jmax(0.0f, aValue * bValue));
            timing.curve[static_cast<size_t>(i)] = overlap;
            weightedOverlap += overlap;
        }
    }

    if (timing.activeBins <= 0)
        return timing;

    timing.available = true;
    timing.simultaneousRatio = static_cast<float>(timing.overlapBins) / static_cast<float>(timing.activeBins);
    timing.weightedOverlap = weightedEither > 1.0e-5f ? weightedOverlap / weightedEither : 0.0f;

    const auto weakerLowDb = juce::jmin(maxLowDb(a), maxLowDb(b));
    const auto audibility = juce::jlimit(0.0f, 1.0f, (weakerLowDb + 74.0f) / 34.0f);
    timing.risk = juce::jlimit(0.0f,
                               1.0f,
                               timing.simultaneousRatio * 0.48f + timing.weightedOverlap * 0.34f + audibility * 0.18f);

    if (timing.risk > 0.68f)
        timing.severity = mixteacher::Severity::problem;
    else if (timing.risk > 0.42f)
        timing.severity = mixteacher::Severity::warning;
    else
        timing.severity = mixteacher::Severity::info;

    return timing;
}

mixteacher::HubTrackSummary makeHubTrackSummary(int instanceId, const mixteacher::AnalysisSnapshot& snapshot)
{
    mixteacher::HubTrackSummary summary;
    summary.instanceId = instanceId;
    summary.manualType = snapshot.track.manualType;
    summary.detectedType = snapshot.track.detectedType;
    summary.effectiveType = snapshot.track.effectiveType;
    summary.typeConfidence = snapshot.track.confidence;
    summary.peakDbfs = snapshot.peakDbfs;
    summary.rmsDbfs = snapshot.rmsDbfs;
    summary.lufsShortTerm = snapshot.lufsShortTerm;
    summary.crestFactorDb = snapshot.crestFactorDb;
    summary.headroomDb = snapshot.headroomDb;
    summary.stereoCorrelation = snapshot.stereoCorrelation;
    summary.stereoWidth = snapshot.stereoWidth;
    summary.monoFoldDownLossDb = snapshot.monoFoldDownLossDb;
    summary.clippingDetected = snapshot.clippingDetected;
    summary.validForAnalysis = snapshot.validity.isValidForAnalysis;
    summary.bandDb = snapshot.bandDb;
    summary.lowEndCurve = snapshot.lowEndCurve;
    summary.drumProfile = snapshot.drumProfile;
    summary.transientScore = snapshot.transientScore;
    summary.ageSec = 0.0;
    return summary;
}
} // namespace

MixTeacherAudioProcessor::MixTeacherAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    instanceId = nextHubInstanceId.fetch_add(1, std::memory_order_relaxed);
    const juce::ScopedLock lock(hubRegistryLock);
    hubProcessors.push_back(this);
    analysisHistory.resize(static_cast<size_t>(currentSampleRate * 3.0));
}

MixTeacherAudioProcessor::~MixTeacherAudioProcessor()
{
    removePublishedTrack();
    const juce::ScopedLock lock(hubRegistryLock);
    hubProcessors.erase(std::remove(hubProcessors.begin(), hubProcessors.end(), this), hubProcessors.end());
}

juce::AudioProcessorValueTreeState::ParameterLayout MixTeacherAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "trackType", 1 },
        "Source",
        juce::StringArray { "Auto", "Vocal", "Drums", "Drums Bus", "Kick", "Snare", "Bass", "Guitar", "Piano", "Synth", "FX", "Master" },
        0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "explanationMode", 1 },
        "Explanation Mode",
        juce::StringArray { "Beginner", "Intermediate", "Advanced" },
        0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "language", 1 },
        "Language",
        juce::StringArray { juce::String::fromUTF8("Русский"), "English" },
        0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "pluginRole", 1 },
        "Role",
        juce::StringArray { "Track", "Mix Hub" },
        0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "spectrumFftSize", 1 },
        "FFT Size",
        juce::StringArray { "1024", "2048", "4096", "8192" },
        2));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "goodizer", 1 },
        "Goodizer",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
        0.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "freeze", 1 },
        "Freeze",
        false));

    return { params.begin(), params.end() };
}

void MixTeacherAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    analysisHistory.assign(static_cast<size_t>(juce::jmax(maxFftSize, static_cast<int>(currentSampleRate * 3.0))), 0.0f);
    historyWriteIndex = 0;
    goodizerLowState.fill(0.0f);
    previousTruePeakSample.fill(0.0f);
    resetAnalysis();
}

void MixTeacherAudioProcessor::releaseResources()
{
}

bool MixTeacherAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& input = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();

    if (input != output)
        return false;

    return input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo();
}

void MixTeacherAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    midiMessages.clear();

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();
    const auto numSamples = buffer.getNumSamples();

    for (auto channel = totalNumInputChannels; channel < totalNumOutputChannels; ++channel)
        buffer.clear(channel, 0, numSamples);

    if (!isMixHubMode())
        applyGoodizer(buffer);

    float peak = 0.0f;
    float truePeak = 0.0f;
    double sumSquares = 0.0;
    double leftSquares = 0.0;
    double rightSquares = 0.0;
    double crossSum = 0.0;
    double sideSquares = 0.0;
    double midSquares = 0.0;
    double monoSquares = 0.0;
    int blockClips = 0;
    const auto channelsToAnalyse = juce::jmax(1, totalNumInputChannels);
    const auto samplesToStore = juce::jmin(numSamples, maxBlockSamples);
    const auto truePeakChannels = juce::jmin(totalNumInputChannels, static_cast<int>(previousTruePeakSample.size()));

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float mono = 0.0f;

        for (int channel = 0; channel < totalNumInputChannels; ++channel)
        {
            const auto value = buffer.getReadPointer(channel)[sample];
            const auto absValue = std::abs(value);
            peak = juce::jmax(peak, absValue);
            sumSquares += static_cast<double>(value) * static_cast<double>(value);

            if (channel < truePeakChannels)
            {
                const auto previous = previousTruePeakSample[static_cast<size_t>(channel)];
                truePeak = juce::jmax(truePeak, absValue);
                truePeak = juce::jmax(truePeak, std::abs(previous + (value - previous) * 0.25f));
                truePeak = juce::jmax(truePeak, std::abs(previous + (value - previous) * 0.50f));
                truePeak = juce::jmax(truePeak, std::abs(previous + (value - previous) * 0.75f));
                previousTruePeakSample[static_cast<size_t>(channel)] = value;
            }

            if (absValue >= 0.999f)
                ++blockClips;

            mono += value;
        }

        if (totalNumInputChannels >= 2)
        {
            const auto left = buffer.getReadPointer(0)[sample];
            const auto right = buffer.getReadPointer(1)[sample];
            const auto mid = (left + right) * 0.5f;
            const auto side = (left - right) * 0.5f;
            leftSquares += static_cast<double>(left) * static_cast<double>(left);
            rightSquares += static_cast<double>(right) * static_cast<double>(right);
            crossSum += static_cast<double>(left) * static_cast<double>(right);
            midSquares += static_cast<double>(mid) * static_cast<double>(mid);
            sideSquares += static_cast<double>(side) * static_cast<double>(side);
            monoSquares += static_cast<double>(mid) * static_cast<double>(mid);
        }

        mono /= static_cast<float>(channelsToAnalyse);

        if (sample < samplesToStore)
            monoScratch[static_cast<size_t>(sample)] = mono;
    }

    const auto rms = std::sqrt(sumSquares / juce::jmax(1, numSamples * channelsToAnalyse));
    latestPeakLinear.store(peak, std::memory_order_relaxed);
    latestTruePeakLinear.store(juce::jmax(peak, truePeak), std::memory_order_relaxed);
    latestRmsLinear.store(static_cast<float>(rms), std::memory_order_relaxed);

    if (totalNumInputChannels >= 2 && rms > 1.0e-5)
    {
        const auto denom = std::sqrt(leftSquares * rightSquares);
        const auto correlation = denom > 1.0e-12 ? juce::jlimit(-1.0, 1.0, crossSum / denom) : 1.0;
        const auto width = midSquares > 1.0e-12 ? std::sqrt(sideSquares / midSquares) : 0.0;
        const auto stereoRms = std::sqrt((leftSquares + rightSquares) / juce::jmax(1, numSamples * 2));
        const auto monoRms = std::sqrt(monoSquares / juce::jmax(1, numSamples));
        const auto monoLossDb = linearToDb(static_cast<float>(monoRms)) - linearToDb(static_cast<float>(stereoRms));

        latestStereoCorrelation.store(static_cast<float>(correlation), std::memory_order_relaxed);
        latestStereoWidth.store(juce::jlimit(0.0f, 2.5f, static_cast<float>(width)), std::memory_order_relaxed);
        latestMonoFoldDownLossDb.store(juce::jlimit(-24.0f, 6.0f, monoLossDb), std::memory_order_relaxed);
    }
    else if (totalNumInputChannels < 2)
    {
        latestStereoCorrelation.store(1.0f, std::memory_order_relaxed);
        latestStereoWidth.store(0.0f, std::memory_order_relaxed);
        latestMonoFoldDownLossDb.store(0.0f, std::memory_order_relaxed);
    }

    clippingCount.fetch_add(blockClips, std::memory_order_relaxed);
    analysedSamples.fetch_add(static_cast<int64_t>(numSamples), std::memory_order_relaxed);

    for (int sample = 0; sample < samplesToStore; ++sample)
        writeSampleToFifo(monoScratch[static_cast<size_t>(sample)]);
}

void MixTeacherAudioProcessor::applyGoodizer(juce::AudioBuffer<float>& buffer)
{
    auto* amountParameter = parameters.getRawParameterValue("goodizer");
    const auto amount = amountParameter != nullptr ? amountParameter->load() : 0.0f;

    if (amount <= 0.0001f)
        return;

    const auto shaped = amount * amount;
    const auto drive = 1.0f + shaped * 7.5f;
    const auto wet = juce::jlimit(0.0f, 0.92f, amount);
    const auto presence = shaped * 0.42f;
    const auto lowCoeff = juce::jlimit(0.01f, 0.35f, 1200.0f / static_cast<float>(currentSampleRate));
    const auto outputTrim = juce::Decibels::decibelsToGain(-1.2f * shaped);
    const auto channelCount = juce::jmin(buffer.getNumChannels(), static_cast<int>(goodizerLowState.size()));

    for (int channel = 0; channel < channelCount; ++channel)
    {
        auto* data = buffer.getWritePointer(channel);
        auto low = goodizerLowState[static_cast<size_t>(channel)];

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto dry = data[sample];
            low += lowCoeff * (dry - low);
            const auto high = dry - low;

            const auto driven = dry * drive + high * presence;
            const auto saturated = std::tanh(driven) / std::tanh(drive);
            const auto excited = saturated + high * presence;
            const auto mixed = dry + (excited - dry) * wet;

            data[sample] = std::tanh(mixed * outputTrim * 1.08f);
        }

        goodizerLowState[static_cast<size_t>(channel)] = low;
    }
}

void MixTeacherAudioProcessor::writeSampleToFifo(float sample) noexcept
{
    int start1 = 0;
    int size1 = 0;
    int start2 = 0;
    int size2 = 0;
    sampleFifo.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 > 0)
    {
        sampleFifoBuffer[static_cast<size_t>(start1)] = sample;
        sampleFifo.finishedWrite(1);
    }
}

void MixTeacherAudioProcessor::drainFifo()
{
    const auto ready = juce::jmin(sampleFifo.getNumReady(), fifoCapacity);
    if (ready <= 0)
        return;

    int start1 = 0;
    int size1 = 0;
    int start2 = 0;
    int size2 = 0;
    sampleFifo.prepareToRead(ready, start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i)
        appendToHistory(sampleFifoBuffer[static_cast<size_t>(start1 + i)]);

    for (int i = 0; i < size2; ++i)
        appendToHistory(sampleFifoBuffer[static_cast<size_t>(start2 + i)]);

    sampleFifo.finishedRead(size1 + size2);
}

void MixTeacherAudioProcessor::appendToHistory(float sample)
{
    if (analysisHistory.empty())
        return;

    analysisHistory[historyWriteIndex] = sample;
    historyWriteIndex = (historyWriteIndex + 1) % analysisHistory.size();
}

int MixTeacherAudioProcessor::findRecentActiveOffset(float threshold) const
{
    if (analysisHistory.empty())
        return -1;

    const auto availableSamples = static_cast<int>(juce::jmin(analysisHistory.size(),
                                                             static_cast<size_t>(juce::jmax(1.0, currentSampleRate * 3.0))));

    for (int offset = 0; offset < availableSamples; ++offset)
    {
        const auto index = (historyWriteIndex + analysisHistory.size() - 1 - static_cast<size_t>(offset)) % analysisHistory.size();
        if (std::abs(analysisHistory[index]) >= threshold)
            return offset;
    }

    return -1;
}

void MixTeacherAudioProcessor::updateHistoryLevels(mixteacher::AnalysisSnapshot& snapshot)
{
    const auto currentPeak = latestPeakLinear.load(std::memory_order_relaxed);
    const auto currentTruePeak = latestTruePeakLinear.load(std::memory_order_relaxed);
    const auto currentRms = latestRmsLinear.load(std::memory_order_relaxed);
    auto peak = currentPeak;
    auto rms = currentRms;

    const auto activeOffset = findRecentActiveOffset(1.0e-4f);
    if (!analysisHistory.empty() && activeOffset >= 0)
    {
        const auto windowSamples = juce::jmin(static_cast<int>(analysisHistory.size()),
                                             juce::jmax(256, static_cast<int>(currentSampleRate * 0.75)));
        double sumSquares = 0.0;
        float maxAbs = 0.0f;
        int usedSamples = 0;

        for (int i = 0; i < windowSamples; ++i)
        {
            const auto offset = activeOffset + i;
            if (offset >= static_cast<int>(analysisHistory.size()))
                break;

            const auto index = (historyWriteIndex + analysisHistory.size() - 1 - static_cast<size_t>(offset)) % analysisHistory.size();
            const auto value = analysisHistory[index];
            maxAbs = juce::jmax(maxAbs, std::abs(value));
            sumSquares += static_cast<double>(value) * static_cast<double>(value);
            ++usedSamples;
        }

        if (usedSamples > 0)
        {
            peak = juce::jmax(peak, maxAbs);
            rms = juce::jmax(rms, static_cast<float>(std::sqrt(sumSquares / static_cast<double>(usedSamples))));
        }
    }

    if (snapshot.activeRmsP50 > -119.0f && rms < 1.0e-5f)
        rms = juce::jmax(rms, juce::Decibels::decibelsToGain(snapshot.activeRmsP50 - 6.0f));

    snapshot.peakDbfs = linearToDb(peak);
    snapshot.truePeakDbfs = juce::jmax(snapshot.peakDbfs, linearToDb(juce::jmax(peak, currentTruePeak)));
    snapshot.rmsDbfs = linearToDb(rms);
    const auto lufsBase = snapshot.activeRmsP50 > -119.0f ? juce::jmax(snapshot.rmsDbfs, snapshot.activeRmsP50 - 6.0f)
                                                          : snapshot.rmsDbfs;
    snapshot.lufsShortTerm = lufsBase - 0.691f;
    snapshot.crestFactorDb = juce::jmax(0.0f, snapshot.peakDbfs - snapshot.rmsDbfs);
    snapshot.headroomDb = juce::jmax(0.0f, -snapshot.truePeakDbfs);
    snapshot.stereoCorrelation = latestStereoCorrelation.load(std::memory_order_relaxed);
    snapshot.stereoWidth = latestStereoWidth.load(std::memory_order_relaxed);
    snapshot.monoFoldDownLossDb = latestMonoFoldDownLossDb.load(std::memory_order_relaxed);
}

juce::String MixTeacherAudioProcessor::getChoiceText(const char* parameterID) const
{
    if (const auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter(parameterID)))
        return parameter->getCurrentChoiceName();

    return {};
}

bool MixTeacherAudioProcessor::isMixHubMode() const
{
    if (auto* value = parameters.getRawParameterValue("pluginRole"))
        return value->load() > 0.5f;

    return false;
}

int MixTeacherAudioProcessor::getSpectrumFftSize() const
{
    if (const auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter("spectrumFftSize")))
        return 1024 << juce::jlimit(0, 3, parameter->getIndex());

    return 4096;
}

int MixTeacherAudioProcessor::getSpectrumFftOrder() const
{
    switch (getSpectrumFftSize())
    {
        case 1024: return 10;
        case 2048: return 11;
        case 8192: return 13;
        case 4096:
        default: return 12;
    }
}

mixteacher::AnalysisSnapshot MixTeacherAudioProcessor::getLatestAnalysisSnapshot()
{
    drainFifo();

    mixteacher::AnalysisSnapshot snapshot;
    snapshot.track.manualType = normaliseTrackName(getChoiceText("trackType"));
    snapshot.explanationMode = getChoiceText("explanationMode").toLowerCase();
    snapshot.language = getChoiceText("language") == "English" ? "en" : "ru";
    snapshot.sensitivity = 0.5f;
    snapshot.spectrumFftSize = getSpectrumFftSize();
    if (auto* value = parameters.getRawParameterValue("goodizer"))
        snapshot.goodizerAmount = value->load();
    snapshot.analysisDurationSec = static_cast<double>(analysedSamples.load(std::memory_order_relaxed)) / juce::jmax(1.0, currentSampleRate);

    snapshot.clippingCount = clippingCount.load(std::memory_order_relaxed);
    snapshot.clippingDetected = snapshot.clippingCount > 0;

    if (!analysisHistory.empty())
    {
        for (int bin = 0; bin < mixteacher::waveformBins; ++bin)
        {
            const auto sourceIndex = (historyWriteIndex + analysisHistory.size()
                                      - static_cast<size_t>(mixteacher::waveformBins - bin) * analysisHistory.size()
                                            / mixteacher::waveformBins)
                                     % analysisHistory.size();
            snapshot.waveform[static_cast<size_t>(bin)] = analysisHistory[sourceIndex];
        }
    }

    updateDynamics(snapshot);
    updateHistoryLevels(snapshot);
    updateSpectrum(snapshot);
    updateValidity(snapshot);
    updateTrackDetection(snapshot);
    updateDrumProfile(snapshot);
    snapshot.issues = mixteacher::buildTeacherIssues(snapshot);
    snapshot.firstStep = mixteacher::buildFirstStep(snapshot);

    if (isMixHubMode())
        removePublishedTrack();
    else
        publishTrackSnapshot(snapshot);

    return snapshot;
}

void MixTeacherAudioProcessor::publishTrackSnapshot(const mixteacher::AnalysisSnapshot& snapshot) const
{
    const auto summary = makeHubTrackSummary(instanceId, snapshot);
    const juce::ScopedLock lock(hubRegistryLock);
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();

    for (auto& entry : hubRegistry)
    {
        if (entry.instanceId == instanceId)
        {
            entry.track = summary;
            entry.updatedMs = nowMs;
            return;
        }
    }

    hubRegistry.push_back({ instanceId, summary, nowMs });
}

void MixTeacherAudioProcessor::removePublishedTrack() const
{
    const juce::ScopedLock lock(hubRegistryLock);
    hubRegistry.erase(std::remove_if(hubRegistry.begin(),
                                     hubRegistry.end(),
                                     [id = instanceId](const auto& entry) { return entry.instanceId == id; }),
                      hubRegistry.end());
}

mixteacher::MixHubSnapshot MixTeacherAudioProcessor::getMixHubSnapshot() const
{
    mixteacher::MixHubSnapshot hub;
    hub.language = getChoiceText("language") == "English" ? "en" : "ru";
    const auto russian = hub.language == "ru";
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();

    std::vector<MixTeacherAudioProcessor*> peers;
    {
        const juce::ScopedLock lock(hubRegistryLock);
        peers = hubProcessors;
    }

    for (auto* processor : peers)
    {
        if (processor == nullptr || processor == this || processor->isMixHubMode())
            continue;

        auto peerSnapshot = processor->getLatestAnalysisSnapshot();
        hub.tracks.push_back(makeHubTrackSummary(processor->instanceId, peerSnapshot));
    }

    if (hub.tracks.empty())
    {
        const juce::ScopedLock lock(hubRegistryLock);
        hubRegistry.erase(std::remove_if(hubRegistry.begin(),
                                         hubRegistry.end(),
                                         [nowMs](const auto& entry) { return (nowMs - entry.updatedMs) > 12000.0; }),
                          hubRegistry.end());

        for (const auto& entry : hubRegistry)
        {
            if (entry.instanceId == instanceId)
                continue;

            auto track = entry.track;
            track.ageSec = (nowMs - entry.updatedMs) / 1000.0;
            hub.tracks.push_back(track);
        }
    }

    std::sort(hub.tracks.begin(), hub.tracks.end(), [](const auto& a, const auto& b) { return a.rmsDbfs > b.rmsDbfs; });
    hub.trackCount = static_cast<int>(hub.tracks.size());

    if (!hub.tracks.empty())
    {
        float lowPower = 0.0f;
        float lowMidPower = 0.0f;
        float midPower = 0.0f;
        float highPower = 0.0f;
        int tonalCount = 0;

        for (const auto& track : hub.tracks)
        {
            if (!track.validForAnalysis)
                continue;

            lowPower += dbToPower(track.bandDb.lowFoundation) + dbToPower(track.bandDb.lowBody);
            lowMidPower += dbToPower(track.bandDb.mud) + dbToPower(track.bandDb.boxiness);
            midPower += dbToPower(track.bandDb.tone) + dbToPower(track.bandDb.presence);
            highPower += dbToPower(track.bandDb.sibilance) + dbToPower(track.bandDb.air);
            ++tonalCount;
        }

        if (tonalCount > 0)
        {
            const auto normaliser = 1.0f / static_cast<float>(tonalCount * 2);
            hub.tonalLowDb = powerToDb(lowPower * normaliser);
            hub.tonalLowMidDb = powerToDb(lowMidPower * normaliser);
            hub.tonalMidDb = powerToDb(midPower * normaliser);
            hub.tonalHighDb = powerToDb(highPower * normaliser);
            hub.tonalTiltDb = hub.tonalHighDb - hub.tonalLowDb;
        }
    }

    if (hub.tracks.empty())
    {
        hub.issues.push_back({ mixteacher::Severity::info,
                               hubText(russian, "Mix Hub ждёт дорожки", "Mix Hub is waiting for tracks"),
                               hubText(russian,
                                       "Поставь Mix Teacher на отдельные дорожки в режиме Track, а этот экземпляр оставь на master в режиме Mix Hub.",
                                       "Insert Mix Teacher on individual tracks in Track mode, and keep this instance on the master in Mix Hub mode."),
                               hubText(russian,
                                       "Добавь хотя бы vocal, kick, bass или drums bus инстанс и запусти playback.",
                                       "Add at least a vocal, kick, bass, or drums bus instance and start playback.") });
    }

    for (const auto& track : hub.tracks)
    {
        if (track.clippingDetected || track.peakDbfs > -1.0f)
        {
            hub.issues.push_back({ mixteacher::Severity::critical,
                                   hubText(russian, "В одной из дорожек почти клиппинг", "One track is almost clipping"),
                                   track.effectiveType + " peak " + juce::String(track.peakDbfs, 1) + " dBFS",
                                   hubText(russian,
                                           "Сначала убери перегруз на этой дорожке: clip/input gain вниз на 3-6 dB.",
                                           "Fix overload first: lower clip/input gain on that track by 3-6 dB.") });
            break;
        }
    }

    for (const auto& track : hub.tracks)
    {
        if (track.stereoCorrelation < -0.15f || track.monoFoldDownLossDb < -4.0f)
        {
            hub.issues.push_back({ mixteacher::Severity::warning,
                                   hubText(russian, "Mono compatibility: ", "Mono compatibility: ") + trackName(track),
                                   juce::String("corr ") + juce::String(track.stereoCorrelation, 2)
                                       + " / mono " + juce::String(track.monoFoldDownLossDb, 1) + " dB",
                                   hubText(russian,
                                           "Проверь эту дорожку в mono. Если исчезает центр или низ, сузь stereo widening/chorus/reverb return.",
                                           "Check this track in mono. If center or low end disappears, narrow stereo widening/chorus/reverb return.") });
            break;
        }
    }

    const mixteacher::HubTrackSummary* vocal = nullptr;
    const mixteacher::HubTrackSummary* kick = nullptr;
    const mixteacher::HubTrackSummary* bass = nullptr;
    const mixteacher::HubTrackSummary* drums = nullptr;

    for (const auto& track : hub.tracks)
    {
        if (vocal == nullptr && isType(track, "vocal"))
            vocal = &track;
        if (kick == nullptr && isType(track, "kick"))
            kick = &track;
        if (bass == nullptr && isType(track, "bass"))
            bass = &track;
        if (drums == nullptr && (isType(track, "drums") || isType(track, "drums_bus")))
            drums = &track;
    }

    auto severityWeight = [](mixteacher::Severity severity)
    {
        switch (severity)
        {
            case mixteacher::Severity::critical: return 4;
            case mixteacher::Severity::problem: return 3;
            case mixteacher::Severity::warning: return 2;
            case mixteacher::Severity::info: return 1;
            case mixteacher::Severity::ok: return 0;
        }

        return 0;
    };

    auto addFrequencyConflict = [&](const mixteacher::HubTrackSummary& a,
                                    const mixteacher::HubTrackSummary& b,
                                    int band,
                                    float strength,
                                    mixteacher::Severity severity) -> bool
    {
        if (a.instanceId == b.instanceId || band < 0)
            return false;

        const auto clippedStrength = juce::jlimit(0.06f, 1.0f, strength);

        for (auto& existing : hub.frequencyConflicts)
        {
            const auto samePair = (existing.trackAId == a.instanceId && existing.trackBId == b.instanceId)
                                  || (existing.trackAId == b.instanceId && existing.trackBId == a.instanceId);

            if (samePair && existing.bandIndex == band)
            {
                existing.strength = juce::jmax(existing.strength, clippedStrength);
                if (severityWeight(severity) > severityWeight(existing.severity))
                    existing.severity = severity;

                return false;
            }
        }

        hub.frequencyConflicts.push_back({ a.instanceId,
                                            b.instanceId,
                                            trackName(a),
                                            trackName(b),
                                            bandLabel(russian, band),
                                            band,
                                            clippedStrength,
                                            severity });
        return true;
    };

    auto isLowDrumSource = [](const mixteacher::HubTrackSummary& track)
    {
        return isType(track, "kick") || isType(track, "drums") || isType(track, "drums_bus");
    };

    auto pairHasLowDrumAndBass = [&](const mixteacher::HubTrackSummary& a, const mixteacher::HubTrackSummary& b)
    {
        return (isLowDrumSource(a) && isType(b, "bass")) || (isLowDrumSource(b) && isType(a, "bass"));
    };

    if (vocal != nullptr && hub.tracks.size() > 1)
    {
        float otherSum = 0.0f;
        int otherCount = 0;
        for (const auto& track : hub.tracks)
        {
            if (&track == vocal || !track.validForAnalysis)
                continue;

            otherSum += track.rmsDbfs;
            ++otherCount;
        }

        if (otherCount > 0)
        {
            const auto otherAverage = otherSum / static_cast<float>(otherCount);
            const auto vocalDelta = vocal->rmsDbfs - otherAverage;

            if (vocalDelta > 5.0f)
                hub.issues.push_back({ mixteacher::Severity::warning,
                                       hubText(russian, "Вокал может быть слишком впереди", "Vocal may be too far forward"),
                                       hubText(russian, "Vocal RMS выше среднего остальных дорожек примерно на ", "Vocal RMS is above the other tracks by about ")
                                           + juce::String(vocalDelta, 1) + " dB",
                                       hubText(russian,
                                               "Попробуй убавить vocal bus на 1-3 dB и сравнить в припеве.",
                                               "Try lowering the vocal bus by 1-3 dB and compare in the chorus.") });
            else if (vocalDelta < -6.0f)
                hub.issues.push_back({ mixteacher::Severity::info,
                                       hubText(russian, "Вокал может теряться", "Vocal may be buried"),
                                       hubText(russian, "Vocal RMS ниже среднего остальных дорожек примерно на ", "Vocal RMS is below the other tracks by about ")
                                           + juce::String(std::abs(vocalDelta), 1) + " dB",
                                       hubText(russian,
                                               "Не поднимай сразу громкость: сначала проверь конкуренцию 2-5 kHz и компрессию.",
                                               "Do not raise volume immediately: first check 2-5 kHz masking and compression.") });
        }
    }

    const auto* lowDrumSource = kick != nullptr ? kick : drums;
    if (lowDrumSource != nullptr && bass != nullptr)
    {
        auto lowTiming = analyseLowEndTiming(*lowDrumSource, *bass);
        if (lowTiming.available)
            hub.lowEndTiming = lowTiming;

        int bestBand = -1;
        float bestStrength = 0.0f;
        float bestDrumDb = -120.0f;
        float bestBassDb = -120.0f;
        mixteacher::Severity bestSeverity = mixteacher::Severity::info;

        for (int band = 0; band <= 1; ++band)
        {
            const auto drumDb = bandValueDb(*lowDrumSource, band);
            const auto bassDb = bandValueDb(*bass, band);
            const auto weakerDb = juce::jmin(drumDb, bassDb);

            if (weakerDb < -74.0f)
                continue;

            const auto deltaDb = std::abs(drumDb - bassDb);
            const auto audibility = juce::jlimit(0.0f, 1.0f, (weakerDb + 74.0f) / 30.0f);
            const auto closeness = juce::jlimit(0.0f, 1.0f, 1.0f - deltaDb / 22.0f);
            const auto strength = juce::jlimit(0.08f, 1.0f, audibility * 0.68f + closeness * 0.32f);

            auto severity = mixteacher::Severity::info;
            if (weakerDb > -55.0f && deltaDb < 8.0f)
                severity = mixteacher::Severity::problem;
            else if (weakerDb > -64.0f && deltaDb < 14.0f)
                severity = mixteacher::Severity::warning;

            addFrequencyConflict(*lowDrumSource, *bass, band, strength, severity);

            if (strength > bestStrength)
            {
                bestBand = band;
                bestStrength = strength;
                bestDrumDb = drumDb;
                bestBassDb = bassDb;
                bestSeverity = severity;
            }
        }

        if (lowTiming.available && lowTiming.risk > 0.24f)
        {
            if (bestBand < 0)
                bestBand = juce::jmax(bandValueDb(*lowDrumSource, 0), bandValueDb(*bass, 0))
                               > juce::jmax(bandValueDb(*lowDrumSource, 1), bandValueDb(*bass, 1))
                           ? 0
                           : 1;

            addFrequencyConflict(*lowDrumSource, *bass, bestBand, lowTiming.risk, lowTiming.severity);

            const auto title = lowTiming.severity == mixteacher::Severity::info
                                   ? hubText(russian, "Низ Kick/Bass потенциально совпадает по времени", "Kick/Bass low end may overlap in time")
                                   : hubText(russian, "Низ Kick/Bass совпадает по времени", "Kick/Bass low end overlaps in time");
            const auto detail = hubText(russian, "Одновременно активны примерно ", "Simultaneously active in about ")
                                + juce::String(lowTiming.simultaneousRatio * 100.0f, 0)
                                + "% "
                                + hubText(russian, "низовых окон. Риск ", "of low-end windows. Risk ")
                                + juce::String(lowTiming.risk * 100.0f, 0) + "%.";

            hub.issues.push_back({ lowTiming.severity,
                                   title,
                                   detail,
                                   hubText(russian,
                                           "Для DnB/808 проверь groove в контексте. Если низ мажется, начни с короткого sidechain или dynamic EQ 1-3 dB ниже 120 Hz, а не с большого EQ-cut.",
                                           "For DnB/808, check the groove in context. If the low end smears, start with short sidechain or 1-3 dB dynamic EQ below 120 Hz, not a large static EQ cut.") });
        }
        else if (bestBand >= 0)
        {
            const auto title = bestSeverity == mixteacher::Severity::info
                                   ? hubText(russian, "Потенциальное пересечение Kick/Bass", "Potential Kick/Bass overlap")
                                   : hubText(russian, "Kick и Bass пересекаются внизу", "Kick and Bass overlap in the low end");
            const auto detail = bandLabel(russian, bestBand) + ": " + trackName(*lowDrumSource) + " "
                                + juce::String(bestDrumDb, 1) + " dB / " + trackName(*bass) + " "
                                + juce::String(bestBassDb, 1) + " dB";

            hub.issues.push_back({ bestSeverity,
                                   title,
                                   detail,
                                   hubText(russian,
                                           "Проверь в контексте: выбери, кто держит фундамент. Если низ мажется, попробуй sidechain или dynamic EQ 1-3 dB в этой зоне.",
                                           "Check in context: decide who owns the foundation. If the low end smears, try sidechain or 1-3 dB dynamic EQ in this range.") });
        }
    }

    if (drums != nullptr && vocal != nullptr && drums->bandDb.sibilance > vocal->bandDb.presence + 4.0f)
    {
        hub.issues.push_back({ mixteacher::Severity::warning,
                               hubText(russian, "Хэты могут мешать вокалу", "Hats may mask the vocal"),
                               hubText(russian, "Drums 5-9 kHz заметно выше vocal presence 2-5 kHz.", "Drums 5-9 kHz is clearly above vocal presence 2-5 kHz."),
                               hubText(russian,
                                       "Попробуй dynamic EQ на hats/overheads 6-9 kHz или чуть прибери верх drum bus.",
                                       "Try dynamic EQ on hats/overheads at 6-9 kHz or reduce drum bus top slightly.") });
    }

    int strongLowTracks = 0;
    for (const auto& track : hub.tracks)
        if (maxLowDb(track) > -45.0f)
            ++strongLowTracks;

    if (strongLowTracks >= 3)
    {
        hub.issues.push_back({ mixteacher::Severity::info,
                               hubText(russian, "Низ может быть перегружен несколькими дорожками", "Low end may be crowded by several tracks"),
                               juce::String(strongLowTracks) + hubText(russian, " дорожки активны ниже 150 Hz.", " tracks are active below 150 Hz."),
                               hubText(russian,
                                       "Проверь, кто реально должен занимать саб/низ: kick, bass или toms. Остальным можно дать high-pass.",
                                       "Decide who owns sub/low end: kick, bass, or toms. Other tracks may need high-pass filtering.") });
    }

    if (hub.tracks.size() >= 2)
    {
        int bandConflictCount = 0;
        for (int band = 0; band < 8 && bandConflictCount < 5; ++band)
        {
            const auto floor = band <= 1 ? -74.0f : (band >= 6 ? -70.0f : -72.0f);
            const auto relativeWindow = band <= 1 ? 20.0f : (band >= 6 ? 14.0f : 16.0f);
            float maxBand = -120.0f;
            for (const auto& track : hub.tracks)
                if (track.validForAnalysis)
                    maxBand = juce::jmax(maxBand, bandValueDb(track, band));

            if (maxBand < floor)
                continue;

            std::vector<const mixteacher::HubTrackSummary*> contenders;
            for (const auto& track : hub.tracks)
            {
                const auto value = bandValueDb(track, band);
                if (track.validForAnalysis && value > floor && value > maxBand - relativeWindow)
                    contenders.push_back(&track);
            }

            if (contenders.size() < 2)
                continue;

            std::sort(contenders.begin(), contenders.end(), [band](const auto* a, const auto* b) {
                return bandValueDb(*a, band) > bandValueDb(*b, band);
            });

            const auto topDb = bandValueDb(*contenders[0], band);
            const auto secondDb = bandValueDb(*contenders[1], band);
            const auto weakerDb = juce::jmin(topDb, secondDb);
            const auto deltaDb = std::abs(topDb - secondDb);
            const auto audibility = juce::jlimit(0.0f, 1.0f, (weakerDb - floor) / 30.0f);
            const auto closeness = juce::jlimit(0.0f, 1.0f, 1.0f - deltaDb / relativeWindow);
            const auto strength = juce::jlimit(0.08f, 1.0f, audibility * 0.62f + closeness * 0.38f);
            const auto lowDrumBassPair = pairHasLowDrumAndBass(*contenders[0], *contenders[1]);

            auto severity = mixteacher::Severity::info;
            if (lowDrumBassPair && band <= 1 && strength > 0.42f)
                severity = strength > 0.64f ? mixteacher::Severity::problem : mixteacher::Severity::warning;
            else if ((band == 2 || band == 5 || band == 6) && strength > 0.56f)
                severity = mixteacher::Severity::warning;
            else if (strength > 0.70f)
                severity = mixteacher::Severity::warning;

            const auto addedNewConflict = addFrequencyConflict(*contenders[0], *contenders[1], band, strength, severity);
            if (!addedNewConflict)
                continue;

            const auto title = severity == mixteacher::Severity::info
                                   ? hubText(russian, "Потенциальное пересечение: ", "Potential overlap: ") + bandLabel(russian, band)
                                   : hubText(russian, "Частотный конфликт: ", "Frequency conflict: ") + bandLabel(russian, band);
            const auto detail = trackName(*contenders[0]) + " / " + trackName(*contenders[1])
                                + hubText(russian, " одновременно активны здесь: ", " are both active here: ")
                                + juce::String(topDb, 1) + " / " + juce::String(secondDb, 1) + " dB.";

            hub.issues.push_back({ severity, title, detail, bandAction(russian, band) });
            ++bandConflictCount;
        }
    }

    if (vocal != nullptr)
    {
        const mixteacher::HubTrackSummary* masker = nullptr;
        float maskerPresence = -120.0f;

        for (const auto& track : hub.tracks)
        {
            if (&track == vocal || !track.validForAnalysis)
                continue;

            const auto presence = track.bandDb.presence;
            const auto closeEnoughInLevel = track.rmsDbfs > vocal->rmsDbfs - 8.0f;
            if (presence > vocal->bandDb.presence - 2.0f && closeEnoughInLevel && presence > maskerPresence)
            {
                maskerPresence = presence;
                masker = &track;
            }
        }

        if (masker != nullptr)
        {
            hub.issues.push_back({ mixteacher::Severity::warning,
                                   hubText(russian, "Вокал может маскироваться в 2-5 kHz", "Vocal may be masked around 2-5 kHz"),
                                   trackName(*masker) + hubText(russian, " конкурирует с vocal presence.", " competes with vocal presence."),
                                   hubText(russian,
                                           "Если текст плохо читается, не поднимай вокал сразу. Попробуй dynamic EQ/cut 1-3 dB на мешающей дорожке в 2-5 kHz.",
                                           "If lyrics are unclear, do not raise vocal immediately. Try dynamic EQ/cut 1-3 dB on the masking track around 2-5 kHz.") });
        }
    }

    if (hub.tracks.size() >= 2 && hub.tonalMidDb > -119.0f)
    {
            if (hub.tonalLowMidDb > hub.tonalMidDb + 7.0f)
            {
                hub.issues.push_back({ mixteacher::Severity::warning,
                                       hubText(russian, "Общая картина: микс может быть мутным", "Big picture: the mix may be muddy"),
                                       hubText(russian, "Нижняя середина 150-800 Hz заметно сильнее зоны читаемости 2-5 kHz.", "Low-mid 150-800 Hz is clearly stronger than readability 2-5 kHz."),
                                       hubText(russian,
                                               "Начни не с master EQ. Найди 1-2 второстепенные дорожки с лишней мутью и убери 180-400 Hz на 1-3 dB.",
                                               "Do not start with master EQ. Find 1-2 supporting tracks with excess mud and cut 180-400 Hz by 1-3 dB.") });
            }
            else if (hub.tonalHighDb > hub.tonalLowMidDb + 8.0f)
            {
                hub.issues.push_back({ mixteacher::Severity::warning,
                                       hubText(russian, "Общая картина: верх может быть резким", "Big picture: the top may be harsh"),
                                       hubText(russian, "5-16 kHz в среднем сильно активнее нижней середины.", "5-16 kHz is much more active on average than low-mids."),
                                       hubText(russian,
                                               "Проверь hats, cymbals, bright synths и sibilance. Лучше dynamic EQ на источнике, чем глушить весь master.",
                                               "Check hats, cymbals, bright synths, and sibilance. Prefer source dynamic EQ over dulling the whole master.") });
            }
            else if (hub.tonalLowDb > hub.tonalMidDb + 9.0f)
            {
                hub.issues.push_back({ mixteacher::Severity::info,
                                       hubText(russian, "Общая картина: низ доминирует", "Big picture: low end dominates"),
                                       hubText(russian, "40-150 Hz заметно сильнее зоны атаки/читаемости.", "40-150 Hz is clearly stronger than attack/readability."),
                                       hubText(russian,
                                               "Проверь баланс kick/bass на тихой громкости. Если ритм пропадает, добавь читаемость 2-5 kHz на кике или освободи низ.",
                                               "Check kick/bass balance quietly. If groove disappears, add 2-5 kHz readability to kick or clear low-end space.") });
            }
    }

    if (hub.tracks.size() >= 3)
    {
        const auto topDelta = hub.tracks[0].rmsDbfs - hub.tracks[1].rmsDbfs;
        if (topDelta < 2.0f)
        {
            hub.issues.push_back({ mixteacher::Severity::info,
                                   hubText(russian, "В миксе может не быть явного фокуса", "The mix may lack a clear focal point"),
                                   trackName(hub.tracks[0]) + " / " + trackName(hub.tracks[1])
                                       + hubText(russian, " почти одинаковы по RMS.", " are almost equal in RMS."),
                                   hubText(russian,
                                           "Выбери главный элемент секции. Поддерживающие дорожки попробуй опустить на 1-3 dB или освободить им другие частоты.",
                                           "Choose the section's main element. Try lowering support tracks by 1-3 dB or move them into different frequency space.") });
        }
    }

    std::sort(hub.frequencyConflicts.begin(), hub.frequencyConflicts.end(), [&](const auto& a, const auto& b) {
        const auto severityA = severityWeight(a.severity);
        const auto severityB = severityWeight(b.severity);
        if (severityA != severityB)
            return severityA > severityB;

        return a.strength > b.strength;
    });

    if (hub.issues.size() > 6)
        hub.issues.resize(6);

    if (hub.issues.empty())
    {
        hub.issues.push_back({ mixteacher::Severity::ok,
                               hubText(russian, "Баланс выглядит спокойно", "Balance looks calm"),
                               hubText(russian,
                                       "Явного конфликта между опубликованными дорожками нет.",
                                       "No obvious conflict between published tracks."),
                               hubText(russian,
                                       "Проверь баланс на тихом мониторинге и сравни припев с референсом.",
                                       "Check balance at low monitoring volume and compare the chorus with a reference.") });
    }

    hub.firstStepTitle = hub.issues.front().title;
    hub.firstStepAction = hub.issues.front().action;

    return hub;
}

void MixTeacherAudioProcessor::updateSpectrum(mixteacher::AnalysisSnapshot& snapshot)
{
    const auto fftOrder = getSpectrumFftOrder();
    const auto fftSize = 1 << fftOrder;

    if (analysisHistory.size() < static_cast<size_t>(fftSize))
        return;

    std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);

    const auto activeOffset = juce::jmax(0, findRecentActiveOffset(1.0e-4f));
    for (int i = 0; i < fftSize; ++i)
    {
        const auto offset = activeOffset + (fftSize - 1 - i);
        const auto sourceIndex = (historyWriteIndex + analysisHistory.size() - 1 - static_cast<size_t>(offset))
                                 % analysisHistory.size();
        fftBuffer[static_cast<size_t>(i)] = analysisHistory[sourceIndex];
    }

    switch (fftSize)
    {
        case 1024:
            window1024.multiplyWithWindowingTable(fftBuffer.data(), fftSize);
            fft1024.performFrequencyOnlyForwardTransform(fftBuffer.data());
            break;
        case 2048:
            window2048.multiplyWithWindowingTable(fftBuffer.data(), fftSize);
            fft2048.performFrequencyOnlyForwardTransform(fftBuffer.data());
            break;
        case 8192:
            window8192.multiplyWithWindowingTable(fftBuffer.data(), fftSize);
            fft8192.performFrequencyOnlyForwardTransform(fftBuffer.data());
            break;
        case 4096:
        default:
            window4096.multiplyWithWindowingTable(fftBuffer.data(), fftSize);
            fft4096.performFrequencyOnlyForwardTransform(fftBuffer.data());
            break;
    }

    auto bandEnergyDb = [this, fftSize](float lowHz, float highHz)
    {
        const auto lowBin = juce::jlimit(1, fftSize / 2, static_cast<int>(lowHz * fftSize / currentSampleRate));
        const auto highBin = juce::jlimit(lowBin + 1, fftSize / 2, static_cast<int>(highHz * fftSize / currentSampleRate));

        float sum = 0.0f;
        for (int bin = lowBin; bin < highBin; ++bin)
            sum += fftBuffer[static_cast<size_t>(bin)];

        const auto average = sum / static_cast<float>(juce::jmax(1, highBin - lowBin));
        return linearToDb(average);
    };

    auto bandPeakDb = [this, fftSize](float lowHz, float highHz)
    {
        const auto lowBin = juce::jlimit(1, fftSize / 2, static_cast<int>(lowHz * fftSize / currentSampleRate));
        const auto highBin = juce::jlimit(lowBin + 1, fftSize / 2, static_cast<int>(highHz * fftSize / currentSampleRate));

        float peak = 0.0f;
        for (int bin = lowBin; bin < highBin; ++bin)
            peak = juce::jmax(peak, fftBuffer[static_cast<size_t>(bin)]);

        return linearToDb(peak);
    };

    for (int bin = 0; bin < mixteacher::spectrumBins; ++bin)
    {
        const auto fftBin = juce::jlimit(1, fftSize / 2 - 1, 1 + bin * ((fftSize / 2 - 1) / mixteacher::spectrumBins));
        snapshot.spectrum[static_cast<size_t>(bin)] = normaliseDb(linearToDb(fftBuffer[static_cast<size_t>(fftBin)]), -84.0f, -18.0f);
    }

    snapshot.bandDb.subRumble = bandEnergyDb(20.0f, 40.0f);
    snapshot.bandDb.lowFoundation = bandEnergyDb(40.0f, 80.0f);
    snapshot.bandDb.lowBody = bandEnergyDb(80.0f, 150.0f);
    snapshot.bandDb.mud = juce::jmax(bandEnergyDb(150.0f, 350.0f), bandEnergyDb(180.0f, 350.0f));
    snapshot.bandDb.boxiness = bandEnergyDb(350.0f, 800.0f);
    snapshot.bandDb.tone = bandEnergyDb(800.0f, 2000.0f);
    snapshot.bandDb.presence = bandEnergyDb(2000.0f, 5000.0f);
    snapshot.bandDb.sibilance = bandEnergyDb(5000.0f, 9000.0f);
    snapshot.bandDb.air = bandEnergyDb(9000.0f, 16000.0f);
    snapshot.sibilancePeakDb = bandPeakDb(5000.0f, 9000.0f);
    const auto sibilanceReference = juce::jmax(snapshot.bandDb.presence, snapshot.bandDb.tone, snapshot.bandDb.air);
    snapshot.sibilanceSpikeScore = juce::jlimit(0.0f, 1.0f, (snapshot.sibilancePeakDb - sibilanceReference - 3.0f) / 14.0f);

    const auto lowMidAverage = (snapshot.bandDb.mud + snapshot.bandDb.boxiness + snapshot.bandDb.tone) / 3.0f;
    const auto highAverage = (snapshot.bandDb.presence + snapshot.bandDb.sibilance + snapshot.bandDb.air) / 3.0f;
    const auto kLikeCorrection = juce::jlimit(-1.5f, 1.8f, (highAverage - lowMidAverage) / 18.0f);
    if (snapshot.lufsShortTerm > -119.0f)
        snapshot.lufsShortTerm += kLikeCorrection;

    snapshot.bands.subRumble = normaliseDb(snapshot.bandDb.subRumble, -84.0f, -18.0f);
    snapshot.bands.lowFoundation = normaliseDb(snapshot.bandDb.lowFoundation, -84.0f, -18.0f);
    snapshot.bands.lowBody = normaliseDb(snapshot.bandDb.lowBody, -84.0f, -18.0f);
    snapshot.bands.mud = normaliseDb(snapshot.bandDb.mud, -84.0f, -18.0f);
    snapshot.bands.boxiness = normaliseDb(snapshot.bandDb.boxiness, -84.0f, -18.0f);
    snapshot.bands.tone = normaliseDb(snapshot.bandDb.tone, -84.0f, -18.0f);
    snapshot.bands.presence = normaliseDb(snapshot.bandDb.presence, -84.0f, -18.0f);
    snapshot.bands.sibilance = normaliseDb(snapshot.bandDb.sibilance, -84.0f, -18.0f);
    snapshot.bands.air = normaliseDb(snapshot.bandDb.air, -84.0f, -18.0f);

    std::vector<std::pair<juce::String, float>> bands {
        { "20_40_hz", snapshot.bandDb.subRumble },
        { "40_80_hz", snapshot.bandDb.lowFoundation },
        { "80_150_hz", snapshot.bandDb.lowBody },
        { "150_350_hz", snapshot.bandDb.mud },
        { "350_800_hz", snapshot.bandDb.boxiness },
        { "800_2khz", snapshot.bandDb.tone },
        { "2_5khz", snapshot.bandDb.presence },
        { "5_9khz", snapshot.bandDb.sibilance },
        { "9_16khz", snapshot.bandDb.air },
    };

    float average = 0.0f;
    for (const auto& band : bands)
        average += band.second;
    average /= static_cast<float>(bands.size());

    std::sort(bands.begin(), bands.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

    for (const auto& band : bands)
    {
        if (band.second > average + 3.0f && snapshot.dominantBands.size() < 3)
            snapshot.dominantBands.push_back(band.first);
        if (band.second < average - 8.0f && snapshot.weakBands.size() < 3)
            snapshot.weakBands.push_back(band.first);
    }
}

void MixTeacherAudioProcessor::updateDynamics(mixteacher::AnalysisSnapshot& snapshot)
{
    if (analysisHistory.empty())
        return;

    const auto availableSamples = static_cast<int>(juce::jmin(analysisHistory.size(),
                                                             static_cast<size_t>(juce::jmax(1.0, currentSampleRate * 3.0))));
    const auto samplesPerBin = juce::jmax(8, availableSamples / mixteacher::dynamicsBins);
    std::vector<float> allRmsValues;
    std::vector<float> activeRmsValues;
    allRmsValues.reserve(mixteacher::dynamicsBins);

    for (int bin = 0; bin < mixteacher::dynamicsBins; ++bin)
    {
        double sumSquares = 0.0;
        double lowSumSquares = 0.0;
        float lowState = 0.0f;
        const auto newestOffset = (mixteacher::dynamicsBins - 1 - bin) * samplesPerBin;
        const auto lowPassAlpha = static_cast<float>(std::exp(-juce::MathConstants<double>::twoPi * 155.0 / juce::jmax(1.0, currentSampleRate)));

        for (int i = 0; i < samplesPerBin; ++i)
        {
            const auto offset = static_cast<size_t>(newestOffset + i);
            const auto index = (historyWriteIndex + analysisHistory.size() - 1 - offset) % analysisHistory.size();
            const auto value = analysisHistory[index];
            sumSquares += static_cast<double>(value) * static_cast<double>(value);
        }

        for (int i = samplesPerBin - 1; i >= 0; --i)
        {
            const auto offset = static_cast<size_t>(newestOffset + i);
            const auto index = (historyWriteIndex + analysisHistory.size() - 1 - offset) % analysisHistory.size();
            const auto value = analysisHistory[index];
            lowState = lowPassAlpha * lowState + (1.0f - lowPassAlpha) * value;
            lowSumSquares += static_cast<double>(lowState) * static_cast<double>(lowState);
        }

        const auto rms = std::sqrt(sumSquares / static_cast<double>(samplesPerBin));
        const auto db = linearToDb(static_cast<float>(rms));
        const auto lowRms = std::sqrt(lowSumSquares / static_cast<double>(samplesPerBin));
        const auto lowDb = linearToDb(static_cast<float>(lowRms));
        allRmsValues.push_back(db);
        snapshot.dynamics[static_cast<size_t>(bin)] = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
        snapshot.lowEndCurve[static_cast<size_t>(bin)] = normaliseDb(lowDb, -78.0f, -20.0f);
    }

    snapshot.noiseFloorDb = percentile(allRmsValues, 0.20f);
    const auto p95 = percentile(allRmsValues, 0.95f);
    const auto activeThreshold = juce::jmax(-58.0f, snapshot.noiseFloorDb + 8.0f, p95 - 32.0f);

    for (const auto db : allRmsValues)
    {
        if (db >= activeThreshold)
            activeRmsValues.push_back(db);
    }

    snapshot.activeRmsP10 = percentile(activeRmsValues, 0.10f);
    snapshot.activeRmsP50 = percentile(activeRmsValues, 0.50f);
    snapshot.activeRmsP90 = percentile(activeRmsValues, 0.90f);
    snapshot.rmsRangeDb = juce::jmax(0.0f, snapshot.activeRmsP90 - snapshot.activeRmsP10);
    snapshot.activeWindowRatio = allRmsValues.empty() ? 0.0f : static_cast<float>(activeRmsValues.size()) / static_cast<float>(allRmsValues.size());

    const auto transientSamples = juce::jmin(availableSamples, static_cast<int>(currentSampleRate * 1.5));
    double diffSum = 0.0;
    double ampSum = 0.0;
    double attackSum = 0.0;
    double absSum = 0.0;

    for (int i = 0; i < transientSamples; ++i)
    {
        const auto index = (historyWriteIndex + analysisHistory.size() - 1 - static_cast<size_t>(i)) % analysisHistory.size();
        absSum += std::abs(analysisHistory[index]);
    }

    const auto meanAbs = transientSamples > 0 ? static_cast<float>(absSum / static_cast<double>(transientSamples)) : 0.0f;
    const auto attackThreshold = juce::jmax(0.018f, meanAbs * 1.4f);
    snapshot.onsetCount = 0;

    for (int i = 1; i < transientSamples; ++i)
    {
        const auto indexA = (historyWriteIndex + analysisHistory.size() - static_cast<size_t>(i)) % analysisHistory.size();
        const auto indexB = (historyWriteIndex + analysisHistory.size() - static_cast<size_t>(i + 1)) % analysisHistory.size();
        const auto a = analysisHistory[indexA];
        const auto b = analysisHistory[indexB];
        const auto absA = std::abs(a);
        const auto absB = std::abs(b);
        const auto rise = absA - absB;
        diffSum += std::abs(a - b);
        ampSum += absA;

        if (rise > attackThreshold && absA > meanAbs * 2.0f)
        {
            ++snapshot.onsetCount;
            attackSum += rise;
        }
    }

    if (transientSamples > 1 && ampSum > 1.0e-6)
    {
        const auto diffScore = static_cast<float>((diffSum / ampSum) * 0.45);
        const auto density = static_cast<float>(snapshot.onsetCount) / juce::jmax(1.0f, static_cast<float>(transientSamples) / static_cast<float>(currentSampleRate));
        const auto densityScore = juce::jlimit(0.0f, 1.0f, density / 18.0f);
        const auto attackScore = juce::jlimit(0.0f, 1.0f, static_cast<float>(attackSum / ampSum) * 2.5f);
        snapshot.transientDensity = densityScore;
        snapshot.transientScore = juce::jlimit(0.0f, 1.0f, diffScore + densityScore * 0.35f + attackScore * 0.35f);
    }
}

void MixTeacherAudioProcessor::updateValidity(mixteacher::AnalysisSnapshot& snapshot)
{
    const auto russian = snapshot.language == "ru";
    const auto usefulRmsDb = snapshot.activeRmsP50 > -119.0f ? snapshot.activeRmsP50 : snapshot.rmsDbfs;
    const auto tooQuietPeak = -35.0f;
    const auto tooQuietRms = -45.0f;
    const auto quietPeak = -18.0f;
    const auto quietRms = -30.0f;

    if (snapshot.peakDbfs < tooQuietPeak || usefulRmsDb < tooQuietRms)
    {
        snapshot.validity.state = mixteacher::ValidityState::tooQuiet;
        snapshot.validity.isValidForAnalysis = false;
        snapshot.validity.confidence = 0.25f;
        snapshot.validity.warnings.push_back(russian ? juce::String::fromUTF8("Сигнал слишком тихий для частотных выводов.")
                                                      : juce::String("Signal is too quiet for frequency conclusions."));
        return;
    }

    if (snapshot.peakDbfs < quietPeak && usefulRmsDb < quietRms)
    {
        snapshot.validity.state = mixteacher::ValidityState::quietButUsable;
        snapshot.validity.isValidForAnalysis = false;
        snapshot.validity.confidence = 0.48f;
        snapshot.validity.warnings.push_back(russian ? juce::String::fromUTF8("Дорожка очень тихая: советы по частотам могут быть неточными.")
                                                      : juce::String("Track is very quiet: frequency suggestions may be inaccurate."));
        return;
    }

    snapshot.validity.state = mixteacher::ValidityState::good;
    snapshot.validity.isValidForAnalysis = true;
    const auto activeConfidence = juce::jlimit(0.0f, 1.0f, snapshot.activeWindowRatio / 0.35f);
    snapshot.validity.confidence = snapshot.analysisDurationSec > 2.0 ? 0.62f + activeConfidence * 0.28f : 0.52f + activeConfidence * 0.2f;

    if (snapshot.activeWindowRatio < 0.08f)
    {
        snapshot.validity.confidence = juce::jmin(snapshot.validity.confidence, 0.62f);
        snapshot.validity.warnings.push_back(russian ? juce::String::fromUTF8("Активных окон мало: выводы лучше проверять на более плотном участке.")
                                                      : juce::String("Few active windows: verify conclusions on a denser section."));
    }
}

void MixTeacherAudioProcessor::updateTrackDetection(mixteacher::AnalysisSnapshot& snapshot)
{
    const auto low = juce::jmax(snapshot.bandDb.lowFoundation, snapshot.bandDb.lowBody);
    const auto mids = juce::jmax(snapshot.bandDb.tone, snapshot.bandDb.presence);
    const auto top = juce::jmax(snapshot.bandDb.sibilance, snapshot.bandDb.air);
    const auto sub = snapshot.bandDb.subRumble;

    struct Candidate
    {
        juce::String type;
        float score;
    };

    std::vector<Candidate> candidates;
    candidates.push_back({ "kick", juce::jlimit(0.0f, 1.0f, 0.45f + (snapshot.bandDb.lowFoundation - snapshot.bandDb.tone) / 22.0f + snapshot.transientScore * 0.25f) });
    candidates.push_back({ "bass", juce::jlimit(0.0f, 1.0f, 0.45f + (low - top) / 24.0f - snapshot.transientScore * 0.18f) });
    candidates.push_back({ "vocal", juce::jlimit(0.0f, 1.0f, 0.45f + (mids - sub) / 26.0f + (snapshot.rmsRangeDb > 4.0f ? 0.12f : 0.0f)) });
    candidates.push_back({ "drums_bus", juce::jlimit(0.0f, 1.0f, 0.38f + snapshot.transientScore * 0.42f + (top > low - 8.0f ? 0.12f : 0.0f)) });
    candidates.push_back({ "guitar", juce::jlimit(0.0f, 1.0f, 0.42f + (snapshot.bandDb.presence - sub) / 30.0f - (low > mids ? 0.1f : 0.0f)) });

    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.score > b.score; });

    snapshot.track.detectedType = candidates.empty() ? "unknown" : candidates.front().type;
    snapshot.track.confidence = candidates.empty() ? 0.0f : candidates.front().score;
    snapshot.track.alternatives.clear();

    for (size_t i = 1; i < candidates.size() && snapshot.track.alternatives.size() < 2; ++i)
        snapshot.track.alternatives.push_back(candidates[i].type + ":" + juce::String(candidates[i].score, 2));

    snapshot.track.effectiveType = snapshot.track.manualType == "auto" ? snapshot.track.detectedType : snapshot.track.manualType;
    snapshot.trackType = snapshot.track.effectiveType;
}

void MixTeacherAudioProcessor::updateDrumProfile(mixteacher::AnalysisSnapshot& snapshot)
{
    const auto lowWeight = juce::jmax(snapshot.bands.lowFoundation, snapshot.bands.lowBody);
    snapshot.drumProfile.kickWeight = juce::jlimit(0.0f, 1.0f, lowWeight);
    snapshot.drumProfile.kickClick = juce::jlimit(0.0f, 1.0f, snapshot.bands.presence);
    snapshot.drumProfile.snareBoxiness = juce::jlimit(0.0f, 1.0f, snapshot.bands.boxiness);
    snapshot.drumProfile.hatHarshness = juce::jlimit(0.0f, 1.0f, snapshot.bands.sibilance * 0.75f + snapshot.bands.air * 0.25f);
    snapshot.drumProfile.transientDensity = juce::jlimit(0.0f, 1.0f, snapshot.transientDensity * 0.65f + snapshot.transientScore * 0.35f);

    const auto crestRisk = snapshot.crestFactorDb > 0.0f ? juce::jlimit(0.0f, 1.0f, (9.0f - snapshot.crestFactorDb) / 7.0f) : 0.0f;
    const auto loudRisk = snapshot.rmsDbfs > -24.0f ? juce::jlimit(0.0f, 1.0f, (snapshot.rmsDbfs + 24.0f) / 12.0f) : 0.0f;
    snapshot.drumProfile.busCompressionRisk = juce::jlimit(0.0f, 1.0f, crestRisk * 0.65f + loudRisk * 0.35f);
}

void MixTeacherAudioProcessor::resetAnalysis()
{
    sampleFifo.reset();
    std::fill(analysisHistory.begin(), analysisHistory.end(), 0.0f);
    historyWriteIndex = 0;
    latestPeakLinear.store(0.0f, std::memory_order_relaxed);
    latestTruePeakLinear.store(0.0f, std::memory_order_relaxed);
    latestRmsLinear.store(0.0f, std::memory_order_relaxed);
    latestStereoCorrelation.store(1.0f, std::memory_order_relaxed);
    latestStereoWidth.store(0.0f, std::memory_order_relaxed);
    latestMonoFoldDownLossDb.store(0.0f, std::memory_order_relaxed);
    clippingCount.store(0, std::memory_order_relaxed);
    analysedSamples.store(0, std::memory_order_relaxed);
    previousTruePeakSample.fill(0.0f);
}

void MixTeacherAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void MixTeacherAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* MixTeacherAudioProcessor::createEditor()
{
    return new MixTeacherAudioProcessorEditor(*this);
}

void MixTeacherAudioProcessor::setCurrentProgram(int)
{
}

const juce::String MixTeacherAudioProcessor::getProgramName(int)
{
    return {};
}

void MixTeacherAudioProcessor::changeProgramName(int, const juce::String&)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixTeacherAudioProcessor();
}
