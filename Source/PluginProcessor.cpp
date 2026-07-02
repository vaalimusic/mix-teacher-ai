#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <algorithm>

namespace
{
float linearToDb(float value)
{
    return juce::Decibels::gainToDecibels(juce::jmax(value, 1.0e-6f), -120.0f);
}

float normaliseDb(float db, float floorDb = -72.0f, float ceilingDb = 0.0f)
{
    return juce::jlimit(0.0f, 1.0f, (db - floorDb) / (ceilingDb - floorDb));
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
} // namespace

MixTeacherAudioProcessor::MixTeacherAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    analysisHistory.resize(static_cast<size_t>(currentSampleRate * 3.0));
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

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "sensitivity", 1 },
        "Sensitivity",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f },
        0.65f));

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
    analysisHistory.assign(static_cast<size_t>(juce::jmax(fftSize, static_cast<int>(currentSampleRate * 3.0))), 0.0f);
    historyWriteIndex = 0;
    goodizerLowState.fill(0.0f);
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

    applyGoodizer(buffer);

    float peak = 0.0f;
    double sumSquares = 0.0;
    int blockClips = 0;
    const auto channelsToAnalyse = juce::jmax(1, totalNumInputChannels);
    const auto samplesToStore = juce::jmin(numSamples, maxBlockSamples);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float mono = 0.0f;

        for (int channel = 0; channel < totalNumInputChannels; ++channel)
        {
            const auto value = buffer.getReadPointer(channel)[sample];
            const auto absValue = std::abs(value);
            peak = juce::jmax(peak, absValue);
            sumSquares += static_cast<double>(value) * static_cast<double>(value);

            if (absValue >= 0.999f)
                ++blockClips;

            mono += value;
        }

        mono /= static_cast<float>(channelsToAnalyse);

        if (sample < samplesToStore)
            monoScratch[static_cast<size_t>(sample)] = mono;
    }

    const auto rms = std::sqrt(sumSquares / juce::jmax(1, numSamples * channelsToAnalyse));
    latestPeakLinear.store(peak, std::memory_order_relaxed);
    latestRmsLinear.store(static_cast<float>(rms), std::memory_order_relaxed);
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

    if (snapshot.activeRmsP50 > -119.0f)
        rms = juce::jmax(rms, juce::Decibels::decibelsToGain(snapshot.activeRmsP50));

    snapshot.peakDbfs = linearToDb(peak);
    snapshot.truePeakDbfs = snapshot.peakDbfs;
    snapshot.rmsDbfs = linearToDb(rms);
    snapshot.lufsShortTerm = snapshot.rmsDbfs - 0.7f;
    snapshot.crestFactorDb = juce::jmax(0.0f, snapshot.peakDbfs - snapshot.rmsDbfs);
    snapshot.headroomDb = juce::jmax(0.0f, -snapshot.peakDbfs);
}

juce::String MixTeacherAudioProcessor::getChoiceText(const char* parameterID) const
{
    if (const auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter(parameterID)))
        return parameter->getCurrentChoiceName();

    return {};
}

mixteacher::AnalysisSnapshot MixTeacherAudioProcessor::getLatestAnalysisSnapshot()
{
    drainFifo();

    mixteacher::AnalysisSnapshot snapshot;
    snapshot.track.manualType = normaliseTrackName(getChoiceText("trackType"));
    snapshot.explanationMode = getChoiceText("explanationMode").toLowerCase();
    snapshot.language = getChoiceText("language") == "English" ? "en" : "ru";
    if (auto* value = parameters.getRawParameterValue("sensitivity"))
        snapshot.sensitivity = value->load();
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
    return snapshot;
}

void MixTeacherAudioProcessor::updateSpectrum(mixteacher::AnalysisSnapshot& snapshot)
{
    if (analysisHistory.size() < fftSize)
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

    window.multiplyWithWindowingTable(fftBuffer.data(), fftSize);
    fft.performFrequencyOnlyForwardTransform(fftBuffer.data());

    auto bandEnergyDb = [this](float lowHz, float highHz)
    {
        const auto lowBin = juce::jlimit(1, fftSize / 2, static_cast<int>(lowHz * fftSize / currentSampleRate));
        const auto highBin = juce::jlimit(lowBin + 1, fftSize / 2, static_cast<int>(highHz * fftSize / currentSampleRate));

        float sum = 0.0f;
        for (int bin = lowBin; bin < highBin; ++bin)
            sum += fftBuffer[static_cast<size_t>(bin)];

        const auto average = sum / static_cast<float>(juce::jmax(1, highBin - lowBin));
        return linearToDb(average);
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
    std::vector<float> activeRmsValues;

    for (int bin = 0; bin < mixteacher::dynamicsBins; ++bin)
    {
        double sumSquares = 0.0;
        const auto newestOffset = (mixteacher::dynamicsBins - 1 - bin) * samplesPerBin;

        for (int i = 0; i < samplesPerBin; ++i)
        {
            const auto offset = static_cast<size_t>(newestOffset + i);
            const auto index = (historyWriteIndex + analysisHistory.size() - 1 - offset) % analysisHistory.size();
            const auto value = analysisHistory[index];
            sumSquares += static_cast<double>(value) * static_cast<double>(value);
        }

        const auto rms = std::sqrt(sumSquares / static_cast<double>(samplesPerBin));
        const auto db = linearToDb(static_cast<float>(rms));
        snapshot.dynamics[static_cast<size_t>(bin)] = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);

        if (db > -50.0f && db > snapshot.rmsDbfs - 30.0f)
            activeRmsValues.push_back(db);
    }

    snapshot.activeRmsP10 = percentile(activeRmsValues, 0.10f);
    snapshot.activeRmsP50 = percentile(activeRmsValues, 0.50f);
    snapshot.activeRmsP90 = percentile(activeRmsValues, 0.90f);
    snapshot.rmsRangeDb = juce::jmax(0.0f, snapshot.activeRmsP90 - snapshot.activeRmsP10);

    const auto transientSamples = juce::jmin(availableSamples, static_cast<int>(currentSampleRate * 0.75));
    double diffSum = 0.0;
    double ampSum = 0.0;

    for (int i = 1; i < transientSamples; ++i)
    {
        const auto indexA = (historyWriteIndex + analysisHistory.size() - static_cast<size_t>(i)) % analysisHistory.size();
        const auto indexB = (historyWriteIndex + analysisHistory.size() - static_cast<size_t>(i + 1)) % analysisHistory.size();
        const auto a = analysisHistory[indexA];
        const auto b = analysisHistory[indexB];
        diffSum += std::abs(a - b);
        ampSum += std::abs(a);

        if (std::abs(a - b) > 0.20f && std::abs(a) > 0.08f)
            ++snapshot.onsetCount;
    }

    if (transientSamples > 1 && ampSum > 1.0e-6)
        snapshot.transientScore = juce::jlimit(0.0f, 1.0f, static_cast<float>((diffSum / ampSum) * 0.65));
}

void MixTeacherAudioProcessor::updateValidity(mixteacher::AnalysisSnapshot& snapshot)
{
    const auto russian = snapshot.language == "ru";
    const auto thresholdShiftDb = (snapshot.sensitivity - 0.5f) * 18.0f;
    const auto usefulRmsDb = snapshot.activeRmsP50 > -119.0f ? snapshot.activeRmsP50 : snapshot.rmsDbfs;
    const auto tooQuietPeak = -35.0f - thresholdShiftDb;
    const auto tooQuietRms = -45.0f - thresholdShiftDb;
    const auto quietPeak = -18.0f - thresholdShiftDb;
    const auto quietRms = -30.0f - thresholdShiftDb;

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
    snapshot.validity.confidence = snapshot.analysisDurationSec > 2.0 ? 0.82f : 0.68f;
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
    snapshot.drumProfile.transientDensity = juce::jlimit(0.0f, 1.0f, snapshot.transientScore);

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
    latestRmsLinear.store(0.0f, std::memory_order_relaxed);
    clippingCount.store(0, std::memory_order_relaxed);
    analysedSamples.store(0, std::memory_order_relaxed);
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
